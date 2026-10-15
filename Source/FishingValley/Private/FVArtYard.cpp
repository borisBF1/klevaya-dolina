// UPDATE 6 [ART-YARD] — the yard: big world (house outside, compost, practice pond, plots), yard buildings, garden beds, outdoor smokehouse & worm farm (Docs/UPDATE6_FOREST_CITY.md §6, §6.4, П7, Р38). Owner: ART-YARD.
// Contract: Public/FVSprites.h «UPDATE 6», Private/FVWorldU6.h.
// Style: pixel art like FVSceneArt.cpp / FVArtHome.cpp (Stardew-like shading via FVArt::Shade/Light, 1px dark outline
// on props), deterministic (hash noise with constant seeds), cached through UFVSprites::AddCached(MakeKey(FVSpriteCat::Yard, ...)).
//
// ==================================================================================================================
// SIZE / PIVOT TABLE (texels; world sprites are drawn at FV::WorldScale = 2). Pivot = bottom-centre.
// ------------------------------------------------------------------------------------------------------------------
//  YardBuildingSprite(Sawmill, 0)   72x30  pegs + rope around the footprint, sign «Здесь можно построить…»
//                   (Sawmill, 1)   72x52  lean-to on 4 posts, table with the circular saw, log on trestles, boards
//                   (Sawmill, 2)   88x56  «Пилорама»: + band-saw frame (left) and a roller conveyor (right)
//                   (Forge, 0)     64x30  pegs + sign
//                   (Forge, 1)     64x60  fieldstone smithy, brick chimney, open front with the hearth, anvil, barrel
//                   (Forge, 2)     64x60  + quench trough with steam, hard rock with violet veins
//                   (Garden, 0)   164x30  the BACK line of the plot: two stakes, rope, the sign (the log kerb around the
//                                         plot is painted into the world; the beds are GardenBed anchors in front)
//                   (Garden, 1..3) 164x40 wattle fence along the back, gate posts, rain barrel, tool rack; L2 + bench and a
//                                         watering can; L3 + a small glass hotbed
//      Collision (suggested): Sawmill / Forge — the bottom 16 texels of the sprite; Garden — none (flat back line).
//      YardEffectPoint: Sawmill L1 (33, 30) / L2 (41, 34) = the saw disc (dust); Forge L1..2 (31, 48) = the hearth
//      (glow, sparks); others (-1, -1).
//  GardenBedSprite(Crop, Stage)     24x22: the bed (24x10 at the bottom) + plants; Crop None = tilled empty soil;
//      Stage 0 planted (seed mounds), 1 sprouts, 2 growing, 3 ripe (reed, carrot, potato, onion, dill, pumpkin).
//  YardStationSprite(Smokehouse | WormFarm, L)  StationSprite(S, L) + an earth mound and grass at its base; same canvas
//      height + 2 (pivot and StationEffectPoint unchanged). Other stations -> StationSprite(S, L).
//  World (FVWorldU6::BuildYard): 800x320 texels. Centre: the cottage (78x62, feet (400, 118)), smokehouse by the fence,
//      compost heap with 3 dig spots + worm farm, practice pond with a plank; left: the garden plot (kerb x 54..218,
//      y 132..244, 12 beds); right: the sawmill plot (feet (606, 166)) and the forge plot (feet (712, 268)); the gate
//      to the map at the bottom centre. Walk band y 100..284.
// ==================================================================================================================

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVWorldU6.h"
#include "FVArtYardFront.h"

// The shared update-6 art kit (defined in FVArtForest.cpp).
namespace FVArtKitU6
{
	float R01(int32 X, int32 Y, uint32 S);
	float VNoise(float X, float Y, uint32 S);
	float Fbm(float X, float Y, uint32 S);
	float Bayer(int32 X, int32 Y);
	FColor Rgb(int32 R, int32 G, int32 B);
	FColor WithA(FColor C, int32 A);
	FColor Mul(FColor C, float F);
	void MulPx(FFVImage& I, int32 X, int32 Y, float F);
	void MixPx(FFVImage& I, int32 X, int32 Y, FColor C, float T);
	FColor LeafTone(FColor Base, int32 Tone);
	FColor CrownOutline(FColor Leaf);
	void ShadeEllipse(FFVImage& I, float CX, float CY, float RX, float RY, float F);
	void FillEllipse(FFVImage& I, float CX, float CY, float RX, float RY, FColor C, bool bBlend);
	void PaintBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Hi, FColor Base, FColor Lo, FColor Deep, uint32 Seed, float Jag, float Noise);
	void LeafBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Leaf, uint32 Seed);
	void ThickLine(FFVImage& I, FVector2D A, FVector2D B, float W0, float W1, FColor C);
	void FillGround(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, FColor Lo, FColor Base, FColor Hi, float Scale, uint32 Seed);
	void Tufts(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Count, FColor Base, uint32 Seed);
	void Flowers(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Count, uint32 Seed);
	void PaintPath(FFVImage& I, const TArray<FVector2D>& Way, float Width, uint32 Seed);
	void DarkRim(FFVImage& I, int32 Band);
	void Stamp(FFVImage& Dst, const FFVImage& Src, int32 FeetX, int32 FeetY, bool bFlip, float ShadowRX);
	FFVImage MakeBush(uint32 Seed, FColor Leaf, int32 Size, bool bBerries);
	FFVImage MakeRock(uint32 Seed, int32 RX, int32 RY, FColor Base, bool bMoss);
}

namespace FVArtYardImpl
{
	using namespace FVArtKitU6;

	// ---------------------------------------------------------------------------------------- palette (UPDATE6_ART §3)
	const FColor GrassLo(86, 154, 66), GrassBase(110, 180, 76), GrassHi(140, 204, 92);
	const FColor PropOut(58, 36, 30);
	const FColor WoodHi(214, 166, 108), Wood(182, 132, 80), WoodMid(150, 104, 62), WoodLo(116, 78, 48), WoodDk(84, 54, 34);
	const FColor StoneHi(204, 200, 190), Stone(164, 160, 152), StoneLo(124, 120, 116), StoneDk(86, 82, 88);
	const FColor BrickHi(214, 122, 86), Brick(178, 88, 62), BrickLo(130, 60, 48), Mortar(206, 190, 164);
	const FColor IronHi(150, 150, 166), Iron(98, 98, 114), IronLo(66, 64, 80), IronDk(44, 42, 56);
	const FColor MetHi(242, 245, 252), Met(194, 200, 214), MetLo(130, 134, 154);
	const FColor FireY(255, 242, 150), FireO(255, 170, 50), FireR(228, 86, 32), Ember(150, 44, 30), FireBack(44, 26, 26);
	const FColor SoilHi(150, 108, 70), Soil(112, 76, 50), SoilLo(84, 56, 40), SoilDk(58, 38, 30);
	const FColor Paper(250, 238, 204), PaperLo(222, 200, 156);
	const FColor LeafHi(150, 214, 92), Leaf(84, 168, 64), LeafLo(44, 110, 60), LeafDk(30, 78, 50);
	const FColor Sawdust(236, 208, 150), CutWood(226, 190, 130), CutRing(196, 150, 96);
	const FColor PondSh(96, 176, 190), PondDp(36, 104, 132);
	const FColor RockC(92, 86, 108), RockHiC(130, 124, 148), Vein(170, 90, 230), VeinHi(214, 150, 255);
	const FColor Wattle(170, 124, 70), WattleLo(134, 94, 52);

	constexpr int32 WorldW = 800, WorldH = 320;
	constexpr int32 WalkTop = 100, WalkBottom = 284;
	// the garden plot: the log kerb, the back line (feet of the garden building), the bed grid
	constexpr int32 PlotX0 = 54, PlotX1 = 218, PlotY0 = 132, PlotY1 = 244;
	const int32 BedCols[4] = { 82, 118, 154, 190 };
	const int32 BedRows[3] = { 166, 198, 230 };

