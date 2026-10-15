// FishingValley ("Клёвая Долина") — shared types used by every module.
//
// COORDINATES: all gameplay/UI positions are VIRTUAL pixels on a 640x360 canvas.
// FVDraw maps the virtual canvas to the real viewport (uniform scale + letterbox). +X right, +Y down.
//
// WORLD LAYOUT CONVENTION: land occupies the top part of the screen, the shoreline runs horizontally,
// water fills the bottom part (deeper = further down). The player always casts DOWN (south) into the water,
// so every fishing animation faces the camera (EFVFacing::Down).
//
// TEXT: every player-facing string is Russian, written directly as TEXT("...") literals (files are UTF-8,
// the compiler runs with /utf-8).

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

class UTexture2D;

DECLARE_LOG_CATEGORY_EXTERN(LogFV, Log, All);

// ============================================================================================
// Constants & helpers
// ============================================================================================
namespace FV
{
	// ---- Virtual canvas (runtime: follows the screen orientation, see UpdateCanvasForViewport) ----
	// Landscape (desktop / wide windows): 640 x 360.
	// Portrait (phones held upright): 360 wide, height 600..820 following the screen aspect ratio, so the world
	// and the UI fill the whole display. Code must read these every frame instead of assuming 640 x 360.
	inline float VW = 640.f;                   // virtual canvas width
	inline float VH = 360.f;                   // virtual canvas height
	inline bool bPortraitCanvas = false;
	inline int32 CanvasRevision = 0;           // incremented whenever VW/VH change (scenes rebuild their backgrounds)
	FORCEINLINE bool IsPortrait() { return bPortraitCanvas; }
	/** World pixel art (scene, character, pet, bobber) is drawn at 2 virtual px per texel, which gives the
	    Stardew Valley zoom (16px tiles = 64 screen px at 1280x720). Scene backgrounds are 320x180 texels. */
	constexpr float WorldScale = 2.f;
	constexpr float PlayerSpeed = 172.5f;      // walking speed, virtual px per second (0.6: +15 %, the big worlds)
	constexpr float SimDt = 1.f / 60.f;        // fixed step of the fishing minigame (Stardew runs at 60 ticks/s)
	inline float TopBarH = 24.f;               // HUD strip at the top of the world screen (portrait: compact info strip)
	inline float BottomBarH = 16.f;            // bottom strip (landscape: hint line; portrait: hint line + big button dock)

	/** Picks the canvas for a viewport of PixelW x PixelH pixels (called by FVDraw::Begin every frame). */
	inline bool UpdateCanvasForViewport(float PixelW, float PixelH)
	{
		const bool bPortrait = PixelH > PixelW * 1.1f;
		float NewW = 640.f, NewH = 360.f;
		if (bPortrait)
		{
			NewW = 360.f;
			NewH = FMath::Clamp(FMath::RoundToFloat(360.f * PixelH / FMath::Max(1.f, PixelW)), 600.f, 900.f);
		}
		const bool bChanged = NewW != VW || NewH != VH || bPortrait != bPortraitCanvas;
		VW = NewW;
		VH = NewH;
		bPortraitCanvas = bPortrait;
		// Portrait: a compact info strip on top (settings button top-right) and a big thumb-reachable dock at the
		// bottom (hint line + large buttons for the main screens).
		TopBarH = bPortrait ? 32.f : 24.f;
		BottomBarH = bPortrait ? 84.f : 16.f;
		if (bChanged) { ++CanvasRevision; }
		return bChanged;
	}
	constexpr int32 MaxLevel = 15;             // fishing level cap (update 6: = ExpertMaxLevel; classic uses ClassicMaxLevel)
	// ---- update 6 (Docs/UPDATE6_FOREST_CITY.md §9): DATA-CORE switches MaxLevel to ExpertMaxLevel and moves the
	//      «classic» uses (bar bonus, «Мастер-рыболов») to ClassicMaxLevel. New code uses these two names only.
	constexpr int32 ClassicMaxLevel = 10;      // last level before the City (XP freezes here until the first arrival, Р1)
	constexpr int32 ExpertMaxLevel = 15;       // levels 11..15 are earned in the expert zone
	constexpr float TrackH = 568.f;            // minigame track height, Stardew units (bar/fish positions live in 0..TrackH)
	constexpr float MinuteSeconds = 0.5f;      // real seconds per in-game minute (1 in-game hour = 30 s)
	constexpr float DayStartMinutes = 6.f * 60.f; // new game starts at 06:00

	/** Touch UI (virtual joystick + action button, finger = pointer). Always on phones/tablets; on desktop it can be
	    forced with -fvtouch for testing (the mouse then acts as a single finger). */
	inline bool IsTouchUI()
	{
#if PLATFORM_ANDROID || PLATFORM_IOS
		return true;
#else
		static const bool bForced = FParse::Param(FCommandLine::Get(), TEXT("fvtouch"));
		return bForced;
#endif
	}

	/** sRGB bytes -> linear color for canvas drawing (canvas expects linear colors). */
	FORCEINLINE FLinearColor Col(uint8 R, uint8 G, uint8 B, uint8 A = 255) { return FLinearColor(FColor(R, G, B, A)); }
	FORCEINLINE FLinearColor Col(const FColor& C) { return FLinearColor(C); }
	FORCEINLINE FLinearColor Alpha(FLinearColor C, float A) { C.A = A; return C; }

	/** 12345678 -> "12 345 678" */
	inline FString Money(int64 V)
	{
		const bool bNeg = V < 0;
		const uint64 U = bNeg ? uint64(-V) : uint64(V);
		const FString Digits = FString::Printf(TEXT("%llu"), (unsigned long long)U);
		FString Out;
		const int32 N = Digits.Len();
		for (int32 i = 0; i < N; ++i)
		{
			Out.AppendChar(Digits[i]);
			const int32 Rem = N - i - 1;
			if (Rem > 0 && Rem % 3 == 0) { Out.AppendChar(TEXT(' ')); }
		}
		return bNeg ? (FString(TEXT("-")) + Out) : Out;
	}
}

/** Axis-aligned rectangle in virtual pixels. */
struct FFVRect
{
	float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;

	FFVRect() = default;
	FFVRect(float InX, float InY, float InW, float InH) : X(InX), Y(InY), W(InW), H(InH) {}

	bool Contains(const FVector2D& P) const { return P.X >= X && P.Y >= Y && P.X < X + W && P.Y < Y + H; }
	FVector2D Pos() const { return FVector2D(X, Y); }
	FVector2D Size() const { return FVector2D(W, H); }
	FVector2D Center() const { return FVector2D(X + W * 0.5f, Y + H * 0.5f); }
	float Right() const { return X + W; }
	float Bottom() const { return Y + H; }
	FFVRect Inset(float D) const { return FFVRect(X + D, Y + D, W - 2.f * D, H - 2.f * D); }
	FFVRect Offset(float DX, float DY) const { return FFVRect(X + DX, Y + DY, W, H); }
};

namespace FV
{
	/** Standard rect for overlay windows. Portrait: nearly the whole screen (the dimmed HUD is covered);
	    landscape: the classic 620x318 window under the 24 px HUD bar. */
	inline FFVRect WindowRect()
	{
		return IsPortrait() ? FFVRect(6.f, 20.f, VW - 12.f, VH - 28.f) : FFVRect(10.f, 30.f, 620.f, 318.f);
	}

	/** UI sizes: phones in portrait get a bigger, finger-friendly UI. Use these instead of literal sizes in
	    portrait layouts (landscape keeps its existing literal values). */
	namespace Ui
	{
		FORCEINLINE float Body()    { return IsPortrait() ? 9.5f : 8.f; }   // body text
		FORCEINLINE float Small()   { return IsPortrait() ? 7.5f : 6.5f; }  // secondary text (never smaller)
		FORCEINLINE float Header()  { return IsPortrait() ? 13.f : 11.f; }  // section headers
		FORCEINLINE float Title()   { return IsPortrait() ? 16.f : 13.f; }  // window titles
		FORCEINLINE float ButtonH() { return IsPortrait() ? 36.f : 20.f; }  // standard button height
		FORCEINLINE float RowH()    { return IsPortrait() ? 40.f : 26.f; }  // list row height
		FORCEINLINE float IconBtn() { return IsPortrait() ? 40.f : 18.f; }  // square icon buttons
		FORCEINLINE float Slot()    { return IsPortrait() ? 52.f : 30.f; }  // item grid cells
	}
}

