// Player profile: persistent progression + all profile-mutating logic.

#include "FVSaveGame.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"

// update 6: values of the update-6 achievement kinds (FVSaveGameU6.cpp, DATA-CORE)
namespace FVSaveGameU6Ach
{
	bool Values(const UFVSaveGame& S, const FFVAchievementDef& A, int64& OutCur, int64& OutTarget);
	bool IsClassicSpecies(const FFVFishDef& F);
}

namespace
{
	bool GFVAutoTestSlot = false;
	constexpr int32 FVSaveUserIndex = 0;
	constexpr int32 FVMaxStackCount = 9999;
	constexpr int32 FVMaxBaitCount = 99999;

	int32 SatAdd(int32 A, int64 B, int32 Max = MAX_int32)
	{
		return int32(FMath::Clamp<int64>(int64(A) + B, 0, Max));
	}

	int32 CountValidRods(const TArray<FName>& Ids)
	{
		int32 N = 0;
		for (const FName& Id : Ids) { if (FVDB::FindRod(Id)) { ++N; } }
		return N;
	}

	int32 CountValidCosmetics(const TArray<FName>& Ids)
	{
		int32 N = 0;
		for (const FName& Id : Ids) { if (FVDB::FindCosmetic(Id)) { ++N; } }
		return N;
	}

	int32 CountValidLocations(const TArray<FName>& Ids)
	{
		// fishing locations only (the home ids are always "unlocked" and never count)
		int32 N = 0;
		for (const FName& Id : Ids) { if (FVDB::LocationIndex(Id) != INDEX_NONE) { ++N; } }
		return N;
	}

	/** Current value and target of an achievement. */
	void AchievementValues(const UFVSaveGame& S, const FFVAchievementDef& A, int64& OutCur, int64& OutTarget)
	{
		OutTarget = FMath::Max(1, A.Target);
		OutCur = 0;
		switch (A.Kind)
		{
		case EFVAchKind::CatchTotal:      OutCur = S.Stats.FishCaught; break;
		case EFVAchKind::CatchSpecies:
		{
			// update 6 (§14): the classic species only — «Ихтиолог» stays reachable before the City
			for (const TPair<FName, FFVFishRecord>& P : S.Records)
			{
				const FFVFishDef* F = P.Value.Caught > 0 ? FVDB::FindFish(P.Key) : nullptr;
				if (F && FVSaveGameU6Ach::IsClassicSpecies(*F)) { ++OutCur; }
			}
			break;
		}
		case EFVAchKind::CatchFish:
		{
			const FFVFishRecord* R = S.GetRecord(A.Param);
			OutCur = R ? R->Caught : 0;
			break;
		}
		case EFVAchKind::CatchLegendary:  OutCur = S.Stats.Legendary; break;
		case EFVAchKind::PerfectTotal:    OutCur = S.Stats.Perfect; break;
		case EFVAchKind::TreasureTotal:   OutCur = S.Stats.Treasures; break;
		case EFVAchKind::IridiumTotal:    OutCur = S.Stats.Iridium; break;
		case EFVAchKind::EarnTotal:       OutCur = S.Stats.Earned; break;
		case EFVAchKind::ReachLevel:      OutCur = S.GetLevel(); break;
		case EFVAchKind::UnlockLocations: OutCur = CountValidLocations(S.UnlockedLocations); break;
		case EFVAchKind::CompleteLocation:
		{
			// Target = number of species living there; an unknown/empty location can never complete.
			const int32 NumHere = FVDB::FishAt(A.Param).Num();
			OutTarget = FMath::Max(1, NumHere);
			OutCur = NumHere > 0 ? S.SpeciesCaughtAt(A.Param) : 0;
			break;
		}
		case EFVAchKind::OwnRods:         OutCur = CountValidRods(S.OwnedRods); break;
		case EFVAchKind::OwnCosmetics:    OutCur = CountValidCosmetics(S.OwnedCosmetics); break;
		// ---- update 2
		case EFVAchKind::GatherTotal:     OutCur = S.Stats.Gathered; break;
		case EFVAchKind::CraftTotal:      OutCur = S.Stats.Crafted; break;
		case EFVAchKind::CookTotal:       OutCur = S.Stats.Cooked; break;
		case EFVAchKind::SmokeTotal:      OutCur = S.Stats.Smoked; break;
		case EFVAchKind::DigTotal:        OutCur = S.Stats.WormsDug; break;
		case EFVAchKind::NetFishTotal:    OutCur = S.Stats.NetFish; break;
		case EFVAchKind::OrdersDone:      OutCur = S.Stats.OrdersDone; break;
		case EFVAchKind::KnotsTotal:      OutCur = S.Stats.KnotsTied; break;
		case EFVAchKind::LuresDiscovered:
		{
			for (const FName& Id : S.DiscoveredLures)
			{
				const FFVBaitDef* B = FVDB::FindBait(Id);
				if (B && B->bLure) { ++OutCur; }
			}
			break;
		}
		case EFVAchKind::StationsBuilt:
		case EFVAchKind::StationsMaxed:
		{
			for (uint8 i = 0; i < uint8(EFVStation::Count); ++i)
			{
				const EFVStation St = EFVStation(i);
				if (St == EFVStation::Bed || St == EFVStation::Chest) { continue; }
				const int32 L = S.GetStationLevel(St);
				const int32 Max = FVDB::StationMaxLevel(St);
				if (A.Kind == EFVAchKind::StationsBuilt ? L >= 1 : (Max > 0 && L >= Max)) { ++OutCur; }
			}
			break;
		}
		case EFVAchKind::AquariumFish:
		{
			for (const FFVInvStack& St : S.Aquarium)
			{
				if (St.Count > 0 && FVDB::FindFish(St.ItemId)) { OutCur += St.Count; }
			}
			break;
		}
		case EFVAchKind::TrophiesMounted:
		{
			for (const FFVTrophy& T : S.Trophies) { if (FVDB::FindFish(T.FishId)) { ++OutCur; } }
			break;
		}
		case EFVAchKind::OwnDecor:
		{
			for (const FName& Id : S.OwnedDecor)
			{
				const FFVDecorDef* D = FVDB::FindDecor(Id);
				if (D && !D->bStarter) { ++OutCur; }
			}
			break;
		}
		default:
			FVSaveGameU6Ach::Values(S, A, OutCur, OutTarget);   // update 6 kinds (no-op for anything else)
			break;
		}
		OutCur = FMath::Max<int64>(0, OutCur);
	}

	/** First starter cosmetic of the given slot (NAME_None if there is none). */
	FName StarterCosmeticFor(EFVCosmeticSlot Slot)
	{
		for (const FName& Id : FVDB::StartCosmetics())
		{
			const FFVCosmeticDef* C = FVDB::FindCosmetic(Id);
			if (C && C->Slot == Slot) { return Id; }
		}
		return NAME_None;
	}

	/** Removes unknown and duplicate ids. Returns the number of removed entries. */
	template <typename TFind>
	int32 CleanIdList(TArray<FName>& Ids, TFind Find)
	{
		TArray<FName> Clean;
		Clean.Reserve(Ids.Num());
		for (const FName& Id : Ids)
		{
			if (Find(Id) && !Clean.Contains(Id)) { Clean.Add(Id); }
		}
		const int32 Removed = Ids.Num() - Clean.Num();
		Ids = MoveTemp(Clean);
		return Removed;
	}
}

// Update 2 repair helpers (named namespace: FVSaveGameHome.cpp has its own).
namespace FVSaveGameFix
{
	constexpr int32 MaxUnits = 9999;          // unit containers (fish): one stack per (id, quality, flags)
	constexpr int32 JunkMaxStack = 99;

	int32 MaxStackOf(FName Id)
	{
		if (const FFVItemDef* I = FVDB::FindItem(Id)) { return FMath::Max(1, I->MaxStack); }
		if (FVDB::FindJunk(Id)) { return JunkMaxStack; }
		return MaxUnits;
	}

	/** Fish and dishes keep their quality; junk and other items are always Normal. */
	uint8 NormQuality(FName Id, uint8 Q)
	{
		const uint8 Max = uint8(EFVQuality::Iridium);
		if (FVDB::FindFish(Id)) { return FMath::Min(Q, Max); }
		const FFVItemDef* I = FVDB::FindItem(Id);
		return (I && I->Kind == EFVItemKind::Food) ? FMath::Min(Q, Max) : uint8(0);
	}

	uint8 NormFlags(FName Id, uint8 F)
	{
		return FVDB::FindFish(Id) ? uint8(F & FVStackFlags::Smoked) : uint8(0);
	}

	/** Adds units ignoring capacity (migration / repairs never destroy player items): fills equal stacks up to
	    MaxStack, then appends new stacks. */
	void AddRaw(TArray<FFVInvStack>& Arr, FName Id, uint8 Q, uint8 F, int64 Count, int32 MaxStack)
	{
		if (Count <= 0 || Id.IsNone()) { return; }
		MaxStack = FMath::Max(1, MaxStack);
		for (FFVInvStack& St : Arr)
		{
			if (Count <= 0) { break; }
			if (St.ItemId != Id || St.Quality != Q || St.Flags != F || St.Count >= MaxStack) { continue; }
			const int32 Add = int32(FMath::Min<int64>(Count, MaxStack - St.Count));
			St.Count += Add;
			Count -= Add;
		}
		for (int32 Guard = 0; Count > 0 && Guard < 1000; ++Guard)
		{
			FFVInvStack& N = Arr.AddDefaulted_GetRef();
			N.ItemId = Id;
			N.Quality = Q;
			N.Flags = F;
			N.Count = int32(FMath::Min<int64>(Count, MaxStack));
			Count -= N.Count;
		}
	}

