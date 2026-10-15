# Code map: core (UFVGame: frame, HUD, screens, modals, toasts, clock, catch, world interaction) — update 6 (CORE-HUD done)

Paths: `Source/FishingValley/`. Files: `Public/FVGame.h` (269), `Private/FVGame.cpp` (3213), `Private/FVGameHome.cpp` (806,
update-2 part of UFVGame), `Private/FVFramework.cpp` (PC/HUD/input), `Private/FVTouch.cpp` (185), `Public/FVTypes.h` (FV
canvas constants, enums, FFVInput). Online (`FVOnline.cpp`) is mapped in screens.md §8. Line numbers are current; re-grep.

## 1. Frame flow
- `AFVPlayerController::BeginPlay` (FVFramework.cpp:169) creates `UFVGame` + `Init`. `PlayerTick` (341): Dt ≤ 0.1,
  `BuildInput()` (229; polled keys `PolledKeys` 56: A-Z, digits, Esc/Tab/Enter/Space, arrows, F1-F12, gamepad) →
  `UFVGame::Tick`. `AFVHUD::DrawHUD` (357) → `UFVGame::Draw` and copies ViewScale/ViewOffset back to the PC (mouse → virtual px
  next frame). Android Back / gamepad B / Start = Escape.
- `UFVGame::Init` (FVGame.cpp:653-780): `-fvautotest` (659) → autotest slot; Sprites (668), Profile = save or new (671-677),
  Audio, FVTextInput, Online (686) + app lifecycle delegates (FCoreDelegates, 689-744), Scene/Fishing (750-751), Touch on
  phones or `-fvtouch` (754, with a cast blocker over world objects 756), loads a backdrop scene (768), opens MainMenu (771),
  AutoTest (776). `Shutdown` 782.
