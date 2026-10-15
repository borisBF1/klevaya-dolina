# Update 2 — «Дом, ремёсла и торговля» (design + engineering contract)

Project root (ALWAYS this ASCII path): `C:\UEProjects\FishingValley` (junction to `<папка проекта>`).
Engine: UE 5.8 at `C:\Program Files\Epic Games\UE_5.8`. Everything is C++ drawn on UCanvas (see Docs/DESIGN.md). All player text is Russian.
Code maps of the existing code (read the ones for your area instead of re-reading big files):
`Docs/CodeMap/{scene,core,fishing,data,screens,artaudiotest}.md`.

The public headers already contain the full contract for this update (look for "UPDATE 2"):
`Public/FVTypes.h` (new enums/structs at the end + additive fields), `Public/FVDatabase.h`, `Public/FVSaveGame.h`,
`Public/FVGame.h`, `Public/FVScene.h`, `Public/FVScreens.h`, `Public/FVSprites.h`. Read them first.

---------------------------------------------------------------------------------------------------------------------
## 0. Rules for every implementer
1. **File ownership** (§12). Edit only your files. Contract headers are frozen: you may ADD private members/helpers to a
   header you own; never rename/remove/change existing declarations. If you need something from another module that
   the contract lacks, work around it locally and list it under "Requests" in your final report.
2. **Compile your files** (no link): `cmd //c "C:\UEProjects\FishingValley\Tools\CompileFile.bat Source\FishingValley\Private\<File>.cpp" 2>&1 | grep -E "error|warning C|Result"`
   (UBT runs one build at a time: a compile may wait — normal). Fix every error and every warning in your files.
   Do NOT run the full build / the game — the integrator links and tests everything.
3. **Namespaces**: helpers of every .cpp go into a unique named namespace (`namespace FVStationScreenImpl {…}`), never
   rely on names being file-local across files.
4. **Both orientations**: landscape 640x360 and portrait 360 x (600..900) (`FV::IsPortrait()`, `FV::VW/VH`,
   `FV::TopBarH/BottomBarH` read every frame, `FV::WindowRect()`, `FV::Ui::*` sizes, `FVDraw::ActionBar` footer).
   Touch (`FV::IsTouchUI()`): no hover-only information in portrait, big targets, lists scroll with finger drags
   (`In.Wheel` steps), select on release (tap tracker). Mouse: tooltips OK in landscape.
5. **Robustness**: null-check every FVDB lookup, clamp indices, never crash on empty data, never block the game thread.
6. **Save safety**: never persist pointers or screen positions; ids are stable strings (never rename an id).
7. **Never emulate OS input** (SendKeys, mouse_event): the user works on this PC.
8. **Tokens**: read only what you need (grep + targeted reads). Final report ≤ 250 words: files, what works, known gaps,
   Requests.

---------------------------------------------------------------------------------------------------------------------
## 1. Overview of the new gameplay
* **Items**: resources gathered on locations (walk up + click/tap/E), materials made at the workshop, food with fishing
  buffs, rod tackle (line + float), nets, a shovel. Junk now goes to the **backpack** (not the keepnet).
* **Inventory**: Садок (fish only, v1 keepnet) + Рюкзак (everything else, slot-based) + bait (Baits map, unchanged).
* **Trade only at stalls**: «Лавка рыбака» (Дед Митрич) at the pond, «Лавка на причале» (Боцман Семёныч) at the ocean
  pier (better sell rates, late goods). The global shop button / B-key / wardrobe link are removed. Selling happens
  in the shop's «Продать» tab (Inventory no longer sells).
* **Home**: new map destination «Дом» = yard (house exterior with a door + garden: resources, 3 dig spots) and the
  house interior (all stations, chest, bed, decor). House decor = cosmetics for the house (coins or achievements).
* **Stations** built/upgraded for coins + materials: Кухня, Мастерская, Коптильня, Червятник, Аквариум, Ледник, Радио,
  Кровать (built), Стена трофеев, Сундук (built).
* **Mini-games**: cooking (cut with swipes + flip in time), lure workshop (combine parts, discover recipes), knots
  («повтори узор» → lines & nets), digging worms (timed, shovel level = more time), smokehouse fuel management,
  resource gathering (simple pickup), order board (deposit / deadline / reward).
* **Nets**: fixed net spots at the shore; nets catch fish over game time (also while sleeping), wear out, tear, need
  repair; bigger/stronger nets catch more / last longer; better fish wear the net more.
* **Rod slots**: Леска + Поплавок (installed at the workshop, tier ≤ rod tier + 1) + Наживка (the equipped bait).
* **Time**: absolute game minutes (`UFVGame::GetAbsMinutes`). Sleeping skips to 06:00 and simulates everything.

---------------------------------------------------------------------------------------------------------------------
## 2. Items (FVDB::Items(), FFVItemDef). Ids are final.
Columns: id · Name · Art · SellPrice · BuyPrice@trader(mask) · MinLevel · extra.
Resources (Kind Resource, MaxStack 99, not sold by traders):
| id | Name | Art | Sell |
|---|---|---|---|
| res_branch | Ветка | Branch | 2 |
| res_pebble | Камешек | Pebble | 2 |
| res_fiber | Волокно | Fiber | 3 |
| res_feather | Перо | Feather | 5 |
| res_mushroom | Гриб | Mushroom | 8 |
| res_berries | Ягоды | Berries | 6 |
| res_reed | Тростник | Reed | 3 |
| res_clay | Глина | Clay | 4 |
| res_resin | Смола | Resin | 6 |

