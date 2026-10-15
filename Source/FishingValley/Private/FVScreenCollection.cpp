// FVScreenCollection.cpp — «Коллекция рыб» (fish encyclopedia per location) and «Достижения» (achievements list).
// Update 3: locations / achievement filters are swipeable pages (FFVPager), every list scrolls smoothly (FFVScrollView).
// Hotkeys: C closes the collection, J closes the achievements; ←/→ switch pages, ↑/↓ scroll the achievements.

#include "CoreMinimal.h"
#include "FVScreens.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVUIKit.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"
#include "FVExpertFight.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVAudio.h"
#include "Algo/StableSort.h"

namespace
{
namespace FVCollectionScreenImpl
{
// ============================================================================================
// Helpers (private to this file)
// ============================================================================================

/** The places of the collection in unlock order (0.6.1, as the map list): the Old mill (level 10) right after the
    volcano, the City (level 10 + the boat) after the mill, the Cave (level 12) last. */
TArray<const FFVLocationDef*> CollectionLocs()
{
	TArray<const FFVLocationDef*> Out;
	for (const FFVLocationDef& L : FVDB::Locations()) { Out.Add(&L); }
	Algo::StableSortBy(Out, [](const FFVLocationDef* L) { return L->MinLevel * 4 + (L->Id == FVDBU6::CityId() ? 1 : 0); });
	return Out;
}

FLinearColor InkGood()  { return FV::Col(46, 132, 34); }
FLinearColor InkBad()   { return FV::Col(196, 40, 36); }
FLinearColor Divider()  { return FV::Col(214, 162, 104); }

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

/** Keeps as many whole leading words as fit ("Вулканическое озеро" -> "Вулканическое"), else cuts with "…". */
FString FitWords(const FVDraw& D, const FString& S, float MaxW, float Size)
{
	if (D.MeasureText(S, Size).X <= MaxW)
	{
		return S;
	}
	TArray<FString> Words;
	S.ParseIntoArrayWS(Words);
	FString Best;
	FString Acc;
	for (const FString& Word : Words)
	{
		const FString Next = Acc.IsEmpty() ? Word : Acc + TEXT(" ") + Word;
		if (D.MeasureText(Next, Size).X > MaxW) { break; }
		Acc = Next;
		Best = Acc;
	}
	return Best.IsEmpty() ? FitText(D, S, MaxW, Size) : Best;
}

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

void CheckBadge(FVDraw& D, const FVector2D& C, float R = 5.f)
{
	D.Circle(C, R, FV::Col(36, 76, 22));
	D.Circle(C, R - 1.f, FVPal::Green());
	D.Line(C + FVector2D(-2.4f, 0.f), C + FVector2D(-0.6f, 1.9f), FVPal::White(), 1.4f);
	D.Line(C + FVector2D(-0.6f, 1.9f), C + FVector2D(2.6f, -2.2f), FVPal::White(), 1.4f);
}

/** Tiny padlock (7x8 px) at TopLeft. */
void MiniLock(FVDraw& D, const FVector2D& TopLeft)
{
	const float X = FMath::FloorToFloat(TopLeft.X);
	const float Y = FMath::FloorToFloat(TopLeft.Y);
	D.Frame(FFVRect(X + 1.f, Y, 5.f, 5.f), FV::Col(84, 78, 84));
	D.Rect(FFVRect(X, Y + 3.f, 7.f, 5.f), FV::Col(222, 176, 58));
	D.Frame(FFVRect(X, Y + 3.f, 7.f, 5.f), FV::Col(110, 70, 26));
	D.Rect(FFVRect(X + 3.f, Y + 5.f, 1.f, 2.f), FV::Col(80, 50, 20));
}

/** Small header: icon + "Label: X / Y" + progress bar + percent. */
void ProgressHeader(FVDraw& D, EFVIcon Icon, const FString& Label, int32 Done, int32 Total, const FLinearColor& Fill)
{
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.Sprite(Sprites->Icon(Icon), FVector2D(24.f, 43.f), 1.f);
	}
	D.Text(FString::Printf(TEXT("%s: %d / %d"), *Label, Done, Total), FVector2D(45.f, 41.f), 9.f, FVPal::Ink(), EFVAlign::Left, false, true);
	const float Frac = Total > 0 ? FMath::Clamp(float(Done) / float(Total), 0.f, 1.f) : 0.f;
	D.Bar(FFVRect(45.f, 54.f, 200.f, 7.f), Frac, Fill, FV::Col(120, 84, 60), true);
	D.Text(FString::Printf(TEXT("%d%%"), FMath::FloorToInt(Frac * 100.f)), FVector2D(251.f, 52.5f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, true);
}

/** Same as ProgressHeader, at any position (portrait layout). */
void ProgressHeaderAt(FVDraw& D, const FVector2D& Pos, float BarW, EFVIcon Icon, const FString& Label, int32 Done, int32 Total, const FLinearColor& Fill)
{
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.Sprite(Sprites->Icon(Icon), FVector2D(Pos.X, Pos.Y + 2.f), 1.f);
	}
	D.Text(FString::Printf(TEXT("%s: %d / %d"), *Label, Done, Total), FVector2D(Pos.X + 21.f, Pos.Y), 9.f, FVPal::Ink(), EFVAlign::Left, false, true);
	const float Frac = Total > 0 ? FMath::Clamp(float(Done) / float(Total), 0.f, 1.f) : 0.f;
	D.Bar(FFVRect(Pos.X + 21.f, Pos.Y + 14.f, BarW, 8.f), Frac, Fill, FV::Col(120, 84, 60), true);
	D.Text(FString::Printf(TEXT("%d%%"), FMath::FloorToInt(Frac * 100.f)), FVector2D(Pos.X + 27.f + BarW, Pos.Y + 12.5f), 7.5f, FVPal::InkSoft(), EFVAlign::Left, false, true);
}

/** Soft horizontal fade over the left or right edge of R (Bg = the colour behind): hints at more content sideways. */
void FadeH(FVDraw& D, const FFVRect& R, const FLinearColor& Bg, bool bLeftEdge)
{
	const int32 Bands = FMath::Max(1, FMath::FloorToInt(R.W / 2.f));
	for (int32 b = 0; b < Bands; ++b)
	{
		const float K = 1.f - (float(b) + 0.5f) / float(Bands);   // 1 at the edge -> 0 inside
		const float X = bLeftEdge ? R.X + float(b) * 2.f : R.Right() - float(b + 1) * 2.f;
		D.Rect(FFVRect(X, R.Y, 2.f, R.H), FV::Alpha(Bg, 0.95f * K * K));
	}
}

int32 CountCaught(const UFVSaveGame& P, const TArray<const FFVFishDef*>& Fish)
{
	int32 N = 0;
	for (const FFVFishDef* F : Fish)
	{
		const FFVFishRecord* Rec = F ? P.GetRecord(F->Id) : nullptr;
		if (Rec && Rec->Caught > 0) { ++N; }
	}
	return N;
}

// ---- portrait: bottom action bar (FVDraw::ActionBar: "← Назад" on the left, the actions to its right) ----

/** Label size of the big bar buttons. */
float BarTextSize() { return FV::Ui::Body() + 0.5f; }

/** Word-wraps S into at most MaxLines lines (the last one is shortened with "…"). Returns the height used. */
float TextWrappedLimited(FVDraw& D, const FString& S, const FVector2D& Pos, float MaxW, float Size, const FLinearColor& C, int32 MaxLines, bool bBold = false)
{
	if (S.IsEmpty() || MaxLines <= 0)
	{
		return 0.f;
	}
	const float Step = FMath::RoundToFloat(D.LineHeight(Size, bBold) * 0.95f);
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

/** Update 6.1: the lines TextWrappedLimited makes of S with no line limit (the same greedy word wrap). */
TArray<FString> WrapAllLines(const FVDraw& D, const FString& S, float MaxW, float Size, bool bBold = false)
{
	TArray<FString> Lines;
	TArray<FString> Words;
	S.ParseIntoArrayWS(Words);
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
	if (!Cur.IsEmpty())
	{
		Lines.Add(Cur);
	}
	return Lines;
}

/** Line step of TextWrappedLimited (and of DrawLines below). */
float WrapStep(const FVDraw& D, float Size, bool bBold = false)
{
	return FMath::RoundToFloat(D.LineHeight(Size, bBold) * 0.95f);
}

/** Draws Lines (from WrapAllLines) one under another, Step apart. Returns the height used. */
float DrawLines(FVDraw& D, const TArray<FString>& Lines, const FVector2D& Pos, float MaxW, float Size, const FLinearColor& C, bool bBold, float Step)
{
	float Y = Pos.Y;
	for (const FString& Line : Lines)
	{
		D.Text(D.Ellipsize(Line, MaxW, Size, bBold), FVector2D(Pos.X, Y), Size, C, EFVAlign::Left, false, bBold);
		Y += Step;
	}
	return float(Lines.Num()) * Step;
}

/** Update 6.1: the tap-to-open hint of a card whose text does not fit — «ещё ▾» (bWord) or «▾» while closed, «▴» while
    open; Right = its right end, CY = its vertical centre. Returns the width used. */
float OpenHint(FVDraw& D, float Right, float CY, bool bOpen, float Size, bool bWord)
{
	const FLinearColor C = FVPal::InkSoft();
	const float TriX = FMath::FloorToFloat(Right - 4.f);
	Triangle(D, FVector2D(TriX, CY), bOpen ? 0 : 1, 4, C);
	if (!bWord || bOpen)
	{
		return 8.f;
	}
	const FString Word(TEXT("ещё"));
	D.Text(Word, FVector2D(TriX - 6.f, FMath::RoundToFloat(CY - D.LineHeight(Size, false) * 0.5f)), Size, C, EFVAlign::Right, false, false);
	return 10.f + D.MeasureText(Word, Size, false).X;
}

void PlayTapSfx()
{
	if (UFVAudio* Audio = UFVAudio::Get())
	{
		Audio->Play(EFVSfx::Click);
	}
}

FString DepthText(float MinDepth)
{
	if (MinDepth < 0.25f) { return TEXT("мелководье"); }
	if (MinDepth < 0.55f) { return TEXT("средняя"); }
	return TEXT("глубоко");
}

FString Cm(float V)
{
	FString S = FString::Printf(TEXT("%.1f"), V);
	S.ReplaceInline(TEXT("."), TEXT(","));
	if (S.EndsWith(TEXT(",0"))) { S.LeftChopInline(2); }
	return S + TEXT(" см");
}

FLinearColor RarityInk(EFVRarity R)
{
	return FVDB::RarityColor(R);
}

/** Update 5: the wrapped label lines of one legend checklist row (CheckRow); a missing rule says what happens without it
    («не клюёт» for a hard rule, «обрыв» for the leader-snapping soft one). Hotfix 061: wrapped, never cut. */
TArray<FString> CheckRowLines(const FVDraw& D, const FFVLegendCheckItem& It, float MaxW, float Size, float RowH)
{
	const float IconS = FMath::Clamp(FMath::FloorToFloat(RowH - 1.f), 8.f, 16.f);
	FString Label = It.Label;
	if (!It.bOk) { Label += It.bHard ? TEXT(" — не клюёт") : TEXT(" — обрыв"); }
	return WrapAllLines(D, Label, MaxW - IconS - 3.f, Size, true);
}

/** Update 5: one row of a legend's preparation checklist — the Check (green) / Close (red) icon, then the label (its extra
    wrapped lines go Step apart under the first). Returns the number of the label lines (>= 1). */
int32 CheckRow(FVDraw& D, const FFVLegendCheckItem& It, const FVector2D& Pos, float MaxW, float Size, float RowH, float Step)
{
	const float IconS = FMath::Clamp(FMath::FloorToFloat(RowH - 1.f), 8.f, 16.f);
	const float LineH = D.LineHeight(Size, true);
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.SpriteCentered(Sprites->Icon(It.bOk ? EFVIcon::Check : EFVIcon::Close), FVector2D(Pos.X + IconS * 0.5f, Pos.Y + LineH * 0.5f), IconS / 16.f);
	}
	const FLinearColor C = It.bOk ? FVPal::Ink() : (It.bHard ? FV::Col(196, 40, 36) : FV::Col(196, 112, 20));
	const TArray<FString> Lines = CheckRowLines(D, It, MaxW, Size, RowH);
	DrawLines(D, Lines, FVector2D(Pos.X + IconS + 3.f, Pos.Y), MaxW - IconS - 3.f, Size, C, true, Step);
	return FMath::Max(1, Lines.Num());
}

EFVHatStyle HatStyleOf(const FFVCosmeticDef& C)       { return EFVHatStyle(FMath::Min<int32>(C.Style, int32(EFVHatStyle::Count) - 1)); }
EFVOutfitStyle OutfitStyleOf(const FFVCosmeticDef& C) { return EFVOutfitStyle(FMath::Min<int32>(C.Style, int32(EFVOutfitStyle::Count) - 1)); }
EFVBobberStyle BobberStyleOf(const FFVCosmeticDef& C) { return EFVBobberStyle(FMath::Min<int32>(C.Style, int32(EFVBobberStyle::Count) - 1)); }
EFVPetStyle PetStyleOf(const FFVCosmeticDef& C)       { return EFVPetStyle(FMath::Min<int32>(C.Style, int32(EFVPetStyle::Count) - 1)); }

void DrawCosmeticIcon(FVDraw& D, const FFVCosmeticDef& C, const FVector2D& Center, float Scale)
{
	UFVSprites* Sprites = UFVSprites::Get();
	if (!Sprites)
	{
		return;
	}
	switch (C.Slot)
	{
	case EFVCosmeticSlot::Hat:    D.SpriteCentered(Sprites->HatIcon(HatStyleOf(C), C.Primary, C.Secondary), Center, Scale); break;
	case EFVCosmeticSlot::Outfit: D.SpriteCentered(Sprites->OutfitIcon(OutfitStyleOf(C), C.Primary, C.Secondary), Center, Scale); break;
	case EFVCosmeticSlot::Bobber: D.SpriteCentered(Sprites->Bobber(BobberStyleOf(C), 0), Center, Scale * 2.f); break;
	case EFVCosmeticSlot::Pet:    D.SpriteCentered(Sprites->Pet(PetStyleOf(C), EFVPetAnim::Idle, 0), Center, Scale); break;
	default: break;
	}
}

// ============================================================================================
// «Коллекция рыб»
// ============================================================================================

/** Size of the species grid (one page of the location pager). */
struct FGridMetrics
{
	int32 Cols = 4;
	float SlotW = 72.f;
	float SlotH = 58.f;      // portrait: fixed; landscape: shrinks with the rows (see DrawSpeciesPage)
	float Gap = 6.f;
	float Pad = 4.f;         // inner padding of the scroll area (room for the selection frame)
	float ViewH = 188.f;     // scroll viewport height (landscape: maximum)
	float CardH = 36.f;      // portrait: location card height under the grid
};

class FCollectionScreen final : public FVScreen
{
public:
	explicit FCollectionScreen(UFVGame* InGame) : FVScreen(InGame) {}

