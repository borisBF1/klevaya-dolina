// Scripted self-test (-fvautotest). See FVAutoTest.h.

#include "FVAutoTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "FVUIKit.h"
#include "FVTouch.h"
#include "FVDatabase.h"
#include "FVGame.h"
#include "FVOnboarding.h"
#include "FVOnline.h"
#include "FVSaveGame.h"
#include "FVSprites.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"

namespace FVAutoTestLocal
{
	constexpr float GlobalTimeout = 450.f;     // seconds; the whole run must never take longer (0.5: ~110-130 s; update 6 adds ~100 s)
	/** Guide entry opened by the "tutorial entry" screenshot (the screen shows its index for an unknown id). */
	const TCHAR* const TutorialEntryId = TEXT("g_fishing");
	int32 SwipeStartPageChanges = 0;
	constexpr float FishingTimeout = 60.f;     // seconds for the "fish one fish" part
	constexpr float ForceBiteAfter = 3.f;      // seconds of waiting before the bite is forced (a natural bite still has a chance)
	constexpr float CatchBoost = 0.9f;         // SetFightProgress for the catches whose bookkeeping is checked (not their balance)
	constexpr float CatchBoostBelow = 0.6f;    // ... given again whenever the meter fell below this
	constexpr int32 BalanceTrials = 200;
	constexpr float BalanceBotSkill = 0.85f;

	const TCHAR* ScreenSlug(EFVScreen S)
	{
		switch (S)
		{
		case EFVScreen::MainMenu: return TEXT("main_menu");
		case EFVScreen::Map: return TEXT("map");
		case EFVScreen::Shop: return TEXT("shop");
		case EFVScreen::Inventory: return TEXT("inventory");
		case EFVScreen::Wardrobe: return TEXT("wardrobe");
		case EFVScreen::Collection: return TEXT("collection");
		case EFVScreen::Achievements: return TEXT("achievements");
		case EFVScreen::Pause: return TEXT("pause");
		case EFVScreen::Chest: return TEXT("chest");
		case EFVScreen::IceCellar: return TEXT("ice_cellar");
		case EFVScreen::Station: return TEXT("station");
		case EFVScreen::Orders: return TEXT("orders");
		case EFVScreen::NetSpot: return TEXT("net_spot");
		case EFVScreen::HomeDecor: return TEXT("home_decor");
		case EFVScreen::Cooking: return TEXT("cooking");
		case EFVScreen::LureCraft: return TEXT("lure_craft");
		case EFVScreen::Knots: return TEXT("knots");
		case EFVScreen::Digging: return TEXT("digging");
		case EFVScreen::Tutorial: return TEXT("tutorial");
		case EFVScreen::Profile: return TEXT("profile");
		case EFVScreen::Leaderboard: return TEXT("leaderboard");
		case EFVScreen::Mail: return TEXT("mail");
		case EFVScreen::Levels: return TEXT("levels");
		case EFVScreen::RestoreSave: return TEXT("restore_save");
		case EFVScreen::Chopping: return TEXT("chopping");
		case EFVScreen::Sawing: return TEXT("sawing");
		case EFVScreen::Forging: return TEXT("forging");
		case EFVScreen::Excavation: return TEXT("excavation");
		case EFVScreen::Builder: return TEXT("builder");
		case EFVScreen::YardBuilding: return TEXT("yard_building");
		case EFVScreen::GardenBed: return TEXT("garden_bed");
		case EFVScreen::Boat: return TEXT("boat");
		case EFVScreen::Voyage: return TEXT("voyage");
		case EFVScreen::CityBoard: return TEXT("city_board");
		case EFVScreen::Artel: return TEXT("artel");
		case EFVScreen::ExpertTackle: return TEXT("expert_tackle");
		default: return TEXT("world");
		}
	}

	const TCHAR* StateName(EFVFishState S)
	{
		switch (S)
		{
		case EFVFishState::Idle: return TEXT("Idle");
		case EFVFishState::Charging: return TEXT("Charging");
		case EFVFishState::Casting: return TEXT("Casting");
		case EFVFishState::Waiting: return TEXT("Waiting");
		case EFVFishState::Bite: return TEXT("Bite");
		case EFVFishState::Minigame: return TEXT("Minigame");
		case EFVFishState::Showcase: return TEXT("Showcase");
		case EFVFishState::Escaped: return TEXT("Escaped");
		case EFVFishState::Retrieving: return TEXT("Retrieving");
		default: return TEXT("?");
		}
	}
}

FVAutoTest::FVAutoTest(UFVGame* InGame)
	: Game(InGame)
{
	BuildScript();
	UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST start: %d steps, screenshots -> %s"), Steps.Num(),
		*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoTest")));
}

FVAutoTest::~FVAutoTest()
{
}

namespace FVAutoTestIcon
{
	FColor Lerp(const FColor& A, const FColor& B, float T)
	{
		return FColor(uint8(FMath::Lerp(float(A.R), float(B.R), T)), uint8(FMath::Lerp(float(A.G), float(B.G), T)),
			uint8(FMath::Lerp(float(A.B), float(B.B), T)), 255);
	}

	/** App icon: the legendary Tsar-Carp leaping out of the pond at noon (48x48 art, written 4x = 192 px). */
	bool Write(const FString& Path)
	{
		const int32 N = 48, Up = 4;
		FFVImage Img(N, N);
		const int32 WaterY = 27;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				FColor C;
				if (Y < WaterY)
				{
					C = Lerp(FColor(96, 180, 250), FColor(196, 234, 255), float(Y) / float(WaterY - 1));
				}
				else
				{
					C = Lerp(FColor(70, 176, 214), FColor(22, 70, 138), float(Y - WaterY) / float(N - 1 - WaterY));
					if (Y % 4 == 1 && ((X * 7 + Y * 3) % 13) < 3) { C = Lerp(C, FColor(220, 245, 255), 0.55f); }
				}
				Img.Set(X, Y, C);
			}
		}
		for (int32 X = 0; X < N; ++X) { Img.Set(X, WaterY, FColor(230, 248, 255)); }
		Img.FillCircle(39, 8, 5, FColor(255, 222, 96));
		Img.FillCircle(39, 8, 3, FColor(255, 244, 170));
		// far shore
		for (int32 X = 0; X < N; ++X)
		{
			const int32 H = 2 + int32((FMath::Sin(X * 0.45f) + 1.f) * 1.5f);
			for (int32 k = 0; k < H; ++k) { Img.Set(X, WaterY - 1 - k, FColor(84, 160, 70)); }
		}

		const FFVFishDef* Fish = FVDB::FindFish(TEXT("fish_tsar_carp"));
		if (!Fish && FVDB::Fish().Num() > 0) { Fish = &FVDB::Fish()[0]; }
		if (Fish)
		{
			const FFVImage F = UFVSprites::BuildFish(*Fish, false);
			if (F.IsValid())
			{
				Img.Blit(F, (N - F.W) / 2, WaterY - F.H + 5);
			}
		}
		// splash droplets
		const FColor Drop(240, 252, 255);
		const int32 Drops[][2] = { { 8, 24 }, { 10, 21 }, { 13, 25 }, { 36, 23 }, { 39, 20 }, { 41, 25 }, { 6, 26 }, { 43, 26 } };
		for (const auto& D : Drops) { Img.Set(D[0], D[1], Drop); }
		const FFVImage Bob = UFVSprites::BuildBobber(EFVBobberStyle::Classic, 0);
		if (Bob.IsValid()) { Img.Blit(Bob, 5, 34); }

		TArray<FColor> Big;
		Big.SetNumUninitialized(N * Up * N * Up);
		for (int32 Y = 0; Y < N * Up; ++Y)
		{
			for (int32 X = 0; X < N * Up; ++X)
			{
				FColor C = Img.Get(X / Up, Y / Up);
				C.A = 255;
				Big[Y * N * Up + X] = C;
			}
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(N * Up, N * Up, TArrayView64<const FColor>(Big.GetData(), Big.Num()), Png);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *Path);
	}
}

// ============================================================================================
// Script
// ============================================================================================
void FVAutoTest::AddStep(const FString& Name, TFunction<void()> Enter, TFunction<bool(float)> Until, const FString& Shot, float Settle, float Timeout)
{
	FStep S;
	S.Name = Name;
	S.Enter = MoveTemp(Enter);
	S.Until = MoveTemp(Until);
	S.Shot = Shot;
	S.Settle = Settle;
	S.Timeout = Timeout;
	Steps.Add(MoveTemp(S));
}

void FVAutoTest::AddScreenShotStep(EFVScreen Screen, const FString& Shot)
{
	AddStep(FString::Printf(TEXT("open screen %s"), FVAutoTestLocal::ScreenSlug(Screen)),
		[this, Screen]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None && Game->GetScreen() != Screen)
			{
				Game->CloseScreen();
			}
			Game->OpenScreen(Screen);
		},
		[this, Screen](float) { return Game->GetScreen() == Screen; },
		Shot, 0.3f, 3.f);
}

void FVAutoTest::AddFreshScreensStep()
{
	AddStep(TEXT("fresh profile: the overlay screens open (no shots)"),
		[this]()
		{
			StepValue = 0;
			SubFrames = 0;
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
		},
		[this](float)
		{
			// (the map, the shop and the achievements are shot with the fresh profile just before: their locked states)
			static const EFVScreen Screens[] = { EFVScreen::Inventory, EFVScreen::Wardrobe, EFVScreen::Collection,
				EFVScreen::Tutorial, EFVScreen::Levels, EFVScreen::Profile };
			if (StepValue >= int32(UE_ARRAY_COUNT(Screens)))
			{
				if (Game->GetScreen() != EFVScreen::None)
				{
					Game->CloseScreen();
				}
				return Game->GetScreen() == EFVScreen::None;
			}
			const EFVScreen S = Screens[StepValue];
			if (SubFrames == 0)
			{
				DismissModals();
				if (Game->GetScreen() != EFVScreen::None && Game->GetScreen() != S)
				{
					Game->CloseScreen();
				}
				Game->OpenScreen(S);
			}
			// opened this frame, then ticked and drawn twice with the fresh (nearly empty) profile
			if (++SubFrames >= 3)
			{
				if (Game->GetScreen() != S)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING fresh profile: screen %s did not open (screen %d)"), FVAutoTestLocal::ScreenSlug(S), int32(Game->GetScreen()));
					++Warnings;
				}
				++StepValue;
				SubFrames = 0;
			}
			return false;
		},
		FString(), 0.1f, 4.f);
}

void FVAutoTest::AddLocationStep(FName LocationId, float ClockMinutes, EFVWeather Weather, const FString& Shot)
{
	AddStep(FString::Printf(TEXT("location %s at %02d:%02d%s"), *LocationId.ToString(), int32(ClockMinutes) / 60, int32(ClockMinutes) % 60,
			Weather == EFVWeather::Rain ? TEXT(" (rain)") : TEXT("")),
		[this, LocationId, ClockMinutes, Weather]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			Game->SetWeather(Weather);
			if (Game->GetScene().GetLocationId() != LocationId && !Game->TravelTo(LocationId))
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING travel to %s failed"), *LocationId.ToString());
				++Warnings;
			}
			Game->SetClockMinutes(ClockMinutes);
		},
		[](float T) { return T > 0.2f; },
		Shot, 0.5f, 5.f);
}