Materials (Kind Material, MaxStack 99):
| id | Name | Art | Sell | Buy |
|---|---|---|---|---|
| mat_board | Доска | Board | 12 | 40 (Mitrich) |
| mat_rope | Верёвка | Rope | 14 | 45 (Mitrich) |
| mat_glue | Клей | Glue | 18 | 60 (Mitrich) |
| mat_scrap | Металлолом | Scrap | 8 | — |
| mat_sinker | Грузило | Sinker | 6 | 20 (Mitrich) |
| mat_glitter | Блёстки | Glitter | 15 | 50 (Pier) |

Food (Kind Food, MaxStack 20; quality = dish quality; effect & duration x FVDB::FoodPower(Q) = .8/1/1.2/1.4):
| id | Name | Art | Sell | Buff | Minutes |
|---|---|---|---|---|---|
| food_fried | Жареная рыба | FriedFish | 35 | Reel x1.10, Drain x0.92 | 180 |
| food_tea | Травяной чай | HerbTea | 20 | Bite x0.85, React +0.15 s | 240 |
| food_soup | Уха | FishSoup | 60 | Bar +16, Drain x0.94 | 240 |
| food_pie | Ягодный пирог | BerryPie | 70 | Luck +0.25, Treasure +0.04, Size +0.05 | 300 |
Only one food buff at a time (eating replaces it).

Lines (Kind Line, Art Line, MaxStack 5; Tint colours the spool):
| id | Name | Tier | Tint | Sell | Buy | MinLvl | Tackle |
|---|---|---|---|---|---|---|---|
| line_linen | Льняная леска | 1 | 214,196,150 | 40 | 150 M | 1 | Reel x1.04, Drain x0.97 |
| line_braided | Плетёная леска | 2 | 90,140,80 | 250 | 900 M | 3 | Reel x1.08, Drain x0.94, Bar +4 |
| line_silk | Шёлковая леска | 3 | 240,236,220 | 600 | 2500 M | 5 | Reel x1.12, Drain x0.90, Treasure +0.02 |
| line_fluoro | Флюорокарбоновая леска | 4 | 150,210,235 | 1500 | 6000 P | 7 | Reel x1.15, Drain x0.86, Bite x0.95 |
| line_titan | Титановая струна | 5 | 170,170,190 | 4500 | 18000 P | 9 | Reel x1.20, Drain x0.80, Bar +8 |

Floats (Kind Float, MaxStack 5):
| id | Name | Art | Tier | Sell | Buy | MinLvl | Tackle |
|---|---|---|---|---|---|---|---|
| float_feather | Поплавок из пера | FloatFeather | 1 | 50 | 200 M | 1 | Bite x0.94, React +0.10 |
| float_reed | Тростниковый поплавок | FloatReed | 1 | 40 | — | 1 | Cast +0.05, Bite x0.97 |
| float_sliding | Скользящий поплавок | FloatSliding | 2 | 300 | 1200 M | 3 | Cast +0.08, Luck +0.06 |
| float_glow | Светящийся поплавок | FloatGlow | 3 | 900 | 3500 P | 5 | React +0.18, Treasure +0.03 |
| float_sport | Спортивный поплавок | FloatSport | 4 | 1800 | 7500 P | 7 | Bite x0.85, React +0.15 |
| float_gold | Золотой поплавок | FloatGold | 5 | 5000 | 20000 P | 9 | Luck +0.18, Treasure +0.05 |

Nets (Kind Net, Art Net, MaxStack 3): cap = fish before it must be emptied, dur = max durability, min/fish.
| id | Name | Tier | Tint | Cap | Dur | Min/fish | Sell | Buy | MinLvl |
|---|---|---|---|---|---|---|---|---|---|
| net_small | Малая сеть | 1 | 200,180,130 | 4 | 100 | 120 | 150 | 600 M | 2 |
| net_medium | Средняя сеть | 2 | 150,170,110 | 8 | 160 | 90 | 500 | 2000 M | 4 |
| net_strong | Прочная сеть | 2 | 160,110,80 | 8 | 400 | 90 | 1200 | 5000 P | 5 |
| net_large | Большая сеть | 3 | 110,150,170 | 14 | 220 | 70 | 1500 | 6000 P | 6 |
M = Mitrich (mask 1), P = pier trader (mask 2).

Existing tables changes:
* Junk: `junk_can` display name → «Консервная банка» (id unchanged). Junk goes to the backpack now.
* Rods TraderMask: bamboo 0, fiberglass/carbon/titanium 1 (Mitrich), iridium/poseidon 2 (pier).
* Baits TraderMask: worm 1, grub 1, lure 2, magic 2. Append (after bait_magic, keep bait_worm first!):
  | id | Name | Style | Bite | Rare | Junk | Size | Source |
  |---|---|---|---|---|---|---|---|
  | bait_nightcrawler | Выползок | Nightcrawler | .80 | .15 | .90 | .12 | digging, worm farm L2 |
  | bait_dough | Тесто | Dough | .70 | 0 | 1.10 | 0 | kitchen; affinity Carp+Minnow shapes, mult 1.0 |
  | bait_groundbait | Прикормка | Groundbait | .45 | .05 | 1.0 | .05 | kitchen L2 |
  PackPrice 0, TraderMask 0, PackSize 5 (display). Filter PackPrice==0 baits out of RollTreasure and shops.
