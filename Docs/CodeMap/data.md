# Code map: data (FVDB content + UFVSaveGame) — current as of 0.5, written for update 6

Paths: `Source/FishingValley/{Public,Private}/`. Files: `Public/FVDatabase.h` (210), `Private/FVDatabase.cpp` (1783, base
tables + balance), `Private/FVDatabaseHome.cpp` (1359, update-2 tables), `Public/FVSaveGame.h` (463), `Private/FVSaveGame.cpp`
(1553, core profile + FixupAfterLoad), `Private/FVSaveGameHome.cpp` (1707, containers/stations/nets/orders...). Content structs
and enums are plain C++ in `Public/FVTypes.h` (874; no UHT, never UPROPERTY). Persisted USTRUCTs live only in FVSaveGame.h.
Line numbers are from the current files; re-grep before editing (they drift).

## 1. How FVDB is built
Two independent lazy statics; tables are immutable after build, so `const T*` into them live for the whole program
(code keeps them: `FFVCatchContext::Rod/Bait`, `FFVCatchResult::Fish`, shop `FShopItem::Rod/Bait/Item/Cos`).
- **Base (FVDatabase.cpp)**: `FFVDatabaseData` (9-26: Locations, Fish, Junk, Rods, Baits, Cosmetics, Achievements, Keepnet +
  `*Idx` maps + StartCosmetics/TreasureCosmetics) in an anon namespace; `DB()` (994) builds once; `Build()` (963-992) order:
  Locations → Fish → Junk → Rods → Baits → Cosmetics → Achievements → Keepnet, then `BuildIndex` per table (197; duplicate id
  = Error log, first wins), `Validate` (892-961), count log. `FVDB::Init()` (1075) = `DB(); Items();`.
- **Home (FVDatabaseHome.cpp)**: `FVDBHome::FData` (12-26: Items, Recipes, Stations, Traders, Decor, Shovels, Backpack,
  StarterDecor + Idx maps); `Data()` (807) with a `bBuilding` re-entrancy guard; `Build()` (783-805) calls `FVDB::Init()` first,
  then Items → Recipes → Stations → Traders → Decor → Shovels → Backpack, index, `Validate` (663-781: every ingredient/output id
  known via the `Known` lambda = token/item/bait/junk/fish).
- Builders (FVDatabase.cpp 31-195): `AddLocation`; `AddFish` → `FFVFishDef&` then `When(F, TimeMask, WeatherReq, MinLevel,
  MinDepth)` (68), `Look(F, Shape, Pattern, Body, Belly, Fin, PatternCol)` (77), `Legend(F, EFVLegend, Prep, Fight)` →
  `FFVLegendPrep&` (89); `AddJunk` 97, `AddRod` 107, `AddBait` → `FFVBaitDef&` 130, `AddCosmetic` 149 + `AddHat/Outfit/Bobber/Pet`
  164-176, `AddAch` 181 (nullptr = no param/cosmetic, Target clamped ≥1). Home builders: `AddItem` 162 + `Sold(I, Buy, Mask,
  MinLevel)` 176; lambdas `AddLine` 271 / `AddFloat` 297 / `AddNet` 319 inside `BuildItems` (186-338); `AddRecipe` 340
  (`BuildRecipes` 358-410); `AddStation`/`AddLevel` 412/423 (`BuildStations` 434-515); `BuildTraders` 517; `AddDecor` 547
  (`BuildDecor` 564-628); `BuildShovels` 630; `BuildBackpack` 650. Home helpers: `Ing(Id, N)` 32, `Mods(...)` 41.
- Lookups: `FindIn(Arr, Idx, Id)` (1005; nullptr for None/unknown). `FindLocation` (1090) ALSO returns `HomeLocation()` /
  `HouseLocation()` (ids `home`/`house`, built by `MakeHomeDef` FVDatabaseHome.cpp:820, MapPos (404,200), Theme Home /
  HomeInterior), which are NOT in `Locations()`. `FishAt` (1106) / `CosmeticsFor` (1116) are linear scans; `LocationIndex`
  (1126) = progression index (INDEX_NONE for home ids).
