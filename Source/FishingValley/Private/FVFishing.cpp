// Fishing: power meter, cast, waiting/bite, the Stardew Valley BobberBar minigame, catch showcase.

#include "FVFishing.h"
#include "FVGame.h"
#include "FVScene.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVAudio.h"
#include "FVExpertFight.h"
#include "FVExpertDraw.h"

namespace
{
	/** .NET-style Random.Next(Min, Max): Min inclusive, Max exclusive (returns Min when the range is empty). */
	FORCEINLINE int32 NextInt(const FRandomStream& R, int32 Min, int32 MaxExclusive)
	{
		return MaxExclusive <= Min ? Min : Min + R.RandHelper(MaxExclusive - Min);
	}

	void Sfx(EFVSfx S, float Volume = 1.f, float Pitch = 1.f)
	{
		if (UFVAudio* A = UFVAudio::Get()) { A->Play(S, Volume, Pitch); }
	}

	void Reeling(bool bOn, float Intensity = 0.5f)
	{
		if (UFVAudio* A = UFVAudio::Get()) { A->SetReeling(bOn, Intensity); }
	}

	FORCEINLINE FLinearColor WithAlpha(FLinearColor C, float A) { C.A = FMath::Clamp(A, 0.f, 1.f); return C; }
	FORCEINLINE float Snap(double V) { return FMath::RoundToFloat(float(V)); }

	/** red (0) -> yellow (0.5) -> green (1) */
	FLinearColor MeterColor(float P)
	{
		P = FMath::Clamp(P, 0.f, 1.f);
		const FLinearColor Red = FV::Col(222, 60, 44), Yellow = FV::Col(246, 206, 58), Green = FV::Col(96, 204, 64);
		return P < 0.5f ? FMath::Lerp(Red, Yellow, P * 2.f) : FMath::Lerp(Yellow, Green, (P - 0.5f) * 2.f);
	}

	/** Height (texels) of the catch sprite the player holds above the head during the showcase. */
	float HeldIconH(UFVSprites* Sp, const FFVCatchResult& R)
	{
		if (Sp)
		{
			const FFVSprite* S = R.Fish ? &Sp->Fish(*R.Fish) : (R.Junk ? &Sp->Junk(R.Junk->Style) : nullptr);
			if (S && S->IsValid()) { return S->H; }
		}
		return 14.f;
	}

	constexpr float FishMaxPos = 532.f;        // Stardew: bobberPosition clamp
	constexpr float FishTargetMax = 548.f;     // Stardew: target clamp / space below
	constexpr float InBarMargin = 10.f;        // fish-icon center must be this far inside the bar (track units)
}

// Update 5 (Docs/UPDATE5_LEGENDS.md §1): every number of the legend fights. Seconds unless noted; track units
// (0..568, one tick = 1/60 s) for positions and speeds. Tuning knobs (§5): Gain first, then the per-legend one.
namespace FVLegendFight
{
	// ---- common
	constexpr float IntroFreeze = 0.7f;        // «Легенда на крючке!»: FVFishing does not step the minigame
	constexpr float BannerLife = 1.5f;         // every banner; a warning never starts while one is on screen
	constexpr float PhasePause = 0.6f;         // «в ярости», «последний рывок», «кольца»: everything stands still
	constexpr float BaseGain = 0.002f;         // classic progress per tick in the bar (x ReelMult x Gain)
	constexpr float BaseDrain = 0.003f;        // classic drain per tick out of the bar (x DrainMult; Gain never touches it)
	constexpr float MinReact = 0.45f;          // the bite window never gets shorter

	/** x progress gain inside the bar — the main difficulty knob. Spec: 0.9 / 0.85 / 0.8 / 0.75 / 0.6; tuned with the
	    autotest's BALANCE_LEGEND setups (§5: tackle «по уровню», human-strong with a Normal ukha; 2026-09-26):
	    the sturgeon 0.9 -> 0.75 (human-mid 92 % -> 87 %, the strong player's fight shows 2 tricks instead of 1),
	    the trout 0.85 -> 0.76 (human-mid 60 % -> 45 %, target 35..50), the angler 0.8 -> 0.7 (human-mid 98 % -> 97 %, 4 tricks),
	    the Zhar-ryba 0.6 -> 0.8 (human-strong ~1 % -> ~25 %, the owner's 18..28; 0.6 would FAIL the < 15 % check).
	    The strong targets of the sturgeon, trout, angler and serpent cannot be reached with the §5 knobs (strong stays
	    89..100 % whenever mid is in range) — see Docs/UPDATE5_LEGENDS.md §12. */
	float Gain(EFVLegend L)
	{
		switch (L)
		{
		case EFVLegend::Sturgeon: return 0.75f;
		case EFVLegend::CrystalTrout: return 0.76f;
		case EFVLegend::Angler: return 0.7f;
		case EFVLegend::FogSerpent: return 0.75f;
		case EFVLegend::ZharRyba: return 0.8f;
		case EFVLegend::MillMaster: return 0.6f;
		default: return 1.f;
		}
	}

	/** Bite window before Cur.ReactBonus (the Angler's starts after its «Огонёк»). */
	float ReactBase(EFVLegend L)
	{
		switch (L)
		{
		case EFVLegend::Sturgeon: return 0.9f;
		case EFVLegend::CrystalTrout: return 0.8f;
		case EFVLegend::Angler: return 0.8f;
		case EFVLegend::FogSerpent: return 0.7f;
		case EFVLegend::ZharRyba: return 0.6f;
		case EFVLegend::MillMaster: return 0.6f;
		default: return 1.f;
		}
	}

	/** LegendWarn pitch: every legend sounds its own warning. */
	float WarnPitch(EFVLegend L)
	{
		switch (L)
		{
		case EFVLegend::Sturgeon: return 0.8f;
		case EFVLegend::CrystalTrout: return 1.35f;
		case EFVLegend::Angler: return 1.1f;
		case EFVLegend::FogSerpent: return 0.9f;
		case EFVLegend::ZharRyba: return 0.7f;
		case EFVLegend::MillMaster: return 0.55f;   // a deep, dull knock
		default: return 1.f;
		}
	}

	// ---- Silver sturgeon «Против течения»
	constexpr float StFirstCalmMin = 0.6f, StFirstCalmMax = 1.1f;
	constexpr float StCalmMin = 3.0f, StCalmMax = 4.5f;
	constexpr float StWarn = 0.9f;
	constexpr float StCurrentMin = 2.0f, StCurrentMax = 3.0f;
	constexpr float StCurrent = 0.09f;         // added to BarSpeed every tick (down): falls 0.34, rises 0.16
	constexpr float StLungeMin = 160.f, StLungeMax = 240.f;   // the fish tears upwards when the current starts

	// ---- Crystal trout «Прозрачность», «Блики»
	constexpr float TrFirstSeenMin = 0.7f, TrFirstSeenMax = 1.2f;
	constexpr float TrSeenMin = 2.5f, TrSeenMax = 3.5f;
	constexpr float TrFlicker = 0.8f, TrFlickerHz = 8.f, TrFlickerLow = 0.35f;
	constexpr float TrClearMin = 2.0f, TrClearMax = 2.8f;
	constexpr float TrClearAlpha = 0.08f, TrFadeOut = 0.2f, TrFadeIn = 0.25f;
	constexpr float TrGlintMin = 0.45f, TrGlintMax = 0.7f, TrGlintFade = 3.3f;

	// ---- Giant angler «Огонёк», «Фонарик», «Бросок», «Обман», «Жуёт»
	constexpr float AnGlow = 1.0f;             // pre-hook glow (Bite sub-state): presses ignored, no «!»
	constexpr float AnGlowBotDelay = 0.12f;
	constexpr float AnAlpha = 0.7f;            // the fish icon in the dark
	constexpr float AnFirstCalmMin = 0.3f, AnFirstCalmMax = 0.8f;
	constexpr float AnCalmMin = 2.5f, AnCalmMax = 3.5f;
	constexpr float AnLantern = 1.2f;          // the real lantern: its ring closes -> lunge
	constexpr float AnLanternMin = 140.f, AnLanternMax = 240.f;
	constexpr float AnLunge = 0.4f, AnLungeMaxSpeed = 10.f, AnLungeMaxTime = 1.2f;
	constexpr float AnFakeChance = 0.35f, AnFake = 0.7f, AnFakeHz = 10.f;
	constexpr float AnTwitchMin = 20.f, AnTwitchMax = 30.f;
	constexpr float AnChew = 1.2f, AnChewAmp = 10.f, AnChewHz = 6.f, AnChewGain = 1.3f;

	// ---- Fog serpent «Туман», «Пузырьки», «Петли», «Кольца»
	constexpr float FsFogH = 128.f;
	constexpr float FsFogSpeedMin = 0.35f, FsFogSpeedMax = 0.6f;
	constexpr float FsFirstFogMin = 1.5f, FsFirstFogMax = 2.0f;   // the opening: no disappearance before it ends
	constexpr float FsFirstDistMin = 100.f, FsFirstDistMax = 160.f;   // band 0 center <-> fish center at the start
	constexpr float FsHoming = 2.5f;           // band 0 homes in on the fish during the opening (at most, per tick)
	// owner 2026-09-26: in the fog the serpent disappears COMPLETELY (was 0.15) — only the mint bubbles betray it
	constexpr float FsFogAlpha = 0.f, FsFogFade = 0.15f;
	constexpr float FsFogAhead = 0.8f;         // the cloud pictogram lights this long before the fish enters
	constexpr float FsReentry = 0.25f;         // out of the fog this long: the next entry is a new trick
	constexpr float FsBubbleEvery = 0.3f, FsBubbleRise = 30.f, FsBubbleLife = 0.8f;
	// the serpent's knob (§5): loops 45 -> 40, coils 65 -> 52 (human-mid 6 % -> 19 %, target 15..30)
	constexpr float FsLoopAmp = 40.f, FsLoopPeriod = 1.6f;
	constexpr float FsCoilAt = 0.6f, FsCoilAmp = 52.f, FsCoilPeriod = 1.2f, FsCoilFog = 1.3f;

	// ---- Zhar-ryba: 3 phases «Пламя», «Ярость», «Последний рывок»
	constexpr float ZhRageAt = 0.5f, ZhLastAt = 0.8f;
	constexpr float ZhFirstCalmMin = 0.5f, ZhFirstCalmMax = 1.0f;
	constexpr float ZhCalm0Min = 2.5f, ZhCalm0Max = 3.5f, ZhBoil0 = 1.0f, ZhZone0 = 110.f;
	constexpr float ZhCalm1Min = 2.0f, ZhCalm1Max = 3.0f, ZhBoil1 = 0.8f, ZhZone1 = 130.f;
	constexpr float ZhCalm2Min = 4.5f, ZhCalm2Max = 6.0f;
	constexpr float ZhErupt = 1.2f;
	constexpr float ZhOnBarChance = 0.6f;      // the zone centers on the bar (else on the fish)
	constexpr float ZhBurnOverlap = 0.25f;     // x BarHeight inside the lava burns
	constexpr float ZhBurn = 0.0025f;          // progress per tick while burning
	constexpr float ZhShrink = 1.5f, ZhShrinkTo = 0.75f;
	constexpr float ZhDartMult = 1.3f;         // rage and later
	constexpr float ZhBlinkMin = 2.0f, ZhBlinkMax = 2.6f, ZhBlinkAfterErupt = 0.5f;
	constexpr float ZhBlinkMinD = 100.f, ZhBlinkMaxD = 200.f;
	constexpr float ZhBlink = 0.8f, ZhBlinkFreezeAfter = 0.3f;
	constexpr float ZhHeat = 0.0005f;          // last dash: extra drain per tick while the fish is out of the bar
	// owner 2026-09-26 («слишком трудная», then after playing 0.5: «отличная, сделай 0.4»): every loss of the catch meter
	// against the Zhar-ryba — the drain out of the bar, the lava burn and the last-dash heat — is ×0.4
	constexpr float ZhLossMult = 0.3f;   // update 6 (Р46): was 0.4

	// ---- update 6 (§18, Р53): «Хозяин омута» — 4 phases «Омут», «Жернова», «Муть», «Последний рывок».
	// Balance (Р53): Difficulty 121; human-strong ~0.9 x the Zhar-ryba's share, human-mid not above it, the median
	// fight not shorter (FVSimulateLegend with the BALANCE_LEGEND setups; knobs: MmLossMult first, then Gain).
	constexpr float MmStonesAt = 0.45f, MmSiltAt = 0.65f, MmLastAt = 0.85f;
	constexpr float MmPull = 0.03f;            // «Омут»: the whirl under the wheel drags the bar down, always (added to BarSpeed)
	constexpr float MmSurge = 0.05f;           // phase 0: a whirl surge adds this for MmSurgeT after a MmWhirlWarn warning
	constexpr float MmFirstCalmMin = 0.6f, MmFirstCalmMax = 1.0f;
	constexpr float MmCalm0Min = 2.2f, MmCalm0Max = 3.2f, MmWhirlWarn = 0.7f, MmSurgeT = 1.4f;
	// «Жернова»: a dull knock (MmThud), then a heavy jerk of MmJerkMin..Max in MmJerk s; the rhythm is ~2 s
	constexpr float MmThud = 0.6f, MmJerk = 0.35f, MmJerkMaxSpeed = 12.f, MmJerkMaxTime = 0.6f;
	constexpr float MmJerkMin = 130.f, MmJerkMax = 210.f;
	constexpr float MmStoneCalmMin = 0.95f, MmStoneCalmMax = 1.15f;
	// «Муть»: two silt clouds drift; inside, the fish shows only in swirls (alpha MmSiltLo..MmSiltHi, never gone)
	constexpr float MmSiltH = 120.f, MmSiltSpeedMin = 0.3f, MmSiltSpeedMax = 0.5f;
	constexpr float MmSiltLo = 0.12f, MmSiltHi = 0.5f, MmSiltHz = 1.6f, MmSiltFade = 0.2f, MmSiltReentry = 0.25f;
	// «Последний рывок»: the catfish tears towards the surface (floats up), jerks upwards again, faster
	constexpr float MmLastCalmMin = 0.9f, MmLastCalmMax = 1.3f, MmLastThud = 0.5f;
	constexpr float MmLastJerkMin = 140.f, MmLastJerkMax = 220.f;
	// tuned 2026-09-27 with the BALANCE_LEGEND setups (line_titan, the level tackle): Gain 0.6, loss x1.15 -> human-strong
	// ~91 % (the Zhar-ryba 100 %), human-mid ~45 %, median 29.5 s / 23.7 s (the Zhar-ryba 20.6 s / 16.3 s)
	constexpr float MmLossMult = 1.15f;        // every loss of the catch meter (the drain out of the bar)
	// Р65 (owner 2026-09-27): the loss after a jerk is halved — in «Жернова» / «Последний рывок» the drain is x0.5 while
	// the jerk flies and in the calm after it (only the knock before the next jerk keeps the full loss)
	// (measured after: human-strong 91 %, human-mid 61 %, median 24.3 s / 20.0 s — the Р53 checks still pass)
	constexpr float MmJerkLossK = 0.5f;

	/** From + / - [MinD, MaxD] towards the side that has room (random when both have it), kept in 0..MaxPos. */
	float OffsetWithRoom(const FRandomStream& R, float From, float MinD, float MaxD, float MaxPos)
	{
		const float Dist = R.FRandRange(MinD, MaxD);
		const bool bUp = From - Dist >= 0.f, bDown = From + Dist <= MaxPos;
		float Dir;
		if (bUp && bDown) { Dir = R.FRand() < 0.5f ? -1.f : 1.f; }
		else if (bUp || bDown) { Dir = bUp ? -1.f : 1.f; }
		else { Dir = From > MaxPos * 0.5f ? -1.f : 1.f; }
		return FMath::Clamp(From + Dir * Dist, 0.f, MaxPos);
	}
}

// 0.5: «Бой с легендой (тест)» — the tackle meant for each legend's level (spec §5 «снасть по уровню», no food).
namespace FVTrialImpl
{
	struct FSetup { const TCHAR* Fish; int32 Level; const TCHAR* Rod; const TCHAR* Line; const TCHAR* Float; };
	const FSetup Setups[] = {
		{ TEXT("fish_tsar_carp"),       4,  TEXT("rod_carbon"),   TEXT("line_braided"), TEXT("float_sliding") },
		{ TEXT("fish_silver_sturgeon"), 5,  TEXT("rod_carbon"),   TEXT("line_silk"),    TEXT("float_glow") },
		{ TEXT("fish_crystal_trout"),   6,  TEXT("rod_titanium"), TEXT("line_silk"),    TEXT("float_glow") },
		{ TEXT("fish_giant_angler"),    8,  TEXT("rod_iridium"),  TEXT("line_fluoro"),  TEXT("float_glow") },
		{ TEXT("fish_fog_serpent"),     9,  TEXT("rod_iridium"),  TEXT("line_titan"),   TEXT("float_gold") },
		{ TEXT("fish_zhar_ryba"),       10, TEXT("rod_poseidon"), TEXT("line_heat"),    TEXT("float_gold") },
		{ TEXT("fish_mill_master"),     10, TEXT("rod_poseidon"), TEXT("line_titan"),   TEXT("float_gold") },   // update 6 (§18)
	};

	const FSetup* Find(FName FishId)
	{
		for (const FSetup& S : Setups) { if (FishId == FName(S.Fish)) { return &S; } }
		return nullptr;
	}

	/** The trial's rod stats and player level; false when the fish has no trial setup (then the player's own tackle). */
	bool Stats(FName FishId, FFVRodStats& Out, int32& OutLevel)
	{
		const FSetup* S = Find(FishId);
		const FFVRodDef* Rod = S ? FVDB::FindRod(FName(S->Rod)) : nullptr;
		if (!Rod) { return false; }
		Out = FVDB::ComposeRodStats(*Rod, FName(S->Line), FName(S->Float), nullptr);
		OutLevel = S->Level;
		return true;
	}
}

// Update 4b: the practice pond of the home yard («учебная рыбка»: no bait, no XP, no records, no keepnet).
namespace FVFishingPracticeImpl
{
	constexpr float BiteMin = 2.f, BiteMax = 4.f;   // seconds until the practice fish bites
	constexpr float ReactWindow = 1.6f;              // generous reaction window
	constexpr float Difficulty = 15.f;               // an easy, Mixed-motion fish
	constexpr float ExtraBar = 32.f;                 // a wider catch bar

	const TCHAR* TryAgainText() { return TEXT("Не беда, попробуйте ещё раз"); }
	const TCHAR* StillHookedText() { return TEXT("Рыбка не уплывёт!"); }   // short: it rises over the head on phones
	constexpr float GiveUpSizeP = 30.f, GiveUpSizeL = 16.f;   // the «give up» [X]: thumb-sized on phones

	/** Greedy word wrap into lines no wider than MaxW (a single long word stays on its own line). */
	TArray<FString> WrapLines(const FVDraw& D, const FString& Text, float MaxW, float Size, bool bBold)
	{
		TArray<FString> Words;
		Text.ParseIntoArray(Words, TEXT(" "), true);
		TArray<FString> Lines;
		FString Cur;
		for (const FString& Wd : Words)
		{
			const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
			if (!Cur.IsEmpty() && D.MeasureText(Try, Size, bBold).X > MaxW)
			{
				Lines.Add(Cur);
				Cur = Wd;
			}
			else
			{
				Cur = Try;
			}
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
		return Lines;
	}
}

// =============================================================================================
// FFVMinigame — port of Stardew Valley's BobberBar (60 ticks per second)
// =============================================================================================
void FFVMinigame::Start(float InDifficulty, EFVMotion InMotion, float InBarHeight, float InReelMult, float InDrainMult, bool bInHasTreasure, int32 Seed,
	EFVLegend InLegend)
{
	Difficulty = FMath::Max(1.f, InDifficulty);
	Motion = InMotion;
	BarHeight = FMath::Clamp(InBarHeight, 24.f, FV::TrackH - 8.f);
	ReelMult = FMath::Max(0.05f, InReelMult);
	DrainMult = FMath::Max(0.05f, InDrainMult);
	bHasTreasure = bInHasTreasure;
	Rng.Initialize(Seed);

	// update 5: the legend state (its own stream: the classic Rng sequence is untouched)
	Legend = InLegend < EFVLegend::Count ? InLegend : EFVLegend::None;
	LegendGain = FVLegendFight::Gain(Legend);   // 1 for None / TsarCarp (the classic Step never reads it)
	BaseBarHeight = BarHeight;
	LegendRng.Initialize(int32(uint32(Seed) ^ 0x4C454745u));
	LegendT = 0.f;
	PauseT = 0.f;
	ProgressFreezeT = 0.f;
	HazardState = 0;
	HazardTimer = 0.f;
	HazardPos = 0.f;
	HazardH = 0.f;
	Current = 0.f;
	FishAlpha = 1.f;
	SeenFishPos = 508.f;
	SeenFishSpeed = 0.f;
	Glint = 0.f;
	FogPos[0] = FogPos[1] = 0.f;
	FogVel[0] = FogVel[1] = 0.f;
	WavePhase = 0.f;
	WaveAmp = 0.f;
	WavePeriod = 1.6f;
	Phase = 0;
	PhaseFlash = 0.f;
	BurnFlash = 0.f;
	BlinkState = 0;
	BlinkTarget = 0.f;
	BlinkTimer = 0.f;
	LungeTime = 0.f;
	bFeinted = false;
	bFakeLantern = false;
	BarShrinkT = 0.f;
	TrickCount = 0;
	Phase0Eruptions = 0;
	HazardAge = 0.f;
	BannerT = 0.f;
	MarkTimer = 0.f;
	LungeSpeed = 0.f;
	FogOutT = 0.f;
	FogOpenT = 0.f;
	for (int32 k = 0; k < 4; ++k) { BubbleY[k] = 0.f; BubbleAge[k] = 9.f; }
	BubbleNext = 0;
	Banner = ELegendBanner::None;
	BannerSerial = 0;

	BarPos = FV::TrackH - BarHeight;       // the bar starts at the bottom
	BarSpeed = 0.f;
	FishPos = 508.f;
	FishSpeed = 0.f;
	FishTarget = (100.f - FMath::Min(100.f, Difficulty)) / 100.f * FishTargetMax;
	FishAccel = 0.f;
	FloatSink = 0.f;
	Progress = 0.3f;
	bFishInBar = false;
	bPerfect = true;
	TreasurePos = 0.f;
	TreasureProgress = 0.f;
	TreasureAppearTimer = Rng.FRandRange(1.f, 3.f);
	bTreasureVisible = false;
	bTreasureInBar = false;
	bTreasureCaught = false;
	Elapsed = 0.f;
	Shake = 0.f;
	bNoEscape = false;
	bEmptied = false;
	if (HasLegendRules()) { StartLegendState(); }   // only LegendRng: the classic Rng sequence above is unchanged
}

FFVMinigame::EResult FFVMinigame::Step(bool bHold)
{
	if (HasLegendRules()) { return StepLegend(bHold); }   // update 5: the legend rules live in StepLegend

	const float Dt = FV::SimDt;
	Elapsed += Dt;

	// ---------------------------------------------------------------- fish (Stardew algorithm)
	const bool bSmooth = Motion == EFVMotion::Smooth;
	if (Rng.FRand() < Difficulty * (bSmooth ? 20.f : 1.f) / 4000.f && (!bSmooth || FishTarget == -1.f))
	{
		const float SpaceBelow = FishTargetMax - FishPos;
		const float SpaceAbove = FishPos;
		const float Percent = FMath::Min(99.f, Difficulty + float(NextInt(Rng, 10, 45))) / 100.f;
		FishTarget = FishPos + float(NextInt(Rng, int32(FMath::Min(-SpaceAbove, SpaceBelow)), int32(SpaceBelow))) * Percent;
	}
	if (Motion == EFVMotion::Floater) { FloatSink = FMath::Max(FloatSink - 0.01f, -1.5f); }
	else if (Motion == EFVMotion::Sinker) { FloatSink = FMath::Min(FloatSink + 0.01f, 1.5f); }

	if (FMath::Abs(FishPos - FishTarget) > 3.f && FishTarget != -1.f)
	{
		FishAccel = (FishTarget - FishPos) / (float(NextInt(Rng, 10, 30)) + (100.f - FMath::Min(100.f, Difficulty)));
		FishSpeed += (FishAccel - FishSpeed) / 5.f;
	}
	else if (!bSmooth && Rng.FRand() < Difficulty / 2000.f)
	{
		FishTarget = FishPos + (Rng.FRand() < 0.5f ? float(NextInt(Rng, -100, -51)) : float(NextInt(Rng, 50, 101)));
	}
	else
	{
		FishTarget = -1.f;
	}
	if (Motion == EFVMotion::Dart && Rng.FRand() < Difficulty / 1000.f)
	{
		const int32 D2 = int32(Difficulty) * 2;
		FishTarget = FishPos + (Rng.FRand() < 0.5f ? float(NextInt(Rng, -100 - D2, -51)) : float(NextInt(Rng, 50, 101 + D2)));
	}
	FishTarget = FMath::Clamp(FishTarget, -1.f, FishTargetMax);
	FishPos = FMath::Clamp(FishPos + FishSpeed + FloatSink, 0.f, FishMaxPos);

	// ---------------------------------------------------------------- catch bar (hold = up)
	const float MaxBar = FV::TrackH - BarHeight;
	const float Gravity = bHold ? -0.25f : 0.25f;
	if (bHold && (BarPos <= 0.001f || BarPos >= MaxBar - 0.001f)) { BarSpeed = 0.f; }
	BarSpeed += Gravity;
	BarPos += BarSpeed;
	if (BarPos + BarHeight > FV::TrackH)
	{
		BarPos = MaxBar;
		BarSpeed = -BarSpeed * 2.f / 3.f;
	}
	else if (BarPos < 0.f)
	{
		BarPos = 0.f;
		BarSpeed = -BarSpeed * 2.f / 3.f;
	}

	auto InBar = [this](float IconTop)
	{
		const float C = IconTop + FishIconH * 0.5f;
		return C >= BarPos + InBarMargin && C <= BarPos + BarHeight - InBarMargin;
	};
	bFishInBar = InBar(FishPos);

	// ---------------------------------------------------------------- treasure
	bTreasureInBar = false;
	if (bHasTreasure && !bTreasureCaught)
	{
		if (!bTreasureVisible)
		{
			TreasureAppearTimer -= Dt;
			if (TreasureAppearTimer <= 0.f)
			{
				bTreasureVisible = true;
				// Stardew spawns the chest away from the bar
				TreasurePos = BarPos > 274.f
					? float(NextInt(Rng, 8, int32(BarPos) - 20))
					: float(NextInt(Rng, FMath::Min(528, int32(BarPos + BarHeight)), 500));
				TreasurePos = FMath::Clamp(TreasurePos, 0.f, FishMaxPos);
			}
		}
		else
		{
			bTreasureInBar = InBar(TreasurePos);
			if (bTreasureInBar)
			{
				TreasureProgress += 0.0135f;
				if (TreasureProgress >= 1.f)
				{
					TreasureProgress = 1.f;
					bTreasureCaught = true;
					bTreasureVisible = false;
				}
			}
			else
			{
				TreasureProgress = FMath::Max(0.f, TreasureProgress - 0.01f);
			}
		}
	}

	// ---------------------------------------------------------------- progress
	if (bFishInBar)
	{
		Progress += 0.002f * ReelMult;
		Shake = FMath::Max(0.f, Shake - Dt * 5.f);
	}
	else
	{
		Progress -= 0.003f * DrainMult;
		bPerfect = false;
		Shake = 1.f;
	}
	Progress = FMath::Clamp(Progress, 0.f, 1.f);
	if (Progress >= 1.f) { return EResult::Caught; }
	if (Progress <= 0.f)
	{
		bEmptied = true;
		if (!bNoEscape) { return EResult::Escaped; }   // the practice fish stays on the hook (the [X] gives up)
	}
	return EResult::Running;
}

bool FFVMinigame::BotShouldHold(float Skill) const
{
	Skill = FMath::Clamp(Skill, 0.f, 1.f);
	const float Look = 3.f + 9.f * Skill;   // ticks of anticipation
	return BotShouldHoldLook(Look);
}

bool FFVMinigame::BotShouldHoldLook(float Look) const
{
	if (HasLegendRules()) { return LegendBotHold(Look); }   // update 5: the fair bot (sees only what the player sees)
	const float FishC = FishPos + FishIconH * 0.5f + (FishSpeed + FloatSink) * Look * 0.6f;
	const float BarC = BarPos + BarHeight * 0.5f + BarSpeed * Look;
	return BarC > FishC;                    // bar center below the fish -> push up
}

// =============================================================================================
// FFVMinigame — update 5 legend rules (Docs/UPDATE5_LEGENDS.md §1). Only reached when HasLegendRules() (Sturgeon
// and later). Every legend decision rolls LegendRng; the Stardew fish motion keeps rolling Rng as in Step.
// =============================================================================================
namespace
{
	constexpr float LegendTwoPi = 6.28318530718f;

