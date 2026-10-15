// UPDATE 6 [CITY] — profile logic of the City task board (§8.2, Р34) and the artel (§8.3, Р36)
// (FVSaveGame.h «UPDATE 6 [CITY]»). Owner: CITY (also FVScreenCityBoard.cpp, FVScreenArtel.cpp).

#include "FVSaveGame.h"
#include "FVDatabase.h"

namespace FVSGHome
{
	bool CanObtainRecipe(const UFVSaveGame& P, const FFVRecipeDef& R);   // FVSaveGameHome.cpp
}

namespace FVSGCity
{
	constexpr int32 MaxTaken = 3;                  // like the pond board
	constexpr int32 OffersPerDay = 4;
	constexpr int64 ArtelHirePrice = 20000;        // §8.3 (предл.)
	constexpr float Commission = 0.2f;             // 20 %
	constexpr double CollectEveryPlayed = 12.0;    // real minutes of play between two collections (§8.3 «сон»)
	constexpr double MorningMinute = 360.0;        // 06:00
	constexpr double MaxSimMinutes = 30.0 * 1440.0;
	constexpr int32 MaxUnits = 999;

	uint32 Mix(uint32 H, uint32 V) { H ^= V + 0x9E3779B9u + (H << 6) + (H >> 2); return H; }
	uint32 Fmix(uint32 H) { H ^= H >> 16; H *= 0x85EBCA6Bu; H ^= H >> 13; H *= 0xC2B2AE35u; H ^= H >> 16; return H; }
	uint32 StrHash(FName N) { const FString S = N.ToString().ToLower(); return FCrc::StrCrc32(*S); }

	int32 NetSeed(FName Loc, int32 Spot, int64 Minute, uint32 Salt)
	{
		uint32 H = StrHash(Loc);
		H = Mix(H, uint32(Spot) * 0x9E3779B1u + 11u);
		H = Mix(H, uint32(uint64(Minute) & 0xFFFFFFFFull));
		H = Mix(H, uint32(uint64(Minute) >> 32));
		H = Mix(H, Salt);
		return int32(Fmix(H));
	}

	/** Same numbers as the player's net posts (FVSaveGameHome.cpp NetSpotCount); none in the expert zones / forest. */
	int32 NetSpotCount(FName Loc)
	{
		if (Loc.IsNone() || FVDBU6::IsExpertLocation(Loc) || FVDBU6::IsForestId(Loc)) { return 0; }
		if (FVMill::IsMillId(Loc)) { return FVMill::NetSpots; }   // §18: 3 on the pond + 1 in the Omut
		const FString L = Loc.ToString().ToLower();
		if (L == TEXT("pond") || L == TEXT("river") || L == TEXT("swamp")) { return 2; }
		if (L == TEXT("mountain") || L == TEXT("ocean")) { return 1; }
		return 0;
	}

	int64 Units(const TArray<FFVInvStack>& Arr)
	{
		int64 N = 0;
		for (const FFVInvStack& S : Arr) { N += FMath::Max(0, S.Count); }
		return N;
	}

	void PutUnits(TArray<FFVInvStack>& Arr, FName Id, uint8 Q, int32 Count)
	{
		for (FFVInvStack& St : Arr)
		{
			if (Count <= 0) { return; }
			if (St.ItemId != Id || St.Quality != Q || St.Flags != 0 || St.Count >= MaxUnits) { continue; }
			const int32 Add = FMath::Min(Count, MaxUnits - St.Count);
			St.Count += Add;
			Count -= Add;
		}
		for (int32 Guard = 0; Count > 0 && Guard < 1000; ++Guard)
		{
			FFVInvStack& N = Arr.AddDefaulted_GetRef();
			N.ItemId = Id;
			N.Quality = Q;
			N.Flags = 0;
			N.Count = FMath::Min(Count, MaxUnits);
			Count -= N.Count;
		}
	}

	bool IsTaskFish(FName Id) { return FVDB::FindFish(Id) != nullptr; }

	/** Keepnet stacks a fish task accepts: that fish, quality >= min, never smoked. */
	bool FishAccepts(const FFVCityTask& T, const FFVInvStack& S)
	{
		return S.ItemId == T.ItemId && S.Count > 0 && S.Quality >= T.MinQuality && (S.Flags & FVStackFlags::Smoked) == 0;
	}

	float MaxDurOf(FName NetItem)
	{
		const FFVItemDef* Def = FVDB::FindItem(NetItem);
		return (Def && Def->NetDurability > 0.f) ? Def->NetDurability : 1.f;
	}

	bool IsDamaged(const FFVNetState& N) { return N.bTorn || N.Durability < MaxDurOf(N.NetItem) - 0.01f; }

