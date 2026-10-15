// FVScreenWardrobe.cpp — «Гардероб»: a cozy room with the animated character (and pet), owned cosmetics per slot.
// Update 3: the slot tabs are swipeable pages (FFVPager), the item grids scroll smoothly (FFVScrollView).
// Landscape: hover = try on, click = equip. Portrait: tap = select & try on, «Надеть» puts it on. Hotkey G closes.

#include "CoreMinimal.h"
#include "FVScreens.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVUIKit.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVAudio.h"
#include "FVScene.h"

namespace
{
namespace FVWardrobeScreenImpl
{
// ============================================================================================
// Helpers (private to this file)
// ============================================================================================

void PlaySfx(EFVSfx Sfx, float Volume = 1.f, float Pitch = 1.f)
{
	if (UFVAudio* Audio = UFVAudio::Get())
	{
		Audio->Play(Sfx, Volume, Pitch);
	}
}

FLinearColor InkGood()  { return FV::Col(46, 132, 34); }
FLinearColor InkEquip() { return FV::Col(36, 104, 186); }
FLinearColor InkReward(){ return FV::Col(132, 62, 184); }
FLinearColor Divider()  { return FV::Col(214, 162, 104); }

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

void CheckBadge(FVDraw& D, const FVector2D& C, float R = 5.f)
{
	D.Circle(C, R, FV::Col(36, 76, 22));
	D.Circle(C, R - 1.f, FVPal::Green());
	D.Line(C + FVector2D(-2.4f, 0.f), C + FVector2D(-0.6f, 1.9f), FVPal::White(), 1.4f);
	D.Line(C + FVector2D(-0.6f, 1.9f), C + FVector2D(2.6f, -2.2f), FVPal::White(), 1.4f);
}

// ---- update 3: tabs / pages helpers ----

/** Gold underline under a tab row that follows the pager's slide (same look as FVKit::TabStrip's indicator). */
void TabIndicator(FVDraw& D, const FFVRect& Row, int32 N, float Gap, const FFVPager& Pager, float Y, float H)
{
	if (N <= 1)
	{
		return;
	}
	const float V = FMath::Clamp(Pager.VisualPos(), 0.f, float(N - 1));
	const int32 I0 = FMath::FloorToInt(V);
	const int32 I1 = FMath::Min(N - 1, I0 + 1);
	const float T = V - float(I0);
	const FFVRect C0 = FVDraw::SplitH(Row, I0, N, Gap);
	const FFVRect C1 = FVDraw::SplitH(Row, I1, N, Gap);
	const float X = FMath::Lerp(C0.X, C1.X, T) + 6.f;
	const float W = FMath::Lerp(C0.W, C1.W, T) - 12.f;
	D.Rect(FFVRect(FMath::RoundToFloat(X), Y, FMath::RoundToFloat(W), H), FVPal::Gold());
}

/** Names of the slot tabs / pages (EFVCosmeticSlot order). */
const TCHAR* SlotTabName(int32 Slot)
{
	static const TCHAR* const Names[] = { TEXT("Шляпы"), TEXT("Костюмы"), TEXT("Поплавки"), TEXT("Питомцы"), TEXT("Элитное") };
	return Names[FMath::Clamp(Slot, 0, 4)];
}

constexpr int32 NumSlots = int32(EFVCosmeticSlot::Count);
static_assert(NumSlots == 4, "Wardrobe tabs expect 4 cosmetic slots");
/** Update 6: the fifth page «Элитное» — every owned boutique item (FFVCosmeticDef::bElite) of all slots. Its pseudo
    slot is EFVCosmeticSlot::Count; each entry is worn in its own slot (Cos->Slot). */
constexpr int32 NumPages = NumSlots + 1;
constexpr EFVCosmeticSlot EliteSlot = EFVCosmeticSlot::Count;
FORCEINLINE bool IsElitePage(EFVCosmeticSlot S) { return S == EliteSlot; }

/** Inventory-slot look (same as FVDraw::Slot) without its press-to-click behaviour. */
void SlotLook(FVDraw& D, const FFVRect& R, bool bSelected, bool bPressed)
{
	D.Rect(R, FV::Col(176, 110, 56));
	const FFVRect In = R.Inset(1.f);
	D.Rect(In, FV::Col(255, 222, 158));
	D.Rect(FFVRect(In.X, In.Y, In.W, 1.f), FV::Col(214, 150, 88));
	D.Rect(FFVRect(In.X, In.Y + 1.f, 1.f, In.H - 1.f), FV::Col(214, 150, 88));
	D.Rect(FFVRect(In.X + 1.f, In.Bottom() - 1.f, In.W - 1.f, 1.f), FV::Col(255, 240, 206));
	D.Rect(FFVRect(In.Right() - 1.f, In.Y + 1.f, 1.f, In.H - 2.f), FV::Col(255, 240, 206));
	if (bPressed)
	{
		D.Rect(In.Inset(1.f), FV::Col(255, 255, 255, 60));
	}
	if (bSelected)
	{
		D.Frame(R.Inset(-1.f), FV::Col(214, 60, 40), 2.f);
		D.Frame(R.Inset(1.f), FV::Col(255, 200, 80, 200), 1.f);
	}
	else if (bPressed)
	{
		D.Frame(R, FV::Col(236, 150, 60), 1.f);
	}
}

// ---- portrait: bottom action bar (FVDraw::ActionBar: "← Назад" on the left, the actions to its right) ----

/** Label size of the big bar buttons. */
float BarTextSize() { return FV::Ui::Body() + 0.5f; }

/** Small coloured label ("Надето", "Примерка"), right-aligned at RightX. Returns its width. */
float Pill(FVDraw& D, const FString& Label, float RightX, float Y, const FLinearColor& Fill, float Size, float H)
{
	const float W = FMath::CeilToFloat(D.MeasureText(Label, Size).X) + 10.f;
	const FFVRect R(RightX - W, Y, W, H);
	D.Rect(R, Fill);
	D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 255, 255, 70));
	D.Frame(R, FV::Col(52, 30, 20, 170));
	const float TH = D.MeasureText(Label, Size).Y;
	D.Text(Label, FVector2D(R.X + W * 0.5f, R.Y + FMath::RoundToFloat((H - TH) * 0.5f)), Size, FVPal::White(), EFVAlign::Center);
	return W;
}

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

EFVHatStyle HatStyleOf(const FFVCosmeticDef& C)       { return EFVHatStyle(FMath::Min<int32>(C.Style, int32(EFVHatStyle::Count) - 1)); }
EFVOutfitStyle OutfitStyleOf(const FFVCosmeticDef& C) { return EFVOutfitStyle(FMath::Min<int32>(C.Style, int32(EFVOutfitStyle::Count) - 1)); }
EFVBobberStyle BobberStyleOf(const FFVCosmeticDef& C) { return EFVBobberStyle(FMath::Min<int32>(C.Style, int32(EFVBobberStyle::Count) - 1)); }
EFVPetStyle PetStyleOf(const FFVCosmeticDef& C)       { return EFVPetStyle(FMath::Min<int32>(C.Style, int32(EFVPetStyle::Count) - 1)); }

bool SlotCanBeEmpty(EFVCosmeticSlot Slot)
{
	return Slot == EFVCosmeticSlot::Hat || Slot == EFVCosmeticSlot::Pet;
}

bool IsNoneStyle(const FFVCosmeticDef& C)
{
	return (C.Slot == EFVCosmeticSlot::Hat && HatStyleOf(C) == EFVHatStyle::None)
		|| (C.Slot == EFVCosmeticSlot::Pet && PetStyleOf(C) == EFVPetStyle::None);
}

FString EmptyName(EFVCosmeticSlot Slot)
{
	return Slot == EFVCosmeticSlot::Pet ? FString(TEXT("Без питомца")) : FString(TEXT("Без шляпы"));
}

FString EmptyDescription(EFVCosmeticSlot Slot)
{
	return Slot == EFVCosmeticSlot::Pet
		? FString(TEXT("Питомец остаётся дома и ждёт вас с уловом. Можно позвать его в любой момент."))
		: FString(TEXT("Ветерок в волосах и солнышко на макушке. Иногда шляпа и правда лишняя."));
}

