// Fishing: cast power meter, waiting/bite, the Stardew-Valley-style reeling minigame, catch showcase.
// Implemented in FVFishing.cpp.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class UFVGame;
class FVDraw;
struct FFVExpertFight;   // update 6: FVExpertFight.h

enum class EFVFishState : uint8
{
	Idle,        // walking around
	Charging,    // action held: power meter goes up & down (Stardew style)
	Casting,     // bobber flying
	Waiting,     // bobber in the water, waiting for a bite
	Bite,        // "!" shown — press action within the reaction window
	Minigame,    // reeling (bobber bar)
	Showcase,    // caught: player holds the fish up + result card; action/click to continue
	Escaped,     // "Сорвалась!" message, then back to Idle
	Retrieving   // reeling the empty line back (cancel / missed bite / cast on land)
};

/**
 * Pure logic of the Stardew Valley BobberBar minigame, run at a fixed 60 Hz (FV::SimDt).
 * Track coordinates: 0 = top, FV::TrackH = bottom. Holding the action pushes the bar UP (towards 0).
 */
struct FFVMinigame
{
	// ---- configuration ----
	float Difficulty = 30.f;
	EFVMotion Motion = EFVMotion::Mixed;
	float BarHeight = 96.f;       // catch bar height (track units)
	float ReelMult = 1.f;
	float DrainMult = 1.f;
	bool bHasTreasure = false;
	// update 5 (Docs/UPDATE5_LEGENDS.md §1): the legend on the hook. None and TsarCarp fight the classic way
	// (HasLegendRules() == false: no LegendRng, no extra Rng rolls — bit-identical to the BALANCE reference).
	EFVLegend Legend = EFVLegend::None;
	float LegendGain = 1.f;       // x progress gain inside the bar (not the drain); only used when HasLegendRules()
	float BaseBarHeight = 96.f;   // BarHeight at Start (the Zhar-ryba's rage shrinks BarHeight to 0.75 x this)

	// ---- state ----
	float BarPos = 0.f;           // top of the bar
	float BarSpeed = 0.f;
	float FishPos = 0.f;          // top of the fish icon (icon is FishIconH tall)
	float FishSpeed = 0.f;
	float FishTarget = -1.f;
	float FishAccel = 0.f;
	float FloatSink = 0.f;        // floater/sinker drift
	float Progress = 0.3f;        // 0 = escaped, 1 = caught
	bool bFishInBar = false;
	bool bPerfect = true;
	float TreasurePos = 0.f;
	float TreasureProgress = 0.f;
	float TreasureAppearTimer = 0.f;
	bool bTreasureVisible = false;
	bool bTreasureInBar = false;
	bool bTreasureCaught = false;
	float Elapsed = 0.f;          // seconds
	float Shake = 0.f;            // visual feedback when the fish leaves the bar
	FRandomStream Rng;
	// 0.4.1, the practice pond: an empty meter never ends the fight — the fish can still be caught, and the [X] of the
	// panel (shown once the meter has been empty) lets the player give up instead
	bool bNoEscape = false;       // set by the caller after Start()
	bool bEmptied = false;        // the meter has reached zero at least once in this fight

