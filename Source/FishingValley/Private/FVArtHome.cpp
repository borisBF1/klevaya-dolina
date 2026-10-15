// House & world art of update 2 («Дом, ремёсла и торговля»): house stations by level (level 0 = construction
// site), trader stalls, the order board, net posts / nets in the water, dig spots, house decor (props, 16x16
// tileable wallpaper & floor, catalog icons), trophy plaques, the dresser, and the labeled update-2 contact sheet.
// Everything is procedural (primitives + a few ASCII templates), generated once and cached by UFVSprites.
//
// ==================================================================================================================
// SIZE / PIVOT TABLE (texels; world sprites are drawn at FV::WorldScale = 2). Pivot = bottom-center unless noted.
// ------------------------------------------------------------------------------------------------------------------
//  StationSprite(S, L)   level 0 = construction site: same width as level 1, height clamp(H, 20, 28)
//                        (TrophyWall L0 = chalk frame + pinned note on the wall, 48x24)
//                        Update 6 (§6.4, §9б): drawn for the lived-in house (FVArtHouse.cpp places them); levels above 3
//                        are the expert upgrades. Every level keeps the pivot (bottom-centre; TrophyWall = centre).
//    Kitchen     L1 Очаг 32x32 · L2 Плита 32x32 · L3 Печь 32x32 · L4 Русская печь 44x44 (лежанка, заслонка, ухват,
//                two hobs with чугунки)                                  fire point: StationEffectPoint
//                (the flue into the ceiling is painted on the kitchen wall by FVArtHouse.cpp)
//    Workshop    L1 Стол 40x28 · L2 Верстак 40x28 · L3 Станок 40x28 · L4 Мастерская мастера 48x30 (vise, grindstone,
//                hard rock, blueprints)                                   lamp: StationEffectPoint (L3, L4)
//    Smokehouse  L1 26x40 · L2 26x40 · L3 30x44 (stone, hard-rock firebox, 12 hooks)   chimney top: StationEffectPoint
//    WormFarm    L1 28x18 · L2 28x18 · L3 32x20 (stone trough, glowing deep worms)
//    Aquarium    L1 Банка 24x24 · L2 32x28 · L3 44x30 · L4 Океанариум 56x36 (corals, hard-rock stand)
//                                                                         water rect: AquariumWaterRect(L)
//    IceCellar   floor hatch (flat, top view): L1 24x18 planks + ring · L2 24x18 iron bands + frost ·
//                L3 28x22 fieldstone rim + blue light from the seams
//    Radio       L1 24x26 radio on a комод · L2 Радиостанция 28x28 (receiver, Morse key, antenna, map of the
//                City on the wall)                                        dial glow: StationEffectPoint
//    Bed         L1 28x40 top view, headboard up (stands with the headboard against the back wall)
//    TrophyWall  L1 48x24 plank (6 slots, 3x2) · L2 96x24 (12 slots, 6x2: slots 0..5 = the L1 layout in the left
//                half, 6..11 = the same in the right half). Plaque centers: TrophySlotCenter(i); a plaque drawn at 1
//                virtual px per texel exactly fills one 16x12-texel slot of the 2x wall
//    Chest       L1 20x16
//  StallSprite(trader)   56x48 (awning, sign, back panel, the trader, counter with goods); unknown ids -> Mitrich
//  OrderBoardSprite(n)   32x36 (n = 0..3 pinned notes)
//  NetPostSprite(b)      28x20 (two stakes + rope; with a net: draped net + lines down to the bottom edge)
//  NetWaterSprite(T,F,t) Tier 1: 28x14 · 2: 36x16 · 3: 44x18 — pivot TOP-center (float line at y = 2)
//  DigSpotSprite(b)      22x10 (flat, no outline)
//  DecorWorld(def)       Rug 40x14 (fish rug 44x14) · Lamp: candle 12x18, kerosene 14x26, float 12x30, jelly 16x28
//                        (flame / glow: DecorLightPoint) · Plant: ficus 16x28, cactus 12x20, fern 20x20, bonsai
//                        22x18 · Painting: pond 24x18, map 26x18, portrait 18x22, goldfish 20x18 (wall) ·
//                        Curtains 36x30 (rod on top, transparent middle for the window) ·
//                        Wallpaper / Floor: 16x16 seamless tile, pivot TOP-LEFT (tile it)
//  TrophyPlaque(fish)    30x22, pivot CENTER (big fish — the legends — are scaled down to at most 26x13 on the 24x13 panel)
//  DresserSprite()       24x26
//  DecorIcon / ItemIcon / ItemArt / LureIcon / ShovelIcon: 16x16 UI icons (see FVArtItems.cpp)
// ==================================================================================================================

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVArtU6.h"           // update 6: yard decor & furniture (ART-DECOR)
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Update 5 builders that live in FVArtItems.cpp (named namespace there, so they link across files).
namespace FVArtItemsImpl
{
	FFVImage BuildFishInterior(const FFVFishDef& Def);   // the fish without its outline ring (the plaque downscales it)
	FFVImage BuildFishTokenIcon(EFVRarity Rarity);       // any_fish_rare / _epic / _legend icon
}

namespace FVArtHomeImpl
{
	// ---------------------------------------------------------------------------------------- palette
	const FColor PropOut(58, 36, 30);
	const FColor WoodHi(214, 166, 108), Wood(182, 132, 80), WoodMid(150, 104, 62), WoodLo(116, 78, 48), WoodDk(84, 54, 34);
	const FColor DWoodHi(170, 108, 64), DWood(136, 82, 48), DWoodLo(100, 58, 36), DWoodDk(70, 40, 28);
	const FColor StoneHi(204, 200, 190), Stone(164, 160, 152), StoneLo(124, 120, 116), StoneDk(86, 82, 88);
	const FColor BrickHi(214, 122, 86), Brick(178, 88, 62), BrickLo(130, 60, 48), Mortar(206, 190, 164);
	const FColor WashHi(252, 248, 238), Wash(234, 228, 214), WashLo(198, 190, 178);
	const FColor IronHi(150, 150, 166), Iron(98, 98, 114), IronLo(66, 64, 80), IronDk(44, 42, 56);
	const FColor MetHi(242, 245, 252), Met(194, 200, 214), MetLo(130, 134, 154);
	const FColor BrassHi(255, 232, 136), Brass(224, 172, 62), BrassLo(160, 110, 36);
	const FColor FireY(255, 242, 150), FireO(255, 170, 50), FireR(228, 86, 32), Ember(150, 44, 30), FireBack(44, 26, 26);
	const FColor SoilHi(150, 108, 70), Soil(112, 76, 50), SoilLo(84, 56, 40), SoilDk(58, 38, 30);
	const FColor Paper(250, 238, 204), PaperLo(222, 200, 156), Scribble(140, 116, 104);
	const FColor LeafHi(150, 214, 92), Leaf(84, 168, 64), LeafLo(44, 110, 60), LeafDk(30, 78, 50);
	const FColor Terra(196, 104, 64), TerraHi(228, 146, 96), TerraLo(142, 70, 46);
	const FColor Chalk(240, 236, 222, 210);
	const FColor Worm(236, 128, 142), WormLo(178, 74, 104);

	FORCEINLINE FColor Rgb(uint8 R, uint8 G, uint8 B, uint8 A = 255) { return FColor(R, G, B, A); }

	// ---------------------------------------------------------------------------------------- primitives
	struct FPalE { TCHAR Ch; FColor Col; };
	FFVImage Ascii(std::initializer_list<const TCHAR*> Rows, std::initializer_list<FPalE> Pal)
	{
		TMap<TCHAR, FColor> P;
		for (const FPalE& E : Pal) { P.Add(E.Ch, E.Col); }
		return FFVImage::FromAscii(Rows, P);
	}

	void HLine(FFVImage& I, int32 X0, int32 X1, int32 Y, FColor C) { for (int32 X = X0; X <= X1; ++X) { I.Set(X, Y, C); } }
	void VLine(FFVImage& I, int32 X, int32 Y0, int32 Y1, FColor C) { for (int32 Y = Y0; Y <= Y1; ++Y) { I.Set(X, Y, C); } }
	void Rect(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor C) { I.FillRect(X, Y, W, H, C); }
	void SetIfEmpty(FFVImage& I, int32 X, int32 Y, FColor C) { if (I.InBounds(X, Y) && I.Get(X, Y).A == 0) { I.Set(X, Y, C); } }
	void SetIfFilled(FFVImage& I, int32 X, int32 Y, FColor C) { if (I.InBounds(X, Y) && I.Get(X, Y).A != 0) { I.Set(X, Y, C); } }

	/** Filled box with a bevel: lit top row / left column, shaded bottom row / right column. */
	void Box(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, float Lt = 0.3f, float Dk = 0.35f)
	{
		if (W <= 0 || H <= 0) { return; }
		Rect(I, X, Y, W, H, Base);
		HLine(I, X, X + W - 1, Y, FVArt::Light(Base, Lt));
		VLine(I, X, Y, Y + H - 1, FVArt::Light(Base, Lt * 0.5f));
		HLine(I, X, X + W - 1, Y + H - 1, FVArt::Shade(Base, Dk));
		VLine(I, X + W - 1, Y + 1, Y + H - 1, FVArt::Shade(Base, Dk * 0.7f));
	}

