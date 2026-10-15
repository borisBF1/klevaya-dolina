# Code map: fishing and mini-games

Root: `C:/UEProjects/FishingValley/Source/FishingValley`. Refreshed for update 6 (state after 0.5 «Легенды долины»; line
numbers checked 2026-09-26). Files: `Public/FVFishing.h` (245 lines), `Private/FVFishing.cpp` (3311),
`Private/FVMiniGames.h` (79) + `Private/FVMiniGames.cpp` (3824). Spec hooks for update 6: §10.12-10.13 (expert fight
module, per-rod screen themes Р32), §10.16 (hidden fish facts Р33, journal pages from treasure), §8.3 (nets, Р35-Р36).

## 0. Ownership and call graph
- `FVFishing.h`: `EFVFishState` (L12-23: Idle, Charging, Casting, Waiting, Bite, Minigame, Showcase, Escaped, Retrieving),
  `struct FFVMinigame` (L29-143, pure logic + legend state), `FFVBotProfile` (L146-155: HumanMid {18,6%,6},
  HumanStrong {13,3%,9}, Bot085), `FFVLegendSimSetup/Result` (L158-174), `FVSimulateCatchRate` (L177),
  `FVSimulateLegend` (L181), `class FVFishing` (L183-245). All runtime state is in the pimpl `FVFishing::FImpl`
  (cpp L1203-1886).
- Public API: `Reset`, `Tick(Dt, FFVInput)`, `DrawWorld`, `DrawUI`, `GetState`, `IsBusy()` (= State != Idle), `GetHint`;
  debug/autotest: `SetAutoPlay`, `GetMinigame`, `DebugForceBite()` / `DebugForceBite(FishId, bApplyGates)`,
  `IsLegendIntro`, `DebugPinProgress`, `DebugDrainMeter`; practice: `GetGiveUpRect`, `IsUiAt` (touch: a finger on the [X]
  is a UI pointer, FVGame.cpp L758); dev legend trial: `StartLegendTrial`, `GetPendingTrial`, `GetTrialScore`,
  `TrialSetupText` (static).
- UFVGame drives it (FVGame.cpp): `Scene->Tick(Dt, In, !Fishing->IsBusy())` then `Fishing->Tick` (L1000-1001, only while
  the world runs: no screen/modal). Draw: `Fishing->DrawWorld` (L1107, under lighting) ... `Fishing->DrawUI` (L1110,
  above the night tint, below the HUD). Reel sound follows `GetState()==Minigame` (L1015, L1068). `Fishing->Reset()` on
  travel/new game/menu (FVGame.cpp L2364, L2460, L2528; FVGameHome.cpp L340, L435).
- Busy gate: `UFVGame::IsWorldBusy()` (FVGameHome.cpp L487) = `Fishing->IsBusy()` only. Used by the HUD dock (FVGame.cpp
  L1458), TryOpen (L2199), house door (FVGameHome.cpp L458/L472), interaction (L546/L618/L669). **A new world activity
  that must block walking/menus ORs itself in here.**
- Callbacks into UFVGame: `MakeCatchContext(Depth)` (FVGame.cpp L2846-2867), `HandleCatch(FFVCatchResult&)` (L2689-2836:
  record, keepnet/backpack, `ApplyTreasure` L2627-2687, XP, achievements, `SaveNow`, online notify), `HandleEscape`
  (L2837, Stats.Escaped++), `GetEffectiveRodStats()` (FVGameHome.cpp L508: `FVDB::ComposeRodStats(rod, line, float,
  &food)`), `Toast`, `SaveNow`.
- FVDB used (FVDatabase.cpp): `IsFishAvailable` L1247, `PickCatch` L1256-1373, `RollBiteDelay` L1374, `RollSizeFrac`
  L1384, `RollQuality` L1398, `RollTreasure` L1419, `LegendGate` L1698, `ComposeRodStats` L1765, `CatchBarHeight` L1779
  (`96 + LevelBarBonus(Level) + Stats.BarBonus`, shared with the autotest balance), `FishPrice`, `CatchXp`, `JunkXp`.