/** Icon of a cosmetic, sized to fill roughly Size x Size virtual px. Tint dims locked reward items (update 5). */
void DrawCosmeticIcon(FVDraw& D, const FFVCosmeticDef& C, const FVector2D& Center, float Scale, int32 PetFrame = 0,
	const FLinearColor& Tint = FLinearColor::White)
{
	UFVSprites* Sprites = UFVSprites::Get();
	if (!Sprites)
	{
		return;
	}
	switch (C.Slot)
	{
	case EFVCosmeticSlot::Hat:    D.SpriteCentered(Sprites->HatIcon(HatStyleOf(C), C.Primary, C.Secondary), Center, Scale, Tint); break;
	case EFVCosmeticSlot::Outfit: D.SpriteCentered(Sprites->OutfitIcon(OutfitStyleOf(C), C.Primary, C.Secondary), Center, Scale, Tint); break;
	case EFVCosmeticSlot::Bobber: D.SpriteCentered(Sprites->Bobber(BobberStyleOf(C), 0), Center, Scale * 1.5f, Tint); break;
	case EFVCosmeticSlot::Pet:
	{
		const int32 Frames = FMath::Max(1, UFVSprites::PetFrameCount(EFVPetAnim::Idle));
		D.SpriteCentered(Sprites->Pet(PetStyleOf(C), EFVPetAnim::Idle, PetFrame % Frames), Center, Scale, Tint);
		break;
	}
	default: break;
	}
}

// ---- update 5: reward skins the player has not earned yet are shown locked, with the way to get them ----

/** The achievement that grants this cosmetic (nullptr for shop / starter / treasure items). */
const FFVAchievementDef* RewardAchievementOf(FName CosmeticId)
{
	for (const FFVAchievementDef& A : FVDB::Achievements())
	{
		if (A.RewardCosmetic == CosmeticId) { return &A; }
	}
	return nullptr;
}

/** «Награда: достижение «Повелитель пруда»». */
FString RewardHint(const FFVCosmeticDef& C)
{
	const FFVAchievementDef* A = RewardAchievementOf(C.Id);
	return A ? FString::Printf(TEXT("Награда: достижение «%s»"), *A->Name) : FString(TEXT("Особая награда"));
}

/** Dim tint of a locked item's icon. */
FLinearColor LockedTint() { return FLinearColor(0.55f, 0.5f, 0.5f, 0.6f); }

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

/** "No hat" / "no pet" placeholder icon: a dashed circle with a small cross. */
void DrawEmptyIcon(FVDraw& D, const FVector2D& C)
{
	const FLinearColor Col = FV::Col(170, 120, 80);
	for (int32 a = 0; a < 16; ++a)
	{
		if (a % 2) { continue; }
		const float Ang = float(a) / 16.f * 2.f * UE_PI;
		D.Rect(FFVRect(FMath::FloorToFloat(C.X + FMath::Cos(Ang) * 11.f) - 1.f, FMath::FloorToFloat(C.Y + FMath::Sin(Ang) * 11.f) - 1.f, 2.f, 2.f), Col);
	}
	D.Line(C + FVector2D(-5.f, -5.f), C + FVector2D(5.f, 5.f), Col, 2.f);
	D.Line(C + FVector2D(5.f, -5.f), C + FVector2D(-5.f, 5.f), Col, 2.f);
}

void ApplyToLook(FFVAppearance& Look, const FFVCosmeticDef& C)
{
	if (C.Slot == EFVCosmeticSlot::Hat)
	{
		Look.Hat = HatStyleOf(C);
		Look.HatPrimary = C.Primary;
		Look.HatSecondary = C.Secondary;
	}
	else if (C.Slot == EFVCosmeticSlot::Outfit)
	{
		Look.Outfit = OutfitStyleOf(C);
		Look.OutfitPrimary = C.Primary;
		Look.OutfitSecondary = C.Secondary;
	}
}

// ============================================================================================
// The screen
// ============================================================================================

struct FEntry
{
	const FFVCosmeticDef* Cos = nullptr;   // nullptr = "Без шляпы" / "Без питомца"
	bool bLocked = false;                  // update 5: an achievement reward not earned yet (try-on only)
};

class FWardrobeScreen final : public FVScreen
{
public:
	explicit FWardrobeScreen(UFVGame* InGame)
		: FVScreen(InGame)
	{
		for (int32& V : SelIdx) { V = -1; }
		SlotPager.NumPagesCached = NumPages;
	}

	virtual void Tick(float Dt, const FFVInput& In) override;
	virtual void Draw(FVDraw& D) override;

private:
	static constexpr int32 Cols = 7;
	static constexpr int32 VisibleRows = 3;
	static constexpr float SlotSize = 44.f;
	static constexpr float SlotGap = 6.f;

	FFVPager SlotPager;                     // page = EFVCosmeticSlot (tabs, swipe, 1–4)
	FFVScrollView GridScroll[NumPages];     // per page: the item grid
	int32 FacingIdx = 0;                    // index into FacingOrder
	double EquipFlashTime = -100.0;

	// per-frame hover (filled by the grid, consumed by the stage preview); portrait: the selected item
	bool bHoverValid = false;
	FEntry HoverEntry;
	EFVCosmeticSlot HoverSlot = EFVCosmeticSlot::Hat;

	// portrait (phone) layout: tap = select & try on, the bar's «Надеть» puts it on, swipe = scroll / switch tab
	int32 SelIdx[NumPages];                 // selected grid entry per page (-1 = the one worn)
	static float CellP() { return FV::Ui::Slot(); }   // grid cell of the portrait layout
	static constexpr float CellGapP = 6.f;

	/** The active tab (target page of the pager). */
	EFVCosmeticSlot CurSlot() const { return EFVCosmeticSlot(FMath::Clamp(SlotPager.Page, 0, NumPages - 1)); }
	/** Worn test that works on the «Элитное» page too (an entry is worn in its own slot). */
	static bool IsWorn(const UFVSaveGame& P, const FEntry& E, EFVCosmeticSlot PageSlot)
	{
		return E.Cos ? P.GetEquipped(E.Cos->Slot) == E.Cos->Id : P.GetEquipped(PageSlot).IsNone();
	}
	static FString SlotText(const FEntry& E, EFVCosmeticSlot PageSlot)
	{
		return E.Cos ? FVDB::SlotName(E.Cos->Slot) : FVDB::SlotName(PageSlot);
	}

	void DrawPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win);
	void DrawSlotTabsPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& GridPanel, float TabH);
	void DrawGridPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, const TArray<FEntry>& Entries, EFVCosmeticSlot Slot);
	void DrawInfoPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& Panel, const TArray<FEntry>& Entries, int32 Sel, EFVCosmeticSlot Slot);
	/** Bottom bar: "← Назад" · Надеть / Снять. True = the screen was replaced (closed). */
	bool DrawActionBarPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win, const TArray<FEntry>& Entries, int32 Sel, EFVCosmeticSlot Slot);
	void DrawRotateButtons(FVDraw& D, const FFVRect& Stage);
	/** Index of the entry worn in Slot (the "nothing" entry when empty), -1 if none. */
	int32 WornIndex(const UFVSaveGame& P, const TArray<FEntry>& Entries, EFVCosmeticSlot Slot) const;
	/** Selected entry of Slot (defaults to the worn one). */
	int32 SelectedIndex(const UFVSaveGame& P, const TArray<FEntry>& Entries, EFVCosmeticSlot Slot);

	static EFVFacing FacingAt(int32 Idx)
	{
		static const EFVFacing Order[] = { EFVFacing::Down, EFVFacing::Left, EFVFacing::Up, EFVFacing::Right };
		return Order[((Idx % 4) + 4) % 4];
	}

	UFVSaveGame* Profile() const { return Game ? Game->GetProfile() : nullptr; }
	void BuildEntries(const UFVSaveGame& P, EFVCosmeticSlot Slot, TArray<FEntry>& Out) const;
	void SetSlot(EFVCosmeticSlot Slot);
	void Rotate(int32 Dir, bool bSound);
	void Equip(UFVSaveGame& P, const FEntry& E, EFVCosmeticSlot Slot);

	void DrawRight(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel);
	/** Landscape page of one slot: scrolling grid + info about the hovered / worn item + "how many more" footer. */
	void DrawSlotPage(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, EFVCosmeticSlot Slot);
	void DrawLeft(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel);
	void DrawStage(FVDraw& D, const UFVSaveGame& P, const FFVRect& Stage);
};

