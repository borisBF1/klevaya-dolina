// UPDATE 6 [ART-DECOR] — yard decor (roof, walls, shutters, fences, path, lanterns, mailbox, bench, flowerbed, scarecrow, well, vane, fountain / statue) and expensive furniture (Docs/UPDATE6_FOREST_CITY.md §4, §6.4, §8, Р20, Р37). Owner: ART-DECOR.
// Contract: Public/FVSprites.h «UPDATE 6», Private/FVArtU6.h (FVArtDecorU6, called by BuildDecorIcon / BuildDecorWorld for slots >= FVDecor::ClassicSlots).
// Style: pixel art like FVSceneArt.cpp / FVArtHome.cpp (Stardew-like shading via FVArt::Shade/Light, 1px dark outline
// on props), deterministic (hash noise with constant seeds, no global state). Caching is done by the callers
// (UFVSprites::DecorIcon / DecorWorld, category FVSpriteCat::Decor); the builders here are pure CPU.
//
// ==================================================================================================================
// STYLES (FFVDecorDef::Style, taken modulo the style count of the slot) — DATA-CONTENT uses these numbers.
// Primary / Secondary: FColor::White + FColor::Black (the struct defaults) = the style's own colours (DefaultColors
// below); any other pair recolours the main (P) / accent (S) parts.
// ------------------------------------------------------------------------------------------------------------------
//  YardRoof      0 red tiles (base) · 1 green tin · 2 grey shingle (дранка) · 3 blue fish-scale tiles     P roof, S ridge
//  YardWalls     0 logs (base) · 1 whitewash + timber · 2 blue boards · 3 stone                           P wall, S trim
//  YardShutters  0 none · 1 carved blue (white fretwork) · 2 green louvres · 3 red with hearts            P panel, S accent
//  YardFence     0 white picket · 1 wattle (плетень) · 2 wrought iron (black, gold spikes)                P main, S accent
//  YardPath      0 trodden (base) · 1 pebbles · 2 brick (basket weave) · 3 flagstones                     P main, S joints
//  YardLantern   0 cast-iron post · 1 lantern-house · 2 garland · 3 stone lantern (hard rock)             P body, S light
//  YardMailbox   0 blue · 1 red with a flag                                                               P box, S flag
//  YardBench     0 simple · 1 carved                                                                      P wood, S accent
//  YardFlowerbed 0 daisies · 1 petunia box · 2 alpine rock garden (hard rock)                             P petals, S 2nd
//  YardScarecrow 0 straw · 1 fisherman                                                                    P shirt, S hat
//  YardWell      0 with a roof (base) · 1 sweep well (журавль)                                            P roof, S trim
//  YardVane      0 rooster · 1 fish                                                                       P figure, S gilt
//  YardCenter    0 woodpile · 1 fountain · 2 fish statue · 3 golden fisherman                            P stone/bronze, S water/gold
//  Cupboard      0 painted buffet · 1 carved oak buffet (expensive)                                       P wood, S paint/brass
//  Bookcase      0 book shelf · 1 grand bookcase with a ladder                                            P wood, S brass
//  Clock         0 wall clock (ходики) · 1 grandfather clock                                              P wood, S brass
//  Fireplace     0 brick · 1 carved stone                                                                 P stone, S brass
//  Chandelier    0 wooden wheel with candles · 1 brass chandelier                                         P metal, S glass
//  Statue        0 stuffed Tsar Carp (wall) · 1 aquarium column                                           P wood/brass, S water
//  StoveSkin     0 blue-and-white tiled stove · 1 green majolica stove                                    P tiles, S door
//  BedSkin       0 canopy bed (bordeaux + gold) · 1 royal canopy bed with a crown                         P fabric, S fringe
//
// SIZE / PIVOT / FRAMES / LIGHT (texels; world sprites are drawn at FV::WorldScale = 2). Pivot = bottom-centre.
// ------------------------------------------------------------------------------------------------------------------
//  Icons: 16x16 (drawing in 1..14, ink outline), every slot and style.
//  YardRoof / YardWalls / YardShutters: OVERLAYS 78x62 = exactly the canvas of MakeCottage (FVSceneArt.cpp). Draw them
//     at the same feet as the cottage, in the order walls -> shutters -> roof. Windows, door, lantern and the attic window
//     stay where they are (walls keep them transparent), so the night window lights do not move. Walls always cover
//     the cottage's own green shutters: shutters style 0 «none» is an empty (transparent) overlay.
//  YardFence: section 36 x (20 | 18 | 24); posts at x 2..3 and x 32..33 (iron: pillars 1..4 / 31..34) -> repeat the
//     section every 30 texels, the posts overlap exactly.
//  YardPath: seamless 32x32 tile, pivot TOP-LEFT (tile it over the path mask, like Wallpaper / Floor).
//  (sizes of the other sprites: see the comment above each builder; light points: FVArtDecorU6::LightPoint)
//  Collision (suggested): the bottom 1/4 of the sprite, 2 texels narrower on each side.
//
// Hooks beyond the frozen contract (defined below, declared here; requested for Private/FVArtU6.h):
//   FVArtDecorU6::WorldFrames(Def)          -> 1..4 animation frames of the world sprite
//   FVArtDecorU6::BuildWorldFrame(Def, F)   -> frame F (BuildWorld == frame 0)
//   FVArtDecorU6::LightPoint(Def)           -> light source in texels from the top-left of the world sprite, (-1,-1) = none
// ==================================================================================================================

#include "FVArtU6.h"
#include "FVSprites.h"
#include "FVDatabase.h"

namespace FVArtDecorU6
{
	int32 WorldFrames(const FFVDecorDef& Def);
	FFVImage BuildWorldFrame(const FFVDecorDef& Def, int32 Frame);
	FVector2D LightPoint(const FFVDecorDef& Def);
}

namespace FVArtDecorU6Impl
{
	// ---------------------------------------------------------------------------------------- palette (as FVArtHome.cpp)
	const FColor PropOut(58, 36, 30);
	const FColor HouseOut(60, 36, 28);   // MakeCottage outline
	const FColor WoodHi(214, 166, 108), Wood(182, 132, 80), WoodMid(150, 104, 62), WoodLo(116, 78, 48), WoodDk(84, 54, 34);
	const FColor DWoodHi(170, 108, 64), DWood(136, 82, 48), DWoodLo(100, 58, 36), DWoodDk(70, 40, 28);
	const FColor StoneHi(204, 200, 190), Stone(164, 160, 152), StoneLo(124, 120, 116), StoneDk(86, 82, 88);
	const FColor BrickHi(214, 122, 86), Brick(178, 88, 62), BrickLo(130, 60, 48), Mortar(206, 190, 164);
	const FColor WashHi(252, 248, 238), Wash(234, 228, 214), WashLo(198, 190, 178);
	const FColor IronHi(150, 150, 166), Iron(98, 98, 114), IronLo(66, 64, 80), IronDk(44, 42, 56);
	const FColor BrassHi(255, 232, 136), Brass(224, 172, 62), BrassLo(160, 110, 36);
	const FColor FireY(255, 242, 150), FireO(255, 170, 50), FireR(228, 86, 32), Ember(150, 44, 30), FireBack(44, 26, 26);
	const FColor SoilHi(150, 108, 70), Soil(112, 76, 50), SoilLo(84, 56, 40), SoilDk(58, 38, 30);
	const FColor LeafHi(150, 214, 92), Leaf(84, 168, 64), LeafLo(44, 110, 60), LeafDk(30, 78, 50);
	const FColor BarkHi(150, 104, 64), Bark(116, 78, 48), BarkLo(84, 54, 34);
	const FColor StrawHi(252, 226, 140), Straw(236, 196, 96), StrawLo(190, 146, 62);
	const FColor Cream(250, 238, 204), CreamLo(222, 200, 156);
	// the cottage (MakeCottage) — the overlays must match it
	const FColor CotTrim(96, 60, 36), CotGlass(66, 92, 132), CotGlassHi(156, 196, 226);
	const FColor LogHi(204, 150, 94), Log(170, 116, 68), LogLo(128, 84, 48), LogSeam(98, 62, 36), LogEnd(222, 180, 122);
	// hard rock (mat_hardrock): grey-violet with violet veins
	const FColor RockHi(158, 150, 176), Rock(108, 102, 126), RockLo(76, 72, 94), RockVein(170, 110, 230), RockVeinHi(220, 180, 255);
	// water
	const FColor WaterHi(186, 236, 246), Water(96, 178, 214), WaterLo(54, 122, 176), WaterDk(34, 82, 134);
	// path (PaintPath in FVSceneArt.cpp) and yard grass
	const FColor PathLo(152, 114, 74), PathMid(180, 140, 94), PathHi(204, 166, 116);
	const FColor Grass(92, 160, 68), GrassLo(66, 128, 56), GrassHi(140, 204, 92);
	const FColor Gold(255, 204, 51), GoldHi(255, 240, 160), GoldLo(196, 136, 30);
	const FColor Bronze(186, 124, 62), BronzeHi(236, 184, 104), BronzeLo(126, 78, 44), BronzeDk(84, 50, 34);

	FORCEINLINE FColor Rgb(uint8 R, uint8 G, uint8 B, uint8 A = 255) { return FColor(R, G, B, A); }
	FORCEINLINE FColor Lt(FColor C, float A) { return FVArt::Light(C, A); }
	FORCEINLINE FColor Dk(FColor C, float A) { return FVArt::Shade(C, A); }
	FORCEINLINE FColor MixC(FColor A, FColor B, float T) { return FVArt::Mix(A, B, T); }
	FORCEINLINE float N01(int32 X, int32 Y, uint32 Seed) { return FVArt::Noise(X, Y, Seed); }

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

	/** Filled box with a bevel: lit top row / left column, shaded bottom row / right column. */
	void Box(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, float LtA = 0.3f, float DkA = 0.35f)
	{
		if (W <= 0 || H <= 0) { return; }
		Rect(I, X, Y, W, H, Base);
		HLine(I, X, X + W - 1, Y, Lt(Base, LtA));
		VLine(I, X, Y, Y + H - 1, Lt(Base, LtA * 0.5f));
		HLine(I, X, X + W - 1, Y + H - 1, Dk(Base, DkA));
		VLine(I, X + W - 1, Y + 1, Y + H - 1, Dk(Base, DkA * 0.7f));
	}

	/** Recessed panel: dark top / left edge, lit bottom / right edge (a door panel, a drawer front). */
	void Inset(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base)
	{
		if (W <= 0 || H <= 0) { return; }
		Rect(I, X, Y, W, H, Base);
		HLine(I, X, X + W - 1, Y, Dk(Base, 0.35f));
		VLine(I, X, Y, Y + H - 1, Dk(Base, 0.3f));
		HLine(I, X + 1, X + W - 1, Y + H - 1, Lt(Base, 0.25f));
		VLine(I, X + W - 1, Y + 1, Y + H - 1, Lt(Base, 0.18f));
	}

