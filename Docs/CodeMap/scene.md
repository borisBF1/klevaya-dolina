# Code map: scene & world (revised for update 6; code state after updates 2-5)

Files (world module):
- `Public/FVScene.h` (129 lines).
- `Private/FVScene.cpp` (1477): water, ambience, player, pet, draw.
- `Private/FVSceneArt.h` (471): FLayout, EDecor, FVSceneRt runtime types, **FVScene::FImpl**, FVPracticePond.
- `Private/FVSceneArt.cpp` (4322): backgrounds of all themes, props, anchors, TranslateLayout, BuildLocation.
- `Private/FVSceneHome.cpp` (1128): world objects, auto-walk, highlight, interior back, runtime lights.
- `Private/FVArtHome.cpp` (3106): station/stall/board/net/dig/decor sprites, UFVSprites getters.
- `Private/FVGameHome.cpp` (775): UFVGame clock/sleep, home scene switching, Interact dispatch, world input.
- `Private/FVTouch.cpp/.h` (185/64): gestures. `Private/FVOnboarding.cpp/.h` (539/21): coach marks.

Namespaces already taken: `FVSceneArt`, `FVSceneRt`, `FVSceneArtLocal`, `FVScenePondImpl`, `FVSceneHomeImpl`,
`FVArtHomeImpl`, `FVArtItemsImpl` (named; links across files), `FVGameHomeImpl`, `FVOnboardingImpl`, `FVPracticePond`.

## 0. Frame pipeline (FVGame.cpp)
- `UFVGame::Tick` runs in this order:
  1. 879 `Touch->Process(In, Dt, bWorldInteractive, bIdle, bIdle && Scene->CanFishFromHere())`.
  2. 886 `TickWorldInteraction(In)` (FVGameHome.cpp:650). It may consume the LMB.
  3. LMB/Space "action" latch (889+).
  4. 1000, world running: `TickClock`, `Scene->Tick(Dt, In, !Fishing->IsBusy())`, `Fishing->Tick`.
     Otherwise 1007: `Scene->Tick(Dt, IdleInput, false)` (menu open: the scene animates, the clock is stopped).
  5. `FVOnboarding::Tick`.
- `UFVGame::Draw` 1106-1114 draws in this order:
  1. `Scene->DrawScene`;
  2. `Fishing->DrawWorld` (line, bobber);
  3. `Scene->DrawLighting` (tints over everything above);
  4. `Scene->DrawOverlay` (name plate + floating texts, not tinted);
  5. `Fishing->DrawUI`, `DrawWorldHud`, `FVOnboarding::Draw`.
  **World labels go in DrawOverlay, never in DrawScene.**
- Touch hooks are set at FVGame.cpp:756 (`SetCastBlocker` = `Scene->ObjectAt(P)!=nullptr`) and 758 (`SetUiTest`).
- LoadLocation callers: FVGame.cpp:768 (start), 2367 and 2530 (travel), 2422 (home), FVGameHome.cpp:437 (LoadHomeScene).

## 1. Coordinate model (classic locations; big worlds with a camera: §16)
- World coords are the virtual canvas coords: landscape 640x360, portrait 360xVH with VH 600..900.
  - `FV::UpdateCanvasForViewport` (FVTypes.h:47) bumps `FV::CanvasRevision`.
  - `In.Mouse`, `In.WorldTapPos`, object `Hit`/`Feet` and `PlayerPos` all live in this same space.
- Constants:
  - `FV::WorldScale` = 2: 1 texel = 2 px (`FVSceneRt::WS`). `PlayerSpeed` = 150 px/s.
  - `TopBarH` 24/32. `BottomBarH` 16/84 (84 = the portrait button dock).
- Each location is exactly one screen, built as **two different compositions**:
  - `BuildX`: landscape, 320x180 texels + mirrored bleed;
  - `BuildXP`: portrait, painted with real bleed.
  - Only `MapPoint` maps positions between them.
- Camera building blocks that already exist:
  - `FVDraw::PushTranslate/PopTranslate` (FVDraw.h:189): moves drawing AND hit-testing; `D.Mouse()` is local.
  - `FVDraw::ScreenRect()` (FVDraw.cpp:1572): the visible rect in local coords. `DrawInteriorBack` already culls with it.
  - `L.Origin` / `WorldR`: background placement.

## 2. Public API (FVScene.h)
- Core:
  - `LoadLocation(FName)`. It also accepts `FVDB::HomeId()` = "home" (the yard) and `FVDB::HouseId()` = "house" (the interior).
  - `GetLocationId`, `Tick`, `DrawScene`, `DrawLighting`, `DrawOverlay`.
- Player: `GetPlayerPos` (feet), `Get/SetFacing`, `SetPlayerAnim`, `GetPlayerFrame`, `IsPlayerMoving`, `GetRodTip`,
  `GetHeadTop`, `SetHeldSprite`, `RefreshAppearance` (re-reads look + pet from the profile).
- Water: `GetShoreY`, `GetWaterBottomY()` (= `VH-BottomBarH`), `IsWater`, `DepthAt`, `CanFishFromHere`, `GetCastTarget`.
- FX: `SpawnSplash`, `SpawnRipple`, `SpawnFloatingText`, `SpawnSparkles`, `PlayPickup(P, Icon)`.
- Update 2 world (implemented FVSceneHome.cpp:987-1128):
  - Scene kind: `IsHome()`, `IsInterior()`, `HasWater()`.
  - Objects: `GetWorldObjects`, `RefreshWorldObjects`, `ObjectAt(P)`, `NearestInReach`, `IsInReach`,
    `FindObject(Kind, Index, Id)`, `SetHighlight` (call every frame).
  - Auto-walk: `SetWalkTarget(Feet, Reach)`, `ClearWalkTarget`, `HasWalkTarget`, `WalkTargetReached/Failed` (true for 1 frame).
  - `DebugTeleport(P)` (snaps to walkable).
