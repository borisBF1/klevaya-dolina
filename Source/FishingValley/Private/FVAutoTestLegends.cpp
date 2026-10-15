// Update 5 self-tests (-fvautotest), part of FVAutoTest (see FVAutoTest.h): the legends (Docs/UPDATE5_LEGENDS.md §1–§5,
// §7 step 11), the fried fish by rarity and the digging penalty (§10), the legend achievements with their bobber skins (§11).
// Lines: "FV: AUTOTEST legends <check>: ok" / "FV: AUTOTEST WARNING legends <check>: FAILED" and "FV: BALANCE_LEGEND ...".
// BALANCE_LEGEND (§5) is a recorded table: the owner accepted the legends' balance by playing (26.09.2026), so there are
// no target ranges any more; the checks are sanity only (human-strong lands every legend, the tricksters show >= 2 tricks).

#include "FVAutoTest.h"
#include "FVDatabase.h"
#include "FVGame.h"
#include "FVMiniGames.h"
#include "FVSaveGame.h"
#include "FVScene.h"
#include "FVSprites.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "UObject/Package.h"

namespace FVAutoTestU5
{
	const TCHAR* const TsarId = TEXT("fish_tsar_carp");
	const TCHAR* const SturgeonId = TEXT("fish_silver_sturgeon");
	const TCHAR* const TroutId = TEXT("fish_crystal_trout");
	const TCHAR* const AnglerId = TEXT("fish_giant_angler");
	const TCHAR* const SerpentId = TEXT("fish_fog_serpent");
	const TCHAR* const ZharId = TEXT("fish_zhar_ryba");
	const TCHAR* const MillMasterId = TEXT("fish_mill_master");   // update 6 (§18, Р53)
	/** The bait the rich profile fishes with (0.5: FVDB::Baits().Last() is bait_ember now). */
	const TCHAR* const RichBait = TEXT("lure_1_3_2");

	/** §5 «снасть по уровню» (level min(MinLevel + 1, 10), the best tackle allowed there). No target ranges: the table is
	    recorded (the owner accepted the balance by playing, 26.09.2026). */
	struct FBalanceRow
	{
		EFVLegend Legend;
		int32 Level;
		const TCHAR* Rod;
		const TCHAR* Line;
		const TCHAR* Float;
		const TCHAR* BestLine;        // «с запасом»: level 10, «Посейдон», the best line the legend allows, a Normal ukha
		const TCHAR* BestFloat;
	};
	const FBalanceRow BalanceRows[] = {
		{ EFVLegend::TsarCarp,     4,  TEXT("rod_carbon"),   TEXT("line_braided"), TEXT("float_sliding"), TEXT("line_titan"),  TEXT("float_gold") },
		{ EFVLegend::Sturgeon,     5,  TEXT("rod_carbon"),   TEXT("line_silk"),    TEXT("float_glow"),    TEXT("line_titan"),  TEXT("float_gold") },
		{ EFVLegend::CrystalTrout, 6,  TEXT("rod_titanium"), TEXT("line_silk"),    TEXT("float_glow"),    TEXT("line_fluoro"), TEXT("float_gold") },
		{ EFVLegend::Angler,       8,  TEXT("rod_iridium"),  TEXT("line_fluoro"),  TEXT("float_glow"),    TEXT("line_titan"),  TEXT("float_glow") },
		{ EFVLegend::FogSerpent,   9,  TEXT("rod_iridium"),  TEXT("line_titan"),   TEXT("float_gold"),    TEXT("line_titan"),  TEXT("float_gold") },
		{ EFVLegend::ZharRyba,     10, TEXT("rod_poseidon"), TEXT("line_heat"),    TEXT("float_gold"),    TEXT("line_heat"),   TEXT("float_gold") },
		// update 6 (§18, Р53): level 10, a strong line (the titanium string is the best one allowed); right after the
		// Zhar-ryba — its row is the reference of the Master's balance check
		{ EFVLegend::MillMaster,   10, TEXT("rod_poseidon"), TEXT("line_titan"),   TEXT("float_gold"),    TEXT("line_titan"),  TEXT("float_gold") },
	};
	/** Р53: the Zhar-ryba's BALANCE_LEGEND results (mid, strong) of this run — the Master of the Omut is judged against them. */
	FFVLegendSimResult ZharBalanceMid, ZharBalanceStr;
	bool bZharBalanceDone = false;
	constexpr int32 BalanceTrials = 300;          // §5: 300 fights per profile
	constexpr int32 BalanceSeed = 12345;
	constexpr float ReserveSlack = 0.03f;         // «с запасом» may fall this much below «по уровню» (different fights) before a note

	FString Pct(float V) { return FString::Printf(TEXT("%.1f%%"), V * 100.f); }

	bool SameImage(const FFVImage& A, const FFVImage& B) { return A.W == B.W && A.H == B.H && A.Px == B.Px; }

	const TCHAR* GateName(EFVLegendGate G)
	{
		switch (G)
		{
		case EFVLegendGate::Ok: return TEXT("Ok");
		case EFVLegendGate::NoBite: return TEXT("NoBite");
		case EFVLegendGate::Snap: return TEXT("Snap");
		default: return TEXT("?");
		}
	}

	FString LegendSlug(EFVLegend L)
	{
		const FFVFishDef* F = FVDB::FindLegend(L);
		return F ? F->Id.ToString() : FString::Printf(TEXT("legend_%d"), int32(L));
	}

	/** A catch context with explicit tackle (the rest: level 10, «Посейдон»). */
	FFVCatchContext TackleCtx(FName Bait, FName Line, FName Float)
	{
		FFVCatchContext C;
		C.Level = FV::MaxLevel;
		C.Rod = FVDB::FindRod(FName(TEXT("rod_poseidon")));
		C.Bait = Bait.IsNone() ? nullptr : FVDB::FindBait(Bait);
		if (const FFVItemDef* L = FVDB::FindItem(Line))
		{
			C.Line = Line;
			C.LineTier = L->Tier;
		}
		C.Float = Float;
		return C;
	}

	/** A full PickCatch context: place, phase, weather, depth, level, rod + tackle (luck / junk from the tackle). */
	FFVCatchContext FishingCtx(FName Location, EFVDayPhase Phase, EFVWeather Weather, float Depth, int32 Level, FName Rod, FName Bait, FName Line, FName Float)
	{
		FFVCatchContext C = TackleCtx(Bait, Line, Float);
		C.Location = Location;
		C.PhaseMask = FVTime::Bit(Phase);
		C.Weather = Weather;
		C.Depth = Depth;
		C.Level = Level;
		C.Rod = FVDB::FindRod(Rod);
		if (C.Rod)
		{
			const FFVRodStats S = FVDB::ComposeRodStats(*C.Rod, Line, Float);
			C.ExtraLuck = S.Luck - C.Rod->Luck;
			C.JunkMultScale = S.JunkMult;
		}
		return C;
	}

	struct FPickStats
	{
		int32 Bites = 0;
		int32 Target = 0;      // the legend asked about
		int32 Gated = 0;       // bites where it was reported as kept away (OutGated)
	};

	FPickStats PickMany(const FFVCatchContext& Ctx, FName Legend, int32 N, int32 Seed)
	{
		FPickStats S;
		FRandomStream Rng(Seed);
		for (int32 i = 0; i < N; ++i)
		{
			const FFVFishDef* Fish = nullptr;
			const FFVJunkDef* Junk = nullptr;
			const FFVFishDef* Gated = nullptr;
			FVDB::PickCatch(Ctx, Rng, Fish, Junk, &Gated);
			++S.Bites;
			if (Fish && Fish->Id == Legend) { ++S.Target; }
			if (Gated && Gated->Id == Legend) { ++S.Gated; }
		}
		return S;
	}

	/** Food buff of a Normal ukha (the human-strong profile of §5). */
	FFVStatMods NormalUkha()
	{
		const FFVItemDef* Soup = FVDB::FindItem(FName(TEXT("food_soup")));
		return Soup ? Soup->Buff.Scaled(FVDB::FoodPower(EFVQuality::Normal)) : FFVStatMods();
	}

	/** The simulation setup of one legend with the given tackle (FVDB::ComposeRodStats + CatchBarHeight, as Hook()). */
	FFVLegendSimSetup SimSetup(const FFVFishDef& F, int32 Level, FName Rod, FName Line, FName Float, const FFVStatMods* Food)
	{
		FFVLegendSimSetup S;
		S.Legend = F.Legend;
		S.Difficulty = float(F.Difficulty);
		S.Motion = F.Motion;
		if (const FFVRodDef* R = FVDB::FindRod(Rod))
		{
			const FFVRodStats St = FVDB::ComposeRodStats(*R, Line, Float, Food);
			S.BarHeight = FVDB::CatchBarHeight(Level, St);
			S.ReelMult = St.ReelMult;
			S.DrainMult = St.DrainMult;
		}
		return S;
	}
}

void FVAutoTest::LegendCheck(bool bOk, const FString& What)
{
	if (bOk)
	{
		++LegendOk;
		UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST legends %s: ok"), *What);
	}
	else
	{
		++LegendFailed;
		++Warnings;
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING legends %s: FAILED"), *What);
	}
}

bool FVAutoTest::RigTackle(UFVSaveGame& P, FName ItemId)
{
	const FFVRodLoadout L = P.GetLoadout(P.EquippedRod);
	if (L.Line == ItemId || L.Float == ItemId)
	{
		return true;
	}
	if (P.CountItem(ItemId) <= 0 && !P.GiveItem(ItemId, 1))
	{
		LegendCheck(false, FString::Printf(TEXT("rig: %s did not fit into the backpack"), *ItemId.ToString()));
		return false;
	}
	const FFVItemDef* Def = FVDB::FindItem(ItemId);
	const FName Old = (Def && Def->Kind == EFVItemKind::Float) ? L.Float : L.Line;
	const EFVBuyResult R = P.InstallTackle(P.EquippedRod, ItemId);
	if (R != EFVBuyResult::Ok)
	{
		LegendCheck(false, FString::Printf(TEXT("rig: install %s on %s -> %s"), *ItemId.ToString(), *P.EquippedRod.ToString(), *UFVSaveGame::BuyResultText(R)));
		return false;
	}
	// the tackle that came off goes to the chest: the rich backpack keeps its free slots (crafting, pickups)
	TArray<FFVInvStack>& Bp = P.GetContainer(EFVContainer::Backpack);
	const int32 Idx = Old.IsNone() ? INDEX_NONE : Bp.IndexOfByPredicate([Old](const FFVInvStack& S) { return S.ItemId == Old; });
	if (Idx != INDEX_NONE)
	{
		P.Transfer(EFVContainer::Backpack, Idx, 1, EFVContainer::Chest);
	}
	return true;
}

