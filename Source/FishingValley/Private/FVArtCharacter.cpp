// Character art: the player (layered: legs, torso per outfit style, arms, head, hat, rod), hat & outfit
// icons, bobbers and pets. Everything is built from small hand-drawn ASCII parts and cached by UFVSprites.
//
// Character canvas: 32x48 texels, feet (bottom edge of the soles) at (16,46), head top at (16,14).
//   head  y14..26 (x 9..22)   torso y27..35 (x 11..20)   arms x 9..10 / 21..22   pants y36..42   shoes y43..45

#include "FVSprites.h"

#define FV_TPL(A) A, int32(UE_ARRAY_COUNT(A))

namespace
{
	// ============================================================================================
	// Helpers
	// ============================================================================================
	constexpr int32 CharW = 32;
	constexpr int32 CharH = 48;

	/** Draws ASCII rows at (X,Y) (overwrite, unknown chars and '.' are skipped). Optional horizontal mirror. */
	void Stamp(FFVImage& Img, int32 X, int32 Y, const TCHAR* const* Rows, int32 N, const TMap<TCHAR, FColor>& Pal, bool bFlip = false)
	{
		int32 MaxLen = 0;
		if (bFlip)
		{
			for (int32 R = 0; R < N; ++R)
			{
				MaxLen = FMath::Max(MaxLen, (int32)FCString::Strlen(Rows[R]));
			}
		}
		for (int32 R = 0; R < N; ++R)
		{
			const TCHAR* Row = Rows[R];
			const int32 Len = (int32)FCString::Strlen(Row);
			for (int32 C = 0; C < Len; ++C)
			{
				const TCHAR Ch = Row[C];
				if (Ch == TEXT('.') || Ch == TEXT(' '))
				{
					continue;
				}
				if (const FColor* Col = Pal.Find(Ch))
				{
					if (Col->A > 0)
					{
						Img.Set(bFlip ? X + (MaxLen - 1 - C) : X + C, Y + R, *Col);
					}
				}
			}
		}
	}

	int32 TplWidth(const TCHAR* const* Rows, int32 N)
	{
		int32 W = 0;
		for (int32 R = 0; R < N; ++R)
		{
			W = FMath::Max(W, (int32)FCString::Strlen(Rows[R]));
		}
		return W;
	}

	/** 2px-thick line (the second pixel below or beside depending on the slope). */
	void ThickLine(FFVImage& Img, int32 X0, int32 Y0, int32 X1, int32 Y1, FColor C, FColor C2)
	{
		const bool bSteep = FMath::Abs(Y1 - Y0) > FMath::Abs(X1 - X0);
		const int32 DX = FMath::Abs(X1 - X0), SX = X0 < X1 ? 1 : -1;
		const int32 DY = -FMath::Abs(Y1 - Y0), SY = Y0 < Y1 ? 1 : -1;
		int32 Err = DX + DY;
		int32 X = X0, Y = Y0;
		for (int32 Guard = 0; Guard < 256; ++Guard)
		{
			Img.Set(X, Y, C);
			if (bSteep) { Img.Set(X + 1, Y, C2); }
			else { Img.Set(X, Y + 1, C2); }
			if (X == X1 && Y == Y1)
			{
				break;
			}
			const int32 E2 = 2 * Err;
			if (E2 >= DY) { Err += DY; X += SX; }
			if (E2 <= DX) { Err += DX; Y += SY; }
		}
	}

	// ============================================================================================
	// Character palette
	// ============================================================================================
	struct FCharStyle
	{
		TMap<TCHAR, FColor> Pal;      // body parts
		TMap<TCHAR, FColor> HatPal;   // hat
		FColor Outline;
		FColor Sleeve, SleeveS;
		FColor Skin, SkinS;
		bool bShortSleeves = false;
		bool bLongCoat = false;
	};

	FCharStyle MakeStyle(const FFVAppearance& L)
	{
		FCharStyle S;
		S.Outline = FColor(56, 30, 42);
		S.Skin = FColor(252, 206, 162);
		S.SkinS = FColor(228, 160, 122);

		const FColor Prim = L.OutfitPrimary, Sec = L.OutfitSecondary;
		FColor Pants(70, 94, 150), Shoes(118, 72, 44);
		S.Sleeve = Prim;
		switch (L.Outfit)
		{
		case EFVOutfitStyle::Overalls: Pants = Prim; S.Sleeve = Sec; S.bShortSleeves = true; break;
		case EFVOutfitStyle::Shirt:    Pants = FColor(70, 94, 150); S.Sleeve = Prim; S.bShortSleeves = true; break;
		case EFVOutfitStyle::Hoodie:   Pants = FColor(84, 88, 108); Shoes = FColor(226, 224, 216); break;
		case EFVOutfitStyle::Vest:     Pants = FColor(142, 114, 80); Shoes = FColor(100, 62, 40); S.Sleeve = Sec; break;
		case EFVOutfitStyle::Raincoat: Pants = FColor(78, 78, 96); Shoes = Prim; S.bLongCoat = true; break;
		case EFVOutfitStyle::Sailor:   Pants = Prim; Shoes = FColor(70, 56, 64); S.Sleeve = Sec; S.bShortSleeves = true; break;
		case EFVOutfitStyle::Suit:     Pants = Prim; Shoes = FColor(64, 44, 42); break;
		// update 6 [ART-WEAR]
		case EFVOutfitStyle::Flannel:        Pants = FColor(70, 94, 150); Shoes = FColor(118, 72, 44); S.Sleeve = Prim; break;
		case EFVOutfitStyle::LumberVest:     Pants = FColor(96, 84, 70); Shoes = FColor(100, 62, 40); S.Sleeve = Sec; break;
		case EFVOutfitStyle::CaptainCoat:    Pants = FColor(40, 42, 60); Shoes = FColor(50, 40, 44); S.bLongCoat = true; break;
		case EFVOutfitStyle::AdmiralUniform: Pants = Prim; Shoes = FColor(40, 34, 40); S.bLongCoat = true; break;
		case EFVOutfitStyle::LegendCloak:    Pants = FColor(60, 50, 70); Shoes = FColor(70, 50, 40); S.bLongCoat = true; break;
		// update 6 (Р64): boutique expert collection
		case EFVOutfitStyle::RodMaster:      Pants = FColor(36, 40, 50); Shoes = FColor(62, 42, 36); S.bLongCoat = true; break;
		case EFVOutfitStyle::MantaCape:      Pants = FColor(40, 46, 70); Shoes = FColor(48, 40, 52); S.bLongCoat = true; break;
		default: break;
		}
		S.SleeveS = FVArt::Shade(S.Sleeve, 0.3f);

		TMap<TCHAR, FColor>& P = S.Pal;
		P.Add(TEXT('o'), S.Outline);
		P.Add(TEXT('K'), S.Skin);
		P.Add(TEXT('k'), S.SkinS);
		P.Add(TEXT('j'), FColor(244, 150, 132));
		P.Add(TEXT('H'), FColor(138, 82, 46));
		P.Add(TEXT('h'), FColor(100, 56, 34));
		P.Add(TEXT('L'), FColor(184, 120, 64));
		P.Add(TEXT('E'), FColor(40, 28, 48));
		P.Add(TEXT('W'), FColor(255, 255, 255));
		P.Add(TEXT('S'), Prim);
		P.Add(TEXT('s'), FVArt::Shade(Prim, 0.3f));
		P.Add(TEXT('Z'), FVArt::Light(Prim, 0.28f));
		P.Add(TEXT('T'), Sec);
		P.Add(TEXT('t'), FVArt::Shade(Sec, 0.3f));
		P.Add(TEXT('X'), FVArt::Light(Sec, 0.3f));
		P.Add(TEXT('P'), Pants);
		P.Add(TEXT('p'), FVArt::Shade(Pants, 0.32f));
		P.Add(TEXT('B'), Shoes);
		P.Add(TEXT('b'), FVArt::Shade(Shoes, 0.38f));
		P.Add(TEXT('Y'), FColor(250, 206, 72));
		P.Add(TEXT('y'), FColor(204, 142, 40));
		P.Add(TEXT('e'), FColor(92, 54, 36));
		P.Add(TEXT('r'), FColor(216, 58, 58));
		P.Add(TEXT('R'), FColor(248, 124, 112));
		P.Add(TEXT('w'), FColor(248, 246, 240));
		P.Add(TEXT('A'), S.Sleeve);
		P.Add(TEXT('a'), S.SleeveS);
		// update 6 [ART-WEAR]: gold braid, plaid crossing, sparkle
		P.Add(TEXT('G'), FColor(255, 212, 80));
		P.Add(TEXT('g'), FColor(200, 136, 36));
		P.Add(TEXT('m'), FVArt::Mix(Prim, Sec, 0.5f));
		P.Add(TEXT('Q'), FColor(255, 252, 224));

		TMap<TCHAR, FColor>& H = S.HatPal;
		H.Add(TEXT('o'), S.Outline);
		H.Add(TEXT('A'), L.HatPrimary);
		H.Add(TEXT('a'), FVArt::Shade(L.HatPrimary, 0.3f));
		H.Add(TEXT('Z'), FVArt::Light(L.HatPrimary, 0.3f));
		H.Add(TEXT('C'), L.HatSecondary);
		H.Add(TEXT('c'), FVArt::Shade(L.HatSecondary, 0.3f));
		H.Add(TEXT('X'), FVArt::Light(L.HatSecondary, 0.35f));
		H.Add(TEXT('G'), FColor(255, 212, 80));
		H.Add(TEXT('g'), FColor(206, 142, 40));
		H.Add(TEXT('w'), FColor(250, 248, 240));
		H.Add(TEXT('k'), FColor(40, 30, 44));
		H.Add(TEXT('R'), FColor(230, 50, 70));
		H.Add(TEXT('B'), FColor(70, 150, 240));
		return S;
	}

	// ============================================================================================
	// Heads (14 wide at x=9; side head 15 wide incl. nose)
	// ============================================================================================
	const TCHAR* const HeadDown[] = {
		TEXT("...HHHHHHHH..."),
		TEXT("..HLLLHHHHHH.."),
		TEXT(".HLLHHHHHHHHh."),
		TEXT("HHLHHHHHHHHHHh"),
		TEXT("HHHHHHHHHHHHHh"),
		TEXT("HHHHKHHHHKHHHh"),
		TEXT("HHKKKKKKKKKKHh"),
		TEXT("HHKKKKKKKKKKHh"),
		TEXT("HKKKEKKKKEKKKh"),
		TEXT("HKKKEKKKKEKKKh"),
		TEXT("hKjKKKKKKKKjKh"),
		TEXT(".kKKKKKKKKKKk."),
		TEXT("..kkKKKKKKkk.."),
	};
	const TCHAR* const HeadUp[] = {
		TEXT("...HHHHHHHH..."),
		TEXT("..HLLLHHHHHH.."),
		TEXT(".HLLHHHHHHHHh."),
		TEXT("HHLHHHHHHHHHHh"),
		TEXT("HHHHHHHHHHHHHh"),
		TEXT("HHHHHHHHHHHHhh"),
		TEXT("HHHHHHHHHHHHHh"),
		TEXT("HHHHHHHHHHHHhh"),
		TEXT("hHHHHHHHHHHHhh"),
		TEXT("hHHHHHHHHHHhhh"),
		TEXT(".hhHHHHHHHhhh."),
		TEXT("..hkKKKKKKkh.."),
		TEXT("...kkKKKKkk..."),
	};
	const TCHAR* const HeadRight[] = {
		TEXT("....HHHHHHH...."),
		TEXT("..HHLLLHHHHH..."),
		TEXT(".HHLLHHHHHHHH.."),
		TEXT("HHLHHHHHHHHHHH."),
		TEXT("HHHHHHHHHHHHKH."),
		TEXT("HHHHHHHHHKKKKK."),
		TEXT("HHHHHHHKKKKKKK."),
		TEXT("hHHHHHkKKKKEKK."),
		TEXT("hHHHHKjKKKKEKKK"),
		TEXT("hhHHHKKKKKKKKK."),
		TEXT(".hhHHKKKKKKjKK."),
		TEXT("..hhkKKKKKKKk.."),
		TEXT("....kkKKKKkk..."),
	};

