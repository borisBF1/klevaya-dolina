<?php
// Fishing Valley — game API (Docs/UPDATE4_ONLINE.md §3, Docs/SERVER.md).
// POST api.php?a=<action> with a JSON body. Signed actions: "pid", "ts", "nonce" in the body and the header
// X-FV-Sig: hex HMAC-SHA1(secret, raw body). Always HTTP 200 + {"ok":true,...} or {"ok":false,"error","message"}.
declare(strict_types=1);

define('FV_ROOT', __DIR__);
require __DIR__ . '/lib/db.php';
require __DIR__ . '/lib/auth.php';
require __DIR__ . '/lib/board.php';
require __DIR__ . '/lib/settings.php';
require __DIR__ . '/lib/schema.php';

fv_json_headers();

/** action => [signed, handler(array $req, int $now, ?array $player): array] */
const FV_ACTIONS = [
	'ping' => [false, 'api_ping'],
	'register' => [false, 'api_register'],
	'sync' => [true, 'api_sync'],
	'save_upload' => [true, 'api_save_upload'],
	'set_name' => [true, 'api_set_name'],
	'set_status' => [true, 'api_set_status'],
	'board' => [false, 'api_board'],
	'profile' => [false, 'api_profile'],
	'mail' => [true, 'api_mail'],
	'mail_claim' => [true, 'api_mail_claim'],
	'restore' => [false, 'api_restore'],
];

$now = time();
try {
	fv_config();
	$now = fv_now();
	$action = $_GET['a'] ?? '';
	$method = strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET'));
	if (!is_string($action) || !isset(FV_ACTIONS[$action])) {
		throw new FVError('bad_action', 'Неизвестное действие.');
	}
	if ($method !== 'POST' && !($method === 'GET' && $action === 'ping')) {
		throw new FVError('bad_request', 'Нужен POST-запрос.');
	}
	$raw = '';
	if ($method === 'POST') {
		$raw = (string)file_get_contents('php://input', false, null, 0, FV_MAX_BODY + 1);
		if (strlen($raw) > FV_MAX_BODY) { throw new FVError('too_big', 'Слишком большой запрос.'); }
	}
	$req = trim($raw) === '' ? [] : json_decode($raw, true, 32);
	if (!is_array($req)) { throw new FVError('bad_request', 'Неверный запрос (JSON).'); }

	fv_db();
	try { fv_schema_upgrade(); } catch (Throwable $e) { fv_log_error('schema upgrade: ' . $e->getMessage()); }
	fv_rate_prune();
	fv_season_close_due($now);

	[$signed, $handler] = FV_ACTIONS[$action];
	$player = null;
	if ($signed) {
		$player = fv_verify_signed($raw, $req, $now);
		fv_q('UPDATE players SET last_seen = ? WHERE id = ?', [$now, (int)$player['id']]);
	} elseif (in_array($action, ['ping', 'board', 'profile'], true)) {
		if (!fv_rate_hit('u:' . fv_client_ip(), (array)fv_config()['rate_unsigned'])) {
			throw new FVError('rate_limited', 'Слишком много запросов. Попробуйте позже.');
		}
	}
	$out = $handler($req, $now, $player);
	echo fv_json(['ok' => true, 'server_time' => $now] + $out);
} catch (FVError $e) {
	echo fv_json(['ok' => false, 'error' => $e->codeName, 'message' => $e->getMessage(), 'server_time' => $now] + $e->extra);
} catch (Throwable $e) {
	$missing = $e instanceof RuntimeException && str_starts_with($e->getMessage(), 'config_');
	if (!$missing) { fv_log_error('api ' . ($_GET['a'] ?? '?') . ': ' . get_class($e) . ': ' . $e->getMessage() . ' @ ' . $e->getFile() . ':' . $e->getLine()); }
	if (isset($e) && function_exists('fv_db')) {
		try { if (fv_db()->inTransaction()) { fv_db()->rollBack(); } } catch (Throwable $ignored) { }
	}
	echo fv_json(['ok' => false, 'error' => 'server_error',
		'message' => $missing ? 'Сервер ещё не настроен.' : 'Ошибка сервера. Попробуйте позже.', 'server_time' => $now]);
}

// ================================================================== actions