// ============================================================================================
// Fresh profile: a legend catch -> its achievement + the bobber skin; the wardrobe (one skin open, five locked)
// ============================================================================================
void FVAutoTest::AddLegendFreshSteps()
{
	using namespace FVAutoTestU5;
	auto LockedLegendSkins = [this]()
	{
		int32 Locked = 0;
		const UFVSaveGame* P = Game->GetProfile();
		for (const FFVCosmeticDef* C : FVDB::CosmeticsFor(EFVCosmeticSlot::Bobber))
		{
			if (C && C->Style >= uint8(EFVBobberStyle::Tsar) && C->Style < uint8(EFVBobberStyle::Diamond) && P && !P->OwnsCosmetic(C->Id)) { ++Locked; }
		}
		return Locked;
	};

	// a legend catch completes its achievement: the Tsar-carp -> «Повелитель пруда» + the «Царский» bobber (toast)
	AddStep(TEXT("update 5: a legend catch completes its achievement"),
		[this, LockedLegendSkins]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			const int32 LockedBefore = LockedLegendSkins();
			LegendCheck(LockedBefore == 6, FString::Printf(TEXT("a new game owns none of the six legend skins (%d locked)"), LockedBefore));
			UFVSaveGame* P = Game->GetProfile();
			const FFVFishDef* Tsar = FVDB::FindFish(FName(TsarId));
			if (!P || !Tsar)
			{
				LegendCheck(false, TEXT("the Tsar-carp catch (no profile / fish)"));
				return;
			}
			const bool bBefore = P->IsAchievementDone(FName(TEXT("ach_legend_pond"))) || P->OwnsCosmetic(FName(TEXT("bobber_tsar")));
			P->RecordCatch(*Tsar, Tsar->MaxSize * 0.8f, EFVQuality::Silver, false);
			Game->AnnounceAchievements(P->CheckAchievements());
			Game->NotifyProfileChanged();
			const bool bDone = P->IsAchievementDone(FName(TEXT("ach_legend_pond")));
			const bool bSkin = P->OwnsCosmetic(FName(TEXT("bobber_tsar")));
			LegendCheck(!bBefore && bDone && bSkin, FString::Printf(TEXT("catching the Tsar-carp completes ach_legend_pond (%s) and grants bobber_tsar (%s)"),
				bDone ? TEXT("yes") : TEXT("no"), bSkin ? TEXT("yes") : TEXT("no")));
			const int32 LockedAfter = LockedLegendSkins();
			LegendCheck(LockedAfter == 5, FString::Printf(TEXT("the other five legend skins stay locked (%d locked)"), LockedAfter));
		},
		[](float T) { return T > 0.5f; },
		TEXT("u5_achievement_legend_toast"), 0.1f, 3.f);

	// the wardrobe's «Поплавки» tab (key 3): the «Царский» skin owned, the other five locked («Награда: достижение «…»»)
	AddStep(TEXT("update 5: wardrobe bobbers (the «Царский» skin unlocked, five locked)"),
		[this]()
		{
			bStepFlag = false;
			DismissModals();
			ClearToasts();
			Game->OpenScreen(EFVScreen::Wardrobe);
		},
		[this](float T)
		{
			if (!bStepFlag && T > 0.15f)
			{
				bStepFlag = true;
				Synth.Pressed.Add(EKeys::Three);   // the bobber tab
			}
			return bStepFlag && Game->GetScreen() == EFVScreen::Wardrobe && T > 0.65f;
		},
		TEXT("u5_wardrobe_bobbers_tsar"), 0.1f, 3.f);
	AddStep(TEXT("update 5: wardrobe closed"),
		[this]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
		},
		[this](float) { return Game->GetScreen() == EFVScreen::None; }, FString(), 0.1f, 3.f);
}

// ============================================================================================
// Rich profile: rule checks, legend fights, collection cards
// ============================================================================================
void FVAutoTest::AddLegendHookSteps(const FString& Name, FName LocationId, float ClockMinutes, EFVWeather Weather, FName FishId, bool bGates,
	TFunction<void(UFVSaveGame&)> Rig, EFVFishState UntilState)
{
	AddStep(FString::Printf(TEXT("%s: travel and rig"), *Name),
		[this, LocationId, ClockMinutes, Weather, Rig]()
		{
			bStepFlag = false;
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			Game->GetFishing().SetAutoPlay(false);
			Game->GetFishing().Reset();
			ClearToasts();
			Game->SetWeather(Weather);
			if (Game->GetScene().GetLocationId() != LocationId && !Game->TravelTo(LocationId))
			{
				LegendCheck(false, FString::Printf(TEXT("travel to %s"), *LocationId.ToString()));
			}
			Game->SetClockMinutes(ClockMinutes);
			if (UFVSaveGame* P = Game->GetProfile())
			{
				if (Rig) { Rig(*P); }
				U5EscapedBefore = P->Stats.Escaped;
			}
			Game->NotifyProfileChanged();
			Game->GetScene().RefreshAppearance();
			U5BiteAt = U5HookAt = U5EscapeAt = -1.f;
		},
		[this](float T)
		{
			if (!bStepFlag && T > 0.2f)
			{
				bStepFlag = true;
				if (!TeleportToFishingSpot())
				{
					LegendCheck(false, FString::Printf(TEXT("a fishing spot in %s"), *Game->GetScene().GetLocationId().ToString()));
				}
			}
			return bStepFlag && T > 0.4f;
		},
		FString(), 0.1f, 4.f);

	AddStep(FString::Printf(TEXT("%s: cast until the legend bites"), *Name),
		[this]()
		{
			ClearToasts();
			Game->GetFishing().SetAutoPlay(true);
		},
		[this, FishId, bGates, UntilState](float)
		{
			FVFishing& F = Game->GetFishing();
			if (Game->HasModal())
			{
				DismissModals();
			}
			const EFVFishState S = F.GetState();
			if (S == EFVFishState::Waiting)
			{
				F.DebugForceBite(FishId, bGates);   // no-op until the bobber is in the water
			}
			if (S == EFVFishState::Bite && U5BiteAt < 0.f) { U5BiteAt = TotalTime; }
			if (S == EFVFishState::Minigame && U5HookAt < 0.f) { U5HookAt = TotalTime; }
			return S == UntilState;
		},
		FString(), 0.f, 25.f);
}

void FVAutoTest::AddLegendFightEndStep(const FString& Name)
{
	AddStep(FString::Printf(TEXT("%s: end"), *Name),
		[this]()
		{
			FVFishing& F = Game->GetFishing();
			F.DebugPinProgress(-1.f);
			F.SetAutoPlay(false);
			F.Reset();
			DismissModals();
			ClearToasts();
		},
		nullptr, FString(), 0.2f);
}