	// ---------------------------------------------------------------------------------------- primitives
	void HLine(FFVImage& I, int32 X0, int32 X1, int32 Y, FColor C) { for (int32 X = X0; X <= X1; ++X) { I.Set(X, Y, C); } }
	void VLine(FFVImage& I, int32 X, int32 Y0, int32 Y1, FColor C) { for (int32 Y = Y0; Y <= Y1; ++Y) { I.Set(X, Y, C); } }
	/** Filled box with a bevel: lit top row / left column, shaded bottom row / right column. */
	void Box(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, float Lt, float Dk)
	{
		if (W <= 0 || H <= 0) { return; }
		I.FillRect(X, Y, W, H, Base);
		HLine(I, X, X + W - 1, Y, FVArt::Light(Base, Lt));
		VLine(I, X, Y, Y + H - 1, FVArt::Light(Base, Lt * 0.5f));
		HLine(I, X, X + W - 1, Y + H - 1, FVArt::Shade(Base, Dk));
		VLine(I, X + W - 1, Y + 1, Y + H - 1, FVArt::Shade(Base, Dk * 0.8f));
	}
	/** Planks: horizontal (bVertical false) or vertical boards PH texels wide, lit edge, dark seam, grain. */
	void Planks(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, int32 PH, uint32 Seed, bool bVertical)
	{
		PH = FMath::Max(2, PH);
		const FColor Hi = FVArt::Light(Base, 0.18f), Seam = FVArt::Shade(Base, 0.42f), Grain = FVArt::Shade(Base, 0.14f);
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const int32 Along = bVertical ? (YY - Y) : (XX - X), Across = bVertical ? (XX - X) : (YY - Y);
				const int32 Band = Across / PH, P = Across % PH;
				FColor C = P == PH - 1 ? Seam : (P == 0 ? Hi : Base);
				if (P != 0 && P != PH - 1 && R01(Along / 3, Band, Seed) < 0.12f) { C = Grain; }
				I.Set(XX, YY, C);
			}
		}
	}
	/** Rows of rounded fieldstones with mortar. */
	void Stones(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, FColor Mort, uint32 Seed)
	{
		for (int32 Row = 0; Row * 5 < H; ++Row)
		{
			const int32 Y0 = Y + Row * 5;
			int32 SX = X - (Row % 2) * 3;
			while (SX < X + W)
			{
				const int32 SW = 5 + FMath::FloorToInt(R01(SX, Row, Seed) * 4.f);
				const FColor C = FVArt::Mix(Base, R01(SX, Row, Seed + 1) < 0.5f ? FVArt::Light(Base, 0.3f) : FVArt::Shade(Base, 0.25f), 0.35f);
				for (int32 YY = Y0; YY < Y0 + 5 && YY < Y + H; ++YY)
				{
					for (int32 XX = FMath::Max(SX, X); XX < SX + SW && XX < X + W; ++XX)
					{
						const bool bMortar = YY == Y0 + 4 || XX == SX + SW - 1;
						const bool bCorner = (YY == Y0 || YY == Y0 + 3) && (XX == SX || XX == SX + SW - 2);
						FColor P = bMortar ? Mort : C;
						if (!bMortar && YY == Y0) { P = FVArt::Light(C, 0.25f); }
						else if (!bMortar && YY == Y0 + 3) { P = FVArt::Shade(C, 0.2f); }
						if (bCorner) { P = Mort; }
						I.Set(XX, YY, P);
					}
				}
				SX += SW;
			}
		}
	}
	void Bricks(FFVImage& I, int32 X, int32 Y, int32 W, int32 H)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			const int32 Row = (YY - Y) / 3, P = (YY - Y) % 3;
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const bool bJoint = ((XX - X) + (Row % 2) * 3) % 6 == 5;
				FColor C = (P == 2 || bJoint) ? Mortar : (P == 0 ? BrickHi : Brick);
				if (XX >= X + W - 2 && C != Mortar) { C = BrickLo; }
				I.Set(XX, YY, C);
			}
		}
	}
	/** Teardrop flame: red base, orange body, yellow core. */
	void Flame(FFVImage& I, float CX, int32 BaseY, int32 H, float HalfW, uint32 Seed)
	{
		for (int32 Y = 0; Y < H; ++Y)
		{
			const float T = float(Y) / float(FMath::Max(1, H));
			const float HW = HalfW * FMath::Pow(1.f - T, 0.7f) + (R01(Y, 1, Seed) - 0.5f) * 0.6f;
			for (int32 X = FMath::FloorToInt(CX - HW); X <= FMath::CeilToInt(CX + HW) - 1; ++X)
			{
				const float D = FMath::Abs(float(X) + 0.5f - CX) / FMath::Max(0.5f, HW);
				I.Set(X, BaseY - Y, D < 0.4f && T < 0.6f ? FireY : (D < 0.75f ? FireO : FireR));
			}
		}
	}
	/** A wooden peg (stake) Hgt texels tall with a lit left side and a pointed shadow. */
	void Peg(FFVImage& I, int32 X, int32 Y, int32 Hgt)
	{
		VLine(I, X, Y, Y + Hgt - 1, WoodHi); VLine(I, X + 1, Y, Y + Hgt - 1, WoodLo);
		I.Set(X, Y - 1, CutWood);
	}
	/** Rope sagging between two points. */
	void Rope(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1)
	{
		const int32 N = FMath::Max(1, FMath::Abs(X1 - X0));
		for (int32 k = 0; k <= N; ++k)
		{
			const float T = float(k) / float(N);
			const float Sag = FMath::Sin(T * UE_PI) * 1.5f;
			I.Set(X0 + (X1 - X0) * k / N, FMath::RoundToInt(float(Y0) + float(Y1 - Y0) * T + Sag), Rgb(222, 196, 140));
		}
	}
	/** The sign «Здесь можно построить… — у Прораба в лесу»: a board on a post with a hammer, a house and a pine. */
	void BuildSign(FFVImage& I, int32 CX, int32 BaseY)
	{
		const int32 X0 = CX - 8, Y0 = BaseY - 20;
		VLine(I, CX - 1, Y0 + 10, BaseY, Wood); VLine(I, CX, Y0 + 10, BaseY, WoodLo);
		Box(I, X0, Y0, 17, 11, Paper, 0.2f, 0.3f);
		for (int32 X = X0; X < X0 + 17; ++X) { if ((X / 2) % 2 == 0) { I.Set(X, Y0, Rgb(255, 204, 51)); } }
		// a little house (the building to be) + a hammer + a pine (the builder lives in the forest)
		for (int32 k = 0; k < 3; ++k) { HLine(I, X0 + 3 - k, X0 + 3 + k, Y0 + 3 + k, Rgb(178, 70, 52)); }
		I.FillRect(X0 + 1, Y0 + 6, 5, 3, Rgb(170, 116, 68)); I.Set(X0 + 3, Y0 + 7, Rgb(66, 92, 132));
		for (int32 k = 0; k < 4; ++k) { I.Set(X0 + 8 + k, Y0 + 8 - k, WoodMid); }
		I.Set(X0 + 11, Y0 + 3, Iron); I.Set(X0 + 12, Y0 + 4, Iron); I.Set(X0 + 10, Y0 + 4, IronHi);
		for (int32 Y = Y0 + 3; Y <= Y0 + 8; ++Y) { const int32 HW = (Y - Y0 - 3) / 2; HLine(I, X0 + 14 - HW, X0 + 14 + HW, Y, Rgb(54, 112, 72)); }
	}

	// ---------------------------------------------------------------------------------------- the cottage (as MakeCottage in FVSceneArt.cpp)
	// The decor overlays of ART-DECOR (roof / walls / shutters, 78x62) are drawn over it at the same feet, so the
	// geometry (door, windows, attic window, chimney) must stay exactly as in MakeCottage.
	constexpr int32 CottageDoorX = 34, CottageDoorY = 36, CottageDoorW = 10, CottageDoorH = 21;
	FFVImage MakeCottage()
	{
		const int32 W = 78, H = 62;
		FFVImage T(W, H);
		const FColor RoofHi = Rgb(214, 100, 68), Roof = Rgb(178, 70, 52), RoofLo = Rgb(134, 50, 42), RoofDeep = Rgb(100, 38, 36);
		const FColor LogHi = Rgb(204, 150, 94), Log = Rgb(170, 116, 68), LogLo = Rgb(128, 84, 48), LogSeam = Rgb(98, 62, 36), LogEnd = Rgb(222, 180, 122);
		const FColor CStone = Rgb(150, 146, 140), CStoneLo = Rgb(112, 106, 104), CStoneHi = Rgb(188, 184, 174);
		const FColor Trim = Rgb(96, 60, 36);
		const FColor Glass = Rgb(66, 92, 132), GlassHi = Rgb(156, 196, 226);
		for (int32 Y = 29; Y <= 55; ++Y)
		{
			const int32 R3 = (Y - 29) % 3;
			for (int32 X = 5; X <= 72; ++X)
			{
				FColor Cc = R3 == 0 ? LogHi : (R3 == 1 ? Log : LogSeam);
				if (R3 == 1 && R01(X / 3, Y, 41) < 0.12f) { Cc = LogLo; }
				if (X >= 69) { Cc = Mul(Cc, 0.82f); }
				T.Set(X, Y, Cc);
			}
			if (R3 != 2)
			{
				T.Set(3, Y, R3 == 0 ? LogEnd : Mul(LogEnd, 0.85f)); T.Set(4, Y, LogEnd);
				T.Set(73, Y, Mul(LogEnd, 0.78f)); T.Set(74, Y, Mul(LogEnd, 0.66f));
			}
		}
		for (int32 X = 5; X <= 72; ++X) { T.Set(X, 29, Trim); T.Set(X, 30, LogSeam); }
		for (int32 Y = 56; Y <= 61; ++Y)
		{
			for (int32 X = 3; X <= 74; ++X)
			{
				FColor Cc = ((X + (Y & 1) * 3) % 7 == 0) ? CStoneLo : CStone;
				if (Y == 56) { Cc = CStoneHi; }
				if (Y == 61) { Cc = CStoneLo; }
				T.Set(X, Y, Cc);
			}
		}
		for (int32 Y = CottageDoorY; Y < CottageDoorY + CottageDoorH; ++Y)
		{
			for (int32 X = CottageDoorX; X < CottageDoorX + CottageDoorW; ++X)
			{
				FColor Cc = Rgb(122, 76, 46);
				if (X == CottageDoorX || X == CottageDoorX + CottageDoorW - 1 || Y == CottageDoorY) { Cc = Trim; }
				else if ((X - CottageDoorX) % 3 == 0) { Cc = Rgb(98, 60, 36); }
				else if (X == CottageDoorX + 1) { Cc = Rgb(146, 94, 58); }
				T.Set(X, Y, Cc);
			}
		}
		for (int32 Y = CottageDoorY + 3; Y <= CottageDoorY + 6; ++Y)
		{
			for (int32 X = CottageDoorX + 3; X <= CottageDoorX + 6; ++X)
			{
				const bool bCorner = (X == CottageDoorX + 3 || X == CottageDoorX + 6) && (Y == CottageDoorY + 3 || Y == CottageDoorY + 6);
				T.Set(X, Y, bCorner ? Trim : ((X == CottageDoorX + 4 && Y == CottageDoorY + 4) ? GlassHi : Glass));
			}
		}
		T.Set(CottageDoorX + 7, CottageDoorY + 11, Rgb(236, 196, 92)); T.Set(CottageDoorX + 7, CottageDoorY + 12, Rgb(180, 140, 60));
		for (int32 X = CottageDoorX - 2; X < CottageDoorX + CottageDoorW + 2; ++X) { T.Set(X, 57, CStoneHi); T.Set(X, 58, CStone); }
		for (int32 Y = 31; Y <= 34; ++Y)
		{
			const int32 X0 = CottageDoorX - 4 - (Y - 31), X1 = CottageDoorX + CottageDoorW + 3 + (Y - 31);
			for (int32 X = X0; X <= X1; ++X) { T.Set(X, Y, Y == 31 ? RoofHi : (Y == 34 ? RoofDeep : ((X % 4) == 0 ? RoofLo : Roof))); }
		}
		auto Window = [&T, Trim, Glass, GlassHi](int32 X0, int32 Y0)
		{
			for (int32 Y = Y0; Y <= Y0 + 10; ++Y)
			{
				for (int32 X = X0; X <= X0 + 11; ++X)
				{
					FColor Cc = Glass;
					if (X == X0 || X == X0 + 11 || Y == Y0 || Y == Y0 + 10) { Cc = Trim; }
					else if (X == X0 + 6 || Y == Y0 + 5) { Cc = Rgb(210, 170, 120); }
					else if ((X == X0 + 1 && Y <= Y0 + 3) || (X == X0 + 2 && Y == Y0 + 1)) { Cc = GlassHi; }
					T.Set(X, Y, Cc);
				}
				T.Set(X0 - 3, Y, Rgb(84, 140, 90)); T.Set(X0 - 2, Y, (Y % 2) ? Rgb(64, 116, 74) : Rgb(84, 140, 90));
				T.Set(X0 + 13, Y, Rgb(64, 116, 74)); T.Set(X0 + 14, Y, (Y % 2) ? Rgb(50, 96, 62) : Rgb(64, 116, 74));
			}
			const FColor Petals[3] = { Rgb(240, 90, 110), Rgb(255, 210, 90), Rgb(250, 150, 200) };
			for (int32 X = X0 - 1; X <= X0 + 12; ++X)
			{
				T.Set(X, Y0 + 11, (X % 2) ? Petals[(X / 2) % 3] : Rgb(76, 140, 60));
				T.Set(X, Y0 + 12, Rgb(136, 88, 52)); T.Set(X, Y0 + 13, Rgb(104, 64, 40));
			}
		};
		Window(12, 38); Window(54, 38);
		for (int32 Y = 4; Y <= 28; ++Y)
		{
			const float Tt = float(Y - 4) / 24.f;
			const int32 X0 = FMath::RoundToInt(14.f - 13.f * Tt), X1 = FMath::RoundToInt(63.f + 13.f * Tt);
			const int32 Row = (Y - 4) / 3, In = (Y - 4) % 3;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor Cc = In == 0 ? RoofHi : (In == 2 ? RoofLo : Roof);
				if (In != 0 && ((X + (Row % 2) * 3) % 6) == 0) { Cc = RoofLo; }
				if (X <= X0 + 1) { Cc = FVArt::Mix(Cc, RoofHi, 0.5f); }
				else if (X >= X1 - 1) { Cc = RoofDeep; }
				T.Set(X, Y, Cc);
			}
		}
		for (int32 X = 14; X <= 63; ++X) { T.Set(X, 3, RoofDeep); }
		for (int32 X = 1; X <= 76; ++X) { T.Set(X, 28, RoofDeep); }
		FillEllipse(T, 39.f, 15.f, 3.4f, 3.4f, Trim, false);
		FillEllipse(T, 39.f, 15.f, 2.4f, 2.4f, Glass, false);
		T.Set(38, 14, GlassHi);
		for (int32 Y = 0; Y <= 12; ++Y)
		{
			for (int32 X = 56; X <= 62; ++X)
			{
				FColor Cc = X == 56 ? CStoneHi : (X == 62 ? CStoneLo : CStone);
				if (Y <= 1) { Cc = Rgb(70, 64, 64); }
				else if ((Y + (X & 1)) % 3 == 0) { Cc = CStoneLo; }
				T.Set(X, Y, Cc);
			}
		}
		T.Set(47, 36, Rgb(60, 50, 50));
		T.Set(46, 37, Rgb(60, 50, 50)); T.Set(47, 37, Rgb(255, 236, 170)); T.Set(48, 37, Rgb(60, 50, 50));
		T.Set(46, 38, Rgb(60, 50, 50)); T.Set(47, 38, Rgb(255, 196, 90)); T.Set(48, 38, Rgb(60, 50, 50));
		T.Set(47, 39, Rgb(60, 50, 50));
		T.Outline(Rgb(60, 36, 28));
		return T;
	}

	// ---------------------------------------------------------------------------------------- building plots (level 0)
	/** Empty plot: pegs + rope around a W x 16 footprint (flat, 3/4 view), the sign at the back. Canvas W x 40. */
	FFVImage BuildPlot(int32 W)
	{
		FFVImage I(W, 40);
		const int32 Back = 22, Front = 38;
		// trodden earth + chalk marks inside the footprint
		for (int32 Y = Back + 1; Y < Front; ++Y)
		{
			for (int32 X = 3; X < W - 3; ++X)
			{
				if (R01(X, Y, 301u) < 0.07f) { I.Set(X, Y, WithA(SoilHi, 120)); }
				else if (R01(X, Y, 302u) < 0.03f) { I.Set(X, Y, WithA(Rgb(250, 250, 240), 200)); }
			}
		}
		Rope(I, 2, Back, W - 3, Back);
		Rope(I, 2, Front - 2, W - 3, Front - 2);
		VLine(I, 2, Back, Front - 2, Rgb(222, 196, 140)); VLine(I, W - 3, Back, Front - 2, Rgb(222, 196, 140));
		for (int32 X = 6; X < W - 6; X += 9) { I.Set(X, Front - 1, WithA(Rgb(250, 250, 240), 180)); }
		BuildSign(I, W / 2, Back + 1);
		I.Outline(PropOut);
		Peg(I, 1, Back - 5, 6); Peg(I, W - 3, Back - 5, 6);
		Peg(I, 1, Front - 6, 6); Peg(I, W - 3, Front - 6, 6);
		return I;
	}

	// ---------------------------------------------------------------------------------------- the sawmill
	void SawDisc(FFVImage& I, int32 CX, int32 CY, int32 R)
	{
		for (int32 Y = CY - R - 1; Y <= CY + R + 1; ++Y)
		{
			for (int32 X = CX - R - 1; X <= CX + R + 1; ++X)
			{
				const float DX = float(X - CX), DY = float(Y - CY);
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				const float A = FMath::Atan2(DY, DX);
				const bool bTooth = D <= float(R) + 1.f && D > float(R) && FMath::Frac(A / (2.f * UE_PI) * 16.f) < 0.5f;
				if (D > float(R) && !bTooth) { continue; }
				FColor C = D < 2.f ? IronLo : (D > float(R) - 1.f ? MetLo : ((DX + DY) < 0.f ? MetHi : Met));
				if (FMath::Abs(D - float(R) * 0.6f) < 0.5f) { C = MetLo; }
				I.Set(X, Y, C);
			}
		}
	}
	FFVImage BuildSawmillL1()
	{
		FFVImage I(72, 52);
		// sawdust on the floor under the lean-to
		for (int32 Y = 44; Y <= 51; ++Y) { for (int32 X = 4; X <= 67; ++X) { if (R01(X, Y, 311u) < 0.12f) { I.Set(X, Y, R01(X, Y, 312u) < 0.5f ? Sawdust : CutWood); } } }
		// back posts
		for (int32 PX : { 8, 62 }) { VLine(I, PX, 14, 40, WoodMid); VLine(I, PX + 1, 14, 40, WoodLo); }
		// stack of boards (left) and the log on trestles (right)
		for (int32 b = 0; b < 4; ++b) { Box(I, 3 + (b % 2), 44 - b * 2, 18, 2, b % 2 ? CutWood : WoodHi, 0.2f, 0.3f); }
		for (int32 TX : { 50, 64 }) { I.Line(TX - 2, 50, TX, 44, WoodLo); I.Line(TX + 2, 50, TX, 44, WoodMid); }
		for (int32 Y = 39; Y <= 43; ++Y) { HLine(I, 46, 69, Y, Y == 39 ? Rgb(150, 104, 64) : (Y == 43 ? Rgb(84, 54, 34) : Rgb(116, 78, 48))); }
		FillEllipse(I, 46.f, 41.f, 1.6f, 2.4f, CutWood, false); I.Set(46, 41, CutRing);
		// the saw table with the circular saw
		Box(I, 16, 30, 32, 5, WoodHi, 0.3f, 0.35f);
		Box(I, 16, 35, 32, 3, WoodMid, 0.2f, 0.3f);
		for (int32 LX : { 18, 45 }) { VLine(I, LX, 38, 49, WoodLo); VLine(I, LX + 1, 38, 49, WoodDk); }
		SawDisc(I, 33, 30, 6);
		HLine(I, 20, 30, 32, CutWood); HLine(I, 36, 44, 32, CutWood);   // the board being cut
		// front posts and the lean-to roof (plank roof seen from above-front)
		for (int32 PX : { 2, 68 }) { VLine(I, PX, 16, 51, WoodHi); VLine(I, PX + 1, 16, 51, WoodMid); }
		for (int32 Y = 2; Y <= 17; ++Y)
		{
			const int32 X0 = 1 + (17 - Y) / 5, X1 = 70 - (17 - Y) / 5;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor C = ((X - X0) % 5 == 0) ? WoodLo : WoodMid;
				if (Y == 2) { C = WoodHi; } else if (Y >= 16) { C = WoodDk; } else if (X <= X0 + 1) { C = FVArt::Light(C, 0.2f); }
				I.Set(X, Y, C);
			}
		}
		I.Outline(PropOut);
		return I;
	}
	FFVImage BuildSawmillL2()
	{
		FFVImage I(88, 56);
		// the roller conveyor (рольганг) on the right
		for (int32 X = 70; X <= 86; ++X) { I.Set(X, 45, IronLo); I.Set(X, 50, IronLo); }
		for (int32 X = 71; X <= 85; X += 3) { VLine(I, X, 45, 50, Met); VLine(I, X + 1, 45, 50, MetLo); }
		for (int32 LX : { 72, 84 }) { VLine(I, LX, 51, 55, IronDk); }
		I.Blit(BuildSawmillL1(), 8, 4, false);
		// the band-saw frame (пилорама) on the left: a tall iron frame with two wheels and the blade
		for (int32 FX : { 2, 12 }) { VLine(I, FX, 4, 55, IronHi); VLine(I, FX + 1, 4, 55, IronLo); }
		HLine(I, 2, 13, 4, IronHi); HLine(I, 2, 13, 5, Iron);
		FillEllipse(I, 7.5f, 11.f, 4.f, 4.f, Iron, false); FillEllipse(I, 7.5f, 11.f, 1.5f, 1.5f, IronDk, false);
		FillEllipse(I, 7.5f, 44.f, 4.f, 4.f, Iron, false); FillEllipse(I, 7.5f, 44.f, 1.5f, 1.5f, IronDk, false);
		VLine(I, 8, 12, 43, MetHi);
		Box(I, 1, 50, 14, 5, IronLo, 0.3f, 0.3f);
		I.Set(5, 51, Rgb(255, 204, 51)); I.Set(6, 51, Rgb(52, 46, 52)); I.Set(7, 51, Rgb(255, 204, 51));   // hazard mark
		I.Outline(PropOut);
		return I;
	}

	// ---------------------------------------------------------------------------------------- the forge
	FFVImage BuildForge(int32 Level)
	{
		FFVImage I(64, 60);
		// brick chimney (back right)
		Bricks(I, 46, 0, 9, 30);
		HLine(I, 45, 55, 0, Rgb(70, 64, 64)); HLine(I, 45, 55, 1, Rgb(90, 84, 84));
		// grey sheet roof (gable seen from the front)
		for (int32 Y = 12; Y <= 29; ++Y)
		{
			const float T = float(Y - 12) / 17.f;
			const int32 X0 = FMath::RoundToInt(10.f - 10.f * T), X1 = FMath::RoundToInt(53.f + 10.f * T);
			for (int32 X = X0; X <= X1; ++X)
			{
				if (X >= 46 && X <= 54 && Y < 20) { continue; }
				FColor C = (X % 4 == 0) ? Rgb(120, 124, 136) : Rgb(146, 150, 162);
				if (X <= X0 + 1) { C = Rgb(180, 184, 196); } else if (X >= X1 - 1) { C = Rgb(96, 98, 112); }
				I.Set(X, Y, C);
			}
		}
		HLine(I, 0, 63, 29, Rgb(80, 80, 96));
		// fieldstone walls with the open front
		Stones(I, 2, 30, 60, 30, Stone, StoneDk, 331u);
		for (int32 Y = 34; Y <= 59; ++Y)
		{
			for (int32 X = 16; X <= 47; ++X)
			{
				const bool bArch = Y < 38 ? (FMath::Square(float(X) + 0.5f - 32.f) / 256.f + FMath::Square(float(Y) - 38.f) / 16.f <= 1.f) : true;
				if (bArch) { I.Set(X, Y, FireBack); }
			}
		}
		// the hearth (горн): brick box with glowing coals and a flame, the hood above it
		Bricks(I, 21, 46, 22, 9);
		for (int32 X = 23; X <= 40; ++X) { I.Set(X, 46, (X % 3 == 0) ? FireY : FireO); I.Set(X, 45, X % 2 ? Ember : FireR); }
		Flame(I, 31.f, 44, 7, 3.2f, 333u);
		Flame(I, 27.f, 44, 4, 1.8f, 335u);
		Flame(I, 36.f, 44, 5, 2.f, 337u);
		for (int32 Y = 36; Y <= 40; ++Y) { HLine(I, 24 + (Y - 36), 38 - (Y - 36), Y, Y == 36 ? IronHi : Iron); }
		// the anvil on a stump (front left), the water barrel (front right), the coal heap
		Box(I, 6, 53, 7, 6, Rgb(116, 78, 48), 0.3f, 0.35f);
		HLine(I, 3, 14, 50, IronHi); HLine(I, 4, 13, 51, Iron); HLine(I, 6, 11, 52, IronLo); I.Set(2, 50, Iron);
		Box(I, 51, 47, 9, 12, WoodMid, 0.3f, 0.35f);
		HLine(I, 51, 59, 50, IronLo); HLine(I, 51, 59, 55, IronLo);
		HLine(I, 52, 58, 47, Rgb(96, 176, 190)); I.Set(53, 47, Rgb(200, 240, 250));
		for (int32 k = 0; k < 9; ++k) { I.Set(44 + k % 4, 58 - k / 4, k % 3 == 0 ? Rgb(90, 90, 104) : Rgb(40, 38, 46)); }
		if (Level >= 2)
		{
			// the quench trough with steam (front left) and hard rock with violet veins (right)
			Box(I, 16, 55, 16, 5, Stone, 0.3f, 0.35f);
			HLine(I, 17, 30, 56, Rgb(96, 176, 190)); HLine(I, 17, 30, 57, Rgb(60, 130, 160));
			FillEllipse(I, 44.f, 55.f, 5.f, 4.f, RockC, false);
			I.Line(40, 55, 47, 53, Vein); I.Set(44, 54, VeinHi); I.Set(42, 57, Vein);
		}
		I.Outline(PropOut);
		if (Level >= 2)
		{
			for (int32 k = 0; k < 5; ++k) { I.Blend(19 + k * 2, 52 - (k % 2), WithA(Rgb(240, 244, 250), 120)); I.Blend(20 + k * 2, 50 - (k % 3), WithA(Rgb(240, 244, 250), 80)); }
		}
		return I;
	}

	// ---------------------------------------------------------------------------------------- the garden (back line)
	FFVImage BuildGarden(int32 Level)
	{
		const int32 W = PlotX1 - PlotX0;   // 164
		if (Level <= 0)
		{
			FFVImage I(W, 30);
			Rope(I, 2, 27, W - 3, 27);
			BuildSign(I, W / 2, 29);
			I.Outline(PropOut);
			Peg(I, 1, 22, 7); Peg(I, W - 3, 22, 7);
			return I;
		}
		FFVImage I(W, 40);
		// wattle fence (плетень) along the back with posts
		for (int32 Y = 26; Y <= 37; ++Y)
		{
			for (int32 X = 1; X < W - 1; ++X)
			{
				const int32 Band = (Y - 26) / 2;
				const bool bOver = ((X / 5) + Band) % 2 == 0;
				FColor C = bOver ? Wattle : WattleLo;
				if ((Y - 26) % 2 == 0 && bOver) { C = FVArt::Light(Wattle, 0.2f); }
				I.Set(X, Y, C);
			}
		}
		for (int32 X = 1; X < W; X += 10) { VLine(I, X, 23, 39, WoodMid); VLine(I, X + 1, 23, 39, WoodLo); I.Set(X, 22, CutWood); }
		// gate posts at the centre with a small arch sign
		for (int32 GX : { W / 2 - 9, W / 2 + 8 }) { VLine(I, GX, 18, 39, WoodHi); VLine(I, GX + 1, 18, 39, WoodLo); }
		Box(I, W / 2 - 10, 15, 21, 5, WoodMid, 0.3f, 0.35f);
		for (int32 X = W / 2 - 7; X <= W / 2 + 6; X += 3) { I.Set(X, 17, Rgb(84, 168, 64)); I.Set(X + 1, 17, Rgb(236, 128, 60)); }   // carrots painted on it
		I.FillRect(W / 2 - 8, 26, 16, 12, FColor(0, 0, 0, 0));   // the open gate
		// the tool rack (left): a rake and a spade leaning on the fence
		I.Line(12, 38, 16, 18, WoodMid); HLine(I, 13, 19, 18, Iron); for (int32 X = 13; X <= 19; X += 2) { I.Set(X, 19, Iron); }
		I.Line(20, 38, 22, 20, WoodMid); I.FillRect(20, 20, 4, 5, Met); I.Set(20, 20, MetHi);
		// the rain barrel (right)
		Box(I, W - 20, 26, 11, 13, WoodMid, 0.3f, 0.35f);
		HLine(I, W - 20, W - 10, 29, IronLo); HLine(I, W - 20, W - 10, 35, IronLo);
		HLine(I, W - 19, W - 11, 26, Rgb(96, 176, 190));
		if (Level >= 2)
		{
			// a bench and a watering can
			Box(I, 34, 31, 20, 3, WoodHi, 0.3f, 0.35f); VLine(I, 36, 34, 39, WoodLo); VLine(I, 51, 34, 39, WoodLo);
			Box(I, 40, 26, 6, 5, Rgb(110, 150, 190), 0.3f, 0.3f); I.Line(46, 27, 49, 25, Rgb(110, 150, 190)); I.Set(41, 25, Rgb(80, 110, 150));
		}
		if (Level >= 3)
		{
			// a small glass hotbed (парник) with arched frames
			const int32 GX0 = W - 60, GX1 = W - 26;
			for (int32 Y = 18; Y <= 38; ++Y)
			{
				for (int32 X = GX0; X <= GX1; ++X)
				{
					const float DX = (float(X) - float(GX0 + GX1) * 0.5f) / (float(GX1 - GX0) * 0.5f);
					if (float(Y) < 38.f - 20.f * FMath::Sqrt(FMath::Max(0.f, 1.f - DX * DX))) { continue; }
					FColor C = WithA(Rgb(200, 236, 240), 150);
					if ((X - GX0) % 7 == 0) { C = Rgb(236, 236, 230); }
					else if (Y > 30 && R01(X, Y, 341u) < 0.4f) { C = Rgb(84, 168, 64); }
					I.Set(X, Y, C);
				}
			}
			HLine(I, GX0, GX1, 38, WoodLo);
		}
		I.Outline(PropOut);
		return I;
	}

	// ---------------------------------------------------------------------------------------- garden beds
	enum class ECrop : uint8 { None, Reed, Carrot, Potato, Onion, Dill, Pumpkin };
	ECrop CropOf(FName Id)
	{
		if (Id.IsNone()) { return ECrop::None; }
		if (Id == FName(TEXT("res_reed"))) { return ECrop::Reed; }
		if (Id == FName(TEXT("veg_carrot"))) { return ECrop::Carrot; }
		if (Id == FName(TEXT("veg_potato"))) { return ECrop::Potato; }
		if (Id == FName(TEXT("veg_onion"))) { return ECrop::Onion; }
		if (Id == FName(TEXT("veg_dill"))) { return ECrop::Dill; }
		if (Id == FName(TEXT("veg_pumpkin"))) { return ECrop::Pumpkin; }
		return ECrop::Carrot;   // unknown vegetables: a generic leafy crop
	}
	/** One plant at (X, Feet) of the bed; Stage 2 growing, 3 ripe. */
	void Plant(FFVImage& I, ECrop C, int32 X, int32 Feet, int32 Stage, uint32 Seed)
	{
		const bool bRipe = Stage >= 3;
		switch (C)
		{
		case ECrop::Reed:
		{
			const int32 H = bRipe ? 11 : 6;
			VLine(I, X, Feet - H, Feet, Leaf); VLine(I, X - 1, Feet - H + 3, Feet, LeafLo); I.Line(X + 1, Feet, X + 2, Feet - H + 2, LeafHi);
			if (bRipe) { VLine(I, X, Feet - H - 3, Feet - H, Rgb(120, 76, 44)); VLine(I, X + 1, Feet - H - 2, Feet - H, Rgb(90, 56, 34)); I.Set(X, Feet - H - 4, Leaf); }
			break;
		}
		case ECrop::Carrot:
		{
			const int32 H = bRipe ? 6 : 4;
			for (int32 k = -1; k <= 1; ++k) { I.Line(X, Feet - 1, X + k * 2, Feet - H, k == 0 ? LeafHi : Leaf); I.Set(X + k * 2 - 1, Feet - H + 1, LeafLo); }
			if (bRipe) { I.Set(X - 1, Feet, Rgb(236, 128, 60)); I.Set(X, Feet, Rgb(250, 160, 80)); I.Set(X + 1, Feet, Rgb(200, 96, 40)); }
			break;
		}
		case ECrop::Potato:
		{
			const float R = bRipe ? 3.6f : 2.6f;
			PaintBlob(I, float(X) + 0.5f, float(Feet) - R, R, R * 0.85f, LeafHi, Leaf, LeafLo, LeafDk, Seed, 0.25f, 0.3f);
			if (bRipe)
			{
				I.Set(X - 1, Feet - 5, Rgb(250, 250, 240)); I.Set(X + 2, Feet - 3, Rgb(200, 170, 240)); I.Set(X, Feet - 6, Rgb(255, 230, 110));
				I.Set(X + 3, Feet, Rgb(214, 180, 120)); I.Set(X + 4, Feet, Rgb(176, 140, 90));
			}
			break;
		}
		case ECrop::Onion:
		{
			const int32 H = bRipe ? 8 : 5;
			VLine(I, X - 1, Feet - H + 1, Feet, Leaf); VLine(I, X + 1, Feet - H, Feet, LeafHi); VLine(I, X, Feet - H + 2, Feet, LeafLo);
			if (bRipe) { I.Set(X - 1, Feet, Rgb(230, 180, 90)); I.Set(X, Feet, Rgb(250, 210, 120)); I.Set(X + 1, Feet, Rgb(200, 140, 70)); I.Set(X, Feet + 1, Rgb(180, 120, 60)); I.Set(X + 1, Feet - H - 1, Rgb(250, 250, 240)); }
			break;
		}
		case ECrop::Dill:
		{
			const int32 H = bRipe ? 10 : 5;
			VLine(I, X, Feet - H, Feet, Rgb(110, 176, 90));
			for (int32 Y = Feet - H + 2; Y < Feet; Y += 2) { I.Set(X - 1, Y, Rgb(140, 200, 110)); I.Set(X + 1, Y + 1, Rgb(90, 150, 70)); }
			if (bRipe) { HLine(I, X - 2, X + 2, Feet - H - 1, Rgb(236, 214, 70)); I.Set(X - 1, Feet - H - 2, Rgb(255, 236, 110)); I.Set(X + 1, Feet - H - 2, Rgb(255, 236, 110)); I.Set(X, Feet - H - 1, Rgb(200, 176, 50)); }
			break;
		}
		case ECrop::Pumpkin:
		{
			// vines with big leaves (the ripe pumpkin itself is drawn once for the bed)
			I.Line(X - 3, Feet, X + 3, Feet - 1, LeafLo);
			PaintBlob(I, float(X) - 2.f, float(Feet) - 2.f, 2.4f, 2.f, LeafHi, Leaf, LeafLo, LeafDk, Seed, 0.2f, 0.3f);
			PaintBlob(I, float(X) + 2.5f, float(Feet) - 3.f, bRipe ? 2.8f : 2.f, bRipe ? 2.4f : 1.8f, LeafHi, Leaf, LeafLo, LeafDk, Seed + 3, 0.2f, 0.3f);
			if (!bRipe) { I.Set(X + 4, Feet - 1, Rgb(255, 214, 80)); }   // a flower
			break;
		}
		default: break;
		}
	}
	FFVImage BuildBed(FName CropId, int32 Stage)
	{
		FFVImage I(24, 22);
		const ECrop C = CropOf(CropId);
		Stage = FMath::Clamp(Stage, 0, 3);
		// the raised bed: plank edge + tilled soil with furrows
		Box(I, 0, 12, 24, 10, Wood, 0.3f, 0.45f);
		for (int32 Y = 13; Y <= 20; ++Y)
		{
			for (int32 X = 1; X <= 22; ++X)
			{
				const int32 F = (Y - 13) % 4;
				FColor Cc = F == 0 ? Soil : (F == 3 ? SoilDk : SoilLo);
				if (R01(X, Y, 351u) < 0.08f) { Cc = SoilHi; }
				I.Set(X, Y, Cc);
			}
		}
		HLine(I, 1, 22, 21, WoodLo);
		if (C != ECrop::None)
		{
			const int32 Rows[2] = { 16, 20 };
			const int32 PerRow = C == ECrop::Pumpkin ? 2 : 4;
			for (int32 r = 0; r < 2; ++r)
			{
				for (int32 p = 0; p < PerRow; ++p)
				{
					const int32 X = PerRow == 4 ? 4 + p * 5 + r * 2 : 6 + p * 11 + r * 2;
					const int32 Feet = Rows[r] - 1;
					if (Stage == 0)
					{
						// the seed mound: a raised hump of fresh soil with a dark planting hole (reads at any zoom)
						I.Set(X - 1, Feet, SoilHi); I.Set(X, Feet, SoilHi); I.Set(X + 1, Feet, SoilLo); I.Set(X + 2, Feet, SoilDk);
						I.Set(X, Feet - 1, SoilHi); I.Set(X + 1, Feet - 1, SoilLo);
						I.Set(X, Feet - 1, SoilDk);
					}
					else if (Stage == 1) { I.Set(X, Feet, LeafLo); I.Set(X - 1, Feet - 1, LeafHi); I.Set(X + 1, Feet - 1, Leaf); }   // a sprout
					else { Plant(I, C, X, Feet, Stage, 360u + uint32(r * 4 + p)); }
				}
			}
			if (Stage <= 1)
			{
				// a freshly planted bed: a stake with the seed tag in the crop colour, so a 0..33 % bed never looks empty
				FColor Tag;
				switch (C)
				{
				case ECrop::Reed: Tag = Rgb(170, 150, 90); break;
				case ECrop::Potato: Tag = Rgb(176, 136, 84); break;
				case ECrop::Onion: Tag = Rgb(222, 184, 92); break;
				case ECrop::Dill: Tag = Rgb(110, 170, 70); break;
				case ECrop::Pumpkin: Tag = Rgb(240, 160, 50); break;
				default: Tag = Rgb(236, 128, 40); break;
				}
				for (int32 Y = 6; Y <= 15; ++Y) { I.Set(20, Y, Y < 9 ? WoodMid : WoodLo); }
				for (int32 Y = 5; Y <= 9; ++Y) { for (int32 X = 17; X <= 22; ++X) { I.Set(X, Y, (Y == 5 || Y == 9) ? PaperLo : Paper); } }
				for (int32 Y = 6; Y <= 8; ++Y) { for (int32 X = 18; X <= 21; ++X) { I.Set(X, Y, Y == 8 ? FVArt::Shade(Tag, 0.25f) : Tag); } }
				I.Set(18, 6, FVArt::Light(Tag, 0.4f));
			}
			if (C == ECrop::Pumpkin && Stage >= 3)
			{
				// the ripe pumpkin (12x9) lying on the bed
				const FColor Pk(236, 128, 40), PkHi(255, 176, 80), PkLo(184, 84, 30);
				for (int32 Y = 11; Y <= 19; ++Y)
				{
					for (int32 X = 6; X <= 17; ++X)
					{
						const float DX = (float(X) + 0.5f - 12.f) / 6.f, DY = (float(Y) + 0.5f - 15.5f) / 4.5f;
						if (DX * DX + DY * DY > 1.f) { continue; }
						FColor Cc = (X == 9 || X == 12 || X == 15) ? PkLo : Pk;
						if (DX < -0.5f && DY < 0.f) { Cc = PkHi; } else if (DY > 0.6f) { Cc = PkLo; }
						I.Set(X, Y, Cc);
					}
				}
				I.Set(12, 10, Rgb(110, 90, 50)); I.Set(12, 9, Rgb(90, 130, 60)); I.Set(13, 9, Leaf);
			}
		}
		else
		{
			for (int32 k = 0; k < 6; ++k) { I.Set(3 + k * 3 + (k % 2), 14 + (k % 3) * 2, SoilHi); }   // clods of freshly dug soil
		}
		I.Outline(PropOut);
		return I;
	}

	// ---------------------------------------------------------------------------------------- world helpers
	struct FStampItem { FFVImage Img; int32 X = 0, Y = 0; bool bFlip = false; float Shadow = 0.f; uint8 Tag = 0; };   // Tag 1 = the cottage
	// the back fence line (the plank run painted into the background and the YardFence decor Hit): feet y, x run, the
	// tallest decor section (the iron one, 24 texels) — the band where props in front of it must cover the decor
	constexpr int32 FenceX0 = 20, FenceX1 = 780, FenceY = 90, FenceBandH = 24;
	constexpr int32 RimBand = 6;   // DarkRim band of the world image
	FVector2D WP(float X, float Y) { return FVector2D(X * FV::WorldScale, Y * FV::WorldScale); }
	FFVRect WR(float X, float Y, float W, float H) { return FFVRect(X * FV::WorldScale, Y * FV::WorldScale, W * FV::WorldScale, H * FV::WorldScale); }
	void AddAnchor(FVWorldU6::FWorldDef& D, FVWorldU6::EAnchor K, FName Id, float X, float Y, int32 Index, FFVRect Hit)
	{
		FVWorldU6::FAnchor A;
		A.Kind = K;
		A.Id = Id;
		A.Index = Index;
		A.Feet = WP(X, Y);
		A.Hit = Hit;
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
	FFVImage MakeAppleTree(int32 V)
	{
		FFVImage T = UFVSprites::BuildTree(EFVTreeKind::Oak, EFVTreeState::Full, V);
		for (int32 k = 0; k < 10; ++k)
		{
			const int32 X = 6 + FMath::FloorToInt(R01(k, 1, 371u + uint32(V)) * 24.f), Y = 4 + FMath::FloorToInt(R01(k, 2, 371u + uint32(V)) * 22.f);
			if (T.Get(X, Y).A > 0 && T.Get(X + 1, Y + 1).A > 0 && T.Get(X, Y).G > 60) { T.Set(X, Y, Rgb(236, 60, 50)); T.Set(X + 1, Y, Rgb(190, 40, 40)); T.Set(X, Y + 1, Rgb(190, 40, 40)); T.Set(X + 1, Y + 1, Rgb(150, 30, 34)); }
		}
		return T;
	}
	/** Plank fence along [X0, X1] with its bottom at Y (18 texels tall, posts every 16). */
	FFVImage MakeFenceRun(int32 Len)
	{
		FFVImage I(Len, 18);
		Planks(I, 0, 4, Len, 13, WoodMid, 4, 381u, true);
		for (int32 X = 0; X < Len; X += 4) { I.Set(X + 1, 3, WoodMid); I.Set(X + 2, 3, WoodMid); }
		HLine(I, 0, Len - 1, 7, WoodLo); HLine(I, 0, Len - 1, 13, WoodLo);
		for (int32 X = 0; X < Len; X += 16) { VLine(I, X, 1, 17, WoodDk); VLine(I, X + 1, 1, 17, WoodLo); I.Set(X, 0, WoodLo); }
		I.Outline(PropOut);
		return I;
	}
	/** The y-sorted static props of the yard background (step 6 of BuildYard; also FVArtYardFront): the cottage, the
	    fences, the trees, bushes, the woodpile, the edges, the gate. */
	void YardStampItems(TArray<FStampItem>& Items)
	{
		auto Add = [&Items](FFVImage&& Img, int32 X, int32 Y, bool bFlip, float Shadow)
		{
			FStampItem It; It.Img = MoveTemp(Img); It.X = X; It.Y = Y; It.bFlip = bFlip; It.Shadow = Shadow;
			Items.Add(MoveTemp(It));
		};
		for (int32 X = 16; X < WorldW - 8; X += 22 + (X % 3) * 4)
		{
			const float Rn = R01(X, 3, 433u);
			const EFVTreeKind K = Rn < 0.4f ? EFVTreeKind::Birch : (Rn < 0.75f ? EFVTreeKind::Oak : EFVTreeKind::Pine);
			Add(UFVSprites::BuildTree(K, EFVTreeState::Full, X % 4), X, 58 + FMath::FloorToInt(R01(X, 4, 433u) * 10.f), Rn > 0.5f, 6.f);
		}
		Add(MakeFenceRun(FenceX1 - FenceX0), (FenceX0 + FenceX1) / 2, FenceY, false, 0.f);
		Add(MakeAppleTree(0), 250, 104, false, 10.f);
		Add(MakeAppleTree(2), 520, 104, true, 10.f);
		Add(MakeCottage(), 400, 118, false, 36.f);
		Items.Last().Tag = 1;
		{
			FFVImage WoodImg(22, 14);
			for (int32 r = 0; r < 3; ++r)
			{
				for (int32 c = 0; c < 4 - (r == 2 ? 1 : 0); ++c)
				{
					const float WX = 3.5f + float(c) * 5.f + (r >= 1 ? 2.5f : 0.f), WY = 11.f - float(r) * 4.f;
					FillEllipse(WoodImg, WX, WY, 2.6f, 2.3f, WoodLo, false); FillEllipse(WoodImg, WX - 0.3f, WY - 0.3f, 1.7f, 1.5f, CutWood, false);
					WoodImg.Set(FMath::FloorToInt(WX), FMath::FloorToInt(WY), CutRing);
				}
			}
			WoodImg.Outline(PropOut);
			Add(MoveTemp(WoodImg), 454, 114, false, 9.f);
		}
		// the left hedge, the right tree line
		for (int32 Y = 96; Y < WorldH + 10; Y += 16) { Add(MakeBush(450u + uint32(Y), Rgb(76, 150, 66), 8, Y % 3 == 0), 10, Y, Y % 2 == 0, 0.f); }
		for (int32 Y = 100; Y < WorldH + 20; Y += 24) { Add(UFVSprites::BuildTree(Y % 2 ? EFVTreeKind::Oak : EFVTreeKind::Birch, EFVTreeState::Full, (Y / 24) % 4), WorldW - 8, Y, false, 6.f); }
		// the bottom: a plank fence with the gate gap, bushes and crowns below it
		Add(MakeFenceRun(360), 202, 304, false, 0.f);
		Add(MakeFenceRun(360), 598, 304, false, 0.f);
		for (int32 X = 14; X < WorldW; X += 20) { if (FMath::Abs(X - 400) > 18) { Add(MakeBush(470u + uint32(X), Rgb(70, 142, 62), 7 + (X % 3), X % 5 == 0), X, 318, X % 2 == 0, 0.f); } }
		for (int32 k = 0; k < 12; ++k)   // bushes along the walk band edges
		{
			const int32 X = k < 6 ? 36 + k * 4 : 740 + (k - 6) * 4;
			Add(MakeBush(490u + uint32(k), Rgb(90, 164, 70), 5 + k % 3, k % 4 == 0), X, 120 + k * 14 % 170, k % 2 == 0, 4.f);
		}
		// gate posts with a sign to the map
		{
			FFVImage Gate(40, 30);
			for (int32 GX : { 4, 34 }) { Box(Gate, GX, 6, 3, 24, WoodMid, 0.3f, 0.35f); Gate.Set(GX + 1, 5, CutWood); }
			Box(Gate, 8, 0, 24, 7, Rgb(214, 170, 110), 0.25f, 0.3f);
			for (int32 X = 11; X <= 28; X += 2) { Gate.Set(X, 3, Rgb(120, 90, 60)); }
			Gate.Outline(PropOut);
			Add(MoveTemp(Gate), 400, 306, false, 0.f);
		}
	}
}

