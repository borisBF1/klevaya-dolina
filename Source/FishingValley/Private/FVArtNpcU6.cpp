// UPDATE 6 [ART-NPC] — villagers of the forest and the City (idle sprites + portraits) (Docs/UPDATE6_FOREST_CITY.md §4, §8, §8.2, §8.3). Owner: ART-NPC.
// Contract: Public/FVSprites.h «UPDATE 6».
// Style: the villagers are built on the player's body (UFVSprites::BuildCharacter, Idle, facing Down, same 32x48 canvas,
// feet at CharacterAnchor() = (16,46), 2 idle frames), recoloured with FFVNpcDef Skin / Hair / Cloth / Cloth2 and dressed
// with their own details (beards, aprons, nets, glasses, toque, pipe...). Outline (56,30,42) as FVArtCharacter.cpp.
// Portrait: 32x32 bust = the head and collar of frame 0 (x 8..23, y 12..27) at x2 over a soft backdrop (forest green /
// City sea blue) with a rounded frame. Unknown ids get a neutral villager. Deterministic, cached under FVSpriteCat::Npc
// (A = name hash, B = frame, C = 0 sprite / 1 portrait).

#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"

namespace FVArtNpcU6Impl
{
	const FColor Out(56, 30, 42);
	// the player's body colours (MakeStyle in FVArtCharacter.cpp) that are swapped for the villager's
	const FColor PlSkin(252, 206, 162), PlSkinS(228, 160, 122), PlHair(138, 82, 46), PlHairS(100, 56, 34), PlHairL(184, 120, 64);

	enum class EKind : uint8 { Neutral, Potapych, Gavrila, Nikifor, Agafya, Kuzma, Zhanna, Burun, Henri, Foma, Lidia, Zoya, Efim, Marfa,
		Prokop /* §18 [MILL-ART] Мельник Прокоп */ };

	EKind KindOf(FName Id)
	{
		static const TPair<const TCHAR*, EKind> Map[] = {
			{ TEXT("npc_potapych"), EKind::Potapych }, { TEXT("npc_gavrila"), EKind::Gavrila }, { TEXT("npc_nikifor"), EKind::Nikifor },
			{ TEXT("npc_agafya"), EKind::Agafya }, { TEXT("npc_kuzma"), EKind::Kuzma }, { TEXT("npc_zhanna"), EKind::Zhanna },
			{ TEXT("npc_burun"), EKind::Burun }, { TEXT("npc_henri"), EKind::Henri }, { TEXT("npc_foma"), EKind::Foma },
			{ TEXT("npc_lidia"), EKind::Lidia }, { TEXT("npc_zoya"), EKind::Zoya }, { TEXT("npc_efim"), EKind::Efim },
			{ TEXT("npc_marfa"), EKind::Marfa }, { TEXT("npc_prokop"), EKind::Prokop } };
		for (const TPair<const TCHAR*, EKind>& E : Map)
		{
			if (Id == FName(E.Key)) { return E.Value; }
		}
		return EKind::Neutral;
	}

	struct FLookU6
	{
		FColor Skin = FColor(236, 188, 150), Hair = FColor(90, 60, 40), Cloth = FColor(120, 90, 60), Cloth2 = FColor(200, 190, 170);
		bool bCity = false;
	};

	FLookU6 MakeLook(FColor Skin, FColor Hair, FColor Cloth, FColor Cloth2, bool bCity)
	{
		FLookU6 L;
		L.Skin = Skin; L.Hair = Hair; L.Cloth = Cloth; L.Cloth2 = Cloth2; L.bCity = bCity;
		return L;
	}