## 1. State machine (`FVFishing::Tick`, cpp L1998-2230)
Input: `bDown/bPressed/bReleased` = `In.bAction*` (UFVGame resolves Space / LMB over world / touch Action); autoplay
replaces them with `FImpl::BotHold` edges (L2013-2020). `bEsc` read at L2022.

| State | Behaviour |
|---|---|
| Idle (L2026) | Only on `bPressed`. `!Scene.HasWater()`: home-yard pond -> `StartCharging(true)` (practice), else a toast (4 s cooldown `LastAwayToast`). `!CanFishFromHere()`: toast on Space only. `KeepnetFull()` (L2054) -> toast + Error sfx. **Update 6: the pre-cast check for expert rods (wrong type/config, Р18) goes next to this keepnet check.** Then `StartCharging()` (L1377) -> `ReadStats()` (L1372: `Cur = Game->GetEffectiveRodStats()`, one snapshot per cast). |
| Charging (L2064) | ESC -> Idle; triangle-wave `Power`; release -> `DoCast()` (L1390: `CastTo = Scene.GetCastTarget(Power, Cur.CastRange)`). |
| Casting (L2073) | Arc lerp; end -> `Land()` (L1423): not water -> retrieve; `BiteDelay = clamp(RollBiteDelay * Cur.BiteSpeed * Bait.BiteSpeed, 0.8, 40)` (practice: 2-4 s). |
| Waiting (L2081) | ESC/press -> retrieve; nibbles/ripples; `StateTime >= BiteDelay` -> `Bite()`. |
| Bite() (L1449-1547) | Practice: fixed window. Else `Ctx = Game->MakeCatchContext(Depth)`; `PickCatch(Ctx, Rng, Fish, Junk, &Gated)`; `ForcedFish` (autotest) / `TrialFish` (dev) override *after* the roll (Rng stream unchanged); gated-legend toast; soft rule -> `bPendingSnap`; fallback random junk; **bait consumed here** (`ConsumeBait`, not in trials) for fish and junk, even if the player then misses. Angler: `GlowTime` sub-state. React window: legends `GlowTime + max(MinReact, ReactBase(L) + Cur.ReactBonus)`, else `max(0.4, (Common ? 1.25 : 1.0) + Cur.ReactBonus)`. |
| Bite (L2105) | Junk auto-hooks -> `FinishJunk`; fish: press -> `Hook()`; timeout -> «Рыба ушла…». |
| Hook() (L1563-1634) | `BarH = FVDB::CatchBarHeight(Level, Cur)`; trial uses `FVTrialImpl::Stats` (L195-224); practice: easy fish, +32 bar, `bNoEscape`; else treasure roll `Loc->TreasureChance + Cur.TreasureBonus` (always rolled, keeps Rng), `Mini.Start(..., Fish->Legend)`; legend-rule fights drop the chest. Legend: `IntroT = 0.7`, banner «Легенда на крючке!», `MarkLegendMet` + SaveNow. **Update 6 expert branch goes here** (spec §10.13: `bExpert`, start `FFVExpertFight` instead of `Mini`, the state stays `Minigame`). |
| Minigame (L2129-2202) | Practice [X] give-up; legend intro freeze (no Step; snap at its end -> `SnapLeader`); fixed-step loop `Accum += Dt; while (Accum >= FV::SimDt && Steps < 8) Mini.Step(bDown)` (L2156-2171) with the `PinnedProgress` override; `LegendFeedback()`; treasure pop; ripples; result -> `FinishPractice` / `FinishTrial` / `FinishFish` or `Escape()`. |
| Showcase (L2203) | After 0.4 s press/LMB/ESC -> `ToIdle` (practice releases the fish). |
| Escaped (L2213) | 0.8 s -> retrieve. Retrieving (L2218): eased lerp to the rod tip -> Idle. |

Finishers (FImpl): `EnterShowcase(Sprite)` L1709 (HoldFish anim, held sprite, +XP text, sparkles), `FinishFish` L1745
(size `RollSizeFrac(...) + Cur.SizeBonus`, quality, XP, price, treasure `RollTreasure(Loc, Level, Rng)` if
`Mini.bTreasureCaught`, then `Game->HandleCatch(Res)`), `FinishPractice` L1777, `FinishTrial` L1792, `FinishJunk` L1826,
`Escape(Text)` L1846 (calls `HandleEscape` unless trial/practice), `BotHold` L1873.