void FVAutoTest::AddLegendSteps()
{
	using namespace FVAutoTestU5;

	// ---- data / logic (one frame each)
	AddStep(TEXT("update 5: legend rules, supply, dishes, tokens, achievements"), [this]() { RunLegendRuleChecks(); }, nullptr, FString(), 0.1f);

	// ---- the Tsar-carp: the classic fight of a legend — golden bar, the 0.7 s hook intro under its banner
	AddLegendHookSteps(TEXT("update 5 tsar"), FVDB::StartLocation(), 9.f * 60.f, EFVWeather::Sunny, FName(TsarId), false,
		[this](UFVSaveGame& P) { RigTackle(P, FName(TEXT("line_titan"))); P.EquipBait(FName(TEXT("bait_dough"))); }, EFVFishState::Minigame);
	AddStep(TEXT("update 5 tsar: the hook intro"),
		[this]()
		{
			FVFishing& F = Game->GetFishing();
			const UFVSaveGame* P = Game->GetProfile();
			LegendCheck(F.IsLegendIntro() && F.GetMinigame().Elapsed < 0.02f,
				FString::Printf(TEXT("the hook starts the 0.7 s legend intro, the fight waits (intro %s, elapsed %.2f s)"), F.IsLegendIntro() ? TEXT("yes") : TEXT("no"), F.GetMinigame().Elapsed));
			LegendCheck(P && P->LegendsMet.Contains(FName(TsarId)), TEXT("the hooked legend is in LegendsMet at once"));
			LegendCheck(!F.GetMinigame().HasLegendRules() && F.GetMinigame().Legend == EFVLegend::TsarCarp, TEXT("the Tsar-carp fights the classic way (no legend rules)"));
			F.DebugPinProgress(0.5f);
		},
		[](float T) { return T > 0.3f; },
		TEXT("u5_tsar_hook_gold_bar"), 0.f, 3.f);
	AddStep(TEXT("update 5 tsar: after the intro"), nullptr,
		[this](float T)
		{
			FVFishing& F = Game->GetFishing();
			if (!F.IsLegendIntro() && F.GetMinigame().Elapsed > 0.3f)
			{
				LegendCheck(F.GetState() == EFVFishState::Minigame, FString::Printf(TEXT("the fight runs after the intro (elapsed %.2f s after %.2f s)"), F.GetMinigame().Elapsed, T));
				return true;
			}
			return false;
		},
		FString(), 0.f, 4.f);
	AddLegendFightEndStep(TEXT("update 5 tsar"));

	// ---- the silver sturgeon with a linen line: it bites, then snaps the leader at the end of the intro
	AddLegendHookSteps(TEXT("update 5 sturgeon snap"), FName(TEXT("river")), 10.f * 60.f, EFVWeather::Rain, FName(SturgeonId), true,
		[this](UFVSaveGame& P) { RigTackle(P, FName(TEXT("line_linen"))); P.EquipBait(FName(TEXT("bait_grub"))); }, EFVFishState::Minigame);
	AddStep(TEXT("update 5 sturgeon snap: the leader snaps"), nullptr,
		[this](float)
		{
			FVFishing& F = Game->GetFishing();
			const EFVFishState S = F.GetState();
			if (S == EFVFishState::Minigame)
			{
				return false;
			}
			U5EscapeAt = TotalTime;
			const UFVSaveGame* P = Game->GetProfile();
			const float Dt = U5HookAt >= 0.f ? U5EscapeAt - U5HookAt : -1.f;
			LegendCheck(S == EFVFishState::Escaped && Dt >= 0.6f && Dt <= 1.0f,
				FString::Printf(TEXT("a weak line: the sturgeon snaps the leader %.2f s after the hook (state %s)"), Dt, S == EFVFishState::Escaped ? TEXT("Escaped") : TEXT("other")));
			if (P)
			{
				LegendCheck(P->Stats.Escaped == U5EscapedBefore + 1, FString::Printf(TEXT("the snap counts as an escape (%d -> %d)"), U5EscapedBefore, P->Stats.Escaped));
				LegendCheck(P->GetLoadout(P->EquippedRod).Line == FName(TEXT("line_linen")), TEXT("the line stays on the rod after the snap"));
				LegendCheck(P->LegendsMet.Contains(FName(SturgeonId)), TEXT("the snapped sturgeon counts as met"));
			}
			return true;
		},
		TEXT("u5_sturgeon_snap"), 0.25f, 5.f);
	AddLegendFightEndStep(TEXT("update 5 sturgeon snap"));

	// ---- the giant angler at night: «Огонёк» (no «!», presses ignored) for 1 s, then the lantern with its ring
	AddLegendHookSteps(TEXT("update 5 angler"), FName(TEXT("ocean")), 23.f * 60.f, EFVWeather::Sunny, FName(AnglerId), true,
		[this](UFVSaveGame& P)
		{
			RigTackle(P, FName(TEXT("line_fluoro")));
			RigTackle(P, FName(TEXT("float_glow")));
			P.EquipBait(FName(TEXT("bait_magic")));
			P.EquipCosmetic(FName(TEXT("bobber_lantern")));   // the «Фонарь удильщика» shines at night
		},
		EFVFishState::Bite);
	AddStep(TEXT("update 5 angler: the glow before the «!»"), nullptr,
		[this](float T)
		{
			FVFishing& F = Game->GetFishing();
			if (F.GetState() != EFVFishState::Bite)
			{
				LegendCheck(false, TEXT("the angler's glow lasts 1 s before the «!»"));
				return true;
			}
			if (T > 0.5f)
			{
				const FString Hint = F.GetHint();
				LegendCheck(Hint.Contains(TEXT("Огонёк")), FString::Printf(TEXT("the hint during the glow is «%s»"), *Hint));
				return true;
			}
			return false;
		},
		TEXT("u5_angler_glow_lantern_bobber"), 0.f, 3.f);
	AddStep(TEXT("update 5 angler: hooked after the «!»"), nullptr,
		[this](float)
		{
			FVFishing& F = Game->GetFishing();
			if (F.GetState() == EFVFishState::Minigame)
			{
				U5HookAt = TotalTime;
				const float Dt = U5BiteAt >= 0.f ? U5HookAt - U5BiteAt : -1.f;
				LegendCheck(Dt >= 1.0f, FString::Printf(TEXT("the bot hooks the angler only after the glow (%.2f s after the bite)"), Dt));
				F.DebugPinProgress(0.5f);
				return true;
			}
			return F.GetState() != EFVFishState::Bite;
		},
		FString(), 0.f, 4.f);
	AddStep(TEXT("update 5 angler: the lantern and its ring"), nullptr,
		[this](float)
		{
			const FFVMinigame& M = Game->GetFishing().GetMinigame();
			return Game->GetFishing().GetState() == EFVFishState::Minigame && M.HazardState == 1 && !M.bFakeLantern && M.HazardAge > 0.6f;
		},
		TEXT("u5_angler_lantern_ring"), 0.f, 20.f);
	AddLegendFightEndStep(TEXT("update 5 angler"));

	// ---- the Zhar-ryba with both hard rules met: an eruption (the gold bar on the dark veil), then the last dash
	AddLegendHookSteps(TEXT("update 5 zhar"), FName(TEXT("volcano")), 12.f * 60.f, EFVWeather::Sunny, FName(ZharId), true,
		[this](UFVSaveGame& P)
		{
			RigTackle(P, FName(TEXT("line_heat")));
			RigTackle(P, FName(TEXT("float_gold")));
			P.EquipBait(FName(TEXT("bait_ember")));
		},
		EFVFishState::Minigame);
	AddStep(TEXT("update 5 zhar: an eruption"),
		[this]() { Game->GetFishing().DebugPinProgress(0.4f); },
		[this](float)
		{
			const FFVMinigame& M = Game->GetFishing().GetMinigame();
			return Game->GetFishing().GetState() == EFVFishState::Minigame && M.HazardState == 2 && M.HazardAge > 0.25f;
		},
		TEXT("u5_zhar_eruption"), 0.f, 15.f);
	// the last dash in the real fight (RunLegendTricksChecks checks it in the simulation): the only shot of phase 2 —
	// the blink spark with its shrinking ring and the narrowed bar, in both orientations
	AddStep(TEXT("update 5 zhar: the last dash (blink spark, narrow bar)"),
		[this]() { Game->GetFishing().DebugPinProgress(0.9f); },
		[this](float)
		{
			const FFVMinigame& M = Game->GetFishing().GetMinigame();
			if (Game->GetFishing().GetState() == EFVFishState::Minigame && M.Phase == 2 && M.BlinkState == 1 && M.BlinkTimer < 0.5f)
			{
				LegendCheck(M.BarHeight <= M.BaseBarHeight * 0.76f, FString::Printf(TEXT("the rage narrows the bar (%.0f of %.0f)"), M.BarHeight, M.BaseBarHeight));
				return true;
			}
			return false;
		},
		TEXT("u5_zhar_last_dash"), 0.f, 15.f);
	AddLegendFightEndStep(TEXT("update 5 zhar"));

	// ---- update 6 (§18, Р53): the Master of the Omut (forced bite, no gates: the fight itself is shot) — the whirl
	// surge, the knock before a jerk of the millstones, the catfish half-hidden in the silt
	AddStep(TEXT("update 6 mill master: open the mill"),
		[this]() { if (UFVSaveGame* P = Game->GetProfile()) { P->UnlockLocation(FName(TEXT("mill"))); } }, nullptr, FString(), 0.1f);
	AddLegendHookSteps(TEXT("update 6 mill master"), FName(TEXT("mill")), 23.f * 60.f, EFVWeather::Sunny, FName(MillMasterId), false,
		[this](UFVSaveGame& P)
		{
			RigTackle(P, FName(TEXT("line_titan")));
			RigTackle(P, FName(TEXT("float_gold")));
			P.EquipBait(FName(TEXT("bait_kolobok")));
		},
		EFVFishState::Minigame);
	AddStep(TEXT("update 6 mill master: «Омут», the whirl surge"),
		[this]() { Game->GetFishing().DebugPinProgress(0.3f); },
		[this](float)
		{
			const FFVMinigame& M = Game->GetFishing().GetMinigame();
			return Game->GetFishing().GetState() == EFVFishState::Minigame && M.Legend == EFVLegend::MillMaster && M.Phase == 0 && M.HazardState == 2 && M.HazardAge > 0.3f;
		},
		TEXT("u6_mill_master_whirl"), 0.f, 15.f);
	AddStep(TEXT("update 6 mill master: «Жернова», the knock"),
		[this]() { Game->GetFishing().DebugPinProgress(0.5f); },
		[this](float)
		{
			const FFVMinigame& M = Game->GetFishing().GetMinigame();
			return Game->GetFishing().GetState() == EFVFishState::Minigame && M.Phase == 1 && M.HazardState == 1 && M.HazardAge > 0.3f;
		},
		TEXT("u6_mill_master_stones"), 0.f, 15.f);
	AddStep(TEXT("update 6 mill master: «Муть», half-hidden"),
		[this]() { Game->GetFishing().DebugPinProgress(0.7f); },
		[this](float)
		{
			const FFVMinigame& M = Game->GetFishing().GetMinigame();
			if (Game->GetFishing().GetState() == EFVFishState::Minigame && M.Phase == 2 && M.HazardState == 2 && M.FishAlpha < 0.35f)
			{
				LegendCheck(M.FishAlpha > 0.05f, FString::Printf(TEXT("the silt hides the Master only partly (alpha %.2f)"), M.FishAlpha));
				return true;
			}
			return false;
		},
		TEXT("u6_mill_master_silt"), 0.f, 20.f);
	AddLegendFightEndStep(TEXT("update 6 mill master"));

	// ---- back to the rich profile's tackle
	AddStep(TEXT("update 5: rich tackle back"),
		[this]()
		{
			if (UFVSaveGame* P = Game->GetProfile())
			{
				RigTackle(*P, FName(TEXT("line_titan")));
				P->EquipBait(FName(RichBait));
				// the tackle the fights took off goes to the chest: the backpack keeps its free slots (crafting, pickups)
				const TArray<FName> Extra = { FName(TEXT("line_linen")), FName(TEXT("line_fluoro")), FName(TEXT("line_heat")), FName(TEXT("float_glow")) };
				TArray<FFVInvStack>& Bp = P->GetContainer(EFVContainer::Backpack);
				for (int32 i = Bp.Num() - 1; i >= 0; --i)
				{
					if (Bp.IsValidIndex(i) && Extra.Contains(Bp[i].ItemId))
					{
						P->Transfer(EFVContainer::Backpack, i, Bp[i].Count, EFVContainer::Chest);
					}
				}
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST legends: backpack %d/%d slots after the fights"), P->ContainerUsed(EFVContainer::Backpack), P->ContainerCapacity(EFVContainer::Backpack));
			}
			Game->NotifyProfileChanged();
		},
		nullptr, FString(), 0.1f);

	// ---- the collection: an unmet legend (checklist, no fight text) and a met one («Приёмы»)
	AddStep(TEXT("update 5: collection of an unmet legend"),
		[this]()
		{
			DismissModals();
			const UFVSaveGame* P = Game->GetProfile();
			LegendCheck(P && !P->HasMetLegend(FName(TroutId)), TEXT("the crystal trout was never met (its card hides the fight text)"));
			Game->OpenScreenWith(EFVScreen::Collection, FName(TroutId));
		},
		[this](float T) { return Game->GetScreen() == EFVScreen::Collection && T > 0.3f; },
		TEXT("u5_collection_trout_unmet"), 0.1f, 3.f);
	AddStep(TEXT("update 5: collection of a met legend"),
		[this]()
		{
			const UFVSaveGame* P = Game->GetProfile();
			LegendCheck(P && P->HasMetLegend(FName(ZharId)) && !P->GetRecord(FName(ZharId)), TEXT("the Zhar-ryba is met but not caught (its card shows «Приёмы»)"));
			Game->CloseScreen();
			Game->OpenScreenWith(EFVScreen::Collection, FName(ZharId));
		},
		[this](float T) { return Game->GetScreen() == EFVScreen::Collection && T > 0.3f; },
		TEXT("u5_collection_zhar_met"), 0.1f, 3.f);

	AddStep(TEXT("update 5: screens closed"),
		[this]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST legends: world checks done (%d ok, %d failed so far)"), LegendOk, LegendFailed);
		},
		nullptr, FString(), 0.2f);
}