	/** Colours: from FFVNpcDef when the villager is in the database, else the package's own per id. */
	FLookU6 LookOf(FName Id, EKind K)
	{
		if (const FFVNpcDef* D = FVDBU6::FindNpc(Id))
		{
			return MakeLook(D->Skin, D->Hair, D->Cloth, D->Cloth2, D->Location == FName(TEXT("city")));
		}
		const FColor SkinLight(236, 188, 150), SkinWarm(222, 170, 128), SkinTan(198, 146, 106), SkinPale(244, 204, 172);
		switch (K)
		{
		case EKind::Potapych: return MakeLook(SkinWarm, FColor(170, 168, 160), FColor(82, 104, 62), FColor(150, 70, 52), false);
		case EKind::Gavrila:  return MakeLook(SkinTan, FColor(112, 72, 40), FColor(236, 162, 46), FColor(70, 86, 120), false);
		case EKind::Nikifor:  return MakeLook(SkinTan, FColor(60, 44, 32), FColor(66, 82, 110), FColor(214, 200, 170), false);
		case EKind::Agafya:   return MakeLook(SkinLight, FColor(150, 130, 110), FColor(96, 140, 70), FColor(226, 86, 72), false);
		case EKind::Kuzma:    return MakeLook(SkinWarm, FColor(40, 34, 30), FColor(58, 70, 84), FColor(196, 150, 70), false);
		case EKind::Zhanna:   return MakeLook(SkinPale, FColor(40, 28, 36), FColor(150, 40, 90), FColor(242, 206, 90), true);
		case EKind::Burun:    return MakeLook(SkinTan, FColor(210, 210, 205), FColor(34, 50, 92), FColor(236, 196, 72), true);
		case EKind::Henri:    return MakeLook(SkinPale, FColor(70, 44, 30), FColor(246, 246, 240), FColor(196, 50, 50), true);
		case EKind::Foma:     return MakeLook(SkinWarm, FColor(120, 100, 80), FColor(40, 60, 88), FColor(226, 226, 220), true);
		case EKind::Lidia:    return MakeLook(SkinLight, FColor(150, 90, 50), FColor(236, 238, 242), FColor(70, 110, 150), true);
		case EKind::Zoya:     return MakeLook(SkinWarm, FColor(180, 60, 40), FColor(90, 130, 150), FColor(236, 222, 190), true);
		case EKind::Efim:     return MakeLook(SkinTan, FColor(236, 236, 230), FColor(70, 58, 50), FColor(40, 56, 80), true);
		case EKind::Marfa:    return MakeLook(SkinLight, FColor(110, 60, 40), FColor(160, 70, 60), FColor(244, 236, 214), true);
		case EKind::Prokop:   return MakeLook(SkinWarm, FColor(214, 210, 200), FColor(236, 230, 212), FColor(120, 96, 70), false);
		default: break;
		}
		return FLookU6();
	}

	FORCEINLINE FColor Lt(FColor C, float A) { return FVArt::Light(C, A); }
	FORCEINLINE FColor Dk(FColor C, float A) { return FVArt::Shade(C, A); }

