<?php
// Fishing Valley — admin panel (Docs/UPDATE4_ONLINE.md §6): login with config admin_password, session + CSRF on every
// form, login rate limit, every action logged to admin_log. Pages: dashboard, players, player, seasons, season, mail, log.
// Update 6: game icons (../img/icons, lib/art.php) next to items, fish and cosmetics; the gift item is chosen in a
// searchable icon grid (admin.js; works as plain radio buttons without JS); fish can be gifted too (the game puts them
// into the keepnet); the player card shows the 0.6 progress (level of 15, earned XP, стаж, arrival in the City).
declare(strict_types=1);

define('FV_ROOT', dirname(__DIR__));
require FV_ROOT . '/lib/db.php';
require FV_ROOT . '/lib/auth.php';
require FV_ROOT . '/lib/board.php';
require FV_ROOT . '/lib/settings.php';
require FV_ROOT . '/lib/schema.php';
require FV_ROOT . '/lib/art.php';

header('Content-Type: text/html; charset=utf-8');
header('X-Robots-Tag: noindex, nofollow');
header('X-Frame-Options: DENY');
header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: no-referrer');
header("Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
header('Cache-Control: no-store');

const FV_Q_NAMES = ['обычная', 'серебряная', 'золотая', 'иридиевая'];
const FV_LOG_NAMES = [
	'login' => 'Вход', 'login_fail' => 'Неверный пароль', 'login_blocked' => 'Вход заблокирован', 'logout' => 'Выход',
	'ban' => 'Блокировка', 'unban' => 'Разблокировка', 'notes' => 'Заметки', 'gift' => 'Подарок', 'message' => 'Сообщение',
	'reset_name' => 'Сброс имени', 'reset_susp' => 'Сброс подозрений', 'accept_baseline' => 'Сброс базы счётчиков',
	'make_code' => 'Код восстановления', 'revoke_code' => 'Отзыв кода', 'revoke_mail' => 'Отзыв письма',
	'download_save' => 'Скачано сохранение', 'broadcast' => 'Рассылка', 'restore_used' => 'Игрок восстановил сохранение',
	'csrf_fail' => 'Ошибка CSRF', 'version' => 'Версия игры',
];
const FV_CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';   // no I, L, O, 0, 1

// ================================================================== helpers

function h($s): string
{
	return htmlspecialchars((string)$s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function url(array $params = []): string
{
	$params = array_filter($params, static fn($v) => $v !== null && $v !== '');
	return 'index.php' . ($params ? '?' . http_build_query($params) : '');
}

function redirect(array $params = []): void
{
	header('Location: ' . url($params), true, 303);
	exit;
}

function flash(string $msg, string $kind = 'ok'): void
{
	$_SESSION['flash'][] = [$kind, $msg];
}

function num($n): string
{
	return number_format((float)$n, 0, ',', ' ');
}

function dur(int $sec): string
{
	$sec = max(0, $sec);
	$d = intdiv($sec, 86400); $h = intdiv($sec % 86400, 3600); $m = intdiv($sec % 3600, 60);
	if ($d > 0) { return $d . ' д ' . $h . ' ч ' . $m . ' мин'; }
	if ($h > 0) { return $h . ' ч ' . $m . ' мин'; }
	return $m . ' мин';
}

function ago(int $ts, int $now): string
{
	if ($ts <= 0) { return 'никогда'; }
	$d = $now - $ts;
	if ($d < 120) { return 'только что'; }
	if ($d < 3600) { return intdiv($d, 60) . ' мин назад'; }
	if ($d < 86400) { return intdiv($d, 3600) . ' ч назад'; }
	if ($d < 86400 * 30) { return intdiv($d, 86400) . ' д назад'; }
	return fv_fmt_date($ts, false);
}

function bytes_text(int $b): string
{
	return $b >= 1048576 ? number_format($b / 1048576, 1, ',', ' ') . ' МБ' : number_format($b / 1024, 0, ',', ' ') . ' КБ';
}

function stars(int $q): string
{
	return str_repeat('★', max(0, min(3, $q))) ?: '—';
}

/** Game icon (../img/icons/<id>.png) in a Size x Size box, «?» when it was not uploaded. */
function ico(string $id, int $size = 24, string $title = ''): string
{
	return fv_icon($id, '../', $size, $title);
}

/** Icon + name of an item / fish / cosmetic id. */
function thing(string $id, int $size = 24): string
{
	$name = isset(FV_ITEMS[$id]) || isset(FV_FISH[$id]) ? fv_item_name($id) : (FV_COSMETICS_U6[$id] ?? (fv_art_name($id) ?: $id));
	return '<span class="thing">' . ico($id, $size) . h($name) . '</span>';
}

/** Ids a letter can carry: items (backpack) and fish (keepnet) — the game's GiveItem accepts both, not cosmetics. */
function giftable(string $id): bool
{
	return isset(FV_ITEMS[$id]) || isset(FV_FISH[$id]);
}

function fish_show($f): string
{
	if (!is_array($f) || ($f['fish'] ?? '') === '') { return '—'; }
	return '<span class="thing">' . ico((string)$f['fish'], 24) . h(fv_fish_name((string)$f['fish'])) . '</span> · ' . h(number_format((float)$f['size'], 1, ',', '')) . ' см'
		. ((int)($f['q'] ?? 0) > 0 ? ' <span class="q" title="' . h(FV_Q_NAMES[(int)$f['q']] ?? '') . '">' . stars((int)$f['q']) . '</span>' : '');
}

function player_link(array $p): string
{
	$id = (int)($p['pid'] ?? $p['id'] ?? 0);
	return '<a href="' . h(url(['p' => 'player', 'id' => $id])) . '">' . h($p['name'] ?? ('#' . (1000 + $id))) . '</a> <span class="tag">#'
		. (1000 + $id) . '</span>';
}

function csrf_field(): string
{
	return '<input type="hidden" name="csrf" value="' . h(fv_csrf_token()) . '">';
}

function post_int(string $k, int $min, int $max, int $def = 0): int
{
	$v = fv_int($_POST[$k] ?? '', $min, $max);
	return $v ?? $def;
}

function post_str(string $k, int $maxLen): string
{
	$v = $_POST[$k] ?? '';
	if (!is_string($v) || !mb_check_encoding($v, 'UTF-8')) { return ''; }
	$v = str_replace("\r\n", "\n", trim($v));
	return mb_substr($v, 0, $maxLen);
}

function new_restore_code(): string
{
	$a = FV_CODE_ALPHABET;
	$s = '';
	for ($i = 0; $i < 8; $i++) { $s .= $a[random_int(0, strlen($a) - 1)]; }
	return $s;
}

function page_start(string $title, string $active): void
{
	$nav = ['dash' => 'Обзор', 'players' => 'Игроки', 'seasons' => 'Турниры', 'mail' => 'Рассылка', 'version' => 'Версия игры', 'log' => 'Журнал'];
	echo '<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
		. '<meta name="robots" content="noindex,nofollow"><title>' . h($title) . ' — Клёвая долина</title>'
		. '<link rel="stylesheet" href="style.css?v=3"><script src="admin.js?v=1" defer></script></head><body>';
	echo '<header class="top"><div class="wrap"><a class="brand" href="index.php"><span class="logo">~</span>Клёвая долина <small>админка</small></a>';
	if (!empty($_SESSION['admin'])) {
		echo '<nav>';
		foreach ($nav as $k => $label) {
			echo '<a href="' . h(url(['p' => $k === 'dash' ? null : $k])) . '"' . ($k === $active ? ' class="on"' : '') . '>' . h($label) . '</a>';
		}
		echo '<form method="post" class="inline">' . csrf_field() . '<input type="hidden" name="do" value="logout">'
			. '<button class="link">Выйти</button></form></nav>';
	}
	echo '</div></header><main class="wrap">';
	foreach ($_SESSION['flash'] ?? [] as [$kind, $msg]) {
		echo '<div class="flash ' . h($kind) . '">' . ($kind === 'code' ? $msg : h($msg)) . '</div>';
	}
	$_SESSION['flash'] = [];
}

function page_end(): void
{
	echo '</main><footer class="wrap foot">Сервер ' . h(FV_SERVER_VERSION) . ' · время сервера ' . h(fv_fmt_date(fv_now())) . ' (МСК)</footer></body></html>';
	exit;
}

function fatal_page(string $title, string $text): void
{
	echo '<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
		. '<meta name="robots" content="noindex,nofollow"><title>' . h($title) . '</title><link rel="stylesheet" href="style.css?v=2"></head>'
		. '<body><main class="wrap"><div class="card narrow"><h1>' . h($title) . '</h1><p>' . h($text) . '</p></div></main></body></html>';
	exit;
}

// ================================================================== bootstrap

try {
	fv_config();
	fv_db();
	fv_q('SELECT 1 FROM players LIMIT 1');
	try { fv_schema_upgrade(); } catch (Throwable $e) { fv_log_error('admin schema upgrade: ' . $e->getMessage()); }
} catch (Throwable $e) {
	if (!($e instanceof RuntimeException && str_starts_with($e->getMessage(), 'config_'))) { fv_log_error('admin: ' . $e->getMessage()); }
	fatal_page('Сервер не настроен', 'Создайте config.php по образцу config.sample.php и запустите install.php (см. Docs/SERVER.md).');
}
fv_admin_session_start();
$now = fv_now();
try { fv_season_close_due($now); } catch (Throwable $e) { fv_log_error('admin season close: ' . $e->getMessage()); }

// ================================================================== POST actions

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
	$do = is_string($_POST['do'] ?? null) ? $_POST['do'] : '';
	$pid = post_int('id', 1, PHP_INT_MAX);
	if (!fv_csrf_ok()) {
		if (fv_admin_logged_in()) { fv_admin_log('csrf_fail', $pid ?: null, $do); }
		flash('Форма устарела — обновите страницу и повторите.', 'err');
		redirect($pid ? ['p' => 'player', 'id' => $pid] : []);
	}
	if ($do === 'login') { handle_login(); }
	if (!fv_admin_logged_in()) { redirect(); }
	handle_action($do, $pid, $now);
}

function handle_login(): void
{
	$ip = fv_client_ip();
	$lim = (array)fv_config()['rate_admin_login'];
	if (fv_rate_count('al:' . $ip, (int)$lim[1]) >= (int)$lim[0]) {
		fv_admin_log('login_blocked', null, 'IP ' . $ip);
		flash('Слишком много неудачных попыток. Подождите ' . (int)ceil((int)$lim[1] / 60) . ' мин.', 'err');
		redirect();
	}
	$pwd = is_string($_POST['password'] ?? null) ? $_POST['password'] : '';
	if ((string)fv_config()['admin_password'] === '') {
		flash('Пароль админки не задан: впишите admin_password в config.php.', 'err');
		redirect();
	}
	if (!fv_admin_password_ok($pwd)) {
		fv_rate_add('al:' . $ip);
		fv_admin_log('login_fail', null, 'IP ' . $ip);
		flash('Неверный пароль.', 'err');
		redirect();
	}
	session_regenerate_id(true);
	$_SESSION['admin'] = true;
	$_SESSION['seen'] = time();
	$_SESSION['csrf'] = bin2hex(random_bytes(32));
	fv_admin_log('login', null, 'IP ' . $ip);
	redirect();
}

function handle_action(string $do, int $pid, int $now): void
{
	if ($do === 'logout') {
		fv_admin_log('logout', null);
		$_SESSION = [];
		session_regenerate_id(true);
		redirect();
	}
	if ($do === 'broadcast') { handle_broadcast($now); }
	if ($do === 'version') { handle_version($now); }

	$p = $pid > 0 ? fv_row('SELECT * FROM players WHERE id = ?', [$pid]) : null;
	if (!$p) { flash('Игрок не найден.', 'err'); redirect(['p' => 'players']); }
	$back = ['p' => 'player', 'id' => $pid];
	$who = $p['name'] . ' #' . (1000 + $pid);

	switch ($do) {
		case 'ban':
			$reason = post_str('reason', 200);
			if ($reason === '') { flash('Укажите причину блокировки.', 'err'); break; }
			fv_q('UPDATE players SET banned = 1, ban_reason = ? WHERE id = ?', [$reason, $pid]);
			fv_admin_log('ban', $pid, $reason);
			flash('Игрок ' . $who . ' заблокирован.');
			break;
		case 'unban':
			fv_q("UPDATE players SET banned = 0, ban_reason = '' WHERE id = ?", [$pid]);
			fv_admin_log('unban', $pid);
			flash('Игрок ' . $who . ' разблокирован.');
			break;
		case 'notes':
			$notes = post_str('notes', 6000);
			fv_q('UPDATE players SET notes = ? WHERE id = ?', [$notes, $pid]);
			fv_admin_log('notes', $pid, mb_strlen($notes) . ' симв.');
			flash('Заметки сохранены.');
			break;
		case 'gift':
			$coins = post_int('coins', 0, 10000000);
			$xp = post_int('xp', 0, 1000000);
			$lvl = post_int('set_level', 0, FV_MAX_LEVEL);
			$item = is_string($_POST['item'] ?? null) ? $_POST['item'] : '';
			$n = post_int('item_n', 1, 999, 1);
			$items = giftable($item) ? [['id' => $item, 'n' => $n]] : [];
			if ($coins === 0 && $xp === 0 && $lvl === 0 && !$items) { flash('Подарок пустой: укажите монеты, опыт, уровень или предмет.', 'err'); break; }
			$title = post_str('title', 100) ?: 'Подарок от администрации';
			$body = post_str('body', 2000) ?: 'Спасибо, что играете в «Клёвую долину»!';
			$mid = fv_mail_insert($pid, 'admin', $title, $body, $coins, $xp, $lvl, $items, $now);
			fv_admin_log('gift', $pid, sprintf('письмо #%d: монеты %d, опыт %d, уровень %d%s', $mid, $coins, $xp, $lvl,
				$items ? ', ' . fv_item_name($item) . ' ×' . $n : ''));
			flash('Подарок отправлен (письмо #' . $mid . ').');
			break;
		case 'message':
			$title = post_str('title', 100);
			$body = post_str('body', 2000);
			if ($title === '' || $body === '') { flash('Заполните заголовок и текст.', 'err'); break; }
			$mid = fv_mail_insert($pid, 'admin', $title, $body, 0, 0, 0, [], $now);
			fv_admin_log('message', $pid, 'письмо #' . $mid . ': ' . $title);
			flash('Сообщение отправлено (письмо #' . $mid . ').');
			break;
		case 'reset_name':
			fv_q('UPDATE players SET name = ?, name_lc = ? WHERE id = ?', ['Рыбак', mb_strtolower('Рыбак'), $pid]);
			fv_admin_log('reset_name', $pid, 'было: ' . $p['name']);
			flash('Имя сброшено на «Рыбак».');
			break;
		case 'reset_susp':
			fv_q('UPDATE players SET suspicious = 0 WHERE id = ?', [$pid]);
			fv_admin_log('reset_susp', $pid, 'было: ' . (int)$p['suspicious']);
			flash('Счётчик подозрений обнулён.');
			break;
		case 'accept_baseline':
			fv_q('UPDATE players SET reset_baseline = 1 WHERE id = ?', [$pid]);
			fv_admin_log('accept_baseline', $pid);
			flash('Следующая синхронизация примет счётчики игрока как новую базу (даже если они меньше).');
			break;
		case 'make_code':
			$saveId = post_int('save_id', 1, PHP_INT_MAX);
			$save = fv_row('SELECT id, created FROM saves WHERE id = ? AND player_id = ?', [$saveId, $pid]);
			if (!$save) { flash('Сохранение не найдено.', 'err'); break; }
			$code = '';
			for ($try = 0; $try < 5 && $code === ''; $try++) {
				$c = new_restore_code();
				try {
					fv_q('INSERT INTO restore_codes (code, player_id, save_id, created, expires, used, used_ip) VALUES (?, ?, ?, ?, ?, NULL, NULL)',
						[$c, $pid, $saveId, $now, $now + 7 * 86400]);
					$code = $c;
				} catch (PDOException $e) { /* collision: try another */ }
			}
			if ($code === '') { flash('Не удалось создать код, попробуйте ещё раз.', 'err'); break; }
			fv_admin_log('make_code', $pid, 'код ' . $code . ', сохранение #' . $saveId);
			flash('Код восстановления: <b class="code">' . h($code) . '</b><br>Действует 7 дней, один раз. Игрок вводит его в «Настройки → Восстановить сохранение».', 'code');
			break;
		case 'revoke_code':
			$code = strtoupper(post_str('code', 8));
			$n = fv_q('UPDATE restore_codes SET expires = ? WHERE code = ? AND player_id = ? AND used IS NULL', [$now, $code, $pid])->rowCount();
			if ($n) { fv_admin_log('revoke_code', $pid, 'код ' . $code); flash('Код ' . $code . ' отозван.'); }
			else { flash('Код не найден или уже использован.', 'err'); }
			break;
		case 'revoke_mail':
			$mid = post_int('mail_id', 1, PHP_INT_MAX);
			$n = fv_q('DELETE FROM mail WHERE id = ? AND player_id = ? AND claimed IS NULL', [$mid, $pid])->rowCount();
			if ($n) { fv_admin_log('revoke_mail', $pid, 'письмо #' . $mid); flash('Письмо #' . $mid . ' отозвано.'); }
			else { flash('Письмо не найдено или уже получено.', 'err'); }
			break;
		default:
			flash('Неизвестное действие.', 'err');
	}
	redirect($back);
}

function handle_broadcast(int $now): void
{
	$title = post_str('title', 100);
	$body = post_str('body', 2000);
	$coins = post_int('coins', 0, 1000000);
	$xp = post_int('xp', 0, 100000);
	$item = is_string($_POST['item'] ?? null) ? $_POST['item'] : '';
	$n = post_int('item_n', 1, 999, 1);
	$items = giftable($item) ? [['id' => $item, 'n' => $n]] : [];
	$days = post_int('days', 0, 3650);
	if ($title === '' || $body === '') { flash('Заполните заголовок и текст.', 'err'); redirect(['p' => 'mail']); }
	if (($_POST['confirm'] ?? '') !== '1') { flash('Поставьте галочку «Отправить всем».', 'err'); redirect(['p' => 'mail']); }
	$where = 'banned = 0' . ($days > 0 ? ' AND last_seen >= ' . ($now - $days * 86400) : '');
	$cnt = fv_q('INSERT INTO mail (player_id, kind, title, body, coins, xp, set_level, items_json, created, claimed)
		SELECT id, ?, ?, ?, ?, ?, 0, ?, ?, NULL FROM players WHERE ' . $where,
		['admin', $title, $body, $coins, $xp, json_encode($items, JSON_UNESCAPED_UNICODE), $now])->rowCount();
	fv_admin_log('broadcast', null, sprintf('«%s» → %d игр.; монеты %d, опыт %d%s', $title, $cnt, $coins, $xp,
		$items ? ', ' . fv_item_name($item) . ' ×' . $n : ''));
	flash('Рассылка отправлена: ' . $cnt . ' писем.');
	redirect(['p' => 'mail']);
}

/** «Версия игры»: latest / minimal build, version name, APK URL, changelog (table settings). */
function handle_version(int $now): void
{
	$back = ['p' => 'version'];
	if (!fv_settings_ready()) {
		flash('Нет таблицы настроек: откройте install.php?key=… ещё раз (данные игроков сохранятся).', 'err');
		redirect($back);
	}
	$latest = fv_int($_POST['latest_build'] ?? '', 1, FV_BUILD_MAX);
	$min = fv_int($_POST['min_build'] ?? '', 0, FV_BUILD_MAX);
	$name = fv_collapse(post_str('latest_name', 40));
	$url = trim(post_str('download_url', 400));
	$log = post_str('changelog', 2001);
	$err = null;
	if ($latest === null) { $err = 'Номер сборки — целое число от 1.'; }
	elseif ($min === null) { $err = 'Минимальная сборка — целое число от 0.'; }
	elseif ($min > $latest) { $err = 'Минимальная сборка не может быть больше последней (' . $latest . ').'; }
	elseif (preg_match('/^[0-9A-Za-zА-Яа-яЁё.+()\- ]{1,16}$/u', $name) !== 1) { $err = 'Название версии: 1–16 символов, например 0.5 или 1.0.2.'; }
	elseif (!fv_valid_download_url($url)) { $err = 'Ссылка на APK должна начинаться с https:// (или http://) и не содержать пробелов.'; }
	elseif (mb_strlen($log) > 2000) { $err = '«Что нового» — не длиннее 2000 символов.'; }
	if ($err !== null) { flash($err, 'err'); redirect($back); }

	$new = ['latest_name' => $name, 'latest_build' => (string)$latest, 'min_build' => (string)$min, 'download_url' => $url, 'changelog' => $log];
	$labels = ['latest_name' => 'версия', 'latest_build' => 'сборка', 'min_build' => 'мин. сборка', 'download_url' => 'ссылка', 'changelog' => 'что нового'];
	$changes = [];
	foreach ($new as $k => $v) {
		$old = fv_setting($k);
		if ($old === $v) { continue; }
		$changes[] = $k === 'changelog' ? 'что нового: ' . mb_strlen($v) . ' симв.' : $labels[$k] . ' ' . $old . ' → ' . $v;
	}
	if (!$changes) { flash('Ничего не изменилось.'); redirect($back); }
	foreach ($new as $k => $v) { fv_setting_set($k, $v); }
	fv_setting_set('version_updated', (string)$now);
	fv_admin_log('version', null, implode('; ', $changes));
	flash('Версия игры сохранена: ' . $name . ' (сборка ' . $latest . ', играть можно со сборки ' . $min . ').');
	redirect($back);
}

// ================================================================== GET pages

if (!fv_admin_logged_in()) {
	page_start('Вход', '');
	echo '<div class="card narrow login"><h1>Вход в админку</h1><form method="post">' . csrf_field()
		. '<input type="hidden" name="do" value="login"><label>Пароль<input type="password" name="password" autocomplete="current-password" required autofocus></label>'
		. '<button class="btn primary">Войти</button></form></div>';
	page_end();
}

$page = is_string($_GET['p'] ?? null) ? $_GET['p'] : 'dash';
switch ($page) {
	case 'players': page_players($now); break;
	case 'player': page_player($now); break;
	case 'save': download_save(); break;
	case 'seasons': page_seasons($now); break;
	case 'season': page_season($now); break;
	case 'mail': page_mail(); break;
	case 'version': page_version($now); break;
	case 'log': page_log($now); break;
	default: page_dash($now);
}

// ------------------------------------------------------------------ dashboard

function page_dash(int $now): void
{
	page_start('Обзор', 'dash');
	$total = (int)fv_val('SELECT COUNT(*) FROM players');
	$day = (int)fv_val('SELECT COUNT(*) FROM players WHERE last_seen >= ?', [$now - 86400]);
	$week = (int)fv_val('SELECT COUNT(*) FROM players WHERE last_seen >= ?', [$now - 7 * 86400]);
	$banned = (int)fv_val('SELECT COUNT(*) FROM players WHERE banned = 1');
	$susp = (int)fv_val('SELECT COUNT(*) FROM players WHERE suspicious > 0');
	$saves = fv_row('SELECT COUNT(*) AS n, COALESCE(SUM(size), 0) AS s FROM saves');
	$mailOpen = (int)fv_val('SELECT COUNT(*) FROM mail WHERE claimed IS NULL');
	echo '<h1>Обзор</h1><div class="stats">';
	foreach ([
		['Игроков', num($total)], ['Активны за сутки', num($day)], ['За неделю', num($week)], ['Заблокировано', num($banned)],
		['Подозрительных', num($susp)], ['Сохранений', num($saves['n']) . ' · ' . h(bytes_text((int)$saves['s']))],
		['Писем не забрано', num($mailOpen)],
		['Версия игры', '<a href="' . h(url(['p' => 'version'])) . '">' . h(fv_version_info()['latest_name']) . '</a>'],
	] as [$label, $val]) {
		echo '<div class="stat"><b>' . $val . '</b><span>' . h($label) . '</span></div>';
	}
	echo '</div>';

	$s = fv_season_info($now);
	$my = fv_season_standings($s['id']);
	echo '<section class="card"><h2>' . h(fv_season_name($s['id'])) . ' <span class="muted">идёт</span></h2>'
		. '<p>' . h(fv_fmt_date($s['starts'])) . ' — ' . h(fv_fmt_date($s['ends'])) . ' · до конца <b>' . h(dur($s['ends'] - $now)) . '</b> · участников: <b>'
		. (int)$my['participants'] . '</b></p>';
	$b = fv_board('s_fish', '', 0, 5, 0, $now);
	if ($b['entries']) {
		echo '<div class="scroll"><table><tr><th>Место</th><th>Игрок</th><th>Ур.</th><th class="r">Улов</th></tr>';
		foreach ($b['entries'] as $e) {
			echo '<tr><td>' . medal($e['rank']) . '</td><td>' . player_link($e) . '</td><td>' . (int)$e['level'] . '</td><td class="r">' . num($e['value']) . '</td></tr>';
		}
		echo '</table></div>';
	} else {
		echo '<p class="muted">Пока никто не поймал рыбу в этом турнире.</p>';
	}
	echo '<p><a href="' . h(url(['p' => 'season', 'id' => $s['id']])) . '">Все участники и прогноз наград →</a></p></section>';

	$rows = fv_all('SELECT * FROM players WHERE suspicious > 0 ORDER BY suspicious DESC, last_seen DESC LIMIT 15');
	echo '<section class="card"><h2>Подозрительные игроки</h2>';
	if ($rows) {
		echo '<div class="scroll"><table><tr><th>Игрок</th><th class="r">Подозр.</th><th>Ур.</th><th class="r">Рыба</th><th>В сети</th><th>Последняя отметка</th></tr>';
		foreach ($rows as $r) {
			$lastNote = trim((string)strrchr("\n" . trim((string)$r['notes']), "\n"));
			echo '<tr><td>' . player_link($r) . ((int)$r['banned'] ? ' <span class="badge red">бан</span>' : '') . '</td><td class="r"><b>'
				. (int)$r['suspicious'] . '</b></td><td>' . (int)$r['level'] . '</td><td class="r">' . num($r['fish_total']) . '</td><td>'
				. h(ago((int)$r['last_seen'], $now)) . '</td><td class="small">' . h(mb_substr($lastNote, 0, 140)) . '</td></tr>';
		}
		echo '</table></div>';
	} else {
		echo '<p class="muted">Нет — все ведут себя честно.</p>';
	}
	echo '</section>';

	$rows = fv_all('SELECT * FROM players ORDER BY id DESC LIMIT 8');
	echo '<section class="card"><h2>Новые игроки</h2>';
	if ($rows) {
		echo '<div class="scroll"><table><tr><th>Игрок</th><th>Платформа</th><th>Регистрация</th><th>В сети</th></tr>';
		foreach ($rows as $r) {
			echo '<tr><td>' . player_link($r) . '</td><td>' . h($r['platform']) . ' ' . h($r['ver']) . '</td><td>' . h(fv_fmt_date((int)$r['created']))
				. '</td><td>' . h(ago((int)$r['last_seen'], $now)) . '</td></tr>';
		}
		echo '</table></div>';
	} else {
		echo '<p class="muted">Игроков пока нет.</p>';
	}
	echo '</section>';
	page_end();
}

function medal(int $rank): string
{
	$m = [1 => 'gold', 2 => 'silver', 3 => 'bronze'];
	return isset($m[$rank]) ? '<span class="medal ' . $m[$rank] . '">' . $rank . '</span>' : (string)$rank;
}

// ------------------------------------------------------------------ players

function admin_search_sql(string $q): array
{
	$q = fv_collapse($q);
	if ($q === '') { return ['', []]; }
	if (preg_match('/^#\s*(\d{1,10})$/', $q, $m)) { return [' AND id = ?', [(int)$m[1] - 1000]]; }
	if (preg_match('/^\d{1,10}$/', $q)) { return [' AND (id = ? OR id = ?)', [(int)$q, (int)$q - 1000]]; }
	$like = '%' . strtr(mb_strtolower($q), ['!' => '!!', '%' => '!%', '_' => '!_']) . '%';
	return [" AND name_lc LIKE ? ESCAPE '!'", [$like]];
}

function page_players(int $now): void
{
	page_start('Игроки', 'players');
	$q = is_string($_GET['q'] ?? null) ? mb_substr($_GET['q'], 0, 40) : '';
	$f = is_string($_GET['f'] ?? null) ? $_GET['f'] : '';
	$sorts = ['id' => 'id', 'name' => 'name_lc', 'level' => 'xp', 'fish' => 'fish_total', 'earned' => 'coins_earned',
		'susp' => 'suspicious', 'seen' => 'last_seen'];
	$sort = is_string($_GET['s'] ?? null) && isset($sorts[$_GET['s']]) ? $_GET['s'] : 'seen';
	$dir = ($_GET['d'] ?? '') === 'asc' ? 'asc' : 'desc';
	$pg = max(1, fv_int($_GET['pg'] ?? 1, 1, 100000) ?? 1);
	[$sw, $sp] = admin_search_sql($q);
	$where = ' WHERE 1 = 1' . $sw . ($f === 'banned' ? ' AND banned = 1' : '') . ($f === 'susp' ? ' AND suspicious > 0' : '');
	$total = (int)fv_val('SELECT COUNT(*) FROM players' . $where, $sp);
	$per = 50;
	$rows = fv_all('SELECT * FROM players' . $where . ' ORDER BY ' . $sorts[$sort] . ' ' . strtoupper($dir) . ', id DESC LIMIT ' . $per
		. ' OFFSET ' . (($pg - 1) * $per), $sp);

	echo '<h1>Игроки <span class="muted">' . num($total) . '</span></h1>';
	echo '<form class="toolbar" method="get"><input type="hidden" name="p" value="players">'
		. '<input type="search" name="q" value="' . h($q) . '" placeholder="Имя, #тег или номер">'
		. '<select name="f"><option value="">Все</option><option value="susp"' . ($f === 'susp' ? ' selected' : '') . '>Подозрительные</option>'
		. '<option value="banned"' . ($f === 'banned' ? ' selected' : '') . '>Заблокированные</option></select>'
		. '<button class="btn">Найти</button></form>';
	$base = ['p' => 'players', 'q' => $q, 'f' => $f];
	$th = static function (string $key, string $label, string $cls = '') use ($base, $sort, $dir): string {
		$nd = ($sort === $key && $dir === 'desc') ? 'asc' : 'desc';
		$arrow = $sort === $key ? ($dir === 'desc' ? ' ▼' : ' ▲') : '';
		return '<th' . ($cls ? ' class="' . $cls . '"' : '') . '><a href="' . h(url($base + ['s' => $key, 'd' => $nd])) . '">' . h($label) . $arrow . '</a></th>';
	};
	if (!$rows) {
		echo '<div class="card"><p class="muted">Никого не нашли.</p></div>';
	} else {
		echo '<div class="card scroll"><table class="list"><tr>' . $th('id', '№') . $th('name', 'Имя') . $th('level', 'Ур.') . $th('fish', 'Рыба', 'r')
			. $th('earned', 'Заработано', 'r') . $th('susp', 'Подозр.', 'r') . $th('seen', 'В сети') . '</tr>';
		foreach ($rows as $r) {
			echo '<tr' . ((int)$r['banned'] ? ' class="banned"' : '') . '><td>' . (int)$r['id'] . '</td><td>' . player_link($r)
				. ((int)$r['banned'] ? ' <span class="badge red">бан</span>' : '') . '</td><td>' . (int)$r['level'] . '</td><td class="r">'
				. num($r['fish_total']) . '</td><td class="r">' . num($r['coins_earned']) . '</td><td class="r">'
				. ((int)$r['suspicious'] > 0 ? '<b class="warn">' . (int)$r['suspicious'] . '</b>' : '0') . '</td><td>' . h(ago((int)$r['last_seen'], $now)) . '</td></tr>';
		}
		echo '</table></div>';
	}
	$pages = (int)ceil($total / $per);
	if ($pages > 1) {
		echo '<div class="pager">';
		if ($pg > 1) { echo '<a class="btn" href="' . h(url($base + ['s' => $sort, 'd' => $dir, 'pg' => $pg - 1])) . '">← Назад</a>'; }
		echo '<span>Страница ' . $pg . ' из ' . $pages . '</span>';
		if ($pg < $pages) { echo '<a class="btn" href="' . h(url($base + ['s' => $sort, 'd' => $dir, 'pg' => $pg + 1])) . '">Дальше →</a>'; }
		echo '</div>';
	}
	page_end();
}

// ------------------------------------------------------------------ player

/** Picker group of a giftable id (tabs of item_picker). */
function gift_group(string $id): string
{
	if (isset(FV_FISH[$id])) { return 'fish'; }
	$p = strstr($id, '_', true) ?: $id;
	if (in_array($p, ['res', 'mat', 'bp'], true)) { return 'mat'; }
	if (in_array($p, ['food', 'veg', 'seed'], true)) { return 'food'; }
	return $p === 'bait' ? 'bait' : 'tackle';
}

/**
 * The letter's item: a grid of game icons with names (radio buttons name="item"), a search box and group tabs (admin.js;
 * without JS the grid still works). Update-6 items and fish carry the «0.6» mark: older clients cannot store them.
 */
function item_picker(): string
{
	$groups = ['' => 'Все', 'mat' => 'Материалы', 'food' => 'Еда и огород', 'tackle' => 'Снасти', 'bait' => 'Наживки', 'fish' => 'Рыба'];
	$u6 = array_flip(FV_ITEMS_U6);
	$cell = static function (string $id, string $name, string $group, string $extra, string $search, bool $new): string {
		return '<label class="pick" data-g="' . h($group) . '" data-s="' . h(mb_strtolower($name . ' ' . $id . ' ' . $search)) . '">'
			. '<input type="radio" name="item" value="' . h($id) . '"' . ($id === '' ? ' checked' : '') . '><span class="pick-b">'
			. ($id === '' ? '<span class="ico ico-none ico-32">—</span>' : ico($id, 32))
			. '<span class="pick-n">' . h($name) . '</span>' . $extra
			. ($new ? '<span class="pick-u6" title="Обновление 6: игра 0.6 и новее">0.6</span>' : '') . '</span></label>';
	};
	$html = '<div class="picker" data-picker><div class="picker-bar"><input type="search" class="picker-q" placeholder="Поиск: название, id или место"'
		. ' aria-label="Поиск предмета или рыбы"><span class="picker-sel">Выбрано: <b>без предмета</b></span></div><div class="picker-tabs" hidden>';
	foreach ($groups as $g => $label) {
		$html .= '<button type="button" class="btn small' . ($g === '' ? ' on' : '') . '" data-g="' . $g . '">' . h($label) . '</button>';
	}
	$html .= '</div><div class="picker-grid">' . $cell('', 'без предмета', 'none', '', '', false);
	foreach (FV_ITEMS as $id => $name) { $html .= $cell($id, $name, gift_group($id), '', '', isset($u6[$id])); }
	foreach (FV_FISH as $id => $f) {
		$loc = fv_location_name((string)$f['loc']);
		$html .= $cell($id, (string)$f['name'], 'fish', '<span class="pick-m">' . ($f['legendary'] ? '★ ' : '') . h($loc) . '</span>',
			$loc . ($f['legendary'] ? ' легенда' : '') . (str_starts_with($id, 'e') ? ' эксперт' : ''), (bool)$f['u6']);
	}
	return $html . '</div><p class="small muted">Метка «0.6» — Обновление 6: такие предметы и рыбу примет только игра 0.6 и новее (в старой версии '
		. 'письмо скажет «не хватило места»). Рыба кладётся в садок. Косметику письмом выдать нельзя — игра принимает только предметы и рыбу.</p></div>';
}

/** «Прогресс 0.6»: level of 15 with the bar to the next one, XP, earned XP, стаж, arrival in the City (Р1: the XP
    stops at level 10 until the first arrival in the City, which gives level 11 at once). */
function progress_card(array $p): string
{
	$lvl = max(1, min(FV_MAX_LEVEL, (int)$p['level']));
	$xp = (int)$p['xp'];
	$from = (int)(FV_XP_TABLE[$lvl] ?? 0);
	$to = $lvl < FV_MAX_LEVEL ? (int)FV_XP_TABLE[$lvl + 1] : 0;
	$pct = $to > $from ? max(0, min(100, (int)floor(($xp - $from) * 100 / ($to - $from)))) : 100;
	$barText = $to > 0 ? 'до ' . ($lvl + 1) . '-го: ' . num(max(0, $to - $xp)) . ' опыта' : 'максимальный уровень';
	if ($lvl >= 11) { $city = '<b class="ok">прибыл</b> <span class="muted">(11 уровень выдаётся при первом прибытии)</span>'; }
	elseif ($lvl === 10) { $city = '<b class="warn">ещё нет</b> <span class="muted">— опыт на 10 уровне заморожен до прибытия</span>'; }
	else { $city = 'нет <span class="muted">(лодка чинится с 10 уровня)</span>'; }
	$u6 = str_starts_with((string)$p['ver'], '0.6') || (int)($p['xp_migrated'] ?? 0) === 1 || (int)($p['xp_earned_seen'] ?? -1) >= 0;
	$tile = static fn(string $icon, string $label, string $value): string => '<div class="ptile">' . ico($icon, 32) . '<div><span>' . h($label)
		. '</span><b>' . $value . '</b></div></div>';
	return '<section class="card progress"><h2>Прогресс ' . ($u6 ? '<span class="badge green">игра 0.6</span>' : '<span class="badge">до 0.6</span>') . '</h2>'
		. '<div class="plevel">' . ico('ui_level', 48) . '<div class="pl-main"><div class="pl-top"><b>Уровень ' . $lvl . '</b> <span class="muted">из '
		. FV_MAX_LEVEL . '</span><span class="pl-xp">' . num($xp) . ' опыта</span></div><svg class="bar" viewBox="0 0 100 4" preserveAspectRatio="none" role="img" aria-label="' . $pct
		. '%"><rect width="100" height="4" class="bar-bg"/><rect width="' . $pct . '" height="4" class="bar-fill"/></svg><div class="small muted">' . h($barText) . '</div>'
		. '<div class="lvls">' . implode('', array_map(static fn($l) => '<span class="' . ($l <= $lvl ? 'on' : '') . ($l > 10 ? ' u6' : '') . '">' . $l . '</span>',
			range(1, FV_MAX_LEVEL))) . '</div></div></div>'
		. '<div class="ptiles">'
		. $tile('ui_xp', 'Опыт заработанный', num($p['xp_earned'] ?? $p['xp']) . ' <span class="muted">(турнир «Опыт»)</span>')
		. $tile('ui_star_gold', 'Стаж', num($p['xp_seniority'] ?? 0) . ' <span class="muted">(опыт выше 10 000, срезанный обновлением 6'
			. ((int)($p['xp_migrated'] ?? 0) === 1 ? '; срез принят' : '') . ')</span>')
		. $tile('ui_boat', 'Прибытие в Город', $city)
		. $tile('ui_fish', 'Видов рыбы', (int)$p['species'] . ' из ' . count(FV_FISH))
		. '</div></section>';
}

function page_player(int $now): void
{
	$pid = fv_int($_GET['id'] ?? null, 1, PHP_INT_MAX) ?? 0;
	$p = fv_row('SELECT * FROM players WHERE id = ?', [$pid]);
	if (!$p) { flash('Игрок не найден.', 'err'); redirect(['p' => 'players']); }
	$pd = fv_profile_data($p['profile_json']);
	page_start($p['name'] . ' #' . (1000 + $pid), 'players');
	$post = static fn(string $do, string $inner, string $cls = '') => '<form method="post" class="' . $cls . '">' . csrf_field()
		. '<input type="hidden" name="do" value="' . h($do) . '"><input type="hidden" name="id" value="' . $pid . '">' . $inner . '</form>';

	echo '<div class="phead"><div><h1>' . h($p['name']) . ' <span class="tag big">#' . (1000 + $pid) . '</span></h1>'
		. '<p class="muted">Игрок № ' . $pid . ($p['status'] !== '' ? ' · «' . h($p['status']) . '»' : '') . ' · ' . h($p['platform']) . ' ' . h($p['ver']) . '</p></div>';
	if ((int)$p['banned']) { echo '<div class="badge red big">Заблокирован: ' . h($p['ban_reason']) . '</div>'; }
	echo '</div>';

	// ---- stats
	$ranks = fv_player_ranks($p);
	$rk = static fn(string $k) => $ranks[$k] > 0 ? ' <span class="muted">(' . $ranks[$k] . ' место)</span>' : '';
	$app = array_filter((array)$pd['appearance'], static fn($v) => $v !== '');
	$show = array_map('fish_show', (array)$pd['showcase']);
	echo progress_card($p);
	echo '<section class="card"><h2>Профиль и статистика</h2><dl class="grid">';
	foreach ([
		['Уровень', (int)$p['level'] . ' <span class="muted">(' . num($p['xp']) . ' опыта)</span>' . $rk('level')],
		['Всего рыбы', num($p['fish_total']) . $rk('fish')],
		['Видов', (int)$p['species'] . ' из ' . count(FV_FISH) . $rk('species')],
		['Легендарных', num($p['legendary']) . $rk('legend')],
		['Достижений', (int)$p['achievements'] . $rk('achievements')],
		['Идеальных поимок', num($p['perfect'])],
		['Заработано монет', num($p['coins_earned']) . $rk('earned')],
		['Построек', (int)$p['stations_built']],
		['Время в игре', h(dur((int)$p['playtime']))],
		['Самая крупная', fish_show(['fish' => $p['best_fish'], 'size' => $p['best_size'], 'q' => $p['best_q']]) . $rk('biggest')],
		['Лучший улов', fish_show($pd['best_catch'])],
		['Любимое место', h($pd['favorite_location'] !== '' ? fv_location_name((string)$pd['favorite_location']) : '—')],
		['Витрина', $show ? implode('<br>', $show) : '—'],
		['Внешний вид', $app ? implode('<br>', array_map(static fn($id) => thing((string)$id), $app)) : '—'],
		['Рыбачит с', h(fv_fmt_date((int)$pd['start_date'], false))],
		['Регистрация', h(fv_fmt_date((int)$p['created']))],
		['Был в сети', h(fv_fmt_date((int)$p['last_seen'])) . ' <span class="muted">(' . h(ago((int)$p['last_seen'], $now)) . ')</span>'],
		['Синхронизация', h(fv_fmt_date((int)$p['last_sync']))],
		['Подозрений', (int)$p['suspicious'] > 0 ? '<b class="warn">' . (int)$p['suspicious'] . '</b>' : '0'],
	] as [$k, $v]) {
		echo '<div><dt>' . h($k) . '</dt><dd>' . $v . '</dd></div>';
	}
	echo '</dl>';
	$s = fv_season_info($now);
	$sm = fv_season_my($s['id'], $pid, (int)$p['banned'] === 1)['my'];
	$part = static function (string $label, string $k, string $value) use ($sm): string {
		$rank = (int)$sm['rank_' . $k];
		return h($label) . ' ' . $value . ($rank > 0 ? ' <span class="muted">(' . $rank . ' из ' . (int)$sm['n_' . $k] . ')</span> '
			. tier_badge((int)$sm['tier_' . $k]) : '');
	};
	echo '<p class="small">' . h(fv_season_name($s['id'])) . ' (прогноз): ' . $part('улов', 'fish', num($sm['fish'])) . ' · '
		. $part('заработок', 'earned', num($sm['earned'])) . ' · ' . $part('опыт', 'xp', num($sm['xp'])) . ' · '
		. $part('крупнейшая', 'biggest', h(number_format((float)$sm['biggest'], 1, ',', '')) . ' см') . '</p></section>';

	// ---- actions
	echo '<div class="cols">';
	echo '<section class="card wide"><h2>Подарок (письмо с вложением)</h2>' . $post('gift',
		'<div class="row"><label>Монеты<input type="number" name="coins" min="0" max="10000000" value="0"></label>'
		. '<label>Опыт<input type="number" name="xp" min="0" max="1000000" value="0"></label>'
		. '<label>Поднять до уровня<input type="number" name="set_level" min="0" max="' . FV_MAX_LEVEL . '" value="0"></label></div>'
		. '<div class="field">Предмет или рыба' . item_picker() . '</div>'
		. '<div class="row"><label>Кол-во<input type="number" name="item_n" min="1" max="999" value="1"></label></div>'
		. '<label>Заголовок<input name="title" maxlength="100" placeholder="Подарок от администрации"></label>'
		. '<label>Текст<textarea name="body" rows="2" maxlength="2000" placeholder="Спасибо, что играете в «Клёвую долину»!"></textarea></label>'
		. '<button class="btn primary">Отправить подарок</button>') . '</section>';

	echo '<section class="card"><h2>Блокировка</h2>';
	if ((int)$p['banned']) {
		echo $post('unban', '<p>Причина: ' . h($p['ban_reason']) . '</p><button class="btn">Разблокировать</button>');
	} else {
		echo $post('ban', '<label>Причина (игрок увидит её)<input name="reason" maxlength="200" required placeholder="Например: читы на монеты"></label>'
			. '<button class="btn danger">Заблокировать</button>');
	}
	echo '</section>';

	echo '<section class="card"><h2>Сообщение</h2>' . $post('message',
		'<label>Заголовок<input name="title" maxlength="100" required></label>'
		. '<label>Текст<textarea name="body" rows="3" maxlength="2000" required></textarea></label><button class="btn primary">Отправить</button>') . '</section>';

	echo '<section class="card"><h2>Заметки и подозрения</h2>' . $post('notes',
		'<label>Заметки (сюда же сервер пишет подозрительные синхронизации)<textarea name="notes" rows="6">' . h($p['notes']) . '</textarea></label>'
		. '<button class="btn">Сохранить заметки</button>')
		. '<div class="btns">' . $post('reset_susp', '<button class="btn">Обнулить подозрения</button>', 'inline')
		. $post('accept_baseline', '<button class="btn" title="Если игрок законно откатил прогресс">Принять новые счётчики</button>', 'inline')
		. $post('reset_name', '<button class="btn danger">Сбросить имя на «Рыбак»</button>', 'inline') . '</div></section>';
	echo '</div>';

	// ---- saves
	$saves = fv_all('SELECT id, created, size, version FROM saves WHERE player_id = ? ORDER BY id DESC', [$pid]);
	echo '<section class="card"><h2>Сохранения <span class="muted">' . count($saves) . '</span></h2>';
	if ($saves) {
		echo '<div class="scroll"><table><tr><th>№</th><th>Дата</th><th class="r">Размер</th><th>Версия</th><th></th></tr>';
		foreach ($saves as $sv) {
			echo '<tr><td>' . (int)$sv['id'] . '</td><td>' . h(fv_fmt_date((int)$sv['created'])) . '</td><td class="r">'
				. h(number_format((int)$sv['size'] / 1024, 1, ',', ' ')) . ' КБ</td><td>' . (int)$sv['version'] . '</td><td class="acts">'
				. '<a class="btn small" href="' . h(url(['p' => 'save', 'id' => (int)$sv['id']])) . '">скачать .sav</a>'
				. $post('make_code', '<input type="hidden" name="save_id" value="' . (int)$sv['id'] . '"><button class="btn small primary">создать код восстановления</button>', 'inline')
				. '</td></tr>';
		}
		echo '</table></div>';
	} else {
		echo '<p class="muted">Игра ещё не присылала сохранений.</p>';
	}
	$codes = fv_all('SELECT * FROM restore_codes WHERE player_id = ? ORDER BY created DESC LIMIT 20', [$pid]);
	if ($codes) {
		echo '<h3>Коды восстановления</h3><div class="scroll"><table><tr><th>Код</th><th>Сохр.</th><th>Создан</th><th>Статус</th><th></th></tr>';
		foreach ($codes as $c) {
			$used = $c['used'] !== null;
			$active = !$used && (int)$c['expires'] > $now;
			$st = $used ? 'использован ' . fv_fmt_date((int)$c['used']) . ' (' . $c['used_ip'] . ')' : ($active ? 'действует до ' . fv_fmt_date((int)$c['expires']) : 'истёк');
			echo '<tr><td><b class="code">' . h($c['code']) . '</b></td><td>#' . (int)$c['save_id'] . '</td><td>' . h(fv_fmt_date((int)$c['created'])) . '</td><td>'
				. h($st) . '</td><td>' . ($active ? $post('revoke_code', '<input type="hidden" name="code" value="' . h($c['code']) . '"><button class="btn small">отозвать</button>', 'inline') : '')
				. '</td></tr>';
		}
		echo '</table></div>';
	}
	echo '</section>';

	// ---- mail
	$mail = fv_all('SELECT * FROM mail WHERE player_id = ? ORDER BY id DESC LIMIT 50', [$pid]);
	echo '<section class="card"><h2>Письма</h2>';
	if ($mail) {
		echo '<div class="scroll"><table><tr><th>Дата</th><th>Тип</th><th>Письмо</th><th>Вложения</th><th>Статус</th></tr>';
		$kinds = ['admin' => 'админ', 'season' => 'турнир', 'system' => 'система'];
		foreach ($mail as $m) {
			$att = [];
			if ((int)$m['coins'] > 0) { $att[] = '<span class="thing">' . ico('ui_coin', 16) . num($m['coins']) . ' мон.</span>'; }
			if ((int)$m['xp'] > 0) { $att[] = '<span class="thing">' . ico('ui_xp', 16) . num($m['xp']) . ' опыта</span>'; }
			if ((int)$m['set_level'] > 0) { $att[] = 'уровень ' . (int)$m['set_level']; }
			foreach ((array)json_decode((string)$m['items_json'], true) as $it) {
				if (is_array($it) && isset($it['id'])) { $att[] = thing((string)$it['id']) . ' ×' . (int)($it['n'] ?? 1); }
			}
			$claimed = $m['claimed'] !== null;
			echo '<tr><td>' . h(fv_fmt_date((int)$m['created'])) . '</td><td>' . h($kinds[$m['kind']] ?? $m['kind']) . '</td><td><b>' . h($m['title'])
				. '</b><div class="small pre">' . h(mb_substr((string)$m['body'], 0, 300)) . '</div></td><td class="atts">' . ($att ? implode('<br>', $att) : '—') . '</td><td>'
				. ($claimed ? 'получено ' . h(fv_fmt_date((int)$m['claimed'])) : 'не получено '
					. $post('revoke_mail', '<input type="hidden" name="mail_id" value="' . (int)$m['id'] . '"><button class="btn small">отозвать</button>', 'inline'))
				. '</td></tr>';
		}
		echo '</table></div>';
	} else {
		echo '<p class="muted">Писем не было.</p>';
	}
	echo '</section>';

	$log = fv_all('SELECT * FROM admin_log WHERE player_id = ? ORDER BY id DESC LIMIT 20', [$pid]);
	if ($log) {
		echo '<section class="card"><h2>Журнал действий</h2><div class="scroll"><table>';
		foreach ($log as $l) {
			echo '<tr><td>' . h(fv_fmt_date((int)$l['ts'])) . '</td><td>' . h(FV_LOG_NAMES[$l['action']] ?? $l['action']) . '</td><td class="small">' . h($l['details']) . '</td></tr>';
		}
		echo '</table></div></section>';
	}
	page_end();
}

function download_save(): void
{
	$id = fv_int($_GET['id'] ?? null, 1, PHP_INT_MAX) ?? 0;
	$s = fv_row('SELECT * FROM saves WHERE id = ?', [$id]);
	if (!$s) { flash('Сохранение не найдено.', 'err'); redirect(['p' => 'players']); }
	fv_admin_log('download_save', (int)$s['player_id'], 'сохранение #' . $id);
	$data = fv_blob($s['data']);
	$name = 'FishingValley_' . (1000 + (int)$s['player_id']) . '_' . date('Y-m-d_H-i', (int)$s['created']) . '.sav';
	header('Content-Type: application/octet-stream');
	header('Content-Disposition: attachment; filename="' . $name . '"');
	header('Content-Length: ' . strlen($data));
	echo $data;
	exit;
}

// ------------------------------------------------------------------ seasons

function tier_badge(int $t, string $extra = ''): string
{
	$t = max(0, min(3, $t));
	return '<span class="tier t' . $t . '">' . h(FV_TIER_NAMES[$t]) . ($extra !== '' ? ': ' . h($extra) : '') . '</span>';
}

/** The §4 rewards table for a level (s_earned shows the kinds count; the kinds come from the level's pool). */
function rewards_table(int $level, int $sid): string
{
	$cats = ['s_fish' => 'Улов → хорошие приманки', 's_earned' => 'Заработок → предметы', 's_xp' => 'Опыт → монеты',
		's_biggest' => 'Самая крупная → опыт'];
	$html = '<div class="scroll"><table><tr><th>Категория</th><th>' . tier_badge(1) . ' топ 25%</th><th>' . tier_badge(2) . ' топ 50%</th><th>'
		. tier_badge(3) . ' топ 75%</th></tr>';
	foreach ($cats as $cat => $label) {
		$html .= '<tr><td>' . h($label) . '</td>';
		for ($t = 1; $t <= 3; $t++) {
			if ($cat === 's_earned') {
				[$k, $n] = fv_earned_counts($t, $level);
				$txt = $k . ' ' . fv_plural($k, 'вид', 'вида', 'видов') . ' × ' . $n;
			} else {
				$txt = fv_reward_text(fv_season_cat_reward($cat, $t, $level, $sid, 0));
			}
			$html .= '<td>' . h($txt) . '</td>';
		}
		$html .= '</tr>';
	}
	$pool = array_map(static fn($id) => thing((string)$id, 20), fv_earned_pool($level));
	return $html . '</table></div><p class="small muted">Предметы за заработок на уровне ' . $level . ' выбираются из набора: ' . implode(', ', $pool)
		. ' (сети и снасти — не больше 2 шт.). Шаг уровня ' . $level . ' — ' . fv_level_span($level) . ' опыта.</p>';
}

function page_seasons(int $now): void
{
	page_start('Турниры', 'seasons');
	$cur = fv_season_info($now);
	$lvl = fv_int($_GET['lvl'] ?? 5, 1, FV_MAX_LEVEL) ?? 5;
	$opts = '';
	for ($l = 1; $l <= FV_MAX_LEVEL; $l++) { $opts .= '<option value="' . $l . '"' . ($l === $lvl ? ' selected' : '') . '>' . $l . '</option>'; }
	echo '<h1>Турниры</h1><section class="card"><h2>Сейчас: ' . h(fv_season_name($cur['id'])) . '</h2><p>'
		. h(fv_fmt_date($cur['starts'])) . ' — ' . h(fv_fmt_date($cur['ends'])) . ' · до конца <b>' . h(dur($cur['ends'] - $now))
		. '</b></p><p><a href="' . h(url(['p' => 'season', 'id' => $cur['id']])) . '">Текущие участники и прогноз наград →</a></p>'
		. '<p class="small muted">Турнир длится 3 реальных дня. Награды — отдельно в каждой из четырёх категорий: места считаются среди участников '
		. 'с результатом в категории (место / их число): топ 25% — золото, 50% — серебро, 75% — бронза; если в категории один участник — золото. '
		. 'Все награды игрока приходят одним письмом «Итоги турнира №…».</p>'
		. '<form class="toolbar" method="get"><input type="hidden" name="p" value="seasons"><label class="check">Награды для уровня '
		. '<select name="lvl">' . $opts . '</select></label><button class="btn small">Показать</button></form>'
		. rewards_table($lvl, $cur['id']) . '</section>';
	$rows = fv_all('SELECT id, starts, ends, closing, closed, results_json FROM seasons ORDER BY id DESC LIMIT 60');
	echo '<section class="card"><h2>История</h2>';
	if (!$rows) { echo '<p class="muted">Турниров ещё не было.</p>'; }
	else {
		echo '<div class="scroll"><table><tr><th>Турнир</th><th>Даты</th><th>Статус</th><th class="r">Участники</th><th class="r">Награждено</th>'
			. '<th class="r">Наград: зол. / сер. / бр.</th></tr>';
		foreach ($rows as $r) {
			$res = $r['results_json'] ? json_decode((string)$r['results_json'], true) : null;
			$st = $r['closed'] !== null ? 'итоги ' . fv_fmt_date((int)$r['closed']) : ((int)$r['ends'] > $now ? 'идёт' : 'закрывается…');
			$tiers = is_array($res) ? (int)($res['tiers'][1] ?? 0) . ' / ' . (int)($res['tiers'][2] ?? 0) . ' / ' . (int)($res['tiers'][3] ?? 0) : '—';
			$n = is_array($res) ? (int)$res['participants'] : (int)fv_val('SELECT COUNT(*) FROM season_stats WHERE season_id = ? AND fish > 0', [(int)$r['id']]);
			$rewarded = is_array($res) ? (isset($res['rewarded']) ? (string)(int)$res['rewarded']
				: (string)count(array_filter((array)$res['players'], static fn($p) => (int)($p['tier'] ?? 0) > 0))) : '—';
			echo '<tr><td><a href="' . h(url(['p' => 'season', 'id' => (int)$r['id']])) . '">' . h(fv_season_name((int)$r['id'])) . '</a></td><td>'
				. h(date('d.m', (int)$r['starts'])) . ' – ' . h(date('d.m.Y', (int)$r['ends'] - 1)) . '</td><td>' . h($st) . '</td><td class="r">' . $n
				. '</td><td class="r">' . $rewarded . '</td><td class="r">' . $tiers . '</td></tr>';
		}
		echo '</table></div>';
	}
	echo '</section>';
	page_end();
}

function page_season(int $now): void
{
	$sid = fv_int($_GET['id'] ?? null, -100000, 100000) ?? fv_season_id($now);
	[$starts, $ends] = fv_season_bounds($sid);
	$row = fv_row('SELECT * FROM seasons WHERE id = ?', [$sid]);
	$res = $row && $row['results_json'] ? json_decode((string)$row['results_json'], true) : null;
	$live = !is_array($res);
	$st = $live ? fv_season_standings($sid) : $res;
	page_start(fv_season_name($sid), 'seasons');
	echo '<h1>' . h(fv_season_name($sid)) . '</h1><p class="muted">' . h(fv_fmt_date($starts)) . ' — ' . h(fv_fmt_date($ends)) . ' · '
		. ($live ? ($ends > $now ? 'идёт, до конца ' . h(dur($ends - $now)) . ' — показан прогноз' : 'ожидает подведения итогов')
			: 'итоги подведены ' . h(fv_fmt_date((int)$res['closed']))) . ' · участников: ' . (int)$st['participants'] . '</p>';
	$players = (array)($st['players'] ?? []);
	if (!$players) {
		echo '<div class="card"><p class="muted">Участников нет.</p></div>';
		page_end();
	}
	$counts = (array)$st['counts'];
	$cell = static function (array $p, string $cat, string $value) use ($counts): string {
		$rank = (int)($p['ranks'][$cat] ?? 0);
		if ($rank <= 0) { return '<td class="r">' . $value . '<div class="small muted">—</div></td>'; }
		$t = isset($p['tiers']) ? (int)($p['tiers'][$cat] ?? 0) : -1;   // -1: a result of the old (update 4) format
		return '<td class="r">' . $value . '<div class="small">' . $rank . ' из ' . (int)($counts[$cat] ?? 0)
			. ($t >= 0 ? ' ' . tier_badge($t) : '') . '</div></td>';
	};
	echo '<div class="card scroll"><table><tr><th>Игрок</th><th>Ур.</th><th class="r">Улов</th><th class="r">Заработок</th><th class="r">Опыт</th>'
		. '<th class="r">Крупнейшая</th><th>Награда' . ($live ? ' (прогноз)' : '') . '</th></tr>';
	foreach ($players as $p) {
		if (isset($p['tiers'])) {
			$total = $live ? fv_season_letter($sid, $p, $counts, (int)$st['participants'])[2] : (array)($p['reward'] ?? []);
			$rwText = (int)($p['awards'] ?? 0) > 0 ? '<span class="small">' . h(fv_reward_text($total)) . '</span>' : tier_badge(0);
		} else {   // update-4 result: one overall tier
			$rwText = tier_badge((int)($p['tier'] ?? 0), (int)($p['tier'] ?? 0) > 0 ? fv_reward_text((array)($p['reward'] ?? [])) : '');
		}
		echo '<tr><td>' . player_link($p) . '</td><td>' . (int)$p['level'] . '</td>'
			. $cell($p, 's_fish', num($p['fish'])) . $cell($p, 's_earned', num($p['earned'])) . $cell($p, 's_xp', num($p['xp']))
			. $cell($p, 's_biggest', fish_show(['fish' => $p['best_fish'], 'size' => $p['best_size'], 'q' => $p['best_q']]))
			. '<td>' . $rwText . '</td></tr>';
	}
	echo '</table></div>';
	page_end();
}

// ------------------------------------------------------------------ mail (broadcast) and log

function page_mail(): void
{
	page_start('Рассылка', 'mail');
	$active = (int)fv_val('SELECT COUNT(*) FROM players WHERE banned = 0');
	echo '<h1>Рассылка всем игрокам</h1><section class="card narrow"><form method="post">' . csrf_field() . '<input type="hidden" name="do" value="broadcast">'
		. '<label>Заголовок<input name="title" maxlength="100" required></label>'
		. '<label>Текст<textarea name="body" rows="4" maxlength="2000" required></textarea></label>'
		. '<div class="row"><label>Монеты<input type="number" name="coins" min="0" max="1000000" value="0"></label>'
		. '<label>Опыт<input type="number" name="xp" min="0" max="100000" value="0"></label>'
		. '<label>Только кто был в сети за N дней (0 = все)<input type="number" name="days" min="0" max="3650" value="0"></label></div>'
		. '<div class="field">Предмет или рыба' . item_picker() . '</div>'
		. '<div class="row"><label>Кол-во<input type="number" name="item_n" min="1" max="999" value="1"></label></div>'
		. '<label class="check"><input type="checkbox" name="confirm" value="1"> Отправить всем незаблокированным (' . num($active) . ')</label>'
		. '<button class="btn primary">Разослать</button></form></section>';
	page_end();
}

function page_version(int $now): void
{
	page_start('Версия игры', 'version');
	$ready = fv_settings_ready();
	$v = fv_version_info();
	$updated = (int)fv_setting('version_updated');
	$apk = fv_apk_local($v['download_url']);
	echo '<h1>Версия игры</h1>';
	if (!$ready) {
		echo '<div class="flash err">Таблицы настроек ещё нет: откройте <code>install.php?key=…</code> ещё раз (данные игроков сохранятся). '
			. 'Пока игра получает значения по умолчанию.</div>';
	}
	if ($apk === null) { $apkText = 'внешняя ссылка'; }
	elseif ($apk['exists']) { $apkText = h($apk['name']) . ' · ' . h(fv_mb_text($apk['size'])); }
	else { $apkText = '<b class="warn">не загружен</b>'; }
	echo '<div class="stats">';
	foreach ([
		['Последняя версия', h($v['latest_name']) . ' <small class="muted">сборка ' . (int)$v['latest_build'] . '</small>'],
		['Играть можно со сборки', (string)(int)$v['min_build']],
		['Файл APK', $apkText],
		['Изменено', $updated > 0 ? h(fv_fmt_date($updated)) : 'по умолчанию'],
	] as [$label, $val]) {
		echo '<div class="stat"><b>' . $val . '</b><span>' . h($label) . '</span></div>';
	}
	echo '</div>';
	if ($apk !== null && !$apk['exists']) {
		echo '<div class="flash err">Файла <b>' . h($apk['name']) . '</b> нет в папке <code>Fishing-game/download/</code> — загрузите его через '
			. 'файловый менеджер хостинга, иначе кнопка «Скачать» не сработает.</div>';
	}

	echo '<div class="cols"><section class="card"><h2>Настройки</h2><form method="post">' . csrf_field() . '<input type="hidden" name="do" value="version">'
		. '<div class="row"><label>Название версии<input name="latest_name" maxlength="16" required value="' . h($v['latest_name']) . '" placeholder="0.5"></label>'
		. '<label>Номер сборки<input type="number" name="latest_build" min="1" max="' . FV_BUILD_MAX . '" required value="' . (int)$v['latest_build'] . '"></label>'
		. '<label>Минимальная сборка<input type="number" name="min_build" min="0" max="' . FV_BUILD_MAX . '" required value="' . (int)$v['min_build'] . '"></label></div>'
		. '<label>Ссылка на APK<input type="url" name="download_url" maxlength="300" required value="' . h($v['download_url']) . '"></label>'
		. '<label>Что нового (каждая строка — отдельный пункт)<textarea name="changelog" rows="7" maxlength="2000">' . h($v['changelog']) . '</textarea></label>'
		. '<button class="btn primary">Сохранить</button></form>'
		. '<p class="small muted">Страница скачивания: <a href="../" target="_blank" rel="noopener">открыть</a>'
		. ($v['download_url'] !== '' ? ' · <a href="' . h($v['download_url']) . '" rel="noopener">ссылка на APK</a>' : '') . '</p></section>';

	echo '<section class="card"><h2>Что увидят игроки</h2><ul class="small">'
		. '<li><b>Сборка игры ниже минимальной</b> — «Нужно обновить игру»: играть нельзя, пока игрок не скачает новую версию.</li>'
		. '<li><b>Ниже последней</b> — «Доступно обновление» с кнопками «Обновить» и «Позже».</li>'
		. '<li><b>Нет связи с сервером</b> — «Повторить» или «Играть офлайн» (без рейтинга, почты и резервных копий).</li>'
		. '<li>Текст «Что нового» показывается в игре и на странице скачивания.</li></ul>'
		. '<h3>Как выпустить обновление</h3><ol class="small">'
		. '<li>Соберите APK с новым номером сборки (в игре — <code>ClientBuild</code>, в Android — <code>StoreVersion</code>) и названием версии.</li>'
		. '<li>Через файловый менеджер хостинга загрузите его в <code>Fishing-game/download/</code> с тем же именем, '
		. 'что в ссылке (по умолчанию <code>KlevayaDolina.apk</code>), заменив старый файл.</li>'
		. '<li>Здесь укажите название версии, номер сборки и что нового — нажмите «Сохранить».</li>'
		. '<li>Если старые версии больше не должны работать (например, изменился протокол), поднимите минимальную сборку.</li></ol></section></div>';
	page_end();
}

function page_log(int $now): void
{
	page_start('Журнал', 'log');
	$rows = fv_all('SELECT l.*, p.name FROM admin_log l LEFT JOIN players p ON p.id = l.player_id ORDER BY l.id DESC LIMIT 200');
	echo '<h1>Журнал действий</h1><div class="card scroll"><table><tr><th>Время</th><th>Действие</th><th>Игрок</th><th>Подробности</th><th>IP</th></tr>';
	foreach ($rows as $l) {
		echo '<tr><td>' . h(fv_fmt_date((int)$l['ts'])) . '</td><td>' . h(FV_LOG_NAMES[$l['action']] ?? $l['action']) . '</td><td>'
			. ($l['player_id'] !== null ? player_link(['id' => (int)$l['player_id'], 'name' => $l['name'] ?? '?']) : '—') . '</td><td class="small">'
			. h($l['details']) . '</td><td class="small">' . h($l['ip']) . '</td></tr>';
	}
	echo '</table></div>';
	page_end();
}
