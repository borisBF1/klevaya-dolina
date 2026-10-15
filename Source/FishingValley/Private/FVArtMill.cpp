// UPDATE 6 [MILL-ART] — «Старая мельница» art (Docs/UPDATE6_FOREST_CITY.md §18, Docs/UPDATE6_ART.md): the big world
// (FVWorldU6::BuildMill + MillZones, Private/FVWorldU6.h) and the object sprites (UFVSprites::MillObjSprite, FVSprites.h).
// Style: the phone version (3/4 top view, light from the top-left, PropOut outlines, FVArtKitU6 helpers of FVArtForest.cpp),
// autumn palette (golden grass, orange / red / gold crowns, dark spruces for contrast), morning fog (FWorldDef::bFog).
//
// ==================================================================================================================
// WORLD 960x400 texels (world px = texel * FV::WorldScale). Pivot of every sprite = bottom-centre.
// ------------------------------------------------------------------------------------------------------------------
//  y 0..112     autumn canopy (rows of maples / birches / spruces), the miller's house (x ~560, baked, feet y 110),
//               a rye patch with sheaves, the stream coming down from the top at x ~874.
//  y 112..236   walk band (Walk x 34..952): paths, fallen leaves; the mill (runtime sprite, feet 834,150) stands
//               by the stream, its water wheel over the stream; the plank bridge over the stream (y 176..194);
//               exit «В лес →» on the right edge (x 944..960, y 170..202).
//  y 236..344   the mill pond (ShoreY = 236): fish pen (x ~200), 3 feeder decks (x 300 / 430 / 560), the beaver
//               lodge (x ~768) with the dam (x 792..810) across the whole pond; the Omut right of the dam (x >= 812,
//               deep dark water, a whirl from the wheel). Net spots 0..2 at x 110 / 365 / 660, 3 = the Omut (x 915).
//  y 344..400   the far bank: reeds, bushes, autumn trees (decor only).
//  Zones (MillZones): deck_d = Rc(DeckX-16, 210, 32, 27); omut = Rc(812, 206, 136, 31) (blocked while the dam is not
//  finished — SCENE-OBJECTS, case EAnchor::MillZone).
// ------------------------------------------------------------------------------------------------------------------
//  MillObjSprite(Id, State, Frame):
//    "mill"          124x104  State 0 ruin / 1 wheel / 2 millstones / 3 roof = working (Frame 0..3: the wheel turns,
//                             the water falls). Collision: the base (scene, FootBlocker).
//    "beaver_lodge"   64x112  State 0..5: the lodge + the dam growing from both banks (5 = closed). Flat (layer 0).
//    "feed_deck"      34x30   State 0 / 1 baited (Frame 0..1 bubbles). Flat.
//    "fish_pen"       52x36   State 0..3 species inside (Frame 0..1). Flat.
//    "mill_bridge"    26x32   (forest) State 0 broken / 1 repaired. Flat, the player walks over it.
// ==================================================================================================================

#include "FVWorldU6.h"
#include "FVSprites.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"

// shared update-6 art kit (defined in FVArtForest.cpp)
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
	FColor LeafTone(FColor Base, int32 Tone);
	FColor CrownOutline(FColor Leaf);
	void FillEllipse(FFVImage& I, float CX, float CY, float RX, float RY, FColor C, bool bBlend);
	void LeafBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Leaf, uint32 Seed);
	void RockBlob(FFVImage& I, float CX, float CY, float RX, float RY, FColor Base, uint32 Seed);
	void ThickLine(FFVImage& I, FVector2D A, FVector2D B, float W0, float W1, FColor C);
	void Tufts(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Count, FColor Base, uint32 Seed);
	void Flowers(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Count, uint32 Seed);
	void PaintPath(FFVImage& I, const TArray<FVector2D>& Way, float Width, uint32 Seed);
	void DarkRim(FFVImage& I, int32 Band);
	void Stamp(FFVImage& Dst, const FFVImage& Src, int32 FeetX, int32 FeetY, bool bFlip, float ShadowRX);
	FFVImage MakeBush(uint32 Seed, FColor Leaf, int32 Size, bool bBerries);
	FFVImage MakeRock(uint32 Seed, int32 RX, int32 RY, FColor Base, bool bMoss);
	FFVImage MakeFern(uint32 Seed, FColor Leaf);
	FFVImage MakeMushroom(uint32 Seed, FColor Cap);
}
// log / plank helpers of the forester's izba (FVArtForest.cpp)
namespace FVArtForestImpl
{
	void LogWall(FFVImage& I, int32 X0, int32 X1, int32 Y0, int32 Y1, uint32 Seed);
	void TesRoof(FFVImage& I, int32 X0, int32 X1, int32 TopY, int32 EaveY, int32 Inset, FColor Base);
	void CarvedWindow(FFVImage& I, int32 X0, int32 Y0, int32 W, int32 H, FColor Frame);
}

namespace FVArtMillImpl
{
	using namespace FVArtKitU6;
	using FVWorldU6::EAnchor;
	using FVWorldU6::FAnchor;

	// ---------------------------------------------------------------------------------------- palette (autumn)
	const FColor GrassLo(126, 128, 56), GrassBase(154, 150, 68), GrassHi(186, 174, 88);
	const FColor TuftC(150, 146, 62);
	const FColor LeafRed(200, 72, 44), LeafOrange(226, 132, 48), LeafGold(236, 190, 66), LeafRust(176, 104, 48);
	const FColor SpruceC(40, 92, 70);
	const FColor Bark(116, 78, 48), BarkHi(150, 104, 64), BarkLo(84, 54, 34);
	const FColor BirchBark(236, 232, 220), BirchDash(60, 56, 60);
	const FColor PropOut(58, 36, 30);
	const FColor WoodHi(214, 170, 110), Wood(176, 128, 78), WoodLo(130, 88, 52), WoodDk(92, 60, 36), WoodOld(128, 112, 96);
	const FColor StoneHi(196, 190, 178), Stone(156, 150, 140), StoneLo(116, 110, 104), StoneDk(84, 80, 82);
	const FColor Mud(120, 92, 60), MudLo(92, 70, 48), MudHi(150, 118, 80);
	const FColor Shallow(96, 150, 138), Deep(36, 78, 86), OmutDeep(18, 40, 54), Foam(226, 240, 236);
	const FColor StreamSh(104, 176, 196), StreamDp(40, 96, 130);
	const FColor RoofC(118, 84, 58), Sack(222, 206, 170), SackLo(180, 160, 122);
	const FColor Rye(226, 188, 96), RyeLo(184, 142, 64), ReedC(150, 150, 72), ReedLo(108, 116, 52), Cattail(118, 72, 42);

	// ---------------------------------------------------------------------------------------- layout (texels)
	constexpr int32 W = 960, H = 400;
	constexpr int32 Shore = 236;                   // land above, the pond below
	constexpr int32 FarBank = 344;                 // the far bank starts (with a wobble)
	constexpr int32 WalkTop = 112;
	constexpr int32 DamX0 = 792, DamX1 = 810;      // the beaver dam across the pond
	constexpr int32 OmutX = 812;                   // the Omut: right of the dam
	constexpr int32 StreamX = 874, StreamHW = 11;  // the stream down to the Omut
	constexpr int32 BridgeY0 = 176, BridgeY1 = 194;
	constexpr int32 DeckX[FVMill::FeedDecks] = { 300, 430, 560 };
	constexpr int32 PenX = 200, LodgeX = 768, HouseX = 560;
	constexpr int32 MillX = 834, MillY = 150;
	constexpr int32 NetX[FVMill::NetSpots] = { 110, 365, 660, 915 };

	FFVRect Rc(float X, float Y, float RW, float RH)
	{
		const float S = FV::WorldScale;
		return FFVRect(X * S, Y * S, RW * S, RH * S);
	}
	FVector2D Pt(float X, float Y) { return FVector2D(X * FV::WorldScale, Y * FV::WorldScale); }

	FAnchor Make(EAnchor Kind, FName Id, float X, float Y, int32 Index = 0)
	{
		FAnchor A;
		A.Kind = Kind;
		A.Id = Id;
		A.Index = Index;
		A.Feet = Pt(X, Y);
		return A;
	}

	TArray<FAnchor> BuildZones()
	{
		TArray<FAnchor> Z;
		for (int32 d = 0; d < FVMill::FeedDecks; ++d)
		{
			FAnchor A = Make(EAnchor::MillZone, FVMill::ZoneDeckId(d), float(DeckX[d]), float(Shore - 2));
			A.Hit = Rc(float(DeckX[d] - 16), float(Shore - 26), 32.f, 27.f);
			Z.Add(A);
		}
		FAnchor O = Make(EAnchor::MillZone, FVMill::ZoneOmutId(), float(OmutX + 30), float(Shore - 2));
		O.Hit = Rc(float(OmutX), float(Shore - 30), float(W - 12 - OmutX), 31.f);
		Z.Add(O);
		return Z;
	}

	int32 StreamCX(int32 Y) { return StreamX + FMath::RoundToInt(FMath::Sin(float(Y) * 0.06f) * 2.f); }
	int32 FarY(int32 X) { return FarBank + FMath::RoundToInt(FMath::Sin(float(X) * 0.031f) * 3.f + VNoise(float(X) * 0.08f, 5.f, 91u) * 4.f); }