void FVAutoTest::BuildScript()
{
	// 1. Title screen
	AddStep(TEXT("main menu"), nullptr,
		[this](float T) { return Game->GetScreen() == EFVScreen::MainMenu && T > 1.0f; },
		TEXT("main_menu"), 0.3f, 10.f);

	// 1b. Update 4b: the start-up gate of the main menu (only with -fvserver)
	AddGateSteps();

	// save check mode: only the real-save copies (Saved/SaveCheck/*.sav), nothing else
	if (FParse::Param(FCommandLine::Get(), TEXT("fvsavecheck")))
	{
		AddSaveCheckSteps();
		return;
	}

	// 2. New game. Update 4: the autotest fisherman has a name, so the first-run name choice never blocks the script
	// (and -fvserver registers him right away). Update 4b: a new game starts in the yard next to the practice pond
	// with the onboarding at step 0 (no "how to play" dialog any more).
	AddStep(TEXT("new game"),
		[this]()
		{
			Game->NewGame();
			if (UFVSaveGame* P = Game->GetProfile())
			{
				if (P->PlayerName.IsEmpty())
				{
					P->PlayerName = TEXT("Автотест");
				}
				if (P->PlayerStatus.IsEmpty())
				{
					P->PlayerStatus = TEXT("Проверяю всё подряд");
				}
			}
		},
		[this](float) { return Game->IsInGame(); },
		FString(), 0.5f, 5.f);

	// integration: -fvbalanceonly runs only the balance tables (BALANCE / BALANCE_LEGEND / BALANCE_EXPERT) on the rich
	// profile — a quick measuring run for tuning, never used by PortraitTest.ps1
	const bool bBalanceOnly = FParse::Param(FCommandLine::Get(), TEXT("fvbalanceonly"));
	if (bBalanceOnly)
	{
		AddStep(TEXT("balance only: grant everything"), [this]() { GrantEverything(); }, nullptr, FString(), 0.3f);
	}
	else
	{
	// 2b. Update 4b: the first-fishing onboarding on the fresh profile (yard -> practice pond -> the village pond)
	AddOnboardingSteps();

	// 3. The pond in the morning. The rest of the run is a fisherman who already knows how to fish: no coach marks.
	AddStep(TEXT("pond world"),
		[this]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			Game->GetFishing().SetAutoPlay(false);
			Game->GetFishing().Reset();
			if (UFVSaveGame* P = Game->GetProfile())
			{
				P->OnboardingStep = 100;
			}
			const FName Pond = FVDB::StartLocation();
			if (Game->GetScene().GetLocationId() != Pond && !Game->TravelTo(Pond))
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING travel to %s failed"), *Pond.ToString());
				++Warnings;
			}
			ClearToasts();
			Game->SetWeather(EFVWeather::Sunny);
			Game->SetClockMinutes(9.f * 60.f);
		},
		[](float T) { return T > 0.2f; }, FString(), 0.2f, 3.f);   // (the pond is shot with the rich profile: location_pond)

	// 3b. Update 4: the online services (only with -fvserver: registration, sync, a board, the own server profile)
	AddOnlineSteps();

	// 4. The screens whose locked states only a new game shows are shot fresh: the map (locked locations and their
	// levels), the shop (level-locked / unaffordable goods), the achievements (nothing done yet). Every other overlay
	// screen only opens with the fresh profile (it is shot once, after the catch or rich; the pause menu during the
	// onboarding).
	AddScreenShotStep(EFVScreen::Map, TEXT("screen_map"));
	AddScreenShotStep(EFVScreen::Shop, TEXT("screen_shop"));
	AddScreenShotStep(EFVScreen::Achievements, TEXT("screen_achievements"));
	AddFreshScreensStep();

	// 4a. Update 5 (fresh profile): a legend catch -> its achievement + bobber skin, the wardrobe (one skin open, five locked)
	AddLegendFreshSteps();

	// 4b. Touch controls (phones, or -fvtouch on desktop): fingers are injected as FFVInput::Touches.
	if (FV::IsTouchUI())
	{
		auto Finger = [this](const FVector2D& Pos)
		{
			FFVTouchPoint P;
			P.Id = 3;
			P.Pos = Pos;
			Synth.Touches.Add(P);
		};
		// (the taps are checks only: the map, levels and profile screens are shot with the rich profile)
		AddStep(TEXT("touch: tap the map icon"), nullptr,
			[this, Finger](float T)
			{
				const FFVRect R = Game->GetHudButtonRect(EFVScreen::Map);
				if (T < 0.06f && R.W > 0.f) { Finger(R.Center()); }
				return Game->GetScreen() == EFVScreen::Map && T > 0.2f;
			},
			FString(), 0.1f, 3.f);
		AddStep(TEXT("touch: back button closes the map"), nullptr,
			[this](float T)
			{
				if (T < 0.05f) { Synth.Pressed.Add(EKeys::Escape); }
				return Game->GetScreen() == EFVScreen::None && T > 0.2f;
			},
			FString(), 0.2f, 3.f);
		AddTouchUpdate4Steps();
		AddStep(TEXT("touch: swipe to walk (floating joystick)"),
			[this]() { TouchStartX = float(Game->GetScene().GetPlayerPos().X); },
			[this, Finger](float T)
			{
				// Touch the land above the player, then slide left: the joystick appears where the finger landed.
				const FVector2D Start(FV::VW * 0.5f, FV::TopBarH + 70.f);
				if (T < 0.05f) { Finger(Start); }
				else if (T < 0.9f) { Finger(Start + FVector2D(-34.f, 0.f)); }
				if (T > 1.1f)
				{
					const float DX = float(Game->GetScene().GetPlayerPos().X) - TouchStartX;
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST touch swipe moved the player by %.0f px %s"), DX, DX < -20.f ? TEXT("(ok)") : TEXT("(FAILED)"));
					if (DX >= -20.f) { ++Warnings; }
					return true;
				}
				return false;
			},
			TEXT("touch_joystick"), 0.05f, 4.f);
	}

	// 5. Walk to the water
	AddStep(TEXT("walk to the water"), nullptr,
		[this](float T)
		{
			if (Game->GetScene().CanFishFromHere() && T > 0.1f)
			{
				return true;
			}
			Synth.MoveY = 1.f;
			return false;
		},
		FString(), 0.3f, 8.f);

	if (FV::IsTouchUI())
	{
		AddStep(TEXT("touch: hold still to wind up, release to cast"),
			[this]()
			{
				// The finger must rest on open ground / water: on a world object it is a tap on it (FVGame's cast blocker
				// hit-tests ScreenToWorld(finger)). Hotfix 0.61: the village pond scrolls, so the usual screen point may now
				// land on another world spot (a net post) — it slides sideways to the nearest free point, and the log says so.
				const FVScene& Scene = Game->GetScene();
				const FFVRect Col = Game->IsHudColumnHidden() ? FFVRect() : Game->GetHudColumnRect();
				auto Blocked = [&Scene, &Col](const FVector2D& P) { return Scene.ObjectAt(Scene.ScreenToWorld(P)) != nullptr || (Col.W > 0.f && Col.Contains(P)); };
				const FVector2D Def(FV::VW * 0.3f, FV::VH - FV::BottomBarH - 60.f);
				TouchTapPos = Def;   // (the tap-anywhere step picks its own point later)
				bool bFree = !Blocked(Def);
				for (float Dx = 12.f; !bFree && Dx <= FV::VW * 0.5f; Dx += 12.f)
				{
					for (const float Sgn : { 1.f, -1.f })
					{
						const FVector2D P(Def.X + Sgn * Dx, Def.Y);
						if (!bFree && P.X >= 20.f && P.X <= FV::VW - 20.f && !Blocked(P)) { TouchTapPos = P; bFree = true; }
					}
				}
				const FFVWorldObject* Under = Scene.ObjectAt(Scene.ScreenToWorld(Def));
				const FVector2D W = Scene.ScreenToWorld(TouchTapPos);
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST touch cast: the finger at (%.0f, %.0f) = world (%.0f, %.0f)%s"), TouchTapPos.X, TouchTapPos.Y, W.X, W.Y,
					*(Under ? FString::Printf(TEXT(", moved off «%s» under the usual point"), *Under->Label) : FString(TEXT(", the usual point is free (no cast blocker)"))));
				if (!bFree)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING touch cast: no free point for the finger on the row y=%.0f"), Def.Y);
					++Warnings;
				}
			},
			[this](float T)
			{
				// Hold a finger still on the water for a moment (wind-up), then lift it (cast).
				if (T < 0.95f)
				{
					FFVTouchPoint P;
					P.Id = 4;
					P.Pos = TouchTapPos;
					Synth.Touches.Add(P);
				}
				const EFVFishState S = Game->GetFishing().GetState();
				if (T > 0.8f && (S == EFVFishState::Waiting || S == EFVFishState::Bite))
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST touch cast ok (state %d)"), int32(S));
					return true;
				}
				return false;
			},
			FString(), 0.1f, 5.f);
		// (no reset: the bot below takes over the bobber that the finger cast)
	}

	// 6. Fish one fish with the bot (touch: from the finger's cast)
	AddStep(TEXT("fishing: until the minigame"),
		[this]()
		{
			FishStartTime = TotalTime;
			WaitingTime = 0.f;
			Game->SetClockMinutes(10.f * 60.f);
			Game->GetFishing().SetAutoPlay(true);
		},
		[this](float) { return DriveFishing(false); },
		TEXT("fishing_minigame"), 0.5f, FVAutoTestLocal::FishingTimeout);

	// the reeling itself is what the BALANCE table and the practice cover: the meter starts near the top here
	AddStep(TEXT("fishing: until the catch showcase"), nullptr,
		[this](float)
		{
			const FVFishing& F = Game->GetFishing();
			if (F.GetState() == EFVFishState::Minigame && F.GetMinigame().Progress < FVAutoTestLocal::CatchBoostBelow)
			{
				SetFightProgress(FVAutoTestLocal::CatchBoost);
			}
			return DriveFishing(true);
		},
		TEXT("fishing_showcase"), 0.8f, FVAutoTestLocal::FishingTimeout);

	AddStep(TEXT("fishing: done"),
		[this]()
		{
			Game->GetFishing().SetAutoPlay(false);
			DismissModals();
			Game->GetFishing().Reset();
			if (UFVSaveGame* P = Game->GetProfile())
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST fishing result: caught=%d escaped=%d keepnet=%d/%d xp=%d"),
					P->Stats.FishCaught, P->Stats.Escaped, P->KeepnetUsed(), P->KeepnetCapacity(), P->Xp);
			}
		},
		nullptr, FString(), 0.2f);

	// 6b. 0.5 developer mode «Бой с легендой (тест)»: the picked legend takes the next bite at the village pond, is fought
	// with the tackle of its level, and nothing is recorded (no catch, XP, «met», stats). The step checks the bookkeeping,
	// not the fight (BALANCE_LEGEND does): after half a second of the legend's rules the meter is kept near the top, so
	// the bot lands it in a few seconds — a landed legend is the case that would record a catch.
	AddStep(TEXT("trial: a legend trial at the pond"),
		[this]()
		{
			WaitingTime = 0.f;
			StepValue = 0;
			if (UFVSaveGame* P = Game->GetProfile())
			{
				TrialFishBefore = P->Stats.FishCaught;
				TrialXpBefore = P->Xp;
				TrialMetBefore = P->LegendsMet.Num();
			}
			Game->GetFishing().StartLegendTrial(FName(TEXT("fish_zhar_ryba")));
			Game->GetFishing().SetAutoPlay(true);
		},
		[this](float T)
		{
			FVFishing& F = Game->GetFishing();
			int32 Tries = 0, Wins = 0;
			F.GetTrialScore(FName(TEXT("fish_zhar_ryba")), Tries, Wins);
			if (F.GetState() == EFVFishState::Waiting)
			{
				WaitingTime += LastDt;
				if (WaitingTime > 1.f) { F.DebugForceBite(); WaitingTime = 0.f; }   // the forced bite still takes the trial legend
			}
			if (Game->HasModal()) { DismissModals(); }
			if (Tries > 0 && F.GetState() == EFVFishState::Minigame)
			{
				StepValue = 1;
				const FFVMinigame& M = F.GetMinigame();
				if (!F.IsLegendIntro() && M.Elapsed > 0.5f && M.Progress < 0.9f)
				{
					SetFightProgress(0.95f);
				}
			}
			if (StepValue == 1 && F.GetState() != EFVFishState::Minigame)
			{
				const UFVSaveGame* P = Game->GetProfile();
				const bool bClean = P && P->Stats.FishCaught == TrialFishBefore && P->Xp == TrialXpBefore && P->LegendsMet.Num() == TrialMetBefore;
				// the meter is kept near the top, so the legend must be landed: the landed trial is the case that would
				// record a catch (an escape records nothing anyway and would leave that case untested)
				const bool bLanded = Wins >= 1;
				if (bClean && bLanded && F.GetPendingTrial().IsNone())
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST legend trial ok (Zhar-ryba: landed %d of %d, nothing recorded)"), Wins, Tries);
				}
				else
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING legend trial: landed %d of %d, recorded something or stayed pending (fish %d -> %d, xp %d -> %d, met %d -> %d, pending %s)"),
						Wins, Tries, TrialFishBefore, P ? P->Stats.FishCaught : -1, TrialXpBefore, P ? P->Xp : -1, TrialMetBefore, P ? P->LegendsMet.Num() : -1,
						*F.GetPendingTrial().ToString());
					++Warnings;
				}
				return true;
			}
			if (T > 40.f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING legend trial: no fight in time (tries %d, state %d)"), Tries, int32(F.GetState()));
				++Warnings;
				return true;
			}
			return false;
		},
		TEXT("trial_zhar_fight"), 0.3f, 45.f);
	AddStep(TEXT("trial: done"),
		[this]()
		{
			Game->GetFishing().SetAutoPlay(false);
			DismissModals();
			Game->GetFishing().Reset();
		},
		nullptr, FString(), 0.3f);

	AddScreenShotStep(EFVScreen::Inventory, TEXT("screen_inventory_after_catch"));
	AddScreenShotStep(EFVScreen::Collection, TEXT("screen_collection_after_catch"));

	// 7. Rich profile: everything unlocked, cosmetics equipped
	AddStep(TEXT("grant everything"),
		[this]()
		{
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			GrantEverything();
			Game->SetWeather(EFVWeather::Sunny);
			Game->SetClockMinutes(12.f * 60.f);
		},
		nullptr, FString(), 0.3f);   // (the rich pond at noon is the location_pond shot right after)

	// 8. Every location at noon
	for (const FFVLocationDef& L : FVDB::Locations())
	{
		AddLocationStep(L.Id, 12.f * 60.f, EFVWeather::Sunny, FString::Printf(TEXT("location_%s"), *L.Id.ToString()));
	}

	// 9. The start pond in the evening, at night and in the rain (the village's own night lights and the rain on the
	// main location without a fight panel over it; the legend fights show the ocean at night / the river in the rain)
	const FName Pond = FVDB::StartLocation();
	AddLocationStep(Pond, 19.f * 60.f + 30.f, EFVWeather::Sunny, TEXT("pond_evening"));
	AddLocationStep(Pond, 23.f * 60.f, EFVWeather::Sunny, TEXT("pond_night"));
	AddLocationStep(Pond, 10.f * 60.f, EFVWeather::Rain, TEXT("pond_rain"));

	// 9b. Update 5: legend rules, the legend fights (intro, snap, glow, lantern, eruption, last dash), collection
	AddLegendSteps();

	// 10. Screens with the rich profile (Mitrich's shop: the stall step below)
	AddScreenShotStep(EFVScreen::Wardrobe, TEXT("screen_wardrobe_full"));
	AddScreenShotStep(EFVScreen::Map, TEXT("screen_map_full"));
	AddScreenShotStep(EFVScreen::Achievements, TEXT("screen_achievements_full"));
	// update 3: the guide with everything unlocked (index) and one entry page
	AddScreenShotStep(EFVScreen::Tutorial, TEXT("screen_tutorial_full"));
	AddScreenShotStepWith(EFVScreen::Tutorial, FName(FVAutoTestLocal::TutorialEntryId), TEXT("screen_tutorial_entry"));
	// update 4: levels, the profile (own + the first-run name choice), the leaderboard, the mail, the restore screen
	AddUpdate4ScreenSteps();
	AddStep(TEXT("back to the world"),
		[this]()
		{
			Game->CloseScreen();
			Game->SetWeather(EFVWeather::Sunny);
		},
		nullptr, FString(), 0.1f);

	// 10b. Update 2: stall -> shop, the home (yard, pickup, house), every new screen, lures and knots, sleeping
	AddHomeSteps();
	// 10c. Update 5: the fried fish by rarity (the legend dish is the one cooking played to the end), digging (timer,
	// the empty-hole penalty, the bot digs to the end)
	AddUpdate5MiniGameSteps();
	// 10d. Update 6: rules (ECONOMY, seeds, ukha, orders, migration, sleep, hot-fixes), camera, HUD column, new screens,
	// the expert fight per rod type, the repeated swipe
	AddUpdate6Steps();

	}   // !bBalanceOnly

	// 11. Balance table (one fish per frame so the game thread never stalls)
	AddStep(TEXT("balance table"),
		[this]()
		{
			BalanceIndex = 0;
			UE_LOG(LogFV, Log, TEXT("FV: BALANCE table: %d fish, %d trials each, bot skill %.2f"), FVDB::Fish().Num(), FVAutoTestLocal::BalanceTrials, FVAutoTestLocal::BalanceBotSkill);
		},
		[this](float) { return RunBalanceStep(); },
		FString(), 0.1f, 90.f);

	// 11b. Update 5: the legend balance (one simulation per frame) and the tricks with the pinned progress
	AddStep(TEXT("legend balance table"),
		[this]()
		{
			LegendBalanceIndex = 0;
			UE_LOG(LogFV, Log, TEXT("FV: BALANCE_LEGEND table: 6 legends x (human-mid, human-strong, «с запасом»), 300 fights each (recorded only)"));
		},
		[this](float) { return RunLegendBalanceStep(); },
		FString(), 0.1f, 60.f);
	AddStep(TEXT("legend tricks (pinned progress)"),
		[this]()
		{
			RunLegendTricksChecks();
			UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST legends summary: %d ok, %d failed"), LegendOk, LegendFailed);
		},
		nullptr, FString(), 0.1f);

	// 11c. Update 6: BALANCE_EXPERT (one cell per frame), then the update-6 summary
	AddStep(TEXT("expert balance table"),
		[this]()
		{
			ExpertBalanceIndex = 0;
			ExpertBalanceNotes = 0;
			UE_LOG(LogFV, Log, TEXT("FV: BALANCE_EXPERT table: fish x rod type x hazard (bots mid / strong / «сыр»), legends «новичок» / «знаток»"));
		},
		[this](float) { return RunExpertBalanceStep(); },
		FString(), 0.1f, 150.f);
	AddStep(TEXT("update 6 summary"),
		[this]() { UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST u6 summary: %d ok, %d failed, %d balance notes"), U6Ok, U6Failed, ExpertBalanceNotes); },
		nullptr, FString(), 0.1f);

	// 12. Sprite contact sheet
	AddStep(TEXT("contact sheet"),
		[this]()
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoTest") / TEXT("contact_sheet.png"));
			if (UFVSprites* Sprites = Game->GetSprites())
			{
				Sprites->DebugWriteContactSheet(Path);
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST contact sheet -> %s"), *Path);
				const FString IconPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoTest") / TEXT("app_icon.png"));
				// update 5: the six legends + silhouettes, the six legend bobbers (both frames) on day and night water
				const FString U5Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoTest") / TEXT("update5_sheet.png"));
				const bool bIcon = FVAutoTestIcon::Write(IconPath);
				const bool bU5 = WriteUpdate5Sheet(U5Path);
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST app icon %s -> %s"), bIcon ? TEXT("written") : TEXT("FAILED"), *IconPath);
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST update 5 sheet %s -> %s"), bU5 ? TEXT("written") : TEXT("FAILED"), *U5Path);
				if (!bIcon || !bU5)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING a sprite sheet could not be written (app icon %s, update 5 sheet %s)"),
						bIcon ? TEXT("ok") : TEXT("FAILED"), bU5 ? TEXT("ok") : TEXT("FAILED"));
					++Warnings;
				}
			}
			else
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING no sprites for the contact sheet"));
				++Warnings;
			}
		},
		nullptr, FString(), 0.2f);

	// 13. Done
	AddStep(TEXT("finish"), [this]() { Finish(false); }, nullptr, FString(), 0.f);
}

