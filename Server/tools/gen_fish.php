<?php
// Regenerates Server/Fishing-game/lib/fish.php from the game sources (run after changing fish / items / the XP curve):
//   php -n Server/tools/gen_fish.php
// Classic data (0.5) is parsed from FVDatabase.cpp / FVDatabaseHome.cpp. Update 6 (Docs/UPDATE6_FOREST_CITY.md §17.3)
// adds locations, fish, items and levels 11–15: the U6_* tables below are the fallback until the DATA packages fill
// FVDatabaseU6*.cpp; whatever the generator can parse there (the same AddLocation / AddFish / AddItem / AddBait formats)
// wins, and every update-6 id found in those files but unknown here is reported (add it to the U6_* tables).
declare(strict_types=1);

// ------------------------------------------------------------------ update 6 fallback data (ids: §17.3; names: §3а, §8–§10)
const U6_LOCATIONS = ['city' => 'Город', 'cave' => 'Пещера', 'mill' => 'Старая мельница'];   // fishing locations (end of FVDB::Locations(); mill — §18.1)
const U6_PLACES = ['forest' => 'Лес'];                                // no water (like home / house)
/** id => [name, location, max size cm (with a margin: it is the anti-cheat limit), legendary]. */
const U6_FISH = [
	// §3а: one more ordinary species per classic location
	'fish_rudd' => ['Краснопёрка', 'pond', 40.0, false],
	'fish_asp' => ['Жерех', 'river', 100.0, false],
	'fish_whitefish' => ['Сиг', 'mountain', 80.0, false],
	'fish_flying_fish' => ['Летучая рыба', 'ocean', 50.0, false],
	'fish_bowfin' => ['Амия', 'swamp', 110.0, false],
	'fish_basalt_carp' => ['Базальтовый карп', 'volcano', 100.0, false],
	// §8, §9а: ordinary fish of the City pier and the Cave lake
	'fish_horse_mackerel' => ['Ставрида', 'city', 70.0, false],
	'fish_sea_burbot' => ['Морской налим', 'city', 80.0, false],
	'fish_blind_char' => ['Слепой голец', 'cave', 60.0, false],
	'fish_cave_gudgeon' => ['Пещерный пескарь', 'cave', 25.0, false],
	// §10.6: expert fish
	'efish_barracuda' => ['Барракуда', 'city', 200.0, false],
	'efish_king_mackerel' => ['Королевская скумбрия', 'city', 180.0, false],
	'efish_giant_bass' => ['Морской окунь-великан', 'city', 250.0, false],
	'efish_swordfish' => ['Меч-рыба', 'city', 500.0, false],
	'efish_stingray' => ['Скат-хвостокол', 'city', 300.0, false],
	'efish_sunfish' => ['Рыба-луна', 'city', 350.0, false],
	'efish_sailfish' => ['Парусник', 'city', 400.0, false],
	'efish_moray' => ['Мурена', 'city', 300.0, false],
	'efish_seadragon' => ['Морской дракон', 'city', 100.0, false],
	'efish_blind_catfish' => ['Слепой сом', 'cave', 250.0, false],
	'efish_cave_grayling' => ['Пещерный хариус', 'cave', 80.0, false],
	'efish_stone_char' => ['Каменный голец', 'cave', 120.0, false],
	'efish_crystal_eel' => ['Кристальный угорь', 'cave', 250.0, false],
	'efish_proteus' => ['Бледный протей', 'cave', 60.0, false],
	'efish_glow_perch' => ['Светящийся окунь', 'cave', 80.0, false],
	'efish_mica_carp' => ['Слюдяной карп', 'cave', 150.0, false],
	'efish_cave_sturgeon' => ['Пещерный осётр', 'cave', 450.0, false],
	// §10.8: expert legends
	'elegend_marlin' => ['Царь-марлин', 'city', 600.0, true],
	'elegend_harbor' => ['Хозяин гавани', 'city', 500.0, true],
	'elegend_ghostray' => ['Призрачный скат', 'city', 800.0, true],
	'elegend_patriarch' => ['Слепой патриарх', 'cave', 500.0, true],
	'elegend_giant' => ['Каменный великан', 'cave', 700.0, true],
	'elegend_dragon' => ['Хрустальный дракон', 'cave', 900.0, true],
	// §18, §18.1: the Old Mill (FVDatabaseU6Mill.cpp, parsed below — the game values win)
	'fish_ruffe' => ['Ёрш', 'mill', 18.0, false],
	'fish_ide' => ['Язь', 'mill', 60.0, false],
	'fish_rainbow_trout' => ['Радужная форель', 'mill', 60.0, false],
	'fish_burbot' => ['Налим', 'mill', 90.0, false],
	'fish_wild_carp' => ['Сазан', 'mill', 100.0, false],
	'fish_grass_carp' => ['Белый амур', 'mill', 120.0, false],
	'fish_sterlet' => ['Стерлядь', 'mill', 100.0, false],
	'fish_mill_master' => ['Хозяин омута', 'mill', 260.0, true],
];
/** Ids in FVDatabaseU6*.cpp that look like fish but are not (the Mill's fish pen object). */
const U6_NOT_FISH = ['fish_pen'];
/** Items an admin can send and tournaments can give (id => name). */
const U6_ITEMS = [
	'mat_log' => 'Бревно',
	'mat_hardrock' => 'Твёрдая порода',
	'mat_mpk' => 'МПК',
	'mat_nails' => 'Гвозди',
	'mat_boat_frame' => 'Каркас лодки',
	'seed_carrot' => 'Семена моркови',
	'seed_potato' => 'Семена картофеля',
	'seed_onion' => 'Семена лука',
	'seed_dill' => 'Семена укропа',
	'seed_pumpkin' => 'Семена тыквы',
	'veg_carrot' => 'Морковь',
	'veg_potato' => 'Картофель',
	'veg_onion' => 'Лук',
	'veg_dill' => 'Укроп',
	'veg_pumpkin' => 'Тыква',
	'food_draniki' => 'Драники',
	'food_fish_dill' => 'Рыба с укропом',
	'food_onion_soup' => 'Луковый суп',
	'food_pumpkin_porridge' => 'Тыквенная каша',
	'food_fish_pie' => 'Рыбник',
	'eline_thin' => 'Тонкая экспертная леска',
	'eline_heavy' => 'Тяжёлая экспертная леска',
	'efloat_surface' => 'Поверхностный поплавок',
	'efloat_deep' => 'Глубинный поплавок',
	'efloat_glow' => 'Светящийся экспертный поплавок',
	'reel_light' => 'Лёгкая катушка',
	'reel_heavy' => 'Тяжёлая катушка',
	'reel_light2' => 'Лёгкая катушка II',
	'reel_heavy2' => 'Тяжёлая катушка II',
	'bp_pickaxe' => 'Чертёж кирки',
	'bait_trash' => 'Мусорная приманка',
	'bait_deepworm' => 'Глубинный червь',
	'bait_marlin' => 'Приманка на марлина',
	'bait_harbor' => 'Приманка на хозяина гавани',
	'bait_giant' => 'Приманка на каменного великана',
	'bait_dragon' => 'Приманка на хрустального дракона',
	// §18.1: the Old Mill
	'seed_rye' => 'Семена ржи',
	'veg_rye' => 'Рожь',
	'mat_flour' => 'Ржаная мука',
	'food_bliny' => 'Блины',
	'food_rye_fish_pie' => 'Рыбный пирог',
	'bait_bread' => 'Хлебная прикормка',
	'bait_kolobok' => 'Мельничный колобок',
	'bait_reed_bundle' => 'Пучок тростника',
];
/** Cosmetics of update 6 the server knows by name (not giveable by letter: UFVSaveGame::GiveItem takes items / baits / fish). */
const U6_COSMETICS = [
	'bobber_millstone' => 'Жёрнов',   // §18.1: reward of ach_legend_mill
	// Р64: the boutique «Эксперт» collection (sold only after the expert achievement, FFVCosmeticDef::RequiredAch)
	'bobber_helm' => 'Штурвал',                      // ach_city_tasks_50
	'bobber_dragon' => 'Драконий глаз',              // ach_legend_volcano
	'hat_marlin_helm' => 'Шлем марлина',             // ach_elite_catch
	'hat_dragon_helm' => 'Драконий шлем',            // ach_legends
	'outfit_rodmaster' => 'Мундир мастера снастей',  // ach_all_rod_types
	'outfit_manta' => 'Плащ-скат',                   // ach_legends_50
	'pet_dragonet' => 'Дракончик Уголёк',            // ach_level_15
];
/** Total XP to reach levels 11..15 (§9, Р15) while FVDB::XpForLevel stops at 10. */
const U6_XP = [11 => 13100, 12 => 19300, 13 => 26700, 14 => 35500, 15 => 45900];
const ACHIEVEMENTS_CLASSIC = 51;   // achievements of 0.5
const ACHIEVEMENTS_U6 = 15;        // §13: Хозяин долины, Легендарный улов I–III, Элитный улов, Повелитель глубин, Иридиевая
                                   // легенда, Без единой ошибки, Мастер всех снастей, Лесоруб, Огородник, Кузнец, Капитан, Миллионер;
                                   // §18.1: Хозяин омута (ach_legend_mill). The total is max(this, the count parsed from the game).

