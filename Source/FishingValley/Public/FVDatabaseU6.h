// UPDATE 6 «Лес, двор и Город» — static content & formulas (Docs/UPDATE6_FOREST_CITY.md §17 «Контракт»).
// Included at the end of FVDatabase.h. Implemented by several packages, one file each (owner in brackets):
//   FVDatabaseU6.cpp         [DATA-CORE]    table hooks (AppendClassicTables / AppendHomeTables), ids, forest location
//   FVDatabaseU6Items.cpp    [DATA-ITEMS]   items, recipes, station levels 4/2/3, traders, tools, seeds, yard, boat
//   FVDatabaseU6Content.cpp  [DATA-CONTENT] City/Cave locations, new ordinary fish, villagers, cosmetics, decor, achievements
//   FVDatabaseU6Expert.cpp   [DATA-EXPERT]  expert rods, fish, legends, facts, local weather, hazards, expert catch
// Every table is built once (lazily) and never changes: returned pointers stay valid. Every lookup may return nullptr.
// Ids used across packages are listed in the spec §17 («Идентификаторы»); never rename an id once shipped.

#pragma once

#include "FVTypes.h"

namespace FVDBU6
{
	// ============================================================================================
	// [DATA-CORE] FVDatabaseU6.cpp — hooks called by FVDatabase.cpp / FVDatabaseHome.cpp while they build
	// ============================================================================================
	/** Tables of FVDatabase.cpp, before indexing. Update-6 rows are APPENDED (never inserted / reordered). */
	struct FClassicTables
	{
		TArray<FFVLocationDef>* Locations = nullptr;
		TArray<FFVFishDef>* Fish = nullptr;
		TArray<FFVJunkDef>* Junk = nullptr;
		TArray<FFVRodDef>* Rods = nullptr;
		TArray<FFVBaitDef>* Baits = nullptr;
		TArray<FFVCosmeticDef>* Cosmetics = nullptr;
		TArray<FFVAchievementDef>* Achievements = nullptr;
	};
	/** Calls DATA-CONTENT (locations, fish, cosmetics, achievements), DATA-EXPERT (rods, expert fish) and DATA-ITEMS
	    (junk, baits) appenders in this order. */
	void AppendClassicTables(const FClassicTables& T);

	/** Tables of FVDatabaseHome.cpp, before indexing. Stations: append LEVELS to existing defs (never new stations). */
	struct FHomeTables
	{
		TArray<FFVItemDef>* Items = nullptr;
		TArray<FFVRecipeDef>* Recipes = nullptr;
		TArray<FFVStationDef>* Stations = nullptr;
		TArray<FFVTraderDef>* Traders = nullptr;
		TArray<FFVDecorDef>* Decor = nullptr;
	};
	/** Calls DATA-ITEMS (items, recipes, station levels, traders) and DATA-CONTENT (decor). */
	void AppendHomeTables(const FHomeTables& T);

	// ---- special ids
	FName ForestId();                                 // "forest" — big location without water (not in Locations())
	FName CityId();                                   // "city"   — expert zone (in Locations(), appended)
	FName CaveId();                                   // "cave"   — expert zone (in Locations(), appended)
	FName YardBuildingId(EFVYardBuilding B);          // "yb_sawmill" / "yb_garden" / "yb_forge"
	bool IsForestId(FName LocationId);
	bool IsExpertLocation(FName LocationId);          // city / cave
	/** The forest def (Theme Forest, bBigWorld). FVDB::FindLocation(ForestId()) returns it too (DATA-CORE). */
	const FFVLocationDef& ForestLocation();
	/** Level at which XP freezes until the first arrival in the City (Р1). */
	constexpr int32 FrozenLevel() { return FV::ClassicMaxLevel; }

