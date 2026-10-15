<?php
// Fishing Valley server: request signing (HMAC-SHA1 of the raw body with the player's secret), replay / rate checks,
// and the admin panel's session login + CSRF tokens + action log.
if (!defined('FV_ROOT')) { http_response_code(403); exit; }

require_once __DIR__ . '/db.php';
require_once __DIR__ . '/validate.php';

const FV_TS_WINDOW = 3600;

/** The X-FV-Sig header (or ?sig= when a hosting drops custom headers). */
function fv_request_sig(): string
{
	$sig = $_SERVER['HTTP_X_FV_SIG'] ?? ($_GET['sig'] ?? '');
	return is_string($sig) ? strtolower(trim($sig)) : '';
}

function fv_sign(string $body, string $secret): string
{
	return hash_hmac('sha1', $body, $secret);
}

/**
 * Verifies a signed request and returns the player's row. Throws FVError: bad_request, unknown_player, bad_time,
 * bad_sig, replay, banned (message contains the reason), rate_limited.
 */
function fv_verify_signed(string $raw, array $req, int $now): array
{
	$pid = fv_int($req['pid'] ?? null, 1, PHP_INT_MAX);
	$nonce = $req['nonce'] ?? '';
	if ($pid === null) { throw new FVError('bad_request', 'Неверный запрос: нет номера игрока.'); }
	if (!is_string($nonce) || preg_match('/^[0-9a-fA-F]{8,32}$/', $nonce) !== 1) {
		throw new FVError('bad_request', 'Неверный запрос: нет nonce.');
	}
	$p = fv_row('SELECT * FROM players WHERE id = ?', [$pid]);
	if (!$p) { throw new FVError('unknown_player', 'Игрок не найден на сервере.'); }
	$ts = fv_int($req['ts'] ?? null);
	if ($ts === null || abs($ts - $now) > FV_TS_WINDOW) {
		throw new FVError('bad_time', 'Неверное время запроса. Проверьте дату и время на устройстве.');
	}
	$sig = fv_request_sig();
	if ($sig === '' || !hash_equals(fv_sign($raw, (string)$p['secret']), $sig)) {
		throw new FVError('bad_sig', 'Неверная подпись запроса.');
	}
	if (fv_config()['check_nonce']) {
		$key = 'n:' . $pid . ':' . strtolower($nonce);
		if (fv_rate_count($key, FV_TS_WINDOW * 2 + 100) > 0) {
			throw new FVError('replay', 'Повторный запрос отклонён.');
		}
		fv_rate_add($key);
	}
	if ((int)$p['banned'] === 1) {
		$reason = trim((string)$p['ban_reason']);
		throw new FVError('banned', 'Аккаунт заблокирован' . ($reason !== '' ? ': ' . $reason : '.'), ['reason' => $reason]);
	}
	if (!fv_rate_hit('s:' . $pid, (array)fv_config()['rate_signed'])) {
		throw new FVError('rate_limited', 'Слишком много запросов. Попробуйте позже.');
	}
	return $p;
}

// ------------------------------------------------------------------ admin panel

function fv_is_https(): bool
{
	return (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
		|| strtolower((string)($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '')) === 'https'
		|| (int)($_SERVER['SERVER_PORT'] ?? 0) === 443;
}

function fv_admin_session_start(): void
{
	$path = str_replace('\\', '/', dirname((string)($_SERVER['SCRIPT_NAME'] ?? '/admin/index.php')));
	$path = rtrim($path, '/') . '/';
	ini_set('session.use_strict_mode', '1');
	ini_set('session.use_only_cookies', '1');
	ini_set('session.gc_maxlifetime', '28800');
	session_name('FVADMIN');
	session_set_cookie_params(['lifetime' => 0, 'path' => $path, 'secure' => fv_is_https(), 'httponly' => true, 'samesite' => 'Strict']);
	session_start();
	if (empty($_SESSION['csrf'])) { $_SESSION['csrf'] = bin2hex(random_bytes(32)); }
}

function fv_admin_logged_in(): bool
{
	if (empty($_SESSION['admin'])) { return false; }
	if ((int)($_SESSION['seen'] ?? 0) < time() - 8 * 3600) {       // 8 h without activity -> log in again
		$_SESSION['admin'] = false;
		return false;
	}
	$_SESSION['seen'] = time();
	return true;
}

/** Compares with config admin_password (plain text via hash_equals, or a password_hash() value via password_verify). */
function fv_admin_password_ok(string $pwd): bool
{
	$stored = (string)fv_config()['admin_password'];
	if ($stored === '' || $pwd === '') { return false; }
	if (preg_match('/^\$(2[aby]|argon2id?)\$/', $stored) === 1) { return password_verify($pwd, $stored); }
	return hash_equals($stored, $pwd);
}

function fv_csrf_token(): string
{
	return (string)($_SESSION['csrf'] ?? '');
}

function fv_csrf_ok(): bool
{
	$t = $_POST['csrf'] ?? '';
	return is_string($t) && $t !== '' && hash_equals(fv_csrf_token(), $t);
}

function fv_admin_log(string $action, ?int $pid, string $details = ''): void
{
	fv_q('INSERT INTO admin_log (ts, action, player_id, details, ip) VALUES (?, ?, ?, ?, ?)',
		[fv_now(), mb_substr($action, 0, 40), $pid, mb_substr($details, 0, 2000), fv_client_ip()]);
}
