<?php
// Fishing Valley server — integration tests. Starts PHP's built-in web server on a fresh SQLite database
// (Server/local/test.sqlite, config Server/local/test_config.php passed via FV_CONFIG) and exercises every API action,
// signing, anti-cheat, bans, search, mail, restore codes, season closing (debug clock) and the admin panel.
//
// Run (from the repo root):
//   php -n -d extension_dir=<php>/ext -d extension=pdo_sqlite -d extension=mbstring
//       -d extension=openssl Server/tests/run_tests.php
// Exit code 0 = all passed.
declare(strict_types=1);

$SERVER = dirname(__DIR__);
$WEB = $SERVER . '/Fishing-game';
$LOCAL = $SERVER . '/local';
$PORT = 8779;                  // not 8765: that one is for a manually started local server
$BASE = 'http://127.0.0.1:' . $PORT;
$pass = 0;
$fail = 0;
$OFFSET = 0;
$COOKIES = [];

function ok(bool $cond, string $label, $detail = null): bool
{
	global $pass, $fail;
	if ($cond) { $pass++; return true; }
	$fail++;
	echo "  FAIL: $label" . ($detail !== null ? ' -> ' . (is_string($detail) ? $detail : json_encode($detail, JSON_UNESCAPED_UNICODE)) : '') . "\n";
	return false;
}

function section(string $t): void { echo "== $t\n"; }

function set_offset(int $o): void
{
	global $OFFSET, $LOCAL;
	$OFFSET = $o;
	file_put_contents($LOCAL . '/test_time_offset.txt', (string)$o);
}

function tnow(): int { global $OFFSET; return time() + $OFFSET; }

function http(string $method, string $path, ?string $body = null, array $headers = [], string $ctype = 'application/json'): array
{
	global $BASE;
	$h = [];
	foreach ($headers as $k => $v) { $h[] = $k . ': ' . $v; }
	if ($body !== null) { $h[] = 'Content-Type: ' . $ctype; }
	$ctx = stream_context_create(['http' => ['method' => $method, 'header' => implode("\r\n", $h), 'content' => $body ?? '',
		'ignore_errors' => true, 'timeout' => 30, 'follow_location' => 0, 'protocol_version' => 1.0]]);
	$resp = @file_get_contents($BASE . $path, false, $ctx);
	$hdrs = $http_response_header ?? [];
	$status = 0;
	if (isset($hdrs[0]) && preg_match('/\s(\d{3})\s?/', $hdrs[0], $m)) { $status = (int)$m[1]; }
	return ['status' => $status, 'headers' => $hdrs, 'body' => $resp === false ? '' : $resp];
}

function header_value(array $r, string $name): string
{
	foreach ($r['headers'] as $line) {
		if (stripos($line, $name . ':') === 0) { return trim(substr($line, strlen($name) + 1)); }
	}
	return '';
}

/** Calls the API. $p = signing player ['pid','secret'] or null. Options: raw, sig, nosig, qsig, headers, ts, nonce. */
function api(string $a, array $req = [], ?array $p = null, array $o = []): array
{
	if ($p !== null) {
		$req = ['pid' => $p['pid'], 'ts' => $o['ts'] ?? tnow(), 'nonce' => $o['nonce'] ?? bin2hex(random_bytes(4))] + $req;
	}
	$body = $o['raw'] ?? json_encode($req, JSON_UNESCAPED_UNICODE);
	$headers = $o['headers'] ?? [];
	$path = '/api.php?a=' . urlencode($a);
	if ($p !== null && empty($o['nosig'])) {
		$sig = $o['sig'] ?? hash_hmac('sha1', $body, $p['secret']);
		if (!empty($o['qsig'])) { $path .= '&sig=' . $sig; } else { $headers['X-FV-Sig'] = $sig; }
	}
	$r = http('POST', $path, $body, $headers);
	$j = json_decode($r['body'], true);
	if (!is_array($j)) { ok(false, "api $a returns JSON", substr($r['body'], 0, 300)); return ['ok' => false, 'error' => 'no_json']; }
	ok(stripos(header_value($r, 'Content-Type'), 'application/json; charset=utf-8') === 0, "api $a content-type");
	ok($r['status'] === 200, "api $a HTTP 200", $r['status']);
	return $j;
}

function expect_ok(array $j, string $label): bool { return ok(($j['ok'] ?? false) === true, $label, $j); }
function expect_err(array $j, string $code, string $label): bool
{
	return ok(($j['ok'] ?? true) === false && ($j['error'] ?? '') === $code && is_string($j['message'] ?? null) && $j['message'] !== '', $label, $j);
}

function new_secret(): string { return bin2hex(random_bytes(32)); }

function register(string $name, array $hdr = []): array
{
	$secret = new_secret();
	$j = api('register', ['secret' => $secret, 'name' => $name, 'platform' => 'win', 'ver' => '4.0'], null, ['headers' => $hdr]);
	return ['j' => $j, 'pid' => (int)($j['pid'] ?? 0), 'secret' => $secret, 'tag' => (int)($j['tag'] ?? 0), 'name' => (string)($j['name'] ?? '')];
}

function tdb(): PDO
{
	static $db = null;
	global $LOCAL;
	if ($db === null) {
		$db = new PDO('sqlite:' . $LOCAL . '/test.sqlite', null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
		$db->exec('PRAGMA busy_timeout = 8000');
	}
	return $db;
}

function dbv(string $sql, array $params = [])
{
	$st = tdb()->prepare($sql);
	$st->execute($params);
	return $st->fetchColumn();
}

// ---- admin (cookie jar)
function admin(string $method, string $query, array $form = [], array $hdr = []): array
{
	global $COOKIES;
	if ($COOKIES) { $hdr['Cookie'] = implode('; ', array_map(static fn($k, $v) => "$k=$v", array_keys($COOKIES), $COOKIES)); }
	$r = http($method, '/admin/index.php' . ($query !== '' ? '?' . $query : ''), $method === 'POST' ? http_build_query($form) : null, $hdr,
		'application/x-www-form-urlencoded');
	foreach ($r['headers'] as $line) {
		if (stripos($line, 'Set-Cookie:') === 0 && preg_match('/Set-Cookie:\s*([^=]+)=([^;]*)/i', $line, $m)) {
			if ($m[2] === 'deleted' || $m[2] === '') { unset($COOKIES[$m[1]]); } else { $COOKIES[$m[1]] = $m[2]; }
		}
	}
	return $r;
}

/** POSTs a form and follows the 303 redirect; returns the final page. */
function admin_post(array $form, array $hdr = []): array
{
	$r = admin('POST', '', $form, $hdr);
	ok($r['status'] === 303, 'admin POST ' . ($form['do'] ?? '') . ' redirects', $r['status']);
	$loc = header_value($r, 'Location');
	$q = (string)parse_url($loc, PHP_URL_QUERY);
	return admin('GET', $q, [], $hdr);
}

function csrf_of(string $html): string
{
	return preg_match('/name="csrf" value="([0-9a-f]{64})"/', $html, $m) ? $m[1] : '';
}

// ================================================================== unit tests (in-process)

section('lint');
$it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($SERVER, FilesystemIterator::SKIP_DOTS));
$lintCount = 0;
foreach ($it as $f) {
	if (!str_ends_with($f->getFilename(), '.php')) { continue; }
	$out = [];
	exec(escapeshellarg(PHP_BINARY) . ' -n -l ' . escapeshellarg($f->getPathname()) . ' 2>&1', $out, $code);
	ok($code === 0, 'php -l ' . $f->getFilename(), implode(' ', $out));
	$lintCount++;
}
echo "  linted $lintCount files\n";

section('unit: config / validation / seasons');
$unitRoot = $LOCAL . '/unit_root';
@mkdir($unitRoot, 0777, true);
file_put_contents($unitRoot . '/config.php', "<?php return ['db_dsn' => 'sqlite::memory:', 'debug_time_offset' => 999, 'ip_header' => 'HTTP_X_EVIL'];");
putenv('FV_CONFIG');
define('FV_ROOT', $unitRoot);
require $WEB . '/lib/db.php';
require $WEB . '/lib/validate.php';
require $WEB . '/lib/season.php';
$c = fv_config();
ok($c['_local'] === false && $c['debug_time_offset'] === 0 && $c['ip_header'] === 'REMOTE_ADDR', 'production config ignores debug settings', $c);
ok(abs(fv_now() - time()) <= 1, 'production clock has no offset');
unlink($unitRoot . '/config.php');
rmdir($unitRoot);

foreach (['хуйло', 'Fuck you', 'xyй', 'П И З Д А', 'сука', 'Bl-ya-d', 'fuсk', 'мудак', 'x у й', 'ХУУУУЙ', 'pidor', 'Sh1t', 'ебать', 'манда',
	'Команда-bitch', 'nahuy', '3алупа', 'пидорас'] as $bad) {
	ok(fv_is_profane($bad), 'profane: ' . $bad);
}
foreach (['Рыбак Вася', 'Команда', 'Мудрый Лис', 'Assassin', 'Хлебник', 'Скипетр', 'Anna_B', 'Сукно', 'Oleg', 'Sasha', 'Cocktail',
	'Therapist', 'Херувим', 'Страна', 'Dmitry-4', 'Ивушка', 'Rebalance'] as $good) {
	ok(!fv_is_profane($good), 'clean: ' . $good);
}
ok(fv_normalize_name('  Рыбак   Вася ') === 'Рыбак Вася', 'name collapse');
ok(fv_normalize_name('ABCDEFGHIJKLMNOPQ') === null, 'name 17 chars rejected');
ok(fv_normalize_name('ABCDEFGHIJKLMNOP') === 'ABCDEFGHIJKLMNOP', 'name 16 chars ok');
ok(fv_normalize_name('<b>x</b>') === null, 'name with <> rejected');
ok(fv_normalize_name('___') === null, 'name of separators rejected');
ok(fv_normalize_name('李小龍') === '李小龍', 'name any alphabet');
ok(fv_normalize_status('Ловлю на рассвете!') === 'Ловлю на рассвете!', 'status ok');
ok(fv_normalize_status(str_repeat('а', 41)) === null, 'status 41 chars rejected');
ok(fv_normalize_status('ну ты сука') === null, 'status profanity rejected');
ok(fv_season_id(FV_SEASON_EPOCH) === 0 && fv_season_id(FV_SEASON_EPOCH - 1) === -1 && fv_season_id(FV_SEASON_EPOCH + 259199) === 0
	&& fv_season_id(FV_SEASON_EPOCH + 259200) === 1, 'season ids');
ok(fv_season_bounds(2) === [FV_SEASON_EPOCH + 518400, FV_SEASON_EPOCH + 777600], 'season bounds');
$msk = static fn(string $s): int => (new DateTime($s, new DateTimeZone('Europe/Moscow')))->getTimestamp();
ok(FV_SEASON_EPOCH === $msk('2026-09-01 00:00'), 'epoch = 01.09.2026 00:00 MSK');
ok(FV_SEASON_LEN === 3 * 86400, 'a tournament lasts 3 real days');
ok(fv_season_id($msk('2026-09-25 12:00')) === 8 && fv_season_bounds(8) === [$msk('2026-09-25 00:00'), $msk('2026-09-28 00:00')]
	&& fv_season_dates(8) === '25.09 – 27.09', 'tournament №8 = 25.09–27.09.2026');
