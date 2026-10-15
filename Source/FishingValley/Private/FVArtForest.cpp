// UPDATE 6 [ART-FOREST] — the forest: big world, trees (4 kinds x states), the windfall (Docs/UPDATE6_FOREST_CITY.md §4, Р38). Owner: ART-FOREST.
// Contract: Public/FVSprites.h «UPDATE 6», Private/FVWorldU6.h.
// Style: pixel art like FVSceneArt.cpp / FVArtHome.cpp (Stardew-like shading via FVArt::Shade/Light, 1px dark outline
// on props), deterministic (hash noise with constant seeds), cached through UFVSprites::AddCached(MakeKey(FVSpriteCat::<cat>, ...)).
//
// ==================================================================================================================
// SIZE / PIVOT TABLE (texels; world sprites are drawn at FV::WorldScale = 2). Pivot = bottom-centre.
// ------------------------------------------------------------------------------------------------------------------
//  TreeSprite(Kind, Full | Marked, Variant 0..3)  one canvas per kind (Full and Marked share it):
//      Birch 26x50 · Pine 32x58 · Oak 36x46 · Spruce (вековая ель) 38x78.  Collision (suggested): trunk base 8x4.
//      Marked = the same tree + a red-white paint ring and a light notch; notch centre = TreeNotchPoint(Kind, Variant).
//  TreeSprite(Kind, Stump, V)    16x10 (annual rings, kind bark)      Collision: 12x4
//  TreeSprite(Kind, Sapling, V)  12x20                                 Collision: none
//  BlockageSprite(false)         96x40 windfall: four crossed trunks, an uprooted root disc, an axe sign
//  BlockageSprite(true)          96x40 cleared: sawn logs rolled to the sides, 3 logs stacked, sawdust
//      Collision of the windfall (not cleared): its Hit rect (it closes the gap of the ravine).
//  World (FVWorldU6::BuildForest): 1120x352 texels — the stream + bridge (x 0..36, exit to the river), the edge
//      (опушка, x 36..368), the ravine with the windfall (x 368..466), the thicket (чаща, x 466..1120) with the forester's
//      cordon (x ~640) and the builder's yard (x ~905). Walk band y 100..300; canopy above, bushes / gully below.
// ==================================================================================================================
//
// FVArtKitU6 (below) is a small shared toolkit of the update-6 world painters (noise, blobs, ground, paths, stamps).
// It has external linkage; FVArtYard.cpp / FVArtHouse.cpp declare the parts they use (no shared header in the contract).

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVWorldU6.h"

// =====================================================================================================================
// Shared update-6 art kit (defined here, declared again in FVArtYard.cpp / FVArtHouse.cpp)
// =====================================================================================================================
namespace FVArtKitU6
{
	uint32 HashU(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16;
		return X;
	}
	float R01(int32 X, int32 Y, uint32 S)
	{
		const uint32 H = HashU((uint32(X) * 0x9E3779B1U) ^ HashU((uint32(Y) * 0x85EBCA77U) ^ (S * 0xC2B2AE3DU)));
		return float(H & 0xFFFFFFU) / 16777216.f;
	}
	float VNoise(float X, float Y, uint32 S)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = X - float(X0), FY = Y - float(Y0);
		const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY);
		const float A = R01(X0, Y0, S), B = R01(X0 + 1, Y0, S), C = R01(X0, Y0 + 1, S), D = R01(X0 + 1, Y0 + 1, S);
		return FMath::Lerp(FMath::Lerp(A, B, SX), FMath::Lerp(C, D, SX), SY);
	}
	float Fbm(float X, float Y, uint32 S)
	{
		return VNoise(X, Y, S) * 0.6f + VNoise(X * 2.1f, Y * 2.1f, S + 17) * 0.3f + VNoise(X * 4.3f, Y * 4.3f, S + 31) * 0.1f;
	}
	float Bayer(int32 X, int32 Y)
	{
		static const uint8 M[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
		return (float(M[Y & 3][X & 3]) + 0.5f) / 16.f;
	}
	FColor Rgb(int32 R, int32 G, int32 B)
	{
		return FColor(uint8(FMath::Clamp(R, 0, 255)), uint8(FMath::Clamp(G, 0, 255)), uint8(FMath::Clamp(B, 0, 255)), 255);
	}
	FColor WithA(FColor C, int32 A) { C.A = uint8(FMath::Clamp(A, 0, 255)); return C; }
	FColor Mul(FColor C, float F)
	{
		return FColor(uint8(FMath::Clamp(FMath::RoundToInt(C.R * F), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(C.G * F), 0, 255)),
			uint8(FMath::Clamp(FMath::RoundToInt(C.B * F), 0, 255)), C.A);
	}
	void MulPx(FFVImage& I, int32 X, int32 Y, float F)
	{
		if (I.InBounds(X, Y)) { I.Set(X, Y, Mul(I.Get(X, Y), F)); }
	}
	void MixPx(FFVImage& I, int32 X, int32 Y, FColor C, float T)
	{
		if (I.InBounds(X, Y)) { const FColor O = I.Get(X, Y); I.Set(X, Y, WithA(FVArt::Mix(O, C, T), O.A)); }
	}
	/** Tone 0 = highlight, 1 = base, 2 = shade, 3 = deep (as LeafPal in FVSceneArt.cpp). */
	FColor LeafTone(FColor Base, int32 Tone)
	{
		switch (Tone)
		{
		case 0:  return FVArt::Mix(Base, Rgb(232, 240, 150), 0.32f);
		case 2:  return FVArt::Mix(Base, Rgb(20, 52, 44), 0.32f);
		case 3:  return FVArt::Mix(Base, Rgb(16, 36, 36), 0.56f);
		default: return Base;
		}
	}
	FColor CrownOutline(FColor Leaf) { return FVArt::Mix(LeafTone(Leaf, 3), Rgb(10, 20, 24), 0.5f); }

	/** Darkens an elliptic area (soft dithered rim). */
	void ShadeEllipse(FFVImage& I, float CX, float CY, float RX, float RY, float F)
	{
		if (RX <= 0.f || RY <= 0.f) { return; }
		for (int32 Y = FMath::FloorToInt(CY - RY); Y <= FMath::CeilToInt(CY + RY); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
			{
				const float DX = (float(X) + 0.5f - CX) / RX, DY = (float(Y) + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY;
				if (D > 1.f) { continue; }
				if (D > 0.6f && Bayer(X, Y) > (1.f - D) / 0.4f) { continue; }
				MulPx(I, X, Y, F);
			}
		}
	}
	void FillEllipse(FFVImage& I, float CX, float CY, float RX, float RY, FColor C, bool bBlend)
	{
		for (int32 Y = FMath::FloorToInt(CY - RY); Y <= FMath::CeilToInt(CY + RY); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
			{
				const float DX = (float(X) + 0.5f - CX) / FMath::Max(RX, 0.5f), DY = (float(Y) + 0.5f - CY) / FMath::Max(RY, 0.5f);
				if (DX * DX + DY * DY <= 1.f)
				{
					if (bBlend) { I.Blend(X, Y, C); } else { I.Set(X, Y, C); }
				}
			}
		}
	}
	/** Shaded blob lit from the top-left (crowns, bushes, rocks). */
	void PaintBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Hi, FColor Base, FColor Lo, FColor Deep, uint32 Seed, float Jag, float Noise)
	{
		RX = FMath::Max(RX, 0.6f); RY = FMath::Max(RY, 0.6f);
		for (int32 Y = FMath::FloorToInt(CY - RY - 1.f); Y <= FMath::CeilToInt(CY + RY + 1.f); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX - 1.f); X <= FMath::CeilToInt(CX + RX + 1.f); ++X)
			{
				const float DX = (float(X) + 0.5f - CX) / RX, DY = (float(Y) + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY;
				if (D > 1.f - Jag * R01(X >> 1, Y >> 1, Seed)) { continue; }
				const float L = -(DX * 0.55f + DY * 0.83f) + (R01(X, Y, Seed + 7) - 0.5f) * Noise;
				I.Set(X, Y, L > 0.45f ? Hi : (L > -0.2f ? Base : (L > -0.62f ? Lo : Deep)));
			}
		}
	}
	void LeafBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Leaf, uint32 Seed)
	{
		PaintBlob(I, CX, CY, RX, RY, LeafTone(Leaf, 0), Leaf, LeafTone(Leaf, 2), LeafTone(Leaf, 3), Seed, 0.18f, 0.35f);
	}
	void RockBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Base, uint32 Seed)
	{
		PaintBlob(I, CX, CY, RX, RY, FVArt::Mix(Base, Rgb(255, 255, 255), 0.28f), Base, Mul(Base, 0.76f), Mul(Base, 0.56f), Seed, 0.12f, 0.2f);
	}
	/** Lit left / top edge and shaded right / bottom edge of every opaque pixel that borders transparency. */
	void EdgeShade(FFVImage& I, float HiAmt, float LoAmt)
	{
		const FFVImage Src = I;
		for (int32 Y = 0; Y < I.H; ++Y)
		{
			for (int32 X = 0; X < I.W; ++X)
			{
				const FColor C = Src.Get(X, Y);
				if (C.A == 0) { continue; }
				if (Src.Get(X - 1, Y).A == 0 || Src.Get(X, Y - 1).A == 0) { I.Set(X, Y, WithA(FVArt::Mix(C, Rgb(255, 255, 255), HiAmt), C.A)); }
				else if (Src.Get(X + 1, Y).A == 0 || Src.Get(X, Y + 1).A == 0) { I.Set(X, Y, Mul(C, 1.f - LoAmt)); }
			}
		}
	}
	/** Thick line made of square stamps. */
	void ThickLine(FFVImage& I, FVector2D A, FVector2D B, float W0, float W1, FColor C)
	{
		const float Len = float(FVector2D::Distance(A, B));
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len * 2.f));
		for (int32 i = 0; i <= Steps; ++i)
		{
			const float F = float(i) / float(Steps);
			const FVector2D P = A + (B - A) * F;
			const int32 H = FMath::Max(0, FMath::RoundToInt(FMath::Lerp(W0, W1, F) * 0.5f - 0.5f));
			const int32 PX = FMath::FloorToInt(float(P.X)), PY = FMath::FloorToInt(float(P.Y));
			for (int32 DY = -H; DY <= H; ++DY)
			{
				for (int32 DX = -H; DX <= H; ++DX) { I.Set(PX + DX, PY + DY, C); }
			}
		}
	}
	/** 3-tone ground noise (Lo / Base / Hi, Bayer-dithered borders) + sparse speckles, in [X0, X1) x [Y0, Y1). */
	void FillGround(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, FColor Lo, FColor Base, FColor Hi, float Scale, uint32 Seed)
	{
		X0 = FMath::Max(0, X0); Y0 = FMath::Max(0, Y0); X1 = FMath::Min(I.W, X1); Y1 = FMath::Min(I.H, Y1);
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				const float V = Fbm(float(X) / Scale, float(Y) / Scale, Seed) + (Bayer(X, Y) - 0.5f) * 0.1f;
				FColor C = V < 0.4f ? Lo : (V > 0.6f ? Hi : Base);
				const float S = R01(X, Y, Seed + 3);
				if (S < 0.012f) { C = FVArt::Shade(C, 0.25f); }
				else if (S > 0.992f) { C = FVArt::Light(C, 0.22f); }
				I.Set(X, Y, C);
			}
		}
	}
	/** Grass tufts (3..5 texels, lit tip) scattered over a rect. */
	void Tufts(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Count, FColor Base, uint32 Seed)
	{
		const FColor Hi = FVArt::Light(Base, 0.35f), Lo = FVArt::Shade(Base, 0.3f);
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 X = X0 + FMath::FloorToInt(R01(i, 1, Seed) * float(FMath::Max(1, X1 - X0)));
			const int32 Y = Y0 + FMath::FloorToInt(R01(i, 2, Seed) * float(FMath::Max(1, Y1 - Y0)));
			const int32 H = 2 + FMath::FloorToInt(R01(i, 3, Seed) * 2.f);
			I.Set(X, Y, Lo); I.Set(X, Y - 1, Base); if (H > 2) { I.Set(X, Y - 2, Hi); }
			I.Set(X - 1, Y, Lo); I.Set(X - 1, Y - 1, Hi);
			I.Set(X + 1, Y, Lo); if (H > 2) { I.Set(X + 1, Y - 1, Base); I.Set(X + 2, Y - 2, Hi); }
		}
	}
	/** Small flowers (petals around a yellow / orange centre). */
	void Flowers(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Count, uint32 Seed)
	{
		static const FColor Petals[6] = { FColor(250, 250, 244), FColor(255, 214, 80), FColor(240, 110, 130), FColor(170, 140, 240), FColor(120, 180, 250), FColor(255, 160, 70) };
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 X = X0 + FMath::FloorToInt(R01(i, 5, Seed) * float(FMath::Max(1, X1 - X0)));
			const int32 Y = Y0 + FMath::FloorToInt(R01(i, 6, Seed) * float(FMath::Max(1, Y1 - Y0)));
			const FColor P = Petals[FMath::FloorToInt(R01(i, 7, Seed) * 5.99f)];
			I.Set(X, Y + 2, Rgb(60, 120, 50));
			I.Set(X - 1, Y, P); I.Set(X + 1, Y, P); I.Set(X, Y - 1, FVArt::Light(P, 0.3f)); I.Set(X, Y + 1, FVArt::Shade(P, 0.25f));
			I.Set(X, Y, (P.R > 240 && P.G > 200) ? Rgb(240, 150, 60) : Rgb(255, 230, 110));
		}
	}
	/** Trodden earth path along a polyline (the colours of PaintPath in FVSceneArt.cpp), soft broken edges. */
	void PaintPath(FFVImage& I, const TArray<FVector2D>& Way, float Width, uint32 Seed)
	{
		const FColor Lo = Rgb(152, 114, 74), Mid = Rgb(180, 140, 94), Hi = Rgb(204, 166, 116);
		for (int32 s = 0; s + 1 < Way.Num(); ++s)
		{
			const FVector2D A = Way[s], B = Way[s + 1];
			const int32 BX0 = FMath::FloorToInt(float(FMath::Min(A.X, B.X)) - Width), BX1 = FMath::CeilToInt(float(FMath::Max(A.X, B.X)) + Width);
			const int32 BY0 = FMath::FloorToInt(float(FMath::Min(A.Y, B.Y)) - Width), BY1 = FMath::CeilToInt(float(FMath::Max(A.Y, B.Y)) + Width);
			const FVector2D AB = B - A;
			const float L2 = FMath::Max(0.001f, float(AB.SizeSquared()));
			for (int32 Y = BY0; Y <= BY1; ++Y)
			{
				for (int32 X = BX0; X <= BX1; ++X)
				{
					if (!I.InBounds(X, Y)) { continue; }
					const FVector2D P(float(X) + 0.5f, float(Y) + 0.5f);
					const float T = FMath::Clamp(float(FVector2D::DotProduct(P - A, AB)) / L2, 0.f, 1.f);
					const float D = float(FVector2D::Distance(P, A + AB * T));
					const float Edge = Width * 0.5f * (0.82f + 0.3f * VNoise(float(X) * 0.25f, float(Y) * 0.25f, Seed));
					if (D > Edge) { continue; }
					const float V = VNoise(float(X) * 0.3f, float(Y) * 0.3f, Seed + 5) + (Bayer(X, Y) - 0.5f) * 0.2f;
					FColor C = D > Edge - 1.2f ? Lo : (V > 0.62f ? Hi : (V < 0.3f ? Lo : Mid));
					if (R01(X, Y, Seed + 9) < 0.03f) { C = Rgb(214, 204, 186); }   // pebble
					I.Set(X, Y, C);
				}
			}
		}
	}
	/** Dark rim of the last Band texels of the world (x0.85 -> x0.55) so the black beyond does not look cut off. */
	void DarkRim(FFVImage& I, int32 Band)
	{
		for (int32 Y = 0; Y < I.H; ++Y)
		{
			for (int32 X = 0; X < I.W; ++X)
			{
				const int32 D = FMath::Min(FMath::Min(X, I.W - 1 - X), FMath::Min(Y, I.H - 1 - Y));
				if (D >= Band) { continue; }
				MulPx(I, X, Y, FMath::Lerp(0.55f, 0.85f, float(D) / float(FMath::Max(1, Band - 1))));
			}
		}
	}
	/** Blits Src with its bottom-centre at (FeetX, FeetY); ShadowRX > 0 adds a ground shadow ellipse (x0.74, +2 X). */
	void Stamp(FFVImage& Dst, const FFVImage& Src, int32 FeetX, int32 FeetY, bool bFlip, float ShadowRX)
	{
		if (!Src.IsValid()) { return; }
		if (ShadowRX > 0.f) { ShadeEllipse(Dst, float(FeetX) + 2.f, float(FeetY) - 1.f, ShadowRX, FMath::Max(2.f, ShadowRX * 0.35f), 0.74f); }
		Dst.Blit(Src, FeetX - Src.W / 2, FeetY - Src.H, bFlip);
	}
	FFVImage MakeBush(uint32 Seed, FColor Leaf, int32 Size, bool bBerries)
	{
		Size = FMath::Clamp(Size, 4, 14);
		FFVImage T(Size * 3 + 4, Size * 2 + 3);
		const float CX = float(T.W) * 0.5f, CY = float(T.H) - float(Size) - 1.f;
		LeafBlob(T, CX - float(Size) * 0.7f, CY + 1.f, float(Size) * 0.8f, float(Size) * 0.75f, Leaf, Seed);
		LeafBlob(T, CX + float(Size) * 0.7f, CY + 1.f, float(Size) * 0.8f, float(Size) * 0.75f, Leaf, Seed + 3);
		LeafBlob(T, CX, CY - 1.f, float(Size), float(Size) * 0.9f, Leaf, Seed + 5);
		if (bBerries)
		{
			for (int32 i = 0; i < Size; ++i)
			{
				const int32 X = FMath::FloorToInt(CX + (R01(i, 1, Seed) - 0.5f) * float(Size) * 2.f), Y = FMath::FloorToInt(CY + (R01(i, 2, Seed) - 0.5f) * float(Size));
				if (T.Get(X, Y).A > 0) { T.Set(X, Y, Rgb(220, 50, 70)); T.Set(X + 1, Y, Rgb(160, 30, 56)); }
			}
		}
		T.Outline(CrownOutline(Leaf));
		return T;
	}
	FFVImage MakeRock(uint32 Seed, int32 RX, int32 RY, FColor Base, bool bMoss)
	{
		FFVImage T(RX * 2 + 4, RY * 2 + 3);
		RockBlob(T, float(T.W) * 0.5f, float(T.H) * 0.5f, float(RX), float(RY), Base, Seed);
		if (bMoss)
		{
			const FColor MossC = Rgb(104, 128, 64);
			for (int32 X = 0; X < T.W; ++X)
			{
				for (int32 Y = 0; Y < T.H; ++Y)
				{
					if (T.Get(X, Y).A == 0) { continue; }
					T.Set(X, Y, R01(X, Y, Seed + 12) < 0.5f ? MossC : FVArt::Light(MossC, 0.25f));
					if (Y + 1 < T.H && R01(X, Y, Seed + 11) < 0.6f) { T.Set(X, Y + 1, MossC); }
					break;
				}
			}
		}
		T.Outline(FVArt::OutlineOf(Base));
		return T;
	}
	FFVImage MakeFern(uint32 Seed, FColor Leaf)
	{
		FFVImage T(15, 11);
		const FColor Hi = LeafTone(Leaf, 0), Lo = LeafTone(Leaf, 2);
		for (int32 f = 0; f < 5; ++f)
		{
			const float Ang = FMath::DegreesToRadians(-160.f + float(f) * 35.f + (R01(f, 1, Seed) - 0.5f) * 14.f);
			const float Len = 5.f + R01(f, 2, Seed) * 2.5f;
			for (int32 s = 0; s <= int32(Len); ++s)
			{
				const float Droop = float(s * s) * 0.06f;
				const int32 X = FMath::RoundToInt(7.f + FMath::Cos(Ang) * float(s)), Y = FMath::RoundToInt(9.f + FMath::Sin(Ang) * float(s) + Droop);
				T.Set(X, Y, s % 2 == 0 ? Leaf : Hi);
				if (s > 0 && s < int32(Len)) { T.Set(X, Y + 1, Lo); }
			}
		}
		T.Outline(CrownOutline(Leaf));
		return T;
	}
	FFVImage MakeMushroom(uint32 Seed, FColor Cap)
	{
		FFVImage T(7, 7);
		const FColor Stem = Rgb(240, 230, 210), StemLo = Rgb(200, 186, 164);
		T.Set(3, 4, Stem); T.Set(3, 5, Stem); T.Set(2, 5, StemLo); T.Set(4, 5, StemLo); T.Set(3, 6, StemLo);
		for (int32 X = 1; X <= 5; ++X) { T.Set(X, 3, FVArt::Shade(Cap, 0.25f)); T.Set(X, 2, Cap); }
		for (int32 X = 2; X <= 4; ++X) { T.Set(X, 1, FVArt::Light(Cap, 0.2f)); }
		if (Cap.R > 180 && Cap.G < 120) { T.Set(2, 2, Rgb(250, 246, 240)); T.Set(4, 1, Rgb(250, 246, 240)); }
		if (Seed % 2 == 1) { T.Set(5, 5, Rgb(96, 150, 64)); }
		T.Outline(FVArt::OutlineOf(Cap));
		return T;
	}
}