// ============================================================================================
// Enums
// ============================================================================================
enum class EFVRarity : uint8 { Common, Uncommon, Rare, Epic, Legendary, Count };
enum class EFVQuality : uint8 { Normal, Silver, Gold, Iridium, Count };

/** Stardew Valley fish behaviour types (BobberBar motionType 0..4). */
enum class EFVMotion : uint8 { Mixed, Dart, Smooth, Sinker, Floater, Count };

enum class EFVDayPhase : uint8 { Morning, Day, Evening, Night, Count };  // 06-12, 12-18, 18-22, 22-06

/** Bit mask of EFVDayPhase values, used by FFVFishDef::TimeMask. */
namespace FVTime
{
	constexpr uint8 Morning = 1 << 0;
	constexpr uint8 Day = 1 << 1;
	constexpr uint8 Evening = 1 << 2;
	constexpr uint8 Night = 1 << 3;
	constexpr uint8 Any = Morning | Day | Evening | Night;
	FORCEINLINE uint8 Bit(EFVDayPhase P) { return uint8(1u << uint8(P)); }
}

enum class EFVWeather : uint8 { Sunny, Rain, Count };
/** Update 6 (§8.1): local sea state of the City / Cave, on top of the global weather (classic fishing never reads it). */
enum class EFVSeaState : uint8 { Calm, Wind, Storm, Fog, Count };
enum class EFVWeatherReq : uint8 { Any, Sunny, Rain };

/** Visual/audio theme of a location. Home = the player's yard (house exterior + garden, no water),
    HomeInterior = inside the house (stations). Both are NOT fishing locations (see FVDB::IsHomeId). */
enum class EFVTheme : uint8 { Pond, River, Mountain, Ocean, Swamp, Volcano, Home, HomeInterior,
	Forest, City, Cave,   // update 6: big locations (Forest is not a fishing location, like Home)
	Mill,                 // update 6 (§18, Р53): «Старая мельница» — big classic location with water
	Count };

/** Body templates for procedurally drawn fish (side view, facing right). */
enum class EFVFishShape : uint8 { Minnow, Perch, Carp, Pike, Eel, Flounder, Catfish, Puffer, Tuna, Squid, Crayfish, Legend,
	// ---- update 6 (ART-FISH-SIDE draws them: UFVSprites::BuildFish forwards Shape >= FlyingFish to FVArtFishSide::BuildFish)
	FlyingFish, Barracuda, Mackerel, Grouper, Swordfish, Ray, Sunfish, Sailfish, Moray, SeaDragon, Grayling, Proteus, Sturgeon,
	ExpertLegend,   // expert legends: the template is chosen by FFVFishDef::ExpertLegend
	Count };
enum class EFVFishPattern : uint8 { Plain, Stripes, Spots, Glow,
	Scales, Shimmer,   // update 6: big scale rows / crystal-mica shimmer
	Count };

/** Update 5 (Docs/UPDATE5_LEGENDS.md): which of the six legends a fish is (FFVFishDef::Legend; None = an ordinary fish).
    It picks the fight rules (FFVMinigame), the preparation (FFVLegendPrep) and the silhouette; Shape stays Legend. */
enum class EFVLegend : uint8 { None, TsarCarp, Sturgeon, CrystalTrout, Angler, FogSerpent, ZharRyba,
	MillMaster,   // update 6 (§18, Р53): «Хозяин омута» (fish_mill_master), Difficulty 121 = Жар-рыба x1.1
	Count };

// ---- update 6 (Docs/UPDATE6_FOREST_CITY.md §10): expert fishing ------------------------------------------------------
/** Type of an expert rod = its control scheme (§10.3). Two rods of one type are different rods (Р12). */
enum class EFVExpertRodType : uint8 { Pendulum /*Маятник*/, Twin /*Двойчатка*/, Compass /*Компас*/, Magnet /*Магнит*/, Helm /*Штурвал*/, Count };
/** Visual theme of the fishing screen (Р32): Classic = the brown wooden panel of the classic rods. */
enum class EFVExpertTheme : uint8 { Classic, Breeze, Twins, Surf, Rhumb, Mirror, Anchor, Helmsman, Pole, Count };
/** The six expert legends (§10.8); FFVFishDef::ExpertLegend, None for everything else. */
enum class EFVExpertLegend : uint8 { None, KingMarlin, HarborMaster, GhostRay, BlindPatriarch, StoneGiant, CrystalDragon, Count };
/** Yard buildings bought from the builder (§6). Separate from EFVStation (house stations keep their table). */
enum class EFVYardBuilding : uint8 { Sawmill, Garden, Forge, Count };
/** Achievement difficulty tiers (§13): FFVAchievementDef::Tier. */
enum class EFVAchTier : uint8 { Normal, Hard, Expert, Legendary, Count };

enum class EFVJunkStyle : uint8 { Weed, Boot, Can, Driftwood, Bottle,
	Scrap,   // update 6: a rusty piece of metal (металлолом caught as junk, §5.1)
	Count };
/** Nightcrawler = выползок, Dough = тесто, Groundbait = прикормка. Crafted lures (блёсны/мушки from the lure
    workshop) are drawn by UFVSprites::LureIcon from their parts, their Style is Lure (spoon) or Fly.
    Update 5: Ember = Жар-наживка (bait_ember, crafted only). New styles go to the end (before Count). */
enum class EFVBaitStyle : uint8 { Worm, Grub, Lure, Magic, Nightcrawler, Dough, Groundbait, Fly, Ember,
	// ---- update 6 (ART-ITEMS): мусорная приманка, глубинный червь, живец, наживки экспертных легенд
	Trash, DeepWorm, LiveBait, LegendMarlin, LegendHarbor, LegendGiant, LegendDragon,
	// ---- update 6 (§18 «Старая мельница», MILL-ITEMS): хлебная прикормка, мельничный колобок, пучок тростника
	BreadGroundbait, Kolobok, ReedBundle,
	Count };

enum class EFVCosmeticSlot : uint8 { Hat, Outfit, Bobber, Pet, Count };
enum class EFVHatStyle : uint8 { None, Straw, Cap, Bandana, Bucket, Cowboy, Beanie, Ushanka, Pirate, Wizard, Crown,
	Pompom, GoldHat, CaptainCap, Bicorne, MasterCrown,          // update 6 (ART-WEAR)
	MarlinHelm, DragonHelm,                                      // update 6 (Р64): boutique, expert achievements
	Count };
enum class EFVOutfitStyle : uint8 { Overalls, Shirt, Hoodie, Vest, Raincoat, Sailor, Suit,
	Flannel, LumberVest, CaptainCoat, AdmiralUniform, LegendCloak,   // update 6 (ART-WEAR)
	RodMaster, MantaCape,                                            // update 6 (Р64): boutique, expert achievements
	Count };
/** Update 5: Tsar..Zhar are the reward skins of the six legend achievements (§11), one per legend. Cosmetics store the
    style in FFVCosmeticDef::Style (built from data, saves keep cosmetic ids): new styles only at the end. */
enum class EFVBobberStyle : uint8 { Classic, Duck, Star, Heart, Mushroom, Golden, Tsar, Sturgeon, Crystal, Lantern, Fog, Zhar,
	Diamond, Marlin, Kraken, Aurora, Anchor,                     // update 6 (ART-WEAR): boutique + expert legend rewards
	Millstone,                                                   // update 6 (§18, MILL-ITEMS): «Жёрнов» — «Хозяин омута»
	Helm, Dragon,                                                // update 6 (Р64): boutique, expert achievements
	Count };
