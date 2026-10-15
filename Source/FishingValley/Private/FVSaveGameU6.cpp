// UPDATE 6 [DATA-CORE] — profile logic of update 6 (FVSaveGame.h «UPDATE 6 [DATA-CORE]»): lifecycle & migration,
// levels 11–15, played minutes, update-6 achievements. Owner: DATA-CORE.
// Also: FVSaveGameU6Ach — the update-6 achievement values used by FVSaveGame.cpp (AchievementValues / CheckAchievements).

#include "FVSaveGame.h"
#include "FVDatabase.h"

namespace FVSGHome
{
	bool CanObtainRecipe(const UFVSaveGame& P, const FFVRecipeDef& R);   // FVSaveGameHome.cpp
}

namespace FVSaveGameU6Impl
{
	constexpr int32 CurrentU6Version = 1;
	constexpr int32 MaxSleepStreak = 2;       // §0.2: >= 2 = no bed until 18:00
	constexpr int32 MaxSawQueue = 99;

	int32 ClampCounter(int32 V) { return FMath::Max(0, V); }

	double CleanMinutes(double V)
	{
		return (FMath::IsFinite(V) && V > 0.0) ? V : 0.0;
	}

	/** XP threshold of level 10 — XP stops here until the first arrival in the City (Р1). */
	int32 FrozenXp() { return FVDB::XpForLevel(FVDBU6::FrozenLevel()); }

	/** XP threshold of level 11 — given on the first arrival in the City (§9). */
	int32 CityXp() { return FVDB::XpForLevel(FVDBU6::FrozenLevel() + 1); }

	int32 CountBits8(uint8 V)
	{
		int32 N = 0;
		for (; V != 0; V &= uint8(V - 1)) { ++N; }
		return N;
	}

	/** Distinct fish caught that pass Pred, and how many fish pass Pred in the table. */
	template <typename TPred>
	void CountCaught(const UFVSaveGame& S, TPred&& Pred, int64& OutCaught, int64& OutTotal)
	{
		OutCaught = 0;
		OutTotal = 0;
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (!Pred(F)) { continue; }
			++OutTotal;
			const FFVFishRecord* R = S.GetRecord(F.Id);
			if (R && R->Caught > 0) { ++OutCaught; }
		}
	}
}

// ---------------------------------------------------------------------------------------------- achievements (shared)
namespace FVSaveGameU6Ach
{
	bool IsClassicSpecies(const FFVFishDef& F)
	{
		return !F.bExpert && !FVDBU6::IsExpertLocation(F.Location);
	}

	/** Current value / target of an update-6 achievement kind. false (values untouched) = not an update-6 kind. */
	bool Values(const UFVSaveGame& S, const FFVAchievementDef& A, int64& OutCur, int64& OutTarget)
	{
		using namespace FVSaveGameU6Impl;
		const FFVSaveU6& U = S.U6;
		int64 Cur = 0;
		int64 Target = FMath::Max(1, A.Target);
		switch (A.Kind)
		{
		case EFVAchKind::LegendsClassicAll:
		{
			int64 Total = 0;
			CountCaught(S, [](const FFVFishDef& F) { return !F.bExpert && F.Legend != EFVLegend::None; }, Cur, Total);
			Target = FMath::Max<int64>(1, Total);
			break;
		}
		case EFVAchKind::LegendsExpertAll:
		{
			int64 Total = 0;
			CountCaught(S, [](const FFVFishDef& F) { return F.bExpert && F.ExpertLegend != EFVExpertLegend::None; }, Cur, Total);
			Target = FMath::Max<int64>(1, Total);
			break;
		}
		case EFVAchKind::ExpertSpecies:
		{
			int64 Total = 0;
			CountCaught(S, [](const FFVFishDef& F) { return F.bExpert && F.ExpertLegend == EFVExpertLegend::None; }, Cur, Total);
			if (Total > 0) { Target = FMath::Min<int64>(Target, Total); }   // never above what exists
			break;
		}
		case EFVAchKind::ExpertRodTypes:
			Cur = CountBits8(U.ExpertTypesCaught);
			Target = FMath::Min<int64>(Target, int64(EFVExpertRodType::Count));
			break;
		case EFVAchKind::IridiumLegend:
		{
			// any legend (classic or expert) of iridium quality; the flag is set by the catch, records cover old saves
			Cur = U.bIridiumLegend ? 1 : 0;
			if (Cur == 0)
			{
				for (const TPair<FName, FFVFishRecord>& P : S.Records)
				{
					const FFVFishDef* F = P.Value.Caught > 0 ? FVDB::FindFish(P.Key) : nullptr;
					if (F && F->bLegendary && P.Value.BestQuality >= uint8(EFVQuality::Iridium)) { Cur = 1; break; }
				}
			}
			Target = 1;
			break;
		}
		case EFVAchKind::FlawlessExpertLegend: Cur = U.bFlawlessLegend ? 1 : 0; Target = 1; break;
		case EFVAchKind::TreesChopped:  Cur = U.TreesChopped; break;
		case EFVAchKind::Harvests:      Cur = U.Harvests; break;
		case EFVAchKind::Forgings:      Cur = U.Forgings; break;
		case EFVAchKind::BoatRepaired:  Cur = S.IsBoatRepaired() ? 1 : 0; Target = 1; break;
		case EFVAchKind::BoutiqueSpent: Cur = U.BoutiqueSpent; break;
		case EFVAchKind::CityTasksDone: Cur = U.CityTasksDone; break;
		case EFVAchKind::FactsKnown:
		{
			Cur = U.KnownFacts.Num();
			int64 Total = 0;   // never above the facts that exist (the table of expert fish decides)
			for (const FFVExpertFishDef& E : FVDBU6::ExpertFish()) { Total += FVDBU6::Facts(E.FishId).Num(); }
			if (Total > 0) { Target = FMath::Min<int64>(Target, Total); }
			break;
		}
		default:
			return false;
		}
		OutCur = FMath::Max<int64>(0, Cur);
		OutTarget = FMath::Max<int64>(1, Target);
		return true;
	}
}