// =====================================================================================================================
// The forest
// =====================================================================================================================
namespace FVArtForestImpl
{
	using namespace FVArtKitU6;

	// ---------------------------------------------------------------------------------------- palette (UPDATE6_ART §3)
	const FColor EdgeLo(70, 136, 58), EdgeBase(92, 160, 68), EdgeHi(124, 188, 84);             // опушка ground
	const FColor DeepLo(58, 112, 50), DeepBase(74, 132, 58), DeepHi(100, 158, 70);             // чаща ground
	const FColor BirchLeaf(120, 190, 80), PineLeaf(54, 112, 72), OakLeaf(78, 146, 58), SpruceLeaf(40, 92, 70);
	const FColor FernC(96, 160, 70), MossC(104, 128, 64), Needles(150, 110, 70);
	const FColor BirchBark(236, 232, 220), BirchDash(60, 56, 60);
	const FColor BarkHi(150, 104, 64), Bark(116, 78, 48), BarkLo(84, 54, 34);
	const FColor PineTop(204, 124, 72), SpruceBark(112, 96, 84), Resin(255, 190, 80);
	const FColor CutWood(226, 190, 130), CutRing(196, 150, 96), CutDk(160, 116, 70);
	const FColor PropOut(58, 36, 30);
	const FColor StreamSh(104, 186, 220), StreamDp(34, 92, 156);
	const FColor RockBase(128, 124, 118);
	const FColor PaintRed(214, 52, 48), PaintWhite(246, 244, 236);
	const FColor LogHi(204, 150, 94), LogC(170, 116, 68), LogLo(128, 84, 48), LogSeam(98, 62, 36), LogEnd(222, 180, 122);
	const FColor Tes(150, 104, 62), TesHi(186, 138, 88), TesLo(112, 74, 44);

	constexpr int32 WorldW = 1120, WorldH = 352;
	constexpr int32 WalkTop = 100, WalkBottom = 300;

	FIntPoint TreeCanvas(EFVTreeKind K)
	{
		switch (K)
		{
		case EFVTreeKind::Birch:  return FIntPoint(26, 50);
		case EFVTreeKind::Pine:   return FIntPoint(32, 58);
		case EFVTreeKind::Oak:    return FIntPoint(36, 46);
		default:                  return FIntPoint(38, 78);
		}
	}
	int32 TrunkHalf(EFVTreeKind K) { return (K == EFVTreeKind::Oak || K == EFVTreeKind::Spruce) ? 3 : 2; }