- **New table idiom**: array + Idx in the Data struct, `AddX`/`BuildX`, call in `Build`, `BuildIndex`, accessor + `FindX` in
  FVDatabase.h (an additive block with an `// UPDATE 6` banner), checks in the matching `Validate`. Put update-6 tables in a NEW
  file (e.g. `FVDatabaseCity.cpp`, own namespace + own lazy static that calls `FVDB::Init()` first, like FVDatabaseHome).
- **Ids are global and persisted as strings**: unique across all tables, prefix convention `fish_ junk_ rod_ bait_ lure_
  hat_ outfit_ bobber_ pet_ ach_ res_ mat_ food_ line_ float_ net_ rcp_ st_ tr_ decor_`, bare location ids. Never rename.
- `ItemName(Id)` (1662-1684) order: fish → junk → rod → bait → cosmetic → location → achievement → item → decor → station →
  trader → any_fish tokens → recipe (name of its output) → raw id. `ItemPrice(Id,Q)` (1524): fish × quality, junk, item (food ×
  quality). `ItemPriceEx(Id,Q,Flags)` (Home 936) adds Smoked ×2; `TraderSellPrice` (Home 947) × trader FishSellMult/ItemSellMult.

## 2. Content (0.5)
- **Locations (BuildLocations 214-234)**: pond (0, L1, MapPos 330,246), river (400, L2), mountain (1500, L3), ocean (5000, L5),
  swamp (12000, L7), volcano (30000, L9). MapPos is authored in 640x360 map space. `EFVTheme` (FVTypes.h:176) = Pond..Volcano,
  Home, HomeInterior, Count; a new location needs a new theme value (scene, music `MusicThemeFor`, map art, `FVUI::ThemeColor`).
- **Fish (BuildFish 236-554)**: 40 (pond/river/ocean/swamp 7 each, mountain/volcano 6), incl. 6 legends; ends with a
  `StableSort` (545) by location progression, then rarity. `bLegendary` is set from Rarity. Legends carry `Legend` + `Prep`
  (tsar_carp 284, sturgeon ~336, crystal_trout ~381, giant_angler ~432, fog_serpent ~485, zhar_ryba ~533).
- **Junk (556)**: weed, boot, can «Ржавая банка», driftwood, bottle. `PickJunk` (1013-1051) weights by `EFVJunkStyle` + theme.
- **Rods (570-591)**: bamboo (T0, free), fiberglass 800, carbon 3000, titanium 9000, iridium 25000, poseidon 60000;
  `TraderMask` (1 = Mitrich, 2 = pier). Stats table in the comment at 572.
- **Baits (593-690)**: worm (MUST stay [0]), grub, lure «Блесна», magic, then PackPrice-0 crafted ones (nightcrawler, dough,
  groundbait), 24 `lure_<b>_<c>_<g>` (bLure, affinity), `bait_ember` at the END; legend favourites are set after (110-125).
- **Cosmetics (693-782)**: 38 = 12 hats, 8 outfits, 12 bobbers (6 legend reward bobbers, Price 0), 6 pets. StartCosmetics =
  hat_straw/outfit_overalls/bobber_classic; TreasureCosmetics (781) = bobber_golden, hat_captain.
- **Achievements (784-878)**: 41 (server `FV_MAX_ACHIEVEMENTS = 51`). Size-dependent targets (NumFish, NumLegends, NumRods,
  NumLocations) are computed at build, so adding fish/rods/locations silently raises species_all / legends / all_rods /
  all_locations. `EFVAchKind` (FVTypes.h:206-237) incl. the update-2 kinds; value switch = `AchievementValues`
  (FVSaveGame.cpp:43-131).
- **Keepnet (880)**: caps 12/20/30/45/60, prices 0/500/2000/6000/15000. **Backpack** levels (650), **Shovels** (630).
- **Items (BuildItems 186-338)**: res_ branch pebble fiber feather mushroom berries reed clay resin; mat_ board rope glue scrap
  sinker glitter; food_ fried(+_rare/_epic/_legend) tea soup pie; lines linen/braided/silk/fluoro/titan/heat (tier 1-5);
  floats feather/reed/sliding/glow/sport/gold; nets small/medium/strong/large (`NetCapacity/NetDurability/NetMinutesPerFish`).
  Trader bits are `constexpr uint8 Mitrich = 1, Pier = 2` (FVDatabaseHome.cpp:183-184, local to that file). Masks are
  `uint8` → at most 8 traders.