	virtual void OnOpen() override
	{
		// start on the location the player is fishing at
		const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
		const TArray<const FFVLocationDef*> Locs = CollectionLocs();
		int32 Start = 0;
		for (int32 i = 0; P && i < Locs.Num(); ++i)
		{
			if (Locs[i]->Id == P->CurrentLocation)
			{
				Start = i;
				break;
			}
		}
		LocPager.NumPagesCached = FMath::Max(1, Locs.Num());
		// update 5: a fish id as the context (OpenScreenWith) opens its location with that fish selected
		if (const FFVFishDef* F = Game ? FVDB::FindFish(Game->GetScreenContext()) : nullptr)
		{
			for (int32 i = 0; i < Locs.Num(); ++i)
			{
				if (Locs[i]->Id != F->Location) { continue; }
				Start = i;
				SyncState(Locs.Num());
				SelFish[i] = FMath::Max(0, FVDB::FishAt(F->Location).IndexOfByKey(F));
				break;
			}
		}
		LocPager.SetPage(Start);
		// update 6.1: the autotest context "fv_expand" (never a fish id) opens the location card when its text is cut
		bAutoOpenCard = Game && Game->GetScreenContext() == FName(TEXT("fv_expand"));
	}

	virtual void Tick(float Dt, const FFVInput& In) override
	{
		if (!Game)
		{
			return;
		}
		if (In.WasPressed(EKeys::C))
		{
			Game->CloseScreen();
			return;   // this screen is destroyed
		}
		if (In.WasPressed(EKeys::Left))  { LocPager.GoTo(LocPager.Page - 1); }   // the pager slides + clicks
		if (In.WasPressed(EKeys::Right)) { LocPager.GoTo(LocPager.Page + 1); }
	}

	virtual void Draw(FVDraw& D) override;

private:
	FFVPager LocPager;                    // page = location index (swipe / tabs / ←→)
	TArray<FFVScrollView> GridScroll;     // per location: the species grid
	TArray<int32> SelFish;                // per location: selected fish
	FFVScrollView DetailScroll;           // facts under the aquarium
	// detail bookkeeping: the facts start at the top when another fish is shown (update 4: no fade — instant)
	FName ShownFish;
	int32 ShownLoc = INDEX_NONE;
	// landscape: the grid slot under the mouse this frame (the detail shows it)
	int32 HoverLoc = INDEX_NONE;
	int32 HoverIdx = INDEX_NONE;
	// update 6.1: the location card opened by a tap (its whole description; the grid above gives it the room)
	int32 OpenLocCard = INDEX_NONE;
	bool bAutoOpenCard = false;

	void SyncState(int32 NumLocs)
	{
		if (GridScroll.Num() != NumLocs) { GridScroll.SetNum(NumLocs); }
		if (SelFish.Num() != NumLocs) { SelFish.SetNumZeroed(NumLocs); }
	}

	void DrawGridSlot(FVDraw& D, const UFVSaveGame& P, const FFVFishDef& F, const FFVRect& SR, bool bSelected);
	/** bCut: the description does not fit the closed card (draws the «ещё ▾» hint); bOpen: opened by a tap («▴»). */
	void DrawLocationCard(FVDraw& D, const UFVSaveGame& P, const FFVLocationDef& Loc, int32 Caught, int32 Total, const FFVRect& R, bool bCut, bool bOpen);
	/** Height a location card of width W needs for the whole description (mirrors DrawLocationCard). */
	static float LocCardNeedH(const FVDraw& D, const UFVSaveGame& P, const FFVLocationDef& Loc, float W);
	void DrawFishDetail(FVDraw& D, const UFVSaveGame& P, const FFVFishDef& F, const FFVLocationDef* Loc, const FFVRect& Panel);
	/** One pager page: the scrolling species grid of location Li + its card, drawn in Panel (local page coords). */
	void DrawSpeciesPage(FVDraw& D, const UFVSaveGame& P, int32 Li, const FFVRect& Panel, const FGridMetrics& M);
	/** Detail panel of the fish ShowIdx (the selected one, or the one under the mouse); shown instantly, no transition. */
	void DrawDetailArea(FVDraw& D, const UFVSaveGame& P, int32 Page, const TArray<const FFVFishDef*>& FishHere, int32 ShowIdx, const FFVRect& Panel);
	/** Portrait: the row of location buttons — a carousel that slides together with the pages. */
	void DrawLocationChips(FVDraw& D, const UFVSaveGame& P, const FFVRect& Row);
	/** Phone layout: the fish (aquarium + facts) on top, locations + species grid in the thumb zone at the bottom. */
	void DrawPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& Win);
};

void FCollectionScreen::Draw(FVDraw& D)
{
	if (!Game)
	{
		return;
	}
	const bool bPortrait = FV::IsPortrait();
	const FFVRect Win = FV::WindowRect();   // same frame as the other overlay screens (landscape: below the HUD bar)
	if (D.Window(Win, TEXT("Коллекция рыб")))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	if (!FV::IsTouchUI() && !bPortrait) (void)0; /* update 3: no key hints at the bottom (see the guide) */
	const UFVSaveGame* P = Game->GetProfile();
	const TArray<const FFVLocationDef*> Locs = CollectionLocs();
	if (!P || Locs.Num() == 0)
	{
		D.Text(TEXT("Коллекция пока пуста."), Win.Center(), 8.f, FVPal::InkSoft(), EFVAlign::Center);
		return;
	}
	const int32 NumLocs = Locs.Num();
	SyncState(NumLocs);
	if (bPortrait)
	{
		DrawPortrait(D, *P, Win);
		return;
	}

	// ---- panels + the pager over the grid panel ----
	const FFVRect GridPanel(20.f, 85.f, 330.f, 254.f);
	const FFVRect DetailPanel(354.f, 85.f, 266.f, 254.f);
	D.Panel(GridPanel, EFVPanel::Parchment);
	D.Panel(DetailPanel, EFVPanel::Parchment);
	const FFVRect PagesArea = GridPanel.Inset(2.f);
	LocPager.Update(D, PagesArea, NumLocs);
	const int32 Page = FMath::Clamp(LocPager.Page, 0, NumLocs - 1);
	const FFVLocationDef& Loc = *Locs[Page];
	const TArray<const FFVFishDef*> FishHere = FVDB::FishAt(Loc.Id);
	const int32 CaughtHere = CountCaught(*P, FishHere);

	// ---- header: total progress + this location ----
	ProgressHeader(D, EFVIcon::Book, TEXT("Поймано видов"), P->SpeciesCaught(), FVDB::Fish().Num(), FVPal::Green());
	{
		const bool bComplete = FishHere.Num() > 0 && CaughtHere >= FishHere.Num();
		const FString LocText = FString::Printf(TEXT("%s: %d / %d"), *Loc.Name, CaughtHere, FishHere.Num());
		const float RX = Win.Right() - 14.f;
		D.Text(LocText, FVector2D(RX, 41.f), 8.f, bComplete ? InkGood() : FVPal::Ink(), EFVAlign::Right, false, true);
		if (bComplete)
		{
			CheckBadge(D, FVector2D(RX - D.MeasureText(LocText, 8.f).X - 9.f, 46.f));
		}
		int32 LegTotal = 0;
		int32 LegCaught = 0;
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (!F.bLegendary) { continue; }
			++LegTotal;
			const FFVFishRecord* Rec = P->GetRecord(F.Id);
			if (Rec && Rec->Caught > 0) { ++LegCaught; }
		}
		if (LegTotal > 0)
		{
			D.Text(FString::Printf(TEXT("Легендарных рыб: %d / %d"), LegCaught, LegTotal), FVector2D(RX, 53.f), 6.5f, FVPal::InkSoft(), EFVAlign::Right, false, false);
		}
	}

	// ---- location tabs (animated underline follows the slide) ----
	{
		const float TabW = FMath::Min(100.f, FMath::FloorToFloat(592.f / float(NumLocs)) - 2.f);
		TArray<FString> Labels;
		for (const FFVLocationDef* LP : Locs)
		{
			// full name if it fits at >= 7pt (the tab shrinks its font itself), otherwise the leading words
			Labels.Add(FitWords(D, LP->Name, TabW - 5.f, 7.f));
		}
		const FFVRect TabRow(24.f, GridPanel.Y - 15.f, float(NumLocs) * TabW + float(NumLocs - 1) * 2.f, 16.f);
		FVKit::TabStrip(D, TabRow, Labels, LocPager);
		for (int32 i = 0; i < NumLocs; ++i)
		{
			const FFVLocationDef& L = *Locs[i];
			const FFVRect TR = FVDraw::SplitH(TabRow, i, NumLocs, 2.f);
			const int32 LocTotal = FVDB::FishAt(L.Id).Num();
			const int32 LocCaught = P->SpeciesCaughtAt(L.Id);
			if (!P->IsLocationUnlocked(L.Id))
			{
				MiniLock(D, FVector2D(TR.Right() - 9.f, TR.Y - 4.f));
			}
			else if (LocTotal > 0 && LocCaught >= LocTotal)
			{
				CheckBadge(D, FVector2D(TR.Right() - 4.f, TR.Y), 4.5f);
			}
			if (D.Hover(TR))
			{
				D.Tooltip(L.Name, FString::Printf(TEXT("Поймано видов: %d / %d%s"), LocCaught, LocTotal,
					P->IsLocationUnlocked(L.Id) ? TEXT("") : TEXT("\nЛокация ещё закрыта")));
			}
		}
	}

	// ---- the species grid of each location: swipe left / right to switch ----
	FGridMetrics M;
	M.SlotW = 72.f;
	M.ViewH = 180.f + 2.f * M.Pad;
	HoverLoc = INDEX_NONE;
	HoverIdx = INDEX_NONE;
	LocPager.DrawPages(D, PagesArea, [&](int32 Li) { DrawSpeciesPage(D, *P, Li, GridPanel, M); });
	FVKit::SwipeHint(D, FFVRect(GridPanel.X - 1.f, GridPanel.Y, GridPanel.W + 2.f, M.ViewH + 12.f), LocPager);

	// ---- detail (the hovered fish wins over the selected one) ----
	const int32 Sel = SelFish.IsValidIndex(Page) ? SelFish[Page] : 0;
	const int32 ShowIdx = (HoverLoc == Page && FishHere.IsValidIndex(HoverIdx)) ? HoverIdx : Sel;
	DrawDetailArea(D, *P, Page, FishHere, ShowIdx, DetailPanel);
}

