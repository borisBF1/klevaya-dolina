// UPDATE 6 [EXPERT-FIGHT] — the expert fight, edition 2 «одна вертикаль» (Public/FVExpertFight.h; §19, Р60): bars of
// the five rod types on one 760-unit track, the classic fish motion + tricks from data, 1D hazards, progress + tension,
// treasure, bots and the headless simulation.
// Owner: EXPERT-FIGHT. Pure logic at 60 Hz, deterministic for a seed: Rng (bars / treasure), FishRng (fish),
// HazardRng (hazards), BotRng (bots). The classic FFVMinigame is not touched.

#include "FVExpertFight.h"
#include "FVDatabase.h"

namespace FVExpertFightImpl
{
	using namespace FVExpert;
	constexpr float XDt = 1.f / 60.f;
	constexpr float MaxFightSec = 240.f;       // simulation cut-off (counts as escaped)
	constexpr int32 SeenSize = 64;             // > 1 s of history at 60 Hz
	// ---- the classic motion on the long track (FFVMinigame: FishTargetMax 548 = 568 - 20, FishMaxPos 532 = 568 - 36)
	constexpr float ClassicTargetMax = TrackH - 20.f;
	constexpr float ClassicMaxTop = TrackH - 36.f;
	// ---- progress. The data's Gain / Drain were tuned for the slow 2D field: in 1D they only give the fish's character
	//      around a common rate — rate = Base x sqrt(data / Ref) (a fish with x4 the data drain drains x2)
	constexpr float GainBase = 0.10f, GainRef = 0.05f, DrainBase = 0.10f, DrainRef = 0.06f;
	constexpr float LegendGainBase = 0.08f, LegendGainRef = 0.03f, LegendDrainBase = 0.07f, LegendDrainRef = 0.045f;
	constexpr float ClassicGain = 0.12f;       // Р18 classic fish: the classic 0.002 / 0.003 per tick
	constexpr float ClassicDrain = 0.18f;
	/** Drain by rod type (Pendulum, Twin, Impulse, Magnet, Slider): one fish costs the same effort on any type. */
	constexpr float TypeDrain[5] = { 0.8f, 0.8f, 0.95f, 0.75f, 1.2f };
	constexpr float LegendMoveDrain = 2.0f;    // legends: the line drains faster while a score move plays
	constexpr float MagnetHeldGain = 0.6f;     // Magnet: progress while pulling (the fish fights the pull)
	constexpr float MagnetHeatAfter = 0.8f;    // Magnet: a pull longer than this heats the line ...
	constexpr float MagnetHeat = 0.8f;         // ... by this x (seconds over) per second (rests matter: §19 «натяжение»)
	constexpr float SliderHazardShift = 0.25f; // Slider: a hazard's push moves the bar off the finger (units per unit/s²)
	constexpr float SliderHazardMax = 90.f;
	constexpr float Phase1Speed = 1.12f;       // progress >= 55 %
	constexpr float Phase2Speed = 1.25f;       // progress >= 80 %
	constexpr float FishVigour = 0.15f;        // every fish is an individual: difficulty x(1 ± 0.15), drain x(1 ± 0.3)
	constexpr float FishStamina = 0.3f;
	constexpr float LegendVigour = 0.05f;
	constexpr float LegendStamina = 0.15f;
	/** The FFVFishDef::Difficulty of expert fish (84..130) is a CLASSIC-scale number where the classic motion is out of
	 *  human reach (classic d >= 86 Dart ~ 0 % for a strong human bot); the expert fight moves the fish like a classic
	 *  fish of ExpertDiffBase + (Difficulty - 85) x ExpertDiffSlope (84 -> ~45, 100 -> ~52, 130 -> ~65). */
	constexpr float ExpertDiffBase = 40.f;
	constexpr float ExpertDiffSlope = 0.4f;
	constexpr float LegendDiffSlope = 0.25f;   // legends (118..130 -> ~48..51): their score, not the base motion, is the test
	// ---- rods: the 2D data gives only the rod's CHARACTER relative to the reference rod of its type
	//      (Бриз, Близнецы, Румб, Якорь, Рулевой); the 1D numbers are the Base* below (Р60: Сила -> accel / push / pull,
	//      Размер зоны -> bar height, Прочность -> tension limit, Устойчивость -> resistance to current / waves)
	constexpr float RefZone[5] = { 112.f, 83.f, 125.f, 80.f, 86.f };      // Pendulum/Twin: ZoneH; others: ZoneW
	constexpr float BaseH[5] = { 150.f, 112.f, 150.f, 122.f, 104.f };      // bar height at the reference (Slider narrowest)
	constexpr float RefSpeed[5] = { 150.f, 120.f, 200.f, 40.f, 180.f };   // MaxSpeed
	constexpr float BaseSpeed[5] = { 430.f, 400.f, 560.f, 75.f, 750.f }; // bar speed cap (Magnet: its crawl)
	constexpr float RefGravity[2] = { 220.f, 200.f };
	constexpr float BarClassicAccel = 900.f;      // 0.25 units / tick² = the classic bar
	constexpr float RefPush = 125.f;           // Impulse (Румб)
	constexpr float BasePush = 300.f;
	constexpr float ImpulseGravity = 650.f;
	constexpr float RefPull = 130.f;           // Magnet (Якорь)
	constexpr float BasePull = 320.f;
	constexpr float RefHelm = 260.f;           // Slider (Рулевой): Push
	constexpr float BaseOmega = 12.f;
	constexpr float SliderZeta = 0.9f;
	// ---- tricks (seconds are real; speeds x the move's Speed x the phase)
	constexpr float DashWindup = 0.35f;
	constexpr float DashThrow = 0.25f;
	constexpr float DashRecover = 0.3f;
	constexpr float DashDist = 180.f;
	constexpr float RedJolt = 0.08f;           // tension when a throw ends inside a bar (/ line limit)
	constexpr float JumpWindup = 0.4f;
	constexpr float JumpAir = 0.35f;
	constexpr float JumpFall = 0.5f;
	constexpr float DiveSpeed = 700.f;
	constexpr float DiveBolt = 0.3f;
	constexpr float DiveDist = 280.f;
	constexpr float ShuttleLeg = 0.22f;
	constexpr float ShuttleAmp = 60.f;
	constexpr float CircleAmp = 140.f;
	constexpr float CirclePeriod = 2.6f;
	constexpr float EightAmp = 150.f;
	constexpr float EightPeriod = 3.2f;
	constexpr float HideSpeed = 300.f;
	constexpr float HideDistMin = 220.f;       // ordinary fish: the hiding spot, this far from the bar
	constexpr float HideDistMax = 340.f;
	constexpr float DecoyDist = 220.f;
	constexpr float FleeDist = 120.f;
	constexpr float FleeTime = 0.25f;
	constexpr float FleeCool = 2.f;           // x0.8..1.2 (FishRng)
	constexpr float TrickEveryMin = 2.5f;      // seconds of base motion between tricks (x0.8 / x0.6 in phases 1 / 2)
	constexpr float TrickEveryMax = 4.5f;
	// ---- legends: the opening lunge of a score move
	constexpr float LungeGather = 0.3f;
	constexpr float LungeBolt = 0.35f;
	constexpr float LungeJolt = 0.12f;
	constexpr float LungeLoss = 0.04f;         // x the legend's own factor (FVExpertLegends::Start)
	constexpr float LungeRedK = 0.3f;
	// ---- hazards (/ Stability)
	constexpr float CurrentAccel = 200.f;
	constexpr float WaveKick = 380.f;
	constexpr float WhirlK = 2.6f;             // accel per unit of distance to the centre
	constexpr float SpringAccel = 1500.f;
	constexpr float SpringFish = 220.f;        // the spring lifts the fish too (units/s)
	constexpr float PilesTension = 0.3f;
	constexpr float StoneH = 26.f;
	// ---- bots
	constexpr float BotBubbles = 0.6f;         // a hidden fish: how much the bubbles tell the bot (0 = pure guess)

	inline int32 XNextInt(FRandomStream& R, int32 Min, int32 MaxExclusive) { return MaxExclusive <= Min ? Min : Min + R.RandHelper(MaxExclusive - Min); }
	inline float XEaseOut(float K) { K = FMath::Clamp(K, 0.f, 1.f); return 1.f - (1.f - K) * (1.f - K); }
	inline float XSmooth(float K) { K = FMath::Clamp(K, 0.f, 1.f); return K * K * (3.f - 2.f * K); }
	inline float XClampFish(float Y) { return FMath::Clamp(Y, FishMin, FishMax); }
	inline float XSign(float V) { return V < 0.f ? -1.f : 1.f; }
	/** The direction Dir, or the other one when Dist does not fit that way. */
	inline float XRoomDir(float From, float Dist, float Dir)
	{
		const float To = From + Dir * Dist;
		return (To < FishMin + 8.f || To > FishMax - 8.f) ? -Dir : Dir;
	}
	/** Scripted motion as a delta: outside pulls (the Magnet, the spring) keep acting on a fish that plays a move. */
	inline void XScript(FFVExpertFight& F, float Abs)
	{
		F.FishPos = XClampFish(F.FishPos + (Abs - F.ScriptPos));
		F.ScriptPos = Abs;
	}
	inline int32 XType(const FFVExpertFight& F) { return FMath::Clamp(int32(F.Setup.Rod.Type), 0, 4); }
	inline float XStability(const FFVExpertFight& F) { return FMath::Max(0.3f, F.Setup.Stats.Stability); }
	inline float XForce(const FFVExpertFight& F) { return FMath::Clamp(F.Setup.Stats.Force, 0.2f, 3.f); }
	inline float XRatio(float V, float Ref) { return FMath::Clamp(V / FMath::Max(1e-4f, Ref), 0.25f, 4.f); }