// ============================================================================================
// Runner
// ============================================================================================
void FVAutoTest::Tick(float Dt, FFVInput& InOutInput)
{
	Synth = FFVInput();
	if (bFinished || !Game)
	{
		InOutInput = Synth;
		return;
	}

	TotalTime += Dt;
	LastDt = Dt;
	if (TotalTime > FVAutoTestLocal::GlobalTimeout)
	{
		Finish(true);
		InOutInput = Synth;
		return;
	}
	if (!Steps.IsValidIndex(StepIndex))
	{
		Finish(false);
		InOutInput = Synth;
		return;
	}

	FStep& S = Steps[StepIndex];
	auto NextStep = [this]()
	{
		++StepIndex;
		Phase = EPhase::Enter;
		StepTime = 0.f;
		PhaseTime = 0.f;
		PhaseFrames = 0;
	};

	switch (Phase)
	{
	case EPhase::Enter:
		UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST step %d/%d: %s (t=%.1fs)"), StepIndex + 1, Steps.Num(), *S.Name, TotalTime);
		StepTime = 0.f;
		PhaseTime = 0.f;
		PhaseFrames = 0;
		Phase = EPhase::Wait;
		if (S.Enter)
		{
			S.Enter();
		}
		break;

	case EPhase::Wait:
		StepTime += Dt;
		if (!S.Until || S.Until(StepTime))
		{
			Phase = EPhase::Settle;
			PhaseTime = 0.f;
			PhaseFrames = 0;
		}
		else if (StepTime > S.Timeout)
		{
			UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING step '%s' timed out after %.1fs (fishing state %s, screen %d)"),
				*S.Name, StepTime, FVAutoTestLocal::StateName(Game->GetFishing().GetState()), int32(Game->GetScreen()));
			++Warnings;
			Phase = EPhase::Settle;
			PhaseTime = 0.f;
			PhaseFrames = 0;
		}
		break;

	case EPhase::Settle:
		PhaseTime += Dt;
		++PhaseFrames;
		if (PhaseFrames >= 3 && PhaseTime >= S.Settle)
		{
			if (!S.Shot.IsEmpty())
			{
				++ShotCount;
				Game->RequestShot(FString::Printf(TEXT("%02d_%s"), ShotCount, *S.Shot));
				Phase = EPhase::AfterShot;
				PhaseTime = 0.f;
				PhaseFrames = 0;
			}
			else
			{
				NextStep();
			}
		}
		break;

	case EPhase::AfterShot:
		// The screenshot is captured when the next frame is presented; give it a few frames.
		PhaseTime += Dt;
		++PhaseFrames;
		if (PhaseFrames >= 4 && PhaseTime >= 0.1f)
		{
			NextStep();
		}
		break;
	}

	InOutInput = Synth;
}

void FVAutoTest::Finish(bool bTimedOut)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (Game)
	{
		Game->GetFishing().SetAutoPlay(false);
	}
	if (bTimedOut)
	{
		const FString StepName = Steps.IsValidIndex(StepIndex) ? Steps[StepIndex].Name : FString(TEXT("-"));
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST TIMEOUT after %.0fs in step %d '%s' (shots=%d warnings=%d)"),
			TotalTime, StepIndex + 1, *StepName, ShotCount, Warnings);
	}
	else
	{
		UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST DONE (shots=%d warnings=%d time=%.1fs)"), ShotCount, Warnings, TotalTime);
	}
	if (Game)
	{
		Game->QuitGame();
	}
}

// ============================================================================================
// Helpers
// ============================================================================================
void FVAutoTest::DismissModals()
{
	int32 Guard = 0;
	while (Game->HasModal() && Guard++ < 64)
	{
		Game->DismissModal(-1);
	}
}

void FVAutoTest::ClearToasts()
{
	Game->Toasts.Reset();
}

void FVAutoTest::SetFightProgress(float P)
{
	FVFishing& F = Game->GetFishing();
	F.DebugPinProgress(P);      // puts the meter there at once (while a fight runs) ...
	F.DebugPinProgress(-1.f);   // ... and lets the fight go on from there
}

bool FVAutoTest::DriveFishing(bool bUntilShowcase)
{
	FVFishing& Fishing = Game->GetFishing();
	FVScene& Scene = Game->GetScene();
	if (!Fishing.IsAutoPlay())
	{
		Fishing.SetAutoPlay(true);
	}

	const EFVFishState State = Fishing.GetState();
	if (State != EFVFishState::Showcase && Game->HasModal())
	{
		DismissModals();
	}
	if (Game->GetScreen() != EFVScreen::None)
	{
		Game->CloseScreen();
	}

	// Walk further towards the water if the bot cannot cast from here.
	if (State == EFVFishState::Idle && !Scene.CanFishFromHere())
	{
		Synth.MoveY = 1.f;
	}

	// Waiting too long (unlucky rolls): force the bite so the run stays short.
	if (State == EFVFishState::Waiting)
	{
		WaitingTime += LastDt;
		if (WaitingTime > FVAutoTestLocal::ForceBiteAfter)
		{
			UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST forcing a bite"));
			Fishing.DebugForceBite();
			WaitingTime = 0.f;
		}
	}
	else
	{
		WaitingTime = 0.f;
	}

	if (FishStartTime >= 0.f && TotalTime - FishStartTime > FVAutoTestLocal::FishingTimeout)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING fishing did not finish in %.0fs (state %s)"), FVAutoTestLocal::FishingTimeout, FVAutoTestLocal::StateName(State));
		++Warnings;
		return true;
	}

	if (bUntilShowcase)
	{
		if (State == EFVFishState::Showcase)
		{
			// Keep the catch card on screen for the screenshot (the bot would dismiss it).
			Fishing.SetAutoPlay(false);
			return true;
		}
		return false;
	}
	// Junk skips the minigame: let the bot dismiss it and keep fishing until a real fish is on the line.
	return State == EFVFishState::Minigame && Fishing.GetMinigame().Elapsed > 1.2f;
}

void FVAutoTest::GrantEverything()
{
	UFVSaveGame* P = Game->GetProfile();
	if (!P)
	{
		return;
	}

	P->Earn(2000000, false);
	// update 6: XP freezes at level 10 until the first arrival in the City (Р1) — the rich profile has been there
	if (!P->U6.bVisitedCity)
	{
		P->ArriveInCity();
	}
	const int32 NeedXp = FVDB::XpForLevel(FV::MaxLevel) - P->Xp;
	if (NeedXp > 0)
	{
		P->AddXp(NeedXp + 25);
	}
	for (const FFVLocationDef& L : FVDB::Locations())
	{
		P->UnlockedLocations.AddUnique(L.Id);
	}
	for (const FFVRodDef& R : FVDB::Rods())
	{
		P->OwnedRods.AddUnique(R.Id);
	}
	// the best CLASSIC rod (update 6: the expert rods are appended after «Посейдон»; they are equipped by the U6 steps)
	for (int32 i = FVDB::Rods().Num() - 1; i >= 0; --i)
	{
		if (!FVDB::Rods()[i].bExpert)
		{
			P->EquipRod(FVDB::Rods()[i].Id);
			break;
		}
	}
	for (const FFVBaitDef& B : FVDB::Baits())
	{
		P->AddBait(B.Id, 50);
	}
	// the golden black fly (0.5: FVDB::Baits().Last() is the crafted Жар-наживка now, which would change the rich profile's fishing)
	if (!P->EquipBait(FName(TEXT("lure_1_3_2"))) && FVDB::Baits().Num() > 0)
	{
		P->EquipBait(FVDB::Baits().Last().Id);
	}
	if (FVDB::KeepnetLevels().Num() > 0)
	{
		P->KeepnetLevel = FVDB::KeepnetLevels().Num() - 1;
	}
	for (const FFVCosmeticDef& C : FVDB::Cosmetics())
	{
		if (!P->OwnsCosmetic(C.Id))
		{
			P->GrantCosmetic(C.Id);
		}
	}

	// Equip one visible item of every slot (skip the "None" hat/pet styles).
	auto EquipPick = [P](EFVCosmeticSlot Slot, int32 Prefer)
	{
		TArray<const FFVCosmeticDef*> List = FVDB::CosmeticsFor(Slot);
		List.RemoveAll([Slot](const FFVCosmeticDef* C)
		{
			return !C || ((Slot == EFVCosmeticSlot::Hat || Slot == EFVCosmeticSlot::Pet) && C->Style == 0);
		});
		if (List.Num() > 0)
		{
			P->EquipCosmetic(List[FMath::Clamp(Prefer, 0, List.Num() - 1)]->Id);
		}
	};
	EquipPick(EFVCosmeticSlot::Hat, 3);
	EquipPick(EFVCosmeticSlot::Outfit, 2);
	EquipPick(EFVCosmeticSlot::Bobber, 2);
	EquipPick(EFVCosmeticSlot::Pet, 0);

	GrantHome();
	GrantU6();

	Game->NotifyProfileChanged();
	Game->GetScene().RefreshWorldObjects();
	UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST profile: level %d, %lld coins, %d locations, %d rods, %d cosmetics"),
		P->GetLevel(), (long long)P->Money, P->UnlockedLocations.Num(), P->OwnedRods.Num(), P->OwnedCosmetics.Num());
}