## 2. FFVMinigame (classic + legend fight): do NOT change behaviour
- `Start(...)` cpp L265; `Step` L344: first line `if (HasLegendRules()) return StepLegend(bHold);`; the classic body is
  bit-exact with `Saved/Logs/FV_balance_reference.log` (the autotest BALANCE step compares every line; drift = WARNING).
- Legend: `StartLegendState` L514, `StepLegend` L563, `StepLegendHazards` L691, `StepLegendFish` L888, `StepLegendFog`
  L963, `LegendBotHold` L1065. Numbers in `namespace FVLegendFight` (L59-190: `Gain`, `ReactBase`, `WarnPitch`, AnGlow,
  IntroFreeze, MinReact, BannerLife...). Legend rolls use `LegendRng` only.
- Bots: `BotShouldHold(Skill)` = `BotShouldHoldLook(3 + 9*Skill)` (L467-485). Sims: `FVSimulateCatchRate` L1106,
  `FVSimulateLegend` L1138 (press-delay queue, self-prediction, noise).
- Constants: `FV::TrackH = 568`, `FishMaxPos 532`, `FishTargetMax 548`, `InBarMargin 10` (cpp L51-53), `FishIconH 32`.
- **Update 6 expert fight** is a separate module (spec §10.13: `Public/FVExpertFight.h`, `Private/FVExpertFight.cpp`,
  drawing in `Private/FVExpertDraw.cpp`). Copy this split: pure POD `Start(Setup, Seed)` / `Step(Input)` at 60 Hz /
  `BotInput`, own RNG streams, a headless `FVSimulateExpert` for BALANCE_EXPERT; FImpl owns the Accum loop (cap 8 steps).

## 3. Drawing (`DrawWorld` L2236-2352, `DrawUI` L2638-3265)
Helpers (anon namespace L12-54): `Sfx`, `Reeling`, `WithAlpha`, `Snap`, `MeterColor` (red->yellow->green), `HeldIconH`.
`namespace FVLegendDraw` L2353-2637: `Star`, `TimerRing`, `Environment`, `BarExtras`, `Markers`, `Warning`, `Pictogram`,
`Banner`. `FVFishingPracticeImpl::WrapLines` (L240) is a greedy word-wrap helper.
- DrawWorld: bobber position, sagging line from `Scene.GetRodTip()`, bobber sprite `Sp->Bobber(Profile->GetBobberStyle(),
  Frame)` (L2298-2317, cosmetic slot), the Angler lantern -> `I.LanternPos/LanternGlow`.
- DrawUI order: lantern glow (L2648) -> power meter (L2654, wood frame 52x12 / 76x16) -> bite «!» (L2679) ->
  **minigame panel (L2695-2912)** -> practice card (L2914) -> showcase card (L3015-3264).
- **Minigame panel = the classic brown wooden screen (Р32 keeps it for classic rods):**
  - Layout L2698-2733: `K = portrait ? clamp((AreaBottom-AreaTop)/300, 1, 1.2) : 1`; `PW = (legend ? 50 : 44)*K`,
    `PH = 300*K`; landscape beside the player (±38), portrait on the roomier side (gap 40); intro slide 24 px; shake.
  - Wooden frame L2735-2749: shadow, `FVPal::WoodDark/Wood/WoodLight`, grain every 17K at `GrainX`, 4 grey nails.
  - Track L2751-2786: `(P.X+7K, P.Y+10K, (legend ? 26 : 20)*K, P.H-20K)`, 8 bands `Loc->WaterShallow -> WaterDeep`,
    bubbles; legend `Environment` clipped to the track; `S = Track.H / FV::TrackH` maps track units to px.
  - Bar L2788 (gold for legends + `BarExtras`), chest `Sp->Chest(0/1)` + treasure meter L2808-2825, legend `Markers`
    L2827, fish icon L2830-2850, progress meter L2854 (`P.X + (legend ? 38 : 32)*K`, 6K wide), reel handle L2863-2870
    (`RC = (P.X-3K, P.Bottom-28K)`), legend phase flash / `Pictogram` / `Banner` L2872-2886, practice [X] L2888-2906.
  - **Theming hook:** every colour here is a hard-coded `FVPal::Wood*` / `FV::Col`. For per-rod skins either (a) extract
    a `struct FFVFightSkin { Frame, FrameDark, FrameLight, Grain, Nail, TrackBorder, BarIn, BarOut, Meter... }` chosen from
    the equipped rod and thread it through this block (classic = current values, byte-identical look), or (b) draw expert
    fights entirely in `FVExpertDraw.cpp` (spec §10.13, recommended: the square 240x240 field is unlike this vertical
    track). Warning colours (jerk red, tension yellow->red) stay theme-independent (§10.12).
