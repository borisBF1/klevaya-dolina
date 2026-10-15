<?php
// Fishing Valley — creates the database tables (MySQL or SQLite, see config.php 'db_dsn').
// Open once: https://<site>/Fishing-game/install.php?key=<install_key from config.php>, then DELETE this file.
// Safe to run again: existing tables and data are kept.
declare(strict_types=1);

define('FV_ROOT', __DIR__);
require __DIR__ . '/lib/db.php';
require __DIR__ . '/lib/season.php';
require __DIR__ . '/lib/schema.php';

header('Content-Type: text/html; charset=utf-8');
header('X-Robots-Tag: noindex, nofollow');
header('Cache-Control: no-store');

function fv_install_page(string $title, string $html, bool $ok): void
{
	$color = $ok ? '#3f7d3a' : '#a33a2a';
	echo '<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
		. '<meta name="robots" content="noindex,nofollow"><title>Установка — Клёвая долина</title><style>'
		. 'body{font:16px/1.5 system-ui,sans-serif;background:#f6efdf;color:#3b2b1a;margin:0;padding:16px}'
		. '.box{max-width:640px;margin:24px auto;background:#fffaf0;border:2px solid #c9a76b;border-radius:12px;padding:20px}'
		. 'h1{font-size:22px;margin:0 0 12px;color:' . $color . '}li{margin:4px 0}code{background:#f0e4c8;padding:1px 5px;border-radius:4px}'
		. '</style></head><body><div class="box"><h1>' . htmlspecialchars($title, ENT_QUOTES, 'UTF-8') . '</h1>' . $html . '</div></body></html>';
	exit;
}