	/** Wooden planks (horizontal bands of height PH, or vertical boards of width PH) with seams, grain and joints. */
	void Planks(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, int32 PH, uint32 Seed, bool bVertical = false)
	{
		PH = FMath::Max(2, PH);
		const FColor Hi = FVArt::Light(Base, 0.18f), Seam = FVArt::Shade(Base, 0.42f), Grain = FVArt::Shade(Base, 0.14f);
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const int32 Along = bVertical ? (YY - Y) : (XX - X);
				const int32 Across = bVertical ? (XX - X) : (YY - Y);
				const int32 Band = Across / PH, P = Across % PH;
				FColor C = Base;
				if (P == PH - 1) { C = Seam; }
				else if (P == 0) { C = Hi; }
				else if (FVArt::Noise(bVertical ? Band : Along / 3, bVertical ? Along / 3 : Band, Seed) < 0.12f) { C = Grain; }
				// board joints (horizontal planks only): one per band at a pseudo-random spot, staggered
				if (!bVertical && W > 10)
				{
					const int32 J = int32(FVArt::Noise(Band, 7, Seed) * (W - 6)) + 3;
					if (Along == J && P != PH - 1) { C = Seam; }
				}
				I.Set(XX, YY, C);
			}
		}
	}

	/** Running-bond bricks with per-brick tint variation. */
	void Bricks(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, FColor Mort, int32 BW, int32 BH, uint32 Seed)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const int32 Row = (YY - Y) / BH, PY = (YY - Y) % BH;
				const int32 OX = (XX - X) + ((Row % 2) ? BW / 2 : 0);
				const int32 Col = OX / BW, PX = OX % BW;
				if (PY == BH - 1 || PX == BW - 1)
				{
					I.Set(XX, YY, Mort);
					continue;
				}
				const float N = FVArt::Noise(Col, Row, Seed);
				FColor C = N < 0.33f ? FVArt::Shade(Base, 0.12f) : (N > 0.8f ? FVArt::Light(Base, 0.12f) : Base);
				if (PY == 0) { C = FVArt::Light(C, 0.15f); }
				I.Set(XX, YY, C);
			}
		}
	}

	/** Irregular fieldstones (jittered cells): mortar gaps, beveled edges, per-stone tint. */
	void Stones(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, FColor Mort, float Cell, uint32 Seed)
	{
		const float CW = Cell, CH = Cell * 0.72f;
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const float PX = XX + 0.5f, PY = YY + 0.5f;
				const int32 GX = FMath::FloorToInt(PX / CW), GY = FMath::FloorToInt(PY / CH);
				float D1 = 1e9f, D2 = 1e9f;
				int32 BestX = 0, BestY = 0;
				float BX = 0.f, BY = 0.f;
				for (int32 OY = -1; OY <= 1; ++OY)
				{
					for (int32 OX = -1; OX <= 1; ++OX)
					{
						const int32 CX = GX + OX, CY = GY + OY;
						const float SX = (CX + 0.2f + 0.6f * FVArt::Noise(CX, CY, Seed)) * CW;
						const float SY = (CY + 0.2f + 0.6f * FVArt::Noise(CX, CY, Seed + 17u)) * CH;
						const float DX = (PX - SX) / CW, DY = (PY - SY) / CH;
						const float D = DX * DX + DY * DY;
						if (D < D1) { D2 = D1; D1 = D; BestX = CX; BestY = CY; BX = SX; BY = SY; }
						else if (D < D2) { D2 = D; }
					}
				}
				const float Edge = (FMath::Sqrt(D2) - FMath::Sqrt(D1)) * CW;
				if (Edge < 0.9f)
				{
					I.Set(XX, YY, Mort);
					continue;
				}
				const float N = FVArt::Noise(BestX, BestY, Seed + 5u);
				FColor C = N < 0.3f ? FVArt::Shade(Base, 0.12f) : (N > 0.75f ? FVArt::Light(Base, 0.1f) : Base);
				if (Edge < 2.0f)
				{
					C = (PX < BX || PY < BY) && (PY <= BY + 0.5f) ? FVArt::Light(C, 0.28f) : FVArt::Shade(C, 0.22f);
				}
				I.Set(XX, YY, C);
			}
		}
	}

	void Ellipse(FFVImage& I, float CX, float CY, float RX, float RY, FColor C, bool bBlend = false)
	{
		for (int32 YY = FMath::FloorToInt(CY - RY) - 1; YY <= FMath::CeilToInt(CY + RY) + 1; ++YY)
		{
			for (int32 XX = FMath::FloorToInt(CX - RX) - 1; XX <= FMath::CeilToInt(CX + RX) + 1; ++XX)
			{
				const float DX = (XX + 0.5f - CX) / FMath::Max(RX, 0.1f), DY = (YY + 0.5f - CY) / FMath::Max(RY, 0.1f);
				if (DX * DX + DY * DY <= 1.f)
				{
					if (bBlend) { I.Blend(XX, YY, C); }
					else { I.Set(XX, YY, C); }
				}
			}
		}
	}

	/** Ellipse lit from the upper-left (3 tones + specular). */
	void ShadedEllipse(FFVImage& I, float CX, float CY, float RX, float RY, FColor Base, bool bSpec = true)
	{
		const FColor Lt = FVArt::Light(Base, 0.3f), Dk = FVArt::Shade(Base, 0.35f);
		for (int32 YY = FMath::FloorToInt(CY - RY) - 1; YY <= FMath::CeilToInt(CY + RY) + 1; ++YY)
		{
			for (int32 XX = FMath::FloorToInt(CX - RX) - 1; XX <= FMath::CeilToInt(CX + RX) + 1; ++XX)
			{
				const float DX = (XX + 0.5f - CX) / FMath::Max(RX, 0.1f), DY = (YY + 0.5f - CY) / FMath::Max(RY, 0.1f);
				const float D = DX * DX + DY * DY;
				if (D > 1.f) { continue; }
				const float S = DX + DY;
				I.Set(XX, YY, S < -0.7f ? Lt : ((S > 0.55f || D > 0.8f && S > 0.f) ? Dk : Base));
			}
		}
		if (bSpec)
		{
			I.Set(FMath::FloorToInt(CX - RX * 0.45f), FMath::FloorToInt(CY - RY * 0.5f), FVArt::Light(Base, 0.8f));
		}
	}

	/** Tints already painted pixels towards a light color around a point (fire / lamp light baked into the art). */
	void Warm(FFVImage& I, float CX, float CY, float R, FColor Light, float Strength)
	{
		for (int32 YY = 0; YY < I.H; ++YY)
		{
			for (int32 XX = 0; XX < I.W; ++XX)
			{
				FColor P = I.Get(XX, YY);
				if (P.A == 0) { continue; }
				const float DX = XX + 0.5f - CX, DY = YY + 0.5f - CY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if (D >= R) { continue; }
				const uint8 A = P.A;
				P = FVArt::Mix(P, Light, Strength * (1.f - D / R));
				P.A = A;
				I.Set(XX, YY, P);
			}
		}
	}

	/** Soft halo blended over everything (transparent pixels included). */
	void Halo(FFVImage& I, float CX, float CY, float R, FColor C, uint8 MaxA)
	{
		for (int32 YY = FMath::FloorToInt(CY - R); YY <= FMath::CeilToInt(CY + R); ++YY)
		{
			for (int32 XX = FMath::FloorToInt(CX - R); XX <= FMath::CeilToInt(CX + R); ++XX)
			{
				const float DX = XX + 0.5f - CX, DY = YY + 0.5f - CY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if (D >= R) { continue; }
				const float K = 1.f - D / R;
				I.Blend(XX, YY, FVArt::WithAlpha(C, uint8(MaxA * K * K)));
			}
		}
	}

	/** A flame tongue rising from (CX, BaseY): red edge, orange body, yellow core. */
	void Flame(FFVImage& I, float CX, int32 BaseY, int32 Height, float HalfW, uint32 Seed)
	{
		for (int32 YY = BaseY - Height; YY <= BaseY; ++YY)
		{
			const float T = float(BaseY - YY) / FMath::Max(1, Height);
			const float Wob = 0.35f * (FVArt::Noise(YY, 3, Seed) - 0.5f);
			const float Hw = HalfW * FMath::Pow(FMath::Max(0.f, 1.f - T), 0.65f) + 0.15f;
			const float C = CX + Wob * (0.5f + T);
			for (int32 XX = FMath::FloorToInt(C - Hw); XX <= FMath::CeilToInt(C + Hw); ++XX)
			{
				const float D = FMath::Abs(XX + 0.5f - C) / FMath::Max(Hw, 0.3f);
				if (D > 1.f) { continue; }
				FColor Col = D < 0.4f && T < 0.7f ? FireY : (D < 0.75f ? FireO : FireR);
				if (T > 0.8f) { Col = FireR; }
				I.Set(XX, YY, Col);
			}
		}
	}

	/** Thin line with an optional second (shade) color below it. */
	void Line2(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, FColor C, FColor Below)
	{
		I.Line(X0, Y0 + 1, X1, Y1 + 1, Below);
		I.Line(X0, Y0, X1, Y1, C);
	}

	/** 2px wooden stake (lit left side), slightly pointed top. */
	void Stake(FFVImage& I, int32 X, int32 Y, int32 H, FColor Base = Wood)
	{
		VLine(I, X, Y + 1, Y + H - 1, FVArt::Light(Base, 0.2f));
		VLine(I, X + 1, Y, Y + H - 1, FVArt::Shade(Base, 0.3f));
		I.Set(X, Y + 1, FVArt::Light(Base, 0.4f));
	}

	/** Tiny glyphs painted on signs (dark). 'H' hammer, 'F' fish, 'A' anchor, 'W' worm, 'S' scroll. */
	void Glyph(FFVImage& I, int32 X, int32 Y, TCHAR G, FColor C, FColor C2)
	{
		switch (G)
		{
		case TEXT('H'):
			HLine(I, X, X + 3, Y, C); I.Set(X + 5, Y, C); HLine(I, X, X + 4, Y + 1, C);
			VLine(I, X + 2, Y + 2, Y + 4, C2);
			break;
		case TEXT('F'):
			HLine(I, X + 1, X + 4, Y, C); HLine(I, X, X + 5, Y + 1, C); HLine(I, X + 1, X + 4, Y + 2, C);
			I.Set(X + 6, Y, C); I.Set(X + 6, Y + 2, C); I.Set(X + 1, Y + 1, C2);
			break;
		case TEXT('A'):
			I.Set(X + 2, Y, C); HLine(I, X + 1, X + 3, Y + 1, C); VLine(I, X + 2, Y + 2, Y + 4, C);
			I.Set(X, Y + 3, C); I.Set(X + 4, Y + 3, C); HLine(I, X + 1, X + 3, Y + 4, C);
			break;
		case TEXT('W'):
			I.Set(X, Y + 1, C); I.Set(X + 1, Y, C); I.Set(X + 2, Y + 1, C); I.Set(X + 3, Y + 2, C); I.Set(X + 4, Y + 1, C);
			I.Set(X + 5, Y, C2);
			break;
		default:
			break;
		}
	}

	// ---------------------------------------------------------------------------------------- construction site
	/** Level 0 of every station: stakes, tape, chalk footprint, a sign with a hammer, boards and stones. */
	FFVImage BuildSite(int32 W, int32 H, bool bWall)
	{
		if (bWall)
		{
			FFVImage Img(W, H);
			const int32 NW = 12, NH = 10, NX = W / 2 - NW / 2, NY = H / 2 - NH / 2;
			Rect(Img, NX, NY, NW, NH, Paper);
			HLine(Img, NX, NX + NW - 1, NY + NH - 1, PaperLo);
			VLine(Img, NX + NW - 1, NY, NY + NH - 1, PaperLo);
			Glyph(Img, NX + 3, NY + 3, TEXT('H'), IronLo, WoodDk);
			Img.Set(NX + NW / 2 - 1, NY, Rgb(220, 58, 58)); Img.Set(NX + NW / 2, NY, Rgb(248, 128, 112));
			Img.Outline(PropOut);
			for (int32 X = 2; X <= W - 3; ++X)
			{
				if ((X / 2) % 2 == 0) { SetIfEmpty(Img, X, 2, Chalk); SetIfEmpty(Img, X, H - 3, Chalk); }
			}
			for (int32 Y = 2; Y <= H - 3; ++Y)
			{
				if ((Y / 2) % 2 == 0) { SetIfEmpty(Img, 2, Y, Chalk); SetIfEmpty(Img, W - 3, Y, Chalk); }
			}
			Img.Set(3, 3, IronHi); Img.Set(W - 4, 3, IronHi); Img.Set(3, H - 4, IronHi); Img.Set(W - 4, H - 4, IronHi);
			return Img;
		}

		const int32 SH = FMath::Clamp(H, 20, 28);
		FFVImage Img(W, SH);
		const int32 FY0 = SH - 10, FY1 = SH - 2;          // footprint back / front rows
		// back stakes (wide sites only: narrow ones would get crowded)
		if (W >= 30)
		{
			Stake(Img, 2, FY0 - 3, 4);
			Stake(Img, W - 4, FY0 - 3, 4);
		}
		// hazard-striped sign on a post with a hammer on a light panel
		const int32 BW = FMath::Clamp(W / 2 + 2, 11, 16), BH = 8, BX = W / 2 - BW / 2, BY = 1;
		VLine(Img, W / 2 - 1, BY + BH, FY0 + 1, Wood);
		VLine(Img, W / 2, BY + BH, FY0 + 1, WoodLo);
		for (int32 Y = BY; Y < BY + BH; ++Y)
		{
			for (int32 X = BX; X < BX + BW; ++X)
			{
				Img.Set(X, Y, ((X + Y) / 2) % 2 == 0 ? Rgb(255, 204, 51) : Rgb(52, 46, 52));
			}
		}
		Box(Img, BX + 2, BY + 1, BW - 4, BH - 2, Paper, 0.2f, 0.25f);
		Glyph(Img, W / 2 - 3, BY + 2, TEXT('H'), IronLo, WoodDk);
		// boards (front-left) and stones (front-right), never overlapping
		const int32 PW = FMath::Max(4, W / 2 - 6);
		Box(Img, 4, SH - 6, PW, 2, Wood, 0.3f, 0.3f);
		Box(Img, 5, SH - 8, PW - 1, 2, WoodHi, 0.3f, 0.3f);
		Img.Set(4, SH - 6, WoodHi); Img.Set(5, SH - 8, Rgb(236, 200, 150));
		const int32 SX = FMath::Max(W / 2 + 1, W - 13);
		Box(Img, SX, SH - 5, 4, 3, Stone, 0.3f, 0.35f);
		Box(Img, SX + 4, SH - 5, 4, 3, StoneLo, 0.3f, 0.35f);
		Box(Img, SX + 2, SH - 8, 4, 3, Stone, 0.35f, 0.3f);
		// front stakes
		Stake(Img, 1, FY1 - 5, 5);
		Stake(Img, W - 3, FY1 - 5, 5);
		Img.Outline(PropOut);
		// chalk footprint + warning tape (no outline)
		for (int32 X = 4; X <= W - 5; ++X)
		{
			if ((X / 2) % 2 == 0) { SetIfEmpty(Img, X, FY0 + 1, Chalk); }
			if ((X / 2) % 2 == 1) { SetIfEmpty(Img, X, FY1, Chalk); }
			const bool bYellow = (X / 2) % 2 == 0;
			SetIfEmpty(Img, X, FY1 - 4, bYellow ? Rgb(255, 204, 51) : Rgb(52, 46, 52));
			SetIfEmpty(Img, X, FY0 - 2, bYellow ? Rgb(236, 186, 48) : Rgb(52, 46, 52));
		}
		for (int32 Y = FY0 + 1; Y <= FY1; ++Y)
		{
			if (Y % 2 == 0) { SetIfEmpty(Img, 3, Y, Chalk); SetIfEmpty(Img, W - 4, Y, Chalk); }
		}
		return Img;
	}

	// ---------------------------------------------------------------------------------------- kitchen
	void Pot(FFVImage& I, float CX, int32 RimY, float RX, float RY, FColor Base)
	{
		const FColor Hi = FVArt::Light(Base, 0.35f), Lo = FVArt::Shade(Base, 0.35f);
		for (int32 YY = RimY + 1; YY <= RimY + 1 + FMath::CeilToInt(RY); ++YY)
		{
			for (int32 XX = FMath::FloorToInt(CX - RX) - 1; XX <= FMath::CeilToInt(CX + RX) + 1; ++XX)
			{
				const float DX = (XX + 0.5f - CX) / RX, DY = (YY + 0.5f - (RimY + 1)) / RY;
				if (DX * DX + DY * DY > 1.f) { continue; }
				I.Set(XX, YY, DX < -0.45f ? Hi : (DX > 0.4f ? Lo : Base));
			}
		}
		HLine(I, FMath::FloorToInt(CX - RX) - 1, FMath::CeilToInt(CX + RX), RimY, Hi);
		HLine(I, FMath::FloorToInt(CX - RX) - 1, FMath::CeilToInt(CX + RX), RimY + 1, Base);
	}

	/** Kitchen level 4 «Русская печь» (44x44): a huge whitewashed oven with a лежанка under a curtain (and the cat),
	    an arched mouth half closed by the iron заслонка, a side cooktop with two чугунки, a firewood niche, an ухват. */
	FFVImage BuildRussianOven()
	{
		FFVImage Img(44, 44);
		const FColor OrnB(60, 100, 184), OrnL(110, 150, 220), OrnR(200, 60, 60);
		const FColor Cloth(206, 58, 58), ClothLo(150, 36, 44), Dot(250, 236, 220);
		// flue at the back left (continues into the chimney breast painted on the wall)
		Bricks(Img, 4, 0, 9, 12, Wash, WashLo, 4, 2, 141u);
		HLine(Img, 3, 13, 0, StoneLo); HLine(Img, 3, 13, 1, Stone);
		VLine(Img, 4, 2, 11, WashHi); VLine(Img, 12, 2, 11, WashLo);
		// the body
		for (int32 Y = 14; Y <= 43; ++Y)
		{
			for (int32 X = 3; X <= 33; ++X)
			{
				FColor C = Wash;
				if (FVArt::Noise(X / 3, Y / 2, 143u) < 0.18f && ((X + Y) % 3) != 0) { C = WashLo; }
				if (X == 3) { C = WashHi; }
				else if (X >= 32) { C = WashLo; }
				if (Y == 43) { C = FVArt::Shade(Wash, 0.3f); }
				Img.Set(X, Y, C);
			}
		}
		// лежанка: the flat top seen from above, a pillow, a patchwork blanket and a sleeping ginger cat
		for (int32 Y = 10; Y <= 13; ++Y) { HLine(Img, 2, 34, Y, Y == 10 ? WashHi : Wash); }
		HLine(Img, 2, 34, 14, WashLo);
		Box(Img, 14, 8, 6, 5, Rgb(246, 242, 232), 0.3f, 0.25f);
		static const FColor Patch[3] = { FColor(80, 132, 194), FColor(238, 212, 152), FColor(204, 72, 62) };
		for (int32 Y = 9; Y <= 13; ++Y)
		{
			for (int32 X = 20; X <= 32; ++X)
			{
				FColor C = Patch[((X - 20) / 3 + (Y - 9) / 2) % 3];
				if (Y == 13) { C = FVArt::Shade(C, 0.3f); }
				Img.Set(X, Y, C);
			}
		}
		const FColor Cat(226, 150, 72), CatLo(176, 100, 48), CatHi(250, 196, 120);
		Ellipse(Img, 25.5f, 8.5f, 3.6f, 2.2f, Cat);
		HLine(Img, 23, 27, 7, CatHi); HLine(Img, 23, 28, 10, CatLo);
		Img.Set(22, 7, Cat); Img.Set(22, 6, CatLo); Img.Set(24, 6, CatLo);                    // ears
		Img.Set(23, 8, Rgb(70, 44, 40));                                                     // closed eye
		HLine(Img, 26, 29, 8, CatLo); Img.Set(29, 9, Cat); Img.Set(29, 10, CatHi);            // stripes + tail tip
		// curtain on a rod, gathered to the right
		HLine(Img, 13, 36, 2, DWoodLo); Img.Set(13, 1, DWood); Img.Set(36, 1, DWood);
		for (int32 X = 14; X <= 35; ++X) { Img.Set(X, 3, (X % 2) ? Cloth : ClothLo); }
		for (int32 Y = 3; Y <= 16; ++Y)
		{
			const int32 X0 = 33 - (Y > 10 ? (Y - 10) / 3 : 0);
			for (int32 X = X0; X <= 36; ++X)
			{
				FColor C = ((X - X0) % 2) ? ClothLo : Cloth;
				if (((X + Y * 2) % 5) == 0) { C = Dot; }
				Img.Set(X, Y, C);
			}
		}
		// ornament band
		for (int32 X = 5; X <= 31; ++X)
		{
			const int32 P = X % 4;
			Img.Set(X, 17, P == 0 ? OrnB : Wash);
			Img.Set(X, 18, (P == 1 || P == 3) ? OrnL : (P == 2 ? OrnR : Wash));
			Img.Set(X, 19, P == 0 ? OrnB : Wash);
		}
		// mouth (чело) with the fire, half closed by the заслонка
		for (int32 Y = 21; Y <= 33; ++Y)
		{
			for (int32 X = 8; X <= 26; ++X)
			{
				const float DX = (X + 0.5f - 17.f) / 7.5f, DY = (Y + 0.5f - 27.f) / 5.5f;
				const bool bIn = Y >= 27 ? (X >= 10 && X <= 24) : (DX * DX + DY * DY <= 1.f);
				const float DXo = (X + 0.5f - 17.f) / 8.5f, DYo = (Y + 0.5f - 27.f) / 6.5f;
				const bool bRim = Y >= 27 ? (X == 9 || X == 25) : (DXo * DXo + DYo * DYo <= 1.f);
				if (bIn) { Img.Set(X, Y, FireBack); }
				else if (bRim) { Img.Set(X, Y, WashLo); }
			}
		}
		HLine(Img, 11, 23, 33, Ember);
		Flame(Img, 13.5f, 32, 5, 2.0f, 145u);
		Flame(Img, 16.5f, 32, 7, 2.4f, 147u);
		for (int32 X = 11; X <= 18; ++X) { if ((X % 3) == 0) { Img.Set(X, 33, FireO); } }
		for (int32 Y = 24; Y <= 34; ++Y)
		{
			for (int32 X = 19; X <= 26; ++X)
			{
				if (Y == 24 && (X == 19 || X == 26)) { continue; }
				Img.Set(X, Y, X == 19 ? IronHi : (X == 26 ? IronDk : (Y == 24 ? IronHi : Iron)));
			}
		}
		HLine(Img, 21, 24, 28, BrassLo); HLine(Img, 21, 24, 27, Brass); Img.Set(21, 27, BrassHi);
		// шесток ledge and the firewood niche under it
		HLine(Img, 6, 28, 35, StoneHi); HLine(Img, 6, 28, 36, StoneLo);
		for (int32 Y = 38; Y <= 42; ++Y) { HLine(Img, 11, 23, Y, Rgb(52, 36, 32)); }
		for (int32 I = 0; I < 6; ++I)
		{
			const int32 LX = 12 + I * 2;
			Img.Set(LX, 41, Rgb(206, 156, 100)); Img.Set(LX, 42, WoodMid);
			if (I < 5) { Img.Set(LX + 1, 40, Rgb(190, 140, 88)); }
		}
		// side cooktop with two hobs and two чугунки
		Bricks(Img, 34, 26, 9, 17, Wash, WashLo, 4, 2, 149u);
		for (int32 Y = 22; Y <= 25; ++Y) { HLine(Img, 33, 43, Y, Y == 22 ? IronHi : (Y == 25 ? IronLo : Iron)); }
		Box(Img, 36, 33, 6, 5, IronLo, 0.35f, 0.3f);
		HLine(Img, 37, 40, 35, FireO); HLine(Img, 37, 40, 36, FireR);
		Pot(Img, 36.5f, 18, 2.6f, 3.2f, Rgb(58, 56, 66));
		Pot(Img, 41.f, 19, 2.2f, 2.6f, Rgb(70, 66, 76));
		Img.Set(35, 17, IronHi); Img.Set(40, 18, IronHi);
		// ухват leaning on the left
		Img.Line(1, 43, 3, 18, WoodHi); Img.Line(2, 43, 4, 18, WoodMid);
		Img.Set(1, 17, IronLo); Img.Set(1, 16, IronLo); Img.Set(1, 15, Iron);
		Img.Set(5, 17, IronLo); Img.Set(5, 16, IronLo); Img.Set(6, 15, Iron);
		HLine(Img, 2, 4, 17, IronLo);
		// painted flowers on the corners of the face
		auto Flower = [&Img, OrnB, OrnR](int32 X, int32 Y)
		{
			Img.Set(X, Y, OrnR); Img.Set(X - 1, Y, OrnB); Img.Set(X + 1, Y, OrnB);
			Img.Set(X, Y - 1, OrnB); Img.Set(X, Y + 1, OrnB); Img.Set(X, Y + 2, Rgb(70, 140, 70));
		};
		Flower(6, 24); Flower(29, 24); Flower(6, 31); Flower(29, 31);
		Img.Outline(PropOut);
		Warm(Img, 16.f, 29.f, 12.f, Rgb(255, 170, 90), 0.3f);
		for (int32 I = 0; I < 5; ++I)
		{
			Img.Blend(36 + (I % 2), 15 - I * 2, Rgb(255, 255, 255, uint8(150 - I * 26)));
			Img.Blend(41 - (I % 2), 16 - I * 2, Rgb(255, 255, 255, uint8(130 - I * 24)));
		}
		return Img;
	}

	FFVImage BuildKitchen(int32 Level)
	{
		if (Level >= 4)
		{
			return BuildRussianOven();
		}
		FFVImage Img(32, 32);
		if (Level <= 1)
		{
			// ---- Очаг: fieldstone hearth with an arched fire mouth and an iron pot on the slab
			Stones(Img, 2, 14, 28, 17, Stone, StoneDk, 5.f, 11u);
			for (int32 X = 1; X <= 30; ++X)
			{
				Img.Set(X, 12, StoneHi); Img.Set(X, 13, Stone); Img.Set(X, 14, StoneLo);
			}
			for (int32 Y = 18; Y <= 30; ++Y)
			{
				for (int32 X = 9; X <= 22; ++X)
				{
					const float DX = (X + 0.5f - 16.f) / 6.2f, DY = (Y + 0.5f - 22.5f) / 4.2f;
					const bool bIn = Y >= 22 ? (X >= 10 && X <= 21) : (DX * DX + DY * DY <= 1.f);
					const float DXo = (X + 0.5f - 16.f) / 7.2f, DYo = (Y + 0.5f - 22.5f) / 5.2f;
					const bool bRing = Y >= 22 ? (X == 9 || X == 22) : (DXo * DXo + DYo * DYo <= 1.f);
					if (bIn) { Img.Set(X, Y, FireBack); }
					else if (bRing) { Img.Set(X, Y, (X + Y) % 3 == 0 ? StoneLo : StoneDk); }
				}
			}
			// logs + flames + embers
			HLine(Img, 11, 20, 29, Rgb(120, 74, 44)); HLine(Img, 12, 19, 28, Rgb(150, 98, 58));
			Img.Set(11, 29, Rgb(200, 150, 100)); Img.Set(20, 29, Rgb(200, 150, 100));
			Flame(Img, 13.5f, 27, 6, 2.2f, 3u);
			Flame(Img, 18.5f, 27, 5, 2.0f, 5u);
			Flame(Img, 16.0f, 27, 8, 2.8f, 7u);
			for (int32 X = 10; X <= 21; ++X) { Img.Set(X, 30, (X % 3 == 0) ? FireO : Ember); }
			// iron pot
			Pot(Img, 16.f, 6, 6.8f, 5.2f, Rgb(74, 72, 86));
			Img.Set(8, 8, IronLo); Img.Set(8, 9, IronLo); Img.Set(24, 8, IronLo); Img.Set(24, 9, IronLo);
			Img.Line(19, 5, 22, 1, WoodHi); Img.Line(20, 5, 23, 1, WoodMid);
			Img.Outline(PropOut);
			Warm(Img, 16.f, 25.f, 10.f, Rgb(255, 170, 90), 0.35f);
			for (int32 I = 0; I < 5; ++I)
			{
				Img.Blend(12 + (I % 2), 4 - I, Rgb(255, 255, 255, uint8(170 - I * 30)));
				Img.Blend(16 - (I % 2), 3 - I, Rgb(255, 255, 255, uint8(150 - I * 30)));
			}
			return Img;
		}
		if (Level == 2)
		{
			// ---- Плита: cast-iron stove with an oven window, a frying pan with fish and a kettle, stovepipe
			for (int32 Y = 0; Y <= 12; ++Y)
			{
				Img.Set(25, Y, IronHi); Img.Set(26, Y, Iron); Img.Set(27, Y, Iron); Img.Set(28, Y, IronLo);
			}
			HLine(Img, 24, 29, 4, IronHi); HLine(Img, 24, 29, 9, IronHi);
			for (int32 Y = 11; Y <= 14; ++Y)
			{
				for (int32 X = 2; X <= 29; ++X)
				{
					Img.Set(X, Y, FVArt::Noise(X, Y, 21u) < 0.15f ? IronLo : Iron);
				}
			}
			HLine(Img, 2, 29, 15, IronHi); HLine(Img, 2, 29, 16, Brass);
			Ellipse(Img, 9.f, 12.8f, 4.6f, 1.6f, IronDk); Ellipse(Img, 9.f, 12.8f, 2.8f, 0.8f, IronLo);
			Ellipse(Img, 20.f, 12.8f, 4.2f, 1.5f, IronDk); Ellipse(Img, 20.f, 12.8f, 2.4f, 0.7f, IronLo);
			Box(Img, 3, 17, 26, 11, Rgb(78, 76, 92), 0.25f, 0.4f);
			// oven door + glowing window
			Box(Img, 5, 18, 12, 9, IronLo, 0.35f, 0.3f);
			for (int32 Y = 20; Y <= 24; ++Y)
			{
				for (int32 X = 8; X <= 13; ++X)
				{
					const float D = FMath::Abs(X - 10.5f) / 3.f + FMath::Abs(Y - 22.f) / 2.5f;
					Img.Set(X, Y, D < 0.6f ? FireY : (D < 1.1f ? FireO : FireR));
				}
			}
			HLine(Img, 7, 14, 19, BrassHi); HLine(Img, 7, 14, 18, BrassLo);
			// firebox door with glowing vents
			Box(Img, 19, 18, 8, 9, IronLo, 0.35f, 0.3f);
			HLine(Img, 21, 24, 21, FireO); HLine(Img, 21, 24, 23, FireR); HLine(Img, 21, 24, 25, Ember);
			Img.Set(25, 19, Brass); Img.Set(25, 20, BrassLo);
			// legs
			VLine(Img, 4, 28, 30, IronLo); VLine(Img, 5, 28, 29, Iron); VLine(Img, 26, 28, 29, Iron); VLine(Img, 27, 28, 30, IronLo);
			// frying pan with a golden fish
			Ellipse(Img, 9.f, 10.6f, 5.6f, 1.9f, IronDk);
			Ellipse(Img, 9.f, 10.3f, 4.4f, 1.1f, Rgb(64, 60, 70));
			Ellipse(Img, 9.2f, 10.2f, 3.f, 0.9f, Rgb(226, 160, 64));
			Img.Set(7, 10, Rgb(255, 214, 120)); Img.Set(12, 9, Rgb(200, 120, 50)); Img.Set(12, 11, Rgb(200, 120, 50));
			HLine(Img, 1, 3, 10, WoodDk); Img.Set(1, 9, WoodDk);
			// enamel kettle
			ShadedEllipse(Img, 19.5f, 8.2f, 4.2f, 3.4f, Rgb(70, 120, 196));
			HLine(Img, 16, 23, 11, Rgb(44, 74, 140));
			Img.Set(19, 4, Rgb(40, 40, 52)); Img.Set(20, 4, Rgb(40, 40, 52));
			Img.Line(15, 8, 13, 6, Rgb(70, 120, 196)); Img.Set(12, 5, Rgb(120, 170, 230));
			Img.Line(16, 5, 17, 3, IronDk); HLine(Img, 17, 22, 2, IronDk); Img.Line(22, 2, 23, 5, IronDk);
			Img.Outline(PropOut);
			Halo(Img, 10.5f, 22.f, 5.f, Rgb(255, 190, 90), 90);
			for (int32 I = 0; I < 4; ++I) { Img.Blend(12 - (I % 2), 4 - I, Rgb(255, 255, 255, uint8(160 - I * 35))); }
			return Img;
		}
		// ---- Печь: whitewashed Russian oven with an arched mouth, a clay pot, firewood niche and blue ornaments
		Bricks(Img, 19, 1, 9, 9, Wash, WashLo, 4, 2, 31u);
		HLine(Img, 18, 28, 0, StoneLo); HLine(Img, 18, 28, 1, Stone);
		for (int32 Y = 9; Y <= 30; ++Y)
		{
			for (int32 X = 2; X <= 29; ++X)
			{
				FColor C = Wash;
				if (FVArt::Noise(X / 4, Y / 2, 33u) < 0.2f && Y % 2 == 0 && X % 4 != 0) { C = WashLo; }
				if (X == 2) { C = WashHi; }
				if (X == 29) { C = WashLo; }
				Img.Set(X, Y, C);
			}
		}
		HLine(Img, 1, 30, 9, WashHi); HLine(Img, 1, 30, 10, Wash); HLine(Img, 1, 30, 11, WashLo);
		// ornament band
		for (int32 X = 4; X <= 27; ++X)
		{
			const int32 P = X % 4;
			Img.Set(X, 13, P == 0 ? Rgb(60, 100, 184) : Wash);
			Img.Set(X, 14, P == 1 || P == 3 ? Rgb(110, 150, 220) : (P == 2 ? Rgb(200, 60, 60) : Wash));
			Img.Set(X, 15, P == 0 ? Rgb(60, 100, 184) : Wash);
		}
		// mouth (чело)
		for (int32 Y = 16; Y <= 24; ++Y)
		{
			for (int32 X = 8; X <= 23; ++X)
			{
				const float DX = (X + 0.5f - 16.f) / 7.f, DY = (Y + 0.5f - 20.f) / 4.f;
				const bool bIn = Y >= 20 ? (X >= 9 && X <= 22) : (DX * DX + DY * DY <= 1.f);
				const float DXo = (X + 0.5f - 16.f) / 8.f, DYo = (Y + 0.5f - 20.f) / 5.f;
				const bool bRim = Y >= 20 ? (X == 8 || X == 23) : (DXo * DXo + DYo * DYo <= 1.f);
				if (bIn) { Img.Set(X, Y, FireBack); }
				else if (bRim) { Img.Set(X, Y, WashLo); }
			}
		}
		Flame(Img, 12.5f, 23, 4, 1.8f, 9u);
		Flame(Img, 20.f, 23, 4, 1.7f, 13u);
		Pot(Img, 16.f, 19, 3.2f, 3.2f, Terra);
		HLine(Img, 13, 19, 19, TerraHi);
		// шесток (ledge)
		HLine(Img, 6, 25, 25, StoneHi); HLine(Img, 6, 25, 26, StoneLo);
		// firewood niche
		for (int32 Y = 27; Y <= 30; ++Y) { HLine(Img, 10, 21, Y, Rgb(52, 36, 32)); }
		for (int32 I = 0; I < 5; ++I)
		{
			const int32 LX = 11 + I * 2;
			Img.Set(LX, 29, Rgb(206, 156, 100)); Img.Set(LX, 30, WoodMid);
			if (I < 4) { Img.Set(LX + 1, 28, Rgb(190, 140, 88)); }
		}
		// corner flowers
		auto Flower = [&Img](int32 X, int32 Y)
		{
			Img.Set(X, Y, Rgb(220, 70, 70)); Img.Set(X - 1, Y, Rgb(60, 100, 184)); Img.Set(X + 1, Y, Rgb(60, 100, 184));
			Img.Set(X, Y - 1, Rgb(60, 100, 184)); Img.Set(X, Y + 1, Rgb(60, 100, 184)); Img.Set(X, Y + 2, Rgb(70, 140, 70));
		};
		Flower(5, 19); Flower(26, 19); Flower(5, 26); Flower(26, 26);
		Img.Outline(PropOut);
		Warm(Img, 16.f, 21.f, 9.f, Rgb(255, 170, 90), 0.3f);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- workshop
	void Saw(FFVImage& I, int32 X, int32 Y, int32 L)
	{
		// blade (trapezoid, teeth along the bottom) + wooden handle on the right
		for (int32 R = 0; R < 4; ++R)
		{
			HLine(I, X + (3 - R), X + L - 1, Y + R, R == 0 ? MetHi : Met);
		}
		for (int32 XX = X + 1; XX < X + L; XX += 2) { I.Set(XX, Y + 4, MetLo); }
		I.Set(X + L - 3, Y + 1, MetLo);
		Box(I, X + L, Y - 1, 4, 6, WoodMid, 0.3f, 0.3f);
		I.Set(X + L + 1, Y + 1, WoodDk); I.Set(X + L + 2, Y + 1, WoodDk); I.Set(X + L + 1, Y + 2, WoodDk); I.Set(X + L + 2, Y + 2, WoodDk);
	}

	/** Workshop level 4 «Мастерская мастера» (48x30): a heavy oak bench with a big vise, chunks of hard rock (purple
	    veins), blueprints of the pickaxe and a reel on the board behind, a grindstone on a trestle, a lamp. */
	FFVImage BuildMasterWorkshop()
	{
		FFVImage Img(48, 30);
		const FColor Rock(92, 86, 108), RockHi(130, 124, 148), RockLo(62, 58, 76), Vein(170, 90, 230);
		const FColor Blue(56, 96, 168), BlueLo(40, 70, 130), Ink(220, 236, 250);
		// back board with the blueprints
		Planks(Img, 4, 0, 34, 11, DWoodLo, 5, 151u, true);
		HLine(Img, 4, 37, 0, DWood);
		Box(Img, 6, 1, 13, 9, Blue, 0.2f, 0.3f);
		HLine(Img, 8, 16, 3, Ink); Img.Line(12, 3, 12, 8, Ink); Img.Set(8, 4, Ink); Img.Set(16, 4, Ink);   // pickaxe drawing
		HLine(Img, 8, 11, 8, BlueLo); HLine(Img, 14, 17, 8, BlueLo);
		Img.Set(7, 1, Rgb(220, 60, 60)); Img.Set(17, 1, Rgb(220, 60, 60));                                     // pins
		Box(Img, 21, 2, 10, 8, Blue, 0.2f, 0.3f);
		Ellipse(Img, 26.f, 6.f, 3.f, 2.6f, Ink); Ellipse(Img, 26.f, 6.f, 1.8f, 1.5f, Blue); Img.Set(26, 6, Ink);   // reel drawing
		Img.Set(22, 2, Rgb(240, 200, 60));
		// hammer and chisel hanging on the board
		Box(Img, 33, 2, 4, 2, Iron, 0.4f, 0.3f); VLine(Img, 34, 4, 9, WoodHi); VLine(Img, 35, 4, 9, WoodMid);
		// lamp on a bracket (effect point 44.5, 5)
		VLine(Img, 46, 1, 12, IronLo); HLine(Img, 42, 46, 1, IronLo);
		HLine(Img, 42, 46, 2, Rgb(236, 196, 72)); HLine(Img, 41, 47, 3, Rgb(200, 150, 50)); Img.Set(44, 4, FireY);
		// bench top: thick oak butcher block
		for (int32 X = 1; X <= 38; ++X)
		{
			const FColor C = ((X / 5) % 2 == 0) ? DWoodHi : Rgb(158, 100, 60);
			Img.Set(X, 11, FVArt::Light(C, 0.2f)); Img.Set(X, 12, C); Img.Set(X, 13, C);
		}
		HLine(Img, 1, 38, 14, DWood); HLine(Img, 1, 38, 15, DWoodLo);
		// heavy legs, a cabinet with drawers, a lower shelf with a rock chunk
		for (int32 Y = 16; Y <= 29; ++Y) { HLine(Img, 2, 6, Y, Y == 29 ? DWoodDk : DWood); Img.Set(2, Y, DWoodHi); }
		Box(Img, 22, 16, 16, 14, DWood, 0.25f, 0.35f);
		Box(Img, 24, 17, 12, 5, DWoodHi, 0.25f, 0.3f); Box(Img, 24, 23, 12, 5, DWoodHi, 0.25f, 0.3f);
		HLine(Img, 29, 31, 19, BrassHi); HLine(Img, 29, 31, 25, BrassHi);
		HLine(Img, 7, 21, 24, DWoodLo); HLine(Img, 7, 21, 25, DWoodDk);
		Box(Img, 10, 20, 7, 4, Rock, 0.35f, 0.35f); Img.Line(11, 22, 15, 21, Vein);
		// big vise on the left end
		Box(Img, 1, 6, 9, 5, Iron, 0.4f, 0.35f);
		Box(Img, 3, 3, 3, 3, IronLo, 0.4f, 0.3f); Box(Img, 6, 3, 3, 3, IronLo, 0.4f, 0.3f);
		HLine(Img, 0, 9, 11, MetHi); VLine(Img, 0, 7, 12, MetLo);
		// hard rock chunks on the bench
		Box(Img, 13, 7, 6, 4, Rock, 0.35f, 0.35f); Img.Line(14, 9, 17, 8, Vein); Img.Set(14, 7, RockHi);
		Box(Img, 20, 8, 5, 3, RockLo, 0.35f, 0.35f); Img.Set(22, 9, Vein);
		Box(Img, 27, 9, 3, 2, Rock, 0.35f, 0.35f);
		// calipers
		Img.Line(31, 10, 35, 8, MetHi); Img.Line(31, 10, 35, 10, Met);
		// grindstone on a trestle, with a water trough
		Img.Line(40, 29, 42, 16, WoodMid); Img.Line(47, 29, 45, 16, WoodLo); HLine(Img, 41, 46, 24, WoodMid);
		for (int32 Y = 10; Y <= 22; ++Y)
		{
			for (int32 X = 41; X <= 46; ++X)
			{
				const float DX = (X + 0.5f - 43.5f) / 2.8f, DY = (Y + 0.5f - 16.f) / 6.2f;
				const float D = DX * DX + DY * DY;
				if (D > 1.f) { continue; }
				Img.Set(X, Y, D > 0.7f ? StoneLo : (DX < -0.2f ? StoneHi : Stone));
			}
		}
		Img.Set(43, 16, IronDk); Img.Set(44, 16, IronDk); HLine(Img, 45, 47, 16, IronLo); VLine(Img, 47, 14, 16, IronLo);
		Box(Img, 39, 22, 9, 3, WoodLo, 0.3f, 0.3f); HLine(Img, 40, 46, 22, Rgb(104, 170, 210));
		Img.Outline(PropOut);
		Halo(Img, 44.f, 5.f, 6.f, Rgb(255, 230, 150), 70);
		Img.Set(40, 12, BrassHi); Img.Set(39, 10, FireY);                                                    // sparks
		return Img;
	}

	FFVImage BuildWorkshop(int32 Level)
	{
		if (Level >= 4)
		{
			return BuildMasterWorkshop();
		}
		FFVImage Img(40, 28);
		if (Level <= 1)
		{
			// ---- Стол: simple table with a saw, a hammer, boards and a box of nails
			VLine(Img, 4, 15, 26, WoodHi); VLine(Img, 5, 15, 26, Wood); VLine(Img, 6, 15, 26, WoodLo);
			VLine(Img, 33, 15, 26, WoodHi); VLine(Img, 34, 15, 26, Wood); VLine(Img, 35, 15, 26, WoodLo);
			HLine(Img, 7, 32, 21, WoodMid); HLine(Img, 7, 32, 22, WoodLo);
			Planks(Img, 1, 10, 38, 3, Wood, 3, 41u);
			HLine(Img, 1, 38, 10, WoodHi);
			HLine(Img, 1, 38, 13, WoodMid); HLine(Img, 1, 38, 14, WoodLo);
			Saw(Img, 5, 6, 11);
			// hammer lying on the table
			Box(Img, 22, 5, 3, 5, Iron, 0.4f, 0.3f);
			HLine(Img, 25, 30, 8, WoodHi); HLine(Img, 25, 30, 9, WoodMid);
			// boards
			Box(Img, 29, 6, 9, 2, WoodHi, 0.3f, 0.3f); Box(Img, 30, 8, 8, 2, Wood, 0.3f, 0.3f);
			Img.Set(37, 6, Rgb(236, 200, 150)); Img.Set(37, 8, Rgb(226, 190, 140));
			// box of nails
			Box(Img, 2, 7, 4, 3, Rgb(196, 160, 110), 0.3f, 0.3f);
			Img.Set(3, 7, MetLo); Img.Set(4, 6, Met);
			Img.Outline(PropOut);
			// nails and shavings (no outline)
			Img.Set(13, 11, MetHi); Img.Set(15, 11, Met); Img.Set(21, 11, MetHi);
			SetIfEmpty(Img, 12, 26, Rgb(236, 200, 150)); SetIfEmpty(Img, 13, 25, Rgb(214, 166, 108)); SetIfEmpty(Img, 14, 26, Rgb(236, 200, 150));
			SetIfEmpty(Img, 19, 26, Rgb(214, 166, 108)); SetIfEmpty(Img, 20, 25, Rgb(236, 200, 150));
			return Img;
		}
		if (Level == 2)
		{
			// ---- Верстак: sturdy workbench with a vise, a tool pegboard and a lower shelf of boards
			Rect(Img, 5, 0, 30, 11, DWoodLo);
			for (int32 Y = 1; Y <= 9; Y += 2) { for (int32 X = 6; X <= 33; X += 3) { Img.Set(X, Y, DWoodDk); } }
			HLine(Img, 5, 34, 0, DWood); VLine(Img, 5, 0, 10, DWood); VLine(Img, 34, 0, 10, DWoodDk);
			// tools on the pegboard
			Saw(Img, 7, 3, 7);
			Box(Img, 18, 1, 4, 2, Iron, 0.4f, 0.3f); VLine(Img, 19, 3, 9, WoodHi); VLine(Img, 20, 3, 9, WoodMid);
			VLine(Img, 24, 3, 9, Met); VLine(Img, 25, 3, 9, MetLo); Img.Set(23, 2, Met); Img.Set(26, 2, Met); Img.Set(23, 3, MetLo); Img.Set(26, 3, MetLo);
			VLine(Img, 28, 2, 4, Rgb(210, 60, 50)); VLine(Img, 29, 2, 4, Rgb(160, 40, 40)); VLine(Img, 28, 5, 9, Met);
			VLine(Img, 31, 2, 4, Rgb(240, 190, 60)); VLine(Img, 32, 2, 4, Rgb(200, 140, 40)); VLine(Img, 31, 5, 8, Met);
			// bench top (butcher block)
			for (int32 X = 1; X <= 38; ++X)
			{
				const FColor C = ((X / 4) % 2 == 0) ? WoodHi : Rgb(200, 150, 96);
				Img.Set(X, 11, FVArt::Light(C, 0.2f)); Img.Set(X, 12, C);
			}
			HLine(Img, 1, 38, 13, WoodMid); HLine(Img, 1, 38, 14, WoodLo);
			// legs + shelf with boards
			for (int32 LX : { 3, 33 })
			{
				VLine(Img, LX, 15, 26, WoodHi); VLine(Img, LX + 1, 15, 26, Wood); VLine(Img, LX + 2, 15, 26, Wood); VLine(Img, LX + 3, 15, 26, WoodLo);
			}
			HLine(Img, 7, 32, 21, WoodMid); HLine(Img, 7, 32, 22, WoodLo);
			Box(Img, 9, 18, 20, 2, WoodHi, 0.3f, 0.3f); Box(Img, 11, 16, 16, 2, Wood, 0.3f, 0.3f);
			// vise on the left end
			Box(Img, 2, 8, 6, 4, Iron, 0.4f, 0.35f);
			Box(Img, 1, 8, 2, 5, IronLo, 0.4f, 0.3f);
			HLine(Img, 1, 5, 13, MetHi); Img.Set(1, 14, MetLo); Img.Set(5, 14, MetLo);
			Img.Outline(PropOut);
			return Img;
		}
		// ---- Станок: bench with a small lathe, drawers, a toolbox and a lamp
		for (int32 X = 1; X <= 38; ++X)
		{
			Img.Set(X, 12, DWoodHi); Img.Set(X, 13, DWood); Img.Set(X, 14, DWood); Img.Set(X, 15, MetLo);
		}
		VLine(Img, 3, 16, 26, IronHi); VLine(Img, 4, 16, 26, Iron); VLine(Img, 5, 16, 26, IronLo);
		HLine(Img, 6, 21, 22, IronLo);
		Box(Img, 22, 16, 15, 11, DWood, 0.25f, 0.35f);
		Box(Img, 24, 17, 11, 4, DWoodHi, 0.25f, 0.3f); Box(Img, 24, 22, 11, 4, DWoodHi, 0.25f, 0.3f);
		HLine(Img, 28, 30, 18, BrassHi); HLine(Img, 28, 30, 23, BrassHi);
		// lathe
		const FColor Green(86, 140, 112);
		Box(Img, 4, 9, 19, 3, Iron, 0.35f, 0.35f);
		Box(Img, 4, 3, 6, 7, Green, 0.35f, 0.35f);
		Box(Img, 19, 5, 4, 5, Green, 0.35f, 0.35f);
		Box(Img, 10, 6, 2, 3, Met, 0.4f, 0.3f);
		for (int32 X = 12; X <= 18; ++X) { Img.Set(X, 6, WoodHi); Img.Set(X, 7, Wood); Img.Set(X, 8, WoodMid); }
		Img.Set(15, 7, WoodLo);
		Ellipse(Img, 6.5f, 2.5f, 2.2f, 2.2f, Met); Img.Set(6, 2, IronDk);
		Img.Set(20, 4, Met); Img.Set(21, 4, Met);
		// toolbox
		Box(Img, 26, 7, 11, 5, Rgb(204, 64, 52), 0.3f, 0.35f);
		HLine(Img, 29, 33, 5, IronLo); Img.Set(29, 6, IronLo); Img.Set(33, 6, IronLo);
		HLine(Img, 26, 36, 9, Rgb(150, 40, 40)); Img.Set(31, 9, Brass);
		// lamp on a bracket
		VLine(Img, 37, 3, 11, IronLo); HLine(Img, 34, 37, 2, IronLo);
		HLine(Img, 31, 35, 3, Rgb(236, 196, 72)); HLine(Img, 30, 36, 4, Rgb(200, 150, 50)); Img.Set(33, 5, FireY);
		Img.Outline(PropOut);
		Halo(Img, 33.f, 6.f, 5.f, Rgb(255, 230, 150), 70);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- smokehouse
	void GableRoof(FFVImage& I, float Apex, int32 TopY, int32 EaveY, int32 X0, int32 X1, FColor Base, bool bMetal)
	{
		for (int32 Y = TopY; Y <= EaveY; ++Y)
		{
			const float T = float(Y - TopY) / FMath::Max(1, EaveY - TopY);
			const float Hw = 1.f + T * ((X1 - X0) * 0.5f);
			for (int32 X = FMath::FloorToInt(Apex - Hw); X <= FMath::CeilToInt(Apex + Hw); ++X)
			{
				if (FMath::Abs(X + 0.5f - Apex) > Hw) { continue; }
				FColor C = Base;
				if (bMetal) { C = (X % 2 == 0) ? FVArt::Light(Base, 0.18f) : FVArt::Shade(Base, 0.12f); }
				else
				{
					if ((Y - TopY) % 2 == 1) { C = FVArt::Shade(Base, 0.25f); }
					else if (((X + (Y / 2) * 2) % 4) == 0) { C = FVArt::Shade(Base, 0.15f); }
				}
				if (X + 0.5f < Apex - Hw + 1.2f) { C = FVArt::Light(C, 0.15f); }
				I.Set(X, Y, C);
			}
		}
	}

	void HangingFish(FFVImage& I, int32 X, int32 Y, FColor Body)
	{
		// hanging by the tail: tail on top, head at the bottom
		I.Set(X - 1, Y, FVArt::Shade(Body, 0.2f)); I.Set(X + 1, Y, FVArt::Shade(Body, 0.2f)); I.Set(X, Y + 1, Body);
		for (int32 R = 2; R <= 6; ++R)
		{
			I.Set(X - 1, Y + R, FVArt::Light(Body, 0.2f)); I.Set(X, Y + R, Body); I.Set(X + 1, Y + R, FVArt::Shade(Body, 0.3f));
		}
		I.Set(X, Y + 7, FVArt::Shade(Body, 0.2f));
		I.Set(X - 1, Y + 5, Rgb(40, 28, 32));
	}

	/** Smokehouse level 3 (30x44): fieldstone walls, a slate roof, a stone chimney, 12 hooks (2 rows of 6 fish) behind
	    a wide opening, a firebox of hard rock with purple veins. Chimney top = StationEffectPoint. */
	FFVImage BuildStoneSmokehouse()
	{
		FFVImage Img(30, 44);
		const FColor Slate(92, 104, 128), Rock(84, 78, 100), Vein(170, 90, 230);
		Stones(Img, 20, 0, 6, 12, Stone, StoneDk, 3.5f, 161u);
		HLine(Img, 19, 26, 0, StoneHi);
		Stones(Img, 2, 13, 26, 22, Stone, StoneDk, 5.f, 163u);
		GableRoof(Img, 15.f, 3, 13, 0, 29, Slate, false);
		HLine(Img, 0, 29, 13, FVArt::Shade(Slate, 0.4f));
		// the opening with two rails of hooks and twelve fish in the smoke
		for (int32 Y = 15; Y <= 33; ++Y) { HLine(Img, 5, 24, Y, Rgb(58, 42, 40)); }
		HLine(Img, 5, 24, 15, StoneHi); VLine(Img, 5, 15, 33, StoneLo); VLine(Img, 24, 15, 33, StoneHi);
		HLine(Img, 6, 23, 16, IronLo); HLine(Img, 6, 23, 24, IronLo);
		static const FColor FishC[3] = { FColor(206, 136, 62), FColor(190, 120, 54), FColor(222, 156, 76) };
		for (int32 R = 0; R < 2; ++R)
		{
			for (int32 K = 0; K < 6; ++K)
			{
				HangingFish(Img, 7 + K * 3, 17 + R * 8, FishC[(K + R) % 3]);
			}
		}
		for (int32 X = 6; X <= 23; ++X) { Img.Blend(X, 17, Rgb(200, 200, 210, 70)); Img.Blend(X, 25, Rgb(200, 200, 210, 60)); }
		// firebox of hard rock
		for (int32 Y = 35; Y <= 43; ++Y)
		{
			for (int32 X = 3; X <= 26; ++X)
			{
				FColor C = FVArt::Noise(X / 2, Y / 2, 165u) < 0.3f ? FVArt::Shade(Rock, 0.15f) : Rock;
				if (Y == 35) { C = FVArt::Light(Rock, 0.3f); }
				Img.Set(X, Y, C);
			}
		}
		Img.Line(4, 38, 9, 36, Vein); Img.Line(20, 42, 25, 39, Vein); Img.Set(15, 43, Vein);
		Box(Img, 10, 37, 10, 6, IronLo, 0.35f, 0.35f);
		HLine(Img, 12, 17, 39, FireO); HLine(Img, 12, 17, 41, FireR); Img.Set(18, 38, Brass);
		Img.Outline(PropOut);
		Halo(Img, 15.f, 40.f, 6.f, Rgb(255, 170, 80), 60);
		return Img;
	}

	FFVImage BuildSmokehouse(int32 Level)
	{
		if (Level >= 3)
		{
			return BuildStoneSmokehouse();
		}
		FFVImage Img(26, 40);
		if (Level <= 1)
		{
			// ---- wooden smoke cabinet with a shingle roof, slatted door and a stone firebox
			Box(Img, 16, 1, 4, 7, Iron, 0.35f, 0.3f); HLine(Img, 15, 20, 0, IronHi);
			Planks(Img, 3, 12, 20, 20, WoodMid, 4, 51u, true);
			GableRoof(Img, 13.f, 2, 12, 1, 24, Rgb(150, 72, 52), false);
			HLine(Img, 1, 24, 12, Rgb(96, 46, 36));
			Box(Img, 7, 15, 12, 16, WoodLo, 0.2f, 0.3f);
			for (int32 Y = 16; Y <= 29; ++Y)
			{
				const bool bGap = (Y % 3) == 1;
				for (int32 X = 8; X <= 17; ++X)
				{
					if (bGap) { Img.Set(X, Y, FVArt::Noise(X, Y, 53u) < 0.35f ? Rgb(214, 150, 66) : Rgb(52, 34, 30)); }
					else { Img.Set(X, Y, (X == 8) ? WoodHi : Wood); }
				}
			}
			Img.Set(17, 22, Brass); Img.Set(17, 23, BrassLo);
			Stones(Img, 3, 31, 20, 8, Stone, StoneDk, 4.5f, 55u);
			for (int32 Y = 33; Y <= 38; ++Y) { HLine(Img, 10, 15, Y, FireBack); }
			HLine(Img, 10, 15, 38, Ember); Img.Set(11, 37, FireO); Img.Set(13, 37, FireR); Img.Set(14, 38, FireO); Img.Set(12, 36, FireR);
			Img.Outline(PropOut);
			Warm(Img, 12.5f, 37.f, 6.f, Rgb(255, 160, 80), 0.35f);
			return Img;
		}
		// ---- level 2: brick base with an iron firebox, metal roof, smoking window with fish, brick chimney
		Bricks(Img, 16, 1, 6, 9, Brick, Mortar, 3, 2, 57u);
		HLine(Img, 15, 22, 0, StoneLo);
		Planks(Img, 2, 12, 22, 17, DWood, 4, 59u, true);
		VLine(Img, 2, 12, 28, IronLo); VLine(Img, 3, 12, 28, IronHi); VLine(Img, 22, 12, 28, IronHi); VLine(Img, 23, 12, 28, IronLo);
		GableRoof(Img, 13.f, 3, 12, 1, 24, Rgb(126, 138, 156), true);
		HLine(Img, 1, 24, 12, IronLo);
		Box(Img, 6, 14, 14, 11, WoodDk, 0.2f, 0.3f);
		Rect(Img, 7, 15, 12, 9, Rgb(62, 46, 42));
		HLine(Img, 7, 18, 15, IronLo);
		HangingFish(Img, 9, 16, Rgb(206, 136, 62));
		HangingFish(Img, 13, 16, Rgb(190, 120, 54));
		HangingFish(Img, 17, 16, Rgb(214, 150, 70));
		for (int32 X = 7; X <= 18; ++X) { Img.Blend(X, 16, Rgb(200, 200, 210, 70)); Img.Blend(X, 17, Rgb(200, 200, 210, 50)); }
		VLine(Img, 20, 16, 19, Rgb(240, 240, 236)); Img.Set(20, 19, Rgb(220, 60, 60)); Img.Set(20, 18, Rgb(220, 60, 60));
		Bricks(Img, 2, 29, 22, 10, Brick, Mortar, 4, 2, 61u);
		Box(Img, 8, 30, 10, 8, Iron, 0.35f, 0.35f);
		HLine(Img, 10, 15, 33, FireO); HLine(Img, 10, 15, 35, FireR);
		Img.Set(16, 31, Brass); Img.Set(16, 32, BrassLo);
		Img.Outline(PropOut);
		Halo(Img, 13.f, 34.f, 5.f, Rgb(255, 170, 80), 60);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- worm farm
	void Soilbed(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, uint32 Seed)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const float N = FVArt::Noise(XX, YY, Seed);
				FColor C = (YY == Y) ? SoilLo : Soil;
				if (N < 0.2f) { C = SoilDk; }
				else if (N > 0.85f) { C = SoilHi; }
				I.Set(XX, YY, C);
			}
		}
	}

	void WormSquiggle(FFVImage& I, int32 X, int32 Y, bool bCrawler = false)
	{
		const FColor A = bCrawler ? Rgb(176, 84, 96) : Worm, B = bCrawler ? Rgb(120, 50, 64) : WormLo;
		I.Set(X, Y + 1, B); I.Set(X + 1, Y, A); I.Set(X + 2, Y, A); I.Set(X + 3, Y + 1, B);
	}

	/** Worm farm level 3 (32x20): a fieldstone trough of dark soil with glowing «глубинные черви», moss on the rim. */
	FFVImage BuildDeepWormTrough()
	{
		FFVImage Img(32, 20);
		const FColor Glow(120, 255, 200), GlowLo(60, 190, 150), Moss(104, 150, 70);
		Stones(Img, 1, 4, 30, 16, Stone, StoneDk, 4.5f, 171u);
		HLine(Img, 1, 30, 4, StoneHi);
		Soilbed(Img, 4, 5, 24, 5, 173u);
		HLine(Img, 4, 27, 5, SoilDk);
		HLine(Img, 1, 30, 10, StoneHi); HLine(Img, 1, 30, 11, StoneLo);
		for (int32 X = 2; X <= 29; X += 3) { if (FVArt::Noise(X, 4, 175u) < 0.5f) { Img.Set(X, 4, Moss); Img.Set(X + 1, 10, Moss); } }
		// the glowing worms
		auto Deep = [&Img, Glow, GlowLo](int32 X, int32 Y)
		{
			Img.Set(X, Y + 1, GlowLo); Img.Set(X + 1, Y, Glow); Img.Set(X + 2, Y, Glow); Img.Set(X + 3, Y + 1, GlowLo);
			Img.Set(X + 4, Y + 1, Glow);
		};
		Deep(6, 6); Deep(14, 7); Deep(21, 6);
		// a stone label with a worm glyph
		Box(Img, 12, 13, 8, 4, StoneHi, 0.3f, 0.3f);
		Glyph(Img, 13, 14, TEXT('W'), GlowLo, Glow);
		Img.Outline(PropOut);
		Halo(Img, 16.f, 7.f, 13.f, Glow, 46);
		return Img;
	}

	FFVImage BuildWormFarm(int32 Level)
	{
		if (Level >= 3)
		{
			return BuildDeepWormTrough();
		}
		FFVImage Img(28, 18);
		if (Level <= 1)
		{
			// ---- wooden crate with soil, worms, a label and a trowel
			Soilbed(Img, 3, 7, 22, 3, 71u);
			Planks(Img, 2, 10, 24, 7, Wood, 3, 73u);
			HLine(Img, 2, 25, 5, WoodHi); HLine(Img, 2, 25, 6, Wood);
			VLine(Img, 2, 5, 16, WoodHi); VLine(Img, 3, 5, 16, WoodMid);
			VLine(Img, 24, 5, 16, WoodMid); VLine(Img, 25, 5, 16, WoodLo);
			Box(Img, 10, 11, 8, 4, Paper, 0.2f, 0.3f);
			WormSquiggle(Img, 12, 12);
			WormSquiggle(Img, 7, 7); WormSquiggle(Img, 16, 8); Img.Set(20, 7, Worm); Img.Set(21, 7, WormLo);
			// trowel
			VLine(Img, 21, 1, 3, WoodHi); VLine(Img, 22, 1, 3, WoodMid); Box(Img, 20, 4, 4, 3, Met, 0.4f, 0.3f);
			Img.Outline(PropOut);
			return Img;
		}
		// ---- two compartments, a propped lid, mushrooms and nightcrawlers
		Planks(Img, 3, 0, 22, 5, WoodMid, 5, 75u, true);
		HLine(Img, 3, 24, 0, WoodHi);
		VLine(Img, 5, 4, 6, WoodDk);
		Soilbed(Img, 2, 7, 24, 3, 77u);
		VLine(Img, 13, 5, 9, WoodLo); VLine(Img, 14, 5, 9, WoodMid);
		Planks(Img, 1, 10, 26, 7, Wood, 3, 79u);
		HLine(Img, 1, 26, 5, WoodHi); HLine(Img, 1, 26, 6, Wood);
		Box(Img, 1, 5, 2, 12, IronLo, 0.4f, 0.3f); Box(Img, 25, 5, 2, 12, IronLo, 0.4f, 0.3f);
		WormSquiggle(Img, 4, 8); WormSquiggle(Img, 8, 7); WormSquiggle(Img, 16, 8, true);
		// mushrooms
		auto Shroom = [&Img](int32 X, int32 Y)
		{
			HLine(Img, X - 1, X + 1, Y, Rgb(196, 120, 72)); Img.Set(X, Y - 1, Rgb(226, 160, 100));
			Img.Set(X, Y + 1, Rgb(240, 232, 214)); Img.Set(X + 1, Y, Rgb(150, 86, 52));
		};
		Shroom(20, 6); Shroom(23, 7);
		Box(Img, 5, 12, 5, 3, Paper, 0.2f, 0.3f); Box(Img, 17, 12, 5, 3, Paper, 0.2f, 0.3f);
		Img.Set(7, 13, Worm); Img.Set(19, 13, Rgb(176, 84, 96));
		Img.Outline(PropOut);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- aquarium
	const FColor WaterTop(150, 214, 240, 235), WaterC(86, 168, 216, 225), WaterLo(60, 128, 190, 230), GlassAir(222, 244, 250, 110);

	void Gravel(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, uint32 Seed)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const float N = FVArt::Noise(XX, YY, Seed);
				I.Set(XX, YY, N < 0.3f ? Rgb(170, 140, 100) : (N < 0.55f ? Rgb(214, 186, 136) : (N < 0.75f ? Rgb(120, 160, 190) : Rgb(236, 214, 170))));
			}
		}
	}

	void WaterBox(FFVImage& I, int32 X, int32 Y, int32 W, int32 H)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			const float T = float(YY - Y) / FMath::Max(1, H - 1);
			const FColor C = YY == Y ? WaterTop : FVArt::Mix(WaterC, WaterLo, T * 0.8f);
			HLine(I, X, X + W - 1, YY, C);
		}
	}

	void WeedStalk(FFVImage& I, int32 X, int32 Y0, int32 Y1, FColor C, uint32 Seed)
	{
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			const int32 Off = ((Y + int32(Seed)) / 2) % 2;
			I.Set(X + Off, Y, (Y % 3 == 0) ? FVArt::Light(C, 0.3f) : C);
		}
	}

	/** Aquarium level 4 «Океанариум» (56x36): a long sea tank with a lamp lid, corals, an anemone, a hard-rock arch
	    and a starfish, on a stand of hard rock with purple veins and a brass plate. Water: AquariumWaterRect(4). */
	FFVImage BuildOceanarium()
	{
		FFVImage Img(56, 36);
		const FColor Rock(84, 78, 100), RockHi(122, 116, 140), RockLo(58, 54, 72), Vein(170, 90, 230), VeinHi(214, 150, 255);
		const FColor Sea(60, 150, 200, 228), SeaLo(30, 90, 160, 234), Sand(232, 210, 160), SandLo(196, 170, 120);
		// stand of hard rock
		for (int32 Y = 23; Y <= 35; ++Y)
		{
			for (int32 X = 2; X <= 53; ++X)
			{
				FColor C = FVArt::Noise(X / 3, Y / 2, 181u) < 0.3f ? RockLo : Rock;
				if (Y == 23) { C = RockHi; }
				else if (X == 2) { C = FVArt::Light(Rock, 0.2f); }
				else if (X == 53 || Y == 35) { C = RockLo; }
				Img.Set(X, Y, C);
			}
		}
		Img.Line(4, 27, 12, 25, Vein); Img.Line(12, 25, 16, 29, Vein); Img.Line(38, 34, 46, 28, Vein); Img.Line(46, 28, 51, 30, Vein);
		Img.Set(8, 26, VeinHi); Img.Set(44, 29, VeinHi);
		Box(Img, 22, 27, 12, 4, Brass, 0.4f, 0.35f); HLine(Img, 24, 31, 29, BrassLo);
		// the tank
		Rect(Img, 1, 3, 54, 20, IronDk);
		for (int32 Y = 4; Y <= 5; ++Y) { HLine(Img, 2, 53, Y, GlassAir); }
		for (int32 Y = 6; Y <= 21; ++Y)
		{
			const float T = float(Y - 6) / 15.f;
			HLine(Img, 2, 53, Y, Y == 6 ? WaterTop : FVArt::Mix(Sea, SeaLo, T));
		}
		for (int32 X = 2; X <= 53; ++X)
		{
			const int32 Top = 19 + (FVArt::Noise(X / 3, 1, 183u) < 0.5f ? 0 : 1);
			for (int32 Y = Top; Y <= 21; ++Y) { Img.Set(X, Y, (FVArt::Noise(X, Y, 185u) < 0.3f) ? SandLo : Sand); }
		}
		// hard-rock arch in the middle
		for (int32 Y = 11; Y <= 20; ++Y)
		{
			for (int32 X = 23; X <= 34; ++X)
			{
				const float DX = (X + 0.5f - 28.5f) / 6.f, DY = (Y + 0.5f - 20.5f) / 9.f;
				const float DXi = (X + 0.5f - 28.5f) / 3.2f, DYi = (Y + 0.5f - 20.5f) / 5.5f;
				if (DX * DX + DY * DY > 1.f || DXi * DXi + DYi * DYi < 1.f) { continue; }
				Img.Set(X, Y, DX < -0.3f ? RockHi : (DX > 0.4f ? RockLo : Rock));
			}
		}
		Img.Set(25, 15, Vein); Img.Set(32, 14, Vein);
		// corals: branching red, orange and pink
		auto Coral = [&Img](int32 X, int32 Base, int32 H, FColor C)
		{
			for (int32 Y = Base - H; Y <= Base; ++Y) { Img.Set(X, Y, C); }
			for (int32 K = 1; K <= 2; ++K)
			{
				const int32 BY = Base - H + K * 2;
				Img.Set(X - K, BY, C); Img.Set(X - K, BY - 1, FVArt::Light(C, 0.25f));
				Img.Set(X + K, BY + 1, C); Img.Set(X + K, BY, FVArt::Light(C, 0.25f));
			}
		};
		Coral(6, 19, 8, Rgb(232, 86, 72)); Coral(12, 19, 6, Rgb(250, 150, 60)); Coral(18, 20, 5, Rgb(236, 110, 170));
		Coral(40, 19, 7, Rgb(250, 150, 60)); Coral(47, 20, 9, Rgb(232, 86, 72));
		// anemone + starfish + weed
		for (int32 K = -2; K <= 2; ++K) { Img.Line(50 + K, 19, 50 + K + FMath::Sign(K), 15, Rgb(250, 180, 210)); }
		Img.Set(36, 20, Rgb(255, 150, 60)); Img.Set(35, 19, Rgb(255, 150, 60)); Img.Set(37, 19, Rgb(255, 150, 60)); Img.Set(36, 18, Rgb(255, 180, 90));
		WeedStalk(Img, 21, 10, 19, Leaf, 13u); WeedStalk(Img, 43, 9, 19, LeafHi, 14u);
		// lamp lid
		Box(Img, 1, 0, 54, 3, IronLo, 0.35f, 0.3f);
		Img.Outline(PropOut);
		HLine(Img, 3, 52, 2, Rgb(255, 244, 190, 210));
		for (int32 Y = 8; Y <= 16; Y += 2) { Img.Blend(15 + ((Y / 2) % 2), Y, Rgb(255, 255, 255, 170)); Img.Blend(44 + ((Y / 2 + 1) % 2), Y - 1, Rgb(255, 255, 255, 150)); }
		VLine(Img, 3, 7, 16, Rgb(255, 255, 255, 110));
		return Img;
	}

	FFVImage BuildAquarium(int32 Level)
	{
		if (Level >= 4)
		{
			return BuildOceanarium();
		}
		if (Level <= 1)
		{
			// ---- Банка: a 3-liter glass jar on a stool
			FFVImage Img(24, 24);
			Planks(Img, 3, 18, 18, 2, WoodHi, 2, 81u);
			VLine(Img, 4, 20, 22, Wood); VLine(Img, 5, 20, 22, WoodLo); VLine(Img, 18, 20, 22, Wood); VLine(Img, 19, 20, 22, WoodLo);
			HLine(Img, 6, 17, 21, WoodMid);
			// glass body (rounded)
			for (int32 Y = 5; Y <= 17; ++Y)
			{
				for (int32 X = 5; X <= 18; ++X)
				{
					const bool bCorner = (Y <= 5 || Y >= 17) && (X <= 5 || X >= 18);
					if (bCorner) { continue; }
					Img.Set(X, Y, GlassAir);
				}
			}
			for (int32 Y = 3; Y <= 4; ++Y) { HLine(Img, 7, 16, Y, GlassAir); }
			WaterBox(Img, 6, 7, 12, 10);
			Gravel(Img, 6, 15, 12, 2, 83u);
			WeedStalk(Img, 8, 10, 14, Leaf, 1u); WeedStalk(Img, 15, 12, 14, LeafLo, 2u);
			VLine(Img, 5, 6, 16, Rgb(190, 220, 230, 200)); VLine(Img, 18, 6, 16, Rgb(120, 170, 190, 220));
			Box(Img, 6, 1, 12, 2, Met, 0.4f, 0.35f);
			Img.Outline(PropOut);
			VLine(Img, 7, 8, 13, Rgb(255, 255, 255, 150));
			Img.Set(8, 4, Rgb(255, 255, 255, 170));
			return Img;
		}
		if (Level == 2)
		{
			// ---- Аквариум: tank with a metal frame on a wooden cabinet
			FFVImage Img(32, 28);
			Planks(Img, 2, 19, 28, 8, DWood, 14, 85u, true);
			HLine(Img, 2, 29, 19, DWoodHi); VLine(Img, 15, 20, 26, DWoodDk); VLine(Img, 16, 20, 26, DWoodLo);
			Img.Set(13, 23, Brass); Img.Set(18, 23, Brass);
			Rect(Img, 1, 3, 30, 16, IronDk);
			for (int32 Y = 4; Y <= 5; ++Y) { HLine(Img, 2, 29, Y, GlassAir); }
			WaterBox(Img, 2, 6, 28, 12);
			Gravel(Img, 2, 15, 28, 3, 87u);
			WeedStalk(Img, 4, 8, 15, Leaf, 3u); WeedStalk(Img, 6, 10, 15, LeafLo, 4u);
			WeedStalk(Img, 25, 9, 15, Leaf, 5u); WeedStalk(Img, 27, 11, 15, LeafHi, 6u);
			Box(Img, 18, 13, 5, 3, StoneLo, 0.35f, 0.3f);
			Box(Img, 1, 1, 30, 2, IronLo, 0.35f, 0.3f);
			Img.Outline(PropOut);
			for (int32 Y = 7; Y <= 12; Y += 2) { Img.Blend(23 + (Y % 4 == 1 ? 1 : 0), Y, Rgb(255, 255, 255, 170)); }
			VLine(Img, 3, 7, 13, Rgb(255, 255, 255, 110));
			return Img;
		}
		// ---- Большой аквариум: long tank with a lamp lid, castle, plants and a treasure chest
		FFVImage Img(44, 30);
		Planks(Img, 2, 21, 40, 8, DWood, 13, 89u, true);
		HLine(Img, 2, 41, 21, DWoodHi);
		Img.Set(14, 25, Brass); Img.Set(28, 25, Brass); Img.Set(40, 25, Brass);
		Rect(Img, 1, 3, 42, 18, IronDk);
		for (int32 Y = 4; Y <= 5; ++Y) { HLine(Img, 2, 41, Y, GlassAir); }
		WaterBox(Img, 2, 6, 40, 14);
		Gravel(Img, 2, 17, 40, 3, 91u);
		// castle
		Rect(Img, 31, 12, 9, 6, Stone); Rect(Img, 31, 9, 3, 3, Stone); Rect(Img, 37, 9, 3, 3, Stone);
		Img.Set(31, 8, Stone); Img.Set(33, 8, Stone); Img.Set(37, 8, Stone); Img.Set(39, 8, Stone);
		VLine(Img, 31, 9, 17, StoneHi); VLine(Img, 39, 9, 17, StoneLo);
		Rect(Img, 34, 14, 3, 4, Rgb(40, 50, 70)); Img.Set(35, 13, Rgb(40, 50, 70));
		Img.Set(32, 10, Rgb(40, 50, 70)); Img.Set(38, 10, Rgb(40, 50, 70));
		// plants, chest, rock
		WeedStalk(Img, 4, 8, 17, Leaf, 7u); WeedStalk(Img, 6, 10, 17, LeafHi, 8u); WeedStalk(Img, 8, 12, 17, LeafLo, 9u);
		WeedStalk(Img, 21, 13, 17, Rgb(200, 80, 90), 10u); WeedStalk(Img, 23, 12, 17, Rgb(230, 120, 110), 11u);
		WeedStalk(Img, 41, 10, 17, Leaf, 12u);
		Box(Img, 14, 15, 5, 3, Rgb(150, 96, 50), 0.3f, 0.3f); HLine(Img, 14, 18, 15, Rgb(255, 204, 51)); Img.Set(16, 16, Rgb(255, 204, 51));
		Box(Img, 26, 15, 4, 2, StoneLo, 0.35f, 0.3f);
		Box(Img, 1, 1, 42, 2, IronLo, 0.35f, 0.3f);
		Img.Outline(PropOut);
		HLine(Img, 3, 40, 3, Rgb(255, 244, 190, 200));
		for (int32 Y = 8; Y <= 15; Y += 2) { Img.Blend(10 + ((Y / 2) % 2), Y, Rgb(255, 255, 255, 170)); Img.Blend(28 + ((Y / 2 + 1) % 2), Y - 1, Rgb(255, 255, 255, 150)); }
		VLine(Img, 3, 7, 15, Rgb(255, 255, 255, 110));
		return Img;
	}

	// ---------------------------------------------------------------------------------------- ice cellar
	/** The ice cellar is a hatch in the kitchen floor (§6.4, §8.1; top view, flat). L1: a plank lid in a frame with
	    an iron ring and hinges · L2: + iron bands, a padlock and frost · L3 (28x22): + a fieldstone rim and cold blue
	    light from the seams. */
	FFVImage BuildIceCellar(int32 Level)
	{
		const FColor Snow(244, 250, 255), SnowLo(200, 226, 244), Ice(190, 236, 255), Glow(140, 224, 255);
		const FColor Lid(150, 112, 74), Gap(40, 28, 26);
		const bool bStone = Level >= 3;
		FFVImage Img(bStone ? 28 : 24, bStone ? 22 : 18);
		const int32 FX = bStone ? 2 : 0, FY = bStone ? 2 : 0;       // the wooden frame of the opening (24x16)
		if (bStone)
		{
			Stones(Img, 0, 0, 28, 20, Stone, StoneDk, 4.5f, 221u);
			HLine(Img, 0, 27, 0, StoneHi);
			HLine(Img, 0, 27, 20, StoneLo); HLine(Img, 0, 27, 21, StoneDk);
		}
		else
		{
			HLine(Img, 0, 23, 16, DWoodDk); HLine(Img, 0, 23, 17, Rgb(52, 34, 28));
		}
		Rect(Img, FX, FY, 24, 16, DWoodLo);
		HLine(Img, FX, FX + 23, FY, DWoodDk); VLine(Img, FX, FY, FY + 15, DWoodDk);
		HLine(Img, FX + 1, FX + 23, FY + 15, DWood); VLine(Img, FX + 23, FY + 1, FY + 15, DWood);
		// the gap around the lid (cold light at level 3)
		const FColor GapC = bStone ? Glow : Gap;
		HLine(Img, FX + 1, FX + 22, FY + 1, GapC); HLine(Img, FX + 1, FX + 22, FY + 14, GapC);
		VLine(Img, FX + 1, FY + 1, FY + 14, GapC); VLine(Img, FX + 22, FY + 1, FY + 14, GapC);
		// the lid: horizontal boards and two battens
		Planks(Img, FX + 2, FY + 2, 20, 12, Lid, 3, 223u + uint32(Level));
		if (bStone)
		{
			for (int32 Y = FY + 4; Y <= FY + 13; Y += 3) { HLine(Img, FX + 2, FX + 21, Y, Glow); }
		}
		for (int32 BX : { FX + 5, FX + 16 })
		{
			VLine(Img, BX, FY + 2, FY + 13, FVArt::Light(Lid, 0.12f)); VLine(Img, BX + 1, FY + 2, FY + 13, FVArt::Shade(Lid, 0.3f));
		}
		// hinges on the far edge
		for (int32 HX : { FX + 4, FX + 15 })
		{
			HLine(Img, HX, HX + 3, FY + 2, IronHi); HLine(Img, HX, HX + 3, FY + 3, IronLo);
		}
		if (Level >= 2)
		{
			// iron bands with rivets
			for (int32 BY : { FY + 5, FY + 11 })
			{
				HLine(Img, FX + 2, FX + 21, BY, Iron); HLine(Img, FX + 2, FX + 21, BY + 1, IronLo);
				for (int32 X = FX + 3; X <= FX + 21; X += 5) { Img.Set(X, BY, MetHi); }
			}
		}
		// the ring handle
		Rect(Img, FX + 11, FY + 6, 3, 2, IronLo);
		HLine(Img, FX + 10, FX + 14, FY + 8, Iron); Img.Set(FX + 10, FY + 8, IronHi);
		Img.Set(FX + 9, FY + 9, Iron); Img.Set(FX + 15, FY + 9, IronLo);
		HLine(Img, FX + 10, FX + 14, FY + 10, IronLo);
		if (Level >= 2)
		{
			Img.Set(FX + 19, FY + 7, IronHi); Img.Set(FX + 20, FY + 7, IronHi);
			Rect(Img, FX + 19, FY + 8, 2, 2, Brass); Img.Set(FX + 19, FY + 8, BrassHi);
		}
		// frost / cold breath from the seams
		if (Level >= 2)
		{
			for (int32 Y = FY + 2; Y <= FY + 13; ++Y)
			{
				for (int32 X = FX + 2; X <= FX + 21; ++X)
				{
					const bool bEdge = X <= FX + 3 || X >= FX + 20 || Y <= FY + 3 || Y >= FY + 12;
					const float N = FVArt::Noise(X, Y, 227u);
					if (bEdge && N < 0.32f) { Img.Set(X, Y, N < 0.12f ? Snow : SnowLo); }
					else if (N < 0.05f) { Img.Set(X, Y, Ice); }
				}
			}
		}
		else
		{
			for (int32 X = FX + 2; X <= FX + 21; X += 3) { Img.Blend(X, FY + 14, Rgb(220, 240, 255, 150)); }
		}
		if (bStone)
		{
			for (int32 X = 1; X <= 26; X += 2) { if (FVArt::Noise(X, 0, 229u) < 0.5f) { Img.Set(X, 1, SnowLo); } }
			Halo(Img, 14.f, 10.f, 13.f, Glow, 64);
		}
		return Img;
	}

	// ---------------------------------------------------------------------------------------- radio
	/** The комод the radio stands on: an overhanging top, two drawers with brass knobs, short feet. */
	void RadioCommode(FFVImage& Img, int32 X, int32 Y, int32 W, int32 H)
	{
		Box(Img, X + 1, Y + 1, W - 2, H - 3, DWood, 0.2f, 0.35f);
		HLine(Img, X, X + W - 1, Y, DWoodHi); HLine(Img, X, X + W - 1, Y + 1, DWood);
		const int32 DH = (H - 6) / 2;
		for (int32 D = 0; D < 2; ++D)
		{
			const int32 DY = Y + 3 + D * (DH + 1);
			Box(Img, X + 3, DY, W - 6, DH, DWoodHi, 0.25f, 0.35f);
			Img.Set(X + W / 2 - 4, DY + DH / 2, BrassHi); Img.Set(X + W / 2 + 3, DY + DH / 2, BrassHi);
		}
		VLine(Img, X + 2, Y + H - 2, Y + H - 1, DWoodLo); VLine(Img, X + W - 3, Y + H - 2, Y + H - 1, DWoodLo);
	}

	/** Radio level 2 «Радиостанция» (28x28): a receiver with a green meter and a tuning dial, a headset, a Morse key,
	    a telescopic antenna and the map of the City pinned on the wall behind. Meter glow = StationEffectPoint. */
	FFVImage BuildRadioStation()
	{
		FFVImage Img(28, 28);
		const FColor Body(92, 116, 100), Glow(140, 240, 150), SeaC(110, 160, 210), LandC(150, 190, 110);
		// the map of the City on the wall
		Box(Img, 0, 0, 12, 10, Paper, 0.15f, 0.3f);
		for (int32 Y = 2; Y <= 8; ++Y)
		{
			for (int32 X = 1; X <= 10; ++X)
			{
				if (float(X) + float(Y) * 0.6f > 10.f) { Img.Set(X, Y, SeaC); }
				else if (FVArt::Noise(X / 2, Y / 2, 231u) < 0.45f) { Img.Set(X, Y, LandC); }
			}
		}
		Img.Set(3, 5, Scribble); Img.Set(4, 6, Scribble); Img.Set(6, 6, Scribble); Img.Set(7, 7, Scribble);   // the route
		Img.Set(3, 3, Rgb(220, 50, 50)); Img.Set(3, 4, Rgb(150, 30, 30));                                   // the pin
		RadioCommode(Img, 0, 14, 28, 14);
		// the receiver
		Box(Img, 7, 5, 14, 9, Body, 0.3f, 0.35f);
		Rect(Img, 9, 7, 5, 3, Rgb(30, 40, 34)); HLine(Img, 10, 12, 8, Glow); Img.Set(11, 7, Glow);
		Ellipse(Img, 17.f, 9.f, 2.4f, 2.4f, Met); Img.Set(17, 9, IronDk); Img.Set(16, 8, MetHi);
		for (int32 X = 9; X <= 13; X += 2) { Img.Set(X, 12, IronHi); }
		// headset on the комод, Morse key
		Img.Set(2, 12, IronDk); HLine(Img, 3, 5, 11, IronDk); Img.Set(6, 12, IronDk);
		HLine(Img, 1, 2, 13, IronLo); HLine(Img, 6, 7, 13, IronLo);
		HLine(Img, 21, 26, 13, DWoodDk); Img.Line(22, 12, 26, 12, Brass); Img.Set(22, 11, BrassHi);
		// antenna
		Img.Line(19, 4, 24, 0, Met); Img.Set(20, 3, MetHi);
		Img.Outline(PropOut);
		Img.Set(24, 0, Rgb(220, 60, 60));
		Halo(Img, 11.f, 8.f, 4.f, Glow, 60);
		return Img;
	}

	/** Radio level 1 (24x26): a wooden cabinet radio with a cloth speaker and a lit dial on a комод with a doily. */
	FFVImage BuildRadio(int32 Level)
	{
		if (Level >= 2)
		{
			return BuildRadioStation();
		}
		FFVImage Img(24, 26);
		RadioCommode(Img, 0, 12, 24, 14);
		HLine(Img, 5, 18, 12, Rgb(250, 248, 240));
		for (int32 X = 5; X <= 18; X += 2) { Img.Set(X, 13, Rgb(236, 232, 222)); }
		Box(Img, 4, 2, 16, 10, Rgb(150, 92, 52), 0.3f, 0.35f);
		Img.Set(4, 2, FColor(0, 0, 0, 0)); Img.Set(19, 2, FColor(0, 0, 0, 0));
		for (int32 Y = 4; Y <= 10; ++Y)
		{
			for (int32 X = 6; X <= 11; ++X) { Img.Set(X, Y, ((X + Y) % 2 == 0) ? Rgb(222, 198, 150) : Rgb(176, 146, 104)); }
		}
		Rect(Img, 13, 4, 5, 3, Rgb(250, 236, 200)); VLine(Img, 15, 4, 6, Rgb(210, 50, 50));
		Rect(Img, 13, 8, 2, 2, Brass); Img.Set(13, 8, BrassHi);
		Rect(Img, 16, 8, 2, 2, Brass); Img.Set(16, 8, BrassHi);
		Img.Line(18, 1, 21, 0, MetLo);
		Img.Outline(PropOut);
		Img.Set(22, 0, Rgb(220, 60, 60));
		Halo(Img, 15.5f, 5.f, 4.f, Rgb(255, 230, 160), 60);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- bed
	/** Bed (28x40), top view with the headboard up: carved headboard, pillow, turned-down sheet, patchwork quilt,
	    footboard with its front face. It stands with the headboard against the back wall. */
	FFVImage BuildBed()
	{
		FFVImage Img(28, 40);
		static const FColor Patches[4] = { FColor(204, 72, 62), FColor(238, 212, 152), FColor(80, 132, 194), FColor(232, 152, 72) };
		// headboard
		for (int32 Y = 1; Y <= 8; ++Y) { HLine(Img, 2, 25, Y, DWood); }
		HLine(Img, 5, 22, 0, DWoodHi); HLine(Img, 2, 25, 1, DWoodHi);
		for (int32 X = 5; X <= 22; X += 3) { VLine(Img, X, 3, 7, DWoodHi); VLine(Img, X + 1, 3, 7, DWoodLo); }
		HLine(Img, 2, 25, 8, DWoodLo);
		// mattress, pillow, sheet
		for (int32 Y = 9; Y <= 33; ++Y) { HLine(Img, 2, 25, Y, Rgb(244, 240, 230)); }
		Box(Img, 4, 9, 20, 6, Rgb(248, 246, 238), 0.25f, 0.28f);
		VLine(Img, 14, 10, 13, Rgb(214, 208, 198));
		HLine(Img, 2, 25, 15, Rgb(250, 248, 240)); HLine(Img, 2, 25, 16, Rgb(232, 228, 218)); HLine(Img, 2, 25, 17, Rgb(206, 200, 190));
		// patchwork quilt
		for (int32 Y = 18; Y <= 33; ++Y)
		{
			for (int32 X = 2; X <= 25; ++X)
			{
				const int32 P = ((X - 2) / 4 + (Y - 18) / 4) % 4;
				FColor C = Patches[P];
				if ((X - 2) % 4 == 0 && (Y - 18) % 4 == 0) { C = FVArt::Light(C, 0.3f); }
				if (X >= 24 || Y == 33) { C = FVArt::Shade(C, 0.28f); }
				else if (X <= 3) { C = FVArt::Light(C, 0.12f); }
				Img.Set(X, Y, C);
			}
		}
		// corner posts and side rails
		for (int32 PX : { 0, 26 })
		{
			VLine(Img, PX, 0, 39, DWoodHi); VLine(Img, PX + 1, 0, 39, DWoodLo);
			Img.Set(PX, 0, BrassHi); Img.Set(PX + 1, 0, Brass);
		}
		// footboard with its front face
		HLine(Img, 2, 25, 34, DWoodHi);
		for (int32 Y = 35; Y <= 38; ++Y) { HLine(Img, 2, 25, Y, DWood); }
		Rect(Img, 4, 36, 9, 2, DWoodLo); Rect(Img, 15, 36, 9, 2, DWoodLo);
		HLine(Img, 2, 25, 39, DWoodDk);
		Img.Outline(PropOut);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- trophy wall
	/** Plaque slot centres (texels from the top-left): 0..5 = L1 (3x2 in a 48-wide board), 6..11 = the right half of
	    the L2 board (96 wide). The L2 board keeps slots 0..5 where L1 has them. */
	const FVector2D TrophySlots[12] = {
		FVector2D(8.f, 6.5f), FVector2D(24.f, 6.5f), FVector2D(40.f, 6.5f),
		FVector2D(8.f, 17.5f), FVector2D(24.f, 17.5f), FVector2D(40.f, 17.5f),
		FVector2D(56.f, 6.5f), FVector2D(72.f, 6.5f), FVector2D(88.f, 6.5f),
		FVector2D(56.f, 17.5f), FVector2D(72.f, 17.5f), FVector2D(88.f, 17.5f) };

	FFVImage BuildTrophyWall(int32 Level)
	{
		// Vertical board panel with a carved top trim, a brass fish emblem, a brass name plate and plaque nails.
		// Level 2: a twice wider dark-oak board with a carved pilaster between the halves and brass corner caps.
		const bool bBig = Level >= 2;
		const int32 W = bBig ? 96 : 48;
		FFVImage Img(W, 24);
		Planks(Img, 1, 3, W - 2, 18, bBig ? DWoodHi : WoodMid, 4, 101u, true);
		HLine(Img, 0, W - 1, 0, DWoodHi); HLine(Img, 0, W - 1, 1, DWood); HLine(Img, 0, W - 1, 2, DWoodLo);
		for (int32 X = 1; X <= W - 2; X += 3) { Img.Set(X, 3, DWoodLo); }   // dentil shadow under the trim
		HLine(Img, 0, W - 1, 21, DWoodHi); HLine(Img, 0, W - 1, 22, DWood); HLine(Img, 0, W - 1, 23, DWoodLo);
		const int32 MX = W / 2;
		HLine(Img, MX - 3, MX + 1, 0, BrassHi); HLine(Img, MX - 4, MX + 2, 1, Brass); HLine(Img, MX - 3, MX + 1, 2, BrassLo);
		Img.Set(MX + 3, 0, Brass); Img.Set(MX + 3, 2, Brass); Img.Set(MX - 3, 1, IronDk);
		const FColor NailShade = FVArt::Shade(bBig ? DWoodHi : WoodMid, 0.35f);
		for (int32 I = 0; I < (bBig ? 12 : 6); ++I)
		{
			const int32 CX = FMath::FloorToInt(TrophySlots[I].X), CY = FMath::FloorToInt(TrophySlots[I].Y);
			Img.Set(CX, CY - 2, IronHi); Img.Set(CX, CY - 1, NailShade);
		}
		if (bBig)
		{
			VLine(Img, 47, 3, 20, DWoodLo); VLine(Img, 48, 3, 20, DWood);
			for (int32 Y = 5; Y <= 19; Y += 4) { Img.Set(47, Y, DWoodHi); }
			Rect(Img, 40, 21, 16, 3, Brass); HLine(Img, 40, 55, 21, BrassHi); HLine(Img, 42, 53, 22, BrassLo);
			for (int32 CX : { 0, W - 2 })
			{
				Rect(Img, CX, 0, 2, 3, Brass); Img.Set(CX, 0, BrassHi);
				Rect(Img, CX, 21, 2, 3, Brass); Img.Set(CX, 21, BrassHi);
			}
		}
		else
		{
			Rect(Img, 19, 21, 10, 3, Brass); HLine(Img, 19, 28, 21, BrassHi); HLine(Img, 21, 26, 22, BrassLo);
		}
		Img.Outline(PropOut);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- chest
	FFVImage BuildHouseChest()
	{
		FFVImage Img(20, 16);
		HLine(Img, 4, 15, 1, WoodHi);
		Planks(Img, 2, 2, 16, 3, Wood, 3, 103u);
		HLine(Img, 3, 16, 2, WoodHi);
		HLine(Img, 2, 17, 5, WoodMid); HLine(Img, 2, 17, 6, WoodDk);
		Planks(Img, 2, 7, 16, 7, Wood, 3, 105u);
		HLine(Img, 2, 17, 13, WoodLo);
		for (int32 BX : { 4, 14 })
		{
			VLine(Img, BX, 2, 13, IronHi); VLine(Img, BX + 1, 2, 13, IronLo);
		}
		Rect(Img, 9, 5, 2, 4, Brass); Img.Set(9, 5, BrassHi); Img.Set(9, 7, IronDk); Img.Set(10, 7, IronDk);
		HLine(Img, 3, 4, 14, WoodLo); HLine(Img, 15, 16, 14, WoodLo);
		Img.Outline(PropOut);
		return Img;
	}
}

namespace FVArtHomeImpl
{
	// ---------------------------------------------------------------------------------------- traders & stalls
	/** Head + shoulders of a trader (16x17), placed behind the counter. */
	FFVImage TraderBust(bool bPier)
	{
		if (!bPier)
		{
			// Дед Митрич: yellow sou'wester, bushy white beard, blue knitted sweater, a pipe
			FFVImage Img = Ascii({
				TEXT("................"),
				TEXT("....oooooooo...."),
				TEXT("...ohlhhhhhhHo.."),
				TEXT("..ohlhhhhhhhhHo."),
				TEXT(".oHHHHHHHHHHHHHo"),
				TEXT("..osWWssssWWso.."),
				TEXT("..osesssssesSo.."),
				TEXT("..osrrsnnsrrSo.."),
				TEXT(".owsssnnnnssSWo."),
				TEXT(".owwwwwwwwwwwWo."),
				TEXT(".owwwwWeewWwwWo."),
				TEXT(".owwwwwwwwwwwWo."),
				TEXT(".okcccowwwwocCo."),
				TEXT("okcccccowwocccCo"),
				TEXT("okccccccooccccCo"),
				TEXT("okckckckckckckCo"),
				TEXT("okccccccccccccCo"),
			}, {
				{ TEXT('o'), Rgb(58, 34, 26) }, { TEXT('h'), Rgb(246, 196, 58) }, { TEXT('l'), Rgb(255, 234, 136) },
				{ TEXT('H'), Rgb(204, 142, 34) }, { TEXT('s'), Rgb(240, 186, 138) }, { TEXT('S'), Rgb(212, 150, 110) },
				{ TEXT('r'), Rgb(236, 140, 122) }, { TEXT('n'), Rgb(226, 124, 100) }, { TEXT('e'), Rgb(44, 30, 40) },
				{ TEXT('w'), Rgb(246, 242, 234) }, { TEXT('W'), Rgb(198, 194, 196) }, { TEXT('c'), Rgb(62, 92, 150) },
				{ TEXT('C'), Rgb(42, 62, 110) }, { TEXT('k'), Rgb(98, 132, 192) } });
			// pipe
			Img.Set(11, 10, Rgb(134, 78, 42)); Img.Set(12, 10, Rgb(134, 78, 42));
			Img.Set(13, 9, Rgb(134, 78, 42)); Img.Set(13, 10, Rgb(86, 48, 28)); Img.Set(13, 8, Rgb(236, 116, 44));
			Img.Set(14, 8, Rgb(58, 34, 26)); Img.Set(14, 9, Rgb(58, 34, 26)); Img.Set(14, 10, Rgb(58, 34, 26));
			return Img;
		}
		// Боцман Семёныч: captain's cap with a gold badge, dark beard, navy jacket over a striped shirt
		return Ascii({
			TEXT("................"),
			TEXT("...oooooooooo..."),
			TEXT("..oaaaaaaaaaAo.."),
			TEXT("..oaaaaggaaaAo.."),
			TEXT(".obbbbbbbbbbbbo."),
			TEXT("..osssssssssSo.."),
			TEXT("..osesssssesSo.."),
			TEXT("..odrrsnnsrrdo.."),
			TEXT(".oddsssnnsssddo."),
			TEXT(".odddddddddddDo."),
			TEXT(".odddDeeDddddDo."),
			TEXT("..oddddddddDo..."),
			TEXT(".ojjjoddddojjjo."),
			TEXT("ojjjjtoddotjjjJo"),
			TEXT("ojjjjTtooTtjjjJo"),
			TEXT("ojjjjtTttTtjjjJo"),
			TEXT("ojjjjTTTTTTjjjJo"),
		}, {
			{ TEXT('o'), Rgb(40, 32, 38) }, { TEXT('a'), Rgb(242, 242, 238) }, { TEXT('A'), Rgb(196, 200, 212) },
			{ TEXT('b'), Rgb(38, 38, 50) }, { TEXT('g'), Rgb(255, 204, 51) }, { TEXT('s'), Rgb(236, 180, 136) },
			{ TEXT('S'), Rgb(206, 146, 108) }, { TEXT('r'), Rgb(230, 136, 118) }, { TEXT('n'), Rgb(220, 120, 98) },
			{ TEXT('e'), Rgb(40, 28, 36) }, { TEXT('d'), Rgb(104, 80, 66) }, { TEXT('D'), Rgb(72, 54, 46) },
			{ TEXT('j'), Rgb(46, 60, 104) }, { TEXT('J'), Rgb(30, 40, 74) }, { TEXT('t'), Rgb(242, 242, 238) },
			{ TEXT('T'), Rgb(54, 94, 172) } });
	}

	FFVImage BuildStallImpl(bool bPier)
	{
		FFVImage Img(56, 48);
		const FColor StripeA = bPier ? Rgb(58, 108, 188) : Rgb(206, 62, 58);
		const FColor StripeB = Rgb(244, 238, 224);
		// back panel (behind the trader), darker under the awning
		Planks(Img, 6, 14, 44, 18, DWoodLo, 5, bPier ? 111u : 113u, true);
		for (int32 X = 6; X <= 49; ++X) { Img.Set(X, 14, DWoodDk); Img.Set(X, 15, FVArt::Shade(DWoodLo, 0.25f)); }
		// hanging goods on the back panel
		if (!bPier)
		{
			// dried fish on a string + a small net
			HLine(Img, 8, 17, 17, Rgb(200, 180, 140));
			for (int32 I = 0; I < 3; ++I) { HangingFish(Img, 9 + I * 3, 18, Rgb(196, 150, 90)); }
			for (int32 Y = 17; Y <= 24; ++Y) { for (int32 X = 39; X <= 47; ++X) { if ((X + Y) % 3 == 0 || (X - Y + 30) % 3 == 0) { Img.Set(X, Y, Rgb(206, 190, 150)); } } }
			HLine(Img, 39, 47, 17, Rgb(236, 116, 52));
		}
		else
		{
			// rope coil + a lantern
			for (int32 Y = 17; Y <= 23; ++Y)
			{
				for (int32 X = 8; X <= 15; ++X)
				{
					const float DX = X + 0.5f - 12.f, DY = Y + 0.5f - 20.f;
					const float D = FMath::Sqrt(DX * DX + DY * DY);
					if (D >= 1.5f && D <= 3.8f) { Img.Set(X, Y, ((X + Y) % 3 == 0) ? Rgb(160, 112, 66) : Rgb(214, 170, 108)); }
				}
			}
			VLine(Img, 44, 16, 17, IronLo);
			Box(Img, 42, 18, 5, 6, Brass, 0.3f, 0.3f);
			Rect(Img, 43, 19, 3, 4, Rgb(255, 220, 120)); Img.Set(44, 20, FireY);
		}
		// trader
		Img.Blit(TraderBust(bPier), 20, 15);
		// posts
		for (int32 PX : { 3, 50 })
		{
			VLine(Img, PX, 6, 45, WoodHi); VLine(Img, PX + 1, 6, 45, Wood); VLine(Img, PX + 2, 6, 45, WoodLo);
		}
		// awning beam + striped fabric with scalloped edge
		HLine(Img, 1, 54, 4, WoodHi); HLine(Img, 1, 54, 5, WoodMid);
		for (int32 X = 1; X <= 54; ++X)
		{
			const int32 S = (X - 1) / 5, P = (X - 1) % 5;
			const FColor Base = (S % 2 == 0) ? StripeA : StripeB;
			for (int32 Y = 6; Y <= 13; ++Y)
			{
				FColor C = Base;
				if (Y == 6) { C = FVArt::Light(Base, 0.2f); }
				else if (P == 4) { C = FVArt::Shade(Base, 0.15f); }
				Img.Set(X, Y, C);
			}
			if (P >= 1 && P <= 3) { Img.Set(X, 14, FVArt::Shade(Base, 0.12f)); }
			if (P == 2) { Img.Set(X, 15, FVArt::Shade(Base, 0.25f)); }
		}
		// sign on top of the beam
		Box(Img, 19, 0, 18, 5, WoodHi, 0.25f, 0.35f);
		if (bPier) { Glyph(Img, 26, 0, TEXT('A'), Rgb(40, 56, 110), Rgb(40, 56, 110)); }
		else
		{
			Glyph(Img, 24, 1, TEXT('F'), Rgb(236, 120, 50), Rgb(40, 28, 36));
			Img.Set(31, 2, Rgb(236, 120, 50));
		}
		// counter
		for (int32 X = 1; X <= 54; ++X)
		{
			Img.Set(X, 31, WoodHi); Img.Set(X, 32, Wood); Img.Set(X, 33, Wood); Img.Set(X, 34, WoodLo);
		}
		Planks(Img, 2, 35, 52, 11, Wood, 5, bPier ? 115u : 117u, true);
		HLine(Img, 2, 53, 35, WoodMid); HLine(Img, 2, 53, 45, WoodDk);
		// cloth banner on the counter front
		const FColor Ban = bPier ? Rgb(44, 70, 130) : Rgb(70, 132, 80);
		Box(Img, 20, 36, 16, 7, Ban, 0.25f, 0.3f);
		for (int32 X = 20; X <= 35; X += 3) { Img.Set(X + 1, 43, Ban); }
		if (bPier) { Glyph(Img, 26, 37, TEXT('A'), Rgb(240, 236, 220), Rgb(240, 236, 220)); }
		else { Glyph(Img, 24, 38, TEXT('F'), Rgb(250, 220, 120), Rgb(40, 28, 36)); Img.Set(31, 39, Rgb(250, 220, 120)); }
		// goods on the counter
		if (!bPier)
		{
			// worm jar
			Rect(Img, 7, 25, 6, 6, Rgb(214, 238, 244));
			Rect(Img, 8, 28, 4, 3, Soil); WormSquiggle(Img, 8, 26); Img.Set(10, 29, Worm);
			VLine(Img, 7, 25, 30, Rgb(236, 248, 252)); HLine(Img, 7, 12, 24, Met); HLine(Img, 7, 12, 25, MetLo);
			// bucket with a fish tail
			Rect(Img, 14, 26, 6, 5, Met); VLine(Img, 14, 26, 30, MetHi); VLine(Img, 19, 26, 30, MetLo); HLine(Img, 14, 19, 26, MetHi);
			Img.Set(16, 25, Rgb(96, 150, 90)); Img.Set(17, 24, Rgb(96, 150, 90)); Img.Set(15, 24, Rgb(120, 176, 110));
			Img.Line(13, 26, 16, 22, IronLo); Img.Line(20, 26, 17, 22, IronLo);
			// fish basket
			for (int32 Y = 27; Y <= 30; ++Y) { for (int32 X = 37; X <= 46; ++X) { Img.Set(X, Y, ((X + Y) % 2 == 0) ? Rgb(206, 160, 96) : Rgb(160, 112, 62)); } }
			HLine(Img, 37, 46, 27, Rgb(226, 186, 120));
			for (int32 I = 0; I < 3; ++I)
			{
				const int32 FX = 39 + I * 3;
				Img.Set(FX, 25, Rgb(200, 206, 216)); Img.Set(FX, 26, Rgb(160, 170, 186)); Img.Set(FX + 1, 26, Rgb(200, 206, 216));
				Img.Set(FX, 25, Rgb(214, 220, 230));
			}
			// rods leaning on the right post
			Img.Line(45, 30, 51, 8, Rgb(226, 190, 110));
			Img.Line(47, 30, 53, 11, Rgb(150, 100, 60));
			Img.Line(48, 30, 54, 14, Rgb(90, 120, 180));
		}
		else
		{
			// crate with fish on ice
			Box(Img, 36, 26, 12, 5, Wood, 0.3f, 0.35f);
			for (int32 X = 37; X <= 46; ++X) { Img.Set(X, 26, (X % 3 == 0) ? Rgb(200, 230, 245) : Rgb(244, 250, 255)); }
			for (int32 I = 0; I < 3; ++I)
			{
				const int32 FX = 37 + I * 3;
				HLine(Img, FX, FX + 2, 25, I == 1 ? Rgb(226, 150, 140) : Rgb(196, 204, 216));
				Img.Set(FX + 3, 24, I == 1 ? Rgb(200, 120, 110) : Rgb(150, 160, 176));
			}
			// rope coil on the counter
			for (int32 Y = 27; Y <= 30; ++Y)
			{
				for (int32 X = 7; X <= 15; ++X)
				{
					const float DX = (X + 0.5f - 11.f) / 4.2f, DY = (Y + 0.5f - 29.f) / 2.f;
					const float D = DX * DX + DY * DY;
					if (D <= 1.f && D >= 0.25f) { Img.Set(X, Y, ((X + Y) % 3 == 0) ? Rgb(160, 112, 66) : Rgb(214, 170, 108)); }
				}
			}
			// lifebuoy on the left post
			for (int32 Y = 17; Y <= 27; ++Y)
			{
				for (int32 X = 0; X <= 9; ++X)
				{
					const float DX = X + 0.5f - 4.5f, DY = Y + 0.5f - 22.f;
					const float D = FMath::Sqrt(DX * DX + DY * DY);
					if (D < 2.f || D > 4.6f) { continue; }
					const float Ang = FMath::Atan2(DY, DX);
					const bool bRed = FMath::FloorToInt((Ang + UE_PI) / (UE_PI * 0.5f)) % 2 == 0;
					Img.Set(X, Y, bRed ? (D > 3.6f ? Rgb(190, 40, 40) : Rgb(232, 72, 62)) : (D > 3.6f ? Rgb(210, 210, 206) : Rgb(250, 250, 246)));
				}
			}
		}
		Img.Outline(PropOut);
		if (bPier) { Halo(Img, 44.5f, 21.f, 5.f, Rgb(255, 220, 130), 70); }
		return Img;
	}

	// ---------------------------------------------------------------------------------------- order board
	FFVImage BuildOrderBoard(int32 Papers)
	{
		FFVImage Img(32, 36);
		for (int32 PX : { 3, 26 })
		{
			VLine(Img, PX, 8, 35, WoodHi); VLine(Img, PX + 1, 8, 35, Wood); VLine(Img, PX + 2, 8, 34, WoodLo);
		}
		GableRoof(Img, 16.f, 0, 6, 0, 31, Rgb(150, 72, 52), false);
		HLine(Img, 0, 31, 6, Rgb(96, 46, 36));
		Planks(Img, 2, 7, 28, 21, WoodHi, 5, 121u, true);
		HLine(Img, 2, 29, 7, WoodMid); HLine(Img, 2, 29, 27, WoodLo); VLine(Img, 2, 7, 27, WoodMid); VLine(Img, 29, 7, 27, WoodLo);
		Box(Img, 4, 8, 24, 3, DWood, 0.3f, 0.3f);
		Img.Set(5, 9, BrassHi); Img.Set(26, 9, BrassHi);
		Glyph(Img, 12, 8, TEXT('F'), Paper, WoodDk);
		const int32 NX[3] = { 5, 13, 21 }, NY[3] = { 12, 14, 12 };
		for (int32 I = 0; I < 3; ++I)
		{
			const int32 X = NX[I], Y = NY[I];
			if (I < Papers)
			{
				Rect(Img, X, Y, 7, 9, Paper);
				HLine(Img, X, X + 6, Y + 8, PaperLo); VLine(Img, X + 6, Y + 1, Y + 8, PaperLo);
				HLine(Img, X + 1, X + 4, Y + 3, Scribble);
				HLine(Img, X + 1, X + 5, Y + 5, Scribble);
				HLine(Img, X + 1, X + 3 + I % 2, Y + 7, Scribble);
				Img.Set(X + 3, Y, Rgb(220, 58, 58)); Img.Set(X + 3, Y - 1, Rgb(248, 128, 112));
			}
			else
			{
				Img.Set(X + 3, Y, WoodDk);
			}
		}
		Img.Outline(PropOut);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- nets
	FFVImage BuildNetPost(bool bHasNet)
	{
		FFVImage Img(28, 20);
		Stake(Img, 3, 2, 18);
		Stake(Img, 23, 2, 18);
		for (int32 Y = 15; Y <= 19; ++Y) { Img.Set(3, Y, FVArt::Shade(Img.Get(3, Y), 0.25f)); Img.Set(4, Y, FVArt::Shade(Img.Get(4, Y), 0.3f)); Img.Set(23, Y, FVArt::Shade(Img.Get(23, Y), 0.25f)); Img.Set(24, Y, FVArt::Shade(Img.Get(24, Y), 0.3f)); }
		const FColor RopeC(206, 176, 124), RopeD(150, 116, 74);
		Rect(Img, 2, 4, 4, 2, RopeC); Img.Set(2, 5, RopeD);
		Rect(Img, 22, 4, 4, 2, RopeC); Img.Set(25, 5, RopeD);
		if (!bHasNet)
		{
			for (int32 Y = 16; Y <= 18; ++Y)
			{
				for (int32 X = 5; X <= 11; ++X)
				{
					const float DX = (X + 0.5f - 8.5f) / 3.5f, DY = (Y + 0.5f - 17.5f) / 1.6f;
					const float D = DX * DX + DY * DY;
					if (D <= 1.f && D >= 0.2f) { Img.Set(X, Y, ((X + Y) % 2) ? RopeC : RopeD); }
				}
			}
			Img.Set(22, 8, IronLo); Img.Set(21, 9, IronLo); Img.Set(21, 10, IronHi);
		}
		Img.Outline(PropOut);
		// sagging rope between the stakes (after the outline: thin)
		for (int32 X = 6; X <= 21; ++X)
		{
			const float T = (X - 6) / 15.f;
			const int32 Y = 5 + FMath::RoundToInt(2.2f * FMath::Sin(T * UE_PI));
			Img.Set(X, Y, (X % 3 == 0) ? RopeD : RopeC);
		}
		if (bHasNet)
		{
			const FColor Twine(222, 210, 176);
			for (int32 X = 7; X <= 20; ++X)
			{
				const float T = (X - 6) / 15.f;
				const int32 Y0 = 6 + FMath::RoundToInt(2.2f * FMath::Sin(T * UE_PI));
				for (int32 Y = Y0; Y <= Y0 + 4; ++Y)
				{
					if ((X + Y) % 3 == 0 || (X - Y + 30) % 3 == 0) { SetIfEmpty(Img, X, Y, FVArt::WithAlpha(Twine, uint8(230 - (Y - Y0) * 25))); }
				}
			}
			for (int32 FX : { 9, 13, 17 })
			{
				const float T = (FX - 6) / 15.f;
				const int32 FY = 5 + FMath::RoundToInt(2.2f * FMath::Sin(T * UE_PI));
				Img.Set(FX, FY - 1, Rgb(250, 170, 90)); Img.Set(FX + 1, FY - 1, Rgb(236, 116, 52));
				Img.Set(FX, FY, Rgb(236, 116, 52)); Img.Set(FX + 1, FY, Rgb(196, 80, 40));
			}
			Img.Line(5, 11, 7, 19, RopeC);
			Img.Line(22, 11, 20, 19, RopeC);
		}
		return Img;
	}

	void NetFish(FFVImage& I, int32 X, int32 Y, FColor Body, bool bFlip)
	{
		FFVImage F(9, 5);
		Ellipse(F, 3.5f, 2.5f, 2.9f, 1.3f, Body);
		HLine(F, 2, 5, 3, FVArt::Light(Body, 0.35f));
		F.Set(1, 2, Rgb(30, 26, 34));
		F.Set(7, 1, FVArt::Shade(Body, 0.25f)); F.Set(7, 3, FVArt::Shade(Body, 0.25f)); F.Set(6, 2, FVArt::Shade(Body, 0.15f));
		F.Outline(Rgb(46, 42, 56, 220));
		I.Blit(F, X, Y, bFlip);
	}

	FFVImage BuildNetWater(int32 Tier, int32 Fill, bool bTorn)
	{
		const int32 T = FMath::Clamp(Tier, 1, 3), Fl = FMath::Clamp(Fill, 0, 3);
		const int32 W = 20 + 8 * T, H = 12 + 2 * T;
		FFVImage Img(W, H);
		const int32 TopY = 2, BotY = H - 2;
		const float HoleX = W * 0.62f, HoleY = H * 0.56f, HoleRX = W * 0.15f, HoleRY = H * 0.3f;
		auto InHole = [&](int32 X, int32 Y)
		{
			if (!bTorn) { return false; }
			const float DX = (X + 0.5f - HoleX) / HoleRX, DY = (Y + 0.5f - HoleY) / HoleRY;
			return DX * DX + DY * DY <= 1.f;
		};
		// mesh fading with depth (6 px diamonds)
		for (int32 Y = TopY + 1; Y < BotY; ++Y)
		{
			const uint8 A = uint8(190 - (Y - TopY) * 110 / FMath::Max(1, BotY - TopY));
			for (int32 X = 1; X <= W - 2; ++X)
			{
				if (InHole(X, Y)) { continue; }
				const bool bSide = X == 1 || X == W - 2;
				if ((X + Y) % 6 == 0 || (X - Y + 96) % 6 == 0 || bSide)
				{
					Img.Set(X, Y, Rgb(232, 222, 190, bSide ? uint8(FMath::Min(255, A + 40)) : A));
				}
			}
		}
		// lead line
		for (int32 X = 1; X <= W - 2; ++X)
		{
			if (InHole(X, BotY)) { continue; }
			Img.Set(X, BotY, Rgb(120, 110, 100, 170));
			if (X % 4 == 2) { Img.Set(X, BotY, Rgb(80, 80, 96, 220)); }
		}
		// caught fish
		static const FColor FishCols[4] = { FColor(200, 206, 218), FColor(198, 152, 92), FColor(132, 152, 104), FColor(222, 150, 140) };
		static const int32 Counts[3][4] = { { 0, 1, 2, 4 }, { 0, 2, 4, 6 }, { 0, 2, 5, 8 } };
		const int32 N = Counts[T - 1][Fl];
		const int32 RowA = TopY + 1, RowB = FMath::Max(RowA, BotY - 6);
		for (int32 I = 0; I < N; ++I)
		{
			// two staggered rows, evenly spread; a little deterministic jitter
			const int32 FX = (N == 1) ? (W / 2 - 4)
				: 1 + (I * (W - 11)) / (N - 1) + FMath::RoundToInt((FVArt::Noise(I, T, 7u) - 0.5f) * 2.f);
			const int32 FY = ((I % 2) == 0 ? RowA : RowB) + (FVArt::Noise(I, T, 11u) < 0.5f ? 0 : 1);
			if (InHole(FX + 4, FY + 2)) { continue; }
			NetFish(Img, FMath::Clamp(FX, 1, W - 10), FY, FishCols[(I + T) % 4], (I % 2) == 1);
		}
		// float line (broken over the hole when torn) + cork floats
		const FColor RopeC(176, 136, 86), RopeD(120, 88, 56);
		const int32 GapX0 = FMath::FloorToInt(HoleX - HoleRX * 0.6f), GapX1 = FMath::CeilToInt(HoleX + HoleRX * 0.6f);
		for (int32 X = 1; X <= W - 2; ++X)
		{
			int32 Y = TopY;
			if (bTorn)
			{
				if (X >= GapX0 && X <= GapX1) { continue; }
				if (X == GapX0 - 1 || X == GapX1 + 1) { Y = TopY + 1; }
				if (X == GapX0 - 1) { Img.Set(X, TopY + 2, RopeD); }
				if (X == GapX1 + 1) { Img.Set(X, TopY + 2, RopeD); Img.Set(X, TopY + 3, FVArt::WithAlpha(RopeD, 180)); }
			}
			Img.Set(X, Y, (X % 3 == 0) ? RopeD : RopeC);
		}
		for (int32 X = 3; X <= W - 5; X += 6)
		{
			if (bTorn && X + 1 >= GapX0 - 1 && X <= GapX1 + 1) { continue; }
			Img.Set(X, TopY - 1, Rgb(252, 184, 104)); Img.Set(X + 1, TopY - 1, Rgb(240, 130, 60));
			Img.Set(X, TopY, Rgb(236, 116, 52)); Img.Set(X + 1, TopY, Rgb(196, 80, 40));
			Img.Set(X, TopY + 1, Rgb(40, 60, 80, 110)); Img.Set(X + 1, TopY + 1, Rgb(40, 60, 80, 110));
		}
		if (bTorn)
		{
			// the lost float drifting in the hole + loose strands at the hole edge
			const int32 LX = FMath::RoundToInt(HoleX), LY = FMath::RoundToInt(HoleY) - 1;
			Img.Set(LX, LY, Rgb(236, 116, 52)); Img.Set(LX + 1, LY, Rgb(196, 80, 40)); Img.Set(LX, LY - 1, Rgb(252, 184, 104));
			for (int32 I = 0; I < 6; ++I)
			{
				const float A = I * 1.047f + 0.3f;
				const int32 SX = FMath::RoundToInt(HoleX + FMath::Cos(A) * HoleRX * 0.9f);
				const int32 SY = FMath::RoundToInt(HoleY + FMath::Sin(A) * HoleRY * 0.9f);
				Img.Set(SX, SY, Rgb(226, 214, 180, 200));
				Img.Set(SX + (I % 2 ? 1 : -1), SY + 1, Rgb(226, 214, 180, 150));
			}
		}
		return Img;
	}

	// ---------------------------------------------------------------------------------------- dig spot
	FFVImage BuildDigSpot(bool bDugToday)
	{
		FFVImage Img(22, 10);
		const float CX = 11.f, CY = 5.f, RX = 10.f, RY = 4.2f;
		for (int32 Y = 0; Y < 10; ++Y)
		{
			for (int32 X = 0; X < 22; ++X)
			{
				const float DX = (X + 0.5f - CX) / RX, DY = (Y + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY;
				if (D > 1.f) { continue; }
				const float N = FVArt::Noise(X, Y, 131u);
				FColor C = Soil;
				if (N < 0.2f) { C = SoilLo; }
				else if (N > 0.82f) { C = SoilHi; }
				if (D > 0.72f) { C = FVArt::WithAlpha(SoilLo, uint8(D > 0.9f ? 150 : 220)); }
				else if (!bDugToday && ((X + Y * 3) % 7 == 0)) { C = SoilDk; }   // furrows
				Img.Set(X, Y, C);
			}
		}
		if (bDugToday)
		{
			Ellipse(Img, 9.f, 5.5f, 4.2f, 1.9f, SoilDk);
			Ellipse(Img, 9.f, 6.f, 3.f, 1.1f, Rgb(40, 26, 22));
			Ellipse(Img, 16.f, 4.2f, 3.2f, 2.f, SoilHi);
			HLine(Img, 14, 17, 3, Rgb(176, 130, 88)); Img.Set(18, 5, SoilLo); Img.Set(13, 5, SoilLo);
			Img.Set(4, 3, SoilHi); Img.Set(3, 7, SoilHi); Img.Set(19, 7, SoilHi);
		}
		else
		{
			Img.Set(7, 3, SoilHi); Img.Set(8, 3, SoilHi); Img.Set(14, 6, SoilHi); Img.Set(15, 6, SoilHi);
			Img.Set(12, 4, Worm); Img.Set(13, 4, WormLo);              // a worm tail peeking out
			Img.Set(5, 6, Rgb(150, 146, 140)); Img.Set(17, 3, Rgb(170, 166, 158));
		}
		return Img;
	}

	// ---------------------------------------------------------------------------------------- decor: defaults
	/** Spec colors per slot/style (used when a def keeps the default White/Black pair). */
	void DecorColors(const FFVDecorDef& Def, FColor& P, FColor& S)
	{
		P = Def.Primary;
		S = Def.Secondary;
		if (!(P == FColor::White && S == FColor::Black))
		{
			return;
		}
		const int32 St = Def.Style;
		switch (Def.Slot)
		{
		case EFVDecorSlot::Wallpaper:
		{
			static const FColor D[5][2] = { { FColor(170, 120, 70), FColor(130, 85, 50) }, { FColor(236, 214, 170), FColor(210, 110, 120) },
				{ FColor(150, 190, 220), FColor(110, 150, 190) }, { FColor(60, 120, 170), FColor(240, 240, 230) }, { FColor(220, 230, 200), FColor(240, 140, 60) } };
			P = D[FMath::Clamp(St, 0, 4)][0]; S = D[FMath::Clamp(St, 0, 4)][1];
			break;
		}
		case EFVDecorSlot::Floor:
		{
			static const FColor D[4][2] = { { FColor(150, 100, 60), FColor(120, 78, 46) }, { FColor(170, 110, 60), FColor(120, 70, 40) },
				{ FColor(220, 220, 210), FColor(150, 170, 190) }, { FColor(150, 150, 150), FColor(110, 110, 120) } };
			P = D[FMath::Clamp(St, 0, 3)][0]; S = D[FMath::Clamp(St, 0, 3)][1];
			break;
		}
		case EFVDecorSlot::Rug:
		{
			static const FColor D[3][2] = { { FColor(200, 170, 110), FColor(150, 110, 70) }, { FColor(180, 50, 50), FColor(230, 190, 90) }, { FColor(70, 130, 200), FColor(240, 200, 90) } };
			P = D[FMath::Clamp(St, 0, 2)][0]; S = D[FMath::Clamp(St, 0, 2)][1];
			break;
		}
		case EFVDecorSlot::Lamp:
		{
			static const FColor D[4][2] = { { FColor(240, 230, 200), FColor(255, 190, 90) }, { FColor(180, 140, 60), FColor(255, 200, 110) },
				{ FColor(220, 60, 50), FColor(250, 250, 240) }, { FColor(200, 120, 230), FColor(140, 220, 255) } };
			P = D[FMath::Clamp(St, 0, 3)][0]; S = D[FMath::Clamp(St, 0, 3)][1];
			break;
		}
		default:
			break;
		}
	}

	// ---------------------------------------------------------------------------------------- decor: tiles
	FFVImage WallpaperTile(int32 Style, FColor P, FColor S)
	{
		FFVImage Img(16, 16, P);
		switch (Style % 5)
		{
		case 0: // log walls: 4 logs per tile, rounded shading, chinking, knots
			for (int32 Y = 0; Y < 16; ++Y)
			{
				const int32 R = Y % 4;
				for (int32 X = 0; X < 16; ++X)
				{
					FColor C = R == 0 ? FVArt::Light(P, 0.25f) : (R == 1 ? P : (R == 2 ? FVArt::Mix(P, S, 0.5f) : S));
					if (R == 3 && (X % 2 == 0)) { C = FVArt::Shade(S, 0.2f); }
					if (R == 1 && FVArt::Noise(X / 2, Y / 4, 141u) < 0.12f) { C = FVArt::Shade(P, 0.18f); }
					Img.Set(X, Y, C);
				}
			}
			Img.Set(5, 1, FVArt::Shade(P, 0.35f)); Img.Set(6, 2, FVArt::Shade(P, 0.3f));
			Img.Set(12, 9, FVArt::Shade(P, 0.35f)); Img.Set(13, 10, FVArt::Shade(P, 0.3f));
			break;
		case 1: // flowers on cream with faint stripes
		{
			for (int32 Y = 0; Y < 16; ++Y) { Img.Set(7, Y, FVArt::Shade(P, 0.06f)); Img.Set(15, Y, FVArt::Shade(P, 0.06f)); }
			auto Flower = [&Img, S](int32 X, int32 Y)
			{
				const FColor L = FVArt::Light(S, 0.3f);
				Img.Set(X & 15, (Y - 1) & 15, L); Img.Set((X - 1) & 15, Y & 15, S); Img.Set((X + 1) & 15, Y & 15, S); Img.Set(X & 15, (Y + 1) & 15, FVArt::Shade(S, 0.2f));
				Img.Set(X & 15, Y & 15, FColor(255, 214, 90));
				Img.Set((X + 1) & 15, (Y + 2) & 15, FColor(110, 160, 90)); Img.Set((X + 2) & 15, (Y + 2) & 15, FColor(90, 140, 76));
			};
			Flower(3, 3); Flower(11, 11);
			Img.Set(11, 3, FVArt::Shade(P, 0.15f)); Img.Set(3, 11, FVArt::Shade(P, 0.15f));
			break;
		}
		case 2: // blue stripes with damask dots
			for (int32 Y = 0; Y < 16; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					FColor C = P;
					if (X >= 6 && X <= 9) { C = S; }
					if (X == 5 || X == 10) { C = FVArt::Light(P, 0.2f); }
					Img.Set(X, Y, C);
				}
			}
			Img.Set(2, 4, FVArt::Light(P, 0.25f)); Img.Set(1, 5, FVArt::Light(P, 0.2f)); Img.Set(3, 5, FVArt::Light(P, 0.2f)); Img.Set(2, 6, FVArt::Light(P, 0.25f));
			Img.Set(13, 12, FVArt::Light(P, 0.25f)); Img.Set(12, 13, FVArt::Light(P, 0.2f)); Img.Set(14, 13, FVArt::Light(P, 0.2f)); Img.Set(13, 14, FVArt::Light(P, 0.25f));
			Img.Set(7, 8, FVArt::Shade(S, 0.15f)); Img.Set(8, 0, FVArt::Shade(S, 0.15f));
			break;
		case 3: // sea: waves in staggered rows
		{
			for (int32 Y = 0; Y < 16; ++Y) { for (int32 X = 0; X < 16; ++X) { if (FVArt::Noise(X, Y, 143u) < 0.08f) { Img.Set(X, Y, FVArt::Light(P, 0.08f)); } } }
			auto Wave = [&Img, S, P](int32 X, int32 Y)
			{
				Img.Set(X & 15, Y & 15, S); Img.Set((X + 1) & 15, (Y - 1) & 15, S); Img.Set((X + 2) & 15, (Y - 1) & 15, S);
				Img.Set((X + 3) & 15, Y & 15, S); Img.Set((X + 4) & 15, (Y + 1) & 15, FVArt::Mix(S, P, 0.4f));
			};
			Wave(1, 4); Wave(9, 12); Wave(9, 4); Wave(1, 12);
			Img.Set(6, 8, FVArt::Mix(S, P, 0.5f)); Img.Set(14, 0, FVArt::Mix(S, P, 0.5f));
			break;
		}
		default: // little fish and bubbles
		{
			auto Fishy = [&Img, S](int32 X, int32 Y)
			{
				const FColor L = FVArt::Light(S, 0.3f), D = FVArt::Shade(S, 0.3f);
				Img.Set(X & 15, Y & 15, L); Img.Set((X + 1) & 15, Y & 15, S); Img.Set((X + 2) & 15, Y & 15, S);
				Img.Set((X + 1) & 15, (Y + 1) & 15, D); Img.Set((X + 2) & 15, (Y + 1) & 15, D); Img.Set(X & 15, (Y + 1) & 15, S);
				Img.Set((X + 3) & 15, (Y - 0) & 15, D); Img.Set((X + 4) & 15, (Y - 1) & 15, S); Img.Set((X + 4) & 15, (Y + 2) & 15, S);
				Img.Set(X & 15, Y & 15, FColor(40, 30, 40));
			};
			Fishy(2, 3); Fishy(10, 11);
			const FColor B = FVArt::Shade(P, 0.18f);
			Img.Set(12, 3, B); Img.Set(13, 1, B); Img.Set(4, 11, B); Img.Set(5, 9, B);
			break;
		}
		}
		return Img;
	}

	FFVImage FloorTile(int32 Style, FColor P, FColor S)
	{
		FFVImage Img(16, 16, P);
		switch (Style % 4)
		{
		case 0: // planks, 4 px tall, staggered joints
			for (int32 Y = 0; Y < 16; ++Y)
			{
				const int32 Band = Y / 4, R = Y % 4;
				const int32 Joint = (Band * 7 + 3) % 16;
				for (int32 X = 0; X < 16; ++X)
				{
					FColor C = R == 3 ? S : (R == 0 ? FVArt::Light(P, 0.12f) : P);
					if (X == Joint && R != 3) { C = S; }
					if (R == 1 && FVArt::Noise(X / 3, Band, 151u) < 0.2f) { C = FVArt::Mix(P, S, 0.35f); }
					Img.Set(X, Y, C);
				}
			}
			break;
		case 1: // parquet: basket weave 8x8 blocks of 4 strips
			for (int32 Y = 0; Y < 16; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					const bool bHor = ((X / 8) + (Y / 8)) % 2 == 0;
					const int32 Strip = bHor ? (Y % 8) / 2 : (X % 8) / 2;
					const int32 In = bHor ? (Y % 2) : (X % 2);
					FColor C = (Strip % 2 == 0) ? P : FVArt::Mix(P, S, 0.45f);
					if (In == 1) { C = FVArt::Shade(C, 0.12f); }
					if ((bHor && X % 8 == 7) || (!bHor && Y % 8 == 7)) { C = S; }
					Img.Set(X, Y, C);
				}
			}
			break;
		case 2: // checkerboard tiles with grout
			for (int32 Y = 0; Y < 16; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					const bool bA = ((X / 8) + (Y / 8)) % 2 == 0;
					FColor C = bA ? P : S;
					if (X % 8 == 0 || Y % 8 == 0) { C = FVArt::Shade(FVArt::Mix(P, S, 0.5f), 0.25f); }
					else if (X % 8 == 1 || Y % 8 == 1) { C = FVArt::Light(C, 0.2f); }
					Img.Set(X, Y, C);
				}
			}
			break;
		default: // flagstones (hand-laid, wrapping)
		{
			// stone id map: two courses of 8 px, joints wrap around the tile edges
			for (int32 Y = 0; Y < 16; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					const int32 Course = Y / 8;
					const int32 Off = Course == 0 ? 0 : 5;
					const int32 XX = (X + Off) % 16;
					const int32 StoneId = Course * 4 + (XX < 7 ? 0 : 1);
					const bool bJoint = (Y % 8 == 7) || XX == 6 || XX == 15;
					FColor C = P;
					const float N = FVArt::Noise(StoneId, 3, 153u);
					if (N < 0.35f) { C = FVArt::Shade(P, 0.1f); }
					else if (N > 0.7f) { C = FVArt::Light(P, 0.1f); }
					if (FVArt::Noise(X, Y, 155u) < 0.1f) { C = FVArt::Shade(C, 0.15f); }
					if (Y % 8 == 0 || XX == 0 || XX == 7) { C = FVArt::Light(C, 0.15f); }
					if (bJoint) { C = S; }
					Img.Set(X, Y, C);
				}
			}
			break;
		}
		}
		return Img;
	}

	// ---------------------------------------------------------------------------------------- decor: world props
	FFVImage RugWorld(int32 Style, FColor P, FColor S)
	{
		if (Style % 3 == 0)
		{
			// woven oval rag rug: concentric rings
			FFVImage Img(40, 14);
			for (int32 Y = 0; Y < 14; ++Y)
			{
				for (int32 X = 0; X < 40; ++X)
				{
					const float DX = (X + 0.5f - 20.f) / 19.f, DY = (Y + 0.5f - 7.f) / 6.2f;
					const float D = FMath::Sqrt(DX * DX + DY * DY);
					if (D > 1.f) { continue; }
					const int32 Ring = FMath::FloorToInt(D * 6.f);
					FColor C = (Ring % 2 == 0) ? P : S;
					if (Ring == 3) { C = FVArt::Mix(P, FColor(230, 220, 200), 0.4f); }
					if (((X + Y) % 3) == 0) { C = FVArt::Shade(C, 0.1f); }
					if (Y >= 11) { C = FVArt::Shade(C, 0.15f); }
					Img.Set(X, Y, C);
				}
			}
			Img.Outline(FVArt::Shade(S, 0.55f));
			return Img;
		}
		if (Style % 3 == 1)
		{
			// red carpet with a golden border, medallion and fringe
			FFVImage Img(40, 14);
			Rect(Img, 2, 1, 36, 12, P);
			for (int32 X = 2; X <= 37; ++X) { Img.Set(X, 2, S); Img.Set(X, 11, S); }
			for (int32 Y = 2; Y <= 11; ++Y) { Img.Set(4, Y, S); Img.Set(35, Y, S); }
			for (int32 X = 5; X <= 34; X += 3) { Img.Set(X, 3, FVArt::Shade(S, 0.2f)); Img.Set(X + 1, 10, FVArt::Shade(S, 0.2f)); }
			for (int32 Y = 4; Y <= 9; ++Y)
			{
				const int32 Hw = 3 - FMath::Abs(Y * 2 - 13) / 2;
				for (int32 X = 20 - Hw * 2; X <= 19 + Hw * 2; ++X) { Img.Set(X, Y, (X + Y) % 2 ? S : FVArt::Light(S, 0.2f)); }
			}
			Img.Set(19, 6, P); Img.Set(20, 7, P);
			for (int32 C : { 8, 31 }) { Img.Set(C, 6, S); Img.Set(C - 1, 7, S); Img.Set(C + 1, 7, S); Img.Set(C, 8, S); }
			HLine(Img, 2, 37, 12, FVArt::Shade(P, 0.3f));
			Img.Outline(FVArt::Shade(P, 0.6f));
			for (int32 Y = 2; Y <= 11; Y += 2) { Img.Set(0, Y, FColor(240, 230, 200)); Img.Set(39, Y, FColor(240, 230, 200)); }
			return Img;
		}
		// fish-shaped rug
		FFVImage Img(44, 14);
		for (int32 Y = 0; Y < 14; ++Y)
		{
			for (int32 X = 0; X < 44; ++X)
			{
				const float DX = (X + 0.5f - 18.f) / 16.5f, DY = (Y + 0.5f - 7.f) / 6.f;
				if (DX * DX + DY * DY > 1.f) { continue; }
				FColor C = P;
				if (Y >= 9) { C = FVArt::Light(P, 0.35f); }
				if (((X / 3) + (Y / 2)) % 3 == 0 && X > 8 && Y < 9) { C = FVArt::Light(P, 0.15f); }
				Img.Set(X, Y, C);
			}
		}
		for (int32 X = 33; X <= 42; ++X)
		{
			const int32 Hw = 1 + (X - 33) * 5 / 9;
			for (int32 Y = 7 - Hw; Y <= 6 + Hw; ++Y) { Img.Set(X, Y, (X % 3 == 0) ? FVArt::Shade(S, 0.15f) : S); }
		}
		for (int32 X = 14; X <= 22; ++X) { Img.Set(X, 0, S); if (X > 15 && X < 21) { Img.Set(X, 1, S); } }
		Ellipse(Img, 7.f, 5.f, 2.f, 2.f, FColor(250, 250, 244)); Img.Set(6, 5, FColor(30, 26, 40)); Img.Set(7, 5, FColor(30, 26, 40));
		VLine(Img, 11, 3, 10, FVArt::Shade(P, 0.3f));
		Img.Outline(FVArt::Shade(P, 0.6f));
		return Img;
	}

	FFVImage LampWorld(int32 Style, FColor P, FColor S)
	{
		switch (Style % 4)
		{
		case 0:
		{
			// candle in a brass holder on a stool
			FFVImage Img(12, 18);
			Planks(Img, 1, 11, 10, 2, WoodHi, 2, 161u);
			VLine(Img, 2, 13, 17, Wood); VLine(Img, 9, 13, 17, WoodLo); HLine(Img, 3, 8, 15, WoodLo);
			HLine(Img, 3, 8, 10, Brass); HLine(Img, 4, 7, 9, BrassHi);
			Rect(Img, 5, 4, 2, 5, P); VLine(Img, 5, 4, 8, FVArt::Light(P, 0.4f)); VLine(Img, 6, 5, 8, FVArt::Shade(P, 0.15f));
			Img.Set(6, 4, FVArt::Light(P, 0.6f));
			Img.Outline(PropOut);
			Img.Set(5, 1, FireY); Img.Set(5, 2, S); Img.Set(6, 2, FVArt::Shade(S, 0.1f)); Img.Set(5, 3, FireR);
			Halo(Img, 5.5f, 2.f, 5.f, S, 90);
			return Img;
		}
		case 1:
		{
			// kerosene lamp on a small side table
			FFVImage Img(14, 26);
			Planks(Img, 1, 15, 12, 2, WoodHi, 2, 163u);
			VLine(Img, 2, 17, 25, Wood); VLine(Img, 3, 17, 25, WoodLo); VLine(Img, 10, 17, 25, Wood); VLine(Img, 11, 17, 25, WoodLo);
			HLine(Img, 4, 9, 21, WoodMid);
			Ellipse(Img, 7.f, 12.5f, 3.2f, 2.f, P); HLine(Img, 4, 10, 14, FVArt::Shade(P, 0.3f)); Img.Set(5, 12, FVArt::Light(P, 0.5f));
			HLine(Img, 5, 8, 10, FVArt::Shade(P, 0.2f));
			for (int32 Y = 3; Y <= 9; ++Y)
			{
				const int32 Hw = (Y >= 5 && Y <= 8) ? 2 : 1;
				for (int32 X = 7 - Hw; X <= 6 + Hw; ++X) { Img.Set(X, Y, FColor(236, 244, 240, 150)); }
			}
			HLine(Img, 5, 8, 2, P); HLine(Img, 6, 7, 1, FVArt::Shade(P, 0.2f));
			Img.Line(10, 11, 11, 8, FVArt::Shade(P, 0.25f)); Img.Set(11, 7, P);
			Img.Outline(PropOut);
			Img.Set(6, 7, S); Img.Set(7, 7, FireY); Img.Set(6, 6, FireY); Img.Set(7, 8, FVArt::Shade(S, 0.1f));
			Halo(Img, 6.5f, 6.5f, 7.f, S, 80);
			return Img;
		}
		case 2:
		{
			// big fishing-float lamp on a pole stand
			FFVImage Img(12, 30);
			VLine(Img, 5, 13, 27, IronHi); VLine(Img, 6, 13, 27, IronLo);
			Ellipse(Img, 6.f, 28.f, 4.5f, 1.5f, IronLo); HLine(Img, 3, 8, 27, IronHi);
			for (int32 Y = 3; Y <= 13; ++Y)
			{
				for (int32 X = 1; X <= 10; ++X)
				{
					const float DX = (X + 0.5f - 6.f) / 4.6f, DY = (Y + 0.5f - 8.5f) / 5.6f;
					if (DX * DX + DY * DY > 1.f) { continue; }
					FColor C = Y <= 8 ? P : S;
					if (Y == 8) { C = FColor(40, 34, 44); }
					if (DX < -0.45f) { C = FVArt::Light(C, 0.3f); } else if (DX > 0.45f) { C = FVArt::Shade(C, 0.25f); }
					Img.Set(X, Y, C);
				}
			}
			VLine(Img, 6, 0, 2, P); Img.Set(6, 0, FVArt::Light(P, 0.4f));
			Img.Outline(PropOut);
			Halo(Img, 6.f, 9.f, 8.f, FVArt::Light(S, 0.2f), 70);
			return Img;
		}
		default:
		{
			// jellyfish lamp: glowing bell + tentacles on a short stand
			FFVImage Img(16, 28);
			Ellipse(Img, 8.f, 26.f, 5.5f, 1.5f, IronLo); HLine(Img, 4, 11, 25, IronHi);
			VLine(Img, 7, 18, 25, IronHi); VLine(Img, 8, 18, 25, IronLo);
			for (int32 Y = 2; Y <= 10; ++Y)
			{
				for (int32 X = 1; X <= 14; ++X)
				{
					const float DX = (X + 0.5f - 8.f) / 6.6f, DY = (Y + 0.5f - 10.f) / 8.f;
					if (DX * DX + DY * DY > 1.f) { continue; }
					FColor C = P;
					if (DX * DX + DY * DY < 0.35f) { C = FVArt::Mix(P, S, 0.6f); }
					if (Y == 10) { C = FVArt::Shade(P, 0.25f); }
					if (DX < -0.5f && Y < 8) { C = FVArt::Light(C, 0.3f); }
					Img.Set(X, Y, C);
				}
			}
			Img.Set(5, 5, S); Img.Set(10, 6, S); Img.Set(8, 4, FVArt::Light(S, 0.4f));
			Img.Outline(FVArt::Shade(P, 0.6f));
			// tentacles: thin, translucent, wavy (after the outline so they stay delicate)
			for (int32 T = 0; T < 5; ++T)
			{
				const int32 TX = 3 + T * 2 + (T > 1 ? 1 : 0);
				for (int32 Y = 11; Y <= 16 + (T % 2) * 2; ++Y)
				{
					const uint8 A = uint8(FMath::Clamp(230 - (Y - 11) * 22, 90, 230));
					Img.Blend(TX + ((Y / 2 + T) % 2), Y, FVArt::WithAlpha(FVArt::Light(P, 0.35f), A));
				}
			}
			Halo(Img, 8.f, 7.f, 9.f, S, 80);
			return Img;
		}
		}
	}

	void PotBase(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor C)
	{
		Box(I, X, Y, W, 2, FVArt::Light(C, 0.15f), 0.3f, 0.3f);
		for (int32 R = 2; R < H; ++R)
		{
			const int32 In = (R * 2) / H;
			HLine(I, X + In, X + W - 1 - In, Y + R, C);
			I.Set(X + In, Y + R, FVArt::Light(C, 0.25f));
			I.Set(X + W - 1 - In, Y + R, FVArt::Shade(C, 0.3f));
		}
	}

	void LeafBlob(FFVImage& I, float CX, float CY, float R, uint32 Seed)
	{
		for (int32 Y = FMath::FloorToInt(CY - R); Y <= FMath::CeilToInt(CY + R); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - R); X <= FMath::CeilToInt(CX + R); ++X)
			{
				const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
				const float D = FMath::Sqrt(DX * DX + DY * DY) + FVArt::Noise(X, Y, Seed) * 1.2f;
				if (D > R) { continue; }
				const float S = (DX + DY) / FMath::Max(R, 1.f);
				FColor C = S < -0.5f ? LeafHi : (S > 0.35f ? LeafLo : Leaf);
				if (FVArt::Noise(X, Y, Seed + 3u) < 0.12f) { C = LeafDk; }
				I.Set(X, Y, C);
			}
		}
	}

	FFVImage PlantWorld(int32 Style)
	{
		switch (Style % 4)
		{
		case 0:
		{
			// ficus: terracotta pot, thin trunk, big leafy crown
			FFVImage Img(16, 28);
			PotBase(Img, 3, 20, 10, 7, Terra);
			HLine(Img, 5, 10, 21, SoilLo);
			VLine(Img, 7, 11, 20, DWood); VLine(Img, 8, 13, 20, DWoodLo); Img.Line(8, 15, 11, 12, DWood);
			LeafBlob(Img, 8.f, 7.f, 6.5f, 171u);
			LeafBlob(Img, 4.5f, 11.f, 3.4f, 173u);
			LeafBlob(Img, 11.5f, 11.5f, 3.2f, 175u);
			Img.Outline(PropOut);
			return Img;
		}
		case 1:
		{
			// cactus with arms and a pink flower in a blue pot
			FFVImage Img(12, 20);
			PotBase(Img, 2, 14, 8, 5, FColor(90, 130, 190));
			const FColor Cac(96, 170, 84), CacL(150, 214, 110), CacD(56, 120, 64);
			for (int32 Y = 3; Y <= 14; ++Y) { Img.Set(4, Y, CacL); Img.Set(5, Y, Cac); Img.Set(6, Y, Cac); Img.Set(7, Y, CacD); }
			for (int32 Y = 6; Y <= 9; ++Y) { Img.Set(1, Y, CacL); Img.Set(2, Y, Cac); }
			HLine(Img, 2, 3, 9, Cac); HLine(Img, 2, 3, 10, CacD);
			for (int32 Y = 4; Y <= 8; ++Y) { Img.Set(9, Y, Cac); Img.Set(10, Y, CacD); }
			HLine(Img, 8, 9, 8, Cac); HLine(Img, 8, 9, 9, CacD);
			Img.Outline(PropOut);
			Img.Set(5, 2, FColor(250, 120, 170)); Img.Set(6, 2, FColor(250, 120, 170)); Img.Set(5, 1, FColor(255, 180, 210)); Img.Set(6, 1, FColor(230, 80, 140));
			Img.Set(5, 6, FColor(250, 250, 240)); Img.Set(6, 9, FColor(250, 250, 240)); Img.Set(5, 12, FColor(250, 250, 240));
			return Img;
		}
		case 2:
		{
			// fern: arching fronds out of a woven basket
			FFVImage Img(20, 20);
			for (int32 Y = 14; Y <= 18; ++Y)
			{
				for (int32 X = 6 + (Y - 14) / 3; X <= 13 - (Y - 14) / 3; ++X) { Img.Set(X, Y, ((X + Y) % 2) ? FColor(206, 160, 96) : FColor(160, 112, 62)); }
			}
			HLine(Img, 5, 14, 14, FColor(226, 186, 120));
			auto Frond = [&Img](float X0, float Y0, float DX, float DY, float Curl, int32 Len)
			{
				float X = X0, Y = Y0, VX = DX, VY = DY;
				for (int32 I = 0; I < Len; ++I)
				{
					const int32 PX = FMath::RoundToInt(X), PY = FMath::RoundToInt(Y);
					Img.Set(PX, PY, LeafLo);
					if (I > 1 && I < Len - 1)
					{
						Img.Set(PX, PY - 1, I % 2 ? LeafHi : Leaf);
						Img.Set(PX + (VX > 0 ? -1 : 1), PY + 1, Leaf);
					}
					X += VX; Y += VY; VY += Curl;
				}
			};
			Frond(9.5f, 13.f, -0.9f, -0.9f, 0.13f, 9);
			Frond(10.5f, 13.f, 0.9f, -0.9f, 0.13f, 9);
			Frond(9.5f, 13.f, -0.5f, -1.2f, 0.12f, 9);
			Frond(10.5f, 13.f, 0.5f, -1.2f, 0.12f, 9);
			Frond(10.f, 13.f, 0.05f, -1.3f, 0.07f, 10);
			Img.Outline(PropOut);
			return Img;
		}
		default:
		{
			// bonsai: shallow glazed pot, twisted trunk, cloud-shaped foliage pads
			FFVImage Img(22, 18);
			Box(Img, 3, 13, 16, 2, FColor(70, 110, 170), 0.3f, 0.3f);
			HLine(Img, 4, 17, 15, FColor(50, 84, 140)); Img.Set(5, 16, FColor(50, 84, 140)); Img.Set(16, 16, FColor(50, 84, 140));
			HLine(Img, 6, 15, 12, FColor(96, 150, 70));
			Img.Line(11, 12, 9, 9, DWood); Img.Line(12, 12, 10, 9, DWoodLo);
			Img.Line(9, 9, 11, 6, DWood); Img.Line(10, 9, 12, 6, DWoodLo);
			Img.Line(10, 8, 6, 6, DWood); Img.Line(12, 7, 15, 5, DWood);
			auto Pad = [&Img](float CX, float CY, float RX)
			{
				for (int32 Y = FMath::FloorToInt(CY - 2); Y <= FMath::CeilToInt(CY + 1); ++Y)
				{
					for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
					{
						const float DX = (X + 0.5f - CX) / RX, DY = (Y + 0.5f - CY) / 2.f;
						if (DX * DX + DY * DY > 1.f) { continue; }
						Img.Set(X, Y, DY < -0.3f ? LeafHi : (DY > 0.3f ? LeafLo : Leaf));
					}
				}
			};
			Pad(6.f, 5.5f, 4.f); Pad(15.f, 4.5f, 4.5f); Pad(11.f, 2.5f, 3.5f);
			Img.Outline(PropOut);
			return Img;
		}
		}
	}

	/** Gilded / wooden frame around a W x H canvas (frame 2 px). */
	void Frame(FFVImage& I, int32 W, int32 H, FColor C)
	{
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				if (X >= 2 && Y >= 2 && X < W - 2 && Y < H - 2) { continue; }
				FColor F = C;
				if (X == 0 || Y == 0) { F = FVArt::Light(C, 0.3f); }
				else if (X == W - 1 || Y == H - 1) { F = FVArt::Shade(C, 0.35f); }
				else if (X == 1 || Y == 1) { F = C; }
				else { F = FVArt::Shade(C, 0.15f); }
				I.Set(X, Y, F);
			}
		}
	}

	FFVImage PaintingWorld(int32 Style)
	{
		switch (Style % 4)
		{
		case 0:
		{
			// «Пруд»: sky, far trees, a pond with reeds and a lily pad
			FFVImage Img(24, 18);
			for (int32 Y = 2; Y < 16; ++Y)
			{
				for (int32 X = 2; X < 22; ++X)
				{
					FColor C = FVArt::Mix(FColor(170, 214, 240), FColor(230, 240, 236), (Y - 2) / 6.f);
					if (Y >= 7) { C = (FVArt::Noise(X / 2, 1, 181u) < 0.5f) ? FColor(70, 130, 70) : FColor(90, 150, 80); }
					if (Y >= 9) { C = FVArt::Mix(FColor(96, 170, 210), FColor(60, 120, 180), (Y - 9) / 6.f); }
					Img.Set(X, Y, C);
				}
			}
			Img.Set(8, 11, FColor(200, 230, 245)); Img.Set(9, 11, FColor(200, 230, 245)); Img.Set(14, 13, FColor(200, 230, 245));
			HLine(Img, 15, 17, 12, FColor(84, 168, 64)); Img.Set(16, 11, FColor(236, 140, 170));
			VLine(Img, 4, 9, 14, FColor(120, 150, 70)); VLine(Img, 5, 10, 14, FColor(96, 130, 60)); Img.Set(4, 8, FColor(140, 90, 50));
			HLine(Img, 16, 18, 4, FColor(250, 250, 250)); HLine(Img, 15, 19, 5, FColor(240, 244, 248));
			Frame(Img, 24, 18, Brass);
			Img.Outline(PropOut);
			return Img;
		}
		case 1:
		{
			// «Карта долины»: parchment map with a river, forest, mountains and a dotted path
			FFVImage Img(26, 18);
			for (int32 Y = 2; Y < 16; ++Y)
			{
				for (int32 X = 2; X < 24; ++X) { Img.Set(X, Y, FVArt::Noise(X, Y, 183u) < 0.15f ? PaperLo : Paper); }
			}
			for (int32 Y = 2; Y < 16; ++Y)
			{
				const int32 RX = 13 + FMath::RoundToInt(2.5f * FMath::Sin(Y * 0.7f));
				Img.Set(RX, Y, FColor(80, 140, 210)); Img.Set(RX + 1, Y, FColor(120, 180, 230));
			}
			for (int32 I = 0; I < 3; ++I)
			{
				const int32 MX = 4 + I * 3;
				Img.Set(MX + 1, 3, FColor(150, 140, 130)); HLine(Img, MX, MX + 2, 4, FColor(120, 110, 104));
			}
			for (int32 I = 0; I < 6; ++I) { Img.Set(17 + (I % 3) * 2, 4 + (I / 3) * 2, FColor(70, 140, 70)); }
			for (int32 X = 4; X <= 11; X += 2) { Img.Set(X, 11 + (X % 4 == 0 ? 0 : 1), FColor(170, 60, 50)); }
			Img.Set(19, 12, FColor(200, 50, 50)); Img.Set(20, 13, FColor(200, 50, 50)); Img.Set(20, 12, FColor(200, 50, 50)); Img.Set(19, 13, FColor(200, 50, 50));
			Frame(Img, 26, 18, DWood);
			Img.Outline(PropOut);
			return Img;
		}
		case 2:
		{
			// «Портрет Митрича»: dark green backdrop, the old fisherman in his yellow hat
			FFVImage Img(18, 22);
			for (int32 Y = 2; Y < 20; ++Y) { HLine(Img, 2, 15, Y, FVArt::Mix(FColor(60, 100, 76), FColor(40, 66, 54), (Y - 2) / 18.f)); }
			FFVImage Bust = TraderBust(false);
			Img.Blit(Bust.Cropped(0, 0, 16, 16), 1, 3);
			Frame(Img, 18, 22, Brass);
			Img.Outline(PropOut);
			return Img;
		}
		default:
		{
			// «Золотая рыбка»: a golden fish with flowing fins on deep blue
			FFVImage Img(20, 18);
			for (int32 Y = 2; Y < 16; ++Y) { HLine(Img, 2, 17, Y, FVArt::Mix(FColor(40, 60, 120), FColor(24, 34, 80), (Y - 2) / 14.f)); }
			Ellipse(Img, 8.5f, 9.f, 4.f, 2.6f, FColor(255, 196, 50));
			HLine(Img, 6, 10, 10, FColor(255, 232, 130)); Img.Set(6, 8, FColor(40, 30, 40));
			for (int32 Y = 5; Y <= 13; ++Y)
			{
				const int32 Hw = FMath::Abs(Y - 9);
				Img.Set(13 + Hw / 2, Y, FColor(255, 150, 40)); Img.Set(14 + Hw / 2, Y, FColor(255, 196, 80));
			}
			Img.Set(8, 5, FColor(255, 170, 50)); Img.Set(9, 6, FColor(255, 170, 50)); Img.Set(8, 13, FColor(255, 170, 50));
			Img.Set(4, 4, FColor(200, 220, 255)); Img.Set(5, 6, FColor(160, 190, 240)); Img.Set(3, 7, FColor(160, 190, 240));
			Frame(Img, 20, 18, Brass);
			Img.Outline(PropOut);
			Img.Set(1, 1, BrassHi); Img.Set(18, 1, BrassHi);
			return Img;
		}
		}
	}

	FFVImage CurtainsWorld(int32 Style)
	{
		FFVImage Img(36, 30);
		FColor Base, Trim;
		switch (Style % 3)
		{
		case 0: Base = FColor(222, 206, 170); Trim = FColor(186, 164, 120); break;
		case 1: Base = FColor(206, 64, 60); Trim = FColor(246, 240, 228); break;
		default: Base = FColor(128, 30, 52); Trim = FColor(236, 186, 70); break;
		}
		auto Cloth = [&](int32 X, int32 Y) -> FColor
		{
			if (Style % 3 == 1)
			{
				const bool bA = (X / 2) % 2 == 0, bB = (Y / 2) % 2 == 0;
				return bA && bB ? FVArt::Shade(Base, 0.1f) : ((bA || bB) ? FVArt::Mix(Base, Trim, 0.5f) : Trim);
			}
			return Base;
		};
		auto Drape = [&](int32 X0, int32 X1, bool bLeft)
		{
			for (int32 Y = 3; Y <= 28; ++Y)
			{
				// pinched at the tie-back (y 18), flaring below
				int32 L = X0, R = X1;
				const int32 Pinch = (Y >= 14 && Y <= 21) ? (4 - FMath::Abs(Y - 18)) : 0;
				if (bLeft) { R = X1 - FMath::Max(0, Pinch); } else { L = X0 + FMath::Max(0, Pinch); }
				for (int32 X = L; X <= R; ++X)
				{
					FColor C = Cloth(X, Y);
					const int32 Fold = (X - X0) % 3;
					if (Fold == 0) { C = FVArt::Light(C, 0.15f); }
					else if (Fold == 2) { C = FVArt::Shade(C, 0.2f); }
					Img.Set(X, Y, C);
				}
			}
			const int32 TX0 = bLeft ? X0 : X0 + 3, TX1 = bLeft ? X1 - 3 : X1;
			HLine(Img, TX0, TX1, 18, Trim); HLine(Img, TX0, TX1, 19, FVArt::Shade(Trim, 0.25f));
		};
		Drape(1, 9, true);
		Drape(26, 34, false);
		// valance
		for (int32 X = 1; X <= 34; ++X)
		{
			const int32 Depth = 5 + ((X % 6) >= 2 && (X % 6) <= 4 ? 1 : 0);
			for (int32 Y = 2; Y <= Depth; ++Y) { Img.Set(X, Y, Y == Depth ? FVArt::Shade(Cloth(X, Y), 0.2f) : Cloth(X, Y)); }
			if (Style % 3 == 2) { Img.Set(X, Depth + 1, (X % 2) ? Trim : FVArt::Shade(Trim, 0.2f)); }
		}
		// rod with finials
		HLine(Img, 0, 35, 1, DWood); HLine(Img, 0, 35, 0, DWoodHi);
		Img.Set(0, 2, DWoodLo); Img.Set(35, 2, DWoodLo);
		Img.Outline(PropOut);
		if (Style % 3 == 2)
		{
			Img.Set(7, 20, Trim); Img.Set(7, 21, Trim); Img.Set(28, 20, Trim); Img.Set(28, 21, Trim);
		}
		return Img;
	}

	// ---------------------------------------------------------------------------------------- decor: catalog icons
	FFVImage DecorIconImpl(const FFVDecorDef& Def)
	{
		FColor P, S;
		DecorColors(Def, P, S);
		const int32 St = Def.Style;
		FFVImage Img(16, 16);
		const FColor Ink = FVArt::InkOutline();
		switch (Def.Slot)
		{
		case EFVDecorSlot::Wallpaper:
		{
			// a sample sheet of the wallpaper with a curled (dog-eared) bottom-right corner
			const FFVImage Tile = WallpaperTile(St, P, S);
			for (int32 Y = 1; Y <= 14; ++Y)
			{
				for (int32 X = 2; X <= 13; ++X)
				{
					const int32 Corner = (X - 9) + (Y - 10);   // > 0: cut off by the curl
					if (Corner > 4) { continue; }
					if (Corner >= 2)
					{
						Img.Set(X, Y, Corner == 2 ? FVArt::Shade(P, 0.35f) : FVArt::Light(FVArt::Mix(P, FColor(250, 244, 230), 0.6f), 0.2f));
						continue;
					}
					Img.Set(X, Y, Tile.Get(X + 1, Y + 1));
				}
			}
			HLine(Img, 2, 13, 1, FVArt::Light(Tile.Get(4, 2), 0.25f));
			Img.Outline(Ink);
			return Img;
		}
		case EFVDecorSlot::Floor:
		{
			// an isometric floor tile (diamond) sampled from the rotated pattern
			const FFVImage Tile = FloorTile(St, P, S);
			for (int32 Y = 2; Y <= 14; ++Y)
			{
				for (int32 X = 0; X <= 15; ++X)
				{
					const float DX = X + 0.5f - 8.f, DY = Y + 0.5f - 8.5f;
					if (FMath::Abs(DX) / 7.6f + FMath::Abs(DY) / 5.6f > 1.f) { continue; }
					const int32 U = FMath::FloorToInt(DX * 0.75f + DY * 1.05f + 64.f) & 15;
					const int32 V = FMath::FloorToInt(DY * 1.05f - DX * 0.75f + 64.f) & 15;
					FColor C = Tile.Get(U, V);
					if (DY > 3.4f - FMath::Abs(DX) * 0.2f) { C = FVArt::Shade(C, 0.2f); }
					Img.Set(X, Y, C);
				}
			}
			Img.Outline(Ink);
			return Img;
		}
		case EFVDecorSlot::Rug:
		{
			if (St % 3 == 0)
			{
				for (int32 Y = 4; Y <= 13; ++Y)
				{
					for (int32 X = 1; X <= 14; ++X)
					{
						const float DX = (X + 0.5f - 8.f) / 6.8f, DY = (Y + 0.5f - 9.f) / 4.6f;
						const float D = FMath::Sqrt(DX * DX + DY * DY);
						if (D > 1.f) { continue; }
						Img.Set(X, Y, (FMath::FloorToInt(D * 4.f) % 2 == 0) ? P : S);
					}
				}
			}
			else if (St % 3 == 1)
			{
				Rect(Img, 2, 5, 12, 8, P);
				HLine(Img, 2, 13, 6, S); HLine(Img, 2, 13, 11, S); VLine(Img, 3, 6, 11, S); VLine(Img, 12, 6, 11, S);
				Img.Set(7, 8, S); Img.Set(8, 8, S); Img.Set(7, 9, S); Img.Set(8, 9, S); Img.Set(6, 8, FVArt::Light(S, 0.2f)); Img.Set(9, 9, FVArt::Light(S, 0.2f));
				Img.Outline(Ink);
				for (int32 Y = 6; Y <= 11; Y += 2) { Img.Set(0, Y, FColor(240, 230, 200)); Img.Set(15, Y, FColor(240, 230, 200)); }
				return Img;
			}
			else
			{
				Ellipse(Img, 6.5f, 9.f, 5.5f, 3.4f, P);
				HLine(Img, 3, 9, 11, FVArt::Light(P, 0.35f));
				for (int32 X = 11; X <= 14; ++X) { const int32 Hw = X - 10; for (int32 Y = 9 - Hw; Y <= 8 + Hw; ++Y) { Img.Set(X, Y, S); } }
				Img.Set(3, 8, FColor(250, 250, 244)); Img.Set(3, 7, FColor(30, 26, 40));
			}
			Img.Outline(Ink);
			return Img;
		}
		case EFVDecorSlot::Lamp:
		{
			switch (St % 4)
			{
			case 0:
				HLine(Img, 4, 11, 13, Brass); HLine(Img, 5, 10, 12, BrassHi); HLine(Img, 5, 10, 14, BrassLo);
				Rect(Img, 7, 6, 2, 6, P); VLine(Img, 7, 6, 11, FVArt::Light(P, 0.4f));
				Img.Outline(Ink);
				Img.Set(7, 3, FireY); Img.Set(7, 4, S); Img.Set(8, 4, FVArt::Shade(S, 0.1f)); Img.Set(7, 5, FireR);
				Halo(Img, 7.5f, 4.f, 4.f, S, 110);
				return Img;
			case 1:
				Ellipse(Img, 7.5f, 12.5f, 3.6f, 2.f, P); Img.Set(5, 12, FVArt::Light(P, 0.5f)); HLine(Img, 4, 11, 14, FVArt::Shade(P, 0.3f));
				for (int32 Y = 4; Y <= 10; ++Y) { const int32 Hw = (Y >= 6 && Y <= 9) ? 2 : 1; for (int32 X = 8 - Hw; X <= 7 + Hw; ++X) { Img.Set(X, Y, FColor(236, 244, 240, 170)); } }
				HLine(Img, 6, 9, 3, P); HLine(Img, 6, 9, 11, FVArt::Shade(P, 0.2f));
				Img.Outline(Ink);
				Img.Set(7, 7, S); Img.Set(8, 7, FireY); Img.Set(7, 8, FireY);
				Halo(Img, 7.5f, 7.5f, 5.f, S, 90);
				return Img;
			case 2:
				for (int32 Y = 2; Y <= 13; ++Y)
				{
					for (int32 X = 2; X <= 13; ++X)
					{
						const float DX = (X + 0.5f - 8.f) / 4.8f, DY = (Y + 0.5f - 8.f) / 5.8f;
						if (DX * DX + DY * DY > 1.f) { continue; }
						FColor C = Y < 8 ? P : S;
						if (Y == 8) { C = FColor(40, 34, 44); }
						if (DX < -0.4f) { C = FVArt::Light(C, 0.3f); } else if (DX > 0.45f) { C = FVArt::Shade(C, 0.25f); }
						Img.Set(X, Y, C);
					}
				}
				VLine(Img, 8, 0, 2, P);
				Img.Outline(Ink);
				return Img;
			default:
				for (int32 Y = 2; Y <= 9; ++Y)
				{
					for (int32 X = 1; X <= 14; ++X)
					{
						const float DX = (X + 0.5f - 8.f) / 6.6f, DY = (Y + 0.5f - 9.5f) / 7.f;
						if (DX * DX + DY * DY > 1.f) { continue; }
						Img.Set(X, Y, (DX * DX + DY * DY < 0.35f) ? FVArt::Mix(P, S, 0.6f) : (DX < -0.5f ? FVArt::Light(P, 0.3f) : P));
					}
				}
				for (int32 T = 0; T < 4; ++T) { for (int32 Y = 10; Y <= 13 + (T % 2); ++Y) { Img.Set(3 + T * 3 + ((Y / 2) % 2), Y, FVArt::Light(P, 0.3f)); } }
				Img.Outline(Ink);
				Halo(Img, 8.f, 6.f, 7.f, S, 80);
				return Img;
			}
		}
		case EFVDecorSlot::Plant:
		{
			const FFVImage W = PlantWorld(St);
			// the world plant fits the icon when scaled to at most 14 px tall (nearest sampling)
			const float K = FMath::Min(14.f / W.H, 14.f / W.W);
			const int32 NW = FMath::Max(1, FMath::RoundToInt(W.W * K)), NH = FMath::Max(1, FMath::RoundToInt(W.H * K));
			for (int32 Y = 0; Y < NH; ++Y)
			{
				for (int32 X = 0; X < NW; ++X)
				{
					const FColor C = W.Get(FMath::Min(W.W - 1, int32((X + 0.5f) / K)), FMath::Min(W.H - 1, int32((Y + 0.5f) / K)));
					if (C.A > 0 && C != PropOut) { Img.Set(8 - NW / 2 + X, 15 - NH + Y - 1, C); }
				}
			}
			Img.Outline(Ink);
			return Img;
		}
		case EFVDecorSlot::Painting:
		{
			const FFVImage W = PaintingWorld(St);
			const float K = FMath::Min(14.f / W.W, 13.f / W.H);
			const int32 NW = FMath::Max(1, FMath::RoundToInt(W.W * K)), NH = FMath::Max(1, FMath::RoundToInt(W.H * K));
			for (int32 Y = 0; Y < NH; ++Y)
			{
				for (int32 X = 0; X < NW; ++X)
				{
					const FColor C = W.Get(FMath::Min(W.W - 1, int32((X + 0.5f) / K)), FMath::Min(W.H - 1, int32((Y + 0.5f) / K)));
					if (C.A > 0 && C != PropOut) { Img.Set(8 - NW / 2 + X, 8 - NH / 2 + Y, C); }
				}
			}
			Img.Outline(Ink);
			return Img;
		}
		case EFVDecorSlot::Curtains:
		default:
		{
			// a small window with the curtains of this style
			Rect(Img, 4, 3, 8, 11, FColor(150, 206, 240));
			HLine(Img, 4, 11, 8, FColor(250, 246, 236)); VLine(Img, 7, 3, 13, FColor(250, 246, 236)); VLine(Img, 8, 3, 13, FColor(230, 222, 206));
			Img.Set(5, 4, FColor(220, 244, 255)); Img.Set(9, 9, FColor(220, 244, 255));
			const FFVImage Cw = CurtainsWorld(St);
			const FColor Cl = Cw.Get(4, 10), Cd = Cw.Get(6, 10), Tr = Cw.Get(3, 18);
			for (int32 Y = 2; Y <= 14; ++Y)
			{
				const int32 Pinch = (Y >= 8 && Y <= 11) ? 1 : 0;
				for (int32 X = 1; X <= 4 - Pinch; ++X) { Img.Set(X, Y, X % 2 ? Cl : Cd); }
				for (int32 X = 11 + Pinch; X <= 14; ++X) { Img.Set(X, Y, X % 2 ? Cd : Cl); }
			}
			Img.Set(2, 9, Tr); Img.Set(3, 9, Tr); Img.Set(12, 9, Tr); Img.Set(13, 9, Tr);
			HLine(Img, 0, 15, 1, DWood);
			Img.Outline(Ink);
			return Img;
		}
		}
	}

	// ---------------------------------------------------------------------------------------- trophy plaque & dresser
	FFVImage BuildTrophyPlaque(const FFVFishDef& Fish)
	{
		FFVImage Img(30, 22);
		// rounded shield
		for (int32 Y = 1; Y <= 20; ++Y)
		{
			for (int32 X = 1; X <= 28; ++X)
			{
				const int32 CX = X < 4 ? 4 - X : (X > 25 ? X - 25 : 0), CY = Y < 4 ? 4 - Y : (Y > 17 ? Y - 17 : 0);
				if (CX * CX + CY * CY > 10) { continue; }
				Img.Set(X, Y, DWood);
			}
		}
		for (int32 Y = 1; Y <= 20; ++Y)
		{
			for (int32 X = 1; X <= 28; ++X)
			{
				if (Img.Get(X, Y).A == 0) { continue; }
				const bool bEdgeTL = Img.Get(X - 1, Y).A == 0 || Img.Get(X, Y - 1).A == 0;
				const bool bEdgeBR = Img.Get(X + 1, Y).A == 0 || Img.Get(X, Y + 1).A == 0;
				if (bEdgeTL) { Img.Set(X, Y, DWoodHi); } else if (bEdgeBR) { Img.Set(X, Y, DWoodDk); }
			}
		}
		Box(Img, 3, 3, 24, 13, WoodMid, 0.0f, 0.0f);
		HLine(Img, 3, 26, 3, WoodLo); VLine(Img, 3, 3, 15, WoodLo); HLine(Img, 3, 26, 15, WoodHi); VLine(Img, 26, 3, 15, WoodHi);
		Rect(Img, 10, 17, 10, 3, Brass); HLine(Img, 10, 19, 17, BrassHi); HLine(Img, 12, 17, 18, BrassLo); HLine(Img, 10, 19, 19, BrassLo);
		Img.Set(4, 4, BrassHi); Img.Set(25, 4, BrassHi); Img.Set(4, 14, BrassHi); Img.Set(25, 14, BrassHi);
		Img.Outline(PropOut);
		// the fish (a sprite over 24x13 — the legends — is scaled down to fit 24x11 before its outline)
		FFVImage F = UFVSprites::BuildFish(Fish, false);
		if (F.IsValid())
		{
			if (F.W > 24 || F.H > 13)
			{
				// Update 5: downscale the fish WITHOUT its outline ring; every texel takes the most common colour of its
				// source block and stays fish when at least a third of the block is fish, so the serpent's thin S-body,
				// the sturgeon's scutes and the angler's rod survive (nearest sampling broke them up).
				const FFVImage In = FVArtItemsImpl::BuildFishInterior(Fish);
				const int32 IW = FMath::Max(1, In.W - 2), IH = FMath::Max(1, In.H - 2);   // the template area
				const float K = FMath::Min(24.f / IW, 11.f / IH);
				const int32 NW = FMath::Max(1, FMath::RoundToInt(IW * K)), NH = FMath::Max(1, FMath::RoundToInt(IH * K));
				FFVImage Small(NW, NH);
				TArray<TPair<FColor, int32>, TInlineAllocator<16>> Votes;
				for (int32 Y = 0; Y < NH; ++Y)
				{
					for (int32 X = 0; X < NW; ++X)
					{
						const int32 X0 = int32(X / K), X1 = FMath::Min(IW, FMath::Max(X0 + 1, int32((X + 1) / K)));
						const int32 Y0 = int32(Y / K), Y1 = FMath::Min(IH, FMath::Max(Y0 + 1, int32((Y + 1) / K)));
						Votes.Reset();
						int32 Total = 0, Filled = 0;
						for (int32 SY = Y0; SY < Y1; ++SY)
						{
							for (int32 SX = X0; SX < X1; ++SX)
							{
								++Total;
								const FColor C = In.Get(SX + 1, SY + 1);
								if (C.A == 0)
								{
									continue;
								}
								++Filled;
								TPair<FColor, int32>* V = Votes.FindByPredicate([&C](const TPair<FColor, int32>& P) { return P.Key == C; });
								if (V) { ++V->Value; } else { Votes.Emplace(C, 1); }
							}
						}
						if (Filled > 0 && Filled * 3 >= Total)
						{
							const TPair<FColor, int32>* Best = &Votes[0];
							for (const TPair<FColor, int32>& V : Votes) { if (V.Value > Best->Value) { Best = &V; } }
							Small.Set(X, Y, Best->Key);
						}
					}
				}
				FFVImage Padded(NW + 2, NH + 2);
				Padded.Blit(Small, 1, 1);
				Padded.Outline(FVArt::OutlineOf(Fish.BodyColor));
				F = Padded;
			}
			// a soft shadow of the mounted fish on the board, then the fish
			for (int32 Y = 0; Y < F.H; ++Y) { for (int32 X = 0; X < F.W; ++X) { if (F.Get(X, Y).A > 0) { Img.Blend(15 - F.W / 2 + X + 1, 9 - F.H / 2 + Y + 1, FColor(40, 24, 20, 90)); } } }
			Img.Blit(F, 15 - F.W / 2, 9 - F.H / 2);
		}
		return Img;
	}

	FFVImage BuildDresser()
	{
		FFVImage Img(24, 26);
		Box(Img, 2, 8, 20, 15, DWood, 0.2f, 0.35f);
		Box(Img, 1, 7, 22, 2, DWoodHi, 0.3f, 0.3f);
		for (int32 D = 0; D < 3; ++D)
		{
			const int32 Y = 10 + D * 4;
			Box(Img, 4, Y, 16, 3, DWoodHi, 0.25f, 0.35f);
			Img.Set(7, Y + 1, BrassHi); Img.Set(16, Y + 1, BrassHi);
		}
		VLine(Img, 3, 23, 24, DWoodLo); VLine(Img, 4, 23, 23, DWoodLo); VLine(Img, 19, 23, 23, DWoodLo); VLine(Img, 20, 23, 24, DWoodLo);
		// open catalog book
		Rect(Img, 4, 4, 8, 3, FColor(250, 246, 234)); VLine(Img, 8, 4, 6, FColor(210, 200, 186));
		HLine(Img, 5, 7, 5, Scribble); HLine(Img, 9, 11, 5, FColor(210, 110, 120));
		HLine(Img, 3, 12, 7, FColor(180, 60, 60)); Img.Set(3, 6, FColor(180, 60, 60)); Img.Set(12, 6, FColor(180, 60, 60));
		// vase with a flower
		Rect(Img, 16, 3, 3, 4, FColor(90, 130, 196)); Img.Set(16, 3, FColor(140, 180, 230)); Img.Set(18, 6, FColor(60, 96, 160));
		VLine(Img, 17, 1, 2, Leaf); Img.Set(17, 0, FColor(250, 120, 170)); Img.Set(16, 1, FColor(250, 160, 196)); Img.Set(18, 1, FColor(230, 80, 140));
		Img.Outline(PropOut);
		return Img;
	}

	// ---------------------------------------------------------------------------------------- station dispatch
	FVector2D StationSize(EFVStation S, int32 Level)
	{
		switch (S)
		{
		case EFVStation::Kitchen:    return Level >= 4 ? FVector2D(44, 44) : FVector2D(32, 32);
		case EFVStation::Workshop:   return Level >= 4 ? FVector2D(48, 30) : FVector2D(40, 28);
		case EFVStation::Smokehouse: return Level >= 3 ? FVector2D(30, 44) : FVector2D(26, 40);
		case EFVStation::WormFarm:   return Level >= 3 ? FVector2D(32, 20) : FVector2D(28, 18);
		case EFVStation::Aquarium:   return Level >= 4 ? FVector2D(56, 36) : (Level >= 3 ? FVector2D(44, 30) : (Level == 2 ? FVector2D(32, 28) : FVector2D(24, 24)));
		case EFVStation::IceCellar:  return Level >= 3 ? FVector2D(28, 22) : FVector2D(24, 18);
		case EFVStation::Radio:      return Level >= 2 ? FVector2D(28, 28) : FVector2D(24, 26);
		case EFVStation::Bed:        return FVector2D(28, 40);
		case EFVStation::TrophyWall: return Level >= 2 ? FVector2D(96, 24) : FVector2D(48, 24);
		case EFVStation::Chest:      return FVector2D(20, 16);
		default:                     return FVector2D(24, 24);
		}
	}

	/** Highest drawn level (update 6 §9б: the expert levels above 3). */
	int32 MaxArtLevel(EFVStation S)
	{
		switch (S)
		{
		case EFVStation::Kitchen: case EFVStation::Workshop: case EFVStation::Aquarium: return 4;
		case EFVStation::Smokehouse: case EFVStation::WormFarm: case EFVStation::IceCellar: return 3;
		case EFVStation::Radio: case EFVStation::TrophyWall: return 2;
		default: return 1;
		}
	}

	// ---------------------------------------------------------------------------------------- 3x5 pixel font (sheet labels)
	uint16 GlyphBits(TCHAR C)
	{
		// 5 rows x 3 bits (row 0 = top, bit 2 = left)
		auto G = [](int32 A, int32 B, int32 Cc, int32 D, int32 E) { return uint16((A << 12) | (B << 9) | (Cc << 6) | (D << 3) | E); };
		if (C >= TEXT('a') && C <= TEXT('z')) { C = TCHAR(C - TEXT('a') + TEXT('A')); }
		switch (C)
		{
		case TEXT('A'): return G(2, 5, 7, 5, 5); case TEXT('B'): return G(6, 5, 6, 5, 6); case TEXT('C'): return G(3, 4, 4, 4, 3);
		case TEXT('D'): return G(6, 5, 5, 5, 6); case TEXT('E'): return G(7, 4, 6, 4, 7); case TEXT('F'): return G(7, 4, 6, 4, 4);
		case TEXT('G'): return G(3, 4, 5, 5, 3); case TEXT('H'): return G(5, 5, 7, 5, 5); case TEXT('I'): return G(7, 2, 2, 2, 7);
		case TEXT('J'): return G(1, 1, 1, 5, 2); case TEXT('K'): return G(5, 5, 6, 5, 5); case TEXT('L'): return G(4, 4, 4, 4, 7);
		case TEXT('M'): return G(5, 7, 7, 5, 5); case TEXT('N'): return G(6, 5, 5, 5, 5); case TEXT('O'): return G(2, 5, 5, 5, 2);
		case TEXT('P'): return G(6, 5, 6, 4, 4); case TEXT('Q'): return G(2, 5, 5, 6, 3); case TEXT('R'): return G(6, 5, 6, 5, 5);
		case TEXT('S'): return G(3, 4, 2, 1, 6); case TEXT('T'): return G(7, 2, 2, 2, 2); case TEXT('U'): return G(5, 5, 5, 5, 7);
		case TEXT('V'): return G(5, 5, 5, 5, 2); case TEXT('W'): return G(5, 5, 7, 7, 5); case TEXT('X'): return G(5, 5, 2, 5, 5);
		case TEXT('Y'): return G(5, 5, 2, 2, 2); case TEXT('Z'): return G(7, 1, 2, 4, 7);
		case TEXT('0'): return G(7, 5, 5, 5, 7); case TEXT('1'): return G(2, 6, 2, 2, 7); case TEXT('2'): return G(6, 1, 2, 4, 7);
		case TEXT('3'): return G(6, 1, 2, 1, 6); case TEXT('4'): return G(5, 5, 7, 1, 1); case TEXT('5'): return G(7, 4, 6, 1, 6);
		case TEXT('6'): return G(3, 4, 7, 5, 7); case TEXT('7'): return G(7, 1, 2, 2, 2); case TEXT('8'): return G(7, 5, 7, 5, 7);
		case TEXT('9'): return G(7, 5, 7, 1, 6);
		case TEXT('-'): return G(0, 0, 7, 0, 0); case TEXT('.'): return G(0, 0, 0, 0, 2); case TEXT(':'): return G(0, 2, 0, 2, 0);
		case TEXT('/'): return G(1, 1, 2, 4, 4); case TEXT('('): return G(1, 2, 2, 2, 1); case TEXT(')'): return G(4, 2, 2, 2, 4);
		case TEXT('+'): return G(0, 2, 7, 2, 0); case TEXT('_'): return G(0, 0, 0, 0, 7); case TEXT('='): return G(0, 7, 0, 7, 0);
		default: return 0;
		}
	}

	void DrawText(FFVImage& I, int32 X, int32 Y, const FString& Text, FColor C)
	{
		int32 CX = X;
		for (int32 N = 0; N < Text.Len(); ++N)
		{
			const uint16 Bits = GlyphBits(Text[N]);
			for (int32 R = 0; R < 5; ++R)
			{
				for (int32 B = 0; B < 3; ++B)
				{
					if (Bits & (1 << ((4 - R) * 3 + (2 - B))))
					{
						I.Set(CX + B + 1, Y + R + 1, FColor(40, 40, 48));
						I.Set(CX + B, Y + R, C);
					}
				}
			}
			CX += 4;
		}
	}

	/** Labeled contact sheet: rows of captioned cells, section titles. */
	struct FHomeSheet
	{
		struct FCell { FFVImage Img; FString Label; int32 X = 0; int32 Y = 0; };
		struct FTitle { FString Text; int32 Y = 0; };
		TArray<FCell> Cells;
		TArray<FTitle> Titles;
		int32 MaxW = 900, CurX = 4, CurY = 4, RowH = 0, TotalW = 0;

		void Title(const FString& T)
		{
			NewRow();
			if (CurY > 4) { CurY += 6; }
			FTitle& Tt = Titles.AddDefaulted_GetRef();
			Tt.Text = T; Tt.Y = CurY;
			CurY += 9;
		}
		void Add(const FFVImage& Img, const FString& Label)
		{
			if (!Img.IsValid()) { return; }
			const int32 CellW = FMath::Max(Img.W, Label.Len() * 4) + 4;
			if (CurX > 4 && CurX + CellW > MaxW) { NewRow(); }
			FCell& C = Cells.AddDefaulted_GetRef();
			C.Img = Img; C.Label = Label; C.X = CurX; C.Y = CurY;
			CurX += CellW;
			RowH = FMath::Max(RowH, Img.H + 8);
			TotalW = FMath::Max(TotalW, CurX);
		}
		void NewRow()
		{
			if (CurX == 4) { return; }
			CurY += RowH + 3;
			CurX = 4;
			RowH = 0;
		}
		FFVImage Render(int32 Scale)
		{
			NewRow();
			FFVImage Sheet(FMath::Max(16, TotalW + 4), FMath::Max(16, CurY + 4), FColor(118, 122, 132));
			for (const FTitle& T : Titles)
			{
				HLine(Sheet, 2, Sheet.W - 3, T.Y + 7, FColor(90, 94, 104));
				DrawText(Sheet, 4, T.Y, T.Text, FColor(255, 232, 140));
			}
			for (const FCell& C : Cells)
			{
				// checker behind every cell so transparency is visible
				for (int32 Y = 0; Y < C.Img.H; ++Y)
				{
					for (int32 X = 0; X < C.Img.W; ++X)
					{
						Sheet.Set(C.X + X, C.Y + Y, (((X / 4) + (Y / 4)) % 2) ? FColor(134, 138, 148) : FColor(146, 150, 160));
					}
				}
				Sheet.Blit(C.Img, C.X, C.Y);
				DrawText(Sheet, C.X, C.Y + C.Img.H + 2, C.Label, FColor(240, 240, 244));
			}
			if (Scale <= 1) { return Sheet; }
			FFVImage Big(Sheet.W * Scale, Sheet.H * Scale);
			for (int32 Y = 0; Y < Big.H; ++Y)
			{
				for (int32 X = 0; X < Big.W; ++X) { Big.Px[Y * Big.W + X] = Sheet.Px[(Y / Scale) * Sheet.W + (X / Scale)]; }
			}
			return Big;
		}
	};

	const TCHAR* StationTag(EFVStation S)
	{
		switch (S)
		{
		case EFVStation::Kitchen: return TEXT("KITCHEN"); case EFVStation::Workshop: return TEXT("WORKSHOP");
		case EFVStation::Smokehouse: return TEXT("SMOKE"); case EFVStation::WormFarm: return TEXT("WORMS");
		case EFVStation::Aquarium: return TEXT("AQUA"); case EFVStation::IceCellar: return TEXT("ICE");
		case EFVStation::Radio: return TEXT("RADIO"); case EFVStation::Bed: return TEXT("BED");
		case EFVStation::TrophyWall: return TEXT("TROPHY"); case EFVStation::Chest: return TEXT("CHEST");
		default: return TEXT("?");
		}
	}
}