- Showcase card L3015-3264: badges `TArray<FBadge{Text,C}>` (L3035-3059: perfect, new species, record, treasure contents
  L3040-3051, level, backpack/keepnet), `FCardStyle` + `Measure` + portrait fallback levels (L3061-3130), placement above
  or below the held fish (portrait) or beside the player (landscape). **Journal-page / fish-fact rewards (Р33) add a
  treasure part here** and a toast in `ApplyTreasure`.
- `GetHint()` L3266: per-state Russian hints (touch variants), legend texts; UFVGame renders it in the portrait dock and
  the landscape bottom bar.

## 4. Treasure, catch data, facts (Р33 hooks)
- `FFVTreasure` (FVTypes.h L547-555): Money, BaitId/BaitCount, CosmeticId, ItemId/ItemCount. `FFVCatchResult` L558-574.
- `FVDB::RollTreasure(Loc, Level, Rng)` (FVDatabase.cpp L1419): coins x `LocMult[] = {1, 2, 3.5, 6, 9, 14}` indexed by
  `LocationIndex(Loc.Id)` **clamped to index 5** (new locations get the Volcano multiplier unless the table grows),
  ~35 % bait, ~6 % treasure cosmetic, then items. A "страница рыбацкого журнала" fact = a new field (e.g. `FName FactFish;
  int32 FactIndex`) rolled here for expert fish only (§10.16: 35 % of chests), applied in `UFVGame::ApplyTreasure`
  (FVGame.cpp L2627: add to the learned-facts set, toast) and listed in the card (FVFishing.cpp L3040-3051).
- Hidden conditions in expert mode are a UI concern (collection/guide screens) plus save data (`FishId:FactIndex`). The
  fishing code must only *not* leak them: `ShowGatedToast` (L1550, `Prep.GatedToast`) and `SnapLeader` (L1644,
  `Prep.SnapText`) print legend-rule hints; gate them for expert legends until the fact is known.

## 5. PickCatch (FVDatabase.cpp L1256-1373)
1. Luck = `Rod->Luck + Ctx.ExtraLuck`; RareBonus from the bait; `AffinityBait` (lures/dough), `LegendBait` (update 5).
2. Junk chance `Loc.JunkChance * (Bait ? JunkMult : (bNet ? 1 : 1.15)) * JunkMultScale * (1 - 0.45*Depth)`, clamped to
   [0.02, 0.75].
3. Available: fish of this location with `IsFishAvailable(F, PhaseMask, Weather, Level)` (L1247: time mask, Sunny/Rain
   requirement, MinLevel); nets skip legendaries; legends failing a hard `LegendGate` are skipped (the first reported via
   `OutGated`).
4. Weights L1317-1337: legendary `(1+0.5*(Luck+Rare))*(0.75+0.5*Depth)`; Rare+ `(1+Luck+Rare)*(...)`; Common
   `1.15-0.3*Depth`; legend-bait / lure-affinity multipliers. Fallback: the easiest fish, then junk.
- **Update 6 hooks:** filtering expert fish by rod *type* (universal vs type fish, Р23) and the 5 % expert-fish chance on
  classic rods (Р25) belong here via new `FFVCatchContext` value fields (FVTypes.h L497-515; never point `Ctx.Rod` at a
  temporary). Local weather of City/Cave (§8.1) enters through `Ctx.Weather` in `MakeCatchContext`. `EFVWeatherReq`
  only knows Sunny/Rain: a "storm" condition needs a new weather value handled in `IsFishAvailable`.
