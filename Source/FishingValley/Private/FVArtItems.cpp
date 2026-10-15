// Item art: fish (hand-made ASCII body templates, procedurally colored/shaded/patterned), junk, bait,
// rod icons and the treasure chest; update 2: generic items (resources, materials, food, tackle, nets),
// crafted lures, shovels and the ItemIcon resolver; update 5: a silhouette per legend (LegendTemplate), the Ember bait,
// fried-fish plates by rarity and the rarity fish-token icons. All images are generated once and cached by UFVSprites.

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVArtU6.h"           // update 6: side views of the new fish shapes (ART-FISH-SIDE)

// update 6 [ART-ITEMS]: the new styles are drawn in FVArtItemsU6.cpp (the default branches below forward there)
namespace FVArtItemsU6
{
	FFVImage BuildItemArt(EFVItemArt Art, FColor Tint, uint8 Tier);
	FFVImage BuildBait(EFVBaitStyle Style);
	FFVImage BuildJunk(EFVJunkStyle Style);
	FFVImage BuildRodIcon(const FFVRodDef& Rod);
}

namespace
{
	// ----------------------------------------------------------------------------------------
	// Fish templates. Facing RIGHT (head on the right). A 1px outline is added automatically,
	// so the final sprite is (template W + 2) x (template H + 2).
	//   B body (auto-shaded: dark back -> base -> lighter flank)   b belly
	//   h body highlight   d body dark (gill line, scales, segments)
	//   F fin   f fin shade   Y fin highlight   G gold (crown tips)
	//   E pupil   W eye highlight   w eye white
	//   o internal outline (mouth)   x thin detail drawn after the outline (whiskers, spikes, antennae, legs)
	//   L glowing detail: lerp(PatternColor, white, 0.5) (update 5: the angler's lantern, flame tips; not a body char)
	// ----------------------------------------------------------------------------------------
	struct FFishTemplate
	{
		const TCHAR* const* Rows = nullptr;
		int32 NumRows = 0;
		int32 PatX0 = 0;     // pattern (stripes/spots) column range
		int32 PatX1 = 0;
		bool bScales = false;
		bool bFinRim = true; // Glow pattern: light rim also along the upper edge of fins (off for the Zhar-ryba's red flames)
	};

	const FFishTemplate& FishTemplate(EFVFishShape Shape)
	{
		static const TCHAR* MinnowRows[] = {
			TEXT(".....FF..."),
			TEXT("F..BBBBBB."),
			TEXT("FFBBBBBWEB"),
			TEXT("FFBbbbbbbB"),
			TEXT("F..bbbbbb."),
			TEXT(".....F...."),
		};
		static const FFishTemplate Minnow = { MinnowRows, UE_ARRAY_COUNT(MinnowRows), 2, 7, false };

		static const TCHAR* PerchRows[] = {
			TEXT("....f.f.f....."),
			TEXT("...fFfFfFf...."),
			TEXT("F.BBBBBBBBBBB."),
			TEXT("FFBBBBBBBBdWEB"),
			TEXT("FFBBBBBBBBdEEB"),
			TEXT("FFbbbbbbbbdbbo"),
			TEXT("F..bbbbbbbbbb."),
			TEXT("....FF...FF..."),
		};
		static const FFishTemplate Perch = { PerchRows, UE_ARRAY_COUNT(PerchRows), 3, 10, false };

		static const TCHAR* CarpRows[] = {
			TEXT("......fFFf......"),
			TEXT(".....fBBBBBB...."),
			TEXT("F..BBBBBBBBBBB.."),
			TEXT("FF.BBBBBBBBBBBB."),
			TEXT("FFBBBBBBBBBBdWEB"),
			TEXT("FFBBBBBBBBBBdEEB"),
			TEXT("FF.bbbbbbbbbdbbo"),
			TEXT("F..bbbbbbbbbbbb."),
			TEXT(".....bbbbbbbb..."),
			TEXT("......FF.FF....."),
		};
		static const FFishTemplate Carp = { CarpRows, UE_ARRAY_COUNT(CarpRows), 3, 11, true };

		static const TCHAR* PikeRows[] = {
			TEXT("....fF................"),
			TEXT("F..BBBBBBBBBBBBBBBB..."),
			TEXT("FFBBBBBBBBBBBBBBBWEBB."),
			TEXT("FFBBBBBBBBBBBBBBBBBBBB"),
			TEXT("FF.bbbbbbbbbbbbbboooo."),
			TEXT("F...bbbbbbbbbbbbbbbb.."),
			TEXT(".....fF.....fF........"),
		};
		static const FFishTemplate Pike = { PikeRows, UE_ARRAY_COUNT(PikeRows), 3, 15, false };

		static const TCHAR* EelRows[] = {
			TEXT(".....FFFF............."),
			TEXT("...FFBBBBFF......BBBB."),
			TEXT(".FFBBBBBBBBF...FBBBBWE"),
			TEXT("FBBBbbbbbbBBFFFBBBBBBB"),
			TEXT(".FFbbb...bbBBBBBbbbbbo"),
			TEXT("...FF.....FbbbbbbbbF.."),
			TEXT("...........FFFFFFFF..."),
		};
		static const FFishTemplate Eel = { EelRows, UE_ARRAY_COUNT(EelRows), 3, 16, false };

		static const TCHAR* FlounderRows[] = {
			TEXT(".....fFfFfFfF..."),
			TEXT("...fFBBBBBBBBBF."),
			TEXT("..fBBBBBBBBWEBBF"),
			TEXT("F.fBBBBBBBBBBWEf"),
			TEXT("FFFBBBBBBBBBBBBF"),
			TEXT("FFFBBBBBBBBBBBBo"),
			TEXT("F.fBBBBBBBBBBBBf"),
			TEXT("..fBBBBBBBBBBBF."),
			TEXT("...fFBBBBBBBBF.."),
			TEXT(".....FfFfFfFf..."),
		};
		static const FFishTemplate Flounder = { FlounderRows, UE_ARRAY_COUNT(FlounderRows), 2, 13, false };

		static const TCHAR* CatfishRows[] = {
			TEXT(".......fF........."),
			TEXT("F....fBBBBBBBB...."),
			TEXT("FF..BBBBBBBBBBB..x"),
			TEXT("FFFBBBBBBBBBBWEBx."),
			TEXT("FFBBBBBBBBBBBBBBB."),
			TEXT("FF.bbbbbbbbbbbbboo"),
			TEXT("F...bbbbbbbbbbbb.x"),
			TEXT("......FF...ff.x..x"),
			TEXT(".............x..x."),
		};
		static const FFishTemplate Catfish = { CatfishRows, UE_ARRAY_COUNT(CatfishRows), 3, 12, false };

		static const TCHAR* PufferRows[] = {
			TEXT("......x....."),
			TEXT("..x...x...x."),
			TEXT("...x.BBB.x.."),
			TEXT("...BBBBBBBB."),
			TEXT("F.BBBBBBBWEB"),
			TEXT("FFBBBBBBBEEB"),
			TEXT("FFBBBBBBBBBo"),
			TEXT("F.bbbbbbbbbb"),
			TEXT("...bbbbbbbb."),
			TEXT("...x.bbb.x.."),
			TEXT("..x...x...x."),
			TEXT("......x....."),
		};
		static const FFishTemplate Puffer = { PufferRows, UE_ARRAY_COUNT(PufferRows), 1, 10, false };

		static const TCHAR* TunaRows[] = {
			TEXT("FF.........F.........."),
			TEXT(".FF.......FFF........."),
			TEXT("..FF...BBBBBBBBB......"),
			TEXT("...FFBBBBBBBBBBBBBB..."),
			TEXT("....BBBBBBBBBBBBBBBBB."),
			TEXT("...BBBBBBBBBBBBBBBBWEB"),
			TEXT("...BBBBBBBBBBBBBBBBEEB"),
			TEXT("....bbbbbbbbbbbbbbbbbo"),
			TEXT("...FFbbbbbbbbbbbbbbbb."),
			TEXT("..FF...bbbbbbbbbbbb..."),
			TEXT(".FF.......FFF........."),
			TEXT("FF.........F.........."),
		};
		static const FFishTemplate Tuna = { TunaRows, UE_ARRAY_COUNT(TunaRows), 5, 16, false };

		static const TCHAR* SquidRows[] = {
			TEXT("..............FFF..."),
			TEXT(".........BBBBBBBFFF."),
			TEXT("dd.dd..BBBBBBBBBBBBF"),
			TEXT("..d..ddBBBBBBBBBBBBF"),
			TEXT("ddddddBWEBBBBBBBBBBB"),
			TEXT("..d..ddEEbbbbbbbbbbF"),
			TEXT("dd.dd..bbbbbbbbbbbbF"),
			TEXT(".........bbbbbbbFFF."),
			TEXT("..............FFF..."),
		};
		static const FFishTemplate Squid = { SquidRows, UE_ARRAY_COUNT(SquidRows), 8, 16, false };

		static const TCHAR* CrayfishRows[] = {
			TEXT("..........xx......"),
			TEXT("............x.BBB."),
			TEXT("..BdBBdBBBB..BBBBB"),
			TEXT(".BBdBBdBBBBBBBB..."),
			TEXT("FBBdBBdBBBBWEBBBBB"),
			TEXT("FbbdbbdbbbbbbbBBB."),
			TEXT("FF.x.x.x.x........"),
			TEXT("F.x.x.x.x........."),
		};
		static const FFishTemplate Crayfish = { CrayfishRows, UE_ARRAY_COUNT(CrayfishRows), 1, 10, false };

		static const TCHAR* LegendRows[] = {
			TEXT("...........G...G...G.........."),
			TEXT("...........F...F...F.........."),
			TEXT("..........FYF.FYF.FYF........."),
			TEXT("..........FfFFFfFFFfF........."),
			TEXT("YF........FFFFFFFFFFFF........"),
			TEXT(".fYF....BBBBBBBBBBBBBBB......."),
			TEXT("..fFF..BBBBBBBBBBBBBBBBBB....."),
			TEXT("...fFFBBBBBBBBBBBBBBBBBdBBB..."),
			TEXT("....fFBBBBBBBBBBBBBBBBBdBwWEB."),
			TEXT(".....FBBBBBBBBBBBBBBBBBdBwEEBB"),
			TEXT("....fFBBBBBBBBBBBBBBBBBdBBBBBo"),
			TEXT("...fFFbbbbbbbbbbbbbbbbbdbbbbo."),
			TEXT("..fFF.bbbbbbbbbbbbbbbbbbbbbb.."),
			TEXT(".fYF...bbbbbbbbbbbbbbbbbbb...."),
			TEXT("YF.......bbbbbbbbbbbbbbbb....."),
			TEXT("..........FFf....FFf.........."),
			TEXT("...........Ff.....Ff.........."),
		};
		static const FFishTemplate Legend = { LegendRows, UE_ARRAY_COUNT(LegendRows), 7, 22, true };

		switch (Shape)
		{
		case EFVFishShape::Minnow: return Minnow;
		case EFVFishShape::Perch: return Perch;
		case EFVFishShape::Carp: return Carp;
		case EFVFishShape::Pike: return Pike;
		case EFVFishShape::Eel: return Eel;
		case EFVFishShape::Flounder: return Flounder;
		case EFVFishShape::Catfish: return Catfish;
		case EFVFishShape::Puffer: return Puffer;
		case EFVFishShape::Tuna: return Tuna;
		case EFVFishShape::Squid: return Squid;
		case EFVFishShape::Crayfish: return Crayfish;
		case EFVFishShape::Legend: return Legend;
		default: break;
		}
		return Perch;
	}

