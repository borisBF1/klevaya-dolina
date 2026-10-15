// Update 6 self-tests (-fvautotest), part of FVAutoTest (see FVAutoTest.h): Docs/UPDATE6_FOREST_CITY.md §0.2 (sleep
// limit), §5 (ECONOMY), §6.2 (seeds, ukha with carrot, orders without the garden), §10.14 (BALANCE_EXPERT), §12 (HUD
// column, repeated swipe, guide), §14 (0.5 save migration, camera, screenshots) and the integration hot-fixes (a trophy
// taken down goes back to the keepnet; the net menu shows the repair cost).
// Lines: "FV: AUTOTEST u6 <check>: ok" / "FV: AUTOTEST WARNING u6 <check>: FAILED", "FV: ECONOMY ..." (failures + a
// summary) and "FV: BALANCE_EXPERT ..." (a recorded table: the targets of §10.14 are proposals, so a row off target is a
// note; «сыр» >= 5 %, a universal fish with a mandatory rod type or a legend that cannot be learnt are failures).

#include "FVAutoTest.h"
#include "FVDatabase.h"
#include "FVExpertFight.h"
#include "FVGame.h"
#include "FVSaveGame.h"
#include "FVScene.h"
#include "FVWorldU6.h"
#include "FVArtYardFront.h"

#include "UObject/Package.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace FVAutoTestU6
{
	constexpr int32 SimTrials = 60;              // BALANCE_EXPERT fights per bot and cell (1D fights are cheap: noise ±6 p.p.)
	constexpr int32 CheeseTrials = 30;
	constexpr int32 LegendTrials = 60;
	constexpr float CheeseMax = 0.05f;           // §10.14: «сыр» < 5 %
	constexpr float UniversalSpreadMax = 0.20f;  // §10.14 (0.6 fix round): universal fish, rate spread between rod types <= 20 p.p. (30 fights: noise ±9 p.p.)

	FString Pct(float V) { return FString::Printf(TEXT("%.0f%%"), V * 100.f); }

	bool Overlaps(const FFVRect& A, const FFVRect& B)
	{
		return A.W > 0.f && A.H > 0.f && B.W > 0.f && B.H > 0.f
			&& A.X < B.Right() && B.X < A.Right() && A.Y < B.Bottom() && B.Y < A.Bottom();
	}

	const TCHAR* TypeSlug(EFVExpertRodType T)
	{
		switch (T)
		{
		case EFVExpertRodType::Pendulum: return TEXT("pendulum");
		case EFVExpertRodType::Twin: return TEXT("twin");
		case EFVExpertRodType::Compass: return TEXT("compass");
		case EFVExpertRodType::Magnet: return TEXT("magnet");
		case EFVExpertRodType::Helm: return TEXT("helm");
		default: return TEXT("?");
		}
	}

	const TCHAR* HazardSlug(int32 H)
	{
		switch (H)
		{
		case int32(EFVHazard::Current): return TEXT("current");
		case int32(EFVHazard::Whirlpool): return TEXT("whirlpool");
		case int32(EFVHazard::Wave): return TEXT("wave");
		case int32(EFVHazard::Piles): return TEXT("piles");
		case int32(EFVHazard::Weeds): return TEXT("weeds");
		case int32(EFVHazard::Darkness): return TEXT("darkness");
		case int32(EFVHazard::Drip): return TEXT("drip");
		case int32(EFVHazard::Spring): return TEXT("spring");
		default: return TEXT("-");
		}
	}

	/** The last (best) expert rod of the type in the table; nullptr if the type has no rod. */
	const FFVExpertRodDef* RodOfType(EFVExpertRodType T)
	{
		const FFVExpertRodDef* Out = nullptr;
		for (const FFVExpertRodDef& R : FVDBU6::ExpertRods())
		{
			if (R.Type == T) { Out = &R; }
		}
		return Out;
	}

	/** The first rod of the type (the one the rich profile fishes with in the screenshots). */
	const FFVExpertRodDef* FirstRodOfType(EFVExpertRodType T)
	{
		for (const FFVExpertRodDef& R : FVDBU6::ExpertRods())
		{
			if (R.Type == T) { return &R; }
		}
		return nullptr;
	}

	bool FitsType(const FFVExpertFishDef& D, EFVExpertRodType T)
	{
		return D.bUniversal || (D.TypeMask & uint8(1u << uint32(T))) != 0;
	}

	/** One BALANCE_EXPERT cell. */
	struct FCell
	{
		int32 Fish = 0;          // index into FVDBU6::ExpertFish()
		EFVExpertRodType Type = EFVExpertRodType::Pendulum;
		int32 Hazard = -1;       // EFVHazard or -1 = none
	};

	const TArray<FCell>& Cells()
	{
		static const TArray<FCell> Out = []()
		{
			TArray<FCell> C;
			const TArray<FFVExpertFishDef>& All = FVDBU6::ExpertFish();
			// 1) every ordinary fish x every rod type it bites on, no hazard (bots mid / strong / «сыр»)
			for (int32 i = 0; i < All.Num(); ++i)
			{
				if (All[i].Legend != EFVExpertLegend::None) { continue; }
				for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
				{
					if (FitsType(All[i], EFVExpertRodType(t)) && RodOfType(EFVExpertRodType(t))) { C.Add({ i, EFVExpertRodType(t), -1 }); }
				}
			}
			// 2) every ordinary fish x the hazards of its place (City: water hazards, Cave: darkness / drip / spring), on its
			//    first rod type (bot strong)
			for (int32 i = 0; i < All.Num(); ++i)
			{
				if (All[i].Legend != EFVExpertLegend::None) { continue; }
				int32 FirstType = -1;
				for (int32 t = 0; t < int32(EFVExpertRodType::Count) && FirstType < 0; ++t)
				{
					if (FitsType(All[i], EFVExpertRodType(t)) && RodOfType(EFVExpertRodType(t))) { FirstType = t; }
				}
				if (FirstType < 0) { continue; }
				const FFVFishDef* F = FVDB::FindFish(All[i].FishId);
				const bool bCave = F && F->Location == FVDBU6::CaveId();
				const int32 H0 = bCave ? int32(EFVHazard::Darkness) : int32(EFVHazard::Current);
				const int32 H1 = bCave ? int32(EFVHazard::Spring) : int32(EFVHazard::Weeds);
				for (int32 h = H0; h <= H1; ++h) { C.Add({ i, EFVExpertRodType(FirstType), h }); }
			}
			// 3) the legends on their own rod type («новичок» / «знаток»)
			for (int32 i = 0; i < All.Num(); ++i)
			{
				if (All[i].Legend == EFVExpertLegend::None) { continue; }
				int32 Type = 0;
				for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
				{
					if ((All[i].TypeMask & uint8(1u << uint32(t))) != 0) { Type = t; break; }
				}
				C.Add({ i, EFVExpertRodType(Type), -1 });
			}
			return C;
		}();
		return Out;
	}

	// ---- §19 (Р60) the 1D fight, pure logic: the control law of every rod type, the tricks of the fish, the hazards and
	// the legend scores (fixed seeds, no bots where the law is checked; the progress is pinned so the fight keeps going)
	struct FMechCheck { bool bOk; FString Text; };

	FFVExpertSetup MechSetup(EFVExpertRodType T, const FFVExpertFishDef& D)
	{
		FFVExpertSetup Su;
		if (const FFVExpertRodDef* R = RodOfType(T))
		{
			Su.Rod = *R;
			Su.Stats = FVDBU6::ComposeExpertStats(R->RodId, NAME_None, NAME_None, NAME_None);
		}
		Su.Fish = D;
		Su.Level = 13;
		Su.bNoSnap = true;
		return Su;
	}

	/** Steps a fight with the progress pinned at 0.5 (and the tension relaxed unless bKeepTension). */
	FFVExpertFight::EResult MechStep(FFVExpertFight& F, const FFVExpertInput& In, bool bKeepTension = false)
	{
		F.Progress = 0.5f;
		if (!bKeepTension) { F.Tension = 0.f; }
		return F.Step(In);
	}

	FFVExpertInput Held(bool bDown, bool bWasDown)
	{
		FFVExpertInput I;
		I.bDown = bDown;
		I.bPressed = bDown && !bWasDown;
		I.bReleased = !bDown && bWasDown;
		return I;
	}

	TArray<FMechCheck> ExpertMechanics()
	{
		TArray<FMechCheck> Out;
		auto Add = [&Out](bool bOk, const FString& Text) { Out.Add({ bOk, Text }); };
		const FFVExpertFishDef* Plain = nullptr;
		for (const FFVExpertFishDef& D : FVDBU6::ExpertFish())
		{
			if (D.Legend == EFVExpertLegend::None && D.bUniversal) { Plain = &D; break; }
		}
		if (!Plain) { Add(false, TEXT("1D mechanics: a universal expert fish")); return Out; }
		FFVExpertFishDef Calm = *Plain;
		Calm.Tricks.Reset();
		constexpr int32 Sec = 60;

		// ---- Маятник: a tap turns the gravity over, the bar never hangs still
		if (RodOfType(EFVExpertRodType::Pendulum))
		{
			FFVExpertFight F;
			F.Start(MechSetup(EFVExpertRodType::Pendulum, Calm), 101);
			int32 Still = 0;
			for (int32 i = 0; i < 3 * Sec; ++i) { MechStep(F, FFVExpertInput()); Still += FMath::Abs(F.Zones[0].Vel) < 5.f ? 1 : 0; }
			const bool bGravBefore = F.bGravityUp;
			FFVExpertInput Tap; Tap.bDown = Tap.bPressed = true;
			MechStep(F, Tap);
			const bool bFlipped = F.bGravityUp != bGravBefore;
			const float Y0 = F.Zones[0].Y;
			for (int32 i = 0; i < Sec / 2; ++i) { MechStep(F, FFVExpertInput()); Still += FMath::Abs(F.Zones[0].Vel) < 5.f ? 1 : 0; }
			Add(bFlipped && F.Zones[0].Y < Y0 - 20.f && Still < Sec / 3, FString::Printf(TEXT("1D Маятник: a tap flips the gravity (%d), the bar flies up %.0f -> %.0f, still %d of %d steps"),
				bFlipped ? 1 : 0, Y0, F.Zones[0].Y, Still, 3 * Sec + Sec / 2));
		}
		// ---- Двойчатка: held — the upper bar up, the lower one down; released — back
		if (RodOfType(EFVExpertRodType::Twin))
		{
			FFVExpertFight F;
			F.Start(MechSetup(EFVExpertRodType::Twin, Calm), 102);
			const float A0 = F.Zones[0].Y, B0 = F.Zones[1].Y;
			bool bWas = false;
			for (int32 i = 0; i < Sec; ++i) { MechStep(F, Held(true, bWas)); bWas = true; }
			const float A1 = F.Zones[0].Y, B1 = F.Zones[1].Y;
			for (int32 i = 0; i < Sec; ++i) { MechStep(F, Held(false, bWas)); bWas = false; }
			Add(F.NumZones == 2 && A1 < A0 - 40.f && B1 > B0 + 40.f && F.Zones[0].Y > A1 + 40.f && F.Zones[1].Y < B1 - 40.f,
				FString::Printf(TEXT("1D Двойчатка: two bars, held %.0f/%.0f -> %.0f/%.0f, released -> %.0f/%.0f"), A0, B0, A1, B1, F.Zones[0].Y, F.Zones[1].Y));
		}
		// ---- Импульс: a tap pushes the bar up, holding gives nothing, between taps it falls
		if (RodOfType(FVExpert::Impulse))
		{
			FFVExpertFight A, B;
			A.Start(MechSetup(FVExpert::Impulse, Calm), 103);
			B.Start(MechSetup(FVExpert::Impulse, Calm), 103);
			for (int32 i = 0; i < Sec / 2; ++i) { MechStep(A, FFVExpertInput()); MechStep(B, FFVExpertInput()); }
			FFVExpertInput Tap; Tap.bDown = Tap.bPressed = true;
			MechStep(A, Tap);
			MechStep(B, Tap);
			const float V = A.Zones[0].Vel;
			float MaxDiff = 0.f, MinY = A.Zones[0].Y;
			for (int32 i = 0; i < Sec * 2; ++i)
			{
				MechStep(A, Held(true, true));     // held
				MechStep(B, Held(false, i == 0));  // released
				MaxDiff = FMath::Max(MaxDiff, FMath::Abs(A.Zones[0].Y - B.Zones[0].Y));
				MinY = FMath::Min(MinY, A.Zones[0].Y);
			}
			Add(V < -100.f && MaxDiff < 0.5f && A.Zones[0].Y > MinY + 30.f, FString::Printf(TEXT("1D Импульс: a tap pushes up (%.0f u/s), holding = releasing (diff %.1f), then it falls %.0f -> %.0f"),
				V, MaxDiff, MinY, A.Zones[0].Y));
		}
		// ---- Магнит: held pulls the fish to the bar; a long pull heats the line
		if (RodOfType(EFVExpertRodType::Magnet))
		{
			FFVExpertFight A, B;
			A.Start(MechSetup(EFVExpertRodType::Magnet, Calm), 104);
			B.Start(MechSetup(EFVExpertRodType::Magnet, Calm), 104);
			float DA = 0.f, DB = 0.f;
			bool bWas = false;
			for (int32 i = 0; i < 4 * Sec; ++i)
			{
				MechStep(A, Held(true, bWas), true);
				MechStep(B, FFVExpertInput(), true);
				bWas = true;
				DA += FMath::Abs(A.FishPos - A.Zones[0].Y);
				DB += FMath::Abs(B.FishPos - B.Zones[0].Y);
			}
			Add(DA < DB * 0.8f && A.Tension > 0.05f && A.Tension > B.Tension, FString::Printf(TEXT("1D Магнит: the pull keeps the fish at the bar (mean gap %.0f vs %.0f free), 4 s of pull -> tension %.2f"),
				DA / (4 * Sec), DB / (4 * Sec), A.Tension));
		}
		// ---- Ползунок: the finger leads the bar
		if (RodOfType(FVExpert::Slider))
		{
			FFVExpertFight F;
			F.Start(MechSetup(FVExpert::Slider, Calm), 105);
			FFVExpertInput In; In.bDown = true; In.bPressed = true; In.TargetY = 200.f;
			for (int32 i = 0; i < Sec * 3 / 2; ++i) { MechStep(F, In); In.bPressed = false; }
			const float Y1 = F.Zones[0].Y;
			In.TargetY = 600.f;
			for (int32 i = 0; i < Sec * 3 / 2; ++i) { MechStep(F, In); }
			Add(FMath::Abs(Y1 - 200.f) < 30.f && FMath::Abs(F.Zones[0].Y - 600.f) < 30.f, FString::Printf(TEXT("1D Ползунок: the bar follows the finger (200 -> %.0f, 600 -> %.0f)"), Y1, F.Zones[0].Y));
		}

		// ---- the tricks (§19 «Приёмы рыб»): each shows its mark in a fight of the strong bot
		struct FTrick { EFVExpertPattern P; float Param; const TCHAR* Name; };
		const FTrick Tricks[] = { { EFVExpertPattern::Dash, 2.f, TEXT("Рывок") }, { EFVExpertPattern::Jump, 0.f, TEXT("Свеча") },
			{ EFVExpertPattern::Dive, 0.f, TEXT("Нырок") }, { EFVExpertPattern::Shuttle, 0.4f, TEXT("Челнок") },
			{ EFVExpertPattern::Decoy, 2.5f, TEXT("Обманка") }, { EFVExpertPattern::Hide, 0.f, TEXT("Прячется") } };
		for (const FTrick& Tr : Tricks)
		{
			FFVExpertFishDef D = Calm;
			FFVExpertMove M;
			M.Pattern = Tr.P; M.Seconds = 3.f; M.Speed = 1.2f; M.Amp = 1.f; M.Param = Tr.Param; M.Name = Tr.Name;
			D.Tricks = { M };
			FFVExpertFight F;
			F.Start(MechSetup(EFVExpertRodType::Pendulum, D), 200 + int32(Tr.P));
			bool bPlayed = false, bMark = false;
			float Lo = FVExpert::TrackH, Hi = 0.f, PrevV = 0.f;
			int32 Turns = 0;
			const FFVExpertBot Bot = FFVExpertBot::Strong();
			for (int32 i = 0; i < 20 * Sec; ++i)
			{
				MechStep(F, F.BotInput(Bot));
				if (F.MoveIndex < 0 || F.Pattern != Tr.P) { continue; }
				bPlayed = true;
				Lo = FMath::Min(Lo, F.FishPos);
				Hi = FMath::Max(Hi, F.FishPos);
				if (FMath::Abs(F.FishVel) > 30.f && PrevV * F.FishVel < 0.f) { ++Turns; }
				if (FMath::Abs(F.FishVel) > 30.f) { PrevV = F.FishVel; }
				switch (Tr.P)
				{
				case EFVExpertPattern::Dash: bMark |= F.bFishRed; break;
				case EFVExpertPattern::Jump: bMark |= F.bFishAirborne; break;
				case EFVExpertPattern::Dive: bMark |= F.bFrozen; break;
				case EFVExpertPattern::Decoy: bMark |= F.bGhost; break;
				case EFVExpertPattern::Hide: bMark |= F.bBubbles; break;
				default: bMark |= Turns >= 4; break;
				}
			}
			Add(bPlayed && bMark && F.SignalSerial > 0, FString::Printf(TEXT("1D trick «%s»: played %d, its mark %d (span %.0f..%.0f, turns %d), signals %d"),
				Tr.Name, bPlayed ? 1 : 0, bMark ? 1 : 0, Lo, Hi, Turns, F.SignalSerial));
		}

		// ---- the hazards (§19 «Стихии»): each acts on the bar (the finger leads it the same way in both fights) or,
		// darkness, hides the fish away from the bar
		for (int32 h = 0; h < int32(EFVHazard::Count); ++h)
		{
			FFVExpertSetup Su = MechSetup(FVExpert::Slider, Calm);
			const FFVExpertSetup Su0 = Su;
			Su.Hazards = { EFVHazard(h) };
			FFVExpertFight A, B;
			A.Start(Su, 300 + h);
			B.Start(Su0, 300 + h);
			float MaxDiff = 0.f;
			int32 Dark = 0;
			for (int32 i = 0; i < 16 * Sec; ++i)
			{
				FFVExpertInput In; In.bDown = true; In.bPressed = i == 0;
				In.TargetY = FVExpert::Mid + 230.f * FMath::Sin(float(i) / Sec * 2.f);
				MechStep(A, In);
				MechStep(B, In);
				MaxDiff = FMath::Max(MaxDiff, FMath::Abs(A.Zones[0].Y - B.Zones[0].Y));
				Dark += A.FishAlpha <= 0.f ? 1 : 0;
			}
			const bool bDarkness = EFVHazard(h) == EFVHazard::Darkness;
			Add(A.Hazards.Num() == 1 && (bDarkness ? Dark > 0 : MaxDiff > 4.f), FString::Printf(TEXT("1D hazard %s: acts (bar off the clean fight by %.0f, fish unseen %d steps)"),
				HazardSlug(h), MaxDiff, Dark));
		}

		// ---- the legends: the score plays in order (the same fight for the same seed), every move is announced by name
		for (const FFVExpertFishDef& D : FVDBU6::ExpertFish())
		{
			if (D.Legend == EFVExpertLegend::None) { continue; }
			EFVExpertRodType Type = EFVExpertRodType::Pendulum;
			for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
			{
				if ((D.TypeMask & uint8(1u << uint32(t))) != 0) { Type = EFVExpertRodType(t); break; }
			}
			FFVExpertSetup Su = MechSetup(Type, D);
			Su.Level = FV::ExpertMaxLevel;
			FFVExpertFight A, B;
			A.Start(Su, 400);
			B.Start(Su, 400);
			FFVExpertBot Bot = FFVExpertBot::Strong();
			Bot.bKnowsScore = true;
			float MaxDiff = 0.f;
			int32 Named = 0, LastSerial = 0;
			const int32 Moves = FVExpertLegends::Score(D.Legend).Num();
			for (int32 i = 0; i < 90 * Sec && A.ScoreReached < Moves; ++i)
			{
				MechStep(A, A.BotInput(Bot));
				MechStep(B, B.BotInput(Bot));
				MaxDiff = FMath::Max(MaxDiff, FMath::Abs(A.FishPos - B.FishPos));
				if (A.SignalSerial != LastSerial) { LastSerial = A.SignalSerial; Named += A.SignalName.IsEmpty() ? 0 : 1; }
			}
			Add(Moves > 0 && A.ScoreReached == Moves && MaxDiff < 0.01f && Named >= Moves,
				FString::Printf(TEXT("1D legend %s: the score %d/%d moves, %d named signals, same seed = same fight (diff %.2f)"),
					*D.FishId.ToString(), A.ScoreReached, Moves, Named, MaxDiff));
		}
		return Out;
	}

	/** Scratch profile for the pure rule checks (never saved). */
	UFVSaveGame* NewScratch()
	{
		UFVSaveGame* S = NewObject<UFVSaveGame>(GetTransientPackage());
		S->ResetToNewGame();
		return S;
	}

	// ---- 0.6 world checks (barriers, camera, reachability): the player is walked by ticking the scene directly — the
	// game's own movement code, no system input
	struct FWalkLog
	{
		float MaxCamStep = 0.f;   // the largest camera move of one tick (px)
		int32 Ticks = 0;
		bool bVoid = false;       // the camera showed past the edge of a world bigger than the view
		bool bLost = false;       // the player's body left the band between the HUD strip and the bottom dock
		bool bDone = false;       // the Done condition was met
	};

	FWalkLog SimWalk(FVScene& S, const FVector2D& Dir, float Seconds, float Dt, TFunctionRef<bool(const FVector2D&)> Done)
	{
		FWalkLog Log;
		FFVInput In;
		In.MoveX = float(Dir.X);
		In.MoveY = float(Dir.Y);
		const FFVRect B = S.GetWorldBounds();
		const float BandTop = FV::TopBarH, BandH = FMath::Max(40.f, FV::VH - FV::TopBarH - FV::BottomBarH);
		FVector2D PrevCam = S.GetCamera();
		for (float T = 0.f; T < Seconds; T += Dt)
		{
			S.Tick(Dt, In, true);
			++Log.Ticks;
			const FVector2D Cam = S.GetCamera();
			Log.MaxCamStep = FMath::Max(Log.MaxCamStep, float(FVector2D::Distance(Cam, PrevCam)));
			PrevCam = Cam;
			if (B.W > FV::VW && (Cam.X < B.X - 1.f || Cam.X + FV::VW > B.Right() + 1.f)) { Log.bVoid = true; }
			if (B.H > BandH && (Cam.Y + BandTop < B.Y - 1.f || Cam.Y + BandTop + BandH > B.Bottom() + 1.f)) { Log.bVoid = true; }
			const FVector2D Scr = S.GetPlayerPos() - Cam;
			if (Scr.X < 4.f || Scr.X > FV::VW - 4.f || Scr.Y - 56.f < BandTop - 2.f || Scr.Y > BandTop + BandH + 2.f) { Log.bLost = true; }
			if (Done(S.GetPlayerPos())) { Log.bDone = true; break; }
		}
		S.ClearPendingExit();
		return Log;
	}

	/** Cells reachable on foot from the player's feet (flood fill, Step px; a move also needs its midpoint walkable, so
	    thin blockers are not hopped). */
	struct FReach
	{
		FFVRect B;
		float Step = 6.f;
		int32 NX = 0, NY = 0;
		TBitArray<> Cells;
		bool At(const FVector2D& P, float Radius) const
		{
			const int32 R = FMath::CeilToInt(Radius / Step);
			const int32 CX = FMath::RoundToInt((float(P.X) - B.X) / Step), CY = FMath::RoundToInt((float(P.Y) - B.Y) / Step);
			for (int32 y = FMath::Max(0, CY - R); y <= FMath::Min(NY - 1, CY + R); ++y)
			{
				for (int32 x = FMath::Max(0, CX - R); x <= FMath::Min(NX - 1, CX + R); ++x)
				{
					if (Cells[y * NX + x] && FVector2D::Distance(FVector2D(B.X + x * Step, B.Y + y * Step), P) <= Radius) { return true; }
				}
			}
			return false;
		}
	};

	FReach FloodReach(const FVScene& S, const FVector2D& From)
	{
		FReach Out;
		Out.B = S.GetWorldBounds();
		Out.NX = FMath::Max(1, FMath::CeilToInt(Out.B.W / Out.Step) + 1);
		Out.NY = FMath::Max(1, FMath::CeilToInt(Out.B.H / Out.Step) + 1);
		Out.Cells.Init(false, Out.NX * Out.NY);
		auto Pt = [&Out](int32 X, int32 Y) { return FVector2D(Out.B.X + X * Out.Step, Out.B.Y + Y * Out.Step); };
		int32 SX = FMath::Clamp(FMath::RoundToInt((float(From.X) - Out.B.X) / Out.Step), 0, Out.NX - 1);
		int32 SY = FMath::Clamp(FMath::RoundToInt((float(From.Y) - Out.B.Y) / Out.Step), 0, Out.NY - 1);
		// (hotfix 0.61: the feet may stand a pixel off the grid cell they round to — at a blocker, the shore: the nearest
		// walkable cell around them starts the fill)
		for (int32 Ring = 1; Ring <= 2 && !S.DebugIsWalkable(Pt(SX, SY)); ++Ring)
		{
			for (int32 Dy = -Ring; Dy <= Ring; ++Dy)
			{
				for (int32 Dx = -Ring; Dx <= Ring; ++Dx)
				{
					const int32 X = FMath::Clamp(SX + Dx, 0, Out.NX - 1), Y = FMath::Clamp(SY + Dy, 0, Out.NY - 1);
					if (!S.DebugIsWalkable(Pt(SX, SY)) && S.DebugIsWalkable(Pt(X, Y))) { SX = X; SY = Y; }
				}
			}
		}
		if (!S.DebugIsWalkable(Pt(SX, SY))) { return Out; }
		TArray<int32> Queue;
		Queue.Add(SY * Out.NX + SX);
		Out.Cells[SY * Out.NX + SX] = true;
		const int32 DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 X = Queue[Head] % Out.NX, Y = Queue[Head] / Out.NX;
			for (int32 k = 0; k < 4; ++k)
			{
				const int32 X2 = X + DX[k], Y2 = Y + DY[k];
				if (X2 < 0 || Y2 < 0 || X2 >= Out.NX || Y2 >= Out.NY || Out.Cells[Y2 * Out.NX + X2]) { continue; }
				if (!S.DebugIsWalkable(Pt(X2, Y2)) || !S.DebugIsWalkable((Pt(X, Y) + Pt(X2, Y2)) * 0.5f)) { continue; }
				Out.Cells[Y2 * Out.NX + X2] = true;
				Queue.Add(Y2 * Out.NX + X2);
			}
		}
		return Out;
	}

	FVector2D Tex(float X, float Y) { return FVector2D(X * FV::WorldScale, Y * FV::WorldScale); }

	int32 KeepnetCount(const UFVSaveGame& P, FName Id)
	{
		int32 N = 0;
		for (const FFVInvStack& S : P.Keepnet)
		{
			if (S.ItemId == Id) { N += S.Count; }
		}
		return N;
	}

	/** Hot-fix «снять трофей» (UFVSaveGame::UnmountTrophy, added by the integration): called only if it exists, so this
	    file compiles before and after the fix; returns false when the method is missing. */
	template <typename TProfile>
	bool TryUnmountTrophy(TProfile& P, FName FishId, EFVBuyResult& OutResult)
	{
		if constexpr (requires { P.UnmountTrophy(FishId); })
		{
			OutResult = P.UnmountTrophy(FishId);
			return true;
		}
		else
		{
			return false;
		}
	}
}

