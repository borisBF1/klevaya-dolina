# Клёвая Долина (FishingValley) — design & engineering spec

A 2D pixel-art fishing game in Unreal Engine 5.7, pure C++ (no .uasset content). Fishing plays like
**Stardew Valley** (hold-to-charge cast, bite "!", bobber-bar minigame). Progression through fishing level,
coins, rods with stats, bait, a keepnet, cosmetics, 6 locations with distinct fish, a fish collection and achievements.
All player-facing text is **Russian**.

## 1. Engineering ground rules

* Project root (ASCII junction, ALWAYS use this path): `C:\UEProjects\FishingValley`
  (physically the same folder as `<папка проекта с кириллицей в пути>`, but MSVC breaks on Cyrillic paths).
* Module sources: `Source/FishingValley/Public/*.h` (contract headers), `Source/FishingValley/Private/*.cpp`.
* Compile ONE file (no link, safe while others work):
  `cmd //c "C:\\UEProjects\\FishingValley\\Tools\\CompileFile.bat Source\\FishingValley\\Private\\FVScene.cpp" 2>&1 | iconv -f CP866 -t UTF-8 | grep -E "error|warning|Result"`
  (compiler messages are Russian/CP866 — the iconv makes them readable). UBT runs one build at a time
  (`-WaitMutex`), so a compile may wait for another agent's compile; that's normal.
  Full build (link): `Tools\BuildEditor.bat` — only the integrator runs it.
* The game runs as `UnrealEditor.exe <uproject> -game`; map `/Engine/Maps/Entry`, GameMode `AFVGameMode`, no pawn.
  Everything is drawn in `AHUD::DrawHUD` through `FVDraw` (UCanvas). Input is polled in `AFVPlayerController::PlayerTick`.
* UE 5.7 facts verified in this project:
  * `UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, Name, TConstArrayView64<uint8>(BGRA bytes))`, then
    `Tex->Filter = TF_Nearest; Tex->SRGB = true; Tex->UpdateResource();` → crisp pixel art. Keep textures in a UPROPERTY array.
  * Canvas colors are LINEAR: build them with `FV::Col(r,g,b,a)` (sRGB bytes → linear). Texture pixels are sRGB bytes.
  * Text: `FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), Size, FName("Bold"))`
    with `FCanvasTextItem` renders Cyrillic. (`FCoreStyle::GetDefaultFontStyle` does NOT render on canvas.)
    Measure with `FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Str, FontInfo, 1.f)`.
  * Input: `PC->WasInputKeyJustPressed(EKeys::SpaceBar)`, `IsInputKeyDown`, `WasInputKeyJustReleased`, `GetMousePosition(float&, float&)`.
  * Source files are UTF-8; the compiler runs with `/utf-8`, so `TEXT("Карась")` is fine.
* No `Date.now`-style nondeterminism concerns; use `FMath::FRand*` or an `FRandomStream`.
* Log category `LogFV` (declared in FVTypes.h). Prefix important logs with `FV:`.
* **File ownership**: each agent owns specific files. Public headers are the contract: you may make ADDITIVE changes
  to headers you own (new members/methods, never rename/remove/change existing declarations). Never edit files you
  don't own; if you need something from another module, use only what its header declares.
* Keep code robust: null checks on lookups, no crashes on empty data, clamp indices. Never block the game thread.

## 2. Screen, scale & layout