ok((1790542800 - FV_SEASON_EPOCH) % FV_SEASON_LEN === 0, 'the update-4 epoch lies on the same 3-day grid (ids shift by +9)');
ok(fv_season_name(8) === 'Турнир №8', 'season name');
ok([fv_tier_for_rank(1, 4), fv_tier_for_rank(2, 4), fv_tier_for_rank(3, 4), fv_tier_for_rank(4, 4), fv_tier_for_rank(1, 1), fv_tier_for_rank(25, 100),
	fv_tier_for_rank(26, 100), fv_tier_for_rank(75, 100), fv_tier_for_rank(76, 100), fv_tier_for_rank(1, 2), fv_tier_for_rank(2, 2),
	fv_tier_for_rank(1, 3), fv_tier_for_rank(2, 3), fv_tier_for_rank(3, 3), fv_tier_for_rank(2, 1), fv_tier_for_rank(0, 5)]
	=== [1, 2, 3, 0, 1, 1, 2, 3, 0, 2, 0, 2, 3, 0, 0, 0], 'tiers by percentile; a lone participant gets gold');
ok(array_map('fv_level_span', range(1, 15)) === [100, 280, 390, 530, 850, 1150, 1500, 2100, 3100, 3100, 6200, 7400, 8800, 10400, 10400],
	'level spans (L10 = 3100; update 6: 11–15, the top step repeats)');
$R = static fn(string $cat, int $t, int $l, int $sid = 8, int $pid = 1) => fv_season_cat_reward($cat, $t, $l, $sid, $pid);
$it = static fn(string $id, int $n) => ['id' => $id, 'n' => $n];
ok($R('s_fish', 1, 5)['items'] === [$it('bait_magic', 6), $it('bait_lure', 10)] && $R('s_fish', 1, 1)['items'] === [$it('bait_magic', 4), $it('bait_lure', 10)]
	&& $R('s_fish', 1, 10)['items'] === [$it('bait_magic', 8), $it('bait_lure', 10)], 's_fish gold: magic ×(3+⌈L/2⌉), lure ×10');
ok($R('s_fish', 1, 11)['items'] === [$it('bait_magic', 9), $it('bait_lure', 10), $it('bait_deepworm', 5)]
	&& $R('s_fish', 2, 15)['items'] === [$it('bait_lure', 12), $it('bait_nightcrawler', 10), $it('bait_deepworm', 3)]
	&& $R('s_fish', 3, 13)['items'] === [$it('bait_grub', 10), $it('bait_deepworm', 2)], 's_fish from level 11: + expert bait 5 / 3 / 2');
ok([$R('s_xp', 1, 15)['coins'], $R('s_biggest', 1, 11)['xp'], $R('s_biggest', 3, 15)['xp']] === [3300, 1860, 624], 'rewards of levels 11–15');
ok($R('s_fish', 2, 5)['items'] === [$it('bait_lure', 7), $it('bait_nightcrawler', 10)] && $R('s_fish', 3, 7)['items'] === [$it('bait_grub', 10)]
	&& $R('s_fish', 2, 5)['coins'] === 0 && $R('s_fish', 2, 5)['xp'] === 0, 's_fish silver / bronze');
ok([$R('s_xp', 1, 5)['coins'], $R('s_xp', 2, 5)['coins'], $R('s_xp', 3, 5)['coins'], $R('s_xp', 1, 1)['coins'], $R('s_xp', 3, 10)['coins']]
	=== [1300, 650, 200, 500, 350] && $R('s_xp', 1, 5)['xp'] === 0 && $R('s_xp', 1, 5)['items'] === [], 's_xp: coins by level');
ok([$R('s_biggest', 1, 5)['xp'], $R('s_biggest', 2, 5)['xp'], $R('s_biggest', 3, 5)['xp'], $R('s_biggest', 2, 3)['xp'], $R('s_biggest', 3, 3)['xp'],
	$R('s_biggest', 1, 1)['xp'], $R('s_biggest', 1, 10)['xp'], $R('s_biggest', 3, 10)['xp']] === [255, 128, 51, 100, 50, 150, 930, 186],
	's_biggest: 30/15/6 % of the span (halves up), at least 150 / 100 / 50 (gold > silver > bronze)');
$earnedOk = true;
foreach (range(1, 15) as $l) {
	foreach ([1 => [3, 4 + $l], 2 => [2, 2 + intdiv($l + 1, 2)], 3 => [1, 1 + intdiv($l + 3, 4)]] as $t => [$kinds, $n]) {
		foreach ([1, 7, 42] as $pid) {
			$items = $R('s_earned', $t, $l, 8, $pid)['items'];
			$ids = array_column($items, 'id');
			$pool = fv_earned_pool($l);
			foreach ($items as $x) {
				$want = preg_match('/^(float|line|net|efloat|eline|reel)_/', $x['id']) ? min(2, $n) : $n;
				if ($x['n'] !== $want) { $earnedOk = false; }
			}
			if (count($ids) !== $kinds || count(array_unique($ids)) !== $kinds || array_diff($ids, $pool)) { $earnedOk = false; }
		}
	}
}
ok($earnedOk, 's_earned: 3/2/1 kinds × (4+L)/(2+⌈L/2⌉)/(1+⌈L/4⌉) from the level pool, tackle ≤ 2');
ok(fv_earned_pool(1) === ['mat_board', 'mat_rope', 'mat_glue', 'res_resin', 'res_feather'] && fv_earned_pool(6)[4] === 'float_feather'
	&& fv_earned_pool(7) === ['mat_glitter', 'float_sliding', 'line_braided', 'mat_glue', 'net_small']
	&& fv_earned_pool(10) === fv_earned_pool(7) && fv_earned_pool(11)[0] === 'mat_log' && fv_earned_pool(15) === fv_earned_pool(11), 'level-band pools');
ok($R('s_earned', 1, 5, 8, 42) === $R('s_earned', 1, 5, 8, 42), 's_earned choice is deterministic');
$firsts = [];
foreach (range(1, 40) as $pid) { $firsts[$R('s_earned', 3, 5, 8, $pid)['items'][0]['id']] = true; }
ok(count($firsts) >= 3, 's_earned choice varies between players', array_keys($firsts));
ok(array_diff(FV_SEASON_REWARD_ITEMS, array_keys(FV_ITEMS)) === [] && array_diff(array_merge(...array_column(FV_EARNED_POOLS, 2)), FV_SEASON_REWARD_ITEMS) === [],
	'every reward item exists in lib/fish.php', array_diff(FV_SEASON_REWARD_ITEMS, array_keys(FV_ITEMS)));
ok($R('s_fish', 0, 5) === ['coins' => 0, 'xp' => 0, 'items' => []] && $R('bogus', 1, 5) === ['coins' => 0, 'xp' => 0, 'items' => []], 'no tier, no reward');
ok(fv_reward_text($R('s_fish', 1, 5)) === 'Волшебная наживка ×6, Блесна ×10' && fv_reward_text($R('s_xp', 2, 5)) === '650 монет'
	&& fv_reward_text($R('s_biggest', 1, 5)) === '255 опыта', 'reward texts');
ok(fv_reward_merge([$R('s_fish', 1, 5), $R('s_fish', 2, 5), $R('s_xp', 1, 5), $R('s_biggest', 2, 5)])
	=== ['coins' => 1300, 'xp' => 128, 'items' => [$it('bait_magic', 6), $it('bait_lure', 17), $it('bait_nightcrawler', 10)]], 'rewards merge');
ok([fv_plural(1, 'а', 'б', 'в'), fv_plural(3, 'а', 'б', 'в'), fv_plural(11, 'а', 'б', 'в'), fv_plural(21, 'а', 'б', 'в'), fv_plural(250, 'а', 'б', 'в')]
	=== ['а', 'б', 'в', 'а', 'в'], 'Russian plurals');
require $WEB . '/lib/settings.php';
ok(fv_version_info() === ['latest_build' => 4, 'latest_name' => '0.4', 'min_build' => 4,
	'download_url' => 'https://example.com/Fishing-game/download/KlevayaDolina.apk', 'changelog' => FV_VERSION_DEFAULTS['changelog']],
	'version defaults without a settings table', fv_version_info());
ok(fv_changelog_lines("• Один\r\n- Два\n\n 3) Три\n* Четыре\nПять") === ['Один', 'Два', 'Три', 'Четыре', 'Пять'], 'changelog lines');
ok(fv_valid_download_url('https://example.com/Fishing-game/download/KlevayaDolina.apk') && !fv_valid_download_url('javascript:alert(1)')
	&& !fv_valid_download_url('ftp://x.ru/a.apk') && !fv_valid_download_url('https://x.ru/a b.apk') && !fv_valid_download_url(''), 'download URL check');
ok(fv_level_for_xp(0) === 1 && fv_level_for_xp(100) === 2 && fv_level_for_xp(379) === 2 && fv_level_for_xp(10000) === 10 && fv_level_for_xp(13099) === 10
	&& fv_level_for_xp(13100) === 11 && fv_level_for_xp(45899) === 14 && fv_level_for_xp(99999) === 15, 'level for xp (update 6: up to 15)');
ok(fv_level_for_xp(99999, 10) === 10 && fv_level_for_xp(25000, 10) === 10 && fv_level_for_xp(6900, 10) === 9, 'level for xp on the 0.5 scale');
ok(fv_client_level_cap([]) === 10 && fv_client_level_cap(['max_level' => 15]) === 15 && fv_client_level_cap(['max_level' => 99]) === 15
	&& fv_client_level_cap(['max_level' => 3]) === 10 && fv_client_level_cap(['max_level' => 'x']) === 10, 'client level scale');
$classicFish = array_filter(FV_FISH, static fn($f) => !$f['u6']);
ok(count($classicFish) === 40 && count(array_filter($classicFish, static fn($f) => $f['legendary'])) === 6, '40 classic fish, 6 legendary');
ok(count(FV_FISH) === 81 && count(array_filter(FV_FISH, static fn($f) => $f['legendary'])) === 13
	&& FV_FISH['efish_swordfish']['loc'] === 'city' && FV_FISH['elegend_dragon']['legendary'] === true && FV_FISH['fish_rudd']['loc'] === 'pond',
	'update 6: +6 classic-location fish, 4 City/Cave fish, 17 expert fish, 6 expert legends, 8 Mill fish');
$millFish = ['fish_ruffe', 'fish_ide', 'fish_rainbow_trout', 'fish_burbot', 'fish_wild_carp', 'fish_grass_carp', 'fish_sterlet', 'fish_mill_master'];
ok(count(array_filter($millFish, static fn($id) => (FV_FISH[$id]['loc'] ?? '') === 'mill' && FV_FISH[$id]['u6'])) === 8
	&& FV_FISH['fish_mill_master']['legendary'] === true && FV_FISH['fish_mill_master']['max'] === 260.0 && FV_FISH['fish_sterlet']['legendary'] === false
	&& (FV_LOCATIONS['mill'] ?? '') === 'Старая мельница' && in_array('mill', FV_LOCATIONS_U6, true), 'update 6 §18: the Old Mill — location and 8 fish');