	bool XIsContinuous(EFVHazard K)
	{
		return K == EFVHazard::Current || K == EFVHazard::Piles || K == EFVHazard::Weeds || K == EFVHazard::Darkness;
	}

	/** Random spans on the track that do not overlap each other (Piles / Weeds). */
	void XAddSpans(FFVExpertHazardState& H, FRandomStream& R, int32 Num, float MinH, float MaxH)
	{
		for (int32 i = 0; i < Num; ++i)
		{
			for (int32 Try = 0; Try < 10; ++Try)
			{
				const float Hh = R.FRandRange(MinH, MaxH);
				const float Top = R.FRandRange(30.f, TrackH - 30.f - Hh);
				bool bOk = true;
				for (const FVector2D& S : H.Spans)
				{
					if (Top < S.Y + 60.f && Top + Hh > S.X - 60.f) { bOk = false; }
				}
				if (bOk || Try == 9)
				{
					H.Spans.Add(FVector2D(Top, Top + Hh));
					H.SpanLife.Add(0.f);
					break;
				}
			}
		}
	}

	FFVExpertHazardState XMakeHazard(FFVExpertFight& F, EFVHazard K)
	{
		FRandomStream& R = F.HazardRng;
		FFVExpertHazardState H;
		H.Kind = K;
		switch (K)
		{
		case EFVHazard::Current:
			H.Dir = R.FRand() < 0.5f ? -1.f : 1.f;
			H.Active = 1.f;
			H.Strength = 1.f;
			break;
		case EFVHazard::Wave:
			H.Next = R.FRandRange(3.f, 5.f);
			break;
		case EFVHazard::Whirlpool:
			H.Next = R.FRandRange(3.f, 6.f);
			H.Pos = Mid;
			break;
		case EFVHazard::Piles:
			XAddSpans(H, R, 2, 50.f, 70.f);
			H.Active = 1.f;
			H.Strength = 1.f;
			break;
		case EFVHazard::Weeds:
			XAddSpans(H, R, 1 + R.RandHelper(2), 110.f, 150.f);
			H.Active = 1.f;
			H.Strength = 1.f;
			break;
		case EFVHazard::Darkness:
			H.Active = 1.f;
			H.Radius = 70.f;
			H.Strength = 1.f;
			break;
		case EFVHazard::Drip:
			H.Next = R.FRandRange(2.f, 4.f);
			break;
		case EFVHazard::Spring:
			H.Next = R.FRandRange(3.f, 5.f);
			H.Radius = 70.f;
			H.Pos = R.FRandRange(0.55f, 0.82f) * TrackH;
			H.Spans.Add(FVector2D(H.Pos - H.Radius, H.Pos + H.Radius));
			H.SpanLife.Add(0.f);
			break;
		default:
			break;
		}
		return H;
	}

	/** A move's hazard (FFVExpertMove::Hazard): an existing periodic one fires now, a missing one is added for Seconds. */
	void XRaiseMoveHazard(FFVExpertFight& F, EFVHazard K, float Seconds)
	{
		if (K >= EFVHazard::Count) { return; }
		for (FFVExpertHazardState& H : F.Hazards)
		{
			if (H.Kind != K) { continue; }
			if (H.bTemp) { H.TempLeft = FMath::Max(H.TempLeft, Seconds); }
			if (!XIsContinuous(K) && H.Warn <= 0.f && H.Active <= 0.f) { H.Next = 0.f; }
			return;
		}
		FFVExpertHazardState H = XMakeHazard(F, K);
		H.bTemp = true;
		H.TempLeft = FMath::Max(0.5f, Seconds);
		if (!XIsContinuous(K)) { H.Next = 0.f; }
		F.Hazards.Add(H);
	}

	/** Timers of the hazards (periodic ones: warning -> action -> pause). */
	void XStepHazards(FFVExpertFight& F, float Dt)
	{
		FRandomStream& R = F.HazardRng;
		const float Stab = XStability(F);
		for (int32 i = F.Hazards.Num() - 1; i >= 0; --i)
		{
			FFVExpertHazardState& H = F.Hazards[i];
			// drip stones age
			for (int32 s = H.Spans.Num() - 1; s >= 0 && H.Kind == EFVHazard::Drip; --s)
			{
				H.SpanLife[s] -= Dt;
				if (H.SpanLife[s] <= 0.f) { H.Spans.RemoveAt(s); H.SpanLife.RemoveAt(s); }
			}
			if (H.bTemp)
			{
				H.TempLeft -= Dt;
				if (H.TempLeft <= 0.f && (XIsContinuous(H.Kind) || (H.Warn <= 0.f && H.Active <= 0.f
					&& (H.Kind != EFVHazard::Drip || H.Spans.Num() == 0))))
				{
					F.Hazards.RemoveAt(i);
					continue;
				}
			}
			if (XIsContinuous(H.Kind)) { continue; }
			const bool bMore = !H.bTemp || H.TempLeft > 0.f;
			H.Strength = 0.f;
			if (H.Active > 0.f)
			{
				H.Active -= Dt;
				H.Strength = 1.f;
				if (H.Active <= 0.f)
				{
					H.Active = 0.f;
					switch (H.Kind)
					{
					case EFVHazard::Wave: H.Next = R.FRandRange(3.f, 5.f); break;
					case EFVHazard::Whirlpool: H.Next = R.FRandRange(3.f, 5.f); break;
					case EFVHazard::Spring: H.Next = R.FRandRange(3.f, 4.5f); break;
					default: H.Next = R.FRandRange(2.5f, 4.f); break;
					}
				}
			}
			else if (H.Warn > 0.f)
			{
				H.Warn -= Dt;
				if (H.Warn <= 0.f)
				{
					H.Warn = 0.f;
					switch (H.Kind)
					{
					case EFVHazard::Wave:
						H.Active = 0.3f;
						for (int32 z = 0; z < F.NumZones; ++z) { F.Zones[z].Vel += H.Dir * WaveKick / Stab; }
						break;
					case EFVHazard::Whirlpool: H.Active = 3.f; break;
					case EFVHazard::Spring: H.Active = 2.f; break;
					case EFVHazard::Drip:
						// the stone falls where the warning showed; there are never more than three
						if (H.Spans.Num() >= 3) { H.Spans.RemoveAt(0); H.SpanLife.RemoveAt(0); }
						H.Spans.Add(FVector2D(H.Pos - StoneH * 0.5f, H.Pos + StoneH * 0.5f));
						H.SpanLife.Add(R.FRandRange(2.5f, 3.5f));
						H.Next = R.FRandRange(2.5f, 4.f);
						break;
					default: break;
					}
				}
			}
			else if (bMore)
			{
				H.Next -= Dt;
				if (H.Next <= 0.f)
				{
					H.Warn = H.Kind == EFVHazard::Drip ? 0.7f : 0.8f;
					if (H.Kind == EFVHazard::Wave) { H.Dir = R.FRand() < 0.5f ? -1.f : 1.f; }
					if (H.Kind == EFVHazard::Drip) { H.Pos = R.FRandRange(60.f, TrackH - 60.f); }
				}
			}
		}
	}

	/** Outside acceleration on a bar (current, whirlpool, spring), units/s². */
	float XHazardAccel(const FFVExpertFight& F, const FFVExpertZone& Z)
	{
		const float Stab = XStability(F);
		float A = 0.f;
		for (const FFVExpertHazardState& H : F.Hazards)
		{
			switch (H.Kind)
			{
			case EFVHazard::Current: A += H.Dir * CurrentAccel / Stab; break;
			case EFVHazard::Whirlpool: if (H.Active > 0.f) { A += (H.Pos - Z.Y) * WhirlK / Stab; } break;
			case EFVHazard::Spring:
				if (H.Active > 0.f && H.Spans.Num() > 0 && Z.Y >= H.Spans[0].X && Z.Y <= H.Spans[0].Y) { A -= SpringAccel / Stab; }
				break;
			default: break;
			}
		}
		return A;
	}

	/** Is a point inside a span of a hazard of this kind? */
	bool XInSpan(const FFVExpertFight& F, EFVHazard K, float Y)
	{
		for (const FFVExpertHazardState& H : F.Hazards)
		{
			if (H.Kind != K) { continue; }
			for (const FVector2D& S : H.Spans)
			{
				if (Y >= S.X && Y <= S.Y) { return true; }
			}
		}
		return false;
	}