bool FVAutoTest::RunBalanceStep()
{
	const TArray<FFVFishDef>& AllFish = FVDB::Fish();
	const TArray<FFVRodDef>& Rods = FVDB::Rods();
	if (Rods.Num() == 0 || !AllFish.IsValidIndex(BalanceIndex))
	{
		return true;
	}

	const FFVFishDef& F = AllFish[BalanceIndex];
	if (F.bExpert)
	{
		// update 6: expert fish fight on the 2D field (BALANCE_EXPERT), not the classic meter
		++BalanceIndex;
		return BalanceIndex >= AllFish.Num();
	}
	const FFVLocationDef* Loc = FVDB::FindLocation(F.Location);
	// update 6: the classic table stays at the classic top level (10) and the best classic rod («Посейдон»), so the
	// reference lines do not move with FV::MaxLevel 15 and the appended expert rods
	const int32 LocLevel = Loc ? FMath::Clamp(Loc->MinLevel, 1, FV::ClassicMaxLevel) : 1;
	const FFVRodDef& Starter = Rods[0];
	const FFVRodDef* BestPtr = &Rods[0];
	for (const FFVRodDef& R : Rods)
	{
		if (!R.bExpert) { BestPtr = &R; }
	}
	const FFVRodDef& Best = *BestPtr;

	const float BarStarter = FVDB::CatchBarHeight(LocLevel, FFVRodStats::FromRod(Starter));
	const float BarBest = FVDB::CatchBarHeight(FV::ClassicMaxLevel, FFVRodStats::FromRod(Best));
	const float RateStarter = FVSimulateCatchRate(float(F.Difficulty), F.Motion, BarStarter, Starter.ReelMult, Starter.DrainMult,
		FVAutoTestLocal::BalanceTrials, 1000 + BalanceIndex * 17, FVAutoTestLocal::BalanceBotSkill);
	const float RateBest = FVSimulateCatchRate(float(F.Difficulty), F.Motion, BarBest, Best.ReelMult, Best.DrainMult,
		FVAutoTestLocal::BalanceTrials, 5000 + BalanceIndex * 17, FVAutoTestLocal::BalanceBotSkill);

	const FString Line = FString::Printf(TEXT("FV: BALANCE %s diff=%d starter=%.0f%% best=%.0f%% (%s, loc=%s lvl=%d motion=%s rarity=%s)"),
		*F.Id.ToString(), F.Difficulty, RateStarter * 100.f, RateBest * 100.f, *F.Name, *F.Location.ToString(), LocLevel,
		*FVDB::MotionName(F.Motion), *FVDB::RarityName(F.Rarity));
	UE_LOG(LogFV, Log, TEXT("%s"), *Line);

	// update 5 regression (§5): every classic fight (all fish here, the legends included, fight the classic way in this
	// table) must give exactly the line of the reference log (2026-09-24, identical to 0.4.1)
	if (!bBalanceReferenceLoaded)
	{
		LoadBalanceReference();
	}
	if (BalanceReference.Num() > 0)
	{
		const FString* Ref = BalanceReference.Find(F.Id);
		if (Ref && *Ref == Line)
		{
			++BalanceMatched;
		}
		else if (!Ref)
		{
			// update 6 (§3а): a fish added after the reference was recorded — only its line is logged
			UE_LOG(LogFV, Log, TEXT("FV: BALANCE regression: %s is new (no reference line)"), *F.Id.ToString());
		}
		else
		{
			++BalanceMismatched;
			UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING BALANCE regression: %s differs from the reference ('%s')"), *F.Id.ToString(), Ref ? **Ref : TEXT("missing"));
			++Warnings;
		}
	}

	++BalanceIndex;
	const bool bDone = BalanceIndex >= AllFish.Num();
	if (bDone && BalanceReference.Num() > 0)
	{
		UE_LOG(LogFV, Log, TEXT("FV: BALANCE regression: %d of %d lines identical to the reference%s"), BalanceMatched, BalanceMatched + BalanceMismatched,
			BalanceMismatched == 0 ? TEXT(" (ok)") : TEXT(" (FAILED)"));
	}
	return bDone;
}

// ============================================================================================
// Update 2: house, stalls, stations, mini-games, sleeping
// ============================================================================================
int32 FVAutoTest::BackpackUnits() const
{
	int32 N = 0;
	if (const UFVSaveGame* P = Game->GetProfile())
	{
		for (const FFVInvStack& S : P->Backpack)
		{
			N += FMath::Max(0, S.Count);
		}
	}
	return N;
}

bool FVAutoTest::TeleportToObject(EFVObjKind Kind, FName Id, const TCHAR* What)
{
	FVScene& Scene = Game->GetScene();
	const FFVWorldObject* Obj = Scene.FindObject(Kind, 0, Id);
	if (!Obj)
	{
		for (const FFVWorldObject& O : Scene.GetWorldObjects())
		{
			if (O.Kind == Kind && (Id.IsNone() || O.Id == Id))
			{
				Obj = &O;
				break;
			}
		}
	}
	if (!Obj)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING no %s in scene %s (%d objects)"), What, *Scene.GetLocationId().ToString(), Scene.GetWorldObjects().Num());
		++Warnings;
		return false;
	}
	const FVector2D Feet = Obj->Feet;
	const FString Label = Obj->Label;
	Scene.ClearWalkTarget();
	Scene.DebugTeleport(Feet);
	UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST teleported to %s '%s' at (%.0f, %.0f)"), What, *Label, Feet.X, Feet.Y);
	return true;
}

void FVAutoTest::InteractOnce(float T, EFVObjKind Kind, FName Id)
{
	if (bStepFlag || T <= 0.15f)
	{
		return;
	}
	bStepFlag = true;
	FVScene& Scene = Game->GetScene();
	auto Matches = [Kind, Id](const FFVWorldObject& O) { return O.Kind == Kind && (Id.IsNone() || O.Id == Id); };
	const FFVWorldObject* Near = Scene.NearestInReach();
	if (Near && Matches(*Near))
	{
		Synth.Pressed.Add(EKeys::E);   // the real input path: UFVGame::TickWorldInteraction -> Interact
		return;
	}
	// Another object is closer to the teleport point: use the intended one directly (still the same dispatch).
	for (const FFVWorldObject& O : Scene.GetWorldObjects())
	{
		if (Matches(O) && Scene.IsInReach(O))
		{
			UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST note: nearest object is '%s', interacting with '%s' directly"), Near ? *Near->Label : TEXT("-"), *O.Label);
			const FFVWorldObject Copy = O;
			Game->Interact(Copy);
			return;
		}
	}
	UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING the target object (kind %d) is not in reach after the teleport"), int32(Kind));
	++Warnings;
}

void FVAutoTest::AddScreenShotStepWith(EFVScreen Screen, FName Context, const FString& Shot)
{
	AddStep(FString::Printf(TEXT("open screen %s (%s)"), FVAutoTestLocal::ScreenSlug(Screen), *Context.ToString()),
		[this, Screen, Context]()
		{
			DismissModals();
			Game->OpenScreenWith(Screen, Context);
		},
		[this, Screen, Context](float) { return Game->GetScreen() == Screen && Game->GetScreenContext() == Context; },
		Shot, 0.3f, 3.f);
}

void FVAutoTest::AddMiniGameSteps(EFVScreen Screen, FName Context, const FString& Slug)
{
	// one step from the opening to the end: the screen's bot plays it and takes the minigame_<slug>_play / _result shots
	AddStep(FString::Printf(TEXT("mini-game %s (%s): the bot plays it to the end"), *Slug, *Context.ToString()),
		[this, Screen, Context]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			Game->OpenScreenWith(Screen, Context);
			if (Game->GetScreen() != Screen)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING mini-game %s did not open"), FVAutoTestLocal::ScreenSlug(Screen));
				++Warnings;
			}
		},
		[this, Screen](float) { return Game->GetScreen() != Screen; },
		FString(), 0.2f, 12.f);
	AddStep(FString::Printf(TEXT("mini-game %s: back to the world"), *Slug),
		[this, Screen, Slug]()
		{
			if (Game->GetScreen() == Screen)
			{
				// Timed out above (already counted as a warning): leave it without spending anything.
				Game->CloseScreen();
			}
			UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST mini-game %s finished -> screen %d (%s)"), *Slug, int32(Game->GetScreen()), *Game->GetScreenContext().ToString());
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
		},
		nullptr, FString(), 0.1f);
}

void FVAutoTest::AddHomeSteps()
{
	const FName Pond = FVDB::StartLocation();

	// ---- Mitrich's stall at the pond: walk up (teleport) + E opens his shop
	AddStep(TEXT("pond: E at the stall of Mitrich opens the shop"),
		[this, Pond]()
		{
			bStepFlag = false;
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			Game->SetWeather(EFVWeather::Sunny);
			if (Game->GetScene().GetLocationId() != Pond)
			{
				Game->TravelTo(Pond);
			}
			Game->SetClockMinutes(11.f * 60.f);
			TeleportToObject(EFVObjKind::Stall, FVDB::DefaultTraderId(), TEXT("stall"));
		},
		[this](float T)
		{
			InteractOnce(T, EFVObjKind::Stall, FVDB::DefaultTraderId());
			if (Game->GetScreen() == EFVScreen::Shop && T > 0.5f)
			{
				const bool bMitrich = Game->GetScreenContext() == FVDB::DefaultTraderId();
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST stall -> shop ok (trader %s)%s"), *Game->GetScreenContext().ToString(), bMitrich ? TEXT("") : TEXT(" (unexpected trader)"));
				if (!bMitrich)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING the pond stall opened trader %s"), *Game->GetScreenContext().ToString());
					++Warnings;
				}
				return true;
			}
			return false;
		},
		TEXT("pond_stall_shop"), 0.5f, 3.f);

	// ---- update 3 (touch): tap anywhere next to the stall -> the shop; swipe across the shop -> another tab
	if (FV::IsTouchUI())
	{
		AddTouchUpdate3Steps();
	}

	// ---- the map -> travel home (the yard)
	AddStep(TEXT("map -> travel home"),
		[this]()
		{
			bStepFlag = false;
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			Game->OpenScreen(EFVScreen::Map);
			Game->SetClockMinutes(10.f * 60.f);
		},
		[this](float T)
		{
			if (!bStepFlag && T > 0.2f)
			{
				bStepFlag = true;
				if (!Game->TravelTo(FVDB::HomeId()))
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING travel home failed"));
					++Warnings;
					return true;
				}
			}
			return bStepFlag && Game->IsAtHome() && !Game->IsInHouse() && Game->GetScreen() == EFVScreen::None && T > 0.5f;
		},
		TEXT("home_yard"), 0.4f, 4.f);

	// ---- a resource in the yard: teleport + E, the backpack must grow
	AddStep(TEXT("yard: pick up a resource"),
		[this]()
		{
			bStepFlag = false;
			StepValue = BackpackUnits();
			if (!TeleportToObject(EFVObjKind::Resource, NAME_None, TEXT("resource")))
			{
				StepValue = -1;   // nothing to pick up (already warned)
			}
		},
		[this](float T)
		{
			if (StepValue < 0)
			{
				return true;
			}
			InteractOnce(T, EFVObjKind::Resource, NAME_None);
			if (BackpackUnits() > StepValue)
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST resource pickup ok (backpack %d -> %d units)"), StepValue, BackpackUnits());
				return true;
			}
			if (T > 1.2f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING resource pickup: the backpack did not grow (%d units)"), BackpackUnits());
				++Warnings;
				return true;
			}
			return false;
		},
		FString(), 0.4f, 3.f);

	// ---- the house door: E enters the interior
	AddStep(TEXT("yard: enter the house"),
		[this]()
		{
			bStepFlag = false;
			ClearToasts();
			Game->SetClockMinutes(13.f * 60.f);
			TeleportToObject(EFVObjKind::Door, NAME_None, TEXT("house door"));
		},
		[this](float T)
		{
			InteractOnce(T, EFVObjKind::Door, NAME_None);
			return Game->IsInHouse() && T > 0.4f;
		},
		TEXT("home_interior"), 0.4f, 3.f);

	// ---- every update-2 screen with its context (the bed and the chest station pages are info-only cards: the bed is
	// the sleep step below, the chest is its storage screen)
	AddScreenShotStepWith(EFVScreen::Inventory, FName(TEXT("backpack")), TEXT("screen_inventory_backpack"));
	AddScreenShotStepWith(EFVScreen::Shop, FName(TEXT("tr_pier")), TEXT("screen_shop_pier"));
	AddScreenShotStepWith(EFVScreen::Chest, NAME_None, TEXT("screen_chest"));
	AddScreenShotStepWith(EFVScreen::IceCellar, NAME_None, TEXT("screen_ice_cellar"));
	for (int32 i = 0; i < int32(EFVStation::Count); ++i)
	{
		const EFVStation St = EFVStation(i);
		const FName Id = FVDB::StationId(St);
		if (!Id.IsNone() && St != EFVStation::Bed && St != EFVStation::Chest)
		{
			AddScreenShotStepWith(EFVScreen::Station, Id, FString::Printf(TEXT("screen_station_%s"), *Id.ToString()));
		}
	}
	AddScreenShotStepWith(EFVScreen::Orders, NAME_None, TEXT("screen_orders"));
	AddScreenShotStepWith(EFVScreen::NetSpot, FName(*FString::Printf(TEXT("%s:0"), *Pond.ToString())), TEXT("screen_net_spot"));
	AddScreenShotStepWith(EFVScreen::HomeDecor, NAME_None, TEXT("screen_home_decor"));

	// ---- mini-games (a built-in bot plays them under -fvautotest); cooking and digging are played once, with the
	// update-5 checks (AddUpdate5MiniGameSteps)
	AddMiniGameSteps(EFVScreen::LureCraft, NAME_None, TEXT("lure_craft"));
	AddMiniGameSteps(EFVScreen::Knots, FName(TEXT("rcp_line_linen")), TEXT("knots"));

	// ---- sleeping: the night passes (nets, smokehouse, worm farm, orders), then the morning summary
	AddStep(TEXT("bed: sleep until the morning"),
		[this]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			StepValue = Game->GetDay();
			Game->SetClockMinutes(22.f * 60.f);
			Game->SleepUntilMorning();
		},
		[this](float T) { return Game->HasModal() && T > 0.3f; },
		TEXT("sleep_summary"), 0.6f, 3.f);
	AddStep(TEXT("bed: good morning"),
		[this]()
		{
			const bool bOk = Game->GetDay() == StepValue + 1 && FMath::IsNearlyEqual(Game->GetClockMinutes(), FV::DayStartMinutes, 1.f);
			UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST sleep: day %d -> %d, %s %s"), StepValue, Game->GetDay(), *Game->GetClockText(), bOk ? TEXT("(ok)") : TEXT("(FAILED)"));
			if (!bOk)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING sleeping did not end at 06:00 of the next day"));
				++Warnings;
			}
			DismissModals();
		},
		nullptr, FString(), 0.3f);
}

