// Update 2 profile logic (Data module): containers & transfers, trading, stations, crafting, rod tackle, food buffs,
// passive systems (nets, smokehouse), worm farm, aquarium, trophies, gathering & digging marks, the order board and
// house decor. Every mutator either succeeds completely or changes nothing.

#include "FVSaveGame.h"
#include "FVDatabase.h"
#include "Misc/Crc.h"

namespace FVSGHome
{
	constexpr int32 MaxUnits = 9999;          // unit containers (fish): stack size limit
	constexpr int32 JunkMaxStack = 99;        // junk has no FFVItemDef: 99 per backpack slot
	constexpr int32 MaxTakenOrders = 3;
	constexpr int32 OffersPerDay = 3;
	constexpr float SmokeMinutes = 360.f;
	constexpr float MaxSmokeFuel = 720.f;
	constexpr double MaxSimMinutes = 30.0 * 1440.0;   // a single SimulatePassive never simulates more than 30 days

	const FName& BranchId() { static const FName Id(TEXT("res_branch")); return Id; }
	const FName& WormId() { static const FName Id(TEXT("bait_worm")); return Id; }
	const FName& CrawlerId() { static const FName Id(TEXT("bait_nightcrawler")); return Id; }

	int32 SatAdd32(int32 A, int64 B, int32 Max = MAX_int32)
	{
		return int32(FMath::Clamp<int64>(int64(A) + B, 0, Max));
	}

	bool IsSlotContainer(EFVContainer C) { return C == EFVContainer::Backpack || C == EFVContainer::Chest; }
	bool IsValidContainer(EFVContainer C) { return uint8(C) < uint8(EFVContainer::Count); }

	int32 MaxStackOf(FName Id)
	{
		if (const FFVItemDef* I = FVDB::FindItem(Id)) { return FMath::Max(1, I->MaxStack); }
		if (FVDB::FindJunk(Id)) { return JunkMaxStack; }
		return MaxUnits;
	}

	/** Fish and dishes keep their quality; junk and other items are always Normal. */
	uint8 NormQ(FName Id, uint8 Q)
	{
		const uint8 Max = uint8(EFVQuality::Iridium);
		if (FVDB::FindFish(Id)) { return FMath::Min(Q, Max); }
		const FFVItemDef* I = FVDB::FindItem(Id);
		return (I && I->Kind == EFVItemKind::Food) ? FMath::Min(Q, Max) : uint8(0);
	}

	uint8 NormF(FName Id, uint8 F) { return FVDB::FindFish(Id) ? uint8(F & FVStackFlags::Smoked) : uint8(0); }

	bool IsSmoked(const FFVInvStack& S) { return (S.Flags & FVStackFlags::Smoked) != 0; }

	EFVQuality QualityOf(uint8 Q) { return EFVQuality(FMath::Min<uint8>(Q, uint8(EFVQuality::Iridium))); }

	int64 Units(const TArray<FFVInvStack>& Arr)
	{
		int64 N = 0;
		for (const FFVInvStack& St : Arr) { N += FMath::Max(0, St.Count); }
		return N;
	}

	int32 UsedSlots(const TArray<FFVInvStack>& Arr)
	{
		int32 N = 0;
		for (const FFVInvStack& St : Arr) { if (St.Count > 0) { ++N; } }
		return N;
	}

	/** Units of (Id, Q, F) that still fit into a slot container with CapSlots slots. */
	int64 SlotRoom(const TArray<FFVInvStack>& Arr, int32 CapSlots, FName Id, uint8 Q, uint8 F)
	{
		const int32 MS = MaxStackOf(Id);
		int64 Room = 0;
		for (const FFVInvStack& St : Arr)
		{
			if (St.Count > 0 && St.ItemId == Id && St.Quality == Q && St.Flags == F) { Room += FMath::Max(0, MS - St.Count); }
		}
		Room += int64(FMath::Max(0, CapSlots - UsedSlots(Arr))) * MS;
		return Room;
	}

	/** Slot container add (room already checked): fills equal stacks, then opens new ones. */
	void PutSlots(TArray<FFVInvStack>& Arr, FName Id, uint8 Q, uint8 F, int32 Count)
	{
		const int32 MS = MaxStackOf(Id);
		for (FFVInvStack& St : Arr)
		{
			if (Count <= 0) { return; }
			if (St.ItemId != Id || St.Quality != Q || St.Flags != F || St.Count >= MS) { continue; }
			const int32 Add = FMath::Min(Count, MS - St.Count);
			St.Count += Add;
			Count -= Add;
		}
		for (int32 Guard = 0; Count > 0 && Guard < 1000; ++Guard)
		{
			FFVInvStack& N = Arr.AddDefaulted_GetRef();
			N.ItemId = Id;
			N.Quality = Q;
			N.Flags = F;
			N.Count = FMath::Min(Count, MS);
			Count -= N.Count;
		}
	}

	/** Unit container add (room already checked): one stack per identity. */
	void PutUnits(TArray<FFVInvStack>& Arr, FName Id, uint8 Q, uint8 F, int32 Count)
	{
		for (FFVInvStack& St : Arr)
		{
			if (Count <= 0) { return; }
			if (St.ItemId != Id || St.Quality != Q || St.Flags != F || St.Count >= MaxUnits) { continue; }
			const int32 Add = FMath::Min(Count, MaxUnits - St.Count);
			St.Count += Add;
			Count -= Add;
		}
		for (int32 Guard = 0; Count > 0 && Guard < 1000; ++Guard)
		{
			FFVInvStack& N = Arr.AddDefaulted_GetRef();
			N.ItemId = Id;
			N.Quality = Q;
			N.Flags = F;
			N.Count = FMath::Min(Count, MaxUnits);
			Count -= N.Count;
		}
	}

	/** Removes up to Count units from the stacks matching Pred, lowest Key first; empty stacks are dropped. */
	template <typename TPred, typename TKey>
	int32 TakeUnits(TArray<FFVInvStack>& Arr, int32 Count, TPred Pred, TKey Key)
	{
		if (Count <= 0) { return 0; }
		TArray<int32, TInlineAllocator<16>> Idx;
		for (int32 i = 0; i < Arr.Num(); ++i)
		{
			if (Arr[i].Count > 0 && Pred(Arr[i])) { Idx.Add(i); }
		}
		Idx.StableSort([&Arr, &Key](int32 A, int32 B) { return Key(Arr[A]) < Key(Arr[B]); });
		int32 Left = Count;
		for (const int32 i : Idx)
		{
			if (Left <= 0) { break; }
			const int32 Take = FMath::Min(Left, Arr[i].Count);
			Arr[i].Count -= Take;
			Left -= Take;
		}
		Arr.RemoveAll([](const FFVInvStack& S) { return S.Count <= 0; });
		return Count - Left;
	}

	int64 FishSortKey(const FFVInvStack& S)
	{
		return int64(FVDB::ItemPriceEx(S.ItemId, QualityOf(S.Quality), S.Flags)) * 16 + S.Quality;
	}

	int64 LowQualityFirst(const FFVInvStack& S)
	{
		return (int64(S.Quality) << 32) + int64(FMath::Max(0, S.Count));   // lowest quality, then the smallest stack (frees slots)
	}

	/** Does the fish token Token ("any_fish" or, update 5, a rarity token — FVDB::FishTokenRarity) accept this keepnet
	    stack? Smoked fish never; a rarity token wants a fish of exactly its rarity. */
	bool FishTokenAccepts(FName Token, const FFVInvStack& S)
	{
		if (IsSmoked(S)) { return false; }
		const FFVFishDef* F = FVDB::FindFish(S.ItemId);
		if (!F) { return false; }
		EFVRarity Want = EFVRarity::Common;
		return !FVDB::FishTokenRarity(Token, Want) || F->Rarity == Want;
	}

	/** Order in which ApplyIngredients serves the inputs: exact ids first, then the rarity tokens, "any_fish" last —
	    a looser fish token never eats the fish that a stricter input of the same recipe needs. */
	int32 IngredientPass(FName Id)
	{
		if (Id == FVDB::AnyFishToken()) { return 2; }
		return FVDB::IsAnyFishToken(Id) ? 1 : 0;
	}

