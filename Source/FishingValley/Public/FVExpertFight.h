// UPDATE 6 — the expert fight, edition 2 «одна вертикаль» (Docs/UPDATE6_FOREST_CITY.md §19, Р60): ONE vertical track
// like the classic bar but longer (FVExpert::TrackH = 760 vs 568), the rod type is the control law of the bar(s)
// (Маятник / Двойчатка / Импульс / Магнит / Ползунок), the fish is the classic side icon moving on the same axis with
// the classic Dart / Mixed / Smooth / Floater / Sinker motion + tricks (Рывок, Свеча, Нырок, Челнок, Обманка,
// Прячется...), 1D hazards, progress + tension, legend scores in 1D, bots for the balance.
// Pure logic at a fixed 60 Hz (FV::SimDt), no UObject access, deterministic for a seed (own RNG streams).
//   FVExpertFight.cpp    [EXPERT-FIGHT]    FFVExpertFight: bars of the 5 types, fish motion + tricks, hazards, bots, sim
//   FVExpertLegends.cpp  [EXPERT-LEGENDS]  scores («партитуры») of the 6 expert legends + their playback
// The classic FFVMinigame (FVFishing.h) is NOT touched: classic and update-5 legend balance stays bit-identical.
//
// COORDINATES: every Y is a track position 0 (top) .. FVExpert::TrackH (bottom), +Y down, and every position is a
// CENTRE (fish icon centre, bar centre). Speeds are track units per second.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

namespace FVExpert
{
	constexpr float TrackH = 760.f;           // track length (classic FV::TrackH = 568: +34 %)
	constexpr float Mid = TrackH * 0.5f;
	constexpr float TrackScale = TrackH / 568.f;
	constexpr float FishIconH = 32.f;         // the fish icon, as in the classic (FFVMinigame::FishIconH)
	constexpr float FishMin = FishIconH * 0.5f;            // fish centre range
	constexpr float FishMax = TrackH - FishIconH * 0.5f;
	constexpr float InMargin = 10.f;          // the fish centre must be this far inside a bar (classic InBarMargin)
	constexpr float PressBufferSec = 0.08f;   // a tap inside one frame still gives at least one step (Android latency)
	constexpr float TensionRelax = 0.6f;      // tension per second without load
	/** The frozen enum keeps its 2D names: Compass = «Импульс», Helm = «Ползунок» (Р60). */
	constexpr EFVExpertRodType Impulse = EFVExpertRodType::Compass;
	constexpr EFVExpertRodType Slider = EFVExpertRodType::Helm;
}

/** Input of one step. bPressed/bReleased are edges.
 *  Slider («Ползунок»): TargetY >= 0 = the track Y the finger / mouse points at (the bar is led there with inertia);
 *  otherwise Stick.Y (-1 up .. 1 down, keyboard) accelerates the bar; nothing = the bar coasts. Other types ignore both. */
struct FFVExpertInput
{
	bool bDown = false;
	bool bPressed = false;
	bool bReleased = false;
	FVector2D Stick = FVector2D::ZeroVector;
	float TargetY = -1.f;
};

/** Everything a fight needs (copied: the fight never points into tables). */
struct FFVExpertSetup
{
	FFVExpertRodDef Rod;
	FFVExpertStats Stats;                     // FVDBU6::ComposeExpertStats (+ food)
	FFVExpertFishDef Fish;                    // the expert fish / legend (Fish.Legend != None = score playback)
	/** Р18: a classic fish on an expert rod — the classic motion of its difficulty only, no tricks / hazards. */
	bool bClassicFish = false;
	float ClassicDifficulty = 30.f;
	EFVMotion ClassicMotion = EFVMotion::Mixed;
	TArray<EFVHazard> Hazards;                // <= 2 (FVDBU6::ExpertHazards)
	bool bHasTreasure = false;
	bool bTutorial = false;                   // training fish: no escape, no snap, the [X] gives up
	bool bNoSnap = false;                     // first fight of a new rod type (§10.12): no line snap
	int32 Level = 11;
	// ---- 1D (Р60): the base motion of an expert fish / legend is the classic one of its FFVFishDef::Difficulty / Motion.
	// 0 / Count = looked up by Fish.FishId (FVDB::FindFish) at Start; unknown id -> derived from Fish.Speed / Base.
	float Difficulty = 0.f;
	EFVMotion Motion = EFVMotion::Count;
};

