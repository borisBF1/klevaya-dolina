// Shared touch-friendly UI building blocks (update 3): smooth kinetic scrolling and swipeable sliding pages.
// Everything is immediate-mode like FVDraw: keep one helper object per list / tab set as a member of your screen and
// call it every frame from Draw(). Implemented in FVUIKit.cpp.
//
// ---------------------------------------------------------------------------------------------------------------------
// SMOOTH SCROLLING (vertical lists, grids, long texts)
//     FFVScrollView Scroll;                                   // member
//     const FFVRect Area = ...;                               // visible list area
//     Scroll.Update(D, Area, ContentH);                       // input (finger/mouse drag, wheel, fling, edge bounce)
//     D.PushClip(Area);
//     for (rows) { const float Y = Area.Y - Scroll.Offset() + RowTop; if (Y + RowH < Area.Y || Y > Area.Bottom()) continue; ... }
//     D.PopClip();
//     Scroll.DrawBar(D, Area);                                // thin indicator (visible while scrolling / when scrollable)
//     Scroll.DrawFades(D, Area, BgColor);                     // optional soft top/bottom fades hinting at more content
//   * Content moves 1:1 with the finger, keeps gliding after release (inertia), rubber-bands past the ends.
//   * Mouse wheel scrolls smoothly (animated). Keyboard selection: Scroll.ScrollToShow(RowTop, RowH).
//   * Rows/buttons inside keep using D.Button / D.Slot / D.Clicked: clicks fire on release and never after a drag.
//
// SWIPEABLE PAGES (tabs)
//     FFVPager Pager;                                         // member; Pager.Page = the active tab
//     if (TabClicked(i)) Pager.GoTo(i);                       // tab buttons: animated too
//     Pager.Update(D, Area, NumTabs);                         // horizontal swipe + slide animation
//     Pager.DrawPages(D, Area, [&](int32 Page) { DrawTabContent(D, Page); });   // clipped; each page translated
//   * Inside DrawPage use LOCAL coordinates exactly as if the page were not moving (the helper translates both the
//     drawing and hit-testing). Use D.Mouse() (NOT D.Input().Mouse) and per-page state (selection/scroll per tab).
//   * Swipes are interruptible: the user can swipe back and forth without waiting for the animation.
//   * Vertical scrolling inside a page works: the first 9 px of movement decide the axis (horizontal = pager,
//     vertical = scroll views).
//   * FVKit::TabStrip() draws a standard tab row with an animated active indicator that follows the slide.
// ---------------------------------------------------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVDraw.h"

/** Kinetic vertical scrolling for one area. */
struct FFVScrollView
{
	/** Per frame, before drawing the content. ContentH = total content height (px). bAllowWheel: react to the mouse
	    wheel (set false when another scroll area under the pointer should get it). */
	void Update(FVDraw& D, const FFVRect& Area, float ContentH, bool bAllowWheel = true);
	/** Current pixel-snapped offset (0 = top). May be slightly negative / beyond the end during the bounce. */
	float Offset() const { return FMath::RoundToFloat(Pos); }
	float MaxOffset() const { return FMath::Max(0.f, Content - View); }
	bool CanScroll() const { return Content > View + 0.5f; }
	/** Makes content range [Y, Y + H] visible (animated unless bInstant). */
	void ScrollToShow(float Y, float H, bool bInstant = false);
	void ScrollTo(float NewOffset, bool bInstant = false);
	void Reset() { Pos = Target = Vel = 0.f; bDrag = false; BarAlpha = 0.f; }
	/** True while the finger drags it or it glides / bounces. */
	bool IsMoving() const { return bDrag || FMath::Abs(Vel) > 1.f || FMath::Abs(Target - Pos) > 0.5f; }
	/** Thin scrollbar at the right edge of Area (fades in while moving, faint while idle if scrollable). */
	void DrawBar(FVDraw& D, const FFVRect& Area) const;
	/** Soft gradient at the top/bottom edge when more content is hidden there (Bg = the panel colour behind). */
	void DrawFades(FVDraw& D, const FFVRect& Area, const FLinearColor& Bg) const;

