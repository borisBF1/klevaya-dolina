// UFVGame — the game "brain": owns subsystems, screens, clock/weather, modals & toasts, catch handling, saving,
// the world HUD (top bar + hint bar) and the autotest. Implemented in FVGame.cpp (+ FVAutoTest.cpp).
//
// Frame flow:  AFVPlayerController::PlayerTick -> UFVGame::Tick(Dt, Input)
//              AFVHUD::DrawHUD                 -> UFVGame::Draw(Canvas)

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FVTypes.h"
#include "FVDraw.h"
#include "FVScene.h"
#include "FVFishing.h"
#include "FVScreens.h"
#include "FVGame.generated.h"

class UCanvas;
class APlayerController;
class UFVSaveGame;
class UFVSprites;
class UFVAudio;
class FVAutoTest;
class FVTouchControls;

/** Modal dialog (drawn above everything; blocks input beneath). */
struct FFVModal
{
	FString Title;
	FString Body;
	TArray<FString> Buttons;                 // empty -> single "OK"
	TFunction<void(int32)> OnChoice;         // index of the pressed button (ESC = last button)
	EFVIcon Icon = EFVIcon::Count;           // Count = no icon
	TArray<FString> Lines;                   // optional bullet list under the body (e.g., level-up unlocks)
};

UCLASS()
class UFVGame : public UObject
{
	GENERATED_BODY()

public:
	UFVGame();
	virtual ~UFVGame() override;

	static UFVGame* Get();

	void Init(APlayerController* InPC);
	void Shutdown();
	void Tick(float Dt, const FFVInput& RawInput);
	void Draw(UCanvas* Canvas);

	virtual UWorld* GetWorld() const override;
	APlayerController* GetPC() const { return PC.Get(); }

	// ------------------------------------------------------------------ subsystems
	UFVSaveGame* GetProfile() const { return Profile; }
	FVScene& GetScene() { return *Scene; }
	FVFishing& GetFishing() { return *Fishing; }
	UFVSprites* GetSprites() const { return Sprites; }
	UFVAudio* GetAudio() const { return Audio; }
	/** Online services (update 4): created in Init, ticked every frame (see Public/FVOnline.h). */
	class UFVOnline* GetOnline() const { return Online; }
	double GetTime() const { return Time; }

	// ------------------------------------------------------------------ screens
	void OpenScreen(EFVScreen Screen);       // creates the screen via FVMake*Screen, plays Open sfx
	void CloseScreen();                      // back to the world (or main menu if no game is running)
	EFVScreen GetScreen() const { return CurrentScreen; }
	bool IsInGame() const { return bInGame; }

	// ------------------------------------------------------------------ modal & toasts
	void ShowModal(FFVModal Modal);          // queued; shown one at a time
	void Confirm(const FString& Title, const FString& Body, TFunction<void()> OnYes, const FString& YesLabel = TEXT("Да"), const FString& NoLabel = TEXT("Нет"));
	bool HasModal() const { return Modals.Num() > 0; }
	void Toast(const FString& Text, const FLinearColor& Color = FVPal::White(), EFVIcon Icon = EFVIcon::Count);

	// ------------------------------------------------------------------ game flow
	bool HasSaveGame() const;
	void NewGame();                          // resets the profile, shows help, enters the world
	bool ContinueGame();                     // loads the save and enters the world
	void ReturnToMainMenu();                 // saves, resets fishing, opens the main menu
	void SaveNow();
	void QuitGame();
	/** Travels to an unlocked location (resets fishing, loads the scene, switches music, autosaves). */
	bool TravelTo(FName LocationId);
	/** Call after any profile change done by screens (purchase, equip, sell): checks achievements, refreshes the
	    character appearance, marks the profile dirty for autosave. */
	void NotifyProfileChanged();
	/** Fullscreen <-> windowed. */
	void ToggleFullscreen();
	void SetSfxVolume(float V);
	void SetMusicVolume(float V);

	// ------------------------------------------------------------------ fishing callbacks (called by FVFishing)
	/** Applies a catch to the profile (records, xp, keepnet, stats, treasure, achievements) and fills the
	    bNewSpecies/bNewRecord/bStored/LevelsGained fields. Level-up modals/toasts are queued here. */
	void HandleCatch(FFVCatchResult& Result);
	void HandleEscape(const FFVFishDef* Fish);
	/** Current catch context (location, phase, weather, level, rod, bait). Depth must be filled by the caller. */
	FFVCatchContext MakeCatchContext(float Depth) const;

	// ------------------------------------------------------------------ clock & weather
	float GetClockMinutes() const;           // 0..1440
	EFVDayPhase GetPhase() const;
	uint8 GetPhaseMask() const { return FVTime::Bit(GetPhase()); }
	EFVWeather GetWeather() const;
	int32 GetDay() const;
	FString GetClockText() const;            // "06:40"
	/** 0 = full daylight .. 1 = deepest night (smooth, for lighting). */
	float GetNightFactor() const;

