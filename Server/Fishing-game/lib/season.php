<?php
// Fishing Valley server: 3-day tournaments (Docs/UPDATE4_ONLINE.md §4, §11). A tournament lasts 3 REAL days (72 hours of
// server time, not game days). Seasons are lazy: a row appears when the first player syncs during the season; any
// request after the end closes every unclosed past season (atomic, in a transaction) and mails the rewards.
// Rewards are given PER CATEGORY (update 4b): in each of the 4 categories the participants with value > 0 are ranked,
// percentile = rank / N -> gold (<= 25 %), silver (<= 50 %), bronze (<= 75 %); a lone participant of a category gets gold.
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

require_once __DIR__ . '/validate.php';

const FV_SEASON_EPOCH = 1788210000;     // 2026-09-01 00:00 Europe/Moscow (UTC+3): tournament №0 starts
const FV_SEASON_LEN = 259200;           // 3 real days
/** Season board categories => season_stats column. */
const FV_SEASON_CATS = ['s_fish' => 'fish', 's_earned' => 'earned', 's_xp' => 'xp', 's_biggest' => 'best_size'];
/** Season category => suffix of rank_* / tier_* / n_* in sync.season.my. */
const FV_SEASON_KEYS = ['s_fish' => 'fish', 's_earned' => 'earned', 's_xp' => 'xp', 's_biggest' => 'biggest'];
const FV_SEASON_LABELS = ['s_fish' => 'Улов', 's_earned' => 'Заработок', 's_xp' => 'Опыт', 's_biggest' => 'Самая крупная рыба'];
const FV_TIER_NAMES = [0 => 'без награды', 1 => 'золото', 2 => 'серебро', 3 => 'бронза'];
/** s_earned item pools by level band: [min level, max level, item ids]. Tackle (fv_is_tackle) counts are capped at 2.
    11–15 (update 6, Docs/UPDATE6_FOREST_CITY.md §9): expert-zone materials and tackle (only update-6 clients reach 11). */
const FV_EARNED_POOLS = [
	[1, 3, ['mat_board', 'mat_rope', 'mat_glue', 'res_resin', 'res_feather']],
	[4, 6, ['mat_glitter', 'mat_sinker', 'mat_glue', 'mat_rope', 'float_feather']],
	[7, 10, ['mat_glitter', 'float_sliding', 'line_braided', 'mat_glue', 'net_small']],
	[11, 15, ['mat_log', 'mat_mpk', 'mat_nails', 'eline_thin', 'eline_heavy', 'efloat_surface', 'efloat_deep']],
];
/** s_fish from level 11 (update 6): the expert bait «Глубинный червь» on top of the classic baits, by tier. */
const FV_EXPERT_BAIT = 'bait_deepworm';
const FV_EXPERT_BAIT_FROM_LEVEL = 11;
const FV_EXPERT_BAIT_COUNTS = [1 => 5, 2 => 3, 3 => 2];
/** Every item id a tournament can give (checked against lib/fish.php FV_ITEMS by the tests and gen_fish.php). */
const FV_SEASON_REWARD_ITEMS = ['bait_magic', 'bait_lure', 'bait_nightcrawler', 'bait_grub', 'mat_board', 'mat_rope', 'mat_glue',
	'res_resin', 'res_feather', 'mat_glitter', 'mat_sinker', 'float_feather', 'float_sliding', 'line_braided', 'net_small',
	'bait_deepworm', 'mat_log', 'mat_mpk', 'mat_nails', 'eline_thin', 'eline_heavy', 'efloat_surface', 'efloat_deep'];

function fv_season_id(int $now): int
{
	return (int)floor(($now - FV_SEASON_EPOCH) / FV_SEASON_LEN);
}

/** [starts, ends] of a season (unix). */
function fv_season_bounds(int $id): array
{
	$s = FV_SEASON_EPOCH + $id * FV_SEASON_LEN;
	return [$s, $s + FV_SEASON_LEN];
}

function fv_season_info(int $now): array
{
	$id = fv_season_id($now);
	[$s, $e] = fv_season_bounds($id);
	return ['id' => $id, 'starts' => $s, 'ends' => $e];
}

/** "Турнир №8". */
function fv_season_name(int $id): string
{
	return $id >= 0 ? 'Турнир №' . $id : 'Пробный турнир';
}