enum class EFVPetStyle : uint8 { None, Cat, Dog, Duck, Fox, Frog, Penguin,
	Squirrel, Hedgehog, CaptainCat, Peacock,                     // update 6 (ART-WEAR)
	Dragonet,                                                    // update 6 (Р64): boutique, expert achievements
	Count };

enum class EFVFacing : uint8 { Down, Up, Left, Right };

/** Character animations. Fishing animations (CastWindup..HoldFish) exist for Facing==Down only. */
enum class EFVCharAnim : uint8 { Idle, Walk, CastWindup, CastRelease, FishWait, Reel, HoldFish, Count };
enum class EFVPetAnim : uint8 { Idle, Walk, Sit, Count };

enum class EFVAchKind : uint8
{
	CatchTotal,        // Target = number of fish caught (any)
	CatchSpecies,      // Target = number of distinct species caught
	CatchFish,         // Param = fish id, Target = count of that fish
	CatchLegendary,    // Target = number of legendary fish caught
	PerfectTotal,      // Target = number of perfect catches
	TreasureTotal,     // Target = number of treasure chests opened
	IridiumTotal,      // Target = number of iridium-quality fish caught
	EarnTotal,         // Target = total coins earned by selling
	ReachLevel,        // Target = fishing level
	UnlockLocations,   // Target = number of unlocked locations (incl. start)
	CompleteLocation,  // Param = location id: every species there caught (Target ignored, use 1)
	OwnRods,           // Target = number of rods owned
	OwnCosmetics,      // Target = number of cosmetics owned
	// ---- update 2 (дом, станции, ремёсла)
	GatherTotal,       // Target = resources gathered on locations (Stats.Gathered)
	CraftTotal,        // Target = items crafted at the workshop (Stats.Crafted)
	CookTotal,         // Target = dishes cooked (Stats.Cooked)
	SmokeTotal,        // Target = fish smoked (Stats.Smoked)
	DigTotal,          // Target = worms dug up (Stats.WormsDug)
	NetFishTotal,      // Target = fish caught by nets (Stats.NetFish)
	OrdersDone,        // Target = orders delivered in time (Stats.OrdersDone)
	KnotsTotal,        // Target = knots tied successfully (Stats.KnotsTied)
	LuresDiscovered,   // Target = lure recipes discovered
	StationsBuilt,     // Target = house stations built (level >= 1; bed & chest excluded)
	StationsMaxed,     // Target = house stations at max level (bed & chest excluded)
	AquariumFish,      // Target = fish living in the aquarium
	TrophiesMounted,   // Target = trophies on the trophy wall
	OwnDecor,          // Target = house decor items owned (starter items excluded)
	// ---- update 6 (§13; evaluated by DATA-CORE in UFVSaveGame::CheckAchievements via UFVSaveGame::AchievementProgressU6)
	LegendsClassicAll, // all 6 pre-expert legends caught (Target ignored, use 1)
	LegendsExpertAll,  // all 6 expert legends caught (use 1)
	ExpertSpecies,     // Target = distinct expert fish caught (17 = all)
	ExpertRodTypes,    // Target = rod types an expert fish was caught with (5 = all)
	IridiumLegend,     // a legend of iridium quality (use 1)
	FlawlessExpertLegend, // an expert legend caught without leaving the zone once (use 1)
	TreesChopped,      // Target = trees chopped
	Harvests,          // Target = garden harvests
	Forgings,          // Target = forge mini-games completed
	BoatRepaired,      // the boat repaired (use 1)
	BoutiqueSpent,     // Target = coins spent in the boutique
	CityTasksDone,     // Target = City tasks delivered
	FactsKnown,        // Target = fish facts learnt
	Count
};

enum class EFVSfx : uint8
{
	Click, Hover, Open, Close, Cast, Splash, Bite, Hook, ReelTick, Catch, Perfect, Escape,
	Coin, Buy, Error, LevelUp, Treasure, Achievement, Step, Equip,
	// ---- update 2
	Pickup,       // gathered a resource / picked an item
	Dig,          // shovel stroke
	Chop,         // knife cut / swipe
	Sizzle,       // frying (short, retriggered)
	Flip,         // flipping food in the pan
	Hammer,       // building / crafting
	Knot,         // rope pluck (knots mini-game step)
	NetTear,      // a net tore
	Door,         // entering / leaving the house
	Sleep,        // going to bed (descending bells)
	Radio,        // radio static + beeps
	Fire,         // fire crackle (smokehouse fuel)
	OrderDone,    // order delivered
	// ---- update 5 (legends)
	LegendWarn,   // a legend's trick is coming (played with a pitch of its own per legend)
	LegendHook,   // «Легенда на крючке!» banner (a raised Achievement)
	LineSnap,     // the leader snapped: the line was too weak for this legend
	// ---- update 6 (AUDIO-U6; until synthesized they play the default short tick)
	AxeChop,      // axe hits the notch
	AxeStuck,     // axe misses and sticks
	TreeFall,     // a tree falls
	Saw,          // saw stroke (retriggered)
	Anvil,        // forge hammer on hot metal
	Pickaxe,      // pickaxe / hammer on rock
	RockBreak,    // a boulder crumbles
	Find,         // a find uncovered (excavation) / a fact learnt
	BoatHorn,     // the boat sails
	Tension,      // expert line creak (tension high)
	GravityFlip,  // Pendulum: gravity flipped
	CompassTick,  // Compass: the arrow clicks
	Whoosh,       // expert fish dash / wave
	ExpertWarn,   // an expert legend's move is coming (pitch per legend)
	Harvest,      // garden harvest
	Plant,        // seed planted
	Coins,        // artel income
	Count
};

/** Overlay screens. None == the world (walking & fishing).
    Screens that need a parameter read UFVGame::GetScreenContext() in OnOpen (see UFVGame::OpenScreenWith). */
enum class EFVScreen : uint8
{
	None, MainMenu, Map, Shop, Inventory, Wardrobe, Collection, Achievements, Pause,
	// ---- update 2
	Chest,        // house chest <-> backpack transfer
	IceCellar,    // ice cellar <-> keepnet transfer
	Station,      // a house station (context = station id "st_kitchen"...): build/upgrade + its own content
	Orders,       // the order board at the pond
	NetSpot,      // a net spot at the shore (context = "<locationId>:<spot>")
	HomeDecor,    // house decor catalog (buy / equip wallpaper, floor, rug...)
	Cooking,      // mini-game: cooking (context = recipe id)
	LureCraft,    // mini-game: lure workshop (no context)
	Knots,        // mini-game: knots "repeat the pattern" (context = recipe id)
	Digging,      // mini-game: digging worms (context = "<locationId>:<spot>")
	// ---- update 3
	Tutorial,     // the illustrated guide (opened from the HUD button next to the settings; context = entry id or None)
	// ---- update 4
	Profile,      // fisherman profile (context: None = own, "setup" = first-run name choice, "<pid>" = another player)
	Leaderboard,  // leaderboards: all-time categories + the 3-day tournament
	Mail,         // letters from the admin / tournament rewards
	Levels,       // what every fishing level unlocks (from the HUD level badge)
	RestoreSave,  // restore a save by a one-time code (from Settings)
	// ---- update 6 (factories in FVScreens.h; owners in Docs/UPDATE6_FOREST_CITY.md §17)
	Chopping,     // mini-game: chopping (context = "<loc>:<treeId>" or "forest:blockage")
	Sawing,       // mini-game: sawing a log at the sawmill (context = recipe id or None)
	Forging,      // mini-game: forging (context = recipe id)
	Excavation,   // mini-game: excavation of a boulder (context = "<loc>:<boulderId>" or "city:<rubbleId>")
	Builder,      // the builder Гаврила: yard buildings, yard decor, furniture (context = trader id)
	YardBuilding, // a yard building: sawmill / forge (context = "yb_sawmill" / "yb_forge")
	GardenBed,    // a garden bed: plant / harvest (context = "bed_<n>")
	Boat,         // the broken boat at the pier: repair stages, «Отплыть» (no context)
	Voyage,       // the short sailing scene (context = "city" / "ocean": destination)
	CityBoard,    // City task board (context None)
	Artel,        // the artel master Никифор (context None)
	ExpertTackle, // expert rod configuration + saved configurations (context = rod id or None)
	Mill,         // update 6 (§18) [MILL-SCREENS]: context "mill" = the working mill (rye -> flour), "pen" = the fish pen
	Count
};

