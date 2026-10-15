// Player profile: persistent progression + all profile-mutating logic. Implemented in FVSaveGame.cpp.
// Saved with UGameplayStatics::SaveGameToSlot (slot "FishingValley", user 0).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "FVTypes.h"
#include "FVSaveGame.generated.h"

enum class EFVBuyResult : uint8
{
	Ok, AlreadyOwned, NotEnoughMoney, LevelTooLow, NotForSale, Invalid,
	// ---- update 2
	NotEnoughMaterials,   // missing ingredients / materials
	NoRoom,               // target container full
	StationRequired,      // station missing or its level too low
	MaxLevel,             // already at the maximum level
	// ---- 0.6.2
	Locked                // an achievement is required first (FFVRodDef / FFVRecipeDef::RequiredAchievement)
};

USTRUCT()
struct FFVInvStack
{
	GENERATED_BODY()
	UPROPERTY() FName ItemId;       // fish id, junk id or item id (FFVItemDef)
	UPROPERTY() uint8 Quality = 0;  // EFVQuality (junk/resources always Normal; food uses it for dish quality)
	UPROPERTY() int32 Count = 0;
	UPROPERTY() uint8 Flags = 0;    // FVStackFlags (Smoked) — part of the stack identity (id, quality, flags)
};

/** Tackle installed on one rod (NAME_None = the default простая леска / пробковый поплавок, no bonus). */
USTRUCT()
struct FFVRodLoadout
{
	GENERATED_BODY()
	UPROPERTY() FName Line;
	UPROPERTY() FName Float;
};

/** A net placed at a net spot. */
USTRUCT()
struct FFVNetState
{
	GENERATED_BODY()
	UPROPERTY() FName Location;              // fishing location id
	UPROPERTY() int32 Spot = 0;              // net spot index at that location
	UPROPERTY() FName NetItem;               // FFVItemDef id (Kind Net)
	UPROPERTY() float Durability = 0.f;      // 0..NetDurability
	UPROPERTY() bool bTorn = false;          // torn nets catch nothing until repaired
	UPROPERTY() double SimMinute = 0.0;      // absolute game minute simulated up to
	UPROPERTY() float Progress = 0.f;        // accumulated fraction of the next fish
	UPROPERTY() TArray<FFVInvStack> Catch;   // fish (and a little junk) waiting to be collected
};

/** One fish in the smokehouse. */
USTRUCT()
struct FFVSmokeSlot
{
	GENERATED_BODY()
	UPROPERTY() FName FishId;
	UPROPERTY() uint8 Quality = 0;
	UPROPERTY() float MinutesLeft = 360.f;   // smoking time left; decreases only while there is fuel
};

/** An order of the order board. */
USTRUCT()
struct FFVOrder
{
	GENERATED_BODY()
	UPROPERTY() FName Id;                    // "ord_<day>_<n>"
	UPROPERTY() FString Customer;            // "Бабушка Нюра"
	UPROPERTY() FName ItemId;                // requested fish / item id
	UPROPERTY() uint8 MinQuality = 0;        // EFVQuality (fish)
	UPROPERTY() bool bSmoked = false;        // requested fish must be smoked
	UPROPERTY() int32 Count = 1;
	UPROPERTY() int32 Deposit = 0;           // paid when taking the order, returned on delivery
	UPROPERTY() int32 Reward = 0;            // coins paid on delivery (plus the deposit back)
	UPROPERTY() FName RewardItem;            // optional item reward
	UPROPERTY() int32 RewardItemCount = 0;
	UPROPERTY() int32 DurationHours = 24;    // time to deliver once taken
	UPROPERTY() double Deadline = 0.0;       // absolute game minute (set when taken)
	UPROPERTY() uint8 State = 0;             // EFVOrderState
};

/** A mounted trophy. */
USTRUCT()
struct FFVTrophy
{
	GENERATED_BODY()
	UPROPERTY() FName FishId;
	UPROPERTY() float SizeCm = 0.f;
	UPROPERTY() uint8 Quality = 0;
};

/** Per-location daily state (gathered resource slots / dug spots). */
USTRUCT()
struct FFVDayMask
{
	GENERATED_BODY()
	UPROPERTY() int32 Day = 0;               // the mask is valid only for this day
	UPROPERTY() int32 Mask = 0;              // bit i = slot/spot i used
};

USTRUCT()
struct FFVFishRecord
{
	GENERATED_BODY()
	UPROPERTY() int32 Caught = 0;
	UPROPERTY() float MaxSize = 0.f;
	UPROPERTY() uint8 BestQuality = 0;
};

USTRUCT()
struct FFVStats
{
	GENERATED_BODY()
	UPROPERTY() int32 FishCaught = 0;
	UPROPERTY() int32 JunkCaught = 0;
	UPROPERTY() int32 Escaped = 0;
	UPROPERTY() int32 Perfect = 0;
	UPROPERTY() int32 Treasures = 0;
	UPROPERTY() int32 Legendary = 0;
	UPROPERTY() int32 Iridium = 0;
	UPROPERTY() int64 Earned = 0;   // coins earned by selling
	UPROPERTY() int64 Spent = 0;
	UPROPERTY() float PlaySeconds = 0.f;
	// ---- update 2
	UPROPERTY() int32 Gathered = 0;
	UPROPERTY() int32 Crafted = 0;
	UPROPERTY() int32 Cooked = 0;
	UPROPERTY() int32 Smoked = 0;
	UPROPERTY() int32 WormsDug = 0;
	UPROPERTY() int32 NetFish = 0;
	UPROPERTY() int32 OrdersDone = 0;
	UPROPERTY() int32 OrdersFailed = 0;
	UPROPERTY() int32 KnotsTied = 0;
};

// ============================================================================================
// UPDATE 6 «Лес, двор и Город» — persistent state (Docs/UPDATE6_FOREST_CITY.md §14). All of it lives in
// UFVSaveGame::U6 (one field: old saves load it as defaults). Ids are stable strings, never indices.
// ============================================================================================

