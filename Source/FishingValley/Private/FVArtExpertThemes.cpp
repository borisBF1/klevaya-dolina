// UPDATE 6 [ART-THEMES] — the fishing screen of the expert rods and the field hazards (Docs/UPDATE6_FOREST_CITY.md §10.7, §10.12).
// Р54 replaces the lush themes of Р32: every screen is the classic wooden panel; a theme only gives the accent colour
// (FVArtThemesImpl::Accent). The Expert* sprites below are therefore plain (wood ring, classic water, classic green zone).
// Contract: Public/FVSprites.h «UPDATE 6».
// Sizes (texels; 2 px / texel):
//   ExpertFrame      S x S ring, 4 texels: wood (dark / base / light) + a 1-texel accent stripe; transparent centre.
//   ExpertWater      S x S opaque, classic track tone (8 bands) + bubbles, 4 frames.
//   ExpertZoneSprite band W x H or circle W x W, the classic green; bHot = brighter.
//   ExpertBarFrame   Len x 6 (or 6 x Len): 1-texel dark wood ring.   ExpertParticle 8 x 8 bubble, 4 frames.
//   ExpertOrnament   16 x 16 plain wood plate with the accent ring.   ExpertJoystick base 32 x 32, knob 16 x 16.
//   HazardSprite     Current 14x3 streak, Whirlpool 32x32, Wave 12x12 foam (stretched), Piles 10x10 side-view snag,
//                    Weeds 16x16, Darkness 16x16, Drip 8x7 stone, Spring 16x16; 4 frames each.
// Edition 2 (§19, Р60): FVExpertDraw draws the fight on the classic vertical panel itself; of these sprites it uses
// only HazardSprite Piles (a snag in each «Коряги» section) and Drip (the stones of «Капель»). The rest stay for the
// contract (Public/FVSprites.h) and are unused by the 1D fight.
// Red and yellow are never used by a theme: they are the shared warning colours (§10.12).

#include "FVSprites.h"
#include "FVDatabase.h"

namespace FVArtThemesImpl
{
	constexpr int32 Ring = 4;
	const FColor White(255, 255, 255);

	FColor A(FColor C, int32 Alpha) { C.A = uint8(FMath::Clamp(Alpha, 0, 255)); return C; }

	// Р54: the expert screen is the classic brown wooden panel; a rod only adds a thin accent stripe (+ a type badge,
	// drawn by FVExpertDraw). The theme table colours give that accent only.
	const FColor WoodD(92, 44, 22), WoodM(160, 84, 36), WoodL(214, 128, 56);
	const FColor WaterSh(90, 170, 220), WaterDp(30, 80, 150);   // the classic minigame track (default location water)
	const FColor ZoneG(136, 240, 92), ZoneGDim(96, 186, 70);    // the classic catch bar

	/** The accent stripe colour of a theme (never red / yellow: those are the warning colours). */
	FColor Accent(EFVExpertTheme T)
	{
		switch (T)
		{
		case EFVExpertTheme::Breeze:   return FColor(64, 190, 180);
		case EFVExpertTheme::Twins:    return FColor(90, 130, 220);
		case EFVExpertTheme::Surf:     return FColor(40, 140, 140);
		case EFVExpertTheme::Rhumb:    return FColor(224, 172, 62);
		case EFVExpertTheme::Mirror:   return FColor(205, 212, 226);
		case EFVExpertTheme::Anchor:   return FColor(120, 124, 144);
		case EFVExpertTheme::Helmsman: return FColor(250, 222, 140);
		case EFVExpertTheme::Pole:     return FColor(190, 160, 250);
		default:                       return WoodL;
		}
	}

