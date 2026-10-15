// Pixel-art system: CPU images -> transient nearest-filtered textures, plus all procedurally generated game art.
//   FVSprites.cpp        — FFVImage, texture creation, registry/cache, UI icons
//   FVArtItems.cpp       — fish, junk, bait, rod icons; update 2: item art, lures, shovels, ItemIcon resolver
//   FVArtHome.cpp        — update 2: stations, stalls, order board, nets, dig spots, decor, trophy plaques
//                          (size / pivot table at the top of that file)
//   FVArtCharacter.cpp   — player character, hats, outfits, bobbers, pets
// Scene backgrounds/decor are built by the world module (FVScene*.cpp) using FFVImage + MakeSprite.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FVTypes.h"
#include "FVSprites.generated.h"

class UTexture2D;

/** CPU-side RGBA image (sRGB, straight alpha). Row-major, (0,0) = top-left. */
struct FFVImage
{
	int32 W = 0;
	int32 H = 0;
	TArray<FColor> Px;

	FFVImage() = default;
	FFVImage(int32 InW, int32 InH, FColor Fill = FColor(0, 0, 0, 0)) { Init(InW, InH, Fill); }

	void Init(int32 InW, int32 InH, FColor Fill = FColor(0, 0, 0, 0));
	bool IsValid() const { return W > 0 && H > 0 && Px.Num() == W * H; }
	bool InBounds(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < W && Y < H; }
	FColor Get(int32 X, int32 Y) const { return InBounds(X, Y) ? Px[Y * W + X] : FColor(0, 0, 0, 0); }
	void Set(int32 X, int32 Y, FColor C) { if (InBounds(X, Y)) { Px[Y * W + X] = C; } }
	/** Alpha-over blend of C onto the pixel. */
	void Blend(int32 X, int32 Y, FColor C);
	void FillRect(int32 X, int32 Y, int32 RW, int32 RH, FColor C, bool bBlend = false);
	void FillCircle(int32 CX, int32 CY, int32 Radius, FColor C, bool bBlend = false);
	void Line(int32 X0, int32 Y0, int32 X1, int32 Y1, FColor C);
	/** Alpha-over copy of Src at (DX,DY). */
	void Blit(const FFVImage& Src, int32 DX, int32 DY, bool bFlipX = false);
	/** Adds a 1px outline of color C around all opaque pixels (grows into transparent pixels only). */
	void Outline(FColor C);
	/** Multiplies RGB of every pixel (keeps alpha). */
	void Tint(FColor Mul);
	FFVImage FlippedX() const;
	FFVImage Cropped(int32 X, int32 Y, int32 CW, int32 CH) const;

	/**
	 * Builds an image from ASCII rows. Each character is looked up in Palette; characters not in the palette
	 * (and '.' / ' ') are transparent. All rows should have the same length (shorter rows are padded).
	 */
	static FFVImage FromAscii(const TArray<FString>& Rows, const TMap<TCHAR, FColor>& Palette);
	static FFVImage FromAscii(std::initializer_list<const TCHAR*> Rows, const TMap<TCHAR, FColor>& Palette);
};

/** A drawable region of a texture. W/H are in texels (== virtual px at scale 1). */
struct FFVSprite
{
	UTexture2D* Tex = nullptr;                 // kept alive by UFVSprites::Textures
	float W = 0.f;
	float H = 0.f;
	FVector2D UV0 = FVector2D(0, 0);
	FVector2D UV1 = FVector2D(1, 1);

	bool IsValid() const { return Tex != nullptr; }
	FVector2D Size() const { return FVector2D(W, H); }
};

/** Update 6 [ART-THEMES]: colours of an expert fishing screen theme (UFVSprites::ExpertThemeStyle). The warning colours
    (dash red, tension yellow -> red) are NOT part of a theme: they are the same everywhere (Р32, §10.12). */