	/** Per-tick speed of the fish as the player sees it move (the serpent's loops included). */
	float LegendVisibleSpeed(const FFVMinigame& M)
	{
		float V = M.FishSpeed + M.FloatSink;
		if (M.WaveAmp > 0.f) { V += M.WaveAmp * FMath::Cos(M.WavePhase) * LegendTwoPi / FMath::Max(0.1f, M.WavePeriod) * FV::SimDt; }
		return V;
	}
}

void FFVMinigame::SetHazard(uint8 State, float Seconds)
{
	HazardState = State;
	HazardTimer = Seconds;
	HazardAge = 0.f;
}

void FFVMinigame::RaiseBanner(ELegendBanner B)
{
	Banner = B;
	++BannerSerial;
	BannerT = FVLegendFight::BannerLife;
	PhaseFlash = FVLegendFight::BannerLife;
}

void FFVMinigame::StartLegendState()
{
	using namespace FVLegendFight;
	// the hook banner stays on screen 0.8 s after the 0.7 s intro freeze: no warning starts under it
	BannerT = BannerLife - IntroFreeze;
	SeenFishPos = FishPos;
	SeenFishSpeed = 0.f;
	switch (Legend)
	{
	case EFVLegend::Sturgeon:
		SetHazard(0, LegendRng.FRandRange(StFirstCalmMin, StFirstCalmMax));
		break;
	case EFVLegend::CrystalTrout:
		SetHazard(0, LegendRng.FRandRange(TrFirstSeenMin, TrFirstSeenMax));
		break;
	case EFVLegend::Angler:
		FishAlpha = AnAlpha;
		SetHazard(0, LegendRng.FRandRange(AnFirstCalmMin, AnFirstCalmMax));
		break;
	case EFVLegend::FogSerpent:
	{
		WaveAmp = FsLoopAmp;
		WavePeriod = FsLoopPeriod;
		FogOutT = FsReentry;                   // the first fog entry is a trick too
		// the opening (1.5..2 s): the fog cannot hide the fish yet (at difficulty 100 it dashes across the whole track
		// first); band 0 starts 100..160 from the fish and homes in on it (StepLegendFog), so the first disappearance
		// comes as the opening ends; band 1 wanders in the other half of the track
		FogOpenT = LegendRng.FRandRange(FsFirstFogMin, FsFirstFogMax);
		const float MaxTop = FV::TrackH - FsFogH;
		const float FishC = FishPos + FishIconH * 0.5f;
		const float Dist = LegendRng.FRandRange(FsFirstDistMin, FsFirstDistMax);
		const float C0 = FishC - Dist - FsFogH * 0.5f >= 0.f ? FishC - Dist : FishC + Dist;
		FogPos[0] = FMath::Clamp(C0 - FsFogH * 0.5f, 0.f, MaxTop);
		FogVel[0] = C0 < FishC ? FsFogSpeedMin : -FsFogSpeedMin;
		const bool bUpperHalf = FogPos[0] + FsFogH * 0.5f < FV::TrackH * 0.5f;
		FogPos[1] = bUpperHalf ? LegendRng.FRandRange(FV::TrackH * 0.55f, MaxTop) : LegendRng.FRandRange(0.f, FV::TrackH * 0.45f - FsFogH);
		const float V1 = LegendRng.FRandRange(FsFogSpeedMin, FsFogSpeedMax);
		FogVel[1] = LegendRng.FRand() < 0.5f ? -V1 : V1;
		SetHazard(0, 0.f);
		break;
	}
	case EFVLegend::ZharRyba:
		SetHazard(0, LegendRng.FRandRange(ZhFirstCalmMin, ZhFirstCalmMax));
		break;
	case EFVLegend::MillMaster:
		Current = MmPull;                      // «Омут»: the whirl drags the bar down from the first tick
		SetHazard(0, LegendRng.FRandRange(MmFirstCalmMin, MmFirstCalmMax));
		break;
	default:
		break;
	}
}

FFVMinigame::EResult FFVMinigame::StepLegend(bool bHold)
{
	using namespace FVLegendFight;
	const float Dt = FV::SimDt;
	Elapsed += Dt;
	BannerT = FMath::Max(0.f, BannerT - Dt);
	PhaseFlash = FMath::Max(0.f, PhaseFlash - Dt);
	BurnFlash = FMath::Max(0.f, BurnFlash - Dt * 4.f);
	Glint = FMath::Max(0.f, Glint - Dt * TrGlintFade);
	for (float& A : BubbleAge) { A += Dt; }
	if (PauseT > 0.f)
	{
		// a phase pause under its banner: the fish, the bar and the progress stand still
		PauseT = FMath::Max(0.f, PauseT - Dt);
		return EResult::Running;
	}
	LegendT += Dt;
	HazardAge += Dt;

	StepLegendHazards(Dt);
	StepLegendFish(Dt);
	if (Legend == EFVLegend::FogSerpent) { StepLegendFog(Dt); }

	// ---------------------------------------------------------------- catch bar (hold = up)
	if (Legend == EFVLegend::ZharRyba && Phase >= 1 && BarShrinkT < 1.f)
	{
		// rage: the bar shrinks around its center to 0.75 x its height in 1.5 s
		BarShrinkT = FMath::Min(1.f, BarShrinkT + Dt / ZhShrink);
		const float E = BarShrinkT * BarShrinkT * (3.f - 2.f * BarShrinkT);
		const float NewH = BaseBarHeight * FMath::Lerp(1.f, ZhShrinkTo, E);
		BarPos += (BarHeight - NewH) * 0.5f;
		BarHeight = NewH;
		BarPos = FMath::Clamp(BarPos, 0.f, FV::TrackH - BarHeight);
	}
	const float MaxBar = FV::TrackH - BarHeight;
	const float Gravity = bHold ? -0.25f : 0.25f;
	if (bHold && (BarPos <= 0.001f || BarPos >= MaxBar - 0.001f)) { BarSpeed = 0.f; }
	BarSpeed += Gravity + Current;         // the sturgeon's current drags the bar down
	BarPos += BarSpeed;
	if (BarPos + BarHeight > FV::TrackH)
	{
		BarPos = MaxBar;
		BarSpeed = -BarSpeed * 2.f / 3.f;
	}
	else if (BarPos < 0.f)
	{
		BarPos = 0.f;
		BarSpeed = -BarSpeed * 2.f / 3.f;
	}

	const bool bGone = BlinkState == 1;    // the Zhar-ryba blinked away: it is nowhere until it reappears
	const float FishC = FishPos + FishIconH * 0.5f;
	bFishInBar = !bGone && FishC >= BarPos + InBarMargin && FishC <= BarPos + BarHeight - InBarMargin;
	if (!bGone && FishAlpha >= 0.3f)
	{
		SeenFishPos = FishPos;
		SeenFishSpeed = LegendVisibleSpeed(*this);
	}

	// ---------------------------------------------------------------- progress
	if (bGone || ProgressFreezeT > 0.f)
	{
		// frozen while the fish is gone and 0.3 s after it is back: progress, bPerfect and the shake all wait
		ProgressFreezeT = FMath::Max(0.f, ProgressFreezeT - Dt);
	}
	else
	{
		if (bFishInBar)
		{
			const float Chew = (Legend == EFVLegend::Angler && HazardState == 3) ? AnChewGain : 1.f;
			Progress += BaseGain * ReelMult * LegendGain * Chew;
			Shake = FMath::Max(0.f, Shake - Dt * 5.f);
		}
		else
		{
			const float Loss = BaseDrain * DrainMult + ((Legend == EFVLegend::ZharRyba && Phase >= 2) ? ZhHeat : 0.f);
			const float MmK = (Phase == 1 || Phase >= 3) && HazardState != 1 ? MmLossMult * MmJerkLossK : MmLossMult;
			Progress -= Legend == EFVLegend::ZharRyba ? Loss * ZhLossMult : (Legend == EFVLegend::MillMaster ? Loss * MmK : Loss);
			bPerfect = false;
			Shake = 1.f;
		}
		if (Legend == EFVLegend::ZharRyba && HazardState == 2)
		{
			// the eruption burns a bar that covers >= 25 % of its height with lava (the fish still pays in the bar)
			const float Overlap = FMath::Min(BarPos + BarHeight, HazardPos + HazardH) - FMath::Max(BarPos, HazardPos);
			if (Overlap >= ZhBurnOverlap * BarHeight)
			{
				Progress -= ZhBurn * ZhLossMult;
				BurnFlash = 1.f;
			}
		}
	}
	Progress = FMath::Clamp(Progress, 0.f, 1.f);

	// ---------------------------------------------------------------- phases (never back)
	if (Legend == EFVLegend::ZharRyba)
	{
		if (Phase == 0 && Progress >= ZhRageAt)
		{
			Phase = 1;
			PauseT = PhasePause;
			RaiseBanner(ELegendBanner::Rage);
		}
		else if (Phase == 1 && Progress >= ZhLastAt)
		{
			Phase = 2;
			PauseT = PhasePause;
			BlinkTimer = LegendRng.FRandRange(ZhBlinkMin, ZhBlinkMax);
			RaiseBanner(ELegendBanner::LastDash);
		}
	}
	else if (Legend == EFVLegend::MillMaster)
	{
		// «Омут» -> «Жернова» -> «Муть» -> «Последний рывок»; a surge or a jerk in flight ends first
		const float At = Phase == 0 ? MmStonesAt : (Phase == 1 ? MmSiltAt : (Phase == 2 ? MmLastAt : 2.f));
		if (Progress >= At && !(HazardState == 2 && Phase != 2))
		{
			++Phase;
			PauseT = PhasePause;
			Current = MmPull;
			FishTarget = -1.f;
			if (Phase == 1)
			{
				SetHazard(0, LegendRng.FRandRange(MmStoneCalmMin, MmStoneCalmMax));
				RaiseBanner(ELegendBanner::Rage);      // «Жернова!» (FVFishing picks the text by the legend)
			}
			else if (Phase == 2)
			{
				// the silt rises: two clouds, the first one a little way from the fish, the second in the other half
				const float MaxTop = FV::TrackH - MmSiltH;
				const float C0 = OffsetWithRoom(LegendRng, FishC, 90.f, 150.f, FV::TrackH);
				FogPos[0] = FMath::Clamp(C0 - MmSiltH * 0.5f, 0.f, MaxTop);
				FogVel[0] = (C0 < FishC ? 1.f : -1.f) * LegendRng.FRandRange(MmSiltSpeedMin, MmSiltSpeedMax);
				const bool bUpper = C0 < FV::TrackH * 0.5f;
				FogPos[1] = bUpper ? LegendRng.FRandRange(FV::TrackH * 0.55f, MaxTop) : LegendRng.FRandRange(0.f, FV::TrackH * 0.45f - MmSiltH);
				FogVel[1] = (LegendRng.FRand() < 0.5f ? -1.f : 1.f) * LegendRng.FRandRange(MmSiltSpeedMin, MmSiltSpeedMax);
				FogOutT = MmSiltReentry;
				SetHazard(0, 0.f);
				RaiseBanner(ELegendBanner::Coils);     // «Муть!»
			}
			else
			{
				FishAlpha = 1.f;                       // the silt settles: the catfish is seen again
				SetHazard(0, LegendRng.FRandRange(MmLastCalmMin, MmLastCalmMax) * 0.5f);
				RaiseBanner(ELegendBanner::LastDash);
			}
		}
	}
	else if (Legend == EFVLegend::FogSerpent && Phase == 0 && Progress >= FsCoilAt)
	{
		Phase = 1;
		PauseT = PhasePause;
		WaveAmp = FsCoilAmp;
		WavePeriod = FsCoilPeriod;
		RaiseBanner(ELegendBanner::Coils);
	}

	if (Progress >= 1.f) { return EResult::Caught; }
	if (Progress <= 0.f)
	{
		bEmptied = true;
		if (!bNoEscape) { return EResult::Escaped; }
	}
	return EResult::Running;
}

void FFVMinigame::StepLegendHazards(float Dt)
{
	using namespace FVLegendFight;
	HazardTimer -= Dt;
	const bool bCanWarn = BannerT <= 0.f;   // a warning never starts while a banner is on screen
	switch (Legend)
	{
	case EFVLegend::Sturgeon:
		// «Против течения»: calm -> 0.9 s of chevrons -> the current drags the bar down while the fish tears upwards
		if (HazardState == 0)
		{
			if (HazardTimer <= 0.f && bCanWarn) { SetHazard(1, StWarn); }
		}
		else if (HazardState == 1)
		{
			if (HazardTimer <= 0.f)
			{
				SetHazard(2, LegendRng.FRandRange(StCurrentMin, StCurrentMax));
				Current = StCurrent;
				FishTarget = FMath::Max(0.f, FishPos - float(LegendRng.FRandRange(StLungeMin, StLungeMax)));
				++TrickCount;
			}
		}
		else if (HazardTimer <= 0.f)
		{
			Current = 0.f;
			SetHazard(0, LegendRng.FRandRange(StCalmMin, StCalmMax));
		}
		break;

	case EFVLegend::CrystalTrout:
		// «Прозрачность»: seen -> 0.8 s flicker (the eye pictogram) -> transparent with glints -> seen again
		if (HazardState == 0)
		{
			FishAlpha = FMath::Min(1.f, FishAlpha + Dt * (1.f - TrClearAlpha) / TrFadeIn);
			if (HazardTimer <= 0.f && bCanWarn) { SetHazard(1, TrFlicker); }
		}
		else if (HazardState == 1)
		{
			FishAlpha = FMath::Lerp(TrFlickerLow, 1.f, 0.5f + 0.5f * FMath::Cos(HazardAge * TrFlickerHz * LegendTwoPi));
			if (HazardTimer <= 0.f)
			{
				SetHazard(2, LegendRng.FRandRange(TrClearMin, TrClearMax));
				MarkTimer = LegendRng.FRandRange(TrGlintMin, TrGlintMax);
				++TrickCount;
			}
		}
		else
		{
			FishAlpha = FMath::Max(TrClearAlpha, FishAlpha - Dt * (1.f - TrClearAlpha) / TrFadeOut);
			MarkTimer -= Dt;
			if (MarkTimer <= 0.f)
			{
				// «Блики»: a star flashes on the real spot — the only thing that gives the fish away
				MarkTimer = LegendRng.FRandRange(TrGlintMin, TrGlintMax);
				Glint = 1.f;
				SeenFishPos = FishPos;
				SeenFishSpeed = LegendVisibleSpeed(*this);
			}
			if (HazardTimer <= 0.f) { SetHazard(0, LegendRng.FRandRange(TrSeenMin, TrSeenMax)); }
		}
		break;

	case EFVLegend::Angler:
		// «Фонарик» (1.2 s, the ring closes) -> «Бросок» -> «Жуёт»; «Обман»: a red flickering fake first (35 %)
		switch (HazardState)
		{
		case 0:
			if (HazardTimer <= 0.f && bCanWarn)
			{
				const bool bFake = !bFeinted && LegendRng.FRand() < AnFakeChance;
				HazardPos = OffsetWithRoom(LegendRng, FishPos, AnLanternMin, AnLanternMax, FishMaxPos);
				bFakeLantern = bFake;
				SetHazard(1, bFake ? AnFake : AnLantern);
				if (bFake)
				{
					bFeinted = true;
					++TrickCount;
					const float Twitch = LegendRng.FRandRange(AnTwitchMin, AnTwitchMax);
					FishTarget = FMath::Clamp(FishPos + (HazardPos < FishPos ? -Twitch : Twitch), 0.f, FishTargetMax);
				}
			}
			break;
		case 1:
			if (HazardTimer > 0.f) { break; }
			if (bFakeLantern)
			{
				// the fake goes out; the real lantern lights elsewhere (the other side of the fish when there is room)
				const float Fake = HazardPos;
				const float Dist = LegendRng.FRandRange(AnLanternMin, AnLanternMax);
				float Real = FishPos + (Fake < FishPos ? Dist : -Dist);
				if (Real < 0.f || Real > FishMaxPos) { Real = OffsetWithRoom(LegendRng, FishPos, AnLanternMin, AnLanternMax, FishMaxPos); }
				HazardPos = FMath::Clamp(Real, 0.f, FishMaxPos);
				bFakeLantern = false;
				SetHazard(1, AnLantern);
			}
			else
			{
				SetHazard(2, AnLungeMaxTime);
				LungeSpeed = FMath::Clamp(FMath::Abs(HazardPos - FishPos) / (AnLunge / FV::SimDt), 1.f, AnLungeMaxSpeed);
				LungeTime = AnLunge;
				++TrickCount;
			}
			break;
		case 2:
			LungeTime = FMath::Max(0.f, LungeTime - Dt);
			if (HazardTimer <= 0.f || FMath::Abs(HazardPos - FishPos) < 0.5f)
			{
				FishPos = HazardPos;
				SetHazard(3, AnChew);
			}
			break;
		default:
			if (HazardTimer <= 0.f)
			{
				FishPos = HazardPos;
				FishSpeed = 0.f;
				FishTarget = -1.f;
				bFeinted = false;              // a new cycle may feint again
				SetHazard(0, LegendRng.FRandRange(AnCalmMin, AnCalmMax));
			}
			break;
		}
		break;

	case EFVLegend::ZharRyba:
	{
		// «Извержение»: calm -> boiling zone (the flame pictogram) -> eruption; the last dash adds «Вспышка»
		if (HazardState == 0)
		{
			// the eruption never overlaps the blink: it waits until the fish is back and the progress runs again
			if (HazardTimer <= 0.f && bCanWarn && BlinkState == 0 && ProgressFreezeT <= 0.f)
			{
				const float H = Phase == 0 ? ZhZone0 : ZhZone1;
				const float C = LegendRng.FRand() < ZhOnBarChance ? BarPos + BarHeight * 0.5f : FishPos + FishIconH * 0.5f;
				HazardH = H;
				HazardPos = FMath::Clamp(C - H * 0.5f, 0.f, FV::TrackH - H);
				SetHazard(1, Phase == 0 ? ZhBoil0 : ZhBoil1);
			}
		}
		else if (HazardState == 1)
		{
			if (HazardTimer <= 0.f)
			{
				SetHazard(2, ZhErupt);
				++TrickCount;
				if (Phase == 0) { ++Phase0Eruptions; }
			}
		}
		else if (HazardTimer <= 0.f)
		{
			const float CalmMin = Phase == 0 ? ZhCalm0Min : (Phase == 1 ? ZhCalm1Min : ZhCalm2Min);
			const float CalmMax = Phase == 0 ? ZhCalm0Max : (Phase == 1 ? ZhCalm1Max : ZhCalm2Max);
			SetHazard(0, LegendRng.FRandRange(CalmMin, CalmMax));
			if (Phase >= 2) { BlinkTimer = FMath::Max(BlinkTimer, ZhBlinkAfterErupt); }   // a waiting blink comes 0.5 s later
		}
		if (Phase >= 2)
		{
			if (BlinkState == 0)
			{
				if (HazardState == 0)
				{
					BlinkTimer -= Dt;          // the blink clock only runs between eruptions
					if (BlinkTimer <= 0.f)
					{
						BlinkState = 1;
						BlinkTimer = ZhBlink;
						BlinkTarget = OffsetWithRoom(LegendRng, FishPos, ZhBlinkMinD, ZhBlinkMaxD, FishMaxPos);
						FishAlpha = 0.f;
						++TrickCount;
					}
				}
			}
			else
			{
				BlinkTimer -= Dt;
				if (BlinkTimer <= 0.f)
				{
					// back at the spark with no speed; the progress waits 0.3 s more
					BlinkState = 0;
					FishPos = BlinkTarget;
					FishSpeed = 0.f;
					FishTarget = -1.f;
					FishAlpha = 1.f;
					ProgressFreezeT = ZhBlinkFreezeAfter;
					BlinkTimer = LegendRng.FRandRange(ZhBlinkMin, ZhBlinkMax);
				}
			}
		}
		break;
	}

	case EFVLegend::MillMaster:
		if (Phase == 0)
		{
			// «Омут»: calm (the whirl still pulls) -> 0.7 s warning (the whirl pictogram) -> a surge drags harder
			if (HazardState == 0)
			{
				if (HazardTimer <= 0.f && bCanWarn) { SetHazard(1, MmWhirlWarn); }
			}
			else if (HazardState == 1)
			{
				if (HazardTimer <= 0.f)
				{
					SetHazard(2, MmSurgeT);
					Current = MmPull + MmSurge;
					++TrickCount;
				}
			}
			else if (HazardTimer <= 0.f)
			{
				Current = MmPull;
				SetHazard(0, LegendRng.FRandRange(MmCalm0Min, MmCalm0Max));
			}
		}
		else if (Phase == 2)
		{
			// «Муть»: the clouds drift and bounce; inside one the catfish only shows in the swirls of silt (never gone)
			const float MaxTop = FV::TrackH - MmSiltH;
			for (int32 k = 0; k < 2; ++k)
			{
				FogPos[k] += FogVel[k];
				if (FogPos[k] < 0.f) { FogPos[k] = -FogPos[k]; FogVel[k] = FMath::Abs(FogVel[k]); }
				else if (FogPos[k] > MaxTop) { FogPos[k] = 2.f * MaxTop - FogPos[k]; FogVel[k] = -FMath::Abs(FogVel[k]); }
			}
			const float FishC = FishPos + FishIconH * 0.5f;
			int32 In = -1;
			for (int32 k = 0; k < 2 && In < 0; ++k)
			{
				if (FishC >= FogPos[k] && FishC <= FogPos[k] + MmSiltH) { In = k; }
			}
			const float Fade = Dt / MmSiltFade;
			if (In >= 0)
			{
				if (HazardState != 2)
				{
					if (FogOutT >= MmSiltReentry) { ++TrickCount; }
					SetHazard(2, 0.f);
				}
				FogOutT = 0.f;
				HazardPos = FogPos[In] + MmSiltH * 0.5f;
				const float Swirl = FMath::Lerp(MmSiltLo, MmSiltHi, 0.5f + 0.5f * FMath::Sin(HazardAge * MmSiltHz * LegendTwoPi));
				FishAlpha = FishAlpha > Swirl ? FMath::Max(Swirl, FishAlpha - Fade) : Swirl;
			}
			else
			{
				if (HazardState != 0) { SetHazard(0, 0.f); }
				FogOutT += Dt;
				FishAlpha = FMath::Min(1.f, FishAlpha + Fade);
			}
		}
		else
		{
			// «Жернова» / «Последний рывок»: a dull knock (the pictogram at the fish), then a heavy jerk; the rhythm ~2 s
			const bool bLast = Phase >= 3;
			switch (HazardState)
			{
			case 0:
				if (HazardTimer <= 0.f && bCanWarn) { SetHazard(1, bLast ? MmLastThud : MmThud); }
				break;
			case 1:
				if (HazardTimer <= 0.f)
				{
					if (bLast)
					{
						// towards the surface; only when the fish is already up there it jerks back down
						const float Dist = LegendRng.FRandRange(MmLastJerkMin, MmLastJerkMax);
						HazardPos = FishPos - Dist >= 0.f ? FishPos - Dist : FMath::Clamp(FishPos + Dist * 0.6f, 0.f, FishMaxPos);
					}
					else
					{
						HazardPos = OffsetWithRoom(LegendRng, FishPos, MmJerkMin, MmJerkMax, FishMaxPos);
					}
					LungeSpeed = FMath::Clamp(FMath::Abs(HazardPos - FishPos) / (MmJerk / FV::SimDt), 1.f, MmJerkMaxSpeed);
					SetHazard(2, MmJerkMaxTime);
					++TrickCount;
				}
				break;
			default:
				if (HazardTimer <= 0.f || FMath::Abs(HazardPos - FishPos) < 0.5f)
				{
					FishSpeed = 0.f;
					FishTarget = -1.f;
					SetHazard(0, bLast ? LegendRng.FRandRange(MmLastCalmMin, MmLastCalmMax) : LegendRng.FRandRange(MmStoneCalmMin, MmStoneCalmMax));
				}
				break;
			}
		}
		break;

	default:
		break;                             // the fog serpent's states are set by StepLegendFog
	}
}

void FFVMinigame::StepLegendFish(float Dt)
{
	using namespace FVLegendFight;
	// ---- tricks that take the fish over: the classic motion does not run
	if (Legend == EFVLegend::Angler && HazardState == 2)
	{
		// «Бросок»: straight at the lantern, dist / 0.4 s (at most 10 per tick)
		const float D = HazardPos - FishPos;
		FishSpeed = D >= 0.f ? LungeSpeed : -LungeSpeed;
		FishPos = FMath::Clamp(FishPos + FMath::Sign(D) * FMath::Min(FMath::Abs(D), LungeSpeed), 0.f, FishMaxPos);
		FishTarget = -1.f;
		return;
	}
	if (Legend == EFVLegend::Angler && HazardState == 3)
	{
		// «Жуёт»: it stands at the lantern and trembles +-10 at 6 Hz
		FishSpeed = 0.f;
		FishTarget = -1.f;
		FishPos = FMath::Clamp(HazardPos + AnChewAmp * FMath::Sin(HazardAge * AnChewHz * LegendTwoPi), 0.f, FishMaxPos);
		return;
	}
	if (BlinkState == 1)
	{
		FishSpeed = 0.f;                   // gone: it reappears at BlinkTarget
		return;
	}
	if (Legend == EFVLegend::MillMaster && (Phase == 1 || Phase >= 3) && HazardState == 2)
	{
		// «Жернова»: the heavy jerk — straight at its spot in 0.35 s (at most 12 per tick)
		const float D = HazardPos - FishPos;
		FishSpeed = D >= 0.f ? LungeSpeed : -LungeSpeed;
		FishPos = FMath::Clamp(FishPos + FMath::Sign(D) * FMath::Min(FMath::Abs(D), LungeSpeed), 0.f, FishMaxPos);
		FishTarget = -1.f;
		return;
	}

	// ---- the Stardew motion of Step, with the legend twists
	const bool bSmooth = Motion == EFVMotion::Smooth;
	const bool bClear = Legend == EFVLegend::CrystalTrout && HazardState == 2;   // transparent: smooth targets, no darts
	const bool bSmoothNow = bSmooth || bClear;
	const bool bUpwards = Legend == EFVLegend::Sturgeon && HazardState == 2;     // against the current: upper half only
	const float UpperMax = FV::TrackH * 0.5f - FishIconH * 0.5f;
	if (Rng.FRand() < Difficulty * (bSmooth ? 20.f : 1.f) / 4000.f && (!bSmoothNow || FishTarget == -1.f))
	{
		const float SpaceBelow = FishTargetMax - FishPos;
		const float SpaceAbove = FishPos;
		const float Percent = FMath::Min(99.f, Difficulty + float(NextInt(Rng, 10, 45))) / 100.f;
		FishTarget = FishPos + float(NextInt(Rng, int32(FMath::Min(-SpaceAbove, SpaceBelow)), int32(SpaceBelow))) * Percent;
		if (bUpwards && FishTarget > UpperMax) { FishTarget = LegendRng.FRandRange(0.f, UpperMax); }
	}
	// the Master's last dash: the bottom-dweller (Sinker) tears towards the surface — it floats up instead
	const bool bRise = Legend == EFVLegend::MillMaster && Phase >= 3;
	if (Motion == EFVMotion::Floater || bRise) { FloatSink = FMath::Max(FloatSink - 0.01f, -1.5f); }
	else if (Motion == EFVMotion::Sinker) { FloatSink = FMath::Min(FloatSink + 0.01f, 1.5f); }

	if (FMath::Abs(FishPos - FishTarget) > 3.f && FishTarget != -1.f)
	{
		FishAccel = (FishTarget - FishPos) / (float(NextInt(Rng, 10, 30)) + (100.f - FMath::Min(100.f, Difficulty)));
		FishSpeed += (FishAccel - FishSpeed) / 5.f;
	}
	else if (!bSmoothNow && Rng.FRand() < Difficulty / 2000.f)
	{
		FishTarget = FishPos + (Rng.FRand() < 0.5f ? float(NextInt(Rng, -100, -51)) : float(NextInt(Rng, 50, 101)));
	}
	else
	{
		FishTarget = -1.f;
	}
	const float DartMult = (Legend == EFVLegend::ZharRyba && Phase >= 1) ? ZhDartMult : 1.f;
	if (Motion == EFVMotion::Dart && !bClear && Rng.FRand() < Difficulty * DartMult / 1000.f)
	{
		const int32 D2 = int32(Difficulty) * 2;
		FishTarget = FishPos + (Rng.FRand() < 0.5f ? float(NextInt(Rng, -100 - D2, -51)) : float(NextInt(Rng, 50, 101 + D2)));
	}
	FishTarget = FMath::Clamp(FishTarget, -1.f, FishTargetMax);
	FishPos = FMath::Clamp(FishPos + FishSpeed + FloatSink, 0.f, FishMaxPos);

	// ---- «Петли»: the serpent sways on top of its motion
	if (WaveAmp > 0.f)
	{
		const float Old = FMath::Sin(WavePhase);
		WavePhase = FMath::Fmod(WavePhase + LegendTwoPi * Dt / FMath::Max(0.1f, WavePeriod), LegendTwoPi);
		FishPos = FMath::Clamp(FishPos + WaveAmp * (FMath::Sin(WavePhase) - Old), 0.f, FishMaxPos);
	}
}

void FFVMinigame::StepLegendFog(float Dt)
{
	using namespace FVLegendFight;
	// «Туман»: two bands drift and bounce; the fish fades inside; mint bubbles give its real spot away
	const float Mult = Phase >= 1 ? FsCoilFog : 1.f;
	const float MaxTop = FV::TrackH - FsFogH;
	const float FishC = FishPos + FishIconH * 0.5f;
	const bool bOpening = FogOpenT > 0.f;
	if (bOpening)
	{
		// the opening: band 0 homes in on the fish to reach it right when the fog starts hiding it
		FogOpenT = FMath::Max(0.f, FogOpenT - Dt);
		const float TicksLeft = FMath::Max(1.f, FogOpenT / FV::SimDt);
		FogVel[0] = FMath::Clamp((FishC - (FogPos[0] + FsFogH * 0.5f)) / TicksLeft, -FsHoming, FsHoming);
	}
	else
	{
		// then both bands drift at their own pace again
		for (float& V : FogVel) { V = (V < 0.f ? -1.f : 1.f) * FMath::Clamp(FMath::Abs(V), FsFogSpeedMin, FsFogSpeedMax); }
	}
	for (int32 k = 0; k < 2; ++k)
	{
		FogPos[k] += FogVel[k] * Mult;
		if (FogPos[k] < 0.f) { FogPos[k] = -FogPos[k]; FogVel[k] = FMath::Abs(FogVel[k]); }
		else if (FogPos[k] > MaxTop) { FogPos[k] = 2.f * MaxTop - FogPos[k]; FogVel[k] = -FMath::Abs(FogVel[k]); }
	}
	int32 In = -1;
	for (int32 k = 0; k < 2 && In < 0 && !bOpening; ++k)
	{
		if (FishC >= FogPos[k] && FishC <= FogPos[k] + FsFogH) { In = k; }
	}
	const float Fade = (1.f - FsFogAlpha) / FsFogFade * Dt;
	if (bOpening && FogOpenT <= FsFogAhead)
	{
		// the last 0.8 s of the opening: band 0 is about to reach the fish — the cloud pictogram lights now
		if (HazardState != 1) { SetHazard(1, 0.f); }
		HazardPos = FogPos[0] + FsFogH * 0.5f;
		FishAlpha = FMath::Min(1.f, FishAlpha + Fade);
		FogOutT += Dt;
		return;
	}
	if (In >= 0 && HazardState != 2)
	{
		// every disappearance is announced: in the fog the fish only fades once the cloud pictogram has shown 0.8 s
		// (the straight forecast below misses when the fish turns)
		if (HazardState != 1) { SetHazard(1, 0.f); }
		HazardPos = FogPos[In] + FsFogH * 0.5f;
		if (HazardAge < FsFogAhead)
		{
			FishAlpha = FMath::Min(1.f, FishAlpha + Fade);
			FogOutT += Dt;
			return;
		}
	}
	FishAlpha = In >= 0 ? FMath::Max(FsFogAlpha, FishAlpha - Fade) : FMath::Min(1.f, FishAlpha + Fade);
	if (In >= 0)
	{
		if (HazardState != 2)
		{
			if (FogOutT >= FsReentry) { ++TrickCount; }
			SetHazard(2, 0.f);
			MarkTimer = FsBubbleEvery * 0.5f;
		}
		HazardPos = FogPos[In] + FsFogH * 0.5f;
		FogOutT = 0.f;
		MarkTimer -= Dt;
		if (MarkTimer <= 0.f)
		{
			MarkTimer += FsBubbleEvery;
			BubbleY[BubbleNext] = FishPos;
			BubbleAge[BubbleNext] = 0.f;
			BubbleNext = (BubbleNext + 1) % 4;
			SeenFishPos = FishPos;
			SeenFishSpeed = LegendVisibleSpeed(*this);
		}
		return;
	}
	FogOutT += Dt;
	// out of the fog: will the fish be inside a band within 0.8 s? (straight forecast of both) -> the cloud pictogram
	const float V = LegendVisibleSpeed(*this) / FV::SimDt;
	int32 Soon = -1;
	for (int32 Si = 1; Si <= 8 && Soon < 0; ++Si)
	{
		const float Ts = FsFogAhead * float(Si) / 8.f;
		const float C = FishC + V * Ts;
		for (int32 k = 0; k < 2 && Soon < 0; ++k)
		{
			const float Top = FogPos[k] + FogVel[k] * Mult / FV::SimDt * Ts;
			if (C >= Top && C <= Top + FsFogH) { Soon = k; }
		}
	}
	if (Soon >= 0)
	{
		if (HazardState != 1) { SetHazard(1, 0.f); }
		HazardPos = FogPos[Soon] + FsFogH * 0.5f;
	}
	else if (HazardState != 0)
	{
		SetHazard(0, 0.f);
	}
}

bool FFVMinigame::LegendBotHold(float Look) const
{
	using namespace FVLegendFight;
	// the fish as the player sees it: hidden -> the last glint / bubble and the last known speed
	float Top = FishPos;
	float V = LegendVisibleSpeed(*this);
	if (FishAlpha < 0.5f)
	{
		Top = SeenFishPos;
		V = SeenFishSpeed;
	}
	float Aim = Top + FishIconH * 0.5f + V * Look * 0.6f;
	if (Legend == EFVLegend::Angler && ((HazardState == 1 && !bFakeLantern && HazardAge >= AnLantern * 0.6f) || HazardState >= 2))
	{
		Aim = HazardPos + FishIconH * 0.5f;    // the real ring is closing: be at the lantern when the fish arrives
	}
	if (Legend == EFVLegend::ZharRyba && BlinkState == 1)
	{
		Aim = BlinkTarget + FishIconH * 0.5f;  // the spark shows where it comes back
	}
	float BarC = BarPos + BarHeight * 0.5f + BarSpeed * Look;
	if (Current > 0.f) { BarC += Current * Look * Look; }   // the current keeps dragging the bar down
	if (Legend == EFVLegend::ZharRyba && HazardState >= 1)
	{
		// keep the bar out of the boiling / erupting zone: just above or below it, on the fish's side when possible
		const float H = BarHeight, Z0 = HazardPos, Z1 = HazardPos + HazardH;
		const float Allowed = ZhBurnOverlap * H * 0.5f;
		const float Overlap = FMath::Min(Aim + H * 0.5f, Z1) - FMath::Max(Aim - H * 0.5f, Z0);
		if (Overlap > Allowed)
		{
			const float Up = Z0 + Allowed - H * 0.5f;
			const float Down = Z1 - Allowed + H * 0.5f;
			const bool bUpOk = Up - H * 0.5f >= 0.f, bDownOk = Down + H * 0.5f <= FV::TrackH;
			if (bUpOk && bDownOk) { Aim = FMath::Abs(Up - Aim) <= FMath::Abs(Down - Aim) ? Up : Down; }
			else if (bUpOk) { Aim = Up; }
			else if (bDownOk) { Aim = Down; }
		}
	}
	return BarC > Aim;
}

float FVSimulateCatchRate(float Difficulty, EFVMotion Motion, float BarHeight, float ReelMult, float DrainMult, int32 Trials, int32 Seed, float BotSkill,
	EFVLegend Legend)
{
	if (Trials <= 0) { return 0.f; }
	BotSkill = FMath::Clamp(BotSkill, 0.f, 1.f);
	const int32 Delay = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(16.f, 4.f, BotSkill)), 1, 60);   // reaction delay, ticks
	const float Noise = (1.f - BotSkill) * 0.18f;                                                     // chance of a wrong input per tick
	constexpr int32 MaxTicks = 60 * 180;
	int32 Wins = 0;
	TArray<bool> Queue;
	for (int32 t = 0; t < Trials; ++t)
	{
		FFVMinigame M;
		M.Start(Difficulty, Motion, BarHeight, ReelMult, DrainMult, false, int32((uint32(Seed) + uint32(t) * 7919u) & 0x7FFFFFFFu), Legend);
		FRandomStream BotRng(int32((uint32(Seed) * 2654435761u + uint32(t) * 40503u) & 0x7FFFFFFFu));
		Queue.Init(false, Delay);
		int32 Head = 0;
		for (int32 Tick = 0; Tick < MaxTicks; ++Tick)
		{
			bool bWant = M.BotShouldHold(BotSkill);
			if (BotRng.FRand() < Noise) { bWant = !bWant; }
			const bool bApplied = Queue[Head];
			Queue[Head] = bWant;
			Head = (Head + 1) % Delay;
			const FFVMinigame::EResult R = M.Step(bApplied);
			if (R == FFVMinigame::EResult::Caught) { ++Wins; break; }
			if (R == FFVMinigame::EResult::Escaped) { break; }
		}
	}
	return float(Wins) / float(Trials);
}