// ============================================================================================
// UFVSprites: update-2 builders (pure)
// ============================================================================================
FFVImage UFVSprites::BuildStation(EFVStation S, int32 Level)
{
	using namespace FVArtHomeImpl;
	if (S >= EFVStation::Count)
	{
		return BuildMissing();
	}
	const int32 L = FMath::Clamp(Level, 0, MaxArtLevel(S));
	if (L <= 0)
	{
		const FVector2D Sz = StationSize(S, 1);
		return BuildSite(int32(Sz.X), int32(Sz.Y), S == EFVStation::TrophyWall);
	}
	switch (S)
	{
	case EFVStation::Kitchen:    return BuildKitchen(L);
	case EFVStation::Workshop:   return BuildWorkshop(L);
	case EFVStation::Smokehouse: return BuildSmokehouse(L);
	case EFVStation::WormFarm:   return BuildWormFarm(L);
	case EFVStation::Aquarium:   return BuildAquarium(L);
	case EFVStation::IceCellar:  return BuildIceCellar(L);
	case EFVStation::Radio:      return BuildRadio(L);
	case EFVStation::Bed:        return BuildBed();
	case EFVStation::TrophyWall: return BuildTrophyWall(L);
	case EFVStation::Chest:      return BuildHouseChest();
	default:                     return BuildMissing();
	}
}