- Nets: `FVSaveGameHome.cpp` `NetCatchOne` (~L283-300, `Ctx.bNet = true`, seeded `NetSeed(Loc, Spot, Minute, salt)`);
  `NetSpotCount(Loc)` (~L265) returns 2 for pond/river/ocean/swamp, 1 for mountain, **0 for any other id**, so new expert
  locations get no net spots automatically (Р35); forest spots must be added there. API: `UFVSaveGame::PlaceNet` L1128,
  `CollectNet` L1148, `NetRepairCost` L1178, `RepairNet` L1190, `RemoveNet` L1208, `DestroyNet` L1225. The hired net
  worker (Р36) should drive these, never `HandleCatch`.

## 6. Mini-games (Private/FVMiniGames.h / .cpp)
- Header: `FFVLureWorkshop` (the lure panel embedded in the workshop «Блёсны» tab of FVScreenStation.cpp; pimpl
  `FVMiniGamesImpl::FLureBench`), `FFVMiniGameDebug` + `FVMiniGameDebug()` (autotest-only readouts: cooking set-up,
  digging timer/penalty/empty hole). Factories (cpp L3795-3798): `FVMakeCookingScreen`, `FVMakeLureCraftScreen`,
  `FVMakeKnotsScreen`, `FVMakeDiggingScreen`; opened via `Game->OpenScreenWith(EFVScreen::Cooking|Knots|Digging|LureCraft,
  Context)`. Context = the recipe id (cooking/knots) or `"<loc>:<spot>"` (digging).
- `namespace FVMiniGamesImpl` (L18-3785): helpers L18-88 (`Snap`, `Smooth01`, `EaseOutBack`, `Tri01`, `Hash01`,
  `MeterColor`, inks `InkGood/InkBad/InkGold`, rope colours, `ItemIconPtr`, `ConfirmPressed`, `ScoreToQuality`:
  <0.45 Normal, <0.75 Silver, <0.93 Gold, else Iridium), `FDirInput` L89, particles `FParticle/FFloater/FJuice` L291-441,
  result card `FCardLine/FCardItem/FResultCard` L446-660.
- **Base `class FMiniGame : public FVScreen`** (L667-905): window + footer (Назад, actions, hint), quit confirmation,
  result card, banner, juice; `bBot = Game->IsAutoTest()` (every mini-game plays itself under the autotest). Virtuals:
  `Title`, `ShotSlug`, `TickGame`, `DrawGame(D, Area)`, `DrawActions`, `Hint`, `QuitNeedsConfirm/QuitTitle/QuitBody`,
  `OnCardButton`. Helpers: `Leave()` (-> `Game->ReturnFromMiniGame(ReturnStation)`), `RequestBack`, `Banner`,
  `ShowCard(C, Sfx)`, `OpenFailed(Reason, Icon, Title)`, `RequestPlayShot`. Esc is handled by the base.
- `FCookingScreen` L906-2061 (returns to `st_kitchen`): `OnOpen` L911 (recipe from the context, `CanCraft`, ingredient
  fish, cuts = `GameDifficulty`, vessel by `Dish->Art` L927-937: Pan/Pot/Kettle/Oven, default Pan with fish else Pot),
  `ApplyResult` L1390 (`P->Craft(*Recipe, 1, Q)`, food-buff lines). **New dishes (Р8 carrot soup, city-order items) need
  a vessel case for their new `EFVItemArt`.**
- `FKnotsScreen` L2062-2470 (`st_workshop`): an arrow pattern of `GameDifficulty` (3-9) steps; `ApplyResult` L2258
  (lines, floats and nets get stat lines).
- `FLureBench` L2471-3048 (lure picker; `Complete` L2729 -> `P.CraftLure`), `FLureCraftScreen` L3049-3127.
- `FDiggingScreen` L3128-3785: context `loc:spot`; needs `ShovelLevel >= 1`; once per day (`IsDugToday/MarkDug`); timer
  `P->DigSeconds()`; loot `FVDB::RollDigLoot(Loc, ShovelLevel, Rng, Count)` (L3421); `Finish` L3467 (baits -> `AddBait`,
  items -> `GiveItem`, lost when the backpack is full).