void FWardrobeScreen::BuildEntries(const UFVSaveGame& P, EFVCosmeticSlot Slot, TArray<FEntry>& Out) const
{
	Out.Reset();
	if (IsElitePage(Slot))
	{
		for (int32 s = 0; s < NumSlots; ++s)
		{
			for (const FFVCosmeticDef* C : FVDB::CosmeticsFor(EFVCosmeticSlot(s)))
			{
				if (C && C->bElite && !IsNoneStyle(*C) && P.OwnsCosmetic(C->Id))
				{
					Out.AddDefaulted_GetRef().Cos = C;
				}
			}
		}
		return;
	}
	if (SlotCanBeEmpty(Slot))
	{
		Out.AddDefaulted();
	}
	for (const FFVCosmeticDef* C : FVDB::CosmeticsFor(Slot))
	{
		if (C && !IsNoneStyle(*C) && P.OwnsCosmetic(C->Id))
		{
			FEntry& E = Out.AddDefaulted_GetRef();
			E.Cos = C;
		}
	}
	// update 5: achievement rewards not earned yet come last, locked (the legend skins: «Награда: достижение «…»»)
	for (const FFVCosmeticDef* C : FVDB::CosmeticsFor(Slot))
	{
		if (C && !IsNoneStyle(*C) && C->Price <= 0 && !P.OwnsCosmetic(C->Id) && RewardAchievementOf(C->Id))
		{
			FEntry& E = Out.AddDefaulted_GetRef();
			E.Cos = C;
			E.bLocked = true;
		}
	}
}

void FWardrobeScreen::SetSlot(EFVCosmeticSlot Slot)
{
	if (int32(Slot) < NumPages)
	{
		SlotPager.GoTo(int32(Slot));   // slides to that tab; the pager plays the click
	}
}

void FWardrobeScreen::Rotate(int32 Dir, bool bSound)
{
	FacingIdx = (FacingIdx + Dir + 4) % 4;
	if (bSound)
	{
		PlaySfx(EFVSfx::Click, 0.6f, Dir > 0 ? 1.1f : 0.95f);
	}
}

void FWardrobeScreen::Equip(UFVSaveGame& P, const FEntry& E, EFVCosmeticSlot PageSlot)
{
	const EFVCosmeticSlot Slot = E.Cos ? E.Cos->Slot : PageSlot;   // «Элитное»: the item's own slot
	if (!E.Cos && IsElitePage(Slot))
	{
		return;
	}
	bool bChanged = false;
	if (E.bLocked)
	{
		PlaySfx(EFVSfx::Error, 0.6f);   // not earned yet: the info says which achievement gives it
		return;
	}
	if (E.Cos)
	{
		if (P.GetEquipped(Slot) != E.Cos->Id)
		{
			bChanged = P.EquipCosmetic(E.Cos->Id);
			if (!bChanged)
			{
				PlaySfx(EFVSfx::Error);
				return;
			}
		}
	}
	else if (SlotCanBeEmpty(Slot) && !P.GetEquipped(Slot).IsNone())
	{
		P.UnequipSlot(Slot);
		bChanged = true;
	}

	if (!bChanged)
	{
		return;   // already worn (the slot widget played its click)
	}
	PlaySfx(EFVSfx::Equip);
	EquipFlashTime = Game ? Game->GetTime() : 0.0;
	if (Game)
	{
		Game->GetScene().RefreshAppearance();
		Game->NotifyProfileChanged();
	}
}

void FWardrobeScreen::Tick(float Dt, const FFVInput& In)
{
	if (!Game)
	{
		return;
	}
	if (In.WasPressed(EKeys::G))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	if (In.WasPressed(EKeys::Left))  { Rotate(-1, true); }
	if (In.WasPressed(EKeys::Right)) { Rotate(+1, true); }

	static const FKey SlotKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
	for (int32 s = 0; s < NumPages; ++s)
	{
		if (In.WasPressed(SlotKeys[s])) { SetSlot(EFVCosmeticSlot(s)); }
	}
	// mouse wheel / finger drags are read by the grids' scroll views while drawing
}

void FWardrobeScreen::Draw(FVDraw& D)
{
	if (!Game)
	{
		return;
	}
	const bool bPortrait = FV::IsPortrait();
	const FFVRect Win = FV::WindowRect();   // same frame as the other overlay screens (landscape: below the HUD bar)
	if (D.Window(Win, TEXT("Гардероб")))
	{
		Game->CloseScreen();
		return;   // this screen is destroyed
	}
	if (!FV::IsTouchUI() && !bPortrait) (void)0; /* update 3: no key hints at the bottom (see the guide) */
	UFVSaveGame* P = Profile();
	if (!P)
	{
		D.Text(TEXT("Гардероб недоступен: профиль игрока не загружен."), Win.Center(), 8.f, FVPal::InkSoft(), EFVAlign::Center);
		return;
	}
	if (bPortrait)
	{
		DrawPortrait(D, *P, Win);
		return;
	}

	// The right side first: it decides what is hovered, the stage then shows the try-on.
	DrawRight(D, *P, FFVRect(236.f, 59.f, 384.f, 280.f));
	DrawLeft(D, *P, FFVRect(20.f, 42.f, 210.f, 297.f));
}

void FWardrobeScreen::DrawRight(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel)
{
	bHoverValid = false;
	HoverEntry = FEntry();

	D.Panel(Panel, EFVPanel::Parchment);
	const FFVRect PagesArea = Panel.Inset(2.f);
	SlotPager.Update(D, PagesArea, NumPages);

	// ---- slot tabs (the underline follows the slide) ----
	TArray<FString> Labels;
	for (int32 t = 0; t < NumPages; ++t)
	{
		Labels.Add(SlotTabName(t));
	}
	const FFVRect TabRow(Panel.X + 4.f, Panel.Y - 15.f, FMath::Min(Panel.W - 8.f, float(NumPages) * 80.f + float(NumPages - 1) * 2.f), 16.f);
	FVKit::TabStrip(D, TabRow, Labels, SlotPager);
	for (int32 t = 0; t < NumPages; ++t)
	{
		if (D.Hover(FVDraw::SplitH(TabRow, t, NumPages, 2.f)))
		{
			D.Tooltip(SlotTabName(t), FString::Printf(TEXT("Клавиша %d · или смахните сетку влево / вправо"), t + 1));
		}
	}

	// ---- one page per slot: swipe the grid left / right to switch ----
	SlotPager.DrawPages(D, PagesArea, [&](int32 Page) { DrawSlotPage(D, P, Panel, EFVCosmeticSlot(Page)); });
	FVKit::SwipeHint(D, FFVRect(Panel.X - 1.f, Panel.Y, Panel.W + 2.f, 6.f + float(VisibleRows) * (SlotSize + SlotGap) + 2.f), SlotPager);
}