// ---------------------------------------------------------------------------------------------- lifecycle
void UFVSaveGame::ResetU6()
{
	U6 = FFVSaveU6();
	U6.U6Version = FVSaveGameU6Impl::CurrentU6Version;   // a new game needs no migration
}

void UFVSaveGame::FixupU6()
{
	using namespace FVSaveGameU6Impl;
	int32 Fixes = 0;
	FFVSaveU6& U = U6;

	// ---- migration of a 0.5 save (U6Version 0)
	if (U.U6Version <= 0)
	{
		// the tournament counter starts from the XP earned so far (the server counted s_xp from the total XP)
		if (U.XpEarned <= 0) { U.XpEarned = FMath::Max(0, Xp); }
		// XP above 10 000 is trimmed below (shown as «стаж» in the profile). The net on the pier spot taken by the boat
		// (ocean:1, §7) was already given back to the backpack by the nets block of FixupAfterLoad
		// (FVSaveGameFix::NetSpotCount("ocean") = 1); its catch went to the keepnet / backpack.
		UE_LOG(LogFV, Log, TEXT("FV: migrating a 0.5 profile to update 6 (XP %d)"), Xp);
		++Fixes;

		// a 0.5 order taken for a dish that can no longer be cooked (уха needs a carrot from the garden now, Р41/Р42):
		// the order is cancelled and its deposit returned, unless the dish is already in the bags
		for (int32 i = Orders.Num() - 1; i >= 0; --i)
		{
			const FFVOrder& O = Orders[i];
			const FFVItemDef* Item = FVDB::FindItem(O.ItemId);
			if (O.State != uint8(EFVOrderState::Taken) || !Item || Item->Kind != EFVItemKind::Food) { continue; }
			if (CountItem(O.ItemId) >= FMath::Max(1, O.Count)) { continue; }
			bool bCookable = false;
			for (const FFVRecipeDef& R : FVDB::Recipes())
			{
				if (R.OutputId == O.ItemId && FVSGHome::CanObtainRecipe(*this, R)) { bCookable = true; break; }
			}
			if (bCookable) { continue; }
			Earn(FMath::Max(0, O.Deposit), false);
			UE_LOG(LogFV, Log, TEXT("FV: 0.5 order %s (%s) cancelled, deposit %d returned"), *O.Id.ToString(), *O.ItemId.ToString(), O.Deposit);
			Orders.RemoveAt(i);
		}
	}

	// ---- levels 11-15 (§9): before the City XP never exceeds the level-10 threshold; after it, never below level 11
	const int32 Frozen = FrozenXp();
	if (!U.bVisitedCity && Xp > Frozen)
	{
		U.XpSeniority = int32(FMath::Clamp<int64>(int64(U.XpSeniority) + (int64(Xp) - Frozen), 0, MAX_int32));
		Xp = Frozen;
		++Fixes;
	}
	if (U.bVisitedCity && Xp < CityXp()) { Xp = CityXp(); ++Fixes; }
	if (U.XpSeniority < 0) { U.XpSeniority = 0; ++Fixes; }
	if (U.XpEarned < 0) { U.XpEarned = 0; ++Fixes; }

	// ---- time & sleep
	if (!FMath::IsFinite(U.PlayedMinutes) || U.PlayedMinutes < 0.0) { U.PlayedMinutes = 0.0; ++Fixes; }
	if (U.SleepStreak < 0 || U.SleepStreak > MaxSleepStreak) { U.SleepStreak = FMath::Clamp(U.SleepStreak, 0, MaxSleepStreak); ++Fixes; }

	// ---- tools & blueprints (clamped only against tables that exist)
	{
		const int32 NumAxes = FVDBU6::Axes().Num();
		const int32 NumPicks = FVDBU6::Pickaxes().Num();
		if (U.AxeLevel < 0 || (NumAxes > 0 && U.AxeLevel > NumAxes)) { U.AxeLevel = FMath::Clamp(U.AxeLevel, 0, FMath::Max(0, NumAxes)); ++Fixes; }
		if (U.PickaxeLevel < 0 || (NumPicks > 0 && U.PickaxeLevel > NumPicks)) { U.PickaxeLevel = FMath::Clamp(U.PickaxeLevel, 0, FMath::Max(0, NumPicks)); ++Fixes; }
		TArray<FName> Bps;
		for (const FName& Id : U.Blueprints)
		{
			const FFVItemDef* I = FVDB::FindItem(Id);
			if (I && I->Kind == EFVItemKind::Blueprint) { Bps.AddUnique(Id); }
		}
		if (Bps.Num() != U.Blueprints.Num()) { U.Blueprints = MoveTemp(Bps); ++Fixes; }
	}

	// ---- world: rubble ids, nodes by stable id (one entry per id, sane timers)
	{
		TArray<FName> Rubble;
		for (const FName& Id : U.RubbleCleared)
		{
			if (Id.ToString().StartsWith(TEXT("rubble_"))) { Rubble.AddUnique(Id); }
		}
		if (Rubble.Num() != U.RubbleCleared.Num()) { U.RubbleCleared = MoveTemp(Rubble); ++Fixes; }

		TArray<FFVWorldNode> Nodes;
		for (const FFVWorldNode& Src : U.Nodes)
		{
			if (Src.Id.IsNone() || !Src.Id.ToString().Contains(TEXT(":"))) { ++Fixes; continue; }
			if (Nodes.ContainsByPredicate([&Src](const FFVWorldNode& N) { return N.Id == Src.Id; })) { ++Fixes; continue; }
			FFVWorldNode N = Src;
			if (N.State >= uint8(EFVTreeState::Count)) { N.State = 0; ++Fixes; }
			N.ReadyAt = CleanMinutes(N.ReadyAt);
			Nodes.Add(N);
		}
		U.Nodes = MoveTemp(Nodes);
	}

	// ---- yard: known buildings with a real level; garden beds with known seeds; boat; sawmill queue
	{
		TMap<FName, int32> Yard;
		for (const TPair<FName, int32>& P : U.YardBuildings)
		{
			const FFVYardBuildingDef* Y = FVDBU6::FindYardBuilding(P.Key);
			if (!Y || P.Value <= 0) { ++Fixes; continue; }
			const int32 Max = Y->Levels.Num();
			Yard.Add(P.Key, Max > 0 ? FMath::Min(P.Value, Max) : P.Value);
		}
		U.YardBuildings = MoveTemp(Yard);

		for (FFVGardenBed& Bed : U.Garden)
		{
			if (!Bed.Seed.IsNone() && !FVDBU6::FindSeed(Bed.Seed)) { Bed = FFVGardenBed(); ++Fixes; continue; }
			if (Bed.Seed.IsNone()) { Bed.PlantedAt = 0.0; Bed.Growth = 0.f; continue; }
			Bed.PlantedAt = CleanMinutes(Bed.PlantedAt);
			Bed.Growth = FMath::IsFinite(Bed.Growth) ? FMath::Clamp(Bed.Growth, 0.f, 1.f) : 0.f;
		}

		const int32 NumStages = FVDBU6::BoatStages().Num();
		if (U.BoatStage < 0 || (NumStages > 0 && U.BoatStage > NumStages)) { U.BoatStage = FMath::Clamp(U.BoatStage, 0, FMath::Max(0, NumStages)); ++Fixes; }
		if (U.SawQueue < 0 || U.SawQueue > MaxSawQueue) { U.SawQueue = FMath::Clamp(U.SawQueue, 0, MaxSawQueue); ++Fixes; }
	}

	// ---- counters
	U.TreesChopped = ClampCounter(U.TreesChopped);
	U.Harvests = ClampCounter(U.Harvests);
	U.Forgings = ClampCounter(U.Forgings);
	U.Excavations = ClampCounter(U.Excavations);
	U.CityTasksDone = ClampCounter(U.CityTasksDone);
	if (U.BoutiqueSpent < 0) { U.BoutiqueSpent = 0; ++Fixes; }

	if (U.U6Version < CurrentU6Version) { U.U6Version = CurrentU6Version; }
	if (Fixes > 0)
	{
		UE_LOG(LogFV, Log, TEXT("FV: update-6 profile state checked (%d fixes)"), Fixes);
	}

	FixupExpert();
	FixupCity();
	// the update-6 locations follow their conditions (forest — level 4, City — the boat, Cave — the rubble)
	for (const FName LocId : { FVDBU6::ForestId(), FVDBU6::CityId(), FVDBU6::CaveId() }) { if (!IsLocationUnlocked(LocId)) { UnlockLocation(LocId); } }
}