	/** Takes a list of ingredients out of the given state (keepnet / backpack / bait map). Returns false when
	    something was missing (the state is then partially changed: use it on copies or after HasIngredients). */
	bool ApplyIngredients(TArray<FFVInvStack>& Keep, TArray<FFVInvStack>& Pack, TMap<FName, int32>& BaitMap,
		const TArray<FFVIngredient>& Inputs, int32 Times)
	{
		bool bOk = true;
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			for (const FFVIngredient& In : Inputs)
			{
				if (IngredientPass(In.ItemId) != Pass) { continue; }
				const int32 Need = int32(FMath::Clamp<int64>(int64(FMath::Max(0, In.Count)) * FMath::Max(0, Times), 0, MAX_int32));
				if (Need <= 0) { continue; }
				int32 Got = 0;
				if (FVDB::IsAnyFishToken(In.ItemId))
				{
					// the cheapest accepted fish first (smoked fish never)
					const FName Token = In.ItemId;
					Got = TakeUnits(Keep, Need,
						[Token](const FFVInvStack& S) { return FishTokenAccepts(Token, S); },
						[](const FFVInvStack& S) { return FishSortKey(S); });
				}
				else if (FVDB::FindBait(In.ItemId))
				{
					int32* C = BaitMap.Find(In.ItemId);
					const int32 Have = C ? FMath::Max(0, *C) : 0;
					Got = FMath::Min(Have, Need);
					if (C)
					{
						*C = Have - Got;
						if (*C <= 0) { BaitMap.Remove(In.ItemId); }
					}
				}
				else if (FVDB::FindFish(In.ItemId))
				{
					const FName Id = In.ItemId;
					Got = TakeUnits(Keep, Need,
						[Id](const FFVInvStack& S) { return S.ItemId == Id && !IsSmoked(S); },
						[](const FFVInvStack& S) { return int64(S.Quality); });
				}
				else
				{
					const FName Id = In.ItemId;
					Got = TakeUnits(Pack, Need, [Id](const FFVInvStack& S) { return S.ItemId == Id; }, &LowQualityFirst);
				}
				bOk &= (Got == Need);
			}
		}
		return bOk;
	}

	/** Units that still fit into container C for this (normalised) identity. */
	int64 RoomFor(const UFVSaveGame& P, EFVContainer C, FName Id, uint8 Q, uint8 F)
	{
		if (!IsValidContainer(C) || !P.ContainerAccepts(C, Id)) { return 0; }
		if (C == EFVContainer::Aquarium && (F & FVStackFlags::Smoked) != 0) { return 0; }   // live fish only
		const int32 Cap = P.ContainerCapacity(C);
		if (IsSlotContainer(C)) { return SlotRoom(P.GetContainer(C), Cap, Id, Q, F); }
		return FMath::Max<int64>(0, int64(Cap) - Units(P.GetContainer(C)));
	}

	// ---------------------------------------------------------------- deterministic hashing (never GetTypeHash(FName))
	uint32 Mix(uint32 H, uint32 V)
	{
		H ^= V + 0x9E3779B9u + (H << 6) + (H >> 2);
		return H;
	}
	uint32 Fmix(uint32 H)
	{
		H ^= H >> 16; H *= 0x85EBCA6Bu; H ^= H >> 13; H *= 0xC2B2AE35u; H ^= H >> 16;
		return H;
	}
	uint32 LocHash(FName Loc)
	{
		const FString S = Loc.ToString().ToLower();
		return FCrc::StrCrc32(*S);
	}
	int32 NetSeed(FName Loc, int32 Spot, int64 Minute, uint32 Salt)
	{
		uint32 H = LocHash(Loc);
		H = Mix(H, uint32(Spot) * 0x9E3779B1u + 7u);
		H = Mix(H, uint32(uint64(Minute) & 0xFFFFFFFFull));
		H = Mix(H, uint32(uint64(Minute) >> 32));
		H = Mix(H, Salt);
		return int32(Fmix(H));
	}

	/** Net spots per fishing location (the world places the same number of posts). Update 6: the right pier spot (1)
	    became the boat (§7) — the ocean keeps spot 0; no nets in the expert zones (Р35). Keep in sync with
	    FVSaveGameFix::NetSpotCount (FVSaveGame.cpp) and the scene. */
	int32 NetSpotCount(FName Loc)
	{
		if (FVDBU6::IsExpertLocation(Loc) || FVDBU6::IsForestId(Loc)) { return 0; }
		if (FVMill::IsMillId(Loc)) { return FVMill::NetSpots; }   // §18: 3 on the pond + 1 in the Omut
		const FString L = Loc.ToString().ToLower();
		if (L == TEXT("pond") || L == TEXT("river") || L == TEXT("swamp")) { return 2; }
		if (L == TEXT("mountain") || L == TEXT("ocean")) { return 1; }
		return 0;
	}

	/** Update 6 (§5.1): caught scrap junk (junk_scrap) is handed out as the material mat_scrap. */
	FName CatchGiveId(FName Id)
	{
		const FFVJunkDef* J = FVDB::FindJunk(Id);
		if (J && J->Style == EFVJunkStyle::Scrap)
		{
			static const FName Scrap(TEXT("mat_scrap"));
			if (FVDB::FindItem(Scrap)) { return Scrap; }
		}
		return Id;
	}

	/** Update 6 (Р42): garden produce (veg_*) exists only once the garden is built. */
	bool IsGardenProduce(FName Id)
	{
		return Id.ToString().StartsWith(TEXT("veg_"));
	}

	bool RecipeNeedsGarden(const FFVRecipeDef& R)
	{
		for (const FFVIngredient& I : R.Inputs)
		{
			if (IsGardenProduce(I.ItemId)) { return true; }
		}
		return false;
	}

	/** Update 6: can the player get this ingredient at all yet? Garden produce needs the garden (carrot) and the seller of
	 *  its seeds (City gardener / the Miller); flour needs a working mill; a named fish needs its location opened. */
	bool CanObtainIngredient(const UFVSaveGame& P, FName Id)
	{
		static const FName Carrot(TEXT("veg_carrot"));
		if (Id == FVMill::FlourId()) { return P.GetYardLevel(EFVYardBuilding::Garden) > 0 && P.IsMillWorking(); }
		if (IsGardenProduce(Id))
		{
			if (P.GetYardLevel(EFVYardBuilding::Garden) <= 0) { return false; }
			if (Id == Carrot) { return true; }
			if (Id == FVMill::RyeId()) { return P.IsLocationUnlocked(FVMill::MillId()); }
			return P.U6.bVisitedCity;   // potato, onion, dill, pumpkin: seeds from the City gardener
		}
		if (const FFVFishDef* F = FVDB::FindFish(Id)) { return P.IsLocationUnlocked(F->Location); }
		return true;
	}

	/** Every input of the recipe can be obtained now (orders never ask for a dish that cannot be cooked yet). */
	bool CanObtainRecipe(const UFVSaveGame& P, const FFVRecipeDef& R)
	{
		for (const FFVIngredient& I : R.Inputs)
		{
			if (!CanObtainIngredient(P, I.ItemId)) { return false; }
		}
		return true;
	}

	EFVDayPhase PhaseOfHour(int32 Hour)
	{
		const int32 H = ((Hour % 24) + 24) % 24;
		if (H >= 6 && H < 12) { return EFVDayPhase::Morning; }
		if (H >= 12 && H < 18) { return EFVDayPhase::Day; }
		if (H >= 18 && H < 22) { return EFVDayPhase::Evening; }
		return EFVDayPhase::Night;
	}

	/** One unit caught by a net at an absolute minute. */
	void NetCatchOne(FFVNetState& N, int64 Minute, EFVDayPhase Phase, EFVWeather Weather, int32 Level, int32& OutFish)
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
		FRandomStream Rng(NetSeed(N.Location, N.Spot, Minute, 0xF1511u));
		const FFVFishDef* F = nullptr;
		const FFVJunkDef* J = nullptr;
		FVDB::PickCatch(Ctx, Rng, F, J);
		if (F)
		{
			const float Frac = FVDB::RollSizeFrac(Level, false, 10.f, nullptr, Rng) * 0.85f;
			EFVQuality Q = FVDB::RollQuality(Frac, false, Rng);
			if (uint8(Q) > uint8(EFVQuality::Gold)) { Q = EFVQuality::Gold; }
			PutUnits(N.Catch, F->Id, uint8(Q), 0, 1);
			N.Durability -= FVDB::NetWear(*F, Q);
			++OutFish;
			if (N.Durability <= 0.f)
			{
				N.Durability = 0.f;
				N.bTorn = true;
			}
		}
		else if (J)
		{
			PutUnits(N.Catch, J->Id, 0, 0, 1);
		}
	}

	/** Advances one net to ToAbs: hour-sized chunks (phase & the hourly tear roll), catches when Progress crosses 1.
	    Cheap for per-frame calls (one chunk) and bounded for long sleeps. */
	void SimulateNet(FFVNetState& N, double ToAbs, EFVWeather Weather, int32 Level, int32& OutFish)
	{
		const FFVItemDef* Def = FVDB::FindItem(N.NetItem);
		if (!FMath::IsFinite(N.SimMinute)) { N.SimMinute = ToAbs; }
		if (N.SimMinute >= ToAbs) { return; }
		if (!Def || Def->Kind != EFVItemKind::Net)
		{
			N.SimMinute = ToAbs;
			return;
		}
		const int64 Cap = FMath::Max(1, Def->NetCapacity);
		const double MinPerFish = FMath::Max(1.0, double(Def->NetMinutesPerFish));
		const float MaxDur = FMath::Max(1.f, Def->NetDurability);
		if (!FMath::IsFinite(N.Progress) || N.Progress < 0.f) { N.Progress = 0.f; }

		double T = FMath::Max(N.SimMinute, ToAbs - MaxSimMinutes);
		for (int32 Guard = 0; T < ToAbs && Guard < 1000; ++Guard)
		{
			if (N.bTorn || Units(N.Catch) >= Cap) { break; }   // torn or full: time passes, nothing is caught
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
				NetCatchOne(N, int64(FMath::FloorToDouble(Cur)), Phase, Weather, Level, OutFish);
			}
			// every whole simulated hour a worn net (< 25%) may tear
			if (SegEnd >= HourEnd && !N.bTorn && N.Durability < 0.25f * MaxDur)
			{
				FRandomStream TearRng(NetSeed(N.Location, N.Spot, int64(HourStart), 0x7EA2u));
				if (TearRng.FRand() < 0.04f) { N.bTorn = true; }
			}
			T = SegEnd;
		}
		N.SimMinute = ToAbs;
	}

	/** Russian plural: 1 червь, 2 червя, 5 червей. */
	FString Plural(int32 N, const TCHAR* One, const TCHAR* Few, const TCHAR* Many)
	{
		const int32 A = FMath::Abs(N) % 100;
		const int32 B = A % 10;
		if (A > 10 && A < 20) { return Many; }
		if (B == 1) { return One; }
		if (B >= 2 && B <= 4) { return Few; }
		return Many;
	}

	template <typename T>
	const T* PickWeighted(const TArray<const T*>& Pool, const TArray<float>& W, FRandomStream& Rng)
	{
		float Total = 0.f;
		for (const float X : W) { Total += FMath::Max(0.f, X); }
		if (Pool.Num() == 0 || Total <= 0.f) { return Pool.Num() > 0 ? Pool[0] : nullptr; }
		float Roll = Rng.FRandRange(0.f, Total);
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			Roll -= FMath::Max(0.f, W[i]);
			if (Roll <= 0.f) { return Pool[i]; }
		}
		return Pool.Last();
	}
}

// ================================================================================================
// Containers
// ================================================================================================
TArray<FFVInvStack>& UFVSaveGame::GetContainer(EFVContainer C)
{
	switch (C)
	{
	case EFVContainer::Keepnet:   return Keepnet;
	case EFVContainer::Backpack:  return Backpack;
	case EFVContainer::Chest:     return Chest;
	case EFVContainer::IceCellar: return IceCellar;
	case EFVContainer::Aquarium:  return Aquarium;
	default: break;
	}
	static TArray<FFVInvStack> Dummy;   // invalid container: an always-empty scratch array
	Dummy.Reset();
	return Dummy;
}

const TArray<FFVInvStack>& UFVSaveGame::GetContainer(EFVContainer C) const
{
	switch (C)
	{
	case EFVContainer::Keepnet:   return Keepnet;
	case EFVContainer::Backpack:  return Backpack;
	case EFVContainer::Chest:     return Chest;
	case EFVContainer::IceCellar: return IceCellar;
	case EFVContainer::Aquarium:  return Aquarium;
	default: break;
	}
	static const TArray<FFVInvStack> Empty;
	return Empty;
}

int32 UFVSaveGame::ContainerCapacity(EFVContainer C) const
{
	switch (C)
	{
	case EFVContainer::Keepnet:
		return KeepnetCapacity();
	case EFVContainer::Backpack:
	{
		const TArray<FFVBackpackLevel>& L = FVDB::BackpackLevels();
		return L.Num() > 0 ? FMath::Max(1, L[FMath::Clamp(BackpackLevel, 0, L.Num() - 1)].Slots) : 16;
	}
	case EFVContainer::Chest:     return StationCapacity(EFVStation::Chest);
	case EFVContainer::IceCellar: return StationCapacity(EFVStation::IceCellar);
	case EFVContainer::Aquarium:  return StationCapacity(EFVStation::Aquarium);
	default:                      return 0;
	}
}

int32 UFVSaveGame::ContainerUsed(EFVContainer C) const
{
	if (!FVSGHome::IsValidContainer(C)) { return 0; }
	const TArray<FFVInvStack>& Arr = GetContainer(C);
	if (FVSGHome::IsSlotContainer(C)) { return FVSGHome::UsedSlots(Arr); }
	return int32(FMath::Min<int64>(FVSGHome::Units(Arr), MAX_int32));
}

bool UFVSaveGame::ContainerAccepts(EFVContainer C, FName ItemId) const
{
	switch (C)
	{
	case EFVContainer::Keepnet:
	case EFVContainer::IceCellar:
	case EFVContainer::Aquarium:
		return FVDB::IsFishId(ItemId);
	case EFVContainer::Backpack:
	case EFVContainer::Chest:
		return FVDB::IsJunkId(ItemId) || FVDB::IsItemId(ItemId);
	default:
		return false;
	}
}

bool UFVSaveGame::CanAddTo(EFVContainer C, FName Id, int32 Count, uint8 Q, uint8 Flags) const
{
	if (Count <= 0 || Id.IsNone()) { return false; }
	return FVSGHome::RoomFor(*this, C, Id, FVSGHome::NormQ(Id, Q), FVSGHome::NormF(Id, Flags)) >= Count;
}

bool UFVSaveGame::AddTo(EFVContainer C, FName Id, int32 Count, uint8 Q, uint8 Flags)
{
	if (!CanAddTo(C, Id, Count, Q, Flags)) { return false; }
	const uint8 NQ = FVSGHome::NormQ(Id, Q);
	const uint8 NF = FVSGHome::NormF(Id, Flags);
	TArray<FFVInvStack>& Arr = GetContainer(C);
	if (FVSGHome::IsSlotContainer(C))
	{
		FVSGHome::PutSlots(Arr, Id, NQ, NF, Count);
	}
	else if (C == EFVContainer::Aquarium)
	{
		for (int32 i = 0; i < Count; ++i)   // one live fish per stack
		{
			FFVInvStack& N = Arr.AddDefaulted_GetRef();
			N.ItemId = Id;
			N.Quality = NQ;
			N.Count = 1;
		}
	}
	else
	{
		FVSGHome::PutUnits(Arr, Id, NQ, NF, Count);
	}
	return true;
}