// =====================================================================================================================
// FVWorldU6 / UFVSprites entry points
// =====================================================================================================================
void FVWorldU6::BuildYard(FFVImage& OutBg, FWorldDef& OutDef)
{
	using namespace FVArtYardImpl;
	FFVImage& I = OutBg;
	I.Init(WorldW, WorldH, FColor(0, 0, 0, 255));

	// ---- 1) the lawn
	FillGround(I, 0, 0, WorldW, WorldH, GrassLo, GrassBase, GrassHi, 15.f, 401u);

	// ---- 2) paths: door -> gate (main), to the garden, to the sawmill and the forge; trodden plots
	// trodden earth under the sawmill and the forge plots (soft-edged, noisy)
	auto Patch = [&I](float CX, float CY, float RX, float RY, FColor A, FColor B, uint32 Seed)
	{
		for (int32 Y = FMath::FloorToInt(CY - RY); Y <= FMath::CeilToInt(CY + RY); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
			{
				const float D = FMath::Square((float(X) - CX) / RX) + FMath::Square((float(Y) - CY) / RY) + (VNoise(float(X) * 0.2f, float(Y) * 0.2f, Seed) - 0.5f) * 0.5f;
				if (D > 1.f || (D > 0.7f && Bayer(X, Y) < (D - 0.7f) / 0.3f)) { continue; }
				const float V = VNoise(float(X) * 0.35f, float(Y) * 0.35f, Seed + 1) + (Bayer(X, Y) - 0.5f) * 0.2f;
				I.Set(X, Y, V > 0.55f ? A : (V > 0.3f ? B : FVArt::Shade(B, 0.15f)));
			}
		}
	};
	Patch(606.f, 156.f, 46.f, 20.f, Rgb(204, 170, 116), Rgb(170, 134, 90), 403u);
	Patch(712.f, 256.f, 42.f, 22.f, Rgb(150, 140, 126), Rgb(128, 110, 90), 405u);
	PaintPath(I, { FVector2D(400.f, 120.f), FVector2D(400.f, 170.f), FVector2D(402.f, 240.f), FVector2D(400.f, 320.f) }, 16.f, 407u);
	PaintPath(I, { FVector2D(400.f, 196.f), FVector2D(320.f, 204.f), FVector2D(250.f, 196.f), FVector2D(214.f, 190.f) }, 11.f, 409u);
	PaintPath(I, { FVector2D(402.f, 186.f), FVector2D(500.f, 176.f), FVector2D(606.f, 172.f) }, 11.f, 411u);
	PaintPath(I, { FVector2D(606.f, 172.f), FVector2D(660.f, 214.f), FVector2D(712.f, 270.f) }, 10.f, 413u);

	// ---- 3) grass and flowers
	Tufts(I, 20, 96, WorldW - 20, WalkBottom, 900, GrassBase, 415u);
	Flowers(I, 20, 100, WorldW - 20, WalkBottom, 260, 417u);

	// ---- 4) the garden plot: a low log kerb around it (its back line is the garden building), tilled corners
	for (int32 X = PlotX0; X <= PlotX1; ++X)
	{
		I.Set(X, PlotY1, WoodHi); I.Set(X, PlotY1 + 1, WoodMid); I.Set(X, PlotY1 + 2, WoodDk);
	}
	for (int32 Y = PlotY0; Y <= PlotY1 + 2; ++Y)
	{
		for (int32 X : { PlotX0, PlotX1 }) { I.Set(X - 1, Y, WoodHi); I.Set(X, Y, WoodMid); I.Set(X + 1, Y, WoodDk); }
	}
	for (int32 Y = PlotY0 + 2; Y < PlotY1; ++Y)
	{
		for (int32 X = PlotX0 + 2; X < PlotX1 - 1; ++X) { if (Fbm(float(X) / 6.f, float(Y) / 6.f, 419u) > 0.64f) { I.Set(X, Y, FVArt::Mix(I.Get(X, Y), SoilHi, 0.35f)); } }
	}

	// ---- 5) the compost heap (dig spots are drawn on it at runtime) and the practice pond with a plank
	PaintBlob(I, 296.f, 238.f, 34.f, 11.f, Rgb(150, 116, 70), Rgb(118, 86, 54), Rgb(92, 64, 42), Rgb(66, 46, 34), 421u, 0.15f, 0.5f);
	for (int32 k = 0; k < 60; ++k)
	{
		const int32 X = 266 + FMath::FloorToInt(R01(k, 1, 423u) * 60.f), Y = 229 + FMath::FloorToInt(R01(k, 2, 423u) * 18.f);
		if (I.Get(X, Y).G > 100) { continue; }
		const float Rn = R01(k, 3, 423u);
		I.Set(X, Y, Rn < 0.3f ? Rgb(120, 150, 60) : (Rn < 0.5f ? Rgb(160, 130, 60) : (Rn < 0.6f ? Rgb(230, 170, 70) : Rgb(80, 110, 50))));
	}
	PaintBlob(I, 290.f, 233.f, 18.f, 6.f, Rgb(160, 126, 76), Rgb(130, 96, 60), Rgb(104, 74, 48), Rgb(80, 56, 40), 429u, 0.2f, 0.5f);
	for (int32 k = 0; k < 14; ++k)   // tops (ботва) and straw sticking out
	{
		const int32 X = 270 + FMath::FloorToInt(R01(k, 4, 423u) * 52.f), Y = 229 + FMath::FloorToInt(R01(k, 5, 423u) * 10.f);
		const FColor C = k % 3 == 0 ? Rgb(220, 196, 110) : Rgb(96, 150, 60);
		I.Set(X, Y, C); I.Set(X + (k % 2 ? 1 : -1), Y - 1, C); I.Set(X, Y + 1, FVArt::Shade(C, 0.3f));
	}
	for (int32 X = 262; X <= 330; ++X) { I.Set(X, 249, WoodHi); I.Set(X, 250, WoodMid); I.Set(X, 251, WoodDk); }   // plank kerb in front of the heap
	for (int32 X : { 262, 330 }) { VLine(I, X, 244, 251, WoodMid); I.Set(X, 243, CutWood); }
	const float PCX = 548.f, PCY = 236.f, PRX = 36.f, PRY = 16.f;
	for (int32 Y = FMath::FloorToInt(PCY - PRY - 4.f); Y <= FMath::CeilToInt(PCY + PRY + 4.f); ++Y)
	{
		for (int32 X = FMath::FloorToInt(PCX - PRX - 5.f); X <= FMath::CeilToInt(PCX + PRX + 5.f); ++X)
		{
			const float DX = (float(X) + 0.5f - PCX) / PRX, DY = (float(Y) + 0.5f - PCY) / PRY;
			const float D = DX * DX + DY * DY;
			const float DOut = FMath::Square((float(X) + 0.5f - PCX) / (PRX + 4.f)) + FMath::Square((float(Y) + 0.5f - PCY) / (PRY + 3.f));
			if (DOut > 1.f) { continue; }
			if (D > 1.f) { I.Set(X, Y, DY < 0.f ? Rgb(146, 204, 96) : Rgb(72, 136, 58)); continue; }   // the moist lip
			const float Depth = 1.f - D;
			FColor C = FVArt::Mix(PondSh, PondDp, FMath::Clamp(Depth * 1.4f + (Bayer(X, Y) - 0.5f) * 0.2f, 0.f, 1.f));
			if (DY < -0.72f) { C = Rgb(206, 186, 134); }   // sandy north edge
			else if (R01(X, Y / 2, 425u) < 0.02f) { C = Rgb(220, 246, 250); }
			I.Set(X, Y, C);
		}
	}
	for (int32 k = 0; k < 4; ++k)   // lily pads
	{
		const float LX = PCX + (R01(k, 1, 427u) - 0.3f) * PRX * 1.2f, LY = PCY + (R01(k, 2, 427u) - 0.3f) * PRY;
		FillEllipse(I, LX, LY, 2.6f, 1.6f, Rgb(84, 168, 64), false); I.Set(FMath::FloorToInt(LX) + 1, FMath::FloorToInt(LY), Rgb(44, 110, 60));
		if (k == 1) { I.Set(FMath::FloorToInt(LX), FMath::FloorToInt(LY) - 1, Rgb(250, 200, 220)); }
	}
	for (int32 k = 0; k < 9; ++k)   // reeds on the east shore
	{
		const int32 RX = FMath::RoundToInt(PCX + PRX - 6.f) + k * 2 - (k % 3), RB = FMath::RoundToInt(PCY) + (k % 4) * 2 - 2;
		const int32 H = 7 + (k * 5) % 6;
		VLine(I, RX, RB - H, RB, Rgb(84, 150, 60)); I.Set(RX + 1, RB - H + 2, Rgb(120, 190, 80));
		if (k % 3 == 1) { VLine(I, RX, RB - H - 3, RB - H - 1, Rgb(120, 76, 44)); }
	}
	const int32 PX0 = 526, PW = 12, PT = FMath::FloorToInt(PCY - PRY) - 5, PB = FMath::RoundToInt(PCY - 0.1f * PRY);
	for (int32 Y = PT; Y < PB; ++Y)
	{
		const int32 Row = (Y - PT) % 4;
		for (int32 X = PX0; X < PX0 + PW; ++X)
		{
			FColor C = Row == 3 ? Rgb(76, 48, 32) : (Row == 0 ? Rgb(204, 150, 94) : Rgb(170, 118, 72));
			if (X == PX0 || X == PX0 + PW - 1) { C = Rgb(76, 48, 32); }
			I.Set(X, Y, C);
		}
		MulPx(I, PX0 + PW, Y, 0.74f); MulPx(I, PX0 + PW + 1, Y, 0.8f);
	}

	// ---- 6) y-sorted static props: the cottage, fences, trees, bushes, the woodpile, the edges
	TArray<FStampItem> Items;
	for (int32 X = 0; X < WorldW; X += 8)   // far crowns behind the fence
	{
		LeafBlob(I, float(X) + R01(X, 1, 431u) * 4.f, 10.f + R01(X, 2, 431u) * 16.f, 12.f, 12.f, Rgb(62, 128, 70), 440u + uint32(X));
	}
	YardStampItems(Items);
	Items.Sort([](const FStampItem& A, const FStampItem& B) { return A.Y < B.Y; });
	for (const FStampItem& It : Items) { Stamp(I, It.Img, It.X, It.Y, It.bFlip, It.Shadow); }
	// front stakes of the garden plot (flat, stay after building: the corners of the kerb)
	for (int32 X : { PlotX0 - 1, PlotX1 - 1 }) { I.Set(X, PlotY1 - 3, CutWood); VLine(I, X, PlotY1 - 2, PlotY1 + 1, WoodHi); VLine(I, X + 1, PlotY1 - 2, PlotY1 + 1, WoodLo); }

	DarkRim(I, RimBand);

	// ---- the world description (world px)
	OutDef = FWorldDef();
	OutDef.LocationId = FVDB::HomeId();
	OutDef.SizeTex = FIntPoint(WorldW, WorldH);
	OutDef.bWater = false;
	OutDef.bBirds = true;
	OutDef.bButterflies = true;
	OutDef.bPond = true;
	OutDef.PondCenter = WP(PCX, PCY);
	OutDef.PondRadii = FVector2D(PRX * FV::WorldScale, PRY * FV::WorldScale);
	OutDef.PondPlank = WR(float(PX0), float(PT), float(PW), float(PB - PT));
	OutDef.Walk.Add(WR(28.f, float(WalkTop), float(WorldW - 56), float(WalkBottom - WalkTop)));
	OutDef.Walk.Add(WR(386.f, float(WalkBottom), 28.f, float(WorldH - WalkBottom)));   // through the gate
	OutDef.Blockers.Add(WR(361.f, float(WalkTop), 78.f, 20.f));   // the cottage
	OutDef.Blockers.Add(WR(441.f, float(WalkTop), 26.f, 16.f));   // the woodpile
	OutDef.Blockers.Add(WR(240.f, float(WalkTop), 20.f, 6.f));    // apple trees
	OutDef.Blockers.Add(WR(510.f, float(WalkTop), 20.f, 6.f));
	OutDef.Blockers.Add(WR(PCX - PRX + 6.f, PCY - PRY + 5.f, PRX * 2.f - 12.f, PRY * 2.f - 8.f));   // the pond water (the plank stays walkable)

	using FVWorldU6::EAnchor;
	const FFVRect NoHit;
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("default")), 400.f, 150.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("door")), 400.f, 128.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("from_house")), 400.f, 128.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("from_map")), 400.f, 276.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("from_pond")), 400.f, 276.f, 0, NoHit);   // Р69: walked up from the pond
	AddAnchor(OutDef, EAnchor::Door, FName(TEXT("house_door")), 400.f, 124.f, 0, WR(391.f, 90.f, 18.f, 32.f));
	{
		FAnchor A;
		A.Kind = EAnchor::Exit;
		// Р69: the gate leads down the road to the village pond (the map stays the quick way)
		A.Id = FName(TEXT("from_home"));
		A.Target = FName(TEXT("pond"));
		A.Feet = WP(400.f, 312.f);
		A.Hit = WR(386.f, 306.f, 28.f, 14.f);
		A.Label = TEXT("↓ Деревенский пруд");
		A.SignFeet = WP(432.f, 298.f);
		A.SignDir = uint8(FVWorldU6::EWayDir::Down);
		OutDef.Anchors.Add(A);
	}
	// building plots (the scene draws YardBuildingSprite(B, level) at the feet)
	AddAnchor(OutDef, EAnchor::BuildPlot, FVDBU6::YardBuildingId(EFVYardBuilding::Garden), float(PlotX0 + PlotX1) * 0.5f, float(PlotY0), 0, WR(float(PlotX0), float(PlotY0) - 30.f, float(PlotX1 - PlotX0), 40.f));
	AddAnchor(OutDef, EAnchor::BuildPlot, FVDBU6::YardBuildingId(EFVYardBuilding::Sawmill), 606.f, 166.f, 0, WR(566.f, 114.f, 80.f, 54.f));
	AddAnchor(OutDef, EAnchor::BuildPlot, FVDBU6::YardBuildingId(EFVYardBuilding::Forge), 712.f, 268.f, 0, WR(678.f, 208.f, 68.f, 62.f));
	for (int32 b = 0; b < 12; ++b) { AddAnchor(OutDef, EAnchor::GardenBed, NAME_None, float(BedCols[b % 4]), float(BedRows[b / 4]), b, NoHit); }
	// outdoor stations (§6.4): the smokehouse by the fence, the worm farm by the compost heap
	AddAnchor(OutDef, EAnchor::Station, FVDB::StationId(EFVStation::Smokehouse), 312.f, 114.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Station, FVDB::StationId(EFVStation::WormFarm), 350.f, 252.f, 0, NoHit);
	OutDef.Blockers.Add(WR(298.f, float(WalkTop), 28.f, 14.f));
	// compost heap dig spots, resource slots
	AddAnchor(OutDef, EAnchor::DigSpot, NAME_None, 278.f, 242.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::DigSpot, NAME_None, 297.f, 238.f, 1, NoHit);
	AddAnchor(OutDef, EAnchor::DigSpot, NAME_None, 316.f, 243.f, 2, NoHit);
	static const FVector2D Picks[8] = { FVector2D(252.f, 150.f), FVector2D(330.f, 178.f), FVector2D(446.f, 226.f), FVector2D(596.f, 206.f),
		FVector2D(640.f, 270.f), FVector2D(484.f, 272.f), FVector2D(248.f, 270.f), FVector2D(522.f, 140.f) };
	for (int32 p = 0; p < 8; ++p) { AddAnchor(OutDef, EAnchor::Pickup, NAME_None, float(Picks[p].X), float(Picks[p].Y), p, NoHit); }
	// decor slots of the yard (ART-DECOR sprites, pivot bottom-centre; fence / path: Hit = the run to tile)
	auto Decor = [&OutDef](EFVDecorSlot S, float X, float Y, FFVRect Hit)
	{
		FAnchor A;
		A.Kind = EAnchor::DecorSlot;
		A.Index = int32(S);
		A.Feet = WP(X, Y);
		A.Hit = Hit;
		OutDef.Anchors.Add(A);
	};
	Decor(EFVDecorSlot::YardRoof, 400.f, 118.f, NoHit);
	Decor(EFVDecorSlot::YardWalls, 400.f, 118.f, NoHit);
	Decor(EFVDecorSlot::YardShutters, 400.f, 118.f, NoHit);
	Decor(EFVDecorSlot::YardVane, 386.f, 60.f, NoHit);
	Decor(EFVDecorSlot::YardFence, float(FenceX0), float(FenceY), WR(float(FenceX0), float(FenceY - 18), float(FenceX1 - FenceX0), 18.f));
	Decor(EFVDecorSlot::YardPath, 400.f, 124.f, WR(392.f, 124.f, 16.f, 196.f));
	Decor(EFVDecorSlot::YardLantern, 424.f, 134.f, NoHit);
	Decor(EFVDecorSlot::YardMailbox, 430.f, 280.f, NoHit);
	Decor(EFVDecorSlot::YardBench, 474.f, 126.f, NoHit);
	Decor(EFVDecorSlot::YardFlowerbed, 348.f, 130.f, NoHit);
	Decor(EFVDecorSlot::YardScarecrow, 70.f, 130.f, NoHit);
	Decor(EFVDecorSlot::YardWell, 332.f, 164.f, NoHit);
	Decor(EFVDecorSlot::YardCenter, 474.f, 212.f, NoHit);
	// lights: cottage windows and the door lantern; chimney smoke
	AddLight(OutDef, 379.f, 99.f, 20.f, FColor(255, 200, 120), false);
	AddLight(OutDef, 421.f, 99.f, 20.f, FColor(255, 200, 120), false);
	AddLight(OutDef, 408.f, 93.f, 26.f, FColor(255, 214, 140), true);
	AddAnchor(OutDef, EAnchor::Smoke, NAME_None, 420.f, 56.f, 0, NoHit);
}