// ---------------------------------------------------------------------------------------------- levels 11-15
bool UFVSaveGame::IsXpFrozen() const
{
	return !U6.bVisitedCity && Xp >= FVSaveGameU6Impl::FrozenXp();
}

int32 UFVSaveGame::AddRewardXp(int32 Amount, int64& OutCoins)
{
	OutCoins = 0;
	if (Amount <= 0) { return 0; }
	int32 ToXp = Amount;
	if (!U6.bVisitedCity)
	{
		// Р10: while the level is frozen, reward XP above the level-10 threshold is paid out as coins 1:1
		const int32 Room = FMath::Max(0, FVSaveGameU6Impl::FrozenXp() - Xp);
		ToXp = FMath::Min(Amount, Room);
		OutCoins = int64(Amount) - ToXp;
	}
	const int32 Gained = ToXp > 0 ? AddXp(ToXp) : 0;   // AddXp adds ToXp to U6.XpEarned
	if (OutCoins > 0)
	{
		U6.XpEarned = FMath::Clamp<int64>(U6.XpEarned + OutCoins, 0, MAX_int64 / 2);   // the whole reward counts
		Earn(OutCoins, false);
	}
	return Gained;
}

int32 UFVSaveGame::ArriveInCity()
{
	if (U6.bVisitedCity) { return 0; }
	const int32 Before = GetLevel();
	U6.bVisitedCity = true;
	Xp = FMath::Max(Xp, FVSaveGameU6Impl::CityXp());   // §9: XP becomes the threshold of level 11
	UE_LOG(LogFV, Log, TEXT("FV: first arrival in the City — level %d"), GetLevel());
	return FMath::Max(0, GetLevel() - Before);
}

void UFVSaveGame::AddPlayedMinutes(double Minutes)
{
	if (Minutes > 0.0 && FMath::IsFinite(Minutes))
	{
		U6.PlayedMinutes += FMath::Min(Minutes, 60.0);
	}
}

// ---------------------------------------------------------------------------------------------- achievements
bool UFVSaveGame::AchievementProgressU6(const FFVAchievementDef& A, float& Out01, FString& OutText) const
{
	Out01 = 0.f;
	OutText.Reset();
	int64 Cur = 0, Target = 1;
	if (!FVSaveGameU6Ach::Values(*this, A, Cur, Target)) { return false; }
	if (IsAchievementDone(A.Id)) { Cur = Target; }
	Out01 = FMath::Clamp(float(double(Cur) / double(FMath::Max<int64>(1, Target))), 0.f, 1.f);
	OutText = FString::Printf(TEXT("%s / %s"), *FV::Money(FMath::Min(Cur, Target)), *FV::Money(Target));
	return true;
}