/** "25.09 – 27.09" (the last day of the tournament, not the exclusive end). */
function fv_season_dates(int $id): string
{
	[$s, $e] = fv_season_bounds($id);
	return date('d.m', $s) . ' – ' . date('d.m', $e - 1);
}

function fv_season_ensure(int $id): void
{
	[$s, $e] = fv_season_bounds($id);
	fv_q(fv_insert_ignore_sql() . ' INTO seasons (id, starts, ends, closing, closed, results_json) VALUES (?, ?, ?, 0, NULL, NULL)',
		[$id, $s, $e]);
}

/**
 * Tier by rank among N: 1 gold (top 25 %), 2 silver (50 %), 3 bronze (75 %), 0 none. Integer maths (no rounding issues).
 * The only participant of a category (rank 1 of 1) gets gold.
 */
function fv_tier_for_rank(int $rank, int $n): int
{
	if ($rank <= 0 || $n <= 0 || $rank > $n) { return 0; }
	if ($n === 1) { return 1; }
	if ($rank * 4 <= $n) { return 1; }
	if ($rank * 2 <= $n) { return 2; }
	if ($rank * 4 <= $n * 3) { return 3; }
	return 0;
}

/** ceil(a / b) for positive integers. */
function fv_ceil_div(int $a, int $b): int
{
	return intdiv($a + $b - 1, $b);
}

/** XP from the start of level L to L+1 (FVDB XpForLevel); at the top level the last step (15: 10 400). */
function fv_level_span(int $level): int
{
	$l = max(1, min(FV_MAX_LEVEL, $level));
	if ($l >= FV_MAX_LEVEL) { return FV_XP_TABLE[FV_MAX_LEVEL] - FV_XP_TABLE[FV_MAX_LEVEL - 1]; }
	return FV_XP_TABLE[$l + 1] - FV_XP_TABLE[$l];
}

/** round(span * pct / 100), halves rounded up. */
function fv_pct_of(int $span, int $pct): int
{
	return intdiv($span * $pct + 50, 100);
}

function fv_is_tackle(string $id): bool
{
	return preg_match('/^(float|line|net|rod|efloat|eline|reel)_/', $id) === 1;
}

/** The s_earned pool of a level band. */
function fv_earned_pool(int $level): array
{
	$l = max(1, min(FV_MAX_LEVEL, $level));
	foreach (FV_EARNED_POOLS as [$lo, $hi, $ids]) {
		if ($l >= $lo && $l <= $hi) { return $ids; }
	}
	return FV_EARNED_POOLS[0][2];
}

/** s_earned: [number of item kinds, count of each] for a tier (1..3) at level L. */
function fv_earned_counts(int $tier, int $level): array
{
	$l = max(1, min(FV_MAX_LEVEL, $level));
	return [1 => [3, 4 + $l], 2 => [2, 2 + fv_ceil_div($l, 2)], 3 => [1, 1 + fv_ceil_div($l, 4)]][$tier] ?? [0, 0];
}

/** $k distinct item kinds from the level's pool, chosen deterministically by (season, player). */
function fv_earned_kinds(int $sid, int $pid, int $level, int $k): array
{
	$pool = fv_earned_pool($level);
	$keyed = [];
	foreach ($pool as $id) { $keyed[$id] = sprintf('%08x', crc32($sid . ':' . $pid . ':' . $id)) . $id; }
	asort($keyed, SORT_STRING);
	return array_slice(array_keys($keyed), 0, max(0, min($k, count($pool))));
}

/**
 * The reward of one season category for a tier at fishing level L (Docs/UPDATE4_ONLINE.md §4 table):
 * ['coins' => int, 'xp' => int, 'items' => [['id', 'n'], ...]]. $sid / $pid seed the s_earned item choice.
 */