enum class EFVAlign : uint8 { Left, Center, Right };

/** UI icons provided by UFVSprites::Icon(). 16x16 texels unless noted. */
enum class EFVIcon : uint8
{
	Coin,          // 8x8 gold coin
	StarSilver, StarGold, StarIridium,   // 8x8 quality stars
	Lock, Map, Shop, Keepnet, Book, Trophy, Wardrobe, Menu,
	Clock, Sun, Rain, Moon, Xp, Treasure, Check, Exclaim, Question,
	ArrowLeft, ArrowRight, Heart, Close, Cursor, Level, Fish, Bait, Rod,
	// ---- update 2 (16x16)
	Backpack, House, Hammer, Pot, Net, Shovel, Order, Snowflake, Radio, Bed, Smoke, Aquarium, Hand, Door,
	Knot, Food, Line, Float, Chest,
	// ---- update 5 (16x16): legend fight warnings drawn next to the minigame panel
	WarnCurrent,   // ▼ the current (Sturgeon)
	WarnEye,       // turning transparent (Crystal trout)
	WarnLantern,   // the lantern (Angler)
	WarnCloud,     // the fog (Fog serpent)
	WarnFlame,     // the eruption (Zhar-ryba)
	WarnSpark,     // the blink (Zhar-ryba, last phase)
	// ---- update 6 (16x16, ART-ITEMS): HUD, world prompts, expert fight, local weather
	Leaders,       // leaderboard button of the HUD column
	Axe, Pickaxe, Saw, Anvil, Seed, Boat, Scroll /*fact / task*/, Artel, ExpertRod,
	SeaCalm, SeaWind, SeaStorm, SeaFog, Tide, Dark,
	WarnWave, WarnWhirl, WarnDrip, WarnSpring, WarnDash, Tensionmeter,
	Count
};

// ============================================================================================
// Content definitions (filled by FVDatabase.cpp)
// ============================================================================================
struct FFVLocationDef
{
	FName Id;
	FString Name;               // "Деревенский пруд"
	FString Description;        // 1-2 sentences
	EFVTheme Theme = EFVTheme::Pond;
	int32 UnlockPrice = 0;      // coins to unlock (0 = free / starting)
	int32 MinLevel = 1;         // fishing level required to unlock
	FVector2D MapPos = FVector2D(320, 180);  // pin position on the world map (virtual px)
	float JunkChance = 0.1f;    // base chance that a bite is junk
	float TreasureChance = 0.1f;// base chance a treasure chest appears in the minigame
	FColor WaterShallow = FColor(90, 170, 220);
	FColor WaterDeep = FColor(30, 80, 150);
	// ---- update 6
	bool bExpertZone = false;   // Город, Пещера: expert fish, local weather, no nets (Р35)
	bool bBigWorld = false;     // one composition with a camera (forest, yard, house, city, cave)
	// ---- hotfix 0.61: a classic location wider than the screen (horizontal scroll camera). Each side grows by this
	//      fraction of the screen width. Only EFVTheme::Pond honours it (its builders know the offset).
	float SideWiden = 0.f;
};

/** Update 5: what a legend asks of the tackle (checked by FVDB::LegendGate / LegendChecklist) and its texts (§3).
    Hard requirements (Bait / BaitStyle / Float / Lines): without them the legend does not bite at all.
    Soft one (SnapBelowLineTier): it bites, but snaps the leader at the end of the hook intro. Empty = not required. */
struct FFVLegendPrep
{
	FName Bait;                             // required bait id (e.g. bait_nightcrawler, bait_ember)
	EFVBaitStyle BaitStyle = EFVBaitStyle::Count;   // or any bait of this style (Count = no style rule)
	FName Float;                            // required float item id (float_glow)
	TArray<FName> Lines;                    // allowed line ids (thin lines for the trout, line_heat for the Zhar-ryba)
	uint8 SnapBelowLineTier = 0;            // a line of a lower tier snaps the leader (0 = never)
	TArray<FString> PrepItems;              // short checklist lines (<= 20 chars), in the order of the rules above
	FString PrepText;                       // «Подготовка» paragraph
	FString FightText;                      // «Приёмы» paragraph (shown after the first meeting)
	FString GatedToast;                     // toast when a hard rule kept the legend away (once per visit)
	FString SnapText;                       // floating text when the leader snaps
};

struct FFVFishDef
{
	FName Id;
	FString Name;               // Russian name, e.g. "Карась"
	FString Description;        // short flavor text
	FName Location;             // FFVLocationDef::Id
	EFVRarity Rarity = EFVRarity::Common;
	int32 BasePrice = 10;       // sell price at Normal quality
	int32 Difficulty = 30;      // Stardew scale (~15 easy .. 110 legendary)
	EFVMotion Motion = EFVMotion::Mixed;
	float MinSize = 10.f;       // cm
	float MaxSize = 30.f;       // cm
	uint8 TimeMask = FVTime::Any;
	EFVWeatherReq Weather = EFVWeatherReq::Any;
	int32 MinLevel = 1;         // fishing level needed for it to bite
	float MinDepth = 0.f;       // 0..1 bobber depth needed (0 = at the shore, 1 = far/deep water)
	float Weight = 10.f;        // relative spawn weight inside its location
	bool bLegendary = false;    // one per location; huge, rare, hard
	// Visuals (procedural sprite)
	EFVFishShape Shape = EFVFishShape::Perch;
	EFVFishPattern Pattern = EFVFishPattern::Plain;
	FColor BodyColor = FColor(120, 140, 90);
	FColor BellyColor = FColor(220, 220, 180);
	FColor FinColor = FColor(200, 90, 60);
	FColor PatternColor = FColor(60, 70, 40);
	// ---- update 5 (legends)
	EFVLegend Legend = EFVLegend::None;   // which legend (fight rules, silhouette); None for ordinary fish
	FFVLegendPrep Prep;                   // tackle rules and texts (legends only)
	// ---- update 6: expert fish / legends live in Fish() too (collection, keepnet, prices, sprites) but classic
	//      PickCatch never picks them; their rules are FVDBU6::FindExpertFish / FindExpertLegend (FVDatabaseU6.h).
	bool bExpert = false;
	EFVExpertLegend ExpertLegend = EFVExpertLegend::None;   // bExpert && != None: an expert legend (bLegendary too)
};

struct FFVJunkDef
{
	FName Id;
	FString Name;
	FString Description;
	EFVJunkStyle Style = EFVJunkStyle::Weed;
	int32 Price = 1;
};

struct FFVRodDef
{
	FName Id;
	FString Name;
	FString Description;
	int32 Tier = 0;             // 0 = starter
	int32 Price = 0;
	int32 MinLevel = 1;
	float BarBonus = 0.f;       // extra catch-bar height (track units; base bar 96 + 8 per level above 1)
	float ReelMult = 1.f;       // progress gain multiplier while the fish is inside the bar
	float DrainMult = 1.f;      // progress loss multiplier while the fish is outside (lower = better)
	float BiteSpeed = 1.f;      // waiting-time multiplier (lower = faster bites)
	float CastRange = 0.6f;     // 0..1 fraction of the full water depth reachable at max power
	float Luck = 0.f;           // bonus to rare+ spawn weights (0.5 = +50%)
	float TreasureBonus = 0.f;  // added to treasure chance
	FColor RodColor = FColor(200, 170, 90);
	FColor AccentColor = FColor(90, 60, 40);
	uint8 TraderMask = 1;       // which traders sell it (FFVTraderDef::Mask bits; 1 = Mitrich, 2 = pier trader)
	// ---- update 6: expert rods are Rods() entries; their control characteristics are FVDBU6::FindExpertRod(Id)
	bool bExpert = false;
	EFVExpertRodType ExpertType = EFVExpertRodType::Count;
	EFVExpertTheme Theme = EFVExpertTheme::Classic;   // fishing screen design (Р32)
	// ---- 0.6.2 (Р81): the trader sells it only after this achievement (NAME_None = no lock)
	FName RequiredAchievement;
};