/** A regrowing world node by stable anchor id: tree (forest), junk pile (City), boulder (Cave). */
USTRUCT()
struct FFVWorldNode
{
	GENERATED_BODY()
	UPROPERTY() FName Id;                    // "forest:tree_07", "city:junk_03", "cave:boulder_02"
	UPROPERTY() uint8 State = 0;             // trees: EFVTreeState; piles / boulders: 0 = ready, 1 = depleted
	UPROPERTY() double ReadyAt = 0.0;        // U6.PlayedMinutes when it is ready again (0 = ready)
};

/** A garden bed (index = bed number 0..11). */
USTRUCT()
struct FFVGardenBed
{
	GENERATED_BODY()
	UPROPERTY() FName Seed;                  // FFVSeedDef::SeedId (NAME_None = empty)
	UPROPERTY() double PlantedAt = 0.0;      // U6.PlayedMinutes
	UPROPERTY() float Growth = 0.f;          // 0..1 (rain speeds it up: accumulated, not recomputed)
};

/** An expert rod's tackle (NAME_None = empty slot). */
USTRUCT()
struct FFVExpertLoadout
{
	GENERATED_BODY()
	UPROPERTY() FName Line;
	UPROPERTY() FName Float;
	UPROPERTY() FName Reel;
};

/** A saved configuration «в одно касание» (§10.15). */
USTRUCT()
struct FFVSavedConfig
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() FName Rod;
	UPROPERTY() FName Line;
	UPROPERTY() FName Float;
	UPROPERTY() FName Reel;
	UPROPERTY() FName Bait;
};

/** A task of the City board (§8.2): like FFVOrder, plus a fact about a fish as a reward. */
USTRUCT()
struct FFVCityTask
{
	GENERATED_BODY()
	UPROPERTY() FName Id;                    // "ctask_<day>_<n>"
	UPROPERTY() FName Npc;                   // customer npc id ("npc_efim")
	UPROPERTY() FName ItemId;                // fish / dish / tackle id
	UPROPERTY() uint8 MinQuality = 0;        // EFVQuality (fish: Gold / Iridium)
	UPROPERTY() int32 Count = 1;
	UPROPERTY() int32 Deposit = 0;
	UPROPERTY() int32 Reward = 0;
	UPROPERTY() bool bFact = false;          // «сведение»: a fact about a (surprise) fish on delivery
	UPROPERTY() FName FactFish;              // chosen on delivery (NAME_None until then)
	UPROPERTY() int32 FactIndex = -1;
	UPROPERTY() int32 DurationHours = 24;
	UPROPERTY() double Deadline = 0.0;       // absolute game minute (set when taken)
	UPROPERTY() uint8 State = 0;             // EFVOrderState
};

/** §18 «Старая мельница»: a species released into the fish pen. */
USTRUCT()
struct FFVMillPenSlot
{
	GENERATED_BODY()
	UPROPERTY() FName Fish;                  // fish id
	UPROPERTY() double Until = 0.0;          // absolute game minute when the boost ends
};

/** §18 «Старая мельница» (Р53): bridge, mill stages, beavers / Omut, feeder decks, the fish pen. [MILL] */
USTRUCT()
struct FFVMillState
{
	GENERATED_BODY()
	UPROPERTY() bool bBridgeRepaired = false;        // forest footbridge (opens the mill with level 10)
	UPROPERTY() int32 Stage = 0;                     // mill repair 0..3 (FVMill::MillStages()); 3 = working
	UPROPERTY() int32 BeaverFeeds = 0;               // 0..FVMill::BeaverFeedsNeeded (5 = dam finished, Omut open)
	UPROPERTY() double BeaverLastFeed = -1000.0;     // U6.PlayedMinutes of the last feeding (real minutes)
	UPROPERTY() TArray<double> DeckUntil;            // per feeder deck: absolute game minute until it is baited
	UPROPERTY() TArray<FFVMillPenSlot> Pen;          // <= FVMill::PenSlots species
	UPROPERTY() double TeaExtended = 0.0;            // FoodExpires value already doubled by the fog morning
	UPROPERTY() int32 FlourMilled = 0;               // counter (flour produced)
	UPROPERTY() int32 PenReleased = 0;               // counter (fish released)
};