function fv_season_cat_reward(string $cat, int $tier, int $level, int $sid = 0, int $pid = 0): array
{
	$l = max(1, min(FV_MAX_LEVEL, $level));
	$r = ['coins' => 0, 'xp' => 0, 'items' => []];
	if ($tier < 1 || $tier > 3) { return $r; }
	switch ($cat) {
		case 's_fish':          // good baits
			$r['items'] = [
				1 => [['id' => 'bait_magic', 'n' => 3 + fv_ceil_div($l, 2)], ['id' => 'bait_lure', 'n' => 10]],
				2 => [['id' => 'bait_lure', 'n' => 4 + fv_ceil_div($l, 2)], ['id' => 'bait_nightcrawler', 'n' => 10]],
				3 => [['id' => 'bait_grub', 'n' => 10]],
			][$tier];
			if ($l >= FV_EXPERT_BAIT_FROM_LEVEL) { $r['items'][] = ['id' => FV_EXPERT_BAIT, 'n' => FV_EXPERT_BAIT_COUNTS[$tier]]; }
			break;
		case 's_earned':        // useful items, the amount grows with the level and the place
			[$kinds, $n] = fv_earned_counts($tier, $l);
			foreach (fv_earned_kinds($sid, $pid, $l, $kinds) as $id) {
				$r['items'][] = ['id' => $id, 'n' => fv_is_tackle($id) ? min(2, $n) : $n];
			}
			break;
		case 's_xp':            // coins by level
			$r['coins'] = [1 => 300 + 200 * $l, 2 => 150 + 100 * $l, 3 => 50 + 30 * $l][$tier];
			break;
		case 's_biggest':       // XP by level: a share of the current level's span
			$span = fv_level_span($l);
			$r['xp'] = [1 => max(150, fv_pct_of($span, 30)), 2 => max(100, fv_pct_of($span, 15)), 3 => max(50, fv_pct_of($span, 6))][$tier];   // per-tier minimums: gold always beats silver beats bronze
			break;
	}
	return $r;
}

/** Sum of several rewards (items with the same id merged, order kept). */
function fv_reward_merge(array $rewards): array
{
	$out = ['coins' => 0, 'xp' => 0, 'items' => []];
	$idx = [];
	foreach ($rewards as $r) {
		$out['coins'] += (int)($r['coins'] ?? 0);
		$out['xp'] += (int)($r['xp'] ?? 0);
		foreach ((array)($r['items'] ?? []) as $it) {
			$id = (string)$it['id'];
			if (isset($idx[$id])) { $out['items'][$idx[$id]]['n'] += (int)$it['n']; continue; }
			$idx[$id] = count($out['items']);
			$out['items'][] = ['id' => $id, 'n' => (int)$it['n']];
		}
	}
	return $out;
}

/** Russian plural: fv_plural(5, 'монета', 'монеты', 'монет'). */
function fv_plural(int $n, string $one, string $few, string $many): string
{
	$n = abs($n) % 100;
	if ($n >= 11 && $n <= 14) { return $many; }
	$d = $n % 10;
	if ($d === 1) { return $one; }
	if ($d >= 2 && $d <= 4) { return $few; }
	return $many;
}

/** "Волшебная наживка ×5, Блесна ×10" / "700 монет" / "117 опыта"; '' for an empty reward. */
function fv_reward_text(array $r): string
{
	$parts = [];
	foreach ((array)($r['items'] ?? []) as $it) { $parts[] = fv_item_name((string)$it['id']) . ' ×' . (int)$it['n']; }
	if ((int)($r['coins'] ?? 0) > 0) { $c = (int)$r['coins']; $parts[] = $c . ' ' . fv_plural($c, 'монета', 'монеты', 'монет'); }
	if ((int)($r['xp'] ?? 0) > 0) { $parts[] = (int)$r['xp'] . ' опыта'; }
	return implode(', ', $parts);
}

/**
 * Full standings of a season: participants (season fish > 0, not banned), their rank and tier in every category
 * (among the participants with value > 0; ties -> earlier updated_at, then lower id), counts per category.
 * Each player: ranks[cat], tiers[cat], tier (best), awards (number of categories with a tier), best_pct.
 */