	/** Update 5 (§4): each legend has its own silhouette, chosen by FFVFishDef::Legend (Shape stays Legend, so the
	    black-spoon shape mask keeps working). At most 18 rows (the held-fish card) and 40 columns. The Tsar carp keeps
	    the original Legend template. Returns nullptr for EFVLegend::None. */
	const FFishTemplate* LegendTemplate(EFVLegend Legend)
	{
		// Sturgeon 38x11: long low body, snout 5 texels ahead of the eye, 4 barbels, 6 bony scutes, heterocercal tail
		static const TCHAR* SturgeonRows[] = {
			TEXT("YF...................................."),
			TEXT(".YFF.................................."),
			TEXT("..fFF..fF.h...h...h...h...h...h......."),
			TEXT("...fFFBBBBdBBBdBBBdBBBdBBBdBBBdB......"),
			TEXT("....fFBBBBBBBBBBBBBBBBBBBBBdBBBWEB...."),
			TEXT("....fFBBBBBBdBBBdBBBdBBBdBBdBBBBBBBB.."),
			TEXT("....fFBBBBBBBBBBBBBBBBBBBBBdBBBBBBBBBB"),
			TEXT("....fFbbbbbbbbbbbbbbbbbbbbbbboobbbb..."),
			TEXT("...fF....bbbbbbbbbbbbbbbbbbbb..x.x.xx."),
			TEXT("...........Ff..FFf......FFf.....x.x..."),
			TEXT(".........................Ff..........."),
		};
		static const FFishTemplate Sturgeon = { SturgeonRows, UE_ARRAY_COUNT(SturgeonRows), 8, 30, false };

		// Crystal trout 28x12: slender, diamond facets, three-spike dorsal fin + adipose fin, sharp forked tail
		static const TCHAR* CrystalRows[] = {
			TEXT("Y............Y..Y..Y........"),
			TEXT("FY..........FY.FY.FY........"),
			TEXT(".FY....F....FFFFFFFF........"),
			TEXT("..FF...BBdBBBBBdBBBBBBB....."),
			TEXT("...fFBBBdhdBBBdhdBBBBdBBB..."),
			TEXT("...fFBBdhhBdBdhhBdBBBdBWEBB."),
			TEXT("...fFBdBhBBBdBhBBBdBBdBBBBBB"),
			TEXT("...fFbbdbbbdbdbbbdbbbdbbbbo."),
			TEXT("..FF..bbdbdbbbdbdbbbbbbbb..."),
			TEXT(".FY......dbbbbbdbbbbbb......"),
			TEXT("FY.........FF.....FF........"),
			TEXT("Y...........f......f........"),
		};
		static const FFishTemplate Crystal = { CrystalRows, UE_ARRAY_COUNT(CrystalRows), 6, 20, false };

		// Angler 26x17: huge head, jutting lower jaw with fangs, rod (x) arcing over the mouth to a 2x2 lantern (L)
		static const TCHAR* AnglerRows[] = {
			TEXT("................xxxx......"),
			TEXT("...............x....xx...."),
			TEXT("..............x.......x..."),
			TEXT(".............x........LL.."),
			TEXT("..........fF.x........LL.."),
			TEXT("...........BBBBBBB........"),
			TEXT("........BBBBBBBBBBBB......"),
			TEXT("Y.....BBBBBBBBBBBwEBBB...."),
			TEXT("FF...BBBBBBBBBBBBBBBBBB..."),
			TEXT("fFF.BBBBBBBBBBBowowowoo..."),
			TEXT(".fFBBBBBBBBBBoowowowowowo."),
			TEXT(".fFBBBBBFFBBdddddddddddddd"),
			TEXT("fFF.bbbbFfbbbbbbbbbbbbbbbb"),
			TEXT("FF...bbbbbbbbbbbbbbbbbbb.."),
			TEXT("Y......bbbbbbbbbbbbbb....."),
			TEXT("..........bbbbbbb........."),
			TEXT("...........FFf..Ff........"),
		};
		static const FFishTemplate Angler = { AnglerRows, UE_ARRAY_COUNT(AnglerRows), 4, 20, false };

		// Fog serpent 40x12: S-shaped body 3 texels thick, solid crest, dragon head with two long whiskers, tail tassel
		static const TCHAR* FogRows[] = {
			TEXT("...............................Y.Y.....x"),
			TEXT("....................FF.........FYFY....x"),
			TEXT(".Y................FFBBFF......FBBBBF..x."),
			TEXT("YFY.F............FBBBBBBF.....BBBBwEBBx."),
			TEXT("FFffBF..........FBBBbbBBBF....BBBBBBBBBB"),
			TEXT("YFf.BBFF......FFBBbb..bbBBFF.FBBBBBBBBBh"),
			TEXT(".Y..bBBBFF...FBBBb......bBBBFBbbbboooob."),
			TEXT(".....bBBBBFFFBBBb........bBBBBbbbbbbbbx."),
			TEXT("......bbBBBBBBbb..........bbBb.........x"),
			TEXT("........bbBBBb..............b..........x"),
			TEXT("..........bbb.........................x."),
			TEXT("....................................xx.."),
		};
		static const FFishTemplate Fog = { FogRows, UE_ARRAY_COUNT(FogRows), 4, 30, false };

		// Zhar-ryba 31x18: tall flame crest (G core, L white-hot tips), tail of three curls, wing-like pectoral fin
		static const TCHAR* ZharRows[] = {
			TEXT("..................L............"),
			TEXT(".GGG..........L...LL..........."),
			TEXT("LFFFGG........LL..FF..L........"),
			TEXT(".L..FFG......L.FF.FGF.LL......."),
			TEXT("......FG.....LLFGFFGGFFF......."),
			TEXT(".......FG....FFFGFFGGFFGF......"),
			TEXT(".L......FG...FGFBBBBBBBBBB....."),
			TEXT("L........FGG.BBBBBBBBBBBdBBBB.."),
			TEXT(".GG.......FFBBBBBBBBBBBBdBwEBB."),
			TEXT(".FFGGGGGGGGBBBBBBBBBBBBBdBwEBBB"),
			TEXT("...FFFFFFFFBBBBBBBBBBBBBdBBBBBo"),
			TEXT("............bbbbbbbbbbbbdbbbbb."),
			TEXT("..........GGFbbbbbbbFFFbbbbbb.."),
			TEXT(".........GFF....bbFGGFbbbb....."),
			TEXT("..L....GGF......FGGFf.........."),
			TEXT(".L....GFF......LFGFf..........."),
			TEXT("..GGGGF.........LfL............"),
			TEXT("..FFFF........................."),
		};
		static const FFishTemplate Zhar = { ZharRows, UE_ARRAY_COUNT(ZharRows), 13, 28, false, false };

		switch (Legend)
		{
		case EFVLegend::TsarCarp: return &FishTemplate(EFVFishShape::Legend);
		case EFVLegend::Sturgeon: return &Sturgeon;
		case EFVLegend::CrystalTrout: return &Crystal;
		case EFVLegend::Angler: return &Angler;
		case EFVLegend::FogSerpent: return &Fog;
		case EFVLegend::ZharRyba: return &Zhar;
		default: break;
		}
		return nullptr;
	}

	bool IsBodyChar(TCHAR C)
	{
		return C == TEXT('B') || C == TEXT('b') || C == TEXT('h') || C == TEXT('d') || C == TEXT('E') || C == TEXT('W') || C == TEXT('w');
	}

	/** Deterministic seed from a fish id (stable across runs). */
	uint32 SeedOf(const FFVFishDef& Def)
	{
		const FString S = Def.Id.ToString();
		uint32 H = 2166136261u;
		for (int32 I = 0; I < S.Len(); ++I)
		{
			H = (H ^ uint32(S[I])) * 16777619u;
		}
		return H;
	}

	/** bOutline = false leaves out the 1px outline ring (x details are still drawn): the trophy plaque downscales that. */
	FFVImage RenderFish(const FFVFishDef& Def, bool bSilhouette, bool bOutline = true)
	{
		const FFishTemplate* LegendT = LegendTemplate(Def.Legend);
		const FFishTemplate& T = LegendT ? *LegendT : FishTemplate(Def.Shape);
		TArray<FString> Rows;
		int32 TW = 0;
		for (int32 I = 0; I < T.NumRows; ++I)
		{
			Rows.Add(FString(T.Rows[I]));
			TW = FMath::Max(TW, Rows.Last().Len());
		}
		const int32 TH = Rows.Num();
		auto At = [&](int32 X, int32 Y) -> TCHAR
		{
			if (Y < 0 || Y >= TH || X < 0 || X >= Rows[Y].Len()) { return TEXT('.'); }
			return Rows[Y][X];
		};

		// Column extents of the body (for automatic back/belly shading).
		TArray<int32> ColTop, ColBot;
		ColTop.Init(-1, TW);
		ColBot.Init(-1, TW);
		for (int32 X = 0; X < TW; ++X)
		{
			for (int32 Y = 0; Y < TH; ++Y)
			{
				if (IsBodyChar(At(X, Y)))
				{
					if (ColTop[X] < 0) { ColTop[X] = Y; }
					ColBot[X] = Y;
				}
			}
		}

		const FColor Body = Def.BodyColor;
		const FColor Belly = Def.BellyColor;
		const FColor Fin = Def.FinColor;
		const FColor Pat = Def.PatternColor;
		const bool bGlow = Def.Pattern == EFVFishPattern::Glow && !bSilhouette;
		FColor OutlineC = FVArt::OutlineOf(Body);
		if (bGlow)
		{
			OutlineC = FVArt::Mix(OutlineC, Pat, 0.35f);
		}
		const FColor SilFill(62, 70, 96), SilLine(38, 44, 64);
		const FColor Detail = bSilhouette ? SilLine : FVArt::Mix(OutlineC, Body, 0.25f);
		const uint32 Seed = SeedOf(Def);
		const int32 Period = TW >= 18 ? 4 : 3;
		const int32 StripeW = TW >= 20 ? 2 : 1;

		FFVImage Img(TW + 2, TH + 2);
		TArray<uint8> Spot;
		Spot.Init(0, TW * TH);

		for (int32 Y = 0; Y < TH; ++Y)
		{
			for (int32 X = 0; X < TW; ++X)
			{
				const TCHAR Ch = At(X, Y);
				if (Ch == TEXT('.') || Ch == TEXT(' ') || Ch == TEXT('x'))
				{
					continue;
				}
				if (bSilhouette)
				{
					Img.Set(X + 1, Y + 1, SilFill);
					continue;
				}
				const int32 Top = ColTop[X], Bot = ColBot[X];
				const float Tt = (Top >= 0 && Bot > Top) ? float(Y - Top) / float(Bot - Top) : 0.5f;
				FColor C(255, 0, 255);
				switch (Ch)
				{
				case TEXT('B'):
					if (Y == Top && Bot - Top >= 2) { C = FVArt::Shade(Body, 0.34f); }
					else if (Tt < 0.34f) { C = FVArt::Shade(Body, 0.14f); }
					else if (Tt < 0.62f) { C = Body; }
					else { C = FVArt::Mix(Body, Belly, 0.22f); }
					if (T.bScales && Y != Top && ((X + (Y % 2) * 2) % 4 == 0) && (Y % 2 == 1))
					{
						C = FVArt::Shade(C, 0.16f);
					}
					break;
				case TEXT('b'):
					C = (Y == Bot && Bot - Top >= 3) ? FVArt::Shade(Belly, 0.16f) : Belly;
					break;
				case TEXT('h'): C = FVArt::Light(Body, 0.4f); break;
				case TEXT('d'): C = FVArt::Shade(Body, 0.4f); break;
				case TEXT('F'): C = Fin; break;
				case TEXT('f'): C = FVArt::Shade(Fin, 0.36f); break;
				case TEXT('Y'): C = FVArt::Light(Fin, 0.45f); break;
				case TEXT('G'): C = FColor(255, 216, 84); break;
				case TEXT('L'): C = FVArt::Mix(Pat, FColor::White, 0.5f); break;
				case TEXT('E'): C = FColor(30, 22, 44); break;
				case TEXT('W'): C = FColor(255, 255, 255); break;
				case TEXT('w'): C = FColor(238, 234, 222); break;
				case TEXT('o'): C = OutlineC; break;
				default: C = Body; break;
				}

				// Patterns (only on the flank, never on eyes/fins).
				const bool bPatArea = X >= T.PatX0 && X <= T.PatX1 && (Ch == TEXT('B') || Ch == TEXT('b'));
				if (bPatArea)
				{
					switch (Def.Pattern)
					{
					case EFVFishPattern::Stripes:
						if (Ch == TEXT('B') && ((X - T.PatX0) % Period) < StripeW && Tt < 0.9f)
						{
							C = (Y == Top) ? FVArt::Shade(Pat, 0.25f) : Pat;
						}
						break;
					case EFVFishPattern::Spots:
					{
						const bool bNeighbour =
							(X > 0 && Spot[Y * TW + X - 1]) || (Y > 0 && Spot[(Y - 1) * TW + X]) ||
							(X > 0 && Y > 0 && Spot[(Y - 1) * TW + X - 1]) || (X + 1 < TW && Y > 0 && Spot[(Y - 1) * TW + X + 1]);
						if (!bNeighbour && FVArt::Noise(X, Y, Seed) < 0.2f && Y != Top)
						{
							Spot[Y * TW + X] = 1;
							C = Ch == TEXT('b') ? FVArt::Mix(Pat, Belly, 0.45f) : Pat;
						}
						break;
					}
					case EFVFishPattern::Glow:
						if (FVArt::Noise(X, Y, Seed ^ 0x5bd1e995u) < 0.1f)
						{
							C = FVArt::Light(Pat, 0.55f);
						}
						break;
					default:
						break;
					}
				}
				// Glow: bright rim along the upper silhouette.
				if (bGlow && (Ch == TEXT('B') || (T.bFinRim && (Ch == TEXT('F') || Ch == TEXT('f')))))
				{
					const bool bEdge = !IsBodyChar(At(X, Y - 1)) && At(X, Y - 1) != TEXT('F') && At(X, Y - 1) != TEXT('f');
					if (bEdge)
					{
						C = FVArt::Light(Pat, 0.3f);
					}
				}
				Img.Set(X + 1, Y + 1, C);
			}
		}

		if (bOutline)
		{
			Img.Outline(bSilhouette ? SilLine : OutlineC);
		}

		// Thin details after the outline so they stay 1px.
		for (int32 Y = 0; Y < TH; ++Y)
		{
			for (int32 X = 0; X < TW; ++X)
			{
				if (At(X, Y) == TEXT('x'))
				{
					Img.Set(X + 1, Y + 1, Detail);
				}
			}
		}
		return Img;
	}