	// ============================================================================================
	// [DATA-ITEMS] FVDatabaseU6Items.cpp
	// ============================================================================================
	void AppendItems(TArray<FFVItemDef>& Items);
	void AppendRecipes(TArray<FFVRecipeDef>& Recipes);
	void AppendStationLevels(TArray<FFVStationDef>& Stations);   // workshop 4, kitchen 4, radio 2, smokehouse 3...
	void AppendTraders(TArray<FFVTraderDef>& Traders);           // tr_forester, tr_builder, tr_city_tackle, tr_gardener, tr_boutique
	void AppendJunk(TArray<FFVJunkDef>& Junk);                   // junk_scrap (Style Scrap)
	void AppendBaits(TArray<FFVBaitDef>& Baits);                 // bait_trash, bait_deepworm, legend baits

	const TArray<FFVToolDef>& Axes();                 // [0] = level 1 (Боцман, 5 000)
	const TArray<FFVToolDef>& Pickaxes();             // [0] = level 1 (Мастерская 4 by blueprint), [1] = «Закалённая кирка»
	const TArray<FFVSeedDef>& Seeds();
	const FFVSeedDef* FindSeed(FName SeedId);
	const TArray<FFVYardBuildingDef>& YardBuildings();            // indexed by EFVYardBuilding
	const FFVYardBuildingDef& YardBuilding(EFVYardBuilding B);
	const FFVYardBuildingDef* FindYardBuilding(FName Id);
	/** Recipes with FFVRecipeDef::Yard == B, in display order (FVDB::RecipesFor(Station) never returns them). */
	TArray<const FFVRecipeDef*> RecipesForYard(EFVYardBuilding B);
	const TArray<FFVBoatStage>& BoatStages();                     // 3 stages (§7)
	/** Hits needed / loot / regrow of trees (§4). Loot: log x1, branches x1–3, pine: resin 0–1. */
	int32 TreeHits(EFVTreeKind Kind, int32 AxeLevel);             // 0 = this axe cannot chop it
	TArray<FFVIngredient> RollTreeLoot(EFVTreeKind Kind, int32 BonusBranches, FRandomStream& Rng);
	TArray<FFVIngredient> BlockageLoot();                         // 3 logs
	TArray<FFVIngredient> RollJunkPileLoot(FRandomStream& Rng);   // 1–2 scrap (City)
	float TreeRegrowMinutes();                                    // real minutes of play (20)
	float JunkPileRegrowMinutes();                                // 15
	float BoulderRegrowMinutes();                                 // 30
	/** Seed price / expected harvest check data for «ECONOMY» (§5.3). */
	float ExpectedSeedHarvestValue(const FFVSeedDef& Seed);

	// ============================================================================================
	// [DATA-CONTENT] FVDatabaseU6Content.cpp
	// ============================================================================================
	void AppendLocations(TArray<FFVLocationDef>& Locations);      // city, cave (bExpertZone, bBigWorld)
	void AppendFish(TArray<FFVFishDef>& Fish);                    // 6 classic (§3а) + 2 City + 2 Cave ordinary fish
	void AppendCosmetics(TArray<FFVCosmeticDef>& Cosmetics);      // forester + boutique + legendary rewards
	void AppendAchievements(TArray<FFVAchievementDef>& Achievements);   // new ones + Tier of ALL (existing too)
	void AppendDecor(TArray<FFVDecorDef>& Decor);                 // yard decor, furniture, boutique yard decor

	const TArray<FFVNpcDef>& Npcs();
	const FFVNpcDef* FindNpc(FName Id);
	TArray<const FFVNpcDef*> NpcsAt(FName LocationId);
	FString AchTierName(EFVAchTier T);                // «Обычные», «Трудные», «Экспертные», «Легендарные»
	FLinearColor AchTierColor(EFVAchTier T);
	/** Russian names of the new decor slots (FVDB::DecorSlotName forwards slots >= FVDecor::ClassicSlots here). */
	FString DecorSlotNameU6(EFVDecorSlot Slot);

	// ============================================================================================
	// [DATA-EXPERT] FVDatabaseU6Expert.cpp
	// ============================================================================================
	void AppendExpertRods(TArray<FFVRodDef>& Rods);               // 8 rods (bExpert)
	void AppendExpertFish(TArray<FFVFishDef>& Fish);              // 17 expert fish + 6 expert legends (bExpert)

