// Scripted self-test (command line -fvautotest): walks through every screen and location, fishes one fish with
// the fishing bot, takes screenshots into Saved/AutoTest, logs a balance table and a sprite contact sheet, then quits.
// Every step is logged as "FV: AUTOTEST step ..."; the run ends with "FV: AUTOTEST DONE" (or "FV: AUTOTEST TIMEOUT").
// 0.5 streamlining: every feature is checked once and every screen is shot once per state (fresh: only the map, the
// shop and the achievements, whose locked states the rich profile never shows; the other fresh screens are only
// opened); fights whose bookkeeping is checked (not their balance) are finished quickly with SetFightProgress; the
// mini-game bots take their own play / result shots.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVFishing.h"

class UFVGame;
class UFVSaveGame;

class FVAutoTest
{
public:
	explicit FVAutoTest(UFVGame* InGame);
	~FVAutoTest();

	/** Advances the script. Replaces the player's input with the script's synthetic input for this frame. */
	void Tick(float Dt, FFVInput& InOutInput);
	bool IsFinished() const { return bFinished; }

private:
	struct FStep
	{
		FString Name;
		TFunction<void()> Enter;                 // runs once when the step starts
		TFunction<bool(float StepTime)> Until;   // polled every frame; nullptr = done immediately
		float Timeout = 15.f;                    // seconds; on timeout the step is abandoned with a warning
		FString Shot;                            // screenshot name after the step (empty = none)
		float Settle = 0.5f;                     // seconds to wait after Until before the shot / next step
	};

	enum class EPhase : uint8 { Enter, Wait, Settle, AfterShot };

	UFVGame* Game = nullptr;
	TArray<FStep> Steps;
	int32 StepIndex = 0;
	EPhase Phase = EPhase::Enter;
	float StepTime = 0.f;
	float PhaseTime = 0.f;
	int32 PhaseFrames = 0;
	float TotalTime = 0.f;
	float LastDt = 0.f;
	bool bFinished = false;
	int32 ShotCount = 0;
	int32 Warnings = 0;
	FFVInput Synth;                              // synthetic input handed to the game this frame
	float TouchStartX = 0.f;                     // player x before the touch-joystick step

	// fishing step bookkeeping
	float FishStartTime = -1.f;
	float WaitingTime = 0.f;
	int32 BalanceIndex = 0;

	void BuildScript();
	void AddStep(const FString& Name, TFunction<void()> Enter, TFunction<bool(float)> Until = nullptr, const FString& Shot = FString(), float Settle = 0.5f, float Timeout = 15.f);
	void AddScreenShotStep(EFVScreen Screen, const FString& Shot);
	void AddLocationStep(FName LocationId, float ClockMinutes, EFVWeather Weather, const FString& Shot);

	void DismissModals();
	void ClearToasts();
	/** Puts the running fight's meter at P and lets it go on from there (DebugPinProgress + unpin): the fights whose
	    bookkeeping is checked, not their balance (the practice, the showcase catch, the legend trial), end in seconds. */
	void SetFightProgress(float P);
	/** The fresh profile's other overlay screens (inventory, wardrobe, collection, guide, levels, profile), each opened
	    for a few frames without a shot (they are shot after the catch or rich): a screen that does not open (or breaks
	    on an empty profile) is a warning. */
	void AddFreshScreensStep();
	int32 SubFrames = 0;                         // per-step frame counter (AddFreshScreensStep)
	void GrantEverything();
	bool DriveFishing(bool bUntilShowcase);
	bool RunBalanceStep();
	void Finish(bool bTimedOut);

	// ---- update 2 (house, stalls, stations, mini-games)
	bool bStepFlag = false;                      // per-step "the key was already sent" latch (reset in Enter)
	int32 StepValue = 0;                         // per-step counter snapshot (e.g. backpack units before a pickup)
	/** Like AddScreenShotStep, but opens the screen with a context (UFVGame::OpenScreenWith). */
	void AddScreenShotStepWith(EFVScreen Screen, FName Context, const FString& Shot);
	/** Opens a mini-game (it auto-plays under -fvautotest and its bot takes the minigame_<slug>_play / _result shots),
	    waits <= 12 s until it is gone. */
	void AddMiniGameSteps(EFVScreen Screen, FName Context, const FString& Slug);
	/** Stall -> shop, map -> home, resource pickup, house, every new screen, the lure and knot mini-games, sleep
	    (cooking and digging are played once in AddUpdate5MiniGameSteps). */
	void AddHomeSteps();
	/** Teleports the player next to the first world object of Kind (Id optional); false (+ warning) if none. */
	bool TeleportToObject(EFVObjKind Kind, FName Id, const TCHAR* What);
	/** Once per step (after the scene had a frame to settle): presses E when the target object (Kind, Id optional)
	    is the nearest one in reach; otherwise interacts with it directly (another object is closer). */
	void InteractOnce(float T, EFVObjKind Kind, FName Id);
	int32 BackpackUnits() const;
	/** GrantEverything, update-2 part: stations, items, containers, nets, smokehouse, decor, lures, food, orders. */
	void GrantHome();