* Virtual canvas **640×360**, uniform scale + letterbox (1280×720 → scale 2, 1920×1080 → scale 3).
* UI is drawn at 1 texel = 1 virtual px (icons 16×16, fonts 6–18 virtual pt; body text 8).
* **World art is drawn at `FV::WorldScale = 2`** virtual px per texel (Stardew zoom). Scene background = 320×180 texels
  → 640×360 virtual. Character canvas 32×48 texels (≈ 16×32 body, like Stardew's farmer) drawn at 64×96 virtual.
* World layout: HUD top bar (24 px) — land (with decor, the player walks here) — shoreline at `ShoreY` (≈ y 180–200
  virtual) — water down to y = 344 (360 − 16 hint bar). A wooden dock extends from the shore down into the water
  (≈ 60–110 px); standing at its end reaches deeper water. The player casts straight **down**.

## 3. Core loop & fishing (Stardew-accurate)

1. Walk with **WASD / arrows** on land and the dock. Near water (`CanFishFromHere`) press & hold **Space or LMB**.
2. **Charging**: power meter above the player oscillates 0→1→0… (≈1.0 s per sweep); release to cast.
   The character plays CastWindup while charging, CastRelease on release, then FishWait.
3. **Casting**: bobber flies in an arc to `GetCastTarget(power, rod.CastRange)`; splash + ripple on landing.
   If it lands on land (shouldn't happen) → Retrieving.
4. **Waiting**: bobber bobs; occasional small "nibble" twitches (visual only). Wait time =
   `FVDB::RollBiteDelay * rod.BiteSpeed * bait.BiteSpeed` (typ. 2–10 s). Pressing action while waiting reels in (cancel).
5. **Bite**: bobber dips, big "!" above the head, `Bite` sfx. Reaction window ≈ 1.0 s (junk: auto-hook, no minigame).
   Missed → "Рыба ушла…" → Retrieving. What bit is decided by `FVDB::PickCatch` (depth = bobber depth).
   One bait is consumed per bite.
6. **Minigame** (port of Stardew `BobberBar`, 60 Hz fixed step, track 0..568, 0 = top):
   * Bar: `BarHeight = 96 + FVDB::LevelBarBonus(level) + rod.BarBonus`. Holding → `BarSpeed -= 0.25/tick`,
     releasing → `+= 0.25/tick`; `BarPos += BarSpeed`; bounce off bottom/top with `speed = -speed * 2/3`
     (Stardew also zeroes the speed when pressing against the top); clamp to [0, 568 − BarHeight].
   * Fish (Stardew algorithm): each tick, with probability `difficulty * (motion==Smooth ? 20 : 1) / 4000` (and for
     Smooth only if it has no target) pick a new target: `spaceBelow = 548 − pos; spaceAbove = pos;
     percent = min(99, difficulty + rand(10,45)) / 100; target = pos + rand(min(−spaceAbove, spaceBelow), spaceBelow) * percent`.
     Floater: `floatSink = max(floatSink − 0.01, −1.5)`; Sinker: `min(floatSink + 0.01, 1.5)`.
     If `|pos − target| > 3 && target != −1`: `accel = (target − pos) / (rand(10,30) + (100 − min(100, difficulty)));
     speed += (accel − speed) / 5` — else if not Smooth and `rand < difficulty/2000`: `target = pos ± rand(50..100)`, else target = −1.
     Dart: with probability `difficulty/1000` → `target = pos + (rand<.5 ? rand(−100 − 2·difficulty, −51) : rand(50, 101 + 2·difficulty))`.
     `target = clamp(−1, 548)`; `pos += speed + floatSink`; clamp pos to [0, 532].
   * Fish in bar ⇔ fish icon (32 tall) overlaps the bar with a few px tolerance (Stardew:
     `pos + 12 <= barPos − 32 + barH + 32 && pos − 16 >= barPos − 32` style; implement an equivalent, fair check).
   * Progress starts at 0.3 (Stardew). In bar: `+0.002 * ReelMult` per tick; outside: `−0.003 * DrainMult` per tick
     (Stardew values). Leaving the bar at least once ⇒ not perfect. 1.0 → caught, 0 → escaped.
   * Treasure (chance = location.TreasureChance + rod.TreasureBonus): appears after ~1–3 s at a random height;
     while inside the bar its progress fills (`+0.0135/tick`, drains slowly otherwise); full → treasure collected.
     While the treasure is being collected and the fish is outside the bar, progress still drains.
   * Visuals: tall wooden panel (≈ 44×300 virtual px) beside the player (right side, or left if near the right edge):
     water-colored track, translucent green bar (brighter while the fish is inside), generic fish icon
     (`EFVIcon::Fish`), vertical progress meter (red→yellow→green), treasure chest icon with its own tiny meter,
     spinning reel handle, panel shake when progress drops, "ИДЕАЛЬНО!" flash on perfect catches. Reel-tick sfx.
7. **Showcase**: character HoldFish with the fish sprite above the head; a parchment card: fish sprite (×3), name in
   rarity color, size (cm), quality stars, price, "+XP", badges "Новый вид!" / "Рекорд!" / "Сундук: …".
   Action/click → Idle. Catches go to the keepnet. If the keepnet is full the player cannot cast
   (toast "Садок полон — продайте улов") — checked BEFORE charging.
8. Escaped: "Рыба сорвалась!" + Escape sfx → Retrieving → Idle.

Quality: `FVDB::RollQuality(sizeFrac, perfect)` (Normal / Silver / Gold / Iridium; perfect bumps one tier).
XP: `FVDB::CatchXp`. Price: `FVDB::FishPrice` (quality multipliers 1 / 1.25 / 1.5 / 2).

## 4. Progression & content targets (FVDatabase.cpp)

* **Levels 1–10**, XP to reach level L (Stardew curve): 2:100, 3:380, 4:770, 5:1300, 6:2150, 7:3300, 8:4800,
  9:6900, 10:10000. Each level: +8 bar height. Level-ups show a modal listing `FVDB::UnlocksAtLevel`.
* **Locations** (ids fixed): `pond` Деревенский пруд (L1, free) · `river` Лесная река (L2, 400) ·
  `mountain` Горное озеро (L3, 1 500) · `ocean` Морской причал (L5, 5 000) · `swamp` Туманное болото (L7, 12 000) ·
  `volcano` Вулканическое озеро (L9, 30 000 — lava fishing). Themes match ids.
* **Fish**: 6–7 per location (≈5 regular + 1 legendary), ≈40 total, each with distinct shape/colors.
  Price bands (Normal): pond 15–120 (legend ~800), river 30–220 (~1 500), mountain 60–400 (~2 500),
  ocean 100–700 (~4 000), swamp 180–1 000 (~6 000), volcano 300–1 800 (~10 000).
  Difficulty bands: pond 15–45 (legend 70), river 25–60 (80), mountain 35–70 (90), ocean 45–80 (95),
  swamp 55–90 (100), volcano 65–100 (110). Some fish only at night / morning / in rain; rarer fish need depth.
* **Rods** (6): Бамбуковая (free) → Стеклопластиковая (800, L2) → Карбоновая (3 000, L4) → Титановая (9 000, L6) →
  Иридиевая (25 000, L8) → «Посейдон» (60 000, L10). Stats rise monotonically (bar +0…+40, reel ×1.0…×1.28,
  drain ×1.0…×0.74, bite ×1.0…×0.55, cast range 0.55…1.0, luck 0…0.75, treasure +0…+0.12).
* **Bait** (4): Червяки, Мотыль, Блесна, Волшебная наживка — bite speed / rarity / junk / size trade-offs.
* **Keepnet** upgrades: 12 → 20 → 30 → 45 → 60 slots (0 / 500 / 2 000 / 6 000 / 15 000).
* **Cosmetics**: ~10 hats, ~8 outfits (styles × colors), 6 bobbers, 6 pets (Cat, Dog, Duck, Fox, Frog, Penguin);
  prices 150–30 000, a few are achievement/treasure rewards only (e.g. Корона for the first legendary).
* **Achievements**: ~16 (first catch, 50/250 fish, species counts, perfect catches, treasures, iridium, earnings,
  levels, all locations, completing a location, all rods, each legendary...). Rewards: coins and/or cosmetics.
* Junk (5): водоросли, старый ботинок, ржавая банка, коряга, бутылка — 1–15 coins, no minigame.
* Target pacing: first rod upgrade after ~10 minutes; full completion 3–4 hours.

## 5. Time & weather

In-game clock: 1 minute = 0.5 s real (24 h = 12 min). Phases: Утро 6–12, День 12–18, Вечер 18–22, Ночь 22–6.
The clock runs only in the world screen with no overlay/modal. At midnight a new day starts and weather rerolls
(≈70% ясно / 30% дождь). Lighting: warm dawn/dusk tint, dark blue night with lantern/window glows, fireflies at
night (pond/swamp), rain streaks + water ripples + darker tint when raining, fog in the swamp, glowing lava at the volcano.

## 6. Screens & controls

| Key | Action |
|---|---|
| WASD / arrows | walk |
| Space / LMB (hold) | charge & cast, hook, reel (hold = bar up) |
| M | Карта (travel / unlock locations) |
| B | Лавка (shop) |
| I or Tab | Снаряжение и садок (keepnet: sell; choose rod & bait) |
| G | Гардероб (cosmetics) |
| C | Коллекция рыб |
| J | Достижения |
| Esc | close overlay / pause menu (cancels a cast while waiting) |
| F11 | fullscreen |

World HUD (top bar, drawn by UFVGame): coins, level badge + XP bar, location name, clock + weather/phase icon,
icon buttons (Карта, Лавка, Садок, Гардероб, Коллекция, Достижения, Меню) with tooltips showing hotkeys.
Bottom hint bar: `FVFishing::GetHint()` or walking hints. Toasts slide in at top-right under the bar.
Overlays pause the clock and fishing; menus can't open while fishing is busy (toast explains).

## 7. Visual style

Stardew-like: warm, saturated, friendly. Outlines are dark tinted (dark brown / navy), never pure black.
Light from the top-left. Dithering sparingly. UI: wooden frames (dark brown outer, orange-brown bevel), parchment
interiors (#F9DAA0), brown ink text. Rarity colors: Common grey-white, Uncommon green, Rare blue, Epic purple,
Legendary gold. Quality stars: silver, gold, iridium (purple-magenta).

## 8. Module ownership (who implements what)

| Owner | Files |
|---|---|
| Data | FVDatabase.cpp, FVSaveGame.cpp (+FVDatabase.h, FVSaveGame.h additive) |
| Art | FVSprites.cpp, FVArtItems.cpp, FVArtCharacter.cpp (+FVSprites.h additive) |
| World&Fishing | FVScene.cpp, FVSceneArt.cpp (+ optional private FVSceneArt.h), FVFishing.cpp (+FVScene.h, FVFishing.h additive) |
| UI | FVDraw.cpp, FVScreenMenus.cpp, FVScreenMap.cpp, FVScreenInventory.cpp, FVScreenCommon.h (+FVDraw.h additive) |
| Screens2 | FVScreenShop.cpp, FVScreenWardrobe.cpp, FVScreenCollection.cpp |
| Core | FVGame.cpp, FVAutoTest.h/.cpp, FVFramework.cpp, FVAudio.cpp, FishingValley.cpp, Config/*.ini (+FVGame.h, FVAudio.h, FVFramework.h additive) |

`Private/HeaderCheck.cpp` is a temporary compile check; the integrator deletes it.