- `FFVWorldObject` (FVTypes.h:865): `Kind, Index, Id, Feet (walk-to point), Hit (click rect), Reach, Label`.
- `EFVObjKind` (FVTypes.h:658): Resource, Stall, OrderBoard, NetSpot, DigSpot, Door, Station, DecorCatalog.

## 3. FImpl (FVSceneArt.h:327-455), shared by FVScene.cpp and FVSceneHome.cpp
**Members**
- Cache:
  - `TMap<uint64,FLocCache{Bg,Layout}> Cache`, keyed `CacheKey(Id, BgSize)` (FVScene.cpp:73), never evicted.
  - `CanvasRev`, `BgSize`, `WorldR` (canvas rect of the bg, bleed included), `LayoutCanvas`, `bAtSpawn`, `bSpawnAtDoor`.
- Active: `Loc`, `Bg`, `L` (layout in canvas coords), `Decor[EDecor::Count]`.
- Pet: `PetStyle, PetPos, bPetFlip, PetAnim, PetAnimTime, PetIdleTime, Trail`.
- FX lists: Glints, Particles (≤500), Ripples (≤120), Texts, Birds, Butterflies, Fireflies, Jumps, Bubbles, Fog;
  the `Next*` timers, `Time`, `Rng`.
- World (update 2):
  - `Objects` (FFVWorldObject) and `Vis` (FVis draw entities).
  - `BaseBlockers`: L.Blockers[0..Base) come from the layout; objects append the rest.
  - `WorldSig`/`WorldSigTimer` (profile watch), highlight copy.
  - Auto-walk state: `bWalk, WalkTarget, WalkReach, WalkBest, WalkStuck, WalkSide`. Pickup animation.
  - Interior: `WallTile, FloorTile, WallCol, FloorCol, AquaFish, AquaVis, AquaLevel`.
  - `RtLights` (runtime lights), `RtSmokes`, `NetRects`, `NextPondRipple`.
  - `bArtPending`: sprites were missing, so rebuild once UFVSprites exists.

**Methods in FVScene.cpp**
- `EnsureDecor` 27 (key `MakeKey(Scene,0xFFF000+i,0,2)`).
- `ApplyLayout` 49: copies the cached layout, calls `TranslateLayout(L, CanvasOrigin())`, sets WorldR and `BaseBlockers`.
- `WaterRect` 60, `BleedMul` 66.
- `EnsureLocation` 80. **It returns a reference into the TMap: copy it immediately.**
- `InitAmbient` 105, `MapPoint` 180.
- `FindWalkable` 216: rings of 6..160 px x 16 directions, falls back to Spawn.
- `SyncCanvas` 233, `OnWaterSurface` 295, `RandomWaterPoint` 302, `RandomPondPoint` 314, `AtPondSpot` 327.

**Methods in FVSceneHome.cpp**
- `Profile` 206, `WorldSignature` 211, `RebuildWorld` 251, `RebuildKeepingState` 580.
- `TickWorld` 602, `WalkSteer` 637, `UpdateWalkProgress` 670, `IsHighlighted` 700.
- `DrawInteriorBack` 710, `DrawWorldLayer` 797, `DrawVis` 817, `DrawWorldAfter` 897, `DrawWorldLights` 911,
  `DrawWorldOverlay` 941.

## 4. Load, cache, canvas change
- `LoadLocation` (FVScene.cpp:349) steps:
  1. `FVDB::FindLocation`. Home and house resolve at FVDatabase.cpp:1095 to static defs from
     `FVDBHome::MakeHomeDef` (FVDatabaseHome.cpp:820), themes Home / HomeInterior.
  2. `EnsureLocation(Loc, CanvasBgSize())`, then `ApplyLayout`, then `RebuildWorld`.
  3. Spawn: at `DoorFeet+(0,8)` when entering or leaving the house, otherwise `L.Spawn` (FindWalkable fallback).
  4. `InitAmbient`, then `RefreshAppearance`.
- `CanvasBgSize()` (FVSceneArt.cpp:4011): landscape 432x244 texels; portrait `(ceil(VW/2)+80) x (ceil(VH/16)*8+64)`.
- `CanvasOrigin()` (4019): landscape (-112,-64); portrait `(round(VW/2-TexW), VH/2-ShoreY_tex)`, which puts the shore at VH/2.
- Bg texture key (FVScene.cpp:93): `MakeKey(Scene, hash(Id)&0xFFFFFF, (Theme+1)&0xF | SizeX<<4 | SizeY<<14, 1)`.
  - **Theme has 4 bits: at most 15 themes.**
  - **Each size has 10 bits: at most 1023 texels per axis** (BuildLocation clamps TexW/TexH to 32..1024).
  - Big update-6 worlds need a new key scheme and/or chunked backgrounds.
- `SyncCanvas` (233) runs first in both Tick and DrawScene. On a `CanvasRevision` change it:
  1. remembers the auto-walk object;
  2. runs EnsureLocation + ApplyLayout + RebuildWorld;
  3. same art: shifts player, pet and trail by the origin delta;
  4. otherwise remaps with `MapPoint`, then FindWalkable, and re-places the pet. `MapPoint` rules:
     - pond plank/oval: same spot;
     - no-water scenes: same fraction of the walk rect;
     - dock: same dock fraction;
     - land: same distance from shore, X scaled by VW;
  5. re-finds the walk target by kind/index/id;
  6. calls `InitAmbient`.
- `FVFishing::FImpl::SyncCanvas` then remaps the bobber.

