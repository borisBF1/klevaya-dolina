<?php
// Fishing Valley server: leaderboards (all-time and the current tournament), ranks and the player search.
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

require_once __DIR__ . '/season.php';

/** Board categories: column, season flag, Russian label (admin). */
const FV_BOARD = [
	'level' => ['col' => 'xp', 'season' => false, 'label' => 'Уровень (опыт)'],
	'fish' => ['col' => 'fish_total', 'season' => false, 'label' => 'Всего рыбы'],
	'biggest' => ['col' => 'best_size', 'season' => false, 'label' => 'Самая крупная'],
	'legend' => ['col' => 'legendary', 'season' => false, 'label' => 'Легендарные'],
	'species' => ['col' => 'species', 'season' => false, 'label' => 'Виды'],
	'earned' => ['col' => 'coins_earned', 'season' => false, 'label' => 'Заработано'],
	'achievements' => ['col' => 'achievements', 'season' => false, 'label' => 'Достижения'],
	's_fish' => ['col' => 'fish', 'season' => true, 'label' => 'Турнир: улов'],
	's_earned' => ['col' => 'earned', 'season' => true, 'label' => 'Турнир: заработок'],
	's_xp' => ['col' => 'xp', 'season' => true, 'label' => 'Турнир: опыт'],
	's_biggest' => ['col' => 'best_size', 'season' => true, 'label' => 'Турнир: крупнейшая'],
];
const FV_BOARD_ALIASES = ['xp' => 'level', 'fish_total' => 'fish', 'legendary' => 'legend', 'coins_earned' => 'earned',
	'size' => 'biggest', 'season_fish' => 's_fish', 'season_earned' => 's_earned', 'season_xp' => 's_xp',
	'season_biggest' => 's_biggest'];
const FV_BOARD_TO_SEASON = ['fish' => 's_fish', 'earned' => 's_earned', 'level' => 's_xp', 'biggest' => 's_biggest'];

/** Normalises the category ("all"/"season" period may map an all-time name to the tournament one). */
function fv_board_cat($cat, $period): ?string
{
	if (!is_string($cat)) { return null; }
	$cat = strtolower(trim($cat));
	$cat = FV_BOARD_ALIASES[$cat] ?? $cat;
	if ($period === 'season' && isset(FV_BOARD_TO_SEASON[$cat])) { $cat = FV_BOARD_TO_SEASON[$cat]; }
	return isset(FV_BOARD[$cat]) ? $cat : null;
}

/** SQL parts of a board: from/where (+ params) and the value / tie-break / id / fish expressions. */
function fv_board_sql(string $cat, int $sid): array
{
	$def = FV_BOARD[$cat];
	$col = $def['col'];
	if ($def['season']) {
		return [
			'from' => ' FROM season_stats s JOIN players p ON p.id = s.player_id',
			'where' => ' WHERE s.season_id = ? AND s.fish > 0 AND p.banned = 0 AND s.' . $col . ' > 0',
			'params' => [$sid],
			'v' => 's.' . $col, 'u' => 's.updated_at', 'id' => 's.player_id',
			'fish' => 's.best_fish', 'size' => 's.best_size', 'q' => 's.best_q',
		];
	}
	return [
		'from' => ' FROM players p',
		'where' => ' WHERE p.banned = 0' . ($cat === 'level' ? '' : ' AND p.' . $col . ' > 0'),
		'params' => [],
		'v' => 'p.' . $col, 'u' => 'p.updated_at', 'id' => 'p.id',
		'fish' => 'p.best_fish', 'size' => 'p.best_size', 'q' => 'p.best_q',
	];
}

/** Search: digits (optionally "#") -> the tag; otherwise a case-insensitive name substring. */
function fv_search_sql($q): array
{
	$q = is_string($q) ? fv_collapse($q) : '';
	if ($q === '') { return ['', []]; }
	if (preg_match('/^#?\s*(\d{1,10})$/', $q, $m)) {
		return [' AND p.id = ?', [(int)$m[1] - 1000]];
	}
	$q = mb_strtolower(mb_substr($q, 0, 32));
	$like = '%' . strtr($q, ['!' => '!!', '%' => '!%', '_' => '!_']) . '%';
	return [" AND p.name_lc LIKE ? ESCAPE '!'", [$like]];
}