struct FFVExpertThemeStyle
{
	FColor Frame = FColor(120, 80, 50), FrameDark = FColor(70, 44, 30), FrameLight = FColor(190, 140, 90);
	FColor WaterTop = FColor(70, 150, 200), WaterBottom = FColor(20, 60, 110);
	FColor Zone = FColor(120, 220, 120), ZoneEdge = FColor(230, 255, 230), Zone2 = FColor(120, 200, 240);
	FColor BarFill = FColor(120, 220, 120), BarBack = FColor(40, 30, 24), Particle = FColor(240, 250, 255);
	FColor Text = FColor(250, 240, 220);
};

UCLASS()
class UFVSprites : public UObject
{
	GENERATED_BODY()

public:
	/** Global instance (created and owned by UFVGame). Valid after Init(). */
	static UFVSprites* Get();

	/** Builds the named static sprites (icons etc.). Call once from the game thread. */
	void Init();
	void Shutdown();

	// ------------------------------------------------------------------ core (FVSprites.cpp)
	/** Creates a transient BGRA8 sRGB texture with nearest filtering and no mips. Kept alive by this object. */
	UTexture2D* MakeTexture(const FFVImage& Img, const FString& DebugName);
	FFVSprite MakeSprite(const FFVImage& Img, const FString& DebugName);
	void Register(FName Key, const FFVSprite& Sprite);
	bool Has(FName Key) const { return Named.Contains(Key); }
	/** Named sprite; unknown keys return a magenta placeholder (logged once). */
	const FFVSprite& Get(FName Key);

	/** Generic cache for generated art. Keys: use FVSpriteKey() to avoid collisions between categories. */
	const FFVSprite* FindCached(uint64 Key) const { return Cache.Find(Key); }
	const FFVSprite& AddCached(uint64 Key, const FFVImage& Img, const FString& DebugName);
	static uint64 MakeKey(uint8 Category, uint32 A, uint32 B = 0, uint8 C = 0)
	{
		return (uint64(Category) << 56) | (uint64(C) << 48) | (uint64(A & 0xFFFFFF) << 24) | uint64(B & 0xFFFFFF);
	}

	/** UI icons (see EFVIcon for sizes). */
	const FFVSprite& Icon(EFVIcon I);

	// ------------------------------------------------------------------ items (FVArtItems.cpp)
	/** Side view, facing RIGHT, 1px dark outline. ~12x8 (Minnow) .. 24x14 (Tuna); legends (update 5: own template per
	    FFVFishDef::Legend) up to 42 wide and 20 high: Tsar 32x19, Sturgeon 40x13, Crystal 30x14, Angler 28x19, Fog 42x14, Zhar 33x20. */
	const FFVSprite& Fish(const FFVFishDef& Def);
	/** Same outline/shape as Fish() but filled with a flat dark color (unknown fish in the collection). */
	const FFVSprite& FishSilhouette(const FFVFishDef& Def);
	const FFVSprite& Junk(EFVJunkStyle Style);       // 16x16
	const FFVSprite& Bait(EFVBaitStyle Style);       // 16x16
	const FFVSprite& RodIcon(const FFVRodDef& Rod);  // 16x16 diagonal rod, colored by RodColor/AccentColor
	/** Treasure chest, 16x16. Frame 0 = closed, 1 = open. */
	const FFVSprite& Chest(int32 Frame);

	// ------------------------------------------------------------------ character (FVArtCharacter.cpp)
	/**
	 * Player sprite on a 32x48 canvas; feet at CharacterAnchor(). Includes the rod in fishing animations
	 * (colored with Look.RodColor/RodAccent). Walk: 4 frames; Idle: 2; FishWait: 2; Reel: 2; others: 1.
	 * Left facing may be produced by mirroring Right.
	 */
	const FFVSprite& Character(const FFVAppearance& Look, EFVCharAnim Anim, EFVFacing Facing, int32 Frame);
	static int32 CharacterFrameCount(EFVCharAnim Anim);
	/** Feet position inside the 32x48 character canvas (texels). */
	static FVector2D CharacterAnchor() { return FVector2D(16.f, 46.f); }
	/** Rod tip inside the character canvas for fishing animations (texels); for other anims returns hand position. */
	static FVector2D RodTipOffset(EFVCharAnim Anim, EFVFacing Facing, int32 Frame);
	/** Head-top position inside the canvas (for "!" markers, held fish). */
	static FVector2D HeadTopOffset() { return FVector2D(16.f, 14.f); }