const FFVSprite& UFVSprites::YardBuildingSprite(EFVYardBuilding B, int32 Level)
{
	const uint32 Bi = FMath::Min(uint32(B), uint32(EFVYardBuilding::Count) - 1u);
	const int32 L = FMath::Clamp(Level, 0, EFVYardBuilding(Bi) == EFVYardBuilding::Garden ? 3 : 2);
	const uint64 CacheKey = MakeKey(FVSpriteCat::Yard, 1 + Bi, uint32(L));
	if (const FFVSprite* Found = FindCached(CacheKey))
	{
		return *Found;
	}
	return AddCached(CacheKey, BuildYardBuilding(EFVYardBuilding(Bi), L), FString::Printf(TEXT("YardBuilding_%u_%d"), Bi, L));
}

const FFVSprite& UFVSprites::GardenBedSprite(FName CropId, int32 Stage)
{
	const FVArtYardImpl::ECrop C = FVArtYardImpl::CropOf(CropId);
	const int32 S = C == FVArtYardImpl::ECrop::None ? 0 : FMath::Clamp(Stage, 0, 3);
	const uint64 CacheKey = MakeKey(FVSpriteCat::Yard, 20 + uint32(C), uint32(S));
	if (const FFVSprite* Found = FindCached(CacheKey))
	{
		return *Found;
	}
	return AddCached(CacheKey, FVArtYardImpl::BuildBed(CropId, S), FString::Printf(TEXT("GardenBed_%d_%d"), int32(C), S));
}