	// ----------------------------------------------------------------------------------------
	// Shared item palette (junk / bait / chest). Templates are 16x16 WITHOUT outline;
	// a dark outline is added automatically (keep a 1px transparent margin).
	// ----------------------------------------------------------------------------------------
	const TMap<TCHAR, FColor>& ItemPalette()
	{
		// Magic static: initialized once, thread-safe.
		static const TMap<TCHAR, FColor> Pal = []()
		{
			TMap<TCHAR, FColor> P;
			// greens
			P.Add(TEXT('g'), FColor(84, 168, 64));
			P.Add(TEXT('G'), FColor(150, 214, 92));
			P.Add(TEXT('h'), FColor(44, 110, 60));
			// browns / leather / wood
			P.Add(TEXT('m'), FColor(150, 88, 50));
			P.Add(TEXT('M'), FColor(196, 128, 72));
			P.Add(TEXT('e'), FColor(98, 56, 38));
			P.Add(TEXT('E'), FColor(70, 42, 34));
			// bark / driftwood greys
			P.Add(TEXT('k'), FColor(170, 150, 124));
			P.Add(TEXT('K'), FColor(212, 196, 168));
			P.Add(TEXT('j'), FColor(122, 104, 88));
			// metals
			P.Add(TEXT('s'), FColor(190, 196, 210));
			P.Add(TEXT('S'), FColor(238, 242, 250));
			P.Add(TEXT('x'), FColor(128, 132, 152));
			P.Add(TEXT('X'), FColor(88, 90, 112));
			// rust
			P.Add(TEXT('r'), FColor(178, 90, 50));
			P.Add(TEXT('R'), FColor(214, 130, 70));
			P.Add(TEXT('q'), FColor(122, 60, 40));
			// glass
			P.Add(TEXT('t'), FColor(80, 170, 110));
			P.Add(TEXT('T'), FColor(170, 230, 180));
			P.Add(TEXT('u'), FColor(44, 116, 84));
			// parchment
			P.Add(TEXT('c'), FColor(249, 226, 172));
			P.Add(TEXT('C'), FColor(222, 180, 118));
			// worm pinks
			P.Add(TEXT('i'), FColor(236, 128, 142));
			P.Add(TEXT('I'), FColor(255, 184, 186));
			P.Add(TEXT('v'), FColor(178, 74, 104));
			// blood-worm reds
			P.Add(TEXT('a'), FColor(212, 42, 52));
			P.Add(TEXT('A'), FColor(250, 110, 100));
			P.Add(TEXT('z'), FColor(130, 20, 44));
			// gold
			P.Add(TEXT('y'), FColor(255, 204, 51));
			P.Add(TEXT('Y'), FColor(255, 242, 150));
			P.Add(TEXT('d'), FColor(214, 138, 30));
			// magic
			P.Add(TEXT('p'), FColor(170, 90, 236));
			P.Add(TEXT('P'), FColor(226, 168, 255));
			P.Add(TEXT('n'), FColor(98, 44, 170));
			P.Add(TEXT('l'), FColor(190, 250, 255));
			// misc
			P.Add(TEXT('w'), FColor(255, 255, 255));
			P.Add(TEXT('W'), FColor(236, 232, 220));
			P.Add(TEXT('o'), FVArt::InkOutline());
			P.Add(TEXT('b'), FColor(40, 120, 200));
			P.Add(TEXT('B'), FColor(120, 190, 240));
			// update 2: nightcrawler (выползок)
			P.Add(TEXT('N'), FColor(168, 78, 92));
			P.Add(TEXT('Z'), FColor(212, 124, 128));
			P.Add(TEXT('V'), FColor(112, 48, 64));
			P.Add(TEXT('U'), FColor(236, 168, 150));
			// update 2: dough / pastry
			P.Add(TEXT('f'), FColor(230, 198, 140));
			P.Add(TEXT('F'), FColor(252, 236, 196));
			P.Add(TEXT('H'), FColor(192, 150, 98));
			// update 2: crumbs / soil
			P.Add(TEXT('D'), FColor(86, 58, 42));
			P.Add(TEXT('L'), FColor(146, 104, 68));
			P.Add(TEXT('J'), FColor(116, 80, 54));
			// update 2: orange, near-black paint
			P.Add(TEXT('O'), FColor(244, 142, 44));
			P.Add(TEXT('Q'), FColor(60, 58, 72));
			return P;
		}();
		return Pal;
	}

	FFVImage ItemAscii(std::initializer_list<const TCHAR*> Rows, bool bOutline = true)
	{
		FFVImage Img = FFVImage::FromAscii(Rows, ItemPalette());
		if (Img.W != 16 || Img.H != 16)
		{
			FFVImage Fixed(16, 16);
			Fixed.Blit(Img, (16 - Img.W) / 2, (16 - Img.H) / 2);
			Img = Fixed;
		}
		if (bOutline)
		{
			Img.Outline(FVArt::InkOutline());
		}
		return Img;
	}
}

// ============================================================================================
// Update 2 helpers (named namespace: unity-build safe)
// ============================================================================================
namespace FVArtItemsImpl
{
	/** Update 6: shared for FVArtItemsU6.cpp (the palette itself lives in the anonymous namespace above). */
	const TMap<TCHAR, FColor>& ItemPalette() { return ::ItemPalette(); }
	/** Shaded ball: lit upper-left, shaded lower-right and along the lower rim, 1px specular. No outline. */
	void Ball(FFVImage& Img, float CX, float CY, float R, FColor Base, bool bSpecular = true)
	{
		const FColor Lt = FVArt::Light(Base, 0.35f), Dk = FVArt::Shade(Base, 0.38f);
		for (int32 Y = FMath::FloorToInt(CY - R) - 1; Y <= FMath::CeilToInt(CY + R) + 1; ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - R) - 1; X <= FMath::CeilToInt(CX + R) + 1; ++X)
			{
				const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if (D > R)
				{
					continue;
				}
				const float S = (DX + DY) / FMath::Max(R, 0.5f);
				const bool bRim = D > R - 0.9f;
				FColor C = Base;
				if (S < -0.55f && !bRim) { C = Lt; }
				else if (S > 0.5f || (bRim && S > -0.1f)) { C = Dk; }
				Img.Set(X, Y, C);
			}
		}
		if (bSpecular)
		{
			Img.Set(FMath::FloorToInt(CX - R * 0.45f), FMath::FloorToInt(CY - R * 0.45f), FVArt::Light(Base, 0.85f));
		}
	}

	/** Filled ellipse (pixel centers inside). */
	void Ellipse(FFVImage& Img, float CX, float CY, float RX, float RY, FColor C)
	{
		for (int32 Y = FMath::FloorToInt(CY - RY) - 1; Y <= FMath::CeilToInt(CY + RY) + 1; ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX) - 1; X <= FMath::CeilToInt(CX + RX) + 1; ++X)
			{
				const float DX = (X + 0.5f - CX) / FMath::Max(RX, 0.1f), DY = (Y + 0.5f - CY) / FMath::Max(RY, 0.1f);
				if (DX * DX + DY * DY <= 1.f)
				{
					Img.Set(X, Y, C);
				}
			}
		}
	}

	/** 4-point twinkle (center + arms), blended so it can sit on top of an outline. */
	void Twinkle(FFVImage& Img, int32 X, int32 Y, FColor C, bool bBig = false)
	{
		Img.Blend(X, Y, C);
		const FColor Arm = FVArt::WithAlpha(C, 170);
		Img.Blend(X - 1, Y, Arm); Img.Blend(X + 1, Y, Arm); Img.Blend(X, Y - 1, Arm); Img.Blend(X, Y + 1, Arm);
		if (bBig)
		{
			const FColor Tip = FVArt::WithAlpha(C, 90);
			Img.Blend(X - 2, Y, Tip); Img.Blend(X + 2, Y, Tip); Img.Blend(X, Y - 2, Tip); Img.Blend(X, Y + 2, Tip);
		}
	}

	/** Soft wavy steam (semi-transparent white) rising from (X, Y) upwards for N pixels. */
	void Steam(FFVImage& Img, int32 X, int32 Y, int32 N, int32 Phase)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const int32 SX = X + (((I + Phase) / 2) % 2 == 0 ? 0 : 1);
			Img.Blend(SX, Y - I, FColor(255, 255, 255, uint8(FMath::Clamp(170 - I * 30, 50, 200))));
		}
	}

	/** Lure paint colors: [color][0 base, 1 light, 2 dark]. */
	FColor LureColor(uint8 Color, int32 Tone)
	{
		static const FColor Cols[4][3] = {
			{ FColor(212, 42, 52), FColor(250, 112, 100), FColor(130, 20, 44) },   // red
			{ FColor(255, 204, 51), FColor(255, 242, 150), FColor(206, 132, 30) }, // yellow
			{ FColor(84, 168, 64), FColor(150, 214, 92), FColor(40, 104, 56) },    // green
			{ FColor(58, 54, 70), FColor(108, 104, 126), FColor(30, 28, 40) },     // black
		};
		return Cols[FMath::Clamp(int32(Color), 0, 3)][FMath::Clamp(Tone, 0, 2)];
	}

	/** Metal of a glitter level: [0 base, 1 light, 2 dark]; silver for 0/1, gold for 2. */
	FColor LureMetal(uint8 Glitter, int32 Tone)
	{
		static const FColor Silver[3] = { FColor(190, 196, 210), FColor(244, 246, 252), FColor(118, 122, 146) };
		static const FColor Gold[3] = { FColor(255, 204, 51), FColor(255, 244, 160), FColor(200, 124, 26) };
		return (Glitter >= 2 ? Gold : Silver)[FMath::Clamp(Tone, 0, 2)];
	}

	/** Deterministic sparkle pixels on opaque, non-outline pixels of a region. */
	void Sparkle(FFVImage& Img, int32 X0, int32 Y0, int32 X1, int32 Y1, FColor C, float Density, uint32 Seed)
	{
		const FColor Ink = FVArt::InkOutline();
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FColor P = Img.Get(X, Y);
				if (P.A == 0 || P == Ink)
				{
					continue;
				}
				if (FVArt::Noise(X, Y, Seed) < Density)
				{
					Img.Set(X, Y, C);
				}
			}
		}
	}

	// ---------------------------------------------------------------- update 5
	/** The fish without its outline ring (x details kept): TrophyPlaque (FVArtHome.cpp) downscales big legends from it. */
	FFVImage BuildFishInterior(const FFVFishDef& Def)
	{
		// §18: the mill legend has its own side view (FVArtFishSide, like UFVSprites::BuildFish) — never the Tsar-Carp
		// template; its 1px outline ring is peeled off so the plaque gets the interior as for the classic legends.
		if (Def.Legend == EFVLegend::MillMaster)
		{
			const FFVImage Src = FVArtFishSide::BuildFish(Def, false);
			if (Src.IsValid())
			{
				FFVImage Out = Src;
				for (int32 Y = 0; Y < Src.H; ++Y)
				{
					for (int32 X = 0; X < Src.W; ++X)
					{
						if (Src.Get(X, Y).A == 0) { continue; }
						const bool bEdge = Src.Get(X - 1, Y).A == 0 || Src.Get(X + 1, Y).A == 0
							|| Src.Get(X, Y - 1).A == 0 || Src.Get(X, Y + 1).A == 0;
						if (bEdge) { Out.Set(X, Y, FColor(0, 0, 0, 0)); }
					}
				}
				return Out;
			}
		}
		return RenderFish(Def, false, false);
	}

	/** sRGB rarity colours (the same values as FVDB::RarityColor). */
	FColor RarityRgb(EFVRarity R)
	{
		switch (R)
		{
		case EFVRarity::Uncommon:  return FColor(112, 204, 72);
		case EFVRarity::Rare:      return FColor(76, 150, 240);
		case EFVRarity::Epic:      return FColor(182, 98, 240);
		case EFVRarity::Legendary: return FColor(255, 198, 40);
		default:                   return FColor(226, 222, 212);
		}
	}

	/** Fried fish on a plate seen from above (§10.2): the rim is the rarity colour; Garnish 1 = parsley, 2 = + lemon,
	    3 = + cherry tomato; bGold adds sparkles (the legendary dish). */
	FFVImage FriedPlate(FColor Rim, int32 Garnish, bool bGold)
	{
		FFVImage Img(16, 16);
		const float CX = 7.5f, CY = 9.5f, RX = 7.f, RY = 4.75f;
		auto InEllipse = [](int32 X, int32 Y, float EX, float EY, float ERX, float ERY)
		{
			const float DX = (X + 0.5f - EX) / ERX, DY = (Y + 0.5f - EY) / ERY;
			return DX * DX + DY * DY <= 1.f;
		};
		const FColor Surf(250, 244, 226), SurfD(214, 206, 196);
		const FColor RimL = FVArt::Light(Rim, 0.5f), RimD = FVArt::Shade(Rim, 0.4f);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				if (!InEllipse(X, Y, CX, CY, RX, RY))
				{
					continue;
				}
				if (InEllipse(X, Y, CX, CY, RX - 1.5f, RY - 1.4f))
				{
					// the well of the plate, softly shaded along its lower-right edge
					Img.Set(X, Y, InEllipse(X + 1, Y + 1, CX, CY, RX - 1.5f, RY - 1.4f) ? Surf : SurfD);
				}
				else
				{
					const float T = (X + 0.5f - CX) / RX + (Y + 0.5f - CY) / RY;
					Img.Set(X, Y, T < -0.55f ? RimL : (T > 0.55f ? RimD : Rim));
				}
			}
		}
		// the golden-crusted fish: head left, the tail pokes over the rim
		static const TCHAR* FishRows[] = {
			TEXT("..ddMMd....."),
			TEXT(".dMYYyMMd.dd"),
			TEXT("dEyyyyyMMdMd"),
			TEXT(".dMyyyyMMd.d"),
			TEXT("..ddMMMd...."),
		};
		const TMap<TCHAR, FColor>& Pal = ItemPalette();
		for (int32 Y = 0; Y < int32(UE_ARRAY_COUNT(FishRows)); ++Y)
		{
			const TCHAR* Row = FishRows[Y];
			for (int32 X = 0; Row[X] != 0; ++X)
			{
				if (const FColor* C = Pal.Find(Row[X]))
				{
					Img.Set(2 + X, 6 + Y, *C);
				}
			}
		}
		auto Dot = [&Img, &Pal](int32 X, int32 Y, TCHAR Ch) { Img.Set(X, Y, Pal.FindRef(Ch)); };
		if (Garnish >= 1)
		{
			Dot(4, 11, TEXT('g')); Dot(5, 11, TEXT('G')); Dot(5, 12, TEXT('g')); Dot(6, 12, TEXT('h'));   // parsley
		}
		if (Garnish >= 2)
		{
			Dot(9, 11, TEXT('Y')); Dot(10, 11, TEXT('y')); Dot(9, 12, TEXT('y')); Dot(10, 12, TEXT('d'));  // lemon
		}
		if (Garnish >= 3)
		{
			Dot(11, 10, TEXT('A')); Dot(12, 10, TEXT('a')); Dot(11, 11, TEXT('a')); Dot(12, 11, TEXT('z')); // cherry tomato
		}
		Img.Outline(FVArt::InkOutline());
		if (bGold)
		{
			Twinkle(Img, 13, 3, FColor(255, 250, 200), true);
			Twinkle(Img, 2, 4, FColor(255, 255, 255));
			Img.Set(14, 13, FColor(255, 244, 170, 220));
		}
		return Img;
	}

	/** Icon of the rarity fish tokens any_fish_rare / _epic / _legend (§10.2): a small fish on a card framed in the
	    rarity colour; the legendary card twinkles. 16x16 with its own ink outline. */
	FFVImage BuildFishTokenIcon(EFVRarity Rarity)
	{
		const FColor Rar = RarityRgb(Rarity);
		const FColor Lt = FVArt::Light(Rar, 0.45f), Dk = FVArt::Shade(Rar, 0.45f), Bg = FVArt::Mix(Rar, FColor(34, 30, 52), 0.72f);
		const FColor Ink = FVArt::InkOutline();
		FFVImage Img(16, 16);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				// rounded card: 2px cut corners, ink ring, 1px rim (lit top-left, shaded bottom-right), dark tinted field
				const int32 EX = FMath::Min(X, 15 - X), EY = FMath::Min(Y, 15 - Y);
				if (EX + EY < 2)
				{
					continue;
				}
				const bool bRing = EX == 0 || EY == 0 || EX + EY == 2;
				const bool bRim = !bRing && (EX == 1 || EY == 1 || EX + EY == 3);
				Img.Set(X, Y, bRing ? Ink : (bRim ? (X + Y < 15 ? Lt : Dk) : Bg));
			}
		}
		// the same teal fish as EFVIcon::Fish (the plain any_fish token), shrunk to 12x9
		TMap<TCHAR, FColor> Pal;
		Pal.Add(TEXT('o'), Ink);
		Pal.Add(TEXT('T'), FColor(140, 224, 210));
		Pal.Add(TEXT('t'), FColor(58, 172, 170));
		Pal.Add(TEXT('u'), FColor(30, 108, 122));
		Pal.Add(TEXT('w'), FColor(255, 255, 255));
		Pal.Add(TEXT('W'), FColor(244, 236, 214));
		Pal.Add(TEXT('O'), FColor(244, 142, 44));
		const FFVImage Fish = FFVImage::FromAscii({
			TEXT(".....oo....."),
			TEXT("....oOOo...."),
			TEXT("oo.oTTTTTo.."),
			TEXT("oOoTTTTTwTo."),
			TEXT("oOOTTTTTTTTo"),
			TEXT("oOOtWWWWWWto"),
			TEXT(".oOoWWWWWuo."),
			TEXT("oOo.ooooo..."),
			TEXT("oo.........."),
		}, Pal);
		Img.Blit(Fish, 2, 3);
		if (Rarity == EFVRarity::Legendary)
		{
			Twinkle(Img, 12, 3, FColor(255, 250, 210));
		}
		return Img;
	}
}