## 5. FLayout (FVSceneArt.h:70-165). TranslateLayout moves every coordinate
- Water scene:
  - Geometry: `ShoreY`, `Dock` (Y = top on land, Bottom() = end in the water), `bRockPier`, `WalkTop`, `WalkMinX`,
    `WalkMaxX`, `Blockers`, `Spawn`.
  - `Lights`: `FLight{Pos,Radius,Color,bFlicker,bAlways,Window}` (the aggregate order matters).
  - `Props`: `FProp{Kind,Feet,bFlip}`.
  - Decor points: `LilyPads/LilyKinds`, `Reeds`, `FlowerSpots`, `Fires`, `Smokes`.
  - Ambience flags: `bCattails bFireflies bButterflies bBirds bGulls bFog bLava bEmbers bBubbles bWaves`.
  - `Current`; colours `Glint Foam FogColor BirdColor`.
- Canvas: `TexW, TexH, bPortrait, BleedX, BleedY, Origin, FarTop, FarLand, FarWater`.
- Update 2:
  - `bNoWater`, `bInterior`, `WalkBottom` (max feet Y without water).
  - `ResourceSlots`, `DigSpots`, `NetSpots` (feet at ShoreY-4).
  - `bStall/StallFeet`, `bBoard/BoardFeet`, `bDoor/DoorFeet/DoorRect`.
  - Interior: `WallRect, FloorRect, Windows`, `Stations[EFVStation::Count]` (index = EFVStation),
    `DresserFeet, RugCenter, LampFeet, PlantFeet, PaintingCenter`.
- Update 4b: `bPond, PondCenter, PondRadii (not translated), PondPlank`.
- **`TranslateLayout` (FVSceneArt.cpp:4029-4072) must move every new coordinate field.** A missing field goes
  unnoticed in landscape (+112/+64 then -112/-64 cancel) and is wrong only in portrait.

## 6. Background builders (FVSceneArt.cpp)
**BuildLocation(Loc, TexW, TexH, ...)** (4079)
- Seeds `FRandomStream R(hash(Id) ^ (Theme*7919+1))`.
- Portrait (TexH > TexW): `Build*P`, then sets ShoreY, Dock, WalkTop, and WalkMinX..MaxX = visible columns ±18.
- Landscape: `Build*` into a 320x180 `Pic`, then `WorldAnchors`, then `MirrorExtend` (interior: `ClampExtend`),
  then `TranslateLayout(+2BX,+2BY)` into texture space.
- Last, the Far* fills (interior: `HouseDark` (52,36,30); no water: FarWater = FarLand).
- Per-theme dispatch switch at 4092 (portrait) and 4117 (landscape): Pond/River/Mountain/Ocean/Swamp/Volcano/Home/
  HomeInterior. **A new theme needs a case in both switches** (the default falls through to Pond).

**Builders**

| Theme | Landscape | Portrait |
|---|---|---|
| Pond | `BuildPond` 1108 | `BuildPondP` 2061 |
| River | `BuildRiver` 1202 | `BuildRiverP` 2140 |
| Mountain | `BuildMountain` 1355 | `BuildMountainP` 2234 |
| Ocean | `BuildOcean` 1415 | `BuildOceanP` 2325 |
| Swamp | `BuildSwamp` 1513 | `BuildSwampP` 2436 |
| Volcano | `BuildVolcano` 1585 | `BuildVolcanoP` 2531 |
| Home | `BuildHome` 3111 | `BuildHomeP` 3165 |
| HomeInterior | `BuildHouse` 3354 | `BuildHouseP` 3410 |

**Painter toolkit**
- Noise and basics: `VNoise` 26, `Fbm` 34, `PaintBlob` 118, `ThickLine` 151, `Ascii` 170.
- Objects: `MakeOak` 182, `MakePine` 226, `MakePalm` 270, `MakeDeadTree` 332, `MakeRock` 367, `MakeBush` 395,
  `MakeMushroom` 416, `MakeCrystal` 431, `MakeHut` 456, `MakeFishSign` 558, `MakeTent` 582, `MakeShack` 608,
  `MakeBoat` 675, `MakeWell` 1904, `MakeLighthouse` 1948, `MakeLog` 1991, `MakeCottage` 2651 (78x62; door const 2646).
- Terrain: `MakeEdge` 766, `FillGround` 777, `FillWater` 789, `DrawBank` 809, `DrawDock` 869, `ReedClump` 976,
  `Tufts` 1001, `Flowers` 1013, `Pebbles` 1037, `LilyPads` 1054, `PaintRange` 1314, `SkyGradient` 1343,
  `SideScenery` 1752, `CanopyBand` 1774, `PaintPath` 1819, `FenceH` 1846, `Garden` 1863, `Cloud` 2006,
  `WaterRock` 2018, `SoilBed` 2783, `NoWaterEdge` 2819.
- Home: `HomeFlags` 2826; `CottageExtras` 2840 (lights, smoke, bDoor/DoorRect/DoorFeet); `CottageFlowers` 2852;
  `PracticePond` 2870.
- Interior: `InteriorCeiling` 3231, `InteriorBaseboard` 3254, `InteriorWindow` 3270, `InteriorDoor` 3292,
  `InteriorShelf` 3332.

**Contexts**
- `FCtx` (708) fields: `Img, L, Loc, R, TW, TH, Shore, WalkTopRow, WaterDeepRow, VisX0/1, VisY0, Edge, Mask,
  DockX/W/Y0/Y1, Objs`.
  - `AddObj(img, CX, BaseY)` queues a baked object. `PlaceObjs` sorts them by BaseY and draws shadows first.
  - `Reserve` marks the Mask (no scatter there). `C.V(x,y)` converts texel to px.
- `FPFrame` (1688) / `PortraitFrame` (1708):
  - `X(vx)`: visible texel to texture texel. `VX(px)`: visible px to texture px.
  - `Vis(t)`: backdrop row between Hud and WalkTop. `Depth(t)`: water row.
  - Walk band = clamp(round(Hv/2*0.33), 56, 68) texels, i.e. 112-136 px.

**Props and anchors**
- `AddProp` (1076) adds the FProp, a blocker from `DecorBlocker`, and lights/fires/smokes for LampPost, Campfire,
  Brazier, GlowShrooms and Crystal. `WindowRect` 1102.
