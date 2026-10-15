// FVScreenHome.h — shared helpers of the UI-B "home" screens: FVScreenStation.cpp, FVScreenOrders.cpp,
// FVScreenNet.cpp, FVScreenDecor.cpp. Private header of the UI-B module: everything is inline, in namespace FVHomeUI.
// Update 3: swipeable tab sets (FFVPager + PagerTabs) and smooth lists (FFVScrollView / FScrollColumn) from FVUIKit.h.

#pragma once

#include "CoreMinimal.h"
#include "FVScreenCommon.h"
#include "InputCoreTypes.h"
#include "FVUIKit.h"

namespace FVHomeUI
{
	// ============================================================================================
	// Colours (readable on parchment) & feedback
	// ============================================================================================
	inline FLinearColor InkGood()   { return FV::Col(46, 132, 34); }
	inline FLinearColor InkBad()    { return FV::Col(196, 40, 36); }
	inline FLinearColor InkWarn()   { return FV::Col(184, 104, 14); }
	inline FLinearColor InkInfo()   { return FV::Col(36, 100, 176); }
	inline FLinearColor BoxFill()   { return FV::Col(236, 200, 146); }
	inline FLinearColor BoxEdge()   { return FV::Col(176, 122, 76); }
	inline FLinearColor RowFill()   { return FV::Col(255, 234, 194); }
	inline FLinearColor RowHover()  { return FV::Col(255, 242, 212); }
	inline FLinearColor RowSel()    { return FV::Col(255, 250, 230); }
	inline FLinearColor ToastGood() { return FV::Col(255, 228, 120); }
	inline FLinearColor ToastBad()  { return FV::Col(255, 130, 112); }

	/** Russian text for any result code (the update-2 codes included). */
	inline FString ResultText(EFVBuyResult R)
	{
		switch (R)
		{
		case EFVBuyResult::Ok: return TEXT("Готово");
		case EFVBuyResult::AlreadyOwned: return TEXT("Уже есть");
		case EFVBuyResult::NotEnoughMoney: return TEXT("Не хватает монет");
		case EFVBuyResult::LevelTooLow: return TEXT("Слишком низкий уровень");
		case EFVBuyResult::NotForSale: return TEXT("Сейчас недоступно");
		case EFVBuyResult::NotEnoughMaterials: return TEXT("Не хватает материалов");
		case EFVBuyResult::NoRoom: return TEXT("Нет места: освободите рюкзак или садок");
		case EFVBuyResult::StationRequired: return TEXT("Нужна станция получше");
		case EFVBuyResult::MaxLevel: return TEXT("Уже максимальный уровень");
		case EFVBuyResult::Locked: return TEXT("Сначала нужно достижение");
		case EFVBuyResult::Invalid:
		default: return TEXT("Не получилось");
		}
	}

	/** Error feedback: sfx + red toast. */
	inline void Fail(UFVGame* G, const FString& Text)
	{
		FVUI::Sfx(EFVSfx::Error);
		if (G) { G->Toast(Text, ToastBad(), EFVIcon::Exclaim); }
	}
	inline void Fail(UFVGame* G, EFVBuyResult R) { Fail(G, ResultText(R)); }

	/** Success feedback: sfx + golden toast. */
	inline void Success(UFVGame* G, const FString& Text, EFVSfx Sound = EFVSfx::Buy, EFVIcon Icon = EFVIcon::Check)
	{
		FVUI::Sfx(Sound);
		if (G) { G->Toast(Text, ToastGood(), Icon); }
	}

	/** After every profile change: achievements/autosave + the world objects (stations, nets, decor...). */
	inline void Refresh(UFVGame* G)
	{
		if (!G) { return; }
		G->NotifyProfileChanged();
		G->GetScene().RefreshWorldObjects();
	}

	// ============================================================================================
	// Text helpers
	// ============================================================================================
	/** Lower-cases the first letter (Latin and Cyrillic, incl. Ё). */
	inline FString LowerFirst(const FString& S)
	{
		if (S.IsEmpty()) { return S; }
		FString R = S;
		TCHAR& C = R[0];
		if (C >= 0x0410 && C <= 0x042F) { C = TCHAR(C + 0x20); }
		else if (C == 0x0401) { C = TCHAR(0x0451); }
		else if (C >= 'A' && C <= 'Z') { C = TCHAR(C + 32); }
		return R;
	}

	/** "Нужна плита" / "Нужен верстак": the station level a recipe needs. */
	inline FString NeedStationText(EFVStation S, int32 Level)
	{
		FString N = FVDB::StationLevelName(S, Level);
		if (N.IsEmpty())
		{
			return FString::Printf(TEXT("Нужен уровень станции %d"), Level);
		}
		const bool bFem = N.EndsWith(TEXT("а")) || N.EndsWith(TEXT("я")) || N.EndsWith(TEXT("ь"));
		return FString::Printf(TEXT("%s %s"), bFem ? TEXT("Нужна") : TEXT("Нужен"), *LowerFirst(N));
	}