// ============================================================================================
// Builders
// ============================================================================================
FFVImage UFVSprites::BuildFish(const FFVFishDef& Def, bool bSilhouette)
{
	// update 6: the new shapes (FlyingFish .. ExpertLegend) are drawn by FVArtFishSide.cpp
	if ((Def.Shape >= EFVFishShape::FlyingFish && Def.Shape < EFVFishShape::Count) || Def.Legend == EFVLegend::MillMaster)   // + §18 legend (MILL-ITEMS)
	{
		FFVImage U6 = FVArtFishSide::BuildFish(Def, bSilhouette);
		if (U6.IsValid())
		{
			return U6;
		}
	}
	return RenderFish(Def, bSilhouette);
}

FFVImage UFVSprites::BuildJunk(EFVJunkStyle Style)
{
	switch (Style)
	{
	case EFVJunkStyle::Weed:
		return ItemAscii({
			TEXT("................"),
			TEXT("......h........."),
			TEXT(".....hg.....h..."),
			TEXT(".....gG....hg..."),
			TEXT("....hgG....gG..."),
			TEXT("....gG....hgG.h."),
			TEXT("...hgG....gG..g."),
			TEXT("...gG....hgG.hG."),
			TEXT("...hgG...gG..gG."),
			TEXT("....gG..hgG.hg.."),
			TEXT("..h.hgG.gG..gG.."),
			TEXT("..gG.gGhgG.hg..."),
			TEXT("...gGhggG.hgG..."),
			TEXT("....hggGhhgG...."),
			TEXT(".....hhhhhh....."),
			TEXT("................"),
		});
	case EFVJunkStyle::Boot:
		return ItemAscii({
			TEXT("................"),
			TEXT("...MMMMMMM......"),
			TEXT("...eeeeeee......"),
			TEXT("...mMmmmmm......"),
			TEXT("...mMWmWmm......"),
			TEXT("...mMmWmmm......"),
			TEXT("...mMWmWmm......"),
			TEXT("...mMmWmmm......"),
			TEXT("...mMmmmmmm....."),
			TEXT("...mMmmmmmmmm..."),
			TEXT("...mMmmmmmmmmm.."),
			TEXT("...mmmmmmmemmm.."),
			TEXT("...mmmmmmeEemm.."),
			TEXT("...EEEEEEEEEEEE."),
			TEXT("...EEEE....EEEE."),
			TEXT("................"),
		});
	case EFVJunkStyle::Can:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("....XxxxxxX....."),
			TEXT("...xSSSSSSsx...."),
			TEXT("...XxxxxxxxX...."),
			TEXT("...SssrsssxX...."),
			TEXT("...Sbbbbbbbq...."),
			TEXT("...SBBwBBbbq...."),
			TEXT("...SbBBBbbrX...."),
			TEXT("...rbbbbbbbq...."),
			TEXT("...SsxxsrsxX...."),
			TEXT("...SsRrssxxX...."),
			TEXT("...Ssrqsssxq...."),
			TEXT("...XxxxxxxxX...."),
			TEXT("....XXXXXXX....."),
			TEXT("................"),
		});
	case EFVJunkStyle::Driftwood:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT(".....K.........."),
			TEXT(".....Kk........."),
			TEXT("......Kk....KKK."),
			TEXT("......kK..KKKkkj"),
			TEXT(".......KKKKkkkj."),
			TEXT(".....KKKkkkkjj.."),
			TEXT("...KKKkkkjkjj..."),
			TEXT(".CCKkkjkkkjj...."),
			TEXT("CKjCkkkjjj......"),
			TEXT("CjjCkjjj........"),
			TEXT(".CCjjj.........."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVJunkStyle::Bottle:
		return ItemAscii({
			TEXT("................"),
			TEXT("......MMM......."),
			TEXT("......mme......."),
			TEXT("......TtT......."),
			TEXT("......Ttu......."),
			TEXT(".....TTttu......"),
			TEXT("....TTtttuu....."),
			TEXT("....TtccCtu....."),
			TEXT("....TtcCCtu....."),
			TEXT("....TtccCtu....."),
			TEXT("....TtcCctu....."),
			TEXT("....TtccCtu....."),
			TEXT("....Tttttuu....."),
			TEXT("....tuuuuuu....."),
			TEXT("................"),
			TEXT("................"),
		});
	default:
	{
		FFVImage U6 = FVArtItemsU6::BuildJunk(Style);   // update 6: Scrap
		if (U6.IsValid()) { return U6; }
		break;
	}
	}
	return BuildMissing();
}