- `WorldAnchors(C, Theme, bPortrait)` (3511-3650):
  - Per-theme stall, board, net X, resource and dig anchors. Landscape uses raw px; portrait uses `Pt(visX, bandFraction)`.
  - Then `FAnchorFit` (3461) keeps pickups off blockers, props, dock, spawn, door and each other (ring search ≤110 px).
  - Home: refits ResourceSlots around the dig spots, door and pond. HomeInterior: returns immediately.
- Props registry:
  - `EDecor` (FVSceneArt.h:31); update 2 added Well, Woodpile, AppleTree, Scarecrow before Count.
  - `PropImage` 3686 (cases e.g. Signpost 3878, Well 3900, Scarecrow 3922), `BuildDecor` 4170,
    `DecorPivot` 4275, `DecorBlocker` 4295.

### Per-location landscape layouts (canvas px), unchanged since update 2 except anchors
| Loc | ShoreY | Dock x,y,w,h | Spawn | props | stall / board | nets X | res | dig |
|---|---|---|---|---|---|---|---|---|
| pond | 190 | 392,180,40,76 | 352,182 | LampPost, Barrel 476,156, Crates 506,162, Bush 52,150 | 566,140 / 440,136 | 130, 262 | 6 | 1 |
| river | 186 | 244,176,40,84 | 194,178 | Campfire 430,150, Stump, Boulder, Bush | none | 130, 356 | 6 | 1 |
| mountain | 194 | 356,184,40,92 | 312,186 | SnowBoulder, YoungPine, LampPost, Signpost 300,140 | none | 200 | 5 | 0 |
| ocean | 180 | 300,170,48,122 Pier | 254,172 | BeachUmbrella 470,148, Crates, Lifebuoy, Barrel | 580,138 (tr_pier) / none | 110, 420 | 5 | 0 |
| swamp | 188 | 428,178,40,82 | 388,180 | DeadStump, GlowShrooms ×2, LampPost | none | 240, 522 | 6 | 1 |
| volcano | 192 | 332,182,44,90 | 284,184 | Brazier ×2, Crystal ×2, BasaltRock | none | none | 4 | 0 |

- Baked objects, never y-sorted:
  - pond hut: `MakeHut` at 1160, canvas 54..170x22..118;
  - river tent: 1284;
  - ocean boat: 1499; portrait lighthouse: 2410;
  - swamp shack: 1551.
- Portrait anchors: the bPortrait branch of WorldAnchors.

### Home yard (`BuildHome` 3111 / `BuildHomeP` 3165)
- Cottage: centre X 128 texels, base row 74 (landscape). `CottageExtras` adds the door, lit windows and chimney smoke.
- **Baked garden with a scarecrow.** Update 6 §6 removes both (no garden at the start):
  - landscape: `Garden(C,206,104,92,34)` at 3121 and `AddProp(Scarecrow, 506,252)` at 3148;
  - portrait: `Garden` at 3185 and Scarecrow at 3214.
  - (`Garden` is also used by the portrait pond at 2086; keep that one.)
- `SoilBed` 32,128 is the dig bed. Its worms become «компостная куча».
- Props: Well 452,176; Woodpile 364,160; Barrel; AppleTree ×2; LampPost 318,198.
- Walking and slots:
  - WalkTop = base+1 row, WalkBottom 334, X 14..626, Spawn 300,298;
  - DigSpots ×3 at y 274; ResourceSlots ×8;
  - `PracticePond(244,158,36,14)`.

### House interior (`BuildHouse` 3354 / `BuildHouseP` 3410)
- The background paints the ceiling, baseboard, door, windows and shelf. **The wall band and the floor are
  transparent holes**: `DrawInteriorBack` fills them with the equipped wallpaper/floor 16x16 tiles (aligned to
  L.Origin) and draws sky in `Windows`.
- Landscape geometry: WallTop 16, WallBot 75, FloorTop 78, front row 173 (texels); door 32,68; windows 172 and 412, 52.
- Today's stations are a fixed grid (landscape px):
  - Kitchen 136,184; Workshop 242,184; TrophyWall 336,92 (on the wall); Bed 568,200; Smokehouse 84,300;
  - WormFarm 192,324; Radio 302,318; Aquarium 440,300; IceCellar 556,322; Chest 598,262;
  - Dresser 440,184; Lamp 506,224; Plant 28,212; Rug 330,252; Painting 596,86.
  - Portrait: rows via `P(vx, t)`, a lerp over the floor.
- Update 6 §6.4 replaces this grid with rooms and a camera. Smokehouse and WormFarm move to the yard.

## 7. Walkability, movement, auto-walk, pet
- `FVSceneRt::IsWalkable(L,P)` (FVSceneArt.h:290) requires all of:
  - X within [WalkMinX, WalkMaxX];
  - **the safety net `Y ≤ VH-BottomBarH-10` and `Y ≥ TopBarH+66`. These are screen-space and must change with a camera**;
  - no water: WalkTop..WalkBottom, minus the pond oval (`PondDist2(L,P,1)<1` unless `OnPondPlank`);
  - water: WalkTop..ShoreY-2, or the dock rect (±8/-6);
  - not inside any Blocker.
- Tick movement (FVScene.cpp:606-683):
  - Input is normalized.
  - Manual input cancels the auto-walk (`bWalkFailed`). Otherwise `WalkSteer` (FVSceneHome.cpp:637) supplies the direction.
  - At the pond, walking down beside the plank slides the feet onto it. The rescue branch clamps a player who is outside.
  - Movement tries full delta, then X only, then Y only.
  - `UpdateWalkProgress` (670): reached when within Reach; gives up after 1.2 s without progress.
  - On arrival the player faces the object. Step SFX on even walk frames.
- Pet (685-740): a trail point every 5 px (at most 96); teleports beyond 300 px; follows at a 30 px path length;
  ignores collision.