	/** Integrates a bar: speed cap, weeds / piles drag, walls with a bounce, drip stones. */
	void XIntegrateBar(FFVExpertFight& F, FFVExpertZone& Z, float Acc, float Dt, float Cap)
	{
		Z.Vel = FMath::Clamp(Z.Vel + Acc * Dt, -Cap, Cap);
		if (XInSpan(F, EFVHazard::Piles, Z.Y)) { Z.Vel *= 1.f - FMath::Min(1.f, 6.f * Dt); }
		else if (XInSpan(F, EFVHazard::Weeds, Z.Y)) { Z.Vel *= 1.f - FMath::Min(1.f, 2.5f * Dt); }
		Z.Y += Z.Vel * Dt;
		const float Lo = Z.Lo + Z.H * 0.5f, Hi = Z.Hi - Z.H * 0.5f;
		if (Z.Y < Lo) { Z.Y = Lo; if (Z.Vel < 0.f) { Z.Vel = -Z.Vel * F.BarBounce; } }
		if (Z.Y > Hi) { Z.Y = Hi; if (Z.Vel > 0.f) { Z.Vel = -Z.Vel * F.BarBounce; } }
		for (const FFVExpertHazardState& H : F.Hazards)
		{
			if (H.Kind != EFVHazard::Drip) { continue; }
			for (const FVector2D& S : H.Spans)
			{
				if (Z.Bottom() <= S.X || Z.Top() >= S.Y) { continue; }
				if (Z.Y < (S.X + S.Y) * 0.5f)
				{
					Z.Y = FMath::Max(Lo, float(S.X) - Z.H * 0.5f);
					if (Z.Vel > 0.f) { Z.Vel = -Z.Vel * 0.5f; }
				}
				else
				{
					Z.Y = FMath::Min(Hi, float(S.Y) + Z.H * 0.5f);
					if (Z.Vel < 0.f) { Z.Vel = -Z.Vel * 0.5f; }
				}
			}
		}
	}

	// ------------------------------------------------------------------------------------ the fish
	/** One tick of the classic Stardew motion (FFVMinigame::Step) on the long track; K scales the displacement. */
	void XStepClassic(FFVExpertFight& F, float K)
	{
		FRandomStream& R = F.FishRng;
		const float Diff = F.FishDifficulty;
		const bool bSmooth = F.FishMotion == EFVMotion::Smooth;
		const float Top = F.FishPos - FishIconH * 0.5f;
		float& Tg = F.ClassicTarget;
		float& Sp = F.ClassicSpeed;
		if (R.FRand() < Diff * (bSmooth ? 20.f : 1.f) / 4000.f && (!bSmooth || Tg == -1.f))
		{
			const float SpaceBelow = ClassicTargetMax - Top;
			const float SpaceAbove = Top;
			const float Percent = FMath::Min(99.f, Diff + float(XNextInt(R, 10, 45))) / 100.f;
			Tg = Top + float(XNextInt(R, int32(FMath::Min(-SpaceAbove, SpaceBelow)), int32(SpaceBelow))) * Percent;
		}
		if (F.FishMotion == EFVMotion::Floater) { F.FloatSink = FMath::Max(F.FloatSink - 0.01f, -1.5f); }
		else if (F.FishMotion == EFVMotion::Sinker) { F.FloatSink = FMath::Min(F.FloatSink + 0.01f, 1.5f); }
		if (FMath::Abs(Top - Tg) > 3.f && Tg != -1.f)
		{
			F.ClassicAccel = (Tg - Top) / (float(XNextInt(R, 10, 30)) + (100.f - FMath::Min(100.f, Diff)));
			Sp += (F.ClassicAccel - Sp) / 5.f;
		}
		else if (!bSmooth && R.FRand() < Diff / 2000.f)
		{
			Tg = Top + (R.FRand() < 0.5f ? float(XNextInt(R, -100, -51)) : float(XNextInt(R, 50, 101))) * TrackScale;
		}
		else
		{
			Tg = -1.f;
		}
		if (F.FishMotion == EFVMotion::Dart && R.FRand() < Diff / 1000.f)
		{
			const int32 D2 = int32(Diff) * 2;
			Tg = Top + (R.FRand() < 0.5f ? float(XNextInt(R, -100 - D2, -51)) : float(XNextInt(R, 50, 101 + D2))) * TrackScale;
		}
		Tg = FMath::Clamp(Tg, -1.f, ClassicTargetMax);
		const float NewTop = FMath::Clamp(Top + (Sp + F.FloatSink) * K, 0.f, ClassicMaxTop);
		F.FishPos = NewTop + FishIconH * 0.5f;
	}

	/** The classic motion picks up from wherever a trick left the fish. */
	void XResumeClassic(FFVExpertFight& F)
	{
		F.ClassicTarget = -1.f;
		F.ClassicSpeed = FMath::Clamp(F.FishVel * XDt, -8.f, 8.f);
	}

	/** Skittish: the fish bolts away when a bar comes close. Returns true while a flight plays. */
	bool XStepFlee(FFVExpertFight& F, float Amp, float S, float Dt)
	{
		F.MoveCool -= Dt;
		if (F.MoveP > 0.f)
		{
			F.MoveP -= Dt;
			const float K = 1.f - FMath::Max(0.f, F.MoveP) / (FleeTime / S);
			XScript(F, FMath::Lerp(F.MoveA, F.MoveB, XEaseOut(K)));
			if (F.MoveP <= 0.f) { F.MoveP = 0.f; XResumeClassic(F); }
			return true;
		}
		if (F.MoveCool > 0.f) { return false; }
		for (int32 z = 0; z < F.NumZones; ++z)
		{
			const FFVExpertZone& Z = F.Zones[z];
			const float D = F.FishPos - Z.Y;
			if (Z.bActive && FMath::Abs(D) < Z.H * 0.5f + 10.f)   // it is in the bar (or at its edge): off it goes
			{
				const float Dir = XRoomDir(F.FishPos, FleeDist * Amp, XSign(D));
				F.MoveA = F.FishPos;
				F.MoveB = XClampFish(F.FishPos + Dir * FleeDist * Amp);
				F.MoveP = FleeTime / S;
				F.ScriptPos = F.MoveA;
				F.MoveCool = FleeCool * F.FishRng.FRandRange(0.8f, 1.2f);
				return true;
			}
		}
		return false;
	}

	/** The base motion of the fish (between tricks): the classic one; a Skittish base also flees the bars. */
	void XStepBase(FFVExpertFight& F, float K, float Dt)
	{
		if (!F.Setup.bClassicFish && F.Setup.Fish.Base == EFVExpertPattern::Skittish && XStepFlee(F, 1.f, K, Dt)) { return; }
		XStepClassic(F, K);
	}

	float XTrickLen(const FFVExpertMove& M) { return FMath::Clamp(M.Seconds, 0.5f, 12.f); }

	/** Ordinary expert fish: base motion, a warning 0.4–1 s before each trick, the trick, back to the base. */
	void XStepOrdinaryFish(FFVExpertFight& F, float Dt)
	{
		const TArray<FFVExpertMove>& Tricks = F.Setup.Fish.Tricks;
		const float K = F.PhaseSpeed;
		if (F.Setup.bClassicFish || Tricks.Num() == 0)
		{
			XStepBase(F, K, Dt);
			return;
		}
		if (F.MoveIndex >= 0 && Tricks.IsValidIndex(F.MoveIndex))
		{
			const FFVExpertMove& M = Tricks[F.MoveIndex];
			F.StepMove(M, F.MoveT, Dt);
			F.MoveT += Dt;
			if (F.MoveT >= F.MoveLen)
			{
				F.MoveIndex = -1;
				F.MoveT = 0.f;
				F.Signal = EFVIcon::Count;
				F.SignalT = 0.f;
				F.SignalName.Reset();
				F.Pattern = EFVExpertPattern::Snake;
				F.CurMoveT = -1.f;
				const float PhaseK = F.Phase >= 2 ? 0.6f : (F.Phase == 1 ? 0.8f : 1.f);
				F.BaseLeft = F.FishRng.FRandRange(TrickEveryMin, TrickEveryMax) * PhaseK;
				XResumeClassic(F);
			}
			return;
		}
		XStepBase(F, K, Dt);
		if (F.PendingMove >= 0)
		{
			F.WarnLeft -= Dt;
			if (F.WarnLeft <= 0.f)
			{
				F.MoveIndex = FMath::Clamp(F.PendingMove, 0, Tricks.Num() - 1);
				F.PendingMove = -1;
				F.MoveT = 0.f;
				F.CurMoveT = -1.f;
				F.MoveLen = XTrickLen(Tricks[F.MoveIndex]);
			}
			return;
		}
		F.BaseLeft -= Dt;
		if (F.BaseLeft <= 0.f)
		{
			F.PendingMove = F.FishRng.RandHelper(Tricks.Num());
			F.WarnLeft = F.FishRng.FRandRange(0.4f, 1.f);
			const FFVExpertMove& M = Tricks[F.PendingMove];
			F.RaiseSignal(M.Signal != EFVIcon::Count ? M.Signal : FFVExpertFight::DefaultSignal(M.Pattern), M.Name);
		}
	}

	void XUpdatePhase(FFVExpertFight& F)
	{
		if (F.Setup.bClassicFish || F.Setup.bTutorial) { return; }
		const int32 P = F.Progress >= 0.8f ? 2 : (F.Progress >= 0.55f ? 1 : 0);
		if (P > F.Phase)
		{
			F.Phase = P;
			F.PhaseSpeed = P >= 2 ? Phase2Speed : Phase1Speed;
		}
	}
}

// ============================================================================================