void FVAutoTest::U6Check(bool bOk, const FString& What)
{
	if (bOk)
	{
		++U6Ok;
		UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST u6 %s: ok"), *What);
	}
	else
	{
		++U6Failed;
		++Warnings;
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING u6 %s: FAILED"), *What);
	}
}

// ============================================================================================
// Rich profile, update-6 part
// ============================================================================================
void FVAutoTest::GrantU6()
{
	UFVSaveGame* P = Game->GetProfile();
	if (!P)
	{
		return;
	}
	FFVSaveU6& U = P->U6;
	U.bVisitedCity = true;
	U.AxeLevel = FVDBU6::Axes().Num();
	U.PickaxeLevel = FVDBU6::Pickaxes().Num();
	U.bBlockageCleared = true;
	for (int32 i = 0; i < 3; ++i)
	{
		U.RubbleCleared.AddUnique(FName(*FString::Printf(TEXT("rubble_%d"), i)));
	}
	for (const FFVYardBuildingDef& B : FVDBU6::YardBuildings())
	{
		U.YardBuildings.Add(B.Id, FMath::Max(1, B.Levels.Num()));
	}
	U.BoatStage = FVDBU6::BoatStages().Num();
	U.Blueprints.AddUnique(FName(TEXT("bp_pickaxe")));
	P->UnlockedLocations.AddUnique(FVDBU6::ForestId());
	P->UnlockedLocations.AddUnique(FVDBU6::CityId());
	P->UnlockedLocations.AddUnique(FVDBU6::CaveId());

	// seeds, vegetables, expert tackle (one of each into the backpack; the first rod of each type gets a line / float / reel)
	const EFVItemKind Kinds[] = { EFVItemKind::Seed, EFVItemKind::Reel };
	for (const EFVItemKind K : Kinds)
	{
		for (const FFVItemDef* It : FVDB::ItemsOfKind(K))
		{
			if (It) { P->GiveItem(It->Id, K == EFVItemKind::Seed ? 6 : 1); }
		}
	}
	for (const FFVItemDef& It : FVDB::Items())
	{
		if (It.bExpert && (It.Kind == EFVItemKind::Line || It.Kind == EFVItemKind::Float)) { P->GiveItem(It.Id, 1); }
		if (It.Id.ToString().StartsWith(TEXT("veg_"))) { P->GiveItem(It.Id, 4); }
	}
	for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
	{
		const FFVExpertRodDef* R = FVAutoTestU6::FirstRodOfType(EFVExpertRodType(t));
		if (!R) { continue; }
		for (const FFVItemDef& It : FVDB::Items())
		{
			if (It.bExpert && (It.Kind == EFVItemKind::Line || It.Kind == EFVItemKind::Float || It.Kind == EFVItemKind::Reel) && P->CountItem(It.Id) > 0)
			{
				const FFVExpertLoadout L = P->GetExpertLoadout(R->RodId);
				const bool bFree = (It.Kind == EFVItemKind::Line && L.Line.IsNone()) || (It.Kind == EFVItemKind::Float && L.Float.IsNone())
					|| (It.Kind == EFVItemKind::Reel && L.Reel.IsNone());
				if (bFree) { P->InstallExpertTackle(R->RodId, It.Id); }
			}
		}
	}
	// one planted bed (the garden bed screen shows growth)
	if (P->GardenBeds() > 0 && P->CountItem(FName(TEXT("seed_carrot"))) > 0)
	{
		P->PlantSeed(0, FName(TEXT("seed_carrot")));
	}
	// integration: the rich backpack keeps free slots for the later steps (legend rigs, cooking, forging) — with the
	// Mill's rye the spare seeds and vegetables overflowed it; they move into the chest (or are dropped when it is full)
	{
		constexpr int32 KeepFree = 10;
		TArray<FFVInvStack>& Bp = P->GetContainer(EFVContainer::Backpack);
		const int32 Cap = P->ContainerCapacity(EFVContainer::Backpack);
		for (int32 Pass = 0; Pass < 2 && Cap - Bp.Num() < KeepFree; ++Pass)
		{
			for (int32 i = Bp.Num() - 1; i >= 0 && Cap - Bp.Num() < KeepFree; --i)
			{
				const FString S = Bp[i].ItemId.ToString();
				const bool bMove = Pass == 0 ? (S.StartsWith(TEXT("seed_")) && S != TEXT("seed_carrot")) : (S.StartsWith(TEXT("veg_")) && S != TEXT("veg_carrot"));
				if (!bMove) { continue; }
				const FFVInvStack St = Bp[i];
				P->AddTo(EFVContainer::Chest, St.ItemId, St.Count, St.Quality, St.Flags);   // (dropped when the chest is full)
				Bp.RemoveAt(i);
			}
		}
		UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST profile u6: backpack %d / %d stacks"), Bp.Num(), Cap);
	}
	UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST profile u6: level %d, axe %d, pickaxe %d, %d yard buildings, boat %d/%d, beds %d"),
		P->GetLevel(), U.AxeLevel, U.PickaxeLevel, U.YardBuildings.Num(), U.BoatStage, FVDBU6::BoatStages().Num(), P->GardenBeds());
}