void FCollectionScreen::DrawSpeciesPage(FVDraw& D, const UFVSaveGame& P, int32 Li, const FFVRect& Panel, const FGridMetrics& M)
{
	const TArray<const FFVLocationDef*> Locs = CollectionLocs();
	if (!Locs.IsValidIndex(Li) || !GridScroll.IsValidIndex(Li) || !SelFish.IsValidIndex(Li))
	{
		return;
	}
	const bool bP = FV::IsPortrait();
	const FFVLocationDef& Loc = *Locs[Li];
	const TArray<const FFVFishDef*> FishHere = FVDB::FishAt(Loc.Id);
	const int32 N = FishHere.Num();
	const int32 Caught = CountCaught(P, FishHere);
	const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(N, M.Cols));

	// Landscape: slots shrink a little for a location with many rows, then the grid scrolls.
	const float SlotH = bP ? M.SlotH
		: FMath::Clamp(FMath::FloorToFloat((M.ViewH - 2.f * M.Pad - float(Rows - 1) * M.Gap) / float(Rows)), 46.f, 58.f);
	const float Pitch = SlotH + M.Gap;
	const float ContentH = 2.f * M.Pad + float(Rows) * Pitch - M.Gap;
	const float AreaY = Panel.Y + (bP ? 4.f : 6.f);
	float ViewH = bP ? M.ViewH : FMath::Min(ContentH, M.ViewH);

	// Update 6.1: a location card whose description does not fit opens by a tap — the grid viewport above shrinks to
	// give it the room (at least one row of slots stays; the grid still scrolls).
	const float CardW = Panel.W - 16.f;
	const float ClosedCardH = bP ? M.CardH : Panel.Bottom() - 6.f - (AreaY + ViewH + 4.f);
	const float NeedCardH = LocCardNeedH(D, P, Loc, CardW);
	const bool bCardCut = ClosedCardH >= 24.f && NeedCardH > ClosedCardH + 0.5f;
	if (bAutoOpenCard && Li == LocPager.Page)
	{
		bAutoOpenCard = false;
		if (bCardCut) { OpenLocCard = Li; }
	}
	const bool bCardOpen = bCardCut && OpenLocCard == Li;
	if (bCardOpen)
	{
		const float MinView = FMath::Min(ViewH, SlotH + 2.f * M.Pad);
		ViewH = FMath::Max(MinView, ViewH - (NeedCardH - ClosedCardH));
	}
	const FFVRect Area(Panel.X + 2.f, AreaY, Panel.W - 4.f, ViewH);
	const float GridW = float(M.Cols) * M.SlotW + float(M.Cols - 1) * M.Gap;
	const float GX = FMath::RoundToFloat(Panel.X + (Panel.W - GridW) * 0.5f);

	FFVScrollView& Scroll = GridScroll[Li];
	Scroll.Update(D, Area, ContentH);
	int32& Sel = SelFish[Li];
	Sel = N > 0 ? FMath::Clamp(Sel, 0, N - 1) : 0;
	if (N == 0)
	{
		D.Text(TEXT("Здесь рыба пока не водится"), FVector2D(Panel.Center().X, Area.Y + 30.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
	}

	D.PushClip(Area);
	const float Y0 = Area.Y + M.Pad - Scroll.Offset();
	int32 NewSel = Sel;
	for (int32 i = 0; i < N; ++i)
	{
		const FFVFishDef* F = FishHere[i];
		if (!F) { continue; }
		const FFVRect SR(GX + float(i % M.Cols) * (M.SlotW + M.Gap), Y0 + float(i / M.Cols) * Pitch, M.SlotW, SlotH);
		if (SR.Bottom() < Area.Y || SR.Y > Area.Bottom()) { continue; }
		if (!bP && D.Hover(SR))
		{
			HoverLoc = Li;
			HoverIdx = i;
		}
		if (D.Slot(SR, i == Sel, true))   // a tap (never a scroll / swipe) selects
		{
			NewSel = i;
		}
		DrawGridSlot(D, P, *F, SR, i == Sel);
	}
	D.PopClip();
	Sel = NewSel;
	Scroll.DrawBar(D, Area);
	Scroll.DrawFades(D, Area, FVPal::Parchment());

	const FFVRect Card = bP
		? FFVRect(Panel.X + 8.f, Area.Bottom() + 8.f, CardW, M.CardH + (M.ViewH - ViewH))
		: FFVRect(Panel.X + 8.f, Area.Bottom() + 4.f, CardW, Panel.Bottom() - 6.f - (Area.Bottom() + 4.f));
	DrawLocationCard(D, P, Loc, Caught, N, Card, bCardCut, bCardOpen);
	if (bCardCut && D.Clicked(Card))   // a tap (never a swipe) opens / closes the card
	{
		OpenLocCard = bCardOpen ? INDEX_NONE : Li;
		PlayTapSfx();
	}
}

void FCollectionScreen::DrawDetailArea(FVDraw& D, const UFVSaveGame& P, int32 Page, const TArray<const FFVFishDef*>& FishHere, int32 ShowIdx, const FFVRect& Panel)
{
	const TArray<const FFVLocationDef*> Locs = CollectionLocs();
	const FFVLocationDef* Loc = Locs.IsValidIndex(Page) ? Locs[Page] : nullptr;
	const FFVFishDef* F = FishHere.IsValidIndex(ShowIdx) ? FishHere[ShowIdx] : nullptr;
	const FName Id = F ? F->Id : NAME_None;
	if (Id != ShownFish || Page != ShownLoc)
	{
		// Another fish: its full view and facts appear at once, the facts start at the top.
		ShownFish = Id;
		ShownLoc = Page;
		DetailScroll.Reset();
	}

	if (F)
	{
		DrawFishDetail(D, P, *F, Loc, Panel);
	}
	else
	{
		D.TextWrapped(FV::IsPortrait() ? TEXT("Нажмите на рыбу внизу, чтобы узнать о ней больше.") : TEXT("Наведите на рыбу, чтобы узнать о ней больше."),
			FVector2D(Panel.X + 12.f, Panel.Y + 30.f), Panel.W - 24.f, FV::Ui::Body(), FVPal::InkSoft(), false);
	}
}

void FCollectionScreen::DrawLocationChips(FVDraw& D, const UFVSaveGame& P, const FFVRect& Row)
{
	const TArray<const FFVLocationDef*> Locs = CollectionLocs();
	const int32 NumLocs = Locs.Num();
	const float ChipW = FMath::FloorToFloat(FMath::Min(124.f, Row.W * 0.37f));
	const float Pitch = ChipW + 6.f;
	const float V = LocPager.VisualPos();   // follows the finger while the species page is swiped
	const float CX = Row.Center().X;
	D.PushClip(FFVRect(Row.X, Row.Y - 4.f, Row.W, Row.H + 6.f));
	for (int32 i = 0; i < NumLocs; ++i)
	{
		const FFVLocationDef& LD = *Locs[i];
		const FFVRect BR(FMath::RoundToFloat(CX + (float(i) - V) * Pitch - ChipW * 0.5f), Row.Y, ChipW, Row.H - 2.f);
		if (BR.Right() < Row.X || BR.X > Row.Right())
		{
			continue;
		}
		const bool bActive = i == LocPager.Page;
		if (D.Button(BR, LD.Name, bActive ? EFVButton::Primary : EFVButton::Normal, true, 8.5f) && !bActive)
		{
			LocPager.GoTo(i);   // slides the pages (and this row) to that location
		}
		const int32 LocTotal = FVDB::FishAt(LD.Id).Num();
		if (!P.IsLocationUnlocked(LD.Id))
		{
			MiniLock(D, FVector2D(BR.Right() - 9.f, BR.Y - 3.f));
		}
		else if (LocTotal > 0 && P.SpeciesCaughtAt(LD.Id) >= LocTotal)
		{
			CheckBadge(D, FVector2D(BR.Right() - 3.f, BR.Y + 1.f), 4.5f);
		}
	}
	D.PopClip();
	// the neighbours peek in from the sides and fade out: there is more to swipe to
	FadeH(D, FFVRect(Row.X, Row.Y - 1.f, 16.f, Row.H + 1.f), FVPal::Parchment(), true);
	FadeH(D, FFVRect(Row.Right() - 16.f, Row.Y - 1.f, 16.f, Row.H + 1.f), FVPal::Parchment(), false);
}

void FCollectionScreen::DrawPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& Win)
{
	const TArray<const FFVLocationDef*> Locs = CollectionLocs();
	const int32 NumLocs = Locs.Num();
	const float L = Win.X + 10.f;
	const float CW = Win.W - 20.f;
	const float R = L + CW;
	const float Top = Win.Y + 14.f;
	const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;
	const bool bTall = FV::VH >= 720.f;

	// ---- header: total progress + legendary fish ----
	ProgressHeaderAt(D, FVector2D(L, Top), 150.f, EFVIcon::Book, TEXT("Поймано видов"), P.SpeciesCaught(), FVDB::Fish().Num(), FVPal::Green());
	{
		int32 LegTotal = 0;
		int32 LegCaught = 0;
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (!F.bLegendary) { continue; }
			++LegTotal;
			const FFVFishRecord* Rec = P.GetRecord(F.Id);
			if (Rec && Rec->Caught > 0) { ++LegCaught; }
		}
		if (LegTotal > 0)
		{
			D.Text(TEXT("Легендарных"), FVector2D(R, Top), FV::Ui::Small(), FVPal::InkSoft(), EFVAlign::Right, false, false);
			D.Text(FString::Printf(TEXT("%d / %d"), LegCaught, LegTotal), FVector2D(R, Top + 11.f), FV::Ui::Body(),
				LegCaught > 0 ? FV::Col(196, 136, 20) : FVPal::Ink(), EFVAlign::Right, false, true);
		}
	}

	// ---- layout, bottom-up: the species panel and the location row live in the thumb zone ----
	FGridMetrics M;
	M.Cols = 4;
	M.SlotW = FMath::FloorToFloat((CW - 16.f - M.Gap * float(M.Cols - 1)) / float(M.Cols));
	M.SlotH = FV::VH >= 760.f ? 70.f : 58.f;
	int32 MaxRows = 1;
	for (const FFVLocationDef* LDP : Locs)
	{
		MaxRows = FMath::Max(MaxRows, FMath::DivideAndRoundUp(FVDB::FishAt(LDP->Id).Num(), M.Cols));
	}
	const int32 VisRows = FMath::Min(MaxRows, bTall ? 3 : 2);   // more rows scroll
	M.ViewH = 2.f * M.Pad + float(VisRows) * (M.SlotH + M.Gap) - M.Gap;
	M.CardH = bTall ? 58.f : 36.f;   // short phones: name + progress only
	const float SpeciesH = 4.f + M.ViewH + 8.f + M.CardH + 6.f;
	const FFVRect SpeciesPanel(L, Bottom - SpeciesH, CW, SpeciesH);
	const FFVRect ChipRow(L, SpeciesPanel.Y - 6.f - 30.f, CW, 30.f);
	const float DetailY = Top + 30.f;
	const FFVRect DetailPanel(L, DetailY, CW, FMath::Max(120.f, ChipRow.Y - 6.f - DetailY));
	D.Panel(DetailPanel, EFVPanel::Parchment);
	D.Panel(SpeciesPanel, EFVPanel::Parchment);

	// ---- locations: swiping the species panel (or the location row) slides to the next / previous place ----
	const FFVRect PagesArea = SpeciesPanel.Inset(2.f);
	LocPager.Update(D, FFVRect(PagesArea.X, ChipRow.Y, PagesArea.W, PagesArea.Bottom() - ChipRow.Y), NumLocs);
	const int32 Page = FMath::Clamp(LocPager.Page, 0, NumLocs - 1);
	DrawLocationChips(D, P, ChipRow);
	LocPager.DrawPages(D, PagesArea, [&](int32 Li) { DrawSpeciesPage(D, P, Li, SpeciesPanel, M); });
	FVKit::SwipeHint(D, FFVRect(SpeciesPanel.X - 1.f, SpeciesPanel.Y, SpeciesPanel.W + 2.f, M.ViewH + 8.f), LocPager);

	// ---- the selected fish on top: its full view and everything about it ----
	const TArray<const FFVFishDef*> FishHere = FVDB::FishAt(Locs[Page]->Id);
	const int32 N = FishHere.Num();
	int32 Sel = SelFish.IsValidIndex(Page) ? FMath::Clamp(SelFish[Page], 0, FMath::Max(0, N - 1)) : 0;
	DrawDetailArea(D, P, Page, FishHere, Sel, DetailPanel);

	// ---- bottom bar: back + step through the fish of this place with the thumb ----
	FFVRect Actions;
	if (D.ActionBar(Win, &Actions))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	if (N > 1)
	{
		const float AW = 50.f;
		const FFVRect Prev(Actions.X, Actions.Y, AW, Actions.H);
		const FFVRect Next(Actions.Right() - AW, Actions.Y, AW, Actions.H);
		int32 NewSel = Sel;
		if (D.Button(Prev, FString(), EFVButton::Normal, true, BarTextSize()))
		{
			NewSel = (Sel - 1 + N) % N;
		}
		Triangle(D, Prev.Center() + FVector2D(0.f, -0.5f), 2, 7, FVPal::Ink());
		if (D.Button(Next, FString(), EFVButton::Normal, true, BarTextSize()))
		{
			NewSel = (Sel + 1) % N;
		}
		Triangle(D, Next.Center() + FVector2D(1.f, -0.5f), 3, 7, FVPal::Ink());
		if (NewSel != Sel && SelFish.IsValidIndex(Page) && GridScroll.IsValidIndex(Page))
		{
			Sel = NewSel;
			SelFish[Page] = Sel;
			// keep the picked slot visible in the grid (animated)
			GridScroll[Page].ScrollToShow(float(Sel / M.Cols) * (M.SlotH + M.Gap), M.SlotH + 2.f * M.Pad);
		}
		const FString Label = FString::Printf(TEXT("Рыба %d из %d"), Sel + 1, N);
		const float TH = D.MeasureText(Label, FV::Ui::Body()).Y;
		D.Text(FitText(D, Label, Actions.W - 2.f * AW - 8.f, FV::Ui::Body()), FVector2D(Actions.Center().X, FMath::RoundToFloat(Actions.Center().Y - TH * 0.5f)),
			FV::Ui::Body(), FVPal::Ink(), EFVAlign::Center, true, true);
	}
}

