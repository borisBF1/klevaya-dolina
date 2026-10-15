<?php
// Fishing Valley server: configuration, database (PDO: MySQL in production, SQLite for local tests), clock,
// rate limits, JSON output and error logging. Included by api.php, install.php and admin/index.php.
//
// Config lookup (first existing file wins):
//   1. the file named by the environment variable FV_CONFIG            (local / tests)
//   2. <this folder>/config.php                                         (production: Fishing-game/config.php)
//   3. <repo>/Server/local/config.php, i.e. ../../local/config.php     (local fallback)
// Debug settings (debug_time_offset, ip_header) are honoured ONLY for configs found via 1 or 3.
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

const FV_SERVER_VERSION = '1.2';           // 1.2 = update 6 (levels 11–15, «опыт заработанный», XP migration)
const FV_MAX_BODY = 1200000;            // bytes of a request body (a 700 KB save in base64 + JSON)
const FV_MAX_SAVE = 716800;             // 700 KB decoded
const FV_SAVES_KEEP = 12;

ini_set('serialize_precision', '-1');   // floats in JSON: 45.2, not 45.200000000000003
ini_set('display_errors', '0');
mb_internal_encoding('UTF-8');
date_default_timezone_set('Europe/Moscow');

final class FVError extends Exception
{
	public string $codeName;
	public array $extra;
	public function __construct(string $code, string $message, array $extra = [])
	{
		parent::__construct($message);
		$this->codeName = $code;
		$this->extra = $extra;
	}
}

/** Loads the config once. Throws RuntimeException('config_missing') when none is found. */
function fv_config(): array
{
	static $cfg = null;
	if ($cfg !== null) { return $cfg; }
	$candidates = [];
	$env = getenv('FV_CONFIG');
	if (is_string($env) && $env !== '') { $candidates[] = [$env, true]; }
	$candidates[] = [FV_ROOT . '/config.php', false];
	$candidates[] = [dirname(FV_ROOT) . '/local/config.php', true];
	foreach ($candidates as [$path, $local]) {
		if (!is_file($path)) { continue; }
		$c = require $path;
		if (!is_array($c)) { throw new RuntimeException('config_invalid'); }
		$defaults = [
			'db_dsn' => '', 'db_user' => '', 'db_pass' => '',
			'admin_password' => '', 'install_key' => '',
			'rate_signed' => [60, 600],        // signed requests per player / window (s)
			'rate_register' => [10, 3600],     // registrations per IP
			'rate_restore' => [20, 3600],      // restore attempts per IP
			'rate_unsigned' => [1200, 600],    // ping / board / profile per IP
			'rate_admin_login' => [10, 900],   // failed admin logins per IP
			'check_nonce' => true,             // reject a repeated nonce of the same player (replay)
			'debug_time_offset' => 0,          // LOCAL ONLY: shifts the server clock (seconds)
			'ip_header' => 'REMOTE_ADDR',      // LOCAL ONLY: $_SERVER key holding the client IP
			'log_dir' => '',                   // error log folder ('' = Fishing-game/logs)
		];
		$c = array_merge($defaults, $c);
		if (!$local) {
			$c['debug_time_offset'] = 0;
			$c['ip_header'] = 'REMOTE_ADDR';
		}
		$c['_local'] = $local;
		$c['_path'] = $path;
		$cfg = $c;
		return $cfg;
	}
	throw new RuntimeException('config_missing');
}

/** Server clock (unix seconds). */
function fv_now(): int
{
	static $offset = null;
	if ($offset === null) { $offset = (int)(fv_config()['debug_time_offset'] ?? 0); }
	return time() + $offset;
}

/**
 * Client IP for rate limits. When the direct peer is a private / loopback address (the hosting's own front-end proxy),
 * the proxy's X-Real-IP (or the last X-Forwarded-For hop, the one the proxy appended) is used instead.
 */
function fv_client_ip(): string
{
	$cfg = fv_config();
	$key = (string)$cfg['ip_header'];
	if ($cfg['_local'] && $key !== 'REMOTE_ADDR' && !empty($_SERVER[$key]) && is_string($_SERVER[$key])) {
		return substr(trim(explode(',', $_SERVER[$key])[0]), 0, 45);
	}
	$ip = trim((string)($_SERVER['REMOTE_ADDR'] ?? ''));
	$public = static fn(string $a): bool => filter_var($a, FILTER_VALIDATE_IP, FILTER_FLAG_NO_PRIV_RANGE | FILTER_FLAG_NO_RES_RANGE) !== false;
	if ($ip === '' || !$public($ip)) {
		$real = trim((string)($_SERVER['HTTP_X_REAL_IP'] ?? ''));
		$fwd = array_values(array_filter(array_map('trim', explode(',', (string)($_SERVER['HTTP_X_FORWARDED_FOR'] ?? '')))));
		if ($real !== '' && filter_var($real, FILTER_VALIDATE_IP) !== false) { $ip = $real; }
		elseif ($fwd && filter_var(end($fwd), FILTER_VALIDATE_IP) !== false) { $ip = end($fwd); }
	}
	return substr($ip !== '' ? $ip : '0.0.0.0', 0, 45);
}