- `Tick` (840-1059), in order: retire old screens, AutoTest->Tick, FVTextInput (typing swallows hotkeys, 856), guard
  (!Scene/!Fishing/!Profile), Touch->Process (879), **`TickWorldInteraction(In)`** (886; FVGameHome.cpp:650), resolve the world
  action (Space or an LMB press that started over the world and not over last frame's UI; touch = action button only; 888-910),
  `FrameInput = In` (911), F11, then modal / screen / world input (919-993): modal → Enter/Esc/choices after
  `ModalInputDelay` .2 s; screen → `Screen->Tick`, Esc closes unless `HandlesEscape()`, hotkeys switch/close screens (not
  from the first-run name setup); world → Esc opens Pause (not while Charging/Waiting), hotkeys (`OpensWhileBusy` screens open
  even while fishing, others via TryOpen). Simulation (995-1008): `bWorldRuns = bInGame && no screen && no modal` →
  `TickClock`, `Scene->Tick(Dt, In, !Fishing->IsBusy())`, `Fishing->Tick`; under menus the scene ticks with idle input.
  Then `FVOnboarding::Tick`, audio reel pitch, `TrackInventoryTab`, autosave (60 s, dirty, not busy, no modal), Online/Audio tick.
- `Draw` (1080-1168): `D.Begin` (clears the whole viewport to black) → if in game and the screen `DrawsWorldBelow()`:
  `Scene->DrawScene`, `Fishing->DrawWorld`, `Scene->DrawLighting`, `Scene->DrawOverlay`, `Fishing->DrawUI`, **`DrawWorldHud`**,
  `FVOnboarding::Draw`; else a flat backdrop. Then the screen (input blocked under a modal, `D.Dim(.45)` when the world is
  below), modal (`DrawModal`), toasts, FVTextInput overlay, touch controls via `D.AfterLetterbox`, `bUIHoveredLastFrame =
  D.WasUIHovered()`, `D.End` (landscape letterbox bars; portrait bleeds).
- **Camera (§2 big locations, update 6):** `Draw` wraps DrawScene..DrawOverlay in `Scene->Begin/EndWorldView` (FVDraw
  translate: drawing and hit tests inside are in WORLD px). `In.Mouse`, `In.WorldTapPos` and every FVTouch position stay in
  SCREEN virtual px; core converts with `Scene->ScreenToWorld` where it asks the scene (`TickWorldInteraction` hover/tap, the
  touch cast blocker in `Init`), and coach marks (FVOnboarding) convert scene answers with `WorldToScreen`. The HUD,
  screens and modals stay unscrolled. The black clear in `FVDraw::Begin` already gives "black beyond the location" in
  landscape; portrait deliberately bleeds (no bars), so a scene smaller than the screen must stop its own background.

## 2. Screens API (FVGame.cpp 2088-2210)
- `OpenScreen(S)` = `OpenScreenWith(S, NAME_None)` (2103-2163): same screen + same context = no-op; factory `switch` 2119-2145
  (add a `case` per new EFVScreen); retires the old screen into `RetiredScreens` (deleted at the next Tick), sets
  `ScreenContext` BEFORE `OnOpen`, clears the LMB latch, plays Open. Ungated (autotest relies on it).
- `CloseScreen` 2165 (no game → MainMenu). `TryOpenFromWorldWith` 2193: refuses with the toast «Сначала закончите рыбалку»
  while `IsWorldBusy()` (fishing state ≠ Idle). World objects and HUD buttons use it.
- `GetScreenContext()` = FName context (trader id, station id, recipe id, "<loc>:<spot>", "keepnet"/"backpack", fish id for
  Collection, "setup"/pid for Profile). A screen that needs richer context encodes it in the FName or reads profile state.
- `ReturnFromMiniGame(StationId)` (FVGameHome.cpp:124): reopens the station screen or closes to the world.
- New EFVScreen values: append before `Count` (FVTypes.h:266-289), add to the factory switch, `FVGameLocal::ScreenName` (560),
  the declaration in `FVScreens.h`, and the autotest shot list if needed.

## 3. HUD (DrawWorldHud 1250-1856; update 6 §12.1 / Р19 / Р22)
- Button table `FVGameLocal::HudButtons[]` (FVGame.cpp:297): {Icon, Screen, Context, Tip, Name}: Карта, Инвентарь, Гардероб,
  Коллекция, Достижения, Учебник, Профиль, **Лидеры** (`EFVIcon::Leaders` → `EFVScreen::Leaderboard`, hotkey L), Меню (always
  last). Constants (309-320): `HudDockCount = 5`, `HudInventoryIndex = 1`, `HudGuideIndex = 5`, `HudProfileIndex = 6`,
  `HudLeadersIndex = 7`, `HudPauseIndex = Num-1`, `HudColumnOrder[] = {Меню, Профиль, Лидеры, Учебник}`, key sizes
  `HudColBtnPortrait 40 / gap 4`, `HudColBtnLandscape 28 / gap 3`. Helpers: `SeaName`/`SeaIcon`/`LocalWeatherText` (local
  weather §8.1), `OpensWhileBusy` (525: Pause, Tutorial, Profile, Levels, Leaderboard), `HotkeyScreen` (534), `HudContext`,
  `DockButton` (166), `ProfileButton` (475; the face crop is centred in big keys), `Badge`, `CountDot`.
- Shared part (1250-1520): XP state (`bMax = Level >= FV::ExpertMaxLevel`; `bXpFrozen = Profile->IsXpFrozen()` → full blue bar
  + a bobbing `EFVIcon::Boat`, tooltip «Опыт на максимуме — плывите в Город»), `GetLocalWeather()` icons (SeaCalm/Wind/Storm/
  Fog + Tide) and tooltip lines, `PressHudButton`, `HudButtonRects.Reset()` + `RegisterHudRect`, lambdas `ColumnButton`
  (one key: profile face / icon, inventory hop + warning dot, guide counter, tooltips) and `DrawColumn` (translucent backing,
  `D.MarkUI`, accumulates `HudColumnRect`).
- **Column visibility** `bShowColumn` (1389) = no overlay screen && `!IsHudColumnHidden()` (expert fight §10.12) && fishing
  state ≠ Minigame (the classic reeling panel may sit at the right edge). Pause stays on Esc / Android «Назад».
- **Portrait** (1531-1733): full-width top strip (coins, level badge + longer XP bar up to 120 px, clock + day, local sea icons
  at the right of row 1, location + food in row 2); the column at `x = VW-44, y = TopBarH+6` (4 keys, 40 px); the bottom
  wooden dock with the 5 main keys (unchanged, hidden under overlay screens) and the inventory badges.
- **Landscape** (1735-1856): 24 px bar without buttons: coins, level + 110 px XP bar, location centred in the free space, food,
  clock, local sea icons at the right edge; two 28 px columns under the bar — outer (right edge) = `HudColumnOrder`, inner =
  the 5 main screens (chosen over «⋯»: every key keeps a HudButtonRects entry for the autotest / coach marks).
- `GetHudColumnRect()` (FVGame.h) = union of the drawn column backings (empty when hidden). The touch UiTest (Init, 830)
  treats it as UI; the portrait onboarding bubble stays left of it; `Draw` clears it every frame (1179).

## 4. Modals and toasts (FVGame.cpp 2215-2330, draw 1661-2083)
- `FFVModal{Title, Body, Buttons, OnChoice(int), Icon, Lines}` (FVGame.h:27). `ShowModal` 2215 queues (one visible at a time),
  `Confirm(Title, Body, OnYes, Yes, No)` 2229, `ChooseModal` 2246 (Esc = last button), `DismissModal` 2268. `DrawModal` 1661
  (portrait card 1678, landscape 1827). Callbacks run after the modal is popped; capture `TWeakObjectPtr<UFVGame>` + ids.
- `Toast(Text, Color, Icon)` 2273: dedupes an identical live toast, max 12 queued, 5 visible, life 3.2 s. `DrawToasts` 1935
  (portrait centered cards 1948, landscape top-right 2045).
- `AnnounceAchievements` 2788 (toast per achievement, legend-skin wording, Online important event).

## 5. Game flow (FVGame.cpp 2334-2622)
- `EnterWorld(Loc)` 2339: falls back to the start location if locked; `Fishing->Reset`, `Scene->LoadLocation`, sets
  `CurrentLocation`, music/rain, location toast, first-run name setup (Profile "setup"), `Online->RequestSync`.
- `NewGame` 2396 (keeps volumes/name; starts at `home`), `ContinueGame` 2428 (CheckAchievements for update-added ones),
  `ReturnToMainMenu` 2455, `SaveNow` 2474 (no-op outside the game), `QuitGame` 2491.
- `TravelTo(Loc)` 2511: locked → toast; else Fishing->Reset, LoadLocation, CurrentLocation, closes Map, music, toast, save.
  Map travel is instant (no boat/transition); a boat trip / city arrival goes here or in the map screen.
- `NotifyProfileChanged` 2552 = CheckAchievements + RefreshAppearance + dirty. Volume/fullscreen 2566-2622.

## 6. Catch handling (FVGame.cpp 2627-2870)
- `ApplyTreasure` 2627: Stats.Treasures++, cosmetic (owned → coins), item → backpack (full → coins), bait, money, one summary
  toast «Сокровище: …». **Hook for treasure fish facts.**
- `HandleCatch(Result)` 2689: fish → `RecordCatch`, `AddToKeepnet`, CatchesByLocation, BestCatch, PendingSeasonBest; junk →
  backpack; full-keepnet/backpack toasts; XP + level-up modal with `UnlocksAtLevel`+`HomeUnlocksAtLevel` lines (update 6:
  levels > ClassicMaxLevel get the expert body text; a frozen XP adds «Опыт на максимуме — плывите в Город»; a catch with XP
  while frozen toasts it once per session, `GXpFrozenToastShown`); achievements; Online important event on level/legend; `SaveNow`. Do not route passive (net/artel) fish through
  it (toast floods, disk writes, XP).
- `HandleEscape` 2837, `MakeCatchContext(Depth)` 2846-2870: location from the scene, phase, weather, level, `&GetRod()`,
  bait, ExtraLuck/JunkMultScale from `GetEffectiveRodStats` (FVGameHome.cpp:508), line/float/LineTier from the loadout.
  **Expert-mode fields (rod type, config, local weather) are filled here.**

## 7. Clock, weather, days (FVGame.cpp 1061, 2875-2952; FVGameHome.cpp 142-405)
- `TickClock` 1061: frozen in the Minigame state; else `AdvanceClock(Dt / FV::MinuteSeconds)` (0.5 s per game minute).
- `AdvanceClock(Minutes, bQuiet)` (FVGameHome.cpp:180-254): midnight by midnight (≤30 days, 128 steps): `SimulatePassive`,
  `ExpireOrders` (toasts or SleepLog), torn-net / smoke-ready toasts via `FPassiveSnapshot` (105), `StartNewDay` at 1440.
- `StartNewDay` (256-300): Day+1, Weather = WeatherTomorrow, new forecast (30% rain), `Profile->OnNewDay` lines (toasts or
  SleepLog), rain audio, `Scene->RefreshWorldObjects`. **Artel morning collection / city task refresh plug in via OnNewDay.**
- `ConfirmSleep` 302, `SleepUntilMorning` 333-404 (quiet AdvanceClock to 06:00, summary modal from SleepLog, save).
- Getters: `GetClockMinutes` 2875, `GetPhase` 2880, `GetWeather` 2889 (global `Profile->Weather`; per-location weather for
  City/Cave §8.1 would wrap this with the scene location), `GetDay`, `GetClockText`, `GetNightFactor` 2912, debug setters
  `SetClockMinutes` 2930 (no rollover), `SetWeather` 2939. `GetAbsMinutes` (Home 142), `AbsTimeText` 156, `DurationText` 164.

## 8. World interaction (FVGameHome.cpp)
- Helpers `FVGameHomeImpl` (12-120): `ObjectKey`, `FindByKey`, `FindKind`, `VerbFor` (51: Russian verb per object kind),
  `LabelOf`. `IsAtHome`/`IsInHouse` 409/414, `MusicThemeFor` 419, `LoadHomeScene` 429, `EnterHouse`/`ExitHouse` 456/470,
  `IsWorldBusy` 487, `GetNearbyTrader` 492 (stall in reach).
- `Interact(Obj)` 526-648: switch on `EFVObjKind` (FVTypes.h:658): Resource (Gather + pickup fx), Stall → Shop(trader id),
  OrderBoard → Orders, NetSpot → NetSpot("<loc>:<spot>"), DigSpot → Digging, Door → Enter/ExitHouse, Station → Bed
  (sleep) / Chest / IceCellar / Station screen, DecorCatalog → HomeDecor. **New NPCs (artel master, city task board, foreman,
  boutique) = new EFVObjKind values + cases here + objects from FVScene.**
- `TickWorldInteraction(In)` 650-775: swallow the rest of a used click, walk-to target (`PendingInteractKey`, interact on
  arrival, cancel on manual move/stuck), hover/in-reach object, input rules (desktop LMB on object or anywhere with an object in
  reach; touch short tap; E/RMB/gamepad X nearest), `Scene->SetHighlight`, `InteractHint`. Update 6: `ObjectAt` gets
  `Scene->ScreenToWorld(In.Mouse / In.WorldTapPos)`.

## 9. Conventions
- Everything player-facing is Russian; plurals via `FVGameLocal::RuPlural` (51) / `FVUI::Plural`. Colours via `FV::Col`.
- Layout must work in landscape 640x360 and portrait 360x(600..900); read `FV::VW/VH/TopBarH/BottomBarH` every frame
  (`FV::UpdateCanvasForViewport`, FVTypes.h:47, runs in `FVDraw::Begin`, i.e. AFTER Tick). Portrait sizes via `FV::Ui::*`.
- Private additions go to FVGame.h's private blocks additively; FVGame.cpp helpers live in `FVGameLocal`, FVGameHome.cpp in
  `FVGameHomeImpl` (unity build). An update-6 core file (e.g. `FVGameCity.cpp`) needs its own namespace.

## 10. Pitfalls
- Tick-side code sees the previous frame's canvas size (portrait/landscape is decided in Draw).
- A screen opened from Tick on an LMB press receives the same `FrameInput`; widgets click on RELEASE (`FVDraw::Clicked`) and
  require the press to start on them, so this is mostly safe, but `PressedIn` widgets (sliders, mini-games) would fire.
- After OpenScreen/CloseScreen from inside a screen, return at once (the screen is retired, `this` stays alive until next Tick).
- `HandlesEscape()=true` screens also stop global hotkeys (they are handled only when !HandlesEscape()).
- The clock stops under screens/modals and during the minigame: passive systems do not advance in menus.
- Autosave is deferred while fishing or a modal is up; `SaveNow` is a no-op outside the game. Big one-off state changes
  (hire, move-in, boat repair) should call `SaveNow` explicitly.
- `HudButtonRects` is rebuilt each frame from what was drawn: a button hidden in one orientation has no rect there (autotest
  taps Map; keep it in both).
- Four input combos must work: landscape/portrait × mouse/touch (`-fvtouch` on desktop; portrait with a mouse via a tall window).
- Weather reroll uses unseeded `FMath::FRand` in StartNewDay; tests use `SetWeather`.

## 11. Update 6 (CORE-HUD) notes
- **Repeat swipe (§12.2)**: `FVDraw::CancelTap()` now sets `bTapCancelled` (checked by `Clicked`, reset on press) instead of
  `bDragGesture`, so `UpdateGesture` still assigns `GestureAxis` when the finger moves on — a tap that stops a gliding
  `FFVScrollView` no longer blocks the drag, and an `FFVPager` over it can still take a horizontal swipe. `IsDragGesture()`
  = moved OR cancelled (no pressed look after a cancel); `IsMovedGesture()` / `IsTapCancelled()` tell them apart.
- **Onboarding** (FVOnboarding.cpp): player / head / pond spot / door are converted with `WorldToScreen`; a landscape
  column key is pointed at from the left (`ObArrowRight`).
- Sleep limit (Р44, §0.2) in FVGameHome.cpp (`U6.SleepStreak`, AdvanceClock 18:00 reset, ConfirmSleep modal) is the
  integrator's code — keep it as is.
- Not verified on screenshots (the integrator runs the game). By the layout tables (FVSceneArt.cpp ~3550-3620) the landscape
  columns (x ≥ VW-67, y 26..184) overlap the right end of the pond stall (feet 566,140) and the ocean stall (580,138) and the
  right-edge resources (x 612-620) of every classic location; they stay usable by proximity («tap anywhere» / E), but the
  anchors should move ~60 px left in landscape (request to SCENE-CAMERA). Portrait column: x ≥ VW-47, y 35..213.
