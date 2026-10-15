// UPDATE 6 [EXPERT-DRAW] — drawing of the expert fight, edition 2 «одна вертикаль» (Private/FVExpertDraw.h; §19, Р60,
// Р54). Owner: EXPERT-DRAW.
// Layout: the classic wooden panel (FVFishing.cpp, FFVMinigame) beside the hero on the roomier side: the water track
// (left), the progress meter, the narrow tension meter (right). Portrait: ~1.3x the classic height, between the top
// strip (+ a banner line) and the bottom dock; landscape: the classic 300. The 760-unit track fills the track height.
// Look (Р54): classic wood, the location's water tone, the classic green bar(s) (gold for a legend), the classic fish
// icon from the side; a rod adds a thin accent stripe around the track + the type badge in the panel's top margin.
// Hazards are plain marks on the track. Warning colours: red (Рывок) and yellow -> red (tension) are shared.
// Immediate-mode; the only state kept between frames is visual (the phase flash) and the hero X of FieldRect.

#include "FVExpertDraw.h"
#include "FVExpertFight.h"
#include "FVDraw.h"
#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVGame.h"
#include "FVScene.h"

namespace FVExpertDrawImpl
{
	constexpr float Edge = 8.f;          // screen margin
	constexpr float BannerRoom = 18.f;   // portrait: a line above the panel for the banner

	float Snap(float V) { return FMath::RoundToFloat(V); }
	FLinearColor WithA(const FLinearColor& C, float A) { FLinearColor R = C; R.A *= FMath::Clamp(A, 0.f, 1.f); return R; }
	FLinearColor Lin(const FColor& C, float A = 1.f) { return WithA(FLinearColor(C), A); }
	float Pulse(float Time, float Hz) { return 0.5f + 0.5f * FMath::Sin(Time * Hz * 2.f * PI); }
	float Hash01(int32 N) { return float((uint32(N) * 2654435761u) >> 8) / float(1 << 24); }

	FLinearColor WarnYellow() { return FV::Col(255, 208, 64); }
	FLinearColor WarnRed() { return FV::Col(255, 70, 40); }
	FLinearColor TensionColor(float T) { return FLinearColor::LerpUsingHSV(WarnYellow(), WarnRed(), FMath::SmoothStep(0.35f, 0.9f, T)); }
	/** The classic progress meter colour (FVFishing.cpp MeterColor): red -> yellow -> green. */
	FLinearColor MeterColor(float P)
	{
		P = FMath::Clamp(P, 0.f, 1.f);
		const FLinearColor Red = FV::Col(222, 60, 44), Yellow = FV::Col(246, 206, 58), Green = FV::Col(96, 204, 64);
		return P < 0.5f ? FMath::Lerp(Red, Yellow, P * 2.f) : FMath::Lerp(Yellow, Green, (P - 0.5f) * 2.f);
	}

	// ---- per-frame layout memory (FieldRect is called first every frame) + the visual-only phase flash
	float GHeroX = -1.f;
	struct FDrawMemory
	{
		float LastElapsed = -1.f;
		float LastTime = 0.f;
		int32 LastPhase = 0;
		float PhaseFlash = 0.f;
	};
	FDrawMemory Mem;

	float HeroX() { return GHeroX >= 0.f ? GHeroX : FV::VW * 0.5f; }
	/** The panel scale K (portrait: the classic K = the screen height / 300 in 1..1.2; landscape 1). */
	float PanelK()
	{
		if (!FV::IsPortrait()) { return 1.f; }
		const float AreaTop = FV::TopBarH + 4.f, AreaBottom = FV::VH - FV::BottomBarH - 4.f;
		return FMath::Clamp((AreaBottom - AreaTop) / 300.f, 1.f, 1.2f);
	}
	float KOf(const FFVRect& P) { return FMath::Max(0.5f, P.W / 52.f); }
	float TopMargin(float K) { return Snap(18.f * K); }
	bool PanelRightOfHero(const FFVRect& P) { return P.X + P.W * 0.5f >= HeroX(); }

	/** Free horizontal span beside the panel for text / pictograms: bFar = the side away from the hero. */
	void SideSpan(const FFVRect& P, bool bFar, float& L, float& R)
	{
		const bool bRight = PanelRightOfHero(P);
		const float Hx = HeroX();
		if (bFar)
		{
			L = bRight ? P.Right() + 6.f : Edge;
			R = bRight ? FV::VW - Edge : P.X - 6.f;
		}
		else
		{
			// the other side of the hero (never between the hero and the panel)
			L = bRight ? Edge : Hx + 24.f;
			R = bRight ? Hx - 24.f : FV::VW - Edge;
		}
	}
	/** The span a hint / banner goes to: the far side when it is roomy, otherwise the wider one. */
	void TextSpan(const FFVRect& P, float MinFar, float& L, float& R, bool& bFar)
	{
		float FL, FR, OL, OR;
		SideSpan(P, true, FL, FR);
		SideSpan(P, false, OL, OR);
		bFar = FR - FL >= MinFar || FR - FL >= OR - OL;
		L = bFar ? FL : OL;
		R = bFar ? FR : OR;
	}

