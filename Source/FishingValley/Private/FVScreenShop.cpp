// FVScreenShop.cpp — a trader's stall (context = trader id, UFVGame::GetScreenContext; default FVDB::DefaultTraderId()):
//   «Лавка рыбака» — Дед Митрич at the pond: rods, bait, tackle (lines, floats, nets, materials, shovels), clothes,
//                    keepnet & backpack upgrades, and the «Продать» tab (buys fish and items);
//   «Лавка на причале» — Боцман Семёныч at the ocean pier: late rods / bait / tackle and better prices for the catch.
// Tabs and stock come from FFVTraderDef (TraderMask bits). Each keeper is a procedural pixel portrait (blinks; Mitrich
// smokes a pipe, the boatswain wears a sailor cap) with his own typewriter greetings.
// IMGUI: everything is drawn and handled in Draw(); arrows navigate in Tick(). The shop is opened only at a stall.
// Update 3: the tabs are swipeable sliding pages (FFVPager: swipe, tab buttons and ←/→ all animate); every list and grid
// scrolls smoothly (FFVScrollView: finger / mouse drag, fling, wheel). Rows are picked by a tap (D.Clicked fires on
// release and never after a drag).

#include "CoreMinimal.h"
#include "FVScreens.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVAudio.h"
#include "FVScene.h"
#include "FVScreenCommon.h"
#include "FVDecorPreview.h"
#include "FVUIKit.h"

namespace
{
namespace FVShopScreenImpl
{
// ============================================================================================
// Small helpers (private to this file)
// ============================================================================================

void PlaySfx(EFVSfx Sfx, float Volume = 1.f, float Pitch = 1.f)
{
	if (UFVAudio* Audio = UFVAudio::Get())
	{
		Audio->Play(Sfx, Volume, Pitch);
	}
}

// Ink colours that stay readable on parchment, and toast colours (toasts sit on a dark plate).
FLinearColor InkGood()   { return FV::Col(46, 132, 34); }
FLinearColor InkBad()    { return FV::Col(196, 40, 36); }
FLinearColor InkEquip()  { return FV::Col(36, 104, 186); }
FLinearColor InkReward() { return FV::Col(132, 62, 184); }
FLinearColor ToastGood() { return FV::Col(255, 228, 120); }
FLinearColor ToastBad()  { return FV::Col(255, 130, 112); }
FLinearColor Divider()   { return FV::Col(214, 162, 104); }
FLinearColor BoxFill()   { return FV::Col(236, 200, 146); }
FLinearColor BoxEdge()   { return FV::Col(176, 122, 76); }

/** Russian plural form: 1 место, 2 места, 5 мест. */
const TCHAR* Plural(int64 N, const TCHAR* One, const TCHAR* Few, const TCHAR* Many)
{
	const int64 A = N < 0 ? -N : N;
	const int64 M100 = A % 100;
	const int64 M10 = A % 10;
	if (M100 >= 11 && M100 <= 14) { return Many; }
	if (M10 == 1) { return One; }
	if (M10 >= 2 && M10 <= 4) { return Few; }
	return Many;
}

/** Cuts S with an ellipsis so it fits MaxW (binary search -> only a handful of measurements). */
FString FitText(const FVDraw& D, const FString& S, float MaxW, float Size, bool bBold = true)
{
	if (S.IsEmpty() || D.MeasureText(S, Size, bBold).X <= MaxW)
	{
		return S;
	}
	const FString Ell(TEXT("…"));
	int32 Lo = 0;
	int32 Hi = S.Len() - 1;
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi + 1) / 2;
		if (D.MeasureText(S.Left(Mid).TrimEnd() + Ell, Size, bBold).X <= MaxW) { Lo = Mid; }
		else { Hi = Mid - 1; }
	}
	return Lo > 0 ? S.Left(Lo).TrimEnd() + Ell : Ell;
}

/** Pixel triangle. Dir: 0 up, 1 down, 2 left, 3 right. Rows = height of the triangle in px. */
void Triangle(FVDraw& D, const FVector2D& C, int32 Dir, int32 Rows, const FLinearColor& Col)
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
void CheckBadge(FVDraw& D, const FVector2D& C, float R = 5.f)
{
	D.Circle(C, R, FV::Col(36, 76, 22));
	D.Circle(C, R - 1.f, FVPal::Green());
	D.Line(C + FVector2D(-2.4f, 0.f), C + FVector2D(-0.6f, 1.9f), FVPal::White(), 1.4f);
	D.Line(C + FVector2D(-0.6f, 1.9f), C + FVector2D(2.6f, -2.2f), FVPal::White(), 1.4f);
}

/** Small coloured label ("Куплено", "Надето"...), right-aligned at RightX. Returns its width. */
float Pill(FVDraw& D, const FString& Label, float RightX, float Y, const FLinearColor& Fill, float Size = 6.f, float H = 10.f)
{
	const float W = FMath::CeilToFloat(D.MeasureText(Label, Size).X) + 8.f;
	const FFVRect R(RightX - W, Y, W, H);
	D.Rect(R, Fill);
	D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 255, 255, 70));
	D.Frame(R, FV::Col(52, 30, 20, 170));
	D.Text(Label, FVector2D(R.X + W * 0.5f, R.Y + 1.f + FMath::RoundToFloat((H - 10.f) * 0.5f)), Size, FVPal::White(), EFVAlign::Center);
	return W;
}

// ---- portrait: bottom action bar (FVDraw::ActionBar: "← Назад" on the left, the actions to its right) ----

/** Label size of the big bar buttons. */
float BarTextSize() { return FV::Ui::Body() + 0.5f; }

/** Splits the action area of the bar: a secondary "… →" button (left) and the primary action (right, wider). */
void SplitActions(const FFVRect& Actions, FFVRect& OutNav, FFVRect& OutMain)
{
	const float Gap = 6.f;
	const float NavW = FMath::RoundToFloat((Actions.W - Gap) * 0.46f);
	OutNav = FFVRect(Actions.X, Actions.Y, NavW, Actions.H);
	OutMain = FFVRect(Actions.X + NavW + Gap, Actions.Y, FMath::Max(0.f, Actions.W - NavW - Gap), Actions.H);
}

/** Big bar button with a label and a price (coin + amount) centred inside, e.g. "Купить ●800". */
bool PriceButton(FVDraw& D, const FFVRect& R, const FString& Label, int64 Price, EFVButton Style, bool bAffordable)
{
	const bool bClicked = D.Button(R, FString(), Style, true, BarTextSize());
	const bool bDown = D.Hover(R) && D.Input().bLmbDown;
	const FString Amount = FV::Money(Price);
	const float CoinW = 8.f;
	float S = BarTextSize();
	float LW = D.MeasureText(Label, S).X;
	float AW = D.MeasureText(Amount, S).X;
	while (LW + 6.f + CoinW + 2.f + AW > R.W - 10.f && S > 6.f)
	{
		S -= 0.5f;
		LW = D.MeasureText(Label, S).X;
		AW = D.MeasureText(Amount, S).X;
	}
	const float Total = LW + 6.f + CoinW + 2.f + AW;
	const float TH = D.MeasureText(Label, S).Y;
	const float X = FMath::RoundToFloat(R.Center().X - Total * 0.5f);
	const float Y = FMath::RoundToFloat(R.Y + (R.H - TH) * 0.5f - 0.5f + (bDown ? 1.f : 0.f));
	const bool bPrimary = Style == EFVButton::Primary;
	const FLinearColor TextC = bPrimary ? FV::Col(255, 255, 240) : FVPal::Ink();
	const FLinearColor OutC = bPrimary ? FV::Col(32, 72, 24) : FV::Col(255, 236, 196);
	if (bPrimary) { D.TextOutlined(Label, FVector2D(X, Y), S, TextC, EFVAlign::Left, OutC); }
	else          { D.Text(Label, FVector2D(X, Y), S, TextC, EFVAlign::Left, false, true); }
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.Sprite(Sprites->Icon(EFVIcon::Coin), FVector2D(X + LW + 6.f, FMath::RoundToFloat(Y + TH * 0.5f - 4.f)), 1.f);
	}
	const FLinearColor AmountC = bAffordable ? (bPrimary ? FV::Col(255, 236, 140) : FVPal::Ink()) : (bPrimary ? FV::Col(255, 160, 140) : InkBad());
	if (bPrimary) { D.TextOutlined(Amount, FVector2D(X + LW + 6.f + CoinW + 2.f, Y), S, AmountC, EFVAlign::Left, OutC); }
	else          { D.Text(Amount, FVector2D(X + LW + 6.f + CoinW + 2.f, Y), S, AmountC, EFVAlign::Left, false, true); }
	return bClicked;
}

/** Small red counter bubble (e.g. the number of fish waiting in the keepnet) centred at C. */
void CountBadge(FVDraw& D, const FVector2D& C, int32 N)
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

/** Word-wraps S into at most MaxLines lines (the last one is shortened with "…"). Returns the height used. */
float TextWrappedLimited(FVDraw& D, const FString& S, const FVector2D& Pos, float MaxW, float Size, const FLinearColor& C, int32 MaxLines, bool bBold = false, float LineStep = 0.f)
{
	if (S.IsEmpty() || MaxLines <= 0)
	{
		return 0.f;
	}
	const float Step = LineStep > 0.f ? LineStep : FMath::RoundToFloat(D.LineHeight(Size, bBold) * 0.95f);
	TArray<FString> Words;
	S.ParseIntoArrayWS(Words);
	TArray<FString> Lines;
	FString Cur;
	int32 WordIdx = 0;
	for (; WordIdx < Words.Num(); ++WordIdx)
	{
		const FString Next = Cur.IsEmpty() ? Words[WordIdx] : Cur + TEXT(" ") + Words[WordIdx];
		if (!Cur.IsEmpty() && D.MeasureText(Next, Size, bBold).X > MaxW)
		{
			Lines.Add(Cur);
			Cur = Words[WordIdx];
			if (Lines.Num() == MaxLines)
			{
				break;
			}
		}
		else
		{
			Cur = Next;
		}
	}
	if (Lines.Num() < MaxLines)
	{
		Lines.Add(Cur);
	}
	else
	{
		// text left over: squeeze it into the last line with an ellipsis
		FString Rest = Lines.Last();
		for (int32 i = WordIdx; i < Words.Num(); ++i)
		{
			Rest += TEXT(" ") + Words[i];
		}
		Lines.Last() = D.Ellipsize(Rest, MaxW, Size, bBold);
	}
	float Y = Pos.Y;
	for (const FString& Line : Lines)
	{
		D.Text(D.Ellipsize(Line, MaxW, Size, bBold), FVector2D(Pos.X, Y), Size, C, EFVAlign::Left, false, bBold);
		Y += Step;
	}
	return float(Lines.Num()) * Step;
}

/** Recessed box for item icons / previews. */
void IconBox(FVDraw& D, const FFVRect& R)
{
	D.Rect(R, BoxFill());
	D.Rect(FFVRect(R.X, R.Y, R.W, 1.f), FV::Col(196, 140, 88));
	D.Frame(R, BoxEdge());
}

/** Label size of the action buttons of the (landscape) detail panel. */
float ActionTextSize() { return 8.f; }

EFVHatStyle HatStyleOf(const FFVCosmeticDef& C)       { return EFVHatStyle(FMath::Min<int32>(C.Style, int32(EFVHatStyle::Count) - 1)); }
EFVOutfitStyle OutfitStyleOf(const FFVCosmeticDef& C) { return EFVOutfitStyle(FMath::Min<int32>(C.Style, int32(EFVOutfitStyle::Count) - 1)); }
EFVBobberStyle BobberStyleOf(const FFVCosmeticDef& C) { return EFVBobberStyle(FMath::Min<int32>(C.Style, int32(EFVBobberStyle::Count) - 1)); }
EFVPetStyle PetStyleOf(const FFVCosmeticDef& C)       { return EFVPetStyle(FMath::Min<int32>(C.Style, int32(EFVPetStyle::Count) - 1)); }

/** Update 6 (Р64): the achievement a boutique item still waits for (nullptr: no condition, or it is already completed). */
const FFVAchievementDef* MissingAch(const UFVSaveGame& P, const FFVCosmeticDef& C)
{
	if (C.RequiredAch.IsNone() || P.IsAchievementDone(C.RequiredAch)) { return nullptr; }
	return FVDB::FindAchievement(C.RequiredAch);
}

/** 0.6.2 (Р81): the achievement a sold rod still waits for (nullptr: no condition, or it is already completed). */
const FFVAchievementDef* MissingRodAch(const UFVSaveGame& P, const FFVRodDef& R)
{
	if (R.RequiredAchievement.IsNone() || P.IsAchievementDone(R.RequiredAchievement)) { return nullptr; }
	return FVDB::FindAchievement(R.RequiredAchievement);
}

/** 0.6.2 (Р81): «Друг Города · 7 / 10» (bLong: «Нужно достижение «Друг Города» · 7 / 10»). */
FString RodLockText(const UFVSaveGame& P, const FFVAchievementDef& A, bool bLong)
{
	return bLong ? FString::Printf(TEXT("Нужно достижение «%s» · %s"), *A.Name, *P.AchievementProgressText(A))
		: FString::Printf(TEXT("%s · %s"), *A.Name, *P.AchievementProgressText(A));
}

/** «Нужно достижение «…»» (short: for a list row). */
FString NeedAchText(const FFVAchievementDef& A, bool bLong)
{
	return bLong ? FString::Printf(TEXT("Продаётся только после достижения «%s»: %s"), *A.Name, *A.Description)
		: FString::Printf(TEXT("Нужно достижение «%s»"), *A.Name);
}

/** Where a not-for-sale cosmetic comes from. */
FString RewardSourceText(const FFVCosmeticDef& C)
{
	for (const FFVAchievementDef& A : FVDB::Achievements())
	{
		if (A.RewardCosmetic == C.Id)
		{
			return FString::Printf(TEXT("Не продаётся: награда за достижение «%s»."), *A.Name);
		}
	}
	return TEXT("Не продаётся: иногда попадается в сундуках с сокровищами.");
}

// ============================================================================================
// Shopkeeper portrait (24x24 texels, drawn at x2)
// ============================================================================================

FFVImage BuildShopkeeperImage(bool bBlink)
{
	TMap<TCHAR, FColor> Pal;
	Pal.Add(TEXT('o'), FColor(58, 34, 26));     // outline
	Pal.Add(TEXT('h'), FColor(246, 196, 58));   // sou'wester hat
	Pal.Add(TEXT('l'), FColor(255, 234, 136));  // hat highlight
	Pal.Add(TEXT('H'), FColor(204, 142, 34));   // hat shade
	Pal.Add(TEXT('s'), FColor(240, 186, 138));  // skin
	Pal.Add(TEXT('S'), FColor(212, 150, 110));  // skin shade
	Pal.Add(TEXT('r'), FColor(236, 140, 122));  // rosy cheeks
	Pal.Add(TEXT('n'), FColor(226, 124, 100));  // nose
	Pal.Add(TEXT('e'), FColor(44, 30, 40));     // eyes / mouth
	Pal.Add(TEXT('w'), FColor(246, 242, 234));  // beard
	Pal.Add(TEXT('W'), FColor(198, 194, 196));  // beard shade
	Pal.Add(TEXT('c'), FColor(62, 92, 150));    // knitted sweater
	Pal.Add(TEXT('C'), FColor(42, 62, 110));    // sweater shade
	Pal.Add(TEXT('k'), FColor(98, 132, 192));   // sweater highlight / knit

	const TArray<FString> Rows = {
		TEXT("........................"),
		TEXT(".........oooooo........."),
		TEXT(".......oohhhhhhoo......."),
		TEXT("......ohlhhhhhhhHo......"),
		TEXT(".....ohlhhhhhhhhhHo....."),
		TEXT(".....ohhhhhhhhhhhHo....."),
		TEXT("...oohhhhhhhhhhhhhHoo..."),
		TEXT("..olhhhhhhhhhhhhhhhhHo.."),
		TEXT("..oHHHHHHHHHHHHHHHHHHo.."),
		TEXT(".....osWWWssssWWWso....."),
		TEXT(".....ossessssssesSo....."),
		TEXT(".....osrrssnnssrrSo....."),
		TEXT("....owssssnnnnsssSWo...."),
		TEXT("....owwwwwwwwwwwwWWo...."),
		TEXT("....owwwwWweewWwwWWo...."),
		TEXT("....owwwwwwwwwwwwwWo...."),
		TEXT("....owwwwwwwwwwwwWWo...."),
		TEXT("..ookcowwwwwwwwwWocCoo.."),
		TEXT(".okccccowwwwwwwWocccCCo."),
		TEXT("okccccccowwwwwWocccccCCo"),
		TEXT("okcccccccowwwWoccccccCCo"),
		TEXT("okccccccccowWocccccccCCo"),
		TEXT("okccccccccccccccccccCCCo"),
		TEXT("okckckckckckckckckckcCCo"),
	};
	FFVImage Img = FFVImage::FromAscii(Rows, Pal);
	if (!Img.IsValid())
	{
		return Img;
	}

	// Pipe: stem from the mouth corner, bowl sticking out of the beard on the right.
	const FColor Out(58, 34, 26);
	const FColor Pipe(134, 78, 42);
	const FColor PipeDark(86, 48, 28);
	for (int32 X = 13; X <= 17; ++X) { Img.Set(X, 14, Pipe); }
	for (int32 Y = 12; Y <= 15; ++Y)
	{
		Img.Set(18, Y, Pipe);
		Img.Set(19, Y, PipeDark);
		Img.Set(20, Y, Out);
	}
	Img.Set(18, 12, FColor(236, 116, 44));   // glowing tobacco
	Img.Set(19, 12, FColor(170, 60, 30));
	Img.Set(18, 11, Out);
	Img.Set(19, 11, Out);
	Img.Set(18, 16, Out);
	Img.Set(19, 16, Out);

	if (bBlink)
	{
		const FColor Lid(212, 150, 110);
		Img.Set(8, 10, Lid);
		Img.Set(15, 10, Lid);
	}
	return Img;
}

/** Боцман Семёныч (the pier trader): navy sailor cap with a white band and a gold badge, bushy grey beard, red nose,
    a pea coat with gold buttons over a striped telnyashka. 24x24 texels, drawn at x2. */
FFVImage BuildPierKeeperImage(bool bBlink)
{
	TMap<TCHAR, FColor> Pal;
	Pal.Add(TEXT('o'), FColor(40, 30, 36));      // outline
	Pal.Add(TEXT('n'), FColor(44, 60, 108));     // cap
	Pal.Add(TEXT('N'), FColor(28, 38, 72));      // cap shade
	Pal.Add(TEXT('l'), FColor(76, 100, 160));    // cap highlight
	Pal.Add(TEXT('b'), FColor(236, 236, 240));   // white band
	Pal.Add(TEXT('v'), FColor(22, 22, 28));      // visor
	Pal.Add(TEXT('g'), FColor(250, 204, 64));    // gold badge
	Pal.Add(TEXT('s'), FColor(236, 178, 134));   // skin
	Pal.Add(TEXT('S'), FColor(204, 140, 104));   // skin shade
	Pal.Add(TEXT('r'), FColor(222, 108, 94));    // red nose & cheeks
	Pal.Add(TEXT('e'), FColor(40, 28, 36));      // eyes / mouth
	Pal.Add(TEXT('y'), FColor(196, 196, 204));   // bushy eyebrows
	Pal.Add(TEXT('w'), FColor(226, 226, 232));   // beard
	Pal.Add(TEXT('W'), FColor(168, 168, 180));   // beard shade
	Pal.Add(TEXT('j'), FColor(36, 50, 90));      // pea coat
	Pal.Add(TEXT('J'), FColor(22, 30, 60));      // coat shade
	Pal.Add(TEXT('k'), FColor(236, 190, 70));    // gold buttons
	Pal.Add(TEXT('t'), FColor(242, 242, 248));   // telnyashka: white
	Pal.Add(TEXT('T'), FColor(56, 88, 172));     // telnyashka: blue stripe

	const TArray<FString> Rows = {
		TEXT("........................"),
		TEXT(".......oooooooooo......."),
		TEXT(".....oolnnnnnnnnNoo....."),
		TEXT("....olnnnnnnnnnnnnNo...."),
		TEXT("....onnnnnnggnnnnnNo...."),
		TEXT("....obbbbbbbbbbbbbbo...."),
		TEXT("...ovvvvvvvvvvvvvvvvo..."),
		TEXT("....osyyysssssyyySo....."),
		TEXT("....ossessssssesSSo....."),
		TEXT("....osssssrrssssSSo....."),
		TEXT("...owsrrssrrrsrrSWo....."),
		TEXT("...owwwwwwwwwwwwwWWo...."),
		TEXT("...owwwwwweewwwwwWWo...."),
		TEXT("...owwwwwwwwwwwwwwWo...."),
		TEXT("....owwwwwwwwwwwwWo....."),
		TEXT("..oojowwwwwwwwwwWojJoo.."),
		TEXT(".ojjjjowwwwwwwwWojjjjJo."),
		TEXT("ojjjjjjotTtTtTtojjjjjJJo"),
		TEXT("ojjjkjjjotTtTtojjjkjjJJo"),
		TEXT("ojjjjjjjjotTtojjjjjjjJJo"),
		TEXT("ojjjkjjjjjoTojjjjjkjjJJo"),
		TEXT("ojjjjjjjjjjojjjjjjjjjJJo"),
		TEXT("ojjjkjjjjjjjjjjjjjkjjJJo"),
		TEXT("ojjjjjjjjjjjjjjjjjjjjJJo"),
	};
	FFVImage Img = FFVImage::FromAscii(Rows, Pal);
	if (!Img.IsValid())
	{
		return Img;
	}
	Img.Set(12, 4, FColor(255, 240, 150));   // badge glint
	if (bBlink)
	{
		const FColor Lid(204, 140, 104);
		Img.Set(7, 8, Lid);
		Img.Set(14, 8, Lid);
	}
	return Img;
}

/** Cached keeper portrait frame (0 = eyes open, 1 = blink). bPier = the pier trader. Copy the result; never keep
    references to cache entries. */
bool GetShopkeeperSprite(int32 Frame, FFVSprite& Out, bool bPier = false)
{
	UFVSprites* Sprites = UFVSprites::Get();
	if (!Sprites)
	{
		return false;
	}
	const uint64 Key = bPier
		? UFVSprites::MakeKey(FVSpriteCat::Misc, 0x5D17D0u + uint32(Frame & 1), 0xB05Au, 0xA8)
		: UFVSprites::MakeKey(FVSpriteCat::Misc, 0x5D17C0u + uint32(Frame & 1), 0xDED0u, 0xA7);
	if (const FFVSprite* Found = Sprites->FindCached(Key))
	{
		Out = *Found;
		return Out.IsValid();
	}
	const FFVImage Img = bPier ? BuildPierKeeperImage(Frame == 1) : BuildShopkeeperImage(Frame == 1);
	if (!Img.IsValid())
	{
		return false;
	}
	Out = Sprites->AddCached(Key, Img, bPier ? (Frame == 1 ? TEXT("FV_PierKeeper_Blink") : TEXT("FV_PierKeeper"))
		: (Frame == 1 ? TEXT("FV_Shopkeeper_Blink") : TEXT("FV_Shopkeeper")));
	return Out.IsValid();
}

// ============================================================================================
// Shop items
// ============================================================================================

enum class EShopTab : uint8 { Rods, Bait, Tackle, Cosmetics, Upgrades, Seeds, Decor, Sell, Count };
enum class EItemKind : uint8 { Rod, Bait, Cosmetic, Keepnet, Item, Shovel, Backpack, Axe, Decor };
enum class EItemStatus : uint8 { ForSale, Owned, Equipped, RewardOnly, Locked };

const TCHAR* TabName(EShopTab T)
{
	switch (T)
	{
	case EShopTab::Rods:      return TEXT("Удочки");
	case EShopTab::Bait:      return TEXT("Наживка");
	case EShopTab::Tackle:    return TEXT("Снасти");
	case EShopTab::Cosmetics: return TEXT("Одежда");
	case EShopTab::Upgrades:  return TEXT("Улучшения");
	case EShopTab::Seeds:     return TEXT("Семена");
	case EShopTab::Decor:     return TEXT("Декор");
	case EShopTab::Sell:      return TEXT("Продать");
	default:                  return TEXT("");
	}
}

struct FShopItem
{
	EItemKind Kind = EItemKind::Rod;
	const FFVRodDef* Rod = nullptr;
	const FFVBaitDef* Bait = nullptr;
	const FFVCosmeticDef* Cos = nullptr;
	const FFVItemDef* Item = nullptr;   // Kind == Item (lines, floats, nets, materials)
	int32 KeepnetLevel = 0;      // target keepnet level index (Kind == Keepnet)
	int32 ShovelLevel = 0;       // shovel level 1..N (Kind == Shovel)
	int32 BackpackLevel = 0;     // target backpack level index (Kind == Backpack)
	int32 AxeLevel = 0;          // update 6: axe level 1..N (Kind == Axe; Боцман sells level 1)
	const FFVDecorDef* Decor = nullptr;   // update 6: Kind == Decor (boutique: elite furniture / yard decor)
	bool bForgeInfo = false;     // 0.6.2 (Р80): Kind == Rod, a forged expert rod shown at Кузьма's for information only
};

const FFVToolDef* AxeAt(int32 Level)
{
	const TArray<FFVToolDef>& A = FVDBU6::Axes();
	return A.IsValidIndex(Level - 1) ? &A[Level - 1] : nullptr;
}

/** Mitrich's defaults: used when the trader table is not available (never crash on missing data). */
const FFVTraderDef& FallbackTrader()
{
	static FFVTraderDef T = []()
	{
		FFVTraderDef D;
		D.Id = FName(TEXT("tr_mitrich"));
		D.Name = TEXT("Дед Митрич");
		D.Role = TEXT("хозяин лавки");
		D.ShopTitle = TEXT("Лавка рыбака");
		D.Location = FName(TEXT("pond"));
		D.Mask = 1;
		D.bSellsClothes = true;
		D.bSellsUpgrades = true;
		return D;
	}();
	return T;
}

const FFVShovelDef* ShovelAt(int32 Level)
{
	const TArray<FFVShovelDef>& S = FVDB::Shovels();
	return S.IsValidIndex(Level - 1) ? &S[Level - 1] : nullptr;
}

int32 BackpackSlotsAt(int32 LevelIdx)
{
	const TArray<FFVBackpackLevel>& BL = FVDB::BackpackLevels();
	return BL.IsValidIndex(LevelIdx) ? BL[LevelIdx].Slots : 0;
}

FString BackpackName(int32 LevelIdx)
{
	const int32 Slots = BackpackSlotsAt(LevelIdx);
	return FString::Printf(TEXT("Рюкзак на %d %s"), Slots, Plural(Slots, TEXT("место"), TEXT("места"), TEXT("мест")));
}

/** Copy of an item icon (fish / bait / generic item...). */
bool IconOf(FName Id, FFVSprite& Out)
{
	return FVUI::ItemIconOf(Id, Out);
}

int32 KeepnetCapacityAt(int32 LevelIdx)
{
	const TArray<FFVKeepnetLevel>& KL = FVDB::KeepnetLevels();
	return KL.IsValidIndex(LevelIdx) ? KL[LevelIdx].Capacity : 0;
}

FString KeepnetName(int32 LevelIdx)
{
	const int32 Cap = KeepnetCapacityAt(LevelIdx);
	return FString::Printf(TEXT("Садок на %d %s"), Cap, Plural(Cap, TEXT("место"), TEXT("места"), TEXT("мест")));
}

FString ItemName(const FShopItem& It)
{
	switch (It.Kind)
	{
	case EItemKind::Rod:      return It.Rod ? It.Rod->Name : FString();
	case EItemKind::Bait:     return It.Bait ? It.Bait->Name : FString();
	case EItemKind::Cosmetic: return It.Cos ? It.Cos->Name : FString();
	case EItemKind::Keepnet:  return KeepnetName(It.KeepnetLevel);
	case EItemKind::Item:     return It.Item ? It.Item->Name : FString();
	case EItemKind::Shovel:   { const FFVShovelDef* S = ShovelAt(It.ShovelLevel); return S ? S->Name : FString(TEXT("Лопата")); }
	case EItemKind::Backpack: return BackpackName(It.BackpackLevel);
	case EItemKind::Axe:      { const FFVToolDef* A = AxeAt(It.AxeLevel); return A ? A->Name : FString(TEXT("Топор")); }
	case EItemKind::Decor:    return It.Decor ? It.Decor->Name : FString();
	}
	return FString();
}

int64 ItemPrice(const FShopItem& It)
{
	switch (It.Kind)
	{
	case EItemKind::Rod:      return It.Rod ? It.Rod->Price : 0;
	case EItemKind::Bait:     return It.Bait ? It.Bait->PackPrice : 0;
	case EItemKind::Cosmetic: return It.Cos ? It.Cos->Price : 0;
	case EItemKind::Keepnet:
	{
		const TArray<FFVKeepnetLevel>& KL = FVDB::KeepnetLevels();
		return KL.IsValidIndex(It.KeepnetLevel) ? KL[It.KeepnetLevel].Price : 0;
	}
	case EItemKind::Item:     return It.Item ? It.Item->BuyPrice : 0;
	case EItemKind::Shovel:   { const FFVShovelDef* S = ShovelAt(It.ShovelLevel); return S ? S->Price : 0; }
	case EItemKind::Backpack:
	{
		const TArray<FFVBackpackLevel>& BL = FVDB::BackpackLevels();
		return BL.IsValidIndex(It.BackpackLevel) ? BL[It.BackpackLevel].Price : 0;
	}
	case EItemKind::Axe:      { const FFVToolDef* A = AxeAt(It.AxeLevel); return A ? A->Price : 0; }
	case EItemKind::Decor:    return It.Decor ? It.Decor->Price : 0;
	}
	return 0;
}