$millItems = ['seed_rye', 'veg_rye', 'mat_flour', 'food_bliny', 'food_rye_fish_pie', 'bait_bread', 'bait_kolobok', 'bait_reed_bundle'];
ok(array_diff($millItems, FV_ITEMS_U6) === [] && array_diff($millItems, array_keys(FV_ITEMS)) === [] && FV_ITEMS['food_rye_fish_pie'] === 'Рыбный пирог'
	&& FV_ITEMS['food_fish_pie'] === 'Рыбник' && !isset(FV_ITEMS['bobber_millstone']) && (FV_COSMETICS_U6['bobber_millstone'] ?? '') === 'Жёрнов'
	&& FV_ACHIEVEMENTS_TOTAL >= 66, 'update 6 §18: Mill items, the Millstone bobber (cosmetic), ach_legend_mill counted');
$expertCos = ['bobber_helm', 'bobber_dragon', 'hat_marlin_helm', 'hat_dragon_helm', 'outfit_rodmaster', 'outfit_manta', 'pet_dragonet'];
ok(array_diff($expertCos, array_keys(FV_COSMETICS_U6)) === [] && array_intersect($expertCos, array_keys(FV_ITEMS)) === []
	&& FV_COSMETICS_U6['pet_dragonet'] === 'Дракончик Уголёк' && FV_COSMETICS_U6['hat_marlin_helm'] === 'Шлем марлина',
	'update 6 Р64: the boutique «Эксперт» cosmetics are known by name and are not letter items');
$v = [];
ok(fv_fish_show(['fish' => 'fish_mill_master', 'size' => 255.5, 'q' => 3], $v, 't') !== null && $v === []
	&& fv_fish_show(['fish' => 'fish_ruffe', 'size' => 30.0, 'q' => 0], $v, 't') === null && count($v) === 1, 'Mill fish sizes checked by the anti-cheat');
ok(isset(FV_LOCATIONS['forest'], FV_LOCATIONS['city'], FV_LOCATIONS['cave']) && FV_XP_TABLE === [0, 0, 100, 380, 770, 1300, 2150, 3300, 4800, 6900, 10000, 13100, 19300, 26700, 35500, 45900]
	&& FV_MAX_SPECIES >= 81 && FV_MAX_ACHIEVEMENTS >= 66 && count(FV_ITEMS_U6) === 44 && array_diff(FV_ITEMS_U6, array_keys(FV_ITEMS)) === []
	&& FV_ITEMS['mat_hardrock'] === 'Твёрдая порода' && FV_ITEMS['bait_marlin'] === 'Приманка на марлина', 'update 6 locations, XP table, caps, items');
ok(FV_FISH['fish_carp']['max'] === 90.0 && FV_FISH['fish_carp']['loc'] === 'pond', 'fish data from FVDatabase.cpp');
ok((FV_ITEMS['line_heat'] ?? '') === 'Жаропрочная леска' && (FV_ITEMS['bait_ember'] ?? '') === 'Жар-наживка', 'update 5 items (the Zhar-ryba tackle) in lib/fish.php');

section('unit: update 6 — levels 11–15, XP migration, «опыт заработанный», стаж');
$P6 = static fn(array $o = []) => $o + ['reset_baseline' => 0, 'last_sync' => 1000, 'level' => 10, 'xp' => 25000, 'grace_xp' => 0,
	'fish_total' => 0, 'legendary' => 0, 'perfect' => 0, 'species' => 0, 'achievements' => 0, 'coins_earned' => 0, 'playtime' => 0,
	'stations_built' => 0, 'xp_earned' => 25000, 'xp_earned_seen' => -1, 'xp_seniority' => 0, 'xp_migrated' => 0];
$chk6 = static function (array $p, array $stats, int $now = 1600): array { $v = []; $o = fv_check_stats($p, $stats, $now, $v); return [$o, $v]; };
$new6 = ['max_level' => 15];
[$o, $v] = $chk6($P6(), ['level' => 10, 'xp' => 26000]);
ok($v === [] && $o['level'] === 10 && $o['xp'] === 26000 && $o['xp_earned'] === 26000 && !isset($o['xp_migrated']), '0.5 client: level 10 above 10 000 XP, earned += growth', [$o, $v]);
[$o, $v] = $chk6($P6(), ['level' => 11, 'xp' => 26000]);
ok(count($v) === 1 && !isset($o['xp']), '0.5 client cannot claim level 11', $v);
[$o, $v] = $chk6($P6(), ['level' => 10, 'xp' => 26000] + $new6);
ok(count($v) === 1 && str_contains($v[0], 'не соответствует'), 'update-6 client: 26 000 XP is level 12, not 10', $v);
[$o, $v] = $chk6($P6(['xp' => 10000, 'xp_migrated' => 1]), ['level' => 12, 'xp' => 20000] + $new6);
ok($v === [] && $o['level'] === 12 && $o['xp'] === 20000, 'update-6 client: level 12', [$o, $v]);
$mig = ['level' => 10, 'xp' => 10000, 'xp_migration' => 1, 'xp_earned' => 0, 'xp_seniority' => 15500] + $new6;
[$o, $v] = $chk6($P6(), $mig);
ok($v === [] && $o['xp'] === 10000 && $o['xp_migrated'] === 1 && $o['xp_earned_seen'] === 0 && !isset($o['xp_earned'])
	&& $o['xp_seniority'] === 15500, 'the one-off trim to 10 000: accepted, remembered, стаж kept, earned not lowered', [$o, $v]);
[$o, $v] = $chk6($P6(['xp_migrated' => 1]), $mig);
ok(count($v) === 1 && str_contains($v[0], 'опыт уменьшился') && !isset($o['xp']), 'the trim works only once', $v);
[$o, $v] = $chk6($P6(), ['xp' => 9000, 'level' => 9] + $mig);
ok(count($v) === 1 && str_contains($v[0], 'опыт уменьшился'), 'no trim below 10 000', $v);
[$o, $v] = $chk6($P6(['xp' => 9000, 'level' => 9]), ['level' => 9, 'xp' => 9500, 'xp_migration' => 1] + $new6);
ok($v === [] && $o['xp'] === 9500 && $o['xp_migrated'] === 1, 'the flag is used up when nothing had to be trimmed', [$o, $v]);
[$o, $v] = $chk6($P6(['xp' => 10000, 'xp_earned_seen' => 100]), ['xp_earned' => 400] + $new6);
ok($v === [] && $o['xp_earned'] === 25300 && $o['xp_earned_seen'] === 400, 'earned grows by the client counter', [$o, $v]);
[$o, $v] = $chk6($P6(['xp' => 10000, 'xp_earned_seen' => 500]), ['xp_earned' => 400] + $new6);
ok(count($v) === 1 && $o['xp_earned_seen'] === 400 && !isset($o['xp_earned']), 'earned decrease: noted once, rebased', [$o, $v]);
[$o, $v] = $chk6($P6(['xp' => 10000, 'xp_earned_seen' => 0]), ['xp_earned' => 100000] + $new6);
ok(count($v) === 1 && !isset($o['xp_earned']) && !isset($o['xp_earned_seen']), 'earned growth limit', [$o, $v]);
[$o, $v] = $chk6($P6(['xp' => 10000, 'xp_earned_seen' => 0, 'grace_xp' => 70000]), ['xp_earned' => 100000] + $new6);
ok($v === [] && $o['xp_earned'] === 125000, 'claimed reward XP counts for earned (frozen level: paid as coins)', [$o, $v]);
[$o, $v] = $chk6($P6(['xp' => 10000]), ['xp_seniority' => 900000] + $new6);
ok(count($v) === 1 && !isset($o['xp_seniority']), 'стаж cannot exceed the possible trim', $v);
[$o, $v] = $chk6($P6(['xp' => 10000, 'last_sync' => 0]), ['xp_seniority' => 900000, 'xp_earned' => 5] + $new6);
ok($v === [] && $o['xp_seniority'] === 900000 && $o['xp_earned_seen'] === 5, 'first sync: стаж and the earned baseline accepted', [$o, $v]);
[$o, $v] = $chk6($P6(), ['species' => FV_MAX_SPECIES, 'achievements' => FV_MAX_ACHIEVEMENTS]);
ok($v === [] && $o['species'] === FV_MAX_SPECIES, 'species / achievement caps from the data', $v);
// schema upgrade of a 0.5 database (in-memory SQLite of the unit config)
require $WEB . '/lib/schema.php';
fv_db()->exec("CREATE TABLE players (id INTEGER PRIMARY KEY, xp INTEGER NOT NULL DEFAULT 0)");
fv_db()->exec("CREATE TABLE settings (k TEXT PRIMARY KEY, v TEXT NULL)");
fv_db()->exec("INSERT INTO players (id, xp) VALUES (1, 25000), (2, 300)");
fv_settings(true);
$added = fv_schema_upgrade();
ok($added === ['players.xp_earned', 'players.xp_earned_seen', 'players.xp_seniority', 'players.xp_migrated']
	&& fv_all('SELECT id, xp_earned, xp_earned_seen, xp_seniority, xp_migrated FROM players ORDER BY id') == [
		['id' => 1, 'xp_earned' => 25000, 'xp_earned_seen' => -1, 'xp_seniority' => 0, 'xp_migrated' => 0],
		['id' => 2, 'xp_earned' => 300, 'xp_earned_seen' => -1, 'xp_seniority' => 0, 'xp_migrated' => 0]]
	&& fv_setting('schema_version') === '2' && fv_schema_u6_ready(), 'schema upgrade: update-6 columns, xp_earned = xp', $added);

// ================================================================== start the server

section('start server');
$uploadLog = $WEB . '/logs/error_log.php';   // must not change: the tests log into Server/local/test_logs
clearstatcache();
$uploadLogState = is_file($uploadLog) ? [filesize($uploadLog), filemtime($uploadLog)] : null;
$cfgFile = $LOCAL . '/test_config.php';
foreach (['/test.sqlite', '/test.sqlite-wal', '/test.sqlite-shm', '/test_logs/error_log.php'] as $f) { @unlink($LOCAL . $f); }
set_offset(0);
file_put_contents($cfgFile, "<?php\n// generated by Server/tests/run_tests.php\nreturn [\n"
	. "\t'db_dsn' => 'sqlite:' . __DIR__ . '/test.sqlite',\n"
	. "\t'admin_password' => 'test-admin-pass',\n"
	. "\t'install_key' => 'test-install-key',\n"
	. "\t'ip_header' => 'HTTP_X_TEST_IP',\n"
	. "\t'log_dir' => __DIR__ . '/test_logs',\n"
	. "\t'debug_time_offset' => (int)@file_get_contents(__DIR__ . '/test_time_offset.txt'),\n];\n");
