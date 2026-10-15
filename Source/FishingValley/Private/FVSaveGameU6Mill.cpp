// UPDATE 6 [MILL] — «Старая мельница» (Docs/UPDATE6_FOREST_CITY.md §18, Р53): profile state and mutators
// (UFVSaveGame «[MILL]» block, state U6.Mill). Same rules as update 2: a failed mutator changes nothing; callers call
// UFVGame::NotifyProfileChanged(). "Now" = absolute game minutes, "played" = U6.PlayedMinutes (real minutes).

#include "FVSaveGame.h"
#include "FVDatabase.h"

namespace FVSaveGameMillImpl
{
	void SizeDecks(FFVMillState& M)
	{
		if (M.DeckUntil.Num() != FVMill::FeedDecks) { M.DeckUntil.SetNumZeroed(FVMill::FeedDecks); }
	}
}

void UFVSaveGame::FixupMill()
{
	using namespace FVSaveGameMillImpl;
	FFVMillState& M = U6.Mill;
	int32 Fixes = 0;
	const int32 Stages = FVMill::MillStages().Num();
	if (M.Stage < 0 || M.Stage > Stages) { M.Stage = FMath::Clamp(M.Stage, 0, Stages); ++Fixes; }
	if (M.BeaverFeeds < 0 || M.BeaverFeeds > FVMill::BeaverFeedsNeeded)
	{
		M.BeaverFeeds = FMath::Clamp(M.BeaverFeeds, 0, FVMill::BeaverFeedsNeeded);
		++Fixes;
	}
	if (!FMath::IsFinite(M.BeaverLastFeed) || M.BeaverLastFeed > U6.PlayedMinutes + 1.0) { M.BeaverLastFeed = -1000.0; ++Fixes; }
	if (M.DeckUntil.Num() != FVMill::FeedDecks) { SizeDecks(M); ++Fixes; }
	for (double& D : M.DeckUntil)
	{
		if (!FMath::IsFinite(D) || D < 0.0) { D = 0.0; ++Fixes; }
	}
	// the pen: known fish only, one slot per species, at most PenSlots
	TSet<FName> Seen;
	const int32 Before = M.Pen.Num();
	M.Pen.RemoveAll([&Seen](const FFVMillPenSlot& S)
	{
		const FFVFishDef* F = FVDB::FindFish(S.Fish);
		bool bDup = false;
		Seen.Add(S.Fish, &bDup);
		return !F || F->bExpert || bDup || !FMath::IsFinite(S.Until) || S.Until <= 0.0;
	});
	if (M.Pen.Num() > FVMill::PenSlots) { M.Pen.SetNum(FVMill::PenSlots); }
	if (M.Pen.Num() != Before) { ++Fixes; }
	if (!FMath::IsFinite(M.TeaExtended)) { M.TeaExtended = 0.0; ++Fixes; }
	M.FlourMilled = FMath::Max(0, M.FlourMilled);
	M.PenReleased = FMath::Max(0, M.PenReleased);
	// a mill unlocked without the bridge (hand-edited save) is closed again; the bridge alone never unlocks it here
	if (!M.bBridgeRepaired && UnlockedLocations.Contains(FVMill::MillId()))
	{
		UnlockedLocations.Remove(FVMill::MillId());
		if (FVMill::IsMillId(CurrentLocation)) { CurrentLocation = FVDBU6::ForestId(); }
		++Fixes;
	}
	if (Fixes > 0) { UE_LOG(LogFV, Warning, TEXT("FV: mill state repaired (%d fixes)"), Fixes); }
}