* **Lures** (24 bait defs appended after those): id `lure_<b>_<c>_<g>` b∈{0 spoon,1 fly}, c∈{0 red,1 yellow,2 green,
  3 black}, g∈{0,1,2}. `bLure=true`, Style Lure (spoon) / Fly, PackPrice 0, TraderMask 0, PackSize 5.
  Name: «<Color> <base>[ с серебром| с золотом]»: Красная/Жёлтая/Зелёная/Чёрная + блесна/мушка
  (e.g. «Зелёная мушка с серебром»). Stats: spoon Bite 1.0, Rare .15, Junk .5, Size .10; fly Bite .85, Rare .10,
  Junk .7, Size .05; glitter silver +.15 Rare +.05 Size, gold +.30 Rare +.10 Size.
  Affinity: base spoon → motions Mixed|Smooth, fly → Dart|Floater; colour → shapes: red Perch|Pike|Tuna,
  yellow Carp|Flounder|Puffer, green Minnow|Eel|Crayfish, black Catfish|Squid|Legend. AffinityMult 1.5
  (weight x (1 + 1.5 x matches), matches 0..2). Inputs (`LureInputs`): spoon mat_scrap x1 | fly res_feather x1 +
  res_fiber x1; colour red res_berries x1 | yellow res_resin x1 | green res_reed x1 | black res_clay x1;
  glitter silver mat_glitter x1 | gold mat_glitter x2. LurePackSize 5.

---------------------------------------------------------------------------------------------------------------------
## 3. Recipes (FVDB::Recipes(), display order = table order)
Workshop (Мастерская):
| id | Lvl | MinLvl | Inputs | Output | Game |
|---|---|---|---|---|---|
| rcp_board | 1 | 1 | res_branch x3 | mat_board x1 | — |
| rcp_rope | 1 | 1 | res_fiber x3 | mat_rope x1 | — |
| rcp_glue | 1 | 1 | res_resin x2 | mat_glue x1 | — |
| rcp_sinker | 1 | 1 | res_pebble x2 | mat_sinker x2 | — |
| rcp_float_feather | 1 | 1 | res_feather x2, mat_glue x1 | float_feather | — |
| rcp_float_reed | 1 | 1 | res_reed x2, mat_glue x1 | float_reed | — |
| rcp_line_linen | 1 | 1 | res_fiber x4 | line_linen | Knots 4 |
| rcp_rec_can (recycle) | 1 | 1 | junk_can x2 | mat_scrap x1 | — |
| rcp_rec_boot (recycle) | 1 | 1 | junk_boot x1 | mat_scrap x1 | — |
| rcp_rec_weed (recycle) | 1 | 1 | junk_weed x3 | res_fiber x1 | — |
| rcp_rec_wood (recycle) | 1 | 1 | junk_driftwood x1 | mat_board x1 | — |
| rcp_rec_bottle (recycle) | 2 | 1 | junk_bottle x1 | mat_glitter x1 | — |
| rcp_sinker_metal | 2 | 1 | mat_scrap x1 | mat_sinker x3 | — |
| rcp_float_sliding | 2 | 3 | float_feather x1, mat_sinker x1, mat_glue x1 | float_sliding | — |
| rcp_line_braided | 2 | 3 | mat_rope x1, res_fiber x3, res_resin x1 | line_braided | Knots 5 |
| rcp_net_small | 2 | 2 | mat_rope x2, res_fiber x4, mat_sinker x2 | net_small | Knots 5 |
| rcp_line_silk | 3 | 5 | mat_rope x2, res_fiber x5, mat_glue x1 | line_silk | Knots 6 |
| rcp_float_glow | 3 | 5 | res_resin x2, mat_glitter x1, mat_glue x1 | float_glow | — |
| rcp_net_medium | 3 | 4 | mat_rope x4, res_fiber x6, mat_sinker x4 | net_medium | Knots 6 |
| rcp_net_large | 3 | 6 | mat_rope x6, res_fiber x10, mat_sinker x6, mat_glue x2 | net_large | Knots 7 |
Also at the workshop: **Мастерская блёсен** (level 2, LureCraft screen), **Оснастка** (install line/float, level 1),
shovel crafting (`FVDB::Shovels()`).

Kitchen (Кухня) — `any_fish` = the cheapest non-smoked fish of the keepnet:
| id | Lvl | MinLvl | Inputs | Output | Game |
|---|---|---|---|---|---|
| rcp_fried | 1 | 1 | any_fish x1 | food_fried | Cooking (2 cuts) |
| rcp_tea | 1 | 1 | res_berries x1, res_fiber x2 | food_tea | Cooking (0 cuts: brew) |
| rcp_dough | 1 | 1 | res_reed x2 | bait_dough x5 | — |
| rcp_soup | 2 | 1 | any_fish x2, res_mushroom x1, res_reed x1 | food_soup | Cooking (3 cuts) |
| rcp_groundbait | 2 | 1 | res_clay x1, res_berries x1, bait_dough x2 | bait_groundbait x5 | — |
| rcp_pie | 3 | 1 | res_berries x4, res_reed x2 | food_pie | Cooking (0 cuts: bake) |
Crafting stats: Stats.Crafted += output units for workshop recipes, Stats.Cooked += 1 per dish, Stats.KnotsTied += 1
per successful knots recipe (the mini-game calls Craft on success only; failure costs nothing).