struct FFVBaitDef
{
	FName Id;
	FString Name;
	FString Description;
	EFVBaitStyle Style = EFVBaitStyle::Worm;
	int32 PackPrice = 10;       // price of one pack
	int32 PackSize = 10;        // baits per pack (one bait is consumed per bite)
	int32 MinLevel = 1;
	float BiteSpeed = 1.f;      // waiting-time multiplier (lower = faster)
	float RareBonus = 0.f;      // bonus to rare+ spawn weights
	float JunkMult = 1.f;       // multiplier on junk chance
	float SizeBonus = 0.f;      // bonus to the size roll (0..0.3)
	// ---- update 2
	uint8 TraderMask = 1;       // which traders sell packs (0 = not sold: crafted / dug / farmed only)
	/** Crafted lure from the lure workshop (id "lure_<base>_<color>_<glitter>", see FVDB::LureId). */
	bool bLure = false;
	uint8 LureBase = 0;         // 0 = блесна (spoon), 1 = мушка (fly)
	uint8 LureColor = 0;        // 0 red, 1 yellow, 2 green, 3 black
	uint8 LureGlitter = 0;      // 0 none, 1 silver, 2 gold
	/** Attraction: fish whose shape bit (1 << EFVFishShape) / motion bit (1 << EFVMotion) matches get their spawn
	    weight multiplied by (1 + AffinityMult * matches) in FVDB::PickCatch. */
	uint16 AffinityShapes = 0;
	uint8 AffinityMotions = 0;
	float AffinityMult = 0.f;
	// ---- update 5: a legend's favourite bait. In PickCatch that legend's weight is multiplied by
	// max(LureAffinity, LegendLureMult) (not their product); every other fish by OtherFishMult (bait_ember: 0.35).
	EFVLegend LegendLure = EFVLegend::None;
	float LegendLureMult = 1.f;
	float OtherFishMult = 1.f;
};

struct FFVCosmeticDef
{
	FName Id;
	FString Name;
	FString Description;
	EFVCosmeticSlot Slot = EFVCosmeticSlot::Hat;
	uint8 Style = 0;            // EFVHatStyle / EFVOutfitStyle / EFVBobberStyle / EFVPetStyle depending on Slot
	int32 Price = 0;            // 0 = not sold in the shop (reward / starting item)
	int32 MinLevel = 1;
	FColor Primary = FColor::White;
	FColor Secondary = FColor::Black;
	uint8 TraderMask = 0;       // update 6: traders that sell it (0 = legacy: every trader with bSellsClothes)
	bool bElite = false;        // update 6: boutique item («Элитное» wardrobe tab)
	FName RequiredAch;          // update 6 (Р64): sold only after this achievement is completed (NAME_None = no condition)
};

struct FFVAchievementDef
{
	FName Id;
	FString Name;
	FString Description;
	EFVAchKind Kind = EFVAchKind::CatchTotal;
	int32 Target = 1;
	FName Param;                // fish id / location id for CatchFish / CompleteLocation
	int64 RewardMoney = 0;
	FName RewardCosmetic;       // optional cosmetic granted on completion
	FName RewardDecor;          // optional house decor granted on completion (update 2)
	uint8 Tier = 0;             // update 6: EFVAchTier (tabs of the achievements screen)
};

struct FFVKeepnetLevel
{
	int32 Capacity = 12;        // max number of items (sum of stack counts)
	int32 Price = 0;            // cost to upgrade TO this level (0 for level 0)
};

// ============================================================================================
// Runtime structs
// ============================================================================================

/** Everything needed to draw the player character. Built by UFVSaveGame::BuildAppearance(). */
struct FFVAppearance
{
	EFVHatStyle Hat = EFVHatStyle::None;
	FColor HatPrimary = FColor::White, HatSecondary = FColor::Black;
	EFVOutfitStyle Outfit = EFVOutfitStyle::Overalls;
	FColor OutfitPrimary = FColor(70, 110, 180), OutfitSecondary = FColor(230, 220, 200);
	FColor RodColor = FColor(200, 170, 90), RodAccent = FColor(90, 60, 40);

	uint32 GetHash() const
	{
		uint32 H = GetTypeHash(uint8(Hat));
		H = HashCombine(H, GetTypeHash(HatPrimary)); H = HashCombine(H, GetTypeHash(HatSecondary));
		H = HashCombine(H, GetTypeHash(uint8(Outfit)));
		H = HashCombine(H, GetTypeHash(OutfitPrimary)); H = HashCombine(H, GetTypeHash(OutfitSecondary));
		H = HashCombine(H, GetTypeHash(RodColor)); H = HashCombine(H, GetTypeHash(RodAccent));
		return H;
	}
};

/** Context used to decide what bites. */
struct FFVCatchContext
{
	FName Location;
	float Depth = 0.f;                 // 0..1 bobber depth
	uint8 PhaseMask = FVTime::Day;     // FVTime::Bit(current phase)
	EFVWeather Weather = EFVWeather::Sunny;
	int32 Level = 1;
	const FFVRodDef* Rod = nullptr;
	const FFVBaitDef* Bait = nullptr;  // nullptr = no bait
	// ---- update 2 (value fields: never point Rod at a temporary)
	float ExtraLuck = 0.f;             // tackle + food luck, added to Rod->Luck in PickCatch
	float JunkMultScale = 1.f;         // tackle/food junk multiplier (applied after the bait's JunkMult)
	bool bNet = false;                 // passive net: no rod/bait rules ("empty hook" x1.15 junk NOT applied), no legendaries
	// ---- update 5: tackle installed on the rod (UFVGame::MakeCatchContext fills them from the loadout; legend gates)
	FName Line;                        // line item id (NAME_None = the default простая леска)
	FName Float;                       // float item id (NAME_None = the default пробковый поплавок)
	uint8 LineTier = 0;                // FFVItemDef::Tier of Line (0 = the default line)
	// ---- update 6 (FVGameU6::FillCatchContext)
	bool bExpertRod = false;           // the equipped rod is an expert rod
	bool bExpertZone = false;          // Город / Пещера
	// ---- update 6 (§18 «Старая мельница»; FVGameU6::FillCatchContext fills them, FVMill::IsFishAllowed / CatchWeightMult
	//      read them inside FVDB::PickCatch). Nets: bMillOmut = the Omut net spot (FVMill::OmutNetSpot)
	bool bMillOmut = false;            // fishing in the Omut (behind the finished beaver dam)
	bool bMillDeck = false;            // fishing from a baited feeder deck (30 game min): rare / epic x1.5
	TArray<FName> MillPenFish;         // species released into the fish pen (3 game days): x1.5 weight
};

/** Update 5: FVDB::LegendGate verdict for one legend with the current tackle. */
enum class EFVLegendGate : uint8
{
	Ok,        // everything is ready (or not a legend)
	NoBite,    // a hard rule is missing: the legend is skipped in PickCatch
	Snap       // only a soft rule is missing: it bites, then snaps the leader at the end of the hook intro
};

/** FFVLegendGateResult::Missing bits. */
namespace FVLegendMissing
{
	constexpr uint8 Bait = 1 << 0;       // required bait / bait style (hard)
	constexpr uint8 Float = 1 << 1;      // required float (hard)
	constexpr uint8 LineKind = 1 << 2;   // line not in FFVLegendPrep::Lines: not thin (trout) / not heat-proof (Zhar-ryba) (hard)
	constexpr uint8 LineTier = 1 << 3;   // line tier below SnapBelowLineTier (soft -> Snap)
}

struct FFVLegendGateResult
{
	EFVLegendGate Result = EFVLegendGate::Ok;
	uint8 Missing = 0;                 // FVLegendMissing bits
};