	const FFVSprite& HatIcon(EFVHatStyle Style, FColor Primary, FColor Secondary);         // 16x16 shop/wardrobe icon
	const FFVSprite& OutfitIcon(EFVOutfitStyle Style, FColor Primary, FColor Secondary);  // 16x16
	/** 8x8 bobber; Frame 0/1 = bobbing. */
	const FFVSprite& Bobber(EFVBobberStyle Style, int32 Frame);
	/** 16x16 pet facing Right (Left mirrored by the caller via bFlipX). Idle 2, Walk 4, Sit 2 frames. */
	const FFVSprite& Pet(EFVPetStyle Style, EFVPetAnim Anim, int32 Frame);
	static int32 PetFrameCount(EFVPetAnim Anim);

	// ------------------------------------------------------------------ debug
	/** Renders every generated sprite (icons, all fish of FVDB, junk, bait, rods, chest, character anims/facings
	    with several appearances, hat/outfit icons, bobbers, pets) into one labeled-grid PNG (used by the autotest). */
	void DebugWriteContactSheet(const FString& FilePath);

	// ------------------------------------------------------------------ CPU image builders (additive)
	// Pure functions: build the pixels without creating textures. The cached getters above wrap these;
	// DebugWriteContactSheet uses them directly. Safe to call from any thread (no UObject access).
	static FFVImage BuildIcon(EFVIcon I);                                         // FVSprites.cpp
	static FFVImage BuildMissing();                                               // FVSprites.cpp (16x16 magenta/black checker)
	static FFVImage BuildFish(const FFVFishDef& Def, bool bSilhouette);           // FVArtItems.cpp
	static FFVImage BuildJunk(EFVJunkStyle Style);                                // FVArtItems.cpp
	static FFVImage BuildBait(EFVBaitStyle Style);                                // FVArtItems.cpp
	static FFVImage BuildRodIcon(const FFVRodDef& Rod);                           // FVArtItems.cpp
	static FFVImage BuildChest(int32 Frame);                                      // FVArtItems.cpp
	static FFVImage BuildCharacter(const FFVAppearance& Look, EFVCharAnim Anim, EFVFacing Facing, int32 Frame); // FVArtCharacter.cpp
	static FFVImage BuildHatIcon(EFVHatStyle Style, FColor Primary, FColor Secondary);        // FVArtCharacter.cpp
	static FFVImage BuildOutfitIcon(EFVOutfitStyle Style, FColor Primary, FColor Secondary);  // FVArtCharacter.cpp
	static FFVImage BuildBobber(EFVBobberStyle Style, int32 Frame);               // FVArtCharacter.cpp
	/** EFVPetStyle::None yields a fully transparent 16x16 image (Pet() returns a transparent sprite for it). */
	static FFVImage BuildPet(EFVPetStyle Style, EFVPetAnim Anim, int32 Frame);    // FVArtCharacter.cpp