---------------------------------------------------------------------------------------------------------------------
## 4. Stations (FVDB::Stations(), index = EFVStation)
| Station (id) | Level: Name — coins; materials; min fishing lvl; capacity — gives |
|---|---|
| Kitchen st_kitchen «Кухня» | 1 Очаг — 250; res_pebble x10, res_clay x5; L1 — жареная рыба, чай, тесто · 2 Плита — 1500; mat_scrap x6, res_clay x6, mat_board x2; L3 — уха, прикормка · 3 Печь — 5000; res_pebble x20, res_clay x12, mat_scrap x10, mat_board x4; L6 — ягодный пирог |
| Workshop st_workshop «Мастерская» | 1 Стол — 150; res_branch x8, res_pebble x4; L1 · 2 Верстак — 1200; mat_board x6, mat_rope x3, mat_scrap x3; L2 · 3 Станок — 4500; mat_board x12, mat_scrap x12, mat_glue x4; L5 |
| Smokehouse st_smokehouse «Коптильня» | 1 — 800; res_pebble x12, res_clay x8, mat_board x3; L2; cap 4 · 2 — 3000; mat_scrap x8, mat_board x6, res_clay x10; L5; cap 8 |
| WormFarm st_wormfarm «Червятник» | 1 — 300; mat_board x3, res_clay x3; L1; cap 12 · 2 — 1500; mat_board x6, res_clay x6, res_mushroom x3; L4; cap 20 |
| Aquarium st_aquarium «Аквариум» | 1 Банка — 200; junk_bottle x2, mat_glue x1; L1; cap 1 · 2 Аквариум — 2000; junk_bottle x4, mat_board x4, mat_glue x3; L3; cap 4 · 3 Большой аквариум — 7000; junk_bottle x8, mat_board x8, mat_glue x6, mat_scrap x6; L6; cap 10 |
| IceCellar st_icecellar «Ледник» | 1 — 600; res_pebble x15, res_clay x6; L2; cap 10 · 2 — 2500; res_pebble x25, res_clay x10, mat_board x6; L4; cap 25 |
| Radio st_radio «Радио» | 1 — 1200; mat_scrap x6, mat_rope x2, mat_board x2; L3 |
| Bed st_bed «Кровать» | 1 (starts built) |
| TrophyWall st_trophy «Стена трофеев» | 1 — 500; mat_board x6, mat_glue x2; L2; cap 6 |
| Chest st_chest «Сундук» | 1 (starts built); cap 40 slots |

* Smokehouse: fish MinutesLeft 360 (6 game hours), progress only while SmokeFuel > 0 (fuel burns 1 min/min while any
  fish is unfinished); ветка = +60 fuel min (L2: +90); max fuel 720. Finished fish wait; Collect → keepnet with
  FVStackFlags::Smoked (price x2); Stats.Smoked += units.
* Worm farm: at each new day (if built and WormFarmDay != Day): L1 +2..3 worms, L2 +3..4 worms and 35% +1 выползок,
  clamped to capacity (worms+crawlers). Collect → Baits.
* Ice cellar: fish storage (units). Aquarium: live fish (1 unit per stack). Trophy wall: 6 plaques, one per
  species; cost mat_board x1 + mat_glue x1 + one fish of that species from the keepnet; SizeCm = the species record
  (Records[fish].MaxSize); a mounted species can be replaced only by a bigger record.
* Radio: shows WeatherTomorrow (+ tips: which fish of unlocked locations need rain / night).
* Bed: sleep (§8). Chest: 40 slots.

---------------------------------------------------------------------------------------------------------------------
## 5. Traders
| id | Name / Role / ShopTitle | Location | Fish x | Items x | Mask | Clothes | Upgrades |
|---|---|---|---|---|---|---|---|
| tr_mitrich | Дед Митрич / хозяин лавки / Лавка рыбака | pond | 1.0 | 1.0 | 1 | yes | yes (keepnet, backpack) |
| tr_pier | Боцман Семёныч / торговец на причале / Лавка на причале | ocean | 1.25 | 1.15 | 2 | no | no |
Stock = everything whose TraderMask has the trader's bit (rods, bait packs, items, shovels). Mitrich tabs:
Удочки · Наживка · Снасти (lines, floats, nets, materials, shovel) · Одежда · Улучшения · Продать. Pier tabs:
Удочки · Наживка · Снасти · Продать. Sell tab: keepnet + backpack stacks with the trader's price (smoked x2).

Shovels (`FVDB::Shovels()`, ShovelLevel = index + 1): 1 Лопата 150 M L1 8 s · 2 Железная лопата 1200 M L2 11 s
(or workshop L2: mat_scrap x4, mat_board x1 + 600) · 3 Стальная лопата 4000 P L4 14 s (or workshop L3: mat_scrap x8,
mat_glue x2 + 2000) · 4 Иридиевая лопата 15000 P L7 18 s (buy only).
Backpack levels: 16 slots (start) → 24 (800) → 36 (3000) at Mitrich «Улучшения».

---------------------------------------------------------------------------------------------------------------------
## 6. Gathering, digging, nets, orders
**Resource slots** (anchors defined by the world module, counts are fixed): pond 6, river 6, mountain 5, ocean 5,
swamp 6, volcano 4, home (yard) 8. `FVDB::RollResource(Loc, Day, Slot, Count)` is deterministic
(hash of loc, day, slot): spawn chance 60% (yard 75%), count 1 (20% → 2). Weights:
pond branch 3, fiber 3, reed 3, berries 2, pebble 2, feather 1 · river branch 3, pebble 3, clay 2, mushroom 2, reed 1,
feather 1 · mountain pebble 4, resin 3, berries 2, branch 1, feather 1 · ocean feather 3, pebble 2, fiber 2, reed 1 ·
swamp reed 3, mushroom 3, clay 3, resin 1, fiber 1 · volcano pebble 4, resin 2, clay 1 · home branch 3, berries 3,
fiber 2, mushroom 2, feather 2, pebble 1. Taken slots are stored in `Gathered[loc]` for the day; Stats.Gathered.

