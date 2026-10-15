#include "FVUIKit.h"
#include "FVAudio.h"

namespace FVUIKitImpl
{
	/** iOS-like rubber band: displacement X beyond an edge maps to a damped offset (never more than Dim). */
	float Rubber(float X, float Dim)
	{
		if (X <= 0.f || Dim <= 0.f) { return 0.f; }
		const float C = 0.55f;
		return (1.f - 1.f / (X * C / Dim + 1.f)) * Dim;
	}

	float RubberRange(float Raw, float Min, float Max, float Dim)
	{
		if (Raw < Min) { return Min - Rubber(Min - Raw, Dim); }
		if (Raw > Max) { return Max + Rubber(Raw - Max, Dim); }
		return Raw;
	}
}

namespace { int32 GPageChanges = 0; }

uint32 FVKit::NewKey()
{
	static uint32 Next = 0x6A000000u;
	return ++Next;
}

int32 FVKit::DebugPageChangeCount()
{
	return GPageChanges;
}

// =====================================================================================================================
// FFVScrollView
// =====================================================================================================================
void FFVScrollView::Update(FVDraw& D, const FFVRect& Area, float ContentH, bool bAllowWheel)
{
	using namespace FVUIKitImpl;
	if (Key == 0) { Key = FVKit::NewKey(); }
	Content = FMath::Max(0.f, ContentH);
	View = FMath::Max(1.f, Area.H);
	const float Dt = D.FrameDt();
	const float MaxO = MaxOffset();
	const FFVInput& In = D.Input();
	const FVector2D M = D.Mouse();

	// ---- a press on a gliding list stops it and does not click the row under the finger. Update 6 (§12.2): CancelTap
	// only cancels the click — the gesture still gets its axis when the finger moves on, so the same finger drags the
	// list again (before, the second swipe merely stopped the glide). The pager (below) benefits the same way.
	if (In.bLmbPressed && Area.Contains(M) && D.InClip(M) && (FMath::Abs(Vel) > 120.f || (!bDrag && FMath::Abs(Pos - FMath::Clamp(Pos, 0.f, MaxO)) > 2.f)))
	{
		Vel = 0.f;
		Target = FMath::Clamp(Pos, 0.f, MaxO);
		D.CancelTap();
	}

	// ---- start a drag: the press began inside the area and the gesture turned out vertical
	if (!bDrag && D.IsPointerDown() && D.IsDragGesture() && D.DragAxis() == FVDraw::EDragAxis::Vertical
		&& Area.Contains(D.PressPos()) && CanScroll() && !D.IsInputBlocked())
	{
		if (D.ClaimDrag(Key))
		{
			bDrag = true;
			DragStartPos = Pos;
			DragStartMouse = M.Y;
			Vel = 0.f;
		}
	}

	if (bDrag)
	{
		if (D.IsPointerDown())
		{
			const float Raw = DragStartPos - (M.Y - DragStartMouse);
			Pos = RubberRange(Raw, 0.f, MaxO, View);
			Target = Pos;
			Vel = 0.f;
			BarAlpha = 1.f;
		}
		else
		{
			bDrag = false;
			// Fling: content keeps the finger's velocity (up swipe = offset grows).
			Vel = FMath::Clamp(-D.PointerVelocity().Y, -4000.f, 4000.f);
			if (FMath::Abs(Vel) < 60.f) { Vel = 0.f; }
			Target = Pos;
		}
	}
	else
	{
		// ---- mouse wheel (desktop): smooth animated steps
		if (bAllowWheel && In.Wheel != 0 && !FV::IsTouchUI() && CanScroll() && Area.Contains(M) && D.InClip(M) && !D.IsInputBlocked())
		{
			const float Step = FMath::Clamp(View * 0.3f, 36.f, 96.f);
			Target = FMath::Clamp(FMath::Clamp(Target, 0.f, MaxO) - float(In.Wheel) * Step, 0.f, MaxO);
			Vel = 0.f;
			BarAlpha = 1.f;
		}

		if (FMath::Abs(Vel) > 0.f)
		{
			// ---- inertia with friction; strong braking + spring back when past an edge
			Pos += Vel * Dt;
			const bool bPast = Pos < 0.f || Pos > MaxO;
			Vel *= FMath::Exp(-Dt * (bPast ? 16.f : 3.2f));
			if (FMath::Abs(Vel) < 25.f || (bPast && FMath::Abs(Vel) < 200.f)) { Vel = 0.f; }
			Target = FMath::Clamp(Pos, 0.f, MaxO);
			BarAlpha = 1.f;
		}
		else
		{
			Target = FMath::Clamp(Target, 0.f, MaxO);
			Pos = FVKit::Approach(Pos, Target, Dt, 0.075f);
			if (FMath::Abs(Pos - Target) < 0.25f) { Pos = Target; }
		}
	}

	// Scrollbar visibility: bright while moving, faint (but visible) when idle and scrollable.
	const float IdleAlpha = CanScroll() ? 0.35f : 0.f;
	BarAlpha = IsMoving() ? 1.f : FVKit::Approach(BarAlpha, IdleAlpha, Dt, 0.35f);
}