	/** Known things found in the wrong place; re-homed at the end of FixupAfterLoad. */
	struct FRejected
	{
		TArray<FFVInvStack> Fish;    // -> keepnet
		TArray<FFVInvStack> Items;   // -> backpack (junk & items)
		TMap<FName, int32> Baits;    // -> Baits map
	};

	void RejectStack(FRejected& Out, const FFVInvStack& St)
	{
		if (St.Count <= 0) { return; }
		if (FVDB::FindFish(St.ItemId)) { Out.Fish.Add(St); }
		else if (FVDB::FindJunk(St.ItemId) || FVDB::FindItem(St.ItemId)) { Out.Items.Add(St); }
		else if (FVDB::FindBait(St.ItemId))
		{
			int32& N = Out.Baits.FindOrAdd(St.ItemId);
			N = int32(FMath::Clamp<int64>(int64(N) + St.Count, 0, MAX_int32));
		}
	}

	/** Container repair: known ids of the right kind only (fish containers: fish; item containers: junk & items),
	    normalised quality/flags, merged equal stacks (re-split by max stack). bOneUnit = aquarium (1 live fish per
	    stack, never smoked). Wrong-kind entries go to Out. Returns the number of fixes. */
	int32 NormalizeStacks(TArray<FFVInvStack>& Arr, bool bFishOnly, bool bOneUnit, FRejected& Out)
	{
		struct FAcc { FName Id; uint8 Q; uint8 F; int64 N; };
		int32 Fixes = 0;
		TArray<FAcc> Acc;
		for (const FFVInvStack& St : Arr)
		{
			if (St.Count <= 0 || St.ItemId.IsNone()) { ++Fixes; continue; }
			const bool bFish = FVDB::FindFish(St.ItemId) != nullptr;
			const bool bOther = !bFish && (FVDB::FindJunk(St.ItemId) || FVDB::FindItem(St.ItemId));
			if (!bFish && !bOther)
			{
				RejectStack(Out, St);   // bait -> Baits map, unknown -> dropped
				++Fixes;
				continue;
			}
			if (bFish != bFishOnly || (bOneUnit && (St.Flags & FVStackFlags::Smoked) != 0))
			{
				RejectStack(Out, St);
				++Fixes;
				continue;
			}
			const uint8 Q = NormQuality(St.ItemId, St.Quality);
			const uint8 F = bOneUnit ? uint8(0) : NormFlags(St.ItemId, St.Flags);
			if (Q != St.Quality || F != St.Flags) { ++Fixes; }
			FAcc* A = Acc.FindByPredicate([&](const FAcc& E) { return E.Id == St.ItemId && E.Q == Q && E.F == F; });
			if (A) { A->N += St.Count; }
			else { Acc.Add({ St.ItemId, Q, F, int64(St.Count) }); }
		}
		TArray<FFVInvStack> Clean;
		Clean.Reserve(Arr.Num());
		for (const FAcc& A : Acc)
		{
			if (bOneUnit)
			{
				const int64 N = FMath::Min<int64>(A.N, 100);
				for (int64 i = 0; i < N; ++i) { AddRaw(Clean, A.Id, A.Q, A.F, 1, 1); }
			}
			else
			{
				AddRaw(Clean, A.Id, A.Q, A.F, FMath::Min<int64>(A.N, 1000000), bFishOnly ? MaxUnits : MaxStackOf(A.Id));
			}
		}
		Arr = MoveTemp(Clean);
		return Fixes;
	}

	/** Net catch: fish & junk only (quality normalised, never smoked), merged. */
	void NormalizeCatch(TArray<FFVInvStack>& Arr)
	{
		TArray<FFVInvStack> Clean;
		for (const FFVInvStack& St : Arr)
		{
			if (St.Count <= 0) { continue; }
			if (!FVDB::FindFish(St.ItemId) && !FVDB::FindJunk(St.ItemId)) { continue; }
			AddRaw(Clean, St.ItemId, NormQuality(St.ItemId, St.Quality), 0, St.Count, MaxUnits);
		}
		Arr = MoveTemp(Clean);
	}

	/** Net spots per fishing location (the world places the same number of posts). Update 6: the right pier spot (1)
	    became the boat (§7): a 0.5 net there goes back to the backpack (the nets block below); no nets in the expert
	    zones (Р35). Keep in sync with FVSGHome::NetSpotCount (FVSaveGameHome.cpp). */
	int32 NetSpotCount(FName Loc)
	{
		if (FVDBU6::IsExpertLocation(Loc) || FVDBU6::IsForestId(Loc)) { return 0; }
		if (FVMill::IsMillId(Loc)) { return FVMill::NetSpots; }   // §18: 3 on the pond + 1 in the Omut
		const FString L = Loc.ToString().ToLower();
		if (L == TEXT("pond") || L == TEXT("river") || L == TEXT("swamp")) { return 2; }
		if (L == TEXT("mountain") || L == TEXT("ocean")) { return 1; }
		return 0;
	}

	uint8 RollWeather() { return FMath::FRand() < 0.3f ? uint8(EFVWeather::Rain) : uint8(EFVWeather::Sunny); }
}

// ================================================================================================
// Update 4: online profile fields (name, motto, identity, profile stats)
// ================================================================================================
namespace FVSaveGameOnlineFix
{
	int64 NowUnix() { return FDateTime::UtcNow().ToUnixTimestamp(); }

	/** One line of player text: no control / invisible characters, trimmed, single spaces, at most MaxLen chars. */
	FString CleanLine(const FString& In, int32 MaxLen)
	{
		FString Out;
		Out.Reserve(In.Len());
		bool bSpace = false;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			TCHAR C = In[i];
			const uint32 U = uint32(C);
			if (U < 32u || FChar::IsWhitespace(C))
			{
				C = TCHAR(' ');
			}
			else if (U == 127u || (U >= 0x80u && U < 0xA0u) || (U >= 0xD800u && U <= 0xDFFFu) || (U >= 0xE000u && U <= 0xF8FFu)
				|| (U >= 0x200Bu && U <= 0x200Fu) || (U >= 0x2028u && U <= 0x202Eu) || U == 0xFEFFu)
			{
				continue;
			}
			if (C == TCHAR(' '))
			{
				if (bSpace || Out.IsEmpty()) { continue; }
				bSpace = true;
			}
			else
			{
				bSpace = false;
			}
			Out.AppendChar(C);
		}
		Out.TrimEndInline();
		if (Out.Len() > MaxLen)
		{
			Out.LeftInline(MaxLen);
			Out.TrimEndInline();
		}
		return Out;
	}

	bool IsHexSecret(const FString& S)
	{
		if (S.Len() != 64) { return false; }
		for (int32 i = 0; i < S.Len(); ++i)
		{
			if (!FChar::IsHexDigit(S[i])) { return false; }
		}
		return true;
	}

	/** A shown fish (best catch / tournament biggest): a known species, a sane size and quality, or nothing. */
	bool CleanShownFish(FFVTrophy& T)
	{
		const FFVTrophy Before = T;
		if (T.FishId.IsNone() || !FVDB::FindFish(T.FishId) || !FMath::IsFinite(T.SizeCm) || T.SizeCm < 0.f)
		{
			T = FFVTrophy();
		}
		else
		{
			T.SizeCm = FMath::Min(T.SizeCm, 10000.f);
			T.Quality = FMath::Min<uint8>(T.Quality, uint8(EFVQuality::Iridium));
		}
		return T.FishId != Before.FishId || T.SizeCm != Before.SizeCm || T.Quality != Before.Quality;
	}
}

// ================================================================================================
// Lifecycle
// ================================================================================================
FString UFVSaveGame::GetSlotName()
{
	return GFVAutoTestSlot ? TEXT("FishingValley_AutoTest") : TEXT("FishingValley");
}

void UFVSaveGame::SetAutoTestSlot(bool bAutoTest)
{
	GFVAutoTestSlot = bAutoTest;
}