FFVImage UFVSprites::BuildBait(EFVBaitStyle Style)
{
	switch (Style)
	{
	case EFVBaitStyle::Worm:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("..........Iii..."),
			TEXT(".........Iivvi.."),
			TEXT(".........iv..v.."),
			TEXT("........Iiv....."),
			TEXT("......IIiv......"),
			TEXT(".....Iiivv......"),
			TEXT("....Iiv........."),
			TEXT("...Iiv.....Ii..."),
			TEXT("...iiv....Iiv..."),
			TEXT("...iiiIIIiivv..."),
			TEXT("....viiiivvv...."),
			TEXT(".....vvvvv......"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVBaitStyle::Grub:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("....Aa.....Aa..."),
			TEXT("...Aaz....Aaz..."),
			TEXT("...az.....az...."),
			TEXT("...Aa.....Aaa..."),
			TEXT("....aa......az.."),
			TEXT(".....az....Aaz.."),
			TEXT("..Aaaaz...Aaz..."),
			TEXT(".Aaz.....Aaz...."),
			TEXT(".az.....Aaz....."),
			TEXT(".Aaaa..Aaz......"),
			TEXT("..zzaaaaz......."),
			TEXT("....zzzz........"),
			TEXT("................"),
		});
	case EFVBaitStyle::Lure:
		return ItemAscii({
			TEXT("................"),
			TEXT("..ss............"),
			TEXT(".s..s..........."),
			TEXT("..ssSS.........."),
			TEXT("...SSSSs........"),
			TEXT("...SwSSSs......."),
			TEXT("....SSaass......"),
			TEXT(".....SaAasx....."),
			TEXT("......saasxX...."),
			TEXT(".......sssxX...."),
			TEXT("........sxXX...."),
			TEXT(".........XX....."),
			TEXT("..........x....."),
			TEXT("........x.x.x..."),
			TEXT(".........xxx...."),
			TEXT("................"),
		});
	case EFVBaitStyle::Magic:
	{
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("......PPPP......"),
			TEXT(".....PPppPP....."),
			TEXT("....PwPppppp...."),
			TEXT("....PPpplppn...."),
			TEXT("....Pppplppn...."),
			TEXT("....ppplllpn...."),
			TEXT("....pppplpnn...."),
			TEXT(".....pppnnn....."),
			TEXT("......nnnn......"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
		// soft sparkles around the orb (drawn after the outline, 1px)
		const FColor Spark(255, 244, 170);
		Img.Set(2, 3, Spark); Img.Set(3, 2, FVArt::WithAlpha(Spark, 150)); Img.Set(1, 3, FVArt::WithAlpha(Spark, 150));
		Img.Set(13, 9, Spark); Img.Set(12, 9, FVArt::WithAlpha(Spark, 150)); Img.Set(13, 10, FVArt::WithAlpha(Spark, 150));
		Img.Set(12, 2, FColor(226, 168, 255));
		Img.Set(3, 11, FColor(226, 168, 255));
		// little pedestal glow
		Img.Set(6, 13, FVArt::WithAlpha(FColor(170, 90, 236), 120));
		Img.Set(7, 13, FVArt::WithAlpha(FColor(170, 90, 236), 170));
		Img.Set(8, 13, FVArt::WithAlpha(FColor(170, 90, 236), 170));
		Img.Set(9, 13, FVArt::WithAlpha(FColor(170, 90, 236), 120));
		return Img;
	}
	case EFVBaitStyle::Nightcrawler:
		// Выползок: a fat, darker earthworm coiled into an S, with the pale saddle band.
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("........VNNV...."),
			TEXT(".......VNZZNV..."),
			TEXT("......VNZNNNV..."),
			TEXT("......NZNV.VV..."),
			TEXT("......NZUV......"),
			TEXT("......VUUNV....."),
			TEXT(".......VNZNV...."),
			TEXT("........VNZN...."),
			TEXT("...VV....VNZN..."),
			TEXT("..VNZV...VNZN..."),
			TEXT("..NZNNVVVNZNV..."),
			TEXT("..VNZZZZZZNV...."),
			TEXT("...VVNNNNVV....."),
			TEXT("................"),
		});
	case EFVBaitStyle::Dough:
	{
		// Тесто: a soft doughball with a thumb dent plus a small pinched-off piece.
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("......HHHH......"),
			TEXT("....HfFFFffH...."),
			TEXT("...HfFFFFfffH..."),
			TEXT("...fFFwFFfffH..."),
			TEXT("..HfFFFFffHffH.."),
			TEXT("..HfFFFfffHHfH.."),
			TEXT("..HffffffffffH.."),
			TEXT("..HfffffffffHH.."),
			TEXT("...HffffffffH..."),
			TEXT("...HHfffffHH...."),
			TEXT(".....HHHHHH....."),
			TEXT("................"),
			TEXT("................"),
		});
		// flour dust specks (after the outline)
		Img.Set(13, 3, FColor(252, 246, 230, 200));
		Img.Set(2, 4, FColor(252, 246, 230, 160));
		Img.Set(14, 12, FColor(252, 246, 230, 150));
		return Img;
	}
	case EFVBaitStyle::Groundbait:
	{
		// Прикормка: two crumbly brown balls with seeds / corn.
		FFVImage Img(16, 16);
		auto CrumbBall = [&Img](float CX, float CY, float R, FColor Base, uint32 Seed)
		{
			FFVImage B(16, 16);
			FVArtItemsImpl::Ball(B, CX, CY, R, Base);
			for (int32 Y = 0; Y < 16; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					if (B.Get(X, Y).A == 0)
					{
						continue;
					}
					const float N = FVArt::Noise(X, Y, Seed);
					if (N < 0.16f) { B.Set(X, Y, FColor(82, 56, 40)); }          // dark crumbs
					else if (N > 0.93f) { B.Set(X, Y, FColor(255, 214, 90)); }  // corn
					else if (N > 0.86f) { B.Set(X, Y, FColor(186, 150, 104)); } // light crumbs
				}
			}
			B.Outline(FVArt::InkOutline());
			Img.Blit(B, 0, 0);
		};
		CrumbBall(9.5f, 6.8f, 4.6f, FColor(132, 94, 60), 811u);
		CrumbBall(4.8f, 11.2f, 3.2f, FColor(150, 108, 70), 813u);
		// loose crumbs on the ground
		Img.Set(13, 13, FColor(116, 80, 54)); Img.Set(11, 14, FColor(146, 104, 68)); Img.Set(1, 14, FColor(116, 80, 54));
		return Img;
	}
	case EFVBaitStyle::Fly:
		// Generic fly (crafted flies use LureIcon from their parts).
		return BuildLureIcon(1, 1, 0);
	case EFVBaitStyle::Ember:
	{
		// update 5: Жар-наживка — a smouldering orange ball: white-hot core, dark red crust, charred specks, embers rising.
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("......aOOa......"),
			TEXT("....aOOYyOOa...."),
			TEXT("...aOYYwyyOOz..."),
			TEXT("...OyYyyqyOOz..."),
			TEXT("..aOyyqqyOOOaz.."),
			TEXT("..aOOyyOOOqOaz.."),
			TEXT("..zaOOOyOOOOaz.."),
			TEXT("...zaOqOOOaazz.."),
			TEXT("...zzaaOaaazz..."),
			TEXT(".....zzzzzz....."),
			TEXT("................"),
			TEXT("................"),
		});
		// embers drifting up (after the outline, 1px)
		Img.Set(11, 2, FColor(255, 230, 140)); Img.Set(12, 1, FColor(255, 200, 90, 170));
		Img.Set(4, 2, FColor(255, 160, 60)); Img.Set(3, 1, FColor(255, 200, 110, 150));
		Img.Set(8, 1, FColor(255, 244, 180, 200));
		return Img;
	}
	default:
	{
		FFVImage U6 = FVArtItemsU6::BuildBait(Style);   // update 6: Trash .. LegendDragon
		if (U6.IsValid()) { return U6; }
		break;
	}
	}
	return BuildMissing();
}

FFVImage UFVSprites::BuildRodIcon(const FFVRodDef& Rod)
{
	if (Rod.bExpert)
	{
		FFVImage U6 = FVArtItemsU6::BuildRodIcon(Rod);   // update 6: expert rods (type sign + theme)
		if (U6.IsValid()) { return U6; }
	}
	FFVImage Img(16, 16);
	const FColor RodC = Rod.RodColor;
	const FColor RodL = FVArt::Light(RodC, 0.35f);
	const FColor RodD = FVArt::Shade(RodC, 0.3f);
	const FColor Acc = Rod.AccentColor;
	const FColor AccL = FVArt::Light(Acc, 0.3f);
	const FColor AccD = FVArt::Shade(Acc, 0.3f);

	// Blank: from (5,10) to the tip at (13,2); 2px thick (lit upper edge + shaded lower edge), 1px at the tip.
	for (int32 I = 0; I <= 8; ++I)
	{
		const int32 X = 5 + I, Y = 10 - I;
		Img.Set(X, Y, RodL);
		if (I < 7)
		{
			Img.Set(X, Y + 1, I % 3 == 2 ? FColor(214, 218, 230) : RodC);   // guides as light specks
			Img.Set(X + 1, Y + 1, RodD);
		}
	}
	Img.Set(14, 1, FVArt::Light(RodC, 0.6f));
	// Handle (grip) from (1,14) to (4,11), 2px thick.
	for (int32 I = 0; I <= 3; ++I)
	{
		const int32 X = 1 + I, Y = 14 - I;
		Img.Set(X, Y, I % 2 == 0 ? AccL : Acc);
		Img.Set(X + 1, Y, AccD);
	}
	Img.Set(1, 15, AccD);
	// Reel: small drum below the blank near the handle.
	const FColor ReelC(196, 200, 214), ReelD(120, 124, 146);
	Img.Set(5, 12, ReelC); Img.Set(6, 12, ReelC); Img.Set(5, 13, ReelD); Img.Set(6, 13, ReelD);
	Img.Set(7, 13, AccL);   // crank knob
	Img.Outline(FVArt::OutlineOf(RodD));
	// Line: from the tip straight down, ending in a small hook (1px, no outline).
	const FColor LineC(238, 238, 244, 230);
	for (int32 Y = 2; Y <= 12; ++Y)
	{
		Img.Set(14, Y, LineC);
	}
	const FColor HookC(150, 156, 176);
	Img.Set(14, 13, HookC); Img.Set(14, 14, HookC); Img.Set(13, 14, HookC); Img.Set(12, 13, HookC);
	return Img;
}

FFVImage UFVSprites::BuildChest(int32 Frame)
{
	if (Frame <= 0)
	{
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("...MMMMMMMMMM..."),
			TEXT("..MMmmmmmmmmmm.."),
			TEXT(".MmyMmmmmmmymme."),
			TEXT(".mmymmmmmmmymme."),
			TEXT(".YYYYYYyyyyyyyd."),
			TEXT(".mmymmmYymmymme."),
			TEXT(".Mmymmydyymymme."),
			TEXT(".mmymmmyymmymme."),
			TEXT(".mmymmmmmmmymme."),
			TEXT(".mmymmmmmmmymme."),
			TEXT(".YyyyyyyyyyyyyD."),
			TEXT("................"),
			TEXT("................"),
		});
	}
	FFVImage Img = ItemAscii({
		TEXT("................"),
		TEXT("..MMMMMMMMMMMM.."),
		TEXT("..MmymmmmmmymE.."),
		TEXT("..EeEeeeeeeEeE.."),
		TEXT("..EEEEEEEEEEEE.."),
		TEXT(".YYYYYYYYyyyyyd."),
		TEXT(".yYwYYyYYyYyYyd."),
		TEXT(".YyYyYYyYyYYyyd."),
		TEXT(".mmymmmYymmymme."),
		TEXT(".Mmymmydyymymme."),
		TEXT(".mmymmmyymmymme."),
		TEXT(".mmymmmmmmmymme."),
		TEXT(".mmymmmmmmmymme."),
		TEXT(".YyyyyyyyyyyyyD."),
		TEXT("................"),
		TEXT("................"),
	});
	// sparkles above the treasure
	const FColor Spark(255, 250, 200);
	Img.Set(4, 0, Spark);
	Img.Set(11, 0, FVArt::WithAlpha(Spark, 200));
	Img.Set(14, 3, FVArt::WithAlpha(Spark, 170));
	return Img;
}

// ============================================================================================
// Cached getters
// ============================================================================================
const FFVSprite& UFVSprites::Fish(const FFVFishDef& Def)
{
	uint32 Vis = GetTypeHash(uint8(Def.Shape));
	Vis = HashCombine(Vis, GetTypeHash(uint8(Def.Pattern)));
	Vis = HashCombine(Vis, GetTypeHash(Def.BodyColor));
	Vis = HashCombine(Vis, GetTypeHash(Def.BellyColor));
	Vis = HashCombine(Vis, GetTypeHash(Def.FinColor));
	Vis = HashCombine(Vis, GetTypeHash(Def.PatternColor));
	const uint64 Key = MakeKey(FVSpriteCat::Fish, GetTypeHash(Def.Id), Vis);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildFish(Def, false), FString::Printf(TEXT("Fish_%s"), *Def.Id.ToString()));
}

const FFVSprite& UFVSprites::FishSilhouette(const FFVFishDef& Def)
{
	const uint64 Key = MakeKey(FVSpriteCat::FishSil, GetTypeHash(Def.Id), uint32(Def.Shape));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildFish(Def, true), FString::Printf(TEXT("FishSil_%s"), *Def.Id.ToString()));
}

const FFVSprite& UFVSprites::Junk(EFVJunkStyle Style)
{
	const uint64 Key = MakeKey(FVSpriteCat::Junk, uint32(Style));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildJunk(Style), FString::Printf(TEXT("Junk_%d"), int32(Style)));
}

const FFVSprite& UFVSprites::Bait(EFVBaitStyle Style)
{
	const uint64 Key = MakeKey(FVSpriteCat::Bait, uint32(Style));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildBait(Style), FString::Printf(TEXT("Bait_%d"), int32(Style)));
}

const FFVSprite& UFVSprites::RodIcon(const FFVRodDef& Rod)
{
	uint32 Vis = HashCombine(GetTypeHash(Rod.RodColor), GetTypeHash(Rod.AccentColor));
	const uint64 Key = MakeKey(FVSpriteCat::Rod, GetTypeHash(Rod.Id), Vis);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildRodIcon(Rod), FString::Printf(TEXT("Rod_%s"), *Rod.Id.ToString()));
}

const FFVSprite& UFVSprites::Chest(int32 Frame)
{
	const int32 F = FMath::Clamp(Frame, 0, 1);
	const uint64 Key = MakeKey(FVSpriteCat::Chest, uint32(F));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildChest(F), FString::Printf(TEXT("Chest_%d"), F));
}