	/** Repair units of one net: верёвка ×1 + волокно ×2 per 50 % of missing durability (at least 1 when damaged). */
	int32 RepairUnits(const FFVNetState& N)
	{
		if (!IsDamaged(N)) { return 0; }
		const float Max = MaxDurOf(N.NetItem);
		const float Missing = FMath::Clamp(Max - FMath::Max(0.f, N.Durability), 0.f, Max);
		return FMath::Clamp(FMath::CeilToInt(Missing / (0.5f * Max) - 0.001f), 1, 2);
	}

	EFVDayPhase PhaseOfHour(int32 Hour)
	{
		const int32 H = ((Hour % 24) + 24) % 24;
		if (H >= 6 && H < 12) { return EFVDayPhase::Morning; }
		if (H >= 12 && H < 18) { return EFVDayPhase::Day; }
		if (H >= 18 && H < 22) { return EFVDayPhase::Evening; }
		return EFVDayPhase::Night;
	}

	/** One unit caught by an artel net (as the player's nets; no expert fish / legends, no records). */
	void CatchOne(FFVNetState& N, int64 Minute, EFVDayPhase Phase, EFVWeather Weather, int32 Level)
	{
		FFVCatchContext Ctx;
		Ctx.Location = N.Location;
		Ctx.Depth = 0.3f;
		Ctx.PhaseMask = FVTime::Bit(Phase);
		Ctx.Weather = Weather;
		Ctx.Level = Level;
		Ctx.Rod = nullptr;
		Ctx.Bait = nullptr;
		Ctx.bNet = true;
		Ctx.JunkMultScale = 0.5f;
		Ctx.bMillOmut = FVMill::IsMillId(N.Location) && N.Spot == FVMill::OmutNetSpot;   // §18.1: the Omut net post
		FRandomStream Rng(NetSeed(N.Location, N.Spot, Minute, 0xA27E1u));
		const FFVFishDef* F = nullptr;
		const FFVJunkDef* J = nullptr;
		FVDB::PickCatch(Ctx, Rng, F, J);
		if (F && !F->bLegendary && !F->bExpert)
		{
			const float Frac = FVDB::RollSizeFrac(Level, false, 10.f, nullptr, Rng) * 0.85f;
			EFVQuality Q = FVDB::RollQuality(Frac, false, Rng);
			if (uint8(Q) > uint8(EFVQuality::Gold)) { Q = EFVQuality::Gold; }
			PutUnits(N.Catch, F->Id, uint8(Q), 1);
			N.Durability -= FVDB::NetWear(*F, Q);
			if (N.Durability <= 0.f)
			{
				N.Durability = 0.f;
				N.bTorn = true;
			}
		}
		else if (J)
		{
			PutUnits(N.Catch, J->Id, 0, 1);
		}
	}

	/** Advances one artel net to ToAbs (hour chunks, hourly tear roll of worn nets) — the player's net rules. */
	void SimulateNet(FFVNetState& N, double ToAbs, EFVWeather Weather, int32 Level)
	{
		const FFVItemDef* Def = FVDB::FindItem(N.NetItem);
		if (!FMath::IsFinite(N.SimMinute)) { N.SimMinute = ToAbs; }
		if (N.SimMinute >= ToAbs) { return; }
		if (!Def || Def->Kind != EFVItemKind::Net) { N.SimMinute = ToAbs; return; }
		const int64 Cap = FMath::Max(1, Def->NetCapacity);
		const double MinPerFish = FMath::Max(1.0, double(Def->NetMinutesPerFish));
		const float MaxDur = FMath::Max(1.f, Def->NetDurability);
		if (!FMath::IsFinite(N.Progress) || N.Progress < 0.f) { N.Progress = 0.f; }

		double T = FMath::Max(N.SimMinute, ToAbs - MaxSimMinutes);
		for (int32 Guard = 0; T < ToAbs && Guard < 1000; ++Guard)
		{
			if (N.bTorn || Units(N.Catch) >= Cap) { break; }
			const double HourStart = FMath::FloorToDouble(T / 60.0);
			const double HourEnd = (HourStart + 1.0) * 60.0;
			const double SegEnd = FMath::Min(ToAbs, HourEnd);
			const EFVDayPhase Phase = PhaseOfHour(int32(FMath::Fmod(HourStart, 24.0)));
			double Rate = 1.0 / MinPerFish;
			if (Weather == EFVWeather::Rain) { Rate *= 1.2; }
			if (Phase == EFVDayPhase::Night) { Rate *= 1.1; }
			double Cur = T;
			while (!N.bTorn && Units(N.Catch) < Cap)
			{
				const double NeedMin = (1.0 - double(N.Progress)) / Rate;
				if (Cur + NeedMin > SegEnd)
				{
					N.Progress = float(FMath::Clamp(double(N.Progress) + Rate * (SegEnd - Cur), 0.0, 0.9999));
					break;
				}
				Cur += NeedMin;
				N.Progress = 0.f;
				CatchOne(N, int64(FMath::FloorToDouble(Cur)), Phase, Weather, Level);
			}
			if (SegEnd >= HourEnd && !N.bTorn && N.Durability < 0.25f * MaxDur)
			{
				FRandomStream TearRng(NetSeed(N.Location, N.Spot, int64(HourStart), 0x7EA3u));
				if (TearRng.FRand() < 0.04f) { N.bTorn = true; }
			}
			T = SegEnd;
		}
		N.SimMinute = ToAbs;
	}