int32 ItemMinLevel(const FShopItem& It)
{
	switch (It.Kind)
	{
	case EItemKind::Rod:      return It.Rod ? It.Rod->MinLevel : 1;
	case EItemKind::Bait:     return It.Bait ? It.Bait->MinLevel : 1;
	case EItemKind::Cosmetic: return It.Cos ? It.Cos->MinLevel : 1;
	case EItemKind::Keepnet:  return 1;
	case EItemKind::Item:     return It.Item ? It.Item->MinLevel : 1;
	case EItemKind::Shovel:   { const FFVShovelDef* S = ShovelAt(It.ShovelLevel); return S ? S->MinLevel : 1; }
	case EItemKind::Backpack: return 1;
	case EItemKind::Axe:      { const FFVToolDef* A = AxeAt(It.AxeLevel); return A ? A->MinLevel : 1; }
	case EItemKind::Decor:    return It.Decor ? It.Decor->MinLevel : 1;
	}
	return 1;
}

EItemStatus GetStatus(const UFVSaveGame& P, const FShopItem& It)
{
	switch (It.Kind)
	{
	case EItemKind::Rod:
		if (!It.Rod) { return EItemStatus::Locked; }
		if (P.OwnsRod(It.Rod->Id)) { return P.GetRod().Id == It.Rod->Id ? EItemStatus::Equipped : EItemStatus::Owned; }
		if (It.bForgeInfo) { return EItemStatus::RewardOnly; }                   // 0.6.2 (Р80): forged, not sold
		if (MissingRodAch(P, *It.Rod)) { return EItemStatus::Locked; }          // 0.6.2 (Р81): waits for an achievement
		return EItemStatus::ForSale;
	case EItemKind::Bait:
		if (It.Bait && P.EquippedBait == It.Bait->Id && P.BaitCount(It.Bait->Id) > 0) { return EItemStatus::Equipped; }
		return EItemStatus::ForSale;
	case EItemKind::Cosmetic:
		if (!It.Cos) { return EItemStatus::Locked; }
		if (P.OwnsCosmetic(It.Cos->Id)) { return P.GetEquipped(It.Cos->Slot) == It.Cos->Id ? EItemStatus::Equipped : EItemStatus::Owned; }
		if (It.Cos->Price > 0 && MissingAch(P, *It.Cos)) { return EItemStatus::Locked; }   // Р64: waits for an expert achievement
		return It.Cos->Price > 0 ? EItemStatus::ForSale : EItemStatus::RewardOnly;
	case EItemKind::Keepnet:
		if (It.KeepnetLevel <= P.KeepnetLevel) { return EItemStatus::Owned; }
		return It.KeepnetLevel == P.KeepnetLevel + 1 ? EItemStatus::ForSale : EItemStatus::Locked;
	case EItemKind::Item:
		if (It.Item && It.Item->Kind == EFVItemKind::Blueprint && P.HasBlueprint(It.Item->Id)) { return EItemStatus::Owned; }   // learnt once
		return It.Item ? EItemStatus::ForSale : EItemStatus::Locked;   // consumable: can always be bought again
	case EItemKind::Axe:
		if (It.AxeLevel <= P.U6.AxeLevel) { return EItemStatus::Owned; }
		return It.AxeLevel == P.U6.AxeLevel + 1 ? EItemStatus::ForSale : EItemStatus::Locked;
	case EItemKind::Decor:
		if (!It.Decor) { return EItemStatus::Locked; }
		if (P.OwnsDecor(It.Decor->Id)) { return P.GetEquippedDecor(It.Decor->Slot) == It.Decor->Id ? EItemStatus::Equipped : EItemStatus::Owned; }
		return It.Decor->Price > 0 ? EItemStatus::ForSale : EItemStatus::RewardOnly;
	case EItemKind::Shovel:
		if (It.ShovelLevel <= P.ShovelLevel) { return EItemStatus::Owned; }
		return It.ShovelLevel == P.ShovelLevel + 1 ? EItemStatus::ForSale : EItemStatus::Locked;
	case EItemKind::Backpack:
		if (It.BackpackLevel <= P.BackpackLevel) { return EItemStatus::Owned; }
		return It.BackpackLevel == P.BackpackLevel + 1 ? EItemStatus::ForSale : EItemStatus::Locked;
	}
	return EItemStatus::Locked;
}

void DrawCosmeticIcon(FVDraw& D, const FFVCosmeticDef& C, const FVector2D& Center, float Scale)
{
	UFVSprites* Sprites = UFVSprites::Get();
	if (!Sprites)
	{
		return;
	}
	switch (C.Slot)
	{
	case EFVCosmeticSlot::Hat:
		D.SpriteCentered(Sprites->HatIcon(HatStyleOf(C), C.Primary, C.Secondary), Center, Scale);
		break;
	case EFVCosmeticSlot::Outfit:
		D.SpriteCentered(Sprites->OutfitIcon(OutfitStyleOf(C), C.Primary, C.Secondary), Center, Scale);
		break;
	case EFVCosmeticSlot::Bobber:
		D.SpriteCentered(Sprites->Bobber(BobberStyleOf(C), 0), Center, Scale * 2.f);   // 8x8 -> 16x16 footprint
		break;
	case EFVCosmeticSlot::Pet:
		D.SpriteCentered(Sprites->Pet(PetStyleOf(C), EFVPetAnim::Idle, 0), Center, Scale);
		break;
	default:
		break;
	}
}

void DrawItemIcon(FVDraw& D, const FShopItem& It, const FVector2D& Center, float Scale)
{
	UFVSprites* Sprites = UFVSprites::Get();
	if (!Sprites)
	{
		return;
	}
	switch (It.Kind)
	{
	case EItemKind::Rod:      if (It.Rod) { D.SpriteCentered(Sprites->RodIcon(*It.Rod), Center, Scale); } break;
	case EItemKind::Bait:
	{
		FFVSprite S;
		if (It.Bait && IconOf(It.Bait->Id, S)) { D.SpriteCentered(S, Center, Scale); }
		break;
	}
	case EItemKind::Cosmetic: if (It.Cos) { DrawCosmeticIcon(D, *It.Cos, Center, Scale); } break;
	case EItemKind::Keepnet:  D.SpriteCentered(Sprites->Icon(EFVIcon::Keepnet), Center, Scale); break;
	case EItemKind::Item:
	{
		FFVSprite S;
		if (It.Item && IconOf(It.Item->Id, S)) { D.SpriteCentered(S, Center, Scale); }
		break;
	}
	case EItemKind::Shovel:
	{
		const FFVSprite S = Sprites->ShovelIcon(FMath::Max(1, It.ShovelLevel));
		D.SpriteCentered(S, Center, Scale);
		break;
	}
	case EItemKind::Backpack: D.SpriteCentered(Sprites->Icon(EFVIcon::Backpack), Center, Scale); break;
	case EItemKind::Axe:      D.SpriteCentered(Sprites->AxeIcon(FMath::Max(1, It.AxeLevel)), Center, Scale); break;
	case EItemKind::Decor:    if (It.Decor) { D.SpriteCentered(Sprites->DecorIcon(*It.Decor), Center, Scale); } break;
	}
}

// ============================================================================================
// Rod stats (all converted to "bigger is better" numbers for display)
// ============================================================================================

struct FRodStat
{
	const TCHAR* Label;
	const TCHAR* Tip;
	float (*Value)(const FFVRodDef&);
	bool bPercent;
	bool bSigned;
	float FixedMax;               // > 0: use as the bar maximum instead of the best rod's value
};

float StatBar(const FFVRodDef& R)      { return R.BarBonus; }
float StatReel(const FFVRodDef& R)     { return (R.ReelMult - 1.f) * 100.f; }
float StatHold(const FFVRodDef& R)     { return (1.f - R.DrainMult) * 100.f; }
float StatBite(const FFVRodDef& R)     { return (1.f - R.BiteSpeed) * 100.f; }
float StatCast(const FFVRodDef& R)     { return R.CastRange * 100.f; }
float StatLuck(const FFVRodDef& R)     { return R.Luck * 100.f; }
float StatTreasure(const FFVRodDef& R) { return R.TreasureBonus * 100.f; }

const FRodStat GRodStats[] = {
	{ TEXT("Полоса поимки"), TEXT("Высота зелёной полосы в мини-игре. Чем она выше, тем проще удержать рыбу."), &StatBar, false, true, 0.f },
	{ TEXT("Подтягивание"), TEXT("Как быстро заполняется шкала улова, пока рыба внутри полосы."), &StatReel, true, true, 0.f },
	{ TEXT("Удержание"), TEXT("Насколько медленнее пустеет шкала, когда рыба вырывается из полосы."), &StatHold, true, true, 0.f },
	{ TEXT("Скорость клёва"), TEXT("Насколько быстрее рыба находит наживку."), &StatBite, true, true, 0.f },
	{ TEXT("Дальность заброса"), TEXT("Как далеко летит поплавок при полном замахе. На глубине водятся редкие рыбы."), &StatCast, true, false, 100.f },
	{ TEXT("Удача"), TEXT("Повышает шанс, что клюнет редкая рыба."), &StatLuck, true, true, 0.f },
	{ TEXT("Сокровища"), TEXT("Прибавка к шансу найти сундук с сокровищами во время вываживания."), &StatTreasure, true, true, 0.f },
};

float RodStatMax(const FRodStat& St)
{
	if (St.FixedMax > 0.f)
	{
		return St.FixedMax;
	}
	float M = 0.f;
	for (const FFVRodDef& R : FVDB::Rods())
	{
		M = FMath::Max(M, St.Value(R));
	}
	return FMath::Max(M, 1.f);
}

FString RodStatText(const FRodStat& St, float V)
{
	const int32 N = FMath::RoundToInt(V);
	const TCHAR* Sign = (St.bSigned && N > 0) ? TEXT("+") : TEXT("");
	return St.bPercent ? FString::Printf(TEXT("%s%d%%"), Sign, N) : FString::Printf(TEXT("%s%d"), Sign, N);
}

/** "+15%" (good, green) / "-5%" (bad, red) / "" (no change). */
FString SignedPercent(float V, bool bHigherIsBetter, FLinearColor& OutColor)
{
	const int32 N = FMath::RoundToInt(V);
	if (N == 0)
	{
		OutColor = FVPal::InkSoft();
		return FString();
	}
	const bool bGood = bHigherIsBetter ? (N > 0) : (N < 0);
	OutColor = bGood ? InkGood() : InkBad();
	return FString::Printf(TEXT("%s%d%%"), N > 0 ? TEXT("+") : TEXT("-"), FMath::Abs(N));
}

// ============================================================================================
// Shopkeeper lines
// ============================================================================================

const TCHAR* const GGreetings[] = {
	TEXT("Заходи, рыбачок, не стесняйся! Чего сегодня изволишь?"),
	TEXT("Удочка — что верный пёс: береги её, и она тебя не подведёт."),
	TEXT("Червячков свежих завёз — клюёт на них всё, что плавает!"),
	TEXT("Говорят, в пруду живёт карась размером с лодку. Брешут, поди… А может, и нет."),
	TEXT("Хорошая наживка — половина улова. А вторая половина — терпение."),
	TEXT("Эх, в мои-то годы я щук голыми руками ловил! Ну, почти."),
	TEXT("Шляпу бы тебе новую справить — солнце голову напечёт."),
	TEXT("Рыба любит тишину, а лавка — звон монет. Хе-хе!"),
	TEXT("Сундучок со дна вытащишь — покажи деду, интересно же!"),
};

const TCHAR* const GRainLines[] = {
	TEXT("Дождик — к клёву! Самое время закидывать."),
	TEXT("Ну и погодка! Рыба в дождь голодная, так и хватает."),
};
const TCHAR* const GNightLines[] = {
	TEXT("Не спится? Ночью сомы да угри на охоту выходят…"),
	TEXT("Поздновато для покупок… Ладно уж, для тебя открою."),
};
const TCHAR* const GMorningLines[] = {
	TEXT("Доброе утро! Кто рано встаёт — тому рыба клюёт."),
};

// Боцман Семёныч, the pier trader
const TCHAR* const GPierGreetings[] = {
	TEXT("Полундра! Заходи, юнга, на причале всё самое ходовое."),
	TEXT("Рыбу беру дороже, чем Митрич: морской воздух цену поднимает!"),
	TEXT("Леска титановая, поплавок спортивный — для морского волка ничего не жалко."),
	TEXT("Кто в море не бывал, тот страха не видал. А тунца — тем более."),
	TEXT("Сети у меня крепкие: ни краб, ни шторм не порвёт."),
	TEXT("Тридцать лет по морям ходил. Теперь вот торгую — тоже качка, только денежная."),
	TEXT("Слыхал? На глубине тут кальмары с бочку. Бери снасть посерьёзнее."),
	TEXT("Блёстки на блесне — что медяшка на кителе: рыба глаз не отведёт!"),
};
const TCHAR* const GPierRainLines[] = {
	TEXT("Штормит! Самое время для крупной рыбы — если удержишь."),
	TEXT("Дождь рыбе не помеха, а рыбаку — закалка."),
};
const TCHAR* const GPierNightLines[] = {
	TEXT("Ночная вахта? Уважаю. Ночью в море водится всякое…"),
	TEXT("Темень у причала — гляди под ноги, юнга."),
};
const TCHAR* const GPierMorningLines[] = {
	TEXT("С добрым утром, юнга! Отдать швартовы — и на лов!"),
};

// ============================================================================================
// The screen
// ============================================================================================

class FShopScreen final : public FVScreen
{
public:
	explicit FShopScreen(UFVGame* InGame)
		: FVScreen(InGame)
	{
		for (int32 i = 0; i < NumLists; ++i)
		{
			Selection[i] = -1;
			EnsureVis[i] = 2;
		}
	}

	virtual void OnOpen() override
	{
		// the trader of this stall (context), Mitrich when opened without one (autotest, old paths)
		FName Ctx = Game ? Game->GetScreenContext() : NAME_None;
		// update 6 (Р61): "<trader>:<decor id>" opens the «Декор» tab with that item selected (autotest shots, links)
		FName PreDecor;
		{
			const FString CS = Ctx.ToString();
			int32 Colon = INDEX_NONE;
			if (CS.FindChar(TEXT(':'), Colon))
			{
				PreDecor = FName(*CS.Mid(Colon + 1));
				Ctx = FName(*CS.Left(Colon));
			}
		}
		Trader = FVDB::FindTrader(Ctx);
		if (!Trader) { Trader = FVDB::FindTrader(FVDB::DefaultTraderId()); }
		if (!Trader) { Trader = &FallbackTrader(); }
		TraderId = Trader->Id;
		BuildTabs();
		Pager.NumPagesCached = FMath::Max(1, Tabs.Num());   // before the first Update: SetPage clamps to it
		Pager.SetPage(0);
		CurTab = ActiveTab();
		for (int32 i = 0; i < NumLists; ++i)
		{
			ListScroll[i].Reset();
			EnsureVis[i] = 2;   // the auto-selected row is shown at once the first time a list is drawn
		}
		if (const FFVDecorDef* Pre = PreDecor.IsNone() ? nullptr : FVDB::FindDecor(PreDecor))
		{
			const int32 Idx = TabIndexOf(EShopTab::Decor);
			if (Idx != INDEX_NONE)
			{
				Pager.SetPage(Idx);
				CurTab = ActiveTab();
				TArray<FShopItem> Items;
				BuildItems(Items);
				Selection[ListKey()] = Items.IndexOfByPredicate([Pre](const FShopItem& It) { return It.Decor == Pre; });
			}
		}
		PickGreeting();
	}

	virtual void Tick(float Dt, const FFVInput& In) override;
	virtual void Draw(FVDraw& D) override;

private:
	static constexpr int32 NumLists = int32(EShopTab::Count) + 4;   // one per tab + 4 cosmetic slots
	static constexpr float RowH = 28.f;
	static float RowHPortrait() { return FV::Ui::RowH(); }   // row pitch of the portrait (touch) list

	const FFVTraderDef* Trader = nullptr;       // never null after OnOpen
	FName TraderId;
	TArray<EShopTab> Tabs;                      // tabs of this trader, in display order
	FFVPager Pager;                             // one sliding page per tab; Pager.Page = the active tab's index in Tabs
	EShopTab CurTab = EShopTab::Rods;           // tab of the page being drawn (= the active tab between frames)
	EFVCosmeticSlot CurSlot = EFVCosmeticSlot::Hat;
	int32 Selection[NumLists];
	FFVScrollView ListScroll[NumLists];         // smooth scrolling of every list (the «Продать» grid included)
	uint8 EnsureVis[NumLists];                  // scroll the selected row into view: 0 no, 1 animated, 2 at once
	FString Greeting;
	double GreetStart = -1.0;                   // D.Time when the current line started (<0: restart next frame)
	int32 LastGreeting = -1;
	int32 BuyQty = 1;                           // portrait: packs / pieces bought by the bar's "Купить" button (1 / 5 / 10)

	/** Selected stack of the «Продать» tab, kept by identity (container, id, quality, flags). */
	struct FSellSel
	{
		EFVContainer C = EFVContainer::Keepnet;
		FName Id;
		uint8 Q = 0;
		uint8 Flags = 0;
		bool bHas = false;
		int32 LastIndex = 0;
	};
	FSellSel SellSel;

	int32 ListKey() const
	{
		return CurTab == EShopTab::Cosmetics ? int32(EShopTab::Count) + FMath::Clamp(int32(CurSlot), 0, 3)
			: FMath::Clamp(int32(CurTab), 0, int32(EShopTab::Count) - 1);
	}

	UFVSaveGame* Profile() const { return Game ? Game->GetProfile() : nullptr; }
	const FFVTraderDef& GetTrader() const { return Trader ? *Trader : FallbackTrader(); }
	/** The pier trader speaks as a sailor; Mitrich as a kind old man. */
	bool IsPierVoice() const { return IsClassicTrader() && GetTrader().Id != FVDB::DefaultTraderId() && GetTrader().Id != FallbackTrader().Id; }
	const TCHAR* Voice(const TCHAR* Mitrich, const TCHAR* Pier) const { return IsPierVoice() ? Pier : Mitrich; }
	/** §18: Мельник Прокоп — buys only smoked fish and crafted lures (the «Продать» tab hides the rest, lists the lures). */
	bool IsMiller() const { return (GetTrader().Mask & FVMill::MillerMask) != 0 && GetTrader().Id == FVMill::MillerTraderId(); }
	/** The «Продать» tab of the miller: crafted lures (P.Baits with bLure) that he buys, best price first. */
	TArray<FName> MillerLures(const UFVSaveGame& P) const;
	/** «Вся рыба»: keepnet units this trader pays for (the miller: smoked only; others: the whole keepnet). */
	int32 SellableFishUnits(const UFVSaveGame& P) const
	{
		if (!IsMiller()) { return P.KeepnetUsed(); }
		int32 N = 0;
		for (const FFVInvStack& S : P.GetContainer(EFVContainer::Keepnet)) { if (S.Count > 0 && SellPrice(S) > 0) { N += S.Count; } }
		return N;
	}

	void BuildTabs();
	int32 TabIndexOf(EShopTab T) const { return Tabs.IndexOfByKey(T); }
	EShopTab TabAt(int32 Page) const { return Tabs.IsValidIndex(Page) ? Tabs[Page] : EShopTab::Rods; }
	EShopTab ActiveTab() const { return TabAt(Pager.Page); }
	void BuildItems(TArray<FShopItem>& Out) const { BuildItemsFor(CurTab, CurSlot, Out); }
	/** Cosmetic slots this trader actually sells (a lone-hat trader shows no empty «Костюмы / Поплавки / Питомцы» tabs);
	    moves CurSlot to the first non-empty one when the current slot has nothing. */
	TArray<int32> CosmeticSlotsWithItems()
	{
		TArray<int32> Out;
		TArray<FShopItem> Tmp;
		for (int32 s = 0; s < 4; ++s)
		{
			Tmp.Reset();
			BuildItemsFor(EShopTab::Cosmetics, EFVCosmeticSlot(s), Tmp);
			if (Tmp.Num() > 0) { Out.Add(s); }
		}
		if (Out.Num() == 0) { Out.Add(int32(CurSlot)); }
		else if (!Out.Contains(int32(CurSlot))) { CurSlot = EFVCosmeticSlot(Out[0]); }
		return Out;
	}
	void BuildItemsFor(EShopTab Tab, EFVCosmeticSlot Slot, TArray<FShopItem>& Out) const;
	/** Update 6 (§4): cosmetics / decor by TraderMask; 0 = the old rule (bSellsClothes, classic traders only). */
	bool SellsCosmetic(const FFVCosmeticDef& C) const
	{
		const FFVTraderDef& T = GetTrader();
		return C.TraderMask != 0 ? (C.TraderMask & T.Mask) != 0 : (T.bSellsClothes && IsClassicTrader());
	}
	/** Mitrich / the pier boatswain (bits 1, 2): the old portraits and voices. */
	bool IsClassicTrader() const { return (GetTrader().Mask & 3) != 0 || GetTrader().NpcId.IsNone(); }
	bool IsBoutique() const { return GetTrader().bBoutique; }
	void TryBuyAxe(UFVSaveGame& P, int32 Level);
	void TryBuyDecor(UFVSaveGame& P, const FFVDecorDef& Dec);
	void DrawAxeDetail(FVDraw& D, UFVSaveGame& P, int32 Level, const FFVRect& Panel);
	void DrawDecorDetail(FVDraw& D, UFVSaveGame& P, const FFVDecorDef& Dec, const FFVRect& Panel);
	/** Greeting lines of an update-6 trader (Лесник, Снастёвщик, Огородница, Жанна). false = a classic trader. */
	bool U6Greetings(TArray<FString>& Lines) const;
	int32 AutoSelect(const UFVSaveGame& P, const TArray<FShopItem>& Items) const;
	/** The selected item of the list of the page being drawn (false: empty list / nothing selected yet). */
	bool SelectedItem(const UFVSaveGame& P, FShopItem& Out) const;
	/** Slides to the page of tab T (animated; the pager plays the click). */
	void GoToTab(EShopTab T);
	void Say(const FString& Line);
	void PickGreeting();

	/** Returns true if the screen was replaced (this object is gone: return immediately). */
	bool DrawHeader(FVDraw& D, UFVSaveGame& P);
	/** Tab row of this trader (FVKit::TabStrip: landscape folder tabs, portrait big buttons; animated indicator). */
	void DrawTabs(FVDraw& D, const FFVRect& Row);
	/** One page (both panels of CurTab) in landscape. */
	void DrawPageLandscape(FVDraw& D, UFVSaveGame& P);
	bool DrawList(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, FShopItem& OutSelected);
	void DrawRow(FVDraw& D, const UFVSaveGame& P, const FShopItem& It, const FFVRect& Row);
	FString RowSubText(const UFVSaveGame& P, const FShopItem& It, EItemStatus St) const;
	void DrawDetail(FVDraw& D, UFVSaveGame& P, const FShopItem* It, const FFVRect& Panel);
	float DrawDetailTitle(FVDraw& D, const FFVRect& Panel, const FString& Name, const FString& Category, int32 MinLevel, int32 PlayerLevel);
	void DrawPriceLine(FVDraw& D, const FFVRect& Panel, int64 Price, bool bAffordable);
	void DrawStatusLine(FVDraw& D, const FFVRect& Panel, const FString& Text, const FLinearColor& Color);
	/** Main action button of the landscape detail panel (portrait: the bottom action bar holds the actions). */
	static FFVRect ButtonRow(const FFVRect& Panel)
	{
		return FFVRect(Panel.X + 8.f, Panel.Bottom() - 28.f, Panel.W - 16.f, 20.f);
	}

	// ---- shared pieces & the portrait (phone) layout ----
	/** Shopkeeper picture in its frame (blinks; Mitrich smokes a pipe, the boatswain looks out of a porthole). Returns true when clicked. */
	bool DrawKeeper(FVDraw& D, const FFVRect& Frame);
	/** Speech bubble with the typewriter greeting (click: reveal / next line). */
	void DrawBubble(FVDraw& D, const FFVRect& Bubble, float TextSize, int32 MaxLines = 0);
	void DrawPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win);
	/** Keeper, name + role, purse and the bubble. Returns the height used (Area.H, more when the role needs its own line). */
	float DrawHeaderPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Area);
	/** One page (the list and detail panels of CurTab; «Продать»: + its extra button row) in portrait. */
	void DrawPagePortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win, float Top);
	bool DrawListPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, FShopItem& OutSelected);
	void DrawRowPortrait(FVDraw& D, const UFVSaveGame& P, const FShopItem& It, const FFVRect& Row);
	/** Price or state of the selected item, right of the detail title. */
	void DrawTitleBadgePortrait(FVDraw& D, const UFVSaveGame& P, const FShopItem& It, const FFVRect& Panel);
	/** The page's part of the bottom bar (right of "← Назад"; slides with the page): "Продать →" + the main action of
	    the selected item, or «Вся рыба» on the «Продать» page. */
	void DrawFooterActionsPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Actions);
	void DrawMainActionPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& R, const FShopItem* It);
	/** ×1 / ×5 / ×10 chips (portrait) for bait packs and items. Returns the X right of the chips. */
	float DrawQtyChips(FVDraw& D, float X, float Y, float H);

	// ---- the «Продать» tab (both layouts) ----
	int32 FindSellIndex(const UFVSaveGame& P) const;
	int32 ResolveSellSelection(const UFVSaveGame& P);
	int32 SellPrice(const FFVInvStack& S) const { return FVDB::TraderSellPrice(TraderId, S.ItemId, FVUI::QualityOf(S.Quality), S.Flags); }
	void DrawSellList(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, bool bPortrait);
	/** §18: one entry of the miller's lure block (Kind 3 header / 4 a lure row with its sell button / 5 hint). true = sold. */
	bool DrawMillerLureEntry(FVDraw& D, UFVSaveGame& P, uint8 Kind, FName Lure, const FFVRect& R, bool bPortrait, const FFVRect& Clip);
	void DrawSellDetail(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, bool bPortrait);
	/** [Продать 1][Стопку] row above the footer (portrait «Продать» page; slides with it). */
	void DrawSellRowPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win);
	void SellSelected(UFVSaveGame& P, int32 Count);
	void ConfirmSellAllFish(UFVSaveGame& P);

	// ---- actions shared by both layouts ----
	void DoEquipRod(UFVSaveGame& P, const FFVRodDef& Rod);
	void DoEquipBait(UFVSaveGame& P, const FFVBaitDef& Bait);
	void DoEquipCosmetic(UFVSaveGame& P, const FFVCosmeticDef& Cos);
	void DoUnequipCosmetic(UFVSaveGame& P, const FFVCosmeticDef& Cos);

	void DrawRodDetail(FVDraw& D, UFVSaveGame& P, const FFVRodDef& Rod, const FFVRect& Panel);
	void DrawBaitDetail(FVDraw& D, UFVSaveGame& P, const FFVBaitDef& Bait, const FFVRect& Panel);
	void DrawBaitDetailPortrait(FVDraw& D, UFVSaveGame& P, const FFVBaitDef& Bait, const FFVRect& Panel, float Y);
	void DrawCosmeticDetail(FVDraw& D, UFVSaveGame& P, const FFVCosmeticDef& Cos, const FFVRect& Panel);
	void DrawCosmeticPreview(FVDraw& D, const UFVSaveGame& P, const FFVCosmeticDef& Cos, const FFVRect& Box);
	/** Keepnet (bBackpack false) or backpack upgrade to level index Target. */
	void DrawUpgradeDetail(FVDraw& D, UFVSaveGame& P, bool bBackpack, int32 Target, const FFVRect& Panel);
	void DrawItemDetail(FVDraw& D, UFVSaveGame& P, const FFVItemDef& Item, const FFVRect& Panel);
	void DrawShovelDetail(FVDraw& D, UFVSaveGame& P, int32 Level, const FFVRect& Panel);

	void TryBuyRod(UFVSaveGame& P, const FFVRodDef& Rod);
	void TryBuyBait(UFVSaveGame& P, const FFVBaitDef& Bait, int32 Packs);
	void TryBuyCosmetic(UFVSaveGame& P, const FFVCosmeticDef& Cos);
	void TryUpgradeKeepnet(UFVSaveGame& P);
	void TryUpgradeBackpack(UFVSaveGame& P);
	void TryBuyItem(UFVSaveGame& P, const FFVItemDef& Item, int32 Count);
	void TryBuyShovel(UFVSaveGame& P, int32 Level);
	void ReportFailure(EFVBuyResult Res, const UFVSaveGame& P, int64 Price, int32 MinLevel);
};

// -------------------------------------------------------------------------------- data