	/** Greedy word wrap (no cut words; a too long word stays on its own line). */
	TArray<FString> Wrap(const FVDraw& D, const FString& Text, float MaxW, float Size)
	{
		TArray<FString> Words, Lines;
		Text.ParseIntoArray(Words, TEXT(" "), true);
		FString Cur;
		for (const FString& W : Words)
		{
			const FString Try = Cur.IsEmpty() ? W : Cur + TEXT(" ") + W;
			if (!Cur.IsEmpty() && D.MeasureText(Try, Size).X > MaxW) { Lines.Add(Cur); Cur = W; }
			else { Cur = Try; }
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
		return Lines;
	}

	void Arrow(FVDraw& D, const FVector2D& From, const FVector2D& To, const FLinearColor& C, float Thick)
	{
		D.Line(From, To, C, Thick);
		const FVector2D V = To - From;
		const float Len = float(V.Size());
		if (Len < 1.f) { return; }
		const FVector2D U = V / Len, N(-U.Y, U.X);
		const float H = FMath::Min(5.f, Len * 0.6f);
		D.Line(To, To - U * H + N * H * 0.8f, C, Thick);
		D.Line(To, To - U * H - N * H * 0.8f, C, Thick);
	}
	/** A chevron «v» (Down) or «^» pointing along the track. */
	void Chevron(FVDraw& D, float Cx, float Y, float HalfW, bool bDown, const FLinearColor& C, float Thick = 1.5f)
	{
		const float Tip = bDown ? HalfW * 0.6f : -HalfW * 0.6f;
		D.Line(FVector2D(Cx - HalfW, Y), FVector2D(Cx, Y + Tip), C, Thick);
		D.Line(FVector2D(Cx + HalfW, Y), FVector2D(Cx, Y + Tip), C, Thick);
	}

	// ------------------------------------------------------------------ the panel
	/** The classic wooden panel (FVFishing.cpp DrawMinigame): shadow, dark edge, wood, light top-left bevel, darker
	    bottom, sparse grain between the track and the meters, four nails. */
	void Panel(FVDraw& D, const FFVRect& P, float K)
	{
		D.Rect(P.Offset(2.f, 3.f), FV::Col(0, 0, 0, 90));
		D.Rect(P, FVPal::WoodDark());
		D.Rect(P.Inset(1.f), FVPal::Wood());
		D.Rect(FFVRect(P.X + 1.f, P.Y + 1.f, P.W - 2.f, 2.f), FVPal::WoodLight());
		D.Rect(FFVRect(P.X + 1.f, P.Y + 1.f, 2.f, P.H - 2.f), FVPal::WoodLight());
		D.Rect(FFVRect(P.X + 1.f, P.Bottom() - 3.f, P.W - 2.f, 2.f), FMath::Lerp(FVPal::WoodDark(), FVPal::Wood(), 0.4f));
		for (float Gy = P.Y + 24.f * K; Gy < P.Bottom() - 6.f * K; Gy += 17.f * K) { D.Rect(FFVRect(Snap(P.X + 30.f * K), Snap(Gy), 2.f, 1.f), FVPal::WoodDark()); }
		const float NailS = Snap(2.f * K);
		for (const FVector2D& N : { FVector2D(P.Right() - 5.f * K, P.Y + 4.f * K), FVector2D(P.X + 4.f * K, P.Bottom() - 6.f * K),
			FVector2D(P.Right() - 5.f * K, P.Bottom() - 6.f * K) })
		{
			D.Rect(FFVRect(Snap(N.X), Snap(N.Y), NailS, NailS), FV::Col(120, 110, 100));
		}
	}

	/** The rod type badge (Р54): a small wooden plate with the accent edge and a plain glyph of the control law. */
	void TypeBadge(FVDraw& D, const FFVRect& R, EFVExpertRodType Type, const FLinearColor& Accent)
	{
		D.Rect(R.Offset(1.f, 1.f), FV::Col(0, 0, 0, 70));
		D.Rect(R, FVPal::WoodDark());
		D.Rect(R.Inset(1.f), FV::Col(120, 62, 28));
		if (Accent.A > 0.f) { D.Frame(R.Inset(1.f), Accent, 1.f); }
		const FLinearColor G = FV::Col(255, 240, 210);
		const FVector2D C = R.Center();
		const float U = R.W / 18.f;
		switch (Type)
		{
		case EFVExpertRodType::Pendulum:   // «Маятник»: up / down
			Arrow(D, C, C + FVector2D(0.f, -6.f * U), G, 1.5f);
			Arrow(D, C, C + FVector2D(0.f, 6.f * U), G, 1.5f);
			break;
		case EFVExpertRodType::Twin:       // «Двойчатка»: two bars on one track
			D.Rect(FFVRect(C.X - 4.f * U, C.Y - 6.f * U, 8.f * U, 4.f * U), G);
			D.Rect(FFVRect(C.X - 4.f * U, C.Y + 2.f * U, 8.f * U, 4.f * U), G);
			break;
		case EFVExpertRodType::Compass:    // «Импульс»: a push up with a spark under it
			Arrow(D, C + FVector2D(0.f, 3.f * U), C + FVector2D(0.f, -6.f * U), G, 1.5f);
			D.Rect(FFVRect(C.X - 3.5f * U, C.Y + 5.f * U, 7.f * U, 1.5f * U), G);
			break;
		case EFVExpertRodType::Magnet:     // «Магнит»: horseshoe
			D.Rect(FFVRect(C.X - 5.f * U, C.Y - 5.f * U, 2.5f * U, 7.f * U), G);
			D.Rect(FFVRect(C.X + 2.5f * U, C.Y - 5.f * U, 2.5f * U, 7.f * U), G);
			D.Rect(FFVRect(C.X - 5.f * U, C.Y + 2.f * U, 10.f * U, 2.5f * U), G);
			break;
		default:                           // «Ползунок»: a slot with a knob
			D.Rect(FFVRect(C.X - 0.75f * U, C.Y - 6.f * U, 1.5f * U, 12.f * U), G);
			D.Rect(FFVRect(C.X - 4.f * U, C.Y - 1.5f * U, 8.f * U, 3.5f * U), G);
			break;
		}
	}

	/** A meter in the classic style: dark wood edge, dark inside, fill from the bottom with a 1 px light side. */
	void Meter(FVDraw& D, const FFVRect& R, float Frac, const FLinearColor& Fill)
	{
		D.Rect(R.Inset(-1.f), FVPal::WoodDark());
		D.Rect(R, FV::Col(50, 34, 26));
		const float H = Snap(R.H * FMath::Clamp(Frac, 0.f, 1.f));
		if (H <= 0.f) { return; }
		D.Rect(FFVRect(R.X, R.Bottom() - H, R.W, H), Fill);
		D.Rect(FFVRect(R.X, R.Bottom() - H, 1.f, H), FMath::Lerp(Fill, FLinearColor::White, 0.4f));
	}

	// ------------------------------------------------------------------ hazards (track units -> screen rows)
	struct FTrackMap
	{
		FFVRect T;
		float Y(float U) const { return T.Y + U / FVExpert::TrackH * T.H; }
		float Len(float U) const { return U / FVExpert::TrackH * T.H; }
		FFVRect Span(float Top, float Bottom) const
		{
			const float A = Snap(Y(FMath::Min(Top, Bottom))), B = Snap(Y(FMath::Max(Top, Bottom)));
			return FFVRect(T.X, A, T.W, FMath::Max(2.f, B - A));
		}
	};

	void HazardsUnder(FVDraw& D, UFVSprites* Sp, const FFVExpertFight& F, const FTrackMap& M, float Time)
	{
		const FFVRect& T = M.T;
		const float Cx = T.X + T.W * 0.5f;
		for (const FFVExpertHazardState& H : F.Hazards)
		{
			const bool bDown = H.Dir >= 0.f;
			switch (H.Kind)
			{
			case EFVHazard::Current:
			{
				// «Течение»: chevrons drifting the way it carries the bar
				const float A = 0.3f + 0.45f * FMath::Clamp(H.Strength, 0.f, 1.f);
				constexpr int32 N = 5;
				const float Step = T.H / N;
				const float Off = FMath::Fmod(Time * 34.f, Step);
				for (int32 k = -1; k <= N; ++k)
				{
					const float Y = T.Y + k * Step + (bDown ? Off : Step - Off);
					if (Y < T.Y + 2.f || Y > T.Bottom() - 2.f) { continue; }
					Chevron(D, Cx, Y, T.W * 0.28f, bDown, FV::Col(225, 242, 255, uint8(255.f * A)));
				}
				break;
			}
			case EFVHazard::Wave:
			{
				// «Волна»: the foam edge upstream while it is announced; a crest running along the track while it pushes
				if (H.Warn > 0.f)
				{
					const float A = 0.45f + 0.5f * Pulse(Time, 4.f);
					D.Rect(FFVRect(T.X, bDown ? T.Y : T.Bottom() - 4.f, T.W, 4.f), FV::Col(245, 250, 255, uint8(200.f * A)));
				}
				if (H.Active > 0.f)
				{
					const float P = FMath::Frac(Time * 1.2f);
					const float Y = bDown ? T.Y + P * T.H : T.Bottom() - P * T.H;
					const float A = 0.35f + 0.6f * FMath::Clamp(H.Strength, 0.f, 1.f);
					D.Rect(FFVRect(T.X, Snap(Y) - 2.f, T.W, 4.f), FV::Col(245, 250, 255, uint8(190.f * A)));
					Chevron(D, Cx, Y + (bDown ? 6.f : -6.f), T.W * 0.3f, bDown, FV::Col(245, 250, 255, uint8(220.f * A)), 2.f);
				}
				break;
			}
			case EFVHazard::Whirlpool:
			{
				// «Водоворот»: a circle at its centre; rings shrink into it while it is announced
				const FVector2D C(Cx, M.Y(H.Pos));
				const float R = T.W * 0.5f;
				if (H.Warn > 0.f)
				{
					for (int32 k = 0; k < 3; ++k)
					{
						const float P = FMath::Fmod(Time * 1.4f + k / 3.f, 1.f);
						const float Rr = R * (1.3f - 0.9f * P);
						D.Ring(C, Rr, Rr, FV::Col(230, 245, 255, uint8(150.f * P)), 1.f);
					}
				}
				if (H.Active > 0.f)
				{
					const float S = FMath::Clamp(H.Strength, 0.f, 1.f);
					D.Circle(C, R, FV::Col(10, 30, 60, uint8(70.f + 60.f * S)));
					D.Ring(C, R - 1.f, R - 1.f, FV::Col(220, 240, 255, uint8(90.f + 90.f * S)), 1.f);
					for (int32 k = 0; k < 6; ++k)
					{
						const float Ang = Time * 5.f + k * (PI / 3.f);
						const float Rr = R * (k & 1 ? 0.35f : 0.65f);
						D.Rect(FFVRect(Snap(C.X + FMath::Cos(Ang) * Rr) - 1.f, Snap(C.Y + FMath::Sin(Ang) * Rr) - 1.f, 2.f, 2.f), FV::Col(235, 248, 255, 200));
					}
					// the pull toward the centre, above and below
					const FLinearColor PC = FV::Col(225, 242, 255, uint8(80.f + 120.f * S));
					Chevron(D, Cx, C.Y - R - 6.f, T.W * 0.22f, true, PC);
					Chevron(D, Cx, C.Y + R + 6.f, T.W * 0.22f, false, PC);
				}
				break;
			}
			case EFVHazard::Piles:
			{
				// «Коряги»: brown sections where the bar sticks; a few dark branches across
				for (const FVector2D& S : H.Spans)
				{
					const FFVRect R = M.Span(float(S.X), float(S.Y));
					D.Rect(R, FV::Col(96, 68, 44, 130));
					D.Rect(FFVRect(R.X, R.Y, R.W, 1.f), FV::Col(50, 34, 22, 200));
					D.Rect(FFVRect(R.X, R.Bottom() - 1.f, R.W, 1.f), FV::Col(50, 34, 22, 200));
					for (float Y = R.Y + 4.f; Y < R.Bottom() - 3.f; Y += 9.f)
					{
						D.Line(FVector2D(R.X, Y + 3.f), FVector2D(R.Right(), Y), FV::Col(60, 40, 24, 200), 1.5f);
					}
					if (Sp && R.H >= 10.f)
					{
						const FFVSprite& S1 = Sp->HazardSprite(EFVHazard::Piles, 0);
						if (S1.IsValid()) { D.SpriteCentered(S1, R.Center(), FMath::Min(R.H, T.W) / FMath::Max(1.f, float(FMath::Max(S1.W, S1.H)))); }
					}
				}
				break;
			}
			case EFVHazard::Drip:
			{
				// «Капель»: where the stone falls (a drop running down to it), then grey stones across the track
				if (H.Warn > 0.f)
				{
					const float Y = M.Y(H.Pos);
					const float G = 1.f - FMath::Clamp(H.Warn, 0.f, 1.f);
					D.Rect(FFVRect(T.X, Snap(Y) - 1.f, T.W, 2.f), FV::Col(40, 40, 60, uint8(90.f + 120.f * Pulse(Time, 4.f))));
					const float Dy = FMath::Lerp(T.Y, Y, G);
					D.Circle(FVector2D(Cx, Dy), 2.5f, FV::Col(200, 225, 255, 230));
					D.Rect(FFVRect(Cx - 1.f, Dy - 6.f, 2.f, 4.f), FV::Col(200, 225, 255, 170));
				}
				for (int32 i = 0; i < H.Spans.Num(); ++i)
				{
					const FFVRect R = M.Span(float(H.Spans[i].X), float(H.Spans[i].Y));
					const float A = H.SpanLife.IsValidIndex(i) ? FMath::Clamp(H.SpanLife[i] * 2.f, 0.f, 1.f) : 1.f;
					const FFVSprite* SS = Sp ? &Sp->HazardSprite(EFVHazard::Drip, 0) : nullptr;
					if (SS && SS->IsValid()) { D.SpriteRect(*SS, R.Inset(1.f), WithA(FLinearColor::White, A)); }
					else
					{
						D.Rect(R.Inset(1.f), FV::Col(110, 110, 118, uint8(230.f * A)));
						D.Frame(R.Inset(1.f), FV::Col(40, 40, 50, uint8(230.f * A)), 1.f);
					}
				}
				break;
			}
			case EFVHazard::Spring:
			{
				// «Подводный ключ»: a pale column that pushes up — rising bubbles and «^»
				FFVRect R = H.Spans.Num() > 0 ? M.Span(float(H.Spans[0].X), float(H.Spans[0].Y)) : M.Span(H.Pos - H.Radius, H.Pos + H.Radius);
				D.Rect(R, FV::Col(200, 240, 255, uint8(40.f + 40.f * FMath::Clamp(H.Strength, 0.f, 1.f))));
				for (int32 k = 0; k < 5; ++k)
				{
					const float P = FMath::Frac(Time * 0.8f + k / 5.f);
					const FVector2D B(T.X + 3.f + Hash01(k * 17 + 4) * (T.W - 6.f), R.Bottom() - P * R.H);
					D.Ring(B, 1.5f, 1.5f, FV::Col(235, 250, 255, uint8(200.f * (1.f - P))), 1.f);
				}
				const float Off = FMath::Fmod(Time * 20.f, 12.f);
				for (float Y = R.Bottom() - 4.f - Off; Y > R.Y + 3.f; Y -= 12.f) { Chevron(D, Cx, Y, T.W * 0.2f, false, FV::Col(235, 250, 255, 170)); }
				break;
			}
			default:
				break;   // Weeds / Darkness go over the fish (HazardsOver)
			}
		}
	}

	void HazardsOver(FVDraw& D, const FFVExpertFight& F, const FTrackMap& M, float Time)
	{
		const FFVRect& T = M.T;
		for (const FFVExpertHazardState& H : F.Hazards)
		{
			if (H.Kind != EFVHazard::Weeds) { continue; }
			// «Водоросли»: green sections with swaying stalks (the fish inside is hidden by FishAlpha)
			for (const FVector2D& S : H.Spans)
			{
				const FFVRect R = M.Span(float(S.X), float(S.Y));
				D.Rect(R, FV::Col(40, 110, 50, 120));
				for (float X = R.X + 3.f; X < R.Right() - 1.f; X += 5.f)
				{
					const float Sway = 1.5f * FMath::Sin(Time * 2.f + X * 0.4f);
					D.Line(FVector2D(X, R.Bottom() - 1.f), FVector2D(X + Sway, R.Y + 2.f), FV::Col(110, 180, 90, 150), 1.f);
				}
			}
		}
		bool bDark = false;
		for (const FFVExpertHazardState& H : F.Hazards) { bDark |= H.Kind == EFVHazard::Darkness; }
		if (!bDark) { return; }
		// «Темнота»: only the rows near the bar(s) are lit (FFVExpertFight::IsLit); a soft band at the edge
		constexpr float Row = 3.f;
		const int32 N = FMath::Max(1, FMath::CeilToInt(T.H / Row));
		TArray<bool, TInlineAllocator<320>> Lit;
		Lit.SetNum(N);
		for (int32 i = 0; i < N; ++i) { Lit[i] = F.IsLit((i + 0.5f) * Row / T.H * FVExpert::TrackH); }
		for (int32 i = 0; i < N; ++i)
		{
			const float Y = T.Y + i * Row;
			const float H = FMath::Min(Row, T.Bottom() - Y);
			if (!Lit[i]) { D.Rect(FFVRect(T.X, Y, T.W, H), FV::Col(4, 8, 18, 238)); continue; }
			const bool bEdge = (i > 0 && !Lit[i - 1]) || (i + 1 < N && !Lit[i + 1]) || (i > 1 && !Lit[i - 2]) || (i + 2 < N && !Lit[i + 2]);
			if (bEdge) { D.Rect(FFVRect(T.X, Y, T.W, H), FV::Col(4, 8, 18, 110)); }
		}
	}

	// ------------------------------------------------------------------ bars
	void DrawBars(FVDraw& D, const FFVExpertFight& F, const FTrackMap& M, float Time)
	{
		const FFVRect& T = M.T;
		const EFVExpertRodType Type = F.Setup.Rod.Type;
		const bool bLegend = F.IsLegend();
		const int32 NZ = FMath::Clamp(F.NumZones, 0, 2);
		FFVRect Rs[2];
		for (int32 i = 0; i < NZ; ++i)
		{
			const FFVExpertZone& Z = F.Zones[i];
			if (!Z.bActive) { continue; }
			const bool bHot = F.IsInZone(F.FishPos, i) && F.FishAlpha > 0.f;
			const float Y0 = Snap(M.Y(Z.Top())), Y1 = Snap(M.Y(Z.Bottom()));
			const FFVRect R(T.X + 1.f, Y0, T.W - 2.f, FMath::Max(3.f, Y1 - Y0));
			Rs[i] = R;
			// the classic catch bar (gold while a legend is on the hook), brighter with the fish inside
			const FLinearColor BarC = bLegend ? (bHot ? FV::Col(255, 208, 64, 230) : FV::Col(150, 110, 40, 150))
				: (bHot ? FV::Col(136, 240, 92, 225) : FV::Col(96, 186, 70, 160));
			D.Rect(R, BarC);
			D.Rect(FFVRect(R.X, R.Y, R.W, 2.f), FMath::Lerp(BarC, FLinearColor::White, 0.45f));
			D.Rect(FFVRect(R.X, R.Bottom() - 2.f, R.W, 2.f), WithA(FMath::Lerp(BarC, FLinearColor::Black, 0.35f), 0.9f));
			D.Rect(FFVRect(R.X + 2.f, R.Y + 3.f, 2.f, FMath::Max(0.f, R.H - 6.f)), FV::Col(255, 255, 255, bHot ? 110 : 60));
			// the tension shows on the bar outline too, in the shared warning colours
			if (F.Tension > 0.3f)
			{
				const float TA = FMath::Min(1.f, (F.Tension - 0.3f) / 0.5f) * (F.Tension > 0.8f ? 0.6f + 0.4f * Pulse(Time, 5.f) : 1.f);
				D.Frame(R, WithA(TensionColor(F.Tension), TA), 1.f);
			}
			const float Cx = R.X + R.W * 0.5f, Cy = R.Y + R.H * 0.5f;
			const FLinearColor Cue = FV::Col(30, 70, 20, 200);
			const float AL = FMath::Clamp(R.H * 0.3f, 4.f, 10.f);
			switch (Type)
			{
			case EFVExpertRodType::Pendulum:
				// where the gravity pulls now
				Arrow(D, FVector2D(Cx, Cy + (F.bGravityUp ? AL * 0.5f : -AL * 0.5f)), FVector2D(Cx, Cy + (F.bGravityUp ? -AL * 0.5f : AL * 0.5f)), Cue, 1.5f);
				break;
			case EFVExpertRodType::Twin:
				if (FMath::Abs(Z.Vel) > 15.f)
				{
					const bool bUp = Z.Vel < 0.f;
					Arrow(D, FVector2D(Cx, Cy + (bUp ? AL * 0.5f : -AL * 0.5f)), FVector2D(Cx, Cy + (bUp ? -AL * 0.5f : AL * 0.5f)), Cue, 1.5f);
				}
				break;
			case EFVExpertRodType::Compass:
				// «Импульс»: a short flash of the push under the bar
				if (F.PushAge < 0.3f)
				{
					const float A = 1.f - F.PushAge / 0.3f;
					D.Rect(FFVRect(R.X, R.Bottom() - 2.f, R.W, 2.f), FV::Col(255, 255, 255, uint8(230.f * A)));
					for (int32 k = -1; k <= 1; ++k)
					{
						const float X = Cx + k * R.W * 0.3f;
						D.Line(FVector2D(X, R.Bottom() + 1.f), FVector2D(X + k * 2.f, R.Bottom() + 3.f + 8.f * A), FV::Col(255, 255, 240, uint8(220.f * A)), 1.5f);
					}
				}
				break;
			case EFVExpertRodType::Helm:
				// «Ползунок»: grip lines (the knob outside the track is drawn by Draw)
				for (int32 k = -1; k <= 1; ++k) { D.Rect(FFVRect(Snap(Cx - R.W * 0.25f), Snap(Cy + k * 3.f), Snap(R.W * 0.5f), 1.f), Cue); }
				break;
			default:
				break;
			}
		}
		// «Двойчатка»: the overlap (x1,5) is framed
		if (NZ == 2 && F.Zones[0].bActive && F.Zones[1].bActive)
		{
			const float A = FMath::Max(Rs[0].Y, Rs[1].Y), B = FMath::Min(Rs[0].Bottom(), Rs[1].Bottom());
			if (B - A > 2.f)
			{
				const FFVRect O(Rs[0].X, A, Rs[0].W, B - A);
				D.Frame(O, F.bInOverlap ? WithA(FVPal::Gold(), 0.7f + 0.3f * Pulse(Time, 3.f)) : FV::Col(255, 255, 255, 90), 1.f);
			}
		}
		// «Магнит»: force lines from the bar to the fish while pulling
		if (Type == EFVExpertRodType::Magnet && F.bMagnetHeld && F.Zones[0].bActive && NZ > 0)
		{
			const FFVExpertZone& Z = F.Zones[0];
			const float Fy = M.Y(F.FishPos);
			const float By = F.FishPos < Z.Y ? M.Y(Z.Top()) : M.Y(Z.Bottom());
			if (FMath::Abs(Fy - By) > 4.f)
			{
				const float A = 0.45f + 0.45f * FMath::Clamp(F.HeldT, 0.f, 1.f);
				const FLinearColor LC = FV::Col(190, 225, 255, uint8(255.f * A));
				const float Cx = T.X + T.W * 0.5f;
				constexpr int32 Seg = 14;
				const int32 Shift = int32(Time * 14.f);
				for (int32 L = 0; L < 3; ++L)
				{
					const float Bulge = (L - 1) * T.W * 0.36f;
					FVector2D Prev(Cx, Fy);
					for (int32 s = 1; s <= Seg; ++s)
					{
						const float U = float(s) / Seg;
						const FVector2D P(Cx + Bulge * FMath::Sin(U * PI), FMath::Lerp(Fy, By, U));
						if (((s + Shift) & 1) == 0) { D.Line(Prev, P, LC, 1.f); }
						Prev = P;
					}
				}
			}
		}
	}

	// ------------------------------------------------------------------ fish
	void Bubbles(FVDraw& D, const FVector2D& P, float Time, int32 Count, float Alpha)
	{
		for (int32 k = 0; k < Count; ++k)
		{
			const float Ph = FMath::Fmod(Time * 0.85f + float(k) / Count, 1.f);
			const FVector2D B = P + FVector2D(FMath::Sin(Time * 3.f + k * 2.1f) * 2.f + (k - Count * 0.5f) * 1.5f, -4.f - Ph * 16.f);
			D.Ring(B, 1.2f + Ph, 1.2f + Ph, FV::Col(235, 250, 255, uint8(220.f * FMath::Clamp(Alpha * (1.f - Ph), 0.f, 1.f))), 1.f);
		}
	}

	/** The classic fish icon seen from the side (a legend: its own sprite with a gold glow). */
	void FishIcon(FVDraw& D, UFVSprites* Sp, const FFVExpertFight& F, const FVector2D& C, float K, float Ang, const FLinearColor& Tint, float TrackW)
	{
		if (!Sp) { return; }
		if (F.IsLegend())
		{
			D.Glow(C, 12.f * K, WithA(FV::Col(255, 200, 80), 0.4f * Tint.A));
			const FFVFishDef* Def = FVDB::FindFish(F.Setup.Fish.FishId);
			const FFVSprite* S = Def ? &Sp->Fish(*Def) : nullptr;
			if (S && S->IsValid())
			{
				const float Sc = FMath::Min(K, (TrackW + 10.f) / FMath::Max(1.f, float(S->W)));
				D.SpriteCentered(*S, C + FVector2D(1.f, 1.f), Sc, WithA(FLinearColor::Black, 0.6f * Tint.A), false, Ang);
				D.SpriteCentered(*S, C, Sc, Tint, false, Ang);
				return;
			}
			D.SpriteCentered(Sp->Icon(EFVIcon::Fish), C + FVector2D(1.f, 1.f), K, WithA(FLinearColor::Black, 0.6f * Tint.A), false, Ang);
		}
		D.SpriteCentered(Sp->Icon(EFVIcon::Fish), C, K, Tint, false, Ang);
	}

	void DrawFish(FVDraw& D, UFVSprites* Sp, const FFVExpertFight& F, const FTrackMap& M, float K, float Time)
	{
		const FFVRect& T = M.T;
		const float Cx = Snap(T.X + T.W * 0.5f);
		const float PerTick = F.FishVel / 60.f;
		float Ang = F.bFrozen ? 0.f : FMath::Clamp(PerTick * 5.f, -28.f, 28.f) + (FMath::Abs(PerTick) > 0.6f ? FMath::Sin(Time * 18.f) * 6.f : 0.f);
		const float Shake = F.bTrembling ? FMath::Sin(Time * 70.f) * 1.5f : 0.f;
		const FVector2D FC(Snap(Cx + Shake), Snap(M.Y(F.FishPos)));
		const float FA = FMath::Clamp(F.FishAlpha, 0.f, 1.f);

		// the aim of a move: Рывок tip (red), Свеча / Нырок landing (pale)
		if (F.MoveIndex >= 0 && (F.bFishRed || F.bTrembling))
		{
			const bool bDash = F.Pattern == EFVExpertPattern::Dash || F.bFishRed;
			if (bDash || F.Pattern == EFVExpertPattern::Jump || F.Pattern == EFVExpertPattern::Dive)
			{
				const float Y = Snap(M.Y(FMath::Clamp(F.MoveB, 0.f, FVExpert::TrackH)));
				const FLinearColor C = bDash ? WithA(WarnRed(), 0.55f + 0.4f * Pulse(Time, 4.f)) : FV::Col(255, 255, 255, 150);
				for (float X = T.X + 1.f; X < T.Right() - 1.f; X += 4.f) { D.Rect(FFVRect(X, Y - 1.f, 2.f, 2.f), C); }
			}
		}
		// Обманка: the double (the real fish gives itself away by bubbles)
		if (F.bGhost)
		{
			const FVector2D GC(Cx, Snap(M.Y(F.GhostPos)));
			FishIcon(D, Sp, F, GC, K, -Ang, FLinearColor(1.f, 1.f, 1.f, 0.8f), T.W);
		}
		// Нырок: a silt cloud where it lies
		if (F.bFrozen)
		{
			for (int32 k = 0; k < 5; ++k)
			{
				const float P = FMath::Frac(Time * 0.5f + k / 5.f);
				D.Rect(FFVRect(Snap(Cx - 8.f + Hash01(k * 5 + 1) * 16.f), Snap(FC.Y + 4.f - P * 6.f), 2.f, 2.f), FV::Col(150, 120, 80, uint8(160.f * (1.f - P))));
			}
		}
		if (F.bFishRed && FA > 0.02f) { D.Ring(FC, 10.f * K, 10.f * K, WithA(WarnRed(), (0.55f + 0.4f * Pulse(Time, 4.f)) * FMath::Max(FA, 0.5f)), 1.5f); }
		if (FA > 0.02f)
		{
			const FLinearColor Tint = F.bFishRed ? FLinearColor(1.f, 0.45f, 0.4f, FA) : WithA(FLinearColor::White, FA);
			FishIcon(D, Sp, F, FC, K, Ang, Tint, T.W);
		}
		if (F.bFishRed)
		{
			// the way of the dash
			const bool bUp = F.MoveB < F.FishPos;
			const float Y0 = FC.Y + (bUp ? -9.f * K : 9.f * K);
			Arrow(D, FVector2D(Cx, Y0), FVector2D(Cx, Y0 + (bUp ? -9.f : 9.f)), WarnRed(), 2.f);
		}
		// Свеча: out of the water at the top — drops around the fish
		if (F.bFishAirborne)
		{
			for (int32 k = 0; k < 4; ++k)
			{
				const float P = FMath::Frac(Time * 1.6f + k * 0.25f);
				const float Sx = (k & 1 ? 1.f : -1.f) * (6.f + P * 6.f);
				D.Rect(FFVRect(Snap(Cx + Sx), Snap(FC.Y + 6.f - FMath::Sin(P * PI) * 8.f), 2.f, 2.f), FV::Col(235, 250, 255, uint8(220.f * (1.f - P))));
			}
			D.Rect(FFVRect(T.X, T.Y, T.W, 2.f), FV::Col(255, 255, 255, 170));
		}
		// hidden (Прячется / weeds / darkness) or a decoy is out: bubbles give the real fish away
		if (F.bBubbles || F.bGhost || FA < 0.5f) { Bubbles(D, FVector2D(Cx, FC.Y), Time, 4, 1.f); }
	}

	// ------------------------------------------------------------------ outside the panel
	/** The move signal: a dark plate with the pictogram beside the panel (far side), the move's name under it. */
	void DrawSignal(FVDraw& D, UFVSprites* Sp, const FFVExpertFight& F, const FFVRect& P, float K, float Y0)
	{
		if (F.Signal == EFVIcon::Count || !Sp) { return; }
		const float T = F.SignalT;
		const float A = T < 2.5f ? 1.f : FMath::Lerp(1.f, 0.4f, FMath::Clamp(T - 2.5f, 0.f, 1.f));
		const float Sz = Snap(18.f * K) + 4.f;
		float L, R;
		SideSpan(P, true, L, R);
		const bool bFar = R - L >= Sz;
		if (!bFar) { SideSpan(P, false, L, R); }
		const bool bLeftOfPanel = R <= P.X;
		const float X = bLeftOfPanel ? R - Sz : L;
		const FFVRect B(Snap(X), Snap(Y0), Sz, Sz);
		const bool bDash = F.Signal == EFVIcon::WarnDash;
		D.Rect(B.Offset(1.f, 1.f), FV::Col(0, 0, 0, uint8(90.f * A)));
		D.Rect(B, FV::Col(24, 16, 10, uint8(215.f * A)));
		D.Frame(B, WithA(bDash ? WarnRed() : WarnYellow(), A * (0.6f + 0.4f * Pulse(T, 3.f))), 1.f);
		const FFVSprite& Ic = Sp->Icon(F.Signal);
		const float Pop = 1.f + 0.3f * FMath::Clamp(1.f - T / 0.25f, 0.f, 1.f);
		if (Ic.IsValid()) { D.SpriteCentered(Ic, B.Center(), (Sz - 4.f) / FMath::Max(1.f, float(FMath::Max(Ic.W, Ic.H))) * Pop, WithA(FLinearColor::White, A)); }
		if (F.SignalName.IsEmpty()) { return; }
		const float Size = FV::Ui::Small();
		const float MaxW = FMath::Clamp(R - L, 40.f, 110.f);
		const TArray<FString> Lines = Wrap(D, F.SignalName, MaxW, Size);
		const float LH = D.LineHeight(Size, true);
		float Y = B.Bottom() + 2.f;
		for (int32 i = 0; i < FMath::Min(2, Lines.Num()); ++i)
		{
			const float Tx = bLeftOfPanel ? R : L;
			D.TextOutlined(Lines[i], FVector2D(Snap(Tx), Snap(Y)), Size, WithA(FVPal::White(), A), bLeftOfPanel ? EFVAlign::Right : EFVAlign::Left);
			Y += LH;
		}
	}

	void GiveUpButton(FVDraw& D, const FFVRect& R)
	{
		// the look of FVDraw::CloseButton without taking the click (the caller tests GiveUpRect)
		const bool bHover = !FV::IsTouchUI() && R.Contains(D.Mouse());
		D.Rect(R.Offset(1.f, 1.f), FV::Col(0, 0, 0, 70));
		D.Rect(R, FV::Col(84, 22, 18));
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, R.H - 3.f), bHover ? FV::Col(232, 96, 72) : FV::Col(204, 66, 52));
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(250, 150, 120));
		const float XS = R.W / 16.f;
		const float XX = R.X + 4.5f * XS, XY = R.Y + 4.f * XS;
		for (int32 i = 0; i < 7; ++i)
		{
			D.Rect(FFVRect(XX + i * XS, XY + i * XS, 1.5f * XS, XS), FV::Col(255, 244, 236));
			D.Rect(FFVRect(XX + (6 - i) * XS, XY + i * XS, 1.5f * XS, XS), FV::Col(255, 244, 236));
		}
	}

	/** Procedural «ghost» pointing hand, fingertip at Tip. */
	void DrawHand(FVDraw& D, const FVector2D& Tip, bool bPressed, float A)
	{
		const FVector2D P = Tip + FVector2D(0.f, bPressed ? 1.f : 0.f);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const float G = Pass == 0 ? 1.5f : 0.f;
			const FLinearColor C = Pass == 0 ? FV::Col(20, 40, 70, uint8(120.f * A)) : FV::Col(235, 245, 255, uint8(190.f * A));
			D.Circle(P + FVector2D(0.f, 3.f), 3.5f + G, C);
			D.Rect(FFVRect(float(P.X) - 3.5f - G, float(P.Y) + 3.f - G, 7.f + 2.f * G, 14.f + 2.f * G), C);
			D.Circle(P + FVector2D(4.f, 21.f), 10.f + G, C);
			D.Circle(P + FVector2D(-7.f, 17.f), 3.5f + G, C);
		}
		if (bPressed) { D.Ring(Tip, 9.f, 9.f, FV::Col(255, 255, 255, uint8(200.f * A)), 1.5f); }
	}

	const TCHAR* HowToText(EFVExpertRodType Type)
	{
		switch (Type)
		{
		case EFVExpertRodType::Pendulum: return TEXT("Нажми — полоса полетит в другую сторону");
		case EFVExpertRodType::Twin:     return TEXT("Держи — одна вверх, другая вниз");
		case EFVExpertRodType::Compass:  return TEXT("Жми — толчок вверх, потом полоса падает");
		case EFVExpertRodType::Magnet:   return TEXT("Держи — рыба тянется к полосе");
		case EFVExpertRodType::Helm:     return TEXT("Веди пальцем вверх-вниз — полоса следом");
		default: return TEXT("");
		}
	}

	/** The soft dark plate + outlined lines (the hint / banner look). Returns the plate. */
	FFVRect PlateText(FVDraw& D, const TArray<FString>& Lines, float Cx, float Y, float Size, const FLinearColor& C, const FLinearColor& Ink, float A)
	{
		const float LH = D.LineHeight(Size, true);
		float TW = 0.f;
		for (const FString& Ln : Lines) { TW = FMath::Max(TW, float(D.MeasureText(Ln, Size).X)); }
		const FFVRect Plate(Snap(Cx - TW * 0.5f - 5.f), Snap(Y - 2.f), Snap(TW + 10.f), Snap(LH * Lines.Num() + 3.f));
		D.Rect(Plate, WithA(FV::Col(24, 18, 14), 0.6f * A));
		for (int32 i = 0; i < Lines.Num(); ++i) { D.TextOutlined(Lines[i], FVector2D(Snap(Cx), Snap(Y + i * LH)), Size, C, EFVAlign::Center, Ink); }
		return Plate;
	}
}