function fv_db(): PDO
{
	static $db = null;
	if ($db !== null) { return $db; }
	$cfg = fv_config();
	$dsn = (string)$cfg['db_dsn'];
	if ($dsn === '') { throw new RuntimeException('db_dsn is empty'); }
	$opts = [
		PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
		PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
	];
	if (str_starts_with($dsn, 'mysql:')) {
		$opts[PDO::ATTR_EMULATE_PREPARES] = false;
		if (!str_contains($dsn, 'charset=')) { $dsn .= ';charset=utf8mb4'; }
	}
	$db = new PDO($dsn, (string)$cfg['db_user'], (string)$cfg['db_pass'], $opts);
	if (fv_driver() === 'sqlite') {
		$db->exec('PRAGMA busy_timeout = 8000');
		$db->exec('PRAGMA journal_mode = WAL');
		$db->exec('PRAGMA synchronous = NORMAL');
	} else {
		$db->exec("SET NAMES utf8mb4 COLLATE utf8mb4_unicode_ci");
	}
	return $db;
}

function fv_driver(): string
{
	return str_starts_with((string)fv_config()['db_dsn'], 'sqlite:') ? 'sqlite' : 'mysql';
}

/** Runs a prepared statement and returns it. */
function fv_q(string $sql, array $params = []): PDOStatement
{
	$st = fv_db()->prepare($sql);
	$i = 0;
	foreach ($params as $k => $v) {
		$key = is_int($k) ? ++$i : $k;
		if (is_int($v)) { $st->bindValue($key, $v, PDO::PARAM_INT); }
		elseif (is_bool($v)) { $st->bindValue($key, $v ? 1 : 0, PDO::PARAM_INT); }
		elseif ($v === null) { $st->bindValue($key, null, PDO::PARAM_NULL); }
		else { $st->bindValue($key, (string)$v, PDO::PARAM_STR); }
	}
	$st->execute();
	return $st;
}

function fv_row(string $sql, array $params = []): ?array
{
	$r = fv_q($sql, $params)->fetch();
	return $r === false ? null : $r;
}

function fv_all(string $sql, array $params = []): array
{
	return fv_q($sql, $params)->fetchAll();
}

function fv_val(string $sql, array $params = [])
{
	$v = fv_q($sql, $params)->fetchColumn();
	return $v === false ? null : $v;
}

/** "INSERT IGNORE" (MySQL) / "INSERT OR IGNORE" (SQLite). */
function fv_insert_ignore_sql(): string
{
	return fv_driver() === 'sqlite' ? 'INSERT OR IGNORE' : 'INSERT IGNORE';
}

/** A BLOB column value as a string (drivers may return streams). */
function fv_blob($v): string
{
	if (is_resource($v)) { return (string)stream_get_contents($v); }
	return (string)$v;
}

// ------------------------------------------------------------------ rate limits (table rate_log)

function fv_rate_count(string $key, int $window): int
{
	return (int)fv_val('SELECT COUNT(*) FROM rate_log WHERE k = ? AND ts > ?', [$key, fv_now() - $window]);
}

function fv_rate_add(string $key): void
{
	fv_q('INSERT INTO rate_log (k, ts) VALUES (?, ?)', [substr($key, 0, 80), fv_now()]);
}

/** Counts a hit; false (and no hit stored) when the limit [max, window] is already reached. */
function fv_rate_hit(string $key, array $limit): bool
{
	[$max, $window] = [(int)$limit[0], (int)$limit[1]];
	if ($max <= 0) { return true; }
	if (fv_rate_count($key, $window) >= $max) { return false; }
	fv_rate_add($key);
	return true;
}

/** Occasionally deletes old rate_log rows (older than 2 h: the longest window and the nonce lifetime). */
function fv_rate_prune(): void
{
	if (random_int(1, 50) === 1) {
		fv_q('DELETE FROM rate_log WHERE ts < ?', [fv_now() - 7300]);
	}
}

// ------------------------------------------------------------------ output / logging

function fv_json_headers(): void
{
	header('Content-Type: application/json; charset=utf-8');
	header('Cache-Control: no-store');
	header('X-Robots-Tag: noindex, nofollow');
	header('X-Content-Type-Options: nosniff');
}

function fv_json(array $data): string
{
	$s = json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);
	return $s === false ? '{"ok":false,"error":"server_error","message":"Ошибка сервера"}' : $s;
}

function fv_log_error(string $what): void
{
	$dir = FV_ROOT . '/logs';
	try { $custom = (string)(fv_config()['log_dir'] ?? ''); if ($custom !== '') { $dir = $custom; } } catch (Throwable $e) { }
	if (!is_dir($dir)) { @mkdir($dir, 0750, true); }
	if (!is_file($dir . '/.htaccess')) {
		@file_put_contents($dir . '/.htaccess', "<IfModule mod_authz_core.c>\n\tRequire all denied\n</IfModule>\n"
			. "<IfModule !mod_authz_core.c>\n\tOrder allow,deny\n\tDeny from all\n</IfModule>\n");
	}
	// a .php file whose first line exits: even if a front-end server ignores .htaccess, the web sees nothing
	$file = $dir . '/error_log.php';
	if (is_file($file) && filesize($file) > 2000000) { @rename($file, $dir . '/error_log.old.php'); }
	if (!is_file($file)) { @file_put_contents($file, "<?php http_response_code(404); exit; ?>\n"); }
	@file_put_contents($file, '[' . date('Y-m-d H:i:s') . '] ' . str_replace(["\r", "\n"], ' ', $what) . "\n", FILE_APPEND | LOCK_EX);
}

function fv_fmt_date(int $ts, bool $withTime = true): string
{
	if ($ts <= 0) { return '—'; }
	return date($withTime ? 'd.m.Y H:i' : 'd.m.Y', $ts);
}