void UFVSaveGame::ResetToNewGame()
{
	FVDB::Init();

	Version = CurrentVersion;
	Money = FVDB::StartMoney();
	Xp = 0;

	Keepnet.Reset();
	KeepnetLevel = 0;

	OwnedRods.Reset();
	const FName StartRod = FVDB::StartRod();
	if (FVDB::FindRod(StartRod)) { OwnedRods.Add(StartRod); }
	EquippedRod = StartRod;

	// A pack of worms as a welcome gift, ready on the hook.
	Baits.Reset();
	EquippedBait = NAME_None;
	if (FVDB::Baits().Num() > 0)
	{
		const FFVBaitDef& Gift = FVDB::Baits()[0];
		AddBait(Gift.Id, FMath::Max(1, Gift.PackSize));
		EquippedBait = Gift.Id;
	}

	OwnedCosmetics.Reset();
	EquippedHat = NAME_None;
	EquippedOutfit = NAME_None;
	EquippedBobber = NAME_None;
	EquippedPet = NAME_None;
	for (const FName& Id : FVDB::StartCosmetics())
	{
		GrantCosmetic(Id);
		EquipCosmetic(Id);
	}

	// Update 4b: a new fisherman wakes up at home — the practice pond in the yard teaches the first cast, the village
	// pond (the first unlocked fishing spot) waits on the map.
	const FName StartLoc = FVDB::StartLocation();
	UnlockedLocations.Reset();
	UnlockedLocations.Add(StartLoc);
	CurrentLocation = FVDB::HomeId();
	OnboardingStep = 0;

	Records.Reset();
	CompletedAchievements.Reset();
	Stats = FFVStats();

	Day = 1;
	TimeMinutes = FV::DayStartMinutes;
	Weather = uint8(EFVWeather::Sunny);
	bSeenHelp = true;   // update 4b: the practice-pond onboarding replaced the "how to play" dialog (the guide stays)
	// SfxVolume / MusicVolume are player settings and survive a new game.

	// ---- update 2: house, backpack, crafts (bed & chest built, starter decor, 16-slot backpack, no shovel)
	Backpack.Reset();
	BackpackLevel = 0;
	Chest.Reset();
	IceCellar.Reset();
	Aquarium.Reset();
	Trophies.Reset();
	StationLevels.Reset();
	for (const FFVStationDef& S : FVDB::Stations())
	{
		if (S.bStartsBuilt && !S.Id.IsNone()) { StationLevels.Add(S.Id, 1); }
	}
	ShovelLevel = 0;
	RodLoadouts.Reset();
	ActiveFood = NAME_None;
	ActiveFoodQuality = 0;
	FoodExpires = 0.0;
	Nets.Reset();
	Smokehouse.Reset();
	SmokeFuel = 0.f;
	WormFarmWorms = 0;
	WormFarmCrawlers = 0;
	WormFarmDay = 0;
	Orders.Reset();
	OrdersDay = 0;
	Gathered.Reset();
	Dug.Reset();
	DiscoveredLures.Reset();
	OwnedDecor.Reset();
	EquippedDecor.Reset();
	for (const FName& Id : FVDB::StarterDecor())
	{
		GrantDecor(Id);
		EquipDecor(Id);
	}
	WeatherTomorrow = FVSaveGameFix::RollWeather();

	// ---- update 4: a new fisherman starts today. The name and the motto are the player's (UFVGame::NewGame copies
	// them over); the online identity is not: a new game registers as a new player.
	ProfileStartUnix = FVSaveGameOnlineFix::NowUnix();
	OnlineId = 0;
	OnlineTag = 0;
	OnlineSecret.Reset();
	CatchesByLocation.Reset();
	BestCatch = FFVTrophy();
	BestCatchPrice = 0;
	PendingSeasonBest = FFVTrophy();
	ClaimedMail.Reset();
	// ---- update 5
	LegendsMet.Reset();
	// ---- update 6
	ResetU6();

	FixupAfterLoad();
	UE_LOG(LogFV, Log, TEXT("FV: new game profile created (money %lld, rod %s, location %s)"),
		Money, *EquippedRod.ToString(), *CurrentLocation.ToString());
}

