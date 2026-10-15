<?php
// Fishing Valley server: input validation — names / statuses (with the profanity filter), numbers, fish,
// and the plausibility checks of synced stats (basic anti-cheat, Docs/UPDATE4_ONLINE.md §3).
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

require_once __DIR__ . '/fish.php';

const FV_NAME_MAX = 16;
const FV_STATUS_MAX = 40;
const FV_MAX_LEVEL = 15;                  // update 6 (Docs/UPDATE6_FOREST_CITY.md §9): levels 11–15 in the expert zone
const FV_CLASSIC_MAX_LEVEL = 10;          // the level cap of clients before update 6 (and the XP freeze until the City)
const FV_MAX_STATIONS = 100;
const FV_SIZE_TOLERANCE = 0.5;
// Caps counted from the game data (lib/fish.php) with a reserve, so a few new species / achievements of a later client
// are not taken for cheating before lib/fish.php is regenerated.
define('FV_MAX_SPECIES', count(FV_FISH) + 10);
define('FV_MAX_ACHIEVEMENTS', FV_ACHIEVEMENTS_TOTAL + 15);
/** Client build of update 6 (0.6): the download page shows the update-6 content from it on. */
const FV_U6_CLIENT_BUILD = 7;

// ------------------------------------------------------------------ numbers

/** An integer from JSON (int, float or numeric string) within [min, max], or null. */
function fv_int($v, int $min = PHP_INT_MIN, int $max = PHP_INT_MAX): ?int
{
	if (is_bool($v) || $v === null) { return null; }
	if (is_string($v)) { $v = trim($v); if ($v === '' || !is_numeric($v)) { return null; } $v = $v + 0; }
	if (is_float($v)) {
		if (!is_finite($v) || $v > 9.0e18 || $v < -9.0e18) { return null; }
		$v = (int)round($v);
	}
	if (!is_int($v) || $v < $min || $v > $max) { return null; }
	return $v;
}

function fv_float($v): ?float
{
	if (is_bool($v) || $v === null) { return null; }
	if (is_string($v)) { $v = trim($v); if (!is_numeric($v)) { return null; } }
	if (!is_int($v) && !is_float($v) && !is_string($v)) { return null; }
	$f = (float)$v;
	return is_finite($f) ? $f : null;
}

/** Level for a total XP; $cap = the client's top level (0.5 clients stop at 10 while their XP keeps growing). */
function fv_level_for_xp(int $xp, int $cap = FV_MAX_LEVEL): int
{
	for ($l = max(1, min(FV_MAX_LEVEL, $cap)); $l > 1; --$l) {
		if ($xp >= FV_XP_TABLE[$l]) { return $l; }
	}
	return 1;
}

/** The top level of the syncing client: stats.max_level (update 6) or 10 (clients 0.4–0.5 do not send it). */
function fv_client_level_cap(array $stats): int
{
	$cap = array_key_exists('max_level', $stats) ? fv_int($stats['max_level'], 1, 1000) : null;
	return $cap === null ? FV_CLASSIC_MAX_LEVEL : max(FV_CLASSIC_MAX_LEVEL, min(FV_MAX_LEVEL, $cap));
}

/** A JSON flag (true / 1 / "1"). */
function fv_flag($v): bool
{
	return $v === true || $v === 1 || $v === '1' || $v === 1.0;
}

function fv_xp_for_level(int $level): int
{
	return FV_XP_TABLE[max(1, min(FV_MAX_LEVEL, $level))];
}

/** Game ids: fish / items / cosmetics / locations ("fish_carp", "hat_crown"). */
function fv_is_game_id($s): bool
{
	return is_string($s) && preg_match('/^[a-z0-9_]{1,40}$/', $s) === 1;
}

// ------------------------------------------------------------------ text

/** Trims, turns any whitespace run into one space. */
function fv_collapse(string $s): string
{
	$s = preg_replace('/[\p{Z}\s]+/u', ' ', $s) ?? '';
	return trim($s);
}

/** Normalised name or null (→ bad_name): 1..16 letters of any alphabet, digits, space, "_", "-"; no profanity. */
function fv_normalize_name($raw): ?string
{
	if (!is_string($raw) || !mb_check_encoding($raw, 'UTF-8') || strlen($raw) > 200) { return null; }
	if (class_exists('Normalizer')) { $n = Normalizer::normalize($raw, Normalizer::FORM_C); if (is_string($n)) { $raw = $n; } }
	$s = fv_collapse($raw);
	$len = mb_strlen($s);
	if ($len < 1 || $len > FV_NAME_MAX) { return null; }
	if (preg_match('/^[\p{L}\p{M}0-9 _\-]+$/u', $s) !== 1) { return null; }
	if (preg_match('/[\p{L}0-9]/u', $s) !== 1) { return null; }          // not only separators
	if (preg_match('/\p{M}{3,}/u', $s) === 1) { return null; }           // "zalgo" stacks
	if (fv_is_profane($s)) { return null; }
	return $s;
}