	/** The body: player template with the villager's hat / outfit. */
	FFVAppearance AppearanceOf(EKind K, const FLookU6& L)
	{
		FFVAppearance A;
		A.Hat = EFVHatStyle::None;
		A.Outfit = EFVOutfitStyle::Shirt;
		A.OutfitPrimary = L.Cloth;
		A.OutfitSecondary = L.Cloth2;
		switch (K)
		{
		case EKind::Potapych: A.Hat = EFVHatStyle::Ushanka; A.HatPrimary = FColor(118, 84, 56); A.HatSecondary = FColor(206, 190, 160); A.Outfit = EFVOutfitStyle::Vest; break;
		case EKind::Gavrila:  A.Hat = EFVHatStyle::Cap; A.HatPrimary = L.Cloth; A.HatSecondary = Dk(L.Cloth, 0.3f);
			A.Outfit = EFVOutfitStyle::Overalls; A.OutfitPrimary = L.Cloth2; A.OutfitSecondary = L.Cloth; break;
		case EKind::Nikifor:  A.Hat = EFVHatStyle::Beanie; A.HatPrimary = FColor(150, 60, 50); A.HatSecondary = FColor(230, 220, 200); break;
		case EKind::Agafya:   A.Hat = EFVHatStyle::Bandana; A.HatPrimary = L.Cloth2; A.HatSecondary = FColor(250, 240, 220); A.Outfit = EFVOutfitStyle::Raincoat; break;
		case EKind::Kuzma:    A.Hat = EFVHatStyle::Bucket; A.HatPrimary = FColor(120, 130, 90); A.HatSecondary = FColor(90, 96, 70); A.Outfit = EFVOutfitStyle::Vest; break;
		case EKind::Zhanna:   A.Outfit = EFVOutfitStyle::Suit; break;
		case EKind::Burun:    A.Hat = EFVHatStyle::CaptainCap; A.HatPrimary = L.Cloth; A.HatSecondary = L.Cloth2; A.Outfit = EFVOutfitStyle::CaptainCoat; break;
		case EKind::Henri:    A.Outfit = EFVOutfitStyle::Shirt; break;
		case EKind::Foma:     A.Hat = EFVHatStyle::Beanie; A.HatPrimary = L.Cloth; A.HatSecondary = L.Cloth2; A.Outfit = EFVOutfitStyle::Hoodie; break;
		case EKind::Lidia:    A.Outfit = EFVOutfitStyle::Raincoat; break;
		case EKind::Zoya:     A.Hat = EFVHatStyle::Bandana; A.HatPrimary = FColor(230, 150, 60); A.HatSecondary = FColor(250, 230, 150); A.Outfit = EFVOutfitStyle::Raincoat; break;
		case EKind::Efim:     A.Hat = EFVHatStyle::Cap; A.HatPrimary = L.Cloth2; A.HatSecondary = FColor(30, 36, 56); A.Outfit = EFVOutfitStyle::Suit;
			A.OutfitPrimary = L.Cloth2; A.OutfitSecondary = L.Cloth; break;
		case EKind::Marfa:    A.Hat = EFVHatStyle::Bandana; A.HatPrimary = FColor(240, 200, 90); A.HatSecondary = FColor(200, 70, 60); A.Outfit = EFVOutfitStyle::Raincoat; break;
		// the miller: a flour-grey peaked cap (картуз), a linen shirt, brown trousers
		case EKind::Prokop:   A.Hat = EFVHatStyle::Cap; A.HatPrimary = FColor(150, 140, 124); A.HatSecondary = FColor(206, 200, 186); break;
		default: break;
		}
		return A;
	}

	void Recolor(FFVImage& Img, const FLookU6& L)
	{
		const FColor SkinS = Dk(L.Skin, 0.22f), HairS = Dk(L.Hair, 0.3f), HairL = Lt(L.Hair, 0.3f);
		for (FColor& C : Img.Px)
		{
			if (C.A == 0) { continue; }
			if (C == PlSkin) { C = L.Skin; }
			else if (C == PlSkinS) { C = SkinS; }
			else if (C == PlHair) { C = L.Hair; }
			else if (C == PlHairS) { C = HairS; }
			else if (C == PlHairL) { C = HairL; }
		}
	}

	/** Two layers of details: Inner (painted over the body, no outline) and Outer (sticking out, gets the outline). */
	struct FDress
	{
		FFVImage Inner, Outer;
		int32 D = 0;
		explicit FDress(int32 InD) : Inner(32, 48), Outer(32, 48), D(InD) {}
		void I(int32 X, int32 Y, FColor C) { Inner.Set(X, Y + D, C); }
		void O(int32 X, int32 Y, FColor C) { Outer.Set(X, Y + D, C); }
		void IH(int32 X0, int32 X1, int32 Y, FColor C) { for (int32 X = X0; X <= X1; ++X) { I(X, Y, C); } }
		void OH(int32 X0, int32 X1, int32 Y, FColor C) { for (int32 X = X0; X <= X1; ++X) { O(X, Y, C); } }
	};

	struct FRowSpan { int32 Y, X0, X1; };