// ============================================================================================
// UPDATE 2: generic items, crafted lures, shovels
// ============================================================================================
FFVImage UFVSprites::BuildItemArt(EFVItemArt Art, FColor Tint, uint8 Tier)
{
	using namespace FVArtItemsImpl;
	switch (Art)
	{
	// ------------------------------------------------------------------ resources
	case EFVItemArt::Branch:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("...........GG..."),
			TEXT("..........GggG.."),
			TEXT("..........emMg.."),
			TEXT(".........emM...."),
			TEXT("..Gg....emM....."),
			TEXT("..gGm..emM......"),
			TEXT("....mmemM......."),
			TEXT(".....emM........"),
			TEXT("....emM........."),
			TEXT("...emM.........."),
			TEXT("..emM..........."),
			TEXT(".emM............"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::Pebble:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("......kKKKk....."),
			TEXT("....kKKWKKKkk..."),
			TEXT("...kKKWKKKKkkj.."),
			TEXT("...kKKKKKKkkkj.."),
			TEXT("..kkKKKKKkkkkj.."),
			TEXT("..kkkKKkkkkkjj.."),
			TEXT("..jkkkkkkkkjjj.."),
			TEXT("...jjkkkkkjjj..."),
			TEXT("....jjjjjjjj...."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::Fiber:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("....G..G..g.G..."),
			TEXT("....gG.Gg.gGg..."),
			TEXT(".....gGgggGg...."),
			TEXT(".....ggGgGgh...."),
			TEXT("......gGgGh....."),
			TEXT("......hgGgh....."),
			TEXT("......mMMMm....."),
			TEXT("......emmme....."),
			TEXT("......hgGgh....."),
			TEXT(".....ggGgGgh...."),
			TEXT("....gGghgGghg..."),
			TEXT("...gG.g.h.g.hg.."),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::Feather:
		return ItemAscii({
			TEXT("................"),
			TEXT("...........wW..."),
			TEXT(".........wwWWx.."),
			TEXT("........wwWWWx.."),
			TEXT(".......wwWWWx..."),
			TEXT("......wwWWCx...."),
			TEXT(".....wwWWCWx...."),
			TEXT("....wwWWCWx....."),
			TEXT("....w.WCWx......"),
			TEXT("...wWWCWx......."),
			TEXT("...wWCWx........"),
			TEXT("...WCWx........."),
			TEXT("...Cx..........."),
			TEXT("..C............."),
			TEXT(".C.............."),
			TEXT("................"),
		});
	case EFVItemArt::Mushroom:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("......MMMM......"),
			TEXT("....MwMMmmmm...."),
			TEXT("...MMmmmmmmme..."),
			TEXT("..MMmmmmmmmmme.."),
			TEXT("..Mmmmmmmmmmee.."),
			TEXT("..eemmmmmmmeee.."),
			TEXT("...eeeeeeeeee..."),
			TEXT(".....KWWWKk....."),
			TEXT(".....KWWWKk....."),
			TEXT("....KWWWWKkk...."),
			TEXT("....kKWWKKkk...."),
			TEXT(".....kkkkkk....."),
			TEXT("................"),
		});
	case EFVItemArt::Berries:
	{
		FFVImage Img(16, 16);
		const FColor Berry(212, 42, 52);
		// leaves + stem first (behind the berries)
		const FColor Leaf(84, 168, 64), LeafL(150, 214, 92), LeafD(44, 110, 60);
		Img.Set(9, 2, LeafL); Img.Set(10, 2, LeafL); Img.Set(11, 2, Leaf);
		Img.Set(8, 3, LeafL); Img.Set(9, 3, Leaf); Img.Set(10, 3, Leaf); Img.Set(11, 3, LeafD); Img.Set(12, 3, LeafD);
		Img.Set(7, 4, LeafD); Img.Set(9, 4, LeafD); Img.Set(10, 4, LeafD);
		Img.Set(6, 4, FColor(98, 56, 38));
		Ball(Img, 7.5f, 6.8f, 2.8f, Berry);
		Ball(Img, 4.6f, 10.6f, 2.8f, Berry);
		Ball(Img, 10.6f, 10.4f, 2.8f, Berry);
		Img.Outline(FVArt::InkOutline());
		return Img;
	}
	case EFVItemArt::Reed:
		return ItemAscii({
			TEXT("................"),
			TEXT(".......e........"),
			TEXT("......mMe....e.."),
			TEXT("......mMe...mMe."),
			TEXT("......mMe...mMe."),
			TEXT("......mMe...mMe."),
			TEXT("......mMe...mMe."),
			TEXT(".......h....mMe."),
			TEXT("..G....g.....h.."),
			TEXT("..gG...g.....g.."),
			TEXT("...gG..g....g..."),
			TEXT("....gG.g....g..."),
			TEXT(".....gGg...g...."),
			TEXT("......gg..g....."),
			TEXT(".......hhh......"),
			TEXT("................"),
		});
	case EFVItemArt::Clay:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT(".....RRRR......."),
			TEXT("...RRRrrrrr....."),
			TEXT("..RRrrrrrrrrq..."),
			TEXT("..RrrRrrrrrrrq.."),
			TEXT(".RRrrrrqrrrrrq.."),
			TEXT(".Rrrrrrrqrrrrqq."),
			TEXT(".rrrrrrrrrrrqqq."),
			TEXT("..qrrrrrrrrqqq.."),
			TEXT("...qqqqqqqqqq..."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::Resin:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT(".......y........"),
			TEXT(".......yd......."),
			TEXT("......yYd......."),
			TEXT("......yYdd......"),
			TEXT(".....yYYydd....."),
			TEXT("....yYwYyydd...."),
			TEXT("....yYYyyydd...."),
			TEXT("...yyYyyyyddd..."),
			TEXT("...yyyyyyyddd..."),
			TEXT("...dyyyyyyddq..."),
			TEXT("....ddyyyddqq..."),
			TEXT(".....qdddqqq...."),
			TEXT("................"),
			TEXT("................"),
		});

	// ------------------------------------------------------------------ materials
	case EFVItemArt::Board:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("....MMMMMMMMMMC."),
			TEXT("....mmmemmmmmmC."),
			TEXT("....mmmmmmmemmC."),
			TEXT("....eeeeeeeeeeC."),
			TEXT(".MMMMMMMMMMMMC.."),
			TEXT(".mmmmemmmmmmmC.."),
			TEXT(".mXmmmmmmemXmC.."),
			TEXT(".mmemmmmmmmmmC.."),
			TEXT(".eeeeeeeeeeeeC.."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::Rope:
	{
		// A coil of twisted rope with a loose end.
		FFVImage Img(16, 16);
		const FColor Mid(214, 170, 108), Lt(246, 222, 166), Dk(160, 112, 66);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const float DX = X + 0.5f - 7.5f, DY = (Y + 0.5f - 7.5f) * 1.12f;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if (D < 2.3f || D > 6.4f)
				{
					continue;
				}
				// twist: diagonal strands; darker on the lower-right of the coil and in the inner ring
				const int32 Ph = ((X + 2 * Y) % 4 + 4) % 4;
				FColor C = Ph == 0 ? Dk : (Ph == 1 ? Lt : Mid);
				if (DX + DY > 4.f && Ph != 1) { C = FVArt::Shade(C, 0.25f); }
				if (D < 3.2f) { C = FVArt::Shade(C, 0.2f); }
				Img.Set(X, Y, C);
			}
		}
		// loose end
		Img.Set(11, 12, Mid); Img.Set(12, 12, Lt); Img.Set(12, 13, Dk); Img.Set(13, 13, Mid); Img.Set(14, 14, Lt); Img.Set(13, 14, Dk);
		Img.Outline(FVArt::InkOutline());
		return Img;
	}
	case EFVItemArt::Glue:
		return ItemAscii({
			TEXT("................"),
			TEXT("...........M...."),
			TEXT("..........Me...."),
			TEXT(".........Me....."),
			TEXT("....xxxxMexx...."),
			TEXT("...xSSSMeSSSx..."),
			TEXT("...xYYYeYYYYx..."),
			TEXT("...sYyyyyyydx..."),
			TEXT("...scccccccCx..."),
			TEXT("...scaaacccCx..."),
			TEXT("...scccccccCx..."),
			TEXT("...syyyyyyddx..."),
			TEXT("...sxxxxxxxxX..."),
			TEXT("....XXXXXXXX...."),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::Scrap:
	{
		// A rusty plate, a cog and a bolt.
		FFVImage Img(16, 16);
		for (int32 Y = 8; Y <= 13; ++Y)
		{
			for (int32 X = 8 - (Y - 8) / 3; X <= 14 - (Y - 8) / 2; ++X)
			{
				const float N = FVArt::Noise(X, Y, 71u);
				Img.Set(X, Y, N < 0.3f ? FColor(122, 60, 40) : (N > 0.8f ? FColor(214, 130, 70) : FColor(178, 90, 50)));
			}
		}
		const float CX = 6.f, CY = 7.f;
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				const float Ang = FMath::Atan2(DY, DX);
				const bool bTooth = FMath::Cos(Ang * 6.f) > 0.35f;
				if (D < 1.5f || D > (bTooth ? 5.4f : 4.2f))
				{
					continue;
				}
				FColor C(150, 156, 176);
				if (DX + DY < -2.5f) { C = FColor(226, 230, 240); }
				else if (DX + DY > 2.f) { C = FColor(96, 100, 122); }
				if (D < 2.4f) { C = FColor(110, 114, 136); }
				Img.Set(X, Y, C);
			}
		}
		Img.Set(12, 3, FColor(196, 200, 214)); Img.Set(13, 3, FColor(150, 156, 176));
		Img.Set(12, 4, FColor(150, 156, 176)); Img.Set(13, 4, FColor(110, 114, 136));
		Img.Set(12, 5, FColor(128, 132, 152)); Img.Set(12, 6, FColor(110, 114, 136));
		Img.Outline(FVArt::InkOutline());
		return Img;
	}
	case EFVItemArt::Sinker:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT(".......xx......."),
			TEXT("......x..x......"),
			TEXT(".......xx......."),
			TEXT(".......sx......."),
			TEXT("......sSxx......"),
			TEXT(".....sSsxxX....."),
			TEXT(".....sSsxxX....."),
			TEXT("....sSssxxXX...."),
			TEXT("....sSsxxxXX...."),
			TEXT("....ssxxxXXX...."),
			TEXT(".....xxxXXX....."),
			TEXT("......XXXX......"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::Glitter:
	{
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("......mmmm......"),
			TEXT("......MMme......"),
			TEXT(".......TT......."),
			TEXT("......TTTu......"),
			TEXT(".....TwyPTu....."),
			TEXT("....TyPYbyTu...."),
			TEXT("....TYbyPYbu...."),
			TEXT("....TPyYbyPu...."),
			TEXT("....TybPyYbu...."),
			TEXT("....TSPybPyu...."),
			TEXT(".....uuuuuu....."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
		Twinkle(Img, 2, 5, FColor(255, 250, 200), true);
		Twinkle(Img, 13, 3, FColor(255, 255, 255));
		Twinkle(Img, 13, 12, FColor(226, 168, 255));
		return Img;
	}

	// ------------------------------------------------------------------ food
	case EFVItemArt::FishSoup:
	{
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("..WWWWWWWWWWWW.."),
			TEXT(".WdyycyyOyygydW."),
			TEXT(".WddycCyyOyddyW."),
			TEXT("..BBBBBBBBBBBB.."),
			TEXT("..bBwBBBwBBBwb.."),
			TEXT("...bbbbbbbbbb..."),
			TEXT("....bbbbbbbb...."),
			TEXT(".....WWWWWW....."),
			TEXT("................"),
			TEXT("................"),
		});
		Steam(Img, 5, 5, 4, 0);
		Steam(Img, 9, 4, 4, 1);
		return Img;
	}
	// update 5 (§10.2): fried fish by rarity — a plate with garnish, the rim in the rarity colour, the legend's is gold
	case EFVItemArt::FriedFishRare:
		return FriedPlate(RarityRgb(EFVRarity::Rare), 1, false);
	case EFVItemArt::FriedFishEpic:
		return FriedPlate(RarityRgb(EFVRarity::Epic), 2, false);
	case EFVItemArt::FriedFishLegend:
		return FriedPlate(RarityRgb(EFVRarity::Legendary), 3, true);
	case EFVItemArt::FriedFish:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT(".....dMMMd......"),
			TEXT("...dMYYyMMd..d.."),
			TEXT("..dMYyyyyMMddMd."),
			TEXT("..deMyyyyMMMMd.."),
			TEXT("...dMMMMMMd..d.."),
			TEXT("..WWWWWWWWWWYY.."),
			TEXT(".wWWWWWWWWWWYyK."),
			TEXT("..KKKKKKKKKKKK.."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::BerryPie:
		return ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("....cCCCCCCC...."),
			TEXT("..cCcaCcaCcaCC.."),
			TEXT(".cCaCcaCcaCcaCC."),
			TEXT(".CcCCCCCCCCCCCC."),
			TEXT(".CaCcaCcaCcaCaC."),
			TEXT(".MCCCCCCCCCCCCM."),
			TEXT(".mMMMMMMMMMMMMm."),
			TEXT("..mmmmmmmmmmmm.."),
			TEXT("...eeeeeeeeee..."),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVItemArt::HerbTea:
	{
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("...WWWWWWWW....."),
			TEXT("..WddgGdddW....."),
			TEXT("..WWWWWWWWWWW..."),
			TEXT("..WwWWWWWWKWxW.."),
			TEXT("..WBBBBBBBBWxW.."),
			TEXT("..WwWWWWWWKWW..."),
			TEXT("...WWWWWWKK....."),
			TEXT(".xWWWWWWWWWWx..."),
			TEXT("..xxxxxxxxxx...."),
			TEXT("................"),
			TEXT("................"),
		});
		Steam(Img, 5, 4, 4, 1);
		Steam(Img, 8, 3, 3, 0);
		return Img;
	}

	// ------------------------------------------------------------------ tackle
	case EFVItemArt::Line:
	{
		// Spool: flanges colored by tier, the wound line in the item tint, a loose end.
		FFVImage Img(16, 16);
		static const FColor Flange[5][3] = {
			{ FColor(196, 128, 72), FColor(150, 88, 50), FColor(98, 56, 38) },       // 1 wood
			{ FColor(92, 150, 84), FColor(60, 112, 62), FColor(38, 76, 48) },        // 2 green plastic
			{ FColor(238, 242, 250), FColor(190, 196, 210), FColor(118, 122, 146) }, // 3 silver
			{ FColor(96, 150, 220), FColor(56, 96, 170), FColor(36, 60, 116) },      // 4 blue
			{ FColor(255, 242, 150), FColor(255, 204, 51), FColor(206, 132, 30) },   // 5 gold
		};
		const int32 T = FMath::Clamp(int32(Tier), 1, 5) - 1;
		const FColor FL = Flange[T][0], FM = Flange[T][1], FD = Flange[T][2];
		const FColor LL = FVArt::Light(Tint, 0.35f), LM = Tint, LD = FVArt::Shade(Tint, 0.35f);
		for (int32 X = 4; X <= 11; ++X) { Img.Set(X, 2, FL); }
		for (int32 X = 3; X <= 12; ++X) { Img.Set(X, 3, FM); }
		Img.Set(7, 2, FD); Img.Set(8, 2, FD);
		for (int32 Y = 4; Y <= 11; ++Y)
		{
			for (int32 X = 4; X <= 11; ++X)
			{
				FColor C = (Y % 2 == 0) ? LL : LM;
				if (X >= 10) { C = LD; }
				else if (X == 4) { C = FVArt::Shade(C, 0.15f); }
				else if (X == 5 && Y % 2 == 0) { C = FVArt::Light(Tint, 0.6f); }
				Img.Set(X, Y, C);
			}
		}
		for (int32 X = 3; X <= 12; ++X) { Img.Set(X, 12, FM); }
		for (int32 X = 4; X <= 11; ++X) { Img.Set(X, 13, FD); }
		Img.Set(3, 12, FL); Img.Set(4, 12, FL);
		Img.Set(12, 7, LM); Img.Set(13, 8, LM); Img.Set(13, 9, LD); Img.Set(14, 10, LM);
		Img.Outline(FVArt::InkOutline());
		if (T >= 3)
		{
			Twinkle(Img, 2, 5, FColor(255, 255, 255));
		}
		return Img;
	}
	case EFVItemArt::FloatFeather:
		return ItemAscii({
			TEXT("................"),
			TEXT(".......Aa......."),
			TEXT(".......Aa......."),
			TEXT(".......Az......."),
			TEXT(".......wW......."),
			TEXT("......wWWk......"),
			TEXT("......wWWk......"),
			TEXT("......wWKk......"),
			TEXT("......wWKk......"),
			TEXT(".......WK......."),
			TEXT(".......WK......."),
			TEXT(".....W.Ck.W....."),
			TEXT("......WCkW......"),
			TEXT(".......CC......."),
			TEXT("........C......."),
			TEXT("................"),
		});
	case EFVItemArt::FloatReed:
		return ItemAscii({
			TEXT("................"),
			TEXT(".......aA......."),
			TEXT(".......az......."),
			TEXT(".......Gh......."),
			TEXT(".......Gh......."),
			TEXT(".......kj......."),
			TEXT("......MMme......"),
			TEXT(".....MMmmme....."),
			TEXT(".....MMmmme....."),
			TEXT(".....MMmmme....."),
			TEXT("......Mmme......"),
			TEXT(".......kj......."),
			TEXT(".......Gh......."),
			TEXT(".......Gh......."),
			TEXT("........h......."),
			TEXT("................"),
		});
	case EFVItemArt::FloatSliding:
		return ItemAscii({
			TEXT("................"),
			TEXT(".......OY......."),
			TEXT(".......QQ......."),
			TEXT(".......OO......."),
			TEXT(".......QQ......."),
			TEXT(".......Gg......."),
			TEXT("......GGgh......"),
			TEXT(".....GGgghh....."),
			TEXT(".....GGgghh....."),
			TEXT("......Gggh......"),
			TEXT(".......gh......."),
			TEXT(".......xX......."),
			TEXT(".......xX......."),
			TEXT("......s..s......"),
			TEXT(".......ss......."),
			TEXT("................"),
		});
	case EFVItemArt::FloatGlow:
	{
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT(".......Yl......."),
			TEXT(".......ll......."),
			TEXT(".......lG......."),
			TEXT(".......GG......."),
			TEXT(".......ww......."),
			TEXT("......BbbQ......"),
			TEXT(".....BbbbQQ....."),
			TEXT(".....BbbQQQ....."),
			TEXT("......bQQQ......"),
			TEXT(".......QQ......."),
			TEXT(".......xX......."),
			TEXT(".......xX......."),
			TEXT("........X......."),
			TEXT("................"),
			TEXT("................"),
		});
		// soft glow around the light-stick tip (blended over the outline)
		const FColor Glow(200, 255, 150, 110);
		for (int32 Y = 0; Y <= 5; ++Y)
		{
			Img.Blend(5, Y, FVArt::WithAlpha(Glow, (Y == 0 || Y == 5) ? 40 : 80));
			Img.Blend(10, Y, FVArt::WithAlpha(Glow, (Y == 0 || Y == 5) ? 40 : 80));
		}
		Img.Blend(6, 0, Glow); Img.Blend(7, 0, Glow); Img.Blend(8, 0, Glow); Img.Blend(9, 0, Glow);
		Img.Blend(6, 1, Glow); Img.Blend(9, 1, Glow); Img.Blend(6, 2, Glow); Img.Blend(9, 2, Glow);
		return Img;
	}
	case EFVItemArt::FloatSport:
		return ItemAscii({
			TEXT("................"),
			TEXT(".......A........"),
			TEXT(".......a........"),
			TEXT(".......a........"),
			TEXT(".......Y........"),
			TEXT("......yOO......."),
			TEXT(".....yOOOr......"),
			TEXT(".....OwOOr......"),
			TEXT("....yOOOOrr....."),
			TEXT("....QQQQQQQ....."),
			TEXT(".....QQQQQ......"),
			TEXT("......QQQ......."),
			TEXT(".......x........"),
			TEXT(".......x........"),
			TEXT(".......X........"),
			TEXT("................"),
		});
	case EFVItemArt::FloatGold:
	{
		FFVImage Img = ItemAscii({
			TEXT("................"),
			TEXT(".......Y........"),
			TEXT(".......y........"),
			TEXT(".......y........"),
			TEXT(".......d........"),
			TEXT("......YYy......."),
			TEXT(".....YwYyd......"),
			TEXT(".....YYyyd......"),
			TEXT("....YYyyydd....."),
			TEXT("....WWWWWKK....."),
			TEXT(".....yyydd......"),
			TEXT("......ydd......."),
			TEXT(".......d........"),
			TEXT(".......x........"),
			TEXT("................"),
			TEXT("................"),
		});
		Twinkle(Img, 12, 3, FColor(255, 250, 200), true);
		Twinkle(Img, 2, 10, FColor(255, 236, 140));
		return Img;
	}
	case EFVItemArt::Net:
	{
		// Folded net bundle: mesh in the tint (holes show the outline color), cork floats on top, lead weights below.
		FFVImage Img(16, 16);
		const int32 T = FMath::Clamp(int32(Tier), 1, 3);
		const float RX = 3.8f + T, RY = 4.3f + T * 0.3f;
		const float CX = 8.f, CY = 8.5f;
		const FColor ML = FVArt::Light(Tint, 0.3f), MM = Tint, MD = FVArt::Shade(Tint, 0.3f);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const float DX = (X + 0.5f - CX) / RX, DY = (Y + 0.5f - CY) / RY;
				if (DX * DX + DY * DY > 1.f)
				{
					continue;
				}
				const bool bCord = ((X + Y) % 3 == 0) || (((X - Y) % 3 + 3) % 3 == 0);
				if (!bCord)
				{
					continue;
				}
				Img.Set(X, Y, DX + DY < -0.5f ? ML : (DX + DY > 0.6f ? MD : MM));
			}
		}
		const int32 X0 = FMath::RoundToInt(CX - RX) + 1, X1 = FMath::RoundToInt(CX + RX) - 2;
		const int32 TopY = FMath::RoundToInt(CY - RY) + 1;
		for (int32 X = X0; X <= X1; ++X) { Img.Set(X, TopY, FColor(160, 112, 66)); }
		const int32 NFloats = T + 2;
		for (int32 I = 0; I < NFloats; ++I)
		{
			const int32 FX = X0 + (X1 - X0 - 1) * I / (NFloats - 1);
			Img.Set(FX, TopY - 1, FColor(236, 116, 52)); Img.Set(FX + 1, TopY - 1, FColor(250, 170, 90));
			Img.Set(FX, TopY, FColor(196, 80, 40)); Img.Set(FX + 1, TopY, FColor(236, 116, 52));
		}
		const int32 BotY = FMath::RoundToInt(CY + RY) - 1;
		for (int32 I = 0; I < NFloats - 1; ++I)
		{
			const int32 WX = X0 + 1 + (X1 - X0 - 2) * I / FMath::Max(1, NFloats - 2);
			Img.Set(WX, BotY, FColor(110, 114, 136)); Img.Set(WX, BotY + 1, FColor(88, 90, 112));
		}
		Img.Outline(FVArt::InkOutline());
		return Img;
	}
	default:
	{
		FFVImage U6 = FVArtItemsU6::BuildItemArt(Art, Tint, Tier);   // update 6: Log .. Blueprint
		if (U6.IsValid()) { return U6; }
		break;
	}
	}
	return BuildMissing();
}