void FShopScreen::BuildTabs()
{
	const FFVTraderDef& T = GetTrader();
	Tabs.Reset();
	// update 6: a tab is shown only when this trader has something in it (Лесник: clothes + his buy list; бутик:
	// elite clothes and decor; огородница: seeds); the classic traders keep their old tab set
	TArray<FShopItem> Tmp;
	auto Has = [&](EShopTab Tab) { BuildItemsFor(Tab, CurSlot, Tmp); return Tmp.Num() > 0; };
	const bool bClassic = IsClassicTrader();
	for (const EShopTab Tab : { EShopTab::Rods, EShopTab::Bait, EShopTab::Tackle })
	{
		if (bClassic || Has(Tab)) { Tabs.Add(Tab); }
	}
	bool bAnyCos = false;
	for (int32 s = 0; s < 4 && !bAnyCos; ++s)
	{
		BuildItemsFor(EShopTab::Cosmetics, EFVCosmeticSlot(s), Tmp);
		bAnyCos = Tmp.Num() > 0;
	}
	if ((bClassic && T.bSellsClothes) || bAnyCos) { Tabs.Add(EShopTab::Cosmetics); }
	if (T.bSellsUpgrades) { Tabs.Add(EShopTab::Upgrades); }
	if (T.bSellsSeeds || Has(EShopTab::Seeds)) { Tabs.Add(EShopTab::Seeds); }
	if (Has(EShopTab::Decor)) { Tabs.Add(EShopTab::Decor); }
	// «Продать»: a list-only buyer that has nothing on its list (прораб, бутик) buys nothing
	bool bBuys = !T.bBuysOnlyList;
	for (int32 i = 0; !bBuys && i < int32(EFVItemKind::Count); ++i)
	{
		for (const FFVItemDef* Def : FVDB::ItemsOfKind(EFVItemKind(i)))
		{
			if (Def && (Def->BuyerMask & T.Mask) != 0) { bBuys = true; break; }
		}
	}
	if (bBuys) { Tabs.Add(EShopTab::Sell); }
	if (Tabs.Num() == 0) { Tabs.Add(EShopTab::Tackle); }
}

void FShopScreen::BuildItemsFor(EShopTab Tab, EFVCosmeticSlot Slot, TArray<FShopItem>& Out) const
{
	Out.Reset();
	const uint8 Mask = GetTrader().Mask;
	switch (Tab)
	{
	case EShopTab::Rods:
	{
		// 0.6.2 (Р80): the city tackle stall also lists the forged expert rods (for information: where they come from)
		const bool bShowForged = GetTrader().Id == FName(TEXT("tr_city_tackle"));
		for (const FFVRodDef& R : FVDB::Rods())
		{
			const bool bSold = (R.TraderMask & Mask) != 0 && R.Price > 0;   // not: the starter rod and the other trader's rods
			const bool bForged = bShowForged && R.TraderMask == 0 && FVDBU6::RodRecipe(R.Id) != nullptr;
			if (!bSold && !bForged) { continue; }
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Rod;
			It.Rod = &R;
			It.bForgeInfo = !bSold;
		}
	}
		break;
	case EShopTab::Bait:
		for (const FFVBaitDef& B : FVDB::Baits())
		{
			// crafted / dug / farmed baits (PackPrice 0, lures) are never sold
			if ((B.TraderMask & Mask) == 0 || B.PackPrice <= 0 || B.bLure) { continue; }
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Bait;
			It.Bait = &B;
		}
		break;
	case EShopTab::Tackle:
	{
		// lines, floats, nets, materials, then the shovels of this trader
		// update 6: expert reels and blueprints (Снасти Города: the pickaxe blueprint) are tackle too
		static const EFVItemKind Kinds[] = { EFVItemKind::Line, EFVItemKind::Float, EFVItemKind::Reel, EFVItemKind::Net, EFVItemKind::Material,
			EFVItemKind::Blueprint };
		for (const EFVItemKind K : Kinds)
		{
			for (const FFVItemDef* Def : FVDB::ItemsOfKind(K))
			{
				if (!Def || Def->BuyPrice <= 0 || (Def->TraderMask & Mask) == 0) { continue; }
				FShopItem& It = Out.AddDefaulted_GetRef();
				It.Kind = EItemKind::Item;
				It.Item = Def;
			}
		}
		const TArray<FFVShovelDef>& Shovels = FVDB::Shovels();
		for (int32 i = 0; i < Shovels.Num(); ++i)
		{
			if (Shovels[i].Price <= 0 || (Shovels[i].TraderMask & Mask) == 0) { continue; }
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Shovel;
			It.ShovelLevel = i + 1;
		}
		// update 6: the axe (level 1 at Боцман; the next levels are made in the workshop)
		const TArray<FFVToolDef>& Axes = FVDBU6::Axes();
		for (int32 i = 0; i < Axes.Num(); ++i)
		{
			if (Axes[i].Price <= 0 || (Axes[i].TraderMask & Mask) == 0) { continue; }
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Axe;
			It.AxeLevel = i + 1;
		}
		break;
	}
	case EShopTab::Seeds:
		for (const FFVItemDef* Def : FVDB::ItemsOfKind(EFVItemKind::Seed))
		{
			if (!Def || Def->BuyPrice <= 0 || (Def->TraderMask & Mask) == 0) { continue; }
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Item;
			It.Item = Def;
		}
		break;
	case EShopTab::Decor:
		for (const FFVDecorDef& Dec : FVDB::Decor())
		{
			if (Dec.Price <= 0 || (Dec.TraderMask & Mask) == 0) { continue; }
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Decor;
			It.Decor = &Dec;
		}
		break;
	case EShopTab::Cosmetics:
	{
		const TArray<const FFVCosmeticDef*> All = FVDB::CosmeticsFor(Slot);
		const TArray<FName> Starters = FVDB::StartCosmetics();   // starting items are never sold
		TArray<const FFVCosmeticDef*> Rewards;
		for (const FFVCosmeticDef* C : All)
		{
			if (!C || Starters.Contains(C->Id) || !SellsCosmetic(*C)) { continue; }
			if (C->Slot == EFVCosmeticSlot::Hat && HatStyleOf(*C) == EFVHatStyle::None) { continue; }
			if (C->Slot == EFVCosmeticSlot::Pet && PetStyleOf(*C) == EFVPetStyle::None) { continue; }
			if (C->Price > 0)
			{
				FShopItem& It = Out.AddDefaulted_GetRef();
				It.Kind = EItemKind::Cosmetic;
				It.Cos = C;
			}
			else
			{
				Rewards.Add(C);   // reward-only items go last, marked "Награда"
			}
		}
		for (const FFVCosmeticDef* C : Rewards)
		{
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Cosmetic;
			It.Cos = C;
		}
		break;
	}
	case EShopTab::Upgrades:
		for (int32 i = 1; i < FVDB::KeepnetLevels().Num(); ++i)
		{
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Keepnet;
			It.KeepnetLevel = i;
		}
		for (int32 i = 1; i < FVDB::BackpackLevels().Num(); ++i)
		{
			FShopItem& It = Out.AddDefaulted_GetRef();
			It.Kind = EItemKind::Backpack;
			It.BackpackLevel = i;
		}
		break;
	default:
		break;   // Sell: its own grid (DrawSellList)
	}
}

int32 FShopScreen::AutoSelect(const UFVSaveGame& P, const TArray<FShopItem>& Items) const
{
	if (Items.Num() == 0)
	{
		return -1;
	}
	const int32 Level = P.GetLevel();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (GetStatus(P, Items[i]) == EItemStatus::ForSale && Level >= ItemMinLevel(Items[i])) { return i; }
	}
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (GetStatus(P, Items[i]) == EItemStatus::ForSale) { return i; }
	}
	return 0;
}

bool FShopScreen::SelectedItem(const UFVSaveGame& P, FShopItem& Out) const
{
	TArray<FShopItem> Items;
	BuildItems(Items);
	const int32 Sel = Selection[ListKey()];
	if (!Items.IsValidIndex(Sel))
	{
		return false;
	}
	Out = Items[Sel];
	return true;
}

void FShopScreen::GoToTab(EShopTab T)
{
	const int32 Idx = TabIndexOf(T);
	if (Idx != INDEX_NONE)
	{
		Pager.GoTo(Idx);
	}
}

void FShopScreen::Say(const FString& Line)
{
	Greeting = Line;
	GreetStart = -1.0;
}

void FShopScreen::PickGreeting()
{
	TArray<FString> Lines;
	if (U6Greetings(Lines))
	{
		if (Lines.Num() > 0)
		{
			int32 Idx = FMath::RandRange(0, Lines.Num() - 1);
			if (Lines.Num() > 1 && Idx == LastGreeting) { Idx = (Idx + 1) % Lines.Num(); }
			LastGreeting = Idx;
			Say(Lines[Idx]);
		}
		return;
	}
	const bool bPier = IsPierVoice();
	if (bPier) { for (const TCHAR* L : GPierGreetings) { Lines.Add(L); } }
	else { for (const TCHAR* L : GGreetings) { Lines.Add(L); } }
	if (Game)
	{
		if (Game->GetWeather() == EFVWeather::Rain)
		{
			if (bPier) { for (const TCHAR* L : GPierRainLines) { Lines.Add(L); Lines.Add(L); } }
			else { for (const TCHAR* L : GRainLines) { Lines.Add(L); Lines.Add(L); } }
		}
		const EFVDayPhase Phase = Game->GetPhase();
		if (Phase == EFVDayPhase::Night)
		{
			if (bPier) { for (const TCHAR* L : GPierNightLines) { Lines.Add(L); Lines.Add(L); } }
			else { for (const TCHAR* L : GNightLines) { Lines.Add(L); Lines.Add(L); } }
		}
		if (Phase == EFVDayPhase::Morning)
		{
			if (bPier) { for (const TCHAR* L : GPierMorningLines) { Lines.Add(L); Lines.Add(L); } }
			else { for (const TCHAR* L : GMorningLines) { Lines.Add(L); Lines.Add(L); } }
		}
	}
	if (const UFVSaveGame* P = Profile())
	{
		if (P->KeepnetFull())
		{
			Lines.Add(Voice(TEXT("Опять садок битком? Вот это я понимаю — рыбак! Продай улов, да бери обновку."),
				TEXT("Садок трещит по швам! Сдавай улов, юнга, — у меня за него дают больше.")));
		}
		if (P->Stats.FishCaught == 0)
		{
			Lines.Add(Voice(TEXT("Новенький? Бери червячков да ступай к пруду — караси там смирные."),
				TEXT("Салага? Ничего, море всех научит. Бери наживку и марш на причал!")));
		}
		if (!bPier && P->ShovelLevel <= 0 && P->Stats.FishCaught > 0)
		{
			Lines.Add(TEXT("Без лопаты-то червей не накопаешь. Глянь в «Снастях» — хорошая лопата, крепкая."));
		}
		// update 5: rumours about the legends — Mitrich knows the first three, Semyonych the last three; told until the
		// legend is caught, from one level before it wakes up (Docs/UPDATE5_LEGENDS.md §3)
		struct FRumour { const TCHAR* Fish; bool bPierVoice; const TCHAR* Text; };
		static const FRumour Rumours[] = {
			{ TEXT("fish_tsar_carp"), false, TEXT("Царь-Карп, говорят, на тесто идёт. Замеси на кухне — и утречком на глубину.") },
			{ TEXT("fish_silver_sturgeon"), false, TEXT("Осётр в дождь поднимается. Только льняную он рвёт, как нитку — ставь плетёную в мастерской.") },
			{ TEXT("fish_crystal_trout"), false, TEXT("Хрустальная форель толстую леску за версту чует. Заведи вторую удочку с шёлковой, а на крючок — мушку.") },
			{ TEXT("fish_giant_angler"), true, TEXT("Удильщик ночью на свет идёт — ставь светящийся поплавок. И флюорокарбон, не то перекусит.") },
			{ TEXT("fish_fog_serpent"), true, TEXT("Туманный змей искусственное не берёт. Выползка ему, живого! И леску покрепче.") },
			{ TEXT("fish_zhar_ryba"), true, TEXT("Жар-рыба? Обычная леска в лаве — как волос в костре. Сплети жаропрочную на станке: титановая струна, обсидиановые панцири, глина, клей.") },
			{ TEXT("fish_zhar_ryba"), true, TEXT("Жар-рыбе на крючок — Жар-наживку из печи. И ухи поешь, сынок.") },
		};
		for (const FRumour& R : Rumours)
		{
			const FFVFishDef* F = FVDB::FindFish(FName(R.Fish));
			const FFVFishRecord* Rec = F ? P->GetRecord(F->Id) : nullptr;
			if (R.bPierVoice == bPier && F && !(Rec && Rec->Caught > 0) && P->GetLevel() >= F->MinLevel - 1)
			{
				Lines.Add(R.Text);
				Lines.Add(R.Text);
			}
		}
	}
	if (Lines.Num() == 0)
	{
		return;
	}
	int32 Idx = FMath::RandRange(0, Lines.Num() - 1);
	if (Lines.Num() > 1 && Idx == LastGreeting)
	{
		Idx = (Idx + 1) % Lines.Num();
	}
	LastGreeting = Idx;
	Say(Lines[Idx]);
}

bool FShopScreen::U6Greetings(TArray<FString>& Lines) const
{
	if (IsClassicTrader())
	{
		return false;
	}
	const FFVTraderDef& T = GetTrader();
	if (T.bBoutique)
	{
		Lines.Add(TEXT("Добро пожаловать в «Жемчужину»! Здесь всё — только лучшее."));
		Lines.Add(TEXT("Элегантность, мой друг, не терпит спешки. Присмотритесь."));
		Lines.Add(TEXT("Эта вещь создана для того, кто поймал своего марлина."));
	}
	else if (IsMiller())
	{
		Lines.Add(TEXT("Мели, Емеля, твоя неделя! Рожь принёс — мука будет."));
		Lines.Add(TEXT("Копчёную рыбку люблю — возьму дороже, чем в лавке."));
		Lines.Add(TEXT("Блёсны сам мастеришь? Неси, мне на омут пригодятся."));
	}
	else if (T.bSellsSeeds)
	{
		Lines.Add(TEXT("Семечко к семечку — и огород полон! Что сажать будем?"));
		Lines.Add(TEXT("Морковку поливай, милок, она воду любит."));
		Lines.Add(TEXT("Овощи со своего огорода принесёшь — возьму подороже."));
	}
	else if (T.bBuysOnlyList)
	{
		Lines.Add(TEXT("Ветки, брёвна, доски — всё беру, плачу честно."));
		Lines.Add(TEXT("Лес рубишь — меня не забывай. Хорошее дерево на вес монеты."));
		Lines.Add(TEXT("Глянь мою одёжку: в лесу в самый раз."));
	}
	else
	{
		Lines.Add(TEXT("Экспертная снасть — не игрушка. Спрашивай, объясню."));
		Lines.Add(TEXT("Бриз или Близнецы? Каждой рыбе — своя удочка."));
		Lines.Add(TEXT("Кирку без чертежа не сделаешь. Чертёж — у меня."));
	}
	return true;
}

// -------------------------------------------------------------------------------- tick

void FShopScreen::Tick(float Dt, const FFVInput& In)
{
	if (!Game)
	{
		return;
	}
	CurTab = ActiveTab();

	// ←/→ slide to the neighbour tab (the pager animates and plays the click); the mouse wheel and finger drags are
	// read by the lists themselves (FFVScrollView)
	if (In.WasPressed(EKeys::Left))
	{
		Pager.GoTo(Pager.Page - 1);
	}
	else if (In.WasPressed(EKeys::Right))
	{
		Pager.GoTo(Pager.Page + 1);
	}

	const int32 Move = In.WasPressed(EKeys::Up) ? -1 : (In.WasPressed(EKeys::Down) ? 1 : 0);
	if (Move != 0 && CurTab != EShopTab::Sell)
	{
		TArray<FShopItem> Items;
		BuildItems(Items);
		if (Items.Num() > 0)
		{
			int32& Sel = Selection[ListKey()];
			const int32 NewSel = FMath::Clamp(Sel < 0 ? 0 : Sel + Move, 0, Items.Num() - 1);
			if (NewSel != Sel)
			{
				Sel = NewSel;
				EnsureVis[ListKey()] = 1;
				PlaySfx(EFVSfx::Click, 0.5f);
			}
		}
	}
}

// -------------------------------------------------------------------------------- draw

void FShopScreen::Draw(FVDraw& D)
{
	if (!Game)
	{
		return;
	}
	const bool bPortrait = FV::IsPortrait();
	const FFVRect Win = FV::WindowRect();   // same frame as the other overlay screens (landscape: below the HUD bar)
	const FString Title = GetTrader().ShopTitle.IsEmpty() ? FString(TEXT("Лавка")) : GetTrader().ShopTitle;
	if (D.Window(Win, Title))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	if (!FV::IsTouchUI() && !bPortrait) (void)0; /* update 3: no key hints at the bottom (see the guide) */

	UFVSaveGame* P = Profile();
	if (!P)
	{
		D.Text(TEXT("Лавка закрыта: профиль игрока не загружен."), Win.Center() - FVector2D(0.f, 4.f), 8.f, FVPal::InkSoft(), EFVAlign::Center);
		return;
	}

	CurTab = ActiveTab();
	if (bPortrait)
	{
		DrawPortrait(D, *P, Win);
		return;
	}

	if (DrawHeader(D, *P))
	{
		return;   // switched to another screen; this object is gone
	}

	// The pages: both panels of a tab slide together (swipe anywhere over them); the keeper, the purse and the tab row
	// stay put. The tabs are drawn last: the active folder tab covers the list panel's top edge.
	const FFVRect PageArea(16.f, 112.f, 608.f, Win.Bottom() - 5.f - 112.f);
	Pager.Update(D, PageArea, Tabs.Num());
	Pager.DrawPages(D, PageArea, [this, &D, P](int32 Page)
	{
		CurTab = TabAt(Page);
		DrawPageLandscape(D, *P);
	});
	CurTab = ActiveTab();

	const int32 N = FMath::Max(1, Tabs.Num());
	const float TabW = FMath::Min(77.f, FMath::FloorToFloat((330.f - 8.f - 2.f * float(N - 1)) / float(N)));
	DrawTabs(D, FFVRect(24.f, 97.f, float(N) * TabW + 2.f * float(N - 1), 16.f));
}

void FShopScreen::DrawPageLandscape(FVDraw& D, UFVSaveGame& P)
{
	const FFVRect ListPanel(20.f, 112.f, 330.f, 227.f);
	const FFVRect DetailPanel(354.f, 112.f, 266.f, 227.f);
	D.Panel(ListPanel, EFVPanel::Parchment);
	D.Panel(DetailPanel, EFVPanel::Parchment);
	if (CurTab == EShopTab::Sell)
	{
		DrawSellList(D, P, ListPanel, false);
		DrawSellDetail(D, P, DetailPanel, false);
		return;
	}
	FShopItem Selected;
	const bool bHasSelection = DrawList(D, P, ListPanel, Selected);
	DrawDetail(D, P, bHasSelection ? &Selected : nullptr, DetailPanel);
}

bool FShopScreen::DrawHeader(FVDraw& D, UFVSaveGame& P)
{
	// ---- the keeper in his frame ----
	if (DrawKeeper(D, FFVRect(22.f, 41.f, 52.f, 52.f)))
	{
		PlaySfx(EFVSfx::Click, 0.6f);
		PickGreeting();
	}

	// ---- name + speech bubble with a typewriter line ----
	const FFVTraderDef& T = GetTrader();
	D.Text(FitText(D, T.Name, 180.f, 9.f), FVector2D(84.f, 40.f), 9.f, FVPal::Ink(), EFVAlign::Left, false, true);
	const float NameW = FMath::Min(180.f, D.MeasureText(T.Name, 9.f).X);
	// the role («что покупает / продаёт») stays on the name row when it fits; a longer one (Мельник) gets its own line
	// under the name, up to the purse, and the bubble gives up that room (its bottom stays put)
	float BubbleY = 53.f;
	if (D.MeasureText(T.Role, 6.5f, false).X <= 300.f - NameW)
	{
		D.Text(T.Role, FVector2D(90.f + NameW, 42.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, false);
	}
	else
	{
		const float RoleH = TextWrappedLimited(D, T.Role, FVector2D(84.f, 53.5f), 320.f, 6.5f, FVPal::InkSoft(), 2, false, 8.5f);
		BubbleY = FMath::RoundToFloat(53.5f + RoleH + 2.f);
	}
	DrawBubble(D, FFVRect(86.f, BubbleY, 292.f, 91.f - BubbleY), 7.5f);

	// ---- purse, level, keepnet & backpack, "sell" tab shortcut ----
	const FFVRect Purse(410.f, 41.f, 208.f, 24.f);
	D.Panel(Purse, EFVPanel::Parchment);
	D.Text(TEXT("Кошелёк"), FVector2D(Purse.X + 8.f, Purse.Y + 7.f), 7.5f, FVPal::InkSoft());
	D.MoneyLabel(FVector2D(Purse.Right() - 8.f, Purse.Y + 5.f), P.Money, 11.f, EFVAlign::Right, FVPal::Ink());

	D.Text(FString::Printf(TEXT("Уровень рыбака: %d"), P.GetLevel()), FVector2D(Purse.X + 2.f, 68.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, false);
	const int32 Used = P.KeepnetUsed();
	const int32 Cap = P.KeepnetCapacity();
	D.Text(FString::Printf(TEXT("Садок: %d / %d"), Used, Cap), FVector2D(Purse.X + 2.f, 77.f), 6.5f,
		Used >= Cap ? InkBad() : FVPal::InkSoft(), EFVAlign::Left, false, Used >= Cap);
	const int32 BUsed = P.ContainerUsed(EFVContainer::Backpack);
	const int32 BCap = P.ContainerCapacity(EFVContainer::Backpack);
	D.Text(FString::Printf(TEXT("Рюкзак: %d / %d"), BUsed, BCap), FVector2D(Purse.X + 2.f, 86.f), 6.5f,
		BUsed >= BCap ? InkBad() : FVPal::InkSoft(), EFVAlign::Left, false, BUsed >= BCap);

	const FFVRect SellBtn(Purse.Right() - 104.f, 71.f, 104.f, 20.f);
	if (CurTab == EShopTab::Sell)
	{
		if (D.Button(SellBtn, TEXT("← К покупкам"), EFVButton::Normal, true, 7.5f) && Tabs.Num() > 0)
		{
			Pager.GoTo(0);
		}
	}
	else
	{
		if (D.Button(SellBtn, TEXT("Продать улов →"), Used > 0 ? EFVButton::Primary : EFVButton::Normal, true, 7.5f))
		{
			GoToTab(EShopTab::Sell);
		}
		if (D.Hover(SellBtn))
		{
			D.Tooltip(TEXT("Продать"), FString::Printf(TEXT("Рыба и вещи из рюкзака.\nВесь улов из садка здесь стоит %s."),
				*FVUI::Coins(P.ContainerValue(EFVContainer::Keepnet, TraderId))));
		}
	}
	return false;
}

bool FShopScreen::DrawKeeper(FVDraw& D, const FFVRect& Frame)
{
	const bool bPier = IsPierVoice();
	D.Panel(Frame, EFVPanel::Parchment);
	const FFVRect Pic = Frame.Inset(2.f);
	if (!IsClassicTrader())
	{
		// update 6: the villager of this stall (UFVSprites::NpcPortrait) in front of a backdrop of the trade
		const FFVTraderDef& T = GetTrader();
		const bool bMill = IsMiller();
		const FLinearColor Top = bMill ? FV::Col(206, 170, 104) : T.bBoutique ? FV::Col(120, 70, 110) : (T.bSellsSeeds ? FV::Col(150, 200, 120)
			: (T.bBuysOnlyList ? FV::Col(96, 130, 78) : FV::Col(86, 120, 150)));
		const FLinearColor Bot = bMill ? FV::Col(120, 86, 52) : T.bBoutique ? FV::Col(70, 36, 66) : (T.bSellsSeeds ? FV::Col(110, 150, 80)
			: (T.bBuysOnlyList ? FV::Col(60, 90, 50) : FV::Col(50, 76, 104)));
		const int32 Bands = 6;
		for (int32 b = 0; b < Bands; ++b)
		{
			D.Rect(FFVRect(Pic.X, Pic.Y + Pic.H * float(b) / float(Bands), Pic.W, Pic.H / float(Bands) + 1.f),
				FMath::Lerp(Top, Bot, float(b) / float(Bands - 1)));
		}
		if (T.bBoutique)
		{
			// pearls glinting on the velvet
			for (int32 k = 0; k < 4; ++k)
			{
				const float Tw = 0.5f + 0.5f * FMath::Sin(float(D.Time) * 2.f + float(k) * 1.7f);
				D.Rect(FFVRect(Pic.X + 4.f + float(k * 11 % 40), Pic.Y + 3.f + float(k * 7 % 10), 1.f, 1.f), FV::Col(255, 240, 250, uint8(120.f + 120.f * Tw)));
			}
		}
		if (UFVSprites* Sprites = UFVSprites::Get())
		{
			const FFVSprite& Face = Sprites->NpcPortrait(T.NpcId);
			const float Sc = FMath::Max(1.f, FMath::FloorToFloat(FMath::Min(Pic.W / FMath::Max(1.f, float(Face.W)), Pic.H / FMath::Max(1.f, float(Face.H)))));
			const float Breath = FMath::RoundToFloat(FMath::Sin(float(D.Time) * 1.8f) * 0.6f);
			D.SpriteCentered(Face, FVector2D(Pic.Center().X, Pic.Bottom() - float(Face.H) * Sc * 0.5f + Breath), Sc);
		}
		D.Frame(Pic, FVPal::WoodDark());
		return D.Clicked(Frame);
	}
	if (bPier)
	{
		// weathered blue planks and a brass porthole with the sea behind the boatswain
		D.Rect(Pic, FV::Col(96, 118, 146));
		for (int32 i = 1; i < 4; ++i)
		{
			D.Rect(FFVRect(Pic.X, Pic.Y + float(i) * 12.f, Pic.W, 1.f), FV::Col(72, 90, 116));
		}
		const FVector2D C = Pic.Pos() + FVector2D(11.f, 11.f);
		D.Circle(C, 8.5f, FV::Col(120, 84, 34));
		D.Circle(C, 7.5f, FV::Col(214, 170, 72));
		D.Circle(C, 6.f, FV::Col(160, 212, 240));
		// sea below the horizon (rows clipped to the glass circle), a gentle swell
		const float Swell = FMath::Sin(float(D.Time) * 1.6f) * 0.8f;
		for (int32 Dy = 0; Dy <= 5; ++Dy)
		{
			const float Yr = float(Dy) + 0.5f;
			const float HalfW = FMath::Sqrt(FMath::Max(0.f, 36.f - Yr * Yr));
			D.Rect(FFVRect(FMath::RoundToFloat(C.X - HalfW), FMath::RoundToFloat(C.Y + Swell + float(Dy)), FMath::RoundToFloat(HalfW * 2.f), 1.f),
				Dy == 0 ? FV::Col(200, 232, 250) : FV::Col(40, 104, 170));
		}
		// a seagull gliding past now and then
		const float G = float(FMath::Frac(D.Time * 0.09));
		if (G < 0.35f)
		{
			const FVector2D Gull = C + FVector2D(-7.f + G / 0.35f * 14.f, -3.f + FMath::Sin(G * 30.f) * 0.6f);
			D.Rect(FFVRect(Gull.X - 1.f, Gull.Y, 1.f, 1.f), FV::Col(250, 250, 250));
			D.Rect(FFVRect(Gull.X + 1.f, Gull.Y, 1.f, 1.f), FV::Col(250, 250, 250));
			D.Rect(FFVRect(Gull.X, Gull.Y + 1.f, 1.f, 1.f), FV::Col(250, 250, 250));
		}
	}
	else
	{
		D.Rect(Pic, FV::Col(184, 118, 68));
		for (int32 i = 1; i < 4; ++i)
		{
			D.Rect(FFVRect(Pic.X, Pic.Y + float(i) * 12.f, Pic.W, 1.f), FV::Col(148, 90, 50));
		}
		const FFVRect Window(Pic.X + 4.f, Pic.Y + 4.f, 14.f, 12.f);
		D.Rect(Window, FV::Col(150, 206, 240));
		D.Rect(FFVRect(Window.X, Window.Y + 8.f, Window.W, 4.f), FV::Col(120, 186, 90));
		D.Rect(FFVRect(Window.X + 6.f, Window.Y, 2.f, Window.H), FVPal::WoodDark());
		D.Frame(Window, FVPal::WoodDark());
	}

	const double Blink = FMath::Fmod(D.Time + (bPier ? 2.9 : 1.3), bPier ? 5.3 : 4.7);
	FFVSprite Portrait;
	if (GetShopkeeperSprite(Blink < 0.13 ? 1 : 0, Portrait, bPier))
	{
		D.Sprite(Portrait, Pic.Pos(), 2.f);
	}
	if (!bPier)
	{
		// pipe smoke puffs rising from the bowl (texel 18.5, 11)
		const FVector2D Bowl = Pic.Pos() + FVector2D(18.5f, 11.f) * 2.f;
		for (int32 k = 0; k < 3; ++k)
		{
			const float T = float(FMath::Frac(D.Time * 0.35 + double(k) / 3.0));
			const FVector2D Pos = Bowl + FVector2D(FMath::Sin(T * 5.f + float(k)) * 2.f + T * 5.f, -T * 20.f);
			if (Pos.Y > Pic.Y + 2.f)
			{
				D.Circle(Pos, 1.2f + T * 2.6f, FV::Col(236, 232, 226, uint8(170.f * (1.f - T))));
			}
		}
	}
	D.Frame(Pic, FVPal::WoodDark());
	return D.Clicked(Frame);
}

void FShopScreen::DrawBubble(FVDraw& D, const FFVRect& Bubble, float TextSize, int32 MaxLines)
{
	D.Rect(Bubble, FV::Col(255, 248, 228));
	D.Frame(Bubble, FV::Col(150, 100, 66));
	Triangle(D, FVector2D(Bubble.X, Bubble.Y + 14.f), 2, 5, FV::Col(150, 100, 66));
	Triangle(D, FVector2D(Bubble.X + 1.f, Bubble.Y + 14.f), 2, 4, FV::Col(255, 248, 228));
	if (GreetStart < 0.0)
	{
		GreetStart = D.Time;
	}
	// autotest shots get the whole line at once (a shot taken mid-typewriter reads like a cut phrase)
	const int32 Shown = (Game && Game->IsAutoTest()) ? Greeting.Len() : FMath::Clamp(int32((D.Time - GreetStart) * 45.0), 0, Greeting.Len());

	// The whole line must be readable in the bubble: the type steps down by 0.5 (never below FV::Ui::Small) until the
	// full line wraps into the lines the bubble holds, so a long rumour neither ends in "…" nor runs out of the frame.
	// Sized by the full line, so the text does not change size while it is being typed.
	const bool bLimited = MaxLines > 0;
	const float Pad = bLimited ? 4.f : 5.f;
	const float TextW = Bubble.W - (bLimited ? 14.f : 12.f);
	const float MinSize = FMath::Min(TextSize, FV::Ui::Small());
	float Size = TextSize;
	float Step = 0.f;
	int32 Budget = 1;
	int32 Need = 1;
	for (;;)
	{
		const float One = D.MeasureWrapped(TEXT("Щ"), TextW, Size, false);   // one line of D.TextWrapped
		Step = bLimited ? FMath::RoundToFloat(D.LineHeight(Size, false) * 0.95f) : One;
		// lines whose baseline stays inside the frame (the descenders of the last one may touch it, as before)
		Budget = FMath::Max(1, 1 + FMath::FloorToInt((Bubble.H - Pad - D.LineHeight(Size, false) * 0.79f + 1.f) / FMath::Max(1.f, Step)));
		if (bLimited) { Budget = FMath::Min(Budget, MaxLines); }
		Need = One > 0.f ? FMath::Max(1, FMath::RoundToInt(D.MeasureWrapped(Greeting, TextW, Size, false) / One)) : 1;
		if (Need <= Budget || Size - 0.5f < MinSize - 0.01f)
		{
			break;
		}
		Size -= 0.5f;
	}
	if (bLimited || Need > Budget)
	{
		TextWrappedLimited(D, Greeting.Left(Shown), FVector2D(Bubble.X + 6.f, Bubble.Y + Pad), TextW, Size, FVPal::Ink(), Budget, false, bLimited ? 0.f : Step);
	}
	else
	{
		D.TextWrapped(Greeting.Left(Shown), FVector2D(Bubble.X + 6.f, Bubble.Y + 5.f), TextW, Size, FVPal::Ink(), false);
	}
	if (Shown >= Greeting.Len() && FMath::Frac(D.Time * 1.2) < 0.6)
	{
		Triangle(D, FVector2D(Bubble.Right() - 8.f, Bubble.Bottom() - 6.f), 1, 3, FVPal::InkSoft());
	}
	if (D.Clicked(Bubble))
	{
		if (Shown < Greeting.Len())
		{
			GreetStart = D.Time - 1000.0;   // reveal the whole line
		}
		else
		{
			PickGreeting();
		}
		PlaySfx(EFVSfx::Click, 0.5f);
	}
}

// -------------------------------------------------------------------------------- portrait (phone) layout
//
//   [keeper] Дед Митрич · хозяин лавки            (coin) 1 234
//            ( speech bubble ............................ )
//   [Удочки][Наживка][Снасти][Одежда][Улучшения][Продать]   (tabs of this trader)
//   | [Шляпы][Костюмы][Поплавки][Питомцы]   (cosmetics only) |
//   | scrollable list of items (swipe / wheel)                |
//   | details of the selected item (stats, description)       |
//   [← Назад] [Продать →] [  Купить ●800  ]                     <- thumb zone (primary action at the right)
//   «Продать»: grids of the keepnet & backpack, the selected stack, [Продать 1][Продать стопку] over [← Назад][Вся рыба].

void FShopScreen::DrawPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win)
{
	const float L = Win.X + 10.f;
	const float CW = Win.W - 20.f;
	const float Top = Win.Y + 14.f;              // below the title plate
	const FFVRect Bar = FVDraw::ActionBarRect(Win);

	const float HeadH = DrawHeaderPortrait(D, P, FFVRect(L, Top, CW, 54.f));   // taller when the role has its own line
	const FFVRect TabRow(L, Top + HeadH + 6.f, CW, 28.f);
	DrawTabs(D, TabRow);

	// the pages: everything between the tab row (+ its sliding indicator) and the footer divider
	const float PageTop = TabRow.Bottom() + 7.f;
	const FFVRect PageArea(Win.X + 5.f, PageTop, Win.W - 10.f, FMath::Max(40.f, Bar.Y - 8.f - PageTop));
	Pager.Update(D, PageArea, Tabs.Num());
	Pager.DrawPages(D, PageArea, [this, &D, &P, &Win, PageTop](int32 Page)
	{
		CurTab = TabAt(Page);
		DrawPagePortrait(D, P, Win, PageTop);
	});
	CurTab = ActiveTab();

	// footer: "← Назад" stays put; the page's own actions slide along with the page
	FFVRect Actions;
	if (D.ActionBar(Win, &Actions))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	const FFVRect Slide(Actions.X, Actions.Y - 8.f, Actions.W + 2.f, Actions.H + 10.f);   // + the count badge and shadows
	Pager.DrawPages(D, Slide, [this, &D, &P, &Actions](int32 Page)
	{
		CurTab = TabAt(Page);
		DrawFooterActionsPortrait(D, P, Actions);
	});
	CurTab = ActiveTab();
}

void FShopScreen::DrawPagePortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win, float Top)
{
	const bool bSell = CurTab == EShopTab::Sell;
	const float L = Win.X + 10.f;
	const float CW = Win.W - 20.f;
	const float Bottom = (bSell ? FVUI::ActionBarExtraRowRect(Win).Y : FVDraw::ActionBarRect(Win).Y) - 10.f;
	// taller phones get a roomier detail panel; short ones keep at least four list rows
	const float DetailH = bSell ? FMath::Clamp(FMath::RoundToFloat(100.f + (FV::VH - 640.f) * 0.1f), 100.f, 124.f)
		: FMath::Clamp(FMath::RoundToFloat(200.f + (FV::VH - 640.f) * 0.2f), 200.f, 240.f);
	const FFVRect DetailPanel(L, Bottom - DetailH, CW, DetailH);
	const FFVRect ListPanel(L, Top, CW, FMath::Max(60.f, DetailPanel.Y - 6.f - Top));
	D.Panel(ListPanel, EFVPanel::Parchment);
	D.Panel(DetailPanel, EFVPanel::Parchment);

	if (bSell)
	{
		DrawSellList(D, P, ListPanel, true);
		DrawSellDetail(D, P, DetailPanel, true);
		DrawSellRowPortrait(D, P, Win);
		return;
	}
	FShopItem Selected;
	const bool bHasSelection = DrawListPortrait(D, P, ListPanel, Selected);
	DrawDetail(D, P, bHasSelection ? &Selected : nullptr, DetailPanel);
}