	// ---------------------------------------------------------------------------------------- small painters
	/** Fallen leaves: 1-2 texel specks of the autumn colours. */
	void Leaves(FFVImage& I, int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Count, uint32 Seed)
	{
		static const FColor Pal[5] = { LeafRed, LeafOrange, LeafGold, LeafRust, FColor(240, 210, 110) };
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 X = X0 + FMath::FloorToInt(R01(i, 1, Seed) * float(FMath::Max(1, X1 - X0)));
			const int32 Y = Y0 + FMath::FloorToInt(R01(i, 2, Seed) * float(FMath::Max(1, Y1 - Y0)));
			const FColor C = Pal[FMath::FloorToInt(R01(i, 3, Seed) * 4.99f)];
			I.Set(X, Y, C);
			if (R01(i, 4, Seed) < 0.5f) { I.Set(X + 1, Y, FVArt::Shade(C, 0.2f)); }
		}
	}
	/** A clump of reeds / cattails standing at (X, Y) (bottom). */
	void Reeds(FFVImage& I, int32 X, int32 Y, int32 N, uint32 Seed)
	{
		for (int32 k = 0; k < N; ++k)
		{
			const int32 RX = X + FMath::FloorToInt((R01(k, 1, Seed) - 0.5f) * float(N) * 2.4f);
			const int32 RH = 6 + FMath::FloorToInt(R01(k, 2, Seed) * 8.f);
			const int32 Lean = R01(k, 3, Seed) < 0.3f ? 1 : 0;
			for (int32 t = 0; t < RH; ++t)
			{
				const int32 PX = RX + (t > RH / 2 ? Lean : 0);
				I.Set(PX, Y - t, t < 2 ? ReedLo : (k % 2 ? ReedC : FVArt::Light(ReedC, 0.15f)));
			}
			if (R01(k, 4, Seed) < 0.4f)
			{
				const int32 PX = RX + Lean;
				I.Set(PX, Y - RH + 2, Cattail); I.Set(PX, Y - RH + 3, Cattail); I.Set(PX, Y - RH + 4, FVArt::Shade(Cattail, 0.2f));
				I.Set(PX, Y - RH + 1, ReedC);
			}
		}
	}
	/** A leafy autumn tree (maple / oak, 36x46) or a golden birch (26x50), feet bottom-centre. */
	FFVImage MakeAutumnTree(uint32 Seed, FColor Leaf, bool bBirch)
	{
		const int32 TW = bBirch ? 26 : 36, TH = bBirch ? 50 : 46;
		FFVImage T(TW, TH);
		const int32 CX = TW / 2;
		const int32 TrunkTop = bBirch ? 14 : 20;
		for (int32 Y = TrunkTop; Y < TH; ++Y)
		{
			const int32 HW = Y > TH - 4 ? 3 : 2;
			for (int32 X = CX - HW; X < CX + HW; ++X)
			{
				FColor C = bBirch ? BirchBark : Bark;
				if (X == CX - HW) { C = bBirch ? Rgb(250, 248, 240) : BarkHi; }
				else if (X == CX + HW - 1) { C = bBirch ? Rgb(196, 190, 180) : BarkLo; }
				if (bBirch && R01(X, Y / 2, Seed) < 0.22f) { C = BirchDash; }
				T.Set(X, Y, C);
			}
		}
		const FColor BrC = bBirch ? BirchBark : Bark;
		ThickLine(T, FVector2D(float(CX), float(TrunkTop + 8)), FVector2D(float(CX - 7), float(TrunkTop - 1)), 2.f, 1.f, BrC);
		ThickLine(T, FVector2D(float(CX), float(TrunkTop + 6)), FVector2D(float(CX + 6), float(TrunkTop - 3)), 2.f, 1.f, BrC);
		if (bBirch)
		{
			LeafBlob(T, float(CX - 4), 15.f, 7.f, 9.f, Leaf, Seed);
			LeafBlob(T, float(CX + 4), 12.f, 7.f, 9.f, Leaf, Seed + 3);
			LeafBlob(T, float(CX), 8.f, 7.f, 7.f, Leaf, Seed + 5);
		}
		else
		{
			LeafBlob(T, float(CX - 8), 21.f, 9.f, 8.f, Leaf, Seed);
			LeafBlob(T, float(CX + 8), 20.f, 9.f, 8.f, Leaf, Seed + 3);
			LeafBlob(T, float(CX), 12.f, 12.f, 11.f, Leaf, Seed + 5);
		}
		// other autumn colours among the leaves (a maple is never one colour)
		static const FColor Acc[3] = { LeafGold, LeafRed, FColor(244, 214, 120) };
		for (int32 k = 0; k < 34; ++k)
		{
			const int32 X = FMath::FloorToInt(R01(k, 1, Seed + 9) * float(TW));
			const int32 Y = FMath::FloorToInt(R01(k, 2, Seed + 9) * float(TrunkTop + 8));
			const FColor O = T.Get(X, Y);
			if (O.A == 0 || O == BrC) { continue; }
			T.Set(X, Y, Acc[k % 3]);
		}
		T.Outline(CrownOutline(Leaf));
		return T;
	}
	/** A dark spruce (the evergreen contrast of the autumn copse), 26 x Height. */
	FFVImage MakeSpruce(uint32 Seed, int32 Height)
	{
		const int32 TW = 26, TH = FMath::Clamp(Height, 30, 60);
		FFVImage T(TW, TH);
		const int32 CX = TW / 2;
		for (int32 Y = TH - 6; Y < TH; ++Y) { T.Set(CX - 1, Y, BarkHi); T.Set(CX, Y, Bark); T.Set(CX + 1, Y, BarkLo); }
		const int32 Tiers = 4;
		for (int32 t = 0; t < Tiers; ++t)
		{
			const int32 Y0 = 2 + t * (TH - 8) / Tiers, Y1 = FMath::Min(TH - 5, Y0 + (TH - 8) / Tiers + 5);
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				const float F = float(Y - Y0) / float(FMath::Max(1, Y1 - Y0));
				const int32 HW = FMath::RoundToInt((2.f + float(t) * 2.4f) * F + 1.f);
				for (int32 X = CX - HW; X <= CX + HW; ++X)
				{
					FColor C = SpruceC;
					if (X < CX - HW / 2) { C = LeafTone(SpruceC, 0); }
					else if (X > CX + HW / 2 || Y == Y1) { C = LeafTone(SpruceC, 2); }
					if (R01(X, Y, Seed) < 0.08f) { C = LeafTone(SpruceC, 3); }
					T.Set(X, Y, C);
				}
			}
		}
		T.Outline(CrownOutline(SpruceC));
		return T;
	}
	/** A rye sheaf (сноп), 10x14. */
	FFVImage MakeSheaf(uint32 Seed)
	{
		FFVImage T(10, 14);
		for (int32 Y = 0; Y < 14; ++Y)
		{
			const int32 HW = Y < 4 ? 3 + Y / 2 : (Y < 7 ? 3 : 2 + (Y - 7) / 3);
			for (int32 X = 5 - HW; X < 5 + HW; ++X)
			{
				FColor C = (X + Y) % 3 == 0 ? RyeLo : Rye;
				if (Y < 4 && R01(X, Y, Seed) < 0.4f) { C = FVArt::Light(Rye, 0.3f); }
				T.Set(X, Y, C);
			}
		}
		for (int32 X = 2; X <= 7; ++X) { T.Set(X, 7, WoodDk); }   // the twine
		T.Outline(PropOut);
		return T;
	}
	/** A flour sack, 10x11. */
	FFVImage MakeSack()
	{
		FFVImage T(10, 11);
		for (int32 Y = 2; Y < 11; ++Y)
		{
			for (int32 X = 1; X < 9; ++X) { T.Set(X, Y, X < 3 ? FVArt::Light(Sack, 0.2f) : (X > 6 || Y > 8 ? SackLo : Sack)); }
		}
		T.Set(3, 1, SackLo); T.Set(4, 0, Sack); T.Set(5, 0, Sack); T.Set(6, 1, SackLo); T.Set(4, 1, WoodDk); T.Set(5, 1, WoodDk);
		T.Set(4, 5, Rgb(170, 80, 50)); T.Set(5, 5, Rgb(170, 80, 50)); T.Set(4, 6, Rgb(170, 80, 50));   // a stamp
		T.Outline(PropOut);
		return T;
	}
	/** An old millstone leaning on its edge, 18x18. */
	FFVImage MakeOldMillstone()
	{
		FFVImage T(18, 18);
		RockBlob(T, 9.f, 9.f, 8.f, 8.f, Stone, 311u);
		FillEllipse(T, 9.f, 9.f, 2.f, 2.f, StoneDk, false);
		for (int32 k = 0; k < 6; ++k)
		{
			const float A = float(k) * UE_PI / 3.f;
			for (int32 r = 3; r < 7; ++r)
			{
				T.Set(FMath::RoundToInt(9.f + FMath::Cos(A + float(r) * 0.12f) * float(r)), FMath::RoundToInt(9.f + FMath::Sin(A + float(r) * 0.12f) * float(r)), StoneLo);
			}
		}
		for (int32 X = 5; X < 13; ++X) { T.Set(X, 16, Rgb(96, 128, 56)); }   // moss at the foot
		T.Outline(PropOut);
		return T;
	}
	/** A cart with sacks, 30x22. */
	FFVImage MakeCart()
	{
		FFVImage T(30, 22);
		for (int32 Y = 8; Y <= 13; ++Y) { for (int32 X = 2; X <= 25; ++X) { T.Set(X, Y, Y == 8 ? WoodHi : (Y == 13 ? WoodLo : Wood)); } }
		ThickLine(T, FVector2D(25.f, 11.f), FVector2D(29.f, 16.f), 1.f, 1.f, WoodLo);   // the shafts
		for (int32 w = 0; w < 2; ++w)
		{
			const float CX = w == 0 ? 7.f : 20.f;
			for (int32 Y = 12; Y < 22; ++Y)
			{
				for (int32 X = int32(CX) - 5; X <= int32(CX) + 5; ++X)
				{
					const float D = FMath::Sqrt(FMath::Square(float(X) + 0.5f - CX) + FMath::Square(float(Y) + 0.5f - 16.5f));
					if (D > 4.8f) { continue; }
					T.Set(X, Y, D > 3.6f ? WoodDk : (D < 1.2f ? StoneDk : ((X + Y) % 2 ? WoodLo : Wood)));
				}
			}
		}
		const FFVImage S = MakeSack();
		T.Blit(S, 3, 0, false); T.Blit(S, 13, 1, true);
		T.Outline(PropOut);
		return T;
	}
	/** A bench for the anglers, 22x12. */
	FFVImage MakeBench()
	{
		FFVImage I(22, 12);
		for (int32 X = 1; X <= 20; ++X) { I.Set(X, 4, WoodHi); I.Set(X, 5, Wood); I.Set(X, 6, WoodLo); I.Set(X, 0, Wood); I.Set(X, 1, WoodLo); }
		for (int32 Y = 2; Y <= 11; ++Y) { I.Set(3, Y, WoodDk); I.Set(18, Y, WoodDk); }
		I.Outline(PropOut);
		return I;
	}
	FFVImage MakeLantern()
	{
		FFVImage I(9, 30);
		for (int32 Y = 8; Y <= 29; ++Y) { I.Set(4, Y, Rgb(52, 46, 52)); }
		for (int32 X = 2; X <= 6; ++X) { I.Set(X, 29, Rgb(52, 46, 52)); I.Set(X, 1, Rgb(52, 46, 52)); }
		I.FillRect(2, 2, 5, 6, Rgb(255, 214, 120));
		I.Set(3, 3, Rgb(255, 246, 200)); I.Set(2, 2, Rgb(52, 46, 52)); I.Set(6, 2, Rgb(52, 46, 52)); I.Set(4, 7, Rgb(52, 46, 52));
		I.Outline(PropOut);
		return I;
	}

	/** The miller's house (baked at the top edge): logs, a dark plank roof with leaves, carved windows, a sheaf sign. 84x66. */
	FFVImage MakeHouse()
	{
		FFVImage I(84, 66);
		const FColor Frame(236, 226, 200);
		FVArtForestImpl::LogWall(I, 8, 75, 32, 57, 71u);
		for (int32 Y = 58; Y <= 65; ++Y) { for (int32 X = 5; X <= 78; ++X) { I.Set(X, Y, Y == 58 ? StoneHi : (((X + (Y & 1) * 3) % 7 == 0) ? StoneLo : Stone)); } }
		// door, porch steps
		for (int32 Y = 38; Y <= 57; ++Y) { for (int32 X = 36; X <= 47; ++X) { I.Set(X, Y, (X == 36 || X == 47 || Y == 38) ? WoodDk : ((X - 36) % 3 == 0 ? Rgb(98, 60, 36) : Rgb(122, 76, 46))); } }
		I.Set(45, 48, Rgb(236, 196, 92));
		for (int32 X = 32; X <= 51; ++X) { I.Set(X, 59, WoodHi); I.Set(X, 60, Wood); I.Set(X, 61, WoodHi); I.Set(X, 62, WoodLo); }
		FVArtForestImpl::CarvedWindow(I, 14, 40, 11, 10, Frame);
		FVArtForestImpl::CarvedWindow(I, 58, 40, 11, 10, Frame);
		// the chimney
		for (int32 Y = 2; Y <= 16; ++Y) { for (int32 X = 58; X <= 64; ++X) { I.Set(X, Y, Y <= 3 ? StoneDk : (X == 58 ? StoneHi : ((Y + X) % 3 == 0 ? StoneLo : Stone))); } }
		FVArtForestImpl::TesRoof(I, 1, 82, 6, 31, 14, RoofC);
		// fallen leaves on the roof
		for (int32 k = 0; k < 40; ++k)
		{
			const int32 X = 6 + FMath::FloorToInt(R01(k, 1, 73u) * 72.f), Y = 8 + FMath::FloorToInt(R01(k, 2, 73u) * 22.f);
			if (I.Get(X, Y).A == 0) { continue; }
			I.Set(X, Y, k % 3 == 0 ? LeafRed : (k % 3 == 1 ? LeafOrange : LeafGold));
		}
		// the sign above the door: a board with an ear of rye and a wheel
		I.FillRect(31, 22, 22, 8, Rgb(214, 170, 110));
		for (int32 X = 31; X < 53; ++X) { I.Set(X, 22, Rgb(236, 196, 140)); I.Set(X, 29, Rgb(150, 104, 62)); }
		for (int32 Y = 24; Y <= 28; ++Y) { I.Set(35, Y, RyeLo); I.Set(36, Y - 1, Rye); I.Set(34, Y - 1, Rye); }
		for (int32 k = 0; k < 8; ++k) { const float A = float(k) * UE_PI / 4.f; I.Set(FMath::RoundToInt(46.f + FMath::Cos(A) * 3.f), FMath::RoundToInt(25.5f + FMath::Sin(A) * 3.f), Rgb(96, 60, 36)); }
		I.Set(46, 25, Rgb(96, 60, 36));
		I.Outline(PropOut);
		return I;
	}

	struct FItem { FFVImage Img; int32 X = 0, Y = 0; bool bFlip = false; float Shadow = 0.f; };

	// =================================================================================================================
	// Object sprites
	// =================================================================================================================
	/** Mill: 124x104; building x 6..88, the wheel centre (104, 68) R 19 over the stream. */
	FFVImage BuildMillImg(int32 Stage, int32 Frame)
	{
		Stage = FMath::Clamp(Stage, 0, 3);
		FFVImage I(124, 104);
		const bool bWorking = Stage >= 3;
		const uint32 Seed = 401u;
		// ---- foundation (stones)
		for (int32 Y = 84; Y <= 101; ++Y)
		{
			for (int32 X = 6; X <= 88; ++X)
			{
				const int32 Row = (Y - 84) / 5, Off = (Row % 2) * 4;
				const bool bSeam = (Y - 84) % 5 == 4 || ((X + Off) % 9 == 0);
				FColor C = bSeam ? StoneDk : ((R01((X + Off) / 9, Row, Seed) < 0.5f) ? Stone : StoneLo);
				if (!bSeam && (Y - 84) % 5 == 0) { C = StoneHi; }
				if (X > 84) { C = Mul(C, 0.85f); }
				I.Set(X, Y, C);
			}
		}
		for (int32 X = 8; X <= 86; ++X) { if (R01(X, 1, Seed + 5) < 0.3f) { I.Set(X, 101, Rgb(96, 128, 56)); } }   // moss
		// ---- log walls
		FVArtForestImpl::LogWall(I, 10, 84, 44, 84, Seed + 1);
		if (Stage == 0)
		{
			// the upper right corner of the walls has fallen in
			for (int32 Y = 40; Y <= 70; ++Y)
			{
				for (int32 X = 60; X <= 88; ++X)
				{
					const float Edge = 44.f + float(X - 60) * 0.9f + (R01(X, Y / 3, Seed + 2) - 0.5f) * 3.f;
					if (float(Y) < Edge) { I.Set(X, Y, FColor(0, 0, 0, 0)); }
					else if (float(Y) < Edge + 1.5f) { I.Set(X, Y, Rgb(222, 180, 122)); }   // broken log ends
				}
			}
		}
		// ---- door
		for (int32 Y = 62; Y <= 84; ++Y)
		{
			for (int32 X = 38; X <= 50; ++X)
			{
				FColor C;
				if (Stage < 2) { C = Rgb(46, 34, 30); }
				else { C = (X == 38 || X == 50 || Y == 62) ? WoodDk : ((X - 38) % 3 == 0 ? Rgb(98, 60, 36) : Rgb(126, 80, 48)); }
				I.Set(X, Y, C);
			}
		}
		if (Stage < 2)
		{
			// boarded up: two crossed planks
			ThickLine(I, FVector2D(37.f, 66.f), FVector2D(51.f, 80.f), 2.f, 2.f, WoodOld);
			ThickLine(I, FVector2D(51.f, 66.f), FVector2D(37.f, 80.f), 2.f, 2.f, FVArt::Light(WoodOld, 0.15f));
		}
		else
		{
			I.Set(48, 74, Rgb(236, 196, 92));
			for (int32 X = 39; X <= 49; ++X) { I.Set(X, 66, Rgb(70, 64, 70)); I.Set(X, 80, Rgb(70, 64, 70)); }   // iron straps
		}
		// ---- window
		FVArtForestImpl::CarvedWindow(I, 62, 56, 11, 10, Rgb(226, 214, 190));
		if (Stage < 2)
		{
			for (int32 Y = 57; Y <= 64; ++Y) { for (int32 X = 63; X <= 71; ++X) { I.Set(X, Y, Rgb(40, 32, 30)); } }
			I.Set(64, 58, Rgb(150, 170, 190)); I.Set(65, 59, Rgb(150, 170, 190)); I.Set(70, 63, Rgb(150, 170, 190));   // shards
		}
		else if (bWorking)
		{
			for (int32 Y = 57; Y <= 64; ++Y) { for (int32 X = 63; X <= 71; ++X) { if (X != 67 && Y != 60) { I.Set(X, Y, Rgb(250, 212, 130)); } } }
		}
		// ---- roof (gable, dark planks); stages 0..2: holes with the rafters showing
		FVArtForestImpl::TesRoof(I, 2, 92, 8, 45, 22, RoofC);
		if (!bWorking)
		{
			const float Holes = Stage == 0 ? 0.48f : 0.56f;
			for (int32 Y = 9; Y <= 44; ++Y)
			{
				for (int32 X = 4; X <= 90; ++X)
				{
					if (I.Get(X, Y).A == 0) { continue; }
					if (Fbm(float(X) / 7.f, float(Y) / 6.f, Seed + 7) > Holes) { I.Set(X, Y, (X % 10 < 2) ? WoodLo : Rgb(44, 34, 32)); }
				}
			}
			if (Stage == 0)
			{
				// the roof slid down on the ruined side
				for (int32 Y = 8; Y <= 45; ++Y) { for (int32 X = 70; X <= 92; ++X) { if (float(Y) < 30.f + float(X - 70) * 0.6f) { I.Set(X, Y, FColor(0, 0, 0, 0)); } } }
			}
		}
		else
		{
			// leaves on the new roof, the hoist beam with a pulley under the ridge, a weather-cock
			for (int32 k = 0; k < 26; ++k)
			{
				const int32 X = 10 + FMath::FloorToInt(R01(k, 1, Seed + 8) * 72.f), Y = 12 + FMath::FloorToInt(R01(k, 2, Seed + 8) * 30.f);
				if (I.Get(X, Y).A != 0) { I.Set(X, Y, k % 2 ? LeafOrange : LeafGold); }
			}
			for (int32 X = 44; X <= 52; ++X) { I.Set(X, 6, WoodLo); I.Set(X, 7, WoodDk); }
			I.Set(52, 8, Rgb(70, 64, 70)); I.Set(52, 9, Rgb(70, 64, 70)); I.Set(51, 10, Rgb(150, 150, 160));
			const FColor Iron(52, 46, 52);
			I.Set(47, 5, Iron); I.Set(47, 4, Iron); I.Set(46, 3, Iron); I.Set(47, 3, Iron); I.Set(48, 3, Iron); I.Set(49, 2, Iron); I.Set(45, 2, Iron);
		}
		// ---- millstones by the wall (stage >= 2) or rubble (stage < 2)
		if (Stage >= 2)
		{
			for (int32 s = 0; s < 2; ++s)
			{
				const float CX = s == 0 ? 20.f : 28.f, CY = s == 0 ? 86.f : 90.f;
				RockBlob(I, CX, CY, 8.f, 8.f, s == 0 ? StoneLo : Stone, Seed + 20 + uint32(s));
				FillEllipse(I, CX, CY, 1.6f, 1.6f, StoneDk, false);
				for (int32 k = 0; k < 6; ++k)
				{
					const float A = float(k) * UE_PI / 3.f;
					I.Set(FMath::RoundToInt(CX + FMath::Cos(A) * 5.f), FMath::RoundToInt(CY + FMath::Sin(A) * 5.f), StoneDk);
				}
			}
			if (bWorking)
			{
				// sacks of flour at the door
				for (int32 s = 0; s < 2; ++s)
				{
					const int32 SX = 54 + s * 8, SY = 90 + (s % 2) * 2;
					for (int32 Y = SY; Y < SY + 9; ++Y) { for (int32 X = SX; X < SX + 7; ++X) { I.Set(X, Y, X == SX ? FVArt::Light(Sack, 0.2f) : (X == SX + 6 || Y == SY + 8 ? SackLo : Sack)); } }
					I.Set(SX + 3, SY - 1, SackLo);
				}
			}
		}
		else
		{
			for (int32 k = 0; k < 7; ++k) { RockBlob(I, 14.f + float(k) * 4.f + R01(k, 1, Seed) * 3.f, 94.f + R01(k, 2, Seed) * 5.f, 3.f, 2.5f, k % 2 ? Stone : StoneLo, Seed + 30 + uint32(k)); }
			ThickLine(I, FVector2D(8.f, 99.f), FVector2D(34.f, 88.f), 2.f, 2.f, WoodOld);   // a fallen beam
		}
		// ---- the flume (жёлоб) from the upper stream to the wheel
		if (Stage >= 1)
		{
			for (int32 X = 96; X <= 123; ++X)
			{
				const int32 Y = 44 + (123 - X) / 10;
				I.Set(X, Y, WoodHi); I.Set(X, Y + 1, Wood); I.Set(X, Y + 2, WoodLo);
				if (bWorking) { I.Set(X, Y, Rgb(150, 206, 226)); }
			}
			for (int32 Y = 47; Y <= 60; ++Y) { I.Set(120, Y, WoodDk); }   // a trestle
		}
		else
		{
			ThickLine(I, FVector2D(123.f, 44.f), FVector2D(110.f, 52.f), 3.f, 3.f, WoodOld);   // the broken flume hangs
		}
		// ---- the water wheel
		const float WCX = 104.f, WCY = 68.f, WR = 19.f;
		const float Ang0 = float(Frame & 3) * (UE_PI / 16.f);   // 4 frames x 11.25 deg = one spoke period (45 deg)
		for (int32 Y = FMath::FloorToInt(WCY - WR - 3.f); Y <= FMath::CeilToInt(WCY + WR + 3.f); ++Y)
		{
			for (int32 X = FMath::FloorToInt(WCX - WR - 3.f); X <= FMath::CeilToInt(WCX + WR + 3.f); ++X)
			{
				const float DX = float(X) + 0.5f - WCX, DY = float(Y) + 0.5f - WCY;
				const float R = FMath::Sqrt(DX * DX + DY * DY);
				float A = FMath::Atan2(DY, DX) - Ang0;
				while (A < 0.f) { A += UE_TWO_PI; }
				if (Stage == 0 && A > 0.4f && A < 3.6f) { continue; }   // half of the wheel is gone
				if (R >= WR - 3.f && R <= WR)
				{
					const bool bLit = DX + DY < 0.f;
					I.Set(X, Y, R > WR - 1.f ? WoodDk : (bLit ? WoodHi : Wood));
					continue;
				}
				// 16 paddles sticking out of the rim
				const float PA = FMath::Fmod(A, UE_TWO_PI / 16.f);
				if (R > WR && R <= WR + 3.f && (PA < 0.12f || PA > UE_TWO_PI / 16.f - 0.05f))
				{
					if (Stage == 0 && R01(FMath::FloorToInt(A * 16.f / UE_TWO_PI), 1, Seed) < 0.5f) { continue; }
					I.Set(X, Y, WoodLo);
					continue;
				}
				// 8 spokes
				const float SA = FMath::Fmod(A, UE_TWO_PI / 8.f);
				if (R < WR - 3.f && R > 2.5f && (SA < 0.09f || SA > UE_TWO_PI / 8.f - 0.09f)) { I.Set(X, Y, R < 6.f ? WoodLo : Wood); continue; }
				if (R <= 2.8f) { I.Set(X, Y, R < 1.2f ? Rgb(150, 150, 160) : Rgb(70, 64, 70)); }   // the iron hub
			}
		}
		if (Stage == 0)
		{
			// wheel fragments lying on the bank
			ThickLine(I, FVector2D(92.f, 100.f), FVector2D(110.f, 96.f), 2.f, 2.f, WoodOld);
			ThickLine(I, FVector2D(100.f, 102.f), FVector2D(106.f, 92.f), 1.f, 1.f, WoodLo);
		}
		for (int32 X = 85; X <= 101; ++X) { I.Set(X, 67, Rgb(70, 64, 70)); I.Set(X, 68, Rgb(52, 46, 52)); }   // the axle
		I.Outline(PropOut);
		// ---- water (after the outline): falling from the flume, down the wheel, foam at the bottom
		if (bWorking)
		{
			const FColor Wt(150, 210, 232, 230), WtHi(226, 246, 250, 240);
			for (int32 Y = 47; Y <= 52; ++Y) { I.Set(96, Y, Wt); I.Set(97, Y, (Y + Frame) % 3 == 0 ? WtHi : Wt); }
			for (int32 k = 0; k < 14; ++k)
			{
				const float A = -1.2f + float(k) * 0.18f;
				const int32 X = FMath::RoundToInt(WCX + FMath::Cos(A) * (WR + 2.f)), Y = FMath::RoundToInt(WCY + FMath::Sin(A) * (WR + 2.f));
				I.Blend(X, Y, (k + Frame) % 3 == 0 ? WtHi : Wt);
				I.Blend(X + 1, Y, WithA(Wt, 150));
			}
			for (int32 k = 0; k < 16; ++k)
			{
				const int32 X = 88 + FMath::FloorToInt(R01(k, Frame, Seed + 50) * 34.f), Y = 88 + FMath::FloorToInt(R01(k, Frame + 7, Seed + 50) * 12.f);
				I.Blend(X, Y, WithA(Foam, 220)); I.Blend(X + 1, Y, WithA(Foam, 150));
			}
			for (int32 k = 0; k < 5; ++k) { I.Blend(118 - k * 3 + (Frame & 1), 84 - ((k + Frame) % 4) * 2, WithA(WtHi, 200)); }   // spray
		}
		return I;
	}

	/** Beaver lodge + dam: 64x112; the lodge dome (18, 26), the dam x ~40..58 over all 112 rows (bank to bank). */
	FFVImage BuildBeaverImg(int32 Stage, int32 Frame)
	{
		Stage = FMath::Clamp(Stage, 0, FVMill::BeaverFeedsNeeded);
		FFVImage I(64, 112);
		const uint32 Seed = 501u;
		const int32 Fill = Stage * 11 + 4;   // rows filled from each bank
		const bool bDone = Stage >= FVMill::BeaverFeedsNeeded;
		auto InDam = [Fill, bDone, Seed](int32 X, int32 Y) -> bool
		{
			if (Y < 0 || Y > 111) { return false; }
			if (!bDone && Y >= Fill && Y < 112 - Fill) { return false; }
			const float X0 = 40.f + float(Y) * 0.03f + (R01(1, Y / 2, Seed) - 0.5f) * 2.f;
			const float X1 = X0 + 16.f + (R01(2, Y / 2, Seed) - 0.5f) * 3.f;
			// the unfinished ends are ragged
			if (!bDone && (FMath::Abs(Y - Fill) < 3 || FMath::Abs(Y - (112 - Fill)) < 3) && R01(X, Y, Seed + 3) < 0.45f) { return false; }
			return float(X) >= X0 && float(X) <= X1;
		};
		for (int32 Y = 0; Y < 112; ++Y)
		{
			for (int32 X = 34; X < 64; ++X)
			{
				if (!InDam(X, Y)) { continue; }
				const float V = Fbm(float(X) / 3.f, float(Y) / 3.f, Seed + 5);
				I.Set(X, Y, V > 0.6f ? MudHi : (V < 0.4f ? MudLo : Mud));
			}
		}
		// sticks across the dam
		for (int32 k = 0; k < 150; ++k)
		{
			const int32 X = 38 + FMath::FloorToInt(R01(k, 1, Seed + 7) * 22.f), Y = FMath::FloorToInt(R01(k, 2, Seed + 7) * 112.f);
			if (!InDam(X, Y)) { continue; }
			const float A = (R01(k, 3, Seed + 7) - 0.5f) * 1.4f;
			const float L = 4.f + R01(k, 4, Seed + 7) * 6.f;
			const FColor C = k % 3 == 0 ? WoodHi : (k % 3 == 1 ? Rgb(150, 112, 74) : WoodDk);
			for (int32 t = 0; t < int32(L); ++t)
			{
				const int32 PX = X + FMath::RoundToInt(FMath::Cos(A) * float(t)), PY = Y + FMath::RoundToInt(FMath::Sin(A) * float(t));
				if (InDam(PX, PY)) { I.Set(PX, PY, C); }
			}
		}
		// the lodge: a dome of sticks and mud
		for (int32 Y = 8; Y <= 38; ++Y)
		{
			for (int32 X = 0; X <= 36; ++X)
			{
				const float DX = (float(X) + 0.5f - 18.f) / 17.f, DY = (float(Y) + 0.5f - 26.f) / 13.f;
				const float D = DX * DX + DY * DY;
				if (D > 1.f - 0.15f * R01(X / 2, Y / 2, Seed + 9)) { continue; }
				const float L = -(DX * 0.55f + DY * 0.83f);
				FColor C = L > 0.4f ? MudHi : (L > -0.2f ? Mud : MudLo);
				if (R01(X, Y, Seed + 11) < 0.3f) { C = ((X + Y) % 3 == 0) ? WoodHi : Rgb(150, 112, 74); }
				I.Set(X, Y, C);
			}
		}
		for (int32 k = 0; k < 40; ++k)   // sticks poking out of the lodge
		{
			const float A = R01(k, 1, Seed + 13) * UE_TWO_PI;
			const float R0 = 9.f + R01(k, 2, Seed + 13) * 5.f;
			for (int32 t = 0; t < 4; ++t)
			{
				I.Set(FMath::RoundToInt(18.f + FMath::Cos(A) * (R0 + float(t)) * 1.2f), FMath::RoundToInt(26.f + FMath::Sin(A) * (R0 + float(t)) * 0.8f), k % 2 ? WoodDk : WoodLo);
			}
		}
		// a beaver: on top of the lodge while building, on the finished dam afterwards
		{
			const int32 BX = bDone ? 46 : 14, BY = bDone ? 54 : 11;
			const FColor Fur(120, 78, 48), FurHi(160, 110, 70), Tail(70, 52, 44);
			for (int32 Y = 0; Y < 5; ++Y) { for (int32 X = 0; X < 6; ++X) { if ((X == 0 || X == 5) && (Y == 0 || Y == 4)) { continue; } I.Set(BX + X, BY + Y, Y < 2 ? FurHi : Fur); } }
			I.Set(BX + 6, BY + 1, Fur); I.Set(BX + 7, BY + 1, Fur); I.Set(BX + 6, BY + 2, Fur); I.Set(BX + 7, BY + 2, Rgb(40, 28, 30));   // head + nose
			I.Set(BX + 6, BY, Rgb(90, 58, 40));            // ear
			I.Set(BX + 7, BY + 3, Rgb(250, 240, 220));     // the teeth
			for (int32 Y = 2; Y < 6; ++Y) { I.Set(BX - 1, BY + Y, Tail); I.Set(BX - 2, BY + Y, Tail); }
			I.Set(BX + 6, BY + 1 + (Frame & 1), Rgb(24, 20, 24));   // the eye
		}
		I.Outline(PropOut);
		// water rings around the lodge and foam along the finished dam (after the outline)
		for (int32 X = 2; X <= 34; ++X)
		{
			const int32 Y = 40 + FMath::RoundToInt(FMath::Sin(float(X) * 0.5f + float(Frame)) * 0.6f);
			if (R01(X, Frame, Seed + 17) < 0.7f) { I.Blend(X, Y, FColor(220, 240, 240, 120)); }
		}
		if (bDone)
		{
			for (int32 Y = 2; Y < 110; Y += 2)
			{
				const int32 X = 59 + FMath::RoundToInt(float(Y) * 0.03f) + ((Y / 2 + Frame) % 3 == 0 ? 1 : 0);
				I.Blend(X, Y, FColor(236, 248, 250, 170));
			}
		}
		return I;
	}

	/** Feeder deck, 34x30: a plank platform at the bank; baited = a bucket and crumbs / bubbles on the water. */
	FFVImage BuildDeckImg(int32 State, int32 Frame)
	{
		FFVImage I(34, 30);
		for (int32 Y = 0; Y <= 17; ++Y)
		{
			for (int32 X = 3; X <= 30; ++X)
			{
				const int32 R4 = Y % 4;
				FColor C = R4 == 3 ? WoodDk : (R4 == 0 ? WoodHi : Wood);
				if (Y >= 16) { C = WoodLo; }
				if (R01(X / 5, Y / 4, 601u) < 0.2f && R4 != 3) { C = FVArt::Shade(C, 0.12f); }
				if (X == 3) { C = FVArt::Light(C, 0.15f); } else if (X == 30) { C = FVArt::Shade(C, 0.25f); }
				I.Set(X, Y, C);
			}
		}
		for (int32 P = 0; P < 2; ++P) { const int32 X = P == 0 ? 5 : 27; for (int32 Y = 18; Y <= 23; ++Y) { I.Set(X, Y, WoodDk); I.Set(X + 1, Y, BarkLo); } }
		if (State > 0)
		{
			// a bucket of groundbait on the deck
			for (int32 Y = 4; Y <= 11; ++Y) { for (int32 X = 21; X <= 27; ++X) { I.Set(X, Y, Y == 4 ? Rgb(200, 170, 110) : (X == 21 ? Rgb(150, 150, 160) : (X == 27 ? Rgb(90, 90, 104) : Rgb(120, 120, 134)))); } }
			for (int32 X = 22; X <= 26; ++X) { I.Set(X, 3, Rgb(70, 64, 70)); }
		}
		else
		{
			FillEllipse(I, 23.f, 8.f, 3.f, 2.f, Rgb(200, 170, 110), false);   // a coil of rope
			I.Set(23, 8, Rgb(150, 120, 70));
		}
		I.Outline(PropOut);
		for (int32 P = 0; P < 2; ++P) { const int32 X = P == 0 ? 5 : 27; I.Blend(X - 1, 24, FColor(220, 240, 240, 120)); I.Blend(X + 2, 24, FColor(220, 240, 240, 120)); }
		if (State > 0)
		{
			for (int32 k = 0; k < 14; ++k)   // crumbs
			{
				const int32 X = 4 + FMath::FloorToInt(R01(k, 1, 603u) * 26.f), Y = 19 + FMath::FloorToInt(R01(k, 2, 603u) * 10.f);
				I.Blend(X, Y, FColor(236, 206, 140, 220));
			}
			for (int32 k = 0; k < 5; ++k)    // bubbles of feeding fish
			{
				const int32 X = 6 + FMath::FloorToInt(R01(k, 3 + Frame, 605u) * 22.f), Y = 20 + FMath::FloorToInt(R01(k, 5 + Frame, 605u) * 8.f);
				I.Blend(X, Y, FColor(240, 250, 250, 200)); I.Blend(X + 1, Y - 1, FColor(240, 250, 250, 110));
			}
		}
		return I;
	}

	/** Fish pen, 52x36: stakes, net walls with floats, a plank from the bank; State = species inside. */
	FFVImage BuildPenImg(int32 State, int32 Frame)
	{
		FFVImage I(52, 36);
		const FColor NetC(226, 214, 180, 170), NetD(180, 166, 130, 150);
		for (int32 Y = 8; Y <= 31; ++Y) { for (int32 X = 4; X <= 47; ++X) { I.Blend(X, Y, FColor(10, 40, 50, 60)); } }   // darker water inside
		auto Mesh = [&I, NetC, NetD](int32 X0, int32 Y0, int32 X1, int32 Y1)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y) { for (int32 X = X0; X <= X1; ++X) { if ((X + Y) % 2 == 0) { I.Blend(X, Y, (X / 2 + Y) % 3 == 0 ? NetD : NetC); } } }
		};
		Mesh(3, 5, 48, 8);     // the back wall
		Mesh(3, 5, 5, 32);     // left
		Mesh(46, 5, 48, 32);   // right
		Mesh(3, 29, 48, 32);   // the front wall
		for (int32 f = 0; f < FMath::Clamp(State, 0, 3); ++f)   // fish inside
		{
			const int32 FX = 12 + f * 11 + ((Frame + f) % 2) * 3, FY = 14 + (f % 2) * 7;
			const FColor Fc(50, 74, 76, 200);
			for (int32 X = 0; X < 7; ++X) { I.Blend(FX + X, FY, Fc); if (X > 0 && X < 5) { I.Blend(FX + X, FY + 1, Fc); } }
			I.Blend(FX - 1, FY - 1, Fc); I.Blend(FX - 1, FY + 1, Fc);
			I.Blend(FX + 3, FY - 2 + (Frame & 1), FColor(230, 245, 245, 150));
		}
		const int32 SX[4] = { 3, 47, 3, 47 }, SY[4] = { 0, 0, 22, 22 };   // stakes at the corners
		for (int32 s = 0; s < 4; ++s)
		{
			for (int32 Y = SY[s]; Y <= SY[s] + 11; ++Y) { I.Set(SX[s], Y, WoodLo); I.Set(SX[s] + 1, Y, WoodDk); }
			I.Set(SX[s], SY[s], WoodHi);
		}
		for (int32 X = 4; X <= 46; ++X)   // rope with floats along the top of the walls
		{
			I.Set(X, 5, Rgb(200, 180, 130)); I.Set(X, 29, Rgb(200, 180, 130));
			if (X % 7 == 0) { I.Set(X, 4, Rgb(236, 90, 70)); I.Set(X, 28, Rgb(250, 246, 240)); }
		}
		for (int32 Y = 0; Y <= 5; ++Y) { for (int32 X = 22; X <= 29; ++X) { I.Set(X, Y, Y % 3 == 2 ? WoodLo : Wood); } }   // the plank from the bank
		I.Outline(PropOut);
		return I;
	}

	/** The forest footbridge over the brook to the mill, 26x32 (the path runs up; planks across). */
	FFVImage BuildBridgeImg(int32 State)
	{
		FFVImage I(26, 32);
		const bool bFixed = State > 0;
		const FColor PlankC = bFixed ? WoodHi : WoodOld, PlankLo = bFixed ? Wood : FVArt::Shade(WoodOld, 0.2f);
		for (int32 Y = 0; Y < 32; ++Y)   // the side beams (the right one snapped while broken)
		{
			I.Set(3, Y, BarkHi); I.Set(4, Y, Bark); I.Set(5, Y, BarkLo);
			if (bFixed || Y < 13 || Y > 19) { I.Set(20, Y, BarkHi); I.Set(21, Y, Bark); I.Set(22, Y, BarkLo); }
		}
		for (int32 p = 0; p < 8; ++p)
		{
			const int32 Y0 = 1 + p * 4;
			if (!bFixed && p >= 2 && p <= 5 && p != 3) { continue; }   // missing planks
			for (int32 Y = Y0; Y < Y0 + 3; ++Y)
			{
				for (int32 X = 2; X <= 23; ++X)
				{
					if (!bFixed && p == 3 && X > 14) { continue; }   // a snapped plank
					I.Set(X, Y, Y == Y0 ? FVArt::Light(PlankC, 0.15f) : (Y == Y0 + 2 ? PlankLo : PlankC));
				}
			}
			I.Set(4, Y0 + 1, Rgb(84, 78, 80)); I.Set(21, Y0 + 1, Rgb(84, 78, 80));   // nails
		}
		if (bFixed)
		{
			for (int32 s = 0; s < 2; ++s)   // rope handrails on short posts
			{
				const int32 X = s == 0 ? 0 : 25;
				for (int32 Y = 0; Y < 32; ++Y) { I.Set(X, Y, Y % 10 < 2 ? WoodDk : Rgb(206, 180, 120)); }
			}
		}
		else
		{
			ThickLine(I, FVector2D(9.f, 13.f), FVector2D(19.f, 17.f), 2.f, 2.f, WoodOld);   // a plank floating askew
		}
		I.Outline(PropOut);
		return I;
	}
}

