// Overlay screens (menus, shop, inventory...). Each screen's class lives entirely in its .cpp;
// UFVGame creates them through the factory functions below.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class UFVGame;
class FVDraw;

class FVScreen
{
public:
	explicit FVScreen(UFVGame* InGame) : Game(InGame) {}
	virtual ~FVScreen() = default;

	virtual void OnOpen() {}
	virtual void OnClose() {}
	/** Keyboard shortcuts & per-frame logic. ESC closes the screen (handled by UFVGame) unless HandlesEscape() is true. */
	virtual void Tick(float Dt, const FFVInput& In) {}
	/** IMGUI: draw + handle mouse. For overlays the (dimmed, paused) world is already drawn beneath. */
	virtual void Draw(FVDraw& D) = 0;
	virtual bool HandlesEscape() const { return false; }
	/** false for full-screen screens (main menu) that draw their own background. */
	virtual bool DrawsWorldBelow() const { return true; }

protected:
	UFVGame* Game = nullptr;
};

TUniquePtr<FVScreen> FVMakeMainMenuScreen(UFVGame* Game);     // FVScreenMenus.cpp — title, New game / Continue / Exit
TUniquePtr<FVScreen> FVMakePauseScreen(UFVGame* Game);        // FVScreenMenus.cpp — resume, settings (volume, fullscreen), help, save & main menu, quit
TUniquePtr<FVScreen> FVMakeMapScreen(UFVGame* Game);          // FVScreenMap.cpp — world map, travel, unlock locations
TUniquePtr<FVScreen> FVMakeShopScreen(UFVGame* Game);         // FVScreenShop.cpp — rods, bait, cosmetics, keepnet upgrades
TUniquePtr<FVScreen> FVMakeInventoryScreen(UFVGame* Game);    // FVScreenInventory.cpp — keepnet (sell), rod & bait selection
TUniquePtr<FVScreen> FVMakeWardrobeScreen(UFVGame* Game);     // FVScreenWardrobe.cpp — equip hats/outfits/bobbers/pets
TUniquePtr<FVScreen> FVMakeCollectionScreen(UFVGame* Game);   // FVScreenCollection.cpp — fish encyclopedia
TUniquePtr<FVScreen> FVMakeAchievementsScreen(UFVGame* Game); // FVScreenCollection.cpp — achievements list

// ---- update 2 (context: UFVGame::GetScreenContext(); see Docs/UPDATE_HOME.md §Screens)
TUniquePtr<FVScreen> FVMakeChestScreen(UFVGame* Game);        // FVScreenStorage.cpp — backpack <-> chest
TUniquePtr<FVScreen> FVMakeIceCellarScreen(UFVGame* Game);    // FVScreenStorage.cpp — keepnet <-> ice cellar
TUniquePtr<FVScreen> FVMakeStationScreen(UFVGame* Game);      // FVScreenStation.cpp — build/upgrade + station content (context = station id)
TUniquePtr<FVScreen> FVMakeOrdersScreen(UFVGame* Game);       // FVScreenOrders.cpp — order board
TUniquePtr<FVScreen> FVMakeNetSpotScreen(UFVGame* Game);      // FVScreenNet.cpp — place / collect / repair a net (context = "<loc>:<spot>")
TUniquePtr<FVScreen> FVMakeHomeDecorScreen(UFVGame* Game);    // FVScreenDecor.cpp — house decor catalog
TUniquePtr<FVScreen> FVMakeCookingScreen(UFVGame* Game);      // FVMiniGames.cpp — cooking mini-game (context = recipe id)
TUniquePtr<FVScreen> FVMakeLureCraftScreen(UFVGame* Game);    // FVMiniGames.cpp — lure workshop
TUniquePtr<FVScreen> FVMakeKnotsScreen(UFVGame* Game);        // FVMiniGames.cpp — knots "repeat the pattern" (context = recipe id)
TUniquePtr<FVScreen> FVMakeDiggingScreen(UFVGame* Game);      // FVMiniGames.cpp — digging worms (context = "<loc>:<spot>")

// ---- update 3
TUniquePtr<FVScreen> FVMakeTutorialScreen(UFVGame* Game);     // FVScreenTutorial.cpp — illustrated guide (context = entry id to open, or None)
/** Number of guide entries unlocked for the current profile that the player has not opened yet (HUD badge). */
int32 FVTutorialUnseenCount(UFVGame* Game);                   // FVScreenTutorial.cpp

// ---- update 4
TUniquePtr<FVScreen> FVMakeProfileScreen(UFVGame* Game);      // FVScreenProfile.cpp (context: None / "setup" / "<pid>")
TUniquePtr<FVScreen> FVMakeLeaderboardScreen(UFVGame* Game);  // FVScreenLeaderboard.cpp
TUniquePtr<FVScreen> FVMakeMailScreen(UFVGame* Game);         // FVScreenMail.cpp
TUniquePtr<FVScreen> FVMakeRestoreScreen(UFVGame* Game);      // FVScreenRestore.cpp
TUniquePtr<FVScreen> FVMakeLevelsScreen(UFVGame* Game);       // FVScreenLevels.cpp

// ---- update 6 (Docs/UPDATE6_FOREST_CITY.md §17; the context of each screen is documented in EFVScreen)
TUniquePtr<FVScreen> FVMakeChoppingScreen(UFVGame* Game);     // FVMiniGameWood.cpp   [MINI-WOOD]
TUniquePtr<FVScreen> FVMakeSawingScreen(UFVGame* Game);       // FVMiniGameWood.cpp   [MINI-WOOD]
TUniquePtr<FVScreen> FVMakeForgingScreen(UFVGame* Game);      // FVMiniGameStone.cpp  [MINI-STONE]
TUniquePtr<FVScreen> FVMakeExcavationScreen(UFVGame* Game);   // FVMiniGameStone.cpp  [MINI-STONE]
TUniquePtr<FVScreen> FVMakeBuilderScreen(UFVGame* Game);      // FVScreenBuilder.cpp  [SCREEN-YARD]
TUniquePtr<FVScreen> FVMakeYardBuildingScreen(UFVGame* Game); // FVScreenYard.cpp     [SCREEN-YARD]
TUniquePtr<FVScreen> FVMakeGardenBedScreen(UFVGame* Game);    // FVScreenYard.cpp     [SCREEN-YARD]
TUniquePtr<FVScreen> FVMakeBoatScreen(UFVGame* Game);         // FVScreenBoat.cpp     [SCREEN-YARD]
TUniquePtr<FVScreen> FVMakeVoyageScreen(UFVGame* Game);       // FVScreenBoat.cpp     [SCREEN-YARD]
TUniquePtr<FVScreen> FVMakeCityBoardScreen(UFVGame* Game);    // FVScreenCityBoard.cpp [CITY]
TUniquePtr<FVScreen> FVMakeArtelScreen(UFVGame* Game);        // FVScreenArtel.cpp    [CITY]
TUniquePtr<FVScreen> FVMakeExpertTackleScreen(UFVGame* Game); // FVScreenExpertTackle.cpp [SCREEN-TACKLE]
TUniquePtr<FVScreen> FVMakeMillScreen(UFVGame* Game);         // FVScreenMill.cpp [MILL-SCREENS] — context "mill" / "pen" (§18)
/** Р43 (§12.3): the first time a mechanic appears (first forest visit, first expert bite, first hazard, first boulder...)
    — toast «Новое в учебнике: …» that opens the entry (once per entry id, e.g. "g_forest"). FVScreenTutorial.cpp [SCREEN-MISC]. */
void FVTutorialAnnounce(UFVGame* Game, FName EntryId);