$env = getenv();
$env['FV_CONFIG'] = $cfgFile;
$cmd = [PHP_BINARY, '-n', '-d', 'extension_dir=' . ini_get('extension_dir'), '-d', 'extension=pdo_sqlite', '-d', 'extension=mbstring',
	'-d', 'extension=openssl', '-S', '127.0.0.1:' . $PORT, '-t', $WEB];
$logFile = $LOCAL . '/test_server.log';
$proc = proc_open($cmd, [0 => ['pipe', 'r'], 1 => ['file', $logFile, 'w'], 2 => ['file', $logFile, 'a']], $pipes, $WEB, $env);
if (!is_resource($proc)) { echo "cannot start the PHP server\n"; exit(2); }
register_shutdown_function(static function () use ($proc) { proc_terminate($proc); });
$up = false;
for ($i = 0; $i < 50 && !$up; $i++) {
	usleep(100000);
	$r = http('GET', '/api.php?a=ping');
	$up = $r['status'] === 200;
}
if (!ok($up, 'server is up')) { echo file_get_contents($logFile); exit(2); }

// ================================================================== install

section('install.php');
$r = http('GET', '/install.php');
ok(str_contains($r['body'], 'Доступ запрещён'), 'install without key refused');
$r = http('GET', '/install.php?key=wrong-key');
ok(str_contains($r['body'], 'Доступ запрещён'), 'install with a wrong key refused');
$r = http('GET', '/install.php?key=test-install-key');
ok(str_contains($r['body'], 'Готово'), 'install creates tables', substr(strip_tags($r['body']), 0, 300));
$r = http('GET', '/install.php?key=test-install-key');
ok(str_contains($r['body'], 'Готово'), 'install is idempotent');
ok((int)dbv("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'settings'") === 1, 'install creates the settings table');
@unlink($LOCAL . '/test_logs/error_log.php');   // the pre-install ping logged "no such table"
ok(str_contains(file_get_contents($WEB . '/download/.htaccess'), 'AddType application/vnd.android.package-archive .apk')
	&& !is_file($WEB . '/index.html') && is_file($WEB . '/download/README.txt'), 'download folder: APK MIME type, README; index.html replaced');

// ================================================================== ping, clock

section('ping / errors');
$j = api('ping');
expect_ok($j, 'ping');
ok(isset($j['server_time'], $j['season']['id'], $j['season']['starts'], $j['season']['ends']), 'ping fields', $j);
ok(($j['season']['ends'] - $j['season']['starts']) === 259200, 'season is 3 days');
ok(($j['version'] ?? null) === ['latest_build' => 4, 'latest_name' => '0.4', 'min_build' => 4,
	'download_url' => 'https://example.com/Fishing-game/download/KlevayaDolina.apk', 'changelog' => FV_VERSION_DEFAULTS['changelog']]
	&& is_string($j['server_version'] ?? null), 'ping version (defaults)', $j['version'] ?? $j);
// put the clock 2 h after the current season's start, so the whole run stays inside one season
set_offset((int)$j['season']['starts'] + 7200 - time());
$SEASON = (int)$j['season']['id'];
$j = api('ping');
ok(abs((int)$j['server_time'] - tnow()) <= 2 && (int)$j['season']['id'] === $SEASON, 'debug time offset works (local config)', $j);
$SEASON_ENDS = (int)$j['season']['ends'];
$r = http('GET', '/api.php?a=ping');
ok(($j2 = json_decode($r['body'], true)) && $j2['ok'] === true, 'GET ping');
expect_err(api('nope'), 'bad_action', 'unknown action');
$r = http('GET', '/api.php?a=sync');
expect_err(json_decode($r['body'], true) ?: [], 'bad_request', 'GET sync refused');
expect_err(api('register', [], null, ['raw' => '{broken']), 'bad_request', 'broken JSON');

// ================================================================== seasons stored by the update-4 server (epoch 28.09.2026)

section('season renumbering (install.php)');
$OLD_EPOCH = 1790542800;
$oldId = static fn(int $newId) => intdiv(fv_season_bounds($newId)[0] - $OLD_EPOCH, FV_SEASON_LEN);   // = newId - 9
/** A season row for tournament $newId (new numbering), stored under the old id when $legacy. */
$ins = static function (int $newId, bool $legacy, ?int $closed, ?string $res) use ($oldId) {
	$s = fv_season_bounds($newId)[0];
	tdb()->prepare('INSERT INTO seasons (id, starts, ends, closing, closed, results_json) VALUES (?, ?, ?, 0, ?, ?)')
		->execute([$legacy ? $oldId($newId) : $newId, $s, $s + FV_SEASON_LEN, $closed, $res]);
};
$stat = static fn(int $sid, int $pid) => tdb()->prepare('INSERT INTO season_stats (season_id, player_id, fish, updated_at) VALUES (?, ?, 5, 1)')
	->execute([$sid, $pid]);
[$nC, $nO, $nM] = [$SEASON - 3, $SEASON - 2, $SEASON - 4];
$ins($nC, true, 1700000000, json_encode(['id' => $oldId($nC), 'participants' => 0, 'players' => []]));   // closed under the old numbering
$ins($nO, true, null, null);                                                                          // ended, not closed yet
$stat($oldId($nO), 999999);
api('ping');
ok(dbv('SELECT closed FROM seasons WHERE id = ?', [$oldId($nO)]) === null, 'a row of the old numbering is never closed as another season');
$ins($nM, false, null, null);                  // the new server already created this season...
$stat($nM, 999998);
$ins($nM, true, 1700000001, null);    // ...which the old one had closed (rewards sent): must not close twice
$stat($oldId($nM), 999997);
$r = http('GET', '/install.php?key=test-install-key');
ok(str_contains($r['body'], 'Готово') && str_contains($r['body'], 'Турниры перенумерованы (новая нумерация с 01.09.2026): 3'), 'install renumbers old seasons',
	substr(strip_tags($r['body']), 0, 400));
ok((int)dbv('SELECT COUNT(*) FROM seasons WHERE id < ?', [$SEASON - 5]) === 0 && (int)dbv('SELECT closed FROM seasons WHERE id = ?', [$nC]) === 1700000000
	&& json_decode((string)dbv('SELECT results_json FROM seasons WHERE id = ?', [$nC]), true)['id'] === $nC, 'closed season moved with its results');
ok((int)dbv('SELECT COUNT(*) FROM season_stats WHERE season_id = ? AND player_id = 999999', [$nO]) === 1, 'season stats moved');
ok((int)dbv('SELECT closed FROM seasons WHERE id = ?', [$nM]) === 1700000001
	&& (int)dbv('SELECT COUNT(*) FROM season_stats WHERE season_id = ?', [$nM]) === 2, 'merged into the existing season, closed flag kept');
ok(!str_contains(http('GET', '/install.php?key=test-install-key')['body'], 'перенумерованы'), 'renumbering runs once');
api('ping');
ok(dbv('SELECT closed FROM seasons WHERE id = ?', [$nO]) !== null && (int)dbv("SELECT COUNT(*) FROM mail WHERE kind = 'season'") === 0,
	'renumbered season closes normally (no letters for unknown players)');

// ================================================================== register

section('register');
expect_err(api('register', ['secret' => 'xyz', 'name' => 'Вася']), 'bad_request', 'bad secret');
foreach (['хуйло', 'Fuck you', 'xyй', '', '   ', 'ABCDEFGHIJKLMNOPQ', '<script>'] as $bad) {
	expect_err(api('register', ['secret' => new_secret(), 'name' => $bad, 'platform' => 'win']), 'bad_name', 'bad name «' . $bad . '»');
}
$A = register('  Рыбак   Вася ');
expect_ok($A['j'], 'register A');
ok($A['name'] === 'Рыбак Вася' && $A['tag'] === 1000 + $A['pid'] && $A['pid'] > 0, 'A normalised name + tag', $A['j']);
$again = api('register', ['secret' => $A['secret'], 'name' => 'Другое', 'platform' => 'win']);
ok(($again['pid'] ?? 0) === $A['pid'] && $again['name'] === 'Рыбак Вася', 'register is idempotent for the same secret', $again);
$B = register('Anna_B');
$C = register('Вася Пупкин');
$D = register('Dmitry-4');
ok($B['pid'] && $C['pid'] && $D['pid'] && count(array_unique([$A['pid'], $B['pid'], $C['pid'], $D['pid']])) === 4, 'B, C, D registered');
ok(dbv('SELECT platform FROM players WHERE id = ?', [$A['pid']]) === 'win', 'platform stored');

// ================================================================== signing

section('signing');
expect_err(api('sync', [], $A, ['nosig' => true]), 'bad_sig', 'missing signature');
expect_err(api('sync', [], $A, ['sig' => str_repeat('0', 40)]), 'bad_sig', 'wrong signature');
expect_err(api('sync', [], ['pid' => $A['pid'], 'secret' => $B['secret']]), 'bad_sig', 'signed with another secret');
expect_err(api('sync', [], $A, ['ts' => tnow() - 4000]), 'bad_time', 'old timestamp');
expect_err(api('sync', [], $A, ['ts' => tnow() + 4000]), 'bad_time', 'future timestamp');
expect_err(api('sync', [], ['pid' => 99999, 'secret' => $A['secret']]), 'unknown_player', 'unknown pid');
expect_err(api('sync', [], $A, ['nonce' => 'xyz']), 'bad_request', 'bad nonce');
// replay: the exact same signed body twice
$body = json_encode(['pid' => $A['pid'], 'ts' => tnow(), 'nonce' => 'abcdef01']);
$sig = hash_hmac('sha1', $body, $A['secret']);
expect_ok(api('mail', [], $A, ['raw' => $body, 'sig' => $sig]), 'signed request ok');
expect_err(api('mail', [], $A, ['raw' => $body, 'sig' => $sig]), 'replay', 'replayed request rejected');
expect_ok(api('mail', [], $A, ['qsig' => true]), 'signature in ?sig= accepted');

// ================================================================== sync: baseline, season

section('sync');
$prof = static fn(string $loc, array $best, array $show) => ['status' => 'Ловлю на рассвете!', 'favorite_location' => $loc,
	'best_catch' => $best, 'showcase' => $show, 'appearance' => ['hat' => 'hat_straw', 'outfit' => 'outfit_default', 'bobber' => 'bobber_red', 'pet' => ''],
	'start_date' => tnow() - 86400 * 10];
$statsA = ['level' => 3, 'xp' => 400, 'fish_total' => 100, 'legendary' => 1, 'species' => 12, 'coins_earned' => 20000, 'achievements' => 10,
	'perfect' => 5, 'playtime' => 36000, 'stations_built' => 3];
$j = api('sync', ['stats' => $statsA, 'profile' => $prof('river', ['fish' => 'fish_carp', 'size' => 70.2, 'q' => 2],
	[['fish' => 'fish_tsar_carp', 'size' => 120.5, 'q' => 3], ['fish' => 'fish_perch', 'size' => 30, 'q' => 1], ['fish' => 'fish_carp', 'size' => 90.4, 'q' => 2]]),
	'season_best' => null], $A);