## 8. World objects (FVSceneHome.cpp)
**RebuildWorld** (251)
- Resets Objects/Vis/NetRects/RtLights/RtSmokes/aqua, trims Blockers to BaseBlockers, calls `PublishPond`, then
  builds each section:
  - 309 resource pickups: `P->ResourceAt(Loc,i,Cnt)`, Vis Style 1.
  - 325 stall: `FVDB::TraderAt(Loc)`, `Spr->StallSprite`.
  - 339 order board: `OfferedOrders`.
  - 352 net spots: `P->FindNet(Loc,i)`. Adds the post as Vis layer 3, the net as Vis layer 1 at `ShoreY+2`, and the
    net rect to `NetRects`.
  - 383 dig spots: layer 0.
  - 397 door: no Vis (it is baked).
  - 404 interior:
    - wallpaper/floor tiles;
    - stations by level (`StationSprite(St,Level)`), TrophyWall plaques, aquarium fish;
    - kitchen fire + smokehouse into `RtLights`/`RtSmokes`;
    - dresser `DecorCatalog`;
    - rug/painting/curtains/plant/lamp decor + the lamp light.
  - Finally every `O.Feet` goes through `FindWalkable`.
- Helper lambdas:
  - `AddObj(Kind, Index, Id, Feet, Hit, Reach, Label)`;
  - `AddVis(Obj, Sprite, Pos, Layer, Pivot, SortY)`;
  - `Block(rect)`, `FloorBlocker(Base, Size, WFrac, MaxH)`.
- Namespace helpers: `SizeOf`, `BaseRect`, `Grow`, `Union`, `NetUnits/NetFill`, `StationName/StationLabel`,
  `StallLabel`, `TankRect`, `InteractVerb` (~121), `InteractPrompt`.

**State watch**
- `WorldSignature` (211) hashes: the day, resources, dug spots, nets (item, torn, fill, units), board papers, and in
  the interior station levels, smoke, aquarium, trophies and decor.
- `TickWorld` (602) checks it every 0.25 s and calls `RebuildKeepingState`.
- **Any new profile state that changes what is drawn must be hashed here**, or the world will not refresh.

**Drawing and picking**
- `FVis` (FVSceneArt.h:208):
  - `Layer`: 0 ground, 1 water, 2 wall, 3 y-sorted.
  - `Style`: 0 sprite, 1 pickup, 2 aquarium.
  - `Obj`: object index or INDEX_NONE.
  - Also `Pos, Pivot, Scale, SortY, bShadow, Count, Phase`.
- `ObjectAt(P)` (1008): Hit rect (+6 px on touch); the topmost wins, by layer then SortY.
- `NearestInReach` (1038): `Distance(PlayerPos, Feet) ≤ Reach`.
- `DrawWorldOverlay` (941): name plate + «Коснитесь/Нажмите — verb» prompt above `Hit`. It clamps to `4..VW-4` and
  `TopBarH+4`, which are screen-space clamps.

## 9. DrawScene order (FVScene.cpp:1010-1330)
1. `SyncCanvas`, `EnsureDecor`. Far fills at ±4000 px: no water = FarLand + FarTop; water = FarLand above ShoreY,
   FarWater below. **Update 6 Р38 wants black here for the new locations.**
2. `DrawInteriorBack` (interior only), then `D.Sprite(Bg, L.Origin, WS)`, then `DrawWorldLayer(2)` (wall decor),
   then `DrawWorldLayer(0)` (ground decals).
3. Water layers:
   - ocean waves 1050; shore foam 1076 (water scenes); pond lapping 1087;
   - glints 1099; lily pads & reeds 1121;
   - **nets `DrawWorldLayer(1)` 1147 (water scenes only)**;
   - ripples & bubbles 1149; jumping fish 1176.
4. Y-sorted block 1191-1280. `FEnt{Y,Kind,Index}` kinds:
   - 0 props (sorted by Feet.Y);
   - 1 fires (Y+7);
   - 4 `Vis` layer 3 (SortY, drawn by `DrawVis`);
   - 2 pet; 3 player (+0.01).
   - Sorted with `StableSort`. Player draw 1258-1275 (shadow, sprite, held fish).
5. `DrawWorldAfter` 1282 (pickup fly-up), then particles 1284, then birds/butterflies 1309.
- Pixel snapping: `Snap2(P,1)` for sprites, `Snap2(P,2)` for water decor.

## 10. Lighting (DrawLighting 1332-1464)
Draw order:
1. Fog bands.
2. Rain tint (alpha 28 indoors, 64 outdoors).
3. Dawn/dusk warm tint.
4. Night `(14,20,62)` x Night x (0.46 indoors / 0.6 outdoors).
5. `L.Lights` (static per layout) + lit windows.
6. `DrawWorldLights` 1408: `RtLights` (lamps, kitchen fire, smokehouse); `DayInt` = daytime intensity.
7. Lava/fire glows, embers 1418, fireflies 1430, rain streaks 1445 (outdoors, over `WorldR`).

Tints use `Full` = ±4000 px.

## 11. Water queries (FVScene.cpp:449-516)
- The practice pond is checked first (`L.bPond`):
  - `IsWater` = `InPondWater(-1)`; `DepthAt` = 1 - sqrt(dist);
  - `CanFishFromHere` = `AtPondSpot` (the plank, or the north bank where the rod tip lands in water);
  - `GetCastTarget` clamps into the oval.
- Normal water:
  - `IsWater`: ShoreY..VH-BottomBarH and **X 0..VW** (screen-space); not the dock; **not in `NetRects`**
    (a cast into a net retrieves, FVFishing).
  - `DepthAt` = (Y-Shore)/(Bottom-Shore).
  - `CanFishFromHere`: on the dock, or within ShoreY-12..+1.
  - `GetCastTarget`: straight down from the rod tip; X clamped to 8..VW-8; reach = RodRange x depth, plus half of
    the player's distance out on the dock.