	// ---- update 3 (touch gestures on the new UI)
	FVector2D TouchTapPos = FVector2D::ZeroVector;   // tap-anywhere test: empty world point far from the stall
	/** Touch-only steps: a short tap on empty ground uses the highlighted stall (the shop opens), then a horizontal
	    finger swipe across the shop's content switches its tab (a pager takes the drag). */
	void AddTouchUpdate3Steps();
	/** Empty world point (no object under it or next to it, not on the HUD) as far as possible from From. */
	FVector2D FindEmptyWorldPoint(const FVector2D& From) const;

	// ---- update 4 (online profile, leaderboards, mail, levels, restore)
	/** Result of one asynchronous online request (shared with its callback: it may arrive after the test ended). */
	struct FOnlineProbe
	{
		bool bDone = false;
		bool bOk = false;
		int32 Count = 0;
		int32 Total = 0;
		int32 Rank = 0;
		FString Info;
	};
	TSharedRef<FOnlineProbe> Probe = MakeShared<FOnlineProbe>();
	int32 OnlineChecksOk = 0;
	/** The new screens (levels, own profile, first-run name choice, leaderboard, mail, restore) as screenshots. */
	void AddUpdate4ScreenSteps();
	/** Only with -fvserver (UFVOnline::IsEnabled): registration, sync, a leaderboard page, the own server profile, the
	    mail box; "FV: AUTOTEST online ... ok" or WARNING lines. */
	void AddOnlineSteps();
	/** Touch only: tapping the HUD level badge opens Levels, the face button opens the profile. */
	void AddTouchUpdate4Steps();
	bool IsOnlineRun() const;

	// ---- update 4b (start-up gate, practice pond + first-fishing onboarding)
	/** With -fvserver: waits for the main-menu start-up gate to decide (shot + "FV: AUTOTEST gate ..." line). */
	void AddGateSteps();
	/** A fresh profile (OnboardingStep 0) in the yard: the coach marks, the practice pond, a practice catch with the
	    bot, the success card, the map hint and the trip to the village pond that ends it ("FV: AUTOTEST onboarding"). */
	void AddOnboardingSteps();
	/** Scans the scene for the walkable spot closest to the player where CanFishFromHere() is true (the practice
	    pond's edge in the yard) and puts the player there. False if there is none. */
	bool TeleportToFishingSpot();
	/** One onboarding check: logs ok / WARNING and counts it. */
	void OnboardingCheck(bool bOk, const FString& What);
	int32 OnboardingOk = 0;
	int32 OnboardingFailed = 0;
	bool bPracticeSpot = false;                  // TeleportToFishingSpot found the practice pond
	int32 TrialFishBefore = 0;                   // 0.5 legend trial: profile counters before it (it must not change them)
	int32 TrialXpBefore = 0;
	int32 TrialMetBefore = 0;
	int32 PracticeFishBefore = 0;                // profile counters before the practice catch (it must not change them)
	int32 PracticeXpBefore = 0;
	int32 PracticeKeepnetBefore = 0;
	int32 PracticeEscapes = 0;                   // practice fish that got away before the success card
	bool bPracticeWasEscaped = false;            // edge detector for the Escaped state

	// ---- update 5 (legends, rarity dishes, digging penalty, legend achievements): FVAutoTestLegends.cpp
	/** One update-5 check: logs "FV: AUTOTEST legends <what>: ok" or a WARNING (counted). */
	void LegendCheck(bool bOk, const FString& What);
	int32 LegendOk = 0;
	int32 LegendFailed = 0;
	/** Fresh profile: a legend catch completes its achievement (toast + skin), the wardrobe with that skin unlocked and
	    the other five locked. */
	void AddLegendFreshSteps();
	/** Rich profile: rule checks, the legend fights (Tsar intro, sturgeon snap, angler glow / lantern, Zhar-ryba
	    eruption / last dash) with screenshots, the collection cards of an unmet / met legend. */
	void AddLegendSteps();
	/** Kitchen: the set-up of the four fried dishes by rarity and the ember bait (cuts, speed, vessel; closed without
	    cooking), the legend dish cooked by the bot; digging: the timer, a tap on an empty hole costs half a second. */
	void AddUpdate5MiniGameSteps();
	/** Pure data / logic checks (one frame): legend defs + sprites, gates, PickCatch bans and supply, shovels, rarity
	    dishes, ingredient tokens and the legend achievements on scratch profiles. */
	void RunLegendRuleChecks();
	/** §7.11: 90 s of FFVMinigame::Step per legend with the progress pinned (every trick must show up). */
	void RunLegendTricksChecks();
	/** One BALANCE_LEGEND simulation per call (legend x {human-mid, human-strong, «с запасом»}); true when all are done.
	    The table is only recorded (the owner accepted the balance by playing); checks: human-strong lands every legend
	    (> 0 %), the tricksters show >= 2 tricks per fight (medians). */
	bool RunLegendBalanceStep();
	int32 LegendBalanceIndex = 0;
	TArray<FFVLegendSimResult> LegendBalanceRow;          // results of the current legend's rows
	/** BALANCE regression: the reference lines (Saved/Logs/FV_balance_reference.log) by fish id; compared by RunBalanceStep. */
	TMap<FName, FString> BalanceReference;
	bool bBalanceReferenceLoaded = false;
	int32 BalanceMatched = 0;
	int32 BalanceMismatched = 0;
	void LoadBalanceReference();
	/** Travel + rig (tackle, bait, bobber) for one legend fight, then the bot casts and DebugForceBite(FishId, bGates) makes
	    the legend bite; the second step ends in UntilState (Bite / Minigame). */
	void AddLegendHookSteps(const FString& Name, FName LocationId, float ClockMinutes, EFVWeather Weather, FName FishId, bool bGates,
		TFunction<void(UFVSaveGame&)> Rig, EFVFishState UntilState);
	/** Ends a legend fight: unpins the meter, stops the bot, resets fishing. */
	void AddLegendFightEndStep(const FString& Name);
	/** Puts ItemId (a line / float) on the equipped rod: gives it, installs it (the old one goes to the backpack). */
	bool RigTackle(UFVSaveGame& P, FName ItemId);
	float U5BiteAt = -1.f;                       // TotalTime of the legend's Bite / Minigame / Escaped (legend fights)
	float U5HookAt = -1.f;
	float U5EscapeAt = -1.f;
	int32 U5EscapedBefore = 0;
	int32 U5Frames = 0;
	int32 U5Serial = 0;                          // FVMiniGameDebug().CookSerial / DigPenalties before a mini-game check
	float U5Value = 0.f;
	int32 U5DishBefore = 0;
	int32 U5TokenBefore = 0;
	/** Saved/AutoTest/update5_sheet.png: the six legends + silhouettes, the six legend bobbers (both frames) on water. */
	bool WriteUpdate5Sheet(const FString& Path) const;

