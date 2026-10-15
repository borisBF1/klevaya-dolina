# Code map: screens (FVScreen*, FVDraw, FVUIKit, shared UI helpers, FVOnline) — 0.5, for update 6

Paths: `Source/FishingValley/`. Screen plumbing lives in UFVGame (see core.md §2). Line numbers are current; re-grep.

## 0. Files
| File | Lines | Screen(s) / role | Class (namespace) |
|---|---|---|---|
| Public/FVScreens.h | 63 | `FVScreen` base + `FVMake*Screen` factories | — |
| Public/FVDraw.h / Private/FVDraw.cpp | 266 / 1585 | IMGUI renderer + widgets + gestures | `FVDraw`, `FVPal` |
| Private/FVUIKit.h / .cpp | 142 / 353 | kinetic scroll, swipe pager, tab strip | `FFVScrollView`, `FFVPager`, `FVKit` |
| Private/FVScreenCommon.h | 679 | inline helpers for all screens | `FVUI` |
| Private/FVScreenHome.h | 707 | inline helpers of Station/Orders/Net/Decor (+Storage) | `FVHomeUI` |
| FVScreenShop.cpp | 3722 | trader stall (context = trader id) | `FShopScreen` (anon + `FVShopScreenImpl`) |
| FVScreenInventory.cpp | 1306 | «Инвентарь»: keepnet / backpack pages + gear | `FVInventoryScreenImpl::FVInventoryScreen` |
| FVScreenMap.cpp | 1768 | world map, travel, unlock | `FVMapScreenImpl::FVMapScreen` |
| FVScreenMenus.cpp | 2672 | main menu, pause/settings/help | `FVMenusScreenImpl::FVMainMenuScreen/FVPauseScreen` |
| FVScreenCollection.cpp | 1419 | fish collection + achievements | `FCollectionScreen`, `FAchievementsScreen` (`FVCollectionScreenImpl`) |
| FVScreenWardrobe.cpp | 1310 | wardrobe (4 cosmetic slots) | `FWardrobeScreen` (`FVWardrobeScreenImpl`) |
| FVScreenStation.cpp | 3008 | house station (context = station id) | `FVStationScreenImpl::FStationScreen` |
| FVScreenStorage.cpp | 640 | Chest (backpack↔chest), IceCellar (keepnet↔cellar) | `FVStorageScreenImpl::FStorageScreen` |
| FVScreenNet.cpp | 596 | net spot (context "<loc>:<spot>") | `FVNetScreenImpl::FNetScreen` |
| FVScreenOrders.cpp | 538 | order board | `FVOrdersScreenImpl::FOrdersScreen` |
| FVScreenDecor.cpp | 479 | house decor catalog | `FVDecorScreenImpl::FDecorScreen` |
| Private/FVDecorPreview.h/.cpp | 27 / 726 | Р61 «Примерка»: real yard / big-house fragment around a decor slot (scene chunks, decor, stations), used by Builder, Shop (decor), Decor | `FVDecorPreview` |
| FVScreenTutorial.cpp | 2060 | «Учебник» guide + `FVTutorialUnseenCount` | `FVTutorialScreenImpl` |
| FVScreenLevels.cpp | 792 | «Уровни рыбака» | `FVLevelsScreenImpl::FLevelsScreen` |
| FVScreenProfile.cpp | 1577 | profile (None / "setup" / "<pid>") | `FVProfileScreenImpl::FProfileScreen` |
| FVScreenLeaderboard.cpp | 1518 | leaderboards + tournament | `FVLeaderboardScreenImpl` |
| FVScreenMail.cpp | 967 | mail + claims | `FVMailScreenImpl::FMailScreen` |
| FVScreenRestore.cpp | 544 | restore save by code | `FVRestoreScreenImpl` |
| FVMiniGames.cpp (not mapped here) | — | Cooking, LureCraft, Knots, Digging | — |

