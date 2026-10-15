// UPDATE 6 [MINI-STONE] — mini-games «Ковка» and «Раскопка» (Docs/UPDATE6_FOREST_CITY.md §6.3, §9а). Owner: MINI-STONE.
//   Forging    (context = recipe id with bForgeGame): strike the glowing metal in time with the pendulum; the metal cools,
//              «В горн» heats it again; a strike on cold metal cracks it (3 cracks = the blank is spoilt, nothing is
//              spent). Success -> UFVSaveGame::Craft, ++U6.Forgings, back to the forge (EFVScreen::YardBuilding "yb_forge").
//   Excavation (context = "cave:<boulderId>" or "city:rubble_N"): the 8x6 face of a boulder, 1–3 layers of rock per cell
//              over the finds (hard rock 2–4 cells, pebbles, scrap, empty rock); valuable finds glint through the rock.
//              Pickaxe = a cross of 5 cells and a strong shake (hardened pickaxe: FFVToolDef::HitsMult), hammer = 1 cell
//              and a weak shake; the boulder's durability bar; only fully uncovered finds are kept -> ExcavateBoulder.
//              Rubble = 3 boulders in a row -> ClearRubble (finds of the rubble go straight to the backpack).
// Both screens: logic + keys + autotest bot in Tick, IMGUI drawing + pointer in Draw, both orientations, a result card
// inside the screen. Esc / «Назад» asks «Бросить?» and leaves without spending anything.

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVScene.h"
#include "FVSprites.h"
#include "HAL/PlatformTime.h"

namespace FVMiniGameStoneImpl
{
	// =====================================================================================================
	// small helpers
	// =====================================================================================================
	FORCEINLINE float Snap(double V) { return FMath::RoundToFloat(float(V)); }
	FORCEINLINE float Clamp01(float V) { return FMath::Clamp(V, 0.f, 1.f); }
	FORCEINLINE float Smooth01(float T) { T = Clamp01(T); return T * T * (3.f - 2.f * T); }
	FORCEINLINE FLinearColor WithAlpha(FLinearColor C, float A) { C.A = Clamp01(A); return C; }
	inline float EaseOutBack(float T)
	{
		const float U = Clamp01(T) - 1.f;
		constexpr float C1 = 1.70158f;
		constexpr float C3 = C1 + 1.f;
		return 1.f + C3 * U * U * U + C1 * U * U;
	}
	inline void Sfx(EFVSfx S, float Volume = 1.f, float Pitch = 1.f) { FVUI::Sfx(S, Volume, Pitch); }
	inline bool ConfirmPressed(const FFVInput& In)
	{
		return In.WasPressed(EKeys::SpaceBar) || In.WasPressed(EKeys::Enter) || In.WasPressed(EKeys::Gamepad_FaceButton_Bottom);
	}

	FORCEINLINE FLinearColor InkGood() { return FV::Col(52, 122, 36); }
	FORCEINLINE FLinearColor InkBad() { return FV::Col(178, 46, 32); }
	FORCEINLINE FLinearColor InkGold() { return FV::Col(172, 108, 14); }

	/** red (0) -> yellow (0.5) -> green (1). */
	inline FLinearColor MeterColor(float P)
	{
		P = Clamp01(P);
		const FLinearColor Red = FV::Col(222, 60, 44), Yellow = FV::Col(246, 206, 58), Green = FV::Col(96, 204, 64);
		return P < 0.5f ? FMath::Lerp(Red, Yellow, P * 2.f) : FMath::Lerp(Yellow, Green, (P - 0.5f) * 2.f);
	}

	/** Colour of hot metal: dark grey (cold) -> red -> orange -> white-yellow. */
	inline FLinearColor HeatColor(float H)
	{
		H = Clamp01(H);
		const FLinearColor Cold = FV::Col(84, 78, 80), Red = FV::Col(200, 54, 28), Orange = FV::Col(255, 140, 40), Hot = FV::Col(255, 232, 150);
		if (H < 0.25f) { return FMath::Lerp(Cold, FMath::Lerp(Cold, Red, 0.6f), H / 0.25f); }
		if (H < 0.5f) { return FMath::Lerp(Red, Orange, (H - 0.25f) / 0.25f); }
		return FMath::Lerp(Orange, Hot, (H - 0.5f) / 0.5f);
	}

	// stable ids (spec §17.3)
	inline FName ForgeYardId() { return FName(TEXT("yb_forge")); }
	inline FName HardrockId() { return FName(TEXT("mat_hardrock")); }
	inline FName ScrapId() { return FName(TEXT("mat_scrap")); }
	inline FName PebbleId() { return FName(TEXT("res_pebble")); }
	constexpr int32 RubbleMinLevel = 12;   // §9а: the cave rubble opens at level 12
	constexpr int32 RubbleTotal = 3;       // rubble_0..2 (§17.3)

	inline void AddIngredient(TArray<FFVIngredient>& Arr, FName Id, int32 Count)
	{
		if (Id.IsNone() || Count <= 0) { return; }
		for (FFVIngredient& I : Arr)
		{
			if (I.ItemId == Id) { I.Count += Count; return; }
		}
		FFVIngredient& N = Arr.AddDefaulted_GetRef();
		N.ItemId = Id;
		N.Count = Count;
	}

	/** FVDraw buttons fire on RELEASE; timed actions react on the press and swallow the release click. */
	inline bool InstantButton(FVDraw& D, const FFVRect& R, const FString& Label, EFVButton Style, bool bEnabled, float TextSize)
	{
		const bool bPress = bEnabled && D.PressedIn(R);
		if (bEnabled) { D.Clicked(R); }
		D.Button(R, Label, Style, bEnabled, TextSize);
		if (bPress) { Sfx(EFVSfx::Click, 0.8f); }
		return bPress;
	}

	/** Text centred on CX when it fits in one line, wrapped (left-aligned) otherwise. Returns the height used. */
	inline float CenteredText(FVDraw& D, const FString& S, float CX, float Y, float MaxW, float Size, const FLinearColor& C, bool bBold = false)
	{
		if (S.IsEmpty()) { return 0.f; }
		if (float(D.MeasureText(S, Size, bBold).X) <= MaxW)
		{
			D.Text(S, FVector2D(CX, Y), Size, C, EFVAlign::Center, false, bBold);
			return D.LineHeight(Size, bBold);
		}
		return D.TextWrapped(S, FVector2D(CX - MaxW * 0.5f, Y), MaxW, Size, C, bBold);
	}

	// =====================================================================================================
	// juice: chips, sparks, sparkles, pop-up words, screen shake
	// =====================================================================================================
	struct FParticle
	{
		FVector2D P = FVector2D::ZeroVector;
		FVector2D V = FVector2D::ZeroVector;
		float Age = 0.f;
		float Life = 1.f;
		float Size = 2.f;
		float Gravity = 0.f;
		FLinearColor C = FLinearColor::White;
		uint8 Kind = 0;        // 0 chunk, 1 sparkle, 2 spark streak
	};

	struct FFloater
	{
		FString Text;
		FVector2D P = FVector2D::ZeroVector;
		FLinearColor C = FLinearColor::White;
		float Age = 0.f;
		float Life = 1.1f;
		float Size = 10.f;
	};

	class FJuice
	{
	public:
		FRandomStream Rng;
		float Shake = 0.f;

		void Burst(const FVector2D& Pos, const FLinearColor& Col, int32 Num, float Speed, float Gravity, float Size, float Life = 0.6f)
		{
			for (int32 i = 0; i < Num && Parts.Num() < MaxParts; ++i)
			{
				FParticle& Pt = Parts.AddDefaulted_GetRef();
				const float Ang = Rng.FRandRange(0.f, 2.f * PI);
				const float Spd = Speed * Rng.FRandRange(0.35f, 1.f);
				Pt.P = Pos;
				Pt.V = FVector2D(FMath::Cos(Ang) * Spd, FMath::Sin(Ang) * Spd - Speed * 0.45f);
				Pt.Life = Life * Rng.FRandRange(0.7f, 1.2f);
				Pt.Size = Size * Rng.FRandRange(0.7f, 1.3f);
				Pt.Gravity = Gravity;
				Pt.C = Col;
				Pt.Kind = 0;
			}
		}

		/** Hot sparks flying up and falling (forge). */
		void Sparks(const FVector2D& Pos, const FLinearColor& Col, int32 Num, float Speed)
		{
			for (int32 i = 0; i < Num && Parts.Num() < MaxParts; ++i)
			{
				FParticle& Pt = Parts.AddDefaulted_GetRef();
				const float Ang = Rng.FRandRange(-PI * 0.95f, -PI * 0.05f);
				const float Spd = Speed * Rng.FRandRange(0.4f, 1.1f);
				Pt.P = Pos + FVector2D(Rng.FRandRange(-4.f, 4.f), 0.f);
				Pt.V = FVector2D(FMath::Cos(Ang) * Spd, FMath::Sin(Ang) * Spd);
				Pt.Life = Rng.FRandRange(0.3f, 0.6f);
				Pt.Size = 1.5f;
				Pt.Gravity = 340.f;
				Pt.C = Col;
				Pt.Kind = 2;
			}
		}

		void Sparkles(const FVector2D& Pos, int32 Num, float Radius, const FLinearColor& Col)
		{
			for (int32 i = 0; i < Num && Parts.Num() < MaxParts; ++i)
			{
				FParticle& Pt = Parts.AddDefaulted_GetRef();
				Pt.P = Pos + FVector2D(Rng.FRandRange(-Radius, Radius), Rng.FRandRange(-Radius, Radius));
				Pt.V = FVector2D(Rng.FRandRange(-12.f, 12.f), Rng.FRandRange(-34.f, -6.f));
				Pt.Life = Rng.FRandRange(0.45f, 0.9f);
				Pt.Size = Rng.FRandRange(2.5f, 4.5f);
				Pt.Gravity = 0.f;
				Pt.C = Col;
				Pt.Kind = 1;
			}
		}

		void Text(const FString& S, const FVector2D& Pos, const FLinearColor& Col, float Size)
		{
			if (Floaters.Num() >= 8) { Floaters.RemoveAt(0); }
			FFloater& F = Floaters.AddDefaulted_GetRef();
			F.Text = S;
			F.P = Pos;
			F.C = Col;
			F.Size = Size;
		}

		void Kick(float Amount) { Shake = FMath::Max(Shake, Amount); }

		void Tick(float Dt)
		{
			for (int32 i = Parts.Num() - 1; i >= 0; --i)
			{
				FParticle& Pt = Parts[i];
				Pt.Age += Dt;
				if (Pt.Age >= Pt.Life) { Parts.RemoveAtSwap(i); continue; }
				Pt.V.Y += Pt.Gravity * Dt;
				if (Pt.Kind == 1) { Pt.V *= FMath::Max(0.f, 1.f - Dt * 1.5f); }
				Pt.P += Pt.V * Dt;
			}
			for (int32 i = Floaters.Num() - 1; i >= 0; --i)
			{
				Floaters[i].Age += Dt;
				if (Floaters[i].Age >= Floaters[i].Life) { Floaters.RemoveAt(i); }
			}
			Shake = FMath::Max(0.f, Shake - Dt * 5.f);
		}

		FVector2D ShakeOffset()
		{
			if (Shake < 0.05f) { return FVector2D::ZeroVector; }
			return FVector2D(FMath::RoundToFloat(Rng.FRandRange(-1.f, 1.f) * 1.6f * Shake), FMath::RoundToFloat(Rng.FRandRange(-1.f, 1.f) * 1.6f * Shake));
		}

		void Clear() { Parts.Reset(); Floaters.Reset(); Shake = 0.f; }

		void Draw(FVDraw& D) const
		{
			for (const FParticle& Pt : Parts)
			{
				const float T = Clamp01(Pt.Age / FMath::Max(0.01f, Pt.Life));
				const float A = 1.f - T;
				if (Pt.Kind == 0)
				{
					const float S = FMath::Max(1.f, Snap(Pt.Size * (1.f - 0.4f * T)));
					D.Rect(FFVRect(Snap(Pt.P.X - S * 0.5f), Snap(Pt.P.Y - S * 0.5f), S, S), WithAlpha(Pt.C, Pt.C.A * FMath::Min(1.f, A * 2.5f)));
				}
				else if (Pt.Kind == 1)
				{
					const float Tw = 0.55f + 0.45f * FMath::Sin(Pt.Age * 20.f + Pt.Size);
					const float L = FMath::Max(1.f, Snap(Pt.Size * Tw * (1.f - T * 0.6f)));
					const FLinearColor Col = WithAlpha(Pt.C, A);
					const float X = Snap(Pt.P.X), Y = Snap(Pt.P.Y);
					D.Glow(Pt.P, 3.f + L, WithAlpha(Pt.C * 0.35f, A * 0.6f));
					D.Rect(FFVRect(X - L, Y, L * 2.f + 1.f, 1.f), Col);
					D.Rect(FFVRect(X, Y - L, 1.f, L * 2.f + 1.f), Col);
					D.Rect(FFVRect(X, Y, 1.f, 1.f), WithAlpha(FLinearColor::White, A));
				}
				else
				{
					const FVector2D Tail = Pt.P - Pt.V * 0.03;
					D.Line(Tail, Pt.P, WithAlpha(Pt.C, FMath::Min(1.f, A * 1.8f)), Pt.Size);
				}
			}
			for (const FFloater& F : Floaters)
			{
				const float T = Clamp01(F.Age / F.Life);
				const float A = T > 0.6f ? (1.f - T) / 0.4f : 1.f;
				const float Pop = F.Age < 0.12f ? 1.f + (0.12f - F.Age) * 2.5f : 1.f;
				D.TextOutlined(F.Text, FVector2D(F.P.X, F.P.Y - T * 18.f), F.Size * Pop, WithAlpha(F.C, A), EFVAlign::Center,
					FV::Col(40, 22, 12, uint8(255.f * Clamp01(A))));
			}
		}

	private:
		static constexpr int32 MaxParts = 240;
		TArray<FParticle> Parts;
		TArray<FFloater> Floaters;
	};

	// =====================================================================================================
	// result card
	// =====================================================================================================
	struct FCardItem
	{
		FName Id;
		int32 Count = 0;
	};