// ------------------------------------------------------------------ sources
$root = dirname(__DIR__, 2);
$src = $root . '/Source/FishingValley/Private/';
$dbCpp = file_get_contents($src . 'FVDatabase.cpp');
$homeCpp = file_get_contents($src . 'FVDatabaseHome.cpp');
if ($dbCpp === false || $homeCpp === false) { fwrite(STDERR, "cannot read FVDatabase*.cpp\n"); exit(1); }
$u6Cpp = '';
foreach (glob($src . 'FVDatabaseU6*.cpp') ?: [] as $f) { $u6Cpp .= (string)file_get_contents($f) . "\n"; }
$millCpp = (string)@file_get_contents($src . 'FVDatabaseU6Mill.cpp');
$warnings = [];

// ---- locations
$reLoc = '/AddLocation\(D, TEXT\("([a-z0-9_]+)"\), TEXT\("([^"]+)"\)/u';
preg_match_all($reLoc, $dbCpp, $m, PREG_SET_ORDER);
$locations = [];
foreach ($m as $r) { $locations[$r[1]] = $r[2]; }
if (count($locations) !== 6) { fwrite(STDERR, 'expected 6 classic locations, parsed ' . count($locations) . "\n"); exit(1); }
$u6Locations = U6_LOCATIONS;
preg_match_all($reLoc, $u6Cpp, $m, PREG_SET_ORDER);
foreach ($m as $r) { $u6Locations[$r[1]] = $r[2]; }
$locations += $u6Locations;
$locations['home'] = 'Дом';
$locations['house'] = 'В доме';
$locations += U6_PLACES;
$u6LocIds = array_merge(array_keys($u6Locations), array_keys(U6_PLACES));