/** One bar of the rod (Twin: two). Track units, centre + height; Lo..Hi = where the bar's edges may go. */
struct FFVExpertZone
{
	float Y = FVExpert::Mid;                  // centre
	float Vel = 0.f;                          // units/s (+ = down)
	float H = 120.f;                          // height
	float Lo = 0.f;                           // Twin: the upper bar lives in [0, Mid + Overlap/2], the lower one in
	float Hi = FVExpert::TrackH;              //       [Mid - Overlap/2, TrackH]; others: the whole track
	bool bActive = true;
	float Top() const { return Y - H * 0.5f; }
	float Bottom() const { return Y + H * 0.5f; }
	/** Is a fish centre at P inside (Margin = FVExpert::InMargin for the fish)? */
	bool Contains(float P, float Margin = FVExpert::InMargin) const { return bActive && P >= Top() + Margin && P <= Bottom() - Margin; }
};

/** A running hazard (1D, §19 «Стихии»). */
struct FFVExpertHazardState
{
	EFVHazard Kind = EFVHazard::Current;
	float Warn = 0.f;                         // > 0: the warning is shown (seconds left)
	float Active = 0.f;                       // > 0: acting (seconds left); continuous hazards keep it > 0
	float Next = 3.f;                         // seconds to the next event (waves, drips, whirlpools, the spring)
	float Pos = FVExpert::Mid;                // Whirlpool: the centre it pulls to; Spring: the column centre
	float Dir = 1.f;                          // Current / Wave: +1 down, -1 up
	float Radius = 60.f;                      // Darkness: lit margin beyond the bar edges; Spring: half-height of the column
	float Strength = 0.f;                     // 0..1: how hard it acts right now (drawer: arrows / swirl intensity)
	/** Piles («Коряги»), Weeds («Водоросли»), Drip stones («Капель»), the Spring column: (X = top, Y = bottom). */
	TArray<FVector2D, TInlineAllocator<6>> Spans;
	TArray<float, TInlineAllocator<6>> SpanLife;  // Drip: seconds each stone stays (parallel to Spans)
	bool bTemp = false;                       // raised by a move (FFVExpertMove::Hazard); removed after TempLeft
	float TempLeft = 0.f;
};

/** The simulated player of the balance tests (§10.14 / §19): the classic bots' model — a reaction delay (mid 0.35 s,
 *  strong 0.2 s), a drifting aim error, a late notice of a new course, presses that land a moment late and overholds. */
struct FFVExpertBot
{
	float ReactionSec = 0.35f;                // the bot sees the fish this late
	float AimNoise = 10.f;                    // track units of aim error (drifting)
	float Anticipation = 0.5f;                // 0 = chases the fish .. 1 = leads it by its course
	bool bKnowsScore = false;                 // legends: «знаток» (knows the order of moves) vs «новичок»
	bool bCheese = false;                     // «сыр»: taps / holds blindly (must fail, < 5 %)
	float CourseLag = 0.2f;                   // seconds until a new course of the fish is believed
	float MotorLag = 0.05f;                   // max extra delay of a press / release (random 0..MotorLag)
	float OverholdChance = 0.f;               // chance a release (or a Pendulum flip) comes OverholdSec late
	float OverholdSec = 0.f;
	float Care = 0.5f;                        // 0..1: how well the tension is watched (Magnet rests, lets a red fish go)
	static FFVExpertBot Mid()
	{
		FFVExpertBot B; B.ReactionSec = 0.35f; B.AimNoise = 14.f; B.Anticipation = 0.4f; B.CourseLag = 0.25f;
		B.MotorLag = 0.08f; B.OverholdChance = 0.2f; B.OverholdSec = 0.15f; B.Care = 0.4f; return B;
	}
	static FFVExpertBot Strong()
	{
		FFVExpertBot B; B.ReactionSec = 0.2f; B.AimNoise = 10.f; B.Anticipation = 0.7f; B.CourseLag = 0.15f;
		B.MotorLag = 0.05f; B.OverholdChance = 0.08f; B.OverholdSec = 0.12f; B.Care = 0.85f; return B;
	}
	static FFVExpertBot Cheese() { FFVExpertBot B; B.bCheese = true; return B; }
};

