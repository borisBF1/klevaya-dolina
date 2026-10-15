// UPDATE 6 [ART-FISH-SIDE] — side views of the new fish shapes (FlyingFish..Sturgeon) and the six expert legends (Docs/UPDATE6_FOREST_CITY.md §3а, §8, §9а, §10.6, §10.8). Owner: ART-FISH-SIDE.
// Contract: Public/FVSprites.h «UPDATE 6», Private/FVArtU6.h (FVArtFishSide::BuildFish, called by UFVSprites::BuildFish).
// Style: the RenderFish technique of FVArtItems.cpp (ASCII template facing RIGHT, auto back -> belly shading, patterns on
// the flank, 1px tinted outline, thin details after the outline), copied locally with the update-6 patterns Scales and
// Shimmer, blind eyes and a few coloured after-outline details for the legends.
// Sizes (template, +2 with the outline): ordinary 18..30 x 7..17; expert legends <= 40 x 18.
//
// Template chars (as FVArtItems.cpp): B body, b belly, h highlight, d dark, F fin, f fin shade, Y fin highlight, G gold,
//   E pupil, W eye highlight, w eye white / teeth, o inner outline (mouth), L glow (PatternColor -> white).
// After-outline chars (1px, not outlined): x dark detail, r rust, c chain steel, s shell, a amber glow, k sparkle, m moss.

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVArtU6.h"

namespace FVArtFishSideImpl
{
	struct FTmpl
	{
		const TCHAR* const* Rows = nullptr;
		int32 NumRows = 0;
		int32 PatX0 = 0, PatX1 = 0;
	};

#define FV_TMPL(Name, X0, X1) { Name, UE_ARRAY_COUNT(Name), X0, X1 }

