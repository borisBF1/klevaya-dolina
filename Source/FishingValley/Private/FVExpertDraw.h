// UPDATE 6 — drawing of the expert fight, edition 2 «одна вертикаль» (Docs/UPDATE6_FOREST_CITY.md §19, Р60, Р54).
// [EXPERT-DRAW] FVExpertDraw.cpp. The look is the classic minigame panel (FVFishing.cpp draws it for FFVMinigame): the
// brown wooden panel beside the hero, the water track, the green bar(s), the fish icon seen from the side, the vertical
// progress meter + a narrow tension meter next to it. The panel is taller than the classic one (portrait ~1.3x, kept
// between the top strip and the bottom dock; landscape — the classic height), the 760-unit track (FVExpert::TrackH)
// is drawn in the panel's scale. A rod adds only a thin accent stripe + the type badge (Р54). Hazards are plain marks
// on the track. Layout is computed from FV::VW / FV::VH every frame (both orientations).
// «Field» below = the whole wooden panel rect returned by FieldRect (every other call takes it).
// Called by FVFishing.cpp [FISHING-EXPERT] (screen space, after the world view is popped).

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class FVDraw;
struct FFVExpertFight;

namespace FVExpertDraw
{
	/** The wooden panel on screen, beside the hero on the roomier side (like the classic panel), never over him.
	    Portrait: ~1.3x the classic panel height, kept between the top strip (+ a banner line) and the bottom dock;
	    landscape: the classic height (300). Remembers HeroScreenX for the hint / pictogram placement of this frame. */
	FFVRect FieldRect(float HeroScreenX);
	/** The water track inside the panel (FVExpert::TrackH units map onto its height). */
	FFVRect TrackRect(const FFVRect& Field);
	/** Track Y (0 = top .. FVExpert::TrackH, centres) -> screen Y, and back (clamped to the track). The Slider
	    («Ползунок»): FFVExpertInput::TargetY = ScreenToTrack(Field, finger / mouse screen Y) — any point on the screen. */
	float TrackToScreen(const FFVRect& Field, float TrackY);
	float ScreenToTrack(const FFVRect& Field, float ScreenY);
	/** Where a tap / hold counts: everything under the top strip (minus the caller's [X]). */
	FFVRect ControlRect(const FFVRect& Field);
	/** The panel itself (kept for callers of the edition-1 API: the banner / hint anchor). */
	FFVRect FrameRect(const FFVRect& Field);
	/** Progress meter (vertical, right of the track) and the narrow tension meter right of it. */
	FFVRect ProgressBarRect(const FFVRect& Field);
	FFVRect TensionBarRect(const FFVRect& Field);
	/** The whole fight: panel, water, hazards, treasure, bar(s) with the rod cues, fish (+ ghost / bubbles), meters,
	    the move signal pictogram beside the panel, the ×1,5 mark, the [X] look of a training fight. */
	void Draw(FVDraw& D, const FFVExpertFight& F, EFVExpertTheme Theme, const FFVRect& Field, float Time);
	/** «Ghost hand» of the first fight of a new rod type (3 s). T = seconds since start. */
	void DrawTutorialHand(FVDraw& D, const FFVExpertFight& F, const FFVRect& Field, float T);
	/** The [X] of a training fight (give up): beside the panel top, away from the hero. Draw paints its look when
	    Setup.bTutorial; the caller takes the tap with D.Clicked / D.CloseButton(GiveUpRect(Field)). */
	FFVRect GiveUpRect(const FFVRect& Field);
	/** Seconds the «ghost hand» is shown; DrawTutorialHand draws nothing for T >= this. */
	constexpr float TutorialHandSec = 3.f;
	/** Seconds the short rod hint (DrawHowTo) stays on screen at the start of a first / training fight. */
	constexpr float HowToSec = 8.f;
	/** Short rod hint (1-3 wrapped lines on a soft plate): beside the panel on the free side of the hero, at the panel
	    top — never over the track or the meters. T = seconds since the fight start; nothing for T >= HowToSec. */
	void DrawHowTo(FVDraw& D, EFVExpertRodType Type, const FFVRect& Field, float T);
	/** A readable gold banner for the expert / legend fight (replaces drawing over the narrow panel): portrait —
	    one line centred above the panel (below it when there is no room); landscape — up to 2 lines beside the panel
	    on the side away from the hero. Age = seconds shown; nothing for Age >= Life. */
	void DrawBanner(FVDraw& D, const FString& Text, float Age, float Life, const FFVRect& Field);
}