// ============================================================================================ layout
FFVRect FVExpertDraw::FieldRect(float HeroScreenX)
{
	using namespace FVExpertDrawImpl;
	GHeroX = HeroScreenX;
	const bool bPortrait = FV::IsPortrait();
	const float K = PanelK();
	const float AreaTop = FV::TopBarH + 4.f, AreaBottom = FV::VH - FV::BottomBarH - 4.f;
	const float PW = Snap(52.f * K);
	float PX, PY, PH;
	if (bPortrait)
	{
		// ~1.3x the classic 300 K, kept under a banner line and above the bottom dock
		const float Top = AreaTop + BannerRoom;
		PH = Snap(FMath::Min(390.f * K, AreaBottom - Top));
		const float Gap = 40.f;
		const float RoomR = FV::VW - (HeroScreenX + Gap) - 4.f;
		const float RoomL = HeroScreenX - Gap - 16.f * K;
		const bool bRight = RoomR >= PW && (RoomR >= RoomL || RoomL < PW);
		PX = bRight ? HeroScreenX + Gap : HeroScreenX - Gap - PW;
		PX = FMath::Clamp(PX, 16.f * K, FV::VW - PW - 4.f);
		PY = Top + FMath::Max(0.f, (AreaBottom - Top - PH) * 0.5f);
	}
	else
	{
		PH = Snap(FMath::Min(300.f, AreaBottom - AreaTop));
		const bool bRight = HeroScreenX + 38.f + PW + 8.f < FV::VW;
		PX = bRight ? HeroScreenX + 38.f : HeroScreenX - 38.f - PW;
		PX = FMath::Clamp(PX, 18.f, FV::VW - PW - 4.f);
		PY = FV::TopBarH + (FV::VH - FV::TopBarH - FV::BottomBarH - PH) * 0.5f;
	}
	return FFVRect(Snap(PX), Snap(PY), PW, FMath::Max(80.f, PH));
}