FFVImage UFVSprites::BuildStall(FName TraderId)
{
	return FVArtHomeImpl::BuildStallImpl(TraderId == FName(TEXT("tr_pier")));
}

FFVImage UFVSprites::BuildDecorWorld(const FFVDecorDef& Def)
{
	using namespace FVArtHomeImpl;
	FColor P, S;
	DecorColors(Def, P, S);
	switch (Def.Slot)
	{
	case EFVDecorSlot::Wallpaper: return WallpaperTile(Def.Style, P, S);
	case EFVDecorSlot::Floor:     return FloorTile(Def.Style, P, S);
	case EFVDecorSlot::Rug:       return RugWorld(Def.Style, P, S);
	case EFVDecorSlot::Lamp:      return LampWorld(Def.Style, P, S);
	case EFVDecorSlot::Plant:     return PlantWorld(Def.Style);
	case EFVDecorSlot::Painting:  return PaintingWorld(Def.Style);
	case EFVDecorSlot::Curtains:  return CurtainsWorld(Def.Style);
	default:                      break;
	}
	// update 6: yard decor / furniture
	if (uint8(Def.Slot) >= FVDecor::ClassicSlots && Def.Slot < EFVDecorSlot::Count)
	{
		FFVImage U6 = FVArtDecorU6::BuildWorld(Def);
		if (U6.IsValid()) { return U6; }
	}
	return BuildMissing();
}