USTRUCT()
struct FFVSaveU6
{
	GENERATED_BODY()
	UPROPERTY() int32 U6Version = 0;         // 0 = a save from 0.5 (FixupU6 migrates: XP trim, ocean net spot)
	// ---- time without sleep (§4): real minutes while the world runs
	UPROPERTY() double PlayedMinutes = 0.0;
	// ---- §0.2 (Р44): sleeps in a row; +1 per sleep, reset when the clock reaches 18:00 while awake; >= 2 = no bed until then
	UPROPERTY() int32 SleepStreak = 0;
	// ---- tools
	UPROPERTY() int32 AxeLevel = 0;          // 0 = none, 1..FVDBU6::Axes().Num()
	UPROPERTY() int32 PickaxeLevel = 0;      // 0 = none, 1..FVDBU6::Pickaxes().Num()
	UPROPERTY() TArray<FName> Blueprints;    // learnt blueprint item ids ("bp_pickaxe")
	// ---- forest / City / Cave world
	UPROPERTY() bool bBlockageCleared = false;       // forest windfall (one-off)
	UPROPERTY() TArray<FName> RubbleCleared;         // cave rubble ids cleared in the City ("rubble_0".."rubble_2")
	UPROPERTY() TArray<FFVWorldNode> Nodes;          // trees / junk piles / boulders by stable id
	// ---- yard
	UPROPERTY() TMap<FName, int32> YardBuildings;    // "yb_sawmill" -> level (absent = not built)
	UPROPERTY() TArray<FFVGardenBed> Garden;         // bed index -> state (size = beds available)
	UPROPERTY() int32 BoatStage = 0;                 // 0 = broken .. 3 = repaired (FVDBU6::BoatStages())
	UPROPERTY() int32 SawQueue = 0;                  // logs waiting in the sawmill queue («Пилорама»)
	// ---- levels 11–15 (§9)
	UPROPERTY() bool bVisitedCity = false;           // first arrival done (level 11 given)
	UPROPERTY() int64 XpEarned = 0;                  // «опыт заработанный»: always grows (tournament category)
	UPROPERTY() int32 XpSeniority = 0;               // «стаж»: XP trimmed by the migration (profile only)
	// ---- expert fishing
	UPROPERTY() TMap<FName, FFVExpertLoadout> ExpertLoadouts;   // expert rod id -> tackle
	UPROPERTY() TArray<FFVSavedConfig> Configs;
	UPROPERTY() TArray<FName> KnownFacts;            // "<fishId>:<factIndex>"
	UPROPERTY() TArray<FName> ExpertMovesSeen;       // "<legendFishId>:<moveIndex>"
	UPROPERTY() TMap<FName, int32> ExpertBestMove;   // legend fish id -> furthest move reached (the «приём 3 из 5» card)
	UPROPERTY() uint8 ExpertTypesCaught = 0;         // bits 1 << EFVExpertRodType (achievement «Мастер всех снастей»)
	UPROPERTY() uint8 ExpertTypesTaught = 0;         // bits: the «ghost hand» tutorial of this type was shown
	UPROPERTY() bool bFlawlessLegend = false;        // an expert legend caught without leaving the zone
	UPROPERTY() bool bIridiumLegend = false;
	// ---- City
	UPROPERTY() TArray<FFVCityTask> CityTasks;
	UPROPERTY() int32 CityTasksDay = 0;
	UPROPERTY() int32 CityTasksDone = 0;
	UPROPERTY() int64 BoutiqueSpent = 0;
	// ---- artel (§8.3)
	UPROPERTY() bool bArtelHired = false;
	UPROPERTY() TArray<FFVInvStack> ArtelStock;      // nets given to the artel master (Kind Net items)
	UPROPERTY() TArray<FName> ArtelLocations;        // locations chosen by the player
	UPROPERTY() TArray<FFVNetState> ArtelNets;       // nets the artel placed (same struct as the player's nets)
	UPROPERTY() double ArtelLastCollect = -1000.0;   // PlayedMinutes of the last morning collection
	UPROPERTY() int32 ArtelLastDay = 0;              // game day of the last morning collection
	UPROPERTY() TArray<FString> ArtelReport;         // lines shown as a toast when the player comes back
	UPROPERTY() TArray<FName> TutorialAnnounced;     // tutorial entries already announced by «Новое в учебнике» (once per save)
	// ---- counters (achievements)
	UPROPERTY() int32 TreesChopped = 0;
	UPROPERTY() int32 Harvests = 0;
	UPROPERTY() int32 Forgings = 0;
	UPROPERTY() int32 Excavations = 0;
	// ---- §18 «Старая мельница» [MILL]
	UPROPERTY() FFVMillState Mill;
};

