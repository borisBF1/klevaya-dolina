# Code map: art, audio, autotest

Root: `C:/UEProjects/FishingValley/Source/FishingValley`. Refreshed for update 6 (state after 0.5; lines checked
2026-09-26). Files: `Public/FVSprites.h` (255), `Private/FVSprites.cpp` (1751), `Private/FVArtItems.cpp` (2289),
`Private/FVArtCharacter.cpp` (2113), `Public/FVAudio.h` (52), `Private/FVAudio.cpp` (2034), `Private/FVAutoTest.h` (204),
`Private/FVAutoTest.cpp` (2691), `Private/FVAutoTestLegends.cpp` (1358). Related, other maps: `FVArtHome.cpp` (stations,
stalls, decor, net/dig props; scene.md), `FVSceneArt.cpp` (location backgrounds and world props; scene.md).

## 1. Pixel-art core (FVSprites.h / FVSprites.cpp)
- **FFVImage** (h L18-53, cpp L71-290): CPU sRGB RGBA, straight alpha, row-major, (0,0) top-left. `Set/Get` bounds-safe,
  `Blend`, `FillRect(x,y,w,h,C,bBlend)`, `FillCircle` (r*r+r), `Line` (Bresenham, blends), `Blit(Src,dx,dy,bFlipX)`,
  `Outline(C)` (4-neighbour ring into transparent px only, L183), `Tint(Mul)`, `FlippedX`, `Cropped`,
  `FromAscii(Rows, Palette)` (L246/L273; '.', ' ' and unknown chars are transparent, short rows padded).
- **FVArt** (h L240-255, cpp L27-66): `Mix`, `Shade` (cool purple), `Light` (warm), `OutlineOf`, `WithAlpha`,
  `Noise(x,y,seed)` in [0,1), `InkOutline()` = (56,32,36).
- **Textures**: `MakeTexture` (`PF_B8G8R8A8`, `TF_Nearest`, SRGB, `TEXTUREGROUP_Pixels2D`, clamp) -> `UPROPERTY
  Textures`, freed only in `Shutdown` (L309). `MakeSprite` L366 falls back to magenta `Missing`.
- **Cache**: `TMap<uint64,FFVSprite> Cache` reserved `CacheReserve = 8192` (cpp L18, L295), `NamedReserve = 1024`.
  `AddCached(Key, Img, DebugName)` L409, `FindCached` h L91. `MakeKey(Cat, A, B=0, C=0)` = `Cat<<56 | C<<48 |
  (A&0xFFFFFF)<<24 | (B&0xFFFFFF)` (h L93-96). Getters return `const FFVSprite&`: copy by value if kept.
- **`FVSpriteCat`** (h L230-237): Icon 1, Fish 2, FishSil 3, Junk 4, Bait 5, Rod 6, Chest 7, ItemArt 8, Lure 9,
  Character 10, HatIcon 11, OutfitIcon 12, Bobber 13, Pet 14, Station 15, Stall 16, HomeProp 17, Decor 18, Trophy 19,
  Scene 20, Misc 30. **Free: 21-29, 31-255.** Sub-uses: ItemArt C=0 art/tier/tint, C=1 shovels (`0xF000+L`), C=2 fish
  tokens (`0xF100+rarity`) (FVArtItems.cpp L2193-2271); HomeProp C=1..5 (FVArtHome.cpp L2786-2841); Scene C=1 backgrounds,
  C=2 decor (FVScene.cpp L34, L93); Misc magic numbers: shopkeeper portraits (FVScreenShop.cpp L424-425), menu
  (FVScreenMenus.cpp L367), map (FVScreenMap.cpp L826). New NPC portraits / expert fight skins / city props: take a new
  category (e.g. 21 NpcPortrait, 22 FightSkin, 23 CityProp), not Misc.
- **Cached getter idiom** (e.g. FVArtItems.cpp L1318 `Junk`): `Key = MakeKey(Cat, ...); if (FindCached) return;
  return AddCached(Key, BuildXxx(...), DebugName)`. Every getter has a static pure `BuildXxx()` (h L144-156, L195-201)
  used by the contact sheet.
