<?php
// Fishing Valley — the download page (<site>/Fishing-game/): title, current version and changelog
// from the settings table (admin → «Версия игры»), a big «Скачать для Android» button and how to install an APK.
// Works without the database too (then the default version info is shown).
// Update 6: styled like the game UI (pixel font, wooden frames, parchment, the game's palette) and illustrated with the
// game's own art (img/, built by Tools/ExportArt.ps1): a real gameplay clip, screenshots, fish and location cards.
// Without the img folder the page still works (the picture blocks are simply left out).
declare(strict_types=1);

define('FV_ROOT', __DIR__);
require __DIR__ . '/lib/db.php';
require __DIR__ . '/lib/season.php';
require __DIR__ . '/lib/settings.php';
require __DIR__ . '/lib/art.php';

header('Content-Type: text/html; charset=utf-8');
header('Cache-Control: no-cache');
header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: strict-origin-when-cross-origin');
header("Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline' https://fonts.googleapis.com; font-src https://fonts.gstatic.com; "
	. "img-src 'self' data:; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");

$v = fv_version_info();                  // defaults when the database is not reachable
$updated = (int)fv_setting('version_updated');
$apk = fv_apk_local($v['download_url']);
$now = time();
try { $now = fv_now(); } catch (Throwable $e) { }
$season = fv_season_info($now);
$lines = fv_changelog_lines($v['changelog']);