const FFVSprite& UFVSprites::YardStationSprite(EFVStation S, int32 Level)
{
	if (S != EFVStation::Smokehouse && S != EFVStation::WormFarm)
	{
		return StationSprite(S, Level);
	}
	const int32 L = FMath::Clamp(Level, 0, 3);
	const uint64 CacheKey = MakeKey(FVSpriteCat::Yard, 40 + uint32(S), uint32(L));
	if (const FFVSprite* Found = FindCached(CacheKey))
	{
		return *Found;
	}
	// the indoor art on an earth mound with grass: same width, 2 texels taller (pivot and effect points unchanged)
	using namespace FVArtYardImpl;
	const FFVImage St = BuildStation(S, L);
	FFVImage Img(St.W, St.H + 2);
	const float CX = float(St.W) * 0.5f;
	FillEllipse(Img, CX, float(St.H) - 0.5f, CX - 0.5f, 2.6f, Soil, false);
	FillEllipse(Img, CX - 1.f, float(St.H) - 1.5f, CX - 3.f, 1.4f, SoilHi, false);
	for (int32 X = 0; X < Img.W; X += 3) { Img.Set(X, St.H + 1 - (X % 2), Leaf); Img.Set(X + 1, St.H - (X % 2), LeafHi); }
	Img.Blit(St, 0, 0, false);
	return AddCached(CacheKey, Img, FString::Printf(TEXT("YardStation_%d_%d"), int32(S), L));
}