**Dig spots**: pond 1, river 1, swamp 1, home 3. Once per day each (`Dug[loc]`). Needs ShovelLevel ≥ 1.
`FVDB::RollDigLoot` per dug cell: worm 45%, выползок 8% (+3% per shovel level above 1), камешек 10%, глина 8%,
junk_can 4%, nothing otherwise. Worms → Baits (Stats.WormsDug), others → backpack.

**Net spots**: pond 2, river 2, mountain 1, ocean 2, swamp 2, volcano 0, home 0.
Simulation (`SimulatePassive`, per net, from SimMinute to ToAbs): skip if torn or catch units ≥ capacity (time still
advances). Rate per game minute = 1 / NetMinutesPerFish x (rain 1.2) x (night 22-06: 1.1). Progress += rate x dt;
each whole unit = one catch via `FVDB::PickCatch` with context {Location, Depth .3, PhaseMask of that minute,
Weather, player level, Rod/Bait null, bNet true, JunkMultScale .5}. Fish: size = RollSizeFrac(level,false,10,null) x .85,
quality = RollQuality capped at Gold. Durability -= `FVDB::NetWear(F,Q)` = 5 x {1,1.5,2.2,3.2}[Q] x
{1,1.3,1.8,2.6,4}[rarity]; ≤ 0 → torn. Each simulated hour with durability < 25%: 4% tear. Junk catches go into the
net too. Records are NOT updated by nets; Stats.NetFish += fish. Deterministic RNG (seed = hash(loc, spot, minute)).
Collect → keepnet (fish) / backpack (junk) as far as it fits. Repair: mat_rope x1 + res_fiber x2 → +50% max
durability, untorn. Remove: only if durability ≥ 95% and the catch is empty (the net item returns to the backpack).

**Orders** (board at the pond): each new day the board keeps taken orders, drops old offers and offers 3 new ones
(seeded by day). Kinds: 45% fish of an unlocked location (non-legendary; count common 2-3, uncommon 1-2, rare+ 1;
30% need Silver+ when level ≥ 4) · 15% smoked fish (only if the smokehouse is built) · 25% resources (5-15) or
materials (2-5 of board/rope/glue) · 15% food (only if the kitchen is built; a dish of the current kitchen level).
Reward = round((value x rand 1.6..2.2 + 20) / 5) * 5; Deposit = max(10, round(Reward x .25)); DurationHours 24 / 36 /
48 (rare fish 48). 20% of orders also give an item (mat_glitter x2, float_feather, line_linen, mat_glue x2).
Customers: Бабушка Нюра, Кузнец Игнат, Учительница Вера, Повар Жора, Рыбак Петрович, Тётя Галя, Почтальон Лёша,
Капитан Бурун. Max 3 taken. Deliver before Deadline → Reward + Deposit, Stats.OrdersDone; expire → Failed,
deposit lost, Stats.OrdersFailed (toast «Заказ сорван: …»).

---------------------------------------------------------------------------------------------------------------------
## 7. Food, tackle, effective stats
* Effective stats = `FFVRodStats::FromRod(rod)` + `GetTackleMods(rod)` (line + float) + food
  (`FFVItemDef::Buff.Scaled(FoodPower(Q))`). FVFishing reads `UFVGame::GetEffectiveRodStats()` once per cast and uses:
  BarBonus (bar = 96 + LevelBarBonus + BarBonus), ReelMult, DrainMult, BiteSpeed (x bait), CastRange, TreasureBonus,
  SizeBonus (added to the size frac), ReactBonus (added to the 1.25 / 1.0 s reaction window).
  MakeCatchContext: `ExtraLuck = eff.Luck - Rod.Luck`, `JunkMultScale = eff.JunkMult`.
* Tackle tier rule: item Tier ≤ rod Tier + 1. Install/remove only in the workshop (Оснастка tab) — needs the workshop
  built. The installed item leaves the backpack; the old one returns.

---------------------------------------------------------------------------------------------------------------------
## 8. Time & sleep (Core)
`GetAbsMinutes() = (Day-1)*1440 + TimeMinutes`. `AdvanceClock(M, bQuiet)`: in chunks up to midnight →
`Profile->SimulatePassive(from, to, weather)` → `ExpireOrders` (toast each) → at midnight `StartNewDay`: Day+1,
Weather = WeatherTomorrow, WeatherTomorrow = roll (30% rain), `Profile->OnNewDay(now)` (lines → toasts unless quiet,
returned for the sleep summary), day toast. The normal clock = AdvanceClock(Dt / MinuteSeconds) (still frozen during
the minigame and under overlays). Food expires naturally (GetActiveFood checks time).
**Sleep** (bed): confirm («Лечь спать до утра?»; before 18:00: «Ещё светло. Всё равно лечь спать?»). Then
Fishing->Reset, advance to 06:00 (next day if now ≥ 06:00), quiet; summary modal «Доброе утро!» with lines: day +
weather, worm farm, nets (fish caught, torn), smokehouse ready, failed orders; SaveNow. Sfx Sleep.