function e(string $s): string
{
	return htmlspecialchars($s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function left_text(int $sec): string
{
	$sec = max(0, $sec);
	$d = intdiv($sec, 86400); $h = intdiv($sec % 86400, 3600); $m = intdiv($sec % 3600, 60);
	if ($d > 0) { return $d . ' ' . fv_plural($d, 'день', 'дня', 'дней') . ' ' . $h . ' ч'; }
	if ($h > 0) { return $h . ' ч ' . $m . ' мин'; }
	return max(1, $m) . ' мин';
}

/** A picture of img/ that really exists (art.json may list files the owner has not uploaded yet). */
function img_ok($rel): bool
{
	return is_string($rel) && preg_match('~^[a-z0-9_/-]+\.(png|webp|gif)$~', $rel) === 1 && is_file(FV_ROOT . '/img/' . $rel);
}

/** Game icon of a fish / item with its name as the accessible label and tooltip. */
function pic(string $id, string $name, int $size, string $cls = ''): string
{
	return '<span class="pic' . ($cls !== '' ? ' ' . $cls : '') . '" role="img" aria-label="' . e($name) . '" title="' . e($name) . '">'
		. fv_icon($id, '', $size) . '</span>';
}

$url = $v['download_url'];
$ready = $url !== '' && ($apk === null || $apk['exists']);
$meta = ['APK'];
if ($apk !== null && $apk['exists']) { $meta[] = fv_mb_text($apk['size']); }
$meta[] = 'Android 8.0 и новее';

// ---- the game's pictures (img/art.json)
$art = fv_art();
$gp = is_array($art['gameplay'] ?? null) && img_ok($art['gameplay']['webp'] ?? null) ? $art['gameplay'] : null;
// hotfix 27.09 (owner): no spoilers on the site — only neutral early-game screenshots, no fish, places or late content
$shotAllow = ['shots/main_menu.webp', 'shots/fishing_minigame.webp', 'shots/home_yard.webp', 'shots/home_interior.webp', 'shots/screen_wardrobe_full.webp'];
$shots = array_values(array_filter((array)($art['shots'] ?? []), static fn($s) => is_array($s) && in_array($s['img'] ?? '', $shotAllow, true) && img_ok($s['thumb'] ?? null) && img_ok($s['img'] ?? null)));
$heroChar = img_ok('sprites/char_down_fish_wait_0.png') && img_ok('sprites/char_down_fish_wait_1.png');
?><!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Клёвая долина — скачать игру для Android</title>
<meta name="description" content="Клёвая долина — уютная пиксельная игра про рыбалку: свой дом, ремёсла, турниры каждые три дня и много секретов долины.">
<meta property="og:title" content="Клёвая долина — скачать для Android">
<meta property="og:description" content="Уютная пиксельная рыбалка: свой дом, ремёсла и турниры каждые три дня. Версия <?= e($v['latest_name']) ?>.">
<meta property="og:type" content="website">
<meta name="theme-color" content="#5c2c16">
<link rel="icon" href="<?= img_ok('icons/fish_perch.png') ? 'img/icons/fish_perch.png' : "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Ccircle cx='16' cy='16' r='15' fill='%235fa8d3'/%3E%3C/svg%3E" ?>">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Pixelify+Sans:wght@500;700&amp;family=PT+Sans:wght@400;700&amp;display=swap">
<style>
:root {
	/* the game's UI palette (wooden frames, parchment panels, green buttons — sampled from the game's screens) */
	--wood-edge: #5c2c16; --wood: #a05424; --wood-light: #d68038; --wood-deep: #7a3a1a;
	--parch: #f9daa0; --parch-hi: #fff1cc; --parch-edge: #eec587; --parch-dark: #e2b878; --ink: #46200e; --ink-soft: #7a4a2a;
	--green: #5bbf45; --green-mid: #3f9a30; --green-dark: #1f4d17; --green-hi: #9be07a;
	--sky-top: #7cc4ef; --sky-bottom: #cfeefc; --grass: #5a9a3c; --grass-dark: #4a8430; --grass-light: #6cb048;
	--water: #3d8fc8; --water-light: #6fb8e6; --sand: #e9cf8e; --gold: #f2c230; --cream: #fff8e4; --tagline: #10324a;
	--pixel: "Pixelify Sans", "PT Sans", system-ui, sans-serif;
	--body: "PT Sans", system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
@media (prefers-color-scheme: dark) {
	:root:not([data-theme="light"]) { --sky-top: #0f1c38; --sky-bottom: #2d4a6e; --grass: #2f5a25; --grass-dark: #264a1e; --grass-light: #3a6b2e;
		--water: #1f5a86; --water-light: #3a7fb0; --sand: #b89a5e; --tagline: #e3f1fb; }
}
:root[data-theme="dark"] { --sky-top: #0f1c38; --sky-bottom: #2d4a6e; --grass: #2f5a25; --grass-dark: #264a1e; --grass-light: #3a6b2e;
	--water: #1f5a86; --water-light: #3a7fb0; --sand: #b89a5e; --tagline: #e3f1fb; }
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { margin: 0; color: var(--ink); font: 17px/1.5 var(--body); background-color: var(--grass);
	background-image: radial-gradient(var(--grass-light) 1.5px, transparent 2px), radial-gradient(var(--grass-dark) 1.5px, transparent 2px);
	background-size: 26px 26px, 34px 34px; background-position: 0 0, 13px 17px; }
img { max-width: 100%; }
img.ico, .px { image-rendering: pixelated; image-rendering: crisp-edges; }
/* the pixel font is used in capitals only: its lowercase Cyrillic is hard to read */
h1.logo, .plate, .btn, .ver b, .soon, footer b { text-transform: uppercase; letter-spacing: .03em; }
a { color: var(--wood-deep); }
.wrap { max-width: 1040px; margin: 0 auto; padding: 0 16px; }

/* ---- hero: sky, title, download panel, the phone with real gameplay */
.hero { background: linear-gradient(var(--sky-top), var(--sky-bottom)); padding: 26px 0 0; overflow: hidden; }
.hero::after { content: ""; display: block; height: 34px;
	background: linear-gradient(var(--sand) 0 10px, var(--wood-edge) 10px 13px, var(--water-light) 13px 19px, var(--water) 19px 100%); }
.hero-grid { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 22px 40px; align-items: end; }
.hero-text { padding-bottom: 30px; align-self: center; }
h1.logo { margin: 0; font: 700 clamp(40px, 9vw, 72px)/1 var(--pixel); color: var(--cream); letter-spacing: .01em;
	text-shadow: 3px 0 var(--wood-edge), -3px 0 var(--wood-edge), 0 3px var(--wood-edge), 0 -3px var(--wood-edge), 3px 3px var(--wood-edge),
		-3px 3px var(--wood-edge), 3px -3px var(--wood-edge), -3px -3px var(--wood-edge), 0 8px 0 var(--wood-deep); }
h1.logo span { color: var(--gold); }
.tagline { margin: 14px 0 34px; max-width: 520px; font-size: 19px; color: var(--tagline); font-weight: 700; }
.phone { position: relative; width: 272px; padding: 12px 10px 14px; background: #2b1a10; border-radius: 30px 30px 0 0;
	border: 3px solid #120a05; border-bottom: 0; box-shadow: inset 0 0 0 2px #5a3a26; }
.phone::before { content: ""; display: block; width: 56px; height: 6px; margin: 0 auto 9px; border-radius: 3px; background: #120a05; }
.phone picture, .phone picture img { display: block; width: 100%; height: auto; border-radius: 6px; background: #1a2a1a; }
.hero-char { position: absolute; left: -66px; bottom: 6px; width: 64px; height: 96px; }
.hero-char img { position: absolute; left: 0; top: 0; width: 64px; height: 96px; }
.hero-char img + img { animation: blink 1.2s steps(1) infinite; }
@keyframes blink { 50% { opacity: 0; } }

/* ---- wooden frame + parchment (the game's panels) */
.frame { position: relative; background: var(--parch); border: 5px solid var(--wood); border-color: var(--wood-light) var(--wood) var(--wood-deep) var(--wood);
	box-shadow: 0 0 0 3px var(--wood-edge), inset 0 0 0 3px var(--wood-edge), inset 0 0 0 6px var(--parch-edge), 0 9px 0 3px rgba(30, 15, 5, .28);
	padding: 22px 20px 20px; }
.plate { display: inline-block; position: relative; margin: -48px 0 12px -8px; padding: 6px 16px 7px; font: 700 20px/1.15 var(--pixel); color: var(--cream);
	background: var(--wood); border: 3px solid var(--wood-edge); box-shadow: inset 0 3px 0 var(--wood-light), inset 0 -4px 0 var(--wood-deep), 0 4px 0 rgba(30, 15, 5, .25);
	text-shadow: 2px 2px 0 var(--wood-edge); }
main section.frame { margin: 62px 0 0; }
h3 { font: 700 18px/1.2 var(--body); margin: 0 0 4px; color: var(--ink); }

/* ---- download panel */
.dl { max-width: 520px; text-align: center; padding-top: 18px; }
.ver { display: flex; align-items: baseline; justify-content: center; gap: 4px 10px; flex-wrap: wrap; font-size: 15px; color: var(--ink-soft); margin-bottom: 12px; }
.ver b { color: var(--ink); font: 700 22px var(--pixel); }
.btn { display: flex; align-items: center; justify-content: center; gap: 12px; width: 100%; min-height: 66px; padding: 12px 18px;
	font: 700 23px/1.1 var(--pixel); color: #fff; text-decoration: none; background: var(--green); border: 3px solid var(--green-dark);
	box-shadow: inset 0 -6px 0 var(--green-mid), inset 0 3px 0 var(--green-hi), 0 6px 0 var(--green-dark); text-shadow: 2px 2px 0 var(--green-dark);
	transition: transform .06s, box-shadow .06s; }
.btn:hover { filter: brightness(1.06); }
.btn:active { transform: translateY(5px); box-shadow: inset 0 -3px 0 var(--green-mid), inset 0 3px 0 var(--green-hi), 0 1px 0 var(--green-dark); }
.btn:focus-visible { outline: 4px solid var(--gold); outline-offset: 4px; }
.btn svg { flex: none; }
.btn.off { background: #b9ad97; border-color: #6d6352; box-shadow: inset 0 -6px 0 #9a8f7b, 0 6px 0 #6d6352; text-shadow: none; pointer-events: none; }
.meta { margin-top: 14px; font-size: 14px; color: var(--ink-soft); }
.season { margin: 12px 0 0; padding: 8px 10px; font-size: 15px; background: var(--parch-hi); border: 2px dashed var(--parch-dark); }
.season b { color: var(--wood-deep); }

/* ---- stats strip */
.stats { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
.stat { display: flex; align-items: center; gap: 10px; background: var(--parch-hi); border: 3px solid var(--parch-dark); padding: 10px; }
.stat b { display: block; font: 700 26px/1 var(--pixel); color: var(--wood-deep); }
.stat span { font-size: 14px; color: var(--ink-soft); line-height: 1.25; }

/* ---- lists */
ul.news { margin: 0; padding: 0; list-style: none; }
ul.news li { position: relative; padding: 7px 0 7px 30px; border-top: 2px dotted var(--parch-dark); }
ul.news li:first-child { border-top: 0; }
ul.news li::before { content: ""; position: absolute; left: 4px; top: 14px; width: 12px; height: 12px; background: var(--green);
	box-shadow: 0 0 0 2px var(--green-dark), inset -3px -3px 0 var(--green-mid); }
.u6 { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; }
.u6 > div { display: flex; gap: 12px; align-items: flex-start; background: var(--parch-hi); border: 3px solid var(--parch-dark); padding: 10px 12px; }
.u6 p { margin: 0; font-size: 15px; color: var(--ink-soft); }
.u6 .pic { flex: none; width: 52px; min-height: 48px; }
.soon { display: inline-block; margin: 0 0 12px; padding: 3px 10px; font: 700 15px var(--pixel); color: var(--cream); background: var(--water); border: 2px solid #1d4a6b; }

/* ---- gallery */
.gallery { display: flex; gap: 14px; overflow-x: auto; scroll-snap-type: x mandatory; padding: 4px 4px 14px; margin: 0 -4px; -webkit-overflow-scrolling: touch; }
.gallery figure { flex: none; width: 190px; margin: 0; scroll-snap-align: start; }
.gallery a { display: block; border: 4px solid var(--wood-edge); background: var(--wood-edge); box-shadow: 0 5px 0 rgba(30, 15, 5, .25); }
.gallery img { display: block; width: 100%; height: auto; }
.gallery figcaption { margin-top: 8px; font-size: 14px; line-height: 1.3; color: var(--ink-soft); }
.hint { margin: 2px 0 0; font-size: 14px; color: var(--ink-soft); }

/* ---- places and fish */
.places { display: grid; grid-template-columns: repeat(auto-fill, minmax(290px, 1fr)); gap: 18px; }
.place { background: var(--parch-hi); border: 3px solid var(--wood-edge); box-shadow: 0 5px 0 rgba(30, 15, 5, .2); }
.place img.shot { display: block; width: 100%; height: auto; border-bottom: 3px solid var(--wood-edge); }
.place-b { padding: 10px 12px 12px; }
.place h3 { display: flex; justify-content: space-between; gap: 8px; align-items: baseline; }
.place h3 small { font: 400 13px var(--body); color: var(--ink-soft); white-space: nowrap; }
.place p { margin: 0 0 8px; font-size: 15px; color: var(--ink-soft); }
.fishes { display: flex; flex-wrap: wrap; gap: 4px; }
.pic { display: inline-flex; align-items: center; justify-content: center; }
.fishes .pic { width: 44px; height: 38px; background: #fff; border: 2px solid var(--parch-dark); }
.fishes .pic.leg { border-color: var(--gold); background: #fff6d0; box-shadow: 0 0 0 1px #b8860b; }
.ico-none { display: inline-flex; align-items: center; justify-content: center; width: 24px; height: 24px; color: var(--ink-soft); font-size: 12px; }
.legends { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 12px; }
.legend { text-align: center; background: linear-gradient(#fff6d0, var(--parch-hi)); border: 3px solid #b8860b; box-shadow: inset 0 0 0 2px #ffe27a; padding: 10px 8px; }
.legend .pic { height: 84px; }
.legend b { display: block; font: 700 16px/1.15 var(--body); color: var(--ink); }
.legend span { font-size: 13px; color: var(--ink-soft); }

/* ---- install steps */
ol.steps { margin: 0; padding: 0; list-style: none; counter-reset: s; }
ol.steps li { counter-increment: s; position: relative; padding: 5px 0 12px 48px; min-height: 40px; }
ol.steps li::before { content: counter(s); position: absolute; left: 0; top: 2px; width: 34px; height: 34px; display: flex; align-items: center; justify-content: center;
	font: 700 20px var(--pixel); color: var(--cream); background: var(--wood); border: 3px solid var(--wood-edge); box-shadow: inset 0 3px 0 var(--wood-light); }
.note { margin: 6px 0 0; padding: 10px 12px; background: #d9eef9; border: 2px solid #8cc3e3; font-size: 15px; }

footer { margin-top: 60px; padding: 22px 16px 30px; text-align: center; color: var(--cream); font-size: 14px; background: var(--wood-edge);
	border-top: 5px solid var(--wood); box-shadow: inset 0 3px 0 var(--wood-light); }
footer b { font: 700 18px var(--pixel); }

@media (max-width: 860px) {
	.hero-grid { grid-template-columns: 1fr; justify-items: center; text-align: center; }
	.hero-text { padding-bottom: 0; }
	.tagline { margin-left: auto; margin-right: auto; }
	.dl { margin: 0 auto; }
	.phone { width: 236px; }
	.hero-char { left: -52px; width: 48px; height: 72px; }
	.hero-char img { width: 48px; height: 72px; }
	.stats { grid-template-columns: repeat(2, minmax(0, 1fr)); }
	.u6 { grid-template-columns: 1fr; }
}
@media (max-width: 420px) {
	body { font-size: 16px; }
	.frame { padding: 20px 14px 16px; }
	.plate { font-size: 17px; }
	.btn { font-size: 19px; gap: 8px; }
	.gallery figure { width: 160px; }
	.hero-char { display: none; }
	.stat { flex-direction: column; text-align: center; }
	.stat b { font-size: 22px; }
}
@media (prefers-reduced-motion: reduce) { .hero-char img + img { animation: none; opacity: 0; } }
</style>
</head>
<body>
<header class="hero">
	<div class="wrap hero-grid">
		<div class="hero-text">
			<h1 class="logo">Клёвая <span>долина</span></h1>
			<p class="tagline">Уютная пиксельная рыбалка: пруды и реки, свой дом во дворе, ремёсла и турниры с другими рыбаками.</p>
			<section class="frame dl" aria-label="Скачать игру">
				<div class="ver"><b>Версия <?= e($v['latest_name']) ?></b><?php if ($updated > 0): ?><span>обновлено <?= e(date('d.m.Y', $updated)) ?></span><?php endif; ?></div>
				<?php if ($ready): ?>
				<a class="btn" href="<?= e($url) ?>" download>
					<svg width="30" height="30" viewBox="0 0 24 24" aria-hidden="true"><path fill="#fff" d="M17.6 9.48l1.84-3.18a.38.38 0 00-.66-.38l-1.87 3.23a11.5 11.5 0 00-9.82 0L5.22 5.92a.38.38 0 00-.66.38L6.4 9.48A10.8 10.8 0 001 18h22a10.8 10.8 0 00-5.4-8.52zM7 15.25a1.25 1.25 0 110-2.5 1.25 1.25 0 010 2.5zm10 0a1.25 1.25 0 110-2.5 1.25 1.25 0 010 2.5z"/></svg>
					Скачать для Android
				</a>
				<div class="meta"><?= e(implode(' · ', $meta)) ?></div>
				<?php else: ?>
				<span class="btn off">Скачать для Android</span>
				<div class="meta">Файл новой версии загружается — загляните чуть позже.</div>
				<?php endif; ?>
				<?php if ($season['id'] >= 0): ?>
				<p class="season">Сейчас идёт <b><?= e(fv_season_name($season['id'])) ?></b> — до финиша <?= e(left_text($season['ends'] - $now)) ?></p>
				<?php endif; ?>
			</section>
		</div>
		<?php if ($gp): ?>
		<div class="phone">
			<?php if ($heroChar): ?><span class="hero-char" aria-hidden="true"><img class="px" src="img/sprites/char_down_fish_wait_0.png" alt=""><img class="px" src="img/sprites/char_down_fish_wait_1.png" alt=""></span><?php endif; ?>
			<picture>
				<source type="image/webp" srcset="img/<?= e($gp['webp']) ?>">
				<img src="img/<?= e(img_ok($gp['gif'] ?? null) ? $gp['gif'] : $gp['webp']) ?>" width="<?= (int)$gp['w'] ?>" height="<?= (int)$gp['h'] ?>" alt="Настоящая игра: заброс, поклёвка, вываживание и улов">
			</picture>
		</div>
		<?php endif; ?>
	</div>
</header>
<main class="wrap">
	<section class="frame">
		<h2 class="plate">Что ждёт в долине</h2>
		<div class="stats">
			<div class="stat"><?= fv_icon('ui_map', '', 40) ?><div><b>Долина</b><span>водоёмы, леса и места, которые ещё предстоит открыть</span></div></div>
			<div class="stat"><?= fv_icon('ui_fish', '', 40) ?><div><b>Рыбы</b><span>от мелочи у берега до тех, о ком ходят легенды</span></div></div>
			<div class="stat"><?= fv_icon('ui_star_gold', '', 40) ?><div><b>Ремёсла</b><span>дом, огород, мастерская и кухня</span></div></div>
			<div class="stat"><?= fv_icon('ui_trophy', '', 40) ?><div><b>3 дня</b><span>турнир с наградами в четырёх категориях</span></div></div>
		</div>
	</section>

	<?php if ($lines): ?>
	<section class="frame">
		<h2 class="plate">Что нового в версии <?= e($v['latest_name']) ?></h2>
		<ul class="news">
			<?php foreach ($lines as $line): ?><li><?= e($line) ?></li>
			<?php endforeach; ?>
		</ul>
	</section>
	<?php endif; ?>

	<?php if ($shots): ?>
	<section class="frame">
		<h2 class="plate">Скриншоты</h2>
		<div class="gallery">
			<?php foreach ($shots as $s): ?>
			<figure><a href="img/<?= e($s['img']) ?>"><img src="img/<?= e($s['thumb']) ?>" width="<?= (int)$s['tw'] ?>" height="<?= (int)$s['th'] ?>" alt="<?= e($s['caption']) ?>" loading="lazy" decoding="async"></a>
				<figcaption><?= e($s['caption']) ?></figcaption></figure>
			<?php endforeach; ?>
		</div>
		<p class="hint">Листайте вбок · нажмите на снимок, чтобы открыть его целиком</p>
	</section>
	<?php endif; ?>

	<section class="frame">
		<h2 class="plate">Как установить</h2>
		<ol class="steps">
			<li>Нажмите «Скачать для Android» и дождитесь, пока загрузится файл <b><?= e($apk['name'] ?? 'KlevayaDolina.apk') ?></b>.</li>
			<li>Откройте его из уведомления о загрузке или из папки «Загрузки» (Download).</li>
			<li>Если телефон спросит, разрешите браузеру устанавливать приложения: «Настройки» → «Установка неизвестных приложений» → ваш браузер → «Разрешить».</li>
			<li>Нажмите «Установить», а затем «Открыть». Приятного клёва!</li>
		</ol>
		<p class="note">Новую версию ставьте поверх старой — удалять игру не нужно, прогресс останется. Если Play Защита предупредит о неизвестном приложении, нажмите «Подробнее» → «Всё равно установить».</p>
	</section>
</main>
<footer><b>Клёвая долина</b><br>игра для Android · kostyukhinboris.ru</footer>
</body>
</html>
