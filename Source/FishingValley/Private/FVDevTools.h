// Update 6 (Р47): the extended developer mode «Режим разработчика» — a scrollable panel in Пауза → Настройки that lets
// the owner check most features by hand and walk every location (teleport, progress, item sets, world, time & weather,
// expert fights, the City). Opened by FVScreenMenus.cpp (the pause screen) when UFVSaveGame::bDevTools is on.
// Owner rule: test builds only — nothing of this exists in a Shipping build (every APK for players).

#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "FVUIKit.h"

class FVDraw;
class UFVGame;

/** Panel state (a member of the pause screen). Draw() also runs the pressed action. */
struct FFVDevPanel
{
	enum class EResult : uint8 { None, Back, Closed };

	/** Draws the panel for this frame. Back = «Назад» pressed (show the settings again); Closed = the action closed the
	    screen (teleport, expert fight): the caller must return at once without touching its members. */
	EResult Draw(FVDraw& D, UFVGame* Game);
	void Reset() { Scroll.Reset(); LastContentH = 0.f; }

	FFVScrollView Scroll;
	float LastContentH = 0.f;       // content height of the previous frame (the scroll view needs it before drawing)
	int32 RodType = 0;              // EFVExpertRodType picked for the expert fights

};

#endif // !UE_BUILD_SHIPPING