void UFVSaveGame::FixupAfterLoad()
{
	FVDB::Init();
	int32 Fixes = 0;
	FVSaveGameFix::FRejected Rejected;   // known things found in the wrong container, re-homed at the end

	// Version 0 = legacy v1 save (the field was never written: delta serialization against the CDO).
	const int32 Loaded = Version <= 0 ? 1 : Version;
	if (Version != CurrentVersion)
	{
		UE_LOG(LogFV, Log, TEXT("FV: upgrading save from version %d to %d"), Loaded, CurrentVersion);
	}
	if (Loaded < 2)
	{
		// ---- v1 -> v2: junk moves from the keepnet to the backpack (BEFORE the keepnet validation), the bed and the
		// chest stand in the house, the starter decor is owned & equipped, a forecast exists, the order board is fresh.
		TArray<FFVInvStack> Fish;
		for (const FFVInvStack& St : Keepnet)
		{
			if (FVDB::FindJunk(St.ItemId))
			{
				FVSaveGameFix::AddRaw(Backpack, St.ItemId, 0, 0, St.Count, FVSaveGameFix::MaxStackOf(St.ItemId));
			}
			else
			{
				Fish.Add(St);
			}
		}
		Keepnet = MoveTemp(Fish);
		for (const FFVStationDef& S : FVDB::Stations())
		{
			if (S.bStartsBuilt && !S.Id.IsNone()) { StationLevels.FindOrAdd(S.Id) = 1; }
		}
		for (const FName& Id : FVDB::StarterDecor())
		{
			GrantDecor(Id);
			EquipDecor(Id);
		}
		WeatherTomorrow = FVSaveGameFix::RollWeather();
		OrdersDay = 0;
		UE_LOG(LogFV, Log, TEXT("FV: migrated a v1 profile to v2 (house, backpack)"));
	}
	Version = CurrentVersion;

	if (Money < 0) { Money = 0; ++Fixes; }
	if (Xp < 0) { Xp = 0; ++Fixes; }

	// ---- keepnet: fish only (junk & items -> backpack, bait -> Baits), valid quality/flags, one stack per identity
	{
		const int32 NumLevels = FVDB::KeepnetLevels().Num();
		const int32 MaxLevel = FMath::Max(0, NumLevels - 1);
		if (KeepnetLevel < 0 || KeepnetLevel > MaxLevel) { KeepnetLevel = FMath::Clamp(KeepnetLevel, 0, MaxLevel); ++Fixes; }
		Fixes += FVSaveGameFix::NormalizeStacks(Keepnet, true, false, Rejected);
	}

	// ---- rods
	Fixes += CleanIdList(OwnedRods, [](FName Id) { return FVDB::FindRod(Id) != nullptr; });
	const FName StartRod = FVDB::StartRod();
	if (FVDB::FindRod(StartRod) && !OwnedRods.Contains(StartRod)) { OwnedRods.Insert(StartRod, 0); ++Fixes; }
	if (!OwnsRod(EquippedRod) || !FVDB::FindRod(EquippedRod))
	{
		EquippedRod = OwnedRods.Num() > 0 ? OwnedRods[0] : StartRod;
		++Fixes;
	}

	// ---- bait
	for (auto It = Baits.CreateIterator(); It; ++It)
	{
		if (!FVDB::FindBait(It.Key()) || It.Value() <= 0) { It.RemoveCurrent(); ++Fixes; continue; }
		if (It.Value() > FVMaxBaitCount) { It.Value() = FVMaxBaitCount; ++Fixes; }
	}
	if (!EquippedBait.IsNone() && (!FVDB::FindBait(EquippedBait) || BaitCount(EquippedBait) <= 0))
	{
		EquippedBait = NAME_None;
		++Fixes;
	}

	// ---- cosmetics
	Fixes += CleanIdList(OwnedCosmetics, [](FName Id) { return FVDB::FindCosmetic(Id) != nullptr; });
	for (const FName& Id : FVDB::StartCosmetics())
	{
		if (FVDB::FindCosmetic(Id) && !OwnedCosmetics.Contains(Id)) { OwnedCosmetics.Add(Id); ++Fixes; }
	}
	auto FixSlot = [this, &Fixes](FName& Equipped, EFVCosmeticSlot Slot, bool bRequired)
	{
		const FFVCosmeticDef* C = FVDB::FindCosmetic(Equipped);
		const bool bValid = C && C->Slot == Slot && OwnsCosmetic(Equipped);
		if (bValid) { return; }
		if (Equipped.IsNone() && !bRequired) { return; }   // intentionally empty slot
		++Fixes;
		Equipped = NAME_None;
		const FName Starter = StarterCosmeticFor(Slot);
		if (!Starter.IsNone() && OwnsCosmetic(Starter))
		{
			Equipped = Starter;
		}
		else if (bRequired)
		{
			for (const FName& Id : OwnedCosmetics)
			{
				const FFVCosmeticDef* O = FVDB::FindCosmetic(Id);
				if (O && O->Slot == Slot) { Equipped = Id; break; }
			}
		}
	};
	FixSlot(EquippedHat, EFVCosmeticSlot::Hat, false);
	FixSlot(EquippedOutfit, EFVCosmeticSlot::Outfit, true);
	FixSlot(EquippedBobber, EFVCosmeticSlot::Bobber, false);
	// Pets: an invalid pet simply goes home (no starter pet).
	if (!EquippedPet.IsNone())
	{
		const FFVCosmeticDef* C = FVDB::FindCosmetic(EquippedPet);
		if (!C || C->Slot != EFVCosmeticSlot::Pet || !OwnsCosmetic(EquippedPet)) { EquippedPet = NAME_None; ++Fixes; }
	}

	// ---- locations
	Fixes += CleanIdList(UnlockedLocations, [](FName Id) { return FVDB::LocationIndex(Id) != INDEX_NONE || FVDBU6::IsForestId(Id); });
	const FName StartLoc = FVDB::StartLocation();
	if (FVDB::FindLocation(StartLoc) && !UnlockedLocations.Contains(StartLoc)) { UnlockedLocations.Insert(StartLoc, 0); ++Fixes; }
	if (!IsLocationUnlocked(CurrentLocation))
	{
		CurrentLocation = StartLoc;
		++Fixes;
	}

	// ---- records & achievements
	for (auto It = Records.CreateIterator(); It; ++It)
	{
		if (!FVDB::FindFish(It.Key()) || It.Value().Caught <= 0) { It.RemoveCurrent(); ++Fixes; continue; }
		FFVFishRecord& R = It.Value();
		if (R.BestQuality > uint8(EFVQuality::Iridium)) { R.BestQuality = uint8(EFVQuality::Iridium); ++Fixes; }
		if (!(R.MaxSize >= 0.f)) { R.MaxSize = 0.f; ++Fixes; }   // also catches NaN
	}
	Fixes += CleanIdList(CompletedAchievements, [](FName Id) { return FVDB::FindAchievement(Id) != nullptr; });

	// ---- stats
	Stats.FishCaught = FMath::Max(0, Stats.FishCaught);
	Stats.JunkCaught = FMath::Max(0, Stats.JunkCaught);
	Stats.Escaped = FMath::Max(0, Stats.Escaped);
	Stats.Perfect = FMath::Max(0, Stats.Perfect);
	Stats.Treasures = FMath::Max(0, Stats.Treasures);
	Stats.Legendary = FMath::Max(0, Stats.Legendary);
	Stats.Iridium = FMath::Max(0, Stats.Iridium);
	Stats.Earned = FMath::Max<int64>(0, Stats.Earned);
	Stats.Spent = FMath::Max<int64>(0, Stats.Spent);
	if (!(Stats.PlaySeconds >= 0.f)) { Stats.PlaySeconds = 0.f; }
	Stats.Gathered = FMath::Max(0, Stats.Gathered);
	Stats.Crafted = FMath::Max(0, Stats.Crafted);
	Stats.Cooked = FMath::Max(0, Stats.Cooked);
	Stats.Smoked = FMath::Max(0, Stats.Smoked);
	Stats.WormsDug = FMath::Max(0, Stats.WormsDug);
	Stats.NetFish = FMath::Max(0, Stats.NetFish);
	Stats.OrdersDone = FMath::Max(0, Stats.OrdersDone);
	Stats.OrdersFailed = FMath::Max(0, Stats.OrdersFailed);
	Stats.KnotsTied = FMath::Max(0, Stats.KnotsTied);

	// ---- clock, weather, settings
	if (Day < 1) { Day = 1; ++Fixes; }
	if (!(TimeMinutes >= 0.f && TimeMinutes < 1440.f))
	{
		TimeMinutes = FMath::IsFinite(TimeMinutes) ? FMath::Fmod(FMath::Fmod(TimeMinutes, 1440.f) + 1440.f, 1440.f) : FV::DayStartMinutes;
		++Fixes;
	}
	if (Weather >= uint8(EFVWeather::Count)) { Weather = uint8(EFVWeather::Sunny); ++Fixes; }
	SfxVolume = FMath::IsFinite(SfxVolume) ? FMath::Clamp(SfxVolume, 0.f, 1.f) : 0.8f;
	MusicVolume = FMath::IsFinite(MusicVolume) ? FMath::Clamp(MusicVolume, 0.f, 1.f) : 0.45f;
	if (WeatherTomorrow >= uint8(EFVWeather::Count)) { WeatherTomorrow = uint8(EFVWeather::Sunny); ++Fixes; }
	// ---- update 3: guide entries opened (ids kept even if the guide changes) and the inventory page to open
	SeenTutorial.Remove(NAME_None);
	{
		TArray<FName> Unique;
		for (const FName& Id : SeenTutorial) { Unique.AddUnique(Id); }
		SeenTutorial = MoveTemp(Unique);
	}
	if (LastInventoryTab > 1) { LastInventoryTab = 0; ++Fixes; }

	// ================================================================ update 2 state (needs the validated clock)
	{
		namespace Fx = FVSaveGameFix;
		const double NowAbs = double(Day - 1) * 1440.0 + double(TimeMinutes);

		// ---- stations: known ids, levels 1..max; bed & chest always stand
		for (auto It = StationLevels.CreateIterator(); It; ++It)
		{
			const FFVStationDef* S = FVDB::FindStation(It.Key());
			if (!S) { It.RemoveCurrent(); ++Fixes; continue; }
			const int32 Max = S->Levels.Num();
			if (It.Value() > Max) { It.Value() = Max; ++Fixes; }
			if (It.Value() <= 0) { It.RemoveCurrent(); continue; }
		}
		for (const FFVStationDef& S : FVDB::Stations())
		{
			if (!S.bStartsBuilt || S.Id.IsNone()) { continue; }
			int32& L = StationLevels.FindOrAdd(S.Id);
			if (L < 1) { L = 1; ++Fixes; }
		}

		// ---- backpack & tools
		const int32 MaxBackpack = FMath::Max(0, FVDB::BackpackLevels().Num() - 1);
		if (BackpackLevel < 0 || BackpackLevel > MaxBackpack) { BackpackLevel = FMath::Clamp(BackpackLevel, 0, MaxBackpack); ++Fixes; }
		if (ShovelLevel < 0 || ShovelLevel > FVDB::Shovels().Num()) { ShovelLevel = FMath::Clamp(ShovelLevel, 0, FVDB::Shovels().Num()); ++Fixes; }

		// ---- containers (keepnet was repaired above)
		Fixes += Fx::NormalizeStacks(Backpack, false, false, Rejected);
		Fixes += Fx::NormalizeStacks(Chest, false, false, Rejected);
		Fixes += Fx::NormalizeStacks(IceCellar, true, false, Rejected);
		Fixes += Fx::NormalizeStacks(Aquarium, true, true, Rejected);
		{
			const int32 Cap = StationCapacity(EFVStation::Aquarium);
			while (Aquarium.Num() > Cap)
			{
				Rejected.Fish.Add(Aquarium.Last());   // too many live fish: back to the keepnet
				Aquarium.Pop();
				++Fixes;
			}
		}

		// ---- trophies: known species, one plaque per species (the biggest), at most the wall's capacity
		{
			TArray<FFVTrophy> Clean;
			for (const FFVTrophy& T : Trophies)
			{
				if (!FVDB::FindFish(T.FishId)) { ++Fixes; continue; }
				FFVTrophy N = T;
				if (!(N.SizeCm >= 0.f) || !FMath::IsFinite(N.SizeCm)) { N.SizeCm = 0.f; ++Fixes; }
				N.Quality = FMath::Min<uint8>(N.Quality, uint8(EFVQuality::Iridium));
				FFVTrophy* Same = Clean.FindByPredicate([&](const FFVTrophy& E) { return E.FishId == N.FishId; });
				if (Same) { if (N.SizeCm > Same->SizeCm) { *Same = N; } ++Fixes; continue; }
				Clean.Add(N);
			}
			const TArray<FFVStationLevel>& WallLevels = FVDB::Station(EFVStation::TrophyWall).Levels;
			const int32 MaxPlaques = WallLevels.Num() > 0 ? FMath::Max(1, WallLevels.Last().Capacity) : 6;
			if (Clean.Num() > MaxPlaques) { Clean.SetNum(MaxPlaques); ++Fixes; }
			Trophies = MoveTemp(Clean);
		}

		// ---- rod tackle: owned rods, a line in the line slot, a float in the float slot (others go back to the backpack)
		for (auto It = RodLoadouts.CreateIterator(); It; ++It)
		{
			const bool bRodOk = FVDB::FindRod(It.Key()) != nullptr && OwnsRod(It.Key());
			auto FixTackleSlot = [&](FName& Slot, EFVItemKind Kind)
			{
				if (Slot.IsNone()) { return; }
				const FFVItemDef* I = FVDB::FindItem(Slot);
				if (I && I->Kind == Kind && bRodOk) { return; }
				if (I)
				{
					FFVInvStack Back;
					Back.ItemId = Slot;
					Back.Count = 1;
					Rejected.Items.Add(Back);
				}
				Slot = NAME_None;
				++Fixes;
			};
			FixTackleSlot(It.Value().Line, EFVItemKind::Line);
			FixTackleSlot(It.Value().Float, EFVItemKind::Float);
			if (It.Value().Line.IsNone() && It.Value().Float.IsNone()) { It.RemoveCurrent(); }
		}

		// ---- food buff
		{
			const FFVItemDef* Food = FVDB::FindItem(ActiveFood);
			if (!ActiveFood.IsNone() && (!Food || Food->Kind != EFVItemKind::Food)) { ActiveFood = NAME_None; ++Fixes; }
			ActiveFoodQuality = FMath::Min<uint8>(ActiveFoodQuality, uint8(EFVQuality::Iridium));
			if (!FMath::IsFinite(FoodExpires) || FoodExpires < 0.0) { FoodExpires = 0.0; ++Fixes; }
			if (FoodExpires > NowAbs + 1440.0) { FoodExpires = NowAbs + 1440.0; ++Fixes; }
			if (ActiveFood.IsNone()) { ActiveFoodQuality = 0; FoodExpires = 0.0; }
		}

		// ---- nets: a real net item on a real spot, one net per spot, sane numbers
		{
			TArray<FFVNetState> Clean;
			for (const FFVNetState& Src : Nets)
			{
				const FFVItemDef* Def = FVDB::FindItem(Src.NetItem);
				const bool bNetItem = Def && Def->Kind == EFVItemKind::Net;
				const bool bSpot = Src.Spot >= 0 && Src.Spot < Fx::NetSpotCount(Src.Location) && FVDB::LocationIndex(Src.Location) != INDEX_NONE;
				const bool bDup = Clean.ContainsByPredicate([&](const FFVNetState& E) { return E.Location == Src.Location && E.Spot == Src.Spot; });
				if (!bNetItem || !bSpot || bDup)
				{
					++Fixes;
					if (bNetItem)
					{
						FFVInvStack Back;
						Back.ItemId = Src.NetItem;
						Back.Count = 1;
						Rejected.Items.Add(Back);
					}
					for (const FFVInvStack& St : Src.Catch) { Fx::RejectStack(Rejected, St); }
					continue;
				}
				FFVNetState N = Src;
				const float MaxDur = FMath::Max(1.f, Def->NetDurability);
				if (!FMath::IsFinite(N.Durability)) { N.Durability = 0.f; ++Fixes; }
				N.Durability = FMath::Clamp(N.Durability, 0.f, MaxDur);
				if (N.Durability <= 0.f) { N.bTorn = true; }
				if (!FMath::IsFinite(N.SimMinute) || N.SimMinute < 0.0 || N.SimMinute > NowAbs) { N.SimMinute = NowAbs; }
				if (!FMath::IsFinite(N.Progress)) { N.Progress = 0.f; }
				N.Progress = FMath::Clamp(N.Progress, 0.f, 0.999f);
				Fx::NormalizeCatch(N.Catch);
				Clean.Add(N);
			}
			Nets = MoveTemp(Clean);
		}

		// ---- smokehouse
		{
			const int32 Cap = StationCapacity(EFVStation::Smokehouse);
			TArray<FFVSmokeSlot> Clean;
			for (const FFVSmokeSlot& Src : Smokehouse)
			{
				if (!FVDB::FindFish(Src.FishId)) { ++Fixes; continue; }
				FFVSmokeSlot S = Src;
				S.Quality = FMath::Min<uint8>(S.Quality, uint8(EFVQuality::Iridium));
				if (!FMath::IsFinite(S.MinutesLeft)) { S.MinutesLeft = 360.f; ++Fixes; }
				S.MinutesLeft = FMath::Clamp(S.MinutesLeft, 0.f, 360.f);
				if (Clean.Num() >= Cap)
				{
					FFVInvStack Back;   // no room (or no smokehouse): the fish goes back to the keepnet
					Back.ItemId = S.FishId;
					Back.Quality = S.Quality;
					Back.Count = 1;
					Back.Flags = S.MinutesLeft <= 0.f ? FVStackFlags::Smoked : 0;
					Rejected.Fish.Add(Back);
					++Fixes;
					continue;
				}
				Clean.Add(S);
			}
			Smokehouse = MoveTemp(Clean);
			if (!FMath::IsFinite(SmokeFuel) || SmokeFuel < 0.f) { SmokeFuel = 0.f; ++Fixes; }
			SmokeFuel = FMath::Min(SmokeFuel, 720.f);
		}

		// ---- worm farm
		{
			const int32 Cap = StationCapacity(EFVStation::WormFarm);
			WormFarmCrawlers = FMath::Clamp(WormFarmCrawlers, 0, Cap);
			WormFarmWorms = FMath::Clamp(WormFarmWorms, 0, FMath::Max(0, Cap - WormFarmCrawlers));
			WormFarmDay = FMath::Clamp(WormFarmDay, 0, Day);
		}

		// ---- orders
		{
			TArray<FFVOrder> Clean;
			for (const FFVOrder& Src : Orders)
			{
				const bool bFish = FVDB::FindFish(Src.ItemId) != nullptr;
				const bool bItem = !bFish && FVDB::FindItem(Src.ItemId) != nullptr;
				const bool bDup = Clean.ContainsByPredicate([&](const FFVOrder& E) { return E.Id == Src.Id; });
				if (Src.Id.IsNone() || bDup || (!bFish && !bItem) || Src.State >= uint8(EFVOrderState::Count)) { ++Fixes; continue; }
				FFVOrder O = Src;
				O.Count = FMath::Clamp(O.Count, 1, 999);
				O.MinQuality = FMath::Min<uint8>(O.MinQuality, uint8(EFVQuality::Iridium));
				if (!bFish) { O.bSmoked = false; }
				O.Deposit = FMath::Max(0, O.Deposit);
				O.Reward = FMath::Max(0, O.Reward);
				if (!O.RewardItem.IsNone() && !FVDB::FindItem(O.RewardItem)) { O.RewardItem = NAME_None; ++Fixes; }
				O.RewardItemCount = O.RewardItem.IsNone() ? 0 : FMath::Clamp(O.RewardItemCount, 1, 99);
				O.DurationHours = FMath::Clamp(O.DurationHours, 1, 240);
				if (!FMath::IsFinite(O.Deadline) || O.Deadline < 0.0) { O.Deadline = 0.0; ++Fixes; }
				if (O.State == uint8(EFVOrderState::Taken) && O.Deadline <= 0.0) { O.Deadline = NowAbs + O.DurationHours * 60.0; }
				if (O.Customer.IsEmpty()) { O.Customer = TEXT("Сосед"); }
				Clean.Add(O);
			}
			Orders = MoveTemp(Clean);
			OrdersDay = FMath::Clamp(OrdersDay, 0, Day);
		}

		// ---- daily masks: only today's entries of known locations matter
		for (auto It = Gathered.CreateIterator(); It; ++It)
		{
			if (!FVDB::FindLocation(It.Key()) || It.Value().Day != Day) { It.RemoveCurrent(); }
		}
		for (auto It = Dug.CreateIterator(); It; ++It)
		{
			if (!FVDB::FindLocation(It.Key()) || It.Value().Day != Day) { It.RemoveCurrent(); }
		}

		// ---- lures & decor
		Fixes += CleanIdList(DiscoveredLures, [](FName Id) { const FFVBaitDef* B = FVDB::FindBait(Id); return B && B->bLure; });
		Fixes += CleanIdList(OwnedDecor, [](FName Id) { return FVDB::FindDecor(Id) != nullptr; });
		for (const FName& Id : FVDB::StarterDecor())
		{
			if (FVDB::FindDecor(Id) && !OwnedDecor.Contains(Id)) { OwnedDecor.Add(Id); ++Fixes; }
		}
		{
			TArray<FName> Equipped;
			bool SlotUsed[uint8(EFVDecorSlot::Count)] = {};
			for (const FName& Id : EquippedDecor)
			{
				const FFVDecorDef* D = FVDB::FindDecor(Id);
				if (!D || !OwnsDecor(Id) || uint8(D->Slot) >= uint8(EFVDecorSlot::Count) || SlotUsed[uint8(D->Slot)]) { ++Fixes; continue; }
				SlotUsed[uint8(D->Slot)] = true;
				Equipped.Add(Id);
			}
			// the walls and the floor are never empty
			for (const EFVDecorSlot Required : { EFVDecorSlot::Wallpaper, EFVDecorSlot::Floor })
			{
				if (SlotUsed[uint8(Required)]) { continue; }
				FName Pick = NAME_None;
				for (const FName& Id : FVDB::StarterDecor())
				{
					const FFVDecorDef* D = FVDB::FindDecor(Id);
					if (D && D->Slot == Required && OwnsDecor(Id)) { Pick = Id; break; }
				}
				if (Pick.IsNone())
				{
					for (const FName& Id : OwnedDecor)
					{
						const FFVDecorDef* D = FVDB::FindDecor(Id);
						if (D && D->Slot == Required) { Pick = Id; break; }
					}
				}
				if (!Pick.IsNone()) { Equipped.Add(Pick); SlotUsed[uint8(Required)] = true; ++Fixes; }
			}
			EquippedDecor = MoveTemp(Equipped);
		}

		// ---- re-home everything found in the wrong place (never destroy the player's things)
		for (const FFVInvStack& St : Rejected.Fish)
		{
			Fx::AddRaw(Keepnet, St.ItemId, Fx::NormQuality(St.ItemId, St.Quality), Fx::NormFlags(St.ItemId, St.Flags), St.Count, Fx::MaxUnits);
		}
		for (const FFVInvStack& St : Rejected.Items)
		{
			Fx::AddRaw(Backpack, St.ItemId, Fx::NormQuality(St.ItemId, St.Quality), 0, St.Count, Fx::MaxStackOf(St.ItemId));
		}
		for (const TPair<FName, int32>& B : Rejected.Baits) { AddBait(B.Key, B.Value); }

		// ---- a fresh order board for today (no-op when it is already up to date)
		RefreshOrders();
	}

	// ================================================================ update 4: the online profile
	{
		namespace Ox = FVSaveGameOnlineFix;
		const int64 Now = Ox::NowUnix();
		// the start date: unknown (saves from before update 4) -> today (a wrong device clock is not "fixed" here)
		if (ProfileStartUnix <= 0)
		{
			ProfileStartUnix = Now;
			++Fixes;
		}

		// name (1..16 chars) and motto (<= 40 chars): one clean line each
		const FString Name = Ox::CleanLine(PlayerName, 16);
		if (Name != PlayerName) { PlayerName = Name; ++Fixes; }
		const FString Status = Ox::CleanLine(PlayerStatus, 40);
		if (Status != PlayerStatus) { PlayerStatus = Status; ++Fixes; }

		// identity: a registered player needs his 64-hex secret (without it no request can be signed -> register anew)
		if (OnlineId < 0) { OnlineId = 0; ++Fixes; }
		if (OnlineTag < 0) { OnlineTag = 0; ++Fixes; }
		if (!OnlineSecret.IsEmpty() && !Ox::IsHexSecret(OnlineSecret))
		{
			OnlineSecret.Reset();
			++Fixes;
		}
		if (OnlineId > 0 && OnlineSecret.IsEmpty())
		{
			UE_LOG(LogFV, Warning, TEXT("FV: online id %lld has no valid secret: the player will register again"), OnlineId);
			OnlineId = 0;
			OnlineTag = 0;
			++Fixes;
		}
		if (OnlineId == 0 && OnlineTag != 0) { OnlineTag = 0; ++Fixes; }

		// fish per location (favourite location): known locations, positive counts
		for (auto It = CatchesByLocation.CreateIterator(); It; ++It)
		{
			if (It.Key().IsNone() || !FVDB::FindLocation(It.Key()) || It.Value() <= 0) { It.RemoveCurrent(); ++Fixes; }
		}

		// claimed letters: positive ids, no duplicates, the newest 500 (ids grow on the server)
		{
			TArray<int64> Ids;
			Ids.Reserve(ClaimedMail.Num());
			for (const int64 Id : ClaimedMail)
			{
				if (Id > 0) { Ids.AddUnique(Id); }
			}
			Ids.Sort();
			if (Ids.Num() > 500)
			{
				Ids.RemoveAt(0, Ids.Num() - 500);
			}
			if (Ids.Num() != ClaimedMail.Num()) { ++Fixes; }
			ClaimedMail = MoveTemp(Ids);
		}

		// best catch & the tournament's biggest fish: known species, sane numbers
		if (Ox::CleanShownFish(BestCatch)) { ++Fixes; }
		if (BestCatch.FishId.IsNone()) { BestCatchPrice = 0; }
		if (BestCatchPrice < 0) { BestCatchPrice = 0; ++Fixes; }
		if (Ox::CleanShownFish(PendingSeasonBest)) { ++Fixes; }
	}

	// ================================================================ update 4b: the first-fishing onboarding
	{
		if (OnboardingStep < 0 || OnboardingStep > 100)
		{
			OnboardingStep = FMath::Clamp(OnboardingStep, 0, 100);
			++Fixes;
		}
		// Saves from before update 4b never wrote the field (delta serialization: it loads as 0). A fisherman who has
		// already caught, earned XP, slept or left home knows how to fish: no coach marks for him.
		const bool bOldSave = Stats.FishCaught > 0 || Xp > 0 || Day > 1
			|| (!CurrentLocation.IsNone() && !FVDB::IsHomeId(CurrentLocation));
		if (OnboardingStep == 0 && bOldSave)
		{
			OnboardingStep = 100;
			UE_LOG(LogFV, Log, TEXT("FV: a save from before update 4b: the first-fishing onboarding is marked done"));
		}
	}

	// ================================================================ update 5: legends met (known fish ids, no duplicates)
	{
		TArray<FName> Met;
		for (const FName& Id : LegendsMet)
		{
			if (!Id.IsNone() && FVDB::FindFish(Id)) { Met.AddUnique(Id); }
		}
		if (Met.Num() != LegendsMet.Num()) { ++Fixes; }
		LegendsMet = MoveTemp(Met);
	}

	// ================================================================ update 6 (FVSaveGameU6.cpp: migration 0.5 -> U6, repairs)
	FixupU6();
	FixupMill();   // §18 «Старая мельница» (FVSaveGameU6Mill.cpp)

	if (Fixes > 0)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: profile repaired (%d fixes)"), Fixes);
	}
}