	const TArray<FFVExpertRodDef>& ExpertRods();
	const FFVExpertRodDef* FindExpertRod(FName RodId);
	const TArray<FFVExpertFishDef>& ExpertFish();                 // fish AND legends
	const FFVExpertFishDef* FindExpertFish(FName FishId);
	const FFVFishDef* FindExpertLegend(EFVExpertLegend Legend);
	/** Facts of an expert fish / legend (empty for anything else). */
	const TArray<FFVFishFact>& Facts(FName FishId);

	FString RodTypeName(EFVExpertRodType T);          // «Маятник»...
	FString RodTypeHowTo(EFVExpertRodType T);         // one-line control hint (ПК and touch)
	// ---- 0.6.2 (Р80–Р84): where a rod comes from and its type label
	FString RodTypeLabel(const FFVRodDef& Rod);       // «Маятник»... for expert rods, «Обычная» for the classic ones
	bool IsRodSold(const FFVRodDef& Rod);             // some trader sells it (Price > 0, TraderMask != 0); may still be Locked
	const FFVRecipeDef* RodRecipe(FName RodId);       // the recipe that makes this rod (nullptr = not crafted)
	/** Short Russian source line: «Лавка снастей Города», «Лавка снастей Города — после достижения «Друг Города»»,
	    «Перековка «Бриза» в Закалочной кузне», «Ковка в Закалочной кузне», «+ достижение «Ихтиолог»». */
	FString RodSourceText(const FFVRodDef& Rod);
	/** The achievement that locks a rod (purchase lock or recipe lock); NAME_None = none. */
	FName RodRequiredAchievement(const FFVRodDef& Rod);
	FString SeaStateName(EFVSeaState S);              // «Штиль», «Ветер», «Шторм», «Туман»
	FString HazardName(EFVHazard H);
	FString ExpertLineName(EFVExpertLine L);
	FString ExpertFloatName(EFVExpertFloat F);
	FString ExpertReelName(EFVExpertReel R);
	EFVExpertLine LineKindOf(FName ItemId);           // from FFVItemDef::ExpertKind (None for anything else)
	EFVExpertFloat FloatKindOf(FName ItemId);
	EFVExpertReel ReelKindOf(FName ItemId);

	/** Local conditions (§8.1): deterministic from the location, day and clock (0..1440) + the global weather. */
	FFVLocalWeather LocalWeather(FName LocationId, int32 Day, float ClockMinutes, EFVWeather Global);
	/** Hazards of the field right now (<= 2, one invisibility; before level 13 at most one). Classic fish: empty. */
	TArray<EFVHazard> ExpertHazards(FName LocationId, const FFVLocalWeather& W, EFVDayPhase Phase, int32 Level, bool bNearPiles);
	/** Rod + expert tackle (item ids, NAME_None = empty) + optional food (already scaled by FVDB::FoodPower). */
	FFVExpertStats ComposeExpertStats(FName RodId, FName Line, FName Float, FName Reel, const FFVExpertStats* Food = nullptr);
	/** Conditions of an expert fish / legend (universal fish: place, time, weather only). */
	bool ExpertConditionsMet(const FFVExpertFishDef& Def, const FFVExpertCatchContext& Ctx);
	/** What bites on an expert rod at an expert location (legends with their own bait: 100 % when all conditions are met,
	    Р17; the bait is spent by the caller in any case, Р27). nullptr = nothing (never happens with universal fish). */
	const FFVFishDef* PickExpertCatch(const FFVExpertCatchContext& Ctx, FRandomStream& Rng);
	/** Pre-cast checklist of a legend bait (§10.9). IsKnown(FactIndex) tells which facts the player knows (Р33). */
	TArray<FFVExpertCheckItem> ExpertChecklist(const FFVExpertFishDef& Def, const FFVExpertCatchContext& Ctx,
		TFunctionRef<bool(int32 FactIndex)> IsKnown);
	/** A treasure of an expert fight (money / hard rock / tackle). bOutJournalPage = a page with a fact (~35 %). */
	FFVTreasure RollExpertTreasure(FName LocationId, int32 Level, FRandomStream& Rng, bool& bOutJournalPage);
	int32 ExpertCatchXp(const FFVFishDef& F, EFVQuality Q, bool bPerfect);
}