EFVIcon FFVExpertFight::DefaultSignal(EFVExpertPattern P)
{
	switch (P)
	{
	case EFVExpertPattern::Circle: return EFVIcon::WarnWhirl;
	case EFVExpertPattern::Eight: return EFVIcon::WarnSpark;
	case EFVExpertPattern::Shuttle: return EFVIcon::WarnCurrent;
	case EFVExpertPattern::Dash: return EFVIcon::WarnDash;
	case EFVExpertPattern::Hide: return EFVIcon::WarnEye;
	case EFVExpertPattern::Dive: return EFVIcon::WarnCloud;
	case EFVExpertPattern::Decoy: return EFVIcon::WarnLantern;
	case EFVExpertPattern::Skittish: return EFVIcon::WarnWave;
	case EFVExpertPattern::Jump: return EFVIcon::WarnSpring;
	default: return EFVIcon::Exclaim;
	}
}

void FFVExpertFight::RaiseSignal(EFVIcon Icon, const FString& Name)
{
	Signal = Icon;
	SignalT = 0.f;
	++SignalSerial;
	SignalName = Name;
}

float FFVExpertFight::TensionLimit() const
{
	return FMath::Max(0.2f, Setup.Stats.LineStrength * Setup.Rod.TensionReserve);
}

bool FFVExpertFight::IsInZone(float P, int32 ZoneIndex) const
{
	if (ZoneIndex >= 0) { return ZoneIndex < NumZones && Zones[ZoneIndex].Contains(P); }
	for (int32 i = 0; i < NumZones; ++i)
	{
		if (Zones[i].Contains(P)) { return true; }
	}
	return false;
}

bool FFVExpertFight::IsLit(float P) const
{
	for (const FFVExpertHazardState& H : Hazards)
	{
		if (H.Kind != EFVHazard::Darkness) { continue; }
		for (int32 i = 0; i < NumZones; ++i)
		{
			const FFVExpertZone& Z = Zones[i];
			if (Z.bActive && P >= Z.Top() - H.Radius && P <= Z.Bottom() + H.Radius) { return true; }
		}
		return false;
	}
	return true;
}

void FFVExpertFight::StartLunge(float ToY)
{
	SurgeFrom = FishPos;
	SurgeTo = FVExpertFightImpl::XClampFish(ToY);
	SurgeT = 0.f;
	SurgeJolt = FVExpertFightImpl::LungeJolt;
}

void FFVExpertFight::StepLunge(float Dt)
{
	using namespace FVExpertFightImpl;
	if (SurgeT < 0.f) { return; }
	SurgeT += Dt;
	bFishRed = true;
	if (SurgeT < LungeGather)
	{
		bTrembling = true;
		SurgeFrom = FishPos;
		ScriptPos = FishPos;
		return;
	}
	const float K = (SurgeT - LungeGather) / LungeBolt;
	XScript(*this, FMath::Lerp(SurgeFrom, SurgeTo, XEaseOut(K)));
	if (K >= 1.f)
	{
		// held where it went: a jolt of the line; missed: the legend takes back some progress
		if (IsInZone(FishPos)) { Tension += SurgeJolt / TensionLimit(); }
		else if (!Setup.bTutorial) { Progress -= LungeLoss * LungeLossK; }
		SurgeT = -1.f;
		XResumeClassic(*this);
	}
}

void FFVExpertFight::Start(const FFVExpertSetup& InSetup, int32 Seed)
{
	using namespace FVExpertFightImpl;
	Setup = InSetup;
	Rng.Initialize(Seed);
	FishRng.Initialize(Seed ^ 0x46495348);
	HazardRng.Initialize(Seed ^ 0x48415A44);
	BotRng.Initialize(Seed ^ 0x424F5453);
	const int32 Ty = XType(*this);
	const bool bLeg = IsLegend();

	// ---- the fish: its classic difficulty / motion (Р18 classic fish: its own; expert: FFVFishDef, Р60)
	if (Setup.bClassicFish)
	{
		FishDifficulty = Setup.ClassicDifficulty;
		FishMotion = Setup.ClassicMotion;
		GainRate = ClassicGain;
		DrainRate = ClassicDrain * TypeDrain[Ty];
		RedTensionRate = 0.3f;
	}
	else
	{
		FishDifficulty = Setup.Difficulty;
		FishMotion = Setup.Motion;
		if (FishDifficulty <= 0.f || FishMotion >= EFVMotion::Count)
		{
			const FFVFishDef* FD = Setup.Fish.FishId.IsNone() ? nullptr : FVDB::FindFish(Setup.Fish.FishId);
			if (FishDifficulty <= 0.f)
			{
				FishDifficulty = FD ? float(FD->Difficulty) : FMath::Clamp(84.f + (Setup.Fish.Speed - 37.f) * 1.5f, 80.f, 130.f);
			}
			if (FishMotion >= EFVMotion::Count)
			{
				if (FD) { FishMotion = FD->Motion; }
				else
				{
					switch (Setup.Fish.Base)
					{
					case EFVExpertPattern::Skittish: FishMotion = EFVMotion::Dart; break;
					case EFVExpertPattern::Circle:
					case EFVExpertPattern::Eight: FishMotion = EFVMotion::Smooth; break;
					default: FishMotion = EFVMotion::Mixed; break;
					}
				}
			}
		}
		FishDifficulty = FMath::Clamp(ExpertDiffBase + (FishDifficulty - 85.f) * (bLeg ? LegendDiffSlope : ExpertDiffSlope), 30.f, 95.f);
		GainRate = bLeg ? LegendGainBase * FMath::Pow(XRatio(Setup.Fish.Gain, LegendGainRef), 0.25f)
			: GainBase * FMath::Sqrt(XRatio(Setup.Fish.Gain, GainRef));
		DrainRate = bLeg ? LegendDrainBase * FMath::Sqrt(XRatio(Setup.Fish.Drain, LegendDrainRef))
			: DrainBase * FMath::Sqrt(XRatio(Setup.Fish.Drain, DrainRef)) * TypeDrain[Ty];
		RedTensionRate = FMath::Max(0.f, Setup.Fish.TensionRate);
		// every fish is an individual: its vigour (difficulty) and stamina (drain) vary from fight to fight
		FishDifficulty *= 1.f + (bLeg ? LegendVigour : FishVigour) * FishRng.FRandRange(-1.f, 1.f);
		DrainRate *= 1.f + (bLeg ? LegendStamina : FishStamina) * FishRng.FRandRange(-1.f, 1.f);
	}
	FishDifficulty = FMath::Clamp(FishDifficulty, 1.f, 150.f);
	if (FishMotion >= EFVMotion::Count) { FishMotion = EFVMotion::Mixed; }
	ClassicTarget = (100.f - FMath::Min(100.f, FishDifficulty)) / 100.f * ClassicTargetMax;
	ClassicSpeed = 0.f;
	ClassicAccel = 0.f;
	FloatSink = 0.f;
	BaseSpeedK = 1.f;

	// ---- the bars by rod type (§19): 1D numbers from the rod's character x the stats
	const FFVExpertRodDef& Rod = Setup.Rod;
	const float ZS = FMath::Clamp(Setup.Stats.ZoneSize, 0.5f, 2.f);
	const float Fo = XForce(*this);
	const bool bBand = Ty == 0 || Ty == 1;
	const float Hh = FMath::Clamp(BaseH[Ty] * FMath::Sqrt(XRatio(bBand ? Rod.ZoneH : Rod.ZoneW, RefZone[Ty])) * ZS, 40.f, 420.f);
	BarMaxSpeed = BaseSpeed[Ty] * FMath::Sqrt(XRatio(Rod.MaxSpeed, RefSpeed[Ty]));
	BarAccel = BarClassicAccel * (bBand ? FMath::Sqrt(XRatio(Rod.Gravity, RefGravity[Ty])) : 1.f) * Fo;
	BarBounce = FMath::Clamp(0.667f * Rod.Bounce / 0.35f, 0.4f, 0.8f);
	BarDrag = FMath::Clamp(Rod.Drag * 0.4f, 0.f, 3.f);
	ImpulsePush = BasePush * FMath::Sqrt(XRatio(Rod.Push, RefPush)) * Fo;
	ImpulseBrake = FMath::Clamp(1.f - Rod.Inertia, 0.1f, 0.9f);
	MagnetPull = BasePull * XRatio(Rod.Pull, RefPull) * Fo;
	MagnetDrift = BarMaxSpeed;
	SliderOmega = BaseOmega * FMath::Sqrt(XRatio(Rod.Push, RefHelm) * Fo) * (1.25f - 0.5f * FMath::Clamp(Rod.Inertia, 0.f, 1.f)) / 0.9f;
	SliderKeyAccel = 1500.f * Fo;
	NumZones = Ty == 1 ? 2 : 1;
	for (int32 i = 0; i < 2; ++i)
	{
		Zones[i] = FFVExpertZone();
		// the Twin bars roam the whole track since 27.09 (and sweep it): a bit narrower keeps blind tapping («сыр») under 5 %
		Zones[i].H = Ty == 1 ? Hh * 0.92f : Hh;
		Zones[i].bActive = i < NumZones;
	}
	if (Ty == 1)
	{
		// owner fix 27.09: both bars roam the WHOLE track and pass through each other (no halves). Held: bar 0 goes up,
		// bar 1 down; released: the other way. Where they cross, the overlap counts x1.5.
		Zones[0].Lo = 0.f; Zones[0].Hi = TrackH;
		Zones[1].Lo = 0.f; Zones[1].Hi = TrackH;
		Zones[0].Y = TrackH * 0.3f;
		Zones[1].Y = TrackH * 0.7f;
		FishPos = Mid;
	}
	else
	{
		Zones[0].Y = TrackH - Hh * 0.5f;   // the bar starts at the bottom, like the classic
		FishPos = XClampFish(TrackH - 80.f);
		if (Ty == 3) { Zones[0].Y = FMath::Clamp(FishPos, Hh * 0.5f, TrackH - Hh * 0.5f); }
	}
	bGravityUp = false;
	PressBuffer = 0.f;
	PushAge = 10.f;
	SliderTarget = -1.f;
	bMagnetHeld = false;
	HeldT = 0.f;

	// ---- fish state
	FishVel = 0.f;
	FishAlpha = 1.f;
	bBubbles = false;
	bFishRed = false;
	RedT = 0.f;
	bGhost = false;
	GhostPos = FishPos;
	GhostSerial = 0;
	bFishAirborne = false;
	bFrozen = false;
	bTrembling = false;
	Pattern = EFVExpertPattern::Snake;
	MoveIndex = -1;
	MoveT = MoveLen = MoveP = 0.f;
	MoveA = MoveB = FishPos;
	Mirror = 1.f;
	CurMove = FFVExpertMove();
	CurMoveT = -1.f;
	MoveCalls = 0;
	MoveStage = 0;
	StageT = 0.f;
	MoveDir = 1.f;
	MoveCount = 0;
	MoveCool = 0.f;
	PendingMove = -1;
	WarnLeft = 0.f;
	BaseLeft = FishRng.FRandRange(TrickEveryMin, TrickEveryMax);
	Phase = 0;
	PhaseSpeed = 1.f;
	SurgeT = -1.f;
	SurgeFrom = SurgeTo = FishPos;
	SurgeJolt = 0.f;
	LungePass = -1;
	LungeLossK = 1.f;

	// ---- meters
	Progress = 0.3f;
	Tension = 0.f;
	bInZone = IsInZone(FishPos);
	bInOverlap = false;
	bFlawless = true;
	Elapsed = 0.f;

	// ---- treasure
	bTreasureVisible = false;
	bTreasureCaught = false;
	TreasureProgress = 0.f;
	TreasurePos = Mid;
	TreasureVel = 0.f;
	TreasureAppear = Setup.bHasTreasure ? Rng.FRandRange(4.f, 10.f) : 0.f;

	// ---- hazards (at most two, one «invisibility», one before level 13; none for a classic fish, Р18)
	Hazards.Reset();
	if (!Setup.bClassicFish)
	{
		const int32 MaxN = Setup.Level < 13 ? 1 : 2;
		TArray<EFVHazard, TInlineAllocator<2>> Kinds;
		bool bInvis = false;
		for (const EFVHazard K : Setup.Hazards)
		{
			if (K >= EFVHazard::Count || Kinds.Contains(K)) { continue; }
			const bool bKInvis = K == EFVHazard::Darkness || K == EFVHazard::Weeds;
			if (bKInvis && bInvis) { continue; }
			if (Kinds.Num() >= MaxN) { break; }
			bInvis = bInvis || bKInvis;
			Kinds.Add(K);
		}
		for (const EFVHazard K : Kinds) { Hazards.Add(XMakeHazard(*this, K)); }
	}

	// ---- signals / bots
	Signal = EFVIcon::Count;
	SignalT = 0.f;
	SignalSerial = 0;
	SignalName.Reset();
	ScoreReached = 0;
	LastInput = FFVExpertInput();
	LastPressT = -10.f;
	BotErr = BotErrTarget = 0.f;
	BotErrT = 0.f;
	BotVel = 0.f;
	BotLastSeen = FishPos;
	BotDelayT = -1.f;
	bBotWant = bBotOut = bBotTapPending = bBotPrevOut = false;
	BotTapT = 0.f;
	BotRestT = 0.f;
	BotTargetY = Zones[0].Y;
	BotGhostSerial = -1;
	bBotFollowsGhost = false;
	BotTwinBar = 1;

	if (bLeg) { FVExpertLegends::Start(*this); }

	FFVExpertSeen S0;
	S0.Pos = FishPos;
	S0.Ghost = FishPos;
	S0.Tip = FishPos;
	Seen.Init(S0, SeenSize);
	SeenHead = 0;
}