FFVImage UFVSprites::BuildYardBuilding(EFVYardBuilding B, int32 Level)
{
	using namespace FVArtYardImpl;
	switch (B)
	{
	case EFVYardBuilding::Sawmill: return Level <= 0 ? BuildPlot(72) : (Level == 1 ? BuildSawmillL1() : BuildSawmillL2());
	case EFVYardBuilding::Forge:   return Level <= 0 ? BuildPlot(64) : BuildForge(Level);
	case EFVYardBuilding::Garden:  return BuildGarden(Level);
	default:                       return BuildMissing();
	}
}

FVector2D UFVSprites::YardEffectPoint(EFVYardBuilding B, int32 Level)
{
	if (Level <= 0)
	{
		return FVector2D(-1.f, -1.f);
	}
	switch (B)
	{
	case EFVYardBuilding::Sawmill: return Level >= 2 ? FVector2D(41.f, 34.f) : FVector2D(33.f, 30.f);
	case EFVYardBuilding::Forge:   return FVector2D(31.f, 42.f);
	default:                       return FVector2D(-1.f, -1.f);
	}
}

// =====================================================================================================================
// FVArtYardFront (FVArtYardFront.h): the background props in front of the back fence line, redrawn y-sorted at runtime
// =====================================================================================================================
namespace FVArtYardFrontImpl
{
	using namespace FVArtYardImpl;