function fv_install_ddl(string $driver): array
{
	if ($driver === 'mysql') {
		$t = ' ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci';
		return [
			'players' => "CREATE TABLE IF NOT EXISTS players (
				id INT UNSIGNED NOT NULL AUTO_INCREMENT,
				secret CHAR(64) NOT NULL,
				name VARCHAR(32) NOT NULL DEFAULT '',
				name_lc VARCHAR(32) NOT NULL DEFAULT '',
				status VARCHAR(64) NOT NULL DEFAULT '',
				platform VARCHAR(16) NOT NULL DEFAULT '',
				ver VARCHAR(24) NOT NULL DEFAULT '',
				created INT NOT NULL DEFAULT 0,
				last_seen INT NOT NULL DEFAULT 0,
				updated_at INT NOT NULL DEFAULT 0,
				banned TINYINT NOT NULL DEFAULT 0,
				ban_reason VARCHAR(200) NOT NULL DEFAULT '',
				suspicious INT NOT NULL DEFAULT 0,
				notes TEXT NULL,
				level INT NOT NULL DEFAULT 1,
				xp INT NOT NULL DEFAULT 0,
				fish_total INT NOT NULL DEFAULT 0,
				legendary INT NOT NULL DEFAULT 0,
				species INT NOT NULL DEFAULT 0,
				coins_earned BIGINT NOT NULL DEFAULT 0,
				achievements INT NOT NULL DEFAULT 0,
				perfect INT NOT NULL DEFAULT 0,
				playtime INT NOT NULL DEFAULT 0,
				stations_built INT NOT NULL DEFAULT 0,
				best_size DOUBLE NOT NULL DEFAULT 0,
				best_fish VARCHAR(40) NOT NULL DEFAULT '',
				best_q TINYINT NOT NULL DEFAULT 0,
				profile_json TEXT NULL,
				last_sync INT NOT NULL DEFAULT 0,
				reset_baseline TINYINT NOT NULL DEFAULT 0,
				grace_xp INT NOT NULL DEFAULT 0,
				xp_earned BIGINT NOT NULL DEFAULT 0,
				xp_earned_seen BIGINT NOT NULL DEFAULT -1,
				xp_seniority INT NOT NULL DEFAULT 0,
				xp_migrated TINYINT NOT NULL DEFAULT 0,
				PRIMARY KEY (id),
				UNIQUE KEY players_secret (secret),
				KEY players_name (name_lc),
				KEY players_xp (banned, xp, updated_at),
				KEY players_fish (banned, fish_total, updated_at),
				KEY players_size (banned, best_size, updated_at),
				KEY players_legend (banned, legendary, updated_at),
				KEY players_species (banned, species, updated_at),
				KEY players_earned (banned, coins_earned, updated_at),
				KEY players_ach (banned, achievements, updated_at),
				KEY players_seen (last_seen),
				KEY players_susp (suspicious)
			)$t",
			'saves' => "CREATE TABLE IF NOT EXISTS saves (
				id INT UNSIGNED NOT NULL AUTO_INCREMENT,
				player_id INT UNSIGNED NOT NULL,
				created INT NOT NULL DEFAULT 0,
				size INT NOT NULL DEFAULT 0,
				version INT NOT NULL DEFAULT 0,
				hash CHAR(40) NOT NULL DEFAULT '',
				data MEDIUMBLOB NOT NULL,
				PRIMARY KEY (id),
				KEY saves_player (player_id, id)
			)$t",
			'restore_codes' => "CREATE TABLE IF NOT EXISTS restore_codes (
				code CHAR(8) NOT NULL,
				player_id INT UNSIGNED NOT NULL,
				save_id INT UNSIGNED NOT NULL,
				created INT NOT NULL DEFAULT 0,
				expires INT NOT NULL DEFAULT 0,
				used INT NULL DEFAULT NULL,
				used_ip VARCHAR(45) NULL DEFAULT NULL,
				PRIMARY KEY (code),
				KEY codes_player (player_id)
			)$t",
			'mail' => "CREATE TABLE IF NOT EXISTS mail (
				id INT UNSIGNED NOT NULL AUTO_INCREMENT,
				player_id INT UNSIGNED NOT NULL,
				kind VARCHAR(16) NOT NULL DEFAULT 'admin',
				title VARCHAR(100) NOT NULL DEFAULT '',
				body TEXT NULL,
				coins INT NOT NULL DEFAULT 0,
				xp INT NOT NULL DEFAULT 0,
				set_level INT NOT NULL DEFAULT 0,
				items_json TEXT NULL,
				created INT NOT NULL DEFAULT 0,
				claimed INT NULL DEFAULT NULL,
				PRIMARY KEY (id),
				KEY mail_player (player_id, id)
			)$t",
			'seasons' => "CREATE TABLE IF NOT EXISTS seasons (
				id INT NOT NULL,
				starts INT NOT NULL DEFAULT 0,
				ends INT NOT NULL DEFAULT 0,
				closing TINYINT NOT NULL DEFAULT 0,
				closed INT NULL DEFAULT NULL,
				results_json MEDIUMTEXT NULL,
				PRIMARY KEY (id)
			)$t",
			'season_stats' => "CREATE TABLE IF NOT EXISTS season_stats (
				season_id INT NOT NULL,
				player_id INT UNSIGNED NOT NULL,
				base_fish INT NOT NULL DEFAULT 0,
				base_earned BIGINT NOT NULL DEFAULT 0,
				base_xp INT NOT NULL DEFAULT 0,
				fish INT NOT NULL DEFAULT 0,
				earned BIGINT NOT NULL DEFAULT 0,
				xp INT NOT NULL DEFAULT 0,
				best_size DOUBLE NOT NULL DEFAULT 0,
				best_fish VARCHAR(40) NOT NULL DEFAULT '',
				best_q TINYINT NOT NULL DEFAULT 0,
				updated_at INT NOT NULL DEFAULT 0,
				PRIMARY KEY (season_id, player_id),
				KEY sstats_fish (season_id, fish),
				KEY sstats_earned (season_id, earned),
				KEY sstats_xp (season_id, xp),
				KEY sstats_size (season_id, best_size)
			)$t",
			'rate_log' => "CREATE TABLE IF NOT EXISTS rate_log (
				k VARCHAR(80) NOT NULL,
				ts INT NOT NULL,
				KEY rate_k (k, ts),
				KEY rate_ts (ts)
			)$t",
			'admin_log' => "CREATE TABLE IF NOT EXISTS admin_log (
				id INT UNSIGNED NOT NULL AUTO_INCREMENT,
				ts INT NOT NULL DEFAULT 0,
				action VARCHAR(40) NOT NULL DEFAULT '',
				player_id INT UNSIGNED NULL DEFAULT NULL,
				details TEXT NULL,
				ip VARCHAR(45) NOT NULL DEFAULT '',
				PRIMARY KEY (id),
				KEY alog_player (player_id)
			)$t",
			'settings' => "CREATE TABLE IF NOT EXISTS settings (
				k VARCHAR(40) NOT NULL,
				v TEXT NULL,
				PRIMARY KEY (k)
			)$t",
		];
	}
	return [
		'players' => "CREATE TABLE IF NOT EXISTS players (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			secret TEXT NOT NULL UNIQUE,
			name TEXT NOT NULL DEFAULT '',
			name_lc TEXT NOT NULL DEFAULT '',
			status TEXT NOT NULL DEFAULT '',
			platform TEXT NOT NULL DEFAULT '',
			ver TEXT NOT NULL DEFAULT '',
			created INTEGER NOT NULL DEFAULT 0,
			last_seen INTEGER NOT NULL DEFAULT 0,
			updated_at INTEGER NOT NULL DEFAULT 0,
			banned INTEGER NOT NULL DEFAULT 0,
			ban_reason TEXT NOT NULL DEFAULT '',
			suspicious INTEGER NOT NULL DEFAULT 0,
			notes TEXT NULL,
			level INTEGER NOT NULL DEFAULT 1,
			xp INTEGER NOT NULL DEFAULT 0,
			fish_total INTEGER NOT NULL DEFAULT 0,
			legendary INTEGER NOT NULL DEFAULT 0,
			species INTEGER NOT NULL DEFAULT 0,
			coins_earned INTEGER NOT NULL DEFAULT 0,
			achievements INTEGER NOT NULL DEFAULT 0,
			perfect INTEGER NOT NULL DEFAULT 0,
			playtime INTEGER NOT NULL DEFAULT 0,
			stations_built INTEGER NOT NULL DEFAULT 0,
			best_size REAL NOT NULL DEFAULT 0,
			best_fish TEXT NOT NULL DEFAULT '',
			best_q INTEGER NOT NULL DEFAULT 0,
			profile_json TEXT NULL,
			last_sync INTEGER NOT NULL DEFAULT 0,
			reset_baseline INTEGER NOT NULL DEFAULT 0,
			grace_xp INTEGER NOT NULL DEFAULT 0,
			xp_earned INTEGER NOT NULL DEFAULT 0,
			xp_earned_seen INTEGER NOT NULL DEFAULT -1,
			xp_seniority INTEGER NOT NULL DEFAULT 0,
			xp_migrated INTEGER NOT NULL DEFAULT 0
		)",
		'players_idx' => [
			'CREATE INDEX IF NOT EXISTS players_name ON players (name_lc)',
			'CREATE INDEX IF NOT EXISTS players_xp ON players (banned, xp, updated_at)',
			'CREATE INDEX IF NOT EXISTS players_fish ON players (banned, fish_total, updated_at)',
			'CREATE INDEX IF NOT EXISTS players_size ON players (banned, best_size, updated_at)',
			'CREATE INDEX IF NOT EXISTS players_legend ON players (banned, legendary, updated_at)',
			'CREATE INDEX IF NOT EXISTS players_species ON players (banned, species, updated_at)',
			'CREATE INDEX IF NOT EXISTS players_earned ON players (banned, coins_earned, updated_at)',
			'CREATE INDEX IF NOT EXISTS players_ach ON players (banned, achievements, updated_at)',
			'CREATE INDEX IF NOT EXISTS players_seen ON players (last_seen)',
			'CREATE INDEX IF NOT EXISTS players_susp ON players (suspicious)',
		],
		'saves' => "CREATE TABLE IF NOT EXISTS saves (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			player_id INTEGER NOT NULL,
			created INTEGER NOT NULL DEFAULT 0,
			size INTEGER NOT NULL DEFAULT 0,
			version INTEGER NOT NULL DEFAULT 0,
			hash TEXT NOT NULL DEFAULT '',
			data BLOB NOT NULL
		)",
		'saves_idx' => ['CREATE INDEX IF NOT EXISTS saves_player ON saves (player_id, id)'],
		'restore_codes' => "CREATE TABLE IF NOT EXISTS restore_codes (
			code TEXT PRIMARY KEY,
			player_id INTEGER NOT NULL,
			save_id INTEGER NOT NULL,
			created INTEGER NOT NULL DEFAULT 0,
			expires INTEGER NOT NULL DEFAULT 0,
			used INTEGER NULL DEFAULT NULL,
			used_ip TEXT NULL DEFAULT NULL
		)",
		'restore_codes_idx' => ['CREATE INDEX IF NOT EXISTS codes_player ON restore_codes (player_id)'],
		'mail' => "CREATE TABLE IF NOT EXISTS mail (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			player_id INTEGER NOT NULL,
			kind TEXT NOT NULL DEFAULT 'admin',
			title TEXT NOT NULL DEFAULT '',
			body TEXT NULL,
			coins INTEGER NOT NULL DEFAULT 0,
			xp INTEGER NOT NULL DEFAULT 0,
			set_level INTEGER NOT NULL DEFAULT 0,
			items_json TEXT NULL,
			created INTEGER NOT NULL DEFAULT 0,
			claimed INTEGER NULL DEFAULT NULL
		)",
		'mail_idx' => ['CREATE INDEX IF NOT EXISTS mail_player ON mail (player_id, id)'],
		'seasons' => "CREATE TABLE IF NOT EXISTS seasons (
			id INTEGER PRIMARY KEY,
			starts INTEGER NOT NULL DEFAULT 0,
			ends INTEGER NOT NULL DEFAULT 0,
			closing INTEGER NOT NULL DEFAULT 0,
			closed INTEGER NULL DEFAULT NULL,
			results_json TEXT NULL
		)",
		'season_stats' => "CREATE TABLE IF NOT EXISTS season_stats (
			season_id INTEGER NOT NULL,
			player_id INTEGER NOT NULL,
			base_fish INTEGER NOT NULL DEFAULT 0,
			base_earned INTEGER NOT NULL DEFAULT 0,
			base_xp INTEGER NOT NULL DEFAULT 0,
			fish INTEGER NOT NULL DEFAULT 0,
			earned INTEGER NOT NULL DEFAULT 0,
			xp INTEGER NOT NULL DEFAULT 0,
			best_size REAL NOT NULL DEFAULT 0,
			best_fish TEXT NOT NULL DEFAULT '',
			best_q INTEGER NOT NULL DEFAULT 0,
			updated_at INTEGER NOT NULL DEFAULT 0,
			PRIMARY KEY (season_id, player_id)
		)",
		'season_stats_idx' => [
			'CREATE INDEX IF NOT EXISTS sstats_fish ON season_stats (season_id, fish)',
			'CREATE INDEX IF NOT EXISTS sstats_earned ON season_stats (season_id, earned)',
			'CREATE INDEX IF NOT EXISTS sstats_xp ON season_stats (season_id, xp)',
			'CREATE INDEX IF NOT EXISTS sstats_size ON season_stats (season_id, best_size)',
		],
		'rate_log' => 'CREATE TABLE IF NOT EXISTS rate_log (k TEXT NOT NULL, ts INTEGER NOT NULL)',
		'rate_log_idx' => [
			'CREATE INDEX IF NOT EXISTS rate_k ON rate_log (k, ts)',
			'CREATE INDEX IF NOT EXISTS rate_ts ON rate_log (ts)',
		],
		'admin_log' => "CREATE TABLE IF NOT EXISTS admin_log (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			ts INTEGER NOT NULL DEFAULT 0,
			action TEXT NOT NULL DEFAULT '',
			player_id INTEGER NULL DEFAULT NULL,
			details TEXT NULL,
			ip TEXT NOT NULL DEFAULT ''
		)",
		'admin_log_idx' => ['CREATE INDEX IF NOT EXISTS alog_player ON admin_log (player_id)'],
		'settings' => 'CREATE TABLE IF NOT EXISTS settings (k TEXT PRIMARY KEY, v TEXT NULL)',
	];
}

