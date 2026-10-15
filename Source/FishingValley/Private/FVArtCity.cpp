// UPDATE 6 [ART-CITY] — the City: big world (pier, streets, shops, boutique, lighthouse), junk piles, cave rubble, the boat & the voyage (Docs/UPDATE6_FOREST_CITY.md §7, §8, Р38). Owner: ART-CITY.
// Contract: Public/FVSprites.h «UPDATE 6», Private/FVWorldU6.h.
// Style: pixel art like FVSceneArt.cpp / FVArtHome.cpp (Stardew-like shading via FVArt::Shade/Light, 1px dark outline
// on props), deterministic (own FRandomStream), cached through UFVSprites::AddCached(MakeKey(FVSpriteCat::<cat>, ...)).
//
// World 1280x360 texels (px = texel * FV::WorldScale). Plan (texels):
//   y 0..~60  hillside with trees; y ~40..140 house fronts (baked, the player never walks behind them);
//   y 140..152 sidewalk with back-line props (lamps, barrels, benches — baked); y 152..250 cobbled embankment (walk);
//   y 250..258 quay top, 258..266 quay wall, water below (ShoreY = 262). x 0..112 river mouth, x 1150+ the cove.
//   Pier 394..426 into the water (Dock), boat berth x ~150..176, old dock piles x 940..1040 (PilesArea),
//   cave adit with rubble_0..2 at x 1092..1128 (walk notch up to y 96, exit «В пещеру» behind the rubble), lighthouse right.
//   The square (x 548..668) is kept free (future players). The task board «city_board» is baked on the square.
// Sprites (pivot bottom-centre, cache FVSpriteCat::City, A = kind):
//   JunkPileSprite 24x16 (4 variants; depleted = rust stain)            A=1
//   RubbleSprite   48x34 (3 variants; cleared = stones pushed aside)    A=2
//   BoatSprite     72x40 stages 0..2 on trestles, 3 afloat (hull cut at the waterline, y=37) A=3
//   VoyageSprite   layer 0 sky 320x180; 1 far sea + coast 320x90; 2 near waves 320x48 (4 frames, tile in X);
//                  3 boat with the player 64x56 (2 frames); layer 3 pivot bottom-centre, layers 0..2 top-left.  A=4

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVWorldU6.h"

namespace FVArtCityImpl
{
	using FVWorldU6::EAnchor;
	using FVWorldU6::FAnchor;
	using FVWorldU6::FWorldDef;

	// ---- palette (UPDATE6_ART.md §3, Город) ----
	const FColor PropOut(58, 36, 30);
	const FColor CobLo(140, 132, 124), CobBase(168, 160, 150), CobHi(196, 190, 180), CobSeam(110, 104, 100);
	const FColor Plaster(236, 222, 190), Ochre(222, 180, 110), BluePl(120, 170, 200), PinkPl(226, 170, 150), MintPl(170, 206, 170);
	const FColor Tile(178, 70, 52), TileDark(128, 48, 40), SlateRoof(92, 100, 120);
	const FColor SeaShallow(72, 150, 180), SeaDeep(20, 66, 112), Foam(226, 240, 240);
	const FColor Wood(150, 104, 62), WoodDark(104, 68, 42), WoodLight(190, 142, 90);
	const FColor Stone(120, 116, 112), StoneDark(84, 80, 82), StoneLight(160, 156, 150);
	const FColor Grass(92, 150, 70), GrassDark(62, 112, 56), GrassLight(126, 180, 84);
	const FColor Iron(70, 70, 80), Rust(150, 80, 44), RustDark(104, 54, 34), Gold(236, 190, 70), Glass(70, 104, 140);