FFVLegendSimResult FVSimulateLegend(const FFVLegendSimSetup& Setup, const FFVBotProfile& Profile, int32 Trials, int32 Seed)
{
	FFVLegendSimResult Out;
	if (Trials <= 0) { return Out; }
	// Update 5 (§5): a simulated phone player. Every press lands DelayTicks later (the same queue delays the reaction to
	// the warnings). Like a real player he knows the presses already on their way: the bar is predicted through them
	// before he decides (a delayed bang-bang control without it only swings — even the classic Tsar-carp would be
	// ~0 % at 18 ticks). He does not know his slips (Noise: a wrong input for one tick) until he sees them, and he
	// sees the fish only as FFVMinigame::BotShouldHoldLook allows. The hook intro freeze is FVFishing's (no Step).
	const int32 Delay = FMath::Clamp(Profile.DelayTicks, 1, 60);
	const float Noise = FMath::Clamp(Profile.Noise, 0.f, 1.f);
	constexpr int32 MaxTicks = 60 * 180;
	int32 Wins = 0, WithEruption = 0;
	TArray<float> Seconds, Tricks;
	Seconds.Reserve(Trials);
	Tricks.Reserve(Trials);
	TArray<bool> Applied, Intended;        // ring buffers: what reaches the reel / what the player meant
	for (int32 t = 0; t < Trials; ++t)
	{
		FFVMinigame M;
		M.Start(Setup.Difficulty, Setup.Motion, Setup.BarHeight, Setup.ReelMult, Setup.DrainMult, false,
			int32((uint32(Seed) + uint32(t) * 7919u) & 0x7FFFFFFFu), Setup.Legend);
		FRandomStream BotRng(int32((uint32(Seed) * 2654435761u + uint32(t) * 40503u) & 0x7FFFFFFFu));
		Applied.Init(false, Delay);
		Intended.Init(false, Delay);
		int32 Head = 0;
		for (int32 Tick = 0; Tick < MaxTicks; ++Tick)
		{
			// the bar when this decision lands: the own presses in flight (gravity / hold / bounces; no outside forces)
			FFVMinigame Pred = M;
			for (int32 k = 0; k < Delay; ++k)
			{
				const bool bQ = Intended[(Head + k) % Delay];
				const float MaxBar = FV::TrackH - Pred.BarHeight;
				if (bQ && (Pred.BarPos <= 0.001f || Pred.BarPos >= MaxBar - 0.001f)) { Pred.BarSpeed = 0.f; }
				Pred.BarSpeed += bQ ? -0.25f : 0.25f;
				Pred.BarPos += Pred.BarSpeed;
				if (Pred.BarPos + Pred.BarHeight > FV::TrackH) { Pred.BarPos = MaxBar; Pred.BarSpeed = -Pred.BarSpeed * 2.f / 3.f; }
				else if (Pred.BarPos < 0.f) { Pred.BarPos = 0.f; Pred.BarSpeed = -Pred.BarSpeed * 2.f / 3.f; }
			}
			const bool bWant = Pred.BotShouldHoldLook(Profile.LookTicks);
			const bool bSlip = BotRng.FRand() < Noise;
			const bool bApplied = Applied[Head];
			Applied[Head] = bSlip ? !bWant : bWant;
			Intended[Head] = bWant;
			Head = (Head + 1) % Delay;
			const FFVMinigame::EResult R = M.Step(bApplied);
			if (R == FFVMinigame::EResult::Caught) { ++Wins; break; }
			if (R == FFVMinigame::EResult::Escaped) { break; }
		}
		Seconds.Add(M.Elapsed);
		Tricks.Add(float(M.TrickCount));
		if (M.Phase0Eruptions > 0) { ++WithEruption; }
	}
	auto Median = [](TArray<float>& V) { V.Sort(); return V.Num() > 0 ? V[V.Num() / 2] : 0.f; };
	Out.Rate = float(Wins) / float(Trials);
	Out.MedianSec = Median(Seconds);
	Out.MedianTricks = Median(Tricks);
	Out.Phase0EruptFrac = float(WithEruption) / float(Trials);
	return Out;
}

// =============================================================================================
// FVFishing implementation state
// =============================================================================================
struct FVFishing::FImpl
{
	FVFishing& O;
	FRandomStream Rng;
	float StateTime = 0.f;
	float Time = 0.f;

	// charging
	float Power = 0.f;
	float ChargeTime = 0.f;

	// bobber
	FVector2D Bobber = FVector2D::ZeroVector;
	FVector2D CastFrom = FVector2D::ZeroVector;
	FVector2D CastTo = FVector2D::ZeroVector;
	float CastDuration = 0.6f;
	float CastPower = 0.f;
	FVector2D RetrieveFrom = FVector2D::ZeroVector;
	float RetrieveDuration = 0.4f;
	bool bBobberVisible = false;

	// waiting / bite
	float BiteDelay = 5.f;
	float NextNibble = 2.f;
	float NibbleTime = 0.f;
	float NextRipple = 1.f;
	float ReactWindow = 1.f;
	const FFVFishDef* Fish = nullptr;
	const FFVJunkDef* Junk = nullptr;
	const FFVBaitDef* BaitUsed = nullptr;
	FName ForcedFish;                  // update 5: DebugForceBite(FishId) — this fish takes the next bite (autotest)
	FName TrialFish;                   // 0.5: StartLegendTrial — this legend takes the next bite, nothing is recorded
	bool bTrial = false;               // this fight is a legend trial (TrialFish took the bite)
	TMap<FName, FIntPoint> TrialScore; // session: X = trial fights started, Y = legends landed
	bool bForcedGates = false;         // ... and FVDB::LegendGate still applies to it
	// update 5: legends at the bite
	bool bPendingSnap = false;         // LegendGate said Snap: the leader snaps at the end of the hook intro
	float GlowTime = 0.f;              // the Angler's «Огонёк»: presses ignored, no «!» until StateTime reaches it
	bool bGlowDone = true;             // the «!» and the Bite sound came after the glow
	TSet<FName> GatedToastShown;       // GatedToast once per visit and legend (cleared by Reset: every location load)

	// minigame
	float Accum = 0.f;
	float ReelAngle = 0.f;
	float TreasurePop = 0.f;
	bool bTreasureWasCaught = false;
	float MiniRipple = 0.f;
	// update 5: the legend hook intro (0.7 s freeze, no Step), banners and warning feedback
	float IntroT = 0.f;
	FString BannerText;
	float BannerAge = 100.f;           // seconds since the banner appeared (shown for FVLegendFight::BannerLife)
	bool bBannerShine = false;         // «Легенда на крючке!»: the shine runs over the gold bar while it is shown
	int32 SeenBannerSerial = 0;
	uint8 PrevHazard = 0;
	uint8 PrevBlink = 0;
	bool bPrevFake = false;
	float LastWarnTime = -10.f;
	float PinnedProgress = -1.f;       // DebugPinProgress (autotest): >= 0 keeps the meter there
	// update 5 (§11): the «Фонарь удильщика» bobber shines through the night — DrawWorld (under the night tint) leaves
	// the lamp's spot here, DrawUI (above the lighting) adds the glow
	FVector2D LanternPos = FVector2D::ZeroVector;
	float LanternGlow = 0.f;           // 0 = none this frame

	// showcase
	FFVCatchResult Result;
	bool bHasResult = false;
	float PerfectFlash = 0.f;

	// autoplay / misc
	bool bBotPrevHold = false;
	float LastAwayToast = -10.f;

	// update 4b: this cast is at the practice pond of the home yard (set in StartCharging)
	bool bPractice = false;
	// 0.4.1: the practice fight cannot be lost — an empty meter shows the «give up» [X] next to the panel
	FFVRect GiveUpRect;                // where DrawUI put the [X] this frame (W = 0: not shown)
	bool bGiveUpClicked = false;       // DrawUI -> Tick: the player gave up
	bool bEmptyHintShown = false;      // «Учебная рыбка не уплывёт…» once per fight

	// effective rod stats (rod + tackle + food), read once per cast in StartCharging
	FFVRodStats Cur;

	// canvas tracking: the scene relays out when the phone rotates / the window changes its aspect ratio
	int32 CanvasRev = -1;
	float LastShore = 0.f, LastBottom = 0.f;
	FVector2D LastPlayer = FVector2D::ZeroVector;

	// update 6 [FISHING-EXPERT] (§10.4, §10.9–10.12): the expert fight and the «wrong rod» fights run inside Minigame
	enum class EExMode : uint8
	{
		None,              // the classic minigame (everything as before)
		Fight,             // the expert fight on one long vertical track (§19, Р60: expert fish, expert legend or — Р18 — a classic fish on an expert rod)
		LegendOnExpert,    // Р28: a classic legend on an expert rod — banner, then the slip after 2–3 s
		ExpertOnClassic    // Р25: an expert fish on a classic rod — the bar cannot hold it, slip after 3–4 s
	};
	EExMode ExMode = EExMode::None;
	FFVExpertFight Ex;
	FName ExRodId;
	EFVExpertTheme ExTheme = EFVExpertTheme::Classic;
	float FakeLen = 3.f;               // LegendOnExpert / ExpertOnClassic: seconds until the slip
	float FakeStartProgress = 0.3f;
	bool bExPrevDown = false;
	bool bExPendingPress = false;      // a press not consumed by a fight step yet (tap inside one frame)
	bool bExPendingRelease = false;
	FVector2D ExStick = FVector2D::ZeroVector;   // Ползунок, keyboard: Stick.Y (W/S, arrows)
	float ExTargetY = -1.f;                      // Ползунок: the track Y under the finger / mouse (-1 = none)
	int32 ExSeenSignal = 0;
	int32 ExLastMove = -1;
	bool bExPrevMagnet = false;
	float ExCreakT = 0.f;
	float ExPinned = -1.f;             // DebugPinExpertProgress
	bool bExTutorialPending = false;   // StartExpertTutorial: the next expert bite is the training fish
	bool bExGhostHand = false;         // first fight of this rod type: the «ghost hand» (3 s)
	float ExHandT = 0.f;
	FFVRect ExField;                   // last drawn field (zero when none)
	FFVRect ExGiveUp;                  // training fight [X] (zero when hidden)
	bool bExGiveUpClicked = false;
	bool bExTreasureWasCaught = false;

	explicit FImpl(FVFishing& InOwner) : O(InOwner) { Rng.GenerateNewSeed(); }

	FVScene* Scene() const { return O.Game ? &O.Game->GetScene() : nullptr; }
	UFVSaveGame* Profile() const { return O.Game ? O.Game->GetProfile() : nullptr; }
	void SetState(EFVFishState S) { O.State = S; StateTime = 0.f; }

	/** Keeps the bobber on the same spot of the water (same depth fraction, same offset from the player) after
	    the scene was rebuilt for another canvas. Must run after the scene synced (Scene::Tick / DrawScene). */
	void SyncCanvas()
	{
		FVScene* S = Scene();
		if (!S) { return; }
		if (CanvasRev != FV::CanvasRevision)
		{
			const bool bPracticeRemap = CanvasRev >= 0 && bPractice && O.State != EFVFishState::Idle && O.State != EFVFishState::Charging;
			const bool bRemap = CanvasRev >= 0 && !bPractice && O.State != EFVFishState::Idle && LastBottom > LastShore + 1.f;
			CanvasRev = FV::CanvasRevision;
			if (bPracticeRemap)
			{
				// the practice pond was rebuilt for the new canvas (the scene keeps the player at the pond): the bobber
				// goes to the same kind of spot of the new pond
				Bobber = S->GetCastTarget(FMath::Max(0.3f, CastPower), Cur.CastRange);
				CastTo = Bobber;
				RetrieveFrom = Bobber;
				CastFrom = S->GetRodTip();
			}
			if (bRemap)
			{
				const FVector2D Player = S->GetPlayerPos();
				const float NewShore = S->GetShoreY(), NewBottom = S->GetWaterBottomY();
				// hotfix 0.61: a classic place clamps to its world bounds (the canvas; the wide pond: its scroll range,
				// X may be negative); big worlds keep the old clamp
				const FFVRect WB = S->IsBigWorld() ? FFVRect(0.f, 0.f, FV::VW, FV::VH) : S->GetWorldBounds();
				auto Map = [&](const FVector2D& P)
				{
					const float X = FMath::Clamp(float(P.X + (Player.X - LastPlayer.X)), WB.X + 6.f, WB.Right() - 6.f);
					if (P.Y >= LastShore)
					{
						const float Fr = (float(P.Y) - LastShore) / FMath::Max(1.f, LastBottom - LastShore);
						return FVector2D(X, NewShore + Fr * (NewBottom - NewShore));
					}
					return FVector2D(X, float(P.Y + (Player.Y - LastPlayer.Y)));
				};
				Bobber = Map(Bobber);
				CastTo = Map(CastTo);
				RetrieveFrom = Map(RetrieveFrom);
				CastFrom = S->GetRodTip();
				const bool bInWater = O.State == EFVFishState::Waiting || O.State == EFVFishState::Bite
					|| O.State == EFVFishState::Minigame || O.State == EFVFishState::Escaped;
				if (bInWater && !S->IsWater(Bobber))
				{
					Bobber = S->GetCastTarget(FMath::Max(0.3f, CastPower), Cur.CastRange);
				}
			}
		}
		LastShore = S->GetShoreY();
		LastBottom = S->GetWaterBottomY();
		LastPlayer = S->GetPlayerPos();
	}

	FVector2D LineEnd() const
	{
		if (O.State == EFVFishState::Minigame && (ExMode == EExMode::Fight || ExMode == EExMode::LegendOnExpert))
		{
			const float Ex01 = Ex.FishPos / FVExpert::TrackH - 0.5f;
			return Bobber + FVector2D(Ex01 * 36.f, 2.f + FMath::Sin(Time * 2.3f) * 1.5f);
		}
		if (O.State == EFVFishState::Minigame)
		{
			const float Fx = (O.Mini.FishPos / FishMaxPos - 0.5f) * 36.f;
			return Bobber + FVector2D(Fx, 2.f + FMath::Sin(Time * 2.3f) * 1.5f);
		}
		return Bobber;
	}

	// ------------------------------------------------------------------ transitions
	void ToIdle()
	{
		Reeling(false);
		bBobberVisible = false;
		if (FVScene* S = Scene())
		{
			S->SetHeldSprite(nullptr);
			S->SetPlayerAnim(EFVCharAnim::Idle, true);
		}
		SetState(EFVFishState::Idle);
	}

	/** Equipped rod + installed tackle + active food buff: one snapshot per cast (a buff expiring mid-cast
	    never changes the running minigame). */
	void ReadStats()
	{
		Cur = O.Game ? O.Game->GetEffectiveRodStats() : FFVRodStats();
	}

	void StartCharging(bool bPracticeCast = false)
	{
		FVScene* S = Scene();
		if (!S) { return; }
		bPractice = bPracticeCast;
		ReadStats();
		Power = 0.f;
		ChargeTime = 0.f;
		S->SetFacing(EFVFacing::Down);
		S->SetPlayerAnim(EFVCharAnim::CastWindup, true);
		SetState(EFVFishState::Charging);
	}

	void DoCast()
	{
		FVScene* S = Scene();
		UFVSaveGame* P = Profile();
		if (!S || !P) { ToIdle(); return; }
		S->SetPlayerAnim(EFVCharAnim::CastRelease, true);
		CastPower = Power;
		CastFrom = S->GetRodTip();
		CastTo = S->GetCastTarget(Power, Cur.CastRange);
		CastDuration = 0.45f + 0.35f * Power;
		Bobber = CastFrom;
		bBobberVisible = true;
		Sfx(EFVSfx::Cast, 0.9f, 0.95f + 0.1f * Power);
		SetState(EFVFishState::Casting);
	}

	void StartRetrieve()
	{
		FVScene* S = Scene();
		Reeling(false);
		RetrieveFrom = LineEnd();
		Bobber = RetrieveFrom;
		bBobberVisible = true;
		if (S)
		{
			const float Dist = float(FVector2D::Distance(RetrieveFrom, S->GetRodTip()));
			RetrieveDuration = FMath::Clamp(Dist / 380.f, 0.25f, 0.6f);
			if (S->IsWater(RetrieveFrom)) { S->SpawnRipple(RetrieveFrom, 0.6f); }
			S->SetPlayerAnim(EFVCharAnim::Reel, true);
		}
		SetState(EFVFishState::Retrieving);
	}

	void Land()
	{
		FVScene* S = Scene();
		UFVSaveGame* P = Profile();
		Bobber = CastTo;
		if (!S || !P || !S->IsWater(CastTo)) { StartRetrieve(); return; }
		Sfx(EFVSfx::Splash, 0.8f);
		S->SpawnSplash(CastTo, 0.8f);
		S->SetPlayerAnim(EFVCharAnim::FishWait, true);
		if (bPractice)
		{
			BiteDelay = Rng.FRandRange(FVFishingPracticeImpl::BiteMin, FVFishingPracticeImpl::BiteMax);   // no bait needed
		}
		else
		{
			const FFVBaitDef* Bait = P->GetBait();
			BiteDelay = FMath::Clamp(FVDB::RollBiteDelay(Rng) * FMath::Max(0.05f, Cur.BiteSpeed) * (Bait ? Bait->BiteSpeed : 1.f), 0.8f, 40.f);
		}
		NextNibble = Rng.FRandRange(1.f, 2.5f);
		NibbleTime = 0.f;
		NextRipple = 1.5f;
		Fish = nullptr;
		Junk = nullptr;
		SetState(EFVFishState::Waiting);
	}

	void Bite()
	{
		FVScene* S = Scene();
		UFVSaveGame* P = Profile();
		if (!S || !P || !O.Game) { StartRetrieve(); return; }
		bPendingSnap = false;
		GlowTime = 0.f;
		bGlowDone = true;
		if (bPractice)
		{
			// the «учебная рыбка»: no catch table, no bait used, a generous reaction window
			Fish = nullptr;
			Junk = nullptr;
			BaitUsed = nullptr;
			Sfx(EFVSfx::Bite);
			S->SpawnSplash(Bobber, 0.5f);
			ReactWindow = FVFishingPracticeImpl::ReactWindow;
			SetState(EFVFishState::Bite);
			return;
		}
		const float Depth = FMath::Clamp(S->DepthAt(Bobber), 0.f, 1.f);
		FFVCatchContext Ctx = O.Game->MakeCatchContext(Depth);
		Ctx.Depth = Depth;
		BaitUsed = P->GetBait();
		Fish = nullptr;
		Junk = nullptr;
		const FFVFishDef* Gated = nullptr;     // update 5: a legend a hard tackle rule kept away (PickCatch skipped it)
		FVDB::PickCatch(Ctx, Rng, Fish, Junk, &Gated);
		bool bGates = true;
		const bool bWasForced = !ForcedFish.IsNone();
		if (!ForcedFish.IsNone())
		{
			// autotest (DebugForceBite(FishId)): the roll above still happens, so the Rng stream stays the same;
			// the forced fish ignores the legend gates unless asked for them
			if (const FFVFishDef* F = FVDB::FindFish(ForcedFish))
			{
				const bool bKeptAway = bForcedGates && F->Legend != EFVLegend::None && FVDB::LegendGate(*F, Ctx).Result == EFVLegendGate::NoBite;
				if (!bKeptAway) { Fish = F; Junk = nullptr; }
				else if (!Gated) { Gated = F; }
			}
			bGates = bForcedGates;
			ForcedFish = NAME_None;
			bForcedGates = false;
		}
		bTrial = false;
		if (!TrialFish.IsNone())
		{
			// 0.5 legend trial: the legend bites wherever the bobber is, whatever the tackle and bait (no gates)
			if (const FFVFishDef* F = FVDB::FindFish(TrialFish))
			{
				Fish = F;
				Junk = nullptr;
				Gated = nullptr;
				bGates = false;
				bTrial = true;
			}
			TrialFish = NAME_None;
		}
		if (!bTrial && ExpertBite(Depth, bWasForced))
		{
			Gated = nullptr;   // an expert rod in the City / Cave: the expert table decided (classic legends never come here)
			bGates = false;
		}
		if (Fish && Junk) { Junk = nullptr; }
		if (Gated) { ShowGatedToast(*Gated); }
		if (bGates && Fish && Fish->Legend != EFVLegend::None)
		{
			// a missing soft rule (a weak line): it bites, but snaps the leader at the end of the hook intro
			bPendingSnap = FVDB::LegendGate(*Fish, Ctx).Result == EFVLegendGate::Snap;
		}
		if (!Fish && !Junk)
		{
			const TArray<FFVJunkDef>& All = FVDB::Junk();
			if (All.Num() > 0) { Junk = &All[Rng.RandHelper(All.Num())]; }
		}
		if (!Fish && !Junk)
		{
			S->SpawnFloatingText(S->GetHeadTop() - FVector2D(0.f, 8.f), TEXT("Не клюёт…"), FV::Col(230, 230, 230));
			StartRetrieve();
			return;
		}
		if (BaitUsed && !bTrial) { P->ConsumeBait(); }
		const EFVLegend Legend = Fish ? Fish->Legend : EFVLegend::None;
		if (Legend == EFVLegend::Angler)
		{
			// «Огонёк»: a light grows under the bobber first — no «!», no sound, presses ignored (see Tick)
			GlowTime = FVLegendFight::AnGlow;
			bGlowDone = false;
		}
		else
		{
			Sfx(EFVSfx::Bite);
			S->SpawnSplash(Bobber, 0.5f);
		}
		if (Legend != EFVLegend::None && Legend != EFVLegend::TsarCarp)
		{
			// update 5: every legend from the sturgeon on gives a shorter window (the Tsar-carp keeps the classic one)
			ReactWindow = GlowTime + FMath::Max(FVLegendFight::MinReact, FVLegendFight::ReactBase(Legend) + Cur.ReactBonus);
		}
		else
		{
			ReactWindow = FMath::Max(0.4f, ((Fish && Fish->Rarity == EFVRarity::Common) ? 1.25f : 1.f) + Cur.ReactBonus);
		}
		SetState(EFVFishState::Bite);
	}