	/** Full beard from the cheeks down over the collar. Len: 0 short, 1 long. */
	void Beard(FDress& Dr, FColor H, int32 Len)
	{
		const FColor Hi = Lt(H, 0.25f), Lo = Dk(H, 0.25f);
		static const FRowSpan Short[] = { { 24, 10, 11 }, { 24, 20, 21 }, { 25, 10, 21 }, { 26, 11, 20 }, { 27, 12, 19 }, { 28, 13, 18 } };
		static const FRowSpan Long[] = { { 29, 13, 18 }, { 30, 14, 17 }, { 31, 15, 16 } };
		for (const FRowSpan& R : Short)
		{
			for (int32 X = R.X0; X <= R.X1; ++X) { Dr.O(X, R.Y, X < 14 ? Hi : (X > 18 ? Lo : H)); }
		}
		if (Len > 0)
		{
			for (const FRowSpan& R : Long) { for (int32 X = R.X0; X <= R.X1; ++X) { Dr.O(X, R.Y, X < 15 ? Hi : H); } }
		}
		// mustache and the mouth
		Dr.OH(13, 18, 24, Lo);
		Dr.O(15, 25, Dk(H, 0.5f)); Dr.O(16, 25, Dk(H, 0.5f));
		Dr.O(14, 27, Lo); Dr.O(17, 28, Lo);
	}

	void Mustache(FDress& Dr, FColor H, bool bCurled)
	{
		Dr.IH(13, 18, 25, H);
		Dr.I(14, 25, Lt(H, 0.2f));
		if (bCurled) { Dr.O(12, 24, H); Dr.O(19, 24, H); }
	}

	void Glasses(FDress& Dr, FColor Rim)
	{
		for (int32 EX : { 12, 17 })
		{
			Dr.IH(EX, EX + 2, 21, Rim); Dr.IH(EX, EX + 2, 24, Rim);
			Dr.I(EX, 22, Rim); Dr.I(EX, 23, Rim); Dr.I(EX + 2, 22, Rim); Dr.I(EX + 2, 23, Rim);
			Dr.I(EX + 1, 22, FColor(210, 236, 250));
		}
		Dr.I(15, 22, Rim); Dr.I(16, 22, Rim);
	}

	/** Apron over the torso and the coat skirt. */
	void Apron(FDress& Dr, FColor C, bool bLong)
	{
		const int32 Y1 = bLong ? 42 : 36;
		for (int32 Y = 29; Y <= Y1; ++Y)
		{
			const int32 X0 = Y < 32 ? 13 : 12, X1 = Y < 32 ? 18 : 19;
			for (int32 X = X0; X <= X1; ++X) { Dr.I(X, Y, X == X0 ? Lt(C, 0.2f) : (X == X1 ? Dk(C, 0.2f) : C)); }
		}
		Dr.IH(11, 20, 32, Dk(C, 0.25f));   // the tie
		Dr.IH(14, 17, 34, Dk(C, 0.12f));   // pocket
		Dr.IH(12, 19, Y1, Dk(C, 0.3f));
	}

	void Earrings(FDress& Dr, FColor C) { Dr.O(8, 24, C); Dr.O(23, 24, C); Dr.O(8, 25, Dk(C, 0.2f)); Dr.O(23, 25, Dk(C, 0.2f)); }

	void Necklace(FDress& Dr, FColor A, FColor B)
	{
		for (int32 X = 12; X <= 19; ++X) { Dr.I(X, (X == 12 || X == 19) ? 28 : 29, (X % 2) ? A : B); }
		Dr.I(15, 30, A); Dr.I(16, 30, B);
	}

	void Pipe(FDress& Dr)
	{
		const FColor W(110, 66, 40);
		Dr.O(18, 26, W); Dr.O(19, 26, W); Dr.O(20, 26, W); Dr.O(21, 25, W); Dr.O(21, 24, Dk(W, 0.3f)); Dr.O(20, 25, Dk(W, 0.2f));
		Dr.O(22, 22, FColor(210, 210, 220)); Dr.O(21, 21, FColor(236, 236, 240));   // smoke
	}