// ================================================================================================
// Level / money
// ================================================================================================
int32 UFVSaveGame::GetLevel() const
{
	return FVDB::LevelForXp(Xp);
}

float UFVSaveGame::GetLevelProgress() const
{
	const int32 L = GetLevel();
	if (L >= FV::MaxLevel) { return 1.f; }
	if (IsXpFrozen()) { return 1.f; }   // update 6 (§9): «Опыт на максимуме — плывите в Город», the bar is full
	const int32 From = FVDB::XpForLevel(L);
	const int32 To = FVDB::XpForLevel(L + 1);
	if (To <= From) { return 1.f; }
	return FMath::Clamp(float(Xp - From) / float(To - From), 0.f, 1.f);
}

int32 UFVSaveGame::AddXp(int32 Amount)
{
	if (Amount <= 0) { return 0; }
	// update 6: «опыт заработанный» always grows (the tournament category, §9)
	U6.XpEarned = FMath::Clamp<int64>(U6.XpEarned + Amount, 0, MAX_int64 / 2);
	const int32 Before = GetLevel();
	// update 6 (Р1): before the first arrival in the City XP stops at the threshold of level 10; the rest is lost
	const int32 Cap = U6.bVisitedCity ? MAX_int32 : FVDB::XpForLevel(FVDBU6::FrozenLevel());
	if (Xp < Cap) { Xp = SatAdd(Xp, Amount, Cap); }
	return FMath::Max(0, GetLevel() - Before);
}