void FFVScrollView::ScrollToShow(float Y, float H, bool bInstant)
{
	float NewT = Target;
	if (Y < NewT) { NewT = Y; }
	else if (Y + H > NewT + View) { NewT = Y + H - View; }
	ScrollTo(NewT, bInstant);
}

void FFVScrollView::ScrollTo(float NewOffset, bool bInstant)
{
	Target = FMath::Clamp(NewOffset, 0.f, MaxOffset());
	Vel = 0.f;
	if (bInstant) { Pos = Target; }
}

void FFVScrollView::DrawBar(FVDraw& D, const FFVRect& Area) const
{
	if (!CanScroll() || BarAlpha <= 0.01f) { return; }
	const float TrackH = Area.H - 4.f;
	if (TrackH < 8.f) { return; }
	const float W = FV::IsPortrait() ? 4.f : 3.f;
	const FFVRect Track(Area.Right() - W - 1.f, Area.Y + 2.f, W, TrackH);
	const float ThumbH = FMath::Max(16.f, TrackH * View / FMath::Max(1.f, Content));
	const float MaxO = FMath::Max(1.f, MaxOffset());
	// Shrink the thumb while rubber-banding past an edge (native feel).
	float Over = 0.f;
	if (Pos < 0.f) { Over = -Pos; }
	else if (Pos > MaxOffset()) { Over = Pos - MaxOffset(); }
	const float H = FMath::Max(8.f, ThumbH - Over * 0.5f);
	const float K = FMath::Clamp(Pos / MaxO, 0.f, 1.f);
	const FFVRect Thumb(Track.X, FMath::RoundToFloat(Track.Y + (TrackH - H) * K), W, FMath::RoundToFloat(H));
	D.Rect(Track, FV::Alpha(FV::Col(120, 72, 40), 0.18f * BarAlpha));
	D.Rect(Thumb, FV::Alpha(FV::Col(120, 66, 30), 0.85f * BarAlpha));
	D.Rect(FFVRect(Thumb.X, Thumb.Y, 1.f, Thumb.H), FV::Alpha(FV::Col(214, 150, 90), 0.8f * BarAlpha));
}

void FFVScrollView::DrawFades(FVDraw& D, const FFVRect& Area, const FLinearColor& Bg) const
{
	if (!CanScroll()) { return; }
	const float FH = FMath::Min(12.f, Area.H * 0.2f);
	if (Pos > 1.f)
	{
		D.GradientV(FFVRect(Area.X, Area.Y, Area.W, FH), FV::Alpha(Bg, 0.9f), FV::Alpha(Bg, 0.f), 6);
	}
	if (Pos < MaxOffset() - 1.f)
	{
		D.GradientV(FFVRect(Area.X, Area.Bottom() - FH, Area.W, FH), FV::Alpha(Bg, 0.f), FV::Alpha(Bg, 0.9f), 6);
	}
}

