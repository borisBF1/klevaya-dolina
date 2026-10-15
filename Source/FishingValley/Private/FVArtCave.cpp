// UPDATE 6 [ART-CAVE] — the Cave: big dark world with the underground lake, boulders, excavation face & cells (Docs/UPDATE6_FOREST_CITY.md §9а, Р14, Р38). Owner: ART-CAVE.
// Contract: Public/FVSprites.h «UPDATE 6», Private/FVWorldU6.h.
// Style: pixel art like FVSceneArt.cpp / FVArtHome.cpp (Stardew-like shading via FVArt::Shade/Light, 1px dark outline
// on props), deterministic (own FRandomStream), cached through UFVSprites::AddCached(MakeKey(FVSpriteCat::<cat>, ...)).
//
// World 960x400 texels (px = texel * FV::WorldScale). Plan (texels):
//   y 0..40 rock ceiling with stalactites, y 40..104 the cave wall (strata, crystals); the adit from the City top-left
//   (x 36..84, exit «В Город ↑»). Floor: x 30..350 y 104..352 (left, down to the bottom rocks) and x 350..924 y 104..258;
//   the underground lake x 350..960, y >= 262 (ShoreY), plank walk (Dock) x 558..598 into the lake; the underwater
//   spring at (780, 330) — lighter water, bubbles; drip puddles with rings; rails and a mine cart by the back wall.
//   Light: crystals (blue / violet) and glowing mushrooms + 2 lanterns = Light anchors (always on in the dark).
// Sprites (pivot bottom-centre unless noted, cache FVSpriteCat::Cave, A = kind):
//   BoulderSprite  32x28, variants 0..3 (grey, brown, bluish, ore veins); depleted = low pile of fragments  A=1
//   ExcavationFace 128x96 (8x6 cells of 16) — the bedrock under the layers, variants 0..3, drawn stretched   A=2
//   ExcavationCell 16x16 by layer 1..3 (light soft rock -> dark hard rock), variants 0..3, opaque           A=3

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVWorldU6.h"

namespace FVArtCaveImpl
{
	using FVWorldU6::EAnchor;
	using FVWorldU6::FAnchor;
	using FVWorldU6::FWorldDef;

	// ---- palette (UPDATE6_ART.md §3, Пещера) ----
	const FColor PropOut(58, 36, 30);
	const FColor FloorLo(70, 64, 78), FloorBase(88, 82, 98), FloorHi(112, 106, 124);
	const FColor RockLo(56, 52, 68), RockHi(96, 92, 110), RockMid(76, 72, 90);
	const FColor WetHi(150, 160, 185), CrystalBlue(120, 230, 255), CrystalViolet(180, 120, 250), Shroom(120, 255, 180);
	const FColor LakeShallow(58, 112, 140), LakeDeep(12, 30, 52);
	const FColor Wood(150, 104, 62), WoodDark(104, 68, 42), WoodLight(190, 142, 90), Iron(70, 70, 80);
	const FColor PathLo(104, 92, 96), PathHi(124, 112, 112);