/** Normalised status/motto or null (→ bad_status): up to 40 chars of letters, digits, punctuation; no profanity. */
function fv_normalize_status($raw): ?string
{
	if (!is_string($raw) || !mb_check_encoding($raw, 'UTF-8') || strlen($raw) > 400) { return null; }
	if (class_exists('Normalizer')) { $n = Normalizer::normalize($raw, Normalizer::FORM_C); if (is_string($n)) { $raw = $n; } }
	$s = fv_collapse($raw);
	if ($s === '') { return ''; }
	if (mb_strlen($s) > FV_STATUS_MAX) { return null; }
	if (preg_match('/^[\p{L}\p{M}\p{N}\p{P} +=<>~^|№$]+$/u', $s) !== 1) { return null; }
	if (fv_is_profane($s)) { return null; }
	return $s;
}

// ------------------------------------------------------------------ profanity filter (Russian + English)

// Substrings forbidden anywhere (checked in the text with separators removed).
const FV_BAD_ANYWHERE = [
	// ru
	'хуй', 'хуе', 'хуё', 'хуя', 'хую', 'пизд', 'пезд', 'ебан', 'ебат', 'ебал', 'ебло', 'ебуч', 'ёбан', 'бляд', 'блят',
	'сучар', 'мудак', 'мудил', 'мудозвон', 'пидор', 'пидар', 'пидрил', 'гандон', 'гондон', 'шлюх', 'залуп', 'дроч',
	'долбоеб', 'долбаеб', 'далбаеб', 'мандав', 'говн', 'нацист', 'гитлер', 'порн', 'минет', 'шалав', 'хуйн',
	'уебок', 'уебищ', 'выбляд', 'ублюд', 'педрил', 'пидорас',
	// en + Russian swearing in Latin letters
	'fuck', 'shit', 'cunt', 'bitch', 'nigger', 'nigga', 'faggot', 'whore', 'slut', 'asshole', 'porn', 'penis',
	'vagina', 'dildo', 'blowjob', 'hitler', 'pussy', 'motherf', 'pizd', 'pezd', 'blyad', 'blyat', 'zalup', 'gandon',
	'gondon', 'pidor', 'pidar', 'pidaras', 'mudak', 'hueso', 'huesos', 'ebany', 'ebanu', 'eblan', 'dolboeb',
];
// Whole words (a token equal to one of these).
const FV_BAD_WORDS = [
	'хер', 'херня', 'нахер', 'похер', 'бля', 'блять', 'сука', 'суки', 'суке', 'суку', 'сукой', 'сучка', 'сучки', 'сучий',
	'жопа', 'жопу', 'жопы', 'жопой', 'срака', 'ссать', 'анал', 'секс', 'мразь', 'пидр', 'педик', 'хуи', 'ебу', 'еби',
	'ебись', 'ебать', 'ебет', 'уеб', 'заеб', 'выеб', 'трах', 'трахать', 'шмара', 'чмо', 'манда', 'манды',
	'dick', 'cock', 'ass', 'sex', 'fag', 'rape', 'cum', 'tits', 'hui', 'huy', 'xuy', 'xui', 'fuk', 'suka', 'suki',
	'blya', 'nazi', 'eba', 'ebal', 'eban', 'jopa', 'zhopa', 'manda', 'nahuy', 'nahui', 'pohuy', 'pohui', 'chmo',
];
// Word beginnings (a token starting with one of these).
const FV_BAD_PREFIX = [
	'бля', 'ебу', 'ебл', 'ебн', 'ебош', 'заеб', 'выеб', 'уеба', 'уебо', 'отъеб', 'съеб', 'поеб', 'наеб', 'проеб', 'жоп',
	'сучк', 'шалав', 'трахн', 'хуе', 'хуя',
	'huj', 'huy', 'hui', 'xuy', 'xui', 'xuj', 'blya', 'eba', 'ebl', 'zaeb', 'vyeb', 'naeb', 'nahu', 'pohu', 'fuck',
];