void FFVExpertFight::StepZones(const FFVExpertInput& In, float Dt)
{
	using namespace FVExpertFightImpl;
	const int32 Ty = XType(*this);
	PushAge += Dt;
	switch (Ty)
	{
	case 0:   // Маятник: a press turns the gravity over; the bar never hangs still; walls bounce
	{
		if (In.bPressed) { bGravityUp = !bGravityUp; }
		FFVExpertZone& Z = Zones[0];
		XIntegrateBar(*this, Z, (bGravityUp ? -BarAccel : BarAccel) + XHazardAccel(*this, Z), Dt, BarMaxSpeed);
		break;
	}
	case 1:   // Двойчатка: held — the upper bar goes up, the lower one down (mirror); released — the other way
	{
		for (int32 i = 0; i < 2; ++i)
		{
			FFVExpertZone& Z = Zones[i];
			const float Sgn = (i == 0) == In.bDown ? -1.f : 1.f;
			XIntegrateBar(*this, Z, Sgn * BarAccel + XHazardAccel(*this, Z), Dt, BarMaxSpeed);
		}
		break;
	}
	case 2:   // Импульс: every press is a push up; between pushes the bar falls; holding gives nothing
	{
		FFVExpertZone& Z = Zones[0];
		if (In.bPressed)
		{
			if (Z.Vel > 0.f) { Z.Vel *= 1.f - ImpulseBrake; }
			Z.Vel -= ImpulsePush;
			PushAge = 0.f;
		}
		XIntegrateBar(*this, Z, ImpulseGravity - BarDrag * Z.Vel + XHazardAccel(*this, Z), Dt, BarMaxSpeed);
		break;
	}
	case 3:   // Магнит: the bar crawls after the fish by itself; the pull is in Step
	{
		FFVExpertZone& Z = Zones[0];
		const float Want = FMath::Clamp((FishPos - Z.Y) * 1.5f, -MagnetDrift, MagnetDrift);
		XIntegrateBar(*this, Z, (Want - Z.Vel) * 4.f + XHazardAccel(*this, Z), Dt, FMath::Max(MagnetDrift * 3.f, 300.f));
		break;
	}
	default:  // Ползунок: the finger leads the bar with inertia (a damped spring); keys accelerate it; free = it coasts
	{
		FFVExpertZone& Z = Zones[0];
		float A = 0.f;
		if (In.TargetY >= 0.f)
		{
			SliderTarget = FMath::Clamp(In.TargetY, Z.H * 0.5f, TrackH - Z.H * 0.5f);
			// the stiff finger spring would swallow a current / a whirlpool: they shift the bar off the finger instead
			const float Shift = FMath::Clamp(XHazardAccel(*this, Z) * SliderHazardShift, -SliderHazardMax, SliderHazardMax);
			A = SliderOmega * SliderOmega * (SliderTarget + Shift - Z.Y) - 2.f * SliderZeta * SliderOmega * Z.Vel;
		}
		else
		{
			SliderTarget = -1.f;
			const float Key = FMath::Clamp(float(In.Stick.Y), -1.f, 1.f);
			A = Key * SliderKeyAccel - BarDrag * 2.f * Z.Vel;
		}
		const float VBefore = Z.Vel;
		XIntegrateBar(*this, Z, A + XHazardAccel(*this, Z), Dt, BarMaxSpeed);
		// sharp turns with the fish inside load the line (Rod.TurnTension)
		if (Setup.Rod.TurnTension > 0.f && Z.Contains(FishPos))
		{
			const float Jerk = FMath::Abs(Z.Vel - VBefore) / FMath::Max(Dt, 1e-4f);
			if (Jerk > 2500.f) { Tension += Setup.Rod.TurnTension * (Jerk - 2500.f) / 2500.f / TensionLimit() * Dt; }
		}
		break;
	}
	}
}