// the arrow sign is shared with the forest side of the mill (FVArtForest.cpp declares it)
namespace FVArtMillImpl
{
	/** An arrow sign on a post, 20x22. Dir 0 = up «К мельнице» (a small wheel), 1 = right «В лес» (a maple leaf). */
	FFVImage MakeSign(int32 Dir)
	{
		FFVImage I(20, 22);
		const FColor BoardC(214, 170, 110);
		for (int32 Y = 9; Y <= 21; ++Y) { I.Set(9, Y, Bark); I.Set(10, Y, BarkLo); }
		for (int32 X = 2; X <= 16; ++X) { for (int32 Y = 3; Y <= 8; ++Y) { I.Set(X, Y, Y == 3 ? Rgb(236, 196, 140) : (Y == 8 ? Rgb(170, 124, 76) : BoardC)); } }
		if (Dir == 1) { for (int32 k = 0; k < 4; ++k) { for (int32 Y = 5 - k; Y <= 6 + k; ++Y) { I.Set(19 - k, Y, BoardC); } } }
		else { for (int32 k = 0; k < 3; ++k) { for (int32 X = 9 - k; X <= 10 + k; ++X) { I.Set(X, k, BoardC); } } }
		if (Dir == 0)
		{
			for (int32 k = 0; k < 8; ++k) { const float A = float(k) * UE_PI / 4.f; I.Set(FMath::RoundToInt(6.f + FMath::Cos(A) * 2.f), FMath::RoundToInt(5.5f + FMath::Sin(A) * 2.f), Rgb(96, 60, 36)); }
		}
		else
		{
			I.Set(5, 5, LeafRed); I.Set(6, 4, LeafRed); I.Set(7, 5, LeafRed); I.Set(6, 5, LeafOrange); I.Set(6, 6, LeafRed); I.Set(6, 7, BarkLo);
		}
		for (int32 X = 10; X <= 14; X += 2) { I.Set(X, 5, Rgb(90, 60, 40)); I.Set(X, 6, Rgb(90, 60, 40)); }
		I.Outline(PropOut);
		return I;
	}
}