expect_ok($j, 'first sync A');
ok($j['name'] === 'Рыбак Вася' && $j['tag'] === $A['tag'] && $j['banned'] === false && $j['mail_unread'] === 0 && $j['rejected'] === 0, 'sync A fields', $j);
ok($j['season']['id'] === $SEASON && $j['season']['participants'] === 0 && $j['season']['my']['rank_fish'] === 0, 'no participants before season fish', $j['season']);
ok((float)dbv('SELECT best_size FROM players WHERE id = ?', [$A['pid']]) === 120.5, 'all-time biggest from showcase');
$base = ['level' => 1, 'xp' => 0, 'fish_total' => 10, 'legendary' => 0, 'species' => 3, 'coins_earned' => 500, 'achievements' => 1, 'perfect' => 0, 'playtime' => 3000, 'stations_built' => 1];
expect_ok(api('sync', ['stats' => $base, 'profile' => $prof('pond', [], []), 'season_best' => null], $B), 'first sync B');
expect_ok(api('sync', ['stats' => $base, 'profile' => $prof('pond', [], []), 'season_best' => null], $C), 'first sync C');
expect_ok(api('sync', ['stats' => $base, 'profile' => $prof('pond', [], []), 'season_best' => null], $D), 'first sync D');

set_offset($OFFSET + 600);   // 10 minutes later: growth limits allow +440 fish
$sA = ['xp' => 700, 'level' => 3, 'fish_total' => 150, 'coins_earned' => 25000, 'legendary' => 2] + $statsA;
$j = api('sync', ['stats' => $sA, 'season_best' => ['fish' => 'fish_carp', 'size' => 80.5, 'q' => 2]], $A);
expect_ok($j, 'sync A +50 fish');
$sB = ['xp' => 200, 'level' => 2, 'fish_total' => 40, 'coins_earned' => 3500] + $base;
$j = api('sync', ['stats' => $sB, 'season_best' => ['fish' => 'fish_perch', 'size' => 35, 'q' => 1]], $B);
expect_ok($j, 'sync B +30 fish');
$sC = ['fish_total' => 20, 'coins_earned' => 600, 'legendary' => 1] + $base;
expect_ok(api('sync', ['stats' => $sC], $C), 'sync C +10 fish');
$sD = ['fish_total' => 15, 'legendary' => 1] + $base;
$jD = api('sync', ['stats' => $sD], $D);
expect_ok($jD, 'sync D +5 fish');
ok($jD['season']['participants'] === 4 && $jD['season']['my']['rank_fish'] === 4 && $jD['season']['my']['tier'] === 0, 'D: 4th of 4, no tier', $jD['season']);
$jA = api('sync', [], $A);
ok($jA['season']['my']['rank_fish'] === 1 && $jA['season']['my']['tier'] === 1 && $jA['season']['my']['fish'] === 50
	&& abs($jA['season']['my']['best_pct'] - 0.25) < 1e-6 && $jA['season']['my']['rank_biggest'] === 1, 'A: 1st, gold, 50 season fish', $jA['season']);
$myA = $jA['season']['my'];
ok([$myA['tier_fish'], $myA['tier_earned'], $myA['tier_xp'], $myA['tier_biggest']] === [1, 2, 2, 2]
	&& [$myA['rank_earned'], $myA['rank_xp']] === [1, 1] && [$myA['n_fish'], $myA['n_earned'], $myA['n_xp'], $myA['n_biggest']] === [4, 3, 2, 2],
	'A: a tier per category (1 of 4 gold; 1 of 3, 1 of 2 silver)', $myA);
ok([$jD['season']['my']['tier_fish'], $jD['season']['my']['tier_earned'], $jD['season']['my']['rank_earned']] === [0, 0, 0], 'D: no tiers', $jD['season']['my']);
$jB = api('sync', [], $B);
ok($jB['season']['my']['rank_fish'] === 2 && $jB['season']['my']['tier'] === 2 && $jB['season']['my']['tier_fish'] === 2
	&& $jB['season']['my']['tier_earned'] === 3 && $jB['season']['my']['tier_xp'] === 0 && $jB['season']['my']['rank_xp'] === 2, 'B: silver / bronze / none', $jB['season']);
$jC = api('sync', [], $C);
ok($jC['season']['my']['rank_fish'] === 3 && $jC['season']['my']['tier'] === 3, 'C: bronze', $jC['season']);

section('anti-cheat');
$susp0 = (int)dbv('SELECT suspicious FROM players WHERE id = ?', [$A['pid']]);
$j = api('sync', ['stats' => ['fish_total' => 1150] + $sA], $A);
ok($j['ok'] === true && $j['rejected'] === 1, 'fish +1000 in a minute rejected', $j);
ok((int)dbv('SELECT fish_total FROM players WHERE id = ?', [$A['pid']]) === 150, 'rejected field not applied');
ok((int)dbv('SELECT suspicious FROM players WHERE id = ?', [$A['pid']]) === $susp0 + 1, 'suspicious counter grows');
ok(str_contains((string)dbv('SELECT notes FROM players WHERE id = ?', [$A['pid']]), 'всего рыбы +1000'), 'note stored');
$j = api('sync', ['stats' => ['level' => 5, 'xp' => 700]], $A);
ok($j['rejected'] === 1 && (int)dbv('SELECT level FROM players WHERE id = ?', [$A['pid']]) === 3, 'level inconsistent with xp rejected', $j);
$j = api('sync', ['stats' => ['fish_total' => 120]], $A);
ok($j['rejected'] === 1, 'decreasing counter rejected');
$j = api('sync', ['stats' => ['species' => FV_MAX_SPECIES + 1, 'achievements' => FV_MAX_ACHIEVEMENTS + 1]], $A);
ok($j['rejected'] === 2 && (int)dbv('SELECT species FROM players WHERE id = ?', [$A['pid']]) === 12, 'species/achievement caps', [$j, dbv('SELECT species FROM players WHERE id = ?', [$A['pid']])]);
$j = api('sync', ['season_best' => ['fish' => 'fish_carp', 'size' => 95, 'q' => 1]], $A);
ok($j['rejected'] === 1 && (float)dbv('SELECT best_size FROM season_stats WHERE player_id = ? AND season_id = ?', [$A['pid'], $SEASON]) === 80.5, 'oversized fish rejected');
$j = api('sync', ['profile' => ['best_catch' => ['fish' => 'fish_shark', 'size' => 10, 'q' => 0]]], $A);
ok($j['rejected'] === 1, 'unknown fish rejected');
$j = api('sync', ['stats' => ['coins_earned' => 25000 + 150000]], $A);
ok($j['rejected'] === 1, 'coins growth limit');

// ================================================================== board

section('board');
$j = api('board', ['cat' => 'fish', 'period' => 'all', 'offset' => 0, 'limit' => 20, 'pid' => $B['pid']]);
expect_ok($j, 'board fish');
$names = array_column($j['entries'] ?? [], 'pid');
ok($names === [$A['pid'], $B['pid'], $C['pid'], $D['pid']] && $j['total'] === 4, 'fish order A B C D', $j);
ok($j['entries'][0]['rank'] === 1 && $j['entries'][0]['value'] === 150 && $j['entries'][0]['tag'] === $A['tag'] && $j['entries'][0]['level'] === 3, 'entry fields', $j['entries'][0]);
ok($j['me']['pid'] === $B['pid'] && $j['me']['rank'] === 2 && $j['me']['me'] === true && $j['entries'][1]['me'] === true && $j['entries'][0]['me'] === false, 'me row', $j['me']);
ok(isset($j['season']['id'], $j['season']['ends']), 'board season info');
$j = api('board', ['cat' => 'fish', 'q' => '#' . $C['tag']]);
ok(count($j['entries']) === 1 && $j['entries'][0]['pid'] === $C['pid'] && $j['entries'][0]['rank'] === 3 && $j['total'] === 1, 'search by #tag keeps the global rank', $j);
$j = api('board', ['cat' => 'fish', 'q' => (string)$D['tag']]);
ok(count($j['entries']) === 1 && $j['entries'][0]['pid'] === $D['pid'], 'search by number without #', $j);
$j = api('board', ['cat' => 'fish', 'q' => 'вас']);
ok($j['total'] === 2 && array_column($j['entries'], 'pid') === [$A['pid'], $C['pid']], 'search by name (Cyrillic, case-insensitive)', $j);
$j = api('board', ['cat' => 'level', 'q' => 'ANNA']);
ok($j['total'] === 1 && $j['entries'][0]['pid'] === $B['pid'], 'search by name (Latin, case-insensitive)', $j);
$j = api('board', ['cat' => 'level', 'q' => '%']);
ok($j['total'] === 0, 'LIKE wildcards escaped', $j);
$j = api('board', ['cat' => 'biggest']);
ok($j['entries'][0]['pid'] === $A['pid'] && $j['entries'][0]['value'] === 1205 && $j['entries'][0]['fish'] === 'fish_tsar_carp'
	&& $j['entries'][0]['size'] === 120.5 && $j['entries'][0]['q'] === 3 && $j['entries'][1]['value'] === 350, 'biggest board (mm, fish)', $j);
$j = api('board', ['cat' => 'legend']);
ok(array_column($j['entries'], 'pid') === [$A['pid'], $C['pid'], $D['pid']], 'ties: earlier achiever / lower id first', $j);
$j = api('board', ['cat' => 'fish', 'period' => 'season']);
ok($j['cat'] === 's_fish' && $j['period'] === 'season' && array_column($j['entries'], 'value') === [50, 30, 10, 5], 'season board via period', $j);
$j = api('board', ['cat' => 's_biggest']);
ok(count($j['entries']) === 2 && $j['entries'][0]['value'] === 805 && $j['entries'][0]['fish'] === 'fish_carp', 'season biggest', $j);
$j = api('board', ['cat' => 'level', 'limit' => 500, 'offset' => 1]);
ok($j['ok'] === true && count($j['entries']) === 3 && $j['entries'][0]['rank'] === 2, 'offset + limit clamp', $j);
expect_err(api('board', ['cat' => 'bogus']), 'bad_request', 'unknown category');

// ================================================================== profile

section('profile');
$j = api('profile', ['id' => $A['pid']]);
expect_ok($j, 'profile A');
$pr = $j['profile'] ?? [];
ok($pr['name'] === 'Рыбак Вася' && $pr['tag'] === $A['tag'] && $pr['level'] === 3 && $pr['xp'] === 700 && $pr['fish_total'] === 150
	&& $pr['status'] === 'Ловлю на рассвете!' && $pr['favorite_location'] === 'river' && $pr['best_catch']['fish'] === 'fish_carp'
	&& count($pr['showcase']) === 3 && $pr['appearance']['hat'] === 'hat_straw' && $pr['start_date'] > 0 && $pr['created'] > 0
	&& $pr['last_seen'] > 0 && $pr['banned'] === false && $pr['ranks']['fish'] === 1 && $pr['ranks']['biggest'] === 1
	&& $pr['playtime'] === 36000 && $pr['stations_built'] === 3 && $pr['species'] === 12, 'profile fields', $pr);
