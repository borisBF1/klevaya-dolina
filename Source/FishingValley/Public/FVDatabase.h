// Static game content & balance formulas. Implemented in FVDatabase.cpp.
// Call FVDB::Init() once at startup (safe to call repeatedly). All returned pointers stay valid for the program lifetime.

#pragma once

#include "FVTypes.h"

namespace FVDB
{
	void Init();

	// ---- tables ----
	const TArray<FFVLocationDef>& Locations();        // progression order; [0] = starting location
	const TArray<FFVFishDef>& Fish();                 // grouped by location, then by rarity
	const TArray<FFVJunkDef>& Junk();
	const TArray<FFVRodDef>& Rods();                  // by tier; [0] = free starter rod
	const TArray<FFVBaitDef>& Baits();
	const TArray<FFVCosmeticDef>& Cosmetics();
	const TArray<FFVAchievementDef>& Achievements();
	const TArray<FFVKeepnetLevel>& KeepnetLevels();   // [0] = starting keepnet (Price 0)

	const FFVLocationDef* FindLocation(FName Id);
	const FFVFishDef* FindFish(FName Id);
	const FFVJunkDef* FindJunk(FName Id);
	const FFVRodDef* FindRod(FName Id);
	const FFVBaitDef* FindBait(FName Id);
	const FFVCosmeticDef* FindCosmetic(FName Id);
	const FFVAchievementDef* FindAchievement(FName Id);

	TArray<const FFVFishDef*> FishAt(FName LocationId);
	TArray<const FFVCosmeticDef*> CosmeticsFor(EFVCosmeticSlot Slot);

	// ---- lookup helpers ----
	int32 LocationIndex(FName LocationId);            // index in Locations() (progression order); INDEX_NONE if unknown
	bool IsStarterCosmetic(FName CosmeticId);         // owned & equipped from the start (Price 0)
	bool IsTreasureCosmetic(FName CosmeticId);        // reward-only item that can drop from treasure chests (Price 0)
	FString CosmeticSourceText(FName CosmeticId);     // Russian: "В лавке: 300 монет", "Награда за достижение «…»", "Можно найти в сундуке…"

	// ---- new game ----
	int64 StartMoney();
	FName StartLocation();
	FName StartRod();
	TArray<FName> StartCosmetics();                   // owned & equipped on a new game (hat/outfit/bobber; no pet)

	// ---- progression ----
	int32 XpForLevel(int32 Level);                    // total XP needed to REACH Level; XpForLevel(1) == 0; clamps at FV::MaxLevel
	int32 LevelForXp(int32 Xp);                       // 1..FV::MaxLevel
	float LevelBarBonus(int32 Level);                 // catch-bar bonus from level (Stardew: +8 per level above 1)
	TArray<FString> UnlocksAtLevel(int32 Level);      // Russian lines describing what becomes available at exactly this level

	// ---- fishing balance ----
	/** Chooses what bites. Sets exactly one of OutFish / OutJunk.
	    Update 5: OutGated (optional) receives the first legend a hard LegendGate rule kept away (nullptr if none). */
	void PickCatch(const FFVCatchContext& Ctx, FRandomStream& Rng, const FFVFishDef*& OutFish, const FFVJunkDef*& OutJunk,
		const FFVFishDef** OutGated = nullptr);
	/** True if the fish can bite right now (time of day / weather / level) — ignores depth. */
	bool IsFishAvailable(const FFVFishDef& F, uint8 PhaseMask, EFVWeather Weather, int32 Level);
	/** Base waiting time before a bite, seconds (before rod/bait multipliers). */
	float RollBiteDelay(FRandomStream& Rng);
	/** Size fraction 0..1 (maps MinSize..MaxSize). CatchSeconds = how long the minigame took. */
	float RollSizeFrac(int32 Level, bool bPerfect, float CatchSeconds, const FFVBaitDef* Bait, FRandomStream& Rng);
	EFVQuality RollQuality(float SizeFrac, bool bPerfect, FRandomStream& Rng);
	int32 CatchXp(const FFVFishDef& F, EFVQuality Q, bool bPerfect);
	int32 JunkXp();
	FFVTreasure RollTreasure(const FFVLocationDef& Loc, int32 Level, FRandomStream& Rng);

	// ---- economy ----
	float QualityPriceMult(EFVQuality Q);             // 1.0 / 1.25 / 1.5 / 2.0
	int32 FishPrice(const FFVFishDef& F, EFVQuality Q);
	int32 ItemPrice(FName ItemId, EFVQuality Q);      // fish or junk; 0 if unknown