// ============================================================================================
// Rule checks (one frame)
// ============================================================================================
void FVAutoTest::RunU6RuleChecks()
{
	using namespace FVAutoTestU6;

	// ---- big yard / house switched on (the integration default; -fvsmallyard is only the fallback)
	U6Check(FVWorldU6::IsBigWorldId(FVDB::HomeId()) && FVWorldU6::IsBigWorldId(FVDB::HouseId()),
		TEXT("the big yard and the big house are on (FVWorldU6::IsBigWorldId)"));

	// ---- §5 ECONOMY
	{
		auto MaxSell = [](FName Id) -> int32
		{
			int32 Best = 0;
			for (const FFVTraderDef& T : FVDB::Traders())
			{
				Best = FMath::Max(Best, FVDB::TraderSellPrice(T.Id, Id, EFVQuality::Normal, 0));
			}
			return Best;
		};
		auto Buyable = [](const FFVItemDef* It) { return It && It->BuyPrice > 0 && It->TraderMask != 0; };

		// 1. buy > sell for every item sold by a trader
		int32 Items = 0, Bad = 0;
		for (const FFVItemDef& It : FVDB::Items())
		{
			if (!Buyable(&It)) { continue; }
			++Items;
			const int32 Sell = MaxSell(It.Id);
			if (Sell >= It.BuyPrice)
			{
				++Bad;
				UE_LOG(LogFV, Warning, TEXT("FV: ECONOMY buy<=sell %s: buy %d, best sell %d"), *It.Id.ToString(), It.BuyPrice, Sell);
			}
		}
		U6Check(Bad == 0, FString::Printf(TEXT("ECONOMY: every bought item costs more than any trader pays (%d items, %d bad)"), Items, Bad));

		// 2. closed cycles: the cheapest way to get an input only from bought things (directly or by recipes of bought
		//    things); a recipe made only of them must not sell for more than its inputs
		TMap<FName, float> Memo;
		TSet<FName> Busy;
		TFunction<float(FName)> CostOf = [&](FName Id) -> float
		{
			if (const float* M = Memo.Find(Id)) { return *M; }
			if (Busy.Contains(Id)) { return TNumericLimits<float>::Max(); }
			Busy.Add(Id);
			float Best = TNumericLimits<float>::Max();
			const FFVItemDef* It = FVDB::FindItem(Id);
			if (Buyable(It)) { Best = float(It->BuyPrice); }
			if (const FFVBaitDef* B = FVDB::FindBait(Id))
			{
				if (B->TraderMask != 0 && B->PackPrice > 0 && B->PackSize > 0) { Best = FMath::Min(Best, float(B->PackPrice) / float(B->PackSize)); }
			}
			for (const FFVRecipeDef& R : FVDB::Recipes())
			{
				if (R.OutputId != Id || R.bRecycle || R.OutputCount <= 0) { continue; }
				float Sum = float(R.CoinCost);
				bool bOk = true;
				for (const FFVIngredient& In : R.Inputs)
				{
					const float C = CostOf(In.ItemId);
					if (C >= TNumericLimits<float>::Max()) { bOk = false; break; }
					Sum += C * float(FMath::Max(1, In.Count));
				}
				if (bOk) { Best = FMath::Min(Best, Sum / float(R.OutputCount)); }
			}
			Busy.Remove(Id);
			Memo.Add(Id, Best);
			return Best;
		};
		int32 Cycles = 0, BadCycles = 0;
		for (const FFVRecipeDef& R : FVDB::Recipes())
		{
			if (R.bRecycle || R.Inputs.Num() == 0) { continue; }
			float Cost = float(R.CoinCost);
			bool bAllBought = true;
			for (const FFVIngredient& In : R.Inputs)
			{
				const float C = CostOf(In.ItemId);
				if (C >= TNumericLimits<float>::Max()) { bAllBought = false; break; }
				Cost += C * float(FMath::Max(1, In.Count));
			}
			if (!bAllBought) { continue; }
			const int32 Sell = MaxSell(R.OutputId) * FMath::Max(1, R.OutputCount);
			if (Sell <= 0) { continue; }
			++Cycles;
			if (float(Sell) > Cost)
			{
				++BadCycles;
				UE_LOG(LogFV, Warning, TEXT("FV: ECONOMY cycle %s -> %s x%d: inputs %.0f, sells for %d"), *R.Id.ToString(), *R.OutputId.ToString(), R.OutputCount, Cost, Sell);
			}
		}
		U6Check(BadCycles == 0, FString::Printf(TEXT("ECONOMY: no recipe of bought inputs sells for more than them (%d such recipes, %d bad; rcp_float_sliding included)"), Cycles, BadCycles));

		// 3. seeds: the price is above the expected harvest (vegetables + returned seeds at the best price)
		int32 Seeds = 0, BadSeeds = 0;
		for (const FFVSeedDef& S : FVDBU6::Seeds())
		{
			const FFVItemDef* It = FVDB::FindItem(S.SeedId);
			if (!Buyable(It)) { continue; }
			++Seeds;
			const float Harvest = FVDBU6::ExpectedSeedHarvestValue(S);
			UE_LOG(LogFV, Log, TEXT("FV: ECONOMY seed %s: price %d, expected harvest %.1f"), *S.SeedId.ToString(), It->BuyPrice, Harvest);
			if (float(It->BuyPrice) <= Harvest) { ++BadSeeds; }
		}
		U6Check(Seeds > 0 && BadSeeds == 0, FString::Printf(TEXT("ECONOMY: every sold seed costs more than its expected harvest (%d seeds, %d bad)"), Seeds, BadSeeds));
	}

	// ---- Р75: every place lists its fish by rarity, the legend last (the update-6 fish were appended after it)
	{
		int32 Places = 0, BadPlaces = 0;
		FString BadList;
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			const TArray<const FFVFishDef*> Here = FVDB::FishAt(L.Id);
			if (Here.Num() == 0) { continue; }
			++Places;
			for (int32 i = 1; i < Here.Num(); ++i)
			{
				if (uint8(Here[i - 1]->Rarity) > uint8(Here[i]->Rarity)) { ++BadPlaces; BadList += L.Id.ToString() + TEXT(" "); break; }
			}
		}
		U6Check(Places > 0 && BadPlaces == 0, FString::Printf(TEXT("fish of every place listed by rarity (%d places, %d bad %s)"), Places, BadPlaces, *BadList));
	}

	// ---- Р74: the yard decor costs 10x the first price (10 000 – 90 000 at the builder, the boutique higher)
	{
		int32 Yard = 0, Cheap = 0;
		for (const FFVDecorDef& Dc : FVDB::Decor())
		{
			if (!FVDecor::IsYardSlot(Dc.Slot) || Dc.Price <= 0) { continue; }
			++Yard;
			if (Dc.Price < 10000) { ++Cheap; }
		}
		U6Check(Yard > 0 && Cheap == 0, FString::Printf(TEXT("ECONOMY: yard decor costs at least 10 000 (%d sold, %d cheaper)"), Yard, Cheap));
	}

	// ---- §6.2 (Р41) the ukha needs a carrot; the orders never ask for it before the garden (Р42)
	{
		const FFVRecipeDef* Soup = FVDB::FindRecipe(FName(TEXT("rcp_soup")));
		bool bCarrot = false;
		if (Soup)
		{
			for (const FFVIngredient& In : Soup->Inputs) { bCarrot |= In.ItemId == FName(TEXT("veg_carrot")) && In.Count >= 1; }
		}
		U6Check(Soup && bCarrot, TEXT("the ukha (rcp_soup) needs a carrot"));

		UFVSaveGame* S = NewScratch();
		S->AddXp(FMath::Max(0, FVDB::XpForLevel(FV::ClassicMaxLevel) - S->Xp) + 25);
		S->StationLevels.Add(FVDB::StationId(EFVStation::Kitchen), FMath::Max(1, FVDB::StationMaxLevel(EFVStation::Kitchen)));
		auto AsksGarden = [](const FFVOrder& O)
		{
			if (O.ItemId.ToString().StartsWith(TEXT("veg_"))) { return true; }
			for (const FFVRecipeDef& R : FVDB::Recipes())
			{
				if (R.OutputId != O.ItemId) { continue; }
				for (const FFVIngredient& In : R.Inputs)
				{
					if (In.ItemId.ToString().StartsWith(TEXT("veg_"))) { return true; }
				}
			}
			return false;
		};
		int32 Offers = 0, Garden = 0, SoupOffers = 0;
		for (int32 Day = 1; Day <= 120; ++Day)
		{
			S->Day = Day;
			S->Orders.Reset();
			S->RefreshOrders();
			for (const FFVOrder& O : S->Orders)
			{
				++Offers;
				if (AsksGarden(O)) { ++Garden; }
			}
		}
		U6Check(Offers > 0 && Garden == 0, FString::Printf(TEXT("orders without a garden never ask for the ukha / vegetables (%d offers in 120 days, %d bad)"), Offers, Garden));
		S->U6.YardBuildings.Add(FVDBU6::YardBuildingId(EFVYardBuilding::Garden), 1);
		for (int32 Day = 1; Day <= 120; ++Day)
		{
			S->Day = Day;
			S->Orders.Reset();
			S->RefreshOrders();
			for (const FFVOrder& O : S->Orders)
			{
				if (O.ItemId == FName(TEXT("food_soup"))) { ++SoupOffers; }
			}
		}
		UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST u6 note: with a garden the board asked for the ukha %d times in 120 days"), SoupOffers);
	}

	// ---- §14 migration of a 0.5 save: XP above level 10 -> «стаж», the net on the pier spot of the boat goes back
	{
		UFVSaveGame* S = NewScratch();
		const int32 Frozen = FVDB::XpForLevel(FV::ClassicMaxLevel);
		S->U6 = FFVSaveU6();                       // a 0.5 profile had no update-6 block at all
		S->Xp = Frozen + 5000;
		const TArray<const FFVItemDef*> Nets = FVDB::ItemsOfKind(EFVItemKind::Net);
		const FName OceanId(TEXT("ocean"));
		if (Nets.Num() > 0 && Nets[0])
		{
			FFVNetState N;
			N.Location = OceanId;
			N.Spot = 1;
			N.NetItem = Nets[0]->Id;
			N.Durability = Nets[0]->NetDurability;
			S->Nets.Add(N);
		}
		const int32 NetsBefore = Nets.Num() > 0 && Nets[0] ? S->CountItem(Nets[0]->Id) : 0;
		S->FixupAfterLoad();
		U6Check(S->U6.U6Version > 0, FString::Printf(TEXT("0.5 save: migrated (U6Version %d)"), S->U6.U6Version));
		U6Check(S->Xp == Frozen && S->U6.XpSeniority == 5000 && S->U6.XpEarned >= Frozen + 5000,
			FString::Printf(TEXT("0.5 save: XP %d -> %d (want %d), «стаж» %d (want 5000), earned %lld"), Frozen + 5000, S->Xp, Frozen, S->U6.XpSeniority, (long long)S->U6.XpEarned));
		U6Check(S->U6.SleepStreak == 0 && !S->U6.bVisitedCity, TEXT("0.5 save: sleep streak 0, the City not visited"));
		if (Nets.Num() > 0 && Nets[0])
		{
			U6Check(S->FindNet(OceanId, 1) == nullptr && S->CountItem(Nets[0]->Id) == NetsBefore + 1,
				FString::Printf(TEXT("0.5 save: the net on ocean:1 (the boat's place) is back in the backpack (%d -> %d)"), NetsBefore, S->CountItem(Nets[0]->Id)));
		}
	}

	// ---- hot-fix: a trophy taken down goes back to the keepnet
	{
		UFVSaveGame* S = NewScratch();
		const FFVFishDef* Fish = nullptr;
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (!F.bLegendary && !F.bExpert) { Fish = &F; break; }
		}
		if (Fish)
		{
			FFVTrophy T;
			T.FishId = Fish->Id;
			T.SizeCm = Fish->MaxSize;
			S->Trophies.Add(T);
			const int32 Before = KeepnetCount(*S, Fish->Id);
			EFVBuyResult R = EFVBuyResult::Invalid;
			if (TryUnmountTrophy(*S, Fish->Id, R))
			{
				const bool bGone = !S->Trophies.ContainsByPredicate([Fish](const FFVTrophy& X) { return X.FishId == Fish->Id; });
				U6Check(R == EFVBuyResult::Ok && bGone && KeepnetCount(*S, Fish->Id) == Before + 1,
					FString::Printf(TEXT("take a trophy down: %s -> %s, off the wall %d, keepnet %d -> %d"), *Fish->Id.ToString(),
						*UFVSaveGame::BuyResultText(R), bGone ? 1 : 0, Before, KeepnetCount(*S, Fish->Id)));
			}
			else
			{
				U6Check(false, TEXT("take a trophy down: UFVSaveGame::UnmountTrophy is missing (hot-fix not integrated)"));
			}
		}
	}

	// ---- hot-fix: the net menu shows the repair cost (the data it draws: a non-empty list of real items)
	{
		const UFVSaveGame* P = Game->GetProfile();
		if (P)
		{
			const TArray<FFVIngredient> Cost = P->NetRepairCost();
			bool bValid = Cost.Num() > 0;
			FString Text;
			for (const FFVIngredient& In : Cost)
			{
				bValid &= FVDB::FindItem(In.ItemId) != nullptr && In.Count > 0;
				Text += FString::Printf(TEXT("%s%s x%d"), Text.IsEmpty() ? TEXT("") : TEXT(", "), *In.ItemId.ToString(), In.Count);
			}
			const FFVNetState* Net = P->FindNet(FVDB::StartLocation(), 0);
			U6Check(bValid, FString::Printf(TEXT("net repair cost shown in the net menu: %s (the rich pond net: %s, durability %.0f)"), *Text,
				Net ? (Net->bTorn ? TEXT("torn") : TEXT("placed")) : TEXT("missing"), Net ? Net->Durability : 0.f));
		}
	}

	// ============================================================================================
	// 0.6.2 (Р79–Р84): expert rods — every rod obtainable, the gates need no expert rod of their own, the purchase lock,
	// перековка (scratch profiles only, never the player's)
	// ============================================================================================
	const FName AchCity(TEXT("ach_city_tasks"));
	const FName AchFacts(TEXT("ach_facts"));
	const FName AchSpecies(TEXT("ach_species_all"));

	// ---- 0.6.2 (Р79): a rod is the start rod, sold or forged; a rod given up to a recipe is sold again
	{
		int32 Rods = 0;
		FString Bad;
		for (const FFVRodDef& R : FVDB::Rods())
		{
			++Rods;
			if (R.Id != FVDB::StartRod() && !FVDBU6::IsRodSold(R) && !FVDBU6::RodRecipe(R.Id)) { Bad += TEXT(" ") + R.Id.ToString(); }
		}
		U6Check(Rods > 0 && Bad.IsEmpty(), FString::Printf(TEXT("0.6.2 rods: every rod is the start rod, sold or forged (%d rods%s%s)"),
			Rods, Bad.IsEmpty() ? TEXT("") : TEXT(", NO SOURCE:"), *Bad));

		int32 RodInputs = 0;
		FString BadIn;
		for (const FFVRecipeDef& Rc : FVDB::Recipes())
		{
			if (!FVDB::FindRod(Rc.OutputId)) { continue; }
			for (const FFVIngredient& In : Rc.Inputs)
			{
				const FFVRodDef* Given = FVDB::FindRod(In.ItemId);
				if (!Given) { continue; }
				++RodInputs;
				if (!FVDBU6::IsRodSold(*Given) || In.Count != 1) { BadIn += FString::Printf(TEXT(" %s<-%s"), *Rc.Id.ToString(), *In.ItemId.ToString()); }
			}
		}
		U6Check(RodInputs > 0 && BadIn.IsEmpty(), FString::Printf(TEXT("0.6.2 перековка: every rod given up to a recipe is sold again, one at a time (%d rod inputs%s%s)"),
			RodInputs, BadIn.IsEmpty() ? TEXT("") : TEXT(", BAD:"), *BadIn));

		int32 Locks = 0;
		FString BadAch;
		for (const FFVRodDef& R : FVDB::Rods())
		{
			if (R.RequiredAchievement.IsNone()) { continue; }
			++Locks;
			if (!FVDB::FindAchievement(R.RequiredAchievement)) { BadAch += TEXT(" ") + R.Id.ToString() + TEXT(":") + R.RequiredAchievement.ToString(); }
		}
		for (const FFVRecipeDef& Rc : FVDB::Recipes())
		{
			if (Rc.RequiredAchievement.IsNone()) { continue; }
			++Locks;
			if (!FVDB::FindAchievement(Rc.RequiredAchievement)) { BadAch += TEXT(" ") + Rc.Id.ToString() + TEXT(":") + Rc.RequiredAchievement.ToString(); }
		}
		U6Check(Locks > 0 && BadAch.IsEmpty(), FString::Printf(TEXT("0.6.2 locks: every required achievement exists (%d locks%s%s)"),
			Locks, BadAch.IsEmpty() ? TEXT("") : TEXT(", MISSING:"), *BadAch));

		// every recipe ingredient has a source: bought, crafted, caught, gathered, dug, chopped or found in a chest
		TSet<FName> Src;
		for (const FFVItemDef& It : FVDB::Items())
		{
			if ((It.BuyPrice > 0 && It.TraderMask != 0) || It.Kind == EFVItemKind::Resource) { Src.Add(It.Id); }
		}
		for (const FFVBaitDef& B : FVDB::Baits())
		{
			if (B.TraderMask != 0 && B.PackPrice > 0) { Src.Add(B.Id); }
		}
		for (const FFVRecipeDef& Rc : FVDB::Recipes()) { Src.Add(Rc.OutputId); }
		FRandomStream Rng(6202);
		auto AddLoot = [&Src](const TArray<FFVIngredient>& Loot) { for (const FFVIngredient& In : Loot) { Src.Add(In.ItemId); } };
		AddLoot(FVDBU6::BlockageLoot());
		for (int32 i = 0; i < 200; ++i)
		{
			for (int32 k = 0; k < int32(EFVTreeKind::Count); ++k) { AddLoot(FVDBU6::RollTreeLoot(EFVTreeKind(k), 2, Rng)); }
			AddLoot(FVDBU6::RollJunkPileLoot(Rng));
		}
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			for (int32 Day = 1; Day <= 40; ++Day)
			{
				for (int32 Slot = 0; Slot < 8; ++Slot)
				{
					int32 N = 0;
					const FName Id = FVDB::RollResource(L.Id, Day, Slot, N);
					if (!Id.IsNone()) { Src.Add(Id); }
				}
			}
			const bool bExpertZone = FVDBU6::IsExpertLocation(L.Id);
			for (int32 i = 0; i < 300; ++i)
			{
				int32 N = 0;
				const FName Dug = FVDB::RollDigLoot(L.Id, FMath::Max(1, FVDB::Shovels().Num()), Rng, N);
				if (!Dug.IsNone()) { Src.Add(Dug); }
				bool bPage = false;
				const FFVTreasure T = bExpertZone ? FVDBU6::RollExpertTreasure(L.Id, FV::MaxLevel, Rng, bPage) : FVDB::RollTreasure(L, FV::MaxLevel, Rng);
				if (!T.ItemId.IsNone()) { Src.Add(T.ItemId); }
				if (!T.BaitId.IsNone()) { Src.Add(T.BaitId); }
			}
		}
		// garden crops (§6.2), the mill's flour (§18) and the any-fish tokens (taken from the keepnet) are sources too
		for (const FFVSeedDef& Sd : FVDBU6::Seeds()) { Src.Add(Sd.CropId); }
		Src.Add(FVMill::FlourId());
		Src.Add(FVDB::AnyFishToken());
		Src.Add(FVDB::AnyFishRareToken());
		Src.Add(FVDB::AnyFishEpicToken());
		Src.Add(FVDB::AnyFishLegendToken());
		int32 Ingredients = 0;
		FString NoSrc;
		TSet<FName> Seen;
		for (const FFVRecipeDef& Rc : FVDB::Recipes())
		{
			for (const FFVIngredient& In : Rc.Inputs)
			{
				if (Seen.Contains(In.ItemId)) { continue; }
				Seen.Add(In.ItemId);
				++Ingredients;
				const FFVRodDef* R = FVDB::FindRod(In.ItemId);
				const bool bOk = R ? (R->Id == FVDB::StartRod() || FVDBU6::IsRodSold(*R) || FVDBU6::RodRecipe(R->Id) != nullptr)
					: (Src.Contains(In.ItemId) || FVDB::IsFishId(In.ItemId) || FVDB::IsJunkId(In.ItemId));
				if (!bOk) { NoSrc += TEXT(" ") + In.ItemId.ToString(); }
			}
		}
		U6Check(Ingredients > 0 && NoSrc.IsEmpty(), FString::Printf(TEXT("0.6.2 recipes: every ingredient has a source (%d ids%s%s)"),
			Ingredients, NoSrc.IsEmpty() ? TEXT("") : TEXT(", NO SOURCE:"), *NoSrc));
	}

	// ---- 0.6.2 (Р81, Р82): the gate achievements never need an expert rod of their own
	{
		const FName Gates[] = { AchCity, AchFacts, AchSpecies };
		for (const FName& G : Gates)
		{
			const FFVAchievementDef* A = FVDB::FindAchievement(G);
			const bool bExpertKind = A && (A->Kind == EFVAchKind::ExpertRodTypes || A->Kind == EFVAchKind::ExpertSpecies
				|| A->Kind == EFVAchKind::LegendsExpertAll || A->Kind == EFVAchKind::FlawlessExpertLegend);
			U6Check(A && !bExpertKind, FString::Printf(TEXT("0.6.2 gate %s: exists, its kind %d needs no expert rod"), *G.ToString(), A ? int32(A->Kind) : -1));
		}
		// «Ихтиолог» (the gate of «Рулевой»): the whole pre-expert collection of the valley — no fish of the City / Cave
		int32 Classic = 0, NeedRod = 0;
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (F.bExpert || FVDBU6::IsExpertLocation(F.Location)) { continue; }
			++Classic;
			if (FVDBU6::FindExpertFish(F.Id)) { ++NeedRod; }
		}
		const FFVAchievementDef* Sp = FVDB::FindAchievement(AchSpecies);
		U6Check(Sp && Sp->Kind == EFVAchKind::CatchSpecies && Classic > 0 && Sp->Target == Classic && NeedRod == 0,
			FString::Printf(TEXT("0.6.2 gate ach_species_all: CatchSpecies of the %d classic species (target %d, %d of them need an expert rod)"),
				Classic, Sp ? Sp->Target : -1, NeedRod));
	}

	// a scratch profile at a level: the City visited (XP not frozen), rich
	auto AtLevel = [](UFVSaveGame* S, int32 Level)
	{
		S->U6.bVisitedCity = true;
		S->Xp = FMath::Max(S->Xp, FVDB::XpForLevel(Level));
		S->Money = 5000000;
	};

	// ---- 0.6.2 (Р81): purchase lock — «Румб» after «Друг Города», «Полюс» after «Знаток повадок»; forged rods are not sold
	{
		struct FLock { const TCHAR* Rod; FName Ach; };
		const FLock RodLocks[] = { { TEXT("rod_rhumb"), AchCity }, { TEXT("rod_pole"), AchFacts } };
		for (const FLock& L : RodLocks)
		{
			UFVSaveGame* S = NewScratch();
			AtLevel(S, 15);
			const FName Rod(L.Rod);
			const EFVBuyResult Before = S->CanBuyRod(Rod);
			S->CompletedAchievements.AddUnique(L.Ach);
			const EFVBuyResult After = S->CanBuyRod(Rod);
			const EFVBuyResult Buy = S->BuyRod(Rod);
			U6Check(S->GetLevel() >= 15 && Before == EFVBuyResult::Locked && After == EFVBuyResult::Ok && Buy == EFVBuyResult::Ok
				&& S->OwnsRod(Rod) && S->EquippedRod == Rod,
				FString::Printf(TEXT("0.6.2 purchase lock %s: level %d, before %s, with %s %s, buy %s, owned %d, in the hands %s"), L.Rod, S->GetLevel(),
					*UFVSaveGame::BuyResultText(Before), *L.Ach.ToString(), *UFVSaveGame::BuyResultText(After), *UFVSaveGame::BuyResultText(Buy),
					S->OwnsRod(Rod) ? 1 : 0, *S->EquippedRod.ToString()));
		}
		UFVSaveGame* S = NewScratch();
		AtLevel(S, 15);
		const EFVBuyResult Surf = S->CanBuyRod(FName(TEXT("rod_surf")));
		U6Check(Surf == EFVBuyResult::NotForSale, FString::Printf(TEXT("0.6.2 purchase: rod_surf is not sold (%s)"), *UFVSaveGame::BuyResultText(Surf)));
	}

	// ---- 0.6.2 (Р80, Р82, Р83): перековка «Бриз» -> «Прибой» with an expert float on it; «Рулевой» behind «Ихтиолог»;
	// «Якорь» forged from tackle
	{
		auto GiveInputs = [](UFVSaveGame* S, const FFVRecipeDef& Rc)
		{
			for (const FFVIngredient& In : Rc.Inputs)
			{
				if (FVDB::FindRod(In.ItemId)) { S->OwnedRods.AddUnique(In.ItemId); }
				else { S->GiveItem(In.ItemId, In.Count); }
			}
		};
		const FName ForgeId = FVDBU6::YardBuildingId(EFVYardBuilding::Forge);
		const FFVRecipeDef* SurfRc = FVDB::FindRecipe(FName(TEXT("rcp_rod_surf")));
		const FFVRecipeDef* HelmRc = FVDB::FindRecipe(FName(TEXT("rcp_rod_helmsman")));
		const FFVRecipeDef* AnchorRc = FVDB::FindRecipe(FName(TEXT("rcp_rod_anchor")));
		U6Check(SurfRc && HelmRc && AnchorRc, TEXT("0.6.2 перековка: rcp_rod_surf, rcp_rod_helmsman and rcp_rod_anchor exist"));
		if (SurfRc && HelmRc && AnchorRc)
		{
			UFVSaveGame* S = NewScratch();
			AtLevel(S, 13);
			S->U6.YardBuildings.Add(ForgeId, 2);
			const FName Breeze(TEXT("rod_breeze"));
			const FName SurfRod(TEXT("rod_surf"));
			const FName Deep(TEXT("efloat_deep"));
			S->OwnedRods.AddUnique(Breeze);
			S->EquipRod(Breeze);
			S->GiveItem(Deep, 1);
			const EFVBuyResult Inst = S->InstallExpertTackle(Breeze, Deep);
			GiveInputs(S, *SurfRc);
			TMap<FName, int32> Before;
			for (const FFVIngredient& In : SurfRc->Inputs)
			{
				if (!FVDB::FindRod(In.ItemId)) { Before.Add(In.ItemId, S->CountItem(In.ItemId)); }
			}
			const int32 DeepBefore = S->CountItem(Deep);
			const int32 ForgingsBefore = S->U6.Forgings;
			const EFVBuyResult Can = S->CanCraft(*SurfRc);
			const EFVBuyResult Done = S->Craft(*SurfRc);
			bool bConsumed = true;
			for (const TPair<FName, int32>& B : Before)
			{
				int32 Need = 0;
				for (const FFVIngredient& In : SurfRc->Inputs) { if (In.ItemId == B.Key) { Need += In.Count; } }
				const int32 Back = B.Key == Deep ? 1 : 0;   // (the float of the given-up rod comes back)
				bConsumed &= S->CountItem(B.Key) == B.Value - Need + Back;
			}
			const EFVBuyResult Again = S->CanCraft(*SurfRc);
			U6Check(S->GetLevel() >= 13 && Inst == EFVBuyResult::Ok && Can == EFVBuyResult::Ok && Done == EFVBuyResult::Ok && S->OwnsRod(SurfRod)
				&& !S->OwnsRod(Breeze) && S->EquippedRod == SurfRod && S->CountItem(Deep) == DeepBefore + 1 && bConsumed
				&& S->U6.Forgings == ForgingsBefore + 1 && Again == EFVBuyResult::AlreadyOwned,
				FString::Printf(TEXT("0.6.2 перековка rod_breeze -> rod_surf: install %s, can %s, craft %s, owns surf %d / breeze %d, in the hands %s, efloat_deep %d -> %d, inputs consumed %d, forgings %d -> %d, again %s"),
					*UFVSaveGame::BuyResultText(Inst), *UFVSaveGame::BuyResultText(Can), *UFVSaveGame::BuyResultText(Done),
					S->OwnsRod(SurfRod) ? 1 : 0, S->OwnsRod(Breeze) ? 1 : 0, *S->EquippedRod.ToString(), DeepBefore, S->CountItem(Deep),
					bConsumed ? 1 : 0, ForgingsBefore, S->U6.Forgings, *UFVSaveGame::BuyResultText(Again)));

			UFVSaveGame* H = NewScratch();
			AtLevel(H, 15);
			H->U6.YardBuildings.Add(ForgeId, 2);
			GiveInputs(H, *HelmRc);
			const EFVBuyResult HLocked = H->CanCraft(*HelmRc);
			H->CompletedAchievements.AddUnique(AchSpecies);
			const EFVBuyResult HCan = H->CanCraft(*HelmRc);
			const EFVBuyResult HDone = H->Craft(*HelmRc);
			U6Check(HLocked == EFVBuyResult::Locked && HCan == EFVBuyResult::Ok && HDone == EFVBuyResult::Ok && H->OwnsRod(HelmRc->OutputId),
				FString::Printf(TEXT("0.6.2 rcp_rod_helmsman: without ach_species_all %s, with it %s, craft %s, owned %d"),
					*UFVSaveGame::BuyResultText(HLocked), *UFVSaveGame::BuyResultText(HCan), *UFVSaveGame::BuyResultText(HDone), H->OwnsRod(HelmRc->OutputId) ? 1 : 0));

			UFVSaveGame* A = NewScratch();
			AtLevel(A, 15);
			A->U6.YardBuildings.Add(ForgeId, 2);
			GiveInputs(A, *AnchorRc);
			const EFVBuyResult ACan = A->CanCraft(*AnchorRc);
			const EFVBuyResult ADone = A->Craft(*AnchorRc);
			U6Check(ACan == EFVBuyResult::Ok && ADone == EFVBuyResult::Ok && A->OwnsRod(AnchorRc->OutputId) && A->EquippedRod == AnchorRc->OutputId,
				FString::Printf(TEXT("0.6.2 rcp_rod_anchor (no rod input): can %s, craft %s, owned %d, in the hands %s"),
					*UFVSaveGame::BuyResultText(ACan), *UFVSaveGame::BuyResultText(ADone), A->OwnsRod(AnchorRc->OutputId) ? 1 : 0, *A->EquippedRod.ToString()));
		}
	}
}