FFVRect FVExpertDraw::TrackRect(const FFVRect& Field)
{
	using namespace FVExpertDrawImpl;
	const float K = KOf(Field);
	const float Top = TopMargin(K);
	return FFVRect(Snap(Field.X + 7.f * K), Field.Y + Top, Snap(22.f * K), FMath::Max(10.f, Field.H - Top - Snap(10.f * K)));
}

float FVExpertDraw::TrackToScreen(const FFVRect& Field, float TrackY)
{
	const FFVRect T = TrackRect(Field);
	return T.Y + TrackY / FVExpert::TrackH * T.H;
}

float FVExpertDraw::ScreenToTrack(const FFVRect& Field, float ScreenY)
{
	const FFVRect T = TrackRect(Field);
	return FMath::Clamp((ScreenY - T.Y) / FMath::Max(1.f, T.H) * FVExpert::TrackH, 0.f, FVExpert::TrackH);
}

FFVRect FVExpertDraw::ControlRect(const FFVRect& Field)
{
	return FFVRect(0.f, FV::TopBarH, FV::VW, FMath::Max(0.f, FV::VH - FV::TopBarH));
}

FFVRect FVExpertDraw::FrameRect(const FFVRect& Field)
{
	return Field;
}

FFVRect FVExpertDraw::ProgressBarRect(const FFVRect& Field)
{
	using namespace FVExpertDrawImpl;
	const float K = KOf(Field);
	const FFVRect T = TrackRect(Field);
	return FFVRect(Snap(Field.X + 33.f * K), T.Y, Snap(6.f * K), T.H);
}