- **UI icons**: `EFVIcon` (FVTypes.h L294-316): classic 30 + update 2 (Backpack, House, Hammer, Pot, Net, Shovel, Order,
  Snowflake, Radio, Bed, Smoke, Aquarium, Hand, Door, Knot, Food, Line, Float, Chest) + update 5 (WarnCurrent, WarnEye,
  WarnLantern, WarnCloud, WarnFlame, WarnSpark). 16x16 except Coin/stars 8x8, Cursor 12x12. `Icon()` L422; `Init()`
  (L292) builds all eagerly. `BuildIcon` L785-1513 is a switch, `default:` (L1506) = `BuildMissing()` magenta checker.
  ASCII icons use `IconPalette()` + `Ascii()` (anon namespace L449-630: `IconStar` palette-swap L502, procedural
  `IconClock/Moon/Close/Sun`); update-2 procedural icons in `namespace FVSpritesIconImpl` (L632-784: Snowflake, Smoke,
  Knot, Food).
- **Contact sheet** `DebugWriteContactSheet` L1597-1751 (`FSheetBuilder`, `Section()` gap 6): icons+Missing, fish,
  silhouettes, junk/bait/rods/chest, characters, hats/outfits, cosmetics, bobbers, pets; then calls
  `DebugAddHomeSheet(<name>_Home.png)` (L1750, FVArtHome.cpp L2898: item art, lures, shovels, decor, stations, stalls).
  **Every new builder must be added to a sheet**, or no autotest image shows it.

## 2. Item art (FVArtItems.cpp)
- **Fish**: ASCII body templates per `EFVFishShape` (L17-213; shapes Minnow..Crayfish + Legend), legend silhouettes per
  `EFVLegend` via `LegendTemplate()` (L215-326: Sturgeon, Crystal, Angler, Fog, Zhar; TsarCarp uses Legend). Template
  chars: body B/b/h/d, fins F/f, Y/G pattern, E pupil, W/w eye, o mouth, x post-outline details, L glow detail.
  `RenderFish(Def, bSilhouette, bOutline)` L346 (auto column shading, Stripes/Spots/Glow, outline). `BuildFish` L876;
  getters `Fish` L1292 (key `MakeKey(Fish, hash(Id), Vis)`), `FishSilhouette` L1308.
  **New fish (Р39 +1 classic per location, 17 expert fish, 6 expert legends)**: a new def with an existing shape + colours
  is free; a distinctive expert legend needs a new template row set + a `LegendTemplate` case (new `EFVLegend` values)
  or a new `EFVFishShape` (FVTypes.h L179: adding before `Count` is safe; check the switches in `FishTemplate`).
- **Item palette** `ItemPalette()` L516-594 (anon namespace, magic static): **all 52 letters A-Z/a-z are taken** (only
  digits and punctuation are free). For new materials use a local palette copy (`TMap` override like `IconStar`) instead of
  editing shared letters. `ItemAscii(Rows, bOutline)` L595: centres non-16x16 onto 16x16, then `Outline(InkOutline())`;
  templates have no outline and keep a 1 px margin.
- `namespace FVArtItemsImpl` L615-875: `Ball`, `Ellipse`, `Twinkle`, `Steam`, `LureColor`, `LureMetal`, `Sparkle`,
  `BuildFishInterior`, `RarityRgb`, `FriedPlate(Rim, Garnish, bGold)` L747, `BuildFishTokenIcon` L823.
- Builders: `BuildJunk` L881 (Weed, Boot, Can, Driftwood, Bottle), `BuildBait` L986, `BuildRodIcon` L1194 (procedural
  diagonal blank, colours `RodColor/AccentColor`), `BuildChest(0/1)` L1240, **`BuildItemArt(Art, Tint, Tier)` L1363-2003**
  (switch over `EFVItemArt`, cases L1369-1954, `default:` L1998), `BuildLureIcon` L2004, `BuildShovelIcon` L2121.
  Getters L1292-1360 and L2190-2222.