/** What a bot sees of one step (FFVExpertFight::Seen ring; the bot reads it ReactionSec late). */
struct FFVExpertSeen
{
	float Pos = FVExpert::Mid;
	float Vel = 0.f;
	float Ghost = FVExpert::Mid;
	float Tip = FVExpert::Mid;                // where a Рывок / Свеча / Нырок aims (MoveB), valid when bTip
	bool bRed = false;
	bool bGhost = false;
	bool bHidden = false;
	bool bTip = false;
};

struct FFVExpertFight
{
	enum class EResult : uint8 { Running, Caught, Escaped, Snapped, GaveUp };

	FFVExpertSetup Setup;

	// ---- fish (the classic side icon; its centre)
	float FishPos = FVExpert::Mid;
	float FishVel = 0.f;                      // units/s (drawing: splashes / wiggle; bots)
	float FishAlpha = 1.f;                    // 0..1: hidden (Прячется / Обманка / weeds / darkness) -> draw bubbles
	bool bBubbles = false;                    // the fish is hidden: bubbles rise at FishPos
	bool bFishRed = false;                    // Рывок: holding it in a bar = x1.5 progress, tension grows; letting go is safe
	float RedT = 0.f;                         // seconds red
	bool bGhost = false;                      // Обманка: the double shown at GhostPos, the real fish shows bubbles
	float GhostPos = FVExpert::Mid;
	bool bFishAirborne = false;               // Свеча: out of the water at the top — in no bar, progress frozen
	bool bFrozen = false;                     // Нырок: lies still (drawer: no wiggle)
	bool bTrembling = false;                  // a Рывок / Свеча / lunge winds up (drawer: shake the icon)
	EFVExpertPattern Pattern = EFVExpertPattern::Snake;
	int32 MoveIndex = -1;                     // trick index (ordinary fish) / score step (legend) now; -1 = base motion
	float MoveT = 0.f;
	float MoveLen = 0.f;
	float Mirror = 1.f;                       // legends: +1 / -1 (random mirror «вверх / вниз» of the score move)
	float MoveA = 0.f;                        // pattern scratch: from / centre
	float MoveB = 0.f;                        // where the move aims: Рывок tip, Свеча / Нырок landing, hiding spot, Челнок end
	float MoveP = 0.f;

	// ---- bars
	FFVExpertZone Zones[2];
	int32 NumZones = 1;
	bool bGravityUp = false;                  // Маятник: the gravity points up now
	float PressBuffer = 0.f;
	float PushAge = 10.f;                     // Импульс: seconds since the last push (drawer flash)
	float SliderTarget = -1.f;                // Ползунок: where the bar is led now (-1 = coasting)
	bool bMagnetHeld = false;                 // Магнит: pulling now
	float HeldT = 0.f;                        // Магнит: seconds of the current pull

	// ---- meters
	float Progress = 0.3f;                    // 0 escaped .. 1 caught
	float Tension = 0.f;                      // 0..1 (1 = snap)
	bool bInZone = false;
	bool bInOverlap = false;                  // Twin: in both bars (x1.5)
	bool bFlawless = true;                    // never left the bars
	float Elapsed = 0.f;

	// ---- treasure (a chest drifts on the track; hold it in a bar; facts come from journal pages in it, §10.16)
	bool bTreasureVisible = false;
	float TreasurePos = FVExpert::Mid;
	float TreasureVel = 0.f;
	float TreasureProgress = 0.f;
	float TreasureAppear = 0.f;
	bool bTreasureCaught = false;