	/** Update 5 (§3): «a hard rule kept the legend away» — once per visit and legend, never after it was caught. */
	void ShowGatedToast(const FFVFishDef& L)
	{
		UFVSaveGame* P = Profile();
		if (!O.Game || !P || L.Prep.GatedToast.IsEmpty() || GatedToastShown.Contains(L.Id)) { return; }
		if (const FFVFishRecord* R = P->GetRecord(L.Id))
		{
			if (R->Caught > 0) { return; }
		}
		GatedToastShown.Add(L.Id);
		O.Game->Toast(L.Prep.GatedToast, FV::Col(255, 222, 150), EFVIcon::Fish);
		UE_LOG(LogFV, Log, TEXT("FV: legend %s kept away by its tackle rules"), *L.Id.ToString());
	}

	void Hook()
	{
		FVScene* S = Scene();
		UFVSaveGame* P = Profile();
		if (!S || !P || (!Fish && !bPractice)) { StartRetrieve(); return; }
		Sfx(EFVSfx::Hook);
		ExMode = EExMode::None;
		if (!bPractice && !bTrial && StartExpertHook()) { return; }
		const int32 Level = P->GetLevel();
		const FFVLocationDef* Loc = FVDB::FindLocation(S->GetLocationId());
		float BarH = FVDB::CatchBarHeight(Level, Cur);
		float Reel = Cur.ReelMult, Drain = Cur.DrainMult;
		if (bTrial && Fish)
		{
			// 0.5 legend trial: the tackle meant for the legend's level, not the player's own
			FFVRodStats T;
			int32 TrialLevel = Level;
			if (FVTrialImpl::Stats(Fish->Id, T, TrialLevel))
			{
				BarH = FVDB::CatchBarHeight(TrialLevel, T);
				Reel = T.ReelMult;
				Drain = T.DrainMult;
			}
			++TrialScore.FindOrAdd(Fish->Id).X;
			UE_LOG(LogFV, Log, TEXT("FV: legend trial %s (bar %.1f, reel %.3f, drain %.3f)"), *Fish->Id.ToString(), BarH, Reel, Drain);
		}
		if (bPractice)
		{
			// an easy Mixed fish, a wider bar, never a treasure — and it never gets away (0.4.1)
			O.Mini.Start(FVFishingPracticeImpl::Difficulty, EFVMotion::Mixed, BarH + FVFishingPracticeImpl::ExtraBar, Cur.ReelMult, Cur.DrainMult, false, int32(Rng.GetUnsignedInt() & 0x7FFFFFFFu));
			O.Mini.bNoEscape = true;
		}
		else
		{
			const float TreasureChance = (Loc ? Loc->TreasureChance : 0.f) + Cur.TreasureBonus;
			const bool bTreasure = Rng.FRand() < TreasureChance;   // rolled for every fish: the Rng stream stays the same
			O.Mini.Start(float(Fish->Difficulty), Fish->Motion, BarH, Reel, Drain, bTreasure, int32(Rng.GetUnsignedInt() & 0x7FFFFFFFu), Fish->Legend);
			if (O.Mini.HasLegendRules()) { O.Mini.bHasTreasure = false; }   // update 5: no chests in legend-rule fights
		}
		GiveUpRect = FFVRect();
		bGiveUpClicked = false;
		bEmptyHintShown = false;
		Accum = 0.f;
		TreasurePop = 0.f;
		bTreasureWasCaught = false;
		MiniRipple = 0.f;
		IntroT = 0.f;
		BannerAge = 100.f;
		SeenBannerSerial = 0;
		PrevHazard = 0;
		PrevBlink = 0;
		bPrevFake = false;
		if (!bPractice && Fish->bLegendary)
		{
			// update 5: «Легенда на крючке!» — a 0.7 s freeze under the banner; the legend counts as met (saved at once)
			IntroT = FVLegendFight::IntroFreeze;
			StartBanner(TEXT("Легенда на крючке!"), true);
			Sfx(EFVSfx::LegendHook, 0.9f);
			if (!bTrial && !P->LegendsMet.Contains(Fish->Id))
			{
				P->MarkLegendMet(Fish->Id);
				O.Game->SaveNow();
			}
			UE_LOG(LogFV, Log, TEXT("FV: legend %s on the hook (rules=%d, snap=%d)"), *Fish->Id.ToString(), O.Mini.HasLegendRules() ? 1 : 0, bPendingSnap ? 1 : 0);
		}
		else
		{
			bPendingSnap = false;
		}
		S->SetPlayerAnim(EFVCharAnim::Reel, true);
		S->SpawnSplash(Bobber, 1.f);
		Reeling(true, 0.4f);
		SetState(EFVFishState::Minigame);
	}

	void StartBanner(const FString& Text, bool bShine)
	{
		BannerText = Text;
		BannerAge = 0.f;
		bBannerShine = bShine;
	}

	/** Update 5: the leader snapped (a weak line for this legend): the line stays on the rod, only the bait is lost. */
	void SnapLeader()
	{
		Sfx(EFVSfx::LineSnap);
		if (O.Game && Fish)
		{
			const FString& Text = Fish->Prep.SnapText;
			O.Game->Toast(Text.IsEmpty() ? FString(TEXT("Леска слабовата для этой рыбы — поводок оборван.")) : Text, FV::Col(255, 150, 130), EFVIcon::Line);
			UE_LOG(LogFV, Log, TEXT("FV: %s snapped the leader"), *Fish->Id.ToString());
		}
		Escape(TEXT("Поводок оборван!"));
	}

	/** Update 5: sounds and banners that follow the legend rules (called after the minigame steps). */
	void LegendFeedback()
	{
		const FFVMinigame& M = O.Mini;
		if (M.BannerSerial != SeenBannerSerial)
		{
			SeenBannerSerial = M.BannerSerial;
			switch (M.Banner)
			{
			// update 6: the Master of the Omut reuses the three phase banners with its own words
			case FFVMinigame::ELegendBanner::Rage:
				StartBanner(M.Legend == EFVLegend::MillMaster ? TEXT("Жернова!") : TEXT("Жар-рыба в ярости!"), false);
				break;
			case FFVMinigame::ELegendBanner::LastDash: StartBanner(TEXT("Последний рывок!"), false); break;
			case FFVMinigame::ELegendBanner::Coils:
				StartBanner(M.Legend == EFVLegend::MillMaster ? TEXT("Муть! Ил прячет сома") : TEXT("Змей свивается кольцами!"), false);
				break;
			default: break;
			}
			Sfx(EFVSfx::LegendHook, 0.8f, 1.1f);
		}
		const float Pitch = FVLegendFight::WarnPitch(M.Legend);
		if (M.HazardState != PrevHazard || M.bFakeLantern != bPrevFake)
		{
			const bool bWarn = M.HazardState == 1 && !(M.Legend == EFVLegend::Angler && M.bFakeLantern);
			// the fog forecast may flicker at a band's edge: its (quieter) warning sounds at most every 1.2 s
			if (bWarn && (M.Legend != EFVLegend::FogSerpent || Time - LastWarnTime > 1.2f))
			{
				LastWarnTime = Time;
				Sfx(EFVSfx::LegendWarn, M.Legend == EFVLegend::FogSerpent ? 0.6f : 0.85f, Pitch);
			}
			switch (M.Legend)
			{
			case EFVLegend::Sturgeon:
				if (M.HazardState == 1) { Sfx(EFVSfx::Splash, 0.35f, 0.6f); }        // rushing water
				else if (M.HazardState == 2) { Sfx(EFVSfx::Splash, 0.7f, 0.75f); }
				break;
			case EFVLegend::Angler:
				if (bWarn) { Sfx(EFVSfx::Splash, 0.3f, 1.6f); }                       // «бульк-динь»
				else if (M.HazardState == 2) { Sfx(EFVSfx::Splash, 0.7f, 1.1f); }     // the lunge
				break;
			case EFVLegend::ZharRyba:
				if (M.HazardState == 1) { Sfx(EFVSfx::Fire, 0.5f, 0.7f); }            // lava bubbling
				else if (M.HazardState == 2) { Sfx(EFVSfx::Fire, 0.9f, 0.9f); }
				break;
			case EFVLegend::MillMaster:
				if (M.Phase == 0)
				{
					if (M.HazardState == 1) { Sfx(EFVSfx::Splash, 0.35f, 0.5f); }    // the wheel churns
					else if (M.HazardState == 2) { Sfx(EFVSfx::Splash, 0.6f, 0.55f); }
				}
				else if (M.Phase != 2)
				{
					if (M.HazardState == 1) { Sfx(EFVSfx::Hammer, 0.85f, 0.45f); }   // the dull knock of the millstones
					else if (M.HazardState == 2) { Sfx(EFVSfx::Splash, 0.8f, 0.6f); } // the heavy jerk
				}
				break;
			default: break;
			}
			PrevHazard = M.HazardState;
			bPrevFake = M.bFakeLantern;
		}
		if (M.BlinkState != PrevBlink)
		{
			if (M.BlinkState == 1) { Sfx(EFVSfx::LegendWarn, 0.8f, 1.5f); }
			PrevBlink = M.BlinkState;
		}
		if (M.BurnFlash >= 0.99f) { Sfx(EFVSfx::Sizzle, 0.55f, 1.2f); }            // the bar burns (Sizzle retriggers at most every 0.3 s)
	}

	void EnterShowcase(const FFVSprite* Sprite)
	{
		FVScene* S = Scene();
		Reeling(false);
		bBobberVisible = false;
		PerfectFlash = Result.bPerfect ? 1.8f : 0.f;
		if (S)
		{
			S->SpawnSplash(LineEnd(), 0.9f);
			S->SetFacing(EFVFacing::Down);
			S->SetPlayerAnim(EFVCharAnim::HoldFish, true);
			S->SetHeldSprite(Sprite);
			const FVector2D Head = S->GetHeadTop();
			if (bPractice)
			{
				// the practice fish gives no XP: just a happy little burst
				S->SpawnSparkles(Head - FVector2D(0.f, 18.f), FV::Col(170, 240, 140), 10);
			}
			else if (FV::IsPortrait())
			{
				// Portrait: the card sits above/below the player, so "+XP" rises beside him on the roomier side.
				// (hotfix 0.61: the side of the SCREEN on a scrolling classic place — the pond; big worlds unchanged)
				const float PX = S->IsBigWorld() ? float(S->GetPlayerPos().X) : float(S->WorldToScreen(S->GetPlayerPos()).X);
				const float Side = PX < FV::VW * 0.5f ? 46.f : -46.f;
				S->SpawnFloatingText(Head + FVector2D(Side, -6.f), FString::Printf(TEXT("+%d опыта"), Result.Xp), FVPal::Green());
			}
			else
			{
				// "+XP" rises beside the held fish, on the side opposite to the result card (hotfix 0.61: screen side)
				const float PX = S->IsBigWorld() ? float(S->GetPlayerPos().X) : float(S->WorldToScreen(S->GetPlayerPos()).X);
				const float Side = PX < FV::VW * 0.5f ? -40.f : 40.f;
				S->SpawnFloatingText(Head + FVector2D(Side, -30.f), FString::Printf(TEXT("+%d опыта"), Result.Xp), FVPal::Green());
			}
			const FLinearColor Spark = Result.Fish ? FVDB::RarityColor(Result.Fish->Rarity) : FV::Col(255, 236, 170);
			S->SpawnSparkles(Head - FVector2D(0.f, 18.f), Spark, Result.bPerfect ? 24 : 12);
		}
		SetState(EFVFishState::Showcase);
	}

	void FinishFish()
	{
		UFVSaveGame* P = Profile();
		FVScene* S = Scene();
		if (!P || !S || !Fish || !O.Game) { StartRetrieve(); return; }
		const FFVFishDef& F = *Fish;
		const int32 Level = P->GetLevel();
		FFVCatchResult Res;
		Res.Fish = &F;
		Res.bPerfect = O.Mini.bPerfect;
		const float SizeFrac = FMath::Clamp(FVDB::RollSizeFrac(Level, Res.bPerfect, O.Mini.Elapsed, BaitUsed, Rng) + Cur.SizeBonus, 0.f, 1.f);
		Res.SizeCm = FMath::RoundToFloat(FMath::Lerp(F.MinSize, F.MaxSize, SizeFrac) * 10.f) / 10.f;
		Res.Quality = FVDB::RollQuality(SizeFrac, Res.bPerfect, Rng);
		Res.Xp = FVDB::CatchXp(F, Res.Quality, Res.bPerfect);
		Res.UnitPrice = FVDB::FishPrice(F, Res.Quality);
		if (O.Mini.bTreasureCaught)
		{
			Res.bTreasure = true;
			if (const FFVLocationDef* Loc = FVDB::FindLocation(S->GetLocationId())) { Res.Treasure = FVDB::RollTreasure(*Loc, Level, Rng); }
		}
		O.Game->HandleCatch(Res);
		Result = Res;
		bHasResult = true;
		Sfx(EFVSfx::Catch);
		if (Res.bPerfect) { Sfx(EFVSfx::Perfect, 0.9f); }
		if (Res.bTreasure) { Sfx(EFVSfx::Treasure, 0.8f); }
		UE_LOG(LogFV, Log, TEXT("FV: caught %s (%.1f cm, q%d, perfect=%d, treasure=%d, %.1fs)"), *F.Id.ToString(), Res.SizeCm, int32(Res.Quality), Res.bPerfect ? 1 : 0, Res.bTreasure ? 1 : 0, O.Mini.Elapsed);
		UFVSprites* Sp = UFVSprites::Get();
		EnterShowcase(Sp ? &Sp->Fish(F) : nullptr);
	}

	/** Update 4b: the practice fish is caught — a friendly card, nothing is recorded (no XP, stats, records, keepnet). */
	void FinishPractice()
	{
		if (!Scene()) { StartRetrieve(); return; }
		Result = FFVCatchResult();
		Result.bPerfect = O.Mini.bPerfect;
		Result.bStored = true;
		bHasResult = true;
		Sfx(EFVSfx::Catch);
		if (Result.bPerfect) { Sfx(EFVSfx::Perfect, 0.7f); }
		UE_LOG(LogFV, Log, TEXT("FV: practice fish caught (perfect=%d, %.1fs)"), Result.bPerfect ? 1 : 0, O.Mini.Elapsed);
		UFVSprites* Sp = UFVSprites::Get();
		EnterShowcase(Sp ? &Sp->Icon(EFVIcon::Fish) : nullptr);
	}

	/** 0.5 legend trial landed: a cheer and the session score, nothing recorded; the line comes back empty. */
	void FinishTrial()
	{
		FVScene* S = Scene();
		if (Fish) { ++TrialScore.FindOrAdd(Fish->Id).Y; }
		Sfx(EFVSfx::Catch);
		if (O.Mini.bPerfect) { Sfx(EFVSfx::Perfect, 0.7f); }
		if (S) { S->SpawnSparkles(S->GetHeadTop() - FVector2D(0.f, 18.f), FVDB::RarityColor(EFVRarity::Legendary), 24); }
		UE_LOG(LogFV, Log, TEXT("FV: legend trial %s landed (%.1fs)"), Fish ? *Fish->Id.ToString() : TEXT("?"), O.Mini.Elapsed);
		TrialToast(true);
		bTrial = false;
		StartRetrieve();
	}

	void TrialToast(bool bLanded)
	{
		if (!O.Game || !Fish) { return; }
		const FIntPoint Sc = TrialScore.FindRef(Fish->Id);
		O.Game->Toast(FString::Printf(TEXT("Тест «%s»: %s Поймано %d из %d"), *Fish->Name, bLanded ? TEXT("есть!") : TEXT("сорвалась."), Sc.Y, Sc.X),
			bLanded ? FVPal::Gold() : FV::Col(255, 190, 170), EFVIcon::Fish);
	}

	/** Update 4b: after the practice card the little fish is let go back into the pond. */
	void ReleasePracticeFish()
	{
		FVScene* S = Scene();
		if (!S) { return; }
		const FVector2D P = S->GetCastTarget(0.2f, 1.f);
		if (S->IsWater(P))
		{
			S->SpawnSplash(P, 0.6f);
			Sfx(EFVSfx::Splash, 0.5f, 1.2f);
		}
	}

	void FinishJunk()
	{
		UFVSaveGame* P = Profile();
		if (!P || !Junk || !O.Game) { StartRetrieve(); return; }
		FFVCatchResult Res;
		Res.Junk = Junk;
		Res.Quality = EFVQuality::Normal;
		Res.Xp = FVDB::JunkXp();
		Res.UnitPrice = Junk->Price;
		O.Game->HandleCatch(Res);
		Result = Res;
		bHasResult = true;
		Sfx(EFVSfx::Hook, 0.7f);
		Sfx(EFVSfx::Catch, 0.6f, 0.8f);
		UE_LOG(LogFV, Log, TEXT("FV: pulled out junk %s"), *Junk->Id.ToString());
		UFVSprites* Sp = UFVSprites::Get();
		EnterShowcase(Sp ? &Sp->Junk(Junk->Style) : nullptr);
	}

	/** Text (update 5): the floating line over the head (the leader snap passes its own). */
	void Escape(const FString& Text = TEXT("Рыба сорвалась!"))
	{
		FVScene* S = Scene();
		Reeling(false);
		Sfx(EFVSfx::Escape);
		IntroT = 0.f;
		bPendingSnap = false;
		if (bTrial)
		{
			// 0.5 legend trial: no escape stats, just the score
			UE_LOG(LogFV, Log, TEXT("FV: legend trial %s got away (%.1fs)"), Fish ? *Fish->Id.ToString() : TEXT("?"), O.Mini.Elapsed);
			TrialToast(false);
			bTrial = false;
		}
		else if (O.Game && !bPractice) { O.Game->HandleEscape(Fish); }
		if (S)
		{
			if (bPractice) { S->SpawnFloatingText(S->GetHeadTop() - FVector2D(0.f, 10.f), FVFishingPracticeImpl::TryAgainText(), FV::Col(255, 222, 150)); }
			else { S->SpawnFloatingText(S->GetHeadTop() - FVector2D(0.f, 10.f), Text, FV::Col(255, 130, 110)); }
			S->SpawnSplash(LineEnd(), 1.1f);
			S->SetPlayerAnim(EFVCharAnim::FishWait, true);
		}
		Bobber = LineEnd();
		SetState(EFVFishState::Escaped);
	}

	// ============================================================================ update 6 [FISHING-EXPERT]
	/** The equipped expert rod's control data (nullptr: a classic rod is equipped). */
	const FFVExpertRodDef* EquippedExpertRod() const
	{
		const UFVSaveGame* P = Profile();
		if (!P || !O.Game || !O.Game->IsExpertRodEquipped()) { return nullptr; }
		return FVDBU6::FindExpertRod(P->EquippedRod);
	}

	/** The expert catch context; the parts GAME-FLOW may leave empty (rod, tackle, bait, local weather) are filled here. */
	FFVExpertCatchContext MakeExCtx(float Depth) const
	{
		FFVExpertCatchContext X = O.Game ? O.Game->MakeExpertContext(Depth) : FFVExpertCatchContext();
		const UFVSaveGame* P = Profile();
		if (P && X.RodId.IsNone())
		{
			X.RodId = P->EquippedRod;
			if (const FFVExpertRodDef* R = FVDBU6::FindExpertRod(P->EquippedRod)) { X.Type = R->Type; }
			const FFVExpertLoadout L = P->GetExpertLoadout(P->EquippedRod);
			X.Line = FVDBU6::LineKindOf(L.Line);
			X.Float = FVDBU6::FloatKindOf(L.Float);
			X.Reel = FVDBU6::ReelKindOf(L.Reel);
		}
		if (P && X.Bait.IsNone())
		{
			if (const FFVBaitDef* B = P->GetBait()) { X.Bait = B->Id; }
		}
		if (!X.Local.bValid && O.Game) { X.Local = O.Game->GetLocalWeather(); }
		X.Depth = FMath::Clamp(Depth, 0.f, 1.f);
		return X;
	}

	/** The training fish of this place: a universal expert fish (the lowest rarity). */
	const FFVFishDef* TrainingFish(const FFVExpertCatchContext& X) const
	{
		const FFVFishDef* Best = nullptr;
		for (const FFVExpertFishDef& E : FVDBU6::ExpertFish())
		{
			if (!E.bUniversal || E.Legend != EFVExpertLegend::None) { continue; }
			const FFVFishDef* F = FVDB::FindFish(E.FishId);
			if (!F || F->Location != X.Location) { continue; }
			if (!Best || F->Rarity < Best->Rarity) { Best = F; }
		}
		return Best;
	}

	/** Bite in the City / Cave. Expert rod: the expert table decides (true). Classic rod: Р25 — 5 % an expert fish. */
	bool ExpertBite(float Depth, bool bWasForced)
	{
		FVScene* S = Scene();
		if (!S || !O.Game || !FVDBU6::IsExpertLocation(S->GetLocationId())) { return false; }
		if (EquippedExpertRod())
		{
			if (bWasForced && Fish) { return true; }   // autotest picked the fish
			const FFVExpertCatchContext X = MakeExCtx(Depth);
			const FFVFishDef* F = bExTutorialPending ? TrainingFish(X) : nullptr;
			if (!F) { F = FVDBU6::PickExpertCatch(X, Rng); }
			Fish = F;
			Junk = nullptr;
			return true;
		}
		if (!bWasForced && Fish && !Fish->bLegendary && !Fish->bExpert && Rng.FRand() < 0.05f)
		{
			const FFVFishDef* F = FVDBU6::PickExpertCatch(MakeExCtx(Depth), Rng);
			if (F && F->bExpert && !F->bLegendary) { Fish = F; Junk = nullptr; }
		}
		return false;
	}

	/** Common tail of every expert hook (as in Hook()). */
	void EnterMinigame()
	{
		FVScene* S = Scene();
		GiveUpRect = FFVRect();
		bGiveUpClicked = false;
		bEmptyHintShown = false;
		Accum = 0.f;
		TreasurePop = 0.f;
		bTreasureWasCaught = false;
		bExTreasureWasCaught = false;
		MiniRipple = 0.f;
		BannerAge = 100.f;
		SeenBannerSerial = 0;
		PrevHazard = 0;
		PrevBlink = 0;
		bPrevFake = false;
		bPendingSnap = false;
		bExPrevDown = false;
		bExPendingPress = false;
		bExPendingRelease = false;
		ExStick = FVector2D::ZeroVector;
		ExTargetY = -1.f;
		ExLastMove = -1;
		bExPrevMagnet = false;
		ExCreakT = 0.f;
		ExHandT = 0.f;
		bExGiveUpClicked = false;
		if (S)
		{
			S->SetPlayerAnim(EFVCharAnim::Reel, true);
			S->SpawnSplash(Bobber, 1.f);
		}
		Reeling(true, 0.4f);
		SetState(EFVFishState::Minigame);
	}

	/** A legend of any kind is on the hook: it counts as met at once (saved). */
	void MeetLegend()
	{
		UFVSaveGame* P = Profile();
		if (!P || !Fish || !O.Game) { return; }
		if (!P->LegendsMet.Contains(Fish->Id))
		{
			P->MarkLegendMet(Fish->Id);
			O.Game->SaveNow();
		}
	}

	/** Hook with an expert rod equipped or an expert fish on the line. false = the classic minigame (unchanged). */
	bool StartExpertHook()
	{
		if (!Fish || !Profile() || !Scene() || !O.Game) { return false; }
		const FFVExpertRodDef* Rod = EquippedExpertRod();
		if (Fish->bExpert)
		{
			if (!Rod) { StartExpertOnClassic(); return true; }
			StartExpertFight(*Rod, FVDBU6::FindExpertFish(Fish->Id));
			return true;
		}
		if (!Rod) { return false; }
		if (Fish->bLegendary) { StartLegendOnExpert(*Rod); return true; }
		StartExpertFight(*Rod, nullptr);   // Р18: a classic fish, the expert way of fishing
		return true;
	}

	void StartExpertFight(const FFVExpertRodDef& Rod, const FFVExpertFishDef* Def)
	{
		UFVSaveGame* P = Profile();
		FVScene* S = Scene();
		FFVExpertSetup Su;
		Su.Rod = Rod;
		Su.Stats = O.Game->GetEffectiveExpertStats();
		Su.Level = P->GetLevel();
		const FFVLocationDef* Loc = FVDB::FindLocation(S->GetLocationId());
		if (Def)
		{
			Su.Fish = *Def;
			const FFVExpertCatchContext X = MakeExCtx(FMath::Clamp(S->DepthAt(Bobber), 0.f, 1.f));
			Su.Hazards = FVDBU6::ExpertHazards(S->GetLocationId(), X.Local, O.Game->GetPhase(), Su.Level, X.bNearPiles);
		}
		else
		{
			Su.bClassicFish = true;
			Su.ClassicDifficulty = float(Fish->Difficulty);
			Su.ClassicMotion = Fish->Motion;
			Su.Fish.FishId = Fish->Id;
		}
		const float TreasureChance = (Loc ? Loc->TreasureChance : 0.f) + Cur.TreasureBonus;
		Su.bHasTreasure = !Fish->bLegendary && Rng.FRand() < TreasureChance;
		Su.bTutorial = bExTutorialPending && !Fish->bLegendary;
		if (Su.bTutorial) { bExTutorialPending = false; }
		// §10.12: the first fight of a new rod type — the «ghost hand» and no line snap
		const uint8 Bit = uint8(1u << uint32(Rod.Type));
		bExGhostHand = (P->U6.ExpertTypesTaught & Bit) == 0;
		if (bExGhostHand)
		{
			Su.bNoSnap = true;
			P->U6.ExpertTypesTaught |= Bit;
		}
		Ex = FFVExpertFight();
		Ex.Start(Su, int32(Rng.GetUnsignedInt() & 0x7FFFFFFFu));
		ExMode = EExMode::Fight;
		ExRodId = Rod.RodId;
		const FFVRodDef* RD = FVDB::FindRod(Rod.RodId);
		ExTheme = RD ? RD->Theme : EFVExpertTheme::Breeze;
		ExSeenSignal = Ex.SignalSerial;
		IntroT = 0.f;
		EnterMinigame();
		if (Fish->bLegendary)
		{
			IntroT = FVLegendFight::IntroFreeze;
			StartBanner(TEXT("Легенда на крючке!"), true);
			Sfx(EFVSfx::LegendHook, 0.9f);
			MeetLegend();
		}
		else if (bExGhostHand)
		{
			O.Game->SaveNow();
		}
		// the rod hint of a first / training fight: a short line off the field (FVExpertDraw::DrawHowTo), not a toast —
		// the toast covered the progress bar (portrait) and the top of the frame (landscape)
		UE_LOG(LogFV, Log, TEXT("FV: expert fight %s on %s (classic=%d, legend=%d, tutorial=%d)"), *Fish->Id.ToString(), *Rod.RodId.ToString(),
			Su.bClassicFish ? 1 : 0, Ex.IsLegend() ? 1 : 0, Su.bTutorial ? 1 : 0);
	}