function fv_season_standings(int $sid): array
{
	$rows = fv_all('SELECT s.*, p.name, p.level FROM season_stats s JOIN players p ON p.id = s.player_id
		WHERE s.season_id = ? AND s.fish > 0 AND p.banned = 0', [$sid]);
	$counts = [];
	$byPid = [];
	foreach ($rows as $r) {
		$pid = (int)$r['player_id'];
		$byPid[$pid] = [
			'pid' => $pid, 'name' => (string)$r['name'], 'tag' => 1000 + $pid, 'level' => (int)$r['level'],
			'fish' => (int)$r['fish'], 'earned' => (int)$r['earned'], 'xp' => (int)$r['xp'],
			'best_size' => round((float)$r['best_size'], 1), 'best_fish' => (string)$r['best_fish'], 'best_q' => (int)$r['best_q'],
			'ranks' => array_fill_keys(array_keys(FV_SEASON_CATS), 0), 'tiers' => array_fill_keys(array_keys(FV_SEASON_CATS), 0),
			'tier' => 0, 'awards' => 0, 'best_pct' => 0.0, 'updated_at' => (int)$r['updated_at'],
		];
	}
	foreach (FV_SEASON_CATS as $cat => $col) {
		$list = array_values(array_filter($rows, static fn($r) => (float)$r[$col] > 0));
		usort($list, static function ($a, $b) use ($col) {
			$va = (float)$a[$col]; $vb = (float)$b[$col];
			if ($va !== $vb) { return $va < $vb ? 1 : -1; }
			if ((int)$a['updated_at'] !== (int)$b['updated_at']) { return (int)$a['updated_at'] <=> (int)$b['updated_at']; }
			return (int)$a['player_id'] <=> (int)$b['player_id'];
		});
		$n = count($list);
		$counts[$cat] = $n;
		foreach ($list as $i => $r) {
			$pid = (int)$r['player_id'];
			$byPid[$pid]['ranks'][$cat] = $i + 1;
			$byPid[$pid]['tiers'][$cat] = fv_tier_for_rank($i + 1, $n);
		}
	}
	foreach ($byPid as &$p) {
		$best = 0.0;
		foreach (FV_SEASON_CATS as $cat => $col) {
			$rank = $p['ranks'][$cat];
			if ($rank <= 0) { continue; }
			$pct = $rank / $counts[$cat];
			if ($best === 0.0 || $pct < $best) { $best = $pct; }
			$t = $p['tiers'][$cat];
			if ($t > 0) {
				$p['awards']++;
				if ($p['tier'] === 0 || $t < $p['tier']) { $p['tier'] = $t; }
			}
		}
		$p['best_pct'] = round($best, 4);
	}
	unset($p);
	$list = array_values($byPid);
	$key = static function (array $p): array {
		$g = array_count_values(array_values($p['tiers']));
		return [$p['tier'] === 0 ? 9 : $p['tier'], -($g[1] ?? 0), -($g[2] ?? 0), -($g[3] ?? 0), $p['best_pct'], -$p['fish'], $p['pid']];
	};
	usort($list, static fn($a, $b) => $key($a) <=> $key($b));
	return ['participants' => count($list), 'counts' => $counts, 'players' => $list];
}

/** Closes every season that has ended and is not closed yet. Cheap when there is nothing to do. */
function fv_season_close_due(int $now): void
{
	$rows = fv_all('SELECT id, starts FROM seasons WHERE closed IS NULL AND closing = 0 AND ends <= ? ORDER BY id', [$now]);
	foreach ($rows as $r) {
		// a row numbered with the old update-4 epoch waits for install.php to renumber it (fv_season_migrate)
		if ((int)$r['starts'] !== fv_season_bounds((int)$r['id'])[0]) { continue; }
		fv_season_close((int)$r['id'], $now);
	}
}

/** The season letter of one player: [title, body, total reward, per-category rewards]. */
function fv_season_letter(int $sid, array $p, array $counts, int $participants): array
{
	$rewards = [];
	$lines = [];
	$won = 0;
	foreach (FV_SEASON_LABELS as $cat => $label) {
		$rank = (int)($p['ranks'][$cat] ?? 0);
		$tier = (int)($p['tiers'][$cat] ?? 0);
		$rw = fv_season_cat_reward($cat, $tier, (int)$p['level'], $sid, (int)$p['pid']);
		$rewards[$cat] = $rw;
		if ($rank <= 0) { $lines[] = $label . ': нет результата'; continue; }
		$line = $label . ': ' . $rank . ' место из ' . (int)($counts[$cat] ?? 0) . ' — ' . FV_TIER_NAMES[$tier];
		if ($tier > 0) { $line .= ': ' . fv_reward_text($rw); $won++; }
		$lines[] = $line;
	}
	$title = $sid >= 0 ? 'Итоги турнира №' . $sid : 'Итоги пробного турнира';
	$body = fv_season_name($sid) . ' (' . fv_season_dates($sid) . ') завершён! Участников: ' . $participants . '.' . "\n\n"
		. implode("\n", $lines) . "\n\n"
		. 'Награды в ' . $won . ' ' . fv_plural($won, 'категории', 'категориях', 'категориях') . ' из 4 — они во вложении.' . "\n"
		. 'Спасибо за участие! Новый турнир уже начался — удачного клёва!';
	return [$title, $body, fv_reward_merge(array_values($rewards)), $rewards];
}

/** Closes one season: ranks, tiers, reward letters, results_json. Returns false if another request closes it. */
function fv_season_close(int $sid, int $now): bool
{
	$db = fv_db();
	$db->beginTransaction();
	try {
		// the atomic flag: only one request can switch closing 0 -> 1 (the row stays locked until commit)
		$n = fv_q('UPDATE seasons SET closing = 1 WHERE id = ? AND closing = 0 AND closed IS NULL AND ends <= ?', [$sid, $now])->rowCount();
		if ($n !== 1) { $db->rollBack(); return false; }
		$st = fv_season_standings($sid);
		[$starts, $ends] = fv_season_bounds($sid);
		$tiers = [1 => 0, 2 => 0, 3 => 0];
		$byCat = array_fill_keys(array_keys(FV_SEASON_CATS), [1 => 0, 2 => 0, 3 => 0]);
		$rewarded = 0;
		foreach ($st['players'] as &$p) {
			[$title, $body, $total, $rewards] = fv_season_letter($sid, $p, $st['counts'], $st['participants']);
			$p['rewards'] = $rewards;
			$p['reward'] = $total;
			if ($p['awards'] === 0) { continue; }
			foreach ($p['tiers'] as $cat => $t) {
				if ($t > 0) { $tiers[$t]++; $byCat[$cat][$t]++; }
			}
			$rewarded++;
			fv_mail_insert((int)$p['pid'], 'season', $title, $body, $total['coins'], $total['xp'], 0, $total['items'], $now);
		}
		unset($p);
		$results = ['id' => $sid, 'starts' => $starts, 'ends' => $ends, 'closed' => $now, 'participants' => $st['participants'],
			'counts' => $st['counts'], 'tiers' => $tiers, 'tiers_by_cat' => $byCat, 'rewarded' => $rewarded, 'players' => $st['players']];
		fv_q('UPDATE seasons SET closed = ?, closing = 0, results_json = ? WHERE id = ?',
			[$now, json_encode($results, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), $sid]);
		$db->commit();
		return true;
	} catch (Throwable $e) {
		if ($db->inTransaction()) { $db->rollBack(); }
		fv_log_error('season close ' . $sid . ': ' . $e->getMessage());
		return false;
	}
}

/**
 * Renumbers season rows created with another epoch (update 4 used 28.09.2026; now 01.09.2026 — the same 3-day grid,
 * ids shift by +9). Called by install.php; safe to run again. Returns the number of moved seasons.
 */
function fv_season_migrate(): int
{
	$moved = 0;
	$db = fv_db();
	foreach (fv_all('SELECT id, starts, closed, results_json FROM seasons ORDER BY id DESC') as $r) {
		$old = (int)$r['id'];
		$starts = (int)$r['starts'];
		$new = fv_season_id($starts);
		if ($new === $old || fv_season_bounds($new)[0] !== $starts) { continue; }
		$res = $r['results_json'] !== null ? json_decode((string)$r['results_json'], true) : null;
		if (is_array($res)) { $res['id'] = $new; }
		$resJson = is_array($res) ? json_encode($res, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES) : $r['results_json'];
		$db->beginTransaction();
		try {
			$exists = fv_row('SELECT id, closed FROM seasons WHERE id = ?', [$new]);
			if ($exists) {
				// the new numbering already runs this season: keep its rows, add the players only the old row knows
				fv_q('UPDATE season_stats SET season_id = ? WHERE season_id = ? AND player_id NOT IN
					(SELECT player_id FROM (SELECT player_id FROM season_stats WHERE season_id = ?) t)', [$new, $old, $new]);
				fv_q('DELETE FROM season_stats WHERE season_id = ?', [$old]);
				if ($r['closed'] !== null && $exists['closed'] === null) {   // rewards were already sent: never twice
					fv_q('UPDATE seasons SET closed = ?, closing = 0, results_json = ? WHERE id = ?', [(int)$r['closed'], $resJson, $new]);
				}
				fv_q('DELETE FROM seasons WHERE id = ?', [$old]);
			} else {
				fv_q('UPDATE seasons SET id = ?, results_json = ? WHERE id = ?', [$new, $resJson, $old]);
				fv_q('UPDATE season_stats SET season_id = ? WHERE season_id = ?', [$new, $old]);
			}
			$db->commit();
			$moved++;
		} catch (Throwable $e) {
			if ($db->inTransaction()) { $db->rollBack(); }
			throw $e;
		}
	}
	return $moved;
}

/** Inserts a letter. $items = [['id' => 'mat_glitter', 'n' => 2], ...]. Returns the mail id. */
function fv_mail_insert(int $pid, string $kind, string $title, string $body, int $coins, int $xp, int $setLevel, array $items, int $now): int
{
	fv_q('INSERT INTO mail (player_id, kind, title, body, coins, xp, set_level, items_json, created, claimed)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NULL)', [
		$pid, $kind, mb_substr($title, 0, 100), mb_substr($body, 0, 4000), max(0, $coins), max(0, $xp),
		max(0, min(FV_MAX_LEVEL, $setLevel)), json_encode(array_values($items), JSON_UNESCAPED_UNICODE), $now]);
	return (int)fv_db()->lastInsertId();
}

/**
 * The player's live standing in the current season (for sync): rank_* / tier_* / n_* per category (fish, earned, xp,
 * biggest; 0 = not ranked), best_pct, tier (best of the four, kept for old clients) and the season values.
 */
function fv_season_my(int $sid, int $pid, bool $banned): array
{
	$participants = (int)fv_val('SELECT COUNT(*) FROM season_stats s JOIN players p ON p.id = s.player_id
		WHERE s.season_id = ? AND s.fish > 0 AND p.banned = 0', [$sid]);
	$my = [];
	foreach (['rank_', 'tier_', 'n_'] as $pre) {
		foreach (FV_SEASON_KEYS as $k) { $my[$pre . $k] = 0; }
	}
	$my += ['best_pct' => 0, 'tier' => 0, 'fish' => 0, 'earned' => 0, 'xp' => 0, 'biggest' => 0.0, 'biggest_fish' => ''];
	$row = fv_row('SELECT * FROM season_stats WHERE season_id = ? AND player_id = ?', [$sid, $pid]);
	if ($row) {
		$my['fish'] = (int)$row['fish'];
		$my['earned'] = (int)$row['earned'];
		$my['xp'] = (int)$row['xp'];
		$my['biggest'] = round((float)$row['best_size'], 1);
		$my['biggest_fish'] = (string)$row['best_fish'];
	}
	$base = ' FROM season_stats s JOIN players p ON p.id = s.player_id WHERE s.season_id = ? AND s.fish > 0 AND p.banned = 0 AND s.';
	foreach (FV_SEASON_CATS as $cat => $col) {
		$my['n_' . FV_SEASON_KEYS[$cat]] = (int)fv_val('SELECT COUNT(*)' . $base . $col . ' > 0', [$sid]);
	}
	if ($row && !$banned && (int)$row['fish'] > 0) {
		$best = 0.0; $tier = 0;
		foreach (FV_SEASON_CATS as $cat => $col) {
			$v = $col === 'best_size' ? (float)$row[$col] : (int)$row[$col];
			if ($v <= 0) { continue; }
			$k = FV_SEASON_KEYS[$cat];
			$n = $my['n_' . $k];
			$u = (int)$row['updated_at'];
			$rank = 1 + (int)fv_val('SELECT COUNT(*)' . $base . $col . ' > 0 AND (s.' . $col . ' > ? OR (s.' . $col . ' = ? AND (s.updated_at < ? OR (s.updated_at = ? AND s.player_id < ?))))',
				[$sid, $v, $v, $u, $u, $pid]);
			$t = fv_tier_for_rank($rank, $n);
			$my['rank_' . $k] = $rank;
			$my['tier_' . $k] = $t;
			$pct = $n > 0 ? $rank / $n : 1.0;
			if ($best === 0.0 || $pct < $best) { $best = $pct; }
			if ($t > 0 && ($tier === 0 || $t < $tier)) { $tier = $t; }
		}
		$my['best_pct'] = round($best, 4);
		$my['tier'] = $tier;
	}
	return ['participants' => $participants, 'my' => $my];
}