// =====================================================================================================================
// FFVPager
// =====================================================================================================================
void FFVPager::GoTo(int32 P)
{
	Page = FMath::Max(0, P);   // the upper clamp happens in Update (the page count may not be known yet)
}

void FFVPager::JumpTo(int32 P)
{
	P = FMath::Max(0, P);
	if (!bDrag && FMath::Abs(float(P) - Visual) > 1.f)
	{
		Visual = float(P) + (float(P) > Visual ? -1.f : 1.f);   // one slide instead of flying over every page
	}
	Page = P;
}

void FFVPager::SetPage(int32 P)
{
	Page = FMath::Max(0, P);
	Visual = float(Page);
	LastSeenPage = Page;
	bDrag = false;
}

void FFVPager::Update(FVDraw& D, const FFVRect& Area, int32 NumPages)
{
	using namespace FVUIKitImpl;
	if (Key == 0) { Key = FVKit::NewKey(); }
	NumPagesCached = FMath::Max(1, NumPages);
	Page = FMath::Clamp(Page, 0, NumPagesCached - 1);
	const float Dt = D.FrameDt();
	const float W = FMath::Max(1.f, Area.W);
	const float MaxP = float(NumPagesCached - 1);
	const FVector2D M = D.Mouse();

	if (!bDrag && NumPagesCached > 1 && D.IsPointerDown() && D.IsDragGesture()
		&& D.DragAxis() == FVDraw::EDragAxis::Horizontal && Area.Contains(D.PressPos()) && !D.IsInputBlocked())
	{
		if (D.ClaimDrag(Key))
		{
			bDrag = true;
			DragBase = Visual;          // interruptible: continue from wherever the slide is right now
			DragStartMouse = M.X;
			DragStartPage = Page;       // ...but count the swipe from the page the slide is heading to
		}
	}

	if (bDrag)
	{
		if (D.IsPointerDown())
		{
			const float Raw = DragBase - (M.X - DragStartMouse) / W;
			Visual = RubberRange(Raw, 0.f, MaxP, 0.6f);
		}
		else
		{
			bDrag = false;
			// Direction of the swipe: the flick velocity decides when it is clear, otherwise the distance travelled.
			const float VelPages = D.PointerVelocity().X / W;          // + = finger moved right = previous page
			const float MovedPages = (M.X - DragStartMouse) / W;
			int32 Dir = 0;                                              // +1 = next page, -1 = previous page
			if (FMath::Abs(VelPages) > 0.45f) { Dir = VelPages < 0.f ? 1 : -1; }
			else if (MovedPages < -0.12f) { Dir = 1; }
			else if (MovedPages > 0.12f) { Dir = -1; }
			int32 NewPage;
			if (Dir == 0)
			{
				NewPage = FMath::RoundToInt(Visual);
			}
			else
			{
				// Always one page further than the page the slide was heading to (so quick repeated swipes keep going),
				// or further still when the finger dragged across several pages.
				NewPage = DragStartPage + Dir;
				const int32 Reached = Dir > 0 ? FMath::FloorToInt(Visual + 0.5f) : FMath::CeilToInt(Visual - 0.5f);
				NewPage = Dir > 0 ? FMath::Max(NewPage, Reached) : FMath::Min(NewPage, Reached);
			}
			Page = FMath::Clamp(NewPage, 0, NumPagesCached - 1);
			bSwipeChange = true;
		}
	}
	else
	{
		Visual = FVKit::Approach(Visual, float(Page), Dt, 0.07f);
		if (FMath::Abs(Visual - float(Page)) < 0.0015f) { Visual = float(Page); }
	}

	Visual = FMath::Clamp(Visual, -0.6f, MaxP + 0.6f);
	bChanged = Page != LastSeenPage;
	if (bChanged)
	{
		LastSeenPage = Page;
		++GPageChanges;
		// Swipes get a soft page sound; tab buttons / keys already play their own click.
		if (bSwipeChange)
		{
			if (UFVAudio* A = UFVAudio::Get()) { A->Play(EFVSfx::Click, 0.35f, 1.15f); }
		}
	}
	bSwipeChange = false;
}