## 1. Screen skeleton
- `FVScreen` (FVScreens.h:12-30): `OnOpen`, `OnClose`, `Tick(Dt, In)` (not called while a modal is up), `Draw(D)=0`,
  `HandlesEscape()` (true = the screen handles Esc AND global hotkeys are off), `DrawsWorldBelow()` (false for full-screen).
  `Game` = `UFVGame*`. Read the context in `OnOpen` via `Game->GetScreenContext()`.
- Template (all update 3+ screens): `const FFVRect Win = FV::WindowRect();` (FVTypes.h:129: portrait `(6,20,VW-12,VH-28)`,
  landscape `(10,30,620,318)`), `if (D.Window(Win, Title)) { UFVGame* G = Game; G->CloseScreen(); return; }`; tabs =
  `FFVPager` + `FVKit::TabStrip` (or `FVHomeUI::PagerTabs`); lists = `FFVScrollView` + `PushClip`; portrait: content ends at
  `FVDraw::ActionBarRect(Win).Y - 10`, `D.ActionBar(Win, &Actions)` draws «← Назад», split the rest with `FVDraw::SplitH`.
  Draw the action bar LAST (it may close/replace the screen). Font sizes: `FV::Ui::Body/Small/Header/Title/ButtonH/RowH/Slot`.
- Profile mutations → `Game->NotifyProfileChanged()` (FVHomeUI::Refresh does it) + feedback (`FVHomeUI::Success/Fail`,
  `FVUI::ReportBuy`, `Game->Toast`). Confirm lambdas capture `TWeakObjectPtr<UFVGame>` + ids, never `this` or indexes.
- Keep selections by identity (id + quality + flags [+ container]), not by array index: containers change under the screen.

## 2. FVDraw (Public/FVDraw.h, Private/FVDraw.cpp)
- Frame: `Begin` (cpp 215: copies input, `UpdateGesture`, `FV::UpdateCanvasForViewport`, Scale/Offset, clears the viewport to
  black), `End` (257: landscape letterbox bars, `AfterLetterbox`, deferred tooltip, hover sfx). Coordinates are virtual px.
- Primitives (427-576): Rect, Frame, Line, Sprite/SpriteCentered/SpriteRect/SpriteAdditive, Texture, Glow, Circle, Dim, Ring,
  GradientV. Text (578-704): Text, TextOutlined, MeasureText, LineHeight, TextWrapped, MeasureWrapped, Ellipsize (Roboto).
- Widgets: `Panel(R, EFVPanel)` 706 (Wood / Parchment / Dark / Tooltip — colours hard-coded from `FVPal`, h:28-47),
  `Window` 779 (wood panel + title plate + close X), `CloseButton` 833, `Button` 868 (Normal/Primary/Danger/Ghost), `IconButton`
  936, `Tab` 981, `Slot` 1022, `Hover` 1054, **`Clicked` 1071 (fires on RELEASE, press must start inside, rejected if the
  gesture became a drag or the click was consumed)**, `PressedIn` 1091 (press semantics), `Bar` 1225, `Stars` 1244,
  `MoneyLabel` 1266, `Tooltip` 1290/1295, `MarkUI` 1364, `Slider` 1379, `Checkbox` 1454, `Divider` 1490, `ActionBarRect` 1499,
  `ActionBar` 1506, `BackButton` 1522, `SplitH` 1562, `ScreenRect` 1572.
- Gestures (update 3): `UpdateGesture` 1108 (press → bDragGesture=false, axis None; axis decided when travel > `TapSlop()`
  9 px touch / 5 px mouse), `ClaimDrag(Key)` 1150, `PushTranslate/PopTranslate` 1157/1163, `InClip` 1175, `PushClip/PopClip`
  1189/1218 (canvas scissor, one flush per push: per list, not per row), `DragAxis`, `PointerVelocity`, `CancelTap` (h:214).