// ============================================================================================
// Steps
// ============================================================================================
void FVAutoTest::AddUpdate6Steps()
{
	using namespace FVAutoTestU6;

	auto CloseAll = [this]()
	{
		DismissModals();
		if (Game->GetScreen() != EFVScreen::None)
		{
			Game->CloseScreen();
		}
		ClearToasts();
	};

	AddStep(TEXT("u6: rule checks"), [this]() { RunU6RuleChecks(); }, nullptr, FString(), 0.1f);

	// ---- §0.2 (Р44) sleep: two sleeps in a row before the evening, the third is refused; after 18:00 awake — allowed
	AddStep(TEXT("u6: the third sleep in a row before 18:00 is refused"),
		[this, CloseAll]()
		{
			CloseAll();
			UFVSaveGame* P = Game->GetProfile();
			if (!P) { return; }
			P->U6.SleepStreak = 0;
			int32 Buttons[3] = { 0, 0, 0 };
			for (int32 i = 0; i < 3; ++i)
			{
				DismissModals();
				Game->SetClockMinutes(10.f * 60.f);
				Game->ConfirmSleep();
				Buttons[i] = Game->HasModal() ? FMath::Max(1, Game->Modals[0].Buttons.Num()) : 0;
				if (i < 2 && Buttons[i] == 2)
				{
					Game->DismissModal(0);   // «Спать»
				}
			}
			U6Check(Buttons[0] == 2 && Buttons[1] == 2 && Buttons[2] == 1 && P->U6.SleepStreak == 2,
				FString::Printf(TEXT("sleep limit: 1st and 2nd sleep at 10:00 allowed, the 3rd refused (buttons %d / %d / %d, streak %d)"),
					Buttons[0], Buttons[1], Buttons[2], P->U6.SleepStreak));
		},
		[this](float T) { return T > 0.3f; }, TEXT("u6_sleep_refused"), 0.3f, 3.f);
	AddStep(TEXT("u6: after 18:00 awake the bed is allowed again"),
		[this]()
		{
			DismissModals();
			UFVSaveGame* P = Game->GetProfile();
			if (!P) { return; }
			Game->SetClockMinutes(10.f * 60.f);
			Game->AdvanceClock(8.5f * 60.f, false);   // awake until 18:30
			const int32 Streak = P->U6.SleepStreak;
			DismissModals();
			Game->ConfirmSleep();
			const int32 Buttons = Game->HasModal() ? FMath::Max(1, Game->Modals[0].Buttons.Num()) : 0;
			U6Check(Streak == 0 && Buttons == 2, FString::Printf(TEXT("sleep limit: at %s after waiting awake the streak is %d and the bed offers %d buttons"),
				*Game->GetClockText(), Streak, Buttons));
			DismissModals();   // «Не сейчас» (ESC = the last button)
			ClearToasts();
		},
		nullptr, FString(), 0.2f);

	// ---- §2 / §14 camera: every big location, the player at its top-left and bottom-right edges (teleport + walking into
	// the corner). The camera never shows outside the world when the world is bigger than the screen (no voids), a smaller
	// world is centred (black around it), and the player gets within half a screen of every edge.
	const FName BigLocs[] = { FVDBU6::ForestId(), FVDB::HomeId(), FVDB::HouseId(), FVDBU6::CityId(), FVDBU6::CaveId() };
	for (const FName Loc : BigLocs)
	{
		for (int32 Corner = 0; Corner < 2; ++Corner)
		{
			AddStep(FString::Printf(TEXT("u6 camera: %s %s"), *Loc.ToString(), Corner == 0 ? TEXT("top-left") : TEXT("bottom-right")),
				[this, CloseAll, Loc, Corner]()
				{
					CloseAll();
					Game->GetFishing().Reset();
					Game->SetWeather(EFVWeather::Sunny);
					Game->SetClockMinutes(12.f * 60.f);
					if (Game->GetScene().GetLocationId() != Loc && !Game->TravelTo(Loc))
					{
						U6Check(false, FString::Printf(TEXT("camera: travel to %s"), *Loc.ToString()));
						return;
					}
					FVScene& S = Game->GetScene();
					U6Check(S.IsBigWorld(), FString::Printf(TEXT("camera: %s is a big world"), *Loc.ToString()));
					const FFVRect B = S.GetWorldBounds();
					// the walkable spot nearest to the corner (corners are often trees / water; a blocked teleport falls back
					// to the spawn)
					const FVector2D CornerP = Corner == 0 ? FVector2D(B.X, B.Y) : FVector2D(B.Right(), B.Bottom());
					FVector2D BestQ = CornerP;
					float BestD = TNumericLimits<float>::Max();
					// keep away from the walk-through exits: standing on one (or walking into it) starts the travel fade
					// and the shot shows the darkening instead of the corner
					const float ExitPad = 72.f;
					auto NearExit = [&S, ExitPad](const FVector2D& Pt)
					{
						for (const FFVWorldObject& O : S.GetWorldObjects())
						{
							if (O.Kind == EFVObjKind::Exit && FFVRect(O.Hit.X - ExitPad, O.Hit.Y - ExitPad, O.Hit.W + 2.f * ExitPad, O.Hit.H + 2.f * ExitPad).Contains(Pt)) { return true; }
						}
						return false;
					};
					for (float GY = 0.f; GY <= B.H * 0.5f; GY += 40.f)
					{
						for (float GX = 0.f; GX <= B.W * 0.5f; GX += 40.f)
						{
							const FVector2D Q = Corner == 0 ? FVector2D(B.X + 2.f + GX, B.Y + 2.f + GY) : FVector2D(B.Right() - 2.f - GX, B.Bottom() - 2.f - GY);
							S.DebugTeleport(Q);
							if (NearExit(S.GetPlayerPos())) { continue; }
							const float D = float(FVector2D::Distance(S.GetPlayerPos(), CornerP));
							if (D < BestD) { BestD = D; BestQ = Q; }
						}
					}
					S.DebugTeleport(BestQ);
					S.ClearPendingExit();
					// the walk into the corner is skipped when an exit lies that way (StepValue = 1)
					const FVector2D WalkTo = S.GetPlayerPos() + (Corner == 0 ? FVector2D(-60.f, -60.f) : FVector2D(60.f, 60.f));
					StepValue = NearExit(WalkTo) ? 1 : 0;
					S.SnapCamera();
					// remember a tree / a boulder for the mini-games
					for (const FFVWorldObject& O : S.GetWorldObjects())
					{
						if (O.Kind == EFVObjKind::Tree && U6TreeCtx.IsNone()) { U6TreeCtx = FName(*FString::Printf(TEXT("%s:%s"), *Loc.ToString(), *O.Id.ToString())); }
						if (O.Kind == EFVObjKind::Boulder && U6BoulderCtx.IsNone()) { U6BoulderCtx = FName(*FString::Printf(TEXT("%s:%s"), *Loc.ToString(), *O.Id.ToString())); }
					}
				},
				[this, Loc, Corner](float T)
				{
					if (T < 0.9f && StepValue == 0)
					{
						Synth.MoveX = Corner == 0 ? -1.f : 1.f;
						Synth.MoveY = Corner == 0 ? -1.f : 1.f;
						return false;
					}
					if (T < 1.2f) { return false; }   // the camera eases after the walk
					FVScene& S = Game->GetScene();
					if (S.GetLocationId() != Loc) { return true; }
					const FFVRect B = S.GetWorldBounds();
					const FVector2D Cam = S.GetCamera();
					const FVector2D Pl = S.GetPlayerPos();
					// the camera keeps the world inside the band between the HUD strip and the bottom dock (X: the whole
					// screen); outside that band the HUD covers the screen
					auto AxisOk = [](float CamV, float Min, float Size, float BandStart, float View)
					{
						return Size >= View ? (CamV + BandStart >= Min - 1.f && CamV + BandStart + View <= Min + Size + 1.f)
							: FMath::Abs(CamV + BandStart - (Min - 0.5f * (View - Size))) <= 2.f;
					};
					const float BandH = FMath::Max(40.f, FV::VH - FV::TopBarH - FV::BottomBarH);
					const bool bClampX = AxisOk(float(Cam.X), B.X, B.W, 0.f, FV::VW);
					const bool bClampY = AxisOk(float(Cam.Y), B.Y, B.H, FV::TopBarH, BandH);
					U6Check(bClampX && bClampY, FString::Printf(TEXT("camera %s %s: camera (%.0f, %.0f) inside the world %.0fx%.0f at (%.0f, %.0f), screen %.0fx%.0f (no voids)"),
						*Loc.ToString(), Corner == 0 ? TEXT("top-left") : TEXT("bottom-right"), Cam.X, Cam.Y, B.W, B.H, B.X, B.Y, FV::VW, FV::VH));
					const float DX = Corner == 0 ? float(Pl.X) - B.X : B.Right() - float(Pl.X);
					const float DY = Corner == 0 ? float(Pl.Y) - B.Y : B.Bottom() - float(Pl.Y);
					// (or the camera leans to that edge of the world: the strip the player cannot walk to — the sea left
					// of the City embankment, the Cave lake — still comes into view)
					const float BandTopY = FV::TopBarH;
					const float CamMinX = B.X, CamMaxX = B.Right() - FV::VW;
					const float CamMinY = B.Y - BandTopY, CamMaxY = B.Bottom() - BandTopY - BandH;
					const bool bEdgeX = B.W <= FV::VW || FMath::Abs(float(Cam.X) - (Corner == 0 ? CamMinX : CamMaxX)) <= 2.f;
					bool bEdgeY = B.H <= BandH || FMath::Abs(float(Cam.Y) - (Corner == 0 ? CamMinY : CamMaxY)) <= 2.f;
					// (integration: the lean stops where the player would leave the band — under the top bar or the
					// button column; then the camera shows as much of that edge as it can with the whole body in view)
					const float FeetScrY = float(Pl.Y) - float(Cam.Y);
					const bool bBodyInBand = FeetScrY - 64.f >= BandTopY - 2.f && FeetScrY <= BandTopY + BandH + 2.f;
					bEdgeY = bEdgeY || (bBodyInBand && DY <= FV::VH - 40.f);
					U6Check((DX <= FV::VW * 0.5f + 64.f || bEdgeX) && (DY <= FV::VH * 0.5f + 64.f || bEdgeY), FString::Printf(TEXT("camera %s %s: the player reaches the edge (%.0f / %.0f px from it)"),
						*Loc.ToString(), Corner == 0 ? TEXT("top-left") : TEXT("bottom-right"), DX, DY));
					return true;
				},
				FString::Printf(TEXT("u6_camera_%s_%s"), *Loc.ToString(), Corner == 0 ? TEXT("tl") : TEXT("br")), 0.2f, 5.f);
		}
	}

	// ---- hotfix 0.61 (the owner: «таблички загораживают Митрича… расширить локацию по бокам»): the village pond stays a
	// classic place, 10 % of the screen wider on each side, and its camera scrolls sideways (X only). At the farthest spot
	// on each side the player reaches on foot (outside the exit triggers) the camera stops at the scroll limit
	// (GetWorldBounds = the scroll range; the right one includes the pad that keeps the HUD column off the new strip), the
	// player stands on the new strip, on the screen, in the band and off the HUD column, and the view never shows past the
	// world (no voids). Then no way sign stands over Mitrich's stall, the order board or another sign. Each orientation
	// checks its own composition (PortraitTest.ps1 -Both runs both).
	const FName PondId(TEXT("pond"));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const TCHAR* SideName = Side == 0 ? TEXT("left") : TEXT("right");
		AddStep(FString::Printf(TEXT("u6 pond wide: the far %s"), SideName),
			[this, CloseAll, PondId, Side, SideName]()
			{
				CloseAll();
				Game->GetFishing().Reset();
				Game->SetWeather(EFVWeather::Sunny);
				Game->SetClockMinutes(12.f * 60.f);
				FVScene& S = Game->GetScene();
				if (S.GetLocationId() != PondId && !Game->TravelTo(PondId))
				{
					U6Check(false, FString::Printf(TEXT("pond wide %s: travel to the pond"), SideName));
					return;
				}
				const FFVRect B = S.GetWorldBounds();
				U6Check(S.HasCamera() && !S.IsBigWorld() && B.W >= FV::VW * 1.2f - 1.f, FString::Printf(
					TEXT("pond wide %s: a classic place with a scroll camera (%s), the scroll range %.0f..%.0f = %.0f px for the %.0f px screen (>= 120 %%)"),
					SideName, S.HasCamera() && !S.IsBigWorld() ? TEXT("yes") : TEXT("NO"), B.X, B.Right(), B.W, FV::VW));
				// the exit triggers, padded: standing in one starts the travel on the next tick
				TArray<FFVRect> Triggers;
				for (const FFVWorldObject& O : S.GetWorldObjects())
				{
					FFVRect Hit;
					FVector2D Feet = FVector2D::ZeroVector, Sign = FVector2D::ZeroVector;
					if (O.Kind == EFVObjKind::Exit && S.DebugFindExit(O.Id, Hit, Feet, Sign))
					{
						Triggers.Add(FFVRect(Hit.X - 16.f, Hit.Y - 16.f, Hit.W + 32.f, Hit.H + 32.f));
					}
				}
				// the farthest reachable cell on that side (ties: the row nearest to where the player stood); no fill from
				// the feet (they stand off the walk area) -> any walkable cell, the report says which
				const FVector2D From = S.GetPlayerPos();
				const FReach R = FloodReach(S, From);
				const bool bReach = R.Cells.Find(true) != INDEX_NONE;
				bool bFound = false;
				FVector2D Best = From;
				for (int32 y = 0; y < R.NY; ++y)
				{
					for (int32 x = 0; x < R.NX; ++x)
					{
						const FVector2D Q(R.B.X + x * R.Step, R.B.Y + y * R.Step);
						if (bReach ? !R.Cells[y * R.NX + x] : !S.DebugIsWalkable(Q)) { continue; }
						if (Triggers.ContainsByPredicate([&Q](const FFVRect& Tr) { return Tr.Contains(Q); })) { continue; }
						const bool bFarther = Side == 0 ? Q.X < Best.X - 0.5f : Q.X > Best.X + 0.5f;
						const bool bSameX = FMath::Abs(Q.X - Best.X) <= 0.5f;
						if (!bFound || bFarther || (bSameX && FMath::Abs(Q.Y - From.Y) < FMath::Abs(Best.Y - From.Y)))
						{
							Best = Q;
							bFound = true;
						}
					}
				}
				S.DebugTeleport(Best);
				S.ClearPendingExit();
				S.SnapCamera();
				const FVector2D Pl = S.GetPlayerPos();
				U6Check(bFound && (Side == 0 ? Pl.X < 0.f : Pl.X > FV::VW), FString::Printf(
					TEXT("pond wide %s: the player walks onto the new strip — the farthest spot %s (%.0f, %.0f) lies past the old screen 0..%.0f"),
					SideName, bReach ? TEXT("reached on foot") : TEXT("(walkable; NO fill from the feet)"), Pl.X, Pl.Y, FV::VW));
			},
			[this, PondId, Side, SideName](float T)
			{
				if (T < 0.4f) { return false; }   // the game ticks meanwhile: the snapped camera has to stay put
				FVScene& S = Game->GetScene();
				if (S.GetLocationId() != PondId)
				{
					U6Check(false, FString::Printf(TEXT("pond wide %s: the player stays at the pond (now %s: an exit fired)"), SideName, *S.GetLocationId().ToString()));
					return true;
				}
				const FFVRect B = S.GetWorldBounds();
				const FVector2D Cam = S.GetCamera(), Pl = S.GetPlayerPos();
				const float Want = Side == 0 ? B.X : B.Right() - FV::VW;
				const bool bLimit = FMath::Abs(float(Cam.X) - Want) <= 1.f && FMath::Abs(float(Cam.Y)) <= 0.5f;
				const bool bNoVoid = Cam.X >= B.X - 1.f && Cam.X + FV::VW <= B.Right() + 1.f && Cam.Y >= B.Y - 1.f && Cam.Y + FV::VH <= B.Bottom() + 1.f;
				U6Check(bLimit && bNoVoid, FString::Printf(TEXT("pond wide %s: the camera (%.0f, %.0f) stops at the scroll limit x %.0f (range %.0f..%.0f, view %.0f px), %s"),
					SideName, Cam.X, Cam.Y, Want, B.X, B.Right(), FV::VW, bNoVoid ? TEXT("no voids") : TEXT("VOIDS shown")));
				const FVector2D Scr = S.WorldToScreen(Pl);
				const bool bOnScreen = Scr.X >= 8.f && Scr.X <= FV::VW - 8.f && Scr.Y - 56.f >= FV::TopBarH - 2.f && Scr.Y <= FV::VH - FV::BottomBarH + 2.f;
				const FFVRect Col = Game->IsHudColumnHidden() ? FFVRect() : Game->GetHudColumnRect();
				const bool bUnderHud = Col.W > 0.f && Overlaps(Col, FFVRect(float(Scr.X) - 10.f, float(Scr.Y) - 60.f, 20.f, 60.f));
				U6Check(bOnScreen && !bUnderHud, FString::Printf(TEXT("pond wide %s: the player (%.0f, %.0f) is %s at (%.0f, %.0f), %s"),
					SideName, Pl.X, Pl.Y, bOnScreen ? TEXT("on the screen") : TEXT("OFF the screen / the band"), Scr.X, Scr.Y,
					bUnderHud ? TEXT("UNDER the HUD column") : TEXT("off the HUD column")));
				return true;
			},
			FString::Printf(TEXT("u6_pond_wide_%s"), SideName), 0.2f, 4.f);
	}
	AddStep(TEXT("u6 pond wide: the way signs keep off Mitrich and the order board"), [this, CloseAll, PondId]()
		{
			CloseAll();
			Game->GetFishing().Reset();
			FVScene& S = Game->GetScene();
			if (S.GetLocationId() != PondId && !Game->TravelTo(PondId))
			{
				U6Check(false, TEXT("pond signs: travel to the pond"));
				return;
			}
			S.RefreshWorldObjects();
			const FFVWorldObject* Stall = nullptr;
			const FFVWorldObject* Board = nullptr;
			TArray<const FFVWorldObject*> Signs;
			for (const FFVWorldObject& O : S.GetWorldObjects())
			{
				if (O.Kind == EFVObjKind::Stall && O.Id == FVDB::DefaultTraderId()) { Stall = &O; }
				else if (O.Kind == EFVObjKind::OrderBoard) { Board = &O; }
				else if (O.Kind == EFVObjKind::Exit) { Signs.Add(&O); }
			}
			FString Missing, Bad;
			for (const FName To : { FName(TEXT("river")), FName(TEXT("ocean")), FVDB::HomeId() })
			{
				if (!Signs.ContainsByPredicate([To](const FFVWorldObject* O) { return O->Id == To; })) { Missing += FString::Printf(TEXT(" %s"), *To.ToString()); }
			}
			auto RectText = [](const FFVRect& Rc) { return FString::Printf(TEXT("%.0f..%.0f x %.0f..%.0f"), Rc.X, Rc.Right(), Rc.Y, Rc.Bottom()); };
			for (int32 i = 0; i < Signs.Num(); ++i)
			{
				const FFVWorldObject& A = *Signs[i];
				if (Stall && Overlaps(A.Hit, Stall->Hit)) { Bad += FString::Printf(TEXT(" «%s» (%s) over Mitrich's stall;"), *A.Label, *RectText(A.Hit)); }
				if (Board && Overlaps(A.Hit, Board->Hit)) { Bad += FString::Printf(TEXT(" «%s» (%s) over the order board;"), *A.Label, *RectText(A.Hit)); }
				for (int32 j = i + 1; j < Signs.Num(); ++j)
				{
					if (Overlaps(A.Hit, Signs[j]->Hit)) { Bad += FString::Printf(TEXT(" «%s» over «%s»;"), *A.Label, *Signs[j]->Label); }
				}
			}
			U6Check(Stall && Board && Missing.IsEmpty() && Bad.IsEmpty(), FString::Printf(
				TEXT("pond signs (%s): the %d way signs keep off Mitrich's stall (%s), the order board (%s) and each other%s%s"),
				FV::IsPortrait() ? TEXT("portrait") : TEXT("landscape"), Signs.Num(), Stall ? *RectText(Stall->Hit) : TEXT("NONE"), Board ? *RectText(Board->Hit) : TEXT("NONE"),
				Missing.IsEmpty() ? TEXT("") : *(TEXT(": NO sign to") + Missing), Bad.IsEmpty() ? TEXT("") : *(TEXT(":") + Bad)));
		}, nullptr, FString(), 0.1f);

	// ---- §12.1 the HUD button column never covers what the player uses (classic locations: stall, board, resources, net
	// and dig spots, way signs). Hotfix 0.61: a classic place wider than the screen (the village pond) scrolls sideways,
	// so every object is checked at the camera the player has standing at it (teleport + snap, then back to where he
	// stood); a one-screen place keeps its one camera (0, 0) — the check there is unchanged.
	for (const FFVLocationDef& L : FVDB::Locations())
	{
		if (L.bBigWorld || L.bExpertZone) { continue; }
		const FName Loc = L.Id;
		AddStep(FString::Printf(TEXT("u6 HUD column vs objects: %s"), *Loc.ToString()),
			[this, CloseAll, Loc]()
			{
				CloseAll();
				Game->GetFishing().Reset();
				if (Game->GetScene().GetLocationId() != Loc) { Game->TravelTo(Loc); }
				Game->SetClockMinutes(12.f * 60.f);
			},
			[this, Loc](float T)
			{
				if (T < 0.3f) { return false; }
				const FFVRect Col = Game->GetHudColumnRect();
				FString Hit;
				FVScene& S = Game->GetScene();
				const bool bScroll = S.HasCamera() && !S.IsBigWorld();
				const FVector2D Back = S.GetPlayerPos();
				// (a copy: the teleports must not touch the array being walked)
				const TArray<FFVWorldObject> Objs = S.GetWorldObjects();
				for (const FFVWorldObject& O : Objs)
				{
					if (bScroll)
					{
						S.DebugTeleport(O.Feet);
						S.SnapCamera();
					}
					const FVector2D P = S.WorldToScreen(FVector2D(O.Hit.X, O.Hit.Y));
					if (Overlaps(Col, FFVRect(P.X, P.Y, O.Hit.W, O.Hit.H)))
					{
						Hit += bScroll ? FString::Printf(TEXT(" «%s» (camera x %.0f)"), *O.Label, S.GetCamera().X) : FString::Printf(TEXT(" «%s»"), *O.Label);
					}
				}
				if (bScroll)
				{
					// back before the next tick: a teleport onto an exit's edge would start the travel
					S.DebugTeleport(Back);
					S.ClearPendingExit();
					S.SnapCamera();
				}
				U6Check(Col.W > 0.f && Hit.IsEmpty(), FString::Printf(TEXT("HUD column (%.0f, %.0f %.0fx%.0f) covers nothing at %s%s%s"),
					Col.X, Col.Y, Col.W, Col.H, *Loc.ToString(), bScroll ? TEXT(" (scrolls: each object at the camera of the player standing at it)") : TEXT(""),
					Hit.IsEmpty() ? TEXT("") : *(TEXT(": covers") + Hit)));
				return true;
			},
			FString(), 0.1f, 3.f);
	}

	// ---- the new screens (both orientations: PortraitTest.ps1 -Both runs portrait and landscape)
	AddScreenShotStepWith(EFVScreen::Shop, FName(TEXT("tr_forester")), TEXT("u6_shop_forester"));
	AddScreenShotStepWith(EFVScreen::Shop, FName(TEXT("tr_city_tackle")), TEXT("u6_shop_city_tackle"));
	AddScreenShotStepWith(EFVScreen::Shop, FName(TEXT("tr_gardener")), TEXT("u6_shop_gardener"));
	AddScreenShotStepWith(EFVScreen::Shop, FName(TEXT("tr_boutique")), TEXT("u6_shop_boutique"));
	AddScreenShotStepWith(EFVScreen::Builder, FName(TEXT("tr_builder")), TEXT("u6_screen_builder"));
	// Р61: the try-on of decor (a real fragment of the yard / house with the item at its place) in every decor shop:
	// the builder (roof = the whole facade, fence = a tiled stretch, furniture), the boutique, the house decor catalog
	AddScreenShotStepWith(EFVScreen::Builder, FName(TEXT("decor_yard_roof_copper")), TEXT("u6_prev_builder_roof"));
	AddScreenShotStepWith(EFVScreen::Builder, FName(TEXT("decor_yard_fence_picket")), TEXT("u6_prev_builder_fence"));
	AddScreenShotStepWith(EFVScreen::Builder, FName(TEXT("decor_furn_fireplace")), TEXT("u6_prev_builder_furniture"));
	AddScreenShotStepWith(EFVScreen::Shop, FName(TEXT("tr_boutique:decor_yard_fountain")), TEXT("u6_prev_boutique"));
	AddScreenShotStepWith(EFVScreen::HomeDecor, FName(TEXT("decor_lamp_kerosene")), TEXT("u6_prev_decor_catalog"));
	// Р61 fix: a bought fence stands BEHIND the cottage and the apple trees (the background props in front of the fence
	// line are redrawn y-sorted): the yard with the picket fence from the builder, the player in front of the porch
	AddStep(TEXT("u6 yard: the bought fence stays behind the cottage"),
		[this, CloseAll]()
		{
			CloseAll();
			Game->GetFishing().Reset();
			Game->SetWeather(EFVWeather::Sunny);
			Game->SetClockMinutes(12.f * 60.f);
			const FName Fence(TEXT("decor_yard_fence_picket"));
			if (UFVSaveGame* P = Game->GetProfile())
			{
				P->GrantDecor(Fence);
				U6Check(P->EquipDecor(Fence), TEXT("yard fence: the picket fence is equipped"));
				Game->NotifyProfileChanged();
			}
			if (Game->GetScene().GetLocationId() != FVDB::HomeId() && !Game->TravelTo(FVDB::HomeId()))
			{
				U6Check(false, TEXT("yard fence: travel to the yard"));
				return;
			}
			Game->GetScene().DebugTeleport(FVector2D(400.f, 150.f) * FV::WorldScale);
			U6Check(FVArtYardFront::Parts().Num() >= 3 && FVArtYardFront::CottageRects().Num() == 3,
				FString::Printf(TEXT("yard depth: the props in front of the back fence are redrawn y-sorted (%d parts)"), FVArtYardFront::Parts().Num()));
		},
		nullptr, TEXT("u6_yard_fence"), 0.6f);
	AddScreenShotStepWith(EFVScreen::YardBuilding, FVDBU6::YardBuildingId(EFVYardBuilding::Sawmill), TEXT("u6_screen_sawmill"));
	AddScreenShotStepWith(EFVScreen::YardBuilding, FVDBU6::YardBuildingId(EFVYardBuilding::Forge), TEXT("u6_screen_forge"));
	AddScreenShotStepWith(EFVScreen::GardenBed, FName(TEXT("bed_0")), TEXT("u6_screen_garden_bed"));
	AddScreenShotStepWith(EFVScreen::Boat, NAME_None, TEXT("u6_screen_boat"));
	AddScreenShotStepWith(EFVScreen::CityBoard, NAME_None, TEXT("u6_screen_city_board"));
	AddScreenShotStepWith(EFVScreen::Artel, NAME_None, TEXT("u6_screen_artel"));
	// 0.6.2 (Р81–Р84): the City tackle shop with the locked rods (lock + progress) and the forge (level 2) with the rod
	// recipes — the rich profile owns every rod and may have the gates done: they are taken away for the two shots and
	// given back after (the later steps fight with the expert rods)
	{
		struct FRodShotState { TArray<FName> Rods; TArray<FName> Achs; FName Equipped; int64 Money = 0; int32 Forge = 0; bool bForge = false; };
		TSharedPtr<FRodShotState> Saved = MakeShared<FRodShotState>();
		AddStep(TEXT("u62 rods: the 0.6.2 rods and their gates taken away for the shots"),
			[this, Saved]()
			{
				UFVSaveGame* P = Game->GetProfile();
				if (!P) { return; }
				Saved->Rods = P->OwnedRods;
				Saved->Achs = P->CompletedAchievements;
				Saved->Equipped = P->EquippedRod;
				Saved->Money = P->Money;
				const FName ForgeId = FVDBU6::YardBuildingId(EFVYardBuilding::Forge);
				const int32* Lv = P->U6.YardBuildings.Find(ForgeId);
				Saved->bForge = Lv != nullptr;
				Saved->Forge = Lv ? *Lv : 0;
				P->U6.YardBuildings.Add(ForgeId, FMath::Max(2, Saved->Forge));
				for (const FFVRodDef& R : FVDB::Rods())
				{
					if (!R.RequiredAchievement.IsNone() || FVDBU6::RodRecipe(R.Id)) { P->OwnedRods.Remove(R.Id); }
					const FName Gate = FVDBU6::RodRequiredAchievement(R);
					if (!Gate.IsNone()) { P->CompletedAchievements.Remove(Gate); }
				}
				if (!P->OwnsRod(P->EquippedRod)) { P->EquipRod(FVDB::StartRod()); }
				P->Money = FMath::Max<int64>(P->Money, 1000000);
				U6Check(P->GetLevel() >= 15 && P->GetYardLevel(EFVYardBuilding::Forge) >= 2
					&& P->CanBuyRod(FName(TEXT("rod_rhumb"))) == EFVBuyResult::Locked,
					FString::Printf(TEXT("u62 rods shots: level %d, forge %d, rod_rhumb %s"), P->GetLevel(), P->GetYardLevel(EFVYardBuilding::Forge),
						*UFVSaveGame::BuyResultText(P->CanBuyRod(FName(TEXT("rod_rhumb"))))));
			},
			nullptr, FString(), 0.1f);
		AddScreenShotStepWith(EFVScreen::Shop, FName(TEXT("tr_city_tackle")), TEXT("u62_shop_rods"));
		AddScreenShotStepWith(EFVScreen::YardBuilding, FVDBU6::YardBuildingId(EFVYardBuilding::Forge), TEXT("u62_forge_rods"));
		AddStep(TEXT("u62 rods: the rods and achievements given back"),
			[this, Saved]()
			{
				UFVSaveGame* P = Game->GetProfile();
				if (!P || Saved->Rods.Num() == 0) { return; }
				P->OwnedRods = Saved->Rods;
				P->CompletedAchievements = Saved->Achs;
				P->Money = Saved->Money;
				const FName ForgeId = FVDBU6::YardBuildingId(EFVYardBuilding::Forge);
				if (Saved->bForge) { P->U6.YardBuildings.Add(ForgeId, Saved->Forge); }
				else { P->U6.YardBuildings.Remove(ForgeId); }
				P->EquipRod(Saved->Equipped);
			},
			nullptr, FString(), 0.1f);
	}
	if (const FFVExpertRodDef* R = FirstRodOfType(EFVExpertRodType::Pendulum))
	{
		AddScreenShotStepWith(EFVScreen::ExpertTackle, R->RodId, TEXT("u6_screen_expert_tackle"));
	}
	AddScreenShotStep(EFVScreen::Map, TEXT("u6_screen_map"));
	AddScreenShotStep(EFVScreen::Leaderboard, TEXT("u6_screen_leaderboard"));
	// Р77: a tap opens the tile of a row whose text does not fit (the context "fv_expand" opens the first such row)
	AddScreenShotStepWith(EFVScreen::Inventory, FName(TEXT("fv_expand")), TEXT("u6_expand_inventory"));
	AddScreenShotStepWith(EFVScreen::Achievements, FName(TEXT("fv_expand")), TEXT("u6_expand_achievements"));
	AddScreenShotStepWith(EFVScreen::Builder, FName(TEXT("fv_expand")), TEXT("u6_expand_builder"));
	const TCHAR* GuideEntries[] = { TEXT("g_bigmap"), TEXT("g_forest"), TEXT("g_yard"), TEXT("g_garden"), TEXT("g_expert"), TEXT("g_expert_rods"), TEXT("g_hazards"), TEXT("g_city") };
	for (const TCHAR* E : GuideEntries)
	{
		AddScreenShotStepWith(EFVScreen::Tutorial, FName(E), FString::Printf(TEXT("u6_tutorial_%s"), E));
	}
	AddStep(TEXT("u6: back to the world"), [CloseAll]() { CloseAll(); }, nullptr, FString(), 0.1f);

	// ---- §18 «Старая мельница»: the forest footbridge (broken / repaired), the mill by repair stage, the dam, the
	// pen, the miller, the mill / pen screens, the miller's shop and the fog morning (visual check, both orientations)
	struct FMillShot { FName Loc; FName Obj; bool bBridge; int32 Stage; int32 Beavers; const TCHAR* Shot; };
	const FMillShot MillShots[] = {
		{ FVDBU6::ForestId(), FVMill::ObjBridgeId(), false, 0, 0, TEXT("u6_mill_bridge_broken") },
		{ FVDBU6::ForestId(), FVMill::ObjBridgeId(), true,  0, 0, TEXT("u6_mill_bridge_fixed") },
		{ FVMill::MillId(),   FVMill::ObjMillId(),   true,  0, 0, TEXT("u6_mill_stage0") },
		{ FVMill::MillId(),   FVMill::ObjMillId(),   true,  2, 2, TEXT("u6_mill_stage2") },
		{ FVMill::MillId(),   FVMill::ObjMillId(),   true,  3, FVMill::BeaverFeedsNeeded, TEXT("u6_mill_working") },
		{ FVMill::MillId(),   FVMill::ObjBeaverId(), true,  3, FVMill::BeaverFeedsNeeded, TEXT("u6_mill_dam_omut") },
		{ FVMill::MillId(),   FVMill::ObjPenId(),    true,  3, FVMill::BeaverFeedsNeeded, TEXT("u6_mill_pen") },
		{ FVMill::MillId(),   FVMill::MillerNpcId(), true, 3, FVMill::BeaverFeedsNeeded, TEXT("u6_mill_miller") },
	};
	for (const FMillShot& M : MillShots)
	{
		AddStep(FString::Printf(TEXT("u6 mill: %s"), M.Shot),
			[this, CloseAll, M]()
			{
				CloseAll();
				Game->GetFishing().Reset();
				Game->SetWeather(EFVWeather::Sunny);
				Game->SetClockMinutes(12.f * 60.f);
				UFVSaveGame* P = Game->GetProfile();
				if (!P) { return; }
				P->U6.Mill.bBridgeRepaired = M.bBridge;
				P->U6.Mill.Stage = M.Stage;
				P->U6.Mill.BeaverFeeds = M.Beavers;
				P->U6.Mill.DeckUntil.SetNum(FVMill::FeedDecks);
				P->U6.Mill.DeckUntil[0] = M.Stage >= 3 ? Game->GetAbsMinutes() + 600.0 : 0.0;
				P->U6.Mill.Pen.Reset();
				if (M.Stage >= 3)
				{
					FFVMillPenSlot Slot;
					Slot.Fish = FName(TEXT("fish_ide"));
					Slot.Until = Game->GetAbsMinutes() + FVMill::PenGameMinutes;
					P->U6.Mill.Pen.Add(Slot);
				}
				if (FVMill::IsMillId(M.Loc)) { P->UnlockLocation(M.Loc); }
				FVScene& S = Game->GetScene();
				if (S.GetLocationId() != M.Loc && !Game->TravelTo(M.Loc))
				{
					U6Check(false, FString::Printf(TEXT("mill shots: travel to %s"), *M.Loc.ToString()));
					return;
				}
				S.RefreshWorldObjects();
				bool bFound = false;
				for (const FFVWorldObject& O : S.GetWorldObjects())
				{
					if (O.Id == M.Obj || (M.Obj == FVMill::MillerNpcId() && O.Id == FVMill::MillerTraderId()))
					{
						// (the miller: stand beside him, not on him, so the shot shows his sprite)
						S.DebugTeleport(O.Feet + (M.Obj == FVMill::MillerNpcId() ? FVector2D(56.f, 36.f) : FVector2D::ZeroVector));
						bFound = true;
						break;
					}
				}
				U6Check(bFound, FString::Printf(TEXT("mill shots: object %s at %s"), *M.Obj.ToString(), *M.Loc.ToString()));
				S.ClearPendingExit();
				S.SnapCamera();
			},
			[](float T) { return T > 0.8f; },
			M.Shot, 0.2f, 5.f);
	}
	AddScreenShotStepWith(EFVScreen::Mill, FName(TEXT("mill")), TEXT("u6_mill_screen_mill"));
	AddScreenShotStepWith(EFVScreen::Mill, FName(TEXT("pen")), TEXT("u6_mill_screen_pen"));
	AddScreenShotStepWith(EFVScreen::Shop, FVMill::MillerTraderId(), TEXT("u6_mill_shop_miller"));
	AddStep(TEXT("u6 mill: fog morning"),
		[this, CloseAll]() { CloseAll(); Game->SetWeather(EFVWeather::Sunny); Game->SetClockMinutes(7.f * 60.f); },
		[](float T) { return T > 0.8f; }, TEXT("u6_mill_fog_morning"), 0.2f, 5.f);
	AddStep(TEXT("u6 mill: back to the world"), [this, CloseAll]() { CloseAll(); Game->SetClockMinutes(12.f * 60.f); }, nullptr, FString(), 0.1f);

	AddU6WorldSteps(CloseAll);

	// ---- the new mini-games (their bots play them and take minigame_<slug>_play / _result shots); the contexts come
	// from the camera steps (a real tree / boulder) and the tables
	auto AddU6MiniGame = [this, CloseAll](EFVScreen Screen, TFunction<FName()> Ctx, const FString& Slug)
	{
		AddStep(FString::Printf(TEXT("u6 mini-game %s: the bot plays it"), *Slug),
			[this, CloseAll, Screen, Ctx, Slug]()
			{
				CloseAll();
				const FName C = Ctx();
				Game->OpenScreenWith(Screen, C);
				U6Check(Game->GetScreen() == Screen, FString::Printf(TEXT("mini-game %s opens (%s)"), *Slug, *C.ToString()));
			},
			[this, Screen](float) { return Game->GetScreen() != Screen; },
			FString(), 0.2f, 14.f);
		AddStep(FString::Printf(TEXT("u6 mini-game %s: back"), *Slug), [CloseAll]() { CloseAll(); }, nullptr, FString(), 0.1f);
	};
	AddU6MiniGame(EFVScreen::Chopping, [this]() { return U6TreeCtx.IsNone() ? FName(TEXT("forest:blockage")) : U6TreeCtx; }, TEXT("chopping"));
	AddU6MiniGame(EFVScreen::Sawing, []() { return FName(NAME_None); }, TEXT("sawing"));
	// the forge bot needs a forge and the materials of its recipe (otherwise the screen only says «Не хватает материалов»);
	// anything still missing is a WARNING
	AddStep(TEXT("u6 mini-game forging: materials for the bot"),
		[this]()
		{
			UFVSaveGame* P = Game->GetProfile();
			const FFVRecipeDef* R = nullptr;
			for (const FFVRecipeDef* It : FVDBU6::RecipesForYard(EFVYardBuilding::Forge))
			{
				if (It && It->bForgeGame) { R = It; break; }
			}
			if (!P || !R)
			{
				U6Check(false, TEXT("forging: a forge recipe with the mini-game"));
				return;
			}
			int32& YL = P->U6.YardBuildings.FindOrAdd(FVDBU6::YardBuildingId(EFVYardBuilding::Forge));
			YL = FMath::Max(YL, FMath::Max(1, R->StationLevel));
			for (const FFVIngredient& In : R->Inputs)
			{
				const int32 Have = P->CountIngredient(In.ItemId);
				if (Have < In.Count * 2) { P->GiveItem(In.ItemId, In.Count * 2 - Have); }
			}
			const EFVBuyResult Can = P->CanCraft(*R, 1);
			U6Check(Can == EFVBuyResult::Ok, FString::Printf(TEXT("forging: the bot can forge %s (%s)"), *R->Id.ToString(),
				Can == EFVBuyResult::Ok ? TEXT("ok") : *UFVSaveGame::BuyResultText(Can)));
		},
		nullptr, FString(), 0.1f);
	AddU6MiniGame(EFVScreen::Forging, []()
		{
			for (const FFVRecipeDef* R : FVDBU6::RecipesForYard(EFVYardBuilding::Forge))
			{
				if (R && R->bForgeGame) { return R->Id; }
			}
			return FName(NAME_None);
		}, TEXT("forging"));
	AddU6MiniGame(EFVScreen::Excavation, [this]() { return U6BoulderCtx.IsNone() ? FName(TEXT("city:rubble_0")) : U6BoulderCtx; }, TEXT("excavation"));

	// ---- §19 the expert fight (Р60, one vertical track): one fight per rod type (+ a legend) in the City / Cave, the
	// progress pinned for the shot; the panel stays inside the screen and the HUD column never covers it
	struct FFight { int32 Type; bool bLegend; };
	TArray<FFight> Fights;
	for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t) { Fights.Add({ t, false }); }
	Fights.Add({ -1, true });
	for (const FFight& Fi : Fights)
	{
		const FString Slug = Fi.bLegend ? FString(TEXT("legend")) : FString(TypeSlug(EFVExpertRodType(Fi.Type)));
		AddStep(FString::Printf(TEXT("u6 expert fight %s: rig, cast, bite"), *Slug),
			[this, CloseAll, Fi, Slug]()
			{
				CloseAll();
				bStepFlag = false;
				StepValue = 0;
				FVFishing& F = Game->GetFishing();
				F.SetAutoPlay(false);
				F.DebugPinExpertProgress(-1.f);
				F.Reset();
				UFVSaveGame* P = Game->GetProfile();
				if (!P) { return; }
				// what bites: the first ordinary expert fish of the type, or the first expert legend on its own type
				const FFVExpertFishDef* Def = nullptr;
				EFVExpertRodType Type = EFVExpertRodType(FMath::Max(0, Fi.Type));
				for (const FFVExpertFishDef& D : FVDBU6::ExpertFish())
				{
					if (Fi.bLegend ? D.Legend != EFVExpertLegend::None : (D.Legend == EFVExpertLegend::None && FitsType(D, Type)))
					{
						Def = &D;
						break;
					}
				}
				if (Fi.bLegend && Def)
				{
					for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
					{
						if ((Def->TypeMask & uint8(1u << uint32(t))) != 0) { Type = EFVExpertRodType(t); break; }
					}
				}
				const FFVExpertRodDef* Rod = FirstRodOfType(Type);
				const FFVFishDef* Fish = Def ? FVDB::FindFish(Def->FishId) : nullptr;
				if (!Def || !Rod || !Fish)
				{
					U6Check(false, FString::Printf(TEXT("expert fight %s: a fish and a rod for it"), *Slug));
					return;
				}
				const FName Loc = Fish->Location == FVDBU6::CaveId() ? FVDBU6::CaveId() : FVDBU6::CityId();
				if (Game->GetScene().GetLocationId() != Loc)
				{
					Game->TravelToEntry(Loc, FName(TEXT("pier")));
				}
				P->OwnedRods.AddUnique(Rod->RodId);
				P->EquipRod(Rod->RodId);
				Game->NotifyProfileChanged();
				Game->SetClockMinutes(12.f * 60.f);
				StepValue = 1;
				U5Value = 0.f;
				FishStartTime = TotalTime;
				U6FightFish = Def->FishId;
				UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST u6 expert fight %s: %s on %s at %s"), *Slug, *Def->FishId.ToString(), *Rod->RodId.ToString(), *Loc.ToString());
			},
			[this](float T)
			{
				if (StepValue == 0) { return true; }
				FVFishing& F = Game->GetFishing();
				FVScene& S = Game->GetScene();
				if (!bStepFlag)
				{
					// to the water: walk down, then scan around the player
					if (!S.CanFishFromHere() && T < 1.5f) { Synth.MoveY = 1.f; return false; }
					if (!S.CanFishFromHere())
					{
						const FVector2D Start = S.GetPlayerPos();
						const FFVRect B = S.GetWorldBounds();
						bool bFound = false;
						for (float R = 8.f; R < 420.f && !bFound; R += 8.f)
						{
							for (int32 k = 0; k < 16 && !bFound; ++k)
							{
								const float A = 2.f * PI * float(k) / 16.f;
								const FVector2D P = Start + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
								if (!B.Contains(P)) { continue; }
								S.DebugTeleport(P);
								bFound = S.CanFishFromHere();
							}
						}
						if (!bFound) { S.DebugTeleport(Start); }
					}
					bStepFlag = true;
					F.SetAutoPlay(true);
					return false;
				}
				if (F.GetState() == EFVFishState::Waiting && T > 1.8f)
				{
					F.DebugForceExpertBite(U6FightFish);
				}
				if (F.GetState() == EFVFishState::Minigame && F.IsExpertFight())
				{
					F.DebugPinExpertProgress(0.5f);
					return true;
				}
				return false;
			},
			FString(), 0.1f, 15.f);
		AddStep(FString::Printf(TEXT("u6 expert fight %s: the panel"), *Slug),
			nullptr,
			[this, Slug, Fi](float T)
			{
				FVFishing& F = Game->GetFishing();
				if (!F.IsExpertFight())
				{
					U6Check(false, FString::Printf(TEXT("expert fight %s runs (fishing state %d)"), *Slug, int32(F.GetState())));
					return true;
				}
				if (T < 1.2f) { return false; }
				// §19 (Р60): one vertical panel like the classic one, inside the screen, never under the HUD column
				const FFVRect Field = F.GetExpertFieldRect();
				const FFVRect Col = Game->GetHudColumnRect();
				const bool bInside = Field.X >= -0.5f && Field.Y >= -0.5f && Field.Right() <= FV::VW + 0.5f && Field.Bottom() <= FV::VH + 0.5f;
				U6Check(Field.W > 0.f && Field.H > Field.W && bInside && (Game->IsHudColumnHidden() || !Overlaps(Field, Col)),
					FString::Printf(TEXT("expert fight %s: vertical panel %.0fx%.0f at %.0f,%.0f (screen %.0fx%.0f), HUD column %s"), *Slug,
						Field.W, Field.H, Field.X, Field.Y, FV::VW, FV::VH,
						Game->IsHudColumnHidden() ? TEXT("hidden") : (Overlaps(Field, Col) ? TEXT("OVER the panel") : TEXT("beside the panel"))));
				// the 1D fight: the fish on the 760 track, the bars of the type (Двойчатка: two), the meters in range
				if (const FFVExpertFight* X = F.GetExpertFight())
				{
					const int32 WantZones = (!Fi.bLegend && EFVExpertRodType(Fi.Type) == EFVExpertRodType::Twin) || X->Setup.Rod.Type == EFVExpertRodType::Twin ? 2 : 1;
					bool bBars = X->NumZones == WantZones;
					for (int32 i = 0; i < X->NumZones; ++i)
					{
						bBars = bBars && X->Zones[i].Top() >= -0.5f && X->Zones[i].Bottom() <= FVExpert::TrackH + 0.5f && X->Zones[i].H > 20.f;
					}
					U6Check(bBars && X->FishPos >= FVExpert::FishMin - 0.5f && X->FishPos <= FVExpert::FishMax + 0.5f
						&& X->Tension >= 0.f && X->Tension <= 1.f && X->Progress >= 0.f && X->Progress <= 1.f,
						FString::Printf(TEXT("expert fight %s: 1D track %.0f, fish %.0f, %d bar(s) at %.0f h %.0f (want %d), progress %.2f, tension %.2f, %.1f s"),
							*Slug, FVExpert::TrackH, X->FishPos, X->NumZones, X->Zones[0].Y, X->Zones[0].H, WantZones, X->Progress, X->Tension, X->Elapsed));
				}
				else
				{
					U6Check(false, FString::Printf(TEXT("expert fight %s: the fight state"), *Slug));
				}
				return true;
			},
			FString::Printf(TEXT("u6_expert_%s"), *Slug), 0.1f, 5.f);
		AddStep(FString::Printf(TEXT("u6 expert fight %s: end"), *Slug),
			[this, CloseAll]()
			{
				FVFishing& F = Game->GetFishing();
				F.DebugPinExpertProgress(-1.f);
				F.SetAutoPlay(false);
				F.Reset();
				CloseAll();
			},
			nullptr, FString(), 0.2f);
	}
	// ---- Р59: the «Легенды» section of the developer mode (the first section of Пауза → Настройки → «Режим разработчика»)
	AddStep(TEXT("u6 developer mode: the «Легенды» section"),
		[this, CloseAll]()
		{
			CloseAll();
			bStepFlag = false;
			if (UFVSaveGame* P = Game->GetProfile()) { bStepFlag = P->bDevTools; P->bDevTools = true; }
			FPlatformMisc::SetEnvironmentVar(TEXT("FV_HUD_PAGE"), TEXT("dev"));
			Game->OpenScreen(EFVScreen::Pause);
		},
		[this](float T) { return Game->GetScreen() == EFVScreen::Pause && T > 0.4f; },
		TEXT("u6_dev_legends"), 0.2f, 3.f);
	AddStep(TEXT("u6 developer mode: back"),
		[this, CloseAll]()
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("FV_HUD_PAGE"), TEXT(""));
			if (UFVSaveGame* P = Game->GetProfile()) { P->bDevTools = bStepFlag; }
			CloseAll();
		},
		nullptr, FString(), 0.1f);
	AddStep(TEXT("u6: back to the classic rod"),
		[this, CloseAll]()
		{
			CloseAll();
			if (UFVSaveGame* P = Game->GetProfile())
			{
				P->EquipRod(FName(TEXT("rod_poseidon")));
			}
			Game->NotifyProfileChanged();
			Game->TravelTo(FVDB::StartLocation());
		},
		nullptr, FString(), 0.2f);

	// ---- §12.2 the repeated swipe: a second swipe on a list that is still gliding scrolls it again (touch only)
	if (FV::IsTouchUI())
	{
		AddStep(TEXT("u6 touch: the second swipe on a gliding list scrolls again"),
			[this, CloseAll]()
			{
				CloseAll();
				bU6SawCancel = false;
				bU6SawVertical = false;
				Game->OpenScreen(EFVScreen::Achievements);
			},
			[this](float T)
			{
				const FFVRect Win = FV::WindowRect();
				const float X = FMath::RoundToFloat(Win.X + Win.W * 0.5f);
				const float Y0 = Win.Y + Win.H * 0.8f, Y1 = Win.Y + Win.H * 0.35f;
				auto Finger = [this](int32 Id, float PX, float PY)
				{
					FFVTouchPoint P;
					P.Id = Id;
					P.Pos = FVector2D(PX, FMath::RoundToFloat(PY));
					Synth.Touches.Add(P);
				};
				// 1st swipe: a fast fling up (0.45 .. 0.60 s)
				if (T >= 0.45f && T <= 0.60f) { Finger(8, X, FMath::Lerp(Y0, Y1, (T - 0.45f) / 0.15f)); }
				// 2nd swipe while the list glides: the finger lands (the glide stops), then drags up
				const float T2 = 0.68f;
				if (T >= T2 && T <= T2 + 0.35f)
				{
					const float K = FMath::Clamp((T - T2 - 0.08f) / 0.25f, 0.f, 1.f);
					Finger(9, X, FMath::Lerp(Y0, Y0 - (Y0 - Y1) * 0.7f, K));
					const FVDraw& D = Game->DrawCtx;
					bU6SawCancel |= D.IsTapCancelled();
					bU6SawVertical |= D.IsMovedGesture() && D.DragAxis() == FVDraw::EDragAxis::Vertical;
				}
				if (T > T2 + 0.6f)
				{
					if (!bU6SawCancel)
					{
						UE_LOG(LogFV, Log, TEXT("FV: AUTOTEST u6 note: repeated swipe — the list was not gliding when the second finger landed"));
					}
					U6Check(Game->GetScreen() == EFVScreen::Achievements && bU6SawVertical,
						FString::Printf(TEXT("repeated swipe: the second swipe drags the list vertically (glide stopped %d, vertical drag %d)"),
							bU6SawCancel ? 1 : 0, bU6SawVertical ? 1 : 0));
					return true;
				}
				return false;
			},
			TEXT("u6_repeated_swipe"), 0.2f, 4.f);
		AddStep(TEXT("u6 touch: back"), [CloseAll]() { CloseAll(); }, nullptr, FString(), 0.1f);
	}
}

