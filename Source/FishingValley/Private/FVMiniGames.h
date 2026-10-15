// FVMiniGames.h — reusable pieces of the mini-games (update 4). Implemented in FVMiniGames.cpp.
//
// LURE WORKSHOP PANEL (FFVLureWorkshop): the whole lure-crafting UI — base / colour / glitter pickers with the
// ingredient each option adds, the live preview (the lure hanging in a little water box), «Нужно», «Собрать», the
// assembly animation, the discovery card and the grid of the 24 recipes. Used by
//   * the workshop's «Блёсны» tab (FVScreenStation.cpp, embedded mode: the card is shown inside the page), and
//   * the standalone EFVScreen::LureCraft mini-game screen (autotest + its bot; the screen shows the card itself).
// Usage (embedded), all immediate-mode like FVDraw:
//     TUniquePtr<FFVLureWorkshop> Lures = MakeUnique<FFVLureWorkshop>(Game, true);   Lures->Open();   // once
//     Tick:  Lures->Tick(Dt, In, bPageActive);            // timers, particles; keys only when bKeys
//     Draw:  Lures->Draw(D, PageArea, bInteractive, bButtonInside);   // inside the pager page (local coords)
//            Lures->DrawAssembleButton(D, FooterRect, bInteractive);  // portrait: the «Собрать» in the fixed footer
// Keys (embedded): ↑/↓ or W/S — row, A/D — variant (←/→ stay with the tabs), Space/Enter — «Собрать», Esc/Space — card.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "FVTypes.h"

class UFVGame;
class FVDraw;

namespace FVMiniGamesImpl { class FLureBench; }

class FFVLureWorkshop
{
public:
	/** bEmbedded: the panel lives inside another screen (shows its own discovery card inside Area, Left/Right arrows are
	    left to that screen). Otherwise the standalone LureCraft screen drives it. */
	FFVLureWorkshop(UFVGame* InGame, bool bEmbedded);
	~FFVLureWorkshop();
	FFVLureWorkshop(const FFVLureWorkshop&) = delete;
	FFVLureWorkshop& operator=(const FFVLureWorkshop&) = delete;

	/** Resets the panel and preselects an affordable (preferably undiscovered) recipe. */
	void Open();
	/** Per frame (paused under modals is fine): animations, particles, the assembly timer. bKeys: keyboard control. */
	void Tick(float Dt, const FFVInput& In, bool bKeys);
	/** The panel in Area. bButtonInside: landscape layouts put «Собрать» into the right column (else the caller draws
	    DrawAssembleButton somewhere, e.g. the portrait footer). */
	void Draw(FVDraw& D, const FFVRect& Area, bool bInteractive, bool bButtonInside);
	/** «Собрать» / «Сборка…» (disabled while not affordable / busy). Returns true on the frame the assembly started. */
	bool DrawAssembleButton(FVDraw& D, const FFVRect& R, bool bInteractive, float TextSize = 0.f);

	bool IsAssembling() const;
	bool IsCardShown() const;
	void CloseCard();
	/** One-line control hint (touch / mouse+keys wording). */
	FString Hint() const;

	/** Implementation access for FVMiniGames.cpp (the standalone screen). */
	FVMiniGamesImpl::FLureBench& Core();

private:
	TUniquePtr<FVMiniGamesImpl::FLureBench> Bench;
};

/** Update 5 (autotest only): what the cooking / digging screens set up and do, read by FVAutoTest (the screens are private
    to FVMiniGames.cpp). Written by the screens, never read by the game itself. */
struct FFVMiniGameDebug
{
	// cooking: FCookingScreen::OnOpen with a recipe that can be made
	int32 CookSerial = 0;          // +1 per cooking screen that set up a recipe
	FName CookRecipe;
	FName CookFish;                // the fish on the board (NAME_None: no fish)
	int32 CookCuts = 0;            // cut lines = the recipe's GameDifficulty
	float CookSpeed = 0.f;         // the CookSpeed multiplier every stage rate got (x1.0 / 1.1 / 1.2 / 1.4)
	int32 CookStages = 0;
	int32 CookVessel = -1;         // 0 pan, 1 pot, 2 kettle, 3 oven
	// digging: FDiggingScreen during its play phase
	bool bDigPlaying = false;
	float DigTimeLeft = 0.f;
	float DigTimeTotal = 0.f;
	int32 DigPenalties = 0;        // taps on empty dug holes
	float DigLastPenalty = 0.f;    // seconds the last one took
	FVector2D DigEmptyHole = FVector2D(-1.f, -1.f);   // centre of a dug hole without a worm that a tap would punish (x < 0: none)
};
FFVMiniGameDebug& FVMiniGameDebug();

class UFVSaveGame;
/** Update 6 (§9б «Русская печь»): from kitchen level 4 every kitchen recipe cooks two portions at once. Returns the recipe
    as it is cooked now (OutputCount doubled when so); the station screen and the cooking mini-game craft with it. */
FFVRecipeDef FVCookedRecipe(const UFVSaveGame& P, const FFVRecipeDef& R);
/** Output units of one cooking of R now (R.OutputCount, doubled at the «Русская печь»). */
int32 FVCookedCount(const UFVSaveGame& P, const FFVRecipeDef& R);
/** Kitchen level that cooks two portions (FVDBU6 «Русская печь»). */
constexpr int32 FVDoublePortionKitchenLevel = 4;
