// UPDATE 6 [ART-ITEMS] — new item art, icons, baits, scrap junk, expert rod icons, axes & pickaxes (Docs/UPDATE6_FOREST_CITY.md §4, §5.1, §6.2, §6.3, §9а, §10.4). Owner: ART-ITEMS.
// Contract: Public/FVSprites.h «UPDATE 6» (+ the default branches of BuildItemArt / BuildBait / BuildJunk / BuildRodIcon in
// FVArtItems.cpp and BuildIcon in FVSprites.cpp, which ART-ITEMS owns and which forward here).
// Style: 16x16 like the update-2 item icons (ItemPalette of FVArtItems.cpp + a few extra colours, drawing in cells 1..14,
// FVArt::InkOutline added automatically). Levels are palette swaps (like the shovels). Pure CPU, deterministic.
// Cache: AxeIcon / PickaxeIcon under FVSpriteCat::ItemU6 (A = 1 axe / 2 pickaxe, B = level); the rest is cached by the
// update-2 getters (ItemArt, Bait, Junk, RodIcon, Icon).

#include "FVSprites.h"
#include "FVDatabase.h"

namespace FVArtItemsImpl
{
	const TMap<TCHAR, FColor>& ItemPalette();   // FVArtItems.cpp
}

namespace FVArtItemsU6
{
	FFVImage BuildItemArt(EFVItemArt Art, FColor Tint, uint8 Tier);
	FFVImage BuildBait(EFVBaitStyle Style);
	FFVImage BuildJunk(EFVJunkStyle Style);
	FFVImage BuildRodIcon(const FFVRodDef& Rod);
	FFVImage BuildIcon(EFVIcon I);
}

namespace FVArtItemsU6Impl
{
	FORCEINLINE FColor Lt(FColor C, float A) { return FVArt::Light(C, A); }
	FORCEINLINE FColor Dk(FColor C, float A) { return FVArt::Shade(C, A); }

	const TMap<TCHAR, FColor>& Palette()
	{
		static const TMap<TCHAR, FColor> Pal = []()
		{
			TMap<TCHAR, FColor> P = FVArtItemsImpl::ItemPalette();
			P.Add(TEXT('1'), FColor(244, 134, 40));    // carrot / pumpkin
			P.Add(TEXT('2'), FColor(196, 88, 30));
			P.Add(TEXT('3'), FColor(255, 186, 96));
			P.Add(TEXT('4'), FColor(170, 140, 230));   // hard-rock vein
			P.Add(TEXT('5'), FColor(206, 150, 88));    // onion skin
			P.Add(TEXT('6'), FColor(244, 220, 170));
			P.Add(TEXT('7'), FColor(46, 58, 140));     // deep blue
			P.Add(TEXT('8'), FColor(92, 112, 210));
			P.Add(TEXT('9'), FColor(90, 244, 220));    // glow teal
			P.Add(TEXT('0'), FColor(214, 250, 255));   // crystal
			P.Add(TEXT('!'), FColor(62, 50, 70));      // rock / dark iron
			P.Add(TEXT('?'), FColor(98, 90, 112));
			P.Add(TEXT('#'), FColor(142, 132, 156));
			return P;
		}();
		return Pal;
	}

	/** ItemAscii of FVArtItems.cpp with the extended palette: 16x16, ink outline. */
	FFVImage A(std::initializer_list<const TCHAR*> Rows, bool bOutline = true)
	{
		FFVImage Img = FFVImage::FromAscii(Rows, Palette());
		if (Img.W != 16 || Img.H != 16)
		{
			FFVImage Fixed(16, 16);
			Fixed.Blit(Img, (16 - Img.W) / 2, (16 - Img.H) / 2);
			Img = Fixed;
		}
		if (bOutline) { Img.Outline(FVArt::InkOutline()); }
		return Img;
	}

	FColor Pc(TCHAR Ch) { const FColor* C = Palette().Find(Ch); return C ? *C : FColor(255, 0, 255); }

	void Twinkle(FFVImage& Img, int32 X, int32 Y, FColor C)
	{
		Img.Blend(X, Y, C);
		const FColor Arm = FVArt::WithAlpha(C, 170);
		Img.Blend(X - 1, Y, Arm); Img.Blend(X + 1, Y, Arm); Img.Blend(X, Y - 1, Arm); Img.Blend(X, Y + 1, Arm);
	}