- **Recipes (358-410)**: 31, workshop/kitchen; tokens `any_fish`, `any_fish_rare/_epic/_legend`; `EFVCraftGame` Cooking/Knots.
- **Stations (434-515)**: Kitchen, Workshop, Smokehouse, WormFarm, Aquarium, IceCellar, Radio, Bed (built), TrophyWall,
  Chest (built); `EFVStation` (FVTypes.h:639) indexes `Stations()`. Levels carry Price/Materials/MinLevel/Capacity.
- **Traders (517-545)**: tr_mitrich (pond, ×1/×1, clothes + upgrades), tr_pier «Боцман Семёныч» (ocean, fish ×1.25, items
  ×1.15). `TraderAt(Loc)` 1077, `DefaultTraderId()` = Mitrich.
- **Decor (564-628)**: 27 items in 7 fixed slots `EFVDecorSlot` {Wallpaper, Floor, Rug, Lamp, Plant, Painting, Curtains}
  (FVTypes.h:655); one equipped per slot; Style = art index inside the slot (UFVSprites::DecorIcon/DecorWorld).

## 3. Balance formulas (FVDatabase.cpp)
- New game: `StartMoney` 150. `XpForLevel` table {0,0,100,380,770,1300,2150,3300,4800,6900,10000,13100,19300,26700,35500,
  45900} (static_assert: one per level) clamped to `FV::MaxLevel` = **15** (update 6; `FV::ClassicMaxLevel` = 10,
  `FV::ExpertMaxLevel` = 15). `LevelBarBonus` = 8·(min(L,10)-1); `RollSizeFrac` / `RollTreasure` also clamp to
  ClassicMaxLevel (classic balance unchanged above 10). «Мастер-рыболов» = ClassicMaxLevel.
- `UnlocksAtLevel` (1194-1245) + `HomeUnlocksAtLevel` (FVDatabaseHome.cpp:1302): Russian lines for exactly level L; both are
  used by the level-up modal (FVGame.cpp:2768-2769) and FVScreenLevels. New level-gated content needs a loop in one of them.
- `IsFishAvailable` (1247): time mask, weather (Sunny/Rain only), MinLevel; depth NOT checked.
- `PickCatch` (1256-1372): junk chance = Loc.JunkChance × bait JunkMult (no bait & not a net ×1.15) × `Ctx.JunkMultScale` ×
  (1-0.45·Depth), clamp .02-.75. Available = location fish passing IsFishAvailable; nets skip legends; legends failing
  `LegendGate` = NoBite are skipped (the first one goes to `OutGated`). Weights: legend W·(1+.5(Luck+Rare))·(.75+.5D),
  rare/epic W·(1+Luck+Rare)·(.75+.5D), common W·(1.15-.3D); lure affinity / legend lure / ember OtherFishMult; the fallbacks
  never return nothing. Luck = Rod.Luck + `Ctx.ExtraLuck`. **Hook for expert fishing, local weather and new conditions:
  FFVCatchContext (FVTypes.h:497-515) + IsFishAvailable + this loop.** Weather is binary (`EFVWeather` Sunny/Rain); a storm is
  a new value (touches every `Weather < Count` clamp, the radio, scene rain, `WeatherName`, `WeatherReqText`).
- `RollBiteDelay` 1374, `RollSizeFrac` 1384, `RollQuality` 1398 (perfect +1 tier), `CatchXp` 1405, `JunkXp` 1414 (global RNG).
- `RollTreasure` (1419-1505): coins × `LocMult[LocationIndex]` {1,2,3.5,6,9,14} (index >5 clamps to 14, so new locations get
  ×14 unless the table grows); 35% sellable bait (PackPrice>0, weight by rank); 6% treasure cosmetic; 12% item from the static
  `Drops[]` (1471). **Hook for "fish facts from expert treasure" (§10.16): add a field to `FFVTreasure` (FVTypes.h:547) and
  apply it in `UFVGame::ApplyTreasure` (FVGame.cpp:2627).**
- Legends: `LegendGate` 1698, `LegendChecklist` 1726, `FindLegend` 1755. `Validate` requires every legendary fish to be exactly
  one `EFVLegend` and all 6 assigned (`NumSeen != Count-1` error): expert legends need new enum values or a separate table.
