// FVMiniGames.cpp — Update 2 mini-games (MiniGames module, Docs/UPDATE_HOME.md §11):
//   Cooking   (context = recipe id)            cut the fish with swipes / a sliding knife, then cook in the green zone;
//   LureCraft (no context)                     combine base + colour + glitter into a lure, discover the 24 recipes
//                                              (update 4: the panel is FFVLureWorkshop, FVMiniGames.h — also the workshop's «Блёсны» tab);
//   Knots     (context = recipe id)            "repeat the pattern" of arrows to tie lines and nets;
//   Digging   (context = "<location>:<spot>")  dig a soil grid against the clock, grab the worms before they hide.
// Every screen: game logic + keyboard + the autotest bot in Tick, IMGUI drawing + pointer (mouse or one touch finger,
// In.Wheel is ignored; update 3: widget clicks fire on release, so timed inputs use PressedIn / InstantButton) in Draw, both orientations, juice (sfx, particles, shake, pop-up words), a result card inside
// the screen and finally UFVGame::ReturnFromMiniGame. Esc / «Назад» asks «Бросить?» and returns without spending.

#include "FVMiniGames.h"
#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVScene.h"
#include "FVUIKit.h"
#include "HAL/PlatformTime.h"

namespace FVMiniGamesImpl
{
	// =====================================================================================================
	// small helpers
	// =====================================================================================================
	FORCEINLINE float Snap(double V) { return FMath::RoundToFloat(float(V)); }
	FORCEINLINE FLinearColor WithAlpha(FLinearColor C, float A) { C.A = FMath::Clamp(A, 0.f, 1.f); return C; }
	FORCEINLINE float Clamp01(float V) { return FMath::Clamp(V, 0.f, 1.f); }
	FORCEINLINE float Smooth01(float T) { T = Clamp01(T); return T * T * (3.f - 2.f * T); }
	inline float EaseOutBack(float T)
	{
		const float U = Clamp01(T) - 1.f;
		constexpr float C1 = 1.70158f;
		constexpr float C3 = C1 + 1.f;
		return 1.f + C3 * U * U * U + C1 * U * U;
	}
	/** Triangle wave 0 -> 1 -> 0 over one period of T. */
	inline float Tri01(float T)
	{
		const float F = T - FMath::FloorToFloat(T);
		return F < 0.5f ? F * 2.f : 2.f - F * 2.f;
	}
	inline float Hash01(int32 A, int32 B) { return FVUI::Hash01(A, B, 7331); }

	/** red (0) -> yellow (0.5) -> green (1), the fishing meters' colours. */
	inline FLinearColor MeterColor(float P)
	{
		P = Clamp01(P);
		const FLinearColor Red = FV::Col(222, 60, 44), Yellow = FV::Col(246, 206, 58), Green = FV::Col(96, 204, 64);
		return P < 0.5f ? FMath::Lerp(Red, Yellow, P * 2.f) : FMath::Lerp(Yellow, Green, (P - 0.5f) * 2.f);
	}

	// readable inks on parchment
	FORCEINLINE FLinearColor InkGood() { return FV::Col(52, 122, 36); }
	FORCEINLINE FLinearColor InkBad() { return FV::Col(178, 46, 32); }
	FORCEINLINE FLinearColor InkGold() { return FV::Col(172, 108, 14); }
	// rope (knots)
	FORCEINLINE FLinearColor RopeMain() { return FV::Col(210, 176, 114); }
	FORCEINLINE FLinearColor RopeLight() { return FV::Col(238, 212, 160); }
	FORCEINLINE FLinearColor RopeDark() { return FV::Col(148, 108, 60); }

	inline void Sfx(EFVSfx S, float Volume = 1.f, float Pitch = 1.f) { FVUI::Sfx(S, Volume, Pitch); }