FFVImage UFVSprites::BuildDecorIcon(const FFVDecorDef& Def)
{
	// update 6: yard decor / furniture
	if (uint8(Def.Slot) >= FVDecor::ClassicSlots && Def.Slot < EFVDecorSlot::Count)
	{
		FFVImage U6 = FVArtDecorU6::BuildIcon(Def);
		if (U6.IsValid()) { return U6; }
	}
	return FVArtHomeImpl::DecorIconImpl(Def);
}

// ============================================================================================
// UFVSprites: update-2 cached getters
// ============================================================================================
namespace FVArtHomeImpl
{
	uint32 DecorVisHash(const FFVDecorDef& Def)
	{
		uint32 H = GetTypeHash(uint8(Def.Slot));
		H = HashCombine(H, GetTypeHash(Def.Style));
		H = HashCombine(H, GetTypeHash(Def.Primary));
		H = HashCombine(H, GetTypeHash(Def.Secondary));
		return H;
	}
}

const FFVSprite& UFVSprites::DecorIcon(const FFVDecorDef& Def)
{
	const uint64 Key = MakeKey(FVSpriteCat::Decor, GetTypeHash(Def.Id), FVArtHomeImpl::DecorVisHash(Def), 0);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildDecorIcon(Def), FString::Printf(TEXT("DecorIcon_%s"), *Def.Id.ToString()));
}