- `ComposeRodStats(Rod, Line, Float, Food*)` 1765 and `CatchBarHeight(Level, Stats)` = 96 + LevelBarBonus + BarBonus (1779):
  the one formula, also behind `UFVSaveGame::GetRodStats` and `UFVGame::GetEffectiveRodStats`.
- Nets: `NetWear(F,Q)` (Home 1292). Gathering/digging is deterministic per day: `RollResource(Loc, Day, Slot)` 1225,
  `ResourceSlotCount(Loc)` (Home 148; hard-coded ids, 0 for unknown, so new locations get no pickups until added), `RollDigLoot` 1272.

## 4. UFVSaveGame: fields (FVSaveGame.h)
- Stack `FFVInvStack{ItemId, Quality, Count, Flags}` (21-29; identity = id + quality + flags, `FVStackFlags::Smoked`).
  USTRUCTs: `FFVRodLoadout{Line, Float}` 33, `FFVNetState{Location, Spot, NetItem, Durability, bTorn, SimMinute, Progress,
  Catch}` 42, `FFVSmokeSlot` 57, `FFVOrder{Id, Customer, ItemId, MinQuality, bSmoked, Count, Deposit, Reward, RewardItem,
  RewardItemCount, DurationHours, Deadline, State}` 67, `FFVTrophy` 87, `FFVDayMask{Day, Mask}` 97, `FFVFishRecord` 105,
  `FFVStats` 114-137 (update-2 counters Gathered..KnotsTied).
- `UFVSaveGame` (139): v1 fields 153-185 (Money, Xp, Keepnet, rods, Baits map, cosmetics, locations, Records, achievements,
  Stats, Day/TimeMinutes/Weather, volumes); update 2 (188-219: Backpack, BackpackLevel, Chest, IceCellar, Aquarium, Trophies,
  StationLevels map, ShovelLevel, RodLoadouts map, ActiveFood*, Nets, Smokehouse/SmokeFuel, WormFarm*, Orders/OrdersDay,
  Gathered/Dug day masks, DiscoveredLures, OwnedDecor/EquippedDecor, WeatherTomorrow); update 3 (SeenTutorial,
  LastInventoryTab); update 4 (PlayerName, PlayerStatus, OnlineId/Tag/Secret, ProfileStartUnix, CatchesByLocation, BestCatch*,
  PendingSeasonBest, ClaimedMail); 4b OnboardingStep; update 5 LegendsMet, bDevTools. **Append update-6 fields after line 239
  under a `// ---- update 6` comment.**
- `CurrentVersion = 2`, `Version = 0` initializer (148-153). Updates 3-5 added fields WITHOUT a bump (safe defaults / Fixup
  heuristics). A real v3 migration works now (the Version field is written because 2 ≠ the default 0): bump to 3 and branch on
  `Loaded < 3`.

## 5. Save lifecycle & migration (FVSaveGame.cpp)
- Slot: `GetSlotName` 382 ("FishingValley" / "FishingValley_AutoTest" via `SetAutoTestSlot`, set under `-fvautotest` before
  Load). `Load` 1511 (null on missing/foreign; calls Fixup), `Save` 1533 (stamps Version), `DeleteSave` 1545.
- `ResetToNewGame` (392-502): the full new-game state, then `FixupAfterLoad`; seeds the starter stations (bStartsBuilt),
  OnboardingStep 0.
- **`FixupAfterLoad` (503-1036) is the only migration + validation point.** Blocks in order: version / v1→v2 branch (509-545;
  junk leaves the keepnet BEFORE validation), keepnet 550 (fish only; others re-homed), rods 558, bait 568, cosmetics 580,
  locations 618, records & achievements 628, stats 638, clock/weather/settings 659, update 3 670, update-2 state 679-936
  (stations 684, backpack/tools 700, containers 705, trophies 720, tackle 739, food 763, **nets 773** (a valid net item, spot <
  `Fx::NetSpotCount(Loc)`, known location), smokehouse 809, worm farm 838, orders 846, day masks 873, lures & decor 883,
  **re-home misplaced stacks 923**: never destroy player items), update 4 938-1000, 4b 1002 (old saves get the onboarding
  "done" by a heuristic because the field loads 0), update 5 1020.
- Helpers: `SatAdd` 15, `CleanIdList(List, Pred)` 145, `FVSaveGameFix::NormalizeStacks` 234, `NetSpotCount` 295,
  `FVSaveGameOnlineFix::CleanLine` 314.