	struct FRegion { int32 Item = 0; FIntRect R; int32 SortY = 0; };   // world texels
	struct FGeo
	{
		bool bBuilt = false;
		TArray<FRegion> Regions;
		FVector2D CottageFeet = FVector2D::ZeroVector;
		TArray<FIntRect> CottageRects;
	};

	FIntRect ItemRect(const FStampItem& It)
	{
		const int32 X0 = It.X - It.Img.W / 2, Y0 = It.Y - It.Img.H;   // == Stamp
		return FIntRect(X0, Y0, X0 + It.Img.W, Y0 + It.Img.H);
	}
	FIntRect Clip(const FIntRect& A, const FIntRect& B)
	{
		return FIntRect(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y), FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y));
	}
	bool NonEmpty(const FIntRect& R) { return R.Max.X > R.Min.X && R.Max.Y > R.Min.Y; }

	/** Front props: painted after the fence line (feet below it) and overlapping the band of the fence decor. Each is
	    redrawn over its rect (the cottage without its door zone below the fence line); the props the background painted
	    later over the same rect are redrawn there too, in the background's order, so the pixels stay exactly the same. */
	void BuildGeo(FGeo& G, const TArray<FStampItem>& Items)
	{
		const FIntRect Inner(RimBand, RimBand, WorldW - RimBand, WorldH - RimBand);   // DarkRim is painted over the props
		const FIntRect Band(FenceX0, FenceY - FenceBandH, FenceX1, FenceY);
		for (int32 f = 0; f < Items.Num(); ++f)
		{
			const FStampItem& F = Items[f];
			const FIntRect IR = ItemRect(F);
			const FIntRect FR = Clip(IR, Inner);
			if (F.Y <= FenceY || !NonEmpty(FR) || !NonEmpty(Clip(FR, Band))) { continue; }
			TArray<FIntRect> Own;
			if (F.Tag == 1)
			{
				// the cottage: everything above the fence line; below it only the sides (the door and its highlight stay
				// in the wall layer)
				const int32 Cut = FMath::Clamp(FenceY - IR.Min.Y, 0, F.Img.H);
				const int32 DX0 = FMath::Clamp(CottageDoorX - 6, 0, F.Img.W), DX1 = FMath::Clamp(CottageDoorX + CottageDoorW + 6, DX0, F.Img.W);
				G.CottageFeet = WP(float(F.X), float(F.Y));
				G.CottageRects = { FIntRect(0, 0, F.Img.W, Cut), FIntRect(0, Cut, DX0, F.Img.H), FIntRect(DX1, Cut, F.Img.W, F.Img.H) };
				for (const FIntRect& CR : G.CottageRects) { Own.Add(Clip(FIntRect(IR.Min + CR.Min, IR.Min + CR.Max), Inner)); }
			}
			else
			{
				Own.Add(FR);
			}
			for (const FIntRect& Rg : Own)
			{
				if (!NonEmpty(Rg)) { continue; }
				FRegion Self;
				Self.Item = f;
				Self.R = Rg;
				Self.SortY = F.Y;
				G.Regions.Add(Self);
				TArray<int32> Later;
				for (int32 g = 0; g < Items.Num(); ++g)
				{
					if (g != f && Items[g].Y > F.Y && NonEmpty(Clip(ItemRect(Items[g]), Rg))) { Later.Add(g); }
				}
				Later.StableSort([&Items](int32 A, int32 B) { return Items[A].Y < Items[B].Y; });
				for (int32 g : Later)
				{
					FRegion Over;
					Over.Item = g;
					Over.R = Clip(ItemRect(Items[g]), Rg);
					Over.SortY = Items[g].Y;
					G.Regions.Add(Over);
				}
			}
		}
		G.bBuilt = true;
	}

	FGeo& Geo()
	{
		static FGeo G;
		if (!G.bBuilt)
		{
			TArray<FStampItem> Items;
			YardStampItems(Items);
			BuildGeo(G, Items);
		}
		return G;
	}
}