void FWardrobeScreen::DrawSlotPage(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, EFVCosmeticSlot Slot)
{
	const int32 SlotI = FMath::Clamp(int32(Slot), 0, NumPages - 1);
	TArray<FEntry> Entries;
	BuildEntries(P, Slot, Entries);

	// ---- grid (smooth scrolling) ----
	const float X0 = Panel.X + 10.f;
	const float IW = Panel.W - 20.f;
	const float Pitch = SlotSize + SlotGap;
	const float Pad = 4.f;
	const float GridW = float(Cols) * Pitch - SlotGap;
	const float GX = FMath::RoundToFloat(Panel.X + (Panel.W - GridW) * 0.5f);
	const FFVRect GridArea(Panel.X + 2.f, Panel.Y + 6.f, Panel.W - 4.f, float(VisibleRows) * Pitch - SlotGap + 2.f * Pad);
	const int32 TotalRows = FMath::DivideAndRoundUp(FMath::Max(1, Entries.Num()), Cols);
	FFVScrollView& Scroll = GridScroll[SlotI];
	Scroll.Update(D, GridArea, float(TotalRows) * Pitch - SlotGap + 2.f * Pad);

	const FName Equipped = P.GetEquipped(Slot);
	const int32 PetFrame = int32(D.Time * 2.5);
	int32 ClickedIdx = INDEX_NONE;
	int32 PageHoverIdx = INDEX_NONE;
	D.PushClip(GridArea);
	const float Y0 = GridArea.Y + Pad - Scroll.Offset();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FFVRect SR(GX + float(i % Cols) * Pitch, Y0 + float(i / Cols) * Pitch, SlotSize, SlotSize);
		if (SR.Bottom() < GridArea.Y || SR.Y > GridArea.Bottom())
		{
			continue;
		}
		const FEntry& E = Entries[i];
		const bool bIsEquipped = IsWorn(P, E, Slot);
		const bool bHover = D.Hover(SR);
		if (D.Slot(SR, bIsEquipped, true))   // a click (never a scroll / swipe) puts it on
		{
			ClickedIdx = i;
		}
		if (E.Cos)
		{
			const bool bAnimate = bHover && E.Cos->Slot == EFVCosmeticSlot::Pet;
			const float Bob = (bHover && E.Cos->Slot != EFVCosmeticSlot::Pet) ? FMath::Sin(float(D.Time) * 8.f) * 1.f : 0.f;
			DrawCosmeticIcon(D, *E.Cos, SR.Center() + FVector2D(0.f, FMath::RoundToFloat(Bob)), 2.f, bAnimate ? PetFrame : 0,
				(E.bLocked && !bHover) ? LockedTint() : FLinearColor::White);
			if (E.bLocked)
			{
				MiniLock(D, FVector2D(SR.Right() - 10.f, SR.Y + 3.f));
			}
		}
		else
		{
			DrawEmptyIcon(D, SR.Center());
		}
		if (bIsEquipped)
		{
			CheckBadge(D, FVector2D(SR.Right() - 5.f, SR.Y + 5.f));
		}
		if (bHover)
		{
			PageHoverIdx = i;
			bHoverValid = true;
			HoverEntry = E;
			HoverSlot = E.Cos ? E.Cos->Slot : Slot;
		}
	}
	D.PopClip();
	Scroll.DrawBar(D, GridArea);
	Scroll.DrawFades(D, GridArea, FVPal::Parchment());
	if (Entries.IsValidIndex(ClickedIdx))
	{
		Equip(P, Entries[ClickedIdx], Slot);
	}

	// ---- info about the hovered (or equipped) item ----
	const float IY = GridArea.Bottom() + 4.f;
	D.Rect(FFVRect(X0, IY - 3.f, IW, 1.f), Divider());
	int32 InfoIdx = PageHoverIdx;
	if (!Entries.IsValidIndex(InfoIdx))
	{
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (IsWorn(P, Entries[i], Slot))
			{
				InfoIdx = i;
				break;
			}
		}
	}
	if (Entries.IsValidIndex(InfoIdx))
	{
		const FEntry& Info = Entries[InfoIdx];
		const bool bInfoEquipped = IsWorn(P, Info, Slot);
		const FString Name = Info.Cos ? Info.Cos->Name : EmptyName(Slot);
		D.Text(FitText(D, Name, IW, 11.f), FVector2D(X0, IY + 1.f), 11.f, FVPal::Ink(), EFVAlign::Left, false, true);
		const FString SlotText = FWardrobeScreen::SlotText(Info, Slot);
		D.Text(SlotText, FVector2D(X0, IY + 16.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
		const float SW = D.MeasureText(SlotText, 7.f, false).X;
		if (Info.bLocked && Info.Cos)
		{
			// update 5: a reward skin not earned yet — how to get it
			D.Text(FitText(D, FString(TEXT("  ·  ")) + RewardHint(*Info.Cos), IW - SW, 7.f), FVector2D(X0 + SW, IY + 16.f), 7.f, InkReward(), EFVAlign::Left, false, true);
		}
		else
		{
			D.Text(bInfoEquipped ? TEXT("  ·  надето") : TEXT("  ·  нажмите, чтобы надеть"), FVector2D(X0 + SW, IY + 16.f), 7.f,
				bInfoEquipped ? InkEquip() : FVPal::InkSoft(), EFVAlign::Left, false, bInfoEquipped);
		}
		D.TextWrapped(Info.Cos ? Info.Cos->Description : EmptyDescription(Slot), FVector2D(X0, IY + 28.f), IW, 7.5f, FVPal::Ink(), false);
	}
	else
	{
		D.TextWrapped(TEXT("Наведите на вещь, чтобы примерить её, и нажмите, чтобы надеть."), FVector2D(X0, IY + 4.f), IW, 8.f, FVPal::InkSoft(), false);
	}

	// ---- how many more exist ----
	// classic clothes (TraderMask 0) are sold only at Mitrich's stall by the pond; update-6 items elsewhere (boutique...)
	int32 InShop = 0;
	int32 InCity = 0;
	int32 AsReward = 0;
	const TArray<FName> Starters = FVDB::StartCosmetics();
	for (const FFVCosmeticDef* C : FVDB::CosmeticsFor(Slot))
	{
		if (!C || IsNoneStyle(*C) || P.OwnsCosmetic(C->Id) || Starters.Contains(C->Id)) { continue; }
		if (C->Price <= 0) { ++AsReward; } else if (C->TraderMask == 0) { ++InShop; } else { ++InCity; }
	}
	const float BY = Panel.Bottom() - 26.f;
	D.Rect(FFVRect(X0, BY - 5.f, IW, 1.f), Divider());
	FString Line1, Line2;
	if (InShop > 0) { Line1 = FString::Printf(TEXT("Ещё %d — в лавке Митрича у пруда"), InShop); }
	else if (InCity > 0) { Line1 = FString::Printf(TEXT("Ещё %d — в лавках Города"), InCity); }
	if (InShop > 0 && InCity > 0)
	{
		Line2 = AsReward > 0 ? FString::Printf(TEXT("%d — в лавках Города, %d — в награду"), InCity, AsReward)
		                     : FString::Printf(TEXT("и %d — в лавках Города"), InCity);
	}
	else if (!Line1.IsEmpty() && AsReward > 0)
	{
		Line2 = FString::Printf(TEXT("и %d — в награду за достижения и сокровища"), AsReward);
	}
	if (!Line1.IsEmpty())
	{
		D.Text(Line1, FVector2D(X0, BY + 1.f), 8.f, FVPal::Ink(), EFVAlign::Left, false, true);
		if (!Line2.IsEmpty()) { D.Text(Line2, FVector2D(X0, BY + 12.f), 6.5f, InkReward(), EFVAlign::Left, false, false); }
	}
	else if (AsReward > 0)
	{
		D.Text(FString::Printf(TEXT("Ещё %d можно получить только в награду"), AsReward), FVector2D(X0, BY + 4.f), 8.f, InkReward(), EFVAlign::Left, false, true);
	}
	else
	{
		D.Text(TEXT("Собрано всё — настоящий модник!"), FVector2D(X0, BY + 4.f), 8.f, InkGood(), EFVAlign::Left, false, true);
	}
}

void FWardrobeScreen::DrawLeft(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel)
{
	D.Panel(Panel, EFVPanel::Parchment);
	const FFVRect Stage(Panel.X + 4.f, Panel.Y + 4.f, Panel.W - 8.f, 188.f);
	DrawStage(D, P, Stage);

	// ---- rotate buttons ----
	const float RY = Stage.Bottom() + 4.f;
	UFVSprites* Sprites = UFVSprites::Get();
	if (Sprites)
	{
		const FFVSprite LeftIcon = Sprites->Icon(EFVIcon::ArrowLeft);
		const FFVSprite RightIcon = Sprites->Icon(EFVIcon::ArrowRight);
		if (D.IconButton(FFVRect(Stage.X, RY, 24.f, 20.f), LeftIcon, false, true, TEXT("Повернуть (←)")))
		{
			Rotate(-1, false);
		}
		if (D.IconButton(FFVRect(Stage.Right() - 24.f, RY, 24.f, 20.f), RightIcon, false, true, TEXT("Повернуть (→)")))
		{
			Rotate(+1, false);
		}
	}
	static const TCHAR* const FacingNames[] = { TEXT("Вид спереди"), TEXT("Вид слева"), TEXT("Вид сзади"), TEXT("Вид справа") };
	D.Text(FacingNames[((FacingIdx % 4) + 4) % 4], FVector2D(Stage.Center().X, RY + 5.f), 7.5f, FVPal::InkSoft(), EFVAlign::Center);

	// ---- summary of what is worn (click = open that tab) ----
	static const TCHAR* const SlotLabels[] = { TEXT("Шляпа"), TEXT("Костюм"), TEXT("Поплавок"), TEXT("Питомец") };
	float Y = RY + 26.f;
	for (int32 s = 0; s < NumSlots; ++s)
	{
		const EFVCosmeticSlot Slot = EFVCosmeticSlot(s);
		const FFVRect Row(Panel.X + 4.f, Y, Panel.W - 8.f, 17.f);
		const bool bActive = Slot == CurSlot();
		if (bActive)
		{
			D.Rect(Row, FV::Col(238, 190, 122));
		}
		else if (D.Hover(Row))
		{
			D.Rect(Row, FV::Col(253, 232, 188));
		}
		if (D.Clicked(Row))
		{
			SetSlot(Slot);
		}
		const FFVCosmeticDef* C = FVDB::FindCosmetic(P.GetEquipped(Slot));
		const FVector2D IconC(Row.X + 10.f, Row.Y + 8.5f);
		if (C)
		{
			DrawCosmeticIcon(D, *C, IconC, 1.f);
		}
		const FString Label = FString::Printf(TEXT("%s:"), SlotLabels[s]);
		D.Text(Label, FVector2D(Row.X + 22.f, Row.Y + 4.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
		const float LW = D.MeasureText(Label, 7.f, false).X + 4.f;
		const FString Value = C ? C->Name : (SlotCanBeEmpty(Slot) ? FString(TEXT("нет")) : FString(TEXT("—")));
		D.Text(FitText(D, Value, Row.W - 26.f - LW, 7.f), FVector2D(Row.X + 22.f + LW, Row.Y + 4.f), 7.f, C ? FVPal::Ink() : FVPal::InkSoft());
		Y += 18.f;
	}
}

// -------------------------------------------------------------------------------- portrait (phone) layout
//
//   ┌ room: the character (big, animated) + pet, ◄ ► rotate ┐
//   [Шляпы][Костюмы][Поплавки][Питомцы]      (icon of what is worn in each tab; gold underline follows the slide)
//   | grid of owned items: tap = select & try on (vertical swipe scrolls)  |  <- swipe left / right = next tab:
//   | name · state · description of the selected item                      |     grid + info slide together
//   [← Назад] [        Надеть / Снять        ]           <- thumb zone (primary action at the right)

int32 FWardrobeScreen::WornIndex(const UFVSaveGame& P, const TArray<FEntry>& Entries, EFVCosmeticSlot Slot) const
{
	const FName Equipped = P.GetEquipped(Slot);
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (IsWorn(P, Entries[i], Slot))
		{
			return i;
		}
	}
	return Entries.Num() > 0 ? 0 : -1;
}

int32 FWardrobeScreen::SelectedIndex(const UFVSaveGame& P, const TArray<FEntry>& Entries, EFVCosmeticSlot Slot)
{
	int32& S = SelIdx[FMath::Clamp(int32(Slot), 0, NumPages - 1)];
	if (!Entries.IsValidIndex(S))
	{
		S = WornIndex(P, Entries, Slot);
	}
	return S;
}

void FWardrobeScreen::DrawPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win)
{
	const float L = Win.X + 10.f;
	const float CW = Win.W - 20.f;
	const float Top = Win.Y + 14.f;
	const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;
	const float StageH = FMath::Clamp(FMath::RoundToFloat(FV::VH * 0.3f), 184.f, 250.f);
	const float TabH = 32.f;
	const float InfoH = 74.f;
	const FFVRect Stage(L, Top, CW, StageH);
	const FFVRect InfoPanel(L, Bottom - InfoH, CW, InfoH);
	const float GridY = Stage.Bottom() + 6.f + TabH;
	const FFVRect GridPanel(L, GridY, CW, FMath::Max(CellP() + 36.f, InfoPanel.Y - 6.f - GridY));

	// The pages first: a tap selects before the room draws the character wearing it.
	D.Panel(GridPanel, EFVPanel::Parchment);
	D.Panel(InfoPanel, EFVPanel::Parchment);
	// One page per slot = its grid + its info; both slide together (the panel frames stay).
	const FFVRect PagesArea(GridPanel.X + 2.f, GridPanel.Y + 2.f, GridPanel.W - 4.f, InfoPanel.Bottom() - 2.f - (GridPanel.Y + 2.f));
	SlotPager.Update(D, PagesArea, NumPages);
	DrawSlotTabsPortrait(D, P, GridPanel, TabH);
	SlotPager.DrawPages(D, PagesArea, [&](int32 Page)
	{
		const EFVCosmeticSlot PageSlot = EFVCosmeticSlot(Page);
		TArray<FEntry> PageEntries;
		BuildEntries(P, PageSlot, PageEntries);
		DrawGridPortrait(D, P, GridPanel, PageEntries, PageSlot);
		DrawInfoPortrait(D, P, InfoPanel, PageEntries, SelectedIndex(P, PageEntries, PageSlot), PageSlot);
	});
	FVKit::SwipeHint(D, FFVRect(GridPanel.X - 1.f, GridPanel.Y, GridPanel.W + 2.f, GridPanel.H), SlotPager);

	// no hover on phones: the stage "tries on" the selected item of the active tab instead
	const EFVCosmeticSlot Slot = CurSlot();
	TArray<FEntry> Entries;
	BuildEntries(P, Slot, Entries);
	const int32 Sel = SelectedIndex(P, Entries, Slot);
	bHoverValid = Entries.IsValidIndex(Sel);
	HoverEntry = bHoverValid ? Entries[Sel] : FEntry();
	HoverSlot = HoverEntry.Cos ? HoverEntry.Cos->Slot : Slot;

	DrawStage(D, P, Stage);
	DrawRotateButtons(D, Stage);

	DrawActionBarPortrait(D, P, Win, Entries, Sel, Slot);   // may replace the screen: keep it last
}

void FWardrobeScreen::DrawRotateButtons(FVDraw& D, const FFVRect& Stage)
{
	UFVSprites* Sprites = UFVSprites::Get();
	if (!Sprites)
	{
		return;
	}
	const float BS = FV::Ui::IconBtn();
	const float BY = FMath::RoundToFloat(Stage.Y + Stage.H * 0.52f - BS * 0.5f);
	if (D.IconButton(FFVRect(Stage.X + 6.f, BY, BS, BS), Sprites->Icon(EFVIcon::ArrowLeft), false, true))
	{
		Rotate(-1, false);
	}
	if (D.IconButton(FFVRect(Stage.Right() - 6.f - BS, BY, BS, BS), Sprites->Icon(EFVIcon::ArrowRight), false, true))
	{
		Rotate(+1, false);
	}
	static const TCHAR* const FacingNames[] = { TEXT("Вид спереди"), TEXT("Вид слева"), TEXT("Вид сзади"), TEXT("Вид справа") };
	D.TextOutlined(FacingNames[((FacingIdx % 4) + 4) % 4], FVector2D(Stage.Center().X, Stage.Bottom() - 14.f), FV::Ui::Small(),
		FV::Col(255, 244, 222), EFVAlign::Center, FV::Col(70, 36, 18));
}

void FWardrobeScreen::DrawSlotTabsPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& GridPanel, float TabH)
{
	const int32 N = NumPages;
	const float Gap = 2.f;
	const FFVRect Row(GridPanel.X + 4.f, GridPanel.Y - TabH + 1.f, GridPanel.W - 8.f, TabH);
	for (int32 t = 0; t < N; ++t)
	{
		const EFVCosmeticSlot Slot = EFVCosmeticSlot(t);
		const bool bActive = SlotPager.Page == t;
		const FFVRect TR = FVDraw::SplitH(Row, t, N, Gap);
		if (D.Tab(TR, FString(), bActive) && !bActive)
		{
			SetSlot(Slot);   // animated slide to that tab
		}
		// icon of what is worn in this slot + the slot name
		const float DY = bActive ? 0.f : 1.f;
		const float CY = TR.Y + TR.H * 0.5f + DY;
		const FFVCosmeticDef* C = FVDB::FindCosmetic(P.GetEquipped(Slot));
		if (C && IsNoneStyle(*C))
		{
			C = nullptr;
		}
		const float Size = 8.5f;
		const FLinearColor Ink = bActive ? FVPal::Ink() : FVPal::InkSoft();
		const FString Name = SlotTabName(t);
		const float TextH = D.MeasureText(Name, Size).Y;
		if (C)
		{
			DrawCosmeticIcon(D, *C, FVector2D(TR.X + 12.f, CY), 1.f);
			D.Text(FitText(D, Name, TR.W - 26.f, Size), FVector2D(TR.X + 22.f, FMath::RoundToFloat(CY - TextH * 0.5f)), Size, Ink, EFVAlign::Left, bActive, true);
		}
		else
		{
			D.Text(FitText(D, Name, TR.W - 6.f, Size), FVector2D(TR.Center().X, FMath::RoundToFloat(CY - TextH * 0.5f)), Size, Ink, EFVAlign::Center, bActive, true);
		}
	}
	// gold underline sliding with the pages
	TabIndicator(D, Row, N, Gap, SlotPager, Row.Bottom(), 2.f);
}