- **Theming hook (expert fishing screen, boutique look):** there is no theme object; `Panel/Window/Button` read `FVPal`
  directly. The fishing minigame frame is drawn in FVFishing.cpp `DrawUI` (2638; wood rects at 2664-2737), not by FVDraw — a
  per-rod theme is a palette struct passed into those draw calls (see fishing.md). Adding an `EFVPanel` value is the cheap
  way to get a new window style.

## 3. FVUIKit (Private/FVUIKit.h/.cpp)
- `FFVScrollView::Update(D, Area, ContentH, bAllowWheel)` (cpp 38-121): stop-on-press for a gliding list (50-55), drag start
  only if `D.IsDragGesture() && D.DragAxis() == Vertical && Area.Contains(PressPos) && ClaimDrag` (58-68), rubber band, fling,
  smooth wheel. `ScrollToShow` 123, `ScrollTo` 131, `DrawBar` 138, `DrawFades` 159.
- `FFVPager` GoTo 176 / JumpTo 181 / SetPage 191 / Update 199 (horizontal claim) / `DrawPages` (h:86, translates drawing AND
  hit-testing; draw pages in local coords, use `D.Mouse()`), `FVKit::TabStrip` 280, `SwipeHint` 332, `Approach` (h:137).
- **§12.2 bug (second swipe only stops the list):** `FFVScrollView::Update` (cpp:54) calls `D.CancelTap()`, which sets
  `bDragGesture = true` (FVDraw.h:214) without an axis; `UpdateGesture` (FVDraw.cpp:1131) assigns the axis only when
  `!bDragGesture`, so the gesture never becomes Vertical and the drag never starts. Fix = a separate `bTapCancelled` flag
  checked by `Clicked` (1077) while `bDragGesture` keeps its meaning; re-check FFVPager and every list.

## 4. Shared helpers
- `FVUI` (FVScreenCommon.h): `Sfx` 18, `Icon` 24, `Plural` 33, `Coins` 44, `Num` 50, `Percent` 63, `ThemeColor(EFVTheme)` 69
  (switch: add new themes), `BuyErrorText` 84, `ReportBuy` 101, `ItemSprite/ItemIconOf` 116/125 (any id → icon),
  `FitScale/SpriteFit` 134/143, `SectionHeader` 153, `KeyCap` 162, pixel-image helpers 177-247, `RarityInk` 264,
  `StatusChip` 279, `PackParts` 318, `ActionBarExtraRow(Rect)` 346/354, `PriceButton` 367, `QualityOf` 398, `ContainerName`
  404, `StackTitle/StackInk/MitrichPrice/StackKindText/StackInfoLines` 418-517, `StackCellContent` 519, `EmptyCell` 556,
  `CapacityBar` 564, `FTapKeys` 589, `GridScroll` 622, `BigTab` 634, `CountBubble` 651, `PriceLine` 664.
- `FVHomeUI` (FVScreenHome.h): ink colours 17-27, `ResultText(EFVBuyResult)` 30, `Fail/Success/Refresh` 49-73,
  `NeedStationText` 87, `IngredientName/IngredientsText` 99/107, `TextWrappedLimited` 124, `DrawItemIcon/DrawIcon` 180/205,
  `LevelPips` 247, `CoinButton` 262, `PagerTabs` 345, `PagerKeys` 402, `QtyChips` 411, `ReqRow/CoinRow/CondRow` 428-470,
  `ItemEffectLines` 482, `BaitParts` 509, `KindText/DescriptionOf` 521/531, `FScrollColumn` 558, `RowBack` 598,
  `EmptyState` 610, `FStackKey` 648, `StackCell` 675. Include it (not FVScreenCommon only) for new home/city screens.