bool UFVSaveGame::RemoveFrom(EFVContainer C, int32 StackIndex, int32 Count)
{
	if (!FVSGHome::IsValidContainer(C) || Count <= 0) { return false; }
	TArray<FFVInvStack>& Arr = GetContainer(C);
	if (!Arr.IsValidIndex(StackIndex) || Count > Arr[StackIndex].Count) { return false; }
	Arr[StackIndex].Count -= Count;
	if (Arr[StackIndex].Count <= 0) { Arr.RemoveAt(StackIndex); }
	return true;
}

EFVBuyResult UFVSaveGame::Transfer(EFVContainer From, int32 StackIndex, int32 Count, EFVContainer To)
{
	if (From == To || !FVSGHome::IsValidContainer(From) || !FVSGHome::IsValidContainer(To)) { return EFVBuyResult::Invalid; }
	TArray<FFVInvStack>& Src = GetContainer(From);
	if (!Src.IsValidIndex(StackIndex) || Count <= 0 || Count > Src[StackIndex].Count) { return EFVBuyResult::Invalid; }
	const FFVInvStack St = Src[StackIndex];
	if (!ContainerAccepts(To, St.ItemId)) { return EFVBuyResult::Invalid; }
	if (To == EFVContainer::Aquarium && FVSGHome::IsSmoked(St)) { return EFVBuyResult::Invalid; }
	if (!CanAddTo(To, St.ItemId, Count, St.Quality, St.Flags)) { return EFVBuyResult::NoRoom; }
	RemoveFrom(From, StackIndex, Count);
	AddTo(To, St.ItemId, Count, St.Quality, St.Flags);
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::TransferAll(EFVContainer From, EFVContainer To)
{
	if (From == To || !FVSGHome::IsValidContainer(From) || !FVSGHome::IsValidContainer(To)) { return 0; }
	TArray<FFVInvStack>& Src = GetContainer(From);
	int32 Moved = 0;
	for (int32 i = 0; i < Src.Num();)
	{
		const FFVInvStack St = Src[i];
		const int64 Room = FVSGHome::RoomFor(*this, To, St.ItemId, FVSGHome::NormQ(St.ItemId, St.Quality), FVSGHome::NormF(St.ItemId, St.Flags));
		const int32 N = int32(FMath::Min<int64>(FMath::Max(0, St.Count), Room));
		if (N > 0 && AddTo(To, St.ItemId, N, St.Quality, St.Flags))
		{
			Src[i].Count -= N;
			Moved += N;
		}
		if (Src[i].Count <= 0) { Src.RemoveAt(i); }
		else { ++i; }
	}
	return Moved;
}

int32 UFVSaveGame::CountItem(FName ItemId) const
{
	if (ItemId.IsNone()) { return 0; }
	int64 N = 0;
	for (const FFVInvStack& St : Backpack)
	{
		if (St.ItemId == ItemId) { N += FMath::Max(0, St.Count); }
	}
	return int32(FMath::Min<int64>(N, MAX_int32));
}

int32 UFVSaveGame::CountIngredient(FName ItemId) const
{
	if (ItemId.IsNone()) { return 0; }
	const bool bAnyFish = FVDB::IsAnyFishToken(ItemId);   // "any_fish" or a rarity token (update 5)
	if (bAnyFish || FVDB::IsFishId(ItemId))
	{
		int64 N = 0;
		for (const FFVInvStack& St : Keepnet)
		{
			if (FVSGHome::IsSmoked(St)) { continue; }
			if (bAnyFish ? FVSGHome::FishTokenAccepts(ItemId, St) : St.ItemId == ItemId) { N += FMath::Max(0, St.Count); }
		}
		return int32(FMath::Min<int64>(N, MAX_int32));
	}
	if (FVDB::IsBaitId(ItemId)) { return BaitCount(ItemId); }
	if (FVDB::FindRod(ItemId)) { return OwnsRod(ItemId) ? 1 : 0; }   // 0.6.2: перековка (a rod input counts while owned)
	return CountItem(ItemId);
}

bool UFVSaveGame::HasIngredients(const TArray<FFVIngredient>& Inputs, int32 Times) const
{
	if (Times <= 0) { return false; }
	TMap<FName, int64> Need;   // the same id may appear twice
	for (const FFVIngredient& In : Inputs)
	{
		if (In.ItemId.IsNone() || In.Count <= 0) { continue; }
		Need.FindOrAdd(In.ItemId) += int64(In.Count) * Times;
	}
	TArray<FFVIngredient> FishInputs;
	for (const TPair<FName, int64>& P : Need)
	{
		if (int64(CountIngredient(P.Key)) < P.Value) { return false; }
		if (FVDB::IsAnyFishToken(P.Key) || FVDB::IsFishId(P.Key)) { FishInputs.Add({ P.Key, int32(P.Value) }); }   // <= a count: fits int32
	}
	// Fish inputs may share fish ("any_fish" also takes the rare fish that "any_fish_rare" wants): with more than one,
	// the per-id counts are not enough — take them out of a copy of the keepnet in ApplyIngredients' order.
	if (FishInputs.Num() > 1)
	{
		TArray<FFVInvStack> Keep = Keepnet;
		TArray<FFVInvStack> NoPack;
		TMap<FName, int32> NoBaits;
		return FVSGHome::ApplyIngredients(Keep, NoPack, NoBaits, FishInputs, 1);
	}
	return true;
}

bool UFVSaveGame::ConsumeIngredients(const TArray<FFVIngredient>& Inputs, int32 Times)
{
	if (!HasIngredients(Inputs, Times)) { return false; }
	// 0.6.2 (Р83): rod inputs are not stored in a container — they are given up (перековка)
	TArray<FFVIngredient> Stored;
	TArray<FName> Rods;
	for (const FFVIngredient& In : Inputs)
	{
		if (FVDB::FindRod(In.ItemId)) { Rods.AddUnique(In.ItemId); }
		else { Stored.Add(In); }
	}
	FVSGHome::ApplyIngredients(Keepnet, Backpack, Baits, Stored, Times);
	for (const FName& Rod : Rods) { GiveUpRod(Rod); }
	if (!EquippedBait.IsNone() && BaitCount(EquippedBait) <= 0) { EquippedBait = NAME_None; }
	return true;
}

bool UFVSaveGame::GiveItem(FName Id, int32 Count, uint8 Q, uint8 Flags)
{
	if (Count <= 0 || Id.IsNone()) { return false; }
	if (FVDB::IsFishId(Id)) { return AddToKeepnetEx(Id, FVSGHome::QualityOf(Q), Count, Flags); }
	if (FVDB::IsBaitId(Id))
	{
		AddBait(Id, Count);
		return true;
	}
	if (FVDB::IsJunkId(Id) || FVDB::IsItemId(Id)) { return AddTo(EFVContainer::Backpack, Id, Count, Q, Flags); }
	return false;
}

bool UFVSaveGame::AddToKeepnetEx(FName FishId, EFVQuality Q, int32 Count, uint8 Flags)
{
	if (!FVDB::IsFishId(FishId)) { return false; }
	return AddTo(EFVContainer::Keepnet, FishId, Count, uint8(Q), Flags);
}

EFVBuyResult UFVSaveGame::UpgradeBackpack()
{
	const TArray<FFVBackpackLevel>& Levels = FVDB::BackpackLevels();
	const int32 Next = FMath::Max(0, BackpackLevel) + 1;
	if (!Levels.IsValidIndex(Next)) { return EFVBuyResult::MaxLevel; }
	const int64 Price = FMath::Max(0, Levels[Next].Price);
	if (!CanAfford(Price) || !Spend(Price)) { return EFVBuyResult::NotEnoughMoney; }
	BackpackLevel = Next;
	UE_LOG(LogFV, Log, TEXT("FV: backpack upgraded to %d slots"), Levels[Next].Slots);
	return EFVBuyResult::Ok;
}

// ================================================================================================
// Trading
// ================================================================================================
int64 UFVSaveGame::SellFrom(EFVContainer C, int32 StackIndex, int32 Count, FName TraderId)
{
	if (!FVSGHome::IsValidContainer(C) || C == EFVContainer::Aquarium || Count <= 0) { return 0; }
	TArray<FFVInvStack>& Arr = GetContainer(C);
	if (!Arr.IsValidIndex(StackIndex)) { return 0; }
	FFVInvStack& St = Arr[StackIndex];
	const int32 N = FMath::Min(Count, St.Count);
	if (N <= 0) { return 0; }
	const int32 Unit = FVDB::TraderSellPrice(TraderId, St.ItemId, FVSGHome::QualityOf(St.Quality), St.Flags);
	if (Unit <= 0) { return 0; }
	const int64 Coins = int64(Unit) * N;
	St.Count -= N;
	if (St.Count <= 0) { Arr.RemoveAt(StackIndex); }
	Earn(Coins, true);
	return Coins;
}

int64 UFVSaveGame::SellAllFish(FName TraderId)
{
	int64 Total = 0;
	for (int32 i = Keepnet.Num() - 1; i >= 0; --i)
	{
		const FFVInvStack& St = Keepnet[i];
		const int32 Unit = St.Count > 0 ? FVDB::TraderSellPrice(TraderId, St.ItemId, FVSGHome::QualityOf(St.Quality), St.Flags) : 0;
		if (Unit <= 0 && St.Count > 0) { continue; }   // unsellable: stays
		Total += int64(Unit) * FMath::Max(0, St.Count);
		Keepnet.RemoveAt(i);
	}
	Earn(Total, true);
	return Total;
}

int64 UFVSaveGame::ContainerValue(EFVContainer C, FName TraderId) const
{
	int64 Total = 0;
	for (const FFVInvStack& St : GetContainer(C))
	{
		if (St.Count <= 0) { continue; }
		Total += int64(FVDB::TraderSellPrice(TraderId, St.ItemId, FVSGHome::QualityOf(St.Quality), St.Flags)) * St.Count;
	}
	return Total;
}

EFVBuyResult UFVSaveGame::BuyItem(FName ItemId, int32 Count, FName TraderId)
{
	const FFVItemDef* I = FVDB::FindItem(ItemId);
	const FFVTraderDef* T = FVDB::FindTrader(TraderId);
	if (!I || !T || Count <= 0 || Count > 999) { return EFVBuyResult::Invalid; }
	if (I->BuyPrice <= 0 || (I->TraderMask & T->Mask) == 0) { return EFVBuyResult::NotForSale; }
	if (GetLevel() < I->MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (I->Kind == EFVItemKind::Blueprint)
	{
		// update 6: a blueprint is bought once and learnt at once (never stored in a container)
		if (HasBlueprint(ItemId)) { return EFVBuyResult::AlreadyOwned; }
		if (Count != 1) { return EFVBuyResult::Invalid; }
		if (!Spend(int64(I->BuyPrice))) { return EFVBuyResult::NotEnoughMoney; }
		LearnBlueprint(ItemId);
		return EFVBuyResult::Ok;
	}
	const int64 Cost = int64(I->BuyPrice) * Count;
	if (!CanAfford(Cost)) { return EFVBuyResult::NotEnoughMoney; }
	if (!CanAddTo(EFVContainer::Backpack, ItemId, Count)) { return EFVBuyResult::NoRoom; }
	if (!Spend(Cost)) { return EFVBuyResult::NotEnoughMoney; }
	AddTo(EFVContainer::Backpack, ItemId, Count);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::BuyShovel(FName TraderId)
{
	const TArray<FFVShovelDef>& Shovels = FVDB::Shovels();
	const int32 Next = FMath::Max(0, ShovelLevel) + 1;
	if (Next > Shovels.Num()) { return EFVBuyResult::MaxLevel; }
	const FFVShovelDef& S = Shovels[Next - 1];
	const FFVTraderDef* T = FVDB::FindTrader(TraderId);
	if (!T) { return EFVBuyResult::Invalid; }
	if (S.Price <= 0 || (S.TraderMask & T->Mask) == 0) { return EFVBuyResult::NotForSale; }
	if (GetLevel() < S.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!CanAfford(S.Price) || !Spend(S.Price)) { return EFVBuyResult::NotEnoughMoney; }
	ShovelLevel = Next;
	UE_LOG(LogFV, Log, TEXT("FV: bought shovel level %d"), ShovelLevel);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::CraftShovel()
{
	const TArray<FFVShovelDef>& Shovels = FVDB::Shovels();
	const int32 Next = FMath::Max(0, ShovelLevel) + 1;
	if (Next > Shovels.Num()) { return EFVBuyResult::MaxLevel; }
	const FFVShovelDef& S = Shovels[Next - 1];
	if (S.WorkshopLevel <= 0) { return EFVBuyResult::NotForSale; }
	if (GetStationLevel(EFVStation::Workshop) < S.WorkshopLevel) { return EFVBuyResult::StationRequired; }
	if (GetLevel() < S.MinLevel) { return EFVBuyResult::LevelTooLow; }
	const int64 Coins = FMath::Max(0, S.Price / 2);
	if (!CanAfford(Coins)) { return EFVBuyResult::NotEnoughMoney; }
	if (!HasIngredients(S.Craft)) { return EFVBuyResult::NotEnoughMaterials; }
	ConsumeIngredients(S.Craft);
	Spend(Coins);
	ShovelLevel = Next;
	UE_LOG(LogFV, Log, TEXT("FV: crafted shovel level %d"), ShovelLevel);
	return EFVBuyResult::Ok;
}

float UFVSaveGame::DigSeconds() const
{
	const TArray<FFVShovelDef>& Shovels = FVDB::Shovels();
	if (ShovelLevel <= 0 || Shovels.Num() == 0) { return 0.f; }
	return FMath::Max(1.f, Shovels[FMath::Clamp(ShovelLevel, 1, Shovels.Num()) - 1].DigSeconds);
}

// ================================================================================================
// Stations
// ================================================================================================
int32 UFVSaveGame::GetStationLevel(EFVStation S) const
{
	const FFVStationDef& D = FVDB::Station(S);
	if (D.Id.IsNone() || D.Levels.Num() == 0) { return 0; }
	const int32* L = StationLevels.Find(D.Id);
	int32 Level = L ? FMath::Clamp(*L, 0, D.Levels.Num()) : 0;
	if (D.bStartsBuilt) { Level = FMath::Max(Level, 1); }
	return Level;
}

int32 UFVSaveGame::StationCapacity(EFVStation S) const
{
	const int32 L = GetStationLevel(S);
	if (L <= 0) { return 0; }
	const FFVStationDef& D = FVDB::Station(S);
	return D.Levels.IsValidIndex(L - 1) ? FMath::Max(0, D.Levels[L - 1].Capacity) : 0;
}

EFVBuyResult UFVSaveGame::CanUpgradeStation(EFVStation S) const
{
	const FFVStationDef& D = FVDB::Station(S);
	if (D.Id.IsNone() || D.Levels.Num() == 0) { return EFVBuyResult::Invalid; }
	const int32 Cur = GetStationLevel(S);
	if (Cur >= D.Levels.Num()) { return EFVBuyResult::MaxLevel; }
	const FFVStationLevel& Next = D.Levels[Cur];
	if (GetLevel() < Next.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!CanAfford(FMath::Max(0, Next.Price))) { return EFVBuyResult::NotEnoughMoney; }
	if (!HasIngredients(Next.Materials)) { return EFVBuyResult::NotEnoughMaterials; }
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::UpgradeStation(EFVStation S)
{
	const EFVBuyResult R = CanUpgradeStation(S);
	if (R != EFVBuyResult::Ok) { return R; }
	const FFVStationDef& D = FVDB::Station(S);
	const int32 Cur = GetStationLevel(S);
	const FFVStationLevel& Next = D.Levels[Cur];
	ConsumeIngredients(Next.Materials);
	Spend(FMath::Max(0, Next.Price));
	StationLevels.FindOrAdd(D.Id) = Cur + 1;
	if (S == EFVStation::WormFarm && Cur == 0) { WormFarmDay = Day; }   // the first worms appear tomorrow morning
	UE_LOG(LogFV, Log, TEXT("FV: station %s -> level %d"), *D.Id.ToString(), Cur + 1);
	return EFVBuyResult::Ok;
}

// ================================================================================================
// Crafting
// ================================================================================================
EFVBuyResult UFVSaveGame::CanCraft(const FFVRecipeDef& R, int32 Times) const
{
	if (Times <= 0 || R.OutputId.IsNone() || R.OutputCount <= 0) { return EFVBuyResult::Invalid; }
	const bool bRodOut = FVDB::FindRod(R.OutputId) != nullptr;   // 0.6.2: a forged rod (one at a time, never twice)
	if (bRodOut)
	{
		if (Times != 1 || R.OutputCount != 1) { return EFVBuyResult::Invalid; }
		if (OwnsRod(R.OutputId)) { return EFVBuyResult::AlreadyOwned; }
	}
	if (R.Yard != EFVYardBuilding::Count)
	{
		// update 6: a yard building recipe (sawmill / forge): StationLevel = the building level, Station is ignored
		if (R.Yard > EFVYardBuilding::Count) { return EFVBuyResult::Invalid; }
		if (GetYardLevel(R.Yard) < FMath::Max(1, R.StationLevel)) { return EFVBuyResult::StationRequired; }
	}
	else if (GetStationLevel(R.Station) < R.StationLevel) { return EFVBuyResult::StationRequired; }
	if (GetLevel() < R.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!R.RequiredAchievement.IsNone() && !IsAchievementDone(R.RequiredAchievement)) { return EFVBuyResult::Locked; }
	if (!HasIngredients(R.Inputs, Times)) { return EFVBuyResult::NotEnoughMaterials; }
	const int64 Coins = int64(FMath::Max(0, R.CoinCost)) * Times;
	if (!CanAfford(Coins)) { return EFVBuyResult::NotEnoughMoney; }
	// перековка: the tackle installed on a given-up rod must find a place (backpack, else chest)
	for (const FFVIngredient& In : R.Inputs)
	{
		if (!FVDB::FindRod(In.ItemId)) { continue; }
		const FFVExpertLoadout L = GetExpertLoadout(In.ItemId);
		for (const FName& T : { L.Line, L.Float, L.Reel })
		{
			if (!T.IsNone() && FVDB::IsItemId(T) && !CanAddTo(EFVContainer::Backpack, T, 1) && !CanAddTo(EFVContainer::Chest, T, 1))
			{
				return EFVBuyResult::NoRoom;
			}
		}
	}
	if (bRodOut) { return EFVBuyResult::Ok; }
	const int64 Out = int64(R.OutputCount) * Times;
	if (Out > FVSGHome::MaxUnits) { return EFVBuyResult::Invalid; }
	if (FVDB::IsBaitId(R.OutputId)) { return EFVBuyResult::Ok; }   // bait never runs out of room

	// room for the output once the inputs are gone (simulated on a copy of the one container that receives it;
	// bait inputs never change container room, so an empty bait map is enough)
	const bool bFishOut = FVDB::IsFishId(R.OutputId);
	TArray<FFVInvStack> Keep;
	TArray<FFVInvStack> Pack;
	if (bFishOut) { Keep = Keepnet; }
	else { Pack = Backpack; }
	TMap<FName, int32> NoBaits;
	FVSGHome::ApplyIngredients(Keep, Pack, NoBaits, R.Inputs, Times);
	if (bFishOut)
	{
		return FVSGHome::Units(Keep) + Out <= int64(KeepnetCapacity()) ? EFVBuyResult::Ok : EFVBuyResult::NoRoom;
	}
	const FFVItemDef* Item = FVDB::FindItem(R.OutputId);
	if (!Item && !FVDB::IsJunkId(R.OutputId)) { return EFVBuyResult::Invalid; }
	const int32 Cap = ContainerCapacity(EFVContainer::Backpack);
	if (Item && Item->Kind == EFVItemKind::Food)
	{
		// the dish quality is only known after the cooking mini-game: every quality must fit
		for (uint8 Q = 0; Q <= uint8(EFVQuality::Iridium); ++Q)
		{
			if (FVSGHome::SlotRoom(Pack, Cap, R.OutputId, Q, 0) < Out) { return EFVBuyResult::NoRoom; }
		}
		return EFVBuyResult::Ok;
	}
	return FVSGHome::SlotRoom(Pack, Cap, R.OutputId, 0, 0) >= Out ? EFVBuyResult::Ok : EFVBuyResult::NoRoom;
}

EFVBuyResult UFVSaveGame::Craft(const FFVRecipeDef& R, int32 Times, EFVQuality Quality)
{
	const EFVBuyResult Can = CanCraft(R, Times);
	if (Can != EFVBuyResult::Ok) { return Can; }
	if (FVDB::FindRod(R.OutputId))
	{
		// 0.6.2 (Р80): a forged rod goes straight into the hands, like a purchase; the inputs (a rod too) are given up
		if (!ConsumeIngredients(R.Inputs, 1)) { return EFVBuyResult::NotEnoughMaterials; }
		Spend(int64(FMath::Max(0, R.CoinCost)));
		OwnedRods.AddUnique(R.OutputId);
		EquipRod(R.OutputId);
		if (R.Yard == EFVYardBuilding::Forge) { U6.Forgings = FVSGHome::SatAdd32(U6.Forgings, 1); }
		UE_LOG(LogFV, Log, TEXT("FV: forged rod %s (recipe %s)"), *R.OutputId.ToString(), *R.Id.ToString());
		return EFVBuyResult::Ok;
	}
	int32 Out = R.OutputCount * Times;
	// §18.1: the mill's water saw — +MillSawBonus() boards per log sawn at the yard sawmill (quick or mini-game)
	static const FName BoardId(TEXT("mat_board"));
	static const FName LogId(TEXT("mat_log"));
	if (R.Yard == EFVYardBuilding::Sawmill && R.OutputId == BoardId && MillSawBonus() > 0)
	{
		for (const FFVIngredient& I : R.Inputs)
		{
			if (I.ItemId == LogId) { Out += FMath::Max(0, I.Count) * Times * MillSawBonus(); }
		}
	}
	const FFVItemDef* Item = FVDB::FindItem(R.OutputId);
	const bool bFood = Item && Item->Kind == EFVItemKind::Food;
	const uint8 Q = bFood ? uint8(FVSGHome::QualityOf(uint8(Quality))) : uint8(0);

	// all or nothing: snapshot what crafting touches
	const TArray<FFVInvStack> OldKeepnet = Keepnet;
	const TArray<FFVInvStack> OldBackpack = Backpack;
	const TMap<FName, int32> OldBaits = Baits;
	const FName OldBait = EquippedBait;
	const int64 OldMoney = Money;
	const int64 OldSpent = Stats.Spent;

	if (!ConsumeIngredients(R.Inputs, Times)) { return EFVBuyResult::NotEnoughMaterials; }
	Spend(int64(FMath::Max(0, R.CoinCost)) * Times);
	if (!GiveItem(R.OutputId, Out, Q, 0))
	{
		Keepnet = OldKeepnet;
		Backpack = OldBackpack;
		Baits = OldBaits;
		EquippedBait = OldBait;
		Money = OldMoney;
		Stats.Spent = OldSpent;
		return EFVBuyResult::NoRoom;
	}

	if (R.Yard == EFVYardBuilding::Forge) { U6.Forgings = FVSGHome::SatAdd32(U6.Forgings, Times); }   // update 6: «Кузнец»
	else if (R.Yard == EFVYardBuilding::Count && R.Station == EFVStation::Workshop) { Stats.Crafted = FVSGHome::SatAdd32(Stats.Crafted, Out); }
	if (R.Game == EFVCraftGame::Knots) { Stats.KnotsTied = FVSGHome::SatAdd32(Stats.KnotsTied, Times); }
	if (bFood) { Stats.Cooked = FVSGHome::SatAdd32(Stats.Cooked, Out); }
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::CraftLure(uint8 Base, uint8 Color, uint8 Glitter, bool& bOutNew)
{
	bOutNew = false;
	if (Base >= FVDB::LureBases || Color >= FVDB::LureColors || Glitter >= FVDB::LureGlitters) { return EFVBuyResult::Invalid; }
	if (GetStationLevel(EFVStation::Workshop) < 2) { return EFVBuyResult::StationRequired; }   // Верстак
	const FName Id = FVDB::LureId(Base, Color, Glitter);
	const FFVBaitDef* B = FVDB::FindBait(Id);
	if (!B || !B->bLure) { return EFVBuyResult::Invalid; }
	const TArray<FFVIngredient> Inputs = FVDB::LureInputs(Base, Color, Glitter);
	if (!HasIngredients(Inputs)) { return EFVBuyResult::NotEnoughMaterials; }
	ConsumeIngredients(Inputs);
	AddBait(Id, FVDB::LurePackSize());
	if (!DiscoveredLures.Contains(Id))
	{
		DiscoveredLures.Add(Id);
		bOutNew = true;
	}
	Stats.Crafted = FVSGHome::SatAdd32(Stats.Crafted, 1);
	return EFVBuyResult::Ok;
}

// ================================================================================================
// Rod tackle
// ================================================================================================
FFVRodLoadout UFVSaveGame::GetLoadout(FName RodId) const
{
	const FFVRodLoadout* L = RodLoadouts.Find(RodId);
	return L ? *L : FFVRodLoadout();
}

EFVBuyResult UFVSaveGame::CanInstallTackle(FName RodId, FName ItemId) const
{
	const FFVRodDef* Rod = FVDB::FindRod(RodId);
	if (!Rod || !OwnsRod(RodId)) { return EFVBuyResult::Invalid; }
	const FFVItemDef* I = FVDB::FindItem(ItemId);
	if (!I || (I->Kind != EFVItemKind::Line && I->Kind != EFVItemKind::Float)) { return EFVBuyResult::Invalid; }
	if (!IsStationBuilt(EFVStation::Workshop)) { return EFVBuyResult::StationRequired; }
	if (int32(I->Tier) > Rod->Tier + 1) { return EFVBuyResult::LevelTooLow; }
	const FFVRodLoadout L = GetLoadout(RodId);
	if ((I->Kind == EFVItemKind::Line ? L.Line : L.Float) == ItemId) { return EFVBuyResult::AlreadyOwned; }
	if (CountItem(ItemId) <= 0) { return EFVBuyResult::Invalid; }
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::InstallTackle(FName RodId, FName ItemId)
{
	const EFVBuyResult R = CanInstallTackle(RodId, ItemId);
	if (R != EFVBuyResult::Ok) { return R; }
	const FFVItemDef* I = FVDB::FindItem(ItemId);
	const bool bLine = I->Kind == EFVItemKind::Line;
	const FFVRodLoadout Cur = GetLoadout(RodId);
	const FName Old = bLine ? Cur.Line : Cur.Float;
	const bool bReturnOld = !Old.IsNone() && FVDB::IsItemId(Old);

	// the new item leaves the backpack; the old one must fit back in
	TArray<FFVInvStack> Pack = Backpack;
	FVSGHome::TakeUnits(Pack, 1, [ItemId](const FFVInvStack& S) { return S.ItemId == ItemId; }, &FVSGHome::LowQualityFirst);
	if (bReturnOld && FVSGHome::SlotRoom(Pack, ContainerCapacity(EFVContainer::Backpack), Old, 0, 0) < 1)
	{
		return EFVBuyResult::NoRoom;
	}
	Backpack = MoveTemp(Pack);
	if (bReturnOld) { FVSGHome::PutSlots(Backpack, Old, 0, 0, 1); }
	FFVRodLoadout& L = RodLoadouts.FindOrAdd(RodId);
	(bLine ? L.Line : L.Float) = ItemId;
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::RemoveTackle(FName RodId, EFVItemKind Slot)
{
	if (!FVDB::FindRod(RodId) || !OwnsRod(RodId)) { return EFVBuyResult::Invalid; }
	if (Slot != EFVItemKind::Line && Slot != EFVItemKind::Float) { return EFVBuyResult::Invalid; }
	if (!IsStationBuilt(EFVStation::Workshop)) { return EFVBuyResult::StationRequired; }
	FFVRodLoadout* L = RodLoadouts.Find(RodId);
	if (!L) { return EFVBuyResult::Invalid; }
	FName& Installed = Slot == EFVItemKind::Line ? L->Line : L->Float;
	if (Installed.IsNone()) { return EFVBuyResult::Invalid; }
	if (FVDB::IsItemId(Installed))
	{
		if (!CanAddTo(EFVContainer::Backpack, Installed, 1)) { return EFVBuyResult::NoRoom; }
		AddTo(EFVContainer::Backpack, Installed, 1);
	}
	Installed = NAME_None;
	if (L->Line.IsNone() && L->Float.IsNone()) { RodLoadouts.Remove(RodId); }
	return EFVBuyResult::Ok;
}

FFVStatMods UFVSaveGame::GetTackleMods(FName RodId) const
{
	FFVStatMods M;
	const FFVRodLoadout L = GetLoadout(RodId);
	const FFVItemDef* Line = FVDB::FindItem(L.Line);
	if (Line && Line->Kind == EFVItemKind::Line) { M.Combine(Line->Tackle); }
	const FFVItemDef* Float = FVDB::FindItem(L.Float);
	if (Float && Float->Kind == EFVItemKind::Float) { M.Combine(Float->Tackle); }
	return M;
}

FFVRodStats UFVSaveGame::GetRodStats() const
{
	const FFVRodDef& Rod = GetRod();
	const FFVRodLoadout L = GetLoadout(Rod.Id);
	return FVDB::ComposeRodStats(Rod, L.Line, L.Float);   // update 5: the same pure composition as UFVGame::GetEffectiveRodStats
}

// ================================================================================================
// Food
// ================================================================================================
bool UFVSaveGame::EatFood(int32 BackpackStackIndex, double Now)
{
	if (!Backpack.IsValidIndex(BackpackStackIndex) || !FMath::IsFinite(Now)) { return false; }
	const FFVInvStack St = Backpack[BackpackStackIndex];
	const FFVItemDef* I = FVDB::FindItem(St.ItemId);
	if (!I || I->Kind != EFVItemKind::Food || St.Count <= 0) { return false; }
	const EFVQuality Q = FVSGHome::QualityOf(St.Quality);
	if (!RemoveFrom(EFVContainer::Backpack, BackpackStackIndex, 1)) { return false; }
	ActiveFood = I->Id;
	ActiveFoodQuality = uint8(Q);
	FoodExpires = Now + double(FMath::Max(0.f, I->BuffMinutes) * FVDB::FoodPower(Q));
	return true;
}

const FFVItemDef* UFVSaveGame::GetActiveFood(double Now) const
{
	if (ActiveFood.IsNone() || !(Now < FoodExpires)) { return nullptr; }
	const FFVItemDef* I = FVDB::FindItem(ActiveFood);
	return (I && I->Kind == EFVItemKind::Food) ? I : nullptr;
}

float UFVSaveGame::FoodMinutesLeft(double Now) const
{
	return GetActiveFood(Now) ? float(FMath::Max(0.0, FoodExpires - Now)) : 0.f;
}

FFVStatMods UFVSaveGame::GetFoodMods(double Now) const
{
	const FFVItemDef* I = GetActiveFood(Now);
	return I ? I->Buff.Scaled(FVDB::FoodPower(FVSGHome::QualityOf(ActiveFoodQuality))) : FFVStatMods();
}

// ================================================================================================
// Time-based systems
// ================================================================================================
void UFVSaveGame::SimulatePassive(double FromAbs, double ToAbs, EFVWeather InWeather)
{
	if (!FMath::IsFinite(FromAbs) || !FMath::IsFinite(ToAbs) || ToAbs <= FromAbs) { return; }

	// ---- food buff expiry
	if (!ActiveFood.IsNone() && ToAbs >= FoodExpires)
	{
		ActiveFood = NAME_None;
		ActiveFoodQuality = 0;
		FoodExpires = 0.0;
	}

	// ---- smokehouse: every unfinished fish smokes while there is fuel; fuel burns only while something smokes
	if (Smokehouse.Num() > 0 && SmokeFuel > 0.f)
	{
		float MaxLeft = 0.f;
		for (const FFVSmokeSlot& S : Smokehouse) { MaxLeft = FMath::Max(MaxLeft, S.MinutesLeft); }
		const float Dt = float(FMath::Min(ToAbs - FromAbs, FVSGHome::MaxSimMinutes));
		const float Burn = FMath::Min3(Dt, SmokeFuel, MaxLeft);
		if (Burn > 0.f)
		{
			for (FFVSmokeSlot& S : Smokehouse) { S.MinutesLeft = FMath::Max(0.f, S.MinutesLeft - Burn); }
			SmokeFuel = FMath::Max(0.f, SmokeFuel - Burn);
		}
	}

	// ---- nets (each from its own SimMinute)
	if (Nets.Num() > 0)
	{
		const EFVWeather W = uint8(InWeather) < uint8(EFVWeather::Count) ? InWeather : EFVWeather::Sunny;
		const int32 Level = GetLevel();
		int32 NetFish = 0;
		for (FFVNetState& N : Nets) { FVSGHome::SimulateNet(N, ToAbs, W, Level, NetFish); }
		if (NetFish > 0) { Stats.NetFish = FVSGHome::SatAdd32(Stats.NetFish, NetFish); }
	}
}

TArray<FString> UFVSaveGame::OnNewDay(double /*Now*/)
{
	TArray<FString> Lines;

	// ---- worm farm: a few fresh worms every morning (deterministic per day)
	const int32 FarmLevel = GetStationLevel(EFVStation::WormFarm);
	if (FarmLevel > 0 && WormFarmDay != Day)
	{
		FRandomStream Rng(int32(FVSGHome::Fmix(FVSGHome::Mix(0xB0B0Au, uint32(Day) * 2654435761u))));
		int32 Worms = FarmLevel >= 2 ? Rng.RandRange(3, 4) : Rng.RandRange(2, 3);
		int32 Crawlers = (FarmLevel >= 2 && Rng.FRand() < 0.35f) ? 1 : 0;
		const int32 Room = FMath::Max(0, StationCapacity(EFVStation::WormFarm) - WormFarmWorms - WormFarmCrawlers);
		Worms = FMath::Min(Worms, Room);
		Crawlers = FMath::Min(Crawlers, Room - Worms);
		WormFarmWorms += Worms;
		WormFarmCrawlers += Crawlers;
		WormFarmDay = Day;
		if (Worms + Crawlers > 0)
		{
			FString Line = Worms > 0
				? FString::Printf(TEXT("Червятник: +%d %s"), Worms, *FVSGHome::Plural(Worms, TEXT("червь"), TEXT("червя"), TEXT("червей")))
				: FString(TEXT("Червятник:"));
			if (Crawlers > 0) { Line += Worms > 0 ? TEXT(" и выползок") : TEXT(" +1 выползок"); }
			Lines.Add(Line);
		}
		else
		{
			Lines.Add(TEXT("Червятник полон — соберите червей"));
		}
		// update 6: «Глубинный червятник» (level 3) — deep worms every morning straight into the bait box, up to the
		// station capacity of this level (§9б)
		static const FName DeepWorm(TEXT("bait_deepworm"));
		if (FarmLevel >= 3 && FVDB::FindBait(DeepWorm))
		{
			const int32 DeepRoom = FMath::Max(0, StationCapacity(EFVStation::WormFarm) - BaitCount(DeepWorm));
			const int32 Deep = FMath::Min(Rng.RandRange(2, 3), DeepRoom);
			if (Deep > 0)
			{
				AddBait(DeepWorm, Deep);
				Lines.Add(FString::Printf(TEXT("Глубинный червятник: +%d %s"), Deep,
					*FVSGHome::Plural(Deep, TEXT("глубинный червь"), TEXT("глубинных червя"), TEXT("глубинных червей"))));
			}
		}
	}

	// ---- stale daily masks
	for (auto It = Gathered.CreateIterator(); It; ++It) { if (It.Value().Day != Day) { It.RemoveCurrent(); } }
	for (auto It = Dug.CreateIterator(); It; ++It) { if (It.Value().Day != Day) { It.RemoveCurrent(); } }

	// ---- a new order board
	RefreshOrders();
	return Lines;
}

TArray<FName> UFVSaveGame::ExpireOrders(double Now)
{
	TArray<FName> Failed;
	if (!FMath::IsFinite(Now)) { return Failed; }
	for (FFVOrder& O : Orders)
	{
		if (O.State == uint8(EFVOrderState::Taken) && Now >= O.Deadline)
		{
			O.State = uint8(EFVOrderState::Failed);   // the deposit is lost
			Stats.OrdersFailed = FVSGHome::SatAdd32(Stats.OrdersFailed, 1);
			Failed.Add(O.Id);
		}
	}
	return Failed;
}

// ================================================================================================
// Nets
// ================================================================================================
FFVNetState* UFVSaveGame::FindNet(FName LocationId, int32 Spot)
{
	return Nets.FindByPredicate([&](const FFVNetState& N) { return N.Location == LocationId && N.Spot == Spot; });
}

const FFVNetState* UFVSaveGame::FindNet(FName LocationId, int32 Spot) const
{
	return Nets.FindByPredicate([&](const FFVNetState& N) { return N.Location == LocationId && N.Spot == Spot; });
}

EFVBuyResult UFVSaveGame::PlaceNet(FName LocationId, int32 Spot, FName NetItemId, double Now)
{
	if (Spot < 0 || Spot >= FVSGHome::NetSpotCount(LocationId) || FVDB::LocationIndex(LocationId) == INDEX_NONE) { return EFVBuyResult::Invalid; }
	if (FVDBU6::IsExpertLocation(LocationId)) { return EFVBuyResult::Invalid; }   // update 6 (Р35): no nets in the City / Cave
	if (FVMill::IsMillId(LocationId) && Spot == FVMill::OmutNetSpot && !IsOmutOpen()) { return EFVBuyResult::Invalid; }   // §18: the dam first
	if (FindNet(LocationId, Spot)) { return EFVBuyResult::AlreadyOwned; }
	// update 6 (Р36): the spot is taken by the artel's net
	if (U6.ArtelNets.ContainsByPredicate([&](const FFVNetState& A) { return A.Location == LocationId && A.Spot == Spot; }))
	{
		return EFVBuyResult::AlreadyOwned;
	}
	const FFVItemDef* Def = FVDB::FindItem(NetItemId);
	if (!Def || Def->Kind != EFVItemKind::Net) { return EFVBuyResult::Invalid; }
	if (CountItem(NetItemId) <= 0) { return EFVBuyResult::NotEnoughMaterials; }
	FVSGHome::TakeUnits(Backpack, 1, [NetItemId](const FFVInvStack& S) { return S.ItemId == NetItemId; }, &FVSGHome::LowQualityFirst);
	FFVNetState& N = Nets.AddDefaulted_GetRef();
	N.Location = LocationId;
	N.Spot = Spot;
	N.NetItem = NetItemId;
	N.Durability = FMath::Max(1.f, Def->NetDurability);
	N.bTorn = false;
	N.SimMinute = FMath::IsFinite(Now) ? FMath::Max(0.0, Now) : 0.0;
	N.Progress = 0.f;
	N.Catch.Reset();
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::CollectNet(FName LocationId, int32 Spot)
{
	FFVNetState* N = FindNet(LocationId, Spot);
	if (!N) { return 0; }
	int32 Moved = 0;
	for (int32 i = 0; i < N->Catch.Num();)
	{
		FFVInvStack& St = N->Catch[i];
		const EFVContainer To = FVDB::IsFishId(St.ItemId) ? EFVContainer::Keepnet
			: (FVDB::IsJunkId(St.ItemId) ? EFVContainer::Backpack : EFVContainer::Count);
		if (To == EFVContainer::Count)
		{
			St.Count = 0;   // unknown id: drop it
		}
		else if (St.Count > 0)
		{
			const FName GiveId = FVSGHome::CatchGiveId(St.ItemId);   // update 6: junk_scrap -> mat_scrap
			const uint8 Q = FVSGHome::NormQ(GiveId, St.Quality);
			const int32 Take = int32(FMath::Min<int64>(St.Count, FVSGHome::RoomFor(*this, To, GiveId, Q, 0)));
			if (Take > 0 && AddTo(To, GiveId, Take, Q, 0))
			{
				St.Count -= Take;
				Moved += Take;
			}
		}
		if (N->Catch[i].Count <= 0) { N->Catch.RemoveAt(i); }
		else { ++i; }
	}
	return Moved;
}

TArray<FFVIngredient> UFVSaveGame::NetRepairCost() const
{
	TArray<FFVIngredient> Cost;
	FFVIngredient& Rope = Cost.AddDefaulted_GetRef();
	Rope.ItemId = FName(TEXT("mat_rope"));
	Rope.Count = 1;
	FFVIngredient& Fiber = Cost.AddDefaulted_GetRef();
	Fiber.ItemId = FName(TEXT("res_fiber"));
	Fiber.Count = 2;
	return Cost;
}

EFVBuyResult UFVSaveGame::RepairNet(FName LocationId, int32 Spot)
{
	FFVNetState* N = FindNet(LocationId, Spot);
	if (!N) { return EFVBuyResult::Invalid; }
	const FFVItemDef* Def = FVDB::FindItem(N->NetItem);
	if (!Def) { return EFVBuyResult::Invalid; }
	const float MaxDur = FMath::Max(1.f, Def->NetDurability);
	if (!N->bTorn && N->Durability >= MaxDur - 0.01f) { return EFVBuyResult::AlreadyOwned; }   // nothing to repair
	const TArray<FFVIngredient> Cost = NetRepairCost();
	if (!HasIngredients(Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	ConsumeIngredients(Cost);
	N = FindNet(LocationId, Spot);   // (Nets untouched by ConsumeIngredients; re-fetch for clarity)
	if (!N) { return EFVBuyResult::Invalid; }
	N->Durability = FMath::Min(MaxDur, FMath::Max(0.f, N->Durability) + 0.5f * MaxDur);
	N->bTorn = false;
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::RemoveNet(FName LocationId, int32 Spot)
{
	const int32 Idx = Nets.IndexOfByPredicate([&](const FFVNetState& N) { return N.Location == LocationId && N.Spot == Spot; });
	if (Idx == INDEX_NONE) { return EFVBuyResult::Invalid; }
	const FFVNetState& N = Nets[Idx];
	if (FVSGHome::Units(N.Catch) > 0) { return EFVBuyResult::Invalid; }   // collect the catch first
	const FFVItemDef* Def = FVDB::FindItem(N.NetItem);
	if (Def && Def->Kind == EFVItemKind::Net)
	{
		if (N.bTorn || N.Durability < 0.95f * FMath::Max(1.f, Def->NetDurability)) { return EFVBuyResult::Invalid; }   // repair it first
		if (!CanAddTo(EFVContainer::Backpack, N.NetItem, 1)) { return EFVBuyResult::NoRoom; }
		AddTo(EFVContainer::Backpack, N.NetItem, 1);
	}
	Nets.RemoveAt(Idx);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::DestroyNet(FName LocationId, int32 Spot, int32& OutCollected, int32& OutLost)
{
	OutCollected = 0;
	OutLost = 0;
	const FFVNetState* Found = FindNet(LocationId, Spot);
	if (!Found) { return EFVBuyResult::Invalid; }
	const FFVItemDef* Def = FVDB::FindItem(Found->NetItem);
	const float MaxDur = (Def && Def->NetDurability > 0.f) ? Def->NetDurability : 1.f;
	if (!Found->bTorn && Found->Durability >= 0.95f * MaxDur) { return EFVBuyResult::Invalid; }   // intact: take it off instead
	OutCollected = CollectNet(LocationId, Spot);
	if (const FFVNetState* Left = FindNet(LocationId, Spot))
	{
		OutLost = FVSGHome::Units(Left->Catch);
	}
	const int32 Idx = Nets.IndexOfByPredicate([&](const FFVNetState& N) { return N.Location == LocationId && N.Spot == Spot; });
	if (Idx != INDEX_NONE) { Nets.RemoveAt(Idx); }
	return EFVBuyResult::Ok;
}

// ================================================================================================
// Smokehouse
// ================================================================================================
EFVBuyResult UFVSaveGame::LoadSmokehouse(int32 KeepnetStackIndex)
{
	if (!IsStationBuilt(EFVStation::Smokehouse)) { return EFVBuyResult::StationRequired; }
	if (!Keepnet.IsValidIndex(KeepnetStackIndex)) { return EFVBuyResult::Invalid; }
	const FFVInvStack St = Keepnet[KeepnetStackIndex];
	if (St.Count <= 0 || !FVDB::IsFishId(St.ItemId) || FVSGHome::IsSmoked(St)) { return EFVBuyResult::Invalid; }
	if (Smokehouse.Num() >= StationCapacity(EFVStation::Smokehouse)) { return EFVBuyResult::NoRoom; }
	if (!RemoveFrom(EFVContainer::Keepnet, KeepnetStackIndex, 1)) { return EFVBuyResult::Invalid; }
	FFVSmokeSlot& S = Smokehouse.AddDefaulted_GetRef();
	S.FishId = St.ItemId;
	S.Quality = uint8(FVSGHome::QualityOf(St.Quality));
	S.MinutesLeft = FVSGHome::SmokeMinutes;
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::AddSmokeFuel(int32 Branches)
{
	if (!IsStationBuilt(EFVStation::Smokehouse)) { return EFVBuyResult::StationRequired; }
	if (Branches <= 0) { return EFVBuyResult::Invalid; }
	if (SmokeFuel >= FVSGHome::MaxSmokeFuel - 0.5f) { return EFVBuyResult::MaxLevel; }   // the firebox is full
	const float Per = FuelMinutesPerBranch();
	const int32 Useful = FMath::Max(1, FMath::CeilToInt((FVSGHome::MaxSmokeFuel - SmokeFuel) / Per));
	const int32 N = FMath::Min(Branches, Useful);
	TArray<FFVIngredient> Cost;
	FFVIngredient& In = Cost.AddDefaulted_GetRef();
	In.ItemId = FVSGHome::BranchId();
	In.Count = N;
	if (!HasIngredients(Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	ConsumeIngredients(Cost);
	SmokeFuel = FMath::Min(FVSGHome::MaxSmokeFuel, SmokeFuel + Per * float(N));
	return EFVBuyResult::Ok;
}

float UFVSaveGame::FuelMinutesPerBranch() const
{
	return GetStationLevel(EFVStation::Smokehouse) >= 2 ? 90.f : 60.f;
}

int32 UFVSaveGame::CollectSmoked()
{
	int32 Collected = 0;
	for (int32 i = 0; i < Smokehouse.Num();)
	{
		const FFVSmokeSlot S = Smokehouse[i];
		if (S.MinutesLeft > 0.f) { ++i; continue; }
		if (!FVDB::IsFishId(S.FishId)) { Smokehouse.RemoveAt(i); continue; }
		if (!AddToKeepnetEx(S.FishId, FVSGHome::QualityOf(S.Quality), 1, FVStackFlags::Smoked)) { ++i; continue; }   // keepnet full
		Smokehouse.RemoveAt(i);
		++Collected;
	}
	if (Collected > 0) { Stats.Smoked = FVSGHome::SatAdd32(Stats.Smoked, Collected); }
	return Collected;
}

// ================================================================================================
// Worm farm
// ================================================================================================
int32 UFVSaveGame::CollectWorms()
{
	const int32 Worms = FMath::Max(0, WormFarmWorms);
	const int32 Crawlers = FMath::Max(0, WormFarmCrawlers);
	if (Worms > 0) { AddBait(FVSGHome::WormId(), Worms); }
	if (Crawlers > 0) { AddBait(FVSGHome::CrawlerId(), Crawlers); }
	WormFarmWorms = 0;
	WormFarmCrawlers = 0;
	return Worms + Crawlers;
}

// ================================================================================================
// Aquarium & trophies
// ================================================================================================
EFVBuyResult UFVSaveGame::PutInAquarium(int32 KeepnetStackIndex)
{
	if (!IsStationBuilt(EFVStation::Aquarium)) { return EFVBuyResult::StationRequired; }
	if (!Keepnet.IsValidIndex(KeepnetStackIndex)) { return EFVBuyResult::Invalid; }
	const FFVInvStack St = Keepnet[KeepnetStackIndex];
	if (St.Count <= 0 || !FVDB::IsFishId(St.ItemId) || FVSGHome::IsSmoked(St)) { return EFVBuyResult::Invalid; }
	if (!CanAddTo(EFVContainer::Aquarium, St.ItemId, 1, St.Quality, 0)) { return EFVBuyResult::NoRoom; }
	RemoveFrom(EFVContainer::Keepnet, KeepnetStackIndex, 1);
	AddTo(EFVContainer::Aquarium, St.ItemId, 1, St.Quality, 0);
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::TakeFromAquarium(int32 AquariumIndex)
{
	if (!Aquarium.IsValidIndex(AquariumIndex)) { return EFVBuyResult::Invalid; }
	const FFVInvStack St = Aquarium[AquariumIndex];
	if (!FVDB::IsFishId(St.ItemId) || St.Count <= 0)
	{
		Aquarium.RemoveAt(AquariumIndex);
		return EFVBuyResult::Invalid;
	}
	if (!CanAddTo(EFVContainer::Keepnet, St.ItemId, 1, St.Quality, 0)) { return EFVBuyResult::NoRoom; }
	RemoveFrom(EFVContainer::Aquarium, AquariumIndex, 1);
	AddTo(EFVContainer::Keepnet, St.ItemId, 1, St.Quality, 0);
	return EFVBuyResult::Ok;
}

TArray<FFVIngredient> UFVSaveGame::TrophyCost() const
{
	TArray<FFVIngredient> Cost;
	FFVIngredient& Board = Cost.AddDefaulted_GetRef();
	Board.ItemId = FName(TEXT("mat_board"));
	Board.Count = 1;
	FFVIngredient& Glue = Cost.AddDefaulted_GetRef();
	Glue.ItemId = FName(TEXT("mat_glue"));
	Glue.Count = 1;
	return Cost;
}

EFVBuyResult UFVSaveGame::MountTrophy(int32 KeepnetStackIndex, float SizeCm)
{
	if (!IsStationBuilt(EFVStation::TrophyWall)) { return EFVBuyResult::StationRequired; }
	if (!Keepnet.IsValidIndex(KeepnetStackIndex)) { return EFVBuyResult::Invalid; }
	const FFVInvStack St = Keepnet[KeepnetStackIndex];
	if (St.Count <= 0 || !FVDB::IsFishId(St.ItemId) || FVSGHome::IsSmoked(St)) { return EFVBuyResult::Invalid; }

	float Size = FMath::IsFinite(SizeCm) ? FMath::Max(0.f, SizeCm) : 0.f;
	if (Size <= 0.f)
	{
		if (const FFVFishRecord* Rec = Records.Find(St.ItemId)) { Size = FMath::Max(0.f, Rec->MaxSize); }   // the species record
	}
	const int32 ExistingIdx = Trophies.IndexOfByPredicate([&](const FFVTrophy& T) { return T.FishId == St.ItemId; });
	if (ExistingIdx != INDEX_NONE && Size <= Trophies[ExistingIdx].SizeCm + 0.05f) { return EFVBuyResult::AlreadyOwned; }
	if (ExistingIdx == INDEX_NONE && Trophies.Num() >= StationCapacity(EFVStation::TrophyWall)) { return EFVBuyResult::NoRoom; }
	const TArray<FFVIngredient> Cost = TrophyCost();
	if (!HasIngredients(Cost)) { return EFVBuyResult::NotEnoughMaterials; }

	ConsumeIngredients(Cost);   // backpack only: the keepnet index stays valid
	RemoveFrom(EFVContainer::Keepnet, KeepnetStackIndex, 1);
	FFVTrophy& T = ExistingIdx != INDEX_NONE ? Trophies[ExistingIdx] : Trophies.AddDefaulted_GetRef();
	T.FishId = St.ItemId;
	T.SizeCm = Size;
	T.Quality = uint8(FVSGHome::QualityOf(St.Quality));
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::UnmountTrophy(FName FishId)
{
	const int32 Idx = FishId.IsNone() ? INDEX_NONE : Trophies.IndexOfByPredicate([FishId](const FFVTrophy& T) { return T.FishId == FishId; });
	if (Idx == INDEX_NONE || !FVDB::IsFishId(FishId)) { return EFVBuyResult::Invalid; }
	if (KeepnetFull()) { return EFVBuyResult::NoRoom; }
	const EFVQuality Q = FVSGHome::QualityOf(Trophies[Idx].Quality);
	if (!AddToKeepnet(FishId, Q, 1)) { return EFVBuyResult::NoRoom; }   // nothing changed
	Trophies.RemoveAt(Idx);
	UE_LOG(LogFV, Log, TEXT("FV: trophy %s taken off the wall"), *FishId.ToString());
	return EFVBuyResult::Ok;
}

// ================================================================================================
// Gathering & digging
// ================================================================================================
FName UFVSaveGame::ResourceAt(FName LocationId, int32 Slot, int32& OutCount) const
{
	OutCount = 0;
	if (Slot < 0 || Slot >= 31 || LocationId.IsNone()) { return NAME_None; }
	if (const FFVDayMask* M = Gathered.Find(LocationId))
	{
		if (M->Day == Day && (M->Mask & (1 << Slot)) != 0) { return NAME_None; }   // already taken today
	}
	return FVDB::RollResource(LocationId, Day, Slot, OutCount);
}

bool UFVSaveGame::Gather(FName LocationId, int32 Slot, FName& OutItem, int32& OutCount)
{
	OutItem = ResourceAt(LocationId, Slot, OutCount);
	if (OutItem.IsNone() || OutCount <= 0) { return false; }
	if (!AddTo(EFVContainer::Backpack, OutItem, OutCount)) { return false; }   // OutItem/OutCount still say what lies there
	FFVDayMask& M = Gathered.FindOrAdd(LocationId);
	if (M.Day != Day)
	{
		M.Day = Day;
		M.Mask = 0;
	}
	M.Mask |= (1 << Slot);
	Stats.Gathered = FVSGHome::SatAdd32(Stats.Gathered, OutCount);
	return true;
}

bool UFVSaveGame::IsDugToday(FName LocationId, int32 Spot) const
{
	if (Spot < 0 || Spot >= 31) { return false; }
	const FFVDayMask* M = Dug.Find(LocationId);
	return M && M->Day == Day && (M->Mask & (1 << Spot)) != 0;
}

void UFVSaveGame::MarkDug(FName LocationId, int32 Spot)
{
	if (Spot < 0 || Spot >= 31 || LocationId.IsNone()) { return; }
	FFVDayMask& M = Dug.FindOrAdd(LocationId);
	if (M.Day != Day)
	{
		M.Day = Day;
		M.Mask = 0;
	}
	M.Mask |= (1 << Spot);
}

// ================================================================================================
// Orders
// ================================================================================================
void UFVSaveGame::RefreshOrders()
{
	if (OrdersDay == Day) { return; }
	// keep the taken orders, drop yesterday's offers and finished orders
	Orders.RemoveAll([](const FFVOrder& O) { return O.State != uint8(EFVOrderState::Taken); });
	OrdersDay = Day;

	FRandomStream Rng(int32(FVSGHome::Fmix(FVSGHome::Mix(0x0DE25u, uint32(Day) * 2654435761u))));
	const int32 Level = GetLevel();

	// ---- pools
	TArray<const FFVFishDef*> FishPool;
	TArray<float> FishW;
	const bool bGarden = GetYardLevel(EFVYardBuilding::Garden) > 0;   // update 6 (Р42): carrots & other produce
	for (const FFVLocationDef& L : FVDB::Locations())
	{
		if (!IsLocationUnlocked(L.Id)) { continue; }
		if (L.bExpertZone || FVDBU6::IsExpertLocation(L.Id)) { continue; }   // update 6: the City has its own task board
		for (const FFVFishDef* F : FVDB::FishAt(L.Id))
		{
			if (!F || F->bLegendary || F->bExpert || F->MinLevel > Level) { continue; }
			if (FVMill::IsOmutOnly(*F) && !IsOmutOpen()) { continue; }   // §18: the sterlet lives only in the Omut
			FishPool.Add(F);
			FishW.Add(F->Rarity == EFVRarity::Common ? 3.f : (F->Rarity == EFVRarity::Uncommon ? 2.f : 1.f));
		}
	}
	TArray<const FFVFishDef*> SmokePool;
	TArray<float> SmokeW;
	for (const FFVFishDef* F : FishPool)
	{
		if (F->Rarity <= EFVRarity::Uncommon) { SmokePool.Add(F); SmokeW.Add(1.f); }
	}
	const bool bSmoke = IsStationBuilt(EFVStation::Smokehouse) && SmokePool.Num() > 0;
	const int32 KitchenLevel = GetStationLevel(EFVStation::Kitchen);
	TArray<const FFVItemDef*> Dishes;
	TArray<float> DishW;
	for (const FFVRecipeDef* R : FVDB::RecipesFor(EFVStation::Kitchen))
	{
		if (!R || R->StationLevel > KitchenLevel || R->MinLevel > Level) { continue; }
		if (!bGarden && FVSGHome::RecipeNeedsGarden(*R)) { continue; }   // update 6 (Р42): no fish soup before the garden
		if (!FVSGHome::CanObtainRecipe(*this, *R)) { continue; }          // update 6: flour, City vegetables, mill fish
		const FFVItemDef* I = FVDB::FindItem(R->OutputId);
		if (I && I->Kind == EFVItemKind::Food && !Dishes.Contains(I)) { Dishes.Add(I); DishW.Add(1.f); }
	}
	TArray<const FFVItemDef*> Resources = FVDB::ItemsOfKind(EFVItemKind::Resource);
	// update 6: only what the player can already get (garden produce needs the garden; level-gated resources)
	Resources.RemoveAll([bGarden, Level](const FFVItemDef* I)
	{
		return !I || I->MinLevel > Level || (!bGarden && FVSGHome::IsGardenProduce(I->Id));
	});
	TArray<float> ResW;
	ResW.Init(1.f, Resources.Num());
	TArray<const FFVItemDef*> Materials;
	TArray<float> MatW;
	for (const TCHAR* Id : { TEXT("mat_board"), TEXT("mat_rope"), TEXT("mat_glue") })
	{
		if (const FFVItemDef* I = FVDB::FindItem(FName(Id))) { Materials.Add(I); MatW.Add(1.f); }
	}
	static const TCHAR* Customers[] = {
		TEXT("Бабушка Нюра"), TEXT("Кузнец Игнат"), TEXT("Учительница Вера"), TEXT("Повар Жора"),
		TEXT("Рыбак Петрович"), TEXT("Тётя Галя"), TEXT("Почтальон Лёша"), TEXT("Капитан Бурун") };
	struct FExtra { const TCHAR* Id; int32 Count; };
	static const FExtra Extras[] = { { TEXT("mat_glitter"), 2 }, { TEXT("float_feather"), 1 }, { TEXT("line_linen"), 1 }, { TEXT("mat_glue"), 2 } };

	for (int32 n = 0; n < FVSGHome::OffersPerDay; ++n)
	{
		FFVOrder O;
		float Value = 0.f;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			O = FFVOrder();
			Value = 0.f;
			const float WFish = FishPool.Num() > 0 ? 45.f : 0.f;
			const float WSmoke = bSmoke ? 15.f : 0.f;
			const float WRes = (Resources.Num() > 0 || Materials.Num() > 0) ? 25.f : 0.f;
			const float WFood = Dishes.Num() > 0 ? 15.f : 0.f;
			const float Total = WFish + WSmoke + WRes + WFood;
			if (Total <= 0.f) { break; }
			const float Roll = Rng.FRandRange(0.f, Total);
			if (Roll < WFish)
			{
				// a fish of an unlocked location
				const FFVFishDef* F = FVSGHome::PickWeighted(FishPool, FishW, Rng);
				if (!F) { continue; }
				O.ItemId = F->Id;
				O.Count = F->Rarity == EFVRarity::Common ? Rng.RandRange(2, 3) : (F->Rarity == EFVRarity::Uncommon ? Rng.RandRange(1, 2) : 1);
				const bool bSilver = Level >= 4 && Rng.FRand() < 0.3f;
				O.MinQuality = uint8(bSilver ? EFVQuality::Silver : EFVQuality::Normal);
				Value = float(FVDB::FishPrice(*F, EFVQuality(O.MinQuality)) * O.Count);
				O.DurationHours = F->Rarity >= EFVRarity::Rare ? 48 : (Rng.FRand() < 0.5f ? 24 : 36);
			}
			else if (Roll < WFish + WSmoke)
			{
				// smoked fish
				const FFVFishDef* F = FVSGHome::PickWeighted(SmokePool, SmokeW, Rng);
				if (!F) { continue; }
				O.ItemId = F->Id;
				O.bSmoked = true;
				O.Count = F->Rarity == EFVRarity::Common ? Rng.RandRange(1, 2) : 1;
				Value = float(FVDB::FishPrice(*F, EFVQuality::Normal) * 2 * O.Count);
				O.DurationHours = Rng.FRand() < 0.5f ? 36 : 48;
			}
			else if (Roll < WFish + WSmoke + WRes)
			{
				// resources (5-15) or materials (2-5 of board / rope / glue)
				const bool bMaterial = Materials.Num() > 0 && (Resources.Num() == 0 || Rng.FRand() < 0.5f);
				const FFVItemDef* I = bMaterial ? FVSGHome::PickWeighted(Materials, MatW, Rng) : FVSGHome::PickWeighted(Resources, ResW, Rng);
				if (!I) { continue; }
				O.ItemId = I->Id;
				O.Count = bMaterial ? Rng.RandRange(2, 5) : Rng.RandRange(5, 15);
				Value = float(FMath::Max(1, I->SellPrice) * O.Count);
				O.DurationHours = Rng.FRand() < 0.5f ? 24 : 36;
			}
			else
			{
				// a dish of the current kitchen level
				const FFVItemDef* I = FVSGHome::PickWeighted(Dishes, DishW, Rng);
				if (!I) { continue; }
				O.ItemId = I->Id;
				O.Count = 1;
				Value = float(FMath::Max(1, I->SellPrice));
				const int32 D = Rng.RandRange(0, 2);
				O.DurationHours = D == 0 ? 24 : (D == 1 ? 36 : 48);
			}
			// two offers never ask for the same thing
			const FName ItemId = O.ItemId;
			const bool bSmoked = O.bSmoked;
			const bool bDup = Orders.ContainsByPredicate([&](const FFVOrder& E)
			{
				return E.State == uint8(EFVOrderState::Offered) && E.ItemId == ItemId && E.bSmoked == bSmoked;
			});
			if (!bDup) { break; }
		}
		if (O.ItemId.IsNone()) { continue; }

		O.Reward = FMath::Max(5, FMath::RoundToInt((Value * Rng.FRandRange(1.6f, 2.2f) + 20.f) / 5.f) * 5);
		O.Deposit = FMath::Max(10, FMath::RoundToInt(float(O.Reward) * 0.25f));
		if (Rng.FRand() < 0.2f)
		{
			const FExtra& X = Extras[Rng.RandRange(0, int32(UE_ARRAY_COUNT(Extras)) - 1)];
			if (FVDB::FindItem(FName(X.Id)))
			{
				O.RewardItem = FName(X.Id);
				O.RewardItemCount = X.Count;
			}
		}
		O.Customer = Customers[Rng.RandRange(0, int32(UE_ARRAY_COUNT(Customers)) - 1)];
		FString IdStr = FString::Printf(TEXT("ord_%d_%d"), Day, n);
		while (Orders.ContainsByPredicate([&](const FFVOrder& E) { return E.Id == FName(*IdStr); })) { IdStr += TEXT("b"); }
		O.Id = FName(*IdStr);
		O.State = uint8(EFVOrderState::Offered);
		O.Deadline = 0.0;
		Orders.Add(O);
	}
}

EFVBuyResult UFVSaveGame::TakeOrder(FName OrderId, double Now)
{
	FFVOrder* O = Orders.FindByPredicate([&](const FFVOrder& E) { return E.Id == OrderId; });
	if (!O || !FMath::IsFinite(Now)) { return EFVBuyResult::Invalid; }
	if (O->State == uint8(EFVOrderState::Taken)) { return EFVBuyResult::AlreadyOwned; }
	if (O->State != uint8(EFVOrderState::Offered)) { return EFVBuyResult::Invalid; }
	if (TakenOrders() >= FVSGHome::MaxTakenOrders) { return EFVBuyResult::NoRoom; }
	const int64 Deposit = FMath::Max(0, O->Deposit);
	if (!CanAfford(Deposit) || !Spend(Deposit)) { return EFVBuyResult::NotEnoughMoney; }
	O->State = uint8(EFVOrderState::Taken);
	O->Deadline = Now + double(FMath::Max(1, O->DurationHours)) * 60.0;
	return EFVBuyResult::Ok;
}

namespace FVSGHome
{
	/** Stacks that satisfy an order: fish (quality >= min, smoked exactly when requested) or items (quality >= min). */
	bool OrderAccepts(const FFVOrder& O, const FFVInvStack& S)
	{
		if (S.ItemId != O.ItemId || S.Count <= 0 || S.Quality < O.MinQuality) { return false; }
		if (FVDB::IsFishId(O.ItemId)) { return IsSmoked(S) == O.bSmoked; }
		return true;
	}
}

bool UFVSaveGame::CanDeliver(const FFVOrder& O) const
{
	if (O.State != uint8(EFVOrderState::Taken) || O.ItemId.IsNone()) { return false; }
	const TArray<FFVInvStack>& Src = FVDB::IsFishId(O.ItemId) ? Keepnet : Backpack;
	int64 Have = 0;
	for (const FFVInvStack& S : Src)
	{
		if (FVSGHome::OrderAccepts(O, S)) { Have += S.Count; }
	}
	return Have >= FMath::Max(1, O.Count);
}

EFVBuyResult UFVSaveGame::DeliverOrder(FName OrderId)
{
	const int32 Idx = Orders.IndexOfByPredicate([&](const FFVOrder& E) { return E.Id == OrderId; });
	if (Idx == INDEX_NONE || Orders[Idx].State != uint8(EFVOrderState::Taken)) { return EFVBuyResult::Invalid; }
	const FFVOrder O = Orders[Idx];
	if (!CanDeliver(O)) { return EFVBuyResult::NotEnoughMaterials; }

	const bool bFish = FVDB::IsFishId(O.ItemId);
	const TArray<FFVInvStack> OldKeepnet = Keepnet;
	const TArray<FFVInvStack> OldBackpack = Backpack;
	TArray<FFVInvStack>& Src = bFish ? Keepnet : Backpack;
	FVSGHome::TakeUnits(Src, FMath::Max(1, O.Count), [&O](const FFVInvStack& S) { return FVSGHome::OrderAccepts(O, S); },
		[](const FFVInvStack& S) { return int64(S.Quality); });   // the lowest acceptable quality goes first
	if (!O.RewardItem.IsNone() && O.RewardItemCount > 0 && !GiveItem(O.RewardItem, O.RewardItemCount))
	{
		Keepnet = OldKeepnet;
		Backpack = OldBackpack;
		return EFVBuyResult::NoRoom;
	}
	Earn(FMath::Max(0, O.Reward), true);
	Earn(FMath::Max(0, O.Deposit), false);   // the deposit comes back
	Orders[Idx].State = uint8(EFVOrderState::Done);
	Stats.OrdersDone = FVSGHome::SatAdd32(Stats.OrdersDone, 1);
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::TakenOrders() const
{
	int32 N = 0;
	for (const FFVOrder& O : Orders) { if (O.State == uint8(EFVOrderState::Taken)) { ++N; } }
	return N;
}

// ================================================================================================
// House decor
// ================================================================================================
EFVBuyResult UFVSaveGame::BuyDecor(FName Id)
{
	const FFVDecorDef* D = FVDB::FindDecor(Id);
	if (!D) { return EFVBuyResult::Invalid; }
	if (OwnsDecor(Id)) { return EFVBuyResult::AlreadyOwned; }
	if (D->Price <= 0) { return EFVBuyResult::NotForSale; }
	if (GetLevel() < D->MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!CanAfford(D->Price) || !Spend(D->Price)) { return EFVBuyResult::NotEnoughMoney; }
	OwnedDecor.Add(Id);
	EquipDecor(Id);
	UE_LOG(LogFV, Log, TEXT("FV: bought decor %s"), *Id.ToString());
	return EFVBuyResult::Ok;
}

void UFVSaveGame::GrantDecor(FName Id)
{
	if (!FVDB::FindDecor(Id) || OwnsDecor(Id)) { return; }
	OwnedDecor.Add(Id);
}

bool UFVSaveGame::EquipDecor(FName Id)
{
	const FFVDecorDef* D = FVDB::FindDecor(Id);
	if (!D || !OwnsDecor(Id)) { return false; }
	const EFVDecorSlot Slot = D->Slot;
	EquippedDecor.RemoveAll([Slot](const FName& E)
	{
		const FFVDecorDef* Other = FVDB::FindDecor(E);
		return !Other || Other->Slot == Slot;
	});
	EquippedDecor.Add(Id);
	return true;
}

void UFVSaveGame::UnequipDecor(EFVDecorSlot Slot)
{
	if (Slot == EFVDecorSlot::Wallpaper || Slot == EFVDecorSlot::Floor) { return; }   // walls & floor are never bare
	EquippedDecor.RemoveAll([Slot](const FName& E)
	{
		const FFVDecorDef* D = FVDB::FindDecor(E);
		return !D || D->Slot == Slot;
	});
}

FName UFVSaveGame::GetEquippedDecor(EFVDecorSlot Slot) const
{
	for (const FName& Id : EquippedDecor)
	{
		const FFVDecorDef* D = FVDB::FindDecor(Id);
		if (D && D->Slot == Slot) { return Id; }
	}
	return NAME_None;
}
