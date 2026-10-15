// UPDATE 6 [ART-HOUSE] — the house inside as a real home: сени, кухня, горница, мастерская (+ station sprites in FVArtHome.cpp) (Docs/UPDATE6_FOREST_CITY.md §6.4, Р37, Р38). Owner: ART-HOUSE.
// Contract: Public/FVSprites.h «UPDATE 6», Private/FVWorldU6.h.
// Style: pixel art like FVSceneArt.cpp / FVArtHome.cpp (Stardew-like shading via FVArt::Shade/Light, 1px dark outline
// on props), deterministic (hash noise with constant seeds). The stations themselves are StationSprite (FVArtHome.cpp).
//
// ==================================================================================================================
// PLAN (texels; world px = texel * FV::WorldScale). The world is 424x300; beyond the walls it is black (Р38).
// ------------------------------------------------------------------------------------------------------------------
//  x 0      8                        248|256                  416   424
//  y 0  ┌── ceiling beam (8) ──────────┬───────────────────────┐
//  y 8  │ ГОРНИЦА: wall face 8..52      │ КУХНЯ: whitewash face, │   горница wall = WallArea (wallpaper decor),
//       │ (wallpaper) + baseboard 52..56│ tiled apron, flue      │   floors of горница / кухня / сени = FloorAreas
//  y 56 │ floor (decor)                 ├ opening y 86..110      │   (the floor decor); мастерская has its own
//  y136 ├─[opening x 16..40]──── middle wall: top 136..144, face 144..176 ───────────┤   rough boards and logs
//  y176 │ СЕНИ (floor decor)  |200..208| МАСТЕРСКАЯ (rough planks)                    │
//       │                     opening y 198..222 (door frame + threshold)             │
//  y252 └──[ENTRANCE x 92..116]── bottom wall 252..264 ───────────────────────────────┘  black 264..300
//  Stations (feet): bed (26,100) · radio on the комод (166,70) · aquarium by window B (216,78) · trophy wall centre
//  (128,30) above the sofa · stove (286,84) with the flue · ice-cellar hatch (296,128) · workbench (326,204) ·
//  chest (24,236) · dresser (64,134). Painted furniture: sofa, kitchen table + benches, base buffet, floor lamp,
//  tool shelf (shovel, axe, pickaxe), pegboard, boards, benches, firewood, mat.
// ==================================================================================================================

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVWorldU6.h"

// The shared update-6 art kit (defined in FVArtForest.cpp).
namespace FVArtKitU6
{
	float R01(int32 X, int32 Y, uint32 S);
	float VNoise(float X, float Y, uint32 S);
	float Bayer(int32 X, int32 Y);
	FColor Rgb(int32 R, int32 G, int32 B);
	FColor WithA(FColor C, int32 A);
	FColor Mul(FColor C, float F);
	void MulPx(FFVImage& I, int32 X, int32 Y, float F);
	void FillEllipse(FFVImage& I, float CX, float CY, float RX, float RY, FColor C, bool bBlend);
	void PaintBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Hi, FColor Base, FColor Lo, FColor Deep, uint32 Seed, float Jag, float Noise);
	void ThickLine(FFVImage& I, FVector2D A, FVector2D B, float W0, float W1, FColor C);
}

namespace FVArtHouseImpl
{
	using namespace FVArtKitU6;

	// ---------------------------------------------------------------------------------------- palette (UPDATE6_ART §3)
	const FColor HouseDark(52, 36, 30), Beam(128, 84, 50), BeamHi(166, 116, 72), BeamLo(92, 58, 36);
	const FColor WallTop(118, 82, 56), WallTopHi(150, 108, 74), WallTopLo(80, 54, 38);
	const FColor PropOut(58, 36, 30);
	const FColor WoodHi(214, 166, 108), Wood(182, 132, 80), WoodMid(150, 104, 62), WoodLo(116, 78, 48), WoodDk(84, 54, 34);
	const FColor DWoodHi(170, 108, 64), DWood(136, 82, 48), DWoodLo(100, 58, 36), DWoodDk(70, 40, 28);
	const FColor WashHi(252, 248, 238), Wash(234, 228, 214), WashLo(198, 190, 178);
	const FColor Tile(70, 110, 190), TileHi(130, 170, 230);
	const FColor LogHi(204, 150, 94), LogC(170, 116, 68), LogLo(128, 84, 48), LogSeam(98, 62, 36), LogEnd(222, 180, 122);
	const FColor IronHi(150, 150, 166), Iron(98, 98, 114), IronLo(66, 64, 80), IronDk(44, 42, 56);
	const FColor MetHi(242, 245, 252), Met(194, 200, 214), MetLo(130, 134, 154);
	const FColor BrassHi(255, 232, 136), Brass(224, 172, 62), BrassLo(160, 110, 36);
	const FColor Fabric(176, 70, 60), FabricHi(214, 108, 90), FabricLo(128, 46, 44);
	const FColor Sawdust(236, 208, 150), CutWood(226, 190, 130);
	const FColor Shadow(24, 12, 24, 90);

	constexpr int32 WorldW = 424, WorldH = 300;
	constexpr int32 FaceTop = 8, FaceBottom = 56, MidTop = 136, MidFace = 144, MidBottom = 176, BottomWall = 252, BottomEnd = 264;
	constexpr int32 VTopX = 248, VBotX = 200;   // the vertical walls (8 thick)