	// ---- presentation (Russian) ----
	FString RarityName(EFVRarity R);
	FLinearColor RarityColor(EFVRarity R);
	FString QualityName(EFVQuality Q);
	FLinearColor QualityColor(EFVQuality Q);
	FString MotionName(EFVMotion M);
	FString TimeMaskText(uint8 Mask);                 // "Утро, День" / "Любое время"
	FString WeatherReqText(EFVWeatherReq W);          // "Любая погода" / "Только в дождь" ...
	FString WeatherName(EFVWeather W);
	FString PhaseName(EFVDayPhase P);
	FString SlotName(EFVCosmeticSlot S);
	FString ItemName(FName ItemId);                   // any fish/junk/rod/bait/cosmetic/location id (+ update 2: item/decor/station/trader)
	FString DifficultyText(int32 Difficulty);         // "Легко" .. "Легендарно"

	// ============================================================================================
	// UPDATE 2 (implemented in FVDatabaseHome.cpp). See Docs/UPDATE_HOME.md for all ids and numbers.
	// ============================================================================================

	// ---- special ids
	FName HomeId();                                   // "home"  — the yard (house exterior + garden); on the world map
	FName HouseId();                                  // "house" — the interior of the house
	bool IsHomeId(FName LocationId);                  // home or house (not fishing locations, always unlocked)
	/** FindLocation() also returns these two defs (Theme Home / HomeInterior); they are NOT in Locations(). */
	const FFVLocationDef& HomeLocation();
	const FFVLocationDef& HouseLocation();
	FName AnyFishToken();                             // "any_fish" (recipe input: the cheapest non-smoked fish of the keepnet)

	// ---- generic items (resources, materials, food, lines, floats, nets)
	const TArray<FFVItemDef>& Items();
	const FFVItemDef* FindItem(FName Id);
	TArray<const FFVItemDef*> ItemsOfKind(EFVItemKind Kind);
	FString ItemKindName(EFVItemKind Kind);           // "Ресурс", "Материал", "Еда", "Леска", "Поплавок", "Сеть"

	// ---- id classification (any table)
	bool IsFishId(FName Id);
	bool IsJunkId(FName Id);
	bool IsBaitId(FName Id);
	bool IsItemId(FName Id);                          // FFVItemDef
	/** Where a stack of this id lives: Keepnet for fish, Backpack for junk & items; Count for bait/unknown. */
	EFVContainer HomeContainerFor(FName Id);

	// ---- prices
	/** Base sell price of one unit (fish with quality, junk, item; x2 when Flags has FVStackFlags::Smoked). 0 = unsellable. */
	int32 ItemPriceEx(FName ItemId, EFVQuality Q, uint8 Flags);
	/** Price the given trader pays for one unit (ItemPriceEx x FishSellMult / ItemSellMult, rounded, >= 1 if sellable). */
	int32 TraderSellPrice(FName TraderId, FName ItemId, EFVQuality Q, uint8 Flags);
	/** "Копчёный карась" for smoked stacks, otherwise ItemName. */
	FString StackName(FName ItemId, uint8 Flags);

	// ---- food
	/** Strength of a dish by its quality: Normal 0.8, Silver 1.0, Gold 1.2, Iridium 1.4 (effect and duration). */
	float FoodPower(EFVQuality Q);
	/** Russian lines describing modifiers: "Полоса поимки +8", "Подтягивание +8%", "Клёв быстрее на 15%"... */
	TArray<FString> StatModsLines(const FFVStatMods& M);

	// ---- recipes
	const TArray<FFVRecipeDef>& Recipes();
	const FFVRecipeDef* FindRecipe(FName Id);
	TArray<const FFVRecipeDef*> RecipesFor(EFVStation Station);    // in display order

	// ---- stations
	const TArray<FFVStationDef>& Stations();          // indexed by EFVStation
	const FFVStationDef& Station(EFVStation S);
	const FFVStationDef* FindStation(FName Id);        // "st_kitchen"...
	FName StationId(EFVStation S);
	int32 StationMaxLevel(EFVStation S);

	// ---- traders
	const TArray<FFVTraderDef>& Traders();
	const FFVTraderDef* FindTrader(FName Id);
	const FFVTraderDef* TraderAt(FName LocationId);   // nullptr if none
	FName DefaultTraderId();                          // Mitrich (used when a shop opens without context)

	// ---- house decor
	const TArray<FFVDecorDef>& Decor();
	const FFVDecorDef* FindDecor(FName Id);
	TArray<const FFVDecorDef*> DecorFor(EFVDecorSlot Slot);
	TArray<FName> StarterDecor();
	FString DecorSlotName(EFVDecorSlot Slot);         // "Обои", "Пол", "Ковёр", "Светильник", "Растение", "Картина", "Шторы"
	FString DecorSourceText(FName DecorId);           // "В каталоге: 400 монет" / "Награда за достижение «…»"