void FWardrobeScreen::DrawGridPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Panel, const TArray<FEntry>& Entries, EFVCosmeticSlot Slot)
{
	const int32 SlotI = FMath::Clamp(int32(Slot), 0, NumPages - 1);
	const float Cell = CellP();
	const float Pitch = Cell + CellGapP;
	const float Pad = 4.f;
	const float FooterH = 16.f;   // "Ещё N в лавке" line at the bottom of the panel
	const int32 GridCols = FMath::Max(1, FMath::FloorToInt((Panel.W - 16.f + CellGapP) / Pitch));
	const float GridW = float(GridCols) * Pitch - CellGapP;
	const float X0 = FMath::RoundToFloat(Panel.X + (Panel.W - GridW) * 0.5f);
	const FFVRect GridArea(Panel.X + 2.f, Panel.Y + 4.f, Panel.W - 4.f, FMath::Max(Cell + 2.f * Pad, Panel.H - 4.f - FooterH - 6.f));
	const int32 TotalRows = FMath::DivideAndRoundUp(FMath::Max(1, Entries.Num()), GridCols);
	FFVScrollView& Scroll = GridScroll[SlotI];
	Scroll.Update(D, GridArea, float(TotalRows) * Pitch - CellGapP + 2.f * Pad);

	const int32 Sel = SelectedIndex(P, Entries, Slot);
	const FName Equipped = P.GetEquipped(Slot);
	const bool bTouch = FV::IsTouchUI();
	int32 TappedIdx = INDEX_NONE;
	D.PushClip(GridArea);
	const float Y0 = GridArea.Y + Pad - Scroll.Offset();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FFVRect SR(X0 + float(i % GridCols) * Pitch, Y0 + float(i / GridCols) * Pitch, Cell, Cell);
		if (SR.Bottom() < GridArea.Y || SR.Y > GridArea.Bottom())
		{
			continue;
		}
		const FEntry& E = Entries[i];
		const bool bIsEquipped = IsWorn(P, E, Slot);
		// pressed look only while the finger rests on it (a scroll / swipe cancels it)
		const bool bHover = D.Hover(SR);
		const bool bPressed = bHover && (!bTouch || (D.IsPointerDown() && !D.IsDragGesture()));
		SlotLook(D, SR, i == Sel, bPressed);
		if (D.Clicked(SR))   // release without dragging
		{
			TappedIdx = i;
		}
		if (E.Cos)
		{
			const bool bAnimate = i == Sel && E.Cos->Slot == EFVCosmeticSlot::Pet;
			const float Bob = (bPressed && E.Cos->Slot != EFVCosmeticSlot::Pet) ? FMath::Sin(float(D.Time) * 8.f) : 0.f;
			DrawCosmeticIcon(D, *E.Cos, SR.Center() + FVector2D(0.f, FMath::RoundToFloat(Bob)), 2.f, bAnimate ? int32(D.Time * 2.5) : 0,
				(E.bLocked && i != Sel) ? LockedTint() : FLinearColor::White);
			if (E.bLocked)
			{
				MiniLock(D, FVector2D(SR.Right() - 11.f, SR.Y + 4.f));
			}
		}
		else
		{
			DrawEmptyIcon(D, SR.Center());
		}
		if (bIsEquipped)
		{
			CheckBadge(D, FVector2D(SR.Right() - 6.f, SR.Y + 6.f));
		}
	}
	D.PopClip();
	Scroll.DrawBar(D, GridArea);
	Scroll.DrawFades(D, GridArea, FVPal::Parchment());

	// only the "nothing" entry: say where to get more
	const int32 Real = Entries.Num() - (SlotCanBeEmpty(Slot) ? 1 : 0);
	if (Real <= 0)
	{
		const float TY = GridArea.Y + Pad + (Entries.Num() > 0 ? Pitch + 6.f : 12.f);
		if (TY + 24.f < GridArea.Bottom())
		{
			const FString Msg = IsElitePage(Slot) ? FString(TEXT("Элитных вещей пока нет. Их продаёт бутик «Жемчужина» в Городе."))
				: (Slot == EFVCosmeticSlot::Pet ? FString(TEXT("Питомцев пока нет. Котята и щенки ждут вас в лавке!"))
				: FString(TEXT("Здесь пока пусто — загляните в лавку.")));
			D.TextWrapped(Msg, FVector2D(X0, TY), GridW, FV::Ui::Body(), FVPal::InkSoft(), false);
		}
	}

	// ---- how many more exist (footer line) ----
	{
		int32 InShop = 0;
		int32 AsReward = 0;
		const TArray<FName> Starters = FVDB::StartCosmetics();
		for (const FFVCosmeticDef* C : FVDB::CosmeticsFor(Slot))
		{
			if (!C || IsNoneStyle(*C) || P.OwnsCosmetic(C->Id) || Starters.Contains(C->Id)) { continue; }
			if (C->Price > 0) { ++InShop; } else { ++AsReward; }
		}
		FString Line;
		FLinearColor LineC = FVPal::InkSoft();
		if (InShop > 0 && AsReward > 0) { Line = FString::Printf(TEXT("Ещё %d в лавке и %d — в награду"), InShop, AsReward); }
		else if (InShop > 0)            { Line = FString::Printf(TEXT("Ещё %d в лавке"), InShop); }
		else if (AsReward > 0)          { Line = FString::Printf(TEXT("Ещё %d можно получить только в награду"), AsReward); LineC = InkReward(); }
		else                            { Line = TEXT("Собрано всё — настоящий модник!"); LineC = InkGood(); }
		const float FY = GridArea.Bottom() + 4.f;
		if (FY + 12.f <= Panel.Bottom())
		{
			D.Text(FitText(D, Line, Panel.W - 20.f, FV::Ui::Small()), FVector2D(Panel.Center().X, FY), FV::Ui::Small(), LineC, EFVAlign::Center, false, true);
		}
	}

	// tap = select (and try on); tapping the selected item again puts it on
	if (Entries.IsValidIndex(TappedIdx))
	{
		int32& S = SelIdx[SlotI];
		const FEntry& E = Entries[TappedIdx];
		const bool bIsEquipped = IsWorn(P, E, Slot);
		if (TappedIdx == S && !bIsEquipped)
		{
			Equip(P, E, Slot);   // plays the equip sound + sparkles
		}
		else
		{
			if (TappedIdx != S)
			{
				PlaySfx(EFVSfx::Click, 0.6f);
			}
			S = TappedIdx;
		}
	}
}