/** One row of the live preparation checklist (FVDB::LegendChecklist): drawn with EFVIcon::Check / Close. */
struct FFVLegendCheckItem
{
	FString Label;                     // FFVLegendPrep::PrepItems entry, e.g. «леска: плетёная+»
	bool bOk = false;                  // satisfied by the current tackle
	bool bHard = false;                // hard rule (no bite without it) vs soft (leader snap)
};

struct FFVTreasure
{
	int64 Money = 0;
	FName BaitId;               // optional bait reward
	int32 BaitCount = 0;
	FName CosmeticId;           // optional rare cosmetic reward (not yet owned)
	FName ItemId;               // optional item reward (update 2: tackle, glitter, materials) -> backpack
	int32 ItemCount = 0;
	// ---- update 6 (§10.16): a page of the fisherman's journal = one fact about an expert fish
	FName FactFish;             // NAME_None = no page
	int32 FactIndex = INDEX_NONE;
};

/** Result of a completed catch. Filled by FVFishing, then completed by UFVGame::HandleCatch. */
struct FFVCatchResult
{
	const FFVFishDef* Fish = nullptr;   // exactly one of Fish/Junk is set
	const FFVJunkDef* Junk = nullptr;
	EFVQuality Quality = EFVQuality::Normal;
	float SizeCm = 0.f;
	bool bPerfect = false;
	bool bTreasure = false;
	FFVTreasure Treasure;
	int32 Xp = 0;
	int32 UnitPrice = 0;
	// filled by UFVGame::HandleCatch
	bool bNewSpecies = false;
	bool bNewRecord = false;
	bool bStored = false;               // added to the keepnet
	int32 LevelsGained = 0;
	// ---- update 6
	bool bExpert = false;               // caught in the expert fight (expert rod)
	bool bFlawless = false;             // the fish never left the zone (expert legends: achievement)
	uint8 RodType = 0xFF;               // EFVExpertRodType of the rod used (0xFF = classic rod)
	int32 CoinsFromFrozenXp = 0;        // Р10: XP turned into coins while the level is frozen (for the card)
};

// ============================================================================================
// Input snapshot (built every tick by AFVPlayerController)
// ============================================================================================
/** A finger currently touching the screen (virtual coords). */
struct FFVTouchPoint
{
	int32 Id = 0;
	FVector2D Pos = FVector2D::ZeroVector;
};

struct FFVInput
{
	FVector2D Mouse = FVector2D(-1000, -1000);  // virtual coords
	bool bMouseValid = false;
	bool bLmbDown = false, bLmbPressed = false, bLmbReleased = false;
	bool bRmbPressed = false;
	int32 Wheel = 0;                   // +1 wheel up, -1 wheel down this frame
	float MoveX = 0.f, MoveY = 0.f;    // WASD / arrows, -1..1 each (Y+ = down)
	bool bShiftDown = false;
	/** "Action" = Space, or LMB when the cursor is NOT over a UI widget (resolved by UFVGame before the world/fishing tick). */
	bool bActionDown = false, bActionPressed = false, bActionReleased = false;
	TArray<FKey> Pressed;              // keys that went down this frame
	TArray<FFVTouchPoint> Touches;     // fingers down (touch UI only); FVTouchControls turns them into the fields above
	// ---- update 2: world interaction
	bool bInteractPressed = false;     // E / RMB / gamepad X: interact with the nearest world object in reach
	bool bWorldTap = false;            // touch: a short still tap on the world this frame (FVTouchControls)
	FVector2D WorldTapPos = FVector2D::ZeroVector;

	bool WasPressed(const FKey& K) const { return Pressed.Contains(K); }
};

// ============================================================================================
// UPDATE 2: items, house & stations, traders, nets, crafting (see Docs/UPDATE_HOME.md)
// ============================================================================================

/** Generic (non-fish, non-bait) item kinds. They live in the backpack / chest. */
enum class EFVItemKind : uint8
{
	Resource,   // gathered on locations: ветка, камешек, волокно, перо, гриб, ягоды, тростник, глина, смола
	Material,   // crafted / recycled: доска, верёвка, клей, металлолом, грузило, блёстки
	Food,       // eaten for a fishing buff: уха, жареная рыба, ягодный пирог, травяной чай
	Line,       // rod slot "Леска" (installed at the workshop)
	Float,      // rod slot "Поплавок" (installed at the workshop)
	Net,        // placed at a net spot on the shore
	// ---- update 6
	Seed,       // planted in a garden bed (FVDBU6::FindSeed)
	Reel,       // expert rod slot "Катушка" (FFVItemDef::bExpert)
	Blueprint,  // bought once: learnt immediately (UFVSaveGame::U6.Blueprints), never stored
	Count
};

/** Art identity of a generic item icon (16x16, UFVSprites::ItemArt). Lines and nets are recolored with
    FFVItemDef::Tint; Tier adds rank details (e.g. metal parts). */
enum class EFVItemArt : uint8
{
	Branch, Pebble, Fiber, Feather, Mushroom, Berries, Reed, Clay, Resin,       // resources
	Board, Rope, Glue, Scrap, Sinker, Glitter,                                  // materials
	FishSoup, FriedFish, BerryPie, HerbTea,                                     // food
	Line,                                                                       // lines (spool, tinted)
	FloatFeather, FloatReed, FloatSliding, FloatGlow, FloatSport, FloatGold,    // floats
	Net,                                                                        // nets (tinted, Tier = size)
	// ---- update 5 (§10.2): fried fish by rarity (a pan dish like FriedFish; rim colour by rarity, the legend's is gold)
	FriedFishRare, FriedFishEpic, FriedFishLegend,
	// ---- update 6 (ART-ITEMS; UFVSprites::BuildItemArt forwards them to FVArtItemsU6::BuildItemArt)
	Log, HardRock, Mpk, Nails, BoatFrame,                                       // materials
	SeedPacket,                                                                 // seeds (packet tinted by Tint)
	Carrot, Potato, Onion, Dill, Pumpkin,                                       // vegetables
	CarrotSoup /*unused: Р41, the only уха needs a carrot*/, Draniki, FishDill, OnionSoup, PumpkinPorridge, FishPie,   // new dishes
	ExpertLine,                                                                 // expert line spool (Tint: thin / heavy)
	FloatSurface, FloatDeep, FloatGlowExpert,                                   // expert floats
	ReelLight, ReelHeavy,                                                       // reels (Tier 2 = tempered)
	Blueprint,                                                                  // blueprint roll
	// ---- update 6 (§18 «Старая мельница», MILL-ITEMS): рожь, ржаная мука, блины, рыбный пирог
	Rye, Flour, Bliny, RyeFishPie,
	Count
};

/** House stations. Bed and Chest exist from the start (level 1, cannot be upgraded). */
enum class EFVStation : uint8
{
	Kitchen,     // Очаг -> Плита -> Печь: food buffs, тесто, прикормка
	Workshop,    // Стол -> Верстак -> Станок: доски, верёвка, клей, снасти, сети, переработка мусора, лопаты, оснастка удочек
	Smokehouse,  // 1-2: копчёная рыба (x2 price), 6 game hours with fuel
	WormFarm,    // 1-2: 2-4 worms every morning
	Aquarium,    // Банка -> Аквариум -> Большой аквариум: live fish 1 / 4 / 10
	IceCellar,   // 1-2: fish storage 10 / 25
	Radio,       // 1: tomorrow's weather forecast
	Bed,         // built from the start: sleep until morning
	TrophyWall,  // 1: stuffed record fish (6 plaques)
	Chest,       // built from the start: item storage
	Count
};

/** House decor slots (cosmetics for the house interior). Update 6: yard decor (Yard*) and furniture of the new house
    (§6.4). Screens that only know the classic slots iterate FVDecor::ClassicSlots, not Count. */
enum class EFVDecorSlot : uint8 { Wallpaper, Floor, Rug, Lamp, Plant, Painting, Curtains,
	// ---- update 6: yard (builder, boutique)
	YardRoof, YardWalls, YardShutters, YardFence, YardPath, YardLantern, YardMailbox, YardBench, YardFlowerbed,
	YardScarecrow, YardWell, YardVane, YardCenter /*fountain, fish statue, lighthouse vane*/,
	// ---- update 6: furniture of the house (builder, boutique prices)
	Cupboard, Bookcase, Clock, Fireplace, Chandelier, Statue, StoveSkin, BedSkin,
	Count };