- Callers: `UFVGame::SaveNow` (only in game), autosave every 60 s when dirty and not fishing / no modal, an immediate save after
  each catch and on `TravelTo`; screens call `UFVGame::NotifyProfileChanged()` (achievements + appearance + dirty).

## 6. Profile logic (where to hook)
- Core (FVSaveGame.cpp): level/money 1040-1081 (`Earn(Amount, bCountAsSales)`), keepnet 1083-1152, rods/bait 1154-1248
  (`GetRod` 1176 returns a reference into the static DB; never return a temporary), cosmetics 1250-1356, locations
  1358-1380 (`IsLocationUnlocked`: home ids always true; `UnlockLocation` checks level + money), `RecordCatch` 1382,
  legends met 1403-1414, species 1416-1441, `CheckAchievements` 1443 (≤4 passes), `BuyResultText` 1486.
- Home (FVSaveGameHome.cpp): containers 402-551 (`GetContainer`, capacity in units vs slots, `AddTo` all-or-nothing,
  `Transfer`, `TransferAll`), ingredients 553-630 (`CountIngredient`, `ConsumeIngredients` via `FVSGHome::ApplyIngredients` 179:
  exact ids → rarity tokens → any_fish), `GiveItem` 617 (fish → keepnet, bait → Baits, the rest → backpack), trading 651-708
  (`SellFrom`, `SellAllFish`, `BuyItem` checks the trader mask), shovel 709-752, stations 754-801, crafting 803-901,
  tackle 903-985, food 987-1020.
- **Time**: `SimulatePassive(From, To, Weather)` 1022-1057 (food expiry, smokehouse fuel, **every net via
  `FVSGHome::SimulateNet` 321**: hour chunks, rate 1/MinPerFish ×1.2 rain ×1.1 night; `NetCatchOne` 283 builds an
  `FFVCatchContext{bNet, Depth .3, JunkMultScale .5}` and calls `FVDB::PickCatch` with a seed from `NetSeed(Loc, Spot, Minute)`
  254; wear `NetWear`; tear roll below 25% durability, 4%/h). `OnNewDay` 1059-1097 (worm farm, stale masks, `RefreshOrders`),
  `ExpireOrders` 1099. Called only by `UFVGame::AdvanceClock` / `StartNewDay` (FVGameHome.cpp).
- **Nets**: `FindNet(Loc, Spot)` 1118, `PlaceNet` 1128 (spot < `FVSGHome::NetSpotCount` 265: pond/river/ocean/swamp 2,
  mountain 1, else 0, **so returning 0 for a location forbids nets there**), `CollectNet` 1148, `NetRepairCost` 1178 (rope 1 +
  fiber 2), `RepairNet` 1190 (+50%), `RemoveNet` 1208 (≥95% and empty), `DestroyNet` 1225. `NetSpotCount` is duplicated in
  FVSaveGame.cpp:295 and in FVScene (spot positions); change all of them together.
- **Orders**: `RefreshOrders` 1436-1580 (deterministic `FRandomStream` from Day; pools: fish of unlocked locations, smoked,
  resources/materials, dishes; 3 offers/day, ≤3 taken, no duplicates), `TakeOrder` 1582 (deposit), `FVSGHome::OrderAccepts`
  1599, `CanDeliver` 1607, `DeliverOrder` 1619, `TakenOrders` 1645. Constants `FVSGHome` 11-17.
- Smokehouse 1247-1302, worms 1304, aquarium/trophies 1318-1385, gathering/digging 1387-1434, decor 1655-1707.

## 7. Serialization rules (verified in the UE 5.8 source)
- `SaveGameToSlot` serializes every `UPROPERTY()` (no SaveGame flag filter) as a **delta against the CDO**: a top-level
  field equal to its initializer is not written and loads as the CURRENT initializer. Never change an existing initializer.
- New fields load their initializer in old saves: choose initializers that are right for an old player, or seed in Fixup.
- Supported: `TArray<USTRUCT>`, `TMap<FName, USTRUCT>`, `TMap<FName,int32>`, a struct containing a TArray. Not supported:
  nested containers (`TMap<FName, TArray<>>`); wrap the inner one in a USTRUCT. Plain enums → `uint8` + a range check in Fixup.
