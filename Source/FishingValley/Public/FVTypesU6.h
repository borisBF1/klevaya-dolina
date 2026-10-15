// UPDATE 6 «Лес, двор и Город» — shared contract types (Docs/UPDATE6_FOREST_CITY.md §17).
// Included at the END of FVTypes.h: do not include this file directly, include FVTypes.h.
// Frozen contract: packages may not rename / remove / reorder anything here. New values only by the architect.

#pragma once

// ============================================================================================
// Expert tackle kinds (FFVItemDef::ExpertKind)
// ============================================================================================
enum class EFVExpertLine : uint8 { None, Thin /*тонкая*/, Heavy /*тяжёлая*/, Count };
enum class EFVExpertFloat : uint8 { None, Surface /*поверхностный*/, Deep /*глубинный*/, Glow /*светящийся*/, Count };
enum class EFVExpertReel : uint8 { None, Light /*лёгкая*/, Heavy /*тяжёлая*/, Count };

// ============================================================================================
// Expert fish behaviour (§10.6–10.8). Data (FFVExpertMove), interpreted by FFVExpertFight (FVExpertFight.h).
// ============================================================================================
/** Movement patterns of expert fish (§10.6 table). Snake is the base pattern between tricks. */
enum class EFVExpertPattern : uint8 { Snake, Circle, Eight, Shuttle, Dash, Hide, Dive, Decoy, Skittish,
	Jump,      // legends: «прыжок-свеча» — leaves the field for a moment, lands at the marked point
	Count };

/** Field hazards («стихии», §10.7). At most two at once and one «invisibility» (Darkness / Weeds / Decoy). */
enum class EFVHazard : uint8 { Current, Whirlpool, Wave, Piles, Weeds, Darkness, Drip, Spring, Count };

/** One move of a fish: a trick of an ordinary expert fish or a step of a legend's score («партитура»). */
struct FFVExpertMove
{
	EFVExpertPattern Pattern = EFVExpertPattern::Snake;
	float Seconds = 3.f;            // duration (legends: ±10 % at runtime)
	float Speed = 1.f;              // x the fish's base speed
	float Amp = 1.f;                // amplitude (radius / throw length / dash distance), x the pattern default
	float Param = 0.f;              // pattern-specific (Dash: count; Circle: turns; Shuttle: pause s; Decoy: ghost s)
	EFVHazard Hazard = EFVHazard::Count;   // hazard raised during this move (Count = none)
	EFVIcon Signal = EFVIcon::Count;       // warning icon shown 0.4–1 s before the move (Count = pattern default)
	FString Name;                   // «Три рывка по кругу» (legend moves: shown in the collection once seen)
};

// ============================================================================================
// Local conditions of the City / Cave (§8.1): deterministic from day + hour, nothing saved
// ============================================================================================
struct FFVLocalWeather
{
	bool bValid = false;            // false on classic locations (everything below is neutral)
	EFVSeaState Sea = EFVSeaState::Calm;
	bool bTide = false;             // evening tide 18–22 (City)
	bool bSpring = false;           // Cave: the underwater spring is beating (cycles every game hour)
	bool bDark = false;             // Cave: always; City: night
};

// ============================================================================================
// Fish facts (§10.16, Р33): hidden conditions of expert fish and legends
// ============================================================================================
enum class EFVFactKind : uint8 { PlaceTime, Weather, RodTypes, Line, Float, Reel, Bait, OwnBait, Count };

struct FFVFishFact
{
	EFVFactKind Kind = EFVFactKind::PlaceTime;
	FString Row;                    // collection line once learnt: «Клюёт на: Маятник, Штурвал», «Леска: тяжёлая»
	FString Tell;                   // what a villager says: «...выходит только в шторм, и леску бери тяжёлую»
};

// ============================================================================================
// Expert rods, fish and legends (data: FVDBU6 in FVDatabaseU6.h, DATA-EXPERT)
// ============================================================================================
/** Control characteristics of an expert rod (§10.3). Units: the field is 240 x 240, seconds. Fields a type does not
    use are ignored. «Характер»: two rods of one type differ only by these numbers. */