const FFVSprite& UFVSprites::DecorWorld(const FFVDecorDef& Def)
{
	const uint64 Key = MakeKey(FVSpriteCat::Decor, GetTypeHash(Def.Id), FVArtHomeImpl::DecorVisHash(Def), 1);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildDecorWorld(Def), FString::Printf(TEXT("DecorWorld_%s"), *Def.Id.ToString()));
}

const FFVSprite& UFVSprites::StationSprite(EFVStation S, int32 Level)
{
	const int32 L = FMath::Clamp(Level, 0, FVArtHomeImpl::MaxArtLevel(S));
	const uint64 Key = MakeKey(FVSpriteCat::Station, uint32(S), uint32(L));
	if (const FFVSprite* Found = FindCached(Key))
	{
		return *Found;
	}
	return AddCached(Key, BuildStation(S, L), FString::Printf(TEXT("Station_%d_%d"), int32(S), L));
}

const FFVSprite& UFVSprites::StallSprite(FName TraderId)
{
	const bool bPier = TraderId == FName(TEXT("tr_pier"));
	const uint64 Key = MakeKey(FVSpriteCat::Stall, bPier ? 2u : 1u);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildStall(TraderId), bPier ? TEXT("Stall_Pier") : TEXT("Stall_Mitrich"));
}