FFVImage UFVSprites::BuildLureIcon(uint8 Base, uint8 Color, uint8 Glitter)
{
	using namespace FVArtItemsImpl;
	FFVImage Img(16, 16);
	const FColor CB = LureColor(Color, 0), CL = LureColor(Color, 1), CD = LureColor(Color, 2);
	const FColor MB = LureMetal(Glitter, 0), ML = LureMetal(Glitter, 1), MD = LureMetal(Glitter, 2);
	const FColor HookC(150, 156, 176), HookD(96, 100, 122);
	const FColor SparkC = Glitter >= 2 ? FColor(255, 236, 130) : FColor(250, 252, 255);

	if (Base == 0)
	{
		// ---- spoon (блесна): chunky diagonal teardrop, painted face with a metal rim; the split ring (top-left)
		// and the treble hook (bottom-right) are thin metal drawn after the outline.
		// Axis coordinate A = x + y, cross coordinate C = x - y (0 on the axis).
		static const int32 Half[] = { 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 4, 4, 3, 3, 2, 1 };   // A = 5..21
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const int32 A = X + Y, C = X - Y;
				if (A < 5 || A > 21)
				{
					continue;
				}
				const int32 W = Half[A - 5];
				const int32 MaxC = ((W + A) % 2 == 0) ? W : W - 1;   // outermost |C| reachable with this parity
				if (FMath::Abs(C) > MaxC)
				{
					continue;
				}
				FColor P = CB;
				const bool bRim = FMath::Abs(C) >= MaxC;
				if (bRim) { P = C > 0 ? ML : (C < 0 ? MD : MB); }             // metal rim: lit upper-right, dark lower-left
				else if (A <= 11 && C >= 0) { P = CL; }                         // paint highlight near the nose
				else if (A >= 17) { P = CD; }                                   // tail end in shade
				Img.Set(X, Y, P);
			}
		}
		// contrasting dot near the tail (red on black, dark on the others)
		Img.Set(8, 8, Color == 3 ? FColor(230, 60, 56) : FColor(40, 28, 48));
		Img.Set(7, 6, FVArt::Light(CL, 0.5f));
		Img.Outline(FVArt::InkOutline());
		// split ring + link
		Img.Set(1, 0, MB); Img.Set(0, 1, MB); Img.Set(2, 1, MD); Img.Set(1, 2, MD); Img.Set(2, 2, ML);
		// swivel + treble hook
		const FColor HookL(214, 218, 230);
		Img.Set(11, 11, MB); Img.Set(12, 12, HookC); Img.Set(13, 13, HookC);
		Img.Set(14, 13, HookC); Img.Set(15, 12, HookC); Img.Set(15, 11, HookL);    // right hook, point up
		Img.Set(13, 14, HookC); Img.Set(12, 15, HookC); Img.Set(11, 14, HookL);    // left hook, point up
		Img.Set(14, 14, HookD); Img.Set(15, 15, HookD);                             // middle hook bend
		if (Glitter > 0)
		{
			Sparkle(Img, 3, 3, 11, 11, SparkC, Glitter >= 2 ? 0.2f : 0.16f, 97u + Color);
			Twinkle(Img, 12, 3, SparkC, Glitter >= 2);
			if (Glitter >= 2) { Twinkle(Img, 3, 12, SparkC); }
		}
		return Img;
	}

	// ---- fly (мушка): tail at the back (left), ribbed body, swept feather wing, hackle collar, dark head;
	// the hook (bend under the body, eye at the head) is thin metal drawn after the outline.
	const FColor Thread(40, 30, 44);
	const FColor Hackle(170, 110, 64), HackleL(222, 170, 110);
	const FColor Wing = FVArt::Mix(FVArt::Light(CB, 0.5f), FColor(226, 228, 236), Color == 3 ? 0.75f : 0.4f);
	const FColor WingD = FVArt::Shade(Wing, 0.28f), WingL = FVArt::Light(Wing, 0.45f);
	// tail fibers
	Img.Set(1, 6, CL); Img.Set(2, 6, CL); Img.Set(1, 8, CD); Img.Set(2, 7, CB); Img.Set(3, 7, CB); Img.Set(2, 8, CD); Img.Set(3, 8, CB);
	// wing: a feather rising from the head and swept back, with a darker vein
	static const int32 WingRows[5][2] = { { 7, 8 }, { 6, 9 }, { 5, 9 }, { 5, 10 }, { 6, 10 } };   // rows 2..6: x from..to
	for (int32 R = 0; R < 5; ++R)
	{
		for (int32 X = WingRows[R][0]; X <= WingRows[R][1]; ++X)
		{
			Img.Set(X, 2 + R, X == WingRows[R][0] ? WingL : Wing);
		}
	}
	Img.Set(9, 5, WingD); Img.Set(8, 4, WingD); Img.Set(7, 3, WingD); Img.Set(10, 6, WingD);
	// body with ribbing (tinsel when glittered)
	for (int32 Y = 7; Y <= 9; ++Y)
	{
		for (int32 X = 4; X <= 10; ++X)
		{
			FColor P = Y == 7 ? CL : (Y == 9 ? CD : CB);
			if (X % 2 == 0 && Y == 8)
			{
				P = Glitter > 0 ? ML : CD;
			}
			else if (X % 2 == 1 && Y != 8 && Glitter > 0)
			{
				P = Y == 7 ? ML : MD;
			}
			Img.Set(X, Y, P);
		}
	}
	// hackle collar
	for (int32 Y = 5; Y <= 11; ++Y)
	{
		Img.Set(11, Y, (Y % 2 == 0) ? HackleL : Hackle);
	}
	Img.Set(10, 5, Hackle); Img.Set(12, 6, HackleL); Img.Set(12, 10, Hackle); Img.Set(10, 11, HackleL);
	// head
	Img.Set(12, 8, Thread); Img.Set(12, 9, Thread);
	Img.Outline(FVArt::InkOutline());
	// hook: bend below the body, point with barb, eye ring at the head
	const FColor HookL(214, 218, 230);
	Img.Set(5, 10, HookC); Img.Set(5, 11, HookC); Img.Set(6, 12, HookD); Img.Set(7, 12, HookC);
	Img.Set(8, 11, HookC); Img.Set(8, 10, HookL); Img.Set(9, 11, HookD);
	Img.Set(13, 8, HookC); Img.Set(14, 8, HookD); Img.Set(13, 9, HookL);
	if (Glitter > 0)
	{
		Sparkle(Img, 4, 7, 10, 9, SparkC, Glitter >= 2 ? 0.3f : 0.22f, 131u + Color);
		Twinkle(Img, 13, 3, SparkC, Glitter >= 2);
		if (Glitter >= 2) { Twinkle(Img, 2, 12, SparkC); }
	}
	return Img;
}