	/** Wooden planks (horizontal bands of height PH, or vertical boards of width PH) with seams and grain. */
	void Planks(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base, int32 PH, uint32 Seed, bool bVertical = false)
	{
		PH = FMath::Max(2, PH);
		const FColor Hi = Lt(Base, 0.18f), Seam = Dk(Base, 0.42f), Grain = Dk(Base, 0.14f);
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
				else if (N01(bVertical ? Band : Along / 3, bVertical ? Along / 3 : Band, Seed) < 0.12f) { C = Grain; }
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
						const float SX = (CX + 0.2f + 0.6f * N01(CX, CY, Seed)) * CW;
						const float SY = (CY + 0.2f + 0.6f * N01(CX, CY, Seed + 17u)) * CH;
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
				const float N = N01(BestX, BestY, Seed + 5u);
				FColor C = N < 0.3f ? Dk(Base, 0.12f) : (N > 0.75f ? Lt(Base, 0.1f) : Base);
				if (Edge < 2.0f)
				{
					C = (PX < BX || PY < BY) && (PY <= BY + 0.5f) ? Lt(C, 0.28f) : Dk(C, 0.22f);
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
		const FColor LtC = Lt(Base, 0.3f), DkC = Dk(Base, 0.35f);
		for (int32 YY = FMath::FloorToInt(CY - RY) - 1; YY <= FMath::CeilToInt(CY + RY) + 1; ++YY)
		{
			for (int32 XX = FMath::FloorToInt(CX - RX) - 1; XX <= FMath::CeilToInt(CX + RX) + 1; ++XX)
			{
				const float DX = (XX + 0.5f - CX) / FMath::Max(RX, 0.1f), DY = (YY + 0.5f - CY) / FMath::Max(RY, 0.1f);
				const float D = DX * DX + DY * DY;
				if (D > 1.f) { continue; }
				const float S = DX + DY;
				I.Set(XX, YY, S < -0.7f ? LtC : ((S > 0.55f || (D > 0.8f && S > 0.f)) ? DkC : Base));
			}
		}
		if (bSpec)
		{
			I.Set(FMath::FloorToInt(CX - RX * 0.45f), FMath::FloorToInt(CY - RY * 0.5f), Lt(Base, 0.8f));
		}
	}

	/** Round thick stroke from (X0,Y0) to (X1,Y1) (a pole, a sweep, a fish body segment). */
	void Thick(FFVImage& I, float X0, float Y0, float X1, float Y1, float R, FColor C)
	{
		const float L = FMath::Sqrt((X1 - X0) * (X1 - X0) + (Y1 - Y0) * (Y1 - Y0));
		const int32 N = FMath::Max(1, FMath::CeilToInt(L * 2.f));
		for (int32 K = 0; K <= N; ++K)
		{
			const float T = float(K) / float(N);
			Ellipse(I, X0 + (X1 - X0) * T, Y0 + (Y1 - Y0) * T, R, R, C);
		}
	}

	/** Tints already painted pixels towards a light color around a point (fire / lamp light baked into the art). */
	void Warm(FFVImage& I, float CX, float CY, float R, FColor LightC, float Strength)
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
				P = MixC(P, LightC, Strength * (1.f - D / R));
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
			const float T = float(BaseY - YY) / float(FMath::Max(1, Height));
			const float Wob = 0.35f * (N01(YY, 3, Seed) - 0.5f);
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

	/** 1-texel candle of height 3 with a flame on top (paint after the outline). */
	void Candle(FFVImage& I, int32 X, int32 TopY, FColor Wax)
	{
		VLine(I, X, TopY, TopY + 2, Wax);
		I.Set(X, TopY, Lt(Wax, 0.4f));
		I.Set(X, TopY - 1, FireO);
		I.Set(X, TopY - 2, FireY);
	}

	/** Periodic value noise (the lattice wraps every Period cells) — for seamless tiles. */
	float PNoise(float X, float Y, int32 Period, uint32 Seed)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = X - float(X0), FY = Y - float(Y0);
		const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY);
		auto Wrap = [Period](int32 V) { return ((V % Period) + Period) % Period; };
		const float A = N01(Wrap(X0), Wrap(Y0), Seed), B = N01(Wrap(X0 + 1), Wrap(Y0), Seed);
		const float C = N01(Wrap(X0), Wrap(Y0 + 1), Seed), D = N01(Wrap(X0 + 1), Wrap(Y0 + 1), Seed);
		const float Top = A + (B - A) * SX, Bot = C + (D - C) * SX;
		return Top + (Bot - Top) * SY;
	}

	/** Periodic jittered cells (Voronoi) on a Size x Size tile with N x N cells. Edge = distance gap to the 2nd site. */
	struct FCellHit { int32 Id = 0; float Edge = 0.f; float DX = 0.f; float DY = 0.f; };
	FCellHit PCells(int32 X, int32 Y, int32 Size, int32 N, uint32 Seed, float Jit)
	{
		const float CS = float(Size) / float(N);
		const float PX = X + 0.5f, PY = Y + 0.5f;
		const int32 GX = FMath::FloorToInt(PX / CS), GY = FMath::FloorToInt(PY / CS);
		float D1 = 1e9f, D2 = 1e9f;
		FCellHit Hit;
		for (int32 OY = -1; OY <= 1; ++OY)
		{
			for (int32 OX = -1; OX <= 1; ++OX)
			{
				const int32 CX = GX + OX, CY = GY + OY;
				const int32 WX = ((CX % N) + N) % N, WY = ((CY % N) + N) % N;
				const float SX = (CX + 0.5f + Jit * (N01(WX, WY, Seed) - 0.5f)) * CS;
				const float SY = (CY + 0.5f + Jit * (N01(WX, WY, Seed + 17u) - 0.5f)) * CS;
				const float DX = PX - SX, DY = PY - SY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if (D < D1) { D2 = D1; D1 = D; Hit.Id = WY * N + WX; Hit.DX = DX; Hit.DY = DY; }
				else if (D < D2) { D2 = D; }
			}
		}
		Hit.Edge = D2 - D1;
		return Hit;
	}

	// ---------------------------------------------------------------------------------------- styles and colours
	int32 StyleCount(EFVDecorSlot Slot)
	{
		switch (Slot)
		{
		case EFVDecorSlot::YardRoof:
		case EFVDecorSlot::YardWalls:
		case EFVDecorSlot::YardShutters:
		case EFVDecorSlot::YardPath:
		case EFVDecorSlot::YardLantern:
		case EFVDecorSlot::YardCenter:    return 4;   // 0 woodpile · 1 fountain · 2 fish statue · 3 golden fisherman (DATA-CONTENT order)
		case EFVDecorSlot::YardFence:
		case EFVDecorSlot::YardFlowerbed: return 3;
		default:                          return 2;
		}
	}

	int32 StyleOf(const FFVDecorDef& Def) { return int32(Def.Style) % FMath::Max(1, StyleCount(Def.Slot)); }

	/** Default colours of a style (used when the def keeps the struct defaults White / Black). St is in range. */
	void DefaultColors(EFVDecorSlot Slot, int32 St, FColor& P, FColor& S)
	{
		P = Wood; S = Brass;
		switch (Slot)
		{
		case EFVDecorSlot::YardRoof:
		{
			static const FColor D[4][2] = { { FColor(178, 70, 52), FColor(100, 38, 36) }, { FColor(78, 150, 96), FColor(206, 214, 204) },
				{ FColor(140, 132, 124), FColor(96, 86, 82) }, { FColor(62, 110, 176), FColor(40, 60, 110) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::YardWalls:
		{
			static const FColor D[4][2] = { { FColor(170, 116, 68), FColor(96, 60, 36) }, { FColor(234, 228, 214), FColor(92, 58, 38) },
				{ FColor(120, 170, 204), FColor(244, 240, 228) }, { FColor(160, 154, 146), FColor(116, 76, 46) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::YardShutters:
		{
			static const FColor D[4][2] = { { FColor(84, 140, 90), FColor(240, 236, 222) }, { FColor(60, 120, 196), FColor(246, 244, 236) },
				{ FColor(76, 150, 88), FColor(226, 232, 206) }, { FColor(196, 58, 54), FColor(250, 222, 196) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::YardFence:
		{
			static const FColor D[3][2] = { { FColor(240, 238, 228), FColor(200, 70, 60) }, { FColor(164, 118, 70), FColor(116, 78, 48) },
				{ FColor(54, 52, 64), FColor(255, 204, 51) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::YardPath:
		{
			static const FColor D[4][2] = { { FColor(180, 140, 94), FColor(152, 114, 74) }, { FColor(170, 164, 154), FColor(214, 190, 150) },
				{ FColor(178, 88, 62), FColor(176, 162, 140) }, { FColor(182, 176, 164), FColor(84, 128, 62) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::YardLantern:
		{
			static const FColor D[4][2] = { { FColor(58, 56, 70), FColor(255, 214, 120) }, { FColor(178, 70, 52), FColor(255, 206, 110) },
				{ FColor(230, 60, 60), FColor(255, 214, 80) }, { FColor(108, 102, 126), FColor(255, 196, 110) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::YardMailbox:
			P = St == 0 ? Rgb(52, 110, 196) : Rgb(206, 52, 48);
			S = St == 0 ? Rgb(250, 246, 236) : Rgb(255, 214, 60);
			break;
		case EFVDecorSlot::YardBench:
			P = St == 0 ? Wood : Rgb(126, 76, 44);
			S = St == 0 ? Iron : Rgb(70, 120, 90);
			break;
		case EFVDecorSlot::YardFlowerbed:
		{
			static const FColor D[3][2] = { { FColor(252, 252, 246), FColor(255, 206, 60) }, { FColor(190, 70, 190), FColor(250, 120, 170) },
				{ FColor(250, 236, 120), FColor(170, 120, 240) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::YardScarecrow:
			P = St == 0 ? Rgb(200, 70, 60) : Rgb(60, 96, 170);
			S = St == 0 ? Straw : Rgb(250, 204, 50);
			break;
		case EFVDecorSlot::YardWell:
			P = St == 0 ? Rgb(174, 74, 54) : Bark;
			S = St == 0 ? DWood : Iron;
			break;
		case EFVDecorSlot::YardVane:
			P = St == 0 ? Rgb(52, 50, 62) : Rgb(196, 112, 60);
			S = Gold;
			break;
		case EFVDecorSlot::YardCenter:
		{
			static const FColor D[4][2] = { { FColor(170, 120, 72), FColor(222, 190, 140) }, { FColor(196, 190, 178), FColor(96, 178, 214) },
				{ FColor(150, 170, 180), FColor(90, 110, 120) }, { FColor(250, 204, 62), FColor(160, 110, 40) } };
			P = D[St][0]; S = D[St][1];
			break;
		}
		case EFVDecorSlot::Cupboard:
			P = St == 0 ? Rgb(98, 146, 150) : Rgb(112, 66, 40);
			S = St == 0 ? Rgb(214, 70, 60) : Brass;
			break;
		case EFVDecorSlot::Bookcase:
			P = St == 0 ? Wood : Rgb(104, 60, 38);
			S = Brass;
			break;
		case EFVDecorSlot::Clock:
			P = St == 0 ? DWood : Rgb(110, 62, 40);
			S = Brass;
			break;
		case EFVDecorSlot::Fireplace:
			P = St == 0 ? Brick : Rgb(214, 206, 190);
			S = Brass;
			break;
		case EFVDecorSlot::Chandelier:
			P = St == 0 ? DWood : Brass;
			S = St == 0 ? Cream : Rgb(200, 236, 255);
			break;
		case EFVDecorSlot::Statue:
			P = St == 0 ? Rgb(120, 74, 44) : Brass;
			S = Water;
			break;
		case EFVDecorSlot::StoveSkin:
			P = St == 0 ? Rgb(52, 96, 176) : Rgb(70, 140, 96);
			S = St == 0 ? Rgb(210, 120, 70) : Brass;
			break;
		case EFVDecorSlot::BedSkin:
			P = St == 0 ? Rgb(150, 34, 52) : Rgb(46, 70, 150);
			S = St == 0 ? Gold : Rgb(226, 230, 240);
			break;
		default:
			break;
		}
	}

	void Colors(const FFVDecorDef& Def, FColor& P, FColor& S)
	{
		DefaultColors(Def.Slot, StyleOf(Def), P, S);
		if (!(Def.Primary == FColor::White && Def.Secondary == FColor::Black))
		{
			P = Def.Primary;
			S = Def.Secondary;
			P.A = 255;
			S.A = 255;
		}
	}

	// ================================================================================================================
	// Cottage overlays 78x62 (the canvas of MakeCottage in FVSceneArt.cpp, drawn at the same feet)
	// ================================================================================================================
	constexpr int32 CotW = 78, CotH = 62;
	constexpr int32 CotDoorX = 34, CotDoorY = 36, CotDoorW = 10, CotDoorH = 21;
	constexpr int32 CotWinY = 38;
	const int32 CotWinX[2] = { 12, 54 };

	FColor Mul(FColor C, float F)
	{
		return FColor(uint8(FMath::Clamp(FMath::RoundToInt(C.R * F), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(C.G * F), 0, 255)),
			uint8(FMath::Clamp(FMath::RoundToInt(C.B * F), 0, 255)), C.A);
	}

	int32 RoofX0(int32 Y) { return FMath::RoundToInt(14.f - 13.f * (float(Y - 4) / 24.f)); }
	int32 RoofX1(int32 Y) { return FMath::RoundToInt(63.f + 13.f * (float(Y - 4) / 24.f)); }

	/** One roof texel of a style (X, Y in cottage coordinates; X0..X1 = the row span). */
	FColor RoofTexel(int32 St, FColor P, FColor S, int32 X, int32 Y, int32 X0, int32 X1)
	{
		const int32 V = FMath::Max(0, Y - 4);
		const FColor Hi = Lt(P, 0.3f), Lo = Dk(P, 0.25f), Deep = Dk(P, 0.5f);
		FColor C = P;
		switch (St)
		{
		case 0:
		{
			// clay pantiles: vertical waves, courses of 4 with a shadow under each course
			const int32 Cc = X % 5, In = V % 4;
			static const float Wave[5] = { -0.3f, 0.f, 0.32f, 0.08f, -0.14f };
			C = Wave[Cc] < 0.f ? Dk(P, -Wave[Cc]) : Lt(P, Wave[Cc]);
			if (In == 0) { C = Dk(C, 0.28f); }
			const float Nn = N01(X / 5, V / 4, 311u);
			if (Nn < 0.12f) { C = Dk(C, 0.1f); }
			else if (Nn > 0.9f) { C = Lt(C, 0.08f); }
			break;
		}
		case 1:
		{
			// green tin: standing seams fanning out from the ridge, laps every 8 rows, a few glints
			const float Span = float(X1 - X0 + 1);
			const int32 Pn = FMath::FloorToInt((X - X0 + 0.5f) * 13.f / Span);
			const int32 PnNext = FMath::FloorToInt((X + 1 - X0 + 0.5f) * 13.f / Span);
			const int32 PnPrev = FMath::FloorToInt((X - 1 - X0 + 0.5f) * 13.f / Span);
			C = MixC(Lt(P, 0.12f), P, float(V) / 24.f);
			if (PnNext != Pn) { C = Lt(P, 0.42f); }
			else if (PnPrev != Pn) { C = Dk(P, 0.3f); }
			else if (V % 8 == 7) { C = Dk(C, 0.12f); }
			else if (N01(X, Y, 313u) < 0.025f) { C = Lt(P, 0.55f); }
			break;
		}
		case 2:
		{
			// grey wooden shingle: courses of 3, staggered boards 4 wide with a tint each, moss here and there
			const int32 Row = V / 3, In = V % 3;
			const int32 Ux = X + (Row % 2) * 2 + (Row % 3);
			const int32 Board = Ux / 4, Bx = Ux % 4;
			const float Nn = N01(Board, Row, 317u);
			C = Nn < 0.33f ? Dk(P, 0.14f) : (Nn > 0.72f ? Lt(P, 0.12f) : P);
			if (In == 0) { C = Lt(C, 0.14f); }
			if (In == 2) { C = Dk(C, 0.3f); }
			if (Bx == 0 && In != 0) { C = Dk(P, 0.38f); }
			if (In == 1 && N01(X, Y, 319u) < 0.06f) { C = MixC(C, Rgb(104, 132, 72), 0.55f); }
			break;
		}
		default:
		{
			// glazed fish-scale tiles: rows of 4, scales 6 wide staggered by 3, rounded bottoms
			const int32 Row = V / 4, In = V % 4;
			const int32 Cc = (X + (Row % 2) * 3) % 6;
			const bool bEdge = Cc == 0 || Cc == 5;
			if (In == 0) { C = Cc == 0 ? Deep : Lo; }
			else if (In == 1) { C = Cc == 0 ? Lo : (Cc == 2 ? Lt(P, 0.5f) : Hi); }
			else if (In == 2) { C = Cc == 0 ? Lo : P; }
			else { C = bEdge ? Deep : Dk(P, 0.12f); }
			break;
		}
		}
		// the lit left edge and the shaded right edge of the roof (as MakeCottage)
		if (X <= X0 + 1) { C = MixC(C, Hi, 0.5f); }
		else if (X >= X1 - 1) { C = Deep; }
		(void)S;
		return C;
	}

	/** Roof overlay: the whole roof trapezoid, ridge, eave, attic window, chimney and the porch roof. */
	FFVImage RoofOverlay(int32 St, FColor P, FColor S)
	{
		FFVImage Img(CotW, CotH);
		for (int32 Y = 4; Y <= 28; ++Y)
		{
			const int32 X0 = RoofX0(Y), X1 = RoofX1(Y);
			for (int32 X = X0; X <= X1; ++X) { Img.Set(X, Y, RoofTexel(St, P, S, X, Y, X0, X1)); }
		}
		// ridge and eave (tin: galvanised ridge cap and gutter)
		if (St == 1)
		{
			HLine(Img, 14, 63, 3, Dk(S, 0.25f));
			HLine(Img, 15, 62, 4, S);
			HLine(Img, 2, 75, 27, Lt(S, 0.2f));
			HLine(Img, 1, 76, 28, Dk(S, 0.4f));
		}
		else
		{
			HLine(Img, 14, 63, 3, S);
			if (St == 0 || St == 3) { for (int32 X = 15; X <= 62; X += 3) { Img.Set(X, 4, Lt(S, 0.3f)); } }
			HLine(Img, 1, 76, 28, Dk(P, 0.5f));
		}
		// round attic window (same place as MakeCottage)
		Ellipse(Img, 39.f, 15.f, 3.4f, 3.4f, CotTrim);
		Ellipse(Img, 39.f, 15.f, 2.4f, 2.4f, CotGlass);
		Img.Set(38, 14, CotGlassHi);
		// chimney (as MakeCottage)
		for (int32 Y = 0; Y <= 12; ++Y)
		{
			for (int32 X = 56; X <= 62; ++X)
			{
				FColor Cc = X == 56 ? Rgb(188, 184, 174) : (X == 62 ? Rgb(112, 106, 104) : Rgb(150, 146, 140));
				if (Y <= 1) { Cc = Rgb(70, 64, 64); }
				else if ((Y + (X & 1)) % 3 == 0) { Cc = Rgb(112, 106, 104); }
				Img.Set(X, Y, Cc);
			}
		}
		if (St == 1) { HLine(Img, 55, 63, 12, Lt(S, 0.1f)); }
		// porch roof over the door
		for (int32 Y = 31; Y <= 34; ++Y)
		{
			const int32 X0 = CotDoorX - 4 - (Y - 31), X1 = CotDoorX + CotDoorW + 3 + (Y - 31);
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor Cc = RoofTexel(St, P, S, X, Y - 20, X0, X1);
				if (Y == 31) { Cc = St == 1 ? Dk(S, 0.1f) : S; }
				else if (Y == 34) { Cc = St == 1 ? Dk(S, 0.4f) : Dk(P, 0.5f); }
				Img.Set(X, Y, Cc);
			}
		}
		Img.Outline(HouseOut);
		return Img;
	}

	bool CotWallHole(int32 X, int32 Y)
	{
		for (int32 W = 0; W < 2; ++W)
		{
			const int32 X0 = CotWinX[W];
			if (X >= X0 && X <= X0 + 11 && Y >= CotWinY && Y <= CotWinY + 10) { return true; }        // window
			if (X >= X0 - 1 && X <= X0 + 12 && Y >= CotWinY + 11 && Y <= CotWinY + 13) { return true; }   // flower box
		}
		if (X >= CotDoorX && X < CotDoorX + CotDoorW && Y >= CotDoorY && Y < CotDoorY + CotDoorH) { return true; }
		if (Y >= 31 && Y <= 34 && X >= CotDoorX - 4 - (Y - 31) && X <= CotDoorX + CotDoorW + 3 + (Y - 31)) { return true; }   // porch
		if (X >= 46 && X <= 48 && Y >= 36 && Y <= 39) { return true; }   // door lantern
		return false;
	}

	/** Walls overlay: the log wall area (x 3..74, y 29..55) in a new material; windows, door, porch and lantern stay
	    transparent. Always covers the cottage's green shutters. */
	FFVImage WallsOverlay(int32 St, FColor P, FColor S)
	{
		FFVImage Img(CotW, CotH);
		const bool bBaseLogs = St == 0 && P == Rgb(170, 116, 68);
		const FColor LHi = bBaseLogs ? LogHi : Lt(P, 0.3f), LMid = P, LLo = bBaseLogs ? LogLo : Dk(P, 0.25f);
		const FColor LSeam = bBaseLogs ? LogSeam : Dk(P, 0.48f), LEnd = bBaseLogs ? LogEnd : Lt(P, 0.5f);
		if (St == 3)
		{
			Stones(Img, 3, 31, 72, 25, P, Dk(P, 0.45f), 5.5f, 331u);
		}
		for (int32 Y = 29; Y <= 55; ++Y)
		{
			for (int32 X = 3; X <= 74; ++X)
			{
				FColor C = P;
				switch (St)
				{
				case 0:
				{
					const int32 R3 = (Y - 29) % 3;
					if (X <= 4 || X >= 73)
					{
						if (R3 == 2) { continue; }   // between the round log ends: the cottage outline shows through
						C = X == 3 ? (R3 == 0 ? LEnd : Mul(LEnd, 0.85f)) : (X == 4 ? LEnd : (X == 73 ? Mul(LEnd, 0.78f) : Mul(LEnd, 0.66f)));
						break;
					}
					C = R3 == 0 ? LHi : (R3 == 1 ? LMid : LSeam);
					if (R3 == 1 && N01(X / 3, Y, 41u) < 0.12f) { C = LLo; }
					if (Y == 29) { C = S; }
					else if (Y == 30) { C = LSeam; }
					if (X >= 69) { C = Mul(C, 0.82f); }
					break;
				}
				case 1:
				{
					// whitewash with dark timber framing
					const float Nn = N01(X, Y, 337u);
					C = Nn < 0.1f ? Dk(P, 0.08f) : (Nn > 0.94f ? Lt(P, 0.2f) : P);
					if (Y <= 32 && Y >= 31) { C = Dk(C, 0.12f); }
					const bool bPost = X <= 4 || X >= 73 || X == 28 || X == 29 || X == 48 || X == 49;
					auto OnBrace = [X, Y](int32 AX, int32 AY, int32 BX, int32 BY)
					{
						if (Y < FMath::Min(AY, BY) || Y > FMath::Max(AY, BY)) { return false; }
						const int32 Want = AX + (BX - AX) * (Y - AY) / (BY - AY);
						return X == Want || X == Want + 1;
					};
					const bool bBrace = OnBrace(5, 37, 11, 31) || OnBrace(21, 31, 27, 37) || OnBrace(50, 37, 56, 31) || OnBrace(66, 31, 72, 37);
					if (Y == 29) { C = Dk(S, 0.2f); }
					else if (Y == 30) { C = S; }
					else if (Y == 54) { C = Lt(S, 0.15f); }
					else if (Y == 55) { C = Dk(S, 0.25f); }
					else if (bPost) { C = (X == 3 || X == 28 || X == 48) ? Lt(S, 0.15f) : (X == 74 ? Dk(S, 0.3f) : S); }
					else if (bBrace) { C = S; }
					if (X >= 69 && !bPost) { C = Dk(C, 0.1f); }
					break;
				}
				case 2:
				{
					// blue vertical boards with white corner boards and trims
					const int32 Cc = (X - 5) % 4;
					C = Cc == 0 ? Lt(P, 0.2f) : (Cc == 3 ? Dk(P, 0.35f) : P);
					if (Cc != 0 && Cc != 3 && N01((X - 5) / 4, Y / 3, 339u) < 0.1f) { C = Dk(P, 0.1f); }
					if (Y <= 32 && Y >= 31) { C = Dk(C, 0.14f); }
					if (X <= 4) { C = X == 3 ? S : Dk(S, 0.12f); }
					else if (X >= 73) { C = X == 73 ? Dk(S, 0.15f) : Dk(S, 0.32f); }
					if (Y == 29) { C = Dk(S, 0.35f); }
					else if (Y == 30) { C = S; }
					else if (Y == 54) { C = S; }
					else if (Y == 55) { C = Dk(S, 0.3f); }
					if (X >= 69 && X < 73) { C = Dk(C, 0.12f); }
					break;
				}
				default:
				{
					// fieldstone with dressed quoins and a wooden top beam
					C = Img.Get(X, Y);
					if (Y == 29) { C = Dk(S, 0.2f); }
					else if (Y == 30) { C = S; }
					else
					{
						const int32 Row = (Y - 31) / 4, In = (Y - 31) % 4;
						const int32 QW = (Row % 2) ? 3 : 5;
						const bool bLeft = X - 3 < QW, bRight = 74 - X < QW;
						if (bLeft || bRight)
						{
							const FColor Q = Lt(P, 0.16f);
							C = In == 3 ? Dk(P, 0.45f) : (In == 0 ? Lt(Q, 0.25f) : Q);
							if (In != 3 && ((bLeft && X - 3 == QW - 1) || (bRight && 74 - X == QW - 1))) { C = Dk(Q, 0.3f); }
							if (bRight && In != 3) { C = Dk(C, 0.14f); }
						}
						if (Y <= 32) { C = Dk(C, 0.12f); }
					}
					if (X >= 69 && X <= 71) { C = Dk(C, 0.08f); }
					break;
				}
				}
				Img.Set(X, Y, C);
			}
		}
		// punch the holes (windows, flower boxes, door, porch, lantern)
		for (int32 Y = 29; Y <= 55; ++Y)
		{
			for (int32 X = 3; X <= 74; ++X)
			{
				if (CotWallHole(X, Y)) { Img.Set(X, Y, FColor(0, 0, 0, 0)); }
			}
		}
		return Img;
	}

	/** One shutter 5x11 (left one; the right one is mirrored). */
	FFVImage ShutterImg(int32 St, FColor P, FColor S)
	{
		const FColor H = Lt(P, 0.25f), D = Dk(P, 0.38f), T = Lt(P, 0.32f), B = Dk(P, 0.45f);
		const FColor L = Lt(P, 0.15f), M = Dk(P, 0.22f), K = Dk(P, 0.6f), Sm = Dk(P, 0.12f);
		if (St == 1)
		{
			return Ascii({
				TEXT("SSSSS"),
				TEXT("hSPPd"),
				TEXT("hPSPd"),
				TEXT("hPPSd"),
				TEXT("hPSPd"),
				TEXT("hSkSd"),
				TEXT("hPSPd"),
				TEXT("hPPSd"),
				TEXT("hPSPd"),
				TEXT("hSPPd"),
				TEXT("SSSSS") },
				{ { 'S', S }, { 'h', H }, { 'd', D }, { 'P', P }, { 'k', K } });
		}
		if (St == 2)
		{
			return Ascii({
				TEXT("htttd"),
				TEXT("hlllk"),
				TEXT("hmmmd"),
				TEXT("hlllk"),
				TEXT("hmmmd"),
				TEXT("hPPPd"),
				TEXT("hlllk"),
				TEXT("hmmmd"),
				TEXT("hlllk"),
				TEXT("hmmmd"),
				TEXT("bbbbb") },
				{ { 't', T }, { 'h', H }, { 'd', D }, { 'l', L }, { 'm', M }, { 'k', Dk(P, 0.45f) }, { 'P', P }, { 'b', B } });
		}
		return Ascii({
			TEXT("htttd"),
			TEXT("hlllk"),
			TEXT("hPsPd"),
			TEXT("hkPkd"),
			TEXT("hkkkd"),
			TEXT("hPkPd"),
			TEXT("hPsPd"),
			TEXT("hPsPd"),
			TEXT("hlllk"),
			TEXT("hPsPd"),
			TEXT("bbbbb") },
			{ { 't', T }, { 'h', H }, { 'd', D }, { 'l', L }, { 'P', P }, { 's', Sm }, { 'k', K }, { 'b', B } });
	}

	/** Shutters overlay: 5x11 shutters on both sides of both windows (+ a carved pediment for style 1). */
	FFVImage ShuttersOverlay(int32 St, FColor P, FColor S)
	{
		FFVImage Img(CotW, CotH);
		if (St == 0) { return Img; }   // «none»: the walls overlay already removed the cottage's own shutters
		const FFVImage L = ShutterImg(St, P, S);
		const FFVImage R = L.FlippedX();
		for (int32 W = 0; W < 2; ++W)
		{
			const int32 X0 = CotWinX[W];
			Img.Blit(L, X0 - 5, CotWinY);
			Img.Blit(R, X0 + 12, CotWinY);
			if (St == 1)
			{
				// white carved pediment (кокошник) over the window
				HLine(Img, X0 - 2, X0 + 13, 37, S);
				for (int32 X = X0; X <= X0 + 11; ++X) { Img.Set(X, 36, ((X - X0) % 3 == 1) ? P : S); }
				HLine(Img, X0 + 3, X0 + 8, 35, S);
				HLine(Img, X0 + 5, X0 + 6, 34, S);
				Img.Set(X0 + 5, 35, P); Img.Set(X0 + 6, 35, P);
				HLine(Img, X0 - 2, X0 + 13, 37, S);
				Img.Set(X0 - 2, 37, Dk(S, 0.3f)); Img.Set(X0 + 13, 37, Dk(S, 0.3f));
			}
		}
		return Img;
	}

	// ---------------------------------------------------------------------------------------- overlay icons
	FFVImage RoofIcon(int32 St, FColor P, FColor S)
	{
		FFVImage Img = Ascii({
			TEXT("................"),
			TEXT("..........cc...."),
			TEXT(".....rrrrrcc...."),
			TEXT("....rrrrrrrrr..."),
			TEXT("...rrrrrrrrrrr.."),
			TEXT("..rrrrrrrrrrrrr."),
			TEXT(".rrrrrrrrrrrrrr."),
			TEXT(".eeeeeeeeeeeeee."),
			TEXT("..wwwwwwwwwwww.."),
			TEXT("..wggwwddwwggw.."),
			TEXT("..wggwwddwwggw.."),
			TEXT("..wwwwwddwwwww.."),
			TEXT("..wwwwwddwwwww.."),
			TEXT("..ssssssssssss.."),
			TEXT("................"),
			TEXT("................") },
			{ { 'c', Stone }, { 'r', P }, { 'e', Dk(P, 0.5f) }, { 'w', Log }, { 'g', CotGlass }, { 'd', DWood }, { 's', Stone } });
		for (int32 Y = 2; Y <= 6; ++Y)
		{
			for (int32 X = 1; X <= 14; ++X)
			{
				if (Img.Get(X, Y) != P) { continue; }
				FColor C = P;
				switch (St)
				{
				case 0:  C = (X % 2 == 0) ? Lt(P, 0.25f) : (Y % 2 == 0 ? Dk(P, 0.25f) : P); break;
				case 1:  C = (X % 3 == 0) ? Lt(P, 0.4f) : ((X % 3 == 1) ? Dk(P, 0.2f) : P); break;
				case 2:  C = ((X + Y * 2) % 4 == 0) ? Dk(P, 0.35f) : (N01(X / 2, Y, 341u) < 0.4f ? Lt(P, 0.12f) : P); break;
				default: C = ((X + (Y % 2) * 2) % 4 == 0) ? Dk(P, 0.35f) : (Y % 2 == 0 ? Lt(P, 0.3f) : P); break;
				}
				if (Img.Get(X - 1, Y).A == 0) { C = Lt(P, 0.35f); }
				Img.Set(X, Y, C);
			}
		}
		HLine(Img, 6, 9, 2, S);
		HLine(Img, 3, 12, 9, LogHi); HLine(Img, 3, 12, 12, LogHi);
		Img.Set(3, 10, LogSeam); Img.Set(12, 11, LogSeam);
		Img.Set(3, 9, CotGlassHi);
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	FFVImage WallsIcon(int32 St, FColor P, FColor S)
	{
		FFVImage Img(16, 16);
		for (int32 Y = 3; Y <= 13; ++Y)
		{
			for (int32 X = 1; X <= 14; ++X)
			{
				FColor C = P;
				switch (St)
				{
				case 0:  C = ((Y - 3) % 3 == 0) ? Lt(P, 0.3f) : (((Y - 3) % 3 == 2) ? Dk(P, 0.45f) : P); break;
				case 1:
				{
					const bool bBeam = X == 1 || X == 14 || Y == 3 || Y == 12 || (Y > 9 && (X - 1 == 12 - Y || 14 - X == 12 - Y));
					C = bBeam ? S : (N01(X, Y, 344u) < 0.12f ? Dk(P, 0.08f) : P);
					break;
				}
				case 2:  C = ((X - 1) % 3 == 0) ? Lt(P, 0.2f) : (((X - 1) % 3 == 2) ? Dk(P, 0.35f) : P); break;
				default: C = P; break;
				}
				Img.Set(X, Y, C);
			}
		}
		if (St == 3) { Stones(Img, 1, 3, 14, 11, P, Dk(P, 0.45f), 4.f, 343u); }
		HLine(Img, 1, 14, 1, Rgb(178, 70, 52)); HLine(Img, 1, 14, 2, Rgb(100, 38, 36));
		// window with a cross
		Rect(Img, 5, 5, 6, 6, CotTrim);
		Rect(Img, 6, 6, 4, 4, CotGlass);
		Img.Set(6, 6, CotGlassHi); Img.Set(7, 6, CotGlassHi); Img.Set(6, 7, CotGlassHi);
		HLine(Img, 5, 10, 8, Rgb(210, 170, 120)); VLine(Img, 8, 5, 10, Rgb(210, 170, 120));
		HLine(Img, 1, 14, 14, Stone);
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	FFVImage ShuttersIcon(int32 St, FColor P, FColor S)
	{
		FFVImage Img(16, 16);
		Rect(Img, 5, 2, 6, 10, CotTrim);
		Rect(Img, 6, 3, 4, 8, CotGlass);
		HLine(Img, 6, 9, 7, Rgb(210, 170, 120)); VLine(Img, 8, 3, 10, Rgb(210, 170, 120));
		Img.Set(6, 3, CotGlassHi); Img.Set(6, 4, CotGlassHi); Img.Set(7, 3, CotGlassHi);
		// flower box
		for (int32 X = 4; X <= 11; ++X) { Img.Set(X, 12, (X % 2) ? Rgb(240, 90, 110) : Rgb(76, 140, 60)); }
		HLine(Img, 4, 11, 13, Rgb(136, 88, 52));
		if (St != 0)
		{
			const FFVImage L = ShutterImg(St, P, S);
			for (int32 Y = 0; Y < 10; ++Y)
			{
				for (int32 X = 0; X < 4; ++X)
				{
					// a 4x10 cut of the 5x11 shutter: frame columns kept, one inner column dropped
					const int32 SX = X == 0 ? 0 : X + 1;
					const int32 SY = Y < 5 ? Y : Y + 1;
					Img.Set(1 + X, 2 + Y, L.Get(SX, SY));
					Img.Set(14 - X, 2 + Y, L.Get(SX, SY));
				}
			}
		}
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	// ================================================================================================================
	// Fence sections (repeat every 30 texels) and path tiles (seamless 32x32)
	// ================================================================================================================
	/** White picket 36x20 · wattle 36x18 · wrought iron 36x24. */
	FFVImage FenceSection(int32 St, FColor P, FColor S)
	{
		if (St == 0)
		{
			FFVImage Img(36, 20);
			const FColor Hi = Lt(P, 0.25f), Lo = Dk(P, 0.2f), Dp = Dk(P, 0.38f);
			// rails
			for (int32 RY : { 7, 13 })
			{
				HLine(Img, 2, 33, RY, Hi);
				HLine(Img, 2, 33, RY + 1, Lo);
			}
			// pickets: 2 wide, pointed, 3-texel gaps (the outline keeps them apart)
			for (int32 X = 7; X <= 27; X += 5)
			{
				Img.Set(X, 3, Hi);
				Img.Set(X + 1, 3, Lo);
				VLine(Img, X, 4, 18, P);
				VLine(Img, X + 1, 4, 18, Lo);
				Img.Set(X, 4, Hi);
				Img.Set(X, 9, Dk(P, 0.1f)); Img.Set(X, 15, Dk(P, 0.1f));   // rail shadows
				Img.Set(X + 1, 18, Dp);
			}
			// posts with painted caps
			for (int32 PX : { 2, 32 })
			{
				VLine(Img, PX, 2, 19, P);
				VLine(Img, PX + 1, 2, 19, Dp);
				HLine(Img, PX - 1, PX + 2, 1, S);
				Img.Set(PX + 2, 1, Dk(S, 0.3f));
				Img.Set(PX - 1, 1, Lt(S, 0.3f));
			}
			Img.Outline(PropOut);
			return Img;
		}
		if (St == 1)
		{
			FFVImage Img(36, 18);
			auto WeaveAt = [&Img, P](int32 X, int32 Y)
			{
				const int32 Band = (Y - 4) / 2, In = (Y - 4) % 2;
				const bool bOver = ((X / 5) + Band) % 2 == 0;
				FColor C = bOver ? (In == 0 ? Lt(P, 0.25f) : P) : (In == 0 ? Dk(P, 0.15f) : Dk(P, 0.35f));
				if (N01(X, Y, 351u) < 0.08f) { C = Dk(C, 0.15f); }
				Img.Set(X, Y, C);
			};
			for (int32 Y = 4; Y <= 14; ++Y)
			{
				for (int32 X = 1; X <= 34; ++X) { WeaveAt(X, Y); }
			}
			// stakes (bark), withies pass in front of them on every other band
			for (int32 SX : { 2, 17, 32 })
			{
				VLine(Img, SX, 2, 17, BarkHi);
				VLine(Img, SX + 1, 2, 17, BarkLo);
				Img.Set(SX, 1, Bark);
				for (int32 Y = 4; Y <= 14; ++Y)
				{
					if (((Y - 4) / 2) % 2 == 1) { WeaveAt(SX, Y); WeaveAt(SX + 1, Y); }
				}
			}
			// twigs sticking out of the top
			for (int32 X = 5; X <= 30; X += 6)
			{
				Img.Set(X + (X % 4), 3, Dk(P, 0.1f));
				Img.Set(X + (X % 4) + 1, 2, Dk(P, 0.25f));
			}
			Img.Outline(PropOut);
			return Img;
		}
		// wrought iron between stone pillars on a low plinth
		FFVImage Img(36, 24);
		Box(Img, 1, 20, 34, 4, Stone, 0.35f, 0.3f);
		for (int32 X = 3; X <= 33; X += 6) { Img.Set(X, 21, StoneLo); Img.Set(X + 3, 22, StoneLo); }
		for (int32 PX : { 1, 31 })
		{
			Box(Img, PX, 5, 4, 16, Stone, 0.3f, 0.35f);
			HLine(Img, PX, PX + 3, 10, StoneLo); HLine(Img, PX, PX + 3, 15, StoneLo);
			Box(Img, PX, 3, 4, 2, StoneHi, 0.3f, 0.3f);
		}
		Img.Outline(PropOut);
		// gilded ball finials (after the outline so they read as metal)
		for (int32 PX : { 1, 31 })
		{
			Img.Set(PX + 1, 1, S); Img.Set(PX + 2, 1, Dk(S, 0.25f));
			Img.Set(PX + 1, 2, Lt(S, 0.45f)); Img.Set(PX + 2, 2, S);
			Img.Set(PX + 1, 0, Dk(S, 0.35f));
		}
		const FColor RailHi = Lt(P, 0.35f);
		for (int32 RY : { 6, 16 })
		{
			HLine(Img, 5, 30, RY, RailHi);
			HLine(Img, 5, 30, RY + 1, P);
		}
		for (int32 X = 7; X <= 27; X += 4)
		{
			VLine(Img, X, 4, 19, P);
			Img.Set(X, 8, RailHi);
			// gilded spear tip
			Img.Set(X, 2, Lt(S, 0.4f));
			Img.Set(X - 1, 3, S); Img.Set(X, 3, S); Img.Set(X + 1, 3, Dk(S, 0.3f));
			if (X < 27)
			{
				// iron ring with a gilded centre between the bars
				const int32 CX = X + 2;
				Img.Set(CX - 1, 10, P); Img.Set(CX, 10, P); Img.Set(CX + 1, 10, P);
				Img.Set(CX - 1, 11, P); Img.Set(CX + 1, 11, P);
				Img.Set(CX - 1, 12, P); Img.Set(CX, 12, P); Img.Set(CX + 1, 12, P);
				Img.Set(CX, 11, S);
			}
		}
		return Img;
	}

	FFVImage FenceIcon(int32 St, FColor P, FColor S)
	{
		FFVImage Img(16, 16);
		if (St == 0)
		{
			for (int32 X : { 2, 6, 10 })
			{
				VLine(Img, X, 4, 13, P); VLine(Img, X + 1, 4, 13, Dk(P, 0.2f));
				Img.Set(X, 3, Lt(P, 0.2f));
			}
			VLine(Img, 13, 3, 14, P); Img.Set(13, 2, S); Img.Set(14, 2, Dk(S, 0.3f)); VLine(Img, 14, 3, 14, Dk(P, 0.38f));
			HLine(Img, 1, 13, 6, Lt(P, 0.25f)); HLine(Img, 1, 13, 7, Dk(P, 0.2f));
			HLine(Img, 1, 13, 10, Lt(P, 0.25f)); HLine(Img, 1, 13, 11, Dk(P, 0.2f));
		}
		else if (St == 1)
		{
			for (int32 Y = 4; Y <= 12; ++Y)
			{
				for (int32 X = 1; X <= 14; ++X)
				{
					const int32 Band = (Y - 4) / 2, In = (Y - 4) % 2;
					const bool bOver = ((X / 3) + Band) % 2 == 0;
					Img.Set(X, Y, bOver ? (In == 0 ? Lt(P, 0.25f) : P) : (In == 0 ? Dk(P, 0.15f) : Dk(P, 0.35f)));
				}
			}
			for (int32 SX : { 2, 12 }) { VLine(Img, SX, 2, 14, BarkHi); VLine(Img, SX + 1, 2, 14, BarkLo); }
		}
		else
		{
			Box(Img, 1, 12, 14, 3, Stone, 0.35f, 0.3f);
			Box(Img, 1, 4, 3, 9, Stone, 0.3f, 0.35f);
			Box(Img, 12, 4, 3, 9, Stone, 0.3f, 0.35f);
			Img.Outline(FVArt::InkOutline());
			for (int32 X = 5; X <= 10; X += 2) { VLine(Img, X, 4, 11, P); Img.Set(X, 3, S); Img.Set(X, 2, Lt(S, 0.4f)); }
			HLine(Img, 4, 11, 5, Lt(P, 0.35f)); HLine(Img, 4, 11, 10, Lt(P, 0.35f));
			Img.Set(2, 2, S); Img.Set(13, 2, S); Img.Set(2, 3, Dk(S, 0.25f)); Img.Set(13, 3, Dk(S, 0.25f));
			return Img;
		}
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	/** Seamless 32x32 path tile. */
	FFVImage PathTile(int32 St, FColor P, FColor S)
	{
		constexpr int32 T = 32;
		FFVImage Img(T, T);
		for (int32 Y = 0; Y < T; ++Y)
		{
			for (int32 X = 0; X < T; ++X)
			{
				FColor C = P;
				switch (St)
				{
				case 0:
				{
					// trodden earth: soft 3-tone patches, grit, a few small stones
					const float N = PNoise(X / 8.f, Y / 8.f, 4, 361u) * 0.65f + PNoise(X / 4.f, Y / 4.f, 8, 362u) * 0.35f;
					C = N < 0.4f ? S : (N > 0.6f ? Lt(P, 0.2f) : P);
					const float G = N01(X, Y, 363u);
					if (G < 0.05f) { C = Dk(C, 0.2f); }
					else if (G > 0.975f) { C = Lt(C, 0.35f); }
					const FCellHit H = PCells(X, Y, T, 5, 364u, 0.8f);
					if (N01(H.Id, 0, 365u) < 0.4f && H.DX * H.DX + H.DY * H.DY < 1.6f)
					{
						C = (H.DX + H.DY < -0.3f) ? Rgb(196, 190, 180) : Rgb(150, 144, 136);
					}
					break;
				}
				case 1:
				{
					// pebbles in sand
					const FCellHit H = PCells(X, Y, T, 9, 371u, 0.7f);
					if (H.Edge < 0.9f) { C = Dk(S, 0.12f); break; }
					const float K = N01(H.Id, 1, 372u);
					const FColor Tone = K < 0.2f ? Lt(P, 0.25f) : (K < 0.4f ? Dk(P, 0.2f) : (K < 0.6f ? MixC(P, Rgb(176, 138, 104), 0.5f)
						: (K < 0.8f ? MixC(P, Rgb(136, 150, 176), 0.4f) : P)));
					C = Tone;
					if (H.DX + H.DY < -1.0f) { C = Lt(Tone, 0.28f); }
					else if (H.DX + H.DY > 0.9f) { C = Dk(Tone, 0.25f); }
					if (H.Edge < 1.5f && H.DY > 0.f) { C = Dk(C, 0.12f); }
					break;
				}
				case 2:
				{
					// brick basket weave: 8x8 blocks of two bricks, horizontal / vertical in turn
					const int32 BX = X / 8, BY = Y / 8, LX = X % 8, LY = Y % 8;
					const bool bHoriz = ((BX + BY) % 2) == 0;
					const int32 Along = bHoriz ? LX : LY, Across = bHoriz ? LY : LX;
					if (Along == 7 || Across % 4 == 3) { C = S; break; }
					const int32 Half = Across / 4;
					const float K = N01(BX * 2 + Half, BY, 381u);
					const FColor Base = K < 0.3f ? Dk(P, 0.12f) : (K > 0.75f ? Lt(P, 0.1f) : P);
					C = Base;
					if ((bHoriz && Across % 4 == 0) || (!bHoriz && Along == 0)) { C = Lt(Base, 0.18f); }
					else if ((bHoriz && Across % 4 == 2) || (!bHoriz && Along == 6)) { C = Dk(Base, 0.18f); }
					if (N01(X, Y, 382u) < 0.05f) { C = Dk(C, 0.12f); }
					break;
				}
				default:
				{
					// flagstones with moss in the joints
					const FCellHit H = PCells(X, Y, T, 3, 391u, 0.75f);
					if (H.Edge < 1.1f)
					{
						C = N01(X, Y, 392u) < 0.4f ? Dk(S, 0.25f) : S;
						if (N01(X, Y, 393u) > 0.85f) { C = Lt(S, 0.3f); }
						break;
					}
					const float K = N01(H.Id, 2, 394u);
					const FColor Base = K < 0.25f ? Dk(P, 0.1f) : (K < 0.5f ? MixC(P, Rgb(196, 170, 140), 0.35f) : (K > 0.8f ? Lt(P, 0.1f) : P));
					C = Base;
					if (H.Edge < 2.1f) { C = (H.DX + H.DY < 0.f) ? Lt(Base, 0.22f) : Dk(Base, 0.22f); }
					const float G = N01(X, Y, 395u);
					if (G < 0.06f) { C = Dk(C, 0.1f); }
					else if (G > 0.96f) { C = Lt(C, 0.12f); }
					break;
				}
				}
				Img.Set(X, Y, C);
			}
		}
		return Img;
	}

	FFVImage PathIcon(int32 St, FColor P, FColor S)
	{
		const FFVImage Tile = PathTile(St, P, S);
		FFVImage Img(16, 16);
		for (int32 Y = 2; Y <= 13; ++Y)
		{
			for (int32 X = 1; X <= 14; ++X)
			{
				// a winding strip from the bottom-left to the top-right
				const float CX = 4.f + (13.5f - float(Y)) * 0.55f + 1.6f * FMath::Sin(float(Y) * 0.7f);
				if (FMath::Abs(X + 0.5f - CX) > 3.6f) { continue; }
				Img.Set(X, Y, Tile.Get((X * 2) & 31, (Y * 2) & 31));
			}
		}
		for (int32 Y = 2; Y <= 13; ++Y)
		{
			for (int32 X = 1; X <= 14; ++X)
			{
				if (Img.Get(X, Y).A != 0) { continue; }
				const bool bNear = Img.Get(X - 1, Y).A != 0 || Img.Get(X + 1, Y).A != 0;
				if (bNear && N01(X, Y, 397u) < 0.55f) { Img.Set(X, Y, (Y % 2) ? Grass : GrassHi); }
			}
		}
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	// ==== BUILDERS ====
	// ================================================================================================================
	// Small helpers of the props below
	// ================================================================================================================
	/** Vertical cylinder-ish column shading: lit left third, shaded right third. */
	void Column(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Base)
	{
		for (int32 YY = Y; YY < Y + H; ++YY)
		{
			for (int32 XX = X; XX < X + W; ++XX)
			{
				const float T = (XX - X + 0.5f) / float(FMath::Max(1, W));
				I.Set(XX, YY, T < 0.3f ? Lt(Base, 0.25f) : (T > 0.7f ? Dk(Base, 0.3f) : Base));
			}
		}
		if (W >= 3) { VLine(I, X + 1, Y, Y + H - 1, Lt(Base, 0.45f)); }
	}

	/** Glass pane with a diagonal glint. */
	void Glass(FFVImage& I, int32 X, int32 Y, int32 W, int32 H, FColor Tint)
	{
		Rect(I, X, Y, W, H, Tint);
		I.Set(X + 1, Y + 1, Lt(Tint, 0.55f));
		if (H > 3) { I.Set(X + 1, Y + 2, Lt(Tint, 0.4f)); }
		if (W > 3) { I.Set(X + 2, Y + 1, Lt(Tint, 0.4f)); }
		HLine(I, X, X + W - 1, Y + H - 1, Dk(Tint, 0.3f));
	}

	/** Small leafy tuft (flowerbeds). */
	void Tuft(FFVImage& I, int32 CX, int32 BY, int32 H, uint32 Seed)
	{
		for (int32 K = -1; K <= 1; ++K)
		{
			const int32 HH = H - FMath::Abs(K) - int32(N01(CX + K, BY, Seed) * 2.f);
			for (int32 T = 0; T < HH; ++T) { I.Set(CX + K + (T > HH / 2 ? K : 0), BY - T, T == HH - 1 ? LeafHi : (T < 1 ? LeafLo : Leaf)); }
		}
	}

	/** A five-dot flower: petals P, centre C. */
	void Blossom(FFVImage& I, int32 X, int32 Y, FColor P, FColor C)
	{
		I.Set(X - 1, Y, P); I.Set(X + 1, Y, Dk(P, 0.12f)); I.Set(X, Y - 1, Lt(P, 0.2f)); I.Set(X, Y + 1, Dk(P, 0.2f));
		I.Set(X, Y, C);
	}

	/** Hard-rock lump with violet veins (alpine garden, stone lantern). */
	void RockLump(FFVImage& I, float CX, float CY, float RX, float RY, uint32 Seed)
	{
		ShadedEllipse(I, CX, CY, RX, RY, Rock, false);
		for (int32 YY = FMath::FloorToInt(CY - RY); YY <= FMath::CeilToInt(CY + RY); ++YY)
		{
			for (int32 XX = FMath::FloorToInt(CX - RX); XX <= FMath::CeilToInt(CX + RX); ++XX)
			{
				if (I.Get(XX, YY).A == 0) { continue; }
				const float V = FMath::Abs(FMath::Sin((XX + YY * 0.6f) * 0.9f + N01(Seed, 0, 7u) * 6.f));
				if (V < 0.08f && N01(XX, YY, Seed) < 0.8f) { I.Set(XX, YY, (XX + YY) % 3 ? RockVein : RockVeinHi); }
			}
		}
		I.Set(FMath::FloorToInt(CX - RX * 0.4f), FMath::FloorToInt(CY - RY * 0.55f), RockHi);
	}

	/** Downscale of a world sprite into a 16x16 icon (box average of the opaque, non-outline texels, drawing in 1..14). */
	FFVImage IconFrom(const FFVImage& Src)
	{
		FFVImage Img(16, 16);
		if (!Src.IsValid()) { return Img; }
		const float Sc = FMath::Max(1.f, FMath::Max(Src.W, Src.H) / 14.f);
		const int32 OW = FMath::Clamp(FMath::RoundToInt(Src.W / Sc), 1, 14), OH = FMath::Clamp(FMath::RoundToInt(Src.H / Sc), 1, 14);
		const int32 OX = 1 + (14 - OW) / 2, OY = 1 + (14 - OH);
		for (int32 Y = 0; Y < OH; ++Y)
		{
			for (int32 X = 0; X < OW; ++X)
			{
				const int32 SX0 = FMath::FloorToInt(X * Sc), SX1 = FMath::Max(SX0 + 1, FMath::FloorToInt((X + 1) * Sc));
				const int32 SY0 = FMath::FloorToInt(Y * Sc), SY1 = FMath::Max(SY0 + 1, FMath::FloorToInt((Y + 1) * Sc));
				float R = 0.f, G = 0.f, B = 0.f;
				int32 N = 0, Tot = 0, Ink = 0;
				for (int32 SY = SY0; SY < SY1; ++SY)
				{
					for (int32 SX = SX0; SX < SX1; ++SX)
					{
						++Tot;
						const FColor C = Src.Get(SX, SY);
						if (C.A < 128) { continue; }
						if (C == PropOut || C == HouseOut) { ++Ink; continue; }
						R += C.R; G += C.G; B += C.B; ++N;
					}
				}
				if (N + Ink == 0 || (N + Ink) * 10 < Tot * 4) { continue; }
				if (N == 0) { continue; }
				FColor C(uint8(R / N), uint8(G / N), uint8(B / N), 255);
				// a little extra contrast so the icon reads at 16 px
				const float L = (C.R + C.G + C.B) / 3.f;
				C = L > 150.f ? Lt(C, 0.08f) : Dk(C, 0.06f);
				Img.Set(OX + X, OY + Y, C);
			}
		}
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	// ================================================================================================================
	// Yard props (pivot bottom-centre)
	// ================================================================================================================
	/** Lanterns: 0 cast-iron post 12x34 · 1 lantern-house 14x32 · 2 garland 44x26 (2 frames) · 3 stone lantern 16x26. */
	FFVImage LanternWorld(int32 St, FColor P, FColor S, int32 F)
	{
		if (St == 0)
		{
			FFVImage Img(12, 34);
			Box(Img, 3, 30, 6, 4, P, 0.3f, 0.35f);
			Box(Img, 4, 27, 4, 3, P, 0.3f, 0.35f);
			Column(Img, 5, 12, 2, 16, P);
			HLine(Img, 4, 7, 16, Lt(P, 0.3f));
			HLine(Img, 3, 8, 12, P);
			Glass(Img, 3, 5, 6, 6, S);
			VLine(Img, 2, 5, 11, P); VLine(Img, 9, 5, 11, Dk(P, 0.3f));
			VLine(Img, 5, 5, 10, Dk(P, 0.1f));
			HLine(Img, 2, 9, 4, P); HLine(Img, 3, 8, 3, Lt(P, 0.2f)); HLine(Img, 4, 7, 2, P);
			Img.Set(5, 1, Lt(P, 0.3f)); Img.Set(6, 1, P); Img.Set(5, 0, P);
			Img.Set(4, 7, Lt(S, 0.5f)); Img.Set(4, 8, Lt(S, 0.35f));
			Img.Outline(PropOut);
			return Img;
		}
		if (St == 1)
		{
			FFVImage Img(14, 32);
			Box(Img, 5, 28, 4, 4, WoodLo, 0.25f, 0.3f);
			Planks(Img, 6, 13, 2, 16, WoodMid, 2, 411u, true);
			VLine(Img, 6, 13, 28, Wood);
			// the little house
			Box(Img, 2, 7, 10, 7, DWood, 0.2f, 0.3f);
			Glass(Img, 4, 8, 6, 5, S);
			VLine(Img, 6, 8, 12, DWoodDk); HLine(Img, 4, 9, 10, DWoodDk);
			HLine(Img, 3, 10, 14, DWoodLo);
			for (int32 Y = 1; Y <= 6; ++Y)
			{
				const int32 HW = Y + 1;
				HLine(Img, 7 - HW, 6 + HW, Y, Y == 6 ? Dk(P, 0.3f) : ((Y % 2) ? P : Dk(P, 0.12f)));
				Img.Set(7 - HW, Y, Lt(P, 0.3f));
			}
			Img.Set(6, 0, Lt(P, 0.35f)); Img.Set(7, 0, P);
			Img.Outline(PropOut);
			return Img;
		}
		if (St == 2)
		{
			FFVImage Img(44, 26);
			for (int32 PX : { 2, 40 })
			{
				Planks(Img, PX, 3, 2, 23, WoodMid, 2, 421u, true);
				VLine(Img, PX, 3, 25, Wood);
				Img.Set(PX, 2, WoodHi); Img.Set(PX + 1, 2, WoodLo);
			}
			static const FColor Bulbs[4] = { FColor(236, 70, 60), FColor(255, 214, 80), FColor(90, 200, 110), FColor(90, 150, 240) };
			int32 K = 0;
			for (int32 X = 4; X <= 39; ++X)
			{
				const float T = (X - 3.5f) / 36.f;
				const int32 Y = FMath::RoundToInt(5.f + 9.f * 4.f * T * (1.f - T));
				Img.Set(X, Y, DWoodDk);
				if ((X - 5) % 4 == 0)
				{
					FColor B = Bulbs[K % 4];
					if (K % 4 == 1) { B = S; }
					if (K % 4 == 0) { B = P; }
					const bool bOn = ((K + F) % 2) == 0;
					const FColor BC = bOn ? Lt(B, 0.25f) : Dk(B, 0.25f);
					Img.Set(X, Y + 1, DWoodDk);
					Img.Set(X, Y + 2, BC); Img.Set(X, Y + 3, Dk(BC, 0.15f));
					if (bOn) { Img.Set(X, Y + 2, Lt(BC, 0.4f)); }
					++K;
				}
			}
			Img.Outline(PropOut);
			return Img;
		}
		// 3: stone lantern of hard rock
		FFVImage Img(16, 26);
		Box(Img, 2, 22, 12, 4, Rock, 0.3f, 0.35f);
		Column(Img, 6, 14, 4, 8, Rock);
		Box(Img, 4, 12, 8, 3, Rock, 0.3f, 0.35f);
		Box(Img, 4, 7, 8, 5, RockLo, 0.2f, 0.3f);
		Rect(Img, 6, 8, 4, 3, S); Img.Set(6, 8, Lt(S, 0.5f));
		for (int32 Y = 3; Y <= 6; ++Y)
		{
			const int32 HW = 3 + (Y - 3) * 2 - (Y == 6 ? 1 : 0);
			HLine(Img, 8 - HW, 7 + HW, Y, Y == 6 ? RockLo : Rock);
			Img.Set(8 - HW, Y, RockHi);
		}
		Img.Set(0, 6, Rock); Img.Set(15, 6, RockLo);
		Ellipse(Img, 8.f, 2.f, 1.6f, 1.6f, Rock); Img.Set(7, 1, RockHi);
		for (int32 V = 0; V < 5; ++V) { Img.Set(3 + V * 2, 23 + (V % 2), RockVein); }
		Img.Set(7, 17, RockVein); Img.Set(8, 18, RockVeinHi); Img.Set(8, 19, RockVein);
		Img.Outline(PropOut);
		Img.Set(7, 9, Lt(S, 0.6f));
		return Img;
	}

	FVector2D LanternLight(int32 St)
	{
		switch (St)
		{
		case 0: return FVector2D(6.f, 8.f);
		case 1: return FVector2D(7.f, 10.5f);
		case 2: return FVector2D(22.f, 14.f);
		default: return FVector2D(8.f, 9.5f);
		}
	}

	/** Mailbox 16x24: 0 blue box with a letter · 1 red box with a flag. */
	FFVImage MailboxWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(16, 24);
		Planks(Img, 6, 12, 3, 12, WoodMid, 3, 431u, true);
		VLine(Img, 6, 12, 23, Wood);
		HLine(Img, 5, 9, 23, WoodLo);
		// box with a rounded top
		Box(Img, 2, 5, 11, 8, P, 0.25f, 0.35f);
		for (int32 X = 3; X <= 11; ++X)
		{
			const int32 Top = (X == 3 || X == 11) ? 4 : 3;
			VLine(Img, X, Top, 4, X < 6 ? Lt(P, 0.35f) : P);
		}
		HLine(Img, 4, 10, 3, Lt(P, 0.3f));
		HLine(Img, 4, 10, 7, Dk(P, 0.45f));   // the slot
		HLine(Img, 5, 9, 8, Lt(P, 0.12f));
		if (St == 0)
		{
			Rect(Img, 5, 9, 5, 3, S);
			Img.Set(5, 9, Dk(S, 0.25f)); Img.Set(6, 10, Dk(S, 0.25f)); Img.Set(7, 10, Dk(S, 0.25f)); Img.Set(8, 10, Dk(S, 0.25f)); Img.Set(9, 9, Dk(S, 0.25f));
			Img.Set(4, 5, Lt(P, 0.5f));
		}
		else
		{
			VLine(Img, 13, 3, 10, IronLo); VLine(Img, 14, 3, 3, IronLo);
			Rect(Img, 13, 1, 3, 3, S); Img.Set(13, 1, Lt(S, 0.4f)); Img.Set(15, 3, Dk(S, 0.3f));
			Img.Set(7, 10, Brass); Img.Set(7, 11, BrassLo);
		}
		Img.Outline(PropOut);
		return Img;
	}

	/** Bench 32x20: 0 simple (planks on iron legs) · 1 carved (scroll ends, a heart in the back). */
	FFVImage BenchWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(32, 20);
		if (St == 0)
		{
			for (int32 LX : { 4, 26 })
			{
				VLine(Img, LX, 2, 19, S); VLine(Img, LX + 1, 2, 19, Dk(S, 0.3f));
				HLine(Img, LX - 1, LX + 2, 19, Dk(S, 0.2f));
			}
			Planks(Img, 2, 3, 28, 6, P, 3, 441u);
			Planks(Img, 1, 11, 30, 4, P, 2, 442u);
			HLine(Img, 1, 30, 15, Dk(P, 0.4f));
			VLine(Img, 6, 15, 18, Dk(S, 0.1f)); VLine(Img, 25, 15, 18, Dk(S, 0.1f));
		}
		else
		{
			// carved back with scrolls and a heart
			Planks(Img, 3, 2, 26, 7, P, 7, 443u);
			HLine(Img, 3, 28, 2, Lt(P, 0.3f));
			for (int32 X = 5; X <= 26; X += 3) { VLine(Img, X, 4, 7, Dk(P, 0.35f)); Img.Set(X + 1, 4, Lt(P, 0.2f)); }
			Rect(Img, 14, 3, 4, 5, P);
			Img.Set(14, 4, S); Img.Set(15, 4, S); Img.Set(16, 4, S); Img.Set(17, 4, S);
			Img.Set(14, 3, S); Img.Set(17, 3, S); Img.Set(15, 5, S); Img.Set(16, 5, S); Img.Set(15, 6, Dk(S, 0.2f)); Img.Set(16, 6, Dk(S, 0.2f));
			for (int32 EX : { 1, 29 })
			{
				Ellipse(Img, EX + 1.f, 4.f, 2.f, 2.2f, Dk(P, 0.15f));
				Img.Set(EX + 1, 4, S); Img.Set(EX, 3, Lt(P, 0.3f));
				VLine(Img, EX + 1, 5, 12, Dk(P, 0.2f));
			}
			Planks(Img, 1, 10, 30, 5, P, 2, 444u);
			HLine(Img, 1, 30, 15, Dk(P, 0.4f));
			for (int32 LX : { 4, 26 })
			{
				VLine(Img, LX, 15, 18, Dk(P, 0.2f)); VLine(Img, LX + 1, 15, 18, Dk(P, 0.45f));
				Img.Set(LX - 1, 19, Dk(P, 0.3f)); Img.Set(LX + 2, 19, Dk(P, 0.45f)); HLine(Img, LX, LX + 1, 19, Dk(P, 0.3f));
				Img.Set(LX, 16, S);
			}
			HLine(Img, 2, 29, 10, Lt(P, 0.35f));
		}
		Img.Outline(PropOut);
		return Img;
	}

	/** Flowerbeds: 0 daisies 26x13 · 1 petunia box 26x16 · 2 alpine rock garden (hard rock) 30x18. */
	FFVImage FlowerbedWorld(int32 St, FColor P, FColor S)
	{
		if (St == 0)
		{
			FFVImage Img(26, 13);
			Ellipse(Img, 13.f, 8.f, 12.5f, 4.8f, StoneLo);
			Ellipse(Img, 13.f, 7.6f, 11.f, 3.8f, Soil);
			for (int32 X = 1; X < 25; X += 3) { Img.Set(X, 8 + (X % 2), Stone); Img.Set(X + 1, 8 + (X % 2), StoneHi); }
			for (int32 K = 0; K < 9; ++K)
			{
				const int32 X = 4 + (K * 7) % 18, Y = 7 + (K * 5) % 4;
				Tuft(Img, X, Y + 1, 4, 451u + K);
			}
			for (int32 K = 0; K < 8; ++K)
			{
				const int32 X = 4 + (K * 5 + K / 3) % 18, Y = 3 + (K * 3) % 5;
				Blossom(Img, X, Y, P, S);
			}
			Img.Outline(PropOut);
			return Img;
		}
		if (St == 1)
		{
			FFVImage Img(26, 16);
			Planks(Img, 1, 8, 24, 7, DWood, 3, 452u);
			HLine(Img, 1, 24, 8, DWoodHi);
			VLine(Img, 1, 8, 14, DWoodLo); VLine(Img, 24, 8, 14, DWoodDk);
			HLine(Img, 2, 23, 7, Soil);
			for (int32 K = 0; K < 7; ++K) { Tuft(Img, 3 + K * 3, 7, 4, 453u + K); }
			for (int32 K = 0; K < 10; ++K)
			{
				const int32 X = 3 + (K * 9) % 20, Y = 2 + (K * 3) % 5;
				Blossom(Img, X, Y, (K % 2) ? P : S, Rgb(255, 240, 200));
			}
			// trailing petunias over the edge
			for (int32 X : { 4, 11, 19 }) { Img.Set(X, 9, Leaf); Img.Set(X, 10, LeafLo); Img.Set(X + 1, 11, P); Img.Set(X, 11, Dk(P, 0.2f)); }
			Img.Outline(PropOut);
			return Img;
		}
		FFVImage Img(30, 18);
		Ellipse(Img, 15.f, 13.f, 14.5f, 4.8f, SoilLo);
		RockLump(Img, 9.f, 10.f, 6.f, 5.f, 461u);
		RockLump(Img, 20.f, 8.f, 7.f, 6.f, 462u);
		RockLump(Img, 14.f, 13.f, 4.f, 3.f, 463u);
		RockLump(Img, 25.f, 13.5f, 3.5f, 2.6f, 464u);
		for (int32 K = 0; K < 8; ++K)
		{
			const int32 X = 3 + (K * 11) % 25, Y = 11 + (K * 3) % 5;
			if (Img.Get(X, Y).A == 0) { continue; }
			Img.Set(X, Y, LeafLo); Img.Set(X + 1, Y, Leaf);
		}
		static const int32 FL[7][2] = { { 5, 12 }, { 12, 9 }, { 17, 13 }, { 23, 10 }, { 27, 13 }, { 9, 14 }, { 20, 3 } };
		for (int32 K = 0; K < 7; ++K)
		{
			Img.Set(FL[K][0], FL[K][1] + 1, Leaf);
			Blossom(Img, FL[K][0], FL[K][1], (K % 2) ? S : P, (K % 2) ? Rgb(255, 240, 200) : Rgb(255, 180, 60));
		}
		Img.Outline(PropOut);
		return Img;
	}

	/** Scarecrow 24x40: 0 straw (P shirt, S straw hat) · 1 fisherman (P jacket, S sou'wester, rod and a fish). */
	FFVImage ScarecrowWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(24, 40);
		Planks(Img, 11, 12, 2, 28, BarkLo, 2, 471u, true);
		VLine(Img, 11, 12, 39, Bark);
		Thick(Img, 2.f, 16.f, 21.f, 16.f, 0.7f, Bark);
		HLine(Img, 2, 21, 16, BarkHi);
		// shirt / jacket
		for (int32 Y = 15; Y <= 28; ++Y)
		{
			const int32 HW = Y < 19 ? 9 - (Y - 15) : 5 + (Y > 25 ? 1 : 0);
			for (int32 X = 12 - HW; X < 12 + HW; ++X)
			{
				FColor C = X < 12 - HW + 2 ? Lt(P, 0.2f) : (X >= 12 + HW - 2 ? Dk(P, 0.3f) : P);
				if (St == 0 && ((X + Y) % 5 == 0)) { C = Dk(C, 0.15f); }   // patched cloth
				Img.Set(X, Y, C);
			}
		}
		if (St == 0)
		{
			Rect(Img, 13, 22, 3, 3, Rgb(90, 130, 190)); Img.Set(13, 22, Rgb(140, 170, 220));   // a patch
			HLine(Img, 7, 16, 25, BarkLo);   // rope belt
			for (int32 X : { 1, 2, 21, 22 }) { Img.Set(X, 17 + (X % 2), Straw); Img.Set(X, 15, StrawLo); }
			for (int32 X = 8; X <= 15; X += 2) { VLine(Img, X, 29, 30 + (X % 3), Straw); }
		}
		else
		{
			for (int32 Y = 16; Y <= 27; ++Y) { Img.Set(12, Y, Dk(P, 0.45f)); }
			Img.Set(11, 19, Brass); Img.Set(11, 23, Brass);
			Rect(Img, 7, 28, 10, 3, Rgb(60, 60, 70));
			for (int32 X : { 1, 22 }) { Img.Set(X, 16, CreamLo); Img.Set(X, 17, CreamLo); }
			// the rod and a fish
			Thick(Img, 22.f, 17.f, 23.f, 2.f, 0.4f, WoodMid);
			VLine(Img, 23, 3, 11, Rgb(220, 220, 220));
			Ellipse(Img, 23.f, 13.f, 1.2f, 2.4f, Rgb(150, 190, 210)); Img.Set(22, 12, Rgb(220, 240, 250));
		}
		// sack head
		ShadedEllipse(Img, 12.f, 11.f, 4.2f, 4.4f, St == 0 ? Rgb(214, 186, 128) : Cream, false);
		Img.Set(10, 11, DWoodDk); Img.Set(14, 11, DWoodDk);
		HLine(Img, 10, 14, 13, DWoodLo); Img.Set(11, 14, DWoodLo); Img.Set(13, 14, DWoodLo);
		Img.Set(9, 12, Rgb(230, 130, 110)); Img.Set(15, 12, Rgb(230, 130, 110));
		if (St == 0)
		{
			HLine(Img, 5, 18, 7, S); HLine(Img, 4, 19, 8, Dk(S, 0.2f));
			for (int32 Y = 3; Y <= 6; ++Y) { HLine(Img, 8, 16, Y, Y == 3 ? Lt(S, 0.3f) : S); }
			HLine(Img, 8, 16, 6, Rgb(200, 70, 60));
		}
		else
		{
			for (int32 Y = 4; Y <= 8; ++Y) { HLine(Img, 8 - (Y > 6 ? Y - 6 : 0), 16 + (Y > 6 ? Y - 6 : 0), Y, Y == 4 ? Lt(S, 0.35f) : (Y == 8 ? Dk(S, 0.25f) : S)); }
			HLine(Img, 5, 19, 9, Dk(S, 0.35f));
		}
		Img.Outline(PropOut);
		return Img;
	}

	/** Wells: 0 with a roof 28x36 · 1 sweep well (журавль) 44x46. */
	FFVImage WellWorld(int32 St, FColor P, FColor S)
	{
		if (St == 0)
		{
			FFVImage Img(28, 36);
			Stones(Img, 2, 22, 24, 14, Stone, StoneDk, 5.f, 481u);
			Ellipse(Img, 14.f, 22.f, 12.f, 3.f, StoneHi);
			Ellipse(Img, 14.f, 22.f, 10.f, 2.1f, WaterDk);
			HLine(Img, 8, 12, 22, WaterLo);
			for (int32 PX : { 3, 23 }) { Planks(Img, PX, 8, 2, 16, S, 2, 482u, true); VLine(Img, PX, 8, 23, Lt(S, 0.2f)); }
			Thick(Img, 4.f, 14.f, 24.f, 14.f, 0.8f, WoodMid);
			HLine(Img, 5, 23, 13, WoodHi);
			Box(Img, 12, 16, 4, 4, WoodMid, 0.3f, 0.35f); HLine(Img, 12, 15, 17, IronLo);
			VLine(Img, 13, 14, 15, Rgb(150, 130, 100));
			VLine(Img, 25, 12, 16, IronLo); Img.Set(26, 16, IronLo);
			for (int32 Y = 1; Y <= 9; ++Y)
			{
				const int32 HW = 3 + Y * 3 / 2;
				for (int32 X = 14 - HW; X < 14 + HW; ++X)
				{
					const FColor C = (X < 14) ? (((X + Y) % 4 == 0) ? Dk(P, 0.12f) : P) : (((X + Y) % 4 == 0) ? Dk(P, 0.4f) : Dk(P, 0.28f));
					Img.Set(X, Y, C);
				}
				Img.Set(14 - HW, Y, Lt(P, 0.3f));
			}
			HLine(Img, 1, 26, 10, Dk(P, 0.5f));
			VLine(Img, 14, 0, 2, S);
			Img.Outline(PropOut);
			return Img;
		}
		FFVImage Img(44, 46);
		// log ring (сруб)
		for (int32 Row = 0; Row < 4; ++Row)
		{
			const int32 Y = 32 + Row * 3;
			Box(Img, 4, Y, 18, 3, (Row % 2) ? LogLo : Log, 0.3f, 0.3f);
			Img.Set(4, Y + 1, LogEnd); Img.Set(21, Y + 1, LogEnd);
		}
		Ellipse(Img, 13.f, 32.f, 8.f, 2.f, WaterDk);
		// pivot post and the sweep
		Planks(Img, 30, 14, 3, 32, P, 3, 491u, true);
		VLine(Img, 30, 14, 45, Lt(P, 0.2f));
		Img.Set(29, 13, P); Img.Set(33, 13, P); Img.Set(29, 12, Lt(P, 0.2f)); Img.Set(33, 12, Dk(P, 0.2f));
		Thick(Img, 6.f, 4.f, 42.f, 20.f, 0.9f, WoodMid);
		Thick(Img, 6.f, 3.5f, 42.f, 19.5f, 0.3f, WoodHi);
		Box(Img, 37, 18, 6, 5, StoneLo, 0.3f, 0.35f);   // counterweight
		VLine(Img, 31, 14, 16, S);
		// the pole and bucket down into the well
		VLine(Img, 7, 5, 26, WoodLo);
		Box(Img, 5, 26, 5, 5, WoodMid, 0.3f, 0.35f);
		HLine(Img, 5, 9, 27, S); HLine(Img, 5, 9, 29, S);
		Img.Outline(PropOut);
		return Img;
	}

	/** Weather vanes 16x42: 0 rooster (P figure) · 1 fish (P copper figure); S gilt. */
	FFVImage VaneWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(16, 42);
		Column(Img, 7, 12, 2, 28, Iron);
		Box(Img, 5, 38, 6, 4, StoneLo, 0.3f, 0.35f);
		HLine(Img, 2, 13, 18, Iron); VLine(Img, 8, 15, 21, Iron);
		Img.Set(1, 18, S); Img.Set(14, 18, S); Img.Set(8, 14, S); Img.Set(8, 22, S);
		Ellipse(Img, 8.f, 11.f, 1.3f, 1.3f, S);
		if (St == 0)
		{
			const FFVImage R = Ascii({
				TEXT("....cc.......2"),
				TEXT("...cPPc....22."),
				TEXT("..bPPPPc..2P2."),
				TEXT("...PPPPPPPPP2."),
				TEXT("....PPPPPPPP..") ,
				TEXT(".....PPPPPP..."),
				TEXT("......d..d...."),
				TEXT("......d..d...."),
			}, { { 'P', P }, { 'c', Rgb(220, 50, 50) }, { 'b', S }, { '2', Lt(P, 0.3f) }, { 'd', S } });
			Img.Blit(R, 1, 2);
			Img.Set(5, 4, Cream);
		}
		else
		{
			const FFVImage R = Ascii({
				TEXT("...........22"),
				TEXT("..hPPPPPP.2P."),
				TEXT(".hPPoPPPPPPP."),
				TEXT("PPPPPPPPPPP.."),
				TEXT(".dPPPdPdPPPP."),
				TEXT("..dPPPPPd.2P."),
				TEXT("...........22"),
			}, { { 'P', P }, { 'h', Lt(P, 0.35f) }, { 'd', Dk(P, 0.3f) }, { 'o', S }, { '2', S } });
			Img.Blit(R, 1, 3);
		}
		Img.Outline(PropOut);
		return Img;
	}

	/** Yard centre pieces: 0 woodpile 30x22 · 1 fountain 36x30 (3 frames) · 2 fish statue 22x34 · 3 golden fisherman 24x38. */
	FFVImage CenterWorld(int32 St, FColor P, FColor S, int32 F)
	{
		// Styles follow the data (FVDatabaseU6Content.cpp): 0 woodpile · 1 fountain · 2 fish statue · 3 golden fisherman
		if (St == 0)
		{
			// woodpile: three rows of birch logs seen end-on between two stakes
			FFVImage Img(30, 22);
			Box(Img, 1, 6, 2, 16, Dk(P, 0.25f), 0.2f, 0.3f);
			Box(Img, 27, 6, 2, 16, Dk(P, 0.25f), 0.2f, 0.3f);
			static const int32 Rows[3] = { 5, 4, 3 };
			for (int32 R = 0; R < 3; ++R)
			{
				const int32 N = Rows[R];
				const float Y = 18.f - float(R) * 5.f;
				const float X0 = 15.f - float(N - 1) * 2.8f;
				for (int32 K = 0; K < N; ++K)
				{
					const float X = X0 + float(K) * 5.6f;
					Ellipse(Img, X, Y, 2.9f, 2.9f, P);
					Ellipse(Img, X, Y, 2.1f, 2.1f, S);
					Img.Set(FMath::RoundToInt(X), FMath::RoundToInt(Y), Dk(S, 0.25f));
					Img.Set(FMath::RoundToInt(X) - 1, FMath::RoundToInt(Y) - 1, Lt(S, 0.3f));
				}
			}
			Img.Outline(PropOut);
			return Img;
		}
		if (St == 3)
		{
			// golden fisherman: a gilt statue on a stone pedestal, rod raised, a fish in the other hand
			FFVImage Img(24, 38);
			Box(Img, 5, 32, 14, 6, Stone, 0.3f, 0.35f);
			Box(Img, 7, 29, 10, 3, StoneLo, 0.3f, 0.35f);
			HLine(Img, 8, 15, 34, S);
			Box(Img, 9, 21, 2, 8, Dk(P, 0.1f), 0.2f, 0.3f);
			Box(Img, 13, 21, 2, 8, Dk(P, 0.1f), 0.2f, 0.3f);
			Box(Img, 8, 12, 8, 10, P, 0.3f, 0.35f);
			Ellipse(Img, 12.f, 9.f, 2.6f, 2.6f, Lt(P, 0.1f));
			HLine(Img, 8, 16, 6, Dk(P, 0.15f));
			Box(Img, 10, 3, 5, 3, P, 0.3f, 0.35f);
			for (int32 K = 0; K <= 12; ++K)
			{
				const float T = float(K) / 12.f;
				Img.Set(FMath::RoundToInt(15.f + 6.f * T), FMath::RoundToInt(14.f - 12.f * T), S);
			}
			Ellipse(Img, 5.f, 18.f, 1.6f, 3.2f, Lt(P, 0.15f));
			Img.Set(5, 21, P); Img.Set(4, 22, Dk(P, 0.2f)); Img.Set(6, 22, Dk(P, 0.2f));
			Img.Outline(PropOut);
			Img.Set(10, 13, GoldHi); Img.Set(11, 8, GoldHi); Img.Set(19, 5, GoldHi); Img.Set(9, 24, GoldHi);
			return Img;
		}
		St -= 1;   // 1 fountain -> the fountain below, 2 fish statue -> the statue below
		if (St == 0)
		{
			FFVImage Img(36, 30);
			// basin
			Ellipse(Img, 18.f, 22.f, 17.f, 7.f, Dk(P, 0.3f));
			for (int32 Y = 22; Y < 30; ++Y) { for (int32 X = 1; X < 35; ++X) { if (Img.Get(X, Y).A) { Img.Set(X, Y, (X < 8) ? Lt(P, 0.1f) : ((X > 28) ? Dk(P, 0.3f) : Dk(P, 0.12f))); } } }
			Ellipse(Img, 18.f, 20.f, 17.f, 5.f, Lt(P, 0.25f));
			Ellipse(Img, 18.f, 20.f, 15.f, 3.8f, S);
			Ellipse(Img, 18.f, 19.6f, 13.f, 2.6f, Lt(S, 0.18f));
			for (int32 K = 0; K < 4; ++K)
			{
				const int32 RX = 6 + ((K * 7 + F * 3) % 22);
				HLine(Img, RX, RX + 2, 19 + (K % 3) - 1, Lt(S, 0.5f));
			}
			// pedestal and the fish spouting water
			Column(Img, 16, 10, 4, 10, P);
			Box(Img, 14, 9, 8, 2, Lt(P, 0.1f), 0.3f, 0.35f);
			Ellipse(Img, 18.f, 6.f, 2.4f, 3.2f, Bronze);
			Img.Set(17, 4, BronzeHi); Img.Set(19, 8, BronzeLo); Img.Set(18, 2, BronzeLo); Img.Set(17, 2, Bronze); Img.Set(19, 2, Bronze);
			Img.Set(18, 5, Rgb(40, 40, 40));
			Img.Outline(PropOut);
			// water arcs (after the outline, so they stay light)
			for (int32 Side : { -1, 1 })
			{
				for (int32 K = 0; K < 12; ++K)
				{
					const float T = float(K) / 11.f;
					const float X = 18.f + Side * (1.f + T * 11.f);
					const float Y = 3.f - 5.f * T + 18.f * T * T;
					if ((K + F) % 3 == 0) { continue; }
					Img.Set(FMath::RoundToInt(X), FMath::RoundToInt(Y), K % 2 ? WaterHi : Lt(S, 0.35f));
				}
			}
			VLine(Img, 18, 0, 1, WaterHi);
			for (int32 K = 0; K < 3; ++K) { Img.Set(6 + K * 12 + F * 2, 17 + (K + F) % 2, WaterHi); }
			return Img;
		}
		{
			FFVImage Img(22, 34);
			Box(Img, 3, 28, 16, 6, Stone, 0.3f, 0.35f);
			Box(Img, 5, 22, 12, 6, StoneLo, 0.3f, 0.35f);
			HLine(Img, 6, 15, 24, S); HLine(Img, 7, 14, 25, Dk(S, 0.2f));   // gilt plate
			// leaping fish: a curved body from the base up
			for (int32 K = 0; K <= 20; ++K)
			{
				const float T = float(K) / 20.f;
				const float X = 8.f + 7.f * FMath::Sin(T * 2.6f);
				const float Y = 21.f - T * 17.f;
				const float R = 1.2f + 2.3f * FMath::Sin(T * PI);
				Ellipse(Img, X, Y, R, R, P);
			}
			for (int32 Y = 2; Y < 22; ++Y)
			{
				for (int32 X = 1; X < 21; ++X)
				{
					const FColor C = Img.Get(X, Y);
					if (C != P) { continue; }
					const bool bL = Img.Get(X - 1, Y).A == 0, bR = Img.Get(X + 1, Y).A == 0;
					if (bL) { Img.Set(X, Y, Lt(P, 0.35f)); }
					else if (bR) { Img.Set(X, Y, Dk(P, 0.35f)); }
					else if ((X + Y * 2) % 5 == 0) { Img.Set(X, Y, S); }
				}
			}
			// tail fins
			Img.Set(6, 21, P); Img.Set(10, 21, P); Img.Set(5, 22, Dk(P, 0.2f)); Img.Set(11, 22, Dk(P, 0.2f));
			// head, eye and dorsal fin
			Img.Set(9, 2, Lt(P, 0.2f)); Img.Set(10, 4, Rgb(30, 30, 40)); Img.Set(11, 4, GoldHi);
			Img.Set(16, 10, S); Img.Set(17, 11, S); Img.Set(17, 12, Dk(S, 0.2f));
			Img.Outline(PropOut);
			Img.Set(8, 5, GoldHi);
			return Img;
		}
	}

	// ================================================================================================================
	// Expensive furniture (pivot bottom-centre; 3/4 front view like FVArtHome.cpp)
	// ================================================================================================================
	/** A shelf of plates / jugs behind glass. */
	void Dishes(FFVImage& I, int32 X, int32 Y, int32 W, uint32 Seed)
	{
		for (int32 XX = X; XX < X + W - 1; XX += 3)
		{
			const float K = N01(XX, Y, Seed);
			if (K < 0.5f) { Ellipse(I, XX + 1.f, Y - 1.5f, 1.3f, 1.8f, K < 0.25f ? Rgb(240, 240, 250) : Rgb(120, 170, 220)); I.Set(XX + 1, Y - 2, Rgb(60, 110, 190)); }
			else { Rect(I, XX, Y - 3, 2, 3, K < 0.75f ? Rgb(230, 200, 150) : Rgb(200, 90, 70)); I.Set(XX, Y - 3, Rgb(255, 250, 240)); }
		}
	}

	/** Carved buffet 30x40: 0 painted (P body, S flowers) · 1 carved dark oak (brass scrolls, crown). */
	FFVImage CupboardWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(30, 40);
		const bool bRich = St == 1;
		// top cornice
		Box(Img, 1, 3, 28, 3, Lt(P, 0.1f), 0.35f, 0.35f);
		if (bRich)
		{
			for (int32 X = 5; X <= 24; ++X) { const int32 H = 3 - FMath::Abs(X - 15) / 4; if (H > 0) { VLine(Img, X, 3 - H, 2, P); } }
			Img.Set(15, 0, S); Img.Set(14, 1, S); Img.Set(16, 1, S);
		}
		// upper glass cabinet
		Box(Img, 2, 6, 26, 14, P, 0.2f, 0.3f);
		for (int32 D = 0; D < 2; ++D)
		{
			const int32 DX = 4 + D * 11;
			Rect(Img, DX, 7, 10, 12, Dk(P, 0.35f));
			Glass(Img, DX + 1, 8, 8, 10, Rgb(110, 120, 140));
			HLine(Img, DX + 1, DX + 8, 12, Dk(P, 0.3f));
			Dishes(Img, DX + 1, 12, 8, 501u + D);
			Dishes(Img, DX + 1, 17, 8, 503u + D);
			Img.Set(DX + 2, 9, Rgb(220, 230, 240));
		}
		// counter
		Box(Img, 0, 20, 30, 2, bRich ? Lt(P, 0.15f) : Lt(P, 0.2f), 0.4f, 0.3f);
		// lower doors
		Box(Img, 2, 22, 26, 17, P, 0.2f, 0.35f);
		for (int32 D = 0; D < 2; ++D)
		{
			const int32 DX = 4 + D * 11;
			Inset(Img, DX, 24, 10, 12, Dk(P, 0.08f));
			if (bRich)
			{
				// brass scrolls
				for (int32 K = 0; K < 3; ++K) { Img.Set(DX + 3 + K, 27, S); Img.Set(DX + 3 + K, 32, S); }
				Img.Set(DX + 2, 28, S); Img.Set(DX + 2, 31, S); Img.Set(DX + 6, 28, S); Img.Set(DX + 6, 31, S);
				Img.Set(DX + 4, 29, Lt(S, 0.3f)); Img.Set(DX + 4, 30, Dk(S, 0.2f));
			}
			else
			{
				// painted flower
				Blossom(Img, DX + 5, 29, S, Rgb(255, 220, 90));
				Img.Set(DX + 4, 31, Leaf); Img.Set(DX + 6, 31, Leaf); Img.Set(DX + 5, 32, LeafLo);
			}
		}
		Img.Set(14, 30, S); Img.Set(15, 30, S);
		HLine(Img, 1, 28, 39, Dk(P, 0.5f));
		Img.Set(3, 39, Dk(P, 0.4f)); Img.Set(26, 39, Dk(P, 0.4f));
		Img.Outline(PropOut);
		return Img;
	}

	/** Bookcase 32x48: 0 shelf of books · 1 grand bookcase with a brass crown and a ladder. */
	FFVImage BookcaseWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(32, 48);
		const bool bRich = St == 1;
		const int32 Top = bRich ? 4 : 2;
		Box(Img, 1, Top, 30, 48 - Top, P, 0.2f, 0.35f);
		HLine(Img, 0, 31, Top, Lt(P, 0.3f)); HLine(Img, 0, 31, Top + 1, P);
		if (bRich)
		{
			for (int32 X = 6; X <= 25; ++X) { const int32 H = 4 - FMath::Abs(X - 15) / 3; if (H > 0) { VLine(Img, X, Top - H, Top - 1, Dk(P, 0.1f)); } }
			HLine(Img, 11, 20, Top - 1, S); Img.Set(15, Top - 3, S); Img.Set(16, Top - 3, S);
		}
		static const FColor Spine[7] = { FColor(180, 60, 50), FColor(60, 110, 170), FColor(70, 140, 80), FColor(210, 170, 70),
			FColor(130, 70, 140), FColor(200, 110, 60), FColor(90, 90, 110) };
		for (int32 Sh = 0; Sh < 4; ++Sh)
		{
			const int32 SY = Top + 3 + Sh * 10;
			Rect(Img, 3, SY, 26, 8, Dk(P, 0.5f));
			int32 X = 3;
			uint32 K = uint32(Sh * 31);
			while (X < 28)
			{
				const int32 BW = 1 + int32(N01(int32(K), Sh, 511u) * 2.2f);
				const int32 BH = 5 + int32(N01(int32(K), Sh, 512u) * 3.f);
				const FColor C = Spine[(K * 5 + Sh) % 7];
				if (N01(int32(K), Sh, 513u) < 0.1f) { X += 2; ++K; continue; }
				Rect(Img, X, SY + 8 - BH, FMath::Min(BW, 29 - X), BH, C);
				VLine(Img, X, SY + 8 - BH, SY + 7, Lt(C, 0.25f));
				Img.Set(X, SY + 9 - BH + 1, bRich ? S : Lt(C, 0.5f));
				X += BW + 0;
				++K;
			}
			Box(Img, 2, SY + 8, 28, 2, Lt(P, 0.12f), 0.35f, 0.3f);
		}
		VLine(Img, 2, Top + 2, 46, Lt(P, 0.2f)); VLine(Img, 29, Top + 2, 46, Dk(P, 0.3f));
		if (bRich)
		{
			// the ladder leaning on the rail
			Thick(Img, 22.f, 8.f, 27.f, 46.f, 0.5f, Lt(P, 0.3f));
			Thick(Img, 26.f, 8.f, 31.f, 46.f, 0.5f, Lt(P, 0.2f));
			for (int32 R = 0; R < 6; ++R) { const int32 Y = 12 + R * 6; HLine(Img, 23 + (Y - 8) * 5 / 38, 27 + (Y - 8) * 5 / 38, Y, Lt(P, 0.35f)); }
			HLine(Img, 1, 30, 7, S);
		}
		Img.Outline(PropOut);
		return Img;
	}

	/** Clocks (4 pendulum frames): 0 wall clock ходики 14x32 · 1 grandfather clock 16x48. */
	FFVImage ClockWorld(int32 St, FColor P, FColor S, int32 F)
	{
		static const int32 Swing[4] = { 0, 1, 0, -1 };
		const int32 Sw = Swing[F & 3];
		if (St == 0)
		{
			FFVImage Img(14, 32);
			for (int32 Y = 0; Y <= 3; ++Y) { HLine(Img, 6 - Y - 1, 7 + Y + 1, Y + 1, Y == 0 ? Lt(P, 0.3f) : P); }
			Box(Img, 2, 4, 10, 11, P, 0.25f, 0.35f);
			ShadedEllipse(Img, 7.f, 9.f, 3.6f, 3.6f, Cream, false);
			Img.Set(7, 7, DWoodDk); Img.Set(7, 8, DWoodDk); Img.Set(8, 9, DWoodDk); Img.Set(7, 9, DWoodDk);
			for (int32 A = 0; A < 4; ++A) { Img.Set(7 + (A == 1 ? 3 : (A == 3 ? -3 : 0)), 9 + (A == 0 ? -3 : (A == 2 ? 3 : 0)), S); }
			Img.Set(4, 13, S); Img.Set(9, 13, S);   // painted rose corners
			// chains and pine-cone weights
			VLine(Img, 5, 15, 26, IronLo); VLine(Img, 9, 15, 22, IronLo);
			Ellipse(Img, 5.f, 28.f, 1.2f, 2.2f, S); Ellipse(Img, 9.f, 24.f, 1.2f, 2.2f, S);
			Img.Set(5, 27, Lt(S, 0.4f)); Img.Set(9, 23, Lt(S, 0.4f));
			// pendulum
			const int32 PX = 7 + Sw;
			VLine(Img, 7, 15, 17, BrassLo); Img.Set(7 + (Sw > 0 ? 1 : (Sw < 0 ? -1 : 0)), 18, BrassLo);
			VLine(Img, PX, 19, 19, BrassLo);
			Ellipse(Img, PX + 0.5f, 21.f, 1.5f, 1.5f, S); Img.Set(PX, 20, Lt(S, 0.5f));
			Img.Outline(PropOut);
			return Img;
		}
		FFVImage Img(16, 48);
		// hood with a crown
		for (int32 X = 3; X <= 12; ++X) { const int32 H = 3 - FMath::Abs(X - 7) / 2; if (H > 0) { VLine(Img, X, 3 - H, 2, P); } }
		Img.Set(7, 0, S); Img.Set(8, 0, S);
		Box(Img, 1, 3, 14, 13, P, 0.25f, 0.35f);
		ShadedEllipse(Img, 8.f, 9.5f, 5.f, 5.f, S, false);
		ShadedEllipse(Img, 8.f, 9.5f, 4.f, 4.f, Cream, false);
		for (int32 A = 0; A < 12; ++A)
		{
			const float Ang = A * PI / 6.f;
			Img.Set(FMath::RoundToInt(7.5f + 3.2f * FMath::Sin(Ang)), FMath::RoundToInt(9.f - 3.2f * FMath::Cos(Ang)), DWoodDk);
		}
		Img.Set(8, 7, DWoodDk); Img.Set(8, 8, DWoodDk); Img.Set(8, 9, DWoodDk); Img.Set(9, 9, DWoodDk); Img.Set(10, 10, DWoodDk);
		// trunk with the pendulum window
		Box(Img, 3, 16, 10, 26, P, 0.2f, 0.35f);
		Rect(Img, 5, 18, 6, 18, Dk(P, 0.45f));
		Glass(Img, 5, 18, 6, 18, Rgb(70, 60, 60));
		VLine(Img, 7, 18, 30, S); VLine(Img, 8, 18, 30 - 1, Dk(S, 0.2f));
		const int32 PX = 7 + Sw * 1;
		Ellipse(Img, PX + 1.f, 32.f, 2.f, 2.f, S); Img.Set(PX, 31, GoldHi);
		HLine(Img, 5, 10, 18, S); HLine(Img, 5, 10, 35, S);
		// plinth
		Box(Img, 1, 42, 14, 6, P, 0.25f, 0.4f);
		HLine(Img, 2, 13, 44, S);
		Img.Outline(PropOut);
		return Img;
	}

	/** Fireplace 40x40 (3 fire frames): 0 brick with a wooden mantel · 1 carved stone with columns and brass. */
	FFVImage FireplaceWorld(int32 St, FColor P, FColor S, int32 F)
	{
		FFVImage Img(40, 40);
		if (St == 0)
		{
			for (int32 Y = 6; Y < 40; ++Y)
			{
				for (int32 X = 2; X < 38; ++X)
				{
					const int32 Row = (Y - 6) / 3, Off = (Row % 2) * 3;
					const bool bJoint = (Y - 6) % 3 == 2 || (X + Off) % 6 == 5;
					FColor C = bJoint ? Mortar : (N01((X + Off) / 6, Row, 521u) < 0.3f ? Dk(P, 0.15f) : P);
					if (!bJoint && (Y - 6) % 3 == 0) { C = Lt(C, 0.15f); }
					Img.Set(X, Y, C);
				}
			}
			Planks(Img, 0, 4, 40, 4, DWood, 2, 522u);
			HLine(Img, 0, 39, 4, DWoodHi);
			// jugs on the mantel
			Ellipse(Img, 7.f, 2.f, 1.6f, 2.f, Rgb(200, 120, 70)); Rect(Img, 30, 0, 3, 4, S); Img.Set(30, 0, Lt(S, 0.4f));
		}
		else
		{
			Box(Img, 2, 8, 36, 32, P, 0.25f, 0.35f);
			for (int32 CX : { 3, 32 }) { Column(Img, CX, 10, 5, 29, Lt(P, 0.05f)); HLine(Img, CX, CX + 4, 12, S); HLine(Img, CX, CX + 4, 37, S); }
			Box(Img, 0, 4, 40, 5, Lt(P, 0.1f), 0.35f, 0.35f);
			for (int32 X = 2; X < 38; X += 3) { Img.Set(X, 7, Dk(P, 0.25f)); }
			HLine(Img, 6, 33, 6, S);
			Ellipse(Img, 20.f, 3.f, 3.f, 2.f, S); Img.Set(19, 2, GoldHi);   // brass shell
			Img.Set(4, 3, Rgb(80, 120, 190)); Img.Set(4, 2, Rgb(140, 180, 230)); Img.Set(35, 3, Rgb(80, 120, 190)); Img.Set(35, 2, Rgb(140, 180, 230));
		}
		// firebox
		const int32 FX = 10, FY = 16, FW = 20, FH = 22;
		for (int32 Y = FY; Y < FY + FH; ++Y)
		{
			for (int32 X = FX; X < FX + FW; ++X)
			{
				const float DX = (X + 0.5f - 20.f) / 10.f, DY = (Y + 0.5f - (FY + 8.f)) / 8.f;
				if (Y < FY + 8 && DX * DX + DY * DY > 1.f) { continue; }
				Img.Set(X, Y, (Y < FY + 3) ? Dk(FireBack, 0.2f) : FireBack);
			}
		}
		HLine(Img, FX, FX + FW - 1, FY + FH - 1, Ember);
		// logs
		Thick(Img, 13.f, 35.f, 27.f, 33.f, 1.2f, Bark);
		Thick(Img, 14.f, 33.f, 26.f, 35.f, 1.1f, BarkLo);
		Img.Set(13, 35, LogEnd); Img.Set(27, 33, LogEnd);
		Flame(Img, 17.f + (F == 1 ? 1.f : 0.f), 32, 9 + (F % 2) * 2, 2.6f, 531u + F);
		Flame(Img, 22.f - (F == 2 ? 1.f : 0.f), 32, 11 - (F % 2) * 2, 3.f, 541u + F);
		Flame(Img, 25.f, 33, 5 + F, 1.6f, 551u + F);
		// grate
		for (int32 X = FX + 1; X < FX + FW - 1; X += 3) { VLine(Img, X, 34, 37, St == 1 ? S : IronLo); }
		HLine(Img, FX + 1, FX + FW - 2, 36, St == 1 ? S : Iron);
		Img.Outline(PropOut);
		Warm(Img, 20.f, 30.f, 22.f, FireO, 0.18f);
		return Img;
	}

	/** Chandelier 32x20 (hangs from the ceiling beam, pivot bottom-centre): 0 wooden wheel · 1 brass with crystal drops. */
	FFVImage ChandelierWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(32, 20);
		for (int32 Y = 0; Y <= 6; ++Y) { Img.Set(16, Y, Y % 2 ? Dk(P, 0.2f) : Lt(P, 0.1f)); }
		if (St == 0)
		{
			Ellipse(Img, 16.f, 14.f, 14.f, 3.6f, P);
			Ellipse(Img, 16.f, 13.6f, 11.f, 2.2f, FVArt::WithAlpha(P, 0));
			for (int32 Y = 10; Y <= 18; ++Y) { for (int32 X = 0; X < 32; ++X) { const FColor C = Img.Get(X, Y); if (C.A) { Img.Set(X, Y, Y < 13 ? Lt(P, 0.25f) : (Y > 15 ? Dk(P, 0.3f) : P)); } } }
			Thick(Img, 16.f, 6.f, 4.f, 13.f, 0.3f, Dk(P, 0.2f));
			Thick(Img, 16.f, 6.f, 28.f, 13.f, 0.3f, Dk(P, 0.2f));
			Ellipse(Img, 16.f, 7.f, 1.5f, 1.5f, P);
			Img.Outline(PropOut);
			for (int32 CX : { 4, 9, 16, 23, 28 }) { Candle(Img, CX, CX == 16 ? 14 : 12, S); }
			return Img;
		}
		// brass: a stem with a ball and 6 curved arms, crystal drops
		Column(Img, 15, 4, 2, 10, P);
		ShadedEllipse(Img, 16.f, 12.f, 3.f, 2.6f, P);
		for (int32 Side : { -1, 1 })
		{
			for (int32 A = 0; A < 3; ++A)
			{
				const float Reach = 5.f + A * 4.5f;
				for (int32 K = 0; K <= 12; ++K)
				{
					const float T = float(K) / 12.f;
					const float X = 16.f + Side * Reach * T;
					const float Y = 12.f + 3.f * FMath::Sin(T * PI) - A * 1.2f * T;
					Img.Set(FMath::RoundToInt(X), FMath::RoundToInt(Y), (K % 3) ? P : Lt(P, 0.35f));
				}
				const int32 EX = FMath::RoundToInt(16.f + Side * Reach);
				const int32 EY = FMath::RoundToInt(12.f - A * 1.2f);
				HLine(Img, EX - 1, EX + 1, EY, P);
			}
		}
		HLine(Img, 13, 19, 3, Lt(P, 0.25f));
		Img.Outline(PropOut);
		for (int32 Side : { -1, 1 })
		{
			for (int32 A = 0; A < 3; ++A)
			{
				const int32 EX = FMath::RoundToInt(16.f + Side * (5.f + A * 4.5f));
				const int32 EY = FMath::RoundToInt(12.f - A * 1.2f);
				Candle(Img, EX, EY - 3, Cream);
				Img.Set(EX, EY + 2, S); Img.Set(EX, EY + 3, Lt(S, 0.4f));
			}
		}
		Img.Set(16, 16, S); Img.Set(16, 17, Lt(S, 0.5f)); Img.Set(15, 16, Dk(S, 0.2f)); Img.Set(17, 16, Dk(S, 0.2f));
		return Img;
	}

	/** Statue: 0 stuffed Tsar Carp on an oak shield 44x28 · 1 aquarium column 24x56 (3 bubble frames). */
	FFVImage StatueWorld(int32 St, FColor P, FColor S, int32 F)
	{
		if (St == 0)
		{
			FFVImage Img(44, 28);
			// oak shield with a carved border
			for (int32 Y = 1; Y < 27; ++Y)
			{
				const int32 In = (Y < 4) ? 4 - Y : ((Y > 22) ? (Y - 22) * 2 : 0);
				for (int32 X = 1 + In; X < 43 - In; ++X)
				{
					FColor C = P;
					if (N01(X / 3, Y, 561u) < 0.12f) { C = Dk(P, 0.12f); }
					if (X < 3 + In || Y < 3) { C = Lt(P, 0.25f); }
					if (X > 40 - In || Y > 24) { C = Dk(P, 0.3f); }
					Img.Set(X, Y, C);
				}
			}
			// the carp: gold scales, red fins, a little crown
			const FColor CarpG(236, 176, 60), CarpHi(255, 230, 130), CarpLo(176, 110, 40), Fin(220, 90, 60);
			Ellipse(Img, 21.f, 14.f, 14.f, 6.2f, CarpG);
			for (int32 Y = 7; Y <= 21; ++Y)
			{
				for (int32 X = 6; X <= 36; ++X)
				{
					if (Img.Get(X, Y) != CarpG) { continue; }
					FColor C = CarpG;
					if (Y < 11) { C = CarpHi; }
					else if (Y > 17) { C = CarpLo; }
					if (((X + (Y % 2) * 2) % 4) == 0 && Y > 9 && Y < 19) { C = Dk(C, 0.18f); }
					Img.Set(X, Y, C);
				}
			}
			for (int32 K = 0; K < 6; ++K) { VLine(Img, 36 + K, 14 - K - 1, 14 + K + 1, K > 3 ? Lt(Fin, 0.2f) : Fin); }
			for (int32 K = 0; K < 5; ++K) { HLine(Img, 16 + K, 22 + K, 7 - K / 2, Fin); }
			Img.Set(20, 21, Fin); Img.Set(21, 22, Fin); Img.Set(22, 21, Fin);
			Img.Set(11, 12, Rgb(30, 30, 40)); Img.Set(10, 11, Rgb(255, 255, 255));
			HLine(Img, 7, 9, 16, CarpLo); Img.Set(7, 17, CarpLo);   // whiskers
			Img.Set(6, 18, CarpLo);
			Img.Set(13, 8, S); Img.Set(15, 7, S); Img.Set(17, 8, S); HLine(Img, 13, 17, 9, S); Img.Set(15, 6, Rgb(220, 60, 60));
			// brass plate
			HLine(Img, 16, 26, 25, S); HLine(Img, 16, 26, 24, Lt(S, 0.3f));
			Img.Outline(PropOut);
			return Img;
		}
		FFVImage Img(24, 56);
		Box(Img, 1, 49, 22, 7, P, 0.3f, 0.4f);
		HLine(Img, 2, 21, 51, Dk(P, 0.25f));
		Box(Img, 2, 0, 20, 5, P, 0.3f, 0.4f);
		HLine(Img, 4, 19, 2, Lt(P, 0.35f));
		// the glass cylinder of water
		for (int32 Y = 5; Y < 49; ++Y)
		{
			for (int32 X = 3; X < 21; ++X)
			{
				const float T = (X - 2.5f) / 18.f;
				FColor C = MixC(Lt(S, 0.15f), Dk(S, 0.3f), float(Y - 5) / 44.f);
				if (T < 0.15f) { C = Lt(C, 0.3f); }
				else if (T > 0.85f) { C = Dk(C, 0.3f); }
				Img.Set(X, Y, C);
			}
		}
		VLine(Img, 5, 7, 46, Lt(S, 0.6f));
		// sand, weeds and a coral
		for (int32 X = 3; X < 21; ++X) { VLine(Img, X, 45 + (X % 3 == 0 ? 0 : 1), 48, (X % 2) ? Rgb(226, 204, 150) : Rgb(204, 180, 128)); }
		for (int32 W : { 6, 16 }) { for (int32 Y = 30; Y < 46; ++Y) { Img.Set(W + ((Y / 3 + F) % 2), Y, (Y % 4) ? Leaf : LeafLo); } }
		Img.Set(12, 44, Rgb(240, 110, 110)); Img.Set(11, 43, Rgb(240, 110, 110)); Img.Set(13, 42, Rgb(250, 150, 140)); Img.Set(12, 43, Rgb(220, 90, 100));
		// little fish (they drift with the frames)
		const int32 FishX[3] = { 8, 14, 10 }, FishY[3] = { 16, 26, 36 };
		static const FColor FishC[3] = { FColor(255, 150, 50), FColor(250, 220, 80), FColor(220, 80, 80) };
		for (int32 K = 0; K < 3; ++K)
		{
			const int32 FX = FishX[K] + ((F + K) % 3) - 1;
			HLine(Img, FX, FX + 2, FishY[K], FishC[K]); Img.Set(FX + 3, FishY[K] - 1, FishC[K]); Img.Set(FX + 3, FishY[K] + 1, FishC[K]);
			Img.Set(FX, FishY[K], Rgb(30, 30, 40));
		}
		// bubbles
		for (int32 K = 0; K < 5; ++K)
		{
			const int32 BY = 44 - ((K * 9 + F * 5) % 38);
			Img.Set(18 - (K % 2), BY, WaterHi);
		}
		Img.Outline(PropOut);
		return Img;
	}

	/** Tiled stove 30x52: 0 blue-and-white tiles (P) with a copper door (S) · 1 green majolica with brass. */
	FFVImage StoveWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(30, 52);
		// cornice + crown
		Box(Img, 1, 4, 28, 4, Lt(P, 0.1f), 0.35f, 0.35f);
		for (int32 X = 6; X <= 23; ++X) { const int32 H = 4 - FMath::Abs(X - 15) / 3; if (H > 0) { VLine(Img, X, 4 - H, 3, P); } }
		Img.Set(14, 0, S); Img.Set(15, 0, S);
		// tiled body
		for (int32 Y = 8; Y < 46; ++Y)
		{
			for (int32 X = 2; X < 28; ++X)
			{
				const int32 TX = (X - 2) % 6, TY = (Y - 8) % 6;
				FColor C;
				if (St == 0)
				{
					C = Wash;
					if (TX == 5 || TY == 5) { C = WashLo; }
					else
					{
						const int32 DX = FMath::Abs(TX - 2), DY = FMath::Abs(TY - 2);
						if ((DX + DY == 2) || (DX == 0 && DY == 0)) { C = P; }
						else if (TX == 0 || TY == 0) { C = WashHi; }
					}
				}
				else
				{
					C = P;
					if (TX == 5 || TY == 5) { C = Dk(P, 0.4f); }
					else if (TX == 0 || TY == 0) { C = Lt(P, 0.3f); }
					else if (TX == 2 && TY == 2) { C = Lt(P, 0.55f); }
					else if ((TX + TY) == 6) { C = Dk(P, 0.15f); }
				}
				if (X > 24) { C = Dk(C, 0.18f); }
				Img.Set(X, Y, C);
			}
		}
		// middle belt and the firebox door
		Box(Img, 1, 26, 28, 2, St == 0 ? Lt(P, 0.1f) : S, 0.35f, 0.3f);
		Box(Img, 9, 35, 12, 9, S, 0.35f, 0.4f);
		Inset(Img, 11, 37, 8, 5, Dk(S, 0.2f));
		Img.Set(17, 39, Lt(S, 0.5f)); Img.Set(18, 39, Lt(S, 0.3f));
		for (int32 X = 12; X < 18; X += 2) { Img.Set(X, 38, Dk(S, 0.45f)); }
		// plinth
		Box(Img, 0, 46, 30, 6, Dk(P, 0.1f), 0.3f, 0.4f);
		if (St == 1) { HLine(Img, 1, 28, 48, S); }
		Img.Outline(PropOut);
		return Img;
	}

	/** Canopy bed 32x48 (3/4 top view, head at the top): 0 bordeaux + gold fringe · 1 royal blue with a crown. */
	FFVImage BedWorld(int32 St, FColor P, FColor S)
	{
		FFVImage Img(32, 48);
		const FColor FrameW = St == 0 ? DWood : Rgb(236, 226, 206), FrameWLo = Dk(FrameW, 0.3f);
		// bed frame and the blanket
		Box(Img, 2, 12, 28, 34, FrameW, 0.25f, 0.4f);
		Rect(Img, 4, 14, 24, 28, Wash);
		for (int32 PX : { 6, 17 }) { Box(Img, PX, 15, 9, 5, WashHi, 0.1f, 0.25f); Img.Set(PX + 1, 16, Rgb(255, 255, 255)); }
		for (int32 Y = 21; Y < 42; ++Y)
		{
			for (int32 X = 4; X < 28; ++X)
			{
				FColor C = P;
				if (Y == 21) { C = Lt(P, 0.35f); }
				else if (X < 6) { C = Lt(P, 0.15f); }
				else if (X > 25) { C = Dk(P, 0.25f); }
				if (((X + Y) % 6 == 0) && Y > 23) { C = Dk(C, 0.12f); }
				if (Y == 23 || Y == 39) { C = S; }
				Img.Set(X, Y, C);
			}
		}
		HLine(Img, 4, 27, 22, WashHi);
		Box(Img, 3, 42, 26, 4, FrameW, 0.3f, 0.4f);
		// the canopy: posts and a drape at the head
		for (int32 PX : { 1, 29 }) { Column(Img, PX, 2, 2, 44, FrameWLo); VLine(Img, PX, 2, 45, FrameW); }
		Rect(Img, 2, 1, 28, 11, Dk(P, 0.15f));
		for (int32 X = 2; X < 30; ++X)
		{
			const bool bFold = (X % 4) == 0;
			VLine(Img, X, 2, 10, bFold ? Dk(P, 0.35f) : ((X % 4) == 1 ? Lt(P, 0.2f) : Dk(P, 0.12f)));
			Img.Set(X, 11 + (X % 2), (X % 2) ? S : Dk(S, 0.2f));   // fringe
		}
		HLine(Img, 1, 30, 1, S); HLine(Img, 1, 30, 0, Lt(S, 0.3f));
		// side drapes tied back
		for (int32 Y = 12; Y < 24; ++Y) { Img.Set(3, Y, P); Img.Set(28, Y, Dk(P, 0.3f)); }
		Img.Set(3, 18, S); Img.Set(28, 18, S);
		if (St == 1)
		{
			// crown above the canopy
			Rect(Img, 12, 0, 8, 2, S);
			Img.Set(12, 0, GoldHi); Img.Set(15, 0, GoldHi); Img.Set(19, 0, GoldHi);
			Img.Set(16, 1, Rgb(220, 60, 70));
			HLine(Img, 4, 27, 30, Lt(S, 0.2f));
		}
		Img.Outline(PropOut);
		return Img;
	}

	// ==== DISPATCH ====
	bool IsU6Slot(EFVDecorSlot Slot) { return uint8(Slot) >= FVDecor::ClassicSlots && Slot < EFVDecorSlot::Count; }

	int32 FrameCount(EFVDecorSlot Slot, int32 St)
	{
		switch (Slot)
		{
		case EFVDecorSlot::YardLantern: return St == 2 ? 2 : 1;
		case EFVDecorSlot::YardCenter: return St == 1 ? 3 : 1;   // the fountain splashes
		case EFVDecorSlot::Clock:      return 4;
		case EFVDecorSlot::Fireplace:  return 3;
		case EFVDecorSlot::Statue:     return St == 1 ? 3 : 1;
		default:                       return 1;
		}
	}

	FFVImage Placeholder(int32 W, int32 H)
	{
		FFVImage Img(W, H);
		Box(Img, 1, 1, W - 2, H - 2, Rgb(200, 60, 200));
		Img.Outline(PropOut);
		return Img;
	}

	FFVImage World(const FFVDecorDef& Def, int32 Frame)
	{
		FColor P, S;
		Colors(Def, P, S);
		const int32 St = StyleOf(Def);
		const int32 F = FMath::Clamp(Frame, 0, FrameCount(Def.Slot, St) - 1);
		switch (Def.Slot)
		{
		case EFVDecorSlot::YardRoof:      return RoofOverlay(St, P, S);
		case EFVDecorSlot::YardWalls:     return WallsOverlay(St, P, S);
		case EFVDecorSlot::YardShutters:  return ShuttersOverlay(St, P, S);
		case EFVDecorSlot::YardFence:     return FenceSection(St, P, S);
		case EFVDecorSlot::YardPath:      return PathTile(St, P, S);
		case EFVDecorSlot::YardLantern:   return LanternWorld(St, P, S, F);
		case EFVDecorSlot::YardMailbox:   return MailboxWorld(St, P, S);
		case EFVDecorSlot::YardBench:     return BenchWorld(St, P, S);
		case EFVDecorSlot::YardFlowerbed: return FlowerbedWorld(St, P, S);
		case EFVDecorSlot::YardScarecrow: return ScarecrowWorld(St, P, S);
		case EFVDecorSlot::YardWell:      return WellWorld(St, P, S);
		case EFVDecorSlot::YardVane:      return VaneWorld(St, P, S);
		case EFVDecorSlot::YardCenter:    return CenterWorld(St, P, S, F);
		case EFVDecorSlot::Cupboard:      return CupboardWorld(St, P, S);
		case EFVDecorSlot::Bookcase:      return BookcaseWorld(St, P, S);
		case EFVDecorSlot::Clock:         return ClockWorld(St, P, S, F);
		case EFVDecorSlot::Fireplace:     return FireplaceWorld(St, P, S, F);
		case EFVDecorSlot::Chandelier:    return ChandelierWorld(St, P, S);
		case EFVDecorSlot::Statue:        return StatueWorld(St, P, S, F);
		case EFVDecorSlot::StoveSkin:     return StoveWorld(St, P, S);
		case EFVDecorSlot::BedSkin:       return BedWorld(St, P, S);
		default: break;
		}
		return Placeholder(20, 20);
	}

	FFVImage Icon(const FFVDecorDef& Def)
	{
		FColor P, S;
		Colors(Def, P, S);
		const int32 St = StyleOf(Def);
		switch (Def.Slot)
		{
		case EFVDecorSlot::YardRoof:      return RoofIcon(St, P, S);
		case EFVDecorSlot::YardWalls:     return WallsIcon(St, P, S);
		case EFVDecorSlot::YardShutters:  return ShuttersIcon(St, P, S);
		case EFVDecorSlot::YardFence:     return FenceIcon(St, P, S);
		case EFVDecorSlot::YardPath:      return PathIcon(St, P, S);
		default: break;
		}
		// the other slots: a careful downscale of the world sprite (frame 0)
		return IconFrom(World(Def, 0));
	}

	FVector2D Light(const FFVDecorDef& Def)
	{
		const int32 St = StyleOf(Def);
		switch (Def.Slot)
		{
		case EFVDecorSlot::YardLantern: return LanternLight(St);
				case EFVDecorSlot::Fireplace:   return FVector2D(20.f, 28.f);
		case EFVDecorSlot::Chandelier:  return FVector2D(16.f, 10.f);
		case EFVDecorSlot::Statue:      if (St == 0) { break; } return FVector2D(12.f, 24.f);
		default: break;
		}
		return FVector2D(-1.f, -1.f);
	}
}

// ==================================================================================================================
// Contract
// ==================================================================================================================
FFVImage FVArtDecorU6::BuildIcon(const FFVDecorDef& Def)
{
	if (!FVArtDecorU6Impl::IsU6Slot(Def.Slot))
	{
		return FFVImage();   // classic slots: FVArtHome.cpp draws them
	}
	return FVArtDecorU6Impl::Icon(Def);
}

FFVImage FVArtDecorU6::BuildWorld(const FFVDecorDef& Def)
{
	return BuildWorldFrame(Def, 0);
}

int32 FVArtDecorU6::WorldFrames(const FFVDecorDef& Def)
{
	return FVArtDecorU6Impl::IsU6Slot(Def.Slot) ? FVArtDecorU6Impl::FrameCount(Def.Slot, FVArtDecorU6Impl::StyleOf(Def)) : 1;
}

FFVImage FVArtDecorU6::BuildWorldFrame(const FFVDecorDef& Def, int32 Frame)
{
	return FVArtDecorU6Impl::IsU6Slot(Def.Slot) ? FVArtDecorU6Impl::World(Def, Frame) : FFVImage();
}

FVector2D FVArtDecorU6::LightPoint(const FFVDecorDef& Def)
{
	return FVArtDecorU6Impl::IsU6Slot(Def.Slot) ? FVArtDecorU6Impl::Light(Def) : FVector2D(-1.f, -1.f);
}