	/** Р28: a classic legend on an expert rod — «Легенда на крючке!», it swims 2–3 s, then slips off. */
	void StartLegendOnExpert(const FFVExpertRodDef& Rod)
	{
		FFVExpertSetup Su;
		Su.Rod = Rod;
		Su.Stats = O.Game->GetEffectiveExpertStats();
		Su.Level = Profile()->GetLevel();
		Su.bClassicFish = true;
		Su.ClassicDifficulty = float(Fish->Difficulty);
		Su.ClassicMotion = Fish->Motion;
		Su.Fish.FishId = Fish->Id;
		Su.bNoSnap = true;
		Ex = FFVExpertFight();
		Ex.Start(Su, int32(Rng.GetUnsignedInt() & 0x7FFFFFFFu));
		ExMode = EExMode::LegendOnExpert;
		ExRodId = Rod.RodId;
		const FFVRodDef* RD = FVDB::FindRod(Rod.RodId);
		ExTheme = RD ? RD->Theme : EFVExpertTheme::Breeze;
		ExSeenSignal = Ex.SignalSerial;
		bExGhostHand = false;
		FakeLen = Rng.FRandRange(2.f, 3.f);
		IntroT = 0.f;
		EnterMinigame();
		StartBanner(TEXT("Легенда на крючке!"), true);
		Sfx(EFVSfx::LegendHook, 0.9f);
		MeetLegend();
		UE_LOG(LogFV, Log, TEXT("FV: classic legend %s on an expert rod (R28)"), *Fish->Id.ToString());
	}

	void EndLegendOnExpert()
	{
		if (O.Game && Fish)
		{
			O.Game->Toast(FString::Printf(TEXT("%s сорвался: экспертная удочка ему не подходит. Возьмите обычную удочку."), *Fish->Name),
				FV::Col(255, 190, 150), EFVIcon::Fish);
		}
		ExMode = EExMode::None;
		Escape(TEXT("Легенда сорвалась!"));
	}

	/** Р25: an expert fish on a classic rod — the classic panel, but the bar cannot hold it. */
	void StartExpertOnClassic()
	{
		UFVSaveGame* P = Profile();
		const float BarH = FVDB::CatchBarHeight(P->GetLevel(), Cur);
		O.Mini.Start(float(Fish->Difficulty), Fish->Motion, BarH, Cur.ReelMult, Cur.DrainMult, false, int32(Rng.GetUnsignedInt() & 0x7FFFFFFFu));
		O.Mini.bNoEscape = true;
		FakeLen = Rng.FRandRange(3.f, 4.f);
		FakeStartProgress = O.Mini.Progress;
		IntroT = 0.f;
		EnterMinigame();
		ExMode = EExMode::ExpertOnClassic;
		if (O.Game) { O.Game->Toast(TEXT("Экспертная рыба на крючке!"), FVPal::Gold(), EFVIcon::Fish); }
		Sfx(EFVSfx::LegendHook, 0.7f, 1.2f);
		UE_LOG(LogFV, Log, TEXT("FV: expert fish %s on a classic rod (R25)"), *Fish->Id.ToString());
	}

	float FakeCap() const
	{
		const float K = FMath::Clamp(StateTime / FMath::Max(0.1f, FakeLen), 0.f, 1.f);
		return FMath::Lerp(FakeStartProgress, 0.02f, K);
	}

	/** «Нужна удочка типа «Маятник» или «Штурвал».» (universal fish: any expert rod). */
	static FString NeedRodText(const FFVExpertFishDef* Def)
	{
		if (!Def || Def->bUniversal || Def->TypeMask == 0) { return TEXT("Нужна любая экспертная удочка."); }
		FString Types;
		for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
		{
			if ((Def->TypeMask & (1u << t)) == 0) { continue; }
			if (!Types.IsEmpty()) { Types += TEXT(" или "); }
			Types += FString::Printf(TEXT("«%s»"), *FVDBU6::RodTypeName(EFVExpertRodType(t)));
		}
		return FString::Printf(TEXT("Нужна удочка типа %s."), *Types);
	}

	void EndExpertOnClassic()
	{
		UFVSaveGame* P = Profile();
		if (O.Game && Fish)
		{
			const FFVExpertFishDef* Def = FVDBU6::FindExpertFish(Fish->Id);
			O.Game->Toast(FString(TEXT("Леска не выдержала: это экспертная рыба. ")) + NeedRodText(Def), FV::Col(255, 190, 150), EFVIcon::Fish);
			// the slip reveals the rod types of the fish (§10.11: the only fact a fish gives by itself)
			if (P)
			{
				const TArray<FFVFishFact>& Fs = FVDBU6::Facts(Fish->Id);
				for (int32 i = 0; i < Fs.Num(); ++i)
				{
					if (Fs[i].Kind == EFVFactKind::RodTypes && P->LearnFact(Fish->Id, i))
					{
						Sfx(EFVSfx::Find, 0.7f);
						O.Game->SaveNow();
					}
				}
			}
		}
		ExMode = EExMode::None;
		Escape(TEXT("Леска не выдержала!"));
	}

	/** §10.9: casting with a legend's own bait shows its checklist — only facts the player knows (Р33). */
	void LegendBaitCheck()
	{
		const UFVSaveGame* P = Profile();
		if (!P || !O.Game || bPractice) { return; }
		const FFVBaitDef* B = P->GetBait();
		if (!B) { return; }
		for (const FFVExpertFishDef& E : FVDBU6::ExpertFish())
		{
			if (E.Legend == EFVExpertLegend::None || E.OwnBait.IsNone() || E.OwnBait != B->Id) { continue; }
			const FFVFishDef* LF = FVDB::FindFish(E.FishId);
			const FString Name = LF ? LF->Name : FVDB::ItemName(E.FishId);
			const FFVExpertCatchContext X = MakeExCtx(0.5f);
			const FName Id = E.FishId;
			const TArray<FFVExpertCheckItem> Items = FVDBU6::ExpertChecklist(E, X, [P, Id](int32 Idx) { return P->KnowsFact(Id, Idx); });
			FString Line, Failed;
			for (const FFVExpertCheckItem& It : Items)
			{
				if (!Line.IsEmpty()) { Line += TEXT(" · "); }
				Line += It.bKnown ? FString::Printf(TEXT("%s — %s"), *It.Label, It.bOk ? TEXT("да") : TEXT("нет")) : FString(TEXT("???"));
				if (It.bKnown && !It.bOk)
				{
					if (!Failed.IsEmpty()) { Failed += TEXT(", "); }
					Failed += It.Label;
				}
			}
			if (!Failed.IsEmpty())
			{
				O.Game->Toast(FString::Printf(TEXT("Условия для «%s» не выполнены: %s. Наживка уйдёт на обычную рыбу."), *Name, *Failed),
					FV::Col(255, 190, 150), EFVIcon::Fish);
			}
			else if (!Line.IsEmpty())
			{
				O.Game->Toast(FString::Printf(TEXT("«%s»: %s"), *Name, *Line), FVPal::Gold(), EFVIcon::Fish);
			}
			return;
		}
	}

	FFVRect CurExField() const
	{
		if (ExField.W > 0.f) { return ExField; }
		const FVScene* S = Scene();
		return FVExpertDraw::FieldRect(S ? float(S->WorldToScreen(S->GetPlayerPos()).X) : FV::VW * 0.5f);
	}

	/** Ползунок: the track Y (0 = top .. TrackH) at a screen Y (FVExpertDraw::ScreenToTrack: the water track inside the
	 *  panel); outside the track the finger / mouse is clamped to the ends. */
	float ExTrackYAt(float ScreenY) const
	{
		const FFVRect Field = CurExField();
		if (Field.H <= 1.f) { return FVExpert::Mid; }
		return FVExpertDraw::ScreenToTrack(Field, ScreenY);
	}

	/** §19: one control for every type — press / hold Space, LMB or a finger anywhere (the [X] of a training fight
	 *  excluded). Ползунок: the finger / mouse held anywhere leads the bar to the track Y at its height (drag along the
	 *  panel; the finger may stay beside it), W/S / arrows accelerate it. */
	bool ReadExpertInput(const FFVInput& In)
	{
		const bool bSlider = Ex.Setup.Rod.Type == FVExpert::Slider;
		const FFVRect Field = CurExField();
		bool bDown = false;
		float TargetY = -1.f;
		FVector2D Stick = FVector2D::ZeroVector;
		if (In.Touches.Num() > 0)
		{
			for (const FFVTouchPoint& Tp : In.Touches)
			{
				if (ExGiveUp.W > 0.f && ExGiveUp.Inset(-6.f).Contains(Tp.Pos)) { continue; }
				// portrait: the top bar and the corner buttons live above the panel — a finger there is not the rod
				if (FV::IsPortrait() && Field.H > 1.f && Tp.Pos.Y < Field.Y - 4.f) { continue; }
				// both orientations: a finger on the top bar (level badge, money...) is never the rod
				if (Tp.Pos.Y < FV::TopBarH) { continue; }
				bDown = true;
				if (bSlider) { TargetY = ExTrackYAt(float(Tp.Pos.Y)); }
				break;
			}
		}
		else
		{
			bDown = In.bActionDown;
			if (bSlider)
			{
				if (In.bLmbDown && In.bMouseValid && !(ExGiveUp.W > 0.f && ExGiveUp.Inset(-6.f).Contains(In.Mouse)))
				{
					TargetY = ExTrackYAt(float(In.Mouse.Y));
					bDown = true;
				}
				else if (!FMath::IsNearlyZero(In.MoveY))
				{
					Stick.Y = FMath::Clamp(In.MoveY, -1.f, 1.f);
					bDown = true;
				}
			}
		}
		ExStick = Stick;
		ExTargetY = TargetY;
		if (bDown && !bExPrevDown) { bExPendingPress = true; }
		if (!bDown && bExPrevDown) { bExPendingRelease = true; }
		bExPrevDown = bDown;
		return bDown;
	}

	void ExpertFeedback(float Dt)
	{
		UFVSaveGame* P = Profile();
		if (Ex.SignalSerial != ExSeenSignal)
		{
			ExSeenSignal = Ex.SignalSerial;
			if (Ex.Signal != EFVIcon::Count)
			{
				if (Ex.IsLegend()) { Sfx(EFVSfx::ExpertWarn, 0.8f, 0.85f + 0.06f * float(uint8(Ex.Setup.Fish.Legend))); }
				else { Sfx(EFVSfx::Whoosh, 0.6f); }
			}
		}
		if (Ex.IsLegend() && Ex.MoveIndex >= 0 && Ex.MoveIndex != ExLastMove)
		{
			ExLastMove = Ex.MoveIndex;
			if (P && Fish) { P->MarkExpertMoveSeen(Fish->Id, Ex.MoveIndex); }
		}
		if (Ex.bMagnetHeld && !bExPrevMagnet) { Sfx(EFVSfx::Tension, 0.2f, 1.3f); }   // Магнит: the pull starts
		bExPrevMagnet = Ex.bMagnetHeld;
		ExCreakT -= Dt;
		if (Ex.Tension > 0.7f && ExCreakT <= 0.f)
		{
			ExCreakT = 0.5f;
			Sfx(EFVSfx::Tension, 0.3f + 0.5f * Ex.Tension);
		}
		if (Ex.bTreasureCaught && !bExTreasureWasCaught)
		{
			bExTreasureWasCaught = true;
			TreasurePop = 1.f;
			Sfx(EFVSfx::Treasure, 0.7f, 1.1f);
		}
		TreasurePop = FMath::Max(0.f, TreasurePop - Dt);
	}

	void TickExpert(float Dt, const FFVInput& In, bool bAuto)
	{
		FVScene* S = Scene();
		if (!S || !Profile()) { ToIdle(); return; }
		ExHandT += Dt;
		if (bExGiveUpClicked)
		{
			bExGiveUpClicked = false;
			if (Ex.Setup.bTutorial) { ExpertEnd(FFVExpertFight::EResult::GaveUp); return; }
		}
		const bool bDown = ReadExpertInput(In);
		if (IntroT > 0.f)
		{
			// «Легенда на крючке!»: the field stands still under the banner
			IntroT = FMath::Max(0.f, IntroT - Dt);
			Accum = 0.f;
			bExPendingPress = bExPendingRelease = false;
			ReelAngle += Dt * 4.f;
			return;
		}
		if (bExPendingPress && Ex.Setup.Rod.Type == EFVExpertRodType::Pendulum) { Sfx(EFVSfx::GravityFlip, 0.35f); }
		if (bExPendingPress && Ex.Setup.Rod.Type == FVExpert::Impulse) { Sfx(EFVSfx::CompassTick, 0.3f); }   // Импульс: a push
		Accum += Dt;
		FFVExpertFight::EResult R = FFVExpertFight::EResult::Running;
		int32 Steps = 0;
		const FFVExpertBot Bot = FFVExpertBot::Strong();
		while (Accum >= FV::SimDt && Steps < 8)
		{
			Accum -= FV::SimDt;
			++Steps;
			FFVExpertInput E;
			if (bAuto)
			{
				E = Ex.BotInput(Bot);
			}
			else
			{
				E.bDown = bDown;
				E.bPressed = bExPendingPress;
				E.bReleased = bExPendingRelease;
				E.Stick = ExStick;
				E.TargetY = ExTargetY;
				bExPendingPress = bExPendingRelease = false;
			}
			R = Ex.Step(E);
			if (ExMode == EExMode::LegendOnExpert)
			{
				// Р28: the legend only shows itself — nothing is decided before the slip
				Ex.Progress = FMath::Clamp(Ex.Progress, 0.1f, 0.9f);
				Ex.Tension = FMath::Min(Ex.Tension, 0.5f);
				R = FFVExpertFight::EResult::Running;
			}
			if (ExPinned >= 0.f)
			{
				Ex.Progress = ExPinned;
				Ex.Tension = FMath::Min(Ex.Tension, 0.9f);
				R = FFVExpertFight::EResult::Running;
			}
			if (R != FFVExpertFight::EResult::Running) { break; }
		}
		if (Steps >= 8) { Accum = 0.f; }
		ExpertFeedback(Dt);
		ReelAngle += Dt * (Ex.bInZone ? 14.f : 6.f) * (bDown ? 1.f : 0.6f);
		Reeling(true, Ex.bInZone ? 1.f : 0.35f);
		MiniRipple -= Dt;
		if (MiniRipple <= 0.f)
		{
			const bool bFast = FMath::Abs(Ex.FishVel) > 90.f;
			MiniRipple = bFast ? 0.18f : 0.4f;
			S->SpawnRipple(LineEnd(), bFast ? 0.7f : 0.45f);
			if (Ex.bFishRed && Rng.FRand() < 0.35f) { S->SpawnSplash(LineEnd(), 0.4f); }
		}
		if (ExMode == EExMode::LegendOnExpert)
		{
			if (StateTime >= FakeLen) { EndLegendOnExpert(); }
			return;
		}
		if (R != FFVExpertFight::EResult::Running) { ExpertEnd(R); }
	}

	void ExpertEnd(FFVExpertFight::EResult R)
	{
		UFVSaveGame* P = Profile();
		bExGhostHand = false;
		if (R == FFVExpertFight::EResult::Caught) { FinishExpertFish(); return; }
		if (Ex.IsLegend() && Fish && P && O.Game)
		{
			// the «Вы дошли до приёма 3 из 5» card (§10.8)
			const int32 Num = FVExpertLegends::Score(Ex.Setup.Fish.Legend).Num();
			const int32 Reached = FMath::Clamp(Ex.ScoreReached, 0, Num);
			int32& Best = P->U6.ExpertBestMove.FindOrAdd(Fish->Id);
			Best = FMath::Max(Best, Reached);
			if (Num > 0)
			{
				O.Game->Toast(FString::Printf(TEXT("%s: вы дошли до приёма %d из %d"), *Fish->Name, FMath::Max(1, Reached), Num), FV::Col(255, 222, 150), EFVIcon::Fish);
			}
			O.Game->SaveNow();
		}
		ExMode = EExMode::None;
		switch (R)
		{
		case FFVExpertFight::EResult::Snapped: Sfx(EFVSfx::LineSnap); Escape(TEXT("Леска лопнула!")); break;
		case FFVExpertFight::EResult::GaveUp: Escape(TEXT("Рыба отпущена")); break;
		default: Escape(TEXT("Рыба ушла!")); break;
		}
	}

	void FinishExpertFish()
	{
		UFVSaveGame* P = Profile();
		FVScene* S = Scene();
		ExMode = EExMode::None;
		if (!P || !S || !Fish || !O.Game) { StartRetrieve(); return; }
		const FFVFishDef& F = *Fish;
		const int32 Level = P->GetLevel();
		FFVCatchResult Res;
		Res.Fish = &F;
		Res.bPerfect = Ex.bFlawless;
		Res.bExpert = true;
		Res.bFlawless = Ex.bFlawless;
		Res.RodType = uint8(Ex.Setup.Rod.Type);
		const float SizeFrac = FMath::Clamp(FVDB::RollSizeFrac(Level, Res.bPerfect, Ex.Elapsed, BaitUsed, Rng) + Cur.SizeBonus, 0.f, 1.f);
		Res.SizeCm = FMath::RoundToFloat(FMath::Lerp(F.MinSize, F.MaxSize, SizeFrac) * 10.f) / 10.f;
		Res.Quality = FVDB::RollQuality(SizeFrac, Res.bPerfect, Rng);
		Res.Xp = F.bExpert ? FVDBU6::ExpertCatchXp(F, Res.Quality, Res.bPerfect) : FVDB::CatchXp(F, Res.Quality, Res.bPerfect);
		Res.UnitPrice = FVDB::FishPrice(F, Res.Quality);
		if (Ex.bTreasureCaught)
		{
			Res.bTreasure = true;
			if (F.bExpert)
			{
				bool bPage = false;
				Res.Treasure = FVDBU6::RollExpertTreasure(S->GetLocationId(), Level, Rng, bPage);
				if (bPage) { LearnJournalFact(F.Id); }
			}
			else if (const FFVLocationDef* Loc = FVDB::FindLocation(S->GetLocationId()))
			{
				Res.Treasure = FVDB::RollTreasure(*Loc, Level, Rng);
			}
		}
		O.Game->HandleCatch(Res);
		Result = Res;
		bHasResult = true;
		Sfx(EFVSfx::Catch);
		if (Res.bPerfect) { Sfx(EFVSfx::Perfect, 0.9f); }
		if (Res.bTreasure) { Sfx(EFVSfx::Treasure, 0.8f); }
		UE_LOG(LogFV, Log, TEXT("FV: expert catch %s (%.1f cm, q%d, flawless=%d, rod type %d, %.1fs)"), *F.Id.ToString(), Res.SizeCm, int32(Res.Quality),
			Res.bFlawless ? 1 : 0, int32(Res.RodType), Ex.Elapsed);
		UFVSprites* Sp = UFVSprites::Get();
		EnterShowcase(Sp ? &Sp->Fish(F) : nullptr);
	}

	/** §10.16: a «страница рыбацкого журнала» from an expert chest — one unknown fact (this fish first). */
	void LearnJournalFact(FName PreferFish)
	{
		UFVSaveGame* P = Profile();
		if (!P || !O.Game) { return; }
		FName FishId;
		int32 Idx = -1;
		if (!P->PickNextFact(PreferFish, Rng, FishId, Idx) || !P->LearnFact(FishId, Idx)) { return; }
		const TArray<FFVFishFact>& Fs = FVDBU6::Facts(FishId);
		const FString Row = Fs.IsValidIndex(Idx) ? Fs[Idx].Row : FString();
		O.Game->Toast(FString::Printf(TEXT("Страница рыбацкого журнала — %s: %s"), *FVDB::ItemName(FishId), *Row), FVPal::Gold(), EFVIcon::Fish);
		Sfx(EFVSfx::Find, 0.8f);
	}

	/** Bot input for AutoPlay (autotest). */
	bool BotHold(const FVScene& S, const UFVSaveGame& P) const
	{
		switch (O.State)
		{
		case EFVFishState::Idle: return StateTime > 0.3f && S.CanFishFromHere() && (S.IsHome() || !P.KeepnetFull());
		case EFVFishState::Charging: return !(Power >= 0.8f && ChargeTime < 1.f) && ChargeTime < 1.9f;
		case EFVFishState::Bite: return StateTime > GlowTime + 0.12f;   // update 5: after the Angler's «Огонёк»
		case EFVFishState::Minigame: return IntroT <= 0.f && O.Mini.BotShouldHold(0.9f);
		case EFVFishState::Showcase: return StateTime > 1.5f;
		default: return false;
		}
	}
};

// =============================================================================================
// FVFishing
// =============================================================================================
FVFishing::FVFishing(UFVGame* InGame)
	: Game(InGame)
	, Impl(MakeUnique<FImpl>(*this))
{
}

FVFishing::~FVFishing() = default;

void FVFishing::Reset()
{
	Reeling(false);
	FImpl& I = *Impl;
	I.bBobberVisible = false;
	I.Fish = nullptr;
	I.Junk = nullptr;
	I.BaitUsed = nullptr;
	I.ForcedFish = NAME_None;
	I.bForcedGates = false;
	I.bTrial = false;                      // a pending trial (TrialFish) survives a reset: the player still wants that fight
	I.bPendingSnap = false;
	I.GlowTime = 0.f;
	I.bGlowDone = true;
	I.IntroT = 0.f;
	I.BannerAge = 100.f;
	I.GatedToastShown.Reset();             // Reset runs on every location load: a new visit
	I.PinnedProgress = -1.f;
	I.bHasResult = false;
	I.PerfectFlash = 0.f;
	I.Accum = 0.f;
	I.bBotPrevHold = false;
	I.bPractice = false;
	I.ExMode = FImpl::EExMode::None;       // update 6: an expert fight is abandoned with the location
	I.ExPinned = -1.f;
	I.bExGhostHand = false;
	I.ExField = FFVRect();
	I.ExGiveUp = FFVRect();
	const bool bWasBusy = State != EFVFishState::Idle;
	State = EFVFishState::Idle;
	I.StateTime = 0.f;
	if (bWasBusy && Game)
	{
		FVScene& S = Game->GetScene();
		S.SetHeldSprite(nullptr);
		S.SetPlayerAnim(EFVCharAnim::Idle, true);
	}
}

void FVFishing::DebugForceBite()
{
	if (State == EFVFishState::Waiting) { Impl->BiteDelay = 0.f; }
}

void FVFishing::DebugForceBite(FName FishId, bool bApplyGates)
{
	if (State != EFVFishState::Waiting) { return; }
	Impl->ForcedFish = FishId;
	Impl->bForcedGates = bApplyGates;
	Impl->BiteDelay = 0.f;
}

void FVFishing::StartLegendTrial(FName FishId)
{
	const FFVFishDef* F = FishId.IsNone() ? nullptr : FVDB::FindFish(FishId);
	Impl->TrialFish = (F && F->bLegendary) ? FishId : NAME_None;
	if (State == EFVFishState::Waiting && !Impl->TrialFish.IsNone())
	{
		Impl->BiteDelay = FMath::Min(Impl->BiteDelay, Impl->StateTime + 1.f);   // the bobber is already out: bite soon
	}
}

FName FVFishing::GetPendingTrial() const
{
	return Impl->TrialFish;
}

void FVFishing::GetTrialScore(FName FishId, int32& OutTries, int32& OutWins) const
{
	const FIntPoint Sc = Impl->TrialScore.FindRef(FishId);
	OutTries = Sc.X;
	OutWins = Sc.Y;
}

FString FVFishing::TrialSetupText(FName FishId)
{
	const FVTrialImpl::FSetup* S = FVTrialImpl::Find(FishId);
	if (!S) { return FString(); }
	const FFVRodDef* Rod = FVDB::FindRod(FName(S->Rod));
	return FString::Printf(TEXT("ур. %d · %s · %s · %s"), S->Level, Rod ? *Rod->Name : S->Rod, *FVDB::ItemName(FName(S->Line)), *FVDB::ItemName(FName(S->Float)));
}

bool FVFishing::IsLegendIntro() const
{
	return State == EFVFishState::Minigame && Impl->IntroT > 0.f;
}

void FVFishing::DebugPinProgress(float P)
{
	Impl->PinnedProgress = P < 0.f ? -1.f : FMath::Clamp(P, 0.01f, 0.99f);
	if (P >= 0.f && State == EFVFishState::Minigame) { Mini.Progress = Impl->PinnedProgress; }
}

FFVRect FVFishing::GetGiveUpRect() const
{
	const FImpl& I = *Impl;
	return (State == EFVFishState::Minigame && I.bPractice && Mini.bEmptied) ? I.GiveUpRect : FFVRect();
}

bool FVFishing::IsUiAt(const FVector2D& P) const
{
	const FFVRect R = GetGiveUpRect();
	if (R.W > 0.f && R.Inset(-6.f).Contains(P)) { return true; }
	const FFVRect X = IsExpertFight() ? Impl->ExGiveUp : FFVRect();   // update 6: the training fight's [X]
	return X.W > 0.f && X.Inset(-6.f).Contains(P);
}

