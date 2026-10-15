// UPDATE 6 [DATA-WORLD] — profile logic of the new world (FVSaveGame.h «UPDATE 6 [DATA-CORE]», world part): tools
// (axes, pickaxes, blueprints), world nodes by stable id (trees, junk piles, boulders, blockage, rubble), yard buildings,
// garden, boat (§4, §6, §7, §9а). Owner: DATA-WORLD. Every mutator changes nothing when it fails.

#include "FVSaveGame.h"
#include "FVDatabase.h"
#include "Misc/Crc.h"

namespace FVSGU6World
{
	constexpr int32 RubbleTotal = 3;        // rubble_0..rubble_2 (§17.3)
	constexpr int32 RubbleMinLevel = 12;    // §9а (Р3)
	const FName BlueprintPickaxe(TEXT("bp_pickaxe"));

	/** Deterministic stream from a node id + played time (never GetTypeHash(FName)). */
	FRandomStream MakeRng(const FString& Key, double Played, uint32 Salt)
	{
		uint32 H = FCrc::StrCrc32(*Key) ^ Salt;
		H ^= uint32(int64(Played * 60.0)) * 2654435761u;
		H ^= H >> 16; H *= 0x85EBCA6Bu; H ^= H >> 13; H *= 0xC2B2AE35u; H ^= H >> 16;
		return FRandomStream(int32(H));
	}

	/** Gives every ingredient (all or nothing): on failure the containers are restored. */
	bool GiveAll(UFVSaveGame& P, const TArray<FFVIngredient>& Loot)
	{
		const TArray<FFVInvStack> Pack = P.Backpack;
		const TArray<FFVInvStack> Net = P.Keepnet;
		const TMap<FName, int32> Baits = P.Baits;
		for (const FFVIngredient& I : Loot)
		{
			if (I.ItemId.IsNone() || I.Count <= 0) { continue; }
			if (!P.GiveItem(I.ItemId, I.Count))
			{
				P.Backpack = Pack; P.Keepnet = Net; P.Baits = Baits;
				return false;
			}
		}
		return true;
	}

	FFVWorldNode& NodeFor(TArray<FFVWorldNode>& Nodes, FName Id)
	{
		FFVWorldNode* N = Nodes.FindByPredicate([Id](const FFVWorldNode& X) { return X.Id == Id; });
		if (N) { return *N; }
		FFVWorldNode& New = Nodes.AddDefaulted_GetRef();
		New.Id = Id;
		return New;
	}

	/** Shared tool crafting (axes 2..4, pickaxes): workshop level or the forge, half price + materials. */
	EFVBuyResult CraftTool(UFVSaveGame& P, const TArray<FFVToolDef>& Tools, int32& Level, bool bNeedsBlueprint)
	{
		const int32 Next = FMath::Max(0, Level) + 1;
		if (Next > Tools.Num()) { return EFVBuyResult::MaxLevel; }
		const FFVToolDef& T = Tools[Next - 1];
		if (bNeedsBlueprint && !P.HasBlueprint(BlueprintPickaxe)) { return EFVBuyResult::NotForSale; }
		if (T.bForge)
		{
			// WorkshopLevel of a forge tool = the forge level it needs (the tempered pickaxe — «Закалочная кузня», level 2)
			if (P.GetYardLevel(EFVYardBuilding::Forge) < FMath::Max(1, T.WorkshopLevel)) { return EFVBuyResult::StationRequired; }
		}
		else
		{
			if (T.WorkshopLevel <= 0) { return EFVBuyResult::NotForSale; }
			if (P.GetStationLevel(EFVStation::Workshop) < T.WorkshopLevel) { return EFVBuyResult::StationRequired; }
		}
		if (P.GetLevel() < T.MinLevel) { return EFVBuyResult::LevelTooLow; }
		const int64 Coins = FMath::Max(0, T.Price / 2);
		if (!P.CanAfford(Coins)) { return EFVBuyResult::NotEnoughMoney; }
		if (!P.HasIngredients(T.Craft)) { return EFVBuyResult::NotEnoughMaterials; }
		P.ConsumeIngredients(T.Craft);
		P.Spend(Coins);
		Level = Next;
		return EFVBuyResult::Ok;
	}
}