	// ============================================================================================
	// UPDATE 2 (Art module: FVArtItems.cpp for icons, FVArtHome.cpp for world/house sprites).
	// Cached getters (copy FFVSprite by value in screens). See Docs/UPDATE_HOME.md §Art.
	// ============================================================================================
	/** 16x16 icon of ANY id: fish (fish sprite, not 16x16), junk, bait (incl. crafted lures), generic item, rod, decor.
	    Unknown ids -> magenta placeholder. The one resolver screens should use for item cells. */
	const FFVSprite& ItemIcon(FName ItemId);
	/** Generic item art (16x16): Tint recolors lines/nets, Tier adds rank details. */
	const FFVSprite& ItemArt(EFVItemArt Art, FColor Tint = FColor::White, uint8 Tier = 0);
	/** Crafted lure icon from its parts (16x16): Base 0 spoon / 1 fly, Color 0..3, Glitter 0..2. */
	const FFVSprite& LureIcon(uint8 Base, uint8 Color, uint8 Glitter);
	/** Shovel icon by level 1..4 (16x16). */
	const FFVSprite& ShovelIcon(int32 Level);
	/** House decor icon (16x16) for catalog cells. */
	const FFVSprite& DecorIcon(const FFVDecorDef& Def);

	// ---- world sprites (drawn at FV::WorldScale by FVScene; pivot = bottom-center unless noted)
	/** House station prop by level (Level 0 = construction site with a sign). Sizes ~24..48 texels wide. */
	const FFVSprite& StationSprite(EFVStation S, int32 Level);
	/** Trader stall (awning, counter, the trader behind it). */
	const FFVSprite& StallSprite(FName TraderId);
	/** Order board on posts (Papers = number of pinned notes 0..3). */
	const FFVSprite& OrderBoardSprite(int32 Papers);
	/** Net spot: posts on the shore (always) — NetPostSprite; the net in the water — NetWaterSprite
	    (Tier 1..3 = size, Fill 0..3 = catch amount, torn variant). Pivot of the water net = top-center. */
	const FFVSprite& NetPostSprite(bool bHasNet);
	const FFVSprite& NetWaterSprite(int32 Tier, int32 Fill, bool bTorn);
	/** Soft soil dig spot (dug-today variant = fresh hole). */
	const FFVSprite& DigSpotSprite(bool bDugToday);
	/** House decor in the room: Rug/Lamp/Plant/Painting/Curtains props; Wallpaper/Floor = 16x16 tileable pattern. */
	const FFVSprite& DecorWorld(const FFVDecorDef& Def);
	/** Trophy plaque with the fish (wall-mounted, pivot = center). */
	const FFVSprite& TrophyPlaque(const FFVFishDef& Fish);
	/** Dresser with the decor catalog. */
	const FFVSprite& DresserSprite();

	// ---- CPU builders (pure, used by the contact sheet)
	static FFVImage BuildItemArt(EFVItemArt Art, FColor Tint, uint8 Tier);            // FVArtItems.cpp
	static FFVImage BuildLureIcon(uint8 Base, uint8 Color, uint8 Glitter);            // FVArtItems.cpp
	static FFVImage BuildShovelIcon(int32 Level);                                     // FVArtItems.cpp
	static FFVImage BuildDecorIcon(const FFVDecorDef& Def);                           // FVArtHome.cpp
	static FFVImage BuildStation(EFVStation S, int32 Level);                          // FVArtHome.cpp
	static FFVImage BuildStall(FName TraderId);                                       // FVArtHome.cpp
	static FFVImage BuildDecorWorld(const FFVDecorDef& Def);                          // FVArtHome.cpp
	/** Writes a labeled contact sheet of all update-2 art (icons, item art, lures, shovels, decor, stations, stalls,
	    nets, plaques) to FilePath. DebugWriteContactSheet also calls it with "<name>_Home.png". */
	void DebugAddHomeSheet(const FString& FilePath);                                  // FVArtHome.cpp