// ============================================================================================
// Kitchen by rarity (set-ups; the legend dish cooked), digging (timer, empty-hole penalty, played to the end)
// ============================================================================================
void FVAutoTest::AddUpdate5MiniGameSteps()
{
	// Vessel: 0 pan, 1 pot. The ember bait (a bait output, two concrete fish inputs) cooks in the pot and gives 4 baits
	// (its Craft is checked on a scratch profile in RunLegendRuleChecks). Every recipe's set-up is checked on the screen
	// (cuts, stage speed, vessel, the fish on the board); only the legend dish is cooked to the end by the bot (the one
	// full cooking game of the run, with its minigame_cooking_play / _result shots) — the others are closed like «Назад».
	struct FDish { const TCHAR* Recipe; const TCHAR* Token; const TCHAR* Dish; EFVRarity Rarity; int32 Cuts; float Speed; int32 Vessel; int32 Out; bool bCook; };
	static const FDish Dishes[] = {
		{ TEXT("rcp_fried"),        TEXT("any_fish"),        TEXT("food_fried"),        EFVRarity::Common,    2, 1.0f, 0, 1, false },
		{ TEXT("rcp_fried_rare"),   TEXT("any_fish_rare"),   TEXT("food_fried_rare"),   EFVRarity::Rare,      2, 1.1f, 0, 1, false },
		{ TEXT("rcp_fried_epic"),   TEXT("any_fish_epic"),   TEXT("food_fried_epic"),   EFVRarity::Epic,      3, 1.2f, 0, 1, false },
		{ TEXT("rcp_bait_ember"),   TEXT("fish_sparkfin"),   TEXT("bait_ember"),        EFVRarity::Common,    3, 1.0f, 1, 4, false },
		{ TEXT("rcp_fried_legend"), TEXT("any_fish_legend"), TEXT("food_fried_legend"), EFVRarity::Legendary, 4, 1.4f, 0, 1, true },
	};
	for (const FDish& Ds : Dishes)
	{
		const FDish D = Ds;
		AddStep(FString::Printf(TEXT("update 5 cooking %s: %s"), D.Recipe, D.bCook ? TEXT("open") : TEXT("set-up, closed before cooking")),
			[this, D]()
			{
				bStepFlag = false;
				DismissModals();
				if (Game->GetScreen() != EFVScreen::None)
				{
					Game->CloseScreen();
				}
				ClearToasts();
				UFVSaveGame* P = Game->GetProfile();
				if (!P)
				{
					return;
				}
				const bool bBait = FVDB::IsBaitId(FName(D.Dish));
				if (bBait)
				{
					// the ember bait: its own fish (2 sparkfins + 1 fire perch) and berries
					const bool bGiven = P->GiveItem(FName(TEXT("fish_sparkfin")), 2) && P->GiveItem(FName(TEXT("fish_fire_perch")), 1) && P->GiveItem(FName(TEXT("res_berries")), 1);
					LegendCheck(bGiven, TEXT("cooking: the ember bait's ingredients fit"));
				}
				else
				{
					// a fish of exactly this rarity in the keepnet (the first one of the table)
					for (const FFVFishDef& F : FVDB::Fish())
					{
						if (F.Rarity == D.Rarity)
						{
							if (!P->AddToKeepnet(F.Id, EFVQuality::Normal, 1))
							{
								LegendCheck(false, FString::Printf(TEXT("cooking: %s did not fit into the keepnet"), *F.Id.ToString()));
							}
							break;
						}
					}
				}
				// room for the dish (a new quality is a new stack): the rich backpack is nearly full — junk goes to the chest
				TArray<FFVInvStack>& Bp = P->GetContainer(EFVContainer::Backpack);
				for (int32 i = Bp.Num() - 1; i >= 0 && P->ContainerUsed(EFVContainer::Backpack) >= P->ContainerCapacity(EFVContainer::Backpack); --i)
				{
					if (Bp.IsValidIndex(i) && FVDB::IsJunkId(Bp[i].ItemId)) { P->Transfer(EFVContainer::Backpack, i, Bp[i].Count, EFVContainer::Chest); }
				}
				U5DishBefore = bBait ? P->BaitCount(FName(D.Dish)) : P->CountItem(FName(D.Dish));
				U5TokenBefore = P->CountIngredient(FName(D.Token));
				U5Serial = FVMiniGameDebug().CookSerial;
				Game->OpenScreenWith(EFVScreen::Cooking, FName(D.Recipe));
			},
			[this, D](float T)
			{
				if (T > 0.1f && !bStepFlag)
				{
					bStepFlag = true;
					const FFVMiniGameDebug& Dbg = FVMiniGameDebug();
					const FFVFishDef* Fish = FVDB::FindFish(Dbg.CookFish);
					const bool bOpened = Game->GetScreen() == EFVScreen::Cooking && Dbg.CookSerial == U5Serial + 1 && Dbg.CookRecipe == FName(D.Recipe);
					LegendCheck(bOpened, FString::Printf(TEXT("cooking %s opens"), D.Recipe));
					if (bOpened)
					{
						LegendCheck(Dbg.CookCuts == D.Cuts && FMath::IsNearlyEqual(Dbg.CookSpeed, D.Speed, 0.001f) && Dbg.CookVessel == D.Vessel,
							FString::Printf(TEXT("cooking %s: %d cuts (want %d), stage speed x%.2f (want x%.2f), vessel %d (want %d)"), D.Recipe, Dbg.CookCuts, D.Cuts, Dbg.CookSpeed, D.Speed, Dbg.CookVessel, D.Vessel));
						const bool bFishOk = FVDB::IsFishId(FName(D.Token)) ? (Fish && Fish->Id == FName(D.Token)) : (Fish && (D.Rarity == EFVRarity::Common || Fish->Rarity == D.Rarity));
						LegendCheck(bFishOk, FString::Printf(TEXT("cooking %s: the board shows the right fish (%s)"), D.Recipe, *Dbg.CookFish.ToString()));
					}
				}
				if (!bStepFlag)
				{
					return false;
				}
				if (D.bCook)
				{
					return T >= 0.3f;
				}
				// set-up only: leave the screen as «Назад» -> «Бросить» does — nothing may be spent
				if (Game->GetScreen() == EFVScreen::Cooking)
				{
					Game->CloseScreen();
					return false;
				}
				if (const UFVSaveGame* P = Game->GetProfile())
				{
					const bool bBait = FVDB::IsBaitId(FName(D.Dish));
					const int32 Dish = bBait ? P->BaitCount(FName(D.Dish)) : P->CountItem(FName(D.Dish));
					const int32 Token = P->CountIngredient(FName(D.Token));
					LegendCheck(Dish == U5DishBefore && Token == U5TokenBefore,
						FString::Printf(TEXT("cooking %s left before cooking spends nothing (%s %d -> %d, %s %d -> %d)"), D.Recipe, D.Dish, U5DishBefore, Dish, D.Token, U5TokenBefore, Token));
				}
				return true;
			},
			FString(), 0.05f, 4.f);
		if (!D.bCook)
		{
			continue;
		}
		AddStep(FString::Printf(TEXT("update 5 cooking %s: the bot cooks"), D.Recipe),
			[this]() { bStepFlag = false; },
			[this](float) { return Game->GetScreen() != EFVScreen::Cooking; },
			FString(), 0.2f, 14.f);
		AddStep(FString::Printf(TEXT("update 5 cooking %s: the dish"), D.Recipe),
			[this, D]()
			{
				bStepFlag = false;
				if (const UFVSaveGame* P = Game->GetProfile())
				{
					const bool bBait = FVDB::IsBaitId(FName(D.Dish));
					const int32 Dish = bBait ? P->BaitCount(FName(D.Dish)) : P->CountItem(FName(D.Dish));
					const int32 Token = P->CountIngredient(FName(D.Token));
					const int32 Used = bBait ? 2 : 1;   // the ember bait takes two sparkfins
					// update 6: «Русская печь» of the rich profile cooks two portions (FVCookedRecipe)
					int32 Out = D.Out;
					if (const FFVRecipeDef* Rc = FVDB::FindRecipe(FName(D.Recipe)))
					{
						if (Rc->OutputCount > 0) { Out = D.Out * FVCookedRecipe(*P, *Rc).OutputCount / Rc->OutputCount; }
					}
					LegendCheck(Dish == U5DishBefore + Out && Token == U5TokenBefore - Used,
						FString::Printf(TEXT("cooking %s gives %d %s (%d -> %d) and uses %d %s (%d -> %d)"), D.Recipe, Out, D.Dish, U5DishBefore, Dish, Used, D.Token, U5TokenBefore, Token));
				}
				DismissModals();
				if (Game->GetScreen() != EFVScreen::None)
				{
					Game->CloseScreen();
				}
			},
			nullptr, FString(), 0.1f);
	}

	// ---- digging (the run's one digging game): the timer of the shovel, a tap on an already dug hole without a worm costs
	// 0.5 s, then the bot digs to the end (its minigame_digging_play / _result shots)
	AddStep(TEXT("update 5 digging: open"),
		[this]()
		{
			bStepFlag = false;
			U5Frames = 0;
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			FVMiniGameDebug().bDigPlaying = false;
			Game->OpenScreenWith(EFVScreen::Digging, FName(*FString::Printf(TEXT("%s:0"), *FVDB::HomeId().ToString())));
		},
		[this](float)
		{
			const FFVMiniGameDebug& Dbg = FVMiniGameDebug();
			return Game->GetScreen() == EFVScreen::Digging && Dbg.bDigPlaying && Dbg.DigEmptyHole.X >= 0.f;
		},
		FString(), 0.f, 5.f);
	AddStep(TEXT("update 5 digging: a tap on an empty hole"),
		[this]()
		{
			const FFVMiniGameDebug& Dbg = FVMiniGameDebug();
			U5Serial = Dbg.DigPenalties;
			U5Frames = 0;
			const UFVSaveGame* P = Game->GetProfile();
			const float Want = P ? P->DigSeconds() : 0.f;
			LegendCheck(FMath::IsNearlyEqual(Dbg.DigTimeTotal, FMath::Max(3.f, Want), 0.01f),
				FString::Printf(TEXT("the dig timer starts at the shovel's %.1f s (%.1f s)"), Want, Dbg.DigTimeTotal));
		},
		[this](float)
		{
			const FFVMiniGameDebug& Dbg = FVMiniGameDebug();
			if (Dbg.DigPenalties > U5Serial)
			{
				LegendCheck(Dbg.DigPenalties == U5Serial + 1 && Dbg.DigLastPenalty >= 0.49f && Dbg.DigLastPenalty <= 0.51f,
					FString::Printf(TEXT("a tap on an empty dug hole costs %.2f s (%d penalty)"), Dbg.DigLastPenalty, Dbg.DigPenalties - U5Serial));
				return true;
			}
			if (!Dbg.bDigPlaying || Game->GetScreen() != EFVScreen::Digging)
			{
				LegendCheck(false, TEXT("a tap on an empty dug hole costs half a second (the play phase ended first)"));
				return true;
			}
			// the player's tap: a finger, or the mouse (hover, press, release) on the hole
			++U5Frames;
			const FVector2D Pos = Dbg.DigEmptyHole.X >= 0.f ? Dbg.DigEmptyHole : Synth.Mouse;
			if (FV::IsTouchUI())
			{
				if (U5Frames <= 3)
				{
					FFVTouchPoint Tp;
					Tp.Id = 7;
					Tp.Pos = Pos;
					Synth.Touches.Add(Tp);
				}
			}
			else
			{
				Synth.Mouse = Pos;
				Synth.bMouseValid = true;
				Synth.bLmbPressed = U5Frames == 2;
				Synth.bLmbDown = U5Frames >= 2 && U5Frames <= 3;
				Synth.bLmbReleased = U5Frames == 4;
			}
			if (U5Frames > 30)
			{
				LegendCheck(false, TEXT("a tap on an empty dug hole costs half a second (no penalty after the tap)"));
				return true;
			}
			return false;
		},
		TEXT("u5_digging_empty_hole_penalty"), 0.05f, 3.f);
	AddStep(TEXT("update 5 digging: the bot finishes"), nullptr,
		[this](float) { return Game->GetScreen() != EFVScreen::Digging; },
		FString(), 0.2f, 14.f);
	AddStep(TEXT("update 5 digging: back"),
		[this]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
		},
		nullptr, FString(), 0.1f);
}