float FShopScreen::DrawHeaderPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Area)
{
	if (DrawKeeper(D, FFVRect(Area.X, Area.Y, 52.f, 52.f)))
	{
		PlaySfx(EFVSfx::Click, 0.6f);
		PickGreeting();
	}

	// name on the left, the purse on the right, the speech bubble under them
	const FFVTraderDef& T = GetTrader();
	const float NX = Area.X + 60.f;
	const float MoneyW = D.MoneyLabel(FVector2D(Area.Right(), Area.Y - 2.f), P.Money, 11.f, EFVAlign::Right, FVPal::Ink());
	const float NameS = FV::Ui::Body();
	const float NameMaxW = Area.Right() - MoneyW - 8.f - NX;
	D.Text(FitText(D, T.Name, NameMaxW, NameS), FVector2D(NX, Area.Y - 1.f), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
	const float SubX = NX + FMath::Min(NameMaxW, D.MeasureText(T.Name, NameS).X) + 5.f;
	// the role («что покупает / продаёт») stays next to the name when it fits; otherwise it gets its own wrapped line(s)
	// under the name and the bubble (with the whole header) moves down by that much
	float RoleH = 0.f;
	if (!T.Role.IsEmpty())
	{
		if (SubX + D.MeasureText(T.Role, FV::Ui::Small(), false).X < Area.Right() - MoneyW - 8.f)
		{
			D.Text(T.Role, FVector2D(SubX, Area.Y + 1.f), FV::Ui::Small(), FVPal::InkSoft(), EFVAlign::Left, false, false);
		}
		else
		{
			RoleH = TextWrappedLimited(D, T.Role, FVector2D(NX, Area.Y + 13.f), Area.Right() - NX, FV::Ui::Small(), FVPal::InkSoft(), 3, false);
		}
	}
	DrawBubble(D, FFVRect(NX + 2.f, Area.Y + 14.f + RoleH, Area.Right() - NX - 2.f, 40.f), 8.5f, 3);
	return Area.H + RoleH;
}

bool FShopScreen::DrawListPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, FShopItem& OutSelected)
{
	float Top = Panel.Y + 6.f;
	if (CurTab == EShopTab::Cosmetics)
	{
		// second tab row: which kind of clothes
		static const TCHAR* const SlotNames[] = { TEXT("Шляпы"), TEXT("Костюмы"), TEXT("Поплавки"), TEXT("Питомцы") };
		const TArray<int32> Slots = CosmeticSlotsWithItems();
		const int32 NS = FMath::Max(1, Slots.Num());
		const float Gap = 4.f;
		const float BW = FMath::FloorToFloat((Panel.W - 12.f - float(NS - 1) * Gap) / float(NS));
		for (int32 k = 0; k < Slots.Num(); ++k)
		{
			const int32 s = Slots[k];
			const FFVRect BR(Panel.X + 6.f + float(k) * (BW + Gap), Top, BW, 30.f);
			const bool bActive = int32(CurSlot) == s;
			if (D.Button(BR, SlotNames[s], bActive ? EFVButton::Primary : EFVButton::Normal, true, 9.f) && !bActive)
			{
				CurSlot = EFVCosmeticSlot(s);
			}
		}
		Top += 36.f;
		D.Rect(FFVRect(Panel.X + 6.f, Top - 3.f, Panel.W - 12.f, 1.f), Divider());
	}

	TArray<FShopItem> Items;
	BuildItems(Items);
	const int32 Key = ListKey();
	int32& Sel = Selection[Key];
	FFVScrollView& Scroll = ListScroll[Key];
	const float Pitch = RowHPortrait();
	const FFVRect Area(Panel.X + 4.f, Top, Panel.W - 8.f, FMath::Max(10.f, Panel.Bottom() - 5.f - Top));

	if (Items.Num() == 0)
	{
		Sel = -1;
		D.Text(TEXT("Здесь пока пусто"), FVector2D(Area.Center().X, Area.Y + 30.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
		return false;
	}
	if (!Items.IsValidIndex(Sel))
	{
		Sel = AutoSelect(P, Items);
		EnsureVis[Key] = FMath::Max<uint8>(EnsureVis[Key], 1);
	}
	Scroll.Update(D, Area, float(Items.Num()) * Pitch + 2.f);
	if (EnsureVis[Key] != 0 && Sel >= 0)
	{
		Scroll.ScrollToShow(float(Sel) * Pitch, Pitch + 2.f, EnsureVis[Key] == 2);
		EnsureVis[Key] = 0;
	}

	const float RowX = Area.X + 2.f;
	const float RowW = Area.W - 4.f - (Scroll.CanScroll() ? 6.f : 0.f);
	const float Y0 = Area.Y + 1.f - Scroll.Offset();
	D.PushClip(Area);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FFVRect Row(RowX, Y0 + float(i) * Pitch, RowW, Pitch - 2.f);
		if (Row.Bottom() + 1.f < Area.Y || Row.Y > Area.Bottom())
		{
			continue;
		}
		const bool bSel = i == Sel;
		if (bSel)
		{
			D.Rect(Row, FV::Col(238, 190, 122));
			D.Frame(Row, FVPal::Wood());
		}
		else if (FVUI::IsPressingOn(D, Row) || (!FV::IsTouchUI() && D.Hover(Row)))
		{
			D.Rect(Row, FV::Col(253, 232, 188));
		}
		else
		{
			D.Rect(FFVRect(Row.X + 2.f, Row.Bottom(), Row.W - 4.f, 1.f), FV::Col(232, 190, 130));
		}
		if (D.Clicked(Row) && !bSel)
		{
			Sel = i;
			PlaySfx(EFVSfx::Click, 0.6f);
		}
		DrawRowPortrait(D, P, Items[i], Row);
	}
	D.PopClip();
	Scroll.DrawBar(D, Area);
	Scroll.DrawFades(D, Area, FVPal::Parchment());

	if (Items.IsValidIndex(Sel))
	{
		OutSelected = Items[Sel];
		return true;
	}
	return false;
}

void FShopScreen::DrawRowPortrait(FVDraw& D, const UFVSaveGame& P, const FShopItem& It, const FFVRect& Row)
{
	const float BoxS = FMath::Max(16.f, Row.H - 4.f);
	const FFVRect Box(Row.X + 2.f, Row.Y + 2.f, BoxS, BoxS);
	IconBox(D, Box);
	DrawItemIcon(D, It, Box.Center(), BoxS >= 34.f ? 2.f : 1.f);

	const int32 Level = P.GetLevel();
	const int32 MinLv = ItemMinLevel(It);
	const bool bLevelOk = Level >= MinLv;
	const EItemStatus St = GetStatus(P, It);
	const int64 Price = ItemPrice(It);
	const float RightX = Row.Right() - 6.f;
	const float NameX = Box.Right() + 8.f;
	const float TextW = RightX - NameX - 84.f;
	const float NameS = FV::Ui::Body();
	const float SubS = FV::Ui::Small();

	D.Text(FitText(D, ItemName(It), TextW, NameS), FVector2D(NameX, Row.Y + 4.f), NameS,
		(bLevelOk && St != EItemStatus::Locked) ? FVPal::Ink() : FVPal::InkSoft());

	FLinearColor SubC = FVPal::InkSoft();
	FString Sub;
	if (!bLevelOk && (St == EItemStatus::ForSale || St == EItemStatus::RewardOnly) && !It.bForgeInfo)
	{
		Sub = FString::Printf(TEXT("Нужен уровень %d"), MinLv);
		SubC = InkBad();
	}
	else
	{
		Sub = RowSubText(P, It, St);
		if (St == EItemStatus::Locked && (It.Kind == EItemKind::Cosmetic || It.Kind == EItemKind::Rod)) { SubC = InkBad(); }   // Р64 / Р81: «Нужно достижение «…»»
	}
	float SubX = NameX;
	if (It.Kind == EItemKind::Rod && It.Rod)
	{
		SubX += FVUI::RodTypeChip(D, *It.Rod, FVector2D(NameX, Row.Y + 20.f), 6.5f) + 4.f;   // 0.6.2 (Р84): rod type plate
	}
	D.Text(FitText(D, Sub, TextW - (SubX - NameX), SubS, false), FVector2D(SubX, Row.Y + 21.f), SubS, SubC, EFVAlign::Left, false, false);

	const float PillS = 7.f;
	const float PillH = 15.f;
	const float MidY = Row.Y + FMath::RoundToFloat((Row.H - PillH) * 0.5f);
	const float PriceS = FV::Ui::Body();
	switch (St)
	{
	case EItemStatus::ForSale:
		D.MoneyLabel(FVector2D(RightX, Row.Y + FMath::RoundToFloat((Row.H - 12.f) * 0.5f)), Price, PriceS, EFVAlign::Right, P.CanAfford(Price) ? FVPal::Ink() : InkBad());
		break;
	case EItemStatus::Owned:
		Pill(D, TEXT("Куплено"), RightX, MidY, FV::Col(70, 150, 52), PillS, PillH);
		break;
	case EItemStatus::Equipped:
		if (It.Kind == EItemKind::Bait)
		{
			D.MoneyLabel(FVector2D(RightX, Row.Y + 3.f), Price, 8.5f, EFVAlign::Right, P.CanAfford(Price) ? FVPal::Ink() : InkBad());
			Pill(D, TEXT("Насажена"), RightX, Row.Y + 19.f, FV::Col(56, 118, 196), PillS, PillH);
		}
		else
		{
			Pill(D, It.Kind == EItemKind::Rod ? TEXT("В руках") : TEXT("Надето"), RightX, MidY, FV::Col(56, 118, 196), PillS, PillH);
		}
		break;
	case EItemStatus::RewardOnly:
		Pill(D, It.Kind == EItemKind::Rod ? TEXT("Кузня") : TEXT("Награда"), RightX, MidY, FV::Col(146, 76, 196), PillS, PillH);
		break;
	case EItemStatus::Locked:
		D.MoneyLabel(FVector2D(RightX, Row.Y + 3.f), Price, 8.5f, EFVAlign::Right, FVPal::Disabled());
		Pill(D, (It.Kind == EItemKind::Cosmetic || It.Kind == EItemKind::Rod) ? TEXT("Закрыто") : TEXT("Позже"), RightX, Row.Y + 19.f, FV::Col(150, 128, 108), PillS, PillH);
		break;
	}
}

void FShopScreen::DrawTitleBadgePortrait(FVDraw& D, const UFVSaveGame& P, const FShopItem& It, const FFVRect& Panel)
{
	const float RX = Panel.Right() - 8.f;
	const float Y = Panel.Y + 7.f;
	const float PillS = 7.f;
	const float PillH = 15.f;
	const int64 Price = ItemPrice(It);
	if (It.Kind == EItemKind::Bait || It.Kind == EItemKind::Item)
	{
		// packs of bait and pieces of tackle can always be bought: the price of one
		D.MoneyLabel(FVector2D(RX, Y), Price, 11.f, EFVAlign::Right, P.CanAfford(Price) ? FVPal::Ink() : InkBad());
		return;
	}
	switch (GetStatus(P, It))
	{
	case EItemStatus::ForSale:
		D.MoneyLabel(FVector2D(RX, Y), Price, 11.f, EFVAlign::Right, P.CanAfford(Price) ? FVPal::Ink() : InkBad());
		break;
	case EItemStatus::Owned:
		Pill(D, TEXT("Куплено"), RX, Y, FV::Col(70, 150, 52), PillS, PillH);
		break;
	case EItemStatus::Equipped:
		Pill(D, It.Kind == EItemKind::Rod ? TEXT("В руках") : TEXT("Надето"), RX, Y, FV::Col(56, 118, 196), PillS, PillH);
		break;
	case EItemStatus::RewardOnly:
		Pill(D, It.Kind == EItemKind::Rod ? TEXT("Кузня") : TEXT("Награда"), RX, Y, FV::Col(146, 76, 196), PillS, PillH);
		break;
	case EItemStatus::Locked:
		D.MoneyLabel(FVector2D(RX, Y), Price, 11.f, EFVAlign::Right, FVPal::Disabled());
		break;
	}
}

void FShopScreen::DrawFooterActionsPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Actions)
{
	if (CurTab == EShopTab::Sell)
	{
		const int32 FishUnits = SellableFishUnits(P);
		const int64 FishValue = P.ContainerValue(EFVContainer::Keepnet, TraderId);
		if (FVUI::PriceButton(D, Actions, FishUnits > 0 ? FString::Printf(TEXT("Вся рыба ×%d"), FishUnits) : FString(TEXT("Вся рыба")),
			FishUnits > 0 ? FishValue : -1, EFVButton::Primary, FishUnits > 0) && FishUnits > 0)
		{
			ConfirmSellAllFish(P);   // asks first
		}
		return;
	}
	FFVRect Nav;
	FFVRect Main;
	SplitActions(Actions, Nav, Main);
	const int32 Used = P.KeepnetUsed();
	if (D.Button(Nav, TEXT("Продать →"), Used > 0 ? EFVButton::Primary : EFVButton::Normal, true, 9.5f))
	{
		GoToTab(EShopTab::Sell);   // the «Продать» page of this stall (slides there)
	}
	if (Used > 0)
	{
		CountBadge(D, FVector2D(Nav.Right() - 5.f, Nav.Y + 1.f), Used);
	}
	FShopItem It;
	const bool bHas = SelectedItem(P, It);
	DrawMainActionPortrait(D, P, Main, bHas ? &It : nullptr);
}