	const FTmpl* ShapeTemplate(EFVFishShape Shape)
	{
		static const TCHAR* FlyingFish[] = {
			TEXT("......YYY"),
			TEXT("....YYFFFY"),
			TEXT("..YFFFFFfBBBB"),
			TEXT("F.FFffBBBBBBBBBB"),
			TEXT("FFBBBBBBBBBBBBBdWEB"),
			TEXT("FFBBBBBBBBBBBBBdEEB"),
			TEXT("FFbbbbbbbbbbbbbdbbo"),
			TEXT("F..bbbbbbbbbbbbbbb"),
			TEXT("....FFf....Ff"),
			TEXT(".....F"),
		};
		static const TCHAR* Barracuda[] = {
			TEXT("F..........f....f"),
			TEXT("FF...BBBBBBBBBBBBBBBBBBB"),
			TEXT(".FF.BBBBBBBBBBBBBBBBBBBBdWEB"),
			TEXT("..FFBBBBBBBBBBBBBBBBBBBBdEEBo"),
			TEXT(".FFbbbbbbbbbbbbbbbbbbbbbbbwbwb"),
			TEXT("FF...bbbbbbbbbbbbbbbbbbbbbbb"),
			TEXT("F.........Ff......f"),
		};
		static const TCHAR* Mackerel[] = {
			TEXT("F........ff.f"),
			TEXT("FF...xxBBBBBBBBBBBB"),
			TEXT(".FF.BBBBBBBBBBBBBBBBBB"),
			TEXT("..FFBBBBBBBBBBBBBBBBdWEB"),
			TEXT("..FFbbbbbbbbbbbbbbbbdEEo"),
			TEXT(".FF.bbbbbbbbbbbbbbbbbbb"),
			TEXT("FF...xxbbbbbbbbbbbbb"),
			TEXT("F.........Ff"),
		};
		static const TCHAR* Grouper[] = {
			TEXT("........x.x.x.x.x"),
			TEXT(".......fFfFfFfFfF"),
			TEXT("......FFFFFFFFFFFFf"),
			TEXT("FF...BBBBBBBBBBBBBBBB"),
			TEXT("FFF.BBBBBBBBBBBBBBBBBBB"),
			TEXT("FFFFBBBBBBBBBBBBBBBdWEBB"),
			TEXT("FFFFBBBBBBBBBBBBBBBdEEBh"),
			TEXT("FFFFbbbbbbbbbbbbbbbdbbbo"),
			TEXT("FFFFbbbbbbbbbbbbbbbbbbbh"),
			TEXT("FFF.bbbbbbbbbbbbbbbbbbb"),
			TEXT("FF...bbbbbbbbbbbbbbbb"),
			TEXT("......FFf.....FFf"),
			TEXT(".......Ff......Ff"),
		};
		static const TCHAR* Swordfish[] = {
			TEXT("..........Ff"),
			TEXT("..........FFf"),
			TEXT("F.........FFFf"),
			TEXT("FF.....BBBBBBBBBBB"),
			TEXT(".FF.BBBBBBBBBBBBBBBBB"),
			TEXT("..FFBBBBBBBBBBBBBBBBBdWEBhhhhhhhh"),
			TEXT("..FFbbbbbbbbbbbbbbbbbdEEBo"),
			TEXT(".FF.bbbbbbbbbbbbbbbbbbbb"),
			TEXT("FF......bbbbbbbbbbbb"),
			TEXT("F..........FFf"),
		};
		static const TCHAR* Ray[] = {
			TEXT("..............BBB"),
			TEXT("............BBBBBBB"),
			TEXT("..........BBBBBBBBBBB"),
			TEXT("........BBBBBBBBBBBBBBB"),
			TEXT("xxxxxxxBBBBBBBBBBBBBBBBWEB"),
			TEXT(".....x.bbbbbbbbbbbbbbbbbEEo"),
			TEXT("........bbbbbbbbbbbbbbbb"),
			TEXT("..........FFFFfffFF"),
			TEXT("............FFff"),
		};
		static const TCHAR* Sunfish[] = {
			TEXT(".......FF"),
			TEXT("......FFf"),
			TEXT(".....FFFf"),
			TEXT("....BBBBBBBB"),
			TEXT("..FBBBBBBBBBBB"),
			TEXT(".FFBBBBBBBBBBBB"),
			TEXT("FFBBBBBBBBBBdWEB"),
			TEXT("FFBBBBBBBBBBdEEB"),
			TEXT("FFBBBBBBBBBBBBBo"),
			TEXT("FFbbbbbbbbbbbbbb"),
			TEXT("FFbbbbbbbbbbbbb"),
			TEXT(".Fbbbbbbbbbbbbb"),
			TEXT("..Fbbbbbbbbbbb"),
			TEXT("....bbbbbbbb"),
			TEXT(".....FFFf"),
			TEXT("......FFf"),
			TEXT(".......FF"),
		};
		static const TCHAR* Sailfish[] = {
			TEXT("......YFYFYFYF"),
			TEXT(".....FFFFFFFFFFY"),
			TEXT(".....FfFfFfFfFFFY"),
			TEXT("....FFFFFFFFFFFFFFY"),
			TEXT("F...FfFfFfFfFfFFFFFY"),
			TEXT("FF...BBBBBBBBBBBBBBBBB"),
			TEXT(".FF.BBBBBBBBBBBBBBBBBBBB"),
			TEXT("..FFBBBBBBBBBBBBBBBBBBdWEBhhhhh"),
			TEXT(".FFbbbbbbbbbbbbbbbbbbbdbbo"),
			TEXT("FF..bbbbbbbbbbbbbbbbbbbb"),
			TEXT("F.......Ff........Ff"),
		};
		static const TCHAR* Moray[] = {
			TEXT("..fFfFfFfFfFfFfFfFfFf"),
			TEXT(".FBBBBBBBBBBBBBBBBBBBBBB"),
			TEXT("FBBBBBBBBBBBBBBBBBBBBBBdWEB"),
			TEXT("FBBBBBBBBBBBBBBBBBBBBBBdEEBw"),
			TEXT("FFbbbbbbbbbbbbbbbbbbbbbbb.o"),
			TEXT(".Fbbbbbbbbbbbbbbbbbbbbbbbbw"),
			TEXT("..fFfFfFfFfFfFfFbbbbbbbb"),
		};
		static const TCHAR* SeaDragon[] = {
			TEXT("......F....F....F"),
			TEXT(".....FF...FF...FF.....FF"),
			TEXT("......BBBBBBBBBBBBBBBBF"),
			TEXT("....BBBBBBBBBBBBBBBBBBBB"),
			TEXT("..BBBbbbbbbbbbbbbbbbbBdWEB"),
			TEXT(".BB..bbbbbbbbbbbbbbbbbdEEBhhhh"),
			TEXT("BB....FF..bbbbbb..FF..bbb"),
			TEXT("B.B...F...F....F...F"),
			TEXT(".BB"),
		};
		static const TCHAR* Grayling[] = {
			TEXT(".....FYFYFYF"),
			TEXT("....FFFFFFFFY"),
			TEXT("....FYFYFYFFFY"),
			TEXT("F...BBBBBBBBBBBB"),
			TEXT("FF.BBBBBBBBBBBBBBB"),
			TEXT(".FFBBBBBBBBBBBBBBdWEB"),
			TEXT(".FFbbbbbbbbbbbbbbdEEo"),
			TEXT("FF..bbbbbbbbbbbbbbb"),
			TEXT("F......f...Ff.Ff"),
		};
		static const TCHAR* Proteus[] = {
			TEXT("....................F.F"),
			TEXT("..ffBBBBBBBBBBBBBBBBFFBB"),
			TEXT("FFBBBBBBBBBBBBBBBBBBBFBBBB"),
			TEXT("FFbbbbbbbbbbbbbbbbbbbFbbbbo"),
			TEXT("..ffbbbbbbbbbbbbbbbbFFbbb"),
			TEXT("....x.x.........x.xF.F"),
			TEXT("....x...........x"),
		};
		static const TCHAR* Sturgeon[] = {
			TEXT("F.........f"),
			TEXT("FF.....BBhBBBhBBBhBBBhBBBhB"),
			TEXT(".FF.BBBBBBBBBBBBBBBBBBBBBBBBB"),
			TEXT("..FBBBBBBBBBBBBBBBBBBBBBBBBdWEB"),
			TEXT(".FFbbbbbbbbbbbbbbbbbbbbbbbbdbbbBB"),
			TEXT("FF..bbbbbbbbbbbbbbbbbbbbbbbbbbb"),
			TEXT("F........Ff.......Ff.....x.x"),
			TEXT(".........................x.x"),
		};
		static const FTmpl T[] = {
			FV_TMPL(FlyingFish, 3, 14), FV_TMPL(Barracuda, 4, 23), FV_TMPL(Mackerel, 4, 19), FV_TMPL(Grouper, 4, 18),
			FV_TMPL(Swordfish, 4, 20), FV_TMPL(Ray, 8, 22), FV_TMPL(Sunfish, 2, 12), FV_TMPL(Sailfish, 4, 21),
			FV_TMPL(Moray, 2, 22), FV_TMPL(SeaDragon, 3, 21), FV_TMPL(Grayling, 3, 16), FV_TMPL(Proteus, 2, 19),
			FV_TMPL(Sturgeon, 4, 26),
		};
		const int32 I = int32(Shape) - int32(EFVFishShape::FlyingFish);
		return (I >= 0 && I < int32(UE_ARRAY_COUNT(T))) ? &T[I] : nullptr;
	}