	// ---- update 5: legend fight state (reset by Start; only touched when HasLegendRules()) ----
	FRandomStream LegendRng;      // every legend roll (Seed ^ 0x4C454745); Rng stays the classic stream
	float LegendT = 0.f;          // seconds of legend fight (after the hook intro)
	float PauseT = 0.f;           // phase pause: everything stands still (banners «в ярости», «кольца»...)
	float ProgressFreezeT = 0.f;  // progress frozen (Zhar-ryba blink + 0.3 s)
	uint8 HazardState = 0;        // 0 calm, 1 warning, 2 hazard, 3 Angler «жуёт»
	float HazardTimer = 0.f;      // seconds left in the current HazardState
	float HazardPos = 0.f;        // track units: lantern / lava zone top
	float HazardH = 0.f;          // lava zone height
	float Current = 0.f;          // Sturgeon: added to BarSpeed every tick while the current runs
	float FishAlpha = 1.f;        // fish icon visibility (trout transparency, fog, blink); drawing only uses this
	float SeenFishPos = 0.f;      // where the player last saw the fish (glints, bubbles) — the fair bot aims here
	float SeenFishSpeed = 0.f;
	float Glint = 0.f;            // trout glint 0..1 (fades 3.3/s)
	float FogPos[2] = { 0.f, 0.f };   // serpent fog bands (top, track units; FogH tall)
	float FogVel[2] = { 0.f, 0.f };
	float WavePhase = 0.f;        // serpent loops
	float WaveAmp = 0.f;
	float WavePeriod = 1.6f;
	uint8 Phase = 0;              // Zhar-ryba: 0 flame, 1 rage (>= 50 %), 2 last dash (>= 80 %); serpent: 1 after the coils
	float PhaseFlash = 0.f;       // track frame pulse after a phase change
	float BurnFlash = 0.f;        // bar edge flash while the lava burns
	uint8 BlinkState = 0;         // Zhar-ryba: 1 = the fish vanished, the spark shows where it reappears
	float BlinkTarget = 0.f;
	float BlinkTimer = 0.f;
	float LungeTime = 0.f;        // Angler lunge time left
	bool bFeinted = false;        // Angler: the fake lantern was used in this cycle
	bool bFakeLantern = false;    // Angler: the current lantern is the red fake one
	float BarShrinkT = 0.f;       // Zhar-ryba rage: 0..1 of the 1.5 s bar shrink
	int32 TrickCount = 0;         // tricks shown in this fight (balance metric)
	int32 Phase0Eruptions = 0;    // Zhar-ryba eruptions (HazardState 2) during phase 0 (balance metric)
	// update 5, fight agent: drawing / timing helpers of the legend rules (all reset by Start)
	float HazardAge = 0.f;        // seconds since HazardState changed (flicker phase, chew shake, lantern ring)
	float BannerT = 0.f;          // a banner is on screen: no warning starts (the hook banner covers 0.8 s after the intro)
	float MarkTimer = 0.f;        // next trout glint / serpent bubble
	float LungeSpeed = 0.f;       // Angler: lunge speed (track units per tick)
	float FogOutT = 0.f;          // Fog serpent: seconds out of the fog (a new fog entry counts as a trick after 0.25 s)
	float FogOpenT = 0.f;         // Fog serpent: the opening (1.5..2 s): the fog cannot hide the fish yet, band 0 homes in
	float BubbleY[4] = { 0.f, 0.f, 0.f, 0.f };     // serpent bubbles: fish icon top where each one rose from
	float BubbleAge[4] = { 9.f, 9.f, 9.f, 9.f };   // seconds (alive below 0.8)
	int32 BubbleNext = 0;
	/** Banners the minigame raises (FVFishing shows them outside the panel; BannerSerial changes with every new one). */
	enum class ELegendBanner : uint8 { None, Hook, Rage, LastDash, Coils };
	ELegendBanner Banner = ELegendBanner::None;
	int32 BannerSerial = 0;

	static constexpr float FishIconH = 32.f;

	enum class EResult : uint8 { Running, Caught, Escaped };