const FFVSprite& UFVSprites::OrderBoardSprite(int32 Papers)
{
	const int32 P = FMath::Clamp(Papers, 0, 3);
	const uint64 Key = MakeKey(FVSpriteCat::HomeProp, uint32(P), 0, 1);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, FVArtHomeImpl::BuildOrderBoard(P), FString::Printf(TEXT("OrderBoard_%d"), P));
}

const FFVSprite& UFVSprites::NetPostSprite(bool bHasNet)
{
	const uint64 Key = MakeKey(FVSpriteCat::HomeProp, bHasNet ? 1u : 0u, 0, 2);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, FVArtHomeImpl::BuildNetPost(bHasNet), bHasNet ? TEXT("NetPost_Net") : TEXT("NetPost_Empty"));
}

const FFVSprite& UFVSprites::NetWaterSprite(int32 Tier, int32 Fill, bool bTorn)
{
	const int32 T = FMath::Clamp(Tier, 1, 3), F = FMath::Clamp(Fill, 0, 3);
	const uint64 Key = MakeKey(FVSpriteCat::HomeProp, uint32(T * 16 + F), bTorn ? 1u : 0u, 3);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, FVArtHomeImpl::BuildNetWater(T, F, bTorn), FString::Printf(TEXT("NetWater_%d_%d_%d"), T, F, bTorn ? 1 : 0));
}