// ============================================================================================
// Update 4: profile, leaderboards, mail, levels, restore (+ the online services with -fvserver)
// ============================================================================================
bool FVAutoTest::IsOnlineRun() const
{
	const UFVOnline* O = Game ? Game->GetOnline() : nullptr;
	return O && O->IsEnabled();
}

void FVAutoTest::AddUpdate4ScreenSteps()
{
	AddScreenShotStep(EFVScreen::Levels, TEXT("screen_levels"));
	AddScreenShotStep(EFVScreen::Profile, TEXT("screen_profile"));
	AddScreenShotStepWith(EFVScreen::Profile, FName(TEXT("setup")), TEXT("screen_profile_setup"));
	AddScreenShotStep(EFVScreen::Leaderboard, TEXT("screen_leaderboard"));
	AddScreenShotStep(EFVScreen::Mail, TEXT("screen_mail"));
	AddScreenShotStep(EFVScreen::RestoreSave, TEXT("screen_restore_save"));
	AddStep(TEXT("update 4 screens: close"),
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

void FVAutoTest::AddTouchUpdate4Steps()
{
	auto Finger = [this](const FVector2D& Pos)
	{
		FFVTouchPoint P;
		P.Id = 5;
		P.Pos = Pos;
		Synth.Touches.Add(P);
	};
	// checks only: both screens are shot with the rich profile (screen_levels, screen_profile)
	struct FTap { EFVScreen Screen; const TCHAR* What; };
	const FTap Taps[] = {
		{ EFVScreen::Levels, TEXT("the level badge") },
		{ EFVScreen::Profile, TEXT("the profile button") },
	};
	for (const FTap& Tap : Taps)
	{
		const EFVScreen Screen = Tap.Screen;
		const FString What = Tap.What;
		AddStep(FString::Printf(TEXT("touch: tap %s"), Tap.What), nullptr,
			[this, Finger, Screen, What](float T)
			{
				const FFVRect R = Game->GetHudButtonRect(Screen);
				if (T < 0.06f && R.W > 0.f) { Finger(R.Center()); }
				if (Game->GetScreen() == Screen && T > 0.2f)
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST touch tap on %s opened %s (ok)"), *What, FVAutoTestLocal::ScreenSlug(Screen));
					return true;
				}
				if (T > 1.5f)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING a tap on %s did not open %s (screen %d)"), *What, FVAutoTestLocal::ScreenSlug(Screen), int32(Game->GetScreen()));
					++Warnings;
					return true;
				}
				return false;
			},
			FString(), 0.1f, 3.f);
		AddStep(FString::Printf(TEXT("touch: close %s"), FVAutoTestLocal::ScreenSlug(Screen)),
			[this]()
			{
				DismissModals();
				if (Game->GetScreen() != EFVScreen::None)
				{
					Game->CloseScreen();
				}
			},
			[this](float) { return Game->GetScreen() == EFVScreen::None; }, FString(), 0.2f, 3.f);
	}
}

void FVAutoTest::AddOnlineSteps()
{
	// 0.5: a normal game (no -fvautotest / -fvserver) must know the real server — 0.4 / 0.4.1 shipped «Онлайн отключён»
	// because the address came only from a config section the engine did not return at start-up.
	AddStep(TEXT("online: the built-in server address"),
		[this]()
		{
			const FString Url = UFVOnline::ConfiguredServerUrl();
			if (Url.StartsWith(TEXT("https://")))
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online a normal game uses %s (ok)"), *Url);
			}
			else
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online: a normal game has no server address ('%s')"), *Url);
				++Warnings;
			}
		},
		nullptr, FString(), 0.f);

	// UFVOnline::Init ran before the autotest was created: whether this run is online is known now.
	if (!IsOnlineRun())
	{
		AddStep(TEXT("online: skipped"),
			[]() { UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online steps skipped (offline run: no -fvserver)")); },
			nullptr, FString(), 0.f);
		return;
	}

	// registration: happens by itself once the fisherman has a name and the server answers
	AddStep(TEXT("online: registration"),
		[this]() { OnlineChecksOk = 0; },
		[this](float T)
		{
			UFVOnline* O = Game->GetOnline();
			if (!IsOnlineRun() || !O)
			{
				return true;
			}
			if (O->IsRegistered())
			{
				const UFVSaveGame* P = Game->GetProfile();
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online registration ok (%s %s, id %lld)"), P ? *P->PlayerName : TEXT("?"), *O->GetTagText(), P ? (long long)P->OnlineId : 0ll);
				++OnlineChecksOk;
				return true;
			}
			if (O->GetStatus() == EFVOnlineStatus::Banned || T > 20.f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online registration failed: %s"), *O->GetStatusText());
				++Warnings;
				return true;
			}
			return false;
		},
		FString(), 0.2f, 25.f);

	// sync (+ a save backup): the server answers with the season -> known
	AddStep(TEXT("online: sync"),
		[this]()
		{
			if (UFVOnline* O = Game->GetOnline(); O && IsOnlineRun() && O->IsRegistered())
			{
				O->RequestSync(true);
			}
		},
		[this](float T)
		{
			UFVOnline* O = Game->GetOnline();
			if (!IsOnlineRun() || !O || !O->IsRegistered())
			{
				return true;
			}
			if (T > 1.5f && O->IsOnline() && O->GetSeason().bKnown)
			{
				const FFVSeasonInfo& S = O->GetSeason();
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online sync ok (season %d, %d participants, ends in %lld s, mail %d)"),
					S.Id, S.Participants, (long long)(S.EndsUnix - O->GetServerNow()), O->UnreadMailCount());
				++OnlineChecksOk;
				return true;
			}
			if (T > 15.f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online sync: no answer (%s)"), *O->GetStatusText());
				++Warnings;
				return true;
			}
			return false;
		},
		FString(), 0.2f, 20.f);

	// a leaderboard page (all-time level)
	AddStep(TEXT("online: fetch a board"),
		[this]()
		{
			Probe = MakeShared<FOnlineProbe>();   // a fresh probe: a late answer of an earlier request cannot count
			UFVOnline* O = Game->GetOnline();
			if (!IsOnlineRun() || !O)
			{
				Probe->bDone = true;
				Probe->Info = TEXT("skip");
				return;
			}
			TSharedRef<FOnlineProbe> P = Probe;
			O->FetchBoard(EFVBoardCategory::Level, FString(), 0, 20, [P](const FFVBoardPage& Page)
			{
				P->bDone = true;
				P->bOk = Page.bOk;
				P->Count = Page.Entries.Num();
				P->Total = Page.Total;
				P->Rank = Page.bHasMe ? Page.Me.Rank : 0;
				P->Info = Page.Error;
			});
		},
		[this](float T)
		{
			if (Probe->bDone)
			{
				if (Probe->Info == TEXT("skip"))
				{
					return true;
				}
				if (Probe->bOk)
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online board ok (%d rows of %d, my rank %d)"), Probe->Count, Probe->Total, Probe->Rank);
					++OnlineChecksOk;
				}
				else
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online board failed: %s"), *Probe->Info);
					++Warnings;
				}
				return true;
			}
			if (T > 15.f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online board: no answer"));
				++Warnings;
				return true;
			}
			return false;
		},
		FString(), 0.2f, 20.f);

	// the own profile as the server sees it
	AddStep(TEXT("online: fetch the own server profile"),
		[this]()
		{
			Probe = MakeShared<FOnlineProbe>();   // a fresh probe: a late answer of an earlier request cannot count
			UFVOnline* O = Game->GetOnline();
			const UFVSaveGame* Me = Game->GetProfile();
			if (!IsOnlineRun() || !O || !Me || Me->OnlineId <= 0)
			{
				Probe->bDone = true;
				Probe->Info = TEXT("skip");
				return;
			}
			TSharedRef<FOnlineProbe> P = Probe;
			O->FetchProfile(Me->OnlineId, [P](const FFVPublicProfile& Prof)
			{
				P->bDone = true;
				P->bOk = Prof.bOk;
				P->Count = Prof.FishTotal;
				P->Rank = Prof.Level;
				P->Info = Prof.bOk ? FString::Printf(TEXT("%s #%d"), *Prof.Name, Prof.Tag) : Prof.Error;
			});
		},
		[this](float T)
		{
			if (Probe->bDone)
			{
				if (Probe->Info == TEXT("skip"))
				{
					return true;
				}
				if (Probe->bOk)
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online profile ok (%s, level %d, %d fish)"), *Probe->Info, Probe->Rank, Probe->Count);
					++OnlineChecksOk;
				}
				else
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online profile failed: %s"), *Probe->Info);
					++Warnings;
				}
				return true;
			}
			if (T > 15.f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online profile: no answer"));
				++Warnings;
				return true;
			}
			return false;
		},
		FString(), 0.2f, 20.f);

	// the mail box
	AddStep(TEXT("online: fetch the mail"),
		[this]()
		{
			Probe = MakeShared<FOnlineProbe>();   // a fresh probe: a late answer of an earlier request cannot count
			UFVOnline* O = Game->GetOnline();
			if (!IsOnlineRun() || !O || !O->IsRegistered())
			{
				Probe->bDone = true;
				Probe->Info = TEXT("skip");
				return;
			}
			TSharedRef<FOnlineProbe> P = Probe;
			TWeakObjectPtr<UFVOnline> WeakO(O);
			O->FetchMail([P, WeakO](bool bOk)
			{
				P->bDone = true;
				P->bOk = bOk;
				P->Count = WeakO.IsValid() ? WeakO->GetMail().Num() : 0;
			});
		},
		[this](float T)
		{
			if (Probe->bDone)
			{
				if (Probe->Info == TEXT("skip"))
				{
					return true;
				}
				if (Probe->bOk)
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online mail ok (%d letters)"), Probe->Count);
					++OnlineChecksOk;
				}
				else
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online mail failed"));
					++Warnings;
				}
				return true;
			}
			if (T > 15.f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online mail: no answer"));
				++Warnings;
				return true;
			}
			return false;
		},
		FString(), 0.2f, 20.f);

	// the leaderboard and the own server profile with real data (give them a moment to load), then the summary
	AddStep(TEXT("online: leaderboard screen"),
		[this]()
		{
			DismissModals();
			Game->OpenScreen(EFVScreen::Leaderboard);
		},
		[](float T) { return T > 1.5f; },
		TEXT("online_leaderboard"), 0.2f, 5.f);
	AddStep(TEXT("online: server profile screen"),
		[this]()
		{
			const UFVSaveGame* Me = Game->GetProfile();
			if (Me && Me->OnlineId > 0)
			{
				Game->OpenScreenWith(EFVScreen::Profile, FName(*FString::Printf(TEXT("%lld"), (long long)Me->OnlineId)));
			}
		},
		[](float T) { return T > 1.5f; },
		TEXT("online_profile"), 0.2f, 5.f);

	// 0.4.1: the offline mode lasts only while there is no connection. The local server is up, so after «coming back
	// from the background» (NotifyAppResume = probe now instead of in 20 s) the game must leave it by itself.
	AddStep(TEXT("online: offline mode ends by itself"),
		[this]()
		{
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			if (UFVOnline* O = Game->GetOnline(); O && IsOnlineRun())
			{
				O->DebugEnterOfflineMode();
				O->NotifyAppResume();
			}
		},
		[this](float T)
		{
			UFVOnline* O = Game->GetOnline();
			if (!IsOnlineRun() || !O)
			{
				return true;
			}
			if (T > 0.5f && !O->IsOfflineMode() && O->GetStartupState() == EFVStartupState::Ready && O->IsOnline())
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online offline mode ended by itself in %.1f s (%s)"), T, *O->GetStatusText());
				++OnlineChecksOk;
				return true;
			}
			if (T > 15.f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online offline mode did not end by itself (offline %s, state %d, %s, last error '%s')"),
					O->IsOfflineMode() ? TEXT("yes") : TEXT("no"), int32(O->GetStartupState()), *O->GetStatusText(), *O->GetLastNetError());
				++Warnings;
				return true;
			}
			return false;
		},
		TEXT("online_back_from_offline"), 0.2f, 20.f);

	AddStep(TEXT("online: done"),
		[this]()
		{
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			DismissModals();
			if (OnlineChecksOk >= 6)
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST online all checks ok (%d/6)"), OnlineChecksOk);
			}
			else
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING online: only %d/6 checks ok"), OnlineChecksOk);
				++Warnings;
			}
		},
		nullptr, FString(), 0.2f);
}