	// ---- layout helpers for the world / UI modules (additive; texels from the sprite's TOP-LEFT corner)
	/** Where fish may swim inside StationSprite(Aquarium, Level) (water area, texels). */
	static FFVRect AquariumWaterRect(int32 Level);                                    // FVArtHome.cpp
	/** Center of plaque slot Index 0..5 on StationSprite(TrophyWall, 1): 3x2 slots of 16x12 texels. A TrophyPlaque
	    (30x22) drawn at 1 virtual px per texel exactly fits one slot of the wall drawn at FV::WorldScale. */
	static FVector2D TrophySlotCenter(int32 Index);                                   // FVArtHome.cpp
	/** Kitchen fire / smokehouse chimney top / radio dial / workshop L3 lamp; (-1,-1) when the station has none. */
	static FVector2D StationEffectPoint(EFVStation S, int32 Level);                   // FVArtHome.cpp
	/** Flame / glow point of a Lamp decor inside DecorWorld(Def); (-1,-1) for other slots. */
	static FVector2D DecorLightPoint(const FFVDecorDef& Def);                         // FVArtHome.cpp

	// ============================================================================================
	// UPDATE 6 (Docs/UPDATE6_FOREST_CITY.md §17). Owner package in brackets = the file that implements it.
	// World sprites: drawn at FV::WorldScale, pivot = bottom-centre unless noted. Cache: MakeKey(FVSpriteCat::<cat>, ...).
	// Big-world BACKGROUNDS are not here: see Private/FVWorldU6.h (FVWorldU6::Build*).
	// ============================================================================================
	// ---- [ART-FOREST] FVArtForest.cpp
	/** Tree by kind / state / variant 0..3 (Full ~40x64 texels, Stump ~16x10, Sapling ~12x20; Marked = Full + a notch). */
	const FFVSprite& TreeSprite(EFVTreeKind Kind, EFVTreeState State, int32 Variant);
	/** Forest windfall across the path (~96x40); bCleared = the cut logs pushed aside. */
	const FFVSprite& BlockageSprite(bool bCleared);
	static FFVImage BuildTree(EFVTreeKind Kind, EFVTreeState State, int32 Variant);
	static FVector2D TreeNotchPoint(EFVTreeKind Kind, int32 Variant);                  // notch centre (texels from top-left)

	// ---- [ART-YARD] FVArtYard.cpp
	/** Yard building by level (0 = empty plot with pegs and a sign «Здесь можно построить…»); ~64..96 texels wide. */
	const FFVSprite& YardBuildingSprite(EFVYardBuilding B, int32 Level);
	/** Garden bed (~24x14): CropId None = empty soil; Stage 0 planted .. 3 ripe. */
	const FFVSprite& GardenBedSprite(FName CropId, int32 Stage);
	/** Smokehouse / worm farm moved outdoors (§6.4): yard style variants of StationSprite. */
	const FFVSprite& YardStationSprite(EFVStation S, int32 Level);
	static FFVImage BuildYardBuilding(EFVYardBuilding B, int32 Level);
	/** Effect point (forge glow, sawmill dust) in texels from top-left; (-1,-1) = none. */
	static FVector2D YardEffectPoint(EFVYardBuilding B, int32 Level);

	// ---- [ART-CITY] FVArtCity.cpp
	const FFVSprite& JunkPileSprite(int32 Variant, bool bDepleted);                   // City junk (cans, pipes, a wheel)
	const FFVSprite& RubbleSprite(int32 Index, bool bCleared);                        // cave rubble in the City (0..2)
	/** The boat at the ocean pier by repair stage 0 (wreck) .. 3 (repaired, afloat). ~72x40. */
	const FFVSprite& BoatSprite(int32 Stage);
	/** Voyage scene layers (EFVScreen::Voyage): 0 sky, 1 far sea + coast, 2 near waves, 3 the boat with the player. */
	const FFVSprite& VoyageSprite(int32 Layer, int32 Frame);
	static FFVImage BuildBoat(int32 Stage);

	// ---- [ART-CAVE] FVArtCave.cpp
	const FFVSprite& BoulderSprite(int32 Variant, bool bDepleted);                    // ~32x28
	/** Excavation: the boulder face (8x6 cells of 16 texels) and one cell of rock by layer 1..3. */
	const FFVSprite& ExcavationFace(int32 Variant);
	const FFVSprite& ExcavationCell(int32 Layer, int32 Variant);
	static FFVImage BuildBoulder(int32 Variant, bool bDepleted);