/** true when the text contains swearing (whole words, roots, look-alike letters, digits, separators, repeats). */
function fv_is_profane(string $text): bool
{
	$lower = mb_strtolower($text);
	$lower = str_replace('ё', 'е', $lower);
	// Latin look-alikes and digits -> Cyrillic ("xyй", "3алуп"), Cyrillic look-alikes and digits -> Latin ("fuсk", "sh1t").
	static $toCyr = ['a' => 'а', 'b' => 'б', 'c' => 'с', 'e' => 'е', 'h' => 'н', 'k' => 'к', 'm' => 'м', 'o' => 'о',
		'p' => 'р', 't' => 'т', 'x' => 'х', 'y' => 'у', 'u' => 'и', '3' => 'з', '0' => 'о', '4' => 'ч', '6' => 'б'];
	static $toLat = ['а' => 'a', 'в' => 'b', 'с' => 'c', 'е' => 'e', 'н' => 'h', 'к' => 'k', 'м' => 'm', 'о' => 'o',
		'р' => 'p', 'т' => 't', 'х' => 'x', 'у' => 'y', 'и' => 'u', '0' => 'o', '1' => 'i', '3' => 'e', '4' => 'a',
		'5' => 's', '7' => 't', '8' => 'b'];
	$variants = [$lower, strtr($lower, $toCyr), strtr($lower, $toLat)];
	$more = [];
	foreach ($variants as $v) {
		$more[] = preg_replace('/(.)\1+/u', '$1', $v) ?? $v;      // "хууууй" -> "хуй"
	}
	$variants = array_unique(array_merge($variants, $more));
	foreach ($variants as $v) {
		$tokens = preg_split('/[\s_\-]+/u', $v, -1, PREG_SPLIT_NO_EMPTY) ?: [];
		$joined = implode('', $tokens);
		foreach (FV_BAD_ANYWHERE as $bad) {
			if (str_contains($joined, $bad)) { return true; }
		}
		foreach ($tokens as $t) {
			if (in_array($t, FV_BAD_WORDS, true)) { return true; }
			foreach (FV_BAD_PREFIX as $p) {
				if (str_starts_with($t, $p)) { return true; }
			}
		}
		// single letters spaced out: "х у й", "f u c k"
		if (count($tokens) >= 3 && in_array($joined, FV_BAD_WORDS, true)) { return true; }
	}
	return false;
}

// ------------------------------------------------------------------ fish shows

/**
 * {fish, size, q} -> ['fish' => id, 'size' => cm (0.1), 'q' => 0..3], null for "nothing" (empty id / null).
 * An impossible fish (unknown id, size above the species max + 0.5 cm) adds a violation and returns null.
 */
function fv_fish_show($v, array &$viol, string $label): ?array
{
	if (!is_array($v)) { return null; }
	$fish = $v['fish'] ?? '';
	if ($fish === '' || $fish === null || $fish === 'None') { return null; }
	if (!is_string($fish) || !isset(FV_FISH[$fish])) {
		$viol[] = $label . ': неизвестная рыба «' . mb_substr((string)(is_scalar($fish) ? $fish : '?'), 0, 40) . '»';
		return null;
	}
	$size = fv_float($v['size'] ?? 0);
	$q = fv_int($v['q'] ?? 0, 0, 3);
	if ($size === null || $size < 0) {
		$viol[] = $label . ': неверный размер';
		return null;
	}
	$max = (float)FV_FISH[$fish]['max'];
	if ($size > $max + FV_SIZE_TOLERANCE) {
		$viol[] = sprintf('%s: %s %.1f см (максимум %.1f)', $label, FV_FISH[$fish]['name'], $size, $max);
		return null;
	}
	return ['fish' => $fish, 'size' => round($size, 1), 'q' => $q ?? 0];
}

// ------------------------------------------------------------------ stats plausibility

/**
 * Validates the synced stats{} against the stored row $p. Returns the columns to update (only valid fields);
 * every rejected field adds a Russian note to $viol. Rules: level 1..15 consistent with xp on the client's scale, counters
 * never decrease (unless the baseline was reset by a restore; the one-off XP trim of update 6), growth limits per minute
 * since the previous sync, absolute caps. Update 6 also returns xp_earned / xp_earned_seen / xp_seniority / xp_migrated.
 */