// ============================================================================================================
// FVWorldU6 entry points
// ============================================================================================================
const TArray<FVWorldU6::FAnchor>& FVWorldU6::MillZones()
{
	static const TArray<FAnchor> Z = FVArtMillImpl::BuildZones();
	return Z;
}

void FVWorldU6::BuildMill(FFVImage& OutBg, FWorldDef& OutDef)
{
	using namespace FVArtMillImpl;
	FFVImage& I = OutBg;
	I.Init(W, H, FColor(0, 0, 0, 255));

	// ---- 1) the ground: autumn grass, dithered 3 tones, green patches, darker under the canopy / on the far bank
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float V = Fbm(float(X) / 15.f, float(Y) / 15.f, 111u) + (Bayer(X, Y) - 0.5f) * 0.1f;
			FColor C = V < 0.4f ? GrassLo : (V > 0.6f ? GrassHi : GrassBase);
			const float S = R01(X, Y, 113u);
			if (S < 0.012f) { C = FVArt::Shade(C, 0.25f); }
			else if (S > 0.992f) { C = FVArt::Light(C, 0.22f); }
			if (VNoise(float(X) / 30.f, float(Y) / 24.f, 117u) > 0.66f) { C = FVArt::Mix(C, Rgb(110, 138, 58), 0.35f); }
			if (Y < WalkTop - 4 || Y > FarBank) { C = Mul(C, 0.86f); }
			I.Set(X, Y, C);
		}
	}

	// ---- 2) paths: from the exit over the bridge along the bank; to the house, to the mill door, to the decks and the pen
	PaintPath(I, { FVector2D(960.f, 186.f), FVector2D(900.f, 185.f), FVector2D(846.f, 188.f), FVector2D(780.f, 196.f), FVector2D(700.f, 192.f),
		FVector2D(620.f, 178.f), FVector2D(540.f, 184.f), FVector2D(460.f, 194.f), FVector2D(380.f, 188.f), FVector2D(300.f, 192.f),
		FVector2D(220.f, 190.f), FVector2D(140.f, 182.f), FVector2D(70.f, 186.f) }, 13.f, 121u);
	PaintPath(I, { FVector2D(610.f, 180.f), FVector2D(580.f, 150.f), FVector2D(562.f, 118.f), FVector2D(560.f, 104.f) }, 10.f, 123u);
	PaintPath(I, { FVector2D(790.f, 194.f), FVector2D(806.f, 172.f), FVector2D(816.f, 154.f) }, 10.f, 125u);
	for (int32 d = 0; d < FVMill::FeedDecks; ++d) { PaintPath(I, { FVector2D(float(DeckX[d] - 6), 190.f), FVector2D(float(DeckX[d]), float(Shore - 4)) }, 8.f, 127u + uint32(d)); }
	PaintPath(I, { FVector2D(float(PenX + 10), 190.f), FVector2D(float(PenX), float(Shore - 4)) }, 8.f, 131u);

	// ---- 3) grass, fallen leaves, a few late flowers
	Tufts(I, 34, WalkTop - 6, W - 12, Shore - 6, 520, TuftC, 141u);
	Tufts(I, 0, 30, W, WalkTop - 6, 260, Mul(TuftC, 0.9f), 143u);
	Leaves(I, 20, 20, W - 8, Shore - 3, 2200, 145u);
	Flowers(I, 40, WalkTop, W - 40, Shore - 12, 50, 147u);

	// ---- 4) the pond: shallow at the banks, deep in the middle; the Omut right of the dam; the far bank
	for (int32 X = 0; X < W; ++X)
	{
		const int32 FY = FarY(X);
		for (int32 Y = Shore; Y < FY; ++Y)
		{
			const float T = float(Y - Shore) / float(FMath::Max(1, FY - Shore));
			const float Dp = FMath::Pow(FMath::Sin(T * UE_PI), 0.7f);
			FColor C = FVArt::Mix(Shallow, Deep, Dp * 0.9f + (Bayer(X, Y) - 0.5f) * 0.08f);
			if (X >= OmutX - 40)
			{
				// the Omut: much deeper, a slow whirl from the wheel (integration: a dithered 40-texel smooth slope into it,
				// no hard vertical edge across the pond while the dam is not built; the dam x 792..810 covers the steep part)
				const float DX = float(X) - 880.f, DY = (float(Y) - 290.f) * 1.4f;
				const float R = FMath::Sqrt(DX * DX + DY * DY);
				const float Edge = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(float(X - (OmutX - 40)) / 40.f + (Bayer(X, Y) - 0.5f) * 0.12f, 0.f, 1.f));
				C = FVArt::Mix(C, FVArt::Mix(Deep, OmutDeep, FMath::Clamp(0.5f + Dp * 0.6f, 0.f, 1.f)), Edge);
				const float Sw = FMath::Sin(R * 0.45f - FMath::Atan2(DY, DX) * 3.f);
				if (Sw > 0.94f && R < 60.f) { C = FVArt::Mix(C, Rgb(80, 128, 138), 0.35f * (1.f - R / 60.f) + 0.1f); }
			}
			// reflections of the autumn crowns near the far bank, glints
			if (Y > FY - 14 && R01(X / 3, 1, 151u) < 0.6f) { C = FVArt::Mix(C, (X / 9) % 2 ? LeafOrange : LeafGold, 0.18f * float(Y - (FY - 14)) / 14.f); }
			if (R01(X / 4, Y, 153u) < 0.012f) { C = FVArt::Light(C, 0.35f); }
			if (Y < Shore + 2) { C = FVArt::Mix(C, MudHi, 0.35f); }
			I.Set(X, Y, C);
		}
		for (int32 Y = Shore - 4; Y < Shore; ++Y)   // the near bank edge (mud + pebbles)
		{
			FColor C = Y == Shore - 1 ? MudLo : (R01(X, Y, 155u) < 0.5f ? Mud : MudHi);
			if (R01(X, Y, 157u) < 0.08f) { C = StoneHi; }
			I.Set(X, Y, C);
		}
		for (int32 Y = FY; Y < FY + 3; ++Y) { I.Set(X, Y, Y == FY ? MudLo : Mud); }   // the far bank edge
	}
	for (int32 k = 0; k < 260; ++k)   // floating leaves
	{
		const int32 X = FMath::FloorToInt(R01(k, 1, 161u) * float(W)), Y = Shore + 3 + FMath::FloorToInt(R01(k, 2, 161u) * 100.f);
		if (Y >= FarY(X) - 1) { continue; }
		const FColor C = k % 3 == 0 ? LeafRed : (k % 3 == 1 ? LeafOrange : LeafGold);
		I.Set(X, Y, C); I.Set(X + 1, Y, FVArt::Shade(C, 0.2f));
	}
	for (int32 X = 8; X < W - 8; X += 11)   // reeds along both banks (not at the objects / net posts)
	{
		bool bFree = FMath::Abs(X - PenX) > 34 && FMath::Abs(X - LodgeX) > 30 && FMath::Abs(X - (DamX0 + DamX1) / 2) > 16 && FMath::Abs(X - StreamX) > 18;
		for (int32 d = 0; d < FVMill::FeedDecks; ++d) { bFree = bFree && FMath::Abs(X - DeckX[d]) > 24; }
		for (int32 n = 0; n < FVMill::NetSpots; ++n) { bFree = bFree && FMath::Abs(X - NetX[n]) > 18; }
		if (bFree && R01(X, 3, 163u) < 0.45f) { Reeds(I, X, Shore + 6 + FMath::FloorToInt(R01(X, 4, 163u) * 3.f), 4, 165u + uint32(X)); }
		Reeds(I, X + 4, FarY(X) + 2, 5, 167u + uint32(X));
	}

	// ---- 5) the stream from the top down into the Omut, with a plank bridge on the path
	for (int32 Y = 0; Y < Shore + 2; ++Y)
	{
		const int32 CX = StreamCX(Y);
		for (int32 X = CX - StreamHW - 2; X <= CX + StreamHW + 2; ++X)
		{
			const int32 D = FMath::Abs(X - CX);
			if (D > StreamHW) { I.Set(X, Y, D == StreamHW + 1 ? MudLo : Mud); continue; }
			const float T = float(D) / float(StreamHW);
			FColor C = FVArt::Mix(StreamDp, StreamSh, T * T);
			if (R01(X, Y / 3, 171u) < 0.06f) { C = FVArt::Light(StreamSh, 0.5f); }
			if (Y > 120 && Y < 160 && R01(X, Y, 173u) < 0.25f) { C = Foam; }   // foam below the wheel
			I.Set(X, Y, C);
		}
	}
	for (int32 Y = BridgeY0; Y <= BridgeY1; ++Y)
	{
		for (int32 X = StreamX - StreamHW - 5; X <= StreamX + StreamHW + 5; ++X)
		{
			const int32 Board = (X - (StreamX - StreamHW - 5)) / 3;
			FColor C = (X - (StreamX - StreamHW - 5)) % 3 == 2 ? WoodLo : (Board % 2 ? Wood : WoodHi);
			if (Y == BridgeY0 || Y == BridgeY1) { C = WoodDk; }
			if (R01(Board, Y / 5, 175u) < 0.15f) { C = FVArt::Shade(C, 0.15f); }
			I.Set(X, Y, C);
		}
	}
	for (int32 X = StreamX - StreamHW - 5; X <= StreamX + StreamHW + 5; ++X) { MulPx(I, X, BridgeY1 + 1, 0.65f); MulPx(I, X, BridgeY1 + 2, 0.8f); }

	TArray<FItem> Items;
	auto Add = [&Items](FFVImage&& Img, int32 X, int32 Y, bool bFlip, float Shadow)
	{
		FItem It; It.Img = MoveTemp(Img); It.X = X; It.Y = Y; It.bFlip = bFlip; It.Shadow = Shadow;
		Items.Add(MoveTemp(It));
	};
	const FColor Crowns[4] = { LeafOrange, LeafRed, LeafGold, LeafRust };

	// ---- 6) the canopy along the top: dark far crowns, then rows of autumn trees (feet y 52..104)
	for (int32 X = 0; X < W; X += 7)
	{
		LeafBlob(I, float(X) + R01(X, 1, 181u) * 4.f, 8.f + R01(X, 2, 181u) * 14.f, 11.f, 12.f, Mul(Crowns[(X / 7) % 4], 0.62f), 800u + uint32(X));
	}
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 X = 18 + Row * 13; X < W - 4; X += 28 + Row * 3)
		{
			if (Row > 0 && X > 506 && X < 700) { continue; }   // keep the house and the rye patch clear
			if (FMath::Abs(X - StreamX) < 20) { continue; }    // the stream
			const float Rn = R01(X, Row, 183u);
			const int32 Y = 52 + Row * 22 + FMath::FloorToInt(R01(X, 9, 183u) * 6.f);
			if (Rn < 0.2f) { Add(MakeSpruce(820u + uint32(X), 46 + FMath::FloorToInt(Rn * 40.f)), X, Y, false, 7.f); }
			else if (Rn < 0.42f) { Add(MakeAutumnTree(840u + uint32(X), LeafGold, true), X, Y, Rn > 0.3f, 6.f); }
			else { Add(MakeAutumnTree(860u + uint32(X), Crowns[FMath::FloorToInt(Rn * 7.f) % 4], false), X, Y, Rn > 0.7f, 8.f); }
		}
	}
	for (int32 Y = 110; Y < FarBank + 20; Y += 20)   // the left edge: a wall of trees and rocks (x 0..34)
	{
		const float Rn = R01(Y, 11, 185u);
		if (Y < Shore - 6) { Add(Rn < 0.4f ? MakeSpruce(870u + uint32(Y), 52) : MakeAutumnTree(872u + uint32(Y), Crowns[(Y / 20) % 4], false), 10 + ((Y / 20) % 2) * 10, Y, false, 6.f); }
		else { Add(MakeRock(874u + uint32(Y), 9, 6, Stone, true), 12, Y, false, 0.f); }
	}
	for (int32 Y = 212; Y < Shore; Y += 14) { Add(MakeBush(880u + uint32(Y), Rgb(170, 120, 50), 7, Y % 28 == 0), 952, Y, false, 4.f); }   // right edge below the exit
	for (int32 Y = 112; Y < 168; Y += 18) { Add(MakeAutumnTree(884u + uint32(Y), Crowns[(Y / 18) % 4], false), 950, Y, false, 6.f); }   // ... and above it
	for (int32 X = 10; X < W - 6; X += 16)   // the far bank: bushes, ferns ...
	{
		const float Rn = R01(X, 13, 187u);
		const int32 Y = FarY(X) + 14 + FMath::FloorToInt(R01(X, 14, 187u) * 14.f);
		if (Rn < 0.5f) { Add(MakeBush(900u + uint32(X), Rn < 0.25f ? Rgb(190, 110, 50) : Rgb(150, 140, 60), 6 + FMath::FloorToInt(Rn * 8.f), Rn < 0.1f), X, Y, Rn < 0.3f, 4.f); }
		else if (Rn < 0.62f) { Add(MakeFern(920u + uint32(X), Rgb(176, 140, 60)), X, Y, false, 0.f); }
	}
	for (int32 X = 6; X < W; )   // ... and a row of trees standing at the very bottom
	{
		const float Rn = R01(X, 15, 189u);
		Add(Rn < 0.3f ? MakeSpruce(940u + uint32(X), 50) : MakeAutumnTree(950u + uint32(X), Crowns[FMath::FloorToInt(Rn * 9.f) % 4], Rn > 0.8f), X, H + 8 + FMath::FloorToInt(R01(X, 16, 189u) * 12.f), Rn > 0.5f, 0.f);
		X += 26 + FMath::FloorToInt(R01(X, 17, 189u) * 20.f);
	}

	// ---- 7) the miller's house, the rye patch with sheaves, props along the upper edge of the walk band
	Add(MakeHouse(), HouseX, 110, false, 30.f);
	Add(MakeLantern(), HouseX + 48, 112, false, 3.f);
	for (int32 X = 616; X <= 692; ++X)   // rye stubble rows
	{
		for (int32 Y = 90; Y <= 108; ++Y) { if (Y % 4 == 0 || R01(X, Y, 191u) < 0.1f) { I.Set(X, Y, (X + Y) % 3 ? RyeLo : Rye); } }
	}
	for (int32 k = 0; k < 6; ++k) { Add(MakeSheaf(960u + uint32(k)), 624 + k * 13, 100 + (k % 2) * 8, k % 2 == 1, 4.f); }
	Add(MakeCart(), 732, 118, false, 12.f);
	Add(MakeOldMillstone(), 704, 116, false, 7.f);
	Add(MakeSack(), 718, 116, true, 4.f);
	Add(MakeBench(), 130, 116, false, 10.f);
	Add(MakeBush(970u, Rgb(196, 90, 48), 6, true), 480, 116, false, 5.f);
	Add(MakeBush(971u, Rgb(210, 150, 60), 5, false), 250, 114, true, 5.f);
	Add(MakeBush(972u, Rgb(170, 110, 50), 6, false), 392, 116, false, 5.f);
	// Р69: the arrow sign of the exit is a runtime way sign with a picture (FAnchor::SignFeet, FVSceneU6Objects.cpp)
	for (int32 k = 0; k < 42; ++k)   // small props in the walk band: mushrooms, stones, ferns (off the paths and the objects)
	{
		const int32 X = 44 + FMath::FloorToInt(R01(k, 1, 193u) * 880.f), Y = 124 + FMath::FloorToInt(R01(k, 2, 193u) * 100.f);
		if (FMath::Abs(Y - 188) < 14 || Y > Shore - 12) { continue; }
		if (FMath::Abs(X - StreamX) < 22 || (X > 770 && X < 900 && Y < 160) || (X > 540 && X < 620 && Y < 150)) { continue; }
		const float Rn = R01(k, 3, 193u);
		if (Rn < 0.4f) { Add(MakeMushroom(980u + uint32(k), Rn < 0.2f ? Rgb(214, 60, 50) : Rgb(170, 110, 60)), X, Y, false, 0.f); }
		else if (Rn < 0.7f) { Add(MakeFern(1000u + uint32(k), Rgb(170, 146, 64)), X, Y, Rn < 0.55f, 0.f); }
		else { Add(MakeRock(1020u + uint32(k), 4, 3, Stone, Rn > 0.85f), X, Y, false, 3.f); }
	}

	Items.Sort([](const FItem& A, const FItem& B) { return A.Y < B.Y; });
	for (const FItem& It : Items) { Stamp(I, It.Img, It.X, It.Y, It.bFlip, It.Shadow); }

	// ---- 8) the dark rim at the world edge
	DarkRim(I, 6);

	// ---- the world description (world px)
	OutDef = FWorldDef();
	OutDef.LocationId = FVMill::MillId();
	OutDef.SizeTex = FIntPoint(W, H);
	OutDef.Walk.Add(Rc(34.f, float(WalkTop), float(952 - 34), float(Shore - WalkTop + 1)));
	OutDef.Blockers.Add(Rc(float(StreamX - StreamHW - 2), float(WalkTop), float(StreamHW * 2 + 5), float(BridgeY0 - WalkTop)));        // the stream above the bridge
	OutDef.Blockers.Add(Rc(float(StreamX - StreamHW - 2), float(BridgeY1 + 1), float(StreamHW * 2 + 5), float(Shore + 2 - BridgeY1)));  // ... and below it
	OutDef.Blockers.Add(Rc(690.f, float(WalkTop), 60.f, 8.f));   // the old millstone, the sack and the cart
	OutDef.bWater = true;
	OutDef.ShoreY = float(Shore) * FV::WorldScale;
	OutDef.WaterArea = Rc(0.f, float(Shore), float(W), float(FarBank - Shore));
	OutDef.Dock = FFVRect();
	OutDef.bBirds = true;
	OutDef.bButterflies = true;
	OutDef.bFireflies = true;
	OutDef.bFog = true;

	OutDef.Anchors.Add(Make(EAnchor::Spawn, FName(TEXT("default")), 700.f, 200.f));
	OutDef.Anchors.Add(Make(EAnchor::Spawn, FName(TEXT("from_map")), 700.f, 200.f));
	OutDef.Anchors.Add(Make(EAnchor::Spawn, FName(TEXT("from_forest")), 924.f, 188.f));
	{
		FAnchor A = Make(EAnchor::Exit, FName(TEXT("from_mill")), 952.f, 188.f);
		A.Target = FVDBU6::ForestId();
		A.Hit = Rc(944.f, 170.f, 16.f, 32.f);
		A.Label = TEXT("В лес →");
		A.SignFeet = Pt(900.f, 172.f);   // Р69: the way sign — spruces (left of the entry: off the HUD column, not behind the player)
		A.SignDir = uint8(FVWorldU6::EWayDir::Right);
		OutDef.Anchors.Add(A);
	}
	OutDef.Anchors.Add(Make(EAnchor::Npc, FVMill::MillerNpcId(), 598.f, 130.f));
	{
		FAnchor A = Make(EAnchor::MillObj, FVMill::ObjMillId(), float(MillX), float(MillY));
		A.Hit = Rc(float(MillX - 60), float(MillY - 96), 120.f, 96.f);
		OutDef.Anchors.Add(A);
	}
	{
		// sprite bottom-centre on the far bank (the dam spans the pond); Hit = the lodge by the near bank
		FAnchor A = Make(EAnchor::MillObj, FVMill::ObjBeaverId(), float(DamX0 - 8), float(Shore + 112));
		A.Hit = Rc(float(LodgeX - 22), float(Shore), 44.f, 36.f);
		OutDef.Anchors.Add(A);
	}
	for (int32 d = 0; d < FVMill::FeedDecks; ++d)
	{
		FAnchor A = Make(EAnchor::MillObj, FVMill::ObjDeckId(), float(DeckX[d]), float(Shore + 22), d);
		A.Hit = Rc(float(DeckX[d] - 17), float(Shore - 8), 34.f, 30.f);
		OutDef.Anchors.Add(A);
	}
	{
		FAnchor A = Make(EAnchor::MillObj, FVMill::ObjPenId(), float(PenX), float(Shore + 40));
		A.Hit = Rc(float(PenX - 26), float(Shore + 2), 52.f, 38.f);
		OutDef.Anchors.Add(A);
	}
	for (int32 s = 0; s < FVMill::NetSpots; ++s)
	{
		OutDef.Anchors.Add(Make(EAnchor::NetSpot, NAME_None, float(NetX[s]), float(Shore - 2), s));
	}
	OutDef.Anchors.Append(MillZones());
	// lights: the house windows and the lantern; chimney smoke
	auto Light = [&OutDef](float X, float Y, float R, FColor C, bool bFlicker)
	{
		FAnchor A = Make(EAnchor::Light, NAME_None, X, Y);
		A.Radius = R * FV::WorldScale;
		A.Color = C;
		A.bFlag = bFlicker;
		OutDef.Anchors.Add(A);
	};
	Light(float(HouseX - 22), 94.f, 22.f, FColor(255, 200, 120), false);
	Light(float(HouseX + 22), 94.f, 22.f, FColor(255, 200, 120), false);
	Light(float(HouseX + 48), 86.f, 34.f, FColor(255, 196, 110), true);
	OutDef.Anchors.Add(Make(EAnchor::Smoke, NAME_None, float(HouseX + 19), 46.f));
}