// ------------------------------------------------------------------ run

try {
	$cfg = fv_config();
} catch (Throwable $e) {
	fv_install_page('Нет config.php', '<p>Скопируйте <code>config.sample.php</code> в <code>config.php</code> (в этой же папке), '
		. 'впишите данные базы, пароль админки и ключ установки, затем откройте эту страницу снова.</p>', false);
}
$key = (string)$cfg['install_key'];
$given = isset($_GET['key']) && is_string($_GET['key']) ? $_GET['key'] : '';
if (strlen($key) < 8 || stripos($key, 'ЗАМЕНИТЕ') !== false) {
	fv_install_page('Установка запрещена', '<p>В <code>config.php</code> задайте <code>install_key</code> — любую строку от 8 символов.</p>', false);
}
if ($given === '' || !hash_equals($key, $given)) {
	http_response_code(403);
	fv_install_page('Доступ запрещён', '<p>Откройте страницу так: <code>install.php?key=ВАШ_install_key</code>.</p>', false);
}

$report = [];
$problems = [];
if (PHP_VERSION_ID < 80000) { $problems[] = 'Нужен PHP 8.0 или новее (сейчас ' . PHP_VERSION . '). Смените версию PHP в панели хостинга.'; }
foreach (['pdo', 'mbstring', 'json'] as $ext) {
	if (!extension_loaded($ext)) { $problems[] = 'Нет расширения PHP: ' . $ext; }
}
$driver = fv_driver();
if (!extension_loaded('pdo_' . $driver)) { $problems[] = 'Нет расширения PHP: pdo_' . $driver; }
if ($problems) {
	fv_install_page('Хостинг не подходит', '<ul><li>' . implode('</li><li>', array_map('htmlspecialchars', $problems)) . '</li></ul>', false);
}