	/** «23 рыбы» style plural. */
	FString Plural(int64 N, const TCHAR* One, const TCHAR* Few, const TCHAR* Many)
	{
		const int64 A = (N < 0 ? -N : N) % 100;
		const int64 B = A % 10;
		if (A >= 11 && A <= 14) { return Many; }
		if (B == 1) { return One; }
		if (B >= 2 && B <= 4) { return Few; }
		return Many;
	}

	/** «3 450» */
	FString Thousands(int64 V)
	{
		FString S = FString::Printf(TEXT("%lld"), (long long)(V < 0 ? -V : V));
		for (int32 i = S.Len() - 3; i > 0; i -= 3) { S.InsertAt(i, TEXT(" ")); }
		return V < 0 ? TEXT("-") + S : S;
	}

	template <typename T>
	const T* PickWeighted(const TArray<const T*>& Pool, const TArray<float>& W, FRandomStream& Rng)
	{
		float Total = 0.f;
		for (int32 i = 0; i < Pool.Num(); ++i) { Total += W.IsValidIndex(i) ? FMath::Max(0.f, W[i]) : 0.f; }
		if (Pool.Num() == 0 || Total <= 0.f) { return nullptr; }
		float R = Rng.FRandRange(0.f, Total);
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			R -= W.IsValidIndex(i) ? FMath::Max(0.f, W[i]) : 0.f;
			if (R <= 0.f) { return Pool[i]; }
		}
		return Pool.Last();
	}

	/** City villagers who give tasks (role «задания Города»). */
	TArray<FName> TaskNpcs()
	{
		TArray<FName> Out;
		for (const FFVNpcDef* N : FVDBU6::NpcsAt(FVDBU6::CityId()))
		{
			if (N && N->Role.Contains(TEXT("задания"))) { Out.Add(N->Id); }
		}
		if (Out.Num() == 0) { Out.Add(FName(TEXT("npc_burun"))); }
		return Out;
	}
}

// ================================================================================================
// City task board
// ================================================================================================
void UFVSaveGame::FixupCity()
{
	using namespace FVSGCity;
	// ---- tasks: valid ids / states / numbers
	U6.CityTasks.RemoveAll([](const FFVCityTask& T)
	{
		return T.Id.IsNone() || T.ItemId.IsNone() || (!FVDB::FindFish(T.ItemId) && !FVDB::FindItem(T.ItemId) && !FVDB::FindBait(T.ItemId));
	});
	for (FFVCityTask& T : U6.CityTasks)
	{
		if (T.State >= uint8(EFVOrderState::Count)) { T.State = uint8(EFVOrderState::Failed); }
		T.Count = FMath::Clamp(T.Count, 1, 99);
		T.Deposit = FMath::Max(0, T.Deposit);
		T.Reward = FMath::Max(0, T.Reward);
		T.DurationHours = FMath::Clamp(T.DurationHours, 1, 24 * 7);
		T.MinQuality = uint8(FMath::Min<int32>(T.MinQuality, int32(EFVQuality::Iridium)));
		if (!FMath::IsFinite(T.Deadline)) { T.Deadline = 0.0; }
	}
	// more than 3 taken (hand-edited save): the extra ones fail
	int32 Taken = 0;
	for (FFVCityTask& T : U6.CityTasks)
	{
		if (T.State == uint8(EFVOrderState::Taken) && ++Taken > MaxTaken) { T.State = uint8(EFVOrderState::Failed); }
	}
	U6.CityTasksDone = FMath::Max(0, U6.CityTasksDone);

	// ---- artel stock: only net items
	U6.ArtelStock.RemoveAll([](const FFVInvStack& S)
	{
		const FFVItemDef* D = FVDB::FindItem(S.ItemId);
		return !D || D->Kind != EFVItemKind::Net || S.Count <= 0;
	});
	// ---- artel locations: real net locations only
	U6.ArtelLocations.RemoveAll([](const FName& L) { return FVDB::LocationIndex(L) == INDEX_NONE || NetSpotCount(L) <= 0; });
	// ---- artel nets: valid place, unique, never on the player's net; displaced intact nets go back to the stock
	TArray<FFVNetState> Keep;
	for (const FFVNetState& N : U6.ArtelNets)
	{
		const FFVItemDef* D = FVDB::FindItem(N.NetItem);
		if (!D || D->Kind != EFVItemKind::Net) { continue; }
		const bool bBadSpot = N.Spot < 0 || N.Spot >= NetSpotCount(N.Location) || FVDB::LocationIndex(N.Location) == INDEX_NONE;
		const bool bDup = Keep.ContainsByPredicate([&N](const FFVNetState& K) { return K.Location == N.Location && K.Spot == N.Spot; });
		if (bBadSpot || bDup || FindNet(N.Location, N.Spot))
		{
			if (!N.bTorn) { PutUnits(U6.ArtelStock, N.NetItem, 0, 1); }
			continue;
		}
		FFVNetState& K = Keep.Add_GetRef(N);
		K.Durability = FMath::Clamp(FMath::IsFinite(K.Durability) ? K.Durability : 0.f, 0.f, MaxDurOf(K.NetItem));
		if (!FMath::IsFinite(K.SimMinute)) { K.SimMinute = -1.0; }
		K.Catch.RemoveAll([](const FFVInvStack& S) { return S.Count <= 0 || (!FVDB::IsFishId(S.ItemId) && !FVDB::IsJunkId(S.ItemId)); });
	}
	U6.ArtelNets = MoveTemp(Keep);
	if (!FMath::IsFinite(U6.ArtelLastCollect)) { U6.ArtelLastCollect = -1000.0; }
	if (U6.ArtelReport.Num() > 8) { U6.ArtelReport.SetNum(8); }
	if (!U6.bArtelHired && (U6.ArtelNets.Num() > 0 || U6.ArtelStock.Num() > 0)) { U6.bArtelHired = true; }
}