void FVFishing::Tick(float Dt, const FFVInput& In)
{
	if (!Game) { return; }
	FImpl& I = *Impl;
	UFVSaveGame* Profile = Game->GetProfile();
	if (!Profile) { return; }
	FVScene& Scene = Game->GetScene();
	I.SyncCanvas();
	Dt = FMath::Clamp(Dt, 0.f, 0.1f);
	I.StateTime += Dt;
	I.Time += Dt;
	I.BannerAge += Dt;

	bool bDown = In.bActionDown;
	bool bPressed = In.bActionPressed;
	bool bReleased = In.bActionReleased;
	if (bAutoPlay)
	{
		const bool bHold = I.BotHold(Scene, *Profile);
		bPressed = bHold && !I.bBotPrevHold;
		bReleased = !bHold && I.bBotPrevHold;
		bDown = bHold;
		I.bBotPrevHold = bHold;
	}
	const bool bEsc = In.WasPressed(EKeys::Escape);

	switch (State)
	{
	case EFVFishState::Idle:
	{
		if (!bPressed) { break; }
		if (!Scene.HasWater())
		{
			// Update 4b: the yard has the practice pond (no keepnet / bait needed there).
			if (Scene.IsHome() && Scene.CanFishFromHere())
			{
				I.StartCharging(true);
				break;
			}
			// Home (house / away from the pond): only an explicit action press explains it.
			if (I.Time - I.LastAwayToast > 4.f)
			{
				I.LastAwayToast = I.Time;
				Game->Toast(Scene.IsHome() ? TEXT("Порыбачить можно с мостков учебного пруда") : TEXT("Здесь не порыбачишь — идите к воде"), FVPal::White(), EFVIcon::Fish);
			}
			break;
		}
		if (!Scene.CanFishFromHere())
		{
			if (In.WasPressed(EKeys::SpaceBar) && I.Time - I.LastAwayToast > 4.f)
			{
				I.LastAwayToast = I.Time;
				Game->Toast(TEXT("Подойдите к воде или выйдите на причал"), FVPal::White(), EFVIcon::Fish);
			}
			break;
		}
		if (Profile->KeepnetFull())
		{
			// Fish only: a full backpack never blocks fishing (junk is simply thrown away then).
			Game->Toast(TEXT("Садок полон — продайте улов в лавке или уберите рыбу в ледник"), FVPal::Red(), EFVIcon::Keepnet);
			Sfx(EFVSfx::Error);
			break;
		}
		I.LegendBaitCheck();   // update 6 (§10.9): the checklist of a legend bait — known facts only, never blocks
		I.StartCharging();
		break;
	}
	case EFVFishState::Charging:
	{
		if (bEsc) { I.ToIdle(); break; }
		I.ChargeTime += Dt;
		const float Ph = FMath::Fmod(I.ChargeTime, 2.f);
		I.Power = Ph < 1.f ? Ph : 2.f - Ph;
		if (!bDown || bReleased) { I.DoCast(); }
		break;
	}
	case EFVFishState::Casting:
	{
		const float F = FMath::Clamp(I.StateTime / FMath::Max(0.05f, I.CastDuration), 0.f, 1.f);
		const float Arc = FMath::Sin(F * UE_PI) * (36.f + 60.f * I.CastPower);
		I.Bobber = FMath::Lerp(I.CastFrom, I.CastTo, F) - FVector2D(0.f, Arc);
		if (F >= 1.f) { I.Land(); }
		break;
	}
	case EFVFishState::Waiting:
	{
		if (bEsc || bPressed) { I.StartRetrieve(); break; }
		I.NibbleTime = FMath::Max(0.f, I.NibbleTime - Dt);
		I.NextNibble -= Dt;
		if (I.NextNibble <= 0.f)
		{
			I.NextNibble = I.Rng.FRandRange(1.2f, 3.5f);
			if (I.StateTime < I.BiteDelay - 0.6f)
			{
				I.NibbleTime = 0.22f;
				Scene.SpawnRipple(I.Bobber + FVector2D(0.f, 2.f), 0.45f);
				Sfx(EFVSfx::Splash, 0.08f, 1.8f);
			}
		}
		I.NextRipple -= Dt;
		if (I.NextRipple <= 0.f)
		{
			I.NextRipple = I.Rng.FRandRange(1.8f, 2.8f);
			Scene.SpawnRipple(I.Bobber + FVector2D(0.f, 2.f), 0.35f);
		}
		if (I.StateTime >= I.BiteDelay) { I.Bite(); }
		break;
	}
	case EFVFishState::Bite:
	{
		if (!I.Fish && !I.bPractice)
		{
			if (I.StateTime >= 0.35f) { I.FinishJunk(); }   // junk auto-hooks, no minigame
			break;
		}
		// update 5: the Angler's «Огонёк» — presses are ignored until the «!» (no penalty, the line stays)
		if (I.StateTime < I.GlowTime) { break; }
		if (!I.bGlowDone)
		{
			I.bGlowDone = true;
			Sfx(EFVSfx::Bite);
			Scene.SpawnSplash(I.Bobber, 0.5f);
		}
		if (bPressed) { I.Hook(); break; }
		if (I.StateTime >= I.ReactWindow)
		{
			Scene.SpawnFloatingText(Scene.GetHeadTop() - FVector2D(0.f, 10.f), I.bPractice ? FString(FVFishingPracticeImpl::TryAgainText()) : FString(TEXT("Рыба ушла…")), FV::Col(255, 210, 150));
			Sfx(EFVSfx::Escape, 0.45f, 1.1f);
			I.StartRetrieve();
		}
		break;
	}
	case EFVFishState::Minigame:
	{
		if (I.ExMode == FImpl::EExMode::Fight || I.ExMode == FImpl::EExMode::LegendOnExpert)
		{
			I.TickExpert(Dt, In, bAutoPlay);   // update 6: the expert field (§10.2–10.12)
			break;
		}
		if (I.bGiveUpClicked)
		{
			// 0.4.1: the practice fight is given up with the [X] — the same friendly «try again» as a lost fish
			I.bGiveUpClicked = false;
			if (I.bPractice && Mini.bEmptied)
			{
				UE_LOG(LogFV, Log, TEXT("FV: practice fish: the player gave up (%.1fs)"), Mini.Elapsed);
				I.Escape();
				break;
			}
		}
		if (I.IntroT > 0.f)
		{
			// update 5: «Легенда на крючке!» — the fish, the bar and the meter stand still (the minigame is not stepped);
			// a weak line snaps the leader right at the end of it
			I.IntroT = FMath::Max(0.f, I.IntroT - Dt);
			I.Accum = 0.f;
			I.ReelAngle += Dt * 4.f;
			if (I.IntroT <= 0.f && I.bPendingSnap)
			{
				I.bPendingSnap = false;
				I.SnapLeader();
			}
			break;
		}
		I.Accum += Dt;
		FFVMinigame::EResult R = FFVMinigame::EResult::Running;
		int32 Steps = 0;
		while (I.Accum >= FV::SimDt && Steps < 8)
		{
			I.Accum -= FV::SimDt;
			++Steps;
			R = Mini.Step(bDown);
			if (I.PinnedProgress >= 0.f)
			{
				Mini.Progress = I.PinnedProgress;   // autotest: the fight goes on as long as the meter is pinned
				R = FFVMinigame::EResult::Running;
			}
			if (R != FFVMinigame::EResult::Running) { break; }
		}
		if (Steps >= 8) { I.Accum = 0.f; }
		if (I.ExMode == FImpl::EExMode::ExpertOnClassic)
		{
			// Р25: the bar cannot hold an expert fish — the meter only falls, then the line gives way
			Mini.Progress = FMath::Min(Mini.Progress, I.FakeCap());
			if (I.StateTime >= I.FakeLen || R != FFVMinigame::EResult::Running)
			{
				I.EndExpertOnClassic();
				break;
			}
			R = FFVMinigame::EResult::Running;
		}
		if (Mini.HasLegendRules()) { I.LegendFeedback(); }
		I.ReelAngle += Dt * (Mini.bFishInBar ? 14.f : 6.f) * (bDown ? 1.f : 0.6f);
		if (Mini.bTreasureCaught && !I.bTreasureWasCaught)
		{
			I.bTreasureWasCaught = true;
			I.TreasurePop = 1.f;
			Sfx(EFVSfx::Treasure, 0.7f, 1.1f);
		}
		I.TreasurePop = FMath::Max(0.f, I.TreasurePop - Dt);
		Reeling(true, Mini.bFishInBar ? 1.f : 0.35f);
		I.MiniRipple -= Dt;
		if (I.MiniRipple <= 0.f)
		{
			I.MiniRipple = FMath::Abs(Mini.FishSpeed) > 3.f ? 0.18f : 0.4f;
			Scene.SpawnRipple(I.LineEnd(), FMath::Abs(Mini.FishSpeed) > 3.f ? 0.7f : 0.45f);
			if (FMath::Abs(Mini.FishSpeed) > 4.5f && I.Rng.FRand() < 0.35f) { Scene.SpawnSplash(I.LineEnd(), 0.4f); }
		}
		if (I.bPractice && Mini.bEmptied && !I.bEmptyHintShown)
		{
			I.bEmptyHintShown = true;
			Scene.SpawnFloatingText(Scene.GetHeadTop() - FVector2D(0.f, 10.f), FVFishingPracticeImpl::StillHookedText(), FV::Col(255, 222, 150));
		}
		if (R == FFVMinigame::EResult::Caught)
		{
			if (I.bPractice) { I.FinishPractice(); }
			else if (I.bTrial) { I.FinishTrial(); }
			else { I.FinishFish(); }
		}
		else if (R == FFVMinigame::EResult::Escaped) { I.Escape(); }
		break;
	}
	case EFVFishState::Showcase:
	{
		I.PerfectFlash = FMath::Max(0.f, I.PerfectFlash - Dt);
		if (I.StateTime > 0.4f && (bPressed || (!bAutoPlay && In.bLmbPressed) || bEsc))
		{
			if (I.bPractice) { I.ReleasePracticeFish(); }
			I.ToIdle();
		}
		break;
	}
	case EFVFishState::Escaped:
	{
		if (I.StateTime >= 0.8f) { I.StartRetrieve(); }
		break;
	}
	case EFVFishState::Retrieving:
	{
		const float F = FMath::Clamp(I.StateTime / FMath::Max(0.05f, I.RetrieveDuration), 0.f, 1.f);
		const FVector2D Tip = Scene.GetRodTip();
		const float E = F * F;
		I.Bobber = FMath::Lerp(I.RetrieveFrom, Tip, E) - FVector2D(0.f, FMath::Sin(F * UE_PI) * 10.f);
		if (F >= 1.f) { I.ToIdle(); }
		break;
	}
	default:
		I.ToIdle();
		break;
	}
}

// =============================================================================================
// Drawing — world space (line & bobber)
// =============================================================================================
void FVFishing::DrawWorld(FVDraw& D)
{
	if (!Game) { return; }
	FImpl& I = *Impl;
	I.SyncCanvas();
	I.LanternGlow = 0.f;
	const bool bLine = I.bBobberVisible || State == EFVFishState::Minigame || State == EFVFishState::Bite || State == EFVFishState::Waiting;
	if (!bLine || State == EFVFishState::Idle || State == EFVFishState::Showcase || State == EFVFishState::Charging) { return; }
	FVScene& Scene = Game->GetScene();
	UFVSaveGame* Profile = Game->GetProfile();
	const FFVLocationDef* Loc = FVDB::FindLocation(Scene.GetLocationId());
	const float T = I.Time;

	// ---------------------------------------------------------------- bobber position
	FVector2D Pos = I.LineEnd();
	bool bInWater = false;
	switch (State)
	{
	case EFVFishState::Waiting:
		Pos.Y += (FMath::Sin(T * 3.f) > 0.f ? 0.f : 2.f) + (I.NibbleTime > 0.f ? 3.f : 0.f);
		bInWater = true;
		break;
	case EFVFishState::Bite:
		if (I.StateTime < I.GlowTime)
		{
			// update 5: the Angler's «Огонёк» — the bobber rests, only the light below it grows
			Pos.Y += FMath::Sin(T * 3.f) > 0.f ? 0.f : 2.f;
			bInWater = true;
			break;
		}
		Pos.Y += 4.f + Snap(FMath::Sin(T * 34.f) * 1.5f);
		Pos.X += Snap(FMath::Sin(T * 21.f) * 1.5f);
		bInWater = true;
		break;
	case EFVFishState::Minigame:
	case EFVFishState::Escaped:
		bInWater = true;
		break;
	default: break;
	}

	// ---------------------------------------------------------------- line with a slight sag
	const FVector2D Tip = Scene.GetRodTip();
	const float Dist = float(FVector2D::Distance(Tip, Pos));
	float SagK = 0.05f;
	if (State == EFVFishState::Waiting || State == EFVFishState::Bite) { SagK = 0.12f; }
	else if (State == EFVFishState::Minigame) { SagK = 0.015f; }
	else if (State == EFVFishState::Escaped) { SagK = 0.2f; }
	const FVector2D Ctrl = (Tip + Pos) * 0.5f + FVector2D(0.f, Dist * SagK);
	const FLinearColor LineC = FV::Col(238, 238, 228, 200);
	constexpr int32 Segs = 10;
	FVector2D Prev = Tip;
	for (int32 k = 1; k <= Segs; ++k)
	{
		const float U = float(k) / float(Segs);
		const FVector2D P = Tip * ((1.f - U) * (1.f - U)) + Ctrl * (2.f * (1.f - U) * U) + Pos * (U * U);
		D.Line(Prev, P, LineC, 1.f);
		Prev = P;
	}

	// ---------------------------------------------------------------- bobber
	UFVSprites* Sp = UFVSprites::Get();
	const EFVBobberStyle Style = Profile ? Profile->GetBobberStyle() : EFVBobberStyle::Classic;
	const FLinearColor Water = FV::Col(Loc ? Loc->WaterShallow : FColor(90, 170, 220));
	if (State == EFVFishState::Minigame || State == EFVFishState::Escaped)
	{
		// submerged: the line disappears into a ring of disturbed water
		D.Circle(Pos, 3.f, WithAlpha(FMath::Lerp(Water, FLinearColor::Black, 0.35f), 0.7f));
		D.Rect(FFVRect(Snap(Pos.X) - 9.f, Snap(Pos.Y), 4.f, 2.f), FV::Col(255, 255, 255, 150));
		D.Rect(FFVRect(Snap(Pos.X) + 5.f, Snap(Pos.Y), 4.f, 2.f), FV::Col(255, 255, 255, 150));
		return;
	}
	if (State == EFVFishState::Casting)
	{
		// shadow on the water/ground under the flying bobber
		const float F = FMath::Clamp(I.StateTime / FMath::Max(0.05f, I.CastDuration), 0.f, 1.f);
		const FVector2D Ground = FMath::Lerp(I.CastFrom, I.CastTo, F);
		D.Circle(Ground + FVector2D(0.f, 2.f), 3.f, FV::Col(10, 20, 40, 70));
	}
	if (!Sp) { return; }
	const int32 Frame = (State == EFVFishState::Waiting || State == EFVFishState::Bite) ? (FMath::Frac(T * 1.5f) < 0.5f ? 0 : 1) : 0;
	const FFVSprite& B = Sp->Bobber(Style, Frame);
	if (!B.IsValid()) { return; }
	const FVector2D Center(Snap(Pos.X), Snap(Pos.Y) - 4.f);
	if (State == EFVFishState::Bite && I.GlowTime > 0.f)
	{
		// update 5: «Огонёк» of the Angler under the bobber (it grows for 1 s, then keeps pulsing until the hook)
		const float F = FMath::Clamp(I.StateTime / I.GlowTime, 0.f, 1.f);
		const float Pulse = F < 1.f ? F : 0.8f + 0.2f * FMath::Sin(T * 9.f);
		const FVector2D G(Center.X, Snap(Pos.Y) + 5.f);
		D.Glow(G, 6.f + 12.f * Pulse, WithAlpha(FV::Col(255, 230, 150), 0.25f + 0.55f * Pulse));
		D.Rect(FFVRect(G.X - 1.f, G.Y - 1.f, 2.f, 2.f), WithAlpha(FV::Col(255, 250, 210), 0.3f + 0.7f * Pulse));
	}
	D.SpriteCentered(B, Center, FV::WorldScale);
	if (Style == EFVBobberStyle::Lantern)
	{
		// the lamp is texel (6, 3) of the 8x8 sprite; brighter on the sprite's bright frame and at night
		const float Night = FMath::Clamp(Game->GetNightFactor(), 0.f, 1.f);
		I.LanternPos = Center + FVector2D(2.5f, -0.5f) * FV::WorldScale;
		I.LanternGlow = Night * (Frame == 0 ? 0.42f : 0.3f);
	}
	if (bInWater)
	{
		// lower part under the surface + a little ring
		const float Hw = B.W * FV::WorldScale * 0.5f;
		D.Rect(FFVRect(Center.X - Hw, Snap(Pos.Y), Hw * 2.f, 4.f), WithAlpha(Water, 0.65f));
		const float Ring = State == EFVFishState::Bite ? 12.f : 9.f;
		D.Rect(FFVRect(Center.X - Ring - 4.f, Snap(Pos.Y) + 1.f, 5.f, 2.f), FV::Col(255, 255, 255, 140));
		D.Rect(FFVRect(Center.X + Ring - 1.f, Snap(Pos.Y) + 1.f, 5.f, 2.f), FV::Col(255, 255, 255, 140));
	}
}

// =============================================================================================
// Update 5 (§2): the legend layers of the minigame panel — drawing only, the simulation never reads any of it.
// Order on the track: water, dark veil, Environment (current / night / fog / lava), the bar (+ BarExtras),
// Markers (glint, bubbles, lanterns, spark), the fish; outside the panel: Pictogram and Banner.
// =============================================================================================
namespace FVLegendDraw
{
	using namespace FVLegendFight;

	/** Four-ray star: colored rays Size texels across (odd, >= 7) and a 3x3 core. */
	void Star(FVDraw& D, const FVector2D& C, float Size, const FLinearColor& Ray, const FLinearColor& Core)
	{
		const float Hs = FMath::Max(3.f, FMath::Floor(Size * 0.5f));
		D.Rect(FFVRect(C.X - Hs, C.Y, Hs * 2.f + 1.f, 1.f), Ray);
		D.Rect(FFVRect(C.X, C.Y - Hs, 1.f, Hs * 2.f + 1.f), Ray);
		D.Rect(FFVRect(C.X - 1.f, C.Y - 1.f, 3.f, 3.f), Core);
	}

	/** Timer ring of dots: the first Frac of it (clockwise from the top) bright, the rest dim. */
	void TimerRing(FVDraw& D, const FVector2D& C, float R, float Frac, const FLinearColor& Col)
	{
		const int32 N = FMath::Clamp(FMath::RoundToInt(2.f * PI * R / 1.6f), 12, 64);
		for (int32 i = 0; i < N; ++i)
		{
			const float U = float(i) / float(N);
			const float A = -0.5f * PI + 2.f * PI * U;
			const FVector2D P(C.X + FMath::Cos(A) * R, C.Y + FMath::Sin(A) * R);
			D.Rect(FFVRect(P.X - 0.5f, P.Y - 0.5f, 1.f, 1.f), WithAlpha(Col, U < Frac ? Col.A : Col.A * 0.25f));
		}
	}

	/** Layer 3: the legend's surroundings (the caller clips to the track). */
	void Environment(FVDraw& D, const FFVMinigame& M, const FFVRect& Track, float S, float K, float T)
	{
		switch (M.Legend)
		{
		case EFVLegend::Sturgeon:
			if (M.HazardState == 1 || M.HazardState == 2)
			{
				// current chevrons: blinking 4 Hz while warning, running down while the current drags the bar
				const bool bRun = M.HazardState == 2;
				if (bRun || FMath::Frac(M.HazardAge * 4.f) < 0.6f)
				{
					const float Gap = Snap(22.f * K);
					const float Off = bRun ? FMath::Frac(T * 1.8f) * Gap : 0.f;
					const float Cx = Snap(Track.X + Track.W * 0.5f);
					const FLinearColor C = FV::Col(120, 220, 255, bRun ? 170 : 220);
					for (float Y = Track.Y - Gap + Off; Y < Track.Bottom() + Gap; Y += Gap)
					{
						D.Line(FVector2D(Cx - 4.f, Y - 3.f), FVector2D(Cx, Y + 1.f), C, 2.f);
						D.Line(FVector2D(Cx, Y + 1.f), FVector2D(Cx + 4.f, Y - 3.f), C, 2.f);
					}
				}
			}
			break;
		case EFVLegend::Angler:
			D.Rect(Track, FV::Col(0, 0, 20, 70));   // the night veil
			break;
		case EFVLegend::FogSerpent:
			// the fog is always visible (an environment, not a hazard): soft 6 px edges in three steps
			for (int32 k = 0; k < 2; ++k)
			{
				const float Y0 = Track.Y + M.FogPos[k] * S, H = FsFogH * S;
				const float E = FMath::Min(6.f, Snap(H * 0.25f));
				const float Step = FMath::Max(1.f, Snap(E / 3.f));
				D.Rect(FFVRect(Track.X, Snap(Y0 + E), Track.W, Snap(H - 2.f * E)), FV::Col(170, 200, 180, 140));
				for (int32 j = 0; j < 3; ++j)
				{
					const FLinearColor C = FV::Col(170, 200, 180, uint8(35 * (j + 1)));
					D.Rect(FFVRect(Track.X, Snap(Y0 + Step * float(j)), Track.W, Step), C);
					D.Rect(FFVRect(Track.X, Snap(Y0 + H - Step * float(j + 1)), Track.W, Step), C);
				}
			}
			break;
			case EFVLegend::MillMaster:
			// «Омут»: a dim green-brown depth; the whirl — curls running down both edges (fast in a surge)
			D.Rect(Track, FV::Col(22, 30, 16, 60));
			if (M.Phase != 2)
			{
				const bool bSurge = M.Phase == 0 && M.HazardState == 2;
				const bool bWarn = M.Phase == 0 && M.HazardState == 1 && FMath::Frac(M.HazardAge * 4.f) < 0.6f;
				const float Gap = Snap(26.f * K);
				const float Off = FMath::Frac(T * (bSurge ? 2.4f : 0.7f)) * Gap;
				const FLinearColor C = FV::Col(150, 205, 170, (bSurge || bWarn) ? 170 : 70);
				const float L = Snap(5.f * K);
				for (float Y = Track.Y - Gap + Off; Y < Track.Bottom() + Gap; Y += Gap)
				{
					D.Line(FVector2D(Track.X + 2.f, Y), FVector2D(Track.X + 2.f + L, Y + L * 0.8f), C, 1.f);
					D.Line(FVector2D(Track.Right() - 2.f, Y + Gap * 0.5f), FVector2D(Track.Right() - 2.f - L, Y + Gap * 0.5f + L * 0.8f), C, 1.f);
				}
			}
			else
			{
				// «Муть»: two silt clouds, soft edges in three steps (the fish still shows through the swirls)
				for (int32 k = 0; k < 2; ++k)
				{
					const float Y0 = Track.Y + M.FogPos[k] * S, H = MmSiltH * S;
					const float E = FMath::Min(6.f, Snap(H * 0.25f));
					const float Step = FMath::Max(1.f, Snap(E / 3.f));
					D.Rect(FFVRect(Track.X, Snap(Y0 + E), Track.W, Snap(H - 2.f * E)), FV::Col(112, 92, 58, 150));
					for (int32 j = 0; j < 3; ++j)
					{
						const FLinearColor C = FV::Col(112, 92, 58, uint8(38 * (j + 1)));
						D.Rect(FFVRect(Track.X, Snap(Y0 + Step * float(j)), Track.W, Step), C);
						D.Rect(FFVRect(Track.X, Snap(Y0 + H - Step * float(j + 1)), Track.W, Step), C);
					}
					// a few darker specks drifting inside the cloud
					for (int32 j = 0; j < 4; ++j)
					{
						const float U = FMath::Frac(T * 0.15f + float(j) * 0.27f + float(k) * 0.5f);
						const float X = Snap(Track.X + 2.f + (Track.W - 5.f) * FMath::Frac(float(j) * 0.37f + float(k) * 0.21f));
						D.Rect(FFVRect(X, Snap(Y0 + E + (H - 2.f * E - 2.f) * U), 2.f, 2.f), FV::Col(70, 56, 34, 170));
					}
				}
			}
			break;
		case EFVLegend::ZharRyba:
			if (M.HazardState == 1 || M.HazardState == 2)
			{
				// lava: a dark hatched band, not a flat orange (the gold bar must stay readable over it)
				const bool bErupt = M.HazardState == 2;
				const FFVRect Z(Track.X, Snap(Track.Y + M.HazardPos * S), Track.W, Snap(M.HazardH * S));
				D.Rect(Z, FV::Col(70, 20, 12, bErupt ? 170 : 100));
				D.PushClip(Z);
				const float Off = bErupt ? FMath::Frac(T * 3.f) * 4.f : 0.f;   // the hatch runs upwards while erupting
				const FLinearColor Hc = FV::Col(255, 110, 40, bErupt ? 210 : 120);
				for (float X = Z.X - Z.H - 4.f + Off; X < Z.Right() + 4.f; X += 4.f)
				{
					D.Line(FVector2D(X, Z.Bottom()), FVector2D(X + Z.H, Z.Y), Hc, 1.f);
				}
				D.PopClip();
				if (bErupt || FMath::Frac(M.HazardAge * 4.f) < 0.55f)
				{
					const FLinearColor Ec = FV::Col(255, 220, 120);
					D.Rect(FFVRect(Z.X, Z.Y, Z.W, 2.f), Ec);
					D.Rect(FFVRect(Z.X, Z.Bottom() - 2.f, Z.W, 2.f), Ec);
				}
				if (bErupt)
				{
					for (int32 j = 0; j < 5; ++j)   // flame tongues on both edges
					{
						const float X = Snap(Z.X + 2.f + (Z.W - 6.f) * float(j) / 4.f);
						const float Hf = Snap(2.f + 3.f * FMath::Abs(FMath::Sin(T * 13.f + float(j) * 1.9f)));
						D.Rect(FFVRect(X, Z.Y - Hf, 2.f, Hf), FV::Col(255, 170, 60, 220));
						D.Rect(FFVRect(X + 1.f, Z.Bottom(), 2.f, Snap(Hf * 0.7f)), FV::Col(255, 170, 60, 180));
					}
				}
			}
			break;
		default:
			break;
		}
	}

	/** Layer 4 extras: the hook shine, the sturgeon's foam, the burn flash. */
	void BarExtras(FVDraw& D, const FFVMinigame& M, const FFVRect& Bar, const FFVRect& Track, float ShineAge, bool bShine, float T)
	{
		if (bShine && ShineAge < BannerLife && Bar.H > 2.f)
		{
			// the shine runs over the gold bar only while «Легенда на крючке!» is shown
			D.PushClip(Bar);
			const float F = FMath::Frac(ShineAge / 0.75f);
			const float Yc = Bar.Y - Bar.W + F * (Bar.H + Bar.W * 2.f);
			D.Line(FVector2D(Bar.X - 2.f, Yc + Bar.W * 0.5f), FVector2D(Bar.Right() + 2.f, Yc - Bar.W * 0.5f), FV::Col(255, 250, 220, 190), 6.f);
			D.PopClip();
		}
		if (M.Legend == EFVLegend::Sturgeon && M.HazardState == 2)
		{
			// foam under the bar while the current drags it
			const float Y = FMath::Min(Bar.Bottom(), Track.Bottom() - 2.f);
			for (float X = Bar.X; X < Bar.Right(); X += 3.f)
			{
				if (FMath::Frac(T * 6.f + X * 0.13f) < 0.7f) { D.Rect(FFVRect(X, Y, FMath::Min(2.f, Bar.Right() - X), 2.f), FV::Col(225, 250, 255, 210)); }
			}
		}
		if (M.BurnFlash > 0.f) { D.Frame(Bar, WithAlpha(FV::Col(255, 60, 40), M.BurnFlash), 2.f); }
	}

	/** Layer 5: what gives the fish away and what it goes for. */
	void Markers(FVDraw& D, const FFVMinigame& M, const FFVRect& Track, float S, float K)
	{
		const float Cx = Snap(Track.X + Track.W * 0.5f);
		const float Half = FFVMinigame::FishIconH * 0.5f;
		const float StarS = FMath::Max(7.f, Snap(7.f * K));
		switch (M.Legend)
		{
		case EFVLegend::CrystalTrout:
			if (M.Glint > 0.01f)
			{
				Star(D, FVector2D(Cx, Snap(Track.Y + (M.SeenFishPos + Half) * S)), StarS, WithAlpha(FV::Col(160, 240, 255), M.Glint), WithAlpha(FLinearColor::White, M.Glint));
			}
			break;
		case EFVLegend::FogSerpent:
			for (int32 k = 0; k < 4; ++k)
			{
				const float Age = M.BubbleAge[k];
				if (Age >= FsBubbleLife) { continue; }
				const float F = Age / FsBubbleLife;
				const float A = F > 0.75f ? (1.f - F) / 0.25f : 1.f;
				const FVector2D C(Snap(Cx + FMath::Sin(Age * 9.f + float(k)) * 2.f), Snap(Track.Y + (M.BubbleY[k] + Half - FsBubbleRise * F) * S));
				D.Rect(FFVRect(C.X - 3.f, C.Y - 3.f, 6.f, 6.f), WithAlpha(FV::Col(20, 50, 40), A));
				D.Rect(FFVRect(C.X - 2.f, C.Y - 2.f, 4.f, 4.f), WithAlpha(FV::Col(150, 255, 190), A));
			}
			break;
		case EFVLegend::Angler:
			if (M.HazardState == 1 || M.HazardState == 2)
			{
				// the real lantern: warm ball, halo, a ring that closes before the lunge; the fake: red, flickering, no ring
				const bool bFake = M.bFakeLantern;
				const bool bOn = !bFake || FMath::Frac(M.HazardAge * AnFakeHz) < 0.5f;
				const FLinearColor Lc = bFake ? FV::Col(255, 70, 60) : FV::Col(255, 240, 170);
				const FVector2D L(Snap(Cx + 5.f * K), Snap(Track.Y + (M.HazardPos + Half) * S));
				if (M.HazardState == 1)
				{
					const FVector2D Head(Cx + 3.f * K, Track.Y + (M.FishPos + Half) * S - 7.f * K);
					D.Line(Head, L, bFake ? FV::Col(255, 120, 100, 110) : FV::Col(255, 240, 170, 120), 1.f);
				}
				D.Glow(L, 10.f * K, WithAlpha(Lc, bOn ? 0.55f : 0.2f));
				D.Rect(FFVRect(L.X - 3.f, L.Y - 3.f, 6.f, 6.f), WithAlpha(Lc, bOn ? 1.f : 0.35f));
				D.Rect(FFVRect(L.X - 1.f, L.Y - 2.f, 2.f, 1.f), WithAlpha(FLinearColor::White, bOn ? 0.9f : 0.3f));
				if (!bFake)
				{
					const float Fill = M.HazardState == 2 ? 1.f : FMath::Clamp(M.HazardAge / AnLantern, 0.f, 1.f);
					TimerRing(D, L, Snap(8.f * K), Fill, FV::Col(255, 240, 170));
				}
			}
			break;
		case EFVLegend::MillMaster:
			if ((M.Phase == 1 || M.Phase >= 3) && M.HazardState == 1)
			{
				// the knock of the millstones: dark ripples spread from the catfish
				const FVector2D C(Cx, Snap(Track.Y + (M.FishPos + Half) * S));
				for (int32 j = 0; j < 2; ++j)
				{
					const float F = FMath::Frac(M.HazardAge * 2.5f + float(j) * 0.5f);
					const float R = FMath::Lerp(5.f, 16.f, F) * K;
					D.Ring(C, R, R, WithAlpha(FV::Col(210, 190, 140), 0.85f * (1.f - F)), 1.f);
				}
			}
			break;
		case EFVLegend::ZharRyba:
			if (M.BlinkState == 1)
			{
				// the spark where it comes back, with a ring shrinking in 0.8 s; a flash where it vanished
				const FVector2D C(Cx, Snap(Track.Y + (M.BlinkTarget + Half) * S));
				const float F = 1.f - FMath::Clamp(M.BlinkTimer / ZhBlink, 0.f, 1.f);
				const float R = FMath::Lerp(14.f, 3.f, F) * K;
				D.Glow(C, 8.f * K, WithAlpha(FV::Col(255, 220, 120), 0.5f));
				D.Ring(C, R, R, FV::Col(255, 220, 120, 230), 1.f);
				Star(D, C, StarS, FV::Col(255, 255, 200), FLinearColor::White);
				if (F < 0.25f) { D.Glow(FVector2D(Cx, Track.Y + (M.FishPos + Half) * S), 12.f * K, WithAlpha(FV::Col(255, 180, 80), (0.25f - F) * 3.f)); }
			}
			break;
		default:
			break;
		}
	}