/** 1-based rank of a value inside a board (0 if not ranked). */
function fv_board_rank(array $b, $v, int $u, int $id): int
{
	return 1 + (int)fv_val('SELECT COUNT(*)' . $b['from'] . $b['where'] . ' AND (' . $b['v'] . ' > ? OR (' . $b['v'] . ' = ? AND ('
		. $b['u'] . ' < ? OR (' . $b['u'] . ' = ? AND ' . $b['id'] . ' < ?))))',
		array_merge($b['params'], [$v, $v, $u, $u, $id]));
}

function fv_board_select(array $b): string
{
	return 'SELECT p.id AS pid, p.name, p.level, ' . $b['v'] . ' AS v, ' . $b['u'] . ' AS u, ' . $b['fish'] . ' AS bf, '
		. $b['size'] . ' AS bs, ' . $b['q'] . ' AS bq';
}

function fv_board_entry(string $cat, array $r, int $rank, int $mePid): array
{
	$big = FV_BOARD[$cat]['col'] === 'best_size';
	$pid = (int)$r['pid'];
	return [
		'rank' => $rank, 'pid' => $pid, 'name' => (string)$r['name'], 'tag' => 1000 + $pid, 'level' => (int)$r['level'],
		'value' => $big ? (int)round((float)$r['v'] * 10) : (int)$r['v'],
		'fish' => $big ? (string)$r['bf'] : '', 'size' => $big ? round((float)$r['bs'], 1) : 0,
		'q' => $big ? (int)$r['bq'] : 0, 'me' => $mePid > 0 && $pid === $mePid,
	];
}

/** A board page: total, entries, me (the given pid's row, ignoring the search) — for the API and the admin. */
function fv_board(string $cat, string $q, int $offset, int $limit, int $mePid, int $now): array
{
	$sid = fv_season_id($now);
	$b = fv_board_sql($cat, $sid);
	[$sw, $sp] = fv_search_sql($q);
	$total = (int)fv_val('SELECT COUNT(*)' . $b['from'] . $b['where'] . $sw, array_merge($b['params'], $sp));
	$rows = fv_all(fv_board_select($b) . $b['from'] . $b['where'] . $sw . ' ORDER BY ' . $b['v'] . ' DESC, ' . $b['u'] . ' ASC, '
		. $b['id'] . ' ASC LIMIT ' . $limit . ' OFFSET ' . $offset, array_merge($b['params'], $sp));
	$entries = [];
	foreach ($rows as $i => $r) {
		$rank = $sw === '' ? $offset + $i + 1 : fv_board_rank($b, FV_BOARD[$cat]['col'] === 'best_size' ? (float)$r['v'] : (int)$r['v'], (int)$r['u'], (int)$r['pid']);
		$entries[] = fv_board_entry($cat, $r, $rank, $mePid);
	}
	$me = null;
	if ($mePid > 0) {
		$r = fv_row(fv_board_select($b) . $b['from'] . $b['where'] . ' AND p.id = ?', array_merge($b['params'], [$mePid]));
		if ($r) {
			$rank = fv_board_rank($b, FV_BOARD[$cat]['col'] === 'best_size' ? (float)$r['v'] : (int)$r['v'], (int)$r['u'], (int)$r['pid']);
			$me = fv_board_entry($cat, $r, $rank, $mePid);
		}
	}
	return ['total' => $total, 'entries' => $entries, 'me' => $me];
}

/** All-time ranks of a player row (0 = not ranked / banned). */
function fv_player_ranks(array $p): array
{
	$keys = ['level', 'fish', 'biggest', 'legend', 'species', 'earned', 'achievements'];
	$out = array_fill_keys($keys, 0);
	if ((int)$p['banned'] === 1) { return $out; }
	foreach ($keys as $cat) {
		$col = FV_BOARD[$cat]['col'];
		$v = $col === 'best_size' ? (float)$p[$col] : (int)$p[$col];
		if ($cat !== 'level' && $v <= 0) { continue; }
		$out[$cat] = fv_board_rank(fv_board_sql($cat, 0), $v, (int)$p['updated_at'], (int)$p['id']);
	}
	return $out;
}