	// ---- hazards (<= 2 from the setup; a legend move may add its own for the move)
	TArray<FFVExpertHazardState, TInlineAllocator<3>> Hazards;

	// ---- signals for the drawer / audio (the warning of the next move)
	EFVIcon Signal = EFVIcon::Count;
	float SignalT = 0.f;                      // seconds the signal has been shown
	int32 SignalSerial = 0;                   // changes with every new signal (sfx trigger)
	FString SignalName;                       // the move's name (legends: collection «Приёмы»)
	int32 ScoreReached = 0;                   // legends: furthest score step reached (the «приём 3 из 5» card)

	FRandomStream Rng;                        // bars / treasure / misc
	FRandomStream FishRng;                    // fish motion
	FRandomStream HazardRng;                  // hazards

	/** Seeds the streams from Seed, places the bars by rod type, the fish low on the track (like the classic). */
	void Start(const FFVExpertSetup& InSetup, int32 Seed);
	/** One fixed 1/60 s step. */
	EResult Step(const FFVExpertInput& In);
	/** Bar physics of the rod type (the game and the bots share it). Dt = real seconds. */
	void StepZones(const FFVExpertInput& In, float Dt);
	/** Moves the fish by one move of data (tricks and legend score steps both use it). T = seconds into the move BEFORE
	 *  this step (0 on the first call: a new move is detected when T goes back or the pattern changes). */
	void StepMove(const FFVExpertMove& Move, float T, float Dt);
	/** Is a fish centre at P inside bar ZoneIndex (-1 = any)? */
	bool IsInZone(float P, int32 ZoneIndex = -1) const;
	/** Darkness: is a point on the track lit (near a bar)? true without darkness. */
	bool IsLit(float P) const;
	/** The bot's input for this step (delayed / noisy; legends use FVExpertLegends::BotAim when bKnowsScore). */
	FFVExpertInput BotInput(const FFVExpertBot& Bot) const;
	bool IsLegend() const { return Setup.Fish.Legend != EFVExpertLegend::None; }
	void RaiseSignal(EFVIcon Icon, const FString& Name);
	static EFVIcon DefaultSignal(EFVExpertPattern P);
	/** Tension limit (Прочность x the rod's reserve): the drawer may show it. */
	float TensionLimit() const;