void FWardrobeScreen::DrawInfoPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& Panel, const TArray<FEntry>& Entries, int32 Sel, EFVCosmeticSlot Slot)
{
	const float X0 = Panel.X + 8.f;
	const float W = Panel.W - 16.f;
	if (!Entries.IsValidIndex(Sel))
	{
		D.TextWrapped(TEXT("Выберите вещь в сетке, чтобы примерить её."), FVector2D(X0, Panel.Y + 10.f), W, FV::Ui::Body(), FVPal::InkSoft(), false);
		return;
	}
	const FEntry& E = Entries[Sel];
	const FName Equipped = P.GetEquipped(Slot);
	const bool bWorn = IsWorn(P, E, Slot);

	// ---- name + state pill ----
	const bool bLocked = E.bLocked && E.Cos;   // update 5: a reward skin not earned yet
	const FString PillText = bWorn ? FString(TEXT("Надето")) : (bLocked ? FString(TEXT("Награда")) : FString(TEXT("Примерка")));
	const float PillS = 7.5f;
	const float PillW = D.MeasureText(PillText, PillS).X + 10.f;
	const FString Name = E.Cos ? E.Cos->Name : EmptyName(Slot);
	D.Text(FitText(D, Name, W - PillW - 8.f, FV::Ui::Header()), FVector2D(X0, Panel.Y + 5.f), FV::Ui::Header(), FVPal::Ink(), EFVAlign::Left, false, true);
	Pill(D, PillText, X0 + W, Panel.Y + 7.f, bWorn ? FV::Col(56, 118, 196) : (bLocked ? FV::Col(146, 76, 196) : FV::Col(214, 128, 40)), PillS, 15.f);

	const FString Hint = bWorn ? FString(TEXT("сейчас на вас"))
		: (bLocked ? RewardHint(*E.Cos) : (E.Cos ? FString(TEXT("нажмите «Надеть» внизу")) : FString(TEXT("нажмите «Снять» внизу"))));
	D.Text(FitText(D, FString::Printf(TEXT("%s · %s"), *SlotText(E, Slot), *Hint), W, FV::Ui::Small(), bLocked),
		FVector2D(X0, Panel.Y + 23.f), FV::Ui::Small(), bWorn ? InkEquip() : (bLocked ? InkReward() : FVPal::InkSoft()), EFVAlign::Left, false, bLocked);

	const FString Desc = E.Cos ? E.Cos->Description : EmptyDescription(Slot);
	const float BodyS = FV::Ui::Body();
	const float Step = FMath::RoundToFloat(D.LineHeight(BodyS, false) * 0.95f);
	const int32 Lines = FMath::Clamp(FMath::FloorToInt((Panel.Bottom() - 4.f - (Panel.Y + 37.f)) / FMath::Max(1.f, Step)), 1, 3);
	TextWrappedLimited(D, Desc, FVector2D(X0, Panel.Y + 37.f), W, BodyS, FVPal::Ink(), Lines, false);
}