FFVRect FVExpertDraw::TensionBarRect(const FFVRect& Field)
{
	using namespace FVExpertDrawImpl;
	const float K = KOf(Field);
	const FFVRect T = TrackRect(Field);
	return FFVRect(Snap(Field.X + 42.f * K), T.Y, FMath::Max(3.f, Snap(4.f * K)), T.H);
}

FFVRect FVExpertDraw::GiveUpRect(const FFVRect& Field)
{
	using namespace FVExpertDrawImpl;
	const float BS = FV::IsPortrait() ? 30.f : 16.f;
	const float Gap = FV::IsPortrait() ? 6.f : 4.f;
	const bool bRight = PanelRightOfHero(Field);
	const bool bRoomR = Field.Right() + Gap + BS <= FV::VW - 3.f, bRoomL = Field.X - Gap - BS >= 3.f;
	const bool bGoRight = bRight ? bRoomR || !bRoomL : !bRoomL && bRoomR;
	return FFVRect(bGoRight ? Field.Right() + Gap : Field.X - Gap - BS, Field.Y, BS, BS);
}

// ============================================================================================ the fight
void FVExpertDraw::Draw(FVDraw& D, const FFVExpertFight& F, EFVExpertTheme Theme, const FFVRect& Field, float Time)
{
	using namespace FVExpertDrawImpl;
	if (Field.W <= 1.f || Field.H <= 1.f) { return; }
	UFVSprites* Sp = UFVSprites::Get();
	const float K = KOf(Field);
	const FFVRect T = TrackRect(Field);
	const FTrackMap M{ T };
	const bool bLegend = F.IsLegend();
	// Р54: the rod only gives the thin accent stripe + the type badge
	const FLinearColor Accent = Theme == EFVExpertTheme::Classic ? FLinearColor::Transparent : Lin(UFVSprites::ExpertThemeStyle(Theme).Frame);

	// ---- visual memory: a new fight resets it; a phase change flashes the track frame
	if (Mem.LastElapsed < 0.f || F.Elapsed < Mem.LastElapsed) { Mem = FDrawMemory(); Mem.LastTime = Time; Mem.LastPhase = F.Phase; }
	const float Dt = FMath::Clamp(Time - Mem.LastTime, 0.f, 0.1f);
	Mem.LastTime = Time;
	Mem.LastElapsed = F.Elapsed;
	Mem.PhaseFlash = FMath::Max(0.f, Mem.PhaseFlash - Dt);
	if (F.Phase != Mem.LastPhase) { Mem.LastPhase = F.Phase; Mem.PhaseFlash = 1.2f; }

	// ---- the wooden panel, the type badge in its top margin (+ ×1,5 next to it)
	Panel(D, Field, K);
	const float BS = FMath::Max(10.f, TopMargin(K) - 5.f);
	const FFVRect Badge(Field.X + Snap(4.f * K), Field.Y + 3.f, BS, BS);
	TypeBadge(D, Badge, F.Setup.Rod.Type, Accent);
	if (F.bInOverlap || (F.bFishRed && F.bInZone))
	{
		D.TextOutlined(TEXT("×1,5"), FVector2D(Badge.Right() + 3.f, Badge.Y + 1.f), FV::Ui::Small(), FVPal::Gold());
	}

	// ---- the water track (classic tone; a legend gets the classic dark veil so gold reads)
	D.Rect(T.Inset(-1.f), FVPal::WoodDark());
	if (Accent.A > 0.f) { D.Frame(T.Inset(-2.f), Accent, 1.f); }
	{
		UFVGame* G = UFVGame::Get();
		const FFVLocationDef* Loc = G ? FVDB::FindLocation(G->GetScene().GetLocationId()) : nullptr;
		const FLinearColor Sh = FV::Col(Loc ? Loc->WaterShallow : FColor(90, 170, 220));
		const FLinearColor Dp = FV::Col(Loc ? Loc->WaterDeep : FColor(30, 80, 150));
		constexpr int32 Bands = 8;
		for (int32 k = 0; k < Bands; ++k)
		{
			const float Y0 = Snap(T.Y + T.H * float(k) / float(Bands)), Y1 = Snap(T.Y + T.H * float(k + 1) / float(Bands));
			D.Rect(FFVRect(T.X, Y0, T.W, Y1 - Y0), FMath::Lerp(Sh, Dp, float(k) / float(Bands - 1)));
		}
	}
	D.PushClip(T);
	if (bLegend) { D.Rect(T, FV::Col(0, 0, 0, 115)); }
	else
	{
		for (int32 k = 0; k < 6; ++k)
		{
			const float Hs = Hash01(k * 7 + 3);
			const float Y = T.Bottom() - FMath::Frac(Time * (0.09f + Hs * 0.08f) + Hs) * T.H;
			const float X = T.X + 3.f + Hs * (T.W - 6.f) + FMath::Sin(Time * 3.f + float(k)) * 1.5f;
			D.Rect(FFVRect(Snap(X), Snap(Y), 2.f, 2.f), FV::Col(255, 255, 255, 80));
		}
	}
	HazardsUnder(D, Sp, F, M, Time);
	DrawBars(D, F, M, Time);
	if (Sp && F.bTreasureVisible)
	{
		// the chest drifts on the track with its tiny meter (hold it in a bar, §10.16)
		const FVector2D C(Snap(T.X + T.W * 0.5f + 3.f * K), Snap(M.Y(F.TreasurePos)));
		D.SpriteCentered(Sp->Chest(F.bTreasureCaught ? 1 : 0), C, 0.75f * K, WithA(FLinearColor::White, FMath::Clamp(F.TreasureAppear * 3.f + (F.bTreasureCaught ? 1.f : 0.f), 0.f, 1.f)));
		if (!F.bTreasureCaught)
		{
			const FFVRect Tm(Snap(C.X - 7.f * K), Snap(C.Y + 7.f * K), Snap(14.f * K), FMath::Max(2.f, Snap(3.f * K)));
			D.Rect(Tm, FV::Col(50, 34, 26));
			D.Rect(FFVRect(Tm.X, Tm.Y, Snap(Tm.W * FMath::Clamp(F.TreasureProgress, 0.f, 1.f)), Tm.H), FVPal::Gold());
		}
	}
	D.PopClip();

	// ---- the fish (unclipped like the classic icon: a legend sprite may be wider than the track)
	DrawFish(D, Sp, F, M, K, Time);

	// ---- weeds / darkness over the fish
	D.PushClip(T);
	HazardsOver(D, F, M, Time);
	D.PopClip();

	// ---- the Slider knob on the wood left of the track + where the finger leads it
	if (F.Setup.Rod.Type == FVExpert::Slider && F.Zones[0].bActive)
	{
		const float Y = Snap(M.Y(F.Zones[0].Y));
		const FFVRect Kn(T.X - Snap(6.f * K), Y - Snap(5.f * K), Snap(5.f * K), Snap(10.f * K));
		D.Rect(Kn.Offset(1.f, 1.f), FV::Col(0, 0, 0, 80));
		D.Rect(Kn, FVPal::WoodDark());
		D.Rect(Kn.Inset(1.f), FVPal::WoodLight());
		if (F.SliderTarget >= 0.f)
		{
			const float Ty = Snap(M.Y(F.SliderTarget));
			D.Rect(FFVRect(T.X - Snap(6.f * K), Ty, Snap(3.f * K), 1.f), FV::Col(255, 255, 255, 170));
		}
	}

	// ---- the phase change: the track frame pulses (gold)
	if (Mem.PhaseFlash > 0.f)
	{
		const float A = FMath::Min(1.f, Mem.PhaseFlash) * (0.55f + 0.45f * FMath::Sin(Time * 14.f));
		D.Frame(T.Inset(-1.f), WithA(FV::Col(255, 208, 64), A), 1.f);
	}

	// ---- meters: progress (classic) and the narrow tension one (yellow -> red, pulsing near the snap)
	Meter(D, ProgressBarRect(Field), F.Progress, MeterColor(F.Progress));
	const float Ten = FMath::Clamp(F.Tension, 0.f, 1.f);
	const FFVRect TR = TensionBarRect(Field);
	FLinearColor TF = TensionColor(Ten);
	if (Ten > 0.8f) { TF = FMath::Lerp(TF, FLinearColor::White, 0.35f * Pulse(Time, 5.f)); }
	Meter(D, TR, Ten, TF);
	if (Ten > 0.8f && !F.Setup.bNoSnap && !F.Setup.bTutorial) { D.Frame(TR.Inset(-2.f), WithA(WarnRed(), 0.5f + 0.5f * Pulse(Time, 5.f)), 1.f); }

	// ---- the classic reel handle at the bottom-left of the panel
	{
		const FVector2D RC(Field.X - 3.f * K, Field.Bottom() - 28.f * K);
		D.Circle(RC, 10.f * K, FVPal::WoodDark());
		D.Circle(RC, 8.5f * K, FV::Col(176, 176, 186));
		D.Circle(RC, 6.f * K, FV::Col(140, 140, 152));
		D.Circle(RC, 2.5f * K, FV::Col(90, 90, 100));
		const float RA = Time * (F.bInZone ? 9.f : 2.f);
		const FVector2D Arm = RC + FVector2D(FMath::Cos(RA), FMath::Sin(RA)) * 7.f * K;
		D.Line(RC, Arm, FV::Col(60, 60, 70), 2.f);
		D.Circle(Arm, 2.5f * K, FVPal::WoodLight());
	}

	// ---- the move signal beside the panel (under the [X] of a training fight)
	const float SigY = F.Setup.bTutorial ? GiveUpRect(Field).Bottom() + 6.f : T.Y;
	DrawSignal(D, Sp, F, Field, K, SigY);

	if (F.Setup.bTutorial) { GiveUpButton(D, GiveUpRect(Field)); }
}