// ============================================================================================
// Pure checks
// ============================================================================================
void FVAutoTest::RunLegendRuleChecks()
{
	using namespace FVAutoTestU5;

	// ---- §4: the six legends, their templates (sizes) and the checklist rows
	struct FExpect { EFVLegend L; const TCHAR* Id; int32 W, H, Rows; bool bToast, bSnap; };
	const FExpect Ex[] = {
		{ EFVLegend::TsarCarp,     TsarId,     32, 19, 0, false, false },
		{ EFVLegend::Sturgeon,     SturgeonId, 40, 13, 1, false, true },
		{ EFVLegend::CrystalTrout, TroutId,    30, 14, 1, true,  false },
		{ EFVLegend::Angler,       AnglerId,   28, 19, 2, true,  true },
		{ EFVLegend::FogSerpent,   SerpentId,  42, 14, 2, true,  true },
		{ EFVLegend::ZharRyba,     ZharId,     33, 20, 2, true,  false },
	};
	TSet<int32> Sizes;
	for (const FExpect& E : Ex)
	{
		const FFVFishDef* F = FVDB::FindLegend(E.L);
		const bool bDef = F && F->Id == FName(E.Id) && F->bLegendary;
		LegendCheck(bDef, FString::Printf(TEXT("FindLegend(%d) is %s"), int32(E.L), E.Id));
		if (!bDef)
		{
			continue;
		}
		const FFVImage Img = UFVSprites::BuildFish(*F, false);
		const FFVImage Sil = UFVSprites::BuildFish(*F, true);
		LegendCheck(Img.W == E.W && Img.H == E.H && Img.H <= 20 && Sil.W == Img.W && Sil.H == Img.H,
			FString::Printf(TEXT("%s sprite %dx%d (want %dx%d, silhouette %dx%d)"), E.Id, Img.W, Img.H, E.W, E.H, Sil.W, Sil.H));
		Sizes.Add(Img.W * 100 + Img.H);
		const int32 Rows = FVDB::LegendChecklist(*F, TackleCtx(NAME_None, NAME_None, NAME_None)).Num();
		LegendCheck(Rows == E.Rows, FString::Printf(TEXT("%s checklist rows %d (want %d)"), E.Id, Rows, E.Rows));
		const FFVLegendPrep& Pr = F->Prep;
		LegendCheck(!Pr.PrepText.IsEmpty() && !Pr.FightText.IsEmpty() && Pr.GatedToast.IsEmpty() != E.bToast && Pr.SnapText.IsEmpty() != E.bSnap,
			FString::Printf(TEXT("%s texts (prep, fight, gated toast %s, snap %s)"), E.Id, E.bToast ? TEXT("yes") : TEXT("no"), E.bSnap ? TEXT("yes") : TEXT("no")));
	}
	LegendCheck(Sizes.Num() == 6, FString::Printf(TEXT("the six legend silhouettes differ in size (%d sizes)"), Sizes.Num()));

	// ---- art stubs replaced: warning icons, the ember bait, the plates by rarity, the six legend bobbers (animated)
	{
		const FFVImage Exclaim = UFVSprites::BuildIcon(EFVIcon::Exclaim);
		const FFVImage Missing = UFVSprites::BuildMissing();
		const EFVIcon Warn[] = { EFVIcon::WarnCurrent, EFVIcon::WarnEye, EFVIcon::WarnLantern, EFVIcon::WarnCloud, EFVIcon::WarnFlame, EFVIcon::WarnSpark };
		int32 OkIcons = 0;
		for (EFVIcon I : Warn)
		{
			const FFVImage Img = UFVSprites::BuildIcon(I);
			if (Img.W == 16 && Img.H == 16 && !SameImage(Img, Exclaim) && !SameImage(Img, Missing)) { ++OkIcons; }
		}
		LegendCheck(OkIcons == 6, FString::Printf(TEXT("six own 16x16 warning icons (%d)"), OkIcons));
		LegendCheck(!SameImage(UFVSprites::BuildBait(EFVBaitStyle::Ember), UFVSprites::BuildBait(EFVBaitStyle::Magic)), TEXT("the ember bait has its own sprite"));
		const FFVImage Plates[] = { UFVSprites::BuildItemArt(EFVItemArt::FriedFish, FColor::White, 0), UFVSprites::BuildItemArt(EFVItemArt::FriedFishRare, FColor::White, 0),
			UFVSprites::BuildItemArt(EFVItemArt::FriedFishEpic, FColor::White, 0), UFVSprites::BuildItemArt(EFVItemArt::FriedFishLegend, FColor::White, 0) };
		bool bPlates = true;
		for (int32 a = 0; a < 4; ++a)
		{
			for (int32 b = a + 1; b < 4; ++b) { bPlates &= !SameImage(Plates[a], Plates[b]); }
		}
		LegendCheck(bPlates, TEXT("the four fried dishes have four different plates"));
		const FFVImage Classic = UFVSprites::BuildBobber(EFVBobberStyle::Classic, 0);
		int32 OkBobbers = 0;
		for (int32 s = int32(EFVBobberStyle::Tsar); s < int32(EFVBobberStyle::Diamond); ++s)
		{
			const FFVImage F0 = UFVSprites::BuildBobber(EFVBobberStyle(s), 0);
			const FFVImage F1 = UFVSprites::BuildBobber(EFVBobberStyle(s), 1);
			if (F0.IsValid() && !SameImage(F0, Classic) && !SameImage(F0, F1)) { ++OkBobbers; }
		}
		LegendCheck(OkBobbers == 6, FString::Printf(TEXT("six own legend bobber styles with two frames (%d)"), OkBobbers));
		if (UFVSprites* Sp = Game->GetSprites())
		{
			// the long legends still read on the trophy wall (downscaled into the plaque); the rarity tokens have icons
			const FFVFishDef* Fog = FVDB::FindLegend(EFVLegend::FogSerpent);
			const FFVSprite* Plaque = Fog ? &Sp->TrophyPlaque(*Fog) : nullptr;
			LegendCheck(Plaque && Plaque->IsValid() && Plaque->W <= 30.f && Plaque->H <= 22.f,
				FString::Printf(TEXT("the fog serpent's trophy plaque fits (%.0fx%.0f, at most 30x22)"), Plaque ? Plaque->W : 0.f, Plaque ? Plaque->H : 0.f));
			int32 Tokens = 0;
			const FName TokenIds[] = { FVDB::AnyFishRareToken(), FVDB::AnyFishEpicToken(), FVDB::AnyFishLegendToken() };
			for (const FName Tok : TokenIds)
			{
				if (Sp->ItemIcon(Tok).IsValid()) { ++Tokens; }
			}
			LegendCheck(Tokens == 3, FString::Printf(TEXT("the rarity tokens have icons (%d of 3)"), Tokens));
		}
	}

	// ---- §3: LegendGate (Missing bits: bait 1, float 2, line kind 4, line tier 8)
	struct FGateCase { const TCHAR* Fish; const TCHAR* Bait; const TCHAR* Line; const TCHAR* Float; EFVLegendGate Want; uint8 Missing; };
	const FGateCase Gates[] = {
		{ TsarId,     nullptr,                   nullptr,               nullptr,              EFVLegendGate::Ok,     0 },
		{ SturgeonId, nullptr,                   TEXT("line_linen"),    nullptr,              EFVLegendGate::Snap,   8 },
		{ SturgeonId, nullptr,                   nullptr,               nullptr,              EFVLegendGate::Snap,   8 },
		{ SturgeonId, nullptr,                   TEXT("line_braided"),  nullptr,              EFVLegendGate::Ok,     0 },
		{ TroutId,    nullptr,                   TEXT("line_titan"),    nullptr,              EFVLegendGate::NoBite, 4 },
		{ TroutId,    nullptr,                   TEXT("line_silk"),     nullptr,              EFVLegendGate::Ok,     0 },
		{ TroutId,    nullptr,                   TEXT("line_fluoro"),   nullptr,              EFVLegendGate::Ok,     0 },
		{ AnglerId,   nullptr,                   TEXT("line_fluoro"),   nullptr,              EFVLegendGate::NoBite, 2 },
		{ AnglerId,   nullptr,                   TEXT("line_silk"),     TEXT("float_glow"),   EFVLegendGate::Snap,   8 },
		{ AnglerId,   nullptr,                   TEXT("line_fluoro"),   TEXT("float_glow"),   EFVLegendGate::Ok,     0 },
		{ SerpentId,  TEXT("bait_worm"),         TEXT("line_fluoro"),   nullptr,              EFVLegendGate::NoBite, 1 },
		{ SerpentId,  TEXT("bait_nightcrawler"), TEXT("line_silk"),     nullptr,              EFVLegendGate::Snap,   8 },
		{ SerpentId,  TEXT("bait_nightcrawler"), TEXT("line_fluoro"),   nullptr,              EFVLegendGate::Ok,     0 },
		{ ZharId,     nullptr,                   nullptr,               nullptr,              EFVLegendGate::NoBite, 5 },
		{ ZharId,     TEXT("bait_ember"),        TEXT("line_titan"),    nullptr,              EFVLegendGate::NoBite, 4 },
		{ ZharId,     TEXT("bait_worm"),         TEXT("line_heat"),     nullptr,              EFVLegendGate::NoBite, 1 },
		{ ZharId,     TEXT("bait_ember"),        TEXT("line_heat"),     nullptr,              EFVLegendGate::Ok,     0 },
	};
	for (const FGateCase& G : Gates)
	{
		const FFVFishDef* F = FVDB::FindFish(FName(G.Fish));
		if (!F)
		{
			LegendCheck(false, FString::Printf(TEXT("gate: no fish %s"), G.Fish));
			continue;
		}
		const FFVLegendGateResult R = FVDB::LegendGate(*F, TackleCtx(G.Bait ? FName(G.Bait) : NAME_None, G.Line ? FName(G.Line) : NAME_None, G.Float ? FName(G.Float) : NAME_None));
		LegendCheck(R.Result == G.Want && R.Missing == G.Missing,
			FString::Printf(TEXT("gate %s (bait %s, line %s, float %s) = %s/%d (want %s/%d)"), G.Fish, G.Bait ? G.Bait : TEXT("-"), G.Line ? G.Line : TEXT("-"),
				G.Float ? G.Float : TEXT("-"), GateName(R.Result), int32(R.Missing), GateName(G.Want), int32(G.Missing)));
	}

	// ---- §5 ban: without the hard preparation a legend never bites (PickCatch x 3000), with it it does
	struct FBan { const TCHAR* Fish; const TCHAR* Loc; EFVDayPhase Phase; const TCHAR* BaitNo; const TCHAR* LineNo; const TCHAR* FloatNo; const TCHAR* BaitYes; const TCHAR* LineYes; const TCHAR* FloatYes; };
	const FBan Bans[] = {
		{ ZharId,    TEXT("volcano"),  EFVDayPhase::Day,     RichBait,           TEXT("line_titan"),  TEXT("float_gold"), TEXT("bait_ember"),        TEXT("line_heat"),   TEXT("float_gold") },
		{ TroutId,   TEXT("mountain"), EFVDayPhase::Morning, TEXT("lure_1_1_0"), TEXT("line_titan"),  TEXT("float_gold"), TEXT("lure_1_1_0"),        TEXT("line_silk"),   TEXT("float_gold") },
		{ AnglerId,  TEXT("ocean"),    EFVDayPhase::Night,   TEXT("bait_magic"), TEXT("line_fluoro"), TEXT("float_gold"), TEXT("bait_magic"),        TEXT("line_fluoro"), TEXT("float_glow") },
		{ SerpentId, TEXT("swamp"),    EFVDayPhase::Night,   TEXT("bait_worm"),  TEXT("line_fluoro"), TEXT("float_gold"), TEXT("bait_nightcrawler"), TEXT("line_fluoro"), TEXT("float_gold") },
	};
	int32 BanSeed = 7001;
	for (const FBan& B : Bans)
	{
		const FFVCatchContext No = FishingCtx(FName(B.Loc), B.Phase, EFVWeather::Sunny, 0.7f, FV::MaxLevel, FName(TEXT("rod_poseidon")), FName(B.BaitNo), FName(B.LineNo), FName(B.FloatNo));
		const FFVCatchContext Yes = FishingCtx(FName(B.Loc), B.Phase, EFVWeather::Sunny, 0.7f, FV::MaxLevel, FName(TEXT("rod_poseidon")), FName(B.BaitYes), FName(B.LineYes), FName(B.FloatYes));
		const FPickStats SN = PickMany(No, FName(B.Fish), 3000, BanSeed++);
		const FPickStats SY = PickMany(Yes, FName(B.Fish), 3000, BanSeed++);
		LegendCheck(SN.Target == 0 && SN.Gated > 0, FString::Printf(TEXT("ban: %s without its preparation bites 0 of 3000 (%d; kept away and reported %d times)"), B.Fish, SN.Target, SN.Gated));
		LegendCheck(SY.Target > 0 && SY.Gated == 0, FString::Printf(TEXT("ban: %s with its preparation bites (%d of 3000)"), B.Fish, SY.Target));
	}

	// ---- §5 supply: nightcrawlers per serpent bite (swamp, night, depth 0.7, level 9 tackle: iridium + fluoro + golden float)
	{
		const FFVCatchContext C = FishingCtx(FName(TEXT("swamp")), EFVDayPhase::Night, EFVWeather::Sunny, 0.7f, 9, FName(TEXT("rod_iridium")),
			FName(TEXT("bait_nightcrawler")), FName(TEXT("line_fluoro")), FName(TEXT("float_gold")));
		const FPickStats S = PickMany(C, FName(SerpentId), 3000, 7101);
		const float PerBite = S.Target > 0 ? float(S.Bites) / float(S.Target) : 999.f;
		LegendCheck(PerBite <= 6.f, FString::Printf(TEXT("supply: %.2f nightcrawlers per serpent bite (<= 6; %d serpents of %d bites)"), PerBite, S.Target, S.Bites));
	}
	// ---- update 6 §0.3 (Р45): the Zhar-ryba's share of the bites on the ember bait — 10 000 casts with the best tackle
	// in the deep volcano water (depth 1, level 10, «Посейдон» + heat line + golden float): 14..24 % (was ~50 %)
	{
		const FFVCatchContext C = FishingCtx(FName(TEXT("volcano")), EFVDayPhase::Day, EFVWeather::Sunny, 1.f, FV::ClassicMaxLevel, FName(TEXT("rod_poseidon")),
			FName(TEXT("bait_ember")), FName(TEXT("line_heat")), FName(TEXT("float_gold")));
		const FPickStats S = PickMany(C, FName(ZharId), 10000, 7202);
		const float Share = float(S.Target) / float(FMath::Max(1, S.Bites));
		LegendCheck(Share >= 0.14f && Share <= 0.24f, FString::Printf(TEXT("supply: the Zhar-ryba takes %s of 10000 ember bites at depth 1 (14..24 %%)"), *Pct(Share)));
	}
	// ... and the same measurement for every legend on its favourite bait (recorded only, for control): its place, its
	// first time of day and weather, depth 1, level 10, «Посейдон» + the best line / float the legend allows
	{
		struct FFav { EFVLegend Legend; const TCHAR* Bait; };
		const FFav Favs[] = {
			{ EFVLegend::TsarCarp, TEXT("bait_dough") }, { EFVLegend::Sturgeon, TEXT("bait_grub") }, { EFVLegend::CrystalTrout, TEXT("lure_1_1_0") },
			{ EFVLegend::Angler, TEXT("bait_magic") }, { EFVLegend::FogSerpent, TEXT("bait_nightcrawler") }, { EFVLegend::ZharRyba, TEXT("bait_ember") },
		};
		int32 FavSeed = 7300;
		for (const FFav& Fv : Favs)
		{
			const FFVFishDef* F = FVDB::FindLegend(Fv.Legend);
			const FBalanceRow* Row = nullptr;
			for (const FBalanceRow& R : BalanceRows) { if (R.Legend == Fv.Legend) { Row = &R; } }
			if (!F || !Row) { LegendCheck(false, FString::Printf(TEXT("favourite bait share: legend %d has no def / tackle row"), int32(Fv.Legend))); continue; }
			EFVDayPhase DayPh = EFVDayPhase::Day;
			for (int32 p = 0; p < int32(EFVDayPhase::Count); ++p)
			{
				if ((F->TimeMask & FVTime::Bit(EFVDayPhase(p))) != 0) { DayPh = EFVDayPhase(p); break; }
			}
			const EFVWeather W = F->Weather == EFVWeatherReq::Rain ? EFVWeather::Rain : EFVWeather::Sunny;
			const FFVCatchContext C = FishingCtx(F->Location, DayPh, W, 1.f, FV::ClassicMaxLevel, FName(TEXT("rod_poseidon")),
				FName(Fv.Bait), FName(Row->BestLine), FName(Row->BestFloat));
			const FPickStats S = PickMany(C, F->Id, 10000, FavSeed++);
			UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST legends favourite bait share: %s on %s at %s (depth 1, phase %d%s) = %s of %d bites"),
				*F->Id.ToString(), Fv.Bait, *F->Location.ToString(), int32(DayPh), W == EFVWeather::Rain ? TEXT(", rain") : TEXT(""),
				*Pct(float(S.Target) / float(FMath::Max(1, S.Bites))), S.Bites);
		}
	}

	// ---- §10.1 shovels: 0.8 x (8 / 11 / 14 / 18 s)
	{
		const float Want[] = { 6.4f, 8.8f, 11.2f, 14.4f };
		const TArray<FFVShovelDef>& Sh = FVDB::Shovels();
		bool bOk = Sh.Num() == 4;
		FString Got;
		for (int32 i = 0; i < Sh.Num(); ++i)
		{
			Got += FString::Printf(TEXT("%s%.1f"), i ? TEXT(" / ") : TEXT(""), Sh[i].DigSeconds);
			bOk &= i < 4 && FMath::IsNearlyEqual(Sh[i].DigSeconds, Want[i], 1e-4f);
		}
		LegendCheck(bOk, FString::Printf(TEXT("dig times %s s (6.4 / 8.8 / 11.2 / 14.4)"), *Got));
	}

	// ---- §10.2 the fried fish by rarity: recipes and dishes
	{
		struct FRcp { const TCHAR* Id; const TCHAR* Token; const TCHAR* Dish; int32 Station, Cuts; float Speed; EFVItemArt Art; int32 Sell; float Bar, Reel, Drain, Luck, Minutes; };
		const FRcp Rcps[] = {
			{ TEXT("rcp_fried"),        TEXT("any_fish"),        TEXT("food_fried"),        1, 2, 1.0f, EFVItemArt::FriedFish,       35,  0.f,  1.10f, 0.92f, 0.f,   180.f },
			{ TEXT("rcp_fried_rare"),   TEXT("any_fish_rare"),   TEXT("food_fried_rare"),   1, 2, 1.1f, EFVItemArt::FriedFishRare,   80,  4.f,  1.14f, 0.89f, 0.f,   210.f },
			{ TEXT("rcp_fried_epic"),   TEXT("any_fish_epic"),   TEXT("food_fried_epic"),   2, 3, 1.2f, EFVItemArt::FriedFishEpic,   180, 8.f,  1.18f, 0.86f, 0.f,   240.f },
			{ TEXT("rcp_fried_legend"), TEXT("any_fish_legend"), TEXT("food_fried_legend"), 3, 4, 1.4f, EFVItemArt::FriedFishLegend, 400, 12.f, 1.25f, 0.80f, 0.05f, 300.f },
		};
		for (const FRcp& R : Rcps)
		{
			const FFVRecipeDef* Rc = FVDB::FindRecipe(FName(R.Id));
			const FFVItemDef* It = FVDB::FindItem(FName(R.Dish));
			const bool bRecipe = Rc && Rc->Station == EFVStation::Kitchen && Rc->StationLevel == R.Station && Rc->GameDifficulty == R.Cuts
				&& FMath::IsNearlyEqual(Rc->CookSpeed, R.Speed, 1e-4f) && Rc->Inputs.Num() == 1 && Rc->Inputs[0].ItemId == FName(R.Token) && Rc->OutputId == FName(R.Dish);
			LegendCheck(bRecipe, FString::Printf(TEXT("recipe %s: kitchen %d, %d cuts, x%.1f, %s -> %s"), R.Id, R.Station, R.Cuts, R.Speed, R.Token, R.Dish));
			const bool bDish = It && It->Kind == EFVItemKind::Food && It->Art == R.Art && It->SellPrice == R.Sell && FMath::IsNearlyEqual(It->Buff.BarBonus, R.Bar, 1e-3f)
				&& FMath::IsNearlyEqual(It->Buff.ReelMult, R.Reel, 1e-4f) && FMath::IsNearlyEqual(It->Buff.DrainMult, R.Drain, 1e-4f)
				&& FMath::IsNearlyEqual(It->Buff.Luck, R.Luck, 1e-4f) && FMath::IsNearlyEqual(It->BuffMinutes, R.Minutes, 1e-3f);
			LegendCheck(bDish, FString::Printf(TEXT("dish %s: sells %d, bar +%.0f, reel x%.2f, drain x%.2f, %.0f min"), R.Dish, R.Sell, R.Bar, R.Reel, R.Drain, R.Minutes));
		}
		LegendCheck(FVDB::FindRecipe(FName(TEXT("rcp_line_heat"))) && FVDB::FindRecipe(FName(TEXT("rcp_bait_ember"))), TEXT("the heat-proof line and the ember bait can be crafted"));
		const FString Unlocks = FString::Join(FVDB::HomeUnlocksAtLevel(10), TEXT(" | "));
		LegendCheck(Unlocks.Contains(TEXT("Жаропрочная леска")) && Unlocks.Contains(TEXT("Жар-наживка")), FString::Printf(TEXT("level 10 unlocks «%s»"), *Unlocks));
	}

	// ---- §10.2 the ember bait (a bait output from concrete fish) on a scratch profile: the Craft the cooking screen calls
	// at its end (the screen itself is only opened for this recipe: its set-up is checked in AddUpdate5MiniGameSteps)
	if (const FFVRecipeDef* Ember = FVDB::FindRecipe(FName(TEXT("rcp_bait_ember"))))
	{
		UFVSaveGame* S = NewObject<UFVSaveGame>(GetTransientPackage());
		S->ResetToNewGame();
		S->AddXp(FMath::Max(0, FVDB::XpForLevel(FV::ClassicMaxLevel) - S->Xp) + 25);
		S->StationLevels.Add(FVDB::StationId(EFVStation::Kitchen), 3);
		const FName Spark(TEXT("fish_sparkfin")), Perch(TEXT("fish_fire_perch")), Berries(TEXT("res_berries")), Bait(TEXT("bait_ember"));
		const bool bGiven = S->GiveItem(Spark, 2) && S->GiveItem(Perch, 1) && S->GiveItem(Berries, 1);
		const EFVBuyResult R = bGiven ? S->Craft(*Ember, 1) : EFVBuyResult::Invalid;
		const int32 Baits = S->BaitCount(Bait);
		const int32 Left = S->CountIngredient(Spark) + S->CountIngredient(Perch) + S->CountItem(Berries);
		LegendCheck(R == EFVBuyResult::Ok && Baits == 4 && Left == 0,
			FString::Printf(TEXT("the ember bait: 2 sparkfins + a fire perch + berries -> %d x %s (%s, %d ingredients left)"), Baits, *Bait.ToString(), *UFVSaveGame::BuyResultText(R), Left));
	}
	else
	{
		LegendCheck(false, TEXT("the ember bait recipe rcp_bait_ember exists"));
	}

	// ---- §10.2 ingredient tokens on a scratch profile: exactly the rarity, never smoked, the cheapest first
	{
		UFVSaveGame* S = NewObject<UFVSaveGame>(GetTransientPackage());
		S->ResetToNewGame();
		const FFVFishDef* Common = nullptr;
		TArray<const FFVFishDef*> Rares;
		const FFVFishDef* Epic = nullptr;
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (F.Rarity == EFVRarity::Common && (!Common || F.BasePrice < Common->BasePrice)) { Common = &F; }
			if (F.Rarity == EFVRarity::Rare) { Rares.Add(&F); }
			if (F.Rarity == EFVRarity::Epic && !Epic) { Epic = &F; }
		}
		Rares.Sort([](const FFVFishDef& A, const FFVFishDef& B) { return A.BasePrice < B.BasePrice; });
		if (Common && Epic && Rares.Num() >= 2)
		{
			const FFVFishDef* RareA = Rares[0];
			const FFVFishDef* RareB = Rares.Last();
			S->AddToKeepnet(Common->Id, EFVQuality::Normal, 1);
			S->AddToKeepnet(RareB->Id, EFVQuality::Normal, 1);
			S->AddToKeepnet(RareA->Id, EFVQuality::Normal, 1);
			S->AddToKeepnetEx(RareA->Id, EFVQuality::Normal, 1, FVStackFlags::Smoked);
			S->AddToKeepnet(Epic->Id, EFVQuality::Normal, 1);
			const int32 NAny = S->CountIngredient(FVDB::AnyFishToken());
			const int32 NRare = S->CountIngredient(FVDB::AnyFishRareToken());
			const int32 NEpic = S->CountIngredient(FVDB::AnyFishEpicToken());
			const int32 NLeg = S->CountIngredient(FVDB::AnyFishLegendToken());
			LegendCheck(NAny == 4 && NRare == 2 && NEpic == 1 && NLeg == 0, FString::Printf(TEXT("tokens count any %d / rare %d / epic %d / legend %d (4 / 2 / 1 / 0)"), NAny, NRare, NEpic, NLeg));
			FFVIngredient IR; IR.ItemId = FVDB::AnyFishRareToken(); IR.Count = 1;
			FFVIngredient IA4; IA4.ItemId = FVDB::AnyFishToken(); IA4.Count = 4;
			FFVIngredient IA3; IA3.ItemId = FVDB::AnyFishToken(); IA3.Count = 3;
			const bool bNo = !S->HasIngredients({ IR, IA4 });
			const bool bYes = S->HasIngredients({ IR, IA3 });
			LegendCheck(bNo && bYes, TEXT("a rare token + any_fish never count one fish twice (rare+4 no, rare+3 yes)"));
			S->ConsumeIngredients({ IR });
			const TArray<FFVInvStack>& K = S->GetContainer(EFVContainer::Keepnet);
			const bool bAGone = !K.ContainsByPredicate([RareA](const FFVInvStack& St) { return St.ItemId == RareA->Id && (St.Flags & FVStackFlags::Smoked) == 0; });
			const bool bBStays = K.ContainsByPredicate([RareB](const FFVInvStack& St) { return St.ItemId == RareB->Id; });
			const bool bSmokedStays = K.ContainsByPredicate([RareA](const FFVInvStack& St) { return St.ItemId == RareA->Id && (St.Flags & FVStackFlags::Smoked) != 0; });
			LegendCheck(bAGone && bBStays && bSmokedStays, FString::Printf(TEXT("the rare token takes the cheapest non-smoked rare fish (%s)"), *RareA->Id.ToString()));
			S->StationLevels.Add(FVDB::StationId(EFVStation::Kitchen), 3);
			if (const FFVRecipeDef* Leg = FVDB::FindRecipe(FName(TEXT("rcp_fried_legend"))))
			{
				const EFVBuyResult Before = S->CanCraft(*Leg, 1);
				S->GiveItem(FName(TsarId), 1);
				const EFVBuyResult After = S->CanCraft(*Leg, 1);
				LegendCheck(Before == EFVBuyResult::NotEnoughMaterials && After == EFVBuyResult::Ok,
					FString::Printf(TEXT("«Пир на весь мир» needs a legendary fish (%s -> %s)"), *UFVSaveGame::BuyResultText(Before), *UFVSaveGame::BuyResultText(After)));
			}
		}
		else
		{
			LegendCheck(false, TEXT("tokens: fish of every rarity exist"));
		}
	}

	// ---- §11: each legend's catch completes its achievement and grants its bobber skin (a scratch profile per legend)
	{
		struct FAch { const TCHAR* Fish; const TCHAR* Ach; const TCHAR* Skin; EFVBobberStyle Style; };
		const FAch Achs[] = {
			{ TsarId,     TEXT("ach_legend_pond"),     TEXT("bobber_tsar"),     EFVBobberStyle::Tsar },
			{ SturgeonId, TEXT("ach_legend_river"),    TEXT("bobber_sturgeon"), EFVBobberStyle::Sturgeon },
			{ TroutId,    TEXT("ach_legend_mountain"), TEXT("bobber_crystal"),  EFVBobberStyle::Crystal },
			{ AnglerId,   TEXT("ach_legend_ocean"),    TEXT("bobber_lantern"),  EFVBobberStyle::Lantern },
			{ SerpentId,  TEXT("ach_legend_swamp"),    TEXT("bobber_fog"),      EFVBobberStyle::Fog },
			{ ZharId,     TEXT("ach_legend_volcano"),  TEXT("bobber_zhar"),     EFVBobberStyle::Zhar },
		};
		for (const FAch& A : Achs)
		{
			const FFVCosmeticDef* C = FVDB::FindCosmetic(FName(A.Skin));
			const bool bSkinDef = C && C->Slot == EFVCosmeticSlot::Bobber && C->Style == uint8(A.Style) && C->Price == 0
				&& FVDB::CosmeticSourceText(C->Id).Contains(TEXT("достижение"));
			LegendCheck(bSkinDef, FString::Printf(TEXT("%s: a bobber skin of its own style, not sold, «%s»"), A.Skin, C ? *FVDB::CosmeticSourceText(C->Id) : TEXT("-")));
			UFVSaveGame* S = NewObject<UFVSaveGame>(GetTransientPackage());
			S->ResetToNewGame();
			const TArray<FName> Before = S->CheckAchievements();
			FFVFishRecord& Rec = S->Records.FindOrAdd(FName(A.Fish));
			Rec.Caught = 1;
			const TArray<FName> Done = S->CheckAchievements();
			int32 OtherSkins = 0;
			for (const FAch& O : Achs)
			{
				if (O.Skin != A.Skin && S->OwnsCosmetic(FName(O.Skin))) { ++OtherSkins; }
			}
			LegendCheck(!Before.Contains(FName(A.Ach)) && Done.Contains(FName(A.Ach)) && S->OwnsCosmetic(FName(A.Skin)) && OtherSkins == 0,
				FString::Printf(TEXT("catching %s completes %s and grants %s only"), A.Fish, A.Ach, A.Skin));
		}
	}
}