	float Smooth(float T) { return T * T * (3.f - 2.f * T); }
	float VN(float X, float Y, uint32 S)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = Smooth(X - float(X0)), FY = Smooth(Y - float(Y0));
		const float A = FVArt::Noise(X0, Y0, S), B = FVArt::Noise(X0 + 1, Y0, S);
		const float C = FVArt::Noise(X0, Y0 + 1, S), D = FVArt::Noise(X0 + 1, Y0 + 1, S);
		return FMath::Lerp(FMath::Lerp(A, B, FX), FMath::Lerp(C, D, FX), FY);
	}
	float Fbm(float X, float Y, uint32 S) { return 0.5f * VN(X / 28.f, Y / 28.f, S) + 0.3f * VN(X / 11.f, Y / 11.f, S + 7) + 0.2f * VN(X / 4.f, Y / 4.f, S + 13); }
	float Bayer(int32 X, int32 Y)
	{
		static const int32 M[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
		return (float(M[(Y & 3) * 4 + (X & 3)]) + 0.5f) / 16.f;
	}
	/** Dithered 4-step ramp between two colours. */
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
	/** Ground shadow under an object (multiplier 0.74, +2 in X). */
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
	/** Opaque box with a 1px outline, a light top row and a shaded bottom row. */
	void Box(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor C)
	{
		I.FillRect(X, Y, W, H, FVArt::OutlineOf(C));
		if (W > 2 && H > 2)
		{
			I.FillRect(X + 1, Y + 1, W - 2, H - 2, C);
			I.FillRect(X + 1, Y + 1, W - 2, 1, FVArt::Light(C, 0.25f));
			I.FillRect(X + 1, Y + H - 2, W - 2, 1, FVArt::Shade(C, 0.25f));
		}
	}
	void Planks(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor C, bool bVertical, int32 Step, uint32 Seed)
	{
		for (int32 y = Y; y < Y + H; ++y)
		{
			for (int32 x = X; x < X + W; ++x)
			{
				const int32 Along = bVertical ? (x - X) : (y - Y);
				const int32 Board = Along / Step, In = Along % Step;
				const float N = FVArt::Noise(Board, bVertical ? y / 6 : x / 6, Seed);
				FColor P = N < 0.3f ? FVArt::Shade(C, 0.08f) : (N > 0.8f ? FVArt::Light(C, 0.08f) : C);
				if (In == 0) { P = FVArt::Light(P, 0.18f); }
				if (In == Step - 1) { P = FVArt::Shade(P, 0.35f); }
				I.Set(x, y, P);
			}
		}
	}
	void Lamp(FFVImage& I, int32 X, int32 Base)
	{
		ShadowEllipse(I, float(X), float(Base), 4.f, 1.5f);
		I.FillRect(X - 2, Base - 3, 5, 3, Iron);
		I.FillRect(X - 1, Base - 28, 2, 26, Iron);
		I.Set(X - 1, Base - 28, FVArt::Light(Iron, 0.3f));
		I.FillRect(X - 3, Base - 36, 7, 8, FVArt::OutlineOf(Iron));
		I.FillRect(X - 2, Base - 35, 5, 6, FColor(250, 222, 150));
		I.FillRect(X - 1, Base - 34, 2, 3, FColor(255, 246, 210));
		I.FillRect(X - 4, Base - 38, 9, 2, Iron);
		I.Set(X, Base - 39, Iron);
	}
	void Barrel(FFVImage& I, int32 X, int32 Base)
	{
		ShadowEllipse(I, float(X), float(Base), 6.f, 2.f);
		for (int32 y = Base - 13; y < Base; ++y)
		{
			const float T = float(y - (Base - 13)) / 13.f;
			const int32 HW = 5 + (T > 0.2f && T < 0.8f ? 1 : 0);
			for (int32 x = X - HW; x <= X + HW; ++x)
			{
				FColor C = (x - X) < -2 ? FVArt::Light(Wood, 0.1f) : ((x - X) > 2 ? FVArt::Shade(Wood, 0.25f) : Wood);
				if (((x - X + 6) % 3) == 0) { C = FVArt::Shade(C, 0.15f); }
				if (y == Base - 11 || y == Base - 3) { C = Iron; }
				I.Set(x, y, C);
			}
			I.Set(X - HW - 1, y, PropOut); I.Set(X + HW + 1, y, PropOut);
		}
		Ellipse(I, float(X) + 0.5f, float(Base - 13), 5.f, 1.6f, WoodLight);
		I.FillRect(X - 5, Base, 11, 1, PropOut);
	}
	void Crate(FFVImage& I, int32 X, int32 Base, int32 S)
	{
		ShadowEllipse(I, float(X), float(Base), float(S) * 0.6f, 2.f);
		Box(I, X - S / 2, Base - S, S, S, WoodLight);
		I.Line(X - S / 2 + 1, Base - S + 1, X + S / 2 - 2, Base - 2, Wood);
		I.FillRect(X - S / 2 + 1, Base - S + 3, S - 2, 1, Wood);
	}
	/** A little A-frame chalk menu board (Р66: street props at the tavern and the restaurant). */
	void MenuBoard(FFVImage& I, int32 X, int32 Base)
	{
		ShadowEllipse(I, float(X), float(Base), 6.f, 2.f);
		I.Line(X - 5, Base - 1, X - 3, Base - 16, WoodDark); I.Line(X + 5, Base - 1, X + 3, Base - 16, WoodDark);
		Box(I, X - 5, Base - 16, 11, 11, FColor(50, 58, 54));
		for (int32 k = 0; k < 3; ++k) { I.FillRect(X - 3, Base - 14 + k * 3, 5 + (k & 1) * 2, 1, FColor(230, 230, 220)); }
		I.FillRect(X - 5, Base - 17, 11, 1, Wood);
	}
	void Flowerbox(FFVImage& I, int32 X, int32 Y, int32 W, uint32 Seed)
	{
		static const FColor Fl[4] = { FColor(236, 80, 90), FColor(250, 210, 80), FColor(240, 150, 200), FColor(250, 250, 240) };
		for (int32 x = X; x < X + W; ++x)
		{
			if (FVArt::Noise(x, Y, Seed) < 0.75f) { I.Set(x, Y - 1, GrassDark); I.Set(x, Y - 2, Grass); }
			if (FVArt::Noise(x, Y, Seed + 1) < 0.35f) { I.Set(x, Y - 2 - int32(FVArt::Noise(x, 3, Seed) * 2.f), Fl[(uint32(x) + Seed) & 3]); }
		}
		I.FillRect(X - 1, Y, W + 2, 3, TileDark);
		I.FillRect(X - 1, Y, W + 2, 1, Tile);
	}
	void Window(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Wall, bool bShutters, uint32 Seed)
	{
		I.FillRect(X - 1, Y - 1, W + 2, H + 2, FVArt::Shade(Wall, 0.45f));
		I.FillRect(X, Y, W, H, Plaster);
		for (int32 y = Y + 1; y < Y + H - 1; ++y)
		{
			for (int32 x = X + 1; x < X + W - 1; ++x)
			{
				const float T = float(y - Y) / float(H);
				I.Set(x, y, FVArt::Mix(FVArt::Light(Glass, 0.2f), FVArt::Shade(Glass, 0.3f), T));
			}
		}
		I.FillRect(X + W / 2, Y + 1, 1, H - 2, Plaster);
		I.FillRect(X + 1, Y + H / 2, W - 2, 1, Plaster);
		I.Set(X + 2, Y + 2, FColor(220, 236, 250)); I.Set(X + 3, Y + 2, FColor(180, 206, 230)); I.Set(X + 2, Y + 3, FColor(180, 206, 230));
		I.FillRect(X - 1, Y + H, W + 2, 1, FVArt::Light(Wall, 0.3f));
		if (bShutters)
		{
			const FColor Sh = (Seed & 1) ? FColor(80, 130, 110) : FColor(80, 110, 160);
			Planks(I, X - 5, Y, 4, H, Sh, true, 2, Seed);
			Planks(I, X + W + 1, Y, 4, H, Sh, true, 2, Seed + 1);
		}
	}
	void Door(FFVImage& I, int32 X, int32 Base, int32 W, int32 H, FColor C, uint32 Seed)
	{
		I.FillRect(X - 2, Base - H - 2, W + 4, H + 2, StoneLight);
		I.FillRect(X - 1, Base - H - 1, W + 2, H + 1, FVArt::Shade(C, 0.5f));
		Planks(I, X, Base - H, W, H, C, true, 3, Seed);
		I.Set(X + W - 3, Base - H / 2, Gold);
		I.FillRect(X - 3, Base, W + 6, 2, StoneLight);
		I.FillRect(X - 3, Base + 1, W + 6, 1, Stone);
	}
	void Awning(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor A, FColor B)
	{
		for (int32 y = Y; y < Y + H; ++y)
		{
			for (int32 x = X; x < X + W; ++x)
			{
				FColor C = (((x - X) / 4) & 1) ? B : A;
				C = FVArt::Mix(FVArt::Light(C, 0.1f), FVArt::Shade(C, 0.2f), float(y - Y) / float(FMath::Max(1, H)));
				I.Set(x, y, C);
			}
		}
		for (int32 x = X; x < X + W; ++x)   // scalloped edge
		{
			const int32 L = (x - X) % 4;
			if (L == 1 || L == 2) { I.Set(x, Y + H, (((x - X) / 4) & 1) ? FVArt::Shade(B, 0.2f) : FVArt::Shade(A, 0.2f)); }
			Mul(I, x, Y + H + 1 + (L == 1 || L == 2 ? 1 : 0), 0.8f);
		}
		I.FillRect(X - 1, Y - 1, W + 2, 1, PropOut);
	}
	void Sign(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor C)
	{
		I.FillRect(X - 1, Y - 1, W + 2, H + 2, PropOut);
		I.FillRect(X, Y, W, H, C);
		I.FillRect(X, Y, W, 1, FVArt::Light(C, 0.3f));
		I.FillRect(X, Y + H - 1, W, 1, FVArt::Shade(C, 0.3f));
	}
	void AnchorGlyph(FFVImage& I, int32 X, int32 Y, FColor C)   // 7x8 anchor (top-left)
	{
		I.Set(X + 3, Y, C); I.FillRect(X + 3, Y + 1, 1, 6, C); I.FillRect(X + 1, Y + 2, 5, 1, C);
		I.Set(X, Y + 5, C); I.Set(X + 6, Y + 5, C); I.Set(X + 1, Y + 6, C); I.Set(X + 5, Y + 6, C); I.FillRect(X + 2, Y + 7, 3, 1, C);
	}
	void FishGlyph(FFVImage& I, int32 X, int32 Y, FColor C)   // 10x5 fish (top-left)
	{
		I.FillRect(X + 2, Y + 1, 5, 3, C); I.FillRect(X + 3, Y, 3, 5, C); I.Set(X + 1, Y + 2, C);
		I.Set(X + 7, Y + 2, C); I.Set(X + 8, Y + 1, C); I.Set(X + 8, Y + 3, C); I.Set(X + 9, Y, C); I.Set(X + 9, Y + 4, C);
		I.Set(X + 2, Y + 2, PropOut);
	}
	void RockBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Base, uint32 Seed)
	{
		for (int32 y = FMath::FloorToInt(CY - RY); y <= FMath::CeilToInt(CY + RY); ++y)
		{
			for (int32 x = FMath::FloorToInt(CX - RX); x <= FMath::CeilToInt(CX + RX); ++x)
			{
				const float DX = (float(x) + 0.5f - CX) / RX, DY = (float(y) + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY + (FVArt::Noise(x, y, Seed) - 0.5f) * 0.18f;
				if (D > 1.f) { continue; }
				const float L = -DX * 0.6f - DY * 0.8f;   // light from the top-left
				FColor C = Base;
				if (L > 0.45f) { C = FVArt::Light(Base, 0.3f); }
				else if (L > 0.05f) { C = FVArt::Light(Base, 0.1f); }
				else if (L < -0.55f) { C = FVArt::Shade(Base, 0.35f); }
				else if (L < -0.15f) { C = FVArt::Shade(Base, 0.15f); }
				if (FVArt::Noise(x / 2, y / 2, Seed + 1) < 0.08f) { C = FVArt::Shade(C, 0.2f); }
				I.Set(x, y, C);
			}
		}
	}

	// ---- houses ----
	struct FHouse
	{
		int32 X = 0, W = 64, WallH = 60, RoofH = 26;
		FColor Wall = Plaster, Roof = Tile;
		bool bShutters = false, bChimney = true;
	};
	void PaintRoof(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Roof, uint32 Seed)
	{
		for (int32 y = Y; y < Y + H; ++y)
		{
			const int32 Row = (y - Y) / 4, LY = (y - Y) % 4;
			const float T = float(y - Y) / float(FMath::Max(1, H));
			for (int32 x = X; x < X + W; ++x)
			{
				const int32 Off = (Row & 1) * 3;
				const int32 LX = (x - X + Off) % 6;
				FColor C = FVArt::Mix(FVArt::Light(Roof, 0.18f), FVArt::Shade(Roof, 0.12f), T);
				if (FVArt::Noise((x - X + Off) / 6, Row, Seed) < 0.18f) { C = FVArt::Shade(C, 0.12f); }
				if (LY == 3) { C = FVArt::Shade(C, 0.4f); }
				else if (LY == 0) { C = FVArt::Light(C, 0.12f); }
				if (LX == 0 && LY != 3) { C = FVArt::Shade(C, 0.25f); }
				I.Set(x, y, C);
			}
		}
		I.FillRect(X, Y, W, 1, FVArt::OutlineOf(Roof));
		I.FillRect(X, Y + 1, W, 1, FVArt::Light(Roof, 0.35f));   // ridge
		I.FillRect(X, Y, 1, H, FVArt::OutlineOf(Roof));
		I.FillRect(X + W - 1, Y, 1, H, FVArt::OutlineOf(Roof));
	}
	void PaintWall(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Wall, uint32 Seed)
	{
		for (int32 y = Y; y < Y + H; ++y)
		{
			for (int32 x = X; x < X + W; ++x)
			{
				const float N = VN(float(x) / 5.f, float(y) / 5.f, Seed);
				FColor C = N < 0.25f ? FVArt::Shade(Wall, 0.06f) : (N > 0.8f ? FVArt::Light(Wall, 0.06f) : Wall);
				if (x - X < 2) { C = FVArt::Light(C, 0.1f); }
				if (X + W - x <= 3) { C = FVArt::Shade(C, 0.18f); }
				I.Set(x, y, C);
			}
		}
		for (int32 y = Y + H - 6; y < Y + H; ++y)   // stone plinth
		{
			for (int32 x = X; x < X + W; ++x)
			{
				const FColor C = ((x - X + ((y - Y) / 3) * 4) % 8 == 0 || (y - (Y + H - 6)) % 3 == 0) ? StoneDark : Stone;
				I.Set(x, y, C);
			}
		}
		I.FillRect(X, Y, 1, H, FVArt::OutlineOf(Wall));
		I.FillRect(X + W - 1, Y, 1, H, FVArt::OutlineOf(Wall));
		I.FillRect(X, Y + H - 7, W, 1, FVArt::Light(Wall, 0.2f));
	}
	void PaintHouse(FFVImage& I, const FHouse& H, int32 Base, uint32 Seed)
	{
		const int32 WallTop = Base - H.WallH;
		const int32 RoofY = WallTop - H.RoofH;
		if (H.bChimney)
		{
			const int32 CX = H.X + H.W - 14 - int32(Seed % 8);
			Box(I, CX, RoofY - 8, 7, 14, FColor(160, 92, 70));
			I.FillRect(CX - 1, RoofY - 9, 9, 2, StoneDark);
		}
		PaintRoof(I, H.X - 3, RoofY, H.W + 6, H.RoofH, H.Roof, Seed);
		PaintWall(I, H.X, WallTop, H.W, H.WallH, H.Wall, Seed + 3);
		I.FillRect(H.X, WallTop, H.W, 2, FVArt::Shade(H.Wall, 0.4f));   // eave shadow
		I.FillRect(H.X - 3, WallTop - 1, H.W + 6, 1, FVArt::OutlineOf(H.Roof));
		const int32 NW = FMath::Max(1, (H.W - 8) / 20);
		const int32 Gap = H.W / NW;
		for (int32 k = 0; k < NW; ++k)
		{
			const int32 WX = H.X + k * Gap + Gap / 2 - 5;
			Window(I, WX, WallTop + 6, 10, 13, H.Wall, H.bShutters, Seed + k);
			Flowerbox(I, WX, WallTop + 20, 10, Seed + 11 * k);
		}
		for (int32 x = H.X + 2; x < H.X + H.W + 4; ++x) { Mul(I, x, Base, 0.72f); Mul(I, x, Base + 1, 0.82f); }   // shadow on the sidewalk
	}

	// ---- ground & water ----
	FColor Cobble(int32 X, int32 Y)
	{
		const int32 Row = Y / 6, LY = Y % 6;
		const int32 Off = (Row & 1) * 5;
		const int32 Col = (X + Off) / 10, LX = (X + Off) % 10;
		if (LX == 0 || LY == 0) { return CobSeam; }
		const float N = FVArt::Noise(Col, Row, 0xC0B1);
		FColor C = N < 0.33f ? CobLo : (N < 0.8f ? CobBase : CobHi);
		if (VN(float(X) / 40.f, float(Y) / 30.f, 0x51) < 0.3f) { C = FVArt::Mix(C, FColor(150, 136, 120), 0.35f); }
		if (LY == 1 || LX == 1) { C = FVArt::Light(C, 0.12f); }
		if (LY == 5 || LX == 9) { C = FVArt::Shade(C, 0.16f); }
		if ((LX == 1 || LX == 9) && (LY == 1 || LY == 5)) { C = FVArt::Mix(C, CobSeam, 0.5f); }
		return C;
	}
	FColor Sea(int32 X, int32 Y, int32 Top, int32 Bottom, uint32 Seed)
	{
		const float T = FMath::Clamp(float(Y - Top) / float(FMath::Max(1, Bottom - Top)), 0.f, 1.f);
		FColor C = Ramp(SeaShallow, SeaDeep, FMath::Pow(T, 0.8f), X, Y);
		const float Wv = VN(float(X) / 14.f, float(Y) / 3.f, Seed);
		if (Wv > 0.74f && (Y % 3) == 0) { C = FVArt::Light(C, 0.3f); }
		else if (Wv < 0.18f && (Y % 4) == 1) { C = FVArt::Shade(C, 0.15f); }
		return C;
	}
	void Hillside(FFVImage& I, int32 X0, int32 X1, int32 Y1)
	{
		for (int32 y = 0; y < Y1; ++y)
		{
			for (int32 x = X0; x < X1; ++x)
			{
				const float N = Fbm(float(x), float(y), 0x4111);
				I.Set(x, y, N < 0.4f ? GrassDark : (N < 0.62f ? Grass : GrassLight));
			}
		}
		FRandomStream R(0xC17A);   // dense crowns in rows (like CanopyBand)
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 x = X0 - 8; x < X1 + 8; x += 14 + R.RandRange(0, 6))
			{
				const float CX = float(x), CY = float(4 + Row * 14 + R.RandRange(-3, 3)), Rad = float(R.RandRange(9, 13));
				const FColor Base = (R.FRand() < 0.3f) ? FColor(54, 112, 72) : FColor(70, 130, 64);
				Ellipse(I, CX + 1.f, CY + 2.f, Rad + 1.f, Rad * 0.85f + 1.f, FVArt::Mix(FColor(40, 80, 50), FColor(10, 20, 24), 0.5f));
				Ellipse(I, CX, CY, Rad, Rad * 0.85f, FVArt::Shade(Base, 0.1f));
				Ellipse(I, CX - Rad * 0.25f, CY - Rad * 0.25f, Rad * 0.65f, Rad * 0.55f, Base);
				Ellipse(I, CX - Rad * 0.4f, CY - Rad * 0.4f, Rad * 0.3f, Rad * 0.25f, FVArt::Light(Base, 0.2f));
			}
		}
	}
	void Bollard(FFVImage& I, int32 X, int32 Base)
	{
		ShadowEllipse(I, float(X), float(Base), 4.f, 1.5f);
		I.FillRect(X - 3, Base - 7, 6, 7, FVArt::OutlineOf(Iron));
		I.FillRect(X - 2, Base - 6, 4, 6, Iron);
		I.FillRect(X - 4, Base - 8, 8, 2, FVArt::Light(Iron, 0.2f));
		I.Set(X - 2, Base - 5, FVArt::Light(Iron, 0.4f));
	}

	// ---- sprites ----
	FFVImage MakeJunk(int32 Variant, bool bDepleted)
	{
		FFVImage I(24, 16);
		FRandomStream R(0x1A2B + Variant * 97);
		if (bDepleted)
		{
			for (int32 y = 9; y < 16; ++y) { for (int32 x = 3; x < 21; ++x)
			{
				const float DX = (float(x) - 11.5f) / 9.f, DY = (float(y) - 12.5f) / 3.5f;
				if (DX * DX + DY * DY < 1.f && FVArt::Noise(x, y, Variant) < 0.8f) { I.Set(x, y, FVArt::WithAlpha(FVArt::Mix(Rust, CobSeam, FVArt::Noise(x, y, 5)), 150)); }
			} }
			I.Set(8, 12, Iron); I.Set(9, 12, FVArt::Light(Iron, 0.3f)); I.Set(15, 13, RustDark);
			return I;
		}
		const int32 V = Variant & 3;
		auto Can = [&I](int32 X, int32 Y, FColor C, bool bLying)
		{
			if (bLying)
			{
				I.FillRect(X, Y, 6, 4, C); I.FillRect(X, Y, 6, 1, FVArt::Light(C, 0.3f)); I.FillRect(X, Y + 3, 6, 1, FVArt::Shade(C, 0.3f));
				I.FillRect(X + 5, Y, 1, 4, FColor(190, 190, 196)); I.Set(X + 2, Y + 1, FVArt::Shade(C, 0.3f));
			}
			else
			{
				I.FillRect(X, Y, 4, 6, C); I.FillRect(X, Y, 4, 1, FColor(190, 190, 196)); I.FillRect(X, Y + 1, 1, 5, FVArt::Light(C, 0.25f));
				I.FillRect(X + 3, Y + 1, 1, 5, FVArt::Shade(C, 0.3f)); I.Set(X + 2, Y + 3, FVArt::Shade(C, 0.35f));
			}
		};
		auto Pipe = [&I](int32 X, int32 Y, int32 L)
		{
			I.FillRect(X, Y, L, 3, Rust); I.FillRect(X, Y, L, 1, FVArt::Light(Rust, 0.3f)); I.FillRect(X, Y + 2, L, 1, RustDark);
			I.FillRect(X + L - 1, Y - 1, 2, 5, FVArt::Shade(Rust, 0.1f)); I.Set(X + L, Y + 1, RustDark);
			for (int32 k = 2; k < L - 1; k += 4) { I.Set(X + k, Y + 1, RustDark); }
		};
		auto Wheel = [&I](int32 CX, int32 CY)
		{
			Ellipse(I, float(CX) + 0.5f, float(CY) + 0.5f, 5.5f, 4.f, FColor(52, 50, 56));
			Ellipse(I, float(CX) + 0.5f, float(CY) + 0.5f, 3.5f, 2.5f, FColor(96, 94, 100));
			Ellipse(I, float(CX) + 0.5f, float(CY) + 0.5f, 1.5f, 1.f, Rust);
			I.Set(CX - 3, CY - 2, FColor(120, 118, 124));
		};
		Ellipse(I, 12.f, 13.f, 10.f, 2.5f, FColor(96, 80, 64, 200), true);
		if (V == 0) { Can(3, 7, FColor(200, 60, 50), false); Can(8, 9, FColor(70, 130, 190), true); Can(14, 6, FColor(210, 190, 80), false); Pipe(12, 11, 8); }
		else if (V == 1) { Pipe(2, 8, 14); Pipe(5, 11, 13); Can(17, 5, FColor(90, 150, 90), false); I.Line(4, 6, 10, 7, Iron); }
		else if (V == 2) { Wheel(8, 9); Can(15, 8, FColor(200, 60, 50), false); Can(15, 11, FColor(190, 190, 196), true); }
		else
		{
			I.FillRect(4, 5, 8, 8, FColor(120, 124, 130)); I.FillRect(4, 5, 8, 1, FColor(170, 174, 180)); I.FillRect(11, 6, 1, 7, FColor(80, 84, 92));
			I.FillRect(5, 9, 6, 1, Rust); I.Line(4, 5, 7, 2, Iron); I.Line(7, 2, 11, 5, Iron);
			for (int32 k = 0; k < 8; ++k) { I.Set(13 + k, 11 + (k & 1), (k & 1) ? Iron : FColor(120, 120, 128)); }
			Can(15, 6, FColor(210, 190, 80), false);
		}
		for (int32 k = 0; k < 5; ++k) { I.Set(R.RandRange(2, 21), R.RandRange(12, 14), R.FRand() < 0.5f ? Rust : FColor(150, 150, 156)); }
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeRubble(int32 Index, bool bCleared)
	{
		FFVImage I(48, 34);
		FRandomStream R(0x7B0 + Index * 31);
		if (bCleared)
		{
			Ellipse(I, 24.f, 30.f, 20.f, 3.5f, FColor(70, 62, 58, 120), true);
			RockBlob(I, 6.f, 29.f, 5.f, 3.5f, Stone, 11 + Index);
			RockBlob(I, 42.f, 29.f, 5.f, 4.f, StoneDark, 12 + Index);
			RockBlob(I, 38.f, 31.f, 3.f, 2.f, Stone, 13 + Index);
			for (int32 k = 0; k < 8; ++k) { I.Set(R.RandRange(4, 44), R.RandRange(28, 33), R.FRand() < 0.5f ? StoneLight : StoneDark); }
			I.Outline(PropOut);
			return I;
		}
		Ellipse(I, 24.f, 29.f, 22.f, 4.5f, FColor(60, 54, 50, 170), true);
		RockBlob(I, 24.f, 20.f, 18.f, 12.f, StoneDark, 21 + Index);
		const int32 N = 7 + Index;
		for (int32 k = 0; k < N; ++k)
		{
			const float CX = float(R.RandRange(8, 40)), CY = float(R.RandRange(10, 28));
			const float RX = float(R.RandRange(4, 8));
			const FColor Base = R.FRand() < 0.5f ? Stone : FColor(132, 118, 104);
			RockBlob(I, CX, CY, RX, RX * 0.75f, Base, 30 + k + Index * 10);
		}
		I.Line(6, 12, 20, 22, WoodDark); I.Line(6, 13, 20, 23, Wood); I.Line(7, 13, 21, 23, WoodLight);   // broken props of the adit
		I.Line(30, 6, 40, 20, WoodDark); I.Line(31, 6, 41, 20, Wood);
		for (int32 k = 0; k < 10; ++k) { I.Set(R.RandRange(6, 42), R.RandRange(24, 31), StoneLight); }
		I.Outline(PropOut);
		return I;
	}

	// ---- the boat (72x40, pivot bottom-centre) ----
	FFVImage MakeBoat(int32 Stage)
	{
		Stage = FMath::Clamp(Stage, 0, 3);
		FFVImage I(72, 40);
		const bool bAfloat = Stage == 3;
		const int32 X0 = 8, X1 = 64;
		const int32 Keel = bAfloat ? 36 : 30;
		const FColor Weathered(150, 146, 138), NewRib(226, 190, 130), Fresh(200, 150, 90), Tar(60, 40, 34), WaterLine(178, 50, 40);
		auto T01 = [X0, X1](int32 X) { return float(X - X0) / float(X1 - X0); };
		auto Top = [&](int32 X) { const float T = T01(X); return float(Keel - 14) - 4.f * FMath::Square(2.f * T - 1.f) - (T > 0.8f ? (T - 0.8f) * 18.f : 0.f); };
		auto Bot = [&](int32 X) { const float T = T01(X); const float E = FMath::Abs(2.f * T - 1.f); return float(Keel) - 9.f * E * E * E - (T > 0.85f ? (T - 0.85f) * 30.f : 0.f); };
		auto Far = [&](int32 X) { const float T = T01(X); return Top(X) - 6.f * FMath::Sin(PI * FMath::Clamp(T, 0.f, 1.f)); };
		if (!bAfloat)
		{
			Ellipse(I, 36.f, 37.f, 32.f, 3.f, FColor(90, 70, 50, 110), true);   // shadow on the sand
			for (const int32 TX : { 20, 50 })   // trestles
			{
				I.Line(TX - 5, 38, TX, 29, WoodDark); I.Line(TX + 5, 38, TX, 29, WoodDark); I.Line(TX - 4, 38, TX + 1, 29, Wood);
				I.FillRect(TX - 6, 29, 12, 2, Wood); I.FillRect(TX - 6, 29, 12, 1, WoodLight);
			}
		}
		for (int32 x = X0; x <= X1; ++x)   // the far rim and the inside
		{
			const int32 YF = FMath::RoundToInt(Far(x)), YT = FMath::RoundToInt(Top(x));
			for (int32 y = YF; y < YT; ++y)
			{
				FColor In = Stage == 0 ? FColor(110, 104, 96) : (Stage == 1 ? FColor(90, 70, 50) : (Stage == 2 ? FVArt::Shade(Fresh, 0.25f) : FColor(120, 84, 54)));
				if (Stage == 1 && ((x - X0) % 6) == 0) { In = NewRib; }
				if (Stage >= 2 && ((x - X0) % 7) == 0) { In = FVArt::Shade(In, 0.2f); }
				if (Stage == 0 && FVArt::Noise(x, y, 3) < 0.12f) { In = FVArt::Shade(In, 0.3f); }
				I.Set(x, y, In);
			}
			if (YF < YT) { I.Set(x, YF, Stage == 0 ? FVArt::Shade(Weathered, 0.2f) : (Stage == 3 ? FVArt::Light(Tar, 0.2f) : (Stage == 1 ? NewRib : Fresh))); }
		}
		for (int32 x = X0; x <= X1; ++x)   // the hull side
		{
			const int32 YT = FMath::RoundToInt(Top(x)), YB = FMath::RoundToInt(Bot(x));
			for (int32 y = YT; y <= YB; ++y)
			{
				const int32 Row = (y - YT) / 3;
				FColor C;
				if (Stage == 0) { C = FVArt::Noise(x / 5, Row, 9) < 0.5f ? Weathered : FVArt::Shade(Weathered, 0.12f); }
				else if (Stage == 1) { continue; }   // only the ribs (drawn below)
				else if (Stage == 2) { C = FVArt::Noise(x / 8, Row, 4) < 0.5f ? Fresh : FVArt::Light(Fresh, 0.08f); }
				else { C = y >= YB - 3 ? WaterLine : Tar; }
				if ((y - YT) % 3 == 2) { C = FVArt::Shade(C, 0.22f); }
				if (y == YT) { C = FVArt::Light(C, 0.3f); }
				if (float(y) > (float(YT) + float(YB)) * 0.5f + 2.f) { C = FVArt::Shade(C, 0.1f); }
				I.Set(x, y, C);
			}
		}
		if (Stage == 0)
		{
			FRandomStream R(77);
			for (int32 k = 0; k < 4; ++k)   // holes with ribs showing
			{
				const int32 HX = 16 + k * 11 + R.RandRange(0, 4), HY = Keel - 7 + R.RandRange(-2, 2);
				Ellipse(I, float(HX), float(HY), 3.f, 2.f, FColor(40, 34, 36));
				I.FillRect(HX, HY - 2, 1, 4, FVArt::Shade(Weathered, 0.35f));
			}
			I.Line(38, 14, 34, 4, FVArt::Shade(Weathered, 0.3f)); I.Line(39, 14, 35, 4, Weathered);   // broken mast
			I.Set(33, 3, Weathered); I.Set(36, 3, Weathered);
			Ellipse(I, 48.f, 14.f, 7.f, 3.f, FColor(200, 190, 170)); Ellipse(I, 50.f, 13.f, 3.f, 1.5f, FColor(170, 160, 142));   // sail heap
			I.Line(42, 15, 54, 12, FColor(160, 150, 132));
		}
		else if (Stage == 1)
		{
			for (int32 x = X0 + 3; x < X1 - 2; x += 6)   // new ribs
			{
				const int32 YT = FMath::RoundToInt(Top(x)), YB = FMath::RoundToInt(Bot(x));
				for (int32 y = YT; y <= YB; ++y) { I.Set(x, y, NewRib); I.Set(x + 1, y, FVArt::Shade(NewRib, 0.25f)); }
			}
			for (int32 x = X0; x <= X1; ++x)   // keel and gunwale
			{
				I.Set(x, FMath::RoundToInt(Bot(x)), FVArt::Shade(NewRib, 0.15f));
				I.Set(x, FMath::RoundToInt(Bot(x)) - 1, NewRib);
				I.Set(x, FMath::RoundToInt(Top(x)), NewRib);
			}
			I.FillRect(X0 - 1, Keel - 12, 2, 12, NewRib); I.FillRect(X1, Keel - 16, 2, 14, NewRib);
		}
		else if (Stage == 2)
		{
			for (int32 x = X0 + 2; x < X1; x += 5)   // rows of silver nails
			{
				const int32 YT = FMath::RoundToInt(Top(x)), YB = FMath::RoundToInt(Bot(x));
				for (int32 y = YT + 1; y < YB; y += 3) { I.Set(x, y, FColor(214, 220, 230)); }
			}
			Box(I, 50, Keel - 22, 10, 6, Wood);
			I.FillRect(20, Keel - 21, 6, 5, FColor(40, 36, 38)); I.FillRect(20, Keel - 21, 6, 1, FColor(90, 80, 84));   // a pot of pitch waiting
		}
		else
		{
			I.FillRect(35, 1, 2, 23, WoodDark); I.FillRect(35, 1, 1, 23, Wood);   // mast + furled sail
			I.FillRect(28, 7, 16, 1, WoodDark);
			Ellipse(I, 36.f, 8.5f, 8.f, 2.f, FColor(236, 228, 206)); I.Line(29, 9, 43, 9, FColor(200, 190, 170));
			for (int32 k = 30; k < 43; k += 4) { I.Set(k, 8, FColor(170, 60, 50)); }
			I.FillRect(62, 10, 1, 8, Iron); I.FillRect(60, 6, 5, 5, FVArt::OutlineOf(Iron)); I.FillRect(61, 7, 3, 3, FColor(255, 214, 120));   // bow lantern
			I.Set(64, 3, FColor(200, 60, 50));
		}
		I.Outline(PropOut);
		if (bAfloat) { for (int32 x = X0 - 4; x <= X1 + 4; ++x) { if ((x % 5) < 3) { I.Set(x, Keel + 2, FColor(226, 240, 240, 200)); } } }
		return I;
	}

	// ---- voyage layers ----
	void Cloud(FFVImage& I, int32 CX, int32 CY, int32 W, uint32 Seed)
	{
		FRandomStream R(Seed);
		for (int32 k = 0; k < 5; ++k)
		{
			const float X = float(CX - W / 2 + k * W / 5 + R.RandRange(0, 4)), Rad = float(R.RandRange(5, 9));
			Ellipse(I, X, float(CY) - Rad * 0.3f, Rad, Rad * 0.7f, FColor(250, 250, 246));
		}
		for (int32 x = CX - W / 2 - 10; x < CX + W / 2 + 10; ++x) { for (int32 y = CY; y < CY + 5; ++y) { if (I.Get(x, y) == FColor(250, 250, 246)) { I.Set(x, y, FColor(214, 222, 236)); } } }
	}
	FFVImage MakeVoyage(int32 Layer, int32 Frame)
	{
		if (Layer <= 0)
		{
			FFVImage I(320, 180);
			for (int32 y = 0; y < 180; ++y) { for (int32 x = 0; x < 320; ++x)
			{
				const float T = float(y) / 180.f;
				I.Set(x, y, T < 0.6f ? Ramp(FColor(92, 160, 226), FColor(170, 214, 240), T / 0.6f, x, y) : Ramp(FColor(170, 214, 240), FColor(250, 226, 196), (T - 0.6f) / 0.4f, x, y));
			} }
			Cloud(I, 50, 40, 44, 1); Cloud(I, 170, 26, 60, 2); Cloud(I, 270, 58, 40, 3); Cloud(I, 120, 86, 30, 4);
			return I;
		}
		if (Layer == 1)
		{
			FFVImage I(320, 90);
			for (int32 x = 0; x < 320; ++x)   // the coast silhouette
			{
				const int32 H = 30 - int32(10.f * VN(float(x) / 30.f, 0.f, 0x5EA) + 6.f * VN(float(x) / 9.f, 3.f, 0x5EB));
				for (int32 y = H; y < 40; ++y) { I.Set(x, y, FVArt::Mix(FColor(96, 130, 150), FColor(120, 150, 164), float(y - H) / 12.f)); }
			}
			for (int32 k = 0; k < 9; ++k)   // the far City
			{
				const int32 X = 200 + k * 10, H = 6 + (k * 7) % 5;
				I.FillRect(X, 36 - H, 8, H, FColor(150, 164, 176));
				I.FillRect(X - 1, 35 - H, 10, 2, FColor(170, 110, 100));
				I.Set(X + 3, 38 - H, FColor(230, 220, 170));
			}
			I.FillRect(300, 14, 4, 22, FColor(236, 236, 236)); I.FillRect(300, 20, 4, 3, FColor(190, 70, 60)); I.FillRect(300, 28, 4, 3, FColor(190, 70, 60));
			I.FillRect(299, 10, 6, 4, FColor(70, 70, 80)); I.FillRect(300, 11, 4, 2, FColor(255, 230, 150));
			for (int32 y = 40; y < 90; ++y) { for (int32 x = 0; x < 320; ++x)
			{
				FColor C = Ramp(FColor(110, 170, 200), FColor(56, 120, 170), float(y - 40) / 50.f, x, y);
				if (VN(float(x) / 16.f, float(y) / 2.f, 0x77) > 0.78f && (y & 1) == 0) { C = FVArt::Light(C, 0.25f); }
				I.Set(x, y, C);
			} }
			I.FillRect(0, 40, 320, 1, FColor(170, 210, 226));
			return I;
		}
		if (Layer == 2)
		{
			FFVImage I(320, 48);
			const float Ph = float(Frame & 3) * 0.25f;
			for (int32 y = 0; y < 48; ++y) { for (int32 x = 0; x < 320; ++x)
			{
				const float Top = 6.f + FMath::Sin((float(x) / 320.f * 8.f + Ph) * 2.f * PI) * 3.f;
				if (float(y) < Top) { continue; }
				FColor C = Ramp(FColor(56, 130, 176), FColor(24, 70, 120), float(y) / 48.f, x, y);
				if (float(y) < Top + 1.5f) { C = Foam; }
				else if (float(y) < Top + 3.f) { C = FColor(150, 206, 226); }
				if (VN((float(x) + Ph * 40.f) / 12.f, float(y) / 2.f, 0x9A) > 0.76f && (y % 3) == 0) { C = FVArt::Light(C, 0.3f); }
				I.Set(x, y, C);
			} }
			return I;
		}
		FFVImage I(64, 56);   // layer 3: the boat with the player
		const int32 Bob = Frame & 1;
		for (int32 y = 4; y < 36; ++y)   // sail
		{
			const float T = float(y - 4) / 32.f;
			const int32 SW = int32(4.f + T * 20.f) + (Bob && y > 12 && y < 30 ? 1 : 0);
			for (int32 x = 33; x < 33 + SW; ++x) { I.Set(x, y + Bob, FVArt::Mix(FColor(250, 246, 234), FColor(214, 204, 186), float(x - 33) / 24.f)); }
			if ((y % 8) == 0) { for (int32 x = 34; x < 33 + SW; ++x) { I.Set(x, y + Bob, FColor(220, 120, 100)); } }
		}
		I.FillRect(31, 2 + Bob, 2, 38, WoodDark); I.FillRect(31, 2 + Bob, 1, 38, Wood);
		I.FillRect(32, 36 + Bob, 26, 1, WoodDark);
		const int32 PY = 26 + Bob;   // the player at the stern: straw hat, blue shirt
		I.FillRect(14, PY + 2, 8, 10, FColor(70, 110, 170)); I.FillRect(14, PY + 2, 2, 10, FColor(96, 140, 196));
		I.FillRect(15, PY - 5, 6, 7, FColor(240, 196, 150)); I.Set(19, PY - 3, FColor(56, 30, 42));
		I.FillRect(13, PY - 7, 10, 2, FColor(214, 176, 90)); I.FillRect(15, PY - 9, 6, 2, FColor(214, 176, 90));
		for (int32 x = 4; x < 60; ++x)   // hull
		{
			const float T = float(x - 4) / 56.f;
			const int32 Top = 38 + Bob - int32(4.f * FMath::Square(2.f * T - 1.f)) - (T > 0.85f ? int32((T - 0.85f) * 26.f) : 0);
			const float E = FMath::Abs(2.f * T - 1.f);
			const int32 Bottom = 50 + Bob - int32(8.f * E * E * E);
			for (int32 y = Top; y <= Bottom; ++y)
			{
				FColor C = y >= Bottom - 2 ? FColor(178, 50, 40) : FColor(60, 40, 34);
				if ((y - Top) % 3 == 2) { C = FVArt::Shade(C, 0.2f); }
				if (y == Top) { C = Wood; }
				I.Set(x, y, C);
			}
		}
		I.Outline(PropOut);
		for (int32 x = 0; x < 64; ++x) { if (((x + Bob * 2) % 6) < 4) { I.Set(x, 52 + Bob, Foam); } }
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
}

void FVWorldU6::BuildCity(FFVImage& OutBg, FWorldDef& OutDef)
{
	using namespace FVArtCityImpl;
	constexpr int32 W = 1280, H = 360;
	constexpr int32 Sidewalk = 140, WalkTop = 152, QuayTop = 250, WallTop = 258, WaterTop = 266;
	FFVImage& I = OutBg;
	I.Init(W, H, FColor(0, 0, 0, 255));

	// 1) hillside behind the roofs
	Hillside(I, 0, W, 70);

	// 2) sidewalk slabs, cobbles, quay, sea
	for (int32 y = Sidewalk; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			FColor C;
			if (y < WalkTop)
			{
				const bool Seam = ((x % 16) == 0) || (y == Sidewalk + 5);
				C = Seam ? CobSeam : (FVArt::Noise(x / 16, y / 6, 0x5D) < 0.5f ? CobHi : FVArt::Mix(CobHi, CobBase, 0.5f));
				if (y == WalkTop - 1) { C = FVArt::Shade(C, 0.25f); }
			}
			else if (y < QuayTop) { C = Cobble(x, y); }
			else if (y < WallTop)
			{
				C = ((x % 14) == 0) ? CobSeam : StoneLight;
				if (y == QuayTop) { C = FVArt::Light(C, 0.2f); }
				if (y == WallTop - 1) { C = FVArt::Shade(StoneLight, 0.2f); }
			}
			else if (y < WaterTop)
			{
				const int32 Row = (y - WallTop) / 4;
				C = (((x + Row * 6) % 12) == 0 || ((y - WallTop) % 4) == 3) ? StoneDark : Stone;
				C = FVArt::Shade(C, 0.1f + 0.05f * float(y - WallTop));
				if (y >= WaterTop - 2 && FVArt::Noise(x, y, 0xA1) < 0.5f) { C = FColor(70, 104, 70); }   // weed at the waterline
			}
			else
			{
				C = Sea(x, y, WaterTop, H, 0x5EA);
				if (y < WaterTop + 3) { C = FVArt::Shade(C, 0.3f); }                     // shadow of the wall
				else if (y == WaterTop + 3 && ((x / 3) % 3) != 0) { C = Foam; }          // foam line
			}
			I.Set(x, y, C);
		}
	}

	// 3) river mouth (x 0..112): the river runs from the top into the sea
	for (int32 y = 0; y < WaterTop; ++y)
	{
		const float Cx = 52.f + 10.f * FMath::Sin(float(y) / 38.f);
		const float HalfW = 24.f + FMath::Max(0.f, float(y - 150)) * 0.35f;
		for (int32 x = 0; x < 124; ++x)
		{
			const float Dd = FMath::Abs(float(x) - Cx);
			if (Dd < HalfW)
			{
				FColor C = Ramp(FColor(104, 186, 220), FColor(50, 120, 170), Dd / HalfW, x, y);
				if (VN(float(x) / 3.f, float(y) / 10.f, 0xE5) > 0.72f && (x % 3) == 0) { C = FVArt::Light(C, 0.35f); }   // current strokes
				I.Set(x, y, C);
			}
			else if (Dd < HalfW + 4.f) { I.Set(x, y, FVArt::Noise(x, y, 0xB4) < 0.5f ? StoneDark : Stone); }
			else if (x < 112 || y < Sidewalk)
			{
				const float N = Fbm(float(x), float(y), 0x22);
				I.Set(x, y, N < 0.42f ? GrassDark : (N < 0.66f ? Grass : GrassLight));
			}
		}
	}
	for (int32 y = WaterTop; y < H; ++y) { for (int32 x = 0; x < 110; ++x)
	{
		if (VN(float(x) / 4.f, float(y) / 12.f, 0xE6) > 0.7f && (x % 3) == 1) { Mul(I, x, y, 1.18f); }   // the current into the sea
	} }
	{
		FRandomStream RR(0xAB);
		for (int32 k = 0; k < 12; ++k) { RockBlob(I, float(RR.RandRange(84, 118)), float(RR.RandRange(150, 262)), float(RR.RandRange(3, 7)), float(RR.RandRange(3, 5)), Stone, 50 + k); }
		for (int32 k = 0; k < 5; ++k) { RockBlob(I, float(RR.RandRange(10, 100)), float(RR.RandRange(280, 340)), 4.f, 2.5f, StoneDark, 70 + k); }
	}

	// 4) houses along the top
	struct FSpot { int32 X, W, WallH, RoofH; FColor Wall, Roof; bool bShutters; };
	const FSpot Houses[] = {
		{ 120, 78, 70, 26, BluePl, SlateRoof, false },                  // 0 warehouse by the boat berth
		{ 204, 90, 56, 22, Plaster, Tile, true },                       // 1 fish row (Зоя)
		{ 300, 92, 76, 28, Ochre, Tile, true },                         // 2 tavern «Три якоря» (Марфа)
		{ 400, 66, 64, 24, BluePl, Tile, false },                       // 3 tackle (Кузьма)
		{ 474, 68, 58, 24, MintPl, TileDark, true },                    // 4 gardener (Агафья)
		{ 676, 100, 84, 22, FColor(240, 232, 214), SlateRoof, false },  // 5 boutique (Жанна)
		{ 784, 70, 70, 24, PinkPl, Tile, true },                        // 6 marine station (Лидия)
		{ 862, 84, 62, 24, Ochre, Tile, false },                        // 7 restaurant (Анри)
		{ 952, 90, 52, 22, FColor(170, 130, 96), WoodDark, false },     // 8 net shed (Никифор)
		{ 1048, 30, 44, 18, BluePl, Tile, false },                      // 9 pilot booth (Ефим)
	};
	for (int32 k = 0; k < int32(UE_ARRAY_COUNT(Houses)); ++k)
	{
		const FSpot& S = Houses[k];
		FHouse Hs; Hs.X = S.X; Hs.W = S.W; Hs.WallH = S.WallH; Hs.RoofH = S.RoofH; Hs.Wall = S.Wall; Hs.Roof = S.Roof; Hs.bShutters = S.bShutters;
		Hs.bChimney = k != 5 && k != 8;
		PaintHouse(I, Hs, Sidewalk, 0x400 + uint32(k) * 17);
	}
	auto WallY = [&Houses](int32 k) { return Sidewalk - Houses[k].WallH; };

	// warehouse: big gate + sign
	Door(I, 146, Sidewalk, 26, 30, WoodDark, 3);
	Sign(I, 140, WallY(0) + 30, 38, 8, FColor(236, 222, 190)); FishGlyph(I, 154, WallY(0) + 31, BluePl);
	// fish row: awning, ice counter with fish
	Awning(I, 206, Sidewalk - 34, 86, 8, FColor(70, 130, 190), FColor(240, 240, 236));
	Box(I, 212, Sidewalk - 18, 74, 16, WoodLight);
	I.FillRect(214, Sidewalk - 17, 70, 4, FColor(220, 240, 250));
	for (int32 k = 0; k < 7; ++k) { FishGlyph(I, 215 + k * 10, Sidewalk - 18, (k & 1) ? FColor(170, 190, 200) : FColor(210, 140, 110)); }
	// tavern: sign with three anchors, door
	Door(I, 340, Sidewalk, 14, 24, WoodDark, 5);
	Sign(I, 316, WallY(2) + 40, 60, 10, WoodDark);
	AnchorGlyph(I, 322, WallY(2) + 41, Gold); AnchorGlyph(I, 342, WallY(2) + 41, Gold); AnchorGlyph(I, 362, WallY(2) + 41, Gold);
	// tackle: shop window with rods, fish sign on a bracket
	Door(I, 446, Sidewalk, 12, 22, FColor(70, 110, 150), 6);
	I.FillRect(406, Sidewalk - 30, 34, 22, PropOut); I.FillRect(407, Sidewalk - 29, 32, 20, FVArt::Shade(Glass, 0.1f));
	for (int32 k = 0; k < 5; ++k) { I.Line(410 + k * 6, Sidewalk - 10, 414 + k * 6, Sidewalk - 28, (k & 1) ? WoodLight : FColor(200, 70, 60)); }
	I.FillRect(452, WallY(3) + 26, 14, 1, Iron); I.FillRect(465, WallY(3) + 24, 1, 4, Iron);
	Sign(I, 454, WallY(3) + 28, 12, 7, Gold); FishGlyph(I, 455, WallY(3) + 29, WoodDark);
	// gardener: cart with seed packets, baskets, a pumpkin
	Door(I, 480, Sidewalk, 12, 22, WoodDark, 7);
	Box(I, 500, Sidewalk - 16, 34, 10, Wood);
	for (int32 k = 0; k < 6; ++k) { I.FillRect(502 + k * 5, Sidewalk - 19, 4, 5, (k % 3 == 0) ? FColor(236, 200, 110) : ((k % 3 == 1) ? FColor(200, 230, 170) : FColor(240, 170, 150))); }
	Ellipse(I, 504.f, Sidewalk - 4.f, 4.f, 3.f, FColor(96, 70, 44)); Ellipse(I, 530.f, Sidewalk - 4.f, 4.f, 3.f, FColor(96, 70, 44));
	Ellipse(I, 504.f, Sidewalk - 4.f, 1.5f, 1.2f, Iron); Ellipse(I, 530.f, Sidewalk - 4.f, 1.5f, 1.2f, Iron);
	RockBlob(I, 540.f, Sidewalk - 4.f, 5.f, 4.f, FColor(236, 140, 50), 90); I.FillRect(540, Sidewalk - 9, 1, 2, GrassDark);
	Ellipse(I, 492.f, Sidewalk - 5.f, 4.f, 3.f, WoodLight); I.FillRect(489, Sidewalk - 9, 6, 2, FColor(236, 120, 50));

	// the square (x 546..676): paved back with the task board under a roof, the fisherman statue, benches
	for (int32 y = 70; y < Sidewalk; ++y) { for (int32 x = 546; x < 676; ++x)
	{
		const float N = Fbm(float(x), float(y), 0x33);
		if (y < 96) { I.Set(x, y, N < 0.45f ? GrassDark : Grass); }
		else { I.Set(x, y, ((x + (y / 5) * 3) % 9 == 0 || y % 5 == 0) ? StoneDark : (N < 0.5f ? Stone : StoneLight)); }
	} }
	for (int32 x = 548; x < 674; x += 12) { Ellipse(I, float(x), 84.f, 7.f, 6.f, FColor(70, 130, 64)); Ellipse(I, float(x) - 2.f, 82.f, 3.f, 2.5f, GrassLight); }
	PaintRoof(I, 562, 92, 48, 10, TileDark, 0x77);
	I.FillRect(566, 102, 2, 38, WoodDark); I.FillRect(604, 102, 2, 38, WoodDark);
	Box(I, 568, 104, 36, 28, Wood);
	for (int32 k = 0; k < 6; ++k)   // notices with seals
	{
		const int32 NX = 571 + (k % 3) * 11, NY = 107 + (k / 3) * 12;
		I.FillRect(NX, NY, 9, 10, FColor(249, 238, 210)); I.FillRect(NX + 1, NY + 2, 6, 1, FColor(150, 140, 120)); I.FillRect(NX + 1, NY + 4, 5, 1, FColor(150, 140, 120));
		I.FillRect(NX + 6, NY + 7, 2, 1, FColor(200, 50, 50));
		I.Set(NX + 4, NY, FColor(200, 60, 60));
	}
	I.FillRect(598, 104, 5, 5, Gold); I.Set(600, 105, PropOut); I.Set(600, 107, PropOut);   // «сведение» mark
	ShadowEllipse(I, 640.f, float(Sidewalk), 12.f, 3.f);
	Box(I, 628, Sidewalk - 18, 24, 18, StoneLight);
	Box(I, 632, Sidewalk - 22, 16, 5, Stone);
	{
		const FColor Br(120, 150, 130);   // bronze fisherman with a rod and a fish
		I.FillRect(636, Sidewalk - 44, 8, 22, Br); I.FillRect(636, Sidewalk - 44, 2, 22, FVArt::Light(Br, 0.25f));
		Ellipse(I, 640.f, Sidewalk - 48.f, 4.f, 4.f, Br); I.FillRect(634, Sidewalk - 53, 12, 2, FVArt::Shade(Br, 0.2f));
		I.Line(644, Sidewalk - 40, 656, Sidewalk - 62, FVArt::Shade(Br, 0.3f));
		I.Line(656, Sidewalk - 62, 658, Sidewalk - 44, FColor(200, 200, 200));
		FishGlyph(I, 652, Sidewalk - 44, Br);
	}
	Box(I, 614, Sidewalk - 8, 12, 4, Wood); Box(I, 654, Sidewalk - 8, 12, 4, Wood);
	// boutique: columns, big windows with mannequins, golden sign, carpet and a velvet rope
	{
		const int32 WY = WallY(5);
		Sign(I, 696, WY + 4, 60, 9, FColor(50, 40, 60));
		for (int32 x = 700; x < 752; x += 4) { I.FillRect(x, WY + 7, 2, 3, Gold); }
		for (int32 k = 0; k < 4; ++k) { Box(I, 680 + k * 30, WY + 16, 6, Houses[5].WallH - 22, FColor(244, 238, 226)); }
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 GX = 688 + k * 54;
			I.FillRect(GX, WY + 28, 22, 40, PropOut); I.FillRect(GX + 1, WY + 29, 20, 38, FVArt::Mix(Glass, FColor(120, 90, 140), 0.3f));
			I.FillRect(GX + 8, WY + 36, 6, 18, k ? FColor(40, 50, 110) : FColor(236, 190, 70)); Ellipse(I, float(GX + 11), float(WY + 33), 2.5f, 2.5f, FColor(236, 214, 190));
			I.FillRect(GX + 8, WY + 38, 6, 1, Gold); I.FillRect(GX + 10, WY + 54, 2, 10, Iron);
			I.Set(GX + 2, WY + 31, FColor(220, 236, 250)); I.Set(GX + 3, WY + 30, FColor(220, 236, 250));
		}
		Door(I, 718, Sidewalk, 16, 30, FColor(90, 40, 60), 8);
		I.FillRect(718, Sidewalk, 16, 12, FColor(170, 40, 60)); I.FillRect(718, Sidewalk, 16, 1, FColor(210, 70, 80));
		for (const int32 PX : { 712, 740 }) { I.FillRect(PX, Sidewalk - 2, 2, 12, Gold); }
		I.Line(713, Sidewalk + 1, 740, Sidewalk + 1, FColor(150, 30, 60));
	}
	// marine station: tower with a telescope, aquarium windows
	{
		const int32 WY = WallY(6);
		Box(I, 830, WY - 40, 18, 40, PinkPl);
		PaintRoof(I, 828, WY - 50, 22, 10, SlateRoof, 0x99);
		Window(I, 834, WY - 32, 10, 10, PinkPl, false, 3);
		I.Line(846, WY - 44, 856, WY - 50, FColor(200, 170, 80)); I.Line(846, WY - 43, 856, WY - 49, FColor(150, 120, 60));
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 AX = 792 + k * 18;
			I.FillRect(AX, Sidewalk - 30, 14, 14, PropOut); I.FillRect(AX + 1, Sidewalk - 29, 12, 12, FColor(70, 170, 190));
			FishGlyph(I, AX + 2, Sidewalk - 25, FColor(250, 160, 60)); I.FillRect(AX + 1, Sidewalk - 20, 12, 3, FColor(210, 200, 150));
		}
		Door(I, 836, Sidewalk, 12, 22, FColor(70, 110, 150), 9);
	}
	// restaurant: veranda with umbrellas
	{
		Door(I, 870, Sidewalk, 12, 22, WoodDark, 10);
		Sign(I, 890, WallY(7) + 30, 44, 8, FColor(40, 70, 60)); for (int32 x = 894; x < 930; x += 3) { I.Set(x, WallY(7) + 33, Gold); }
		Planks(I, 886, Sidewalk - 10, 58, 10, WoodLight, false, 3, 12);
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 UX = 900 + k * 28;
			I.FillRect(UX, Sidewalk - 30, 1, 22, Iron);
			for (int32 y = 0; y < 7; ++y) { for (int32 x = -10 + y; x <= 10 - y; ++x) { I.Set(UX + x, Sidewalk - 30 - 6 + (6 - y), (((x + 20) / 4) & 1) ? FColor(240, 240, 236) : FColor(210, 70, 70)); } }
			Box(I, UX - 5, Sidewalk - 12, 11, 3, FColor(240, 236, 226));
		}
	}
	// net shed: nets on poles with floats, the chalk board of nets
	{
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 NX = 958 + k * 26;
			I.FillRect(NX, Sidewalk - 34, 2, 34, WoodDark); I.FillRect(NX + 20, Sidewalk - 34, 2, 34, WoodDark);
			for (int32 y = Sidewalk - 32; y < Sidewalk - 8; ++y) { for (int32 x = NX + 2; x < NX + 20; ++x)
			{
				if (((x + y) % 4) == 0 || ((x - y + 400) % 4) == 0) { I.Set(x, y + int32(FMath::Sin(float(x - NX) / 6.f) * 2.f), FColor(120, 150, 130)); }
			} }
			for (int32 x = NX + 3; x < NX + 20; x += 5) { Ellipse(I, float(x), float(Sidewalk - 10), 1.6f, 1.6f, (x & 1) ? FColor(230, 90, 60) : FColor(240, 220, 90)); }
		}
		Sign(I, 1018, Sidewalk - 40, 18, 22, FColor(50, 60, 56));
		for (int32 k = 0; k < 5; ++k) { I.FillRect(1020, Sidewalk - 37 + k * 4, 6 + (k * 5) % 8, 1, FColor(230, 230, 220)); }
	}
	Door(I, 1056, Sidewalk, 12, 20, FColor(70, 110, 150), 11);   // pilot booth

	// 5) cliff with the cave adit (x 1080..1164) and the lighthouse on the rocks (x 1164..1280)
	for (int32 y = 0; y < Sidewalk + 8; ++y)
	{
		const float Edge = 1078.f + 6.f * VN(float(y) / 8.f, 0.f, 0xCF);
		for (int32 x = 1078; x < W; ++x)
		{
			if (float(x) < Edge) { continue; }
			const float N = Fbm(float(x), float(y) * 1.4f, 0xC1F);
			FColor C = N < 0.35f ? FColor(96, 88, 84) : (N < 0.6f ? FColor(122, 112, 104) : FColor(150, 140, 128));
			if (((y + int32(N * 10.f)) % 9) == 0) { C = FVArt::Shade(C, 0.25f); }
			if (y > Sidewalk) { C = FVArt::Shade(C, 0.2f); }
			I.Set(x, y, C);
		}
	}
	for (int32 x = 1078; x < W; ++x) { for (int32 y = 0; y < 12; ++y) { if (FVArt::Noise(x, y, 5) < 0.6f) { I.Set(x, y, y < 6 ? GrassDark : Grass); } } }
	for (int32 y = 80; y < WalkTop + 2; ++y) { for (int32 x = 1090; x < 1130; ++x)   // the adit + its gravel path
	{
		if (y < 96) { I.Set(x, y, FVArt::Mix(FColor(16, 14, 20), FColor(40, 34, 40), float(y - 80) / 16.f)); }
		else { I.Set(x, y, FVArt::Noise(x / 3, y / 3, 0x6) < 0.5f ? FColor(120, 104, 90) : FColor(138, 120, 100)); }
	} }
	I.FillRect(1086, 70, 4, 30, WoodDark); I.FillRect(1130, 70, 4, 30, WoodDark); I.FillRect(1084, 68, 52, 4, Wood); I.FillRect(1084, 68, 52, 1, WoodLight);
	I.FillRect(1087, 70, 1, 30, Wood); I.FillRect(1131, 70, 1, 30, Wood);
	Sign(I, 1138, 84, 22, 9, WoodLight); for (int32 x = 1141; x < 1157; x += 2) { I.Set(x, 88, PropOut); }   // «Пещера» board
	I.FillRect(1146, 93, 1, 10, WoodDark);
	Lamp(I, 1082, 106);
	{
		const int32 LX = 1210, Base = 128;   // the lighthouse (MakeLighthouse-like, red bands)
		ShadowEllipse(I, float(LX), float(Base), 18.f, 4.f);
		for (int32 y = 24; y < Base; ++y)
		{
			const float T = float(y - 24) / float(Base - 24);
			const int32 HW = int32(8.f + T * 7.f);
			for (int32 x = LX - HW; x <= LX + HW; ++x)
			{
				const bool Band = ((y - 24) / 14) % 2 == 1;
				FColor C = Band ? FColor(200, 64, 54) : FColor(244, 240, 232);
				const float L = float(x - LX) / float(HW);
				if (L < -0.5f) { C = FVArt::Light(C, 0.15f); } else if (L > 0.4f) { C = FVArt::Shade(C, 0.25f); }
				I.Set(x, y, C);
			}
			I.Set(LX - HW - 1, y, PropOut); I.Set(LX + HW + 1, y, PropOut);
		}
		Box(I, LX - 11, 20, 22, 5, Iron);
		I.FillRect(LX - 7, 8, 14, 12, FVArt::OutlineOf(Iron)); I.FillRect(LX - 6, 9, 12, 10, FColor(255, 232, 150)); I.FillRect(LX - 3, 10, 5, 7, FColor(255, 250, 220));
		for (int32 k = 0; k < 7; ++k) { I.Set(LX - 9 + k, 7 - k / 3, FColor(180, 60, 50)); I.Set(LX + 9 - k, 7 - k / 3, FColor(180, 60, 50)); }
		I.FillRect(LX - 2, 3, 4, 3, FColor(180, 60, 50)); I.Set(LX, 1, Iron); I.Set(LX, 2, Iron);
		Door(I, LX - 5, Base, 10, 16, WoodDark, 13);
		Window(I, LX - 3, 60, 6, 8, FColor(244, 240, 232), false, 0);
		Window(I, LX - 3, 92, 6, 8, FColor(244, 240, 232), false, 1);
	}

	// 6) back-line props on the sidewalk: lamps, barrels, crates, flower pots
	static const int32 LampX[] = { 200, 298, 396, 472, 546, 674, 782, 860, 950, 1044 };
	for (const int32 LX : LampX) { Lamp(I, LX, Sidewalk + 10); }
	Barrel(I, 380, Sidewalk + 10); Barrel(I, 392, Sidewalk + 11); Barrel(I, 202, Sidewalk + 11);
	Crate(I, 130, Sidewalk + 11, 10); Crate(I, 184, Sidewalk + 11, 9); Crate(I, 940, Sidewalk + 11, 9); Crate(I, 1070, Sidewalk + 11, 8);
	// 6b) Р66: props where the task givers used to stand — fish crates (Зоя), the tavern barrel and menu board (Марфа),
	// crates of sample jars (Лидия), the restaurant menu (Анри), the pilot's barrel (Ефим), barrel and crate by the lighthouse
	Crate(I, 250, Sidewalk + 11, 10); FishGlyph(I, 245, Sidewalk - 1, FColor(150, 176, 196));
	Crate(I, 262, Sidewalk + 12, 8); FishGlyph(I, 258, Sidewalk + 2, FColor(214, 150, 110));
	Barrel(I, 322, Sidewalk + 11); MenuBoard(I, 338, Sidewalk + 12);
	Crate(I, 804, Sidewalk + 11, 8);
	I.FillRect(801, Sidewalk + 1, 2, 2, FColor(150, 210, 220)); I.FillRect(805, Sidewalk + 1, 2, 2, FColor(170, 220, 190));
	MenuBoard(I, 914, Sidewalk + 12);
	Barrel(I, 1058, Sidewalk + 11);
	Barrel(I, 1194, Sidewalk + 12); Crate(I, 1218, Sidewalk + 12, 9);
	for (const int32 FX : { 234, 512, 818, 900 })
	{
		Box(I, FX - 4, Sidewalk + 3, 9, 7, FColor(180, 100, 70)); Ellipse(I, float(FX) + 0.5f, float(Sidewalk + 1), 6.f, 4.f, Grass);
		I.Set(FX - 2, Sidewalk - 1, FColor(236, 80, 90)); I.Set(FX + 2, Sidewalk, FColor(250, 210, 80)); I.Set(FX, Sidewalk - 2, FColor(240, 150, 200));
	}

	// 7) embankment details: drain grates, puddles, fish scales
	{
		FRandomStream RD(0xD0D);
		for (int32 k = 0; k < 7; ++k)
		{
			const int32 GX = 160 + k * 150 + RD.RandRange(-10, 10), GY = 196 + RD.RandRange(-20, 30);
			I.FillRect(GX, GY, 10, 6, FColor(60, 60, 66)); for (int32 x = GX + 1; x < GX + 10; x += 2) { I.FillRect(x, GY + 1, 1, 4, FColor(30, 30, 36)); }
		}
		for (int32 k = 0; k < 6; ++k)
		{
			const float PX = float(140 + k * 190 + RD.RandRange(0, 40)), PY = float(170 + RD.RandRange(0, 60));
			for (int32 y = int32(PY) - 3; y <= int32(PY) + 3; ++y) { for (int32 x = int32(PX) - 9; x <= int32(PX) + 9; ++x)
			{
				const float DX = (float(x) - PX) / 9.f, DY = (float(y) - PY) / 3.f;
				if (DX * DX + DY * DY < 1.f) { I.Set(x, y, (DY < -0.4f && DX < 0.f) ? FColor(190, 214, 230) : FVArt::Mix(I.Get(x, y), FColor(110, 150, 180), 0.55f)); }
			} }
		}
		for (int32 k = 0; k < 60; ++k) { I.Set(RD.RandRange(120, 1180), RD.RandRange(154, 248), RD.FRand() < 0.5f ? FColor(210, 220, 226) : FColor(236, 236, 236)); }
	}
	// quay: bollards, steps at the berth, a ladder
	for (const int32 BX : { 140, 240, 340, 470, 560, 650, 760, 860, 1060, 1140 }) { Bollard(I, BX, QuayTop + 4); }
	for (int32 s = 0; s < 3; ++s) { I.FillRect(130 + s * 2, WallTop + s * 3, 14 - s * 2, 3, FVArt::Shade(StoneLight, 0.1f * float(s))); }
	for (int32 y = WallTop; y < WaterTop + 8; y += 3) { I.FillRect(700, y, 6, 1, Iron); }
	I.FillRect(700, WallTop, 1, 14, Iron); I.FillRect(705, WallTop, 1, 14, Iron);

	// 8) boat berth jetty (x 150..176) and the pier (x 394..426)
	auto Jetty = [&I](int32 X, int32 JW, int32 Y0, int32 Y1, uint32 Seed)
	{
		for (int32 y = Y0; y < Y1 + 6; ++y) { for (int32 x = X + JW; x < X + JW + 4; ++x) { if (y >= WaterTop) { Mul(I, x, y, 0.75f); } } }   // shadow on the water
		for (int32 PY = Y0 + 6; PY <= Y1; PY += 14)
		{
			for (const int32 PX : { X - 1, X + JW - 2 })
			{
				I.FillRect(PX, PY, 3, 8, WoodDark); I.FillRect(PX, PY, 1, 8, Wood);
				for (int32 x = PX - 1; x < PX + 4; ++x) { if ((x & 1) == 0) { I.Set(x, PY + 8, Foam); } }
			}
		}
		Planks(I, X, Y0, JW, Y1 - Y0, WoodLight, false, 3, Seed);
		I.FillRect(X - 1, Y0, 1, Y1 - Y0, PropOut); I.FillRect(X + JW, Y0, 1, Y1 - Y0, PropOut);
		I.FillRect(X, Y1, JW, 2, WoodDark); I.FillRect(X - 1, Y1 + 2, JW + 2, 1, PropOut);
	};
	Jetty(150, 26, QuayTop, 282, 0x1E7);
	Jetty(394, 32, QuayTop, 314, 0x9E7);
	Bollard(I, 400, 312); Bollard(I, 420, 312);
	for (int32 k = 0; k < 18; ++k)   // old dock: piles with round tops and weed (x 940..1040)
	{
		const int32 PX = 946 + (k % 6) * 17 + ((k / 6) & 1) * 6, PY = 276 + (k / 6) * 12;
		for (int32 x = PX - 5; x < PX + 6; ++x) { Mul(I, x + 2, PY + 3, 0.75f); }
		I.FillRect(PX - 3, PY - 2, 7, 6, WoodDark);
		Ellipse(I, float(PX) + 0.5f, float(PY) - 2.f, 3.5f, 2.f, FColor(170, 128, 86));
		I.Set(PX, PY - 2, WoodDark); I.Set(PX - 1, PY - 3, FColor(196, 156, 110));
		I.FillRect(PX - 3, PY + 2, 7, 2, FColor(60, 110, 70));
		for (int32 x = PX - 4; x < PX + 5; x += 2) { I.Set(x, PY + 4, Foam); }
	}
	Planks(I, 960, 286, 40, 4, FVArt::Shade(Wood, 0.2f), true, 5, 0x0D);
	{
		FRandomStream RW(0xC0E);   // the cove at the right: seaweed and rocks
		for (int32 k = 0; k < 26; ++k)
		{
			const int32 SX = RW.RandRange(1150, 1270), SY = RW.RandRange(WaterTop + 6, WaterTop + 40);
			for (int32 j = 0; j < 5; ++j) { I.Set(SX + ((j & 1) ? 1 : 0), SY - j, j < 2 ? FColor(40, 96, 70) : FColor(70, 140, 90)); }
		}
		for (int32 k = 0; k < 6; ++k) { RockBlob(I, float(RW.RandRange(1160, 1270)), float(RW.RandRange(WaterTop + 10, 330)), float(RW.RandRange(4, 8)), 3.5f, StoneDark, 200 + k); }
		for (int32 k = 0; k < 20; ++k) { const int32 FX = RW.RandRange(0, W - 1), FY = RW.RandRange(WaterTop + 20, H - 8); for (int32 j = 0; j < 4; ++j) { I.Set(FX + j, FY, Foam); } }
	}

	// 9) dark rim of the world (last 6 texels: 0.85 -> 0.55)
	for (int32 k = 0; k < 6; ++k)
	{
		const float F = 0.55f + 0.3f * float(k) / 5.f;
		for (int32 x = k; x < W - k; ++x) { Mul(I, x, k, F); Mul(I, x, H - 1 - k, F); }
		for (int32 y = k + 1; y < H - k - 1; ++y) { Mul(I, k, y, F); Mul(I, W - 1 - k, y, F); }
	}

	// ---- the def ----
	FWorldDef& D = OutDef;
	D.LocationId = FVDBU6::CityId();
	D.SizeTex = FIntPoint(W, H);
	D.Walk.Add(Rc(116, WalkTop, 1140, QuayTop + 4 - WalkTop));    // embankment (x 116..1256)
	D.Walk.Add(Rc(398, QuayTop, 24, 62));                         // the pier
	D.Walk.Add(Rc(152, QuayTop, 22, 30));                         // the boat berth jetty
	D.Walk.Add(Rc(1094, 92, 32, WalkTop - 92 + 2));               // the path to the adit (through the rubble)
	D.bWater = true;
	D.ShoreY = float(WallTop + 4) * FV::WorldScale;
	D.WaterArea = Rc(0, WallTop + 4, W, H - WallTop - 4);
	D.Dock = Rc(394, QuayTop, 32, 64);
	D.bNearPilesArea = true;
	D.PilesArea = Rc(936, WallTop, 110, 60);
	D.bGulls = true;

	AddAnchor(D, EAnchor::Spawn, FName(TEXT("default")), 186, 240);     // arrival by boat at the berth
	AddAnchor(D, EAnchor::Spawn, FName(TEXT("pier")), 186, 240);
	AddAnchor(D, EAnchor::Spawn, FName(TEXT("from_map")), 186, 240);
	AddAnchor(D, EAnchor::Spawn, FName(TEXT("from_cave")), 1110, 150);
	{
		FAnchor A; A.Kind = EAnchor::Exit; A.Id = FName(TEXT("from_city")); A.Target = FVDBU6::CaveId();
		A.Feet = P(1110, 100); A.Hit = Rc(1094, 90, 32, 10); A.Label = TEXT("В пещеру ↑");
		A.SignFeet = P(1070, 150); A.SignDir = uint8(FVWorldU6::EWayDir::Up);   // Р69: the way sign — the cave mouth
		D.Anchors.Add(A);
	}
	AddAnchor(D, EAnchor::Boat, FName(TEXT("boat")), 196, 300);
	// Р66: on the streets only the villagers who offer something; the task givers (Бурун, Анри, Фома, Лидия, Зоя, Ефим,
	// Марфа) stay in the data for the task board only — their spots got street props (6b)
	AddAnchor(D, EAnchor::Npc, FName(TEXT("npc_kuzma")), 432, 162);
	AddAnchor(D, EAnchor::Npc, FName(TEXT("npc_agafya")), 516, 162);
	AddAnchor(D, EAnchor::Npc, FName(TEXT("npc_zhanna")), 752, 166);
	AddAnchor(D, EAnchor::Npc, FName(TEXT("npc_nikifor")), 996, 164);
	{
		FAnchor A; A.Kind = EAnchor::Board; A.Id = FName(TEXT("city_board")); A.Feet = P(586, 156);
		A.Hit = Rc(566, 100, 40, 58); A.Label = TEXT("Доска заданий");
		D.Anchors.Add(A);
	}
	{
		const FIntPoint J[8] = { { 150, 234 }, { 318, 230 }, { 474, 220 }, { 704, 238 }, { 792, 212 }, { 896, 236 }, { 1016, 228 }, { 1160, 234 } };
		for (int32 k = 0; k < 8; ++k) { AddAnchor(D, EAnchor::JunkPile, FName(*FString::Printf(TEXT("junk_%02d"), k)), float(J[k].X), float(J[k].Y), k, uint8(k & 3)); }
	}
	for (int32 k = 0; k < 3; ++k)
	{
		FAnchor A; A.Kind = EAnchor::Rubble; A.Id = FName(*FString::Printf(TEXT("rubble_%d"), k)); A.Index = k; A.Variant = uint8(k);
		A.Feet = P(1110, float(146 - k * 16));
		A.Hit = Rc(1088, float(146 - k * 16 - 26), 44, 28);
		D.Anchors.Add(A);
	}
	for (const int32 LX : LampX) { AddLight(D, float(LX), float(Sidewalk - 26), 34.f, FColor(255, 214, 140), false); }
	AddLight(D, 1082.f, 70.f, 26.f, FColor(255, 196, 110), true);
	AddLight(D, 1210.f, 14.f, 90.f, FColor(255, 236, 170), false);
	for (const FIntPoint& Wn : { FIntPoint(346, 72), FIntPoint(250, 96), FIntPoint(720, 90), FIntPoint(900, 96), FIntPoint(160, 80), FIntPoint(820, 80) })
	{
		AddLight(D, float(Wn.X), float(Wn.Y), 20.f, FColor(255, 200, 120), false);
	}
}

