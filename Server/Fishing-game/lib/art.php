<?php
// Fishing Valley server: pictures exported from the game (Tools/ExportArt.ps1 -> Tools/build_site_art.py) for the
// download page and the admin panel: img/icons/<id>.png (x4 pixel art), img/art.json (icon sizes and names, gallery,
// location cards, the gameplay clip). Everything degrades gracefully while the img folder is not uploaded.
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

/** img/art.json decoded: icons {id: [w, h, kind, name]}, shots, locations, gameplay. [] when missing / broken. */
function fv_art(): array
{
	static $art = null;
	if ($art === null) {
		$j = @file_get_contents(FV_ROOT . '/img/art.json');
		$art = is_string($j) ? json_decode($j, true) : null;
		if (!is_array($art)) { $art = []; }
	}
	return $art;
}

/** [w, h] of the icon in art texels, null when there is no such icon. */
function fv_icon_size(string $id): ?array
{
	if (preg_match('/^[a-z0-9_]{1,64}$/', $id) !== 1) { return null; }
	$i = fv_art()['icons'][$id] ?? null;
	if (is_array($i) && count($i) >= 2) { return [max(1, (int)$i[0]), max(1, (int)$i[1])]; }
	return is_file(FV_ROOT . '/img/icons/' . $id . '.png') ? [16, 16] : null;
}

/** Name of an icon from the game (cosmetics of every update included); '' when unknown. */
function fv_art_name(string $id): string
{
	$i = fv_art()['icons'][$id] ?? null;
	return is_array($i) && isset($i[3]) ? (string)$i[3] : '';
}

/**
 * <img> of a game icon in a Size x Size box (pixelated: whole-number scales when the icon fits at least twice), or a
 * «?» stub when the icon is missing. $base = path to the site root from the page ('' for index.php, '../' for admin).
 */
function fv_icon(string $id, string $base = '', int $size = 32, string $title = '', string $cls = ''): string
{
	$t = $title !== '' ? ' title="' . htmlspecialchars($title, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8') . '"' : '';
	$wh = fv_icon_size($id);
	if ($wh === null) {
		return '<span class="ico ico-none ico-' . $size . ($cls !== '' ? ' ' . $cls : '') . '"' . $t . ' aria-hidden="true">?</span>';   // no inline style (CSP)
	}
	[$w, $h] = $wh;
	$s = $size / max($w, $h);
	if ($s >= 2) { $s = floor($s); }
	$iw = max(1, (int)round($w * $s));
	$ih = max(1, (int)round($h * $s));
	return '<img class="ico' . ($cls !== '' ? ' ' . $cls : '') . '" src="' . $base . 'img/icons/' . $id . '.png" width="' . $iw . '" height="' . $ih
		. '" alt="" loading="lazy" decoding="async"' . $t . '>';
}
