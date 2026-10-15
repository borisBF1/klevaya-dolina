// Main menu (animated title scene) and the pause menu (settings, help, the guide, save & exit).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVOnline.h"
#include "FVFishing.h"
#include "FVUIKit.h"
#include "InputCoreTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformTime.h"
#include "FVDevTools.h"   // update 6 (Р47): the extended developer mode (empty in Shipping)

namespace FVMenusScreenImpl
{
	// ========================================================================================
	// Title scene art (built once, cached in UFVSprites)
	// ========================================================================================
	constexpr int32 SceneW = 320;
	constexpr int32 SceneH = 180;
	constexpr float WaterTopV = 254.f;          // virtual y where the lake begins
	constexpr float DockEndTexel = 121.f;       // dock spans texels 0..121
	constexpr float DockTopTexel = 144.f;

	static float FarRidge(int32 X)
	{
		return 96.f + 13.f * FMath::Sin(X * 0.029f + 0.7f) + 7.f * FMath::Sin(X * 0.071f + 2.1f) + 3.f * FMath::Sin(X * 0.17f + 0.3f);
	}
	static float NearRidge(int32 X)
	{
		return 112.f + 6.f * FMath::Sin(X * 0.021f + 1.9f) + 4.f * FMath::Sin(X * 0.052f + 0.2f) + 2.f * FMath::Sin(X * 0.13f);
	}

	struct FHouseSpot { int32 X; int32 W; FColor Roof; };
	static const FHouseSpot Houses[3] = {
		{ 212, 11, FColor(184, 70, 52) },
		{ 231, 12, FColor(70, 104, 170) },
		{ 252, 10, FColor(176, 104, 58) } };

	static int32 HouseBase(const FHouseSpot& H)
	{
		// the house sits on the lowest ridge point under it (so it never floats)
		float B = 0.f;
		for (int32 i = 0; i < H.W; ++i) { B = FMath::Max(B, NearRidge(H.X + i)); }
		return FMath::RoundToInt(B) + 2;
	}

	/** Window centers of the village houses (virtual px) — glow at night. */
	static void HouseWindows(TArray<FVector2D>& Out)
	{
		Out.Reset();
		for (const FHouseSpot& H : Houses)
		{
			const int32 Base = HouseBase(H);
			Out.Add(FVector2D(H.X + H.W - 3.f, Base - 4.f) * FV::WorldScale);
		}
	}

	static FVector2D ChimneyTop()
	{
		const FHouseSpot& H = Houses[0];
		return FVector2D(H.X + H.W - 3.f, HouseBase(H) - 14.f) * FV::WorldScale;
	}

	static FFVImage BuildFarHills()
	{
		FFVImage Img(SceneW, SceneH, FColor(0, 0, 0, 0));
		for (int32 X = 0; X < SceneW; ++X)
		{
			const float R = FarRidge(X);
			const bool bLit = FarRidge(X + 1) < FarRidge(X - 1);   // slope facing the light (top-left)
			const int32 Top = FMath::RoundToInt(R);
			for (int32 Y = Top; Y < 128; ++Y)
			{
				const float Depth = float(Y - Top);
				FColor C = FVUI::LerpColor(FColor(150, 160, 210), FColor(118, 134, 190), Depth / 40.f);
				if (bLit && Depth < 18.f) { C = FVUI::LerpColor(C, FColor(184, 192, 232), 0.5f * (1.f - Depth / 18.f)); }
				const float SnowDepth = (90.f - R) * 0.55f + (FVUI::Hash01(X, 7, 3) > 0.5f ? 1.f : 0.f);
				if (R < 90.f && Depth < SnowDepth)
				{
					C = bLit ? FColor(238, 242, 252) : FColor(204, 212, 240);
				}
				C = FVUI::LerpColor(C, FColor(186, 204, 232), FMath::Clamp((Y - 100.f) / 28.f, 0.f, 1.f) * 0.35f);
				if (Y == Top) { C = FVUI::LerpColor(C, FColor(255, 255, 255), 0.25f); }
				Img.Set(X, Y, C);
			}
		}
		return Img;
	}

	static void RoundTree(FFVImage& Img, int32 X, int32 Y, int32 R)
	{
		FVUI::DiskPx(Img, X + 0.5f, Y + 1.5f, float(R), FColor(40, 92, 56));
		FVUI::DiskPx(Img, X + 0.5f, Y + 0.5f, float(R), FColor(62, 130, 70));
		FVUI::DiskPx(Img, X - R * 0.3f + 0.5f, Y - R * 0.35f + 0.5f, R * 0.55f, FColor(98, 170, 88));
		FVUI::RectPx(Img, X, Y + R, 1, 2, FColor(98, 68, 46));
	}

	static void PineTree(FFVImage& Img, int32 X, int32 Y, int32 H)
	{
		for (int32 j = 0; j < H; ++j)
		{
			const int32 HW = (j * 3) / 5;
			for (int32 i = -HW; i <= HW; ++i)
			{
				FVUI::BlendPx(Img, X + i, Y - H + j, i <= 0 ? FColor(58, 120, 84) : FColor(40, 96, 70));
			}
		}
		FVUI::RectPx(Img, X, Y, 1, 2, FColor(96, 66, 44));
	}

	static FFVImage BuildNearHills()
	{
		FFVImage Img(SceneW, SceneH, FColor(0, 0, 0, 0));
		for (int32 X = 0; X < SceneW; ++X)
		{
			const int32 Top = FMath::RoundToInt(NearRidge(X));
			for (int32 Y = Top; Y < 128; ++Y)
			{
				FColor C = FVUI::LerpColor(FColor(116, 182, 92), FColor(86, 148, 78), (Y - Top) / 18.f);
				if (Y == Top) { C = FColor(150, 208, 112); }
				else if (FVUI::Hash01(X, Y, 5) > 0.93f) { C = FVUI::LerpColor(C, FColor(70, 126, 66), 0.6f); }
				if (Y >= 125) { C = (Y == 127) ? FColor(128, 150, 96) : FColor(186, 192, 128); }   // far shore strip
				Img.Set(X, Y, C);
			}
		}
		// houses (village on the far hill)
		for (const FHouseSpot& H : Houses)
		{
			const int32 Base = HouseBase(H);
			const int32 WallH = 6;
			const int32 WallTop = Base - WallH;
			FVUI::RectPx(Img, H.X, WallTop, H.W, WallH, FColor(236, 222, 186));
			FVUI::RectPx(Img, H.X + H.W - 1, WallTop, 1, WallH, FColor(204, 186, 150));
			const FColor RoofDark(FMath::Max(0, H.Roof.R - 50), FMath::Max(0, H.Roof.G - 30), FMath::Max(0, H.Roof.B - 20));
			for (int32 r = 0; r < 5; ++r)
			{
				const int32 Inset = 4 - r;
				FVUI::RectPx(Img, H.X - 1 + Inset, WallTop - 5 + r, H.W + 2 - Inset * 2, 1, r == 4 ? RoofDark : H.Roof);
			}
			FVUI::RectPx(Img, H.X + 2, Base - 4, 2, 4, FColor(118, 76, 46));                   // door
			FVUI::RectPx(Img, H.X + H.W - 4, Base - 5, 2, 2, FColor(84, 76, 96));              // window
			FVUI::RectPx(Img, H.X - 1, Base, H.W + 2, 1, FColor(70, 110, 60));                 // ground shadow
		}
		// chimney on the first house
		{
			const FHouseSpot& H = Houses[0];
			const int32 Base = HouseBase(H);
			FVUI::RectPx(Img, H.X + H.W - 4, Base - 13, 2, 4, FColor(140, 90, 70));
		}
		// trees along the hills (skip the village)
		FRandomStream Rng(777);
		int32 X = 4;
		while (X < SceneW - 2)
		{
			const bool bVillage = X > 204 && X < 268;
			if (!bVillage)
			{
				const int32 Y = FMath::RoundToInt(NearRidge(X)) + Rng.RandRange(1, 5);
				if (Rng.FRand() < 0.35f) { PineTree(Img, X, Y + 2, Rng.RandRange(8, 12)); }
				else { RoundTree(Img, X, Y - 3, Rng.RandRange(3, 5)); }
			}
			X += Rng.RandRange(5, 12);
		}
		return Img;
	}

	static void Cattail(FFVImage& Img, int32 X, int32 BaseY, int32 H)
	{
		for (int32 j = 0; j < H; ++j) { Img.Set(X, BaseY - j, j < H / 2 ? FColor(70, 128, 62) : FColor(92, 150, 70)); }
		FVUI::RectPx(Img, X - 1, BaseY - H - 3, 2, 4, FColor(128, 86, 48));
		Img.Set(X, BaseY - H - 4, FColor(92, 150, 70));
		Img.Set(X + 1, BaseY - H / 2, FColor(92, 150, 70));
		Img.Set(X + 2, BaseY - H / 2 - 1, FColor(92, 150, 70));
	}

	static void LilyPad(FFVImage& Img, int32 X, int32 Y, bool bFlower)
	{
		FVUI::EllipsePx(Img, X + 0.5f, Y + 1.f, 5.f, 2.3f, FColor(40, 90, 70, 120));
		FVUI::EllipsePx(Img, X + 0.5f, Y + 0.5f, 5.f, 2.3f, FColor(82, 154, 72));
		FVUI::EllipsePx(Img, X - 1.f, Y, 2.5f, 1.f, FColor(116, 186, 92));
		Img.Set(X + 3, Y, FColor(0, 0, 0, 0)); Img.Set(X + 4, Y, FColor(0, 0, 0, 0)); Img.Set(X + 2, Y, FColor(60, 120, 60));
		if (bFlower)
		{
			Img.Set(X - 1, Y - 1, FColor(244, 150, 188)); Img.Set(X, Y - 2, FColor(255, 214, 230));
			Img.Set(X + 1, Y - 1, FColor(244, 150, 188)); Img.Set(X, Y - 1, FColor(255, 236, 140));
		}
	}

	static FFVImage BuildForeground()
	{
		FFVImage Img(SceneW, SceneH, FColor(0, 0, 0, 0));
		auto Bank = [&Img](int32 X, int32 Top)
		{
			for (int32 Y = Top; Y < SceneH; ++Y)
			{
				FColor C = FVUI::LerpColor(FColor(118, 182, 86), FColor(84, 142, 70), (Y - Top) / 26.f);
				if (Y == Top) { C = FColor(154, 210, 112); }
				else if (Y == Top + 1) { C = FColor(132, 194, 96); }
				if (FVUI::Hash01(X, Y, 9) > 0.92f) { C = FVUI::LerpColor(C, FColor(160, 214, 116), 0.5f); }
				Img.Set(X, Y, C);
			}
			// shadow of the bank on the water
			FVUI::BlendPx(Img, X, Top - 1, FColor(20, 50, 80, 70));
		};
		// left bank (the dock starts here)
		for (int32 X = 0; X < 92; ++X)
		{
			const float K = X / 92.f;
			Bank(X, FMath::RoundToInt(150.f + K * K * 30.f + 1.5f * FMath::Sin(X * 0.4f)));
		}
		// right bank
		for (int32 X = 262; X < SceneW; ++X)
		{
			const float K = (X - 262) / 58.f;
			Bank(X, FMath::RoundToInt(180.f - FMath::Pow(K, 1.4f) * 36.f + 1.2f * FMath::Sin(X * 0.5f)));
		}
		// reeds & cattails
		const int32 LeftReeds[6] = { 62, 68, 71, 77, 83, 86 };
		for (int32 i = 0; i < 6; ++i)
		{
			const int32 X = LeftReeds[i];
			const float K = X / 92.f;
			const int32 Top = FMath::RoundToInt(150.f + K * K * 30.f);
			Cattail(Img, X, Top + 2, 10 + (i * 5) % 7);
		}
		const int32 RightReeds[5] = { 270, 276, 281, 296, 304 };
		for (int32 i = 0; i < 5; ++i)
		{
			const int32 X = RightReeds[i];
			const float K = (X - 262) / 58.f;
			const int32 Top = FMath::RoundToInt(180.f - FMath::Pow(K, 1.4f) * 36.f);
			Cattail(Img, X, Top + 2, 9 + (i * 3) % 6);
		}
		// grass tufts
		for (int32 i = 0; i < 40; ++i)
		{
			const int32 X = (i * 37) % SceneW;
			if (X >= 92 && X < 262) { continue; }
			const int32 Y = 160 + (i * 13) % 20;
			if (Img.Get(X, Y).A == 0 || Img.Get(X, Y - 3).A == 0) { continue; }
			Img.Set(X, Y, FColor(70, 130, 60)); Img.Set(X - 1, Y - 1, FColor(92, 160, 70)); Img.Set(X + 1, Y - 1, FColor(92, 160, 70));
		}
		// lily pads
		LilyPad(Img, 150, 160, false);
		LilyPad(Img, 166, 170, true);
		LilyPad(Img, 236, 150, false);
		LilyPad(Img, 250, 164, true);
		LilyPad(Img, 196, 176, false);

		// dock: posts first (behind the deck front)
		for (int32 PX = 42; PX <= 118; PX += 18)
		{
			const int32 X = FMath::Min(PX, 117);
			for (int32 Y = int32(DockTopTexel) + 5; Y < int32(DockTopTexel) + 20; ++Y)
			{
				const float Under = FMath::Clamp((Y - (DockTopTexel + 11.f)) / 8.f, 0.f, 1.f);
				FVUI::BlendPx(Img, X, Y, FVUI::LerpColor(FColor(126, 84, 50), FColor(60, 90, 110), Under * 0.7f));
				FVUI::BlendPx(Img, X + 1, Y, FVUI::LerpColor(FColor(126, 84, 50), FColor(60, 90, 110), Under * 0.7f));
				FVUI::BlendPx(Img, X + 2, Y, FVUI::LerpColor(FColor(86, 56, 34), FColor(40, 70, 96), Under * 0.7f));
			}
			FVUI::RectPx(Img, X - 1, int32(DockTopTexel) + 16, 5, 1, FColor(220, 240, 250, 120));   // water line
		}
		// deck
		const int32 DT = int32(DockTopTexel);
		for (int32 X = 0; X <= int32(DockEndTexel); ++X)
		{
			Img.Set(X, DT, FColor(206, 150, 92));
			for (int32 Y = DT + 1; Y < DT + 5; ++Y) { Img.Set(X, Y, FColor(170, 114, 64)); }
			Img.Set(X, DT + 5, FColor(104, 66, 40));
			if (X % 7 == 0)
			{
				for (int32 Y = DT + 1; Y < DT + 5; ++Y) { Img.Set(X, Y, FColor(126, 82, 46)); }
			}
			if (FVUI::Hash01(X, 1, 17) > 0.85f) { Img.Set(X, DT + 2, FColor(186, 128, 74)); }
			FVUI::BlendPx(Img, X, DT + 6, FColor(20, 40, 60, 90));   // deck shadow on the water
		}
		// bucket on the dock
		FVUI::RectPx(Img, 94, DT - 5, 6, 5, FColor(150, 160, 170));
		FVUI::RectPx(Img, 94, DT - 5, 6, 1, FColor(200, 210, 220));
		FVUI::RectPx(Img, 99, DT - 4, 1, 4, FColor(110, 118, 130));
		FVUI::RectPx(Img, 95, DT - 6, 4, 1, FColor(90, 96, 106));
		// tackle box
		FVUI::RectPx(Img, 80, DT - 3, 7, 3, FColor(64, 120, 70));
		FVUI::RectPx(Img, 80, DT - 3, 7, 1, FColor(96, 160, 96));
		FVUI::RectPx(Img, 83, DT - 4, 1, 1, FColor(50, 50, 50));
		return Img;
	}

