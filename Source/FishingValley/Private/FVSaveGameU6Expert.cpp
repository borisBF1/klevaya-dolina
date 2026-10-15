// UPDATE 6 [DATA-EXPERT] — profile logic of expert fishing (FVSaveGame.h «UPDATE 6 [DATA-EXPERT]»): expert tackle,
// saved configurations, fish facts (Р33), legend moves seen, expert records. Owner: DATA-EXPERT.

#include "FVSaveGame.h"
#include "FVDatabase.h"

namespace FVSaveGameU6ExpertImpl
{
	constexpr int32 MaxConfigs = 8;
	constexpr int32 MaxConfigName = 24;
	constexpr int32 MaxLegendMoves = 32;

	FName FactKey(FName FishId, int32 FactIndex)
	{
		return FName(*FString::Printf(TEXT("%s:%d"), *FishId.ToString(), FactIndex));
	}

	/** "<fishId>:<index>" -> parts (false when malformed). */
	bool SplitKey(FName Key, FName& OutFish, int32& OutIndex)
	{
		FString L, R;
		if (!Key.ToString().Split(TEXT(":"), &L, &R, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { return false; }
		if (L.IsEmpty() || R.IsEmpty() || !R.IsNumeric()) { return false; }
		OutFish = FName(*L);
		OutIndex = FCString::Atoi(*R);
		return OutIndex >= 0;
	}

	EFVQuality QualityOf(uint8 Q) { return EFVQuality(FMath::Min<uint8>(Q, uint8(EFVQuality::Iridium))); }

	/** Expert tackle slot of an item (Count = not expert tackle). */
	EFVItemKind SlotOf(FName ItemId)
	{
		const FFVItemDef* D = FVDB::FindItem(ItemId);
		if (!D || !D->bExpert) { return EFVItemKind::Count; }
		switch (D->Kind)
		{
		case EFVItemKind::Line: return D->ExpertKind > 0 && D->ExpertKind < uint8(EFVExpertLine::Count) ? EFVItemKind::Line : EFVItemKind::Count;
		case EFVItemKind::Float: return D->ExpertKind > 0 && D->ExpertKind < uint8(EFVExpertFloat::Count) ? EFVItemKind::Float : EFVItemKind::Count;
		case EFVItemKind::Reel: return D->ExpertKind > 0 && D->ExpertKind < uint8(EFVExpertReel::Count) ? EFVItemKind::Reel : EFVItemKind::Count;
		default: return EFVItemKind::Count;
		}
	}

	FName* SlotRef(FFVExpertLoadout& L, EFVItemKind Slot)
	{
		switch (Slot)
		{
		case EFVItemKind::Line: return &L.Line;
		case EFVItemKind::Float: return &L.Float;
		case EFVItemKind::Reel: return &L.Reel;
		default: return nullptr;
		}
	}

	bool IsEmpty(const FFVExpertLoadout& L) { return L.Line.IsNone() && L.Float.IsNone() && L.Reel.IsNone(); }

	bool IsExpertRod(FName RodId)
	{
		const FFVRodDef* R = FVDB::FindRod(RodId);
		return R && R->bExpert;
	}
}

void UFVSaveGame::FixupExpert()
{
	using namespace FVSaveGameU6ExpertImpl;

	// ---- loadouts: only owned expert rods, only expert tackle of the right slot
	TArray<FName> DropRods;
	for (TPair<FName, FFVExpertLoadout>& P : U6.ExpertLoadouts)
	{
		if (!IsExpertRod(P.Key) || !OwnsRod(P.Key)) { DropRods.Add(P.Key); continue; }
		FFVExpertLoadout& L = P.Value;
		if (!L.Line.IsNone() && SlotOf(L.Line) != EFVItemKind::Line) { L.Line = NAME_None; }
		if (!L.Float.IsNone() && SlotOf(L.Float) != EFVItemKind::Float) { L.Float = NAME_None; }
		if (!L.Reel.IsNone() && SlotOf(L.Reel) != EFVItemKind::Reel) { L.Reel = NAME_None; }
		if (IsEmpty(L)) { DropRods.Add(P.Key); }
	}
	for (const FName& Id : DropRods) { U6.ExpertLoadouts.Remove(Id); }

	// ---- configurations: known rods, valid tackle / bait ids, sane names, at most MaxConfigs
	for (int32 i = U6.Configs.Num() - 1; i >= 0; --i)
	{
		FFVSavedConfig& C = U6.Configs[i];
		if (!FVDB::FindRod(C.Rod)) { U6.Configs.RemoveAt(i); continue; }
		const bool bExpertRod = IsExpertRod(C.Rod);
		if (!C.Line.IsNone() && (!bExpertRod || SlotOf(C.Line) != EFVItemKind::Line)) { C.Line = NAME_None; }
		if (!C.Float.IsNone() && (!bExpertRod || SlotOf(C.Float) != EFVItemKind::Float)) { C.Float = NAME_None; }
		if (!C.Reel.IsNone() && (!bExpertRod || SlotOf(C.Reel) != EFVItemKind::Reel)) { C.Reel = NAME_None; }
		if (!C.Bait.IsNone() && !FVDB::FindBait(C.Bait)) { C.Bait = NAME_None; }
		C.Name = C.Name.TrimStartAndEnd().Left(MaxConfigName);
		if (C.Name.IsEmpty()) { C.Name = FString::Printf(TEXT("Набор %d"), i + 1); }
	}
	if (U6.Configs.Num() > MaxConfigs) { U6.Configs.SetNum(MaxConfigs); }

	// ---- facts: known expert fish, index inside its facts, no duplicates
	{
		TArray<FName> Clean;
		for (const FName& K : U6.KnownFacts)
		{
			FName Fish;
			int32 Index = INDEX_NONE;
			if (SplitKey(K, Fish, Index) && Index < FVDBU6::Facts(Fish).Num()) { Clean.AddUnique(FactKey(Fish, Index)); }
		}
		U6.KnownFacts = MoveTemp(Clean);
	}

	// ---- legend moves seen / furthest move: expert legends only
	{
		TArray<FName> Clean;
		for (const FName& K : U6.ExpertMovesSeen)
		{
			FName Fish;
			int32 Index = INDEX_NONE;
			const FFVExpertFishDef* E = SplitKey(K, Fish, Index) ? FVDBU6::FindExpertFish(Fish) : nullptr;
			if (E && E->Legend != EFVExpertLegend::None && Index < MaxLegendMoves) { Clean.AddUnique(FactKey(Fish, Index)); }
		}
		U6.ExpertMovesSeen = MoveTemp(Clean);
	}
	{
		TArray<FName> Drop;
		for (TPair<FName, int32>& P : U6.ExpertBestMove)
		{
			const FFVExpertFishDef* E = FVDBU6::FindExpertFish(P.Key);
			if (!E || E->Legend == EFVExpertLegend::None) { Drop.Add(P.Key); continue; }
			P.Value = FMath::Clamp(P.Value, 0, MaxLegendMoves);
		}
		for (const FName& Id : Drop) { U6.ExpertBestMove.Remove(Id); }
	}

	const uint8 TypeBits = uint8((1u << uint8(EFVExpertRodType::Count)) - 1u);
	U6.ExpertTypesCaught &= TypeBits;
	U6.ExpertTypesTaught &= TypeBits;
}

bool UFVSaveGame::IsExpertRodEquipped() const
{
	const FFVRodDef* R = FVDB::FindRod(EquippedRod);
	return R && R->bExpert;
}

FFVExpertLoadout UFVSaveGame::GetExpertLoadout(FName RodId) const
{
	const FFVExpertLoadout* L = U6.ExpertLoadouts.Find(RodId);
	return L ? *L : FFVExpertLoadout();
}

EFVBuyResult UFVSaveGame::InstallExpertTackle(FName RodId, FName ItemId)
{
	using namespace FVSaveGameU6ExpertImpl;
	if (!IsExpertRod(RodId) || !OwnsRod(RodId)) { return EFVBuyResult::Invalid; }
	const EFVItemKind Slot = SlotOf(ItemId);
	if (Slot == EFVItemKind::Count) { return EFVBuyResult::Invalid; }   // classic tackle never goes on an expert rod

	const FFVExpertLoadout Cur = GetExpertLoadout(RodId);
	const FName Old = Slot == EFVItemKind::Line ? Cur.Line : Slot == EFVItemKind::Float ? Cur.Float : Cur.Reel;
	if (Old == ItemId) { return EFVBuyResult::AlreadyOwned; }

	// the lowest-quality stack of the new item leaves the backpack
	int32 Stack = INDEX_NONE;
	for (int32 i = 0; i < Backpack.Num(); ++i)
	{
		if (Backpack[i].ItemId == ItemId && Backpack[i].Count > 0 && (Stack == INDEX_NONE || Backpack[i].Quality < Backpack[Stack].Quality)) { Stack = i; }
	}
	if (Stack == INDEX_NONE) { return EFVBuyResult::NotEnoughMaterials; }
	const FFVInvStack Taken = Backpack[Stack];
	if (!RemoveFrom(EFVContainer::Backpack, Stack, 1)) { return EFVBuyResult::NotEnoughMaterials; }

	// the old one must fit back (all or nothing)
	const bool bReturnOld = !Old.IsNone() && FVDB::IsItemId(Old);
	if (bReturnOld)
	{
		if (!CanAddTo(EFVContainer::Backpack, Old, 1))
		{
			AddTo(EFVContainer::Backpack, Taken.ItemId, 1, Taken.Quality, Taken.Flags);
			return EFVBuyResult::NoRoom;
		}
		AddTo(EFVContainer::Backpack, Old, 1);
	}
	FFVExpertLoadout& L = U6.ExpertLoadouts.FindOrAdd(RodId);
	if (FName* Ref = SlotRef(L, Slot)) { *Ref = ItemId; }
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::RemoveExpertTackle(FName RodId, EFVItemKind Slot)
{
	using namespace FVSaveGameU6ExpertImpl;
	if (!IsExpertRod(RodId) || !OwnsRod(RodId)) { return EFVBuyResult::Invalid; }
	FFVExpertLoadout* L = U6.ExpertLoadouts.Find(RodId);
	FName* Ref = L ? SlotRef(*L, Slot) : nullptr;
	if (!Ref || Ref->IsNone()) { return EFVBuyResult::Invalid; }
	if (FVDB::IsItemId(*Ref))
	{
		if (!CanAddTo(EFVContainer::Backpack, *Ref, 1)) { return EFVBuyResult::NoRoom; }
		AddTo(EFVContainer::Backpack, *Ref, 1);
	}
	*Ref = NAME_None;
	if (IsEmpty(*L)) { U6.ExpertLoadouts.Remove(RodId); }
	return EFVBuyResult::Ok;
}

FFVExpertStats UFVSaveGame::GetExpertStats(FName RodId, double Now) const
{
	const FFVExpertLoadout L = GetExpertLoadout(RodId);
	if (const FFVItemDef* Food = GetActiveFood(Now))
	{
		const FFVExpertStats Buff = Food->ExpertBuff.Scaled(FVDB::FoodPower(FVSaveGameU6ExpertImpl::QualityOf(ActiveFoodQuality)));
		return FVDBU6::ComposeExpertStats(RodId, L.Line, L.Float, L.Reel, &Buff);
	}
	return FVDBU6::ComposeExpertStats(RodId, L.Line, L.Float, L.Reel, nullptr);
}

int32 UFVSaveGame::SaveConfig(const FString& Name)
{
	using namespace FVSaveGameU6ExpertImpl;
	if (!FVDB::FindRod(EquippedRod) || !OwnsRod(EquippedRod)) { return INDEX_NONE; }
	FFVSavedConfig C;
	C.Name = Name.TrimStartAndEnd().Left(MaxConfigName);
	C.Rod = EquippedRod;
	if (IsExpertRod(EquippedRod))
	{
		const FFVExpertLoadout L = GetExpertLoadout(EquippedRod);
		C.Line = L.Line;
		C.Float = L.Float;
		C.Reel = L.Reel;
	}
	C.Bait = EquippedBait;

	// the same name overwrites that configuration
	if (!C.Name.IsEmpty())
	{
		for (int32 i = 0; i < U6.Configs.Num(); ++i)
		{
			if (U6.Configs[i].Name.Equals(C.Name, ESearchCase::IgnoreCase))
			{
				U6.Configs[i] = C;
				return i;
			}
		}
	}
	if (U6.Configs.Num() >= MaxConfigs) { return INDEX_NONE; }
	if (C.Name.IsEmpty()) { C.Name = FString::Printf(TEXT("Набор %d"), U6.Configs.Num() + 1); }
	return U6.Configs.Add(C);
}

EFVBuyResult UFVSaveGame::ApplyConfig(int32 Index)
{
	using namespace FVSaveGameU6ExpertImpl;
	if (!U6.Configs.IsValidIndex(Index)) { return EFVBuyResult::Invalid; }
	const FFVSavedConfig C = U6.Configs[Index];
	if (!EquipRod(C.Rod)) { return EFVBuyResult::Invalid; }

	// tackle: install / remove what differs; a missing item leaves the slot as it is (result says so)
	EFVBuyResult Result = EFVBuyResult::Ok;
	if (IsExpertRod(C.Rod))
	{
		const EFVItemKind Slots[] = { EFVItemKind::Line, EFVItemKind::Float, EFVItemKind::Reel };
		const FName Want[] = { C.Line, C.Float, C.Reel };
		for (int32 i = 0; i < 3; ++i)
		{
			FFVExpertLoadout Cur = GetExpertLoadout(C.Rod);
			const FName* Have = SlotRef(Cur, Slots[i]);
			if (!Have || *Have == Want[i]) { continue; }
			const EFVBuyResult R = Want[i].IsNone() ? RemoveExpertTackle(C.Rod, Slots[i]) : InstallExpertTackle(C.Rod, Want[i]);
			if (R != EFVBuyResult::Ok && Result == EFVBuyResult::Ok) { Result = R; }
		}
	}
	if (C.Bait.IsNone() || BaitCount(C.Bait) > 0)
	{
		EquipBait(C.Bait);
	}
	else if (Result == EFVBuyResult::Ok)
	{
		Result = EFVBuyResult::NotEnoughMaterials;   // the bait of the configuration has run out
	}
	return Result;
}

void UFVSaveGame::DeleteConfig(int32 Index)
{
	if (U6.Configs.IsValidIndex(Index))
	{
		U6.Configs.RemoveAt(Index);
	}
}

bool UFVSaveGame::KnowsFact(FName FishId, int32 FactIndex) const
{
	return U6.KnownFacts.Contains(FVSaveGameU6ExpertImpl::FactKey(FishId, FactIndex));
}

bool UFVSaveGame::LearnFact(FName FishId, int32 FactIndex)
{
	if (FishId.IsNone() || FactIndex < 0 || KnowsFact(FishId, FactIndex))
	{
		return false;
	}
	U6.KnownFacts.Add(FVSaveGameU6ExpertImpl::FactKey(FishId, FactIndex));
	return true;
}

int32 UFVSaveGame::KnownFactCount(FName FishId) const
{
	const int32 N = FVDBU6::Facts(FishId).Num();
	int32 Known = 0;
	for (int32 i = 0; i < N; ++i)
	{
		if (KnowsFact(FishId, i)) { ++Known; }
	}
	return Known;
}

bool UFVSaveGame::IsFishStudied(FName FishId) const
{
	const int32 N = FVDBU6::Facts(FishId).Num();
	return N > 0 && KnownFactCount(FishId) >= N;
}

int32 UFVSaveGame::TotalFactsKnown() const
{
	return U6.KnownFacts.Num();
}

bool UFVSaveGame::PickNextFact(FName PreferFish, FRandomStream& Rng, FName& OutFish, int32& OutFactIndex) const
{
	OutFish = NAME_None;
	OutFactIndex = INDEX_NONE;
	// facts are stored in the reveal order (place/time -> type -> tackle): the next one is the first unknown
	auto FirstUnknown = [this](FName FishId) -> int32
	{
		const int32 N = FVDBU6::Facts(FishId).Num();
		for (int32 i = 0; i < N; ++i)
		{
			if (!KnowsFact(FishId, i)) { return i; }
		}
		return INDEX_NONE;
	};

	if (!PreferFish.IsNone())
	{
		const int32 I = FirstUnknown(PreferFish);
		if (I != INDEX_NONE)
		{
			OutFish = PreferFish;
			OutFactIndex = I;
			return true;
		}
	}

	// otherwise a fish of an opened place (else any); half-known fish are twice as likely, so the hints add up
	TArray<FName, TInlineAllocator<32>> Ids;
	TArray<float, TInlineAllocator<32>> Weights;
	float Total = 0.f;
	for (int32 Pass = 0; Pass < 2 && Ids.Num() == 0; ++Pass)
	{
		for (const FFVExpertFishDef& E : FVDBU6::ExpertFish())
		{
			if (FirstUnknown(E.FishId) == INDEX_NONE) { continue; }
			if (Pass == 0)
			{
				const FFVFishDef* F = FVDB::FindFish(E.FishId);
				if (!F || !IsLocationUnlocked(F->Location)) { continue; }
			}
			const float W = KnownFactCount(E.FishId) > 0 ? 2.f : 1.f;
			Ids.Add(E.FishId);
			Weights.Add(W);
			Total += W;
		}
	}
	if (Ids.Num() == 0 || Total <= 0.f) { return false; }

	float Roll = Rng.FRandRange(0.f, Total);
	int32 Pick = Ids.Num() - 1;
	for (int32 i = 0; i < Ids.Num(); ++i)
	{
		Roll -= Weights[i];
		if (Roll <= 0.f) { Pick = i; break; }
	}
	OutFish = Ids[Pick];
	OutFactIndex = FirstUnknown(OutFish);
	return OutFactIndex != INDEX_NONE;
}

void UFVSaveGame::MarkExpertMoveSeen(FName LegendFishId, int32 MoveIndex)
{
	if (!LegendFishId.IsNone() && MoveIndex >= 0)
	{
		U6.ExpertMovesSeen.AddUnique(FName(*FString::Printf(TEXT("%s:%d"), *LegendFishId.ToString(), MoveIndex)));
	}
}

bool UFVSaveGame::HasSeenExpertMove(FName LegendFishId, int32 MoveIndex) const
{
	return U6.ExpertMovesSeen.Contains(FName(*FString::Printf(TEXT("%s:%d"), *LegendFishId.ToString(), MoveIndex)));
}

void UFVSaveGame::RecordExpertCatch(const FFVCatchResult& R)
{
	if (!R.bExpert || !R.Fish) { return; }
	if (R.RodType < uint8(EFVExpertRodType::Count))
	{
		U6.ExpertTypesCaught |= uint8(1u << R.RodType);
	}
	if (R.Fish->bExpert && R.Fish->ExpertLegend != EFVExpertLegend::None)
	{
		if (R.bFlawless) { U6.bFlawlessLegend = true; }
		if (R.Quality == EFVQuality::Iridium) { U6.bIridiumLegend = true; }
	}
}