	FFVImage Build(FName Id, int32 Frame)
	{
		const EKind K = KindOf(Id);
		const FLookU6 L = LookOf(Id, K);
		const int32 F = ((Frame % 2) + 2) % 2;
		FFVImage Img = UFVSprites::BuildCharacter(AppearanceOf(K, L), EFVCharAnim::Idle, EFVFacing::Down, F);
		Recolor(Img, L);
		FDress Dr(F == 1 ? 1 : 0);
		const FColor GoldC(255, 204, 51), Silver(220, 226, 236);
		switch (K)
		{
		case EKind::Potapych:
			Beard(Dr, L.Hair, 1);
			// a bag strap across the chest and a hatchet at the belt
			for (int32 T = 0; T < 9; ++T) { Dr.I(11 + T, 28 + T, FColor(120, 84, 52)); }
			Dr.O(22, 33, FColor(150, 104, 64)); Dr.O(22, 34, FColor(150, 104, 64)); Dr.O(22, 35, FColor(150, 104, 64));
			Dr.O(23, 32, FColor(170, 176, 190)); Dr.O(24, 32, FColor(130, 136, 150)); Dr.O(23, 33, FColor(170, 176, 190));
			break;
		case EKind::Gavrila:
			Mustache(Dr, Dk(L.Hair, 0.1f), false);
			// the pencil behind the ear and a folding rule in the bib pocket
			Dr.O(22, 18, FColor(250, 206, 60)); Dr.O(22, 19, FColor(250, 206, 60)); Dr.O(22, 20, FColor(236, 170, 150)); Dr.O(22, 17, FColor(60, 50, 50));
			Dr.I(17, 30, FColor(250, 220, 90)); Dr.I(17, 31, FColor(250, 220, 90)); Dr.I(18, 30, FColor(60, 50, 50));
			break;
		case EKind::Nikifor:
		{
			Mustache(Dr, L.Hair, false);
			// fishing nets over the shoulder: a diagonal mesh with floats
			const FColor NetC(214, 196, 150), NetD(160, 140, 100);
			for (int32 T = 0; T <= 12; ++T)
			{
				const int32 X = 9 + T, Y = 27 + T * 3 / 4;
				for (int32 W = 0; W < 3; ++W) { Dr.O(X, Y + W, ((X + Y + W) % 2) ? NetC : NetD); }
			}
			Dr.O(12, 30, FColor(230, 70, 60)); Dr.O(16, 33, FColor(250, 250, 240)); Dr.O(20, 36, FColor(230, 70, 60));
			Dr.O(8, 27, NetD); Dr.O(8, 28, NetC);
			break;
		}
		case EKind::Agafya:
			Apron(Dr, FColor(248, 244, 232), true);
			// a carrot bunch in the hand
			Dr.O(23, 34, FColor(240, 130, 40)); Dr.O(23, 35, FColor(240, 130, 40)); Dr.O(24, 34, FColor(220, 110, 30));
			Dr.O(23, 33, FColor(90, 170, 70)); Dr.O(24, 32, FColor(90, 170, 70)); Dr.O(22, 32, FColor(60, 130, 60));
			break;
		case EKind::Kuzma:
			Mustache(Dr, L.Hair, true);
			Glasses(Dr, FColor(60, 50, 50));
			// lures pinned to the vest
			Dr.I(12, 30, Silver); Dr.I(12, 31, FColor(230, 70, 60)); Dr.I(19, 30, GoldC); Dr.I(19, 31, FColor(80, 170, 220)); Dr.I(13, 33, Silver);
			break;
		case EKind::Zhanna:
		{
			// hair bun, long side locks, earrings, a pearl necklace and red lips
			const FColor H = L.Hair, HL = Lt(L.Hair, 0.3f);
			for (int32 Y = 10; Y <= 13; ++Y) { Dr.OH(13, 18, Y, (Y == 10) ? HL : H); }
			Dr.O(12, 11, H); Dr.O(19, 11, Dk(H, 0.3f)); Dr.O(14, 10, Lt(HL, 0.3f));
			Dr.O(17, 12, L.Cloth2);   // hairpin
			for (int32 Y = 22; Y <= 29; ++Y) { Dr.O(8, Y, H); Dr.O(23, Y, Dk(H, 0.25f)); }
			Earrings(Dr, L.Cloth2);
			Necklace(Dr, FColor(250, 246, 236), FColor(220, 214, 206));
			Dr.I(15, 25, FColor(210, 50, 70)); Dr.I(16, 25, FColor(210, 50, 70));
			Dr.I(13, 21, Dk(H, 0.2f)); Dr.I(18, 21, Dk(H, 0.2f));   // lashes
			break;
		}
		case EKind::Burun:
			Beard(Dr, L.Hair, 1);
			Pipe(Dr);
			break;
		case EKind::Henri:
		{
			// the chef's toque, a red neckerchief and a curled mustache
			const FColor W(250, 250, 246), WS(216, 214, 222);
			Dr.OH(10, 21, 14, WS); Dr.OH(10, 21, 13, W);
			for (int32 Y = 7; Y <= 12; ++Y) { Dr.OH(11, 20, Y, W); Dr.O(20, Y, WS); Dr.O(11, Y, FColor(255, 255, 255)); }
			for (int32 X = 10; X <= 21; ++X) { Dr.O(X, 6, (X % 3) ? W : WS); }
			Dr.OH(11, 20, 5, W); Dr.O(14, 8, WS); Dr.O(17, 9, WS);
			Mustache(Dr, L.Hair, true);
			Dr.IH(13, 18, 27, L.Cloth2); Dr.IH(14, 17, 28, Dk(L.Cloth2, 0.2f)); Dr.I(15, 29, L.Cloth2);
			// buttons of the chef's jacket
			Dr.I(14, 31, FColor(210, 206, 196)); Dr.I(17, 31, FColor(210, 206, 196)); Dr.I(14, 33, FColor(210, 206, 196)); Dr.I(17, 33, FColor(210, 206, 196));
			break;
		}
		case EKind::Foma:
		{
			Beard(Dr, L.Hair, 0);
			// the keeper's lantern in the hand
			const FColor Lamp(255, 220, 120), Rim(60, 60, 70);
			Dr.O(24, 31, Rim); Dr.OH(23, 25, 32, Rim);
			Dr.O(23, 33, Rim); Dr.O(24, 33, Lamp); Dr.O(25, 33, Rim);
			Dr.O(23, 34, Rim); Dr.O(24, 34, Lt(Lamp, 0.4f)); Dr.O(25, 34, Rim);
			Dr.OH(23, 25, 35, Rim);
			break;
		}
		case EKind::Lidia:
		{
			// ponytail, glasses, the shirt collar under the lab coat and a notebook
			const FColor H = L.Hair;
			for (int32 Y = 18; Y <= 27; ++Y) { Dr.O(23 + (Y > 22 ? 1 : 0), Y, (Y % 3) ? H : Dk(H, 0.25f)); }
			Dr.O(22, 18, L.Cloth2);
			Glasses(Dr, FColor(90, 70, 60));
			Dr.IH(14, 17, 28, L.Cloth2); Dr.I(15, 29, L.Cloth2); Dr.I(16, 29, Dk(L.Cloth2, 0.2f));
			const FColor Book(70, 110, 170);
			Dr.O(6, 32, Book); Dr.O(7, 32, Book); Dr.O(6, 33, FColor(240, 236, 220)); Dr.O(7, 33, Book); Dr.O(6, 34, Book); Dr.O(7, 34, Book);
			Dr.I(19, 30, FColor(40, 60, 120)); Dr.I(19, 31, FColor(40, 60, 120));   // pen in the pocket
			break;
		}
		case EKind::Zoya:
		{
			// curls from under the scarf, big hoops, beads and a basket of fruit
			const FColor H = L.Hair;
			for (int32 Y = 21; Y <= 27; ++Y) { Dr.O(8, Y, (Y % 2) ? H : Lt(H, 0.2f)); Dr.O(23, Y, (Y % 2) ? Dk(H, 0.2f) : H); }
			Earrings(Dr, GoldC);
			Necklace(Dr, FColor(220, 60, 60), FColor(250, 200, 60));
			const FColor Wk(190, 140, 80), WkD(140, 96, 52);
			Dr.OH(22, 27, 33, Wk); Dr.OH(22, 27, 34, WkD); Dr.OH(23, 26, 35, Wk);
			Dr.O(22, 32, FColor(230, 60, 60)); Dr.O(24, 32, FColor(250, 170, 40)); Dr.O(26, 32, FColor(120, 190, 60)); Dr.O(25, 31, FColor(230, 60, 60));
			Dr.O(22, 31, WkD); Dr.O(27, 31, WkD); Dr.OH(23, 26, 30, WkD);
			break;
		}
		case EKind::Efim:
		{
			Beard(Dr, L.Hair, 0);
			// the pilot's spyglass under the arm and gold buttons
			const FColor Br(224, 172, 62), BrLo(160, 110, 36);
			for (int32 X = 20; X <= 26; ++X) { Dr.O(X, 31 - (X - 20) / 3, X < 23 ? BrLo : Br); }
			Dr.O(26, 28, Lt(Br, 0.4f));
			Dr.I(14, 30, GoldC); Dr.I(17, 30, GoldC); Dr.I(14, 33, GoldC); Dr.I(17, 33, GoldC);
			break;
		}
		case EKind::Marfa:
		{
			Apron(Dr, L.Cloth2, true);
			// a foaming mug of kvass
			const FColor Mug(170, 110, 60), Foam(252, 248, 236);
			Dr.OH(23, 25, 31, Foam); Dr.O(24, 30, Foam);
			Dr.OH(23, 25, 32, Mug); Dr.OH(23, 25, 33, Dk(Mug, 0.15f)); Dr.OH(23, 25, 34, Dk(Mug, 0.3f));
			Dr.O(26, 32, Mug); Dr.O(26, 33, Mug);
			Dr.I(13, 21, Dk(L.Hair, 0.2f)); Dr.I(18, 21, Dk(L.Hair, 0.2f));
			break;
		}
		case EKind::Prokop:
		{
			// long white beard, a canvas apron dusted with flour, a flour sack over the right shoulder
			Beard(Dr, L.Hair, 1);
			const FColor Canvas(206, 190, 150), FlourC(250, 248, 240);
			Apron(Dr, Canvas, true);
			static const FIntPoint Dust[] = { { 13, 33 }, { 16, 36 }, { 18, 39 }, { 14, 40 }, { 12, 37 }, { 17, 34 }, { 10, 30 }, { 21, 30 } };
			for (const FIntPoint& D : Dust) { Dr.I(D.X, D.Y, FlourC); }
			const FColor SackC(226, 212, 176), SackS(186, 166, 126);
			for (int32 Y = 23; Y <= 31; ++Y)
			{
				const int32 X0 = Y < 25 ? 21 : 20, X1 = Y < 25 ? 24 : 26;
				for (int32 X = X0; X <= X1; ++X) { Dr.O(X, Y, X == X0 ? Lt(SackC, 0.2f) : (X == X1 || Y == 31 ? SackS : SackC)); }
			}
			Dr.O(22, 22, SackS); Dr.O(23, 22, SackS);   // the tied neck
			Dr.O(23, 27, FColor(170, 80, 50)); Dr.O(24, 27, FColor(170, 80, 50));   // a stamp on the sack
			break;
		}
		default:
			break;
		}
		Img.Blit(Dr.Inner, 0, 0);
		// the outer details get their own outline, but it is never painted over the body
		FFVImage Outer = Dr.Outer;
		Outer.Outline(Out);
		for (int32 Y = 0; Y < Outer.H; ++Y)
		{
			for (int32 X = 0; X < Outer.W; ++X)
			{
				const FColor C = Outer.Get(X, Y);
				if (C.A == 0) { continue; }
				if (C == Out && Img.Get(X, Y).A != 0) { continue; }
				Img.Set(X, Y, C);
			}
		}
		return Img;
	}