function fv_check_stats(array $p, array $stats, int $now, array &$viol): array
{
	$out = [];
	$reset = (int)$p['reset_baseline'] === 1;
	$first = (int)$p['last_sync'] === 0;
	$elapsed = $first ? 0 : max(0, $now - (int)$p['last_sync']);
	$mins = $elapsed / 60.0;
	$checkGrowth = !$first && !$reset;
	$oldXp = (int)$p['xp'];
	$grace = max(0, (int)$p['grace_xp']);
	$xpLimit = (int)floor(3000 * $mins) + 5000 + $grace;

	// level + xp (one pair). Update 6 (§9): the level is checked on the client's own scale (stats.max_level; 0.5 = 10), and
	// the update-6 client trims XP above 10 000 once (stats.xp_migration): that single decrease is accepted and remembered
	// (players.xp_migrated), without a «suspicious» mark.
	if (array_key_exists('level', $stats) || array_key_exists('xp', $stats)) {
		$level = fv_int($stats['level'] ?? $p['level'], -1000000, 1000000);
		$xp = fv_int($stats['xp'] ?? $p['xp'], PHP_INT_MIN, 2000000000);
		$cap = fv_client_level_cap($stats);
		$migration = fv_flag($stats['xp_migration'] ?? null) && (int)($p['xp_migrated'] ?? 0) === 0;
		if ($level === null || $xp === null || $level < 1 || $level > FV_MAX_LEVEL || $xp < 0) {
			$viol[] = 'уровень/опыт: недопустимые значения';
		} elseif (fv_level_for_xp($xp, $cap) !== $level) {
			$viol[] = sprintf('уровень %d не соответствует опыту %d', $level, $xp);
		} elseif (!$reset && $xp < $oldXp && !($migration && $xp >= FV_XP_TABLE[FV_CLASSIC_MAX_LEVEL])) {
			$viol[] = sprintf('опыт уменьшился: %d → %d', $oldXp, $xp);
		} elseif ($checkGrowth && $xp - $oldXp > $xpLimit) {
			$viol[] = sprintf('опыт +%d за %s (лимит %d)', $xp - $oldXp, fv_fmt_minutes($mins), $xpLimit);
		} else {
			$out['level'] = $level;
			$out['xp'] = $xp;
			if ($migration) { $out['xp_migrated'] = 1; }   // used up even when nothing had to be trimmed
		}
	}

	// «опыт заработанный» (§9): the tournament «Опыт» counts it. The server keeps its own counter players.xp_earned
	// (= xp when the column appeared) and adds the growth of the client's counter (stats.xp_earned, update 6) since the
	// value it reported last (players.xp_earned_seen, -1 = never); a client without the counter adds its XP growth.
	$gain = 0;
	$seen = (int)($p['xp_earned_seen'] ?? -1);
	if (array_key_exists('xp_earned', $stats)) {
		$ce = fv_int($stats['xp_earned'], PHP_INT_MIN, PHP_INT_MAX);
		if ($ce === null || $ce < 0 || $ce > 900000000000) {
			$viol[] = 'опыт заработанный: недопустимое значение';
		} elseif ($seen < 0 || $reset) {
			$out['xp_earned_seen'] = $ce;                  // the first report / after a restore: a new baseline
			$gain = isset($out['xp']) ? max(0, $out['xp'] - $oldXp) : 0;
		} elseif ($ce < $seen) {
			$viol[] = sprintf('опыт заработанный уменьшился: %d → %d', $seen, $ce);
			$out['xp_earned_seen'] = $ce;                  // rebased: one note, not one on every sync
		} elseif ($checkGrowth && $ce - $seen > $xpLimit) {
			$viol[] = sprintf('опыт заработанный +%d за %s (лимит %d)', $ce - $seen, fv_fmt_minutes($mins), $xpLimit);
		} else {
			$gain = $ce - $seen;
			$out['xp_earned_seen'] = $ce;
		}
	} elseif (isset($out['xp'])) {
		$gain = max(0, $out['xp'] - $oldXp);
	}
	if ($gain > 0) { $out['xp_earned'] = (int)($p['xp_earned'] ?? 0) + $gain; }

	// «стаж» (§9): the XP trimmed by the migration, shown in the profile. It cannot exceed what the player could have had.
	if (array_key_exists('xp_seniority', $stats)) {
		$sn = fv_int($stats['xp_seniority'], PHP_INT_MIN, PHP_INT_MAX);
		$old = (int)($p['xp_seniority'] ?? 0);
		$snMax = ($first || $reset) ? 2000000000 : max($old, $oldXp - FV_XP_TABLE[FV_CLASSIC_MAX_LEVEL] + $xpLimit);
		if ($sn === null || $sn < 0) {
			$viol[] = 'стаж: недопустимое значение';
		} elseif ($sn > $snMax) {
			$viol[] = sprintf('стаж %d больше возможного (%d)', $sn, $snMax);
		} elseif ($sn !== $old) {
			$out['xp_seniority'] = $sn;
		}
	}

	$fishLimit = (int)floor(40 * $mins) + 40;
	// field => [label, absolute max, growth limit or null]
	$rules = [
		'fish_total' => ['всего рыбы', 100000000, $fishLimit],
		'legendary' => ['легендарные', 10000000, $fishLimit],
		'perfect' => ['идеальные поимки', 100000000, $fishLimit],
		'species' => ['виды', FV_MAX_SPECIES, null],
		'achievements' => ['достижения', FV_MAX_ACHIEVEMENTS, null],
		// + the claimed reward XP: while the level is frozen (update 6, Р10) it is paid out as coins 1:1
		'coins_earned' => ['заработано монет', 900000000000000, (int)floor(60000 * $mins) + 100000 + $grace],
		'playtime' => ['время в игре', 2000000000, $elapsed + 900],
		'stations_built' => ['постройки', FV_MAX_STATIONS, null],
	];
	foreach ($rules as $field => [$label, $absMax, $growth]) {
		if (!array_key_exists($field, $stats)) { continue; }
		$v = fv_int($stats[$field], PHP_INT_MIN, PHP_INT_MAX);
		$old = (int)$p[$field];
		if ($v === null || $v < 0) {
			$viol[] = $label . ': недопустимое значение';
		} elseif ($v > $absMax) {
			$viol[] = sprintf('%s: %d (максимум %d)', $label, $v, $absMax);
		} elseif (!$reset && $v < $old) {
			$viol[] = sprintf('%s уменьшилось: %d → %d', $label, $old, $v);
		} elseif ($checkGrowth && $growth !== null && $v - $old > $growth) {
			$viol[] = sprintf('%s +%d за %s (лимит %d)', $label, $v - $old, fv_fmt_minutes($mins), $growth);
		} else {
			$out[$field] = $v;
		}
	}
	return $out;
}

