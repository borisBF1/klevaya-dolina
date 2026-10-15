// First-fishing onboarding (update 4b, Docs/UPDATE4_ONLINE.md §11): big friendly coach marks — a speech bubble and a
// bouncing arrow — that lead a new fisherman from the practice pond of his yard to the first trip to the village pond.
// State: UFVSaveGame::OnboardingStep (0 = not started, 1..7 = the furthest step reached, 100 = done). The stage shown
// is derived live from the scene / fishing state, so a reloaded save or an escaped fish always shows the right hint.

#include "FVOnboarding.h"
#include "FVGame.h"
#include "FVScene.h"
#include "FVSceneArt.h"
#include "FVFishing.h"
#include "FVDraw.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVAudio.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace FVOnboardingImpl
{
	constexpr int32 ObStepDone = 100;
	// saved steps (the furthest reached)
	constexpr int32 ObStepGoToPond = 1, ObStepCast = 2, ObStepWait = 3, ObStepReel = 4, ObStepCaught = 5, ObStepOpenMap = 6, ObStepMapOpened = 7;

	/** What the coach mark shows right now. */
	enum class EObStage : uint8 { None, GoToPond, LeaveHouse, Cast, Charging, Wait, Bite, Reel, Card, OpenMap, ChoosePond };

	struct FObState
	{
		EObStage Shown = EObStage::None;   // stage drawn last frame (the bubble pops in when it changes)
		float Age = 0.f;               // seconds the current stage has been shown
	};
	FObState& ObState()
	{
		static FObState S;
		return S;
	}

	/** The coach marks are driven only by UFVSaveGame::OnboardingStep (the autotest sets 0 to test them, 100 to skip). */
	bool ObAllowed(const UFVGame* G)
	{
		return G != nullptr;
	}

	bool ObWorldVisible(UFVGame* G)
	{
		return G->IsInGame() && G->GetScreen() == EFVScreen::None && !G->HasModal();
	}

	void ObSfx(EFVSfx S, float Volume = 1.f, float Pitch = 1.f)
	{
		if (UFVAudio* A = UFVAudio::Get()) { A->Play(S, Volume, Pitch); }
	}

	EObStage ObStageNow(UFVGame* G, int32 Step)
	{
		FVScene& S = G->GetScene();
		if (S.GetLocationId().IsNone() || S.HasWater()) { return EObStage::None; }
		if (Step >= ObStepMapOpened) { return EObStage::ChoosePond; }
		if (Step >= ObStepOpenMap) { return EObStage::OpenMap; }
		if (S.IsInterior()) { return Step >= ObStepCaught ? EObStage::OpenMap : EObStage::LeaveHouse; }
		if (!S.IsHome()) { return EObStage::None; }
		switch (G->GetFishing().GetState())
		{
		case EFVFishState::Showcase: return EObStage::Card;
		case EFVFishState::Minigame: return EObStage::Reel;
		case EFVFishState::Bite: return EObStage::Bite;
		case EFVFishState::Casting:
		case EFVFishState::Waiting: return EObStage::Wait;
		case EFVFishState::Charging: return EObStage::Charging;
		case EFVFishState::Escaped:
		case EFVFishState::Retrieving: return EObStage::Cast;
		case EFVFishState::Idle:
		default:
			if (Step >= ObStepCaught) { return EObStage::OpenMap; }
			return S.CanFishFromHere() ? EObStage::Cast : EObStage::GoToPond;
		}
	}

	/** 0..4: which of the five dots is lit. */
	int32 ObDot(EObStage St)
	{
		switch (St)
		{
		case EObStage::GoToPond: case EObStage::LeaveHouse: return 0;
		case EObStage::Cast: case EObStage::Charging: return 1;
		case EObStage::Wait: case EObStage::Bite: return 2;
		case EObStage::Reel: case EObStage::Card: return 3;
		default: return 4;
		}
	}

	struct FObText { FString Title; FString Body; EFVIcon Icon = EFVIcon::Fish; };

	FObText ObTextFor(EObStage St, bool bTouch, bool bRetry)
	{
		FObText T;
		const TCHAR* Press = bTouch ? TEXT("коснитесь экрана") : TEXT("жмите [Пробел] или ЛКМ");
		switch (St)
		{
		case EObStage::GoToPond:
			T.Title = TEXT("Подойдите к учебному пруду");
			T.Body = bTouch ? TEXT("Ведите пальцем по экрану, чтобы идти, и встаньте на мостки — туда, куда показывает стрелка.")
				: TEXT("Ходите клавишами [WASD] или стрелками и встаньте на мостки — туда, куда показывает стрелка.");
			T.Icon = EFVIcon::Rod;
			break;
		case EObStage::LeaveHouse:
			T.Title = TEXT("Выйдите во двор");
			T.Body = bTouch ? TEXT("Учебный пруд — прямо у дома. Подойдите к двери и коснитесь её.")
				: TEXT("Учебный пруд — прямо у дома. Подойдите к двери и нажмите [E].");
			T.Icon = EFVIcon::Door;
			break;
		case EObStage::Cast:
			T.Title = bRetry ? TEXT("Попробуйте ещё раз!") : TEXT("Зажмите и отпустите, чтобы забросить");
			T.Body = bTouch ? TEXT("Зажмите палец на экране — над головой растёт шкала силы. Отпустите, и поплавок полетит в воду.")
				: TEXT("Зажмите [Пробел] или ЛКМ — над головой растёт шкала силы. Отпустите, и поплавок полетит в воду.");
			T.Icon = EFVIcon::Rod;
			break;
		case EObStage::Charging:
			T.Title = bTouch ? TEXT("Теперь отпустите палец!") : TEXT("Теперь отпустите!");
			T.Body = TEXT("Чем полнее шкала силы, тем дальше улетит поплавок.");
			T.Icon = EFVIcon::Rod;
			break;
		case EObStage::Wait:
			T.Title = TEXT("Ждите поклёвку");
			T.Body = FString::Printf(TEXT("Когда над головой появится «!» — сразу %s!"), Press);
			T.Icon = EFVIcon::Clock;
			break;
		case EObStage::Bite:
			T.Title = TEXT("Клюёт! Нажимайте!");
			T.Body = bTouch ? TEXT("Коснитесь экрана — подсеките рыбку!") : TEXT("Жмите [Пробел] или ЛКМ — подсеките рыбку!");
			T.Icon = EFVIcon::Exclaim;
			break;
		case EObStage::Reel:
			T.Title = TEXT("Держите рыбку в зелёной полосе");
			T.Body = bTouch ? TEXT("Держите палец — полоса вверх, отпустите — вниз. Шкала справа заполнится — рыбка ваша!")
				: TEXT("Зажали [Пробел] или ЛКМ — полоса вверх, отпустили — вниз. Шкала справа заполнится — рыбка ваша!");
			T.Icon = EFVIcon::Fish;
			break;
		case EObStage::OpenMap:
			T.Title = TEXT("Настоящая рыба ждёт на Деревенском пруду");
			T.Body = bTouch ? TEXT("Просто пройдите по дороге — через калитку внизу двора. Или откройте Карту — кнопка со стрелкой.")
				: TEXT("Просто пройдите по дороге — через калитку внизу двора. Или откройте Карту — кнопка со стрелкой или [M].");
			T.Icon = EFVIcon::Map;
			break;
		case EObStage::ChoosePond:
			T.Title = TEXT("Выберите на Карте Деревенский пруд");
			T.Body = bTouch ? TEXT("Откройте Карту и коснитесь пруда — и в путь! Там клюёт настоящая рыба.")
				: TEXT("Откройте Карту [M] и нажмите на пруд — и в путь! Там клюёт настоящая рыба.");
			T.Icon = EFVIcon::Map;
			break;
		default:
			break;
		}
		return T;
	}

	/** Greedy word wrap (a long word stays on its own line). */
	TArray<FString> ObWrap(const FVDraw& D, const FString& Text, float MaxW, float Size, bool bBold)
	{
		TArray<FString> Words;
		Text.ParseIntoArray(Words, TEXT(" "), true);
		TArray<FString> Lines;
		FString Cur;
		for (const FString& Wd : Words)
		{
			const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
			if (!Cur.IsEmpty() && D.MeasureText(Try, Size, bBold).X > MaxW)
			{
				Lines.Add(Cur);
				Cur = Wd;
			}
			else
			{
				Cur = Try;
			}
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
		return Lines;
	}

	FORCEINLINE float ObPx(float V) { return FMath::RoundToFloat(V); }
	FORCEINLINE FLinearColor ObFade(FLinearColor C, float A) { C.A = FMath::Clamp(A, 0.f, 1.f); return C; }

	// ---- colours of the speech bubble
	FLinearColor ObBorderC() { return FV::Col(92, 52, 28); }
	FLinearColor ObFillC() { return FV::Col(255, 247, 226); }
	FLinearColor ObShadeC() { return FV::Col(240, 222, 186); }

	/** Pixel rounded rectangle (3 px corners). */
	void ObRoundRect(FVDraw& D, const FFVRect& Q, const FLinearColor& C)
	{
		if (Q.W < 8.f || Q.H < 8.f) { D.Rect(Q, C); return; }
		D.Rect(FFVRect(Q.X + 3.f, Q.Y, Q.W - 6.f, Q.H), C);
		D.Rect(FFVRect(Q.X + 1.f, Q.Y + 1.f, Q.W - 2.f, Q.H - 2.f), C);
		D.Rect(FFVRect(Q.X, Q.Y + 3.f, Q.W, Q.H - 6.f), C);
	}

	/** The speech bubble body: soft shadow, dark wooden outline, cream fill, a light top edge. */
	void ObBubble(FVDraw& D, const FFVRect& R)
	{
		ObRoundRect(D, R.Offset(2.f, 3.f), FV::Col(0, 0, 0, 70));
		ObRoundRect(D, R, ObBorderC());
		ObRoundRect(D, R.Inset(2.f), ObFillC());
		D.Rect(FFVRect(R.X + 5.f, R.Y + 2.f, R.W - 10.f, 1.f), FV::Col(255, 255, 255, 200));
		D.Rect(FFVRect(R.X + 5.f, R.Bottom() - 3.f, R.W - 10.f, 1.f), ObShadeC());
	}

	/** Tail below the bubble, leaning towards Target (drawn after the bubble: it opens the bottom border). */
	void ObTail(FVDraw& D, const FFVRect& R, const FVector2D& Target, float Scale)
	{
		const float BaseX = FMath::Clamp(float(Target.X), R.X + 18.f, R.Right() - 18.f);
		const float Lean = FMath::Clamp(float(Target.X - BaseX) / 40.f, -0.8f, 0.8f);
		const int32 N = FMath::RoundToInt(7.f * Scale);
		for (int32 k = 0; k < N + 2; ++k)
		{
			// rows 0-1 open the bubble's bottom border, the rest narrow down to the tip
			const float HalfW = FMath::Max(1.f, float(N + 1 - k) * 0.9f);
			const float CX = ObPx(BaseX + Lean * float(FMath::Max(0, k - 2)));
			const float Y = R.Bottom() - 2.f + float(k);
			if (k >= 2) { D.Rect(FFVRect(ObPx(CX - HalfW - 2.f), Y, ObPx(HalfW * 2.f + 4.f), 1.f), ObBorderC()); }
			if (k < N + 1) { D.Rect(FFVRect(ObPx(CX - HalfW), Y, ObPx(HalfW * 2.f), 1.f), ObFillC()); }
		}
		D.Rect(FFVRect(ObPx(BaseX + Lean * float(N - 1)) - 1.f, R.Bottom() + float(N), 2.f, 2.f), ObBorderC());
	}

	/** Chunky pixel arrow (outlined gold) whose tip is at Tip; bDown = pointing down. U = size of one art pixel. */
	void ObArrow(FVDraw& D, const FVector2D& Tip, bool bDown, float U)
	{
		TArray<FFVRect, TInlineAllocator<12>> Rows;
		auto Row = [&Tip, bDown, U](int32 R, int32 C0, int32 C1)
		{
			const float Y = bDown ? float(Tip.Y) - float(R + 1) * U : float(Tip.Y) + float(R) * U;
			return FFVRect(ObPx(float(Tip.X) + (float(C0) - 0.5f) * U), ObPx(Y), float(C1 - C0 + 1) * U, U);
		};
		for (int32 R = 0; R < 5; ++R) { Rows.Add(Row(R, -R, R)); }
		for (int32 R = 5; R < 9; ++R) { Rows.Add(Row(R, -1, 1)); }
		for (const FFVRect& Q : Rows) { D.Rect(Q.Offset(U, U), FV::Col(0, 0, 0, 60)); }
		for (const FFVRect& Q : Rows) { D.Rect(FFVRect(Q.X - U, Q.Y - U, Q.W + 2.f * U, Q.H + 2.f * U), FV::Col(92, 44, 18)); }
		for (const FFVRect& Q : Rows) { D.Rect(Q, FV::Col(255, 206, 64)); }
		// a light edge on the left, a warm shade on the right
		for (int32 R = 1; R < 9; ++R)
		{
			const FFVRect& Q = Rows[R];
			D.Rect(FFVRect(Q.X, Q.Y, U, U), FV::Col(255, 246, 180));
			D.Rect(FFVRect(Q.Right() - U, Q.Y, U, U), FV::Col(236, 150, 40));
		}
	}

	/** Update 6 (§12.1): the same arrow lying on its side, pointing RIGHT with its tip at Tip (keys of the HUD column at
	    the right edge of the landscape screen). */
	void ObArrowRight(FVDraw& D, const FVector2D& Tip, float U)
	{
		TArray<FFVRect, TInlineAllocator<12>> Cols;
		auto Col = [&Tip, U](int32 C, int32 R0, int32 R1)
		{
			const float X = float(Tip.X) - float(C + 1) * U;
			return FFVRect(ObPx(X), ObPx(float(Tip.Y) + (float(R0) - 0.5f) * U), U, float(R1 - R0 + 1) * U);
		};
		for (int32 C = 0; C < 5; ++C) { Cols.Add(Col(C, -C, C)); }
		for (int32 C = 5; C < 9; ++C) { Cols.Add(Col(C, -1, 1)); }
		for (const FFVRect& Q : Cols) { D.Rect(Q.Offset(U, U), FV::Col(0, 0, 0, 60)); }
		for (const FFVRect& Q : Cols) { D.Rect(FFVRect(Q.X - U, Q.Y - U, Q.W + 2.f * U, Q.H + 2.f * U), FV::Col(92, 44, 18)); }
		for (const FFVRect& Q : Cols) { D.Rect(Q, FV::Col(255, 206, 64)); }
		// a light edge on the top, a warm shade at the bottom
		for (int32 C = 1; C < 9; ++C)
		{
			const FFVRect& Q = Cols[C];
			D.Rect(FFVRect(Q.X, Q.Y, U, U), FV::Col(255, 246, 180));
			D.Rect(FFVRect(Q.X, Q.Bottom() - U, U, U), FV::Col(236, 150, 40));
		}
	}

	/** Arrow bouncing towards a target rect: above it pointing down, or below it pointing up when there is no room. */
	void ObPointAt(FVDraw& D, const FFVRect& Target, float T, float U, bool bPreferBelow = false)
	{
		const float Len = 9.f * U + 4.f;
		const float Bounce = ObPx(FMath::Abs(FMath::Sin(T * 4.5f)) * 2.5f * U);
		const bool bRoomAbove = Target.Y - Len - 6.f > FV::TopBarH + 2.f;
		const bool bBelow = bPreferBelow || !bRoomAbove;
		const float X = ObPx(Target.Center().X);
		if (bBelow) { ObArrow(D, FVector2D(X, Target.Bottom() + 3.f + Bounce), false, U); }
		else { ObArrow(D, FVector2D(X, Target.Y - 3.f - Bounce), true, U); }
	}

	/** Pulsing marker on the ground where the player should stand. */
	void ObGroundMark(FVDraw& D, const FVector2D& P, float T, float K)
	{
		const float F = FMath::Frac(T * 0.8f);
		D.Glow(P, 26.f * K, FLinearColor(0.35f, 0.3f, 0.12f, 0.5f));
		D.Ring(P, (10.f + 16.f * F) * K, (4.f + 6.f * F) * K, ObFade(FV::Col(255, 226, 120), (1.f - F) * 0.9f), 2.f);
		D.Ring(P, 11.f * K, 4.5f * K, ObFade(FV::Col(255, 236, 150), 0.6f + 0.3f * FMath::Sin(T * 5.f)), 2.f);
	}

	void ObFinish(UFVGame* G, bool bPraise)
	{
		UFVSaveGame* P = G ? G->GetProfile() : nullptr;
		if (!P) { return; }
		P->OnboardingStep = ObStepDone;
		G->NotifyProfileChanged();
		ObState().Shown = EObStage::None;
		if (bPraise) { G->Toast(TEXT("Обучение пройдено! Удачного клёва!"), FVPal::Gold(), EFVIcon::Check); }
		UE_LOG(LogFV, Log, TEXT("FV: onboarding finished (%s)"), bPraise ? TEXT("travelled to the pond") : TEXT("left home early"));
	}
}

// =============================================================================================
// API (Private/FVOnboarding.h)
// =============================================================================================
bool FVOnboarding::IsActive(UFVGame* Game)
{
	using namespace FVOnboardingImpl;
	const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
	return P && P->OnboardingStep < ObStepDone && Game->IsInGame() && !Game->GetScene().GetLocationId().IsNone() && ObAllowed(Game);
}

void FVOnboarding::Skip(UFVGame* Game)
{
	using namespace FVOnboardingImpl;
	UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
	if (!P || P->OnboardingStep >= ObStepDone) { return; }
	P->OnboardingStep = ObStepDone;
	Game->NotifyProfileChanged();
	ObState().Shown = EObStage::None;
	Game->Toast(TEXT("Обучение пропущено. Подсказки всегда в справке «?»"), FVPal::White(), EFVIcon::Question);
	UE_LOG(LogFV, Log, TEXT("FV: onboarding skipped"));
}

void FVOnboarding::Tick(UFVGame* Game, float Dt)
{
	using namespace FVOnboardingImpl;
	if (!IsActive(Game)) { return; }
	UFVSaveGame* P = Game->GetProfile();
	FVScene& S = Game->GetScene();
	int32 Step = FMath::Clamp(P->OnboardingStep, 0, ObStepDone);
	const int32 Before = Step;

	// the map screen was opened after the practice catch
	if (Game->GetScreen() == EFVScreen::Map && Step >= ObStepOpenMap) { Step = FMath::Max(Step, ObStepMapOpened); }

	if (ObWorldVisible(Game))
	{
		if (S.HasWater())
		{
			// arrived at a real fishing place: the lesson is over (praised when it was followed to the end)
			ObFinish(Game, Step >= ObStepCaught);
			return;
		}
		Step = FMath::Max(Step, ObStepGoToPond);
		if (S.IsHome())
		{
			const EFVFishState FS = Game->GetFishing().GetState();
			switch (FS)
			{
			case EFVFishState::Showcase: Step = FMath::Max(Step, ObStepCaught); break;
			case EFVFishState::Minigame: Step = FMath::Max(Step, ObStepReel); break;
			case EFVFishState::Bite:
			case EFVFishState::Waiting:
			case EFVFishState::Casting: Step = FMath::Max(Step, ObStepWait); break;
			case EFVFishState::Charging: Step = FMath::Max(Step, ObStepCast); break;
			case EFVFishState::Idle:
				if (Step == ObStepCaught) { Step = ObStepOpenMap; }   // the success card was closed
				else if (S.CanFishFromHere()) { Step = FMath::Max(Step, ObStepCast); }
				break;
			default: break;
			}
		}
	}
	if (Step != Before)
	{
		P->OnboardingStep = Step;
		Game->NotifyProfileChanged();
	}
	(void)Dt;
}

void FVOnboarding::Draw(UFVGame* Game, FVDraw& D)
{
	using namespace FVOnboardingImpl;
	FObState& St = ObState();
	if (!IsActive(Game) || !ObWorldVisible(Game))
	{
		St.Shown = EObStage::None;
		return;
	}
	UFVSaveGame* P = Game->GetProfile();
	FVScene& S = Game->GetScene();
	const int32 Step = FMath::Clamp(P->OnboardingStep, 0, ObStepDone);
	const EObStage Stage = ObStageNow(Game, Step);
	if (Stage == EObStage::None || Stage == EObStage::Card)
	{
		// the practice card of the fishing module is the lesson's "well done" page
		St.Shown = Stage;
		return;
	}
	if (Stage != St.Shown)
	{
		const bool bFirst = St.Shown == EObStage::None;
		St.Shown = Stage;
		St.Age = 0.f;
		if (!bFirst && Stage != EObStage::Charging && Stage != EObStage::Bite) { ObSfx(EFVSfx::Hover, 0.5f, 1.25f); }
	}
	else
	{
		St.Age += D.FrameDt();
	}

	const bool bPortrait = FV::IsPortrait();
	const bool bTouch = FV::IsTouchUI();
	const float T = float(D.Time);
	const float U = bPortrait ? 3.f : 2.f;           // arrow pixel
	UFVSprites* Sp = UFVSprites::Get();
	const FVPracticePond::FInfo Pond = FVPracticePond::Get(S);
	// update 6 (§2): the scene answers in world px, the coach marks are drawn in screen px (above the HUD)
	const FVector2D Player = S.WorldToScreen(S.GetPlayerPos());
	const FVector2D Head = S.WorldToScreen(S.GetHeadTop());

	// ---------------------------------------------------------------- what the coach points at
	FVector2D TailTarget(-1.f, -1.f);
	switch (Stage)
	{
	case EObStage::GoToPond:
		if (Pond.bValid)
		{
			const FVector2D Spot = S.WorldToScreen(Pond.Spot);
			ObGroundMark(D, Spot, T, bPortrait ? 1.2f : 1.f);
			ObPointAt(D, FFVRect(float(Spot.X) - 6.f, float(Spot.Y) - 12.f, 12.f, 12.f), T, U);
			TailTarget = Spot - FVector2D(0.f, 40.f);
		}
		break;
	case EObStage::LeaveHouse:
		if (const FFVWorldObject* Door = S.FindObject(EFVObjKind::Door, 0))
		{
			const FVector2D DoorPos = S.WorldToScreen(Door->Hit.Pos());
			const FFVRect DoorR(float(DoorPos.X), float(DoorPos.Y), Door->Hit.W, Door->Hit.H);
			ObPointAt(D, DoorR, T, U);
			TailTarget = DoorR.Center();
		}
		break;
	case EObStage::Cast:
		// a little hand that presses and holds next to the fisherman
		if (Sp && Sp->Icon(EFVIcon::Hand).IsValid())
		{
			const float Ph = FMath::Frac(T * 0.8f);
			const float Press = Ph < 0.55f ? (bPortrait ? 3.f : 2.f) : 0.f;
			const float HX = float(Player.X) + (Player.X < FV::VW - 60.f ? 34.f : -34.f);
			const float HScale = bPortrait ? 2.f : 1.f;
			D.SpriteCentered(Sp->Icon(EFVIcon::Hand), FVector2D(ObPx(HX), ObPx(float(Head.Y) + 6.f + Press)), HScale);
			if (Ph < 0.55f) { D.Ring(FVector2D(ObPx(HX) - 3.f * HScale, ObPx(float(Head.Y) - 2.f * HScale + Press)), 6.f * HScale, 3.f * HScale, ObFade(FVPal::Gold(), 0.7f * (1.f - Ph / 0.55f)), 1.f); }
		}
		TailTarget = Head;
		break;
	case EObStage::Charging:
	case EObStage::Wait:
	case EObStage::Bite:
	case EObStage::Reel:
		TailTarget = Head;
		break;
	case EObStage::OpenMap:
	case EObStage::ChoosePond:
	{
		const FFVRect MapR = Game->GetHudButtonRect(EFVScreen::Map);
		if (MapR.W > 0.f && MapR.H > 0.f)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(T * 5.f);
			D.Glow(MapR.Center(), FMath::Max(MapR.W, MapR.H) * (bPortrait ? 0.8f : 1.2f), FLinearColor(0.5f, 0.4f, 0.12f, 0.6f));
			D.Frame(FFVRect(MapR.X - 2.f, MapR.Y - 2.f, MapR.W + 4.f, MapR.H + 4.f), ObFade(FV::Col(255, 222, 110), 0.6f + 0.4f * Pulse), 2.f);
			const bool bTopButton = MapR.Center().Y < FV::VH * 0.5f;
			if (bTopButton && MapR.Center().X > FV::VW * 0.6f)
			{
				// update 6 (§12.1): a key of the HUD column at the right edge — point at it from the left
				const float Bounce = ObPx(FMath::Abs(FMath::Sin(T * 4.5f)) * 2.5f * U);
				ObArrowRight(D, FVector2D(MapR.X - 4.f - Bounce, ObPx(MapR.Center().Y)), U);
			}
			else
			{
				ObPointAt(D, MapR, T, U, bTopButton);
			}
			if (!bTopButton) { TailTarget = FVector2D(MapR.Center().X, MapR.Y); }
		}
		break;
	}
	default:
		break;
	}

	// ---------------------------------------------------------------- the speech bubble
	const FObText Txt = ObTextFor(Stage, bTouch, Step >= ObStepWait);
	// update 6 (§12.1): in portrait the bubble stays left of the HUD button column (the column keys stay tappable)
	const FFVRect HudCol = Game->GetHudColumnRect();
	const bool bBesideColumn = bPortrait && HudCol.W > 0.f && HudCol.X > FV::VW * 0.5f;
	const float W = bPortrait ? (bBesideColumn ? FMath::Max(200.f, HudCol.X - 4.f - 10.f) : FV::VW - 20.f) : 300.f;
	const float Pad = bPortrait ? 10.f : 7.f;
	const float Badge = bPortrait ? 44.f : 30.f;
	const float CapS = bPortrait ? FV::Ui::Small() : 6.5f;
	const float TitleS = bPortrait ? 12.f : 9.5f;
	const float BodyS = bPortrait ? FV::Ui::Body() - 0.5f : 7.f;
	const float SkipS = bPortrait ? FV::Ui::Small() : 6.5f;
	const float SkipH = bPortrait ? 20.f : 13.f;
	const float ColX = Pad + Badge + (bPortrait ? 10.f : 7.f);
	const float ColW = W - ColX - Pad;
	const TArray<FString> TitleLines = ObWrap(D, Txt.Title, ColW, TitleS, true);
	const float CapH = FMath::Max(SkipH, D.LineHeight(CapS, false));
	const float TitleLH = D.LineHeight(TitleS, true);
	const float BodyH = D.MeasureWrapped(Txt.Body, ColW, BodyS, false, 1.1f);
	const float H = FMath::Max(Badge + Pad * 2.f, Pad + CapH + 3.f + TitleLH * float(TitleLines.Num()) + 3.f + BodyH + Pad);
	float Y = bPortrait ? FV::TopBarH + 44.f : FV::TopBarH + 6.f;
	// portrait: the reeling panel grows from the middle of the screen — keep the bubble right under the top strip
	if (bPortrait && Stage == EObStage::Reel) { Y = FV::TopBarH + 4.f; }
	const float Pop = FMath::Clamp(St.Age / 0.22f, 0.f, 1.f);
	const float Ease = 1.f - (1.f - Pop) * (1.f - Pop);
	Y += ObPx((1.f - Ease) * -10.f);
	const float X = bPortrait ? (bBesideColumn ? 10.f : ObPx((FV::VW - W) * 0.5f)) : 10.f;
	const FFVRect R(X, ObPx(Y), ObPx(W), ObPx(H));
	D.MarkUI(R);
	ObBubble(D, R);
	if (TailTarget.Y > R.Bottom() + 16.f) { ObTail(D, R, TailTarget, bPortrait ? 1.3f : 1.f); }

	// icon badge: a round wooden medallion with the step's icon, bouncing in when the step changes
	{
		const float Bounce = St.Age < 0.35f ? ObPx(FMath::Sin(St.Age / 0.35f * UE_PI) * -4.f) : 0.f;
		const FVector2D C(R.X + Pad + Badge * 0.5f, R.Y + Pad + Badge * 0.5f + Bounce);
		D.Circle(C + FVector2D(1.f, 2.f), Badge * 0.5f, FV::Col(0, 0, 0, 50));
		D.Circle(C, Badge * 0.5f, FV::Col(176, 110, 56));
		D.Circle(C, Badge * 0.5f - 2.f, FV::Col(236, 184, 116));
		D.Circle(C - FVector2D(Badge * 0.08f, Badge * 0.08f), Badge * 0.5f - 5.f, FV::Col(250, 214, 150));
		if (Sp && Sp->Icon(Txt.Icon).IsValid())
		{
			const float Wob = (Stage == EObStage::Bite) ? FMath::Sin(T * 30.f) * 8.f : 0.f;
			D.SpriteCentered(Sp->Icon(Txt.Icon), FVector2D(ObPx(float(C.X)), ObPx(float(C.Y))), bPortrait ? 2.f : 1.f, FLinearColor::White, false, Wob);
		}
	}

	// caption «Первая рыбалка» + five progress dots; «Пропустить обучение» at the top right
	const float TX = R.X + ColX;
	float TY = R.Y + Pad;
	{
		const FString Cap = TEXT("Первая рыбалка");
		const FVector2D CapSz = D.MeasureText(Cap, CapS, false);
		const float CapY = ObPx(TY + (CapH - float(CapSz.Y)) * 0.5f);
		D.Text(Cap, FVector2D(TX, CapY), CapS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		const int32 Lit = ObDot(Stage);
		const float DotS = bPortrait ? 5.f : 4.f, DotGap = bPortrait ? 4.f : 3.f;
		float DX = ObPx(TX + float(CapSz.X) + (bPortrait ? 8.f : 6.f));
		const float DY = ObPx(TY + CapH * 0.5f - DotS * 0.5f);
		for (int32 k = 0; k < 5; ++k)
		{
			const bool bOn = k <= Lit;
			const float S2 = (k == Lit) ? DotS + 2.f : DotS;
			const float Off = (k == Lit) ? -1.f : 0.f;
			D.Rect(FFVRect(DX + Off, DY + Off, S2, S2), bOn ? (k == Lit ? FV::Col(96, 176, 60) : FV::Col(150, 196, 110)) : FV::Col(222, 196, 150));
			DX += DotS + DotGap;
		}

		const FString SkipTxt = TEXT("Пропустить обучение");
		const float SkipW = ObPx(float(D.MeasureText(SkipTxt, SkipS, true).X) + (bPortrait ? 16.f : 12.f));
		const FFVRect SkipR(R.Right() - Pad - SkipW, TY + ObPx((CapH - SkipH) * 0.5f), SkipW, SkipH);
		bool bSkip = D.Button(SkipR, SkipTxt, EFVButton::Normal, true, SkipS);
		// touch: world fingers never become the UI pointer — a short tap on the button arrives as a world tap
		const FFVInput& In = D.Input();
		if (!bSkip && bTouch && In.bWorldTap && FFVRect(SkipR.X - 6.f, SkipR.Y - 6.f, SkipR.W + 12.f, SkipR.H + 12.f).Contains(In.WorldTapPos))
		{
			bSkip = true;
			ObSfx(EFVSfx::Click);
		}
		if (bSkip)
		{
			FVOnboarding::Skip(Game);
			return;
		}
	}
	TY += CapH + 3.f;
	for (const FString& Ln : TitleLines)
	{
		D.Text(Ln, FVector2D(TX, ObPx(TY)), TitleS, FVPal::Ink(), EFVAlign::Left, false, true);
		TY += TitleLH;
	}
	TY += 3.f;
	D.TextWrapped(Txt.Body, FVector2D(TX, ObPx(TY)), ColW, BodyS, FV::Col(96, 62, 40), false, 1.1f);
}