	// ============================================================================================
	// Torsos: 9 rows (y27..35). Down/Up are 10 wide at x=11, Right is 8 wide at x=12.
	// ============================================================================================
	// --- Overalls
	const TCHAR* const OverallsDown[] = {
		TEXT("TTTTkkTTTt"),
		TEXT("TSTTTTTTSt"),
		TEXT("TSSSSSSSSt"),
		TEXT("TYSSSSSSYt"),
		TEXT("TSSZZZZSSt"),
		TEXT("TSSSSSSSSt"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
	};
	const TCHAR* const OverallsUp[] = {
		TEXT("TTTTTTTTTt"),
		TEXT("TTSTTTTSTt"),
		TEXT("TTTSTTSTTt"),
		TEXT("TTTTSSTTTt"),
		TEXT("TTTSTTSTTt"),
		TEXT("TTSTTTTSTt"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
	};
	const TCHAR* const OverallsRight[] = {
		TEXT("TTTTTTkT"),
		TEXT("TTTTSTTt"),
		TEXT("TTTTSSSs"),
		TEXT("TTTTSYSs"),
		TEXT("TTTTSSSs"),
		TEXT("TTTTSSSs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSSs"),
	};
	// --- Shirt
	const TCHAR* const ShirtDown[] = {
		TEXT("SSTTkkTTSs"),
		TEXT("SSSTkkTSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSYSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSYSSSSs"),
		TEXT("ssssssssss"),
		TEXT("eeeeYyeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const ShirtUp[] = {
		TEXT("SSSTTTTSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("ssssssssss"),
		TEXT("eeeeeeeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const ShirtRight[] = {
		TEXT("SSSSTTkT"),
		TEXT("SSSSSTSs"),
		TEXT("SZSSSSSs"),
		TEXT("SSSSSSYs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSYs"),
		TEXT("ssssssss"),
		TEXT("eeeeeeYe"),
		TEXT("PPPPPPPp"),
	};
	// --- Hoodie
	const TCHAR* const HoodieDown[] = {
		TEXT("sSSSkkSSSs"),
		TEXT("SsSSssSSsS"),
		TEXT("SZSSTSTSSs"),
		TEXT("SSSSTSTSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSssssssSs"),
		TEXT("SSsSSSSsSs"),
		TEXT("ssssssssss"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const HoodieUp[] = {
		TEXT("SSsSSSSsSs"),
		TEXT("SSsSSSSsSs"),
		TEXT("SSSsSSsSSs"),
		TEXT("SSSSssSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("ssssssssss"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const HoodieRight[] = {
		TEXT("ssSSSSkS"),
		TEXT("sSSSSSSs"),
		TEXT("sSSSSSTs"),
		TEXT("SSSSSSTs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSssss"),
		TEXT("SSSSSSSs"),
		TEXT("ssssssss"),
		TEXT("PPPPPPPp"),
	};
	// --- Vest (vest = primary, shirt = secondary)
	const TCHAR* const VestDown[] = {
		TEXT("SSTTkkTTSs"),
		TEXT("SSSTTTTSSs"),
		TEXT("SZSTTTTSSs"),
		TEXT("sssTTTTsss"),
		TEXT("SZSTTTTSSs"),
		TEXT("sssTTTTsss"),
		TEXT("SSSTTTTSSs"),
		TEXT("eeeeYyeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const VestUp[] = {
		TEXT("SSSTTTTSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSsssSSSs"),
		TEXT("SSSsTsSSSs"),
		TEXT("SSSsssSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("eeeeeeeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const VestRight[] = {
		TEXT("SSSSTTkT"),
		TEXT("SSSSSSTT"),
		TEXT("SZSSSSTT"),
		TEXT("SSsssSTT"),
		TEXT("SSSSSSTT"),
		TEXT("SSsssSTT"),
		TEXT("SSSSSSTt"),
		TEXT("eeeeeeee"),
		TEXT("PPPPPPPp"),
	};
	// --- Raincoat (long: continues over the legs, see RaincoatSkirt*)
	const TCHAR* const RaincoatDown[] = {
		TEXT("ZSSSkkSSSs"),
		TEXT("SZSsSSsSSs"),
		TEXT("SSSSsTSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsTSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsTSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsSSSSs"),
	};
	const TCHAR* const RaincoatUp[] = {
		TEXT("SSsssssSSs"),
		TEXT("SsSSSSSsSs"),
		TEXT("SSsSSSsSSs"),
		TEXT("SSSsssSSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
	};
	const TCHAR* const RaincoatRight[] = {
		TEXT("ZSSSSSkS"),
		TEXT("SSSSSSSs"),
		TEXT("SZSSSSTs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSTs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSTs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSSs"),
	};
	const TCHAR* const RaincoatSkirtDown[] = {
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("ssssssssss"),
	};
	const TCHAR* const RaincoatSkirtUp[] = {
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("ssssssssss"),
	};
	const TCHAR* const RaincoatSkirtRight[] = {
		TEXT("SSSSSSSs."),
		TEXT("SSSSSSSSs"),
		TEXT("SSSSSSSSs"),
		TEXT("sssssssss"),
	};
	// --- Sailor (shirt = secondary, collar & trousers = primary, red kerchief)
	const TCHAR* const SailorDown[] = {
		TEXT("SSSKkkKSSs"),
		TEXT("WSSSKkSSSW"),
		TEXT("TWSSrrSSWt"),
		TEXT("TTWSrRSWTt"),
		TEXT("TTTTrrTTTt"),
		TEXT("TTTTrTTTTt"),
		TEXT("TTTTTTTTTt"),
		TEXT("tttttttttt"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const SailorUp[] = {
		TEXT("SSSSSSSSSs"),
		TEXT("SWWWWWWWWs"),
		TEXT("SWSSSSSSWs"),
		TEXT("SWSSSSSSWs"),
		TEXT("SWWWWWWWWs"),
		TEXT("TTTTTTTTTt"),
		TEXT("TTTTTTTTTt"),
		TEXT("tttttttttt"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const SailorRight[] = {
		TEXT("SSSSSSKk"),
		TEXT("SWWWWSrT"),
		TEXT("SSSSSrRT"),
		TEXT("TTTTTTrt"),
		TEXT("TTTTTTTt"),
		TEXT("TTTTTTTt"),
		TEXT("TTTTTTTt"),
		TEXT("tttttttt"),
		TEXT("PPPPPPPp"),
	};
	// --- Suit (jacket & trousers = primary, tie = secondary, white shirt)
	const TCHAR* const SuitDown[] = {
		TEXT("SSwwkkwwSs"),
		TEXT("SSSwTTwSSs"),
		TEXT("SZSSwTwSSs"),
		TEXT("SSSSwTSSSs"),
		TEXT("SSSSSTSSSs"),
		TEXT("SSSSYSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSYSSSSs"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const SuitUp[] = {
		TEXT("SSSwwwwSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const SuitRight[] = {
		TEXT("SSSSSwwk"),
		TEXT("SSSSSSwT"),
		TEXT("SZSSSSST"),
		TEXT("SSSSSSST"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSYs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSYs"),
		TEXT("PPPPPPPp"),
	};

	// --- update 6 [ART-WEAR] Flannel (plaid: primary cloth, secondary check lines)
	const TCHAR* const FlannelDown[] = {
		TEXT("SSTSkkSTSs"),
		TEXT("SZTSwSSTSs"),
		TEXT("TTmTTTTmTt"),
		TEXT("SSTSwSSTSs"),
		TEXT("SZTSSSSTSs"),
		TEXT("SSTSwSSTSs"),
		TEXT("TTmTTTTmTt"),
		TEXT("eeeeYyeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const FlannelUp[] = {
		TEXT("SSTSSSSTSs"),
		TEXT("SZTSSSSTSs"),
		TEXT("TTmTTTTmTt"),
		TEXT("SSTSSSSTSs"),
		TEXT("SZTSSSSTSs"),
		TEXT("SSTSSSSTSs"),
		TEXT("TTmTTTTmTt"),
		TEXT("eeeeeeeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const FlannelRight[] = {
		TEXT("SSTSSSkS"),
		TEXT("SZTSSSwS"),
		TEXT("TTmTTTTt"),
		TEXT("SSTSSSwS"),
		TEXT("SZTSSSSs"),
		TEXT("SSTSSSwS"),
		TEXT("TTmTTTTt"),
		TEXT("eeeeeeee"),
		TEXT("PPPPPPPp"),
	};
	// --- LumberVest (quilted vest = primary over a checked shirt = secondary)
	const TCHAR* const LumberVestDown[] = {
		TEXT("SSTTkkTTSs"),
		TEXT("SZSmTTmSSs"),
		TEXT("SZSTmmTSSs"),
		TEXT("sssmTTmsss"),
		TEXT("SZSTmmTSYs"),
		TEXT("sssmTTmsss"),
		TEXT("SSSTmmTSSs"),
		TEXT("eeeeYyeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const LumberVestUp[] = {
		TEXT("SSSmTTmSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("ssssssssss"),
		TEXT("SZSSSSSSSs"),
		TEXT("ssssssssss"),
		TEXT("SZSSSSSSSs"),
		TEXT("ssssssssss"),
		TEXT("eeeeeeeeee"),
		TEXT("PPPPPPPPPp"),
	};
	const TCHAR* const LumberVestRight[] = {
		TEXT("SSSSTTkT"),
		TEXT("SZSSSSTm"),
		TEXT("sssssSmT"),
		TEXT("SZSSSSTm"),
		TEXT("sssssSmT"),
		TEXT("SZSSSYTm"),
		TEXT("SSSSSSmt"),
		TEXT("eeeeeeee"),
		TEXT("PPPPPPPp"),
	};
	// --- CaptainCoat (long double-breasted coat = primary, gold buttons and epaulettes, white shirt)
	const TCHAR* const CaptainCoatDown[] = {
		TEXT("GSSwkkwSSG"),
		TEXT("SZSSwwSSSs"),
		TEXT("SZGSSSSGSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SZGSSSSGSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SZGSSSSGSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsSSSSs"),
	};
	const TCHAR* const CaptainCoatUp[] = {
		TEXT("GSSSSSSSSG"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSGSSGSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSsSSSSs"),
		TEXT("SSSSsSSSSs"),
	};
	const TCHAR* const CaptainCoatRight[] = {
		TEXT("GSSSSwkS"),
		TEXT("SZSSSSwS"),
		TEXT("SSSSSSGs"),
		TEXT("SSSSSSSs"),
		TEXT("SZSSSSGs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSGs"),
		TEXT("SSSSSSSs"),
		TEXT("SSSSSSSs"),
	};
	// --- AdmiralUniform (elite: coat = primary, trim = secondary, heavy gold epaulettes, medal, gold sash)
	const TCHAR* const AdmiralUniformDown[] = {
		TEXT("GGSwkkwSGG"),
		TEXT("gQSTwwTSQg"),
		TEXT("SZGSTTSGSs"),
		TEXT("SrYSSSSGSs"),
		TEXT("SZGSSSSGSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("GGGGQGGGGg"),
		TEXT("SSSSTSSSSs"),
		TEXT("SSSSTSSSSs"),
	};
	const TCHAR* const AdmiralUniformUp[] = {
		TEXT("GGSSSSSSGG"),
		TEXT("gQSSSSSSQg"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("GGGGGGGGGg"),
		TEXT("SSSSTSSSSs"),
		TEXT("SSSSTSSSSs"),
	};
	const TCHAR* const AdmiralUniformRight[] = {
		TEXT("GGSSSwkS"),
		TEXT("gQSSSSwT"),
		TEXT("SZSSSSGT"),
		TEXT("SSSSSrYT"),
		TEXT("SZSSSSGT"),
		TEXT("SSSSSSST"),
		TEXT("GGGGGQGg"),
		TEXT("SSSSSSST"),
		TEXT("SSSSSSST"),
	};
	// --- LegendCloak (elite: cloak = primary with star sparkles, lining = secondary, gold clasp)
	const TCHAR* const LegendCloakDown[] = {
		TEXT("TZSGkkGSsT"),
		TEXT("TSZSGgSSsT"),
		TEXT("TSZSSSSSsT"),
		TEXT("TSSQSSSSsT"),
		TEXT("TSZSSSSQsT"),
		TEXT("TSSSSSSSsT"),
		TEXT("TQSSSSSSsT"),
		TEXT("TSSSSQSSsT"),
		TEXT("TSSSSSSSsT"),
	};
	const TCHAR* const LegendCloakUp[] = {
		TEXT("SSSSSSSSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSQSSSSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSSSSQSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SQSSSSSSSs"),
		TEXT("SSSSSSQSSs"),
		TEXT("SSSSSSSSSs"),
	};
	const TCHAR* const LegendCloakRight[] = {
		TEXT("SSSSSGkS"),
		TEXT("SZSSSGgT"),
		TEXT("SSSSSSsT"),
		TEXT("SSQSSSsT"),
		TEXT("SZSSSSsT"),
		TEXT("SSSSSQsT"),
		TEXT("SQSSSSsT"),
		TEXT("SSSSSSsT"),
		TEXT("SSSSSSsT"),
	};

	// --- RodMaster (Р64, elite: coat = primary, gold trim = secondary, crossed silver rods with gold tips on the chest, gold belt)
	const TCHAR* const RodMasterDown[] = {
		TEXT("TSSwkkwSST"),
		TEXT("SZSSwwSSSs"),
		TEXT("SZGSSSSGSs"),
		TEXT("SSSwSSwSSs"),
		TEXT("SZSSwwSSSs"),
		TEXT("SSSwSSwSSs"),
		TEXT("GGgGQGGgGg"),
		TEXT("SSSTSSTSSs"),
		TEXT("SSSTSsTSSs"),
	};
	const TCHAR* const RodMasterUp[] = {
		TEXT("TSSSSSSSST"),
		TEXT("SZSSSSSSSs"),
		TEXT("SZSSGGSSSs"),
		TEXT("SSSGgQGSSs"),
		TEXT("SZSSGGSSSs"),
		TEXT("SSSSSSSSSs"),
		TEXT("GGGGGGGGGg"),
		TEXT("SSSSTSSSSs"),
		TEXT("SSSSTSSSSs"),
	};
	const TCHAR* const RodMasterRight[] = {
		TEXT("TSSSSwkS"),
		TEXT("SZSSSSwT"),
		TEXT("SZSSSGST"),
		TEXT("SSSSwSST"),
		TEXT("SZSwSSST"),
		TEXT("SSSSSSST"),
		TEXT("GGGGGQGg"),
		TEXT("SSSSSSST"),
		TEXT("SSSSSSST"),
	};
	// --- MantaCape (Р64, elite: night-sea wings = primary, pale belly with gill slits = secondary, gold clasp; wing tips in code)
	const TCHAR* const MantaCapeDown[] = {
		TEXT("SZSGkkGSSs"),
		TEXT("SZTTGgTTSs"),
		TEXT("SZTXTTTTSs"),
		TEXT("SStTTTTtSs"),
		TEXT("SZtTTTTtSs"),
		TEXT("SStTTTTtSs"),
		TEXT("SZSTTTTSSs"),
		TEXT("SSSSTTSSSs"),
		TEXT("SSSSQSSSSs"),
	};
	const TCHAR* const MantaCapeUp[] = {
		TEXT("SSSSSSSSSs"),
		TEXT("STTSSSSTTs"),
		TEXT("SSTTSSTTSs"),
		TEXT("SSSTSSTSSs"),
		TEXT("SZSSQSSSSs"),
		TEXT("SSSSSSSQSs"),
		TEXT("SZSSSSSSSs"),
		TEXT("SSSSmSSSSs"),
		TEXT("SSSSmSSSSs"),
	};
	const TCHAR* const MantaCapeRight[] = {
		TEXT("SSSSSGkS"),
		TEXT("SZSSSGgT"),
		TEXT("SSSSSSTT"),
		TEXT("SXTSSStT"),
		TEXT("SZSSSStT"),
		TEXT("SSSSSSTT"),
		TEXT("SQSSSSsT"),
		TEXT("SSSSSSsT"),
		TEXT("SSSSSSsT"),
	};

	struct FTpl { const TCHAR* const* Rows; int32 N; };

	FTpl TorsoTpl(EFVOutfitStyle O, EFVFacing F)
	{
		const int32 V = F == EFVFacing::Down ? 0 : (F == EFVFacing::Up ? 1 : 2);
		switch (O)
		{
		case EFVOutfitStyle::Overalls: return V == 0 ? FTpl{ FV_TPL(OverallsDown) } : V == 1 ? FTpl{ FV_TPL(OverallsUp) } : FTpl{ FV_TPL(OverallsRight) };
		case EFVOutfitStyle::Shirt:    return V == 0 ? FTpl{ FV_TPL(ShirtDown) } : V == 1 ? FTpl{ FV_TPL(ShirtUp) } : FTpl{ FV_TPL(ShirtRight) };
		case EFVOutfitStyle::Hoodie:   return V == 0 ? FTpl{ FV_TPL(HoodieDown) } : V == 1 ? FTpl{ FV_TPL(HoodieUp) } : FTpl{ FV_TPL(HoodieRight) };
		case EFVOutfitStyle::Vest:     return V == 0 ? FTpl{ FV_TPL(VestDown) } : V == 1 ? FTpl{ FV_TPL(VestUp) } : FTpl{ FV_TPL(VestRight) };
		case EFVOutfitStyle::Raincoat: return V == 0 ? FTpl{ FV_TPL(RaincoatDown) } : V == 1 ? FTpl{ FV_TPL(RaincoatUp) } : FTpl{ FV_TPL(RaincoatRight) };
		case EFVOutfitStyle::Sailor:   return V == 0 ? FTpl{ FV_TPL(SailorDown) } : V == 1 ? FTpl{ FV_TPL(SailorUp) } : FTpl{ FV_TPL(SailorRight) };
		case EFVOutfitStyle::Suit:     return V == 0 ? FTpl{ FV_TPL(SuitDown) } : V == 1 ? FTpl{ FV_TPL(SuitUp) } : FTpl{ FV_TPL(SuitRight) };
		case EFVOutfitStyle::Flannel:        return V == 0 ? FTpl{ FV_TPL(FlannelDown) } : V == 1 ? FTpl{ FV_TPL(FlannelUp) } : FTpl{ FV_TPL(FlannelRight) };
		case EFVOutfitStyle::LumberVest:     return V == 0 ? FTpl{ FV_TPL(LumberVestDown) } : V == 1 ? FTpl{ FV_TPL(LumberVestUp) } : FTpl{ FV_TPL(LumberVestRight) };
		case EFVOutfitStyle::CaptainCoat:    return V == 0 ? FTpl{ FV_TPL(CaptainCoatDown) } : V == 1 ? FTpl{ FV_TPL(CaptainCoatUp) } : FTpl{ FV_TPL(CaptainCoatRight) };
		case EFVOutfitStyle::AdmiralUniform: return V == 0 ? FTpl{ FV_TPL(AdmiralUniformDown) } : V == 1 ? FTpl{ FV_TPL(AdmiralUniformUp) } : FTpl{ FV_TPL(AdmiralUniformRight) };
		case EFVOutfitStyle::LegendCloak:    return V == 0 ? FTpl{ FV_TPL(LegendCloakDown) } : V == 1 ? FTpl{ FV_TPL(LegendCloakUp) } : FTpl{ FV_TPL(LegendCloakRight) };
		case EFVOutfitStyle::RodMaster:      return V == 0 ? FTpl{ FV_TPL(RodMasterDown) } : V == 1 ? FTpl{ FV_TPL(RodMasterUp) } : FTpl{ FV_TPL(RodMasterRight) };
		case EFVOutfitStyle::MantaCape:      return V == 0 ? FTpl{ FV_TPL(MantaCapeDown) } : V == 1 ? FTpl{ FV_TPL(MantaCapeUp) } : FTpl{ FV_TPL(MantaCapeRight) };
		default: break;
		}
		return V == 0 ? FTpl{ FV_TPL(OverallsDown) } : V == 1 ? FTpl{ FV_TPL(OverallsUp) } : FTpl{ FV_TPL(OverallsRight) };
	}

	// ============================================================================================
	// Legs (pants + shoes), y36..45
	// ============================================================================================
	// Front/back: 10 wide at x=11. Leg lift is done in code.
	const TCHAR* const LegsFrontL[] = {   // left leg (x 11..15)
		TEXT("PPPPP"),
		TEXT("PPPPp"),
		TEXT("PPPPp"),
		TEXT("PPPp."),
		TEXT("PPPp."),
		TEXT("PPPp."),
		TEXT("PPPp."),
		TEXT("BBBB."),
		TEXT("BBBb."),
		TEXT("bbbb."),
	};
	const TCHAR* const LegsFrontR[] = {   // right leg (x 16..20)
		TEXT("PPPPp"),
		TEXT("PPPPp"),
		TEXT("PPPPp"),
		TEXT(".PPPp"),
		TEXT(".PPPp"),
		TEXT(".PPPp"),
		TEXT(".PPPp"),
		TEXT(".BBBB"),
		TEXT(".BBBb"),
		TEXT(".bbbb"),
	};
	// Side legs (facing right), 10 wide at x=11. Frames: stand / stride A / stride B.
	const TCHAR* const LegsSideStand[] = {
		TEXT("..PPPPPPp."),
		TEXT("..PPPPPPp."),
		TEXT("..PPPPPp.."),
		TEXT("...PPPPp.."),
		TEXT("...PPPPp.."),
		TEXT("...PPPPp.."),
		TEXT("...PPPPp.."),
		TEXT("...BBBBBB."),
		TEXT("...BBBBBBb"),
		TEXT("...bbbbbbb"),
	};
	const TCHAR* const LegsSideStrideA[] = {   // near leg forward
		TEXT("..PPPPPPp."),
		TEXT("..PPPPPPp."),
		TEXT("..pPPPPPPp"),
		TEXT(".ppp.PPPPp"),
		TEXT(".ppp..PPPp"),
		TEXT("ppp...PPPp"),
		TEXT("ppp....PPP"),
		TEXT("bbb....BBB"),
		TEXT("bbbb...BBB"),
		TEXT(".......bbb"),
	};
	const TCHAR* const LegsSideStrideB[] = {   // near leg back
		TEXT("..PPPPPPp."),
		TEXT("..PPPPPPp."),
		TEXT("..PPPPpppp"),
		TEXT(".PPPp.pppp"),
		TEXT(".PPPp..ppp"),
		TEXT("PPPp...ppp"),
		TEXT("PPP....ppp"),
		TEXT("BBB....bbb"),
		TEXT("BBBb...bbb"),
		TEXT("bbb......."),
	};

	// ============================================================================================
	// Hats. Each view: rows, top Y on the canvas, left X on the canvas.
	// ============================================================================================
	struct FHatView { const TCHAR* const* Rows; int32 N; int32 TopY; int32 LeftX; };
	struct FHatDef { FHatView Front, Back, Side; };

	// --- Straw
	const TCHAR* const StrawFront[] = {
		TEXT(".....ZZZZZZ....."),
		TEXT("....ZAZAZAZA...."),
		TEXT("....AZAZAZAa...."),
		TEXT("...ZAZAZAZAZa..."),
		TEXT("...CCCCCCCCCC..."),
		TEXT("...cccccccccc..."),
		TEXT("ZZAZAZAZAZAZAZAa"),
		TEXT(".aaAaAaAaAaAaaa."),
	};
	const TCHAR* const StrawSide[] = {
		TEXT("......ZZZZZ....."),
		TEXT(".....ZAZAZAZ...."),
		TEXT(".....AZAZAZa...."),
		TEXT(".....ZAZAZAa...."),
		TEXT(".....CCCCCCC...."),
		TEXT(".....ccccccc...."),
		TEXT("ZZAZAZAZAZAZAZAa"),
		TEXT(".aaAaAaAaAaAaaa."),
	};
	// --- Cap
	const TCHAR* const CapFront[] = {
		TEXT("....ZZZZZZ...."),
		TEXT("..ZZAAAAAAaa.."),
		TEXT(".ZAAAAAAAAAAa."),
		TEXT(".AAAAAXCAAAAa."),
		TEXT(".AAAAACcAAAAa."),
		TEXT("AAAAAAAAAAAAAa"),
		TEXT("CCCCCCCCCCCCCc"),
		TEXT(".cccccccccccc."),
	};
	const TCHAR* const CapBack[] = {
		TEXT("....ZZZZZZ...."),
		TEXT("..ZZAAAAAAaa.."),
		TEXT(".ZAAAAAAAAAAa."),
		TEXT(".AAAAAAAAAAAa."),
		TEXT(".AAAACCCCAAAa."),
		TEXT(".aAAACooCAAAa."),
		TEXT("..aaaaaaaaaa.."),
	};
	const TCHAR* const CapSide[] = {
		TEXT("...ZZZZZZ......."),
		TEXT("..ZAAAAAAa......"),
		TEXT(".ZAAAAAAAAa....."),
		TEXT(".AAAAAAAAAa....."),
		TEXT(".AAAAAAAAAAa...."),
		TEXT(".aAAAAAAAAACCCCc"),
		TEXT("..aaaaaaaaacccc."),
	};
	// --- Bandana
	const TCHAR* const BandanaFront[] = {
		TEXT("...ZZZZZZZZ..."),
		TEXT("..ZAXAAAXAAAa."),
		TEXT(".ZAAAAAXAAAAa."),
		TEXT(".AAXAAAAAAXAa."),
		TEXT(".aaaaaaaaaaaa."),
	};
	const TCHAR* const BandanaBack[] = {
		TEXT("...ZZZZZZZZ..."),
		TEXT("..ZAXAAAXAAAa."),
		TEXT(".ZAAAAAXAAAAa."),
		TEXT(".AAXAAaAAAXAa."),
		TEXT(".aaaaAAAaaaaa."),
		TEXT(".....aAa......"),
		TEXT("....aa.aa....."),
	};
	const TCHAR* const BandanaSide[] = {
		TEXT(".....ZZZZZZZZ.."),
		TEXT("....ZAXAAAXAAa."),
		TEXT("..AaAAAAAXAAAa."),
		TEXT(".AAaAAXAAAAAXa."),
		TEXT(".aA.aaaaaaaaaa."),
		TEXT(".a............."),
	};
	// --- Bucket
	const TCHAR* const BucketFront[] = {
		TEXT(".....ZZZZZZ....."),
		TEXT("....ZAAAAAAa...."),
		TEXT("....AAAAAAAa...."),
		TEXT("...ZAAAAAAAAa..."),
		TEXT("...CCCCCCCCCc..."),
		TEXT("..ZAAAAAAAAAAa.."),
		TEXT(".ZAAAAAAAAAAAAa."),
		TEXT(".aaaaaaaaaaaaaa."),
	};
	const TCHAR* const BucketSide[] = {
		TEXT("......ZZZZZ....."),
		TEXT(".....ZAAAAAa...."),
		TEXT(".....AAAAAAa...."),
		TEXT("....ZAAAAAAAa..."),
		TEXT("....CCCCCCCCc..."),
		TEXT("...ZAAAAAAAAAa.."),
		TEXT("..ZAAAAAAAAAAAa."),
		TEXT("..aaaaaaaaaaaaa."),
	};
	// --- Cowboy
	const TCHAR* const CowboyFront[] = {
		TEXT("....ZZ....ZZ...."),
		TEXT("....ZAZ..ZAa...."),
		TEXT("....ZAAZZAAa...."),
		TEXT("....AAAAAAAa...."),
		TEXT("....AAAAAAAa...."),
		TEXT("....CCCCCCCc...."),
		TEXT("Za..AAAAAAAa..Za"),
		TEXT("ZAAAAAAAAAAAAAAa"),
		TEXT(".aaaaaaaaaaaaaa."),
	};
	const TCHAR* const CowboySide[] = {
		TEXT("......ZZZ......."),
		TEXT(".....ZAAAZ......"),
		TEXT(".....AAAAAa....."),
		TEXT(".....AAAAAa....."),
		TEXT(".....AAAAAa....."),
		TEXT(".....CCCCCc....."),
		TEXT("Z....AAAAAa....Z"),
		TEXT("ZAAAAAAAAAAAAAAa"),
		TEXT(".aaaaaaaaaaaaaa."),
	};
	// --- Beanie
	const TCHAR* const BeanieFront[] = {
		TEXT("......XC......"),
		TEXT(".....XCCc....."),
		TEXT("....ZAAAAa...."),
		TEXT("..ZAZAAZAAAa.."),
		TEXT(".ZAAZAAZAAZAa."),
		TEXT(".AAAZAAZAAZAa."),
		TEXT(".AAAZAAZAAZAa."),
		TEXT(".CcCcCcCcCcCc."),
		TEXT(".cCcCcCcCcCcc."),
	};
	const TCHAR* const BeanieSide[] = {
		TEXT("......XC......"),
		TEXT(".....XCCc....."),
		TEXT("....ZAAAAa...."),
		TEXT("..ZAZAAZAAa..."),
		TEXT(".ZAAZAAZAAZa.."),
		TEXT(".AAAZAAZAAZAa."),
		TEXT(".AAAZAAZAAZAa."),
		TEXT(".CcCcCcCcCcCc."),
		TEXT(".cCcCcCcCcCc.."),
	};
	// --- Ushanka (crown = primary, fur = secondary)
	const TCHAR* const UshankaFront[] = {
		TEXT("....ZZZZZZZZ...."),
		TEXT("...ZAAAAAAAAa..."),
		TEXT("..ZAAAAAAAAAAa.."),
		TEXT(".XCcCXcCCcXCCc.."),
		TEXT("XCCcCCcXCcCCcCc."),
		TEXT("CcXCcCCcCCXcCCc."),
		TEXT("CX............Cc"),
		TEXT("Cc............cC"),
		TEXT("XC............Cc"),
		TEXT("cC............cc"),
		TEXT(".c............c."),
	};
	const TCHAR* const UshankaBack[] = {
		TEXT("....ZZZZZZZZ...."),
		TEXT("...ZAAAAAAAAa..."),
		TEXT("..ZAAAAAAAAAAa.."),
		TEXT(".XCcCXcCCcXCCc.."),
		TEXT("XCCcCCcXCcCCcCc."),
		TEXT("CcXCcCCcCCXcCCcc"),
		TEXT("CXCcCXCcCcCXCCCc"),
		TEXT("CcCCcCCcCCcCCcCC"),
		TEXT("XC............Cc"),
		TEXT("cC............cc"),
		TEXT(".c............c."),
	};
	const TCHAR* const UshankaSide[] = {
		TEXT("....ZZZZZZZZ...."),
		TEXT("...ZAAAAAAAAa..."),
		TEXT("..ZAAAAAAAAAAa.."),
		TEXT(".XCcCXcCCcXCCc.."),
		TEXT("XCCcCCcXCcCCcCc."),
		TEXT("CcXCcCCcCCXcCCc."),
		TEXT("CXCcCXCc........"),
		TEXT("CcCCcCCc........"),
		TEXT(".CcXCcC........."),
		TEXT("..cCcc.........."),
	};
	// --- Pirate tricorn (hat = primary, trim = secondary)
	const TCHAR* const PirateFront[] = {
		TEXT(".......ZZ......."),
		TEXT(".....ZZAAZZ....."),
		TEXT("...ZZAAwwAAZZ..."),
		TEXT(".ZZAAAwkwkAAAZZ."),
		TEXT("ZAAAAAAwwAAAAAAa"),
		TEXT("CAAAAAAkwAAAAAaC"),
		TEXT(".CCAAAAAAAAAACC."),
		TEXT("...CCCCCCCCCCc.."),
	};
	const TCHAR* const PirateSide[] = {
		TEXT("........ZZ......"),
		TEXT("......ZZAAZ....."),
		TEXT("....ZZAAAAAZ...."),
		TEXT("..ZZAAAAAAAAZ..."),
		TEXT("CZAAAAAAAAAAAZZC"),
		TEXT(".CAAAAAAAAAAAAC."),
		TEXT("..CCCCCCCCCCCC.."),
	};
	const TCHAR* const PirateBack[] = {
		TEXT(".......ZZ......."),
		TEXT(".....ZZAAZZ....."),
		TEXT("...ZZAAAAAAZZ..."),
		TEXT(".ZZAAAAAAAAAAZZ."),
		TEXT("ZAAAAAAAAAAAAAAa"),
		TEXT("CAAAAAAAAAAAAAaC"),
		TEXT(".CCAAAAAAAAAACC."),
		TEXT("...CCCCCCCCCCc.."),
	};
	// --- Wizard (hat = primary, stars & band = secondary)
	const TCHAR* const WizardFront[] = {
		TEXT("..........ZZ...."),
		TEXT(".........ZAa...."),
		TEXT("........ZAa....."),
		TEXT(".......ZAAa....."),
		TEXT("......ZAAAa....."),
		TEXT("......ZACAa....."),
		TEXT(".....ZAAAAAa...."),
		TEXT(".....ZAAAAAa...."),
		TEXT("....ZAAAAXAAa..."),
		TEXT("....ZAXAAAAAa..."),
		TEXT("...ZAAAAAAAAAa.."),
		TEXT("...CCCCCCCCCCc.."),
		TEXT("ZAAAAAAAAAAAAAAa"),
		TEXT(".aaaaaaaaaaaaaa."),
	};
	const TCHAR* const WizardBack[] = {
		TEXT("..........ZZ...."),
		TEXT(".........ZAa...."),
		TEXT("........ZAa....."),
		TEXT(".......ZAAa....."),
		TEXT("......ZAAAa....."),
		TEXT("......ZAAAa....."),
		TEXT(".....ZAAAAAa...."),
		TEXT(".....ZACAAAa...."),
		TEXT("....ZAAAAAAAa..."),
		TEXT("....ZAAAAAXAa..."),
		TEXT("...ZAAAAAAAAAa.."),
		TEXT("...CCCCCCCCCCc.."),
		TEXT("ZAAAAAAAAAAAAAAa"),
		TEXT(".aaaaaaaaaaaaaa."),
	};
	const TCHAR* const WizardSide[] = {
		TEXT("ZZ.............."),
		TEXT(".ZAa............"),
		TEXT("..ZAa..........."),
		TEXT("...ZAAa........."),
		TEXT("....ZAAa........"),
		TEXT(".....ZACa......."),
		TEXT(".....ZAAAa......"),
		TEXT(".....ZAAAAa....."),
		TEXT("....ZAAAXAAa...."),
		TEXT("....ZAAAAAAa...."),
		TEXT("...ZAAAAAAAAa..."),
		TEXT("...CCCCCCCCCc..."),
		TEXT("ZAAAAAAAAAAAAAAa"),
		TEXT(".aaaaaaaaaaaaaa."),
	};
	// --- Crown (gold = primary, gems = secondary + fixed ruby/sapphire)
	const TCHAR* const CrownFront[] = {
		TEXT(".C....C.....C."),
		TEXT(".Z...ZZa....Z."),
		TEXT(".ZA..ZAa...ZA."),
		TEXT(".ZAa.ZAa..ZAa."),
		TEXT(".ZAAZAAAaZAAa."),
		TEXT(".ZAAAAAAAAAAa."),
		TEXT(".ARAACCAABAAa."),
		TEXT(".aaaaaaaaaaaa."),
	};
	const TCHAR* const CrownSide[] = {
		TEXT("..C....C....C.."),
		TEXT("..Z...ZZ....Z.."),
		TEXT("..ZA..ZAa..ZA.."),
		TEXT("..ZAa.ZAa.ZAa.."),
		TEXT("..ZAAZAAAZAAa.."),
		TEXT("..ZAAAAAAAAAa.."),
		TEXT("..AABAACCAARa.."),
		TEXT("..aaaaaaaaaaa.."),
	};

	// --- update 6 [ART-WEAR] Pompom (knit hat = primary, stripes and the big pompom = secondary)
	const TCHAR* const PompomFront[] = {
		TEXT(".....XCCc....."),
		TEXT("....XCCCCc...."),
		TEXT("....CCXCCc...."),
		TEXT(".....cccc....."),
		TEXT("....ZAAAAa...."),
		TEXT("..ZAAAAAAAa..."),
		TEXT(".ZCCCCCCCCCc.."),
		TEXT(".AAAAAAAAAAAa."),
		TEXT(".CCCCCCCCCCCc."),
		TEXT("ZAAAAAAAAAAAAa"),
		TEXT("XCXCXCXCXCXCXc"),
		TEXT("cCcCcCcCcCcCcc"),
	};
	// --- GoldHat (elite top hat: gold = primary, band = secondary, fixed gems and glints)
	const TCHAR* const GoldHatFront[] = {
		TEXT("...ZZZZZZZZ..."),
		TEXT("..ZwAAAAAAAa.."),
		TEXT("..ZAAAAAAAAa.."),
		TEXT("..ZwAAAAAAAa.."),
		TEXT("..ZAAAAAAAAa.."),
		TEXT("..CCRCCBCCRc.."),
		TEXT("..cCCCCCCCCc.."),
		TEXT("..ZAAAAAAAAa.."),
		TEXT("ZZAwAAAAAAAAAa"),
		TEXT(".aaaaaaaaaaaa."),
	};
	const TCHAR* const GoldHatSide[] = {
		TEXT("....ZZZZZZZZ...."),
		TEXT("....ZwAAAAAa...."),
		TEXT("....ZAAAAAAa...."),
		TEXT("....ZwAAAAAa...."),
		TEXT("....ZAAAAAAa...."),
		TEXT("....CCRCCCCc...."),
		TEXT("....cCCCCCCc...."),
		TEXT("....ZAAAAAAa...."),
		TEXT("ZZAAwAAAAAAAAAAa"),
		TEXT(".aaaaaaaaaaaaaa."),
	};
	// --- CaptainCap (crown = primary, cord = secondary, black band and visor, gold badge)
	const TCHAR* const CaptainCapFront[] = {
		TEXT("....ZZZZZZZZ...."),
		TEXT("..ZZAAAAAAAAaa.."),
		TEXT(".ZAAAAAAAAAAAAa."),
		TEXT(".aAAAAAGGAAAAaa."),
		TEXT("..kkkkGwGGkkkk.."),
		TEXT("..CCCCCCCCCCCC.."),
		TEXT("..kkkkkkkkkkkk.."),
		TEXT("...kkkkkkkkkk..."),
	};
	const TCHAR* const CaptainCapBack[] = {
		TEXT("....ZZZZZZZZ...."),
		TEXT("..ZZAAAAAAAAaa.."),
		TEXT(".ZAAAAAAAAAAAAa."),
		TEXT(".aAAAAAAAAAAAaa."),
		TEXT("..kkkkkkkkkkkk.."),
		TEXT("..cCCCCCCCCCCc.."),
		TEXT("..kkkkkkkkkkkk.."),
	};
	const TCHAR* const CaptainCapSide[] = {
		TEXT("...ZZZZZZZZ....."),
		TEXT(".ZZAAAAAAAAaa..."),
		TEXT("ZAAAAAAAAAAAAa.."),
		TEXT("aAAAAAAAAAAGaa.."),
		TEXT(".kkkkkkkkkkGk..."),
		TEXT(".CCCCCCCCCCCC..."),
		TEXT(".kkkkkkkkkkkkkkk"),
	};
	// --- Bicorne (elite admiral hat: felt = primary, cockade = secondary, gold trim)
	const TCHAR* const BicorneFront[] = {
		TEXT("......ZZZZZZ......"),
		TEXT("....ZZAAAAAAaa...."),
		TEXT("..ZZAAAAACCAAAaa.."),
		TEXT("ZZAAAAAAACRcAAAAaa"),
		TEXT("GAAAAAAAAACcAAAAAg"),
		TEXT(".GGAAAAAAAAAAAAgg."),
		TEXT("...GGwGGGGGGGGg..."),
	};
	const TCHAR* const BicorneBack[] = {
		TEXT("......ZZZZZZ......"),
		TEXT("....ZZAAAAAAaa...."),
		TEXT("..ZZAAAAAAAAAAaa.."),
		TEXT("ZZAAAAAAAAAAAAAAaa"),
		TEXT("GAAAAAAAAAAAAAAAAg"),
		TEXT(".GGAAAAAAAAAAAAgg."),
		TEXT("...GGGGGGGGGGGg..."),
	};
	const TCHAR* const BicorneSide[] = {
		TEXT(".....ZZ........."),
		TEXT("....ZAAa........"),
		TEXT("...ZAAAAa......."),
		TEXT("..ZAAAAAAa......"),
		TEXT("..ZAACRAAAa....."),
		TEXT(".ZAAACcAAAAa...."),
		TEXT(".GAAAAAAAAAg...."),
		TEXT("..GGwGGGGGGg...."),
	};
	// --- MasterCrown (elite: gold = primary, big stones = secondary, fixed rubies / sapphires, ermine band)
	const TCHAR* const MasterCrownFront[] = {
		TEXT(".G.....CC.....G."),
		TEXT(".ZG...ZCCa...GA."),
		TEXT(".ZAa.ZAwAAa.ZAa."),
		TEXT(".ZAAZAAAAAAZAAa."),
		TEXT(".ZwAAAAAAAAAAAa."),
		TEXT(".ZRAACAABAACARa."),
		TEXT(".ZAAAAAAAAAAAAa."),
		TEXT("wwkwwwkwwwkwwwkw"),
		TEXT("wkwwwkwwwkwwwkww"),
	};

	// --- MarlinHelm (Р64, elite: helmet = primary, sail crest / belly band = secondary, gold rim with a sapphire; the bill in the side view)
	const TCHAR* const MarlinHelmFront[] = {
		TEXT(".......XC......."),
		TEXT("......XCcC......"),
		TEXT(".....XCcCcC....."),
		TEXT("...ZAAAAAAAAa..."),
		TEXT("..ZAAAAAAAAAAa.."),
		TEXT(".ZAAwAAAAAAAAAa."),
		TEXT(".ZAAAAAAAAAAAAa."),
		TEXT(".ZCCCCCCCCCCCCa."),
		TEXT("GGGGGGGBBGGGGGGg"),
		TEXT(".Aa..........aA."),
		TEXT(".aA..........Aa."),
	};
	const TCHAR* const MarlinHelmBack[] = {
		TEXT(".......XC......."),
		TEXT("......XCcC......"),
		TEXT(".....XCcCcC....."),
		TEXT("...ZAAAAAAAAa..."),
		TEXT("..ZAAAAAAAAAAa.."),
		TEXT(".ZAAAAAAAAAAAAa."),
		TEXT(".ZAAAAAAAAAAAAa."),
		TEXT(".ZCCCCCCCCCCCCa."),
		TEXT("GGGGGGGGGGGGGGGg"),
		TEXT(".Aa..........aA."),
		TEXT(".aA..........Aa."),
	};
	const TCHAR* const MarlinHelmSide[] = {
		TEXT(".....XC................."),
		TEXT("....XCcC................"),
		TEXT("...XCcCcC..............."),
		TEXT("...ZAAAAAAAAa..........."),
		TEXT("..ZAAAAAAAAAAa.........."),
		TEXT("..ZAAAAAAAAwAAa........."),
		TEXT("..ZAAAAAAAAAAAAa........"),
		TEXT("..ZCCCCCCCCCCCCCa......."),
		TEXT(".GGGGGGGGGGGGGGGAAAZZZww"),
		TEXT(".....AAa................"),
		TEXT(".....aAa................"),
	};
	// --- DragonHelm (Р64, elite: scales = primary, horns = secondary, gold crest and rim with rubies)
	const TCHAR* const DragonHelmFront[] = {
		TEXT("X..............X"),
		TEXT("Cc.....GG.....cC"),
		TEXT(".Cc...GggG...cC."),
		TEXT("..CZAAAGGAAAaC.."),
		TEXT("..ZAAaAGGAaAAa.."),
		TEXT(".ZAaAAaAAaAAaAa."),
		TEXT(".ZAAaAAaAAaAAaa."),
		TEXT(".GGRGGGGGGGGRGg."),
		TEXT(".Aa..........aA."),
		TEXT("..a..........a.."),
	};
	const TCHAR* const DragonHelmSide[] = {
		TEXT("X................."),
		TEXT("Cc....G..G........"),
		TEXT(".CcGgGgGgGgG......"),
		TEXT("..CZAAAAAAAAAa...."),
		TEXT("..ZAaAAaAAaAAAa..."),
		TEXT(".ZAAAaAAaAAaAAAa.."),
		TEXT(".ZAaAAaAAaAAAAAAa."),
		TEXT(".GGGRGGGGGGGGGGGGg"),
		TEXT("....Aa............"),
		TEXT("....aA............"),
	};

	FHatDef HatFor(EFVHatStyle H)
	{
		auto V = [](const TCHAR* const* R, int32 N, int32 TopY, int32 LeftX) { return FHatView{ R, N, TopY, LeftX }; };
		switch (H)
		{
		case EFVHatStyle::Straw:
			return { V(FV_TPL(StrawFront), 10, 8), V(FV_TPL(StrawFront), 10, 8), V(FV_TPL(StrawSide), 10, 7) };
		case EFVHatStyle::Cap:
			return { V(FV_TPL(CapFront), 11, 9), V(FV_TPL(CapBack), 11, 9), V(FV_TPL(CapSide), 11, 9) };
		case EFVHatStyle::Bandana:
			return { V(FV_TPL(BandanaFront), 13, 9), V(FV_TPL(BandanaBack), 13, 9), V(FV_TPL(BandanaSide), 13, 7) };
		case EFVHatStyle::Bucket:
			return { V(FV_TPL(BucketFront), 10, 8), V(FV_TPL(BucketFront), 10, 8), V(FV_TPL(BucketSide), 10, 7) };
		case EFVHatStyle::Cowboy:
			return { V(FV_TPL(CowboyFront), 9, 8), V(FV_TPL(CowboyFront), 9, 8), V(FV_TPL(CowboySide), 9, 7) };
		case EFVHatStyle::Beanie:
			return { V(FV_TPL(BeanieFront), 10, 9), V(FV_TPL(BeanieFront), 10, 9), V(FV_TPL(BeanieSide), 10, 8) };
		case EFVHatStyle::Ushanka:
			return { V(FV_TPL(UshankaFront), 11, 8), V(FV_TPL(UshankaBack), 11, 8), V(FV_TPL(UshankaSide), 11, 7) };
		case EFVHatStyle::Pirate:
			return { V(FV_TPL(PirateFront), 10, 8), V(FV_TPL(PirateBack), 10, 8), V(FV_TPL(PirateSide), 10, 7) };
		case EFVHatStyle::Wizard:
			return { V(FV_TPL(WizardFront), 4, 8), V(FV_TPL(WizardBack), 4, 8), V(FV_TPL(WizardSide), 4, 6) };
		case EFVHatStyle::Crown:
			return { V(FV_TPL(CrownFront), 8, 9), V(FV_TPL(CrownFront), 8, 9), V(FV_TPL(CrownSide), 8, 8) };
		// update 6 [ART-WEAR]
		case EFVHatStyle::Pompom:
			return { V(FV_TPL(PompomFront), 6, 9), V(FV_TPL(PompomFront), 6, 9), V(FV_TPL(PompomFront), 6, 9) };
		case EFVHatStyle::GoldHat:
			return { V(FV_TPL(GoldHatFront), 5, 9), V(FV_TPL(GoldHatFront), 5, 9), V(FV_TPL(GoldHatSide), 5, 8) };
		case EFVHatStyle::CaptainCap:
			return { V(FV_TPL(CaptainCapFront), 10, 8), V(FV_TPL(CaptainCapBack), 10, 8), V(FV_TPL(CaptainCapSide), 10, 8) };
		case EFVHatStyle::Bicorne:
			return { V(FV_TPL(BicorneFront), 9, 7), V(FV_TPL(BicorneBack), 9, 7), V(FV_TPL(BicorneSide), 7, 9) };
		case EFVHatStyle::MasterCrown:
			return { V(FV_TPL(MasterCrownFront), 7, 8), V(FV_TPL(MasterCrownFront), 7, 8), V(FV_TPL(MasterCrownFront), 7, 8) };
		// update 6 (Р64)
		case EFVHatStyle::MarlinHelm:
			return { V(FV_TPL(MarlinHelmFront), 6, 8), V(FV_TPL(MarlinHelmBack), 6, 8), V(FV_TPL(MarlinHelmSide), 6, 7) };
		case EFVHatStyle::DragonHelm:
			return { V(FV_TPL(DragonHelmFront), 6, 8), V(FV_TPL(DragonHelmFront), 6, 8), V(FV_TPL(DragonHelmSide), 6, 7) };
		default:
			break;
		}
		return { V(nullptr, 0, 0, 0), V(nullptr, 0, 0, 0), V(nullptr, 0, 0, 0) };
	}

	// ============================================================================================
	// Rod poses (fishing anims, facing Down). Tip is the exact texel returned by RodTipOffset.
	// ============================================================================================
	struct FRodPose
	{
		bool bValid = false;
		FIntPoint Butt, Hand, Mid, Tip;   // Mid == Tip for straight rods
		bool bBent = false;
		bool bBehind = false;             // drawn behind the body (windup)
	};

	FRodPose RodPose(EFVCharAnim Anim, int32 Frame)
	{
		FRodPose P;
		P.bValid = true;
		switch (Anim)
		{
		case EFVCharAnim::CastWindup:
			P.Butt = FIntPoint(25, 29); P.Hand = FIntPoint(22, 25); P.Tip = FIntPoint(4, 5); P.Mid = P.Tip; P.bBehind = true;
			break;
		case EFVCharAnim::CastRelease:
			P.Butt = FIntPoint(17, 30); P.Hand = FIntPoint(20, 33); P.Tip = FIntPoint(30, 43); P.Mid = P.Tip;
			break;
		case EFVCharAnim::FishWait:
			P.Butt = FIntPoint(16, 32); P.Hand = FIntPoint(20, 34); P.Tip = Frame % 2 == 0 ? FIntPoint(30, 38) : FIntPoint(30, 39); P.Mid = P.Tip;
			break;
		case EFVCharAnim::Reel:
			P.Butt = FIntPoint(16, 32); P.Hand = FIntPoint(20, 34); P.Mid = FIntPoint(26, 36);
			P.Tip = Frame % 2 == 0 ? FIntPoint(30, 41) : FIntPoint(29, 42); P.bBent = true;
			break;
		default:
			P.bValid = false;
			break;
		}
		return P;
	}

	/** Arm polyline for fishing poses: shoulder -> elbow -> hand (texels). */
	struct FArmPose { FIntPoint Shoulder, Elbow, Hand; };

	void FishingArms(EFVCharAnim Anim, int32 Frame, FArmPose& OutL, FArmPose& OutR)
	{
		switch (Anim)
		{
		case EFVCharAnim::CastWindup:
			OutL = { FIntPoint(10, 28), FIntPoint(14, 29), FIntPoint(20, 26) };
			OutR = { FIntPoint(21, 28), FIntPoint(23, 28), FIntPoint(22, 24) };
			break;
		case EFVCharAnim::CastRelease:
			OutL = { FIntPoint(10, 28), FIntPoint(12, 31), FIntPoint(17, 33) };
			OutR = { FIntPoint(21, 28), FIntPoint(21, 30), FIntPoint(20, 33) };
			break;
		case EFVCharAnim::FishWait:
			OutL = { FIntPoint(10, 28), FIntPoint(11, 32), FIntPoint(16, 34) };
			OutR = { FIntPoint(21, 28), FIntPoint(21, 31), FIntPoint(20, 34) };
			break;
		case EFVCharAnim::Reel:
			OutL = { FIntPoint(10, 28), FIntPoint(11, 32), FIntPoint(16, 34) };
			OutR = Frame % 2 == 0
				? FArmPose{ FIntPoint(21, 28), FIntPoint(22, 31), FIntPoint(21, 36) }
				: FArmPose{ FIntPoint(21, 28), FIntPoint(23, 30), FIntPoint(23, 34) };
			break;
		case EFVCharAnim::HoldFish:
		default:
			OutL = { FIntPoint(10, 28), FIntPoint(7, 22), FIntPoint(7, 12) };
			OutR = { FIntPoint(21, 28), FIntPoint(24, 22), FIntPoint(23, 12) };
			break;
		}
	}

	/** Draws an arm (2px sleeve polyline + 2x2 hand) onto its own layer. */
	void DrawArmLayer(FFVImage& Layer, const FArmPose& A, const FCharStyle& S)
	{
		// Sleeve: shoulder -> elbow (and forearm too for long sleeves)
		ThickLine(Layer, A.Shoulder.X, A.Shoulder.Y, A.Elbow.X, A.Elbow.Y, S.Sleeve, S.SleeveS);
		if (S.bShortSleeves)
		{
			ThickLine(Layer, A.Elbow.X, A.Elbow.Y, A.Hand.X, A.Hand.Y, S.Skin, S.SkinS);
		}
		else
		{
			ThickLine(Layer, A.Elbow.X, A.Elbow.Y, A.Hand.X, A.Hand.Y, S.Sleeve, S.SleeveS);
		}
	}

	void DrawHand(FFVImage& Img, const FIntPoint& H, const FCharStyle& S)
	{
		Img.Set(H.X, H.Y, S.Skin);
		Img.Set(H.X + 1, H.Y, S.Skin);
		Img.Set(H.X, H.Y + 1, S.SkinS);
		Img.Set(H.X + 1, H.Y + 1, S.SkinS);
	}

	/** Pixel rod (1px blank, thicker near the grip) + grip/reel in the accent color. Drawn on its own layer. */
	void DrawRod(FFVImage& Img, const FRodPose& P, const FFVAppearance& L, int32 Frame, FColor OutlineC)
	{
		const FColor RodC = L.RodColor;
		const FColor RodL = FVArt::Light(RodC, 0.3f);
		const FColor RodTip = FVArt::Light(RodC, 0.55f);
		const FColor Acc = L.RodAccent;
		const FColor AccL = FVArt::Light(Acc, 0.25f);

		// Blank: a 1px line (lighter where the bent tip flexes) + the grip in the accent color.
		FFVImage Blank(Img.W, Img.H);
		if (P.bBent)
		{
			Blank.Line(P.Hand.X, P.Hand.Y, P.Mid.X, P.Mid.Y, RodC);
			Blank.Line(P.Mid.X, P.Mid.Y, P.Tip.X, P.Tip.Y, RodL);
		}
		else
		{
			Blank.Line(P.Hand.X, P.Hand.Y, P.Tip.X, P.Tip.Y, RodC);
		}
		Blank.Line(P.Butt.X, P.Butt.Y, P.Hand.X, P.Hand.Y, Acc);
		Blank.Set(P.Butt.X, P.Butt.Y, AccL);
		Blank.Set(P.Tip.X, P.Tip.Y, RodTip);

		// Dark edge under the blank so it reads on grass, sand and water alike.
		FFVImage Edge(Img.W, Img.H);
		for (int32 Y = 0; Y < Blank.H; ++Y)
		{
			for (int32 X = 0; X < Blank.W; ++X)
			{
				if (Blank.Get(X, Y).A != 0 && Blank.Get(X, Y + 1).A == 0)
				{
					Edge.Set(X, Y + 1, OutlineC);
				}
			}
		}

		// Reel: small outlined drum hanging under the grip; the crank knob alternates between frames.
		FFVImage Reel(Img.W, Img.H);
		const FColor ReelC(206, 210, 224), ReelD(124, 128, 150);
		const int32 RX = (P.Butt.X + P.Hand.X) / 2, RY = (P.Butt.Y + P.Hand.Y) / 2 + 1;
		Reel.Set(RX, RY, ReelC); Reel.Set(RX + 1, RY, ReelD);
		Reel.Set(RX, RY + 1, ReelD); Reel.Set(RX + 1, RY + 1, ReelD);
		Reel.Set(RX + ((Frame % 2) ? 2 : -1), RY + ((Frame % 2) ? 1 : 0), Acc);
		Reel.Outline(OutlineC);

		Img.Blit(Edge, 0, 0);
		Img.Blit(Reel, 0, 0);
		Img.Blit(Blank, 0, 0);
	}

	// ============================================================================================
	// Character composition
	// ============================================================================================
	void DrawLegsFront(FFVImage& Img, const FCharStyle& S, int32 LiftL, int32 LiftR)
	{
		// A lifted leg is drawn shorter (skip one pants row) so the shoe sits higher.
		auto DrawLeg = [&](const TCHAR* const* Rows, int32 N, int32 X, int32 Lift)
		{
			for (int32 R = 0; R < N; ++R)
			{
				const int32 SrcRow = R;
				int32 DstY = 36 + R;
				if (Lift > 0 && R >= 3)
				{
					DstY -= Lift;
				}
				Stamp(Img, X, DstY, &Rows[SrcRow], 1, S.Pal);
			}
		};
		DrawLeg(FV_TPL(LegsFrontL), 11, LiftL);
		DrawLeg(FV_TPL(LegsFrontR), 16, LiftR);
	}

	FFVImage BuildCharacterImpl(const FFVAppearance& L, EFVCharAnim Anim, EFVFacing Facing, int32 Frame)
	{
		const FCharStyle S = MakeStyle(L);
		const bool bFishing = Anim == EFVCharAnim::CastWindup || Anim == EFVCharAnim::CastRelease || Anim == EFVCharAnim::FishWait
			|| Anim == EFVCharAnim::Reel || Anim == EFVCharAnim::HoldFish;
		if (bFishing)
		{
			Facing = EFVFacing::Down;
		}
		const bool bSide = Facing == EFVFacing::Left || Facing == EFVFacing::Right;
		const EFVFacing View = bSide ? EFVFacing::Right : Facing;
		const int32 NumFrames = UFVSprites::CharacterFrameCount(Anim);
		Frame = ((Frame % NumFrames) + NumFrames) % NumFrames;

		// Per-frame motion.
		int32 BodyDY = 0;       // head + torso + arms
		int32 LiftL = 0, LiftR = 0;
		int32 ArmL = 0, ArmR = 0;   // arm length delta (walk swing)
		int32 SideStride = 0;       // 0 stand, 1 stride A, 2 stride B
		if (Anim == EFVCharAnim::Idle)
		{
			BodyDY = Frame == 1 ? 1 : 0;
		}
		else if (Anim == EFVCharAnim::Walk)
		{
			switch (Frame)
			{
			case 0: LiftL = 1; ArmL = -1; ArmR = 1; SideStride = 1; break;
			case 1: BodyDY = -1; break;
			case 2: LiftR = 1; ArmL = 1; ArmR = -1; SideStride = 2; break;
			default: BodyDY = -1; break;
			}
		}
		else if (Anim == EFVCharAnim::FishWait || Anim == EFVCharAnim::Reel)
		{
			BodyDY = (Anim == EFVCharAnim::Reel && Frame == 1) ? 1 : 0;
		}
		else if (Anim == EFVCharAnim::CastRelease)
		{
			BodyDY = 1;
		}

		FFVImage Body(CharW, CharH);

		// --- legs
		if (View == EFVFacing::Right)
		{
			const FTpl Legs = SideStride == 1 ? FTpl{ FV_TPL(LegsSideStrideA) } : SideStride == 2 ? FTpl{ FV_TPL(LegsSideStrideB) } : FTpl{ FV_TPL(LegsSideStand) };
			Stamp(Body, 11, 36, Legs.Rows, Legs.N, S.Pal);
		}
		else
		{
			DrawLegsFront(Body, S, LiftL, LiftR);
		}

		// --- far arm in side view (behind the torso; only visible while it swings out during a stride)
		if (View == EFVFacing::Right && SideStride != 0)
		{
			const int32 Swing = SideStride == 1 ? -2 : (SideStride == 2 ? 2 : 0);
			const FIntPoint Sh(15, 28 + BodyDY);
			const FIntPoint Hd(15 + Swing, 34 + BodyDY);
			ThickLine(Body, Sh.X, Sh.Y, Hd.X, Hd.Y, S.SleeveS, S.SleeveS);
			DrawHand(Body, Hd, S);
			Body.Set(Hd.X, Hd.Y, S.SkinS);
		}

		// --- torso
		const FTpl Torso = TorsoTpl(L.Outfit, View);
		const int32 TorsoX = View == EFVFacing::Right ? 12 : 11;
		Stamp(Body, TorsoX, 27 + BodyDY, Torso.Rows, Torso.N, S.Pal);
		if (S.bLongCoat)
		{
			const FTpl Skirt = View == EFVFacing::Right ? FTpl{ FV_TPL(RaincoatSkirtRight) } : View == EFVFacing::Up ? FTpl{ FV_TPL(RaincoatSkirtUp) } : FTpl{ FV_TPL(RaincoatSkirtDown) };
			Stamp(Body, TorsoX, 36 + FMath::Max(0, BodyDY), Skirt.Rows, Skirt.N, S.Pal);
		}
		// update 6 (Р64): the manta cape spreads its wing tips behind the arms (they flutter while walking)
		if (L.Outfit == EFVOutfitStyle::MantaCape && !bFishing)
		{
			const FColor Wg = L.OutfitPrimary, WgS = FVArt::Shade(L.OutfitPrimary, 0.3f), Rim = L.OutfitSecondary;
			if (View != EFVFacing::Right)
			{
				const int32 Fl = (Anim == EFVCharAnim::Walk && (Frame == 1 || Frame == 3)) ? 1 : 0;
				for (int32 I = 0; I < 6; ++I)
				{
					const int32 Y = 28 + BodyDY + I;
					if (I < 5) { Body.Set(8, Y, I == 4 ? Rim : Wg); Body.Set(23, Y, I == 4 ? Rim : WgS); }
					if (I >= 1 && I < 4 - Fl) { Body.Set(7, Y, Wg); Body.Set(24, Y, WgS); }
				}
			}
			else
			{
				// side view: the cape hangs behind the back, its hem lifts on a stride
				const int32 Hem = 40 - (SideStride != 0 ? 1 : 0);
				for (int32 Y = 28 + BodyDY; Y <= Hem; ++Y) { Body.Set(11, Y, Y == Hem ? Rim : WgS); }
				for (int32 Y = 30 + BodyDY; Y <= Hem - 2; ++Y) { Body.Set(10, Y, Wg); }
			}
		}

		// --- arms at the sides (front/back views, non-fishing)
		if (!bFishing && View != EFVFacing::Right)
		{
			auto SideArm = [&](int32 X, int32 LenDelta, bool bLeft)
			{
				const int32 Top = 27 + BodyDY;
				const int32 HandY = 34 + BodyDY + LenDelta;
				const int32 SleeveEnd = S.bShortSleeves ? Top + 2 : HandY - 1;
				for (int32 Y = Top; Y < HandY; ++Y)
				{
					const bool bSleeve = Y <= SleeveEnd;
					const FColor Outer = bSleeve ? S.Sleeve : S.Skin;
					const FColor Inner = bSleeve ? S.SleeveS : S.SkinS;
					Body.Set(X, Y, bLeft ? Outer : Inner);
					Body.Set(X + 1, Y, bLeft ? Inner : Outer);
				}
				Body.Set(X, HandY, S.Skin); Body.Set(X + 1, HandY, S.Skin);
				Body.Set(X, HandY + 1, S.SkinS); Body.Set(X + 1, HandY + 1, S.SkinS);
			};
			// back view: the character's left arm is on the viewer's left
			SideArm(9, View == EFVFacing::Up ? ArmR : ArmL, true);
			SideArm(21, View == EFVFacing::Up ? ArmL : ArmR, false);
		}

		// --- head
		{
			const int32 HY = 14 + BodyDY;
			if (View == EFVFacing::Down) { Stamp(Body, 9, HY, FV_TPL(HeadDown), S.Pal); }
			else if (View == EFVFacing::Up) { Stamp(Body, 9, HY, FV_TPL(HeadUp), S.Pal); }
			else { Stamp(Body, 9, HY, FV_TPL(HeadRight), S.Pal); }
		}

		// --- hat
		if (L.Hat != EFVHatStyle::None)
		{
			const FHatDef Hat = HatFor(L.Hat);
			const FHatView& HV = View == EFVFacing::Down ? Hat.Front : (View == EFVFacing::Up ? Hat.Back : Hat.Side);
			if (HV.Rows && HV.N > 0)
			{
				Stamp(Body, HV.LeftX, HV.TopY + BodyDY, HV.Rows, HV.N, S.HatPal);
			}
		}

		// --- near arm in side view (in front of the torso, own outline)
		if (View == EFVFacing::Right)
		{
			FFVImage ArmLayer(CharW, CharH);
			const int32 Swing = SideStride == 1 ? 2 : (SideStride == 2 ? -2 : 0);
			FArmPose A{ FIntPoint(15, 28 + BodyDY), FIntPoint(15 + Swing / 2, 31 + BodyDY), FIntPoint(15 + Swing, 34 + BodyDY) };
			DrawArmLayer(ArmLayer, A, S);
			DrawHand(ArmLayer, A.Hand, S);
			ArmLayer.Outline(S.Outline);
			Body.Blit(ArmLayer, 0, 0);
		}

		// --- fishing arms (own outline, in front of the body)
		FArmPose AL, AR;
		if (bFishing)
		{
			FishingArms(Anim, Frame, AL, AR);
			AL.Shoulder.Y += BodyDY; AR.Shoulder.Y += BodyDY;
			FFVImage ArmLayer(CharW, CharH);
			DrawArmLayer(ArmLayer, AL, S);
			DrawArmLayer(ArmLayer, AR, S);
			DrawHand(ArmLayer, AL.Hand, S);
			DrawHand(ArmLayer, AR.Hand, S);
			ArmLayer.Outline(S.Outline);
			Body.Blit(ArmLayer, 0, 0);
		}

		Body.Outline(S.Outline);

		// --- rod
		const FRodPose Rod = RodPose(Anim, Frame);
		FFVImage Out(CharW, CharH);
		if (Rod.bValid)
		{
			FFVImage RodLayer(CharW, CharH);
			DrawRod(RodLayer, Rod, L, Frame, S.Outline);
			if (Rod.bBehind)
			{
				Out.Blit(RodLayer, 0, 0);
				Out.Blit(Body, 0, 0);
				// hands grip the rod in front
				DrawHand(Out, AL.Hand, S);
				DrawHand(Out, AR.Hand, S);
			}
			else
			{
				Out.Blit(Body, 0, 0);
				Out.Blit(RodLayer, 0, 0);
				DrawHand(Out, AL.Hand, S);
				DrawHand(Out, AR.Hand, S);
			}
		}
		else
		{
			Out = Body;
		}

		if (Facing == EFVFacing::Left)
		{
			return Out.FlippedX();
		}
		return Out;
	}

	// ============================================================================================
	// Bobbers (8x8; drawn with an automatic outline; frame 1 = dipped 1px lower)
	// ============================================================================================
	FFVImage BuildBobberImpl(EFVBobberStyle Style, int32 Frame)
	{
		TMap<TCHAR, FColor> P;
		P.Add(TEXT('r'), FColor(226, 52, 52));
		P.Add(TEXT('R'), FColor(255, 128, 112));
		P.Add(TEXT('q'), FColor(160, 30, 44));
		P.Add(TEXT('w'), FColor(250, 250, 246));
		P.Add(TEXT('W'), FColor(210, 214, 222));
		P.Add(TEXT('x'), FColor(120, 120, 140));
		P.Add(TEXT('y'), FColor(255, 214, 64));
		P.Add(TEXT('Y'), FColor(255, 246, 170));
		P.Add(TEXT('d'), FColor(214, 140, 30));
		P.Add(TEXT('O'), FColor(250, 140, 40));
		P.Add(TEXT('E'), FColor(40, 28, 48));
		P.Add(TEXT('i'), FColor(242, 100, 150));
		P.Add(TEXT('I'), FColor(255, 176, 204));
		P.Add(TEXT('v'), FColor(176, 50, 100));
		P.Add(TEXT('c'), FColor(246, 230, 196));
		P.Add(TEXT('C'), FColor(214, 190, 150));
		// update 5: legend reward skins (§11)
		P.Add(TEXT('s'), FColor(196, 206, 222));        // sturgeon: silver ...
		P.Add(TEXT('S'), FColor(242, 246, 252));
		P.Add(TEXT('X'), FColor(104, 112, 136));
		P.Add(TEXT('k'), FColor(238, 226, 188));        // ... and bone scutes
		P.Add(TEXT('b'), FColor(120, 200, 244, 200));   // crystal: see-through ice
		P.Add(TEXT('B'), FColor(206, 242, 255, 220));
		P.Add(TEXT('n'), FColor(64, 126, 200, 210));
		P.Add(TEXT('l'), FColor(255, 255, 255));
		P.Add(TEXT('D'), FColor(46, 54, 84));           // angler: dark body and the lantern
		P.Add(TEXT('N'), FColor(78, 90, 126));
		P.Add(TEXT('Z'), FColor(60, 70, 104));
		P.Add(TEXT('L'), FColor(255, 244, 190));
		P.Add(TEXT('g'), FColor(118, 162, 136));        // fog: smoky green with a mint zigzag («змейка»)
		P.Add(TEXT('G'), FColor(170, 204, 184));
		P.Add(TEXT('h'), FColor(70, 108, 92));
		P.Add(TEXT('m'), FColor(150, 255, 190));
		P.Add(TEXT('F'), FColor(255, 86, 40));          // zhar: flame
		// update 6 [ART-WEAR]
		P.Add(TEXT('u'), FColor(150, 220, 255));        // diamond / marlin blues
		P.Add(TEXT('U'), FColor(226, 250, 255));
		P.Add(TEXT('V'), FColor(52, 104, 190));
		P.Add(TEXT('A'), FColor(196, 150, 255));        // aurora violet
		// update 6 (§18, MILL-ITEMS): «Жёрнов» — millstone greys
		P.Add(TEXT('T'), FColor(150, 146, 138));
		P.Add(TEXT('t'), FColor(100, 94, 88));
		P.Add(TEXT('K'), FColor(204, 200, 190));
		P.Add(TEXT('j'), FColor(52, 46, 44));
		// update 6 (Р64): «Штурвал» wood
		P.Add(TEXT('e'), FColor(170, 104, 56));
		P.Add(TEXT('f'), FColor(110, 62, 32));

		FFVImage Img;
		switch (Style)
		{
		case EFVBobberStyle::Classic:
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("...x...."),
				TEXT("..RRr..."),
				TEXT(".RwrRq.."),
				TEXT(".rrrrq.."),
				TEXT(".wwwwW.."),
				TEXT("..wwW..."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Duck:
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("....YY.."),
				TEXT("...yYEO."),
				TEXT("y..yyyO."),
				TEXT("yyyYyy.."),
				TEXT("yyyyyyd."),
				TEXT(".ddddd.."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Star:
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("...Y...."),
				TEXT("..YYy..."),
				TEXT("YYYyyyd."),
				TEXT(".yyyyd.."),
				TEXT(".yyddd.."),
				TEXT(".yd..d.."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Heart:
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT(".II.ii.."),
				TEXT("IwIiiiv."),
				TEXT("IIiiiiv."),
				TEXT(".iiiiv.."),
				TEXT("..iiv..."),
				TEXT("...v...."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Mushroom:
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("..RRr..."),
				TEXT(".RwrrwR."),
				TEXT("rrrwrrq."),
				TEXT("qqqqqqq."),
				TEXT("..cEC..."),
				TEXT("..ccC..."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Tsar:   // «Царский»: gold with a three-tooth crown and a ruby
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT(".Y.Y.Y.."),
				TEXT(".yyryy.."),
				TEXT(".dyyyd.."),
				TEXT(".YwYyd.."),
				TEXT(".yyydd.."),
				TEXT("..ddd..."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Sturgeon:   // «Осетровый»: silver, a dark tip and a row of bone scutes
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("...X...."),
				TEXT("..sSs..."),
				TEXT(".sSSsX.."),
				TEXT(".kXkXk.."),
				TEXT(".sssXX.."),
				TEXT("..XXX..."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Crystal:   // «Хрустальный»: a see-through blue crystal
			Img = FFVImage::FromAscii({
				TEXT("...B...."),
				TEXT("..BlB..."),
				TEXT(".BBlbn.."),
				TEXT(".Bbbbn.."),
				TEXT(".bbbnn.."),
				TEXT("..bnn..."),
				TEXT("...n...."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Lantern:   // «Фонарь удильщика»: a dark float with a rod arc and a glowing ball
			Img = FFVImage::FromAscii({
				TEXT("..xxx..."),
				TEXT(".x...x.."),
				TEXT(".D...LL."),
				TEXT("DNZ..LL."),
				TEXT("DNDD...."),
				TEXT("DDDDZ..."),
				TEXT(".DDZ...."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Fog:   // «Туманный»: smoky green with a mint zigzag
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("...h...."),
				TEXT("..GGg..."),
				TEXT(".mGmGm.."),
				TEXT(".gmGmh.."),
				TEXT(".gggGh.."),
				TEXT("..hhh..."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Zhar:   // «Жар-птица»: fiery orange with a flame crest
			Img = FFVImage::FromAscii({
				TEXT("..Y....."),
				TEXT("..yF.Y.."),
				TEXT("..FyFy.."),
				TEXT(".OOyOr.."),
				TEXT(".OYOOr.."),
				TEXT(".OOrrq.."),
				TEXT("..qqq..."),
				TEXT("........"),
			}, P);
			break;
		// update 6 [ART-WEAR]: boutique + expert legend rewards
		case EFVBobberStyle::Diamond:   // a brilliant-cut gem
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("..uUUu.."),
				TEXT(".uUlUUV."),
				TEXT(".VVuVVn."),
				TEXT("..VuVn.."),
				TEXT("...Vn..."),
				TEXT("........"),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Marlin:    // deep blue with a sail fin and a bill
			Img = FFVImage::FromAscii({
				TEXT("...V...."),
				TEXT("..VuV..."),
				TEXT(".VuUuV.."),
				TEXT(".VuwVVxx"),
				TEXT(".VVVVn.."),
				TEXT(".wwwwW.."),
				TEXT("..wwW..."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Kraken:    // a purple head with curling tentacles
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("..iIi..."),
				TEXT(".iIIiv.."),
				TEXT(".iEiEv.."),
				TEXT(".iiiiv.."),
				TEXT(".v.v.v.."),
				TEXT("v..v..v."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Aurora:    // northern lights: mint and violet bands
			Img = FFVImage::FromAscii({
				TEXT("...A...."),
				TEXT("..AmG..."),
				TEXT(".mAmGm.."),
				TEXT(".GmAmg.."),
				TEXT(".ggmgh.."),
				TEXT("..hhh..."),
				TEXT("........"),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Anchor:    // a brass anchor
			Img = FFVImage::FromAscii({
				TEXT("...yy..."),
				TEXT("...dy..."),
				TEXT(".dyyyyd."),
				TEXT("...dy..."),
				TEXT("y..dy..y"),
				TEXT("yd.dy.dy"),
				TEXT(".ddyydd."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Millstone:   // «Жёрнов»: a grey stone disc with dressing grooves, a dark eye hole, a red tip
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("...r...."),
				TEXT("..TKKT.."),
				TEXT(".TtKTtT."),
				TEXT(".KTjjTK."),
				TEXT(".TtTTtT."),
				TEXT("..tTTt.."),
				TEXT("........"),
			}, P);
			break;
		// update 6 (Р64): boutique expert collection
		case EFVBobberStyle::Helm:   // «Штурвал»: a ship's wheel with a brass hub
			Img = FFVImage::FromAscii({
				TEXT("...f...."),
				TEXT(".feeef.."),
				TEXT(".e.e.e.."),
				TEXT("feeyeef."),
				TEXT(".e.e.e.."),
				TEXT(".feeef.."),
				TEXT("...f...."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Dragon:   // «Драконий глаз»: red scales, gold horns and a slit eye
			Img = FFVImage::FromAscii({
				TEXT(".y...y.."),
				TEXT(".yrrry.."),
				TEXT(".rRrrrq."),
				TEXT(".rYEYrq."),
				TEXT(".rrrrqq."),
				TEXT(".qrqrq.."),
				TEXT("..qqq..."),
				TEXT("........"),
			}, P);
			break;
		case EFVBobberStyle::Golden:
		default:
			Img = FFVImage::FromAscii({
				TEXT("........"),
				TEXT("...Y...."),
				TEXT("..YYy..."),
				TEXT(".YwYyd.."),
				TEXT(".Yyyyd.."),
				TEXT(".yyydd.."),
				TEXT("..ddd..."),
				TEXT("........"),
			}, P);
			break;
		}
		Img.Outline(FColor(52, 30, 40));
		// update 5: the legend skins animate with the two bobbing frames (after the outline, 1px touches)
		const bool bF0 = Frame % 2 == 0;
		switch (Style)
		{
		case EFVBobberStyle::Tsar:
			// the glint jumps between the crown teeth, a soft radiance at the sides
			Img.Set(bF0 ? 1 : 5, 1, FColor(255, 255, 255));
			Img.Blend(7, bF0 ? 3 : 5, FColor(255, 220, 110, 110));
			Img.Blend(0, bF0 ? 5 : 3, FColor(255, 220, 110, 110));
			break;
		case EFVBobberStyle::Sturgeon:
			// a shimmer runs down the silver like a ripple
			if (bF0) { Img.Set(2, 3, FColor(255, 255, 255)); Img.Set(3, 2, FColor(255, 255, 255)); }
			else { Img.Set(2, 5, FColor(255, 255, 255)); Img.Set(3, 5, FColor(255, 255, 255)); }
			break;
		case EFVBobberStyle::Crystal:
			// the running glint: upper facet, then lower facet
			if (bF0) { Img.Set(3, 2, FColor(255, 255, 255)); Img.Set(2, 3, FColor(255, 255, 255)); }
			else { Img.Set(3, 4, FColor(255, 255, 255)); Img.Set(4, 5, FColor(255, 255, 255)); }
			break;
		case EFVBobberStyle::Lantern:
			if (bF0)
			{
				// the lantern pulses: bright with a halo over its outline ...
				const FIntPoint Halo[] = { {5, 1}, {6, 1}, {7, 2}, {7, 3}, {4, 2}, {4, 3}, {5, 4}, {6, 4} };
				for (const FIntPoint& H : Halo) { Img.Blend(H.X, H.Y, FColor(255, 236, 140, 120)); }
				Img.Set(5, 2, FColor(255, 255, 240));
			}
			else
			{
				// ... then dims
				Img.Set(5, 2, FColor(232, 206, 120)); Img.Set(6, 2, FColor(232, 206, 120));
				Img.Set(5, 3, FColor(232, 206, 120)); Img.Set(6, 3, FColor(232, 206, 120));
			}
			break;
		case EFVBobberStyle::Fog:
		{
			// a slow haze drifts around it (swaps sides between the frames)
			const FIntPoint HazeA[] = { {7, 2}, {7, 3}, {6, 1}, {0, 5}, {0, 6}, {1, 7} };
			const FIntPoint HazeB[] = { {7, 4}, {7, 5}, {6, 6}, {0, 2}, {0, 3}, {1, 1} };
			for (const FIntPoint& H : (bF0 ? HazeA : HazeB)) { Img.Blend(H.X, H.Y, FColor(200, 228, 212, 110)); }
			break;
		}
		case EFVBobberStyle::Diamond:
			// a four-point star flashes on alternate facets
			if (bF0) { Img.Set(3, 2, FColor(255, 255, 255)); Img.Blend(0, 1, FColor(255, 255, 255, 150)); Img.Blend(7, 4, FColor(200, 240, 255, 110)); }
			else { Img.Set(4, 3, FColor(255, 255, 255)); Img.Blend(7, 0, FColor(255, 255, 255, 150)); Img.Blend(0, 4, FColor(200, 240, 255, 110)); }
			break;
		case EFVBobberStyle::Marlin:
			// a wet glint slides along the sail
			Img.Set(bF0 ? 3 : 4, bF0 ? 1 : 2, FColor(255, 255, 255));
			break;
		case EFVBobberStyle::Kraken:
			// the tentacles curl
			if (bF0) { Img.Set(0, 6, FColor(176, 50, 100)); Img.Set(6, 6, FColor(176, 50, 100)); }
			else { Img.Set(1, 7, FColor(176, 50, 100)); Img.Set(5, 7, FColor(176, 50, 100)); Img.Set(0, 6, FColor(0, 0, 0, 0)); }
			break;
		case EFVBobberStyle::Aurora:
			// the glow changes hue
			Img.Blend(3, 0, bF0 ? FColor(150, 255, 190, 170) : FColor(196, 150, 255, 170));
			Img.Blend(6, 2, bF0 ? FColor(196, 150, 255, 120) : FColor(150, 255, 190, 120));
			Img.Blend(0, 3, bF0 ? FColor(150, 255, 190, 100) : FColor(196, 150, 255, 100));
			break;
		case EFVBobberStyle::Anchor:
			Img.Set(bF0 ? 3 : 4, bF0 ? 0 : 2, FColor(255, 246, 170));
			break;
		case EFVBobberStyle::Millstone:
			// the stone turns on the wave: the grooves run round, a puff of flour dust
			if (bF0) { Img.Set(2, 3, FColor(204, 200, 190)); Img.Set(5, 5, FColor(100, 94, 88)); Img.Blend(0, 2, FColor(250, 246, 232, 120)); }
			else { Img.Set(5, 3, FColor(204, 200, 190)); Img.Set(2, 5, FColor(100, 94, 88)); Img.Blend(7, 2, FColor(250, 246, 232, 120)); }
			break;
		case EFVBobberStyle::Helm:
			// the brass hub flashes, then a glint runs round the rim (the wheel «turns»)
			if (bF0) { Img.Set(3, 3, FColor(255, 255, 220)); Img.Set(1, 1, FColor(214, 150, 90)); }
			else { Img.Set(5, 1, FColor(214, 150, 90)); Img.Set(5, 5, FColor(255, 236, 150)); }
			break;
		case EFVBobberStyle::Dragon:
			// the eye glows and a puff of fire rises between the horns
			if (bF0) { Img.Set(3, 3, FColor(255, 250, 200)); Img.Blend(3, 0, FColor(255, 150, 40, 190)); }
			else { Img.Set(3, 3, FColor(40, 28, 48)); Img.Blend(3, 0, FColor(255, 220, 90, 150)); Img.Blend(7, 3, FColor(255, 120, 40, 110)); }
			break;
		case EFVBobberStyle::Zhar:
			// rare sparks fly off the flame crest
			if (bF0) { Img.Set(6, 0, FColor(255, 236, 140)); Img.Blend(7, 1, FColor(255, 170, 60, 170)); }
			else { Img.Set(0, 1, FColor(255, 236, 140)); Img.Blend(1, 0, FColor(255, 170, 60, 170)); Img.Set(3, 0, FColor(255, 255, 220)); }
			break;
		default:
			break;
		}
		if (Frame % 2 == 1)
		{
			FFVImage Dip(8, 8);
			Dip.Blit(Img, 0, 1);
			return Dip;
		}
		return Img;
	}

	// ============================================================================================
	// Pets (16x16, facing right). Special template chars: 'T' only in pose 0, 'U' only in pose 1
	// (tail wag / puffs), 'E' eye (closed on blink frames). A 1px outline is added automatically.
	// ============================================================================================
	struct FPetColors { FColor Main, Shade, Light, Second, Accent, Nose, Dark; };

	FPetColors PetColors(EFVPetStyle P)
	{
		switch (P)
		{
		case EFVPetStyle::Cat:     return { FColor(238, 150, 66), FColor(196, 104, 46), FColor(252, 194, 116), FColor(252, 238, 214), FColor(200, 106, 44), FColor(244, 130, 140), FColor(170, 86, 38) };
		case EFVPetStyle::Dog:     return { FColor(190, 126, 70), FColor(146, 90, 50), FColor(222, 166, 104), FColor(250, 232, 200), FColor(214, 56, 56), FColor(48, 34, 40), FColor(112, 66, 40) };
		case EFVPetStyle::Duck:    return { FColor(250, 248, 238), FColor(206, 204, 196), FColor(255, 255, 255), FColor(250, 150, 40), FColor(250, 150, 40), FColor(214, 110, 30), FColor(160, 160, 166) };
		case EFVPetStyle::Fox:     return { FColor(236, 122, 48), FColor(190, 84, 34), FColor(252, 170, 90), FColor(252, 244, 232), FColor(70, 44, 44), FColor(48, 30, 36), FColor(70, 44, 44) };
		case EFVPetStyle::Frog:    return { FColor(104, 184, 74), FColor(66, 136, 58), FColor(160, 220, 110), FColor(214, 234, 150), FColor(58, 110, 50), FColor(40, 28, 48), FColor(58, 110, 50) };
		case EFVPetStyle::Squirrel:   return { FColor(206, 110, 50), FColor(160, 74, 36), FColor(240, 160, 90), FColor(250, 232, 200), FColor(150, 96, 50), FColor(60, 36, 40), FColor(120, 60, 30) };
		case EFVPetStyle::Hedgehog:   return { FColor(150, 120, 96), FColor(110, 86, 70), FColor(196, 170, 140), FColor(236, 206, 160), FColor(220, 120, 90), FColor(40, 28, 36), FColor(86, 66, 56) };
		case EFVPetStyle::CaptainCat: return { FColor(150, 156, 170), FColor(110, 114, 130), FColor(200, 206, 216), FColor(250, 250, 246), FColor(34, 50, 92), FColor(244, 130, 140), FColor(80, 84, 100) };
		case EFVPetStyle::Peacock:    return { FColor(40, 110, 200), FColor(26, 70, 150), FColor(100, 180, 240), FColor(60, 160, 100), FColor(240, 200, 70), FColor(200, 186, 150), FColor(30, 50, 110) };
		case EFVPetStyle::Dragonet:   return { FColor(196, 56, 44), FColor(140, 34, 34), FColor(236, 106, 76), FColor(252, 208, 110), FColor(255, 150, 50), FColor(70, 30, 30), FColor(110, 26, 30) };
		case EFVPetStyle::Penguin: return { FColor(58, 62, 84), FColor(38, 40, 60), FColor(96, 102, 130), FColor(250, 250, 246), FColor(250, 160, 40), FColor(220, 120, 30), FColor(210, 214, 222) };
		default: break;
		}
		return { FColor::White, FColor::White, FColor::White, FColor::White, FColor::White, FColor::White, FColor::White };
	}

	// Quadrupeds share leg templates (rows 12..14).
	const TCHAR* const QuadLegsIdle[] = {
		TEXT("....Aa....Aa...."),
		TEXT("....Aa....Aa...."),
		TEXT("....Aa....Aa...."),
	};
	const TCHAR* const QuadLegsWalkA[] = {
		TEXT("....Aa....Aa...."),
		TEXT("...A..a..a..A..."),
		TEXT("...A..a..a..A..."),
	};
	const TCHAR* const QuadLegsWalkB[] = {
		TEXT("....aA....aA...."),
		TEXT("...a..A..A..a..."),
		TEXT("...a..A..A..a..."),
	};

	// --- Cat
	const TCHAR* const CatBody[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT(".........A..A..."),
		TEXT("..T......AA.AA.."),
		TEXT(".TT......ZAAAA.."),
		TEXT(".T..U....AAAEAN."),
		TEXT(".T..U....AAAACC."),
		TEXT("..TUUkAkAkAACC.."),
		TEXT("..AAAAAAAAAAa..."),
		TEXT("...AkAkAkAAAa..."),
		TEXT("...CCCCCCCCCa..."),
		TEXT("...aaaaaaaaaa..."),
	};
	const TCHAR* const CatSit[] = {
		TEXT("................"),
		TEXT("........A..A...."),
		TEXT("........AA.AA..."),
		TEXT("........ZAAAA..."),
		TEXT("........AAAEAN.."),
		TEXT("........AAAACC.."),
		TEXT(".......kAAAACC.."),
		TEXT("......AAkACCC..."),
		TEXT("......AAAACCC..."),
		TEXT(".....AkAAACCC..."),
		TEXT(".....AAkAACCa..."),
		TEXT(".....AAAAAACa..."),
		TEXT("..TTUAAAAAACa..."),
		TEXT(".TUUUaAaaACaC..."),
		TEXT("......aaaaaaa..."),
		TEXT("................"),
	};
	// --- Dog
	const TCHAR* const DogBody[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT(".........AAAA..."),
		TEXT("........kAAAAA.."),
		TEXT("..T.....kAAEAA.."),
		TEXT(".T..U...kkACCCN."),
		TEXT(".T..U....AACCC.."),
		TEXT("..TUAAAAAddd...."),
		TEXT("...AAAAAAAAAa..."),
		TEXT("...AAAAAAAAAa..."),
		TEXT("...ACCCCCCCAa..."),
		TEXT("...aaaaaaaaaa..."),
	};
	const TCHAR* const DogSit[] = {
		TEXT("................"),
		TEXT("........AAAA...."),
		TEXT(".......kAAAAA..."),
		TEXT(".......kAAEAA..."),
		TEXT(".......kkACCCN.."),
		TEXT("........AACCC..."),
		TEXT("........dddA...."),
		TEXT("......AAACCA...."),
		TEXT(".....AAAACCA...."),
		TEXT(".....AAAACCA...."),
		TEXT(".....AAAAACA...."),
		TEXT(".....AAAAACAa..."),
		TEXT("..TTUAAAAACAa..."),
		TEXT(".TUUUaaAaACaC..."),
		TEXT("......aaaaaaa..."),
		TEXT("................"),
	};
	// --- Fox
	const TCHAR* const FoxBody[] = {
		TEXT("................"),
		TEXT(".........k..k..."),
		TEXT(".........AA.AA.."),
		TEXT(".........AAAAA.."),
		TEXT("..TT.....AAAEAA."),
		TEXT(".TTTUU...AACCCCN"),
		TEXT(".CTUUU....ACCC.."),
		TEXT(".CCUUUAAAAACC..."),
		TEXT("...AAAAAAAACC..."),
		TEXT("...AAAAAAAAAa..."),
		TEXT("...ACCCCCCCAa..."),
		TEXT("...aaaaaaaaaa..."),
	};
	const TCHAR* const FoxSit[] = {
		TEXT("................"),
		TEXT("........k..k...."),
		TEXT("........AA.AA..."),
		TEXT("........AAAAA..."),
		TEXT("........AAAEAA.."),
		TEXT("........AACCCCN."),
		TEXT(".........ACCC..."),
		TEXT("......AAACCC...."),
		TEXT(".....AAAACCC...."),
		TEXT(".....AAAACCC...."),
		TEXT(".....AAAAACCa..."),
		TEXT("..TT.AAAAACCa..."),
		TEXT(".TTTUAAAAACCa..."),
		TEXT(".CCUUUAaaaDaD..."),
		TEXT("..CCUUaaaaaaa..."),
		TEXT("................"),
	};

	const TCHAR* const DuckSit[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("..........AAA..."),
		TEXT(".........ZAAEbb."),
		TEXT(".........AAAABB."),
		TEXT(".........AAAA..."),
		TEXT("..T......AAAA..."),
		TEXT(".UTAAAAAAAAAAa.."),
		TEXT(".UAAAaaaaAAAAa.."),
		TEXT("..AAAAaaaaAAAa.."),
		TEXT("..aAAAAAAAAAaa.."),
		TEXT("...aaaaaaaaaa..."),
		TEXT("................"),
		TEXT("................"),
	};
	const TCHAR* const DuckBody[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT("..........AAA..."),
		TEXT(".........ZAAEbb."),
		TEXT(".........AAAABB."),
		TEXT(".........AAAA..."),
		TEXT("..T......AAAA..."),
		TEXT(".UTAAAAAAAAAAa.."),
		TEXT(".UAAAaaaaAAAAa.."),
		TEXT("..AAAAaaaaAAAa.."),
		TEXT("..aAAAAAAAAAaa.."),
		TEXT("...aaaaaaaaaa..."),
	};
	const TCHAR* const DuckFeetA[] = {
		TEXT(".....b....B....."),
		TEXT("....b......B...."),
		TEXT("....bb.....BB..."),
	};
	const TCHAR* const DuckFeetB[] = {
		TEXT(".....B....b....."),
		TEXT("....B......b...."),
		TEXT("....BB.....bb..."),
	};
	const TCHAR* const DuckFeetIdle[] = {
		TEXT("......B..b......"),
		TEXT("......B..b......"),
		TEXT("......BB.bb....."),
	};
	const TCHAR* const PenguinBody[] = {
		TEXT("................"),
		TEXT("......AAAA......"),
		TEXT(".....ZAAAAA....."),
		TEXT(".....AAACEA....."),
		TEXT(".....AACCCbb...."),
		TEXT(".....AACCCBb...."),
		TEXT("....AACCCCC....."),
		TEXT("...TaACCCCCU...."),
		TEXT("...TaACCCCCU...."),
		TEXT("....aACCCCC....."),
		TEXT("....aACCCCC....."),
		TEXT("....aaCCCCC....."),
		TEXT(".....aaCCCa....."),
	};
	const TCHAR* const PenguinFeetA[] = {
		TEXT(".....bb..BB....."),
		TEXT("..........BB...."),
	};
	const TCHAR* const PenguinFeetB[] = {
		TEXT(".....BB..bb....."),
		TEXT(".....bbb........"),
	};
	const TCHAR* const PenguinFeetIdle[] = {
		TEXT(".....bb..bb....."),
		TEXT(".....bbb.bbb...."),
	};
	const TCHAR* const FrogCrouch[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT("..........WW...."),
		TEXT(".........ZWEA..."),
		TEXT(".....ZZAAAAAAA.."),
		TEXT("...ZAAkAAAAAAAm."),
		TEXT("..AAAAAAAkAAACC."),
		TEXT("..AAkAAAAAACCCU."),
		TEXT(".aAAAAaAACCCCU.."),
		TEXT(".aaAAAAaCCCaA..."),
		TEXT("..aaaaaa..aAA..."),
		TEXT("...aa.aa..aa.a.."),
	};
	const TCHAR* const FrogLeap[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT("..........WW...."),
		TEXT(".........ZWEA..."),
		TEXT(".....ZZAAAAAAA.."),
		TEXT("...ZAAkAAAAAAAm."),
		TEXT("..AAAAAAAkAAACC."),
		TEXT(".aAAkAAAAAACCC.."),
		TEXT("aa.AAAAaACCCCaA."),
		TEXT("a...aaaaaCC.aA.."),
		TEXT("..........aa...."),
		TEXT("................"),
	};

	// --- update 6 [ART-WEAR] Squirrel (bushy tail over the back)
	const TCHAR* const SquirrelBody[] = {
		TEXT(".TTTT..........."),
		TEXT("AAZAAT....A....."),
		TEXT("AZAAAA...AA....."),
		TEXT("AAAAAA..ZAAA...."),
		TEXT(".AAAAA..AAAEA..."),
		TEXT("..UAAA.AAAAAAN.."),
		TEXT("...AAAAAAAACC..."),
		TEXT("....AAAAAACCC..."),
		TEXT("....AAAAAACCC..."),
		TEXT(".....AAAAACCa..."),
		TEXT(".....aAaaaaa...."),
		TEXT("................"),
	};
	const TCHAR* const SquirrelSit[] = {
		TEXT("................"),
		TEXT("..........A....."),
		TEXT(".........AA.A..."),
		TEXT("TTT......ZAAA..."),
		TEXT("TZTT.....AAAEAN."),
		TEXT("AZAAT....AAAACC."),
		TEXT("AAZAA...AAAACC.."),
		TEXT(".AAAAA..AAACdd.."),
		TEXT("..AAAA.AAAACdo.."),
		TEXT("...UAAAAAACCC..."),
		TEXT("....AAAAAACCC..."),
		TEXT("....AAAAAACCa..."),
		TEXT("....AAAAAACCa..."),
		TEXT("....aAaaAaCa...."),
		TEXT("....aa...aa....."),
		TEXT("................"),
	};
	// --- Hedgehog (spines = dark / main)
	const TCHAR* const HedgehogBody[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("....kAkAk......."),
		TEXT("..kAkAkAkAk....."),
		TEXT(".kAkZkAkAkAC...."),
		TEXT("kAkAkAkAkACCC..."),
		TEXT("TkAkAkAkAkCCEC.."),
		TEXT("UAkAkAkAkACCCCN."),
		TEXT(".aaaaaaaaaCCCC.."),
		TEXT("..aaaaaaaaaa...."),
	};
	const TCHAR* const HedgehogSit[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
		TEXT("......kAkAk....."),
		TEXT("....kAkAkAkAk..."),
		TEXT("...kAkZkAkAkAC.."),
		TEXT("..kAkAkAkAkCCC.."),
		TEXT("..AkAkAkAkCCEC.."),
		TEXT("..kAkAkAkACCCCN."),
		TEXT("..AkAkAkAkCCCC.."),
		TEXT("...aaaaaaaaaaa.."),
		TEXT("....aa....aa...."),
		TEXT("................"),
	};
	// --- Peacock (blue body, green fan with gold eyes; 'c' = shaded fan)
	const TCHAR* const PeacockBody[] = {
		TEXT("..........O.O..."),
		TEXT("...........A...."),
		TEXT("..........AAA..."),
		TEXT(".........ZAAEbb."),
		TEXT(".........AAAAB.."),
		TEXT("..TUT.....AAA..."),
		TEXT(".TCOCT....AAA..."),
		TEXT("TCODOCTAAAAAAa.."),
		TEXT("UCCOCCAAAaaAAa.."),
		TEXT(".UCCCAAAaaAAAa.."),
		TEXT("..UCCaAAAAAAa..."),
		TEXT("....ccaaaaaaa..."),
	};
	const TCHAR* const PeacockSit[] = {   // resting with the fan spread open behind
		TEXT("..TCUCTCU......."),
		TEXT(".CCOCCOCCC......"),
		TEXT("TCODOCODOCT....."),
		TEXT("CCCOCCCOCCC.O.O."),
		TEXT("UCODOCODOCUC.A.."),
		TEXT("CCCCOCCOCCCCAAA."),
		TEXT(".CCOCCCCOCCZAAEb"),
		TEXT("..cCCCCCCCcAAAAB"),
		TEXT("...cccAAAAAAAA.."),
		TEXT("......AAAAAAAa.."),
		TEXT(".....aAAAaaAAa.."),
		TEXT("......aAAAAAa..."),
		TEXT(".......aaaaa...."),
		TEXT("................"),
		TEXT("................"),
		TEXT("................"),
	};

	// --- Dragonet (Р64: red scales, gold belly, orange wings; 'M'/'B' = wing up (pose 0), 'm'/'b' = wing down (pose 1))
	const TCHAR* const DragonetBody[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT(".....M.....d.d.."),
		TEXT("....MMB...ZAAA.."),
		TEXT("...MMMB...AAEAA."),
		TEXT("...MMBA..AAAAAAN"),
		TEXT("..mmmbAA.AACAa.."),
		TEXT(".mmmmAAAAAACCa.."),
		TEXT("d..AAkAkAAACCa.."),
		TEXT("dd.AAAAAAAAACa.."),
		TEXT(".dAAAkAkAkACCa.."),
		TEXT("...aaaaaaaaaa..."),
	};
	const TCHAR* const DragonetSit[] = {
		TEXT("................"),
		TEXT("................"),
		TEXT("..........d.d..."),
		TEXT(".........ZAAA..."),
		TEXT(".........AAEAA.."),
		TEXT("....M....AAAAAN."),
		TEXT("...MMB...CAAa..."),
		TEXT("..MMMBAAACCa...."),
		TEXT("..mmbAAAACCC...."),
		TEXT(".mmmAAkAACCC...."),
		TEXT(".....AAAkACC...."),
		TEXT(".....AkAAACCa..."),
		TEXT("..d..AAAAAACa..."),
		TEXT(".ddAAAaAAaACa..."),
		TEXT("......aaaaaaa..."),
		TEXT("................"),
	};

	/** Update 6: the captain's cap on the cat (white crown, navy band, gold badge, black visor). */
	void CaptainCatCap(FFVImage& Img, int32 X0, int32 Y0)
	{
		const FColor Wt(250, 250, 246), WtS(214, 216, 226), Navy(34, 50, 92), Visor(40, 34, 44), Gold(255, 212, 80);
		for (int32 X = X0 + 1; X <= X0 + 5; ++X) { Img.Set(X, Y0, Wt); }
		for (int32 X = X0; X <= X0 + 6; ++X) { Img.Set(X, Y0 + 1, X == X0 + 6 ? WtS : Wt); Img.Set(X, Y0 + 2, Navy); }
		Img.Set(X0 + 3, Y0 + 1, Gold);
		Img.Set(X0 + 6, Y0 + 3, Visor); Img.Set(X0 + 7, Y0 + 3, Visor); Img.Set(X0 + 7, Y0 + 2, Visor);
	}

	void AddPetCommon(TMap<TCHAR, FColor>& P, const FPetColors& C)
	{
		P.Add(TEXT('A'), C.Main);
		P.Add(TEXT('a'), C.Shade);
		P.Add(TEXT('Z'), C.Light);
		P.Add(TEXT('C'), C.Second);
		P.Add(TEXT('k'), C.Dark);
		P.Add(TEXT('d'), C.Accent);
		P.Add(TEXT('D'), C.Dark);
		P.Add(TEXT('N'), C.Nose);
		P.Add(TEXT('O'), C.Accent);
		P.Add(TEXT('o'), FVArt::Shade(C.Accent, 0.3f));
		P.Add(TEXT('W'), FColor(255, 255, 255));
	}

	FFVImage BuildPetImpl(EFVPetStyle Style, EFVPetAnim Anim, int32 Frame)
	{
		FFVImage Img(16, 16);
		if (Style == EFVPetStyle::None || Style >= EFVPetStyle::Count)
		{
			return Img;
		}
		const int32 NumFrames = UFVSprites::PetFrameCount(Anim);
		Frame = ((Frame % NumFrames) + NumFrames) % NumFrames;
		const FPetColors C = PetColors(Style);
		TMap<TCHAR, FColor> Pal;
		AddPetCommon(Pal, C);

		// pose 0/1 (tail wag), blink
		int32 Pose = 0;
		bool bBlink = false;
		switch (Anim)
		{
		case EFVPetAnim::Idle: Pose = Frame; break;
		case EFVPetAnim::Walk: Pose = Frame % 2; break;
		case EFVPetAnim::Sit:  Pose = Frame; bBlink = Frame == 1; break;
		default: break;
		}
		if (Pose == 0) { Pal.Add(TEXT('T'), C.Main); }
		else { Pal.Add(TEXT('U'), C.Main); }
		Pal.Add(TEXT('E'), bBlink ? C.Shade : FColor(36, 26, 44));

		const FColor Outline = FVArt::OutlineOf(C.Shade);

		const bool bQuad = Style == EFVPetStyle::Cat || Style == EFVPetStyle::Dog || Style == EFVPetStyle::Fox
			|| Style == EFVPetStyle::CaptainCat || Style == EFVPetStyle::Squirrel || Style == EFVPetStyle::Hedgehog;
		if (bQuad)
		{
			if (Style == EFVPetStyle::Fox)
			{
				// dark socks
				Pal.Add(TEXT('A'), C.Main);
			}
			if (Anim == EFVPetAnim::Sit)
			{
				const FTpl T = (Style == EFVPetStyle::Cat || Style == EFVPetStyle::CaptainCat) ? FTpl{ FV_TPL(CatSit) }
					: Style == EFVPetStyle::Dog ? FTpl{ FV_TPL(DogSit) } : Style == EFVPetStyle::Squirrel ? FTpl{ FV_TPL(SquirrelSit) }
					: Style == EFVPetStyle::Hedgehog ? FTpl{ FV_TPL(HedgehogSit) } : FTpl{ FV_TPL(FoxSit) };
				Stamp(Img, 0, 0, T.Rows, T.N, Pal);
				if (Style == EFVPetStyle::CaptainCat) { CaptainCatCap(Img, 7, 0); }
			}
			else
			{
				const FTpl Body = (Style == EFVPetStyle::Cat || Style == EFVPetStyle::CaptainCat) ? FTpl{ FV_TPL(CatBody) }
					: Style == EFVPetStyle::Dog ? FTpl{ FV_TPL(DogBody) } : Style == EFVPetStyle::Squirrel ? FTpl{ FV_TPL(SquirrelBody) }
					: Style == EFVPetStyle::Hedgehog ? FTpl{ FV_TPL(HedgehogBody) } : FTpl{ FV_TPL(FoxBody) };
				const int32 BodyDY = (Anim == EFVPetAnim::Walk && Frame % 2 == 1) ? 0 : 1;
				FTpl Legs{ FV_TPL(QuadLegsIdle) };
				if (Anim == EFVPetAnim::Walk)
				{
					Legs = Frame == 0 ? FTpl{ FV_TPL(QuadLegsWalkA) } : Frame == 2 ? FTpl{ FV_TPL(QuadLegsWalkB) } : FTpl{ FV_TPL(QuadLegsIdle) };
				}
				TMap<TCHAR, FColor> LegPal;
				LegPal.Add(TEXT('A'), Style == EFVPetStyle::Fox ? C.Dark : C.Main);
				LegPal.Add(TEXT('a'), Style == EFVPetStyle::Fox ? FVArt::Shade(C.Dark, 0.3f) : C.Shade);
				Stamp(Img, 0, 12, Legs.Rows, Legs.N, LegPal);
				Stamp(Img, 0, BodyDY, Body.Rows, Body.N, Pal);
				if (BodyDY == 0)
				{
					// stretch the legs one row up so they stay attached to the body
					Stamp(Img, 0, 11, Legs.Rows, 1, LegPal);
				}
				if (Style == EFVPetStyle::CaptainCat) { CaptainCatCap(Img, 8, 1 + BodyDY); }
			}
			Img.Outline(Outline);
			return Img;
		}

		if (Style == EFVPetStyle::Dragonet)
		{
			// update 6 (Р64): walks on four legs, the wings flap between the poses, sparks fly from the nose
			const FColor NoCol(0, 0, 0, 0);
			Pal.Add(TEXT('M'), Pose == 0 ? C.Accent : NoCol);
			Pal.Add(TEXT('B'), Pose == 0 ? C.Dark : NoCol);
			Pal.Add(TEXT('m'), Pose == 1 ? FVArt::Shade(C.Accent, 0.15f) : NoCol);
			Pal.Add(TEXT('b'), Pose == 1 ? C.Dark : NoCol);
			int32 DY = 0;
			if (Anim == EFVPetAnim::Sit)
			{
				Stamp(Img, 0, 0, FV_TPL(DragonetSit), Pal);
			}
			else
			{
				DY = (Anim == EFVPetAnim::Walk && Frame % 2 == 1) ? 0 : 1;
				FTpl Legs{ FV_TPL(QuadLegsIdle) };
				if (Anim == EFVPetAnim::Walk)
				{
					Legs = Frame == 0 ? FTpl{ FV_TPL(QuadLegsWalkA) } : Frame == 2 ? FTpl{ FV_TPL(QuadLegsWalkB) } : FTpl{ FV_TPL(QuadLegsIdle) };
				}
				TMap<TCHAR, FColor> LegPal;
				LegPal.Add(TEXT('A'), C.Main);
				LegPal.Add(TEXT('a'), C.Shade);
				Stamp(Img, 0, 12, Legs.Rows, Legs.N, LegPal);
				Stamp(Img, 0, DY, FV_TPL(DragonetBody), Pal);
				if (DY == 0) { Stamp(Img, 0, 11, Legs.Rows, 1, LegPal); }
			}
			Img.Outline(Outline);
			// elite shine: a spark from the nose on the second frame, a glint on the horn on the first
			if (Anim == EFVPetAnim::Sit)
			{
				if (Frame == 1) { Img.Blend(15, 4, FColor(255, 200, 80, 200)); }
				Img.Set(10, 2, FColor(255, 236, 170));
			}
			else if (Frame % 2 == 1) { Img.Blend(15, 3 + DY, FColor(255, 200, 80, 210)); Img.Blend(14, 2 + DY, FColor(255, 240, 150, 150)); }
			else { Img.Set(11, 2 + DY, FColor(255, 236, 170)); }
			return Img;
		}

		if (Style == EFVPetStyle::Peacock)
		{
			// update 6: a peacock walks like the duck; the fan shimmers between the poses
			Pal.Add(TEXT('b'), FColor(200, 186, 150));   // beak
			Pal.Add(TEXT('B'), FColor(150, 136, 110));
			Pal.Add(TEXT('c'), FVArt::Shade(C.Second, 0.3f));
			Pal.Add(TEXT('T'), Pose == 0 ? C.Second : FVArt::Light(C.Second, 0.3f));
			Pal.Add(TEXT('U'), Pose == 0 ? FVArt::Light(C.Second, 0.3f) : C.Second);
			const int32 Bob = (Anim == EFVPetAnim::Walk && Frame % 2 == 1) ? -1 : 0;
			if (Anim == EFVPetAnim::Sit)
			{
				Stamp(Img, 0, 0, FV_TPL(PeacockSit), Pal);
			}
			else
			{
				Stamp(Img, 0, Bob, FV_TPL(PeacockBody), Pal);
				TMap<TCHAR, FColor> FeetPal;
				FeetPal.Add(TEXT('b'), FColor(150, 140, 130));
				FeetPal.Add(TEXT('B'), FColor(110, 100, 96));
				if (Anim == EFVPetAnim::Walk && Frame == 0) { Stamp(Img, 0, 12 + Bob, FV_TPL(DuckFeetA), FeetPal); }
				else if (Anim == EFVPetAnim::Walk && Frame == 2) { Stamp(Img, 0, 12 + Bob, FV_TPL(DuckFeetB), FeetPal); }
				else { Stamp(Img, 0, 12 + Bob, FV_TPL(DuckFeetIdle), FeetPal); }
			}
			Img.Outline(Outline);
			// a glint on the fan eyes (elite shine)
			Img.Set(Pose == 0 ? 3 : 2, Anim == EFVPetAnim::Sit ? 2 : 7 + Bob, FColor(255, 246, 190));
			return Img;
		}

		if (Style == EFVPetStyle::Duck)
		{
			Pal.Add(TEXT('b'), FColor(250, 150, 40));     // beak & feet
			Pal.Add(TEXT('B'), FColor(214, 110, 30));
			const int32 Bob = (Anim == EFVPetAnim::Walk && Frame % 2 == 1) ? -1 : 0;
			if (Anim == EFVPetAnim::Sit)
			{
				Stamp(Img, 0, 0, FV_TPL(DuckSit), Pal);
			}
			else
			{
				Stamp(Img, 0, Bob, FV_TPL(DuckBody), Pal);
				TMap<TCHAR, FColor> FeetPal;
				FeetPal.Add(TEXT('b'), FColor(250, 150, 40));
				FeetPal.Add(TEXT('B'), FColor(214, 110, 30));
				if (Anim == EFVPetAnim::Walk && Frame == 0)
				{
					Stamp(Img, 0, 12 + Bob, FV_TPL(DuckFeetA), FeetPal);
				}
				else if (Anim == EFVPetAnim::Walk && Frame == 2)
				{
					Stamp(Img, 0, 12 + Bob, FV_TPL(DuckFeetB), FeetPal);
				}
				else
				{
					Stamp(Img, 0, 12 + Bob, FV_TPL(DuckFeetIdle), FeetPal);
				}
			}
			Img.Outline(Outline);
			return Img;
		}

		if (Style == EFVPetStyle::Penguin)
		{
			Pal.Add(TEXT('b'), FColor(250, 160, 40));
			Pal.Add(TEXT('B'), FColor(214, 120, 30));
			const int32 Sway = Anim == EFVPetAnim::Walk ? (Frame == 0 ? -1 : (Frame == 2 ? 1 : 0)) : 0;
			const int32 Bob = (Anim == EFVPetAnim::Walk && Frame % 2 == 1) ? -1 : 0;
			const bool bSit = Anim == EFVPetAnim::Sit;
			const int32 SitDY = bSit ? 2 : 0;
			Stamp(Img, Sway, Bob + SitDY, FV_TPL(PenguinBody), Pal);
			if (!bSit)
			{
				TMap<TCHAR, FColor> FeetPal;
				FeetPal.Add(TEXT('b'), FColor(250, 160, 40));
				FeetPal.Add(TEXT('B'), FColor(214, 120, 30));
				if (Anim == EFVPetAnim::Walk && (Frame == 0 || Frame == 2))
				{
					const bool bA = Frame == 0;
					if (bA) { Stamp(Img, Sway, 13 + Bob, FV_TPL(PenguinFeetA), FeetPal); }
					else { Stamp(Img, Sway, 13 + Bob, FV_TPL(PenguinFeetB), FeetPal); }
				}
				else
				{
					Stamp(Img, 0, 13 + Bob, FV_TPL(PenguinFeetIdle), FeetPal);
				}
			}
			Img.Outline(Outline);
			return Img;
		}

		// Frog
		{
			Pal.Add(TEXT('m'), FVArt::Shade(C.Shade, 0.4f));
			int32 DY = 0;
			int32 LegPose = 0;   // 0 crouch, 1 extended
			if (Anim == EFVPetAnim::Walk)
			{
				static const int32 HopY[4] = { 0, -2, -3, -1 };
				DY = HopY[Frame];
				LegPose = (Frame == 1 || Frame == 2) ? 1 : 0;
			}
			if (Anim == EFVPetAnim::Sit)
			{
				Pal.Add(TEXT('E'), bBlink ? C.Main : FColor(36, 26, 44));
			}
			if (LegPose == 0)
			{
				Stamp(Img, 0, 3 + DY, FV_TPL(FrogCrouch), Pal);
			}
			else
			{
				Stamp(Img, 0, 3 + DY, FV_TPL(FrogLeap), Pal);
			}
			Img.Outline(Outline);
			return Img;
		}
	}
}

// ============================================================================================
// UFVSprites (character module)
// ============================================================================================
int32 UFVSprites::CharacterFrameCount(EFVCharAnim Anim)
{
	switch (Anim)
	{
	case EFVCharAnim::Idle: return 2;
	case EFVCharAnim::Walk: return 4;
	case EFVCharAnim::FishWait: return 2;
	case EFVCharAnim::Reel: return 2;
	default: return 1;
	}
}

int32 UFVSprites::PetFrameCount(EFVPetAnim Anim)
{
	switch (Anim)
	{
	case EFVPetAnim::Idle: return 2;
	case EFVPetAnim::Walk: return 4;
	case EFVPetAnim::Sit: return 2;
	default: return 1;
	}
}

FVector2D UFVSprites::RodTipOffset(EFVCharAnim Anim, EFVFacing Facing, int32 Frame)
{
	const int32 NumFrames = CharacterFrameCount(Anim);
	const int32 F = ((Frame % NumFrames) + NumFrames) % NumFrames;
	const FRodPose P = RodPose(Anim, F);
	if (P.bValid)
	{
		return FVector2D(P.Tip.X + 0.5f, P.Tip.Y + 0.5f);
	}
	if (Anim == EFVCharAnim::HoldFish)
	{
		return FVector2D(23.5f + 0.5f, 12.5f + 0.5f);
	}
	// Hand position for walking/idle.
	switch (Facing)
	{
	case EFVFacing::Right: return FVector2D(16.f, 35.f);
	case EFVFacing::Left:  return FVector2D(16.f, 35.f);
	case EFVFacing::Up:    return FVector2D(10.f, 35.f);
	case EFVFacing::Down:
	default:               return FVector2D(22.f, 35.f);
	}
}

FFVImage UFVSprites::BuildCharacter(const FFVAppearance& Look, EFVCharAnim Anim, EFVFacing Facing, int32 Frame)
{
	return BuildCharacterImpl(Look, Anim, Facing, Frame);
}

FFVImage UFVSprites::BuildHatIcon(EFVHatStyle Style, FColor Primary, FColor Secondary)
{
	FFVImage Icon(16, 16);
	if (Style == EFVHatStyle::None || Style >= EFVHatStyle::Count)
	{
		// "no hat": a soft grey crossed circle
		const FColor Ring(150, 140, 130, 200);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const float DX = X + 0.5f - 8.f, DY = Y + 0.5f - 8.f;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if ((D >= 4.6f && D <= 6.0f) || (FMath::Abs(DX + DY) <= 0.8f && D < 5.2f))
				{
					Icon.Set(X, Y, Ring);
				}
			}
		}
		return Icon;
	}
	FFVAppearance L;
	L.Hat = Style;
	L.HatPrimary = Primary;
	L.HatSecondary = Secondary;
	const FCharStyle S = MakeStyle(L);
	const FHatDef Hat = HatFor(Style);
	if (!Hat.Front.Rows)
	{
		return Icon;
	}
	TArray<FString> Rows;
	for (int32 I = 0; I < Hat.Front.N; ++I)
	{
		Rows.Add(FString(Hat.Front.Rows[I]));
	}
	FFVImage T = FFVImage::FromAscii(Rows, S.HatPal);
	if (T.W > 14)
	{
		T = T.Cropped((T.W - 14) / 2, 0, 14, T.H);
	}
	if (T.H > 14)
	{
		T = T.Cropped(0, T.H - 14, T.W, 14);
	}
	Icon.Blit(T, (16 - T.W) / 2, (16 - T.H) / 2);
	Icon.Outline(S.Outline);
	return Icon;
}

FFVImage UFVSprites::BuildOutfitIcon(EFVOutfitStyle Style, FColor Primary, FColor Secondary)
{
	FFVAppearance L;
	L.Outfit = Style;
	L.OutfitPrimary = Primary;
	L.OutfitSecondary = Secondary;
	FCharStyle S = MakeStyle(L);
	// the collar opening is transparent on the icon
	S.Pal.Add(TEXT('K'), FColor(0, 0, 0, 0));
	S.Pal.Add(TEXT('k'), FColor(0, 0, 0, 0));

	FFVImage Icon(16, 16);
	const FTpl Torso = TorsoTpl(Style, EFVFacing::Down);
	const bool bLong = S.bLongCoat;
	const int32 TopY = bLong ? 1 : 2;
	Stamp(Icon, 3, TopY, Torso.Rows, Torso.N, S.Pal);
	if (bLong)
	{
		Stamp(Icon, 3, TopY + 9, FV_TPL(RaincoatSkirtDown), S.Pal);
	}
	else if (Style == EFVOutfitStyle::Overalls || Style == EFVOutfitStyle::Suit || Style == EFVOutfitStyle::Sailor)
	{
		// short trouser stubs under the torso
		for (int32 Y = TopY + 9; Y < TopY + 12; ++Y)
		{
			for (int32 X = 3; X <= 12; ++X)
			{
				if (X == 7 || X == 8)
				{
					if (Y > TopY + 9) { continue; }
				}
				Icon.Set(X, Y, X == 12 || X == 6 ? *S.Pal.Find(TEXT('p')) : *S.Pal.Find(TEXT('P')));
			}
		}
	}
	// sleeves spread out like a folded shirt
	const int32 SleeveLen = S.bShortSleeves ? 3 : 6;
	for (int32 I = 0; I < SleeveLen; ++I)
	{
		const int32 Y = TopY + I;
		Icon.Set(1, Y + 1, S.Sleeve); Icon.Set(2, Y, S.Sleeve); Icon.Set(2, Y + 1, S.Sleeve);
		Icon.Set(13, Y, S.SleeveS); Icon.Set(13, Y + 1, S.SleeveS); Icon.Set(14, Y + 1, S.SleeveS);
	}
	Icon.Outline(S.Outline);
	return Icon;
}

FFVImage UFVSprites::BuildBobber(EFVBobberStyle Style, int32 Frame)
{
	return BuildBobberImpl(Style, Frame);
}

FFVImage UFVSprites::BuildPet(EFVPetStyle Style, EFVPetAnim Anim, int32 Frame)
{
	return BuildPetImpl(Style, Anim, Frame);
}

// ---------------------------------------------------------------------------------------------- cached getters
const FFVSprite& UFVSprites::Character(const FFVAppearance& Look, EFVCharAnim Anim, EFVFacing Facing, int32 Frame)
{
	const int32 NumFrames = CharacterFrameCount(Anim);
	const int32 F = ((Frame % NumFrames) + NumFrames) % NumFrames;
	const bool bFishing = Anim != EFVCharAnim::Idle && Anim != EFVCharAnim::Walk;
	const EFVFacing Fc = bFishing ? EFVFacing::Down : Facing;
	const uint32 B = (uint32(Anim) << 8) | (uint32(Fc) << 4) | uint32(F);
	const uint64 Key = MakeKey(FVSpriteCat::Character, Look.GetHash(), B, uint8(Look.Hat));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildCharacter(Look, Anim, Fc, F), FString::Printf(TEXT("Char_%u_%d_%d_%d"), Look.GetHash(), int32(Anim), int32(Fc), F));
}

const FFVSprite& UFVSprites::HatIcon(EFVHatStyle Style, FColor Primary, FColor Secondary)
{
	const uint64 Key = MakeKey(FVSpriteCat::HatIcon, HashCombine(GetTypeHash(Primary), GetTypeHash(Secondary)), uint32(Style));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildHatIcon(Style, Primary, Secondary), FString::Printf(TEXT("HatIcon_%d"), int32(Style)));
}

const FFVSprite& UFVSprites::OutfitIcon(EFVOutfitStyle Style, FColor Primary, FColor Secondary)
{
	const uint64 Key = MakeKey(FVSpriteCat::OutfitIcon, HashCombine(GetTypeHash(Primary), GetTypeHash(Secondary)), uint32(Style));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildOutfitIcon(Style, Primary, Secondary), FString::Printf(TEXT("OutfitIcon_%d"), int32(Style)));
}

const FFVSprite& UFVSprites::Bobber(EFVBobberStyle Style, int32 Frame)
{
	const int32 F = ((Frame % 2) + 2) % 2;
	const uint64 Key = MakeKey(FVSpriteCat::Bobber, uint32(Style), uint32(F));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildBobber(Style, F), FString::Printf(TEXT("Bobber_%d_%d"), int32(Style), F));
}

const FFVSprite& UFVSprites::Pet(EFVPetStyle Style, EFVPetAnim Anim, int32 Frame)
{
	const int32 NumFrames = PetFrameCount(Anim);
	const int32 F = ((Frame % NumFrames) + NumFrames) % NumFrames;
	const uint64 Key = MakeKey(FVSpriteCat::Pet, uint32(Style), (uint32(Anim) << 4) | uint32(F));
	if (const FFVSprite* S = FindCached(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildPet(Style, Anim, F), FString::Printf(TEXT("Pet_%d_%d_%d"), int32(Style), int32(Anim), F));
}