void FVAutoTest::RunLegendTricksChecks()
{
	using namespace FVAutoTestU5;
	// §7.11: the bot plays 90 s of each legend with the progress pinned after every Step (otherwise it would win before
	// the tricks show up): the serpent at 0.5 then 0.65, the Zhar-ryba at 0.4 / 0.6 / 0.9 for 30 s each, the rest at 0.5.
	for (int32 r = 1; r < int32(UE_ARRAY_COUNT(BalanceRows)); ++r)
	{
		const FBalanceRow& Row = BalanceRows[r];
		const FFVFishDef* F = FVDB::FindLegend(Row.Legend);
		if (!F)
		{
			LegendCheck(false, FString::Printf(TEXT("tricks: legend %d has no fish"), int32(Row.Legend)));
			continue;
		}
		const FFVLegendSimSetup S = SimSetup(*F, Row.Level, FName(Row.Rod), FName(Row.Line), FName(Row.Float), nullptr);
		FFVMinigame M;
		M.Start(S.Difficulty, S.Motion, S.BarHeight, S.ReelMult, S.DrainMult, false, 4242 + r, Row.Legend);
		bool bH2 = false, bH3 = false, bFake = false, bP1 = false, bP2 = false, bBlink = false, bBurn = false, bCoils = false;
		bool bP2BeforeP1 = false;
		float MinAlpha = 1.f, MinBar = M.BarHeight, FirstTrick = -1.f;
		// the Master: the whirl always pulls, a surge in phase 0, jerks in 1 and 3, the silt hides partly (never fully) in 2
		bool bMmSurge = false, bMmJerk1 = false, bMmJerk3 = false, bMmOrder = true, bMmPull = true;
		float MmSiltMinA = 1.f, MmSiltMaxA = 0.f;
		uint8 MmLastPhase = 0;
		for (int32 t = 0; t < 90 * 60; ++t)
		{
			float Pin = 0.5f;
			if (Row.Legend == EFVLegend::FogSerpent) { Pin = t < 45 * 60 ? 0.5f : 0.65f; }
			if (Row.Legend == EFVLegend::ZharRyba) { Pin = t < 30 * 60 ? 0.4f : (t < 60 * 60 ? 0.6f : 0.9f); }
			// the Master: «Омут» 0.3, «Жернова» 0.5, «Муть» 0.7, «Последний рывок» 0.9 (22.5 s each)
			if (Row.Legend == EFVLegend::MillMaster) { Pin = t < 1350 ? 0.3f : (t < 2700 ? 0.5f : (t < 4050 ? 0.7f : 0.9f)); }
			const int32 Tricks0 = M.TrickCount;
			M.Step(M.BotShouldHold(0.9f));
			M.Progress = Pin;
			if (M.TrickCount > Tricks0 && FirstTrick < 0.f) { FirstTrick = M.LegendT; }
			bH2 |= M.HazardState == 2;
			bH3 |= M.HazardState == 3;
			bFake |= M.bFakeLantern;
			bBlink |= M.BlinkState == 1;
			bBurn |= M.BurnFlash > 0.f;
			bCoils |= M.Banner == FFVMinigame::ELegendBanner::Coils;
			bP2BeforeP1 |= M.Phase == 2 && !bP1;
			bP1 |= M.Phase >= 1;
			bP2 |= M.Phase >= 2;
			MinAlpha = FMath::Min(MinAlpha, M.FishAlpha);
			MinBar = FMath::Min(MinBar, M.BarHeight);
			if (Row.Legend == EFVLegend::MillMaster)
			{
				bMmOrder &= M.Phase == MmLastPhase || M.Phase == MmLastPhase + 1;
				MmLastPhase = M.Phase;
				bMmPull &= M.Current > 0.f;
				bMmSurge |= M.Phase == 0 && M.HazardState == 2;
				bMmJerk1 |= M.Phase == 1 && M.HazardState == 2;
				bMmJerk3 |= M.Phase == 3 && M.HazardState == 2;
				if (M.Phase == 2 && M.HazardState == 2 && M.HazardAge > 0.3f)
				{
					MmSiltMinA = FMath::Min(MmSiltMinA, M.FishAlpha);
					MmSiltMaxA = FMath::Max(MmSiltMaxA, M.FishAlpha);
				}
			}
		}
		const FString Slug = F->Id.ToString();
		LegendCheck(bH2, FString::Printf(TEXT("tricks %s: the hazard shows (HazardState 2), %d tricks, the first at %.2f s"), *Slug, M.TrickCount, FirstTrick));
		switch (Row.Legend)
		{
		case EFVLegend::CrystalTrout:
			LegendCheck(MinAlpha < 0.3f, FString::Printf(TEXT("tricks %s: turns transparent (alpha %.2f)"), *Slug, MinAlpha));
			break;
		case EFVLegend::Angler:
			LegendCheck(bH3 && bFake, FString::Printf(TEXT("tricks %s: chews (HazardState 3: %s) and fakes a lantern (%s)"), *Slug, bH3 ? TEXT("yes") : TEXT("no"), bFake ? TEXT("yes") : TEXT("no")));
			break;
		case EFVLegend::FogSerpent:
			LegendCheck(MinAlpha < 0.3f && bCoils && M.Phase == 1, FString::Printf(TEXT("tricks %s: hides in the fog (alpha %.2f), coils at 60 %% (%s)"), *Slug, MinAlpha, bCoils ? TEXT("yes") : TEXT("no")));
			break;
		case EFVLegend::ZharRyba:
			LegendCheck(bP1 && bP2 && !bP2BeforeP1 && bBlink && bBurn && MinBar <= M.BaseBarHeight * 0.76f,
				FString::Printf(TEXT("tricks %s: phase 1 -> 2 (%s), blink (%s), burn (%s), bar %.0f -> %.0f"), *Slug, bP1 && bP2 && !bP2BeforeP1 ? TEXT("yes") : TEXT("no"),
					bBlink ? TEXT("yes") : TEXT("no"), bBurn ? TEXT("yes") : TEXT("no"), M.BaseBarHeight, MinBar));
			break;
		case EFVLegend::MillMaster:
			LegendCheck(bMmOrder && M.Phase == 3 && bMmPull && bMmSurge && bMmJerk1 && bMmJerk3,
				FString::Printf(TEXT("tricks %s: phases 0 -> 3 in order (%s, now %d), the whirl always pulls (%s), surge (%s), jerks in «Жернова» (%s) and the last dash (%s)"),
					*Slug, bMmOrder ? TEXT("yes") : TEXT("no"), int32(M.Phase), bMmPull ? TEXT("yes") : TEXT("no"), bMmSurge ? TEXT("yes") : TEXT("no"),
					bMmJerk1 ? TEXT("yes") : TEXT("no"), bMmJerk3 ? TEXT("yes") : TEXT("no")));
			LegendCheck(MmSiltMaxA > MmSiltMinA && MmSiltMinA > 0.05f && MmSiltMinA < 0.3f && MmSiltMaxA >= 0.4f,
				FString::Printf(TEXT("tricks %s: the silt hides the catfish only partly (alpha %.2f..%.2f, never 0)"), *Slug, MmSiltMinA, MmSiltMaxA));
			break;
		default:
			break;
		}
	}
}