	/** The warning of the moment (at most one): its pictogram, its track height and color. */
	bool Warning(const FFVMinigame& M, EFVIcon& OutIcon, float& OutPos, FLinearColor& OutC)
	{
		const float Half = FFVMinigame::FishIconH * 0.5f;
		if (M.Legend == EFVLegend::ZharRyba && M.BlinkState == 1)
		{
			OutIcon = EFVIcon::WarnSpark; OutPos = M.BlinkTarget + Half; OutC = FV::Col(255, 255, 200);
			return true;
		}
		if (M.HazardState != 1) { return false; }
		switch (M.Legend)
		{
		case EFVLegend::Sturgeon: OutIcon = EFVIcon::WarnCurrent; OutPos = M.BarPos + M.BarHeight * 0.5f; OutC = FV::Col(120, 220, 255); return true;
		case EFVLegend::CrystalTrout: OutIcon = EFVIcon::WarnEye; OutPos = M.FishPos + Half; OutC = FV::Col(160, 240, 255); return true;
		case EFVLegend::Angler:
			if (M.bFakeLantern) { return false; }   // the fake is no danger: red means «обман»
			OutIcon = EFVIcon::WarnLantern; OutPos = M.HazardPos + Half; OutC = FV::Col(255, 240, 170);
			return true;
		case EFVLegend::FogSerpent: OutIcon = EFVIcon::WarnCloud; OutPos = M.HazardPos; OutC = FV::Col(170, 200, 180); return true;
		case EFVLegend::ZharRyba: OutIcon = EFVIcon::WarnFlame; OutPos = M.HazardPos + M.HazardH * 0.5f; OutC = FV::Col(255, 110, 40); return true;
		case EFVLegend::MillMaster:
			if (M.Phase == 0) { OutIcon = EFVIcon::WarnWhirl; OutPos = M.BarPos + M.BarHeight * 0.5f; OutC = FV::Col(150, 205, 170); return true; }
			if (M.Phase == 2) { return false; }   // the silt clouds are always visible: no pictogram
			OutIcon = EFVIcon::WarnDash; OutPos = M.FishPos + Half; OutC = FV::Col(210, 190, 140);
			return true;
		default: return false;
		}
	}

	/** Layer 8: the blinking 16 px pictogram outside the panel, at the height of the danger (far side when it fits). */
	void Pictogram(FVDraw& D, UFVSprites* Sp, const FFVMinigame& M, const FFVRect& P, const FFVRect& Track, float S, float K, bool bFarRight, float T)
	{
		EFVIcon Icon = EFVIcon::Exclaim;
		float Pos = 0.f;
		FLinearColor C = FLinearColor::White;
		if (!Warning(M, Icon, Pos, C) || FMath::Frac(T * 4.f) > 0.7f) { return; }
		const float Sz = Snap(18.f * K);
		const bool bRightRoom = FV::VW - P.Right() >= Sz + 3.f, bLeftRoom = P.X >= Sz + 3.f;
		const bool bRight = bFarRight ? (bRightRoom || !bLeftRoom) : (!bLeftRoom && bRightRoom);
		const float X = bRight ? P.Right() + 2.f : P.X - 2.f - Sz;
		const float Y = FMath::Clamp(Snap(Track.Y + Pos * S - Sz * 0.5f), P.Y, P.Bottom() - Sz);
		const FFVRect R(Snap(X), Y, Sz, Sz);
		D.Rect(R.Offset(1.f, 1.f), FV::Col(0, 0, 0, 90));
		D.Rect(R, FV::Col(24, 16, 10, 215));
		D.Frame(R, C, 1.f);
		if (Sp) { D.SpriteCentered(Sp->Icon(Icon), FVector2D(R.X + Sz * 0.5f, R.Y + Sz * 0.5f), K); }
	}

	/** Layer 8: a gold banner outside the panel (portrait: above it, or below; landscape: beside it, away from the player). */
	void Banner(FVDraw& D, const FString& Text, float Age, const FFVRect& P, float K, bool bPortrait, bool bFarRight)
	{
		if (Text.IsEmpty() || Age >= BannerLife) { return; }
		const float A = FMath::Min(1.f, Age / 0.15f) * FMath::Min(1.f, (BannerLife - Age) / 0.3f);
		const FLinearColor Gold = WithAlpha(FV::Col(255, 208, 64), A);
		const FLinearColor Ink = FV::Col(60, 30, 5, uint8(FMath::RoundToInt(A * 255.f)));
		if (bPortrait)
		{
			const float Size = FV::Ui::Body() + 1.f;
			const float LH = D.LineHeight(Size, true);
			const float W = D.MeasureText(Text, Size, true).X;
			const float Cx = FMath::Clamp(P.X + P.W * 0.5f, 4.f + W * 0.5f, FV::VW - 4.f - W * 0.5f);
			const bool bAbove = P.Y - (FV::TopBarH + 2.f) >= 22.f;
			const float Y = bAbove ? P.Y - LH - 3.f : P.Bottom() + 3.f;
			D.TextOutlined(Text, FVector2D(Snap(Cx), Snap(Y)), Size, Gold, EFVAlign::Center, Ink);
			return;
		}
		// landscape: in the top quarter of the panel, beyond the pictogram column, <= 2 lines of <= 96 px
		const float Column = Snap(18.f * K) + 6.f;
		const float RoomR = FV::VW - P.Right() - Column - 4.f, RoomL = P.X - Column - 4.f;
		const bool bRight = bFarRight ? (RoomR >= 60.f || RoomR >= RoomL) : !(RoomL >= 60.f || RoomL >= RoomR);
		const float MaxW = FMath::Clamp(bRight ? RoomR : RoomL, 30.f, 96.f);
		float Size = 9.f;
		TArray<FString> Lines = FVFishingPracticeImpl::WrapLines(D, Text, MaxW, Size, true);
		if (Lines.Num() > 2)
		{
			Size = 8.f;
			Lines = FVFishingPracticeImpl::WrapLines(D, Text, MaxW, Size, true);
		}
		const float LH = D.LineHeight(Size, true);
		float Y = P.Y + P.H * 0.06f;
		for (const FString& Ln : Lines)
		{
			const float X = bRight ? P.Right() + Column : P.X - Column;
			D.TextOutlined(Ln, FVector2D(Snap(X), Snap(Y)), Size, Gold, bRight ? EFVAlign::Left : EFVAlign::Right, Ink);
			Y += LH;
		}
	}
}