// ============================================================================================
// BALANCE_EXPERT (§10.14)
// ============================================================================================
bool FVAutoTest::RunExpertBalanceStep()
{
	using namespace FVAutoTestU6;
	const TArray<FCell>& All = Cells();
	const TArray<FFVExpertFishDef>& Fish = FVDBU6::ExpertFish();
	static TMap<FName, FVector2D> UniversalSpread;   // fish -> (min, max) strong rate over the rod types (no hazard)
	if (ExpertBalanceIndex == 0)
	{
		UniversalSpread.Reset();
		U6Check(All.Num() > 0 && FVDBU6::ExpertRods().Num() > 0, FString::Printf(TEXT("BALANCE_EXPERT: %d cells, %d expert rods, %d expert fish"),
			All.Num(), FVDBU6::ExpertRods().Num(), Fish.Num()));
		for (const FMechCheck& M : ExpertMechanics()) { U6Check(M.bOk, M.Text); }
	}
	if (!All.IsValidIndex(ExpertBalanceIndex))
	{
		for (const TPair<FName, FVector2D>& U : UniversalSpread)
		{
			U6Check(U.Value.Y - U.Value.X <= UniversalSpreadMax, FString::Printf(TEXT("BALANCE_EXPERT universal %s: strong rate %s..%s over the rod types (spread <= 20 p.p.)"),
				*U.Key.ToString(), *Pct(U.Value.X), *Pct(U.Value.Y)));
		}
		return true;
	}

	const FCell& C = All[ExpertBalanceIndex];
	const int32 Seed = 9000 + ExpertBalanceIndex * 101;
	++ExpertBalanceIndex;
	if (!Fish.IsValidIndex(C.Fish))
	{
		return false;
	}
	const FFVExpertFishDef& D = Fish[C.Fish];
	const FFVExpertRodDef* Rod = RodOfType(C.Type);
	const FFVFishDef* FD = FVDB::FindFish(D.FishId);
	if (!Rod || !FD)
	{
		U6Check(false, FString::Printf(TEXT("BALANCE_EXPERT %s: rod of type %s / fish def"), *D.FishId.ToString(), TypeSlug(C.Type)));
		return false;
	}

	FFVExpertSetup Su;
	Su.Rod = *Rod;
	Su.Stats = FVDBU6::ComposeExpertStats(Rod->RodId, NAME_None, NAME_None, NAME_None);
	Su.Fish = D;
	Su.Level = D.Legend != EFVExpertLegend::None ? FV::ExpertMaxLevel : 13;
	if (C.Hazard >= 0)
	{
		Su.Hazards.Add(EFVHazard(C.Hazard));
	}

	if (D.Legend != EFVExpertLegend::None)
	{
		FFVExpertBot Novice = FFVExpertBot::Strong();
		FFVExpertBot Expert = FFVExpertBot::Strong();
		Expert.bKnowsScore = true;
		FFVExpertBot NoviceMid = FFVExpertBot::Mid();
		const FFVExpertSimResult RN = FVSimulateExpert(Su, Novice, LegendTrials, Seed);
		const FFVExpertSimResult RE = FVSimulateExpert(Su, Expert, LegendTrials, Seed + 1);
		const FFVExpertSimResult RM = FVSimulateExpert(Su, NoviceMid, LegendTrials, Seed + 2);
		const bool bOnTarget = RN.Rate >= 0.10f && RN.Rate <= 0.30f && RE.Rate >= 0.5f && RE.Rate <= 0.75f && RE.MedianSec >= 40.f && RE.MedianSec <= 70.f;
		if (!bOnTarget) { ++ExpertBalanceNotes; }
		UE_LOG(LogFV, Log, TEXT("FV: BALANCE_EXPERT legend %s type=%s novice-mid=%s novice-strong=%s expert-strong=%s med=%.0fs score=%.1f/%.1f snap=%s fz=%.2f/%.2f/%.2f%s"),
			*D.FishId.ToString(), TypeSlug(C.Type), *Pct(RM.Rate), *Pct(RN.Rate), *Pct(RE.Rate), RE.MedianSec, RN.MedianScoreReached, RE.MedianScoreReached,
			*Pct(RE.SnapRate), RM.InZoneFrac, RN.InZoneFrac, RE.InZoneFrac, bOnTarget ? TEXT("") : TEXT(" (note: off the target 10-30 % / 50-75 % / 40-70 s)"));
		U6Check(RE.Rate > RN.Rate || (RE.Rate > 0.f && RE.Rate == RN.Rate),
			FString::Printf(TEXT("BALANCE_EXPERT legend %s can be learnt: «знаток» %s > «новичок» %s"), *D.FishId.ToString(), *Pct(RE.Rate), *Pct(RN.Rate)));
		return false;
	}

	const FFVExpertSimResult RM = FVSimulateExpert(Su, FFVExpertBot::Mid(), SimTrials, Seed);
	const FFVExpertSimResult RS = FVSimulateExpert(Su, FFVExpertBot::Strong(), SimTrials, Seed + 1);
	FString Cheese = TEXT("-");
	if (C.Hazard < 0)
	{
		const FFVExpertSimResult RC = FVSimulateExpert(Su, FFVExpertBot::Cheese(), CheeseTrials, Seed + 2);
		Cheese = Pct(RC.Rate);
		U6Check(RC.Rate < CheeseMax, FString::Printf(TEXT("BALANCE_EXPERT %s on %s: «сыр» lands %s (< 5 %%)"), *D.FishId.ToString(), TypeSlug(C.Type), *Pct(RC.Rate)));
		if (D.bUniversal)
		{
			FVector2D& S = UniversalSpread.FindOrAdd(D.FishId, FVector2D(2.f, -1.f));
			S.X = FMath::Min(float(S.X), RS.Rate);
			S.Y = FMath::Max(float(S.Y), RS.Rate);
		}
	}
	FString Note;
	if (FD->Rarity == EFVRarity::Epic && C.Hazard < 0)
	{
		const bool bOnTarget = RS.MedianSec >= 12.f && RS.MedianSec <= 20.f && RM.Rate >= 0.45f && RM.Rate <= 0.75f && RS.Rate >= 0.75f && RS.Rate <= 0.92f;
		if (!bOnTarget)
		{
			++ExpertBalanceNotes;
			Note = TEXT(" (note: epic off the target 12-20 s / mid 45-75 % / strong 75-92 %)");
		}
	}
	UE_LOG(LogFV, Log, TEXT("FV: BALANCE_EXPERT %s type=%s hazard=%s mid=%s strong=%s cheese=%s med=%.0fs snap=%s fz=%.2f/%.2f (%s, %s)%s"),
		*D.FishId.ToString(), TypeSlug(C.Type), HazardSlug(C.Hazard), *Pct(RM.Rate), *Pct(RS.Rate), *Cheese, RS.MedianSec, *Pct(RS.SnapRate), RM.InZoneFrac, RS.InZoneFrac,
		*FVDB::RarityName(FD->Rarity), D.bUniversal ? TEXT("universal") : TEXT("typed"), *Note);
	return false;
}