	inline const FFVSprite* ItemIconPtr(FName Id)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp || Id.IsNone()) { return nullptr; }
		const FFVSprite& S = Sp->ItemIcon(Id);
		return S.IsValid() ? &S : nullptr;
	}

	inline bool ConfirmPressed(const FFVInput& In)
	{
		return In.WasPressed(EKeys::SpaceBar) || In.WasPressed(EKeys::Enter) || In.WasPressed(EKeys::Gamepad_FaceButton_Bottom);
	}

	/** Dish / craft score 0..1 -> quality (spec §11: Normal < .45, Silver < .75, Gold < .93, Iridium otherwise). */
	inline EFVQuality ScoreToQuality(float S)
	{
		if (S < 0.45f) { return EFVQuality::Normal; }
		if (S < 0.75f) { return EFVQuality::Silver; }
		if (S < 0.93f) { return EFVQuality::Gold; }
		return EFVQuality::Iridium;
	}

	inline FString PercentText(float S) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Clamp01(S) * 100.f)); }

	enum EDir : int32 { DirUp = 0, DirRight = 1, DirDown = 2, DirLeft = 3 };

	/** Direction presses from arrows / WASD (key edges) and from the d-pad / stick (MoveX/MoveY edges). A keyboard
	    press also moves MoveX/MoveY, so the stick path is latched until the axes return to neutral. */
	struct FDirInput
	{
		bool bLatched = false;

		int32 Poll(const FFVInput& In)
		{
			int32 Dir = -1;
			if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { Dir = DirUp; }
			else if (In.WasPressed(EKeys::Right) || In.WasPressed(EKeys::D)) { Dir = DirRight; }
			else if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { Dir = DirDown; }
			else if (In.WasPressed(EKeys::Left) || In.WasPressed(EKeys::A)) { Dir = DirLeft; }
			const float AX = FMath::Abs(In.MoveX), AY = FMath::Abs(In.MoveY);
			const float Mag = FMath::Max(AX, AY);
			if (Dir >= 0) { bLatched = true; return Dir; }
			if (Mag < 0.35f) { bLatched = false; return -1; }
			if (!bLatched && Mag > 0.6f)
			{
				bLatched = true;
				return AX > AY ? (In.MoveX > 0.f ? DirRight : DirLeft) : (In.MoveY > 0.f ? DirDown : DirUp);
			}
			return -1;
		}
	};

	/** Update 3: FVDraw buttons fire on RELEASE. Timed mini-game actions («Резать», «Перевернуть»…) must react the moment
	    the finger / button goes down, so this draws the normal button but fires on the press and swallows the release
	    click (the action never fires twice). */
	inline bool InstantButton(FVDraw& D, const FFVRect& R, const FString& Label, EFVButton Style, bool bEnabled, float TextSize)
	{
		const bool bPress = bEnabled && D.PressedIn(R);
		if (bEnabled) { D.Clicked(R); }
		D.Button(R, Label, Style, bEnabled, TextSize);
		if (bPress) { Sfx(EFVSfx::Click, 0.8f); }
		return bPress;
	}

	// =====================================================================================================
	// drawing primitives (visual language of the fishing mini-game: wooden frames, dark tracks, meters)
	// =====================================================================================================
	/** Filled pixel ellipse made of 1 px rows. Half: 0 = full, 1 = top half, 2 = bottom half. */
	void FillEllipse(FVDraw& D, const FVector2D& C, float RX, float RY, const FLinearColor& Col, int32 Half = 0)
	{
		if (RX < 0.5f || RY < 0.5f) { return; }
		const float CX = float(C.X);
		const float CY = Snap(C.Y);
		const int32 N = FMath::CeilToInt(RY);
		for (int32 i = -N; i < N; ++i)
		{
			if ((Half == 1 && i >= 0) || (Half == 2 && i < 0)) { continue; }
			const float Yn = (float(i) + 0.5f) / RY;
			if (Yn <= -1.f || Yn >= 1.f) { continue; }
			const float Hw = RX * FMath::Sqrt(1.f - Yn * Yn);
			const float XA = Snap(CX - Hw), XB = Snap(CX + Hw);
			if (XB > XA) { D.Rect(FFVRect(XA, CY + float(i), XB - XA, 1.f), Col); }
		}
	}

	/** Wooden frame recipe of the fishing panels: drop shadow, dark rim, light bevel, nails. */
	void WoodFrame(FVDraw& D, const FFVRect& R, float K = 1.f)
	{
		D.Rect(R.Offset(2.f, 3.f), FV::Col(0, 0, 0, 90));
		D.Rect(R, FVPal::WoodDark());
		D.Rect(R.Inset(1.f), FVPal::Wood());
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 2.f), FVPal::WoodLight());
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 2.f, R.H - 2.f), FVPal::WoodLight());
		D.Rect(FFVRect(R.X + 1.f, R.Bottom() - 3.f, R.W - 2.f, 2.f), FMath::Lerp(FVPal::WoodDark(), FVPal::Wood(), 0.4f));
		if (R.W >= 18.f && R.H >= 18.f)
		{
			const float NailS = Snap(FMath::Max(2.f, 2.f * K));
			const float In = Snap(3.f * K + 1.f);
			const FVector2D Nails[4] = { FVector2D(R.X + In, R.Y + In), FVector2D(R.Right() - In - NailS, R.Y + In),
				FVector2D(R.X + In, R.Bottom() - In - NailS - 1.f), FVector2D(R.Right() - In - NailS, R.Bottom() - In - NailS - 1.f) };
			for (const FVector2D& N : Nails)
			{
				D.Rect(FFVRect(Snap(N.X), Snap(N.Y), NailS, NailS), FV::Col(120, 110, 100));
				D.Rect(FFVRect(Snap(N.X), Snap(N.Y), 1.f, 1.f), FV::Col(176, 168, 156));
			}
		}
	}

	/** Dark inner track with a WoodDark border (meters). Returns the track rect. */
	FFVRect InnerTrack(FVDraw& D, const FFVRect& R, float Inset, const FLinearColor& Col)
	{
		const FFVRect T = R.Inset(Inset);
		D.Rect(T.Inset(-1.f), FVPal::WoodDark());
		D.Rect(T, Col);
		return T;
	}

	/** Raised wooden tile (arrow cards, d-pad buttons). */
	void WoodTile(FVDraw& D, const FFVRect& R, const FLinearColor& Face, bool bPressed)
	{
		const float Down = bPressed ? 2.f : 0.f;
		D.Rect(FFVRect(R.X, R.Y + 3.f, R.W, R.H), FV::Col(60, 30, 14, 200));
		const FFVRect F = R.Offset(0.f, Down);
		D.Rect(F, FVPal::WoodDark());
		D.Rect(F.Inset(1.f), Face);
		D.Rect(FFVRect(F.X + 2.f, F.Y + 1.f, F.W - 4.f, 1.f), FMath::Lerp(Face, FLinearColor::White, 0.35f));
		D.Rect(FFVRect(F.X + 1.f, F.Bottom() - 2.f, F.W - 2.f, 1.f), FMath::Lerp(Face, FLinearColor::Black, 0.25f));
	}

	/** Chunky pixel arrow (7x7 grid) pointing Dir. */
	void DrawArrow(FVDraw& D, const FVector2D& C, float Size, int32 Dir, const FLinearColor& Col)
	{
		const float U = FMath::Max(1.f, FMath::FloorToFloat(Size / 7.f));
		const float X0 = Snap(C.X - U * 3.5f), Y0 = Snap(C.Y - U * 3.5f);
		auto Row = [&D, &Col, U, X0, Y0, Dir](int32 GX0, int32 GX1, int32 GY)
		{
			const float Len = float(GX1 - GX0 + 1) * U;
			switch (Dir)
			{
			case DirUp:   D.Rect(FFVRect(X0 + float(GX0) * U, Y0 + float(GY) * U, Len, U), Col); break;
			case DirDown: D.Rect(FFVRect(X0 + float(GX0) * U, Y0 + float(6 - GY) * U, Len, U), Col); break;
			case DirLeft: D.Rect(FFVRect(X0 + float(GY) * U, Y0 + float(GX0) * U, U, Len), Col); break;
			default:      D.Rect(FFVRect(X0 + float(6 - GY) * U, Y0 + float(GX0) * U, U, Len), Col); break;
			}
		};
		for (int32 Rw = 0; Rw < 4; ++Rw) { Row(3 - Rw, 3 + Rw, Rw); }
		for (int32 Rw = 4; Rw < 7; ++Rw) { Row(2, 4, Rw); }
	}

	void DrawArrowShadowed(FVDraw& D, const FVector2D& C, float Size, int32 Dir, const FLinearColor& Col)
	{
		const float U = FMath::Max(1.f, FMath::FloorToFloat(Size / 7.f));
		DrawArrow(D, C + FVector2D(0.f, U), Size, Dir, FV::Col(60, 30, 14, 170));
		DrawArrow(D, C, Size, Dir, Col);
	}

	/** Vertical dashed line (animated by Offset). */
	void DashedV(FVDraw& D, float X, float Y0, float Y1, const FLinearColor& Col, float Offset, float Thick)
	{
		constexpr float Dash = 4.f, Gap = 3.f, Period = Dash + Gap;
		for (float Y = Y0 - FMath::Fmod(FMath::Max(0.f, Offset), Period); Y < Y1; Y += Period)
		{
			const float A = Snap(FMath::Max(Y, Y0)), B = Snap(FMath::Min(Y + Dash, Y1));
			if (B > A) { D.Rect(FFVRect(Snap(X - Thick * 0.5f), A, Thick, B - A), Col); }
		}
	}

	/** Flickering flame tongues on BaseY (stove burner, oven). */
	void DrawFlames(FVDraw& D, float Time, float CX, float BaseY, float HalfW, float Scale, float Intensity)
	{
		D.Glow(FVector2D(CX, BaseY - 6.f * Scale), HalfW * 1.5f, WithAlpha(FV::Col(255, 140, 50) * (0.35f + 0.2f * Intensity), 0.6f));
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

	/** Draws a sprite cut into vertical slices at the given fractions (pieces separated by Gap). */
	void DrawSpriteSlices(FVDraw& D, const FFVSprite& S, const FVector2D& Center, float Scale, const TArray<float>& Cuts, float Gap,
		const FLinearColor& Tint, bool bFlip, float RotDeg)
	{
		if (!S.IsValid()) { return; }
		const float W = S.W * Scale;
		const int32 NumSeg = Cuts.Num() + 1;
		float F0 = 0.f;
		for (int32 k = 0; k < NumSeg; ++k)
		{
			const float F1 = k < Cuts.Num() ? FMath::Clamp(Cuts[k], F0, 1.f) : 1.f;
			if (F1 > F0)
			{
				FFVSprite Part = S;
				Part.UV0.X = FMath::Lerp(S.UV0.X, S.UV1.X, double(F0));
				Part.UV1.X = FMath::Lerp(S.UV0.X, S.UV1.X, double(F1));
				Part.W = S.W * (F1 - F0);
				float PX = float(Center.X) - W * 0.5f + W * (F0 + F1) * 0.5f + (float(k) - float(NumSeg - 1) * 0.5f) * Gap;
				if (bFlip) { PX = 2.f * float(Center.X) - PX; }
				D.SpriteCentered(Part, FVector2D(PX, Center.Y), Scale, Tint, bFlip, RotDeg);
			}
			F0 = F1;
		}
	}

	/** Colour multiplier of food by doneness: raw -> golden -> brown -> burnt. */
	inline FLinearColor CookTint(float Dn)
	{
		const FLinearColor Raw = FLinearColor::White, Golden = FV::Col(255, 212, 150), Brown = FV::Col(206, 142, 92), Burnt = FV::Col(84, 58, 50);
		if (Dn < 0.5f) { return FMath::Lerp(Raw, Golden, Dn / 0.5f); }
		if (Dn < 0.85f) { return FMath::Lerp(Golden, Brown, (Dn - 0.5f) / 0.35f); }
		return FMath::Lerp(Brown, Burnt, Clamp01((Dn - 0.85f) / 0.15f));
	}

	void DrawKnot(FVDraw& D, const FVector2D& C, float R)
	{
		D.Circle(C + FVector2D(1.f, 1.5f), R, FV::Col(60, 34, 16, 120));
		D.Circle(C, R, RopeDark());
		D.Circle(C, R - 1.f, RopeMain());
		D.Line(C + FVector2D(-R * 0.65f, -R * 0.45f), C + FVector2D(R * 0.65f, R * 0.45f), RopeDark(), 1.5f);
		D.Line(C + FVector2D(-R * 0.65f, R * 0.45f), C + FVector2D(R * 0.1f, -R * 0.05f), RopeDark(), 1.5f);
		D.Rect(FFVRect(Snap(C.X - R * 0.45f), Snap(C.Y - R * 0.65f), 2.f, 1.f), RopeLight());
	}

	// =====================================================================================================
	// juice: particles, pop-up words, screen shake
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
		uint8 Kind = 0;        // 0 chunk, 1 sparkle, 2 puff
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

		void Sparkles(const FVector2D& Pos, int32 Num, float Radius = 12.f, const FLinearColor& Col = FV::Col(255, 238, 150))
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

		void Puff(const FVector2D& Pos, const FLinearColor& Col, float Size, float Rise)
		{
			if (Parts.Num() >= MaxParts) { return; }
			FParticle& Pt = Parts.AddDefaulted_GetRef();
			Pt.P = Pos;
			Pt.V = FVector2D(Rng.FRandRange(-7.f, 7.f), -Rise * Rng.FRandRange(0.7f, 1.3f));
			Pt.Life = Rng.FRandRange(0.8f, 1.2f);
			Pt.Size = Size * Rng.FRandRange(0.8f, 1.2f);
			Pt.C = Col;
			Pt.Kind = 2;
		}

		void Text(const FString& S, const FVector2D& Pos, const FLinearColor& Col, float Size = 10.f)
		{
			if (Floaters.Num() >= 10) { Floaters.RemoveAt(0); }
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
				if (Pt.Kind != 0) { Pt.V *= FMath::Max(0.f, 1.f - Dt * 1.5f); }
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
					D.Circle(Pt.P, Pt.Size * (0.7f + 0.9f * T), WithAlpha(Pt.C, Pt.C.A * A));
				}
			}
			for (const FFloater& F : Floaters)
			{
				const float T = Clamp01(F.Age / F.Life);
				const float Pop = T < 0.12f ? 1.f + (0.12f - T) * 3.f : 1.f;
				const float A = T > 0.6f ? (1.f - T) / 0.4f : 1.f;
				const FVector2D P(F.P.X, F.P.Y - 20.f * Smooth01(T));
				D.TextOutlined(F.Text, FVector2D(Snap(P.X), Snap(P.Y)), F.Size * Pop, WithAlpha(F.C, A), EFVAlign::Center, FV::Col(40, 22, 12, uint8(255.f * Clamp01(A))));
			}
		}

	private:
		static constexpr int32 MaxParts = 360;
		TArray<FParticle> Parts;
		TArray<FFloater> Floaters;
	};

	// =====================================================================================================
	// result card
	// =====================================================================================================
	struct FCardLine
	{
		FString Text;
		FLinearColor Color = FVPal::Ink();
		bool bBold = false;
	};

	struct FCardItem
	{
		FName Id;
		int32 Count = 0;
	};

	struct FResultCard
	{
		bool bShown = false;
		float Age = 0.f;
		bool bGood = true;
		FString Badge;                       // gold ribbon ("Новый рецепт!")
		FString Title;
		FString Sub;
		FName IconId;                        // ItemIcon
		bool bLureIcon = false;              // LureIcon(LureB, LureC, LureG) instead
		uint8 LureB = 0, LureC = 0, LureG = 0;
		EFVIcon Icon = EFVIcon::Count;       // fallback UI icon
		bool bStars = false;
		EFVQuality Quality = EFVQuality::Normal;
		TArray<FCardLine> Lines;
		TArray<FCardItem> Items;
		FString Primary = TEXT("Готово");
		FString Secondary;

		void AddLine(const FString& T, const FLinearColor& C = FVPal::Ink(), bool bInBold = false)
		{
			FCardLine& L = Lines.AddDefaulted_GetRef();
			L.Text = T;
			L.Color = C;
			L.bBold = bInBold;
		}
	};

	/** The card's big icon: a lure / an item / a UI icon (nullptr = none). */
	inline const FFVSprite* ResultCardIcon(const FResultCard& Card)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp) { return nullptr; }
		if (Card.bLureIcon)
		{
			const FFVSprite& S = Sp->LureIcon(Card.LureB, Card.LureC, Card.LureG);
			return S.IsValid() ? &S : nullptr;
		}
		if (!Card.IconId.IsNone()) { return ItemIconPtr(Card.IconId); }
		if (Card.Icon != EFVIcon::Count) { return FVUI::Icon(Card.Icon); }
		return nullptr;
	}

	/** Result card centered in Area (Bounds = where it must fit; nullptr = the whole canvas). Returns the clicked button
	    (0 primary, 1 secondary) or -1. OutIconCenter = the icon centre in untranslated coordinates (for the sparkles). */
	int32 DrawResultCard(FVDraw& D, const FFVRect& Area, const FResultCard& Card, float Time, FFVScrollView& CardScroll, FVector2D& OutIconCenter, const FFVRect* Bounds = nullptr)
	{
		const bool bP = FV::IsPortrait();
		const float Appear = Clamp01(Card.Age / 0.28f);
		D.Rect(FFVRect(Area.X - 3.f, Area.Y - 3.f, Area.W + 6.f, Area.H + 6.f), FV::Col(40, 22, 12, uint8(120.f * Appear)));

		const float W = bP ? FMath::Min(Area.W - 8.f, 304.f) : 300.f;
		const float Pad = bP ? 14.f : 10.f;
		const float InnerW = W - Pad * 2.f;
		const float TitleS = FV::Ui::Header(), BodyS = FV::Ui::Body(), SmallS = FV::Ui::Small();
		const float IconBox = bP ? 64.f : 48.f;
		const float BtnH = FV::Ui::ButtonH();
		const float BadgeH = Card.Badge.IsEmpty() ? 0.f : (bP ? 22.f : 17.f);
		const float StarsH = Card.bStars ? (bP ? 16.f : 12.f) : 0.f;
		const float ItemBox = bP ? 24.f : 16.f;
		const float ItemW = bP ? 64.f : 52.f;
		const int32 PerRow = FMath::Max(1, int32(InnerW / ItemW));
		const int32 ItemRows = (Card.Items.Num() + PerRow - 1) / PerRow;

		float H = Pad + BadgeH + IconBox + 6.f + D.LineHeight(TitleS) + 2.f + StarsH;
		if (!Card.Sub.IsEmpty()) { H += D.MeasureWrapped(Card.Sub, InnerW, SmallS, false) + 4.f; }
		for (const FCardLine& L : Card.Lines) { H += D.MeasureWrapped(L.Text, InnerW, BodyS, L.bBold) + 2.f; }
		if (ItemRows > 0) { H += float(ItemRows) * (ItemBox + 6.f) + 4.f; }
		H = Snap(H + 8.f + BtnH + Pad);
		// Too tall for the screen (many lines / items on a small canvas): the body scrolls, the buttons stay put.
		const float FullH = H;
		const float MaxH = Bounds ? Snap(FMath::Max(80.f, Bounds->H - 8.f)) : Snap(FMath::Max(140.f, FV::VH - 12.f));
		const bool bScroll = FullH > MaxH;
		if (bScroll) { H = MaxH; }

		const float Slide = (1.f - EaseOutBack(Appear)) * 36.f;
		const float MinY = Bounds ? Bounds->Y + 4.f : 4.f;
		const float MaxY = Bounds ? Bounds->Bottom() - H - 4.f : FV::VH - H - 4.f;
		const float Y0 = FMath::Clamp(float(Area.Center().Y) - H * 0.5f + Slide, MinY, FMath::Max(MinY, MaxY));
		const FFVRect R(Snap(Area.Center().X - W * 0.5f), Snap(Y0), W, H);
		D.Panel(R, EFVPanel::Wood);
		D.MarkUI(R);
		const float CX = float(R.Center().X);
		const FFVRect BodyR(R.X + 5.f, R.Y + 5.f, R.W - 10.f, FMath::Max(20.f, H - Pad - BtnH - 13.f));
		if (bScroll)
		{
			CardScroll.Update(D, BodyR, FullH - Pad - BtnH - 8.f);
			D.PushClip(BodyR);
			D.PushTranslate(FVector2D(0.f, -CardScroll.Offset()));
		}
		float Y = R.Y + Pad;

		if (!Card.Badge.IsEmpty())
		{
			const float BS = SmallS + 1.f;
			const float TW = D.MeasureText(Card.Badge, BS).X + 22.f;
			const FFVRect BR(Snap(CX - TW * 0.5f), Y - 3.f, Snap(TW), BadgeH - 4.f);
			D.Rect(BR.Offset(1.f, 2.f), FV::Col(60, 30, 14, 120));
			D.Rect(BR, FV::Col(196, 120, 20));
			D.Rect(BR.Inset(1.f), WithAlpha(FMath::Lerp(FVPal::Gold(), FLinearColor::White, 0.15f + 0.15f * FMath::Sin(Time * 6.f)), 1.f));
			D.Text(Card.Badge, FVector2D(CX, Snap(BR.Y + (BR.H - D.LineHeight(BS)) * 0.5f)), BS, FVPal::WoodDark(), EFVAlign::Center, false, true);
			Y += BadgeH;
		}

		const FVector2D IC(CX, Y + IconBox * 0.5f);
		OutIconCenter = IC + D.GetTranslate();   // the overlay sparkles are drawn untranslated
		if (Card.bGood)
		{
			for (int32 k = 0; k < 12; ++k)
			{
				const float Ang = Time * 0.7f + float(k) * PI / 6.f;
				const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
				D.Line(IC + Dir * (IconBox * 0.36f), IC + Dir * (IconBox * 0.64f), WithAlpha(FVPal::Gold(), 0.4f * Appear), bP ? 3.f : 2.f);
			}
			D.Glow(IC, IconBox * 0.6f, WithAlpha(FV::Col(255, 220, 120) * 0.45f, 0.6f * Appear));
		}
		const FFVRect IB(Snap(CX - IconBox * 0.5f), Snap(Y), IconBox, IconBox);
		D.Rect(IB, FVPal::WoodDark());
		D.Rect(IB.Inset(1.f), FVPal::ParchDark());
		D.Rect(IB.Inset(2.f), FV::Col(255, 236, 196));
		if (const FFVSprite* IconS = ResultCardIcon(Card)) { FVUI::SpriteFit(D, *IconS, IB.Inset(bP ? 7.f : 5.f), FLinearColor::White, 6.f); }
		Y += IconBox + 6.f;

		D.Text(D.Ellipsize(Card.Title, InnerW, TitleS), FVector2D(CX, Y), TitleS, Card.bGood ? FVPal::Ink() : InkBad(), EFVAlign::Center, false, true);
		Y += D.LineHeight(TitleS) + 2.f;

		if (Card.bStars)
		{
			const FString QN = FVDB::QualityName(Card.Quality);
			const float StarSc = bP ? 1.5f : 1.f;
			const float SW = Card.Quality == EFVQuality::Normal ? 0.f : 8.f * StarSc + 3.f;
			const float TW = D.MeasureText(QN, SmallS + 0.5f).X;
			const float X0 = CX - (SW + TW) * 0.5f;
			if (SW > 0.f) { D.Stars(FVector2D(Snap(X0), Snap(Y + 1.f)), Card.Quality, StarSc); }
			D.Text(QN, FVector2D(Snap(X0 + SW), Y), SmallS + 0.5f, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += StarsH;
		}

		auto CenteredText = [&D, InnerW, CX, &R, Pad](const FString& S, float& InOutY, float Size, const FLinearColor& Col, bool bBold)
		{
			if (D.MeasureText(S, Size, bBold).X <= InnerW)
			{
				D.Text(S, FVector2D(CX, InOutY), Size, Col, EFVAlign::Center, false, bBold);
				InOutY += D.MeasureWrapped(S, InnerW, Size, bBold);
			}
			else
			{
				InOutY += D.TextWrapped(S, FVector2D(R.X + Pad, InOutY), InnerW, Size, Col, bBold);
			}
		};
		if (!Card.Sub.IsEmpty())
		{
			CenteredText(Card.Sub, Y, SmallS, FVPal::InkSoft(), false);
			Y += 4.f;
		}
		for (const FCardLine& L : Card.Lines)
		{
			CenteredText(L.Text, Y, BodyS, L.Color, L.bBold);
			Y += 2.f;
		}

		if (ItemRows > 0)
		{
			Y += 2.f;
			for (int32 Rw = 0; Rw < ItemRows; ++Rw)
			{
				const int32 First = Rw * PerRow;
				const int32 N = FMath::Min(PerRow, Card.Items.Num() - First);
				const float RowX = CX - float(N) * ItemW * 0.5f;
				for (int32 k = 0; k < N; ++k)
				{
					const FCardItem& It = Card.Items[First + k];
					const float X = RowX + float(k) * ItemW;
					const FFVRect Box(Snap(X + 4.f), Snap(Y), ItemBox, ItemBox);
					if (const FFVSprite* S = ItemIconPtr(It.Id)) { FVUI::SpriteFit(D, *S, Box, FLinearColor::White, 2.f); }
					D.Text(FString::Printf(TEXT("×%d"), It.Count), FVector2D(Box.Right() + 3.f, Snap(Box.Y + (ItemBox - D.LineHeight(BodyS)) * 0.5f)), BodyS, FVPal::Ink(), EFVAlign::Left, false, true);
				}
				Y += ItemBox + 6.f;
			}
		}
		if (bScroll)
		{
			D.PopTranslate();
			D.PopClip();
			CardScroll.DrawBar(D, BodyR);
			CardScroll.DrawFades(D, BodyR, FVPal::Parchment());
		}

		int32 Result = -1;
		const float BY = R.Bottom() - Pad - BtnH;
		const float BtnText = FV::Ui::Body() + 1.f;
		if (Card.Secondary.IsEmpty())
		{
			const float BW = FMath::Min(InnerW, bP ? 200.f : 130.f);
			if (D.Button(FFVRect(Snap(CX - BW * 0.5f), BY, BW, BtnH), Card.Primary, EFVButton::Primary, true, BtnText) && Card.Age > 0.25f) { Result = 0; }
		}
		else
		{
			const FFVRect Row(R.X + Pad, BY, InnerW, BtnH);
			if (D.Button(FVDraw::SplitH(Row, 0, 2), Card.Secondary, EFVButton::Normal, true, BtnText) && Card.Age > 0.25f) { Result = 1; }
			if (D.Button(FVDraw::SplitH(Row, 1, 2), Card.Primary, EFVButton::Primary, true, BtnText) && Card.Age > 0.25f) { Result = 0; }
		}
		return Result;
	}

	// =====================================================================================================
	// base screen: window, footer (Назад + actions + hint), quit confirmation, result card, banner, juice
	// =====================================================================================================
	class FMiniGame : public FVScreen
	{
	public:
		FMiniGame(UFVGame* InGame, FName InStation)
			: FVScreen(InGame), ReturnStation(InStation)
		{
			bBot = InGame != nullptr && InGame->IsAutoTest();
			const int32 Seed = int32(FPlatformTime::Cycles() & 0x7fffffffu);
			Rng.Initialize(Seed);
			Juice.Rng.Initialize(Seed ^ 0x2545F491);
			Overlay.Rng.Initialize(Seed ^ 0x51ED270B);
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
						Overlay.Sparkles(CardIconCenter, 3, FV::IsPortrait() ? 34.f : 26.f);
					}
				}
				if (Card.Age > 0.35f && (ConfirmPressed(In) || In.WasPressed(EKeys::Escape))) { OnCardButton(0); return; }
				if (bBot && Card.Age > 1.3f) { OnCardButton(0); return; }
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
			LastArea = Area;

			// the game (its widgets are inert while the card is up: the card's buttons must get the clicks)
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
					const float HW = D.MeasureText(HintS, FV::Ui::Small(), false).X;
					if (HW <= Win.W - 36.f)
					{
						D.Text(HintS, FVector2D(Win.Center().X, HintY), FV::Ui::Small(), FVPal::InkSoft(), EFVAlign::Center, false, false);
					}
					else
					{
						D.TextWrapped(HintS, FVector2D(Win.X + 18.f, HintY), Win.W - 36.f, FV::Ui::Small(), FVPal::InkSoft());
					}
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
				const int32 Btn = DrawResultCard(D, Area, Card, Time, CardScroll, CardIconCenter);
				Overlay.Draw(D);
				if (Btn >= 0) { OnCardButton(Btn); return; }   // may destroy the screen
			}
			if (bBack) { RequestBack(); return; }                // may destroy the screen
		}

	protected:
		FName ReturnStation;
		bool bBot = false;               // autotest: the built-in bot plays
		bool bLeaving = false;
		bool bInteractive = false;       // Draw: pointer interactions allowed this frame
		float Time = 0.f;                // seconds (paused under modals, like the game logic)
		FRandomStream Rng;
		FJuice Juice;                    // game particles (under the card)
		FJuice Overlay;                  // card particles (above the card)
		FResultCard Card;
		float CardSparkT = 0.f;
		FVector2D CardIconCenter = FVector2D::ZeroVector;
		FString BannerText;
		FLinearColor BannerColor = FLinearColor::White;
		float BannerAge = 99.f;
		FFVRect LastArea;
		bool bPlayShot = false;

		virtual FString Title() const = 0;
		virtual FString ShotSlug() const = 0;
		virtual void TickGame(float Dt, const FFVInput& In) = 0;
		virtual void DrawGame(FVDraw& D, const FFVRect& Area) = 0;
		virtual void DrawActions(FVDraw& D, const FFVRect& Area) {}
		virtual FString Hint() const { return FString(); }
		virtual bool QuitNeedsConfirm() const { return true; }
		virtual FString QuitTitle() const { return TEXT("Бросить?"); }
		virtual FString QuitBody() const { return TEXT("Ничего не потратится."); }
		/** 0 = primary («Готово»), 1 = secondary. Default: leave. */
		virtual void OnCardButton(int32 Index) { Leave(); }

		/** Returns to the station screen / the world. Last statement of any caller: the screen is retired here. */
		void Leave()
		{
			if (bLeaving || !Game) { return; }
			bLeaving = true;
			UFVGame* G = Game;
			G->ReturnFromMiniGame(ReturnStation);
		}

		void RequestBack()
		{
			if (Card.bShown) { OnCardButton(0); return; }
			if (!QuitNeedsConfirm()) { Leave(); return; }
			AskQuit();
		}

		void AskQuit()
		{
			if (!Game || Game->HasModal()) { return; }
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			const FName Station = ReturnStation;
			Game->Confirm(QuitTitle(), QuitBody(), [WeakGame, Station]()
			{
				if (UFVGame* G = WeakGame.Get()) { G->ReturnFromMiniGame(Station); }
			}, TEXT("Бросить"), TEXT("Остаться"));
		}

		void Banner(const FString& Text, const FLinearColor& Col)
		{
			BannerText = Text;
			BannerColor = Col;
			BannerAge = 0.f;
		}

		void ShowCard(const FResultCard& C, EFVSfx Sound)
		{
			Card = C;
			Card.bShown = true;
			Card.Age = 0.f;
			CardSparkT = 0.25f;
			CardScroll.Reset();
			Sfx(Sound, 0.9f);
			if (bBot && Game) { Game->RequestShot(FString::Printf(TEXT("minigame_%s_result"), *ShotSlug())); }
		}

		/** The screen cannot run (bad context, missing shovel...): a card with the reason and «Назад». */
		void OpenFailed(const FString& Reason, EFVIcon Icon, const FString& InTitle = TEXT("Не получится"))
		{
			FResultCard C;
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
		FFVScrollView CardScroll;        // the card body scrolls when it is taller than the screen

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
	// COOKING (context = recipe id): cut phase (swipes / knife + Space) -> cook phase (doneness meter, green zone)
	// =====================================================================================================
	class FCookingScreen final : public FMiniGame
	{
	public:
		explicit FCookingScreen(UFVGame* InGame) : FMiniGame(InGame, FName(TEXT("st_kitchen"))) {}

		virtual void OnOpen() override
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Recipe = Game ? FVDB::FindRecipe(Game->GetScreenContext()) : nullptr;
			if (!P || !Recipe) { Recipe = nullptr; OpenFailed(TEXT("Рецепт не найден."), EFVIcon::Pot); return; }
			const EFVBuyResult Can = P->CanCraft(*Recipe, 1);
			if (Can != EFVBuyResult::Ok) { Recipe = nullptr; OpenFailed(UFVSaveGame::BuyResultText(Can), EFVIcon::Pot); return; }
			Dish = FVDB::FindItem(Recipe->OutputId);

			bool bUsesFish = false;
			for (const FFVIngredient& I : Recipe->Inputs)
			{
				if (FVDB::IsAnyFishToken(I.ItemId) || FVDB::IsFishId(I.ItemId)) { bUsesFish = true; }   // "any_fish" + rarity tokens
			}
			Fish = bUsesFish ? FindIngredientFish(*P, *Recipe) : nullptr;
			NumCuts = (Recipe->Game == EFVCraftGame::Cooking && Fish) ? FMath::Clamp(Recipe->GameDifficulty, 0, 5) : 0;

			switch (Dish ? Dish->Art : EFVItemArt::FishSoup)
			{
			case EFVItemArt::FriedFish:
			case EFVItemArt::FriedFishRare:     // update 5: dishes by rarity (§10.2)
			case EFVItemArt::FriedFishEpic:
			case EFVItemArt::FriedFishLegend: Vessel = EVessel::Pan; break;
			case EFVItemArt::FishSoup:  Vessel = EVessel::Pot; break;
			case EFVItemArt::HerbTea:   Vessel = EVessel::Kettle; break;
			case EFVItemArt::BerryPie:  Vessel = EVessel::Oven; break;
			// update 6 (§6.2): the dishes of the «Русская печь»
			case EFVItemArt::Draniki:
			case EFVItemArt::FishDill:  Vessel = EVessel::Pan; break;
			case EFVItemArt::OnionSoup:
			case EFVItemArt::PumpkinPorridge:
			case EFVItemArt::CarrotSoup: Vessel = EVessel::Pot; break;
			case EFVItemArt::FishPie:   Vessel = EVessel::Oven; break;
			default:                    Vessel = Fish ? EVessel::Pan : EVessel::Pot; break;
			}
			switch (Vessel)
			{
			case EVessel::Pan:    AddStage(TEXT("Перевернуть"), 1.f); AddStage(TEXT("Снять"), 1.1f); break;
			case EVessel::Pot:    AddStage(TEXT("Помешать"), 1.f); AddStage(TEXT("Снять с огня"), 1.1f); break;
			case EVessel::Kettle: AddStage(TEXT("Снять с огня"), 1.05f); break;
			case EVessel::Oven:   AddStage(TEXT("Достать"), 0.95f); break;
			}
			for (int32 i = 0; i < NumCuts; ++i)
			{
				const float Base = float(i + 1) / float(NumCuts + 1);
				CutFrac.Add(FMath::Clamp(Base + Rng.FRandRange(-0.04f, 0.04f), 0.12f, 0.88f));
			}
			CutFrac.Sort();
			Phase = EPhase::Intro;
			PhaseT = 0.f;
			Banner(NumCuts > 0 ? FString(TEXT("Нарезка!")) : CookIntroText(), FVPal::Gold());

			// update 5: the set-up for the autotest (§10.2: cuts and stage speed by rarity)
			FFVMiniGameDebug& Dbg = FVMiniGameDebug();
			++Dbg.CookSerial;
			Dbg.CookRecipe = Recipe->Id;
			Dbg.CookFish = Fish ? Fish->Id : NAME_None;
			Dbg.CookCuts = NumCuts;
			Dbg.CookSpeed = FMath::Max(0.1f, Recipe->CookSpeed);
			Dbg.CookStages = Stages.Num();
			Dbg.CookVessel = int32(Vessel);
		}

	protected:
		enum class EPhase : uint8 { Intro, Cut, CutOutro, Cook, Action, Finale, Over };
		enum class EVessel : uint8 { Pan, Pot, Kettle, Oven };
		struct FStage
		{
			FString Label;
			float Z0 = 0.6f, Z1 = 0.8f;   // green zone of the doneness meter
			float Rate = 1.f;
			float Score = -1.f;           // < 0 = not done yet
			float FinalD = 0.f;
			float Center() const { return (Z0 + Z1) * 0.5f; }
			float HalfW() const { return (Z1 - Z0) * 0.5f; }
		};
		struct FStepView
		{
			FString Label;
			uint8 State = 0;              // 0 pending, 1 current, 2 done
			FString Result;
			FLinearColor Color = FVPal::InkSoft();
		};

		static constexpr float KnifePass = 0.9f;     // seconds for one knife sweep across the fish
		static constexpr float BaseCookTime = 2.3f;  // seconds from raw to burnt (before the rate)

		const FFVRecipeDef* Recipe = nullptr;
		const FFVItemDef* Dish = nullptr;
		const FFVFishDef* Fish = nullptr;
		EVessel Vessel = EVessel::Pan;
		EPhase Phase = EPhase::Intro;
		float PhaseT = 0.f;
		// cutting
		int32 NumCuts = 0;
		TArray<float> CutFrac;
		TArray<float> CutAcc;
		int32 NextCut = 0;
		float KnifeClock = 0.f;
		float KnifeDrop = 1.f;
		bool bSwiping = false;
		TArray<FVector2D> SwipePts;
		TArray<FVector2D> LastSwipe;
		float SwipeFade = 0.f;
		// cooking
		TArray<FStage> Stages;
		int32 StageIdx = 0;
		float Doneness = 0.f;
		float SfxT = 0.f;
		float PuffT = 0.f;
		float ActionT = 0.f;
		bool bFlipped = false;
		bool bApplied = false;
		float BotT = 0.f;
		// layout of the last frame
		FFVRect BoardR, FishR, StoveR, MeterR;
		float FishScale = 1.f;
		float PieceGap = 4.f;
		FVector2D FoodC = FVector2D::ZeroVector;
		FVector2D SteamC = FVector2D::ZeroVector;
		float SteamW = 10.f;

		virtual FString Title() const override
		{
			return Recipe ? FString::Printf(TEXT("Кухня: %s"), *FVDB::ItemName(Recipe->OutputId)) : FString(TEXT("Кухня"));
		}
		virtual FString ShotSlug() const override { return TEXT("cooking"); }
		virtual FString QuitTitle() const override { return TEXT("Бросить готовку?"); }
		virtual FString QuitBody() const override { return TEXT("Продукты останутся целы."); }
		virtual bool QuitNeedsConfirm() const override { return Recipe != nullptr && Phase != EPhase::Over; }

		virtual FString Hint() const override
		{
			const bool bTouch = FV::IsTouchUI();
			if (Phase == EPhase::Cut || (Phase == EPhase::Intro && NumCuts > 0))
			{
				return bTouch ? TEXT("Проведите пальцем поперёк пунктира или жмите «Резать», когда нож над линией")
					: TEXT("Проведите мышью поперёк пунктира · Пробел — резать ножом");
			}
			if (Phase == EPhase::Cook || Phase == EPhase::Action || Phase == EPhase::Intro || Phase == EPhase::CutOutro)
			{
				return bTouch ? TEXT("Коснитесь плиты или кнопки, когда стрелка в зелёной зоне")
					: TEXT("Пробел или клик — когда стрелка в зелёной зоне");
			}
			return FString();
		}

		void AddStage(const TCHAR* Label, float Rate)
		{
			FStage& S = Stages.AddDefaulted_GetRef();
			S.Label = Label;
			const float Speed = Recipe ? FMath::Max(0.1f, Recipe->CookSpeed) : 1.f;   // update 5 (§10.2): x1.1 / 1.2 / 1.4 by rarity
			S.Rate = Rate * Speed * Rng.FRandRange(0.95f, 1.08f);
			S.Z0 = Rng.FRandRange(0.56f, 0.64f);
			S.Z1 = S.Z0 + Rng.FRandRange(0.17f, 0.2f) - 0.01f * float(Stages.Num() - 1);
		}

		FString CookIntroText() const
		{
			switch (Vessel)
			{
			case EVessel::Pan: return TEXT("На сковороду!");
			case EVessel::Pot: return TEXT("В котелок!");
			case EVessel::Kettle: return TEXT("Заварка!");
			default: return TEXT("В печь!");
			}
		}

		/** The fish the recipe will use: an explicit fish input, else the cheapest non-smoked keepnet fish of the token —
		    update 5: of exactly the token's rarity for "any_fish_rare" / "_epic" / "_legend", any rarity for "any_fish". */
		static const FFVFishDef* FindIngredientFish(const UFVSaveGame& P, const FFVRecipeDef& R)
		{
			bool bRarity = false;
			EFVRarity Want = EFVRarity::Common;
			for (const FFVIngredient& I : R.Inputs)
			{
				if (const FFVFishDef* F = FVDB::FindFish(I.ItemId)) { return F; }
				EFVRarity TokenRarity = EFVRarity::Common;
				if (!bRarity && FVDB::FishTokenRarity(I.ItemId, TokenRarity)) { bRarity = true; Want = TokenRarity; }
			}
			const FFVFishDef* Best = nullptr;
			int32 BestPrice = MAX_int32;
			for (const FFVInvStack& S : P.GetContainer(EFVContainer::Keepnet))
			{
				if (S.Count <= 0 || (S.Flags & FVStackFlags::Smoked) != 0) { continue; }
				const FFVFishDef* F = FVDB::FindFish(S.ItemId);
				if (!F || (bRarity && F->Rarity != Want)) { continue; }
				const int32 Price = FVDB::FishPrice(*F, EFVQuality(FMath::Min<uint8>(S.Quality, uint8(EFVQuality::Iridium))));
				if (Price < BestPrice) { Best = F; BestPrice = Price; }
			}
			return Best;
		}

		// ------------------------------------------------------------------ logic
		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			if (!Recipe || Stages.Num() == 0) { return; }
			PhaseT += Dt;
			KnifeDrop += Dt;
			SwipeFade = FMath::Max(0.f, SwipeFade - Dt * 3.f);
			const bool bKey = Time > 0.2f && ConfirmPressed(In);
			switch (Phase)
			{
			case EPhase::Intro:
				if (PhaseT >= (bBot ? 0.45f : 0.9f))
				{
					if (NumCuts > 0) { SetPhase(EPhase::Cut); }
					else { StartCook(); }
				}
				break;
			case EPhase::Cut:
				KnifeClock += Dt;
				if (bKey) { KnifeCut(); }
				else if (bBot && FishR.W > 0.f)
				{
					BotT += Dt;
					if (BotT >= 0.42f) { BotT = 0.f; PerformCut(LineX(NextCut) + Rng.FRandRange(-1.f, 1.f)); }
				}
				break;
			case EPhase::CutOutro:
				if (PhaseT >= (bBot ? 0.4f : 0.8f)) { StartCook(); }
				break;
			case EPhase::Cook:
				TickCook(Dt, bKey);
				break;
			case EPhase::Action:
				ActionT += Dt;
				HeatEffects(Dt);
				if (ActionT >= ActionDur()) { EndAction(); }
				break;
			case EPhase::Finale:
				if (PhaseT >= 0.75f) { ApplyResult(); }
				break;
			default:
				break;
			}
			if (Phase == EPhase::Cook && PhaseT > 0.8f) { RequestPlayShot(); }
		}

		float ActionDur() const { return bBot ? 0.32f : 0.45f; }

		void SetPhase(EPhase NewPhase)
		{
			Phase = NewPhase;
			PhaseT = 0.f;
		}

		float LineX(int32 Index) const
		{
			if (!CutFrac.IsValidIndex(Index)) { return FishR.Center().X; }
			return FishR.X + FishR.W * CutFrac[Index] + float(NextCut) * 0.5f * PieceGap;
		}

		float KnifeX() const
		{
			return FishR.X + FishR.W * Tri01(KnifeClock / (KnifePass * 2.f)) + float(NextCut) * 0.5f * PieceGap;
		}

		void KnifeCut()
		{
			if (Phase != EPhase::Cut || FishR.W <= 0.f) { return; }
			KnifeDrop = 0.f;
			PerformCut(KnifeX());
		}

		void PerformCut(float X)
		{
			if (Phase != EPhase::Cut || NextCut >= NumCuts) { return; }
			const float LX = LineX(NextCut);
			const float Tol = FMath::Max(10.f, FishR.W * 0.16f);
			const float Dist = FMath::Abs(X - LX);
			const float Acc = FMath::Clamp(1.f - FMath::Max(0.f, Dist - 0.2f * Tol) / (0.8f * Tol), 0.1f, 1.f);
			CutAcc.Add(Acc);
			++NextCut;
			const bool bP = FV::IsPortrait();
			const FVector2D P(LX, FishR.Center().Y);
			Sfx(EFVSfx::Chop, 0.95f, 0.92f + 0.2f * Acc);
			Juice.Burst(P, FV::Col(244, 196, 186), 8, 70.f, 240.f, bP ? 3.f : 2.f, 0.5f);
			Juice.Kick(0.6f);
			const FVector2D TP(LX, FishR.Y - 10.f);
			if (Acc >= 0.95f) { Juice.Sparkles(P, 8, 10.f); Juice.Text(TEXT("Точно!"), TP, FVPal::Gold(), bP ? 13.f : 11.f); }
			else if (Acc >= 0.7f) { Juice.Text(TEXT("Хорошо"), TP, FV::Col(150, 230, 110), bP ? 12.f : 10.f); }
			else if (Acc >= 0.4f) { Juice.Text(TEXT("Криво"), TP, FV::Col(250, 190, 90), bP ? 12.f : 10.f); }
			else { Juice.Text(TEXT("Мимо…"), TP, FV::Col(255, 130, 110), bP ? 12.f : 10.f); }
			if (NextCut >= NumCuts) { SetPhase(EPhase::CutOutro); }
		}

		/** Swipe path -> a cut when it crossed the fish vertically (live when it covers the whole body, else on release). */
		void EvaluateSwipe(bool bReleased)
		{
			if (SwipePts.Num() < 2 || FishR.H <= 0.f) { return; }
			float MinY = FLT_MAX, MaxY = -FLT_MAX, MinX = FLT_MAX, MaxX = -FLT_MAX;
			for (const FVector2D& Pt : SwipePts)
			{
				MinY = FMath::Min(MinY, float(Pt.Y)); MaxY = FMath::Max(MaxY, float(Pt.Y));
				MinX = FMath::Min(MinX, float(Pt.X)); MaxX = FMath::Max(MaxX, float(Pt.X));
			}
			const float CY = float(FishR.Center().Y);
			const bool bFull = MinY <= FishR.Y + FishR.H * 0.15f && MaxY >= FishR.Bottom() - FishR.H * 0.15f;
			const bool bEnough = (MaxY - MinY) >= FMath::Max(16.f, FishR.H * 0.5f) && MinY <= CY && MaxY >= CY;
			if (!bFull && !(bReleased && bEnough))
			{
				if (bReleased && (MaxX - MinX) > 18.f && (MaxY - MinY) < 12.f)
				{
					Juice.Text(TEXT("Режьте поперёк!"), FVector2D(FishR.Center().X, FishR.Y - 10.f), FV::Col(250, 190, 90), FV::IsPortrait() ? 11.f : 9.f);
				}
				return;
			}
			// X where the path crosses the middle line of the fish
			float CrossX = float(SwipePts[0].X);
			float BestDY = FLT_MAX;
			bool bCrossed = false;
			for (int32 i = 1; i < SwipePts.Num() && !bCrossed; ++i)
			{
				const FVector2D& A = SwipePts[i - 1];
				const FVector2D& B = SwipePts[i];
				if ((float(A.Y) - CY) * (float(B.Y) - CY) <= 0.f && A.Y != B.Y)
				{
					const float T = (CY - float(A.Y)) / float(B.Y - A.Y);
					CrossX = float(A.X) + T * float(B.X - A.X);
					bCrossed = true;
				}
			}
			if (!bCrossed)
			{
				for (const FVector2D& Pt : SwipePts)
				{
					const float DY = FMath::Abs(float(Pt.Y) - CY);
					if (DY < BestDY) { BestDY = DY; CrossX = float(Pt.X); }
				}
			}
			LastSwipe = SwipePts;
			SwipeFade = 1.f;
			SwipePts.Reset();
			bSwiping = false;
			PerformCut(CrossX);
		}

		void StartCook()
		{
			SetPhase(EPhase::Cook);
			StageIdx = 0;
			Doneness = 0.f;
			SfxT = 0.f;
			PuffT = 0.2f;
			Banner(CookIntroText(), FV::Col(255, 180, 90));
			Sfx(Vessel == EVessel::Oven ? EFVSfx::Fire : EFVSfx::Sizzle, 0.6f);
		}

		void TickCook(float Dt, bool bKey)
		{
			if (!Stages.IsValidIndex(StageIdx)) { return; }
			const FStage& S = Stages[StageIdx];
			Doneness += Dt * S.Rate * (0.75f + 0.55f * Doneness) / BaseCookTime;
			HeatEffects(Dt);
			if (Doneness >= 1.f) { Doneness = 1.f; Burn(); return; }
			if (bKey) { DoAction(); return; }
			if (bBot && Doneness >= S.Center() - 0.004f) { DoAction(); }
		}

		void HeatEffects(float Dt)
		{
			const float Heat = Phase == EPhase::Cook ? Doneness : 0.3f;
			SfxT -= Dt;
			if (SfxT <= 0.f)
			{
				switch (Vessel)
				{
				case EVessel::Pan: SfxT = 0.34f; Sfx(EFVSfx::Sizzle, 0.14f + 0.3f * Heat, Rng.FRandRange(0.92f, 1.08f)); break;
				case EVessel::Oven: SfxT = 0.9f; Sfx(EFVSfx::Fire, 0.22f + 0.2f * Heat, Rng.FRandRange(0.9f, 1.1f)); break;
				default: SfxT = 0.6f; Sfx(EFVSfx::Sizzle, 0.07f + 0.16f * Heat, Rng.FRandRange(0.55f, 0.65f)); break;
				}
			}
			PuffT -= Dt;
			if (PuffT <= 0.f && !SteamC.IsZero())
			{
				PuffT = FMath::Lerp(0.55f, 0.1f, Heat);
				const bool bSmoke = Stages.IsValidIndex(StageIdx) && Phase == EPhase::Cook && Doneness > Stages[StageIdx].Z1;
				const FLinearColor C = bSmoke ? FV::Col(70, 64, 64, 190) : FV::Col(250, 250, 250, 140);
				const float PS = FV::IsPortrait() ? 5.f : 4.f;
				Juice.Puff(SteamC + FVector2D(Rng.FRandRange(-SteamW, SteamW), 0.f), C, bSmoke ? PS * 1.3f : PS, bSmoke ? 34.f : 24.f);
			}
		}

		static float StageScore(float Dn, const FStage& S)
		{
			if (Dn >= S.Z0 && Dn <= S.Z1)
			{
				const float E = FMath::Abs(Dn - S.Center()) / FMath::Max(0.01f, S.HalfW());
				return E <= 0.3f ? 1.f : 1.f - 0.4f * (E - 0.3f) / 0.7f;
			}
			if (Dn < S.Z0) { return 0.5f * FMath::Pow(Clamp01(Dn / FMath::Max(0.01f, S.Z0)), 1.5f); }
			return FMath::Max(0.12f, 0.5f * (1.f - (Dn - S.Z1) / FMath::Max(0.01f, 1.f - S.Z1)));
		}

		void DoAction()
		{
			if (Phase != EPhase::Cook || !Stages.IsValidIndex(StageIdx)) { return; }
			FStage& S = Stages[StageIdx];
			S.FinalD = Doneness;
			S.Score = StageScore(Doneness, S);
			const bool bP = FV::IsPortrait();
			const bool bLast = StageIdx == Stages.Num() - 1;
			const FVector2D TP = FoodC - FVector2D(0.f, bP ? 40.f : 30.f);
			FString Word;
			FLinearColor WordC;
			if (Doneness < S.Z0) { Word = (Vessel == EVessel::Pan || Vessel == EVessel::Pot) ? TEXT("Сыровато") : TEXT("Рановато"); WordC = FV::Col(250, 190, 90); }
			else if (Doneness > S.Z1) { Word = Vessel == EVessel::Pan ? TEXT("Пережарено") : TEXT("Перестояло"); WordC = FV::Col(255, 130, 110); }
			else if (S.Score >= 0.99f) { Word = TEXT("Идеально!"); WordC = FVPal::Gold(); Juice.Sparkles(FoodC, 12, 24.f); }
			else if (S.Score >= 0.8f) { Word = TEXT("Отлично!"); WordC = FV::Col(150, 230, 110); }
			else { Word = TEXT("Хорошо"); WordC = FV::Col(190, 230, 140); }
			Juice.Text(Word, TP, WordC, bP ? 14.f : 11.f);
			if (!bLast && Vessel == EVessel::Pan)
			{
				Sfx(EFVSfx::Flip, 1.f, Rng.FRandRange(0.95f, 1.05f));
				Juice.Burst(FoodC, FV::Col(255, 236, 170), 8, 80.f, 260.f, bP ? 3.f : 2.f, 0.5f);
			}
			else if (!bLast)
			{
				Sfx(EFVSfx::Flip, 0.8f, 0.8f);
			}
			else
			{
				Sfx(EFVSfx::Pickup, 1.f, 1.f);
			}
			Juice.Kick(0.4f);
			SetPhase(EPhase::Action);
			ActionT = 0.f;
		}

		void Burn()
		{
			if (!Stages.IsValidIndex(StageIdx)) { return; }
			FStage& S = Stages[StageIdx];
			S.FinalD = 1.f;
			S.Score = 0.08f;
			const bool bP = FV::IsPortrait();
			Juice.Text(TEXT("Сгорело!"), FoodC - FVector2D(0.f, bP ? 40.f : 30.f), FV::Col(255, 110, 90), bP ? 15.f : 12.f);
			for (int32 i = 0; i < 6; ++i) { Juice.Puff(FoodC + FVector2D(Rng.FRandRange(-20.f, 20.f), Rng.FRandRange(-6.f, 6.f)), FV::Col(50, 46, 46, 210), bP ? 8.f : 6.f, 30.f); }
			Sfx(EFVSfx::Error, 0.8f, 0.8f);
			Juice.Kick(1.2f);
			SetPhase(EPhase::Action);
			ActionT = 0.f;
		}

		void EndAction()
		{
			const bool bWasFlip = Vessel == EVessel::Pan && StageIdx < Stages.Num() - 1;
			++StageIdx;
			if (bWasFlip) { bFlipped = !bFlipped; }
			if (StageIdx < Stages.Num())
			{
				SetPhase(EPhase::Cook);
				Doneness = 0.f;
			}
			else
			{
				StageIdx = Stages.Num() - 1;
				SetPhase(EPhase::Finale);
				Juice.Sparkles(FoodC, 14, 26.f);
				Sfx(EFVSfx::Treasure, 0.6f, 1.1f);
			}
		}

		float CutAvg() const
		{
			if (CutAcc.Num() == 0) { return 0.f; }
			float Sum = 0.f;
			for (const float A : CutAcc) { Sum += A; }
			return Sum / float(CutAcc.Num());
		}

		/** Score so far (false when nothing is judged yet). Cuts 30%, cooking 70%. */
		bool RunningScore(float& Out) const
		{
			float CookSum = 0.f;
			int32 CookN = 0;
			for (const FStage& S : Stages) { if (S.Score >= 0.f) { CookSum += S.Score; ++CookN; } }
			const bool bHasCuts = NumCuts > 0 && CutAcc.Num() > 0;
			if (!bHasCuts && CookN == 0) { return false; }
			if (!bHasCuts) { Out = CookSum / float(CookN); return true; }
			if (CookN == 0) { Out = CutAvg(); return true; }
			Out = 0.3f * CutAvg() + 0.7f * CookSum / float(CookN);
			return true;
		}

		void ApplyResult()
		{
			if (bApplied) { return; }
			bApplied = true;
			SetPhase(EPhase::Over);
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			float Score = 0.5f;
			RunningScore(Score);
			const EFVQuality Q = ScoreToQuality(Score);
			FResultCard C;
			C.IconId = Recipe ? Recipe->OutputId : NAME_None;
			if (!P || !Recipe)
			{
				C.bGood = false;
				C.Title = TEXT("Не получилось");
				C.Primary = TEXT("Назад");
				ShowCard(C, EFVSfx::Error);
				return;
			}
			const int32 CookedBefore = P->Stats.Cooked;
			const FFVRecipeDef Cooked = FVCookedRecipe(*P, *Recipe);   // «Русская печь»: two portions
			const EFVBuyResult R = P->Craft(Cooked, 1, Q);
			if (R != EFVBuyResult::Ok)
			{
				C.bGood = false;
				C.Title = TEXT("Не получилось");
				C.Sub = UFVSaveGame::BuyResultText(R) + TEXT(". Продукты не потрачены.");
				C.Primary = TEXT("Назад");
				ShowCard(C, EFVSfx::Error);
				return;
			}
			if (Dish && Dish->Kind == EFVItemKind::Food && P->Stats.Cooked == CookedBefore) { ++P->Stats.Cooked; }
			Game->NotifyProfileChanged();

			C.Title = FVDB::ItemName(Recipe->OutputId);
			if (Cooked.OutputCount > 1) { C.Title += FString::Printf(TEXT(" ×%d"), Cooked.OutputCount); }
			C.bStars = true;
			C.Quality = Q;
			C.Sub = NumCuts > 0
				? FString::Printf(TEXT("Оценка повара: %s (нарезка %s)"), *PercentText(Score), *PercentText(CutAvg()))
				: FString::Printf(TEXT("Оценка повара: %s"), *PercentText(Score));
			if (Q == EFVQuality::Iridium) { C.Badge = TEXT("Шедевр!"); }
			if (Dish && Dish->Kind == EFVItemKind::Food)
			{
				const float Pow = FVDB::FoodPower(Q);
				for (const FString& L : FVDB::StatModsLines(Dish->Buff.Scaled(Pow))) { C.AddLine(L, InkGood(), true); }
				if (Dish->BuffMinutes > 0.f) { C.AddLine(FString::Printf(TEXT("Действует %s"), *UFVGame::DurationText(Dish->BuffMinutes * Pow)), FVPal::InkSoft()); }
				C.AddLine(TEXT("Блюдо в рюкзаке: съешьте перед рыбалкой"), FVPal::InkSoft());
			}
			ShowCard(C, Q == EFVQuality::Iridium ? EFVSfx::Perfect : EFVSfx::Catch);
		}

		// ------------------------------------------------------------------ drawing
		virtual void DrawActions(FVDraw& D, const FFVRect& Area) override
		{
			if (!Recipe) { return; }
			const float TS = FV::IsPortrait() ? FV::Ui::Body() + 1.f : 8.f;
			if (Phase == EPhase::Cut || (Phase == EPhase::Intro && NumCuts > 0))
			{
				if (InstantButton(D, Area, TEXT("Резать"), EFVButton::Primary, Phase == EPhase::Cut && bInteractive, TS)) { KnifeCut(); }
				return;
			}
			const int32 Idx = FMath::Clamp(StageIdx, 0, Stages.Num() - 1);
			if (!Stages.IsValidIndex(Idx)) { return; }
			const bool bOn = Phase == EPhase::Cook && bInteractive;
			if (InstantButton(D, Area, Stages[Idx].Label, EFVButton::Primary, bOn, TS)) { DoAction(); }
		}

		virtual void DrawGame(FVDraw& D, const FFVRect& Area) override
		{
			if (!Recipe) { return; }
			const bool bP = FV::IsPortrait();
			FFVRect Info, Stage;
			if (bP)
			{
				Info = FFVRect(Area.X, Area.Y, Area.W, 46.f);
				Stage = FFVRect(Area.X, Area.Y + 52.f, Area.W, Area.H - 52.f);
			}
			else
			{
				Info = FFVRect(Area.X, Area.Y, 150.f, Area.H);
				Stage = FFVRect(Area.X + 158.f, Area.Y, Area.W - 158.f, Area.H);
			}
			DrawInfo(D, Info, bP);
			DrawKitchen(D, Stage);
			const bool bBoard = NumCuts > 0 && (Phase == EPhase::Intro || Phase == EPhase::Cut || Phase == EPhase::CutOutro);
			if (bBoard) { DrawBoard(D, Stage); }
			else { DrawStove(D, Stage); }
		}

		TArray<FStepView> BuildSteps() const
		{
			TArray<FStepView> Out;
			if (NumCuts > 0)
			{
				FStepView& V = Out.AddDefaulted_GetRef();
				V.Label = TEXT("Нарезка");
				if (NextCut >= NumCuts) { V.State = 2; V.Result = PercentText(CutAvg()); V.Color = CutAvg() >= 0.7f ? InkGood() : FVPal::InkSoft(); }
				else { V.State = Phase == EPhase::Cut ? 1 : 0; V.Result = FString::Printf(TEXT("%d/%d"), NextCut, NumCuts); }
			}
			for (int32 i = 0; i < Stages.Num(); ++i)
			{
				const FStage& S = Stages[i];
				FStepView& V = Out.AddDefaulted_GetRef();
				V.Label = S.Label;
				if (S.Score >= 0.f)
				{
					V.State = 2;
					if (S.FinalD >= 1.f) { V.Result = TEXT("сгорело"); V.Color = InkBad(); }
					else if (S.Score >= 0.99f) { V.Result = TEXT("идеально"); V.Color = InkGold(); }
					else if (S.Score >= 0.8f) { V.Result = TEXT("отлично"); V.Color = InkGood(); }
					else if (S.Score >= 0.6f) { V.Result = TEXT("хорошо"); V.Color = InkGood(); }
					else { V.Result = TEXT("так себе"); V.Color = InkBad(); }
				}
				else if (i == StageIdx && Phase == EPhase::Cook) { V.State = 1; }
			}
			return Out;
		}

		void DrawInfo(FVDraw& D, const FFVRect& R, bool bP)
		{
			D.Panel(R, EFVPanel::Parchment);
			const float IconS = bP ? 32.f : 30.f;
			const FFVRect IB(R.X + 7.f, R.Y + 7.f, IconS, IconS);
			D.Rect(IB, FVPal::ParchDark());
			D.Rect(IB.Inset(1.f), FV::Col(255, 236, 196));
			if (const FFVSprite* S = ItemIconPtr(Recipe->OutputId)) { FVUI::SpriteFit(D, *S, IB.Inset(2.f), FLinearColor::White, 4.f); }
			const FString Name = FVDB::ItemName(Recipe->OutputId);
			const TArray<FStepView> Steps = BuildSteps();
			float Running = 0.f;
			const bool bHasScore = RunningScore(Running);
			const float TX = IB.Right() + 7.f;
			if (bP)
			{
				const float Body = FV::Ui::Body();
				// running quality on the right
				float RightW = 0.f;
				if (bHasScore)
				{
					const EFVQuality Q = ScoreToQuality(Running);
					const FString Pc = PercentText(Running);
					RightW = D.MeasureText(Pc, Body).X + (Q != EFVQuality::Normal ? 16.f : 0.f) + 8.f;
					const float RX = R.Right() - 8.f;
					D.Text(Pc, FVector2D(RX, R.Y + 8.f), Body, FVPal::Ink(), EFVAlign::Right, false, true);
					if (Q != EFVQuality::Normal) { D.Stars(FVector2D(RX - D.MeasureText(Pc, Body).X - 15.f, R.Y + 9.f), Q, 1.5f); }
				}
				D.Text(D.Ellipsize(Name, R.Right() - TX - RightW - 6.f, Body), FVector2D(TX, R.Y + 6.f), Body, FVPal::Ink(), EFVAlign::Left, false, true);
				int32 Cur = 0;
				for (int32 i = 0; i < Steps.Num(); ++i) { if (Steps[i].State >= 1) { Cur = i; } }
				const FStepView* V = Steps.IsValidIndex(Cur) ? &Steps[Cur] : nullptr;
				const FString StepS = V ? FString::Printf(TEXT("Шаг %d из %d: %s"), Cur + 1, Steps.Num(), *V->Label) : FString();
				D.Text(D.Ellipsize(StepS, R.Right() - TX - 8.f, FV::Ui::Small(), false), FVector2D(TX, R.Y + 25.f), FV::Ui::Small(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				return;
			}
			D.TextWrapped(Name, FVector2D(TX, R.Y + 8.f), R.Right() - TX - 6.f, 8.f, FVPal::Ink(), true);
			float Y = FVUI::SectionHeader(D, TEXT("Шаги"), FVector2D(R.X + 8.f, IB.Bottom() + 8.f), R.W - 16.f, 9.f);
			for (const FStepView& V : Steps)
			{
				const float RowY = Y;
				if (V.State == 2)
				{
					if (const FFVSprite* Ck = FVUI::Icon(EFVIcon::Check)) { D.SpriteRect(*Ck, FFVRect(R.X + 8.f, RowY + 1.f, 9.f, 9.f)); }
				}
				else if (V.State == 1)
				{
					const float Bob = FMath::Sin(Time * 8.f) * 1.f;
					DrawArrow(D, FVector2D(R.X + 12.f + Bob, RowY + 5.5f), 7.f, DirRight, FVPal::Ink());
				}
				else
				{
					D.Rect(FFVRect(R.X + 11.f, RowY + 5.f, 2.f, 2.f), FVPal::InkSoft());
				}
				D.Text(D.Ellipsize(V.Label, R.W - 70.f, 7.5f, V.State == 1), FVector2D(R.X + 21.f, RowY), 7.5f, V.State == 0 ? FVPal::InkSoft() : FVPal::Ink(), EFVAlign::Left, false, V.State == 1);
				if (!V.Result.IsEmpty()) { D.Text(V.Result, FVector2D(R.Right() - 7.f, RowY + 1.f), 6.5f, V.Color, EFVAlign::Right, false, true); }
				Y += 15.f;
			}
			// running quality at the bottom
			const float BY = R.Bottom() - 30.f;
			D.Divider(FVector2D(R.X + 8.f, BY - 4.f), R.W - 16.f);
			D.Text(TEXT("Оценка"), FVector2D(R.X + 8.f, BY), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
			if (bHasScore)
			{
				const EFVQuality Q = ScoreToQuality(Running);
				D.Text(PercentText(Running), FVector2D(R.Right() - 8.f, BY), 9.f, FVPal::Ink(), EFVAlign::Right, false, true);
				D.Stars(FVector2D(R.X + 8.f, BY + 11.f), Q, 1.f);
				D.Text(FVDB::QualityName(Q), FVector2D(R.X + (Q == EFVQuality::Normal ? 8.f : 20.f), BY + 11.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, true);
			}
			else
			{
				D.Text(TEXT("—"), FVector2D(R.Right() - 8.f, BY), 9.f, FVPal::InkSoft(), EFVAlign::Right, false, true);
			}
		}

		void DrawKitchen(FVDraw& D, const FFVRect& Stage)
		{
			D.Rect(Stage.Inset(-1.f), FVPal::WoodDark());
			D.Rect(Stage, FV::Col(240, 226, 200));
			const float CounterY = Snap(Stage.Y + Stage.H * 0.62f);
			const float T = FV::IsPortrait() ? 18.f : 14.f;
			const FLinearColor Grout = FV::Col(222, 204, 174);
			for (int32 Rw = 0; ; ++Rw)
			{
				const float Y0 = Stage.Y + float(Rw) * T;
				if (Y0 >= CounterY) { break; }
				D.Rect(FFVRect(Stage.X, Snap(Y0), Stage.W, 1.f), Grout);
				const float Hgt = FMath::Min(T, CounterY - Y0);
				for (float X = Stage.X + ((Rw & 1) ? T * 0.5f : T); X < Stage.Right(); X += T) { D.Rect(FFVRect(Snap(X), Snap(Y0), 1.f, Hgt), Grout); }
			}
			D.Rect(FFVRect(Stage.X, CounterY, Stage.W, Stage.Bottom() - CounterY), FV::Col(168, 108, 60));
			D.Rect(FFVRect(Stage.X, CounterY, Stage.W, 3.f), FV::Col(206, 146, 86));
			D.Rect(FFVRect(Stage.X, CounterY + 3.f, Stage.W, 2.f), FV::Col(120, 72, 38));
			for (int32 k = 0; k < 4; ++k)
			{
				const float Y = Snap(CounterY + 10.f + float(k) * (Stage.Bottom() - CounterY - 14.f) / 4.f);
				const float X0 = Stage.X + 6.f + Hash01(k, 17) * 30.f;
				D.Rect(FFVRect(Snap(X0), Y, Snap(Stage.W * (0.4f + 0.4f * Hash01(k, 29))), 1.f), FV::Col(150, 94, 50));
			}
		}

		void DrawBoard(FVDraw& D, const FFVRect& Stage)
		{
			const bool bP = FV::IsPortrait();
			const float BW = FMath::Min(Stage.W - 24.f, bP ? 320.f : 360.f);
			const float BH = FMath::Min(Stage.H - 40.f, bP ? 230.f : 170.f);
			BoardR = FFVRect(Snap(Stage.Center().X - BW * 0.5f), Snap(FMath::Clamp(Stage.Y + Stage.H * 0.56f - BH * 0.5f, Stage.Y + 20.f, Stage.Bottom() - BH - 6.f)), BW, BH);
			PieceGap = bP ? 6.f : 5.f;
			// board
			D.Rect(BoardR.Offset(2.f, 4.f), FV::Col(60, 36, 20, 110));
			D.Rect(BoardR, FV::Col(142, 90, 46));
			D.Rect(BoardR.Inset(2.f), FV::Col(214, 162, 102));
			D.Rect(FFVRect(BoardR.X + 2.f, BoardR.Y + 2.f, BoardR.W - 4.f, 2.f), FV::Col(236, 190, 130));
			for (int32 k = 0; k < 7; ++k)
			{
				const float Y = Snap(BoardR.Y + 8.f + float(k) * (BoardR.H - 16.f) / 6.f);
				const float X0 = BoardR.X + 8.f + Hash01(k, 3) * 24.f;
				D.Rect(FFVRect(Snap(X0), Y, Snap(BoardR.W - 16.f - Hash01(k, 5) * 48.f), 1.f), FV::Col(196, 142, 86));
			}
			D.Circle(FVector2D(BoardR.Right() - 13.f, BoardR.Y + 13.f), bP ? 5.f : 4.f, FV::Col(142, 90, 46));

			// the fish (a plain rect is still cuttable if its sprite is missing, so the game can never get stuck)
			const FVector2D FC = BoardR.Center();
			UFVSprites* Sp = UFVSprites::Get();
			const FFVSprite* FS = (Sp && Fish) ? &Sp->Fish(*Fish) : nullptr;
			if (FS && FS->IsValid())
			{
				FishScale = FVUI::FitScale(*FS, BoardR.W * 0.78f, BoardR.H * 0.62f, 8.f);
				FishR = FFVRect(Snap(FC.X - FS->W * FishScale * 0.5f), Snap(FC.Y - FS->H * FishScale * 0.5f), FS->W * FishScale, FS->H * FishScale);
			}
			else
			{
				FishR = FFVRect(Snap(FC.X - BoardR.W * 0.3f), Snap(FC.Y - BoardR.H * 0.2f), Snap(BoardR.W * 0.6f), Snap(BoardR.H * 0.4f));
			}
			FillEllipse(D, FVector2D(FC.X, FishR.Bottom() - 2.f), FishR.W * 0.46f, FMath::Max(3.f, FishR.H * 0.12f), FV::Col(90, 56, 30, 90));
			TArray<float> DoneCuts;
			for (int32 i = 0; i < NextCut && i < CutFrac.Num(); ++i) { DoneCuts.Add(CutFrac[i]); }
			if (FS && FS->IsValid()) { DrawSpriteSlices(D, *FS, FC, FishScale, DoneCuts, PieceGap, FLinearColor::White, false, 0.f); }
			else { D.Rect(FishR, FV::Col(170, 180, 150)); }

			// dashed cut lines (the next one pulses)
			const float Thick = bP ? 3.f : 2.f;
			for (int32 i = NextCut; i < NumCuts; ++i)
			{
				const float LX = LineX(i);
				if (i == NextCut && Phase == EPhase::Cut)
				{
					const float Pulse = 0.65f + 0.35f * FMath::Sin(Time * 7.f);
					D.Glow(FVector2D(LX, FishR.Center().Y), FishR.H * 0.5f, WithAlpha(FV::Col(255, 255, 220) * 0.25f, 0.5f));
					DashedV(D, LX, FishR.Y - 10.f, FishR.Bottom() + 10.f, WithAlpha(FLinearColor::White, Pulse), Time * 18.f, Thick);
					DrawArrow(D, FVector2D(LX, FishR.Y - 16.f), bP ? 10.f : 7.f, DirDown, WithAlpha(FVPal::Gold(), Pulse));
				}
				else
				{
					DashedV(D, LX, FishR.Y - 6.f, FishR.Bottom() + 6.f, WithAlpha(FLinearColor::White, 0.35f), 0.f, 1.f);
				}
			}

			// the sliding knife (Space / «Резать»)
			if (Phase == EPhase::Cut && NextCut < NumCuts)
			{
				const float KX = Snap(KnifeX());
				const float Drop = KnifeDrop < 0.16f ? FMath::Sin(KnifeDrop / 0.16f * PI) * FishR.H * 0.55f : 0.f;
				const float TipY = Snap(FishR.Y - 8.f + Drop);
				const float BladeH = bP ? 34.f : 26.f, BladeW = bP ? 8.f : 6.f, HandleH = bP ? 16.f : 12.f;
				const FFVRect Blade(KX - BladeW * 0.5f, TipY - BladeH, BladeW, BladeH - 3.f);
				D.Rect(Blade.Offset(2.f, 2.f), FV::Col(0, 0, 0, 60));
				D.Rect(Blade, FV::Col(196, 202, 212));
				D.Rect(FFVRect(Blade.X, Blade.Y, 1.f, Blade.H), FV::Col(244, 246, 250));
				D.Rect(FFVRect(Blade.Right() - 1.f, Blade.Y, 1.f, Blade.H), FV::Col(128, 134, 146));
				D.Rect(FFVRect(Blade.X + 1.f, Blade.Bottom(), BladeW - 2.f, 2.f), FV::Col(196, 202, 212));
				D.Rect(FFVRect(KX - 1.f, Blade.Bottom() + 2.f, 2.f, 1.f), FV::Col(170, 176, 188));
				const FFVRect Handle(KX - BladeW * 0.5f - 1.f, Blade.Y - HandleH, BladeW + 2.f, HandleH);
				D.Rect(Handle, FV::Col(70, 38, 20));
				D.Rect(Handle.Inset(1.f), FV::Col(122, 70, 36));
				D.Rect(FFVRect(KX - 1.f, Handle.Y + 3.f, 2.f, 2.f), FV::Col(210, 200, 180));
				D.Rect(FFVRect(KX - 1.f, Handle.Bottom() - 5.f, 2.f, 2.f), FV::Col(210, 200, 180));
			}

			// swipe trails
			const float TrailW = bP ? 4.f : 3.f;
			for (int32 i = 1; i < SwipePts.Num(); ++i) { D.Line(SwipePts[i - 1], SwipePts[i], WithAlpha(FLinearColor::White, 0.85f), TrailW); }
			if (SwipeFade > 0.f)
			{
				for (int32 i = 1; i < LastSwipe.Num(); ++i) { D.Line(LastSwipe[i - 1], LastSwipe[i], WithAlpha(FV::Col(255, 250, 220), SwipeFade * 0.8f), TrailW * SwipeFade); }
			}

			// pointer swipes
			if (Phase == EPhase::Cut && bInteractive)
			{
				const FFVInput& In = D.Input();
				if (!bSwiping)
				{
					if (D.PressedIn(BoardR) && In.bMouseValid) { bSwiping = true; SwipePts.Reset(); SwipePts.Add(In.Mouse); }
				}
				else
				{
					if (In.bMouseValid && (SwipePts.Num() == 0 || FVector2D::Distance(SwipePts.Last(), In.Mouse) >= 2.0) && SwipePts.Num() < 96) { SwipePts.Add(In.Mouse); }
					EvaluateSwipe(false);
					if (bSwiping && (In.bLmbReleased || !In.bLmbDown))
					{
						EvaluateSwipe(true);
						SwipePts.Reset();
						bSwiping = false;
					}
				}
			}
			else if (bSwiping)
			{
				bSwiping = false;
				SwipePts.Reset();
			}
		}

		float VisibleDoneness() const
		{
			const float Prev = (StageIdx > 0 && Stages.IsValidIndex(StageIdx - 1)) ? Stages[StageIdx - 1].FinalD : 0.f;
			const float Cur = (Phase == EPhase::Cook) ? Doneness : (Stages.IsValidIndex(StageIdx) && Stages[StageIdx].Score >= 0.f ? Stages[StageIdx].FinalD : Doneness);
			if (Vessel == EVessel::Pan)
			{
				// the top side is raw while the first side cooks; after the flip the cooked side faces up
				return StageIdx == 0 ? Cur * 0.35f : Prev * 0.85f + Cur * 0.15f;
			}
			return FMath::Max(Cur, Prev * 0.8f);
		}

		void DrawStove(FVDraw& D, const FFVRect& Stage)
		{
			const bool bP = FV::IsPortrait();
			const float MW = bP ? 26.f : 20.f, MGap = bP ? 12.f : 14.f, LabW = bP ? 0.f : 46.f;
			const float SW = FMath::Min(Stage.W - MW - MGap - LabW - 20.f, 250.f);
			const float SH = FMath::Min(Stage.H - 20.f, bP ? 270.f : 220.f);
			const float Total = SW + MGap + MW + LabW;
			const float X0 = Snap(Stage.Center().X - Total * 0.5f);
			StoveR = FFVRect(X0, Snap(Stage.Bottom() - 8.f - SH), SW, SH);
			MeterR = FFVRect(StoveR.Right() + MGap, Snap(StoveR.Y + SH * 0.08f), MW, Snap(SH * 0.84f));
			const float VisD = VisibleDoneness();
			if (Vessel == EVessel::Oven) { DrawOven(D, VisD); }
			else
			{
				const float PlateY = DrawRange(D);
				if (Vessel == EVessel::Pan) { DrawPan(D, PlateY, VisD); }
				else { DrawPot(D, PlateY, VisD); }
			}
			DrawMeter(D, !bP);
			if (Phase == EPhase::Finale || Phase == EPhase::Over) { DrawDishPop(D); }
			if (bInteractive && Phase == EPhase::Cook)
			{
				if (D.PressedIn(FFVRect(StoveR.X, StoveR.Y, MeterR.Right() - StoveR.X, StoveR.H))) { DoAction(); }
			}
		}

		/** Iron stove with the burner. Returns the Y of the cooking plate. */
		float DrawRange(FVDraw& D)
		{
			const bool bP = FV::IsPortrait();
			const float PlateY = Snap(StoveR.Y + StoveR.H * 0.5f);
			const FFVRect Body(StoveR.X + 10.f, PlateY, StoveR.W - 20.f, StoveR.Bottom() - PlateY - 6.f);
			// legs
			D.Rect(FFVRect(Body.X + 4.f, Body.Bottom(), 8.f, 6.f), FV::Col(40, 36, 44));
			D.Rect(FFVRect(Body.Right() - 12.f, Body.Bottom(), 8.f, 6.f), FV::Col(40, 36, 44));
			D.Rect(Body.Offset(3.f, 3.f), FV::Col(0, 0, 0, 60));
			D.Rect(Body, FV::Col(40, 36, 44));
			D.Rect(Body.Inset(1.f), FV::Col(64, 58, 68));
			D.Rect(FFVRect(Body.X + 1.f, Body.Y + 1.f, 2.f, Body.H - 2.f), FV::Col(92, 86, 98));
			// firebox door with a glowing grille
			const FFVRect Door(Snap(Body.Center().X - Body.W * 0.2f), Snap(Body.Y + Body.H * 0.3f), Snap(Body.W * 0.4f), Snap(Body.H * 0.42f));
			D.Rect(Door, FV::Col(34, 30, 38));
			const float Flick = 0.75f + 0.25f * FMath::Sin(Time * 17.f) * FMath::Sin(Time * 7.f);
			const FFVRect Grille = Door.Inset(3.f);
			D.Rect(Grille, FV::Col(uint8(200.f * Flick), uint8(80.f * Flick), 30));
			for (float X = Grille.X + 3.f; X < Grille.Right() - 1.f; X += 5.f) { D.Rect(FFVRect(Snap(X), Grille.Y, 2.f, Grille.H), FV::Col(34, 30, 38)); }
			D.Glow(Grille.Center(), Door.W * 0.8f, WithAlpha(FV::Col(255, 120, 40) * 0.3f, 0.5f));
			// rivets
			for (int32 k = 0; k < 4; ++k)
			{
				const float RX = (k & 1) ? Body.Right() - 6.f : Body.X + 4.f;
				const float RY = (k & 2) ? Body.Bottom() - 6.f : Body.Y + 5.f;
				D.Rect(FFVRect(RX, RY, 2.f, 2.f), FV::Col(120, 114, 126));
			}
			// plate
			const FFVRect Plate(StoveR.X + 4.f, PlateY - 6.f, StoveR.W - 8.f, 8.f);
			D.Rect(Plate, FV::Col(46, 42, 50));
			D.Rect(Plate.Inset(1.f), FV::Col(92, 86, 98));
			D.Rect(FFVRect(Plate.X + 1.f, Plate.Y + 1.f, Plate.W - 2.f, 1.f), FV::Col(130, 124, 136));
			// flames
			const bool bHot = Phase == EPhase::Cook || Phase == EPhase::Action || Phase == EPhase::Intro || Phase == EPhase::CutOutro;
			const float Intensity = Phase == EPhase::Cook ? 0.85f + 0.3f * Doneness : (bHot ? 0.8f : 0.45f);
			DrawFlames(D, Time, StoveR.Center().X, Plate.Y, StoveR.W * 0.2f, bP ? 1.3f : 1.f, Intensity);
			return PlateY;
		}

		void DrawPan(FVDraw& D, float PlateY, float VisD)
		{
			const bool bP = FV::IsPortrait();
			const float RX = Snap(StoveR.W * 0.33f), RY = Snap(RX * 0.34f);
			const FVector2D C(StoveR.Center().X, PlateY - 6.f - RY * 0.55f - 4.f);
			FoodC = C - FVector2D(0.f, RY * 0.2f);
			SteamC = C - FVector2D(0.f, RY * 0.6f);
			SteamW = RX * 0.6f;
			// handle
			const FFVRect Hd(Snap(C.X + RX - 6.f), Snap(C.Y - 3.f), Snap(RX * 0.8f), bP ? 9.f : 7.f);
			D.Rect(Hd.Inset(-1.f), FV::Col(40, 22, 12));
			D.Rect(Hd, FV::Col(122, 70, 36));
			D.Rect(FFVRect(Hd.X, Hd.Y, Hd.W, 1.f), FV::Col(162, 102, 58));
			D.Rect(FFVRect(Hd.X + 3.f, Hd.Y + 2.f, 2.f, 2.f), FV::Col(200, 190, 170));
			// pan
			FillEllipse(D, C + FVector2D(0.f, 5.f), RX, RY, FV::Col(24, 24, 28));
			FillEllipse(D, C, RX, RY, FV::Col(48, 48, 56));
			FillEllipse(D, C + FVector2D(0.f, 1.5f), RX - 5.f, RY - 4.f, FV::Col(86, 84, 94));
			FillEllipse(D, C + FVector2D(-RX * 0.28f, -RY * 0.2f), RX * 0.28f, RY * 0.26f, FV::Col(106, 104, 116));
			// sizzling oil dots
			if (Phase == EPhase::Cook || Phase == EPhase::Action)
			{
				const float Heat = Phase == EPhase::Cook ? Doneness : 0.5f;
				for (int32 i = 0; i < 12; ++i)
				{
					const float Ph = FMath::Frac(Time * (1.3f + 0.25f * float(i % 3)) + Hash01(i, 1));
					const FVector2D DP = C + FVector2D((Hash01(i, 2) * 2.f - 1.f) * RX * 0.78f, (Hash01(i, 3) * 2.f - 1.f) * RY * 0.55f - Ph * 3.f);
					D.Rect(FFVRect(Snap(DP.X), Snap(DP.Y), bP ? 2.f : 1.f, bP ? 2.f : 1.f), WithAlpha(FLinearColor::White, (1.f - Ph) * (0.25f + 0.6f * Heat)));
				}
			}
			// the fish (pieces)
			if (Phase == EPhase::Finale || Phase == EPhase::Over) { return; }
			UFVSprites* Sp = UFVSprites::Get();
			if (!Sp) { return; }
			float YOff = 0.f, Rot = 0.f, Alpha = 1.f;
			bool bFlipNow = bFlipped;
			if (Phase == EPhase::Action && Stages.IsValidIndex(StageIdx))
			{
				const float T = Clamp01(ActionT / ActionDur());
				const bool bLast = StageIdx == Stages.Num() - 1;
				if (!bLast) { YOff = -FMath::Sin(T * PI) * RY * 2.6f; Rot = 360.f * T; bFlipNow = bFlipped != (T > 0.5f); }
				else { YOff = -Smooth01(T) * RY * 3.f; Alpha = 1.f - T; }
			}
			const FLinearColor Tint = WithAlpha(CookTint(VisD), Alpha);
			if (Fish)
			{
				const FFVSprite& FS = Sp->Fish(*Fish);
				const float Sc = FVUI::FitScale(FS, RX * 1.45f, RY * 1.9f, 8.f);
				DrawSpriteSlices(D, FS, FoodC + FVector2D(0.f, YOff), Sc, CutFrac, 2.f, Tint, bFlipNow, Rot);
			}
			else if (const FFVSprite* DS = ItemIconPtr(Recipe->OutputId))
			{
				const float Sc = FVUI::FitScale(*DS, RX, RY * 1.6f, 4.f);
				D.SpriteCentered(*DS, FoodC + FVector2D(0.f, YOff), Sc, Tint, bFlipNow, Rot);
			}
		}

		FLinearColor LiquidColor(float Dn) const
		{
			const bool bTea = Vessel == EVessel::Kettle;
			const FLinearColor A = bTea ? FV::Col(206, 214, 170) : FV::Col(224, 212, 162);
			const FLinearColor B = bTea ? FV::Col(196, 118, 54) : FV::Col(236, 176, 84);
			const FLinearColor Cc = bTea ? FV::Col(92, 52, 30) : FV::Col(128, 74, 40);
			if (Dn < 0.75f) { return FMath::Lerp(A, B, Dn / 0.75f); }
			return FMath::Lerp(B, Cc, Clamp01((Dn - 0.8f) / 0.2f));
		}

		void DrawPot(FVDraw& D, float PlateY, float VisD)
		{
			const bool bP = FV::IsPortrait();
			const bool bTea = Vessel == EVessel::Kettle;
			const float CX = StoveR.Center().X;
			const float BW = Snap(StoveR.W * (bTea ? 0.4f : 0.5f)), BH = Snap(StoveR.H * (bTea ? 0.24f : 0.3f));
			const FFVRect Body(Snap(CX - BW * 0.5f), PlateY - 6.f - BH, BW, BH);
			// handles
			D.Rect(FFVRect(Body.X - 7.f, Body.Y + 6.f, 8.f, bP ? 5.f : 4.f), FV::Col(40, 40, 46));
			D.Rect(FFVRect(Body.Right() - 1.f, Body.Y + 6.f, 8.f, bP ? 5.f : 4.f), FV::Col(40, 40, 46));
			D.Rect(Body, FV::Col(66, 68, 80));
			D.Rect(FFVRect(Body.X, Body.Bottom() - 4.f, Body.W, 4.f), FV::Col(46, 46, 56));
			D.Rect(FFVRect(Body.X + 3.f, Body.Y + 4.f, 2.f, Body.H - 10.f), FV::Col(110, 112, 128));
			D.Rect(FFVRect(Body.X + 7.f, Body.Y + 4.f, 1.f, Body.H - 12.f), FV::Col(90, 92, 106));
			// rim & liquid
			const float RimRY = Snap(FMath::Max(5.f, BW * 0.14f));
			const FVector2D Top(CX, Body.Y);
			FillEllipse(D, Top, BW * 0.5f + 3.f, RimRY, FV::Col(44, 44, 52));
			const FLinearColor Liq = LiquidColor(VisD);
			FillEllipse(D, Top + FVector2D(0.f, 1.f), BW * 0.5f - 2.f, RimRY - 2.f, Liq);
			FillEllipse(D, Top + FVector2D(-BW * 0.12f, 0.f), BW * 0.16f, FMath::Max(1.f, RimRY * 0.35f), FMath::Lerp(Liq, FLinearColor::White, 0.25f));
			FoodC = Top;
			SteamC = Top - FVector2D(0.f, RimRY);
			SteamW = BW * 0.3f;
			// floating ingredients
			int32 Shown = 0;
			if (Fish && !bTea)
			{
				if (UFVSprites* Sp = UFVSprites::Get())
				{
					const FFVSprite& FS = Sp->Fish(*Fish);
					const float Sc = FVUI::FitScale(FS, BW * 0.3f, RimRY * 1.6f, 3.f);
					TArray<float> Half;
					Half.Add(0.5f);
					const FVector2D FP = Top + FVector2D(-BW * 0.14f, FMath::Sin(Time * 2.f) * 1.f - 1.f);
					DrawSpriteSlices(D, FS, FP, Sc, Half, 2.f, CookTint(VisD * 0.8f), false, FMath::Sin(Time * 1.3f) * 6.f);
					++Shown;
				}
			}
			for (const FFVIngredient& I : Recipe->Inputs)
			{
				if (Shown >= 2 || FVDB::IsAnyFishToken(I.ItemId) || FVDB::IsFishId(I.ItemId)) { continue; }
				if (const FFVSprite* IS = ItemIconPtr(I.ItemId))
				{
					const float Side = Shown == 0 ? -0.12f : 0.14f;
					const FVector2D IP = Top + FVector2D(BW * Side, FMath::Sin(Time * 2.3f + float(Shown)) * 1.f - 2.f);
					D.SpriteCentered(*IS, IP, bP ? 1.5f : 1.f, CookTint(VisD * 0.6f), false, FMath::Sin(Time * 1.7f + float(Shown)) * 10.f);
					++Shown;
				}
			}
			// bubbles
			const float Heat = (Phase == EPhase::Cook) ? Doneness : 0.3f;
			for (int32 i = 0; i < 9; ++i)
			{
				if (Hash01(i, 41) > Heat * 1.3f + 0.1f) { continue; }
				const float Ph = FMath::Frac(Time * (0.9f + 0.2f * float(i % 4)) + Hash01(i, 43));
				const FVector2D BP = Top + FVector2D((Hash01(i, 47) * 2.f - 1.f) * (BW * 0.5f - 7.f), (Hash01(i, 53) * 2.f - 1.f) * (RimRY - 3.f));
				D.Circle(BP, 0.8f + 2.2f * Ph, WithAlpha(FMath::Lerp(Liq, FLinearColor::White, 0.45f), (1.f - Ph) * 0.9f));
			}
			// stirring ladle (first pot action)
			if (Phase == EPhase::Action && StageIdx < Stages.Num() - 1)
			{
				const float Ang = ActionT * 14.f;
				const FVector2D Tip = Top + FVector2D(FMath::Cos(Ang) * BW * 0.25f, FMath::Sin(Ang) * (RimRY - 2.f));
				D.Line(Tip, FVector2D(CX + BW * 0.34f, Body.Y - BH * 0.9f), FV::Col(150, 110, 70), bP ? 4.f : 3.f);
				D.Circle(Tip, bP ? 5.f : 4.f, FV::Col(120, 84, 50));
			}
		}

		void DrawOven(FVDraw& D, float VisD)
		{
			const bool bP = FV::IsPortrait();
			const FFVRect O = StoveR;
			const float CX = O.Center().X;
			const float DomeRY = Snap(O.H * 0.28f);
			const float BodyTop = O.Y + DomeRY;
			const FLinearColor Brick = FV::Col(176, 92, 60), Mortar = FV::Col(128, 70, 50), Outline = FV::Col(88, 42, 28);
			// chimney (behind)
			const FFVRect Chim(Snap(O.X + O.W * 0.66f), O.Y - 10.f, bP ? 20.f : 16.f, DomeRY + 4.f);
			D.Rect(Chim.Inset(-1.f), Outline);
			D.Rect(Chim, FV::Col(150, 78, 52));
			// body
			FillEllipse(D, FVector2D(CX, BodyTop), O.W * 0.5f + 1.f, DomeRY + 1.f, Outline, 1);
			D.Rect(FFVRect(O.X - 1.f, BodyTop, O.W + 2.f, O.Bottom() - BodyTop + 1.f), Outline);
			FillEllipse(D, FVector2D(CX, BodyTop), O.W * 0.5f, DomeRY, Brick, 1);
			D.Rect(FFVRect(O.X, BodyTop, O.W, O.Bottom() - BodyTop), Brick);
			// bricks
			const float BrickH = bP ? 10.f : 8.f, BrickW = bP ? 22.f : 18.f;
			int32 Rw = 0;
			for (float Y = BodyTop + BrickH; Y < O.Bottom(); Y += BrickH, ++Rw)
			{
				D.Rect(FFVRect(O.X, Snap(Y), O.W, 1.f), Mortar);
				for (float X = O.X + ((Rw & 1) ? BrickW * 0.5f : BrickW); X < O.Right() - 2.f; X += BrickW)
				{
					D.Rect(FFVRect(Snap(X), Snap(Y - BrickH + 1.f), 1.f, BrickH - 1.f), Mortar);
				}
			}
			for (int32 k = 0; k < 3; ++k)
			{
				const float Y = BodyTop - DomeRY * (0.3f + 0.25f * float(k));
				const float Hw = O.W * 0.5f * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square((BodyTop - Y) / DomeRY))) - 4.f;
				if (Hw > 4.f) { D.Rect(FFVRect(Snap(CX - Hw), Snap(Y), Snap(Hw * 2.f), 1.f), Mortar); }
			}
			// opening
			const float OW = Snap(O.W * 0.52f), OH = Snap(O.H * 0.4f);
			const float OY = Snap(BodyTop + O.H * 0.04f);
			const float ArchRY = Snap(OW * 0.3f);
			const FLinearColor Hole = FV::Col(34, 20, 16);
			FillEllipse(D, FVector2D(CX, OY + ArchRY), OW * 0.5f + 2.f, ArchRY + 2.f, Outline, 1);
			D.Rect(FFVRect(Snap(CX - OW * 0.5f - 2.f), OY + ArchRY, OW + 4.f, OH - ArchRY + 2.f), Outline);
			FillEllipse(D, FVector2D(CX, OY + ArchRY), OW * 0.5f, ArchRY, Hole, 1);
			const FFVRect Mouth(Snap(CX - OW * 0.5f), OY + ArchRY, OW, OH - ArchRY);
			D.Rect(Mouth, Hole);
			const float Flick = 0.8f + 0.2f * FMath::Sin(Time * 13.f) * FMath::Sin(Time * 5.f);
			D.Glow(FVector2D(CX, Mouth.Bottom() - 8.f), OW * 0.7f, WithAlpha(FV::Col(255, 120, 40) * (0.45f * Flick), 0.7f));
			DrawFlames(D, Time, CX, Mouth.Bottom() - 2.f, OW * 0.36f, bP ? 1.1f : 0.8f, 0.6f + 0.3f * Flick);
			// ledge
			D.Rect(FFVRect(Mouth.X - 6.f, Mouth.Bottom(), Mouth.W + 12.f, bP ? 7.f : 5.f), FV::Col(150, 140, 130));
			D.Rect(FFVRect(Mouth.X - 6.f, Mouth.Bottom(), Mouth.W + 12.f, 1.f), FV::Col(196, 188, 178));
			SteamC = FVector2D(CX, OY + 4.f);
			SteamW = OW * 0.25f;
			// the pie on its tray
			if (Phase == EPhase::Finale || Phase == EPhase::Over) { FoodC = FVector2D(CX, Mouth.Bottom() - 12.f); return; }
			const FFVSprite* PS = ItemIconPtr(Recipe->OutputId);
			float Out = 0.f;
			if (Phase == EPhase::Action && StageIdx == Stages.Num() - 1) { Out = Smooth01(ActionT / ActionDur()); }
			const float Sc = PS ? FVUI::FitScale(*PS, OW * 0.62f, OH * 0.6f, 5.f) * (1.f + 0.25f * Out) : 1.f;
			const FVector2D PieC(CX, Mouth.Bottom() - (PS ? PS->H * Sc * 0.5f : 10.f) - 3.f + Out * OH * 0.55f);
			FoodC = PieC;
			D.Rect(FFVRect(Snap(PieC.X - OW * 0.36f), Snap(PieC.Y + (PS ? PS->H * Sc * 0.4f : 6.f)), Snap(OW * 0.72f), 3.f), FV::Col(96, 96, 108));
			if (PS) { D.SpriteCentered(*PS, PieC, Sc, CookTint(VisD)); }
		}

		void DrawMeter(FVDraw& D, bool bLabels)
		{
			if (Stages.Num() == 0) { return; }
			const FStage& S = Stages[FMath::Clamp(StageIdx, 0, Stages.Num() - 1)];
			WoodFrame(D, MeterR);
			const FFVRect T = InnerTrack(D, MeterR, 4.f, FV::Col(48, 30, 22));
			auto YAt = [&T](float V) { return Snap(T.Bottom() - T.H * Clamp01(V)); };
			const float ZY0 = YAt(S.Z1), ZY1 = YAt(S.Z0);
			D.Rect(FFVRect(T.X, T.Y, T.W, ZY0 - T.Y), FV::Col(222, 60, 44, 70));
			D.Rect(FFVRect(T.X, ZY0, T.W, ZY1 - ZY0), FV::Col(96, 204, 64, 95));
			const float PY0 = YAt(S.Center() + 0.3f * S.HalfW()), PY1 = YAt(S.Center() - 0.3f * S.HalfW());
			D.Rect(FFVRect(T.X, PY0, T.W, FMath::Max(1.f, PY1 - PY0)), FV::Col(170, 250, 120, 120));
			float Dv = 0.f;
			if (Phase == EPhase::Cook) { Dv = Doneness; }
			else if (Phase == EPhase::Action || Phase == EPhase::Finale || Phase == EPhase::Over) { Dv = S.Score >= 0.f ? S.FinalD : Doneness; }
			const float FH = Snap(T.H * Clamp01(Dv));
			const FLinearColor FC = Dv < S.Z0 ? FV::Col(246, 206, 58) : (Dv <= S.Z1 ? FV::Col(96, 204, 64) : FV::Col(222, 60, 44));
			if (FH > 0.f)
			{
				D.Rect(FFVRect(T.X + 2.f, T.Bottom() - FH, T.W - 4.f, FH), WithAlpha(FC, 0.92f));
				D.Rect(FFVRect(T.X + 2.f, T.Bottom() - FH, T.W - 4.f, 1.f), FMath::Lerp(FC, FLinearColor::White, 0.45f));
			}
			// zone ticks
			D.Rect(FFVRect(T.X - 4.f, ZY0, 3.f, 1.f), FV::Col(96, 204, 64));
			D.Rect(FFVRect(T.X - 4.f, ZY1, 3.f, 1.f), FV::Col(96, 204, 64));
			// level marker (triangle pointing at the track)
			const float MY = Snap(T.Bottom() - FH);
			for (int32 i = 0; i < 4; ++i) { D.Rect(FFVRect(MeterR.X - 7.f + float(i), MY - float(3 - i), 1.f, float((3 - i) * 2 + 1)), FVPal::Ink()); }
			if (Phase == EPhase::Cook && Dv >= S.Z0 && Dv <= S.Z1)
			{
				D.Glow(FVector2D(T.Center().X, MY), 16.f, WithAlpha(FV::Col(120, 255, 90) * 0.5f, 0.55f));
			}
			if (bLabels)
			{
				const float LX = MeterR.Right() + 5.f;
				D.Text(TEXT("Сгорит"), FVector2D(LX, T.Y + 1.f), 6.5f, InkBad(), EFVAlign::Left, false, true);
				D.Text(TEXT("Готово"), FVector2D(LX, Snap((ZY0 + ZY1) * 0.5f - 4.f)), 6.5f, InkGood(), EFVAlign::Left, false, true);
				D.Text(TEXT("Сыро"), FVector2D(LX, T.Bottom() - 9.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, true);
			}
		}

		void DrawDishPop(FVDraw& D)
		{
			const FFVSprite* DS = ItemIconPtr(Recipe->OutputId);
			if (!DS) { return; }
			const bool bP = FV::IsPortrait();
			const float T = Phase == EPhase::Over ? 1.f : Clamp01(PhaseT / 0.5f);
			const float Sc = (bP ? 4.f : 3.f) * EaseOutBack(T);
			const FVector2D P = FoodC - FVector2D(0.f, (bP ? 40.f : 30.f) * Smooth01(T));
			for (int32 k = 0; k < 10; ++k)
			{
				const float Ang = Time * 0.9f + float(k) * PI / 5.f;
				const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
				D.Line(P + Dir * (Sc * 6.f), P + Dir * (Sc * 11.f), WithAlpha(FVPal::Gold(), 0.45f * T), 2.f);
			}
			D.Glow(P, Sc * 10.f, WithAlpha(FV::Col(255, 220, 120) * 0.5f, 0.6f * T));
			if (Sc > 0.1f) { D.SpriteCentered(*DS, P, Sc); }
		}
	};

	// =====================================================================================================
	// KNOTS (context = recipe id): watch the arrow pattern, repeat it (keys / swipes / d-pad), 3 tries
	// =====================================================================================================
	class FKnotsScreen final : public FMiniGame
	{
	public:
		explicit FKnotsScreen(UFVGame* InGame) : FMiniGame(InGame, FName(TEXT("st_workshop"))) {}

		virtual void OnOpen() override
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Recipe = Game ? FVDB::FindRecipe(Game->GetScreenContext()) : nullptr;
			if (!P || !Recipe) { Recipe = nullptr; OpenFailed(TEXT("Рецепт не найден."), EFVIcon::Knot); return; }
			const EFVBuyResult Can = P->CanCraft(*Recipe, 1);
			if (Can != EFVBuyResult::Ok) { Recipe = nullptr; OpenFailed(UFVSaveGame::BuyResultText(Can), EFVIcon::Knot); return; }
			OutItem = FVDB::FindItem(Recipe->OutputId);
			const int32 N = FMath::Clamp(Recipe->GameDifficulty > 0 ? Recipe->GameDifficulty : 4, 3, 9);
			for (int32 i = 0; i < N; ++i)
			{
				int32 Dir = Rng.RandRange(0, 3);
				while (i >= 2 && Dir == Pattern[i - 1] && Dir == Pattern[i - 2]) { Dir = Rng.RandRange(0, 3); }
				Pattern.Add(uint8(Dir));
			}
			StepT = FMath::Clamp(0.6f - 0.03f * float(N - 4), 0.38f, 0.62f);
			Phase = EPhase::Intro;
			PhaseT = 0.f;
			Banner(TEXT("Запоминайте узор!"), FVPal::Gold());
		}

	protected:
		enum class EPhase : uint8 { Intro, Show, Input, Mistake, Success, Over };
		static constexpr float ShowLead = 0.3f;
		static constexpr int32 MaxTries = 3;

		const FFVRecipeDef* Recipe = nullptr;
		const FFVItemDef* OutItem = nullptr;
		TArray<uint8> Pattern;
		int32 Entered = 0;
		int32 Tries = MaxTries;
		EPhase Phase = EPhase::Intro;
		float PhaseT = 0.f;
		float StepT = 0.55f;
		int32 ShowIdx = -1;
		float PadFlash[4] = { 0.f, 0.f, 0.f, 0.f };
		int32 WrongDir = -1;
		float Wiggle = 0.f;
		TArray<float> KnotAge;
		FDirInput Dirs;
		bool bSwipe = false;
		FVector2D SwipeStart = FVector2D::ZeroVector;
		float BotT = 0.f;
		// layout of the last frame
		TArray<float> TileX;
		float TileSize = 38.f;
		float RopeY = 0.f;
		FFVRect PadR[4];

		virtual FString Title() const override
		{
			return Recipe ? FString::Printf(TEXT("Плетение: %s"), *FVDB::ItemName(Recipe->OutputId)) : FString(TEXT("Плетение"));
		}
		virtual FString ShotSlug() const override { return TEXT("knots"); }
		virtual FString QuitTitle() const override { return TEXT("Бросить плетение?"); }
		virtual FString QuitBody() const override { return TEXT("Материалы останутся целы."); }
		virtual bool QuitNeedsConfirm() const override { return Recipe != nullptr && Phase != EPhase::Over; }
		virtual FString Hint() const override
		{
			return FV::IsTouchUI() ? TEXT("Смотрите, затем повторите: смахивайте или жмите стрелки")
				: TEXT("Стрелки / WASD, клик по стрелкам или смахивание мышью");
		}

		static float DirPitch(int32 Dir)
		{
			switch (Dir) { case DirUp: return 1.25f; case DirRight: return 1.1f; case DirDown: return 0.85f; default: return 0.98f; }
		}

		FVector2D KnotPos(int32 Index) const
		{
			if (!TileX.IsValidIndex(Index)) { return LastArea.Center(); }
			return FVector2D(TileX[Index] + TileSize * 0.5f, RopeY);
		}

		void StartShow()
		{
			Phase = EPhase::Show;
			PhaseT = 0.f;
			ShowIdx = -1;
			Entered = 0;
			WrongDir = -1;
			KnotAge.Reset();
		}

		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			if (!Recipe || Pattern.Num() == 0) { return; }
			PhaseT += Dt;
			for (float& F : PadFlash) { F = FMath::Max(0.f, F - Dt * 4.f); }
			Wiggle = FMath::Max(0.f, Wiggle - Dt * 1.2f);
			for (float& K : KnotAge) { K += Dt; }
			const int32 Dir = Dirs.Poll(In);
			const int32 N = Pattern.Num();
			switch (Phase)
			{
			case EPhase::Intro:
				if (PhaseT >= (bBot ? 0.5f : 0.9f)) { StartShow(); }
				break;
			case EPhase::Show:
			{
				const int32 Idx = FMath::FloorToInt((PhaseT - ShowLead) / StepT);
				if (Idx >= 0 && Idx < N && Idx != ShowIdx)
				{
					ShowIdx = Idx;
					PadFlash[Pattern[Idx]] = 1.f;
					Sfx(EFVSfx::Knot, 0.7f, DirPitch(Pattern[Idx]));
				}
				if (PhaseT >= ShowLead + float(N) * StepT + 0.2f)
				{
					Phase = EPhase::Input;
					PhaseT = 0.f;
					ShowIdx = -1;
					Entered = 0;
					Banner(TEXT("Повторите!"), FV::Col(150, 230, 110));
				}
				RequestPlayShot();
				break;
			}
			case EPhase::Input:
				if (Dir >= 0) { Enter(Dir); }
				else if (bBot)
				{
					BotT += Dt;
					if (BotT >= 0.26f && Pattern.IsValidIndex(Entered)) { BotT = 0.f; Enter(Pattern[Entered]); }
				}
				break;
			case EPhase::Mistake:
				if (PhaseT >= 1.2f)
				{
					if (Tries > 0) { StartShow(); }
					else { Fail(); }
				}
				break;
			case EPhase::Success:
				if (PhaseT >= 0.9f) { ApplyResult(); }
				break;
			default:
				break;
			}
		}

		void Enter(int32 Dir)
		{
			if (Phase != EPhase::Input || !Pattern.IsValidIndex(Entered) || Dir < 0 || Dir > 3) { return; }
			PadFlash[Dir] = 1.f;
			const bool bP = FV::IsPortrait();
			if (Pattern[Entered] == Dir)
			{
				const FVector2D KP = KnotPos(Entered);
				KnotAge.Add(0.f);
				++Entered;
				Sfx(EFVSfx::Knot, 0.95f, 0.85f + 0.07f * float(Entered));
				Juice.Sparkles(KP, 5, 8.f);
				Juice.Burst(KP, RopeMain(), 4, 40.f, 120.f, bP ? 3.f : 2.f, 0.4f);
				if (Entered >= Pattern.Num())
				{
					Phase = EPhase::Success;
					PhaseT = 0.f;
					Banner(TEXT("Узел завязан!"), FV::Col(150, 230, 110));
					for (int32 i = 0; i < Pattern.Num(); ++i) { Juice.Sparkles(KnotPos(i), 3, 10.f); }
					Sfx(EFVSfx::Knot, 1.f, 1.5f);
				}
			}
			else
			{
				WrongDir = Dir;
				--Tries;
				Sfx(EFVSfx::Error, 0.9f);
				Juice.Kick(1.6f);
				Wiggle = 1.f;
				Banner(TEXT("Узел распустился!"), FV::Col(255, 130, 110));
				const FVector2D TP(LastArea.Center().X, RopeY + (bP ? 30.f : 22.f));
				Juice.Text(Tries > 0 ? FString::Printf(TEXT("Осталось попыток: %d"), Tries) : FString(TEXT("Попытки кончились")), TP, FV::Col(255, 150, 130), bP ? 12.f : 9.f);
				for (int32 i = 0; i < Entered; ++i) { Juice.Burst(KnotPos(i), RopeMain(), 5, 50.f, 160.f, bP ? 3.f : 2.f, 0.5f); }
				KnotAge.Reset();
				Phase = EPhase::Mistake;
				PhaseT = 0.f;
			}
		}

		void Fail()
		{
			Phase = EPhase::Over;
			FResultCard C;
			C.bGood = false;
			C.Title = TEXT("Узел не получился");
			C.Sub = TEXT("Материалы остались целы. Попробуйте ещё раз!");
			C.IconId = Recipe ? Recipe->OutputId : NAME_None;
			ShowCard(C, EFVSfx::Escape);
		}

		void ApplyResult()
		{
			Phase = EPhase::Over;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			FResultCard C;
			C.IconId = Recipe ? Recipe->OutputId : NAME_None;
			if (!P || !Recipe) { C.bGood = false; C.Title = TEXT("Не получилось"); ShowCard(C, EFVSfx::Error); return; }
			const int32 KnotsBefore = P->Stats.KnotsTied;
			const int32 CraftedBefore = P->Stats.Crafted;
			const EFVBuyResult R = P->Craft(*Recipe, 1);
			if (R != EFVBuyResult::Ok)
			{
				C.bGood = false;
				C.Title = TEXT("Не получилось");
				C.Sub = UFVSaveGame::BuyResultText(R) + TEXT(". Материалы не потрачены.");
				ShowCard(C, EFVSfx::Error);
				return;
			}
			if (P->Stats.KnotsTied == KnotsBefore) { ++P->Stats.KnotsTied; }
			if (P->Stats.Crafted == CraftedBefore) { P->Stats.Crafted += FMath::Max(1, Recipe->OutputCount); }
			Game->NotifyProfileChanged();

			C.Title = FVDB::ItemName(Recipe->OutputId);
			if (Recipe->OutputCount > 1) { C.Title += FString::Printf(TEXT(" ×%d"), Recipe->OutputCount); }
			const int32 Attempt = MaxTries - Tries + 1;
			if (Attempt <= 1) { C.Badge = TEXT("С первой попытки!"); }
			C.Sub = FString::Printf(TEXT("Узел из %d петель завязан"), Pattern.Num());
			if (OutItem)
			{
				if (OutItem->Kind == EFVItemKind::Line || OutItem->Kind == EFVItemKind::Float)
				{
					for (const FString& L : FVDB::StatModsLines(OutItem->Tackle)) { C.AddLine(L, InkGood(), true); }
					C.AddLine(TEXT("Установите в «Оснастке» мастерской"), FVPal::InkSoft());
				}
				else if (OutItem->Kind == EFVItemKind::Net)
				{
					C.AddLine(FString::Printf(TEXT("Вместимость: %d %s"), OutItem->NetCapacity, FVUI::Plural(OutItem->NetCapacity, TEXT("рыба"), TEXT("рыбы"), TEXT("рыб"))), InkGood(), true);
					C.AddLine(FString::Printf(TEXT("Прочность: %d"), FMath::RoundToInt(OutItem->NetDurability)), InkGood(), true);
					C.AddLine(TEXT("Поставьте её на берегу у сетевых кольев"), FVPal::InkSoft());
				}
				else if (!OutItem->Description.IsEmpty())
				{
					C.AddLine(OutItem->Description, FVPal::InkSoft());
				}
			}
			ShowCard(C, EFVSfx::Catch);
		}

		// ------------------------------------------------------------------ drawing
		virtual void DrawGame(FVDraw& D, const FFVRect& Area) override
		{
			if (!Recipe || Pattern.Num() == 0) { return; }
			const bool bP = FV::IsPortrait();
			const int32 N = Pattern.Num();

			// header: what we tie + tries
			const float HeadH = bP ? 26.f : 20.f;
			const float HS = FV::Ui::Body();
			const FFVRect IconB(Area.X + 4.f, Area.Y + 1.f, HeadH - 2.f, HeadH - 2.f);
			if (const FFVSprite* OS = ItemIconPtr(Recipe->OutputId)) { FVUI::SpriteFit(D, *OS, IconB, FLinearColor::White, 2.f); }
			const float IconSz = bP ? 20.f : 14.f;
			const float TriesW = float(MaxTries) * (IconSz + 3.f);
			const float TriesX = Area.Right() - 6.f - TriesW;
			D.Text(D.Ellipsize(FVDB::ItemName(Recipe->OutputId), TriesX - IconB.Right() - 70.f, HS), FVector2D(IconB.Right() + 6.f, Snap(Area.Y + (HeadH - D.LineHeight(HS)) * 0.5f)), HS, FVPal::Ink(), EFVAlign::Left, false, true);
			D.Text(TEXT("Попытки:"), FVector2D(TriesX - 4.f, Snap(Area.Y + (HeadH - D.LineHeight(FV::Ui::Small())) * 0.5f)), FV::Ui::Small(), FVPal::InkSoft(), EFVAlign::Right, false, true);
			for (int32 i = 0; i < MaxTries; ++i)
			{
				const bool bLeft = i < Tries;
				const FVector2D TC(TriesX + float(i) * (IconSz + 3.f) + IconSz * 0.5f, Area.Y + HeadH * 0.5f);
				if (const FFVSprite* KS = FVUI::Icon(EFVIcon::Knot)) { D.SpriteCentered(*KS, TC, IconSz / FMath::Max(1.f, KS->W), WithAlpha(FLinearColor::White, bLeft ? 1.f : 0.25f)); }
				else { DrawKnot(D, TC, IconSz * 0.4f); }
			}

			// the pattern tiles
			const float Gap = bP ? 5.f : 6.f;
			TileSize = FMath::Min(bP ? 46.f : 38.f, FMath::FloorToFloat((Area.W - 24.f) / float(N)) - Gap);
			const float RowW = float(N) * TileSize + float(N - 1) * Gap;
			const float TX0 = Snap(Area.Center().X - RowW * 0.5f);
			const float TY = Snap(Area.Y + HeadH + (bP ? 14.f : 10.f));
			TileX.SetNum(N);
			for (int32 i = 0; i < N; ++i) { TileX[i] = TX0 + float(i) * (TileSize + Gap); }
			for (int32 i = 0; i < N; ++i)
			{
				const float Since = PhaseT - ShowLead - float(i) * StepT;
				const bool bLit = Phase == EPhase::Show && i == ShowIdx && Since < StepT * 0.72f;
				const bool bDone = ((Phase == EPhase::Input || Phase == EPhase::Mistake) && i < Entered) || Phase == EPhase::Success || (Phase == EPhase::Over && Card.bGood);
				const bool bWrong = Phase == EPhase::Mistake && i == Entered;
				const bool bNext = Phase == EPhase::Input && i == Entered;
				FLinearColor Face = FV::Col(186, 118, 64);
				if (bLit) { Face = FV::Col(255, 214, 90); }
				else if (bWrong) { Face = FV::Col(226, 90, 70); }
				else if (bDone) { Face = FV::Col(126, 196, 86); }
				float Bounce = bLit ? -3.f : 0.f;
				if (Phase == EPhase::Success) { Bounce = -FMath::Max(0.f, FMath::Sin(PhaseT * 9.f - float(i) * 0.6f)) * 4.f; }
				const FFVRect TR(TileX[i], TY + Snap(Bounce), TileSize, TileSize);
				if (bLit) { D.Glow(TR.Center(), TileSize, WithAlpha(FV::Col(255, 220, 120) * 0.5f, 0.6f)); }
				WoodTile(D, TR, Face, false);
				const float AS = FMath::Max(7.f, TileSize * 0.55f);
				if (bLit) { DrawArrowShadowed(D, TR.Center(), AS, Pattern[i], FVPal::WoodDark()); }
				else if (bWrong && WrongDir >= 0) { DrawArrowShadowed(D, TR.Center(), AS, WrongDir, FLinearColor::White); }
				else if (bDone) { DrawArrowShadowed(D, TR.Center(), AS, Pattern[i], FLinearColor::White); }
				else
				{
					const float QS = FMath::Max(8.f, TileSize * 0.42f);
					D.Text(TEXT("?"), FVector2D(TR.Center().X, Snap(TR.Center().Y - D.LineHeight(QS) * 0.5f)), QS, FV::Col(120, 70, 38, 170), EFVAlign::Center, false, true);
				}
				if (bNext) { D.Frame(TR.Inset(-2.f), WithAlpha(FVPal::Gold(), 0.55f + 0.45f * FMath::Sin(Time * 8.f)), 2.f); }
			}

			// the rope with knots under the tiles
			RopeY = Snap(TY + TileSize + (bP ? 34.f : 26.f));
			const float RX0 = Area.X + (bP ? 8.f : 24.f), RX1 = Area.Right() - (bP ? 8.f : 24.f);
			const float Th = bP ? 10.f : 8.f;
			for (float X = RX0; X < RX1; X += 6.f)
			{
				const float W = FMath::Min(6.f, RX1 - X);
				const float Off = Wiggle > 0.f ? FMath::Sin(X * 0.09f + Time * 16.f) * 3.f * Wiggle : 0.f;
				const float Y = Snap(RopeY - Th * 0.5f + Off);
				D.Rect(FFVRect(Snap(X), Y, W, Th), RopeMain());
				D.Rect(FFVRect(Snap(X), Y, W, 2.f), RopeLight());
				D.Rect(FFVRect(Snap(X), Y + Th - 2.f, W, 2.f), RopeDark());
				if (W >= 5.f) { D.Line(FVector2D(X + 1.f, Y + 2.f), FVector2D(X + 4.f, Y + Th - 2.f), RopeDark(), 1.f); }
			}
			for (int32 k = 0; k < 3; ++k)
			{
				D.Line(FVector2D(RX0, RopeY), FVector2D(RX0 - 5.f, RopeY - 3.f + float(k) * 3.f), RopeDark(), 1.f);
				D.Line(FVector2D(RX1, RopeY), FVector2D(RX1 + 5.f, RopeY - 3.f + float(k) * 3.f), RopeDark(), 1.f);
			}
			for (int32 i = 0; i < KnotAge.Num() && i < N; ++i)
			{
				const float Pop = KnotAge[i] < 0.2f ? 1.f + (0.2f - KnotAge[i]) * 2.5f : 1.f;
				DrawKnot(D, KnotPos(i), (bP ? 9.f : 7.f) * Pop);
			}

			// status line
			FString Status;
			FLinearColor StatusC = FVPal::Ink();
			switch (Phase)
			{
			case EPhase::Intro: Status = TEXT("Приготовьтесь…"); break;
			case EPhase::Show: Status = TEXT("Запоминайте…"); StatusC = InkGold(); break;
			case EPhase::Input: Status = FString::Printf(TEXT("Повторите узор: %d / %d"), Entered, N); break;
			case EPhase::Mistake: Status = TEXT("Узел распустился! Смотрите ещё раз"); StatusC = InkBad(); break;
			case EPhase::Success: Status = TEXT("Готово!"); StatusC = InkGood(); break;
			default: break;
			}
			const float StatusY = RopeY + (bP ? 18.f : 12.f);
			if (!Status.IsEmpty()) { D.Text(Status, FVector2D(Area.Center().X, StatusY), FV::Ui::Body() + 0.5f, StatusC, EFVAlign::Center, false, true); }

			// d-pad
			const float PadGap = bP ? 6.f : 4.f;
			const float PadTop = StatusY + D.LineHeight(FV::Ui::Body()) + (bP ? 14.f : 8.f);
			const float PadB = FMath::Clamp(FMath::FloorToFloat((Area.Bottom() - PadTop - 4.f - 2.f * PadGap) / 3.f), 22.f, bP ? 70.f : 32.f);
			const FVector2D PC(Area.Center().X, Snap(Area.Bottom() - 4.f - PadB * 1.5f - PadGap));
			PadR[DirUp] = FFVRect(Snap(PC.X - PadB * 0.5f), Snap(PC.Y - PadB * 1.5f - PadGap), PadB, PadB);
			PadR[DirDown] = FFVRect(Snap(PC.X - PadB * 0.5f), Snap(PC.Y + PadB * 0.5f + PadGap), PadB, PadB);
			PadR[DirLeft] = FFVRect(Snap(PC.X - PadB * 1.5f - PadGap), Snap(PC.Y - PadB * 0.5f), PadB, PadB);
			PadR[DirRight] = FFVRect(Snap(PC.X + PadB * 0.5f + PadGap), Snap(PC.Y - PadB * 0.5f), PadB, PadB);
			const bool bPadOn = Phase == EPhase::Input;
			for (int32 Dr = 0; Dr < 4; ++Dr)
			{
				FLinearColor Face = FMath::Lerp(FV::Col(186, 116, 60), FV::Col(255, 214, 90), PadFlash[Dr]);
				if (!bPadOn && PadFlash[Dr] <= 0.f) { Face = FMath::Lerp(Face, FV::Col(150, 130, 110), 0.35f); }
				const bool bDown = PadFlash[Dr] > 0.6f;
				WoodTile(D, PadR[Dr], Face, bDown);
				DrawArrowShadowed(D, PadR[Dr].Center() + FVector2D(0.f, bDown ? 2.f : 0.f), PadB * 0.5f, Dr, PadFlash[Dr] > 0.f ? FVPal::WoodDark() : FV::Col(255, 240, 210));
				if (bInteractive && bPadOn && D.PressedIn(PadR[Dr])) { Enter(Dr); }
			}
			if (!FV::IsTouchUI() && !bP)
			{
				D.Text(TEXT("W"), FVector2D(PadR[DirUp].Right() + 4.f, PadR[DirUp].Y + 2.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, true);
				D.Text(TEXT("A"), FVector2D(PadR[DirLeft].X - 4.f, PadR[DirLeft].Y - 9.f), 6.5f, FVPal::InkSoft(), EFVAlign::Right, false, true);
				D.Text(TEXT("S"), FVector2D(PadR[DirDown].Right() + 4.f, PadR[DirDown].Bottom() - 10.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, true);
				D.Text(TEXT("D"), FVector2D(PadR[DirRight].Right() + 4.f, PadR[DirRight].Y - 9.f), 6.5f, FVPal::InkSoft(), EFVAlign::Right, false, true);
			}

			// swipes anywhere else in the play area
			if (bInteractive && bPadOn)
			{
				const FFVInput& In = D.Input();
				if (!bSwipe)
				{
					if (D.PressedIn(Area) && In.bMouseValid) { bSwipe = true; SwipeStart = In.Mouse; }
				}
				else
				{
					const FVector2D Dv = In.Mouse - SwipeStart;
					const float Len = float(Dv.Size());
					if (In.bMouseValid) { D.Line(SwipeStart, In.Mouse, WithAlpha(FLinearColor::White, 0.6f), bP ? 4.f : 3.f); }
					const bool bRelease = In.bLmbReleased || !In.bLmbDown;
					if (Len >= (bP ? 40.f : 30.f) || (bRelease && Len >= 18.f))
					{
						bSwipe = false;
						Enter(FMath::Abs(Dv.X) > FMath::Abs(Dv.Y) ? (Dv.X > 0.0 ? DirRight : DirLeft) : (Dv.Y > 0.0 ? DirDown : DirUp));
					}
					else if (bRelease)
					{
						bSwipe = false;
					}
				}
			}
			else
			{
				bSwipe = false;
			}
		}
	};

	// =====================================================================================================
	// LURE WORKSHOP (update 4: a reusable panel) — base x colour x glitter, preview, assembly animation, CraftLure,
	// discovered recipes (x/24). FLureBench is the panel (wrapped by FFVLureWorkshop, FVMiniGames.h): the standalone
	// FLureCraftScreen hosts it as a mini-game, the workshop's «Блёсны» tab embeds it (its card is drawn in the page).
	// =====================================================================================================
	class FLureBench
	{
	public:
		enum class EPhase : uint8 { Pick, Assemble };
		static constexpr int32 TotalLures = FVDB::LureBases * FVDB::LureColors * FVDB::LureGlitters;

		/** Standalone screen: receives the discovery card and shows it with the mini-game card. Empty = the panel shows it. */
		TFunction<void(const FResultCard&, EFVSfx)> OnCrafted;
		bool bFast = false;              // the autotest bot: quicker assembly

		FLureBench(UFVGame* InGame, bool bInEmbedded)
			: Game(InGame), bEmbedded(bInEmbedded)
		{
			const int32 Seed = int32(FPlatformTime::Cycles() & 0x7fffffffu);
			Rng.Initialize(Seed ^ 0x3C6EF372);
			Juice.Rng.Initialize(Seed ^ 0x1B873593);
			Overlay.Rng.Initialize(Seed ^ 0x68E31DA4);
		}

		void Open()
		{
			Phase = EPhase::Pick;
			AssembleT = 0.f;
			Card = FResultCard();
			CardScroll.Reset();
			Scroll.Reset();
			bKeyUsed = false;
			FocusAxis = 0;
			if (const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr) { ChooseDefault(*P); }
		}

		bool IsAssembling() const { return Phase == EPhase::Assemble; }
		bool IsCardShown() const { return Card.bShown; }

		/** bSound: keys / Esc (a card button already clicked). */
		void CloseCard(bool bSound = true)
		{
			if (!Card.bShown) { return; }
			Card.bShown = false;
			if (bSound) { Sfx(EFVSfx::Click); }
		}

		FString Hint() const
		{
			if (FV::IsTouchUI()) { return TEXT("Выберите основу, цвет и блёстки, затем «Собрать»"); }
			return bEmbedded ? TEXT("Клик — выбрать · ↑↓ — ряд · A/D — вариант · Пробел — собрать")
				: TEXT("Клик — выбрать · стрелки — ряд и вариант · Пробел — собрать");
		}

		static int32 CountDiscovered(const UFVSaveGame& P)
		{
			int32 N = 0;
			for (uint8 B = 0; B < FVDB::LureBases; ++B)
				for (uint8 C = 0; C < FVDB::LureColors; ++C)
					for (uint8 G = 0; G < FVDB::LureGlitters; ++G)
						if (P.IsLureDiscovered(FVDB::LureId(B, C, G))) { ++N; }
			return N;
		}

		/** Initial selection: an affordable undiscovered recipe, else any affordable one (cheapest glitter first). */
		void ChooseDefault(const UFVSaveGame& P)
		{
			int32 BestScore = -1;
			for (uint8 B = 0; B < FVDB::LureBases; ++B)
				for (uint8 C = 0; C < FVDB::LureColors; ++C)
					for (uint8 G = 0; G < FVDB::LureGlitters; ++G)
					{
						if (!P.HasIngredients(FVDB::LureInputs(B, C, G))) { continue; }
						const int32 Pref = (P.IsLureDiscovered(FVDB::LureId(B, C, G)) ? 0 : 10) + (2 - G);
						if (Pref > BestScore) { BestScore = Pref; Sel[0] = B; Sel[1] = C; Sel[2] = G; }
					}
		}

		bool CanAssemble() const
		{
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			return P && Phase == EPhase::Pick && !Card.bShown && P->HasIngredients(CurrentInputs());
		}

		void TryAssemble()
		{
			if (Phase != EPhase::Pick || Card.bShown || !Game) { return; }
			if (!CanAssemble())
			{
				Sfx(EFVSfx::Error);
				Game->Toast(TEXT("Не хватает материалов"), FV::Col(255, 150, 130), EFVIcon::Exclaim);
				return;
			}
			Phase = EPhase::Assemble;
			AssembleT = 0.f;
			SfxT = 0.f;
		}

		/** Animations and particles: every frame, also while a card is up. */
		void TickFx(float Dt)
		{
			Dt = FMath::Clamp(Dt, 0.f, 0.1f);
			Time += Dt;
			Juice.Tick(Dt);
			Overlay.Tick(Dt);
			if (!Card.bShown) { return; }
			Card.Age += Dt;
			if (Card.bGood && Card.Age < 1.6f && !CardIconCenter.IsZero())
			{
				CardSparkT -= Dt;
				if (CardSparkT <= 0.f)
				{
					CardSparkT = 0.2f;
					Overlay.Sparkles(CardIconCenter, 3, FV::IsPortrait() ? 34.f : 26.f);
				}
			}
		}

		/** Keys + the assembly timer. bKeys: keyboard control; bArrowsLR: ←/→ (and the stick) change the variant. */
		void TickLogic(float Dt, const FFVInput& In, bool bKeys, bool bArrowsLR)
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { return; }
			Dt = FMath::Clamp(Dt, 0.f, 0.1f);
			if (Card.bShown)
			{
				if (bKeys && Card.Age > 0.35f && ConfirmPressed(In)) { CloseCard(); }
				return;
			}
			if (Phase == EPhase::Pick)
			{
				if (bKeys) { PollKeys(In, bArrowsLR); }
				return;
			}
			// assembling
			AssembleT += Dt;
			SfxT -= Dt;
			if (SfxT <= 0.f)
			{
				SfxT = 0.28f;
				Sfx(Sel[0] == 1 ? EFVSfx::Knot : EFVSfx::Hammer, 0.55f, Rng.FRandRange(0.9f, 1.25f));
				if (!PreviewIconC.IsZero()) { Juice.Sparkles(PreviewIconC, 2, 12.f); }
			}
			if (AssembleT >= AssembleDuration()) { Complete(*P); }
		}

		/** The whole panel in Area (local coordinates; may be inside a sliding page). */
		void Draw(FVDraw& D, const FFVRect& Area, bool bInInteractive, bool bButtonInside)
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { return; }
			BaseT = D.GetTranslate();
			const bool bWasBlocked = D.IsInputBlocked();
			bInteractive = bInInteractive && !Card.bShown && !bWasBlocked;
			if (Card.bShown) { D.SetInputBlocked(true); }   // the card's button must get the clicks
			DrawBody(D, *P, Area, bButtonInside);
			D.SetInputBlocked(bWasBlocked);
			Juice.Draw(D);
			if (Card.bShown)
			{
				FVector2D IC = FVector2D::ZeroVector;
				const int32 Btn = DrawResultCard(D, Area, Card, Time, CardScroll, IC, &Area);
				CardIconCenter = IC - BaseT;
				Overlay.Draw(D);
				if (Btn >= 0) { CloseCard(false); }
			}
		}

		bool DrawAssembleButton(FVDraw& D, const FFVRect& R, bool bInInteractive, float TextSize)
		{
			const float TS = TextSize > 0.f ? TextSize : (FV::IsPortrait() ? FV::Ui::Body() + 1.f : 8.f);
			const bool bBusy = Phase == EPhase::Assemble;
			const bool bOn = bInInteractive && !D.IsInputBlocked() && CanAssemble();
			if (D.Button(R, bBusy ? FString(TEXT("Сборка…")) : FString(TEXT("Собрать")), EFVButton::Primary, bOn, TS))
			{
				TryAssemble();
				return Phase == EPhase::Assemble;
			}
			return false;
		}

	private:
		UFVGame* Game = nullptr;
		bool bEmbedded = false;
		uint8 Sel[3] = { 0, 0, 0 };
		EPhase Phase = EPhase::Pick;
		float Time = 0.f;
		float AssembleT = 0.f;
		float SfxT = 0.f;
		int32 FocusAxis = 0;
		bool bKeyUsed = false;
		bool bInteractive = false;       // Draw: pointer interactions allowed this frame
		FDirInput Dirs;
		FRandomStream Rng;
		FJuice Juice;                    // assembly sparkles (panel-local coordinates)
		FJuice Overlay;                  // card sparkles (above the card)
		FResultCard Card;                // embedded mode: the discovery card inside the panel
		FFVScrollView CardScroll;
		float CardSparkT = 0.f;
		FVector2D CardIconCenter = FVector2D::ZeroVector;
		FVector2D PreviewIconC = FVector2D::ZeroVector;   // panel-local (relative to BaseT)
		FVector2D BaseT = FVector2D::ZeroVector;          // D's translation when Draw started (a sliding page...)
		FFVScrollView Scroll;           // portrait: the column scrolls on short phones

		float AssembleDuration() const { return bFast ? 0.9f : 1.4f; }
		static int32 AxisCount(int32 Axis) { return Axis == 0 ? FVDB::LureBases : (Axis == 1 ? FVDB::LureColors : FVDB::LureGlitters); }
		static FString AxisTitle(int32 Axis) { return Axis == 0 ? TEXT("Основа") : (Axis == 1 ? TEXT("Цвет") : TEXT("Блёстки")); }

		TArray<FFVIngredient> CurrentInputs() const { return FVDB::LureInputs(Sel[0], Sel[1], Sel[2]); }

		/** Ingredients a single option adds (difference against another value of the same axis / no glitter). */
		TArray<FFVIngredient> OptionInputs(int32 Axis, uint8 V) const
		{
			uint8 A[3] = { Sel[0], Sel[1], Sel[2] };
			A[Axis] = V;
			TArray<FFVIngredient> Out;
			if (Axis == 2 && V == 0) { return Out; }
			uint8 B[3] = { A[0], A[1], A[2] };
			B[Axis] = Axis == 2 ? uint8(0) : uint8((int32(V) + 1) % AxisCount(Axis));
			const TArray<FFVIngredient> With = FVDB::LureInputs(A[0], A[1], A[2]);
			const TArray<FFVIngredient> Without = FVDB::LureInputs(B[0], B[1], B[2]);
			for (const FFVIngredient& I : With)
			{
				int32 Other = 0;
				for (const FFVIngredient& J : Without) { if (J.ItemId == I.ItemId) { Other += J.Count; } }
				if (I.Count - Other > 0)
				{
					FFVIngredient& O = Out.AddDefaulted_GetRef();
					O.ItemId = I.ItemId;
					O.Count = I.Count - Other;
				}
			}
			return Out;
		}

		void Select(int32 Axis, int32 V)
		{
			if (Phase != EPhase::Pick || Axis < 0 || Axis > 2) { return; }
			const uint8 NV = uint8(FMath::Clamp(V, 0, AxisCount(Axis) - 1));
			if (Sel[Axis] != NV) { Sfx(EFVSfx::Click, 0.8f, 1.f + 0.05f * float(NV)); }
			Sel[Axis] = NV;
			FocusAxis = Axis;
		}

		void PollKeys(const FFVInput& In, bool bArrowsLR)
		{
			int32 Dir = -1;
			if (bArrowsLR) { Dir = Dirs.Poll(In); }
			else if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { Dir = DirUp; }
			else if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { Dir = DirDown; }
			else if (In.WasPressed(EKeys::A)) { Dir = DirLeft; }
			else if (In.WasPressed(EKeys::D)) { Dir = DirRight; }
			if (Dir == DirUp) { FocusAxis = (FocusAxis + 2) % 3; bKeyUsed = true; Sfx(EFVSfx::Hover, 0.6f); }
			else if (Dir == DirDown) { FocusAxis = (FocusAxis + 1) % 3; bKeyUsed = true; Sfx(EFVSfx::Hover, 0.6f); }
			else if (Dir == DirLeft || Dir == DirRight)
			{
				const int32 N = AxisCount(FocusAxis);
				bKeyUsed = true;
				Select(FocusAxis, (int32(Sel[FocusAxis]) + (Dir == DirRight ? 1 : N - 1)) % N);
			}
			if (Time > 0.2f && ConfirmPressed(In)) { TryAssemble(); }
		}

		void Complete(UFVSaveGame& P)
		{
			bool bNew = false;
			const EFVBuyResult R = P.CraftLure(Sel[0], Sel[1], Sel[2], bNew);
			Phase = EPhase::Pick;
			if (R != EFVBuyResult::Ok)
			{
				Sfx(EFVSfx::Error);
				if (Game) { Game->Toast(UFVSaveGame::BuyResultText(R), FV::Col(255, 150, 130), EFVIcon::Exclaim); }
				return;
			}
			if (Game) { Game->NotifyProfileChanged(); }
			const FName Id = FVDB::LureId(Sel[0], Sel[1], Sel[2]);
			const FFVBaitDef* B = FVDB::FindBait(Id);
			FResultCard C;
			C.bLureIcon = true;
			C.LureB = Sel[0];
			C.LureC = Sel[1];
			C.LureG = Sel[2];
			C.Title = B ? B->Name : FVDB::ItemName(Id);
			if (bNew) { C.Badge = TEXT("Новый рецепт!"); }
			C.Sub = FString::Printf(TEXT("+%d шт. в наживке"), FVDB::LurePackSize());
			if (B)
			{
				C.AddLine(TEXT("Лучше всего клюют:"), FVPal::InkSoft());
				C.AddLine(FavoritesText(P, *B), FVPal::Ink(), true);
			}
			C.AddLine(FString::Printf(TEXT("Открыто рецептов: %d / %d"), CountDiscovered(P), TotalLures), FVPal::InkSoft());
			if (bEmbedded) { C.Primary = TEXT("Отлично"); }
			else { C.Secondary = TEXT("Ещё"); }
			if (!PreviewIconC.IsZero()) { Juice.Sparkles(PreviewIconC, bNew ? 16 : 8, 20.f); }
			const EFVSfx Sound = bNew ? EFVSfx::Achievement : EFVSfx::Buy;
			if (OnCrafted) { OnCrafted(C, Sound); return; }
			Card = C;
			Card.bShown = true;
			Card.Age = 0.f;
			CardSparkT = 0.25f;
			CardScroll.Reset();
			Sfx(Sound, 0.9f);
		}

		static FString FavoritesText(const UFVSaveGame& P, const FFVBaitDef& B)
		{
			TArray<FString> Names;
			for (const FFVFishDef* F : FVDB::LureFavorites(B, 3))
			{
				if (!F) { continue; }
				const FFVFishRecord* Rec = P.GetRecord(F->Id);
				Names.Add(Rec && Rec->Caught > 0 ? F->Name : FString(TEXT("???")));
			}
			return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("???"));
		}

		// ------------------------------------------------------------------ drawing
		void DrawBody(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, bool bButtonInside)
		{
			const bool bP = FV::IsPortrait();
			FFVRect Pickers, Preview, Needs, Grid, Btn;
			bool bScroll = false;
			bool bHasBtn = false;
			if (bP)
			{
				// Finger-sized sections; when the phone is too short for all of them (with the recipe grid), the whole
				// column scrolls smoothly instead of squeezing the pickers or dropping the grid.
				const float PreviewH = 92.f, NeedsH = 26.f, Gap = 6.f, MinSec = 80.f;
				const float BtnRowH = bButtonInside ? FV::Ui::ButtonH() + Gap : 0.f;
				const float GridH = 16.f + 2.f * 27.f;
				float SecH = (Area.H - PreviewH - NeedsH - BtnRowH - GridH - Gap * 3.f) / 3.f;
				float ContentH = Area.H;
				if (SecH < MinSec)
				{
					SecH = MinSec;
					ContentH = PreviewH + NeedsH + BtnRowH + GridH + Gap * 3.f + SecH * 3.f + 4.f;
				}
				Scroll.Update(D, Area, ContentH);
				bScroll = Scroll.CanScroll();
				if (bScroll)
				{
					D.PushClip(FFVRect(Area.X - 2.f, Area.Y, Area.W + 4.f, Area.H));
					D.PushTranslate(FVector2D(0.f, -Scroll.Offset()));
				}
				Preview = FFVRect(Area.X, Area.Y, Area.W, PreviewH);
				Needs = FFVRect(Area.X, Preview.Bottom() + Gap, Area.W, NeedsH);
				float Y = Needs.Bottom() + Gap;
				if (bButtonInside)
				{
					Btn = FFVRect(Area.X, Y, Area.W, FV::Ui::ButtonH());
					bHasBtn = true;
					Y = Btn.Bottom() + Gap;
				}
				Pickers = FFVRect(Area.X, Y, Area.W, SecH * 3.f);
				Grid = FFVRect(Area.X, Pickers.Bottom() + Gap, Area.W, GridH);
			}
			else
			{
				Pickers = FFVRect(Area.X, Area.Y, 336.f, Area.H);
				const float RX = Area.X + 346.f, RW = Area.Right() - RX;
				const float PreviewH = bButtonInside ? FMath::Clamp(Area.H - 124.f, 84.f, 116.f) : 116.f;
				Preview = FFVRect(RX, Area.Y, RW, PreviewH);
				Needs = FFVRect(RX, Preview.Bottom() + 6.f, RW, 22.f);
				float GridY = Needs.Bottom() + 8.f;
				if (bButtonInside)
				{
					Btn = FFVRect(RX, Needs.Bottom() + 6.f, RW, 20.f);
					bHasBtn = true;
					GridY = Btn.Bottom() + 8.f;
				}
				Grid = FFVRect(RX, GridY, RW, Area.Bottom() - GridY);
				D.Rect(FFVRect(RX - 6.f, Area.Y + 4.f, 1.f, Area.H - 8.f), FVPal::ParchDark());
			}
			DrawPreview(D, P, Preview);
			DrawNeeds(D, P, Needs);
			if (bHasBtn)
			{
				DrawAssembleButton(D, Btn, bInteractive, 0.f);
				if (!FV::IsTouchUI() && D.Hover(Btn)) { D.Tooltip(TEXT("Собрать приманку"), Hint()); }
			}
			DrawPickers(D, P, Pickers);
			DrawGrid(D, P, Grid);
			if (bScroll)
			{
				D.PopTranslate();
				D.PopClip();
				Scroll.DrawBar(D, Area);
				Scroll.DrawFades(D, Area, FVPal::Parchment());
			}
		}

		void DrawIngredientRow(FVDraw& D, const UFVSaveGame& P, const TArray<FFVIngredient>& Ins, const FVector2D& Center, float IconS, float TextS)
		{
			if (Ins.Num() == 0)
			{
				D.Text(TEXT("—"), FVector2D(Center.X, Center.Y - D.LineHeight(TextS) * 0.5f), TextS, FVPal::InkSoft(), EFVAlign::Center, false, true);
				return;
			}
			TArray<FString> Texts;
			float TotalW = 0.f;
			for (const FFVIngredient& I : Ins)
			{
				const FString T = FString::Printf(TEXT("%d/%d"), P.CountIngredient(I.ItemId), I.Count);
				TotalW += IconS + 1.f + D.MeasureText(T, TextS).X + 5.f;
				Texts.Add(T);
			}
			float X = Snap(Center.X - (TotalW - 5.f) * 0.5f);
			for (int32 i = 0; i < Ins.Num(); ++i)
			{
				const bool bHave = P.CountIngredient(Ins[i].ItemId) >= Ins[i].Count;
				const FFVRect IB(X, Snap(Center.Y - IconS * 0.5f), IconS, IconS);
				if (const FFVSprite* S = ItemIconPtr(Ins[i].ItemId)) { FVUI::SpriteFit(D, *S, IB, FLinearColor::White, 2.f); }
				if (!FV::IsTouchUI() && D.Hover(IB)) { D.Tooltip(FVDB::ItemName(Ins[i].ItemId), FString::Printf(TEXT("Есть %d, нужно %d"), P.CountIngredient(Ins[i].ItemId), Ins[i].Count)); }
				X += IconS + 1.f;
				D.Text(Texts[i], FVector2D(X, Snap(Center.Y - D.LineHeight(TextS) * 0.5f)), TextS, bHave ? InkGood() : InkBad(), EFVAlign::Left, false, true);
				X += D.MeasureText(Texts[i], TextS).X + 5.f;
			}
		}

		void DrawPickers(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			UFVSprites* Sp = UFVSprites::Get();
			if (!Sp) { return; }
			const bool bP = FV::IsPortrait();
			const float SecH = R.H / 3.f;
			const float HeadS = bP ? FV::Ui::Body() : 8.f;
			const float SmallS = FV::Ui::Small();
			const float HeadH = bP ? 16.f : 13.f;
			const float ColW = R.W / 4.f;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float Y = R.Y + float(Axis) * SecH;
				const float Cell = FMath::Clamp(SecH - HeadH - (bP ? 30.f : 24.f), 24.f, bP ? 48.f : 32.f);
				const bool bFocus = bKeyUsed && FocusAxis == Axis;
				if (bFocus) { DrawArrow(D, FVector2D(R.X + 4.f + FMath::Sin(Time * 8.f), Y + HeadH * 0.45f), 7.f, DirRight, FVPal::Ink()); }
				D.Text(AxisTitle(Axis), FVector2D(R.X + (bFocus ? 11.f : 4.f), Y), HeadS, FVPal::Ink(), EFVAlign::Left, false, true);
				if (Axis > 0) { D.Rect(FFVRect(R.X + 4.f, Snap(Y - 3.f), R.W - 8.f, 1.f), FVPal::ParchDark()); }
				for (int32 V = 0; V < AxisCount(Axis); ++V)
				{
					uint8 A[3] = { Sel[0], Sel[1], Sel[2] };
					A[Axis] = uint8(V);
					const float CX = R.X + ColW * (float(V) + 0.5f);
					const FFVRect CR(Snap(CX - Cell * 0.5f), Snap(Y + HeadH), Cell, Cell);
					const bool bSel = Sel[Axis] == V;
					if (D.Slot(CR, bSel, true) && bInteractive && Phase == EPhase::Pick) { Select(Axis, V); }
					const FFVSprite& LS = Sp->LureIcon(A[0], A[1], A[2]);
					const float Bob = bSel ? FMath::Sin(Time * 4.f) * 1.f : 0.f;
					FVUI::SpriteFit(D, LS, CR.Inset(3.f).Offset(0.f, Snap(Bob)), FLinearColor::White, bP ? 3.f : 2.f);
					const FString PartName = FVDB::LurePartName(Axis, uint8(V));
					D.Text(D.Ellipsize(PartName, ColW - 4.f, SmallS, bSel), FVector2D(Snap(CX), CR.Bottom() + 2.f), SmallS, bSel ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Center, false, bSel);
					const float IngY = CR.Bottom() + 3.f + D.LineHeight(SmallS) + (bP ? 7.f : 5.f);
					DrawIngredientRow(D, P, OptionInputs(Axis, uint8(V)), FVector2D(CX, IngY), bP ? 12.f : 8.f, SmallS);
				}
			}
		}

		void DrawPreview(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			UFVSprites* Sp = UFVSprites::Get();
			if (!Sp) { return; }
			const bool bP = FV::IsPortrait();
			D.Panel(R, EFVPanel::Parchment);
			const FName Id = FVDB::LureId(Sel[0], Sel[1], Sel[2]);
			const FFVBaitDef* B = FVDB::FindBait(Id);
			const bool bKnown = P.IsLureDiscovered(Id);
			// the display: a little water box with the lure hanging on a line
			const float Box = R.H - 12.f;
			const FFVRect BoxR(R.X + 6.f, R.Y + 6.f, Box, Box);
			D.Rect(BoxR.Inset(-1.f), FVPal::WoodDark());
			D.GradientV(BoxR, FV::Col(88, 150, 196), FV::Col(34, 70, 120), 8);
			for (int32 k = 0; k < 3; ++k)
			{
				const float WY = BoxR.Y + 8.f + float(k) * Box * 0.3f + FMath::Sin(Time * 1.5f + float(k)) * 1.5f;
				D.Rect(FFVRect(Snap(BoxR.X + 6.f + float(k) * 7.f), Snap(WY), Snap(Box * 0.35f), 1.f), FV::Col(150, 200, 230, 120));
			}
			const FFVSprite& LS = Sp->LureIcon(Sel[0], Sel[1], Sel[2]);
			float Rot = FMath::Sin(Time * 1.7f) * 8.f;
			float Sc = FVUI::FitScale(LS, Box * 0.62f, Box * 0.62f, 5.f);
			const FVector2D LC(BoxR.Center().X, Snap(BoxR.Center().Y + 4.f + FMath::Sin(Time * 2.2f) * 3.f));
			PreviewIconC = LC + D.GetTranslate() - BaseT;   // the juice is drawn at the panel's base translation
			if (Phase == EPhase::Assemble)
			{
				Rot = FMath::Sin(AssembleT * 20.f) * 14.f;
				Sc *= 1.f + 0.08f * FMath::Sin(AssembleT * 12.f);
			}
			D.Line(FVector2D(LC.X, BoxR.Y), FVector2D(LC.X, LC.Y - LS.H * Sc * 0.45f), FV::Col(235, 235, 235, 170), 1.f);
			D.SpriteCentered(LS, LC, Sc, FLinearColor::White, false, Rot);
			// periodic glint
			const float GT = FMath::Frac(Time / 2.6f);
			if (GT < 0.25f) { D.Glow(LC + FVector2D((GT / 0.25f - 0.5f) * LS.W * Sc, -2.f), 7.f, WithAlpha(FLinearColor::White * 0.7f, 0.8f)); }
			if (Phase == EPhase::Assemble)
			{
				if (Sel[0] == 1)
				{
					// thread wrapping the fly
					const float WR = Box * 0.3f;
					D.Ring(LC, FMath::Max(2.f, WR * FMath::Abs(FMath::Cos(AssembleT * 9.f))), WR * 0.8f, FV::Col(245, 236, 200, 220), 1.f);
				}
				else
				{
					// polishing cloth + shine
					const FVector2D Cl = LC + FVector2D(FMath::Cos(AssembleT * 10.f) * Box * 0.18f, FMath::Sin(AssembleT * 10.f) * Box * 0.1f);
					D.Rect(FFVRect(Snap(Cl.X - 4.f), Snap(Cl.Y - 3.f), 8.f, 6.f), FV::Col(240, 236, 226));
					D.Glow(Cl, 9.f, WithAlpha(FLinearColor::White * 0.6f, 0.7f));
				}
				D.Bar(FFVRect(BoxR.X + 4.f, BoxR.Bottom() - 8.f, BoxR.W - 8.f, 4.f), Clamp01(AssembleT / AssembleDuration()), FVPal::Gold(), FV::Col(40, 26, 18), false);
			}
			// texts
			const float TX = BoxR.Right() + 8.f, TW = R.Right() - TX - 6.f;
			float Y = R.Y + 6.f;
			const float NameS = bP ? FV::Ui::Body() + 0.5f : 8.5f;
			const FString Name = B ? B->Name : FVDB::ItemName(Id);
			Y += D.TextWrapped(Name, FVector2D(TX, Y), TW, NameS, FVPal::Ink(), true) + 2.f;
			const float SmallS = FV::Ui::Small();
			D.Text(bKnown ? FString(TEXT("Рецепт известен")) : FString(TEXT("Новый рецепт!")), FVector2D(TX, Y), SmallS, bKnown ? InkGood() : InkGold(), EFVAlign::Left, false, true);
			Y += D.LineHeight(SmallS) + 2.f;
			if (B)
			{
				const FString Stats = FString::Printf(TEXT("Редкие +%d%% · размер +%d%% · %d шт."), FMath::RoundToInt(B->RareBonus * 100.f), FMath::RoundToInt(B->SizeBonus * 100.f), FVDB::LurePackSize());
				Y += D.TextWrapped(Stats, FVector2D(TX, Y), TW, SmallS, FVPal::InkSoft()) + 2.f;
				const FString Fav = bKnown ? FString::Printf(TEXT("Клюют: %s"), *FavoritesText(P, *B)) : FString(TEXT("Кто клюёт — узнаете после сборки"));
				if (Y < R.Bottom() - D.LineHeight(SmallS)) { D.TextWrapped(Fav, FVector2D(TX, Y), TW, SmallS, FVPal::Ink()); }
			}
		}

		void DrawNeeds(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			const bool bP = FV::IsPortrait();
			const float S = FV::Ui::Small();
			D.Rect(R, FV::Col(240, 206, 148));
			D.Frame(R, FVPal::ParchDark(), 1.f);
			const FString Label = TEXT("Нужно:");
			D.Text(Label, FVector2D(R.X + 6.f, Snap(R.Center().Y - D.LineHeight(S) * 0.5f)), S, FVPal::Ink(), EFVAlign::Left, false, true);
			const float LW = D.MeasureText(Label, S).X + 12.f;
			const float RowCX = R.X + LW + (R.W - LW) * 0.5f;
			DrawIngredientRow(D, P, CurrentInputs(), FVector2D(RowCX, R.Center().Y), bP ? 14.f : 10.f, S + (bP ? 0.5f : 0.f));
		}

		void DrawGrid(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			UFVSprites* Sp = UFVSprites::Get();
			if (!Sp) { return; }
			const bool bP = FV::IsPortrait();
			const float S = FV::Ui::Small();
			D.Text(FString::Printf(TEXT("Открыто рецептов: %d / %d"), CountDiscovered(P), TotalLures), FVector2D(R.X + 2.f, R.Y), S, FVPal::Ink(), EFVAlign::Left, false, true);
			const float Top = R.Y + D.LineHeight(S) + 3.f;
			if (R.Bottom() - Top < 14.f) { return; }
			constexpr int32 PerRow = FVDB::LureColors * FVDB::LureGlitters;   // one row per base
			const float Gap = 3.f;
			const float IconS = FMath::Clamp(FMath::FloorToFloat(FMath::Min((R.W - float(PerRow - 1) * Gap) / float(PerRow), (R.Bottom() - Top - Gap) / 2.f)), 10.f, bP ? 26.f : 18.f);
			const float RowW = float(PerRow) * IconS + float(PerRow - 1) * Gap;
			const float X0 = Snap(R.X + (R.W - RowW) * 0.5f);
			for (uint8 B = 0; B < FVDB::LureBases; ++B)
			{
				for (uint8 C = 0; C < FVDB::LureColors; ++C)
				{
					for (uint8 G = 0; G < FVDB::LureGlitters; ++G)
					{
						const int32 Col = int32(C) * FVDB::LureGlitters + int32(G);
						const FFVRect Cell(X0 + float(Col) * (IconS + Gap), Snap(Top + float(B) * (IconS + Gap)), IconS, IconS);
						const FName Id = FVDB::LureId(B, C, G);
						const bool bKnown = P.IsLureDiscovered(Id);
						const bool bCur = Sel[0] == B && Sel[1] == C && Sel[2] == G;
						D.Rect(Cell, bKnown ? FV::Col(255, 236, 196) : FVPal::ParchDark());
						FVUI::SpriteFit(D, Sp->LureIcon(B, C, G), Cell.Inset(1.f), bKnown ? FLinearColor::White : FV::Col(40, 24, 14, 110), 2.f);
						if (bCur) { D.Frame(Cell.Inset(-1.f), FVPal::Red(), 1.f); }
						if (bInteractive && Phase == EPhase::Pick && D.Clicked(Cell))
						{
							Sel[0] = B; Sel[1] = C; Sel[2] = G;
							Sfx(EFVSfx::Click, 0.8f);
						}
						else if (!FV::IsTouchUI() && D.Hover(Cell))
						{
							const FFVBaitDef* BD = FVDB::FindBait(Id);
							D.Tooltip(BD ? BD->Name : FVDB::ItemName(Id), bKnown ? FString(TEXT("Рецепт открыт")) : FString(TEXT("Ещё не собирали")));
						}
					}
				}
			}
		}
	};

	/** The standalone lure workshop mini-game (EFVScreen::LureCraft): hosts the panel; the autotest bot plays it. */
	class FLureCraftScreen final : public FMiniGame
	{
	public:
		explicit FLureCraftScreen(UFVGame* InGame)
			: FMiniGame(InGame, FName(TEXT("st_workshop"))), Panel(InGame, false)
		{
			FLureBench& B = Panel.Core();
			B.bFast = bBot;
			B.OnCrafted = [this](const FResultCard& C, EFVSfx Sound) { ShowCard(C, Sound); };
		}

		virtual void OnOpen() override
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { OpenFailed(TEXT("Профиль не загружен."), EFVIcon::Hammer); return; }
			Panel.Open();
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			if (Game && !bLeaving) { Panel.Core().TickFx(Dt); }   // the panel's sparkles keep moving under the card
			FMiniGame::Tick(Dt, In);
		}

	protected:
		FFVLureWorkshop Panel;
		float BotT = 0.f;
		bool bBotChosen = false;
		bool bBotTried = false;

		virtual FString Title() const override { return TEXT("Мастерская блёсен"); }
		virtual FString ShotSlug() const override { return TEXT("lures"); }
		virtual bool QuitNeedsConfirm() const override { return Panel.IsAssembling(); }
		virtual FString QuitTitle() const override { return TEXT("Прервать сборку?"); }
		virtual FString QuitBody() const override { return TEXT("Материалы останутся целы."); }
		virtual FString Hint() const override { return Panel.Hint(); }

		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { return; }
			FLureBench& B = Panel.Core();
			B.TickLogic(Dt, In, true, true);
			if (!bBot || B.IsAssembling() || Card.bShown) { return; }
			BotT += Dt;
			if (BotT > 0.35f && !bBotChosen) { bBotChosen = true; B.ChooseDefault(*P); RequestPlayShot(); }
			if (BotT > 0.9f)
			{
				// one attempt: a refused craft (no workshop level...) must not loop forever
				if (B.CanAssemble() && !bBotTried) { bBotTried = true; B.TryAssemble(); }
				else { Leave(); return; }
			}
		}

		virtual void OnCardButton(int32 Index) override
		{
			if (Index == 1)
			{
				Card.bShown = false;
				Sfx(EFVSfx::Click);
				return;
			}
			Leave();
		}

		virtual void DrawActions(FVDraw& D, const FFVRect& Area) override
		{
			Panel.DrawAssembleButton(D, Area, bInteractive, FV::IsPortrait() ? FV::Ui::Body() + 1.f : 8.f);
		}

		virtual void DrawGame(FVDraw& D, const FFVRect& Area) override
		{
			Panel.Draw(D, Area, bInteractive, false);
		}
	};

	// =====================================================================================================
	// DIGGING (context = "<location>:<spot>"): timed soil grid, 1-2 hits per cell, worms must be tapped in 1.2 s
	// =====================================================================================================
	class FDiggingScreen final : public FMiniGame
	{
	public:
		explicit FDiggingScreen(UFVGame* InGame) : FMiniGame(InGame, NAME_None) {}

		virtual void OnOpen() override
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { OpenFailed(TEXT("Профиль не загружен."), EFVIcon::Shovel); return; }
			const FString Ctx = Game->GetScreenContext().ToString();
			int32 Colon = INDEX_NONE;
			if (!Game->GetScreenContext().IsNone() && Ctx.FindLastChar(TEXT(':'), Colon))
			{
				Loc = FName(*Ctx.Left(Colon));
				Spot = FMath::Max(0, FCString::Atoi(*Ctx.Mid(Colon + 1)));
			}
			else
			{
				Loc = Game->GetScene().GetLocationId();
				Spot = 0;
			}
			if (P->ShovelLevel < 1)
			{
				OpenFailed(TEXT("Нужна лопата. Её продаёт Дед Митрич в лавке рыбака."), EFVIcon::Shovel, TEXT("Нечем копать"));
				return;
			}
			if (P->IsDugToday(Loc, Spot))
			{
				OpenFailed(TEXT("Сегодня здесь уже копали. Приходите завтра!"), EFVIcon::Shovel, TEXT("Уже перекопано"));
				return;
			}
			ShovelLevel = P->ShovelLevel;
			HitsNeeded = ShovelLevel <= 2 ? 2 : 1;
			TimeTotal = FMath::Max(3.f, P->DigSeconds());
			TimeLeft = TimeTotal;
			Cells.SetNum(NumCells);
			bReady = true;
			Phase = EPhase::Intro;
			PhaseT = 0.f;
			Banner(TEXT("Копайте!"), FVPal::Gold());
		}

	protected:
		enum class EPhase : uint8 { Intro, Play, TimeUp, Over };
		static constexpr int32 NumCells = 20;
		static constexpr float WormWindow = 1.2f;
		static constexpr float FlyTime = 0.55f;
		static constexpr float EmptyHolePenalty = 0.5f;   // update 5 (§10.1): seconds lost per tap on an empty dug hole

		struct FCell
		{
			int32 Hits = 0;
			bool bDug = false;
			FName Loot;
			int32 Count = 0;
			uint8 WormState = 0;      // 0 none, 1 peeking, 2 caught, 3 escaped
			float WormAge = 0.f;
			float HitT = 99.f;
			float DugT = 99.f;
		};
		struct FFly
		{
			FName Id;
			FVector2D From = FVector2D::ZeroVector;
			float Age = 0.f;
		};

		bool bReady = false;
		FName Loc;
		int32 Spot = 0;
		int32 ShovelLevel = 1;
		int32 HitsNeeded = 2;
		float TimeTotal = 8.f;
		float TimeLeft = 8.f;
		EPhase Phase = EPhase::Intro;
		float PhaseT = 0.f;
		TArray<FCell> Cells;
		TMap<FName, int32> Found;
		int32 WormsLost = 0;
		int32 Cursor = 0;
		bool bKeyUsed = false;
		FDirInput Dirs;
		TArray<FFly> Flies;
		float BotT = 0.f;
		float ShovelT = 99.f;
		int32 ShovelCell = -1;
		int32 LastSec = -1;
		bool bFinished = false;
		int32 Penalties = 0;             // taps on empty holes (update 5)
		float PenaltyFlash = 0.f;        // red flash + jolt of the timer after a penalty, 1 -> 0
		// layout of the last frame
		int32 Cols = 5;
		TArray<FFVRect> CellR;
		float CellS = 48.f;
		FVector2D TallyTarget = FVector2D::ZeroVector;

		virtual FString Title() const override { return TEXT("Раскопки"); }
		virtual FString ShotSlug() const override { return TEXT("digging"); }
		virtual bool QuitNeedsConfirm() const override { return bReady && Phase != EPhase::Over; }
		virtual FString QuitTitle() const override { return TEXT("Бросить раскопки?"); }
		virtual FString QuitBody() const override { return TEXT("Находки пропадут, а здесь можно будет покопать снова."); }
		virtual FString Hint() const override
		{
			if (!bReady) { return FString(); }
			const FString Controls = FV::IsTouchUI() ? TEXT("Касайтесь земли, чтобы копать. Ловите червей, пока не уползли!")
				: TEXT("Клик — копать и ловить червей · стрелки + Пробел — то же с клавиатуры");
			const TCHAR* Warn = TEXT("Не тыкайте в пустые ямки — каждая отнимает полсекунды");   // update 5 (§10.1)
			// portrait: the hint height shapes the layout, so one constant text (wrapped); landscape: one ellipsized
			// line, so the controls until the first hole is open, then the warning
			if (FV::IsPortrait()) { return FString::Printf(TEXT("%s%s %s."), *Controls, Controls.EndsWith(TEXT("!")) ? TEXT("") : TEXT("."), Warn); }
			return AnyDug() ? FString(Warn) : Controls;
		}

		FVector2D CellCenter(int32 i) const { return CellR.IsValidIndex(i) ? CellR[i].Center() : LastArea.Center(); }
		bool AnyWormPeeking() const { for (const FCell& C : Cells) { if (C.WormState == 1) { return true; } } return false; }
		bool AllDug() const { for (const FCell& C : Cells) { if (!C.bDug) { return false; } } return true; }
		bool AnyDug() const { for (const FCell& C : Cells) { if (C.bDug) { return true; } } return false; }

		void MoveCursor(int32 Dir)
		{
			const int32 NC = FMath::Max(1, Cols);
			const int32 Rows = (Cells.Num() + NC - 1) / NC;
			int32 Cx = Cursor % NC, Cy = Cursor / NC;
			if (Dir == DirLeft) { Cx = (Cx + NC - 1) % NC; }
			else if (Dir == DirRight) { Cx = (Cx + 1) % NC; }
			else if (Dir == DirUp) { Cy = (Cy + Rows - 1) % Rows; }
			else { Cy = (Cy + 1) % Rows; }
			Cursor = FMath::Clamp(Cy * NC + Cx, 0, Cells.Num() - 1);
			Sfx(EFVSfx::Hover, 0.5f);
		}

		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			if (!bReady || Cells.Num() == 0) { return; }
			PhaseT += Dt;
			ShovelT += Dt;
			PenaltyFlash = FMath::Max(0.f, PenaltyFlash - Dt * 2.5f);
			for (FCell& C : Cells) { C.HitT += Dt; C.DugT += Dt; }
			for (int32 i = Flies.Num() - 1; i >= 0; --i)
			{
				Flies[i].Age += Dt;
				if (Flies[i].Age >= FlyTime)
				{
					if (!TallyTarget.IsZero()) { Juice.Sparkles(TallyTarget, 3, 6.f); }
					Flies.RemoveAt(i);
				}
			}
			const int32 Dir = Dirs.Poll(In);
			if (Dir >= 0 && Phase != EPhase::Over) { bKeyUsed = true; MoveCursor(Dir); }
			const bool bKey = Time > 0.2f && ConfirmPressed(In);
			FVMiniGameDebug().bDigPlaying = false;   // update 5: UpdateDebug sets it again while the play phase runs
			switch (Phase)
			{
			case EPhase::Intro:
				if (PhaseT >= (bBot ? 0.4f : 0.8f)) { Phase = EPhase::Play; PhaseT = 0.f; }
				break;
			case EPhase::Play:
			{
				TimeLeft -= Dt;
				const int32 Sec = FMath::CeilToInt(TimeLeft);
				if (Sec != LastSec && Sec <= 3 && Sec > 0) { Sfx(EFVSfx::Click, 0.7f, 1.6f); }
				LastSec = Sec;
				if (bKey) { Hit(Cursor); }
				if (bBot) { BotStep(Dt); }
				if (PhaseT > 1.5f) { RequestPlayShot(); }
				UpdateDebug();
				if (TimeLeft <= 0.f)
				{
					TimeLeft = 0.f;
					Phase = EPhase::TimeUp;
					PhaseT = 0.f;
					Banner(TEXT("Время вышло!"), FV::Col(255, 150, 120));
					Sfx(EFVSfx::Close, 0.8f);
				}
				else if (AllDug() && !AnyWormPeeking())
				{
					Phase = EPhase::TimeUp;
					PhaseT = 0.f;
					Banner(TEXT("Всё перекопано!"), FV::Col(150, 230, 110));
				}
				break;
			}
			case EPhase::TimeUp:
				if (bKey) { Hit(Cursor); }
				if (bBot) { BotStep(Dt); }
				if (AnyWormPeeking()) { PhaseT = 0.f; }
				else if (PhaseT >= 0.7f) { Finish(); return; }
				break;
			default:
				break;
			}
			TickWorms(Dt);
		}

		void TickWorms(float Dt)
		{
			const bool bP = FV::IsPortrait();
			for (int32 i = 0; i < Cells.Num(); ++i)
			{
				FCell& C = Cells[i];
				if (C.WormState == 0 || C.WormState == 2) { continue; }
				C.WormAge += Dt;
				if (C.WormState == 1 && C.WormAge >= WormWindow)
				{
					C.WormState = 3;
					++WormsLost;
					Juice.Text(TEXT("Уполз!"), CellCenter(i) - FVector2D(0.f, CellS * 0.3f), FV::Col(220, 210, 200), bP ? 11.f : 8.f);
					Sfx(EFVSfx::Escape, 0.35f, 1.6f);
				}
			}
		}

		void BotStep(float Dt)
		{
			BotT += Dt;
			if (BotT < 0.1f) { return; }
			BotT = 0.f;
			for (int32 i = 0; i < Cells.Num(); ++i)
			{
				if (Cells[i].WormState == 1 && Cells[i].WormAge > 0.12f) { Hit(i); return; }
			}
			if (Phase != EPhase::Play) { return; }
			for (int32 i = 0; i < Cells.Num(); ++i)
			{
				if (!Cells[i].bDug) { Hit(i); return; }
			}
		}

		void Hit(int32 i)
		{
			if (!Cells.IsValidIndex(i) || Phase == EPhase::Over || Phase == EPhase::Intro) { return; }
			Cursor = i;
			FCell& C = Cells[i];
			if (C.WormState == 1) { TakeWorm(i); return; }
			if (Phase == EPhase::Play && C.bDug) { PokeEmptyHole(i); return; }
			if (Phase != EPhase::Play || C.bDug) { return; }
			const bool bP = FV::IsPortrait();
			++C.Hits;
			C.HitT = 0.f;
			ShovelT = 0.f;
			ShovelCell = i;
			const FVector2D Ctr = CellCenter(i);
			Sfx(EFVSfx::Dig, 0.85f, Rng.FRandRange(0.9f, 1.1f));
			Juice.Burst(Ctr, FV::Col(112, 74, 44), 7, 70.f, 260.f, bP ? 3.5f : 2.5f, 0.5f);
			Juice.Kick(0.35f);
			if (C.Hits >= HitsNeeded) { Reveal(i); }
		}

		/** Update 5 (§10.1): a tap / click / Space on an already dug hole without a peeking worm costs half a second.
		    Not while the escaping worm is still sliding back (the tap was meant for it) or in the very frames that opened
		    the hole (one press counted twice). The bot never pokes empty holes. */
		void PokeEmptyHole(int32 i)
		{
			FCell& C = Cells[i];
			if (C.DugT < 0.1f || (C.WormState == 3 && C.WormAge < WormWindow + 0.3f)) { return; }
			const float Before = TimeLeft;
			TimeLeft = FMath::Max(0.f, TimeLeft - EmptyHolePenalty);
			++Penalties;
			PenaltyFlash = 1.f;
			FVMiniGameDebug().DigLastPenalty = Before - TimeLeft;
			C.HitT = 0.f;   // the cell bumps
			const bool bP = FV::IsPortrait();
			// from the middle of the hole: the loot texts of that cell rise from its upper part, the two do not overlap
			Juice.Text(FString::Printf(TEXT("−%s с"), *FVUI::Num(EmptyHolePenalty, 1)), CellCenter(i) + FVector2D(0.f, CellS * 0.12f), FV::Col(255, 86, 64), bP ? 13.f : 10.f);
			Juice.Kick(0.3f);
			Sfx(EFVSfx::Error, 0.4f, 1.1f);
		}

		/** Update 5 (autotest): the play state and a dug hole a tap would be punished for (FVMiniGameDebug). */
		void UpdateDebug()
		{
			FFVMiniGameDebug& Dbg = FVMiniGameDebug();
			Dbg.bDigPlaying = true;
			Dbg.DigTimeLeft = TimeLeft;
			Dbg.DigTimeTotal = TimeTotal;
			Dbg.DigPenalties = Penalties;
			Dbg.DigEmptyHole = FVector2D(-1.f, -1.f);
			for (int32 i = 0; i < Cells.Num() && CellR.IsValidIndex(i); ++i)
			{
				const FCell& C = Cells[i];
				if (C.bDug && C.DugT >= 0.25f && (C.WormState == 0 || C.WormState == 2)) { Dbg.DigEmptyHole = CellCenter(i); break; }
			}
		}

		void Reveal(int32 i)
		{
			FCell& C = Cells[i];
			C.bDug = true;
			C.DugT = 0.f;
			const bool bP = FV::IsPortrait();
			const FVector2D Ctr = CellCenter(i);
			Juice.Burst(Ctr, FV::Col(96, 62, 38), 10, 90.f, 300.f, bP ? 4.f : 3.f, 0.6f);
			int32 Count = 0;
			const FName Id = FVDB::RollDigLoot(Loc, ShovelLevel, Rng, Count);
			if (Id.IsNone() || Count <= 0)
			{
				Juice.Text(TEXT("пусто"), Ctr - FVector2D(0.f, CellS * 0.25f), FV::Col(230, 220, 210), bP ? 10.f : 8.f);
				return;
			}
			C.Loot = Id;
			C.Count = Count;
			if (FVDB::IsBaitId(Id))
			{
				C.WormState = 1;
				C.WormAge = 0.f;
				Sfx(EFVSfx::Pickup, 0.45f, 0.7f);
				Juice.Text(TEXT("Червяк!"), Ctr - FVector2D(0.f, CellS * 0.45f), FV::Col(255, 190, 200), bP ? 11.f : 9.f);
			}
			else
			{
				Found.FindOrAdd(Id) += Count;
				AddFly(Id, Ctr);
				Sfx(EFVSfx::Pickup, 0.9f, 1.f);
				Juice.Sparkles(Ctr, 5, 8.f);
				Juice.Text(FString::Printf(TEXT("+%d %s"), Count, *FVDB::ItemName(Id)), Ctr - FVector2D(0.f, CellS * 0.3f), FVPal::Gold(), bP ? 11.f : 8.f);
			}
		}

		void TakeWorm(int32 i)
		{
			FCell& C = Cells[i];
			C.WormState = 2;
			Found.FindOrAdd(C.Loot) += C.Count;
			const bool bP = FV::IsPortrait();
			const FVector2D Ctr = CellCenter(i);
			AddFly(C.Loot, Ctr);
			Sfx(EFVSfx::Pickup, 1.f, 1.15f);
			Juice.Sparkles(Ctr, 8, 12.f);
			Juice.Kick(0.25f);
			Juice.Text(FString::Printf(TEXT("+%d %s"), C.Count, *FVDB::ItemName(C.Loot)), Ctr - FVector2D(0.f, CellS * 0.35f), FV::Col(150, 230, 110), bP ? 12.f : 9.f);
		}

		void AddFly(FName Id, const FVector2D& From)
		{
			FFly& F = Flies.AddDefaulted_GetRef();
			F.Id = Id;
			F.From = From;
		}

		void Finish()
		{
			if (bFinished) { return; }
			bFinished = true;
			Phase = EPhase::Over;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { Leave(); return; }
			P->MarkDug(Loc, Spot);
			int32 Worms = 0;
			int32 Dug = 0;
			for (const FCell& C : Cells) { if (C.bDug) { ++Dug; } }
			TArray<FString> Lost;
			TArray<FString> ItemLines;
			TArray<FString> ChestLines;
			FResultCard Res;
			for (const TPair<FName, int32>& KV : Found)
			{
				if (KV.Value <= 0) { continue; }
				if (FVDB::IsBaitId(KV.Key))
				{
					P->AddBait(KV.Key, KV.Value);
					Worms += KV.Value;
				}
				else if (P->GiveItem(KV.Key, KV.Value))
				{
					ItemLines.Add(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(KV.Key), KV.Value));
				}
				else if (P->AddTo(EFVContainer::Chest, KV.Key, KV.Value))   // full backpack: the chest
				{
					ChestLines.Add(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(KV.Key), KV.Value));
				}
				else
				{
					Lost.Add(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(KV.Key), KV.Value));
					continue;
				}
				FCardItem& It = Res.Items.AddDefaulted_GetRef();
				It.Id = KV.Key;
				It.Count = KV.Value;
			}
			P->Stats.WormsDug += Worms;
			Game->NotifyProfileChanged();
			Game->GetScene().RefreshWorldObjects();

			Res.bGood = Res.Items.Num() > 0;
			Res.Title = Res.bGood ? FString(TEXT("Раскопки окончены")) : FString(TEXT("Ничего не нашлось"));
			Res.Icon = EFVIcon::Shovel;
			Res.Sub = FString::Printf(TEXT("Перекопано %d из %d клеток"), Dug, Cells.Num());
			if (Worms > 0) { Res.AddLine(FString::Printf(TEXT("В наживку: +%d %s"), Worms, FVUI::Plural(Worms, TEXT("червяк"), TEXT("червяка"), TEXT("червяков"))), InkGood(), true); }
			if (ItemLines.Num() > 0) { Res.AddLine(FString::Printf(TEXT("В рюкзак: %s"), *FString::Join(ItemLines, TEXT(", "))), FVPal::Ink()); }
			if (ChestLines.Num() > 0) { Res.AddLine(FString::Printf(TEXT("Рюкзак полон, в сундук: %s"), *FString::Join(ChestLines, TEXT(", "))), FVPal::Ink()); }
			if (WormsLost > 0) { Res.AddLine(FString::Printf(TEXT("Уползло: %d"), WormsLost), FVPal::InkSoft()); }
			if (Penalties > 0) { Res.AddLine(FString::Printf(TEXT("Пустые ямки: −%s с"), *FVUI::Num(EmptyHolePenalty * float(Penalties), 1)), InkBad()); }
			if (Lost.Num() > 0) { Res.AddLine(FString::Printf(TEXT("Рюкзак полон, пропало: %s"), *FString::Join(Lost, TEXT(", "))), InkBad()); }
			ShowCard(Res, Res.bGood ? EFVSfx::Catch : EFVSfx::Escape);
		}

		// ------------------------------------------------------------------ drawing
		virtual void DrawGame(FVDraw& D, const FFVRect& Area) override
		{
			if (!bReady || Cells.Num() == 0) { return; }
			const bool bP = FV::IsPortrait();
			Cols = bP ? 4 : 5;
			const int32 Rows = (Cells.Num() + Cols - 1) / Cols;

			// garden background
			D.Rect(Area.Inset(-1.f), FVPal::WoodDark());
			D.GradientV(Area, FV::Col(128, 180, 86), FV::Col(96, 150, 64), 8);
			for (int32 k = 0; k < 26; ++k)
			{
				const FVector2D G(Area.X + 4.f + Hash01(k, 61) * (Area.W - 8.f), Area.Y + 30.f + Hash01(k, 67) * (Area.H - 34.f));
				const FLinearColor GC = FV::Col(70, 124, 50);
				D.Rect(FFVRect(Snap(G.X), Snap(G.Y), 1.f, 3.f), GC);
				D.Rect(FFVRect(Snap(G.X - 2.f), Snap(G.Y + 1.f), 1.f, 2.f), GC);
				D.Rect(FFVRect(Snap(G.X + 2.f), Snap(G.Y + 1.f), 1.f, 2.f), GC);
			}

			// timer bar
			const float BarH = bP ? 20.f : 14.f;
			const FFVRect TimerR(Area.X + 8.f, Area.Y + 6.f, Area.W - 16.f, BarH + 6.f);
			const float Jolt = PenaltyFlash > 0.f ? FMath::RoundToFloat(FMath::Sin(PenaltyFlash * 40.f) * 3.f * PenaltyFlash) : 0.f;   // penalty jolt
			DrawTimer(D, TimerR.Offset(Jolt, 0.f), bP);

			// grid + tally
			FFVRect GridArea, TallyR;
			const float Top = TimerR.Bottom() + 8.f;
			if (bP)
			{
				TallyR = FFVRect(Area.X + 6.f, Area.Bottom() - 50.f, Area.W - 12.f, 44.f);
				GridArea = FFVRect(Area.X + 6.f, Top, Area.W - 12.f, TallyR.Y - 8.f - Top);
			}
			else
			{
				TallyR = FFVRect(Area.Right() - 152.f, Top, 144.f, Area.Bottom() - Top - 6.f);
				GridArea = FFVRect(Area.X + 8.f, Top, TallyR.X - 14.f - Area.X, TallyR.H);
			}
			const float Gap = bP ? 5.f : 4.f;
			CellS = FMath::FloorToFloat(FMath::Min((GridArea.W - float(Cols - 1) * Gap) / float(Cols), (GridArea.H - float(Rows - 1) * Gap) / float(Rows)));
			CellS = FMath::Clamp(CellS, 16.f, bP ? 76.f : 56.f);
			const float GW = float(Cols) * CellS + float(Cols - 1) * Gap, GH = float(Rows) * CellS + float(Rows - 1) * Gap;
			const FFVRect GridR(Snap(GridArea.Center().X - GW * 0.5f), Snap(GridArea.Center().Y - GH * 0.5f), GW, GH);
			D.Rect(GridR.Inset(-6.f), FV::Col(74, 48, 28));
			D.Rect(GridR.Inset(-4.f), FV::Col(98, 64, 40));
			CellR.SetNum(Cells.Num());
			for (int32 i = 0; i < Cells.Num(); ++i)
			{
				const int32 Cx = i % Cols, Cy = i / Cols;
				CellR[i] = FFVRect(GridR.X + float(Cx) * (CellS + Gap), GridR.Y + float(Cy) * (CellS + Gap), CellS, CellS);
			}
			const bool bCanAct = bInteractive && (Phase == EPhase::Play || Phase == EPhase::TimeUp);
			for (int32 i = 0; i < Cells.Num(); ++i) { DrawCell(D, i, bCanAct); }
			if (bCanAct)
			{
				for (int32 i = 0; i < Cells.Num(); ++i)
				{
					if (D.PressedIn(CellR[i])) { Hit(i); break; }
				}
			}

			// shovel swing
			if (ShovelT < 0.22f && CellR.IsValidIndex(ShovelCell))
			{
				if (UFVSprites* Sp = UFVSprites::Get())
				{
					const FFVSprite& SS = Sp->ShovelIcon(ShovelLevel);
					const float T = ShovelT / 0.22f;
					const FVector2D SC = CellR[ShovelCell].Center() + FVector2D(CellS * 0.22f, -CellS * 0.3f + FMath::Sin(T * PI) * CellS * 0.15f);
					D.SpriteCentered(SS, SC, (bP ? 3.f : 2.f), FLinearColor::White, false, FMath::Lerp(-55.f, 15.f, Smooth01(T)));
				}
			}

			DrawTally(D, TallyR, bP);

			// items flying into the tally
			for (const FFly& F : Flies)
			{
				const FFVSprite* IS = ItemIconPtr(F.Id);
				if (!IS) { continue; }
				const float T = Clamp01(F.Age / FlyTime);
				const FVector2D To = TallyTarget.IsZero() ? F.From - FVector2D(0.f, 40.f) : TallyTarget;
				const FVector2D P = FMath::Lerp(F.From, To, double(Smooth01(T))) - FVector2D(0.f, FMath::Sin(T * PI) * 30.f);
				const float Sc = FMath::Lerp(bP ? 3.f : 2.f, 1.f, T) * FMath::Min(1.f, 16.f / FMath::Max(1.f, IS->W));
				D.SpriteCentered(*IS, P, Sc);
			}
		}

		void DrawTimer(FVDraw& D, const FFVRect& R, bool bP)
		{
			WoodFrame(D, R);
			const FFVRect T = InnerTrack(D, R, 3.f, FV::Col(48, 30, 22));
			const float F = TimeTotal > 0.f ? Clamp01(TimeLeft / TimeTotal) : 0.f;
			const float FW = Snap(T.W * F);
			const FLinearColor C = MeterColor(F);
			if (FW > 0.f)
			{
				D.Rect(FFVRect(T.X, T.Y, FW, T.H), C);
				D.Rect(FFVRect(T.X, T.Y, FW, 1.f), FMath::Lerp(C, FLinearColor::White, 0.45f));
			}
			if (Phase == EPhase::Play && TimeLeft < 3.f) { D.Rect(T, FV::Col(255, 60, 40, uint8(70.f * FMath::Abs(FMath::Sin(Time * 9.f))))); }
			if (PenaltyFlash > 0.f)   // update 5: a tap on an empty hole flashes the timer red
			{
				D.Rect(T, FV::Col(255, 40, 30, uint8(170.f * PenaltyFlash)));
				D.Frame(R, WithAlpha(FV::Col(255, 90, 70), PenaltyFlash), 2.f);
			}
			const float TS = bP ? 9.f : 7.f;
			if (const FFVSprite* Ck = FVUI::Icon(EFVIcon::Clock))
			{
				D.SpriteCentered(*Ck, FVector2D(T.X + T.H * 0.5f + 2.f, T.Center().Y), (T.H + 2.f) / FMath::Max(1.f, Ck->H));
			}
			const FLinearColor TimeC = FMath::Lerp(FLinearColor::White, FV::Col(255, 150, 130), PenaltyFlash);
			D.TextOutlined(FString::Printf(TEXT("%s с"), *FVUI::Num(TimeLeft, 1)), FVector2D(T.Right() - 4.f, Snap(T.Center().Y - D.LineHeight(TS) * 0.5f)), TS, TimeC, EFVAlign::Right);
		}

		void DrawCell(FVDraw& D, int32 i, bool bCanAct)
		{
			const FCell& C = Cells[i];
			FFVRect R = CellR[i];
			if (C.HitT < 0.12f) { R = R.Offset(0.f, 1.f); }
			const int32 H0 = i * 7 + 3;
			if (!C.bDug)
			{
				D.Rect(R, FV::Col(96, 62, 38));
				D.Rect(R.Inset(1.f), FV::Col(138, 92, 56));
				D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 2.f), FV::Col(164, 114, 72));
				for (int32 k = 0; k < 8; ++k)
				{
					const bool bLight = Hash01(H0, k + 23) > 0.5f;
					D.Rect(FFVRect(Snap(R.X + 3.f + Hash01(H0, k) * (R.W - 8.f)), Snap(R.Y + 4.f + Hash01(H0, k + 11) * (R.H - 8.f)), 2.f, 2.f), bLight ? FV::Col(166, 118, 76) : FV::Col(108, 70, 42));
				}
				if (Hash01(H0, 99) > 0.68f)
				{
					const float GX = Snap(R.X + 5.f + Hash01(H0, 98) * (R.W - 12.f)), GY = Snap(R.Y + 4.f);
					D.Rect(FFVRect(GX, GY, 1.f, 4.f), FV::Col(86, 150, 60));
					D.Rect(FFVRect(GX - 2.f, GY + 1.f, 1.f, 3.f), FV::Col(86, 150, 60));
					D.Rect(FFVRect(GX + 2.f, GY + 2.f, 1.f, 2.f), FV::Col(86, 150, 60));
				}
				if (C.Hits > 0)
				{
					const FVector2D Cc = R.Center();
					const FLinearColor CrC = FV::Col(70, 44, 28);
					D.Line(Cc + FVector2D(-R.W * 0.25f, -R.H * 0.1f), Cc + FVector2D(R.W * 0.05f, R.H * 0.05f), CrC, 1.5f);
					D.Line(Cc + FVector2D(R.W * 0.05f, R.H * 0.05f), Cc + FVector2D(R.W * 0.28f, -R.H * 0.12f), CrC, 1.5f);
					D.Line(Cc + FVector2D(R.W * 0.05f, R.H * 0.05f), Cc + FVector2D(0.f, R.H * 0.28f), CrC, 1.5f);
				}
			}
			else
			{
				D.Rect(R, FV::Col(84, 54, 32));
				D.Rect(R.Inset(1.f), FV::Col(108, 70, 42));
				D.Circle(FVector2D(R.X + R.W * 0.8f, R.Y + R.H * 0.26f), R.W * 0.1f, FV::Col(134, 90, 56));
				D.Circle(FVector2D(R.X + R.W * 0.2f, R.Y + R.H * 0.8f), R.W * 0.07f, FV::Col(134, 90, 56));
				const FVector2D HC(R.Center().X, R.Center().Y + R.H * 0.1f);
				const float HRX = R.W * 0.32f, HRY = R.H * 0.17f;
				const FLinearColor Rim = FV::Col(130, 88, 54), HoleC = FV::Col(36, 22, 14);
				FillEllipse(D, HC, HRX + 2.f, HRY + 2.f, Rim);
				FillEllipse(D, HC, HRX, HRY, HoleC);
				// a found item pops out of the hole before flying to the tally
				if (C.DugT < 0.25f && C.WormState == 0 && !C.Loot.IsNone())
				{
					if (const FFVSprite* IS = ItemIconPtr(C.Loot)) { D.SpriteCentered(*IS, HC - FVector2D(0.f, R.H * 0.3f * Smooth01(C.DugT / 0.25f)), 1.f); }
				}
				float Emerge = 0.f;
				if (C.WormState == 1) { Emerge = Smooth01(C.WormAge / 0.18f); }
				else if (C.WormState == 3 && C.WormAge < WormWindow + 0.3f) { Emerge = 1.f - Smooth01((C.WormAge - WormWindow) / 0.3f); }
				if (Emerge > 0.f)
				{
					DrawWorm(D, HC, CellS, Emerge, C.WormAge, C.Loot != FName(TEXT("bait_worm")));
					FillEllipse(D, HC + FVector2D(0.f, 1.f), HRX + 2.f, HRY + 2.f, Rim, 2);
					FillEllipse(D, HC, HRX, HRY, HoleC, 2);
					if (C.WormState == 1)
					{
						const float Left = 1.f - Clamp01(C.WormAge / WormWindow);
						const FFVRect Bar(Snap(R.Center().X - R.W * 0.3f), Snap(R.Bottom() - 6.f), Snap(R.W * 0.6f), 3.f);
						D.Rect(Bar.Inset(-1.f), FV::Col(40, 26, 18));
						D.Rect(FFVRect(Bar.X, Bar.Y, Snap(Bar.W * Left), Bar.H), MeterColor(Left));
					}
				}
			}
			if (bKeyUsed && i == Cursor)
			{
				D.Frame(R.Inset(-1.f), WithAlpha(FVPal::Gold(), 0.7f + 0.3f * FMath::Sin(Time * 8.f)), 2.f);
			}
			else if (bCanAct && !FV::IsTouchUI() && D.Hover(R) && (!C.bDug || C.WormState == 1))
			{
				D.Frame(R.Inset(-1.f), WithAlpha(FLinearColor::White, 0.7f), 1.f);
			}
		}

		void DrawWorm(FVDraw& D, const FVector2D& HC, float S, float Emerge, float Age, bool bBig) const
		{
			const int32 NSeg = bBig ? 7 : 6;
			const float Rad = FMath::Max(1.5f, S * (bBig ? 0.075f : 0.062f));
			const float Hgt = S * (bBig ? 0.5f : 0.42f) * Emerge;
			const FLinearColor Body = bBig ? FV::Col(176, 90, 104) : FV::Col(232, 132, 146);
			const FLinearColor Dark = bBig ? FV::Col(118, 54, 68) : FV::Col(168, 80, 96);
			TArray<FVector2D, TInlineAllocator<8>> Pts;
			for (int32 k = 0; k < NSeg; ++k)
			{
				const float T = float(k) / float(NSeg - 1);
				Pts.Add(FVector2D(HC.X + FMath::Sin(Age * 11.f + float(k) * 0.9f) * S * 0.07f * T, HC.Y - Hgt * T));
			}
			for (const FVector2D& P : Pts) { D.Circle(P, Rad + 1.f, Dark); }
			for (int32 k = 0; k < Pts.Num(); ++k) { D.Circle(Pts[k], Rad, (k % 2) ? FMath::Lerp(Body, Dark, 0.18f) : Body); }
			const FVector2D Hd = Pts.Last();
			D.Rect(FFVRect(Snap(Hd.X - Rad * 0.55f), Snap(Hd.Y - Rad * 0.35f), FMath::Max(1.f, Snap(Rad * 0.3f)), FMath::Max(1.f, Snap(Rad * 0.3f))), FV::Col(30, 16, 16));
			D.Rect(FFVRect(Snap(Hd.X + Rad * 0.25f), Snap(Hd.Y - Rad * 0.35f), FMath::Max(1.f, Snap(Rad * 0.3f)), FMath::Max(1.f, Snap(Rad * 0.3f))), FV::Col(30, 16, 16));
			D.Rect(FFVRect(Snap(Hd.X - Rad * 0.2f), Snap(Hd.Y - Rad * 0.85f), FMath::Max(1.f, Snap(Rad * 0.5f)), 1.f), FV::Col(255, 220, 226));
		}

		void DrawTally(FVDraw& D, const FFVRect& R, bool bP)
		{
			D.Panel(R, EFVPanel::Parchment);
			const float Body = FV::Ui::Body(), Small = FV::Ui::Small();
			const float IconS = bP ? 20.f : 14.f;
			if (bP)
			{
				D.Text(TEXT("Находки:"), FVector2D(R.X + 8.f, Snap(R.Center().Y - D.LineHeight(Body) * 0.5f)), Body, FVPal::Ink(), EFVAlign::Left, false, true);
				float X = R.X + 12.f + D.MeasureText(TEXT("Находки:"), Body).X;
				TallyTarget = FVector2D(X + IconS * 0.5f, R.Center().Y);
				for (const TPair<FName, int32>& KV : Found)
				{
					if (X > R.Right() - 70.f) { break; }
					if (const FFVSprite* IS = ItemIconPtr(KV.Key)) { FVUI::SpriteFit(D, *IS, FFVRect(X, Snap(R.Center().Y - IconS * 0.5f), IconS, IconS), FLinearColor::White, 2.f); }
					X += IconS + 2.f;
					const FString N = FString::Printf(TEXT("×%d"), KV.Value);
					D.Text(N, FVector2D(X, Snap(R.Center().Y - D.LineHeight(Small) * 0.5f)), Small, FVPal::Ink(), EFVAlign::Left, false, true);
					X += D.MeasureText(N, Small).X + 8.f;
				}
				if (WormsLost > 0)
				{
					D.Text(FString::Printf(TEXT("Уползло: %d"), WormsLost), FVector2D(R.Right() - 8.f, Snap(R.Center().Y - D.LineHeight(Small) * 0.5f)), Small, FVPal::InkSoft(), EFVAlign::Right, false, true);
				}
				return;
			}
			float Y = FVUI::SectionHeader(D, TEXT("Находки"), FVector2D(R.X + 8.f, R.Y + 6.f), R.W - 16.f, 9.f);
			TallyTarget = FVector2D(R.Center().X, Y + 6.f);
			if (Found.Num() == 0)
			{
				D.Text(TEXT("Пока пусто"), FVector2D(R.X + 8.f, Y + 2.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += 14.f;
			}
			for (const TPair<FName, int32>& KV : Found)
			{
				if (Y > R.Bottom() - 50.f) { break; }
				if (const FFVSprite* IS = ItemIconPtr(KV.Key)) { FVUI::SpriteFit(D, *IS, FFVRect(R.X + 8.f, Y, IconS, IconS), FLinearColor::White, 1.f); }
				D.Text(D.Ellipsize(FVDB::ItemName(KV.Key), R.W - 60.f, 7.f, false), FVector2D(R.X + 26.f, Y + 3.f), 7.f, FVPal::Ink(), EFVAlign::Left, false, false);
				D.Text(FString::Printf(TEXT("×%d"), KV.Value), FVector2D(R.Right() - 8.f, Y + 3.f), 7.5f, FVPal::Ink(), EFVAlign::Right, false, true);
				Y += IconS + 3.f;
			}
			if (WormsLost > 0)
			{
				D.Text(FString::Printf(TEXT("Уползло: %d"), WormsLost), FVector2D(R.X + 8.f, Y + 2.f), 6.5f, InkBad(), EFVAlign::Left, false, true);
			}
			// shovel info at the bottom
			const float BY = R.Bottom() - 34.f;
			D.Divider(FVector2D(R.X + 8.f, BY - 4.f), R.W - 16.f);
			if (UFVSprites* Sp = UFVSprites::Get()) { D.SpriteRect(Sp->ShovelIcon(ShovelLevel), FFVRect(R.X + 8.f, BY, 16.f, 16.f)); }
			const TArray<FFVShovelDef>& Shovels = FVDB::Shovels();
			const FString SName = Shovels.IsValidIndex(ShovelLevel - 1) ? Shovels[ShovelLevel - 1].Name : FString(TEXT("Лопата"));
			D.Text(D.Ellipsize(SName, R.W - 36.f, 7.f), FVector2D(R.X + 28.f, BY), 7.f, FVPal::Ink(), EFVAlign::Left, false, true);
			D.Text(HitsNeeded <= 1 ? FString(TEXT("1 удар на клетку")) : FString::Printf(TEXT("%d удара на клетку"), HitsNeeded), FVector2D(R.X + 28.f, BY + 10.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, false);
		}
	};
}

// =========================================================================================================
// factories (Public/FVScreens.h)
// =========================================================================================================
FFVMiniGameDebug& FVMiniGameDebug()
{
	static FFVMiniGameDebug Debug;
	return Debug;
}

TUniquePtr<FVScreen> FVMakeCookingScreen(UFVGame* Game) { return MakeUnique<FVMiniGamesImpl::FCookingScreen>(Game); }
TUniquePtr<FVScreen> FVMakeLureCraftScreen(UFVGame* Game) { return MakeUnique<FVMiniGamesImpl::FLureCraftScreen>(Game); }
TUniquePtr<FVScreen> FVMakeKnotsScreen(UFVGame* Game) { return MakeUnique<FVMiniGamesImpl::FKnotsScreen>(Game); }
TUniquePtr<FVScreen> FVMakeDiggingScreen(UFVGame* Game) { return MakeUnique<FVMiniGamesImpl::FDiggingScreen>(Game); }

// =========================================================================================================
// FFVLureWorkshop (Private/FVMiniGames.h): the reusable lure workshop panel
// =========================================================================================================
FFVLureWorkshop::FFVLureWorkshop(UFVGame* InGame, bool bEmbedded)
	: Bench(MakeUnique<FVMiniGamesImpl::FLureBench>(InGame, bEmbedded))
{
}

FFVLureWorkshop::~FFVLureWorkshop() = default;

FVMiniGamesImpl::FLureBench& FFVLureWorkshop::Core() { return *Bench; }
void FFVLureWorkshop::Open() { Bench->Open(); }

void FFVLureWorkshop::Tick(float Dt, const FFVInput& In, bool bKeys)
{
	Bench->TickFx(Dt);
	Bench->TickLogic(Dt, In, bKeys, false);
}

void FFVLureWorkshop::Draw(FVDraw& D, const FFVRect& Area, bool bInteractive, bool bButtonInside) { Bench->Draw(D, Area, bInteractive, bButtonInside); }
bool FFVLureWorkshop::DrawAssembleButton(FVDraw& D, const FFVRect& R, bool bInteractive, float TextSize) { return Bench->DrawAssembleButton(D, R, bInteractive, TextSize); }
bool FFVLureWorkshop::IsAssembling() const { return Bench->IsAssembling(); }
bool FFVLureWorkshop::IsCardShown() const { return Bench->IsCardShown(); }
void FFVLureWorkshop::CloseCard() { Bench->CloseCard(); }
FString FFVLureWorkshop::Hint() const { return Bench->Hint(); }

// ============================================================================================
// Update 6: «Русская печь» (kitchen level 4) — two portions at once
// ============================================================================================
FFVRecipeDef FVCookedRecipe(const UFVSaveGame& P, const FFVRecipeDef& R)
{
	FFVRecipeDef Out = R;
	Out.OutputCount = FVCookedCount(P, R);
	return Out;
}

int32 FVCookedCount(const UFVSaveGame& P, const FFVRecipeDef& R)
{
	const int32 N = FMath::Max(1, R.OutputCount);
	const bool bKitchen = R.Yard == EFVYardBuilding::Count && R.Station == EFVStation::Kitchen;
	return bKitchen && P.GetStationLevel(EFVStation::Kitchen) >= FVDoublePortionKitchenLevel ? N * 2 : N;
}