- **`EFVItemArt`** (FVTypes.h L625-636): Branch, Pebble, Fiber, Feather, Mushroom, Berries, Reed, Clay, Resin | Board,
  Rope, Glue, Scrap, Sinker, Glitter | FishSoup, FriedFish, BerryPie, HerbTea | Line | FloatFeather..FloatGold | Net |
  FriedFishRare/Epic/Legend. Update 6 appends (before `Count`): axe, nails, carrot / new dishes, city-order goods, journal
  page, reel/expert tackle, boutique cosmetics icons, etc. Each needs a `case` in `BuildItemArt` (else `default`).
- **`ItemIcon(FName)` resolver** L2223-2289: fish -> junk -> bait (lure icon) -> `FVDB::FindItem` (`ItemArt(Art, Tint,
  Tier)`) -> rod -> decor -> fish tokens -> `shovel_N` pseudo ids -> magenta. Results cached in `Named` by exact FName.
  New item kinds that are not `FFVItemDef` (e.g. expert rods, facts) must be added here.

## 3. Character art (FVArtCharacter.cpp)
- 32x48 canvas, feet (16,46), head top (16,14); layered `Stamp()` (anon L20), `ThickLine` L63. Arms `FishingArms` L921,
  `DrawRod(Img, Pose, Look, Frame, OutlineC)` L975 (colours `Look.RodColor/RodAccent`), `BuildCharacterImpl` L1048.
- Cache key `MakeKey(Character, Look.GetHash(), anim<<8|facing<<4|frame, Hat)` (L2064). `FFVAppearance` (FVTypes.h
  L477-495) hashes RodColor/RodAccent: **a new visual rod field (expert rod shape/type) must be in `FFVAppearance` and
  `GetHash`**, or stale sprites are reused.
- Bobbers 8x8, 2 frames: `BuildBobberImpl` L1234, getter `Bobber` L2092; `EFVBobberStyle` (FVTypes.h L197): Classic, Duck,
  Star, Heart, Mushroom, Golden + legend skins Tsar, Sturgeon, Crystal, Lantern, Fog, Zhar. Cosmetic slot.
- Pets 16x16 (`BuildPetImpl` L1738), hat/outfit icons L1949/L1999. NPCs (city residents, the net worker Р36) have no
  builder yet: the shopkeeper portraits are drawn in FVScreenShop.cpp (Misc keys); a generic NPC builder should reuse
  `BuildCharacter` with an `FFVAppearance` (world sprite) plus a portrait builder under a new category.

## 4. Art recipes for update 6
- **Expert fight skins (Р32, §10.12)**: draw with FVDraw primitives at runtime (rects, circles, lines, glow) inside
  `FVExpertDraw.cpp`, parameterised by a skin struct (frame colours, water gradient, zone shape/colour, particles).
  Only static ornaments (brass compass rose, ship wheel, chain, anchor, ice frame corners) should be cached images:
  build once per skin (`MakeKey(FightSkin, SkinId, Part)`), never per frame. Keep warning colours shared.
- **Tiers/recolours**: palette-swap (`IconStar` pattern) or `Img.Tint()` before `Outline`.
- **Fish-derived items**: runtime `D.Sprite(Sp->Fish(F), Pos, Scale, Tint)` (no new texture) or a cached CPU variant.
- **Large world images** (house interior furniture Р37, city buildings): 32-128 texel images cached per kind/level; world
  props go through FVSceneArt `EDecor` / `PropImage` (scene.md). Never build per-frame images: each `AddCached` is a
  permanent `UTexture2D`.
- Icons must read at 1x (16 px, landscape slot 30) and are shown x3 in portrait (`FVUI::FitScale`).

## 5. Audio (FVAudio.h / FVAudio.cpp)
- API (h L20-39): `Get`, `Init`, `Shutdown`, `Tick`, `Play(Sfx, Vol, Pitch)`, volumes, `SetMusicTheme(EFVTheme)` (Count =
  menu), `SetRain`, `SetReeling(bOn, Intensity)`. Everything synthesized; no audio device = silent, no crash.