// ================================================================================================
// Bridge / mill stages
// ================================================================================================
EFVBuyResult UFVSaveGame::RepairBridge()
{
	if (U6.Mill.bBridgeRepaired) { return EFVBuyResult::AlreadyOwned; }
	const FFVBoatStage& S = FVMill::BridgeRepair();
	if (GetLevel() < S.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!HasIngredients(S.Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	if (S.Coins > 0 && !CanAfford(S.Coins)) { return EFVBuyResult::NotEnoughMoney; }
	if (!ConsumeIngredients(S.Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	if (S.Coins > 0) { Spend(S.Coins); }
	U6.Mill.bBridgeRepaired = true;
	UE_LOG(LogFV, Log, TEXT("FV: mill bridge repaired"));
	return EFVBuyResult::Ok;
}

bool UFVSaveGame::CanOpenMill() const
{
	return U6.Mill.bBridgeRepaired && GetLevel() >= FVMill::MinLevel;
}

int32 UFVSaveGame::GetMillStage() const
{
	return FMath::Clamp(U6.Mill.Stage, 0, FVMill::MillStages().Num());
}

bool UFVSaveGame::IsMillWorking() const
{
	return GetMillStage() >= FVMill::MillStages().Num();
}

EFVBuyResult UFVSaveGame::CanRepairMillStage() const
{
	const TArray<FFVBoatStage>& Stages = FVMill::MillStages();
	const int32 Cur = GetMillStage();
	if (Cur >= Stages.Num()) { return EFVBuyResult::MaxLevel; }
	if (!IsLocationUnlocked(FVMill::MillId())) { return EFVBuyResult::StationRequired; }
	const FFVBoatStage& S = Stages[Cur];
	if (GetLevel() < S.MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (S.Coins > 0 && !CanAfford(S.Coins)) { return EFVBuyResult::NotEnoughMoney; }
	if (!HasIngredients(S.Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	return EFVBuyResult::Ok;
}

EFVBuyResult UFVSaveGame::RepairMillStage()
{
	const EFVBuyResult Can = CanRepairMillStage();
	if (Can != EFVBuyResult::Ok) { return Can; }
	const FFVBoatStage& S = FVMill::MillStages()[GetMillStage()];
	if (!ConsumeIngredients(S.Cost)) { return EFVBuyResult::NotEnoughMaterials; }
	if (S.Coins > 0) { Spend(S.Coins); }
	U6.Mill.Stage = GetMillStage() + 1;
	UE_LOG(LogFV, Log, TEXT("FV: mill stage %d repaired"), U6.Mill.Stage);
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::MaxGrindTimes() const
{
	return FMath::Max(0, CountItem(FVMill::RyeId()) / FVMill::RyePerFlour);
}

EFVBuyResult UFVSaveGame::GrindRye(int32 Times)
{
	if (Times <= 0) { return EFVBuyResult::Invalid; }
	if (!IsMillWorking()) { return EFVBuyResult::StationRequired; }
	TArray<FFVIngredient> In;
	FFVIngredient& I = In.AddDefaulted_GetRef();
	I.ItemId = FVMill::RyeId();
	I.Count = FVMill::RyePerFlour;
	if (!HasIngredients(In, Times)) { return EFVBuyResult::NotEnoughMaterials; }
	// the rye leaves the backpack first (its slot may free up); no room for the flour -> the rye goes back
	if (!ConsumeIngredients(In, Times)) { return EFVBuyResult::NotEnoughMaterials; }
	if (!AddTo(EFVContainer::Backpack, FVMill::FlourId(), Times))
	{
		// give the rye back (it may have come from the chest: a full backpack sends it there)
		if (!AddTo(EFVContainer::Backpack, FVMill::RyeId(), Times * FVMill::RyePerFlour))
		{
			AddTo(EFVContainer::Chest, FVMill::RyeId(), Times * FVMill::RyePerFlour);
		}
		return EFVBuyResult::NoRoom;
	}
	U6.Mill.FlourMilled += Times;
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::MillSawBonus() const
{
	return IsMillWorking() ? FVMill::SawBonusBoards : 0;
}

// ================================================================================================
// Beavers / the Omut
// ================================================================================================
float UFVSaveGame::BeaverCooldownLeft() const
{
	const double Left = U6.Mill.BeaverLastFeed + double(FVMill::BeaverCooldownMinutes) - U6.PlayedMinutes;
	return float(FMath::Max(0.0, Left));
}

EFVBuyResult UFVSaveGame::FeedBeavers()
{
	FFVMillState& M = U6.Mill;
	if (M.BeaverFeeds >= FVMill::BeaverFeedsNeeded) { return EFVBuyResult::MaxLevel; }
	if (BeaverCooldownLeft() > 0.f) { return EFVBuyResult::Invalid; }
	TArray<FFVIngredient> In;
	FFVIngredient& I = In.AddDefaulted_GetRef();
	I.ItemId = FName(TEXT("res_branch"));
	I.Count = FVMill::BeaverBranches;
	if (!HasIngredients(In) || !ConsumeIngredients(In)) { return EFVBuyResult::NotEnoughMaterials; }
	M.BeaverFeeds = FMath::Min(FVMill::BeaverFeedsNeeded, M.BeaverFeeds + 1);
	M.BeaverLastFeed = U6.PlayedMinutes;
	if (M.BeaverFeeds >= FVMill::BeaverFeedsNeeded) { UE_LOG(LogFV, Log, TEXT("FV: the beaver dam is finished, the Omut is open")); }
	return EFVBuyResult::Ok;
}

bool UFVSaveGame::IsOmutOpen() const
{
	return U6.Mill.BeaverFeeds >= FVMill::BeaverFeedsNeeded;
}

// ================================================================================================
// Feeder decks
// ================================================================================================
EFVBuyResult UFVSaveGame::BaitFeedDeck(int32 Deck, FName BaitId, double Now)
{
	using namespace FVSaveGameMillImpl;
	if (Deck < 0 || Deck >= FVMill::FeedDecks || !FVMill::DeckBaits().Contains(BaitId)) { return EFVBuyResult::Invalid; }
	if (IsDeckBaited(Deck, Now)) { return EFVBuyResult::AlreadyOwned; }
	int32* Have = Baits.Find(BaitId);
	if (!Have || *Have <= 0) { return EFVBuyResult::NotEnoughMaterials; }
	--*Have;
	if (*Have <= 0) { Baits.Remove(BaitId); }
	SizeDecks(U6.Mill);
	U6.Mill.DeckUntil[Deck] = Now + double(FVMill::DeckGameMinutes);
	return EFVBuyResult::Ok;
}

bool UFVSaveGame::IsDeckBaited(int32 Deck, double Now) const
{
	return DeckMinutesLeft(Deck, Now) > 0.f;
}

float UFVSaveGame::DeckMinutesLeft(int32 Deck, double Now) const
{
	const TArray<double>& D = U6.Mill.DeckUntil;
	if (!D.IsValidIndex(Deck)) { return 0.f; }
	return float(FMath::Max(0.0, D[Deck] - Now));
}

// ================================================================================================
// The fish pen
// ================================================================================================
EFVBuyResult UFVSaveGame::ReleaseToPen(int32 AquariumIndex, double Now, FName& OutFish)
{
	OutFish = NAME_None;
	const TArray<FFVInvStack>& Aq = GetContainer(EFVContainer::Aquarium);
	if (!Aq.IsValidIndex(AquariumIndex) || Aq[AquariumIndex].Count <= 0) { return EFVBuyResult::Invalid; }
	const FName Fish = Aq[AquariumIndex].ItemId;
	const FFVFishDef* F = FVDB::FindFish(Fish);
	if (!F || F->bExpert || F->bLegendary) { return EFVBuyResult::Invalid; }
	PruneMill(Now);
	FFVMillState& M = U6.Mill;
	FFVMillPenSlot* Slot = M.Pen.FindByPredicate([Fish](const FFVMillPenSlot& S) { return S.Fish == Fish; });
	if (!Slot && M.Pen.Num() >= FVMill::PenSlots) { return EFVBuyResult::NoRoom; }
	if (!RemoveFrom(EFVContainer::Aquarium, AquariumIndex, 1)) { return EFVBuyResult::Invalid; }
	if (!Slot)
	{
		Slot = &M.Pen.AddDefaulted_GetRef();
		Slot->Fish = Fish;
	}
	Slot->Until = Now + double(FVMill::PenGameMinutes);
	++M.PenReleased;
	OutFish = Fish;
	return EFVBuyResult::Ok;
}

TArray<FName> UFVSaveGame::ActivePenFish(double Now) const
{
	TArray<FName> Out;
	for (const FFVMillPenSlot& S : U6.Mill.Pen)
	{
		if (S.Until > Now && !S.Fish.IsNone()) { Out.AddUnique(S.Fish); }
	}
	return Out;
}

float UFVSaveGame::PenMinutesLeft(FName FishId, double Now) const
{
	for (const FFVMillPenSlot& S : U6.Mill.Pen)
	{
		if (S.Fish == FishId) { return float(FMath::Max(0.0, S.Until - Now)); }
	}
	return 0.f;
}

void UFVSaveGame::PruneMill(double Now)
{
	using namespace FVSaveGameMillImpl;
	U6.Mill.Pen.RemoveAll([Now](const FFVMillPenSlot& S) { return S.Until <= Now; });
	SizeDecks(U6.Mill);
	for (double& D : U6.Mill.DeckUntil)
	{
		if (D <= Now) { D = 0.0; }
	}
}

// ================================================================================================
// Fog morning / the miller
// ================================================================================================
bool UFVSaveGame::ApplyFogTea(double Now, float ClockMinutes)
{
	if (!FVMill::IsFogMorning(ClockMinutes) || ActiveFood != FVMill::TeaId() || FoodExpires <= Now) { return false; }
	if (U6.Mill.TeaExtended > 0.0 && FMath::IsNearlyEqual(U6.Mill.TeaExtended, FoodExpires, 0.01)) { return false; }
	const FFVItemDef* Tea = FVDB::FindItem(ActiveFood);
	const double Extra = Tea ? double(FMath::Max(0.f, Tea->BuffMinutes)) : 0.0;
	if (Extra <= 0.0) { return false; }
	FoodExpires += Extra;   // «действует вдвое дольше»: the full duration once more, once per eaten tea
	U6.Mill.TeaExtended = FoodExpires;
	return true;
}

int64 UFVSaveGame::SellLuresToMiller(FName BaitId, int32 Count)
{
	const int32 Unit = FVMill::MillerLurePrice(BaitId);
	int32* Have = Baits.Find(BaitId);
	if (Unit <= 0 || Count <= 0 || !Have || *Have <= 0) { return 0; }
	const int32 N = FMath::Min(Count, *Have);
	*Have -= N;
	if (*Have <= 0) { Baits.Remove(BaitId); }
	const int64 Coins = int64(Unit) * N;
	Earn(Coins, true);
	return Coins;
}