	float Smooth(float T) { return T * T * (3.f - 2.f * T); }
	float VN(float X, float Y, uint32 S)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = Smooth(X - float(X0)), FY = Smooth(Y - float(Y0));
		const float A = FVArt::Noise(X0, Y0, S), B = FVArt::Noise(X0 + 1, Y0, S);
		const float C = FVArt::Noise(X0, Y0 + 1, S), D = FVArt::Noise(X0 + 1, Y0 + 1, S);
		return FMath::Lerp(FMath::Lerp(A, B, FX), FMath::Lerp(C, D, FX), FY);
	}
	float Fbm(float X, float Y, uint32 S) { return 0.5f * VN(X / 26.f, Y / 26.f, S) + 0.3f * VN(X / 10.f, Y / 10.f, S + 7) + 0.2f * VN(X / 4.f, Y / 4.f, S + 13); }
	float Bayer(int32 X, int32 Y)
	{
		static const int32 M[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
		return (float(M[(Y & 3) * 4 + (X & 3)]) + 0.5f) / 16.f;
	}
	FColor Ramp(FColor A, FColor B, float T, int32 X, int32 Y)
	{
		const float Q = FMath::Clamp(FMath::FloorToFloat(T * 4.f + Bayer(X, Y)) / 4.f, 0.f, 1.f);
		return FVArt::Mix(A, B, Q);
	}
	void Mul(FFVImage& I, int32 X, int32 Y, float F)
	{
		if (!I.InBounds(X, Y)) { return; }
		FColor C = I.Get(X, Y);
		C.R = uint8(FMath::Clamp(float(C.R) * F, 0.f, 255.f));
		C.G = uint8(FMath::Clamp(float(C.G) * F, 0.f, 255.f));
		C.B = uint8(FMath::Clamp(float(C.B) * F, 0.f, 255.f));
		I.Set(X, Y, C);
	}
	void Ellipse(FFVImage& I, float CX, float CY, float RX, float RY, FColor C, bool bBlend = false)
	{
		for (int32 y = FMath::FloorToInt(CY - RY); y <= FMath::CeilToInt(CY + RY); ++y)
		{
			for (int32 x = FMath::FloorToInt(CX - RX); x <= FMath::CeilToInt(CX + RX); ++x)
			{
				const float DX = (float(x) + 0.5f - CX) / RX, DY = (float(y) + 0.5f - CY) / RY;
				if (DX * DX + DY * DY <= 1.f) { if (bBlend) { I.Blend(x, y, C); } else { I.Set(x, y, C); } }
			}
		}
	}
	void ShadowEllipse(FFVImage& I, float CX, float CY, float RX, float RY)
	{
		CX += 2.f;
		for (int32 y = FMath::FloorToInt(CY - RY); y <= FMath::CeilToInt(CY + RY); ++y)
		{
			for (int32 x = FMath::FloorToInt(CX - RX); x <= FMath::CeilToInt(CX + RX); ++x)
			{
				const float DX = (float(x) + 0.5f - CX) / RX, DY = (float(y) + 0.5f - CY) / RY;
				if (DX * DX + DY * DY <= 1.f) { Mul(I, x, y, 0.74f); }
			}
		}
	}
	/** Shaded rock lump, light from the top-left; bWet adds cold highlights. */
	void RockBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Base, uint32 Seed, bool bWet = false)
	{
		for (int32 y = FMath::FloorToInt(CY - RY); y <= FMath::CeilToInt(CY + RY); ++y)
		{
			for (int32 x = FMath::FloorToInt(CX - RX); x <= FMath::CeilToInt(CX + RX); ++x)
			{
				const float DX = (float(x) + 0.5f - CX) / RX, DY = (float(y) + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY + (FVArt::Noise(x, y, Seed) - 0.5f) * 0.2f;
				if (D > 1.f) { continue; }
				const float L = -DX * 0.6f - DY * 0.8f;
				FColor C = Base;
				if (L > 0.5f) { C = bWet ? FVArt::Mix(Base, WetHi, 0.6f) : FVArt::Light(Base, 0.3f); }
				else if (L > 0.1f) { C = FVArt::Light(Base, 0.12f); }
				else if (L < -0.55f) { C = FVArt::Shade(Base, 0.35f); }
				else if (L < -0.15f) { C = FVArt::Shade(Base, 0.15f); }
				if (FVArt::Noise(x / 2, y / 2, Seed + 1) < 0.08f) { C = FVArt::Shade(C, 0.2f); }
				I.Set(x, y, C);
			}
		}
	}
	/** Crystal cluster standing on (X, Base): 3..4 pointed prisms. */
	void Crystals(FFVImage& I, int32 X, int32 Base, FColor C, uint32 Seed)
	{
		FRandomStream R(Seed);
		const int32 N = 3 + R.RandRange(0, 1);
		for (int32 k = 0; k < N; ++k)
		{
			const int32 CX = X - 5 + k * 4 + R.RandRange(-1, 1), Hh = 6 + R.RandRange(0, 8) - (k == 0 || k == N - 1 ? 3 : 0);
			const int32 Lean = R.RandRange(-2, 2);
			for (int32 y = 0; y < Hh; ++y)
			{
				const int32 HW = y < 3 ? y / 2 : 1 + (y > Hh / 2 ? 1 : 0);
				const int32 SX = CX + Lean * (Hh - y) / FMath::Max(1, Hh);
				for (int32 x = -HW; x <= HW; ++x)
				{
					FColor P = x < 0 ? FVArt::Light(C, 0.35f) : (x > 0 ? FVArt::Shade(C, 0.25f) : C);
					if (y < 2) { P = FColor(240, 250, 255); }
					I.Set(SX + x, Base - Hh + y, P);
				}
				I.Set(SX - HW - 1, Base - Hh + y, FVArt::OutlineOf(C)); I.Set(SX + HW + 1, Base - Hh + y, FVArt::OutlineOf(C));
			}
		}
		for (int32 x = X - 8; x <= X + 8; ++x) { I.Blend(x, Base + 1, FVArt::WithAlpha(C, 70)); }   // glow on the floor
	}
	void Mushrooms(FFVImage& I, int32 X, int32 Base, uint32 Seed)
	{
		FRandomStream R(Seed);
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 MX = X - 4 + k * 4 + R.RandRange(-1, 1), Hh = 3 + R.RandRange(0, 3);
			I.FillRect(MX, Base - Hh, 1, Hh, FColor(200, 230, 210));
			const int32 CapW = 2 + (k == 1 ? 1 : 0);
			I.FillRect(MX - CapW, Base - Hh - 2, CapW * 2 + 1, 2, Shroom);
			I.FillRect(MX - CapW + 1, Base - Hh - 3, CapW * 2 - 1, 1, FVArt::Light(Shroom, 0.3f));
			I.Set(MX - 1, Base - Hh - 2, FColor(230, 255, 240));
			I.FillRect(MX - CapW, Base - Hh - 1, CapW * 2 + 1, 1, FVArt::Shade(Shroom, 0.3f));
		}
		for (int32 x = X - 8; x <= X + 8; ++x) { I.Blend(x, Base + 1, FVArt::WithAlpha(Shroom, 60)); }
	}
	/** Stalagmite on (X, Base), height Hh. */
	void Stalagmite(FFVImage& I, int32 X, int32 Base, int32 Hh, uint32 Seed)
	{
		ShadowEllipse(I, float(X), float(Base), float(Hh) * 0.35f + 2.f, 2.f);
		for (int32 y = 0; y < Hh; ++y)
		{
			const int32 HW = FMath::Max(0, int32(float(y) / float(Hh) * float(Hh) * 0.3f));
			for (int32 x = -HW; x <= HW; ++x)
			{
				FColor C = x < -HW / 3 ? RockHi : (x > HW / 3 ? FVArt::Shade(RockMid, 0.2f) : RockMid);
				if (((y + int32(FVArt::Noise(x, 0, Seed) * 3.f)) % 5) == 0) { C = FVArt::Shade(C, 0.15f); }
				I.Set(X + x, Base - Hh + y, C);
			}
			I.Set(X - HW - 1, Base - Hh + y, FVArt::OutlineOf(RockMid)); I.Set(X + HW + 1, Base - Hh + y, FVArt::OutlineOf(RockMid));
		}
		I.Set(X, Base - Hh, WetHi);
	}

	// ---- sprites ----
	FFVImage MakeBoulder(int32 Variant, bool bDepleted)
	{
		static const FColor Bases[4] = { FColor(120, 114, 126), FColor(128, 106, 92), FColor(100, 112, 138), FColor(110, 104, 112) };
		const int32 V = Variant & 3;
		const FColor B = Bases[V];
		FFVImage I(32, 28);
		FRandomStream R(0xB0 + V * 13);
		if (bDepleted)
		{
			Ellipse(I, 16.f, 25.f, 14.f, 3.f, FColor(30, 26, 36, 120), true);
			for (int32 k = 0; k < 7; ++k) { RockBlob(I, float(R.RandRange(5, 27)), float(R.RandRange(20, 25)), float(R.RandRange(2, 4)), float(R.RandRange(2, 3)), B, 40 + k + V * 7); }
			for (int32 k = 0; k < 10; ++k) { I.Set(R.RandRange(3, 28), R.RandRange(23, 27), R.FRand() < 0.5f ? FVArt::Light(B, 0.3f) : FVArt::Shade(B, 0.3f)); }
			I.Outline(FVArt::OutlineOf(B));
			return I;
		}
		Ellipse(I, 17.f, 24.5f, 14.f, 3.f, FColor(20, 16, 26, 140), true);
		const float RX = 12.f + float(V & 1) * 1.5f, RY = 10.5f - float(V & 1);
		RockBlob(I, 16.f, 25.f - RY - 1.f, RX, RY, B, 7 + V * 3, true);
		if (V == 1 || V == 3) { RockBlob(I, 24.f, 20.f, 5.f, 4.f, FVArt::Shade(B, 0.08f), 9 + V, true); }
		if (V == 2) { RockBlob(I, 8.f, 21.f, 5.f, 4.f, FVArt::Light(B, 0.05f), 11, true); }
		// strata + cracks
		for (int32 x = 5; x < 28; ++x)
		{
			const int32 Y = 12 + int32(3.f * FMath::Sin(float(x + V * 5) / 5.f));
			if (I.Get(x, Y).A > 0) { I.Set(x, Y, FVArt::Shade(B, 0.25f)); }
		}
		I.Line(10, 8, 13, 14, FVArt::Shade(B, 0.4f)); I.Line(13, 14, 11, 18, FVArt::Shade(B, 0.4f));
		if (V == 3)   // ore veins
		{
			for (int32 k = 0; k < 6; ++k) { const int32 X = R.RandRange(8, 24), Y = R.RandRange(8, 20); if (I.Get(X, Y).A > 0) { I.Set(X, Y, FColor(236, 190, 90)); I.Set(X + 1, Y, FColor(180, 130, 60)); } }
		}
		// a glint (valuables inside)
		I.Set(21, 9, FColor(240, 250, 255)); I.Set(20, 9, CrystalBlue); I.Set(22, 9, CrystalBlue); I.Set(21, 8, CrystalBlue); I.Set(21, 10, CrystalBlue);
		// moss / pebbles at the foot
		for (int32 k = 0; k < 5; ++k) { I.Set(R.RandRange(4, 28), R.RandRange(24, 26), FVArt::Light(B, 0.2f)); }
		I.Outline(FVArt::OutlineOf(B));
		return I;
	}
	FFVImage MakeFace(int32 Variant)
	{
		static const FColor Beds[4] = { FColor(60, 54, 66), FColor(66, 56, 52), FColor(52, 58, 72), FColor(62, 58, 60) };
		const FColor B = Beds[Variant & 3];
		FFVImage I(128, 96);
		for (int32 y = 0; y < 96; ++y)
		{
			for (int32 x = 0; x < 128; ++x)
			{
				const int32 LX = x & 15, LY = y & 15;
				const float N = Fbm(float(x) * 1.5f, float(y) * 1.5f, 0xFA + Variant);
				FColor C = N < 0.4f ? FVArt::Shade(B, 0.12f) : (N > 0.62f ? FVArt::Light(B, 0.1f) : B);
				// hollow of each cell: darker rim (the pit), lighter floor
				if (LX == 0 || LY == 0) { C = FVArt::Shade(B, 0.4f); }
				else if (LX == 1 || LY == 1) { C = FVArt::Shade(C, 0.25f); }
				else if (LX == 15 || LY == 15) { C = FVArt::Light(C, 0.12f); }
				if (((x + y * 2 + Variant * 7) % 23) == 0 && LX > 2 && LY > 2) { C = FVArt::Light(B, 0.25f); }   // specks
				I.Set(x, y, C);
			}
		}
		// strata lines across the bedrock
		for (int32 k = 0; k < 3; ++k)
		{
			for (int32 x = 0; x < 128; ++x)
			{
				const int32 Y = 18 + k * 30 + int32(4.f * FMath::Sin(float(x) / 14.f + float(Variant + k)));
				Mul(I, x, Y, 0.8f); Mul(I, x, Y + 1, 1.1f);
			}
		}
		return I;
	}
	FFVImage MakeCell(int32 Layer, int32 Variant)
	{
		static const FColor Layers[3] = { FColor(168, 144, 118), FColor(128, 118, 124), FColor(84, 80, 98) };
		const FColor B = Layers[FMath::Clamp(Layer, 1, 3) - 1];
		const uint32 Seed = 0xCE11 + uint32(Layer) * 31 + uint32(Variant) * 7;
		FFVImage I(16, 16);
		for (int32 y = 0; y < 16; ++y)
		{
			for (int32 x = 0; x < 16; ++x)
			{
				const float N = FVArt::Noise(x / 2, y / 2, Seed);
				FColor C = N < 0.3f ? FVArt::Shade(B, 0.1f) : (N > 0.78f ? FVArt::Light(B, 0.1f) : B);
				if (x == 0 || y == 0) { C = FVArt::Light(B, 0.3f); }                 // bevel: light top-left
				else if (x == 1 || y == 1) { C = FVArt::Light(C, 0.12f); }
				if (x == 15 || y == 15) { C = FVArt::Shade(B, 0.45f); }              // dark bottom-right
				else if (x == 14 || y == 14) { C = FVArt::Shade(C, 0.2f); }
				I.Set(x, y, C);
			}
		}
		FRandomStream R(Seed);
		const int32 V = Variant & 3;
		if (Layer == 1)   // soft top layer: pebbles and grit
		{
			for (int32 k = 0; k < 3 + V; ++k)
			{
				const int32 X = R.RandRange(3, 12), Y = R.RandRange(3, 12);
				I.Set(X, Y, FVArt::Light(B, 0.3f)); I.Set(X + 1, Y, FVArt::Shade(B, 0.2f)); I.Set(X, Y + 1, FVArt::Shade(B, 0.3f));
			}
		}
		else if (Layer == 2)   // medium rock: a crack and a stratum
		{
			int32 X = R.RandRange(3, 10), Y = 2;
			while (Y < 13) { I.Set(X, Y, FVArt::Shade(B, 0.45f)); I.Set(X + 1, Y, FVArt::Light(B, 0.15f)); Y += 1; X += R.RandRange(-1, 1); X = FMath::Clamp(X, 2, 12); }
			if (V & 1) { for (int32 x = 2; x < 14; ++x) { I.Set(x, 9 + (x + V) / 6, FVArt::Shade(B, 0.2f)); } }
		}
		else   // hard dark rock: faceted, several cracks, cold highlights
		{
			for (int32 k = 0; k < 2; ++k)
			{
				const int32 X0 = R.RandRange(2, 13), Y0 = R.RandRange(2, 5), X1 = R.RandRange(2, 13), Y1 = R.RandRange(10, 13);
				I.Line(X0, Y0, X1, Y1, FVArt::Shade(B, 0.5f));
			}
			I.Line(3, 3, 6, 3, WetHi); I.Set(3, 4, FVArt::Mix(B, WetHi, 0.5f));
			if (V >= 2) { I.Set(10, 6, FVArt::Mix(B, WetHi, 0.7f)); I.Set(11, 6, FVArt::Mix(B, WetHi, 0.4f)); }
		}
		return I;
	}

	// ---- def helpers (texels -> world px) ----
	FVector2D P(float X, float Y) { return FVector2D(X * FV::WorldScale, Y * FV::WorldScale); }
	FFVRect Rc(float X, float Y, float W, float H) { return FFVRect(X * FV::WorldScale, Y * FV::WorldScale, W * FV::WorldScale, H * FV::WorldScale); }
	void AddAnchor(FWorldDef& D, EAnchor K, const FName& Id, float X, float Y, int32 Index = 0, uint8 Variant = 0)
	{
		FAnchor A; A.Kind = K; A.Id = Id; A.Index = Index; A.Variant = Variant; A.Feet = P(X, Y);
		D.Anchors.Add(A);
	}
	void AddLight(FWorldDef& D, float X, float Y, float Radius, FColor C, bool bFlicker)
	{
		FAnchor A; A.Kind = EAnchor::Light; A.Feet = P(X, Y); A.Radius = Radius * FV::WorldScale; A.Color = C; A.bFlag = bFlicker;
		D.Anchors.Add(A);
	}
	/** Distance from P to the segment AB. */
	float SegDist(const FVector2D& Pt, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(FVector2D::DotProduct(Pt - A, AB) / FMath::Max(1.f, AB.SizeSquared()), 0.f, 1.f);
		return FVector2D::Distance(Pt, A + AB * T);
	}
}