void FFVExpertFight::StepMove(const FFVExpertMove& Move, float T, float Dt)
{
	using namespace FVExpertFightImpl;
	++MoveCalls;
	const float S = FMath::Clamp(Move.Speed, 0.3f, 3.f) * PhaseSpeed;
	const float A = FMath::Clamp(Move.Amp, 0.3f, 3.f);
	const bool bNew = T <= 0.f || T < CurMoveT || Move.Pattern != CurMove.Pattern || CurMoveT < 0.f;
	CurMove = Move;
	CurMoveT = T;
	Pattern = Move.Pattern;
	if (bNew)
	{
		MoveStage = 0;
		StageT = 0.f;
		MoveCount = 0;
		MoveCool = 0.f;
		MoveP = 0.f;
		MoveLen = Move.Seconds;
		MoveDir = IsLegend() ? Mirror : (FishRng.FRand() < 0.5f ? -1.f : 1.f);
		MoveA = MoveB = FishPos;
		ScriptPos = FishPos;
		XRaiseMoveHazard(*this, Move.Hazard, Move.Seconds);
		switch (Move.Pattern)
		{
		case EFVExpertPattern::Jump:
			// «Свеча»: never mirrored (it always leaps up); the landing depends on the mirror (legends) / is random
			MoveB = IsLegend() ? (Mirror > 0.f ? 0.7f : 0.45f) * TrackH : FishRng.FRandRange(0.35f, 0.8f) * TrackH;
			break;
		case EFVExpertPattern::Dive:
		{
			const float Edge = MoveDir > 0.f ? FishMax : FishMin;
			MoveA = Edge;
			MoveB = XClampFish(Edge - MoveDir * DiveDist * A);
			MoveP = FMath::Clamp(Move.Seconds * 0.4f, 0.8f, 2.2f);   // freeze length
			break;
		}
		case EFVExpertPattern::Shuttle:
		{
			const float R = FMath::Min(ShuttleAmp * A, Mid - FishMin - 4.f);
			const float C = FMath::Clamp(FishPos, FishMin + R, FishMax - R);
			MoveB = C + MoveDir * R;
			MoveA = C - MoveDir * R;
			MoveP = FishPos;
			break;
		}
		case EFVExpertPattern::Circle:
		case EFVExpertPattern::Eight:
		{
			const float R = FMath::Min((Move.Pattern == EFVExpertPattern::Circle ? CircleAmp : EightAmp) * A, Mid - FishMin - 4.f);
			MoveA = FMath::Clamp(FishPos, FishMin + R, FishMax - R);   // centre
			MoveP = FishPos;                                            // where it blends from
			break;
		}
		case EFVExpertPattern::Hide:
			if (IsLegend()) { MoveB = Mid + MoveDir * 0.3f * TrackH; }
			else
			{
				// away from the bar, but within reach (the opposite end of the long track made the Magnet hopeless)
				const float Away = FishRng.FRandRange(HideDistMin, HideDistMax);
				const float Dir = Zones[0].Y < Mid ? 1.f : -1.f;
				MoveB = Zones[0].Y + Dir * Away;
			}
			MoveB = XClampFish(MoveB);
			break;
		case EFVExpertPattern::Decoy:
		{
			const float Dir = XRoomDir(FishPos, DecoyDist * A, MoveDir);
			GhostPos = FishPos;
			MoveA = FishPos;
			MoveB = XClampFish(FishPos + Dir * DecoyDist * A);
			MoveP = FMath::Clamp(Move.Param > 0.f ? Move.Param : Move.Seconds * 0.8f, 0.5f, FMath::Max(0.5f, Move.Seconds));
			++GhostSerial;
			break;
		}
		default:
			break;
		}
	}
	StageT += Dt;

	switch (Move.Pattern)
	{
	case EFVExpertPattern::Dash:
	{
		// «Рывок»: Param throws; each winds up (red, trembling) and bolts DashDist x Amp, alternating sides
		const int32 Count = FMath::Max(1, FMath::RoundToInt(Move.Param));
		const float Windup = DashWindup / FMath::Sqrt(S), Throw = DashThrow / S, Recover = DashRecover;
		if (MoveStage == 0)
		{
			if (StageT <= Dt * 1.5f)
			{
				const float Want = MoveCount == 0 ? MoveDir : -MoveP;
				MoveP = XRoomDir(FishPos, DashDist * A, Want);
				MoveA = FishPos;
				MoveB = XClampFish(FishPos + MoveP * DashDist * A);
			}
			bFishRed = true;
			bTrembling = true;
			if (StageT >= Windup) { MoveStage = 1; StageT = 0.f; MoveA = FishPos; ScriptPos = FishPos; }
		}
		else if (MoveStage == 1)
		{
			bFishRed = true;
			const float K = StageT / Throw;
			XScript(*this, FMath::Lerp(MoveA, MoveB, XEaseOut(K)));
			if (K >= 1.f)
			{
				++MoveCount;
				if (IsInZone(FishPos)) { Tension += RedJolt / TensionLimit(); }
				MoveStage = 2;
				StageT = 0.f;
			}
		}
		else if (MoveStage == 2)
		{
			bFishRed = StageT < Recover * 0.5f;
			if (StageT >= Recover)
			{
				StageT = 0.f;
				if (MoveCount < Count) { MoveStage = 0; }
				else { MoveStage = 3; XResumeClassic(*this); }
			}
		}
		else
		{
			XStepClassic(*this, S);
		}
		break;
	}
	case EFVExpertPattern::Jump:
	{
		// «Свеча»: winds up, leaps to the top, hangs above the water, falls to the landing point
		if (MoveStage == 0)
		{
			bTrembling = true;
			if (StageT >= JumpWindup) { MoveStage = 1; StageT = 0.f; MoveA = FishPos; }
		}
		else if (MoveStage == 1)
		{
			const float Rise = (0.15f + (MoveA - FishMin) / 2200.f) / S;
			const float K = StageT / Rise;
			FishPos = FMath::Lerp(MoveA, FishMin, FMath::Min(1.f, K * K));
			if (K >= 1.f) { MoveStage = 2; StageT = 0.f; FishPos = FishMin; }
		}
		else if (MoveStage == 2)
		{
			bFishAirborne = true;
			FishPos = FishMin;
			if (StageT >= JumpAir) { MoveStage = 3; StageT = 0.f; }
		}
		else if (MoveStage == 3)
		{
			const float K = StageT / (JumpFall / FMath::Sqrt(S));
			FishPos = XClampFish(FMath::Lerp(FishMin, MoveB, FMath::Min(1.f, K * K)));
			if (K >= 1.f) { MoveStage = 4; StageT = 0.f; XResumeClassic(*this); }
		}
		else
		{
			XStepClassic(*this, S);
		}
		break;
	}
	case EFVExpertPattern::Dive:
	{
		// «Нырок»: rushes to the bottom (mirror: the top), lies still, then explodes away (red)
		if (MoveStage == 0)
		{
			const float D = MoveA - FishPos;
			const float Stp = DiveSpeed * S * Dt;
			if (FMath::Abs(D) <= Stp) { FishPos = MoveA; MoveStage = 1; StageT = 0.f; }
			else { FishPos += XSign(D) * Stp; }
		}
		else if (MoveStage == 1)
		{
			bFrozen = true;
			bTrembling = StageT > MoveP - 0.35f;
			if (StageT >= MoveP) { MoveStage = 2; StageT = 0.f; MoveA = FishPos; ScriptPos = FishPos; }
		}
		else if (MoveStage == 2)
		{
			bFishRed = true;
			const float K = StageT / (DiveBolt / S);
			XScript(*this, FMath::Lerp(MoveA, MoveB, XEaseOut(K)));
			if (K >= 1.f) { MoveStage = 3; StageT = 0.f; XResumeClassic(*this); }
		}
		else
		{
			XStepClassic(*this, S);
		}
		break;
	}
	case EFVExpertPattern::Shuttle:
	{
		// «Челнок»: fast legs between two ends, a short pause (Param s) at each
		const float Leg = ShuttleLeg / S;
		const float Pause = FMath::Clamp(Move.Param, 0.f, 1.f) * 0.5f;
		if ((MoveStage & 1) == 0)
		{
			const float To = (MoveStage & 2) == 0 ? MoveB : MoveA;
			XScript(*this, FMath::Lerp(MoveP, To, XSmooth(StageT / Leg)));
			if (StageT >= Leg) { ++MoveStage; StageT = 0.f; MoveP = FishPos; ScriptPos = FishPos; }
		}
		else if (StageT >= Pause)
		{
			MoveStage = (MoveStage + 1) & 3;
			StageT = 0.f;
			MoveP = FishPos;
			ScriptPos = FishPos;
		}
		break;
	}
	case EFVExpertPattern::Circle:
	case EFVExpertPattern::Eight:
	{
		// 1D «Круг» / «Восьмёрка»: wide sweeps of the track (Eight: with a second harmonic)
		const bool bEight = Move.Pattern == EFVExpertPattern::Eight;
		const float R = FMath::Min((bEight ? EightAmp : CircleAmp) * A, Mid - FishMin - 4.f);
		const float W = 2.f * PI * S / (bEight ? EightPeriod : CirclePeriod);
		const float Tt = T + Dt;
		const float Wave = bEight ? (0.65f * FMath::Sin(W * Tt) + 0.35f * FMath::Sin(2.f * W * Tt)) / 0.9f : FMath::Sin(W * Tt);
		const float Curve = MoveA + MoveDir * R * Wave;
		XScript(*this, FMath::Lerp(MoveP, Curve, XSmooth(Tt / 0.25f)));
		break;
	}
	case EFVExpertPattern::Hide:
	{
		// «Прячется»: only bubbles; it slips to its hiding spot and idles there; shows again at the end
		const float D = MoveB - FishPos;
		const float Stp = HideSpeed * S * Dt;
		if (FMath::Abs(D) > 30.f) { FishPos += XSign(D) * FMath::Min(FMath::Abs(D), Stp); }
		else { FishPos = XClampFish(FishPos + FMath::Sin((T + Dt) * 3.1f) * 40.f * Dt); }
		const float Left = MoveLen - (T + Dt);
		FishAlpha = FMath::Min(FishAlpha, FMath::Max(0.1f, FMath::Max(1.f - (T + Dt) / 0.3f, 1.f - Left / 0.4f)));
		break;
	}
	case EFVExpertPattern::Decoy:
	{
		// «Обманка»: a double runs off the way of the move (full colour), the real fish fades to bubbles
		const float Tt = T + Dt;
		if (Tt < MoveP)
		{
			bGhost = true;
			const float K = Tt / 0.5f;
			GhostPos = K < 1.f ? FMath::Lerp(MoveA, MoveB, XEaseOut(K)) : XClampFish(MoveB + FMath::Sin((Tt - 0.5f) * 2.7f) * 40.f);
			FishAlpha = FMath::Min(FishAlpha, 0.25f);
		}
		XStepClassic(*this, S * 0.7f);
		break;
	}
	case EFVExpertPattern::Skittish:
		if (!XStepFlee(*this, A, S, Dt)) { XStepClassic(*this, S); }
		break;
	default:   // Snake: the classic motion (legend interludes: at the move's speed)
		XStepClassic(*this, S);
		break;
	}
}