bool UFVSaveGame::Spend(int64 Price)
{
	if (Price < 0) { return false; }
	if (Price == 0) { return true; }
	if (Money < Price) { return false; }
	Money -= Price;
	Stats.Spent += Price;
	return true;
}

void UFVSaveGame::Earn(int64 Amount, bool bCountAsSales)
{
	if (Amount <= 0) { return; }
	Money += Amount;
	if (bCountAsSales) { Stats.Earned += Amount; }
}

// ================================================================================================
// Keepnet
// ================================================================================================
int32 UFVSaveGame::KeepnetCapacity() const
{
	const TArray<FFVKeepnetLevel>& Levels = FVDB::KeepnetLevels();
	if (Levels.Num() == 0) { return 12; }
	return FMath::Max(1, Levels[FMath::Clamp(KeepnetLevel, 0, Levels.Num() - 1)].Capacity);
}

int32 UFVSaveGame::KeepnetUsed() const
{
	int64 Used = 0;
	for (const FFVInvStack& St : Keepnet) { Used += FMath::Max(0, St.Count); }
	return int32(FMath::Min<int64>(Used, MAX_int32));
}

bool UFVSaveGame::AddToKeepnet(FName ItemId, EFVQuality Q, int32 Count)
{
	// Update 2: fish only (junk goes to the backpack through AddTo / GiveItem).
	return AddToKeepnetEx(ItemId, Q, Count, 0);
}

int64 UFVSaveGame::SellStack(int32 StackIndex, int32 Count)
{
	if (!Keepnet.IsValidIndex(StackIndex) || Count <= 0) { return 0; }
	FFVInvStack& St = Keepnet[StackIndex];
	const int32 N = FMath::Min(Count, St.Count);
	if (N <= 0)
	{
		Keepnet.RemoveAt(StackIndex);
		return 0;
	}
	const int64 Coins = int64(FVDB::ItemPriceEx(St.ItemId, EFVQuality(FMath::Min<uint8>(St.Quality, uint8(EFVQuality::Iridium))), St.Flags)) * N;
	St.Count -= N;
	if (St.Count <= 0) { Keepnet.RemoveAt(StackIndex); }
	Earn(Coins, true);
	return Coins;
}

int64 UFVSaveGame::SellAll()
{
	const int64 Total = KeepnetValue();
	Keepnet.Reset();
	Earn(Total, true);
	return Total;
}

int64 UFVSaveGame::KeepnetValue() const
{
	int64 Total = 0;
	for (const FFVInvStack& St : Keepnet)
	{
		if (St.Count <= 0) { continue; }
		Total += int64(FVDB::ItemPriceEx(St.ItemId, EFVQuality(FMath::Min<uint8>(St.Quality, uint8(EFVQuality::Iridium))), St.Flags)) * St.Count;
	}
	return Total;
}

EFVBuyResult UFVSaveGame::UpgradeKeepnet()
{
	const TArray<FFVKeepnetLevel>& Levels = FVDB::KeepnetLevels();
	const int32 Next = FMath::Max(0, KeepnetLevel) + 1;
	if (!Levels.IsValidIndex(Next)) { return EFVBuyResult::AlreadyOwned; }   // already at the biggest keepnet
	const int64 Price = FMath::Max(0, Levels[Next].Price);
	if (!CanAfford(Price)) { return EFVBuyResult::NotEnoughMoney; }
	if (!Spend(Price)) { return EFVBuyResult::NotEnoughMoney; }
	KeepnetLevel = Next;
	return EFVBuyResult::Ok;
}

// ================================================================================================
// Rods & bait
// ================================================================================================
EFVBuyResult UFVSaveGame::CanBuyRod(FName Id) const
{
	const FFVRodDef* R = FVDB::FindRod(Id);
	if (!R) { return EFVBuyResult::Invalid; }
	if (OwnsRod(Id)) { return EFVBuyResult::AlreadyOwned; }
	if (R->Price <= 0 || R->TraderMask == 0) { return EFVBuyResult::NotForSale; }   // 0.6.2: forged rods are not sold
	if (GetLevel() < R->MinLevel) { return EFVBuyResult::LevelTooLow; }
	if (!R->RequiredAchievement.IsNone() && !IsAchievementDone(R->RequiredAchievement)) { return EFVBuyResult::Locked; }
	if (!CanAfford(R->Price)) { return EFVBuyResult::NotEnoughMoney; }
	return EFVBuyResult::Ok;
}

void UFVSaveGame::GiveUpRod(FName Id)
{
	if (!OwnsRod(Id) || Id == FVDB::StartRod()) { return; }
	auto GiveBack = [this](FName T)
	{
		if (T.IsNone() || !FVDB::IsItemId(T)) { return; }
		if (!AddTo(EFVContainer::Backpack, T, 1) && !AddTo(EFVContainer::Chest, T, 1))
		{
			UE_LOG(LogFV, Warning, TEXT("FV: no room for %s from a given-up rod"), *T.ToString());
		}
	};
	if (const FFVExpertLoadout* L = U6.ExpertLoadouts.Find(Id))
	{
		const FFVExpertLoadout Copy = *L;
		U6.ExpertLoadouts.Remove(Id);
		GiveBack(Copy.Line);
		GiveBack(Copy.Float);
		GiveBack(Copy.Reel);
	}
	if (const FFVRodLoadout* L = RodLoadouts.Find(Id))
	{
		const FFVRodLoadout Copy = *L;
		RodLoadouts.Remove(Id);
		GiveBack(Copy.Line);
		GiveBack(Copy.Float);
	}
	OwnedRods.Remove(Id);
	if (EquippedRod == Id) { EquippedRod = OwnedRods.Num() > 0 ? OwnedRods[0] : FVDB::StartRod(); }
	UE_LOG(LogFV, Log, TEXT("FV: rod %s given up (перековка)"), *Id.ToString());
}