struct FFVExpertRodDef
{
	FName RodId;                    // FFVRodDef::Id (the rod itself is in FVDB::Rods(), bExpert = true)
	EFVExpertRodType Type = EFVExpertRodType::Pendulum;
	float ZoneW = 200.f;            // Pendulum / Twin: band width; Compass / Magnet / Helm: circle diameter
	float ZoneH = 60.f;             // Pendulum / Twin: band height
	float Gravity = 260.f;          // Pendulum / Twin: acceleration, units/s²
	float MaxSpeed = 170.f;         // zone speed limit, units/s
	float Drag = 2.f;               // water resistance (1/s)
	float Leash = 2.5f;             // Pendulum: how fast the band follows the fish along X (1/s)
	float Bounce = 0.35f;           // wall bounce restitution
	float Overlap = 40.f;           // Twin: overlap of the two halves in the centre (units)
	float Push = 140.f;             // Compass: impulse per press; Helm: acceleration at full stick
	float ArrowPeriod = 0.25f;      // Compass: seconds per arrow step (8 directions)
	float Pull = 90.f;              // Magnet: pull speed towards the zone while held
	float Inertia = 0.4f;           // Compass / Helm: 0 = no inertia .. 1 = heavy
	float TensionReserve = 1.f;     // x tension limit (Магнит «Якорь» small, «Полюс» huge)
	float TurnTension = 0.f;        // Helm: tension added by sharp turns with the fish inside
};

/** An expert fish (§10.6) or an expert legend (§10.8). Place / time / local weather apply to all; the tackle rules
    (Line, Float, Reel, Bait) only to fish of a type and legends — universal fish ignore them (§10.4). */
struct FFVExpertFishDef
{
	FName FishId;                   // FFVFishDef::Id (bExpert = true)
	bool bUniversal = false;        // bites on ANY expert rod
	uint8 TypeMask = 0;             // bits 1 << EFVExpertRodType (legends: exactly one bit)
	uint8 TimeMask = FVTime::Any;
	uint8 SeaMask = 0;              // bits 1 << EFVSeaState; 0 = any sea state
	bool bNeedTide = false;
	bool bNeedDark = false;
	bool bNeedSpring = false;       // Cave: at the spring
	bool bNeedPiles = false;        // City: near the piles (the old dock spot of the pier)
	EFVExpertLine Line = EFVExpertLine::None;       // None = any
	EFVExpertFloat Float = EFVExpertFloat::None;
	EFVExpertReel Reel = EFVExpertReel::None;
	FName Bait;                     // required bait id (NAME_None = any expert bait / none)
	// behaviour
	EFVExpertPattern Base = EFVExpertPattern::Snake;
	TArray<FFVExpertMove> Tricks;   // 1–2 tricks of an ordinary fish (legends: empty, see FVExpertLegends::Score)
	float Speed = 60.f;             // base speed, units/s
	float Agility = 1.f;            // turn rate multiplier
	float Gain = 0.045f;            // progress per second inside the zone
	float Drain = 0.035f;           // progress per second outside
	float TensionRate = 0.35f;      // tension per second while a red (dashing) fish is held in the zone
	float TargetSeconds = 28.f;     // balance target: median fight length (BALANCE_EXPERT)
	// legends
	EFVExpertLegend Legend = EFVExpertLegend::None;
	FName OwnBait;                  // Р17: with this bait and all conditions the legend bites at 100 %
	float LegendChance = 0.02f;     // chance per bite without the own bait (conditions met)
	TArray<FFVFishFact> Facts;      // 3–5, in the order they are revealed (place/time -> type -> tackle)
};

/** Everything that decides what bites on an expert rod (FVDBU6::PickExpertCatch). */
struct FFVExpertCatchContext
{
	FName Location;
	uint8 PhaseMask = FVTime::Day;
	EFVWeather Weather = EFVWeather::Sunny;
	FFVLocalWeather Local;
	int32 Level = 1;
	FName RodId;
	EFVExpertRodType Type = EFVExpertRodType::Pendulum;
	EFVExpertLine Line = EFVExpertLine::None;
	EFVExpertFloat Float = EFVExpertFloat::None;
	EFVExpertReel Reel = EFVExpertReel::None;
	FName Bait;
	bool bNearPiles = false;
	float Depth = 0.5f;
	float ExtraLuck = 0.f;
};