ok(!isset($pr['secret']) && !str_contains(json_encode($j), $A['secret']), 'no secret in the profile');
expect_err(api('profile', ['id' => 99999]), 'not_found', 'unknown profile');
ok((api('profile', ['tag' => '#' . $B['tag']])['profile']['pid'] ?? 0) === $B['pid'], 'profile by tag');

// ================================================================== names / status

section('set_name / set_status');
$j = api('set_name', ['name' => ' Вася   Рыбак '], $A);
ok($j['ok'] === true && $j['name'] === 'Вася Рыбак', 'set_name normalises', $j);
expect_err(api('set_name', ['name' => 'Пиздец'], $A), 'bad_name', 'set_name profanity');
expect_ok(api('set_name', ['name' => 'Команда'], $A), 'set_name: no false positive');
expect_ok(api('set_name', ['name' => 'Рыбак Вася'], $A), 'set_name back');
$j = api('set_status', ['status' => 'Клюёт!'], $A);
ok($j['ok'] === true && $j['status'] === 'Клюёт!', 'set_status');
expect_err(api('set_status', ['status' => 'ну ты сука'], $A), 'bad_status', 'status profanity');
expect_err(api('set_status', ['status' => str_repeat('я', 41)], $A), 'bad_status', 'status too long');

// ================================================================== saves

section('save_upload');
set_offset($OFFSET + 660);    // fresh signed-request window
$saveData = "FVSAVE\x00\x01" . random_bytes(2000);
$j = api('save_upload', ['save_b64' => base64_encode($saveData), 'save_version' => 7], $A);
ok($j['ok'] === true && $j['stored'] === true, 'save stored', $j);
$j = api('save_upload', ['save_b64' => base64_encode($saveData), 'save_version' => 7], $A);
ok($j['ok'] === true && ($j['duplicate'] ?? false) === true, 'identical save not duplicated', $j);
for ($i = 0; $i < 13; $i++) { api('save_upload', ['save_b64' => base64_encode('S' . $i . random_bytes(100)), 'save_version' => 7], $A); }
$saveData = "FVSAVE-latest" . random_bytes(3000);
api('save_upload', ['save_b64' => base64_encode($saveData), 'save_version' => 8], $A);
ok((int)dbv('SELECT COUNT(*) FROM saves WHERE player_id = ?', [$A['pid']]) === 12, 'only the newest 12 saves are kept');
expect_err(api('save_upload', ['save_b64' => base64_encode(str_repeat('x', 716801))], $A), 'too_big', 'save > 700 KB');
expect_err(api('save_upload', ['save_b64' => '!!!notbase64'], $A), 'bad_request', 'broken base64');
ok((int)dbv('SELECT COUNT(*) FROM saves WHERE player_id = ?', [$B['pid']]) === 0, 'saves are per player');

// ================================================================== admin

section('admin');
set_offset($OFFSET + 660);
$r = admin('GET', '');
ok($r['status'] === 200 && str_contains($r['body'], 'Вход в админку') && isset($COOKIES['FVADMIN']), 'login page + session cookie');
ok(stripos(header_value($r, 'X-Robots-Tag'), 'noindex') !== false, 'admin X-Robots-Tag noindex');
$csrf = csrf_of($r['body']);
ok(strlen($csrf) === 64, 'login form has a CSRF token');
$r = admin_post(['do' => 'login', 'password' => 'test-admin-pass']);
ok(str_contains($r['body'], 'Форма устарела') && str_contains($r['body'], 'Вход в админку'), 'login without CSRF refused');
$r = admin_post(['do' => 'login', 'password' => 'wrong', 'csrf' => $csrf]);
ok(str_contains($r['body'], 'Неверный пароль'), 'wrong password refused');
$r = admin_post(['do' => 'login', 'password' => 'test-admin-pass', 'csrf' => $csrf]);
ok(str_contains($r['body'], 'Обзор') && str_contains($r['body'], 'Подозрительные игроки') && str_contains($r['body'], 'Рыбак Вася'), 'logged in: dashboard', substr(strip_tags($r['body']), 0, 400));
$csrf = csrf_of($r['body']);
ok($csrf !== '', 'new CSRF after login');

section('admin: game version + download page');
$DEF = FV_VERSION_DEFAULTS;
tdb()->exec('DROP TABLE settings');   // a database of the update-4 server (install.php not re-run yet)
$j = api('ping');
ok(($j['version']['latest_name'] ?? '') === '0.4' && $j['version']['min_build'] === 4, 'ping works without the settings table', $j);
$r = http('GET', '/');
ok($r['status'] === 200 && str_contains($r['body'], 'Версия 0.4') && str_contains($r['body'], 'Скачать для Android'), 'download page works without the settings table');
$r = admin('GET', 'p=version');
ok(str_contains($r['body'], 'Таблицы настроек ещё нет'), 'version page warns about the missing table');
$r = http('GET', '/install.php?key=test-install-key');
ok(str_contains($r['body'], 'Готово') && str_contains($r['body'], 'Игроков в базе: 4')
	&& (int)dbv("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'settings'") === 1, 'install adds settings to an existing database');
$r = admin('GET', 'p=version');
ok($r['status'] === 200 && str_contains($r['body'], 'value="0.4"') && str_contains($r['body'], 'KlevayaDolina.apk') && !str_contains($r['body'], 'Таблицы настроек ещё нет')
	&& str_contains($r['body'], 'внешняя ссылка'), 'version page with defaults', substr(strip_tags($r['body']), 0, 300));
$APK_URL = $BASE . '/download/FVTest.apk';
$form = ['do' => 'version', 'latest_name' => '0.5', 'latest_build' => 5, 'min_build' => 4, 'download_url' => $APK_URL,
	'changelog' => "• Новая река\n- Больше рыбы <b>x</b>\n\nТурниры каждые 3 дня", 'csrf' => $csrf];
$r = admin_post(['csrf' => 'bad'] + $form);
ok(str_contains($r['body'], 'Форма устарела') && api('ping')['version']['latest_build'] === 4, 'version: no change without CSRF');
foreach ([['min_build' => 6, 'не может быть больше'], ['download_url' => 'javascript:alert(1)', 'Ссылка на APK'], ['latest_name' => '<b>', 'Название версии'],
	['latest_build' => 0, 'Номер сборки'], ['changelog' => str_repeat('я', 2001), 'не длиннее 2000']] as $bad) {
	$needle = array_pop($bad);
	$r = admin_post($bad + $form);
	ok(str_contains($r['body'], $needle) && api('ping')['version']['latest_build'] === 4, 'version rejected: ' . $needle);
}
$r = admin_post($form);
ok(str_contains($r['body'], 'Версия игры сохранена: 0.5 (сборка 5, играть можно со сборки 4)'), 'version saved', substr(strip_tags($r['body']), 0, 300));
$j = api('ping');
ok(($j['version'] ?? null) === ['latest_build' => 5, 'latest_name' => '0.5', 'min_build' => 4, 'download_url' => $APK_URL,
	'changelog' => "• Новая река\n- Больше рыбы <b>x</b>\n\nТурниры каждые 3 дня"], 'ping returns the new version', $j['version'] ?? $j);
ok(str_contains((string)dbv("SELECT details FROM admin_log WHERE action = 'version'"), 'сборка 4 → 5')
	&& (int)dbv("SELECT COUNT(*) FROM admin_log WHERE action = 'version'") === 1, 'version change logged');
$r = admin_post($form);
ok(str_contains($r['body'], 'Ничего не изменилось') && (int)dbv("SELECT COUNT(*) FROM admin_log WHERE action = 'version'") === 1, 'unchanged version not logged twice');
ok(str_contains($r['body'], 'FVTest.apk') && str_contains($r['body'], 'не загружен'), 'version page: the APK is not uploaded yet');
$r = http('GET', '/');
ok(str_contains($r['body'], 'Версия 0.5') && str_contains($r['body'], '<li>Новая река</li>') && str_contains($r['body'], 'Больше рыбы &lt;b&gt;x&lt;/b&gt;')
	&& !str_contains($r['body'], '<b>x</b>') && str_contains($r['body'], 'Файл новой версии загружается') && !str_contains($r['body'], 'href="' . $APK_URL),
	'download page: version, escaped changelog, no button until the APK is uploaded');
$apkFile = $WEB . '/download/FVTest.apk';
file_put_contents($apkFile, str_repeat("\x50\x4B", 786432));   // 1.5 MB
$r = http('GET', '/');
@unlink($apkFile);
ok(str_contains($r['body'], 'href="' . $APK_URL . '" download') && str_contains($r['body'], 'APK · 1,5 МБ · Android 8.0 и новее')
	&& str_contains($r['body'], 'Турнир №' . $SEASON) && str_contains($r['body'], '<b>FVTest.apk</b>'), 'download page: button, size, tournament', substr(strip_tags($r['body']), 0, 600));
ok(str_contains(header_value($r, 'Content-Type'), 'text/html; charset=utf-8') && str_contains($r['body'], 'name="viewport"'), 'download page: UTF-8, phone viewport');
$r = admin('GET', 'p=player&id=' . $A['pid']);
ok($r['status'] === 200 && str_contains($r['body'], 'Рыбак Вася') && str_contains($r['body'], 'всего рыбы +1000') && str_contains($r['body'], 'Сохранения'), 'player page');
$mails0 = (int)dbv('SELECT COUNT(*) FROM mail WHERE player_id = ?', [$A['pid']]);
$r = admin_post(['do' => 'gift', 'id' => $A['pid'], 'coins' => 500, 'xp' => 1000, 'item' => 'mat_glitter', 'item_n' => 3, 'csrf' => 'bad']);
ok(str_contains($r['body'], 'Форма устарела') && (int)dbv('SELECT COUNT(*) FROM mail WHERE player_id = ?', [$A['pid']]) === $mails0, 'admin action without CSRF refused');
$r = admin_post(['do' => 'gift', 'id' => $A['pid'], 'coins' => 500, 'xp' => 1000, 'set_level' => 0, 'item' => 'mat_glitter', 'item_n' => 3,
	'title' => 'Подарок', 'body' => 'Держи!', 'csrf' => $csrf]);