void FVAutoTest::GrantHome()
{
	UFVSaveGame* P = Game->GetProfile();
	if (!P)
	{
		return;
	}
	const double Now = Game->GetAbsMinutes();
	auto Warn = [this](const FString& What)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING grant: %s"), *What);
		++Warnings;
	};

	// Upgrades: max backpack, best shovel, every station at its max level.
	P->BackpackLevel = FMath::Max(0, FVDB::BackpackLevels().Num() - 1);
	P->ShovelLevel = FVDB::Shovels().Num();
	for (int32 i = 0; i < int32(EFVStation::Count); ++i)
	{
		const EFVStation S = EFVStation(i);
		const FName Id = FVDB::StationId(S);
		if (!Id.IsNone())
		{
			P->StationLevels.Add(Id, FMath::Max(1, FVDB::StationMaxLevel(S)));
		}
	}

	// Backpack: a bit of everything (leaves free slots for the pickup test); the chest gets the bulk.
	auto Give = [P, &Warn](FName Id, int32 N, EFVQuality Q)
	{
		if (!P->GiveItem(Id, N, uint8(Q)))
		{
			Warn(FString::Printf(TEXT("%s x%d did not fit"), *Id.ToString(), N));
		}
	};
	auto Stash = [P, &Warn](FName Id, int32 N)
	{
		if (!P->AddTo(EFVContainer::Chest, Id, N))
		{
			Warn(FString::Printf(TEXT("%s x%d did not fit into the chest"), *Id.ToString(), N));
		}
	};
	// (update 6 added many resources / materials: the backpack takes them while it has room for the rest below, and
	// the chest takes them while it keeps room for the tackle)
	auto Room = [P](EFVContainer C, int32 Keep) { return P->ContainerCapacity(C) - P->ContainerUsed(C) > Keep; };
	for (const FFVItemDef* It : FVDB::ItemsOfKind(EFVItemKind::Resource))
	{
		if (It && Room(EFVContainer::Backpack, 30)) { Give(It->Id, 12, EFVQuality::Normal); }
		if (It && Room(EFVContainer::Chest, 16)) { Stash(It->Id, 60); }
	}
	for (const FFVItemDef* It : FVDB::ItemsOfKind(EFVItemKind::Material))
	{
		if (It && Room(EFVContainer::Backpack, 30)) { Give(It->Id, 8, EFVQuality::Normal); }
		if (It && Room(EFVContainer::Chest, 16)) { Stash(It->Id, 25); }
	}
	{
		const EFVQuality FoodQ[] = { EFVQuality::Silver, EFVQuality::Normal, EFVQuality::Gold, EFVQuality::Iridium };
		int32 k = 0;
		for (const FFVItemDef* It : FVDB::ItemsOfKind(EFVItemKind::Food))
		{
			if (It) { Give(It->Id, 3, FoodQ[k++ % 4]); }
		}
	}
	const TArray<const FFVItemDef*> Lines = FVDB::ItemsOfKind(EFVItemKind::Line);
	const TArray<const FFVItemDef*> Floats = FVDB::ItemsOfKind(EFVItemKind::Float);
	const TArray<const FFVItemDef*> Nets = FVDB::ItemsOfKind(EFVItemKind::Net);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		if (!Lines[i]) { continue; }
		if (i == 1) { Give(Lines[i]->Id, 1, EFVQuality::Normal); }
		else { Stash(Lines[i]->Id, 1); }
	}
	for (int32 i = 0; i < Floats.Num(); ++i)
	{
		if (!Floats[i]) { continue; }
		if (i == 2) { Give(Floats[i]->Id, 1, EFVQuality::Normal); }
		else { Stash(Floats[i]->Id, 1); }
	}
	for (const FFVItemDef* It : Nets)
	{
		if (It) { Give(It->Id, 1, EFVQuality::Normal); }
	}
	for (const FFVJunkDef& J : FVDB::Junk())
	{
		// (integration: the Mill's foods filled the backpack — the junk only takes the room the later steps do not need)
		if (Room(EFVContainer::Backpack, 10)) { Give(J.Id, 2, EFVQuality::Normal); }
	}

	// Rod tackle: the best line and float, installed on the equipped rod (workshop built above).
	auto BestTackle = [](const TArray<const FFVItemDef*>& List) -> const FFVItemDef*
	{
		const FFVItemDef* Best = nullptr;
		for (const FFVItemDef* It : List)
		{
			if (It && (!Best || It->Tier > Best->Tier)) { Best = It; }
		}
		return Best;
	};
	const FFVItemDef* TackleList[] = { BestTackle(Lines), BestTackle(Floats) };
	for (const FFVItemDef* Tackle : TackleList)
	{
		if (!Tackle)
		{
			continue;
		}
		Give(Tackle->Id, 1, EFVQuality::Normal);
		const EFVBuyResult R = P->InstallTackle(P->EquippedRod, Tackle->Id);
		if (R != EFVBuyResult::Ok)
		{
			Warn(FString::Printf(TEXT("install %s on %s -> %s"), *Tackle->Id.ToString(), *P->EquippedRod.ToString(), *UFVSaveGame::BuyResultText(R)));
		}
	}

	// Fish: keepnet (incl. a smoked one), ice cellar, aquarium, trophies, smokehouse, a placed net with a catch.
	const FName Pond = FVDB::StartLocation();
	TArray<const FFVFishDef*> PondFish = FVDB::FishAt(Pond);
	PondFish.RemoveAll([](const FFVFishDef* F) { return !F || F->bLegendary; });
	TArray<const FFVFishDef*> Various;
	for (const FFVFishDef& F : FVDB::Fish())
	{
		if (!F.bLegendary && (Various.Num() == 0 || Various.Last()->Location != F.Location))
		{
			Various.Add(&F);
		}
	}
	for (int32 i = 0; i < FMath::Min(5, PondFish.Num()); ++i)
	{
		if (!P->AddToKeepnet(PondFish[i]->Id, EFVQuality(i % 3), 2))
		{
			Warn(FString::Printf(TEXT("keepnet %s"), *PondFish[i]->Id.ToString()));
		}
	}
	if (PondFish.Num() > 0 && !P->AddToKeepnetEx(PondFish[0]->Id, EFVQuality::Silver, 1, FVStackFlags::Smoked))
	{
		Warn(TEXT("smoked keepnet fish"));
	}
	for (int32 i = 0; i < FMath::Min(4, Various.Num()); ++i)
	{
		if (!P->AddTo(EFVContainer::IceCellar, Various[i]->Id, 3, uint8(i % 4)))
		{
			Warn(FString::Printf(TEXT("ice cellar %s"), *Various[i]->Id.ToString()));
		}
	}
	const int32 AquaCap = FMath::Min(6, P->StationCapacity(EFVStation::Aquarium));
	for (int32 i = 0; i < FMath::Min(AquaCap, Various.Num()); ++i)
	{
		P->AddTo(EFVContainer::Aquarium, Various[i]->Id, 1, uint8(i % 4));
	}
	P->Trophies.Reset();
	for (int32 i = 0; i < FMath::Min(5, Various.Num()); ++i)
	{
		const FFVFishDef& F = *Various[i];
		FFVTrophy T;
		T.FishId = F.Id;
		T.SizeCm = FMath::RoundToFloat(F.MaxSize * 0.92f * 10.f) / 10.f;
		T.Quality = uint8(EFVQuality::Gold);
		P->Trophies.Add(T);
		FFVFishRecord& Rec = P->Records.FindOrAdd(F.Id);
		Rec.Caught = FMath::Max(1, Rec.Caught);
		Rec.MaxSize = FMath::Max(Rec.MaxSize, T.SizeCm);
		Rec.BestQuality = FMath::Max<uint8>(Rec.BestQuality, T.Quality);
	}
	P->Smokehouse.Reset();
	const float SmokeTimes[] = { 240.f, 75.f, 0.f };
	for (int32 i = 0; i < FMath::Min(3, PondFish.Num()); ++i)
	{
		FFVSmokeSlot Slot;
		Slot.FishId = PondFish[i]->Id;
		Slot.Quality = uint8(i % 3);
		Slot.MinutesLeft = SmokeTimes[i];
		P->Smokehouse.Add(Slot);
	}
	P->SmokeFuel = 150.f;
	P->WormFarmWorms = 6;
	P->WormFarmCrawlers = 2;
	P->WormFarmDay = P->Day;

	if (Nets.Num() > 0 && Nets[0])
	{
		const FFVItemDef* NetDef = (Nets.Num() > 1 && Nets[1]) ? Nets[1] : Nets[0];
		Give(NetDef->Id, 1, EFVQuality::Normal);
		const EFVBuyResult R = P->PlaceNet(Pond, 0, NetDef->Id, Now);
		if (R != EFVBuyResult::Ok)
		{
			Warn(FString::Printf(TEXT("place %s at %s:0 -> %s"), *NetDef->Id.ToString(), *Pond.ToString(), *UFVSaveGame::BuyResultText(R)));
		}
		else if (FFVNetState* Net = P->FindNet(Pond, 0))
		{
			Net->Durability = NetDef->NetDurability * 0.6f;
			Net->Catch.Reset();
			for (int32 i = 0; i < FMath::Min(3, PondFish.Num()); ++i)
			{
				FFVInvStack S;
				S.ItemId = PondFish[i]->Id;
				S.Quality = uint8(i % 2);
				S.Count = 1 + (i % 2);
				Net->Catch.Add(S);
			}
			if (FVDB::Junk().Num() > 0)
			{
				FFVInvStack J;
				J.ItemId = FVDB::Junk()[0].Id;
				J.Count = 1;
				Net->Catch.Add(J);
			}
		}
	}

	// House decor: everything owned, a non-starter piece equipped in every slot.
	for (const FFVDecorDef& Dc : FVDB::Decor())
	{
		P->GrantDecor(Dc.Id);
	}
	for (int32 s = 0; s < int32(EFVDecorSlot::Count); ++s)
	{
		const TArray<const FFVDecorDef*> List = FVDB::DecorFor(EFVDecorSlot(s));
		if (List.Num() > 0)
		{
			const FFVDecorDef* Pick = List[FMath::Min(1, List.Num() - 1)];
			if (Pick)
			{
				P->EquipDecor(Pick->Id);
			}
		}
	}

	// Lure workshop: 10 of 24 recipes discovered.
	int32 Found = 0;
	for (int32 c = 0; c < FVDB::LureColors && Found < 10; ++c)
	{
		for (int32 b = 0; b < FVDB::LureBases && Found < 10; ++b)
		{
			for (int32 g = 0; g < FVDB::LureGlitters && Found < 10; g += 2)
			{
				P->DiscoveredLures.AddUnique(FVDB::LureId(uint8(b), uint8(c), uint8(g)));
				++Found;
			}
		}
	}

	// An active food buff (shown next to the clock).
	const TArray<const FFVItemDef*> Foods = FVDB::ItemsOfKind(EFVItemKind::Food);
	if (Foods.Num() > 0 && Foods.Last())
	{
		const FName FoodId = Foods.Last()->Id;
		const int32 Idx = P->Backpack.IndexOfByPredicate([FoodId](const FFVInvStack& S) { return S.ItemId == FoodId; });
		if (Idx == INDEX_NONE || !P->EatFood(Idx, Now))
		{
			Warn(FString::Printf(TEXT("eat %s"), *FoodId.ToString()));
		}
	}

	// Order board: today's offers, one of them taken.
	P->RefreshOrders();
	FName TakeId;
	for (const FFVOrder& O : P->Orders)
	{
		if (O.State == uint8(EFVOrderState::Offered))
		{
			TakeId = O.Id;
			break;
		}
	}
	if (!TakeId.IsNone())
	{
		P->TakeOrder(TakeId, Now);
	}

	UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST home: backpack %d/%d slots, chest %d/%d, keepnet %d/%d, %d nets, %d trophies, %d decor"),
		P->ContainerUsed(EFVContainer::Backpack), P->ContainerCapacity(EFVContainer::Backpack),
		P->ContainerUsed(EFVContainer::Chest), P->ContainerCapacity(EFVContainer::Chest),
		P->KeepnetUsed(), P->KeepnetCapacity(), P->Nets.Num(), P->Trophies.Num(), P->OwnedDecor.Num());
}