/** A legend condition row for the pre-cast checklist (§10.9): only KNOWN facts are shown (Р33). */
struct FFVExpertCheckItem
{
	EFVFactKind Kind = EFVFactKind::PlaceTime;
	FString Label;                  // «Шторм», «Леска: тяжёлая» or «???» when unknown
	bool bKnown = false;
	bool bOk = false;
};

// ============================================================================================
// World content (forest, yard, garden, tools, villagers, boat, City)
// ============================================================================================
enum class EFVTreeKind : uint8 { Birch, Pine, Oak, Spruce /*вековая ель*/, Count };
/** Visual state of a tree anchor (FVScene draws UFVSprites::TreeSprite(Kind, State, Variant)). */
enum class EFVTreeState : uint8 { Full, Marked /*choppable: notch on the trunk*/, Stump, Sapling, Count };

/** Axe / pickaxe levels (FVDBU6::Axes(), Pickaxes()), modelled after FFVShovelDef. */
struct FFVToolDef
{
	FString Name;                   // «Топор», «Железный топор»...
	int32 Price = 0;                // coins (trader) / half price + materials (craft)
	uint8 TraderMask = 0;           // sold by (0 = craft only)
	int32 MinLevel = 1;
	int32 WorkshopLevel = 0;        // workshop level to craft (0 = not at the workshop)
	bool bForge = false;            // crafted at the forge instead
	TArray<FFVIngredient> Craft;
	EFVTreeKind MaxTree = EFVTreeKind::Pine;   // axes: hardest tree it can chop
	float HitsMult = 1.f;           // axes: x hits needed; pickaxes: x boulder shake
};

/** A seed of the garden (§6.2). */
struct FFVSeedDef
{
	FName SeedId;                   // "seed_carrot" (FFVItemDef, Kind Seed) or "res_reed" (reed is planted as is)
	FName CropId;                   // "veg_carrot" / "res_reed"
	float GrowMinutes = 20.f;       // real minutes of play (U6.PlayedMinutes), not game minutes; rain x RainMult speed
	int32 YieldMin = 1, YieldMax = 2;
	int32 SeedsMin = 0, SeedsMax = 0;   // seeds returned with the harvest
	float RainMult = 1.25f;
	int32 MinLevel = 7;
};

/** A villager (forest, City). Drawn by UFVSprites::NpcSprite, talked to via EFVObjKind::Npc. */
struct FFVNpcDef
{
	FName Id;                       // "npc_potapych"
	FString Name;                   // «Лесник Потапыч»
	FString Role;                   // «покупает брёвна и доски»
	FName Location;                 // "forest" / "city"
	FName TraderId;                 // opens EFVScreen::Shop with this trader (NAME_None = none)
	EFVScreen Screen = EFVScreen::None;   // or opens this screen (Builder, Artel, CityBoard...)
	FString Greeting;               // one line in the speech bubble
	FColor Skin = FColor(236, 188, 150), Hair = FColor(90, 60, 40), Cloth = FColor(120, 90, 60), Cloth2 = FColor(200, 190, 170);
};

/** A yard building (§6): levels like a station (level 1 = built). */
struct FFVYardBuildingDef
{
	EFVYardBuilding Building = EFVYardBuilding::Sawmill;
	FName Id;                       // "yb_sawmill" / "yb_garden" / "yb_forge"
	FString Name;
	FString Description;
	TArray<FFVStationLevel> Levels; // [0] = build; Capacity: garden = beds (6/9/12)
	bool bNeedsBlockageCleared = false;   // sawmill: only after the forest windfall is cleared
};

/** One stage of the boat repair (§7): 1 frame, 2 planks + nails, 3 tar. */
struct FFVBoatStage
{
	FString Name;                   // «Каркас», «Обшивка», «Смоление»
	TArray<FFVIngredient> Cost;
	int32 Coins = 0;
	int32 MinLevel = 10;
};