	/** Name of a recipe ingredient (the "any fish" tokens included: «Любая рыба», update 5 «Редкая рыба» …). */
	inline FString IngredientName(FName Id)
	{
		if (FVDB::IsAnyFishToken(Id)) { return FVDB::ItemName(Id); }
		const FString N = FVDB::ItemName(Id);
		return N.IsEmpty() ? Id.ToString() : N;
	}

	/** "Ветка ×3 · Камешек ×2". */
	inline FString IngredientsText(const TArray<FFVIngredient>& In, int32 Times = 1)
	{
		TArray<FString> Parts;
		for (const FFVIngredient& I : In)
		{
			Parts.Add(FString::Printf(TEXT("%s ×%d"), *IngredientName(I.ItemId), I.Count * FMath::Max(1, Times)));
		}
		return FString::Join(Parts, TEXT(" · "));
	}

	/** "3 рыбы", "1 рыба", "5 рыб". */
	inline FString FishCount(int32 N)
	{
		return FString::Printf(TEXT("%d %s"), N, FVUI::Plural(N, TEXT("рыба"), TEXT("рыбы"), TEXT("рыб")));
	}

	/** Word-wraps S into at most MaxLines lines (the last one ends with "…" if the text does not fit). Returns the height used. */
	inline float TextWrappedLimited(FVDraw& D, const FString& S, const FVector2D& Pos, float MaxW, float Size, const FLinearColor& C, int32 MaxLines, bool bBold = false)
	{
		if (S.IsEmpty() || MaxLines <= 0) { return 0.f; }
		const float Step = FMath::RoundToFloat(D.LineHeight(Size, bBold) * 0.95f);
		TArray<FString> Paragraphs;
		S.ParseIntoArray(Paragraphs, TEXT("\n"), false);
		TArray<FString> Lines;
		for (const FString& Para : Paragraphs)
		{
			TArray<FString> Words;
			Para.ParseIntoArrayWS(Words);
			FString Cur;
			for (const FString& Word : Words)
			{
				const FString Next = Cur.IsEmpty() ? Word : Cur + TEXT(" ") + Word;
				if (!Cur.IsEmpty() && D.MeasureText(Next, Size, bBold).X > MaxW)
				{
					Lines.Add(Cur);
					Cur = Word;
				}
				else
				{
					Cur = Next;
				}
			}
			Lines.Add(Cur);
			if (Lines.Num() > MaxLines) { break; }
		}
		const bool bCut = Lines.Num() > MaxLines;
		if (bCut) { Lines.SetNum(MaxLines); }
		if (bCut && Lines.Num() > 0)
		{
			Lines.Last() = D.Ellipsize(Lines.Last() + TEXT(" …"), MaxW, Size, bBold);
		}
		float Y = Pos.Y;
		for (const FString& L : Lines)
		{
			D.Text(D.Ellipsize(L, MaxW, Size, bBold), FVector2D(Pos.X, Y), Size, C, EFVAlign::Left, false, bBold);
			Y += Step;
		}
		return float(Lines.Num()) * Step;
	}

	// ============================================================================================
	// Small widgets
	// ============================================================================================
	/** Recessed box for item icons / previews. */
	inline void IconBox(FVDraw& D, const FFVRect& R)
	{
		D.Rect(R, BoxFill());
		D.Rect(FFVRect(R.X, R.Y, R.W, 1.f), FV::Col(196, 140, 88));
		D.Frame(R, BoxEdge());
	}