// ============================================================================================
// [MILL] «Старая мельница» (§18, Р53) — FVDatabaseU6Mill.cpp. Classic location "mill" (big world, classic fishing),
// 7 fish + the legend «Хозяин омута», rye / flour / bread baits / dishes, the miller Прокоп (tr_miller, mask 128).
// Rows are appended by FVDBU6::AppendClassicTables / AppendHomeTables (last). State: UFVSaveGame::U6.Mill.
// ============================================================================================
class UFVSaveGame;
namespace FVMill
{
	// ---- ids (stable, saved)
	FName MillId();                                   // "mill"
	bool IsMillId(FName LocationId);
	FName MillerTraderId();                           // "tr_miller"
	FName MillerNpcId();                              // "npc_prokop"
	FName LegendFishId();                             // "fish_mill_master"
	FName KolobokBaitId();                            // "bait_kolobok"
	FName BreadBaitId();                              // "bait_bread"   (хлебная прикормка)
	FName ReedBaitId();                               // "bait_reed_bundle"
	FName RyeSeedId();                                // "seed_rye"
	FName RyeId();                                    // "veg_rye"
	FName FlourId();                                  // "mat_flour"
	FName TeaId();                                    // "food_tea"
	// world objects (EFVObjKind::MillObj / EAnchor::MillObj ids)
	FName ObjBridgeId();                              // "mill_bridge"  (forest: the footbridge over the stream)
	FName ObjMillId();                                // "mill"         (repair stages -> EFVScreen::Mill "mill")
	FName ObjBeaverId();                              // "beaver_lodge"
	FName ObjDeckId();                                // "feed_deck"    (Index 0..FeedDecks-1)
	FName ObjPenId();                                 // "fish_pen"     (EFVScreen::Mill "pen")
	FName ZoneOmutId();                               // "omut"
	FName ZoneDeckId(int32 Deck);                     // "deck_0".."deck_2"

	constexpr uint8 MillerMask = 128;                 // FFVTraderDef::Mask bit of tr_miller (the last free bit)
	constexpr int32 MinLevel = 10;
	constexpr int32 BeaverFeedsNeeded = 5;            // dam finished -> the Omut is open
	constexpr int32 BeaverBranches = 20;              // res_branch per feeding
	constexpr float BeaverCooldownMinutes = 15.f;     // REAL minutes of play (U6.PlayedMinutes) between feedings
	constexpr int32 FeedDecks = 3;
	constexpr float DeckGameMinutes = 30.f;           // a baited deck works 30 game minutes
	constexpr float DeckBiteSpeed = 0.6f;             // x waiting time on a baited deck
	constexpr float DeckRareMult = 1.5f;              // x weight of Rare / Epic fish on a baited deck
	constexpr int32 PenSlots = 3;                     // species in the pen at once
	constexpr float PenGameMinutes = 3.f * 1440.f;    // 3 game days
	constexpr float PenWeightMult = 1.5f;
	constexpr float PenSizeBonus = 0.05f;             // + size fraction of a pen species caught at the mill
	constexpr int32 NetSpots = 4;                     // 0..2 the pond, 3 = the Omut (only when it is open)
	constexpr int32 OmutNetSpot = 3;
	constexpr int32 RyePerFlour = 2;                  // the mill screen: veg_rye x2 -> mat_flour x1
	constexpr int32 SawBonusBoards = 1;               // the water saw: +1 board per log at the yard sawmill (§6.1)
	constexpr float MillerSmokedMult = 1.4f;          // x the shop price of SMOKED fish (ItemPriceEx incl. smoked x2)