- Transient caches = plain members without UPROPERTY. The online backup uses `SaveGameToMemory` (FVOnline.cpp:2027, limit
  700 KB `MaxSaveBytes`); large per-object arrays (trees, rocks, furniture) count toward it.

## 8. Update 6 hook list (data side)
- New fish (+1 per location, §3а/Р39): `BuildFish` blocks; the sort keeps the grouping; raises the species_all target, server
  `FV_MAX_SPECIES = 40`, `gen_fish.php`; art comes from EFVFishShape/Pattern (no per-id art needed).
- New locations (forest/city/cave): `BuildLocations` + `EFVTheme`. Check `LocationIndex`-indexed tables (`LocMult`), the
  `all_locations` / `CompleteLocation` achievements, `ResourceSlotCount`, both `NetSpotCount`s (0 for the expert location = no
  nets, per spec), the `RefreshOrders` pools (loop over `Locations()`), map MapPos, `MusicThemeFor`. A non-fishing hub (city)
  modelled like `home` (outside `Locations()`, special-cased in `FindLocation`) avoids those side effects.
- Fish facts (§10.16): no knowledge state exists. The Collection (FVScreenCollection.cpp:947-999) and the Map
  (FVScreenMap.cpp:1488, 1609) print `TimeMaskText/WeatherReqText/MinDepth` unconditionally. Needs a persisted set (e.g.
  `TMap<FName, int32> FishFacts`, a bitmask per fish id) + Fixup (known fish ids, mask range) + UI gating in expert mode only.
- City tasks (§8.2): copy the `FFVOrder`/`RefreshOrders` pattern (deterministic per day, pools filtered by unlocked content)
  into a separate array + day stamp; a reward can carry a fact id. Kitchen/workshop items are recipe outputs, and
  `CountIngredient` already counts items/baits/fish.
- Artel nets (§8.3): reuse `FFVNetState` + `SimulateNet`; either a separate `TArray<FFVNetState> ArtelNets` with
  `TArray<FFVInvStack> ArtelStock` (nets handed over) or an owner flag. The daily collect + sell belongs in `OnNewDay` (pure
  data, deterministic), returning summary lines to `StartNewDay`; sell through `TraderSellPrice` + `Earn(…, true)`.
- XP > 10000 migration (§9): branch on `Loaded < 3` in Fixup after bumping `CurrentVersion`.
- Boutique cosmetics / real-house furniture: new cosmetics are rows (Style enums need art); furniture needs a data model beyond
  the 7 decor slots (placement ids, not pixels; see §9).
- New buy results: extend `EFVBuyResult` (FVSaveGame.h:11) AND all three text switches: `UFVSaveGame::BuyResultText`,
  `FVUI::BuyErrorText` (FVScreenCommon.h:84), `FVHomeUI::ResultText` (FVScreenHome.h:30).
- `FVAutoTest::GrantEverything` must grant new state for the screenshots; server `lib/validate.php` limits (§14).

## 9. Pitfalls
- FixupAfterLoad DELETES unknown ids (rods, cosmetics, locations, records, achievements, bait-map entries, decor, stacks whose
  id no table knows). Removing or renaming any id wipes player data.
- `bait_worm` must stay `Baits()[0]`; new baits append at the end (treasure weights by rank, the new-game gift).
- Achievement targets derived from table sizes change silently; adding achievements pays them out retroactively on the next
  `CheckAchievements` (ContinueGame checks at load).
- `ItemName` resolves by table order; a shared id is shadowed silently.
- Nothing in data may be stored in virtual px (the canvas is 640x360 or 360x600..900, and update 6 adds a camera). Store stable
  ids and normalised or world-tile coordinates. MapPos is the only px-like field (map space 640x360, normalised by the map screen).