void FVExpertDraw::DrawTutorialHand(FVDraw& D, const FFVExpertFight& F, const FFVRect& Field, float T)
{
	using namespace FVExpertDrawImpl;
	if (T < 0.f || T >= TutorialHandSec || Field.W <= 1.f) { return; }
	const float A = FMath::Clamp(T / 0.25f, 0.f, 1.f) * FMath::Clamp((TutorialHandSec - T) / 0.4f, 0.f, 1.f);
	float L, R;
	bool bFar;
	TextSpan(Field, 110.f, L, R, bFar);
	const FFVRect Tr = TrackRect(Field);
	FVector2D Tip((L + R) * 0.5f, Tr.Y + Tr.H * 0.45f);
	bool bPressed = false;
	switch (F.Setup.Rod.Type)
	{
	case EFVExpertRodType::Pendulum: bPressed = FMath::Fmod(T, 1.f) >= 0.35f && FMath::Fmod(T, 1.f) < 0.5f; break;
	case EFVExpertRodType::Twin:     bPressed = FMath::Fmod(T, 1.5f) >= 0.3f && FMath::Fmod(T, 1.5f) < 1.1f; break;
	case EFVExpertRodType::Compass:  bPressed = FMath::Fmod(T, 0.6f) >= 0.3f && FMath::Fmod(T, 0.6f) < 0.42f; break;
	case EFVExpertRodType::Magnet:   bPressed = FMath::Fmod(T, 1.5f) >= 0.3f && FMath::Fmod(T, 1.5f) < 1.3f; break;
	default:
		// the Slider: the finger slides up and down, the bar follows it
		bPressed = T >= 0.3f;
		Tip.Y = Tr.Y + Tr.H * (0.5f + 0.25f * FMath::Sin(FMath::Max(0.f, T - 0.3f) * 2.4f));
		break;
	}
	if (bPressed && F.Setup.Rod.Type != FVExpert::Slider)
	{
		const float P = FMath::Fmod(T * 3.f, 1.f);
		D.Ring(Tip, 8.f + P * 14.f, 8.f + P * 14.f, FV::Col(255, 255, 255, uint8(160.f * A * (1.f - P))), 1.5f);
	}
	DrawHand(D, Tip, bPressed, A);
}