	// ---- appenders (FVDBU6::AppendClassicTables / AppendHomeTables call them LAST; no FVDB getters inside)
	void AppendLocations(TArray<FFVLocationDef>& Locations);   // "mill" (Theme Mill, bBigWorld, MinLevel 10, free)
	void AppendFish(TArray<FFVFishDef>& Fish);                 // 7 fish + fish_mill_master (EFVLegend::MillMaster)
	void AppendBaits(TArray<FFVBaitDef>& Baits);               // bait_bread, bait_kolobok, bait_reed_bundle
	void AppendCosmetics(TArray<FFVCosmeticDef>& Cosmetics);   // bobber_millstone «Жёрнов»
	void AppendAchievements(TArray<FFVAchievementDef>& Achievements);   // ach_legend_mill «Хозяин омута»
	/** seed_rye, veg_rye, mat_flour, food_bliny, food_rye_fish_pie + §18 «Магазины»: Митрич / Боцман stop selling
	    glue, sinker, float_feather, line_linen, line_braided, float_sliding, net_small, net_medium / float_glow, net_large. */
	void AppendItems(TArray<FFVItemDef>& Items);
	void AppendRecipes(TArray<FFVRecipeDef>& Recipes);         // rcp_bread_bait, rcp_bliny, rcp_rye_fish_pie, rcp_kolobok, rcp_reed_bundle
	void AppendTraders(TArray<FFVTraderDef>& Traders);         // tr_miller (buys smoked fish x1.4 and crafted lures)
	FFVSeedDef RyeSeed();                                      // appended to FVDBU6::Seeds() by DATA-ITEMS' table

	// ---- repairs (FFVBoatStage: Name, Cost, Coins, MinLevel)
	const FFVBoatStage& BridgeRepair();                        // «Мостик через ручей»: доски x12, верёвка x6, клей x2
	const TArray<FFVBoatStage>& MillStages();                  // «Колесо», «Жернова», «Крыша»

	// ---- fishing (called by FVDB::PickCatch for Ctx.Location == mill; the rest of the game never needs them)
	bool IsOmutOnly(const FFVFishDef& F);                      // стерлядь, Хозяин омута
	/** false = this fish cannot bite here right now (Omut-only fish outside the Omut). */
	bool IsFishAllowed(const FFVCatchContext& Ctx, const FFVFishDef& F);
	/** Weight multiplier: bait affinities of §18 (ёрш — прикормка, язь — блёсны, форель — мушки x3, налим — выползок
	    and rain, сазан — тесто / хлебная прикормка, белый амур — пучок тростника), deck (rare x1.5), pen (x1.5). */
	float CatchWeightMult(const FFVCatchContext& Ctx, const FFVFishDef& F);
	/** Baits that can be thrown onto a feeder deck, best first: bait_bread, bait_groundbait, bait_dough. */
	const TArray<FName>& DeckBaits();
	bool IsFogMorning(float ClockMinutes);                     // 06:00-10:00: herbal tea lasts twice as long at the mill

	// ---- trading
	/** tr_miller: SMOKED fish = FVDB::ItemPriceEx x MillerSmokedMult; everything else 0 (he does not buy it). */
	int32 MillerSellPrice(FName ItemId, EFVQuality Q, uint8 Flags);
	/** tr_miller buys crafted spoons / flies (FFVBaitDef::bLure) per piece; 0 = not a crafted lure. */
	int32 MillerLurePrice(FName BaitId);

	// ---- world objects (for SCENE-OBJECTS / MILL-ART / screens)
	/** Name plate of a MillObj anchor for the current state (e.g. «Мельница (ремонт 1/3)», «Бобровая хатка (2/5)»). */
	FString ObjLabel(const UFVSaveGame* P, FName ObjId, int32 Index, double NowAbs);
	/** Sprite state of a MillObj: bridge 0 broken / 1 fixed; mill = stage 0..3; beaver = feeds 0..5; deck 0 / 1 baited;
	    pen = active species 0..3. */
	int32 ObjState(const UFVSaveGame* P, FName ObjId, int32 Index, double NowAbs);
}
