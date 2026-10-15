// Gesture controls for phones (and desktop testing with -fvtouch, where the mouse acts as one finger).
// No on-screen buttons:
//   * a finger that starts moving on the world  -> floating joystick anchored where it first touched (walk)
//   * a finger held still near the water         -> the action (wind up while held, cast on release)
//   * while fishing (bobber out, bite, reeling, catch card) any touch on the world is the action
//   * touches on UI (HUD bars, overlay screens, dialogs) act as the mouse pointer (one finger). Drags are NOT turned
//     into wheel steps: scroll views / pagers read the pointer drag themselves (FVDraw gestures, Private/FVUIKit.h)

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class FVDraw;

class FVTouchControls
{
public:
	/**
	 * Converts In.Touches into movement, action and pointer input (modifies In in place).
	 * bWorldControls: the world is interactive (no overlay screen / modal).
	 * bFishingIdle:   fishing is idle -> gestures decide between walking and casting.
	 * bCanCast:       a still hold may start a cast here (the player stands by the water).
	 */
	void Process(FFVInput& In, float Dt, bool bWorldControls, bool bFishingIdle, bool bCanCast);

	/** Draws the floating joystick while the player swipes. */
	void Draw(FVDraw& D, bool bWorldControls);

	bool IsWalking() const { return bJoyActive; }
	bool IsWorldFingerDown() const;

	/** World objects: while a Pending finger rests on a point that satisfies this predicate (e.g. an interactive world
	    object) it never becomes a cast. A short still tap (Age < 0.35 s, travel <= 9 px) on the world sets
	    In.bWorldTap / In.WorldTapPos either way.
	    Update 6 (§2): every position of this class (fingers, In.WorldTapPos, the predicates' argument) is in SCREEN
	    virtual px; whoever looks up world objects converts with FVScene::ScreenToWorld (UFVGame does). */
	void SetCastBlocker(TFunction<bool(const FVector2D&)> InFn) { CastBlocker = MoveTemp(InFn); }
	/** Widgets over the world (e.g. the «give up» [X] of the practice minigame): a finger landing where this predicate
	    is true is the pointer for the UI, never the reel / cast action. */
	void SetUiTest(TFunction<bool(const FVector2D&)> InFn) { UiTest = MoveTemp(InFn); }

private:
	enum class ERole : uint8 { Pointer, Pending, Joystick, Action };
	struct FTrack
	{
		int32 Id = 0;
		ERole Role = ERole::Pointer;
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D Pos = FVector2D::ZeroVector;
		float Age = 0.f;
	};
	TArray<FTrack> Tracks;

	FVector2D JoyOrigin = FVector2D::ZeroVector;
	FVector2D JoyKnob = FVector2D::ZeroVector;
	bool bJoyActive = false;
	float JoyFade = 0.f;          // fades the joystick out after the finger lifts
	TFunction<bool(const FVector2D&)> CastBlocker;   // true = no cast from a still finger here (world object)
	TFunction<bool(const FVector2D&)> UiTest;        // true = a widget over the world is here (SetUiTest)

	/** HUD strips: the top bar, and in portrait the bottom button dock (landscape has no bottom bar any more). The update-6
	    button column under the bar is reported by UFVGame through SetUiTest (its rect changes with the layout). */
	static bool IsUiArea(const FVector2D& P);
	/** IsUiArea or a widget over the world (UiTest). */
	bool IsUiAt(const FVector2D& P) const { return IsUiArea(P) || (UiTest && UiTest(P)); }
};