ok(str_contains($r['body'], 'Подарок отправлен') && (int)dbv('SELECT COUNT(*) FROM mail WHERE player_id = ?', [$A['pid']]) === $mails0 + 1, 'gift mail created (CSRF ok)');
ok((int)dbv("SELECT COUNT(*) FROM admin_log WHERE action = 'gift' AND player_id = ?", [$A['pid']]) === 1, 'admin action logged');
$r = admin_post(['do' => 'message', 'id' => $B['pid'], 'title' => 'Привет', 'body' => 'Как клёв?', 'csrf' => $csrf]);
ok(str_contains($r['body'], 'Сообщение отправлено'), 'message sent');
$latest = (int)dbv('SELECT MAX(id) FROM saves WHERE player_id = ?', [$A['pid']]);
$r = admin_post(['do' => 'make_code', 'id' => $A['pid'], 'save_id' => $latest, 'csrf' => $csrf]);
$CODE = preg_match('/class="code">([A-Z0-9]{8})</', $r['body'], $m) ? $m[1] : '';
ok($CODE !== '' && preg_match('/^[ABCDEFGHJKMNPQRSTUVWXYZ2-9]{8}$/', $CODE) === 1, 'restore code created', $CODE);
$r = admin('GET', 'p=save&id=' . $latest);
ok($r['body'] === $saveData && str_contains(header_value($r, 'Content-Disposition'), '.sav'), 'download .sav');
$r = admin_post(['do' => 'ban', 'id' => $C['pid'], 'reason' => 'Читы на монеты', 'csrf' => $csrf]);
ok(str_contains($r['body'], 'заблокирован'), 'ban C');
foreach (['p=players' => 'Игроки', 'p=players&q=%23' . $A['tag'] => 'Рыбак Вася', 'p=players&q=' . $B['pid'] => 'Anna_B', 'p=players&q=' . urlencode('пуп') => 'Вася Пупкин',
	'p=players&s=fish&d=asc' => 'Dmitry-4', 'p=players&f=susp' => 'Рыбак Вася', 'p=players&f=banned' => 'Вася Пупкин', 'p=seasons' => 'История',
	'p=season&id=' . $SEASON => 'прогноз', 'p=mail' => 'Рассылка', 'p=log' => 'Подарок'] as $q => $needle) {
	$r = admin('GET', $q);
	ok($r['status'] === 200 && str_contains($r['body'], $needle), 'admin page ' . $q, substr(strip_tags($r['body']), 0, 200));
}
$r = admin_post(['do' => 'broadcast', 'title' => 'Новости', 'body' => 'Скоро турнир!', 'coins' => 10, 'confirm' => '1', 'csrf' => $csrf]);
ok(str_contains($r['body'], 'Рассылка отправлена: 3'), 'broadcast to 3 non-banned players', substr(strip_tags($r['body']), 0, 300));
// login rate limit from another IP
$blocked = false;
for ($i = 0; $i < 11; $i++) {
	$r = admin_post(['do' => 'login', 'password' => 'nope', 'csrf' => $csrf], ['X-Test-IP' => '10.9.9.9']);
	$blocked = str_contains($r['body'], 'Слишком много неудачных попыток');
}
ok($blocked, 'admin login rate limit');

// ================================================================== mail

section('mail / claim');
$j = api('mail', [], $A);
expect_ok($j, 'mail list');
$gift = null;
foreach ($j['mail'] ?? [] as $m) { if ($m['title'] === 'Подарок') { $gift = $m; } }
ok($gift !== null && $gift['kind'] === 'admin' && $gift['coins'] === 500 && $gift['xp'] === 1000 && $gift['items'] === [['id' => 'mat_glitter', 'n' => 3]]
	&& $gift['claimed'] === 0 && $gift['created'] > 0 && $j['mail_unread'] === 2, 'gift letter fields', $j);
expect_ok(api('sync', [], $A), 'sync right before the claim');
$j = api('mail_claim', ['id' => $gift['id']], $A);
ok($j['ok'] === true && $j['claimed'] === true && $j['mail']['coins'] === 500, 'claim', $j);
expect_err(api('mail_claim', ['id' => $gift['id']], $A), 'already_claimed', 'second claim');
expect_err(api('mail_claim', ['id' => $gift['id']], $B), 'not_found', 'claiming another player\'s letter');
ok((int)dbv('SELECT grace_xp FROM players WHERE id = ?', [$A['pid']]) === 1000, 'claimed XP expected by the next sync');
$j = api('sync', ['stats' => ['xp' => 6600, 'level' => 8]], $A);   // +5900 now: 5000 + 1000 grace
ok($j['rejected'] === 0 && (int)dbv('SELECT xp FROM players WHERE id = ?', [$A['pid']]) === 6600, 'mail XP accepted by the growth check', $j);
ok((api('sync', [], $B)['mail_unread'] ?? -1) === 2, 'mail_unread in sync');

// ================================================================== ban

section('ban');
$j = api('sync', [], $C);
ok(($j['error'] ?? '') === 'banned' && str_contains($j['message'], 'Читы на монеты') && ($j['reason'] ?? '') === 'Читы на монеты', 'banned sync', $j);
$j = api('board', ['cat' => 'fish', 'pid' => $C['pid']]);
ok(!in_array($C['pid'], array_column($j['entries'], 'pid'), true) && $j['me'] === null && $j['total'] === 3, 'banned not on the board', $j);
ok((api('profile', ['id' => $C['pid']])['profile']['banned'] ?? false) === true, 'banned flag in the profile');
$r = admin_post(['do' => 'unban', 'id' => $C['pid'], 'csrf' => $csrf]);
ok(str_contains($r['body'], 'разблокирован'), 'unban');
expect_ok(api('sync', [], $C), 'sync after unban');

// ================================================================== restore

section('restore');
set_offset($OFFSET + 660);
$seasonFishA = (int)dbv('SELECT fish FROM season_stats WHERE season_id = ? AND player_id = ?', [$SEASON, $A['pid']]);
expect_err(api('restore', ['code' => 'ZZZZZZZZ']), 'bad_code', 'wrong code');
expect_err(api('restore', ['code' => 'bad']), 'bad_code', 'malformed code');
$j = api('restore', ['code' => strtolower(substr($CODE, 0, 4)) . '-' . substr($CODE, 4)]);
ok($j['ok'] === true && base64_decode($j['save_b64'] ?? '') === $saveData && $j['pid'] === $A['pid'] && $j['name'] === 'Рыбак Вася' && $j['save_version'] === 8, 'restore by code', array_diff_key($j, ['save_b64' => 1]));
ok(!str_contains(json_encode($j), $A['secret']), 'restore answer has no secret field');
expect_err(api('restore', ['code' => $CODE]), 'bad_code', 'code is one-time');
$j = api('sync', ['stats' => ['fish_total' => 60, 'xp' => 300, 'level' => 2, 'coins_earned' => 9000]], $A);
ok($j['rejected'] === 0 && (int)dbv('SELECT fish_total FROM players WHERE id = ?', [$A['pid']]) === 60, 'lower counters accepted after a restore', $j);
ok((int)dbv('SELECT fish FROM season_stats WHERE season_id = ? AND player_id = ?', [$SEASON, $A['pid']]) === $seasonFishA, 'season progress kept after a restore');
$j = api('sync', ['stats' => ['fish_total' => 50]], $A);
ok($j['rejected'] === 1, 'baseline reset only once');
$j = api('sync', ['stats' => ['fish_total' => 70]], $A);
ok($j['rejected'] === 0 && $j['season']['my']['fish'] === $seasonFishA + 10, 'season counts from the new baseline', $j['season']['my']);
$limited = false;
for ($i = 0; $i < 21; $i++) {
	$j = api('restore', ['code' => 'AAAAAAAA'], null, ['headers' => ['X-Test-IP' => '10.1.1.1']]);
	if (($j['error'] ?? '') === 'rate_limited') { $limited = $i === 20; break; }
}
ok($limited, 'restore: 20 attempts per hour per IP');
$limited = false;
for ($i = 0; $i < 11; $i++) {
	$x = register('Бот ' . $i, ['X-Test-IP' => '10.2.2.2']);
	if (($x['j']['error'] ?? '') === 'rate_limited') { $limited = $i === 10; break; }
}
ok($limited, 'register: 10 per hour per IP');

// ================================================================== signed rate limit

section('rate limit');
set_offset($OFFSET + 660);
$n = 0;
$limited = false;
for ($i = 0; $i < 70; $i++) {
	$j = api('mail', [], $D);
	if (($j['error'] ?? '') === 'rate_limited') { $limited = true; break; }
	$n++;
}
ok($limited && $n === 60, '60 signed requests per 10 min per player', $n);
set_offset($OFFSET + 660);
expect_ok(api('mail', [], $D), 'limit window moves on');

// ================================================================== season closing

section('season closing');
$lvl = static fn(array $p) => (int)dbv('SELECT level FROM players WHERE id = ?', [$p['pid']]);
$LA = $lvl($A); $LB = $lvl($B); $LC = $lvl($C);
// final season values: fish A 60 > B 30 > C 10 > D 5 (N 4); earned A 5000 > B 3000 > C 100 (N 3); xp A 6200 > B 200 (N 2);
// biggest A 80.5 > B 35 (N 2)  =>  A: gold, silver, silver, silver; B: silver, bronze, -, -; C: bronze, -, -, -; D: nothing
$want = static fn(array $p, int $sid, int $l, array $tiers) => fv_reward_merge(array_map(
	static fn($cat) => fv_season_cat_reward($cat, $tiers[$cat] ?? 0, $l, $sid, $p['pid']), array_keys(FV_SEASON_CATS)));
$sm = static fn(array $p) => tdb()->query("SELECT * FROM mail WHERE kind = 'season' AND player_id = " . (int)$p['pid'] . ' ORDER BY id')->fetchAll();
$letterIs = static function (array $m, array $w): bool {
	return (int)$m['coins'] === $w['coins'] && (int)$m['xp'] === $w['xp'] && json_decode($m['items_json'], true) === $w['items'];
};
set_offset($SEASON_ENDS + 60 - time());
$j = api('ping');
ok($j['season']['id'] === $SEASON + 1, 'next season', $j['season']);
ok(dbv('SELECT closed FROM seasons WHERE id = ?', [$SEASON]) !== null, 'season closed lazily');
$res = json_decode((string)dbv('SELECT results_json FROM seasons WHERE id = ?', [$SEASON]), true);
ok(is_array($res) && $res['participants'] === 4 && $res['tiers'] == [1 => 1, 2 => 4, 3 => 2] && $res['rewarded'] === 3
	&& $res['counts'] == ['s_fish' => 4, 's_earned' => 3, 's_xp' => 2, 's_biggest' => 2] && $res['tiers_by_cat']['s_fish'] == [1 => 1, 2 => 1, 3 => 1],
	'results_json (awards per category)', $res['tiers'] ?? $res);
$title = 'Итоги турнира №' . $SEASON;
$mA = $sm($A); $mB = $sm($B); $mC = $sm($C); $mD = $sm($D);
$wA = $want($A, $SEASON, $LA, ['s_fish' => 1, 's_earned' => 2, 's_xp' => 2, 's_biggest' => 2]);
ok(count($mA) === 1 && $mA[0]['title'] === $title && $letterIs($mA[0], $wA) && $wA['coins'] === 150 + 100 * $LA
	&& $wA['xp'] === max(100, intdiv(fv_level_span($LA) * 15 + 50, 100)) && $wA['items'][0] === ['id' => 'bait_magic', 'n' => 3 + intdiv($LA + 1, 2)]
	&& count($wA['items']) === 4, 'A: one letter with 4 rewards (gold baits, silver items / coins / XP)', [$mA, $wA]);