EFVBuyResult UFVSaveGame::BuyRod(FName Id)
{
	const FFVRodDef* R = FVDB::FindRod(Id);
	const EFVBuyResult Can = CanBuyRod(Id);
	if (Can != EFVBuyResult::Ok) { return Can; }
	if (!Spend(R->Price)) { return EFVBuyResult::NotEnoughMoney; }
	OwnedRods.Add(Id);
	EquipRod(Id);
	UE_LOG(LogFV, Log, TEXT("FV: bought rod %s"), *Id.ToString());
	return EFVBuyResult::Ok;
}

bool UFVSaveGame::EquipRod(FName Id)
{
	if (!FVDB::FindRod(Id) || !OwnsRod(Id)) { return false; }
	EquippedRod = Id;
	return true;
}

const FFVRodDef& UFVSaveGame::GetRod() const
{
	if (OwnsRod(EquippedRod))
	{
		if (const FFVRodDef* R = FVDB::FindRod(EquippedRod)) { return *R; }
	}
	if (const FFVRodDef* R = FVDB::FindRod(FVDB::StartRod())) { return *R; }
	const TArray<FFVRodDef>& Rods = FVDB::Rods();
	if (Rods.Num() > 0) { return Rods[0]; }
	static const FFVRodDef Fallback;
	return Fallback;
}

EFVBuyResult UFVSaveGame::BuyBait(FName Id, int32 Packs)
{
	const FFVBaitDef* B = FVDB::FindBait(Id);
	if (!B || Packs <= 0) { return EFVBuyResult::Invalid; }
	if (B->PackPrice <= 0 || B->PackSize <= 0) { return EFVBuyResult::NotForSale; }
	if (GetLevel() < B->MinLevel) { return EFVBuyResult::LevelTooLow; }
	const int64 Cost = int64(B->PackPrice) * Packs;
	if (!CanAfford(Cost)) { return EFVBuyResult::NotEnoughMoney; }
	if (!Spend(Cost)) { return EFVBuyResult::NotEnoughMoney; }
	AddBait(Id, B->PackSize * Packs);
	// Convenience: if the hook is empty, put the new bait on it.
	if (EquippedBait.IsNone()) { EquipBait(Id); }
	return EFVBuyResult::Ok;
}

int32 UFVSaveGame::BaitCount(FName Id) const
{
	const int32* C = Baits.Find(Id);
	return C ? FMath::Max(0, *C) : 0;
}

bool UFVSaveGame::EquipBait(FName Id)
{
	if (Id.IsNone())
	{
		EquippedBait = NAME_None;
		return true;
	}
	if (!FVDB::FindBait(Id) || BaitCount(Id) <= 0) { return false; }
	EquippedBait = Id;
	return true;
}

const FFVBaitDef* UFVSaveGame::GetBait() const
{
	if (EquippedBait.IsNone() || BaitCount(EquippedBait) <= 0) { return nullptr; }
	return FVDB::FindBait(EquippedBait);
}

void UFVSaveGame::ConsumeBait()
{
	if (EquippedBait.IsNone()) { return; }
	int32* C = Baits.Find(EquippedBait);
	if (C && *C > 0) { --(*C); }
	if (!C || *C <= 0)
	{
		Baits.Remove(EquippedBait);
		EquippedBait = NAME_None;
	}
}

void UFVSaveGame::AddBait(FName Id, int32 Count)
{
	if (Count <= 0 || !FVDB::FindBait(Id)) { return; }
	int32& C = Baits.FindOrAdd(Id);
	C = SatAdd(FMath::Max(0, C), Count, FVMaxBaitCount);
}

// ================================================================================================
// Cosmetics
// ================================================================================================
EFVBuyResult UFVSaveGame::BuyCosmetic(FName Id)
{
	const FFVCosmeticDef* C = FVDB::FindCosmetic(Id);
	if (!C) { return EFVBuyResult::Invalid; }
	if (OwnsCosmetic(Id)) { return EFVBuyResult::AlreadyOwned; }
	if (C->Price <= 0) { return EFVBuyResult::NotForSale; }
	if (GetLevel() < C->MinLevel) { return EFVBuyResult::LevelTooLow; }
	// Р64: expert cosmetics are sold only after their achievement (the shop shows them locked until then)
	if (!C->RequiredAch.IsNone() && !IsAchievementDone(C->RequiredAch)) { return EFVBuyResult::NotForSale; }
	if (!CanAfford(C->Price)) { return EFVBuyResult::NotEnoughMoney; }
	if (!Spend(C->Price)) { return EFVBuyResult::NotEnoughMoney; }
	OwnedCosmetics.Add(Id);
	UE_LOG(LogFV, Log, TEXT("FV: bought cosmetic %s"), *Id.ToString());
	return EFVBuyResult::Ok;
}

void UFVSaveGame::GrantCosmetic(FName Id)
{
	if (!FVDB::FindCosmetic(Id) || OwnsCosmetic(Id)) { return; }
	OwnedCosmetics.Add(Id);
}

bool UFVSaveGame::EquipCosmetic(FName Id)
{
	const FFVCosmeticDef* C = FVDB::FindCosmetic(Id);
	if (!C || !OwnsCosmetic(Id)) { return false; }
	switch (C->Slot)
	{
	case EFVCosmeticSlot::Hat:    EquippedHat = Id; break;
	case EFVCosmeticSlot::Outfit: EquippedOutfit = Id; break;
	case EFVCosmeticSlot::Bobber: EquippedBobber = Id; break;
	case EFVCosmeticSlot::Pet:    EquippedPet = Id; break;
	default: return false;
	}
	return true;
}

void UFVSaveGame::UnequipSlot(EFVCosmeticSlot Slot)
{
	switch (Slot)
	{
	case EFVCosmeticSlot::Hat:    EquippedHat = NAME_None; break;
	case EFVCosmeticSlot::Bobber: EquippedBobber = NAME_None; break;
	case EFVCosmeticSlot::Pet:    EquippedPet = NAME_None; break;
	default: break;   // the outfit always stays on
	}
}

FName UFVSaveGame::GetEquipped(EFVCosmeticSlot Slot) const
{
	switch (Slot)
	{
	case EFVCosmeticSlot::Hat:    return EquippedHat;
	case EFVCosmeticSlot::Outfit: return EquippedOutfit;
	case EFVCosmeticSlot::Bobber: return EquippedBobber;
	case EFVCosmeticSlot::Pet:    return EquippedPet;
	default:                      return NAME_None;
	}
}

FFVAppearance UFVSaveGame::BuildAppearance() const
{
	FFVAppearance A;
	const FFVCosmeticDef* Hat = FVDB::FindCosmetic(EquippedHat);
	if (Hat && Hat->Slot == EFVCosmeticSlot::Hat)
	{
		A.Hat = EFVHatStyle(FMath::Min<uint8>(Hat->Style, uint8(EFVHatStyle::Count) - 1));
		A.HatPrimary = Hat->Primary;
		A.HatSecondary = Hat->Secondary;
	}
	else
	{
		A.Hat = EFVHatStyle::None;
	}
	const FFVCosmeticDef* Outfit = FVDB::FindCosmetic(EquippedOutfit);
	if (Outfit && Outfit->Slot == EFVCosmeticSlot::Outfit)
	{
		A.Outfit = EFVOutfitStyle(FMath::Min<uint8>(Outfit->Style, uint8(EFVOutfitStyle::Count) - 1));
		A.OutfitPrimary = Outfit->Primary;
		A.OutfitSecondary = Outfit->Secondary;
	}
	const FFVRodDef& Rod = GetRod();
	A.RodColor = Rod.RodColor;
	A.RodAccent = Rod.AccentColor;
	return A;
}

EFVBobberStyle UFVSaveGame::GetBobberStyle() const
{
	const FFVCosmeticDef* C = FVDB::FindCosmetic(EquippedBobber);
	if (C && C->Slot == EFVCosmeticSlot::Bobber)
	{
		return EFVBobberStyle(FMath::Min<uint8>(C->Style, uint8(EFVBobberStyle::Count) - 1));
	}
	return EFVBobberStyle::Classic;
}

EFVPetStyle UFVSaveGame::GetPetStyle() const
{
	const FFVCosmeticDef* C = FVDB::FindCosmetic(EquippedPet);
	if (C && C->Slot == EFVCosmeticSlot::Pet)
	{
		return EFVPetStyle(FMath::Min<uint8>(C->Style, uint8(EFVPetStyle::Count) - 1));
	}
	return EFVPetStyle::None;
}

// ================================================================================================
// Locations
// ================================================================================================
bool UFVSaveGame::IsLocationUnlocked(FName Id) const
{
	if (Id.IsNone()) { return false; }
	if (FVDB::IsHomeId(Id)) { return true; }   // the yard and the house are always open
	return UnlockedLocations.Contains(Id);
}