// ============================================================================================
// BALANCE_LEGEND (one simulation per frame) and the BALANCE regression reference
// ============================================================================================
bool FVAutoTest::RunLegendBalanceStep()
{
	using namespace FVAutoTestU5;
	constexpr int32 PerRow = 3;   // human-mid «по уровню», human-strong «по уровню» (+ ukha), human-strong «с запасом»
	const int32 NumRows = int32(UE_ARRAY_COUNT(BalanceRows));
	if (LegendBalanceIndex >= NumRows * PerRow)
	{
		return true;
	}
	const int32 RowIdx = LegendBalanceIndex / PerRow;
	const int32 Sub = LegendBalanceIndex % PerRow;
	const FBalanceRow& Row = BalanceRows[RowIdx];
	const FFVFishDef* F = FVDB::FindLegend(Row.Legend);
	if (!F)
	{
		LegendCheck(false, FString::Printf(TEXT("BALANCE_LEGEND: legend %d has no fish"), int32(Row.Legend)));
		LegendBalanceIndex = (RowIdx + 1) * PerRow;
		return LegendBalanceIndex >= NumRows * PerRow;
	}
	if (Sub == 0)
	{
		LegendBalanceRow.Reset();
	}
	const FFVStatMods Ukha = NormalUkha();
	FFVLegendSimSetup S;
	FFVBotProfile Profile = FFVBotProfile::HumanStrong();
	if (Sub == 0)
	{
		S = SimSetup(*F, Row.Level, FName(Row.Rod), FName(Row.Line), FName(Row.Float), nullptr);
		Profile = FFVBotProfile::HumanMid();
	}
	else if (Sub == 1)
	{
		S = SimSetup(*F, Row.Level, FName(Row.Rod), FName(Row.Line), FName(Row.Float), &Ukha);
	}
	else
	{
		S = SimSetup(*F, FV::ClassicMaxLevel   /* update 6: «с запасом» = level 10 */, FName(TEXT("rod_poseidon")), FName(Row.BestLine), FName(Row.BestFloat), &Ukha);
	}
	LegendBalanceRow.Add(FVSimulateLegend(S, Profile, BalanceTrials, BalanceSeed));
	UE_LOG(LogFV, Log, TEXT("FV: BALANCE_LEGEND setup %s %s: bar %.2f reel %.6f drain %.6f diff %.0f motion %d"), *F->Id.ToString(),
		Sub == 0 ? TEXT("mid") : (Sub == 1 ? TEXT("strong") : TEXT("reserve")), S.BarHeight, S.ReelMult, S.DrainMult, S.Difficulty, int32(S.Motion));
	++LegendBalanceIndex;
	if (Sub < PerRow - 1)
	{
		return false;
	}

	const FFVLegendSimResult& Mid = LegendBalanceRow[0];
	const FFVLegendSimResult& Str = LegendBalanceRow[1];
	const FFVLegendSimResult& Res = LegendBalanceRow[2];
	const bool bTsar = Row.Legend == EFVLegend::TsarCarp;   // the classic fight: no tricks
	// the table is recorded (the owner accepted the legends' balance by playing, 26.09.2026): no target ranges any more
	UE_LOG(LogFV, Log, TEXT("FV: BALANCE_LEGEND %s mid=%s strong=%s median=%.1fs/%.1fs tricks=%.0f/%.0f reserve=%s%s (recorded)"),
		*F->Id.ToString(), *Pct(Mid.Rate), *Pct(Str.Rate), Mid.MedianSec, Str.MedianSec, Mid.MedianTricks, Str.MedianTricks,
		*Pct(Res.Rate), Row.Legend == EFVLegend::ZharRyba ? *FString::Printf(TEXT(" phase0-eruption=%s"), *Pct(Str.Phase0EruptFrac)) : TEXT(""));
	if (Row.Legend == EFVLegend::ZharRyba)
	{
		ZharBalanceMid = Mid;
		ZharBalanceStr = Str;
		bZharBalanceDone = true;
	}
	else if (Row.Legend == EFVLegend::MillMaster)
	{
		// Р53: 10 % harder than the Zhar-ryba — human-strong ~0.9 x its share (+-3 p.p.), human-mid not above it,
		// the median fight not shorter (both rows are in the log side by side)
		if (bZharBalanceDone)
		{
			const float Want = 0.9f * ZharBalanceStr.Rate;
			UE_LOG(LogFV, Log, TEXT("FV: BALANCE_LEGEND Р53 %s vs fish_zhar_ryba: strong %s (target %s +-3 p.p.), mid %s (Zhar %s), median %.1fs/%.1fs (Zhar %.1fs/%.1fs)"),
				*F->Id.ToString(), *Pct(Str.Rate), *Pct(Want), *Pct(Mid.Rate), *Pct(ZharBalanceMid.Rate), Mid.MedianSec, Str.MedianSec, ZharBalanceMid.MedianSec, ZharBalanceStr.MedianSec);
			LegendCheck(FMath::Abs(Str.Rate - Want) <= 0.03f + 1e-4f,
				FString::Printf(TEXT("BALANCE_LEGEND Р53: the Master's human-strong %s is 0.9 x the Zhar-ryba's %s (+-3 p.p.)"), *Pct(Str.Rate), *Pct(ZharBalanceStr.Rate)));
			LegendCheck(Mid.Rate <= ZharBalanceMid.Rate + 1e-4f,
				FString::Printf(TEXT("BALANCE_LEGEND Р53: the Master's human-mid %s is not above the Zhar-ryba's %s"), *Pct(Mid.Rate), *Pct(ZharBalanceMid.Rate)));
			LegendCheck(Mid.MedianSec >= ZharBalanceMid.MedianSec && Str.MedianSec >= ZharBalanceStr.MedianSec,
				FString::Printf(TEXT("BALANCE_LEGEND Р53: the Master's fights are not shorter (median %.1fs/%.1fs vs %.1fs/%.1fs)"),
					Mid.MedianSec, Str.MedianSec, ZharBalanceMid.MedianSec, ZharBalanceStr.MedianSec));
		}
		else
		{
			LegendCheck(false, TEXT("BALANCE_LEGEND Р53: the Zhar-ryba row runs before the Master's"));
		}
	}
	if (Res.Rate + ReserveSlack < Str.Rate)
	{
		UE_LOG(LogFV, Log, TEXT("FV: BALANCE_LEGEND note %s: the best tackle («с запасом» %s) is below the level tackle (%s)"), *F->Id.ToString(), *Pct(Res.Rate), *Pct(Str.Rate));
	}
	// sanity: a strong player can land every legend, and every legend with tricks shows them in a fight
	LegendCheck(Str.Rate > 0.f, FString::Printf(TEXT("BALANCE_LEGEND %s can be landed: human-strong %s (> 0 %%)"), *F->Id.ToString(), *Pct(Str.Rate)));
	if (!bTsar)
	{
		LegendCheck(Mid.MedianTricks >= 2.f && Str.MedianTricks >= 2.f,
			FString::Printf(TEXT("BALANCE_LEGEND %s shows its tricks: median %.0f (mid) / %.0f (strong) per fight (>= 2)"), *F->Id.ToString(), Mid.MedianTricks, Str.MedianTricks));
	}
	const bool bDone = LegendBalanceIndex >= NumRows * PerRow;
	if (bDone)
	{
		UE_LOG(LogFV, Log, TEXT("FV: BALANCE_LEGEND done: %d legends recorded"), NumRows);
	}
	return bDone;
}