## 12. Art of world objects (FVArtHome.cpp); size table in the file header (lines 6-33)
- Station builders: `BuildKitchen` 420, `BuildWorkshop` 592, `BuildSmokehouse` 721, `BuildWormFarm` 795,
  `BuildAquarium` 871, `BuildIceCellar` 951, `BuildRadio` 1003, `BuildBed` 1026, `BuildTrophyWall` 1069,
  `BuildHouseChest` 1091; construction site `BuildSite` 328. `MaxArtLevel` 2526: higher levels reuse the top art.
- World objects:
  - `TraderBust` 1115;
  - `BuildStallImpl(bPier)` 1178: only 2 stall looks (Mitrich / tr_pier);
  - `BuildOrderBoard` 1321, `BuildNetPost` 1358, `BuildNetWater(Tier,Fill,bTorn)` 1424 (tiers 1-3), `BuildDigSpot` 1512.
- Decor builders: `DecorColors` 1551, `WallpaperTile` 1595, `FloorTile` 1675, `RugWorld` 1751, `LampWorld` 1821,
  `PlantWorld` 1949, `PaintingWorld` 2061, `CurtainsWorld` 2145, `DecorIconImpl` 2205, `BuildTrophyPlaque` 2398,
  `BuildDresser` 2484.
  - Art is chosen by `FFVDecorDef{Slot, Style, Primary, Secondary}`.
  - New expensive furniture = new Style cases, possibly new `EFVDecorSlot` values + layout anchors + a RebuildWorld section.
- Cached getters 2741-2847:
  - `DecorIcon/DecorWorld` (key includes `DecorVisHash`), `StationSprite(S,L)`;
  - `StallSprite(TraderId)`: key 1/2 only, so a new trader needs a new key + art;
  - `OrderBoardSprite`, `NetPostSprite`, `NetWaterSprite`, `DigSpotSprite`, `TrophyPlaque`, `DresserSprite`.
- Geometry getters: `AquariumWaterRect` 2852, `TrophySlotCenter` 2859, `StationEffectPoint` 2864, `DecorLightPoint` 2880.
- Contact sheet: `DebugAddHomeSheet` 2898.
- Primitives: `Box` 89, `Planks` 100, `Bricks` 127, `Stones` 150, `Ellipse` 192, `ShadedEllipse` 209, `Warm` 230,
  `Halo` 250, `Flame` 266, `Glyph` 301, `GableRoof` 687. Palette constants at 53-70.

## 13. UFVGame world side (FVGameHome.cpp)
- Time:
  - `GetAbsMinutes` 142, `GetWeatherTomorrow` 147, `AdvanceClock` 180.
  - `StartNewDay` 256 runs the passive systems at day rollover; this is the hook for the update-6 artel morning collection.
  - `ConfirmSleep` 302, `SleepUntilMorning` 333.
- Home:
  - `IsAtHome` 409, `IsInHouse` 414, `MusicThemeFor` 419 (home and house share music).
  - `LoadHomeScene(Id, bSpawnAtDoor)` 429: Fishing->Reset, LoadLocation, CurrentLocation, door teleport, audio.
    Use it as the model for walking between neighbouring big locations.
  - `EnterHouse` 456, `ExitHouse` 470.
- `GetNearbyTrader` 492 (a stall in reach), `GetEffectiveRodStats` 508.
- `Interact(Obj)` 526 copies Obj first (RefreshWorldObjects invalidates pointers). Cases:
  - Resource 544 (`Gather`), Stall 575, OrderBoard 578, NetSpot 581 (net screen), DigSpot 584, Door 598;
  - Station 608: Bed → sleep; Chest; IceCellar; else `TryOpenFromWorldWith(EFVScreen::Station, Id)`;
  - DecorCatalog 642.
  - New NPCs (artel manager, city quest givers) need a new `EFVObjKind`, a case here, `InteractVerb`, and
    `VerbFor` (FVGameHome.cpp:51).
- `TickWorldInteraction` 650:
  - `GSwallowLmb` eats the rest of a click that was used. Runs only with no screen/modal and fishing Idle.
  - A pending walk-to is tracked by `ObjectKey` (kind|index|id).
  - Object choice:
    - desktop LMB: the hovered object, else the nearest in reach;
    - touch: `In.bWorldTap` → `ObjectAt(In.WorldTapPos)`;
    - E/RMB/gamepad: the nearest.
  - Out of reach: `SetWalkTarget(Feet, Reach*0.8)`.
  - Highlight = pending, else hovered, else nearest.
  - **`In.Mouse` and `WorldTapPos` are used as world coords.**

## 14. Touch (FVTouch.cpp) and onboarding (FVOnboarding.cpp)
- `FVTouchControls::Process` (23):
  - Roles: Pointer / Pending / Joystick / Action.
  - Pending becomes Joystick after `MoveThreshold` (9 px), or Action after `HoldDelay` (0.22 s) if `bCanCast` and
    not `CastBlocker(P)`.
  - A still finger lifted before `TapMaxAge` (0.35 s) sets `In.bWorldTap/WorldTapPos` (59-64).
  - `IsUiArea` (13) covers the top strip and the portrait bottom dock. The update-6 right-side button column must be
    added here.
  - All positions are virtual screen px.
- Onboarding:
  - The stage comes from live state in `ObStage` (~55): GoToPond, LeaveHouse, Cast…, OpenMap. Home and house only.
  - `Draw` (352) points at `FVPracticePond::Get(S).Spot`, the Door object's `Hit`, the player's head and the HUD map
    button. **These world positions are drawn after the HUD in screen space**, so they need world→screen conversion
    once a camera exists.
  - `FVPracticePond::Get` (FVSceneHome.cpp:196) returns the pond published by the last RebuildWorld.