// ============================================================================================
// 0.6 world checks (the owner's bugs of 0.6: walking through the forest windfall / the broken footbridge, the camera
// on the City pier / the path to the adit)
// ============================================================================================
void FVAutoTest::AddU6WorldSteps(TFunction<void()> CloseAll)
{
	using namespace FVAutoTestU6;

	// ---- entries and exits of every big world (data): every spawn stands inside the walk rects, outside the blockers and
	// the exit triggers; every exit into another big world names an entry that world has
	AddStep(TEXT("u6 world: entries and exits"), [this]()
		{
			const FName Worlds[] = { FVDBU6::ForestId(), FVDB::HomeId(), FVDB::HouseId(), FVDBU6::CityId(), FVDBU6::CaveId(), FVMill::MillId() };
			TMap<FName, FVWorldU6::FWorldDef> Defs;
			for (const FName W : Worlds)
			{
				FFVImage Img;
				FVWorldU6::FWorldDef D;
				if (FVWorldU6::BuildWorld(W, Img, D)) { Defs.Add(W, MoveTemp(D)); }
			}
			for (const TPair<FName, FVWorldU6::FWorldDef>& It : Defs)
			{
				const FVWorldU6::FWorldDef& D = It.Value;
				FString Bad;
				int32 Spawns = 0;
				for (const FVWorldU6::FAnchor& A : D.Anchors)
				{
					if (A.Kind == FVWorldU6::EAnchor::Spawn)
					{
						++Spawns;
						bool bIn = false, bBlocked = false, bOnExit = false;
						for (const FFVRect& R : D.Walk) { bIn = bIn || R.Contains(A.Feet); }
						for (const FFVRect& R : D.Blockers) { bBlocked = bBlocked || R.Contains(A.Feet); }
						for (const FVWorldU6::FAnchor& E : D.Anchors) { bOnExit = bOnExit || (E.Kind == FVWorldU6::EAnchor::Exit && E.Hit.W > 0.f && E.Hit.Contains(A.Feet)); }
						if (!bIn || bBlocked || bOnExit)
						{
							Bad += FString::Printf(TEXT(" spawn «%s»%s%s%s"), *A.Id.ToString(), bIn ? TEXT("") : TEXT(" outside the walk"),
								bBlocked ? TEXT(" in a blocker") : TEXT(""), bOnExit ? TEXT(" on an exit") : TEXT(""));
						}
					}
					else if (A.Kind == FVWorldU6::EAnchor::Exit)
					{
						const FVWorldU6::FWorldDef* T = Defs.Find(A.Target);
						if (!T) { continue; }   // a classic location / the map
						bool bEntry = false;
						for (const FVWorldU6::FAnchor& S2 : T->Anchors) { bEntry = bEntry || (S2.Kind == FVWorldU6::EAnchor::Spawn && S2.Id == A.Id); }
						if (!bEntry) { Bad += FString::Printf(TEXT(" exit -> %s: no entry «%s» there"), *A.Target.ToString(), *A.Id.ToString()); }
					}
				}
				U6Check(Spawns > 0 && Bad.IsEmpty(), FString::Printf(TEXT("world %s: %d entries on walkable ground, exits lead to existing entries%s"),
					*It.Key.ToString(), Spawns, *Bad));
			}
		}, nullptr, FString(), 0.05f);

	// the barrier state of the rich profile, restored after the checks
	struct FSaved { bool bBlockage = true; bool bBridge = true; int32 Beavers = 0; TArray<FName> Rubble; bool bMillOpen = false; };
	TSharedRef<FSaved> Saved = MakeShared<FSaved>();
	auto SetBarrier = [](UFVSaveGame& P, int32 Kind, bool bOpen)
	{
		switch (Kind)
		{
		case 0: P.U6.bBlockageCleared = bOpen; break;
		case 1: P.U6.Mill.bBridgeRepaired = bOpen; break;
		case 2:
			P.U6.RubbleCleared.Reset();
			for (int32 i = 0; bOpen && i < 3; ++i) { P.U6.RubbleCleared.AddUnique(FName(*FString::Printf(TEXT("rubble_%d"), i))); }
			break;
		default: P.U6.Mill.BeaverFeeds = bOpen ? FVMill::BeaverFeedsNeeded : 0; break;
		}
	};
	AddStep(TEXT("u6 world: remember the barriers"), [this, Saved]()
		{
			if (UFVSaveGame* P = Game->GetProfile())
			{
				Saved->bBlockage = P->U6.bBlockageCleared;
				Saved->bBridge = P->U6.Mill.bBridgeRepaired;
				Saved->Beavers = P->U6.Mill.BeaverFeeds;
				Saved->Rubble = P->U6.RubbleCleared;
				Saved->bMillOpen = P->IsLocationUnlocked(FVMill::MillId());
			}
		}, nullptr, FString(), 0.05f);

	// ---- the barriers close the way until cleared, then let the player pass: walked with the game's movement code at
	// the worst frame step (0.1 s, the Dt clamp: no tunnelling through a thin band) and at 30 fps. The shot shows the
	// player stopped at the closed barrier.
	struct FBarrier { FName Loc; const TCHAR* Slug; FVector2D From; FVector2D Dir; FVector2D Past; int32 Kind; };
	const FBarrier Barriers[] = {
		{ FVDBU6::ForestId(), TEXT("windfall_top"),  FVector2D(340.f, 206.f),  FVector2D(1.f, 0.f),  FVector2D(472.f, 206.f), 0 },
		{ FVDBU6::ForestId(), TEXT("windfall_path"), FVector2D(340.f, 228.f),  FVector2D(1.f, 0.f),  FVector2D(472.f, 228.f), 0 },
		{ FVDBU6::ForestId(), TEXT("footbridge"),    FVector2D(525.f, 116.f),  FVector2D(0.f, -1.f), FVector2D(525.f, 66.f), 1 },
		{ FVDBU6::CityId(),   TEXT("cave_rubble"),   FVector2D(1110.f, 172.f), FVector2D(0.f, -1.f), FVector2D(1110.f, 106.f), 2 },
		{ FVMill::MillId(),   TEXT("omut"),          FVector2D(780.f, 222.f),  FVector2D(1.f, 0.f),  FVector2D(836.f, 222.f), 3 },
	};
	for (const FBarrier& Bar : Barriers)
	{
		AddStep(FString::Printf(TEXT("u6 world: barrier %s"), Bar.Slug), [this, CloseAll, Bar, SetBarrier]()
			{
				CloseAll();
				Game->GetFishing().Reset();
				Game->SetWeather(EFVWeather::Sunny);
				Game->SetClockMinutes(12.f * 60.f);
				UFVSaveGame* P = Game->GetProfile();
				if (!P) { return; }
				if (Bar.Kind == 3) { P->U6.Mill.bBridgeRepaired = true; P->UnlockedLocations.AddUnique(FVMill::MillId()); }
				FVScene& S = Game->GetScene();
				if (S.GetLocationId() != Bar.Loc && !Game->TravelTo(Bar.Loc))
				{
					U6Check(false, FString::Printf(TEXT("barrier %s: travel to %s"), Bar.Slug, *Bar.Loc.ToString()));
					return;
				}
				const FVector2D From = Tex(float(Bar.From.X), float(Bar.From.Y)), Past = Tex(float(Bar.Past.X), float(Bar.Past.Y));
				for (int32 Phase = 0; Phase < 2; ++Phase)
				{
					const bool bOpen = Phase == 0;
					SetBarrier(*P, Bar.Kind, bOpen);
					S.RefreshWorldObjects();
					S.DebugTeleport(From);
					S.ClearPendingExit();
					S.SnapCamera();
					const bool bStart = FVector2D::Distance(S.GetPlayerPos(), From) < 2.f;
					const FWalkLog W = SimWalk(S, Bar.Dir, 5.f, bOpen ? 1.f / 30.f : 0.1f,
						[&Past, &Bar](const FVector2D& Pp) { return FVector2D::DotProduct(Pp - Past, Bar.Dir) >= 0.f; });
					const FVector2D End = S.GetPlayerPos();
					U6Check(bStart && W.bDone == bOpen, FString::Printf(TEXT("barrier %s %s: %s (start %s, the feet stopped at %.0f, %.0f texels)"),
						Bar.Slug, bOpen ? TEXT("cleared") : TEXT("closed"), bOpen ? TEXT("the player walks through") : TEXT("the player cannot pass"),
						bStart ? TEXT("walkable") : TEXT("NOT walkable"), End.X / FV::WorldScale, End.Y / FV::WorldScale));
				}
				S.SnapCamera();
			},
			[](float T) { return T > 0.5f; }, FString::Printf(TEXT("u6_barrier_%s"), Bar.Slug), 0.2f, 5.f);
	}
	AddStep(TEXT("u6 world: restore the barriers"), [this, CloseAll, Saved]()
		{
			CloseAll();
			if (UFVSaveGame* P = Game->GetProfile())
			{
				P->U6.bBlockageCleared = Saved->bBlockage;
				P->U6.Mill.bBridgeRepaired = Saved->bBridge;
				P->U6.Mill.BeaverFeeds = Saved->Beavers;
				P->U6.RubbleCleared = Saved->Rubble;
				if (Saved->bMillOpen) { P->UnlockedLocations.AddUnique(FVMill::MillId()); }
			}
			Game->GetScene().RefreshWorldObjects();
		}, nullptr, FString(), 0.1f);

	// ---- the camera on the narrow strips (the owner: «the camera breaks when I walk down to the City pier / up to the
	// adit»): walking along them the camera moves smoothly (no tick moves it more than MaxStep px), never shows past the
	// world's edge and keeps the player's body in the band between the HUD strip and the bottom dock
	constexpr float MaxStep = 20.f;
	struct FCamWalk { FName Loc; const TCHAR* Slug; FVector2D From; FVector2D Dir; FVector2D Past; };
	const FCamWalk CamWalks[] = {
		{ FVDBU6::CityId(),   TEXT("city_pier_down"),   FVector2D(410.f, 238.f),  FVector2D(0.f, 1.f),  FVector2D(410.f, 306.f) },
		{ FVDBU6::CityId(),   TEXT("city_pier_up"),     FVector2D(410.f, 306.f),  FVector2D(0.f, -1.f), FVector2D(410.f, 200.f) },
		{ FVDBU6::CityId(),   TEXT("city_quay_along"),  FVector2D(330.f, 248.f),  FVector2D(1.f, 0.f),  FVector2D(500.f, 248.f) },
		{ FVDBU6::CityId(),   TEXT("city_berth_down"),  FVector2D(163.f, 238.f),  FVector2D(0.f, 1.f),  FVector2D(163.f, 276.f) },
		{ FVDBU6::CityId(),   TEXT("city_adit_up"),     FVector2D(1110.f, 200.f), FVector2D(0.f, -1.f), FVector2D(1110.f, 106.f) },
		{ FVDBU6::CityId(),   TEXT("city_adit_down"),   FVector2D(1110.f, 106.f), FVector2D(0.f, 1.f),  FVector2D(1110.f, 220.f) },
		{ FVDBU6::ForestId(), TEXT("forest_mill_path"), FVector2D(525.f, 116.f),  FVector2D(0.f, -1.f), FVector2D(525.f, 66.f) },
		{ FVDBU6::CaveId(),   TEXT("cave_planks"),      FVector2D(578.f, 240.f),  FVector2D(0.f, 1.f),  FVector2D(578.f, 310.f) },
		{ FVDBU6::CaveId(),   TEXT("cave_adit"),        FVector2D(58.f, 130.f),   FVector2D(0.f, -1.f), FVector2D(58.f, 78.f) },
	};
	for (const FCamWalk& CW : CamWalks)
	{
		AddStep(FString::Printf(TEXT("u6 world: camera %s"), CW.Slug), [this, CloseAll, CW, MaxStep]()
			{
				CloseAll();
				Game->GetFishing().Reset();
				FVScene& S = Game->GetScene();
				if (S.GetLocationId() != CW.Loc && !Game->TravelTo(CW.Loc))
				{
					U6Check(false, FString::Printf(TEXT("camera %s: travel to %s"), CW.Slug, *CW.Loc.ToString()));
					return;
				}
				const FVector2D From = Tex(float(CW.From.X), float(CW.From.Y)), Past = Tex(float(CW.Past.X), float(CW.Past.Y));
				S.DebugTeleport(From);
				S.ClearPendingExit();
				S.SnapCamera();
				const FVector2D Cam0 = S.GetCamera();
				const FWalkLog W = SimWalk(S, CW.Dir, 4.f, 1.f / 30.f,
					[&Past, &CW](const FVector2D& Pp) { return FVector2D::DotProduct(Pp - Past, CW.Dir) >= 0.f; });
				const FVector2D End = S.GetPlayerPos(), Cam1 = S.GetCamera();
				U6Check(W.bDone && W.MaxCamStep <= MaxStep && !W.bVoid && !W.bLost, FString::Printf(
					TEXT("camera %s: the walk %s, the largest camera step %.1f px (<= %.0f), %s, %s (camera %.0f,%.0f -> %.0f,%.0f, the feet at %.0f, %.0f texels)"),
					CW.Slug, W.bDone ? TEXT("arrives") : TEXT("DOES NOT arrive"), W.MaxCamStep, MaxStep, W.bVoid ? TEXT("VOIDS shown") : TEXT("no voids"),
					W.bLost ? TEXT("the player LEFT the band") : TEXT("the player stays in the band"), Cam0.X, Cam0.Y, Cam1.X, Cam1.Y,
					End.X / FV::WorldScale, End.Y / FV::WorldScale));
			},
			[](float T) { return T > 0.5f; }, FString::Printf(TEXT("u6_camwalk_%s"), CW.Slug), 0.2f, 5.f);
	}

	// ---- every big location and the scrolling village pond (hotfix 0.61): (1) every interactive object can be reached on
	// foot from the arrival point (nothing inside a wall / behind a blocker), (2) a sweep over the walkable area: from every
	// grid point short walks in four directions — the camera never jumps, never shows voids, the player stays in the band
	// and out from under the HUD column
	const FName SweepLocs[] = { FVDBU6::ForestId(), FVDB::HomeId(), FVDB::HouseId(), FVDBU6::CityId(), FVDBU6::CaveId(), FVMill::MillId(), FName(TEXT("pond")) };
	for (const FName Loc : SweepLocs)
	{
		AddStep(FString::Printf(TEXT("u6 world: sweep %s"), *Loc.ToString()), [this, CloseAll, Loc, MaxStep]()
			{
				CloseAll();
				Game->GetFishing().Reset();
				Game->SetClockMinutes(12.f * 60.f);
				FVScene& S = Game->GetScene();
				if (S.GetLocationId() != Loc && !Game->TravelTo(Loc))
				{
					U6Check(false, FString::Printf(TEXT("sweep: travel to %s"), *Loc.ToString()));
					return;
				}
				S.RefreshWorldObjects();
				// (1) reachability from the arrival point
				const FVector2D Arrival = S.GetPlayerPos();
				U6Check(S.DebugIsWalkable(Arrival), FString::Printf(TEXT("sweep %s: the arrival point (%.0f, %.0f) is walkable"), *Loc.ToString(), Arrival.X, Arrival.Y));
				const FReach R = FloodReach(S, Arrival);
				FString Unreached;
				int32 Objects = 0;
				for (const FFVWorldObject& O : S.GetWorldObjects())
				{
					++Objects;
					if (!R.At(O.Feet, FMath::Max(8.f, O.Reach - 2.f)))
					{
						Unreached += FString::Printf(TEXT(" «%s» (%.0f, %.0f)"), *O.Label, O.Feet.X, O.Feet.Y);
					}
				}
				U6Check(Unreached.IsEmpty(), FString::Printf(TEXT("sweep %s: all %d objects reachable on foot%s"), *Loc.ToString(), Objects,
					Unreached.IsEmpty() ? TEXT("") : *(TEXT(": NOT reachable") + Unreached)));

				// (2) the camera sweep
				const FFVRect B = S.GetWorldBounds();
				const FFVRect Col = Game->IsHudColumnHidden() ? FFVRect() : Game->GetHudColumnRect();
				const FVector2D Dirs[4] = { FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f) };
				int32 Points = 0, Bad = 0, UnderHud = 0;
				float Worst = 0.f;
				FString FirstBad, FirstHud;
				constexpr float Grid = 112.f;
				for (float Y = B.Y + Grid * 0.5f; Y < B.Bottom(); Y += Grid)
				{
					for (float X = B.X + Grid * 0.5f; X < B.Right(); X += Grid)
					{
						const FVector2D Q(X, Y);
						if (!S.DebugIsWalkable(Q) || !R.At(Q, 8.f)) { continue; }
						++Points;
						for (const FVector2D& D : Dirs)
						{
							S.DebugTeleport(Q);
							S.ClearPendingExit();
							S.SnapCamera();
							if (D.X > 0.f && Col.W > 0.f)
							{
								// the HUD column never covers the player standing still (§12.1)
								const FVector2D Scr = S.GetPlayerPos() - S.GetCamera();
								if (Overlaps(Col, FFVRect(float(Scr.X) - 10.f, float(Scr.Y) - 60.f, 20.f, 60.f)))
								{
									++UnderHud;
									if (FirstHud.IsEmpty()) { FirstHud = FString::Printf(TEXT(", first at (%.0f, %.0f)"), X, Y); }
								}
							}
							const FWalkLog W = SimWalk(S, D, 0.35f, 1.f / 30.f, [](const FVector2D&) { return false; });
							Worst = FMath::Max(Worst, W.MaxCamStep);
							if (W.MaxCamStep > MaxStep || W.bVoid || W.bLost)
							{
								++Bad;
								if (FirstBad.IsEmpty())
								{
									FirstBad = FString::Printf(TEXT(", first from (%.0f, %.0f) dir (%.0f, %.0f): step %.1f%s%s"), X, Y, D.X, D.Y, W.MaxCamStep,
										W.bVoid ? TEXT(", voids") : TEXT(""), W.bLost ? TEXT(", the player left the band") : TEXT(""));
								}
							}
						}
					}
				}
				U6Check(Points > 0 && Bad == 0, FString::Printf(TEXT("sweep %s: camera smooth, no voids, the player in the band (%d points x 4 walks, the largest step %.1f px, %d bad%s)"),
					*Loc.ToString(), Points, Worst, Bad, *FirstBad));
				U6Check(UnderHud == 0, FString::Printf(TEXT("sweep %s: the HUD column never covers the standing player (%d of %d points under it%s)"),
					*Loc.ToString(), UnderHud, Points, *FirstHud));
				S.DebugTeleport(Arrival);
				S.ClearPendingExit();
				S.SnapCamera();
			},
			[](float T) { return T > 0.3f; }, FString(), 0.1f, 10.f);
	}
	// ---- Р69: walking between the places without the map. Every edge of the graph, both ways, with the game's movement
	// code (no system input): the player walks into the exit, UFVGame fades and travels, the player then stands beside
	// the exit back (outside its trigger: no bounce) on the edge facing the place he came from; that exit has its way
	// sign with the picture of the place, off the HUD column. Then the way back.
	struct FWalkState { FName Before; TStrongObjectPtr<UFVSaveGame> Rich; FString Why; };
	TSharedRef<FWalkState> WS = MakeShared<FWalkState>();
	// walks into the exit to Target; true when the trigger fired (left pending: UFVGame's tick uses it)
	auto WalkInto = [](FVScene& S, FName Target) -> bool
	{
		FFVRect Hit;
		FVector2D Feet = FVector2D::ZeroVector, Sign = FVector2D::ZeroVector;
		if (!S.DebugFindExit(Target, Hit, Feet, Sign)) { return false; }
		const FVector2D C(Hit.X + Hit.W * 0.5f, Hit.Y + Hit.H * 0.5f);
		S.ClearPendingExit();
		FFVInput In;
		for (int32 i = 0; i < 240; ++i)
		{
			const FVector2D D = (C - S.GetPlayerPos()).GetSafeNormal();
			In.MoveX = float(D.X);
			In.MoveY = float(D.Y);
			S.Tick(1.f / 30.f, In, true);
			FName L, E;
			if (S.GetPendingExit(L, E)) { return L == Target; }
		}
		return false;
	};
	auto PictoOk = [](FName T) -> bool
	{
		using FVWorldU6::EWayPicto;
		const FString N = T.ToString();
		const EWayPicto Want = N == TEXT("river") ? EWayPicto::River : N == TEXT("pond") ? EWayPicto::Pond : N == TEXT("mountain") ? EWayPicto::Mountain
			: N == TEXT("ocean") ? EWayPicto::Ocean : N == TEXT("swamp") ? EWayPicto::Swamp : N == TEXT("volcano") ? EWayPicto::Volcano
			: T == FVDBU6::ForestId() ? EWayPicto::Forest : FVMill::IsMillId(T) ? EWayPicto::Mill : T == FVDBU6::CityId() ? EWayPicto::City
			: T == FVDBU6::CaveId() ? EWayPicto::Cave : EWayPicto::Home;
		return FVWorldU6::WayPictoFor(T) == Want;
	};
	// the player is in Loc beside the exit back to From; the exit has its sign; nothing of it under the HUD column
	auto CheckArrival = [this, PictoOk](FName Loc, FName From, const FString& What)
	{
		FVScene& S = Game->GetScene();
		const bool bHere = S.GetLocationId() == Loc;
		FFVRect Hit;
		FVector2D Feet = FVector2D::ZeroVector, Sign = FVector2D::ZeroVector;
		const bool bExit = bHere && S.DebugFindExit(From, Hit, Feet, Sign);
		const FVector2D Pp = S.GetPlayerPos();
		const float K = S.IsBigWorld() ? FV::WorldScale : 1.f;
		const float Dist = bExit ? float(FVector2D::Distance(Pp, Feet)) : -1.f;
		const bool bOut = bExit && !Hit.Contains(Pp);
		bool bEdge = true;
		if (bExit && !S.IsBigWorld())
		{
			// (hotfix 0.61: relative to the world bounds — the village pond scrolls past the screen; a one-screen place's
			// bounds are the screen)
			const FFVRect WB = S.GetWorldBounds();
			for (const FVWorldU6::FWalkLink& L : FVWorldU6::ClassicWalkLinks(Loc))
			{
				if (L.To != From) { continue; }
				bEdge = L.Side == FVWorldU6::EWayDir::Left ? Feet.X < WB.X + WB.W * 0.3f
					: (L.Side == FVWorldU6::EWayDir::Right ? Feet.X > WB.X + WB.W * 0.7f : Feet.Y < WB.Y + WB.H * 0.6f);
			}
		}
		U6Check(bHere && bExit && Dist >= 0.f && Dist <= (S.IsBigWorld() ? 80.f * K : 130.f) && bOut && bEdge, FString::Printf(
			TEXT("walk %s: arrives in %s (now %s) beside the exit back to %s (%.0f px from it, %s the trigger, %s)"), *What, *Loc.ToString(),
			*S.GetLocationId().ToString(), *From.ToString(), Dist, bOut ? TEXT("outside") : TEXT("INSIDE"), bEdge ? TEXT("on its edge") : TEXT("on the WRONG edge")));
		if (!bExit) { return; }
		// the sign: a picture of the place, a tap object with its name, off the HUD column
		bool bObj = false;
		for (const FFVWorldObject& O : S.GetWorldObjects()) { bObj = bObj || (O.Kind == EFVObjKind::Exit && O.Id == From && O.Label.Len() > 2); }
		const FVector2D Cam = S.GetCamera();
		const FFVRect Col = Game->IsHudColumnHidden() ? FFVRect() : Game->GetHudColumnRect();
		const FFVRect SignScr(float(Sign.X - Cam.X) - 16.f * K, float(Sign.Y - Cam.Y) - 32.f * K, 32.f * K, 32.f * K);
		const FFVRect HitScr(float(Hit.X - Cam.X), float(Hit.Y - Cam.Y), Hit.W, Hit.H);
		const bool bSignHud = Col.W > 0.f && Overlaps(Col, SignScr), bHitHud = Col.W > 0.f && Overlaps(Col, HitScr);
		U6Check(!Sign.IsZero() && bObj && PictoOk(From) && !bSignHud && !bHitHud, FString::Printf(
			TEXT("walk %s: the exit %s -> %s has a way sign (%s, the picture %s, a tap object %s) %s, the trigger %s"), *What, *Loc.ToString(), *From.ToString(),
			Sign.IsZero() ? TEXT("NONE") : TEXT("yes"), PictoOk(From) ? TEXT("of the place") : TEXT("WRONG"), bObj ? TEXT("yes") : TEXT("NONE"),
			bSignHud ? TEXT("UNDER the HUD column") : TEXT("off the HUD column"), bHitHud ? TEXT("UNDER the HUD column") : TEXT("off the HUD column")));
	};
	auto Prepare = [this, CloseAll]()
	{
		CloseAll();
		Game->GetFishing().Reset();
		Game->SetWeather(EFVWeather::Sunny);
		Game->SetClockMinutes(12.f * 60.f);
	};
	AddStep(TEXT("u6 walk: every place of the graph is open on the rich profile"), [this, WS]()
		{
			WS->Before = Game->GetScene().GetLocationId();
			const UFVSaveGame* P = Game->GetProfile();
			FString Closed;
			for (const TCHAR* T : { TEXT("pond"), TEXT("river"), TEXT("mountain"), TEXT("ocean"), TEXT("swamp"), TEXT("volcano"), TEXT("forest"), TEXT("mill"), TEXT("city"), TEXT("cave"), TEXT("home") })
			{
				if (!FVWorldU6::WalkTargetOpen(P, FName(T))) { Closed += FString::Printf(TEXT(" %s"), T); }
			}
			U6Check(Closed.IsEmpty(), FString::Printf(TEXT("walk: all places open on the rich profile%s"), Closed.IsEmpty() ? TEXT("") : *(TEXT(": CLOSED") + Closed)));
		}, nullptr, FString(), 0.05f);

	struct FEdge { const TCHAR* A; const TCHAR* B; };
	const FEdge Edges[] = {
		{ TEXT("home"), TEXT("pond") }, { TEXT("pond"), TEXT("river") }, { TEXT("pond"), TEXT("ocean") }, { TEXT("river"), TEXT("forest") },
		{ TEXT("river"), TEXT("mountain") }, { TEXT("forest"), TEXT("swamp") }, { TEXT("forest"), TEXT("mill") }, { TEXT("mountain"), TEXT("volcano") },
		{ TEXT("volcano"), TEXT("ocean") }, { TEXT("city"), TEXT("cave") },
	};
	for (const FEdge& Ed : Edges)
	{
		const FName A(Ed.A), B(Ed.B);
		const FString AB = FString::Printf(TEXT("%s -> %s"), Ed.A, Ed.B), BA = FString::Printf(TEXT("%s -> %s"), Ed.B, Ed.A);
		AddStep(FString::Printf(TEXT("u6 walk: %s"), *AB), [this, Prepare, WalkInto, A, B, AB]()
			{
				Prepare();
				if (!Game->TravelToEntry(A, FName(*FString::Printf(TEXT("from_%s"), *B.ToString()))))
				{
					U6Check(false, FString::Printf(TEXT("walk %s: travel to %s"), *AB, *A.ToString()));
					return;
				}
				const bool bFired = WalkInto(Game->GetScene(), B);
				U6Check(bFired, FString::Printf(TEXT("walk %s: walking into the exit fires it"), *AB));
			},
			[this, B](float T) { return T > 0.6f && Game->GetScene().GetLocationId() == B; }, FString(), 0.1f, 4.f);
		AddStep(FString::Printf(TEXT("u6 walk: %s"), *BA), [this, CheckArrival, WalkInto, A, B, AB, BA]()
			{
				CheckArrival(B, A, AB);
				if (Game->GetScene().GetLocationId() != B) { return; }
				const FVector2D P0 = Game->GetScene().GetPlayerPos();
				const bool bFired = WalkInto(Game->GetScene(), A);
				const FVector2D P1 = Game->GetScene().GetPlayerPos();
				FString Blocked;
				for (float X = float(P1.X); !bFired && X < float(P1.X) + 12.f; X += 1.f)
				{
					if (!Game->GetScene().DebugIsWalkable(FVector2D(X, P1.Y))) { Blocked = FString::Printf(TEXT(", not walkable at x %.0f"), X); break; }
				}
				U6Check(bFired, FString::Printf(TEXT("walk %s: walking into the exit fires it (the feet %.0f, %.0f -> %.0f, %.0f%s)"), *BA, P0.X, P0.Y, P1.X, P1.Y, *Blocked));
			},
			[this, A](float T) { return T > 0.6f && Game->GetScene().GetLocationId() == A; }, FString(), 0.1f, 4.f);
		AddStep(FString::Printf(TEXT("u6 walk: %s arrival"), *BA), [CheckArrival, A, B, BA]() { CheckArrival(A, B, BA); }, nullptr, FString(), 0.05f);
	}

	// ---- the way signs on screen (the owner: «the forest sign to the river confused me»): the player beside the sign,
	// its caption shown
	struct FSignShot { const TCHAR* Loc; const TCHAR* To; };
	const FSignShot SignShots[] = {
		{ TEXT("forest"), TEXT("river") }, { TEXT("pond"), TEXT("river") }, { TEXT("river"), TEXT("mountain") }, { TEXT("forest"), TEXT("swamp") },
		{ TEXT("home"), TEXT("pond") }, { TEXT("city"), TEXT("cave") }, { TEXT("cave"), TEXT("city") }, { TEXT("pond"), TEXT("ocean") }, { TEXT("ocean"), TEXT("volcano") },
		{ TEXT("volcano"), TEXT("mountain") }, { TEXT("swamp"), TEXT("forest") },
	};
	for (const FSignShot& Sh : SignShots)
	{
		const FName Loc(Sh.Loc), To(Sh.To);
		AddStep(FString::Printf(TEXT("u6 walk: the sign %s -> %s"), Sh.Loc, Sh.To), [this, Prepare, Loc, To]()
			{
				Prepare();
				const bool bOk = Game->TravelToEntry(Loc, FName(*FString::Printf(TEXT("from_%s"), *To.ToString())));
				FFVRect Hit;
				FVector2D Feet = FVector2D::ZeroVector, Sign = FVector2D::ZeroVector;
				U6Check(bOk && Game->GetScene().DebugFindExit(To, Hit, Feet, Sign) && !Sign.IsZero(),
					FString::Printf(TEXT("walk: %s has a way sign to %s"), *Loc.ToString(), *To.ToString()));
			},
			[](float T) { return T > 0.6f; }, FString::Printf(TEXT("u6_walk_sign_%s_%s"), Sh.Loc, Sh.To), 0.1f, 4.f);
	}

	// ---- closed places: a new level-1 profile. The walk into a closed place is refused with the condition (the player
	// stays, the sign shows a padlock); the yard gate to the pond (open from the start) works.
	AddStep(TEXT("u6 walk closed: a new level-1 profile"), [this, Prepare, WS]()
		{
			Prepare();
			WS->Rich.Reset(Game->Profile.Get());
			UFVSaveGame* N = NewObject<UFVSaveGame>(Game);
			N->ResetToNewGame();
			N->OnboardingStep = 100;
			Game->Profile = N;
			const bool bTravel = Game->TravelToEntry(FName(TEXT("pond")), FName(TEXT("from_river")));
			Game->GetScene().RefreshWorldObjects();
			FString Wrong;
			for (const TCHAR* T : { TEXT("river"), TEXT("mountain"), TEXT("forest"), TEXT("mill"), TEXT("swamp"), TEXT("volcano"), TEXT("city"), TEXT("cave") })
			{
				if (FVWorldU6::WalkTargetOpen(N, FName(T))) { Wrong += FString::Printf(TEXT(" %s"), T); }
			}
			if (!FVWorldU6::WalkTargetOpen(N, FName(TEXT("pond"))) || !FVWorldU6::WalkTargetOpen(N, FVDB::HomeId())) { Wrong += TEXT(" pond/home CLOSED"); }
			U6Check(bTravel && N->GetLevel() == 1 && Wrong.IsEmpty(), FString::Printf(TEXT("walk closed: level %d, the pond reached (%s), the river / mountains / forest / mill / swamp / volcano / City / Cave closed, the pond and home open%s"),
				N->GetLevel(), bTravel ? TEXT("yes") : TEXT("NO"), Wrong.IsEmpty() ? TEXT("") : *(TEXT(": WRONG") + Wrong)));
		}, nullptr, FString(), 0.1f);
	AddStep(TEXT("u6 walk closed: pond -> river is refused"), [this, WalkInto, WS]()
		{
			ClearToasts();
			FString Why;
			FVWorldU6::WalkTargetOpen(Game->GetProfile(), FName(TEXT("river")), nullptr, &Why);
			WS->Why = Why;
			const bool bFired = WalkInto(Game->GetScene(), FName(TEXT("river")));
			U6Check(bFired, TEXT("walk closed: walking into the exit to the river fires it"));
		},
		[](float T) { return T > 1.2f; }, TEXT("u6_walk_closed_pond_river"), 0.1f, 4.f);
	AddStep(TEXT("u6 walk closed: the player stays, the condition is told"), [this, WS]()
		{
			FVScene& S = Game->GetScene();
			bool bToast = false;
			for (const UFVGame::FToast& T : Game->Toasts) { bToast = bToast || (!WS->Why.IsEmpty() && T.Text == WS->Why); }
			bool bLocked = false;
			for (const FFVWorldObject& O : S.GetWorldObjects()) { bLocked = bLocked || (O.Kind == EFVObjKind::Exit && O.Id == FName(TEXT("river")) && O.Index == 1 && O.Label.Contains(TEXT("Закрыто"))); }
			FFVRect Hit;
			FVector2D Feet = FVector2D::ZeroVector, Sign = FVector2D::ZeroVector;
			const bool bExit = S.DebugFindExit(FName(TEXT("river")), Hit, Feet, Sign);
			const float Dist = bExit ? float(FVector2D::Distance(S.GetPlayerPos(), Feet)) : -1.f;
			U6Check(S.GetLocationId() == FName(TEXT("pond")) && bExit && Dist >= 0.f && Dist <= 60.f && bToast && bLocked && WS->Why.Contains(TEXT("уровня")), FString::Printf(
				TEXT("walk closed: still at %s (%.0f px from the exit), the toast «%s» %s, the sign %s"), *S.GetLocationId().ToString(), Dist, *WS->Why,
				bToast ? TEXT("shown") : TEXT("NOT shown"), bLocked ? TEXT("locked («Закрыто…»)") : TEXT("NOT locked")));
		}, nullptr, FString(), 0.05f);
	AddStep(TEXT("u6 walk closed: the yard gate leads to the pond"), [this, Prepare, WalkInto]()
		{
			Prepare();
			const bool bTravel = Game->TravelToEntry(FVDB::HomeId(), FName(TEXT("from_pond")));
			U6Check(bTravel && WalkInto(Game->GetScene(), FName(TEXT("pond"))), TEXT("walk closed: level 1, walking into the yard gate fires it"));
		},
		[this](float T) { return T > 0.6f && Game->GetScene().GetLocationId() == FName(TEXT("pond")); }, FString(), 0.1f, 4.f);
	AddStep(TEXT("u6 walk closed: at the pond"), [CheckArrival]() { CheckArrival(FName(TEXT("pond")), FVDB::HomeId(), TEXT("closed profile home -> pond")); }, nullptr, FString(), 0.05f);
	AddStep(TEXT("u6 walk: back to the rich profile"), [this, Prepare, WS]()
		{
			Prepare();
			if (WS->Rich.IsValid()) { Game->Profile = WS->Rich.Get(); }
			WS->Rich.Reset();
			if (!WS->Before.IsNone()) { Game->TravelTo(WS->Before); }
			Game->GetScene().RefreshWorldObjects();
		}, nullptr, FString(), 0.1f);
}