namespace FVDecor
{
	constexpr int32 ClassicSlots = 7;          // Wallpaper..Curtains (the update-2 catalog)
	constexpr int32 FirstYardSlot = 7;         // YardRoof
	constexpr int32 FirstFurnitureSlot = 20;   // Cupboard
	FORCEINLINE bool IsYardSlot(EFVDecorSlot S) { return uint8(S) >= FirstYardSlot && uint8(S) < FirstFurnitureSlot; }
	FORCEINLINE bool IsFurnitureSlot(EFVDecorSlot S) { return uint8(S) >= FirstFurnitureSlot && S < EFVDecorSlot::Count; }
}

/** Kinds of interactive objects placed in the world by FVScene. */
enum class EFVObjKind : uint8
{
	Resource,     // pickup (Id = resource item id, Index = slot)
	Stall,        // trader stall (Id = trader id)
	OrderBoard,   // order board (pond)
	NetSpot,      // net spot at the shore (Index = spot)
	DigSpot,      // soft soil for digging worms (Index = spot)
	Door,         // yard: house door (enter); interior: exit door
	Station,      // house station (Id = station id, e.g. "st_kitchen")
	DecorCatalog, // interior: dresser with the decor catalog
	// ---- update 6 (built by SCENE-OBJECTS, dispatched by FVGameU6::Interact). Id = stable anchor id unless noted.
	Tree,         // choppable tree (Id "tree_07"); Index = EFVTreeKind
	JunkPile,     // City junk with scrap (Id "junk_03")
	Boulder,      // Cave boulder (Id "boulder_02")
	Npc,          // a villager (Id = npc id "npc_potapych")
	BuildPlot,    // empty yard plot with pegs (Id = yard building id "yb_sawmill")
	YardBuilding, // a built yard building (Id = "yb_sawmill" / "yb_forge" / "yb_garden")
	GardenBed,    // garden bed (Index = bed index)
	Boat,         // the boat at the ocean pier / the City pier
	Blockage,     // forest windfall (Id "blockage") — one-off, cleared with the axe
	CaveRubble,   // City rubble in front of the cave (Id "rubble_0..2") — one-off, excavation
	Exit,         // walk-through exit (Id = target location, Label = «В лес →»)
	MillObj,      // update 6 (§18): «Старая мельница» object (Id = FVMill::Obj*Id(): mill_bridge, mill, beaver_lodge,
	              // feed_deck (Index 0..2), fish_pen); dispatched by FVGameU6::Interact, label FVMill::ObjLabel
	Count
};

/** Item containers of the profile. Keepnet/IceCellar/Aquarium hold fish only; Backpack/Chest hold everything else
    (resources, materials, food, tackle, nets, junk). Bait is NOT in containers: it stays in UFVSaveGame::Baits. */
enum class EFVContainer : uint8 { Keepnet, Backpack, Chest, IceCellar, Aquarium, Count };

/** Mini-game played while crafting a recipe. */
enum class EFVCraftGame : uint8 { None, Cooking, Knots, Count };

enum class EFVOrderState : uint8 { Offered, Taken, Done, Failed, Count };

/** FFVInvStack::Flags bits. */
namespace FVStackFlags
{
	constexpr uint8 Smoked = 1 << 0;   // smoked fish: price x2 (FVDB::ItemPriceEx)
}

/** Update 6 (§10.5): the four expert characteristics. Multipliers, neutral = 1; combined by multiplication. */
struct FFVExpertStats
{
	float Force = 1.f;          // Сила: gravity, push, pull
	float ZoneSize = 1.f;       // Размер зоны
	float LineStrength = 1.f;   // Прочность лески: tension limit
	float Stability = 1.f;      // Устойчивость: resistance to current and waves

	void Combine(const FFVExpertStats& O) { Force *= O.Force; ZoneSize *= O.ZoneSize; LineStrength *= O.LineStrength; Stability *= O.Stability; }
	/** Strength P (food quality): 1 + (m - 1) x P. */
	FFVExpertStats Scaled(float P) const
	{
		FFVExpertStats R;
		R.Force = 1.f + (Force - 1.f) * P; R.ZoneSize = 1.f + (ZoneSize - 1.f) * P;
		R.LineStrength = 1.f + (LineStrength - 1.f) * P; R.Stability = 1.f + (Stability - 1.f) * P;
		return R;
	}
};

/** Additive / multiplicative fishing modifiers (tackle, food buffs). Neutral by default. */
struct FFVStatMods
{
	float BarBonus = 0.f;       // + catch-bar height (track units)
	float ReelMult = 1.f;       // x progress gain while the fish is inside the bar
	float DrainMult = 1.f;      // x progress loss while outside (lower = better)
	float BiteSpeed = 1.f;      // x waiting time (lower = faster bites)
	float CastRange = 0.f;      // + fraction of the full water depth
	float Luck = 0.f;           // + rare+ spawn weights
	float TreasureBonus = 0.f;  // + treasure chance
	float SizeBonus = 0.f;      // + size roll
	float ReactBonus = 0.f;     // + seconds of the bite reaction window
	float JunkMult = 1.f;       // x junk chance

	void Combine(const FFVStatMods& O)
	{
		BarBonus += O.BarBonus; ReelMult *= O.ReelMult; DrainMult *= O.DrainMult; BiteSpeed *= O.BiteSpeed;
		CastRange += O.CastRange; Luck += O.Luck; TreasureBonus += O.TreasureBonus; SizeBonus += O.SizeBonus;
		ReactBonus += O.ReactBonus; JunkMult *= O.JunkMult;
	}
	/** Strength P (food quality): additive fields x P, multipliers 1 + (m - 1) x P. */
	FFVStatMods Scaled(float P) const
	{
		FFVStatMods R;
		R.BarBonus = BarBonus * P; R.ReelMult = 1.f + (ReelMult - 1.f) * P; R.DrainMult = 1.f + (DrainMult - 1.f) * P;
		R.BiteSpeed = 1.f + (BiteSpeed - 1.f) * P; R.CastRange = CastRange * P; R.Luck = Luck * P;
		R.TreasureBonus = TreasureBonus * P; R.SizeBonus = SizeBonus * P; R.ReactBonus = ReactBonus * P;
		R.JunkMult = 1.f + (JunkMult - 1.f) * P;
		return R;
	}
};

/** Effective rod stats used by fishing: equipped rod + installed tackle (+ active food, see UFVGame::GetEffectiveRodStats). */
struct FFVRodStats
{
	float BarBonus = 0.f;       // bar height = 96 + FVDB::LevelBarBonus(level) + BarBonus
	float ReelMult = 1.f;
	float DrainMult = 1.f;
	float BiteSpeed = 1.f;
	float CastRange = 0.55f;
	float Luck = 0.f;
	float TreasureBonus = 0.f;
	float SizeBonus = 0.f;
	float ReactBonus = 0.f;
	float JunkMult = 1.f;

	static FFVRodStats FromRod(const FFVRodDef& R)
	{
		FFVRodStats S;
		S.BarBonus = R.BarBonus; S.ReelMult = R.ReelMult; S.DrainMult = R.DrainMult; S.BiteSpeed = R.BiteSpeed;
		S.CastRange = R.CastRange; S.Luck = R.Luck; S.TreasureBonus = R.TreasureBonus;
		return S;
	}
	void Apply(const FFVStatMods& M)
	{
		BarBonus += M.BarBonus; ReelMult *= M.ReelMult; DrainMult *= M.DrainMult; BiteSpeed *= M.BiteSpeed;
		CastRange = FMath::Clamp(CastRange + M.CastRange, 0.05f, 1.f); Luck += M.Luck; TreasureBonus += M.TreasureBonus;
		SizeBonus += M.SizeBonus; ReactBonus += M.ReactBonus; JunkMult *= M.JunkMult;
	}
};

