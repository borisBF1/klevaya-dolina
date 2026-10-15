// UPDATE 6 — game-flow hooks (Docs/UPDATE6_FOREST_CITY.md §17). [GAME-FLOW] implements them in FVGameU6.cpp.
// UFVGame (FVGame.cpp / FVGameHome.cpp, owned by CORE-HUD) calls these at fixed points; FVGameU6 is a friend of
// UFVGame. The hook call sites are frozen: GAME-FLOW changes only FVGameU6.cpp (and may add private helpers there).

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class UFVGame;
class FVDraw;

class FVGameU6
{
public:
	/** UFVGame::EnterWorld, after the scene is loaded: artel report toasts, pending first-arrival flow. */
	static void OnEnterWorld(UFVGame& G);
	/** UFVGame::Tick every frame while in game. bWorldRuns = the world is simulated (no screen, no modal):
	    played minutes (+ garden / regrowth), walk-through exits (FVScene::GetPendingExit), timed banners. */
	static void Tick(UFVGame& G, float Dt, bool bWorldRuns);
	/** UFVGame::Interact, first thing: update-6 object kinds (tree, junk pile, boulder, villager, plot, yard building,
	    garden bed, boat, blockage, rubble, exit) and update-6 behaviour of old kinds (net spots at expert locations,
	    Р35). true = handled (UFVGame does nothing else). */
	static bool Interact(UFVGame& G, const FFVWorldObject& Obj);
	/** UFVGame::TravelTo / TravelToEntry after the scene loaded (From = previous location): first arrival in the City
	    (level 11, banner «Вы вступили в экспертную зону», the training fish), music of the new themes. */
	static void OnTravel(UFVGame& G, FName From, FName To);
	/** UFVGame::AdvanceClock, per step after SimulatePassive: artel nets, morning collection (06:00). */
	static void OnAdvanceClock(UFVGame& G, double FromAbs, double ToAbs, bool bQuiet);
	/** UFVGame::StartNewDay: City tasks refresh, artel lines. Lines = the summary lines (toasts / sleep log). */
	static void OnNewDay(UFVGame& G, TArray<FString>& Lines);
	/** UFVGame::HandleCatch, before anything is recorded: scrap junk -> mat_scrap, expert catch rewards / XP. */
	static void BeforeCatch(UFVGame& G, FFVCatchResult& Result);
	/** UFVGame::HandleCatch, after achievements: journal page facts, expert records, the «легенда сорвалась» logic. */
	static void AfterCatch(UFVGame& G, FFVCatchResult& Result);
	/** UFVGame::MakeCatchContext, at the end (bExpertRod, bExpertZone...). */
	static void FillCatchContext(const UFVGame& G, FFVCatchContext& Ctx);
	/** UFVGame::Draw, after DrawWorldHud (screen space, above the HUD, below overlay screens): big banners. */
	static void DrawOverlay(UFVGame& G, FVDraw& D);
};