	// ---------------------------------------------------------------------------------------- primitives
	void HLine(FFVImage& I, int32 X0, int32 X1, int32 Y, FColor C) { for (int32 X = X0; X <= X1; ++X) { I.Set(X, Y, C); } }
	void VLine(FFVImage& I, int32 X, int32 Y0, int32 Y1, FColor C) { for (int32 Y = Y0; Y <= Y1; ++Y) { I.Set(X, Y, C); } }
	void Box(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, float Lt, float Dk)
	{
		if (W <= 0 || H <= 0) { return; }
		I.FillRect(X, Y, W, H, Base);
		HLine(I, X, X + W - 1, Y, FVArt::Light(Base, Lt));
		VLine(I, X, Y, Y + H - 1, FVArt::Light(Base, Lt * 0.5f));
		HLine(I, X, X + W - 1, Y + H - 1, FVArt::Shade(Base, Dk));
		VLine(I, X + W - 1, Y + 1, Y + H - 1, FVArt::Shade(Base, Dk * 0.8f));
	}
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
				if (!bVertical && P != PH - 1 && Along == FMath::FloorToInt(R01(Band, 7, Seed) * float(FMath::Max(1, W - 6))) + 3) { C = Seam; }   // board joints
				I.Set(XX, YY, C);
			}
		}
	}
	/** Horizontal logs (3 rows each). */
	void Logs(FFVImage& I, int32 X0, int32 X1, int32 Y0, int32 Y1, float Mulc, uint32 Seed)
	{
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			const int32 R3 = (Y - Y0) % 3;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor C = R3 == 0 ? LogHi : (R3 == 1 ? LogC : LogSeam);
				if (R3 == 1 && R01(X / 3, Y, Seed) < 0.12f) { C = LogLo; }
				I.Set(X, Y, Mul(C, Mulc));
			}
		}
	}
	/** Soft semi-transparent contact shadow (works on the transparent floor holes too). */
	void FloorShadow(FFVImage& I, float CX, float CY, float RX, float RY)
	{
		for (int32 Y = FMath::FloorToInt(CY - RY); Y <= FMath::CeilToInt(CY + RY); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
			{
				const float D = FMath::Square((float(X) + 0.5f - CX) / RX) + FMath::Square((float(Y) + 0.5f - CY) / RY);
				if (D > 1.f || (D > 0.6f && Bayer(X, Y) > (1.f - D) / 0.4f)) { continue; }
				I.Blend(X, Y, Shadow);
			}
		}
	}
	/** Stamps Src with its bottom-centre at (FeetX, FeetY) and a contact shadow. */
	void Put(FFVImage& I, const FFVImage& Src, int32 FeetX, int32 FeetY, float ShadowRX)
	{
		if (ShadowRX > 0.f) { FloorShadow(I, float(FeetX) + 1.f, float(FeetY) - 1.f, ShadowRX, FMath::Max(2.f, ShadowRX * 0.3f)); }
		I.Blit(Src, FeetX - Src.W / 2, FeetY - Src.H, false);
	}
	/** Window on a wall face: frame, sill, mullion cross; the glass (X+2..X+W-3, Y+2..Y+H-3) stays transparent for the sky. */
	void WallWindow(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Frame)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const bool bFrame = XX < X + 2 || XX >= X + W - 2 || YY < Y + 2 || YY >= Y + H - 2;
				const bool bMull = XX == X + W / 2 || YY == Y + H / 2;
				if (bFrame) { I.Set(XX, YY, (XX == X || YY == Y) ? FVArt::Light(Frame, 0.25f) : Frame); }
				else if (bMull) { I.Set(XX, YY, FVArt::Shade(Frame, 0.1f)); }
				else { I.Set(XX, YY, FColor(0, 0, 0, 0)); }
			}
		}
		Box(I, X - 2, Y + H, W + 4, 3, FVArt::Light(Frame, 0.1f), 0.3f, 0.4f);   // the sill
	}

	// ---------------------------------------------------------------------------------------- furniture painted into the house
	FFVImage MakeSofa()
	{
		FFVImage I(44, 22);
		Box(I, 2, 0, 40, 10, Fabric, 0.25f, 0.35f);                         // backrest
		for (int32 X = 4; X < 40; X += 12) { VLine(I, X + 11, 2, 8, FabricLo); }
		Box(I, 2, 10, 40, 8, FabricHi, 0.2f, 0.3f);                         // seat cushions
		VLine(I, 15, 11, 16, Fabric); VLine(I, 28, 11, 16, Fabric);
		Box(I, 0, 4, 5, 15, FabricLo, 0.3f, 0.35f); Box(I, 39, 4, 5, 15, FabricLo, 0.3f, 0.35f);   // armrests
		Box(I, 7, 5, 8, 6, Rgb(236, 212, 150), 0.3f, 0.3f); Box(I, 30, 5, 8, 6, Rgb(120, 160, 200), 0.3f, 0.3f);   // pillows
		HLine(I, 2, 41, 18, DWoodLo); I.Set(3, 19, DWoodDk); I.Set(40, 19, DWoodDk); I.Set(3, 20, DWoodDk); I.Set(40, 20, DWoodDk);
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeKitchenTable()
	{
		// top view: the back bench, the table with a cloth, a samovar and a bread loaf, the front bench
		FFVImage I(44, 40);
		Box(I, 2, 0, 40, 5, WoodMid, 0.3f, 0.4f); VLine(I, 4, 5, 7, WoodLo); VLine(I, 39, 5, 7, WoodLo);
		Box(I, 0, 8, 44, 20, Wood, 0.25f, 0.35f);
		for (int32 Y = 10; Y <= 25; ++Y)
		{
			for (int32 X = 4; X <= 39; ++X)
			{
				FColor C = Rgb(246, 240, 228);
				if ((X - 4) % 8 < 2 || (Y - 10) % 8 < 2) { C = Rgb(214, 80, 70); }
				if (Y == 25 || X == 39) { C = FVArt::Shade(C, 0.15f); }
				I.Set(X, Y, C);
			}
		}
		// samovar
		FillEllipse(I, 30.f, 15.f, 4.f, 4.5f, Brass, false); FillEllipse(I, 29.f, 14.f, 1.6f, 2.f, BrassHi, false);
		HLine(I, 28, 32, 10, BrassLo); I.Set(30, 9, IronDk); I.Set(34, 17, BrassLo); I.Set(35, 17, BrassLo);
		// bread and cups
		FillEllipse(I, 14.f, 18.f, 4.f, 2.6f, Rgb(200, 140, 70), false); HLine(I, 12, 16, 17, Rgb(236, 186, 110));
		I.Set(21, 13, Rgb(250, 250, 250)); I.Set(22, 13, Rgb(210, 210, 220)); I.Set(8, 22, Rgb(250, 250, 250)); I.Set(9, 22, Rgb(210, 210, 220));
		VLine(I, 3, 28, 30, WoodLo); VLine(I, 40, 28, 30, WoodLo);
		Box(I, 2, 32, 40, 5, WoodMid, 0.3f, 0.4f); VLine(I, 4, 37, 39, WoodLo); VLine(I, 39, 37, 39, WoodLo);
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeBuffet()
	{
		FFVImage I(28, 36);
		Box(I, 1, 0, 26, 3, DWoodHi, 0.3f, 0.35f);
		Box(I, 2, 3, 24, 15, DWood, 0.2f, 0.35f);
		for (int32 D = 0; D < 2; ++D)   // glass doors with plates
		{
			const int32 X0 = 4 + D * 11;
			I.FillRect(X0, 5, 9, 11, Rgb(160, 196, 210));
			HLine(I, X0, X0 + 8, 10, DWoodLo);
			FillEllipse(I, float(X0) + 4.5f, 7.5f, 2.6f, 1.6f, Rgb(250, 250, 244), false); I.Set(X0 + 4, 7, Tile);
			I.Set(X0 + 2, 13, Rgb(236, 128, 60)); I.Set(X0 + 3, 13, Rgb(236, 128, 60)); I.Set(X0 + 6, 13, Rgb(250, 250, 244)); I.Set(X0 + 7, 12, Rgb(250, 250, 244));
			I.Set(X0 + 1, 6, Rgb(236, 246, 250));
		}
		Box(I, 1, 18, 26, 2, DWoodHi, 0.3f, 0.3f);
		Box(I, 2, 20, 24, 14, DWood, 0.2f, 0.35f);
		for (int32 DY : { 22, 28 }) { Box(I, 4, DY, 20, 5, DWoodHi, 0.2f, 0.3f); I.Set(13, DY + 2, BrassHi); I.Set(14, DY + 2, Brass); }
		VLine(I, 3, 34, 35, DWoodDk); VLine(I, 24, 34, 35, DWoodDk);
		I.Outline(PropOut);
		return I;
	}
	/** Floor lamp (торшер) by the bookcase: a round foot, a turned pole and a pleated cream shade with a warm glow under it. */
	FFVImage MakeCoatRack()
	{
		FFVImage I(18, 40);
		// foot
		FillEllipse(I, 8.5f, 37.5f, 5.f, 1.6f, DWood, false);
		HLine(I, 5, 12, 36, DWoodHi);
		// pole with two turned rings
		VLine(I, 8, 14, 36, DWood); VLine(I, 9, 14, 36, DWoodLo);
		for (int32 RY : { 22, 30 }) { HLine(I, 7, 10, RY, DWoodHi); }
		// the warm glow under the shade
		for (int32 Y = 13; Y <= 16; ++Y)
		{
			const int32 HW = 3 + (Y - 13);
			for (int32 X = 8 - HW; X <= 9 + HW; ++X) { I.Blend(X, Y, FColor(255, 226, 130, uint8(150 - (Y - 13) * 35))); }
		}
		I.Set(8, 13, Rgb(255, 250, 210)); I.Set(9, 13, Rgb(255, 236, 160));
		// shade: a trapezoid, pleats, a brown trim along the bottom and a top rim
		for (int32 Y = 2; Y <= 12; ++Y)
		{
			const int32 L = 5 - (Y - 2) * 3 / 10 - (Y >= 7 ? 1 : 0), R = 12 + (Y - 2) * 3 / 10 + (Y >= 7 ? 1 : 0);
			for (int32 X = L; X <= R; ++X)
			{
				FColor C = Rgb(246, 226, 168);
				if ((X - L) % 3 == 2) { C = Rgb(222, 196, 132); }
				if (X == L) { C = Rgb(206, 178, 116); }
				if (X == R - 1 || X == R - 2) { C = Rgb(255, 242, 200); }
				I.Set(X, Y, C);
			}
		}
		HLine(I, 5, 12, 2, Rgb(200, 150, 80));
		HLine(I, 1, 16, 12, Rgb(176, 108, 50)); HLine(I, 2, 15, 11, Rgb(206, 140, 70));
		I.Outline(PropOut);
		return I;
	}
	/** Wall shelf with the tools (shovel, axe, pickaxe) — seen on the wall face of the сени. */
	FFVImage MakeToolShelf()
	{
		FFVImage I(34, 28);
		Box(I, 0, 0, 34, 3, WoodMid, 0.3f, 0.4f);
		for (int32 PX : { 4, 29 }) { VLine(I, PX, 3, 26, WoodLo); }
		Box(I, 0, 25, 34, 3, WoodMid, 0.3f, 0.4f);
		// shovel
		VLine(I, 8, 4, 16, Wood); I.FillRect(6, 16, 5, 7, Met); HLine(I, 6, 10, 16, MetHi); I.Set(10, 22, MetLo); HLine(I, 7, 9, 4, WoodLo);
		// axe
		VLine(I, 17, 5, 23, WoodMid); I.FillRect(18, 5, 4, 5, Iron); I.Set(21, 5, IronHi); I.Set(21, 9, IronHi); I.Set(18, 9, IronLo);
		// pickaxe
		VLine(I, 26, 7, 23, WoodMid); HLine(I, 22, 30, 6, Iron); I.Set(22, 7, IronLo); I.Set(30, 7, IronLo); I.Set(26, 5, IronHi);
		I.Outline(PropOut);
		return I;
	}
	/** Pegboard with tools on the workshop wall: a saw, a hammer, pliers, a square, a rope coil. */
	FFVImage MakePegboard()
	{
		FFVImage I(54, 26);
		Box(I, 0, 0, 54, 26, Rgb(170, 130, 84), 0.2f, 0.35f);
		for (int32 Y = 3; Y < 24; Y += 4) { for (int32 X = 3; X < 52; X += 4) { I.Set(X, Y, Rgb(120, 88, 56)); } }
		// saw
		I.FillRect(4, 6, 16, 5, Met); for (int32 X = 4; X < 20; X += 2) { I.Set(X, 11, MetLo); } I.FillRect(18, 5, 5, 6, Rgb(170, 70, 50));
		// hammer
		VLine(I, 27, 6, 20, WoodMid); I.FillRect(24, 4, 7, 3, Iron); I.Set(24, 4, IronHi);
		// pliers
		I.Line(34, 5, 37, 14, IronLo); I.Line(38, 5, 35, 14, Iron); I.Set(34, 15, Rgb(200, 60, 50)); I.Set(38, 15, Rgb(200, 60, 50));
		// square ruler
		VLine(I, 43, 4, 18, Brass); HLine(I, 43, 50, 18, Brass);
		// rope coil
		FillEllipse(I, 12.f, 19.f, 5.f, 3.f, Rgb(214, 186, 120), false); FillEllipse(I, 12.f, 19.f, 2.5f, 1.4f, Rgb(170, 130, 84), false);
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeBootBench()
	{
		FFVImage I(36, 14);
		Box(I, 0, 2, 36, 4, WoodMid, 0.3f, 0.4f); VLine(I, 3, 6, 12, WoodLo); VLine(I, 32, 6, 12, WoodLo);
		// felt boots and rubber boots under it
		Box(I, 8, 8, 4, 6, Rgb(90, 90, 100), 0.3f, 0.3f); Box(I, 13, 8, 4, 6, Rgb(90, 90, 100), 0.3f, 0.3f);
		Box(I, 20, 7, 4, 7, Rgb(60, 110, 70), 0.3f, 0.3f); Box(I, 25, 7, 4, 7, Rgb(60, 110, 70), 0.3f, 0.3f);
		// a bucket on the bench
		Box(I, 26, 0, 7, 3, Met, 0.3f, 0.3f);
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeFirewood()
	{
		FFVImage I(20, 12);
		for (int32 r = 0; r < 2; ++r)
		{
			for (int32 c = 0; c < 4 - r; ++c)
			{
				const float X = 3.5f + float(c) * 4.6f + float(r) * 2.3f, Y = 9.f - float(r) * 4.f;
				FillEllipse(I, X, Y, 2.4f, 2.2f, WoodLo, false); FillEllipse(I, X - 0.3f, Y - 0.3f, 1.5f, 1.4f, CutWood, false);
			}
		}
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeBoards()
	{
		FFVImage I(30, 12);
		for (int32 b = 0; b < 4; ++b)
		{
			const int32 Y = 9 - b * 2;
			for (int32 X = 1 + (b % 2); X <= 27 - ((b + 1) % 2); ++X) { I.Set(X, Y, CutWood); I.Set(X, Y + 1, Rgb(196, 150, 96)); }
		}
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeChoppingBlock()
	{
		FFVImage I(14, 18);
		Box(I, 2, 8, 10, 9, WoodLo, 0.3f, 0.3f);
		FillEllipse(I, 7.f, 8.f, 5.f, 2.2f, CutWood, false); I.Set(7, 8, Rgb(196, 150, 96));
		I.Line(8, 7, 11, 0, WoodMid); I.FillRect(5, 5, 4, 3, Iron); I.Set(5, 5, IronHi);
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeLantern()
	{
		FFVImage I(7, 10);
		I.Set(3, 0, IronDk); HLine(I, 1, 5, 1, IronDk);
		I.FillRect(1, 2, 5, 6, Rgb(255, 214, 120)); I.Set(2, 3, Rgb(255, 246, 200));
		VLine(I, 1, 2, 7, IronDk); VLine(I, 5, 2, 7, IronDk); HLine(I, 1, 5, 8, IronDk);
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeMat()
	{
		FFVImage I(26, 8);
		for (int32 Y = 0; Y < 8; ++Y)
		{
			for (int32 X = 0; X < 26; ++X)
			{
				const int32 Band = (X / 3) % 4;
				FColor C = Band == 0 ? Rgb(200, 70, 60) : (Band == 1 ? Rgb(236, 204, 120) : (Band == 2 ? Rgb(80, 130, 180) : Rgb(120, 170, 90)));
				if (Y == 0 || Y == 7) { C = FVArt::Shade(C, 0.25f); }
				I.Set(X, Y, C);
			}
		}
		return I;
	}
	/** Hanging bunch of onions / herbs (from the beam). */
	void HangingBunch(FFVImage& I, int32 X, int32 Y, bool bOnion)
	{
		VLine(I, X, Y, Y + 5, Rgb(170, 130, 84));
		for (int32 k = 0; k < 5; ++k)
		{
			const int32 BX = X - 2 + k, BY = Y + 6 + (k % 2) * 2;
			if (bOnion) { FillEllipse(I, float(BX) + 0.5f, float(BY), 1.4f, 1.6f, k % 2 ? Rgb(230, 170, 80) : Rgb(200, 130, 60), false); }
			else { VLine(I, BX, BY - 1, BY + 3, k % 2 ? Rgb(96, 150, 70) : Rgb(130, 180, 90)); }
		}
	}

	// ---------------------------------------------------------------------------------------- world helpers
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
	/** Top of a wall seen from above: a strip with a lit top edge and a dark bottom edge. */
	void WallStrip(FFVImage& I, int32 X, int32 Y, int32 W, int32 H)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				FColor C = WallTop;
				if (YY == Y || XX == X) { C = WallTopHi; }
				else if (YY == Y + H - 1 || XX == X + W - 1) { C = WallTopLo; }
				else if (R01(XX / 2, YY / 2, 501u) < 0.1f) { C = FVArt::Shade(WallTop, 0.12f); }
				I.Set(XX, YY, C);
			}
		}
	}
	/** Floorboards of a passage (threshold) in an opening. */
	void Passage(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, bool bVerticalBoards)
	{
		Planks(I, X, Y, W, H, Rgb(176, 124, 76), 4, 503u, bVerticalBoards);
	}
}

// =====================================================================================================================
// FVWorldU6 entry point
// =====================================================================================================================
void FVWorldU6::BuildHouse(FFVImage& OutBg, FWorldDef& OutDef)
{
	using namespace FVArtHouseImpl;
	FFVImage& I = OutBg;
	I.Init(WorldW, WorldH, FColor(0, 0, 0, 255));   // beyond the walls: black (Р38)
	const FColor Clear(0, 0, 0, 0);

	// ---- 1) the holes for the decor: the горница wallpaper, the floors of горница / кухня / сени
	I.FillRect(8, FaceTop, VTopX - 8, FaceBottom - 4 - FaceTop, Clear);                 // горница wall (wallpaper)
	I.FillRect(8, FaceBottom, VTopX - 8, MidTop - FaceBottom, Clear);                   // горница floor
	I.FillRect(VTopX + 8, FaceBottom, WorldW - 8 - VTopX - 8, MidTop - FaceBottom, Clear);   // кухня floor
	I.FillRect(8, MidBottom, VBotX - 8, BottomWall - MidBottom, Clear);                  // сени floor

	// ---- 2) the кухня wall: whitewash, the tiled apron and the whitewashed flue of the stove
	for (int32 Y = FaceTop; Y < FaceBottom; ++Y)
	{
		for (int32 X = VTopX + 8; X < WorldW - 8; ++X)
		{
			FColor C = R01(X, Y, 511u) < 0.08f ? WashLo : (R01(X, Y, 512u) < 0.1f ? WashHi : Wash);
			if (Y >= FaceBottom - 10) { C = FVArt::Mix(C, WashLo, float(Y - (FaceBottom - 10)) / 14.f); }
			I.Set(X, Y, C);
		}
	}
	for (int32 Y = 30; Y < FaceBottom - 4; ++Y)
	{
		for (int32 X = 262; X <= 312; ++X)
		{
			const int32 TX = (X - 262) % 5, TY = (Y - 30) % 5;
			FColor C = (TX == 4 || TY == 4) ? WashLo : (((X - 262) / 5 + (Y - 30) / 5) % 2 == 0 ? Tile : Rgb(244, 244, 250));
			if (TX == 2 && TY == 2 && C == Tile) { C = TileHi; }
			I.Set(X, Y, C);
		}
	}
	for (int32 Y = FaceTop; Y < 44; ++Y)
	{
		for (int32 X = 276; X <= 296; ++X)
		{
			FColor C = (X == 276) ? WashHi : (X == 296 ? WashLo : Wash);
			if ((Y % 4 == 0) || ((X - 276 + (Y / 4) * 3) % 7 == 0)) { C = FVArt::Mix(C, WashLo, 0.5f); }
			I.Set(X, Y, C);
		}
	}
	// a shelf with чугунки, bunches of onions and herbs, the painting spot stays free
	Box(I, 300, 13, 30, 3, WoodMid, 0.3f, 0.4f);
	FillEllipse(I, 306.f, 10.f, 4.f, 3.f, IronLo, false); HLine(I, 302, 310, 7, Iron);
	FillEllipse(I, 317.f, 10.5f, 3.f, 2.5f, IronDk, false); HLine(I, 314, 320, 8, IronLo);
	FillEllipse(I, 325.f, 11.f, 2.4f, 2.f, Rgb(196, 104, 64), false);
	HangingBunch(I, 380, FaceTop, true);
	HangingBunch(I, 374, FaceTop, false);
	HLine(I, VTopX + 8, WorldW - 9, FaceBottom - 4, DWoodHi);   // baseboard
	I.FillRect(VTopX + 8, FaceBottom - 3, WorldW - 16 - VTopX, 3, DWood);
	WallWindow(I, 344, 16, 24, 24, Rgb(236, 232, 220));

	// ---- 3) the горница wall: baseboard, windows (the wallpaper between them is decor), a small shelf
	HLine(I, 8, VTopX - 1, FaceBottom - 4, DWoodHi);
	I.FillRect(8, FaceBottom - 3, VTopX - 8, 3, DWood);
	WallWindow(I, 16, 16, 24, 24, WoodMid);
	WallWindow(I, 204, 16, 24, 24, WoodMid);
	Box(I, 46, 12, 32, 3, WoodMid, 0.3f, 0.4f);
	FillEllipse(I, 52.f, 9.5f, 2.2f, 2.5f, Rgb(80, 130, 180), false);    // jars on the shelf
	FillEllipse(I, 60.f, 10.f, 2.f, 2.f, Rgb(214, 170, 90), false);
	I.FillRect(66, 6, 3, 6, Rgb(170, 70, 60)); I.FillRect(69, 7, 3, 5, Rgb(60, 110, 160)); I.FillRect(72, 6, 3, 6, Rgb(90, 140, 80));   // books

	// ---- 4) the ceiling beam
	for (int32 Y = 0; Y < FaceTop; ++Y)
	{
		for (int32 X = 0; X < WorldW; ++X)
		{
			FColor C = Y == FaceTop - 1 ? BeamLo : (Y == 0 ? BeamHi : Beam);
			if (Y > 0 && Y < FaceTop - 1 && R01(X / 4, Y, 521u) < 0.12f) { C = FVArt::Shade(Beam, 0.15f); }
			I.Set(X, Y, C);
		}
	}
	for (int32 X = 30; X < WorldW; X += 60) { I.FillRect(X, 1, 6, FaceTop - 2, BeamLo); I.FillRect(X + 1, 2, 4, FaceTop - 4, FVArt::Light(Beam, 0.15f)); }   // cross beams

	// ---- 5) walls seen from above: outer sides, the vertical walls, the middle wall and the bottom wall
	WallStrip(I, 0, 0, 8, BottomEnd);
	WallStrip(I, WorldW - 8, 0, 8, BottomEnd);
	WallStrip(I, VTopX, FaceTop, 8, MidTop - FaceTop);
	WallStrip(I, 0, MidTop, WorldW, MidFace - MidTop);
	WallStrip(I, VBotX, MidFace, 8, BottomWall - MidFace);
	WallStrip(I, 0, BottomWall, WorldW, BottomEnd - BottomWall);
	// the faces of the bottom rooms: сени (logs), мастерская (rough darker logs)
	Logs(I, 8, VBotX - 1, MidFace, MidBottom - 1, 1.f, 531u);
	Logs(I, VBotX + 8, WorldW - 9, MidFace, MidBottom - 1, 0.84f, 533u);
	HLine(I, 8, WorldW - 9, MidBottom - 1, LogSeam);
	WallWindow(I, 232, 150, 20, 18, WoodLo);
	// openings: горница -> кухня (y 86..110), горница -> сени (x 16..40), сени -> мастерская (y 198..222)
	Passage(I, VTopX, 86, 8, 24, true);
	Passage(I, 16, MidTop, 24, MidBottom - MidTop, false);
	for (int32 Y = MidTop; Y < MidBottom; ++Y) { I.Set(15, Y, WallTopLo); I.Set(40, Y, WallTopLo); }
	for (int32 Y = MidFace; Y < MidBottom; ++Y) { I.Set(14, Y, DWood); I.Set(41, Y, DWood); }   // door jambs
	Passage(I, VBotX, 198, 8, 24, true);
	I.FillRect(VBotX - 1, 196, 10, 2, DWood); I.FillRect(VBotX - 1, 222, 10, 2, DWood);
	HLine(I, VBotX, VBotX + 7, 221, WoodHi);   // threshold
	// the entrance door in the bottom wall (x 92..116): threshold and a lit doorway
	Passage(I, 92, BottomWall, 24, BottomEnd - BottomWall, true);
	I.FillRect(90, BottomWall, 2, BottomEnd - BottomWall, DWood); I.FillRect(116, BottomWall, 2, BottomEnd - BottomWall, DWood);
	HLine(I, 92, 115, BottomEnd - 1, Rgb(255, 236, 170));

	// ---- 6) the мастерская floor: rough planks and sawdust
	Planks(I, VBotX + 8, MidBottom, WorldW - 16 - VBotX, BottomWall - MidBottom, Rgb(150, 110, 70), 5, 541u, false);
	for (int32 k = 0; k < 160; ++k)
	{
		const float A = R01(k, 1, 543u) * 2.f * UE_PI, Rr = FMath::Sqrt(R01(k, 2, 543u)) * 30.f;
		I.Set(326 + FMath::RoundToInt(FMath::Cos(A) * Rr), 214 + FMath::RoundToInt(FMath::Sin(A) * Rr * 0.5f), R01(k, 3, 543u) < 0.5f ? Sawdust : CutWood);
	}

	// ---- 7) soft shade on the floors along the walls (ambient occlusion)
	for (int32 X = 8; X < WorldW - 8; ++X)
	{
		for (int32 D = 0; D < 4; ++D)
		{
			const FColor S(24, 12, 24, uint8(70 - D * 16));
			if (X < VTopX || X >= VTopX + 8) { I.Blend(X, FaceBottom + D, S); }
			if (X < VBotX || X >= VBotX + 8) { I.Blend(X, MidBottom + D, S); }
		}
	}

	// ---- 8) painted furniture (walls first, then by feet)
	Put(I, MakeToolShelf(), 146, 174, 0.f);
	Put(I, MakePegboard(), 326, 172, 0.f);
	Put(I, MakeLantern(), 284, 160, 0.f);
	Put(I, MakeLantern(), 66, 156, 0.f);
	Put(I, MakeSofa(), 128, 80, 22.f);
	Put(I, MakeBuffet(), 398, 72, 14.f);
	Put(I, MakeKitchenTable(), 356, 118, 22.f);
	{
		// a water bucket and a poker by the stove
		FFVImage B(10, 12);
		Box(B, 1, 3, 8, 8, Met, 0.3f, 0.35f); HLine(B, 2, 7, 3, Rgb(96, 176, 190)); B.Line(1, 3, 5, 0, IronLo); B.Line(5, 0, 8, 3, IronLo);
		B.Outline(PropOut);
		Put(I, B, 318, 88, 5.f);
	}
	Put(I, MakeCoatRack(), 56, 184, 6.f);
	Put(I, MakeFirewood(), 156, 188, 10.f);
	Put(I, MakeBootBench(), 150, 250, 18.f);
	Put(I, MakeMat(), 104, 251, 0.f);
	Put(I, MakeBoards(), 392, 246, 15.f);
	Put(I, MakeChoppingBlock(), 236, 240, 6.f);

	// ---- the world description (world px)
	OutDef = FWorldDef();
	OutDef.LocationId = FVDB::HouseId();
	OutDef.SizeTex = FIntPoint(WorldW, WorldH);
	OutDef.bWater = false;
	OutDef.bDust = true;
	OutDef.WallAreas.Add(WR(8.f, float(FaceTop), float(VTopX - 8), float(FaceBottom - 4 - FaceTop)));
	OutDef.FloorAreas.Add(WR(8.f, float(FaceBottom), float(VTopX - 8), float(MidTop - FaceBottom)));
	OutDef.FloorAreas.Add(WR(float(VTopX + 8), float(FaceBottom), float(WorldW - 16 - VTopX), float(MidTop - FaceBottom)));
	OutDef.FloorAreas.Add(WR(8.f, float(MidBottom), float(VBotX - 8), float(BottomWall - MidBottom)));
	OutDef.Windows.Add(WR(18.f, 18.f, 20.f, 20.f));
	OutDef.Windows.Add(WR(206.f, 18.f, 20.f, 20.f));
	OutDef.Windows.Add(WR(346.f, 18.f, 20.f, 20.f));
	OutDef.Windows.Add(WR(234.f, 152.f, 16.f, 14.f));
	// walking: the four rooms and the three openings, the entrance
	OutDef.Walk.Add(WR(10.f, 58.f, float(VTopX - 12), 76.f));                        // горница
	OutDef.Walk.Add(WR(float(VTopX + 10), 58.f, float(WorldW - VTopX - 20), 76.f));   // кухня
	OutDef.Walk.Add(WR(float(VTopX - 4), 88.f, 16.f, 20.f));                         // горница <-> кухня
	OutDef.Walk.Add(WR(18.f, 130.f, 20.f, 52.f));                                     // горница <-> сени
	OutDef.Walk.Add(WR(10.f, 178.f, float(VBotX - 12), 72.f));                        // сени
	OutDef.Walk.Add(WR(float(VBotX + 10), 178.f, float(WorldW - VBotX - 20), 72.f));  // мастерская
	OutDef.Walk.Add(WR(float(VBotX - 4), 200.f, 16.f, 20.f));                        // сени <-> мастерская
	OutDef.Walk.Add(WR(94.f, 246.f, 20.f, 14.f));                                     // the entrance
	OutDef.Blockers.Add(WR(106.f, 58.f, 44.f, 22.f));    // sofa
	OutDef.Blockers.Add(WR(334.f, 78.f, 44.f, 40.f));    // kitchen table + benches
	OutDef.Blockers.Add(WR(384.f, 58.f, 28.f, 14.f));    // buffet
	OutDef.Blockers.Add(WR(10.f, 58.f, 32.f, 42.f));     // bed
	OutDef.Blockers.Add(WR(264.f, 58.f, 44.f, 26.f));    // stove
	OutDef.Blockers.Add(WR(48.f, 178.f, 16.f, 6.f));     // floor lamp
	OutDef.Blockers.Add(WR(146.f, 178.f, 22.f, 10.f));   // firewood
	OutDef.Blockers.Add(WR(132.f, 240.f, 36.f, 10.f));   // boot bench
	OutDef.Blockers.Add(WR(376.f, 236.f, 32.f, 12.f));   // boards
	OutDef.Blockers.Add(WR(302.f, 178.f, 48.f, 26.f));   // workbench

	using FVWorldU6::EAnchor;
	const FFVRect NoHit;
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("default")), 104.f, 238.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("door")), 104.f, 238.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Spawn, FName(TEXT("from_yard")), 104.f, 238.f, 0, NoHit);
	AddAnchor(OutDef, EAnchor::Door, FName(TEXT("exit_door")), 104.f, 248.f, 0, WR(92.f, 238.f, 24.f, 26.f));
	// stations (the dresser with the decor catalog is a Station anchor "dresser")
	auto Station = [&OutDef](EFVStation S, float X, float Y) { AddAnchor(OutDef, EAnchor::Station, FVDB::StationId(S), X, Y, 0, FFVRect()); };
	Station(EFVStation::Bed, 26.f, 100.f);
	Station(EFVStation::Radio, 166.f, 70.f);
	Station(EFVStation::Aquarium, 216.f, 78.f);
	Station(EFVStation::TrophyWall, 128.f, 30.f);        // pivot = centre (StationSprite table)
	Station(EFVStation::Kitchen, 286.f, 84.f);
	Station(EFVStation::IceCellar, 296.f, 128.f);
	Station(EFVStation::Workshop, 326.f, 204.f);
	Station(EFVStation::Chest, 24.f, 238.f);
	// 0.6 fix: the dresser stood on the top of the stairs горница <-> сени (x 18..38) and its blocker closed the passage:
	// the горница and the кухня were unreachable from the entrance. It stands right of the stairs now.
	AddAnchor(OutDef, EAnchor::Station, FName(TEXT("dresser")), 64.f, 134.f, 0, NoHit);
	// decor slots: the classic room decor + the expensive furniture (ART-DECOR sprites, pivot bottom-centre)
	auto Decor = [&OutDef](EFVDecorSlot S, float X, float Y) { AddAnchor(OutDef, EAnchor::DecorSlot, NAME_None, X, Y, int32(S), FFVRect()); };
	Decor(EFVDecorSlot::Rug, 128.f, 110.f);
	Decor(EFVDecorSlot::Lamp, 96.f, 78.f);
	Decor(EFVDecorSlot::Plant, 240.f, 134.f);
	Decor(EFVDecorSlot::Painting, 326.f, 32.f);
	Decor(EFVDecorSlot::Curtains, 28.f, 44.f);
	Decor(EFVDecorSlot::Curtains, 216.f, 44.f);
	Decor(EFVDecorSlot::Fireplace, 62.f, 72.f);
	Decor(EFVDecorSlot::Chandelier, 128.f, 2.f);        // pivot top-centre: hangs from the ceiling beam
	Decor(EFVDecorSlot::BedSkin, 26.f, 100.f);
	Decor(EFVDecorSlot::Cupboard, 398.f, 72.f);
	Decor(EFVDecorSlot::Bookcase, 86.f, 182.f);
	Decor(EFVDecorSlot::Clock, 114.f, 182.f);
	Decor(EFVDecorSlot::StoveSkin, 182.f, 194.f);
	Decor(EFVDecorSlot::Statue, 386.f, 172.f);
	// lights: the stove mouth, the lanterns of the сени and the workshop, the windows (daylight, dim at night)
	AddLight(OutDef, 281.f, 70.f, 40.f, FColor(255, 170, 80), true);
	AddLight(OutDef, 284.f, 156.f, 44.f, FColor(255, 206, 130), true);
	AddLight(OutDef, 66.f, 152.f, 40.f, FColor(255, 206, 130), true);
	AddLight(OutDef, 28.f, 30.f, 30.f, FColor(200, 220, 255), false);
	AddLight(OutDef, 216.f, 30.f, 30.f, FColor(200, 220, 255), false);
	AddLight(OutDef, 356.f, 30.f, 30.f, FColor(200, 220, 255), false);
}