UCLASS()
class UFVSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** v2 = update 2 (house, backpack, stations...). The save is written as a delta against the CDO, so Version's
	    initializer MUST stay 0: a v1 save never contained the field (it equalled the old default) and loads as 0.
	    Save()/ResetToNewGame() stamp CurrentVersion; FixupAfterLoad migrates Version 0/1 -> 2. */
	static constexpr int32 CurrentVersion = 2;
	/** "FishingValley", or "FishingValley_AutoTest" while the autotest runs (never touches the player's save). */
	static FString GetSlotName();
	static void SetAutoTestSlot(bool bAutoTest);

	UPROPERTY() int32 Version = 0;
	UPROPERTY() int64 Money = 0;
	UPROPERTY() int32 Xp = 0;

	UPROPERTY() TArray<FFVInvStack> Keepnet;
	UPROPERTY() int32 KeepnetLevel = 0;

	UPROPERTY() TArray<FName> OwnedRods;
	UPROPERTY() FName EquippedRod;

	UPROPERTY() TMap<FName, int32> Baits;         // bait id -> count
	UPROPERTY() FName EquippedBait;               // NAME_None = fishing without bait

	UPROPERTY() TArray<FName> OwnedCosmetics;
	UPROPERTY() FName EquippedHat;                // NAME_None = nothing
	UPROPERTY() FName EquippedOutfit;
	UPROPERTY() FName EquippedBobber;
	UPROPERTY() FName EquippedPet;

	UPROPERTY() TArray<FName> UnlockedLocations;
	UPROPERTY() FName CurrentLocation;

	UPROPERTY() TMap<FName, FFVFishRecord> Records;   // fish id -> record
	UPROPERTY() TArray<FName> CompletedAchievements;
	UPROPERTY() FFVStats Stats;

	UPROPERTY() int32 Day = 1;
	UPROPERTY() float TimeMinutes = 360.f;        // 0..1440
	UPROPERTY() uint8 Weather = 0;                // EFVWeather

	UPROPERTY() float SfxVolume = 0.8f;
	UPROPERTY() float MusicVolume = 0.45f;
	UPROPERTY() bool bSeenHelp = false;

	// ============================================================================================ update 2 state
	UPROPERTY() TArray<FFVInvStack> Backpack;          // resources, materials, food, tackle, nets, junk
	UPROPERTY() int32 BackpackLevel = 0;               // FVDB::BackpackLevels() index
	UPROPERTY() TArray<FFVInvStack> Chest;             // house chest (FVDB::Station(Chest) capacity, slots)
	UPROPERTY() TArray<FFVInvStack> IceCellar;         // fish (capacity = units by ice cellar level)
	UPROPERTY() TArray<FFVInvStack> Aquarium;          // live fish (count 1 per stack; capacity by aquarium level)
	UPROPERTY() TArray<FFVTrophy> Trophies;            // trophy wall plaques (one per species)

	UPROPERTY() TMap<FName, int32> StationLevels;      // station id -> level (0/absent = not built)
	UPROPERTY() int32 ShovelLevel = 0;                 // 0 = no shovel, 1..FVDB::Shovels().Num()
	UPROPERTY() TMap<FName, FFVRodLoadout> RodLoadouts;// rod id -> installed line & float

	UPROPERTY() FName ActiveFood;                      // food item id of the active buff (NAME_None = none)
	UPROPERTY() uint8 ActiveFoodQuality = 0;           // EFVQuality of the eaten dish
	UPROPERTY() double FoodExpires = 0.0;              // absolute game minute

	UPROPERTY() TArray<FFVNetState> Nets;              // placed nets
	UPROPERTY() TArray<FFVSmokeSlot> Smokehouse;       // fish being smoked / smoked and waiting
	UPROPERTY() float SmokeFuel = 0.f;                 // fuel left, game minutes
	UPROPERTY() int32 WormFarmWorms = 0;               // produced, waiting to be collected
	UPROPERTY() int32 WormFarmCrawlers = 0;            // выползки waiting to be collected
	UPROPERTY() int32 WormFarmDay = 0;                 // last day the farm produced

	UPROPERTY() TArray<FFVOrder> Orders;               // offered + taken orders (Done/Failed are pruned daily)
	UPROPERTY() int32 OrdersDay = 0;                   // day the board was last refreshed

	UPROPERTY() TMap<FName, FFVDayMask> Gathered;      // location id -> resource slots taken today
	UPROPERTY() TMap<FName, FFVDayMask> Dug;           // location id -> dig spots used today

	UPROPERTY() TArray<FName> DiscoveredLures;         // lure bait ids crafted at least once
	UPROPERTY() TArray<FName> OwnedDecor;
	UPROPERTY() TArray<FName> EquippedDecor;           // at most one per EFVDecorSlot
	UPROPERTY() uint8 WeatherTomorrow = 0;             // EFVWeather, rolled at every day start (radio forecast)
	// ---- update 3
	UPROPERTY() TArray<FName> SeenTutorial;            // guide entry ids the player has opened
	UPROPERTY() uint8 LastInventoryTab = 0;            // 0 = keepnet, 1 = backpack: where the last received item went
	// ---- update 4 (online profile)
	UPROPERTY() FString PlayerName;                    // chosen fisherman name (empty = not chosen yet)
	UPROPERTY() FString PlayerStatus;                  // motto shown in the profile (<= 40 chars)
	UPROPERTY() int64 OnlineId = 0;                    // server player id (0 = not registered yet)
	UPROPERTY() int32 OnlineTag = 0;                   // "#1042" number under the name
	UPROPERTY() FString OnlineSecret;                  // 64 hex chars, HMAC key shared with the server
	UPROPERTY() int64 ProfileStartUnix = 0;            // when this fisherman started (unix seconds)
	UPROPERTY() TMap<FName, int32> CatchesByLocation;  // fish caught per location id (favourite location)
	UPROPERTY() FFVTrophy BestCatch;                   // most valuable catch ever (FishId None = none)
	UPROPERTY() int32 BestCatchPrice = 0;              // its sell price at catch time
	UPROPERTY() FFVTrophy PendingSeasonBest;           // biggest fish since the last successful sync (reported, then reset)
	UPROPERTY() TArray<int64> ClaimedMail;             // mail ids already applied (claims are applied exactly once)
	// ---- update 4b
	UPROPERTY() int32 OnboardingStep = 0;              // first-fishing tutorial: 0 = not started .. 100 = done (old saves: done)
	// ---- update 5 (legends)
	UPROPERTY() TArray<FName> LegendsMet;              // legend fish ids that have been on the hook at least once (old saves: empty)
	UPROPERTY() bool bDevTools = false;                // 0.5: «режим разработчика» (5 taps on the version in the main menu): legend trials
	// ---- update 6 (everything new lives here; see FFVSaveU6)
	UPROPERTY() FFVSaveU6 U6;

	// ------------------------------------------------------------------ lifecycle
	void ResetToNewGame();                         // fills starting state from FVDB
	void FixupAfterLoad();                         // repairs invalid ids / missing starter items

	// ------------------------------------------------------------------ level / money
	int32 GetLevel() const;
	float GetLevelProgress() const;                // 0..1 towards next level (1 at max level)
	int32 AddXp(int32 Amount);                     // returns number of levels gained
	bool CanAfford(int64 Price) const { return Money >= Price; }
	bool Spend(int64 Price);                       // false if not enough money
	void Earn(int64 Amount, bool bCountAsSales);   // bCountAsSales -> Stats.Earned

	// ------------------------------------------------------------------ keepnet
	int32 KeepnetCapacity() const;
	int32 KeepnetUsed() const;                     // sum of stack counts
	bool KeepnetFull() const { return KeepnetUsed() >= KeepnetCapacity(); }
	/** Update 2: FISH ONLY (junk goes to the backpack via AddTo(Backpack...)). False if no room / not a fish. */
	bool AddToKeepnet(FName ItemId, EFVQuality Q, int32 Count = 1);
	int64 SellStack(int32 StackIndex, int32 Count); // returns coins earned (0 if invalid)
	int64 SellAll();
	int64 KeepnetValue() const;
	EFVBuyResult UpgradeKeepnet();

	// ------------------------------------------------------------------ rods & bait
	bool OwnsRod(FName Id) const { return OwnedRods.Contains(Id); }
	EFVBuyResult BuyRod(FName Id);                 // also equips it on success
	/** 0.6.2: what BuyRod would answer (no side effects): NotForSale for forged rods, Locked before RequiredAchievement. */
	EFVBuyResult CanBuyRod(FName Id) const;
	/** 0.6.2 (Р83): a rod given up to a recipe (перековка): its tackle goes back to the backpack (full -> chest), the rod
	    leaves OwnedRods; if it was in the hands, the hands get the first rod left (the caller equips the new one). */
	void GiveUpRod(FName Id);
	bool EquipRod(FName Id);
	const FFVRodDef& GetRod() const;               // equipped rod (falls back to starter)
	EFVBuyResult BuyBait(FName Id, int32 Packs = 1);
	int32 BaitCount(FName Id) const;
	bool EquipBait(FName Id);                      // NAME_None to fish without bait
	const FFVBaitDef* GetBait() const;             // equipped bait with count > 0, else nullptr
	void ConsumeBait();                            // -1 equipped bait; auto-unequips at 0
	void AddBait(FName Id, int32 Count);

	// ------------------------------------------------------------------ cosmetics
	bool OwnsCosmetic(FName Id) const { return OwnedCosmetics.Contains(Id); }
	EFVBuyResult BuyCosmetic(FName Id);
	void GrantCosmetic(FName Id);                  // no cost
	bool EquipCosmetic(FName Id);                  // must be owned
	void UnequipSlot(EFVCosmeticSlot Slot);        // Outfit cannot be unequipped (no-op)
	FName GetEquipped(EFVCosmeticSlot Slot) const;
	FFVAppearance BuildAppearance() const;         // from equipped hat/outfit + rod colors
	EFVBobberStyle GetBobberStyle() const;
	EFVPetStyle GetPetStyle() const;

	// ------------------------------------------------------------------ locations
	/** Home ids (FVDB::IsHomeId) are always unlocked. */
	bool IsLocationUnlocked(FName Id) const;
	EFVBuyResult UnlockLocation(FName Id);         // checks level & money

	// ------------------------------------------------------------------ records & achievements
	struct FRecordResult { bool bNewSpecies = false; bool bNewRecord = false; };
	FRecordResult RecordCatch(const FFVFishDef& F, float SizeCm, EFVQuality Q, bool bPerfect);
	const FFVFishRecord* GetRecord(FName FishId) const { return Records.Find(FishId); }
	int32 SpeciesCaught() const;
	int32 SpeciesCaughtAt(FName LocationId) const;
	bool IsLocationComplete(FName LocationId) const;
	/** Evaluates all achievements; newly completed ones are recorded, rewards granted, ids returned. */
	TArray<FName> CheckAchievements();
	bool IsAchievementDone(FName Id) const { return CompletedAchievements.Contains(Id); }
	float AchievementProgress(const FFVAchievementDef& A) const;   // 0..1
	FString AchievementProgressText(const FFVAchievementDef& A) const; // "12 / 50"
	/** Stat counters not covered by RecordCatch (same as incrementing Stats.* directly). */
	void RecordJunk() { ++Stats.JunkCaught; }
	void RecordEscape() { ++Stats.Escaped; }
	void RecordTreasure() { ++Stats.Treasures; }   // feeds the TreasureTotal achievements
	/** Update 5: the legend has been on the hook at least once (LegendsMet) — or caught (a catch counts as a meeting). */
	bool HasMetLegend(FName FishId) const;
	/** Update 5: remembers a hooked legend in LegendsMet (no duplicates). The caller saves / NotifyProfileChanged. */
	void MarkLegendMet(FName FishId);

	/** Short Russian message for a purchase result ("Не хватает монет" ...). */
	static FString BuyResultText(EFVBuyResult R);

	// ============================================================================================
	// UPDATE 2 — implemented in FVSaveGameHome.cpp (Data module). All mutators return false / an error code
	// without changing anything when they fail. Callers (screens / UFVGame) call UFVGame::NotifyProfileChanged().
	// "Now" arguments are absolute game minutes (UFVGame::GetAbsMinutes()).
	// ============================================================================================

	// ---- containers (see EFVContainer). Keepnet/IceCellar/Aquarium: fish only; Backpack/Chest: junk & items.
	TArray<FFVInvStack>& GetContainer(EFVContainer C);
	const TArray<FFVInvStack>& GetContainer(EFVContainer C) const;
	/** Keepnet/IceCellar: units (sum of counts); Aquarium: fish; Backpack/Chest: SLOTS (stacks, MaxStack per stack). */
	int32 ContainerCapacity(EFVContainer C) const;
	int32 ContainerUsed(EFVContainer C) const;          // same unit as ContainerCapacity
	bool ContainerAccepts(EFVContainer C, FName ItemId) const;
	/** True if Count units of (Id, Q, Flags) fit into C (stacking into existing stacks first). */
	bool CanAddTo(EFVContainer C, FName Id, int32 Count, uint8 Q = 0, uint8 Flags = 0) const;
	/** Adds (Id, Q, Flags) x Count to C. All or nothing. */
	bool AddTo(EFVContainer C, FName Id, int32 Count, uint8 Q = 0, uint8 Flags = 0);
	/** Removes Count units from stack StackIndex of C. */
	bool RemoveFrom(EFVContainer C, int32 StackIndex, int32 Count);
	/** Moves Count units of a stack between containers (all or nothing; respects ContainerAccepts & capacity). */
	EFVBuyResult Transfer(EFVContainer From, int32 StackIndex, int32 Count, EFVContainer To);
	/** Moves as much as fits of every stack of From into To. Returns units moved. */
	int32 TransferAll(EFVContainer From, EFVContainer To);
	/** Units of an item in the backpack (all qualities/flags). */
	int32 CountItem(FName ItemId) const;
	/** Units of any id: backpack items/junk, Baits map for bait ids, keepnet non-smoked fish for AnyFishToken. */
	int32 CountIngredient(FName ItemId) const;
	bool HasIngredients(const TArray<FFVIngredient>& Inputs, int32 Times = 1) const;
	/** Removes ingredients (lowest quality first; AnyFishToken = cheapest non-smoked fish of the keepnet). */
	bool ConsumeIngredients(const TArray<FFVIngredient>& Inputs, int32 Times = 1);
	/** Gives anything: fish -> keepnet, bait -> Baits, junk/items -> backpack. False (nothing given) if no room. */
	bool GiveItem(FName Id, int32 Count, uint8 Q = 0, uint8 Flags = 0);
	/** Fish-only keepnet add with flags (AddToKeepnet keeps its v1 signature = Flags 0). */
	bool AddToKeepnetEx(FName FishId, EFVQuality Q, int32 Count, uint8 Flags);
	EFVBuyResult UpgradeBackpack();

	// ---- trading (TraderId = FFVTraderDef id; prices from FVDB::TraderSellPrice)
	int64 SellFrom(EFVContainer C, int32 StackIndex, int32 Count, FName TraderId);   // coins earned (0 = failed)
	int64 SellAllFish(FName TraderId);                                              // whole keepnet
	int64 ContainerValue(EFVContainer C, FName TraderId) const;
	/** Buys Count units of a generic item (FFVItemDef with BuyPrice > 0 and this trader's bit) into the backpack. */
	EFVBuyResult BuyItem(FName ItemId, int32 Count, FName TraderId);
	/** Buys the next shovel level from a trader. */
	EFVBuyResult BuyShovel(FName TraderId);
	/** Crafts the next shovel level at the workshop (materials + half price). */
	EFVBuyResult CraftShovel();
	float DigSeconds() const;                                                       // 0 without a shovel

	// ---- stations
	int32 GetStationLevel(EFVStation S) const;                // 0 = not built
	bool IsStationBuilt(EFVStation S) const { return GetStationLevel(S) > 0; }
	int32 StationCapacity(EFVStation S) const;                // FFVStationLevel::Capacity of the current level (0 if not built)
	EFVBuyResult CanUpgradeStation(EFVStation S) const;       // Ok / MaxLevel / LevelTooLow / NotEnoughMoney / NotEnoughMaterials
	EFVBuyResult UpgradeStation(EFVStation S);                // builds (0 -> 1) or upgrades; spends coins & materials

	// ---- crafting
	/** Ok / StationRequired / LevelTooLow / NotEnoughMaterials / NotEnoughMoney / NoRoom. */
	EFVBuyResult CanCraft(const FFVRecipeDef& R, int32 Times = 1) const;
	/** Consumes inputs, gives OutputCount x Times. Food gets Quality (dish quality from the cooking mini-game). */
	EFVBuyResult Craft(const FFVRecipeDef& R, int32 Times = 1, EFVQuality Quality = EFVQuality::Normal);
	/** Lure workshop: consumes LureInputs, gives FVDB::LurePackSize() baits. bOutNew = first time discovered. */
	EFVBuyResult CraftLure(uint8 Base, uint8 Color, uint8 Glitter, bool& bOutNew);
	bool IsLureDiscovered(FName LureBaitId) const { return DiscoveredLures.Contains(LureBaitId); }

	// ---- rod tackle (installed at the workshop)
	FFVRodLoadout GetLoadout(FName RodId) const;
	/** Ok / Invalid (not tackle / not owned rod) / LevelTooLow (tackle tier > rod tier + 1) / StationRequired (no workshop). */
	EFVBuyResult CanInstallTackle(FName RodId, FName ItemId) const;
	/** Takes the item from the backpack and installs it; the previously installed one goes back to the backpack. */
	EFVBuyResult InstallTackle(FName RodId, FName ItemId);
	/** Returns the installed line (Kind Line) or float (Kind Float) to the backpack. */
	EFVBuyResult RemoveTackle(FName RodId, EFVItemKind Slot);
	/** Equipped rod + its installed tackle (no food). */
	FFVRodStats GetRodStats() const;
	FFVStatMods GetTackleMods(FName RodId) const;

	// ---- food buffs
	/** Eats one dish from backpack stack StackIndex (replaces the active buff). */
	bool EatFood(int32 BackpackStackIndex, double Now);
	const FFVItemDef* GetActiveFood(double Now) const;       // nullptr when expired / none
	float FoodMinutesLeft(double Now) const;
	FFVStatMods GetFoodMods(double Now) const;               // neutral when none

	// ---- time-based systems (called by UFVGame::AdvanceClock)
	/** Nets + smokehouse + food expiry between two absolute minutes. Weather = current weather. */
	void SimulatePassive(double FromAbs, double ToAbs, EFVWeather Weather);
	/** Called by UFVGame at the midnight rollover (Day already incremented, Weather/WeatherTomorrow already rolled):
	    worm farm production, order board refresh. Returns Russian summary lines (e.g. "Червятник: +3 червя"). */
	TArray<FString> OnNewDay(double Now);
	/** Fails taken orders whose deadline passed. Returns their ids. */
	TArray<FName> ExpireOrders(double Now);

	// ---- nets
	FFVNetState* FindNet(FName LocationId, int32 Spot);
	const FFVNetState* FindNet(FName LocationId, int32 Spot) const;
	EFVBuyResult PlaceNet(FName LocationId, int32 Spot, FName NetItemId, double Now);   // from the backpack
	int32 CollectNet(FName LocationId, int32 Spot);           // catch -> keepnet/backpack as far as it fits; returns units moved
	EFVBuyResult RepairNet(FName LocationId, int32 Spot);     // верёвка x1 + волокно x2 -> +50% durability, untorn
	TArray<FFVIngredient> NetRepairCost() const;
	EFVBuyResult RemoveNet(FName LocationId, int32 Spot);     // only when durability >= 95% and the catch is empty
	/** Throws a damaged (torn / < 95%) net away for good: the catch is collected first as far as it fits (OutCollected),
	    whatever does not fit is lost (OutLost); the net item is NOT returned. Invalid when there is no net or it is intact. */
	EFVBuyResult DestroyNet(FName LocationId, int32 Spot, int32& OutCollected, int32& OutLost);

	// ---- smokehouse
	EFVBuyResult LoadSmokehouse(int32 KeepnetStackIndex);    // one fish from the keepnet (non-smoked)
	EFVBuyResult AddSmokeFuel(int32 Branches);                // ветка x N from the backpack -> +FVDB fuel minutes each
	float FuelMinutesPerBranch() const;                       // 60
	int32 CollectSmoked();                                    // finished fish -> keepnet (Smoked flag); returns units

	// ---- worm farm
	int32 CollectWorms();                                     // worms -> Baits (bait_worm / bait_nightcrawler); returns units

	// ---- aquarium & trophies
	EFVBuyResult PutInAquarium(int32 KeepnetStackIndex);      // one fish
	EFVBuyResult TakeFromAquarium(int32 AquariumIndex);       // back to the keepnet
	EFVBuyResult MountTrophy(int32 KeepnetStackIndex, float SizeCm);  // доска x1 + клей x1 + the fish; replaces a smaller one
	/** Р48: takes the plaque of this species off the wall; the fish (its quality) goes back to the keepnet, the board and
	    glue are not returned. NoRoom = «Садок полон» (nothing changes), Invalid = no such trophy. */
	EFVBuyResult UnmountTrophy(FName FishId);
	TArray<FFVIngredient> TrophyCost() const;

	// ---- gathering & digging
	/** Resource currently lying at a slot of a location today (NAME_None if empty or already taken). */
	FName ResourceAt(FName LocationId, int32 Slot, int32& OutCount) const;
	/** Picks it up into the backpack. False if nothing there or no room. */
	bool Gather(FName LocationId, int32 Slot, FName& OutItem, int32& OutCount);
	bool IsDugToday(FName LocationId, int32 Spot) const;
	void MarkDug(FName LocationId, int32 Spot);

	// ---- orders (board at the pond)
	void RefreshOrders();                                     // regenerates offers for the current Day if needed
	EFVBuyResult TakeOrder(FName OrderId, double Now);        // pays the deposit (max 3 taken)
	bool CanDeliver(const FFVOrder& O) const;
	EFVBuyResult DeliverOrder(FName OrderId);                 // consumes items, pays reward + deposit back
	int32 TakenOrders() const;

	// ---- house decor
	bool OwnsDecor(FName Id) const { return OwnedDecor.Contains(Id); }
	EFVBuyResult BuyDecor(FName Id);                          // also equips it
	void GrantDecor(FName Id);
	bool EquipDecor(FName Id);                                // replaces the item of the same slot
	void UnequipDecor(EFVDecorSlot Slot);                     // Wallpaper/Floor cannot be empty (no-op)
	FName GetEquippedDecor(EFVDecorSlot Slot) const;

	// ============================================================================================
	// UPDATE 6 «Лес, двор и Город» (Docs/UPDATE6_FOREST_CITY.md). State: U6 (FFVSaveU6). Same rules as update 2:
	// mutators change nothing when they fail; callers call UFVGame::NotifyProfileChanged(). "Played" = U6.PlayedMinutes.
	// ============================================================================================

	// ---- [DATA-CORE] FVSaveGameU6.cpp: lifecycle / migration, levels 11–15, played minutes, achievements;
	//      [DATA-WORLD] FVSaveGameU6World.cpp: tools, blueprints, world nodes, blockage / rubble, yard, garden, boat
	void ResetU6();                                           // called by ResetToNewGame
	void FixupU6();                                           // called by FixupAfterLoad (migration 0.5 -> U6, repairs)
	/** Р1: XP stops at XpForLevel(ClassicMaxLevel) until the first arrival in the City. */
	bool IsXpFrozen() const;
	/** XP from rewards (mail, tournaments): while frozen it is paid out as coins 1:1 (Р10). Returns levels gained;
	    OutCoins = coins paid instead of XP. Always adds to U6.XpEarned. */
	int32 AddRewardXp(int32 Amount, int64& OutCoins);
	/** First arrival in the City (§8): level becomes 11 (XP = threshold of 11), bVisitedCity. Returns levels gained. */
	int32 ArriveInCity();
	void AddPlayedMinutes(double Minutes);                    // real minutes while the world runs (sleep never moves it)
	double GetPlayedMinutes() const { return U6.PlayedMinutes; }
	// tools
	EFVBuyResult BuyAxe(FName TraderId);                      // level 1 from Боцман
	EFVBuyResult CraftAxe();                                  // next level at the workshop / forge
	EFVBuyResult CraftPickaxe();                              // needs the blueprint + Мастерская 4 (level 1) / forge (level 2)
	bool HasBlueprint(FName BlueprintId) const { return U6.Blueprints.Contains(BlueprintId); }
	void LearnBlueprint(FName BlueprintId);
	// world nodes (trees / junk piles / boulders) by stable id "<loc>:<anchorId>"
	const FFVWorldNode* FindNode(FName NodeId) const;
	/** Tree state now (regrowth stump -> sapling -> Marked by played time). Default = its state before any chop. */
	EFVTreeState GetTreeState(FName NodeId, EFVTreeState Default) const;
	bool IsNodeReady(FName NodeId) const;                     // junk piles / boulders
	/** Chops a Marked tree: loot to the backpack (all or nothing), stump, U6.TreesChopped. */
	EFVBuyResult ChopTree(FName NodeId, EFVTreeKind Kind, int32 BonusBranches, TArray<FFVIngredient>& OutLoot);
	EFVBuyResult ClearBlockage(TArray<FFVIngredient>& OutLoot);
	EFVBuyResult CollectJunkPile(FName NodeId, TArray<FFVIngredient>& OutLoot);
	/** Gives the excavation finds (all or nothing) and depletes the boulder. */
	EFVBuyResult ExcavateBoulder(FName NodeId, const TArray<FFVIngredient>& Finds);
	EFVBuyResult ClearRubble(FName RubbleId);                 // City rubble in front of the cave (level 12, pickaxe)
	bool IsCaveOpen() const;                                  // all rubble cleared
	// yard buildings & garden & boat
	int32 GetYardLevel(EFVYardBuilding B) const;              // 0 = not built
	EFVBuyResult CanBuildYard(EFVYardBuilding B) const;       // Ok / MaxLevel / LevelTooLow / NotEnoughMoney / NotEnoughMaterials / StationRequired
	EFVBuyResult BuildYard(EFVYardBuilding B);                // builds or upgrades (coins + materials)
	int32 GardenBeds() const;                                 // beds available (0 without the garden)
	EFVBuyResult PlantSeed(int32 Bed, FName SeedId);
	float GetBedGrowth(int32 Bed) const;                      // 0..1 (1 = ripe), -1 = empty
	EFVBuyResult HarvestBed(int32 Bed, TArray<FFVIngredient>& OutLoot);
	/** Garden growth by played minutes (called with AddPlayedMinutes; rain x FFVSeedDef::RainMult). */
	void TickGarden(double PlayedDelta, bool bRain);
	EFVBuyResult RepairBoatStage();                           // next stage of FVDBU6::BoatStages()
	bool IsBoatRepaired() const;
	/** Progress of the update-6 achievement kinds (EFVAchKind::LegendsClassicAll...). false = not an update-6 kind. */
	bool AchievementProgressU6(const FFVAchievementDef& A, float& Out01, FString& OutText) const;

	// ---- [DATA-EXPERT] FVSaveGameU6Expert.cpp: expert tackle, configurations, facts, legends
	void FixupExpert();                                       // called by FixupU6
	bool IsExpertRodEquipped() const;
	FFVExpertLoadout GetExpertLoadout(FName RodId) const;
	/** Ok / Invalid (not expert tackle / rod not owned or classic) / NotEnoughMaterials (not in the backpack). */
	EFVBuyResult InstallExpertTackle(FName RodId, FName ItemId);
	EFVBuyResult RemoveExpertTackle(FName RodId, EFVItemKind Slot);   // Line / Float / Reel back to the backpack
	/** Rod + tackle + active food (only expert buffs). */
	FFVExpertStats GetExpertStats(FName RodId, double Now) const;
	int32 SaveConfig(const FString& Name);                    // the equipped rod + its tackle + bait; returns index
	EFVBuyResult ApplyConfig(int32 Index);                    // equips rod, moves tackle from the backpack if needed
	void DeleteConfig(int32 Index);
	bool KnowsFact(FName FishId, int32 FactIndex) const;
	/** Learns a fact (no duplicates). Returns true if it was new. */
	bool LearnFact(FName FishId, int32 FactIndex);
	int32 KnownFactCount(FName FishId) const;
	bool IsFishStudied(FName FishId) const;                   // every fact known («Изучено»)
	int32 TotalFactsKnown() const;
	/** Next unknown fact, «place/time -> type -> tackle» order; PreferFish first when it has one. false = all known. */
	bool PickNextFact(FName PreferFish, FRandomStream& Rng, FName& OutFish, int32& OutFactIndex) const;
	void MarkExpertMoveSeen(FName LegendFishId, int32 MoveIndex);
	bool HasSeenExpertMove(FName LegendFishId, int32 MoveIndex) const;
	/** Records an expert catch (types mask, flawless / iridium legend flags). */
	void RecordExpertCatch(const FFVCatchResult& R);

	// ---- [CITY] FVSaveGameU6City.cpp: City task board and the artel
	void FixupCity();                                         // called by FixupU6
	void RefreshCityTasks();                                  // regenerates offers for the current Day if needed
	EFVBuyResult TakeCityTask(FName TaskId, double Now);      // deposit, max 3 taken
	bool CanDeliverCityTask(const FFVCityTask& T) const;
	/** Consumes the items, pays reward + deposit; bFact tasks pick and learn a fact (OutFish / OutFactIndex, else None). */
	EFVBuyResult DeliverCityTask(FName TaskId, FName& OutFish, int32& OutFactIndex);
	TArray<FName> ExpireCityTasks(double Now);
	int32 TakenCityTasks() const;
	EFVBuyResult HireArtel();
	EFVBuyResult GiveNetsToArtel(int32 BackpackStackIndex, int32 Count);
	EFVBuyResult TakeNetsFromArtel(int32 StockIndex, int32 Count);
	void SetArtelLocation(FName LocationId, bool bOn);
	/** Places nets from the stock on free net spots of the chosen locations (never where the player has a net). */
	int32 ArtelDeploy();
	/** Artel nets fish like the player's nets (called by SimulatePassive). */
	void SimulateArtel(double FromAbs, double ToAbs, EFVWeather Weather);
	/** Morning collection (>= 06:00, at most once per 12 played minutes, §8.3): sells the catch minus the commission.
	    Returns coins; OutReport = lines for the toast («Артельщик продал 23 рыбы: +3 450»). */
	int64 ArtelMorningCollect(double Now, TArray<FString>& OutReport);
	TArray<FFVIngredient> ArtelRepairCost() const;            // for all damaged artel nets
	EFVBuyResult ArtelRepairAll();
	int32 ArtelDiscardTorn();                                 // torn nets thrown away and replaced from the stock

	// ---- [MILL] FVSaveGameU6Mill.cpp: «Старая мельница» (§18). Now = absolute game minutes; played = U6.PlayedMinutes
	void FixupMill();                                         // called by FixupAfterLoad (after FixupU6)
	bool IsBridgeRepaired() const { return U6.Mill.bBridgeRepaired; }
	/** Ok / AlreadyOwned / LevelTooLow (level 10) / NotEnoughMaterials. Consumes FVMill::BridgeRepair().Cost. */
	EFVBuyResult RepairBridge();
	/** Level 10 + the bridge: UnlockLocation("mill") succeeds (free). */
	bool CanOpenMill() const;
	int32 GetMillStage() const;                               // 0..3
	bool IsMillWorking() const;                               // all 3 stages done
	/** Next stage of FVMill::MillStages(): Ok / MaxLevel / StationRequired (mill not open) / LevelTooLow / NotEnoughMoney /
	    NotEnoughMaterials. */
	EFVBuyResult CanRepairMillStage() const;
	EFVBuyResult RepairMillStage();
	/** The mill screen: veg_rye x (2 x Times) -> mat_flour x Times. StationRequired (not working) / NotEnoughMaterials / NoRoom. */
	EFVBuyResult GrindRye(int32 Times);
	int32 MaxGrindTimes() const;                              // how many flour the rye in the backpack makes
	/** The water saw (§6.1): boards added per sawn log while the mill works (0 / FVMill::SawBonusBoards). The sawing
	    code (Craft of a Sawmill recipe / the Sawing mini-game) adds this many boards per log. */
	int32 MillSawBonus() const;
	/** Beavers: res_branch x20, at most once per 15 real minutes. Ok / MaxLevel (dam done) / NotEnoughMaterials / Invalid
	    (cooldown: see BeaverCooldownLeft). The 5th feeding opens the Omut. */
	EFVBuyResult FeedBeavers();
	float BeaverCooldownLeft() const;                         // real minutes until the next feeding (0 = now)
	bool IsOmutOpen() const;
	/** Feeder deck: one portion of Bait (FVMill::DeckBaits) from Baits -> 30 game minutes. Invalid (bad deck / bait) /
	    AlreadyOwned (still baited) / NotEnoughMaterials (no such bait). */
	EFVBuyResult BaitFeedDeck(int32 Deck, FName BaitId, double Now);
	bool IsDeckBaited(int32 Deck, double Now) const;
	float DeckMinutesLeft(int32 Deck, double Now) const;      // game minutes
	/** The fish pen: one fish of aquarium stack AquariumIndex is released for good -> 3 game days x1.5 / bigger. The same
	    species refreshes its timer. Invalid / NoRoom (3 other species active). OutFish = the species. */
	EFVBuyResult ReleaseToPen(int32 AquariumIndex, double Now, FName& OutFish);
	TArray<FName> ActivePenFish(double Now) const;
	float PenMinutesLeft(FName FishId, double Now) const;     // game minutes (0 = not in the pen)
	/** Drops expired pen slots / deck timers (UFVGame: new day). */
	void PruneMill(double Now);
	/** Fog morning at the mill (06-10): the active herbal tea lasts twice as long (once per eaten tea). true = extended. */
	bool ApplyFogTea(double Now, float ClockMinutes);
	/** Sells crafted lures (bait ids with bLure) to the miller: Count from Baits. Returns coins (0 = nothing sold). */
	int64 SellLuresToMiller(FName BaitId, int32 Count);

	// ------------------------------------------------------------------ persistence
	static bool SlotExists();
	static UFVSaveGame* Load();                    // nullptr if missing/corrupt; calls FixupAfterLoad
	bool Save();
	static void DeleteSave();
};