	void Disc(FFVImage& I, float CX, float CY, float R, FColor C)
	{
		for (int32 Y = FMath::FloorToInt(CY - R); Y <= FMath::CeilToInt(CY + R); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - R); X <= FMath::CeilToInt(CX + R); ++X)
			{
				const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
				if (DX * DX + DY * DY <= R * R) { I.Blend(X, Y, C); }
			}
		}
	}

	void RingAt(FFVImage& I, float CX, float CY, float R0, float R1, FColor C)
	{
		for (int32 Y = FMath::FloorToInt(CY - R1); Y <= FMath::CeilToInt(CY + R1); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - R1); X <= FMath::CeilToInt(CX + R1); ++X)
			{
				const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
				const float D2 = DX * DX + DY * DY;
				if (D2 <= R1 * R1 && D2 >= R0 * R0) { I.Blend(X, Y, C); }
			}
		}
	}

	// ------------------------------------------------------------------ frame: plain wood ring + the accent stripe
	FFVImage Frame(EFVExpertTheme T, int32 S)
	{
		S = FMath::Clamp(S, 24, 600);
		FFVImage I(S, S);
		const FColor Ac = Accent(T);
		for (int32 Y = 0; Y < S; ++Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const int32 Dm = FMath::Min(FMath::Min(X, Y), FMath::Min(S - 1 - X, S - 1 - Y));
				if (Dm >= Ring) { continue; }
				FColor C = WoodM;
				if (Dm == 0 || Dm == Ring - 1) { C = WoodD; }
				else if (Dm == 1 && (Y == 1 || X == 1)) { C = WoodL; }
				else if (Dm == 2 && T != EFVExpertTheme::Classic) { C = Ac; }
				I.Set(X, Y, C);
			}
		}
		return I;
	}

	// ------------------------------------------------------------------ water: the classic track tone, 8 bands
	FFVImage Water(EFVExpertTheme, int32 S, int32 F)
	{
		S = FMath::Clamp(S, 8, 600);
		FFVImage I(S, S);
		constexpr int32 Bands = 8;
		for (int32 Y = 0; Y < S; ++Y)
		{
			const int32 B = FMath::Clamp(Y * Bands / S, 0, Bands - 1);
			const FColor C = FVArt::Mix(WaterSh, WaterDp, float(B) / float(Bands - 1));
			for (int32 X = 0; X < S; ++X) { I.Set(X, Y, C); }
		}
		// a few rising bubbles (like the classic track)
		for (int32 k = 0; k < 6; ++k)
		{
			const int32 X = (k * 37 + 11) % FMath::Max(1, S - 2);
			const int32 Y = ((k * 53 + 7) + S - (F & 3) * (S / 16)) % FMath::Max(1, S - 2);
			I.Blend(X, Y, A(White, 80)); I.Blend(X + 1, Y, A(White, 80));
		}
		return I;
	}

	bool IsCircle(EFVExpertRodType Type)
	{
		return Type == EFVExpertRodType::Compass || Type == EFVExpertRodType::Magnet || Type == EFVExpertRodType::Helm;
	}

	// ------------------------------------------------------------------ zone: the classic green (same for every theme)
	FFVImage Zone(EFVExpertTheme, EFVExpertRodType Type, int32 W, int32 H, bool bHot)
	{
		const FColor Fill = A(bHot ? ZoneG : ZoneGDim, bHot ? 150 : 110);
		const FColor Edge = bHot ? FVArt::Mix(ZoneG, White, 0.45f) : ZoneGDim;
		if (IsCircle(Type))
		{
			const int32 S = FMath::Clamp(FMath::Max(W, H), 6, 256);
			FFVImage I(S, S);
			const float C = S * 0.5f;
			Disc(I, C, C, C - 0.5f, Fill);
			RingAt(I, C, C, C - 1.5f, C - 0.5f, Edge);
			return I;
		}
		W = FMath::Clamp(W, 2, 512); H = FMath::Clamp(H, 2, 512);
		FFVImage I(W, H, Fill);
		for (int32 X = 0; X < W; ++X) { I.Set(X, 0, Edge); I.Set(X, H - 1, FVArt::Mix(ZoneGDim, FColor(0, 0, 0), 0.35f)); }
		return I;
	}

	// ------------------------------------------------------------------ bar frame: 1-texel dark wood ring
	FFVImage BarFrame(EFVExpertTheme, bool bVertical, int32 Len)
	{
		Len = FMath::Clamp(Len, 4, 1024);
		FFVImage I(bVertical ? 6 : Len, bVertical ? Len : 6);
		for (int32 Y = 0; Y < I.H; ++Y)
		{
			for (int32 X = 0; X < I.W; ++X)
			{
				if (X == 0 || Y == 0 || X == I.W - 1 || Y == I.H - 1) { I.Set(X, Y, WoodD); }
			}
		}
		return I;
	}

	// ------------------------------------------------------------------ particle: a small pale bubble
	FFVImage Particle(EFVExpertTheme, int32 F)
	{
		FFVImage I(8, 8);
		const int32 Al = 150 - (F & 3) * 25;
		I.Set(3, 3, A(White, Al)); I.Set(4, 3, A(White, Al)); I.Set(3, 4, A(White, Al)); I.Set(4, 4, A(White, Al / 2));
		return I;
	}

	// ------------------------------------------------------------------ ornament: a small plain accent plate (no emblems)
	FFVImage Ornament(EFVExpertTheme T)
	{
		FFVImage I(16, 16);
		Disc(I, 8.f, 8.f, 7.5f, WoodD);
		Disc(I, 8.f, 8.f, 6.5f, WoodM);
		RingAt(I, 8.f, 8.f, 5.f, 6.f, Accent(T));
		return I;
	}

	// ------------------------------------------------------------------ joystick: plain wood base + knob
	FFVImage Joystick(EFVExpertTheme T, bool bKnob)
	{
		const int32 S = bKnob ? 16 : 32;
		FFVImage I(S, S);
		const float C = S * 0.5f;
		if (bKnob)
		{
			Disc(I, C, C, C - 0.5f, WoodD);
			Disc(I, C, C, C - 1.5f, WoodM);
			Disc(I, C - 2.f, C - 2.f, C * 0.35f, WoodL);
			return I;
		}
		Disc(I, C, C, C - 0.5f, A(WoodD, 150));
		RingAt(I, C, C, C - 2.f, C - 0.5f, WoodL);
		RingAt(I, C, C, C - 3.f, C - 2.f, A(Accent(T), T == EFVExpertTheme::Classic ? 0 : 200));
		return I;
	}

	// ------------------------------------------------------------------ hazards
	FFVImage Hazard(EFVHazard Hz, int32 F)
	{
		F &= 3;
		const float Ph = F * (PI * 0.5f);
		const FColor Foam(240, 250, 255);
		switch (Hz)
		{
		case EFVHazard::Current:
		{
			FFVImage I(14, 3);
			for (int32 X = 0; X < 14; ++X)
			{
				const float T = float(X) / 13.f;
				const int32 Al = int32(220.f * T * (0.8f + 0.2f * FMath::Sin(Ph + X)));
				I.Set(X, 1, A(FColor(220, 240, 255), Al));
				if (X > 8) { I.Set(X, 0, A(FColor(160, 210, 240), Al / 2)); I.Set(X, 2, A(FColor(100, 160, 210), Al / 2)); }
			}
			I.Set(13, 1, Foam);
			return I;
		}
		case EFVHazard::Whirlpool:
		{
			FFVImage I(32, 32);
			for (int32 Y = 0; Y < 32; ++Y)
			{
				for (int32 X = 0; X < 32; ++X)
				{
					const float DX = X + 0.5f - 16.f, DY = Y + 0.5f - 16.f, R = FMath::Sqrt(DX * DX + DY * DY);
					if (R > 15.5f) { continue; }
					const float Arm = FMath::Sin(FMath::Atan2(DY, DX) * 3.f + R * 0.55f - Ph);
					I.Blend(X, Y, A(FColor(8, 30, 60), int32(150.f * (1.f - R / 15.5f))));
					if (Arm > 0.55f) { I.Blend(X, Y, A(Foam, int32(200.f * (R / 15.5f) * (Arm - 0.4f)))); }
				}
			}
			return I;
		}
		case EFVHazard::Wave:
		{
			FFVImage I(12, 12);
			for (int32 Y = 0; Y < 12; ++Y)
			{
				for (int32 X = 0; X < 12; ++X)
				{
					const float DX = FMath::Abs(X + 0.5f - 6.f) / 6.f, DY = FMath::Abs(Y + 0.5f - 6.f) / 6.f;
					const float Core = 1.f - FMath::Max(DX, DY * 0.4f);
					if (Core <= 0.f) { continue; }
					const float N = FVArt::Noise(X, (Y + F * 3) % 12, 17);
					const FColor C = N > 0.6f ? Foam : FColor(170, 220, 240);
					I.Set(X, Y, A(C, int32(255.f * FMath::Min(1.f, Core * 1.6f) * (N > 0.25f ? 1.f : 0.55f))));
				}
			}
			return I;
		}
		case EFVHazard::Piles:
		{
			// Р60 (1D): a snag seen from the side — a crooked branch with two stubs, dark bark, a wet highlight
			FFVImage I(10, 10);
			const FColor Bark(70, 46, 30), Wood(128, 90, 54);
			for (int32 X = 0; X < 10; ++X)
			{
				const int32 Y = 6 - X / 3;             // the trunk rises to the right
				I.Set(X, Y, Wood);
				I.Set(X, Y + 1, Bark);
			}
			for (int32 k = 0; k < 3; ++k) { I.Set(3 - k / 2, 5 - k, Wood); I.Set(7 + k / 2, 3 + k, Bark); }   // two stubs
			I.Set(2, 6, FColor(196, 156, 106));
			I.Blend((F & 1) ? 8 : 1, 8, A(Foam, 170));
			return I;
		}
		case EFVHazard::Weeds:
		{
			FFVImage I(16, 16);
			for (int32 s = 0; s < 6; ++s)
			{
				const int32 X0 = 1 + s * 3 - (s & 1);
				const FColor C = (s & 1) ? FColor(56, 130, 60) : FColor(84, 160, 72);
				for (int32 Y = 15; Y >= 1 + (s % 3); --Y)
				{
					const int32 Sw = FMath::RoundToInt(FMath::Sin(Ph + s + Y * 0.35f) * (15 - Y) * 0.12f);
					I.Set(X0 + Sw, Y, A(C, 230));
					if ((Y + s) % 4 == 0) { I.Set(X0 + Sw + ((s & 1) ? -1 : 1), Y, A(FVArt::Light(C, 0.3f), 220)); }
				}
			}
			return I;
		}
		case EFVHazard::Darkness:
		{
			FFVImage I(16, 16);
			Disc(I, 8.f, 8.f, 8.f, FColor(4, 8, 18, 150));
			Disc(I, 8.f, 8.f, 5.f, FColor(4, 8, 18, 220));
			return I;
		}
		case EFVHazard::Drip:
		{
			// the fallen stone, top view
			FFVImage I(8, 7);
			static const TCHAR* Rows[7] = { TEXT("..oooo.."), TEXT(".oLLMMo."), TEXT("oLLMMMNo"), TEXT("oLMMMNNo"), TEXT("oMMMNNNo"), TEXT(".oNNNNo."), TEXT("..oooo..") };
			for (int32 Y = 0; Y < 7; ++Y)
			{
				for (int32 X = 0; X < 8; ++X)
				{
					const TCHAR Ch = Rows[Y][X];
					const FColor C = Ch == TEXT('o') ? FColor(50, 46, 60) : Ch == TEXT('L') ? FColor(150, 146, 164)
						: Ch == TEXT('M') ? FColor(112, 106, 124) : Ch == TEXT('N') ? FColor(80, 76, 94) : FColor(0, 0, 0, 0);
					I.Set(X, Y, C);
				}
			}
			if (F == 1 || F == 2) { I.Set(2, 1, FColor(200, 210, 230)); }
			return I;
		}
		case EFVHazard::Spring:
		{
			FFVImage I(16, 16);
			Disc(I, 8.f, 8.f, 7.8f, FColor(200, 240, 255, 60));
			for (int32 k = 0; k < 3; ++k)
			{
				const float R = 1.f + FMath::Fmod(1.f + k * 2.2f + F * 0.55f, 6.5f);
				RingAt(I, 8.f, 8.f, R - 0.7f, R, A(Foam, int32(200.f * (1.f - R / 8.f))));
			}
			Disc(I, 8.f, 8.f, 1.8f, A(Foam, 230));
			return I;
		}
		default:
			break;
		}
		return FFVImage(8, 8, FColor(255, 255, 255, 120));
	}
}