- Weather/clock: `Weather` is rolled only in `StartNewDay` (today = yesterday's `WeatherTomorrow`, FRand 30% rain). Timers use
  absolute minutes `(Day-1)*1440+Time`; the clock stops under screens/modals and during the minigame.
- Unity build: every .cpp needs its own namespace for helpers (`FVDBHome`, `FVSGHome`, `FVSaveGameFix` exist; names like
  `BuildIndex`, `FindIn`, `Mix`, `Fmix`, `NetSpotCount` already exist in several files).

## 10. Update 6 — DATA-CORE (implemented)
- **§0 / economy**: `float_sliding` sells 220 (was 300), `mat_board` costs 80 at Митрич (Р6), `rcp_soup` = any_fish ×2 +
  res_mushroom + res_reed + **veg_carrot** (Р41; the id comes from DATA-ITEMS). `bait_ember` LegendLureMult 3 (Р45).
- **Levels (FVSaveGame.cpp / FVSaveGameU6.cpp)**: `AddXp` always adds to `U6.XpEarned`; before `U6.bVisitedCity` XP stops
  at `XpForLevel(10)` (the rest is lost). `IsXpFrozen` = not visited && XP ≥ 10 000; `GetLevelProgress` = 1 while frozen.
  `AddRewardXp` (mail / tournaments): the part above 10 000 is paid as coins 1:1 (`Earn(…, false)`), all of it counts
  in XpEarned. `ArriveInCity` (once): XP = max(XP, 13 100) → level 11. `UnlocksAtLevel` 4 (forest), 10 (freeze), 11
  (expert fishing), expert-zone locations without a price; `HomeUnlocksAtLevel` lists yard buildings.
- **Migration (`FixupU6`)**: U6Version 0 → XpEarned = XP; every load: XP > 10 000 without the City → `U6.XpSeniority`
  (+ trim), visited City → XP ≥ 13 100; clamps of tools / nodes / yard / garden / boat / counters (only against non-empty
  tables); stamps `U6Version` = 1; then FixupExpert, FixupCity. The pier net on spot 1: both `NetSpotCount`s now return
  **ocean = 1** (city / cave / forest = 0), so the nets block of FixupAfterLoad returns that net to the backpack and its
  catch to the keepnet / backpack. **FVScene's own net spot list must match (SCENE-CAMERA).**
- **Catch (FVDatabase.cpp)**: `PickCatch` never picks `bExpert` fish (also in the fallback). `PickJunk(…, Bait)`:
  `Scrap` weight 8 (24 with a bait of Style `Trash`), City scrap/can ×1.5, Cave scrap ×1.5. Nets: `CollectNet` gives
  `mat_scrap` for `junk_scrap` (`FVSGHome::CatchGiveId`). The per-cast catch path (FVGame) must do the same.
- **Locations**: `FindLocation("forest")` → `FVDBU6::ForestLocation()` (MinLevel 4, MapPos (64,176), not in Locations();
  `LocationIndex` = INDEX_NONE). Home / house defs: bBigWorld, yard text without the garden.
- **Trade**: `TraderSellPrice` — item with `BuyerMask & T.Mask` → × `BuyListMult`; `bBuysOnlyList` trader returns 0
  (does not buy) for anything else (fish too). `BuyItem` of a `Blueprint` item: once (AlreadyOwned), Count 1, coins →
  `LearnBlueprint`, nothing stored.
- **Crafting**: `RecipesFor(Station)` skips yard recipes (`Yard != Count`; use `FVDBU6::RecipesForYard`). `CanCraft`: a
  yard recipe needs `GetYardLevel(Yard) >= StationLevel`; `Craft` counts forge recipes in `U6.Forgings`, only
  non-yard workshop recipes in `Stats.Crafted`. Home `Validate`: yard recipes checked against the building levels,
  expert tackle skips the tier 1..5 check.
- **Nets**: `PlaceNet` refuses the expert zones (Р35).
- **Pond order board (`RefreshOrders`)**: no expert-zone fish, no `bExpert` fish; dishes whose recipe needs `veg_*`
  and `veg_*` resources only with the garden built (Р42); resources with `MinLevel > level` skipped.
- **Achievements**: `ach_species_all` («Ихтиолог») target = classic species (not bExpert, not city/cave) recomputed after
  the appenders; `CatchSpecies` counts the same. Update-6 kinds: `FVSaveGameU6Ach::Values` (FVSaveGameU6.cpp), called
  from `AchievementValues` (default case) and `AchievementProgressU6`. `Validate` skips bExpert fish in the legend checks.

- Hotfix 0.6.1: `FFVLocationDef::SideWiden` (fraction of the screen width added on EACH side; only the pond, 0.1 —
  see scene.md §17). `FVDB::FishAt` returns the place's fish sorted by rarity (stable).