- Adding a mini-game: subclass `FMiniGame` here (or in a new `.cpp` with its own named namespace), add an `EFVScreen`
  value + a factory case in the `OpenScreen` switch (FVGame.cpp ~L2120) + `ScreenSlug` in FVAutoTest.cpp (L31),
  give it a `bBot` branch and call `RequestPlayShot()` mid-game.

## 7. Autoplay / autotest touch points
- `FImpl::BotHold` L1873-1885 (Idle requires `S.IsHome() || !P.KeepnetFull()`; Bite waits `GlowTime + 0.12`; Minigame
  waits for the intro). New states or fight kinds need a branch here and in `FVAutoTestLocal::StateName` (FVAutoTest.cpp L63).
- The autotest drives fishing via `SetAutoPlay`, `DebugForceBite(FishId, bGates)` and `DebugPinProgress`
  (`FVAutoTest::SetFightProgress`). See artaudiotest.md §7.

## Extension points (update 6)
- Expert fight: branch in `Hook()` (L1563) + the Minigame case (L2129) + the DrawUI panel block (L2695); keep
  `State = Minigame` so busy, clock freeze, Esc and reel sound work unchanged; new module files per spec §10.13.
- Per-rod fight skin (Р32): a skin struct selected from `Profile->GetRod()` (new `FFVRodDef` field, e.g. `uint8
  ExpertType` + `FName Skin`), consumed by `FVExpertDraw.cpp`; the classic panel code stays untouched.
- Pre-cast checks (expert rod in a classic pond Р18, wrong config): the Idle case near L2054.
- Catch table: `PickCatch` + `FFVCatchContext` value fields; `MakeCatchContext` FVGame.cpp L2846.
- Treasure facts: `RollTreasure` (FVDatabase.cpp L1419) + `ApplyTreasure` (FVGame.cpp L2627) + the card (L3040).
- Legend-hint gating in expert mode: `ShowGatedToast` L1550, `SnapLeader` L1644, `GetHint` L3266.
- +1 classic fish per location (Р39): data only (FVDatabase fish table + art template/colours); PickCatch picks them up
  automatically; the BALANCE table gains lines, so regenerate `FV_balance_reference.log` (otherwise WARNING).
- Busy: `UFVGame::IsWorldBusy` FVGameHome.cpp L487.

## Pitfalls
- Classic and legend fights must stay bit-identical (BALANCE and BALANCE_LEGEND references). Do not add Rng draws to
  `FFVMinigame`, or change the draw order in `Bite()` / `Hook()` for classic fish; put new randomness on separate streams.
- `Bite()` consumes bait for any bite (fish or junk) before the hook; Р25/Р27 keep that rule for expert fish.
- `HandleCatch` does XP, the level-up modal, achievements and `SaveNow` per call: never call it per net fish.
- `FFVCatchContext::Rod` is a raw pointer to the static def; pass extra effects as value fields.
- `RollTreasure` LocMult has 6 entries (clamped); `NetSpotCount` is keyed by lower-case location id strings.
- `Cur` (effective stats) is read once per cast (`StartCharging`); switching rods must happen in Idle.
- The panel and card read `FV::VW/VH/TopBarH/BottomBarH` every frame; `SyncCanvas()` (L1298) remaps the bobber after a
  rotation. With the update-6 camera / large locations, the panel and card are in **screen** space but position from
  `Scene.GetPlayerPos()` / `GetHeadTop()` / `GetRodTip()`: those must return screen-space points (or be converted).
- Desktop LMB over the world and a touch hold (0.22 s) near water start a cast: world taps on NPCs/objects must be consumed
  first (MarkUI or clearing `In.bAction*`).
- ESC during Charging/Waiting cancels the cast; otherwise UFVGame opens Pause. An expert fight inherits this.
- The right-hand HUD column (§12.1) is drawn above `DrawUI`: fight panels must avoid `HudButtonRects` (or the column
  hides during fights, as the spec says).
- `JunkXp()` uses the global random stream (not seeded).