	// ---- tools & upgrades
	const TArray<FFVShovelDef>& Shovels();            // [0] = level 1
	const TArray<FFVBackpackLevel>& BackpackLevels(); // [0] = starting backpack (Price 0)

	// ---- lures (lure workshop). 2 bases x 4 colors x 3 glitters = 24 bait defs "lure_<b>_<c>_<g>" in Baits().
	constexpr int32 LureBases = 2, LureColors = 4, LureGlitters = 3;
	FName LureId(uint8 Base, uint8 Color, uint8 Glitter);
	FString LurePartName(int32 Axis, uint8 Value);    // Axis 0 base ("Блесна"/"Мушка"), 1 color, 2 glitter
	TArray<FFVIngredient> LureInputs(uint8 Base, uint8 Color, uint8 Glitter);
	int32 LurePackSize();                             // baits produced per assembly (5)
	/** Weight multiplier the lure gives this fish in PickCatch (1 = no effect). */
	float LureAffinity(const FFVBaitDef& Bait, const FFVFishDef& Fish);
	/** Fish most attracted by the lure (highest affinity, then rarity), at most Max. */
	TArray<const FFVFishDef*> LureFavorites(const FFVBaitDef& Bait, int32 Max);

	// ---- gathering & digging (deterministic per day)
	/** Resource item at a location's slot on that day (NAME_None = empty slot). OutCount 1..2. */
	FName RollResource(FName LocationId, int32 Day, int32 Slot, int32& OutCount);
	/** Loot of one dig "hit" in the digging mini-game: worms, sometimes выползок / глина / камешек / junk. */
	FName RollDigLoot(FName LocationId, int32 ShovelLevel, FRandomStream& Rng, int32& OutCount);

	// ---- nets
	/** Net wear caused by one caught fish (quality & rarity make it larger). */
	float NetWear(const FFVFishDef& F, EFVQuality Q);

	// ---- misc
	FString StationLevelName(EFVStation S, int32 Level);   // "Плита" (Level 1-based; 0 -> "Не построено")
	/** Russian lines for stations / recipes / tackle unlocked at exactly this fishing level (level-up modal). */
	TArray<FString> HomeUnlocksAtLevel(int32 Level);

	// ============================================================================================
	// UPDATE 5 «Легенды долины» (Docs/UPDATE5_LEGENDS.md)
	// ============================================================================================

	// ---- legends (FVDatabase.cpp)
	/** Is the tackle of Ctx (Bait, Line, Float, LineTier) enough for this legend (FFVFishDef::Prep)? Ok for ordinary fish. */
	FFVLegendGateResult LegendGate(const FFVFishDef& Def, const FFVCatchContext& Ctx);
	/** The live checklist: one row per FFVLegendPrep::PrepItems entry (empty when nothing is required). Outside fishing
	    use UFVGame::MakeCatchContext(0) — the equipped rod with its tackle and the current bait. */
	TArray<FFVLegendCheckItem> LegendChecklist(const FFVFishDef& Def, const FFVCatchContext& Ctx);
	/** The fish def of a legend (nullptr for None / not assigned). */
	const FFVFishDef* FindLegend(EFVLegend Legend);

	// ---- rod stats & the catch bar (FVDatabase.cpp)
	/** Pure: rod + line + float (item ids, NAME_None = default) + optional food buff (already scaled by FoodPower). */
	FFVRodStats ComposeRodStats(const FFVRodDef& Rod, FName Line, FName Float, const FFVStatMods* Food = nullptr);
	/** Catch bar height in track units: 96 + LevelBarBonus(Level) + Stats.BarBonus (the formula of FVFishing's Hook). */
	float CatchBarHeight(int32 Level, const FFVRodStats& Stats);

	// ---- cooking by rarity (§10.2; FVDatabaseHome.cpp)
	FName AnyFishRareToken();                         // "any_fish_rare"   — a fish of exactly Rare rarity
	FName AnyFishEpicToken();                         // "any_fish_epic"   — exactly Epic
	FName AnyFishLegendToken();                       // "any_fish_legend" — exactly Legendary
	/** True for the three rarity tokens above (OutRarity = their rarity); false for anything else, "any_fish" included. */
	bool FishTokenRarity(FName Id, EFVRarity& OutRarity);
	/** "any_fish" or one of the rarity tokens. */
	bool IsAnyFishToken(FName Id);
}

// UPDATE 6: FVDBU6 (FVDatabaseU6.h) — new content and formulas.
#include "FVDatabaseU6.h"