FFVExpertFight::EResult FFVExpertFight::Step(const FFVExpertInput& In)
{
	using namespace FVExpertFightImpl;
	const float Dt = XDt;
	Elapsed += Dt;
	const int32 Ty = XType(*this);

	// ---- input: a tap inside one frame still holds for PressBufferSec (Android touch latency)
	if (In.bPressed)
	{
		PressBuffer = FVExpert::PressBufferSec;
		LastPressT = Elapsed;
	}
	FFVExpertInput E = In;
	E.bDown = In.bDown || PressBuffer > 0.f;
	PressBuffer = FMath::Max(0.f, PressBuffer - Dt);
	bMagnetHeld = Ty == 3 && E.bDown;
	HeldT = bMagnetHeld ? HeldT + Dt : 0.f;

	// ---- hazards, bars
	const float Lim = TensionLimit();
	const float TensionBefore = Tension;
	XStepHazards(*this, Dt);
	StepZones(E, Dt);
	bool bLoaded = Tension > TensionBefore;

	// ---- fish
	const float PrevPos = FishPos;
	FishAlpha = 1.f;
	bBubbles = false;
	bFishRed = false;
	bGhost = false;
	bFishAirborne = false;
	bFrozen = false;
	bTrembling = false;
	if (IsLegend())
	{
		const int32 CallsBefore = MoveCalls;
		const float SurgeBefore = SurgeT;
		FVExpertLegends::Step(*this, Dt);
		if (MoveCalls == CallsBefore && SurgeBefore < 0.f && SurgeT < 0.f)
		{
			XStepClassic(*this, BaseSpeedK * PhaseSpeed);
		}
	}
	else
	{
		XStepOrdinaryFish(*this, Dt);
	}
	if (Signal != EFVIcon::Count) { SignalT += Dt; }
	RedT = bFishRed ? RedT + Dt : 0.f;
	const float OwnVel = (FishPos - PrevPos) / Dt;

	// ---- outside forces on the fish: the spring lifts it, the Magnet pulls it to the bar
	if (!bFishAirborne && !bFrozen)
	{
		for (const FFVExpertHazardState& H : Hazards)
		{
			if (H.Kind == EFVHazard::Spring && H.Active > 0.f && H.Spans.Num() > 0 && FishPos >= H.Spans[0].X && FishPos <= H.Spans[0].Y)
			{
				FishPos -= SpringFish / XStability(*this) * Dt;
			}
		}
		if (bMagnetHeld)
		{
			const float D = Zones[0].Y - FishPos;
			if (FMath::Abs(D) > 0.5f)
			{
				FishPos += XSign(D) * FMath::Min(FMath::Abs(D), MagnetPull * Dt);
				// the fish fights the pull: its own run away from the bar loads the line (a red fish twice as hard)
				const float Away = -OwnVel * XSign(D);
				if (Away > 0.f)
				{
					Tension += 0.3f * FMath::Min(Away / 300.f, 2.f) * (bFishRed ? 2.f : 1.f) / Lim * Dt;
					bLoaded = true;
				}
			}
			// pulling without a break works the line like a heating drag (short pulls with rests cost almost nothing)
			if (HeldT > MagnetHeatAfter)
			{
				Tension += MagnetHeat * (HeldT - MagnetHeatAfter) / Lim * Dt;
				bLoaded = true;
			}
		}
		FishPos = XClampFish(FishPos);
	}
	FishVel = (FishPos - PrevPos) / Dt;

	// ---- visibility: weeds hide the fish (bubbles), darkness shows only the water near the bars
	if (XInSpan(*this, EFVHazard::Weeds, FishPos)) { FishAlpha = FMath::Min(FishAlpha, 0.15f); }
	if (!IsLit(FishPos)) { FishAlpha = 0.f; }
	if (bGhost && !IsLit(GhostPos)) { bGhost = false; }
	bBubbles = FishAlpha < 0.5f;

	// ---- meters
	bInZone = !bFishAirborne && IsInZone(FishPos);
	bInOverlap = bInZone && NumZones == 2 && IsInZone(FishPos, 0) && IsInZone(FishPos, 1);
	if (!bInZone && !bFishAirborne && Elapsed > 0.5f) { bFlawless = false; }
	if (bInZone)
	{
		float Mult = bInOverlap ? 1.5f : 1.f;
		if (bFishRed)
		{
			Mult *= 1.5f;
			const float RedK = (IsLegend() && SurgeT >= 0.f) ? LungeRedK : 1.f;
			Tension += RedTensionRate * RedK / Lim * Dt;
			bLoaded = true;
		}
		if (bMagnetHeld) { Mult *= MagnetHeldGain; }
		Progress += GainRate * Mult * Dt;
		// «Коряги»: a bar stuck in the piles with the fish in it loads the line
		for (int32 i = 0; i < NumZones; ++i)
		{
			if (Zones[i].Contains(FishPos) && XInSpan(*this, EFVHazard::Piles, Zones[i].Y))
			{
				Tension += PilesTension / Lim * Dt;
				bLoaded = true;
				break;
			}
		}
	}
	else if (!bFishRed && !bFishAirborne)
	{
		// letting a red fish go is safe; a legend pulls harder while a score move plays
		const float DrainK = (IsLegend() && MoveIndex >= 0) ? LegendMoveDrain : 1.f;
		Progress -= DrainRate * DrainK * Dt;
	}
	if (!bLoaded) { Tension -= FVExpert::TensionRelax * Dt; }
	Tension = FMath::Clamp(Tension, 0.f, 1.f);
	Progress = FMath::Clamp(Progress, 0.f, 1.f);
	if (Setup.bTutorial) { Progress = FMath::Max(Progress, 0.02f); }
	XUpdatePhase(*this);

	// ---- treasure: a chest drifts on the track; hold it in a bar to open it
	if (Setup.bHasTreasure && !bTreasureCaught)
	{
		if (!bTreasureVisible)
		{
			if (Elapsed >= TreasureAppear)
			{
				bTreasureVisible = true;
				TreasureProgress = 0.f;
				float P = Mid;
				for (int32 Try = 0; Try < 6; ++Try)
				{
					P = Rng.FRandRange(40.f, TrackH - 40.f);
					if (FMath::Abs(P - FishPos) > 150.f) { break; }
				}
				TreasurePos = P;
				TreasureVel = (Rng.FRand() < 0.5f ? -1.f : 1.f) * 25.f;
			}
		}
		else
		{
			TreasurePos += TreasureVel * Dt;
			if (TreasurePos < 40.f || TreasurePos > TrackH - 40.f) { TreasureVel = -TreasureVel; }
			TreasurePos = FMath::Clamp(TreasurePos, 40.f, TrackH - 40.f);
			TreasureProgress = FMath::Clamp(TreasureProgress + (IsInZone(TreasurePos) ? 0.35f : -0.15f) * Dt, 0.f, 1.f);
			if (TreasureProgress >= 1.f)
			{
				bTreasureCaught = true;
				bTreasureVisible = false;
			}
		}
	}

	// ---- the bots' memory (what a player could see this step)
	if (Seen.Num() > 0)
	{
		FFVExpertSeen& S = Seen[SeenHead % Seen.Num()];
		S.Pos = FishPos;
		S.Vel = FishVel;
		S.Ghost = GhostPos;
		S.bGhost = bGhost;
		S.bRed = bFishRed;
		S.bHidden = FishAlpha < 0.4f;
		S.bTip = SurgeT >= 0.f || (MoveIndex >= 0 && (Pattern == EFVExpertPattern::Dash || Pattern == EFVExpertPattern::Jump
			|| Pattern == EFVExpertPattern::Dive) && MoveStage <= 2);
		S.Tip = SurgeT >= 0.f ? SurgeTo : MoveB;
		SeenHead = (SeenHead + 1) % Seen.Num();
	}
	LastInput = In;

	// ---- result
	if (Tension >= 1.f)
	{
		if (Setup.bTutorial || Setup.bNoSnap) { Tension = 0.98f; }
		else { return EResult::Snapped; }
	}
	if (Progress >= 1.f) { return EResult::Caught; }
	if (Progress <= 0.f && !Setup.bTutorial) { return EResult::Escaped; }
	return EResult::Running;
}