	struct FCardLine
	{
		FString Text;
		FLinearColor Color = FLinearColor::Black;
	};

	struct FCard
	{
		bool bShown = false;
		float Age = 0.f;
		bool bGood = true;
		FString Title;
		FString Sub;
		FName IconId;                        // item icon (preferred)
		EFVIcon Icon = EFVIcon::Count;       // UI icon fallback
		TArray<FCardItem> Items;
		TArray<FCardLine> Lines;
		FString Primary = TEXT("Готово");
		FString Secondary;

		void AddLine(const FString& T, const FLinearColor& C)
		{
			FCardLine& L = Lines.AddDefaulted_GetRef();
			L.Text = T;
			L.Color = C;
		}
		void AddItem(FName Id, int32 Count)
		{
			FCardItem& It = Items.AddDefaulted_GetRef();
			It.Id = Id;
			It.Count = Count;
		}
	};

	/** Draws the card over Area. Returns the pressed button (0 = primary, 1 = secondary) or -1. */
	int32 DrawCard(FVDraw& D, const FFVRect& Area, const FCard& C, FVector2D& OutIconCenter)
	{
		const bool bP = FV::IsPortrait();
		D.Rect(Area, FV::Col(24, 14, 8, 110));
		const float W = FMath::Min(Area.W - 12.f, bP ? 320.f : 300.f);
		const float Pad = 10.f;
		const float TextW = W - 2.f * Pad;
		const float TitleS = FV::Ui::Header(), BodyS = FV::Ui::Body(), SmallS = FV::Ui::Small();
		const float IconBox = bP ? 44.f : 34.f;
		const float ItemBox = bP ? 26.f : 22.f;
		const float BtnH = FV::Ui::ButtonH();
		float H = Pad + IconBox + 4.f + D.LineHeight(TitleS) + 3.f;
		if (!C.Sub.IsEmpty()) { H += D.MeasureWrapped(C.Sub, TextW, BodyS, false) + 4.f; }
		if (C.Items.Num() > 0) { H += ItemBox + D.LineHeight(SmallS) + 6.f; }
		for (const FCardLine& L : C.Lines) { H += D.MeasureWrapped(L.Text, TextW, SmallS, false) + 2.f; }
		H += 8.f + BtnH + Pad;
		H = FMath::Min(H, Area.H - 4.f);
		const float Slide = (1.f - EaseOutBack(Clamp01(C.Age / 0.3f))) * 18.f;
		const FFVRect R(Snap(Area.Center().X - W * 0.5f), Snap(Area.Center().Y - H * 0.5f + Slide), W, Snap(H));
		D.Panel(R, EFVPanel::Parchment);
		if (C.bGood) { D.Rect(FFVRect(R.X + 4.f, R.Y + 3.f, R.W - 8.f, 2.f), FV::Col(236, 190, 70)); }

		const float CX = R.X + R.W * 0.5f;
		float Y = R.Y + Pad;
		const FFVRect IconR(Snap(CX - IconBox * 0.5f), Y, IconBox, IconBox);
		OutIconCenter = IconR.Center();
		if (C.bGood) { D.Glow(IconR.Center(), IconBox * 0.8f, WithAlpha(FV::Col(255, 220, 120), 0.45f)); }
		const FFVSprite* S = !C.IconId.IsNone() ? FVUI::ItemSprite(C.IconId) : nullptr;
		if (!S && C.Icon != EFVIcon::Count) { S = FVUI::Icon(C.Icon); }
		if (S) { FVUI::SpriteFit(D, *S, IconR, FLinearColor::White, 3.f); }
		Y += IconBox + 4.f;
		D.Text(C.Title, FVector2D(CX, Y), TitleS, C.bGood ? InkGold() : InkBad(), EFVAlign::Center, false, true);
		Y += D.LineHeight(TitleS) + 3.f;
		if (!C.Sub.IsEmpty()) { Y += CenteredText(D, C.Sub, CX, Y, TextW, BodyS, FVPal::Ink()) + 4.f; }
		if (C.Items.Num() > 0)
		{
			const float CellW = ItemBox + 22.f;
			const int32 N = FMath::Min(C.Items.Num(), FMath::Max(1, int32(TextW / CellW)));
			float X = Snap(CX - CellW * float(N) * 0.5f);
			for (int32 i = 0; i < N; ++i)
			{
				const FCardItem& It = C.Items[i];
				const FFVRect Box(Snap(X + (CellW - ItemBox) * 0.5f), Y, ItemBox, ItemBox);
				D.Rect(Box, FV::Col(214, 186, 140));
				D.Frame(Box, FV::Col(150, 110, 70), 1.f);
				if (const FFVSprite* IS = FVUI::ItemSprite(It.Id)) { FVUI::SpriteFit(D, *IS, Box.Inset(2.f), FLinearColor::White, 2.f); }
				D.Text(FString::Printf(TEXT("×%d"), It.Count), FVector2D(X + CellW * 0.5f, Y + ItemBox + 1.f), SmallS, FVPal::Ink(), EFVAlign::Center, false, true);
				X += CellW;
			}
			Y += ItemBox + D.LineHeight(SmallS) + 6.f;
		}
		for (const FCardLine& L : C.Lines) { Y += CenteredText(D, L.Text, CX, Y, TextW, SmallS, L.Color) + 2.f; }

		const bool bOn = C.Age > 0.3f;
		const float BY = R.Bottom() - Pad - BtnH;
		const float TS = bP ? FV::Ui::Body() + 1.f : 8.f;
		int32 Result = -1;
		if (C.Secondary.IsEmpty())
		{
			const float BW = FMath::Min(140.f, TextW);
			if (D.Button(FFVRect(Snap(CX - BW * 0.5f), BY, BW, BtnH), C.Primary, EFVButton::Primary, bOn, TS)) { Result = 0; }
		}
		else
		{
			const FFVRect Row(R.X + Pad, BY, TextW, BtnH);
			if (D.Button(FVDraw::SplitH(Row, 0, 2), C.Secondary, EFVButton::Normal, bOn, TS)) { Result = 1; }
			if (D.Button(FVDraw::SplitH(Row, 1, 2), C.Primary, EFVButton::Primary, bOn, TS)) { Result = 0; }
		}
		return Result;
	}

	// =====================================================================================================
	// base screen: window, footer (Назад + actions + hint), quit confirmation, result card, banner, juice
	// =====================================================================================================
	class FStoneGame : public FVScreen
	{
	public:
		FStoneGame(UFVGame* InGame, FName InReturnYard)
			: FVScreen(InGame), ReturnYard(InReturnYard)
		{
			bBot = InGame != nullptr && InGame->IsAutoTest();
			const int32 Seed = int32(FPlatformTime::Cycles() & 0x7fffffffu);
			Juice.Rng.Initialize(Seed);
			Overlay.Rng.Initialize(Seed ^ 0x2545F491);
		}

		virtual bool HandlesEscape() const override { return true; }

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			if (!Game || bLeaving) { return; }
			Dt = FMath::Clamp(Dt, 0.f, 0.1f);
			Time += Dt;
			BannerAge += Dt;
			Juice.Tick(Dt);
			Overlay.Tick(Dt);
			if (Card.bShown)
			{
				Card.Age += Dt;
				if (Card.bGood && Card.Age < 1.6f && !CardIconCenter.IsZero())
				{
					CardSparkT -= Dt;
					if (CardSparkT <= 0.f)
					{
						CardSparkT = 0.2f;
						Overlay.Sparkles(CardIconCenter, 3, FV::IsPortrait() ? 30.f : 24.f, FV::Col(255, 238, 150));
					}
				}
				if (bBot && Card.Age > 1.3f) { Leave(); return; }
				if (Card.Age > 0.35f)
				{
					if (ConfirmPressed(In)) { OnCardButton(0); return; }
					if (In.WasPressed(EKeys::Escape)) { Leave(); return; }
				}
				return;
			}
			if (In.WasPressed(EKeys::Escape)) { RequestBack(); return; }
			TickGame(Dt, In);
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bPortrait = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			bool bBack = D.Window(Win, Title());
			bInteractive = !D.IsInputBlocked() && !Card.bShown && !bLeaving;
			const FString HintS = Card.bShown ? FString() : Hint();

			FFVRect Area;
			float HintY = 0.f;
			if (bPortrait)
			{
				const FFVRect Bar = FVDraw::ActionBarRect(Win);
				const float Top = Win.Y + 22.f;
				float Bottom = Bar.Y - 12.f;
				if (!HintS.IsEmpty())
				{
					const float HintH = D.MeasureWrapped(HintS, Win.W - 36.f, FV::Ui::Small(), false);
					Bottom -= HintH + 6.f;
					HintY = Bottom + 5.f;
				}
				Area = FFVRect(Win.X + 10.f, Top, Win.W - 20.f, FMath::Max(60.f, Bottom - Top));
			}
			else
			{
				Area = FFVRect(Win.X + 10.f, Win.Y + 14.f, Win.W - 20.f, Win.H - 50.f);
			}

			// the game (inert while the card is up: the card's buttons must get the clicks)
			const bool bWasBlocked = D.IsInputBlocked();
			if (Card.bShown || bLeaving) { D.SetInputBlocked(true); }
			const FVector2D Sh = Juice.ShakeOffset();
			DrawGame(D, Area.Offset(float(Sh.X), float(Sh.Y)));
			D.SetInputBlocked(bWasBlocked);
			Juice.Draw(D);
			DrawBanner(D, Area);

			// footer
			if (bPortrait)
			{
				if (!HintS.IsEmpty())
				{
					CenteredText(D, HintS, Win.X + Win.W * 0.5f, HintY, Win.W - 36.f, FV::Ui::Small(), FVPal::InkSoft());
				}
				FFVRect Actions;
				if (D.ActionBar(Win, &Actions)) { bBack = true; }
				if (!Card.bShown && !bLeaving) { DrawActions(D, Actions); }
			}
			else
			{
				const FFVRect Row(Win.X + 10.f, Win.Bottom() - 30.f, Win.W - 20.f, 20.f);
				D.Divider(FVector2D(Row.X, Row.Y - 5.f), Row.W);
				if (D.BackButton(FFVRect(Row.X, Row.Y, 90.f, Row.H), TEXT("Назад"), 8.f)) { bBack = true; }
				const FFVRect Act(Row.Right() - 176.f, Row.Y, 176.f, Row.H);
				if (!Card.bShown && !bLeaving) { DrawActions(D, Act); }
				if (!HintS.IsEmpty())
				{
					const float L = Row.X + 98.f, R = Act.X - 8.f;
					D.Text(D.Ellipsize(HintS, R - L, 6.5f, false), FVector2D(Snap((L + R) * 0.5f), Row.Y + 5.f), 6.5f, FVPal::InkSoft(), EFVAlign::Center, false, false);
				}
			}

			if (Card.bShown)
			{
				const int32 Btn = DrawCard(D, Area, Card, CardIconCenter);
				Overlay.Draw(D);
				if (Btn >= 0) { OnCardButton(Btn); return; }   // may retire the screen
			}
			if (bBack) { RequestBack(); return; }                // may retire the screen
		}

	protected:
		FName ReturnYard;                // NAME_None = back to the world, "yb_forge" = the forge screen
		bool bBot = false;               // autotest: the built-in bot plays
		bool bLeaving = false;
		bool bInteractive = false;       // Draw: pointer interactions allowed this frame
		float Time = 0.f;                // seconds (paused under modals, like the game logic)
		FJuice Juice;                    // game particles (under the card)
		FJuice Overlay;                  // card particles (above the card)
		FCard Card;
		float CardSparkT = 0.f;
		FVector2D CardIconCenter = FVector2D::ZeroVector;
		FString BannerText;
		FLinearColor BannerColor = FLinearColor::White;
		float BannerAge = 99.f;
		bool bPlayShot = false;

		virtual FString Title() const = 0;
		virtual FString ShotSlug() const = 0;
		virtual void TickGame(float Dt, const FFVInput& In) = 0;
		virtual void DrawGame(FVDraw& D, const FFVRect& Area) = 0;
		virtual void DrawActions(FVDraw&, const FFVRect&) {}
		virtual FString Hint() const { return FString(); }
		virtual bool QuitNeedsConfirm() const { return true; }
		virtual FString QuitBody() const { return TEXT("Ничего не потратится."); }
		/** 0 = primary, 1 = secondary. Default: leave. */
		virtual void OnCardButton(int32) { Leave(); }

		/** The forge screen for the forge, the world otherwise (ReturnFromMiniGame refreshes the world objects). */
		static void ReturnTo(UFVGame* G, FName Yard)
		{
			if (!G) { return; }
			if (Yard.IsNone()) { G->ReturnFromMiniGame(NAME_None); return; }
			G->OpenScreenWith(EFVScreen::YardBuilding, Yard);
		}

		/** Last statement of any caller: the screen is retired here. */
		void Leave()
		{
			if (bLeaving || !Game) { return; }
			bLeaving = true;
			UFVGame* G = Game;
			ReturnTo(G, ReturnYard);
		}

		void RequestBack()
		{
			if (Card.bShown) { Leave(); return; }
			if (!QuitNeedsConfirm()) { Leave(); return; }
			if (!Game || Game->HasModal()) { return; }
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			const FName Yard = ReturnYard;
			Game->Confirm(TEXT("Бросить?"), QuitBody(), [WeakGame, Yard]()
			{
				if (UFVGame* G = WeakGame.Get()) { ReturnTo(G, Yard); }
			}, TEXT("Бросить"), TEXT("Остаться"));
		}

		void Banner(const FString& Text, const FLinearColor& Col)
		{
			BannerText = Text;
			BannerColor = Col;
			BannerAge = 0.f;
		}

		void ShowCard(const FCard& C, EFVSfx Sound)
		{
			Card = C;
			Card.bShown = true;
			Card.Age = 0.f;
			CardSparkT = 0.25f;
			Sfx(Sound, 0.9f);
			if (bBot && Game) { Game->RequestShot(FString::Printf(TEXT("minigame_%s_result"), *ShotSlug())); }
		}