void UFVSaveGame::RefreshCityTasks()
{
	using namespace FVSGCity;
	if (U6.CityTasksDay == Day) { return; }
	U6.CityTasks.RemoveAll([](const FFVCityTask& T) { return T.State != uint8(EFVOrderState::Taken); });
	U6.CityTasksDay = Day;

	FRandomStream Rng(int32(Fmix(Mix(0xC171Bu, uint32(Day) * 2654435761u))));
	const int32 Level = GetLevel();

	// ---- good fish (Р34): gold / iridium, classic fish too; expert fish of opened expert places; never legends
	TArray<const FFVFishDef*> FishPool;
	TArray<float> FishW;
	for (const FFVLocationDef& L : FVDB::Locations())
	{
		if (!IsLocationUnlocked(L.Id)) { continue; }
		for (const FFVFishDef* F : FVDB::FishAt(L.Id))
		{
			if (!F || F->bLegendary || F->MinLevel > Level || FishPool.Contains(F)) { continue; }
			if (FVMill::IsOmutOnly(*F) && !IsOmutOpen()) { continue; }   // §18: the sterlet lives only in the Omut
			if (F->bExpert)
			{
				const FFVExpertFishDef* E = FVDBU6::FindExpertFish(F->Id);
				if (!E || E->Legend != EFVExpertLegend::None) { continue; }
			}
			FishPool.Add(F);
			FishW.Add(F->bExpert ? 1.5f : (F->Rarity <= EFVRarity::Uncommon ? 2.f : 1.f));
		}
	}
	// ---- kitchen & workshop (dishes, lines, floats, reels, baits)
	TArray<const FFVItemDef*> Items;
	TArray<float> ItemW;
	const int32 KitchenLevel = GetStationLevel(EFVStation::Kitchen);
	for (const FFVRecipeDef* R : FVDB::RecipesFor(EFVStation::Kitchen))
	{
		if (!R || R->StationLevel > KitchenLevel || R->MinLevel > Level) { continue; }
		if (!FVSGHome::CanObtainRecipe(*this, *R)) { continue; }   // e.g. уха without a garden, blini without the mill
		const FFVItemDef* I = FVDB::FindItem(R->OutputId);
		if (I && I->Kind == EFVItemKind::Food && I->MinLevel <= Level && !Items.Contains(I)) { Items.Add(I); ItemW.Add(1.5f); }
	}
	for (const EFVItemKind K : { EFVItemKind::Line, EFVItemKind::Float, EFVItemKind::Reel })
	{
		for (const FFVItemDef* I : FVDB::ItemsOfKind(K))
		{
			if (I && I->MinLevel <= Level && I->SellPrice > 0 && !Items.Contains(I)) { Items.Add(I); ItemW.Add(0.6f); }
		}
	}
	TArray<const FFVBaitDef*> PickBaits;
	TArray<float> BaitW;
	for (const FFVBaitDef& B : FVDB::Baits())
	{
		if (B.MinLevel <= Level && B.PackPrice > 0) { PickBaits.Add(&B); BaitW.Add(1.f); }
	}

	const TArray<FName> Npcs = TaskNpcs();
	FName ProbeFish;
	int32 ProbeFact = INDEX_NONE;
	FRandomStream Probe(1);
	const bool bFactsLeft = PickNextFact(NAME_None, Probe, ProbeFish, ProbeFact);

	for (int32 n = 0; n < OffersPerDay; ++n)
	{
		FFVCityTask T;
		float Value = 0.f;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			T = FFVCityTask();
			Value = 0.f;
			const float WFish = FishPool.Num() > 0 ? 55.f : 0.f;
			const float WItem = Items.Num() > 0 ? 30.f : 0.f;
			const float WBait = PickBaits.Num() > 0 ? 15.f : 0.f;
			const float Total = WFish + WItem + WBait;
			if (Total <= 0.f) { break; }
			const float Roll = Rng.FRandRange(0.f, Total);
			if (Roll < WFish)
			{
				const FFVFishDef* F = PickWeighted(FishPool, FishW, Rng);
				if (!F) { continue; }
				T.ItemId = F->Id;
				const bool bIridium = Level >= 12 && Rng.FRand() < 0.25f;
				T.MinQuality = uint8(bIridium ? EFVQuality::Iridium : EFVQuality::Gold);
				T.Count = (!F->bExpert && F->Rarity <= EFVRarity::Uncommon && !bIridium) ? Rng.RandRange(1, 2) : 1;
				Value = float(FVDB::FishPrice(*F, EFVQuality(T.MinQuality)) * T.Count);
				T.DurationHours = (F->bExpert || F->Rarity >= EFVRarity::Rare) ? 72 : (Rng.FRand() < 0.5f ? 36 : 48);
			}
			else if (Roll < WFish + WItem)
			{
				const FFVItemDef* I = PickWeighted(Items, ItemW, Rng);
				if (!I) { continue; }
				T.ItemId = I->Id;
				T.Count = I->Kind == EFVItemKind::Food ? Rng.RandRange(1, 2) : 1;
				Value = float(FMath::Max(10, FMath::Max(I->SellPrice, I->BuyPrice / 2)) * T.Count);
				T.DurationHours = Rng.FRand() < 0.5f ? 36 : 48;
			}
			else
			{
				const FFVBaitDef* B = PickWeighted(PickBaits, BaitW, Rng);
				if (!B) { continue; }
				T.ItemId = B->Id;
				T.Count = Rng.RandRange(4, 8);
				// the price of one bait (pack price / pack size): a task must not pay several times what the baits cost at
				// the shop (buy -> deliver -> profit, §5)
				Value = float(FMath::Max(1, B->PackPrice)) / float(FMath::Max(1, B->PackSize)) * float(T.Count);
				T.DurationHours = 36;
			}
			const FName ItemId = T.ItemId;
			const bool bDup = U6.CityTasks.ContainsByPredicate([ItemId](const FFVCityTask& E) { return E.ItemId == ItemId; });
			if (!bDup) { break; }
		}
		if (T.ItemId.IsNone()) { continue; }
		T.Reward = FMath::Max(50, FMath::RoundToInt((Value * Rng.FRandRange(1.6f, 2.1f) + 80.f) / 10.f) * 10);
		T.Deposit = FMath::Max(20, FMath::RoundToInt(float(T.Reward) * 0.25f / 5.f) * 5);
		T.bFact = bFactsLeft && (Rng.FRand() < 0.6f || (n == 0 && Rng.FRand() < 0.8f));
		T.Npc = Npcs[Rng.RandRange(0, Npcs.Num() - 1)];
		FString IdStr = FString::Printf(TEXT("ctask_%d_%d"), Day, n);
		while (U6.CityTasks.ContainsByPredicate([&IdStr](const FFVCityTask& E) { return E.Id == FName(*IdStr); })) { IdStr += TEXT("b"); }
		T.Id = FName(*IdStr);
		T.State = uint8(EFVOrderState::Offered);
		T.Deadline = 0.0;
		U6.CityTasks.Add(T);
	}
}