- **`EFVSfx`** (FVTypes.h L239-262): classic 20 + update 2 (Pickup, Dig, Chop, Sizzle, Flip, Hammer, Knot, NetTear, Door,
  Sleep, Radio, Fire, OrderDone) + update 5 (LegendWarn, LegendHook, LineSnap). Append before `Count`; `Pcm[]` and
  `BuildAll` (L1663) size automatically.
- **`FVSynth::BuildSfx(Sfx, Out)`** L340-805: one `case` per sfx, `default:` L803 = 1 kHz beep. Building blocks
  (namespace FVSynth L18-340): `FTone` L72 (`.To/.Env/.Vib/.Lp`, waves Sine/Triangle/Square/Saw/Pulse), `FNoiseSpec`
  L151 (`.Env/.Lp/.Hp/.Band/.WithSeed`), `AddBell` L244, `AddPluck` L262 (Karplus-Strong), `ToPcm(B, Gain, Out)` L315.
  Noise seeds used: 11-121+i, 131-155. **Start new seeds at 160.** Ideas for update 6: axe chop (Chop + low thump),
  boat oars (band noise swells), city bell (`AddBell`), expert line tension creak (Band noise, pitch rises), jerk alarm,
  magnet hum (Saw + Lp), compass tick (Hp click), storm/thunder (Lp noise burst).
- **Playback** `FImpl::PlaySfx` (~L1680-1760): MinGap switch L1687-1703 (default 25 ms; ReelTick 35, Step 100, Hover 40,
  Pickup 60, Dig 80, Chop 50, Sizzle 300, Fire 350, Knot 60, Hammer 80, Door 300, Sleep 1000); random pitch ±6 % list
  L1731-1733; `MaxSfxVoices = 12` (L1527), the oldest voice is dropped (L1725). Rapid new sounds need entries in both.
- **Continuous**: `SetReeling` L2022 (bool + intensity + retriggered ReelTick) is the template for a tension creak;
  streamed loops use `Feed(Stream, Render)` L1788 (music L1869, rain L1899).
- **Music** `FMusicGen::Configure(EFVTheme)` (switch L937-1015): Pond, River, Mountain, Ocean, Swamp, Volcano, Home,
  HomeInterior, `default:` menu. `EFVTheme` (FVTypes.h L176). New locations (Forest, City, Cave, maybe Boat) need a theme
  value + a Configure case; also check the other `switch (Theme)` sites: FVDatabase.cpp L1031, FVSceneArt.cpp L3546,
  L4090, L4118, and `EFVTheme::Count` arrays in FVScreenMap.cpp L138-149. `SetMusicTheme` L1999 returns early on the same
  theme; crossfade 1.2 s / 1.5 s.

## 6. Autotest (FVAutoTest.h / .cpp, FVAutoTestLegends.cpp)
- Activation `-fvautotest` (separate save slot); `FVAutoTest::Tick(Dt, InOutInput)` (cpp L674) runs before touch
  processing and replaces real input with `Synth`. Constants (cpp L20-29): `GlobalTimeout 300`, `FishingTimeout 60`,
  `ForceBiteAfter 3`, `CatchBoost 0.9`, `BalanceTrials 200`, `BalanceBotSkill 0.85`.
- Step API: `AddStep(Name, Enter, Until, Shot, Settle=0.5, Timeout)` L172, `AddScreenShotStep` L184 (opens the screen
  directly, ungated), `AddScreenShotStepWith(Screen, Context, Shot)` L1094, `AddLocationStep(Loc, Minutes, Weather, Shot)`
  L252, `AddFreshScreensStep` L200, `AddMiniGameSteps(Screen, Context, Slug)` L1106 (the screen's bot plays it and takes
  `minigame_<slug>_play/_result`). Helpers: `DismissModals` L805, `ClearToasts` L814, `SetFightProgress` L819,
  `DriveFishing` L826, `GrantEverything` L888, `RunBalanceStep` L961 (uses `FVDB::CatchBarHeight` + `FVSimulateCatchRate`,
  compares with `Saved/Logs/FV_balance_reference.log`), `BackpackUnits` L1022, `TeleportToObject(Kind, Id, What)` L1035,
  `InteractOnce` L1064, `GrantHome` L1734, `FindEmptyWorldPoint` L2005, `TeleportToFishingSpot` L2248, `OnboardingCheck`
  L2233. Game hooks: `UFVGame::OpenScreenWith`, `RequestShot`, `SetClockMinutes`, `DebugAdvanceHours(H)` (FVGame.h L180,
  runs the real clock/sleep simulation), `GetHudButtonRect(Screen)`, `FVScene::DebugTeleport(P)` (FVScene.h L111).