---------------------------------------------------------------------------------------------------------------------
## 9. World (FVScene) & interaction
* **Home locations** are not in `FVDB::Locations()`; `FindLocation("home"/"house")` returns their defs (Theme Home /
  HomeInterior). Map: extra pin «Дом» (always unlocked) → TravelTo(home). TravelTo/EnterWorld accept home ids;
  music theme Home for both.
* **Yard** (home): house exterior with a reachable DOOR (walk band touches it), fence, garden beds (3 dig spots),
  trees, flowers, a well, a path; 8 resource slots; no water (no fishing hints: «Здесь не порыбачишь — идите к воде»
  is shown only if the player presses the action). Both orientations. Spawn near the path.
* **Interior** (house): a cosy room, wall band at the top (wallpaper = runtime tiles of the equipped wallpaper decor),
  floor = runtime tiles of the equipped floor, windows (curtains decor), and anchors for: Kitchen, Workshop,
  Smokehouse, WormFarm, Aquarium, IceCellar, Radio, Bed, TrophyWall (on the wall; interaction feet below), Chest,
  Dresser (DecorCatalog), exit Door, plus decor anchors (rug centre, lamp, plant, painting, curtains on windows).
  Stations draw `UFVSprites::StationSprite(S, level)` (level 0 = construction site). Aquarium shows its fish swimming,
  the trophy wall shows plaques. The lamp decor gives a light at night. Kitchen (built) = warm fire light;
  smokehouse (fuel > 0) = smoke particles. The interior walk area is much taller than the fishing land band
  (landscape ~ y 150..330, portrait ~ TopBarH+120 .. VH-BottomBarH-10).
* **Fishing locations**: anchors in BOTH orientations for: stall (pond «Лавка Митрича», ocean «Лавка на причале» on
  the sand, away from the pier/umbrella/crates), order board (pond, near the stall), net spots (§6 counts; posts on
  the shore at ShoreY-4, the net drawn in the water below, away from the dock and not in the autotest cast path),
  resource slots, dig spots. Keep the autotest paths clear: straight down from spawn, (VW*0.3, VH-BottomBarH-60),
  swipe start (VW/2, TopBarH+70). Every new FLayout coordinate must be moved by TranslateLayout.
* Objects are y-sorted entities with blockers. Resource pickups = `ItemIcon(id)` at scale 2 with a shadow and a gentle
  bob/sparkle. Highlight (hovered or nearest in reach): outline glow + name plate in DrawOverlay.
* **Interaction (Core, `TickWorldInteraction`, before action resolution; only when the world is interactive and
  fishing is Idle)**: desktop LMB press on an object's Hit (and not over UI) → consume the click (no cast); E/RMB/
  gamepad X → nearest in reach; touch `bWorldTap` → object at the tap. In reach → `Interact`; else walk-to (scene
  auto-walk) and interact on arrival; manual movement cancels. On touch, `bCanCast` is false while a finger is on an
  object. Hint line: «[E] Собрать: Ветка» / «Коснитесь: Лавка Митрича» (before the fishing hint).
* **Interact dispatch**: Resource → Gather (+toast «+1 Ветка», PlayPickup, Sfx Pickup; backpack full → toast) ·
  Stall → Shop(trader) · OrderBoard → Orders · NetSpot → NetSpot("<loc>:<i>") · DigSpot → no shovel: toast «Нужна
  лопата — купите у Митрича»; dug today: toast «Здесь уже копали сегодня»; else Digging("<loc>:<i>") · Door → yard:
  EnterHouse, interior: ExitHouse (Sfx Door) · Station → Bed: sleep; Chest: Chest screen; IceCellar (built): IceCellar
  screen; others / not built: Station(station id) · DecorCatalog → HomeDecor.

---------------------------------------------------------------------------------------------------------------------
## 10. HUD & controls (Core)
HudButtons: [0] Карта (M) · [1] **Рюкзак** (EFVIcon::Backpack, Inventory ctx "backpack", **B**) · [2] Садок (Inventory ctx
"keepnet", I/Tab) · [3] Гардероб (G) · [4] Коллекция (C) · [5] Достижения (J) · [6] Меню (Esc). No global shop.
Bait badge on Рюкзак, keepnet Used/Cap on Садок. Active food: food icon + time left next to the clock (landscape) /
in the top strip (portrait), tooltip with the effect lines. E = interact. Help/pause texts updated accordingly.
Junk catches → backpack (card badge «В рюкзак» / «Рюкзак полон — мусор выброшен»). Treasure items → backpack.
Level-up modal also lists `FVDB::HomeUnlocksAtLevel(L)`.

---------------------------------------------------------------------------------------------------------------------
## 11. Screens
Common: `FV::WindowRect()` in portrait, a landscape window rect of your choice (usually 10,30,620,318); title via
`D.Window`; portrait footer `D.ActionBar` drawn last; select-on-release in scrolling lists; `Game->Confirm` lambdas
capture `TWeakObjectPtr<UFVGame>` + ids; after every profile change `Game->NotifyProfileChanged()` and
`Game->GetScene().RefreshWorldObjects()` when world state changes. Item icons via `UFVSprites::ItemIcon(id)`, names via
`FVDB::StackName`. Requirement rows «icon · name · есть/нужно» green/red.