	/** Icon of any id (fish, junk, bait, item, rod, decor; "any_fish" = the fish UI icon; update 5: a rarity token =
	    the fish icon in a frame of the rarity colour) fitted into Box. */
	inline void DrawItemIcon(FVDraw& D, FName Id, const FFVRect& Box, float MaxUp = 2.f, const FLinearColor& Tint = FLinearColor::White)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp || Id.IsNone()) { return; }
		EFVRarity TokenRarity = EFVRarity::Common;
		if (FVDB::FishTokenRarity(Id, TokenRarity))
		{
			const float Th = Box.W >= 24.f ? 2.f : 1.f;
			FLinearColor Edge = FVUI::RarityInk(TokenRarity);
			Edge.A *= Tint.A;
			D.Rect(Box, FLinearColor(Edge.R, Edge.G, Edge.B, Edge.A * 0.18f));
			if (const FFVSprite* F = FVUI::Icon(EFVIcon::Fish)) { FVUI::SpriteFit(D, *F, Box, Tint, MaxUp); }
			D.Frame(Box.Inset(-Th), Edge, Th);   // just outside the box: the icon keeps its full size
			return;
		}
		if (Id == FVDB::AnyFishToken())
		{
			if (const FFVSprite* F = FVUI::Icon(EFVIcon::Fish)) { FVUI::SpriteFit(D, *F, Box, Tint, MaxUp); }
			return;
		}
		const FFVSprite S = Sp->ItemIcon(Id);
		FVUI::SpriteFit(D, S, Box, Tint, MaxUp);
	}

	/** UI icon fitted into Box. */
	inline void DrawIcon(FVDraw& D, EFVIcon I, const FFVRect& Box, float MaxUp = 2.f, const FLinearColor& Tint = FLinearColor::White)
	{
		if (const FFVSprite* S = FVUI::Icon(I)) { FVUI::SpriteFit(D, *S, Box, Tint, MaxUp); }
	}

	/** Pixel triangle. Dir: 0 up, 1 down, 2 left, 3 right. */
	inline void Triangle(FVDraw& D, const FVector2D& C, int32 Dir, int32 Rows, const FLinearColor& Col)
	{
		const float CX = FMath::FloorToFloat(C.X);
		const float CY = FMath::FloorToFloat(C.Y);
		const float Half = float(Rows / 2);
		for (int32 i = 0; i < Rows; ++i)
		{
			const float Wd = 2.f * float(i) + 1.f;
			const float Along = float(i) - Half;
			switch (Dir)
			{
			case 0:  D.Rect(FFVRect(CX - float(i), CY + Along, Wd, 1.f), Col); break;
			case 1:  D.Rect(FFVRect(CX - float(i), CY - Along - 1.f, Wd, 1.f), Col); break;
			case 2:  D.Rect(FFVRect(CX + Along, CY - float(i), 1.f, Wd), Col); break;
			default: D.Rect(FFVRect(CX - Along - 1.f, CY - float(i), 1.f, Wd), Col); break;
			}
		}
	}

	/** Round green badge with a white tick. */
	inline void CheckBadge(FVDraw& D, const FVector2D& C, float R = 5.f)
	{
		D.Circle(C, R, FV::Col(36, 76, 22));
		D.Circle(C, R - 1.f, FVPal::Green());
		D.Line(C + FVector2D(-2.4f, 0.f), C + FVector2D(-0.6f, 1.9f), FVPal::White(), 1.4f);
		D.Line(C + FVector2D(-0.6f, 1.9f), C + FVector2D(2.6f, -2.2f), FVPal::White(), 1.4f);
	}

	/** Small counter bubble ("×12") at the bottom-right corner of a cell. */
	inline void CornerCount(FVDraw& D, const FFVRect& Cell, int32 N, float Size)
	{
		if (N <= 1) { return; }
		D.TextOutlined(FString::FromInt(N), FVector2D(Cell.Right() - 2.f, Cell.Bottom() - Size - 3.f), Size, FVPal::White(), EFVAlign::Right);
	}

	/** Level pips: Max squares, the first Level filled. Returns the width used. */
	inline float LevelPips(FVDraw& D, const FVector2D& Pos, int32 Level, int32 Max, float S, float Gap = 2.f)
	{
		float X = Pos.X;
		for (int32 i = 0; i < Max; ++i)
		{
			const FFVRect R(X, Pos.Y, S, S);
			D.Rect(R, FV::Col(92, 44, 22));
			D.Rect(R.Inset(1.f), i < Level ? FV::Col(106, 190, 48) : FV::Col(226, 186, 130));
			if (i < Level) { D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(190, 240, 140)); }
			X += S + Gap;
		}
		return Max > 0 ? X - Gap - Pos.X : 0.f;
	}

	/** Button with "Label ●Price" centred inside (Price < 0: label only). bAffordable colours the amount. */
	inline bool CoinButton(FVDraw& D, const FFVRect& R, const FString& Label, int64 Price, EFVButton Style, bool bEnabled, bool bAffordable = true, float TextSize = 0.f)
	{
		const float Base = TextSize > 0.f ? TextSize : (FV::IsPortrait() ? FV::Ui::Body() + 0.5f : 8.f);
		if (Price < 0)
		{
			return D.Button(R, Label, Style, bEnabled, Base);
		}
		const bool bClicked = D.Button(R, FString(), Style, bEnabled, Base);
		const bool bDown = bEnabled && D.Hover(R) && D.Input().bLmbDown;
		const FString Amount = FV::Money(Price);
		const float CoinW = 8.f;
		float S = Base;
		float LW = D.MeasureText(Label, S).X;
		float AW = D.MeasureText(Amount, S).X;
		while (LW + 5.f + CoinW + 2.f + AW > R.W - 8.f && S > 5.5f)
		{
			S -= 0.5f;
			LW = D.MeasureText(Label, S).X;
			AW = D.MeasureText(Amount, S).X;
		}
		const float Total = LW + 5.f + CoinW + 2.f + AW;
		const float TH = D.MeasureText(Label, S).Y;
		const float X = FMath::RoundToFloat(R.Center().X - Total * 0.5f);
		const float Y = FMath::RoundToFloat(R.Y + (R.H - TH) * 0.5f - 0.5f + (bDown ? 1.f : 0.f));
		const bool bLight = bEnabled && (Style == EFVButton::Primary || Style == EFVButton::Danger);
		const FLinearColor TextC = !bEnabled ? FV::Col(128, 112, 98) : (bLight ? FV::Col(255, 255, 240) : FVPal::Ink());
		const FLinearColor OutC = Style == EFVButton::Danger ? FV::Col(90, 20, 16) : FV::Col(32, 72, 24);
		if (bLight) { D.TextOutlined(Label, FVector2D(X, Y), S, TextC, EFVAlign::Left, OutC); }
		else { D.Text(Label, FVector2D(X, Y), S, TextC, EFVAlign::Left, false, true); }
		if (const FFVSprite* Coin = FVUI::Icon(EFVIcon::Coin))
		{
			D.Sprite(*Coin, FVector2D(X + LW + 5.f, FMath::RoundToFloat(Y + TH * 0.5f - 4.f)), 1.f, bEnabled ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.5f));
		}
		const FLinearColor AmountC = !bEnabled ? TextC : (bAffordable ? (bLight ? FV::Col(255, 236, 140) : FVPal::Ink()) : (bLight ? FV::Col(255, 170, 150) : InkBad()));
		if (bLight) { D.TextOutlined(Amount, FVector2D(X + LW + 5.f + CoinW + 2.f, Y), S, AmountC, EFVAlign::Left, OutC); }
		else { D.Text(Amount, FVector2D(X + LW + 5.f + CoinW + 2.f, Y), S, AmountC, EFVAlign::Left, false, true); }
		return bClicked;
	}

	/** Folder tab with a finger-sized label (shrinks to fit). */
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

	/** Gold marker that follows a pager's slide across the tab cells (bTop: along the top edge of folder tabs, else
	    under the cells). Moves with the finger during a swipe and glides with the page animation. */
	inline void PagerMarker(FVDraw& D, const TArray<FFVRect>& Cells, const FFVPager& Pager, bool bTop)
	{
		const int32 N = Cells.Num();
		if (N <= 1) { return; }
		const float V = FMath::Clamp(Pager.VisualPos(), 0.f, float(N - 1));
		const int32 I0 = FMath::Clamp(FMath::FloorToInt(V), 0, N - 1);
		const int32 I1 = FMath::Min(N - 1, I0 + 1);
		const float T = V - float(I0);
		const FFVRect& A = Cells[I0];
		const FFVRect& B = Cells[I1];
		const float Inset = FV::IsPortrait() ? 6.f : 4.f;
		const float X = FMath::Lerp(A.X, B.X, T) + Inset;
		const float W = FMath::Max(4.f, FMath::Lerp(A.W, B.W, T) - 2.f * Inset);
		const float H = FV::IsPortrait() ? 3.f : 2.f;
		const float Y = bTop ? FMath::Lerp(A.Y, B.Y, T) + 1.f : FMath::Lerp(A.Bottom(), B.Bottom(), T) + 1.f;
		const FFVRect M(FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::RoundToFloat(W), H);
		D.Rect(FFVRect(M.X, M.Bottom(), M.W, 1.f), FV::Col(120, 66, 20, 110));
		D.Rect(M, FVPal::Gold());
		D.Rect(FFVRect(M.X, M.Y, M.W, 1.f), FV::Col(255, 240, 170));
	}

	/** Tab row of a swipeable tab set (update 3). Landscape: folder tabs standing on the panel whose top edge is Y
	    (the content starts at Y). Portrait: big folder tabs (<= 4 labels) or balanced rows of chip buttons at Y.
	    The active tab = Pager.Page; a click animates the pager (GoTo); a gold marker follows the slide.
	    Returns the height used in portrait (content starts at Y + result), 0 in landscape. */
	inline float PagerTabs(FVDraw& D, const TArray<FString>& Labels, FFVPager& Pager, float X, float Y, float W)
	{
		const int32 N = Labels.Num();
		if (N <= 0) { return 0.f; }
		TArray<FFVRect> Cells;
		int32 Hit = -1;
		float OutH = 0.f;
		bool bTop = true;
		if (!FV::IsPortrait())
		{
			const float Gap = 2.f;
			const float TabW = FMath::Min(104.f, FMath::FloorToFloat((W - 8.f - Gap * float(N - 1)) / float(N)));
			for (int32 i = 0; i < N; ++i)
			{
				const FFVRect R(X + 4.f + float(i) * (TabW + Gap), Y - 15.f, TabW, 16.f);
				Cells.Add(R);
				if (D.Tab(R, Labels[i], i == Pager.Page)) { Hit = i; }
			}
		}
		else if (N <= 4)
		{
			const float TabH = 30.f, Gap = 3.f;
			const float TabW = FMath::FloorToFloat((W - 8.f - Gap * float(N - 1)) / float(N));
			for (int32 i = 0; i < N; ++i)
			{
				const FFVRect R(X + 4.f + float(i) * (TabW + Gap), Y, TabW, TabH);
				Cells.Add(R);
				if (BigTab(D, R, Labels[i], i == Pager.Page, FV::Ui::Body())) { Hit = i; }
			}
			OutH = TabH;
		}
		else
		{
			bTop = false;
			const int32 Rows = FMath::DivideAndRoundUp(N, 4);
			const int32 PerRow = FMath::DivideAndRoundUp(N, Rows);
			const float ChipH = 30.f, Gap = 5.f;
			const float CellW = FMath::FloorToFloat((W - Gap * float(PerRow - 1)) / float(PerRow));
			for (int32 i = 0; i < N; ++i)
			{
				const int32 Row = i / PerRow;
				const int32 Col = i % PerRow;
				const int32 InRow = FMath::Min(PerRow, N - Row * PerRow);
				const float RowW = float(InRow) * CellW + float(InRow - 1) * Gap;
				const float RX = FMath::RoundToFloat(X + (W - RowW) * 0.5f + float(Col) * (CellW + Gap));
				const FFVRect R(RX, Y + float(Row) * (ChipH + Gap), CellW, ChipH);
				Cells.Add(R);
				if (D.Button(R, Labels[i], i == Pager.Page ? EFVButton::Primary : EFVButton::Normal, true, FV::Ui::Body())) { Hit = i; }
			}
			OutH = float(Rows) * ChipH + float(Rows - 1) * Gap + 8.f;
		}
		if (Hit >= 0 && Hit != Pager.Page) { Pager.GoTo(Hit); }
		PagerMarker(D, Cells, Pager, bTop);
		return OutH;
	}

	/** Left/Right keys for a tab set: animated like a swipe. Returns true when a key moved the pager. */
	inline bool PagerKeys(const FFVInput& In, FFVPager& Pager)
	{
		if (Pager.NumPagesCached <= 1) { return false; }
		if (In.WasPressed(EKeys::Left) && Pager.Page > 0) { Pager.GoTo(Pager.Page - 1); return true; }
		if (In.WasPressed(EKeys::Right) && Pager.Page < Pager.NumPagesCached - 1) { Pager.GoTo(Pager.Page + 1); return true; }
		return false;
	}

	/** Small row of quantity chips (×1 / ×5 ...). Returns the new value. */
	inline int32 QtyChips(FVDraw& D, const FVector2D& Pos, const TArray<int32>& Options, int32 Current, float ChipW, float ChipH, float TextSize, bool bEnabled = true)
	{
		int32 Out = Current;
		float X = Pos.X;
		for (int32 Opt : Options)
		{
			const FFVRect R(X, Pos.Y, ChipW, ChipH);
			if (D.Button(R, FString::Printf(TEXT("×%d"), Opt), Opt == Current ? EFVButton::Primary : EFVButton::Normal, bEnabled, TextSize)) { Out = Opt; }
			X += ChipW + 4.f;
		}
		return Out;
	}

	// ============================================================================================
	// Requirement rows ("icon · name · есть/нужно")
	// ============================================================================================
	/** One requirement: icon, name and "есть/нужно" (green when enough). Returns the row height. */
	inline float ReqRow(FVDraw& D, float X, float Y, float W, FName Id, int32 Have, int32 Need, float Size)
	{
		const float IconS = FV::IsPortrait() ? 16.f : 12.f;
		const float LH = D.LineHeight(Size, false);
		const float RowH = FMath::RoundToFloat(FMath::Max(IconS, LH) + 2.f);
		DrawItemIcon(D, Id, FFVRect(X, FMath::RoundToFloat(Y + (RowH - IconS) * 0.5f), IconS, IconS), 1.f);
		const bool bOk = Have >= Need;
		const FString Count = FString::Printf(TEXT("%d/%d"), Have, Need);
		const float CW = D.MeasureText(Count, Size, true).X;
		const float TY = FMath::RoundToFloat(Y + (RowH - LH) * 0.5f);
		D.Text(D.Ellipsize(IngredientName(Id), W - IconS - 8.f - CW, Size, false), FVector2D(X + IconS + 4.f, TY), Size, FVPal::Ink(), EFVAlign::Left, false, false);
		D.Text(Count, FVector2D(X + W, TY), Size, bOk ? InkGood() : InkBad(), EFVAlign::Right, false, true);
		return RowH;
	}

	/** Coins requirement: coin icon, "Монеты", the price (green when affordable). Returns the row height. */
	inline float CoinRow(FVDraw& D, float X, float Y, float W, int64 Have, int64 Need, float Size)
	{
		const float IconS = FV::IsPortrait() ? 16.f : 12.f;
		const float LH = D.LineHeight(Size, false);
		const float RowH = FMath::RoundToFloat(FMath::Max(IconS, LH) + 2.f);
		DrawIcon(D, EFVIcon::Coin, FFVRect(X, FMath::RoundToFloat(Y + (RowH - IconS) * 0.5f), IconS, IconS), 1.f);
		const float TY = FMath::RoundToFloat(Y + (RowH - LH) * 0.5f);
		D.Text(TEXT("Монеты"), FVector2D(X + IconS + 4.f, TY), Size, FVPal::Ink(), EFVAlign::Left, false, false);
		D.Text(FV::Money(Need), FVector2D(X + W, TY), Size, Have >= Need ? InkGood() : InkBad(), EFVAlign::Right, false, true);
		return RowH;
	}

	/** A text requirement ("Уровень рыбалки 3", "Нужна плита") with a tick / cross. Returns the row height. */
	inline float CondRow(FVDraw& D, float X, float Y, float W, const FString& Label, bool bOk, float Size, EFVIcon Icon = EFVIcon::Level)
	{
		const float IconS = FV::IsPortrait() ? 16.f : 12.f;
		const float LH = D.LineHeight(Size, false);
		const float RowH = FMath::RoundToFloat(FMath::Max(IconS, LH) + 2.f);
		DrawIcon(D, Icon, FFVRect(X, FMath::RoundToFloat(Y + (RowH - IconS) * 0.5f), IconS, IconS), 1.f);
		const float TY = FMath::RoundToFloat(Y + (RowH - LH) * 0.5f);
		const FString State = bOk ? TEXT("есть") : TEXT("нет");
		const float SW = D.MeasureText(State, Size, true).X;
		D.Text(D.Ellipsize(Label, W - IconS - 8.f - SW, Size, false), FVector2D(X + IconS + 4.f, TY), Size, FVPal::Ink(), EFVAlign::Left, false, false);
		D.Text(State, FVector2D(X + W, TY), Size, bOk ? InkGood() : InkBad(), EFVAlign::Right, false, true);
		return RowH;
	}

	/** Height of one requirement row (same rule as ReqRow / CoinRow / CondRow). */
	inline float ReqRowH(const FVDraw& D, float Size)
	{
		const float IconS = FV::IsPortrait() ? 16.f : 12.f;
		return FMath::RoundToFloat(FMath::Max(IconS, D.LineHeight(Size, false)) + 2.f);
	}

	// ============================================================================================
	// Item descriptions
	// ============================================================================================
	/** Effect lines of an item: food buff (+ duration), tackle mods (+ tier rule), net stats. */
	inline TArray<FString> ItemEffectLines(const FFVItemDef& It)
	{
		TArray<FString> L;
		switch (It.Kind)
		{
		case EFVItemKind::Food:
			L = FVDB::StatModsLines(It.Buff);
			if (It.BuffMinutes > 0.f) { L.Add(FString::Printf(TEXT("Действует %s"), *UFVGame::DurationText(It.BuffMinutes))); }
			break;
		case EFVItemKind::Line:
		case EFVItemKind::Float:
			L = FVDB::StatModsLines(It.Tackle);
			if (It.Tier > 1) { L.Add(FString::Printf(TEXT("Ранг %d: для удочек ранга %d и выше"), int32(It.Tier), int32(It.Tier) - 1)); }
			else { L.Add(TEXT("Подходит к любой удочке")); }
			break;
		case EFVItemKind::Net:
			L.Add(FString::Printf(TEXT("Вмещает %s"), *FishCount(It.NetCapacity)));
			L.Add(FString::Printf(TEXT("Прочность %d"), FMath::RoundToInt(It.NetDurability)));
			if (It.NetMinutesPerFish > 0.f) { L.Add(FString::Printf(TEXT("≈ 1 рыба за %s"), *UFVGame::DurationText(It.NetMinutesPerFish))); }
			break;
		default:
			break;
		}
		return L;
	}

	/** Short stat parts of a bait ("Клёв ×0,8 · Редкие +15%"). */
	inline TArray<FString> BaitParts(const FFVBaitDef& B)
	{
		TArray<FString> Parts;
		if (!FMath::IsNearlyEqual(B.BiteSpeed, 1.f)) { Parts.Add(FString::Printf(TEXT("Клёв ×%s"), *FVUI::Num(B.BiteSpeed))); }
		if (B.RareBonus > 0.f) { Parts.Add(FString::Printf(TEXT("Редкие +%s"), *FVUI::Percent(B.RareBonus))); }
		if (!FMath::IsNearlyEqual(B.JunkMult, 1.f)) { Parts.Add(FString::Printf(TEXT("Мусор ×%s"), *FVUI::Num(B.JunkMult))); }
		if (B.SizeBonus > 0.f) { Parts.Add(FString::Printf(TEXT("Размер +%s"), *FVUI::Percent(B.SizeBonus))); }
		if (Parts.Num() == 0) { Parts.Add(TEXT("Обычная наживка")); }
		return Parts;
	}

	/** Category line of any id: "Ресурс", "Наживка", "Рыба"... */
	inline FString KindText(FName Id)
	{
		if (const FFVItemDef* It = FVDB::FindItem(Id)) { return FVDB::ItemKindName(It->Kind); }
		if (FVDB::FindBait(Id)) { return TEXT("Наживка"); }
		if (FVDB::FindFish(Id)) { return TEXT("Рыба"); }
		if (FVDB::FindJunk(Id)) { return TEXT("Хлам"); }
		return FString();
	}

	/** Description of any id. */
	inline FString DescriptionOf(FName Id)
	{
		if (const FFVItemDef* It = FVDB::FindItem(Id)) { return It->Description; }
		if (const FFVBaitDef* B = FVDB::FindBait(Id)) { return B->Description; }
		if (const FFVFishDef* F = FVDB::FindFish(Id)) { return F->Description; }
		if (const FFVJunkDef* J = FVDB::FindJunk(Id)) { return J->Description; }
		return FString();
	}

	// ============================================================================================
	// Smooth lists (update 3: FFVScrollView, release clicks)
	// ============================================================================================
	/** Held on R without dragging (the pressed look of a tappable row; a drag that scrolls never presses). */
	inline bool PressingIn(const FVDraw& D, const FFVRect& R)
	{
		if (!D.IsPointerDown() || D.IsDragGesture() || D.IsInputBlocked()) { return false; }
		const FVector2D M = D.Mouse();
		return R.Contains(D.PressPos()) && R.Contains(M) && D.InClip(M);
	}

	/** Top/bottom fade colour of lists that scroll on a parchment panel. */
	inline FLinearColor ListBg() { return FVPal::Parchment(); }

	/** A scrolling column of free-form content (texts, requirement rows, cards of varying height):
	        float Y = Col.Begin(D, Area, 2.f);   ...draw, advancing Y...   Col.End(D, Y, 4.f);
	    Begin updates the kinetic scroll, clips to Area and returns the (scrolled) Y where the content starts; End records
	    the height used (the scroll range follows next frame), pops the clip and draws the edge fades + scrollbar. */
	struct FScrollColumn
	{
		FFVScrollView View;
		FFVRect Area;
		float ContentH = 0.f;

		float Begin(FVDraw& D, const FFVRect& InArea, float PadTop = 0.f, bool bAllowWheel = true)
		{
			Area = InArea;
			View.Update(D, Area, ContentH, bAllowWheel);
			D.PushClip(Area);
			return Area.Y - View.Offset() + PadTop;
		}
		void End(FVDraw& D, float EndY, float PadBottom = 0.f, bool bFades = true)
		{
			ContentH = FMath::Max(0.f, EndY + View.Offset() - Area.Y + PadBottom);
			D.PopClip();
			if (bFades) { View.DrawFades(D, Area, FVPal::Parchment()); }
			View.DrawBar(D, Area);
		}
		/** Width to leave free at the right for the scrollbar. */
		float BarRoom() const { return View.CanScroll() ? 6.f : 0.f; }
		void Reset() { View.Reset(); ContentH = 0.f; }
	};

	/** Uniform rows / grid rows inside a scroll view: is the row [Y, Y+H] (current coords) visible in Area? */
	inline bool RowVisible(const FFVRect& Area, float Y, float H)
	{
		return Y + H >= Area.Y - 2.f && Y <= Area.Bottom() + 2.f;
	}

	/** Finishes a uniform list drawn inside D.PushClip(Area): pops the clip, draws the edge fades and the scrollbar. */
	inline void EndList(FVDraw& D, const FFVScrollView& View, const FFVRect& Area, bool bFades = true)
	{
		D.PopClip();
		if (bFades) { View.DrawFades(D, Area, FVPal::Parchment()); }
		View.DrawBar(D, Area);
	}

	/** Background of a selectable list row (normal / hover / pressed / selected). */
	inline void RowBack(FVDraw& D, const FFVRect& R, bool bSelected, bool bHover, bool bPressed)
	{
		D.Rect(R, bSelected ? FV::Col(190, 120, 60) : FV::Col(214, 162, 104));
		D.Rect(R.Inset(1.f), bSelected ? RowSel() : (bPressed ? FV::Col(246, 214, 164) : (bHover ? RowHover() : RowFill())));
		if (bSelected)
		{
			D.Frame(R, FV::Col(214, 52, 52), 1.f);
			D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 2.f, R.H - 2.f), FV::Col(214, 52, 52));
		}
	}

	/** Empty-state message centred in R (icon above the wrapped text). */
	inline void EmptyState(FVDraw& D, const FFVRect& R, EFVIcon Icon, const FString& Text, float Size)
	{
		const float TW = FMath::Min(R.W - 16.f, 300.f);
		const float TH = D.MeasureWrapped(Text, TW, Size);
		const float IconS = 32.f;
		const float Total = IconS + 6.f + TH;
		const float Y0 = FMath::RoundToFloat(R.Y + FMath::Max(4.f, (R.H - Total) * 0.5f));
		DrawIcon(D, Icon, FFVRect(R.Center().X - IconS * 0.5f, Y0, IconS, IconS), 2.f, FLinearColor(1.f, 1.f, 1.f, 0.75f));
		// centred wrapped text: wrap manually line by line
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		FString Cur;
		float Y = Y0 + IconS + 6.f;
		const float Step = FMath::RoundToFloat(D.LineHeight(Size, false) * 1.05f);
		for (int32 i = 0; i <= Words.Num(); ++i)
		{
			const bool bEnd = i == Words.Num();
			const FString Next = bEnd ? Cur : (Cur.IsEmpty() ? Words[i] : Cur + TEXT(" ") + Words[i]);
			if (bEnd || (!Cur.IsEmpty() && D.MeasureText(Next, Size, false).X > TW))
			{
				if (!Cur.IsEmpty() && Y + Step <= R.Bottom() + 2.f)
				{
					D.Text(Cur, FVector2D(R.Center().X, Y), Size, FVPal::InkSoft(), EFVAlign::Center, false, false);
					Y += Step;
				}
				Cur = bEnd ? FString() : Words[i];
			}
			else
			{
				Cur = Next;
			}
		}
	}

	// ============================================================================================
	// Containers
	// ============================================================================================
	/** Stack identity inside a mutable container (never keep indexes across frames). */
	struct FStackKey
	{
		FName ItemId;
		uint8 Quality = 0;
		uint8 Flags = 0;
		bool bValid = false;

		void Set(const FFVInvStack& S) { ItemId = S.ItemId; Quality = S.Quality; Flags = S.Flags; bValid = true; }
		void Clear() { bValid = false; ItemId = NAME_None; }
		bool Matches(const FFVInvStack& S) const { return bValid && S.ItemId == ItemId && S.Quality == Quality && S.Flags == Flags; }
		int32 Find(const TArray<FFVInvStack>& Stacks) const
		{
			if (!bValid) { return INDEX_NONE; }
			for (int32 i = 0; i < Stacks.Num(); ++i)
			{
				if (Matches(Stacks[i]) && Stacks[i].Count > 0) { return i; }
			}
			return INDEX_NONE;
		}
	};

	inline EFVQuality QualityOf(uint8 Q)
	{
		return EFVQuality(FMath::Clamp<int32>(Q, 0, int32(EFVQuality::Count) - 1));
	}

	/** Item cell content: icon, quality star, smoked mark and the count. */
	inline void StackCell(FVDraw& D, const FFVRect& R, FName Id, uint8 Quality, uint8 Flags, int32 Count, bool bDim = false)
	{
		const FLinearColor Tint = bDim ? FLinearColor(1.f, 1.f, 1.f, 0.4f) : FLinearColor::White;
		DrawItemIcon(D, Id, R.Inset(FV::IsPortrait() ? 5.f : 3.f), 2.f, Tint);
		const EFVQuality Q = QualityOf(Quality);
		if (Q != EFVQuality::Normal) { D.Stars(FVector2D(R.X + 2.f, R.Bottom() - 10.f), Q, 1.f); }
		if (Flags & FVStackFlags::Smoked)
		{
			// small smoke curl at the top-left: "копчёная"
			D.Circle(FVector2D(R.X + 5.f, R.Y + 6.f), 3.f, FV::Col(120, 100, 90, 200));
			D.Circle(FVector2D(R.X + 8.f, R.Y + 4.f), 2.f, FV::Col(160, 140, 130, 200));
		}
		CornerCount(D, R, Count, FV::IsPortrait() ? 8.f : 6.5f);
	}
}