/** An input of a recipe / station upgrade. ItemId may be FVDB::AnyFishToken() ("any_fish": the cheapest fish of the
    keepnet is used, smoked fish never) or, update 5, a rarity token "any_fish_rare" / "any_fish_epic" / "any_fish_legend"
    (a fish of exactly that rarity, see FVDB::FishTokenRarity). Otherwise any id: resource/material/item (backpack),
    junk (backpack), bait (Baits map). */
struct FFVIngredient
{
	FName ItemId;
	int32 Count = 1;
};

struct FFVItemDef
{
	FName Id;                       // "res_branch", "mat_board", "food_soup", "line_linen", "float_feather", "net_small"
	FString Name;                   // "Ветка"
	FString Description;
	EFVItemKind Kind = EFVItemKind::Resource;
	EFVItemArt Art = EFVItemArt::Branch;
	FColor Tint = FColor::White;    // recolor for generic art (lines, nets)
	uint8 Tier = 0;                 // tackle tier 1..5 (rod tier + 1 needed), net size 1..3
	int32 SellPrice = 1;            // base sell price (0 = cannot be sold)
	int32 BuyPrice = 0;             // price at traders (0 = not sold)
	uint8 TraderMask = 0;           // traders that sell it (FFVTraderDef::Mask bits)
	int32 MinLevel = 1;             // fishing level needed to buy / craft
	int32 MaxStack = 99;
	// Food
	FFVStatMods Buff;               // effect while the food is active (scaled by quality: see FVDB::FoodPower)
	float BuffMinutes = 0.f;        // game minutes
	// Line / Float
	FFVStatMods Tackle;             // effect while installed on the equipped rod
	// Net
	int32 NetCapacity = 0;          // fish the net holds before it must be emptied
	float NetDurability = 0.f;      // max durability
	float NetMinutesPerFish = 0.f;  // average game minutes per fish
	// ---- update 6
	bool bExpert = false;           // expert tackle (Line/Float/Reel): only on expert rods; classic tackle only on classic rods
	uint8 ExpertKind = 0;           // Line: EFVExpertLine; Float: EFVExpertFloat; Reel: EFVExpertReel (FVTypesU6.h)
	FFVExpertStats ExpertTackle;    // expert tackle effect (multipliers)
	FFVExpertStats ExpertBuff;      // expert food effect (only in the expert fight, §6.2)
	uint8 BuyerMask = 0;            // traders that BUY it at their FFVTraderDef::BuyListMult (Лесник: ветки, брёвна, доски)
};

struct FFVRecipeDef
{
	FName Id;                       // "rcp_board"
	EFVStation Station = EFVStation::Workshop;
	int32 StationLevel = 1;         // minimum station level
	int32 MinLevel = 1;             // fishing level
	TArray<FFVIngredient> Inputs;
	int32 CoinCost = 0;
	FName OutputId;                 // item id or bait id
	int32 OutputCount = 1;
	EFVCraftGame Game = EFVCraftGame::None;
	int32 GameDifficulty = 1;       // Knots: pattern length; Cooking: number of cuts (0 = no cutting phase)
	bool bRecycle = false;          // workshop "Переработка" (junk -> materials)
	float CookSpeed = 1.f;          // update 5: Cooking — x arrow speed (Rate) of every pan/pot stage (FCookingScreen::OnOpen)
	// ---- update 6: recipes of the yard buildings (Station is ignored when Yard != Count; StationLevel = building level)
	EFVYardBuilding Yard = EFVYardBuilding::Count;
	bool bForgeGame = false;        // forge mini-game (EFVScreen::Forging) instead of instant crafting
	bool bSawGame = false;          // saw mini-game (EFVScreen::Sawing)
	// ---- 0.6.2 (Р80–Р82): OutputId / an input may be a rod id (rod output -> OwnedRods; rod input = перековка);
	// the recipe opens only after this achievement (NAME_None = no lock)
	FName RequiredAchievement;
};

struct FFVStationLevel
{
	FString Name;                   // "Очаг", "Плита", "Печь"
	FString Description;            // what this level gives (Russian, 1-2 lines)
	int32 Price = 0;                // coins to build / upgrade TO this level
	TArray<FFVIngredient> Materials;
	int32 MinLevel = 1;             // fishing level
	int32 Capacity = 0;             // smokehouse slots 4/8, aquarium 1/4/10, ice cellar 10/25, trophies 6, chest 40 slots
};

struct FFVStationDef
{
	EFVStation Station = EFVStation::Kitchen;
	FName Id;                       // "st_kitchen"
	FString Name;                   // "Кухня"
	FString Description;
	TArray<FFVStationLevel> Levels; // [0] = level 1
	bool bStartsBuilt = false;      // bed, chest
};

struct FFVTraderDef
{
	FName Id;                       // "tr_mitrich", "tr_pier"
	FString Name;                   // "Дед Митрич"
	FString Role;                   // "хозяин лавки"
	FString ShopTitle;              // "Лавка рыбака"
	FName Location;                 // "pond" / "ocean"
	float FishSellMult = 1.f;       // x sell price of fish
	float ItemSellMult = 1.f;       // x sell price of everything else
	uint8 Mask = 1;                 // bit tested against *::TraderMask
	bool bSellsClothes = false;     // cosmetics tab
	bool bSellsUpgrades = false;    // keepnet / backpack upgrades
	// ---- update 6 (§4 «Проблема (код)»: one trader per NPC point, a buy list with its own markup)
	FName NpcId;                    // the villager standing at the stall / NPC point (FVDBU6::FindNpc)
	float BuyListMult = 1.f;        // x sell price for items whose FFVItemDef::BuyerMask has this trader's bit
	bool bBuysOnlyList = false;     // true: buys ONLY BuyerMask items (Лесник), nothing else
	bool bSellsSeeds = false;       // огородница
	bool bBoutique = false;         // бутик (elite cosmetics / decor)
	bool bSellsBuildings = false;   // прораб (yard buildings, yard decor, furniture — EFVScreen::Builder)
};

struct FFVDecorDef
{
	FName Id;                       // "decor_wall_flowers"
	FString Name;
	FString Description;
	EFVDecorSlot Slot = EFVDecorSlot::Wallpaper;
	uint8 Style = 0;                // art style inside the slot (UFVSprites::DecorIcon / DecorWorld)
	int32 Price = 0;                // 0 = starter or reward only
	int32 MinLevel = 1;
	bool bStarter = false;          // owned & equipped from the start
	FColor Primary = FColor::White;
	FColor Secondary = FColor::Black;
	uint8 TraderMask = 0;           // update 6: who sells it (0 = the house catalog / dresser, as in update 2)
};

struct FFVShovelDef
{
	FString Name;                   // "Лопата", "Железная лопата"...
	int32 Price = 0;                // at traders (0 = craft only)
	uint8 TraderMask = 1;
	int32 MinLevel = 1;
	float DigSeconds = 8.f;         // digging mini-game time
	int32 WorkshopLevel = 0;        // 0 = cannot be crafted
	TArray<FFVIngredient> Craft;    // workshop upgrade materials (+ Price/2 coins)
};

struct FFVBackpackLevel
{
	int32 Slots = 16;
	int32 Price = 0;                // cost to upgrade TO this level
};

/** An interactive object in the current scene (FVScene::GetWorldObjects). All positions are virtual px of the
    current canvas and are rebuilt when the canvas changes. */
struct FFVWorldObject
{
	EFVObjKind Kind = EFVObjKind::Resource;
	int32 Index = 0;                // slot / spot index within its kind at this location
	FName Id;                       // resource item id / trader id / station id
	FVector2D Feet = FVector2D::ZeroVector;   // the player walks to this point to use the object
	FFVRect Hit;                    // click / tap rect
	float Reach = 30.f;             // max distance between the player's feet and Feet to interact
	FString Label;                  // "Ветка", "Лавка Митрича", "Кухня (не построена)"
};

// ============================================================================================
// UPDATE 6 — the rest of the contract types (expert fishing, big worlds, City). See FVTypesU6.h.
// ============================================================================================
#include "FVTypesU6.h"