- **Script order** (`BuildScript` L276-673): main menu -> `AddGateSteps` -> new game -> `AddOnboardingSteps` -> pond world
  -> `AddOnlineSteps` -> fresh map/shop/achievements shots + `AddFreshScreensStep` -> `AddLegendFreshSteps` -> touch steps
  (map tap, back, `AddTouchUpdate4Steps`, joystick swipe) -> walk to water, touch cast -> fishing (minigame, showcase)
  -> legend trial -> grant everything -> location shots -> `AddLegendSteps` -> rich screens -> `AddUpdate4ScreenSteps` ->
  back to world -> `AddHomeSteps` (L1145) -> `AddUpdate5MiniGameSteps` -> balance table (L610) -> legend balance -> legend
  tricks -> contact sheet (L637: `contact_sheet.png`, `_Home.png`, `update5_sheet.png`) -> finish.
- Update 5 file `FVAutoTestLegends.cpp`: `namespace FVAutoTestU5` L21, `LegendCheck` L161, `RigTackle` L176,
  `AddLegendFreshSteps` L209, `AddLegendHookSteps` L291, `AddLegendFightEndStep` L360, `AddLegendSteps` L375,
  `AddUpdate5MiniGameSteps` L589, `RunLegendRuleChecks` L820 (one-frame pure checks on scratch `UFVSaveGame`s),
  `RunLegendTricksChecks` L1126, `RunLegendBalanceStep` L1194, `LoadBalanceReference` L1269, `WriteUpdate5Sheet` L1305.
  **Pattern for update 6:** a new `FVAutoTestUpdate6.cpp` (own named namespace) with `AddUpdate6Steps()` declared in
  FVAutoTest.h, `Update6Check(bOk, What)` logging `FV: AUTOTEST u6 <what>: ok` / `FV: AUTOTEST WARNING ...` (++Warnings),
  `RunExpertBalanceStep()` (BALANCE_EXPERT per spec §10.14, one sim per frame), `WriteUpdate6Sheet()` for the new art.
  0.6 rework (§19, Р60, 1D): `FVAutoTestU6::ExpertMechanics()` (run at the first BALANCE_EXPERT cell) checks the control
  law of the 5 rod types, the 6 tricks, the 8 hazards and the legend scores on pure fights; BALANCE_EXPERT = 60 fights per
  bot (epic 12–20 s / mid 45–75 % / strong 75–92 %, legends 10–30 % / 50–75 % / 40–70 s are notes; «сыр» < 5 %, universal
  spread <= 20 p.p. and «знаток» > «новичок» are failures); shots `u6_expert_<type>`, `u6_expert_legend`, `u6_dev_legends`
  (Пауза on the dev page via FV_HUD_PAGE=dev). Tuning knobs: fish Gain/Drain in FVDatabaseU6Expert.cpp, TypeDrain /
  hazard constants in FVExpertFight.cpp, per-legend `Tune` (lunge loss, gain, vigour) in FVExpertLegends.cpp.
- Screenshots: `RequestShot` -> `Saved/<-fvshotdir or AutoTest>/NN_name.png`; NN counts shot steps only (inserting a step
  renumbers later files). UI shots need a visible, non-minimized window (`FV: screenshot SKIPPED ...` otherwise).
- Synth input: `Synth.Pressed.Add(Key)`, `MoveX/MoveY`, `bActionDown/Pressed`, touches `Synth.Touches.Add({Id,Pos})` per
  frame (a lift = the finger missing next frame); tap idiom = finger < 0.06 s at `GetHudButtonRect(Screen).Center()`;
  swipe > 9 px = joystick; hold still >= 0.22 s near water = cast. Desktop mouse: `Synth.Mouse`, `bMouseValid`,
  `bLmbDown/Pressed` (touch mode overwrites them from Touches).