	/** Last frame's hover state of UI widgets (for resolving LMB as a world action). */
	bool WasUIHoveredLastFrame() const { return bUIHoveredLastFrame; }

	// ------------------------------------------------------------------ autotest (-fvautotest)
	bool IsAutoTest() const { return AutoTest.IsValid(); }
	/** Requests a screenshot of the next rendered frame into Saved/AutoTest/<Name>.png */
	void RequestShot(const FString& Name);

	// ------------------------------------------------------------------ additions (Core module)
	/** Virtual-canvas mapping of the last drawn frame: screen px per virtual px, screen position of virtual (0,0). */
	float GetViewScale() const { return DrawCtx.Scale; }
	FVector2D GetViewOffset() const { return DrawCtx.Offset; }
	/** Closes the front modal as if button ButtonIndex was pressed (-1 = last button, same as ESC). */
	void DismissModal(int32 ButtonIndex = -1);
	/** Shows the "how to play" dialog (controls). */
	void ShowHelp();
	/** Debug / autotest: sets the in-game clock (minutes, wrapped to 0..1440). */
	void SetClockMinutes(float Minutes);
	/** Debug / autotest: sets the current weather (also switches the rain ambience). */
	void SetWeather(EFVWeather NewWeather);
	/** Rect of the HUD icon button that opens Screen in the last drawn frame (empty if not drawn). */
	FFVRect GetHudButtonRect(EFVScreen Screen) const { const FFVRect* R = HudButtonRects.Find(Screen); return R ? *R : FFVRect(); }

	// ============================================================================================
	// UPDATE 2 (Core module; implemented in FVGameHome.cpp). See Docs/UPDATE_HOME.md.
	// ============================================================================================
	// ---- screens with a parameter
	/** Opens Screen after storing Context (trader id, station id, recipe id, "<locationId>:<spot>", "backpack"/"keepnet").
	    The screen reads GetScreenContext() in OnOpen. OpenScreen(S) = OpenScreenWith(S, NAME_None). Ungated. */
	void OpenScreenWith(EFVScreen Screen, FName Context);
	FName GetScreenContext() const { return ScreenContext; }
	/** TryOpenFromWorld with a context (refuses while the world is busy). */
	bool TryOpenFromWorldWith(EFVScreen Screen, FName Context);
	/** Mini-games call this when they finish: reopens the station screen (StationId = "st_kitchen"...) or closes to the
	    world when StationId is None. */
	void ReturnFromMiniGame(FName StationId);

	// ---- time
	/** (Day - 1) * 1440 + clock minutes. All timers (nets, smokehouse, food, orders) use it. */
	double GetAbsMinutes() const;
	/** Advances the clock by Minutes: day rollovers (weather, OnNewDay, toasts unless bQuiet) and passive systems
	    (UFVSaveGame::SimulatePassive, order expiry). The normal clock tick uses it too. */
	void AdvanceClock(float Minutes, bool bQuiet = false);
	/** Bed: sleeps until 06:00 (next day if it's past 06:00), shows a summary modal, saves. */
	void SleepUntilMorning();
	EFVWeather GetWeatherTomorrow() const;
	/** "День 3, 14:00" for an absolute minute; "2 ч 30 мин" for a duration in minutes. */
	FString AbsTimeText(double AbsMinutes) const;
	static FString DurationText(float Minutes);

	// ---- home & world interaction
	bool IsAtHome() const;                   // yard or house interior
	bool IsInHouse() const;
	void EnterHouse();                       // yard door -> interior
	void ExitHouse();                        // interior door -> yard
	/** Uses a world object (the player is in reach): pickup, stall -> shop, station -> screen, bed -> sleep... */
	void Interact(const FFVWorldObject& Obj);
	/** Fishing busy (anything but Idle). Hotkeys, HUD buttons and interactions are refused while busy. */
	bool IsWorldBusy() const;
	/** Trader whose stall is within reach of the player right now (NAME_None if none). */
	FName GetNearbyTrader() const;

	// ---- fishing stats
	/** Equipped rod + installed tackle + active food buff. FVFishing reads this once per cast. */
	FFVRodStats GetEffectiveRodStats() const;

	// ---- autotest / debug
	void DebugAdvanceHours(float Hours) { AdvanceClock(Hours * 60.f, true); }