// ---- fish: AddFish(D, id, name, location, R::Rarity, price, difficulty, M::Motion, min, max, weight, ...
$re = '/AddFish\(D, TEXT\("([a-z0-9_]+)"\), TEXT\("([^"]+)"\), TEXT\("([a-z0-9_]+)"\), R::(\w+), (\d+), (\d+), M::\w+, ([\d.]+)f?, ([\d.]+)f?,/u';
preg_match_all($re, $dbCpp, $m, PREG_SET_ORDER);
$fish = [];
foreach ($m as $r) {
	$fish[$r[1]] = ['name' => $r[2], 'loc' => $r[3], 'legendary' => $r[4] === 'Legendary', 'max' => (float)$r[8], 'u6' => false];
}
if (count($fish) !== 40) { fwrite(STDERR, 'expected 40 classic fish, parsed ' . count($fish) . "\n"); exit(1); }
foreach (U6_FISH as $id => [$name, $loc, $max, $legend]) {
	$fish[$id] = ['name' => $name, 'loc' => $loc, 'legendary' => $legend, 'max' => $max, 'u6' => true];
}
preg_match_all($re, $u6Cpp, $m, PREG_SET_ORDER);
foreach ($m as $r) {
	$max = (float)$r[8];
	if (isset(U6_FISH[$r[1]]) && $max > U6_FISH[$r[1]][2]) { $warnings[] = "fish {$r[1]}: max $max cm in the game data (fallback " . U6_FISH[$r[1]][2] . ')'; }
	$fish[$r[1]] = ['name' => $r[2], 'loc' => $r[3], 'legendary' => $r[4] === 'Legendary', 'max' => $max, 'u6' => true];
}
// the Mill (§18.1): AddFish(Fish, id, name, R::Rarity, price, difficulty, M::Motion, min, max, ... — no location argument
$reMill = '/AddFish\(Fish, TEXT\("([a-z0-9_]+)"\), TEXT\("([^"]+)"\), R::(\w+), (\d+), (\d+), M::\w+, ([\d.]+)f?, ([\d.]+)f?,/u';
preg_match_all($reMill, $millCpp, $m, PREG_SET_ORDER);
foreach ($m as $r) {
	$max = (float)$r[7];
	if (isset(U6_FISH[$r[1]]) && $max > U6_FISH[$r[1]][2]) { $warnings[] = "fish {$r[1]}: max $max cm in the game data (fallback " . U6_FISH[$r[1]][2] . ')'; }
	$fish[$r[1]] = ['name' => $r[2], 'loc' => 'mill', 'legendary' => $r[3] === 'Legendary', 'max' => $max, 'u6' => true];
}
foreach ($fish as $id => $f) {
	if (!isset($locations[$f['loc']])) { fwrite(STDERR, "fish $id: unknown location {$f['loc']}\n"); exit(1); }
}