	// ---- update 6 (forest, yard, City, Cave, expert fishing): FVAutoTestU6.cpp (Docs/UPDATE6_FOREST_CITY.md §0, §5,
	// §6.2, §10.14, §12, §14). Lines: "FV: AUTOTEST u6 <check>: ok" / "FV: AUTOTEST WARNING u6 <check>: FAILED",
	// "FV: ECONOMY ..." and "FV: BALANCE_EXPERT ...".
	void U6Check(bool bOk, const FString& What);
	int32 U6Ok = 0;
	int32 U6Failed = 0;
	/** GrantEverything, update-6 part: the City visited (levels 11–15), tools, yard buildings, the boat, expert rods and
	    their tackle, seeds and vegetables. */
	void GrantU6();
	/** Pure data / logic checks (one frame): ECONOMY (§5), seeds, the ukha with carrot and the orders without the garden
	    (§6.2), the 0.5 save migration (§14), the sleep limit (§0.2), the hot-fixes (trophy -> keepnet, net repair cost),
	    the big yard / house switched on. */
	void RunU6RuleChecks();
	/** Rich profile: the camera at the edges of every big location (shots), the HUD column vs the interactive objects of
	    every classic location, the new screens / shops / guide entries / mini-games (shots), the expert fight per rod type
	    (shots, the column hidden), the repeated swipe in a vertical list (touch). */
	void AddUpdate6Steps();
	/** 0.6 world checks: the forest windfall / footbridge, the City rubble and the Omut close the way until cleared and
	    then let the player pass; the camera on the City pier / adit path (and other narrow strips) never jumps, shows no
	    voids and keeps the player in the band; every big location: the camera sweep and objects reachable on foot. */
	void AddU6WorldSteps(TFunction<void()> CloseAll);
	/** One BALANCE_EXPERT cell per call (fish x rod type x hazard, legends novice / expert); true when all are done. */
	bool RunExpertBalanceStep();
	int32 ExpertBalanceIndex = 0;
	int32 ExpertBalanceNotes = 0;
	/** Per-step scratch of the update-6 steps. */
	int32 U6Phase = 0;
	FName U6TreeCtx;                             // "forest:<treeId>" of the first tree seen by the camera step (chopping)
	FName U6BoulderCtx;                          // "cave:<boulderId>" (excavation)
	FName U6FightFish;                           // the expert fish forced to bite in the current expert-fight step
	bool bU6SawCancel = false;
	bool bU6SawVertical = false;

	// ---- save check (-fvautotest -fvsavecheck): FVAutoTestU6.cpp. Loads every Saved/SaveCheck/*.sav (COPIES of real
	// saves, never the player's slot) from memory: fixup loses nothing, is idempotent (save -> load -> fixup gives the
	// same bytes), then plays each copy through the autotest slot (Continue, screens, Return to menu, reload).
	// Lines: "FV: SAVECHECK ..." and "FV: AUTOTEST WARNING savecheck ...".
	void AddSaveCheckSteps();
	void SaveCheck(bool bOk, const FString& What);
	TArray<FString> SaveCheckFiles;
	TMap<FString, int64> SaveCheckSnap;          // snapshot of the fixed profile of the copy being played
	int32 SaveCheckOk = 0;
	int32 SaveCheckFailed = 0;
};