void FVAutoTest::LoadBalanceReference()
{
	bBalanceReferenceLoaded = true;
	BalanceReference.Reset();
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Logs") / TEXT("FV_balance_reference.log");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING BALANCE regression: no reference log %s"), *FPaths::ConvertRelativePathToFull(Path));
		++Warnings;
		return;
	}
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	const FString Tag = TEXT("FV: BALANCE fish_");
	for (const FString& L : Lines)
	{
		const int32 At = L.Find(Tag, ESearchCase::CaseSensitive);
		if (At == INDEX_NONE)
		{
			continue;
		}
		const FString Rest = L.Mid(At).TrimEnd();
		const int32 IdStart = FCString::Strlen(TEXT("FV: BALANCE "));
		int32 Space = INDEX_NONE;
		if (Rest.Mid(IdStart).FindChar(TEXT(' '), Space))
		{
			BalanceReference.Add(FName(*Rest.Mid(IdStart, Space)), Rest);
		}
	}
	UE_LOG(LogFV, Log, TEXT("FV: BALANCE regression reference: %d lines from %s"), BalanceReference.Num(), *FPaths::GetCleanFilename(Path));
}

// ============================================================================================
// Sprite sheet
// ============================================================================================
bool FVAutoTest::WriteUpdate5Sheet(const FString& Path) const
{
	const int32 W = 190, H = 120, Up = 4;
	FFVImage Img(W, H, FColor(30, 38, 56));
	// the six legends (sprite + silhouette), two columns of three
	int32 Row = 0;
	for (int32 L = int32(EFVLegend::TsarCarp); L < int32(EFVLegend::Count); ++L, ++Row)
	{
		const FFVFishDef* F = FVDB::FindLegend(EFVLegend(L));
		if (!F)
		{
			continue;
		}
		const int32 X = (Row % 2) * 95 + 2, Y = (Row / 2) * 22 + 2;
		Img.Blit(UFVSprites::BuildFish(*F, false), X, Y);
		Img.Blit(UFVSprites::BuildFish(*F, true), X + 46, Y);
	}
	// the six legend bobbers, both frames, on day and night water
	const int32 WaterY = 72;
	for (int32 Y = WaterY; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const bool bNight = Y >= WaterY + 24;
			Img.Set(X, Y, bNight ? FColor(22, 40, 78) : FColor(80, 168, 214));
		}
	}
	int32 k = 0;
	for (int32 S = int32(EFVBobberStyle::Tsar); S < int32(EFVBobberStyle::Count); ++S, ++k)
	{
		for (int32 Fr = 0; Fr < 2; ++Fr)
		{
			const FFVImage B = UFVSprites::BuildBobber(EFVBobberStyle(S), Fr);
			const int32 X = 6 + k * 30 + Fr * 12;
			Img.Blit(B, X, WaterY + 6);
			Img.Blit(B, X, WaterY + 30);
		}
	}
	TArray<FColor> Big;
	Big.SetNumUninitialized(W * Up * H * Up);
	for (int32 Y = 0; Y < H * Up; ++Y)
	{
		for (int32 X = 0; X < W * Up; ++X)
		{
			FColor C = Img.Get(X / Up, Y / Up);
			C.A = 255;
			Big[Y * W * Up + X] = C;
		}
	}
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(W * Up, H * Up, TArrayView64<const FColor>(Big.GetData(), Big.Num()), Png);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *Path);
}