	static FFVImage BuildCloud(int32 Variant)
	{
		const int32 W = 34 + Variant * 6, H = 14;
		FFVImage Img(W, H, FColor(0, 0, 0, 0));
		FRandomStream Rng(100 + Variant);
		const int32 Blobs = 4 + Variant;
		for (int32 b = 0; b < Blobs; ++b)
		{
			const float CX = 6.f + (W - 12.f) * (b + 0.5f) / Blobs + Rng.FRandRange(-2.f, 2.f);
			const float R = Rng.FRandRange(3.5f, 6.f) - FMath::Abs(b - (Blobs - 1) * 0.5f) * 0.4f;
			FVUI::DiskPx(Img, CX, H - 4.f - R * 0.6f, FMath::Max(2.5f, R), FColor(255, 255, 255));
		}
		FVUI::RectPx(Img, 3, H - 5, W - 6, 3, FColor(255, 255, 255));
		// underside shading
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				FColor C = Img.Get(X, Y);
				if (C.A == 0) { continue; }
				const bool bEdgeBelow = Img.Get(X, Y + 2).A == 0;
				if (bEdgeBelow) { Img.Set(X, Y, FColor(214, 224, 242)); }
			}
		}
		return Img;
	}

	static FFVImage BuildFisherman()
	{
		const TMap<TCHAR, FColor> Pal = {
			{ TEXT('h'), FColor(86, 66, 62) }, { TEXT('H'), FColor(62, 48, 52) },
			{ TEXT('s'), FColor(50, 38, 48) }, { TEXT('b'), FColor(42, 34, 48) },
			{ TEXT('a'), FColor(48, 38, 52) }, { TEXT('p'), FColor(34, 28, 42) },
			{ TEXT('k'), FColor(26, 22, 32) } };
		const TCHAR* Rows[18] = {
			TEXT("....hhhh......"),
			TEXT("..HHHHHHHH...."),
			TEXT("....ssss......"),
			TEXT("....ssss......"),
			TEXT(".....ss......."),
			TEXT("....bbbbb....."),
			TEXT("...bbbbbbb...."),
			TEXT("...bbbbbbbb..."),
			TEXT("...bbbb.bbbaa."),
			TEXT("...bbbb...aaa."),
			TEXT("...bbbbb......"),
			TEXT("...pppppp....."),
			TEXT("...ppppppppp.."),
			TEXT("...pppppppppp."),
			TEXT(".........pp..."),
			TEXT(".........pp..."),
			TEXT(".........pp..."),
			TEXT(".........ppk..") };
		FFVImage Img(14, 18, FColor(0, 0, 0, 0));
		for (int32 Y = 0; Y < 18; ++Y)
		{
			const FString Row(Rows[Y]);
			for (int32 X = 0; X < Row.Len() && X < 14; ++X)
			{
				if (const FColor* C = Pal.Find(Row[X])) { Img.Set(X, Y, *C); }
			}
		}
		return Img;
	}

	struct FTitleArt
	{
		FFVSprite Far, Near, Fore, Fisher;
		FFVSprite Cloud[3];
		bool bReady = false;
	};

	static bool FetchArt(FTitleArt& Art)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp) { return false; }
		auto Get = [Sp](uint32 Id, TFunctionRef<FFVImage()> Build, const TCHAR* Name) -> FFVSprite
		{
			const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::Misc, 0xA11B0 + Id, 0x3E1, 0x4D);
			if (const FFVSprite* S = Sp->FindCached(Key)) { return *S; }
			return Sp->AddCached(Key, Build(), Name);
		};
		Art.Far = Get(0, [] { return BuildFarHills(); }, TEXT("FV_Title_Far"));
		Art.Near = Get(1, [] { return BuildNearHills(); }, TEXT("FV_Title_Near"));
		Art.Fore = Get(2, [] { return BuildForeground(); }, TEXT("FV_Title_Fore"));
		Art.Fisher = Get(3, [] { return BuildFisherman(); }, TEXT("FV_Title_Fisher"));
		for (int32 i = 0; i < 3; ++i)
		{
			Art.Cloud[i] = Get(4 + i, [i] { return BuildCloud(i); }, TEXT("FV_Title_Cloud"));
		}
		Art.bReady = true;
		return true;
	}

	// ========================================================================================
	// Sky cycle
	// ========================================================================================
	struct FSkyKey { float P; FColor Top, Hor, Tint; float Night; };
	static const FSkyKey SkyKeys[] = {
		{ 0.00f, FColor(86, 156, 228), FColor(184, 224, 246), FColor(255, 255, 255), 0.f },
		{ 0.38f, FColor(86, 156, 228), FColor(184, 224, 246), FColor(255, 255, 255), 0.f },
		{ 0.50f, FColor(84, 96, 176), FColor(252, 170, 112), FColor(255, 218, 192), 0.15f },
		{ 0.60f, FColor(40, 42, 104), FColor(150, 96, 140), FColor(176, 162, 210), 0.6f },
		{ 0.70f, FColor(14, 18, 52), FColor(40, 50, 100), FColor(112, 122, 182), 1.f },
		{ 0.84f, FColor(14, 18, 52), FColor(40, 50, 100), FColor(112, 122, 182), 1.f },
		{ 0.93f, FColor(96, 112, 192), FColor(255, 196, 150), FColor(244, 214, 204), 0.25f },
		{ 1.00f, FColor(86, 156, 228), FColor(184, 224, 246), FColor(255, 255, 255), 0.f } };

	struct FSky { FColor Top, Hor, Tint; float Night = 0.f; };

	static FSky SampleSky(float P)
	{
		P = FMath::Frac(P);
		const int32 N = UE_ARRAY_COUNT(SkyKeys);
		for (int32 i = 0; i < N - 1; ++i)
		{
			const FSkyKey& A = SkyKeys[i];
			const FSkyKey& B = SkyKeys[i + 1];
			if (P >= A.P && P <= B.P)
			{
				float T = (B.P > A.P) ? (P - A.P) / (B.P - A.P) : 0.f;
				T = T * T * (3.f - 2.f * T);
				FSky S;
				S.Top = FVUI::LerpColor(A.Top, B.Top, T);
				S.Hor = FVUI::LerpColor(A.Hor, B.Hor, T);
				S.Tint = FVUI::LerpColor(A.Tint, B.Tint, T);
				S.Night = FMath::Lerp(A.Night, B.Night, T);
				return S;
			}
		}
		FSky S;
		S.Top = SkyKeys[0].Top; S.Hor = SkyKeys[0].Hor; S.Tint = SkyKeys[0].Tint;
		return S;
	}

	static FLinearColor Mul(FColor C, FColor Tint, float Alpha = 1.f)
	{
		return FV::Col(uint8(C.R * Tint.R / 255), uint8(C.G * Tint.G / 255), uint8(C.B * Tint.B / 255), uint8(FMath::Clamp(Alpha, 0.f, 1.f) * 255.f));
	}

	/** Texel columns [U0, U1) of a sprite as its own sprite (pixel-exact UVs). */
	static FFVSprite SubSpriteX(const FFVSprite& S, float U0, float U1)
	{
		FFVSprite R = S;
		if (S.W <= 0.f) { return R; }
		U0 = FMath::Clamp(U0, 0.f, S.W);
		U1 = FMath::Clamp(U1, U0, S.W);
		const float Du = (S.UV1.X - S.UV0.X) / S.W;
		R.UV0.X = S.UV0.X + U0 * Du;
		R.UV1.X = S.UV0.X + U1 * Du;
		R.W = U1 - U0;
		return R;
	}

	// ========================================================================================
	// Title scene layout: landscape = the classic 640x360 composition; portrait = the same art re-composed
	// for a tall phone screen (big sky with the title, mountains and the village in the middle, the lake below
	// and the dock with the fisherman at the bottom edge).
	// ========================================================================================
	/** Foreground pieces used in portrait: texel columns [U0, U1) drawn side by side from the left edge. */
	struct FForePiece { float U0, U1; };
	static const FForePiece PortraitFore[3] = { { 30.f, 124.f }, { 158.f, 206.f }, { 282.f, 320.f } };
	constexpr float PortraitHillsU0 = 118.f;   // first column of the hills window (the village stays in view)
	constexpr int32 PortraitBtnCount = 3;      // portrait title buttons (the block keeps its height without a save)
	constexpr float PortraitBtnW = 240.f;
	constexpr float PortraitBtnH = 40.f;
	constexpr float PortraitBtnGap = 10.f;

	struct FTitleLayout
	{
		bool bPortrait = false;
		float WaterTop = WaterTopV;                // virtual y where the lake begins
		float HillsDY = 0.f;                       // vertical offset of the hills layers
		float HillsU0 = 0.f;                       // first texel column of the hills window
		float ForeDY = 0.f;                        // vertical offset of the foreground (banks, dock)
		FVector2D Fisher = FVector2D((DockEndTexel - 11.f) * FV::WorldScale, (DockTopTexel - 14.f) * FV::WorldScale);
		FVector2D Bobber = FVector2D(352.f, 306.f);
		FVector2D JumpMin = FVector2D(390.f, 282.f), JumpMax = FVector2D(500.f, 334.f);
		float SunX0 = 90.f, SunSpan = 460.f, SunBaseY = 250.f, SunArc = 190.f;
		float MoonX0 = 120.f, MoonSpan = 400.f, MoonBaseY = 250.f, MoonArc = 170.f;
		int32 NumStars = 70;
		float StarsMaxY = 190.f, StarsFadeY = 230.f;
		float CloudY0 = 96.f, CloudStep = 21.f, CloudAlt = 60.f;
		float BirdY = 70.f;
		int32 NumShimmer = 44;
		int32 NumReflect = 14;
		FVector2D FireflyL0 = FVector2D(60.f, 250.f), FireflyR0 = FVector2D(520.f, 250.f);
		float FireflyLW = 110.f, FireflyRW = 100.f, FireflyH = 50.f;
		// portrait menu buttons: a block of PortraitBtnCount x PortraitBtnH buttons starting at ButtonsTop
		float ButtonsTop = 168.f;

		/** Landscape virtual position on the hills layers -> current canvas. */
		FVector2D Hills(const FVector2D& P) const { return FVector2D(P.X - HillsU0 * FV::WorldScale, P.Y + HillsDY); }
	};

	static FTitleLayout MakeTitleLayout()
	{
		FTitleLayout L;
		if (!FV::IsPortrait())
		{
			return L;
		}
		const float S = FV::WorldScale;
		const float VH = FV::VH;
		L.bPortrait = true;
		// Bottom up: the dock with the fisherman at the bottom edge (its lowest 20 px run below the screen), the menu
		// buttons in the lower third just above the rod tip (thumb zone), the lake surface a little above the buttons,
		// the hills and the village on the horizon, a tall sky with the title on top.
		L.ForeDY = VH - SceneH * S + 20.f;
		const float ForeShiftX = -PortraitFore[0].U0 * S;
		L.Fisher = FVector2D((DockEndTexel - 11.f) * S + ForeShiftX, (DockTopTexel - 14.f) * S + L.ForeDY);
		L.Bobber = FVector2D(352.f + ForeShiftX, 306.f + L.ForeDY);
		const float RodTipY = L.Fisher.Y + 17.f - 48.f;
		L.ButtonsTop = FMath::RoundToFloat(RodTipY - 16.f - (PortraitBtnCount * PortraitBtnH + (PortraitBtnCount - 1) * PortraitBtnGap));
		L.WaterTop = FMath::RoundToFloat(L.ButtonsTop - 40.f);
		L.HillsDY = L.WaterTop - WaterTopV;
		L.HillsU0 = PortraitHillsU0;
		// fish leap in the open water right of the dock, below the buttons
		L.JumpMin = FVector2D(FMath::Min(L.Fisher.X + 60.f, FV::VW - 70.f), RodTipY + 44.f);
		L.JumpMax = FVector2D(FV::VW - 40.f, RodTipY + 72.f);
		L.SunX0 = 30.f; L.SunSpan = FV::VW - 60.f; L.SunBaseY = L.WaterTop - 4.f; L.SunArc = L.WaterTop - 100.f;
		L.MoonX0 = 50.f; L.MoonSpan = FV::VW - 100.f; L.MoonBaseY = L.WaterTop - 4.f; L.MoonArc = L.WaterTop - 120.f;
		L.NumStars = 100;
		L.StarsMaxY = L.WaterTop - 70.f; L.StarsFadeY = L.WaterTop;
		L.CloudY0 = L.WaterTop * 0.30f; L.CloudStep = L.WaterTop * 0.09f; L.CloudAlt = L.WaterTop * 0.20f;
		L.BirdY = L.WaterTop * 0.42f;
		L.NumShimmer = 64;
		L.NumReflect = 22;
		L.FireflyL0 = FVector2D(10.f, L.Fisher.Y - 30.f); L.FireflyLW = 120.f;
		L.FireflyR0 = FVector2D(FV::VW - 90.f, L.Fisher.Y - 50.f); L.FireflyRW = 80.f;
		L.FireflyH = 50.f;
		return L;
	}

	// ========================================================================================
	// Update 4b: the start-up gate (version check + first sync) and the offline mode — shared by both menus
	// (Docs/UPDATE4_ONLINE.md §10).
	// ========================================================================================
	/** Eight dots running around a ring: "please wait". */
	static void DrawSpinner(FVDraw& D, const FVector2D& C, float Radius, float DotR, const FLinearColor& Col)
	{
		const float Head = FMath::Fmod(float(D.Time) * 9.f, 8.f);
		for (int32 i = 0; i < 8; ++i)
		{
			const float A = float(i) / 8.f * 2.f * PI - 0.5f * PI;
			const float Behind = FMath::Fmod(Head - float(i) + 8.f, 8.f);   // 0 = the leading dot
			const float K = 1.f - Behind / 8.f;
			FLinearColor DotC = Col;
			DotC.A *= 0.15f + 0.85f * K * K;
			D.Circle(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius, DotR * (0.65f + 0.35f * K), DotC);
		}
	}

	/** What the start-up gate allows right now. */
	struct FGateView
	{
		bool bEnabled = false;      // a server is configured (not the offline autotest / no URL)
		bool bOffline = false;      // the player chose «Играть офлайн»
		EFVStartupState State = EFVStartupState::Ready;

		/** «Продолжить» / «Новая игра» / «Восстановить»: only in Ready, Disabled or the offline mode. */
		bool PlayAllowed() const { return !bEnabled || bOffline || State == EFVStartupState::Ready; }
		bool IsChecking() const { return bEnabled && !bOffline && (State == EFVStartupState::Idle || State == EFVStartupState::Checking); }
		/** A decision is needed (update, no connection, banned): the gate card replaces the menu buttons. */
		bool HasCard() const
		{
			return bEnabled && !bOffline && (State == EFVStartupState::UpdateRequired || State == EFVStartupState::UpdateAvailable
				|| State == EFVStartupState::NoConnection || State == EFVStartupState::Banned);
		}
	};

	static FGateView GateOf(const UFVGame* Game)
	{
		FGateView V;
		const UFVOnline* O = Game ? Game->GetOnline() : nullptr;
		if (O && O->IsEnabled())
		{
			V.bEnabled = true;
			V.bOffline = O->IsOfflineMode();
			V.State = O->GetStartupState();
		}
		return V;
	}

	/** «Проверка обновлений…» until the server told the version, then «Синхронизация…» (the first sync + mail). */
	static FString CheckingText(const UFVOnline* O)
	{
		return (O && O->GetVersionInfo().bKnown) ? FString(TEXT("Синхронизация…")) : FString(TEXT("Проверка обновлений…"));
	}

	/** Choices on the gate card / the offline plate / the pause-menu online row. */
	enum class EGateAct : uint8 { None, Download, Later, Retry, Offline, Connect, Quit };

	/** "0.5" from the server, or an empty string when the server did not name the version. */
	static FString LatestVersionName(const UFVOnline* O)
	{
		return O ? O->GetVersionInfo().LatestName.TrimStartAndEnd() : FString();
	}

	/** The server's changelog as display text (CR removed, trimmed). */
	static FString ChangelogText(const UFVOnline* O)
	{
		if (!O) { return FString(); }
		FString S = O->GetVersionInfo().Changelog;
		S.ReplaceInline(TEXT("\r"), TEXT(""));
		return S.TrimStartAndEnd();
	}

	// ========================================================================================
	// Main menu
	// ========================================================================================
	class FVMainMenuScreen final : public FVScreen
	{
	public:
		explicit FVMainMenuScreen(UFVGame* InGame) : FVScreen(InGame), Rng(int32(FPlatformTime::Cycles() & 0x7FFFFFFF)) {}

		virtual bool HandlesEscape() const override { return true; }
		virtual bool DrawsWorldBelow() const override { return false; }

		virtual void OnOpen() override
		{
			InitState();
		}

		void InitState()
		{
			bInit = true;
			Age = 0.f;
			TickCount = 0;
			NextJump = 2.f;
			NibbleTimer = 3.f;
			bJumping = false;
			Drops.Reset();
			Ripples.Reset();
			HouseWindows(Windows);
			bHasSaveCached = Game ? Game->HasSaveGame() : false;
			SaveCheckTimer = 1.f;
			if (UFVAudio* A = UFVAudio::Get()) { A->SetMusicTheme(EFVTheme::Count); }
			// update 4b: the start-up gate — ping, version check and (when registered) the first sync + mail
			LogScroll.Reset();
			GateKick = 2.f;
			if (UFVOnline* O = Online())
			{
				if (O->IsEnabled() && !O->IsOfflineMode() && O->GetStartupState() != EFVStartupState::Checking)
				{
					O->StartupCheck();
				}
			}
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			if (!bInit) { InitState(); }
			++TickCount;
			const float DT = FMath::Clamp(Dt, 0.f, 0.1f);
			Age += DT;
			TickScene(DT);
			SaveCheckTimer -= DT;
			if (SaveCheckTimer <= 0.f)
			{
				SaveCheckTimer = 1.f;
				bHasSaveCached = Game ? Game->HasSaveGame() : false;
			}
			// update 4b: a gate that has not started (e.g. the check was refused while another request ran): nudge it
			GateKick -= DT;
			if (GateKick <= 0.f)
			{
				GateKick = 2.f;
				UFVOnline* O = Online();
				if (O && O->IsEnabled() && !O->IsOfflineMode() && O->GetStartupState() == EFVStartupState::Idle)
				{
					O->StartupCheck();
				}
			}
			if (TickCount > 1 && Game && !Game->HasModal() && In.WasPressed(EKeys::Enter))
			{
				PendingEnter = true;   // handled at the end of Draw (the same place as button clicks)
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!bInit) { InitState(); }
			if (!Art.bReady) { FetchArt(Art); }
			Layout = MakeTitleLayout();
			const float T = Age + 8.f;
			const FSky Sky = SampleSky(T / 90.f);
			DrawScene(D, Sky, T);
			if (Layout.bPortrait)
			{
				DrawTitlePortrait(D, float(D.Time));
			}
			else
			{
				DrawTitle(D, float(D.Time));
			}

			// ---- update 4b: the start-up gate. Playing needs Ready (up to date and synced), the offline mode or no
			// server at all; an update / no connection / a ban shows its card in place of the menu buttons.
			const FGateView Gate = GateOf(Game);
			const bool bPlay = Gate.PlayAllowed();
			const bool bCard = Gate.HasCard();
			EGateAct GateAct = EGateAct::None;

			// ---- buttons ----
			const bool bHasSave = bHasSaveCached;
			const bool bPortrait = Layout.bPortrait;
			enum class EAct : uint8 { None, Continue, NewGame, Quit, Restore } Act = EAct::None;
			if (bCard)
			{
				GateAct = DrawGateCard(D, Gate.State);
			}
			else if (bPortrait)
			{
				// Thumb zone: the block sits in the lower third; the main action is the lowest button.
				// Without a save there is nothing to continue: "Новая игра" takes the bottom slot.
				const float BW = PortraitBtnW, BH = PortraitBtnH, Step = PortraitBtnH + PortraitBtnGap;
				const float TS = 14.f;
				const float BX = FMath::RoundToFloat(FV::VW * 0.5f - BW * 0.5f);
				float BY = Layout.ButtonsTop + (PortraitBtnCount - 1) * Step;
				if (bHasSave)
				{
					if (D.Button(FFVRect(BX, BY, BW, BH), TEXT("Продолжить"), EFVButton::Primary, bPlay, TS)) { Act = EAct::Continue; }
					BY -= Step;
					if (D.Button(FFVRect(BX, BY, BW, BH), TEXT("Новая игра"), EFVButton::Normal, bPlay, TS)) { Act = EAct::NewGame; }
				}
				else if (D.Button(FFVRect(BX, BY, BW, BH), TEXT("Новая игра"), EFVButton::Primary, bPlay, TS))
				{
					Act = EAct::NewGame;
				}
				BY -= Step;
				if (D.Button(FFVRect(BX, BY, BW, BH), TEXT("Выход"), EFVButton::Normal, true, TS)) { Act = EAct::Quit; }
				// update 4: a new phone / a reinstall: the progress comes back with a one-time code
				const float RW = 196.f, RH = 26.f;
				if (D.Button(FFVRect(FMath::RoundToFloat(FV::VW * 0.5f - RW * 0.5f), Layout.ButtonsTop - RH - 10.f, RW, RH), TEXT("Восстановить по коду"), EFVButton::Normal, bPlay, 9.5f))
				{
					Act = EAct::Restore;
				}
			}
			else
			{
				const float BW = 160.f, BH = 24.f, BStep = 30.f, BTextSize = 11.f;
				const float BX = FMath::RoundToFloat(FV::VW * 0.5f - BW * 0.5f);
				float BY = 168.f;
				if (D.Button(FFVRect(BX, BY, BW, BH), TEXT("Продолжить"), EFVButton::Primary, bHasSave && bPlay, BTextSize)) { Act = EAct::Continue; }
				BY += BStep;
				if (D.Button(FFVRect(BX, BY, BW, BH), TEXT("Новая игра"), bHasSave ? EFVButton::Normal : EFVButton::Primary, bPlay, BTextSize)) { Act = EAct::NewGame; }
				BY += BStep;
				if (D.Button(FFVRect(BX, BY, BW, BH), TEXT("Выход"), EFVButton::Normal, true, BTextSize)) { Act = EAct::Quit; }
				// update 4: a new computer / a reinstall: the progress comes back with a one-time code
				BY += BStep + 4.f;
				if (D.Button(FFVRect(BX + 14.f, BY, BW - 28.f, 17.f), TEXT("Восстановить по коду"), EFVButton::Normal, bPlay, 7.5f)) { Act = EAct::Restore; }
			}
			if (!bCard)
			{
				// the check in progress (spinner) or the offline mode with «Подключиться», right above the buttons
				const EGateAct PlateAct = DrawGatePlate(D, Gate);
				if (PlateAct != EGateAct::None) { GateAct = PlateAct; }
			}
			DrawVersion(D);
			const bool bEnterHint = bPlay && !bCard;

			if (bPortrait)
			{
				const FLinearColor FootC = FV::Col(255, 246, 226);
				const FLinearColor FootOut = FV::Col(40, 28, 24);
				const float FootY = FV::VH - 15.f;
				if (FV::IsTouchUI())
				{
					D.TextOutlined(TEXT("Свайп — идти · зажмите у воды — заброс"), FVector2D(FV::VW * 0.5f, FootY), 7.5f, FootC, EFVAlign::Center, FootOut);
				}
				else
				{
					D.TextOutlined(TEXT("WASD — ходить · Пробел/ЛКМ — рыбачить"), FVector2D(FV::VW * 0.5f, FootY), 7.5f, FootC, EFVAlign::Center, FootOut);
					if (bEnterHint)
					{
						D.TextOutlined(bHasSave ? TEXT("Enter — продолжить") : TEXT("Enter — начать"), FVector2D(FV::VW * 0.5f, FootY - 12.f), 6.5f, FV::Col(230, 220, 200), EFVAlign::Center, FootOut);
					}
				}
			}
			else if (FV::IsTouchUI())
			{
				D.TextOutlined(TEXT("Свайп — идти · зажмите у воды — заброс"), FVector2D(320.f, 343.f), 7.f, FV::Col(255, 246, 226), EFVAlign::Center, FV::Col(40, 28, 24));
			}
			else
			{
				D.TextOutlined(TEXT("WASD — ходить · Пробел/ЛКМ — рыбачить"), FVector2D(320.f, 343.f), 7.f, FV::Col(255, 246, 226), EFVAlign::Center, FV::Col(40, 28, 24));
				if (bEnterHint)
				{
					D.TextOutlined(bHasSave ? TEXT("Enter — продолжить") : TEXT("Enter — начать"), FVector2D(634.f, 343.f), 6.f, FV::Col(230, 220, 200), EFVAlign::Right, FV::Col(40, 28, 24));
				}
			}

			// fade-in
			if (Age < 0.9f)
			{
				D.Rect(D.ScreenRect(), FLinearColor(0.f, 0.f, 0.f, 1.f - Age / 0.9f));
			}

			if (PendingEnter)
			{
				PendingEnter = false;
				if (Act == EAct::None && bEnterHint) { Act = bHasSave ? EAct::Continue : EAct::NewGame; }
			}
			if (GateAct != EGateAct::None)
			{
				ApplyGateAct(GateAct);   // may quit the app: nothing after it
				return;
			}
			if (!Game || Act == EAct::None) { return; }

			// ---- actions: last statements (the screen may be destroyed) ----
			UFVGame* G = Game;
			switch (Act)
			{
			case EAct::Continue:
				if (!G->ContinueGame())
				{
					FVUI::Sfx(EFVSfx::Error);
					G->Toast(TEXT("Не удалось загрузить сохранение"), FV::Col(255, 150, 130), EFVIcon::Exclaim);
				}
				return;
			case EAct::NewGame:
				if (bHasSave)
				{
					TWeakObjectPtr<UFVGame> Weak(G);
					G->Confirm(TEXT("Новая игра"), TEXT("Сохранённый прогресс будет стёрт: монеты, удочки, коллекция рыб. Начать заново?"),
						[Weak]() { if (UFVGame* GG = Weak.Get()) { GG->NewGame(); } },
						TEXT("Начать заново"), TEXT("Отмена"));
				}
				else
				{
					G->NewGame();
				}
				return;
			case EAct::Quit:
				G->QuitGame();
				return;
			case EAct::Restore:
				G->OpenScreen(EFVScreen::RestoreSave);
				return;
			default:
				return;
			}
		}

	private:
		// ---------------------------------------------------------------- update 4b: the start-up gate
		FFVScrollView LogScroll;           // the changelog on the update card
		float GateKick = 2.f;              // re-starts a gate that stayed Idle
		float TitleBottom = 86.f;          // lowest point of the title block (set by DrawTitle*)
		int32 DevTaps = 0;                 // 0.5: taps on the version label (5 within 3 s = developer mode)
		double DevTapAt = -10.0;

		UFVOnline* Online() const { return Game ? Game->GetOnline() : nullptr; }

		/** «v0.5» in the bottom-left corner. 0.5: five taps on it within 3 s toggle the developer mode (legend trials). */
		void DrawVersion(FVDraw& D)
		{
			const FString V = FString::Printf(TEXT("v%s"), *UFVOnline::ClientVersionName());
			const FLinearColor C = FV::Col(255, 246, 226, 210);
			const FLinearColor Out = FV::Col(40, 28, 24);
			if (Layout.bPortrait)
			{
				D.TextOutlined(V, FVector2D(6.f, FV::VH - 15.f), 7.f, C, EFVAlign::Left, Out);
			}
			else
			{
				D.TextOutlined(V, FVector2D(6.f, 344.f), 6.f, C, EFVAlign::Left, Out);
			}
#if !UE_BUILD_SHIPPING
			// owner rule: test builds only — a Shipping build (every APK for players) cannot switch the developer mode on
			const FFVRect Hit = Layout.bPortrait ? FFVRect(0.f, FV::VH - 26.f, 64.f, 26.f) : FFVRect(0.f, 336.f, 48.f, 24.f);
			if (D.Clicked(Hit))
			{
				const double Now = FPlatformTime::Seconds();
				DevTaps = (Now - DevTapAt < 3.0) ? DevTaps + 1 : 1;
				DevTapAt = Now;
				if (DevTaps >= 5)
				{
					DevTaps = 0;
					ToggleDevTools();
				}
			}
#endif
		}

		void ToggleDevTools()
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P)
			{
				return;
			}
			P->bDevTools = !P->bDevTools;
			if (UFVSaveGame::SlotExists())
			{
				P->Save();
			}
			FVUI::Sfx(EFVSfx::Click);
			Game->Toast(P->bDevTools ? FString(TEXT("Режим разработчика включён: Пауза → Настройки → «Режим разработчика»"))
				: FString(TEXT("Режим разработчика выключен")), FVPal::White(), EFVIcon::Fish);
			UE_LOG(LogFV, Log, TEXT("FV: developer mode %s"), P->bDevTools ? TEXT("on") : TEXT("off"));
		}

		/** A small dark plate right above the menu buttons: the running check (spinner) or the offline mode with
		    «Подключиться». Nothing when the game is simply online / has no server. */
		EGateAct DrawGatePlate(FVDraw& D, const FGateView& G)
		{
			const bool bP = Layout.bPortrait;
			const float TS = bP ? FV::Ui::Body() : 7.5f;
			const FLinearColor TextC = FV::Col(255, 246, 226);
			// landscape: between the subtitle and «Продолжить» (y 168); portrait: above «Восстановить по коду»
			auto PlateY = [this, bP](float H) { return FMath::RoundToFloat(bP ? Layout.ButtonsTop - 36.f - 10.f - H : 160.f - H); };
			if (G.IsChecking())
			{
				const FString Text = CheckingText(Online());
				const float H = bP ? 30.f : 20.f;
				const float TW = D.MeasureText(Text, TS, true).X;
				const float W = FMath::RoundToFloat(TW + H + 16.f);
				const FFVRect R(FMath::RoundToFloat(FV::VW * 0.5f - W * 0.5f), PlateY(H), W, H);
				D.Panel(R, EFVPanel::Dark);
				DrawSpinner(D, FVector2D(R.X + H * 0.5f + 2.f, R.Center().Y), bP ? 6.5f : 4.5f, bP ? 1.8f : 1.3f, FV::Col(255, 222, 140));
				D.Text(Text, FVector2D(R.X + H + 6.f, FMath::RoundToFloat(R.Center().Y - D.LineHeight(TS, true) * 0.5f)), TS, TextC, EFVAlign::Left, false, true);
				return EGateAct::None;
			}
			if (G.bEnabled && G.bOffline)
			{
				const FString Text = TEXT("Офлайн-режим");
				const float H = bP ? 38.f : 22.f;
				const float BW = bP ? 136.f : 84.f;
				const float BH = bP ? 30.f : 16.f;
				const float TW = D.MeasureText(Text, TS, true).X;
				const float W = FMath::RoundToFloat(22.f + TW + 12.f + BW + 4.f);
				const FFVRect R(FMath::RoundToFloat(FV::VW * 0.5f - W * 0.5f), PlateY(H), W, H);
				D.Panel(R, EFVPanel::Dark);
				D.Circle(FVector2D(R.X + 12.f, R.Center().Y), 3.5f, FV::Col(40, 28, 24));
				D.Circle(FVector2D(R.X + 12.f, R.Center().Y), 2.5f, FV::Col(170, 150, 130));
				D.Text(Text, FVector2D(R.X + 22.f, FMath::RoundToFloat(R.Center().Y - D.LineHeight(TS, true) * 0.5f)), TS, TextC, EFVAlign::Left, false, true);
				const FFVRect B(R.Right() - 4.f - BW, FMath::RoundToFloat(R.Y + (H - BH) * 0.5f), BW, BH);
				if (D.Button(B, TEXT("Подключиться"), EFVButton::Primary, true, bP ? 10.5f : 7.5f))
				{
					return EGateAct::Connect;
				}
				if (!FV::IsTouchUI() && D.Hover(B))
				{
					D.Tooltip(TEXT("Подключиться"), TEXT("Проверить обновления и снова включить рейтинг, почту и копию на сервере."));
				}
			}
			return EGateAct::None;
		}

		/** The gate card in place of the menu buttons: what happened, what is new (the server's changelog), the choices. */
		EGateAct DrawGateCard(FVDraw& D, EFVStartupState State)
		{
			const UFVOnline* O = Online();
			const bool bP = Layout.bPortrait;
			const FString NewName = LatestVersionName(O);
			const FString Mine = FString::Printf(TEXT("У вас версия %s"), *UFVOnline::ClientVersionName());
			struct FBtn { const TCHAR* Label; EGateAct Act; EFVButton Style; };
			TArray<FBtn, TInlineAllocator<2>> Btns;   // [0] = the main action
			FString Title, Sub, Body;
			FString Diag;                              // 0.4.1: small print under the body — why the server was not reached
			EFVIcon Icon = EFVIcon::Exclaim;
			FLinearColor TitleC = FVPal::Ink();
			bool bLog = false;
			switch (State)
			{
			case EFVStartupState::UpdateRequired:
				Title = NewName.IsEmpty() ? FString(TEXT("Нужна новая версия игры")) : FString::Printf(TEXT("Нужна новая версия %s"), *NewName);
				Sub = Mine;
				Body = TEXT("Эта версия устарела, и сервер её больше не принимает. Скачайте обновление — улов, дом и весь прогресс останутся с вами.");
				Icon = EFVIcon::Exclaim;
				TitleC = FV::Col(170, 56, 34);
				bLog = true;
				Btns.Add({ TEXT("Скачать обновление"), EGateAct::Download, EFVButton::Primary });
				Btns.Add({ TEXT("Выход"), EGateAct::Quit, EFVButton::Normal });
				break;
			case EFVStartupState::UpdateAvailable:
				Title = NewName.IsEmpty() ? FString(TEXT("Доступна новая версия")) : FString::Printf(TEXT("Доступна версия %s"), *NewName);
				Sub = Mine;
				Body = TEXT("В Клёвой Долине вышло обновление! Установите его сейчас или играйте дальше — обновиться можно в любой момент.");
				Icon = EFVIcon::Treasure;
				TitleC = FV::Col(56, 112, 34);
				bLog = true;
				Btns.Add({ TEXT("Обновить"), EGateAct::Download, EFVButton::Primary });
				Btns.Add({ TEXT("Позже"), EGateAct::Later, EFVButton::Normal });
				break;
			case EFVStartupState::Banned:
			{
				const FString Reason = O ? O->GetBanReason().TrimStartAndEnd() : FString();
				Title = TEXT("Аккаунт заблокирован");
				Body = Reason.IsEmpty() ? FString(TEXT("Администратор игры заблокировал этот аккаунт.")) : FString::Printf(TEXT("Причина: %s"), *Reason);
				Body += TEXT("\nИграть можно офлайн — без рейтинга, почты и копии на сервере.");
				Icon = EFVIcon::Lock;
				TitleC = FV::Col(170, 56, 34);
				Btns.Add({ TEXT("Играть офлайн"), EGateAct::Offline, EFVButton::Primary });
				Btns.Add({ TEXT("Выход"), EGateAct::Quit, EFVButton::Normal });
				break;
			}
			default:   // NoConnection
				Title = TEXT("Нет связи с сервером");
				Body = TEXT("Сервер игры сейчас не отвечает. Проверьте интернет и попробуйте ещё раз — или играйте офлайн: рейтинг, почта и копия на сервере вернутся сами, как только появится связь.");
				Diag = O ? O->GetLastNetError() : FString();
				Icon = EFVIcon::Radio;
				Btns.Add({ TEXT("Повторить"), EGateAct::Retry, EFVButton::Primary });
				Btns.Add({ TEXT("Играть офлайн"), EGateAct::Offline, EFVButton::Normal });
				break;
			}
			const FString Log = bLog ? ChangelogText(O) : FString();

			// ---- sizes (portrait: phone-sized text and finger-sized buttons)
			const float Pad = bP ? 16.f : 12.f;
			const float TitleS = bP ? 14.f : 11.f;
			const float SubS = bP ? FV::Ui::Small() + 0.5f : 7.f;
			const float BodyS = bP ? FV::Ui::Body() : 7.5f;
			const float LogS = bP ? FV::Ui::Body() - 0.5f : 7.f;
			const float LogHeadS = bP ? FV::Ui::Body() : 7.5f;
			const float BtnH = bP ? 40.f : 22.f;
			const float BtnGap = bP ? 10.f : 8.f;
			const float IconBox = bP ? 22.f : 14.f;
			// the free area: below the title (portrait: down to the footer) / between the title and the footer (landscape)
			const FFVRect Area = bP
				? FFVRect(12.f, TitleBottom + 10.f, FV::VW - 24.f, FV::VH - 28.f - (TitleBottom + 10.f))
				: FFVRect(150.f, 90.f, 340.f, 244.f);
			const float CW = Area.W - 2.f * Pad;
			const float LogInnerW = CW - 14.f;
			const float TitleH = FMath::Max(IconBox, D.LineHeight(TitleS, true));
			const float SubH = Sub.IsEmpty() ? 0.f : D.LineHeight(SubS, false) + 1.f;
			const float BodyH = D.MeasureWrapped(Body, CW, BodyS, false, 1.1f);
			const float DiagS = bP ? FV::Ui::Small() : 6.5f;
			const float DiagH = Diag.IsEmpty() ? 0.f : 6.f + D.MeasureWrapped(Diag, CW, DiagS, false, 1.1f);
			const float LogHeadH = Log.IsEmpty() ? 0.f : D.LineHeight(LogHeadS, true) + 3.f;
			const float LogContentH = Log.IsEmpty() ? 0.f : D.MeasureWrapped(Log, LogInnerW, LogS, false, 1.1f) + 10.f;
			const float BtnBlockH = bP ? Btns.Num() * BtnH + (Btns.Num() - 1) * BtnGap : BtnH;
			const float FixedH = Pad + TitleH + SubH + 8.f + BodyH + DiagH + (Log.IsEmpty() ? 0.f : 10.f + LogHeadH) + 14.f + BtnBlockH + Pad;
			float LogBoxH = Log.IsEmpty() ? 0.f : FMath::Min(LogContentH, bP ? 220.f : 96.f);
			if (!Log.IsEmpty() && FixedH + LogBoxH > Area.H)
			{
				LogBoxH = FMath::Max(34.f, Area.H - FixedH);   // a long changelog scrolls inside its box
			}
			const float H = FMath::RoundToFloat(FixedH + LogBoxH);
			// portrait: a little below the middle (closer to the thumb)
			const float CardY = FMath::RoundToFloat(FMath::Max(Area.Y, Area.Y + (Area.H - H) * (bP ? 0.62f : 0.5f)));
			const FFVRect Card(Area.X, CardY, Area.W, H);

			D.Panel(Card, EFVPanel::Wood);
			D.MarkUI(Card);
			float Y = Card.Y + Pad;
			{
				// icon + title, centered
				const FString Shown = D.Ellipsize(Title, CW - IconBox - 6.f, TitleS, true);
				const float TW = D.MeasureText(Shown, TitleS, true).X;
				const float X = FMath::RoundToFloat(Card.Center().X - (IconBox + 6.f + TW) * 0.5f);
				if (const FFVSprite* Ic = FVUI::Icon(Icon))
				{
					FVUI::SpriteFit(D, *Ic, FFVRect(X, FMath::RoundToFloat(Y + (TitleH - IconBox) * 0.5f), IconBox, IconBox));
				}
				D.Text(Shown, FVector2D(X + IconBox + 6.f, FMath::RoundToFloat(Y + (TitleH - D.LineHeight(TitleS, true)) * 0.5f)), TitleS, TitleC, EFVAlign::Left, false, true);
				Y += TitleH;
			}
			if (!Sub.IsEmpty())
			{
				D.Text(Sub, FVector2D(Card.Center().X, Y), SubS, FVPal::InkSoft(), EFVAlign::Center, false, false);
				Y += SubH;
			}
			Y += 8.f;
			D.TextWrapped(Body, FVector2D(Card.X + Pad, Y), CW, BodyS, FVPal::Ink(), false, 1.1f);
			Y += BodyH;
			if (!Diag.IsEmpty())
			{
				D.TextWrapped(Diag, FVector2D(Card.X + Pad, Y + 6.f), CW, DiagS, FVPal::InkSoft(), false, 1.1f);
				Y += DiagH;
			}
			if (!Log.IsEmpty())
			{
				Y += 10.f;
				D.Text(TEXT("Что нового"), FVector2D(Card.X + Pad, Y), LogHeadS, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += LogHeadH;
				const FFVRect Box(Card.X + Pad, Y, CW, LogBoxH);
				const FLinearColor LogBg = FVPal::Parchment() * 0.7f + FVPal::ParchDark() * 0.3f;
				D.Rect(Box, LogBg);
				D.Frame(Box, FV::Alpha(FVPal::InkSoft(), 0.45f), 1.f);
				const FFVRect Inner = Box.Inset(1.f);
				LogScroll.Update(D, Inner, LogContentH);
				D.PushClip(Inner);
				D.TextWrapped(Log, FVector2D(Box.X + 7.f, Box.Y + 5.f - LogScroll.Offset()), LogInnerW, LogS, FVPal::Ink(), false, 1.1f);
				D.PopClip();
				LogScroll.DrawFades(D, Inner, LogBg);
				LogScroll.DrawBar(D, Inner);
			}

			// ---- the choices: portrait stacked with the main one lowest (thumb zone); landscape side by side
			EGateAct Result = EGateAct::None;
			const float BTS = bP ? 13.f : 9.f;
			if (bP)
			{
				float BY = Card.Bottom() - Pad - BtnH;
				for (int32 i = 0; i < Btns.Num(); ++i)
				{
					if (D.Button(FFVRect(Card.X + Pad, BY, CW, BtnH), Btns[i].Label, Btns[i].Style, true, i == 0 ? BTS : BTS - 1.f))
					{
						Result = Btns[i].Act;
					}
					BY -= BtnH + BtnGap;
				}
			}
			else
			{
				const float BY = Card.Bottom() - Pad - BtnH;
				if (Btns.Num() == 1)
				{
					if (D.Button(FFVRect(FMath::RoundToFloat(Card.Center().X - 80.f), BY, 160.f, BtnH), Btns[0].Label, Btns[0].Style, true, BTS))
					{
						Result = Btns[0].Act;
					}
				}
				else if (Btns.Num() >= 2)
				{
					const float MainW = FMath::RoundToFloat((CW - BtnGap) * 0.6f);
					if (D.Button(FFVRect(Card.X + Pad, BY, MainW, BtnH), Btns[0].Label, Btns[0].Style, true, BTS)) { Result = Btns[0].Act; }
					if (D.Button(FFVRect(Card.X + Pad + MainW + BtnGap, BY, CW - MainW - BtnGap, BtnH), Btns[1].Label, Btns[1].Style, true, BTS)) { Result = Btns[1].Act; }
				}
			}
			return Result;
		}

		/** Runs a gate choice. Nothing here closes the screen except «Выход» (it quits the app). */
		void ApplyGateAct(EGateAct A)
		{
			UFVGame* G = Game;
			UFVOnline* O = Online();
			if (!G)
			{
				return;
			}
			switch (A)
			{
			case EGateAct::Download:
				if (O) { O->OpenDownloadPage(); }
				G->Toast(TEXT("Открываем страницу загрузки…"), FVPal::White(), EFVIcon::Map);
				return;
			case EGateAct::Later:
				if (O) { O->SkipOptionalUpdate(); }
				return;
			case EGateAct::Retry:
				if (O) { O->StartupCheck(); }
				return;
			case EGateAct::Offline:
				if (O) { O->EnterOfflineMode(); }
				G->Toast(TEXT("Офлайн-режим: рейтинг и почта подождут связи"), FVPal::White(), EFVIcon::Radio);
				return;
			case EGateAct::Connect:
				if (O) { O->RetryOnline(); }
				return;
			case EGateAct::Quit:
				G->QuitGame();
				return;
			default:
				return;
			}
		}

		FTitleArt Art;
		FTitleLayout Layout;               // recomputed every Draw (follows the screen orientation)
		TArray<FVector2D> Windows;
		FRandomStream Rng;
		float Age = 0.f;
		int32 TickCount = 0;
		bool PendingEnter = false;
		bool bInit = false;
		bool bHasSaveCached = false;       // HasSaveGame() touches the disk: refreshed once per second
		float SaveCheckTimer = 0.f;

		// jumping fish
		bool bJumping = false;
		float JumpT = 0.f;
		float NextJump = 2.f;
		FVector2D JumpOrigin = FVector2D(400.f, 300.f);
		float JumpDir = 1.f;
		float JumpH = 40.f;
		float JumpLen = 50.f;
		const FFVFishDef* JumpFish = nullptr;

		// bobber
		float NibbleTimer = 3.f;
		float NibbleT = 0.f;

		struct FDrop { FVector2D P; FVector2D V; float Life; float WaterY; };
		struct FRipple { FVector2D C; float Age; float Size; };
		TArray<FDrop> Drops;
		TArray<FRipple> Ripples;

		FVector2D BobberBase() const { return Layout.Bobber; }
		FVector2D FisherPos() const { return Layout.Fisher; }

		void Splash(const FVector2D& P, float Size)
		{
			Ripples.Add({ P, 0.f, Size });
			const int32 N = FMath::RoundToInt(6 * Size);
			for (int32 i = 0; i < N; ++i)
			{
				FDrop Dr;
				Dr.P = P;
				Dr.V = FVector2D(Rng.FRandRange(-40.f, 40.f), Rng.FRandRange(-110.f, -50.f)) * FMath::Sqrt(Size);
				Dr.Life = 1.2f;
				Dr.WaterY = P.Y + 1.f;
				Drops.Add(Dr);
			}
		}

		void StartJump()
		{
			TArray<const FFVFishDef*> Pool = FVDB::FishAt(FVDB::StartLocation());
			Pool.RemoveAll([](const FFVFishDef* F) { return F == nullptr || F->bLegendary; });
			if (Pool.Num() == 0)
			{
				for (const FFVFishDef& F : FVDB::Fish()) { if (!F.bLegendary) { Pool.Add(&F); } }
			}
			JumpFish = Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : nullptr;
			JumpDir = Rng.FRand() < 0.5f ? -1.f : 1.f;
			JumpOrigin = FVector2D(Rng.FRandRange(Layout.JumpMin.X, Layout.JumpMax.X), Rng.FRandRange(Layout.JumpMin.Y, Layout.JumpMax.Y));
			if (Layout.bPortrait)
			{
				// keep the leap inside the narrow screen
				JumpDir = JumpOrigin.X > FV::VW * 0.7f ? -1.f : (JumpOrigin.X < FV::VW * 0.3f ? 1.f : JumpDir);
			}
			JumpH = Rng.FRandRange(30.f, 48.f);
			JumpLen = Rng.FRandRange(36.f, 64.f);
			JumpT = 0.f;
			bJumping = true;
			Splash(JumpOrigin, 1.f);
		}

		void TickScene(float Dt)
		{
			NextJump -= Dt;
			if (!bJumping && NextJump <= 0.f) { StartJump(); }
			if (bJumping)
			{
				JumpT += Dt / 1.1f;
				if (JumpT >= 1.f)
				{
					bJumping = false;
					Splash(JumpOrigin + FVector2D(JumpDir * JumpLen, 0.f), 1.2f);
					NextJump = Rng.FRandRange(3.5f, 7.5f);
				}
			}
			NibbleTimer -= Dt;
			if (NibbleTimer <= 0.f)
			{
				NibbleT = 0.7f;
				Ripples.Add({ BobberBase(), 0.f, 0.6f });
				NibbleTimer = Rng.FRandRange(4.f, 9.f);
			}
			NibbleT = FMath::Max(0.f, NibbleT - Dt);

			for (int32 i = Drops.Num() - 1; i >= 0; --i)
			{
				FDrop& Dr = Drops[i];
				Dr.V.Y += 300.f * Dt;
				Dr.P += Dr.V * Dt;
				Dr.Life -= Dt;
				if (Dr.Life <= 0.f || (Dr.V.Y > 0.f && Dr.P.Y > Dr.WaterY)) { Drops.RemoveAtSwap(i); }
			}
			for (int32 i = Ripples.Num() - 1; i >= 0; --i)
			{
				Ripples[i].Age += Dt;
				if (Ripples[i].Age > 1.6f) { Ripples.RemoveAtSwap(i); }
			}
		}

		void DrawScene(FVDraw& D, const FSky& Sky, float T)
		{
			const FTitleLayout& L = Layout;
			const float P = FMath::Frac(T / 90.f);
			const float Night = Sky.Night;
			const float WaterTop = L.WaterTop;

			// Portrait: the sky and the lake bleed beyond the canvas (no letterbox bars on phones).
			const FFVRect Scr = L.bPortrait ? D.ScreenRect() : FFVRect(0.f, 0.f, FV::VW, FV::VH);

			// sky
			D.GradientV(FFVRect(Scr.X, Scr.Y, Scr.W, WaterTop + 2.f - Scr.Y), FV::Col(Sky.Top), FV::Col(Sky.Hor), L.bPortrait ? 48 : 36);

			// stars
			if (Night > 0.02f)
			{
				for (int32 i = 0; i < L.NumStars; ++i)
				{
					const float SX = FVUI::Hash01(i, 1, 91) * FV::VW;
					const float SY = FVUI::Hash01(i, 2, 91) * L.StarsMaxY;
					const float Tw = 0.55f + 0.45f * FMath::Sin(float(D.Time) * (1.5f + FVUI::Hash01(i, 3, 91) * 2.f) + i);
					const float A = Night * Tw * (1.f - SY / L.StarsFadeY);
					const float Sz = FVUI::Hash01(i, 4, 91) > 0.85f ? 2.f : 1.f;
					D.Rect(FFVRect(FMath::RoundToFloat(SX), FMath::RoundToFloat(SY), Sz, Sz), FLinearColor(1.f, 1.f, 0.9f, A));
				}
			}

			// sun
			const float SunU = FMath::Frac(P - 0.9f) / 0.66f;
			FVector2D SunPos(-100.f, -100.f);
			float SunVis = 0.f;
			if (SunU <= 1.f)
			{
				SunPos = FVector2D(L.SunX0 + SunU * L.SunSpan, L.SunBaseY - FMath::Sin(SunU * PI) * L.SunArc);
				SunVis = 1.f;
				const float Low = L.bPortrait
					? FMath::Clamp((SunPos.Y - (L.SunBaseY - L.SunArc * 0.35f)) / (L.SunArc * 0.35f), 0.f, 1.f)
					: FMath::Clamp((SunPos.Y - 120.f) / 120.f, 0.f, 1.f);
				const FLinearColor Core = FVUI::LerpLinear(FV::Col(255, 244, 196), FV::Col(255, 170, 90), Low);
				D.Glow(SunPos, 60.f, FLinearColor(Core.R, Core.G * 0.9f, Core.B * 0.6f, 0.45f));
				D.Circle(SunPos, 11.f, FV::Alpha(Core, 0.35f));
				D.Circle(SunPos, 9.f, Core);
				D.Circle(SunPos + FVector2D(-2.f, -2.f), 4.f, FV::Col(255, 255, 240, 160));
			}
			// moon
			if (P >= 0.56f && P <= 0.96f)
			{
				const float U = (P - 0.56f) / 0.40f;
				const FVector2D MP(L.MoonX0 + U * L.MoonSpan, L.MoonBaseY - FMath::Sin(U * PI) * L.MoonArc);
				const float Fade = FMath::Clamp(FMath::Min(U, 1.f - U) * 6.f, 0.f, 1.f);
				D.Glow(MP, 40.f, FLinearColor(0.6f, 0.7f, 1.f, 0.35f * Fade));
				D.Circle(MP, 8.f, FV::Col(242, 240, 222, uint8(255 * Fade)));
				D.Circle(MP + FVector2D(2.f, -1.f), 2.f, FV::Col(214, 212, 196, uint8(255 * Fade)));
				D.Circle(MP + FVector2D(-3.f, 3.f), 1.5f, FV::Col(214, 212, 196, uint8(255 * Fade)));
				D.Circle(MP + FVector2D(-1.f, -4.f), 1.f, FV::Col(214, 212, 196, uint8(255 * Fade)));
			}

			// clouds (behind the hills)
			if (Art.bReady)
			{
				const FColor CloudTint = FVUI::LerpColor(FVUI::LerpColor(FColor(255, 255, 255), Sky.Hor, 0.35f), FColor(70, 80, 126), Night);
				for (int32 i = 0; i < 5; ++i)
				{
					const FFVSprite& C = Art.Cloud[i % 3];
					const float Span = FV::VW + C.W * 2.f + 40.f;
					const float X = FMath::Fmod(T * (3.f + i * 1.3f) + i * 173.f, Span) - C.W * 2.f - 20.f;
					const float Y = L.CloudY0 + i * L.CloudStep - (i % 2) * L.CloudAlt;
					D.Sprite(C, FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), FV::WorldScale, Mul(FColor(255, 255, 255), CloudTint, 0.92f - Night * 0.35f));
				}
			}

			// birds (daytime)
			if (Night < 0.5f)
			{
				for (int32 i = 0; i < 3; ++i)
				{
					const float BX = FMath::Fmod(T * (22.f + i * 4.f) + i * 240.f, FV::VW + 160.f) - 80.f;
					const float BY = L.BirdY + i * 16.f + FMath::Sin(T * 1.3f + i) * 5.f;
					const float Flap = FMath::Sin(T * 9.f + i * 2.f) * 2.f;
					const FLinearColor BC = FV::Col(50, 44, 60, uint8(220 * (1.f - Night * 2.f)));
					D.Line(FVector2D(BX - 4.f, BY - Flap), FVector2D(BX, BY), BC, 1.f);
					D.Line(FVector2D(BX, BY), FVector2D(BX + 4.f, BY - Flap), BC, 1.f);
				}
			}

			// hills
			if (Art.bReady)
			{
				const FColor FarTint = FVUI::LerpColor(Sky.Tint, Sky.Hor, 0.25f);
				if (L.bPortrait)
				{
					const float U1 = L.HillsU0 + FV::VW / FV::WorldScale;
					D.Sprite(SubSpriteX(Art.Far, L.HillsU0, U1), FVector2D(0.f, L.HillsDY), FV::WorldScale, Mul(FColor(255, 255, 255), FarTint));
					D.Sprite(SubSpriteX(Art.Near, L.HillsU0, U1), FVector2D(0.f, L.HillsDY), FV::WorldScale, Mul(FColor(255, 255, 255), Sky.Tint));
				}
				else
				{
					D.Sprite(Art.Far, FVector2D(0.f, 0.f), FV::WorldScale, Mul(FColor(255, 255, 255), FarTint));
					D.Sprite(Art.Near, FVector2D(0.f, 0.f), FV::WorldScale, Mul(FColor(255, 255, 255), Sky.Tint));
				}
			}
			// chimney smoke
			{
				const FVector2D CT = L.Hills(ChimneyTop());
				for (int32 i = 0; i < 5; ++i)
				{
					const float Ph = FMath::Frac(T * 0.22f + i / 5.f);
					const FVector2D SP = CT + FVector2D(Ph * 16.f + FMath::Sin(Ph * 5.f + i) * 2.f, -Ph * 34.f);
					D.Circle(SP, 2.f + Ph * 4.f, Mul(FColor(236, 236, 240), Sky.Tint, 0.5f * (1.f - Ph)));
				}
			}
			// window glows at night
			if (Night > 0.05f)
			{
				for (const FVector2D& W0 : Windows)
				{
					const FVector2D W = L.Hills(W0);
					D.Rect(FFVRect(W.X - 2.f, W.Y - 2.f, 4.f, 4.f), FV::Col(255, 214, 120, uint8(255 * FMath::Clamp(Night * 1.4f, 0.f, 1.f))));
					D.Glow(W, 14.f, FLinearColor(1.f, 0.7f, 0.3f, 0.5f * Night));
				}
			}

			// water
			const FColor WTop = FVUI::LerpColor(Sky.Hor, FColor(70, 140, 200), 0.55f);
			D.GradientV(FFVRect(Scr.X, WaterTop, Scr.W, Scr.Bottom() - WaterTop), Mul(WTop, Sky.Tint), Mul(FColor(30, 80, 150), Sky.Tint), L.bPortrait ? 24 : 18);
			D.Rect(FFVRect(Scr.X, WaterTop, Scr.W, 1.f), FV::Col(255, 255, 240, 70));
			// sun / moon reflection
			if (SunVis > 0.f && SunPos.Y < L.SunBaseY)
			{
				const float Str = L.bPortrait
					? FMath::Clamp((SunPos.Y - (L.SunBaseY - L.SunArc)) / L.SunArc, 0.2f, 1.f)
					: FMath::Clamp((SunPos.Y - 60.f) / 180.f, 0.2f, 1.f);
				for (int32 i = 0; i < L.NumReflect; ++i)
				{
					const float RY = WaterTop + 4.f + i * 7.f;
					const float W = (18.f - i * 0.6f) * (0.6f + 0.4f * FMath::Sin(float(D.Time) * 3.f + i * 1.7f));
					D.Rect(FFVRect(SunPos.X - W * 0.5f + FMath::Sin(float(D.Time) * 2.f + i) * 2.f, RY, W, 2.f), FV::Col(255, 226, 160, uint8(150 * Str * FMath::Max(0.f, 1.f - i / (L.NumReflect + 2.f)))));
				}
			}
			// shimmer dashes
			for (int32 i = 0; i < L.NumShimmer; ++i)
			{
				const float Sp = 4.f + FVUI::Hash01(i, 5, 92) * 6.f;
				const float SX = FMath::Fmod(FVUI::Hash01(i, 1, 92) * FV::VW + T * Sp, FV::VW + 30.f) - 15.f;
				const float SY = WaterTop + 6.f + FVUI::Hash01(i, 2, 92) * (FV::VH - WaterTop - 10.f);
				const float W = 5.f + FVUI::Hash01(i, 3, 92) * 12.f;
				const float A = 0.12f + 0.2f * (0.5f + 0.5f * FMath::Sin(float(D.Time) * 2.2f + i * 1.3f));
				D.Rect(FFVRect(FMath::RoundToFloat(SX), FMath::RoundToFloat(SY), W, 1.f), FLinearColor(1.f, 1.f, 1.f, A * (1.f - Night * 0.5f)));
			}
			// portrait: a few lily pads floating in the middle of the (much taller) lake
			if (L.bPortrait && Art.bReady)
			{
				const FFVSprite Pads = SubSpriteX(Art.Fore, PortraitFore[1].U0, PortraitFore[1].U1);
				const FLinearColor PadTint = Mul(FColor(255, 255, 255), Sky.Tint);
				const float PadRow = 170.f * FV::WorldScale;   // texel row of the flowering pad inside the strip
				const float Y1 = FMath::RoundToFloat(WaterTop + 40.f - PadRow);
				const float Y2 = FMath::RoundToFloat(WaterTop + (L.Fisher.Y - WaterTop) * 0.52f - PadRow);
				D.Sprite(Pads, FVector2D(10.f, Y1), FV::WorldScale, PadTint, true);
				D.Sprite(Pads, FVector2D(FMath::RoundToFloat(FV::VW - Pads.W * FV::WorldScale - 24.f), Y2), FV::WorldScale, PadTint);
			}
			// ripples
			for (const FRipple& R : Ripples)
			{
				const float K = R.Age / 1.6f;
				const float Rad = (4.f + K * 22.f) * R.Size;
				D.Ring(R.C, Rad, Rad * 0.32f, FLinearColor(1.f, 1.f, 1.f, 0.55f * (1.f - K)), 1.f);
			}
			// jumping fish
			UFVSprites* Sp = UFVSprites::Get();
			if (bJumping && JumpFish && Sp)
			{
				const float U = FMath::Clamp(JumpT, 0.f, 1.f);
				const FVector2D FP = JumpOrigin + FVector2D(JumpDir * U * JumpLen, -FMath::Sin(U * PI) * JumpH);
				const float DX = JumpDir * JumpLen;
				const float DY = -FMath::Cos(U * PI) * PI * JumpH;
				const bool bFlip = JumpDir < 0.f;
				const float Deg = FMath::RadiansToDegrees(bFlip ? FMath::Atan2(-DY, -DX) : FMath::Atan2(DY, DX));
				const FFVSprite& FS = Sp->Fish(*JumpFish);
				D.SpriteCentered(FS, FP, FV::WorldScale, Mul(FColor(255, 255, 255), FVUI::LerpColor(Sky.Tint, FColor(255, 255, 255), 0.3f)), bFlip, Deg);
			}
			for (const FDrop& Dr : Drops)
			{
				D.Rect(FFVRect(Dr.P.X - 1.f, Dr.P.Y - 1.f, 2.f, 2.f), FV::Col(214, 238, 255, uint8(220 * FMath::Clamp(Dr.Life, 0.f, 1.f))));
			}

			// foreground: banks, dock
			if (Art.bReady)
			{
				const FLinearColor ForeTint = Mul(FColor(255, 255, 255), Sky.Tint);
				if (L.bPortrait)
				{
					// left bank with the dock, a strip of lily pads, the right bank with cattails
					float PX = 0.f;
					for (const FForePiece& Piece : PortraitFore)
					{
						const FFVSprite Part = SubSpriteX(Art.Fore, Piece.U0, Piece.U1);
						D.Sprite(Part, FVector2D(PX, L.ForeDY), FV::WorldScale, ForeTint);
						PX += Part.W * FV::WorldScale;
					}
				}
				else
				{
					D.Sprite(Art.Fore, FVector2D(0.f, 0.f), FV::WorldScale, ForeTint);
				}
			}

			// fisherman, rod, line, bobber
			const FVector2D FPos = FisherPos();
			const FLinearColor FisherTint = Mul(FColor(255, 255, 255), FVUI::LerpColor(Sky.Tint, FColor(255, 255, 255), 0.4f));
			if (Art.bReady)
			{
				const float Breath = (FMath::Sin(T * 1.6f) > 0.85f) ? -1.f : 0.f;
				D.Sprite(Art.Fisher, FPos + FVector2D(0.f, Breath), FV::WorldScale, FisherTint);
			}
			const FVector2D Hand = FPos + FVector2D(12.5f, 8.5f) * FV::WorldScale;
			const float Sway = FMath::Sin(T * 0.9f) * 2.f + (NibbleT > 0.f ? FMath::Sin(NibbleT * 30.f) * 2.f : 0.f);
			const FVector2D Tip = Hand + FVector2D(58.f, -46.f + Sway);
			const FVector2D RodMid = Hand + (Tip - Hand) * 0.5f + FVector2D(0.f, 2.f);
			const FLinearColor RodC = Mul(FColor(120, 82, 48), Sky.Tint);
			D.Line(Hand, RodMid, RodC, 2.f);
			D.Line(RodMid, Tip, RodC, 1.25f);
			const float Dip = NibbleT > 0.f ? FMath::Max(0.f, FMath::Sin((0.7f - NibbleT) * 18.f)) * 2.5f : 0.f;
			const FVector2D Bob = BobberBase() + FVector2D(0.f, FMath::Sin(T * 2.4f) * 0.8f + Dip);
			const FLinearColor LineC = FV::Col(240, 240, 240, 150);
			FVector2D Prev = Tip;
			for (int32 s = 1; s <= 6; ++s)
			{
				const float U = s / 6.f;
				const FVector2D Pt = FMath::Lerp(Tip, Bob, U) + FVector2D(0.f, FMath::Sin(U * PI) * 10.f);
				D.Line(Prev, Pt, LineC, 0.5f);
				Prev = Pt;
			}
			D.Circle(Bob, 3.f, Mul(FColor(60, 30, 30), Sky.Tint));
			D.Rect(FFVRect(Bob.X - 2.f, Bob.Y - 2.f, 4.f, 2.f), Mul(FColor(226, 56, 48), Sky.Tint));
			D.Rect(FFVRect(Bob.X - 2.f, Bob.Y, 4.f, 1.5f), Mul(FColor(250, 250, 250), Sky.Tint));
			D.Rect(FFVRect(Bob.X - 3.f, Bob.Y + 1.5f, 6.f, 1.f), FV::Col(255, 255, 255, 90));

			// fireflies
			if (Night > 0.2f)
			{
				for (int32 i = 0; i < 14; ++i)
				{
					const float Ph = T * (0.2f + FVUI::Hash01(i, 1, 93) * 0.3f) + i * 2.3f;
					const bool bLeft = i % 2 == 0;
					const FVector2D FF(bLeft ? L.FireflyL0.X + L.FireflyLW * (0.5f + 0.5f * FMath::Sin(Ph)) : L.FireflyR0.X + L.FireflyRW * (0.5f + 0.5f * FMath::Sin(Ph * 1.3f)),
						(bLeft ? L.FireflyL0.Y : L.FireflyR0.Y) + L.FireflyH * (0.5f + 0.5f * FMath::Sin(Ph * 1.7f + 1.f)));
					const float Blink = FMath::Max(0.f, FMath::Sin(float(D.Time) * (2.f + i * 0.2f) + i));
					const float A = (Night - 0.2f) * 1.25f * Blink;
					D.Glow(FF, 7.f, FLinearColor(0.8f, 1.f, 0.3f, 0.6f * A));
					D.Rect(FFVRect(FF.X - 0.5f, FF.Y - 0.5f, 1.5f, 1.5f), FLinearColor(1.f, 1.f, 0.6f, A));
				}
			}
		}

		/** Portrait title: "Клёвая" / "Долина" on two big wavy outlined lines + subtitle. */
		void DrawTitlePortrait(FVDraw& D, float T)
		{
			const FString Words[2] = { TEXT("Клёвая"), TEXT("Долина") };
			float Size = 42.f;
			while (Size > 22.f && FMath::Max(D.MeasureText(Words[0], Size, true).X, D.MeasureText(Words[1], Size, true).X) > FV::VW - 70.f)
			{
				Size -= 1.f;
			}
			// The font line box is much taller than the glyphs (caps start ~19% down, the baseline sits at ~80%):
			// step by 75% of it so the two words sit close without touching.
			const float LineBox = D.MeasureText(TEXT("КДЁу"), Size, true).Y;
			const float LineStep = FMath::RoundToFloat(LineBox * 0.75f);
			const float Y0 = FMath::RoundToFloat(FV::VH * 0.045f);
			const FLinearColor OutC = FV::Col(74, 36, 16);
			int32 Index = 0;
			for (int32 w = 0; w < 2; ++w)
			{
				const FString& Word = Words[w];
				const float TotalW = D.MeasureText(Word, Size, true).X;
				// "Клёвая" sits a little to the left, "Долина" a little to the right (hand-lettered sign look)
				const float X0 = FMath::RoundToFloat(FV::VW * 0.5f - TotalW * 0.5f + (w == 0 ? -12.f : 12.f));
				const float BaseY = Y0 + w * LineStep;
				for (int32 i = 0; i < Word.Len(); ++i, ++Index)
				{
					const FString Ch = Word.Mid(i, 1);
					const float X = X0 + D.MeasureText(Word.Left(i), Size, true).X;
					const float Y = BaseY + FMath::Sin(T * 2.2f - Index * 0.45f) * 2.5f;
					D.Text(Ch, FVector2D(X + 3.f, Y + 4.5f), Size, FV::Col(30, 16, 10, 110), EFVAlign::Left, false, true);
					for (int32 k = 0; k < 12; ++k)
					{
						const float A = k * 2.f * PI / 12.f;
						D.Text(Ch, FVector2D(X + FMath::Cos(A) * 2.5f, Y + FMath::Sin(A) * 2.5f), Size, OutC, EFVAlign::Left, false, true);
					}
					D.Text(Ch, FVector2D(X, Y - 1.f), Size, FV::Col(255, 248, 206), EFVAlign::Left, false, true);
					D.Text(Ch, FVector2D(X, Y + 0.6f), Size, w == 0 ? FV::Col(255, 196, 64) : FV::Col(170, 218, 92), EFVAlign::Left, false, true);
				}
			}
			const float SubY = FMath::RoundToFloat(Y0 + LineStep + LineBox * 0.93f);
			D.TextOutlined(TEXT("рыбалка, прогресс и уют"), FVector2D(FV::VW * 0.5f, SubY), 11.f, FV::Col(255, 244, 220), EFVAlign::Center, FV::Col(70, 40, 24));
			TitleBottom = SubY + D.LineHeight(11.f, true);
		}

		void DrawTitle(FVDraw& D, float T)
		{
			const FString Title = TEXT("Клёвая Долина");
			const float Size = 30.f;
			const float TotalW = D.MeasureText(Title, Size, true).X;
			const float X0 = 320.f - TotalW * 0.5f;
			const float BaseY = 24.f;
			const FLinearColor OutC = FV::Col(74, 36, 16);
			for (int32 i = 0; i < Title.Len(); ++i)
			{
				const FString Ch = Title.Mid(i, 1);
				if (Ch == TEXT(" ")) { continue; }
				const float X = X0 + D.MeasureText(Title.Left(i), Size, true).X;
				const float Y = BaseY + FMath::Sin(T * 2.2f - i * 0.45f) * 2.5f;
				D.Text(Ch, FVector2D(X + 2.5f, Y + 4.f), Size, FV::Col(30, 16, 10, 110), EFVAlign::Left, false, true);
				for (int32 k = 0; k < 12; ++k)
				{
					const float A = k * 2.f * PI / 12.f;
					D.Text(Ch, FVector2D(X + FMath::Cos(A) * 2.f, Y + FMath::Sin(A) * 2.f), Size, OutC, EFVAlign::Left, false, true);
				}
				D.Text(Ch, FVector2D(X, Y - 1.f), Size, FV::Col(255, 248, 206), EFVAlign::Left, false, true);
				const bool bAccent = i < 6;   // "Клёвая" warm gold, "Долина" leafy green-gold
				D.Text(Ch, FVector2D(X, Y + 0.6f), Size, bAccent ? FV::Col(255, 196, 64) : FV::Col(170, 218, 92), EFVAlign::Left, false, true);
			}
			const float SubY = BaseY + 48.f;
			D.TextOutlined(TEXT("рыбалка, прогресс и уют"), FVector2D(320.f, SubY), 10.f, FV::Col(255, 244, 220), EFVAlign::Center, FV::Col(70, 40, 24));
			TitleBottom = SubY + D.LineHeight(10.f, true);
		}
	};

	// ========================================================================================
	// Pause menu
	// ========================================================================================
	static bool IsFullscreenNow()
	{
		return GEngine && GEngine->GameViewport && GEngine->GameViewport->IsFullScreenViewport();
	}

	class FVPauseScreen final : public FVScreen
	{
	public:
		explicit FVPauseScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual bool HandlesEscape() const override { return true; }

		virtual void OnOpen() override
		{
			InitState();
		}

		void InitState()
		{
			bInit = true;
			Page = EPage::Main;
			TickCount = 0;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			UFVAudio* A = UFVAudio::Get();
			SfxVol = P ? P->SfxVolume : (A ? A->GetSfxVolume() : 0.8f);
			MusicVol = P ? P->MusicVolume : (A ? A->GetMusicVolume() : 0.45f);
			// TEMP-HUD-DEBUG (screenshots of the sub-pages; removed before hand-off)
			{
				const FString DebugPage = FPlatformMisc::GetEnvironmentVariable(TEXT("FV_HUD_PAGE"));
				if (DebugPage == TEXT("settings")) { Page = EPage::Settings; }
				else if (DebugPage.StartsWith(TEXT("help"))) { Page = EPage::Help; bHelpToEnd = DebugPage == TEXT("help2"); }
				else if (DebugPage == TEXT("trials")) { Page = EPage::Trials; }
#if !UE_BUILD_SHIPPING
				else if (DebugPage == TEXT("dev")) { Page = EPage::Dev; DevPanel.Reset(); }
#endif
			}
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			if (!bInit) { InitState(); }
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (In.WasPressed(EKeys::Escape))
			{
				if (Page != EPage::Main)
				{
					Page = EPage::Main;
					FVUI::Sfx(EFVSfx::Close, 0.7f);
					return;
				}
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
			// help: arrow keys / PageUp / PageDown scroll it smoothly too (the wheel and finger drags are read by the scroll view)
			if (Page == EPage::Help)
			{
				FFVScrollView& S = FV::IsPortrait() ? HelpScroll : HelpScrollSide;
				const float Step = FV::IsPortrait() ? 48.f : 36.f;
				if (In.WasPressed(EKeys::Down)) { S.ScrollTo(S.Target + Step); }
				if (In.WasPressed(EKeys::Up)) { S.ScrollTo(S.Target - Step); }
				if (In.WasPressed(EKeys::PageDown)) { S.ScrollTo(S.Target + S.View * 0.85f); }
				if (In.WasPressed(EKeys::PageUp)) { S.ScrollTo(S.Target - S.View * 0.85f); }
				if (In.WasPressed(EKeys::Home)) { S.ScrollTo(0.f); }
				if (In.WasPressed(EKeys::End)) { S.ScrollTo(S.MaxOffset()); }
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!bInit) { InitState(); }
			EAct Act = EAct::None;
			const bool bPortrait = FV::IsPortrait();
			switch (Page)
			{
			case EPage::Main: Act = bPortrait ? DrawMainPortrait(D) : DrawMain(D); break;
			case EPage::Settings: Act = bPortrait ? DrawSettingsPortrait(D) : DrawSettings(D); break;
			case EPage::Help: Act = bPortrait ? DrawHelpPortrait(D) : DrawHelp(D); break;
			case EPage::Trials: Act = DrawTrials(D); break;
			case EPage::Dev:
#if !UE_BUILD_SHIPPING
				// update 6 (Р47): the panel runs its own actions; Closed = the screen is retired, return at once
				switch (DevPanel.Draw(D, Game))
				{
				case FFVDevPanel::EResult::Closed: return;
				case FFVDevPanel::EResult::Back: Page = EPage::Settings; break;
				default: break;
				}
#else
				Page = EPage::Main;
#endif
				break;
			}
			if (!Game || Act == EAct::None) { return; }

			UFVGame* G = Game;   // the calls below may destroy this screen: nothing after them touches members
			const FName Pick = TrialPick;
			switch (Act)
			{
			case EAct::Trial:
			{
				// 0.5 developer mode: the next bite is this legend (tackle for its level, nothing recorded)
				G->GetFishing().StartLegendTrial(Pick);
				const FFVFishDef* F = FVDB::FindFish(Pick);
				G->CloseScreen();
				G->Toast(FString::Printf(TEXT("Тест: забросьте удочку у любой воды — клюнет «%s»"), F ? *F->Name : TEXT("?")), FVPal::Gold(), EFVIcon::Fish);
				return;
			}
			case EAct::Resume:
				G->CloseScreen();
				return;
			case EAct::MainMenu:
				G->ReturnToMainMenu();
				return;
			case EAct::Guide:
				G->OpenScreen(EFVScreen::Tutorial);
				return;
			case EAct::Profile:
				G->OpenScreen(EFVScreen::Profile);
				return;
			case EAct::Leaderboard:
				G->OpenScreen(EFVScreen::Leaderboard);
				return;
			case EAct::Restore:
				G->OpenScreen(EFVScreen::RestoreSave);
				return;
			case EAct::Quit:
			{
				TWeakObjectPtr<UFVGame> Weak(G);
				G->Confirm(TEXT("Выйти из игры?"), TEXT("Прогресс будет сохранён. До встречи у воды!"),
					[Weak]()
					{
						if (UFVGame* GG = Weak.Get())
						{
							GG->SaveNow();
							GG->QuitGame();
						}
					},
					TEXT("Выйти"), TEXT("Остаться"));
				return;
			}
			default:
				return;
			}
		}

	private:
		enum class EPage : uint8 { Main, Settings, Help, Trials, Dev };
		enum class EAct : uint8 { None, Resume, MainMenu, Quit, Guide, Profile, Leaderboard, Restore, Trial };
		EPage Page = EPage::Main;
		FName TrialPick;                // 0.5 developer mode: the legend picked on the Trials page
#if !UE_BUILD_SHIPPING
		FFVDevPanel DevPanel;           // update 6 (Р47): «Режим разработчика» (FVDevTools.cpp)
#endif

		/** Owner rule: the developer mode exists only in test builds — a Shipping build (every APK for players) never
		    shows it, whatever the save says. */
		bool DevTools() const
		{
#if UE_BUILD_SHIPPING
			return false;
#else
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			return P && P->bDevTools;
#endif
		}

		/** Update 6 (Р47): «Режим разработчика» starts at the top. */
		void OpenDevPanel()
		{
#if !UE_BUILD_SHIPPING
			DevPanel.Reset();
#endif
		}

		/** 0.5 developer mode «Бой с легендой (тест)»: the six legends + the Хозяин омута (update 6), the tackle each trial uses, the session score. */
		EAct DrawTrials(FVDraw& D)
		{
			const bool bP = FV::IsPortrait();
			const float TS = bP ? FV::Ui::Body() + 1.f : 8.5f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float BtnH = bP ? 34.f : 18.f;
			const float RowH = bP ? 58.f : 30.f;
			const float W = bP ? FV::WindowRect().W : 340.f;
			const float CW = W - (bP ? 40.f : 32.f);
			const FString Intro = TEXT("Выберите легенду и забросьте удочку у любой воды — клюнет она. Снасть рассчитана на её уровень, подготовка не нужна. Улов, опыт и достижения не засчитываются.");
			const float IntroH = D.MeasureWrapped(Intro, CW, SS);
			const TCHAR* const Ids[] = { TEXT("fish_tsar_carp"), TEXT("fish_silver_sturgeon"), TEXT("fish_crystal_trout"),
				TEXT("fish_giant_angler"), TEXT("fish_fog_serpent"), TEXT("fish_zhar_ryba"), TEXT("fish_mill_master") };
			const int32 N = UE_ARRAY_COUNT(Ids);
			const float H = FMath::RoundToFloat((bP ? 30.f : 22.f) + IntroH + 8.f + RowH * N + 8.f + BtnH + (bP ? 22.f : 12.f));
			const FFVRect R = bP ? FFVRect(FV::WindowRect().X, FMath::RoundToFloat(FMath::Max(FV::TopBarH + 4.f, FV::WindowRect().Bottom() - H)), W, H)
				: FFVRect(FMath::RoundToFloat((FV::VW - W) * 0.5f), FMath::RoundToFloat(FMath::Max(24.f, (FV::VH - H) * 0.5f)), W, H);
			EAct Act = EAct::None;
			if (D.Window(R, TEXT("Бой с легендой (тест)"))) { Page = EPage::Settings; }
			const float LX = R.X + (bP ? 20.f : 16.f);
			float Y = R.Y + (bP ? 30.f : 22.f);
			D.TextWrapped(Intro, FVector2D(LX, Y), CW, SS, FVPal::InkSoft());
			Y += IntroH + 8.f;
			const FName Pending = Game ? Game->GetFishing().GetPendingTrial() : NAME_None;
			const float BW = bP ? 108.f : 70.f;
			for (int32 i = 0; i < N; ++i)
			{
				const FName Id(Ids[i]);
				const FFVFishDef* F = FVDB::FindFish(Id);
				if (!F) { continue; }
				int32 Tries = 0, Wins = 0;
				if (Game) { Game->GetFishing().GetTrialScore(Id, Tries, Wins); }
				const float TW = CW - BW - 8.f;
				D.Text(D.Ellipsize(F->Name, TW, TS, true), FVector2D(LX, Y + 1.f), TS, FVPal::Ink(), EFVAlign::Left, false, true);
				const FString Setup = FVFishing::TrialSetupText(Id);
				const FString Line2 = Tries > 0 ? FString::Printf(TEXT("%s · поймано %d из %d"), *Setup, Wins, Tries) : Setup;
				D.TextWrapped(Line2, FVector2D(LX, Y + D.LineHeight(TS, true) + 2.f), TW, SS, FVPal::InkSoft());
				const FFVRect B(LX + CW - BW, FMath::RoundToFloat(Y + (RowH - 8.f - BtnH) * 0.5f), BW, BtnH);
				const bool bWaiting = Pending == Id;
				if (D.Button(B, bWaiting ? FString(TEXT("Ждёт…")) : FString(TEXT("Вызвать")), bWaiting ? EFVButton::Normal : EFVButton::Primary, !bWaiting, TS))
				{
					TrialPick = Id;
					Act = EAct::Trial;
				}
				Y += RowH;
				if (i < N - 1) { D.Divider(FVector2D(LX, Y - 5.f), CW); }
			}
			Y += 4.f;
			const FFVRect Back(LX, Y, FMath::RoundToFloat((CW - 8.f) * 0.5f), BtnH);
			const FFVRect Off(Back.Right() + 8.f, Y, CW - Back.W - 8.f, BtnH);
			if (D.Button(Back, TEXT("Назад"), EFVButton::Normal, true, TS)) { Page = EPage::Settings; }
			if (D.Button(Off, TEXT("Выключить режим"), EFVButton::Danger, true, TS - 0.5f))
			{
				if (UFVSaveGame* P = Game ? Game->GetProfile() : nullptr)
				{
					P->bDevTools = false;
					Game->GetFishing().StartLegendTrial(NAME_None);
					Game->SaveNow();
				}
				Page = EPage::Settings;
			}
			return Act;
		}
		bool bInit = false;
		int32 TickCount = 0;
		float SfxVol = 0.8f;
		float MusicVol = 0.45f;
		bool bSfxDirty = false;
		FFVScrollView HelpScroll;       // portrait help: the whole page scrolls smoothly (finger drag, fling, wheel, keys)
		FFVScrollView HelpScrollSide;   // landscape help: the right column (fishing steps + tips)
		bool bHelpToEnd = false;        // debug screenshots: jump to the end of the help once it is laid out

		/** «Учебник» button with a red bubble counting the unread guide entries. */
		bool GuideButton(FVDraw& D, const FFVRect& R, float TextSize, EFVButton Style = EFVButton::Normal)
		{
			const bool bHit = D.Button(R, TEXT("Учебник"), Style, true, TextSize);
			const int32 Unseen = Game ? FVTutorialUnseenCount(Game) : 0;
			if (Unseen > 0)
			{
				FVUI::CountBubble(D, FVector2D(R.Right() - 4.f, R.Y + 1.f), Unseen);
			}
			return bHit;
		}

		// ---- update 4: the fisherman's profile, the leaderboard, the player number and the online status

		UFVOnline* Online() const { return Game ? Game->GetOnline() : nullptr; }

		/** The autosave note (+ the server backup once the player is registered). */
		FString SaveNote() const
		{
			const UFVOnline* O = Online();
			return (O && O->IsRegistered())
				? FString(TEXT("Игра сохраняется автоматически после каждой покупки, поездки и раз в минуту. Копия хранится на сервере игры."))
				: FString(TEXT("Игра сохраняется автоматически после каждой покупки, поездки и раз в минуту."));
		}

		/** «Профиль» with a red bubble counting the unread letters. */
		bool ProfileMenuButton(FVDraw& D, const FFVRect& R, float TextSize)
		{
			const bool bHit = D.Button(R, TEXT("Профиль"), EFVButton::Normal, true, TextSize);
			const UFVOnline* O = Online();
			const int32 Unread = O ? O->UnreadMailCount() : 0;
			if (Unread > 0)
			{
				FVUI::CountBubble(D, FVector2D(R.Right() - 4.f, R.Y + 1.f), Unread);
			}
			return bHit;
		}

		/** [Профиль | Рейтинг] row. */
		EAct SocialRow(FVDraw& D, const FFVRect& Row, float TextSize, float Gap)
		{
			EAct Act = EAct::None;
			if (ProfileMenuButton(D, FVDraw::SplitH(Row, 0, 2, Gap), TextSize)) { Act = EAct::Profile; }
			if (D.Button(FVDraw::SplitH(Row, 1, 2, Gap), TEXT("Рейтинг"), EFVButton::Normal, true, TextSize)) { Act = EAct::Leaderboard; }
			return Act;
		}

		FString PlayerNumberText() const
		{
			const UFVOnline* O = Online();
			const FString Tag = O ? O->GetTagText() : FString();
			return Tag.IsEmpty() ? FString(TEXT("Номер игрока: ещё не выдан")) : FString::Printf(TEXT("Номер игрока: %s"), *Tag);
		}

		// ---- update 4b: the offline mode («Подключиться») and the outcome of a new check started from here
		enum class EGateRow : uint8 { None, Offline, Checking, NoConnection, UpdateRequired, UpdateAvailable };

		EGateRow GateRow() const
		{
			const UFVOnline* O = Online();
			if (!O || !O->IsEnabled())
			{
				return EGateRow::None;
			}
			if (O->IsOfflineMode())
			{
				return EGateRow::Offline;
			}
			switch (O->GetStartupState())
			{
			case EFVStartupState::Checking: return EGateRow::Checking;
			case EFVStartupState::NoConnection: return EGateRow::NoConnection;
			case EFVStartupState::UpdateRequired: return EGateRow::UpdateRequired;
			case EFVStartupState::UpdateAvailable: return EGateRow::UpdateAvailable;
			default: return EGateRow::None;   // Ready / Idle; Banned: the status line already names the reason
			}
		}

		/** The button(s) of the online row in R (offline: «Подключиться»; no connection: «Повторить» / «Офлайн»;
		    an update: «Скачать» / «Позже»). The calls never close this screen, so they run right here. */
		void GateButtons(FVDraw& D, const FFVRect& R, float TextSize)
		{
			UFVOnline* O = Online();
			if (!O)
			{
				return;
			}
			const float Gap = FV::IsPortrait() ? 8.f : 6.f;
			switch (GateRow())
			{
			case EGateRow::Offline:
				if (D.Button(R, TEXT("Подключиться"), EFVButton::Primary, true, TextSize)) { O->RetryOnline(); }
				break;
			case EGateRow::Checking:
				D.Button(R, TEXT("Подключение…"), EFVButton::Normal, false, TextSize);
				break;
			case EGateRow::NoConnection:
				if (D.Button(FVDraw::SplitH(R, 0, 2, Gap), TEXT("Повторить"), EFVButton::Primary, true, TextSize)) { O->StartupCheck(); }
				if (D.Button(FVDraw::SplitH(R, 1, 2, Gap), TEXT("Офлайн"), EFVButton::Normal, true, TextSize)) { O->EnterOfflineMode(); }
				break;
			case EGateRow::UpdateRequired:
				if (D.Button(R, TEXT("Скачать обновление"), EFVButton::Primary, true, TextSize)) { O->OpenDownloadPage(); }
				break;
			case EGateRow::UpdateAvailable:
				if (D.Button(FVDraw::SplitH(R, 0, 2, Gap), TEXT("Обновить"), EFVButton::Primary, true, TextSize)) { O->OpenDownloadPage(); }
				if (D.Button(FVDraw::SplitH(R, 1, 2, Gap), TEXT("Позже"), EFVButton::Normal, true, TextSize)) { O->SkipOptionalUpdate(); }
				break;
			default:
				break;
			}
		}

		/** Height of the pause-menu online row (status line + buttons), 0 when there is nothing to offer. */
		float GateRowH(FVDraw& D, float TextSize, float BtnH) const
		{
			return GateRow() == EGateRow::None ? 0.f : D.LineHeight(TextSize, true) + 4.f + BtnH;
		}

		/** Pause menu: dot + status (centered in W) and the button(s) below it. Returns the height used. */
		float DrawGateRow(FVDraw& D, float X, float Y, float W, float TextSize, float BtnH, float BtnTextSize)
		{
			if (GateRow() == EGateRow::None)
			{
				return 0.f;
			}
			const FString Text = D.Ellipsize(GateRowText(), W - 14.f, TextSize, true);
			const float TW = D.MeasureText(Text, TextSize, true).X;
			const float SX = FMath::RoundToFloat(X + (W - TW - 10.f) * 0.5f);
			const float LH = D.LineHeight(TextSize, true);
			DrawStatusDot(D, FVector2D(SX + 3.f, Y + FMath::RoundToFloat(LH * 0.5f)));
			D.Text(Text, FVector2D(SX + 10.f, Y), TextSize, FVPal::InkSoft(), EFVAlign::Left, false, true);
			GateButtons(D, FFVRect(X, Y + LH + 4.f, W, BtnH), BtnTextSize);
			return LH + 4.f + BtnH;
		}

		/** Short status for the pause-menu row. */
		FString GateRowText() const
		{
			const FString NewName = LatestVersionName(Online());
			switch (GateRow())
			{
			case EGateRow::Offline: return TEXT("Офлайн-режим");
			case EGateRow::Checking: return TEXT("Подключение…");
			case EGateRow::NoConnection: return TEXT("Нет связи с сервером");
			case EGateRow::UpdateRequired: return NewName.IsEmpty() ? FString(TEXT("Нужна новая версия")) : FString::Printf(TEXT("Нужна версия %s"), *NewName);
			case EGateRow::UpdateAvailable: return NewName.IsEmpty() ? FString(TEXT("Доступна новая версия")) : FString::Printf(TEXT("Доступна версия %s"), *NewName);
			default: return FString();
			}
		}

		/** The coloured status dot (a small spinner while a check runs). */
		void DrawStatusDot(FVDraw& D, const FVector2D& C) const
		{
			if (GateRow() == EGateRow::Checking)
			{
				DrawSpinner(D, C, 3.2f, 0.9f, FV::Col(200, 140, 40));
				return;
			}
			D.Circle(C, 3.f, FV::Col(60, 36, 24));
			D.Circle(C, 2.f, StatusColor());
		}

		FString StatusText() const
		{
			const UFVOnline* O = Online();
			if (!O)
			{
				return TEXT("Онлайн недоступен");
			}
			const FString NewName = LatestVersionName(O);
			// 0.4.1: without a connection the reason goes on its own line (players send screenshots of this)
			const FString Why = O->GetLastNetError();
			const bool bNoLink = GateRow() == EGateRow::Offline || GateRow() == EGateRow::NoConnection
				|| (GateRow() == EGateRow::None && O->GetStatus() == EFVOnlineStatus::Offline);
			const FString Tail = (bNoLink && !Why.IsEmpty()) ? FString::Printf(TEXT("\n(%s)"), *Why) : FString();
			switch (GateRow())
			{
			case EGateRow::Offline: return FString(TEXT("Офлайн-режим: рейтинг, почта и копия на сервере вернутся, когда появится связь")) + Tail;
			case EGateRow::Checking: return TEXT("Подключение к серверу…");
			case EGateRow::UpdateRequired:
				return NewName.IsEmpty() ? FString(TEXT("Нужна новая версия игры — онлайн недоступен"))
					: FString::Printf(TEXT("Нужна новая версия %s — онлайн недоступен"), *NewName);
			case EGateRow::UpdateAvailable:
				return NewName.IsEmpty() ? FString(TEXT("Доступна новая версия игры")) : FString::Printf(TEXT("Доступна новая версия %s"), *NewName);
			default: return O->GetStatusText() + Tail;
			}
		}

		FLinearColor StatusColor() const
		{
			const UFVOnline* O = Online();
			switch (GateRow())
			{
			case EGateRow::Offline: return FV::Col(150, 128, 110);
			case EGateRow::Checking: return FV::Col(200, 140, 40);
			case EGateRow::NoConnection:
			case EGateRow::UpdateRequired: return FV::Col(200, 56, 44);
			case EGateRow::UpdateAvailable: return FV::Col(220, 170, 40);
			default: break;
			}
			switch (O ? O->GetStatus() : EFVOnlineStatus::Disabled)
			{
			case EFVOnlineStatus::Online: return FV::Col(70, 150, 40);
			case EFVOnlineStatus::Connecting: return FV::Col(200, 140, 40);
			case EFVOnlineStatus::Banned: return FV::Col(200, 56, 44);
			default: return FV::Col(150, 128, 110);
			}
		}

		/** Height of the status block (number line + wrapped status line) at width W. */
		float OnlineInfoH(FVDraw& D, float W, float Size, float SmallSize) const
		{
			return D.LineHeight(Size, true) + 2.f + D.MeasureWrapped(StatusText(), W - 10.f, SmallSize);
		}

		/** "Номер игрока: #1042" and, below it, a coloured dot + the online status ("В сети", "Нет связи с сервером"...). */
		float DrawOnlineInfo(FVDraw& D, float X, float Y, float W, float Size, float SmallSize)
		{
			D.Text(PlayerNumberText(), FVector2D(X, Y), Size, FVPal::Ink(), EFVAlign::Left, false, true);
			const float SY = Y + D.LineHeight(Size, true) + 2.f;
			const float DotY = SY + FMath::RoundToFloat(D.LineHeight(SmallSize, false) * 0.5f);
			DrawStatusDot(D, FVector2D(X + 3.f, DotY));
			const float H = D.TextWrapped(StatusText(), FVector2D(X + 10.f, SY), W - 10.f, SmallSize, FVPal::InkSoft());
			return SY + H - Y;
		}

		EAct DrawMain(FVDraw& D)
		{
			// update 4b: + an online row (offline mode «Подключиться», a running check, an update) when there is one
			const float GateH = GateRowH(D, 7.f, 18.f);
			const float ExtraH = GateH > 0.f ? GateH + 8.f : 0.f;
			const FFVRect R(220.f, FMath::RoundToFloat(68.f - ExtraH * 0.5f), 200.f, 224.f + ExtraH);
			EAct Act = EAct::None;
			if (D.Window(R, TEXT("Пауза"))) { Act = EAct::Resume; }

			float Y = R.Y + 14.f;
			if (Game)
			{
				const FString Line1 = FString::Printf(TEXT("День %d · %s · %s"), Game->GetDay(), *Game->GetClockText(), *FVDB::WeatherName(Game->GetWeather()));
				D.Text(Line1, FVector2D(R.Center().X, Y), 7.f, FVPal::InkSoft(), EFVAlign::Center, false, true);
				Y += 11.f;
				if (UFVSaveGame* P = Game->GetProfile())
				{
					const FString Lvl = FString::Printf(TEXT("Уровень %d  ·"), P->GetLevel());
					const float LW = D.MeasureText(Lvl, 7.f, true).X;
					const float MW = D.MeasureText(FV::Money(P->Money), 7.f, true).X + 10.f;
					const float SX = R.Center().X - (LW + 4.f + MW) * 0.5f;
					D.Text(Lvl, FVector2D(SX, Y), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, true);
					D.MoneyLabel(FVector2D(SX + LW + 4.f, Y), P->Money, 7.f, EFVAlign::Left, FVPal::InkSoft());
				}
				Y += 14.f;
			}
			D.Divider(FVector2D(R.X + 12.f, Y), R.W - 24.f);
			Y += 7.f;

			const float BX = R.X + 16.f, BW = R.W - 32.f, BH = 22.f, Gap = 6.f;
			if (GateH > 0.f)
			{
				Y += DrawGateRow(D, BX, Y, BW, 7.f, 18.f, 8.f) + 8.f;
			}
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Продолжить"), EFVButton::Primary, true, 9.f)) { Act = EAct::Resume; }
			Y += BH + Gap;
			{
				// update 4: the fisherman's profile (unread mail bubble) and the leaderboard
				const EAct Social = SocialRow(D, FFVRect(BX, Y, BW, BH), 9.f, 6.f);
				if (Social != EAct::None) { Act = Social; }
			}
			Y += BH + Gap;
			{
				// the guide sits right next to the settings
				const FFVRect Row(BX, Y, BW, BH);
				if (D.Button(FVDraw::SplitH(Row, 0, 2, 6.f), TEXT("Настройки"), EFVButton::Normal, true, 9.f)) { Page = EPage::Settings; }
				if (GuideButton(D, FVDraw::SplitH(Row, 1, 2, 6.f), 9.f)) { Act = EAct::Guide; }
			}
			Y += BH + Gap;
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Как играть"), EFVButton::Normal, true, 9.f)) { Page = EPage::Help; HelpScrollSide.Reset(); }
			Y += BH + Gap;
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Сохранить и выйти в меню"), EFVButton::Normal, true, 8.5f)) { Act = EAct::MainMenu; }
			Y += BH + Gap;
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Выйти из игры"), EFVButton::Danger, true, 9.f)) { Act = EAct::Quit; }

			if (!FV::IsTouchUI()) D.Text(TEXT("Esc — вернуться к рыбалке"), FVector2D(R.Center().X, R.Bottom() + 4.f), 6.5f, FV::Col(255, 240, 220), EFVAlign::Center, true, true);
			return Act;
		}

		EAct DrawSettings(FVDraw& D)
		{
			// update 4: + the player number / online status and «Восстановить сохранение»
			const float CW = 280.f - 36.f;
			const FString Note = SaveNote();
			const float NoteH = D.MeasureWrapped(Note, CW, 7.f);
			const float InfoH = OnlineInfoH(D, CW, 8.f, 7.f);
			const float WH = FMath::RoundToFloat(22.f + 60.f + (FV::IsTouchUI() ? 0.f : 26.f) + 8.f + InfoH + 6.f + 18.f + 10.f + 8.f + NoteH + 8.f + 20.f + 12.f
				+ (DevTools() ? 26.f : 0.f));
			const FFVRect R(180.f, FMath::RoundToFloat(FMath::Max(28.f, (FV::VH - WH) * 0.5f)), 280.f, WH);
			EAct Act = EAct::None;
			if (D.Window(R, TEXT("Настройки"))) { Page = EPage::Main; }
			const float LX = R.X + 18.f;
			const float SX = R.X + 88.f;
			const float SW = R.W - 88.f - 52.f;
			float Y = R.Y + 22.f;

			// sound effects
			D.Text(TEXT("Звуки"), FVector2D(LX, Y), 9.f, FVPal::Ink(), EFVAlign::Left, false, true);
			if (D.Slider(FFVRect(SX, Y + 1.f, SW, 12.f), SfxVol) && Game)
			{
				Game->SetSfxVolume(SfxVol);
				bSfxDirty = true;
			}
			D.Text(FVUI::Percent(SfxVol), FVector2D(R.Right() - 18.f, Y), 8.f, FVPal::InkSoft(), EFVAlign::Right, false, true);
			if (bSfxDirty && !D.IsDragging())
			{
				bSfxDirty = false;
				FVUI::Sfx(EFVSfx::Coin);   // preview at the new volume
			}
			Y += 30.f;

			// music
			D.Text(TEXT("Музыка"), FVector2D(LX, Y), 9.f, FVPal::Ink(), EFVAlign::Left, false, true);
			if (D.Slider(FFVRect(SX, Y + 1.f, SW, 12.f), MusicVol) && Game)
			{
				Game->SetMusicVolume(MusicVol);
			}
			D.Text(FVUI::Percent(MusicVol), FVector2D(R.Right() - 18.f, Y), 8.f, FVPal::InkSoft(), EFVAlign::Right, false, true);
			Y += 30.f;

			// fullscreen (desktop only: phones are always fullscreen)
			if (!FV::IsTouchUI())
			{
				bool bFull = IsFullscreenNow();
				if (D.Checkbox(FFVRect(LX, Y, 190.f, 14.f), bFull, TEXT("Полный экран"), 9.f) && Game)
				{
					Game->ToggleFullscreen();
				}
				D.Text(TEXT("или клавиша F11"), FVector2D(LX + 124.f, Y + 3.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += 26.f;
			}

			D.Divider(FVector2D(LX, Y), R.W - 36.f);
			Y += 8.f;
			// online: the player number, the status, restoring a save by a one-time code
			Y += DrawOnlineInfo(D, LX, Y, CW, 8.f, 7.f) + 6.f;
			if (GateRow() != EGateRow::None)
			{
				// update 4b: offline / no connection / an update: connecting comes first (restoring needs the server)
				GateButtons(D, FFVRect(R.Center().X - 80.f, Y, 160.f, 18.f), 8.f);
			}
			else if (D.Button(FFVRect(R.Center().X - 80.f, Y, 160.f, 18.f), TEXT("Восстановить сохранение"), EFVButton::Normal, true, 8.f))
			{
				Act = EAct::Restore;
			}
			if (GateRow() == EGateRow::None && !FV::IsTouchUI() && D.Hover(FFVRect(R.Center().X - 80.f, Y, 160.f, 18.f)))
			{
				D.Tooltip(TEXT("Восстановить сохранение"), TEXT("Вернуть прогресс по одноразовому коду (его выдаёт администратор игры)."));
			}
			Y += 18.f + 10.f;
			if (DevTools())
			{
				// 0.5 developer mode (5 taps on the version in the main menu)
				if (D.Button(FFVRect(LX, Y - 4.f, 120.f, 18.f), TEXT("Бой с легендой (тест)"), EFVButton::Primary, true, 7.5f)) { Page = EPage::Trials; }
				if (D.Button(FFVRect(LX + 124.f, Y - 4.f, CW - 124.f, 18.f), TEXT("Режим разработчика"), EFVButton::Primary, true, 7.5f)) { Page = EPage::Dev; OpenDevPanel(); }
				Y += 26.f;
			}
			D.Divider(FVector2D(LX, Y), R.W - 36.f);
			Y += 8.f;
			D.TextWrapped(Note, FVector2D(LX, Y), CW, 7.f, FVPal::InkSoft());

			if (D.Button(FFVRect(R.Center().X - 50.f, R.Bottom() - 32.f, 100.f, 20.f), TEXT("Назад"), EFVButton::Normal, true, 9.f))
			{
				Page = EPage::Main;
			}
			return Act;
		}

		EAct DrawHelp(FVDraw& D)
		{
			const FFVRect R(30.f, 30.f, 580.f, 306.f);
			EAct Act = EAct::None;
			if (D.Window(R, TEXT("Как играть"))) { Page = EPage::Main; }

			// ---- left: controls ----
			const float LX = R.X + 16.f;
			const float LW = 236.f;
			float Y = FVUI::SectionHeader(D, TEXT("Управление"), FVector2D(LX, R.Y + 14.f), LW, 10.f);
			for (const FKeyRow& Row : HelpRows())
			{
				const float KW = FVUI::KeyCap(D, Row.Keys, FVector2D(LX, Y), 6.5f);
				D.Text(D.Ellipsize(Row.What, LW - KW - 6.f, 7.f, false), FVector2D(LX + KW + 6.f, Y + 1.f), 7.f, FVPal::Ink(), EFVAlign::Left, false, false);
				Y += 14.f;
			}

			// animated mini bobber-bar illustration
			Y += 4.f;
			DrawMiniBar(D, FFVRect(LX, Y, LW, R.Bottom() - Y - 12.f));

			// ---- right: fishing steps + tips, one smoothly scrolling column ----
			const float RX = R.X + 268.f;
			const float RW = R.Right() - RX - 22.f;
			const FFVRect Area(RX - 2.f, R.Y + 12.f, RW + 12.f, R.Bottom() - 36.f - (R.Y + 12.f));
			const float HeadH = FMath::RoundToFloat(10.f * 1.45f + 4.f);
			const float TipHeadH = FMath::RoundToFloat(9.f * 1.45f + 4.f);
			float ContentH = 2.f + HeadH;
			for (const TCHAR* S : HelpSteps()) { ContentH += D.MeasureWrapped(S, RW, 7.f, false, 1.05f) + 3.f; }
			ContentH += 2.f + TipHeadH;
			for (const TCHAR* S : HelpTips()) { ContentH += D.MeasureWrapped(S, RW, 7.f, false, 1.05f) + 2.f; }
			ContentH += 4.f;
			HelpScrollSide.Update(D, Area, ContentH);
			if (bHelpToEnd) { HelpScrollSide.ScrollTo(HelpScrollSide.MaxOffset(), true); bHelpToEnd = false; }

			D.PushClip(Area);
			float RY = Area.Y + 2.f - HelpScrollSide.Offset();
			RY = FVUI::SectionHeader(D, TEXT("Рыбалка"), FVector2D(RX, RY), RW, 10.f);
			for (const TCHAR* S : HelpSteps())
			{
				RY += D.TextWrapped(S, FVector2D(RX, RY), RW, 7.f, FVPal::Ink(), false, 1.05f) + 3.f;
			}
			RY += 2.f;
			RY = FVUI::SectionHeader(D, TEXT("Советы"), FVector2D(RX, RY), RW, 9.f);
			for (const TCHAR* S : HelpTips())
			{
				RY += D.TextWrapped(S, FVector2D(RX, RY), RW, 7.f, FVPal::InkSoft(), false, 1.05f) + 2.f;
			}
			D.PopClip();
			HelpScrollSide.DrawFades(D, Area, FVPal::Parchment());
			HelpScrollSide.DrawBar(D, Area);

			// ---- bottom row: the guide (big, green) + back ----
			const float BY = R.Bottom() - 28.f;
			if (D.Button(FFVRect(RX, BY, 170.f, 20.f), TEXT("Открыть учебник"), EFVButton::Primary, true, 8.5f)) { Act = EAct::Guide; }
			if (D.Button(FFVRect(R.Right() - 100.f, BY, 84.f, 20.f), TEXT("Назад"), EFVButton::Normal, true, 8.f))
			{
				Page = EPage::Main;
			}
			return Act;
		}

		// ---------------------------------------------------------------- help texts (shared by both layouts)
		struct FKeyRow { const TCHAR* Keys; const TCHAR* What; };

		static TArrayView<const FKeyRow> HelpRows()
		{
			static const FKeyRow Rows[] = {
				{ TEXT("WASD / стрелки"), TEXT("ходить") },
				{ TEXT("Пробел / ЛКМ"), TEXT("заброс, подсечка, вываживание") },
				{ TEXT("E / ЛКМ"), TEXT("взять, открыть, войти — если предмет выделен") },
				{ TEXT("M"), TEXT("карта и поездки (и домой)") },
				{ TEXT("I / B"), TEXT("инвентарь: садок и рюкзак") },
				{ TEXT("G"), TEXT("гардероб") },
				{ TEXT("C / J"), TEXT("коллекция / достижения") },
				{ TEXT("F1 / P"), TEXT("учебник / профиль рыбака") },
				{ TEXT("Esc"), TEXT("меню, отмена заброса") },
				{ TEXT("F11"), TEXT("полный экран") } };
			static const FKeyRow TouchRows[] = {
				{ TEXT("Свайп"), TEXT("идти (джойстик там, где коснулись)") },
				{ TEXT("Зажать на месте"), TEXT("замах и заброс у воды") },
				{ TEXT("Касание"), TEXT("подсечка; держать — вываживание") },
				{ TEXT("Касание экрана"), TEXT("взять выделенный предмет") },
				{ TEXT("Иконки вверху"), TEXT("карта, инвентарь, гардероб, коллекция") },
				{ TEXT("Иконки вверху"), TEXT("достижения, учебник, профиль") },
				{ TEXT("Назад"), TEXT("закрыть окно / пауза") },
				{ TEXT("Свайп в окнах"), TEXT("прокрутка и вкладки") } };
			// portrait phones: the main screens live in the bottom dock, the menu and the guide are at the top-right
			static const FKeyRow PortraitTouchRows[] = {
				{ TEXT("Свайп"), TEXT("идти (джойстик появляется там, где коснулись)") },
				{ TEXT("Зажать на месте"), TEXT("у воды: замах, отпустите — заброс") },
				{ TEXT("Касание"), TEXT("подсечка; держать палец — вываживание") },
				{ TEXT("Касание экрана"), TEXT("предмет рядом выделен — возьмёте его, откроете лавку или дверь, куда бы ни коснулись") },
				{ TEXT("Кнопки внизу"), TEXT("карта, инвентарь, гардероб, коллекция, достижения") },
				{ TEXT("Вверху справа"), TEXT("меню, учебник и профиль рыбака") },
				{ TEXT("Значок уровня"), TEXT("что открывает каждый уровень") },
				{ TEXT("Свайп вбок"), TEXT("в окнах — соседняя вкладка") },
				{ TEXT("Свайп вверх-вниз"), TEXT("прокрутка списков") },
				{ TEXT("Назад"), TEXT("закрыть окно (кнопка внизу слева)") } };
			if (FV::IsTouchUI())
			{
				return FV::IsPortrait() ? TArrayView<const FKeyRow>(PortraitTouchRows) : TArrayView<const FKeyRow>(TouchRows);
			}
			return TArrayView<const FKeyRow>(Rows);
		}

		static TArray<const TCHAR*> HelpSteps()
		{
			if (FV::IsTouchUI())
			{
				return {
					TEXT("1. Подойдите к воде или встаньте на мостки. Зажмите палец на месте — шкала силы качается. Отпустите, чтобы забросить: чем сильнее, тем дальше и глубже."),
					TEXT("2. Ждите поклёвки. Когда над головой вспыхнет «!», быстро коснитесь экрана — подсечка!"),
					TEXT("3. Вываживание: держите палец на экране — зелёная зона поднимается, отпустите — опускается. Держите рыбу в зоне, пока шкала улова не заполнится.") };
			}
			return {
				TEXT("1. Подойдите к воде или встаньте на мостки. Зажмите Пробел или ЛКМ — шкала силы качается. Отпустите, чтобы забросить: чем сильнее, тем дальше и глубже."),
				TEXT("2. Ждите поклёвки. Когда над головой вспыхнет «!», быстро нажмите — подсечка!"),
				TEXT("3. Вываживание: держите кнопку — зелёная зона поднимается, отпустите — опускается. Держите рыбу в зоне, пока шкала улова не заполнится.") };
		}

		static TArray<const TCHAR*> HelpTips()
		{
			return {
				FV::IsTouchUI() ? TEXT("• Зона скользит по инерции — касайтесь короткими нажатиями.") : TEXT("• Зона скользит по инерции — жмите короткими нажатиями."),
				TEXT("• Не выпустили рыбу ни разу — «идеальный» улов и качество выше."),
				TEXT("• Появился сундук? Наведите на него зону, чтобы забрать сокровище."),
				TEXT("• Редкая рыба держится глубже и клюёт в своё время и погоду."),
				!FV::IsTouchUI() ? TEXT("• Подойдите к предмету, прилавку или двери — они подсветятся. Теперь хватит клика в любом месте или клавиши E.")
					: TEXT("• Подойдите к предмету, прилавку или двери — они подсветятся. Теперь хватит касания в любом месте экрана."),
				TEXT("• Лавка Митрича — у пруда: там продают улов и покупают снасти. Дом с сундуком и станками — на карте."),
				TEXT("• Про наживку, снасти, дом и ремёсла подробно рассказано в учебнике — новые статьи появляются по мере игры.") };
		}

		// ---------------------------------------------------------------- portrait (phones held upright)
		// The pause menu is a bottom sheet: the world stays visible above it and every button sits in the lower half,
		// with the main action ("Продолжить") lowest, right under the thumb.
		EAct DrawMainPortrait(FVDraw& D)
		{
			const FFVRect Win = FV::WindowRect();
			const float BH = 40.f, Gap = 8.f, PrimaryGap = 14.f, Pad = 16.f;
			const float InfoH = 40.f;
			const int32 NumSecondary = 5;
			// update 4b: + an online row (offline mode «Подключиться», a running check, an update) when there is one
			const float GateTS = FV::Ui::Body(), GateBH = 36.f;
			const float GateH = GateRowH(D, GateTS, GateBH);
			const float ExtraH = GateH > 0.f ? GateH + 12.f : 0.f;
			const float H = 22.f + InfoH + 12.f + ExtraH + NumSecondary * (BH + Gap) - Gap + PrimaryGap + BH + Pad;
			const FFVRect R(Win.X, FMath::RoundToFloat(Win.Bottom() - H), Win.W, H);
			EAct Act = EAct::None;
			if (D.Window(R, TEXT("Пауза"))) { Act = EAct::Resume; }

			// secondary info on top of the sheet
			if (Game)
			{
				const float TS = FV::Ui::Body();
				const float Y = R.Y + 20.f;
				const FString Line1 = FString::Printf(TEXT("День %d · %s · %s"), Game->GetDay(), *Game->GetClockText(), *FVDB::WeatherName(Game->GetWeather()));
				D.Text(Line1, FVector2D(R.Center().X, Y), TS, FVPal::InkSoft(), EFVAlign::Center, false, true);
				if (UFVSaveGame* P = Game->GetProfile())
				{
					const float LY = Y + 17.f;
					const FString Lvl = FString::Printf(TEXT("Уровень %d  ·"), P->GetLevel());
					const float LW = D.MeasureText(Lvl, TS, true).X;
					const float MW = D.MeasureText(FV::Money(P->Money), TS, true).X + 10.f;
					const float SX = FMath::RoundToFloat(R.Center().X - (LW + 4.f + MW) * 0.5f);
					D.Text(Lvl, FVector2D(SX, LY), TS, FVPal::InkSoft(), EFVAlign::Left, false, true);
					D.MoneyLabel(FVector2D(SX + LW + 4.f, LY), P->Money, TS, EFVAlign::Left, FVPal::InkSoft());
				}
			}
			float Y = R.Y + 22.f + InfoH;
			D.Divider(FVector2D(R.X + 14.f, Y), R.W - 28.f);
			Y += 12.f;

			// top -> bottom: [online row], quit, save & main menu, help, settings, [gap] resume
			const float BX = R.X + Pad, BW = R.W - 2.f * Pad;
			if (GateH > 0.f)
			{
				Y += DrawGateRow(D, BX, Y, BW, GateTS, GateBH, FV::Ui::Body() + 2.f) + 12.f;
			}
			const float TS = FV::Ui::Body() + 2.5f;
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Выйти из игры"), EFVButton::Danger, true, TS)) { Act = EAct::Quit; }
			Y += BH + Gap;
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Сохранить и выйти в меню"), EFVButton::Normal, true, TS)) { Act = EAct::MainMenu; }
			Y += BH + Gap;
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Как играть"), EFVButton::Normal, true, TS)) { Page = EPage::Help; HelpScroll.Reset(); }
			Y += BH + Gap;
			{
				// update 4: the fisherman's profile (unread mail bubble) and the leaderboard
				const EAct Social = SocialRow(D, FFVRect(BX, Y, BW, BH), TS, 8.f);
				if (Social != EAct::None) { Act = Social; }
			}
			Y += BH + Gap;
			{
				// the guide sits right next to the settings
				const FFVRect Row(BX, Y, BW, BH);
				if (D.Button(FVDraw::SplitH(Row, 0, 2, 8.f), TEXT("Настройки"), EFVButton::Normal, true, TS)) { Page = EPage::Settings; }
				if (GuideButton(D, FVDraw::SplitH(Row, 1, 2, 8.f), TS)) { Act = EAct::Guide; }
			}
			Y += BH + PrimaryGap;
			if (D.Button(FFVRect(BX, Y, BW, BH), TEXT("Продолжить"), EFVButton::Primary, true, TS + 1.f)) { Act = EAct::Resume; }
			return Act;
		}

		/** Settings as a bottom sheet: two big volume sliders (finger-draggable), the note, "← Назад" + "Продолжить". */
		EAct DrawSettingsPortrait(FVDraw& D)
		{
			const FFVRect Win = FV::WindowRect();
			const float CW = Win.W - 40.f;
			const bool bFullscreenRow = !FV::IsTouchUI();   // phones are always fullscreen
			const FString Note = SaveNote();
			const float NoteSize = FV::Ui::Small() + 0.5f;
			const float NoteH = D.MeasureWrapped(Note, CW, NoteSize);
			const float RowH = 66.f;
			const float BarZone = FV::Ui::ButtonH() + 10.f + 12.f;   // ActionBarRect + the divider above it
			// update 4: the player number / online status and «Восстановить сохранение»
			const float InfoSize = FV::Ui::Body() + 0.5f, InfoSmall = FV::Ui::Small() + 0.5f;
			const float OnlineH = 8.f + OnlineInfoH(D, CW, InfoSize, InfoSmall) + 10.f + FV::Ui::ButtonH() + 14.f;
			const float H = FMath::RoundToFloat(28.f + 2.f * RowH + (bFullscreenRow ? 40.f : 0.f) + OnlineH + 6.f + NoteH + 14.f + BarZone
				+ (DevTools() ? FV::Ui::ButtonH() + 10.f : 0.f));
			const FFVRect R(Win.X, FMath::RoundToFloat(Win.Bottom() - H), Win.W, H);
			EAct Act = EAct::None;
			if (D.Window(R, TEXT("Настройки"))) { Page = EPage::Main; }

			const float LX = R.X + 20.f;
			float Y = R.Y + 26.f;
			// label + percent on one line, a wide finger-sized slider below (hit area >= 36 px tall)
			auto VolumeRow = [&D, &Y, LX, CW, RowH](const TCHAR* Label, float& Value) -> bool
			{
				const float LS = FV::Ui::Body() + 1.5f;
				D.Text(Label, FVector2D(LX, Y), LS, FVPal::Ink(), EFVAlign::Left, true, true);
				D.Text(FVUI::Percent(Value), FVector2D(LX + CW, Y + 1.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Right, false, true);
				const bool bChanged = D.Slider(FFVRect(LX + 10.f, Y + 26.f, CW - 20.f, 24.f), Value);
				Y += RowH;
				return bChanged;
			};
			if (VolumeRow(TEXT("Звуки"), SfxVol) && Game)
			{
				Game->SetSfxVolume(SfxVol);
				bSfxDirty = true;
			}
			if (bSfxDirty && !D.IsDragging())
			{
				bSfxDirty = false;
				FVUI::Sfx(EFVSfx::Coin);   // preview at the new volume
			}
			if (VolumeRow(TEXT("Музыка"), MusicVol) && Game)
			{
				Game->SetMusicVolume(MusicVol);
			}
			if (bFullscreenRow)
			{
				bool bFull = IsFullscreenNow();
				if (D.Checkbox(FFVRect(LX, Y, CW, 20.f), bFull, TEXT("Полный экран (F11)"), FV::Ui::Body() + 1.f) && Game)
				{
					Game->ToggleFullscreen();
				}
				Y += 40.f;
			}
			D.Divider(FVector2D(LX, Y), CW);
			Y += 8.f;
			Y += DrawOnlineInfo(D, LX, Y, CW, InfoSize, InfoSmall) + 10.f;
			if (GateRow() != EGateRow::None)
			{
				// update 4b: offline / no connection / an update: connecting comes first (restoring needs the server)
				GateButtons(D, FFVRect(LX, Y, CW, FV::Ui::ButtonH()), FV::Ui::Body() + 1.5f);
			}
			else if (D.Button(FFVRect(LX, Y, CW, FV::Ui::ButtonH()), TEXT("Восстановить сохранение"), EFVButton::Normal, true, FV::Ui::Body() + 1.5f))
			{
				Act = EAct::Restore;
			}
			Y += FV::Ui::ButtonH() + 14.f;
			if (DevTools())
			{
				// 0.5 developer mode (5 taps on the version in the main menu)
				const float HW = FMath::RoundToFloat((CW - 8.f) * 0.5f);
				if (D.Button(FFVRect(LX, Y - 4.f, HW, FV::Ui::ButtonH()), TEXT("Бой с легендой"), EFVButton::Primary, true, FV::Ui::Body() + 0.5f)) { Page = EPage::Trials; }
				if (D.Button(FFVRect(LX + HW + 8.f, Y - 4.f, CW - HW - 8.f, FV::Ui::ButtonH()), TEXT("Разработчик"), EFVButton::Primary, true, FV::Ui::Body() + 0.5f)) { Page = EPage::Dev; OpenDevPanel(); }
				Y += FV::Ui::ButtonH() + 10.f;
			}
			D.Divider(FVector2D(LX, Y), CW);
			Y += 8.f;
			D.TextWrapped(Note, FVector2D(LX, Y), CW, NoteSize, FVPal::InkSoft());

			FFVRect Actions;
			if (D.ActionBar(R, &Actions)) { Page = EPage::Main; }
			if (D.Button(FVDraw::SplitH(Actions, 0, 1), TEXT("Продолжить"), EFVButton::Primary, true, FV::Ui::Body() + 1.5f)) { Act = EAct::Resume; }
			return Act;
		}

		// ---- portrait help: one list of blocks in a smooth scroll view ----
		struct FHelpBlock
		{
			enum class EKind : uint8 { Header, Key, Step, Tip, MiniBar, Guide } Kind = EKind::Step;
			FString A, B;
			float H = 0.f;
		};

		static constexpr float HelpHeaderSize = 12.f;
		static constexpr float HelpKeySize = 8.5f;

		void BuildHelpBlocks(const FVDraw& D, float W, TArray<FHelpBlock>& Out)
		{
			Out.Reset();
			const float TextSize = FV::Ui::Body();
			const float TipSize = FV::Ui::Body() - 0.5f;
			const float HeaderH = FMath::RoundToFloat(HelpHeaderSize * 1.45f + 4.f + 8.f);
			auto Header = [&Out, HeaderH](const TCHAR* Title)
			{
				FHelpBlock B; B.Kind = FHelpBlock::EKind::Header; B.A = Title; B.H = HeaderH; Out.Add(B);
			};
			// controls table: the key column is as wide as the widest key cap
			float KeyColW = 14.f;
			for (const FKeyRow& Row : HelpRows())
			{
				KeyColW = FMath::Max(KeyColW, D.MeasureText(Row.Keys, HelpKeySize, true).X + 8.f);
			}
			KeyColW = FMath::Min(KeyColW, W * 0.45f);
			{
				// the guide is one tap away (the button in the action bar)
				FHelpBlock B; B.Kind = FHelpBlock::EKind::Guide;
				B.A = TEXT("Про наживку, снасти, дом и ремёсла — в учебнике. Он пополняется по мере игры.");
				B.H = FMath::RoundToFloat(FMath::Max(40.f, D.MeasureWrapped(B.A, W - 44.f, TipSize, false, 1.05f) + 16.f)) + 8.f;
				Out.Add(B);
			}
			Header(TEXT("Управление"));
			for (const FKeyRow& Row : HelpRows())
			{
				FHelpBlock B;
				B.Kind = FHelpBlock::EKind::Key;
				B.A = Row.Keys;
				B.B = Row.What;
				B.H = FMath::RoundToFloat(FMath::Max(24.f, D.MeasureWrapped(Row.What, W - KeyColW - 10.f, TextSize, false, 1.05f) + 8.f));
				Out.Add(B);
			}
			Header(TEXT("Вываживание"));
			{
				FHelpBlock B; B.Kind = FHelpBlock::EKind::MiniBar; B.H = 124.f; Out.Add(B);
			}
			Header(TEXT("Рыбалка"));
			for (const TCHAR* S : HelpSteps())
			{
				FHelpBlock B; B.Kind = FHelpBlock::EKind::Step; B.A = S;
				B.H = FMath::RoundToFloat(D.MeasureWrapped(S, W, TextSize, false, 1.05f) + 7.f);
				Out.Add(B);
			}
			Header(TEXT("Советы"));
			for (const TCHAR* S : HelpTips())
			{
				FHelpBlock B; B.Kind = FHelpBlock::EKind::Tip; B.A = S;
				B.H = FMath::RoundToFloat(D.MeasureWrapped(S, W, TipSize, false, 1.05f) + 5.f);
				Out.Add(B);
			}
			HelpKeyColW = KeyColW;
		}

		void DrawHelpBlock(FVDraw& D, const FHelpBlock& B, float X, float Y, float W)
		{
			const float TextSize = FV::Ui::Body();
			switch (B.Kind)
			{
			case FHelpBlock::EKind::Header:
				FVUI::SectionHeader(D, B.A, FVector2D(X, Y + 2.f), W, HelpHeaderSize);
				break;
			case FHelpBlock::EKind::Key:
				FVUI::KeyCap(D, D.Ellipsize(B.A, HelpKeyColW - 8.f, HelpKeySize, true), FVector2D(X, Y + 1.f), HelpKeySize);
				D.TextWrapped(B.B, FVector2D(X + HelpKeyColW + 10.f, Y + 1.5f), W - HelpKeyColW - 10.f, TextSize, FVPal::Ink(), false, 1.05f);
				break;
			case FHelpBlock::EKind::MiniBar:
				DrawMiniBar(D, FFVRect(X, Y, W, B.H - 8.f), TextSize, FV::Ui::Small() + 0.5f, B.H - 8.f);
				break;
			case FHelpBlock::EKind::Step:
				D.TextWrapped(B.A, FVector2D(X, Y), W, TextSize, FVPal::Ink(), false, 1.05f);
				break;
			case FHelpBlock::EKind::Tip:
				D.TextWrapped(B.A, FVector2D(X, Y), W, TextSize - 0.5f, FVPal::InkSoft(), false, 1.05f);
				break;
			case FHelpBlock::EKind::Guide:
			{
				// a soft "tip" card with the book icon
				const FFVRect Card(X, Y, W, B.H - 8.f);
				D.Rect(Card, FV::Col(196, 150, 90));
				D.Rect(Card.Inset(1.f), FV::Col(255, 240, 200));
				D.Rect(FFVRect(Card.X + 1.f, Card.Y + 1.f, 3.f, Card.H - 2.f), FV::Col(106, 190, 48));
				if (const FFVSprite* Book = FVUI::Icon(EFVIcon::Book))
				{
					FVUI::SpriteFit(D, *Book, FFVRect(Card.X + 8.f, Card.Center().Y - 13.f, 26.f, 26.f), FLinearColor::White, 2.f);
				}
				D.TextWrapped(B.A, FVector2D(Card.X + 40.f, Card.Y + 8.f), W - 44.f, TextSize - 0.5f, FVPal::Ink(), false, 1.05f);
				break;
			}
			}
		}

		/** Portrait help: full window, the blocks scroll smoothly (finger drag + fling, wheel, arrow keys),
		    "← Назад" + the big «Открыть учебник». */
		EAct DrawHelpPortrait(FVDraw& D)
		{
			const FFVRect R = FV::WindowRect();
			EAct Act = EAct::None;
			if (D.Window(R, TEXT("Как играть"))) { Page = EPage::Main; return Act; }

			const float X = R.X + 16.f;
			const float W = R.W - 32.f - 6.f;           // room for the scroll bar
			const float Top = R.Y + 18.f;
			const float Bottom = FVDraw::ActionBarRect(R).Y - 12.f;
			const FFVRect Area(R.X + 8.f, Top, R.W - 16.f, FMath::Max(40.f, Bottom - Top));
			TArray<FHelpBlock> Blocks;
			BuildHelpBlocks(D, W, Blocks);
			float Total = 8.f;
			for (const FHelpBlock& B : Blocks) { Total += B.H; }
			HelpScroll.Update(D, Area, Total);
			if (bHelpToEnd) { HelpScroll.ScrollTo(HelpScroll.MaxOffset(), true); bHelpToEnd = false; }

			D.PushClip(Area);
			float Y = Area.Y + 4.f - HelpScroll.Offset();
			for (const FHelpBlock& B : Blocks)
			{
				if (Y + B.H >= Area.Y && Y <= Area.Bottom()) { DrawHelpBlock(D, B, X, Y, W); }
				Y += B.H;
			}
			D.PopClip();
			HelpScroll.DrawFades(D, Area, FVPal::Parchment());
			HelpScroll.DrawBar(D, Area);

			FFVRect Actions;
			const bool bBack = D.ActionBar(R, &Actions);
			if (D.Button(FVDraw::SplitH(Actions, 0, 1), TEXT("Открыть учебник"), EFVButton::Primary, true, FV::Ui::Body() + 1.5f)) { Act = EAct::Guide; }
			if (bBack) { Page = EPage::Main; }
			return Act;
		}

		float HelpKeyColW = 60.f;          // portrait help: width of the key-cap column (set by BuildHelpBlocks)

		/** A tiny animated version of the reeling minigame to explain the controls. */
		void DrawMiniBar(FVDraw& D, const FFVRect& Area, float TextSize = 7.f, float SmallSize = 6.5f, float MaxH = 96.f)
		{
			if (Area.H < 50.f) { return; }
			const float T = float(D.Time);
			const float H = FMath::Min(Area.H, MaxH);
			const FFVRect Panel(Area.X + 6.f, Area.Y, 26.f, H);
			D.Rect(Panel, FVPal::WoodDark());
			D.Rect(Panel.Inset(1.f), FVPal::Wood());
			D.Rect(FFVRect(Panel.X + 1.f, Panel.Y + 1.f, Panel.W - 2.f, 1.f), FVPal::WoodLight());
			const FFVRect Track(Panel.X + 5.f, Panel.Y + 4.f, 16.f, H - 8.f);
			D.Rect(Track, FV::Col(40, 90, 150));
			// fish drifts smoothly; the bar follows with a lag (like a player)
			const float FishU = 0.5f + 0.38f * FMath::Sin(T * 0.9f) + 0.08f * FMath::Sin(T * 2.7f);
			const float BarU = 0.5f + 0.36f * FMath::Sin(T * 0.9f - 0.35f);
			const float BarH = Track.H * 0.34f;
			const float BarY = Track.Y + (Track.H - BarH) * FMath::Clamp(BarU, 0.f, 1.f);
			const float FishY = Track.Y + (Track.H - 12.f) * FMath::Clamp(FishU, 0.f, 1.f);
			const bool bIn = FishY + 6.f >= BarY && FishY + 6.f <= BarY + BarH;
			D.Rect(FFVRect(Track.X, BarY, Track.W, BarH), bIn ? FV::Col(120, 230, 90, 220) : FV::Col(90, 190, 70, 170));
			if (const FFVSprite* FishIcon = FVUI::Icon(EFVIcon::Fish))
			{
				D.SpriteCentered(*FishIcon, FVector2D(Track.X + Track.W * 0.5f, FishY + 6.f), FMath::Min(1.f, 16.f / FMath::Max(1.f, FishIcon->W)));
			}
			else
			{
				D.Rect(FFVRect(Track.X + 3.f, FishY + 3.f, Track.W - 6.f, 6.f), FV::Col(240, 170, 60));
			}
			// progress meter
			const float Prog = 0.5f + 0.45f * FMath::Sin(T * 0.5f);
			const FFVRect Meter(Panel.Right() + 2.f, Panel.Y + 2.f, 5.f, H - 4.f);
			D.Rect(Meter, FVPal::WoodDark());
			const float MH = (Meter.H - 2.f) * Prog;
			D.Rect(FFVRect(Meter.X + 1.f, Meter.Bottom() - 1.f - MH, 3.f, MH), FVUI::LerpLinear(FVPal::Red(), FVPal::Green(), Prog));

			const float TX = Meter.Right() + 10.f;
			const float TW = Area.Right() - TX;
			const bool bTouch = FV::IsTouchUI();
			float Y = Area.Y + 2.f;
			Y += D.TextWrapped(bTouch ? TEXT("Палец на экране — зона вверх") : TEXT("Держите — зона вверх"), FVector2D(TX, Y), TW, TextSize, FVPal::Ink(), true) + 2.f;
			Y += D.TextWrapped(bTouch ? TEXT("Отпустите палец — зона вниз") : TEXT("Отпустите — зона вниз"), FVector2D(TX, Y), TW, TextSize, FVPal::Ink(), true) + 4.f;
			Y += D.TextWrapped(TEXT("Рыба в зоне — шкала справа растёт. Вне зоны — убывает. Полная шкала — рыба ваша!"), FVector2D(TX, Y), TW, SmallSize, FVPal::InkSoft(), false, 1.05f);
		}
	};
}

TUniquePtr<FVScreen> FVMakeMainMenuScreen(UFVGame* Game)
{
	return MakeUnique<FVMenusScreenImpl::FVMainMenuScreen>(Game);
}

TUniquePtr<FVScreen> FVMakePauseScreen(UFVGame* Game)
{
	return MakeUnique<FVMenusScreenImpl::FVPauseScreen>(Game);
}