bool FWardrobeScreen::DrawActionBarPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win, const TArray<FEntry>& Entries, int32 Sel, EFVCosmeticSlot Slot)
{
	FFVRect Actions;
	if (D.ActionBar(Win, &Actions))
	{
		Game->CloseScreen();
		return true;   // this screen is destroyed
	}
	// Clothes are bought only at Mitrich's stall by the pond, so the whole action area is the main action.
	const FFVRect Main = Actions;

	const float S = BarTextSize();
	if (!Entries.IsValidIndex(Sel))
	{
		D.Button(Main, TEXT("Надеть"), EFVButton::Normal, false, S);
		return false;
	}
	const FEntry& E = Entries[Sel];
	const FName Equipped = P.GetEquipped(Slot);
	if (E.bLocked)
	{
		D.Button(Main, TEXT("Награда за достижение"), EFVButton::Normal, false, S);   // update 5: not earned yet
	}
	else if (E.Cos)
	{
		const bool bWorn = IsWorn(P, E, Slot);
		if (!bWorn)
		{
			if (D.Button(Main, TEXT("Надеть"), EFVButton::Primary, true, S))
			{
				Equip(P, E, Slot);
			}
		}
		else if (SlotCanBeEmpty(E.Cos->Slot))
		{
			if (D.Button(Main, TEXT("Снять"), EFVButton::Normal, true, S))
			{
				Equip(P, FEntry(), E.Cos->Slot);   // the "nothing" entry = take it off
			}
		}
		else
		{
			D.Button(Main, TEXT("Надето"), EFVButton::Normal, false, S);
		}
	}
	else if (!Equipped.IsNone())
	{
		// the "no hat" / "no pet" entry: take off what is worn
		if (D.Button(Main, TEXT("Снять"), EFVButton::Primary, true, S))
		{
			Equip(P, E, Slot);
		}
	}
	else
	{
		D.Button(Main, EmptyName(Slot), EFVButton::Normal, false, S);
	}
	return false;
}