		/** The screen cannot run (bad context, no pickaxe...): a card with the reason and «Назад». */
		void OpenFailed(const FString& Reason, EFVIcon Icon, const FString& InTitle = TEXT("Не получится"))
		{
			FCard C;
			C.bGood = false;
			C.Title = InTitle;
			C.Sub = Reason;
			C.Icon = Icon;
			C.Primary = TEXT("Назад");
			ShowCard(C, EFVSfx::Error);
		}

		void RequestPlayShot()
		{
			if (bBot && !bPlayShot && Game)
			{
				bPlayShot = true;
				Game->RequestShot(FString::Printf(TEXT("minigame_%s_play"), *ShotSlug()));
			}
		}

	private:
		void DrawBanner(FVDraw& D, const FFVRect& Area) const
		{
			constexpr float Life = 1.15f;
			if (BannerText.IsEmpty() || BannerAge >= Life) { return; }
			const float T = BannerAge / Life;
			const float Pop = BannerAge < 0.14f ? 1.f + (0.14f - BannerAge) * 3.f : 1.f;
			const float A = T > 0.7f ? (1.f - T) / 0.3f : 1.f;
			const float Size = (FV::IsPortrait() ? 20.f : 16.f) * Pop;
			const FVector2D P(Area.Center().X, Snap(Area.Y + Area.H * 0.3f - Size * 0.6f - T * 8.f));
			D.TextOutlined(BannerText, P, Size, WithAlpha(BannerColor, A), EFVAlign::Center, FV::Col(40, 22, 12, uint8(255.f * Clamp01(A))));
		}
	};

	// =====================================================================================================
	// FORGING (context = recipe id): strike in time with the pendulum while the metal glows
	// =====================================================================================================
	class FForgeScreen final : public FStoneGame
	{
	public:
		explicit FForgeScreen(UFVGame* InGame) : FStoneGame(InGame, ForgeYardId()) {}

		virtual void OnOpen() override
		{
			RecipeId = Game ? Game->GetScreenContext() : NAME_None;
			Setup();
		}

	protected:
		enum class EPhase : uint8 { Intro, Play, Reheat, Done, Cracked, Over };
		static constexpr float PerfectWin = 0.075f;     // seconds from the beat
		static constexpr float GoodWin = 0.16f;
		static constexpr float ColdHeat = 0.25f;        // below: the metal no longer glows, a strike cracks it
		static constexpr int32 MaxCracks = 3;
		static constexpr float StrikeHeatCost = 0.02f;

		FName RecipeId;
		const FFVRecipeDef* Recipe = nullptr;
		bool bReady = false;
		bool bFinished = false;
		EPhase Phase = EPhase::Over;
		float PhaseT = 0.f;
		int32 Difficulty = 1;
		float Period = 0.8f;             // seconds per beat
		float Drain = 0.085f;            // heat lost per second
		float Required = 6.f;            // «Форма» points (perfect strike = 1)
		float Heat = 1.f;
		float ReheatFrom = 0.f;
		float ReheatDur = 1.f;
		float BeatStart = 0.f;
		int32 LastBeat = MIN_int32;
		int32 LastTickBeat = MIN_int32;
		float Progress = 0.f;
		float GradeSum = 0.f;
		int32 Strikes = 0;
		int32 Perfects = 0;
		int32 Cracks = 0;
		int32 Reheats = 0;
		float HammerT = 99.f;
		float BeatFlash = 0.f;
		int32 AgainButton = -1;          // card button that restarts the forging (-1 = none)
		FVector2D IngotPos = FVector2D::ZeroVector;   // top centre of the ingot in the last frame (particles)

		virtual FString Title() const override
		{
			return Recipe ? FString::Printf(TEXT("Ковка: %s"), *FVDB::ItemName(Recipe->OutputId)) : FString(TEXT("Ковка"));
		}
		virtual FString ShotSlug() const override { return TEXT("forge"); }
		virtual bool QuitNeedsConfirm() const override { return bReady && Strikes > 0 && Phase != EPhase::Over; }
		virtual FString QuitBody() const override { return TEXT("Заготовка остынет. Материалы останутся целы."); }

		virtual FString Hint() const override
		{
			if (!bReady) { return FString(); }
			// touch (tablets in landscape too): no keyboard words; the PC line fits the landscape footer without an ellipsis
			return (FV::IsPortrait() || FV::IsTouchUI()) ? FString(TEXT("Бейте, когда маятник в центре. Остыл металл — в горн!"))
				: FString(TEXT("Пробел / клик — удар в такт маятника, R — в горн."));
		}

		void Setup()
		{
			bReady = false;
			bFinished = false;
			Phase = EPhase::Over;
			Juice.Clear();
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { OpenFailed(TEXT("Профиль не загружен."), EFVIcon::Anvil); return; }
			Recipe = FVDB::FindRecipe(RecipeId);
			if (!Recipe) { OpenFailed(TEXT("Не выбран рецепт ковки."), EFVIcon::Anvil); return; }
			const EFVBuyResult Can = P->CanCraft(*Recipe, 1);
			if (Can != EFVBuyResult::Ok) { OpenFailed(UFVSaveGame::BuyResultText(Can), EFVIcon::Anvil); return; }
			Difficulty = FMath::Clamp(Recipe->GameDifficulty, 1, 3);
			static const float Periods[3] = { 0.80f, 0.70f, 0.62f };
			Period = Periods[Difficulty - 1];
			Drain = 0.085f + 0.015f * float(Difficulty - 1);
			Required = float(5 + Difficulty);
			Heat = 1.f;
			Progress = 0.f;
			GradeSum = 0.f;
			Strikes = Perfects = Cracks = Reheats = 0;
			LastBeat = LastTickBeat = MIN_int32;
			HammerT = 99.f;
			BeatFlash = 0.f;
			AgainButton = -1;
			Phase = EPhase::Intro;
			PhaseT = 0.f;
			bReady = true;
			Banner(TEXT("Куйте в ритм!"), FVPal::Gold());
			Sfx(EFVSfx::Fire, 0.6f);
		}

		float BeatU() const { return (Time - BeatStart) / Period; }

		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			if (!bReady) { return; }
			PhaseT += Dt;
			HammerT += Dt;
			BeatFlash = FMath::Max(0.f, BeatFlash - Dt * 4.f);
			if (Phase == EPhase::Play || Phase == EPhase::Reheat)
			{
				const int32 B = FMath::FloorToInt(BeatU());
				if (B != LastTickBeat)
				{
					if (LastTickBeat != MIN_int32 && B >= 0)
					{
						Sfx(EFVSfx::Click, 0.22f, 1.6f);
						BeatFlash = 1.f;
					}
					LastTickBeat = B;
				}
			}
			switch (Phase)
			{
			case EPhase::Intro:
				if (PhaseT >= 0.9f)
				{
					Phase = EPhase::Play;
					PhaseT = 0.f;
					BeatStart = Time + Period * 0.5f;
				}
				break;
			case EPhase::Play:
				Heat = FMath::Max(0.f, Heat - Drain * Dt);
				RequestPlayShot();
				if (bBot) { TickBot(); break; }
				if (ConfirmPressed(In)) { Strike(); break; }
				if (In.WasPressed(EKeys::R) || In.WasPressed(EKeys::Gamepad_FaceButton_Left)) { Reheat(); }
				break;
			case EPhase::Reheat:
				Heat = FMath::Lerp(ReheatFrom, 1.f, Smooth01(PhaseT / ReheatDur));
				if (PhaseT >= ReheatDur)
				{
					Heat = 1.f;
					Phase = EPhase::Play;
					PhaseT = 0.f;
				}
				break;
			case EPhase::Done:
				if (PhaseT >= 1.0f) { Finish(true); }
				break;
			case EPhase::Cracked:
				if (PhaseT >= 1.0f) { Finish(false); }
				break;
			default:
				break;
			}
		}

		void TickBot()
		{
			if (Heat < 0.33f) { Reheat(); return; }
			const float U = BeatU();
			const int32 Beat = FMath::RoundToInt(U);
			const float Err = FMath::Abs(U - float(Beat)) * Period;
			if (Beat >= 0 && Beat != LastBeat && Err < 0.03f) { Strike(); }
		}

		void Strike()
		{
			if (Phase != EPhase::Play) { return; }
			HammerT = 0.f;
			++Strikes;
			const float U = BeatU();
			const int32 Beat = FMath::RoundToInt(U);
			const float Err = FMath::Abs(U - float(Beat)) * Period;
			const FVector2D P = IngotPos;
			const float WordSize = FV::IsPortrait() ? 11.f : 9.f;
			if (Heat < ColdHeat)
			{
				++Cracks;
				LastBeat = Beat;
				Sfx(EFVSfx::Anvil, 0.75f, 0.62f);
				Juice.Kick(0.7f);
				Juice.Burst(P, FV::Col(96, 90, 90), 5, 50.f, 240.f, 2.f, 0.4f);
				if (Cracks >= MaxCracks)
				{
					Phase = EPhase::Cracked;
					PhaseT = 0.f;
					Banner(TEXT("Треснуло!"), FV::Col(255, 120, 100));
					Sfx(EFVSfx::Error, 0.8f);
				}
				else
				{
					Banner(TEXT("Остыл! В горн!"), FV::Col(255, 150, 130));
					Juice.Text(FString::Printf(TEXT("Трещина %d из %d"), Cracks, MaxCracks), P + FVector2D(0.f, -20.f), FV::Col(255, 150, 130), WordSize);
				}
				return;
			}
			float Grade = 0.3f;
			FString Word = TEXT("Мимо ритма");
			FLinearColor Col = FV::Col(224, 214, 204);
			if (Beat < 0 || Beat == LastBeat) { Grade = 0.2f; Word = TEXT("Не спешите"); }
			else if (Err <= PerfectWin) { Grade = 1.f; Word = TEXT("Отлично!"); Col = FV::Col(255, 226, 110); ++Perfects; }
			else if (Err <= GoodWin) { Grade = 0.7f; Word = TEXT("Хорошо"); Col = FV::Col(150, 230, 120); }
			LastBeat = Beat;
			GradeSum += Grade;
			Progress = FMath::Min(Required, Progress + Grade);
			Heat = FMath::Max(0.f, Heat - StrikeHeatCost);
			Sfx(EFVSfx::Anvil, 0.85f + 0.15f * Grade, 0.92f + 0.2f * Grade + Juice.Rng.FRandRange(-0.04f, 0.04f));
			Juice.Sparks(P, HeatColor(FMath::Max(Heat, 0.55f)), 8 + int32(12.f * Grade), 110.f + 60.f * Grade);
			Juice.Kick(0.5f + 0.7f * Grade);
			Juice.Text(Word, P + FVector2D(0.f, -24.f), Col, WordSize);
			if (Progress >= Required - 0.001f)
			{
				Phase = EPhase::Done;
				PhaseT = 0.f;
				Banner(TEXT("Готово!"), FVPal::Gold());
				Sfx(EFVSfx::Perfect, 0.8f);
				Juice.Sparkles(P, 10, 16.f, FV::Col(255, 238, 150));
			}
		}

		void Reheat()
		{
			if (Phase != EPhase::Play || Heat >= 0.97f) { return; }
			Phase = EPhase::Reheat;
			PhaseT = 0.f;
			++Reheats;
			ReheatFrom = Heat;
			ReheatDur = 0.45f + 0.9f * (1.f - Heat);
			Sfx(EFVSfx::Fire, 0.8f);
		}

		void Finish(bool bOk)
		{
			if (bFinished) { return; }
			bFinished = true;
			Phase = EPhase::Over;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P || !Recipe) { Leave(); return; }
			FCard C;
			C.Icon = EFVIcon::Anvil;
			if (!bOk)
			{
				C.bGood = false;
				C.Title = TEXT("Заготовка треснула");
				C.Sub = TEXT("Холодный металл бить нельзя — он трескается. Материалы целы, попробуйте ещё раз.");
				C.Primary = TEXT("Ещё раз");
				C.Secondary = TEXT("К кузне");
				AgainButton = 0;
				ShowCard(C, EFVSfx::Escape);
				return;
			}
			const EFVBuyResult R = P->Craft(*Recipe, 1);
			if (R != EFVBuyResult::Ok)
			{
				C.bGood = false;
				C.Title = TEXT("Не получилось");
				C.Sub = UFVSaveGame::BuyResultText(R);
				C.Primary = TEXT("К кузне");
				AgainButton = -1;
				ShowCard(C, EFVSfx::Error);
				return;
			}
			++P->U6.Forgings;
			Game->NotifyProfileChanged();

			const float Acc = Strikes > 0 ? GradeSum / float(Strikes) : 0.f;
			const int32 N = FMath::Max(1, Recipe->OutputCount);
			C.bGood = true;
			C.Title = Acc >= 0.9f ? FString(TEXT("Мастерская ковка!")) : FString(TEXT("Выковано!"));
			C.IconId = Recipe->OutputId;   // FVUI::ItemSprite knows rod ids too
			if (const FFVRodDef* OutRod = FVDB::FindRod(Recipe->OutputId))
			{
				// 0.6.2 (Р80): a rod output — Craft put it into OwnedRods and in hand (no item cell: it is not a stack)
				FString Short = OutRod->Name;
				Short.RemoveFromStart(TEXT("Удочка "));
				C.Sub = FString::Printf(TEXT("Выкована удочка %s (%s) — уже в руках"), *Short, *FVDBU6::RodTypeLabel(*OutRod));
			}
			else
			{
				C.Sub = FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(Recipe->OutputId), N);
				C.AddItem(Recipe->OutputId, N);
			}
			C.AddLine(FString::Printf(TEXT("Точность ударов: %d%%"), FMath::RoundToInt(Acc * 100.f)), Acc >= 0.65f ? InkGood() : FVPal::Ink());
			C.AddLine(FString::Printf(TEXT("Ударов: %d, точных: %d"), Strikes, Perfects), FVPal::InkSoft());
			if (Reheats > 0)
			{
				C.AddLine(FString::Printf(TEXT("В горн: %d %s"), Reheats, FVUI::Plural(Reheats, TEXT("раз"), TEXT("раза"), TEXT("раз"))), FVPal::InkSoft());
			}
			if (Cracks > 0) { C.AddLine(FString::Printf(TEXT("Трещин: %d"), Cracks), InkBad()); }
			const bool bMore = P->CanCraft(*Recipe, 1) == EFVBuyResult::Ok;
			C.Primary = TEXT("Готово");
			if (bMore) { C.Secondary = TEXT("Ещё"); }
			AgainButton = bMore ? 1 : -1;
			ShowCard(C, EFVSfx::Catch);
		}

		virtual void OnCardButton(int32 Index) override
		{
			if (Index >= 0 && Index == AgainButton && !bBot)
			{
				Card = FCard();
				Setup();
				return;
			}
			Leave();
		}

		// ------------------------------------------------------------------ drawing
		virtual void DrawActions(FVDraw& D, const FFVRect& R) override
		{
			if (!bReady) { return; }
			const float TS = FV::IsPortrait() ? FV::Ui::Body() + 1.f : 8.f;
			const bool bPlay = bInteractive && Phase == EPhase::Play;
			const bool bLow = Phase == EPhase::Play && Heat < 0.35f;
			const bool bBlink = bLow && FMath::Fmod(Time, 0.5f) < 0.3f;
			if (InstantButton(D, FVDraw::SplitH(R, 0, 2), TEXT("В горн"), bBlink ? EFVButton::Danger : EFVButton::Normal, bPlay && Heat < 0.97f, TS)) { Reheat(); }
			if (InstantButton(D, FVDraw::SplitH(R, 1, 2), TEXT("Ударить"), EFVButton::Primary, bPlay, TS)) { Strike(); }
		}

		virtual void DrawGame(FVDraw& D, const FFVRect& Area) override
		{
			if (!bReady) { return; }
			const bool bP = FV::IsPortrait();
			const float RowH = bP ? 18.f : 14.f;
			const float LS = bP ? FV::Ui::Small() : 7.f;

			// top: «Форма» and «Жар»
			const FFVRect TopR(Area.X, Area.Y, Area.W, RowH);
			const FFVRect FormR = FVDraw::SplitH(TopR, 0, 2, 8.f), HeatR = FVDraw::SplitH(TopR, 1, 2, 8.f);
			const float TY = FormR.Y + Snap((RowH - D.LineHeight(LS)) * 0.5f);
			D.Bar(FormR, Required > 0.f ? Progress / Required : 0.f, FV::Col(236, 190, 70), FV::Col(60, 40, 30), true);
			D.TextOutlined(FString::Printf(TEXT("Форма %d%%"), FMath::RoundToInt(Clamp01(Progress / FMath::Max(0.01f, Required)) * 100.f)),
				FVector2D(FormR.X + FormR.W * 0.5f, TY), LS, FVPal::White(), EFVAlign::Center);
			D.Bar(HeatR, Heat, HeatColor(Heat), FV::Col(40, 30, 28), true);
			D.Rect(FFVRect(Snap(HeatR.X + HeatR.W * ColdHeat), HeatR.Y, 1.f, HeatR.H), FV::Col(140, 210, 255, 220));
			const bool bCold = Heat < ColdHeat;
			D.TextOutlined(bCold ? FString(TEXT("Остыл!")) : (Phase == EPhase::Reheat ? FString(TEXT("Греется…")) : FString(TEXT("Жар"))),
				FVector2D(HeatR.X + HeatR.W * 0.5f, TY), LS, bCold ? FV::Col(160, 220, 255) : FVPal::White(), EFVAlign::Center);

			// bottom: the pendulum
			const float MH = bP ? 26.f : 18.f;
			const FFVRect MetR(Area.X + 8.f, Area.Bottom() - MH, Area.W - 16.f, MH);
			const FFVRect Scene(Area.X, TopR.Bottom() + 4.f, Area.W, FMath::Max(40.f, MetR.Y - 4.f - (TopR.Bottom() + 4.f)));

			DrawSmithy(D, Scene);
			DrawPendulum(D, MetR);

			// the whole scene is the strike button (finger-friendly)
			if (bInteractive && Phase == EPhase::Play)
			{
				const FFVRect HitR(Area.X, Scene.Y, Area.W, MetR.Bottom() - Scene.Y);
				if (D.PressedIn(HitR)) { Strike(); }
				D.Clicked(HitR);
			}
		}

		void DrawPendulum(FVDraw& D, const FFVRect& MetR)
		{
			D.Rect(MetR.Offset(1.f, 2.f), FV::Col(0, 0, 0, 70));
			D.Rect(MetR, FVPal::WoodDark());
			const FFVRect Tr = MetR.Inset(2.f);
			D.Rect(Tr, FV::Col(36, 26, 22));
			const float CX = Tr.X + Tr.W * 0.5f;
			const float HalfW = Tr.W * 0.5f - 3.f;
			const float GoodH = HalfW * FMath::Sin(PI * FMath::Min(0.5f, GoodWin / Period));
			const float PerfH = HalfW * FMath::Sin(PI * FMath::Min(0.5f, PerfectWin / Period));
			D.Rect(FFVRect(Snap(CX - GoodH), Tr.Y, Snap(GoodH * 2.f), Tr.H), FV::Col(96, 204, 64, 90));
			D.Rect(FFVRect(Snap(CX - PerfH), Tr.Y, Snap(PerfH * 2.f), Tr.H), WithAlpha(FV::Col(246, 206, 58), 0.45f + 0.35f * BeatFlash));
			D.Rect(FFVRect(Snap(CX), Tr.Y, 1.f, Tr.H), FV::Col(255, 240, 200, 160));
			const float U = (Phase == EPhase::Intro) ? -0.5f : BeatU();
			const float MX = Snap(CX + HalfW * FMath::Sin(PI * U));
			const float MW = FV::IsPortrait() ? 6.f : 5.f;
			const FFVRect Mk(MX - MW * 0.5f, MetR.Y - 2.f, MW, MetR.H + 4.f);
			D.Rect(Mk.Inset(-1.f), FV::Col(40, 26, 18));
			D.Rect(Mk, FMath::Lerp(FV::Col(214, 206, 196), FLinearColor::White, BeatFlash));
			D.Rect(FFVRect(Mk.X, Mk.Y, Mk.W, 2.f), FV::Col(255, 255, 255));
		}

		void DrawFlames(FVDraw& D, float CX, float BaseY, float HalfW, float Scale, float Intensity) const
		{
			const int32 N = FMath::Max(3, int32(HalfW * 2.f / (6.f * Scale)));
			for (int32 i = 0; i < N; ++i)
			{
				const float X = CX - HalfW + (float(i) + 0.5f) * (HalfW * 2.f / float(N));
				const float Hgt = (7.f + 4.f * FMath::Sin(Time * 14.f + float(i) * 1.9f) + 3.f * FMath::Sin(Time * 23.f + float(i) * 0.7f)) * Scale * Intensity;
				const float W = 5.f * Scale;
				if (Hgt <= 1.f) { continue; }
				D.Rect(FFVRect(Snap(X - W * 0.5f), Snap(BaseY - Hgt), Snap(W), Snap(Hgt)), FV::Col(236, 96, 36, 230));
				D.Rect(FFVRect(Snap(X - W * 0.25f), Snap(BaseY - Hgt * 0.6f), FMath::Max(1.f, Snap(W * 0.5f)), Snap(Hgt * 0.6f)), FV::Col(255, 214, 90, 240));
			}
		}

		void DrawSmithy(FVDraw& D, const FFVRect& S)
		{
			const bool bP = FV::IsPortrait();
			const float K = FMath::Clamp(FMath::Min(S.W / 240.f, S.H / 150.f), 0.9f, 2.2f);
			D.PushClip(S);

			// brick wall
			D.Rect(S, FV::Col(46, 32, 28));
			const float BH = FMath::Max(3.f, Snap(7.f * K)), BW = FMath::Max(6.f, Snap(16.f * K));
			const float FloorY = Snap(S.Y + S.H * (bP ? 0.72f : 0.80f));
			int32 Row = 0;
			for (float Y = S.Y; Y < FloorY; Y += BH, ++Row)
			{
				D.Rect(FFVRect(S.X, Snap(Y), S.W, 1.f), FV::Col(32, 22, 20));
				const float Off = (Row & 1) ? BW * 0.5f : 0.f;
				for (float X = S.X - Off; X < S.Right(); X += BW)
				{
					D.Rect(FFVRect(Snap(X), Snap(Y), 1.f, BH), FV::Col(32, 22, 20));
				}
			}
			// floor
			D.Rect(FFVRect(S.X, FloorY, S.W, S.Bottom() - FloorY), FV::Col(60, 48, 42));
			D.Rect(FFVRect(S.X, FloorY, S.W, 2.f), FV::Col(86, 70, 60));
			for (float X = S.X + 7.f * K; X < S.Right(); X += 22.f * K)
			{
				D.Rect(FFVRect(Snap(X), FloorY + 2.f, 1.f, S.Bottom() - FloorY), FV::Col(48, 38, 34));
			}

			// furnace (горн)
			const float Intensity = (Phase == EPhase::Reheat ? 1.3f : 0.8f) + 0.08f * FMath::Sin(Time * 7.f);
			const float FW = Snap(62.f * K), FH = Snap(74.f * K);
			const float FX = Snap(FMath::Max(S.X + 4.f, S.X + S.W * 0.2f - FW * 0.5f));
			const FFVRect Furn(FX, FloorY - FH, FW, FH);
			const FFVRect Chim(Snap(Furn.X + FW * 0.3f), S.Y, Snap(FW * 0.4f), Furn.Y - S.Y + 2.f);
			D.Rect(Chim.Inset(-1.f), FV::Col(40, 24, 20));
			D.Rect(Chim, FV::Col(92, 58, 46));
			D.Rect(Furn.Inset(-1.f), FV::Col(40, 24, 20));
			D.Rect(Furn, FV::Col(112, 70, 54));
			D.Rect(FFVRect(Furn.X, Furn.Y, Furn.W, Snap(3.f * K)), FV::Col(140, 94, 72));
			for (float Y = Furn.Y + 8.f * K; Y < Furn.Bottom(); Y += 8.f * K)
			{
				D.Rect(FFVRect(Furn.X, Snap(Y), Furn.W, 1.f), FV::Col(80, 48, 38));
			}
			const FFVRect Mouth(Snap(Furn.X + FW * 0.2f), Snap(FloorY - FH * 0.62f), Snap(FW * 0.6f), Snap(FH * 0.34f));
			const float MouthCX = Mouth.X + Mouth.W * 0.5f;
			D.Rect(Mouth.Inset(-1.f), FV::Col(52, 30, 24));
			D.Rect(Mouth, FV::Col(26, 12, 8));
			D.Glow(Mouth.Center(), 30.f * K * (1.f + 0.3f * Intensity), WithAlpha(FV::Col(255, 120, 40), 0.3f * Intensity));
			for (int32 i = 0; i < 5; ++i)
			{
				const float CW = Mouth.W / 5.f;
				const float Fl = 0.5f + 0.5f * FMath::Sin(Time * 5.f + float(i) * 2.1f);
				D.Rect(FFVRect(Snap(Mouth.X + CW * float(i) + 1.f), Snap(Mouth.Bottom() - 4.f * K), Snap(CW - 2.f), Snap(4.f * K)),
					FMath::Lerp(FV::Col(170, 40, 20), FV::Col(255, 170, 60), Clamp01(Fl * Intensity * 0.8f)));
			}
			DrawFlames(D, MouthCX, Mouth.Bottom() - 3.f * K, Mouth.W * 0.38f, K * 0.8f, Intensity);

			// anvil on a stump
			const float AX = Snap(S.X + S.W * (bP ? 0.62f : 0.58f));
			const FFVRect Stump(Snap(AX - 15.f * K), Snap(FloorY - 20.f * K), Snap(30.f * K), Snap(20.f * K));
			D.Rect(Stump.Inset(-1.f), FV::Col(52, 30, 18));
			D.Rect(Stump, FV::Col(112, 72, 42));
			D.Rect(FFVRect(Stump.X, Stump.Y, Stump.W, Snap(3.f * K)), FV::Col(148, 102, 62));
			D.Rect(FFVRect(Snap(Stump.X + 6.f * K), Stump.Y + 4.f * K, 1.f, Stump.H - 5.f * K), FV::Col(88, 54, 32));
			const FFVRect Waist(Snap(AX - 7.f * K), Snap(Stump.Y - 8.f * K), Snap(14.f * K), Snap(8.f * K));
			D.Rect(Waist.Inset(-1.f), FV::Col(34, 34, 40));
			D.Rect(Waist, FV::Col(70, 74, 84));
			const float TopW = Snap(46.f * K), TopH = Snap(8.f * K);
			const FFVRect Top(Snap(AX - TopW * 0.5f + 4.f * K), Snap(Waist.Y - TopH), TopW, TopH);
			const float TopCX = Top.X + Top.W * 0.5f;
			for (int32 i = 0; i < 7; ++i)   // horn
			{
				const float SW = 2.f * K;
				const float Hh = TopH * (0.3f + 0.7f * float(i) / 6.f);
				const FFVRect Sl(Snap(Top.X - 14.f * K + float(i) * SW), Top.Y, Snap(SW) + 1.f, Snap(Hh));
				D.Rect(Sl.Inset(-1.f), FV::Col(34, 34, 40));
				D.Rect(Sl, FV::Col(96, 100, 112));
			}
			D.Rect(Top.Inset(-1.f), FV::Col(34, 34, 40));
			D.Rect(Top, FV::Col(96, 100, 112));
			D.Rect(FFVRect(Top.X, Top.Y, Top.W, 2.f), FV::Col(158, 164, 178));
			D.Rect(FFVRect(Top.X, Top.Bottom() - 2.f, Top.W, 2.f), FV::Col(60, 62, 72));

			// the ingot (flattens and stretches with «Форма»)
			const float Prog = Required > 0.f ? Clamp01(Progress / Required) : 0.f;
			const float IW = Snap((20.f + 12.f * Prog) * K), IH = FMath::Max(2.f, Snap((6.f - 2.5f * Prog) * K));
			FFVRect Ingot(Snap(TopCX - IW * 0.5f), Top.Y - IH, IW, IH);
			if (Phase == EPhase::Reheat)
			{
				Ingot = FFVRect(Snap(MouthCX - IW * 0.5f), Snap(Mouth.Bottom() - 4.f * K - IH), IW, IH);
			}
			const FLinearColor HC = HeatColor(Heat);
			if (Heat >= ColdHeat) { D.Glow(Ingot.Center(), IW * 0.9f, WithAlpha(HC, 0.5f * Heat)); }
			if (Phase != EPhase::Over && Phase != EPhase::Done)
			{
				D.Rect(Ingot.Inset(-1.f), FV::Col(40, 26, 22));
				D.Rect(Ingot, HC);
				D.Rect(FFVRect(Ingot.X, Ingot.Y, Ingot.W, 1.f), FMath::Lerp(HC, FLinearColor::White, 0.4f));
				D.Rect(FFVRect(Ingot.X, Ingot.Bottom() - 1.f, Ingot.W, 1.f), FMath::Lerp(HC, FLinearColor::Black, 0.3f));
				const float MidY = Ingot.Y + Ingot.H * 0.5f;
				for (int32 c = 0; c < Cracks; ++c)   // cracks
				{
					const float CXc = Ingot.X + Ingot.W * (0.25f + 0.25f * float(c));
					D.Line(FVector2D(CXc, Ingot.Y), FVector2D(CXc + 2.f * K, MidY), FV::Col(30, 20, 18), 1.f);
					D.Line(FVector2D(CXc + 2.f * K, MidY), FVector2D(CXc - 1.f * K, Ingot.Bottom()), FV::Col(30, 20, 18), 1.f);
				}
			}
			IngotPos = FVector2D(Ingot.X + Ingot.W * 0.5f, Ingot.Y);

			// the finished item pops above the anvil
			if (Phase == EPhase::Done && Recipe)
			{
				if (const FFVSprite* IS = FVUI::ItemSprite(Recipe->OutputId))
				{
					const float Pop = EaseOutBack(Clamp01(PhaseT / 0.35f));
					const float Box = 26.f * K * Pop;
					const float PX = TopCX, PY = Top.Y - 16.f * K;
					D.Glow(FVector2D(PX, PY), FMath::Max(1.f, Box), WithAlpha(FV::Col(255, 220, 120), 0.5f));
					if (Box > 2.f) { FVUI::SpriteFit(D, *IS, FFVRect(PX - Box * 0.5f, PY - Box * 0.5f, Box, Box), FLinearColor::White, 4.f); }
				}
			}

			// the hammer: raised at rest, down for a moment after a strike
			if (Phase != EPhase::Done && Phase != EPhase::Over)
			{
				const FVector2D Pivot(TopCX + 34.f * K, Top.Y - 7.f * K);
				const float Rest = 245.f + 4.f * FMath::Sin(Time * 3.f);
				float A = Rest;
				if (HammerT < 0.05f) { A = 180.f; }
				else if (HammerT < 0.3f) { A = FMath::Lerp(180.f, Rest, Smooth01((HammerT - 0.05f) / 0.25f)); }
				const float Rad = FMath::DegreesToRadians(A);
				const FVector2D Dir(FMath::Cos(Rad), FMath::Sin(Rad));
				const FVector2D Perp(-Dir.Y, Dir.X);
				const FVector2D Head = Pivot + Dir * double(32.f * K);
				const FVector2D Grip = Pivot - Dir * double(10.f * K);
				D.Line(Grip, Head, FV::Col(60, 36, 20), 3.5f * K);
				D.Line(Grip, Head, FV::Col(132, 88, 50), 2.f * K);
				D.Line(Head - Perp * double(7.f * K), Head + Perp * double(7.f * K), FV::Col(34, 34, 40), 7.f * K);
				D.Line(Head - Perp * double(6.f * K), Head + Perp * double(6.f * K), FV::Col(92, 96, 108), 5.f * K);
				const FVector2D Hl = Head - Dir * double(1.5f * K);
				D.Line(Hl - Perp * double(5.f * K), Hl + Perp * double(5.f * K), FV::Col(150, 156, 170), 1.2f * K);
			}

			// what is forged + cracks
			const float LS = bP ? FV::Ui::Small() : 7.f;
			if (Cracks > 0)
			{
				D.TextOutlined(FString::Printf(TEXT("Трещины: %d из %d"), Cracks, MaxCracks), FVector2D(S.X + 6.f, S.Y + 4.f), LS, FV::Col(255, 150, 130), EFVAlign::Left);
			}
			if (Recipe)
			{
				// 0.6.2 (Р80): a rod is one rod — its name without «×1»
				const FString What = FVDB::FindRod(Recipe->OutputId) ? FVDB::ItemName(Recipe->OutputId)
					: FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(Recipe->OutputId), FMath::Max(1, Recipe->OutputCount));
				const float IconS = bP ? 22.f : 16.f;
				const float TW = float(D.MeasureText(What, LS, true).X);
				const float X0 = Snap(S.Right() - 6.f - TW - IconS - 4.f);
				const float Y0 = bP ? Snap(FloorY + (S.Bottom() - FloorY - IconS) * 0.5f) : S.Y + 4.f;
				if (const FFVSprite* IS = FVUI::ItemSprite(Recipe->OutputId)) { FVUI::SpriteFit(D, *IS, FFVRect(X0, Y0, IconS, IconS), FLinearColor::White, 2.f); }
				D.TextOutlined(What, FVector2D(X0 + IconS + 4.f, Y0 + Snap((IconS - D.LineHeight(LS)) * 0.5f)), LS, FVPal::White(), EFVAlign::Left);
			}
			D.PopClip();
		}
	};

	// =====================================================================================================
	// EXCAVATION (context = "cave:<boulderId>" / "city:rubble_N")
	// =====================================================================================================
	/** A find's shape in cells (offsets from its top-left). */
	struct FShape
	{
		int32 N;
		int32 X[4];
		int32 Y[4];
	};
	// hard rock: 2–4 cells (§9а)
	static const FShape GStoneHardShapes[] = {
		{ 2, { 0, 1, 0, 0 }, { 0, 0, 0, 0 } },
		{ 2, { 0, 0, 0, 0 }, { 0, 1, 0, 0 } },
		{ 3, { 0, 1, 2, 0 }, { 0, 0, 0, 0 } },
		{ 3, { 0, 0, 0, 0 }, { 0, 1, 2, 0 } },
		{ 3, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } },
		{ 3, { 0, 1, 1, 0 }, { 0, 0, 1, 0 } },
		{ 4, { 0, 1, 0, 1 }, { 0, 0, 1, 1 } },
		{ 4, { 0, 1, 2, 1 }, { 0, 0, 0, 1 } },
		{ 4, { 0, 0, 0, 1 }, { 0, 1, 2, 2 } },
	};
	// scrap: 1–2 cells; pebble: 1 cell
	static const FShape GStoneScrapShapes[] = {
		{ 1, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
		{ 2, { 0, 1, 0, 0 }, { 0, 0, 0, 0 } },
		{ 2, { 0, 0, 0, 0 }, { 0, 1, 0, 0 } },
	};
	static const FShape GStonePebbleShape = { 1, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };

	class FExcavationScreen final : public FStoneGame
	{
	public:
		explicit FExcavationScreen(UFVGame* InGame) : FStoneGame(InGame, NAME_None) {}

		virtual void OnOpen() override
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { OpenFailed(TEXT("Профиль не загружен."), EFVIcon::Pickaxe); return; }
			const FName CtxName = Game->GetScreenContext();
			const FString Ctx = CtxName.ToString();
			int32 Colon = INDEX_NONE;
			if (CtxName.IsNone() || !Ctx.FindChar(TEXT(':'), Colon) || Colon <= 0 || Colon >= Ctx.Len() - 1)
			{
				OpenFailed(TEXT("Не выбран валун."), EFVIcon::Pickaxe);
				return;
			}
			const FString Anchor = Ctx.Mid(Colon + 1);
			bRubble = Anchor.StartsWith(TEXT("rubble"));
			BoulderCount = bRubble ? 3 : 1;
			if (bRubble) { RubbleId = FName(*Anchor); }
			else { NodeId = CtxName; }

			if (P->U6.PickaxeLevel < 1)
			{
				OpenFailed(TEXT("Нужна кирка. Чертёж продают в Городе, а саму кирку делают в мастерской."), EFVIcon::Pickaxe, TEXT("Нечем копать"));
				return;
			}
			if (bRubble)
			{
				if (P->GetLevel() < RubbleMinLevel)
				{
					OpenFailed(FString::Printf(TEXT("Завал можно разобрать с %d уровня."), RubbleMinLevel), EFVIcon::Lock, TEXT("Пока рано"));
					return;
				}
				if (P->U6.RubbleCleared.Contains(RubbleId))
				{
					OpenFailed(TEXT("Этот завал уже разобран."), EFVIcon::Check, TEXT("Путь свободен"));
					return;
				}
			}
			else if (!P->IsNodeReady(NodeId))
			{
				OpenFailed(TEXT("Валун ещё не восстановился. Загляните попозже."), EFVIcon::Clock, TEXT("Здесь пусто"));
				return;
			}

			const TArray<FFVToolDef>& Picks = FVDBU6::Pickaxes();
			const int32 Lv = P->U6.PickaxeLevel;
			PickMult = Picks.IsValidIndex(Lv - 1) ? Picks[Lv - 1].HitsMult : (Lv >= 2 ? 0.7f : 1.f);
			PickMult = FMath::Clamp(PickMult, 0.3f, 2.f);

			// deterministic layout: the same boulder shows the same face until it regrows (leaving changes nothing)
			uint32 Seed = GetTypeHash(bRubble ? RubbleId : NodeId);
			if (!bRubble)
			{
				if (const FFVWorldNode* N = P->FindNode(NodeId)) { Seed = (Seed * 16777619u) ^ GetTypeHash(N->ReadyAt); }
			}
			BaseSeed = Seed;
			Collected.Reset();
			FindsTotal = 0;
			FindsRevealed = 0;
			Hits = 0;
			StartBoulder(0);
			bReady = true;
		}

	protected:
		static constexpr int32 GW = 8;
		static constexpr int32 GH = 6;
		static constexpr int32 NC = GW * GH;
		static constexpr float HammerCost = 3.f;     // durability per hammer hit
		static constexpr float PickCost = 8.f;       // durability per pickaxe hit (x FFVToolDef::HitsMult)

		enum class EFind : uint8 { Hardrock, Scrap, Pebble };
		enum class EPhase : uint8 { Intro, Play, Crumble, Over };
		enum class ETool : uint8 { Pickaxe, Hammer };

		struct FFind
		{
			EFind Kind = EFind::Pebble;
			TArray<int32> Cells;
			FName ItemId;
			int32 Count = 1;
			bool bRevealed = false;
			float RevealT = 99.f;
			int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
		};
		struct FCell
		{
			int32 Layers = 0;
			int32 Find = -1;
			int32 Variant = 0;
			float HitT = 99.f;
		};
		struct FCrack
		{
			FVector2D Pts[5];   // 0..1 in grid space
		};

		bool bReady = false;
		bool bRubble = false;
		bool bFinished = false;
		bool bAllFound = false;
		bool bWarned = false;
		FName NodeId;
		FName RubbleId;
		int32 BoulderIndex = 0;
		int32 BoulderCount = 1;
		uint32 BaseSeed = 0;
		int32 FaceSeed = 0;
		EPhase Phase = EPhase::Over;
		float PhaseT = 0.f;
		ETool Tool = ETool::Pickaxe;
		float PickMult = 1.f;
		float Durability = 100.f;
		float MaxDurability = 100.f;
		FCell Cells[NC];
		TArray<FFind> Finds;
		TArray<FCrack> CrackLines;
		TArray<FFVIngredient> Collected;
		int32 FindsTotal = 0;
		int32 FindsRevealed = 0;
		int32 Hits = 0;
		FIntPoint Cursor = FIntPoint(3, 2);
		bool bKeyCursor = false;
		int32 HitCell = -1;
		float HitAge = 99.f;
		ETool HitTool = ETool::Pickaxe;
		float BotT = 0.f;
		FFVRect LastGrid;
		float LastCs = 16.f;

		virtual FString Title() const override { return bRubble ? FString(TEXT("Завал")) : FString(TEXT("Раскопка")); }
		virtual FString ShotSlug() const override { return TEXT("excavation"); }
		virtual bool QuitNeedsConfirm() const override { return bReady && Phase != EPhase::Over && (Hits > 0 || BoulderIndex > 0); }
		virtual FString QuitBody() const override
		{
			return bRubble ? FString(TEXT("Завал придётся разбирать заново.")) : FString(TEXT("Валун останется нетронутым."));
		}
		virtual FString Hint() const override
		{
			if (!bReady) { return FString(); }
			return (FV::IsPortrait() || FV::IsTouchUI()) ? FString(TEXT("Нажмите на клетку. Блеск под породой — ценная находка."))
				: FString(TEXT("Клик — удар, 1 / 2 — инструмент. Блеск — ценная находка."));
		}

		// ------------------------------------------------------------------ setup
		bool PlaceFind(FRandomStream& R, EFind Kind, const FShape& S)
		{
			int32 W = 1, H = 1;
			for (int32 k = 0; k < S.N; ++k) { W = FMath::Max(W, S.X[k] + 1); H = FMath::Max(H, S.Y[k] + 1); }
			for (int32 Try = 0; Try < 60; ++Try)
			{
				const int32 OX = R.RandRange(0, GW - W), OY = R.RandRange(0, GH - H);
				bool bFree = true;
				for (int32 k = 0; k < S.N && bFree; ++k)
				{
					if (Cells[(OY + S.Y[k]) * GW + OX + S.X[k]].Find >= 0) { bFree = false; }
				}
				if (!bFree) { continue; }
				const int32 Index = Finds.Num();
				FFind& F = Finds.AddDefaulted_GetRef();
				F.Kind = Kind;
				F.MinX = GW; F.MinY = GH; F.MaxX = 0; F.MaxY = 0;
				for (int32 k = 0; k < S.N; ++k)
				{
					const int32 X = OX + S.X[k], Y = OY + S.Y[k];
					Cells[Y * GW + X].Find = Index;
					F.Cells.Add(Y * GW + X);
					F.MinX = FMath::Min(F.MinX, X); F.MinY = FMath::Min(F.MinY, Y);
					F.MaxX = FMath::Max(F.MaxX, X); F.MaxY = FMath::Max(F.MaxY, Y);
				}
				switch (Kind)
				{
				case EFind::Hardrock: F.ItemId = HardrockId(); F.Count = S.N >= 3 ? 2 : 1; break;   // (предл.)
				case EFind::Scrap:    F.ItemId = ScrapId();    F.Count = 1; break;
				default:              F.ItemId = PebbleId();   F.Count = 1; break;
				}
				return true;
			}
			return false;
		}

		void StartBoulder(int32 Index)
		{
			BoulderIndex = Index;
			FRandomStream R(int32((BaseSeed ^ (uint32(Index + 1) * 2654435761u)) & 0x7fffffffu));
			const int32 NoiseSeed = R.RandRange(1, 1 << 20);
			for (int32 i = 0; i < NC; ++i)
			{
				const int32 X = i % GW, Y = i / GW;
				const float Nz = FVUI::ValueNoise(float(X) * 0.6f, float(Y) * 0.6f, NoiseSeed);
				int32 L = 1 + FMath::Clamp(int32(Nz * 3.f), 0, 2);
				if (R.FRand() < 0.12f) { L = FMath::Min(3, L + 1); }
				if (bRubble) { L = FMath::Max(2, L); }
				Cells[i] = FCell();
				Cells[i].Layers = L;
				Cells[i].Variant = R.RandRange(0, 3);
			}
			Finds.Reset();
			// counts (предл.): a cave boulder always hides hard rock; the rubble is mostly plain stone
			const int32 NumHard = bRubble ? (R.FRand() < 0.15f ? 1 : 0) : (1 + (R.FRand() < 0.35f ? 1 : 0));
			int32 NumScrap = 0;
			if (bRubble) { NumScrap = R.RandRange(1, 2); }
			else { const float Roll = R.FRand(); NumScrap = Roll < 0.3f ? 0 : (Roll < 0.8f ? 1 : 2); }
			const int32 NumPebbles = bRubble ? R.RandRange(3, 5) : R.RandRange(2, 4);
			constexpr int32 NumHardShapes = int32(UE_ARRAY_COUNT(GStoneHardShapes));
			constexpr int32 NumScrapShapes = int32(UE_ARRAY_COUNT(GStoneScrapShapes));
			for (int32 i = 0; i < NumHard; ++i) { PlaceFind(R, EFind::Hardrock, GStoneHardShapes[R.RandRange(0, NumHardShapes - 1)]); }
			for (int32 i = 0; i < NumScrap; ++i) { PlaceFind(R, EFind::Scrap, GStoneScrapShapes[R.RandRange(0, NumScrapShapes - 1)]); }
			for (int32 i = 0; i < NumPebbles; ++i) { PlaceFind(R, EFind::Pebble, GStonePebbleShape); }
			FindsTotal += Finds.Num();

			MaxDurability = bRubble ? 90.f : 100.f;
			Durability = MaxDurability;
			FaceSeed = R.RandRange(0, 1 << 20);
			CrackLines.Reset();
			for (int32 c = 0; c < 7; ++c)
			{
				FCrack& Cr = CrackLines.AddDefaulted_GetRef();
				const int32 Edge = R.RandRange(0, 3);
				const float T = R.FRandRange(0.1f, 0.9f);
				FVector2D Pt = Edge == 0 ? FVector2D(T, 0.f) : (Edge == 1 ? FVector2D(1.f, T) : (Edge == 2 ? FVector2D(T, 1.f) : FVector2D(0.f, T)));
				const FVector2D Inward = (FVector2D(0.5f, 0.5f) - Pt).GetSafeNormal();
				for (int32 k = 0; k < 5; ++k)
				{
					Cr.Pts[k] = Pt;
					Pt += Inward * double(R.FRandRange(0.05f, 0.1f)) + FVector2D(R.FRandRange(-0.04f, 0.04f), R.FRandRange(-0.04f, 0.04f));
				}
			}
			bAllFound = false;
			bWarned = false;
			HitCell = -1;
			HitAge = 99.f;
			Phase = EPhase::Intro;
			PhaseT = 0.f;
			Banner(bRubble ? FString::Printf(TEXT("Валун %d из %d"), Index + 1, BoulderCount) : FString(TEXT("Раскопка!")), FVPal::Gold());
		}

		// ------------------------------------------------------------------ logic
		FVector2D CellCenter(int32 X, int32 Y) const
		{
			return FVector2D(LastGrid.X + (float(X) + 0.5f) * LastCs, LastGrid.Y + (float(Y) + 0.5f) * LastCs);
		}
		FVector2D FindCenter(const FFind& F) const
		{
			return FVector2D(LastGrid.X + (float(F.MinX + F.MaxX) * 0.5f + 0.5f) * LastCs, LastGrid.Y + (float(F.MinY + F.MaxY) * 0.5f + 0.5f) * LastCs);
		}
		static FLinearColor LayerColor(int32 L)
		{
			return L >= 3 ? FV::Col(100, 90, 86) : (L == 2 ? FV::Col(132, 120, 108) : FV::Col(170, 154, 132));
		}

		void SetTool(ETool T)
		{
			if (Tool == T) { return; }
			Tool = T;
			Sfx(EFVSfx::Click, 0.6f);
		}

		void Hit(ETool T, int32 X, int32 Y)
		{
			if (Phase != EPhase::Play || X < 0 || X >= GW || Y < 0 || Y >= GH) { return; }
			static const int32 PickDX[5] = { 0, 1, -1, 0, 0 };
			static const int32 PickDY[5] = { 0, 0, 0, 1, -1 };
			static const int32 PickAmt[5] = { 2, 1, 1, 1, 1 };
			const int32 NT = T == ETool::Pickaxe ? 5 : 1;
			int32 Removed = 0;
			for (int32 k = 0; k < NT; ++k)
			{
				const int32 CX = X + PickDX[k], CY = Y + PickDY[k];
				if (CX < 0 || CX >= GW || CY < 0 || CY >= GH) { continue; }
				FCell& C = Cells[CY * GW + CX];
				if (C.Layers <= 0) { continue; }
				const int32 Prev = C.Layers;
				C.Layers = FMath::Max(0, C.Layers - (T == ETool::Pickaxe ? PickAmt[k] : 1));
				Removed += Prev - C.Layers;
				C.HitT = 0.f;
				Juice.Burst(CellCenter(CX, CY), LayerColor(Prev), 3 + (Prev - C.Layers) * 2, 70.f, 300.f, 2.5f, 0.5f);
			}
			HitCell = Y * GW + X;
			HitAge = 0.f;
			HitTool = T;
			if (Removed == 0)
			{
				Sfx(EFVSfx::Click, 0.5f, 0.7f);   // bare rock: nothing to chip, no durability spent
				return;
			}
			++Hits;
			Durability = FMath::Max(0.f, Durability - (T == ETool::Pickaxe ? PickCost * PickMult : HammerCost));
			Sfx(EFVSfx::Pickaxe, T == ETool::Pickaxe ? 1.f : 0.75f, (T == ETool::Pickaxe ? 0.85f : 1.2f) + Juice.Rng.FRandRange(-0.05f, 0.05f));
			Juice.Kick(T == ETool::Pickaxe ? 1.9f * PickMult : 0.45f);

			bool bAll = true;
			for (FFind& F : Finds)
			{
				if (F.bRevealed) { continue; }
				bool bClear = true;
				for (const int32 Idx : F.Cells) { if (Cells[Idx].Layers > 0) { bClear = false; break; } }
				if (!bClear) { bAll = false; continue; }
				F.bRevealed = true;
				F.RevealT = 0.f;
				++FindsRevealed;
				const bool bValuable = F.Kind != EFind::Pebble;
				Sfx(EFVSfx::Find, bValuable ? 1.f : 0.7f, F.Kind == EFind::Hardrock ? 1.f : 1.15f);
				const FVector2D FC = FindCenter(F);
				Juice.Sparkles(FC, bValuable ? 10 : 4, LastCs * 0.6f, F.Kind == EFind::Scrap ? FV::Col(200, 220, 255) : FV::Col(255, 238, 150));
				Juice.Text(FVDB::ItemName(F.ItemId), FC + FVector2D(0.f, -LastCs * 0.5f), F.Kind == EFind::Hardrock ? FV::Col(255, 226, 110) : FVPal::White(),
					FV::IsPortrait() ? 10.f : 8.f);
			}
			if (bAll && Finds.Num() > 0)
			{
				bAllFound = true;
				Banner(TEXT("Всё откопано!"), FVPal::Gold());
				EndBoulder();
				return;
			}
			if (Durability <= 0.f)
			{
				Banner(TEXT("Валун рассыпался!"), FV::Col(255, 190, 140));
				EndBoulder();
				return;
			}
			if (!bWarned && Durability < MaxDurability * 0.25f)
			{
				bWarned = true;
				Banner(TEXT("Валун трещит!"), FV::Col(255, 170, 110));
			}
		}

		/** The boulder is done (crumbled / all found / «Забрать»): only fully uncovered finds are kept. */
		void EndBoulder()
		{
			if (Phase != EPhase::Play) { return; }
			Phase = EPhase::Crumble;
			PhaseT = 0.f;
			Sfx(EFVSfx::RockBreak, 0.9f);
			Juice.Kick(2.2f);
			for (int32 i = 0; i < NC; ++i)
			{
				if (Cells[i].Layers > 0 && (i % 3) == 0)
				{
					Juice.Burst(CellCenter(i % GW, i / GW), LayerColor(Cells[i].Layers), 2, 90.f, 360.f, 3.f, 0.7f);
				}
			}
			for (const FFind& F : Finds)
			{
				if (F.bRevealed) { AddIngredient(Collected, F.ItemId, F.Count); }
			}
		}

		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			if (!bReady) { return; }
			PhaseT += Dt;
			HitAge += Dt;
			for (FCell& C : Cells) { C.HitT += Dt; }
			for (FFind& F : Finds) { F.RevealT += Dt; }
			switch (Phase)
			{
			case EPhase::Intro:
				if (PhaseT >= 0.6f) { Phase = EPhase::Play; PhaseT = 0.f; }
				break;
			case EPhase::Play:
			{
				RequestPlayShot();
				if (bBot) { TickBot(Dt); break; }
				int32 DX = 0, DY = 0;
				if (In.WasPressed(EKeys::Left) || In.WasPressed(EKeys::A)) { DX = -1; }
				else if (In.WasPressed(EKeys::Right) || In.WasPressed(EKeys::D)) { DX = 1; }
				else if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { DY = -1; }
				else if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { DY = 1; }
				if (DX != 0 || DY != 0)
				{
					Cursor.X = FMath::Clamp(Cursor.X + DX, 0, GW - 1);
					Cursor.Y = FMath::Clamp(Cursor.Y + DY, 0, GH - 1);
					bKeyCursor = true;
				}
				if (In.WasPressed(EKeys::One)) { SetTool(ETool::Pickaxe); }
				else if (In.WasPressed(EKeys::Two)) { SetTool(ETool::Hammer); }
				else if (In.WasPressed(EKeys::Tab) || In.WasPressed(EKeys::Q) || In.WasPressed(EKeys::Gamepad_FaceButton_Left))
				{
					SetTool(Tool == ETool::Pickaxe ? ETool::Hammer : ETool::Pickaxe);
				}
				if (ConfirmPressed(In))
				{
					bKeyCursor = true;
					Hit(Tool, Cursor.X, Cursor.Y);
				}
				break;
			}
			case EPhase::Crumble:
				if (PhaseT >= 1.2f)
				{
					if (BoulderIndex + 1 < BoulderCount) { StartBoulder(BoulderIndex + 1); }
					else { Finish(); }
				}
				break;
			default:
				break;
			}
		}

		/** Autotest: hammers the cells over the finds (the bot "knows" the layout), then finishes. */
		void TickBot(float Dt)
		{
			BotT -= Dt;
			if (BotT > 0.f) { return; }
			BotT = 0.2f;
			for (const FFind& F : Finds)
			{
				if (F.bRevealed) { continue; }
				for (const int32 Idx : F.Cells)
				{
					if (Cells[Idx].Layers > 0)
					{
						Tool = ETool::Hammer;
						Hit(ETool::Hammer, Idx % GW, Idx / GW);
						return;
					}
				}
			}
			EndBoulder();
		}

		void Finish()
		{
			if (bFinished) { return; }
			bFinished = true;
			Phase = EPhase::Over;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { Leave(); return; }
			FCard C;
			C.Icon = EFVIcon::Pickaxe;
			if (bRubble)
			{
				const EFVBuyResult R = P->ClearRubble(RubbleId);
				if (R != EFVBuyResult::Ok)
				{
					C.bGood = false;
					C.Title = TEXT("Завал устоял");
					C.Sub = UFVSaveGame::BuyResultText(R);
					C.Primary = TEXT("Назад");
					ShowCard(C, EFVSfx::Error);
					return;
				}
				P->U6.Excavations += BoulderCount;
				TArray<FString> Lost;
				for (const FFVIngredient& I : Collected)
				{
					// a full backpack never loses the finds of a rubble (it is cleared only once): they go to the chest
					if (P->GiveItem(I.ItemId, I.Count) || P->AddTo(EFVContainer::Chest, I.ItemId, I.Count)) { C.AddItem(I.ItemId, I.Count); }
					else { Lost.Add(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(I.ItemId), I.Count)); }
				}
				Game->NotifyProfileChanged();
				Game->GetScene().RefreshWorldObjects();
				C.bGood = true;
				C.Title = TEXT("Завал разобран!");
				const int32 Left = FMath::Max(0, RubbleTotal - P->U6.RubbleCleared.Num());
				if (P->IsCaveOpen()) { C.Sub = TEXT("Путь в Пещеру открыт!"); }
				else if (Left > 0) { C.Sub = FString::Printf(TEXT("До Пещеры осталось завалов: %d"), Left); }
				else { C.Sub = TEXT("Путь к Пещере стал свободнее."); }
				C.AddLine(FString::Printf(TEXT("Разбито валунов: %d"), BoulderCount), FVPal::InkSoft());
				if (C.Items.Num() == 0 && Lost.Num() == 0) { C.AddLine(TEXT("Находок не было — только камень."), FVPal::InkSoft()); }
				if (Lost.Num() > 0) { C.AddLine(FString::Printf(TEXT("Рюкзак полон, пропало: %s"), *FString::Join(Lost, TEXT(", "))), InkBad()); }
				ShowCard(C, EFVSfx::Achievement);
				return;
			}

			const EFVBuyResult R = P->ExcavateBoulder(NodeId, Collected);
			if (R == EFVBuyResult::NoRoom)
			{
				C.bGood = false;
				C.Title = TEXT("Рюкзак полон");
				C.Sub = TEXT("Находки остались в валуне. Освободите место в рюкзаке и возвращайтесь.");
				C.Primary = TEXT("Назад");
				ShowCard(C, EFVSfx::Error);
				return;
			}
			if (R != EFVBuyResult::Ok)
			{
				C.bGood = false;
				C.Title = TEXT("Не получилось");
				C.Sub = UFVSaveGame::BuyResultText(R);
				C.Primary = TEXT("Назад");
				ShowCard(C, EFVSfx::Error);
				return;
			}
			++P->U6.Excavations;
			Game->NotifyProfileChanged();
			Game->GetScene().RefreshWorldObjects();
			for (const FFVIngredient& I : Collected) { C.AddItem(I.ItemId, I.Count); }
			C.bGood = Collected.Num() > 0;
			C.Title = bAllFound ? FString(TEXT("Всё откопано!")) : (C.bGood ? FString(TEXT("Раскопка окончена")) : FString(TEXT("Ничего не досталось")));
			C.Sub = FString::Printf(TEXT("Откопано находок: %d из %d"), FindsRevealed, FindsTotal);
			if (!C.bGood) { C.AddLine(TEXT("Достаётся только то, что откопано целиком."), FVPal::InkSoft()); }
			ShowCard(C, C.bGood ? EFVSfx::Catch : EFVSfx::Escape);
		}

		// ------------------------------------------------------------------ drawing
		virtual void DrawActions(FVDraw& D, const FFVRect& R) override
		{
			if (!bReady) { return; }
			const float TS = FV::IsPortrait() ? FV::Ui::Body() + 1.f : 8.f;
			const bool bOn = bInteractive && (Phase == EPhase::Play || Phase == EPhase::Intro);
			const FFVRect A = FVDraw::SplitH(R, 0, 2), B = FVDraw::SplitH(R, 1, 2);
			if (D.Button(A, TEXT("Кирка"), Tool == ETool::Pickaxe ? EFVButton::Primary : EFVButton::Normal, bOn, TS)) { SetTool(ETool::Pickaxe); }
			if (D.Button(B, TEXT("Молоток"), Tool == ETool::Hammer ? EFVButton::Primary : EFVButton::Normal, bOn, TS)) { SetTool(ETool::Hammer); }
			const float IS = FMath::Min(16.f, A.H - 4.f);
			if (const FFVSprite* PickIcon = FVUI::Icon(EFVIcon::Pickaxe)) { FVUI::SpriteFit(D, *PickIcon, FFVRect(A.X + 4.f, A.Y + Snap((A.H - IS) * 0.5f), IS, IS), FLinearColor::White, 1.f); }
			if (const FFVSprite* HamIcon = FVUI::Icon(EFVIcon::Hammer)) { FVUI::SpriteFit(D, *HamIcon, FFVRect(B.X + 4.f, B.Y + Snap((B.H - IS) * 0.5f), IS, IS), FLinearColor::White, 1.f); }
		}

		virtual void DrawGame(FVDraw& D, const FFVRect& Area) override
		{
			if (!bReady) { return; }
			const bool bP = FV::IsPortrait();
			const float LS = bP ? FV::Ui::Small() : 7.f;

			// top: durability of the boulder
			const float BarH = bP ? 16.f : 13.f;
			const FFVRect TopR(Area.X, Area.Y, Area.W, BarH);
			const FString Lbl = TEXT("Прочность");
			const float LW = float(D.MeasureText(Lbl, LS, true).X) + 6.f;
			const float TY = TopR.Y + Snap((BarH - D.LineHeight(LS)) * 0.5f);
			D.Text(Lbl, FVector2D(TopR.X, TY), LS, FVPal::Ink(), EFVAlign::Left, false, true);
			float BarRight = TopR.Right();
			if (bRubble)
			{
				const FString Cnt = FString::Printf(TEXT("Валун %d из %d"), BoulderIndex + 1, BoulderCount);
				const float CW = float(D.MeasureText(Cnt, LS, true).X);
				D.Text(Cnt, FVector2D(TopR.Right(), TY), LS, FVPal::Ink(), EFVAlign::Right, false, true);
				BarRight -= CW + 8.f;
			}
			const float Fr = MaxDurability > 0.f ? Clamp01(Durability / MaxDurability) : 0.f;
			D.Bar(FFVRect(TopR.X + LW, TopR.Y, FMath::Max(20.f, BarRight - TopR.X - LW), BarH), Fr, MeterColor(Fr), FV::Col(60, 40, 30), true);

			// layout: grid + finds panel (right in landscape, under the grid in portrait)
			constexpr float Rim = 7.f;
			FFVRect Grid, Side;
			if (bP)
			{
				const float SideH = 44.f;
				const float AvailTop = TopR.Bottom() + 8.f;
				const float AvailH = Area.Bottom() - AvailTop;
				const float Cs = FMath::Max(12.f, FMath::FloorToFloat(FMath::Min((Area.W - 2.f * Rim) / float(GW), (AvailH - SideH - 10.f - 2.f * Rim) / float(GH))));
				const float GroupH = Cs * float(GH) + 2.f * Rim + 10.f + SideH;
				const float Y0 = AvailTop + FMath::Max(0.f, (AvailH - GroupH) * 0.35f);
				Grid = FFVRect(Snap(Area.X + Area.W * 0.5f - Cs * float(GW) * 0.5f), Snap(Y0 + Rim), Cs * float(GW), Cs * float(GH));
				Side = FFVRect(Area.X, Grid.Bottom() + Rim + 10.f, Area.W, SideH);
				LastCs = Cs;
			}
			else
			{
				const float SideW = 170.f;
				const FFVRect GA(Area.X, TopR.Bottom() + 6.f, Area.W - SideW - 8.f, Area.Bottom() - TopR.Bottom() - 6.f);
				const float Cs = FMath::Max(12.f, FMath::FloorToFloat(FMath::Min((GA.W - 2.f * Rim) / float(GW), (GA.H - 2.f * Rim) / float(GH))));
				Grid = FFVRect(Snap(GA.X + GA.W * 0.5f - Cs * float(GW) * 0.5f), Snap(GA.Y + GA.H * 0.5f - Cs * float(GH) * 0.5f), Cs * float(GW), Cs * float(GH));
				Side = FFVRect(Area.Right() - SideW, GA.Y, SideW, GA.H);
				LastCs = Cs;
			}
			LastGrid = Grid;

			DrawBoulder(D, Grid, LastCs);
			DrawSide(D, Side);

			// pointer: a press on a cell hits it at once
			if (bInteractive && Phase == EPhase::Play)
			{
				if (D.PressedIn(Grid))
				{
					const FVector2D M = D.Mouse();
					const int32 X = FMath::Clamp(int32((float(M.X) - Grid.X) / LastCs), 0, GW - 1);
					const int32 Y = FMath::Clamp(int32((float(M.Y) - Grid.Y) / LastCs), 0, GH - 1);
					Cursor = FIntPoint(X, Y);
					bKeyCursor = false;
					Hit(Tool, X, Y);
				}
				D.Clicked(Grid);
			}
		}

		void DrawBedrock(FVDraw& D, const FFVRect& R, int32 X, int32 Y) const
		{
			D.Rect(R, FV::Col(56, 48, 46));
			for (int32 k = 0; k < 3; ++k)
			{
				const float HX = FVUI::Hash01(X * 7 + k, Y * 5, FaceSeed), HY = FVUI::Hash01(X * 3, Y * 11 + k, FaceSeed + 1);
				D.Rect(FFVRect(Snap(R.X + 2.f + HX * (R.W - 5.f)), Snap(R.Y + 2.f + HY * (R.H - 5.f)), 2.f, 2.f), FV::Col(72, 62, 58));
			}
		}

		void DrawFindCell(FVDraw& D, const FFVRect& R, int32 FindIdx, int32 X, int32 Y) const
		{
			const FFind& F = Finds[FindIdx];
			const float Cs = R.W;
			auto SameFind = [this, FindIdx](int32 NX, int32 NY)
			{
				return NX >= 0 && NX < GW && NY >= 0 && NY < GH && Cells[NY * GW + NX].Find == FindIdx;
			};
			auto Outline = [&D, &R, &SameFind, X, Y](const FLinearColor& Col)
			{
				if (!SameFind(X, Y - 1)) { D.Rect(FFVRect(R.X, R.Y, R.W, 1.f), Col); }
				if (!SameFind(X, Y + 1)) { D.Rect(FFVRect(R.X, R.Bottom() - 1.f, R.W, 1.f), Col); }
				if (!SameFind(X - 1, Y)) { D.Rect(FFVRect(R.X, R.Y, 1.f, R.H), Col); }
				if (!SameFind(X + 1, Y)) { D.Rect(FFVRect(R.Right() - 1.f, R.Y, 1.f, R.H), Col); }
			};
			if (F.Kind == EFind::Hardrock)
			{
				D.Rect(R, FV::Col(70, 80, 104));
				D.Rect(FFVRect(R.X, R.Y, Snap(Cs * 0.55f), Snap(Cs * 0.45f)), FV::Col(98, 112, 146));
				D.Rect(FFVRect(Snap(R.X + Cs * 0.4f), Snap(R.Y + Cs * 0.55f), Snap(Cs * 0.6f), Snap(Cs * 0.45f)), FV::Col(54, 62, 84));
				D.Rect(FFVRect(Snap(R.X + Cs * 0.2f), Snap(R.Y + Cs * 0.18f), Snap(Cs * 0.25f), 1.f), FV::Col(170, 184, 214));
				D.Rect(FFVRect(Snap(R.X + Cs * 0.62f), Snap(R.Y + Cs * 0.35f), 2.f, 2.f), FV::Col(230, 236, 255));
				Outline(FV::Col(30, 34, 48));
			}
			else if (F.Kind == EFind::Scrap)
			{
				D.Rect(R, FV::Col(132, 82, 52));
				D.Rect(R.Inset(Snap(Cs * 0.15f)), FV::Col(164, 122, 88));
				D.Rect(FFVRect(Snap(R.X + Cs * 0.3f), Snap(R.Y + Cs * 0.55f), Snap(Cs * 0.35f), Snap(Cs * 0.15f)), FV::Col(98, 56, 34));
				D.Rect(FFVRect(Snap(R.X + Cs * 0.22f), Snap(R.Y + Cs * 0.22f), 2.f, 2.f), FV::Col(196, 196, 204));
				D.Rect(FFVRect(Snap(R.Right() - Cs * 0.22f - 2.f), Snap(R.Y + Cs * 0.22f), 2.f, 2.f), FV::Col(196, 196, 204));
				Outline(FV::Col(62, 36, 22));
			}
			else
			{
				DrawBedrock(D, R, X, Y);
				const FVector2D C(R.X + R.W * 0.5f, R.Y + R.H * 0.5f);
				D.Circle(C + FVector2D(1.0, 1.5), Cs * 0.3f, FV::Col(36, 30, 28));
				D.Circle(C, Cs * 0.3f, FV::Col(150, 146, 138));
				D.Circle(C - FVector2D(double(Cs) * 0.09, double(Cs) * 0.09), Cs * 0.11f, FV::Col(194, 190, 182));
			}
		}

		void DrawLayerCell(FVDraw& D, const FFVRect& R, int32 L, int32 X, int32 Y, float Alpha) const
		{
			const FLinearColor Base = LayerColor(L);
			D.Rect(R, WithAlpha(Base, Alpha));
			D.Rect(FFVRect(R.X, R.Y, R.W, 1.f), WithAlpha(FMath::Lerp(Base, FLinearColor::White, 0.25f), Alpha));
			D.Rect(FFVRect(R.X, R.Y, 1.f, R.H), WithAlpha(FMath::Lerp(Base, FLinearColor::White, 0.15f), Alpha));
			D.Rect(FFVRect(R.X, R.Bottom() - 1.f, R.W, 1.f), WithAlpha(FMath::Lerp(Base, FLinearColor::Black, 0.35f), Alpha));
			D.Rect(FFVRect(R.Right() - 1.f, R.Y, 1.f, R.H), WithAlpha(FMath::Lerp(Base, FLinearColor::Black, 0.3f), Alpha));
			for (int32 k = 0; k < 2 + L; ++k)
			{
				const float HX = FVUI::Hash01(X * 13 + k, Y * 7 + L, FaceSeed + 5), HY = FVUI::Hash01(X * 5 + L, Y * 17 + k, FaceSeed + 9);
				const bool bDark = ((k + L) & 1) == 0;
				D.Rect(FFVRect(Snap(R.X + 2.f + HX * (R.W - 6.f)), Snap(R.Y + 2.f + HY * (R.H - 6.f)), 2.f, bDark ? 1.f : 2.f),
					WithAlpha(FMath::Lerp(Base, bDark ? FLinearColor::Black : FLinearColor::White, 0.2f), Alpha));
			}
			if (L >= 3)
			{
				const FLinearColor Cr = WithAlpha(FMath::Lerp(Base, FLinearColor::Black, 0.4f), Alpha);
				D.Line(FVector2D(R.X + R.W * 0.2f, R.Y + R.H * 0.7f), FVector2D(R.X + R.W * 0.5f, R.Y + R.H * 0.55f), Cr, 1.f);
				D.Line(FVector2D(R.X + R.W * 0.5f, R.Y + R.H * 0.55f), FVector2D(R.X + R.W * 0.65f, R.Y + R.H * 0.75f), Cr, 1.f);
			}
		}

		void DrawBoulder(FVDraw& D, const FFVRect& Grid, float Cs)
		{
			constexpr float Rim = 7.f;
			const FFVRect Outer = Grid.Inset(-Rim);
			D.Rect(Outer.Offset(2.f, 3.f), FV::Col(0, 0, 0, 80));
			D.Rect(Outer, FV::Col(58, 52, 50));
			D.Rect(Outer.Inset(1.f), FV::Col(98, 90, 84));
			D.Rect(FFVRect(Outer.X + 1.f, Outer.Y + 1.f, Outer.W - 2.f, 2.f), FV::Col(140, 130, 120));
			D.Rect(FFVRect(Outer.X + 1.f, Outer.Bottom() - 3.f, Outer.W - 2.f, 2.f), FV::Col(70, 64, 60));
			D.Rect(Grid.Inset(-1.f), FV::Col(40, 34, 32));

			// the face: ART-CAVE sprites when they are there (the 16x16 magenta placeholder = not yet), procedural otherwise
			UFVSprites* Sp = UFVSprites::Get();
			bool bArt = false;
			if (Sp)
			{
				const FFVSprite& Face = Sp->ExcavationFace(FaceSeed & 3);
				if (Face.IsValid() && Face.W > 16.f)
				{
					D.SpriteRect(Face, Grid);
					bArt = true;
				}
			}

			const bool bCrumble = Phase == EPhase::Crumble;
			const float CrT = bCrumble ? Clamp01(PhaseT / 0.7f) : 0.f;
			for (int32 i = 0; i < NC; ++i)
			{
				const int32 X = i % GW, Y = i / GW;
				const FFVRect CR(Grid.X + float(X) * Cs, Grid.Y + float(Y) * Cs, Cs, Cs);
				const FCell& C = Cells[i];
				if (!bArt) { DrawBedrock(D, CR, X, Y); }
				if (C.Find >= 0 && !Finds[C.Find].bRevealed) { DrawFindCell(D, CR, C.Find, X, Y); }
			}
			// revealed finds: the item itself, framed and glowing for a moment
			for (const FFind& F : Finds)
			{
				if (!F.bRevealed) { continue; }
				const FFVRect B(Grid.X + float(F.MinX) * Cs, Grid.Y + float(F.MinY) * Cs, float(F.MaxX - F.MinX + 1) * Cs, float(F.MaxY - F.MinY + 1) * Cs);
				const float BCX = B.X + B.W * 0.5f, BCY = B.Y + B.H * 0.5f;
				for (const int32 Idx : F.Cells)
				{
					const FFVRect CR(Grid.X + float(Idx % GW) * Cs, Grid.Y + float(Idx / GW) * Cs, Cs, Cs);
					D.Rect(CR, F.Kind == EFind::Hardrock ? FV::Col(84, 92, 116) : (F.Kind == EFind::Scrap ? FV::Col(112, 84, 64) : FV::Col(80, 72, 68)));
				}
				if (F.RevealT < 0.8f) { D.Glow(FVector2D(BCX, BCY), FMath::Max(B.W, B.H) * 0.8f, WithAlpha(FV::Col(255, 226, 130), 0.55f * (1.f - F.RevealT / 0.8f))); }
				D.Frame(B, F.Kind == EFind::Pebble ? FV::Col(170, 160, 150) : FV::Col(236, 190, 70), 1.f);
				if (const FFVSprite* IS = FVUI::ItemSprite(F.ItemId))
				{
					const float Pop = F.RevealT < 0.3f ? EaseOutBack(F.RevealT / 0.3f) : 1.f;
					const float BoxS = FMath::Min(B.W, B.H) * 0.9f * Pop;
					if (BoxS > 2.f) { FVUI::SpriteFit(D, *IS, FFVRect(BCX - BoxS * 0.5f, BCY - BoxS * 0.5f, BoxS, BoxS), FLinearColor::White, 4.f); }
				}
			}
			// rock layers
			for (int32 i = 0; i < NC; ++i)
			{
				const FCell& C = Cells[i];
				if (C.Layers <= 0) { continue; }
				const int32 X = i % GW, Y = i / GW;
				float OY = 0.f, OX = 0.f;
				if (C.HitT < 0.15f) { OX = Snap(FMath::Sin(C.HitT * 90.f) * 1.5f); }
				if (bCrumble) { OY = Snap(CrT * CrT * 24.f * (0.6f + 0.4f * FVUI::Hash01(X, Y, FaceSeed + 3))); }
				const FFVRect CR(Grid.X + float(X) * Cs + OX, Grid.Y + float(Y) * Cs + OY, Cs, Cs);
				const float Alpha = 1.f - CrT;
				if (Alpha <= 0.01f) { continue; }
				if (bArt && Sp)
				{
					const FFVSprite& CS = Sp->ExcavationCell(C.Layers, C.Variant);
					if (CS.IsValid()) { D.SpriteRect(CS, CR, FLinearColor(1.f, 1.f, 1.f, Alpha)); continue; }
				}
				DrawLayerCell(D, CR, C.Layers, X, Y, Alpha);
			}
			// cracks grow as the durability falls
			if (!bCrumble)
			{
				const float Dmg = MaxDurability > 0.f ? 1.f - Durability / MaxDurability : 0.f;
				const int32 NCr = FMath::Min(CrackLines.Num(), int32(Dmg * float(CrackLines.Num() + 1)));
				for (int32 c = 0; c < NCr; ++c)
				{
					const FCrack& Cr = CrackLines[c];
					for (int32 k = 0; k < 4; ++k)
					{
						const FVector2D A(Grid.X + Cr.Pts[k].X * Grid.W, Grid.Y + Cr.Pts[k].Y * Grid.H);
						const FVector2D B(Grid.X + Cr.Pts[k + 1].X * Grid.W, Grid.Y + Cr.Pts[k + 1].Y * Grid.H);
						D.Line(A, B, FV::Col(34, 28, 26, 190), 1.5f);
					}
				}
			}
			// honest hint: valuable finds glint through the rock
			if (!bCrumble)
			{
				for (int32 i = 0; i < NC; ++i)
				{
					const FCell& C = Cells[i];
					if (C.Layers <= 0 || C.Find < 0) { continue; }
					const FFind& F = Finds[C.Find];
					if (F.Kind == EFind::Pebble || F.bRevealed) { continue; }
					const int32 X = i % GW, Y = i / GW;
					const float Ph = Time * 1.7f + FVUI::Hash01(X, Y, FaceSeed + 7) * 6.2832f;
					const float Sv = FMath::Sin(Ph);
					if (Sv < 0.72f) { continue; }
					const float Tw = (Sv - 0.72f) / 0.28f;
					const float Depth = C.Layers >= 3 ? 0.6f : (C.Layers == 2 ? 0.8f : 1.f);
					const float PX = Grid.X + (float(X) + 0.3f + 0.4f * FVUI::Hash01(X, Y, FaceSeed + 11)) * Cs;
					const float PY = Grid.Y + (float(Y) + 0.3f + 0.4f * FVUI::Hash01(Y, X, FaceSeed + 13)) * Cs;
					const FLinearColor Col = F.Kind == EFind::Hardrock ? FV::Col(255, 244, 200) : FV::Col(200, 222, 255);
					const float L = FMath::Max(1.f, Snap(Cs * 0.2f * Tw * Depth));
					D.Glow(FVector2D(PX, PY), L * 2.5f, WithAlpha(Col * 0.5f, 0.6f * Tw * Depth));
					D.Rect(FFVRect(Snap(PX - L), Snap(PY), L * 2.f + 1.f, 1.f), WithAlpha(Col, Tw * Depth));
					D.Rect(FFVRect(Snap(PX), Snap(PY - L), 1.f, L * 2.f + 1.f), WithAlpha(Col, Tw * Depth));
				}
			}
			// the tool's target (mouse hover / keyboard cursor)
			if (Phase == EPhase::Play)
			{
				int32 HX = -1, HY = -1;
				const FVector2D M = D.Mouse();
				if (bKeyCursor) { HX = Cursor.X; HY = Cursor.Y; }
				else if (!FV::IsTouchUI() && bInteractive && Grid.Contains(M))
				{
					HX = FMath::Clamp(int32((float(M.X) - Grid.X) / Cs), 0, GW - 1);
					HY = FMath::Clamp(int32((float(M.Y) - Grid.Y) / Cs), 0, GH - 1);
				}
				if (HX >= 0)
				{
					static const int32 TDX[5] = { 0, 1, -1, 0, 0 };
					static const int32 TDY[5] = { 0, 0, 0, 1, -1 };
					const int32 NT = Tool == ETool::Pickaxe ? 5 : 1;
					for (int32 k = 0; k < NT; ++k)
					{
						const int32 X = HX + TDX[k], Y = HY + TDY[k];
						if (X < 0 || X >= GW || Y < 0 || Y >= GH) { continue; }
						const FFVRect CR(Grid.X + float(X) * Cs, Grid.Y + float(Y) * Cs, Cs, Cs);
						D.Rect(CR, FV::Col(255, 255, 255, k == 0 ? 60 : 36));
						D.Frame(CR, FV::Col(255, 250, 220, 170), 1.f);
					}
				}
			}
			// the tool swings at the cell it hit
			if (HitCell >= 0 && HitAge < 0.25f)
			{
				if (const FFVSprite* TI = FVUI::Icon(HitTool == ETool::Pickaxe ? EFVIcon::Pickaxe : EFVIcon::Hammer))
				{
					const float T = HitAge / 0.25f;
					const float Rot = T < 0.3f ? FMath::Lerp(-50.f, 15.f, T / 0.3f) : FMath::Lerp(15.f, -20.f, (T - 0.3f) / 0.7f);
					const FVector2D P(Grid.X + (float(HitCell % GW) + 0.85f) * Cs, Grid.Y + (float(HitCell / GW) + 0.15f) * Cs);
					D.SpriteCentered(*TI, P, FMath::Max(1.f, Cs / 16.f), FLinearColor(1.f, 1.f, 1.f, 1.f - T * 0.5f), false, Rot);
				}
			}
		}

		void DrawSide(FVDraw& D, const FFVRect& Side)
		{
			const bool bP = FV::IsPortrait();
			const float LS = bP ? FV::Ui::Small() : 7.f;
			const float SlotS = bP ? 26.f : 22.f;
			float Y = Side.Y;
			int32 Shown = 0;
			for (const FFind& F : Finds) { if (F.bRevealed) { ++Shown; } }
			const bool bCanTake = bInteractive && Phase == EPhase::Play && Shown > 0;

			float ChipsRight = Side.Right();
			if (bP)
			{
				const float BtnH = FV::Ui::ButtonH();
				const float BW = 96.f;
				const FFVRect Btn(Side.Right() - BW, Side.Y + Snap((Side.H - BtnH) * 0.5f), BW, BtnH);
				if (D.Button(Btn, bRubble ? FString(TEXT("Дальше")) : FString(TEXT("Забрать")), EFVButton::Primary, bCanTake, FV::Ui::Body() + 1.f)) { EndBoulder(); }
				ChipsRight = Btn.X - 8.f;
			}
			D.Text(FString::Printf(TEXT("Находки: %d из %d"), Shown, Finds.Num()), FVector2D(Side.X, Y), LS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(LS, true) + 2.f;
			float X = Side.X;
			const float Step = SlotS + 3.f;
			for (const FFind& F : Finds)
			{
				if (X + SlotS > ChipsRight)
				{
					if (bP) { break; }
					X = Side.X;
					Y += Step;
				}
				const FFVRect Box(X, Y, SlotS, SlotS);
				if (F.bRevealed)
				{
					D.Rect(Box, FV::Col(236, 214, 164));
					D.Frame(Box, FV::Col(172, 108, 14), 1.f);
					if (const FFVSprite* IS = FVUI::ItemSprite(F.ItemId)) { FVUI::SpriteFit(D, *IS, Box.Inset(2.f), FLinearColor::White, 2.f); }
					if (F.Count > 1)
					{
						D.TextOutlined(FString::Printf(TEXT("%d"), F.Count), FVector2D(Box.Right() - 2.f, Box.Bottom() - D.LineHeight(6.5f) + 1.f), 6.5f, FVPal::White(), EFVAlign::Right);
					}
				}
				else
				{
					D.Rect(Box, FV::Col(92, 82, 76));
					D.Frame(Box, FV::Col(58, 50, 46), 1.f);
					D.Text(TEXT("?"), FVector2D(Box.X + Box.W * 0.5f, Box.Y + Snap((SlotS - D.LineHeight(LS + 2.f)) * 0.5f)), LS + 2.f, FV::Col(200, 190, 176), EFVAlign::Center, false, true);
				}
				X += Step;
			}
			if (bP) { return; }

			// landscape: the tools' legend and «Забрать»
			Y += SlotS + 8.f;
			D.Divider(FVector2D(Side.X, Y), Side.W);
			Y += 6.f;
			FString PickText = TEXT("Кирка — крест из 5 клеток, сильно трясёт валун.");
			if (PickMult < 0.99f) { PickText += TEXT(" Закалённая трясёт меньше."); }
			Y += D.TextWrapped(PickText, FVector2D(Side.X, Y), Side.W, 6.5f, Tool == ETool::Pickaxe ? FVPal::Ink() : FVPal::InkSoft()) + 3.f;
			Y += D.TextWrapped(TEXT("Молоток — одна клетка, трясёт слабо."), FVector2D(Side.X, Y), Side.W, 6.5f, Tool == ETool::Hammer ? FVPal::Ink() : FVPal::InkSoft()) + 3.f;
			D.TextWrapped(TEXT("Достаётся только то, что откопано целиком."), FVector2D(Side.X, Y), Side.W, 6.5f, FVPal::InkSoft());
			const FFVRect Btn(Side.X, Side.Bottom() - 22.f, Side.W, 22.f);
			if (D.Button(Btn, bRubble ? FString(TEXT("Разбить дальше")) : FString(TEXT("Забрать находки")), EFVButton::Primary, bCanTake, 8.f)) { EndBoulder(); }
		}
	};
}

TUniquePtr<FVScreen> FVMakeForgingScreen(UFVGame* Game)
{
	return MakeUnique<FVMiniGameStoneImpl::FForgeScreen>(Game);
}

TUniquePtr<FVScreen> FVMakeExcavationScreen(UFVGame* Game)
{
	return MakeUnique<FVMiniGameStoneImpl::FExcavationScreen>(Game);
}