	// ---- [ART-NPC] FVArtNpcU6.cpp — villagers (FVDBU6::Npcs()), same 32x48 canvas and anchor as Character()
	const FFVSprite& NpcSprite(FName NpcId, int32 Frame);                             // idle, Frame 0..1
	const FFVSprite& NpcPortrait(FName NpcId);                                        // 32x32 bust for dialogs / boards
	static FFVImage BuildNpc(FName NpcId, int32 Frame);
	static FFVImage BuildNpcPortrait(FName NpcId);

	// ---- [ART-ITEMS] FVArtItemsU6.cpp (+ the hooks in FVArtItems.cpp / FVSprites.cpp it owns)
	const FFVSprite& AxeIcon(int32 Level);                                            // 16x16, levels 1..4 (pseudo id "axe_N")
	const FFVSprite& PickaxeIcon(int32 Level);                                        // 16x16, levels 1..2 (pseudo id "pickaxe_N")
	static FFVImage BuildAxeIcon(int32 Level);
	static FFVImage BuildPickaxeIcon(int32 Level);

	// ---- [ART-FISH-FIELD] FVArtFishField.cpp — top-down fish for the expert field, facing RIGHT (rotate when drawing)
	/** Any fish (classic fish on an expert rod too, Р18); >= 12 texels long (24 px at 2x); Frame 0..3 = tail swing. */
	const FFVSprite& FieldFish(const FFVFishDef& Def, int32 Frame);
	static int32 FieldFishFrames() { return 4; }
	static FFVImage BuildFieldFish(const FFVFishDef& Def, int32 Frame);

	// ---- [ART-THEMES] FVArtExpertThemes.cpp — the fishing screen of each expert rod (Р32) + field hazards
	static FFVExpertThemeStyle ExpertThemeStyle(EFVExpertTheme Theme);
	/** Frame ring around the field (transparent centre). SizeTexels = outer size (the field is drawn at 2 px / texel). */
	const FFVSprite& ExpertFrame(EFVExpertTheme Theme, int32 SizeTexels);
	/** Water of the field (opaque, SizeTexels square), Frame 0..3 animated. */
	const FFVSprite& ExpertWater(EFVExpertTheme Theme, int32 SizeTexels, int32 Frame);
	/** The zone: band W x H (Pendulum / Twin) or circle W (Compass / Magnet / Helm). bHot = the fish is inside. */
	const FFVSprite& ExpertZoneSprite(EFVExpertTheme Theme, EFVExpertRodType Type, int32 WTexels, int32 HTexels, bool bHot);
	/** Frame of the progress / tension bar (LenTexels long, 6 texels thick). */
	const FFVSprite& ExpertBarFrame(EFVExpertTheme Theme, bool bVertical, int32 LenTexels);
	const FFVSprite& ExpertParticle(EFVExpertTheme Theme, int32 Frame);              // 8x8
	/** Theme ornament: compass rose (Rhumb), helm wheel (Helmsman), anchor + chain (Anchor), star (Pole)... ~32x32. */
	const FFVSprite& ExpertOrnament(EFVExpertTheme Theme);
	/** Helm joystick: base ring (bKnob false) or the knob (true). */
	const FFVSprite& ExpertJoystick(EFVExpertTheme Theme, bool bKnob);
	/** Field hazard visuals (current streaks, whirlpool, wave crest, pile, weeds, falling stone, spring column). */
	const FFVSprite& HazardSprite(EFVHazard Hazard, int32 Frame);
	static FFVImage BuildExpertFrame(EFVExpertTheme Theme, int32 SizeTexels);