// ================================================================================================
// Save check (-fvautotest -fvsavecheck): copies of real saves in Saved/SaveCheck/*.sav
// ================================================================================================
namespace FVSaveCheck
{
	void Add(TMap<FString, int64>& M, const FString& K, int64 V) { M.FindOrAdd(K) += V; }

	void AddStacks(TMap<FString, int64>& M, const TCHAR* Where, const TArray<FFVInvStack>& A)
	{
		for (const FFVInvStack& S : A)
		{
			Add(M, FString::Printf(TEXT("item %s %s q%d f%d"), Where, *S.ItemId.ToString(), int32(S.Quality), int32(S.Flags)), S.Count);
			Add(M, FString::Printf(TEXT("all %s"), *S.ItemId.ToString()), S.Count);
		}
	}

	/** Everything a player could miss after a load, as "key -> amount" (a missing key = 0). */
	TMap<FString, int64> Snap(const UFVSaveGame& P)
	{
		TMap<FString, int64> M;
		const FFVSaveU6& U = P.U6;
		M.Add(TEXT("money"), P.Money);
		M.Add(TEXT("xp"), P.Xp);
		M.Add(TEXT("xp seniority"), U.XpSeniority);
		M.Add(TEXT("xp total (xp + seniority)"), int64(P.Xp) + U.XpSeniority);
		M.Add(TEXT("xp earned"), U.XpEarned);
		M.Add(TEXT("level"), P.GetLevel());
		M.Add(TEXT("day"), P.Day);
		M.Add(TEXT("city visited"), U.bVisitedCity ? 1 : 0);
		M.Add(TEXT("stats fish caught"), P.Stats.FishCaught);
		AddStacks(M, TEXT("keepnet"), P.Keepnet);
		AddStacks(M, TEXT("backpack"), P.Backpack);
		AddStacks(M, TEXT("chest"), P.Chest);
		AddStacks(M, TEXT("icecellar"), P.IceCellar);
		AddStacks(M, TEXT("aquarium"), P.Aquarium);
		AddStacks(M, TEXT("artelstock"), U.ArtelStock);
		for (const FFVNetState& N : P.Nets)
		{
			Add(M, FString::Printf(TEXT("net placed %s:%d %s"), *N.Location.ToString(), N.Spot, *N.NetItem.ToString()), 1);
			Add(M, FString::Printf(TEXT("all %s"), *N.NetItem.ToString()), 1);
			AddStacks(M, TEXT("netcatch"), N.Catch);
		}
		for (const FFVNetState& N : U.ArtelNets)
		{
			Add(M, FString::Printf(TEXT("artel net %s:%d %s"), *N.Location.ToString(), N.Spot, *N.NetItem.ToString()), 1);
			Add(M, FString::Printf(TEXT("all %s"), *N.NetItem.ToString()), 1);
			AddStacks(M, TEXT("artelcatch"), N.Catch);
		}
		for (const FFVSmokeSlot& S : P.Smokehouse) { Add(M, FString::Printf(TEXT("smokehouse %s"), *S.FishId.ToString()), 1); Add(M, FString::Printf(TEXT("all %s"), *S.FishId.ToString()), 1); }
		for (const FFVTrophy& T : P.Trophies) { Add(M, FString::Printf(TEXT("trophy %s"), *T.FishId.ToString()), 1); Add(M, FString::Printf(TEXT("all %s"), *T.FishId.ToString()), 1); }
		for (const TPair<FName, int32>& B : P.Baits) { Add(M, FString::Printf(TEXT("bait %s"), *B.Key.ToString()), B.Value); }
		for (const FName& Id : P.OwnedRods) { Add(M, FString::Printf(TEXT("rod %s"), *Id.ToString()), 1); }
		for (const FName& Id : P.OwnedCosmetics) { Add(M, FString::Printf(TEXT("cosmetic %s"), *Id.ToString()), 1); }
		for (const FName& Id : P.OwnedDecor) { Add(M, FString::Printf(TEXT("decor %s"), *Id.ToString()), 1); }
		for (const FName& Id : P.EquippedDecor) { Add(M, FString::Printf(TEXT("decor equipped %s"), *Id.ToString()), 1); }
		for (const FName& Id : P.CompletedAchievements) { Add(M, FString::Printf(TEXT("achievement %s"), *Id.ToString()), 1); }
		for (const FName& Id : P.UnlockedLocations) { Add(M, FString::Printf(TEXT("location %s"), *Id.ToString()), 1); }
		for (const FName& Id : P.DiscoveredLures) { Add(M, FString::Printf(TEXT("lure %s"), *Id.ToString()), 1); }
		for (const FName& Id : P.LegendsMet) { Add(M, FString::Printf(TEXT("legend met %s"), *Id.ToString()), 1); }
		for (const TPair<FName, FFVFishRecord>& R : P.Records) { Add(M, FString::Printf(TEXT("record %s caught"), *R.Key.ToString()), R.Value.Caught); }
		for (const TPair<FName, int32>& S : P.StationLevels) { Add(M, FString::Printf(TEXT("station %s"), *S.Key.ToString()), S.Value); }
		for (const TPair<FName, int32>& S : U.YardBuildings) { Add(M, FString::Printf(TEXT("yard %s"), *S.Key.ToString()), S.Value); }
		for (const FName& Id : U.Blueprints) { Add(M, FString::Printf(TEXT("blueprint %s"), *Id.ToString()), 1); }
		for (const FName& Id : U.KnownFacts) { Add(M, FString::Printf(TEXT("fact %s"), *Id.ToString()), 1); }
		for (const FName& Id : U.RubbleCleared) { Add(M, FString::Printf(TEXT("rubble %s"), *Id.ToString()), 1); }
		for (const TPair<FName, FFVExpertLoadout>& L : U.ExpertLoadouts)
		{
			Add(M, FString::Printf(TEXT("expert tackle %s line %s"), *L.Key.ToString(), *L.Value.Line.ToString()), 1);
			Add(M, FString::Printf(TEXT("expert tackle %s float %s"), *L.Key.ToString(), *L.Value.Float.ToString()), 1);
			Add(M, FString::Printf(TEXT("expert tackle %s reel %s"), *L.Key.ToString(), *L.Value.Reel.ToString()), 1);
		}
		for (const TPair<FName, FFVRodLoadout>& L : P.RodLoadouts) { Add(M, FString::Printf(TEXT("rod loadout %s"), *L.Key.ToString()), 1); }
		for (const FFVCityTask& T : U.CityTasks) { Add(M, FString::Printf(TEXT("city task %s"), *T.Id.ToString()), 1); }
		for (const FFVOrder& O : P.Orders) { Add(M, FString::Printf(TEXT("order %s"), *O.Id.ToString()), 1); }
		for (const FFVGardenBed& B : U.Garden) { if (!B.Seed.IsNone()) { Add(M, FString::Printf(TEXT("garden %s"), *B.Seed.ToString()), 1); } }
		M.Add(TEXT("eq hat ") + P.EquippedHat.ToString(), 1);
		M.Add(TEXT("eq outfit ") + P.EquippedOutfit.ToString(), 1);
		M.Add(TEXT("eq bobber ") + P.EquippedBobber.ToString(), 1);
		M.Add(TEXT("eq pet ") + P.EquippedPet.ToString(), 1);
		M.Add(TEXT("eq rod ") + P.EquippedRod.ToString(), 1);
		M.Add(TEXT("eq bait ") + P.EquippedBait.ToString(), 1);
		M.Add(TEXT("keepnet level"), P.KeepnetLevel);
		M.Add(TEXT("backpack level"), P.BackpackLevel);
		M.Add(TEXT("shovel"), P.ShovelLevel);
		M.Add(TEXT("axe"), U.AxeLevel);
		M.Add(TEXT("pickaxe"), U.PickaxeLevel);
		M.Add(TEXT("boat stage"), U.BoatStage);
		M.Add(TEXT("saw queue"), U.SawQueue);
		M.Add(TEXT("artel hired"), U.bArtelHired ? 1 : 0);
		M.Add(TEXT("blockage cleared"), U.bBlockageCleared ? 1 : 0);
		M.Add(TEXT("expert types caught"), U.ExpertTypesCaught);
		M.Add(TEXT("boutique spent"), U.BoutiqueSpent);
		M.Add(TEXT("city tasks done"), U.CityTasksDone);
		M.Add(TEXT("trees chopped"), U.TreesChopped);
		M.Add(TEXT("harvests"), U.Harvests);
		M.Add(TEXT("forgings"), U.Forgings);
		M.Add(TEXT("excavations"), U.Excavations);
		M.Add(TEXT("mill bridge"), U.Mill.bBridgeRepaired ? 1 : 0);
		M.Add(TEXT("mill stage"), U.Mill.Stage);
		M.Add(TEXT("mill beaver feeds"), U.Mill.BeaverFeeds);
		M.Add(TEXT("mill pen"), U.Mill.Pen.Num());
		M.Add(TEXT("worms"), P.WormFarmWorms);
		M.Add(TEXT("crawlers"), P.WormFarmCrawlers);
		M.Add(TEXT("online id"), P.OnlineId);
		M.Add(TEXT("name ") + P.PlayerName, 1);
		M.Add(TEXT("current location ") + P.CurrentLocation.ToString(), 1);
		M.Add(TEXT("u6 version"), U.U6Version);
		return M;
	}