// ---- XP curve (FVDB::XpForLevel), extended to level 15 by U6_XP while the game table stops at 10
if (!preg_match('/int32 XpForLevel\(int32 Level\).*?Table\[\] = \{([^}]+)\}/s', $dbCpp, $xm)) { fwrite(STDERR, "XpForLevel table not found\n"); exit(1); }
$xp = array_map('intval', array_map('trim', explode(',', $xm[1])));
foreach (U6_XP as $level => $need) {
	if (!isset($xp[$level])) { $xp[$level] = $need; }
	elseif ($xp[$level] !== $need) { $warnings[] = "XP for level $level: {$xp[$level]} in the game, $need in U6_XP (the game wins)"; }
}
ksort($xp);
if (array_keys($xp) !== range(0, 15)) { fwrite(STDERR, "the XP table must cover levels 0..15\n"); exit(1); }

// ---- items the admin can attach to letters (GiveItem accepts items, baits, fish)
$items = [];
$reItem = '/(?:AddItem\(\w+, |AddLine\(|AddFloat\(|AddNet\()TEXT\("([a-z0-9_]+)"\), TEXT\("([^"]+)"\)/u';
$reBait = '/AddBait\(\w+, TEXT\("([a-z0-9_]+)"\), TEXT\("([^"]+)"\)/u';
preg_match_all($reItem, $homeCpp, $m, PREG_SET_ORDER);
foreach ($m as $r) { $items[$r[1]] = $r[2]; }
preg_match_all($reBait, $dbCpp, $m, PREG_SET_ORDER);
foreach ($m as $r) { $items[$r[1]] = $r[2]; }
$u6Items = U6_ITEMS;
foreach ([$reItem, $reBait] as $reU6) {
	preg_match_all($reU6, $u6Cpp, $m, PREG_SET_ORDER);
	foreach ($m as $r) { $u6Items[$r[1]] = $r[2]; }
}
$u6Items = array_diff_key($u6Items, $items);     // an id of 0.5 stays classic
$items += $u6Items;