/** Brings every FVHomeUI helper into the current (named) namespace with using-DECLARATIONS. Unlike
    `using namespace FVHomeUI;` (whose names take part in lookup at global scope), this can never become ambiguous with
    same-named helpers that other .cpp files keep in anonymous namespaces when UBT merges them into one unity file.
    Usage (inside the file's own namespace): FV_HOME_UI_USING(); */
#define FV_HOME_UI_USING() \
	using FVHomeUI::InkGood; using FVHomeUI::InkBad; using FVHomeUI::InkWarn; using FVHomeUI::InkInfo; \
	using FVHomeUI::BoxFill; using FVHomeUI::BoxEdge; using FVHomeUI::RowFill; using FVHomeUI::RowHover; using FVHomeUI::RowSel; \
	using FVHomeUI::ToastGood; using FVHomeUI::ToastBad; using FVHomeUI::ResultText; using FVHomeUI::Fail; using FVHomeUI::Success; \
	using FVHomeUI::Refresh; using FVHomeUI::LowerFirst; using FVHomeUI::NeedStationText; using FVHomeUI::IngredientName; \
	using FVHomeUI::IngredientsText; using FVHomeUI::FishCount; using FVHomeUI::TextWrappedLimited; using FVHomeUI::IconBox; \
	using FVHomeUI::DrawItemIcon; using FVHomeUI::DrawIcon; using FVHomeUI::Triangle; using FVHomeUI::CheckBadge; \
	using FVHomeUI::CornerCount; using FVHomeUI::LevelPips; using FVHomeUI::CoinButton; using FVHomeUI::BigTab; \
	using FVHomeUI::PagerMarker; using FVHomeUI::PagerTabs; using FVHomeUI::PagerKeys; using FVHomeUI::QtyChips; \
	using FVHomeUI::ReqRow; using FVHomeUI::CoinRow; using FVHomeUI::CondRow; using FVHomeUI::ReqRowH; using FVHomeUI::ItemEffectLines; \
	using FVHomeUI::BaitParts; using FVHomeUI::KindText; using FVHomeUI::DescriptionOf; using FVHomeUI::PressingIn; \
	using FVHomeUI::ListBg; using FVHomeUI::FScrollColumn; using FVHomeUI::RowVisible; using FVHomeUI::EndList; \
	using FVHomeUI::RowBack; using FVHomeUI::EmptyState; using FVHomeUI::FStackKey; using FVHomeUI::QualityOf; using FVHomeUI::StackCell