	/** "key: a -> b" lines of the differences; OutLost counts the keys that went down (or vanished). */
	TArray<FString> Diff(const TMap<FString, int64>& A, const TMap<FString, int64>& B, int32& OutLost)
	{
		OutLost = 0;
		TArray<FString> Keys;
		for (const TPair<FString, int64>& P : A) { Keys.AddUnique(P.Key); }
		for (const TPair<FString, int64>& P : B) { Keys.AddUnique(P.Key); }
		Keys.Sort();
		TArray<FString> Out;
		for (const FString& K : Keys)
		{
			const int64 Va = A.Contains(K) ? A[K] : 0;
			const int64 Vb = B.Contains(K) ? B[K] : 0;
			if (Va == Vb) { continue; }
			if (Vb < Va) { ++OutLost; }
			Out.Add(FString::Printf(TEXT("%s: %lld -> %lld"), *K, (long long)Va, (long long)Vb));
		}
		return Out;
	}

	int64 Get(const TMap<FString, int64>& M, const TCHAR* K)
	{
		const int64* V = M.Find(K);
		return V ? *V : 0;
	}

	void LogDiff(const FString& What, const TArray<FString>& Lines)
	{
		for (const FString& L : Lines) { UE_LOG(LogFV, Log, TEXT("FV: SAVECHECK %s | %s"), *What, *L); }
	}

	UFVSaveGame* FromBytes(const TArray<uint8>& Bytes)
	{
		return Cast<UFVSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	}

	bool ToBytes(UFVSaveGame* P, TArray<uint8>& Out)
	{
		P->Version = UFVSaveGame::CurrentVersion;   // as UFVSaveGame::Save does
		Out.Reset();
		return UGameplayStatics::SaveGameToMemory(P, Out) && Out.Num() > 0;
	}

	bool IsU6Location(FName Id) { return Id == FVDBU6::ForestId() || Id == FVDBU6::CityId() || Id == FVDBU6::CaveId(); }

	FString Dir() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveCheck")); }
	FString AutoTestSlotFile() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames") / (UFVSaveGame::GetSlotName() + TEXT(".sav"))); }
}

void FVAutoTest::SaveCheck(bool bOk, const FString& What)
{
	if (bOk)
	{
		++SaveCheckOk;
		UE_LOG(LogFV, Log, TEXT("FV: SAVECHECK %s: ok"), *What);
	}
	else
	{
		++SaveCheckFailed;
		++Warnings;
		UE_LOG(LogFV, Warning, TEXT("FV: AUTOTEST WARNING savecheck %s: FAILED"), *What);
	}
}

void FVAutoTest::AddSaveCheckSteps()
{
	using namespace FVSaveCheck;
	// never the player's slot: the autotest slot only (UFVGame::Init set it for -fvautotest)
	if (UFVSaveGame::GetSlotName() != TEXT("FishingValley_AutoTest"))
	{
		UE_LOG(LogFV, Error, TEXT("FV: SAVECHECK refused: the save slot is not the autotest one"));
		return;
	}
	IFileManager::Get().FindFiles(SaveCheckFiles, *(Dir() / TEXT("*.sav")), true, false);
	SaveCheckFiles.Sort();
	UE_LOG(LogFV, Log, TEXT("FV: SAVECHECK %d copies in %s"), SaveCheckFiles.Num(), *Dir());

	// 1. data checks from memory (one frame): load, fixup, nothing lost, idempotent; every update-6 copy also as 0.5
	AddStep(TEXT("savecheck: memory"), [this]()
	{
		auto CheckBytes = [this](const FString& Name, const TArray<uint8>& Raw)
		{
			UFVSaveGame* P = FromBytes(Raw);
			SaveCheck(P != nullptr, FString::Printf(TEXT("%s: loads (%d bytes)"), *Name, Raw.Num()));
			if (!P) { return; }
			const int32 VerBefore = P->Version;
			const int32 U6Before = P->U6.U6Version;
			const FName Loc0 = P->CurrentLocation;
			const bool bLocOpen0 = P->IsLocationUnlocked(Loc0);
			const TMap<FString, int64> S0 = Snap(*P);
			P->FixupAfterLoad();
			const TMap<FString, int64> S1 = Snap(*P);
			int32 Lost = 0;
			const TArray<FString> D01 = Diff(S0, S1, Lost);
			UE_LOG(LogFV, Log, TEXT("FV: SAVECHECK %s: version %d, U6Version %d -> %d, level %d, xp %d (+%d seniority), %lld coins, city %d, %d diffs by the fixup (%d went down)"),
				*Name, VerBefore, U6Before, P->U6.U6Version, P->GetLevel(), P->Xp, P->U6.XpSeniority, (long long)P->Money, P->U6.bVisitedCity ? 1 : 0, D01.Num(), Lost);
			LogDiff(Name + TEXT(" fixup"), D01);
			SaveCheck(!bLocOpen0 || P->CurrentLocation == Loc0, FString::Printf(TEXT("%s: the player stays where he saved (%s -> %s)"), *Name, *Loc0.ToString(), *P->CurrentLocation.ToString()));
			// XP: moved to «стаж» only (the sum stays); level <= 10 before the City, >= 11 after it
			const int64 XpSum0 = Get(S0, TEXT("xp total (xp + seniority)"));
			const int64 XpSum1 = Get(S1, TEXT("xp total (xp + seniority)"));
			SaveCheck(P->U6.bVisitedCity ? XpSum1 >= XpSum0 : XpSum1 == XpSum0, FString::Printf(TEXT("%s: xp + seniority kept (%lld -> %lld)"), *Name, (long long)XpSum0, (long long)XpSum1));
			SaveCheck(P->U6.bVisitedCity ? P->GetLevel() >= FVDBU6::FrozenLevel() + 1 : P->GetLevel() <= FVDBU6::FrozenLevel(),
				FString::Printf(TEXT("%s: level %d fits the City flag %d"), *Name, P->GetLevel(), P->U6.bVisitedCity ? 1 : 0));
			SaveCheck(P->Money >= Get(S0, TEXT("money")), FString::Printf(TEXT("%s: coins kept (%lld -> %lld)"), *Name, (long long)Get(S0, TEXT("money")), (long long)P->Money));
			// nothing the player owns may go down (items as totals per id: a net taken off a spot goes to the bags)
			TArray<FString> LostOwned;
			for (const TPair<FString, int64>& KV : S0)
			{
				const FString& K = KV.Key;
				const bool bOwned = K.StartsWith(TEXT("all ")) || K.StartsWith(TEXT("cosmetic ")) || K.StartsWith(TEXT("decor "))
					|| K.StartsWith(TEXT("achievement ")) || K.StartsWith(TEXT("rod ")) || K.StartsWith(TEXT("record ")) || K.StartsWith(TEXT("bait "))
					|| K.StartsWith(TEXT("station ")) || K.StartsWith(TEXT("yard ")) || K.StartsWith(TEXT("blueprint ")) || K.StartsWith(TEXT("fact "))
					|| K.StartsWith(TEXT("location ")) || K.StartsWith(TEXT("lure ")) || K.StartsWith(TEXT("legend met "))
					|| K == TEXT("stats fish caught") || K == TEXT("keepnet level") || K == TEXT("backpack level") || K == TEXT("shovel")
					|| K == TEXT("axe") || K == TEXT("pickaxe") || K == TEXT("boat stage") || K == TEXT("artel hired") || K.StartsWith(TEXT("mill "));
				const int64 After = Get(S1, *K);
				if (bOwned && After < KV.Value) { LostOwned.Add(FString::Printf(TEXT("%s %lld -> %lld"), *K, (long long)KV.Value, (long long)After)); }
			}
			SaveCheck(LostOwned.Num() == 0, FString::Printf(TEXT("%s: nothing owned lost by the fixup%s%s"), *Name,
				LostOwned.Num() ? TEXT(": ") : TEXT(""), *FString::Join(LostOwned, TEXT("; "))));
			SaveCheck(P->U6.U6Version > 0, FString::Printf(TEXT("%s: migrated to update 6 (U6Version %d)"), *Name, P->U6.U6Version));

			// idempotent: in place, and save -> load -> fixup (what the next start does)
			TArray<uint8> M1, M2, M3;
			const bool bM1 = ToBytes(P, M1);
			P->FixupAfterLoad();
			const bool bM3 = ToBytes(P, M3);
			int32 L13 = 0;
			const TArray<FString> D13 = Diff(S1, Snap(*P), L13);
			LogDiff(Name + TEXT(" 2nd fixup in place"), D13);
			SaveCheck(bM1 && bM3 && M1 == M3 && D13.Num() == 0, FString::Printf(TEXT("%s: a second fixup changes nothing (%d / %d bytes, %d diffs)"), *Name, M1.Num(), M3.Num(), D13.Num()));
			UFVSaveGame* Q = FromBytes(M1);
			if (!Q)
			{
				SaveCheck(false, FString::Printf(TEXT("%s: the re-saved bytes load"), *Name));
				return;
			}
			int32 LQ = 0;
			const TArray<FString> DQ0 = Diff(S1, Snap(*Q), LQ);
			LogDiff(Name + TEXT(" saved -> loaded (before fixup)"), DQ0);
			Q->FixupAfterLoad();
			const bool bM2 = ToBytes(Q, M2);
			const TArray<FString> DQ = Diff(S1, Snap(*Q), LQ);
			LogDiff(Name + TEXT(" reload"), DQ);
			SaveCheck(DQ0.Num() == 0, FString::Printf(TEXT("%s: the saved bytes load back the same profile (%d diffs)"), *Name, DQ0.Num()));
			SaveCheck(bM2 && M1 == M2 && DQ.Num() == 0, FString::Printf(TEXT("%s: reload + fixup changes nothing (%d / %d bytes, %d diffs)"), *Name, M1.Num(), M2.Num(), DQ.Num()));
		};

		for (const FString& F : SaveCheckFiles)
		{
			TArray<uint8> Raw;
			if (!FFileHelper::LoadFileToArray(Raw, *(Dir() / F)))
			{
				SaveCheck(false, FString::Printf(TEXT("%s: read"), *F));
				continue;
			}
			CheckBytes(F, Raw);
			// saved in the forest (a big location outside FVDB::Locations()): he must load there again
			UFVSaveGame* RawF = FromBytes(Raw);
			if (RawF && RawF->IsLocationUnlocked(FVDBU6::ForestId()) && RawF->CurrentLocation != FVDBU6::ForestId())
			{
				RawF->CurrentLocation = FVDBU6::ForestId();
				TArray<uint8> BF;
				if (ToBytes(RawF, BF)) { CheckBytes(F + TEXT(" [in forest]"), BF); }
			}
			// the same profile as a 0.5 save would bring it (no update-6 block): the full 0.5 migration on real data
			UFVSaveGame* Raw5 = FromBytes(Raw);
			if (Raw5 && Raw5->U6.U6Version > 0)
			{
				Raw5->U6 = FFVSaveU6();
				Raw5->UnlockedLocations.RemoveAll([](const FName& Id) { return IsU6Location(Id); });
				if (IsU6Location(Raw5->CurrentLocation)) { Raw5->CurrentLocation = NAME_None; }
				TArray<uint8> B5;
				if (ToBytes(Raw5, B5)) { CheckBytes(F + TEXT(" [as 0.5]"), B5); }
			}
		}
	}, nullptr, FString(), 0.2f, 60.f);

	// 2. every copy played through the autotest slot: Continue, a few screens, back to the menu (saves), reload
	for (int32 i = 0; i < SaveCheckFiles.Num(); ++i)
	{
		const FString F = SaveCheckFiles[i];
		const FString Slug = FPaths::GetBaseFilename(F);
		AddStep(FString::Printf(TEXT("savecheck: continue %s"), *F), [this, F]()
		{
			DismissModals();
			if (Game->IsInGame()) { Game->ReturnToMainMenu(); }
			const bool bCopied = IFileManager::Get().Copy(*AutoTestSlotFile(), *(Dir() / F), true, true) == COPY_OK;
			SaveCheck(bCopied, FString::Printf(TEXT("%s: copied into the autotest slot"), *F));
			const bool bOk = bCopied && Game->ContinueGame();
			SaveCheck(bOk, FString::Printf(TEXT("%s: Continue enters the world"), *F));
			SaveCheckSnap = (bOk && Game->GetProfile()) ? Snap(*Game->GetProfile()) : TMap<FString, int64>();
		}, [](float T) { return T > 2.f; }, TEXT("savecheck_") + Slug + TEXT("_world"), 0.5f, 10.f);
		AddStep(FString::Printf(TEXT("savecheck: inventory %s"), *F), [this]() { DismissModals(); Game->OpenScreen(EFVScreen::Inventory); },
			[](float T) { return T > 0.6f; }, TEXT("savecheck_") + Slug + TEXT("_inventory"), 0.3f, 5.f);
		AddStep(FString::Printf(TEXT("savecheck: profile %s"), *F), [this]() { Game->OpenScreen(EFVScreen::None); Game->OpenScreen(EFVScreen::Profile); },
			[](float T) { return T > 0.6f; }, TEXT("savecheck_") + Slug + TEXT("_profile"), 0.3f, 5.f);
		AddStep(FString::Printf(TEXT("savecheck: wardrobe %s"), *F), [this]() { Game->OpenScreen(EFVScreen::None); Game->OpenScreen(EFVScreen::Wardrobe); },
			[](float T) { return T > 0.6f; }, TEXT("savecheck_") + Slug + TEXT("_wardrobe"), 0.3f, 5.f);
		AddStep(FString::Printf(TEXT("savecheck: menu + reload %s"), *F), [this, F]()
		{
			Game->OpenScreen(EFVScreen::None);
			DismissModals();
			const TMap<FString, int64> Played = Game->GetProfile() ? Snap(*Game->GetProfile()) : TMap<FString, int64>();
			int32 LostC = 0;
			LogDiff(F + TEXT(" continue -> played"), Diff(SaveCheckSnap, Played, LostC));
			Game->ReturnToMainMenu();   // saves into the autotest slot
			UFVSaveGame* Back = UFVSaveGame::Load();
			SaveCheck(Back != nullptr, FString::Printf(TEXT("%s: the game's own save loads back"), *F));
			if (!Back) { return; }
			int32 Lost = 0;
			const TArray<FString> D = Diff(Played, Snap(*Back), Lost);
			LogDiff(F + TEXT(" in-game save -> load"), D);
			SaveCheck(D.Num() == 0, FString::Printf(TEXT("%s: the in-game save reloads the same profile (%d diffs)"), *F, D.Num()));
		}, [](float T) { return T > 0.5f; }, FString(), 0.3f, 10.f);
	}
	AddStep(TEXT("savecheck: summary"), [this]()
	{
		UE_LOG(LogFV, Log, TEXT("FV: SAVECHECK summary: %d ok, %d failed, %d copies"), SaveCheckOk, SaveCheckFailed, SaveCheckFiles.Num());
	}, nullptr, FString(), 0.1f, 5.f);
}