	/** InLegend (update 5, trailing so old calls compile): None/TsarCarp = the classic fight. */
	void Start(float InDifficulty, EFVMotion InMotion, float InBarHeight, float InReelMult, float InDrainMult, bool bInHasTreasure, int32 Seed,
		EFVLegend InLegend = EFVLegend::None);
	/** One fixed 1/60 s step. */
	EResult Step(bool bHold);
	/** Legend tricks apply (Sturgeon and later). The Tsar-carp is a legend with the classic fight. */
	bool HasLegendRules() const { return Legend != EFVLegend::None && Legend != EFVLegend::TsarCarp; }
	/** Simple controller used by the autotest / balance simulation. Skill 0..1 (1 = near-perfect).
	    Exactly BotShouldHoldLook(3 + 9 * Skill) — the BALANCE regression depends on it. */
	bool BotShouldHold(float Skill = 1.f) const;
	/** The bot with LookTicks ticks of anticipation (FFVBotProfile::LookTicks). In a legend fight it only uses what
	    the player sees (SeenFishPos while the fish is hidden, the real lantern, the lava zone, the blink spark). */
	bool BotShouldHoldLook(float LookTicks) const;

private:
	// update 5 (§1): the legend rules. Step() branches here only when HasLegendRules(); the classic code is untouched.
	EResult StepLegend(bool bHold);
	void StartLegendState();
	void StepLegendHazards(float Dt);
	void StepLegendFish(float Dt);
	void StepLegendFog(float Dt);
	void RaiseBanner(ELegendBanner B);
	void SetHazard(uint8 State, float Seconds);
	bool LegendBotHold(float Look) const;
};

/** Update 5 (§5): a simulated player — reaction delay in ticks, chance of a wrong input per tick, anticipation in ticks. */
struct FFVBotProfile
{
	int32 DelayTicks = 6;
	float Noise = 0.027f;
	float LookTicks = 10.65f;

	static FFVBotProfile HumanMid() { return { 18, 0.06f, 6.f }; }         // an average phone player (300 ms)
	static FFVBotProfile HumanStrong() { return { 13, 0.03f, 9.f }; }      // a strong player (217 ms)
	static FFVBotProfile Bot085() { return { 6, 0.027f, 10.65f }; }        // ~ BotSkill 0.85 (the BALANCE table itself stays on FVSimulateCatchRate)
};

/** Update 5: one legend fight setup for FVSimulateLegend (the tackle is already composed into the numbers). */
struct FFVLegendSimSetup
{
	EFVLegend Legend = EFVLegend::None;
	float Difficulty = 70.f;
	EFVMotion Motion = EFVMotion::Mixed;
	float BarHeight = 96.f;       // FVDB::CatchBarHeight(level, stats)
	float ReelMult = 1.f;
	float DrainMult = 1.f;
};

struct FFVLegendSimResult
{
	float Rate = 0.f;             // caught / trials
	float MedianSec = 0.f;        // median fight length, seconds (all fights)
	float MedianTricks = 0.f;     // median FFVMinigame::TrickCount
	float Phase0EruptFrac = 0.f;  // fights with at least one eruption in phase 0 (Zhar-ryba)
};

/** Headless balance helper: fraction of successful catches using the bot. Legend (update 5, trailing) = its fight rules. */
float FVSimulateCatchRate(float Difficulty, EFVMotion Motion, float BarHeight, float ReelMult, float DrainMult, int32 Trials, int32 Seed, float BotSkill = 0.85f,
	EFVLegend Legend = EFVLegend::None);
/** Update 5: legend fights played by a bot profile: every press lands DelayTicks later, the player predicts his bar
    through the presses already on their way (he does not know his Noise slips), then BotShouldHoldLook decides. */
FFVLegendSimResult FVSimulateLegend(const FFVLegendSimSetup& Setup, const FFVBotProfile& Profile, int32 Trials, int32 Seed);

class FVFishing
{
public:
	explicit FVFishing(UFVGame* InGame);
	~FVFishing();

	/** Aborts everything and returns to Idle (travel, opening the main menu...). */
	void Reset();

	/** In: world input (bAction* already resolved by UFVGame). Called only while the world screen is active and no modal is open. */
	void Tick(float Dt, const FFVInput& In);

	/** Fishing line, bobber, bite "!" and water feedback — world space, drawn after FVScene::DrawScene. */
	void DrawWorld(FVDraw& D);
	/** Power meter, minigame panel, showcase card, messages — drawn above lighting, below the HUD. */
	void DrawUI(FVDraw& D);