// ============================================================================================
// Update 3: touch gestures on the new UI (tap anywhere, swipeable tabs)
// ============================================================================================
FVector2D FVAutoTest::FindEmptyWorldPoint(const FVector2D& From) const
{
	// The result is a SCREEN point (it is tapped); From and the objects' Hit rects are WORLD px. Hotfix 0.61: the village
	// pond scrolls, so each screen point is tested (and measured) at its world point — identity on a one-screen place.
	const FVScene& Scene = Game->GetScene();
	const FFVRect Col = Game->IsHudColumnHidden() ? FFVRect() : Game->GetHudColumnRect();
	const float Top = FV::TopBarH + 20.f;
	const float Bottom = FV::VH - FV::BottomBarH - 20.f;
	FVector2D Best(FV::VW * 0.5f, (Top + Bottom) * 0.5f);
	float BestD = -1.f;
	for (float Y = Top; Y <= Bottom; Y += 10.f)
	{
		for (float X = 20.f; X <= FV::VW - 20.f; X += 10.f)
		{
			const FVector2D P(X, Y);
			if (Col.W > 0.f && Col.Contains(P))
			{
				continue;   // a tap on the HUD button column is a button press
			}
			const FVector2D W = Scene.ScreenToWorld(P);
			bool bNearObject = false;
			for (const FFVWorldObject& O : Scene.GetWorldObjects())
			{
				const FFVRect G(O.Hit.X - 16.f, O.Hit.Y - 16.f, O.Hit.W + 32.f, O.Hit.H + 32.f);
				if (G.Contains(W))
				{
					bNearObject = true;
					break;
				}
			}
			if (bNearObject)
			{
				continue;
			}
			const float Dist = float(FVector2D::Distance(W, From));
			if (Dist > BestD)
			{
				BestD = Dist;
				Best = P;
			}
		}
	}
	return Best;
}

void FVAutoTest::AddTouchUpdate3Steps()
{
	const FName Pond = FVDB::StartLocation();

	// ---- tap anywhere: standing at Mitrich's stall (it is highlighted), a short tap on empty ground opens his shop
	AddStep(TEXT("touch: tap anywhere uses the highlighted stall"),
		[this, Pond]()
		{
			bStepFlag = false;
			StepValue = 0;
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			Game->GetFishing().Reset();
			if (Game->GetScene().GetLocationId() != Pond)
			{
				Game->TravelTo(Pond);
			}
			TeleportToObject(EFVObjKind::Stall, FVDB::DefaultTraderId(), TEXT("stall"));
		},
		[this](float T)
		{
			FVScene& Scene = Game->GetScene();
			if (!bStepFlag && T > 0.3f)
			{
				// pick the tap point once the scene settled: empty ground, as far from the stall as possible
				bStepFlag = true;
				const FFVWorldObject* Near = Scene.NearestInReach();
				if (!Near || Near->Kind != EFVObjKind::Stall)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING touch tap-anywhere: the highlighted object is '%s', not the stall"), Near ? *Near->Label : TEXT("-"));
					++Warnings;
				}
				const FVector2D From = Near ? Near->Hit.Center() : Scene.GetPlayerPos();
				TouchTapPos = FindEmptyWorldPoint(From);   // a screen point; From is in world px
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST touch tap-anywhere at (%.0f, %.0f), %.0f px from '%s'"),
					TouchTapPos.X, TouchTapPos.Y, FVector2D::Distance(Scene.ScreenToWorld(TouchTapPos), From), Near ? *Near->Label : TEXT("-"));
			}
			// a short still tap: the finger rests for two frames (well under the hold-to-cast delay), then lifts
			if (bStepFlag && StepValue < 2)
			{
				FFVTouchPoint P;
				P.Id = 5;
				P.Pos = TouchTapPos;
				Synth.Touches.Add(P);
				++StepValue;
			}
			if (Game->GetScreen() == EFVScreen::Shop)
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST touch tap-anywhere ok (trader %s)"), *Game->GetScreenContext().ToString());
				return true;
			}
			if (T > 2.4f)
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING touch tap-anywhere: the shop did not open (screen %d, fishing %s)"),
					int32(Game->GetScreen()), FVAutoTestLocal::StateName(Game->GetFishing().GetState()));
				++Warnings;
				return true;
			}
			return false;
		},
		FString(), 0.1f, 3.f);   // (a check only: the stall step shot Mitrich's shop)

	// ---- swipe tabs: a quick horizontal finger swipe across the shop's content goes to the next tab (animated)
	AddStep(TEXT("touch: swipe the shop tabs"),
		[this]()
		{
			bStepFlag = false;   // a pager claimed the horizontal drag
			FVAutoTestLocal::SwipeStartPageChanges = FVKit::DebugPageChangeCount();
			DismissModals();
			if (Game->GetScreen() != EFVScreen::Shop)
			{
				Game->OpenScreenWith(EFVScreen::Shop, FVDB::DefaultTraderId());
			}
		},
		[this](float T)
		{
			const FFVRect Win = FV::WindowRect();
			const float Y = FMath::RoundToFloat(Win.Y + Win.H * 0.45f);
			const float X0 = Win.X + Win.W * 0.8f;
			const float X1 = Win.X + Win.W * 0.2f;
			const float T0 = 0.45f, Dur = 0.25f;
			if (T >= T0 && T <= T0 + Dur)
			{
				FFVTouchPoint P;
				P.Id = 6;
				P.Pos = FVector2D(FMath::RoundToFloat(FMath::Lerp(X0, X1, (T - T0) / Dur)), Y);
				Synth.Touches.Add(P);
			}
			// Horizontal drags are claimed only by pagers (scroll views take vertical ones): the tab strip follows it.
			const FVDraw& D = Game->DrawCtx;
			if (D.IsPointerDown() && D.IsDragGesture() && D.DragAxis() == FVDraw::EDragAxis::Horizontal && D.DragOwner() != 0)
			{
				bStepFlag = true;
			}
			if (T > T0 + Dur + 0.6f)
			{
				if (Game->GetScreen() != EFVScreen::Shop)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING touch swipe tabs: the shop closed during the swipe (screen %d)"), int32(Game->GetScreen()));
					++Warnings;
				}
				else if (bStepFlag && FVKit::DebugPageChangeCount() > FVAutoTestLocal::SwipeStartPageChanges)
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST touch swipe tabs ok (the swipe switched the tab)"));
				}
				else if (bStepFlag)
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING touch swipe tabs: a pager took the swipe but the tab did not change"));
					++Warnings;
				}
				else
				{
					UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING touch swipe tabs: no pager took the horizontal swipe at y=%.0f"), Y);
					++Warnings;
				}
				return true;
			}
			return false;
		},
		TEXT("touch_swipe_tabs"), 0.2f, 3.f);
}

// ============================================================================================
// Update 4b: the start-up gate, the practice pond and the first-fishing onboarding
// ============================================================================================
namespace FVAutoTestOnb
{
	const TCHAR* StartupName(EFVStartupState S)
	{
		switch (S)
		{
		case EFVStartupState::Idle: return TEXT("Idle");
		case EFVStartupState::Checking: return TEXT("Checking");
		case EFVStartupState::Ready: return TEXT("Ready");
		case EFVStartupState::UpdateRequired: return TEXT("UpdateRequired");
		case EFVStartupState::UpdateAvailable: return TEXT("UpdateAvailable");
		case EFVStartupState::NoConnection: return TEXT("NoConnection");
		case EFVStartupState::Banned: return TEXT("Banned");
		default: return TEXT("?");
		}
	}

	constexpr float PracticeForceBiteAfter = 2.f;   // the practice fish bites after 2-4 s; force it after this (a natural bite can still win)
	constexpr int32 ModalFramesBeforeDismiss = 45;   // a dialog blocking the practice (e.g. "try again") is closed
}

void FVAutoTest::AddGateSteps()
{
	if (!IsOnlineRun())
	{
		return;
	}
	// The main menu started the gate in its OnOpen: ping + version + (registered) the first sync. Wait for the answer.
	AddStep(TEXT("main menu: start-up gate"), nullptr,
		[this](float T)
		{
			const UFVOnline* O = Game->GetOnline();
			const EFVStartupState S = O ? O->GetStartupState() : EFVStartupState::Ready;
			return (S != EFVStartupState::Idle && S != EFVStartupState::Checking && T > 0.3f) || T > 9.f;
		},
		TEXT("main_menu_gate"), 0.5f, 10.f);
	AddStep(TEXT("main menu: gate result"),
		[this]()
		{
			const UFVOnline* O = Game->GetOnline();
			if (!O)
			{
				return;
			}
			const EFVStartupState S = O->GetStartupState();
			const FFVVersionInfo& V = O->GetVersionInfo();
			const FString Info = FString::Printf(TEXT("state %s, client build %d (v%s), server latest %d '%s', min %d, version known %s, offline mode %s"),
				FVAutoTestOnb::StartupName(S), UFVOnline::ClientBuild(), *UFVOnline::ClientVersionName(), V.LatestBuild, *V.LatestName,
				V.MinBuild, V.bKnown ? TEXT("yes") : TEXT("no"), O->IsOfflineMode() ? TEXT("yes") : TEXT("no"));
			if (S == EFVStartupState::Ready || S == EFVStartupState::UpdateAvailable)
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST gate ok (%s)"), *Info);
			}
			else
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING gate did not let the player in (%s)"), *Info);
				++Warnings;
			}
		},
		nullptr, FString(), 0.1f);
}

void FVAutoTest::OnboardingCheck(bool bOk, const FString& What)
{
	if (bOk)
	{
		++OnboardingOk;
		UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST onboarding %s: ok"), *What);
	}
	else
	{
		++OnboardingFailed;
		++Warnings;
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING onboarding %s: FAILED"), *What);
	}
}

bool FVAutoTest::TeleportToFishingSpot()
{
	FVScene& Scene = Game->GetScene();
	Scene.ClearWalkTarget();
	const FVector2D Start = Scene.GetPlayerPos();
	FVector2D Best = Start;
	float BestD2 = TNumericLimits<float>::Max();
	int32 Probes = 0;
	// Every walkable point of the canvas on a 5 px grid: the practice pond's edge is where CanFishFromHere() holds.
	// (update 6: a big world — the yard of the home — is scanned over its whole world rect on a coarser grid)
	const bool bBigScan = Scene.IsBigWorld();
	const FFVRect WB = Scene.GetWorldBounds();
	const float Step = bBigScan ? 8.f : 5.f;
	const float Y0 = bBigScan ? WB.Y + 4.f : FV::TopBarH + 6.f, Y1 = bBigScan ? WB.Bottom() - 4.f : FV::VH - 6.f;
	const float X0 = bBigScan ? WB.X + 4.f : 4.f, X1 = bBigScan ? WB.Right() - 4.f : FV::VW - 4.f;
	for (float Y = Y0; Y <= Y1; Y += Step)
	{
		for (float X = X0; X <= X1; X += Step)
		{
			const FVector2D P(X, Y);
			Scene.DebugTeleport(P);
			++Probes;
			const FVector2D At = Scene.GetPlayerPos();
			if (FVector2D::DistSquared(At, P) > 4.f || !Scene.CanFishFromHere())
			{
				continue;   // not walkable here (snapped elsewhere) or not at the water
			}
			const float D2 = FVector2D::DistSquared(At, Start);
			if (D2 < BestD2)
			{
				BestD2 = D2;
				Best = At;
			}
		}
	}
	const bool bFound = BestD2 < TNumericLimits<float>::Max();
	Scene.DebugTeleport(Best);
	UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST fishing spot in %s: %s (%.0f, %.0f), %.0f px from the start (%d probes)"),
		*Scene.GetLocationId().ToString(), bFound ? TEXT("found at") : TEXT("NONE, staying at"), Best.X, Best.Y,
		bFound ? FMath::Sqrt(BestD2) : 0.f, Probes);
	return bFound && Scene.CanFishFromHere();
}