	FFVImage Portrait(FName Id)
	{
		const EKind K = KindOf(Id);
		const FLookU6 L = LookOf(Id, K);
		const FFVImage Body = Build(Id, 0);
		FFVImage Img(32, 32);
		// backdrop: forest green or City sea blue, lit from the top-left, a hint of the villager's accent colour
		// the miller (§18): a warm autumn backdrop
		const bool bMill = K == EKind::Prokop;
		const FColor Top = bMill ? FColor(236, 178, 96) : (L.bCity ? FColor(120, 176, 214) : FColor(128, 180, 104));
		const FColor Bot = bMill ? FColor(150, 82, 46) : (L.bCity ? FColor(52, 96, 150) : FColor(56, 104, 64));
		for (int32 Y = 0; Y < 32; ++Y)
		{
			for (int32 X = 0; X < 32; ++X)
			{
				const bool bCorner = (X + Y < 2) || (31 - X + Y < 2) || (X + 31 - Y < 2) || (62 - X - Y < 2);
				if (bCorner) { continue; }
				FColor C = FVArt::Mix(Top, Bot, (Y + X * 0.3f) / 41.f);
				const float DX = X - 11.f, DY = Y - 9.f;
				if (DX * DX + DY * DY < 90.f) { C = FVArt::Mix(C, L.Cloth2, 0.12f); }
				if (((X / 2 + Y / 2) % 7) == 0 && (X + Y) % 3 == 0) { C = FVArt::Light(C, 0.08f); }
				Img.Set(X, Y, C);
			}
		}
		// the bust: head and collar at x2
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const FColor C = Body.Get(8 + X, 12 + Y);
				if (C.A == 0) { continue; }
				Img.Set(X * 2, Y * 2, C); Img.Set(X * 2 + 1, Y * 2, C); Img.Set(X * 2, Y * 2 + 1, C); Img.Set(X * 2 + 1, Y * 2 + 1, C);
			}
		}
		// frame
		const FColor FrameC(70, 44, 30);
		for (int32 T = 0; T < 32; ++T)
		{
			for (const FIntPoint& P : { FIntPoint(T, 0), FIntPoint(T, 31), FIntPoint(0, T), FIntPoint(31, T) })
			{
				if (Img.Get(P.X, P.Y).A != 0) { Img.Set(P.X, P.Y, FrameC); }
			}
		}
		Img.Set(1, 1, FrameC); Img.Set(30, 1, FrameC); Img.Set(1, 30, FrameC); Img.Set(30, 30, FrameC);
		return Img;
	}
}

const FFVSprite& UFVSprites::NpcSprite(FName NpcId, int32 Frame)
{
	const int32 F = ((Frame % 2) + 2) % 2;
	const uint64 Key = MakeKey(FVSpriteCat::Npc, GetTypeHash(NpcId), uint32(F), 0);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildNpc(NpcId, F), FString::Printf(TEXT("Npc_%s_%d"), *NpcId.ToString(), F));
}

const FFVSprite& UFVSprites::NpcPortrait(FName NpcId)
{
	const uint64 Key = MakeKey(FVSpriteCat::Npc, GetTypeHash(NpcId), 0, 1);
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildNpcPortrait(NpcId), FString::Printf(TEXT("NpcPortrait_%s"), *NpcId.ToString()));
}

FFVImage UFVSprites::BuildNpc(FName NpcId, int32 Frame)
{
	return FVArtNpcU6Impl::Build(NpcId, Frame);
}

FFVImage UFVSprites::BuildNpcPortrait(FName NpcId)
{
	return FVArtNpcU6Impl::Portrait(NpcId);
}