	// state (public for debugging)
	float Pos = 0.f;
	float Target = 0.f;         // wheel / keyboard / edge target
	float Vel = 0.f;            // fling velocity, px/s (positive = content moves up = offset grows)
	bool bDrag = false;
	float DragStartPos = 0.f;
	float DragStartMouse = 0.f;
	float Content = 0.f;
	float View = 0.f;
	float BarAlpha = 0.f;
	uint32 Key = 0;             // unique drag-claim key (assigned on first use)
};

/** Horizontal sliding pages (tabs) with swipe. */
struct FFVPager
{
	/** Animate to page P (clamped in Update). Safe to call every frame with the same value. */
	void GoTo(int32 P);
	/** Like GoTo, but a jump over several pages plays as ONE slide (starts from the page next to the target). */
	void JumpTo(int32 P);
	/** Jump without animation (e.g. in OnOpen; works before the first Update). */
	void SetPage(int32 P);
	/** Per frame: swipe input over Area + the slide animation. */
	void Update(FVDraw& D, const FFVRect& Area, int32 NumPages);
	/** Draws the visible page(s) clipped to Area, translating each by its slide offset. DrawPage(Page) draws in local
	    coordinates as if the page were at rest. */
	template<typename FnType>
	void DrawPages(FVDraw& D, const FFVRect& Area, FnType&& DrawPage)
	{
		const float W = FMath::Max(1.f, Area.W);
		const int32 First = FMath::FloorToInt(Visual);
		D.PushClip(Area);
		for (int32 P = First; P <= First + 1; ++P)
		{
			if (P < 0 || P >= NumPagesCached) { continue; }
			const float Dx = FMath::RoundToFloat((float(P) - Visual) * W);
			if (FMath::Abs(Dx) >= W) { continue; }
			D.PushTranslate(FVector2D(Dx, 0.f));
			DrawPage(P);
			D.PopTranslate();
		}
		D.PopClip();
	}
	/** Fractional visual position (page units) — e.g. for an indicator under the tabs. */
	float VisualPos() const { return Visual; }
	bool IsAnimating() const { return bDrag || FMath::Abs(Visual - float(Page)) > 0.002f; }
	/** True for the frame the target page changed (by swipe, key or GoTo) — play a sound / reset state. */
	bool PageChangedThisFrame() const { return bChanged; }

	int32 Page = 0;             // target page (highlight this tab)
	float Visual = 0.f;         // animated position in pages
	bool bDrag = false;
	float DragBase = 0.f;       // Visual at the moment the drag started
	float DragStartMouse = 0.f;
	int32 DragStartPage = 0;    // target page when the drag started (a swipe during the slide continues from it)
	int32 NumPagesCached = 1;
	bool bChanged = false;
	bool bSwipeChange = false;   // the pending change came from a swipe (plays the page sound; tab clicks have their own)
	int32 LastSeenPage = 0;
	uint32 Key = 0;
};

namespace FVKit
{
	/** Unique keys for drag claims (never 0). */
	uint32 NewKey();
	/** Total page changes of all pagers since start (autotest checks that a swipe really switched a tab). */
	int32 DebugPageChangeCount();

	/** Standard tab row: equal cells in Row, labels, click → Pager.GoTo, animated underline following the slide.
	    Style: parchment tabs like FVDraw::Tab in landscape, big finger-sized buttons in portrait (Row.H decides).
	    Returns true on the frame a tab was clicked. Badges (optional, same count as Labels): small number chips. */
	bool TabStrip(FVDraw& D, const FFVRect& Row, const TArray<FString>& Labels, FFVPager& Pager, const TArray<int32>* Badges = nullptr);

	/** Horizontal swipe arrows hint ("‹ ›") drawn at the edges of Area while the pager is idle and has more pages. */
	void SwipeHint(FVDraw& D, const FFVRect& Area, const FFVPager& Pager);

	/** Critically-damped approach helper: moves Cur towards Target with a time constant (seconds), frame-rate independent. */
	inline float Approach(float Cur, float Target, float Dt, float TimeConst)
	{
		if (TimeConst <= 0.f) { return Target; }
		return FMath::Lerp(Cur, Target, 1.f - FMath::Exp(-Dt / TimeConst));
	}
}