// ============================================================================================
// Object sprites
// ============================================================================================
const FFVSprite& UFVSprites::MillObjSprite(FName ObjId, int32 State, int32 Frame)
{
	const uint64 Key = MakeKey(FVSpriteCat::Mill, GetTypeHash(ObjId), uint32(FMath::Clamp(State, 0, 15)), uint8(Frame & 3));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildMillObj(ObjId, State, Frame), FString::Printf(TEXT("Mill_%s_%d_%d"), *ObjId.ToString(), State, Frame & 3));
}

FFVImage UFVSprites::BuildMillObj(FName ObjId, int32 State, int32 Frame)
{
	using namespace FVArtMillImpl;
	if (ObjId == FVMill::ObjMillId()) { return BuildMillImg(State, Frame & 3); }
	if (ObjId == FVMill::ObjBeaverId()) { return BuildBeaverImg(State, Frame & 1); }
	if (ObjId == FVMill::ObjDeckId()) { return BuildDeckImg(State, Frame & 1); }
	if (ObjId == FVMill::ObjPenId()) { return BuildPenImg(State, Frame & 1); }
	if (ObjId == FVMill::ObjBridgeId()) { return BuildBridgeImg(State); }
	FFVImage I(16, 16);
	I.FillRect(2, 2, 12, 12, Wood);
	I.Outline(PropOut);
	return I;
}