## 5. Screens that update 6 touches
- **Shop** (`FShopScreen`): `OnOpen` 808 (trader from context, fallback Mitrich / `FallbackTrader` 476), `BuildTabs` 965
  (Rods, Bait, Tackle, [Cosmetics if bSellsClothes], [Upgrades if bSellsUpgrades], Sell), `BuildItems` 977-1072 (filters by
  `TraderMask & Trader.Mask`, PackPrice>0, BuyPrice>0), `EShopTab`/`EItemKind`/`EItemStatus` 445-447, `FShopItem` 463,
  keeper portrait `GetShopkeeperSprite(Frame, Out, bPier)` 416 (only Mitrich / boatswain: a new trader needs a keeper art
  switch), greetings `PickGreeting` 1120, landscape `Draw` 1239 / `DrawPageLandscape` 1290 / `DrawHeader` 1307, portrait
  `DrawPortrait` 1484 … `DrawMainActionPortrait` 1793, details 2249-3050 (rod 2275, bait 2391, cosmetic 2651 + preview 2537,
  upgrade 2740, item 2846, shovel 2954), sell 3053-3444, buy actions 3553-3717. `NumLists = EShopTab::Count + 4` (832): a new
  tab or cosmetic slot must grow the list-state arrays. Boutique / foreman shop = new trader rows + probably a new tab kind.
- **Wardrobe**: slot pages = `EFVCosmeticSlot::Count` (`NumSlots` 90, `SlotPager`, TabStrip 533); grid 7 cols. «Элитное» tab
  (§13) = an extra page that is not a slot (touch NumSlots users: 410, 524, 801).
- **Collection** (`FCollectionScreen`): one page per `FVDB::Locations()` (OnOpen 300, context = fish id opens it); fish detail
  prints time/weather/depth at 947-951 and 997-999, legend checklist 832. **Fact gating for expert fish goes here** (and in
  Map 1488, 1609). `DepthText` 221.
- **Map** (`FVMapScreen`): procedural image `FMapBuilder` (170-900; Grass/Sea/Swamp/Homestead/Village/Mountains/Volcano/
  River/Roads/Forest…), dims `FMapDims` 17 (landscape 200x148 texels, portrait tall), pins from `MapPos` (`PinTexel` 104,
  `MapPins` 113, home pin `HomeMapPos` 91), `DrawMap` 1160, side panel `DrawSide` 1636, portrait info 1316 / fish detail 1447 /
  action bar 1513, home info 1534, unlock `TryUnlock` 1092. **§3 scrolling map**: the map is one fixed image drawn at
  `MapOrigin`/`MapScale 2` (11-24); scrolling = bigger dims + `FFVScrollView`-like 2D offset + `PushClip`.
- **Station** (`FStationScreen`): `ETab` 27 (Build, Recipes, Materials, Tackle, Nets, Recycle, Rigging, Shovel, Smoke, Worms,
  Aquarium, Cellar, Radio, Trophies, Info, Lures, Upgrade), `Tabs(P)` 404-440 (switch per station), `DrawTab` 561 (switch per
  tab), recipes 1221-1500, rigging 1696-1900, smoke 2118, worms 2200, tank 2309, picker 2723, level track 980 / next level
  1143 / upgrade button 947. Expert station levels (§9б) come from data (`FFVStationDef::Levels`) automatically.
- **Orders** (`FOrdersScreen`): board art `DrawBoard` 335, card 386, action 350. City task board = a sibling screen with
  the same layout (new EFVScreen) rather than a context switch.
- **Net** (`FNetScreen`): context parse in `OnOpen` 39, `DrawEmpty` 224 (choose net), `DrawNet` 335 (collect/repair/remove/
  destroy), scene 124. The artel master screen can reuse its cells/scene helpers.
- **Decor** (`FDecorScreen`): 7 slot pages, `DrawRoom` 297 (preview of the room with the item), buy/equip 397.
- **Inventory**: pages keepnet/backpack (`PageContainer` 37), grid `DrawGrid` 573 (landscape 10 cols, portrait 6), gear list
  1024 (rod/bait/tackle/food/shovel rows 831-1022), eat/remove 433-548. Expert rods/config would add rows to `DrawGearList`.