void FVWorldU6::BuildCave(FFVImage& OutBg, FWorldDef& OutDef)
{
	using namespace FVArtCaveImpl;
	constexpr int32 W = 960, H = 400;
	constexpr int32 WallBase = 104, Shore = 262, LakeX = 350;
	FFVImage& I = OutBg;
	I.Init(W, H, FColor(0, 0, 0, 255));

	// the winding path from the adit down to the plank walk
	const FVector2D PathPts[] = { FVector2D(60, 104), FVector2D(90, 150), FVector2D(200, 180), FVector2D(250, 230), FVector2D(380, 240), FVector2D(480, 214), FVector2D(578, 250) };
	auto PathD = [&PathPts](float X, float Y)
	{
		float Best = 1e9f;
		for (int32 k = 0; k + 1 < int32(UE_ARRAY_COUNT(PathPts)); ++k) { Best = FMath::Min(Best, SegDist(FVector2D(X, Y), PathPts[k], PathPts[k + 1])); }
		return Best;
	};

	// 1) floor, walls, lake
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			const float N = Fbm(float(x), float(y), 0xCA7E);
			// lake region (curved left edge)
			const float LakeEdge = float(LakeX) - 18.f * FMath::Clamp(float(y - Shore) / 138.f, 0.f, 1.f) + 6.f * VN(float(y) / 10.f, 1.f, 0x1A);
			const bool bLake = y >= Shore && float(x) >= LakeEdge;
			const bool bShoreRim = !bLake && y >= Shore - 8 && float(x) >= LakeEdge - 2.f && x >= LakeX - 20;
			FColor C;
			if (bLake)
			{
				const float T = FMath::Clamp(float(y - Shore) / 110.f, 0.f, 1.f);
				C = Ramp(LakeShallow, LakeDeep, FMath::Pow(T, 0.7f), x, y);
				if (y < Shore + 3) { C = FVArt::Shade(C, 0.35f); }
				const float Wv = VN(float(x) / 12.f, float(y) / 3.f, 0x1AE);
				if (Wv > 0.8f && (y % 3) == 0) { C = FVArt::Mix(C, WetHi, 0.35f); }
			}
			else if (bShoreRim)
			{
				C = (N < 0.45f) ? RockMid : RockHi;
				if (y >= Shore - 2) { C = FVArt::Shade(C, 0.3f); }
				if (y == Shore - 8 || (FVArt::Noise(x, y, 0x5E) < 0.08f)) { C = FVArt::Mix(C, WetHi, 0.5f); }
			}
			else if (y < 40)   // ceiling
			{
				C = N < 0.45f ? FVArt::Shade(RockLo, 0.35f) : FVArt::Shade(RockLo, 0.15f);
			}
			else if (y < WallBase && !(x >= 36 && x < 84))   // wall face with strata
			{
				const float T = float(y - 40) / float(WallBase - 40);
				C = FVArt::Mix(FVArt::Shade(RockLo, 0.2f), RockMid, T);
				if (N > 0.6f) { C = FVArt::Light(C, 0.1f); }
				if (((y + int32(N * 12.f)) % 11) == 0) { C = FVArt::Shade(C, 0.25f); }
				if (y >= WallBase - 3) { C = FVArt::Shade(C, 0.3f); }
			}
			else if (x < 26 || x > 930 || (y > 352 && x < LakeX))   // outer rock (not walkable)
			{
				C = N < 0.5f ? RockLo : RockMid;
				if (((x + y) % 13) == 0) { C = FVArt::Shade(C, 0.2f); }
			}
			else   // floor
			{
				C = N < 0.38f ? FloorLo : (N < 0.66f ? FloorBase : FloorHi);
				const float Pd = PathD(float(x), float(y));
				if (Pd < 9.f + 3.f * VN(float(x) / 7.f, float(y) / 7.f, 0x9A)) { C = (FVArt::Noise(x / 2, y / 2, 0x9B) < 0.5f) ? PathLo : PathHi; }
			}
			I.Set(x, y, C);
		}
	}
	// the adit from the City (top-left): dark tunnel + timber frame
	for (int32 y = 0; y < WallBase; ++y) { for (int32 x = 36; x < 84; ++x)
	{
		const float T = float(y) / float(WallBase);
		I.Set(x, y, T < 0.55f ? FVArt::Mix(FColor(10, 8, 14), FColor(34, 30, 40), T / 0.55f) : ((FVArt::Noise(x / 2, y / 2, 0x51) < 0.5f) ? PathLo : PathHi));
	} }
	for (int32 k = 0; k < 3; ++k)   // props of the timbering, getting smaller into the tunnel
	{
		const int32 Y = 16 + k * 28, Inset = 10 - k * 4;
		I.FillRect(36 + Inset, Y, 4, 28, WoodDark); I.FillRect(80 - Inset - 4, Y, 4, 28, WoodDark);
		I.FillRect(36 + Inset, Y, 1, 28, Wood); I.FillRect(80 - Inset - 4, Y, 1, 28, Wood);
		I.FillRect(34 + Inset, Y - 3, 50 - Inset * 2, 4, Wood); I.FillRect(34 + Inset, Y - 3, 50 - Inset * 2, 1, WoodLight);
	}
	for (int32 y = 34; y < WallBase; y += 5) { I.FillRect(52, y, 12, 2, WoodDark); }   // rails going up
	I.FillRect(52, 34, 1, WallBase - 34, Iron); I.FillRect(63, 34, 1, WallBase - 34, Iron);

	// 2) stalactites hanging from the ceiling, over the wall
	{
		FRandomStream R(0x57A1);
		for (int32 x = 4; x < W - 4; x += 5 + R.RandRange(0, 6))
		{
			if (x >= 30 && x < 90) { continue; }
			const int32 L = 6 + R.RandRange(0, 18);
			for (int32 y = 0; y < L; ++y)
			{
				const int32 HW = FMath::Max(0, (L - y) * 3 / L);
				for (int32 dx = -HW; dx <= HW; ++dx) { I.Set(x + dx, 36 + y, dx < 0 ? RockHi : (dx > 0 ? FVArt::Shade(RockMid, 0.3f) : RockMid)); }
			}
			I.Set(x, 36 + L, WetHi);
			if (R.FRand() < 0.3f) { I.Set(x, 36 + L + 3, FVArt::WithAlpha(WetHi, 200)); }   // a falling drop
		}
		for (int32 x = 0; x < W; ++x) { if ((x & 1) == 0) { I.Set(x, 36, FVArt::Shade(RockLo, 0.4f)); } }
	}

	// 3) rails with a mine cart by the back wall (flat rails; the cart stands at the back line)
	for (int32 x = 90; x < 340; ++x)
	{
		if ((x % 6) < 2) { I.FillRect(x, WallBase + 2, 2, 7, WoodDark); }
		I.Set(x, WallBase + 3, Iron); I.Set(x, WallBase + 7, Iron);
		I.Set(x, WallBase + 2, FVArt::Light(Iron, 0.4f)); I.Set(x, WallBase + 6, FVArt::Light(Iron, 0.4f));
	}
	{
		const int32 CX = 290, Base = WallBase + 6;
		ShadowEllipse(I, float(CX), float(Base), 14.f, 2.5f);
		I.FillRect(CX - 13, Base - 16, 26, 13, FVArt::OutlineOf(Iron));
		I.FillRect(CX - 12, Base - 15, 24, 11, FColor(110, 90, 80));
		I.FillRect(CX - 12, Base - 15, 24, 2, FColor(140, 120, 104)); I.FillRect(CX - 12, Base - 9, 24, 1, Iron);
		for (int32 k = 0; k < 5; ++k) { Ellipse(I, float(CX - 8 + k * 4), float(Base - 16), 3.f, 2.f, k & 1 ? RockHi : FColor(120, 110, 104)); }
		I.Set(CX - 4, Base - 17, CrystalBlue);
		Ellipse(I, float(CX - 7), float(Base - 2), 3.f, 3.f, Iron); Ellipse(I, float(CX + 7), float(Base - 2), 3.f, 3.f, Iron);
		I.Set(CX - 7, Base - 2, FColor(140, 140, 150)); I.Set(CX + 7, Base - 2, FColor(140, 140, 150));
	}

	// 4) plank walk into the lake (Dock) + ladder-planks on the shore
	{
		const int32 X0 = 560, X1 = 596, Y0 = Shore - 10, Y1 = 318;
		for (int32 y = Shore; y < Y1 + 5; ++y) { for (int32 x = X1; x < X1 + 4; ++x) { Mul(I, x, y, 0.7f); } }
		for (int32 PY = Shore + 6; PY <= Y1; PY += 14)
		{
			for (const int32 PX : { X0 - 1, X1 - 2 }) { I.FillRect(PX, PY, 3, 9, WoodDark); I.FillRect(PX, PY, 1, 9, Wood); I.Set(PX + 1, PY + 9, WetHi); }
		}
		for (int32 y = Y0; y < Y1; ++y)
		{
			for (int32 x = X0; x < X1; ++x)
			{
				const int32 In = (y - Y0) % 4;
				FColor C = FVArt::Noise((y - Y0) / 4, x / 6, 0xD0C) < 0.4f ? FVArt::Shade(Wood, 0.12f) : Wood;
				if (In == 0) { C = FVArt::Light(C, 0.15f); }
				if (In == 3) { C = FVArt::Shade(C, 0.35f); }
				I.Set(x, y, C);
			}
		}
		I.FillRect(X0 - 1, Y0, 1, Y1 - Y0, PropOut); I.FillRect(X1, Y0, 1, Y1 - Y0, PropOut);
		I.FillRect(X0 - 1, Y1, X1 - X0 + 2, 2, PropOut);
	}

	// 5) the underwater spring: light water patch with bubble rings
	{
		const float SX = 780.f, SY = 330.f;
		for (int32 y = int32(SY) - 16; y <= int32(SY) + 16; ++y) { for (int32 x = int32(SX) - 30; x <= int32(SX) + 30; ++x)
		{
			const float DX = (float(x) - SX) / 30.f, DY = (float(y) - SY) / 16.f;
			const float D = DX * DX + DY * DY;
			if (D < 1.f && I.InBounds(x, y)) { I.Set(x, y, FVArt::Mix(I.Get(x, y), FColor(110, 200, 220), (1.f - D) * 0.55f * (Bayer(x, y) < 0.8f ? 1.f : 0.7f))); }
		} }
		for (int32 k = 0; k < 3; ++k)
		{
			const float Rr = 5.f + float(k) * 6.f;
			for (int32 a = 0; a < 48; ++a) { if ((a & 1) == 0) { const float An = float(a) / 48.f * 2.f * PI; I.Set(int32(SX + FMath::Cos(An) * Rr * 1.6f), int32(SY + FMath::Sin(An) * Rr * 0.7f), FColor(200, 240, 250)); } }
		}
		FRandomStream RB(0xB0B);
		for (int32 k = 0; k < 10; ++k) { const int32 BX = int32(SX) + RB.RandRange(-8, 8), BY = int32(SY) + RB.RandRange(-10, 4); I.Set(BX, BY, FColor(240, 252, 255)); I.Set(BX + 1, BY, FColor(170, 220, 236)); }
	}

	// 6) drip puddles with rings on the floor
	{
		const FIntPoint Pd[] = { { 150, 130 }, { 330, 150 }, { 420, 120 }, { 640, 150 }, { 760, 200 }, { 120, 320 }, { 250, 330 } };
		for (const FIntPoint& Q : Pd)
		{
			for (int32 y = Q.Y - 3; y <= Q.Y + 3; ++y) { for (int32 x = Q.X - 8; x <= Q.X + 8; ++x)
			{
				const float DX = float(x - Q.X) / 8.f, DY = float(y - Q.Y) / 3.f;
				if (DX * DX + DY * DY < 1.f) { I.Set(x, y, FVArt::Mix(LakeShallow, FloorLo, 0.3f)); }
			} }
			for (int32 a = 0; a < 24; ++a) { const float An = float(a) / 24.f * 2.f * PI; I.Set(Q.X + int32(FMath::Cos(An) * 4.f), Q.Y + int32(FMath::Sin(An) * 1.5f), WetHi); }
			I.Set(Q.X - 5, Q.Y - 1, FColor(200, 214, 236));
		}
	}

	// 7) wet highlights and pebbles on the floor
	{
		FRandomStream R(0xFE);
		for (int32 k = 0; k < 400; ++k)
		{
			const int32 X = R.RandRange(28, 928), Y = R.RandRange(WallBase + 4, 350);
			if (Y >= Shore - 10 && X >= LakeX - 20) { continue; }
			const FColor C = I.Get(X, Y);
			I.Set(X, Y, FVArt::Light(C, 0.25f)); I.Set(X + 1, Y + 1, FVArt::Shade(C, 0.25f));
		}
		for (int32 k = 0; k < 16; ++k)   // rocks along the lake shore
		{
			const int32 X = R.RandRange(LakeX + 10, 930);
			RockBlob(I, float(X), float(Shore - 5), float(R.RandRange(3, 6)), 2.5f, RockMid, 300 + k, true);
		}
	}

	// 8) decor at the edges: stalagmites, crystals, mushrooms (outside the walkable area / at the back line)
	{
		const FIntPoint Stg[] = { { 12, 150 }, { 18, 240 }, { 10, 330 }, { 40, 380 }, { 120, 372 }, { 200, 390 }, { 290, 376 }, { 944, 150 }, { 948, 220 }, { 938, 110 },
			{ 110, WallBase }, { 400, WallBase }, { 620, WallBase }, { 820, WallBase } };
		for (int32 k = 0; k < int32(UE_ARRAY_COUNT(Stg)); ++k) { Stalagmite(I, Stg[k].X, Stg[k].Y, 14 + (k * 7) % 14, 0x5A + k); }
	}
	struct FGlow { int32 X, Y; bool bShroom; bool bViolet; };
	const FGlow Glows[] = {
		{ 140, WallBase, false, false }, { 262, WallBase - 1, false, true }, { 470, WallBase, false, false }, { 700, WallBase, false, true },
		{ 900, 128, false, false }, { 30, 300, false, true }, { 330, 366, false, false }, { 34, 200, true, false }, { 916, 240, true, false },
		{ 160, 362, true, false }, { 520, WallBase, true, false }, { 360, 250, false, true } };
	for (int32 k = 0; k < int32(UE_ARRAY_COUNT(Glows)); ++k)
	{
		const FGlow& G = Glows[k];
		if (G.bShroom) { Mushrooms(I, G.X, G.Y, 0x300 + k); }
		else { Crystals(I, G.X, G.Y, G.bViolet ? CrystalViolet : CrystalBlue, 0x200 + k); }
	}
	// lantern posts (at the adit and at the plank walk)
	auto Lantern = [&I](int32 X, int32 Base)
	{
		ShadowEllipse(I, float(X), float(Base), 4.f, 1.5f);
		I.FillRect(X - 1, Base - 24, 2, 24, WoodDark); I.FillRect(X - 1, Base - 24, 1, 24, Wood);
		I.FillRect(X - 1, Base - 24, 7, 2, WoodDark);
		I.FillRect(X + 3, Base - 22, 5, 6, FVArt::OutlineOf(Iron)); I.FillRect(X + 4, Base - 21, 3, 4, FColor(255, 214, 120));
	};
	Lantern(96, WallBase + 12);
	Lantern(552, Shore - 6);

	// 9) dark rim of the world
	for (int32 k = 0; k < 6; ++k)
	{
		const float F = 0.55f + 0.3f * float(k) / 5.f;
		for (int32 x = k; x < W - k; ++x) { Mul(I, x, k, F); Mul(I, x, H - 1 - k, F); }
		for (int32 y = k + 1; y < H - k - 1; ++y) { Mul(I, k, y, F); Mul(I, W - 1 - k, y, F); }
	}

	// ---- the def ----
	FWorldDef& D = OutDef;
	D.LocationId = FVDBU6::CaveId();
	D.SizeTex = FIntPoint(W, H);
	D.Walk.Add(Rc(30, WallBase, LakeX - 30, 352 - WallBase));        // left floor (down to the bottom rocks)
	D.Walk.Add(Rc(LakeX - 20, WallBase, 924 - (LakeX - 20), Shore - 4 - WallBase));   // right floor above the lake
	D.Walk.Add(Rc(40, 60, 36, WallBase - 60 + 2));                   // the adit mouth
	D.Walk.Add(Rc(562, Shore - 6, 32, 318 - Shore + 4));             // plank walk
	D.bWater = true;
	D.ShoreY = float(Shore) * FV::WorldScale;
	D.WaterArea = Rc(LakeX - 18, Shore, W - (LakeX - 18), H - Shore);
	D.Dock = Rc(558, Shore - 10, 40, 318 - Shore + 10);
	D.bDust = true;
	D.DarkRadius = 150.f;
	D.Ambient = FColor(96, 96, 140);

	AddAnchor(D, EAnchor::Spawn, FName(TEXT("default")), 60, 120);
	AddAnchor(D, EAnchor::Spawn, FName(TEXT("from_city")), 60, 120);
	AddAnchor(D, EAnchor::Spawn, FName(TEXT("from_map")), 60, 120);
	{
		FAnchor A; A.Kind = EAnchor::Exit; A.Id = FName(TEXT("from_cave")); A.Target = FVDBU6::CityId();
		A.Feet = P(60, 68); A.Hit = Rc(42, 60, 36, 12); A.Label = TEXT("В Город ↑");
		A.SignFeet = P(94, 108); A.SignDir = uint8(FVWorldU6::EWayDir::Up);   // Р69: the way sign — houses and the lighthouse
		D.Anchors.Add(A);
	}
	{
		const FIntPoint B[8] = { { 170, 160 }, { 300, 146 }, { 110, 250 }, { 220, 300 }, { 430, 170 }, { 540, 196 }, { 700, 160 }, { 840, 206 } };
		for (int32 k = 0; k < 8; ++k) { AddAnchor(D, EAnchor::Boulder, FName(*FString::Printf(TEXT("boulder_%02d"), k)), float(B[k].X), float(B[k].Y), k, uint8(k & 3)); }
	}
	for (int32 k = 0; k < int32(UE_ARRAY_COUNT(Glows)); ++k)
	{
		const FGlow& G = Glows[k];
		const FColor C = G.bShroom ? Shroom : (G.bViolet ? CrystalViolet : CrystalBlue);
		AddLight(D, float(G.X), float(G.Y - 6), G.bShroom ? 26.f : 36.f, C, false);
	}
	AddLight(D, 101.f, float(WallBase - 7), 40.f, FColor(255, 196, 110), true);
	AddLight(D, 557.f, float(Shore - 25), 40.f, FColor(255, 196, 110), true);
	AddLight(D, 780.f, 330.f, 34.f, FColor(120, 220, 255), false);   // the underwater spring
}