// ---- update-6 ids mentioned in the game data but unknown here
preg_match_all('/TEXT\("((?:e?fish|elegend)_[a-z0-9_]+)"\)/', $u6Cpp, $m);
foreach (array_unique($m[1]) as $id) { if (!isset($fish[$id]) && !in_array($id, U6_NOT_FISH, true)) { $warnings[] = "fish id $id is in FVDatabaseU6*.cpp but not in U6_FISH"; } }
preg_match_all('/TEXT\("((?:mat|seed|veg|food|eline|efloat|reel|bp|bait)_[a-z0-9_]+)"\)/', $u6Cpp, $m);
foreach (array_unique($m[1]) as $id) { if (!isset($items[$id])) { $warnings[] = "item id $id is in FVDatabaseU6*.cpp but not in U6_ITEMS"; } }

// ---- cosmetics of update 6 (names from the game when it defines them as  C.Id = FName(TEXT("id")); C.Name = TEXT("..."))
$cosmetics = U6_COSMETICS;
preg_match_all('/C\.Id = FName\(TEXT\("([a-z0-9_]+)"\)\);\s*C\.Name = TEXT\("([^"]+)"\)/u', $u6Cpp, $m, PREG_SET_ORDER);
foreach ($m as $r) { if (isset($cosmetics[$r[1]])) { $cosmetics[$r[1]] = $r[2]; } }
// the CosRow(Cosmetics, TEXT("id"), TEXT("name"), ...) rows of FVDatabaseU6Content.cpp
preg_match_all('/CosRow\(Cosmetics, TEXT\("([a-z0-9_]+)"\), TEXT\("([^"]+)"\)/u', $u6Cpp, $m, PREG_SET_ORDER);
foreach ($m as $r) { if (isset($cosmetics[$r[1]])) { $cosmetics[$r[1]] = $r[2]; } }
foreach (array_keys($cosmetics) as $id) { if (!preg_match('/TEXT\("' . preg_quote($id, '/') . '"\)/', $u6Cpp)) { $warnings[] = "cosmetic $id is in U6_COSMETICS but not in FVDatabaseU6*.cpp"; } }

// ---- achievements: the game count (AddAch in FVDatabase.cpp, AchRow / A.Id in FVDatabaseU6*.cpp) vs the constants
$achIds = [];
preg_match_all('/AddAch\(D, TEXT\("(ach_[a-z0-9_]+)"\)/', $dbCpp, $m);
foreach ($m[1] as $id) { $achIds[$id] = true; }
preg_match_all('/(?:AchRow\(\w+, TEXT|A\.Id = FName\(TEXT)\("(ach_[a-z0-9_]+)"\)/', $u6Cpp, $m);
foreach ($m[1] as $id) { $achIds[$id] = true; }
$achTotal = ACHIEVEMENTS_CLASSIC + ACHIEVEMENTS_U6;
if (count($achIds) !== $achTotal) { $warnings[] = 'achievements: ' . count($achIds) . " parsed from the game, $achTotal in the constants (the larger one is used)"; }
$achTotal = max($achTotal, count($achIds));

// ---- every tournament reward (lib/season.php FV_SEASON_REWARD_ITEMS) must be a real game item
$seasonPhp = (string)file_get_contents($root . '/Server/Fishing-game/lib/season.php');
if (!preg_match('/const FV_SEASON_REWARD_ITEMS = \[(.*?)\];/s', $seasonPhp, $sm)) { fwrite(STDERR, "FV_SEASON_REWARD_ITEMS not found in season.php\n"); exit(1); }
preg_match_all("/'([a-z0-9_]+)'/", $sm[1], $rm);
$missing = array_values(array_diff($rm[1], array_keys($items)));
if ($missing) { fwrite(STDERR, 'tournament reward items missing in the game data: ' . implode(', ', $missing) . "\n"); exit(1); }