	EFVFishState GetState() const { return State; }
	/** Anything but Idle: the player can't walk; the HUD should not open the map/shop. */
	bool IsBusy() const { return State != EFVFishState::Idle; }
	/** Contextual hint for the bottom bar (Russian), e.g. "[Пробел / ЛКМ] Забросить". */
	FString GetHint() const;

	// ---- autotest / debug hooks ----
	void SetAutoPlay(bool bEnabled) { bAutoPlay = bEnabled; }   // bot presses buttons (cast at 0.8, hooks, reels)
	bool IsAutoPlay() const { return bAutoPlay; }
	const FFVMinigame& GetMinigame() const { return Mini; }
	void DebugForceBite();                                       // Waiting -> Bite immediately
	/** Update 5: Waiting -> Bite immediately, and this fish (e.g. a legend) takes the bait instead of PickCatch's pick.
	    bApplyGates = false (screenshots): the legend bites and holds whatever the tackle; true: FVDB::LegendGate applies
	    (NoBite -> PickCatch's own pick stays, Snap -> the leader snaps at the end of the hook intro). */
	void DebugForceBite(FName FishId, bool bApplyGates = false);
	/** Update 5: the 0.7 s «Легенда на крючке!» freeze after the hook is running (the minigame is not stepped). */
	bool IsLegendIntro() const;
	/** Update 5 (autotest screenshots): the minigame progress is put back to P after every step, so a fight can be
	    watched as long as needed (P < 0 = off; Reset clears it). The fight then never ends by itself. */
	void DebugPinProgress(float P);
	void DebugDrainMeter() { Mini.Progress = FMath::Min(Mini.Progress, 0.02f); }   // the meter almost empty
	/** 0.4.1: the practice minigame's «give up» [X] (drawn once its meter has been empty); zero rect when hidden. */
	FFVRect GetGiveUpRect() const;
	/** Touch: a finger landing here is a pointer for the UI (the [X]), not the reel action. */
	bool IsUiAt(const FVector2D& P) const;

	// ---- 0.5: «Бой с легендой (тест)» (developer mode, UFVSaveGame::bDevTools) ----
	/** The next bite (at any water) is this legend, fought with the tackle meant for its level (TrialSetup) and no
	    preparation rules; nothing is recorded (no catch, XP, stats, achievements, «met», online). NAME_None cancels. */
	void StartLegendTrial(FName FishId);
	FName GetPendingTrial() const;
	/** Trials this session: fights started / legends landed (for the «X из Y» line). */
	void GetTrialScore(FName FishId, int32& OutTries, int32& OutWins) const;
	/** The tackle of a legend trial (spec §5 «снасть по уровню»): rod, line, float names + the player level used. */
	static FString TrialSetupText(FName FishId);

	// ---- update 6 [FISHING-EXPERT] (Docs/UPDATE6_FOREST_CITY.md §10): the expert fight runs inside State Minigame
	/** The current Minigame is the expert 2D fight (expert rod). */
	bool IsExpertFight() const;
	/** The running expert fight (nullptr when none). */
	const FFVExpertFight* GetExpertFight() const;
	/** Expert fight rect on screen this frame (zero when none) — the HUD column must not overlap it (§10.12). */
	FFVRect GetExpertFieldRect() const;
	/** Autotest: the next bite (any water) is this fish on the equipped rod; expert fish use the expert fight. */
	void DebugForceExpertBite(FName FishId);
	/** Autotest: pins the expert progress like DebugPinProgress (P < 0 = off). */
	void DebugPinExpertProgress(float P);
	/** First arrival (§8): the next bite is the training fish for the equipped expert rod type (no escape, no snap,
	    [X] to leave). Also used for the first fight of every new rod type (ghost hand, §10.12). Called by GAME-FLOW. */
	void StartExpertTutorial();
	bool IsExpertTutorialPending() const;

private:
	UFVGame* Game = nullptr;
	EFVFishState State = EFVFishState::Idle;
	FFVMinigame Mini;
	bool bAutoPlay = false;

	// Implementation state (timers, bobber, hooked fish, result...) is private to FVFishing.cpp.
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