EFVBuyResult UFVSaveGame::TakeCityTask(FName TaskId, double Now)
{
	FFVCityTask* T = U6.CityTasks.FindByPredicate([TaskId](const FFVCityTask& E) { return E.Id == TaskId; });
	if (!T || !FMath::IsFinite(Now)) { return EFVBuyResult::Invalid; }
	if (T->State == uint8(EFVOrderState::Taken)) { return EFVBuyResult::AlreadyOwned; }
	if (T->State != uint8(EFVOrderState::Offered)) { return EFVBuyResult::Invalid; }
	if (TakenCityTasks() >= FVSGCity::MaxTaken) { return EFVBuyResult::NoRoom; }
	const int64 Deposit = FMath::Max(0, T->Deposit);
	if (!CanAfford(Deposit) || !Spend(Deposit)) { return EFVBuyResult::NotEnoughMoney; }
	T->State = uint8(EFVOrderState::Taken);
	T->Deadline = Now + double(FMath::Max(1, T->DurationHours)) * 60.0;
	return EFVBuyResult::Ok;
}

bool UFVSaveGame::CanDeliverCityTask(const FFVCityTask& T) const
{
	if (T.State != uint8(EFVOrderState::Taken) || T.ItemId.IsNone()) { return false; }
	const int32 Need = FMath::Max(1, T.Count);
	if (FVSGCity::IsTaskFish(T.ItemId))
	{
		int64 Have = 0;
		for (const FFVInvStack& S : Keepnet) { if (FVSGCity::FishAccepts(T, S)) { Have += S.Count; } }
		return Have >= Need;
	}
	if (FVDB::FindBait(T.ItemId)) { return BaitCount(T.ItemId) >= Need; }
	return CountItem(T.ItemId) >= Need;
}