function api_ping(array $req, int $now, ?array $p): array
{
	// version = the game's start-up gate (Docs/UPDATE4_ONLINE.md §10): latest / minimal build, download URL, changelog
	return ['season' => fv_season_info($now), 'version' => fv_version_info(), 'server_version' => FV_SERVER_VERSION];
}

function api_register(array $req, int $now, ?array $p): array
{
	$secret = $req['secret'] ?? '';
	if (!is_string($secret) || preg_match('/^[0-9a-fA-F]{64}$/', $secret) !== 1) {
		throw new FVError('bad_request', 'Неверный ключ игрока.');
	}
	// idempotent: a retry after a lost answer gets the same player back
	$existing = fv_row('SELECT id, name FROM players WHERE secret = ?', [$secret]);
	if ($existing) {
		return ['pid' => (int)$existing['id'], 'tag' => 1000 + (int)$existing['id'], 'name' => (string)$existing['name']];
	}
	$name = fv_normalize_name($req['name'] ?? null);
	if ($name === null) { throw new FVError('bad_name', fv_bad_name_message()); }
	if (!fv_rate_hit('r:' . fv_client_ip(), (array)fv_config()['rate_register'])) {
		throw new FVError('rate_limited', 'Слишком много регистраций с этого адреса. Попробуйте через час.');
	}
	$platform = $req['platform'] ?? '';
	$platform = in_array($platform, ['win', 'android'], true) ? $platform : 'other';
	$ver = is_string($req['ver'] ?? null) ? substr(preg_replace('/[^0-9A-Za-z._\- ]/', '', $req['ver']) ?? '', 0, 24) : '';
	try {
		fv_q('INSERT INTO players (secret, name, name_lc, status, platform, ver, created, last_seen, updated_at, banned, ban_reason,
			suspicious, notes, level, xp, fish_total, legendary, species, coins_earned, achievements, perfect, playtime, stations_built,
			best_size, best_fish, best_q, profile_json, last_sync, reset_baseline, grace_xp)
			VALUES (?, ?, ?, \'\', ?, ?, ?, ?, ?, 0, \'\', 0, NULL, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \'\', 0, NULL, 0, 0, 0)',
			[$secret, $name, mb_strtolower($name), $platform, $ver, $now, $now, $now]);
		$pid = (int)fv_db()->lastInsertId();
	} catch (PDOException $e) {
		$again = fv_row('SELECT id, name FROM players WHERE secret = ?', [$secret]);   // a parallel retry won
		if (!$again) { throw $e; }
		return ['pid' => (int)$again['id'], 'tag' => 1000 + (int)$again['id'], 'name' => (string)$again['name']];
	}
	return ['pid' => $pid, 'tag' => 1000 + $pid, 'name' => $name];
}

function fv_bad_name_message(): string
{
	return 'Имя: от 1 до 16 символов — буквы, цифры, пробел, «_» и «-», без грубых слов.';
}