const FFVSprite& UFVSprites::DigSpotSprite(bool bDugToday)
{
	const uint64 Key = MakeKey(FVSpriteCat::HomeProp, bDugToday ? 1u : 0u, 0, 4);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, FVArtHomeImpl::BuildDigSpot(bDugToday), bDugToday ? TEXT("DigSpot_Dug") : TEXT("DigSpot"));
}

const FFVSprite& UFVSprites::TrophyPlaque(const FFVFishDef& Fish)
{
	uint32 Vis = GetTypeHash(uint8(Fish.Shape));
	Vis = HashCombine(Vis, GetTypeHash(uint8(Fish.Pattern)));
	Vis = HashCombine(Vis, GetTypeHash(Fish.BodyColor));
	Vis = HashCombine(Vis, GetTypeHash(Fish.FinColor));
	const uint64 Key = MakeKey(FVSpriteCat::Trophy, GetTypeHash(Fish.Id), Vis);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, FVArtHomeImpl::BuildTrophyPlaque(Fish), FString::Printf(TEXT("Trophy_%s"), *Fish.Id.ToString()));
}

const FFVSprite& UFVSprites::DresserSprite()
{
	const uint64 Key = MakeKey(FVSpriteCat::HomeProp, 0, 0, 5);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, FVArtHomeImpl::BuildDresser(), TEXT("Dresser"));
}

// ============================================================================================
// Layout helpers (texels from the sprite's top-left)
// ============================================================================================
FFVRect UFVSprites::AquariumWaterRect(int32 Level)
{
	if (Level >= 4) { return FFVRect(3.f, 7.f, 50.f, 11.f); }
	if (Level >= 3) { return FFVRect(3.f, 7.f, 38.f, 10.f); }
	if (Level == 2) { return FFVRect(3.f, 7.f, 26.f, 8.f); }
	return FFVRect(6.f, 8.f, 12.f, 7.f);
}

FVector2D UFVSprites::TrophySlotCenter(int32 Index)
{
	// 0..5 = the L1 wall (= the left half of L2), 6..11 = the right half of the L2 wall
	return FVArtHomeImpl::TrophySlots[FMath::Clamp(Index, 0, 11)];
}

FVector2D UFVSprites::StationEffectPoint(EFVStation S, int32 Level)
{
	if (Level <= 0)
	{
		return FVector2D(-1.f, -1.f);
	}
	switch (S)
	{
	case EFVStation::Kitchen:    return Level >= 4 ? FVector2D(16.5f, 29.f) : (Level >= 3 ? FVector2D(16.f, 21.f) : (Level == 2 ? FVector2D(10.5f, 22.f) : FVector2D(16.f, 25.f)));
	case EFVStation::Smokehouse: return Level >= 3 ? FVector2D(23.f, 0.f) : FVector2D(18.f, 0.f);
	case EFVStation::Radio:      return Level >= 2 ? FVector2D(11.f, 8.f) : FVector2D(15.5f, 5.f);
	case EFVStation::Workshop:   return Level >= 4 ? FVector2D(44.5f, 5.f) : (Level >= 3 ? FVector2D(33.f, 5.f) : FVector2D(-1.f, -1.f));
	default:                     return FVector2D(-1.f, -1.f);
	}
}

FVector2D UFVSprites::DecorLightPoint(const FFVDecorDef& Def)
{
	if (Def.Slot != EFVDecorSlot::Lamp)
	{
		return FVector2D(-1.f, -1.f);
	}
	switch (Def.Style % 4)
	{
	case 0:  return FVector2D(5.5f, 2.f);
	case 1:  return FVector2D(6.5f, 7.f);
	case 2:  return FVector2D(6.f, 8.f);
	default: return FVector2D(8.f, 6.f);
	}
}

// ============================================================================================
// Debug: labeled contact sheet of all update-2 art
// ============================================================================================
void UFVSprites::DebugAddHomeSheet(const FString& FilePath)
{
	using namespace FVArtHomeImpl;
	FVDB::Init();
	FHomeSheet SB;

	SB.Title(TEXT("ICONS (UPDATE 2)"));
	static const TCHAR* IconNames[] = { TEXT("BACKPACK"), TEXT("HOUSE"), TEXT("HAMMER"), TEXT("POT"), TEXT("NET"), TEXT("SHOVEL"),
		TEXT("ORDER"), TEXT("SNOW"), TEXT("RADIO"), TEXT("BED"), TEXT("SMOKE"), TEXT("AQUA"), TEXT("HAND"), TEXT("DOOR"), TEXT("KNOT"),
		TEXT("FOOD"), TEXT("LINE"), TEXT("FLOAT"), TEXT("CHEST"),
		TEXT("W-CURRENT"), TEXT("W-EYE"), TEXT("W-LANTERN"), TEXT("W-CLOUD"), TEXT("W-FLAME"), TEXT("W-SPARK") };   // update 5 warnings
	for (int32 I = int32(EFVIcon::Backpack); I < int32(EFVIcon::Count); ++I)
	{
		const int32 N = I - int32(EFVIcon::Backpack);
		SB.Add(BuildIcon(EFVIcon(I)), N < int32(UE_ARRAY_COUNT(IconNames)) ? FString(IconNames[N]) : FString::FromInt(I));
	}

	SB.Title(TEXT("NEW BAIT"));
	SB.Add(BuildBait(EFVBaitStyle::Nightcrawler), TEXT("CRAWLER"));
	SB.Add(BuildBait(EFVBaitStyle::Dough), TEXT("DOUGH"));
	SB.Add(BuildBait(EFVBaitStyle::Groundbait), TEXT("GROUND"));
	SB.Add(BuildBait(EFVBaitStyle::Fly), TEXT("FLY"));

	SB.Title(TEXT("ITEMS"));
	if (FVDB::Items().Num() > 0)
	{
		for (const FFVItemDef& It : FVDB::Items())
		{
			FString L = It.Id.ToString();
			L.RemoveFromStart(TEXT("res_")); L.RemoveFromStart(TEXT("mat_")); L.RemoveFromStart(TEXT("food_"));
			SB.Add(BuildItemArt(It.Art, It.Tint, It.Tier), L.Left(8));
		}
	}
	else
	{
		for (int32 A = 0; A < int32(EFVItemArt::Count); ++A) { SB.Add(BuildItemArt(EFVItemArt(A), FColor(200, 180, 130), 1), FString::FromInt(A)); }
	}
	SB.NewRow();
	for (int32 T = 1; T <= 5; ++T) { SB.Add(BuildItemArt(EFVItemArt::Line, FColor(150, 210, 235), uint8(T)), FString::Printf(TEXT("LINE%d"), T)); }
	for (int32 T = 1; T <= 3; ++T) { SB.Add(BuildItemArt(EFVItemArt::Net, FColor(150, 170, 110), uint8(T)), FString::Printf(TEXT("NET%d"), T)); }

	SB.Title(TEXT("LURES  (BASE COLOR GLITTER)"));
	for (int32 B = 0; B < 2; ++B)
	{
		for (int32 C = 0; C < 4; ++C)
		{
			for (int32 G = 0; G < 3; ++G) { SB.Add(BuildLureIcon(uint8(B), uint8(C), uint8(G)), FString::Printf(TEXT("%d%d%d"), B, C, G)); }
		}
		SB.NewRow();
	}

	SB.Title(TEXT("SHOVELS"));
	for (int32 L = 1; L <= 4; ++L) { SB.Add(BuildShovelIcon(L), FString::Printf(TEXT("L%d"), L)); }

	SB.Title(TEXT("STATIONS (LEVEL 0 = SITE)"));
	for (int32 S = 0; S < int32(EFVStation::Count); ++S)
	{
		const EFVStation St = EFVStation(S);
		const int32 From = (St == EFVStation::Bed || St == EFVStation::Chest) ? 1 : 0;
		for (int32 L = From; L <= MaxArtLevel(St); ++L)
		{
			SB.Add(BuildStation(St, L), FString::Printf(TEXT("%s %d"), StationTag(St), L));
		}
		if (St == EFVStation::Smokehouse || St == EFVStation::Aquarium || St == EFVStation::Radio) { SB.NewRow(); }
	}

	SB.Title(TEXT("STALLS  BOARD  NETS  DIG"));
	SB.Add(BuildStall(FName(TEXT("tr_mitrich"))), TEXT("MITRICH"));
	SB.Add(BuildStall(FName(TEXT("tr_pier"))), TEXT("PIER"));
	for (int32 P = 0; P <= 3; ++P) { SB.Add(BuildOrderBoard(P), FString::Printf(TEXT("BOARD%d"), P)); }
	SB.Add(BuildNetPost(false), TEXT("POST"));
	SB.Add(BuildNetPost(true), TEXT("POST+NET"));
	SB.Add(BuildDigSpot(false), TEXT("DIG"));
	SB.Add(BuildDigSpot(true), TEXT("DUG"));
	SB.NewRow();
	for (int32 T = 1; T <= 3; ++T)
	{
		for (int32 F = 0; F <= 3; ++F) { SB.Add(BuildNetWater(T, F, false), FString::Printf(TEXT("T%d F%d"), T, F)); }
		SB.Add(BuildNetWater(T, 2, true), FString::Printf(TEXT("T%d TORN"), T));
		SB.NewRow();
	}

	SB.Title(TEXT("DECOR: ICON / WORLD"));
	TArray<FFVDecorDef> Decor = FVDB::Decor();
	if (Decor.Num() == 0)
	{
		const int32 Styles[int32(EFVDecorSlot::Count)] = { 5, 4, 3, 4, 4, 4, 3 };
		for (int32 Sl = 0; Sl < int32(EFVDecorSlot::Count); ++Sl)
		{
			for (int32 St = 0; St < Styles[Sl]; ++St)
			{
				FFVDecorDef D;
				D.Id = FName(*FString::Printf(TEXT("decor_%d_%d"), Sl, St));
				D.Slot = EFVDecorSlot(Sl);
				D.Style = uint8(St);
				Decor.Add(D);
			}
		}
	}
	EFVDecorSlot LastSlot = EFVDecorSlot::Count;
	for (const FFVDecorDef& D : Decor)
	{
		if (D.Slot != LastSlot) { SB.NewRow(); LastSlot = D.Slot; }
		FString L = D.Id.ToString();
		L.RemoveFromStart(TEXT("decor_"));
		SB.Add(BuildDecorIcon(D), L.Left(9));
		const FFVImage W = BuildDecorWorld(D);
		if (D.Slot == EFVDecorSlot::Wallpaper || D.Slot == EFVDecorSlot::Floor)
		{
			FFVImage Tiled(48, 32);
			for (int32 Y = 0; Y < 32; ++Y) { for (int32 X = 0; X < 48; ++X) { Tiled.Set(X, Y, W.Get(X % 16, Y % 16)); } }
			SB.Add(Tiled, TEXT("TILED"));
		}
		else
		{
			SB.Add(W, TEXT(""));
		}
	}

	SB.Title(TEXT("TROPHY PLAQUES  DRESSER"));
	int32 NPlaques = 0;
	for (const FFVFishDef& F : FVDB::Fish())
	{
		if (NPlaques >= 12) { break; }
		if (F.bLegendary || (NPlaques % 2 == 0 && F.Rarity >= EFVRarity::Uncommon) || NPlaques < 4)
		{
			FString L = F.Id.ToString();
			L.RemoveFromStart(TEXT("fish_"));
			SB.Add(BuildTrophyPlaque(F), L.Left(8));
			++NPlaques;
		}
	}
	SB.Add(BuildDresser(), TEXT("DRESSER"));

	// Update 5: the six legend silhouettes (sprite / unknown-fish silhouette / trophy plaque), the Ember bait, fried
	// dishes by rarity, the rarity fish tokens and the legend bobber skins (both frames, 2x). A legend whose
	// FFVFishDef::Legend is not set in the data yet is drawn from the n-th legendary fish with the tag forced: (F).
	SB.Title(TEXT("UPDATE 5: LEGENDS   (F) = LEGEND NOT SET IN DATA"));
	{
		static const TCHAR* LegendTags[] = { TEXT("TSAR"), TEXT("STURGEON"), TEXT("CRYSTAL"), TEXT("ANGLER"), TEXT("FOG"), TEXT("ZHAR") };
		TArray<const FFVFishDef*> Legendary;
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (F.bLegendary) { Legendary.Add(&F); }
		}
		TArray<FFVFishDef> Legends;
		TArray<FString> Tags;
		for (int32 L = 1; L < int32(EFVLegend::Count); ++L)
		{
			const FString Tag = (L - 1) < int32(UE_ARRAY_COUNT(LegendTags)) ? FString(LegendTags[L - 1]) : FString::FromInt(L);
			if (const FFVFishDef* Def = FVDB::FindLegend(EFVLegend(L)))
			{
				Legends.Add(*Def);
				Tags.Add(Tag);
			}
			else if (Legendary.IsValidIndex(L - 1))
			{
				FFVFishDef Forced = *Legendary[L - 1];
				Forced.Legend = EFVLegend(L);
				Legends.Add(Forced);
				Tags.Add(Tag + TEXT("(F)"));
			}
		}
		for (int32 I = 0; I < Legends.Num(); ++I) { SB.Add(UFVSprites::BuildFish(Legends[I], false), Tags[I]); }
		SB.NewRow();
		for (int32 I = 0; I < Legends.Num(); ++I) { SB.Add(UFVSprites::BuildFish(Legends[I], true), TEXT("")); }
		SB.NewRow();
		for (int32 I = 0; I < Legends.Num(); ++I) { SB.Add(BuildTrophyPlaque(Legends[I]), Tags[I].Left(8)); }
		SB.NewRow();
		SB.Add(BuildBait(EFVBaitStyle::Ember), TEXT("EMBER"));
		SB.Add(BuildItemArt(EFVItemArt::FriedFish, FColor::White, 0), TEXT("FRIED"));
		SB.Add(BuildItemArt(EFVItemArt::FriedFishRare, FColor::White, 0), TEXT("F-RARE"));
		SB.Add(BuildItemArt(EFVItemArt::FriedFishEpic, FColor::White, 0), TEXT("F-EPIC"));
		SB.Add(BuildItemArt(EFVItemArt::FriedFishLegend, FColor::White, 0), TEXT("F-LEG"));
		SB.Add(FVArtItemsImpl::BuildFishTokenIcon(EFVRarity::Rare), TEXT("T-RARE"));
		SB.Add(FVArtItemsImpl::BuildFishTokenIcon(EFVRarity::Epic), TEXT("T-EPIC"));
		SB.Add(FVArtItemsImpl::BuildFishTokenIcon(EFVRarity::Legendary), TEXT("T-LEG"));
		SB.NewRow();
		static const TCHAR* BobberTags[] = { TEXT("TSAR"), TEXT("STURG"), TEXT("CRYST"), TEXT("LANT"), TEXT("FOG"), TEXT("ZHAR") };
		for (int32 B = int32(EFVBobberStyle::Tsar); B < int32(EFVBobberStyle::Count); ++B)
		{
			for (int32 Fr = 0; Fr < 2; ++Fr)
			{
				const FFVImage Bob = BuildBobber(EFVBobberStyle(B), Fr);
				FFVImage Big(Bob.W * 2, Bob.H * 2);
				for (int32 Y = 0; Y < Big.H; ++Y) { for (int32 X = 0; X < Big.W; ++X) { Big.Set(X, Y, Bob.Get(X / 2, Y / 2)); } }
				const int32 N = B - int32(EFVBobberStyle::Tsar);
				SB.Add(Big, Fr == 0 && N < int32(UE_ARRAY_COUNT(BobberTags)) ? FString(BobberTags[N]) : FString());
			}
		}
	}

	const FFVImage Sheet = SB.Render(2);
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Sheet.W, Sheet.H, TArrayView64<const FColor>(Sheet.Px.GetData(), Sheet.Px.Num()), Png);
	const FString Dir = FPaths::GetPath(FilePath);
	if (!Dir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Dir, true);
	}
	if (Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *FilePath))
	{
		UE_LOG(LogFV, Log, TEXT("FV: update-2 contact sheet written: %s (%dx%d)"), *FilePath, Sheet.W, Sheet.H);
	}
	else
	{
		UE_LOG(LogFV, Warning, TEXT("FV: failed to write update-2 contact sheet %s"), *FilePath);
	}
}