void FShopScreen::DrawMainActionPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& R, const FShopItem* It)
{
	const float S = BarTextSize();
	if (!It)
	{
		D.Button(R, TEXT("Выберите товар"), EFVButton::Normal, false, S);
		return;
	}
	const int32 Level = P.GetLevel();
	switch (It->Kind)
	{
	case EItemKind::Rod:
	{
		if (!It->Rod) { break; }
		const FFVRodDef& Rod = *It->Rod;
		if (!P.OwnsRod(Rod.Id) && It->bForgeInfo)
		{
			D.Button(R, TEXT("Куётся в кузне"), EFVButton::Normal, false, S);   // 0.6.2 (Р80): not sold
		}
		else if (!P.OwnsRod(Rod.Id) && MissingRodAch(P, Rod))
		{
			// 0.6.2 (Р81): greyed, but a tap lets the shopkeeper say which achievement is missing
			if (D.Button(R, TEXT("Нужно достижение"), EFVButton::Normal, true, S))
			{
				TryBuyRod(P, Rod);
			}
		}
		else if (!P.OwnsRod(Rod.Id))
		{
			const bool bAfford = P.CanAfford(Rod.Price);
			// still clickable when it can't be bought: the shopkeeper explains why
			if (PriceButton(D, R, TEXT("Купить"), Rod.Price, (bAfford && Level >= Rod.MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAfford))
			{
				TryBuyRod(P, Rod);
			}
		}
		else if (P.GetRod().Id != Rod.Id)
		{
			if (D.Button(R, TEXT("Взять в руки"), EFVButton::Primary, true, S))
			{
				DoEquipRod(P, Rod);
			}
		}
		else
		{
			D.Button(R, TEXT("В руках"), EFVButton::Normal, false, S);
		}
		break;
	}
	case EItemKind::Bait:
	{
		if (!It->Bait) { break; }
		const FFVBaitDef& Bait = *It->Bait;
		const int32 Packs = FMath::Clamp(BuyQty, 1, 10);
		const int64 Price = int64(Bait.PackPrice) * Packs;
		const bool bAfford = P.CanAfford(Price);
		const FString Label = Packs > 1 ? FString::Printf(TEXT("Купить ×%d"), Packs) : FString(TEXT("Купить"));
		if (PriceButton(D, R, Label, Price, (bAfford && Level >= Bait.MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAfford))
		{
			TryBuyBait(P, Bait, Packs);
		}
		break;
	}
	case EItemKind::Item:
	{
		if (!It->Item) { break; }
		const FFVItemDef& Item = *It->Item;
		if (Item.Kind == EFVItemKind::Blueprint)
		{
			if (P.HasBlueprint(Item.Id))
			{
				D.Button(R, TEXT("Чертёж изучен"), EFVButton::Normal, false, S);
			}
			else
			{
				const bool bAff = P.CanAfford(Item.BuyPrice);
				if (PriceButton(D, R, TEXT("Купить чертёж"), Item.BuyPrice, (bAff && Level >= Item.MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAff))
				{
					TryBuyItem(P, Item, 1);
				}
			}
			break;
		}
		const int32 Qty = FMath::Clamp(BuyQty, 1, 10);
		const int64 Price = int64(Item.BuyPrice) * Qty;
		const bool bAfford = P.CanAfford(Price);
		const FString Label = Qty > 1 ? FString::Printf(TEXT("Купить ×%d"), Qty) : FString(TEXT("Купить"));
		if (PriceButton(D, R, Label, Price, (bAfford && Level >= Item.MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAfford))
		{
			TryBuyItem(P, Item, Qty);
		}
		break;
	}
	case EItemKind::Shovel:
	{
		const FFVShovelDef* Sh = ShovelAt(It->ShovelLevel);
		if (!Sh) { break; }
		const EItemStatus St = GetStatus(P, *It);
		if (St == EItemStatus::Owned)
		{
			D.Button(R, TEXT("Уже есть"), EFVButton::Normal, false, S);
		}
		else if (St == EItemStatus::ForSale)
		{
			const bool bAfford = P.CanAfford(Sh->Price);
			if (PriceButton(D, R, TEXT("Купить"), Sh->Price, (bAfford && Level >= Sh->MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAfford))
			{
				TryBuyShovel(P, It->ShovelLevel);
			}
		}
		else
		{
			D.Button(R, TEXT("Сначала лопата попроще"), EFVButton::Normal, false, S);
		}
		break;
	}
	case EItemKind::Axe:
	{
		const FFVToolDef* Ax = AxeAt(It->AxeLevel);
		if (!Ax) { break; }
		const EItemStatus St = GetStatus(P, *It);
		if (St == EItemStatus::Owned)
		{
			D.Button(R, TEXT("Уже есть"), EFVButton::Normal, false, S);
		}
		else if (St == EItemStatus::ForSale)
		{
			const bool bAfford = P.CanAfford(Ax->Price);
			if (PriceButton(D, R, TEXT("Купить"), Ax->Price, (bAfford && Level >= Ax->MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAfford))
			{
				TryBuyAxe(P, It->AxeLevel);
			}
		}
		else
		{
			D.Button(R, TEXT("Сначала топор попроще"), EFVButton::Normal, false, S);
		}
		break;
	}
	case EItemKind::Decor:
	{
		if (!It->Decor) { break; }
		const FFVDecorDef& Dec = *It->Decor;
		if (P.OwnsDecor(Dec.Id))
		{
			D.Button(R, TEXT("Уже куплено"), EFVButton::Normal, false, S);
		}
		else
		{
			const bool bAfford = P.CanAfford(Dec.Price);
			if (PriceButton(D, R, TEXT("Купить"), Dec.Price, (bAfford && Level >= Dec.MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAfford))
			{
				TryBuyDecor(P, Dec);
			}
		}
		break;
	}
	case EItemKind::Cosmetic:
	{
		if (!It->Cos) { break; }
		const FFVCosmeticDef& Cos = *It->Cos;
		const bool bOwned = P.OwnsCosmetic(Cos.Id);
		const bool bEquipped = bOwned && P.GetEquipped(Cos.Slot) == Cos.Id;
		if (bEquipped)
		{
			if (Cos.Slot == EFVCosmeticSlot::Hat || Cos.Slot == EFVCosmeticSlot::Pet)
			{
				if (D.Button(R, TEXT("Снять"), EFVButton::Normal, true, S))
				{
					DoUnequipCosmetic(P, Cos);
				}
			}
			else
			{
				D.Button(R, TEXT("Надето"), EFVButton::Normal, false, S);
			}
		}
		else if (bOwned)
		{
			if (D.Button(R, TEXT("Надеть"), EFVButton::Primary, true, S))
			{
				DoEquipCosmetic(P, Cos);
			}
		}
		else if (Cos.Price > 0 && MissingAch(P, Cos))
		{
			D.Button(R, TEXT("Нужно достижение"), EFVButton::Normal, false, S);   // Р64
		}
		else if (Cos.Price > 0)
		{
			const bool bAfford = P.CanAfford(Cos.Price);
			if (PriceButton(D, R, TEXT("Купить"), Cos.Price, (bAfford && Level >= Cos.MinLevel) ? EFVButton::Primary : EFVButton::Normal, bAfford))
			{
				TryBuyCosmetic(P, Cos);
			}
		}
		else
		{
			D.Button(R, TEXT("Только в награду"), EFVButton::Normal, false, S);
		}
		break;
	}
	case EItemKind::Keepnet:
	case EItemKind::Backpack:
	{
		const bool bBackpack = It->Kind == EItemKind::Backpack;
		const int32 Target = bBackpack ? It->BackpackLevel : It->KeepnetLevel;
		const int32 Cur = bBackpack ? P.BackpackLevel : P.KeepnetLevel;
		const int32 Num = bBackpack ? FVDB::BackpackLevels().Num() : FVDB::KeepnetLevels().Num();
		if (Target <= 0 || Target >= Num) { break; }
		const int64 Price = ItemPrice(*It);
		if (Target <= Cur)
		{
			D.Button(R, TEXT("Куплено"), EFVButton::Normal, false, S);
		}
		else if (Target == Cur + 1)
		{
			const bool bAfford = P.CanAfford(Price);
			if (PriceButton(D, R, TEXT("Улучшить"), Price, bAfford ? EFVButton::Primary : EFVButton::Normal, bAfford))
			{
				if (bBackpack) { TryUpgradeBackpack(P); }
				else { TryUpgradeKeepnet(P); }
			}
		}
		else
		{
			D.Button(R, TEXT("Сначала предыдущий"), EFVButton::Normal, false, S);
		}
		break;
	}
	}
}

float FShopScreen::DrawQtyChips(FVDraw& D, float X, float Y, float H)
{
	const float S = FV::Ui::Small();
	const FString Label = CurTab == EShopTab::Bait ? FString(TEXT("Пачек:")) : FString(TEXT("Штук:"));
	const float LabelW = D.MeasureText(Label, S, false).X;
	D.Text(Label, FVector2D(X, Y + FMath::RoundToFloat((H - D.LineHeight(S, false)) * 0.5f)), S, FVPal::InkSoft(), EFVAlign::Left, false, false);
	static const int32 Options[] = { 1, 5, 10 };
	float CX = X + LabelW + 6.f;
	for (const int32 N : Options)
	{
		const FFVRect Chip(CX, Y, 40.f, H);
		const bool bOn = BuyQty == N;
		if (D.Button(Chip, FString::Printf(TEXT("×%d"), N), bOn ? EFVButton::Primary : EFVButton::Normal, true, 9.f) && !bOn)
		{
			BuyQty = N;
		}
		CX += 44.f;
	}
	return CX;
}

void FShopScreen::DrawTabs(FVDraw& D, const FFVRect& Row)
{
	TArray<FString> Labels;
	Labels.Reserve(Tabs.Num());
	for (const EShopTab T : Tabs)
	{
		Labels.Add(TabName(T));
	}
	FVKit::TabStrip(D, Row, Labels, Pager);
}

bool FShopScreen::DrawList(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, FShopItem& OutSelected)
{
	float Top = Panel.Y + 7.f;
	if (CurTab == EShopTab::Cosmetics)
	{
		static const TCHAR* const SlotNames[] = { TEXT("Шляпы"), TEXT("Костюмы"), TEXT("Поплавки"), TEXT("Питомцы") };
		const TArray<int32> Slots = CosmeticSlotsWithItems();
		const int32 NS = FMath::Max(1, Slots.Num());
		const float BW = (Panel.W - 12.f - 3.f * float(NS - 1)) / float(NS);
		for (int32 k = 0; k < Slots.Num(); ++k)
		{
			const int32 s = Slots[k];
			const FFVRect BR(Panel.X + 6.f + float(k) * (BW + 3.f), Top, BW, 15.f);
			const bool bActive = int32(CurSlot) == s;
			if (D.Button(BR, SlotNames[s], bActive ? EFVButton::Primary : EFVButton::Ghost, true, 7.f) && !bActive)
			{
				CurSlot = EFVCosmeticSlot(s);
			}
		}
		Top += 21.f;
	}

	TArray<FShopItem> Items;
	BuildItems(Items);
	const int32 Key = ListKey();
	int32& Sel = Selection[Key];
	FFVScrollView& Scroll = ListScroll[Key];
	const FFVRect Area(Panel.X + 4.f, Top, Panel.W - 8.f, FMath::Max(10.f, Panel.Bottom() - 5.f - Top));

	if (Items.Num() == 0)
	{
		Sel = -1;
		D.Text(TEXT("Здесь пока пусто"), FVector2D(Area.Center().X, Area.Y + 30.f), 8.f, FVPal::InkSoft(), EFVAlign::Center);
		return false;
	}
	if (!Items.IsValidIndex(Sel))
	{
		Sel = AutoSelect(P, Items);
		EnsureVis[Key] = FMath::Max<uint8>(EnsureVis[Key], 1);
	}
	Scroll.Update(D, Area, float(Items.Num()) * RowH);
	if (EnsureVis[Key] != 0 && Sel >= 0)
	{
		Scroll.ScrollToShow(float(Sel) * RowH, RowH, EnsureVis[Key] == 2);
		EnsureVis[Key] = 0;
	}

	const float RowX = Area.X + 2.f;
	const float RowW = Area.W - 4.f - (Scroll.CanScroll() ? 5.f : 0.f);
	const float Y0 = Area.Y - Scroll.Offset();
	D.PushClip(Area);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FFVRect Row(RowX, Y0 + float(i) * RowH, RowW, RowH - 2.f);
		if (Row.Bottom() + 1.f < Area.Y || Row.Y > Area.Bottom())
		{
			continue;
		}
		const bool bSel = i == Sel;
		const bool bHover = D.Hover(Row);
		if (bSel)
		{
			D.Rect(Row, FV::Col(238, 190, 122));
			D.Frame(Row, FVPal::Wood());
		}
		else if (bHover)
		{
			D.Rect(Row, FV::Col(253, 232, 188));
		}
		else
		{
			D.Rect(FFVRect(Row.X + 2.f, Row.Bottom(), Row.W - 4.f, 1.f), FV::Col(232, 190, 130));
		}
		if (D.Clicked(Row) && !bSel)
		{
			Sel = i;
			PlaySfx(EFVSfx::Click, 0.6f);
		}
		DrawRow(D, P, Items[i], Row);
	}
	D.PopClip();
	Scroll.DrawBar(D, Area);
	Scroll.DrawFades(D, Area, FVPal::Parchment());

	if (Items.IsValidIndex(Sel))
	{
		OutSelected = Items[Sel];
		return true;
	}
	return false;
}

FString FShopScreen::RowSubText(const UFVSaveGame& P, const FShopItem& It, EItemStatus St) const
{
	switch (It.Kind)
	{
	case EItemKind::Rod:
		if (It.Rod)
		{
			// 0.6.2 (Р80/Р81): a forged rod — where it comes from; a locked one — the achievement and its progress
			if (St == EItemStatus::RewardOnly) { return FVDBU6::RodSourceText(*It.Rod); }
			if (St == EItemStatus::Locked)
			{
				if (const FFVAchievementDef* A = MissingRodAch(P, *It.Rod)) { return RodLockText(P, *A, false); }
			}
			if (FVDBU6::FindExpertRod(It.Rod->Id))
			{
				// expert rods have no classic green band (their type is on the plate in front of this line)
				return FString::Printf(TEXT("Клёв +%d%% · удача +%d%%"), FMath::RoundToInt(StatBite(*It.Rod)), FMath::RoundToInt(StatLuck(*It.Rod)));
			}
			return FString::Printf(TEXT("Полоса +%d · клёв +%d%% · удача +%d%%"),
				FMath::RoundToInt(StatBar(*It.Rod)), FMath::RoundToInt(StatBite(*It.Rod)), FMath::RoundToInt(StatLuck(*It.Rod)));
		}
		break;
	case EItemKind::Bait:
		if (It.Bait)
		{
			return FString::Printf(TEXT("%d шт. в пачке · у вас %d"), It.Bait->PackSize, P.BaitCount(It.Bait->Id));
		}
		break;
	case EItemKind::Cosmetic:
		if (It.Cos)
		{
			if (St == EItemStatus::Locked)
			{
				if (const FFVAchievementDef* A = MissingAch(P, *It.Cos)) { return NeedAchText(*A, false); }
			}
			return (St == EItemStatus::Owned || St == EItemStatus::Equipped) ? FString(TEXT("В гардеробе"))
				: (St == EItemStatus::RewardOnly ? FString(TEXT("Не продаётся")) : FVDB::SlotName(It.Cos->Slot));
		}
		break;
	case EItemKind::Keepnet:
		return FString::Printf(TEXT("Садок · улучшение %d из %d"), It.KeepnetLevel, FMath::Max(1, FVDB::KeepnetLevels().Num() - 1));
	case EItemKind::Backpack:
		return FString::Printf(TEXT("Рюкзак · улучшение %d из %d"), It.BackpackLevel, FMath::Max(1, FVDB::BackpackLevels().Num() - 1));
	case EItemKind::Item:
		if (It.Item)
		{
			const FFVItemDef& D = *It.Item;
			const int32 Have = P.CountItem(D.Id);
			switch (D.Kind)
			{
			case EFVItemKind::Blueprint:
				return P.HasBlueprint(D.Id) ? FString(TEXT("Чертёж · изучен")) : FString(TEXT("Чертёж · изучается сразу"));
			case EFVItemKind::Seed:
				return FString::Printf(TEXT("Семена · у вас %d"), Have);
			case EFVItemKind::Reel:
				return FString::Printf(TEXT("Катушка эксперта · у вас %d"), Have);
			case EFVItemKind::Line:
			case EFVItemKind::Float:
				if (D.bExpert) { return FString::Printf(TEXT("%s эксперта · у вас %d"), *FVDB::ItemKindName(D.Kind), Have); }
				return FString::Printf(TEXT("%s · ранг %d · у вас %d"), *FVDB::ItemKindName(D.Kind), int32(D.Tier), Have);
			case EFVItemKind::Net:
				return FString::Printf(TEXT("Сеть · до %d рыб · у вас %d"), D.NetCapacity, Have);
			default:
				return FString::Printf(TEXT("%s · у вас %d"), *FVDB::ItemKindName(D.Kind), Have);
			}
		}
		break;
	case EItemKind::Axe:
		if (const FFVToolDef* Ax = AxeAt(It.AxeLevel))
		{
			return St == EItemStatus::Owned ? FString(TEXT("Топор · уже есть")) : FString::Printf(TEXT("Топор · рубит до %s"),
				Ax->MaxTree == EFVTreeKind::Birch ? TEXT("берёзы") : (Ax->MaxTree == EFVTreeKind::Pine ? TEXT("сосны")
				: (Ax->MaxTree == EFVTreeKind::Oak ? TEXT("дуба") : TEXT("вековой ели"))));
		}
		break;
	case EItemKind::Decor:
		if (It.Decor)
		{
			return (St == EItemStatus::Owned || St == EItemStatus::Equipped) ? FString(TEXT("Уже куплено"))
				: FString::Printf(TEXT("Декор · %s"), *FVDB::DecorSlotName(It.Decor->Slot));
		}
		break;
	case EItemKind::Shovel:
		if (const FFVShovelDef* Sh = ShovelAt(It.ShovelLevel))
		{
			if (St == EItemStatus::Locked)
			{
				const FFVShovelDef* Prev = ShovelAt(It.ShovelLevel - 1);
				return Prev ? FString::Printf(TEXT("Сначала: %s"), *Prev->Name) : FString(TEXT("Лопата"));
			}
			return FString::Printf(TEXT("Лопата · раскопки %s с"), *FVUI::Num(Sh->DigSeconds, 1));
		}
		break;
	}
	return FString();
}

void FShopScreen::DrawRow(FVDraw& D, const UFVSaveGame& P, const FShopItem& It, const FFVRect& Row)
{
	const FFVRect Box(Row.X + 2.f, Row.Y + 2.f, 22.f, 22.f);
	IconBox(D, Box);
	DrawItemIcon(D, It, Box.Center(), 1.f);

	const int32 Level = P.GetLevel();
	const int32 MinLv = ItemMinLevel(It);
	const bool bLevelOk = Level >= MinLv;
	const EItemStatus St = GetStatus(P, It);
	const int64 Price = ItemPrice(It);
	const float RightX = Row.Right() - 5.f;
	const float NameX = Row.X + 30.f;
	const float TextW = Row.W - 30.f - 76.f;

	// ---- name ----
	const FString Name = ItemName(It);
	D.Text(FitText(D, Name, TextW, 8.f), FVector2D(NameX, Row.Y + 3.f), 8.f,
		(bLevelOk && St != EItemStatus::Locked) ? FVPal::Ink() : FVPal::InkSoft());

	// ---- second line ----
	FLinearColor SubC = FVPal::InkSoft();
	FString Sub;
	if (!bLevelOk && (St == EItemStatus::ForSale || St == EItemStatus::RewardOnly) && !It.bForgeInfo)
	{
		Sub = FString::Printf(TEXT("Нужен уровень %d"), MinLv);
		SubC = InkBad();
	}
	else
	{
		Sub = RowSubText(P, It, St);
		if (St == EItemStatus::Locked && (It.Kind == EItemKind::Cosmetic || It.Kind == EItemKind::Rod)) { SubC = InkBad(); }   // Р64 / Р81: «Нужно достижение «…»»
	}
	float SubX = NameX;
	if (It.Kind == EItemKind::Rod && It.Rod)
	{
		SubX += FVUI::RodTypeChip(D, *It.Rod, FVector2D(NameX, Row.Y + 13.f), 5.5f) + 3.f;   // 0.6.2 (Р84): rod type plate
	}
	D.Text(FitText(D, Sub, TextW - (SubX - NameX), 6.5f, false), FVector2D(SubX, Row.Y + 14.f), 6.5f, SubC, EFVAlign::Left, false, false);

	// ---- right column: price and/or status ----
	switch (St)
	{
	case EItemStatus::ForSale:
		D.MoneyLabel(FVector2D(RightX, Row.Y + 4.f), Price, 8.f, EFVAlign::Right, P.CanAfford(Price) ? FVPal::Ink() : InkBad());
		break;
	case EItemStatus::Owned:
		Pill(D, TEXT("Куплено"), RightX, Row.Y + 8.f, FV::Col(70, 150, 52));
		break;
	case EItemStatus::Equipped:
		if (It.Kind == EItemKind::Bait)
		{
			D.MoneyLabel(FVector2D(RightX, Row.Y + 2.f), Price, 8.f, EFVAlign::Right, P.CanAfford(Price) ? FVPal::Ink() : InkBad());
			Pill(D, TEXT("Насажена"), RightX, Row.Y + 14.f, FV::Col(56, 118, 196));
		}
		else
		{
			Pill(D, It.Kind == EItemKind::Rod ? TEXT("В руках") : TEXT("Надето"), RightX, Row.Y + 8.f, FV::Col(56, 118, 196));
		}
		break;
	case EItemStatus::RewardOnly:
		Pill(D, It.Kind == EItemKind::Rod ? TEXT("Кузня") : TEXT("Награда"), RightX, Row.Y + 8.f, FV::Col(146, 76, 196));
		break;
	case EItemStatus::Locked:
		D.MoneyLabel(FVector2D(RightX, Row.Y + 2.f), Price, 8.f, EFVAlign::Right, FVPal::Disabled());
		Pill(D, (It.Kind == EItemKind::Cosmetic || It.Kind == EItemKind::Rod) ? TEXT("Закрыто") : TEXT("Позже"), RightX, Row.Y + 14.f, FV::Col(150, 128, 108));
		break;
	}
}

// -------------------------------------------------------------------------------- detail panel

float FShopScreen::DrawDetailTitle(FVDraw& D, const FFVRect& Panel, const FString& Name, const FString& Category, int32 MinLevel, int32 PlayerLevel)
{
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	if (FV::IsPortrait())
	{
		// the right end of the title line holds the price / state badge (DrawTitleBadgePortrait)
		const float TitleS = FV::Ui::Header();
		const float SubS = FV::Ui::Small();
		D.Text(FitText(D, Name, W - 96.f, TitleS), FVector2D(X0, Panel.Y + 5.f), TitleS, FVPal::Ink(), EFVAlign::Left, false, true);
		D.Text(Category, FVector2D(X0, Panel.Y + 23.f), SubS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		if (MinLevel > 1)
		{
			const float CatW = D.MeasureText(Category, SubS, false).X;
			const bool bOk = PlayerLevel >= MinLevel;
			D.Text(FString::Printf(TEXT("  ·  нужен уровень %d"), MinLevel), FVector2D(X0 + CatW, Panel.Y + 23.f), SubS,
				bOk ? FVPal::InkSoft() : InkBad(), EFVAlign::Left, false, !bOk);
		}
		D.Rect(FFVRect(X0, Panel.Y + 36.f, W, 1.f), Divider());
		return Panel.Y + 41.f;
	}
	D.Text(FitText(D, Name, W, 11.f), FVector2D(X0, Panel.Y + 5.f), 11.f, FVPal::Ink(), EFVAlign::Left, false, true);
	D.Text(Category, FVector2D(X0, Panel.Y + 20.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
	if (MinLevel > 1)
	{
		const float CatW = D.MeasureText(Category, 7.f, false).X;
		const bool bOk = PlayerLevel >= MinLevel;
		D.Text(FString::Printf(TEXT("  ·  нужен уровень %d"), MinLevel), FVector2D(X0 + CatW, Panel.Y + 20.f), 7.f,
			bOk ? FVPal::InkSoft() : InkBad(), EFVAlign::Left, false, !bOk);
	}
	D.Rect(FFVRect(X0, Panel.Y + 31.f, W, 1.f), Divider());
	return Panel.Y + 36.f;
}

void FShopScreen::DrawPriceLine(FVDraw& D, const FFVRect& Panel, int64 Price, bool bAffordable)
{
	const float Y = Panel.Bottom() - 48.f;
	D.Rect(FFVRect(Panel.X + 8.f, Y - 3.f, Panel.W - 16.f, 1.f), Divider());
	D.Text(TEXT("Цена"), FVector2D(Panel.X + 8.f, Y + 4.f), 8.f, FVPal::InkSoft());
	D.MoneyLabel(FVector2D(Panel.Right() - 8.f, Y + 1.f), Price, 11.f, EFVAlign::Right, bAffordable ? FVPal::Ink() : InkBad());
}

void FShopScreen::DrawStatusLine(FVDraw& D, const FFVRect& Panel, const FString& Text, const FLinearColor& Color)
{
	const float Y = Panel.Bottom() - 48.f;
	D.Rect(FFVRect(Panel.X + 8.f, Y - 3.f, Panel.W - 16.f, 1.f), Divider());
	D.Text(FitText(D, Text, Panel.W - 16.f, 7.5f), FVector2D(Panel.Center().X, Y + 4.f), 7.5f, Color, EFVAlign::Center);
}

void FShopScreen::DrawDetail(FVDraw& D, UFVSaveGame& P, const FShopItem* It, const FFVRect& Panel)
{
	const bool bP = FV::IsPortrait();
	if (!It)
	{
		D.TextWrapped(bP ? TEXT("Выберите товар в списке, чтобы рассмотреть его поближе.")
			: TEXT("Выберите товар в списке слева, чтобы рассмотреть его поближе."),
			FVector2D(Panel.X + 12.f, Panel.Y + 40.f), Panel.W - 24.f, bP ? FV::Ui::Body() : 8.f, FVPal::InkSoft(), false);
		return;
	}
	switch (It->Kind)
	{
	case EItemKind::Rod:      if (It->Rod) { DrawRodDetail(D, P, *It->Rod, Panel); } break;
	case EItemKind::Bait:     if (It->Bait) { DrawBaitDetail(D, P, *It->Bait, Panel); } break;
	case EItemKind::Cosmetic: if (It->Cos) { DrawCosmeticDetail(D, P, *It->Cos, Panel); } break;
	case EItemKind::Keepnet:  DrawUpgradeDetail(D, P, false, It->KeepnetLevel, Panel); break;
	case EItemKind::Backpack: DrawUpgradeDetail(D, P, true, It->BackpackLevel, Panel); break;
	case EItemKind::Item:     if (It->Item) { DrawItemDetail(D, P, *It->Item, Panel); } break;
	case EItemKind::Shovel:   DrawShovelDetail(D, P, It->ShovelLevel, Panel); break;
	case EItemKind::Axe:      DrawAxeDetail(D, P, It->AxeLevel, Panel); break;
	case EItemKind::Decor:    if (It->Decor) { DrawDecorDetail(D, P, *It->Decor, Panel); } break;
	}
	if (bP)
	{
		DrawTitleBadgePortrait(D, P, *It, Panel);
	}
}

void FShopScreen::DrawRodDetail(FVDraw& D, UFVSaveGame& P, const FFVRodDef& Rod, const FFVRect& Panel)
{
	const int32 Level = P.GetLevel();
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	float Y = DrawDetailTitle(D, Panel, Rod.Name, TEXT("Удочка"), Rod.MinLevel, Level);
	const bool bP = FV::IsPortrait();

	const float BoxS = bP ? 40.f : 38.f;
	const FFVRect Box(X0, Y, BoxS, BoxS);
	IconBox(D, Box);
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.SpriteCentered(Sprites->RodIcon(Rod), Box.Center(), 2.f);
	}
	const float DescH = bP
		? TextWrappedLimited(D, Rod.Description, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, FV::Ui::Body(), FVPal::Ink(), 3, false)
		: D.TextWrapped(Rod.Description, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, 7.f, FVPal::Ink(), false);
	Y += FMath::Max(BoxS + 4.f, DescH + 5.f);

	// ---- stats vs. the rod in hand ----
	const FFVRodDef& Cur = P.GetRod();
	const bool bSame = Cur.Id == Rod.Id;
	// Expert rods fight in the expert duel: the classic band / reel / hold stats do not apply to them, and a classic rod
	// is no yardstick for an expert one (and vice versa) — compare only within the same family.
	const FFVExpertRodDef* ExpertDef = FVDBU6::FindExpertRod(Rod.Id);
	const bool bCurExpert = FVDBU6::FindExpertRod(Cur.Id) != nullptr;
	const bool bNoCompare = bSame || (ExpertDef != nullptr) != bCurExpert;
	// 0.6.2 (Р84): the type plate (every rod); Р80/Р81: a forged rod — where it comes from, a locked one — what opens it
	const bool bOwned = P.OwnsRod(Rod.Id);
	const bool bForged = !bOwned && !FVDBU6::IsRodSold(Rod);
	const FFVAchievementDef* NeedA = (bOwned || bForged) ? nullptr : MissingRodAch(P, Rod);
	{
		const float ChipW = FVUI::RodTypeChip(D, Rod, FVector2D(X0, Y), bP ? 7.5f : 6.5f);
		const FString Note = (bForged && !bP) ? FVDBU6::RodSourceText(Rod) : (NeedA ? RodLockText(P, *NeedA, false) : FString());
		if (!Note.IsEmpty())
		{
			const float NoteS = bP ? FV::Ui::Small() : 6.5f;
			D.Text(FitText(D, Note, W - ChipW - 6.f, NoteS, false), FVector2D(X0 + ChipW + 6.f, Y + 1.5f), NoteS,
				NeedA ? InkBad() : FVPal::InkSoft(), EFVAlign::Left, false, false);
		}
		Y += bP ? 15.f : 13.f;
	}
	if (bForged && bP)
	{
		// portrait has no status line at the bottom: the whole source line, wrapped
		Y += TextWrappedLimited(D, FVDBU6::RodSourceText(Rod), FVector2D(X0, Y), W, FV::Ui::Small(), FVPal::InkSoft(), 2, false) + 4.f;
	}
	if (ExpertDef)
	{
		Y += D.TextWrapped(FVDBU6::RodTypeHowTo(ExpertDef->Type), FVector2D(X0, Y), W, bP ? FV::Ui::Small() : 6.5f, FVPal::InkSoft(), false) + (bP ? 6.f : 4.f);
	}
	D.Text(TEXT("Характеристики"), FVector2D(X0, Y), bP ? 8.5f : 7.5f, FVPal::Ink(), EFVAlign::Left, false, true);
	const FString VsText = bSame ? FString(TEXT("сейчас в руках"))
		: bNoCompare ? FString(ExpertDef ? TEXT("экспертная удочка") : TEXT("обычная удочка"))
		: FString::Printf(TEXT("против «%s»"), *Cur.Name);
	const float VsSize = bP ? FV::Ui::Small() : 6.f;
	D.Text(FitText(D, VsText, bP ? W - 96.f : W - 80.f, VsSize, false), FVector2D(X0 + W, Y + 1.5f), VsSize, FVPal::InkSoft(), EFVAlign::Right, false, false);
	Y += bP ? 14.f : 12.f;

	float StatStep = bP ? 13.f : 11.f;
	{
		// a long expert «how to» text above: the stat rows close up a little instead of running into the price line
		int32 NumRows = 0;
		for (const FRodStat& St : GRodStats)
		{
			if (!(ExpertDef && (St.Value == &StatBar || St.Value == &StatReel || St.Value == &StatHold))) { ++NumRows; }
		}
		const float Limit = bP ? Panel.Bottom() - 4.f : Panel.Bottom() - 52.f;
		if (NumRows > 0 && Y + float(NumRows) * StatStep > Limit)
		{
			StatStep = FMath::Max(9.f, FMath::FloorToFloat((Limit - Y) / float(NumRows)));
		}
	}
	const float StatSize = bP ? FV::Ui::Small() : 6.5f;
	const float BarX = X0 + (bP ? 102.f : 96.f);
	const float BarW = bP ? W - 102.f - 84.f : 82.f;
	const float ValueX = bP ? X0 + W - 44.f : X0 + 212.f;
	for (const FRodStat& St : GRodStats)
	{
		if (ExpertDef && (St.Value == &StatBar || St.Value == &StatReel || St.Value == &StatHold)) { continue; }
		const FFVRect RowR(X0, Y, W, StatStep);
		if (!bP && D.Hover(RowR))
		{
			D.Rect(RowR, FV::Col(255, 238, 200));
			D.Tooltip(St.Label, St.Tip);
		}
		const float V = St.Value(Rod);
		const float VCur = St.Value(Cur);
		const float Max = RodStatMax(St);
		const float A = FMath::Clamp(V / Max, 0.f, 1.f);
		const float B = FMath::Clamp(VCur / Max, 0.f, 1.f);

		D.Text(St.Label, FVector2D(X0 + 1.f, Y + 1.5f), StatSize, FVPal::Ink(), EFVAlign::Left, false, false);

		const FFVRect Bar(BarX, Y + 3.f, BarW, bP ? 7.f : 6.f);
		D.Rect(Bar, FV::Col(122, 86, 60));
		const FFVRect In = Bar.Inset(1.f);
		const FLinearColor Base = FV::Col(96, 176, 64);
		if (bNoCompare || FMath::IsNearlyEqual(A, B, 0.001f))
		{
			D.Rect(FFVRect(In.X, In.Y, In.W * A, In.H), Base);
		}
		else if (A > B)
		{
			D.Rect(FFVRect(In.X, In.Y, In.W * B, In.H), Base);
			D.Rect(FFVRect(In.X + In.W * B, In.Y, In.W * (A - B), In.H), FV::Col(176, 238, 96));
		}
		else
		{
			D.Rect(FFVRect(In.X, In.Y, In.W * A, In.H), Base);
			D.Rect(FFVRect(In.X + In.W * A, In.Y, In.W * (B - A), In.H), FV::Col(214, 72, 60));
		}
		D.Rect(FFVRect(In.X, In.Y, In.W, 1.f), FV::Col(255, 255, 255, 50));

		D.Text(RodStatText(St, V), FVector2D(ValueX, Y + 1.5f), StatSize, FVPal::Ink(), EFVAlign::Right, false, true);
		if (!bNoCompare)
		{
			const int32 Diff = FMath::RoundToInt(V) - FMath::RoundToInt(VCur);
			FString DiffText = TEXT("=");
			FLinearColor DiffC = FVPal::InkSoft();
			if (Diff != 0)
			{
				DiffText = FString::Printf(TEXT("%s%d%s"), Diff > 0 ? TEXT("+") : TEXT("-"), FMath::Abs(Diff), St.bPercent ? TEXT("%") : TEXT(""));
				DiffC = Diff > 0 ? InkGood() : InkBad();
			}
			D.Text(DiffText, FVector2D(X0 + W, Y + 1.5f), StatSize, DiffC, EFVAlign::Right, false, true);
		}
		Y += StatStep;
	}

	if (bP)
	{
		return;   // portrait: buy / equip live in the bottom action bar
	}

	// ---- purchase / equip ----
	const FFVRect Btn = ButtonRow(Panel);
	if (bForged)
	{
		DrawStatusLine(D, Panel, FVDBU6::RodSourceText(Rod), FVPal::InkSoft());   // 0.6.2 (Р80): not sold
		D.Button(Btn, TEXT("Куётся в кузне"), EFVButton::Normal, false, ActionTextSize());
	}
	else if (NeedA)
	{
		DrawStatusLine(D, Panel, RodLockText(P, *NeedA, true), InkBad());          // 0.6.2 (Р81)
		if (D.Button(Btn, TEXT("Нужно достижение"), EFVButton::Normal, true, ActionTextSize()))
		{
			TryBuyRod(P, Rod);   // the shopkeeper explains
		}
	}
	else if (!bOwned)
	{
		DrawPriceLine(D, Panel, Rod.Price, P.CanAfford(Rod.Price));
		if (D.Button(Btn, TEXT("Купить"), EFVButton::Primary, true, ActionTextSize()))
		{
			TryBuyRod(P, Rod);
		}
	}
	else if (!bSame)
	{
		DrawStatusLine(D, Panel, TEXT("Эта удочка уже ваша"), InkGood());
		if (D.Button(Btn, TEXT("Взять в руки"), EFVButton::Primary, true, ActionTextSize()))
		{
			DoEquipRod(P, Rod);
		}
	}
	else
	{
		DrawStatusLine(D, Panel, TEXT("Эта удочка у вас в руках"), InkEquip());
		D.Button(Btn, TEXT("В руках"), EFVButton::Normal, false, ActionTextSize());
	}
}

void FShopScreen::DrawBaitDetail(FVDraw& D, UFVSaveGame& P, const FFVBaitDef& Bait, const FFVRect& Panel)
{
	const int32 Level = P.GetLevel();
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	float Y = DrawDetailTitle(D, Panel, Bait.Name, TEXT("Наживка"), Bait.MinLevel, Level);
	const bool bP = FV::IsPortrait();

	const float BoxS = bP ? 40.f : 38.f;
	const FFVRect Box(X0, Y, BoxS, BoxS);
	IconBox(D, Box);
	FFVSprite BaitIcon;
	if (IconOf(Bait.Id, BaitIcon))
	{
		D.SpriteCentered(BaitIcon, Box.Center(), 2.f);
	}
	const float DescH = bP
		? TextWrappedLimited(D, Bait.Description, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, FV::Ui::Body(), FVPal::Ink(), 3, false)
		: D.TextWrapped(Bait.Description, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, 7.f, FVPal::Ink(), false);
	Y += FMath::Max(BoxS + 4.f, DescH + 5.f);

	if (bP)
	{
		DrawBaitDetailPortrait(D, P, Bait, Panel, Y);
		return;
	}

	D.Text(TEXT("Действие"), FVector2D(X0, Y), 7.5f, FVPal::Ink(), EFVAlign::Left, false, true);
	Y += 12.f;

	struct FEffect { const TCHAR* Label; FString Value; FLinearColor Color; };
	TArray<FEffect> Effects;
	{
		FLinearColor C;
		FString V = SignedPercent((1.f - Bait.BiteSpeed) * 100.f, true, C);
		Effects.Add({ TEXT("Скорость клёва"), V.IsEmpty() ? FString(TEXT("обычная")) : V, C });
		V = SignedPercent(Bait.RareBonus * 100.f, true, C);
		Effects.Add({ TEXT("Шанс редкой рыбы"), V.IsEmpty() ? FString(TEXT("обычный")) : V, C });
		V = SignedPercent((Bait.JunkMult - 1.f) * 100.f, false, C);
		Effects.Add({ TEXT("Мусор на крючке"), V.IsEmpty() ? FString(TEXT("как обычно")) : V, C });
		V = SignedPercent(Bait.SizeBonus * 100.f, true, C);
		Effects.Add({ TEXT("Размер улова"), V.IsEmpty() ? FString(TEXT("обычный")) : V, C });
		Effects.Add({ TEXT("В пачке"), FString::Printf(TEXT("%d шт."), Bait.PackSize), FVPal::Ink() });
	}
	for (const FEffect& E : Effects)
	{
		D.Text(E.Label, FVector2D(X0 + 1.f, Y), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
		D.Text(E.Value, FVector2D(X0 + W, Y), 7.f, E.Color, EFVAlign::Right, false, true);
		Y += 11.f;
	}

	// owned count + "use" button
	const int32 Count = P.BaitCount(Bait.Id);
	const bool bInUse = P.EquippedBait == Bait.Id && Count > 0;
	D.Text(TEXT("У вас"), FVector2D(X0 + 1.f, Y + 2.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
	D.Text(FString::Printf(TEXT("%d шт."), Count), FVector2D(X0 + 40.f, Y + 2.f), 7.f, FVPal::Ink(), EFVAlign::Left, false, true);
	if (bInUse)
	{
		Pill(D, TEXT("Насажена"), X0 + W, Y + 1.f, FV::Col(56, 118, 196));
	}
	else if (Count > 0)
	{
		if (D.Button(FFVRect(X0 + W - 70.f, Y - 1.f, 70.f, 13.f), TEXT("Насадить"), EFVButton::Normal, true, 6.5f))
		{
			DoEquipBait(P, Bait);
		}
	}

	// ---- prices for one pack and for five ----
	const float PY = Panel.Bottom() - 48.f;
	D.Rect(FFVRect(X0, PY - 3.f, W, 1.f), Divider());
	const float ColW = (W - 6.f) * 0.5f;
	const int64 Price1 = Bait.PackPrice;
	const int64 Price5 = int64(Bait.PackPrice) * 5;
	D.MoneyLabel(FVector2D(X0 + ColW * 0.5f, PY + 2.f), Price1, 9.f, EFVAlign::Center, P.CanAfford(Price1) ? FVPal::Ink() : InkBad());
	D.MoneyLabel(FVector2D(X0 + ColW + 6.f + ColW * 0.5f, PY + 2.f), Price5, 9.f, EFVAlign::Center, P.CanAfford(Price5) ? FVPal::Ink() : InkBad());

	const FFVRect Btn = ButtonRow(Panel);
	if (D.Button(FFVRect(Btn.X, Btn.Y, ColW, Btn.H), TEXT("Купить пачку"), EFVButton::Primary, true, 7.5f))
	{
		TryBuyBait(P, Bait, 1);
	}
	if (D.Button(FFVRect(Btn.X + ColW + 6.f, Btn.Y, ColW, Btn.H), TEXT("Купить ×5"), EFVButton::Normal, true, 7.5f))
	{
		TryBuyBait(P, Bait, 5);
	}
}

void FShopScreen::DrawBaitDetailPortrait(FVDraw& D, UFVSaveGame& P, const FFVBaitDef& Bait, const FFVRect& Panel, float Y)
{
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	const float S = FV::Ui::Small();
	const int32 Count = P.BaitCount(Bait.Id);

	struct FEffect { const TCHAR* Label; FString Value; FLinearColor Color; };
	TArray<FEffect> Effects;
	{
		FLinearColor C;
		FString V = SignedPercent((1.f - Bait.BiteSpeed) * 100.f, true, C);
		Effects.Add({ TEXT("Скорость клёва"), V.IsEmpty() ? FString(TEXT("обычная")) : V, C });
		V = SignedPercent(Bait.RareBonus * 100.f, true, C);
		Effects.Add({ TEXT("Шанс редкой рыбы"), V.IsEmpty() ? FString(TEXT("обычный")) : V, C });
		V = SignedPercent((Bait.JunkMult - 1.f) * 100.f, false, C);
		Effects.Add({ TEXT("Мусор на крючке"), V.IsEmpty() ? FString(TEXT("как обычно")) : V, C });
		V = SignedPercent(Bait.SizeBonus * 100.f, true, C);
		Effects.Add({ TEXT("Размер улова"), V.IsEmpty() ? FString(TEXT("обычный")) : V, C });
		Effects.Add({ TEXT("В пачке · у вас"), FString::Printf(TEXT("%d шт. · %d шт."), Bait.PackSize, Count), FVPal::Ink() });
	}
	const float RowStep = 13.f;
	const float BottomY = Panel.Bottom() - 36.f;   // the packs / "use" row
	for (const FEffect& E : Effects)
	{
		if (Y + RowStep > BottomY - 4.f)
		{
			break;   // very short panel: keep the controls visible
		}
		D.Text(E.Label, FVector2D(X0 + 1.f, Y), S, FVPal::InkSoft(), EFVAlign::Left, false, false);
		D.Text(E.Value, FVector2D(X0 + W, Y), S, E.Color, EFVAlign::Right, false, true);
		Y += RowStep;
	}

	// ---- how many packs the bar's "Купить" buys + "use" ----
	D.Rect(FFVRect(X0, BottomY - 5.f, W, 1.f), Divider());
	const float BH = 28.f;
	const float CX = DrawQtyChips(D, X0, BottomY, BH);

	const bool bInUse = P.EquippedBait == Bait.Id && Count > 0;
	const float UseW = FMath::Min(100.f, X0 + W - CX - 4.f);
	if (bInUse)
	{
		Pill(D, TEXT("Насажена"), X0 + W, BottomY + 6.f, FV::Col(56, 118, 196), 7.5f, 16.f);
	}
	else if (Count > 0 && UseW >= 60.f)
	{
		if (D.Button(FFVRect(X0 + W - UseW, BottomY, UseW, BH), TEXT("Насадить"), EFVButton::Normal, true, 9.f))
		{
			DoEquipBait(P, Bait);
		}
	}
	else if (Count <= 0)
	{
		D.Text(TEXT("нет в запасе"), FVector2D(X0 + W, BottomY + FMath::RoundToFloat((BH - D.LineHeight(S, false)) * 0.5f)), S, FVPal::InkSoft(), EFVAlign::Right, false, false);
	}
}

void FShopScreen::DrawCosmeticPreview(FVDraw& D, const UFVSaveGame& P, const FFVCosmeticDef& Cos, const FFVRect& Box)
{
	UFVSprites* Sprites = UFVSprites::Get();
	const float T = float(FMath::Fmod(D.Time, 1000.0));

	if (Cos.Slot == EFVCosmeticSlot::Bobber)
	{
		// water with ripples and the bobber enlarged
		const int32 Bands = 6;
		for (int32 b = 0; b < Bands; ++b)
		{
			const float F = float(b) / float(Bands - 1);
			const FLinearColor C = FMath::Lerp(FV::Col(96, 172, 222), FV::Col(40, 96, 166), F);
			D.Rect(FFVRect(Box.X, Box.Y + Box.H * float(b) / float(Bands), Box.W, Box.H / float(Bands) + 1.f), C);
		}
		const FVector2D C0 = Box.Center() + FVector2D(0.f, 8.f);
		for (int32 r = 0; r < 2; ++r)
		{
			const float Ph = FMath::Frac(T * 0.5f + float(r) * 0.5f);
			const float RX = 10.f + Ph * 44.f;
			const float RY = RX * 0.32f;
			const FLinearColor RC = FV::Col(220, 240, 255, uint8(150.f * (1.f - Ph)));
			for (int32 a = 0; a < 36; ++a)
			{
				const float Ang = float(a) / 36.f * 2.f * UE_PI;
				const FVector2D Pt = C0 + FVector2D(FMath::Cos(Ang) * RX, FMath::Sin(Ang) * RY);
				if (Box.Contains(Pt)) { D.Rect(FFVRect(FMath::FloorToFloat(Pt.X), FMath::FloorToFloat(Pt.Y), 1.f, 1.f), RC); }
			}
		}
		for (int32 s = 0; s < 5; ++s)
		{
			const float SX = Box.X + 14.f + float((s * 53) % int32(FMath::Max(10.f, Box.W - 28.f)));
			const float SY = Box.Y + 10.f + float((s * 29) % 30);
			const float Tw = 0.5f + 0.5f * FMath::Sin(T * 2.f + float(s) * 1.7f);
			D.Rect(FFVRect(SX, SY, 3.f, 1.f), FV::Col(230, 246, 255, uint8(90.f * Tw)));
		}
		if (Sprites)
		{
			const int32 Frame = int32(T * 2.f) % 2;
			const FVector2D Pos = C0 + FVector2D(0.f, -10.f + FMath::Sin(T * 3.f) * 1.5f);
			D.SpriteCentered(Sprites->Bobber(BobberStyleOf(Cos), Frame), Pos, 4.f);
		}
		D.Frame(Box, FVPal::WoodDark());
		return;
	}

	// sky + grass
	D.Rect(Box, FV::Col(170, 216, 240));
	D.Rect(FFVRect(Box.X, Box.Y + Box.H * 0.45f, Box.W, Box.H * 0.25f), FV::Col(150, 204, 234));
	const float GroundY = Box.Bottom() - 18.f;
	D.Rect(FFVRect(Box.X, GroundY, Box.W, Box.Bottom() - GroundY), FV::Col(108, 174, 70));
	D.Rect(FFVRect(Box.X, GroundY, Box.W, 2.f), FV::Col(142, 204, 88));
	for (int32 g = 0; g < 9; ++g)
	{
		const float GX = Box.X + 6.f + float(g) * (Box.W - 12.f) / 8.f;
		D.Rect(FFVRect(GX, GroundY + 5.f + float((g * 7) % 8), 1.f, 3.f), FV::Col(84, 146, 56));
	}
	// a soft cloud
	D.Circle(FVector2D(Box.X + 30.f, Box.Y + 16.f), 6.f, FV::Col(255, 255, 255, 200));
	D.Circle(FVector2D(Box.X + 39.f, Box.Y + 14.f), 8.f, FV::Col(255, 255, 255, 200));
	D.Circle(FVector2D(Box.X + 48.f, Box.Y + 17.f), 5.f, FV::Col(255, 255, 255, 200));

	if (Sprites)
	{
		if (Cos.Slot == EFVCosmeticSlot::Hat || Cos.Slot == EFVCosmeticSlot::Outfit)
		{
			FFVAppearance Look = P.BuildAppearance();
			if (Cos.Slot == EFVCosmeticSlot::Hat)
			{
				Look.Hat = HatStyleOf(Cos);
				Look.HatPrimary = Cos.Primary;
				Look.HatSecondary = Cos.Secondary;
			}
			else
			{
				Look.Outfit = OutfitStyleOf(Cos);
				Look.OutfitPrimary = Cos.Primary;
				Look.OutfitSecondary = Cos.Secondary;
			}
			static const EFVFacing Turn[] = { EFVFacing::Down, EFVFacing::Right, EFVFacing::Up, EFVFacing::Left };
			const int32 TurnIdx = int32(T / 1.8f) % 4;
			const int32 Frames = FMath::Max(1, UFVSprites::CharacterFrameCount(EFVCharAnim::Idle));
			const int32 Frame = int32(T * 2.f) % Frames;
			const float Scale = (FV::IsPortrait() && Box.H >= 150.f) ? 3.f : 2.f;   // tall portrait preview: bigger character
			const FVector2D Feet(Box.Center().X, GroundY + 8.f);
			D.Rect(FFVRect(Feet.X - 12.f, Feet.Y - 2.f, 24.f, 3.f), FV::Col(40, 70, 30, 90));
			D.Rect(FFVRect(Feet.X - 9.f, Feet.Y - 3.f, 18.f, 5.f), FV::Col(40, 70, 30, 60));
			const FFVSprite Spr = Sprites->Character(Look, EFVCharAnim::Idle, Turn[TurnIdx], Frame);
			D.Sprite(Spr, Feet - UFVSprites::CharacterAnchor() * Scale, Scale);
		}
		else if (Cos.Slot == EFVCosmeticSlot::Pet)
		{
			// the pet strolls back and forth, then sits for a moment
			const float Cycle = FMath::Fmod(T, 8.f);
			const float Range = FMath::Max(10.f, Box.W * 0.5f - 34.f);
			float X = 0.f;
			EFVPetAnim Anim = EFVPetAnim::Walk;
			bool bLeft = false;
			if (Cycle < 3.f)      { X = FMath::Lerp(-Range, Range, Cycle / 3.f); }
			else if (Cycle < 4.f) { X = Range; Anim = EFVPetAnim::Sit; bLeft = true; }
			else if (Cycle < 7.f) { X = FMath::Lerp(Range, -Range, (Cycle - 4.f) / 3.f); bLeft = true; }
			else                  { X = -Range; Anim = EFVPetAnim::Idle; }
			const int32 Frames = FMath::Max(1, UFVSprites::PetFrameCount(Anim));
			const int32 Frame = int32(T * (Anim == EFVPetAnim::Walk ? 8.f : 2.5f)) % Frames;
			const float Scale = 3.f;
			const FVector2D Feet(Box.Center().X + X, GroundY + 10.f);
			D.Rect(FFVRect(Feet.X - 12.f, Feet.Y - 2.f, 24.f, 3.f), FV::Col(40, 70, 30, 80));
			const FFVSprite Spr = Sprites->Pet(PetStyleOf(Cos), Anim, Frame);
			D.Sprite(Spr, FVector2D(Feet.X - Spr.W * Scale * 0.5f, Feet.Y - Spr.H * Scale), Scale, FLinearColor::White, bLeft);
		}
	}
	D.Frame(Box, FVPal::WoodDark());
}

void FShopScreen::DrawCosmeticDetail(FVDraw& D, UFVSaveGame& P, const FFVCosmeticDef& Cos, const FFVRect& Panel)
{
	const int32 Level = P.GetLevel();
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	float Y = DrawDetailTitle(D, Panel, Cos.Name, FVDB::SlotName(Cos.Slot), Cos.MinLevel, Level);
	const bool bOwned = P.OwnsCosmetic(Cos.Id);
	const bool bEquipped = bOwned && P.GetEquipped(Cos.Slot) == Cos.Id;

	if (FV::IsPortrait())
	{
		// preview on the left, description + where to get it / its state on the right; the action is in the bar
		const float BoxW = 140.f;
		const float BoxH = FMath::Clamp(Panel.Bottom() - 8.f - Y, 70.f, 190.f);
		const FFVRect Box(X0, Y, BoxW, BoxH);
		DrawCosmeticPreview(D, P, Cos, Box);
		const float TX = Box.Right() + 8.f;
		const float TW = X0 + W - TX;
		const float BodyS = FV::Ui::Body();
		const float SmallS = FV::Ui::Small();
		const float BodyStep = FMath::RoundToFloat(D.LineHeight(BodyS, false) * 0.95f);
		const float SmallStep = FMath::RoundToFloat(D.LineHeight(SmallS, true) * 0.95f);
		FString Note;
		FLinearColor NoteC = FVPal::InkSoft();
		if (bEquipped)                 { Note = TEXT("Сейчас надето"); NoteC = InkEquip(); }
		else if (bOwned)               { Note = TEXT("Уже в вашем гардеробе"); NoteC = InkGood(); }
		else if (Cos.Price <= 0)       { Note = RewardSourceText(Cos); NoteC = InkReward(); }
		else if (const FFVAchievementDef* NeedA = MissingAch(P, Cos)) { Note = NeedAchText(*NeedA, true); NoteC = InkBad(); }   // Р64
		else if (Level < Cos.MinLevel) { Note = FString::Printf(TEXT("Продаётся с %d уровня рыбака."), Cos.MinLevel); NoteC = InkBad(); }
		// the note gets up to 3 lines under the description, the description fills the rest of the column
		const float NoteH = Note.IsEmpty() ? 0.f : FMath::Min(D.MeasureWrapped(Note, TW, SmallS, true, 0.95f), SmallStep * 3.f);
		const float Avail = Box.H - 2.f - (NoteH > 0.f ? NoteH + 6.f : 0.f);
		const int32 DescLines = FMath::Max(1, FMath::FloorToInt(Avail / FMath::Max(1.f, BodyStep)));
		const float DescH = TextWrappedLimited(D, Cos.Description, FVector2D(TX, Y + 1.f), TW, BodyS, FVPal::Ink(), DescLines, false);
		if (!Note.IsEmpty())
		{
			TextWrappedLimited(D, Note, FVector2D(TX, Y + 1.f + DescH + 6.f), TW, SmallS, NoteC, 3, true);
		}
		return;
	}

	const FFVRect Box(X0, Y, W, 92.f);
	DrawCosmeticPreview(D, P, Cos, Box);
	Y += 96.f;
	Y += D.TextWrapped(Cos.Description, FVector2D(X0, Y), W, 7.f, FVPal::Ink(), false) + 3.f;
	const FFVAchievementDef* NeedA = bOwned ? nullptr : MissingAch(P, Cos);   // Р64
	if (!bOwned && Cos.Price <= 0)
	{
		D.TextWrapped(RewardSourceText(Cos), FVector2D(X0, Y), W, 7.f, InkReward(), true);
	}
	else if (NeedA)
	{
		D.TextWrapped(NeedAchText(*NeedA, true), FVector2D(X0, Y), W, 7.f, InkBad(), true);
	}

	const FFVRect Btn = ButtonRow(Panel);
	if (bEquipped)
	{
		DrawStatusLine(D, Panel, TEXT("Сейчас надето"), InkEquip());
		const bool bCanRemove = Cos.Slot == EFVCosmeticSlot::Hat || Cos.Slot == EFVCosmeticSlot::Pet;
		if (bCanRemove)
		{
			if (D.Button(Btn, Cos.Slot == EFVCosmeticSlot::Pet ? TEXT("Отпустить погулять") : TEXT("Снять"), EFVButton::Normal, true, ActionTextSize()))
			{
				DoUnequipCosmetic(P, Cos);
			}
		}
		else
		{
			D.Button(Btn, TEXT("Надето"), EFVButton::Normal, false, ActionTextSize());
		}
	}
	else if (bOwned)
	{
		DrawStatusLine(D, Panel, TEXT("Уже в вашем гардеробе"), InkGood());
		if (D.Button(Btn, TEXT("Надеть"), EFVButton::Primary, true, ActionTextSize()))
		{
			DoEquipCosmetic(P, Cos);
		}
	}
	else if (Cos.Price > 0 && NeedA)
	{
		DrawPriceLine(D, Panel, Cos.Price, P.CanAfford(Cos.Price));
		D.Button(Btn, TEXT("Нужно достижение"), EFVButton::Normal, false, ActionTextSize());
	}
	else if (Cos.Price > 0)
	{
		DrawPriceLine(D, Panel, Cos.Price, P.CanAfford(Cos.Price));
		if (D.Button(Btn, TEXT("Купить"), EFVButton::Primary, true, ActionTextSize()))
		{
			TryBuyCosmetic(P, Cos);
		}
	}
	else
	{
		DrawStatusLine(D, Panel, TEXT("Только в награду"), InkReward());
		D.Button(Btn, TEXT("Награда"), EFVButton::Normal, false, ActionTextSize());
	}
}

void FShopScreen::DrawUpgradeDetail(FVDraw& D, UFVSaveGame& P, bool bBackpack, int32 Target, const FFVRect& Panel)
{
	TArray<int32> Caps;
	TArray<int64> Prices;
	if (bBackpack)
	{
		for (const FFVBackpackLevel& L : FVDB::BackpackLevels()) { Caps.Add(L.Slots); Prices.Add(L.Price); }
	}
	else
	{
		for (const FFVKeepnetLevel& L : FVDB::KeepnetLevels()) { Caps.Add(L.Capacity); Prices.Add(L.Price); }
	}
	if (!Caps.IsValidIndex(Target))
	{
		return;
	}
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	const FString Name = bBackpack ? BackpackName(Target) : KeepnetName(Target);
	float Y = DrawDetailTitle(D, Panel, Name, bBackpack ? TEXT("Улучшение рюкзака") : TEXT("Улучшение садка"), 1, P.GetLevel());
	const bool bP = FV::IsPortrait();

	const float BoxS = bP ? 40.f : 38.f;
	const FFVRect Box(X0, Y, BoxS, BoxS);
	IconBox(D, Box);
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.SpriteCentered(Sprites->Icon(bBackpack ? EFVIcon::Backpack : EFVIcon::Keepnet), Box.Center(), 2.f);
	}
	const FString Desc = bBackpack
		? FString(TEXT("Рюкзак попросторнее: больше ресурсов, еды и снастей с собой, меньше беготни до сундука. Улучшения покупаются по порядку."))
		: FString(TEXT("Садок попросторнее: больше рыбы за один выход и меньше беготни до лавки. Улучшения покупаются по порядку."));
	const float DescH = bP
		? TextWrappedLimited(D, Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, FV::Ui::Body(), FVPal::Ink(), 4, false)
		: D.TextWrapped(Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, 7.f, FVPal::Ink(), false);
	Y += FMath::Max(BoxS + 4.f, DescH + 5.f);

	const int32 N = Caps.Num();
	const int32 CurLevel = FMath::Clamp(bBackpack ? P.BackpackLevel : P.KeepnetLevel, 0, N - 1);
	const int32 CurCap = bBackpack ? P.ContainerCapacity(EFVContainer::Backpack) : P.KeepnetCapacity();
	const int32 CurUsed = bBackpack ? P.ContainerUsed(EFVContainer::Backpack) : P.KeepnetUsed();
	const int32 NewCap = Caps[Target];
	const float RowSize = bP ? 8.f : 7.f;
	const float RowStep = bP ? 13.f : 11.f;
	auto Row = [&D, X0, W, RowSize](float RowY, const FString& Label, const FString& Value, const FLinearColor& C)
	{
		D.Text(Label, FVector2D(X0 + 1.f, RowY), RowSize, FVPal::InkSoft(), EFVAlign::Left, false, false);
		D.Text(Value, FVector2D(X0 + W, RowY), RowSize, C, EFVAlign::Right, false, true);
	};
	Row(Y, TEXT("Вместимость сейчас"), FString::Printf(TEXT("%d %s"), CurCap, Plural(CurCap, TEXT("место"), TEXT("места"), TEXT("мест"))), FVPal::Ink());
	Y += RowStep;
	const int32 Gain = NewCap - CurCap;
	Row(Y, bBackpack ? TEXT("С этим рюкзаком") : TEXT("С этим садком"), Gain > 0 ? FString::Printf(TEXT("%d (+%d)"), NewCap, Gain) : FString::Printf(TEXT("%d"), NewCap), Gain > 0 ? InkGood() : FVPal::Ink());
	Y += RowStep;
	Row(Y, TEXT("Занято сейчас"), FString::Printf(TEXT("%d / %d"), CurUsed, CurCap), CurUsed >= CurCap ? InkBad() : FVPal::Ink());
	Y += bP ? 18.f : 16.f;

	// level track: one pip per level
	const float PipH = bP ? 16.f : 12.f;
	const float PipW = FMath::Min(bP ? 52.f : 40.f, (W - float(N - 1) * 4.f) / float(FMath::Max(1, N)));
	const float TrackW = PipW * float(N) + 4.f * float(N - 1);
	float PX = X0 + (W - TrackW) * 0.5f;
	for (int32 i = 0; i < N; ++i)
	{
		const FFVRect Pip(PX, Y, PipW, PipH);
		const bool bHave = i <= CurLevel;
		D.Rect(Pip, bHave ? FV::Col(96, 176, 64) : FV::Col(226, 186, 130));
		D.Frame(Pip, i == Target ? FVPal::Gold() : BoxEdge(), i == Target ? 2.f : 1.f);
		D.Text(FString::FromInt(Caps[i]), FVector2D(Pip.Center().X, Pip.Y + (bP ? 3.f : 2.f)), bP ? 7.5f : 6.f, bHave ? FVPal::White() : FVPal::InkSoft(), EFVAlign::Center);
		PX += PipW + 4.f;
	}
	if (bP)
	{
		// state under the track (the price sits in the title, the button in the bottom bar)
		const FString State = Target <= CurLevel ? FString(TEXT("Это улучшение уже куплено"))
			: (Target == CurLevel + 1 ? FString(TEXT("Следующее улучшение — можно купить")) : FString(TEXT("Сначала купите предыдущее улучшение")));
		const FLinearColor StateC = Target <= CurLevel ? InkGood() : (Target == CurLevel + 1 ? FVPal::Ink() : FVPal::InkSoft());
		if (Y + PipH + 18.f < Panel.Bottom())
		{
			D.Text(FitText(D, State, W, FV::Ui::Small()), FVector2D(X0 + W * 0.5f, Y + PipH + 6.f), FV::Ui::Small(), StateC, EFVAlign::Center, false, true);
		}
		return;
	}

	const FFVRect Btn = ButtonRow(Panel);
	if (Target <= CurLevel)
	{
		DrawStatusLine(D, Panel, TEXT("Это улучшение уже куплено"), InkGood());
		D.Button(Btn, TEXT("Куплено"), EFVButton::Normal, false, ActionTextSize());
	}
	else if (Target == CurLevel + 1)
	{
		DrawPriceLine(D, Panel, Prices[Target], P.CanAfford(Prices[Target]));
		if (D.Button(Btn, bBackpack ? TEXT("Улучшить рюкзак") : TEXT("Улучшить садок"), EFVButton::Primary, true, ActionTextSize()))
		{
			if (bBackpack) { TryUpgradeBackpack(P); }
			else { TryUpgradeKeepnet(P); }
		}
	}
	else
	{
		DrawStatusLine(D, Panel, TEXT("Сначала купите предыдущее улучшение"), FVPal::InkSoft());
		D.Button(Btn, TEXT("Недоступно"), EFVButton::Normal, false, ActionTextSize());
	}
}

void FShopScreen::DrawItemDetail(FVDraw& D, UFVSaveGame& P, const FFVItemDef& Item, const FFVRect& Panel)
{
	const int32 Level = P.GetLevel();
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	FFVInvStack Tmp;
	Tmp.ItemId = Item.Id;
	Tmp.Count = 1;
	float Y = DrawDetailTitle(D, Panel, Item.Name, FVUI::StackKindText(Tmp), Item.MinLevel, Level);
	const bool bP = FV::IsPortrait();

	const float BoxS = bP ? 40.f : 38.f;
	const FFVRect Box(X0, Y, BoxS, BoxS);
	IconBox(D, Box);
	FFVSprite Icon;
	if (IconOf(Item.Id, Icon))
	{
		D.SpriteCentered(Icon, Box.Center(), 2.f);
	}
	FString Desc = Item.Description;
	if (Desc.IsEmpty())
	{
		switch (Item.Kind)
		{
		case EFVItemKind::Line:  Desc = TEXT("Леска покрепче — рыба реже срывается, а вываживать легче."); break;
		case EFVItemKind::Float: Desc = TEXT("Хороший поплавок — поклёвку видно издалека."); break;
		case EFVItemKind::Net:   Desc = TEXT("Сеть ловит рыбу сама, пока вы заняты другими делами."); break;
		default:                 Desc = TEXT("Пригодится в хозяйстве."); break;
		}
	}
	const float DescH = bP
		? TextWrappedLimited(D, Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, FV::Ui::Body(), FVPal::Ink(), 3, false)
		: D.TextWrapped(Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, 7.f, FVPal::Ink(), false);
	Y += FMath::Max(BoxS + 4.f, DescH + 5.f);

	// ---- what it does
	struct FLine { FString Text; FLinearColor Color; };
	TArray<FLine> Lines;
	switch (Item.Kind)
	{
	case EFVItemKind::Line:
	case EFVItemKind::Float:
	{
		for (const FString& L : FVDB::StatModsLines(Item.Tackle)) { Lines.Add({ L, InkGood() }); }
		// tier rule: tackle tier <= rod tier + 1 (rods are listed by tier)
		const FFVRodDef* MinRod = nullptr;
		for (const FFVRodDef& R : FVDB::Rods())
		{
			if (R.Tier + 1 >= int32(Item.Tier)) { MinRod = &R; break; }
		}
		Lines.Add({ MinRod ? FString::Printf(TEXT("Подходит к удочке «%s» и лучше"), *MinRod->Name) : FString(TEXT("Подходит к любой удочке")), FVPal::InkSoft() });
		Lines.Add({ TEXT("Ставится в мастерской дома («Оснастка»)"), FVPal::InkSoft() });
		break;
	}
	case EFVItemKind::Net:
		Lines.Add({ FString::Printf(TEXT("Улов до %d рыб, потом сеть надо опустошить"), Item.NetCapacity), FVPal::Ink() });
		Lines.Add({ FString::Printf(TEXT("Прочность %d · примерно 1 рыба за %s"), FMath::RoundToInt(Item.NetDurability), *UFVGame::DurationText(Item.NetMinutesPerFish)), FVPal::Ink() });
		Lines.Add({ TEXT("Ставится у колышков для сетей на берегу"), FVPal::InkSoft() });
		break;
	case EFVItemKind::Seed:
		Lines.Add({ TEXT("Сажается на грядку в огороде"), FVPal::InkSoft() });
		break;
	default:
		Lines.Add({ TEXT("Нужен для построек в доме и рецептов мастерской"), FVPal::InkSoft() });
		break;
	}
	const float LS = bP ? FV::Ui::Small() : 7.f;
	const float Limit = bP ? Panel.Bottom() - 42.f : Panel.Bottom() - 64.f;
	float Step = bP ? 13.f : 11.f;
	if (Lines.Num() > 0 && Y + float(Lines.Num()) * Step > Limit)
	{
		Step = FMath::Max(FMath::CeilToFloat(D.LineHeight(LS, false)), FMath::FloorToFloat((Limit - Y) / float(Lines.Num())));   // close up before dropping a line
	}
	for (const FLine& L : Lines)
	{
		if (Y + Step > Limit) { break; }
		D.Text(FitText(D, L.Text, W, LS, false), FVector2D(X0 + 1.f, Y), LS, L.Color, EFVAlign::Left, false, false);
		Y += Step;
	}
	const int32 Have = P.CountItem(Item.Id);

	if (bP)
	{
		// ---- how many the bar's "Купить" buys + how many are in the backpack
		const float BottomY = Panel.Bottom() - 36.f;
		const float BH = 28.f;
		D.Rect(FFVRect(X0, BottomY - 5.f, W, 1.f), Divider());
		DrawQtyChips(D, X0, BottomY, BH);
		const float S = FV::Ui::Small();
		D.Text(FString::Printf(TEXT("у вас %d"), Have), FVector2D(X0 + W, BottomY + FMath::RoundToFloat((BH - D.LineHeight(S, false)) * 0.5f)), S, FVPal::InkSoft(), EFVAlign::Right, false, false);
		return;
	}

	D.Text(TEXT("У вас"), FVector2D(X0 + 1.f, Panel.Bottom() - 62.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
	D.Text(FString::Printf(TEXT("%d шт. в рюкзаке"), Have), FVector2D(X0 + 40.f, Panel.Bottom() - 62.f), 7.f, FVPal::Ink(), EFVAlign::Left, false, true);

	// ---- prices for one piece and for five ----
	const float PY = Panel.Bottom() - 48.f;
	D.Rect(FFVRect(X0, PY - 3.f, W, 1.f), Divider());
	const float ColW = (W - 6.f) * 0.5f;
	const int64 Price1 = Item.BuyPrice;
	const int64 Price5 = int64(Item.BuyPrice) * 5;
	D.MoneyLabel(FVector2D(X0 + ColW * 0.5f, PY + 2.f), Price1, 9.f, EFVAlign::Center, P.CanAfford(Price1) ? FVPal::Ink() : InkBad());
	D.MoneyLabel(FVector2D(X0 + ColW + 6.f + ColW * 0.5f, PY + 2.f), Price5, 9.f, EFVAlign::Center, P.CanAfford(Price5) ? FVPal::Ink() : InkBad());
	const FFVRect Btn = ButtonRow(Panel);
	if (Item.Kind == EFVItemKind::Blueprint)
	{
		// update 6: a blueprint is learnt at once and bought only once
		if (P.HasBlueprint(Item.Id))
		{
			D.Button(Btn, TEXT("Чертёж изучен"), EFVButton::Normal, false, 7.5f);
		}
		else if (D.Button(Btn, TEXT("Купить чертёж"), EFVButton::Primary, true, 7.5f))
		{
			TryBuyItem(P, Item, 1);
		}
		return;
	}
	if (D.Button(FFVRect(Btn.X, Btn.Y, ColW, Btn.H), TEXT("Купить 1"), EFVButton::Primary, true, 7.5f))
	{
		TryBuyItem(P, Item, 1);
	}
	if (D.Button(FFVRect(Btn.X + ColW + 6.f, Btn.Y, ColW, Btn.H), TEXT("Купить ×5"), EFVButton::Normal, true, 7.5f))
	{
		TryBuyItem(P, Item, 5);
	}
}

void FShopScreen::DrawShovelDetail(FVDraw& D, UFVSaveGame& P, int32 Level, const FFVRect& Panel)
{
	const FFVShovelDef* Sh = ShovelAt(Level);
	if (!Sh)
	{
		return;
	}
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	float Y = DrawDetailTitle(D, Panel, Sh->Name, FString::Printf(TEXT("Лопата · уровень %d"), Level), Sh->MinLevel, P.GetLevel());
	const bool bP = FV::IsPortrait();

	const float BoxS = bP ? 40.f : 38.f;
	const FFVRect Box(X0, Y, BoxS, BoxS);
	IconBox(D, Box);
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		const FFVSprite Icon = Sprites->ShovelIcon(Level);
		D.SpriteCentered(Icon, Box.Center(), 2.f);
	}
	const FString Desc(TEXT("Копать червей на мягкой земле у воды и на грядках во дворе — раз в день на каждом месте. Чем лучше лопата, тем дольше раскопки."));
	const float DescH = bP
		? TextWrappedLimited(D, Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, FV::Ui::Body(), FVPal::Ink(), 3, false)
		: D.TextWrapped(Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, 7.f, FVPal::Ink(), false);
	Y += FMath::Max(BoxS + 4.f, DescH + 5.f);

	const float RowSize = bP ? 8.f : 7.f;
	const float RowStep = bP ? 13.f : 11.f;
	auto Row = [&D, X0, W, RowSize](float RowY, const FString& Label, const FString& Value, const FLinearColor& C)
	{
		D.Text(Label, FVector2D(X0 + 1.f, RowY), RowSize, FVPal::InkSoft(), EFVAlign::Left, false, false);
		D.Text(Value, FVector2D(X0 + W, RowY), RowSize, C, EFVAlign::Right, false, true);
	};
	const FFVShovelDef* Cur = ShovelAt(P.ShovelLevel);
	const float CurSeconds = Cur ? Cur->DigSeconds : 0.f;
	Row(Y, TEXT("Время на раскопки"), FString::Printf(TEXT("%s с"), *FVUI::Num(Sh->DigSeconds, 1)), Sh->DigSeconds > CurSeconds ? InkGood() : FVPal::Ink());
	Y += RowStep;
	Row(Y, TEXT("Сейчас у вас"), Cur ? Cur->Name : FString(TEXT("нет лопаты")), FVPal::Ink());
	Y += RowStep;
	if (Sh->WorkshopLevel > 0)
	{
		Row(Y, TEXT("Или сделать в мастерской"), FString::Printf(TEXT("уровень %d"), Sh->WorkshopLevel), FVPal::InkSoft());
		Y += RowStep;
	}
	Y += 4.f;

	// level track
	const int32 N = FVDB::Shovels().Num();
	const float PipH = bP ? 16.f : 12.f;
	const float PipW = FMath::Min(bP ? 52.f : 40.f, (W - float(N - 1) * 4.f) / float(FMath::Max(1, N)));
	const float TrackW = PipW * float(N) + 4.f * float(N - 1);
	float PX = X0 + (W - TrackW) * 0.5f;
	for (int32 i = 0; i < N; ++i)
	{
		const FFVRect Pip(PX, Y, PipW, PipH);
		const bool bHave = i + 1 <= P.ShovelLevel;
		D.Rect(Pip, bHave ? FV::Col(96, 176, 64) : FV::Col(226, 186, 130));
		D.Frame(Pip, i + 1 == Level ? FVPal::Gold() : BoxEdge(), i + 1 == Level ? 2.f : 1.f);
		D.Text(FString::FromInt(i + 1), FVector2D(Pip.Center().X, Pip.Y + (bP ? 3.f : 2.f)), bP ? 7.5f : 6.f, bHave ? FVPal::White() : FVPal::InkSoft(), EFVAlign::Center);
		PX += PipW + 4.f;
	}

	const bool bOwned = Level <= P.ShovelLevel;
	const bool bNext = Level == P.ShovelLevel + 1;
	const FFVShovelDef* Prev = ShovelAt(Level - 1);
	const FString LockedText = Prev ? FString::Printf(TEXT("Сначала нужна «%s»"), *Prev->Name) : FString(TEXT("Пока недоступно"));
	if (bP)
	{
		const FString State = bOwned ? FString(TEXT("Эта лопата уже у вас")) : (bNext ? FString(TEXT("Можно купить")) : LockedText);
		if (Y + PipH + 18.f < Panel.Bottom())
		{
			D.Text(FitText(D, State, W, FV::Ui::Small()), FVector2D(X0 + W * 0.5f, Y + PipH + 6.f), FV::Ui::Small(),
				bOwned ? InkGood() : (bNext ? FVPal::Ink() : FVPal::InkSoft()), EFVAlign::Center, false, true);
		}
		return;
	}
	const FFVRect Btn = ButtonRow(Panel);
	if (bOwned)
	{
		DrawStatusLine(D, Panel, TEXT("Эта лопата уже у вас"), InkGood());
		D.Button(Btn, TEXT("Куплено"), EFVButton::Normal, false, ActionTextSize());
	}
	else if (bNext)
	{
		DrawPriceLine(D, Panel, Sh->Price, P.CanAfford(Sh->Price));
		if (D.Button(Btn, TEXT("Купить лопату"), EFVButton::Primary, true, ActionTextSize()))
		{
			TryBuyShovel(P, Level);
		}
	}
	else
	{
		DrawStatusLine(D, Panel, LockedText, FVPal::InkSoft());
		D.Button(Btn, TEXT("Недоступно"), EFVButton::Normal, false, ActionTextSize());
	}
}

void FShopScreen::DrawAxeDetail(FVDraw& D, UFVSaveGame& P, int32 Level, const FFVRect& Panel)
{
	const FFVToolDef* Ax = AxeAt(Level);
	if (!Ax)
	{
		return;
	}
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	float Y = DrawDetailTitle(D, Panel, Ax->Name, FString::Printf(TEXT("Топор · уровень %d"), Level), Ax->MinLevel, P.GetLevel());
	const bool bP = FV::IsPortrait();
	const float BoxS = bP ? 40.f : 38.f;
	const FFVRect Box(X0, Y, BoxS, BoxS);
	IconBox(D, Box);
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.SpriteCentered(Sprites->AxeIcon(Level), Box.Center(), 2.f);
	}
	const FString Desc(TEXT("Рубить деревья с зарубкой в Лесу: брёвна идут на доски, ветки — Леснику. Топор получше берёт и твёрдые породы — его делают в мастерской."));
	const float DescH = bP
		? TextWrappedLimited(D, Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, FV::Ui::Body(), FVPal::Ink(), 4, false)
		: D.TextWrapped(Desc, FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, 7.f, FVPal::Ink(), false);
	Y += FMath::Max(BoxS + 4.f, DescH + 5.f);
	const float RowSize = bP ? 8.f : 7.f;
	const float RowStep = bP ? 13.f : 11.f;
	auto Row = [&D, X0, W, RowSize](float RowY, const FString& Label, const FString& Value, const FLinearColor& C)
	{
		D.Text(Label, FVector2D(X0 + 1.f, RowY), RowSize, FVPal::InkSoft(), EFVAlign::Left, false, false);
		D.Text(Value, FVector2D(X0 + W, RowY), RowSize, C, EFVAlign::Right, false, true);
	};
	static const TCHAR* Trees[] = { TEXT("берёзы"), TEXT("сосны"), TEXT("дуба"), TEXT("вековой ели") };
	Row(Y, TEXT("Рубит"), FString::Printf(TEXT("до %s"), Trees[FMath::Clamp(int32(Ax->MaxTree), 0, 3)]), FVPal::Ink());
	Y += RowStep;
	const FFVToolDef* Cur = AxeAt(P.U6.AxeLevel);
	Row(Y, TEXT("Сейчас у вас"), Cur ? Cur->Name : FString(TEXT("нет топора")), FVPal::Ink());
	const bool bOwned = Level <= P.U6.AxeLevel;
	const bool bNext = Level == P.U6.AxeLevel + 1;
	if (bP)
	{
		return;   // the bottom bar holds the action (DrawMainActionPortrait)
	}
	const FFVRect Btn = ButtonRow(Panel);
	if (bOwned)
	{
		DrawStatusLine(D, Panel, TEXT("Этот топор уже у вас"), InkGood());
		D.Button(Btn, TEXT("Куплено"), EFVButton::Normal, false, ActionTextSize());
	}
	else if (bNext)
	{
		DrawPriceLine(D, Panel, Ax->Price, P.CanAfford(Ax->Price));
		if (D.Button(Btn, TEXT("Купить топор"), EFVButton::Primary, true, ActionTextSize()))
		{
			TryBuyAxe(P, Level);
		}
	}
	else
	{
		DrawStatusLine(D, Panel, TEXT("Сначала нужен топор попроще"), FVPal::InkSoft());
		D.Button(Btn, TEXT("Недоступно"), EFVButton::Normal, false, ActionTextSize());
	}
}

void FShopScreen::DrawDecorDetail(FVDraw& D, UFVSaveGame& P, const FFVDecorDef& Dec, const FFVRect& Panel)
{
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	float Y = DrawDetailTitle(D, Panel, Dec.Name, FString::Printf(TEXT("Декор · %s"), *FVDB::DecorSlotName(Dec.Slot)), Dec.MinLevel, P.GetLevel());
	const bool bP = FV::IsPortrait();
	const bool bOwned = P.OwnsDecor(Dec.Id);
	const FString Desc = Dec.Description.IsEmpty() ? FString(TEXT("Украшение для дома и двора.")) : Dec.Description;
	// Р61: the try-on — the real yard / room fragment with the item at its place; under it the whole description
	// (wrapped, the preview gives up the room) and, for an owned item, the «уже у вас» note
	const FString Owned = TEXT("Уже у вас — расставьте дома или во дворе");
	const float LineS = bP ? FV::Ui::Small() : 7.f;
	const float LineH = D.LineHeight(LineS, false);
	const float TextW = W - 2.f;
	const float One = D.MeasureWrapped(TEXT("Щ"), TextW, LineS, false, 1.f);
	const int32 DescLines = One > 0.f ? FMath::Max(1, FMath::RoundToInt(D.MeasureWrapped(Desc, TextW, LineS, false, 1.f) / One)) : 1;
	const float TextH = float(DescLines) * LineH + (bOwned ? LineH : 0.f);
	const float Bottom = bP ? Panel.Bottom() - 6.f : Panel.Bottom() - 54.f;   // landscape: above the price line
	const float BoxH = FMath::Max(60.f, FMath::RoundToFloat(Bottom - Y - TextH - 3.f));
	FVDecorPreview::Draw(D, FFVRect(X0, Y, W, BoxH), Dec, &P);
	float TY = Y + BoxH + 3.f;
	TY += TextWrappedLimited(D, Desc, FVector2D(X0 + 1.f, TY), TextW, LineS, FVPal::Ink(), 40, false, LineH);
	if (bOwned)
	{
		D.Text(D.Ellipsize(Owned, TextW, LineS, true), FVector2D(X0 + 1.f, TY), LineS, InkGood(), EFVAlign::Left, false, true);
	}
	if (bP)
	{
		return;
	}
	const FFVRect Btn = ButtonRow(Panel);
	if (bOwned)
	{
		DrawStatusLine(D, Panel, TEXT("Уже куплено"), InkGood());
		D.Button(Btn, TEXT("Куплено"), EFVButton::Normal, false, ActionTextSize());
	}
	else
	{
		DrawPriceLine(D, Panel, Dec.Price, P.CanAfford(Dec.Price));
		if (D.Button(Btn, TEXT("Купить"), EFVButton::Primary, true, ActionTextSize()))
		{
			TryBuyDecor(P, Dec);
		}
	}
}

void FShopScreen::TryBuyAxe(UFVSaveGame& P, int32 Level)
{
	const FFVToolDef* Ax = AxeAt(Level);
	const int64 Price = Ax ? Ax->Price : 0;
	const int32 MinLevel = Ax ? Ax->MinLevel : 1;
	if (Level != P.U6.AxeLevel + 1)
	{
		ReportFailure(Level <= P.U6.AxeLevel ? EFVBuyResult::AlreadyOwned : EFVBuyResult::NotForSale, P, Price, MinLevel);
		return;
	}
	const EFVBuyResult Res = P.BuyAxe(TraderId);
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, Price, MinLevel);
		return;
	}
	PlaySfx(EFVSfx::Buy);
	const FFVToolDef* Now = AxeAt(P.U6.AxeLevel);
	Game->Toast(FString::Printf(TEXT("Куплено: %s!"), Now ? *Now->Name : TEXT("топор")), ToastGood(), EFVIcon::Axe);
	Game->NotifyProfileChanged();
	Say(Voice(TEXT("Топор добрый — в лесу пригодится."), TEXT("Держи топор, юнга! В лесу деревья с зарубкой — те и руби.")));
}

void FShopScreen::TryBuyDecor(UFVSaveGame& P, const FFVDecorDef& Dec)
{
	const EFVBuyResult Res = P.BuyDecor(Dec.Id);
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, Dec.Price, Dec.MinLevel);
		return;
	}
	if (IsBoutique())
	{
		P.U6.BoutiqueSpent += Dec.Price;   // achievement «Миллионер» (§13)
	}
	PlaySfx(EFVSfx::Buy);
	Game->Toast(FString::Printf(TEXT("Куплено: %s"), *Dec.Name), ToastGood(), EFVIcon::Check);
	Game->NotifyProfileChanged();
	if (IsBoutique())
	{
		Say(TEXT("Прекрасный выбор! Такая вещь украсит любой дом."));
	}
}

// -------------------------------------------------------------------------------- the «Продать» tab

int32 FShopScreen::FindSellIndex(const UFVSaveGame& P) const
{
	if (!SellSel.bHas)
	{
		return INDEX_NONE;
	}
	const TArray<FFVInvStack>& A = P.GetContainer(SellSel.C);
	for (int32 i = 0; i < A.Num(); ++i)
	{
		const FFVInvStack& S = A[i];
		if (S.ItemId == SellSel.Id && S.Quality == SellSel.Q && S.Flags == SellSel.Flags && S.Count > 0) { return i; }
	}
	return INDEX_NONE;
}

int32 FShopScreen::ResolveSellSelection(const UFVSaveGame& P)
{
	const int32 Found = FindSellIndex(P);
	if (Found != INDEX_NONE)
	{
		SellSel.LastIndex = Found;
		return Found;
	}
	// nothing selected (or it was sold out): pick a sellable stack near the old place, else the first one
	auto Pick = [this, &P](EFVContainer C, int32 Near) -> int32
	{
		const TArray<FFVInvStack>& A = P.GetContainer(C);
		if (A.Num() == 0) { return INDEX_NONE; }
		const int32 Start = FMath::Clamp(Near, 0, A.Num() - 1);
		int32 Any = INDEX_NONE;
		for (int32 k = 0; k < A.Num(); ++k)
		{
			const int32 i = (Start + k) % A.Num();
			if (A[i].Count <= 0) { continue; }
			if (IsMiller() && SellPrice(A[i]) <= 0) { continue; }   // §18: the miller's list hides what he does not buy
			if (Any == INDEX_NONE) { Any = i; }
			if (SellPrice(A[i]) > 0) { Any = i; break; }
		}
		if (Any != INDEX_NONE)
		{
			SellSel.C = C;
			SellSel.Id = A[Any].ItemId;
			SellSel.Q = A[Any].Quality;
			SellSel.Flags = A[Any].Flags;
			SellSel.bHas = true;
			SellSel.LastIndex = Any;
		}
		return Any;
	};
	int32 Index = INDEX_NONE;
	if (SellSel.bHas) { Index = Pick(SellSel.C, SellSel.LastIndex); }
	if (Index == INDEX_NONE) { Index = Pick(EFVContainer::Keepnet, 0); }
	if (Index == INDEX_NONE) { Index = Pick(EFVContainer::Backpack, 0); }
	if (Index == INDEX_NONE) { SellSel.bHas = false; }
	return Index;
}

void FShopScreen::DrawSellList(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, bool bPortrait)
{
	const FFVRect Area(Panel.X + 4.f, Panel.Y + 4.f, Panel.W - 8.f, Panel.H - 8.f);
	const float TopPad = bPortrait ? 2.f : 3.f;
	const float Slot = bPortrait ? FV::Ui::Slot() : 26.f;
	const float Gap = bPortrait ? 3.f : 2.f;
	const float Pitch = Slot + Gap;
	const float HeaderH = bPortrait ? 24.f : 15.f;
	const float HintH = bPortrait ? 26.f : 16.f;
	const float BarW = bPortrait ? 4.f : 3.f;
	const float AreaX = Area.X + 2.f;
	const float AreaW = Area.W - 4.f - BarW - 4.f;   // room for the scroll bar at the right
	const int32 Cols = FMath::Max(1, FMath::FloorToInt((AreaW + Gap) / Pitch));

	// entries: a header per container, then its rows of stacks (or a hint when empty)
	// Kind 0 header, 1 row, 2 hint, 3 lure header, 4 lure row, 5 lure hint (3..5: §18 the miller only)
	struct FEntry { uint8 Kind = 0; EFVContainer C = EFVContainer::Keepnet; int32 First = 0; float H = 0.f; };
	TArray<FEntry> Entries;
	static const EFVContainer Conts[] = { EFVContainer::Keepnet, EFVContainer::Backpack };
	const bool bMiller = IsMiller();
	TArray<int32> Vis[2];   // shown stack indices per container (the miller hides what he does not buy)
	for (int32 c = 0; c < 2; ++c)
	{
		const EFVContainer C = Conts[c];
		const TArray<FFVInvStack>& CA = P.GetContainer(C);
		for (int32 i = 0; i < CA.Num(); ++i)
		{
			if (!bMiller || (CA[i].Count > 0 && SellPrice(CA[i]) > 0)) { Vis[c].Add(i); }
		}
		Entries.Add({ 0, C, 0, HeaderH });
		const int32 N = Vis[c].Num();
		if (N == 0)
		{
			Entries.Add({ 2, C, 0, HintH });
			continue;
		}
		for (int32 First = 0; First < N; First += Cols)
		{
			Entries.Add({ 1, C, First, Pitch });
		}
	}
	const TArray<FName> Lures = bMiller ? MillerLures(P) : TArray<FName>();
	const float LureH = bPortrait ? FMath::Max(Slot, 40.f) + Gap : 24.f;
	if (bMiller)
	{
		Entries.Add({ 3, EFVContainer::Keepnet, 0, HeaderH });
		if (Lures.Num() == 0) { Entries.Add({ 5, EFVContainer::Keepnet, 0, HintH }); }
		for (int32 i = 0; i < Lures.Num(); ++i) { Entries.Add({ 4, EFVContainer::Keepnet, i, LureH }); }
	}
	TArray<float> Offsets;
	float Total = 0.f;
	for (const FEntry& E : Entries)
	{
		Offsets.Add(Total);
		Total += E.H;
	}
	FFVScrollView& Scroll = ListScroll[ListKey()];
	Scroll.Update(D, Area, Total + TopPad + 2.f);

	const int32 SelIndex = ResolveSellSelection(P);
	const float HeadS = bPortrait ? FV::Ui::Body() : 8.f;
	const float SmallS = bPortrait ? FV::Ui::Small() : 6.5f;
	const FFVInvStack* Hovered = nullptr;
	const float Y0 = Area.Y + TopPad - Scroll.Offset();
	D.PushClip(Area);
	for (int32 e = 0; e < Entries.Num(); ++e)
	{
		const FEntry& E = Entries[e];
		const float Y = Y0 + Offsets[e];
		if (Y + E.H < Area.Y)
		{
			continue;
		}
		if (Y > Area.Bottom())
		{
			break;
		}
		if (E.Kind >= 3)
		{
			if (DrawMillerLureEntry(D, P, E.Kind, Lures.IsValidIndex(E.First) ? Lures[E.First] : NAME_None,
				FFVRect(AreaX, Y, AreaW, E.H), bPortrait, Area))
			{
				break;   // sold: the list changed
			}
			continue;
		}
		const TArray<FFVInvStack>& A = P.GetContainer(E.C);
		const TArray<int32>& V = Vis[E.C == EFVContainer::Keepnet ? 0 : 1];
		if (E.Kind == 0)
		{
			const bool bKeep = E.C == EFVContainer::Keepnet;
			int32 Units = 0;
			for (const int32 i : V) { Units += FMath::Max(0, A[i].Count); }
			const FString Title = FString::Printf(TEXT("%s · %d шт."), bKeep ? TEXT("Садок") : TEXT("Рюкзак"), Units);
			const float TY = Y + FMath::RoundToFloat((E.H - 4.f - D.LineHeight(HeadS)) * 0.5f);
			D.Text(Title, FVector2D(AreaX, TY), HeadS, FVPal::Ink(), EFVAlign::Left, false, true);
			const int64 Value = P.ContainerValue(E.C, TraderId);
			if (Value > 0)
			{
				const float MW = D.MoneyLabel(FVector2D(AreaX + AreaW, TY + 1.f), Value, SmallS + 0.5f, EFVAlign::Right);
				D.Text(TEXT("всё за"), FVector2D(AreaX + AreaW - MW - 4.f, TY + 1.f), SmallS, FVPal::InkSoft(), EFVAlign::Right, false, false);
			}
			D.Rect(FFVRect(AreaX, Y + E.H - 3.f, AreaW, 1.f), Divider());
		}
		else if (E.Kind == 2)
		{
			const FString Msg = bMiller
				? (E.C == EFVContainer::Keepnet ? FString(TEXT("Копчёной рыбы нет — Прокоп берёт только копчёную"))
					: FString(TEXT("В рюкзаке нет копчёной рыбы")))
				: (E.C == EFVContainer::Keepnet ? FString(TEXT("Садок пуст — рыба появится после рыбалки"))
					: FString(TEXT("В рюкзаке пусто")));
			D.Text(FitText(D, Msg, AreaW, SmallS, false), FVector2D(AreaX + 4.f, Y + FMath::RoundToFloat((E.H - D.LineHeight(SmallS, false)) * 0.5f)), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		}
		else
		{
			for (int32 k = 0; k < Cols; ++k)
			{
				if (!V.IsValidIndex(E.First + k)) { break; }
				const int32 Idx = V[E.First + k];
				if (!A.IsValidIndex(Idx)) { break; }
				const FFVInvStack& S = A[Idx];
				const FFVRect R(AreaX + float(k) * Pitch, Y, Slot, Slot);
				const bool bSel = SellSel.bHas && SellSel.C == E.C && Idx == SelIndex;
				if (D.Slot(R, bSel, true) && !bSel)   // a tap (never fires after a drag / scroll)
				{
					SellSel.C = E.C;
					SellSel.Id = S.ItemId;
					SellSel.Q = S.Quality;
					SellSel.Flags = S.Flags;
					SellSel.bHas = true;
					SellSel.LastIndex = Idx;
					PlaySfx(EFVSfx::Click, 0.6f);
				}
				FVUI::StackCellContent(D, R, S, bPortrait, SellPrice(S) <= 0);
				if (!bPortrait && D.Hover(R)) { Hovered = &S; }
			}
		}
	}
	D.PopClip();
	Scroll.DrawBar(D, Area);
	Scroll.DrawFades(D, Area, FVPal::Parchment());
	if (Hovered && !FV::IsTouchUI())
	{
		const int32 Unit = SellPrice(*Hovered);
		FString Body = FVUI::StackKindText(*Hovered);
		Body += Unit > 0 ? FString::Printf(TEXT("\nЗдесь дадут: %s за шт."), *FVUI::Coins(Unit)) : FString(TEXT("\nЗдесь это не покупают"));
		if (Hovered->Count > 1 && Unit > 0) { Body += FString::Printf(TEXT("\nВся стопка (%d шт.): %s"), Hovered->Count, *FVUI::Coins(int64(Unit) * Hovered->Count)); }
		D.Tooltip(FVUI::StackTitle(*Hovered), Body);
	}
}

TArray<FName> FShopScreen::MillerLures(const UFVSaveGame& P) const
{
	TArray<FName> Out;
	for (const TPair<FName, int32>& It : P.Baits)
	{
		if (It.Value > 0 && FVMill::MillerLurePrice(It.Key) > 0) { Out.Add(It.Key); }
	}
	Out.Sort([](const FName& A, const FName& B)
	{
		const int32 PA = FVMill::MillerLurePrice(A);
		const int32 PB = FVMill::MillerLurePrice(B);
		return PA != PB ? PA > PB : A.LexicalLess(B);
	});
	return Out;
}

bool FShopScreen::DrawMillerLureEntry(FVDraw& D, UFVSaveGame& P, uint8 Kind, FName Lure, const FFVRect& R, bool bPortrait, const FFVRect& Clip)
{
	const float HeadS = bPortrait ? FV::Ui::Body() : 8.f;
	const float SmallS = bPortrait ? FV::Ui::Small() : 6.5f;
	if (Kind == 3)
	{
		const float TY = R.Y + FMath::RoundToFloat((R.H - 4.f - D.LineHeight(HeadS)) * 0.5f);
		D.Text(TEXT("Самодельные блёсны и мушки"), FVector2D(R.X, TY), HeadS, FVPal::Ink(), EFVAlign::Left, false, true);
		D.Rect(FFVRect(R.X, R.Bottom() - 3.f, R.W, 1.f), Divider());
		return false;
	}
	if (Kind == 5)
	{
		D.Text(FitText(D, TEXT("Нет самодельных блёсен — их делают в мастерской"), R.W - 4.f, SmallS, false),
			FVector2D(R.X + 4.f, R.Y + FMath::RoundToFloat((R.H - D.LineHeight(SmallS, false)) * 0.5f)), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		return false;
	}
	const FFVBaitDef* B = FVDB::FindBait(Lure);
	const int32* HaveP = P.Baits.Find(Lure);
	const int32 Have = HaveP ? *HaveP : 0;
	const int32 Unit = FVMill::MillerLurePrice(Lure);
	if (!B || Have <= 0 || Unit <= 0) { return false; }
	const float Pad = bPortrait ? 3.f : 2.f;
	const float BoxS = R.H - 2.f * Pad - (bPortrait ? 3.f : 0.f);
	const FFVRect Box(R.X, R.Y + Pad, BoxS, BoxS);
	IconBox(D, Box);
	FFVSprite Icon;
	if (IconOf(Lure, Icon)) { FVUI::SpriteFit(D, Icon, Box.Inset(2.f), FLinearColor::White, 2.f); }
	const float BtnW = FMath::Min(bPortrait ? 150.f : 96.f, R.W * 0.48f);
	const FFVRect Btn(R.Right() - BtnW, Box.Y, BtnW, BoxS);
	const float TX = Box.Right() + 6.f;
	const float TW = Btn.X - TX - 6.f;
	const float NameY = Box.Y + (bPortrait ? 1.f : 0.f);
	D.Text(FitText(D, B->Name, TW, HeadS), FVector2D(TX, NameY), HeadS, FVPal::Ink(), EFVAlign::Left, false, true);
	D.Text(FitText(D, FString::Printf(TEXT("×%d · %s за шт."), Have, *FV::Money(Unit)), TW, SmallS, false),
		FVector2D(TX, NameY + D.LineHeight(HeadS)), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
	const bool bVisible = Btn.Y >= Clip.Y && Btn.Bottom() <= Clip.Bottom();
	if (PriceButton(D, Btn, FString::Printf(TEXT("Все ×%d"), Have), int64(Unit) * Have, EFVButton::Normal, bVisible) && bVisible)
	{
		const FString Name = B->Name;
		const int64 Got = P.SellLuresToMiller(Lure, Have);
		if (Got > 0)
		{
			PlaySfx(EFVSfx::Coin);
			Game->Toast(FString::Printf(TEXT("Продано: %s ×%d  +%s"), *Name, Have, *FV::Money(Got)), ToastGood(), EFVIcon::Coin);
			Game->NotifyProfileChanged();
			Say(TEXT("Добрые блёсны! На омуте такие в самый раз."));
			return true;
		}
	}
	return false;
}

void FShopScreen::DrawSellDetail(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, bool bPortrait)
{
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	const int32 Index = FindSellIndex(P);
	const TArray<FFVInvStack>& A = P.GetContainer(SellSel.C);
	const int32 FishUnits = SellableFishUnits(P);
	const int64 FishValue = P.ContainerValue(EFVContainer::Keepnet, TraderId);
	if (Index == INDEX_NONE || !A.IsValidIndex(Index))
	{
		D.TextWrapped(IsMiller() ? TEXT("Прокоп берёт копчёную рыбу (дороже лавки) и самодельные блёсны и мушки — выберите в списке.")
			: TEXT("Продавать пока нечего. Наловите рыбы или соберите что-нибудь — и приходите."),
			FVector2D(X0 + 4.f, Panel.Y + (bPortrait ? 14.f : 40.f)), W - 8.f, bPortrait ? FV::Ui::Body() : 8.f, FVPal::InkSoft(), false);
		return;
	}
	const FFVInvStack S = A[Index];   // copy: a sale below may remove the stack
	const int32 Unit = SellPrice(S);
	const int64 StackValue = int64(Unit) * S.Count;
	const FString Name = FVUI::StackTitle(S);

	// the other trader's price, when his stall is open to the player
	FString OtherText;
	int32 OtherPrice = 0;
	for (const FFVTraderDef& T : FVDB::Traders())
	{
		if (T.Id == TraderId || !P.IsLocationUnlocked(T.Location)) { continue; }
		OtherPrice = FVDB::TraderSellPrice(T.Id, S.ItemId, FVUI::QualityOf(S.Quality), S.Flags);
		if (OtherPrice > 0 && OtherPrice != Unit) { OtherText = FString::Printf(TEXT("%s даст"), *T.Name); }
		break;
	}

	if (bPortrait)
	{
		const float TitleS = FV::Ui::Header();
		const float SmallS = FV::Ui::Small();
		const float BoxS = FMath::Min(52.f, Panel.H - 16.f);
		const FFVRect Box(X0, Panel.Y + 8.f, BoxS, BoxS);
		IconBox(D, Box);
		FFVSprite Icon;
		if (IconOf(S.ItemId, Icon)) { FVUI::SpriteFit(D, Icon, Box.Inset(3.f), FLinearColor::White, 2.f); }
		const float TX = Box.Right() + 8.f;
		const float TW = X0 + W - TX;
		float Y = Panel.Y + 6.f;
		const float PriceW = Unit > 0 ? D.MeasureText(FV::Money(Unit), TitleS).X + 14.f : 0.f;
		D.Text(FitText(D, Name, TW - PriceW - 6.f, TitleS), FVector2D(TX, Y), TitleS, FVUI::StackInk(S), EFVAlign::Left, false, true);
		if (Unit > 0) { D.MoneyLabel(FVector2D(X0 + W, Y + 1.f), Unit, TitleS - 2.f, EFVAlign::Right, FVPal::Ink()); }
		Y += FMath::RoundToFloat(D.LineHeight(TitleS) + 1.f);
		const EFVQuality Q = FVUI::QualityOf(S.Quality);
		float QX = TX;
		if (Q != EFVQuality::Normal)
		{
			D.Stars(FVector2D(QX, FMath::RoundToFloat(Y + D.LineHeight(SmallS) * 0.5f - 4.f)), Q);
			QX += 10.f;
		}
		D.Text(FitText(D, FString::Printf(TEXT("%s · ×%d"), *FVUI::StackKindText(S), S.Count), X0 + W - QX, SmallS, false), FVector2D(QX, Y), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		Y += FMath::RoundToFloat(D.LineHeight(SmallS) + 3.f);
		if (Unit <= 0)
		{
			D.Text(FitText(D, Voice(TEXT("Такое у меня не берут, внучок."), TEXT("Такое на причале не берут, юнга.")), TW, SmallS), FVector2D(TX, Y), SmallS, InkBad(), EFVAlign::Left, false, true);
			return;
		}
		if (S.Count > 1)
		{
			D.Text(TEXT("Вся стопка:"), FVector2D(TX, Y), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			D.MoneyLabel(FVector2D(TX + D.MeasureText(TEXT("Вся стопка:"), SmallS, false).X + 4.f, Y), StackValue, SmallS, EFVAlign::Left, FVPal::Ink());
			Y += FMath::RoundToFloat(D.LineHeight(SmallS) + 2.f);
		}
		if (!OtherText.IsEmpty() && Y + D.LineHeight(SmallS) <= Panel.Bottom() - 2.f)
		{
			D.Text(OtherText, FVector2D(TX, Y), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			D.MoneyLabel(FVector2D(TX + D.MeasureText(OtherText, SmallS, false).X + 4.f, Y), OtherPrice, SmallS, EFVAlign::Left, OtherPrice > Unit ? InkBad() : InkGood());
		}
		return;
	}

	// ---- landscape
	float Y = DrawDetailTitle(D, Panel, Name, FVUI::StackKindText(S), 1, P.GetLevel());
	const float BoxS = 38.f;
	const FFVRect Box(X0, Y, BoxS, BoxS);
	IconBox(D, Box);
	FFVSprite Icon;
	if (IconOf(S.ItemId, Icon)) { FVUI::SpriteFit(D, Icon, Box.Inset(2.f), FLinearColor::White, 2.f); }
	const TArray<FString> Info = FVUI::StackInfoLines(S, &P);
	const float InfoH = Info.Num() > 0
		? TextWrappedLimited(D, FString::Join(Info, TEXT(" · ")), FVector2D(X0 + BoxS + 6.f, Y + 1.f), W - BoxS - 6.f, 7.f, FVPal::Ink(), 4, false)
		: 0.f;
	Y += FMath::Max(BoxS + 4.f, InfoH + 5.f);

	auto Row = [&D, X0, W](float RowY, const FString& Label, int64 Amount, const FLinearColor& C)
	{
		D.Text(Label, FVector2D(X0 + 1.f, RowY), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
		D.MoneyLabel(FVector2D(X0 + W, RowY), Amount, 7.5f, EFVAlign::Right, C);
	};
	if (Unit > 0)
	{
		Row(Y, TEXT("Цена за штуку"), Unit, FVPal::Ink());
		Y += 11.f;
		Row(Y, FString::Printf(TEXT("Вся стопка (%d шт.)"), S.Count), StackValue, FVPal::Ink());
		Y += 11.f;
		if (!OtherText.IsEmpty() && Y < Panel.Bottom() - 64.f)
		{
			Row(Y, OtherText, OtherPrice, OtherPrice > Unit ? InkBad() : InkGood());
		}
	}
	else
	{
		D.Text(FitText(D, Voice(TEXT("Такое у меня не берут, внучок."), TEXT("Такое на причале не берут, юнга.")), W, 7.5f), FVector2D(X0 + 1.f, Y), 7.5f, InkBad(), EFVAlign::Left, false, true);
	}

	// ---- buttons: [Продать 1][Продать стопку] / [Вся рыба]
	const float RowA = Panel.Bottom() - 52.f;
	const FFVRect Btn = ButtonRow(Panel);
	const float ColW = FMath::FloorToFloat((W - 6.f) * 0.5f);
	const bool bCan = Unit > 0;
	if (bCan)
	{
		if (PriceButton(D, FFVRect(X0, RowA, ColW, 20.f), TEXT("Продать 1"), Unit, EFVButton::Normal, true))
		{
			SellSelected(P, 1);
			return;   // the stack may be gone
		}
		if (PriceButton(D, FFVRect(X0 + ColW + 6.f, RowA, W - ColW - 6.f, 20.f), S.Count > 1 ? FString::Printf(TEXT("Стопку ×%d"), S.Count) : FString(TEXT("Стопку")), StackValue, EFVButton::Normal, true))
		{
			SellSelected(P, S.Count);
			return;
		}
	}
	else
	{
		D.Button(FFVRect(X0, RowA, W, 20.f), TEXT("Не покупают"), EFVButton::Normal, false, ActionTextSize());
	}
	if (FishUnits > 0)
	{
		if (PriceButton(D, Btn, FString::Printf(TEXT("Вся рыба ×%d"), FishUnits), FishValue, EFVButton::Primary, true))
		{
			ConfirmSellAllFish(P);
		}
	}
	else
	{
		D.Button(Btn, TEXT("В садке нет рыбы"), EFVButton::Normal, false, ActionTextSize());
	}
}

void FShopScreen::DrawSellRowPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win)
{
	const FFVRect Row0 = FVUI::ActionBarExtraRow(D, Win);
	// read everything before selling
	const int32 Index = FindSellIndex(P);
	const TArray<FFVInvStack>& A = P.GetContainer(SellSel.C);
	const bool bSel = Index != INDEX_NONE && A.IsValidIndex(Index);
	const FFVInvStack S = bSel ? A[Index] : FFVInvStack();
	const int32 Unit = bSel ? SellPrice(S) : 0;
	const bool bCan = bSel && Unit > 0;

	const bool bOne = FVUI::PriceButton(D, FVDraw::SplitH(Row0, 0, 2, 8.f), TEXT("Продать 1"), bCan ? Unit : -1, EFVButton::Normal, bCan);
	const bool bStack = FVUI::PriceButton(D, FVDraw::SplitH(Row0, 1, 2, 8.f), S.Count > 1 ? FString::Printf(TEXT("Стопку ×%d"), S.Count) : FString(TEXT("Стопку")),
		bCan ? int64(Unit) * S.Count : -1, EFVButton::Normal, bCan);
	if (bCan && bOne) { SellSelected(P, 1); }
	else if (bCan && bStack) { SellSelected(P, S.Count); }
}

void FShopScreen::SellSelected(UFVSaveGame& P, int32 Count)
{
	const int32 Index = FindSellIndex(P);
	const TArray<FFVInvStack>& A = P.GetContainer(SellSel.C);
	if (Index == INDEX_NONE || !A.IsValidIndex(Index))
	{
		return;
	}
	const FFVInvStack Stack = A[Index];
	const int32 N = FMath::Clamp(Count, 1, FMath::Max(1, Stack.Count));
	const int64 Got = P.SellFrom(SellSel.C, Index, N, TraderId);
	if (Got <= 0)
	{
		PlaySfx(EFVSfx::Error);
		Game->Toast(TEXT("Это здесь не покупают"), ToastBad(), EFVIcon::Close);
		return;
	}
	PlaySfx(EFVSfx::Coin);
	Game->Toast(FString::Printf(TEXT("Продано: %s ×%d  +%s"), *FVUI::StackTitle(Stack), N, *FV::Money(Got)), ToastGood(), EFVIcon::Coin);
	Game->NotifyProfileChanged();
	if (N > 1 || Got >= 200)
	{
		Say(Voice(TEXT("Хороша рыбка! Держи монеты — честно заработал."), TEXT("По рукам! Честная цена за честный улов.")));
	}
}

void FShopScreen::ConfirmSellAllFish(UFVSaveGame& P)
{
	const int32 Units = SellableFishUnits(P);
	if (Units <= 0 || !Game)
	{
		return;
	}
	const int64 Value = P.ContainerValue(EFVContainer::Keepnet, TraderId);
	TWeakObjectPtr<UFVGame> WeakGame(Game);
	const FName Tr = TraderId;
	Game->Confirm(TEXT("Продать всю рыбу?"),
		FString::Printf(TEXT("Все %d шт. из садка — за %s."), Units, *FVUI::Coins(Value)),
		[WeakGame, Tr]()
		{
			UFVGame* G = WeakGame.Get();
			UFVSaveGame* Prof = G ? G->GetProfile() : nullptr;
			if (!Prof) { return; }
			const int64 Got = Prof->SellAllFish(Tr);
			if (Got > 0)
			{
				PlaySfx(EFVSfx::Coin);
				G->Toast(FString::Printf(TEXT("Улов продан: +%s"), *FVUI::Coins(Got)), ToastGood(), EFVIcon::Coin);
				G->NotifyProfileChanged();
			}
		},
		TEXT("Продать"), TEXT("Отмена"));
}

// -------------------------------------------------------------------------------- equip (shared by both layouts)

void FShopScreen::DoEquipRod(UFVSaveGame& P, const FFVRodDef& Rod)
{
	if (!P.EquipRod(Rod.Id))
	{
		PlaySfx(EFVSfx::Error);
		return;
	}
	PlaySfx(EFVSfx::Equip);
	Game->GetScene().RefreshAppearance();
	Game->NotifyProfileChanged();
	Game->Toast(FString::Printf(TEXT("В руках: «%s»"), *Rod.Name), ToastGood(), EFVIcon::Rod);
}

void FShopScreen::DoEquipBait(UFVSaveGame& P, const FFVBaitDef& Bait)
{
	if (!P.EquipBait(Bait.Id))
	{
		PlaySfx(EFVSfx::Error);
		return;
	}
	PlaySfx(EFVSfx::Equip);
	Game->NotifyProfileChanged();
	Game->Toast(FString::Printf(TEXT("Наживка: %s"), *Bait.Name), ToastGood(), EFVIcon::Bait);
}

void FShopScreen::DoEquipCosmetic(UFVSaveGame& P, const FFVCosmeticDef& Cos)
{
	if (!P.EquipCosmetic(Cos.Id))
	{
		PlaySfx(EFVSfx::Error);
		return;
	}
	PlaySfx(EFVSfx::Equip);
	Game->GetScene().RefreshAppearance();
	Game->NotifyProfileChanged();
	Game->Toast(FString::Printf(TEXT("Надето: %s"), *Cos.Name), ToastGood(), EFVIcon::Wardrobe);
}

void FShopScreen::DoUnequipCosmetic(UFVSaveGame& P, const FFVCosmeticDef& Cos)
{
	P.UnequipSlot(Cos.Slot);
	PlaySfx(EFVSfx::Equip, 0.8f, 0.9f);
	Game->GetScene().RefreshAppearance();
	Game->NotifyProfileChanged();
}

// -------------------------------------------------------------------------------- purchases

void FShopScreen::ReportFailure(EFVBuyResult Res, const UFVSaveGame& P, int64 Price, int32 MinLevel)
{
	FString Msg;
	EFVIcon Icon = EFVIcon::Close;
	switch (Res)
	{
	case EFVBuyResult::Ok:
		return;
	case EFVBuyResult::AlreadyOwned:
		Msg = TEXT("Это у вас уже есть");
		Icon = EFVIcon::Check;
		break;
	case EFVBuyResult::NotEnoughMoney:
		Msg = FString::Printf(TEXT("Не хватает монет: нужно ещё %s"), *FV::Money(FMath::Max<int64>(1, Price - P.Money)));
		Icon = EFVIcon::Coin;
		Say(Voice(TEXT("Эх, монет маловато… Налови ещё рыбки да приходи — отложу для тебя."),
			TEXT("Маловато монет, юнга. Сходи на лов да возвращайся.")));
		break;
	case EFVBuyResult::LevelTooLow:
		Msg = FString::Printf(TEXT("Нужен уровень рыбака %d"), MinLevel);
		Icon = EFVIcon::Level;
		Say(Voice(TEXT("Рановато тебе, внучок. Набей руку на рыбалке — тогда и продам."),
			TEXT("Рано тебе такую снасть, юнга. Подрасти в деле.")));
		break;
	case EFVBuyResult::NotForSale:
		Msg = TEXT("Этот товар здесь не продаётся");
		Icon = EFVIcon::Lock;
		break;
	case EFVBuyResult::NoRoom:
		Msg = TEXT("Рюкзак полон — освободите место");
		Icon = EFVIcon::Backpack;
		Say(Voice(TEXT("Куда ж ты это положишь? Рюкзак-то битком. Разгрузись в сундук дома."),
			TEXT("Трюм полон, юнга! Разгрузи рюкзак — тогда и бери.")));
		break;
	case EFVBuyResult::NotEnoughMaterials:
		Msg = TEXT("Не хватает материалов");
		Icon = EFVIcon::Hammer;
		break;
	case EFVBuyResult::StationRequired:
		Msg = TEXT("Нужна постройка в доме");
		Icon = EFVIcon::House;
		break;
	case EFVBuyResult::MaxLevel:
		Msg = TEXT("Уже максимальный уровень");
		Icon = EFVIcon::Check;
		break;
	case EFVBuyResult::Locked:
		Msg = TEXT("Сначала нужно достижение");   // 0.6.2 (Р81): TryBuyRod names the achievement itself
		Icon = EFVIcon::Lock;
		break;
	case EFVBuyResult::Invalid:
	default:
		Msg = TEXT("Покупка сейчас невозможна");
		Icon = EFVIcon::Close;
		break;
	}
	PlaySfx(EFVSfx::Error);
	if (Game)
	{
		Game->Toast(Msg, ToastBad(), Icon);
	}
}

void FShopScreen::TryBuyRod(UFVSaveGame& P, const FFVRodDef& Rod)
{
	const EFVBuyResult Res = P.BuyRod(Rod.Id);
	if (Res == EFVBuyResult::Locked)
	{
		// 0.6.2 (Р81): «Румб» / «Полюс» go only to a proven angler
		const FFVAchievementDef* A = MissingRodAch(P, Rod);
		const FString AchName = A ? A->Name : FString(TEXT("нужное достижение"));
		PlaySfx(EFVSfx::Error);
		Game->Toast(A ? RodLockText(P, *A, true) : FString(TEXT("Нужно достижение")), ToastBad(), EFVIcon::Lock);
		Say(FString::Printf(TEXT("Такую снасть продаю только проверенному рыбаку. Сначала — «%s»."), *AchName));
		return;
	}
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, Rod.Price, Rod.MinLevel);
		return;
	}
	PlaySfx(EFVSfx::Buy);
	Game->Toast(FString::Printf(TEXT("Куплена удочка «%s»!"), *Rod.Name), ToastGood(), EFVIcon::Rod);
	Game->GetScene().RefreshAppearance();
	Game->NotifyProfileChanged();
	Say(Voice(TEXT("Добрая удочка! С такой и сома вытащишь. Уже у тебя в руках."),
		TEXT("Добрая удочка! С такой хоть на кита. Держи крепче, юнга.")));
}

void FShopScreen::TryBuyBait(UFVSaveGame& P, const FFVBaitDef& Bait, int32 Packs)
{
	Packs = FMath::Max(1, Packs);
	const bool bHadNoBait = P.GetBait() == nullptr;
	const EFVBuyResult Res = P.BuyBait(Bait.Id, Packs);
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, int64(Bait.PackPrice) * Packs, Bait.MinLevel);
		return;
	}
	PlaySfx(EFVSfx::Buy);
	FString Msg = FString::Printf(TEXT("Куплено: %s, %d шт."), *Bait.Name, Bait.PackSize * Packs);
	if (bHadNoBait && P.EquipBait(Bait.Id))
	{
		Msg += TEXT(" — уже на крючке");
	}
	Game->Toast(Msg, ToastGood(), EFVIcon::Bait);
	Game->NotifyProfileChanged();
	Say(Packs > 1 ? Voice(TEXT("Вот это запас! Рыба сама на крючок полезет."), TEXT("Солидный запас! На целую вахту хватит."))
		: Voice(TEXT("Свеженькая наживка — рыба не устоит!"), TEXT("Держи наживку — рыба клюнет, будь спок.")));
}

void FShopScreen::TryBuyItem(UFVSaveGame& P, const FFVItemDef& Item, int32 Count)
{
	Count = FMath::Max(1, Count);
	const EFVBuyResult Res = P.BuyItem(Item.Id, Count, TraderId);
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, int64(Item.BuyPrice) * Count, Item.MinLevel);
		return;
	}
	PlaySfx(EFVSfx::Buy);
	Game->Toast(Count > 1 ? FString::Printf(TEXT("Куплено: %s ×%d — в рюкзаке"), *Item.Name, Count)
		: FString::Printf(TEXT("Куплено: %s — в рюкзаке"), *Item.Name), ToastGood(), EFVIcon::Backpack);
	Game->NotifyProfileChanged();
	if (Item.Kind == EFVItemKind::Line || Item.Kind == EFVItemKind::Float)
	{
		Say(Voice(TEXT("Поставь её в мастерской на удочку — сразу почуешь разницу!"), TEXT("Снасть что надо! Ставь в мастерской — и в море.")));
	}
	else if (Item.Kind == EFVItemKind::Net)
	{
		Say(Voice(TEXT("Сетку поставь у колышков на берегу — пусть рыбка сама ловится."), TEXT("Крепкая сеть! Ставь у берега — к утру улов будет.")));
	}
	else
	{
		Say(Voice(TEXT("Добрая вещь — в хозяйстве пригодится!"), TEXT("Отличный выбор, юнга! Такое на причале ценят.")));
	}
}

void FShopScreen::TryBuyShovel(UFVSaveGame& P, int32 Level)
{
	const FFVShovelDef* Sh = ShovelAt(Level);
	const int64 Price = Sh ? Sh->Price : 0;
	const int32 MinLevel = Sh ? Sh->MinLevel : 1;
	if (Level != P.ShovelLevel + 1)
	{
		ReportFailure(Level <= P.ShovelLevel ? EFVBuyResult::AlreadyOwned : EFVBuyResult::NotForSale, P, Price, MinLevel);
		return;
	}
	const EFVBuyResult Res = P.BuyShovel(TraderId);
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, Price, MinLevel);
		return;
	}
	PlaySfx(EFVSfx::Buy);
	const FFVShovelDef* Now = ShovelAt(P.ShovelLevel);
	Game->Toast(FString::Printf(TEXT("Куплено: %s!"), Now ? *Now->Name : TEXT("лопата")), ToastGood(), EFVIcon::Shovel);
	Game->NotifyProfileChanged();
	Say(Voice(TEXT("Лопатой червей накопаешь — и на наживку тратиться не надо!"), TEXT("Добрая лопата! Хоть якорь выкапывай.")));
}

void FShopScreen::TryBuyCosmetic(UFVSaveGame& P, const FFVCosmeticDef& Cos)
{
	// Р64: the expert collection of the boutique is sold only after its achievement
	if (!P.OwnsCosmetic(Cos.Id))
	{
		if (const FFVAchievementDef* NeedA = MissingAch(P, Cos))
		{
			PlaySfx(EFVSfx::Error);
			Game->Toast(NeedAchText(*NeedA, false), ToastBad(), EFVIcon::Lock);
			return;
		}
	}
	const EFVBuyResult Res = P.BuyCosmetic(Cos.Id);
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, Cos.Price, Cos.MinLevel);
		return;
	}
	if (IsBoutique())
	{
		P.U6.BoutiqueSpent += Cos.Price;   // achievement «Миллионер» (§13)
	}
	PlaySfx(EFVSfx::Buy);
	Game->Toast(FString::Printf(TEXT("Куплено: %s"), *Cos.Name), ToastGood(), EFVIcon::Wardrobe);
	Game->NotifyProfileChanged();
	Say(Cos.Slot == EFVCosmeticSlot::Pet ? TEXT("Ох и славный зверь! Корми его хорошо, он тебе удачу принесёт.")
		: TEXT("Ох, красота! Хоть сейчас на ярмарку."));

	// Offer to put it on right away. The lambda must not touch this screen: it may be closed by then.
	TWeakObjectPtr<UFVGame> WeakGame(Game);
	const FName Id = Cos.Id;
	const FString Name = Cos.Name;
	const FString Question = Cos.Slot == EFVCosmeticSlot::Pet
		? FString::Printf(TEXT("Взять «%s» с собой на рыбалку?"), *Name)
		: FString::Printf(TEXT("Надеть «%s» прямо сейчас?"), *Name);
	Game->Confirm(TEXT("Обновка!"), Question, [WeakGame, Id, Name]()
	{
		UFVGame* G = WeakGame.Get();
		UFVSaveGame* Prof = G ? G->GetProfile() : nullptr;
		if (!Prof)
		{
			return;
		}
		if (Prof->EquipCosmetic(Id))
		{
			PlaySfx(EFVSfx::Equip);
			G->GetScene().RefreshAppearance();
			G->NotifyProfileChanged();
			G->Toast(FString::Printf(TEXT("Надето: %s"), *Name), ToastGood(), EFVIcon::Wardrobe);
		}
	}, TEXT("Надеть"), TEXT("Позже"));
}

void FShopScreen::TryUpgradeKeepnet(UFVSaveGame& P)
{
	const TArray<FFVKeepnetLevel>& KL = FVDB::KeepnetLevels();
	const int32 Next = P.KeepnetLevel + 1;
	const int64 Price = KL.IsValidIndex(Next) ? KL[Next].Price : 0;
	const EFVBuyResult Res = P.UpgradeKeepnet();
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, Price, 1);
		return;
	}
	PlaySfx(EFVSfx::Buy);
	const int32 Cap = P.KeepnetCapacity();
	Game->Toast(FString::Printf(TEXT("Садок улучшен: теперь %d %s!"), Cap, Plural(Cap, TEXT("место"), TEXT("места"), TEXT("мест"))), ToastGood(), EFVIcon::Keepnet);
	Game->NotifyProfileChanged();
	Say(TEXT("Садок попросторнее — и улова поболее! Носи на здоровье."));
}

void FShopScreen::TryUpgradeBackpack(UFVSaveGame& P)
{
	const TArray<FFVBackpackLevel>& BL = FVDB::BackpackLevels();
	const int32 Next = P.BackpackLevel + 1;
	const int64 Price = BL.IsValidIndex(Next) ? BL[Next].Price : 0;
	const EFVBuyResult Res = P.UpgradeBackpack();
	if (Res != EFVBuyResult::Ok)
	{
		ReportFailure(Res, P, Price, 1);
		return;
	}
	PlaySfx(EFVSfx::Buy);
	const int32 Slots = P.ContainerCapacity(EFVContainer::Backpack);
	Game->Toast(FString::Printf(TEXT("Рюкзак улучшен: теперь %d %s!"), Slots, Plural(Slots, TEXT("место"), TEXT("места"), TEXT("мест"))), ToastGood(), EFVIcon::Backpack);
	Game->NotifyProfileChanged();
	Say(TEXT("Рюкзак попросторнее — таскай больше добра! Только спину береги."));
}

} // namespace FVShopScreenImpl
} // namespace

TUniquePtr<FVScreen> FVMakeShopScreen(UFVGame* Game)
{
	return MakeUnique<FVShopScreenImpl::FShopScreen>(Game);
}