void FVExpertDraw::DrawHowTo(FVDraw& D, EFVExpertRodType Type, const FFVRect& Field, float T)
{
	using namespace FVExpertDrawImpl;
	if (T < 0.f || T >= HowToSec || Field.W <= 1.f) { return; }
	const FString Caption = HowToText(Type);
	if (Caption.IsEmpty()) { return; }
	const float A = FMath::Clamp(T / 0.25f, 0.f, 1.f) * FMath::Clamp((HowToSec - T) / 0.6f, 0.f, 1.f);
	float L, R;
	bool bFar;
	TextSpan(Field, 110.f, L, R, bFar);
	const float MaxW = FMath::Max(40.f, R - L - 10.f);
	float Size = FV::Ui::Body();
	TArray<FString> Lines = Wrap(D, Caption, MaxW, Size);
	if (Lines.Num() > 3) { Size = FV::Ui::Small(); Lines = Wrap(D, Caption, MaxW, Size); }
	// at the panel top, clear of the signal pictogram (it sits at the top of the far side)
	float Y = Field.Y + 2.f;
	if (bFar) { Y = Field.Y + Field.H * 0.3f; }
	PlateText(D, Lines, (L + R) * 0.5f, Y, Size, WithA(FVPal::White(), A), FV::Col(30, 18, 12, uint8(255.f * A)), A);
}