function fv_fmt_minutes(float $mins): string
{
	if ($mins < 1) { return 'меньше минуты'; }
	if ($mins < 120) { return (int)round($mins) . ' мин'; }
	return round($mins / 60, 1) . ' ч';
}

/**
 * Validates profile{} (status is handled separately). Returns ['json' => array for players.profile_json,
 * 'shows' => [valid fish shows for the all-time biggest]].
 */
function fv_check_profile(array $old, array $profile, array &$viol): array
{
	$j = $old;
	$shows = [];
	if (array_key_exists('favorite_location', $profile)) {
		$loc = $profile['favorite_location'];
		$j['favorite_location'] = (is_string($loc) && isset(FV_LOCATIONS[$loc])) ? $loc : '';
	}
	if (array_key_exists('best_catch', $profile)) {
		$bc = fv_fish_show($profile['best_catch'], $viol, 'лучший улов');
		if ($bc !== null) { $j['best_catch'] = $bc; $shows[] = $bc; }
		elseif (!is_array($profile['best_catch']) || ($profile['best_catch']['fish'] ?? '') === '') { $j['best_catch'] = null; }
	}
	if (array_key_exists('showcase', $profile) && is_array($profile['showcase'])) {
		$list = [];
		foreach (array_slice(array_values($profile['showcase']), 0, 3) as $i => $s) {
			$f = fv_fish_show($s, $viol, 'витрина #' . ($i + 1));
			if ($f !== null) { $list[] = $f; $shows[] = $f; }
		}
		$j['showcase'] = $list;
	}
	if (array_key_exists('appearance', $profile) && is_array($profile['appearance'])) {
		$a = [];
		foreach (['hat', 'outfit', 'bobber', 'pet'] as $k) {
			$v = $profile['appearance'][$k] ?? '';
			$a[$k] = fv_is_game_id($v) ? $v : '';
		}
		$j['appearance'] = $a;
	}
	if (array_key_exists('start_date', $profile)) {
		$sd = fv_int($profile['start_date'], 0, PHP_INT_MAX);
		$j['start_date'] = ($sd !== null && $sd >= 1577836800 && $sd <= fv_now() + 86400) ? $sd : 0;
	}
	return ['json' => $j, 'shows' => $shows];
}

/** Decoded profile_json with all keys present. */
function fv_profile_data(?string $json): array
{
	$j = $json ? json_decode($json, true) : null;
	if (!is_array($j)) { $j = []; }
	return array_merge([
		'favorite_location' => '',
		'best_catch' => null,
		'showcase' => [],
		'appearance' => ['hat' => '', 'outfit' => '', 'bobber' => '', 'pet' => ''],
		'start_date' => 0,
	], $j);
}

function fv_fish_name(string $id): string
{
	return FV_FISH[$id]['name'] ?? $id;
}

function fv_location_name(string $id): string
{
	return FV_LOCATIONS[$id] ?? $id;
}

function fv_item_name(string $id): string
{
	return FV_ITEMS[$id] ?? (FV_FISH[$id]['name'] ?? $id);
}