**Trade & storage (UI-A)** — Inventory («Инвентарь»): tabs Садок | Рюкзак (ctx), right column: rods («Взять»), bait
list, rod tackle summary, active food, shovel. Keepnet actions: «Выпустить» 1/стопку (confirm). Backpack: «Съесть»
(food), «Выбросить» 1/стопку (confirm). No selling (tooltip/card shows «У Митрича: N»). Shop: per trader (§5) with a
keeper portrait (pier: sailor with a cap & beard, different greetings), «Продать» tab (grid of keepnet + backpack,
«Продать 1 / стопку / всю рыбу»). Chest screen (backpack ↔ chest) and IceCellar screen (keepnet ↔ cellar, «Улучшить →»
opens Station st_icecellar): two panes, «→ 1 / стопку / всё». Map: «Дом» pin. Menus/Wardrobe: texts & links.

**Home (UI-B)** — Station screen (ctx = station id): header (name, level pips, level name); not built → build panel
(description, coins, materials have/need, min level, «Построить»). Built → content + «Улучшение» tab (next level
panel, or «Максимальный уровень»). Contents: Kitchen = recipe list (locked ones: «Нужна плита»), detail (inputs,
output, buff lines, duration), «Готовить» (Cooking → OpenScreenWith(Cooking, recipe) / plain craft with x1/x5) ·
Workshop = tabs Материалы · Снасти · Сети · Переработка · Оснастка · Лопата + «Мастерская блёсен» (L2) · Knots
recipes → OpenScreenWith(Knots, recipe) · Оснастка: owned rods, 3 slots (Леска / Поплавок / Наживка) with stat
preview (base vs with tackle), pick from backpack items of that kind (tier check) · Smokehouse = slots with progress,
fuel bar («Топлива на 2 ч 30 мин»), «Загрузить рыбу» (pick a keepnet fish), «Подбросить дрова» (ветка), «Забрать» ·
WormFarm = stock + «Собрать» · Aquarium = animated tank, «Запустить рыбу» / «В садок» · Radio = tomorrow's weather
+ tips · TrophyWall = 6 plaques, «Сделать чучело» (keepnet fish with a record). Orders screen: cards (customer, item,
count, quality/smoked, deposit, reward, deadline / duration, state chip), «Взять (залог N)» (confirm), «Сдать».
NetSpot screen: no net → choose a net from the backpack (stats) → «Поставить»; net → durability bar, torn state,
catch (icons), «Собрать улов», «Починить (верёвка 1, волокно 2)», «Снять». HomeDecor: slot tabs, grid, preview,
source text, «Купить» / «Поставить» / «Убрать».

**Mini-games (MiniGames)** — full-window screens (`HandlesEscape()` true: Esc/«Назад» → confirm «Бросить?» → return
without spending), keyboard + mouse + touch, both orientations, `Game->IsAutoTest()` → built-in bot plays it:
* **Cooking** (ctx recipe): phase 1 (only if the recipe has cuts): the fish sprite (the ingredient fish, big) on a
  board with N dashed cut lines; swipe (drag) across each line in order — accuracy = distance of the swipe to the
  line; keyboard: a knife slides back and forth, Space cuts. Phase 2: pan/pot/oven with a doneness meter rising;
  green zone; fish: «Перевернуть» (Space / tap) in the zone, then the second side, then «Снять»; tea: «Снять с огня»
  once; pie: «Достать» once. Too late = burnt (low score). Score 0..1 → Normal <.45, Silver <.75, Gold <.93,
  Iridium otherwise. Then `Craft(recipe, 1, Q)` → result card (dish, quality stars, buff lines) → ReturnFromMiniGame.
* **LureCraft**: three part pickers (Основа: блесна/мушка; Цвет: 4; Блёстки: 3) showing their ingredient + have
  counts; preview icon (`LureIcon`); «Собрать» → short assembly animation (tie / polish) → CraftLure → card: «Новый
  рецепт!» + «Лучше всего клюют: …» (LureFavorites names, unknown species «???») + pack size. Discovered recipes
  list (x/24).
* **Knots** (ctx recipe): shows a sequence of N arrows (↑→↓←), highlighted one by one (~0.55 s each, faster at
  higher lengths), then the player repeats: arrows/WASD, swipe on touch, or four big arrow buttons. Mistake → «Узел
  распустился» and the pattern replays (3 tries). Success → Craft + Stats (knots) → card. Failure → nothing spent.
* **Digging** (ctx "<loc>:<spot>"): a soil grid 5x4 (portrait 4x5), timer = DigSeconds (bar); click/tap a cell →
  dig (2 hits with shovel level 1-2, 1 hit with 3-4); loot per cell (RollDigLoot); a worm peeks out and must be
  tapped within 1.2 s or it hides (lost). End → MarkDug, summary card (worms, выползки, items) → world.

---------------------------------------------------------------------------------------------------------------------
## 12. File ownership
| Module | Files (Private/ unless noted) |
|---|---|
| Data | FVDatabase.cpp, **FVDatabaseHome.cpp** (new), FVSaveGame.cpp, **FVSaveGameHome.cpp** (new); owns Public/FVDatabase.h, Public/FVSaveGame.h |
| World | FVScene.cpp, **FVSceneHome.cpp** (new), FVSceneArt.cpp, FVSceneArt.h, FVTouch.cpp/.h, FVFramework.cpp; owns Public/FVScene.h |
| Core | FVGame.cpp, **FVGameHome.cpp** (new), FVFishing.cpp, FVAutoTest.cpp/.h, FVAudio.cpp; owns Public/FVGame.h, FVFishing.h, FVAudio.h |
| UI-A (trade) | FVScreenShop.cpp, FVScreenInventory.cpp, **FVScreenStorage.cpp** (new), FVScreenMap.cpp, FVScreenMenus.cpp, FVScreenWardrobe.cpp, FVScreenCommon.h (additive), FVDraw.cpp (additive) |
| UI-B (home) | **FVScreenStation.cpp**, **FVScreenOrders.cpp**, **FVScreenNet.cpp**, **FVScreenDecor.cpp** (all new; optional private FVScreenHome.h) |
| MiniGames | **FVMiniGames.cpp** (new; optional private FVMiniGames.h) |
| Art | FVArtItems.cpp, FVSprites.cpp, **FVArtHome.cpp** (new), FVArtCharacter.cpp; owns Public/FVSprites.h |
FVTypes.h is shared and frozen (integrator only).