ok(str_contains($mA[0]['body'], 'Улов: 1 место из 4 — золото: Волшебная наживка ×') && str_contains($mA[0]['body'], 'Заработок: 1 место из 3 — серебро: ')
	&& str_contains($mA[0]['body'], 'Опыт: 1 место из 2 — серебро: ' . (150 + 100 * $LA) . ' монет')
	&& str_contains($mA[0]['body'], 'Самая крупная рыба: 1 место из 2 — серебро: ' . $wA['xp'] . ' опыта')
	&& str_contains($mA[0]['body'], 'Участников: 4') && str_contains($mA[0]['body'], 'Награды в 4 категориях из 4'), 'A: letter text', $mA[0]['body']);
$wB = $want($B, $SEASON, $LB, ['s_fish' => 2, 's_earned' => 3]);
ok(count($mB) === 1 && $letterIs($mB[0], $wB) && $wB['coins'] === 0 && $wB['xp'] === 0
	&& str_contains($mB[0]['body'], 'Улов: 2 место из 4 — серебро: Блесна ×' . (4 + intdiv($LB + 1, 2)) . ', Выползок ×10')
	&& str_contains($mB[0]['body'], 'Опыт: 2 место из 2 — без награды'), 'B: silver baits + bronze item', [$mB, $wB]);
ok(count($mC) === 1 && $letterIs($mC[0], ['coins' => 0, 'xp' => 0, 'items' => [['id' => 'bait_grub', 'n' => 10]]])
	&& str_contains($mC[0]['body'], 'Самая крупная рыба: нет результата') && str_contains($mC[0]['body'], 'Заработок: 3 место из 3 — без награды'), 'C: bronze grubs', $mC);
ok(count($mD) === 0, 'no letter for D (no tier in any category)');
api('ping');
api('board', ['cat' => 'level']);
ok((int)dbv("SELECT COUNT(*) FROM mail WHERE kind = 'season'") === 3, 'closing happens once');
$j = api('mail', [], $A);
ok(($j['mail'][0]['kind'] ?? '') === 'season' && $j['mail'][0]['title'] === $title, 'season letter via the API');
$j = api('sync', ['stats' => ['fish_total' => 75]], $A);
ok($j['season']['id'] === $SEASON + 1 && $j['season']['my']['fish'] === 0 && $j['season']['participants'] === 0, 'new season starts from a fresh baseline', $j['season']);
$r = admin('GET', 'p=season&id=' . $SEASON);
ok(str_contains($r['body'], 'итоги подведены') && str_contains($r['body'], 'золото') && str_contains($r['body'], '1 из 4')
	&& str_contains($r['body'], 'Волшебная наживка'), 'admin season results page');
$r = admin('GET', 'p=seasons&lvl=3');
ok(str_contains($r['body'], 'Волшебная наживка ×5, Блесна ×10') && str_contains($r['body'], '3 вида × 7') && str_contains($r['body'], '150 опыта')
	&& str_contains($r['body'], '<td class="r">3</td>'), 'admin seasons page: rewards for level 3, awards count', substr(strip_tags($r['body']), 0, 800));
// a lone participant: gold in every category where he has a result
$j = api('sync', ['stats' => ['fish_total' => 80, 'coins_earned' => 9500], 'season_best' => ['fish' => 'fish_perch', 'size' => 30.5, 'q' => 1]], $A);
$my = $j['season']['my'] ?? [];
ok($j['season']['participants'] === 1 && [$my['rank_fish'], $my['tier_fish'], $my['tier_earned'], $my['tier_xp'], $my['rank_xp'], $my['tier_biggest'], $my['tier']]
	=== [1, 1, 1, 0, 0, 1, 1], 'lone participant: projected gold where he has a result', $j['season']);

section('restore code expiry');
$r = admin_post(['do' => 'make_code', 'id' => $B['pid'], 'save_id' => $latest, 'csrf' => $csrf]);
ok(str_contains($r['body'], 'Сохранение не найдено'), 'code only for the player\'s own saves');
$r = admin_post(['do' => 'make_code', 'id' => $A['pid'], 'save_id' => $latest, 'csrf' => $csrf]);
$CODE2 = preg_match('/class="code">([A-Z0-9]{8})</', $r['body'], $m) ? $m[1] : '';
set_offset($OFFSET + 8 * 86400);
expect_err(api('restore', ['code' => $CODE2]), 'bad_code', 'expired code');
// the jump also closed tournament S+1 with A as the only participant
$mA = $sm($A);
$wA1 = $want($A, $SEASON + 1, $lvl($A), ['s_fish' => 1, 's_earned' => 1, 's_biggest' => 1]);
ok(count($mA) === 2 && $mA[1]['title'] === 'Итоги турнира №' . ($SEASON + 1) && $letterIs($mA[1], $wA1) && $wA1['coins'] === 0
	&& $wA1['xp'] === max(150, intdiv(fv_level_span($lvl($A)) * 30 + 50, 100)) && count($wA1['items']) === 5
	&& str_contains($mA[1]['body'], 'Участников: 1') && str_contains($mA[1]['body'], 'Улов: 1 место из 1 — золото')
	&& str_contains($mA[1]['body'], 'Опыт: нет результата') && str_contains($mA[1]['body'], 'Награды в 3 категориях из 4'),
	'lone participant gets gold in fish / earned / biggest', [$mA[1] ?? $mA, $wA1]);
// ================================================================== update 6: server first, then the 0.6 client

section('update 6: XP migration, «опыт заработанный», стаж');
$E = register('Ветеран', ['X-Test-IP' => '10.6.6.6']);
expect_ok($E['j'], 'register E');
$j = api('sync', ['stats' => ['level' => 10, 'xp' => 25000, 'fish_total' => 900], 'ver' => '0.5'], $E);   // still the 0.5 client
ok($j['ok'] === true && $j['rejected'] === 0 && (int)dbv('SELECT xp_earned FROM players WHERE id = ?', [$E['pid']]) === 25000, '0.5 client above 10 000 XP', $j);
set_offset($OFFSET + 600);
$j = api('sync', ['stats' => ['level' => 10, 'xp' => 10000, 'max_level' => 15, 'xp_migration' => 1, 'xp_earned' => 0, 'xp_seniority' => 15200,
	'fish_total' => 905], 'ver' => '0.6'], $E);
$row = tdb()->query('SELECT * FROM players WHERE id = ' . $E['pid'])->fetch();
ok($j['rejected'] === 0 && (int)$row['xp'] === 10000 && (int)$row['level'] === 10 && (int)$row['xp_migrated'] === 1 && (int)$row['suspicious'] === 0
	&& (int)$row['xp_seniority'] === 15200 && (int)$row['xp_earned'] === 25000 && (int)$row['xp_earned_seen'] === 0, 'the 0.6 client trims XP once, not suspicious', [$j, $row]);
$sx = static fn() => (int)dbv('SELECT xp FROM season_stats WHERE player_id = ? ORDER BY season_id DESC LIMIT 1', [$E['pid']]);
$s0 = $sx();
set_offset($OFFSET + 600);
$j = api('sync', ['stats' => ['level' => 10, 'xp' => 10000, 'max_level' => 15, 'xp_migration' => 1, 'xp_earned' => 700, 'fish_total' => 910]], $E);
ok($j['rejected'] === 0 && (int)dbv('SELECT xp_earned FROM players WHERE id = ?', [$E['pid']]) === 25700 && $sx() === $s0 + 700,
	'frozen at level 10: the tournament «Опыт» counts the earned XP', [$j, $sx(), $s0]);
$j = api('sync', ['stats' => ['level' => 9, 'xp' => 9000, 'max_level' => 15, 'xp_migration' => 1]], $E);
ok($j['rejected'] === 1 && (int)dbv('SELECT xp FROM players WHERE id = ?', [$E['pid']]) === 10000, 'a second decrease is rejected', $j);
$j = api('sync', ['stats' => ['level' => 11, 'xp' => 13100, 'max_level' => 15, 'xp_earned' => 1000]], $E);
ok($j['rejected'] === 0 && (int)dbv('SELECT level FROM players WHERE id = ?', [$E['pid']]) === 11 && $sx() === $s0 + 1000, 'first arrival in the City: level 11', $j);
$pr = api('profile', ['id' => $E['pid']])['profile'] ?? [];
ok(($pr['level'] ?? 0) === 11 && ($pr['xp_earned'] ?? 0) === 26000 && ($pr['seniority'] ?? 0) === 15200, 'profile: level 11, earned XP, стаж', $pr);
$r = admin('GET', 'p=player&id=' . $E['pid']);
ok(str_contains($r['body'], 'Опыт заработанный') && str_contains($r['body'], '15 200') && str_contains($r['body'], 'срез принят')
	&& str_contains($r['body'], 'max="15"') && str_contains($r['body'], 'Обновление 6') && str_contains($r['body'], 'Твёрдая порода'), 'admin: earned XP, стаж, levels to 15, update-6 items');
// update 6 (admin icons): the progress card, the icon picker, fish in letters (the game's GiveItem puts them into the keepnet), no cosmetics
ok(str_contains($r['body'], 'Прибытие в Город') && str_contains($r['body'], '>прибыл<') && str_contains($r['body'], 'data-picker')
	&& str_contains($r['body'], 'value="elegend_dragon"') && str_contains($r['body'], 'admin.js'), 'admin: 0.6 progress card (level 11 = arrived in the City), item picker');
$csrfE = csrf_of($r['body']);
$r = admin_post(['do' => 'gift', 'id' => $E['pid'], 'item' => 'fish_carp', 'item_n' => 2, 'title' => 'Рыба', 'csrf' => $csrfE]);
ok(str_contains($r['body'], 'Подарок отправлен') && dbv('SELECT items_json FROM mail WHERE player_id = ? ORDER BY id DESC LIMIT 1', [$E['pid']]) === '[{"id":"fish_carp","n":2}]',
	'admin: a fish can be gifted');
$r = admin_post(['do' => 'gift', 'id' => $E['pid'], 'item' => 'pet_dragonet', 'item_n' => 1, 'csrf' => $csrfE]);
ok(str_contains($r['body'], 'Подарок пустой'), 'admin: a cosmetic is not a letter item');
$r = http('GET', '/');
// hotfix 0.6.1 (Р71, «сайт без спойлеров») replaced the fish / update-6 blocks with «Что ждёт в долине»
ok($r['status'] === 200 && str_contains($r['body'], 'Что ждёт в долине') && str_contains($r['body'], 'Что нового в версии') && str_contains($r['body'], 'fonts.googleapis.com'),
	'download page: the game-styled blocks');
$r = admin_post(['do' => 'logout', 'csrf' => csrf_of(admin('GET', '')['body'])]);
ok(str_contains($r['body'], 'Вход в админку'), 'logout');

// ================================================================== done

clearstatcache();
ok((is_file($uploadLog) ? [filesize($uploadLog), filemtime($uploadLog)] : null) === $uploadLogState, 'nothing logged into the upload folder');
$errLog = $LOCAL . '/test_logs/error_log.php';
if (is_file($errLog)) { $fail++; echo "server error log:\n" . file_get_contents($errLog); }
echo "\n" . ($fail === 0 ? "ALL PASSED" : "FAILED") . ": $pass passed, $fail failed\n";
proc_terminate($proc);
exit($fail === 0 ? 0 : 1);