## 15. Update 6 hooks (spec §2, §6, §8.3, Р36-Р38)
- **Camera and big worlds: DONE (SCENE-CAMERA), see §16.** Still open in other packages: convert `In.Mouse` /
  `WorldTapPos` with `ScreenToWorld` (TickWorldInteraction, CastBlocker); world anchors -> screen for onboarding,
  the fishing UI and the name-plate clamps (DrawWorldOverlay clamps to VW); new `EFVTheme` music (`MusicThemeFor`).
- **Stable ids** (spec §2): trees, beds and rubble are saved by id, not by slot index as `ResourceSlots`/`NetSpots` are today.
- **Nets**
  - `NetSpots` are per theme in `WorldAnchors`. The expert location must have none (§10, owner).
  - Artel manager (§8.3): nets the NPC places reuse the `FindNet(Loc,i)` state; morning collection goes in
    `StartNewDay`; add to `WorldSignature` whatever changes the net visuals.
- **House as a real home** (§6.4)
  - Replace `BuildHouse/P` and the fixed `Stations[]` with rooms (сени, кухня, горница, мастерская).
  - Smokehouse/WormFarm anchors move to the yard. The `RebuildWorld` interior section only runs when `L.bInterior`,
    so yard stations need a new section.
  - Expensive furniture = decor defs + new anchors/slots.
- **Yard**
  - Remove `Garden` + Scarecrow (BuildHome 3121/3148, BuildHomeP 3185/3214). Rename the dig bed.
  - Building plots for the lumber mill, garden and forge become world objects with a construction-site look (`BuildSite` 328).

## Pitfalls
- `TranslateLayout` must cover every new FLayout coordinate. Otherwise you get a portrait-only bug.
- `ApplyLayout` overwrites `L`. Runtime blockers and lights added to L are lost unless re-added in `RebuildWorld`
  (Blockers beyond `BaseBlockers`). Put runtime state in RebuildWorld; never write L directly anywhere else.
- Never bake profile-dependent visuals into BuildLocation: the cache key is id + size only. Draw them as `Vis`
  entities and hash their state in `WorldSignature`.
- All Build* functions share one `FRandomStream`. Inserting `C.R` calls or `AddObj` early reshuffles everything after
  it (trees, tufts, flowers). Append new code, or use hash-based placement as `PracticePond` does.
- `EnsureLocation` returns a reference into a TMap that rehashes on Add. Copy the entry immediately.
- Object pointers (`ObjectAt`, `FindObject`, `NearestInReach`) die on any `RebuildWorld`, which can happen every
  0.25 s. Copy the struct. Walk-to targets are keyed by kind|index|id.
- LMB over the world is the cast. A click on an object avoids a cast only because `TickWorldInteraction` runs first
  and consumes it. On touch, a still hold near water casts after 0.22 s unless `CastBlocker` reports an object there.
- Screen-space assumptions to fix with a camera:
  - the `IsWalkable` safety net; `IsWater` X 0..VW; `GetWaterBottomY`; the `GetCastTarget` X clamp;
  - the non-wide `RandomWaterPoint` range; `MapPoint` VW scaling; `DrawWorldOverlay` clamps;
  - onboarding and FVFishing UI placement from `GetPlayerPos` (FVFishing.cpp 1730, 2700-2955); FVTouch `IsUiArea`.
- Scene Tick runs while menus are open, but the clock does not. Timers (nets, smokehouse, garden growth, artel) must
  use profile / absolute game time (`GetAbsMinutes`), never `FImpl::Time`.
- The portrait walk band is only 112-136 px tall. Interactables in the top-right corner collide with the update-6
  button column. Anything below `VH-BottomBarH` is hidden by the portrait dock.
- Registering a new place as an `FFVLocationDef` puts it on the map and into unlocks, `Profile->CurrentLocation`,
  fish/junk tables and music. Home and house avoid that through the `FVDB::IsHomeId` special cases; new non-fishing
  places need the same treatment (or an explicit flag). `EFVTheme::Count` sizes arrays in FVScreenMap.cpp.
- Autotests that new blockers below or left of spawns will break:
  - walk-to-water holds MoveY=1 from Spawn until `CanFishFromHere` (FVAutoTest.cpp:404);
  - the touch swipe test expects more than 20 px of movement (381-390);
  - the pond fishing search runs `DebugTeleport` over a 5 px grid (2252-2283).

## 16. Camera & big worlds (update 6, SCENE-CAMERA; FVSceneCamera.cpp)
- **Which**: `FVWorldU6::IsBigWorldId` = forest, city, cave; home / house only with `-fvbigyard` (until integration).
  `FVDB::FindLocation` misses the forest -> LoadLocation falls back to `FVDBU6::ForestLocation()`.
- **Load** (`LoadLocation` -> `FImpl::ApplyBigWorld`): `FVWorldU6::BuildWorld` once per location id (`BigCache`, one
  composition for every canvas) -> `FVSceneArt::LayoutFromWorld` -> `L` in **world px** (`bBigWorld`, `WorldSize`,
  `WalkRects`, `WaterArea`, Origin 0, never translated; `SyncCanvas` only re-snaps the camera). `WorldR` = world rect.
  Build fails / invalid image -> the classic scene. Entry: `LoadLocationAt(loc, entry)` -> Spawn anchor with that Id,
  else "default"; through the house door without an entry -> "door".
- **LayoutFromWorld**: Walk -> `WalkRects` (empty = world inset 12), bbox -> `WalkMinX/MaxX/Top/Bottom`; Blockers;
  `bNoWater = !bWater`; water: `ShoreY`, `WaterArea` (W 0 -> below ShoreY, full width), `Dock`; dry: `ShoreY` =
  world height. Wall/FloorAreas -> `bInterior`, `WallRect`/`FloorRect` = union, Windows. Pond fields. Anchors:
  Pickup/DigSpot/NetSpot -> `ResourceSlots/DigSpots/NetSpots[Index]`, Door (first) -> `bDoor/DoorFeet/DoorRect`,
  Station (`FVDB::StationId`, or "dresser") -> `Stations[]`/`DresserFeet`, DecorSlot Rug/Lamp/Plant/Painting ->
  the interior points, Board "order_board" -> `bBoard`, Boat -> `bBoat/BoatFeet`, Light -> `Lights` (bAlways in the
  Cave / interior), Fire, Smoke; the default Spawn -> `L.Spawn`. **Missing slots / stations / decor points =
  `UnplacedCoord` (-10000); test with `FVSceneArt::IsPlaced(P)`.** bButterflies -> 14 FlowerSpots on WalkRects.