void FVAutoTest::AddOnboardingSteps()
{
	// ---- the fresh profile: the yard, step 0 -> the first coach mark «Подойдите к учебному пруду»
	AddStep(TEXT("onboarding: fresh profile in the yard, coach mark 1"),
		[this]()
		{
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			ClearToasts();
			OnboardingOk = 0;
			OnboardingFailed = 0;
			PracticeEscapes = 0;
			bPracticeWasEscaped = false;
			bPracticeSpot = false;
			UFVSaveGame* P = Game->GetProfile();
			const bool bYard = Game->IsAtHome() && !Game->IsInHouse();
			OnboardingCheck(bYard, FString::Printf(TEXT("a new game starts in the yard (location %s)"), *Game->GetScene().GetLocationId().ToString()));
			if (!bYard)
			{
				Game->TravelTo(FVDB::HomeId());
			}
			OnboardingCheck(P && P->OnboardingStep >= 0 && P->OnboardingStep <= 1, FString::Printf(TEXT("a new game starts at the first onboarding step (step %d)"), P ? P->OnboardingStep : -1));
			if (P)
			{
				P->OnboardingStep = 0;   // the autotest forces the onboarding here (the rest of the run sets 100)
			}
			Game->SetWeather(EFVWeather::Sunny);
			Game->SetClockMinutes(9.f * 60.f);
		},
		[](float T) { return T > 0.6f; },
		TEXT("onboarding_1_coach"), 0.2f, 3.f);

	// ---- Esc still opens the pause menu while the coach marks are up (and closes it again)
	AddStep(TEXT("onboarding: Esc opens the pause menu"),
		[this]()
		{
			const UFVSaveGame* P = Game->GetProfile();
			const int32 Step = P ? P->OnboardingStep : -1;
			const bool bActive = FVOnboarding::IsActive(Game);
			OnboardingCheck(Step >= 1 && Step < 100 && bActive, FString::Printf(TEXT("coach marks shown (step %d, active %s)"),
				Step, bActive ? TEXT("yes") : TEXT("no")));
		},
		[this](float T)
		{
			if (T < 0.05f)
			{
				Synth.Pressed.Add(EKeys::Escape);
			}
			if (Game->GetScreen() == EFVScreen::Pause && T > 0.2f)
			{
				return true;
			}
			return T > 1.2f;
		},
		TEXT("screen_pause"), 0.2f, 2.f);   // (the pause menu's only shot)
	AddStep(TEXT("onboarding: back from the pause menu"),
		[this]()
		{
			OnboardingCheck(Game->GetScreen() == EFVScreen::Pause, FString::Printf(TEXT("Esc opens the pause menu during the onboarding (screen %d)"), int32(Game->GetScreen())));
			DismissModals();
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
		},
		[this](float) { return Game->GetScreen() == EFVScreen::None; }, FString(), 0.2f, 2.f);

	// ---- to the practice pond (teleport to its edge) -> «Зажмите и отпустите, чтобы забросить»
	AddStep(TEXT("onboarding: to the practice pond"),
		[this]()
		{
			if (UFVSaveGame* P = Game->GetProfile())
			{
				PracticeFishBefore = P->Stats.FishCaught;
				PracticeXpBefore = P->Xp;
				PracticeKeepnetBefore = P->KeepnetUsed();
			}
			bPracticeSpot = TeleportToFishingSpot();
			OnboardingCheck(bPracticeSpot, TEXT("the practice pond in the yard can be fished from its edge"));
		},
		[](float T) { return T > 0.5f; },
		TEXT("onboarding_2_pond"), 0.2f, 3.f);

	// ---- the bot casts -> «Ждите поклёвку…»
	AddStep(TEXT("onboarding: practice cast (bot)"),
		[this]()
		{
			if (!bPracticeSpot)
			{
				return;
			}
			const UFVSaveGame* P = Game->GetProfile();
			const int32 Step = P ? P->OnboardingStep : -1;
			OnboardingCheck(Step >= 2 && Step < 100, FString::Printf(TEXT("standing at the practice pond moves on to the cast hint (step %d)"), Step));
			FishStartTime = TotalTime;
			WaitingTime = 0.f;
			Game->GetFishing().SetAutoPlay(true);
		},
		[this](float T)
		{
			if (!bPracticeSpot)
			{
				return true;
			}
			const EFVFishState S = Game->GetFishing().GetState();
			return T > 0.3f && (S == EFVFishState::Waiting || S == EFVFishState::Bite || S == EFVFishState::Minigame);
		},
		TEXT("onboarding_3_wait"), 0.3f, 6.f);

	// ---- the practice fish bites -> the easy minigame «Держите рыбку в зелёной полосе…»
	AddStep(TEXT("onboarding: the practice minigame"),
		[this]()
		{
			WaitingTime = 0.f;
			if (!bPracticeSpot)
			{
				return;
			}
			const EFVFishState S = Game->GetFishing().GetState();
			OnboardingCheck(S == EFVFishState::Waiting || S == EFVFishState::Bite || S == EFVFishState::Minigame,
				FString::Printf(TEXT("a cast into the practice pond (state %s)"), FVAutoTestLocal::StateName(S)));
		},
		[this](float)
		{
			if (!bPracticeSpot)
			{
				return true;
			}
			FVFishing& F = Game->GetFishing();
			const EFVFishState S = F.GetState();
			if (S == EFVFishState::Waiting)
			{
				WaitingTime += LastDt;
				if (WaitingTime > FVAutoTestOnb::PracticeForceBiteAfter)
				{
					UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST forcing the practice bite"));
					F.DebugForceBite();
					WaitingTime = 0.f;
				}
			}
			return S == EFVFishState::Minigame && F.GetMinigame().Elapsed > 1.f;
		},
		TEXT("onboarding_4_minigame"), 0.4f, 15.f);

	// ---- 0.4.1: the practice fight cannot be lost — an empty meter keeps the fish on the hook and shows the [X]
	AddStep(TEXT("onboarding: an empty meter keeps the practice fish"),
		[this]()
		{
			StepValue = 0;
			if (bPracticeSpot)
			{
				Game->GetFishing().SetAutoPlay(false);   // nobody reels: the meter runs out
			}
		},
		[this](float T)
		{
			if (!bPracticeSpot)
			{
				return true;
			}
			FVFishing& F = Game->GetFishing();
			if (F.GetState() != EFVFishState::Minigame)
			{
				OnboardingCheck(false, FString::Printf(TEXT("the practice fight goes on with an empty meter (state %s)"), FVAutoTestLocal::StateName(F.GetState())));
				return true;
			}
			if (!F.GetMinigame().bEmptied)
			{
				// reel all the time: the bar rises to the top, away from the easy fish that idles near the bottom
				Synth.bActionDown = true;
				F.DebugDrainMeter();
				if (T > 10.f)
				{
					OnboardingCheck(false, TEXT("the practice meter runs empty"));
					return true;
				}
				return false;
			}
			if (++StepValue >= 30)   // half a second after it ran empty: still fighting, the [X] is there
			{
				const FFVRect X = F.GetGiveUpRect();
				OnboardingCheck(X.W > 0.f, FString::Printf(TEXT("an empty practice meter keeps the fish and shows the give-up [X] (%.0f px)"), X.W));
				return true;
			}
			return false;
		},
		TEXT("onboarding_4b_empty_meter"), 0.3f, 14.f);

	// ---- the [X] is pressed like a player does it (a finger, or the mouse: hover, press, release)
	AddStep(TEXT("onboarding: the [X] lets the practice fish go"),
		[this]() { StepValue = 0; },
		[this](float T)
		{
			if (!bPracticeSpot)
			{
				return true;
			}
			FVFishing& F = Game->GetFishing();
			const EFVFishState S = F.GetState();
			if (S == EFVFishState::Minigame)
			{
				const FFVRect X = F.GetGiveUpRect();
				if (X.W <= 0.f || ++StepValue > 40)
				{
					OnboardingCheck(false, TEXT("pressing the give-up [X] ends the practice fight"));
					return true;
				}
				if (FV::IsTouchUI())
				{
					if (StepValue <= 8)
					{
						FFVTouchPoint P;
						P.Id = 6;
						P.Pos = X.Center();
						Synth.Touches.Add(P);
					}
				}
				else
				{
					Synth.Mouse = X.Center();
					Synth.bMouseValid = true;
					Synth.bLmbPressed = StepValue == 3;
					Synth.bLmbDown = StepValue >= 3 && StepValue <= 8;
					Synth.bLmbReleased = StepValue == 9;
				}
				return false;
			}
			const UFVSaveGame* P = Game->GetProfile();
			const bool bLetGo = S == EFVFishState::Escaped || S == EFVFishState::Retrieving || S == EFVFishState::Idle;
			OnboardingCheck(bLetGo && P && P->OnboardingStep < 5,
				FString::Printf(TEXT("the give-up [X] lets the practice fish go (state %s, step %d)"), FVAutoTestLocal::StateName(S), P ? P->OnboardingStep : -1));
			return true;
		},
		TEXT("onboarding_4c_gave_up"), 0.2f, 6.f);

	// ---- and the next cast brings a new practice fish
	AddStep(TEXT("onboarding: cast again after letting it go"),
		[this]()
		{
			WaitingTime = 0.f;
			if (bPracticeSpot)
			{
				Game->GetFishing().SetAutoPlay(true);
			}
		},
		[this](float)
		{
			if (!bPracticeSpot)
			{
				return true;
			}
			if (Game->HasModal())
			{
				DismissModals();
			}
			FVFishing& F = Game->GetFishing();
			if (F.GetState() == EFVFishState::Waiting)
			{
				WaitingTime += LastDt;
				if (WaitingTime > FVAutoTestOnb::PracticeForceBiteAfter)
				{
					F.DebugForceBite();
					WaitingTime = 0.f;
				}
			}
			return F.GetState() == EFVFishState::Minigame && F.GetMinigame().Elapsed > 0.5f;
		},
		FString(), 0.2f, 20.f);

	// ---- the bot reels it in -> the success card «Отлично! Теперь вы умеете рыбачить» (step 5)
	AddStep(TEXT("onboarding: until the success card"),
		[this]()
		{
			WaitingTime = 0.f;
			StepValue = 0;
			if (bPracticeSpot)
			{
				OnboardingCheck(Game->GetFishing().GetState() == EFVFishState::Minigame, TEXT("the practice fish bites and the minigame starts"));
			}
		},
		[this](float)
		{
			if (!bPracticeSpot)
			{
				return true;
			}
			const UFVSaveGame* P = Game->GetProfile();
			FVFishing& F = Game->GetFishing();
			const EFVFishState S = F.GetState();
			const bool bEscaped = S == EFVFishState::Escaped;
			if (bEscaped && !bPracticeWasEscaped)
			{
				++PracticeEscapes;
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST the practice fish got away (%d), the bot tries again"), PracticeEscapes);
			}
			bPracticeWasEscaped = bEscaped;
			if ((P && P->OnboardingStep >= 5 && P->OnboardingStep < 100) || S == EFVFishState::Showcase)
			{
				F.SetAutoPlay(false);   // keep the card on screen for the shot
				return true;
			}
			// the practice reeling was seen above (the minigame shot, the empty meter): the meter starts near the top
			if (S == EFVFishState::Minigame && F.GetMinigame().Progress < FVAutoTestLocal::CatchBoostBelow)
			{
				SetFightProgress(FVAutoTestLocal::CatchBoost);
			}
			if (S == EFVFishState::Waiting)
			{
				WaitingTime += LastDt;
				if (WaitingTime > FVAutoTestOnb::PracticeForceBiteAfter)
				{
					F.DebugForceBite();
					WaitingTime = 0.f;
				}
			}
			// a dialog ("try again"...) would pause the world: close it so the bot can cast again
			StepValue = Game->HasModal() ? StepValue + 1 : 0;
			if (StepValue > FVAutoTestOnb::ModalFramesBeforeDismiss)
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST note: closing a dialog during the practice (step %d)"), P ? P->OnboardingStep : -1);
				DismissModals();
				StepValue = 0;
			}
			if (Game->GetScreen() != EFVScreen::None)
			{
				Game->CloseScreen();
			}
			return false;
		},
		TEXT("onboarding_5_success"), 0.6f, 30.f);

	// ---- the practice gave nothing real; step 6: «Настоящая рыба ждёт на Деревенском пруду: откройте Карту»
	AddStep(TEXT("onboarding: the map hint"),
		[this]()
		{
			FVFishing& F = Game->GetFishing();
			F.SetAutoPlay(false);
			UFVSaveGame* P = Game->GetProfile();
			if (bPracticeSpot && P)
			{
				OnboardingCheck(P->OnboardingStep >= 5 && P->OnboardingStep < 100,
					FString::Printf(TEXT("the practice catch shows the success card (step %d, state %s, %d escape(s))"),
						P->OnboardingStep, FVAutoTestLocal::StateName(F.GetState()), PracticeEscapes));
				const bool bClean = P->Stats.FishCaught == PracticeFishBefore && P->Xp == PracticeXpBefore && P->KeepnetUsed() == PracticeKeepnetBefore;
				OnboardingCheck(bClean, FString::Printf(TEXT("the practice fish gives no fish / XP / keepnet (fish %d -> %d, xp %d -> %d, keepnet %d -> %d)"),
					PracticeFishBefore, P->Stats.FishCaught, PracticeXpBefore, P->Xp, PracticeKeepnetBefore, P->KeepnetUsed()));
			}
			DismissModals();
			F.Reset();
			if (P && P->OnboardingStep < 6)
			{
				// the player would close the card himself; the script moves on to the map hint
				P->OnboardingStep = 6;
			}
			ClearToasts();
		},
		[](float T) { return T > 0.4f; },
		TEXT("onboarding_6_map"), 0.2f, 3.f);

	// ---- the first trip to the village pond finishes the onboarding
	AddStep(TEXT("onboarding: the trip to the village pond ends it"),
		[this]()
		{
			const UFVSaveGame* P = Game->GetProfile();
			const int32 Step = P ? P->OnboardingStep : -1;
			OnboardingCheck(Step == 6 && FVOnboarding::IsActive(Game), FString::Printf(TEXT("the map hint is shown (step %d)"), Step));
			const FName Pond = FVDB::StartLocation();
			if (!Game->TravelTo(Pond))
			{
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING travel to %s failed"), *Pond.ToString());
				++Warnings;
			}
		},
		[this](float T)
		{
			const UFVSaveGame* P = Game->GetProfile();
			return (P && P->OnboardingStep >= 100 && T > 0.2f) || T > 2.f;
		},
		FString(), 0.1f, 3.f);

	AddStep(TEXT("onboarding: summary"),
		[this]()
		{
			UFVSaveGame* P = Game->GetProfile();
			const int32 Step = P ? P->OnboardingStep : -1;
			OnboardingCheck(Step >= 100 && !FVOnboarding::IsActive(Game), FString::Printf(TEXT("done after arriving at the village pond (step %d)"), Step));
			if (OnboardingFailed == 0)
			{
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST onboarding practice pond -> village pond ok (%d checks)"), OnboardingOk);
			}
			else
			{
				// (each failed check was already counted as a warning)
				UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING onboarding: %d of %d checks failed"), OnboardingFailed, OnboardingOk + OnboardingFailed);
			}
			if (P)
			{
				P->OnboardingStep = 100;
			}
			Game->GetFishing().SetAutoPlay(false);
		},
		nullptr, FString(), 0.1f);
}