EFVBuyResult UFVSaveGame::DeliverCityTask(FName TaskId, FName& OutFish, int32& OutFactIndex)
{
	OutFish = NAME_None;
	OutFactIndex = INDEX_NONE;
	const int32 Idx = U6.CityTasks.IndexOfByPredicate([TaskId](const FFVCityTask& E) { return E.Id == TaskId; });
	if (Idx == INDEX_NONE || U6.CityTasks[Idx].State != uint8(EFVOrderState::Taken)) { return EFVBuyResult::Invalid; }
	const FFVCityTask T = U6.CityTasks[Idx];
	if (!CanDeliverCityTask(T)) { return EFVBuyResult::NotEnoughMaterials; }

	const int32 Need = FMath::Max(1, T.Count);
	if (FVSGCity::IsTaskFish(T.ItemId))
	{
		// the lowest acceptable quality goes first
		int32 Left = Need;
		for (int32 Q = T.MinQuality; Q <= int32(EFVQuality::Iridium) && Left > 0; ++Q)
		{
			for (FFVInvStack& S : Keepnet)
			{
				if (Left <= 0) { break; }
				if (S.Quality != Q || !FVSGCity::FishAccepts(T, S)) { continue; }
				const int32 Take = FMath::Min(Left, S.Count);
				S.Count -= Take;
				Left -= Take;
			}
		}
		Keepnet.RemoveAll([](const FFVInvStack& S) { return S.Count <= 0; });
	}
	else
	{
		TArray<FFVIngredient> In;
		FFVIngredient& I = In.AddDefaulted_GetRef();
		I.ItemId = T.ItemId;
		I.Count = Need;
		if (!ConsumeIngredients(In)) { return EFVBuyResult::NotEnoughMaterials; }
	}
	Earn(FMath::Max(0, T.Reward), true);
	Earn(FMath::Max(0, T.Deposit), false);   // the deposit comes back

	FFVCityTask& Done = U6.CityTasks[Idx];
	Done.State = uint8(EFVOrderState::Done);
	U6.CityTasksDone = FMath::Min(U6.CityTasksDone, MAX_int32 - 1) + 1;
	if (T.bFact)
	{
		// an expert fish asked for gives a fact about itself; otherwise a surprise fish (§8.2)
		const FName Prefer = FVDBU6::Facts(T.ItemId).Num() > 0 ? T.ItemId : NAME_None;
		FRandomStream Rng(int32(FVSGCity::Fmix(FVSGCity::StrHash(T.Id) ^ (uint32(TotalFactsKnown()) * 0x9E3779B1u))));
		FName Fish;
		int32 Fact = INDEX_NONE;
		if (PickNextFact(Prefer, Rng, Fish, Fact) && LearnFact(Fish, Fact))
		{
			Done.FactFish = Fish;
			Done.FactIndex = Fact;
			OutFish = Fish;
			OutFactIndex = Fact;
		}
	}
	return EFVBuyResult::Ok;
}

TArray<FName> UFVSaveGame::ExpireCityTasks(double Now)
{
	TArray<FName> Failed;
	if (!FMath::IsFinite(Now)) { return Failed; }
	for (FFVCityTask& T : U6.CityTasks)
	{
		if (T.State == uint8(EFVOrderState::Taken) && Now >= T.Deadline)
		{
			T.State = uint8(EFVOrderState::Failed);   // the deposit is lost
			Failed.Add(T.Id);
		}
	}
	return Failed;
}

int32 UFVSaveGame::TakenCityTasks() const
{
	int32 N = 0;
	for (const FFVCityTask& T : U6.CityTasks)
	{
		if (T.State == uint8(EFVOrderState::Taken)) { ++N; }
	}
	return N;
}

