<?php
// Fishing Valley server: key/value settings (table settings) — the game version for the start-up gate and the download
// page (Docs/UPDATE4_ONLINE.md §10). Edited in the admin panel («Версия игры»).
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

// Placeholder: the admin panel («Версия игры» → «Ссылка на APK») sets the real link of your deployment.
const FV_DOWNLOAD_URL_DEFAULT = 'https://example.com/Fishing-game/download/KlevayaDolina.apk';
/** Values used until the admin saves his own (all stored as strings). */
const FV_VERSION_DEFAULTS = [
	'latest_build' => '4',
	'latest_name' => '0.4',
	'min_build' => '4',
	'download_url' => FV_DOWNLOAD_URL_DEFAULT,
	'changelog' => "Профиль рыбака: имя, номер, статус, витрина трофеев и статистика\n"
		. "Таблица лидеров и турниры каждые 3 дня с наградами в четырёх категориях\n"
		. "Почта: подарки и итоги турниров\n"
		. "Восстановление сохранения по коду\n"
		. "Учебный пруд во дворе и обучение для новичков\n"
		. "Проверка обновлений при запуске и офлайн-режим",
	'version_updated' => '0',
];
const FV_BUILD_MAX = 1000000;

/** All stored settings (k => v). [] when the table is missing (install.php not re-run yet) or the database is down. */
function fv_settings(bool $reload = false): array
{
	static $cache = null;
	if ($cache !== null && !$reload) { return $cache; }
	$cache = [];
	try {
		foreach (fv_all('SELECT k, v FROM settings') as $r) { $cache[(string)$r['k']] = (string)($r['v'] ?? ''); }
	} catch (Throwable $e) {
		$cache = [];
	}
	return $cache;
}

/** true when the settings table exists (the admin page warns otherwise). */
function fv_settings_ready(): bool
{
	try { fv_q('SELECT COUNT(*) FROM settings'); return true; } catch (Throwable $e) { return false; }
}

function fv_setting(string $k): string
{
	return fv_settings()[$k] ?? (FV_VERSION_DEFAULTS[$k] ?? '');
}

/** Stores a value (portable upsert: MySQL and SQLite). */
function fv_setting_set(string $k, string $v): void
{
	fv_q(fv_insert_ignore_sql() . ' INTO settings (k, v) VALUES (?, ?)', [$k, $v]);
	fv_q('UPDATE settings SET v = ? WHERE k = ?', [$v, $k]);
	fv_settings(true);
}

/** ping.version: {latest_build, latest_name, min_build, download_url, changelog} (min_build never above latest_build). */
function fv_version_info(): array
{
	$latest = max(0, min(FV_BUILD_MAX, (int)fv_setting('latest_build')));
	return [
		'latest_build' => $latest,
		'latest_name' => fv_setting('latest_name'),
		'min_build' => max(0, min($latest, (int)fv_setting('min_build'))),
		'download_url' => fv_setting('download_url'),
		'changelog' => fv_setting('changelog'),
	];
}

/** Changelog lines without list markers ("• ", "- ", "* ", "– "), empty lines dropped. */
function fv_changelog_lines(string $text): array
{
	$out = [];
	foreach (preg_split('/\R/u', $text) ?: [] as $line) {
		$line = trim((string)preg_replace('/^\s*(?:[•\-\*–—]|\d+[.)])\s*/u', '', $line));
		if ($line !== '') { $out[] = $line; }
	}
	return $out;
}

/** A download URL the admin may store: http(s), no spaces, <= 300 chars. */
function fv_valid_download_url(string $url): bool
{
	if ($url === '' || strlen($url) > 300 || preg_match('/\s/', $url) === 1) { return false; }
	if (filter_var($url, FILTER_VALIDATE_URL) === false) { return false; }
	$scheme = strtolower((string)parse_url($url, PHP_URL_SCHEME));
	return $scheme === 'https' || $scheme === 'http';
}

/**
 * When the download URL points into this server's download/ folder: ['name', 'exists', 'size', 'mtime'] of the APK
 * on disk; null when the URL leads somewhere else.
 */
function fv_apk_local(string $url): ?array
{
	$path = (string)parse_url($url, PHP_URL_PATH);
	if (preg_match('~/download/([A-Za-z0-9._\-]{1,100}\.apk)$~', $path, $m) !== 1) { return null; }
	$host = strtolower((string)parse_url($url, PHP_URL_HOST));
	$here = strtolower((string)preg_replace('/:\d+$/', '', (string)($_SERVER['HTTP_HOST'] ?? '')));
	if ($here !== '' && $host !== $here && $host !== 'www.' . $here && 'www.' . $host !== $here) { return null; }
	$file = FV_ROOT . '/download/' . $m[1];
	$exists = is_file($file);
	return ['name' => $m[1], 'exists' => $exists, 'size' => $exists ? (int)filesize($file) : 0, 'mtime' => $exists ? (int)filemtime($file) : 0];
}

/** "48,3 МБ". */
function fv_mb_text(int $bytes): string
{
	return number_format($bytes / 1048576, 1, ',', ' ') . ' МБ';
}