	const FTmpl* LegendTemplate(EFVExpertLegend L)
	{
		// Царь-марлин: dark-blue marlin, golden crest-crown, spear, lightning stripes
		static const TCHAR* Marlin[] = {
			TEXT("..............G.G.G"),
			TEXT(".............GFGFGFG"),
			TEXT("F...........FFFFFFFFFF"),
			TEXT("FF.........FFfFfFfFFFFF"),
			TEXT(".FF......BBBBBBBBBBBBBBBBB"),
			TEXT("..FF..BBBBBBBBBBBBBBBBBBBBBB"),
			TEXT("...FFBBBBBBBBBBBBBBBBBBBBBBBBBdWEBhhhhhh"),
			TEXT("...FFBBBBBBBBBBBBBBBBBBBBBBBBBdEEBo"),
			TEXT("..FFbbbbbbbbbbbbbbbbbbbbbbbbbbdbbo"),
			TEXT(".FF..bbbbbbbbbbbbbbbbbbbbbbbbbb"),
			TEXT("FF.......bbbbbbbbbbbbbbbbbbbb"),
			TEXT("F...........FFf.......Ff"),
			TEXT("............Ff........F"),
		};
		// Хозяин гавани: an old giant grouper in shells, a rusty hook in the lip, a chain end at the tail, a glowing eye
		static const TCHAR* Harbor[] = {
			TEXT("..........x.x.x.x.x.x.x"),
			TEXT(".........fFfFfFfFfFfFfF"),
			TEXT("........FFFFFFFFFFFFFFFFf"),
			TEXT("FF.....BBBsBBBBBBBBsBBBBBBB"),
			TEXT("FFF..BBBBBBBBBBBsBBBBBBBBBBBB"),
			TEXT("FFFFBBBBBBBBBBBBBBBBBBBBBBBBBB"),
			TEXT("FFFFBBBBBBBBBBBBBBBBBBBBBBdwLBB"),
			TEXT("FFFFBBsBBBBBBBBBBBBBBBBBBBdLLBBh"),
			TEXT("FFFFbbbbbbbbbbbbbbbbbbbbbbdbbbbo.rr"),
			TEXT("FFFFbbbbbbbbbbbbbbbbbbbbbbbbbbbh..r"),
			TEXT("cFFFbbbbbbbbsbbbbbbbbbbbbbbbbbb.rr"),
			TEXT("ccF..bbbbbbbbbbbbbbbbbbbbbbbbb"),
			TEXT(".c....bbbbbbbbbbbbbbbbbbbbbb"),
			TEXT(".cc......FFf.........FFf"),
			TEXT("..c......Ff...........Ff"),
		};
		// Призрачный скат: a manta with spread wings, half transparent, a smoky tail
		static const TCHAR* GhostRay[] = {
			TEXT(".........................BB"),
			TEXT(".......................BBBB"),
			TEXT(".....................BBBBBB"),
			TEXT("...................BBBBBBBBB"),
			TEXT(".................BBBBBBBBBBBB....F"),
			TEXT("...............BBBBBBBBBBBBBBB..FF"),
			TEXT("xx.xxx.xxxxxxxBBBBBBBBBBBBBBBBBBBWEB"),
			TEXT("..x..x.......bbbbbbbbbbbbbbbbbbbbEEo"),
			TEXT("...............bbbbbbbbbbbbbbbb.FF"),
			TEXT(".................FFFFffffFFF....F"),
			TEXT("...................FFFFfff"),
			TEXT(".....................FFf"),
		};
		// Слепой патриарх: an ancient bone-coloured catfish, scars, very long whiskers
		static const TCHAR* Patriarch[] = {
			TEXT("...............fFFf"),
			TEXT("..............fFFFFf"),
			TEXT("FF.........BBBBBBBBBBBBBBBB"),
			TEXT("FFF....BBBBBBBBdBBBBBBBBBBBBBB"),
			TEXT("FFFFBBBBBBBBBBBBBdBBBBBBBBBBBBBB"),
			TEXT("FFFFBBBBBdBBBBBBBBBBBBBBBBBBBddBB"),
			TEXT("FFFFbbbbbbdbbbbbbbbbbbbbbbbbbbbbbo"),
			TEXT("FFF.bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbxx"),
			TEXT("FF...bbbbbbbbbbbbbbbbbbbbbbbbbbb.x..xx"),
			TEXT("........FFf........FFf......x.x.....x"),
			TEXT(".........Ff.........Ff.....x...x"),
			TEXT("..........................x.....x"),
			TEXT(".........................x"),
		};
		// Каменный великан: a body of stone plates (d seams), moss, amber glowing cracks
		static const TCHAR* Giant[] = {
			TEXT("............mm.m"),
			TEXT("..........FFFFFFFf"),
			TEXT(".........FFfFFfFFFf"),
			TEXT("FF.....BBBBBdBBBBBBdBBBBB"),
			TEXT("FFF..BBBBBBBdBBBBBBBdBBBBBBB"),
			TEXT("FFFFBBBBBBBBdBBBBaBBBdBBBBBBBB"),
			TEXT("FFFFddddddddddddaddddddBBBBdWEBB"),
			TEXT("FFFFBBBBBBdBBBBBBBaBBBdBBBBdEEBh"),
			TEXT("FFFFbbbbbbdbbbbbbbbbbbdbbbbdbbbo"),
			TEXT("FFF.bbbbbbdbbbbbbabbbbdbbbbbbbb"),
			TEXT("FF...bbbbbdbbbbbbbbbbbdbbbbbb"),
			TEXT(".......bbbbbbbbbbbbbbbbbbb"),
			TEXT("........FFf.....m.FFf"),
			TEXT("........Ff.........Ff"),
		};
		// Хрустальный дракон: a horned head, a crest of crystal shards, rainbow sparkles after the outline
		static const TCHAR* Dragon[] = {
			TEXT("...........Y...Y...Y.............hh"),
			TEXT("..........YF..YF..YF..Y.........h.h"),
			TEXT(".........YFF.YFF.YFF.YF........hB"),
			TEXT("F.......BBBBBBBBBBBBBBBBBBB...hBB"),
			TEXT("FF...BBBBBBBBBBBBBBBBBBBBBBBBBBBBB"),
			TEXT(".FFBBBBBBBBBBBBBBBBBBBBBBBBBBBBdWEBB"),
			TEXT("..FBBBBBBBBBBBBBBBBBBBBBBBBBBBBdEEBBh"),
			TEXT(".FFbbbbbbbbbbbbbbbbbbbbbbbbbbbbdbbbbo"),
			TEXT("FF..bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
			TEXT("F......bbbbbbbbbbbbbbbbbbbbbbbb"),
			TEXT(".........FFf.........FFf"),
			TEXT("..........Ff..........Ff"),
		};
		static const FTmpl T[] = {
			FV_TMPL(Marlin, 6, 28), FV_TMPL(Harbor, 5, 25), FV_TMPL(GhostRay, 14, 30), FV_TMPL(Patriarch, 5, 29),
			FV_TMPL(Giant, 5, 26), FV_TMPL(Dragon, 4, 29),
		};
		const int32 I = int32(L) - 1;
		return (I >= 0 && I < int32(UE_ARRAY_COUNT(T))) ? &T[I] : nullptr;
	}
	/** Update 6 (§18, MILL-ITEMS): the classic legends drawn here (FFVFishDef::Legend, Shape stays Legend).
	    «Хозяин омута» 42x17 (+2 outline = 44x19, <= 18 template rows for the held-fish card): a huge old water-spirit
	    catfish — flat broad head, amber eyes, a wide mouth, two pairs of long whiskers, moss on the back and the
	    dorsal fin, a long anal fin and a rounded tail. */
	const FTmpl* ClassicLegendTemplate(EFVLegend L)
	{
		static const TCHAR* MillMaster[] = {
			TEXT("..................m.m..m"),
			TEXT(".................mFmFmFm"),
			TEXT("...............FfFFfFFff"),
			TEXT("FF.......m.mBBBBBBBBBBBBBBmm.m"),
			TEXT("FFF....BBBBBBBdBBBBBhhBBBBBBBBBBBB"),
			TEXT("FFFF.BBBBBBBBBBBBdBBBBBBBBBBdBBBBBBBB"),
			TEXT("FFFFBBBBBBdBBBBBBBBBBBBBBBBBBBBBaaBBBhh"),
			TEXT("FFFFBBBBBBBBBBBBBBBBBBBdBBBBBBBBdaaBBBBB"),
			TEXT("FFFFbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbo"),
			TEXT("FFFFbbbbbbbbbbbbbbbbbbbbbbbbbbbbboooooooo"),
			TEXT("FFFfbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbxx"),
			TEXT("FFf..bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.x..x"),
			TEXT("......FFfFFfFFfFFfFFfFbbbbbbbbbb...x....x"),
			TEXT(".......FfFfFfFfFfFfFf....FFf.....x......x"),
			TEXT(".........................Ff.....x.......x"),
			TEXT("...............................x.........x"),
			TEXT("..............................x"),
		};
		static const FTmpl Master = FV_TMPL(MillMaster, 6, 30);
		return L == EFVLegend::MillMaster ? &Master : nullptr;
	}
#undef FV_TMPL

	bool IsBodyChar(TCHAR C)
	{
		return C == TEXT('B') || C == TEXT('b') || C == TEXT('h') || C == TEXT('d') || C == TEXT('E') || C == TEXT('W') || C == TEXT('w') || C == TEXT('L');
	}
	bool IsPostChar(TCHAR C)
	{
		return C == TEXT('x') || C == TEXT('r') || C == TEXT('c') || C == TEXT('s') || C == TEXT('a') || C == TEXT('k') || C == TEXT('m');
	}

	uint32 SeedOf(const FFVFishDef& Def)
	{
		const FString S = Def.Id.ToString();
		uint32 H = 2166136261u;
		for (int32 I = 0; I < S.Len(); ++I) { H = (H ^ uint32(S[I])) * 16777619u; }
		return H;
	}

	bool IsBlind(const FFVFishDef& Def)
	{
		return Def.ExpertLegend == EFVExpertLegend::BlindPatriarch || Def.Id.ToString().Contains(TEXT("blind"))
			|| Def.Shape == EFVFishShape::Proteus;
	}

	FFVImage Render(const FFVFishDef& Def, const FTmpl& T, bool bSilhouette)
	{
		TArray<FString> Rows;
		int32 TW = 0;
		for (int32 I = 0; I < T.NumRows; ++I)
		{
			Rows.Add(FString(T.Rows[I]));
			TW = FMath::Max(TW, Rows.Last().Len());
		}
		const int32 TH = Rows.Num();
		const bool bBlind = IsBlind(Def);
		auto At = [&](int32 X, int32 Y) -> TCHAR
		{
			if (Y < 0 || Y >= TH || X < 0 || X >= Rows[Y].Len()) { return TEXT('.'); }
			TCHAR C = Rows[Y][X];
			if (bBlind && (C == TEXT('E') || C == TEXT('W') || C == TEXT('w'))) { C = TEXT('B'); }
			return C;
		};
		TArray<int32> ColTop, ColBot;
		ColTop.Init(-1, TW);
		ColBot.Init(-1, TW);
		for (int32 X = 0; X < TW; ++X)
		{
			for (int32 Y = 0; Y < TH; ++Y)
			{
				if (IsBodyChar(At(X, Y))) { if (ColTop[X] < 0) { ColTop[X] = Y; } ColBot[X] = Y; }
			}
		}

		const FColor Body = Def.BodyColor, Belly = Def.BellyColor, Fin = Def.FinColor, Pat = Def.PatternColor;
		const bool bGlow = (Def.Pattern == EFVFishPattern::Glow || Def.Pattern == EFVFishPattern::Shimmer) && !bSilhouette;
		FColor OutlineC = FVArt::OutlineOf(Body);
		if (bGlow) { OutlineC = FVArt::Mix(OutlineC, Pat, 0.3f); }
		const FColor SilFill(62, 70, 96), SilLine(38, 44, 64);
		const FColor Detail = bSilhouette ? SilLine : FVArt::Mix(OutlineC, Body, 0.25f);
		const uint32 Seed = SeedOf(Def);
		const int32 Period = TW >= 18 ? 4 : 3;
		const int32 StripeW = TW >= 24 ? 2 : 1;

		FFVImage Img(TW + 2, TH + 2);
		TArray<uint8> Spot;
		Spot.Init(0, TW * TH);
		for (int32 Y = 0; Y < TH; ++Y)
		{
			for (int32 X = 0; X < TW; ++X)
			{
				const TCHAR Ch = At(X, Y);
				if (Ch == TEXT('.') || Ch == TEXT(' ') || IsPostChar(Ch)) { continue; }
				if (bSilhouette) { Img.Set(X + 1, Y + 1, SilFill); continue; }
				const int32 Top = ColTop[X], Bot = ColBot[X];
				const float Tt = (Top >= 0 && Bot > Top) ? float(Y - Top) / float(Bot - Top) : 0.5f;
				FColor C = Body;
				switch (Ch)
				{
				case TEXT('B'):
					if (Y == Top && Bot - Top >= 2) { C = FVArt::Shade(Body, 0.34f); }
					else if (Tt < 0.34f) { C = FVArt::Shade(Body, 0.14f); }
					else if (Tt < 0.62f) { C = Body; }
					else { C = FVArt::Mix(Body, Belly, 0.22f); }
					break;
				case TEXT('b'): C = (Y == Bot && Bot - Top >= 3) ? FVArt::Shade(Belly, 0.16f) : Belly; break;
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
				default: break;
				}
				const bool bPatArea = X >= T.PatX0 && X <= T.PatX1 && (Ch == TEXT('B') || Ch == TEXT('b'));
				if (bPatArea)
				{
					switch (Def.Pattern)
					{
					case EFVFishPattern::Stripes:
						if (Ch == TEXT('B') && ((X - T.PatX0 + (Y / 3)) % Period) < StripeW && Tt < 0.9f)
						{
							C = (Y == Top) ? FVArt::Shade(Pat, 0.25f) : Pat;
						}
						break;
					case EFVFishPattern::Spots:
					{
						const bool bN = (X > 0 && Spot[Y * TW + X - 1]) || (Y > 0 && Spot[(Y - 1) * TW + X]) ||
							(X > 0 && Y > 0 && Spot[(Y - 1) * TW + X - 1]) || (X + 1 < TW && Y > 0 && Spot[(Y - 1) * TW + X + 1]);
						if (!bN && FVArt::Noise(X, Y, Seed) < 0.2f && Y != Top)
						{
							Spot[Y * TW + X] = 1;
							C = Ch == TEXT('b') ? FVArt::Mix(Pat, Belly, 0.45f) : Pat;
						}
						break;
					}
					case EFVFishPattern::Glow:
						if (FVArt::Noise(X, Y, Seed ^ 0x5bd1e995u) < 0.1f) { C = FVArt::Light(Pat, 0.55f); }
						break;
					case EFVFishPattern::Scales:
						// big scale rows: a dark arc under a light rim, offset every other row
						if (Y != Top && ((X + (Y % 2) * 2) % 4) == 0) { C = FVArt::Mix(C, Pat, 0.25f); C = FVArt::Shade(C, 0.18f); }
						else if (Y != Top && ((X + (Y % 2) * 2) % 4) == 1) { C = FVArt::Mix(C, Pat, 0.3f); }
						break;
					case EFVFishPattern::Shimmer:
						if (FVArt::Noise(X, Y, Seed ^ 0x2c1b3c6du) < 0.16f) { C = FVArt::Mix(C, Pat, 0.7f); }
						else if (((X + Y) % 5) == 0) { C = FVArt::Light(C, 0.18f); }
						break;
					default: break;
					}
				}
				if (bGlow && (Ch == TEXT('B') || Ch == TEXT('F') || Ch == TEXT('f')))
				{
					const TCHAR Up = At(X, Y - 1);
					if (!IsBodyChar(Up) && Up != TEXT('F') && Up != TEXT('f')) { C = FVArt::Light(Pat, 0.3f); }
				}
				if (bBlind && Ch == TEXT('B') && Rows[Y].IsValidIndex(X) && (Rows[Y][X] == TEXT('E') || Rows[Y][X] == TEXT('W') || Rows[Y][X] == TEXT('w')))
				{
					C = FVArt::Mix(FVArt::Shade(Body, 0.2f), FColor(236, 236, 240), 0.35f);   // a milky pit instead of an eye
				}
				Img.Set(X + 1, Y + 1, C);
			}
		}
		Img.Outline(bSilhouette ? SilLine : OutlineC);

		// thin details after the outline
		for (int32 Y = 0; Y < TH; ++Y)
		{
			for (int32 X = 0; X < TW; ++X)
			{
				const TCHAR Ch = At(X, Y);
				if (!IsPostChar(Ch)) { continue; }
				FColor C = Detail;
				if (!bSilhouette)
				{
					switch (Ch)
					{
					case TEXT('r'): C = FColor(178, 90, 50); break;
					case TEXT('c'): C = FColor(150, 152, 166); break;
					case TEXT('s'): C = FColor(226, 216, 192); break;
					case TEXT('a'): C = FColor(255, 170, 60); break;
					case TEXT('k'): C = FColor(255, 255, 255); break;
					case TEXT('m'): C = FColor(96, 140, 70); break;
					default: break;
					}
				}
				else if (Ch == TEXT('k')) { continue; }
				Img.Set(X + 1, Y + 1, C);
			}
		}
		if (bSilhouette) { return Img; }

		// legend finishing touches
		FRandomStream Rng(int32(Seed & 0x7fffffff));
		auto Opaque = [&Img](int32 X, int32 Y) { return Img.Get(X, Y).A > 0; };
		switch (Def.ExpertLegend)
		{
		case EFVExpertLegend::GhostRay:
			for (FColor& P : Img.Px) { if (P.A > 0) { P.A = uint8(FMath::Min<int32>(P.A, 200)); } }
			break;
		case EFVExpertLegend::CrystalDragon:
		{
			static const FColor Rainbow[5] = { FColor(255, 150, 200), FColor(255, 230, 140), FColor(150, 255, 200), FColor(140, 200, 255), FColor(210, 160, 255) };
			for (int32 k = 0; k < 14; ++k)
			{
				const int32 X = Rng.RandRange(1, Img.W - 2), Y = Rng.RandRange(1, Img.H - 2);
				if (Opaque(X, Y)) { Img.Set(X, Y, Rainbow[k % 5]); }
			}
			break;
		}
		case EFVExpertLegend::StoneGiant:
			for (int32 k = 0; k < 8; ++k)
			{
				const int32 X = Rng.RandRange(4, Img.W - 8), Y = Rng.RandRange(2, 5);
				if (Opaque(X, Y) && !Opaque(X, Y - 1)) { Img.Set(X, Y, FColor(96, 140, 70)); Img.Set(X, Y - 1, FColor(120, 170, 84)); }
			}
			break;
		default: break;
		}
		if (Def.Legend == EFVLegend::MillMaster)
		{
			// moss tufts along the back and pale water-weed specks on the flank; old scars
			for (int32 k = 0; k < 10; ++k)
			{
				const int32 X = Rng.RandRange(6, Img.W - 12), Y = Rng.RandRange(3, 7);
				if (Opaque(X, Y) && !Opaque(X, Y - 1)) { Img.Set(X, Y, FColor(92, 138, 64)); Img.Set(X, Y - 1, FColor(126, 172, 80)); }
			}
			for (int32 k = 0; k < 7; ++k)
			{
				const int32 X = Rng.RandRange(6, Img.W - 12), Y = Rng.RandRange(5, 9);
				if (Opaque(X, Y) && Opaque(X + 1, Y)) { Img.Set(X, Y, FColor(150, 176, 104)); }
			}
			for (int32 k = 0; k < 3; ++k)
			{
				const int32 X = Rng.RandRange(10, Img.W - 16), Y = Rng.RandRange(6, 8);
				if (Opaque(X, Y) && Opaque(X + 1, Y + 1)) { Img.Set(X, Y, FColor(196, 188, 150)); Img.Set(X + 1, Y + 1, FColor(196, 188, 150)); }
			}
		}
		// Shimmer (mica / crystal): tiny sparks over the body
		if (Def.Pattern == EFVFishPattern::Shimmer)
		{
			for (int32 k = 0; k < Img.W / 3; ++k)
			{
				const int32 X = Rng.RandRange(2, Img.W - 3), Y = Rng.RandRange(2, Img.H - 3);
				if (Opaque(X, Y) && Opaque(X - 1, Y) && Opaque(X + 1, Y)) { Img.Set(X, Y, FColor(255, 255, 250)); }
			}
		}
		return Img;
	}
}

FFVImage FVArtFishSide::BuildFish(const FFVFishDef& Def, bool bSilhouette)
{
	using namespace FVArtFishSideImpl;
	if (const FTmpl* CL = ClassicLegendTemplate(Def.Legend)) { return Render(Def, *CL, bSilhouette); }
	const FTmpl* T = Def.Shape == EFVFishShape::ExpertLegend ? LegendTemplate(Def.ExpertLegend) : ShapeTemplate(Def.Shape);
	if (!T && Def.Shape == EFVFishShape::ExpertLegend) { T = LegendTemplate(EFVExpertLegend::KingMarlin); }
	if (!T) { return FFVImage(); }
	return Render(Def, *T, bSilhouette);
}