// ---------------------------------------------------------------------------------------------- tools
EFVBuyResult UFVSaveGame::BuyAxe(FName TraderId)
{
	const TArray<FFVToolDef>& Axes = FVDBU6::Axes();
	const int32 Next = FMath::Max(0, U6.AxeLevel) + 1;
	if (Next > Axes.Num()) { return EFVBuyResult::MaxLevel; }
	const FFVToolDef& A = Axes[Next - 1];
	const FFVTraderDef* T = FVDB::FindTrader(TraderId);
	if (!T) { return EFVBuyResult::Invalid; }
	if (A.Price <= 0 || (A.TraderMask & T->Mask) == 0) { return EFVBuyResult::NotForSale; }
	if (GetLevel() < A.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!CanAfford(A.Price) || !Spend(A.Price)) { return EFVBuyResult::NotEnoughMoney; }
	U6.AxeLevel = Next;
	UE_LOG(LogFV, Log, TEXT("FV: bought axe level %d"), U6.AxeLevel);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::CraftAxe()
{
	if (U6.AxeLevel <= 0) { return EFVBuyResult::NotForSale; }   // level 1 is bought from Боцман
	int32 L = U6.AxeLevel;
	const EFVBuyResult R = FVSGU6World::CraftTool(*this, FVDBU6::Axes(), L, false);
	if (R == EFVBuyResult::Ok) { U6.AxeLevel = L; UE_LOG(LogFV, Log, TEXT("FV: crafted axe level %d"), L); }
	return R;
}

EFVBuyResult UFVSaveGame::CraftPickaxe()
{
	int32 L = U6.PickaxeLevel;
	const EFVBuyResult R = FVSGU6World::CraftTool(*this, FVDBU6::Pickaxes(), L, L <= 0);
	if (R == EFVBuyResult::Ok) { U6.PickaxeLevel = L; UE_LOG(LogFV, Log, TEXT("FV: crafted pickaxe level %d"), L); }
	return R;
}

void UFVSaveGame::LearnBlueprint(FName BlueprintId)
{
	if (!BlueprintId.IsNone())
	{
		U6.Blueprints.AddUnique(BlueprintId);
	}
}

// ---------------------------------------------------------------------------------------------- world nodes
const FFVWorldNode* UFVSaveGame::FindNode(FName NodeId) const
{
	return U6.Nodes.FindByPredicate([NodeId](const FFVWorldNode& N) { return N.Id == NodeId; });
}

EFVTreeState UFVSaveGame::GetTreeState(FName NodeId, EFVTreeState Default) const
{
	const FFVWorldNode* N = FindNode(NodeId);
	if (!N || N->State != uint8(EFVTreeState::Stump)) { return Default; }
	if (U6.PlayedMinutes >= N->ReadyAt) { return EFVTreeState::Marked; }
	const double Regrow = FMath::Max(1.0, double(FVDBU6::TreeRegrowMinutes()));
	return (N->ReadyAt - U6.PlayedMinutes) <= Regrow * 0.5 ? EFVTreeState::Sapling : EFVTreeState::Stump;
}

bool UFVSaveGame::IsNodeReady(FName NodeId) const
{
	const FFVWorldNode* N = FindNode(NodeId);
	return !N || N->ReadyAt <= U6.PlayedMinutes;
}

EFVBuyResult UFVSaveGame::ChopTree(FName NodeId, EFVTreeKind Kind, int32 BonusBranches, TArray<FFVIngredient>& OutLoot)
{
	OutLoot.Reset();
	if (NodeId.IsNone() || Kind >= EFVTreeKind::Count) { return EFVBuyResult::Invalid; }
	if (GetTreeState(NodeId, EFVTreeState::Marked) != EFVTreeState::Marked) { return EFVBuyResult::Invalid; }
	if (U6.AxeLevel <= 0) { return EFVBuyResult::StationRequired; }
	if (FVDBU6::TreeHits(Kind, U6.AxeLevel) <= 0) { return EFVBuyResult::LevelTooLow; }
	FRandomStream Rng = FVSGU6World::MakeRng(NodeId.ToString(), U6.PlayedMinutes, 0x7EE5u);
	TArray<FFVIngredient> Loot = FVDBU6::RollTreeLoot(Kind, FMath::Clamp(BonusBranches, 0, 9), Rng);
	if (!FVSGU6World::GiveAll(*this, Loot)) { return EFVBuyResult::NoRoom; }
	FFVWorldNode& N = FVSGU6World::NodeFor(U6.Nodes, NodeId);
	N.State = uint8(EFVTreeState::Stump);
	N.ReadyAt = U6.PlayedMinutes + FMath::Max(1.0, double(FVDBU6::TreeRegrowMinutes()));
	++U6.TreesChopped;
	OutLoot = MoveTemp(Loot);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::ClearBlockage(TArray<FFVIngredient>& OutLoot)
{
	OutLoot.Reset();
	if (U6.bBlockageCleared) { return EFVBuyResult::AlreadyOwned; }
	if (U6.AxeLevel <= 0) { return EFVBuyResult::StationRequired; }
	TArray<FFVIngredient> Loot = FVDBU6::BlockageLoot();
	if (!FVSGU6World::GiveAll(*this, Loot)) { return EFVBuyResult::NoRoom; }
	U6.bBlockageCleared = true;
	OutLoot = MoveTemp(Loot);
	UE_LOG(LogFV, Log, TEXT("FV: forest blockage cleared"));
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::CollectJunkPile(FName NodeId, TArray<FFVIngredient>& OutLoot)
{
	OutLoot.Reset();
	if (NodeId.IsNone() || !IsNodeReady(NodeId)) { return EFVBuyResult::Invalid; }
	FRandomStream Rng = FVSGU6World::MakeRng(NodeId.ToString(), U6.PlayedMinutes, 0x5C4Au);
	TArray<FFVIngredient> Loot = FVDBU6::RollJunkPileLoot(Rng);
	if (!FVSGU6World::GiveAll(*this, Loot)) { return EFVBuyResult::NoRoom; }
	FFVWorldNode& N = FVSGU6World::NodeFor(U6.Nodes, NodeId);
	N.State = 1;
	N.ReadyAt = U6.PlayedMinutes + FMath::Max(1.0, double(FVDBU6::JunkPileRegrowMinutes()));
	OutLoot = MoveTemp(Loot);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::ExcavateBoulder(FName NodeId, const TArray<FFVIngredient>& Finds)
{
	if (NodeId.IsNone() || !IsNodeReady(NodeId)) { return EFVBuyResult::Invalid; }
	if (!FVSGU6World::GiveAll(*this, Finds)) { return EFVBuyResult::NoRoom; }
	FFVWorldNode& N = FVSGU6World::NodeFor(U6.Nodes, NodeId);
	N.State = 1;
	N.ReadyAt = U6.PlayedMinutes + FMath::Max(1.0, double(FVDBU6::BoulderRegrowMinutes()));
	++U6.Excavations;
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::ClearRubble(FName RubbleId)
{
	if (!RubbleId.ToString().StartsWith(TEXT("rubble_"))) { return EFVBuyResult::Invalid; }
	if (U6.RubbleCleared.Contains(RubbleId)) { return EFVBuyResult::AlreadyOwned; }
	if (U6.PickaxeLevel <= 0) { return EFVBuyResult::StationRequired; }
	if (GetLevel() < FVSGU6World::RubbleMinLevel) { return EFVBuyResult::LevelTooLow; }
	U6.RubbleCleared.Add(RubbleId);
	UE_LOG(LogFV, Log, TEXT("FV: rubble %s cleared (%d/%d)"), *RubbleId.ToString(), U6.RubbleCleared.Num(), FVSGU6World::RubbleTotal);
	if (IsCaveOpen() && !IsLocationUnlocked(FVDBU6::CaveId())) { UnlockLocation(FVDBU6::CaveId()); }   // §9а: the Cave opens
	return EFVBuyResult::Ok;
}

bool UFVSaveGame::IsCaveOpen() const
{
	return U6.RubbleCleared.Num() >= FVSGU6World::RubbleTotal;
}

// ---------------------------------------------------------------------------------------------- yard, garden, boat
int32 UFVSaveGame::GetYardLevel(EFVYardBuilding B) const
{
	const int32* L = U6.YardBuildings.Find(FVDBU6::YardBuildingId(B));
	return L ? FMath::Max(0, *L) : 0;
}

EFVBuyResult UFVSaveGame::CanBuildYard(EFVYardBuilding B) const
{
	if (B >= EFVYardBuilding::Count) { return EFVBuyResult::Invalid; }
	const FFVYardBuildingDef& D = FVDBU6::YardBuilding(B);
	if (D.Id.IsNone() || D.Levels.Num() == 0) { return EFVBuyResult::Invalid; }
	const int32 Cur = GetYardLevel(B);
	if (Cur >= D.Levels.Num()) { return EFVBuyResult::MaxLevel; }
	if (D.bNeedsBlockageCleared && !U6.bBlockageCleared) { return EFVBuyResult::StationRequired; }
	const FFVStationLevel& Next = D.Levels[Cur];
	if (GetLevel() < Next.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!CanAfford(FMath::Max(0, Next.Price))) { return EFVBuyResult::NotEnoughMoney; }
	if (!HasIngredients(Next.Materials)) { return EFVBuyResult::NotEnoughMaterials; }
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::BuildYard(EFVYardBuilding B)
{
	const EFVBuyResult R = CanBuildYard(B);
	if (R != EFVBuyResult::Ok) { return R; }
	const FFVYardBuildingDef& D = FVDBU6::YardBuilding(B);
	const int32 Cur = GetYardLevel(B);
	const FFVStationLevel& Next = D.Levels[Cur];
	ConsumeIngredients(Next.Materials);
	Spend(FMath::Max(0, Next.Price));
	U6.YardBuildings.FindOrAdd(D.Id) = Cur + 1;
	if (B == EFVYardBuilding::Garden && U6.Garden.Num() < GardenBeds())
	{
		U6.Garden.SetNum(GardenBeds());
	}
	UE_LOG(LogFV, Log, TEXT("FV: yard %s -> level %d"), *D.Id.ToString(), Cur + 1);
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::GardenBeds() const
{
	const int32 L = GetYardLevel(EFVYardBuilding::Garden);
	if (L <= 0) { return 0; }
	const FFVYardBuildingDef& D = FVDBU6::YardBuilding(EFVYardBuilding::Garden);
	return D.Levels.IsValidIndex(L - 1) ? FMath::Clamp(D.Levels[L - 1].Capacity, 0, 12) : 0;
}

EFVBuyResult UFVSaveGame::PlantSeed(int32 Bed, FName SeedId)
{
	if (Bed < 0 || Bed >= GardenBeds()) { return EFVBuyResult::Invalid; }
	if (U6.Garden.IsValidIndex(Bed) && !U6.Garden[Bed].Seed.IsNone()) { return EFVBuyResult::AlreadyOwned; }
	const FFVSeedDef* S = FVDBU6::FindSeed(SeedId);
	if (!S) { return EFVBuyResult::Invalid; }
	if (GetLevel() < S->MinLevel) { return EFVBuyResult::LevelTooLow; }
	const TArray<FFVIngredient> Cost = { FFVIngredient{ S->SeedId, 1 } };
	if (!HasIngredients(Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	ConsumeIngredients(Cost);
	if (U6.Garden.Num() <= Bed) { U6.Garden.SetNum(Bed + 1); }
	FFVGardenBed& G = U6.Garden[Bed];
	G.Seed = S->SeedId;
	G.PlantedAt = U6.PlayedMinutes;
	G.Growth = 0.f;
	return EFVBuyResult::Ok;
}

float UFVSaveGame::GetBedGrowth(int32 Bed) const
{
	if (!U6.Garden.IsValidIndex(Bed) || U6.Garden[Bed].Seed.IsNone()) { return -1.f; }
	return FMath::Clamp(U6.Garden[Bed].Growth, 0.f, 1.f);
}

EFVBuyResult UFVSaveGame::HarvestBed(int32 Bed, TArray<FFVIngredient>& OutLoot)
{
	OutLoot.Reset();
	if (!U6.Garden.IsValidIndex(Bed) || U6.Garden[Bed].Seed.IsNone()) { return EFVBuyResult::Invalid; }
	FFVGardenBed& G = U6.Garden[Bed];
	if (G.Growth < 1.f) { return EFVBuyResult::Invalid; }
	const FFVSeedDef* S = FVDBU6::FindSeed(G.Seed);
	if (!S) { return EFVBuyResult::Invalid; }
	FRandomStream Rng = FVSGU6World::MakeRng(FString::Printf(TEXT("bed_%d_%s"), Bed, *G.Seed.ToString()), G.PlantedAt, 0x6A7Du);
	TArray<FFVIngredient> Loot;
	const int32 Crop = Rng.RandRange(FMath::Max(0, S->YieldMin), FMath::Max(S->YieldMin, S->YieldMax));
	if (Crop > 0 && !S->CropId.IsNone()) { Loot.Add(FFVIngredient{ S->CropId, Crop }); }
	const int32 Seeds = Rng.RandRange(FMath::Max(0, S->SeedsMin), FMath::Max(S->SeedsMin, S->SeedsMax));
	if (Seeds > 0 && !S->SeedId.IsNone())
	{
		FFVIngredient* Same = Loot.FindByPredicate([S](const FFVIngredient& I) { return I.ItemId == S->SeedId; });
		if (Same) { Same->Count += Seeds; } else { Loot.Add(FFVIngredient{ S->SeedId, Seeds }); }
	}
	if (!FVSGU6World::GiveAll(*this, Loot)) { return EFVBuyResult::NoRoom; }
	G = FFVGardenBed();
	++U6.Harvests;
	OutLoot = MoveTemp(Loot);
	return EFVBuyResult::Ok;
}

void UFVSaveGame::TickGarden(double PlayedDelta, bool bRain)
{
	if (!FMath::IsFinite(PlayedDelta) || PlayedDelta <= 0.0) { return; }
	for (FFVGardenBed& G : U6.Garden)
	{
		if (G.Seed.IsNone() || G.Growth >= 1.f) { continue; }
		const FFVSeedDef* S = FVDBU6::FindSeed(G.Seed);
		if (!S) { continue; }
		const double Speed = bRain ? double(FMath::Max(0.f, S->RainMult)) : 1.0;
		G.Growth = FMath::Clamp(G.Growth + float(PlayedDelta * Speed / double(FMath::Max(0.1f, S->GrowMinutes))), 0.f, 1.f);
	}
}

EFVBuyResult UFVSaveGame::RepairBoatStage()
{
	const TArray<FFVBoatStage>& Stages = FVDBU6::BoatStages();
	if (U6.BoatStage < 0 || U6.BoatStage >= Stages.Num()) { return EFVBuyResult::MaxLevel; }
	const FFVBoatStage& S = Stages[U6.BoatStage];
	if (GetLevel() < S.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!CanAfford(FMath::Max(0, S.Coins))) { return EFVBuyResult::NotEnoughMoney; }
	if (!HasIngredients(S.Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	ConsumeIngredients(S.Cost);
	Spend(FMath::Max(0, S.Coins));
	++U6.BoatStage;
	UE_LOG(LogFV, Log, TEXT("FV: boat stage %d done"), U6.BoatStage);
	if (IsBoatRepaired() && !IsLocationUnlocked(FVDBU6::CityId())) { UnlockLocation(FVDBU6::CityId()); }   // §7: the City opens
	return EFVBuyResult::Ok;
}

bool UFVSaveGame::IsBoatRepaired() const
{
	const int32 N = FVDBU6::BoatStages().Num();
	return U6.BoatStage >= (N > 0 ? N : 3);
}