	// ---- [MILL-ART] FVArtMill.cpp — «Старая мельница» (§18). ObjId = FVMill::Obj*Id(), State = FVMill::ObjState:
	//      mill_bridge 0 broken / 1 fixed (~64x24); mill 0..3 repair stages (~96x96, the wheel turns when 3: Frame 0..3);
	//      beaver_lodge 0..5 feeds (dam grows, ~56x32); feed_deck 0 / 1 baited (~32x16); fish_pen 0..3 species (~48x24).
	const FFVSprite& MillObjSprite(FName ObjId, int32 State, int32 Frame = 0);
	static FFVImage BuildMillObj(FName ObjId, int32 State, int32 Frame);

	// ---- [Р69 WAY SIGNS] FVArtSigns.cpp — way signs at the walk exits between locations
	/** A way sign on a post, 30x30 texels, feet = bottom-centre: a board with the picture of the target (Picto =
	    FVWorldU6::EWayPicto), shaped as an arrow (Dir = FVWorldU6::EWayDir: 0 left, 1 right, 2 up, 3 down);
	    bLocked = the picture dimmed + a padlock. */
	const FFVSprite& WaySignSprite(uint8 Picto, uint8 Dir, bool bLocked);
	/** A trodden path running off the screen edge (flat ground decal, pivot = centre): Dir 0 left, 1 right, 2 up. */
	const FFVSprite& WayPathSprite(uint8 Dir);
	static FFVImage BuildWaySign(uint8 Picto, uint8 Dir, bool bLocked);

	virtual void BeginDestroy() override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Textures;

	TMap<FName, FFVSprite> Named;
	TMap<uint64, FFVSprite> Cache;
	FFVSprite Missing;
	TSet<FName> WarnedMissing;
};

/** Cache categories for UFVSprites::MakeKey. */
namespace FVSpriteCat
{
	constexpr uint8 Icon = 1, Fish = 2, FishSil = 3, Junk = 4, Bait = 5, Rod = 6, Chest = 7,
		ItemArt = 8, Lure = 9,
		Character = 10, HatIcon = 11, OutfitIcon = 12, Bobber = 13, Pet = 14,
		Station = 15, Stall = 16, HomeProp = 17, Decor = 18, Trophy = 19,
		Scene = 20, Misc = 30,
		// update 6 (one category per art package; A/B/C of MakeKey are the package's business)
		Tree = 21, Yard = 22, City = 23, Cave = 24, Npc = 25, ItemU6 = 26, ExpertTheme = 27, FieldFish = 28,
		Hazard = 29, Forest = 31, DecorU6 = 32, FishU6 = 33, WearU6 = 34, WorldBg = 35, HouseU6 = 36,
		Mill = 37,   // update 6 (§18) [MILL-ART]
		WaySign = 38;   // update 6 (Р69) way signs, FVArtSigns.cpp
}

/** Small sRGB color helpers shared by the art generators (implemented in FVSprites.cpp; usable by any module). */
namespace FVArt
{
	/** Per-channel lerp between two sRGB colors (alpha included). T is clamped to 0..1. */
	FColor Mix(FColor A, FColor B, float T);
	/** Stardew-style shade: darker and slightly shifted towards cool purple. Amount 0..1. Keeps alpha. */
	FColor Shade(FColor C, float Amount);
	/** Highlight: lighter and slightly shifted towards warm cream. Amount 0..1. Keeps alpha. */
	FColor Light(FColor C, float Amount);
	/** Dark tinted outline color for a base color (never pure black). */
	FColor OutlineOf(FColor C);
	FORCEINLINE FColor WithAlpha(FColor C, uint8 A) { C.A = A; return C; }
	/** Deterministic hash noise in [0,1) for integer coordinates and a seed. */
	float Noise(int32 X, int32 Y, uint32 Seed);
	/** Default dark outline used by UI icons and items (dark warm brown). */
	FORCEINLINE FColor InkOutline() { return FColor(56, 32, 36); }
}
