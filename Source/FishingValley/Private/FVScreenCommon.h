// Small helpers shared by the UI-module screens (Menus, Map, Inventory, Storage, Shop and the update-2 home screens).
// Private header: not part of the public contract.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVDraw.h"
#include "FVSprites.h"
#include "FVAudio.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVGame.h"
#include "FVScreens.h"

namespace FVUI
{
	inline void Sfx(EFVSfx S, float Volume = 1.f, float Pitch = 1.f)
	{
		if (UFVAudio* A = UFVAudio::Get()) { A->Play(S, Volume, Pitch); }
	}

	/** Icon sprite or nullptr when the sprite system is not ready. */
	inline const FFVSprite* Icon(EFVIcon I)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp) { return nullptr; }
		const FFVSprite& S = Sp->Icon(I);
		return S.IsValid() ? &S : nullptr;
	}

	/** Russian plural form: Plural(5, "монета", "монеты", "монет") -> "монет". */
	inline const TCHAR* Plural(int64 N, const TCHAR* One, const TCHAR* Few, const TCHAR* Many)
	{
		const int64 A = N < 0 ? -N : N;
		const int64 N100 = A % 100;
		const int64 N10 = A % 10;
		if (N100 > 10 && N100 < 20) { return Many; }
		if (N10 == 1) { return One; }
		if (N10 >= 2 && N10 <= 4) { return Few; }
		return Many;
	}

	inline FString Coins(int64 N)
	{
		return FString::Printf(TEXT("%s %s"), *FV::Money(N), Plural(N, TEXT("монета"), TEXT("монеты"), TEXT("монет")));
	}

	/** 1.25 -> "1,25", 1.2 -> "1,2", 2.0 -> "2" (Russian decimal comma). */
	inline FString Num(float V, int32 MaxDecimals = 2)
	{
		FString S = MaxDecimals <= 0 ? FString::Printf(TEXT("%.0f"), V)
			: (MaxDecimals == 1 ? FString::Printf(TEXT("%.1f"), V) : FString::Printf(TEXT("%.2f"), V));
		if (S.Contains(TEXT(".")))
		{
			while (S.EndsWith(TEXT("0"))) { S.LeftChopInline(1); }
			if (S.EndsWith(TEXT("."))) { S.LeftChopInline(1); }
		}
		S.ReplaceInline(TEXT("."), TEXT(","));
		return S;
	}

	inline FString Percent(float Frac)
	{
		return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Frac * 100.f));
	}

	/** Accent color of a location theme. */
	inline FLinearColor ThemeColor(EFVTheme T)
	{
		switch (T)
		{
		case EFVTheme::Pond: return FV::Col(96, 186, 90);
		case EFVTheme::River: return FV::Col(64, 150, 214);
		case EFVTheme::Mountain: return FV::Col(150, 170, 206);
		case EFVTheme::Ocean: return FV::Col(40, 110, 200);
		case EFVTheme::Swamp: return FV::Col(120, 140, 70);
		case EFVTheme::Volcano: return FV::Col(236, 96, 40);
		default: return FV::Col(200, 160, 100);
		}
	}

	/** Russian explanation of a failed purchase. */
	inline FString BuyErrorText(EFVBuyResult R)
	{
		switch (R)
		{
		case EFVBuyResult::AlreadyOwned: return TEXT("Уже есть");
		case EFVBuyResult::NotEnoughMoney: return TEXT("Не хватает монет");
		case EFVBuyResult::LevelTooLow: return TEXT("Слишком низкий уровень");
		case EFVBuyResult::NotForSale: return TEXT("Не продаётся");
		case EFVBuyResult::NotEnoughMaterials: return TEXT("Не хватает материалов");
		case EFVBuyResult::NoRoom: return TEXT("Нет места");
		case EFVBuyResult::StationRequired: return TEXT("Нужна постройка в доме");
		case EFVBuyResult::MaxLevel: return TEXT("Уже максимальный уровень");
		case EFVBuyResult::Locked: return TEXT("Сначала нужно достижение");
		case EFVBuyResult::Invalid: default: return TEXT("Не получилось");
		}
	}

	/** Plays Buy/Error sfx and shows a toast. Returns true on success. */
	inline bool ReportBuy(UFVGame* G, EFVBuyResult R, const FString& SuccessText)
	{
		if (R == EFVBuyResult::Ok)
		{
			Sfx(EFVSfx::Buy);
			if (G) { G->Toast(SuccessText, FVPal::Gold(), EFVIcon::Check); }
			return true;
		}
		Sfx(EFVSfx::Error);
		if (G) { G->Toast(BuyErrorText(R), FV::Col(255, 150, 130), EFVIcon::Exclaim); }
		return false;
	}

	/** Icon of ANY item id (fish, junk, bait, generic item, rod, decor) via UFVSprites::ItemIcon; nullptr when the sprite
	    system is not ready. The pointer points into the sprite cache: use it right away (or copy it, see ItemIconOf). */
	inline const FFVSprite* ItemSprite(FName ItemId)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp || ItemId.IsNone()) { return nullptr; }
		const FFVSprite& S = Sp->ItemIcon(ItemId);
		return S.IsValid() ? &S : nullptr;
	}

	/** Same as ItemSprite, copied by value (safe to keep while other sprites are generated). */
	inline bool ItemIconOf(FName ItemId, FFVSprite& Out)
	{
		const FFVSprite* S = ItemSprite(ItemId);
		if (!S) { return false; }
		Out = *S;
		return true;
	}

	/** Scale that fits a sprite into MaxW x MaxH: integer up-scale when it fits (pixel-perfect), fractional down-scale otherwise. */
	inline float FitScale(const FFVSprite& S, float MaxW, float MaxH, float MaxUp = 3.f)
	{
		if (S.W <= 0.f || S.H <= 0.f) { return 1.f; }
		const float Fit = FMath::Min(MaxW / S.W, MaxH / S.H);
		if (Fit >= 1.f) { return FMath::Clamp(FMath::FloorToFloat(Fit), 1.f, MaxUp); }
		return Fit;
	}

	/** Draws a sprite centered in Box, fitted with FitScale. */
	inline void SpriteFit(FVDraw& D, const FFVSprite& S, const FFVRect& Box, const FLinearColor& Tint = FLinearColor::White, float MaxUp = 3.f)
	{
		if (!S.IsValid()) { return; }
		const float M = FitScale(S, Box.W, Box.H, MaxUp);
		const FVector2D C = Box.Center();
		const float W = S.W * M, H = S.H * M;
		D.SpriteRect(S, FFVRect(FMath::RoundToFloat(C.X - W * 0.5f), FMath::RoundToFloat(C.Y - H * 0.5f), W, H), Tint);
	}

	/** Section title + engraved divider. Returns the Y below it. */
	inline float SectionHeader(FVDraw& D, const FString& Title, const FVector2D& Pos, float Width, float Size = 10.f)
	{
		D.Text(Title, Pos, Size, FVPal::Ink(), EFVAlign::Left, true, true);
		const float Y = Pos.Y + Size * 1.45f;
		D.Divider(FVector2D(Pos.X, Y), Width);
		return Y + 4.f;
	}

	/** Keyboard key cap ("M", "Пробел"). Returns its width. */
	inline float KeyCap(FVDraw& D, const FString& Key, const FVector2D& Pos, float Size = 7.f)
	{
		const FVector2D Sz = D.MeasureText(Key, Size, true);
		const float W = FMath::Max(Sz.X + 8.f, 14.f);
		const float H = Sz.Y + 2.f;
		const FFVRect R(Pos.X, Pos.Y, W, H);
		D.Rect(R.Offset(0.f, 1.f), FV::Col(120, 70, 36));
		D.Rect(R, FV::Col(92, 44, 22));
		D.Rect(R.Inset(1.f), FV::Col(255, 238, 206));
		D.Rect(FFVRect(R.X + 1.f, R.Bottom() - 2.f, R.W - 2.f, 1.f), FV::Col(214, 170, 120));
		D.Text(Key, FVector2D(R.X + W * 0.5f, R.Y + 1.f), Size, FVPal::Ink(), EFVAlign::Center, false, true);
		return W;
	}

	/** Deterministic hash noise 0..1 for procedural art. */
	inline float Hash01(int32 X, int32 Y, int32 Seed = 0)
	{
		uint32 H = uint32(X) * 374761393u + uint32(Y) * 668265263u + uint32(Seed) * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return float(H & 0xFFFFFF) / float(0xFFFFFF);
	}

	/** Smooth value noise 0..1 (bilinear-interpolated hash lattice). */
	inline float ValueNoise(float X, float Y, int32 Seed = 0)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = X - X0, FY = Y - Y0;
		const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY);
		const float A = Hash01(X0, Y0, Seed), B = Hash01(X0 + 1, Y0, Seed);
		const float C = Hash01(X0, Y0 + 1, Seed), E = Hash01(X0 + 1, Y0 + 1, Seed);
		return FMath::Lerp(FMath::Lerp(A, B, SX), FMath::Lerp(C, E, SX), SY);
	}

	/** Alpha-over blend on a CPU image (local helper, independent of FFVImage::Blend). */
	inline void BlendPx(FFVImage& Img, int32 X, int32 Y, FColor C)
	{
		if (!Img.InBounds(X, Y) || C.A == 0) { return; }
		if (C.A == 255) { Img.Set(X, Y, C); return; }
		const FColor D = Img.Get(X, Y);
		const float A = C.A / 255.f;
		const float DA = D.A / 255.f;
		const float OA = A + DA * (1.f - A);
		if (OA <= 0.f) { return; }
		auto Mix = [&](uint8 S, uint8 T) { return uint8(FMath::Clamp(FMath::RoundToInt((S * A + T * DA * (1.f - A)) / OA), 0, 255)); };
		Img.Set(X, Y, FColor(Mix(C.R, D.R), Mix(C.G, D.G), Mix(C.B, D.B), uint8(FMath::Clamp(FMath::RoundToInt(OA * 255.f), 0, 255))));
	}

	inline void DiskPx(FFVImage& Img, float CX, float CY, float R, FColor C)
	{
		const int32 X0 = FMath::FloorToInt(CX - R), X1 = FMath::CeilToInt(CX + R);
		const int32 Y0 = FMath::FloorToInt(CY - R), Y1 = FMath::CeilToInt(CY + R);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
				if (DX * DX + DY * DY <= R * R) { BlendPx(Img, X, Y, C); }
			}
		}
	}

	inline void EllipsePx(FFVImage& Img, float CX, float CY, float RX, float RY, FColor C)
	{
		if (RX <= 0.f || RY <= 0.f) { return; }
		const int32 X0 = FMath::FloorToInt(CX - RX), X1 = FMath::CeilToInt(CX + RX);
		const int32 Y0 = FMath::FloorToInt(CY - RY), Y1 = FMath::CeilToInt(CY + RY);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const float DX = (X + 0.5f - CX) / RX, DY = (Y + 0.5f - CY) / RY;
				if (DX * DX + DY * DY <= 1.f) { BlendPx(Img, X, Y, C); }
			}
		}
	}

	inline void RectPx(FFVImage& Img, int32 X, int32 Y, int32 W, int32 H, FColor C)
	{
		for (int32 j = 0; j < H; ++j)
		{
			for (int32 i = 0; i < W; ++i) { BlendPx(Img, X + i, Y + j, C); }
		}
	}

	inline FColor LerpColor(FColor A, FColor B, float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return FColor(uint8(FMath::RoundToInt(A.R + (B.R - A.R) * T)), uint8(FMath::RoundToInt(A.G + (B.G - A.G) * T)),
			uint8(FMath::RoundToInt(A.B + (B.B - A.B) * T)), uint8(FMath::RoundToInt(A.A + (B.A - A.A) * T)));
	}

	inline FLinearColor LerpLinear(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return A + (B - A) * FMath::Clamp(T, 0.f, 1.f);
	}

	// ---------------------------------------------------------------------------------------------
	// Additions for the portrait (phone) layouts
	// ---------------------------------------------------------------------------------------------

	/** Rarity color that stays readable on parchment (FVDB::RarityColor is made for dark backgrounds). */
	inline FLinearColor RarityInk(EFVRarity R)
	{
		switch (R)
		{
		case EFVRarity::Uncommon: return FV::Col(58, 128, 36);
		case EFVRarity::Rare: return FV::Col(38, 96, 190);
		case EFVRarity::Epic: return FV::Col(128, 56, 186);
		case EFVRarity::Legendary: return FV::Col(184, 116, 10);
		default: return FVPal::Ink();
		}
	}

	enum class EChip : uint8 { Good, Bad, Neutral };

	/** Small status label ("Открыто", "Закрыто"...) right-aligned at RightTop, optional lock icon. Returns its width. */
	inline float StatusChip(FVDraw& D, const FString& Label, const FVector2D& RightTop, EChip Kind, bool bLockIcon = false, float Size = 7.5f)
	{
		const FVector2D Sz = D.MeasureText(Label, Size, true);
		const float IconW = bLockIcon ? 11.f : 0.f;
		const float W = FMath::RoundToFloat(Sz.X + 10.f + IconW);
		const float H = FMath::RoundToFloat(Sz.Y + 4.f);
		const FFVRect R(FMath::RoundToFloat(RightTop.X - W), FMath::RoundToFloat(RightTop.Y), W, H);
		FLinearColor Border = FV::Col(176, 110, 56), Fill = FV::Col(255, 236, 196), TextC = FVPal::InkSoft();
		if (Kind == EChip::Good) { Border = FV::Col(96, 150, 56); Fill = FV::Col(214, 238, 160); TextC = FV::Col(52, 116, 34); }
		else if (Kind == EChip::Bad) { Border = FV::Col(170, 70, 50); Fill = FV::Col(250, 206, 180); TextC = FV::Col(170, 44, 34); }
		D.Rect(R, Border);
		D.Rect(R.Inset(1.f), Fill);
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Alpha(FVPal::White(), 0.45f));
		float TX = R.X + 5.f;
		if (bLockIcon)
		{
			if (const FFVSprite* Lock = Icon(EFVIcon::Lock))
			{
				SpriteFit(D, *Lock, FFVRect(TX - 1.f, R.Y + 1.f, 10.f, H - 2.f), FLinearColor::White, 1.f);
			}
			TX += IconW;
		}
		D.Text(Label, FVector2D(TX, FMath::RoundToFloat(R.Y + (H - Sz.Y) * 0.5f)), Size, TextC, EFVAlign::Left, false, true);
		return W;
	}

	/** 0.6.2 (Р84): colour of a rod type plate (one per expert type; the classic rods — warm neutral). */
	inline FLinearColor RodTypeColor(const FFVRodDef& R)
	{
		if (!R.bExpert) { return FV::Col(128, 104, 80); }
		switch (R.ExpertType)
		{
		case EFVExpertRodType::Pendulum: return FV::Col(34, 136, 146);
		case EFVExpertRodType::Twin: return FV::Col(64, 92, 184);
		case EFVExpertRodType::Compass: return FV::Col(176, 118, 30);
		case EFVExpertRodType::Magnet: return FV::Col(168, 58, 48);
		case EFVExpertRodType::Helm: return FV::Col(132, 62, 168);
		default: return FV::Col(128, 104, 80);
		}
	}

	/** 0.6.2 (Р84): a small plate with the rod type («Маятник» / «Двойчатка» / «Импульс» / «Магнит» / «Ползунок»,
	    «Обычная» for the classic rods), LEFT-aligned at LeftTop. Returns its width (use it to place the next text). */
	inline float RodTypeChip(FVDraw& D, const FFVRodDef& R, const FVector2D& LeftTop, float Size = 7.f)
	{
		const FString Label = FVDBU6::RodTypeLabel(R);
		const FVector2D Sz = D.MeasureText(Label, Size, true);
		const float W = FMath::RoundToFloat(Sz.X + 8.f);
		const float H = FMath::RoundToFloat(Sz.Y + 3.f);
		const FFVRect Rc(FMath::RoundToFloat(LeftTop.X), FMath::RoundToFloat(LeftTop.Y), W, H);
		const FLinearColor C = RodTypeColor(R);
		D.Rect(Rc, FLinearColor(C.R * 0.6f, C.G * 0.6f, C.B * 0.6f, 1.f));
		D.Rect(Rc.Inset(1.f), C);
		D.Rect(FFVRect(Rc.X + 1.f, Rc.Y + 1.f, Rc.W - 2.f, 1.f), FV::Alpha(FVPal::White(), 0.35f));
		D.Text(Label, FVector2D(Rc.X + 4.f, FMath::RoundToFloat(Rc.Y + (H - Sz.Y) * 0.5f)), Size, FVPal::White(), EFVAlign::Left, false, true);
		return W;
	}

	/** Thin vertical scroll indicator for lists scrolled with In.Wheel / finger drags. Draws nothing when all fits. */
	inline void ScrollBar(FVDraw& D, const FFVRect& Track, float First, float Visible, float Total)
	{
		if (Total <= Visible || Total <= 0.f || Track.H <= 4.f) { return; }
		D.Rect(Track, FV::Col(176, 110, 56, 90));
		const float ThumbH = FMath::Max(12.f, Track.H * Visible / Total);
		const float K = FMath::Clamp(First / FMath::Max(1.f, Total - Visible), 0.f, 1.f);
		const FFVRect Thumb(Track.X, FMath::RoundToFloat(Track.Y + (Track.H - ThumbH) * K), Track.W, ThumbH);
		D.Rect(Thumb, FVPal::Wood());
		D.Rect(FFVRect(Thumb.X, Thumb.Y, 1.f, Thumb.H), FVPal::WoodLight());
	}

	/** Splits " · "-joinable parts into at most two lines that fit MaxWidth (the 2nd line is ellipsized if needed). */
	inline TArray<FString> PackParts(const FVDraw& D, const TArray<FString>& Parts, float MaxWidth, float Size, bool bBold = false)
	{
		TArray<FString> Lines;
		FString Cur;
		for (const FString& Part : Parts)
		{
			const FString Try = Cur.IsEmpty() ? Part : Cur + TEXT(" · ") + Part;
			if (Cur.IsEmpty() || D.MeasureText(Try, Size, bBold).X <= MaxWidth || Lines.Num() >= 1)
			{
				Cur = Try;
			}
			else
			{
				Lines.Add(Cur);
				Cur = Part;
			}
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
		for (FString& L : Lines) { L = D.Ellipsize(L, MaxWidth, Size, bBold); }
		return Lines;
	}

	// ---------------------------------------------------------------------------------------------
	// Portrait overlay windows: the standard bottom action bar is FVDraw::ActionBar ("← Назад" + primary actions).
	// Screens with more actions add one extra row of big buttons right above it (same footer band).
	// ---------------------------------------------------------------------------------------------

	/** Rect of an extra button row directly above FVDraw::ActionBarRect(Win). Content should end at its Y - 10. */
	inline FFVRect ActionBarExtraRowRect(const FFVRect& Win)
	{
		const FFVRect Bar = FVDraw::ActionBarRect(Win);
		return FFVRect(Bar.X, Bar.Y - 14.f - FV::Ui::ButtonH(), Bar.W, FV::Ui::ButtonH());
	}

	/** Draws the footer band + divider behind the extra row (FVDraw::ActionBar draws its own divider under it,
	    which separates the two rows). Returns the row rect; split it with FVDraw::SplitH. */
	inline FFVRect ActionBarExtraRow(FVDraw& D, const FFVRect& Win)
	{
		const FFVRect Row = ActionBarExtraRowRect(Win);
		const FFVRect Bar = FVDraw::ActionBarRect(Win);
		const float BandY = Row.Y - 7.f;
		D.Rect(FFVRect(Win.X + 5.f, BandY, Win.W - 10.f, FMath::Max(0.f, (Bar.Y - 8.f) - BandY)), FV::Alpha(FVPal::ParchDark(), 0.35f));
		D.Divider(FVector2D(Win.X + 6.f, BandY - 1.f), Win.W - 12.f);
		D.MarkUI(FFVRect(Row.X, BandY, Row.W, Bar.Y - BandY));
		return Row;
	}

	/** Big button with a label and a coin amount under it ("Продать 1" / "+45"). Amount < 0: label only.
	    Returns true when pressed (same rules as FVDraw::Button). */
	inline bool PriceButton(FVDraw& D, const FFVRect& R, const FString& Label, int64 Amount, EFVButton Style = EFVButton::Normal, bool bEnabled = true)
	{
		const bool bPressed = D.Button(R, FString(), Style, bEnabled);
		const bool bDown = bEnabled && D.Hover(R) && D.Input().bLmbDown;
		const float DY = bDown ? 1.f : 0.f;
		const bool bLight = bEnabled && (Style == EFVButton::Primary || Style == EFVButton::Danger);
		const FLinearColor TextC = !bEnabled ? FV::Col(128, 112, 98) : (bLight ? FV::Col(255, 255, 240) : FVPal::Ink());
		const FLinearColor OutlineC = Style == EFVButton::Danger ? FV::Col(90, 20, 16) : FV::Col(32, 72, 24);
		float LS = FV::Ui::Body() + 1.f;
		while (LS > 6.f && D.MeasureText(Label, LS, true).X > R.W - 8.f) { LS -= 0.5f; }
		const float MS = FV::Ui::Small() + 0.5f;
		const bool bAmount = Amount >= 0 && R.H >= 26.f;
		const float LH = D.LineHeight(LS, true);
		const float MH = bAmount ? D.LineHeight(MS, true) : 0.f;
		const float Total = LH + (bAmount ? MH - 1.f : 0.f);
		const float TY = FMath::RoundToFloat(R.Y + (R.H - Total) * 0.5f - 0.5f + DY);
		const FString L = D.Ellipsize(Label, R.W - 6.f, LS, true);
		if (bLight) { D.TextOutlined(L, FVector2D(R.Center().X, TY), LS, TextC, EFVAlign::Center, OutlineC); }
		else { D.Text(L, FVector2D(R.Center().X, TY), LS, TextC, EFVAlign::Center, bEnabled, true); }
		if (bAmount)
		{
			const FLinearColor MC = !bEnabled ? TextC : (bLight ? FV::Col(255, 244, 200) : FV::Col(120, 72, 20));
			D.MoneyLabel(FVector2D(R.Center().X, TY + LH - 1.f), Amount, MS, EFVAlign::Center, MC);
		}
		return bPressed;
	}

	// ---------------------------------------------------------------------------------------------
	// Update 2 (UI-A): item containers, stacks, trade — shared by Inventory, Storage, Shop and the home screens
	// ---------------------------------------------------------------------------------------------

	inline EFVQuality QualityOf(uint8 Q)
	{
		return EFVQuality(FMath::Clamp<int32>(Q, 0, int32(EFVQuality::Count) - 1));
	}

	/** "Садок", "Рюкзак", "Сундук", "Ледник", "Аквариум". */
	inline FString ContainerName(EFVContainer C)
	{
		switch (C)
		{
		case EFVContainer::Keepnet: return TEXT("Садок");
		case EFVContainer::Backpack: return TEXT("Рюкзак");
		case EFVContainer::Chest: return TEXT("Сундук");
		case EFVContainer::IceCellar: return TEXT("Ледник");
		case EFVContainer::Aquarium: return TEXT("Аквариум");
		default: return FString();
		}
	}

	/** Display name of a stack ("Копчёный карась" for smoked fish); the raw id when unknown. */
	inline FString StackTitle(const FFVInvStack& S)
	{
		FString N = FVDB::StackName(S.ItemId, S.Flags);
		if (N.IsEmpty()) { N = FVDB::ItemName(S.ItemId); }
		return N.IsEmpty() ? S.ItemId.ToString() : N;
	}

	/** Name colour of a stack on parchment (fish: rarity). */
	inline FLinearColor StackInk(const FFVInvStack& S)
	{
		if (const FFVFishDef* F = FVDB::FindFish(S.ItemId)) { return RarityInk(F->Rarity); }
		return FVPal::Ink();
	}

	/** Coins Дед Митрич pays for one unit: the reference price shown outside the shops ("У Митрича: N"). */
	inline int32 MitrichPrice(const FFVInvStack& S)
	{
		return FVDB::TraderSellPrice(FVDB::DefaultTraderId(), S.ItemId, QualityOf(S.Quality), S.Flags);
	}

	/** Category line of a stack: "Рыба · Редкая · Серебро", "Мусор", "Еда · Золото", "Леска · ранг 2". */
	inline FString StackKindText(const FFVInvStack& S)
	{
		const EFVQuality Q = QualityOf(S.Quality);
		if (const FFVFishDef* F = FVDB::FindFish(S.ItemId))
		{
			FString T = FString::Printf(TEXT("%s · %s"), *FVDB::RarityName(F->Rarity), *FVDB::QualityName(Q));
			if (S.Flags & FVStackFlags::Smoked) { T = TEXT("Копчёная · ") + T; }
			return T;
		}
		if (FVDB::FindJunk(S.ItemId)) { return TEXT("Мусор"); }
		if (const FFVItemDef* It = FVDB::FindItem(S.ItemId))
		{
			FString T = FVDB::ItemKindName(It->Kind);
			if (It->Kind == EFVItemKind::Food) { T += TEXT(" · ") + FVDB::QualityName(Q); }
			else if ((It->Kind == EFVItemKind::Line || It->Kind == EFVItemKind::Float || It->Kind == EFVItemKind::Net) && It->Tier > 0)
			{
				T += FString::Printf(TEXT(" · ранг %d"), int32(It->Tier));
			}
			return T;
		}
		if (FVDB::IsBaitId(S.ItemId)) { return TEXT("Наживка"); }
		return FString();
	}

	/** Russian detail lines of a stack (no name, no price): description, food effect, tackle bonuses, net stats, record. */
	inline TArray<FString> StackInfoLines(const FFVInvStack& S, const UFVSaveGame* P)
	{
		TArray<FString> Lines;
		const EFVQuality Q = QualityOf(S.Quality);
		if (const FFVFishDef* F = FVDB::FindFish(S.ItemId))
		{
			if (S.Flags & FVStackFlags::Smoked) { Lines.Add(TEXT("Копчёная рыба — стоит вдвое дороже")); }
			if (P)
			{
				if (const FFVFishRecord* Rec = P->GetRecord(F->Id))
				{
					if (Rec->MaxSize > 0.f) { Lines.Add(FString::Printf(TEXT("Рекорд: %s см"), *Num(Rec->MaxSize, 1))); }
				}
			}
			if (!F->Description.IsEmpty()) { Lines.Add(F->Description); }
			return Lines;
		}
		if (const FFVJunkDef* J = FVDB::FindJunk(S.ItemId))
		{
			if (!J->Description.IsEmpty()) { Lines.Add(J->Description); }
			Lines.Add(TEXT("В мастерской пойдёт на переработку"));
			return Lines;
		}
		const FFVItemDef* It = FVDB::FindItem(S.ItemId);
		if (!It) { return Lines; }
		switch (It->Kind)
		{
		case EFVItemKind::Food:
		{
			const float Power = FVDB::FoodPower(Q);
			for (const FString& L : FVDB::StatModsLines(It->Buff.Scaled(Power))) { Lines.Add(L); }
			if (It->BuffMinutes > 0.f) { Lines.Add(FString::Printf(TEXT("Действует %s"), *UFVGame::DurationText(It->BuffMinutes * Power))); }
			break;
		}
		case EFVItemKind::Line:
		case EFVItemKind::Float:
			for (const FString& L : FVDB::StatModsLines(It->Tackle)) { Lines.Add(L); }
			Lines.Add(TEXT("Ставится на удочку в мастерской («Оснастка»)"));
			break;
		case EFVItemKind::Net:
			Lines.Add(FString::Printf(TEXT("Улов до %d рыб · прочность %d"), It->NetCapacity, FMath::RoundToInt(It->NetDurability)));
			if (It->NetMinutesPerFish > 0.f)
			{
				Lines.Add(FString::Printf(TEXT("Примерно 1 рыба за %s"), *UFVGame::DurationText(It->NetMinutesPerFish)));
			}
			Lines.Add(TEXT("Ставится на берегу, у колышков для сетей"));
			break;
		default:
			break;
		}
		if (!It->Description.IsEmpty()) { Lines.Add(It->Description); }
		return Lines;
	}

	/** Contents of one container cell: icon, quality stars, count, smoked mark. bBig = portrait (52 px) cells. */
	inline void StackCellContent(FVDraw& D, const FFVRect& R, const FFVInvStack& S, bool bBig, bool bDim = false)
	{
		const FLinearColor Tint = bDim ? FLinearColor(1.f, 1.f, 1.f, 0.45f) : FLinearColor::White;
		FFVSprite Spr;
		if (ItemIconOf(S.ItemId, Spr))
		{
			SpriteFit(D, Spr, R.Inset(2.f), Tint, bBig ? 2.f : 1.f);
		}
		else
		{
			D.Text(TEXT("?"), FVector2D(R.Center().X, R.Y + (bBig ? 14.f : 6.f)), bBig ? 14.f : 9.f, FVPal::InkSoft(), EFVAlign::Center);
		}
		const EFVQuality Q = QualityOf(S.Quality);
		if (Q != EFVQuality::Normal)
		{
			D.Stars(FVector2D(R.X + (bBig ? 2.f : 1.f), R.Bottom() - (bBig ? 18.f : 9.f)), Q, bBig ? 2.f : 1.f);
		}
		if (S.Flags & FVStackFlags::Smoked)
		{
			// smoked fish: a little smoke puff in the top-left corner
			if (const FFVSprite* Smoke = Icon(EFVIcon::Smoke))
			{
				SpriteFit(D, *Smoke, FFVRect(R.X + 1.f, R.Y + 1.f, bBig ? 16.f : 9.f, bBig ? 16.f : 9.f), Tint, 1.f);
			}
		}
		if (S.Count > 1)
		{
			const float CS = bBig ? FV::Ui::Body() : 6.5f;
			D.TextOutlined(FString::FromInt(S.Count), FVector2D(R.Right() - (bBig ? 3.f : 2.f), R.Bottom() - (bBig ? D.LineHeight(CS) + 1.f : 11.f)), CS, FVPal::White(), EFVAlign::Right);
		}
		if (bDim)
		{
			D.Rect(R.Inset(1.f), FV::Col(236, 206, 156, 90));
		}
	}

	/** A free place of a container (faint cell). */
	inline void EmptyCell(FVDraw& D, const FFVRect& R)
	{
		D.Rect(R, FV::Col(176, 110, 56, 110));
		D.Rect(R.Inset(1.f), FV::Col(255, 226, 170, 120));
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(214, 150, 88, 90));
	}

	/** Capacity row: "Label used / cap" + a bar that turns orange / red when (almost) full. Returns the bar rect. */
	inline FFVRect CapacityBar(FVDraw& D, const FString& Label, int32 Used, int32 Cap, const FVector2D& Pos, float W, float Size, float BarH = 8.f)
	{
		const FString Text = FString::Printf(TEXT("%s %d / %d"), *Label, Used, Cap);
		const float LH = D.LineHeight(Size);
		D.Text(Text, Pos, Size, Used >= Cap && Cap > 0 ? FV::Col(170, 44, 34) : FVPal::Ink(), EFVAlign::Left, false, true);
		const float BX = Pos.X + FMath::Max(W * 0.3f, D.MeasureText(Text, Size, true).X + 8.f);
		const float Frac = Cap > 0 ? FMath::Clamp(float(Used) / float(Cap), 0.f, 1.f) : 1.f;
		const FLinearColor BarC = (Cap > 0 && Used >= Cap) ? FVPal::Red() : (Frac > 0.75f ? FV::Col(236, 170, 40) : FVPal::Green());
		const FFVRect BarR(FMath::RoundToFloat(BX), FMath::RoundToFloat(Pos.Y + LH * 0.5f - BarH * 0.5f), FMath::Max(10.f, Pos.X + W - BX), BarH);
		D.Bar(BarR, Frac, BarC);
		return BarR;
	}

	/** Update 3: the pointer is held down on R (current local coords, inside the active clip) and the gesture has not
	    turned into a drag — the "pressed" look of tappable list rows (their D.Clicked fires on the release). */
	inline bool IsPressingOn(const FVDraw& D, const FFVRect& R)
	{
		if (D.IsInputBlocked() || !D.IsPointerDown() || D.IsDragGesture() || !D.Input().bMouseValid) { return false; }
		const FVector2D M = D.Mouse();
		return R.Contains(M) && R.Contains(D.PressPos()) && D.InClip(M);
	}

	/** (Pre-update-3; new code uses FFVScrollView + D.Clicked, which never fires after a drag.)
	    Tap = press AND release on the same widget without scrolling in between (lists that scroll with finger drags
	    select on release). Keys are salted per widget. BeginFrame/EndFrame once per frame around the widgets. */
	struct FTapKeys
	{
		uint32 Key = 0;          // widget pressed and not released yet
		bool bMoved = false;     // the list scrolled since the press

		void BeginFrame(const FFVInput& In)
		{
			if (Key != 0 && In.Wheel != 0) { bMoved = true; }
		}
		/** bPressedNow = the widget's own click result this frame. Returns true on a clean release over R. */
		bool Tap(const FFVInput& In, bool bPressedNow, uint32 InKey, const FFVRect& R)
		{
			if (bPressedNow)
			{
				Key = InKey;
				bMoved = false;
			}
			if (Key == InKey && In.bLmbReleased && !bMoved && In.bMouseValid && R.Inset(FV::IsTouchUI() ? -4.f : 0.f).Contains(In.Mouse))
			{
				Key = 0;
				return true;
			}
			return false;
		}
		void EndFrame(const FFVInput& In)
		{
			if (In.bLmbReleased || !In.bLmbDown) { Key = 0; }
		}
	};

	/** (Pre-update-3; new code uses FFVScrollView.) Row scrolling of a grid kept as a pixel accumulator (touch drags
	    arrive as 14 px wheel steps, a mouse notch is one row). Applies the wheel when bApply, clamps, returns the first
	    visible row. */
	inline int32 GridScroll(float& ScrollPx, int32 Wheel, bool bApply, int32 TotalRows, int32 VisRows, float Pitch)
	{
		const int32 MaxFirst = FMath::Max(0, TotalRows - VisRows);
		if (bApply && Wheel != 0)
		{
			ScrollPx -= float(Wheel) * (FV::IsTouchUI() ? 14.f : Pitch);
		}
		ScrollPx = FMath::Clamp(ScrollPx, 0.f, float(MaxFirst) * Pitch);
		return FMath::Clamp(FMath::RoundToInt(ScrollPx / FMath::Max(1.f, Pitch)), 0, MaxFirst);
	}

	/** Folder tab with a finger-friendly label (shrinks to fit). Fires on press like FVDraw::Tab. */
	inline bool BigTab(FVDraw& D, const FFVRect& R, const FString& Label, bool bActive, float Size)
	{
		const bool bClicked = D.Tab(R, FString(), bActive);
		float S = Size;
		FVector2D Sz = D.MeasureText(Label, S);
		while (Sz.X > R.W - 6.f && S > 6.f)
		{
			S -= 0.5f;
			Sz = D.MeasureText(Label, S);
		}
		const float TopY = R.Y + (bActive ? 0.f : 2.f);
		const float TY = FMath::RoundToFloat(TopY + ((R.Bottom() - TopY) - Sz.Y) * 0.5f);
		D.Text(Label, FVector2D(R.Center().X, TY), S, bActive ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Center, bActive, true);
		return bClicked;
	}

	/** Small red counter bubble centred at C (e.g. the fish waiting in the keepnet). */
	inline void CountBubble(FVDraw& D, const FVector2D& C, int32 N)
	{
		const FString T = FString::FromInt(N);
		const float W = FMath::Max(15.f, D.MeasureText(T, 7.f).X + 8.f);
		const FFVRect R(FMath::RoundToFloat(C.X - W * 0.5f), FMath::RoundToFloat(C.Y - 7.5f), W, 15.f);
		D.Rect(R.Offset(1.f, 1.f), FV::Col(0, 0, 0, 70));
		D.Rect(R, FV::Col(120, 24, 20));
		D.Rect(R.Inset(1.f), FV::Col(214, 52, 52));
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(250, 140, 120));
		D.Text(T, FVector2D(R.Center().X, R.Y + 2.f), 7.f, FVPal::White(), EFVAlign::Center, false, true);
	}

	/** "Цена:" + coin amount (+ " за шт. · всего:" + total when Count > 1) on one line. Returns the width used. */
	inline float PriceLine(FVDraw& D, const FString& Label, int64 Unit, int32 Count, const FVector2D& Pos, float Size, const FLinearColor& C = FVPal::InkSoft())
	{
		float X = Pos.X;
		D.Text(Label, FVector2D(X, Pos.Y), Size, C, EFVAlign::Left, false, false);
		X += D.MeasureText(Label, Size, false).X + 3.f;
		X += D.MoneyLabel(FVector2D(X, Pos.Y), Unit, Size);
		if (Count > 1)
		{
			const FString Mid = TEXT("за шт. · всего:");
			D.Text(Mid, FVector2D(X + 4.f, Pos.Y), Size, C, EFVAlign::Left, false, false);
			X += 4.f + D.MeasureText(Mid, Size, false).X + 3.f;
			X += D.MoneyLabel(FVector2D(X, Pos.Y), int64(Unit) * Count, Size);
		}
		return X - Pos.X;
	}
}