// ------------------------------------------------------------------ output
$e = static fn(string $s): string => var_export($s, true);
$ids = static fn(array $list): string => '[' . implode(', ', array_map($e, $list)) . ']';
$out = "<?php\n";
$out .= "// GENERATED by Server/tools/gen_fish.php from Source/FishingValley/Private/FVDatabase*.cpp (+ its update-6 tables).\n";
$out .= "// Do not edit by hand: change the game data (or the U6_* tables of the generator) and run  php -n Server/tools/gen_fish.php\n";
$out .= "if (!defined('FV_ROOT')) { http_response_code(403); exit; }\n\n";
$out .= "/** Locations (id => Russian name); home / house are the yard and the house interior, forest has no water. */\n";
$out .= "const FV_LOCATIONS = [\n";
foreach ($locations as $id => $name) { $out .= "\t" . $e($id) . ' => ' . $e($name) . ",\n"; }
$out .= "];\n\n";
$out .= "/** Locations of update 6 (the download page shows them once the update-6 client is out). */\n";
$out .= 'const FV_LOCATIONS_U6 = ' . $ids($u6LocIds) . ";\n\n";
$out .= "/** All " . count($fish) . " fish: id => name, max size (cm), location id, legendary, added by update 6. */\n";
$out .= "const FV_FISH = [\n";
foreach ($fish as $id => $f) {
	$out .= "\t" . $e($id) . ' => [\'name\' => ' . $e($f['name']) . ', \'max\' => ' . number_format($f['max'], 1, '.', '')
		. ', \'loc\' => ' . $e($f['loc']) . ', \'legendary\' => ' . ($f['legendary'] ? 'true' : 'false')
		. ', \'u6\' => ' . ($f['u6'] ? 'true' : 'false') . "],\n";
}
$out .= "];\n\n";
$out .= "/** Total XP needed to reach level L (index = L, FVDB::XpForLevel; 11–15 — update 6). */\n";
$out .= 'const FV_XP_TABLE = [' . implode(', ', $xp) . "];\n\n";
$out .= "/** Items an admin can attach to a letter and tournament rewards (id => name; GiveItem accepts all of them). */\n";
$out .= "const FV_ITEMS = [\n";
foreach ($items as $id => $name) { $out .= "\t" . $e($id) . ' => ' . $e($name) . ",\n"; }
$out .= "];\n\n";
$out .= "/** Items of update 6 (clients before 0.6 cannot store them). */\n";
$out .= 'const FV_ITEMS_U6 = ' . $ids(array_keys($u6Items)) . ";\n\n";
$out .= "/** Cosmetics of update 6 (id => name; not items — a letter cannot give them). */\n";
$out .= "const FV_COSMETICS_U6 = [\n";
foreach ($cosmetics as $id => $name) { $out .= "\t" . $e($id) . ' => ' . $e($name) . ",\n"; }
$out .= "];\n\n";
$out .= "/** Achievements in the game (0.5: " . ACHIEVEMENTS_CLASSIC . ", update 6: +" . ACHIEVEMENTS_U6 . "; or the parsed count if larger). */\n";
$out .= 'const FV_ACHIEVEMENTS_TOTAL = ' . $achTotal . ";\n";

$dst = $root . '/Server/Fishing-game/lib/fish.php';
file_put_contents($dst, $out);
foreach ($warnings as $w) { fwrite(STDERR, "warning: $w\n"); }
echo 'wrote ' . $dst . ': ' . count($locations) . ' locations, ' . count($fish) . ' fish, ' . count($items) . ' items ('
	. count($u6Items) . " of update 6), xp levels " . (count($xp) - 1) . "\n";