- Runs: `Tools\PortraitTest.ps1 -Tag <t> [-W -H] [-Landscape]` (always `-fvtouch`, kills at 330 s wall), `Tools\AutoTest.bat`
  (desktop mouse 1280x720). PortraitTest surfaces only `FV: AUTOTEST (touch|WARNING|DONE|TIMEOUT)` lines. 0.5 run: ~122-142
  steps, 68-74 shots, ~115-135 s game time; budget for update 6 additions ~60-90 s (use `DebugAdvanceHours`, forced bites,
  `SetFightProgress`, one full play per mini-game).

## Extension points (update 6)
- New icons: `EFVIcon` before `Count` + `BuildIcon` case (FVSprites.cpp L785-1513). New item art: `EFVItemArt` +
  `BuildItemArt` case (FVArtItems.cpp L1363). New id kinds: `ItemIcon` resolver L2223.
- New fish/legend art: templates L17-326 (`FishTemplate`, `LegendTemplate`), `RenderFish` L346.
- Expert rods: `BuildRodIcon` L1194 (per-type silhouette needs a new `FFVRodDef` visual field) and `DrawRod`
  FVArtCharacter.cpp L975 + `FFVAppearance::GetHash`.
- New categories: `FVSpriteCat` 21-29 (h L230-237).
- Sheets: `DebugWriteContactSheet` L1597 / `DebugAddHomeSheet` or a `WriteUpdate6Sheet`.
- Sfx: `EFVSfx` + `BuildSfx` case + MinGap/pitch lists; music: `EFVTheme` + `Configure` case + other theme switches.
- Autotest: `AddUpdate6Steps` hooked into `BuildScript` before the balance table (L610); `GrantEverything` L888 for
  expert rods/facts/city goods; `ScreenSlug` (FVAutoTest.cpp L31) for new screens; `StateName` (L63) for new fish states.

## Pitfalls
- Sprite refs point into a TMap reserved for 8192 entries: copy `FFVSprite` by value in long-lived state. Never create
  images per frame; animate with Tint / UV sub-rects / `RotationDeg`.
- `MakeKey` truncates A/B to 24 bits; hash keys can collide: use a distinct category and the C byte for variants.
- Missing `case` for a new `EFVIcon` / `EFVItemArt` / `EFVSfx` compiles fine but shows magenta / beeps: check the sheets
  and the log.
- `ItemPalette` has no free letters; `IconPalette` templates include their own `o` outline, `ItemAscii` templates must not.
- The build has `bUseUnity = false` (FishingValley.Build.cs L9) and 43 .cpp files, but the rule stays: helpers in every new
  .cpp go in a **unique named namespace** (`FVAutoTestU5`, `FVArtItemsImpl` ...), never a second anonymous `Snap()`.
- `FFVAppearance::GetHash` is the character cache key; `EFVBobberStyle` is a cosmetic slot (don't overload it with tackle).
- Adding `EFVTheme` values moves `Count` (the menu-music sentinel and array sizes); scene cache keys pack the theme in 4 bits
  (`(Theme+1)&0xF`, up to 15 themes: currently 8 used). The same key (FVScene.cpp L92) packs the background size in 10 bits per axis: **update-6 large locations wider or taller than 1023 texels alias in the cache** — widen the key (use A/C or a new category) when the size grows.
- Each `Play` spawns a component + procedural wave; high-rate sounds need MinGap or they evict others (12 voices).
- Autotest time is clamped game time; one game hour = 30 s real: always use debug time-advance, never wait.
- `AddScreenShotStep` bypasses proximity gates (`OpenScreen` directly): gate new NPC screens in the world-interaction path,
  not inside `OpenScreen`, or the steps time out.
- HUD buttons move to the right-hand column (§12.1): taps must keep using `GetHudButtonRect`, never literal coordinates; use
  runtime `FV::VW/VH/TopBarH/BottomBarH`.