try {
	$db = fv_db();
	foreach (fv_install_ddl($driver) as $name => $sql) {
		foreach ((array)$sql as $one) { $db->exec($one); }
		if (!str_ends_with($name, '_idx')) { $report[] = 'Таблица <code>' . htmlspecialchars($name) . '</code> — готово'; }
	}
	// update 4b: tournaments count from 01.09.2026 (was 28.09.2026) — renumber the seasons stored by the older server
	$moved = fv_season_migrate();
	if ($moved > 0) { $report[] = 'Турниры перенумерованы (новая нумерация с 01.09.2026): ' . $moved; }
	// update 6: new players columns (the API also adds them by itself on the first request)
	$added = fv_schema_upgrade();
	if ($added) { $report[] = 'Добавлены столбцы обновления 6: ' . htmlspecialchars(implode(', ', $added)); }
	$players = (int)$db->query('SELECT COUNT(*) FROM players')->fetchColumn();
	$report[] = 'Игроков в базе: ' . $players;
	if ($driver === 'mysql') {
		$report[] = 'MySQL ' . htmlspecialchars((string)$db->query('SELECT VERSION()')->fetchColumn());
	}
} catch (Throwable $e) {
	fv_log_error('install: ' . $e->getMessage());
	fv_install_page('Ошибка базы данных', '<p>Не удалось подключиться к базе или создать таблицы.</p><p><code>'
		. htmlspecialchars($e->getMessage(), ENT_QUOTES, 'UTF-8') . '</code></p><p>Проверьте <code>db_dsn</code>, '
		. '<code>db_user</code> и <code>db_pass</code> в config.php.</p>', false);
}