const FFVSprite& UFVSprites::BoulderSprite(int32 Variant, bool bDepleted)
{
	Variant &= 3;
	const uint64 Key = MakeKey(FVSpriteCat::Cave, 1, uint32(Variant), bDepleted ? 1 : 0);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildBoulder(Variant, bDepleted), FString::Printf(TEXT("Boulder_%d_%d"), Variant, bDepleted ? 1 : 0));
}
const FFVSprite& UFVSprites::ExcavationFace(int32 Variant)
{
	Variant &= 3;
	const uint64 Key = MakeKey(FVSpriteCat::Cave, 2, uint32(Variant));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtCaveImpl::MakeFace(Variant), FString::Printf(TEXT("ExcavationFace_%d"), Variant));
}
const FFVSprite& UFVSprites::ExcavationCell(int32 Layer, int32 Variant)
{
	Layer = FMath::Clamp(Layer, 1, 3);
	Variant &= 3;
	const uint64 Key = MakeKey(FVSpriteCat::Cave, 3, uint32(Layer), uint8(Variant));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtCaveImpl::MakeCell(Layer, Variant), FString::Printf(TEXT("ExcavationCell_%d_%d"), Layer, Variant));
}
FFVImage UFVSprites::BuildBoulder(int32 Variant, bool bDepleted)
{
	return FVArtCaveImpl::MakeBoulder(Variant, bDepleted);
}