- **Walk**: `IsWalkable` big branch = inside any WalkRect (or on the dock), not in water below ShoreY-2, pond rule,
  minus Blockers. No screen-space safety net. The rescue clamp uses WalkBottom.
  0.6: a walk rect starting within 32 px of ShoreY is a jetty (walkable over water, like the dock); the rescue of a
  big world snaps to FindWalkable. Barriers (SCENE-OBJECTS): the windfall blocks its whole Hit (the ravine gap), the
  broken footbridge blocks the brook, the Omut zone is blocked until the dam; the mill exit object hides until repair.
- **Background**: cut into 256-texel chunks, key `MakeKey(WorldBg, hash(Id)&0xFFFFFF, chunkIndex, 1)`; fully
  transparent chunks get no texture; the image is dropped once the textures exist. DrawScene: black over
  `VisibleRect(D)` (true device rect under the camera translate), interior back, visible chunks only.
- **Camera** (`UpdateCamera`, from `TickBigWorld` at the end of the movement step, LoadLocation, SyncCanvas,
  SnapCamera): per axis, a world shorter than the band is centred (black sides), else the body (feet - 22) is
  centred and clamped. X band = 0..VW; Y band = TopBarH..VH-BottomBarH (black may show under the HUD strips at the
  world's top/bottom). Eases at 7/s, snaps on load / canvas change / a player jump > 220 px (DebugTeleport).
  `Cam` is rounded to whole px; `BeginWorldView` syncs the canvas first, then `PushTranslate(-Cam)`.
  0.6: the edge lean uses the WHOLE walk box on X (per-row extents made the pier / adit path jump the camera 1000+ px)
  and per-column Y extents blended over 64 px of X; after easing the camera is clamped so the body (62 px) stays in the
  band and 16 px inside the screen, then to the world.
- **Exits**: Exit anchors (Hit, or 40x40 around Feet) -> `GetPendingExit` = (Target, anchor Id = entry at the
  target). Armed only after the player stood outside every exit (no bounce on arrival / after ClearPendingExit).
- **Water** (City pier, Cave lake): `IsWater` = inside WaterArea, ShoreY..WaterArea.Bottom, not dock / nets;
  `GetWaterBottomY` = WaterArea.Bottom (dry: world height); `CanFishFromHere` also needs the rod tip X inside
  WaterArea ±8; `GetCastTarget` clamps X to WaterArea. Glints / rain rings / jumps / foam use WaterArea.
- **Ambience**: fireflies over the walk bbox (16 per 640x200, 8..90); fog bands across the world width; birds fly
  across the camera view; rain streaks over the visible rect; tints cover the visible rect.
- **Cave darkness**: `GetDarkRadius` = `FWorldDef::DarkRadius`. DrawLighting: no dawn/night tints; `DrawBigDarkness`
  = ambient veil (`FWorldDef::Ambient`), then a stepped hole sprite (radius R around the head, breathing ±2.5 %) and
  black rects around it (alpha 0.93); `L.Lights`, fires, runtime lights glow on top at 0.9.
- **Ocean pier boat** (§7): `WorldAnchors` Ocean, both orientations: the right net spot (index 1) is not added to
  `NetSpots`; `bBoat/BoatFeet` = its post point (ShoreY-4); the reserved rect is unchanged, so the resource slots do
  not move. `TranslateLayout` moves `BoatFeet`.

## 17. Wide classic pond (hotfix 0.6.1, Р76)
- The pond (only EFVTheme::Pond, `FFVLocationDef::SideWiden = 0.1`) is a CLASSIC location 1.2 screens wide: landscape
  content canvas −64..704, portrait −36..396 (centred convention: every old object keeps its canvas px, the new side
  strips live at negative X and beyond VW). `FLayout::ScrollRect` (canvas px, W == 0 = one screen, moved by
  TranslateLayout only when W > 0) = content + the right HUD pad (`ScrollPadLand` 80 / `ScrollPadPort` 56).
- `FImpl::HasCamera()` / `FVScene::HasCamera()` = big world or a scrolling classic layout; GetCamera, GetWorldBounds,
  BeginWorldView honour it. `CanvasBgSize(const FFVLocationDef&)` gives the wider texture (the old overload stays).
- Camera (UpdateCamera, classic branch): X only, RESTS at x 0 (the old framing) and is pushed only when the player is
  within MarginL / MarginR of a view side (landscape 128 / 170 — the button column covers the right of the walk band;
  portrait 80 / 80), clamped to ScrollRect; CamJump snap + CamEase + «feet 16 px inside» clamp.
- Art: FCtx OX/ContentW/NX/NW and FPFrame offset keep the old composition pixel-identical; strip art is painted from
  its own random stream (FSideStream). Portrait: Mitrich's stall VX 270 (was 280), the Bush prop moved off the river
  corridor. Exits: FWalkLink FracPort / SignLand / SignPort (hand-placed pond signs at the new edges); RebuildWorldU6
  measures the HUD no-go rect from the scroll right edge when scrolled.
- Screen-space code switched to world bounds (identity for one-screen places): IsWater, GetCastTarget,
  RandomWaterPoint, embers, fireflies/fog, the name plate, FVFishing bobber remap / XP text side, DevTools teleport,
  autotest FindEmptyWorldPoint. Autotest: «u6 pond wide» steps (limits, no voids, signs off the stall/board).