function api_sync(array $req, int $now, ?array $p): array
{
	$pid = (int)$p['id'];
	$viol = [];
	$stats = is_array($req['stats'] ?? null) ? $req['stats'] : [];
	$upd = fv_check_stats($p, $stats, $now, $viol);

	$prof = is_array($req['profile'] ?? null) ? $req['profile'] : [];
	$chk = fv_check_profile(fv_profile_data($p['profile_json']), $prof, $viol);
	$status = (string)$p['status'];
	if (array_key_exists('status', $prof)) {
		$s = fv_normalize_status($prof['status']);
		if ($s !== null) { $status = $s; }
	}
	$sb = fv_fish_show($req['season_best'] ?? null, $viol, 'рекорд турнира');

	// the all-time biggest fish = the biggest valid size ever reported
	$best = ['fish' => (string)$p['best_fish'], 'size' => round((float)$p['best_size'], 1), 'q' => (int)$p['best_q']];
	foreach (array_merge($chk['shows'], $sb ? [$sb] : []) as $f) {
		if ($f['size'] > $best['size']) { $best = $f; }
	}
	$changed = $best['size'] > round((float)$p['best_size'], 1);
	foreach (['xp', 'fish_total', 'legendary', 'species', 'coins_earned', 'achievements'] as $c) {
		if (isset($upd[$c]) && (int)$upd[$c] !== (int)$p[$c]) { $changed = true; }
	}

	$set = $upd;
	$u6 = fv_schema_u6_ready();
	if (!$u6) { unset($set['xp_earned'], $set['xp_earned_seen'], $set['xp_seniority'], $set['xp_migrated']); }
	$set['best_size'] = $best['size'];
	$set['best_fish'] = $best['fish'];
	$set['best_q'] = $best['q'];
	$set['status'] = $status;
	$set['profile_json'] = json_encode($chk['json'], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
	$set['last_sync'] = $now;
	$set['reset_baseline'] = 0;
	if (isset($upd['xp'])) { $set['grace_xp'] = 0; }
	if ($changed) { $set['updated_at'] = $now; }
	if (isset($req['ver']) && is_string($req['ver'])) { $set['ver'] = substr(preg_replace('/[^0-9A-Za-z._\- ]/', '', $req['ver']) ?? '', 0, 24); }
	if (isset($req['platform']) && in_array($req['platform'], ['win', 'android'], true)) { $set['platform'] = $req['platform']; }
	if ($viol) {
		$set['suspicious'] = (int)$p['suspicious'] + 1;
		$note = '[' . date('d.m.Y H:i', $now) . '] ' . implode('; ', $viol);
		$notes = trim((string)$p['notes'] . "\n" . $note);
		if (mb_strlen($notes) > 6000) { $notes = '…' . mb_substr($notes, -5900); }
		$set['notes'] = $notes;
	}

	$db = fv_db();
	$db->beginTransaction();
	$cols = [];
	$vals = [];
	foreach ($set as $k => $v) { $cols[] = $k . ' = ?'; $vals[] = $v; }
	$vals[] = $pid;
	fv_q('UPDATE players SET ' . implode(', ', $cols) . ' WHERE id = ?', $vals);

	// ---- tournament
	$sid = fv_season_id($now);
	[$ss, $se] = fv_season_bounds($sid);
	fv_season_ensure($sid);
	// update 6 (§9): the category «Опыт» counts «опыт заработанный» (players.xp_earned): it keeps growing at the frozen
	// level 10 and does not drop with the one-off XP trim. Before the schema upgrade — the total XP, as in 0.5.
	$xpPre = $u6 ? (int)$p['xp_earned'] : (int)$p['xp'];
	$xpNow = $u6 ? (int)($upd['xp_earned'] ?? $xpPre) : (int)($upd['xp'] ?? $xpPre);
	$cur = ['fish' => $upd['fish_total'] ?? (int)$p['fish_total'], 'earned' => $upd['coins_earned'] ?? (int)$p['coins_earned'],
		'xp' => $xpNow];
	$reset = (int)$p['reset_baseline'] === 1;
	$row = fv_row('SELECT * FROM season_stats WHERE season_id = ? AND player_id = ?', [$sid, $pid]);
	if (!$row) {
		// baseline: the last known values if the previous sync was right before the start, else the current ones
		$usePre = !$reset && (int)$p['last_sync'] > 0 && (int)$p['last_sync'] >= $ss - 900;
		$base = $usePre ? ['fish' => (int)$p['fish_total'], 'earned' => (int)$p['coins_earned'], 'xp' => $xpPre] : $cur;
		fv_q(fv_insert_ignore_sql() . ' INTO season_stats (season_id, player_id, base_fish, base_earned, base_xp, fish, earned, xp,
			best_size, best_fish, best_q, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', [
			$sid, $pid, $base['fish'], $base['earned'], $base['xp'],
			max(0, $cur['fish'] - $base['fish']), max(0, $cur['earned'] - $base['earned']), max(0, $cur['xp'] - $base['xp']),
			$sb ? $sb['size'] : 0.0, $sb ? $sb['fish'] : '', $sb ? $sb['q'] : 0, $now]);
	} else {
		$base = ['fish' => (int)$row['base_fish'], 'earned' => (int)$row['base_earned'], 'xp' => (int)$row['base_xp']];
		if ($reset) {   // after a restore: keep the season progress, move the baseline under the restored counters
			$base = ['fish' => $cur['fish'] - (int)$row['fish'], 'earned' => $cur['earned'] - (int)$row['earned'], 'xp' => $cur['xp'] - (int)$row['xp']];
		}
		$vals = ['fish' => max(0, $cur['fish'] - $base['fish']), 'earned' => max(0, $cur['earned'] - $base['earned']),
			'xp' => max(0, $cur['xp'] - $base['xp'])];
		$bs = ['size' => round((float)$row['best_size'], 1), 'fish' => (string)$row['best_fish'], 'q' => (int)$row['best_q']];
		$sChanged = $vals['fish'] !== (int)$row['fish'] || $vals['earned'] !== (int)$row['earned'] || $vals['xp'] !== (int)$row['xp'];
		if ($sb && $sb['size'] > $bs['size']) { $bs = $sb; $sChanged = true; }
		fv_q('UPDATE season_stats SET base_fish = ?, base_earned = ?, base_xp = ?, fish = ?, earned = ?, xp = ?, best_size = ?,
			best_fish = ?, best_q = ?, updated_at = ? WHERE season_id = ? AND player_id = ?', [
			$base['fish'], $base['earned'], $base['xp'], $vals['fish'], $vals['earned'], $vals['xp'], $bs['size'], $bs['fish'],
			$bs['q'], $sChanged ? $now : (int)$row['updated_at'], $sid, $pid]);
	}
	$db->commit();

	$my = fv_season_my($sid, $pid, false);
	return [
		'name' => (string)$p['name'], 'tag' => 1000 + $pid, 'status' => $status, 'banned' => false,
		'mail_unread' => (int)fv_val('SELECT COUNT(*) FROM mail WHERE player_id = ? AND claimed IS NULL', [$pid]),
		'season' => ['id' => $sid, 'starts' => $ss, 'ends' => $se, 'participants' => $my['participants'], 'my' => $my['my']],
		'rejected' => count($viol),
	];
}