// =============================================================================================
// Drawing — UI (power meter, "!", minigame panel, showcase card)
// =============================================================================================
void FVFishing::DrawUI(FVDraw& D)
{
	if (!Game) { return; }
	FImpl& I = *Impl;
	I.SyncCanvas();
	FVScene& Scene = Game->GetScene();
	UFVSprites* Sp = UFVSprites::Get();
	const float T = I.Time;
	const bool bPortrait = FV::IsPortrait();

	// ---------------------------------------------------------------- update 5: the lantern bobber at night
	if (I.LanternGlow > 0.01f)
	{
		D.Glow(Scene.WorldToScreen(I.LanternPos), 10.f, WithAlpha(FV::Col(255, 236, 140), I.LanternGlow));
	}

	// ---------------------------------------------------------------- power meter
	if (State == EFVFishState::Charging)
	{
		const FVector2D Head = Scene.WorldToScreen(Scene.GetHeadTop());
		// Portrait: a bigger meter (thumb-sized screens), still right above the head.
		const float W = bPortrait ? 76.f : 52.f, H = bPortrait ? 16.f : 12.f;
		FFVRect R(Snap(Head.X - W * 0.5f), Snap(Head.Y - (bPortrait ? 32.f : 26.f)), W, H);
		R.X = FMath::Clamp(R.X, 4.f, FV::VW - W - 4.f);
		R.Y = FMath::Max(R.Y, FV::TopBarH + 4.f);
		D.Rect(R.Offset(1.f, 2.f), FV::Col(0, 0, 0, 90));
		D.Rect(R, FVPal::WoodDark());
		D.Rect(R.Inset(1.f), FVPal::Wood());
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FVPal::WoodLight());
		const FFVRect Track = R.Inset(3.f);
		D.Rect(Track, FV::Col(48, 30, 22));
		const float Fill = Snap(Track.W * FMath::Clamp(I.Power, 0.f, 1.f));
		if (Fill > 0.f)
		{
			const FLinearColor C = MeterColor(I.Power);
			D.Rect(FFVRect(Track.X, Track.Y, Fill, Track.H), C);
			D.Rect(FFVRect(Track.X, Track.Y, Fill, 1.f), FMath::Lerp(C, FLinearColor::White, 0.45f));
		}
		for (int32 k = 1; k < 4; ++k) { D.Rect(FFVRect(Snap(Track.X + Track.W * float(k) / 4.f), Track.Bottom() - 2.f, 1.f, 2.f), FV::Col(255, 240, 210, 140)); }
	}

	// ---------------------------------------------------------------- bite "!"
	if (State == EFVFishState::Bite && Sp && I.StateTime >= I.GlowTime)   // update 5: no «!» during the Angler's «Огонёк»
	{
		const FVector2D Head = Scene.WorldToScreen(Scene.GetHeadTop());
		const float ExT = I.StateTime - I.GlowTime;
		const float Pop = ExT < 0.12f ? 1.f + (0.12f - ExT) * 5.f : 1.f;
		const float Bounce = FMath::Abs(FMath::Sin(T * 16.f)) * 3.f;
		const float ExScale = bPortrait ? 3.f : 2.f;
		const float Lift = bPortrait ? 30.f : 22.f;
		const FVector2D C(Snap(Head.X), Snap(FMath::Max(float(Head.Y) - Lift - Bounce, FV::TopBarH + 8.f * ExScale + 4.f)));
		const FFVSprite& Ex = Sp->Icon(EFVIcon::Exclaim);
		if (bPortrait) { D.Glow(C, 30.f, WithAlpha(FV::Col(255, 230, 120) * 0.5f, 0.6f)); }
		D.SpriteCentered(Ex, C + FVector2D(1.f, 2.f), ExScale * Pop, FV::Col(0, 0, 0, 90));
		D.SpriteCentered(Ex, C, ExScale * Pop);
	}

	// ---------------------------------------------------------------- minigame panel
	const bool bExpertField = State == EFVFishState::Minigame
		&& (I.ExMode == FImpl::EExMode::Fight || I.ExMode == FImpl::EExMode::LegendOnExpert);
	if (State == EFVFishState::Minigame && !bExpertField)
	{
		const FVector2D Player = Scene.WorldToScreen(Scene.GetPlayerPos());
		// Portrait: the whole panel is scaled up (K) to fit the taller screen; landscape keeps the classic 44 x 300.
		const float AreaTop = FV::TopBarH + 4.f, AreaBottom = FV::VH - FV::BottomBarH - 4.f;
		const float K = bPortrait ? FMath::Clamp((AreaBottom - AreaTop) / 300.f, 1.f, 1.2f) : 1.f;
		// update 5 (§2): a legend on the hook gets the gold bar and a wider panel (50 x 300, a 26 wide track)
		const bool bLegendHook = !I.bPractice && I.Fish && I.Fish->bLegendary;
		const bool bLegendRules = bLegendHook && Mini.HasLegendRules();
		const float PW = Snap((bLegendHook ? 50.f : 44.f) * K), PH = Snap(300.f * K);
		const float Intro = FMath::Clamp(I.StateTime / 0.18f, 0.f, 1.f);
		float PX = 0.f, PY = 0.f;
		if (bPortrait)
		{
			// Beside the player on the roomier side (never over him), vertically centered on his body and kept
			// between the top info strip and the bottom button dock.
			const float Gap = 40.f;
			const float RoomR = FV::VW - (float(Player.X) + Gap) - 4.f;
			const float RoomL = float(Player.X) - Gap - 16.f * K;
			const bool bRight = RoomR >= PW && (RoomR >= RoomL || RoomL < PW);
			PX = bRight ? float(Player.X) + Gap : float(Player.X) - Gap - PW;
			PX = FMath::Clamp(PX, 16.f * K, FV::VW - PW - 4.f);
			PY = FMath::Clamp(float(Player.Y) - 48.f - PH * 0.5f, AreaTop, FMath::Max(AreaTop, AreaBottom - PH)) + (1.f - Intro) * 24.f;
		}
		else
		{
			const bool bRight = Player.X + 38.f + PW + 8.f < FV::VW;
			PX = bRight ? float(Player.X) + 38.f : float(Player.X) - 38.f - PW;
			PX = FMath::Clamp(PX, 18.f, FV::VW - PW - 4.f);
			PY = FV::TopBarH + (FV::VH - FV::TopBarH - FV::BottomBarH - PH) * 0.5f + (1.f - Intro) * 24.f;
		}
		FVector2D ShakeOff = FVector2D::ZeroVector;
		if (Mini.Shake > 0.01f)
		{
			ShakeOff = FVector2D(FMath::RoundToFloat(I.Rng.FRandRange(-1.f, 1.f) * 1.5f * Mini.Shake), FMath::RoundToFloat(I.Rng.FRandRange(-1.f, 1.f) * 1.5f * Mini.Shake));
		}
		const FFVRect P(Snap(PX + ShakeOff.X), Snap(PY + ShakeOff.Y), PW, PH);

		// wooden frame
		D.Rect(P.Offset(2.f, 3.f), FV::Col(0, 0, 0, 90));
		D.Rect(P, FVPal::WoodDark());
		D.Rect(P.Inset(1.f), FVPal::Wood());
		D.Rect(FFVRect(P.X + 1.f, P.Y + 1.f, P.W - 2.f, 2.f), FVPal::WoodLight());
		D.Rect(FFVRect(P.X + 1.f, P.Y + 1.f, 2.f, P.H - 2.f), FVPal::WoodLight());
		D.Rect(FFVRect(P.X + 1.f, P.Bottom() - 3.f, P.W - 2.f, 2.f), FMath::Lerp(FVPal::WoodDark(), FVPal::Wood(), 0.4f));
		const float GrainX = bLegendHook ? 35.f : 29.f;
		for (float Gy = P.Y + 14.f * K; Gy < P.Bottom() - 6.f * K; Gy += 17.f * K) { D.Rect(FFVRect(Snap(P.X + GrainX * K), Snap(Gy), 2.f, 1.f), FVPal::WoodDark()); }
		// nails
		const float NailS = Snap(2.f * K);
		for (const FVector2D& N : { FVector2D(P.X + 4.f * K, P.Y + 4.f * K), FVector2D(P.Right() - 5.f * K, P.Y + 4.f * K), FVector2D(P.X + 4.f * K, P.Bottom() - 6.f * K), FVector2D(P.Right() - 5.f * K, P.Bottom() - 6.f * K) })
		{
			D.Rect(FFVRect(Snap(N.X), Snap(N.Y), NailS, NailS), FV::Col(120, 110, 100));
		}

		// water-colored track
		const FFVLocationDef* Loc = FVDB::FindLocation(Scene.GetLocationId());
		const FLinearColor Sh = FV::Col(Loc ? Loc->WaterShallow : FColor(90, 170, 220));
		const FLinearColor Dp = FV::Col(Loc ? Loc->WaterDeep : FColor(30, 80, 150));
		const FFVRect Track(Snap(P.X + 7.f * K), Snap(P.Y + 10.f * K), Snap((bLegendHook ? 26.f : 20.f) * K), Snap(P.H - 20.f * K));
		D.Rect(Track.Inset(-1.f), FVPal::WoodDark());
		constexpr int32 Bands = 8;
		for (int32 k = 0; k < Bands; ++k)
		{
			const float Y0 = Track.Y + Track.H * float(k) / float(Bands);
			const float Y1 = Track.Y + Track.H * float(k + 1) / float(Bands);
			D.Rect(FFVRect(Track.X, Snap(Y0), Track.W, Snap(Y1) - Snap(Y0)), FMath::Lerp(Sh, Dp, float(k) / float(Bands - 1)));
		}
		if (bLegendHook)
		{
			// update 5: the dark veil — gold reads over any water (the volcano's orange too); no decorative bubbles
			// (they would look like the serpent's)
			D.Rect(Track, FV::Col(0, 0, 0, 115));
		}
		else
		{
			for (int32 k = 0; k < 6; ++k)   // rising bubbles
			{
				const float Hs = FMath::Frac(FMath::Sin(float(k) * 91.7f) * 437.5f);
				const float Y = Track.Bottom() - FMath::Frac(T * (0.12f + Hs * 0.1f) + Hs) * Track.H;
				const float X = Track.X + 3.f + Hs * (Track.W - 6.f) + FMath::Sin(T * 3.f + float(k)) * 1.5f;
				D.Rect(FFVRect(Snap(X), Snap(Y), 2.f, 2.f), FV::Col(255, 255, 255, 80));
			}
		}
		const float S = Track.H / FV::TrackH;
		if (bLegendRules)
		{
			D.PushClip(Track);
			FVLegendDraw::Environment(D, Mini, Track, S, K, T);
			D.PopClip();
		}

		// catch bar (update 5: gold while a legend is on the hook — its brightness tells «inside / outside»)
		const FFVRect Bar(Track.X + 1.f, Snap(Track.Y + Mini.BarPos * S), Track.W - 2.f, Snap(Mini.BarHeight * S));
		if (bLegendHook)
		{
			const FLinearColor BarC = Mini.bFishInBar ? FV::Col(255, 208, 64, 230) : FV::Col(150, 110, 40, 140);
			const float EdgeA = Mini.bFishInBar ? 1.f : 0.6f;
			D.Rect(Bar, BarC);
			D.Rect(FFVRect(Bar.X, Bar.Y, Bar.W, 2.f), WithAlpha(FV::Col(255, 240, 160), EdgeA));
			D.Rect(FFVRect(Bar.X, Bar.Bottom() - 2.f, Bar.W, 2.f), WithAlpha(FV::Col(150, 100, 20), EdgeA * 0.9f));
			D.Rect(FFVRect(Bar.X + 2.f, Bar.Y + 3.f, 2.f, FMath::Max(0.f, Bar.H - 6.f)), FV::Col(255, 255, 255, Mini.bFishInBar ? 110 : 50));
			FVLegendDraw::BarExtras(D, Mini, Bar, Track, I.BannerAge, I.bBannerShine, T);
		}
		else
		{
			const FLinearColor BarC = Mini.bFishInBar ? FV::Col(136, 240, 92, 225) : FV::Col(96, 186, 70, 160);
			D.Rect(Bar, BarC);
			D.Rect(FFVRect(Bar.X, Bar.Y, Bar.W, 2.f), FMath::Lerp(BarC, FLinearColor::White, 0.45f));
			D.Rect(FFVRect(Bar.X, Bar.Bottom() - 2.f, Bar.W, 2.f), WithAlpha(FMath::Lerp(BarC, FLinearColor::Black, 0.35f), 0.9f));
			D.Rect(FFVRect(Bar.X + 2.f, Bar.Y + 3.f, 2.f, FMath::Max(0.f, Bar.H - 6.f)), FV::Col(255, 255, 255, Mini.bFishInBar ? 110 : 60));
		}

		// treasure chest with its own tiny meter
		if (Sp && Mini.bTreasureVisible)
		{
			const FVector2D C(Track.X + Track.W * 0.5f + 3.f * K, Snap(Track.Y + (Mini.TreasurePos + FFVMinigame::FishIconH * 0.5f) * S));
			const FVector2D Wob = Mini.bTreasureInBar ? FVector2D(FMath::RoundToFloat(FMath::Sin(T * 40.f)), 0.f) : FVector2D::ZeroVector;
			D.SpriteCentered(Sp->Chest(0), C + Wob, 0.75f * K);
			const FFVRect Tm(Snap(C.X - 7.f * K), Snap(C.Y + 7.f * K), Snap(14.f * K), Snap(3.f * K));
			D.Rect(Tm.Inset(-1.f), FVPal::WoodDark());
			D.Rect(Tm, FV::Col(50, 34, 26));
			D.Rect(FFVRect(Tm.X, Tm.Y, Snap(Tm.W * Mini.TreasureProgress), Tm.H), FVPal::Gold());
		}
		if (Sp && I.TreasurePop > 0.f)
		{
			const float F = 1.f - I.TreasurePop;
			const FVector2D C(Track.X + Track.W * 0.5f, Track.Y + (Mini.TreasurePos + 16.f) * S - F * 24.f * K);
			D.SpriteCentered(Sp->Chest(1), FVector2D(Snap(C.X), Snap(C.Y)), (1.f + F * 0.3f) * K, WithAlpha(FLinearColor::White, I.TreasurePop));
			D.Glow(C, 16.f * K, WithAlpha(FV::Col(255, 220, 120) * (0.6f * I.TreasurePop), I.TreasurePop));
		}

		if (bLegendRules) { FVLegendDraw::Markers(D, Mini, Track, S, K); }

		// fish icon
		if (Sp)
		{
			const float FishCY = Track.Y + (Mini.FishPos + FFVMinigame::FishIconH * 0.5f) * S;
			const float Ang = FMath::Clamp(Mini.FishSpeed * 5.f, -28.f, 28.f) + (FMath::Abs(Mini.FishSpeed) > 0.6f ? FMath::Sin(T * 18.f) * 6.f : 0.f);
			const FVector2D FC(Snap(Track.X + Track.W * 0.5f), Snap(FishCY));
			if (bLegendHook)
			{
				// update 5: never tinted — white with a dark outline and a small gold glow; only FishAlpha hides it
				const float A = FMath::Clamp(Mini.FishAlpha, 0.f, 1.f);
				if (A > 0.01f)
				{
					const FFVSprite& Icon = Sp->Icon(EFVIcon::Fish);
					D.Glow(FC, 10.f * K, WithAlpha(FV::Col(255, 200, 80), 0.35f * A));
					D.SpriteCentered(Icon, FC + FVector2D(1.f, 1.f), K, WithAlpha(FLinearColor::Black, 0.6f * A), false, Ang);
					D.SpriteCentered(Icon, FC, K, WithAlpha(FLinearColor::White, A), false, Ang);
				}
			}
			else
			{
				D.SpriteCentered(Sp->Icon(EFVIcon::Fish), FC, K, FLinearColor::White, false, Ang);
			}
		}

		// progress meter
		const FFVRect Meter(Snap(P.X + (bLegendHook ? 38.f : 32.f) * K), Track.Y, Snap(6.f * K), Track.H);
		D.Rect(Meter.Inset(-1.f), FVPal::WoodDark());
		D.Rect(Meter, FV::Col(50, 34, 26));
		const float Fill = Snap(Meter.H * Mini.Progress);
		const FLinearColor MC = MeterColor(Mini.Progress);
		D.Rect(FFVRect(Meter.X, Meter.Bottom() - Fill, Meter.W, Fill), MC);
		D.Rect(FFVRect(Meter.X, Meter.Bottom() - Fill, 1.f, Fill), FMath::Lerp(MC, FLinearColor::White, 0.4f));

		// spinning reel handle
		const FVector2D RC(P.X - 3.f * K, P.Bottom() - 28.f * K);
		D.Circle(RC, 10.f * K, FVPal::WoodDark());
		D.Circle(RC, 8.5f * K, FV::Col(176, 176, 186));
		D.Circle(RC, 6.f * K, FV::Col(140, 140, 152));
		D.Circle(RC, 2.5f * K, FV::Col(90, 90, 100));
		const FVector2D Arm = RC + FVector2D(FMath::Cos(I.ReelAngle), FMath::Sin(I.ReelAngle)) * 7.f * K;
		D.Line(RC, Arm, FV::Col(60, 60, 70), 2.f);
		D.Circle(Arm, 2.5f * K, FVPal::WoodLight());

		if (bLegendHook)
		{
			// update 5: the track frame pulses after a phase change (red-gold for the Zhar-ryba); outside the panel:
			// the warning pictogram and the banners, on the side away from the player
			if (Mini.PhaseFlash > 0.f)
			{
				const float A = FMath::Min(1.f, Mini.PhaseFlash) * (0.55f + 0.45f * FMath::Sin(T * 14.f));
				const bool bRed = Mini.Legend == EFVLegend::ZharRyba && FMath::Frac(T * 5.f) < 0.5f;
				const FLinearColor FC = bRed ? FV::Col(255, 70, 40) : FV::Col(255, 208, 64);
				D.Frame(Track.Inset(-1.f), WithAlpha(FC, A), 1.f);
				D.Frame(Track.Inset(-2.f), WithAlpha(FC, A * 0.5f), 1.f);
			}
			const bool bFarRight = P.X + P.W * 0.5f >= float(Player.X);
			if (bLegendRules) { FVLegendDraw::Pictogram(D, Sp, Mini, P, Track, S, K, bFarRight, T); }
			FVLegendDraw::Banner(D, I.BannerText, I.BannerAge, P, K, bPortrait, bFarRight);
		}

		// 0.4.1: the practice fight cannot be lost — once its meter has been empty, the [X] beside the top of the panel
		// (steady: not shaken with it; the reel handle is at the bottom-left) lets the player give up
		I.GiveUpRect = FFVRect();
		if (I.bPractice && Mini.bEmptied)
		{
			const float BS = bPortrait ? FVFishingPracticeImpl::GiveUpSizeP : FVFishingPracticeImpl::GiveUpSizeL;
			const float Gap = bPortrait ? 6.f : 4.f;
			const float Top = Snap(PY);
			FFVRect X;
			if (Snap(PX) + PW + Gap + BS <= FV::VW - 3.f) { X = FFVRect(Snap(PX) + PW + Gap, Top, BS, BS); }
			else if (Snap(PX) - Gap - BS >= 3.f) { X = FFVRect(Snap(PX) - Gap - BS, Top, BS, BS); }
			else { X = FFVRect(Snap(PX + (PW - BS) * 0.5f), FMath::Max(FV::TopBarH + 2.f, Top - Gap - BS), BS, BS); }
			I.GiveUpRect = X;
			if (D.CloseButton(X)) { I.bGiveUpClicked = true; }
			if (!FV::IsTouchUI() && D.Hover(X))
			{
				D.Tooltip(TEXT("Отпустить рыбку"), TEXT("Учебная рыбка не уплывёт: её можно дотянуть или отпустить и забросить снова."));
			}
		}
	}
	else
	{
		I.GiveUpRect = FFVRect();
	}

	// ---------------------------------------------------------------- update 6: the expert fight (§10.12)
	I.ExGiveUp = FFVRect();
	if (bExpertField)
	{
		const float HeroX = float(Scene.WorldToScreen(Scene.GetPlayerPos()).X);
		I.ExField = FVExpertDraw::FieldRect(HeroX);
		FVExpertDraw::Draw(D, I.Ex, I.ExTheme, I.ExField, T);
		if (I.bExGhostHand && I.ExHandT < FVExpertDraw::TutorialHandSec) { FVExpertDraw::DrawTutorialHand(D, I.Ex, I.ExField, I.ExHandT); }
		if (I.bExGhostHand || I.Ex.Setup.bTutorial) { FVExpertDraw::DrawHowTo(D, I.Ex.Setup.Rod.Type, I.ExField, I.ExHandT); }
		if (I.Ex.Setup.bTutorial)
		{
			I.ExGiveUp = FVExpertDraw::GiveUpRect(I.ExField);
			if (D.CloseButton(I.ExGiveUp)) { I.bExGiveUpClicked = true; }
			if (!FV::IsTouchUI() && D.Hover(I.ExGiveUp))
			{
				D.Tooltip(TEXT("Отпустить рыбу"), TEXT("Учебная рыба не уйдёт сама: её можно дотянуть или отпустить."));
			}
		}
		FVExpertDraw::DrawBanner(D, I.BannerText, I.BannerAge, FVLegendFight::BannerLife, I.ExField);
	}
	else
	{
		I.ExField = FFVRect();
	}

	// ---------------------------------------------------------------- update 4b: the practice card
	if (State == EFVFishState::Showcase && I.bHasResult && I.bPractice)
	{
		using namespace FVFishingPracticeImpl;
		const FVector2D Player = Scene.WorldToScreen(Scene.GetPlayerPos());
		const FVector2D Head = Scene.WorldToScreen(Scene.GetHeadTop());
		const float W = bPortrait ? FMath::Min(FV::VW - 24.f, 252.f) : 190.f;
		const float Pad = bPortrait ? 12.f : 9.f;
		const float InnerW = W - Pad * 2.f;
		const float TitleS = bPortrait ? FV::Ui::Header() : 11.f;
		const float MainS = bPortrait ? FV::Ui::Body() + 1.5f : 9.f;
		const float SubS = bPortrait ? FV::Ui::Small() + 0.5f : 7.f;
		const float HintS = bPortrait ? FV::Ui::Small() : 6.f;
		const float IconScale = bPortrait ? 3.f : 2.f;
		const FString Title = TEXT("Учебная рыбка");
		const FString Main = TEXT("Отлично! Теперь вы умеете рыбачить");
		const FString Sub = FString::Printf(TEXT("%s Настоящая рыба ждёт на Деревенском пруду."),
			I.Result.bPerfect ? TEXT("Идеально — ни разу не упустили!") : TEXT("Рыбка отпущена обратно в пруд."));
		const TArray<FString> MainLines = WrapLines(D, Main, InnerW, MainS, true);
		const TArray<FString> SubLines = WrapLines(D, Sub, InnerW, SubS, false);
		const float TitleH = D.LineHeight(TitleS, true), MainLH = D.LineHeight(MainS, true), SubLH = D.LineHeight(SubS, false);
		const float IconBackH = 16.f * IconScale + 10.f;
		const float H = Pad + TitleH + 5.f + IconBackH + 7.f + MainLH * float(MainLines.Num()) + 6.f + SubLH * float(SubLines.Num()) + 8.f + HintS + 6.f + Pad;

		// place it like the catch card: beside the player (landscape), above the held fish or below the feet (portrait)
		const float HeldTop = float(Head.Y) - 16.f * FV::WorldScale - 10.f;
		float CX = 0.f, CY = 0.f;
		if (bPortrait)
		{
			const float AboveTop = FV::TopBarH + 6.f, AboveBottom = HeldTop - 8.f;
			const float BelowTop = float(Player.Y) + 12.f, BelowBottom = FV::VH - FV::BottomBarH - 6.f;
			CX = FMath::Clamp(float(Player.X) - W * 0.5f, 6.f, FV::VW - W - 6.f);
			if (AboveBottom - AboveTop >= H) { CY = AboveTop + (AboveBottom - AboveTop - H) * 0.7f; }
			else if (BelowBottom - BelowTop >= H) { CY = BelowTop + (BelowBottom - BelowTop - H) * 0.25f; }
			else { CY = FMath::Max(AboveTop, AboveBottom - H); }
		}
		else
		{
			const bool bCardRight = Player.X < FV::VW * 0.5f;
			CX = bCardRight ? float(Player.X) + 46.f : float(Player.X) - 46.f - W;
			CX = FMath::Clamp(CX, 6.f, FV::VW - W - 6.f);
			CY = FMath::Clamp(float(Player.Y) - H * 0.5f - 60.f, FV::TopBarH + 6.f, FV::VH - FV::BottomBarH - H - 4.f);
		}
		const float Slide = FMath::Clamp(I.StateTime / 0.2f, 0.f, 1.f);
		CY += Snap((1.f - Slide) * 12.f);
		const FFVRect Card(Snap(CX), Snap(CY), W, Snap(H));
		D.Rect(Card.Offset(2.f, 3.f), FV::Col(0, 0, 0, 80));
		D.Panel(Card, EFVPanel::Parchment);

		const float MidX = Card.X + W * 0.5f;
		float Y = Card.Y + Pad;
		// title with a little check mark
		{
			const float Tw = D.MeasureText(Title, TitleS, true).X;
			const bool bCheck = Sp && Sp->Icon(EFVIcon::Check).IsValid();
			const float CheckW = bCheck ? 16.f + 4.f : 0.f;
			const float TX = MidX - (Tw + CheckW) * 0.5f;
			if (bCheck) { D.SpriteCentered(Sp->Icon(EFVIcon::Check), FVector2D(Snap(TX + 8.f), Snap(Y + TitleH * 0.5f)), 1.f); }
			D.TextOutlined(Title, FVector2D(Snap(TX + CheckW), Y), TitleS, FV::Col(150, 230, 110), EFVAlign::Left);
		}
		Y += TitleH + 5.f;
		// the little fish on a soft band, with twinkles
		{
			const FFVRect Back(Card.X + Pad, Y, InnerW, IconBackH);
			D.Rect(Back, WithAlpha(FVPal::ParchDark(), 0.55f));
			D.Rect(FFVRect(Back.X, Back.Y, Back.W, 1.f), WithAlpha(FVPal::InkSoft(), 0.35f));
			const FFVSprite* Fish = Sp ? &Sp->Icon(EFVIcon::Fish) : nullptr;
			if (Fish && Fish->IsValid())
			{
				const float Bob = FMath::Sin(T * 2.5f) > 0.f ? 0.f : 1.f;
				D.SpriteCentered(*Fish, FVector2D(Snap(MidX), Snap(Back.Y + Back.H * 0.5f) + Bob), IconScale, FLinearColor::White, false, FMath::Sin(T * 3.f) * 6.f);
			}
			for (int32 k = 0; k < 3; ++k)
			{
				const float F = FMath::Frac(T * 0.7f + float(k) * 0.33f);
				if (F > 0.3f) { continue; }
				const float A = FMath::Sin(F / 0.3f * UE_PI);
				const FVector2D P(Snap(MidX + (k == 0 ? -1.f : 1.f) * (18.f + 10.f * float(k)) * (IconScale * 0.5f)), Snap(Back.Y + 5.f + float(k) * 6.f));
				D.Rect(FFVRect(P.X - 1.f, P.Y - 3.f, 2.f, 6.f), WithAlpha(FVPal::Gold(), A));
				D.Rect(FFVRect(P.X - 3.f, P.Y - 1.f, 6.f, 2.f), WithAlpha(FVPal::Gold(), A));
			}
		}
		Y += IconBackH + 7.f;
		for (const FString& Ln : MainLines)
		{
			D.Text(Ln, FVector2D(MidX, Y), MainS, FVPal::Ink(), EFVAlign::Center, false, true);
			Y += MainLH;
		}
		Y += 2.f;
		D.Rect(FFVRect(Card.X + Pad + 10.f, Y, InnerW - 20.f, 1.f), WithAlpha(FVPal::InkSoft(), 0.3f));
		Y += 4.f;
		for (const FString& Ln : SubLines)
		{
			D.Text(Ln, FVector2D(MidX, Y), SubS, FVPal::InkSoft(), EFVAlign::Center, false, false);
			Y += SubLH;
		}
		const float HintA = I.StateTime > 0.4f ? 0.55f + 0.45f * FMath::Abs(FMath::Sin(T * 2.5f)) : 0.3f;
		const FString HintTxt = FV::IsTouchUI() ? TEXT("Коснитесь, чтобы продолжить") : TEXT("[Пробел / ЛКМ] — продолжить");
		D.Text(D.Ellipsize(HintTxt, InnerW, HintS, false), FVector2D(MidX, Card.Bottom() - Pad - HintS - 2.f), HintS, WithAlpha(FVPal::InkSoft(), HintA), EFVAlign::Center, false, false);
	}

	// ---------------------------------------------------------------- perfect flash & showcase card
	if (State == EFVFishState::Showcase && I.bHasResult && !I.bPractice)
	{
		const FFVCatchResult& R = I.Result;
		const FVector2D Player = Scene.WorldToScreen(Scene.GetPlayerPos());
		const FVector2D Head = Scene.WorldToScreen(Scene.GetHeadTop());

		// ---- card content
		const bool bFish = R.Fish != nullptr;
		const FFVSprite* Icon = nullptr;
		if (Sp)
		{
			if (R.Fish) { Icon = &Sp->Fish(*R.Fish); }
			else if (R.Junk) { Icon = &Sp->Junk(R.Junk->Style); }
		}
		const bool bIcon = Icon && Icon->IsValid();
		const FString Name = R.Fish ? R.Fish->Name : (R.Junk ? R.Junk->Name : FString(TEXT("???")));
		const FString Sub = R.Fish ? FVDB::RarityName(R.Fish->Rarity) : FString(TEXT("Мусор"));
		const FLinearColor NameC = R.Fish ? FVDB::RarityColor(R.Fish->Rarity) : FV::Col(200, 190, 170);

		struct FBadge { FString Text; FLinearColor C; };
		TArray<FBadge, TInlineAllocator<6>> Badges;
		if (R.bPerfect) { Badges.Add({ TEXT("Идеальный улов!"), FVPal::Purple() }); }
		if (R.bNewSpecies) { Badges.Add({ TEXT("Новый вид!"), FV::Col(214, 150, 20) }); }
		if (R.bNewRecord && !R.bNewSpecies) { Badges.Add({ TEXT("Рекорд!"), FVPal::Blue() }); }
		if (R.bTreasure)
		{
			TArray<FString> Parts;
			if (R.Treasure.Money > 0) { Parts.Add(FString::Printf(TEXT("%s монет"), *FV::Money(R.Treasure.Money))); }
			if (!R.Treasure.BaitId.IsNone() && R.Treasure.BaitCount > 0) { Parts.Add(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(R.Treasure.BaitId), R.Treasure.BaitCount)); }
			if (!R.Treasure.ItemId.IsNone() && R.Treasure.ItemCount > 0)
			{
				Parts.Add(R.Treasure.ItemCount > 1 ? FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(R.Treasure.ItemId), R.Treasure.ItemCount) : FVDB::ItemName(R.Treasure.ItemId));
			}
			if (!R.Treasure.CosmeticId.IsNone()) { Parts.Add(FVDB::ItemName(R.Treasure.CosmeticId)); }
			Badges.Add({ FString(TEXT("Сокровище: ")) + (Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("пусто"))), FV::Col(186, 104, 40) });
		}
		if (R.LevelsGained > 0) { Badges.Add({ TEXT("Новый уровень рыбалки!"), FVPal::Green() }); }
		if (R.Junk)
		{
			// Junk goes to the backpack (the workshop recycles it into materials).
			if (R.bStored) { Badges.Add({ TEXT("В рюкзак"), FV::Col(96, 130, 70) }); }
			else { Badges.Add({ TEXT("Рюкзак полон — мусор выброшен"), FVPal::Red() }); }
		}
		else if (!R.bStored) { Badges.Add({ TEXT("Садок полон — улов не поместился"), FVPal::Red() }); }

		// ---- card style: landscape = the classic compact card; portrait = FV::Ui sizes (a compact variant is used
		// when the full card does not fit above or below the player).
		struct FCardStyle
		{
			float W = 176.f, Pad = 8.f;
			float NameSize = 11.f, NameAdv = 15.f, SubSize = 7.f, SubAdv = 11.f;
			float IconMaxScale = 3.f, IconMaxH = 60.f, IconBackPad = 8.f, IconAdv = 12.f;
			float RowH = 11.f, LabelSize = 7.f, ValueSize = 8.f;
			float BadgeSize = 7.f, BadgeLineH = 9.f, BadgePad = 4.f;
			float HintSize = 6.f, HintAdv = 14.f;
		};
		auto MakeStyle = [bPortrait](int32 Level)
		{
			FCardStyle St;
			if (!bPortrait || Level >= 2) { return St; }
			St.W = FMath::Min(FV::VW - 20.f, 240.f);
			St.Pad = 10.f;
			St.NameSize = Level == 0 ? FV::Ui::Title() : FV::Ui::Header();
			St.NameAdv = Level == 0 ? 21.f : 17.f;
			St.SubSize = FV::Ui::Small();
			St.SubAdv = 13.f;
			St.IconMaxScale = Level == 0 ? 4.f : 2.f;
			St.IconMaxH = Level == 0 ? 64.f : 40.f;
			St.IconBackPad = 10.f;
			St.IconAdv = 14.f;
			St.RowH = Level == 0 ? 16.f : 14.f;
			St.LabelSize = FV::Ui::Small() + 0.5f;
			St.ValueSize = FV::Ui::Body();
			St.BadgeSize = FV::Ui::Small();
			St.BadgeLineH = 11.f;
			St.BadgePad = 5.f;
			St.HintSize = FV::Ui::Small();
			St.HintAdv = 17.f;
			return St;
		};
		struct FCardMetrics { float IconScale = 1.f; float IconH = 24.f; float H = 0.f; TArray<int32, TInlineAllocator<6>> BadgeLines; };
		const int32 InfoLines = bFish ? 4 : 2;
		auto Measure = [&D, &Badges, Icon, bIcon, InfoLines](const FCardStyle& St)
		{
			FCardMetrics M;
			const float InnerW = St.W - St.Pad * 2.f;
			if (bIcon)
			{
				M.IconScale = FMath::Min(St.IconMaxScale, FMath::Min((InnerW - 12.f) / FMath::Max(1.f, Icon->W), St.IconMaxH / FMath::Max(1.f, Icon->H)));
				M.IconScale = FMath::Max(1.f, FMath::FloorToFloat(M.IconScale));
				M.IconH = Icon->H * M.IconScale;
			}
			float BadgesH = 0.f;
			for (const FBadge& B : Badges)
			{
				const float Tw = D.MeasureText(B.Text, St.BadgeSize).X;
				const int32 Lines = FMath::Clamp(FMath::CeilToInt(Tw / (InnerW - 8.f)), 1, 3);
				M.BadgeLines.Add(Lines);
				BadgesH += St.BadgePad + St.BadgeLineH * float(Lines) + 2.f;
			}
			M.H = St.Pad + St.NameAdv + St.SubAdv + M.IconH + St.IconAdv + St.RowH * float(InfoLines) + (Badges.Num() > 0 ? 4.f + BadgesH : 0.f) + St.HintAdv + St.Pad;
			return M;
		};

		// Top of the fish held above the head (the card must never hide it, nor the player).
		const float HeldTop = float(Head.Y) - (HeldIconH(Sp, R)) * FV::WorldScale - 10.f;
		const float AboveTop = FV::TopBarH + 6.f, AboveBottom = HeldTop - 8.f;
		const float BelowTop = float(Player.Y) + 12.f, BelowBottom = FV::VH - FV::BottomBarH - 6.f;
		const float RoomAbove = AboveBottom - AboveTop, RoomBelow = BelowBottom - BelowTop;

		FCardStyle St = MakeStyle(0);
		FCardMetrics M = Measure(St);
		if (bPortrait)
		{
			for (int32 Level = 1; Level <= 2 && M.H > FMath::Max(RoomAbove, RoomBelow); ++Level)
			{
				St = MakeStyle(Level);
				M = Measure(St);
			}
		}
		const float W = St.W, Pad = St.Pad, H = M.H;
		const float InnerW = W - Pad * 2.f;

		float CX = 0.f, CY = 0.f;
		bool bCardAbove = true;
		if (bPortrait)
		{
			// Portrait: centered on the player, in the land area above the held fish when it fits there, else below
			// the feet (over the water); otherwise the roomier of the two.
			CX = FMath::Clamp(float(Player.X) - W * 0.5f, 6.f, FV::VW - W - 6.f);
			if (RoomAbove >= H) { CY = AboveTop + (RoomAbove - H) * 0.7f; }
			else if (RoomBelow >= H) { CY = BelowTop + (RoomBelow - H) * 0.25f; bCardAbove = false; }
			else if (RoomAbove >= RoomBelow) { CY = FMath::Max(AboveTop, AboveBottom - H); }
			else { CY = FMath::Max(AboveTop, FMath::Min(BelowTop, BelowBottom - H)); bCardAbove = false; }
		}
		else
		{
			const bool bCardRight = Player.X < FV::VW * 0.5f;
			CX = bCardRight ? float(Player.X) + 46.f : float(Player.X) - 46.f - W;
			CX = FMath::Clamp(CX, 6.f, FV::VW - W - 6.f);
			CY = FMath::Clamp(float(Player.Y) - H * 0.5f - 60.f, FV::TopBarH + 6.f, FV::VH - FV::BottomBarH - H - 4.f);
		}
		const float Slide = FMath::Clamp(I.StateTime / 0.2f, 0.f, 1.f);
		CY += Snap((1.f - Slide) * 12.f);
		const FFVRect Card(Snap(CX), Snap(CY), W, Snap(H));

		if (I.PerfectFlash > 0.f)
		{
			const float Age = 1.8f - I.PerfectFlash;
			const float Pop = Age < 0.15f ? 0.6f + Age / 0.15f * 0.5f : (Age < 0.3f ? 1.1f - (Age - 0.15f) / 0.15f * 0.1f : 1.f);
			const float A = FMath::Min(1.f, I.PerfectFlash / 0.4f);
			const FLinearColor Gold = (FMath::Frac(T * 6.f) < 0.5f) ? FVPal::Gold() : FV::Col(255, 250, 200);
			float FlashX = 0.f, Y = 0.f;
			if (bPortrait)
			{
				// Above the held fish when the card is below; under the feet when the card is above.
				FlashX = FMath::Clamp(float(Head.X), 64.f, FV::VW - 64.f);
				Y = bCardAbove ? float(Player.Y) + 12.f : HeldTop - 26.f;
				if (bCardAbove && Y > FV::VH - FV::BottomBarH - 24.f) { Y = Card.Bottom() + 4.f; }
				Y = FMath::Max(Y, FV::TopBarH + 4.f);
			}
			else
			{
				Y = FMath::Max(float(Head.Y) - 86.f, FV::TopBarH + 4.f);
				// Keep the flash on the opposite side of the catch card (the card sits beside the player).
				FlashX = FMath::Clamp(float(Head.X) + (Player.X < FV::VW * 0.5f ? -44.f : 44.f), 64.f, FV::VW - 64.f);
			}
			D.TextOutlined(TEXT("ИДЕАЛЬНО!"), FVector2D(Snap(FlashX), Snap(Y)), (bPortrait ? 17.f : 14.f) * Pop, WithAlpha(Gold, A), EFVAlign::Center, FV::Col(70, 30, 10, uint8(FMath::RoundToInt(A * 255.f))));
		}
		D.Rect(Card.Offset(2.f, 3.f), FV::Col(0, 0, 0, 80));
		D.Panel(Card, EFVPanel::Parchment);

		const float MidX = Card.X + W * 0.5f;
		float Y = Card.Y + Pad;
		{
			// Name in rarity color; long names shrink, then get an ellipsis (never overflow the card).
			float NS = St.NameSize;
			while (NS > 9.f && D.MeasureText(Name, NS).X > InnerW) { NS -= 1.f; }
			D.TextOutlined(D.Ellipsize(Name, InnerW, NS), FVector2D(MidX, Y), NS, NameC, EFVAlign::Center);
		}
		Y += St.NameAdv;
		D.Text(Sub, FVector2D(MidX, Y), St.SubSize, FVPal::InkSoft(), EFVAlign::Center);
		Y += St.SubAdv;
		{
			const FFVRect Back(Card.X + Pad, Y, InnerW, M.IconH + St.IconBackPad);
			D.Rect(Back, WithAlpha(FVPal::ParchDark(), 0.55f));
			D.Rect(FFVRect(Back.X, Back.Y, Back.W, 1.f), WithAlpha(FVPal::InkSoft(), 0.35f));
			if (bIcon)
			{
				const float Bob = FMath::Sin(T * 2.5f) > 0.f ? 0.f : 1.f;
				D.SpriteCentered(*Icon, FVector2D(Snap(MidX), Snap(Back.Y + Back.H * 0.5f) + Bob), M.IconScale);
			}
		}
		Y += M.IconH + St.IconAdv;

		const float LabelDY = bPortrait ? FMath::Max(0.f, (St.ValueSize - St.LabelSize) * 0.6f) : 0.f;
		auto InfoRow = [&D, &Card, Pad, &St, LabelDY](float RowY, const FString& Label)
		{
			D.Text(Label, FVector2D(Card.X + Pad + 2.f, RowY + LabelDY), St.LabelSize, FVPal::InkSoft(), EFVAlign::Left, false, false);
		};
		const float RightX = Card.Right() - Pad - 2.f;
		if (bFish)
		{
			InfoRow(Y, TEXT("Размер"));
			D.Text(FString::Printf(TEXT("%.1f см"), R.SizeCm).Replace(TEXT("."), TEXT(",")), FVector2D(RightX, Y), St.ValueSize, FVPal::Ink(), EFVAlign::Right);
			Y += St.RowH;
			InfoRow(Y, TEXT("Качество"));
			{
				const FString QN = FVDB::QualityName(R.Quality);
				const float Qw = D.MeasureText(QN, St.ValueSize).X;
				// Darker quality tints: the light star colors are unreadable on parchment.
				const FLinearColor QC = R.Quality == EFVQuality::Silver ? FV::Col(96, 112, 140)
					: R.Quality == EFVQuality::Gold ? FV::Col(184, 124, 0)
					: R.Quality == EFVQuality::Iridium ? FV::Col(150, 48, 190) : FVPal::Ink();
				D.Text(QN, FVector2D(RightX, Y), St.ValueSize, QC, EFVAlign::Right);
				const float StarScale = bPortrait ? 1.25f : 1.f;
				if (R.Quality != EFVQuality::Normal) { D.Stars(FVector2D(RightX - Qw - 3.f - 8.f * StarScale, Y + 1.f), R.Quality, StarScale); }
			}
			Y += St.RowH;
		}
		InfoRow(Y, TEXT("Цена"));
		D.MoneyLabel(FVector2D(RightX, Y), R.UnitPrice, St.ValueSize, EFVAlign::Right);
		Y += St.RowH;
		InfoRow(Y, TEXT("Опыт"));
		D.Text(FString::Printf(TEXT("+%d"), R.Xp), FVector2D(RightX, Y), St.ValueSize, FV::Col(60, 150, 40), EFVAlign::Right);
		Y += St.RowH;

		if (Badges.Num() > 0)
		{
			Y += 4.f;
			for (int32 k = 0; k < Badges.Num(); ++k)
			{
				const int32 Lines = M.BadgeLines.IsValidIndex(k) ? M.BadgeLines[k] : 1;
				const float BH = St.BadgePad + St.BadgeLineH * float(Lines);
				const FFVRect BR(Card.X + Pad, Y, InnerW, BH);
				D.Rect(BR, Badges[k].C);
				D.Rect(FFVRect(BR.X, BR.Y, BR.W, 1.f), FMath::Lerp(Badges[k].C, FLinearColor::White, 0.4f));
				D.Rect(FFVRect(BR.X, BR.Bottom() - 1.f, BR.W, 1.f), FMath::Lerp(Badges[k].C, FLinearColor::Black, 0.35f));
				const float TY = BR.Y + FMath::Floor(St.BadgePad * 0.5f);
				if (Lines <= 1) { D.Text(Badges[k].Text, FVector2D(MidX, TY), St.BadgeSize, FVPal::White(), EFVAlign::Center, true); }
				else { D.TextWrapped(Badges[k].Text, FVector2D(BR.X + 4.f, TY), BR.W - 8.f, St.BadgeSize, FVPal::White(), true, 1.05f); }
				Y += BH + 2.f;
			}
		}
		const float HintA = I.StateTime > 0.4f ? 0.55f + 0.45f * FMath::Abs(FMath::Sin(T * 2.5f)) : 0.3f;
		const FString HintTxt = FV::IsTouchUI() ? TEXT("Коснитесь, чтобы продолжить") : TEXT("[Пробел / ЛКМ] — продолжить");
		D.Text(D.Ellipsize(HintTxt, InnerW, St.HintSize, false), FVector2D(MidX, Card.Bottom() - Pad - St.HintSize - 2.f), St.HintSize, WithAlpha(FVPal::InkSoft(), HintA), EFVAlign::Center, false, false);
	}
}

FString FVFishing::GetHint() const
{
	switch (State)
	{
	case EFVFishState::Idle:
	{
		if (!Game) { return FString(); }
		const bool bTouchUI = FV::IsTouchUI();
		if (!Game->GetScene().HasWater())
		{
			// Update 4b: the practice pond of the yard.
			if (Game->GetScene().IsHome() && Game->GetScene().CanFishFromHere())
			{
				return bTouchUI ? TEXT("Учебный пруд: зажмите палец на месте — замах") : TEXT("Учебный пруд: удерживайте [Пробел / ЛКМ], чтобы забросить удочку");
			}
			// Home: no fishing hints at all (the interaction prompt of UFVGame takes the line when something is near).
			return bTouchUI ? TEXT("Свайп — идти · коснитесь предмета, чтобы им воспользоваться") : TEXT("[WASD] — ходить · [E] / ЛКМ — взаимодействовать · [M] — карта");
		}
		if (const UFVSaveGame* P = Game->GetProfile())
		{
			if (P->KeepnetFull()) { return bTouchUI ? TEXT("Садок полон — продайте улов в лавке") : TEXT("Садок полон — продайте улов в лавке или уберите в ледник · [I] — садок"); }
		}
		// Touch hints stay short: the portrait hint bar is only 360 px wide.
		if (Game->GetScene().CanFishFromHere()) { return bTouchUI ? TEXT("Зажмите палец на месте — замах · свайп — идти") : TEXT("Удерживайте [Пробел / ЛКМ], чтобы забросить удочку · [WASD] — ходить"); }
		return bTouchUI ? TEXT("Свайп — идти к воде или на причал") : TEXT("[WASD / стрелки] — ходить · подойдите к воде или выйдите на причал, чтобы рыбачить");
	}
	case EFVFishState::Charging: return FV::IsTouchUI() ? TEXT("Отпустите палец, чтобы забросить") : TEXT("Отпустите [Пробел / ЛКМ], чтобы забросить · [Esc] — отмена");
	case EFVFishState::Casting: return TEXT("Заброс…");
	case EFVFishState::Waiting: return FV::IsTouchUI() ? TEXT("Ждём поклёвку… · касание — смотать") : TEXT("Ждём поклёвку… · [Пробел / ЛКМ] — смотать леску · [Esc] — отмена");
	case EFVFishState::Bite:
		if (Impl->StateTime < Impl->GlowTime) { return TEXT("Огонёк… жди «!»"); }   // update 5: the Angler's pre-hook glow
		return (Impl->Fish || Impl->bPractice) ? FString(FV::IsTouchUI() ? TEXT("Клюёт! Коснитесь экрана!") : TEXT("Клюёт! Жмите [Пробел / ЛКМ]!")) : FString(TEXT("Что-то зацепилось…"));
	case EFVFishState::Minigame:
		if (IsExpertFight())
		{
			// update 6: the rod type's own control hint (§10.3)
			return FVDBU6::RodTypeHowTo(Impl->Ex.Setup.Rod.Type);
		}
		if (!Impl->bPractice && Impl->Fish && Impl->Fish->bLegendary)
		{
			// update 5 (§2): a legend is on the hook — the gold bar
			return FV::IsTouchUI() ? TEXT("Держите палец — полоса вверх, отпустите — вниз. Легенда: держите её в золотой полосе!")
				: TEXT("Держите [Пробел / ЛКМ] — полоса вверх, отпустите — вниз. Держите рыбу в золотой полосе!");
		}
		return FV::IsTouchUI() ? TEXT("Держите палец — полоса вверх, отпустите — вниз") : TEXT("Держите [Пробел / ЛКМ] — полоса вверх, отпустите — вниз. Держите рыбу в зелёной полосе!");
	case EFVFishState::Showcase: return FV::IsTouchUI() ? TEXT("Коснитесь, чтобы продолжить") : TEXT("[Пробел / ЛКМ] — продолжить");
	case EFVFishState::Escaped: return Impl->bPractice ? FString(FVFishingPracticeImpl::TryAgainText()) : FString(TEXT("Рыба сорвалась…"));
	case EFVFishState::Retrieving: return TEXT("Сматываем леску…");
	default: return FString();
	}
}

// ============================================================================================
// UPDATE 6 [FISHING-EXPERT] — the expert fight lives inside State Minigame (§10.13): FImpl::ExMode / FImpl::Ex.
// ============================================================================================
bool FVFishing::IsExpertFight() const
{
	return State == EFVFishState::Minigame
		&& (Impl->ExMode == FImpl::EExMode::Fight || Impl->ExMode == FImpl::EExMode::LegendOnExpert);
}

const FFVExpertFight* FVFishing::GetExpertFight() const
{
	return IsExpertFight() ? &Impl->Ex : nullptr;
}

FFVRect FVFishing::GetExpertFieldRect() const
{
	return IsExpertFight() ? Impl->CurExField() : FFVRect();
}

void FVFishing::DebugForceExpertBite(FName FishId)
{
	DebugForceBite(FishId);   // Bite / Hook route expert fish to the expert fight on an expert rod (Р25 on a classic one)
}

void FVFishing::DebugPinExpertProgress(float P)
{
	Impl->ExPinned = P < 0.f ? -1.f : FMath::Clamp(P, 0.01f, 0.99f);
	if (P >= 0.f && IsExpertFight()) { Impl->Ex.Progress = Impl->ExPinned; }
}

void FVFishing::StartExpertTutorial()
{
	Impl->bExTutorialPending = true;
	if (State == EFVFishState::Waiting && Impl->EquippedExpertRod())
	{
		Impl->BiteDelay = FMath::Min(Impl->BiteDelay, Impl->StateTime + 1.f);   // the bobber is already out: bite soon
	}
}

bool FVFishing::IsExpertTutorialPending() const
{
	return Impl->bExTutorialPending;
}