void FWardrobeScreen::DrawStage(FVDraw& D, const UFVSaveGame& P, const FFVRect& Stage)
{
	const float T = float(FMath::Fmod(D.Time, 10000.0));
	// Portrait: a wide, tall stage -> bigger character in the middle, the pet beside it, wider rug.
	const bool bP = FV::IsPortrait();
	const float CharScale = bP ? (Stage.H >= 220.f ? 4.f : 3.f) : 3.f;
	const float FloorY = Stage.Bottom() - (bP ? 20.f + 10.f * CharScale : 46.f);

	// ---- wall with striped wallpaper ----
	D.Rect(FFVRect(Stage.X, Stage.Y, Stage.W, FloorY - Stage.Y), FV::Col(232, 196, 146));
	for (float X = Stage.X + 5.f; X < Stage.Right() - 2.f; X += 14.f)
	{
		D.Rect(FFVRect(X, Stage.Y, 4.f, FloorY - Stage.Y), FV::Col(220, 180, 130));
		D.Rect(FFVRect(X + 1.f, Stage.Y + 6.f + float(int32(X) % 20), 2.f, 2.f), FV::Col(206, 150, 110));
	}

	// ---- window: shows the actual time of day and weather ----
	{
		const FFVRect Win(Stage.X + 10.f, Stage.Y + 10.f, 44.f, 36.f);
		const float Night = Game ? FMath::Clamp(Game->GetNightFactor(), 0.f, 1.f) : 0.f;
		const bool bRain = Game && Game->GetWeather() == EFVWeather::Rain;
		FLinearColor Sky = FMath::Lerp(FV::Col(150, 208, 244), FV::Col(28, 36, 84), Night);
		if (bRain) { Sky = FMath::Lerp(Sky, FV::Col(110, 124, 140), 0.45f); }
		D.Rect(Win, Sky);
		if (Night > 0.5f)
		{
			D.Circle(FVector2D(Win.X + 32.f, Win.Y + 10.f), 4.f, FV::Col(250, 240, 200));
			D.Circle(FVector2D(Win.X + 34.f, Win.Y + 9.f), 3.5f, Sky);
			D.Rect(FFVRect(Win.X + 8.f, Win.Y + 6.f, 1.f, 1.f), FV::Col(255, 255, 220));
			D.Rect(FFVRect(Win.X + 18.f, Win.Y + 12.f, 1.f, 1.f), FV::Col(255, 255, 220));
		}
		else if (!bRain)
		{
			D.Circle(FVector2D(Win.X + 33.f, Win.Y + 10.f), 5.f, FV::Col(255, 226, 110));
		}
		D.Rect(FFVRect(Win.X, Win.Bottom() - 9.f, Win.W, 9.f), FMath::Lerp(FV::Col(112, 178, 80), FV::Col(34, 58, 50), Night));
		if (bRain)
		{
			for (int32 r = 0; r < 7; ++r)
			{
				const float RX = Win.X + 3.f + float((r * 13) % 40);
				const float RY = Win.Y + FMath::Fmod(T * 60.f + float(r * 11), Win.H - 6.f);
				D.Line(FVector2D(RX, RY), FVector2D(RX - 2.f, RY + 5.f), FV::Col(210, 225, 245, 180), 1.f);
			}
		}
		D.Rect(FFVRect(Win.X + Win.W * 0.5f - 1.f, Win.Y, 2.f, Win.H), FVPal::Wood());
		D.Rect(FFVRect(Win.X, Win.Y + Win.H * 0.5f - 1.f, Win.W, 2.f), FVPal::Wood());
		D.Frame(Win.Inset(-2.f), FVPal::WoodDark(), 2.f);
		D.Rect(FFVRect(Win.X - 4.f, Win.Bottom() + 2.f, Win.W + 8.f, 3.f), FVPal::Wood());
	}

	// ---- try-on state (hover preview) ----
	FFVAppearance Look = P.BuildAppearance();
	EFVPetStyle PetStyle = P.GetPetStyle();
	EFVBobberStyle BobberStyle = P.GetBobberStyle();
	bool bTryOn = false;
	if (bHoverValid)
	{
		const EFVCosmeticSlot Slot = HoverSlot;   // the slot of the hovered (portrait: selected) item
		const FName Equipped = P.GetEquipped(Slot);
		const bool bDiffers = HoverEntry.Cos ? (HoverEntry.Cos->Id != Equipped) : !Equipped.IsNone();
		if (bDiffers)
		{
			bTryOn = true;
			if (HoverEntry.Cos)
			{
				ApplyToLook(Look, *HoverEntry.Cos);
				if (HoverEntry.Cos->Slot == EFVCosmeticSlot::Pet) { PetStyle = PetStyleOf(*HoverEntry.Cos); }
				if (HoverEntry.Cos->Slot == EFVCosmeticSlot::Bobber) { BobberStyle = BobberStyleOf(*HoverEntry.Cos); }
			}
			else if (Slot == EFVCosmeticSlot::Hat)
			{
				Look.Hat = EFVHatStyle::None;
			}
			else if (Slot == EFVCosmeticSlot::Pet)
			{
				PetStyle = EFVPetStyle::None;
			}
		}
	}

	UFVSprites* Sprites = UFVSprites::Get();

	// ---- bobber "painting" on the wall ----
	{
		const FFVRect Pic(Stage.Right() - 52.f, Stage.Y + 12.f, 40.f, 30.f);
		const bool bFocus = CurSlot() == EFVCosmeticSlot::Bobber;
		if (bFocus)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(T * 4.f);
			D.Rect(Pic.Inset(-5.f), FV::Col(255, 214, 90, uint8(60.f + 80.f * Pulse)));
		}
		D.Rect(FFVRect(Pic.X, Pic.Y, Pic.W, Pic.H * 0.5f), FV::Col(96, 172, 222));
		D.Rect(FFVRect(Pic.X, Pic.Y + Pic.H * 0.5f, Pic.W, Pic.H * 0.5f), FV::Col(58, 124, 192));
		D.Rect(FFVRect(Pic.X + 6.f, Pic.Y + 8.f, 6.f, 1.f), FV::Col(220, 240, 255, 150));
		D.Rect(FFVRect(Pic.X + 26.f, Pic.Y + 20.f, 8.f, 1.f), FV::Col(220, 240, 255, 150));
		if (Sprites && BobberStyle < EFVBobberStyle::Count)
		{
			const int32 Frame = int32(T * 2.f) % 2;
			D.SpriteCentered(Sprites->Bobber(BobberStyle, Frame), Pic.Center() + FVector2D(0.f, FMath::RoundToFloat(FMath::Sin(T * 3.f))), 2.f);
		}
		D.Frame(Pic.Inset(-2.f), FV::Col(150, 96, 40), 2.f);
		D.Frame(Pic.Inset(-3.f), FVPal::WoodDark());
		// nail + string
		D.Line(FVector2D(Pic.X + 4.f, Pic.Y - 3.f), FVector2D(Pic.Center().X, Pic.Y - 9.f), FVPal::WoodDark(), 1.f);
		D.Line(FVector2D(Pic.Right() - 4.f, Pic.Y - 3.f), FVector2D(Pic.Center().X, Pic.Y - 9.f), FVPal::WoodDark(), 1.f);
		D.Rect(FFVRect(Pic.Center().X - 1.f, Pic.Y - 10.f, 2.f, 2.f), FV::Col(90, 80, 80));
	}

	// ---- skirting board & wooden floor ----
	D.Rect(FFVRect(Stage.X, FloorY - 5.f, Stage.W, 5.f), FVPal::Wood());
	D.Rect(FFVRect(Stage.X, FloorY - 5.f, Stage.W, 1.f), FVPal::WoodLight());
	D.Rect(FFVRect(Stage.X, FloorY, Stage.W, Stage.Bottom() - FloorY), FV::Col(180, 112, 60));
	int32 PlankRow = 0;
	for (float Y = FloorY + 9.f; Y < Stage.Bottom(); Y += 9.f, ++PlankRow)
	{
		D.Rect(FFVRect(Stage.X, Y, Stage.W, 1.f), FV::Col(142, 84, 44));
		const float Off = (PlankRow % 2) ? 20.f : 46.f;
		for (float X = Stage.X + Off; X < Stage.Right(); X += 52.f)
		{
			D.Rect(FFVRect(X, Y - 8.f, 1.f, 8.f), FV::Col(150, 90, 48));
		}
	}

	// ---- rug ----
	const FVector2D Feet = bP ? FVector2D(FMath::RoundToFloat(Stage.Center().X - 8.f * CharScale), Stage.Bottom() - 26.f)
		: FVector2D(Stage.X + 76.f, Stage.Bottom() - 16.f);
	{
		const float RugCX = bP ? FMath::RoundToFloat(Stage.Center().X + 4.f) : Stage.X + Stage.W * 0.5f - 6.f;
		const float RugK = bP ? 1.4f : 1.f;   // wider rug on the wide portrait stage
		const float RugY = Feet.Y - 8.f;
		D.Rect(FFVRect(RugCX - 62.f * RugK, RugY, 124.f * RugK, 2.f), FV::Col(150, 44, 48));
		D.Rect(FFVRect(RugCX - 74.f * RugK, RugY + 2.f, 148.f * RugK, 12.f), FV::Col(170, 56, 58));
		D.Rect(FFVRect(RugCX - 62.f * RugK, RugY + 14.f, 124.f * RugK, 2.f), FV::Col(150, 44, 48));
		D.Rect(FFVRect(RugCX - 66.f * RugK, RugY + 5.f, 132.f * RugK, 1.f), FV::Col(236, 190, 90));
		D.Rect(FFVRect(RugCX - 66.f * RugK, RugY + 10.f, 132.f * RugK, 1.f), FV::Col(236, 190, 90));
	}

	// ---- character & pet ----
	const bool bWalking = FMath::Fmod(T, 6.f) >= 3.5f;
	const EFVFacing Facing = FacingAt(FacingIdx);
	// soft shadow
	D.Rect(FFVRect(Feet.X - 16.f, Feet.Y - 3.f, 32.f, 4.f), FV::Col(60, 20, 20, 70));
	D.Rect(FFVRect(Feet.X - 12.f, Feet.Y - 4.f, 24.f, 6.f), FV::Col(60, 20, 20, 50));
	if (Sprites)
	{
		const EFVCharAnim Anim = bWalking ? EFVCharAnim::Walk : EFVCharAnim::Idle;
		const int32 Frames = FMath::Max(1, UFVSprites::CharacterFrameCount(Anim));
		const int32 Frame = int32(T * (bWalking ? 7.f : 2.f)) % Frames;
		const FFVSprite Spr = Sprites->Character(Look, Anim, Facing, Frame);
		D.Sprite(Spr, Feet - UFVSprites::CharacterAnchor() * CharScale, CharScale);

		if (PetStyle != EFVPetStyle::None && PetStyle < EFVPetStyle::Count)
		{
			const float PetScale = bP ? CharScale : 3.f;
			const FVector2D PetFeet = bP ? FVector2D(Feet.X + 23.f * CharScale, Feet.Y + 2.f) : FVector2D(Stage.X + 150.f, Feet.Y + 2.f);
			EFVPetAnim PetAnim = EFVPetAnim::Idle;
			if (bWalking) { PetAnim = EFVPetAnim::Walk; }
			else if (FMath::Fmod(T, 6.f) > 1.8f) { PetAnim = EFVPetAnim::Sit; }
			const int32 PetFrames = FMath::Max(1, UFVSprites::PetFrameCount(PetAnim));
			const int32 PetFrame = int32(T * (bWalking ? 8.f : 2.5f)) % PetFrames;
			D.Rect(FFVRect(PetFeet.X - 12.f, PetFeet.Y - 3.f, 24.f, 3.f), FV::Col(60, 20, 20, 60));
			const FFVSprite PetSpr = Sprites->Pet(PetStyle, PetAnim, PetFrame);
			// faces the player (pet sprites face right by default)
			D.Sprite(PetSpr, FVector2D(PetFeet.X - PetSpr.W * PetScale * 0.5f, PetFeet.Y - PetSpr.H * PetScale), PetScale, FLinearColor::White, true);
		}
	}

	// ---- equip sparkles ----
	const float Since = float((Game ? Game->GetTime() : D.Time) - EquipFlashTime);
	if (Since >= 0.f && Since < 0.8f)
	{
		const float K = Since / 0.8f;
		const FVector2D Center = Feet + FVector2D(0.f, -70.f * CharScale / 3.f);
		for (int32 s = 0; s < 10; ++s)
		{
			const float Ang = float(s) / 10.f * 2.f * UE_PI + K * 2.f;
			const float Rad = 18.f + K * 46.f;
			const FVector2D Pt = Center + FVector2D(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad * 1.25f);
			const FLinearColor C = FV::Col(255, 236, 120, uint8(255.f * (1.f - K)));
			const float Px = FMath::FloorToFloat(Pt.X);
			const float Py = FMath::FloorToFloat(Pt.Y);
			D.Rect(FFVRect(Px - 2.f, Py, 5.f, 1.f), C);
			D.Rect(FFVRect(Px, Py - 2.f, 1.f, 5.f), C);
		}
	}

	// ---- "try-on" tag ----
	if (bTryOn)
	{
		const FString Tag = TEXT("Примерка");
		const float TW = D.MeasureText(Tag, 7.f).X + 12.f;
		const FFVRect TagR(Stage.Center().X - TW * 0.5f + 6.f, Stage.Y + 6.f, TW, 13.f);
		D.Rect(TagR, FV::Col(255, 248, 228));
		D.Frame(TagR, FV::Col(150, 100, 66));
		D.Text(Tag, FVector2D(TagR.Center().X, TagR.Y + 2.5f), 7.f, FVPal::Ink(), EFVAlign::Center);
	}

	D.Frame(Stage, FVPal::WoodDark());
}

} // namespace FVWardrobeScreenImpl
} // namespace

TUniquePtr<FVScreen> FVMakeWardrobeScreen(UFVGame* Game)
{
	return MakeUnique<FVWardrobeScreenImpl::FWardrobeScreen>(Game);
}