	void Disc(FFVImage& Img, float CX, float CY, float RX, float RY, FColor C)
	{
		for (int32 Y = FMath::FloorToInt(CY - RY) - 1; Y <= FMath::CeilToInt(CY + RY) + 1; ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX) - 1; X <= FMath::CeilToInt(CX + RX) + 1; ++X)
			{
				const float DX = (X + 0.5f - CX) / FMath::Max(RX, 0.1f), DY = (Y + 0.5f - CY) / FMath::Max(RY, 0.1f);
				if (DX * DX + DY * DY <= 1.f) { Img.Set(X, Y, C); }
			}
		}
	}

	/** Shaded ball lit from the upper-left. */
	void Ball(FFVImage& Img, float CX, float CY, float R, FColor Base)
	{
		for (int32 Y = FMath::FloorToInt(CY - R) - 1; Y <= FMath::CeilToInt(CY + R) + 1; ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - R) - 1; X <= FMath::CeilToInt(CX + R) + 1; ++X)
			{
				const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				if (D > R) { continue; }
				const float S = (DX + DY) / FMath::Max(R, 0.5f);
				Img.Set(X, Y, S < -0.55f ? Lt(Base, 0.35f) : (S > 0.45f || D > R - 0.9f ? Dk(Base, 0.35f) : Base));
			}
		}
		Img.Set(FMath::FloorToInt(CX - R * 0.45f), FMath::FloorToInt(CY - R * 0.45f), Lt(Base, 0.7f));
	}

	// ============================================================================================================
	// Tools: axe (4 levels) and pickaxe (2 levels)
	// ============================================================================================================
	FFVImage Axe(int32 Level)
	{
		const int32 L = FMath::Clamp(Level, 1, 4);
		// [level] blade light, blade, blade dark, edge, handle light, handle dark
		static const FColor Pal[4][6] = {
			{ FColor(230, 234, 244), FColor(176, 182, 198), FColor(112, 118, 138), FColor(255, 255, 255), FColor(222, 170, 108), FColor(160, 110, 62) },
			{ FColor(150, 150, 164), FColor(102, 102, 118), FColor(66, 64, 80), FColor(196, 198, 210), FColor(176, 112, 62), FColor(120, 72, 42) },
			{ FColor(210, 232, 255), FColor(130, 170, 220), FColor(70, 100, 160), FColor(236, 248, 255), FColor(150, 90, 50), FColor(100, 58, 34) },
			{ FColor(96, 92, 110), FColor(56, 54, 68), FColor(34, 32, 44), FColor(255, 204, 51), FColor(120, 70, 44), FColor(76, 44, 30) },
		};
		const FColor* P = Pal[L - 1];
		FFVImage Img(16, 16);
		// handle: diagonal from the bottom-left to the head
		for (int32 K = 0; K <= 10; ++K)
		{
			const int32 X = 2 + K, Y = 14 - K;
			Img.Set(X, Y, P[4]);
			Img.Set(X + 1, Y, P[5]);
		}
		Img.Set(2, 15, P[5]); Img.Set(1, 14, P[5]);
		// head: a wedge at the top-right, edge facing up-left
		struct FR { int32 Y, X0, X1; };
		static const FR Head[] = { { 1, 8, 10 }, { 2, 7, 11 }, { 3, 6, 12 }, { 4, 6, 13 }, { 5, 7, 13 }, { 6, 8, 12 }, { 7, 10, 11 } };
		for (const FR& R : Head)
		{
			for (int32 X = R.X0; X <= R.X1; ++X)
			{
				FColor C = P[1];
				if (X <= R.X0 + 1) { C = P[0]; }
				if (X >= R.X1 - 1) { C = P[2]; }
				Img.Set(X, R.Y, C);
			}
		}
		// the eye of the head where the handle passes
		Img.Set(11, 4, P[5]); Img.Set(12, 4, P[4]); Img.Set(11, 5, P[5]);
		Img.Outline(FVArt::InkOutline());
		// the bright cutting edge (after the outline)
		Img.Set(8, 1, P[3]); Img.Set(7, 2, P[3]); Img.Set(6, 3, P[3]); Img.Set(6, 4, P[3]); Img.Set(7, 5, P[3]);
		if (L == 3) { Twinkle(Img, 7, 3, FColor(255, 255, 255)); }
		if (L == 4) { Twinkle(Img, 12, 2, FColor(255, 236, 150)); Img.Set(8, 6, P[3]); }
		return Img;
	}

	FFVImage Pickaxe(int32 Level)
	{
		const int32 L = FMath::Clamp(Level, 1, 2);
		const FColor HeadL = L == 1 ? FColor(220, 226, 238) : FColor(150, 110, 220);
		const FColor HeadC = L == 1 ? FColor(160, 166, 184) : FColor(96, 60, 150);
		const FColor HeadD = L == 1 ? FColor(100, 104, 124) : FColor(58, 34, 96);
		FFVImage Img(16, 16);
		// handle (bottom-left to the top-right)
		for (int32 K = 0; K <= 10; ++K)
		{
			const int32 X = 2 + K, Y = 14 - K;
			Img.Set(X, Y, Pc('M')); Img.Set(X + 1, Y, Pc('m'));
		}
		Img.Set(2, 15, Pc('e'));
		// the curved head across the top of the handle: from the point at the top-left to the point at the bottom-right
		static const FIntPoint Arc[] = { { 4, 2 }, { 5, 1 }, { 6, 1 }, { 7, 1 }, { 8, 1 }, { 9, 1 }, { 10, 2 }, { 11, 2 }, { 12, 3 }, { 13, 4 }, { 13, 5 }, { 14, 6 }, { 14, 7 }, { 14, 8 } };
		for (const FIntPoint& Pt : Arc)
		{
			Img.Set(Pt.X, Pt.Y, HeadC);
			Img.Set(Pt.X, Pt.Y + 1, (Pt.X > 11) ? HeadD : HeadC);
			Img.Set(Pt.X - (Pt.X > 11 ? 1 : 0), Pt.Y - (Pt.X > 11 ? 0 : 1), HeadL);
		}
		Img.Set(3, 3, HeadC); Img.Set(15, 9, HeadD);
		Img.Set(11, 3, Pc('e')); Img.Set(12, 4, Pc('m'));
		Img.Outline(FVArt::InkOutline());
		if (L == 2)
		{
			// the crystal facet on the tempered point
			Img.Set(3, 3, Pc('0')); Img.Set(4, 2, Pc('l')); Img.Set(4, 3, Pc('l'));
			Twinkle(Img, 13, 11, FColor(230, 200, 255));
		}
		return Img;
	}

	// ============================================================================================================
	// Item art
	// ============================================================================================================
	FFVImage Log()
	{
		FFVImage Img(16, 16);
		for (int32 Y = 5; Y <= 11; ++Y)
		{
			for (int32 X = 1; X <= 11; ++X)
			{
				FColor C = Y <= 6 ? Pc('M') : (Y >= 10 ? Pc('e') : Pc('m'));
				if ((X * 7 + Y * 3) % 11 == 0 && Y > 5 && Y < 10) { C = Pc('e'); }
				Img.Set(X, Y, C);
			}
		}
		Img.Set(4, 7, Pc('E')); Img.Set(5, 8, Pc('e'));   // a knot
		Disc(Img, 12.f, 8.5f, 3.f, 3.6f, Pc('m'));
		Disc(Img, 12.2f, 8.5f, 2.3f, 2.9f, Pc('K'));
		Disc(Img, 12.2f, 8.5f, 1.5f, 2.0f, Pc('k'));
		Disc(Img, 12.2f, 8.5f, 0.7f, 1.0f, Pc('j'));
		Img.Set(11, 6, Pc('K'));
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	/** A() with the crop tint as extra colours ('*' light, '%' base, '&' dark, '@' deep): 16x16 rows, ink outline. */
	FFVImage AT(std::initializer_list<const TCHAR*> Rows, FColor Tint)
	{
		TMap<TCHAR, FColor> P = Palette();
		P.Add(TEXT('*'), Lt(Tint, 0.4f));
		P.Add(TEXT('%'), Tint);
		P.Add(TEXT('&'), Dk(Tint, 0.3f));
		P.Add(TEXT('@'), Dk(Tint, 0.55f));
		FFVImage Img = FFVImage::FromAscii(Rows, P);
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	/** The generic seed packet (paper tinted by the crop, a sprout in the window) for seeds without their own art. */
	FFVImage SeedPacketGeneric(FColor Tint)
	{
		FFVImage Img(16, 16);
		const FColor Paper = Lt(Tint, 0.62f), PaperDk = Lt(Tint, 0.35f), Band = Tint, BandDk = Dk(Tint, 0.3f);
		for (int32 Y = 2; Y <= 13; ++Y)
		{
			for (int32 X = 3; X <= 12; ++X) { Img.Set(X, Y, X >= 11 ? PaperDk : Paper); }
		}
		for (int32 X = 3; X <= 12; ++X) { Img.Set(X, 2, BandDk); Img.Set(X, 3, Band); Img.Set(X, 12, Band); Img.Set(X, 13, (X & 1) ? BandDk : Band); }
		// the picture window
		const FColor Sky(196, 232, 240), Soil(120, 82, 52), SoilLt(150, 106, 68);
		for (int32 Y = 5; Y <= 10; ++Y) { for (int32 X = 4; X <= 11; ++X) { Img.Set(X, Y, Y >= 10 ? ((X & 1) ? Soil : SoilLt) : Sky); } }
		Ball(Img, 7.8f, 7.6f, 1.9f, Tint);
		Img.Set(7, 5, FColor(86, 160, 60)); Img.Set(8, 5, FColor(52, 112, 44));
		// a fold line and the seeds shaken to the bottom
		Img.Set(5, 11, Pc('e')); Img.Set(8, 11, Pc('e')); Img.Set(10, 11, Pc('e'));
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	/** Р67: every seed kind is its own 16x16 object (silhouettes differ at a glance), the crop Tint is the accent.
	    Variant = FFVItemDef::Tier: 0 carrot, 1 potato, 2 onion, 3 dill, 4 pumpkin, 5 rye; anything else -> the packet. */
	FFVImage SeedPacket(FColor Tint, uint8 Variant)
	{
		switch (Variant)
		{
		case 0:   // морковь: a canvas pouch with carrot tops in its neck, a tinted tie, a carrot stamp, spilled seeds
			return AT({
				TEXT("................"),
				TEXT(".....G..G..G...."),
				TEXT("....GgG.g.GgG..."),
				TEXT(".....gh.h.hg...."),
				TEXT(".....WKWKKk....."),
				TEXT("......&%%&......"),
				TEXT("....WKKKKKKk...."),
				TEXT("...WKKKKgKKKk..."),
				TEXT("...WKKK*%&KKk..."),
				TEXT("...WKKK*%&KKk..."),
				TEXT("...WKKKK%&KKk..."),
				TEXT("...KKKKK&KKkk..."),
				TEXT("....kKKKKKKk...."),
				TEXT(".....jjjjjj.C..."),
				TEXT("...........LCL.."),
				TEXT("................"),
			}, Tint);
		case 1:   // семенной картофель: a tinted tuber with dark eyes and pale sprouts
			return AT({
				TEXT("................"),
				TEXT("................"),
				TEXT("........G......."),
				TEXT("...G....W......."),
				TEXT("...W...W....G..."),
				TEXT("....W..W....W..."),
				TEXT("....U..U...U...."),
				TEXT("....*%%U%%%U...."),
				TEXT("...*%*%%%%%%&..."),
				TEXT("..*%%%@%%%%@&&.."),
				TEXT("..%%U%%%%%%%&&.."),
				TEXT("..%%@%%%%@%&&&.."),
				TEXT("...&%%%%%&&&&..."),
				TEXT(".....&&&&&&....."),
				TEXT("................"),
				TEXT("................"),
			}, Tint);
		case 2:   // лук-севок: little tinted bulbs in a red net bag tied at the top
			return AT({
				TEXT("................"),
				TEXT("......a...a....."),
				TEXT(".......a.a......"),
				TEXT("........a......."),
				TEXT("........z......."),
				TEXT(".....**a.a*....."),
				TEXT("....a*z%*a%z...."),
				TEXT("....*%%za%%&...."),
				TEXT("....%%&az%&&...."),
				TEXT("...*z&a.@z&a*..."),
				TEXT("..*%%a**%%a*%%.."),
				TEXT("..*%a&z%%a*z%&.."),
				TEXT("..za&&%za&%%za.."),
				TEXT("...z&.%az&.&a..."),
				TEXT("................"),
				TEXT("................"),
			}, Tint);
		case 3:   // укроп: a dry umbel of seed clusters on a stalk with a tinted ribbon
			return AT({
				TEXT("................"),
				TEXT("......CLC......."),
				TEXT("..CLC.LJL.CLC..."),
				TEXT("..LJL..c..LJL..."),
				TEXT(".LC.c..c..c.CL.."),
				TEXT(".JL..c.c.c..LJ.."),
				TEXT("..c..c.c.c..c..."),
				TEXT("...c..ccc..c...."),
				TEXT("....cc.c.cc....."),
				TEXT("......cHc......."),
				TEXT(".......H........"),
				TEXT("......&%&......."),
				TEXT(".....%.H.%......"),
				TEXT(".......H........"),
				TEXT("...L...H...L...."),
				TEXT("................"),
			}, Tint);
		case 4:   // тыква: a handful of flat cream seeds with bits of tinted pulp
			return AT({
				TEXT("................"),
				TEXT("....F..........."),
				TEXT("...FFF....FF...."),
				TEXT("...wFF....wFF..."),
				TEXT("..FFFFC..FFFF..."),
				TEXT("..F%FfC..FFFfC.."),
				TEXT("..FFFfC..FFFfC.."),
				TEXT("...CfCC..FFffC.."),
				TEXT("....C.....CCC..."),
				TEXT("......FFFFF....."),
				TEXT("....FwFFFFFC...."),
				TEXT("...FFF%FFFfC...."),
				TEXT("....FFF&fffC...."),
				TEXT("......CCCCCC...."),
				TEXT("................"),
				TEXT("................"),
			}, Tint);
		case 5:   // рожь: three golden ears over a heap of tinted grains
			return AT({
				TEXT("................"),
				TEXT(".......y........"),
				TEXT(".Y....Yd........"),
				TEXT("..y...dy....Y..."),
				TEXT("..dy..yd...yd..."),
				TEXT("...yd.dy..dy...."),
				TEXT("...dy.yd..yd...."),
				TEXT("....C..C.dy....."),
				TEXT(".....C.C.C......"),
				TEXT("....%&*%&*%....."),
				TEXT("...%&&%&&%&&...."),
				TEXT("..%&*%&*%&*%&..."),
				TEXT("..&&%&&%&&%&&..."),
				TEXT("..*%@*%@*%@*%..."),
				TEXT("...@@%@@%@@%...."),
				TEXT("................"),
			}, Tint);
		default:
			return SeedPacketGeneric(Tint);
		}
	}

	FFVImage ExpertLine(FColor Tint)
	{
		const bool bHeavy = (Tint.R + Tint.G + Tint.B) < 330;
		const FColor Thread = bHeavy ? FColor(40, 70, 50) : FColor(220, 230, 240);
		FFVImage Img(16, 16);
		// spool flanges
		for (int32 Y = 3; Y <= 12; ++Y) { Img.Set(3, Y, bHeavy ? Pc('X') : Pc('l')); Img.Set(12, Y, bHeavy ? Pc('Q') : Pc('B')); }
		for (int32 Y = 4; Y <= 11; ++Y) { Img.Set(2, Y, bHeavy ? Pc('x') : Pc('0')); Img.Set(13, Y, bHeavy ? Pc('X') : Pc('b')); }
		// wound thread
		for (int32 Y = 4; Y <= 11; ++Y)
		{
			for (int32 X = 4; X <= 11; ++X)
			{
				FColor C = Thread;
				if (bHeavy) { C = ((X + Y) % 2) ? Lt(Thread, 0.2f) : Dk(Thread, 0.1f); }
				else if (Y % 2) { C = Dk(Thread, 0.12f); }
				if (X == 4) { C = Lt(C, 0.2f); }
				if (X == 11) { C = Dk(C, 0.2f); }
				Img.Set(X, Y, C);
			}
		}
		Img.Outline(FVArt::InkOutline());
		// the loose end
		Img.Set(13, 13, Thread); Img.Set(14, 14, Thread); Img.Set(12, 12, Thread);
		if (!bHeavy) { Twinkle(Img, 6, 5, FColor(255, 255, 255)); }
		return Img;
	}

	FFVImage Reel(bool bHeavy, bool bTempered)
	{
		const FColor Body = bHeavy ? FColor(52, 50, 62) : FColor(206, 212, 226);
		const FColor Acc = bHeavy ? FColor(255, 204, 51) : FColor(70, 130, 220);
		FFVImage Img(16, 16);
		// foot and stem
		for (int32 X = 3; X <= 12; ++X) { Img.Set(X, 13, Dk(Body, 0.2f)); }
		Img.Set(7, 12, Dk(Body, 0.1f)); Img.Set(8, 12, Dk(Body, 0.2f)); Img.Set(7, 11, Body); Img.Set(8, 11, Dk(Body, 0.2f));
		// drum
		Ball(Img, 7.5f, 6.5f, 4.6f, Body);
		Disc(Img, 7.5f, 6.5f, 2.4f, 2.4f, Acc);
		Disc(Img, 7.5f, 6.5f, 1.f, 1.f, bTempered ? FColor(170, 90, 236) : Dk(Acc, 0.3f));
		// handle and knob
		Img.Set(12, 4, Acc); Img.Set(13, 3, Acc); Img.Set(14, 3, bTempered ? FColor(226, 168, 255) : Lt(Acc, 0.3f));
		Img.Outline(FVArt::InkOutline());
		Img.Set(5, 4, Lt(Body, 0.6f));
		if (bTempered) { Img.Set(4, 8, FColor(170, 90, 236)); Img.Set(10, 9, FColor(170, 90, 236)); Twinkle(Img, 12, 10, FColor(230, 200, 255)); }
		return Img;
	}

	FFVImage ItemArt(EFVItemArt Art, FColor Tint, uint8 Tier)
	{
		switch (Art)
		{
		case EFVItemArt::Log:
			return Log();
		case EFVItemArt::HardRock:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT(".......#??......"),
				TEXT(".....##??4?!...."),
				TEXT("....#\?\?#4\?\?!!..."),
				TEXT("...#??4??#?!!..."),
				TEXT("...#\?4\?\?\?4\?\?!!.."),
				TEXT("..#\?\?4\?\?4\?\?\?\?!.."),
				TEXT("..#\?\?\?\?44\?\?!\?!.."),
				TEXT("..\?\?4\?\?\?\?\?!!!!.."),
				TEXT("...\?4\?\?\?!!!!!..."),
				TEXT("....!!!!!!!!...."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::Mpk:   // metal of poor quality: a grey ingot with pits and rust spots
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("......SSSSSSs..."),
				TEXT("....SSsSSsSsxx.."),
				TEXT("...SsXsRsssxxX.."),
				TEXT("..ssssssssxxXX.."),
				TEXT("..sssXsrsssxXX.."),
				TEXT("..sRsssssXsxX..."),
				TEXT("..xxxxxxxxxX...."),
				TEXT("..XXXXXXXXX....."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::Nails:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("..ss....ss......"),
				TEXT("..SsXx..SsX....."),
				TEXT("...sx....sx....."),
				TEXT("...sx....sx.ss.."),
				TEXT("...sx....sx.SsXx"),
				TEXT("...sx....sx..sx."),
				TEXT("...sx....sx..sx."),
				TEXT("...sx....sx..sx."),
				TEXT("...sx....sx..sx."),
				TEXT("...sx....x...sx."),
				TEXT("...x.........sx."),
				TEXT("..............x."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::BoatFrame:   // the keel with ribs
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("..M...M...M...M."),
				TEXT("..m...m...m...m."),
				TEXT("..mM..mM..mM..m."),
				TEXT("...m...m...m.mm."),
				TEXT("...mM..mM..mMm.."),
				TEXT("....m...m..mm..."),
				TEXT("....mM..mM.m...."),
				TEXT(".MMMMMMMMMMMMe.."),
				TEXT("..mmmmmmmmmme..."),
				TEXT("...eeeeeeeee...."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::SeedPacket:
			return SeedPacket(Tint, Tier);
		case EFVItemArt::Carrot:
			return A({
				TEXT("................"),
				TEXT("..........G..G.."),
				TEXT("........G.gGgG.."),
				TEXT(".........ghggG.."),
				TEXT(".........3hg...."),
				TEXT("........3111...."),
				TEXT(".......31112...."),
				TEXT("......31112....."),
				TEXT(".....3112......."),
				TEXT(".....1122......."),
				TEXT("....1122........"),
				TEXT("....122........."),
				TEXT("...122.........."),
				TEXT("...22..........."),
				TEXT("..2............."),
				TEXT("................"),
			});
		case EFVItemArt::Potato:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT(".......ffFf....."),
				TEXT(".....fFFfffH...."),
				TEXT("....fFFfHffHH..."),
				TEXT("...fFfffffffHL.."),
				TEXT("...fffHfffHfHL.."),
				TEXT("...HffffffffHL.."),
				TEXT("....HHfffHHHL..."),
				TEXT(".....LHHHHLL...."),
				TEXT("......LLLL......"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::Onion:
			return A({
				TEXT("................"),
				TEXT(".......g........"),
				TEXT(".......gG......."),
				TEXT("........g......."),
				TEXT(".......65......."),
				TEXT("......6555......"),
				TEXT(".....655655....."),
				TEXT("....6556555C...."),
				TEXT("....6556555C...."),
				TEXT("....655655CC...."),
				TEXT(".....65555C....."),
				TEXT("......5CCC......"),
				TEXT(".......WW......."),
				TEXT("......W..W......"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::Dill:
			return A({
				TEXT("................"),
				TEXT("..G.G.G...G.G..."),
				TEXT("...GGG.....GG..."),
				TEXT("..G.g.G...G.g.G."),
				TEXT("....g.....g.g..."),
				TEXT(".....g...g......"),
				TEXT(".G...g..g....G.."),
				TEXT("..G...g.g...G..."),
				TEXT("...G..gg...G...."),
				TEXT("....g.gh..g....."),
				TEXT(".....ggh.g......"),
				TEXT("......hgg......."),
				TEXT("......hh........"),
				TEXT("......h........."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::Pumpkin:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("........h......."),
				TEXT(".......hg.gG...."),
				TEXT(".....31h11G....."),
				TEXT("...3311211111..."),
				TEXT("..331121121112.."),
				TEXT("..311211211122.."),
				TEXT(".3311211211122.."),
				TEXT(".3112112111122.."),
				TEXT(".1112112111222.."),
				TEXT("..11211211222..."),
				TEXT("...222222222...."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::Draniki:   // a stack of potato pancakes with sour cream
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("......WWW......."),
				TEXT(".....WwWWW......"),
				TEXT("...fFFWWWFfH...."),
				TEXT("..fFfHfffHffH..."),
				TEXT("..HHLHHHLHHHL..."),
				TEXT("..fFfffHfffHH..."),
				TEXT("..HHLHHLHHHLL..."),
				TEXT(".kKfffHfffHfHkk."),
				TEXT(".kKHHLHHLHHLLkj."),
				TEXT("..kkkkkkkkkkkj.."),
				TEXT("...jjjjjjjjjj..."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::FishDill:  // a fish on a plate with dill sprigs
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("....WWWWWWWW...."),
				TEXT("..WWWWWWWWWWWW.."),
				TEXT(".WWsSSSSSsWG.GW."),
				TEXT(".WsSSwSSSSsxGWW."),
				TEXT(".WQSSSSSSSxxxGW."),
				TEXT(".WWsxxxxxsxGW.W."),
				TEXT("..WWWGWGWWWWWW.."),
				TEXT("...kWWWWWWWWk..."),
				TEXT(".....kkkkkk....."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::OnionSoup:  // brown soup with onion rings and a crouton
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("...kKKKKKKKKk..."),
				TEXT("..kLL6LLLL6LLk.."),
				TEXT("..KL6L6LFFL6Lk.."),
				TEXT("..kLL6LLFfLLLk.."),
				TEXT("..jkLLLLLLLLkj.."),
				TEXT("...jkkKKKKkkj..."),
				TEXT("....jjkkkkjj...."),
				TEXT(".....jjjjjj....."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::PumpkinPorridge:  // a clay pot of pumpkin porridge with berries
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("....3311113....."),
				TEXT("...31a1111A1...."),
				TEXT("..RRRRRRRRRRR..."),
				TEXT("..rRRRRRRRRrr..."),
				TEXT("...rRRrrrRrq...."),
				TEXT("...rRrrrrrrq...."),
				TEXT("...rrrrrrrqq...."),
				TEXT("....rrrrrqq....."),
				TEXT(".....qqqqq......"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::FishPie:  // рыбник: a golden pie with a fish-shaped slit
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT(".....fFFFFf....."),
				TEXT("...fFFFFFFFFf..."),
				TEXT("..fFFHLHHHLHFf.."),
				TEXT("..fFHHLLLLLHHf.."),
				TEXT("..fFFHLHHHLHff.."),
				TEXT("..HfffffffffHH.."),
				TEXT("...HHHfHfHHHH..."),
				TEXT("..kKkKkKkKkKkj.."),
				TEXT("...jjjjjjjjjj..."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		// ---- update 6 (§18 «Старая мельница», MILL-ITEMS)
		case EFVItemArt::Rye:   // рожь: a sheaf of golden ears tied with twine
			return A({
				TEXT("................"),
				TEXT(".....Y....Y....."),
				TEXT("..Y.yY...yYy.Y.."),
				TEXT("..yYdY.Y.dYd.yY."),
				TEXT("..dYdy.yY.yd.dy."),
				TEXT("...yd.yYdY.d.yd."),
				TEXT("...dy.dYdy..dy.."),
				TEXT("....d.yd.d..d..."),
				TEXT(".....CdCdCCd...."),
				TEXT("......CHCHC....."),
				TEXT(".....eEeEeEe...."),
				TEXT("......CHCHC....."),
				TEXT(".....CH.C.HC...."),
				TEXT("....CH..C..HC..."),
				TEXT("...H....C....H.."),
				TEXT("................"),
			});
		case EFVItemArt::Flour:   // ржаная мука: a burlap sack with a rye-ear stamp, twine on top, a little flour heap
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT(".....eE.Ee......"),
				TEXT("......eEe......."),
				TEXT(".....KwewK......"),
				TEXT("......jKj......."),
				TEXT(".....KKKKKk....."),
				TEXT("....KKKKKKKk...."),
				TEXT("...KKKKdKKKKk..."),
				TEXT("...KKKdKdKKkk..."),
				TEXT("...KKKKdKKKkk..."),
				TEXT("...KKKKdKKKkk..."),
				TEXT("...kKKKKKKkkj..."),
				TEXT("....kkkkkkkj.ww."),
				TEXT(".....jjjjjj.wWww"),
				TEXT("................"),
			});
		case EFVItemArt::Bliny:   // блины: a stack of lacy rye pancakes with a pat of butter on a plate
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("......YYy......."),
				TEXT("....cCYydCc....."),
				TEXT("..cCCdCCCdCCc..."),
				TEXT(".CCdCCCCdCCCdC.."),
				TEXT(".dCCCCdCCCCCCd.."),
				TEXT(".cCCCCCCCCdCCc.."),
				TEXT(".HHHHHHHHHHHHHH."),
				TEXT(".cCCCCCCCCCCCCc."),
				TEXT(".HHHHHHHHHHHHH.."),
				TEXT("kKcCCCCCCCCCCcKk"),
				TEXT(".kkkkkkkkkkkkkj."),
				TEXT("..jjjjjjjjjjjj.."),
			});
		case EFVItemArt::RyeFishPie:   // рыбный пирог: a dark rye crust with a fish-shaped slit, crimped rim, an ear on top
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("......d.d......."),
				TEXT(".....JLdLLJ....."),
				TEXT("...JLLLLLLLLJ..."),
				TEXT("..JLLLWWWLLWLJ.."),
				TEXT("..JLLWWqWWWWLJ.."),
				TEXT("..JLLLWWWLLWLJ.."),
				TEXT("..DJLLLLLLLLJD.."),
				TEXT("..HDHDHDHDHDHD.."),
				TEXT("..DJJJJJJJJJJD.."),
				TEXT(".kKkKkKkKkKkKkj."),
				TEXT("..jjjjjjjjjjjj.."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::ExpertLine:
			return ExpertLine(Tint);
		case EFVItemArt::FloatSurface:   // a wide white-and-orange disc
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT(".......x........"),
				TEXT(".......x........"),
				TEXT(".....OOOOO......"),
				TEXT("...OOOwOOOOO...."),
				TEXT("..O3OOOOOOOO2..."),
				TEXT("..WWWWWWWWWWW..."),
				TEXT("..WwWWWWWWWWs..."),
				TEXT("...sWWWWWWss...."),
				TEXT(".....sssss......"),
				TEXT(".......x........"),
				TEXT(".......x........"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::FloatDeep:      // long, dark blue with a red tip
			return A({
				TEXT("................"),
				TEXT(".......a........"),
				TEXT("......aAa......."),
				TEXT("......aaz......."),
				TEXT("......Wwx......."),
				TEXT("......878......."),
				TEXT("......877......."),
				TEXT("......877......."),
				TEXT("......877......."),
				TEXT("......877......."),
				TEXT("......877......."),
				TEXT(".......7........"),
				TEXT(".......x........"),
				TEXT(".......x........"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVItemArt::FloatGlowExpert:  // dark with a turquoise glowing tip
		{
			FFVImage Img = A({
				TEXT("................"),
				TEXT(".......9........"),
				TEXT("......999......."),
				TEXT("......9l9......."),
				TEXT("......QQQ......."),
				TEXT("......Q?Q......."),
				TEXT("......Q?!......."),
				TEXT("......Q?!......."),
				TEXT("......Q?!......."),
				TEXT("......!!!......."),
				TEXT(".......!........"),
				TEXT(".......x........"),
				TEXT(".......x........"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
			Img.Blend(5, 1, FColor(90, 244, 220, 110)); Img.Blend(9, 1, FColor(90, 244, 220, 110)); Img.Blend(7, 0, FColor(90, 244, 220, 140));
			return Img;
		}
		case EFVItemArt::ReelLight:
			return Reel(false, Tier >= 2);
		case EFVItemArt::ReelHeavy:
			return Reel(true, Tier >= 2);
		case EFVItemArt::Blueprint:   // a blue roll with a white pickaxe
		{
			FFVImage Img = A({
				TEXT("................"),
				TEXT("................"),
				TEXT("..BB............"),
				TEXT(".BbbbbbbbbbbbB.."),
				TEXT(".Bb8b8888b8b8B.."),
				TEXT(".Bb88888888888.."),
				TEXT(".Bb88888888888.."),
				TEXT(".Bb88888888888.."),
				TEXT(".Bb88888888888.."),
				TEXT(".Bb88888888888.."),
				TEXT(".Bb88888888888.."),
				TEXT(".Bb8b8888b8b8B.."),
				TEXT(".BbbbbbbbbbbbB.."),
				TEXT("..BB............"),
				TEXT("................"),
				TEXT("................"),
			});
			// white chalk pickaxe
			const FColor W(240, 246, 255);
			for (int32 K = 0; K < 5; ++K) { Img.Set(5 + K, 10 - K, W); }
			Img.Set(7, 5, W); Img.Set(8, 5, W); Img.Set(9, 5, W); Img.Set(10, 5, W); Img.Set(11, 6, W); Img.Set(6, 6, W);
			return Img;
		}
		default:
			break;
		}
		return FFVImage();
	}

	// ============================================================================================================
	// Baits and junk
	// ============================================================================================================
	FFVImage Bait(EFVBaitStyle Style)
	{
		switch (Style)
		{
		case EFVBaitStyle::Trash:   // a tin can with bolts
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT(".....sSSSs......"),
				TEXT("....sSSSSSx....."),
				TEXT("....xsssssX....."),
				TEXT("....SsrRsxX....."),
				TEXT("....SsRRsxX..x.."),
				TEXT("....SsrrsxX.xsx."),
				TEXT("....SsssSxX..X.."),
				TEXT("....SssssxX....."),
				TEXT("....xxxxxXX.xX.."),
				TEXT(".xX...XXX..xsxX."),
				TEXT("xsX.........X..."),
				TEXT(".X.............."),
				TEXT("................"),
			});
		// ---- update 6 (§18 «Старая мельница», MILL-ITEMS)
		case EFVBaitStyle::BreadGroundbait:   // хлебная прикормка: a crumbly heap of rye groundbait with crumbs around
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("......LHHL......"),
				TEXT("....LHCcCHHL...."),
				TEXT("...LHCcCCcCHL..."),
				TEXT("...HCcCdCCcCH..."),
				TEXT("..LHCCcCCdCcHL.."),
				TEXT("..HCdCCcCCCcCH.."),
				TEXT("..LHCCCCcCdCHL.."),
				TEXT("...JLHHHHHHLJ..."),
				TEXT(".C..JJJJJJJJ..c."),
				TEXT("c.H..........H.C"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVBaitStyle::Kolobok:   // мельничный колобок: a round rye ball with berries and a cheeky face
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT(".....LLLLL......"),
				TEXT("...LHHCHHHHL...."),
				TEXT("..LHCCHHHaHHL..."),
				TEXT("..HCHDHHHDHHL..."),
				TEXT(".LHHHHHaHHHHHL.."),
				TEXT(".LHaHHHHHHHnHL.."),
				TEXT(".LHHHDHHHDHHHL.."),
				TEXT(".JLHHHDDDHHHLJ.."),
				TEXT("..JLHHHHHaHLJ..."),
				TEXT("...JJLLLLLJJ...."),
				TEXT(".....JJJJJ......"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVBaitStyle::ReedBundle:   // пучок тростника: young green reeds tied with twine
			return A({
				TEXT("................"),
				TEXT("..G....G...G...."),
				TEXT("..gG..Gg..Gg..G."),
				TEXT("...g.Gg..gG..Gg."),
				TEXT("...hg.g.Gg..gh.."),
				TEXT("....gGh.gh.gG..."),
				TEXT(".....ggGghgGh..."),
				TEXT("......gGghGg...."),
				TEXT("......eEeEeE...."),
				TEXT("......gGhgGh...."),
				TEXT(".....gGh.gGhg..."),
				TEXT("....gGh..gG.hg.."),
				TEXT("...gGh...gG..hg."),
				TEXT("...hh....hh...h."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVBaitStyle::DeepWorm:   // a fat blue-violet worm with glowing dots
		{
			FFVImage Img = A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("..........888..."),
				TEXT(".........88878.."),
				TEXT("........8877.7.."),
				TEXT(".......887......"),
				TEXT("......887......."),
				TEXT("..88.887........"),
				TEXT(".8878877........"),
				TEXT(".877887........."),
				TEXT("..777..........."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
			Img.Set(11, 4, Pc('9')); Img.Set(8, 6, Pc('9')); Img.Set(5, 8, Pc('9')); Img.Set(2, 9, Pc('9')); Img.Set(12, 3, Pc('l'));
			return Img;
		}
		case EFVBaitStyle::LiveBait:   // a small silver fish on the hook
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("..........x....."),
				TEXT("..........x....."),
				TEXT("..........x....."),
				TEXT(".....sSSS.x....."),
				TEXT("...sSSwSSSs....."),
				TEXT("..sSQSSSSSsxs..."),
				TEXT("..sSSSSSSsx.xs.."),
				TEXT("...xxxxxxx...s.."),
				TEXT(".....xx.....s..."),
				TEXT("..........ss...."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVBaitStyle::LegendMarlin:   // a silver jig with golden feathers
		{
			FFVImage Img = A({
				TEXT("................"),
				TEXT("................"),
				TEXT("...Y............"),
				TEXT("...yY..........."),
				TEXT("..Yyyd.........."),
				TEXT("...yydSS........"),
				TEXT("....dSSSSs......"),
				TEXT(".....SSwSSs....."),
				TEXT("......SSSSsx...."),
				TEXT(".......sSSxx...."),
				TEXT("........xxxQ.x.."),
				TEXT("...........xx..."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
			Twinkle(Img, 7, 6, FColor(255, 255, 255));
			return Img;
		}
		case EFVBaitStyle::LegendHarbor:   // a piece of crab with a shell on the hook
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT(".....a...a......"),
				TEXT("....aA...Aa....."),
				TEXT("....aaa.aaa....."),
				TEXT(".....aAAAa......"),
				TEXT("....aAAwAAa....."),
				TEXT("....aAAAAAa.x..."),
				TEXT(".....zaaaz..x..."),
				TEXT("..cCc..z....x..."),
				TEXT(".cCcCc.....xx..."),
				TEXT(".CcCcC...x.x...."),
				TEXT("..CCC.....x....."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVBaitStyle::LegendGiant:   // a clay ball with a crystal
		{
			FFVImage Img(16, 16);
			Ball(Img, 7.5f, 9.f, 5.f, FColor(176, 110, 74));
			Img.Set(5, 10, FColor(140, 84, 56)); Img.Set(9, 12, FColor(140, 84, 56)); Img.Set(4, 8, FColor(140, 84, 56));
			const FColor C0 = Pc('0'), C1 = Pc('l'), C2 = Pc('4');
			Img.Set(8, 3, C0); Img.Set(8, 4, C1); Img.Set(9, 4, C2); Img.Set(7, 5, C0); Img.Set(8, 5, C1); Img.Set(9, 5, C2); Img.Set(10, 5, C2);
			Img.Set(8, 2, C0);
			Img.Outline(FVArt::InkOutline());
			Twinkle(Img, 12, 3, FColor(230, 250, 255));
			return Img;
		}
		case EFVBaitStyle::LegendDragon:   // a glass prism bead
		{
			FFVImage Img = A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT(".......0........"),
				TEXT("......0ll......."),
				TEXT(".....0llBB......"),
				TEXT("....0lPlBBb....."),
				TEXT("...0llPPBBbb...."),
				TEXT("...lllpPBBbb...."),
				TEXT("....llppBbb....."),
				TEXT(".....lpBbb......"),
				TEXT("......Bbb......."),
				TEXT(".......b........"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
			Twinkle(Img, 6, 6, FColor(255, 255, 255));
			Img.Blend(12, 8, FColor(255, 120, 120, 150)); Img.Blend(13, 9, FColor(255, 220, 90, 150)); Img.Blend(13, 10, FColor(120, 230, 140, 150));
			return Img;
		}
		default:
			break;
		}
		return FFVImage();
	}

	FFVImage Junk(EFVJunkStyle Style)
	{
		if (Style != EFVJunkStyle::Scrap) { return FFVImage(); }
		return A({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT(".........rR....."),
			TEXT("......xsrRrq...."),
			TEXT("....xsSsxrRq...."),
			TEXT("...xsSxsxxrqq..."),
			TEXT("..xsSsxRrxxXq..."),
			TEXT("..xsxxrRqxXX...."),
			TEXT("...xXrrqXXX.X..."),
			TEXT("....XXqXX..xsX.."),
			TEXT("....qq.....XsxX."),
			TEXT("...........XXX.."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	}

	// ============================================================================================================
	// Expert rods: the classic rod icon + the rod type's sign (two tips, a compass-rose reel, a magnet...)
	// ============================================================================================================
	FFVImage ExpertRod(const FFVRodDef& Rod)
	{
		const FColor RodC = Rod.RodColor, RodL = Lt(RodC, 0.35f), RodD = Dk(RodC, 0.3f);
		const FColor Acc = Rod.AccentColor, AccL = Lt(Acc, 0.3f), AccD = Dk(Acc, 0.3f);
		FFVImage Img(16, 16);
		const bool bTwin = Rod.ExpertType == EFVExpertRodType::Twin;
		for (int32 K = 0; K <= 8; ++K)
		{
			const int32 X = 5 + K, Y = 10 - K;
			Img.Set(X, Y, RodL);
			if (K < 7)
			{
				Img.Set(X, Y + 1, K % 3 == 2 ? Acc : RodC);   // wraps in the accent colour
				Img.Set(X + 1, Y + 1, RodD);
			}
		}
		if (bTwin)
		{
			// the second tip splits off
			for (int32 K = 5; K <= 8; ++K) { Img.Set(5 + K + 1, 10 - K + 2, RodL); }
			Img.Set(14, 4, RodL);
		}
		// grip
		for (int32 K = 0; K <= 3; ++K)
		{
			const int32 X = 1 + K, Y = 14 - K;
			Img.Set(X, Y, K % 2 == 0 ? AccL : Acc);
			Img.Set(X + 1, Y, AccD);
		}
		Img.Set(1, 15, AccD);
		// reel by type
		switch (Rod.ExpertType)
		{
		case EFVExpertRodType::Compass:   // a compass-rose reel
			Disc(Img, 6.f, 12.5f, 2.2f, 2.2f, Pc('W'));
			Img.Set(5, 11, Acc); Img.Set(5, 13, AccD); Img.Set(4, 12, Acc); Img.Set(6, 12, Pc('a'));
			break;
		case EFVExpertRodType::Helm:      // a steering-wheel reel
			Disc(Img, 6.f, 12.5f, 2.4f, 2.4f, Pc('m'));
			Disc(Img, 6.f, 12.5f, 1.2f, 1.2f, Pc('y'));
			Img.Set(3, 12, Pc('M')); Img.Set(8, 12, Pc('M')); Img.Set(5, 10, Pc('M')); Img.Set(5, 15, Pc('M'));
			break;
		case EFVExpertRodType::Magnet:    // a red horseshoe magnet reel
			Img.Set(4, 12, Pc('a')); Img.Set(4, 13, Pc('a')); Img.Set(5, 14, Pc('a')); Img.Set(6, 14, Pc('a')); Img.Set(7, 13, Pc('a')); Img.Set(7, 12, Pc('a'));
			Img.Set(4, 11, Pc('S')); Img.Set(7, 11, Pc('S'));
			break;
		case EFVExpertRodType::Pendulum:  // a weighted drum with a swinging bob
			Img.Set(5, 12, Pc('s')); Img.Set(6, 12, Pc('s')); Img.Set(5, 13, Pc('x')); Img.Set(6, 13, Pc('x'));
			Img.Set(7, 14, Pc('y')); Img.Set(8, 15, Pc('d'));
			break;
		default:
			Img.Set(5, 12, Pc('s')); Img.Set(6, 12, Pc('s')); Img.Set(5, 13, Pc('x')); Img.Set(6, 13, Pc('x'));
			Img.Set(7, 13, AccL);
			break;
		}
		Img.Outline(FVArt::OutlineOf(RodD));
		// theme touches after the outline
		switch (Rod.Theme)
		{
		case EFVExpertTheme::Mirror: Twinkle(Img, 9, 5, FColor(255, 255, 255)); break;
		case EFVExpertTheme::Pole:   Img.Set(14, 1, FColor(120, 240, 170)); Img.Set(13, 1, FColor(200, 150, 255)); Img.Blend(14, 0, FColor(120, 240, 170, 140)); break;
		case EFVExpertTheme::Anchor: Img.Set(14, 12, Pc('x')); Img.Set(13, 13, Pc('x')); Img.Set(14, 13, Pc('X')); Img.Set(15, 13, Pc('x')); break;
		case EFVExpertTheme::Breeze: Img.Set(10, 7, Pc('W')); Img.Set(8, 9, Pc('W')); break;
		default: Img.Set(14, 1, Lt(RodC, 0.6f)); break;
		}
		// line and hook (1px, no outline); the expert line is silver; the Anchor rod has a little anchor instead
		const FColor LineC(226, 234, 244, 230);
		for (int32 Y = 2; Y <= 11; ++Y) { if (Img.Get(14, Y).A == 0) { Img.Set(14, Y, LineC); } }
		if (Rod.Theme != EFVExpertTheme::Anchor)
		{
			Img.Set(14, 13, Pc('x')); Img.Set(14, 14, Pc('x')); Img.Set(13, 14, Pc('x')); Img.Set(12, 13, Pc('x'));
		}
		return Img;
	}

	// ============================================================================================================
	// UI icons (16x16)
	// ============================================================================================================
	FFVImage Icon(EFVIcon I)
	{
		switch (I)
		{
		case EFVIcon::Leaders:   // podium 1-2-3
			return A({
				TEXT("................"),
				TEXT("......Yyy......."),
				TEXT(".....YyYyd......"),
				TEXT("......ydd......."),
				TEXT(".......d........"),
				TEXT(".....yyyyy......"),
				TEXT(".....yYYyd......"),
				TEXT(".SSs.yYyyd......"),
				TEXT(".SsxyyYyyd......"),
				TEXT("SSSSxyyYyd.RRr.."),
				TEXT("SsssxyyYyd.Rrrq."),
				TEXT("SsssxyyYydRRrrrq"),
				TEXT("SsssxyyyydRrrrrq"),
				TEXT("xxxxxdddddqqqqqq"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Axe:
			return Axe(1);
		case EFVIcon::Pickaxe:
			return Pickaxe(1);
		case EFVIcon::Saw:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("..MMM..........."),
				TEXT(".MmmMSSSSSSSSs.."),
				TEXT(".Mm.mSSSSSSSSSs."),
				TEXT(".MmmmSsssssssSx."),
				TEXT("..mmmxsxsxsxsxX."),
				TEXT("......x.x.x.x.x."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Anvil:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("...sSSSSSSSSs..."),
				TEXT("sSSSSSSSSSSSx..."),
				TEXT(".xxsSSSSSSSxX..."),
				TEXT("....xsssssxX...."),
				TEXT(".....xsssxX....."),
				TEXT(".....xsssxX....."),
				TEXT("....xsssssxX...."),
				TEXT("...XXXXXXXXXX..."),
				TEXT("..mMMMMMMMMMMm.."),
				TEXT("..eeeeeeeeeeee.."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Seed:
			return A({
				TEXT("................"),
				TEXT("........G......."),
				TEXT(".......GgG......"),
				TEXT("......Gg.gG....."),
				TEXT("......g..gh....."),
				TEXT(".........h......"),
				TEXT("........Hh......"),
				TEXT(".......fFHf....."),
				TEXT("......fFFHHf...."),
				TEXT(".....LJfFHfJL..."),
				TEXT("....LJJJJJJJJL.."),
				TEXT("...DLJJJDJJJJLD."),
				TEXT("...DDDDDDDDDDDD."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Boat:
			return A({
				TEXT("................"),
				TEXT("........m......."),
				TEXT("........mW......"),
				TEXT("........mWW....."),
				TEXT("........mWWW...."),
				TEXT("........mWWWW..."),
				TEXT("........mWWWWW.."),
				TEXT("........m......."),
				TEXT(".MMMMMMMMMMMMMM."),
				TEXT("..mMMMMMMMMMMm.."),
				TEXT("...mmmmmmmmmm..."),
				TEXT("....eeeeeeee...."),
				TEXT("..BB..BB...BB..."),
				TEXT(".B..BB..BBB..B.."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Scroll:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("..CCCCCCCCCCC..."),
				TEXT(".CcCccccccccCC.."),
				TEXT(".CCcccccccccCC.."),
				TEXT("...cJJJJJJJJc..."),
				TEXT("...cccccccccc..."),
				TEXT("...cJJJJJJJcc..."),
				TEXT("...cccccccccc..."),
				TEXT("...cJJJJJJccc..."),
				TEXT("...cccccccccc..."),
				TEXT("..CCCCCCCCCCCC.."),
				TEXT(".CcccccccccCcC.."),
				TEXT("..CCCCCCCCCCC..."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Artel:   // two crossed oars over a net float
			return A({
				TEXT("................"),
				TEXT(".MM.........MM.."),
				TEXT(".MmM.......MmM.."),
				TEXT("..Mmm.....mmM..."),
				TEXT("...mm.....mm...."),
				TEXT("....mm...mm....."),
				TEXT(".....mm.mm......"),
				TEXT("......mmm......."),
				TEXT(".....mmOmm......"),
				TEXT("....mmOOOmm....."),
				TEXT("...mm.OWO.mm...."),
				TEXT("..mm...O...mm..."),
				TEXT(".ee.........ee.."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::ExpertRod:
		{
			FFVRodDef R;
			R.RodColor = FColor(96, 60, 150);
			R.AccentColor = FColor(255, 204, 51);
			R.bExpert = true;
			R.ExpertType = EFVExpertRodType::Helm;
			R.Theme = EFVExpertTheme::Pole;
			return ExpertRod(R);
		}
		case EFVIcon::SeaCalm:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("........yyy....."),
				TEXT(".......yYYYy...."),
				TEXT(".......yYYYy...."),
				TEXT("........yyy....."),
				TEXT("................"),
				TEXT("................"),
				TEXT("..BBBB....BBBB.."),
				TEXT("bbbbbbbbbbbbbbbb"),
				TEXT("................"),
				TEXT(".....BBBB......."),
				TEXT("bbbbbbbbbbbbbbbb"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::SeaWind:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("...WWWWWWWW....."),
				TEXT("..........WW...."),
				TEXT(".WWWWWWWW..W...."),
				TEXT(".........W.W...."),
				TEXT("..WWWWW..WW....."),
				TEXT("................"),
				TEXT("..BB....BB......"),
				TEXT(".BbbB..BbbB..BB."),
				TEXT("bb..bbbb..bbbbbb"),
				TEXT("................"),
				TEXT("bbbbbbbbbbbbbbbb"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::SeaStorm:
			return A({
				TEXT("................"),
				TEXT("....xxxx........"),
				TEXT("..xxXXXXxxx....."),
				TEXT(".xXXXXXXXXXxx..."),
				TEXT(".XXXXXXyXXXXXx.."),
				TEXT("..XXXXyyXXXXX..."),
				TEXT("......yY........"),
				TEXT(".....yY........."),
				TEXT("......Y..W......"),
				TEXT("..W..........W.."),
				TEXT(".BWB....BWB..WB."),
				TEXT("BbbbB..BbbbBBbbB"),
				TEXT("bbbbbbbbbbbbbbbb"),
				TEXT("7777777777777777"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::SeaFog:
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("..WWWWWWWW......"),
				TEXT("................"),
				TEXT("......WWWWWWWWW."),
				TEXT("................"),
				TEXT(".WWWWWWWWW......"),
				TEXT("................"),
				TEXT(".....WWWWWWWWW.."),
				TEXT("................"),
				TEXT("bbbbbbbbbbbbbbbb"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Tide:   // water level arrows up / down over the waves
			return A({
				TEXT("................"),
				TEXT("....w....s......"),
				TEXT("...www...s......"),
				TEXT("..wwwww..s......"),
				TEXT("....w....s......"),
				TEXT("....w..sssss...."),
				TEXT("....w...sss....."),
				TEXT("....w....s......"),
				TEXT("................"),
				TEXT("..BB....BB....BB"),
				TEXT("bbbbbbbbbbbbbbbb"),
				TEXT("7777777777777777"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Dark:   // a dark eye with a thin moon
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("......QQQQ......"),
				TEXT("....QQ7777QQ...."),
				TEXT("...Q777777YQQ..."),
				TEXT("..Q7777777YY7Q.."),
				TEXT("..Q7777777YY7Q.."),
				TEXT("..Q7777777Y77Q.."),
				TEXT("...Q7777777YQ..."),
				TEXT("....QQ7777QQ...."),
				TEXT("......QQQQ......"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::WarnWave:
			return A({
				TEXT("................"),
				TEXT("........BBBB...."),
				TEXT("......BBbbbbB..."),
				TEXT(".....Bbb....bW.."),
				TEXT("....Bbb.....WW.."),
				TEXT("...Bbb.........."),
				TEXT("...bb..........."),
				TEXT("..Bbb..........."),
				TEXT("..bbb.......B..."),
				TEXT(".Bbbb......Bb..."),
				TEXT(".bbbbB...BBbb..."),
				TEXT("Bbbbbbbbbbbbbb.."),
				TEXT("7777777777777777"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::WarnWhirl:
			return A({
				TEXT("................"),
				TEXT("....bbbbbbb....."),
				TEXT("..bb.......bb..."),
				TEXT(".b...BBBBB...b.."),
				TEXT(".b..B.....B..b.."),
				TEXT("b..B..bbb..B..b."),
				TEXT("b..B.b...b.B..b."),
				TEXT("b..B.b.7.b.B..b."),
				TEXT("b..B..b.b..B..b."),
				TEXT(".b..B....bB..b.."),
				TEXT(".b...BBBB....b.."),
				TEXT("..bb.......bb..."),
				TEXT("....bbbbbbb....."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::WarnDrip:   // a drop falling from the cave roof
			return A({
				TEXT("?!!!!!!!!!!!!!!?"),
				TEXT(".\?!!!\?\?!!!\?\?!!\?."),
				TEXT("..??..?!?..??..."),
				TEXT("......?!?......."),
				TEXT(".......B........"),
				TEXT("................"),
				TEXT(".......B........"),
				TEXT("......BBb......."),
				TEXT(".....BlBbb......"),
				TEXT(".....BBbbb......"),
				TEXT("......bbb......."),
				TEXT("................"),
				TEXT("..b..........b.."),
				TEXT("...bBBBBBBBBb..."),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::WarnSpring:   // a spring gushing up
			return A({
				TEXT("................"),
				TEXT("......l.l......."),
				TEXT(".....l.B.l......"),
				TEXT("....l..B..l....."),
				TEXT("......BlB......."),
				TEXT("......BlB......."),
				TEXT(".....BBlBB......"),
				TEXT(".....BllbB......"),
				TEXT("....BBlbbBB....."),
				TEXT("...BBllbbbBB...."),
				TEXT(".BBbbbbbbbbbBB.."),
				TEXT("bbbbbbbbbbbbbbbb"),
				TEXT("?!!!!!!!!!!!!!!?"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::WarnDash:   // the dash (рывок): a red double arrow, the same red everywhere (Р32)
			return A({
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("....a.....a....."),
				TEXT("....aa....aa...."),
				TEXT("....aAa...aAa..."),
				TEXT("aaaaaAAaaaaAAa.."),
				TEXT("AAAAAAAAAAAAAAa."),
				TEXT("zzzzzAAzzzzAAz.."),
				TEXT("....zAz...zAz..."),
				TEXT("....zz....zz...."),
				TEXT("....z.....z....."),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
				TEXT("................"),
			});
		case EFVIcon::Tensionmeter:   // a gauge: green -> yellow -> red arc, needle
		{
			FFVImage Img(16, 16);
			for (int32 Y = 0; Y < 16; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					const float DX = X + 0.5f - 8.f, DY = Y + 0.5f - 11.f;
					const float D = FMath::Sqrt(DX * DX + DY * DY);
					if (DY > 0.5f || D > 7.f) { continue; }
					if (D > 5.2f)
					{
						const float Ang = FMath::Atan2(-DY, DX);   // 0 right .. PI left
						Img.Set(X, Y, Ang > 2.1f ? FColor(106, 190, 48) : (Ang > 1.05f ? FColor(255, 204, 51) : FColor(220, 58, 58)));
					}
					else { Img.Set(X, Y, D > 4.3f ? Pc('x') : Pc('W')); }
				}
			}
			for (int32 X = 1; X <= 14; ++X) { Img.Set(X, 11, Pc('x')); Img.Set(X, 12, Pc('X')); }
			Img.Outline(FVArt::InkOutline());
			// needle towards the red
			Img.Set(8, 10, Pc('Q')); Img.Set(9, 9, Pc('Q')); Img.Set(10, 8, Pc('a')); Img.Set(11, 7, Pc('a')); Img.Set(7, 10, Pc('Q'));
			return Img;
		}
		default:
			break;
		}
		return FFVImage();
	}
}

// ==================================================================================================================
// Forwarders (called from the default branches of FVArtItems.cpp / FVSprites.cpp)
// ==================================================================================================================
FFVImage FVArtItemsU6::BuildItemArt(EFVItemArt Art, FColor Tint, uint8 Tier) { return FVArtItemsU6Impl::ItemArt(Art, Tint, Tier); }
FFVImage FVArtItemsU6::BuildBait(EFVBaitStyle Style) { return FVArtItemsU6Impl::Bait(Style); }
FFVImage FVArtItemsU6::BuildJunk(EFVJunkStyle Style) { return FVArtItemsU6Impl::Junk(Style); }
FFVImage FVArtItemsU6::BuildRodIcon(const FFVRodDef& Rod) { return Rod.bExpert ? FVArtItemsU6Impl::ExpertRod(Rod) : FFVImage(); }
FFVImage FVArtItemsU6::BuildIcon(EFVIcon I) { return FVArtItemsU6Impl::Icon(I); }

// ==================================================================================================================
// Contract
// ==================================================================================================================
const FFVSprite& UFVSprites::AxeIcon(int32 Level)
{
	const int32 L = FMath::Clamp(Level, 1, 4);
	const uint64 Key = MakeKey(FVSpriteCat::ItemU6, 1, uint32(L));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildAxeIcon(L), FString::Printf(TEXT("Axe_%d"), L));
}

const FFVSprite& UFVSprites::PickaxeIcon(int32 Level)
{
	const int32 L = FMath::Clamp(Level, 1, 2);
	const uint64 Key = MakeKey(FVSpriteCat::ItemU6, 2, uint32(L));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildPickaxeIcon(L), FString::Printf(TEXT("Pickaxe_%d"), L));
}

FFVImage UFVSprites::BuildAxeIcon(int32 Level)
{
	return FVArtItemsU6Impl::Axe(Level);
}

FFVImage UFVSprites::BuildPickaxeIcon(int32 Level)
{
	return FVArtItemsU6Impl::Pickaxe(Level);
}