	// ================= internals (public for FVExpertLegends / tests) =================
	FFVExpertMove CurMove;                    // the move StepMove plays now
	float CurMoveT = -1.f;                    // T of the last StepMove call (-1 = none yet)
	int32 MoveCalls = 0;
	int32 MoveStage = 0;
	float StageT = 0.f;
	float MoveDir = 1.f;                      // +1 down / -1 up: the side the move goes
	int32 MoveCount = 0;                      // Рывок: throws done
	float ScriptPos = 0.f;                    // scripted moves advance the fish by deltas of this (outside pulls persist)
	float MoveCool = 0.f;                     // Пугливая: cooldown of the next flight
	// classic motion (the Stardew algorithm per tick, on the 760 track)
	float FishDifficulty = 90.f;
	EFVMotion FishMotion = EFVMotion::Mixed;
	float ClassicTarget = -1.f;               // icon-top units, as FFVMinigame::FishTarget
	float ClassicSpeed = 0.f;                 // units per tick
	float ClassicAccel = 0.f;
	float FloatSink = 0.f;
	float BaseSpeedK = 1.f;                   // x the classic motion (legend interludes)
	// ordinary fish: tricks
	int32 PendingMove = -1;                   // trick being warned about (Signal shown)
	float WarnLeft = 0.f;
	float BaseLeft = 3.f;                     // seconds of base motion before the next warning
	// derived at Start
	float GainRate = 0.1f;
	float DrainRate = 0.1f;
	float RedTensionRate = 0.35f;
	float BarAccel = 900.f;                   // Маятник / Двойчатка: units/s² (0.25 / tick² = the classic bar)
	float BarMaxSpeed = 500.f;
	float BarBounce = 0.6f;
	float BarDrag = 0.f;
	float ImpulsePush = 330.f;                // Импульс: units/s per press
	float ImpulseBrake = 0.4f;                // Импульс: share of a falling speed a press cancels
	float MagnetPull = 260.f;                 // Магнит: units/s the fish is pulled at
	float MagnetDrift = 60.f;                 // Магнит: the bar's own crawl toward the fish
	float SliderOmega = 7.f;                  // Ползунок: spring (rad/s)
	float SliderKeyAccel = 1500.f;            // Ползунок: keyboard acceleration
	// phases by progress (55 % / 80 %): faster fish, tricks more often (never go back)
	int32 Phase = 0;
	float PhaseSpeed = 1.f;
	// legends: the opening lunge of a score move (a red bolt the way the move goes)
	float SurgeT = -1.f;                      // >= 0: a lunge plays (seconds in)
	float SurgeFrom = 0.f;
	float SurgeTo = 0.f;
	float SurgeJolt = 0.f;
	int32 LungePass = -1;
	float LungeLossK = 1.f;                   // x the progress a missed lunge takes (per legend)
	void StartLunge(float ToY);
	void StepLunge(float Dt);
	// bots
	FFVExpertInput LastInput;
	float LastPressT = -10.f;
	TArray<FFVExpertSeen> Seen;               // ring, SeenHead = next write
	int32 SeenHead = 0;
	mutable FRandomStream BotRng;             // BotInput is const: the bot's own memory is mutable
	mutable float BotErr = 0.f;
	mutable float BotErrTarget = 0.f;
	mutable float BotErrT = 0.f;
	mutable float BotVel = 0.f;               // the course the bot believes
	mutable float BotLastSeen = FVExpert::Mid;
	mutable float BotDelayT = -1.f;           // >= 0: a press / release / tap is on its way
	mutable bool bBotWant = false;            // what the bot's hand wants
	mutable bool bBotOut = false;             // what the bot's hand does now
	mutable bool bBotTapPending = false;
	mutable bool bBotPrevOut = false;
	mutable float BotTapT = 0.f;              // seconds since the bot's last tap
	mutable float BotRestT = 0.f;             // Магнит: a rest the bot is taking
	mutable float BotTargetY = FVExpert::Mid; // Ползунок: the finger
	mutable int32 BotGhostSerial = -1;        // Обманка: decided for this ghost
	mutable bool bBotFollowsGhost = false;
	mutable int32 BotTwinBar = 0;
	int32 GhostSerial = 0;                    // increments with every new ghost
};

struct FFVExpertSimResult
{
	float Rate = 0.f;                         // caught / trials
	float MedianSec = 0.f;                    // median of the caught fights (all fights when none was caught)
	float SnapRate = 0.f;
	float MedianScoreReached = 0.f;           // legends
	float InZoneFrac = 0.f;                   // share of the fight time with the fish inside a bar (all trials)
};

/** Headless balance (BALANCE_EXPERT): Trials fights of the bot. */
FFVExpertSimResult FVSimulateExpert(const FFVExpertSetup& Setup, const FFVExpertBot& Bot, int32 Trials, int32 Seed);

/** [EXPERT-LEGENDS] FVExpertLegends.cpp: the scores of the expert legends (§10.8) and their 1D playback (§19). */
namespace FVExpertLegends
{
	/** The fixed sequence of moves (their order never changes; the playback varies mirror, ±10 % time and amplitude). */
	const TArray<FFVExpertMove>& Score(EFVExpertLegend Legend);
	/** Called by FFVExpertFight::Start / Step when IsLegend(). */
	void Start(FFVExpertFight& F);
	void Step(FFVExpertFight& F, float Dt);
	/** «Знаток»: where the bot should put its bar knowing the move. false = no opinion (use the default bot). */
	bool BotAim(const FFVExpertFight& F, const FFVExpertBot& Bot, float& OutAimY);
}