// ================================================================================================
// Artel (§8.3)
// ================================================================================================
EFVBuyResult UFVSaveGame::HireArtel()
{
	if (U6.bArtelHired) { return EFVBuyResult::AlreadyOwned; }
	if (!U6.bVisitedCity) { return EFVBuyResult::LevelTooLow; }
	if (!CanAfford(FVSGCity::ArtelHirePrice) || !Spend(FVSGCity::ArtelHirePrice)) { return EFVBuyResult::NotEnoughMoney; }
	U6.bArtelHired = true;
	U6.ArtelLastCollect = U6.PlayedMinutes - FVSGCity::CollectEveryPlayed;   // the next morning already counts
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::GiveNetsToArtel(int32 BackpackStackIndex, int32 Count)
{
	if (!U6.bArtelHired) { return EFVBuyResult::NotForSale; }
	if (!Backpack.IsValidIndex(BackpackStackIndex) || Count <= 0) { return EFVBuyResult::Invalid; }
	const FFVInvStack St = Backpack[BackpackStackIndex];
	const FFVItemDef* D = FVDB::FindItem(St.ItemId);
	if (!D || D->Kind != EFVItemKind::Net) { return EFVBuyResult::Invalid; }
	const int32 N = FMath::Min(Count, St.Count);
	if (N <= 0 || !RemoveFrom(EFVContainer::Backpack, BackpackStackIndex, N)) { return EFVBuyResult::NotEnoughMaterials; }
	FVSGCity::PutUnits(U6.ArtelStock, St.ItemId, 0, N);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::TakeNetsFromArtel(int32 StockIndex, int32 Count)
{
	if (!U6.ArtelStock.IsValidIndex(StockIndex) || Count <= 0) { return EFVBuyResult::Invalid; }
	const FName Item = U6.ArtelStock[StockIndex].ItemId;
	const int32 N = FMath::Min(Count, U6.ArtelStock[StockIndex].Count);
	if (N <= 0) { return EFVBuyResult::Invalid; }
	if (!CanAddTo(EFVContainer::Backpack, Item, N) || !AddTo(EFVContainer::Backpack, Item, N)) { return EFVBuyResult::NoRoom; }
	FFVInvStack& St = U6.ArtelStock[StockIndex];
	St.Count -= N;
	if (St.Count <= 0) { U6.ArtelStock.RemoveAt(StockIndex); }
	return EFVBuyResult::Ok;
}

void UFVSaveGame::SetArtelLocation(FName LocationId, bool bOn)
{
	if (LocationId.IsNone()) { return; }
	if (bOn) { U6.ArtelLocations.AddUnique(LocationId); }
	else { U6.ArtelLocations.Remove(LocationId); }
}

int32 UFVSaveGame::ArtelDeploy()
{
	using namespace FVSGCity;
	if (!U6.bArtelHired) { return 0; }
	int32 Placed = 0;
	for (const FName& Loc : U6.ArtelLocations)
	{
		if (FVDB::LocationIndex(Loc) == INDEX_NONE || !IsLocationUnlocked(Loc)) { continue; }
		const int32 Spots = NetSpotCount(Loc);
		for (int32 Spot = 0; Spot < Spots; ++Spot)
		{
			if (U6.ArtelStock.Num() == 0) { return Placed; }
			if (FindNet(Loc, Spot)) { continue; }   // the player's own net
			if (FVMill::IsMillId(Loc) && Spot == FVMill::OmutNetSpot && !IsOmutOpen()) { continue; }   // §18: the dam first
			if (U6.ArtelNets.ContainsByPredicate([Loc, Spot](const FFVNetState& N) { return N.Location == Loc && N.Spot == Spot; })) { continue; }
			// the strongest net of the stock first
			int32 Best = INDEX_NONE;
			float BestDur = -1.f;
			for (int32 i = 0; i < U6.ArtelStock.Num(); ++i)
			{
				const FFVItemDef* D = FVDB::FindItem(U6.ArtelStock[i].ItemId);
				if (D && D->Kind == EFVItemKind::Net && U6.ArtelStock[i].Count > 0 && D->NetDurability > BestDur) { Best = i; BestDur = D->NetDurability; }
			}
			if (Best == INDEX_NONE) { U6.ArtelStock.Reset(); return Placed; }
			const FName Item = U6.ArtelStock[Best].ItemId;
			if (--U6.ArtelStock[Best].Count <= 0) { U6.ArtelStock.RemoveAt(Best); }
			FFVNetState& N = U6.ArtelNets.AddDefaulted_GetRef();
			N.Location = Loc;
			N.Spot = Spot;
			N.NetItem = Item;
			N.Durability = MaxDurOf(Item);
			N.bTorn = false;
			N.SimMinute = -1.0;   // starts fishing at the next simulation step
			N.Progress = 0.f;
			++Placed;
		}
	}
	return Placed;
}

void UFVSaveGame::SimulateArtel(double FromAbs, double ToAbs, EFVWeather W)
{
	if (!U6.bArtelHired || U6.ArtelNets.Num() == 0) { return; }
	if (!FMath::IsFinite(FromAbs) || !FMath::IsFinite(ToAbs) || ToAbs <= FromAbs) { return; }
	const EFVWeather Wx = uint8(W) < uint8(EFVWeather::Count) ? W : EFVWeather::Sunny;
	const int32 Level = GetLevel();
	for (FFVNetState& N : U6.ArtelNets)
	{
		if (N.SimMinute < 0.0) { N.SimMinute = FromAbs; }
		FVSGCity::SimulateNet(N, ToAbs, Wx, Level);
	}
}

int64 UFVSaveGame::ArtelMorningCollect(double Now, TArray<FString>& OutReport)
{
	using namespace FVSGCity;
	OutReport.Reset();
	if (!U6.bArtelHired || !FMath::IsFinite(Now) || Now < 0.0) { return 0; }
	const int32 DayKey = int32(FMath::FloorToDouble(Now / 1440.0)) + 1;
	const double Clock = Now - double(DayKey - 1) * 1440.0;
	if (Clock < MorningMinute || U6.ArtelLastDay == DayKey) { return 0; }
	if (U6.PlayedMinutes - U6.ArtelLastCollect < CollectEveryPlayed) { return 0; }   // slept too fast: the catch waits
	if (U6.ArtelNets.Num() == 0 && U6.ArtelStock.Num() == 0) { return 0; }

	U6.ArtelLastDay = DayKey;
	U6.ArtelLastCollect = U6.PlayedMinutes;

	// ---- sell every catch at the shop price, minus the commission
	const FName Trader = FVDB::DefaultTraderId();
	static const FName ScrapId(TEXT("mat_scrap"));
	int64 Gross = 0;
	int32 Fish = 0;
	TArray<FName> TornAt;
	for (FFVNetState& N : U6.ArtelNets)
	{
		for (const FFVInvStack& S : N.Catch)
		{
			if (S.Count <= 0) { continue; }
			FName Id = S.ItemId;
			if (const FFVJunkDef* J = FVDB::FindJunk(Id))
			{
				if (J->Style == EFVJunkStyle::Scrap && FVDB::FindItem(ScrapId)) { Id = ScrapId; }
			}
			const int32 Unit = FVDB::TraderSellPrice(Trader, Id, EFVQuality(FMath::Min<int32>(S.Quality, int32(EFVQuality::Iridium))), 0);
			Gross += int64(FMath::Max(0, Unit)) * S.Count;
			if (FVDB::IsFishId(S.ItemId)) { Fish += S.Count; }
		}
		N.Catch.Reset();
		if (N.bTorn) { TornAt.AddUnique(N.Location); }
	}
	const int64 Fee = int64(FMath::RoundToDouble(double(Gross) * double(Commission)));
	const int64 Coins = FMath::Max<int64>(0, Gross - Fee);
	if (Coins > 0) { Earn(Coins, true); }
	if (Fish > 0 || Coins > 0)
	{
		OutReport.Add(FString::Printf(TEXT("Артельщик продал %d %s: +%s %s"), Fish, *Plural(Fish, TEXT("рыбу"), TEXT("рыбы"), TEXT("рыб")),
			*Thousands(Coins), *Plural(Coins, TEXT("монета"), TEXT("монеты"), TEXT("монет"))));
	}
	for (const FName& L : TornAt)
	{
		const FFVLocationDef* Def = FVDB::FindLocation(L);
		OutReport.Add(FString::Printf(TEXT("Артель: порвалась сеть — %s"), Def ? *Def->Name : *L.ToString()));
	}

	// ---- intact nets on places the player unticked come back to the stock; free spots get nets
	for (int32 i = U6.ArtelNets.Num() - 1; i >= 0; --i)
	{
		const FFVNetState& N = U6.ArtelNets[i];
		if (!U6.ArtelLocations.Contains(N.Location) && !N.bTorn && N.Durability >= 0.95f * MaxDurOf(N.NetItem))
		{
			PutUnits(U6.ArtelStock, N.NetItem, 0, 1);
			U6.ArtelNets.RemoveAt(i);
		}
	}
	const int32 Placed = ArtelDeploy();
	if (Placed > 0)
	{
		OutReport.Add(FString::Printf(TEXT("Артельщик поставил %d %s"), Placed, *Plural(Placed, TEXT("сеть"), TEXT("сети"), TEXT("сетей"))));
	}
	return Coins;
}

TArray<FFVIngredient> UFVSaveGame::ArtelRepairCost() const
{
	int32 Units = 0;
	for (const FFVNetState& N : U6.ArtelNets) { Units += FVSGCity::RepairUnits(N); }
	TArray<FFVIngredient> Cost;
	if (Units <= 0) { return Cost; }
	for (const FFVIngredient& I : NetRepairCost())
	{
		FFVIngredient& C = Cost.Add_GetRef(I);
		C.Count = FMath::Max(1, I.Count) * Units;
	}
	return Cost;
}

EFVBuyResult UFVSaveGame::ArtelRepairAll()
{
	const TArray<FFVIngredient> Cost = ArtelRepairCost();
	if (Cost.Num() == 0) { return EFVBuyResult::AlreadyOwned; }   // nothing to repair
	if (!HasIngredients(Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	if (!ConsumeIngredients(Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	for (FFVNetState& N : U6.ArtelNets)
	{
		if (!FVSGCity::IsDamaged(N)) { continue; }
		N.Durability = FVSGCity::MaxDurOf(N.NetItem);
		N.bTorn = false;
	}
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::ArtelDiscardTorn()
{
	const int32 Before = U6.ArtelNets.Num();
	U6.ArtelNets.RemoveAll([](const FFVNetState& N) { return N.bTorn; });   // their catch goes with them
	const int32 Removed = Before - U6.ArtelNets.Num();
	if (Removed > 0) { ArtelDeploy(); }
	return Removed;
}