EFVBuyResult UFVSaveGame::UnlockLocation(FName Id)
{
	const FFVLocationDef* L = FVDB::FindLocation(Id);
	if (!L) { return EFVBuyResult::Invalid; }
	if (IsLocationUnlocked(Id)) { return EFVBuyResult::AlreadyOwned; }
	// update 6: the forest opens for free at its level; the City — only by the repaired boat (§7); the Cave — only
	// when all the rubble is cleared (§9а). Their MinLevel is informative (the level is frozen at 10 before the City).
	// §18: the mill opens for free at level 10 once the footbridge in the forest is repaired
	if (FVMill::IsMillId(Id))
	{
		if (!IsBridgeRepaired()) { return EFVBuyResult::StationRequired; }
		if (GetLevel() < L->MinLevel) { return EFVBuyResult::LevelTooLow; }
		UnlockedLocations.Add(Id);
		UE_LOG(LogFV, Log, TEXT("FV: unlocked location %s"), *Id.ToString());
		return EFVBuyResult::Ok;
	}
	if (Id == FVDBU6::CityId() || Id == FVDBU6::CaveId())
	{
		if (Id == FVDBU6::CityId() ? !IsBoatRepaired() : !IsCaveOpen()) { return EFVBuyResult::StationRequired; }
		UnlockedLocations.Add(Id);
		UE_LOG(LogFV, Log, TEXT("FV: unlocked location %s"), *Id.ToString());
		return EFVBuyResult::Ok;
	}
	if (GetLevel() < L->MinLevel) { return EFVBuyResult::LevelTooLow; }
	const int64 Price = FMath::Max(0, L->UnlockPrice);
	if (!CanAfford(Price)) { return EFVBuyResult::NotEnoughMoney; }
	if (!Spend(Price)) { return EFVBuyResult::NotEnoughMoney; }
	UnlockedLocations.Add(Id);
	UE_LOG(LogFV, Log, TEXT("FV: unlocked location %s"), *Id.ToString());
	return EFVBuyResult::Ok;
}

// ================================================================================================
// Records & achievements
// ================================================================================================
UFVSaveGame::FRecordResult UFVSaveGame::RecordCatch(const FFVFishDef& F, float SizeCm, EFVQuality Q, bool bPerfect)
{
	FRecordResult Res;
	if (F.Id.IsNone()) { return Res; }
	const float Size = FMath::IsFinite(SizeCm) ? FMath::Max(0.f, SizeCm) : 0.f;
	const uint8 Quality = uint8(FMath::Clamp(int32(Q), 0, int32(EFVQuality::Iridium)));

	FFVFishRecord& Rec = Records.FindOrAdd(F.Id);
	Res.bNewSpecies = Rec.Caught <= 0;
	Res.bNewRecord = !Res.bNewSpecies && Size > Rec.MaxSize + 0.05f;   // first catch is "new species", not a record
	Rec.Caught = SatAdd(Rec.Caught, 1);
	Rec.MaxSize = FMath::Max(Rec.MaxSize, Size);
	Rec.BestQuality = FMath::Max(Rec.BestQuality, Quality);

	Stats.FishCaught = SatAdd(Stats.FishCaught, 1);
	if (bPerfect) { Stats.Perfect = SatAdd(Stats.Perfect, 1); }
	if (F.bLegendary) { Stats.Legendary = SatAdd(Stats.Legendary, 1); }
	if (Quality == uint8(EFVQuality::Iridium)) { Stats.Iridium = SatAdd(Stats.Iridium, 1); }
	return Res;
}

bool UFVSaveGame::HasMetLegend(FName FishId) const
{
	if (FishId.IsNone()) { return false; }
	if (LegendsMet.Contains(FishId)) { return true; }
	const FFVFishRecord* R = Records.Find(FishId);
	return R && R->Caught > 0;
}

void UFVSaveGame::MarkLegendMet(FName FishId)
{
	if (!FishId.IsNone()) { LegendsMet.AddUnique(FishId); }
}

int32 UFVSaveGame::SpeciesCaught() const
{
	int32 N = 0;
	for (const TPair<FName, FFVFishRecord>& P : Records)
	{
		if (P.Value.Caught > 0 && FVDB::FindFish(P.Key)) { ++N; }
	}
	return N;
}

int32 UFVSaveGame::SpeciesCaughtAt(FName LocationId) const
{
	int32 N = 0;
	for (const FFVFishDef* F : FVDB::FishAt(LocationId))
	{
		const FFVFishRecord* R = F ? Records.Find(F->Id) : nullptr;
		if (R && R->Caught > 0) { ++N; }
	}
	return N;
}

bool UFVSaveGame::IsLocationComplete(FName LocationId) const
{
	const int32 NumHere = FVDB::FishAt(LocationId).Num();
	return NumHere > 0 && SpeciesCaughtAt(LocationId) >= NumHere;
}

TArray<FName> UFVSaveGame::CheckAchievements()
{
	TArray<FName> NewlyDone;
	// Rewards can complete other achievements (e.g. a cosmetic reward -> "own N cosmetics"), so re-check a few times.
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		bool bAny = false;
		for (const FFVAchievementDef& A : FVDB::Achievements())
		{
			if (IsAchievementDone(A.Id)) { continue; }
			int64 Cur = 0, Target = 1;
			AchievementValues(*this, A, Cur, Target);
			if (Cur < Target) { continue; }

			CompletedAchievements.Add(A.Id);
			if (A.RewardMoney > 0) { Earn(A.RewardMoney, false); }
			if (!A.RewardCosmetic.IsNone()) { GrantCosmetic(A.RewardCosmetic); }
			if (!A.RewardDecor.IsNone()) { GrantDecor(A.RewardDecor); }
			NewlyDone.Add(A.Id);
			bAny = true;
			UE_LOG(LogFV, Log, TEXT("FV: achievement completed: %s"), *A.Id.ToString());
		}
		if (!bAny) { break; }
	}
	return NewlyDone;
}

float UFVSaveGame::AchievementProgress(const FFVAchievementDef& A) const
{
	if (IsAchievementDone(A.Id)) { return 1.f; }
	float V = 0.f;
	FString T;
	if (AchievementProgressU6(A, V, T)) { return FMath::IsFinite(V) ? FMath::Clamp(V, 0.f, 1.f) : 0.f; }   // update-6 kinds
	int64 Cur = 0, Target = 1;
	AchievementValues(*this, A, Cur, Target);
	return Target > 0 ? FMath::Clamp(float(double(Cur) / double(Target)), 0.f, 1.f) : 0.f;
}

FString UFVSaveGame::AchievementProgressText(const FFVAchievementDef& A) const
{
	float V = 0.f;
	FString T;
	if (AchievementProgressU6(A, V, T)) { return T; }   // update-6 kinds
	int64 Cur = 0, Target = 1;
	AchievementValues(*this, A, Cur, Target);
	if (IsAchievementDone(A.Id)) { Cur = Target; }
	return FString::Printf(TEXT("%s / %s"), *FV::Money(FMath::Min(Cur, Target)), *FV::Money(Target));
}

FString UFVSaveGame::BuyResultText(EFVBuyResult R)
{
	switch (R)
	{
	case EFVBuyResult::Ok:             return TEXT("Готово!");
	case EFVBuyResult::AlreadyOwned:   return TEXT("Уже есть");
	case EFVBuyResult::NotEnoughMoney: return TEXT("Не хватает монет");
	case EFVBuyResult::LevelTooLow:    return TEXT("Нужен уровень повыше");
	case EFVBuyResult::NotForSale:     return TEXT("Не продаётся");
	case EFVBuyResult::NotEnoughMaterials: return TEXT("Не хватает материалов");
	case EFVBuyResult::NoRoom:         return TEXT("Нет места");
	case EFVBuyResult::StationRequired: return TEXT("Нужна постройка получше");
	case EFVBuyResult::MaxLevel:       return TEXT("Максимальный уровень");
	case EFVBuyResult::Locked:         return TEXT("Сначала нужно достижение");
	default:                           return TEXT("Недоступно");
	}
}

// ================================================================================================
// Persistence
// ================================================================================================
bool UFVSaveGame::SlotExists()
{
	return UGameplayStatics::DoesSaveGameExist(GetSlotName(), FVSaveUserIndex);
}

UFVSaveGame* UFVSaveGame::Load()
{
	const FString Slot = GetSlotName();
	if (!UGameplayStatics::DoesSaveGameExist(Slot, FVSaveUserIndex)) { return nullptr; }

	USaveGame* Raw = UGameplayStatics::LoadGameFromSlot(Slot, FVSaveUserIndex);
	if (!Raw)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: save slot '%s' exists but could not be read"), *Slot);
		return nullptr;
	}
	UFVSaveGame* Loaded = Cast<UFVSaveGame>(Raw);
	if (!Loaded)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: save slot '%s' holds a %s, not a FishingValley profile"), *Slot, *Raw->GetClass()->GetName());
		return nullptr;
	}
	Loaded->FixupAfterLoad();
	UE_LOG(LogFV, Log, TEXT("FV: loaded profile from '%s' (level %d, money %lld, day %d)"), *Slot, Loaded->GetLevel(), Loaded->Money, Loaded->Day);
	return Loaded;
}

bool UFVSaveGame::Save()
{
	Version = CurrentVersion;
	const FString Slot = GetSlotName();
	const bool bOk = UGameplayStatics::SaveGameToSlot(this, Slot, FVSaveUserIndex);
	if (!bOk)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: failed to save profile to '%s'"), *Slot);
	}
	return bOk;
}

void UFVSaveGame::DeleteSave()
{
	const FString Slot = GetSlotName();
	if (UGameplayStatics::DoesSaveGameExist(Slot, FVSaveUserIndex))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, FVSaveUserIndex);
		UE_LOG(LogFV, Log, TEXT("FV: deleted save slot '%s'"), *Slot);
	}
}