$warn = '';
if (strlen((string)$cfg['admin_password']) < 8) {
	$warn = '<p><b>Внимание:</b> пароль админки (<code>admin_password</code>) пустой или короче 8 символов — вход в админку будет невозможен/небезопасен.</p>';
}
fv_install_page('Готово: база данных создана', '<ul><li>' . implode('</li><li>', $report) . '</li></ul>' . $warn
	. '<p><b>Теперь удалите файл <code>install.php</code> с хостинга</b>, затем откройте <a href="admin/">админку</a>'
	. ' (раздел «Версия игры») и <a href="./">страницу скачивания</a>.</p>'
	. '<p>Адрес API для игры: <code>' . htmlspecialchars(fv_install_api_url(), ENT_QUOTES, 'UTF-8') . '</code></p>', true);

function fv_install_api_url(): string
{
	$host = (string)($_SERVER['HTTP_HOST'] ?? 'localhost');
	$dir = rtrim(str_replace('\\', '/', dirname((string)($_SERVER['SCRIPT_NAME'] ?? '/install.php'))), '/');
	return (fv_is_https_install() ? 'https://' : 'http://') . $host . $dir . '/api.php';
}

function fv_is_https_install(): bool
{
	return (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') || (int)($_SERVER['SERVER_PORT'] ?? 0) === 443
		|| strtolower((string)($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '')) === 'https';
}