---------------------------------------------------------------------------------------------------------------------
## 13. Achievements (append to FVDB::Achievements; RewardDecor = reward-only decor)
| id | Name | Kind Target | Reward |
|---|---|---|---|
| ach_first_station | Новоселье | StationsBuilt 1 | 300 |
| ach_all_stations | Хозяйство | StationsBuilt 8 | 5000 |
| ach_stations_max | Мастер на все руки | StationsMaxed 8 | 15000 + decor_plant_bonsai |
| ach_gather_100 | Собиратель | GatherTotal 100 | 800 |
| ach_craft_50 | Рукодельник | CraftTotal 50 | 1500 |
| ach_chef | Шеф-повар | CookTotal 25 | 2000 + decor_paint_goldfish |
| ach_smoke_30 | Коптильщик | SmokeTotal 30 | 2500 |
| ach_dig_100 | Землекоп | DigTotal 100 | 1000 |
| ach_nets | Сетевик | NetFishTotal 100 | 3000 + decor_rug_fish |
| ach_orders | Надёжный поставщик | OrdersDone 10 | 3000 + decor_paint_mitrich |
| ach_knots | Морской узел | KnotsTotal 20 | 1500 |
| ach_lures | Исследователь блёсен | LuresDiscovered 12 | 3000 |
| ach_lures_all | Все блёсны | LuresDiscovered 24 | 8000 |
| ach_aquarium | Аквариумист | AquariumFish 10 | 4000 + decor_lamp_jelly |
| ach_trophies | Трофейщик | TrophiesMounted 5 | 3000 + decor_wall_fish |
| ach_decor | Уютный дом | OwnDecor 10 | 3000 |

## 14. House decor (FVDB::Decor) — slot · id · Name · Style · Price (0 = starter/reward) · colours
Wallpaper: decor_wall_logs Бревенчатые стены s0 starter (170,120,70)/(130,85,50) · decor_wall_flowers Обои в
цветочек s1 400 (236,214,170)/(210,110,120) · decor_wall_blue Голубые обои s2 600 (150,190,220)/(110,150,190) ·
decor_wall_sea Морские обои s3 1500 L4 (60,120,170)/(240,240,230) · decor_wall_fish Обои с рыбками s4 reward
(220,230,200)/(240,140,60).
Floor: decor_floor_planks Дощатый пол s0 starter (150,100,60)/(120,78,46) · decor_floor_parquet Паркет s1 800
(170,110,60)/(120,70,40) · decor_floor_tiles Плитка s2 1200 (220,220,210)/(150,170,190) · decor_floor_stone Каменный
пол s3 2000 L5 (150,150,150)/(110,110,120).
Rug: decor_rug_woven Плетёный коврик s0 300 (200,170,110)/(150,110,70) · decor_rug_red Красный ковёр s1 900
(180,50,50)/(230,190,90) · decor_rug_fish Ковёр-рыба s2 reward (70,130,200)/(240,200,90).
Lamp: decor_lamp_candle Свеча s0 starter (240,230,200)/(255,190,90) · decor_lamp_kerosene Керосиновая лампа s1 350
(180,140,60)/(255,200,110) · decor_lamp_float Фонарь-поплавок s2 1000 (220,60,50)/(250,250,240) · decor_lamp_jelly
Светильник-медуза s3 reward (200,120,230)/(140,220,255).
Plant: decor_plant_ficus Фикус s0 250 · decor_plant_cactus Кактус s1 300 · decor_plant_fern Папоротник s2 700 ·
decor_plant_bonsai Бонсай s3 reward (greens / pot colours of your choice).
Painting: decor_paint_pond Картина «Пруд» s0 500 · decor_paint_map Карта долины s1 900 · decor_paint_mitrich Портрет
Митрича s2 reward · decor_paint_goldfish Золотая рыбка s3 reward.
Curtains: decor_curt_linen Льняные шторы s0 300 · decor_curt_check Клетчатые шторы s1 500 · decor_curt_velvet
Бархатные шторы s2 1500 L4.
Starter (owned & equipped): decor_wall_logs, decor_floor_planks, decor_lamp_candle. Rug/Plant/Painting/Curtains may
be empty. Bought in the HomeDecor screen (catalog). Treasure chests: 12% item drop (mat_glitter x1-2 common,
float_feather, line_linen, line_braided L3+, float_glow L5+, float_gold L9+ 1%).

## 15. Save migration (v1 → v2)
Version initializer 0 = legacy. In FixupAfterLoad (Loaded ≤ 1): move junk stacks from Keepnet to Backpack (before
the keepnet validation), Bed & Chest level 1, starter decor owned+equipped, WeatherTomorrow roll, OrdersDay 0.
Validate every new field (ids, counts, enums, capacities; drop unknown ids; keepnet = fish only). New game:
bed & chest built, starter decor, 16-slot backpack, no shovel, WeatherTomorrow rolled.