// ============================================================================================
// Bots (§19: by the classic bots' model) — the bot sees the fish ReactionSec late (the Seen ring), believes a new course
// CourseLag late, aims with a drifting error, and its hand lands a moment late (MotorLag, overholds).
// ============================================================================================
FFVExpertInput FFVExpertFight::BotInput(const FFVExpertBot& Bot) const
{
	using namespace FVExpertFightImpl;
	const float Dt = XDt;
	const int32 Ty = XType(*this);
	FFVExpertInput Out;
	BotTapT += Dt;

	auto HoldEdges = [this](FFVExpertInput& O, bool bDown)
	{
		O.bDown = bDown;
		O.bPressed = bDown && !bBotPrevOut;
		O.bReleased = !bDown && bBotPrevOut;
		bBotPrevOut = bDown;
	};

	// ---- «сыр»: blind taps / holds (must fail)
	if (Bot.bCheese)
	{
		switch (Ty)
		{
		case 0:
		case 2:
		{
			const float Every = Ty == 0 ? 0.45f : 0.3f;
			const bool bTap = BotTapT >= Every;
			if (bTap) { BotTapT = 0.f; }
			HoldEdges(Out, bTap);
			break;
		}
		case 1: HoldEdges(Out, FMath::Fmod(Elapsed, 2.f) < 1.f); break;
		case 3: HoldEdges(Out, true); break;
		default: Out.TargetY = Mid; HoldEdges(Out, true); break;
		}
		return Out;
	}

	// ---- perception (ReactionSec late)
	const int32 N = Seen.Num();
	if (N <= 1) { return Out; }
	const int32 Late = FMath::Clamp(FMath::RoundToInt(Bot.ReactionSec * 60.f), 1, N - 1);
	const FFVExpertSeen& S = Seen[((SeenHead - 1 - Late) % N + N) % N];
	if (S.bGhost && BotGhostSerial != GhostSerial)
	{
		BotGhostSerial = GhostSerial;
		bBotFollowsGhost = !Bot.bKnowsScore && BotRng.FRand() < (1.f - Bot.Care) * 0.8f;
	}
	float P;
	if (S.bGhost && bBotFollowsGhost) { P = S.Ghost; }
	else if (S.bHidden)
	{
		BotLastSeen += (S.Pos - BotLastSeen) * FMath::Min(1.f, Dt * 6.f * BotBubbles);
		P = BotLastSeen;
	}
	else
	{
		P = S.Pos;
		BotLastSeen = S.Pos;
	}
	const float SeenVel = S.bHidden ? S.Vel * BotBubbles : S.Vel;
	BotVel += (SeenVel - BotVel) * FMath::Min(1.f, Dt / FMath::Max(0.02f, Bot.CourseLag));
	BotErrT -= Dt;
	if (BotErrT <= 0.f)
	{
		BotErrTarget = BotRng.FRandRange(-1.f, 1.f) * Bot.AimNoise * 1.2f;
		BotErrT = BotRng.FRandRange(0.5f, 1.1f);
	}
	BotErr += (BotErrTarget - BotErr) * FMath::Min(1.f, Dt * 3.f);
	const float Lead = Bot.ReactionSec * (0.4f + 0.6f * Bot.Anticipation) + 0.12f * Bot.Anticipation;
	float Aim = P + FMath::Clamp(BotVel, -900.f, 900.f) * Lead + BotErr;
	float Known = 0.f;
	if (Bot.bKnowsScore && FVExpertLegends::BotAim(*this, Bot, Known)) { Aim = FMath::Lerp(Aim, Known, 0.7f); }   // (a human's knowledge is not exact)
	Aim = FMath::Clamp(Aim, FishMin, FishMax);
	const float Look = 0.1f + 0.12f * Bot.Anticipation;

	// ---- the hand: a tap (Pendulum / Impulse) or a hold (Twin / Magnet) lands MotorLag late (+ overholds)
	auto WantTap = [&](bool bTap)
	{
		if (bTap && !bBotTapPending && BotTapT >= 0.1f)
		{
			bBotTapPending = true;
			BotDelayT = BotRng.FRandRange(0.f, Bot.MotorLag) + (BotRng.FRand() < Bot.OverholdChance * 0.5f ? Bot.OverholdSec : 0.f);
		}
		bool bNow = false;
		if (bBotTapPending)
		{
			BotDelayT -= Dt;
			if (BotDelayT <= 0.f) { bBotTapPending = false; BotTapT = 0.f; bNow = true; }
		}
		HoldEdges(Out, bNow);
	};
	auto WantHold = [&](bool bWant)
	{
		if (bWant != bBotWant)
		{
			bBotWant = bWant;
			BotDelayT = BotRng.FRandRange(0.f, Bot.MotorLag) + (!bWant && BotRng.FRand() < Bot.OverholdChance ? Bot.OverholdSec : 0.f);
		}
		if (BotDelayT >= 0.f)
		{
			BotDelayT -= Dt;
			if (BotDelayT < 0.f) { bBotOut = bBotWant; }
		}
		HoldEdges(Out, bBotOut);
	};

	switch (Ty)
	{
	case 0:   // Маятник: flip when the bar (with its gravity) will pass the aim
	{
		const FFVExpertZone& Z = Zones[0];
		const float G = bGravityUp ? -BarAccel : BarAccel;
		const float Yp = Z.Y + Z.Vel * Look + 0.5f * G * Look * Look;
		const float Hy = Z.H * 0.12f;
		const bool bWantUp = Yp > Aim + Hy ? true : (Yp < Aim - Hy ? false : bGravityUp);
		WantTap(bWantUp != bGravityUp);
		break;
	}
	case 1:   // Двойчатка: the bar nearer to the fish; held = bar 0 up, bar 1 down
	{
		// the bars roam the whole track: follow the one nearer to the aim (with a little hysteresis)
		const float D0 = FMath::Abs(Zones[0].Y - Aim), D1 = FMath::Abs(Zones[1].Y - Aim);
		if (BotTwinBar == 0 ? D1 + 40.f < D0 : D0 + 40.f < D1) { BotTwinBar = 1 - BotTwinBar; }
		const FFVExpertZone& Z = Zones[BotTwinBar];
		const float Yp = Z.Y + Z.Vel * Look;
		WantHold(BotTwinBar == 0 ? Yp > Aim : Yp < Aim);
		break;
	}
	case 2:   // Импульс: push when the falling bar will drop below the aim
	{
		const FFVExpertZone& Z = Zones[0];
		const float Yp = Z.Y + Z.Vel * Look + 0.5f * ImpulseGravity * Look * Look;
		WantTap(Yp > Aim + Z.H * 0.1f);
		break;
	}
	case 3:   // Магнит: pull while the fish is off the bar; rest the line; a careful hand lets a red fish go
	{
		const FFVExpertZone& Z = Zones[0];
		BotRestT = FMath::Max(0.f, BotRestT - Dt);
		if (bBotOut && HeldT > FMath::Lerp(2.6f, 1.1f, Bot.Care)) { BotRestT = FMath::Lerp(0.15f, 0.4f, Bot.Care); }
		const bool bOff = FMath::Abs(Aim - Z.Y) > Z.H * 0.5f - 14.f;
		// «знаток» knows a legend's opening lunge: holds through it (the pull keeps the fish), the others let the red go
		const bool bKnownLunge = Bot.bKnowsScore && SurgeT >= 0.f && Tension < 0.55f;
		const bool bRedLetGo = S.bRed && Bot.Care >= 0.5f && !bKnownLunge;
		WantHold((bOff || bKnownLunge) && BotRestT <= 0.f && !bRedLetGo && Tension < FMath::Lerp(0.9f, 0.6f, Bot.Care));
		break;
	}
	default:  // Ползунок: the finger follows the aim
	{
		// (leads the fish by the bar's own lag behind a moving finger: 2 zeta / omega)
		const float SAim = FMath::Clamp(Aim + FMath::Clamp(BotVel, -900.f, 900.f) * 2.f * SliderZeta / FMath::Max(1.f, SliderOmega), FishMin, FishMax);
		BotTargetY += (SAim - BotTargetY) * FMath::Min(1.f, Dt / (0.03f + Bot.MotorLag * 0.5f));
		Out.TargetY = BotTargetY;
		HoldEdges(Out, true);
		break;
	}
	}
	return Out;
}

FFVExpertSimResult FVSimulateExpert(const FFVExpertSetup& Setup, const FFVExpertBot& Bot, int32 Trials, int32 Seed)
{
	using namespace FVExpertFightImpl;
	FFVExpertSimResult Res;
	if (Trials <= 0) { return Res; }
	TArray<float> CaughtSec, AllSec, Scores;
	int32 Caught = 0, Snaps = 0;
	double InSteps = 0.0, AllSteps = 0.0;
	FFVExpertFight F;
	for (int32 i = 0; i < Trials; ++i)
	{
		F.Start(Setup, Seed + i * 7919);
		FFVExpertFight::EResult R = FFVExpertFight::EResult::Running;
		while (R == FFVExpertFight::EResult::Running && F.Elapsed < MaxFightSec)
		{
			R = F.Step(F.BotInput(Bot));
			AllSteps += 1.0;
			if (F.bInZone) { InSteps += 1.0; }
		}
		if (R == FFVExpertFight::EResult::Caught) { ++Caught; CaughtSec.Add(F.Elapsed); }
		else if (R == FFVExpertFight::EResult::Snapped) { ++Snaps; }
		AllSec.Add(F.Elapsed);
		Scores.Add(float(F.ScoreReached));
	}
	auto Median = [](TArray<float>& A) -> float
	{
		if (A.Num() == 0) { return 0.f; }
		A.Sort();
		const int32 H = A.Num() / 2;
		return (A.Num() % 2) ? A[H] : 0.5f * (A[H - 1] + A[H]);
	};
	Res.Rate = float(Caught) / float(Trials);
	Res.SnapRate = float(Snaps) / float(Trials);
	Res.InZoneFrac = AllSteps > 0.0 ? float(InSteps / AllSteps) : 0.f;
	Res.MedianSec = CaughtSec.Num() > 0 ? Median(CaughtSec) : Median(AllSec);
	Res.MedianScoreReached = Median(Scores);
	return Res;
}