const FFVSprite& UFVSprites::JunkPileSprite(int32 Variant, bool bDepleted)
{
	Variant = FMath::Clamp(Variant, 0, 3);
	const uint64 Key = MakeKey(FVSpriteCat::City, 1, uint32(Variant), bDepleted ? 1 : 0);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtCityImpl::MakeJunk(Variant, bDepleted), FString::Printf(TEXT("CityJunk_%d_%d"), Variant, bDepleted ? 1 : 0));
}
const FFVSprite& UFVSprites::RubbleSprite(int32 Index, bool bCleared)
{
	Index = FMath::Clamp(Index, 0, 2);
	const uint64 Key = MakeKey(FVSpriteCat::City, 2, uint32(Index), bCleared ? 1 : 0);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtCityImpl::MakeRubble(Index, bCleared), FString::Printf(TEXT("CityRubble_%d_%d"), Index, bCleared ? 1 : 0));
}
const FFVSprite& UFVSprites::BoatSprite(int32 Stage)
{
	Stage = FMath::Clamp(Stage, 0, 3);
	const uint64 Key = MakeKey(FVSpriteCat::City, 3, uint32(Stage));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildBoat(Stage), FString::Printf(TEXT("Boat_%d"), Stage));
}
const FFVSprite& UFVSprites::VoyageSprite(int32 Layer, int32 Frame)
{
	Layer = FMath::Clamp(Layer, 0, 3);
	Frame = Layer == 2 ? (Frame & 3) : (Layer == 3 ? (Frame & 1) : 0);
	const uint64 Key = MakeKey(FVSpriteCat::City, 4, uint32(Layer), uint8(Frame));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, FVArtCityImpl::MakeVoyage(Layer, Frame), FString::Printf(TEXT("Voyage_%d_%d"), Layer, Frame));
}
FFVImage UFVSprites::BuildBoat(int32 Stage)
{
	return FVArtCityImpl::MakeBoat(Stage);
}