	/** Trunk column [CX-HW, CX+HW-1] from Base up to Top; kind-specific bark. */
	void Trunk(FFVImage& T, int32 CX, int32 Base, int32 Top, int32 HW, EFVTreeKind K, uint32 Seed)
	{
		for (int32 Y = Top; Y <= Base; ++Y)
		{
			const int32 Flare = FMath::Max(0, Y - (Base - 2));
			const int32 X0 = CX - HW - Flare, X1 = CX + HW - 1 + Flare;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor C;
				if (K == EFVTreeKind::Birch)
				{
					C = X == X0 ? FVArt::Light(BirchBark, 0.2f) : (X == X1 ? FVArt::Shade(BirchBark, 0.28f) : BirchBark);
					if (R01(Y / 2, X - X0 < HW ? 0 : 1, Seed) < 0.22f) { C = BirchDash; }
					if (Y > Base - 3 && R01(X, Y, Seed + 1) < 0.5f) { C = FVArt::Mix(BirchDash, BirchBark, 0.4f); }
				}
				else if (K == EFVTreeKind::Pine)
				{
					const FColor B = Y < Base - (Base - Top) * 2 / 3 ? PineTop : Bark;
					C = X == X0 ? FVArt::Light(B, 0.25f) : (X >= X1 ? FVArt::Shade(B, 0.3f) : B);
					if (R01(X, Y / 2, Seed) < 0.14f) { C = FVArt::Shade(B, 0.35f); }
				}
				else if (K == EFVTreeKind::Spruce)
				{
					C = X == X0 ? FVArt::Light(SpruceBark, 0.2f) : (X >= X1 ? FVArt::Shade(SpruceBark, 0.3f) : SpruceBark);
					if (R01(X, Y, Seed) < 0.18f) { C = FVArt::Shade(SpruceBark, 0.3f); }
				}
				else
				{
					C = X == X0 ? BarkHi : (X >= X1 - 1 ? BarkLo : Bark);
					if (R01(X, Y, Seed) < 0.14f) { C = BarkLo; }
				}
				T.Set(X, Y, C);
			}
		}
	}

	FFVImage BuildTreeFull(EFVTreeKind K, int32 V, bool bMarked)
	{
		const FIntPoint Sz = TreeCanvas(K);
		FFVImage T(Sz.X, Sz.Y);
		const int32 CX = Sz.X / 2, Base = Sz.Y - 1;
		const uint32 Seed = 1000u + uint32(K) * 97u + uint32(V) * 31u;
		const float Jit = float(V % 4) - 1.5f;
		int32 VisibleTop = 0;   // where the crown ends (the paint ring goes just below it)
		switch (K)
		{
		case EFVTreeKind::Birch:
		{
			VisibleTop = Base - 17;
			Trunk(T, CX, Base, 8, 2, K, Seed);
			// thin branches reaching into the crown
			for (int32 b = 0; b < 4; ++b)
			{
				const int32 BY = 12 + b * 5;
				const int32 Dir = (b + V) % 2 == 0 ? -1 : 1;
				ThickLine(T, FVector2D(float(CX), float(BY + 4)), FVector2D(float(CX + Dir * (6 + (b % 2) * 2)), float(BY)), 1.f, 1.f, BirchDash);
			}
			// airy crown: many small lacy clumps
			for (int32 c = 0; c < 9; ++c)
			{
				const float Ang = float(c) * 2.4f + float(V);
				const float CCX = float(CX) + FMath::Cos(Ang) * (4.f + float(c % 3) * 2.2f) + Jit * 0.5f;
				const float CCY = 9.f + float(c) * 2.4f + FMath::Sin(Ang) * 2.f;
				const float Rad = 4.2f + R01(c, V, Seed) * 1.8f;
				PaintBlob(T, CCX, CCY, Rad, Rad * 0.9f, LeafTone(BirchLeaf, 0), BirchLeaf, LeafTone(BirchLeaf, 2), LeafTone(BirchLeaf, 3), Seed + uint32(c) * 7, 0.4f, 0.4f);
			}
			// holes in the crown (lacy look)
			for (int32 Y = 4; Y < VisibleTop; ++Y)
			{
				for (int32 X = 0; X < T.W; ++X)
				{
					if (T.Get(X, Y).A > 0 && FMath::Abs(X - CX) > 1 && R01(X / 2, Y / 2, Seed + 40) < 0.07f) { T.Set(X, Y, FColor(0, 0, 0, 0)); }
				}
			}
			break;
		}
		case EFVTreeKind::Pine:
		{
			VisibleTop = Base - 26;
			Trunk(T, CX, Base, 10, 2, K, Seed);
			T.Set(CX - 3, Base - 16, Bark); T.Set(CX - 4, Base - 17, Bark); T.Set(CX + 2, Base - 22, Bark); T.Set(CX + 3, Base - 23, Bark);
			// umbrella crown: flat wide clumps at the top
			const float Cl[5][4] = { { 0.f, 12.f, 12.f, 7.f }, { -6.f, 18.f, 8.f, 5.f }, { 7.f, 17.f, 8.f, 5.f }, { -2.f, 7.f, 8.f, 5.f }, { 3.f, 25.f, 6.f, 4.f } };
			for (int32 c = 0; c < 5; ++c)
			{
				LeafBlob(T, float(CX) + Cl[c][0] + (c == 0 ? Jit : 0.f), Cl[c][1] + float(V % 2), Cl[c][2] + (V == 3 ? 1.f : 0.f), Cl[c][3], PineLeaf, Seed + uint32(c) * 11);
			}
			break;
		}
		case EFVTreeKind::Oak:
		{
			VisibleTop = Base - 14;
			Trunk(T, CX, Base, 14, 3, K, Seed);
			ThickLine(T, FVector2D(float(CX - 1), float(Base - 13)), FVector2D(float(CX - 8), float(Base - 21)), 2.f, 1.f, Bark);
			ThickLine(T, FVector2D(float(CX + 1), float(Base - 15)), FVector2D(float(CX + 8), float(Base - 22)), 2.f, 1.f, BarkLo);
			const float Rad = 11.f + float(V % 2);
			const float CCY = float(Base) - 14.f - Rad * 0.95f;
			const float Cl[6][3] = { { 0.f, -0.55f, 0.85f }, { -0.78f, -0.1f, 0.72f }, { 0.78f, -0.1f, 0.72f },
				{ -0.42f, 0.45f, 0.7f }, { 0.45f, 0.45f, 0.7f }, { 0.f, 0.15f, 0.72f } };
			for (int32 c = 0; c < 6; ++c)
			{
				const float J = (R01(c, V, Seed) - 0.5f) * 2.f;
				LeafBlob(T, float(CX) + Cl[c][0] * Rad + J, CCY + Cl[c][1] * Rad + J * 0.5f, Cl[c][2] * Rad, Cl[c][2] * Rad * 0.9f, OakLeaf, Seed + uint32(c) * 13);
			}
			for (int32 a = 0; a < 3 + V; ++a)   // acorns
			{
				const int32 AX = CX + FMath::RoundToInt((R01(a, 1, Seed) - 0.5f) * Rad * 1.6f), AY = FMath::RoundToInt(CCY + (R01(a, 2, Seed) - 0.1f) * Rad);
				if (T.Get(AX, AY).A > 0 && T.Get(AX, AY - 1).A > 0) { T.Set(AX, AY, Rgb(170, 120, 60)); T.Set(AX, AY - 1, Rgb(110, 80, 44)); }
			}
			break;
		}
		default:   // Spruce (вековая ель): drooping tiers
		{
			VisibleTop = Base - 12;
			Trunk(T, CX, Base, 6, 3, K, Seed);
			const FColor Hi = LeafTone(SpruceLeaf, 0), Lo = LeafTone(SpruceLeaf, 2), Dp = LeafTone(SpruceLeaf, 3);
			const int32 Tiers = 7;
			const float Top = 1.f, Bot = float(Base - 12);
			for (int32 t = 0; t < Tiers; ++t)
			{
				const float TT = Top + (Bot - Top) * float(t) / float(Tiers) * 0.92f;
				const float TB = Top + (Bot - Top) * float(t + 1) / float(Tiers) + 2.f;
				const float HWMax = 3.5f + float(t) * 2.35f + (t == Tiers - 1 ? 1.f : 0.f) + (V % 2 == 1 ? 0.5f : 0.f);
				for (int32 Y = FMath::FloorToInt(TT); Y <= FMath::CeilToInt(TB); ++Y)
				{
					const float F = FMath::Clamp((float(Y) - TT) / FMath::Max(1.f, TB - TT), 0.f, 1.f);
					const float HW = FMath::Lerp(1.5f, HWMax, F);
					for (int32 X = FMath::FloorToInt(float(CX) - HW); X <= FMath::CeilToInt(float(CX) + HW); ++X)
					{
						const float DX = (float(X) + 0.5f - float(CX)) / FMath::Max(1.f, HW);
						if (FMath::Abs(DX) > 1.f) { continue; }
						if (F > 0.8f && R01(X, Y, Seed + uint32(t)) < (F - 0.8f) * 3.5f) { continue; }   // ragged lower edge
						const float L = -DX * 0.7f - (F - 0.5f) * 0.9f + (R01(X, Y, Seed + 50) - 0.5f) * 0.5f;
						T.Set(X, Y, F > 0.86f ? Dp : (L > 0.5f ? Hi : (L > -0.25f ? SpruceLeaf : Lo)));
					}
				}
			}
			T.Set(CX + 1, Base - 7, Resin); T.Set(CX + 1, Base - 6, FVArt::Shade(Resin, 0.3f));   // resin drop
			T.Set(CX - 2, Base - 4, Rgb(170, 190, 150)); T.Set(CX - 3, Base - 3, Rgb(150, 170, 130));   // lichen
			break;
		}
		}
		T.Outline(CrownOutline(K == EFVTreeKind::Birch ? BirchLeaf : (K == EFVTreeKind::Pine ? PineLeaf : (K == EFVTreeKind::Oak ? OakLeaf : SpruceLeaf))));
		T.Set(CX - 5, Base, Rgb(70, 120, 50)); T.Set(CX + 5, Base, Rgb(70, 120, 50)); T.Set(CX + 6, Base - 1, Rgb(96, 150, 64));   // grass at the base
		if (bMarked)
		{
			// red-white paint ring (seen from afar) + the notch: a light wedge cut into the right side of the trunk
			const int32 HW = TrunkHalf(K);
			const int32 RY = VisibleTop + 2;
			for (int32 X = CX - HW - 1; X <= CX + HW; ++X)
			{
				if (T.Get(X, RY + 1).A == 0) { continue; }
				T.Set(X, RY, PaintRed); T.Set(X, RY + 1, PaintWhite); T.Set(X, RY + 2, PaintRed);
			}
			const int32 NX = CX + HW - 1, NY = Base - 6;
			T.Set(NX, NY - 2, BarkLo); T.Set(NX - 1, NY - 1, CutDk);
			T.Set(NX, NY - 1, CutWood); T.Set(NX - 1, NY, CutWood); T.Set(NX - 2, NY, CutRing); T.Set(NX, NY, CutWood);
			T.Set(NX, NY + 1, CutRing); T.Set(NX - 1, NY + 1, CutDk);
			T.Set(NX + 1, NY - 1, CutWood); T.Set(NX + 1, NY, CutRing); T.Set(NX + 2, NY - 2, CutWood);   // chips
		}
		return T;
	}

	FFVImage BuildStump(EFVTreeKind K, int32 V)
	{
		FFVImage T(16, 10);
		const uint32 Seed = 2000u + uint32(K) * 13u + uint32(V);
		const FColor B = K == EFVTreeKind::Birch ? BirchBark : (K == EFVTreeKind::Spruce ? SpruceBark : (K == EFVTreeKind::Pine ? Rgb(150, 92, 56) : Bark));
		for (int32 Y = 3; Y <= 8; ++Y)   // bark sides
		{
			const int32 Fl = Y >= 7 ? 1 : 0;
			for (int32 X = 3 - Fl; X <= 12 + Fl; ++X)
			{
				FColor C = X <= 4 ? FVArt::Light(B, 0.15f) : (X >= 11 ? FVArt::Shade(B, 0.3f) : B);
				if (K == EFVTreeKind::Birch && R01(X, Y / 2, Seed) < 0.25f) { C = BirchDash; }
				else if (R01(X, Y, Seed) < 0.15f) { C = FVArt::Shade(B, 0.25f); }
				T.Set(X, Y, C);
			}
		}
		T.Set(1, 9, FVArt::Shade(B, 0.2f)); T.Set(2, 9, B); T.Set(2, 8, B); T.Set(13, 8, FVArt::Shade(B, 0.3f)); T.Set(14, 9, FVArt::Shade(B, 0.35f));   // roots
		for (int32 Y = 0; Y <= 5; ++Y)   // the cut with annual rings
		{
			for (int32 X = 3; X <= 12; ++X)
			{
				const float DX = (float(X) + 0.5f - 8.f) / 5.f, DY = (float(Y) + 0.5f - 3.f) / 2.6f;
				const float D = DX * DX + DY * DY;
				if (D > 1.f) { continue; }
				FColor C = (FMath::FloorToInt(FMath::Sqrt(D) * 4.f) % 2 == 0) ? CutWood : CutRing;
				if (D > 0.78f) { C = FVArt::Shade(B, 0.1f); }
				T.Set(X, Y, C);
			}
		}
		T.Set(8, 3, CutDk);
		T.Outline(PropOut);
		if (V % 2 == 1) { T.Set(14, 6, Rgb(214, 70, 60)); T.Set(14, 5, Rgb(236, 110, 90)); }   // a little mushroom
		else { T.Set(3, 7, MossC); T.Set(4, 8, MossC); }
		return T;
	}

	FFVImage BuildSapling(EFVTreeKind K, int32 V)
	{
		FFVImage T(12, 20);
		const uint32 Seed = 3000u + uint32(K) * 7u + uint32(V);
		if (K == EFVTreeKind::Pine || K == EFVTreeKind::Spruce)
		{
			const FColor L = K == EFVTreeKind::Pine ? PineLeaf : SpruceLeaf;
			T.Set(6, 19, Bark); T.Set(6, 18, Bark); T.Set(5, 19, BarkLo);
			for (int32 Y = 2; Y <= 17; ++Y)
			{
				const int32 Tier = (Y - 2) % 5;
				const int32 HW = 1 + Tier + (Y - 2) / 5;
				for (int32 X = 6 - HW; X <= 5 + HW; ++X) { T.Set(X, Y, X < 5 ? LeafTone(L, 0) : (Tier == 4 ? LeafTone(L, 2) : L)); }
			}
			T.Set(6, 1, LeafTone(L, 0));
			T.Outline(CrownOutline(L));
			return T;
		}
		const FColor Stem = K == EFVTreeKind::Birch ? BirchBark : Bark;
		const FColor L = K == EFVTreeKind::Birch ? BirchLeaf : OakLeaf;
		for (int32 Y = 6; Y <= 19; ++Y) { T.Set(6, Y, Stem); }
		T.Set(5, 12, Stem); T.Set(4, 11, Stem); T.Set(7, 9, Stem); T.Set(8, 8, Stem);
		LeafBlob(T, 6.f, 5.f, 3.4f, 3.f, L, Seed);
		LeafBlob(T, 3.f, 10.f, 2.6f, 2.2f, L, Seed + 3);
		LeafBlob(T, 9.f, 8.f, 2.4f, 2.f, L, Seed + 5);
		T.Outline(CrownOutline(L));
		return T;
	}

	// ---------------------------------------------------------------------------------------- the windfall
	/** A lying trunk from A to B, radius R (thinning to 0.75 R), bark lit from the top; cut ends or a broken end at A. */
	void LyingTrunk(FFVImage& I, FVector2D A, FVector2D B, float R, FColor BarkC, uint32 Seed, bool bCutEnds)
	{
		const FVector2D AB = B - A;
		const float Len = FMath::Max(1.f, float(AB.Size()));
		const FVector2D Dir = AB / Len;
		const FVector2D Nrm(-Dir.Y, Dir.X);
		const bool bBirch = BarkC == BirchBark;
		const int32 X0 = FMath::FloorToInt(float(FMath::Min(A.X, B.X)) - R - 1.f), X1 = FMath::CeilToInt(float(FMath::Max(A.X, B.X)) + R + 1.f);
		const int32 Y0 = FMath::FloorToInt(float(FMath::Min(A.Y, B.Y)) - R - 1.f), Y1 = FMath::CeilToInt(float(FMath::Max(A.Y, B.Y)) + R + 1.f);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector2D P(float(X) + 0.5f, float(Y) + 0.5f);
				const float T = float(FVector2D::DotProduct(P - A, Dir));
				if (T < 0.f || T > Len) { continue; }
				const float S = float(FVector2D::DotProduct(P - A, Nrm));
				const float RR = R * (1.f - 0.25f * T / Len);
				if (FMath::Abs(S) > RR) { continue; }
				const float Up = (Nrm.Y < 0.f ? S : -S) / RR;   // +1 = the upper (lit) side
				FColor C = Up > 0.45f ? FVArt::Light(BarkC, 0.25f) : (Up < -0.5f ? FVArt::Shade(BarkC, 0.4f) : BarkC);
				if (bBirch) { if (R01(FMath::FloorToInt(T / 2.f), S > 0.f ? 1 : 0, Seed + 1) < 0.25f) { C = BirchDash; } }
				else if (R01(FMath::FloorToInt(T / 3.f), FMath::FloorToInt(S), Seed) < 0.2f) { C = FVArt::Shade(C, 0.25f); }
				I.Set(X, Y, C);
			}
		}
		if (bCutEnds)
		{
			const FVector2D Ends[2] = { A, B };
			for (int32 e = 0; e < 2; ++e)
			{
				const float ER = e == 0 ? R : R * 0.75f;
				FillEllipse(I, float(Ends[e].X), float(Ends[e].Y), ER * 0.55f + 0.5f, ER, CutWood, false);
				FillEllipse(I, float(Ends[e].X), float(Ends[e].Y), ER * 0.3f, ER * 0.5f, CutRing, false);
				I.Set(FMath::FloorToInt(float(Ends[e].X)), FMath::FloorToInt(float(Ends[e].Y)), CutDk);
			}
		}
		else
		{
			// the broken end: jagged light splinters
			for (int32 k = -FMath::FloorToInt(R); k <= FMath::FloorToInt(R); ++k)
			{
				const FVector2D P = A + Nrm * float(k) - Dir * (1.f + R01(k, 3, Seed) * 2.f);
				I.Set(FMath::FloorToInt(float(P.X)), FMath::FloorToInt(float(P.Y)), (k + 10) % 2 == 0 ? CutWood : CutDk);
			}
		}
	}

	void RootDisc(FFVImage& I, float CX, float CY, float RX, float RY, uint32 Seed)
	{
		const FColor Soil(112, 76, 50), SoilLo(84, 56, 40), SoilHi(150, 108, 70), Root(160, 118, 76);
		for (int32 r = 0; r < 12; ++r)
		{
			const float Ang = float(r) / 12.f * 2.f * UE_PI + R01(r, 1, Seed);
			ThickLine(I, FVector2D(CX, CY), FVector2D(CX + FMath::Cos(Ang) * (RX + 3.f), CY + FMath::Sin(Ang) * (RY + 3.f)), 2.f, 1.f, Root);
		}
		for (int32 Y = FMath::FloorToInt(CY - RY); Y <= FMath::CeilToInt(CY + RY); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
			{
				const float DX = (float(X) + 0.5f - CX) / RX, DY = (float(Y) + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY;
				if (D > 1.f - 0.2f * R01(X / 2, Y / 2, Seed)) { continue; }
				const float L = -(DX * 0.6f + DY * 0.8f) + (R01(X, Y, Seed + 2) - 0.5f) * 0.6f;
				I.Set(X, Y, L > 0.4f ? SoilHi : (L < -0.4f ? SoilLo : Soil));
				if (R01(X, Y, Seed + 3) < 0.06f) { I.Set(X, Y, Rgb(164, 156, 146)); }
			}
		}
		for (int32 r = 0; r < 6; ++r)
		{
			const float Ang = float(r) * 1.1f;
			ThickLine(I, FVector2D(CX + FMath::Cos(Ang) * RX * 0.3f, CY + FMath::Sin(Ang) * RY * 0.3f), FVector2D(CX + FMath::Cos(Ang) * RX * 0.8f, CY + FMath::Sin(Ang) * RY * 0.8f), 1.f, 1.f, Root);
		}
	}

	void AxeSign(FFVImage& I, int32 X, int32 Y)
	{
		// a post with a small board: an axe pictogram («Нужен топор»)
		for (int32 YY = Y + 8; YY <= Y + 15; ++YY) { I.Set(X + 4, YY, Bark); I.Set(X + 5, YY, BarkLo); }
		I.FillRect(X, Y, 10, 8, Rgb(214, 170, 110));
		for (int32 XX = X; XX < X + 10; ++XX) { I.Set(XX, Y, Rgb(236, 196, 140)); I.Set(XX, Y + 7, Rgb(170, 124, 76)); }
		I.Set(X + 2, Y + 2, Rgb(150, 150, 166)); I.Set(X + 3, Y + 2, Rgb(150, 150, 166)); I.Set(X + 2, Y + 3, Rgb(110, 110, 124)); I.Set(X + 3, Y + 3, Rgb(98, 98, 114));
		I.Set(X + 4, Y + 2, Rgb(98, 98, 114));
		for (int32 k = 0; k < 4; ++k) { I.Set(X + 4 + k, Y + 3 + k / 2 + (k > 1 ? 1 : 0), Rgb(140, 92, 52)); }
	}

	FFVImage BuildBlockage(bool bCleared)
	{
		FFVImage I(96, 40);
		const uint32 Seed = 4100u;
		if (!bCleared)
		{
			// dry needles, the uprooted root disc on the left, four crossed trunks, dead twigs, an axe sign
			for (int32 X = 6; X < 90; ++X) { if (R01(X, 1, Seed) < 0.5f) { I.Set(X, 38, Needles); } }
			LyingTrunk(I, FVector2D(84.f, 11.f), FVector2D(26.f, 33.f), 5.f, Bark, Seed + 1, false);
			RootDisc(I, 14.f, 17.f, 12.f, 11.f, Seed + 2);
			LyingTrunk(I, FVector2D(22.f, 20.f), FVector2D(90.f, 28.f), 5.5f, SpruceBark, Seed + 3, false);
			LyingTrunk(I, FVector2D(34.f, 35.f), FVector2D(80.f, 34.f), 4.f, BirchBark, Seed + 4, false);
			LyingTrunk(I, FVector2D(40.f, 6.f), FVector2D(76.f, 20.f), 3.f, Bark, Seed + 5, false);
			for (int32 b = 0; b < 7; ++b)   // dead branches with dry needles
			{
				const FVector2D P(50.f + float(b) * 5.f, 18.f + float(b % 3) * 5.f);
				ThickLine(I, P, P + FVector2D(3.f + float(b % 2) * 2.f, -7.f + float(b % 3)), 1.f, 1.f, BarkLo);
				PaintBlob(I, float(P.X) + 4.f, float(P.Y) - 7.f, 3.f, 2.2f, Rgb(170, 150, 80), Rgb(140, 118, 60), Rgb(110, 90, 50), Rgb(84, 66, 40), Seed + uint32(b), 0.35f, 0.4f);
			}
			AxeSign(I, 84, 20);
			I.Outline(PropOut);
			return I;
		}
		// cleared: the root disc pushed aside, sawdust, logs rolled to the sides, the stack of three logs
		RootDisc(I, 9.f, 26.f, 7.f, 7.f, Seed + 2);
		for (int32 k = 0; k < 90; ++k)
		{
			const int32 X = 30 + FMath::FloorToInt(R01(k, 1, Seed) * 40.f), Y = 30 + FMath::FloorToInt(R01(k, 2, Seed) * 9.f);
			I.Set(X, Y, R01(k, 3, Seed) < 0.5f ? CutWood : Rgb(240, 214, 160));
		}
		LyingTrunk(I, FVector2D(5.f, 37.f), FVector2D(24.f, 34.f), 3.5f, Bark, Seed + 6, true);
		const FVector2D Stack[3] = { FVector2D(74.f, 34.f), FVector2D(84.f, 34.f), FVector2D(79.f, 26.f) };
		for (int32 s = 0; s < 3; ++s)
		{
			FillEllipse(I, float(Stack[s].X), float(Stack[s].Y), 4.6f, 4.6f, Bark, false);
			FillEllipse(I, float(Stack[s].X) - 0.3f, float(Stack[s].Y) - 0.3f, 3.6f, 3.6f, CutWood, false);
			FillEllipse(I, float(Stack[s].X) - 0.3f, float(Stack[s].Y) - 0.3f, 2.2f, 2.2f, CutRing, false);
			I.Set(FMath::FloorToInt(float(Stack[s].X)), FMath::FloorToInt(float(Stack[s].Y)), CutDk);
		}
		LyingTrunk(I, FVector2D(92.f, 12.f), FVector2D(92.f, 22.f), 3.f, SpruceBark, Seed + 8, true);
		I.Outline(PropOut);
		for (int32 k = 0; k < 20; ++k)
		{
			const int32 X = 28 + FMath::FloorToInt(R01(k, 4, Seed) * 44.f), Y = 34 + FMath::FloorToInt(R01(k, 5, Seed) * 5.f);
			if (I.Get(X, Y).A == 0) { I.Set(X, Y, Rgb(248, 226, 176)); }
		}
		return I;
	}

	// ---------------------------------------------------------------------------------------- forest buildings & props
	/** Log walls (horizontal logs, 3 rows each, round ends at the corners) in [X0, X1] x [Y0, Y1]. */
	void LogWall(FFVImage& I, int32 X0, int32 X1, int32 Y0, int32 Y1, uint32 Seed)
	{
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			const int32 R3 = (Y - Y0) % 3;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor C = R3 == 0 ? LogHi : (R3 == 1 ? LogC : LogSeam);
				if (R3 == 1 && R01(X / 3, Y, Seed) < 0.12f) { C = LogLo; }
				if (X >= X1 - 3) { C = Mul(C, 0.82f); }
				I.Set(X, Y, C);
			}
			if (R3 != 2)
			{
				I.Set(X0 - 2, Y, R3 == 0 ? LogEnd : Mul(LogEnd, 0.85f)); I.Set(X0 - 1, Y, LogEnd);
				I.Set(X1 + 1, Y, Mul(LogEnd, 0.78f)); I.Set(X1 + 2, Y, Mul(LogEnd, 0.66f));
			}
		}
	}
	/** Plank (тёс) roof trapezoid: narrow at TopY, full [X0, X1] at EaveY. */
	void TesRoof(FFVImage& I, int32 X0, int32 X1, int32 TopY, int32 EaveY, int32 Inset, FColor Base)
	{
		const FColor Hi = FVArt::Light(Base, 0.3f), Lo = FVArt::Shade(Base, 0.3f), Dp = FVArt::Shade(Base, 0.55f);
		for (int32 Y = TopY; Y <= EaveY; ++Y)
		{
			const float T = float(Y - TopY) / float(FMath::Max(1, EaveY - TopY));
			const int32 A = FMath::RoundToInt(float(X0 + Inset) - float(Inset) * T), B = FMath::RoundToInt(float(X1 - Inset) + float(Inset) * T);
			for (int32 X = A; X <= B; ++X)
			{
				FColor C = ((X - A) % 5 == 0) ? Lo : Base;
				if ((Y - TopY) % 8 == 7) { C = Lo; }
				if (X <= A + 1) { C = Hi; } else if (X >= B - 1) { C = Dp; }
				I.Set(X, Y, C);
			}
		}
		for (int32 X = X0 + Inset; X <= X1 - Inset; ++X) { I.Set(X, TopY, Hi); }
		for (int32 X = X0; X <= X1; ++X) { I.Set(X, EaveY, Dp); }
	}
	void CarvedWindow(FFVImage& I, int32 X0, int32 Y0, int32 W, int32 H, FColor Frame)
	{
		const FColor Glass(66, 92, 132), GlassHi(156, 196, 226);
		for (int32 Y = Y0; Y < Y0 + H; ++Y)
		{
			for (int32 X = X0; X < X0 + W; ++X)
			{
				FColor C = Glass;
				if (X == X0 || X == X0 + W - 1 || Y == Y0 || Y == Y0 + H - 1) { C = Frame; }
				else if (X == X0 + W / 2 || Y == Y0 + H / 2) { C = FVArt::Shade(Frame, 0.15f); }
				else if (X == X0 + 1 && Y < Y0 + H / 2) { C = GlassHi; }
				I.Set(X, Y, C);
			}
		}
		for (int32 k = 0; k <= W / 2; ++k) { I.Set(X0 + k, Y0 - 1 - k / 2, Frame); I.Set(X0 + W - 1 - k, Y0 - 1 - k / 2, Frame); }   // carved наличник
		for (int32 X = X0 - 1; X <= X0 + W; ++X) { I.Set(X, Y0 + H, Frame); I.Set(X, Y0 + H + 1, FVArt::Shade(Frame, 0.3f)); }
	}

	/** The forester's izba (canvas 76x62, feet = bottom-centre): logs, plank roof, porch, antlers, a sign with a pine. */
	FFVImage BuildIzba()
	{
		FFVImage I(76, 62);
		const FColor Frame(236, 232, 220);
		LogWall(I, 6, 69, 30, 55, 51u);
		for (int32 Y = 56; Y <= 61; ++Y) { for (int32 X = 4; X <= 71; ++X) { I.Set(X, Y, Y == 56 ? Rgb(188, 184, 174) : (((X + (Y & 1) * 3) % 7 == 0) ? Rgb(112, 106, 104) : Rgb(150, 146, 140))); } }
		for (int32 Y = 38; Y <= 55; ++Y) { for (int32 X = 32; X <= 43; ++X) { I.Set(X, Y, (X == 32 || X == 43 || Y == 38) ? Rgb(96, 60, 36) : ((X - 32) % 3 == 0 ? Rgb(98, 60, 36) : Rgb(122, 76, 46))); } }
		I.Set(41, 47, Rgb(236, 196, 92));
		for (int32 X = 28; X <= 47; ++X) { I.Set(X, 57, TesHi); I.Set(X, 58, Tes); I.Set(X, 59, TesHi); I.Set(X, 60, TesLo); }
		for (int32 Y = 33; Y <= 56; ++Y) { I.Set(29, Y, Tes); I.Set(46, Y, TesLo); }
		for (int32 Y = 31; Y <= 34; ++Y) { for (int32 X = 26 - (Y - 31); X <= 49 + (Y - 31); ++X) { I.Set(X, Y, Y == 31 ? TesHi : (Y == 34 ? TesLo : Tes)); } }
		CarvedWindow(I, 12, 40, 11, 10, Frame);
		CarvedWindow(I, 53, 40, 11, 10, Frame);
		for (int32 Y = 2; Y <= 14; ++Y) { for (int32 X = 52; X <= 58; ++X) { I.Set(X, Y, Y <= 3 ? Rgb(70, 64, 64) : (X == 52 ? Rgb(188, 184, 174) : ((Y + X) % 3 == 0 ? Rgb(112, 106, 104) : Rgb(150, 146, 140)))); } }
		TesRoof(I, 1, 74, 6, 29, 13, Tes);
		// antlers above the door + the sign «Лесничество» (a board with a pine and text strokes)
		const FColor Horn(236, 222, 190), HornLo(190, 170, 140);
		I.Set(37, 36, HornLo); I.Set(38, 36, HornLo);
		for (int32 k = 0; k < 5; ++k) { I.Set(36 - k, 35 - k / 2, Horn); I.Set(39 + k, 35 - k / 2, Horn); }
		I.Set(33, 32, Horn); I.Set(42, 32, Horn); I.Set(35, 32, Horn); I.Set(40, 32, Horn);
		I.FillRect(29, 20, 18, 7, Rgb(214, 170, 110));
		for (int32 X = 29; X < 47; ++X) { I.Set(X, 20, Rgb(236, 196, 140)); I.Set(X, 26, Rgb(150, 104, 62)); }
		for (int32 Y = 21; Y <= 25; ++Y) { const int32 HW = (Y - 21) / 2; for (int32 X = 32 - HW; X <= 32 + HW; ++X) { I.Set(X, Y, PineLeaf); } }
		for (int32 X = 35; X <= 44; X += 2) { I.Set(X, 22, Rgb(90, 60, 40)); I.Set(X, 24, Rgb(90, 60, 40)); }
		I.Outline(PropOut);
		return I;
	}

	/** The builder's site trailer (бытовка), canvas 66x46: green boards, a window, skids, steps, a sign with a hammer. */
	FFVImage BuildTrailer()
	{
		FFVImage I(66, 46);
		const FColor Gr(78, 138, 84), GrHi(118, 176, 110), GrLo(56, 100, 64);
		for (int32 X = 2; X <= 63; ++X) { I.Set(X, 42, Rgb(90, 70, 56)); I.Set(X, 43, Rgb(70, 54, 44)); }
		I.Set(1, 41, Rgb(90, 70, 56)); I.Set(64, 41, Rgb(70, 54, 44));
		for (int32 Y = 8; Y <= 41; ++Y)
		{
			for (int32 X = 3; X <= 62; ++X)
			{
				const int32 B = (X - 3) % 4;
				FColor C = B == 0 ? GrLo : (B == 1 ? GrHi : Gr);
				if (R01(X / 4, Y / 5, 71u) < 0.1f && B != 0) { C = FVArt::Shade(C, 0.1f); }
				if (X >= 59) { C = Mul(C, 0.82f); }
				I.Set(X, Y, C);
			}
		}
		for (int32 X = 1; X <= 64; ++X) { I.Set(X, 5, Rgb(160, 160, 170)); I.Set(X, 6, Rgb(130, 130, 144)); I.Set(X, 7, Rgb(96, 96, 110)); }
		for (int32 X = 3; X <= 62; X += 6) { I.Set(X, 4, Rgb(186, 186, 196)); }
		CarvedWindow(I, 10, 18, 14, 11, Rgb(236, 232, 220));
		for (int32 Y = 16; Y <= 41; ++Y) { for (int32 X = 42; X <= 53; ++X) { I.Set(X, Y, (X == 42 || X == 53 || Y == 16) ? Rgb(60, 90, 60) : Rgb(96, 150, 96)); } }
		I.Set(51, 29, Rgb(236, 196, 92));
		for (int32 X = 40; X <= 55; ++X) { I.Set(X, 42, TesHi); I.Set(X, 43, Tes); I.Set(X, 44, TesLo); }
		// the sign «Прораб. Постройки»: a yellow board with a hammer and text strokes
		I.FillRect(26, 9, 14, 7, Rgb(255, 204, 51));
		for (int32 X = 26; X < 40; ++X) { I.Set(X, 9, Rgb(255, 236, 140)); I.Set(X, 15, Rgb(196, 136, 30)); }
		for (int32 k = 0; k < 4; ++k) { I.Set(28 + k, 14 - k, Rgb(140, 92, 52)); }
		I.Set(31, 10, Rgb(98, 98, 114)); I.Set(32, 11, Rgb(98, 98, 114)); I.Set(30, 11, Rgb(150, 150, 166)); I.Set(33, 10, Rgb(150, 150, 166));
		for (int32 X = 35; X <= 38; ++X) { I.Set(X, 11, Rgb(52, 46, 52)); I.Set(X, 13, Rgb(52, 46, 52)); }
		I.Outline(PropOut);
		return I;
	}

	FFVImage BuildWoodpile()
	{
		FFVImage I(22, 14);
		for (int32 r = 0; r < 3; ++r)
		{
			for (int32 c = 0; c < 4 - (r == 2 ? 1 : 0); ++c)
			{
				const float X = 3.5f + float(c) * 5.f + (r >= 1 ? 2.5f : 0.f);
				const float Y = 11.f - float(r) * 4.f;
				FillEllipse(I, X, Y, 2.6f, 2.3f, Bark, false);
				FillEllipse(I, X - 0.3f, Y - 0.3f, 1.7f, 1.5f, CutWood, false);
				I.Set(FMath::FloorToInt(X), FMath::FloorToInt(Y), CutRing);
			}
		}
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildBeehive()
	{
		FFVImage I(12, 14);
		const FColor Straw(222, 180, 96), StrawLo(170, 124, 56);
		for (int32 Y = 2; Y <= 11; ++Y)
		{
			const float HW = 2.f + FMath::Sqrt(float(Y)) * 1.5f;
			for (int32 X = FMath::FloorToInt(6.f - HW); X <= FMath::CeilToInt(5.f + HW); ++X) { I.Set(X, Y, (Y % 3 == 0) ? StrawLo : (X < 5 ? FVArt::Light(Straw, 0.2f) : Straw)); }
		}
		I.Set(5, 9, Rgb(50, 36, 30)); I.Set(6, 9, Rgb(50, 36, 30));
		for (int32 X = 2; X <= 9; ++X) { I.Set(X, 12, Tes); I.Set(X, 13, TesLo); }
		I.Outline(PropOut);
		I.Set(10, 3, Rgb(255, 214, 60)); I.Set(11, 4, Rgb(40, 30, 30));   // a bee
		return I;
	}
	FFVImage BuildBench()
	{
		FFVImage I(20, 9);
		for (int32 X = 1; X <= 18; ++X) { I.Set(X, 2, TesHi); I.Set(X, 3, Tes); I.Set(X, 4, TesLo); }
		for (int32 Y = 5; Y <= 8; ++Y) { I.Set(3, Y, TesLo); I.Set(16, Y, TesLo); }
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildBirdhouse()
	{
		FFVImage I(9, 26);
		for (int32 Y = 9; Y <= 25; ++Y) { I.Set(4, Y, Bark); I.Set(5, Y, BarkLo); }
		I.FillRect(1, 3, 7, 7, Tes);
		for (int32 k = 0; k < 5; ++k) { I.Set(4 - k, 2 + k / 2, TesLo); I.Set(4 + k, 2 + k / 2, TesLo); }
		I.Set(4, 1, TesLo);
		I.Set(4, 5, Rgb(40, 28, 24)); I.Set(4, 6, Rgb(40, 28, 24)); I.Set(4, 8, Rgb(186, 138, 88));
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildSawhorse()
	{
		FFVImage I(24, 14);
		for (int32 k = 0; k < 8; ++k) { I.Set(3 + k / 2, 13 - k, Tes); I.Set(8 - k / 2, 13 - k, TesLo); I.Set(15 + k / 2, 13 - k, Tes); I.Set(20 - k / 2, 13 - k, TesLo); }
		LyingTrunk(I, FVector2D(2.f, 5.f), FVector2D(22.f, 4.f), 2.5f, Bark, 91u, true);
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildBoardStack()
	{
		FFVImage I(28, 12);
		for (int32 b = 0; b < 4; ++b)
		{
			const int32 Y = 9 - b * 2, X0 = 2 + (b % 2), X1 = 25 - ((b + 1) % 2);
			for (int32 X = X0; X <= X1; ++X) { I.Set(X, Y, CutWood); I.Set(X, Y + 1, CutRing); }
			I.Set(X1, Y, CutDk); I.Set(X1, Y + 1, CutDk);
		}
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildBrickPallet()
	{
		FFVImage I(18, 12);
		for (int32 X = 1; X <= 16; ++X) { I.Set(X, 10, Tes); I.Set(X, 11, TesLo); }
		for (int32 Y = 2; Y <= 9; ++Y)
		{
			for (int32 X = 2; X <= 15; ++X)
			{
				const bool bMortar = (Y % 3 == 1) || ((X + ((Y / 3) % 2) * 3) % 6 == 0);
				I.Set(X, Y, bMortar ? Rgb(206, 190, 164) : (X > 13 ? Rgb(130, 60, 48) : Rgb(178, 88, 62)));
			}
		}
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildWheelbarrow()
	{
		FFVImage I(22, 12);
		const FColor MetC(110, 140, 170), MetLo(70, 96, 124);
		for (int32 Y = 2; Y <= 7; ++Y) { for (int32 X = 4 + (Y - 2) / 2; X <= 16 - (Y - 2) / 3; ++X) { I.Set(X, Y, Y == 2 ? FVArt::Light(MetC, 0.3f) : (X > 13 ? MetLo : MetC)); } }
		for (int32 X = 6; X <= 13; ++X) { I.Set(X, 2, Rgb(200, 190, 170)); }
		FillEllipse(I, 5.f, 9.f, 2.5f, 2.5f, Rgb(52, 46, 52), false); I.Set(5, 9, Rgb(150, 150, 166));
		for (int32 k = 0; k < 5; ++k) { I.Set(16 + k, 6 + k / 3, Tes); }
		I.Set(15, 8, TesLo); I.Set(15, 9, TesLo); I.Set(15, 10, TesLo);
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildTrough()
	{
		FFVImage I(20, 8);
		for (int32 Y = 1; Y <= 6; ++Y) { for (int32 X = 1 + (Y > 4 ? 1 : 0); X <= 18 - (Y > 4 ? 1 : 0); ++X) { I.Set(X, Y, (Y == 1 || X <= 2 || X >= 17 || Y == 6) ? Tes : Rgb(170, 166, 158)); } }
		for (int32 X = 3; X <= 16; ++X) { if (R01(X, 2, 13u) < 0.3f) { I.Set(X, 2, Rgb(200, 196, 188)); } }
		I.Outline(PropOut);
		return I;
	}
	/** Three fence samples (the builder's showcase of the yard cosmetics): white picket, wattle, wrought iron. */
	FFVImage BuildFenceSamples()
	{
		FFVImage I(64, 20);
		for (int32 p = 0; p < 4; ++p)
		{
			const int32 X = 1 + p * 4;
			for (int32 Y = 6; Y <= 19; ++Y) { I.Set(X, Y, Rgb(246, 244, 236)); I.Set(X + 1, Y, Rgb(206, 204, 198)); }
			I.Set(X, 5, Rgb(246, 244, 236));
		}
		for (int32 X = 1; X <= 15; ++X) { I.Set(X, 9, Rgb(226, 224, 216)); I.Set(X, 15, Rgb(226, 224, 216)); }
		for (int32 Y = 7; Y <= 19; ++Y) { I.Set(22, Y, Bark); I.Set(36, Y, Bark); }
		for (int32 Y = 8; Y <= 18; ++Y) { for (int32 X = 21; X <= 37; ++X) { if (X != 22 && X != 36) { I.Set(X, Y, ((Y / 2 + X / 3) % 2 == 0) ? Rgb(170, 124, 70) : Rgb(134, 94, 52)); } } }
		for (int32 p = 0; p < 5; ++p)
		{
			const int32 X = 44 + p * 4;
			for (int32 Y = 5; Y <= 19; ++Y) { I.Set(X, Y, Rgb(44, 42, 56)); }
			I.Set(X, 4, Rgb(255, 204, 51)); I.Set(X, 3, Rgb(255, 240, 160));
		}
		for (int32 X = 43; X <= 61; ++X) { I.Set(X, 8, Rgb(44, 42, 56)); I.Set(X, 17, Rgb(44, 42, 56)); }
		for (int32 p = 0; p < 4; ++p) { I.Set(46 + p * 4, 12, Rgb(255, 204, 51)); }
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildLanternPost()
	{
		FFVImage I(9, 30);
		for (int32 Y = 8; Y <= 29; ++Y) { I.Set(4, Y, Rgb(52, 46, 52)); }
		for (int32 X = 2; X <= 6; ++X) { I.Set(X, 29, Rgb(52, 46, 52)); I.Set(X, 1, Rgb(52, 46, 52)); }
		I.FillRect(2, 2, 5, 6, Rgb(255, 214, 120));
		I.Set(3, 3, Rgb(255, 246, 200)); I.Set(2, 2, Rgb(52, 46, 52)); I.Set(6, 2, Rgb(52, 46, 52)); I.Set(4, 7, Rgb(52, 46, 52));
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildArrowSign()
	{
		// «← К реке»: an arrow board on a post pointing left, a blue wave on it
		FFVImage I(20, 20);
		for (int32 Y = 8; Y <= 19; ++Y) { I.Set(12, Y, Bark); I.Set(13, Y, BarkLo); }
		for (int32 X = 5; X <= 18; ++X) { for (int32 Y = 2; Y <= 7; ++Y) { I.Set(X, Y, Y == 2 ? Rgb(236, 196, 140) : (Y == 7 ? Rgb(170, 124, 76) : Rgb(214, 170, 110))); } }
		for (int32 k = 0; k < 4; ++k) { for (int32 Y = 4 - k; Y <= 5 + k; ++Y) { I.Set(1 + k, Y, Rgb(214, 170, 110)); } }
		for (int32 X = 6; X <= 16; ++X) { I.Set(X, (X / 2) % 2 == 0 ? 4 : 5, Rgb(60, 110, 190)); }
		I.Outline(PropOut);
		return I;
	}

	// ---------------------------------------------------------------------------------------- world helpers
	struct FStampItem { FFVImage Img; int32 X = 0, Y = 0; bool bFlip = false; float Shadow = 0.f; };

	FVector2D WP(float X, float Y) { return FVector2D(X * FV::WorldScale, Y * FV::WorldScale); }
	FFVRect WR(float X, float Y, float W, float H) { return FFVRect(X * FV::WorldScale, Y * FV::WorldScale, W * FV::WorldScale, H * FV::WorldScale); }

	void AddAnchor(FVWorldU6::FWorldDef& D, FVWorldU6::EAnchor K, const TCHAR* Id, float X, float Y, int32 Index = 0, uint8 Variant = 0, bool bFlag = false)
	{
		FVWorldU6::FAnchor A;
		A.Kind = K;
		A.Id = Id ? FName(Id) : NAME_None;
		A.Index = Index;
		A.Variant = Variant;
		A.bFlag = bFlag;
		A.Feet = WP(X, Y);
		D.Anchors.Add(A);
	}
	void AddLight(FVWorldU6::FWorldDef& D, float X, float Y, float RadiusTex, FColor C, bool bFlicker)
	{
		FVWorldU6::FAnchor A;
		A.Kind = FVWorldU6::EAnchor::Light;
		A.Feet = WP(X, Y);
		A.Radius = RadiusTex * FV::WorldScale;
		A.Color = C;
		A.bFlag = bFlicker;
		D.Anchors.Add(A);
	}

	/** 0 = опушка, 1 = чаща (blended across the ravine). */
	float DeepFactor(int32 X) { return FMath::Clamp(float(X - 380) / 90.f, 0.f, 1.f); }
}

// [MILL-ART] the arrow sign of the mill (FVArtMill.cpp): Dir 0 = up «К мельнице»
namespace FVArtMillImpl { FFVImage MakeSign(int32 Dir); }

// =====================================================================================================================
// FVWorldU6 / UFVSprites entry points
// =====================================================================================================================
void FVWorldU6::BuildForest(FFVImage& OutBg, FWorldDef& OutDef)
{
	using namespace FVArtForestImpl;
	FFVImage& I = OutBg;
	I.Init(WorldW, WorldH, FColor(0, 0, 0, 255));

	// ---- 1) ground: опушка (light) -> чаща (dark, shade patches, needles, moss), dithered blend across the ravine
	for (int32 Y = 0; Y < WorldH; ++Y)
	{
		for (int32 X = 0; X < WorldW; ++X)
		{
			const float V = Fbm(float(X) / 16.f, float(Y) / 16.f, 11u) + (Bayer(X, Y) - 0.5f) * 0.1f;
			const bool bDeep = DeepFactor(X) > Bayer(X, Y) * 0.9f + 0.05f;
			const FColor GLo = bDeep ? DeepLo : EdgeLo, GBase = bDeep ? DeepBase : EdgeBase, GHi = bDeep ? DeepHi : EdgeHi;
			FColor C = V < 0.4f ? GLo : (V > 0.6f ? GHi : GBase);
			const float S = R01(X, Y, 13u);
			if (S < 0.012f) { C = FVArt::Shade(C, 0.25f); }
			else if (S > 0.992f) { C = FVArt::Light(C, 0.22f); }
			if (bDeep)
			{
				C = Mul(C, 0.92f);
				if (Fbm(float(X) / 40.f, float(Y) / 30.f, 17u) > 0.56f) { C = Mul(C, 0.82f); }
				if (R01(X, Y, 19u) < 0.02f) { C = Needles; }
				else if (Fbm(float(X) / 9.f, float(Y) / 9.f, 21u) > 0.7f) { C = FVArt::Mix(C, MossC, 0.5f); }
			}
			I.Set(X, Y, C);
		}
	}

	// ---- 2) the stream on the left edge with a two-plank bridge (exit to the river)
	for (int32 Y = 0; Y < WorldH; ++Y)
	{
		const float Wob = FMath::Sin(float(Y) * 0.07f) * 2.f + VNoise(float(Y) * 0.1f, 3.f, 23u) * 2.f;
		const int32 SL = FMath::RoundToInt(6.f + Wob), SR = FMath::RoundToInt(26.f + Wob);
		for (int32 X = 0; X <= SR + 3; ++X)
		{
			if (X < SL || X > SR)
			{
				if (X > SR) { I.Set(X, Y, X == SR + 1 ? Rgb(120, 96, 64) : Rgb(96, 150, 70)); }   // the banks
				else { I.Set(X, Y, X == SL - 1 ? Rgb(120, 96, 64) : Rgb(62, 110, 52)); }
				continue;
			}
			const float T = FMath::Abs(float(X) - float(SL + SR) * 0.5f) / (float(SR - SL) * 0.5f);
			FColor C = FVArt::Mix(StreamDp, StreamSh, T * T);
			if (R01(X, Y / 3, 25u) < 0.05f) { C = FVArt::Light(StreamSh, 0.5f); }
			if (X == SR) { C = Mul(C, 0.8f); }
			I.Set(X, Y, C);
		}
	}
	for (int32 Y = 197; Y <= 217; ++Y)
	{
		for (int32 X = 0; X <= 36; ++X)
		{
			const int32 Plank = Y < 207 ? 0 : 1;
			const int32 Row = (Y - 197) % 10;
			FColor C = Row == 0 ? CutWood : (Row == 9 ? TesLo : (Plank ? Tes : TesHi));
			if (R01(X / 4, Y, 27u) < 0.1f) { C = FVArt::Shade(C, 0.15f); }
			if ((X == 3 || X == 32) && (Row == 2 || Row == 7)) { C = Rgb(84, 78, 80); }
			I.Set(X, Y, C);
		}
	}
	for (int32 X = 0; X <= 36; ++X) { MulPx(I, X, 218, 0.7f); MulPx(I, X, 219, 0.8f); }

	// ---- 3) the path: from the bridge through the gap of the ravine into the thicket; branches to the cordon and the site
	PaintPath(I, { FVector2D(0.f, 207.f), FVector2D(44.f, 208.f), FVector2D(100.f, 214.f), FVector2D(170.f, 200.f), FVector2D(240.f, 208.f),
		FVector2D(300.f, 214.f), FVector2D(360.f, 216.f), FVector2D(416.f, 218.f), FVector2D(470.f, 220.f), FVector2D(540.f, 214.f),
		FVector2D(620.f, 204.f), FVector2D(700.f, 198.f), FVector2D(780.f, 210.f), FVector2D(860.f, 204.f), FVector2D(940.f, 210.f),
		FVector2D(1010.f, 214.f), FVector2D(1100.f, 212.f) }, 14.f, 31u);
	PaintPath(I, { FVector2D(640.f, 204.f), FVector2D(636.f, 160.f), FVector2D(640.f, 116.f) }, 11.f, 33u);
	PaintPath(I, { FVector2D(905.f, 206.f), FVector2D(910.f, 160.f), FVector2D(905.f, 116.f) }, 11.f, 35u);
	// Р69: the path south, to the swamp (off the bottom edge of the опушка)
	PaintPath(I, { FVector2D(236.f, 210.f), FVector2D(244.f, 262.f), FVector2D(250.f, 310.f), FVector2D(250.f, 360.f) }, 12.f, 38u);

	// ---- 4) grass, flowers (flat, walkable)
	Tufts(I, 36, 96, 368, 300, 700, EdgeBase, 41u);
	Tufts(I, 466, 96, 1100, 300, 900, DeepBase, 43u);
	Flowers(I, 40, 110, 366, 296, 190, 45u);
	Flowers(I, 470, 120, 1090, 296, 40, 47u);

	// ---- 4b) [MILL-ART] the path to the mill (§18): from the main path up through the canopy (x ~525) over a brook
	//      (the footbridge "mill_bridge" is a runtime sprite over the brook at y 72..100, the exit is at the top edge)
	PaintPath(I, { FVector2D(530.f, 214.f), FVector2D(527.f, 160.f), FVector2D(525.f, 110.f), FVector2D(524.f, 60.f), FVector2D(522.f, 30.f) }, 11.f, 37u);
	for (int32 X = 430; X <= 640; ++X)
	{
		const float CY = 86.f + FMath::Sin(float(X) * 0.05f) * 3.f;
		for (int32 Y = FMath::FloorToInt(CY) - 9; Y <= FMath::CeilToInt(CY) + 9; ++Y)
		{
			const float D = FMath::Abs(float(Y) - CY);
			if (D > 8.f) { continue; }
			if (D > 6.5f) { I.Set(X, Y, Rgb(120, 96, 64)); continue; }   // the banks
			FColor C = FVArt::Mix(StreamDp, StreamSh, (D / 6.5f) * (D / 6.5f));
			if (R01(X / 3, Y, 39u) < 0.05f) { C = FVArt::Light(StreamSh, 0.5f); }
			I.Set(X, Y, C);
		}
	}

	// ---- 5) the ravine at the windfall: a gully with a brook below the gap (the rock slope above is stamped later)
	for (int32 Y = 238; Y < WorldH; ++Y)
	{
		for (int32 X = 362; X <= 472; ++X)
		{
			const float Mid = 417.f + FMath::Sin(float(Y) * 0.05f) * 6.f;
			const float HW = 30.f + float(Y - 238) * 0.2f + VNoise(float(Y) * 0.2f, 1.f, 51u) * 6.f;
			const float D = FMath::Abs(float(X) - Mid);
			if (D > HW) { continue; }
			FColor C = FVArt::Mix(Rgb(96, 84, 64), Rgb(62, 54, 46), D / HW);
			if (D < 5.f) { C = FVArt::Mix(StreamSh, StreamDp, D / 5.f); if (R01(X, Y / 2, 53u) < 0.06f) { C = Rgb(220, 240, 250); } }
			else if (R01(X, Y, 55u) < 0.06f) { C = Rgb(140, 134, 126); }
			I.Set(X, Y, C);
		}
	}

	TArray<FStampItem> Items;
	auto Add = [&Items](FFVImage&& Img, int32 X, int32 Y, bool bFlip, float Shadow)
	{
		FStampItem It; It.Img = MoveTemp(Img); It.X = X; It.Y = Y; It.bFlip = bFlip; It.Shadow = Shadow;
		Items.Add(MoveTemp(It));
	};
	// the rock slope above the gap: a solid scree mass, overlapping mossy boulders, pines on the crest
	for (int32 Y = 60; Y <= 204; ++Y)
	{
		for (int32 X = 362; X <= 472; ++X)
		{
			const float Edge = 8.f + VNoise(float(Y) * 0.15f, 7.f, 58u) * 8.f;
			if (float(X - 362) < Edge * 0.6f || float(472 - X) < Edge * 0.6f) { continue; }
			if (Y > 196 - FMath::FloorToInt(VNoise(float(X) * 0.2f, 9.f, 59u) * 6.f)) { continue; }
			const float V = Fbm(float(X) / 7.f, float(Y) / 7.f, 60u);
			I.Set(X, Y, V > 0.62f ? Rgb(118, 112, 106) : (V < 0.38f ? Rgb(74, 70, 70) : Rgb(96, 92, 88)));
		}
	}
	for (int32 k = 0; k < 40; ++k)
	{
		const int32 Row = k / 8, Col = k % 8;
		const int32 X = 370 + Col * 12 + (Row % 2) * 6 + FMath::FloorToInt(R01(k, 1, 57u) * 6.f);
		const int32 Y = 104 + Row * 20 + FMath::FloorToInt(R01(k, 2, 57u) * 8.f);
		Add(MakeRock(600u + uint32(k), 7 + FMath::FloorToInt(R01(k, 3, 57u) * 5.f), 6 + FMath::FloorToInt(R01(k, 4, 57u) * 3.f), R01(k, 5, 57u) < 0.3f ? Mul(RockBase, 0.88f) : RockBase, Row < 2 || R01(k, 6, 57u) < 0.4f), X, Y, k % 3 == 1, 0.f);
	}
	for (int32 k = 0; k < 4; ++k) { Add(UFVSprites::BuildTree(EFVTreeKind::Pine, EFVTreeState::Full, k), 378 + k * 26, 96 + (k % 2) * 4, k % 2 == 0, 6.f); }
	for (int32 k = 0; k < 7; ++k)    // stones and bushes along the gully
	{
		Add(MakeRock(640u + uint32(k), 6 + k % 3, 4 + k % 2, Mul(RockBase, 0.9f), k % 2 == 0), 374 + k * 14, 262 + (k % 3) * 22, false, 0.f);
		Add(MakeBush(650u + uint32(k), PineLeaf, 5 + k % 3, false), k < 4 ? 364 + k * 2 : 470 - (k - 4) * 2, 250 + k * 14, k % 2 == 0, 4.f);
	}
	Add(MakeRock(660u, 12, 9, RockBase, true), 376, 202, false, 3.f);
	Add(MakeRock(661u, 11, 8, RockBase, true), 458, 204, true, 3.f);

	// ---- 6) light rays in the thicket (warm transparent trapezoids, dithered edges)
	const int32 Rays[4] = { 520, 700, 850, 1010 };
	for (int32 r = 0; r < 4; ++r)
	{
		for (int32 Y = 40; Y < 300; ++Y)
		{
			const float T = float(Y - 40) / 260.f;
			const float RX0 = float(Rays[r]) + T * 60.f, RW = 10.f + T * 26.f;
			for (int32 X = FMath::FloorToInt(RX0); X <= FMath::CeilToInt(RX0 + RW); ++X)
			{
				const float E = FMath::Min(float(X) - RX0, RX0 + RW - float(X)) / 4.f;
				if (E < Bayer(X, Y)) { continue; }
				MixPx(I, X, Y, Rgb(255, 244, 200), 0.2f * (1.f - T * 0.5f));
			}
		}
	}

	// ---- 7) the canopy: dark far crowns along the top, then rows of decorative trees (feet y 56..102)
	for (int32 X = 0; X < WorldW; X += 7)
	{
		const FColor L = DeepFactor(X) > 0.5f ? SpruceLeaf : PineLeaf;
		LeafBlob(I, float(X) + R01(X, 1, 61u) * 4.f, 8.f + R01(X, 2, 61u) * 14.f, 11.f, 12.f, Mul(L, 0.8f), 700u + uint32(X));
	}
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 X = 30 + Row * 11; X < WorldW - 4; X += 26 + Row * 3)
		{
			if (Row > 0 && ((X > 596 && X < 684) || (X > 866 && X < 944))) { continue; }   // keep the izba and the trailer clear
			if (X > 494 && X < 552) { continue; }                                               // [MILL-ART] the path to the mill
			const float Rn = R01(X, Row, 63u);
			EFVTreeKind K;
			if (DeepFactor(X) < 0.5f) { K = Rn < 0.45f ? EFVTreeKind::Birch : (Rn < 0.85f ? EFVTreeKind::Pine : EFVTreeKind::Oak); }
			else { K = Rn < 0.45f ? EFVTreeKind::Spruce : (Rn < 0.75f ? EFVTreeKind::Pine : (Rn < 0.92f ? EFVTreeKind::Oak : EFVTreeKind::Birch)); }
			Add(UFVSprites::BuildTree(K, EFVTreeState::Full, (X / 7 + Row) % 4), X + FMath::FloorToInt(Rn * 8.f), 56 + Row * 20 + FMath::FloorToInt(R01(X, 9, 63u) * 6.f), Rn > 0.5f, 7.f);
		}
	}
	// the bottom band (y 300..352): bushes, ferns, rocks; trees standing at the very edge
	for (int32 X = 40; X < WorldW - 20; X += 18)
	{
		if ((X > 356 && X < 478) || (X > 222 && X < 282)) { continue; }   // the gully; Р69: the path to the swamp
		const float Rn = R01(X, 3, 65u);
		const bool bDeep = DeepFactor(X) > 0.5f;
		if (Rn < 0.45f) { Add(MakeBush(800u + uint32(X), bDeep ? PineLeaf : Rgb(84, 150, 62), 6 + FMath::FloorToInt(Rn * 8.f), Rn < 0.12f), X, 318 + FMath::FloorToInt(R01(X, 4, 65u) * 20.f), false, 5.f); }
		else if (Rn < 0.7f) { Add(MakeFern(820u + uint32(X), FernC), X, 312 + FMath::FloorToInt(R01(X, 5, 65u) * 30.f), Rn < 0.6f, 0.f); }
		else { Add(MakeRock(840u + uint32(X), 6 + FMath::FloorToInt(Rn * 4.f), 4, RockBase, bDeep), X, 322 + FMath::FloorToInt(R01(X, 6, 65u) * 18.f), false, 4.f); }
	}
	for (int32 X = 16; X < WorldW - 20; )
	{
		const float Rn = R01(X, 7, 67u);
		if ((X < 352 || X > 484) && (X < 210 || X > 292))   // Р69: no edge tree over the path to the swamp
		{
			EFVTreeKind K;
			if (DeepFactor(X) > 0.5f) { K = Rn < 0.3f ? EFVTreeKind::Oak : (Rn < 0.8f ? EFVTreeKind::Spruce : EFVTreeKind::Pine); }
			else { K = Rn < 0.45f ? EFVTreeKind::Birch : (Rn < 0.8f ? EFVTreeKind::Pine : EFVTreeKind::Oak); }
			Add(UFVSprites::BuildTree(K, EFVTreeState::Full, FMath::FloorToInt(Rn * 3.99f)), X, WorldH + 6 + FMath::FloorToInt(R01(X, 8, 67u) * 14.f), Rn > 0.5f, 0.f);
		}
		X += 24 + FMath::FloorToInt(R01(X, 9, 67u) * 22.f);
	}
	// lush bushes along the upper edge of the walk band and fallen logs / stumps at its lower edge
	for (int32 X = 44; X < WorldW - 30; X += 30)
	{
		if ((X > 350 && X < 480) || (X > 570 && X < 740) || (X > 830 && X < 1050) || (X > 500 && X < 552)) { continue; }
		const float Rn = R01(X, 11, 69u);
		if (Rn < 0.6f) { Add(MakeBush(860u + uint32(X), DeepFactor(X) > 0.5f ? Rgb(60, 120, 70) : Rgb(96, 164, 70), 5 + FMath::FloorToInt(Rn * 5.f), Rn < 0.2f), X + FMath::FloorToInt(Rn * 12.f), 108 + FMath::FloorToInt(R01(X, 12, 69u) * 8.f), Rn < 0.3f, 5.f); }
	}
	for (int32 X = 60; X < WorldW - 40; X += 70)
	{
		if ((X > 340 && X < 490) || (X > 226 && X < 282)) { continue; }   // Р69: the path to the swamp
		const float Rn = R01(X, 13, 69u);
		const int32 Y = 294 + FMath::FloorToInt(Rn * 6.f);
		if (Rn < 0.4f) { Add(UFVSprites::BuildTree(Rn < 0.2f ? EFVTreeKind::Birch : EFVTreeKind::Spruce, EFVTreeState::Stump, X % 4), X, Y, false, 5.f); }
		else if (Rn < 0.7f)
		{
			FFVImage Log(30, 10);
			LyingTrunk(Log, FVector2D(3.f, 5.f), FVector2D(27.f, 4.f), 3.5f, Rn < 0.55f ? Bark : BirchBark, 870u + uint32(X), true);
			for (int32 k = 0; k < 4; ++k) { Log.Set(8 + k * 5, 1, MossC); Log.Set(9 + k * 5, 1, FVArt::Light(MossC, 0.2f)); }
			Log.Outline(PropOut);
			Add(MoveTemp(Log), X, Y, Rn < 0.55f, 12.f);
		}
		else { Add(MakeFern(880u + uint32(X), FernC), X, Y, false, 0.f); Add(MakeFern(881u + uint32(X), FernC), X + 9, Y + 3, true, 0.f); }
	}
	for (int32 Y = 110; Y < WorldH + 20; Y += 22)   // the right edge: a wall of old spruces and oaks
	{
		Add(UFVSprites::BuildTree(Y % 3 == 0 ? EFVTreeKind::Oak : EFVTreeKind::Spruce, EFVTreeState::Full, (Y / 22) % 4), WorldW - 6 - ((Y / 22) % 2) * 10, Y, false, 6.f);
	}

	// ---- 8) small props in the walk band (low): mushrooms, ferns, mossy stones
	for (int32 k = 0; k < 46; ++k)
	{
		const int32 X = 44 + FMath::FloorToInt(R01(k, 1, 71u) * 1040.f), Y = 110 + FMath::FloorToInt(R01(k, 2, 71u) * 186.f);
		if (X > 356 && X < 474) { continue; }
		if (FMath::Abs(Y - 210) < 16) { continue; }                                               // off the path
		if ((X > 580 && X < 734 && Y < 140) || (X > 836 && X < 1046 && Y < 140)) { continue; }   // off the yards
		const float Rn = R01(k, 3, 71u);
		if (Rn < 0.35f) { Add(MakeMushroom(900u + uint32(k), Rn < 0.18f ? Rgb(214, 60, 50) : Rgb(170, 110, 60)), X, Y, false, 0.f); }
		else if (Rn < 0.75f) { Add(MakeFern(920u + uint32(k), DeepFactor(X) > 0.5f ? FernC : Rgb(110, 176, 80)), X, Y, Rn < 0.55f, 0.f); }
		else { Add(MakeRock(940u + uint32(k), 4, 3, RockBase, DeepFactor(X) > 0.5f), X, Y, false, 3.f); }
	}

	// ---- 9) the forester's cordon (x ~640) and the builder's yard (x ~905)
	Add(BuildIzba(), 640, 112, false, 30.f);
	Add(BuildBench(), 596, 116, false, 8.f);
	Add(BuildWoodpile(), 690, 114, false, 9.f);
	Add(BuildBirdhouse(), 582, 110, false, 3.f);
	Add(BuildBeehive(), 708, 120, false, 5.f);
	Add(BuildBeehive(), 722, 124, true, 5.f);
	Add(BuildTrailer(), 905, 112, false, 28.f);
	Add(BuildSawhorse(), 860, 120, false, 9.f);
	Add(BuildBoardStack(), 956, 114, false, 12.f);
	Add(BuildBrickPallet(), 846, 110, false, 8.f);
	Add(BuildWheelbarrow(), 952, 126, true, 8.f);
	Add(BuildTrough(), 878, 126, false, 8.f);
	Add(BuildFenceSamples(), 1008, 118, false, 0.f);
	Add(BuildLanternPost(), 944, 110, false, 3.f);
	// Р69: the arrow signs of the exits are runtime way signs with pictures (FAnchor::SignFeet, FVSceneU6Objects.cpp)

	Items.Sort([](const FStampItem& A, const FStampItem& B) { return A.Y < B.Y; });
	for (const FStampItem& It : Items) { Stamp(I, It.Img, It.X, It.Y, It.bFlip, It.Shadow); }

	// ---- 10) the dark rim at the world edge
	DarkRim(I, 6);

	// ---- the world description (world px)
	OutDef = FWorldDef();
	OutDef.LocationId = FVDBU6::ForestId();
	OutDef.SizeTex = FIntPoint(WorldW, WorldH);
	OutDef.bWater = false;
	OutDef.bBirds = true;
	OutDef.bButterflies = true;
	OutDef.bFireflies = true;
	OutDef.Walk.Add(WR(34.f, float(WalkTop), float(WorldW - 34 - 18), float(WalkBottom - WalkTop)));
	OutDef.Walk.Add(WR(0.f, 198.f, 40.f, 19.f));                               // the bridge
	OutDef.Walk.Add(WR(512.f, 40.f, 26.f, 62.f));                              // [MILL-ART] the path up to the mill
	OutDef.Walk.Add(WR(238.f, 298.f, 26.f, float(WorldH - 298)));              // Р69: the path south to the swamp
	OutDef.Blockers.Add(WR(368.f, float(WalkTop), 98.f, 100.f));              // the rock slope above the gap
	OutDef.Blockers.Add(WR(368.f, 238.f, 98.f, float(WalkBottom - 238)));     // the gully below the gap
	OutDef.Blockers.Add(WR(602.f, float(WalkTop), 76.f, 16.f));               // the izba
	OutDef.Blockers.Add(WR(684.f, float(WalkTop), 48.f, 26.f));               // woodpile + hives
	OutDef.Blockers.Add(WR(836.f, float(WalkTop), 136.f, 28.f));              // trailer, pallet, sawhorse, trough, barrow
	OutDef.Blockers.Add(WR(972.f, float(WalkTop), 72.f, 20.f));               // the fence samples

	AddAnchor(OutDef, EAnchor::Spawn, TEXT("default"), 50.f, 208.f);
	AddAnchor(OutDef, EAnchor::Spawn, TEXT("from_river"), 50.f, 208.f);
	AddAnchor(OutDef, EAnchor::Spawn, TEXT("from_map"), 50.f, 208.f);
	{
		FAnchor A;
		A.Kind = EAnchor::Exit;
		A.Id = FName(TEXT("from_forest"));
		A.Target = FName(TEXT("river"));
		A.Feet = WP(8.f, 208.f);
		A.Hit = WR(0.f, 196.f, 14.f, 22.f);
		A.Label = TEXT("← К реке");
		A.SignFeet = WP(80.f, 196.f);   // Р69: the way sign — the river with the forest on its banks (right of the spawn: never hidden by the player)
		A.SignDir = uint8(EWayDir::Left);
		OutDef.Anchors.Add(A);
	}
	// Р69: the path south to the swamp (closed until the swamp is opened on the map — the sign shows a padlock)
	AddAnchor(OutDef, EAnchor::Spawn, TEXT("from_swamp"), 250.f, 318.f);
	{
		FAnchor A;
		A.Kind = EAnchor::Exit;
		A.Id = FName(TEXT("from_forest"));
		A.Target = FName(TEXT("swamp"));
		A.Feet = WP(250.f, 346.f);
		A.Hit = WR(236.f, 338.f, 30.f, 14.f);
		A.Label = TEXT("↓ К болоту");
		A.SignFeet = WP(276.f, 308.f);
		A.SignDir = uint8(EWayDir::Down);
		OutDef.Anchors.Add(A);
	}
	{
		// the windfall closes the 38-texel gap of the ravine
		FAnchor A;
		A.Kind = EAnchor::Blockage;
		A.Id = FName(TEXT("blockage"));
		A.Feet = WP(417.f, 239.f);
		A.Hit = WR(369.f, 199.f, 96.f, 40.f);
		OutDef.Anchors.Add(A);
	}
	// [MILL-ART] §18: the footbridge over the brook, the exit to the mill (closed by FVGameU6 until the bridge is repaired)
	AddAnchor(OutDef, EAnchor::Spawn, TEXT("from_mill"), 525.f, 118.f);
	{
		FAnchor A;
		A.Kind = EAnchor::MillObj;
		A.Id = FVMill::ObjBridgeId();
		A.Feet = WP(525.f, 101.f);
		A.Hit = WR(510.f, 70.f, 30.f, 32.f);
		OutDef.Anchors.Add(A);
	}
	{
		FAnchor A;
		A.Kind = EAnchor::Exit;
		A.Id = FName(TEXT("from_forest"));
		A.Target = FVMill::MillId();
		A.Feet = WP(525.f, 48.f);
		A.Hit = WR(510.f, 40.f, 30.f, 16.f);
		A.Label = TEXT("↑ К мельнице");
		A.SignFeet = WP(546.f, 116.f);   // Р69: the way sign — the water wheel
		A.SignDir = uint8(EWayDir::Up);
		OutDef.Anchors.Add(A);
	}
	AddAnchor(OutDef, EAnchor::Npc, TEXT("npc_potapych"), 664.f, 130.f);
	AddAnchor(OutDef, EAnchor::Npc, TEXT("npc_gavrila"), 932.f, 132.f);
	// trees: stable ids tree_01..tree_22, Variant = EFVTreeKind, all marked (choppable); Index = art variant hint
	struct FT { float X, Y; EFVTreeKind K; };
	static const FT Trees[] = {
		{ 90.f, 150.f, EFVTreeKind::Birch }, { 150.f, 268.f, EFVTreeKind::Pine }, { 215.f, 140.f, EFVTreeKind::Birch },
		{ 262.f, 262.f, EFVTreeKind::Birch }, { 318.f, 150.f, EFVTreeKind::Pine }, { 345.f, 284.f, EFVTreeKind::Birch },
		{ 490.f, 140.f, EFVTreeKind::Pine }, { 520.f, 286.f, EFVTreeKind::Oak }, { 560.f, 154.f, EFVTreeKind::Spruce },
		{ 585.f, 262.f, EFVTreeKind::Pine }, { 626.f, 292.f, EFVTreeKind::Oak }, { 680.f, 244.f, EFVTreeKind::Pine },
		{ 745.f, 152.f, EFVTreeKind::Oak }, { 740.f, 284.f, EFVTreeKind::Spruce }, { 790.f, 160.f, EFVTreeKind::Birch },
		{ 830.f, 262.f, EFVTreeKind::Oak }, { 860.f, 160.f, EFVTreeKind::Pine }, { 950.f, 272.f, EFVTreeKind::Spruce },
		{ 992.f, 162.f, EFVTreeKind::Oak }, { 1030.f, 266.f, EFVTreeKind::Pine }, { 1062.f, 152.f, EFVTreeKind::Spruce },
		{ 1076.f, 292.f, EFVTreeKind::Birch } };
	for (int32 t = 0; t < int32(UE_ARRAY_COUNT(Trees)); ++t)
	{
		const FString Id = FString::Printf(TEXT("tree_%02d"), t + 1);
		AddAnchor(OutDef, EAnchor::Tree, *Id, Trees[t].X, Trees[t].Y, t % 4, uint8(Trees[t].K), true);
	}
	// resource slots of the edge (branches / pebbles)
	static const FVector2D Picks[10] = { FVector2D(55.f, 165.f), FVector2D(110.f, 180.f), FVector2D(130.f, 244.f), FVector2D(190.f, 262.f),
		FVector2D(200.f, 168.f), FVector2D(250.f, 180.f), FVector2D(292.f, 246.f), FVector2D(330.f, 188.f), FVector2D(64.f, 262.f), FVector2D(356.f, 250.f) };
	for (int32 p = 0; p < 10; ++p) { AddAnchor(OutDef, EAnchor::Pickup, nullptr, float(Picks[p].X), float(Picks[p].Y), p); }
	// lights: izba windows, trailer window, the lantern; chimney smoke
	AddLight(OutDef, 623.f, 101.f, 22.f, FColor(255, 200, 120), false);
	AddLight(OutDef, 664.f, 101.f, 22.f, FColor(255, 200, 120), false);
	AddLight(OutDef, 889.f, 90.f, 22.f, FColor(255, 214, 140), false);
	AddLight(OutDef, 944.f, 84.f, 34.f, FColor(255, 196, 110), true);
	AddAnchor(OutDef, EAnchor::Smoke, nullptr, 657.f, 52.f);
}

const FFVSprite& UFVSprites::TreeSprite(EFVTreeKind Kind, EFVTreeState State, int32 Variant)
{
	const uint32 K = FMath::Min(uint32(Kind), uint32(EFVTreeKind::Count) - 1u);
	const uint32 S = FMath::Min(uint32(State), uint32(EFVTreeState::Count) - 1u);
	const int32 V = FMath::Clamp(Variant, 0, 3);
	const uint64 CacheKey = MakeKey(FVSpriteCat::Tree, K, S, uint8(V));
	if (const FFVSprite* Found = FindCached(CacheKey))
	{
		return *Found;
	}
	return AddCached(CacheKey, BuildTree(EFVTreeKind(K), EFVTreeState(S), V), FString::Printf(TEXT("Tree_%u_%u_%d"), K, S, V));
}

const FFVSprite& UFVSprites::BlockageSprite(bool bCleared)
{
	const uint64 CacheKey = MakeKey(FVSpriteCat::Forest, 1, bCleared ? 1 : 0);
	if (const FFVSprite* Found = FindCached(CacheKey))
	{
		return *Found;
	}
	return AddCached(CacheKey, FVArtForestImpl::BuildBlockage(bCleared), bCleared ? TEXT("Blockage_Cleared") : TEXT("Blockage"));
}

FFVImage UFVSprites::BuildTree(EFVTreeKind Kind, EFVTreeState State, int32 Variant)
{
	if (Kind >= EFVTreeKind::Count || State >= EFVTreeState::Count)
	{
		return BuildMissing();
	}
	const int32 V = FMath::Clamp(Variant, 0, 3);
	switch (State)
	{
	case EFVTreeState::Stump:   return FVArtForestImpl::BuildStump(Kind, V);
	case EFVTreeState::Sapling: return FVArtForestImpl::BuildSapling(Kind, V);
	case EFVTreeState::Marked:  return FVArtForestImpl::BuildTreeFull(Kind, V, true);
	default:                    return FVArtForestImpl::BuildTreeFull(Kind, V, false);
	}
}

FVector2D UFVSprites::TreeNotchPoint(EFVTreeKind Kind, int32 Variant)
{
	if (Kind >= EFVTreeKind::Count)
	{
		return FVector2D(-1.f, -1.f);
	}
	// matches BuildTreeFull: the notch at (CX + HW - 1, Base - 6) of the kind's canvas (the variant does not move it)
	const FIntPoint Sz = FVArtForestImpl::TreeCanvas(Kind);
	return FVector2D(float(Sz.X / 2 + FVArtForestImpl::TrunkHalf(Kind) - 1) + 0.5f, float(Sz.Y - 1 - 6) + 0.5f);
}