void FCollectionScreen::DrawGridSlot(FVDraw& D, const UFVSaveGame& P, const FFVFishDef& F, const FFVRect& SR, bool bSelected)
{
	const FFVFishRecord* Rec = P.GetRecord(F.Id);
	const bool bCaught = Rec && Rec->Caught > 0;
	UFVSprites* Sprites = UFVSprites::Get();

	if (bCaught && F.bLegendary)
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(float(D.Time) * 3.f);
		D.Frame(SR.Inset(2.f), FV::Col(255, 204, 51, uint8(150.f + 100.f * Pulse)), 1.f);
	}
	if (F.bExpert && P.IsFishStudied(F.Id))
	{
		// update 6: «Изучено» — every fact of the fish is known (golden double frame + a small star)
		D.Frame(SR.Inset(1.f), FV::Col(206, 150, 34), 2.f);
		D.Frame(SR.Inset(3.f), FV::Col(255, 226, 120, 170), 1.f);
		D.Circle(FVector2D(SR.Right() - 6.f, SR.Bottom() - 6.f), 3.5f, FV::Col(255, 204, 51));
		D.Circle(FVector2D(SR.Right() - 6.f, SR.Bottom() - 6.f), 1.5f, FV::Col(255, 250, 220));
	}

	const bool bP = FV::IsPortrait();
	const float NameSize = bP ? FV::Ui::Small() : 6.f;
	const float NameH = SR.H >= 40.f ? (bP ? 13.f : 10.f) : 0.f;
	const FVector2D SpriteC(SR.Center().X, SR.Y + (SR.H - NameH) * 0.5f + 1.f);
	if (Sprites)
	{
		const FFVSprite Spr = bCaught ? Sprites->Fish(F) : Sprites->FishSilhouette(F);
		float Scale = 2.f;
		if (Spr.W * Scale > SR.W - 6.f || Spr.H * Scale > SR.H - NameH - 6.f) { Scale = 1.f; }
		const float Bob = (bCaught && bSelected) ? FMath::RoundToFloat(FMath::Sin(float(D.Time) * 4.f)) : 0.f;
		D.SpriteCentered(Spr, SpriteC + FVector2D(0.f, Bob), Scale);
	}
	if (NameH > 0.f)
	{
		const FString Name = bCaught ? F.Name : FString(TEXT("???"));
		D.Text(FitText(D, Name, SR.W - 6.f, NameSize), FVector2D(SR.Center().X, SR.Bottom() - NameH), NameSize,
			bCaught ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Center, false, true);
	}
	if (bCaught)
	{
		D.Stars(FVector2D(SR.X + 3.f, SR.Y + 3.f), EFVQuality(FMath::Clamp<int32>(Rec->BestQuality, 0, int32(EFVQuality::Count) - 1)), 1.f);
		D.Text(FString::Printf(TEXT("×%d"), Rec->Caught), FVector2D(SR.Right() - 4.f, SR.Y + 3.f), bP ? 6.5f : 6.f, FVPal::InkSoft(), EFVAlign::Right, false, true);
	}
}

float FCollectionScreen::LocCardNeedH(const FVDraw& D, const UFVSaveGame& P, const FFVLocationDef& Loc, float W)
{
	// name row (+ the unlock requirement), the description, 2 px, the «Поймано» bar row (11 px)
	const float DescY = P.IsLocationUnlocked(Loc.Id) ? 14.f : 25.f;
	const float DescH = FV::IsPortrait()
		? float(WrapAllLines(D, Loc.Description, W, 7.f).Num()) * WrapStep(D, 7.f)
		: D.MeasureWrapped(Loc.Description, W, 7.f, false);
	return DescY + DescH + 2.f + 11.f;
}