- **Tutorial**: entry registry 1078-1111 (`Add(id, category, unlock-predicate(FFacts), &BuildX)`), categories `FCategory`
  147, builders 400-1060 (`FEntry` 137, block kinds `EBlk` 117, pictures `EPic` 26), resource lines per location 712-717
  (hard-coded ids). New features = new `Add` rows + builders; `FVTutorialUnseenCount` 2049 drives the HUD badge.
- **Levels**: cards from `UnlocksAtLevel` + `HomeUnlocksAtLevel`, locations loop 73/188; clamps to `FV::MaxLevel`.
- **Menus**: main menu 600-1595 (animated title scene, dev-tools toggle `ToggleDevTools` 876 = 5 taps on the version),
  pause 1596-2660 (settings, help blocks `BuildHelpBlocks` 2485, online info 2029).

## 6. Conventions
- One namespace per .cpp (unity build); names like `PlaySfx`, `Triangle`, `CheckBadge`, `IconBox`, `PriceButton`, `BigTab`,
  `DrawCosmeticIcon`, `DrawItemIcon` exist in several files with different signatures — never pull two of them into one scope.
- Russian text only; never below `FV::Ui::Small()` (7.5) in portrait; portrait content width ≈ 328 px.
- Portrait has no hover: whatever a landscape tooltip shows must be visible inline in portrait.
- Every list scrolls with FFVScrollView (drag, fling, wheel); rows click via `D.Clicked` (release) so swipes never click.

## 7. Pitfalls
- `OpenScreen`/`CloseScreen` from inside Draw/Tick: return immediately (the screen is retired).
- Tick is skipped under a modal, but Draw still runs with input blocked: no per-frame logic in Draw that must pause.
- Changing a container while iterating/selecting: read counts/prices first, re-resolve selection by identity afterwards.
- `EFVBuyResult` texts live in three switches (UFVSaveGame::BuyResultText, FVUI::BuyErrorText, FVHomeUI::ResultText).
- A screen that lists `FVDB::Locations()` (Collection, Map, Levels, Tutorial, Online profile) will pick up new locations
  automatically; a hub location kept out of `Locations()` (like `home`) will not.
- `PushClip` flushes the canvas; nested clips intersect; unbalanced pushes are reset in `End` but break the frame's clipping.

## 8. FVOnline (Public/FVOnline.h 251, Private/FVOnline.cpp 3469)
- `UFVOnline` (h:150-250): status/startup gate, register (HMAC `OnlineSecret`), `SetName/SetStatus`, `RequestSync`, boards,
  profiles, season (3-day tournament), mail (`FetchMail/ClaimMail/MarkMailRead`), `RestoreByCode`, offline mode.
- Constants `FVOnlineImpl` 44-85: sync every 300 s, backup every 900 s, important events 5 s, `MaxSaveBytes` 700 KB,
  `ClientBuildNumber = 6` / `ClientVersionText "0.5"` (bump per release, keep `DefaultEngine.ini` StoreVersion in step),
  server URL `https://example.com/Fishing-game/api.php`.
- Impl class methods: `Send` 1067 (signed JSON POST), `OnHttpDone` 1143, `Tick` 1301, startup 1541-1675, register 1677,
  `BuildSyncBody` 1895 (stats: level, xp, fish_total, legendary, species, coins_earned, achievements, perfect, playtime,
  stations_built; profile: status, favourite location, best catch, showcase, appearance, start date; season_best), `StartSync`
  1938, `StartUpload` 2017 (`SaveGameToMemory` backup), `ApplySeason` 2073, mail 2168-2390 (`ApplyMailRewards` 2319: coins,
  XP, SetLevel clamped to `FV::MaxLevel`, items via `GiveItem`; `HasRoomFor` 754). Public wrappers 2393-3469.
- `ProfileFromSave` 649 / `StationsBuilt` 635: what the server sees. **Update 6:** new levels (XP > 10000), species > 40,
  achievements > 51 and new item ids must be accepted by the server first (`lib/validate.php`, `lib/fish.php`), and
  `ClientBuildNumber` bumped; the server never validates ids inside uploaded saves.