// =====================================================================================================================
// Tab strip & hints
// =====================================================================================================================
bool FVKit::TabStrip(FVDraw& D, const FFVRect& Row, const TArray<FString>& Labels, FFVPager& Pager, const TArray<int32>* Badges)
{
	const int32 N = Labels.Num();
	if (N <= 0) { return false; }
	const bool bBig = Row.H >= 24.f;
	const float Gap = bBig ? 4.f : 2.f;
	bool bClicked = false;
	for (int32 i = 0; i < N; ++i)
	{
		const FFVRect Cell = FVDraw::SplitH(Row, i, N, Gap);
		const bool bActive = i == Pager.Page;
		bool bHit = false;
		if (bBig)
		{
			bHit = D.Button(Cell, Labels[i], bActive ? EFVButton::Primary : EFVButton::Normal, true, FV::Ui::Body());
		}
		else
		{
			bHit = D.Tab(Cell, Labels[i], bActive);
		}
		if (bHit && !bActive)
		{
			Pager.GoTo(i);
			bClicked = true;
		}
		if (Badges && Badges->IsValidIndex(i) && (*Badges)[i] > 0)
		{
			const FString B = FString::FromInt((*Badges)[i]);
			const float S = FV::Ui::Small();
			const FVector2D Sz = D.MeasureText(B, S, true);
			const FFVRect Chip(FMath::RoundToFloat(Cell.Right() - Sz.X - 7.f), Cell.Y - 4.f, Sz.X + 6.f, Sz.Y + 1.f);
			D.Rect(Chip, FV::Col(176, 44, 36));
			D.Text(B, FVector2D(Chip.X + 3.f, Chip.Y), S, FVPal::White(), EFVAlign::Left, false, true);
		}
	}
	// Animated indicator under the row, following the slide position.
	if (N > 1)
	{
		const float V = FMath::Clamp(Pager.VisualPos(), 0.f, float(N - 1));
		const int32 I0 = FMath::FloorToInt(V);
		const int32 I1 = FMath::Min(N - 1, I0 + 1);
		const float T = V - float(I0);
		const FFVRect C0 = FVDraw::SplitH(Row, I0, N, Gap);
		const FFVRect C1 = FVDraw::SplitH(Row, I1, N, Gap);
		const float X = FMath::Lerp(C0.X, C1.X, T) + 6.f;
		const float W = FMath::Lerp(C0.W, C1.W, T) - 12.f;
		const float H = bBig ? 3.f : 2.f;
		D.Rect(FFVRect(FMath::RoundToFloat(X), Row.Bottom() + (bBig ? 2.f : 0.f), FMath::RoundToFloat(W), H), FVPal::Gold());
	}
	return bClicked;
}

void FVKit::SwipeHint(FVDraw& D, const FFVRect& Area, const FFVPager& Pager)
{
	if (Pager.IsAnimating() || Pager.NumPagesCached <= 1) { return; }
	const float A = 0.35f + 0.2f * FMath::Sin(float(D.Time) * 3.f);
	const float S = FV::IsPortrait() ? 5.f : 4.f;
	const float CY = FMath::RoundToFloat(Area.Center().Y);
	const FLinearColor C = FV::Alpha(FV::Col(120, 72, 40), A);
	if (Pager.Page > 0)
	{
		for (int32 i = 0; i < int32(S); ++i)
		{
			D.Rect(FFVRect(Area.X + 2.f + float(i), CY - float(i), 1.f, float(i) * 2.f + 1.f), C);
		}
	}
	if (Pager.Page < Pager.NumPagesCached - 1)
	{
		for (int32 i = 0; i < int32(S); ++i)
		{
			D.Rect(FFVRect(Area.Right() - 3.f - float(i), CY - float(i), 1.f, float(i) * 2.f + 1.f), C);
		}
	}
}