FFVImage UFVSprites::BuildShovelIcon(int32 Level)
{
	using namespace FVArtItemsImpl;
	const int32 L = FMath::Clamp(Level, 1, 4);
	// [level][blade light, blade base, blade dark, handle light, handle dark, collar]
	static const FColor Pal[4][6] = {
		{ FColor(176, 170, 160), FColor(130, 126, 122), FColor(88, 86, 90), FColor(206, 150, 92), FColor(150, 98, 58), FColor(96, 96, 108) },
		{ FColor(214, 218, 230), FColor(160, 166, 184), FColor(104, 108, 128), FColor(176, 112, 62), FColor(120, 72, 42), FColor(120, 124, 144) },
		{ FColor(250, 252, 255), FColor(200, 210, 228), FColor(126, 138, 168), FColor(214, 84, 70), FColor(150, 44, 44), FColor(222, 160, 48) },
		{ FColor(236, 190, 255), FColor(176, 98, 236), FColor(100, 46, 170), FColor(96, 80, 110), FColor(58, 46, 70), FColor(255, 204, 51) },
	};
	const FColor* P = Pal[L - 1];
	FFVImage Img(16, 16);
	// S = along the handle (grows to the top-right), T = across (15/16 = the handle line).
	for (int32 Y = 0; Y < 16; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			const int32 S = X - Y, T = X + Y;
			if ((S == 10 || S == 11) && T >= 12 && T <= 18)          // T-grip
			{
				Img.Set(X, Y, S == 10 ? P[3] : P[4]);
				continue;
			}
			if ((T == 15 || T == 16) && S >= -1 && S <= 9)           // shaft
			{
				Img.Set(X, Y, T == 15 ? P[3] : P[4]);
				continue;
			}
			if ((S == -3 || S == -2) && T >= 14 && T <= 17)          // collar
			{
				Img.Set(X, Y, T <= 15 ? FVArt::Light(P[5], 0.3f) : P[5]);
				continue;
			}
			if (S <= -4 && S >= -13)                                 // blade: widest at the shoulder, pointed tip
			{
				float W = 3.5f;
				if (S == -10) { W = 3.f; }
				else if (S == -11) { W = 2.5f; }
				else if (S == -12) { W = 1.5f; }
				else if (S == -13) { W = 0.5f; }
				const float Off = T - 15.5f;
				if (FMath::Abs(Off) > W)
				{
					continue;
				}
				FColor C = P[1];
				if (Off < -1.f) { C = P[0]; }
				else if (Off > 1.f) { C = P[2]; }
				if (S <= -11 && Off <= 0.f) { C = FVArt::Light(P[0], 0.4f); }   // sharpened edge
				if (S == -4) { C = FVArt::Shade(C, 0.15f); }                   // step at the shoulder
				Img.Set(X, Y, C);
			}
		}
	}
	Img.Outline(FVArt::InkOutline());
	if (L == 3)
	{
		Twinkle(Img, 3, 8, FColor(255, 255, 255));
	}
	else if (L == 4)
	{
		Twinkle(Img, 2, 8, FColor(250, 220, 255), true);
		Twinkle(Img, 9, 13, FColor(255, 240, 200));
	}
	return Img;
}

// ---------------------------------------------------------------- cached getters (update 2)
const FFVSprite& UFVSprites::ItemArt(EFVItemArt Art, FColor Tint, uint8 Tier)
{
	const uint32 TintKey = (uint32(Tint.R) << 16) | (uint32(Tint.G) << 8) | uint32(Tint.B);
	const uint64 Key = MakeKey(FVSpriteCat::ItemArt, uint32(Art) | (uint32(Tier) << 8), TintKey);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildItemArt(Art, Tint, Tier), FString::Printf(TEXT("ItemArt_%d_%d"), int32(Art), int32(Tier)));
}

const FFVSprite& UFVSprites::LureIcon(uint8 Base, uint8 Color, uint8 Glitter)
{
	const uint8 B = FMath::Min<uint8>(Base, 1), C = FMath::Min<uint8>(Color, 3), G = FMath::Min<uint8>(Glitter, 2);
	const uint64 Key = MakeKey(FVSpriteCat::Lure, (uint32(B) << 8) | (uint32(C) << 4) | uint32(G));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildLureIcon(B, C, G), FString::Printf(TEXT("Lure_%d_%d_%d"), int32(B), int32(C), int32(G)));
}

const FFVSprite& UFVSprites::ShovelIcon(int32 Level)
{
	const int32 L = FMath::Clamp(Level, 1, 4);
	const uint64 Key = MakeKey(FVSpriteCat::ItemArt, 0xF000u + uint32(L), 0, 1);
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildShovelIcon(L), FString::Printf(TEXT("Shovel_%d"), L));
}

const FFVSprite& UFVSprites::ItemIcon(FName ItemId)
{
	if (ItemId.IsNone())
	{
		if (!Missing.IsValid())
		{
			Missing = MakeSprite(BuildMissing(), TEXT("Missing"));
		}
		return Missing;
	}
	// Resolved ids are remembered in the named registry (exact FName key: no hash collisions).
	if (const FFVSprite* Known = Named.Find(ItemId))
	{
		return *Known;
	}
	FFVSprite Resolved;
	EFVRarity TokenRarity = EFVRarity::Common;
	if (const FFVFishDef* F = FVDB::FindFish(ItemId))
	{
		Resolved = Fish(*F);
	}
	else if (const FFVJunkDef* J = FVDB::FindJunk(ItemId))
	{
		Resolved = Junk(J->Style);
	}
	else if (const FFVBaitDef* B = FVDB::FindBait(ItemId))
	{
		Resolved = B->bLure ? LureIcon(B->LureBase, B->LureColor, B->LureGlitter) : Bait(B->Style);
	}
	else if (const FFVItemDef* It = FVDB::FindItem(ItemId))
	{
		Resolved = ItemArt(It->Art, It->Tint, It->Tier);
	}
	else if (const FFVRodDef* R = FVDB::FindRod(ItemId))
	{
		Resolved = RodIcon(*R);
	}
	else if (const FFVDecorDef* D = FVDB::FindDecor(ItemId))
	{
		Resolved = DecorIcon(*D);
	}
	else if (ItemId == FVDB::AnyFishToken())
	{
		Resolved = Icon(EFVIcon::Fish);
	}
	else if (FVDB::FishTokenRarity(ItemId, TokenRarity))
	{
		// update 5: any_fish_rare / _epic / _legend — the fish on a card framed in the rarity colour
		const uint64 Key = MakeKey(FVSpriteCat::ItemArt, 0xF100u + uint32(TokenRarity), 0, 2);
		const FFVSprite* S = FindCached(Key);
		Resolved = S ? *S : AddCached(Key, FVArtItemsImpl::BuildFishTokenIcon(TokenRarity), FString::Printf(TEXT("FishToken_%d"), int32(TokenRarity)));
	}
	else
	{
		// Pseudo ids "shovel_1".."shovel_4" (shovels have no table ids).
		const FString S = ItemId.ToString();
		if (S.StartsWith(TEXT("shovel_")))
		{
			Resolved = ShovelIcon(FCString::Atoi(*S.RightChop(7)));
		}
		// update 6: pseudo ids "axe_1".."axe_4" and "pickaxe_1".."pickaxe_2"
		else if (S.StartsWith(TEXT("axe_")))
		{
			Resolved = AxeIcon(FCString::Atoi(*S.RightChop(4)));
		}
		else if (S.StartsWith(TEXT("pickaxe_")))
		{
			Resolved = PickaxeIcon(FCString::Atoi(*S.RightChop(8)));
		}
	}
	if (!Resolved.IsValid())
	{
		return Get(ItemId);   // magenta placeholder (logged once per id)
	}
	return Named.Add(ItemId, Resolved);
}