const TArray<FVArtYardFront::FPart>& FVArtYardFront::Parts()
{
	using namespace FVArtYardFrontImpl;
	static TArray<FPart> Cached;
	static TWeakObjectPtr<UFVSprites> Owner;
	const FGeo& G = Geo();
	UFVSprites* Spr = UFVSprites::Get();
	if (!Spr)
	{
		Cached.Reset();
		return Cached;
	}
	const uint64 Key0 = UFVSprites::MakeKey(FVSpriteCat::Yard, 80u, 0u, 0);
	if (Owner.Get() == Spr && Cached.Num() == G.Regions.Num() && (Cached.Num() == 0 || Spr->FindCached(Key0))) { return Cached; }
	Cached.Reset();
	Owner = Spr;
	TArray<FStampItem> Items;
	for (int32 k = 0; k < G.Regions.Num(); ++k)
	{
		const FRegion& Rg = G.Regions[k];
		const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::Yard, 80u, uint32(k), 0);
		const FFVSprite* S = Spr->FindCached(Key);
		if (!S)
		{
			if (Items.Num() == 0) { YardStampItems(Items); }
			if (!Items.IsValidIndex(Rg.Item)) { continue; }
			const FStampItem& It = Items[Rg.Item];
			const FIntRect IR = ItemRect(It);
			FFVImage Part(Rg.R.Width(), Rg.R.Height());
			for (int32 Y = 0; Y < Part.H; ++Y)
			{
				for (int32 X = 0; X < Part.W; ++X)
				{
					const int32 SX = Rg.R.Min.X - IR.Min.X + X, SY = Rg.R.Min.Y - IR.Min.Y + Y;
					Part.Set(X, Y, It.Img.Get(It.bFlip ? It.Img.W - 1 - SX : SX, SY));   // == Blit's flip
				}
			}
			S = &Spr->AddCached(Key, Part, FString::Printf(TEXT("YardFront_%d"), k));
		}
		FPart P;
		P.S = *S;
		P.TopLeft = FVector2D(float(Rg.R.Min.X), float(Rg.R.Min.Y)) * FV::WorldScale;
		P.SortY = float(Rg.SortY) * FV::WorldScale;
		Cached.Add(P);
	}
	return Cached;
}

FVector2D FVArtYardFront::CottageFeet()
{
	return FVArtYardFrontImpl::Geo().CottageFeet;
}

const TArray<FIntRect>& FVArtYardFront::CottageRects()
{
	return FVArtYardFrontImpl::Geo().CottageRects;
}