void FVExpertDraw::DrawBanner(FVDraw& D, const FString& Text, float Age, float Life, const FFVRect& Field)
{
	using namespace FVExpertDrawImpl;
	if (Text.IsEmpty() || Age < 0.f || Age >= Life || Field.W <= 1.f) { return; }
	const float A = FMath::Min(1.f, Age / 0.12f) * FMath::Min(1.f, (Life - Age) / 0.3f);
	const FLinearColor Gold = WithA(FV::Col(255, 208, 64), A);
	const FLinearColor Ink = FV::Col(60, 30, 5, uint8(255.f * A));
	if (FV::IsPortrait())
	{
		float Size = FV::Ui::Body() + 1.f;
		while (D.MeasureText(Text, Size).X > FV::VW - 20.f && Size > FV::Ui::Small()) { Size -= 0.5f; }
		const float LH = D.LineHeight(Size, true);
		const float W = D.MeasureText(Text, Size).X;
		const float Cx = FMath::Clamp(Field.X + Field.W * 0.5f, 8.f + W * 0.5f, FV::VW - 8.f - W * 0.5f);
		const bool bAbove = Field.Y - (FV::TopBarH + 2.f) >= LH + 3.f;
		const float Y = bAbove ? Field.Y - LH - 3.f : Field.Bottom() + 3.f;
		PlateText(D, { Text }, Cx, Y, Size, Gold, Ink, A);
		return;
	}
	// landscape: beside the panel, away from the hero when roomy (else the other side of the hero), <= 2 lines
	float L, R;
	bool bFar;
	TextSpan(Field, 80.f, L, R, bFar);
	const float MaxW = FMath::Clamp(R - L - 10.f, 40.f, 170.f);
	float Size = FV::Ui::Body() + 1.f;
	TArray<FString> Lines = Wrap(D, Text, MaxW, Size);
	while (Lines.Num() > 2 && Size > FV::Ui::Small()) { Size -= 0.5f; Lines = Wrap(D, Text, MaxW, Size); }
	const bool bLeftOfPanel = R <= Field.X;
	const float Cx = bFar ? (bLeftOfPanel ? R - MaxW * 0.5f : L + MaxW * 0.5f) : (L + R) * 0.5f;
	PlateText(D, Lines, Cx, Field.Y + Field.H * 0.5f, Size, Gold, Ink, A);
}