function api_save_upload(array $req, int $now, ?array $p): array
{
	$pid = (int)$p['id'];
	$b64 = $req['save_b64'] ?? '';
	if (!is_string($b64) || $b64 === '') { throw new FVError('bad_request', 'Нет данных сохранения.'); }
	if (strlen($b64) > (int)ceil(FV_MAX_SAVE / 3) * 4 + 8) { throw new FVError('too_big', 'Сохранение больше 700 КБ.'); }
	$data = base64_decode($b64, true);
	if ($data === false || $data === '') { throw new FVError('bad_request', 'Повреждённые данные сохранения.'); }
	if (strlen($data) > FV_MAX_SAVE) { throw new FVError('too_big', 'Сохранение больше 700 КБ.'); }
	$version = fv_int($req['save_version'] ?? 0, 0, 1000000000) ?? 0;
	$hash = sha1($data);
	$last = fv_row('SELECT id, hash FROM saves WHERE player_id = ? ORDER BY id DESC LIMIT 1', [$pid]);
	if ($last && hash_equals((string)$last['hash'], $hash)) {
		return ['stored' => true, 'save_id' => (int)$last['id'], 'duplicate' => true];
	}
	$st = fv_db()->prepare('INSERT INTO saves (player_id, created, size, version, hash, data) VALUES (?, ?, ?, ?, ?, ?)');
	$st->bindValue(1, $pid, PDO::PARAM_INT);
	$st->bindValue(2, $now, PDO::PARAM_INT);
	$st->bindValue(3, strlen($data), PDO::PARAM_INT);
	$st->bindValue(4, $version, PDO::PARAM_INT);
	$st->bindValue(5, $hash, PDO::PARAM_STR);
	$st->bindValue(6, $data, PDO::PARAM_LOB);
	$st->execute();
	$saveId = (int)fv_db()->lastInsertId();
	// keep the newest 12 (plus any save an active restore code points to)
	$cut = fv_val('SELECT id FROM saves WHERE player_id = ? ORDER BY id DESC LIMIT 1 OFFSET ' . (FV_SAVES_KEEP - 1), [$pid]);
	if ($cut !== null) {
		fv_q('DELETE FROM saves WHERE player_id = ? AND id < ? AND id NOT IN
			(SELECT save_id FROM restore_codes WHERE player_id = ? AND used IS NULL AND expires > ?)', [$pid, (int)$cut, $pid, $now]);
	}
	return ['stored' => true, 'save_id' => $saveId];
}

function api_set_name(array $req, int $now, ?array $p): array
{
	$name = fv_normalize_name($req['name'] ?? null);
	if ($name === null) { throw new FVError('bad_name', fv_bad_name_message()); }
	fv_q('UPDATE players SET name = ?, name_lc = ? WHERE id = ?', [$name, mb_strtolower($name), (int)$p['id']]);
	return ['name' => $name, 'tag' => 1000 + (int)$p['id']];
}

function api_set_status(array $req, int $now, ?array $p): array
{
	$status = fv_normalize_status($req['status'] ?? '');
	if ($status === null) { throw new FVError('bad_status', 'Статус: до 40 символов, без грубых слов.'); }
	fv_q('UPDATE players SET status = ? WHERE id = ?', [$status, (int)$p['id']]);
	return ['status' => $status];
}

function api_board(array $req, int $now, ?array $p): array
{
	$cat = fv_board_cat($req['cat'] ?? 'level', $req['period'] ?? 'all');
	if ($cat === null) { throw new FVError('bad_request', 'Неизвестная категория рейтинга.'); }
	$limit = min(50, fv_int($req['limit'] ?? 20, 1, 100000) ?? 20);
	$offset = fv_int($req['offset'] ?? 0, 0, 10000000) ?? 0;
	$pid = fv_int($req['pid'] ?? 0, 0, PHP_INT_MAX) ?? 0;
	$q = is_string($req['q'] ?? null) ? mb_substr($req['q'], 0, 64) : '';
	$b = fv_board($cat, $q, $offset, $limit, $pid, $now);
	$season = fv_season_info($now);
	return ['cat' => $cat, 'period' => FV_BOARD[$cat]['season'] ? 'season' : 'all', 'q' => $q, 'offset' => $offset,
		'total' => $b['total'], 'entries' => $b['entries'], 'me' => $b['me'], 'season' => $season];
}

function api_profile(array $req, int $now, ?array $p): array
{
	$id = fv_int($req['id'] ?? ($req['pid'] ?? null), 1, PHP_INT_MAX);
	if ($id === null && isset($req['tag'])) {
		$tag = fv_int(ltrim((string)$req['tag'], '#'), 1001, PHP_INT_MAX);
		$id = $tag !== null ? $tag - 1000 : null;
	}
	$row = $id !== null ? fv_row('SELECT * FROM players WHERE id = ?', [$id]) : null;
	if (!$row) { throw new FVError('not_found', 'Игрок не найден.'); }
	$pd = fv_profile_data($row['profile_json']);
	$bc = is_array($pd['best_catch']) ? $pd['best_catch'] : ['fish' => '', 'size' => 0, 'q' => 0];
	$pid = (int)$row['id'];
	return ['profile' => [
		'pid' => $pid, 'name' => (string)$row['name'], 'tag' => 1000 + $pid, 'status' => (string)$row['status'],
		'level' => (int)$row['level'], 'xp' => (int)$row['xp'],
		'xp_earned' => (int)($row['xp_earned'] ?? $row['xp']), 'seniority' => (int)($row['xp_seniority'] ?? 0),   // update 6 (§9)
		'fish_total' => (int)$row['fish_total'],
		'species' => (int)$row['species'], 'legendary' => (int)$row['legendary'], 'achievements' => (int)$row['achievements'],
		'coins_earned' => (int)$row['coins_earned'], 'perfect' => (int)$row['perfect'], 'playtime' => (int)$row['playtime'],
		'stations_built' => (int)$row['stations_built'], 'favorite_location' => (string)$pd['favorite_location'],
		'best_catch' => ['fish' => (string)$bc['fish'], 'size' => (float)$bc['size'], 'q' => (int)$bc['q']],
		'biggest' => ['fish' => (string)$row['best_fish'], 'size' => round((float)$row['best_size'], 1), 'q' => (int)$row['best_q']],
		'showcase' => array_values($pd['showcase']),
		'appearance' => array_merge(['hat' => '', 'outfit' => '', 'bobber' => '', 'pet' => ''], (array)$pd['appearance']),
		'start_date' => (int)$pd['start_date'], 'created' => (int)$row['created'], 'last_seen' => (int)$row['last_seen'],
		'banned' => (int)$row['banned'] === 1,
		'ranks' => fv_player_ranks($row),
	]];
}

function fv_mail_out(array $m): array
{
	$items = [];
	foreach ((array)json_decode((string)$m['items_json'], true) as $it) {
		if (is_array($it) && fv_is_game_id($it['id'] ?? null)) { $items[] = ['id' => $it['id'], 'n' => max(1, (int)($it['n'] ?? 1))]; }
	}
	return [
		'id' => (int)$m['id'], 'kind' => (string)$m['kind'], 'title' => (string)$m['title'], 'body' => (string)$m['body'],
		'coins' => (int)$m['coins'], 'xp' => (int)$m['xp'], 'set_level' => (int)$m['set_level'], 'items' => $items,
		'created' => (int)$m['created'], 'claimed' => (int)($m['claimed'] ?? 0),
	];
}

function api_mail(array $req, int $now, ?array $p): array
{
	$pid = (int)$p['id'];
	$rows = fv_all('SELECT * FROM mail WHERE player_id = ? ORDER BY id DESC LIMIT 50', [$pid]);
	return ['mail' => array_map('fv_mail_out', $rows),
		'mail_unread' => (int)fv_val('SELECT COUNT(*) FROM mail WHERE player_id = ? AND claimed IS NULL', [$pid])];
}

function api_mail_claim(array $req, int $now, ?array $p): array
{
	$pid = (int)$p['id'];
	$id = fv_int($req['id'] ?? null, 1, PHP_INT_MAX);
	if ($id === null) { throw new FVError('bad_request', 'Не указано письмо.'); }
	$m = fv_row('SELECT * FROM mail WHERE id = ? AND player_id = ?', [$id, $pid]);
	if (!$m) { throw new FVError('not_found', 'Письмо не найдено.'); }
	$n = fv_q('UPDATE mail SET claimed = ? WHERE id = ? AND player_id = ? AND claimed IS NULL', [$now, $id, $pid])->rowCount();
	if ($n !== 1) { throw new FVError('already_claimed', 'Это письмо уже получено.'); }
	// the XP the client adds now is expected by the next sync's growth check
	$grace = (int)$m['xp'] + ((int)$m['set_level'] > 0 ? fv_xp_for_level((int)$m['set_level']) : 0);
	if ($grace > 0) { fv_q('UPDATE players SET grace_xp = grace_xp + ? WHERE id = ?', [$grace, $pid]); }
	$m['claimed'] = $now;
	return ['claimed' => true, 'mail' => fv_mail_out($m)];
}

function api_restore(array $req, int $now, ?array $p): array
{
	if (!fv_rate_hit('x:' . fv_client_ip(), (array)fv_config()['rate_restore'])) {
		throw new FVError('rate_limited', 'Слишком много попыток. Попробуйте через час.');
	}
	$code = strtoupper(preg_replace('/[^0-9A-Za-z]/', '', is_string($req['code'] ?? null) ? $req['code'] : '') ?? '');
	$bad = 'Неверный или устаревший код восстановления.';
	if (preg_match('/^[A-Z0-9]{8}$/', $code) !== 1) { throw new FVError('bad_code', $bad); }
	$db = fv_db();
	$db->beginTransaction();
	$rc = fv_row('SELECT * FROM restore_codes WHERE code = ?', [$code]);
	if (!$rc || $rc['used'] !== null || (int)$rc['expires'] <= $now) { $db->rollBack(); throw new FVError('bad_code', $bad); }
	$save = fv_row('SELECT * FROM saves WHERE id = ? AND player_id = ?', [(int)$rc['save_id'], (int)$rc['player_id']]);
	$pl = fv_row('SELECT id, name FROM players WHERE id = ?', [(int)$rc['player_id']]);
	if (!$save || !$pl) { $db->rollBack(); throw new FVError('bad_code', 'Сохранение для этого кода не найдено.'); }
	$n = fv_q('UPDATE restore_codes SET used = ?, used_ip = ? WHERE code = ? AND used IS NULL', [$now, fv_client_ip(), $code])->rowCount();
	if ($n !== 1) { $db->rollBack(); throw new FVError('bad_code', $bad); }
	fv_q('UPDATE players SET reset_baseline = 1 WHERE id = ?', [(int)$pl['id']]);
	$db->commit();
	fv_admin_log('restore_used', (int)$pl['id'], 'код ' . $code . ', сохранение #' . (int)$save['id']);
	$pid = (int)$pl['id'];
	return ['save_b64' => base64_encode(fv_blob($save['data'])), 'save_version' => (int)$save['version'],
		'pid' => $pid, 'tag' => 1000 + $pid, 'name' => (string)$pl['name']];
}