	// ============================================================================================
	// UPDATE 6 (Docs/UPDATE6_FOREST_CITY.md). [GAME-FLOW] FVGameU6.cpp — the hooks of this file call class FVGameU6
	// (Private/FVGameU6.h), which is a friend and may use the private members below.
	// ============================================================================================
	/** Local sea state of the current location (§8.1); bValid = false on classic locations. */
	FFVLocalWeather GetLocalWeather() const;
	/** Real minutes of play (U6.PlayedMinutes, not moved by sleep): regrowth, garden, artel limit. */
	double GetPlayedMinutes() const;
	/** Walk-through travel between big locations (exits) or by boat: like TravelTo (checks unlocks) with an entry point. */
	bool TravelToEntry(FName LocationId, FName Entry);
	/** Expert catch context of the equipped expert rod at the current location (Depth filled by the caller). */
	FFVExpertCatchContext MakeExpertContext(float Depth) const;
	/** Equipped expert rod + tackle + active food (expert buffs only). Neutral for classic rods. */
	FFVExpertStats GetEffectiveExpertStats() const;
	bool IsExpertRodEquipped() const;
	/** The HUD button column is hidden (expert fight, §10.12). Read by DrawWorldHud [CORE-HUD]. */
	bool IsHudColumnHidden() const;
	/** [CORE-HUD] Screen rect (virtual px) of the HUD button column(s) with their backing in the last drawn frame; empty
	    when hidden (overlay screen, expert fight, reeling minigame). Touch treats it as UI; coach marks avoid it. */
	FFVRect GetHudColumnRect() const { return HudColumnRect; }

private:
	// ---- update 2 (private, Core module)
	FName ScreenContext;
	FName PendingInteractKey;                // object the player walks to (kind/index/id key), NAME_None = none
	void TickWorldInteraction(FFVInput& In); // hover, click/tap/E, walk-to, dispatch (FVGameHome.cpp)
	void StartNewDay(bool bQuiet);           // midnight rollover (extracted from TickClock)
	// (additive, Core-private)
	FString InteractHint;                    // HUD prompt for the object in reach / under the cursor ("[E] Собрать: Ветка")
	FFVRect HudBackpackRect;                 // HUD "Рюкзак" button (the "Садок" button is HudButtonRects[Inventory])
	TArray<FString> SleepLog;                // lines collected by quiet AdvanceClock calls (sleep summary)
	/** Loads a home scene (yard / interior) without the map-travel toast; bSpawnAtDoor puts the player by the door. */
	void LoadHomeScene(FName LocationId, bool bSpawnAtDoor);
	/** Bed: asks for confirmation, then SleepUntilMorning. */
	void ConfirmSleep();
	/** Music theme for a location id (home ids share the cozy home waltz). */
	EFVTheme MusicThemeFor(FName LocationId) const;

private:
	UPROPERTY(Transient) TObjectPtr<UFVSaveGame> Profile;
	UPROPERTY(Transient) TObjectPtr<UFVSprites> Sprites;
	UPROPERTY(Transient) TObjectPtr<UFVAudio> Audio;
	UPROPERTY(Transient) TObjectPtr<class UFVOnline> Online;
	TWeakObjectPtr<APlayerController> PC;

	TUniquePtr<FVScene> Scene;
	TUniquePtr<FVFishing> Fishing;
	TUniquePtr<FVScreen> ActiveScreen;
	EFVScreen CurrentScreen = EFVScreen::None;
	bool bInGame = false;

	FVDraw DrawCtx;
	FFVInput FrameInput;          // input after resolving world action (consumed by Draw)
	double Time = 0.0;
	bool bUIHoveredLastFrame = false;
	bool bProfileDirty = false;
	float AutosaveTimer = 0.f;

	TArray<FFVModal> Modals;
	struct FToast { FString Text; FLinearColor Color; EFVIcon Icon; float Age; float Y = -1.f; };
	TArray<FToast> Toasts;

	TSharedPtr<FVAutoTest> AutoTest;
	TSharedPtr<FVTouchControls> Touch;       // phones / -fvtouch: gestures (swipe = walk, hold = cast) + finger pointer
	TMap<EFVScreen, FFVRect> HudButtonRects; // filled by DrawWorldHud (autotest taps them)
	FFVRect HudColumnRect;                   // update 6 (§12.1): the button column(s) + backing, last frame (empty = hidden)

	// Implementation helpers are private to FVGame.cpp; add private members here as needed (additive only).
	void TickClock(float Dt);
	void DrawWorldHud(FVDraw& D);
	void DrawModal(FVDraw& D);
	void DrawToasts(FVDraw& D, float Dt);

	// ---- additions (Core module, private)
	friend class FVAutoTest;
	friend class FVGameU6;                        // update 6: the hooks (Private/FVGameU6.h)
	TArray<TUniquePtr<FVScreen>> RetiredScreens;  // screens closed during this frame; deleted at the start of the next Tick
	double LastDrawTime = -1.0;
	float ModalAge = 0.f;                         // seconds the front modal has been visible
	bool bLmbActionLatched = false;               // LMB went down over the world (not UI) and is still held
	bool bPrevActionDown = false;
	void EnterWorld(FName LocationId);
	void RetireActiveScreen();
	void ChooseModal(int32 ButtonIndex);
	void AnnounceAchievements(const TArray<FName>& Ids);
	bool TryOpenFromWorld(EFVScreen Screen);
	void ApplyTreasure(FFVCatchResult& Result);
};