FFVExpertThemeStyle UFVSprites::ExpertThemeStyle(EFVExpertTheme Theme)
{
	// Р54: every expert screen looks like the classic wooden panel (default style = classic wood / water / green);
	// Frame* carry only the rod's accent stripe colour.
	FFVExpertThemeStyle S;
	if (Theme == EFVExpertTheme::Classic || Theme >= EFVExpertTheme::Count) { return S; }
	const FColor Ac = FVArtThemesImpl::Accent(Theme);
	S.Frame = Ac; S.FrameDark = FVArt::Mix(Ac, FColor(0, 0, 0), 0.4f); S.FrameLight = FVArt::Mix(Ac, FColor(255, 255, 255), 0.4f);
	return S;
}
const FFVSprite& UFVSprites::ExpertFrame(EFVExpertTheme Theme, int32 SizeTexels)
{
	const uint64 Key = MakeKey(FVSpriteCat::ExpertTheme, uint32(Theme), uint32(FMath::Clamp(SizeTexels, 0, 4095)), 1);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildExpertFrame(Theme, SizeTexels), FString::Printf(TEXT("ExpertFrame_%d_%d"), int32(Theme), SizeTexels));
}
const FFVSprite& UFVSprites::ExpertWater(EFVExpertTheme Theme, int32 SizeTexels, int32 Frame)
{
	const int32 Fr = Frame & 3;
	const uint64 Key = MakeKey(FVSpriteCat::ExpertTheme, uint32(Theme) | (uint32(Fr) << 8), uint32(FMath::Clamp(SizeTexels, 0, 4095)), 2);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtThemesImpl::Water(Theme, SizeTexels, Fr), FString::Printf(TEXT("ExpertWater_%d_%d_%d"), int32(Theme), SizeTexels, Fr));
}
const FFVSprite& UFVSprites::ExpertZoneSprite(EFVExpertTheme Theme, EFVExpertRodType Type, int32 WTexels, int32 HTexels, bool bHot)
{
	const uint32 W = uint32(FMath::Clamp(WTexels, 0, 4095)), H = uint32(FMath::Clamp(HTexels, 0, 4095));
	const uint64 Key = MakeKey(FVSpriteCat::ExpertTheme, uint32(Theme) | (uint32(Type) << 8) | (bHot ? 1u << 16 : 0u), W | (H << 12), 3);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtThemesImpl::Zone(Theme, Type, WTexels, HTexels, bHot), FString::Printf(TEXT("ExpertZone_%d_%d"), int32(Theme), int32(Type)));
}
const FFVSprite& UFVSprites::ExpertBarFrame(EFVExpertTheme Theme, bool bVertical, int32 LenTexels)
{
	const uint64 Key = MakeKey(FVSpriteCat::ExpertTheme, uint32(Theme) | (bVertical ? 1u << 8 : 0u), uint32(FMath::Clamp(LenTexels, 0, 4095)), 4);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtThemesImpl::BarFrame(Theme, bVertical, LenTexels), FString::Printf(TEXT("ExpertBar_%d"), int32(Theme)));
}
const FFVSprite& UFVSprites::ExpertParticle(EFVExpertTheme Theme, int32 Frame)
{
	const uint64 Key = MakeKey(FVSpriteCat::ExpertTheme, uint32(Theme), uint32(Frame & 3), 5);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtThemesImpl::Particle(Theme, Frame), FString::Printf(TEXT("ExpertFx_%d_%d"), int32(Theme), Frame & 3));
}
const FFVSprite& UFVSprites::ExpertOrnament(EFVExpertTheme Theme)
{
	const uint64 Key = MakeKey(FVSpriteCat::ExpertTheme, uint32(Theme), 0, 6);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtThemesImpl::Ornament(Theme), FString::Printf(TEXT("ExpertOrnament_%d"), int32(Theme)));
}
const FFVSprite& UFVSprites::ExpertJoystick(EFVExpertTheme Theme, bool bKnob)
{
	const uint64 Key = MakeKey(FVSpriteCat::ExpertTheme, uint32(Theme), bKnob ? 1u : 0u, 7);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtThemesImpl::Joystick(Theme, bKnob), FString::Printf(TEXT("ExpertJoystick_%d_%d"), int32(Theme), bKnob ? 1 : 0));
}
const FFVSprite& UFVSprites::HazardSprite(EFVHazard Hazard, int32 Frame)
{
	const uint64 Key = MakeKey(FVSpriteCat::Hazard, uint32(Hazard), uint32(Frame & 3));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtThemesImpl::Hazard(Hazard, Frame), FString::Printf(TEXT("Hazard_%d_%d"), int32(Hazard), Frame & 3));
}
FFVImage UFVSprites::BuildExpertFrame(EFVExpertTheme Theme, int32 SizeTexels)
{
	return FVArtThemesImpl::Frame(Theme, SizeTexels);
}