void FCollectionScreen::DrawLocationCard(FVDraw& D, const UFVSaveGame& P, const FFVLocationDef& Loc, int32 Caught, int32 Total, const FFVRect& R, bool bCut, bool bOpen)
{
	if (R.H < 24.f)
	{
		return;
	}
	D.Rect(FFVRect(R.X, R.Y - 3.f, R.W, 1.f), Divider());
	const bool bUnlocked = P.IsLocationUnlocked(Loc.Id);
	const float NameS = FV::IsPortrait() ? FV::Ui::Body() : 9.f;
	const FString Status = bUnlocked ? TEXT("Открыта") : TEXT("Закрыта");
	const float StatusW = D.MeasureText(Status, 7.f).X;
	// update 6.1: «ещё ▾» left of the status when the description is cut (a tap opens the card), «▴» while open
	const float HintW = bCut ? OpenHint(D, R.Right() - StatusW - (bUnlocked ? 4.f : 14.f), R.Y + 6.f, bOpen, 6.5f, true) + 4.f : 0.f;
	D.Text(FitText(D, Loc.Name, R.W - 80.f - HintW, NameS), FVector2D(R.X, R.Y + 1.f), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
	D.Text(Status, FVector2D(R.Right(), R.Y + 2.f), 7.f, bUnlocked ? InkGood() : InkBad(), EFVAlign::Right, false, true);
	if (!bUnlocked)
	{
		MiniLock(D, FVector2D(R.Right() - StatusW - 11.f, R.Y + 1.f));
	}

	const float BarY = R.Bottom() - 11.f;
	float Y = R.Y + 14.f;
	if (!bUnlocked)
	{
		const FString Req = Loc.UnlockPrice > 0
			? FString::Printf(TEXT("Откройте на карте: уровень %d, %s монет"), Loc.MinLevel, *FV::Money(Loc.UnlockPrice))
			: FString::Printf(TEXT("Откройте на карте: уровень %d"), Loc.MinLevel);
		D.Text(FitText(D, Req, R.W, 7.f, false), FVector2D(R.X, Y), 7.f, InkBad(), EFVAlign::Left, false, false);
		Y += 11.f;
	}
	// the description: all of it when it fits (an opened card has the room), else as many whole lines as fit above
	// the progress bar (the last one ends with «…»; the hint in the name row says a tap shows the rest)
	const float Step = FMath::RoundToFloat(D.LineHeight(7.f, false) * 0.95f);
	const int32 MaxLines = FMath::FloorToInt((BarY - 2.f - Y) / FMath::Max(1.f, Step));
	if (FV::IsPortrait() || LocCardNeedH(D, P, Loc, R.W) > R.H + 0.5f)
	{
		TextWrappedLimited(D, Loc.Description, FVector2D(R.X, Y), R.W, 7.f, FVPal::InkSoft(), MaxLines, false);
	}
	else
	{
		D.TextWrapped(Loc.Description, FVector2D(R.X, Y), R.W, 7.f, FVPal::InkSoft(), false);
	}

	const bool bComplete = Total > 0 && Caught >= Total;
	const float Frac = Total > 0 ? float(Caught) / float(Total) : 0.f;
	D.Text(FString::Printf(TEXT("Поймано: %d / %d"), Caught, Total), FVector2D(R.X, BarY - 1.f), 7.f, FVPal::Ink(), EFVAlign::Left, false, true);
	D.Bar(FFVRect(R.X + 82.f, BarY, R.W - 82.f - (bComplete ? 16.f : 0.f), 7.f), Frac, bComplete ? FVPal::Gold() : FVPal::Green(), FV::Col(120, 84, 60), true);
	if (bComplete)
	{
		CheckBadge(D, FVector2D(R.Right() - 6.f, BarY + 3.5f));
		if (D.Hover(FFVRect(R.X, BarY - 2.f, R.W, 11.f)))
		{
			D.Tooltip(TEXT("Все виды пойманы!"), FString::Printf(TEXT("Вы поймали каждую рыбу, что водится в месте «%s»."), *Loc.Name));
		}
	}
}

void FCollectionScreen::DrawFishDetail(FVDraw& D, const UFVSaveGame& P, const FFVFishDef& F, const FFVLocationDef* Loc, const FFVRect& Panel)
{
	const FFVFishRecord* Rec = P.GetRecord(F.Id);
	const bool bCaught = Rec && Rec->Caught > 0;
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	const float T = float(FMath::Fmod(D.Time, 10000.0));
	UFVSprites* Sprites = UFVSprites::Get();
	// Portrait: the detail panel is wide and tall -> bigger aquarium, bigger text, roomier rows.
	const bool bP = FV::IsPortrait();
	const float TxtS = bP ? 8.f : 7.f;
	const float LabelW = bP ? 104.f : 80.f;
	const float RowStep = bP ? 13.f + FMath::Clamp((Panel.H - 330.f) / 40.f, 0.f, 1.5f) : 10.5f;
	const float NameStep = bP ? 18.f : 16.f;
	const FString Intro(TEXT("Эта рыба вам ещё не попадалась. Вот что о ней рассказывают рыбаки:"));

	// Update 5: a legend shows «Подготовка» (the live checklist of the equipped rod + current bait, then PrepText) and
	// «Приёмы» — the fight text only after the first meeting (LegendsMet) or a catch (owner's decision).
	const bool bLegend = F.Legend != EFVLegend::None && !F.bExpert;
	const TArray<FFVLegendCheckItem> Check = (bLegend && Game) ? FVDB::LegendChecklist(F, Game->MakeCatchContext(0.f)) : TArray<FFVLegendCheckItem>();
	const bool bMet = bLegend && P.HasMetLegend(F.Id);
	const FString FightLocked(TEXT("Приёмы откроются после первой встречи."));
	// hotfix 061: a long checklist label wraps under itself — its extra lines (WrapStep apart) are part of the height
	const float CheckS = FMath::Min(RowStep, WrapStep(D, TxtS, true));
	int32 CheckMore = 0;
	for (const FFVLegendCheckItem& It : Check)
	{
		CheckMore += FMath::Max(0, CheckRowLines(D, It, W - LabelW, TxtS, RowStep).Num() - 1);
	}
	const float LegendH = bLegend
		? 2.f + RowStep * float(FMath::Max(1, Check.Num())) + float(CheckMore) * CheckS + D.MeasureWrapped(F.Prep.PrepText, W, TxtS, false) + 3.f
			+ (bMet ? RowStep + D.MeasureWrapped(F.Prep.FightText, W, TxtS, false) : D.MeasureWrapped(FightLocked, W, TxtS, false)) + 3.f
		: 0.f;

	// Update 6 (§10.16, Р33): expert fish and legends hide their conditions — one «???» row per fact until it is learnt
	// (City tasks, treasure pages; the rod types also by a snap on a classic rod). All facts known = «Изучено».
	const FFVExpertFishDef* Ex = F.bExpert ? FVDBU6::FindExpertFish(F.Id) : nullptr;
	const EFVExpertLegend ExLegend = F.ExpertLegend != EFVExpertLegend::None ? F.ExpertLegend : (Ex ? Ex->Legend : EFVExpertLegend::None);
	const TArray<FFVExpertMove>* Score = ExLegend != EFVExpertLegend::None ? &FVExpertLegends::Score(ExLegend) : nullptr;
	const bool bStudied = Ex && Ex->Facts.Num() > 0 && P.IsFishStudied(F.Id);
	const int32 ExRows = Ex ? 1 + FMath::Max(1, Ex->Facts.Num()) + (Score ? 2 + Score->Num() : 0) : 0;
	const float ExpertH = Ex ? 2.f + float(ExRows) * RowStep + 3.f : 0.f;
	// expert fish: the classic Время / Погода / Глубина rows are replaced by the facts
	const int32 ClassicRowsCaught = Ex ? 5 : 8;
	const int32 ClassicRowsUnknown = Ex ? 1 : (bLegend ? 5 : 3);

	// Update 6.1: a long value (time / weather lists, the expert conditions) wraps under itself instead of being cut —
	// the values of the rows are made here once, their extra lines are part of the text height.
	const float ValW = W - LabelW;
	const float WrapS = FMath::Min(RowStep, WrapStep(D, TxtS, true));
	auto MoreLines = [&D, TxtS](const FString& V, float MaxW) { return FMath::Max(0, WrapAllLines(D, V, MaxW, TxtS, true).Num() - 1); };
	const FString LocV = Loc ? Loc->Name : FString(TEXT("?"));
	const FString TimeV = FVDB::TimeMaskText(F.TimeMask);
	const FString WeatherV = FVDB::WeatherReqText(F.Weather);
	const FString DepthV = DepthText(F.MinDepth);
	const FString LevelV = FString::Printf(TEXT("с %d-го"), F.MinLevel);
	const FString DiffV = FString::Printf(TEXT("%s (%d)"), *FVDB::DifficultyText(F.Difficulty), F.Difficulty);
	const FString MotionV = FVDB::MotionName(F.Motion);
	const FString CaughtV = bCaught ? FString::Printf(TEXT("%d %s"), Rec->Caught, Plural(Rec->Caught, TEXT("раз"), TEXT("раза"), TEXT("раз"))) : FString();
	const FString RecordV = bCaught ? FString::Printf(TEXT("%s (от %s до %s)"), *Cm(Rec->MaxSize), *Cm(F.MinSize), *Cm(F.MaxSize)) : FString();
	int32 WrapRows = 0;
	if (bCaught)
	{
		WrapRows += MoreLines(DiffV, ValW) + MoreLines(MotionV, ValW) + MoreLines(CaughtV, ValW) + MoreLines(RecordV, ValW);
		if (!Ex) { WrapRows += MoreLines(TimeV, ValW) + MoreLines(WeatherV, ValW) + MoreLines(DepthV, ValW); }
	}
	else
	{
		WrapRows += MoreLines(LocV, ValW);
		if (!Ex) { WrapRows += MoreLines(TimeV, ValW) + MoreLines(WeatherV, ValW); }
		if (bLegend && !Ex) { WrapRows += MoreLines(DepthV, ValW) + MoreLines(LevelV, ValW); }
	}
	for (int32 i = 0; Ex && i < Ex->Facts.Num(); ++i)
	{
		if (P.KnowsFact(F.Id, i)) { WrapRows += MoreLines(Ex->Facts[i].Row, W - 12.f); }
	}
	const float WrapH = float(WrapRows) * WrapS;

	// Height of the text block under the aquarium (mirrors the drawing code below).
	const float TextH = WrapH + (bCaught
		? NameStep + D.MeasureWrapped(F.Description, W, TxtS, false) + 4.f + (bP ? RowStep + 1.f : 12.f) + float(ClassicRowsCaught) * RowStep + LegendH + ExpertH
		: NameStep + D.MeasureWrapped(Intro, W, TxtS, false) + 5.f + float(ClassicRowsUnknown) * RowStep + LegendH + ExpertH);

	// ---- the full view: a little aquarium with the location's water colours ----
	// Portrait: the tank takes the room the text does not need (never less than ~40 % of the panel); the text scrolls.
	float TankH = 70.f;
	if (bP)
	{
		const float MinTank = FMath::Clamp(FMath::FloorToFloat(Panel.H * 0.42f), 70.f, 130.f);
		TankH = FMath::Clamp(FMath::FloorToFloat(Panel.H - 7.f - 8.f - TextH - 6.f), MinTank, 210.f);
	}
	const FFVRect Tank(X0, Panel.Y + 7.f, W, TankH);
	const FLinearColor Shallow = Loc ? FV::Col(Loc->WaterShallow) : FV::Col(90, 170, 220);
	const FLinearColor Deep = Loc ? FV::Col(Loc->WaterDeep) : FV::Col(30, 80, 150);
	const int32 Bands = 7;
	for (int32 b = 0; b < Bands; ++b)
	{
		const float K = float(b) / float(Bands - 1);
		D.Rect(FFVRect(Tank.X, Tank.Y + Tank.H * float(b) / float(Bands), Tank.W, Tank.H / float(Bands) + 1.f), FMath::Lerp(Shallow, Deep, K));
	}
	for (int32 r = 0; r < 3; ++r)   // light rays
	{
		const float RX = Tank.X + 30.f + float(r) * 70.f + FMath::Sin(T * 0.6f + float(r)) * 6.f;
		D.Line(FVector2D(RX, Tank.Y), FVector2D(RX - 18.f, Tank.Bottom() - 8.f), FV::Col(255, 255, 255, 26), 6.f);
	}
	D.Rect(FFVRect(Tank.X, Tank.Bottom() - 7.f, Tank.W, 7.f), FV::Col(214, 184, 120));
	D.Rect(FFVRect(Tank.X, Tank.Bottom() - 7.f, Tank.W, 1.f), FV::Col(236, 210, 150));
	for (int32 w = 0; w < 3; ++w)  // weeds
	{
		const float WX = Tank.X + 14.f + float(w) * 97.f;
		for (int32 s = 0; s < 5; ++s)
		{
			const float Sway = FMath::Sin(T * 1.5f + float(s) * 0.7f + float(w)) * float(s) * 0.6f;
			D.Rect(FFVRect(FMath::FloorToFloat(WX + Sway), Tank.Bottom() - 10.f - float(s) * 4.f, 2.f, 4.f), FV::Col(70, 150, 70));
		}
	}
	for (int32 b = 0; b < 5; ++b)  // bubbles
	{
		const float Ph = FMath::Frac(T * 0.25f + float(b) * 0.21f);
		const FVector2D BP(Tank.X + 20.f + float((b * 47) % int32(FMath::Max(20.f, Tank.W - 40.f))) + FMath::Sin(Ph * 12.f) * 2.f,
			Tank.Bottom() - 8.f - Ph * (Tank.H - 10.f));
		D.Circle(BP, 1.2f + float(b % 2) * 0.6f, FV::Col(230, 245, 255, uint8(160.f * (1.f - Ph))));
	}
	if (Sprites)
	{
		const FFVSprite Spr = bCaught ? Sprites->Fish(F) : Sprites->FishSilhouette(F);
		float Scale = 3.f;
		if (bP && Spr.W * 5.f <= Tank.W - 60.f && Spr.H * 5.f <= Tank.H - 30.f) { Scale = 5.f; }
		else if (bP && Spr.W * 4.f <= Tank.W - 40.f && Spr.H * 4.f <= Tank.H - 24.f) { Scale = 4.f; }
		if (Spr.W * Scale > Tank.W - 12.f || Spr.H * Scale > Tank.H - 14.f) { Scale = 2.f; }
		const FVector2D FC(Tank.Center().X + FMath::RoundToFloat(FMath::Sin(T * 0.7f) * 8.f), Tank.Center().Y - 3.f + FMath::RoundToFloat(FMath::Sin(T * 2.f) * 2.f));
		D.SpriteCentered(Spr, FC, Scale);
		if (!bCaught)
		{
			const float Pulse = 0.6f + 0.4f * FMath::Sin(T * 3.f);
			D.TextOutlined(TEXT("?"), FVector2D(FC.X, FC.Y - 10.f), 16.f, FV::Col(255, 255, 255, uint8(255.f * Pulse)), EFVAlign::Center);
		}
	}
	D.Frame(Tank, FVPal::WoodDark());
	if (bStudied)
	{
		// «Изучено»: a golden frame with a slow shine and a ribbon in the corner of the aquarium
		const float Pulse = 0.5f + 0.5f * FMath::Sin(T * 2.2f);
		D.Frame(Tank.Inset(-1.f), FV::Col(255, 204, 51, uint8(190.f + 60.f * Pulse)), 2.f);
		D.Frame(Tank.Inset(2.f), FV::Col(255, 236, 150, 120), 1.f);
		const FString Lbl(TEXT("Изучено"));
		const float LS = bP ? 7.5f : 6.5f;
		const float LW = D.MeasureText(Lbl, LS).X + 10.f;
		const FFVRect Rib(Tank.Right() - LW - 3.f, Tank.Y + 3.f, LW, LS + 5.f);
		D.Rect(Rib, FV::Col(206, 150, 34));
		D.Frame(Rib, FV::Col(120, 80, 20));
		D.TextOutlined(Lbl, FVector2D(Rib.Center().X, Rib.Y + 2.f), LS, FV::Col(255, 248, 220), EFVAlign::Center, FV::Col(90, 56, 14));
	}

	// ---- the facts: scroll (finger / wheel) when they do not fit under the aquarium ----
	const FFVRect TextArea(Panel.X + 2.f, Tank.Bottom() + 2.f, Panel.W - 4.f, FMath::Max(12.f, Panel.Bottom() - 3.f - (Tank.Bottom() + 2.f)));
	DetailScroll.Update(D, TextArea, 3.f + TextH + 6.f);
	D.PushClip(TextArea);
	float Y = TextArea.Y + 3.f - DetailScroll.Offset();
	auto InfoRow = [&D, &Y, X0, TxtS, LabelW, RowStep, ValW, WrapS](const FString& Label, const FString& Value, const FLinearColor& C)
	{
		D.Text(Label, FVector2D(X0, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		const TArray<FString> Lines = WrapAllLines(D, Value, ValW, TxtS, true);   // update 6.1: wraps, never cut (WrapH)
		DrawLines(D, Lines, FVector2D(X0 + LabelW, Y), ValW, TxtS, C, true, WrapS);
		Y += RowStep + float(FMath::Max(0, Lines.Num() - 1)) * WrapS;
	};
	// update 5: the legend block (its height is LegendH above); long texts are paragraphs, not InfoRows (FitText cuts those)
	auto LegendBlock = [&]()
	{
		Y += 2.f;
		D.Text(TEXT("Подготовка"), FVector2D(X0, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		if (Check.Num() == 0)
		{
			D.Text(TEXT("не нужна"), FVector2D(X0 + LabelW, Y), TxtS, InkGood(), EFVAlign::Left, false, true);
			Y += RowStep;
		}
		for (const FFVLegendCheckItem& It : Check)
		{
			const int32 NL = CheckRow(D, It, FVector2D(X0 + LabelW, Y), W - LabelW, TxtS, RowStep, CheckS);
			Y += RowStep + float(NL - 1) * CheckS;
		}
		Y += D.TextWrapped(F.Prep.PrepText, FVector2D(X0, Y), W, TxtS, FVPal::Ink(), false) + 3.f;
		if (bMet)
		{
			D.Text(TEXT("Приёмы"), FVector2D(X0, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			Y += RowStep;
			Y += D.TextWrapped(F.Prep.FightText, FVector2D(X0, Y), W, TxtS, FVPal::Ink(), false) + 3.f;
		}
		else
		{
			Y += D.TextWrapped(FightLocked, FVector2D(X0, Y), W, TxtS, FVPal::InkSoft(), false) + 3.f;
		}
	};

	auto ExpertBlock = [&]()
	{
		if (!Ex) { return; }
		Y += 2.f;
		D.Text(bStudied ? FString(TEXT("Условия (изучено)")) : FString::Printf(TEXT("Условия: узнано %d из %d"), P.KnownFactCount(F.Id), Ex->Facts.Num()),
			FVector2D(X0, Y), TxtS, bStudied ? FV::Col(170, 116, 10) : FVPal::InkSoft(), EFVAlign::Left, false, true);
		Y += RowStep;
		if (Ex->Facts.Num() == 0)
		{
			D.Text(TEXT("—"), FVector2D(X0 + 6.f, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			Y += RowStep;
		}
		for (int32 i = 0; i < Ex->Facts.Num(); ++i)
		{
			const bool bKnown = P.KnowsFact(F.Id, i);
			D.Text(bKnown ? TEXT("•") : TEXT("?"), FVector2D(X0 + 2.f, Y), TxtS, bKnown ? InkGood() : FVPal::InkSoft(), EFVAlign::Left, false, true);
			if (bKnown)
			{
				// update 6.1: a long condition («Клюёт на: …», «Поплавок: …, катушка: …») wraps (WrapH), never cut
				const TArray<FString> Lines = WrapAllLines(D, Ex->Facts[i].Row, W - 12.f, TxtS, true);
				DrawLines(D, Lines, FVector2D(X0 + 10.f, Y), W - 12.f, TxtS, FVPal::Ink(), true, WrapS);
				Y += float(FMath::Max(0, Lines.Num() - 1)) * WrapS;
			}
			else
			{
				D.Text(TEXT("???"), FVector2D(X0 + 10.f, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			Y += RowStep;
		}
		if (Score)
		{
			const int32 NumMoves = Score->Num();
			const int32* Best = P.U6.ExpertBestMove.Find(F.Id);
			const int32 BestN = Best ? FMath::Clamp(*Best, 0, NumMoves) : 0;
			D.Text(TEXT("Приёмы"), FVector2D(X0, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			Y += RowStep;
			for (int32 m = 0; m < NumMoves; ++m)
			{
				const bool bSeen = P.HasSeenExpertMove(F.Id, m);
				const FString Nm = bSeen && !(*Score)[m].Name.IsEmpty() ? (*Score)[m].Name : FString(TEXT("???"));
				D.Text(FitText(D, FString::Printf(TEXT("%d. %s"), m + 1, *Nm), W - 10.f, TxtS, bSeen), FVector2D(X0 + 6.f, Y), TxtS,
					bSeen ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Left, false, bSeen);
				Y += RowStep;
			}
			const FString BestText = BestN > 0 ? FString::Printf(TEXT("Лучший результат: приём %d из %d"), BestN, NumMoves)
				: FString(TEXT("Лучший результат: ещё не встречалась"));
			D.Text(FitText(D, BestText, W, TxtS), FVector2D(X0, Y), TxtS, BestN >= NumMoves && NumMoves > 0 ? InkGood() : FVPal::Ink(), EFVAlign::Left, false, true);
			Y += RowStep;
		}
		Y += 3.f;
	};

	if (!bCaught)
	{
		D.TextOutlined(TEXT("???"), FVector2D(X0, Y), bP ? 12.f : 11.f, FV::Col(200, 190, 180), EFVAlign::Left, FV::Col(60, 40, 30));
		Y += NameStep;
		Y += D.TextWrapped(Intro, FVector2D(X0, Y), W, TxtS, FVPal::InkSoft(), false) + 5.f;
		InfoRow(TEXT("Где водится"), LocV, FVPal::Ink());
		if (Ex)
		{
			ExpertBlock();
		}
		else
		{
			InfoRow(TEXT("Время"), TimeV, FVPal::Ink());
			InfoRow(TEXT("Погода"), WeatherV, FVPal::Ink());
		}
		if (bLegend && !Ex)
		{
			InfoRow(TEXT("Глубина"), DepthV, FVPal::Ink());
			InfoRow(TEXT("Уровень"), LevelV, P.GetLevel() >= F.MinLevel ? FVPal::Ink() : InkBad());
			LegendBlock();
		}
	}
	else
	{
		// ---- name, rarity ----
		const FLinearColor RC = RarityInk(F.Rarity);
		const float NameS = bP ? FV::Ui::Header() : 11.f;
		const float PillS = bP ? 7.f : 6.f;
		const FString Rarity = FVDB::RarityName(F.Rarity);
		const float PW = D.MeasureText(Rarity, PillS).X + 10.f;
		D.TextOutlined(FitText(D, F.Name, bP ? W - PW - 8.f : W, NameS), FVector2D(X0, Y), NameS, RC, EFVAlign::Left, FV::Col(52, 30, 22));
		{
			const FFVRect Pill(X0 + W - PW, Y + 2.f, PW, bP ? 14.f : 11.f);
			D.Rect(Pill, RC);
			D.Frame(Pill, FV::Col(52, 30, 22, 200));
			D.TextOutlined(Rarity, FVector2D(Pill.Center().X, Pill.Y + 2.f), PillS, FVPal::White(), EFVAlign::Center, FV::Col(40, 24, 18));
		}
		Y += NameStep;
		Y += D.TextWrapped(F.Description, FVector2D(X0, Y), W, TxtS, FVPal::Ink(), false) + 4.f;

		// ---- prices per quality ----
		D.Text(TEXT("Цена"), FVector2D(X0, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		{
			const float PriceX = bP ? 34.f : 30.f;
			const float ColW = (W - PriceX) / 4.f;
			for (int32 q = 0; q < int32(EFVQuality::Count); ++q)
			{
				const EFVQuality Q = EFVQuality(q);
				const float CX = X0 + PriceX + float(q) * ColW;
				float TX = CX;
				if (Q != EFVQuality::Normal)
				{
					D.Stars(FVector2D(CX, Y + (bP ? 1.f : 0.f)), Q, 1.f);
					TX += 9.f;
				}
				D.MoneyLabel(FVector2D(TX, Y), FVDB::FishPrice(F, Q), bP ? 8.f : 6.5f, EFVAlign::Left, FVPal::Ink());
			}
		}
		Y += bP ? RowStep + 1.f : 12.f;

		InfoRow(TEXT("Сложность"), DiffV, F.Difficulty >= 80 ? InkBad() : FVPal::Ink());
		InfoRow(TEXT("Повадки"), MotionV, FVPal::Ink());
		if (Ex)
		{
			ExpertBlock();   // Р33: a catch does not reveal the conditions
		}
		else
		{
			InfoRow(TEXT("Время"), TimeV, FVPal::Ink());
			InfoRow(TEXT("Погода"), WeatherV, FVPal::Ink());
			InfoRow(TEXT("Глубина"), DepthV, FVPal::Ink());
			if (bLegend) { LegendBlock(); }
		}
		InfoRow(TEXT("Поймано"), CaughtV, FVPal::Ink());
		InfoRow(TEXT("Рекорд"), RecordV, FVPal::Ink());

		const EFVQuality Best = EFVQuality(FMath::Clamp<int32>(Rec->BestQuality, 0, int32(EFVQuality::Count) - 1));
		D.Text(TEXT("Лучшее качество"), FVector2D(X0, Y), TxtS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		float QX = X0 + LabelW;
		if (Best != EFVQuality::Normal)
		{
			D.Stars(FVector2D(QX, Y), Best, 1.f);
			QX += 10.f;
		}
		D.Text(FVDB::QualityName(Best), FVector2D(QX, Y), TxtS, FVPal::Ink(), EFVAlign::Left, false, true);
	}
	D.PopClip();
	DetailScroll.DrawBar(D, TextArea);
	DetailScroll.DrawFades(D, TextArea, FVPal::Parchment());
}

// ============================================================================================
// «Достижения»
// ============================================================================================

/** 0.6.2 (Р81–Р83): the rods an achievement unlocks, as one line: a purchase lock — «Открывает: удочка «Румб» в лавке
    Кузьмы», a recipe lock — «Открывает: ковку удочки «Рулевой»». Empty = it unlocks no rod. */
FString AchRodRewardText(const FFVAchievementDef& A)
{
	TArray<FString> Parts;
	for (const FFVRodDef& Rod : FVDB::Rods())
	{
		if (FVDBU6::RodRequiredAchievement(Rod) != A.Id) { continue; }
		FString Short = Rod.Name;
		Short.RemoveFromStart(TEXT("Удочка "));
		Parts.Add(FVDBU6::RodRecipe(Rod.Id) ? FString::Printf(TEXT("ковку удочки %s"), *Short)
			: FString::Printf(TEXT("удочка %s в лавке Кузьмы"), *Short));
	}
	return Parts.Num() > 0 ? FString::Printf(TEXT("Открывает: %s."), *FString::Join(Parts, TEXT(", "))) : FString();
}

/** The card text of an achievement: its description + the rod it unlocks (0.6.2, Р83). The card wraps it as one text
    (closed: 2 lines in portrait / 1 in landscape; a tap opens all of it). */
FString AchCardText(const FFVAchievementDef& A)
{
	const FString Rod = AchRodRewardText(A);
	if (Rod.IsEmpty()) { return A.Description; }
	return A.Description.IsEmpty() ? Rod : A.Description + TEXT(" ") + Rod;
}

/** Update 6: new achievement kinds report their progress through AchievementProgressU6. */
float AchProgress01(const UFVSaveGame& P, const FFVAchievementDef& A)
{
	float V = 0.f;
	FString T;
	if (P.AchievementProgressU6(A, V, T)) { return FMath::Clamp(V, 0.f, 1.f); }
	return FMath::Clamp(P.AchievementProgress(A), 0.f, 1.f);
}

FString AchProgressText(const UFVSaveGame& P, const FFVAchievementDef& A)
{
	float V = 0.f;
	FString T;
	if (P.AchievementProgressU6(A, V, T)) { return T; }
	return P.AchievementProgressText(A);
}

class FAchievementsScreen final : public FVScreen
{
public:
	explicit FAchievementsScreen(UFVGame* InGame) : FVScreen(InGame)
	{
		Pager.NumPagesCached = NumFilters;
	}

	virtual void OnOpen() override
	{
		// update 6.1: the autotest context "fv_expand" opens the first card whose description does not fit
		bAutoOpen = Game && Game->GetScreenContext() == FName(TEXT("fv_expand"));
	}

	virtual void Tick(float Dt, const FFVInput& In) override
	{
		if (!Game)
		{
			return;
		}
		if (In.WasPressed(EKeys::J))
		{
			Game->CloseScreen();
			return;   // this screen is destroyed
		}
		if (In.WasPressed(EKeys::Left))  { Pager.GoTo(Pager.Page - 1); }   // the pager slides + clicks
		if (In.WasPressed(EKeys::Right)) { Pager.GoTo(Pager.Page + 1); }
		const int32 Fi = FMath::Clamp(Pager.Page, 0, NumFilters - 1);
		FFVScrollView& List = Lists[Fi];
		const float Step = FV::IsPortrait() ? CardPitchP : 45.f;
		// one card row (an opened card makes its row taller)
		if (In.WasPressed(EKeys::Up))   { List.ScrollTo(List.Target - RowStep(Fi, List.Target, false, Step)); }
		if (In.WasPressed(EKeys::Down)) { List.ScrollTo(List.Target + RowStep(Fi, List.Target, true, Step)); }
	}

	virtual void Draw(FVDraw& D) override;

private:
	static constexpr int32 NumFilters = int32(EFVAchTier::Count);   // update 6: one page per tier (§13)
	FFVPager Pager;
	FFVScrollView Lists[NumFilters];           // one scroll position per page
	static constexpr float CardHP = 62.f;      // portrait card height / pitch
	static constexpr float CardPitchP = 67.f;
	static constexpr float CardHL = 40.f;      // landscape card height (pitch 45)
	// update 6.1: a tap opens a card whose description does not fit — it grows to the whole text, the rows below move
	FName OpenAch[NumFilters];                 // per page: the opened card (NAME_None = all closed)
	bool bRevealOpen[NumFilters] = {};         // scroll the just-opened card into view once its height is known
	TArray<float> RowTops[NumFilters];         // per page: content Y of every card row (without the padding) + the end
	bool bAutoOpen = false;                    // context "fv_expand": open the first cut card on the first frame
	bool bRevealInstant = false;               // ...and show it without the scroll animation (the screenshot)

	/** Card width of the list drawn in ListPanel (DrawListPage's layout). */
	static float CardWidth(const FFVRect& ListPanel);
	/** Width of the name / description column of a card of width CardW. */
	static float DescWidth(float CardW);
	/** Does A's description not fit the closed card (portrait: 2 lines, landscape: 1 line)? */
	static bool DescCut(const FVDraw& D, const FFVAchievementDef& A, float CardW);
	/** Height of A's card: the closed one, or (bOpen) tall enough for its whole description. */
	static float CardHeight(const FVDraw& D, const FFVAchievementDef& A, float CardW, bool bOpen);
	/** Keyboard ↑/↓: the pitch of the card row the list is scrolled to (↑: the row above); Fallback = no rows. */
	float RowStep(int32 Fi, float Target, bool bDown, float Fallback) const;
	/** Page buttons: Target ± Amount; with an opened card (rows of different heights) it lands on a row top. */
	float PageTarget(int32 Fi, float Target, bool bDown, float Amount) const;
	/** Context "fv_expand": opens the first card with a cut description (this page first, else the next one with such). */
	void AutoOpen(const FVDraw& D, const UFVSaveGame& P, const FFVRect& ListPanel);

	static void Collect(const UFVSaveGame& P, int32 Filter, TArray<const FFVAchievementDef*>& Out);
	/** «Обычные 12/30», «Трудные 3/14»... (FVDBU6::AchTierName). */
	static TArray<FString> TabLabels(const UFVSaveGame& P, bool bShort = false);
	/** One pager page: the scrolling card list of Filter inside ListPanel (local page coords). */
	void DrawListPage(FVDraw& D, const UFVSaveGame& P, int32 Filter, const FFVRect& ListPanel);
	/** Phone layout: header, filter tabs (swipe the list to switch), one column of cards, back + page buttons. */
	void DrawPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& Win);
	/** The cards draw the whole description when bOpen; they return DescCut (the «ещё ▾» / «▴» hint is drawn then). */
	bool DrawCardPortrait(FVDraw& D, const UFVSaveGame& P, const FFVAchievementDef& A, const FFVRect& R, bool bOpen);
	bool DrawCard(FVDraw& D, const UFVSaveGame& P, const FFVAchievementDef& A, const FFVRect& R, bool bOpen);
};

float FAchievementsScreen::CardWidth(const FFVRect& ListPanel)
{
	const bool bP = FV::IsPortrait();
	const FFVRect Area = ListPanel.Inset(2.f);
	const float PadX = bP ? 6.f : 4.f;
	const float BarRoom = 6.f;
	const int32 Cols = bP ? 1 : 2;
	const float Gap = bP ? CardPitchP - CardHP : 5.f;
	return FMath::FloorToFloat((Area.W - 2.f * PadX - BarRoom - Gap * float(Cols - 1)) / float(Cols));
}

float FAchievementsScreen::DescWidth(float CardW)
{
	// portrait: icon 47 + reward column 86 + 8; landscape: icon 42 + reward column 84 + 6
	return FV::IsPortrait() ? CardW - 47.f - 86.f - 8.f : CardW - 42.f - 84.f - 6.f;
}

bool FAchievementsScreen::DescCut(const FVDraw& D, const FFVAchievementDef& A, float CardW)
{
	const float TW = DescWidth(CardW);
	if (FV::IsPortrait())
	{
		return WrapAllLines(D, AchCardText(A), TW, FV::Ui::Small()).Num() > 2;
	}
	return !AchCardText(A).IsEmpty() && D.MeasureText(AchCardText(A), 6.5f, false).X > TW;
}

float FAchievementsScreen::CardHeight(const FVDraw& D, const FFVAchievementDef& A, float CardW, bool bOpen)
{
	const bool bP = FV::IsPortrait();
	const float Closed = bP ? CardHP : CardHL;
	if (!bOpen)
	{
		return Closed;
	}
	// the closed card has room for 2 lines (portrait) / 1 line (landscape): every further line adds one step
	const float S = bP ? FV::Ui::Small() : 6.5f;
	const int32 N = WrapAllLines(D, AchCardText(A), DescWidth(CardW), S).Num();
	return Closed + float(FMath::Max(0, N - (bP ? 2 : 1))) * WrapStep(D, S);
}

float FAchievementsScreen::RowStep(int32 Fi, float Target, bool bDown, float Fallback) const
{
	const TArray<float>& Tops = RowTops[FMath::Clamp(Fi, 0, NumFilters - 1)];
	for (int32 r = 0; r + 1 < Tops.Num(); ++r)
	{
		const bool bIn = bDown ? (Target >= Tops[r] - 0.5f && Target < Tops[r + 1] - 0.5f)
			: (Target > Tops[r] + 0.5f && Target <= Tops[r + 1] + 0.5f);
		if (bIn)
		{
			return Tops[r + 1] - Tops[r];
		}
	}
	return Fallback;
}

float FAchievementsScreen::PageTarget(int32 Fi, float Target, bool bDown, float Amount) const
{
	const int32 F = FMath::Clamp(Fi, 0, NumFilters - 1);
	const float NewT = bDown ? Target + Amount : Target - Amount;
	if (OpenAch[F].IsNone())
	{
		return NewT;   // equal rows: the page step is whole rows already
	}
	float Best = NewT;
	for (const float T : RowTops[F])
	{
		if (bDown && T > Target + 0.5f && T <= NewT + 0.5f) { Best = T; }         // the last row top within the step
		if (!bDown && T < Target - 0.5f && T >= NewT - 0.5f) { return T; }        // the first row top within the step
	}
	return Best;
}

void FAchievementsScreen::AutoOpen(const FVDraw& D, const UFVSaveGame& P, const FFVRect& ListPanel)
{
	bAutoOpen = false;
	const float CardW = CardWidth(ListPanel);
	TArray<const FFVAchievementDef*> Shown;
	for (int32 k = 0; k < NumFilters; ++k)
	{
		const int32 Fi = (FMath::Clamp(Pager.Page, 0, NumFilters - 1) + k) % NumFilters;
		Collect(P, Fi, Shown);
		for (const FFVAchievementDef* A : Shown)
		{
			if (A && DescCut(D, *A, CardW))
			{
				OpenAch[Fi] = A->Id;
				bRevealOpen[Fi] = true;
				bRevealInstant = true;
				if (Fi != Pager.Page) { Pager.SetPage(Fi); }
				return;
			}
		}
	}
}

void FAchievementsScreen::Collect(const UFVSaveGame& P, int32 Filter, TArray<const FFVAchievementDef*>& Out)
{
	Out.Reset();
	// update 6: the page is the tier; unfinished first (the list order of the data is kept inside each group)
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const FFVAchievementDef& A : FVDB::Achievements())
		{
			if (int32(FMath::Min<uint8>(A.Tier, uint8(EFVAchTier::Count) - 1)) != Filter) { continue; }
			if (P.IsAchievementDone(A.Id) == (Pass == 0)) { continue; }
			Out.Add(&A);
		}
	}
}

TArray<FString> FAchievementsScreen::TabLabels(const UFVSaveGame& P, bool bShort)
{
	int32 Total[NumFilters] = {};
	int32 Done[NumFilters] = {};
	for (const FFVAchievementDef& A : FVDB::Achievements())
	{
		const int32 T = int32(FMath::Min<uint8>(A.Tier, uint8(EFVAchTier::Count) - 1));
		++Total[T];
		if (P.IsAchievementDone(A.Id)) { ++Done[T]; }
	}
	TArray<FString> L;
	for (int32 t = 0; t < NumFilters; ++t)
	{
		L.Add(bShort ? FVDBU6::AchTierName(EFVAchTier(t))
			: FString::Printf(TEXT("%s %d/%d"), *FVDBU6::AchTierName(EFVAchTier(t)), Done[t], Total[t]));
	}
	return L;
}

void FAchievementsScreen::DrawListPage(FVDraw& D, const UFVSaveGame& P, int32 Filter, const FFVRect& ListPanel)
{
	TArray<const FFVAchievementDef*> Shown;
	Collect(P, Filter, Shown);
	const bool bP = FV::IsPortrait();
	const int32 Fi = FMath::Clamp(Filter, 0, NumFilters - 1);
	FFVScrollView& Scroll = Lists[Fi];
	const FFVRect Area = ListPanel.Inset(2.f);          // the scroll viewport
	const float PadX = bP ? 6.f : 4.f;
	const float PadY = 4.f;
	const int32 Cols = bP ? 1 : 2;                       // (the scrollbar lives at the right edge: CardWidth)
	const float CardH = bP ? CardHP : CardHL;
	const float Gap = bP ? CardPitchP - CardHP : 5.f;
	const float CardW = CardWidth(ListPanel);
	const int32 Rows = FMath::DivideAndRoundUp(Shown.Num(), Cols);

	// Update 6.1: rows of different heights — the opened card's grid row takes its height (landscape: the card next to
	// it keeps the closed height), the rows below move down. RowTops also serve the ↑/↓ steps.
	int32 OpenIdx = INDEX_NONE;
	for (int32 i = 0; !OpenAch[Fi].IsNone() && i < Shown.Num(); ++i)
	{
		if (Shown[i] && Shown[i]->Id == OpenAch[Fi])
		{
			OpenIdx = i;
			break;
		}
	}
	const float OpenH = OpenIdx != INDEX_NONE ? FMath::Max(CardH, CardHeight(D, *Shown[OpenIdx], CardW, true)) : CardH;
	TArray<float>& Tops = RowTops[Fi];
	Tops.Reset(Rows + 1);
	float Acc = 0.f;
	for (int32 r = 0; r < Rows; ++r)
	{
		Tops.Add(Acc);
		Acc += (OpenIdx != INDEX_NONE && OpenIdx / Cols == r ? OpenH : CardH) + Gap;
	}
	Tops.Add(Acc);
	const float ContentH = 2.f * PadY + FMath::Max(0.f, Acc - Gap);
	Scroll.Update(D, Area, ContentH);
	if (bRevealOpen[Fi] && OpenIdx != INDEX_NONE)
	{
		// the just-opened card (with the padding around it) comes into view; a card taller than the list shows its top
		Scroll.ScrollToShow(Tops[OpenIdx / Cols], FMath::Min(OpenH + 2.f * PadY, Scroll.View), bRevealInstant);
		bRevealInstant = false;
	}
	bRevealOpen[Fi] = false;

	if (Shown.Num() == 0)
	{
		const TCHAR* Empty = TEXT("Достижений этой ступени пока нет.");
		D.Text(Empty, FVector2D(ListPanel.Center().X, ListPanel.Y + (bP ? 50.f : 60.f)), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
		return;
	}

	D.PushClip(Area);
	const float Y0 = Area.Y + PadY - Scroll.Offset();
	for (int32 i = 0; i < Shown.Num(); ++i)
	{
		const bool bOpen = i == OpenIdx;
		const FFVRect R(Area.X + PadX + float(i % Cols) * (CardW + Gap), Y0 + Tops[i / Cols], CardW, bOpen ? OpenH : CardH);
		if (R.Bottom() < Area.Y || R.Y > Area.Bottom() || !Shown[i])
		{
			continue;
		}
		const bool bCut = bP ? DrawCardPortrait(D, P, *Shown[i], R, bOpen) : DrawCard(D, P, *Shown[i], R, bOpen);
		if (D.Clicked(R))   // a tap (never a swipe / scroll): opens a cut card, closes the open one
		{
			const FName NewOpen = (bOpen || !bCut) ? NAME_None : Shown[i]->Id;
			if (NewOpen != OpenAch[Fi])
			{
				OpenAch[Fi] = NewOpen;
				bRevealOpen[Fi] = !NewOpen.IsNone();
				PlayTapSfx();
			}
		}
	}
	D.PopClip();
	Scroll.DrawBar(D, Area);
	Scroll.DrawFades(D, Area, FVPal::Parchment());
}

void FAchievementsScreen::Draw(FVDraw& D)
{
	if (!Game)
	{
		return;
	}
	const bool bPortrait = FV::IsPortrait();
	const FFVRect Win = FV::WindowRect();   // same frame as the other overlay screens (landscape: below the HUD bar)
	if (D.Window(Win, TEXT("Достижения")))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	if (!FV::IsTouchUI() && !bPortrait) (void)0; /* update 3: no key hints at the bottom (see the guide) */
	const UFVSaveGame* P = Game->GetProfile();
	if (!P)
	{
		D.Text(TEXT("Профиль игрока не загружен."), Win.Center(), 8.f, FVPal::InkSoft(), EFVAlign::Center);
		return;
	}
	if (bPortrait)
	{
		DrawPortrait(D, *P, Win);
		return;
	}

	const TArray<FFVAchievementDef>& All = FVDB::Achievements();
	int32 DoneCount = 0;
	int64 RewardsEarned = 0;
	for (const FFVAchievementDef& A : All)
	{
		if (P->IsAchievementDone(A.Id))
		{
			++DoneCount;
			RewardsEarned += A.RewardMoney;
		}
	}

	// ---- header ----
	ProgressHeader(D, EFVIcon::Trophy, TEXT("Выполнено"), DoneCount, All.Num(), FVPal::Gold());
	{
		const float RX = Win.Right() - 14.f;
		const float MW = D.MoneyLabel(FVector2D(RX, 41.f), RewardsEarned, 8.f, EFVAlign::Right, FVPal::Ink());
		D.Text(TEXT("Получено наград:"), FVector2D(RX - MW - 4.f, 41.5f), 7.f, FVPal::InkSoft(), EFVAlign::Right, false, false);
		if (All.Num() > 0 && DoneCount >= All.Num())
		{
			D.Text(TEXT("Все достижения выполнены!"), FVector2D(RX, 53.f), 7.f, InkGood(), EFVAlign::Right, false, true);
		}
	}

	// ---- panel + filter tabs; each filter is a page (swipe the list left / right) ----
	const FFVRect ListPanel(20.f, 85.f, 600.f, 254.f);
	D.Panel(ListPanel, EFVPanel::Parchment);
	if (bAutoOpen) { AutoOpen(D, *P, ListPanel); }
	const FFVRect PagesArea = ListPanel.Inset(2.f);
	Pager.Update(D, PagesArea, NumFilters);
	FVKit::TabStrip(D, FFVRect(ListPanel.X + 4.f, ListPanel.Y - 15.f, float(NumFilters) * 130.f + 2.f * float(NumFilters - 1), 16.f), TabLabels(*P), Pager);
	Pager.DrawPages(D, PagesArea, [&](int32 Filter) { DrawListPage(D, *P, Filter, ListPanel); });
	FVKit::SwipeHint(D, FFVRect(ListPanel.X - 1.f, ListPanel.Y, ListPanel.W + 2.f, ListPanel.H), Pager);
}

void FAchievementsScreen::DrawPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& Win)
{
	const TArray<FFVAchievementDef>& All = FVDB::Achievements();
	int32 DoneCount = 0;
	int64 RewardsEarned = 0;
	for (const FFVAchievementDef& A : All)
	{
		if (P.IsAchievementDone(A.Id))
		{
			++DoneCount;
			RewardsEarned += A.RewardMoney;
		}
	}

	const float L = Win.X + 10.f;
	const float CW = Win.W - 20.f;
	const float R = L + CW;
	const float Top = Win.Y + 14.f;
	const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;

	// ---- header: progress + rewards earned ----
	ProgressHeaderAt(D, FVector2D(L, Top), 140.f, EFVIcon::Trophy, TEXT("Выполнено"), DoneCount, All.Num(), FVPal::Gold());
	D.Text(TEXT("Получено наград"), FVector2D(R, Top), FV::Ui::Small(), FVPal::InkSoft(), EFVAlign::Right, false, false);
	D.MoneyLabel(FVector2D(R, Top + 11.f), RewardsEarned, FV::Ui::Body(), EFVAlign::Right, FVPal::Ink());

	// ---- filter tabs + the list panel (pages slide inside it) ----
	const FFVRect TabRow(L, Top + 30.f, CW, 30.f);
	const float PanelY = TabRow.Bottom() + 9.f;
	const FFVRect ListPanel(L, PanelY, CW, FMath::Max(80.f, Bottom - PanelY));
	D.Panel(ListPanel, EFVPanel::Parchment);
	if (bAutoOpen) { AutoOpen(D, P, ListPanel); }
	const FFVRect PagesArea = ListPanel.Inset(2.f);
	Pager.Update(D, PagesArea, NumFilters);
	FVKit::TabStrip(D, TabRow, TabLabels(P, true), Pager);
	Pager.DrawPages(D, PagesArea, [&](int32 Filter) { DrawListPage(D, P, Filter, ListPanel); });
	FVKit::SwipeHint(D, FFVRect(ListPanel.X - 1.f, ListPanel.Y, ListPanel.W + 2.f, ListPanel.H), Pager);

	// ---- bottom bar: back + page up / down for the thumb (animated) ----
	FFVRect Actions;
	if (D.ActionBar(Win, &Actions))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	{
		const int32 Fi = FMath::Clamp(Pager.Page, 0, NumFilters - 1);
		FFVScrollView& List = Lists[Fi];
		const float PageStep = FMath::Max(CardPitchP, FMath::FloorToFloat((ListPanel.H - 20.f) / CardPitchP) * CardPitchP);
		if (D.Button(FVDraw::SplitH(Actions, 0, 2), TEXT("↑ Выше"), EFVButton::Normal, List.Target > 0.5f, BarTextSize()))
		{
			List.ScrollTo(PageTarget(Fi, List.Target, false, PageStep));
		}
		if (D.Button(FVDraw::SplitH(Actions, 1, 2), TEXT("Ниже ↓"), EFVButton::Normal, List.Target < List.MaxOffset() - 0.5f, BarTextSize()))
		{
			List.ScrollTo(PageTarget(Fi, List.Target, true, PageStep));
		}
	}
}

bool FAchievementsScreen::DrawCardPortrait(FVDraw& D, const UFVSaveGame& P, const FFVAchievementDef& A, const FFVRect& R, bool bOpen)
{
	const bool bDone = P.IsAchievementDone(A.Id);
	const float Prog = bDone ? 1.f : AchProgress01(P, A);
	const FLinearColor TierC = FVDBU6::AchTierColor(EFVAchTier(FMath::Min<uint8>(A.Tier, uint8(EFVAchTier::Count) - 1)));

	// card
	D.Rect(R, bDone ? FV::Col(255, 228, 140) : FV::Col(250, 228, 184));
	D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 255, 255, 90));
	D.Frame(R, TierC, bDone ? 2.f : 1.f);   // update 6: the frame has the colour of the tier
	D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 3.f, R.H - 2.f), TierC);
	if (bDone)
	{
		const float Sweep = FMath::Frac(float(D.Time) * 0.25f + R.Y * 0.013f);
		const float SX = R.X + Sweep * (R.W + 40.f) - 20.f;
		for (int32 k = 0; k < 6; ++k)
		{
			const float LX = SX + float(k);
			if (LX > R.X + 2.f && LX < R.Right() - 3.f)
			{
				D.Rect(FFVRect(LX, R.Y + 2.f, 1.f, R.H - 4.f), FV::Col(255, 255, 255, 40));
			}
		}
	}

	// icon (an opened, taller card keeps it where the closed card has it)
	const FVector2D IC(R.X + 23.f, R.Y + FMath::Min(R.H, CardHP) * 0.5f);
	D.Circle(IC, 17.f, bDone ? FV::Col(206, 150, 34) : FV::Col(196, 156, 110));
	D.Circle(IC, 15.f, bDone ? FV::Col(255, 214, 96) : FV::Col(226, 192, 146));
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.SpriteCentered(Sprites->Icon(EFVIcon::Trophy), IC, 1.f, bDone ? FLinearColor::White : FV::Col(120, 96, 80, 170));
	}
	if (bDone)
	{
		CheckBadge(D, IC + FVector2D(12.f, 12.f));
	}

	// reward column (right)
	const float RewardW = 86.f;
	const float RX = R.Right() - 7.f;
	D.Text(TEXT("Награда"), FVector2D(RX, R.Y + 5.f), 7.f, FVPal::InkSoft(), EFVAlign::Right, false, false);
	float RY = R.Y + 17.f;
	const FFVCosmeticDef* Cos = A.RewardCosmetic.IsNone() ? nullptr : FVDB::FindCosmetic(A.RewardCosmetic);
	if (A.RewardMoney > 0)
	{
		D.MoneyLabel(FVector2D(RX, RY), A.RewardMoney, FV::Ui::Body(), EFVAlign::Right, bDone ? InkGood() : FVPal::Ink());
		RY += 16.f;
	}
	if (Cos)
	{
		const float CS = FV::Ui::Small();
		const FString CName = FitText(D, Cos->Name, RewardW - 18.f, CS);
		const float NW = D.MeasureText(CName, CS).X;
		D.Text(CName, FVector2D(RX, RY + 1.f), CS, bDone ? InkGood() : FVPal::Ink(), EFVAlign::Right, false, true);
		DrawCosmeticIcon(D, *Cos, FVector2D(RX - NW - 9.f, RY + 5.f), A.RewardMoney > 0 ? 0.75f : 1.f);
	}
	if (A.RewardMoney <= 0 && !Cos)
	{
		D.Text(TEXT("—"), FVector2D(RX, RY), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Right);
	}

	// name, description (closed: up to 2 lines; opened by a tap: all of it — the card is taller), progress bar with the numbers
	const float TX = R.X + 47.f;
	const float TW = DescWidth(R.W);   // R.W - 47 - RewardW - 8
	D.Text(FitText(D, A.Name, TW, FV::Ui::Body()), FVector2D(TX, R.Y + 4.f), FV::Ui::Body(), FVPal::Ink(), EFVAlign::Left, false, true);
	const float DescS = FV::Ui::Small();
	const TArray<FString> DescLines = WrapAllLines(D, AchCardText(A), TW, DescS);
	const bool bCut = DescLines.Num() > 2;
	if (bOpen && bCut)
	{
		DrawLines(D, DescLines, FVector2D(TX, R.Y + 18.f), TW, DescS, FVPal::InkSoft(), false, WrapStep(D, DescS));
	}
	else
	{
		TextWrappedLimited(D, AchCardText(A), FVector2D(TX, R.Y + 18.f), TW, DescS, FVPal::InkSoft(), 2, false);
	}
	const FFVRect BarR(TX, R.Bottom() - 15.f, TW, 11.f);
	D.Bar(BarR, Prog, bDone ? FVPal::Gold() : FVPal::Green(), FV::Col(120, 84, 60), true);
	const FString ProgText = bDone ? FString(TEXT("Выполнено!")) : AchProgressText(P, A);
	D.TextOutlined(ProgText, FVector2D(BarR.Center().X, BarR.Y + 1.f), 7.f, FVPal::White(), EFVAlign::Center);
	if (bCut)
	{
		OpenHint(D, RX, R.Bottom() - 8.f, bOpen, 6.5f, true);   // under the reward column
	}
	return bCut;
}

bool FAchievementsScreen::DrawCard(FVDraw& D, const UFVSaveGame& P, const FFVAchievementDef& A, const FFVRect& R, bool bOpen)
{
	const bool bDone = P.IsAchievementDone(A.Id);
	const float Prog = bDone ? 1.f : AchProgress01(P, A);
	const FLinearColor TierC = FVDBU6::AchTierColor(EFVAchTier(FMath::Min<uint8>(A.Tier, uint8(EFVAchTier::Count) - 1)));
	const bool bHover = D.Hover(R);

	// card
	FLinearColor Fill = bDone ? FV::Col(255, 228, 140) : FV::Col(250, 228, 184);
	if (bHover) { Fill = bDone ? FV::Col(255, 236, 168) : FV::Col(255, 238, 204); }
	D.Rect(R, Fill);
	D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 255, 255, 90));
	D.Frame(R, TierC, bDone ? 2.f : 1.f);   // update 6: the frame has the colour of the tier
	D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 3.f, R.H - 2.f), TierC);
	if (bDone)
	{
		// a slow shine sweeping over completed cards
		const float Sweep = FMath::Frac(float(D.Time) * 0.25f + R.Y * 0.013f);
		const float SX = R.X + Sweep * (R.W + 40.f) - 20.f;
		for (int32 k = 0; k < 6; ++k)
		{
			const float LX = SX + float(k);
			if (LX > R.X + 2.f && LX < R.Right() - 3.f)
			{
				D.Rect(FFVRect(LX, R.Y + 2.f, 1.f, R.H - 4.f), FV::Col(255, 255, 255, 40));
			}
		}
	}

	// icon
	const FFVRect IconR(R.X + 6.f, R.Y + 5.f, 30.f, 30.f);
	D.Circle(IconR.Center(), 14.f, bDone ? FV::Col(206, 150, 34) : FV::Col(196, 156, 110));
	D.Circle(IconR.Center(), 12.f, bDone ? FV::Col(255, 214, 96) : FV::Col(226, 192, 146));
	if (UFVSprites* Sprites = UFVSprites::Get())
	{
		D.SpriteCentered(Sprites->Icon(EFVIcon::Trophy), IconR.Center(), 1.f, bDone ? FLinearColor::White : FV::Col(120, 96, 80, 170));
	}
	if (bDone)
	{
		CheckBadge(D, FVector2D(IconR.Right() - 2.f, IconR.Bottom() - 3.f));
	}

	// reward column
	const float RewardW = 84.f;
	const float RX = R.Right() - 6.f;
	D.Text(TEXT("Награда"), FVector2D(RX, R.Y + 3.f), 6.f, FVPal::InkSoft(), EFVAlign::Right, false, false);
	float RY = R.Y + 12.f;
	const FFVCosmeticDef* Cos = A.RewardCosmetic.IsNone() ? nullptr : FVDB::FindCosmetic(A.RewardCosmetic);
	if (A.RewardMoney > 0)
	{
		D.MoneyLabel(FVector2D(RX, RY), A.RewardMoney, 7.5f, EFVAlign::Right, bDone ? InkGood() : FVPal::Ink());
		RY += 12.f;
	}
	if (Cos)
	{
		const FString CName = FitText(D, Cos->Name, RewardW - 20.f, 6.5f);
		const float NW = D.MeasureText(CName, 6.5f).X;
		D.Text(CName, FVector2D(RX, RY + 1.f), 6.5f, bDone ? InkGood() : FVPal::Ink(), EFVAlign::Right, false, true);
		DrawCosmeticIcon(D, *Cos, FVector2D(RX - NW - 10.f, RY + 4.f), A.RewardMoney > 0 ? 0.75f : 1.f);
	}
	if (A.RewardMoney <= 0 && !Cos)
	{
		D.Text(TEXT("—"), FVector2D(RX, RY), 7.f, FVPal::InkSoft(), EFVAlign::Right);
	}

	// name, description (closed: one line; opened by a tap: all of it — the card is taller), progress
	const float TX = R.X + 42.f;
	const float TW = DescWidth(R.W);   // R.W - 42 - RewardW - 6
	D.Text(FitText(D, A.Name, TW, 8.f), FVector2D(TX, R.Y + 3.f), 8.f, FVPal::Ink(), EFVAlign::Left, false, true);
	const bool bCut = DescCut(D, A, R.W);
	if (bOpen && bCut)
	{
		DrawLines(D, WrapAllLines(D, AchCardText(A), TW, 6.5f), FVector2D(TX, R.Y + 14.f), TW, 6.5f, FVPal::InkSoft(), false, WrapStep(D, 6.5f));
	}
	else
	{
		D.Text(FitText(D, AchCardText(A), TW, 6.5f, false), FVector2D(TX, R.Y + 14.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, false);
	}
	const FFVRect BarR(TX, R.Bottom() - 14.f, TW, 9.f);   // closed: R.Y + 26
	D.Bar(BarR, Prog, bDone ? FVPal::Gold() : FVPal::Green(), FV::Col(120, 84, 60), true);
	const FString ProgText = bDone ? FString(TEXT("Выполнено!")) : AchProgressText(P, A);
	D.TextOutlined(ProgText, FVector2D(BarR.Center().X, BarR.Y + 0.5f), 6.f, FVPal::White(), EFVAlign::Center);
	if (bCut)
	{
		OpenHint(D, R.Right() - 4.f, R.Bottom() - 4.f, bOpen, 6.f, false);   // bottom-right, under the reward column
	}

	if (bHover)
	{
		FString Body = AchCardText(A);
		TArray<FString> Rewards;
		if (A.RewardMoney > 0) { Rewards.Add(FString::Printf(TEXT("%s монет"), *FV::Money(A.RewardMoney))); }
		if (Cos) { Rewards.Add(FString::Printf(TEXT("%s «%s»"), *FVDB::SlotName(Cos->Slot), *Cos->Name)); }
		if (Rewards.Num() > 0) { Body += FString(TEXT("\nНаграда: ")) + FString::Join(Rewards, TEXT(", ")); }
		Body += bDone ? FString(TEXT("\nВыполнено!")) : FString::Printf(TEXT("\nПрогресс: %s"), *AchProgressText(P, A));
		D.Tooltip(A.Name, Body);
	}
	return bCut;
}

} // namespace FVCollectionScreenImpl
} // namespace

TUniquePtr<FVScreen> FVMakeCollectionScreen(UFVGame* Game)
{
	return MakeUnique<FVCollectionScreenImpl::FCollectionScreen>(Game);
}

TUniquePtr<FVScreen> FVMakeAchievementsScreen(UFVGame* Game)
{
	return MakeUnique<FVCollectionScreenImpl::FAchievementsScreen>(Game);
}
