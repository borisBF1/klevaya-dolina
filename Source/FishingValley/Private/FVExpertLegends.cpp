// UPDATE 6 [EXPERT-LEGENDS] — the scores («партитуры») of the six expert legends and their 1D playback (§10.8, §19 Р60):
// a fixed order of moves, a random mirror «вверх / вниз» per move, ±10 % time / amplitude, a signal per move, the
// opening lunge of every move; «знаток» bot aim.
// Owner: EXPERT-LEGENDS.
//
// Playback state lives in the fight's own fields:
//   MoveIndex    — score step playing now (-1 = the interlude of the classic motion between moves)
//   MoveT        — seconds into the current move / interlude
//   ScoreReached — number of score steps started (next step = min(ScoreReached, Num - 1): after the whole score the
//                  legend keeps repeating its final move)
// Per-move jitter (time / amplitude / interlude length / speed / mirror) is a hash of the fight seed and the step:
// a fight is repeatable for a seed, and every fight still differs.

#include "FVExpertFight.h"

namespace FVExpertLegendsImpl
{
	FFVExpertMove M(EFVExpertPattern P, float Sec, float Speed, float Amp, float Param, EFVHazard Hz, const TCHAR* Name)
	{
		FFVExpertMove R;
		R.Pattern = P;
		R.Seconds = Sec;
		R.Speed = Speed;
		R.Amp = Amp;
		R.Param = Param;
		R.Hazard = Hz;
		R.Signal = FFVExpertFight::DefaultSignal(P);
		R.Name = Name;
		return R;
	}

	constexpr EFVHazard NoHz = EFVHazard::Count;

	/** The scores: the same moves in the same order as edition 1 (the collection «Приёмы» keeps its indices). */
	TArray<FFVExpertMove> Build(EFVExpertLegend L)
	{
		using P = EFVExpertPattern;
		TArray<FFVExpertMove> S;
		switch (L)
		{
		case EFVExpertLegend::KingMarlin:
			S.Add(M(P::Eight, 7.f, 1.0f, 1.0f, 0.f, NoHz, TEXT("Восьмёрка-разведка")));
			S.Add(M(P::Dash, 7.5f, 1.2f, 1.0f, 3.f, NoHz, TEXT("Три рывка по кругу")));
			S.Add(M(P::Jump, 3.5f, 1.0f, 1.1f, 0.f, NoHz, TEXT("Прыжок-свеча")));
			S.Add(M(P::Shuttle, 7.5f, 1.0f, 1.0f, 0.9f, EFVHazard::Wave, TEXT("Шторм и серия челноков")));
			S.Add(M(P::Circle, 6.f, 1.1f, 1.1f, 1.f, NoHz, TEXT("Круг над глубиной")));
			S.Add(M(P::Dash, 4.f, 1.4f, 1.2f, 1.f, NoHz, TEXT("Последний рывок")));
			break;
		case EFVExpertLegend::HarborMaster:
			S.Add(M(P::Circle, 7.f, 0.9f, 1.0f, 1.f, NoHz, TEXT("Обход гавани")));
			S.Add(M(P::Hide, 6.f, 0.8f, 1.0f, 0.f, EFVHazard::Piles, TEXT("Уход под сваи")));
			S.Add(M(P::Dash, 6.f, 1.1f, 1.0f, 2.f, NoHz, TEXT("Двойной рывок")));
			S.Add(M(P::Circle, 8.f, 1.2f, 0.8f, 2.f, EFVHazard::Whirlpool, TEXT("Водоворот у причала")));
			S.Add(M(P::Shuttle, 7.f, 1.1f, 1.0f, 0.6f, NoHz, TEXT("Челнок вдоль свай")));
			S.Add(M(P::Dash, 5.f, 1.3f, 1.2f, 2.f, NoHz, TEXT("Хозяйский бросок")));
			break;
		case EFVExpertLegend::GhostRay:
			S.Add(M(P::Skittish, 6.f, 1.0f, 1.0f, 0.f, NoHz, TEXT("Осторожный заход")));
			S.Add(M(P::Decoy, 7.f, 1.0f, 1.0f, 3.f, NoHz, TEXT("Призрачный двойник")));
			S.Add(M(P::Eight, 6.f, 1.1f, 1.1f, 0.f, NoHz, TEXT("Туманная восьмёрка")));
			S.Add(M(P::Hide, 6.f, 0.9f, 1.0f, 0.f, EFVHazard::Weeds, TEXT("Растворение в тумане")));
			S.Add(M(P::Decoy, 7.f, 1.2f, 1.1f, 2.5f, NoHz, TEXT("Два призрака")));
			S.Add(M(P::Dash, 4.f, 1.2f, 1.0f, 1.f, NoHz, TEXT("Взмах крыльями")));
			break;
		case EFVExpertLegend::BlindPatriarch:
			S.Add(M(P::Dive, 5.f, 0.9f, 1.0f, 0.f, NoHz, TEXT("Лёжка на дне")));
			S.Add(M(P::Shuttle, 7.f, 1.0f, 1.0f, 0.8f, NoHz, TEXT("Ощупывание стен")));
			S.Add(M(P::Circle, 7.f, 1.0f, 1.2f, 1.f, EFVHazard::Drip, TEXT("Круг под капелью")));
			S.Add(M(P::Dash, 6.f, 1.1f, 1.0f, 2.f, NoHz, TEXT("Два слепых броска")));
			S.Add(M(P::Dive, 5.f, 1.0f, 1.2f, 0.f, NoHz, TEXT("Ил и прыжок")));
			S.Add(M(P::Eight, 6.f, 1.1f, 1.0f, 0.f, NoHz, TEXT("Старый путь")));
			S.Add(M(P::Dash, 4.f, 1.3f, 1.2f, 1.f, NoHz, TEXT("Гнев патриарха")));
			break;
		case EFVExpertLegend::StoneGiant:
			S.Add(M(P::Shuttle, 7.f, 0.9f, 1.0f, 0.8f, NoHz, TEXT("Тяжёлый челнок")));
			S.Add(M(P::Dive, 5.f, 0.9f, 1.1f, 0.f, EFVHazard::Spring, TEXT("Нырок к ключу")));
			S.Add(M(P::Dash, 7.f, 1.0f, 1.2f, 2.f, NoHz, TEXT("Каменные удары")));
			S.Add(M(P::Jump, 4.f, 1.0f, 1.0f, 0.f, EFVHazard::Spring, TEXT("Взлёт на струе")));
			S.Add(M(P::Circle, 7.f, 1.0f, 1.1f, 1.f, NoHz, TEXT("Обход глыбы")));
			S.Add(M(P::Dash, 5.f, 1.2f, 1.3f, 1.f, NoHz, TEXT("Обвал")));
			break;
		case EFVExpertLegend::CrystalDragon:
			S.Add(M(P::Eight, 6.f, 1.1f, 1.0f, 0.f, NoHz, TEXT("Хрустальный танец")));
			S.Add(M(P::Decoy, 6.f, 1.1f, 1.0f, 2.5f, NoHz, TEXT("Отражение в кристалле")));
			S.Add(M(P::Dash, 6.f, 1.2f, 1.0f, 3.f, EFVHazard::Drip, TEXT("Звонкие рывки")));
			S.Add(M(P::Jump, 4.f, 1.0f, 1.1f, 0.f, NoHz, TEXT("Взлёт дракона")));
			S.Add(M(P::Skittish, 6.f, 1.2f, 1.0f, 0.f, NoHz, TEXT("Пугливые круги")));
			S.Add(M(P::Circle, 6.f, 1.3f, 1.0f, 2.f, NoHz, TEXT("Вихрь чешуи")));
			S.Add(M(P::Dash, 4.f, 1.4f, 1.2f, 1.f, NoHz, TEXT("Бросок дракона")));
			break;
		default:
			break;
		}
		return S;
	}

	/** Deterministic 0..1 from the fight seed, a step and a channel. */
	float Hash01(const FFVExpertFight& F, int32 Step, int32 Channel)
	{
		uint32 H = uint32(F.FishRng.GetInitialSeed()) * 2654435761u;
		H ^= uint32(Step + 1) * 40503u + uint32(Channel) * 97u;
		H ^= H >> 15; H *= 0x2C1B3C6Du; H ^= H >> 12; H *= 0x297A2D39u; H ^= H >> 15;
		return float(H & 0xFFFFFF) / float(0x1000000);
	}

	float Jit(const FFVExpertFight& F, int32 Step, int32 Channel) { return 0.9f + 0.2f * Hash01(F, Step, Channel); }

	/** The move with this fight's jitter for step Step (±10 % time — the tempo — and amplitude). */
	FFVExpertMove Jittered(const FFVExpertFight& F, const FFVExpertMove& Src, int32 Step)
	{
		FFVExpertMove R = Src;
		R.Seconds = Src.Seconds * Jit(F, Step, 1);
		R.Amp = Src.Amp * Jit(F, Step, 2);
		return R;
	}

	/** The interlude (classic motion) before pass Pass: 2..3.5 s at a random speed. */
	float InterludeLen(const FFVExpertFight& F, int32 Pass) { return Pass == 0 ? 2.5f : 2.f + 1.5f * Hash01(F, Pass, 3); }

	FFVExpertMove Interlude(const FFVExpertFight& F, int32 Pass)
	{
		FFVExpertMove R;
		R.Pattern = EFVExpertPattern::Snake;
		R.Seconds = InterludeLen(F, Pass);
		R.Speed = 0.8f + 0.3f * Hash01(F, Pass, 4);
		return R;
	}

	constexpr float WarnSec = 0.8f;   // the signal comes 0.8 s before the move (§10.6: 0.4–1 s)
	constexpr float LungeSec = 0.65f; // the opening lunge ends when the move starts (0.3 s gathering + 0.35 s bolt)
	constexpr float LungeDist = 150.f;

	/** Per legend (BALANCE_EXPERT: «новичок» strong 10–30 %, «знаток» strong 50–75 %, 40–70 s): x the progress a missed
	 *  opening lunge takes (x FVExpertFightImpl::LungeLoss), x the gain of the line, x the vigour (difficulty) of the base
	 *  motion. Index = EFVExpertLegend. */
	/** Р65 (owner 2026-09-27): the progress a legend takes back after its lunge is halved (the sweep below is before it).
	 *  Measured after (-fvbalanceonly, «новичок»-strong / «знаток»): Marlin 12/72, Harbor 57/72, GhostRay 50/77,
	 *  Patriarch 27/68, Giant 15/68, Dragon 33/60 (Harbor, GhostRay, Dragon now above the 10-30 % novice target — accepted). */
	constexpr float LungeLossHalf = 0.5f;
	float Tune[int32(EFVExpertLegend::Count)][3] =
	{
		{ 1.f, 1.f, 1.f },     // None
		{ 0.f, 1.4f, 1.f },    // KingMarlin      (BALANCE_EXPERT sweep, 60 fights: «новичок» ~13 %, «знаток» ~68 %, ~66 s)
		{ 6.f, 0.8f, 1.f },    // HarborMaster    (~13 % / ~47 %, ~47 s: on the Magnet the score helps little)
		{ 3.f, 0.9f, 1.f },    // GhostRay        (~22 % / ~57 %, ~54 s)
		{ 0.f, 1.3f, 1.f },    // BlindPatriarch  (~23 % / ~63 %, ~45 s)
		{ 0.f, 1.2f, 1.8f },   // StoneGiant      (~15 % / ~65 %, ~55 s)
		{ 2.f, 1.05f, 1.f },   // CrystalDragon   (~15 % / ~50 %, ~40 s)
	};
}

const TArray<FFVExpertMove>& FVExpertLegends::Score(EFVExpertLegend Legend)
{
	static TArray<FFVExpertMove> Scores[int32(EFVExpertLegend::Count)];
	static bool bBuilt = false;
	if (!bBuilt)
	{
		for (int32 i = 0; i < int32(EFVExpertLegend::Count); ++i) { Scores[i] = FVExpertLegendsImpl::Build(EFVExpertLegend(i)); }
		bBuilt = true;
	}
	const int32 Idx = FMath::Clamp(int32(Legend), 0, int32(EFVExpertLegend::Count) - 1);
	return Scores[Idx];
}

void FVExpertLegends::Start(FFVExpertFight& F)
{
	F.MoveIndex = -1;
	F.MoveT = 0.f;
	F.ScoreReached = 0;
	F.Mirror = F.FishRng.FRand() < 0.5f ? -1.f : 1.f;
	const int32 L = FMath::Clamp(int32(F.Setup.Fish.Legend), 0, int32(EFVExpertLegend::Count) - 1);
	F.LungeLossK = FVExpertLegendsImpl::Tune[L][0] * FVExpertLegendsImpl::LungeLossHalf;
	F.GainRate *= FVExpertLegendsImpl::Tune[L][1];
	F.FishDifficulty = FMath::Clamp(F.FishDifficulty * FVExpertLegendsImpl::Tune[L][2], 1.f, 150.f);
	// a random start point of the first interlude
	F.FishPos = FMath::Clamp(F.FishPos + F.FishRng.FRandRange(-60.f, 20.f), FVExpert::FishMin, FVExpert::FishMax);
	F.MoveA = F.MoveB = F.FishPos;
}

void FVExpertLegends::Step(FFVExpertFight& F, float Dt)
{
	using namespace FVExpertLegendsImpl;
	const TArray<FFVExpertMove>& S = Score(F.Setup.Fish.Legend);
	const int32 Num = S.Num();
	if (Num <= 0) { return; }   // no score: the fight plays the classic motion

	if (F.MoveIndex < 0)
	{
		// ---- the interlude between moves; the next move is announced WarnSec before it starts
		const int32 Pass = F.ScoreReached;
		const int32 Next = FMath::Min(Pass, Num - 1);
		const FFVExpertMove& NM = S[Next];
		const float Len = InterludeLen(F, Pass);
		if (F.MoveT >= Len - WarnSec && F.SignalName != NM.Name)
		{
			F.RaiseSignal(NM.Signal != EFVIcon::Count ? NM.Signal : FFVExpertFight::DefaultSignal(NM.Pattern), NM.Name);
		}
		// every score move opens with a lunge the way the move goes (its mirror): a player who knows the move already
		// waits there; one who does not is left behind with the bar in the wrong place
		if (F.SurgeT >= 0.f)
		{
			F.StepLunge(Dt);
			F.MoveT += Dt;
			return;
		}
		if (F.LungePass != Pass && F.MoveT >= Len - LungeSec)
		{
			F.LungePass = Pass;
			F.Mirror = Hash01(F, Pass, 5) < 0.5f ? -1.f : 1.f;   // random mirror «вверх / вниз» per move
			const float Dist = LungeDist * NM.Amp * (0.85f + 0.3f * Hash01(F, Pass, 6));
			float Dir = F.Mirror;
			const float To = F.FishPos + Dir * Dist;
			if (To < FVExpert::FishMin + 20.f || To > FVExpert::FishMax - 20.f) { Dir = -Dir; }   // no room: the other way
			F.StartLunge(F.FishPos + Dir * Dist);
			F.StepLunge(Dt);
			F.MoveT += Dt;
			return;
		}
		if (F.MoveT >= Len)
		{
			F.MoveIndex = Next;
			F.MoveT = 0.f;
			F.ScoreReached = FMath::Min(Pass + 1, Num);
			F.Mirror = Hash01(F, Pass, 5) < 0.5f ? -1.f : 1.f;
			F.StepMove(Jittered(F, NM, Next), 0.f, Dt);
			F.MoveT += Dt;
			return;
		}
		F.StepMove(Interlude(F, Pass), F.MoveT, Dt);
		F.MoveT += Dt;
		return;
	}

	// ---- a score move (its jitter is keyed by the step index: the same for the whole move)
	const int32 I = FMath::Clamp(F.MoveIndex, 0, Num - 1);
	const FFVExpertMove Mv = Jittered(F, S[I], I);
	if (F.MoveT >= Mv.Seconds)
	{
		F.MoveIndex = -1;
		F.MoveT = 0.f;
		F.Signal = EFVIcon::Count;
		F.SignalT = 0.f;
		F.SignalName.Reset();
		F.StepMove(Interlude(F, F.ScoreReached), 0.f, Dt);
		F.MoveT += Dt;
		return;
	}
	F.StepMove(Mv, F.MoveT, Dt);
	F.MoveT += Dt;
}

bool FVExpertLegends::BotAim(const FFVExpertFight& F, const FFVExpertBot& Bot, float& OutAimY)
{
	if (!Bot.bKnowsScore || !F.IsLegend()) { return false; }
	const float BarH = F.Zones[0].H;
	if (F.SurgeT >= 0.f)
	{
		// knows the opening lunge of the move: meets it where it ends (unless the line is already taut)
		if (F.Tension > 0.5f) { return false; }
		OutAimY = F.SurgeTo;
		return true;
	}
	if (F.MoveIndex < 0)
	{
		// knows which way the next move opens: during the warning leans the bar that way (the lunge's mirror)
		if (F.Signal != EFVIcon::Count)
		{
			const int32 Pass = F.ScoreReached;
			const float Dir = FVExpertLegendsImpl::Hash01(F, Pass, 5) < 0.5f ? -1.f : 1.f;
			OutAimY = F.FishPos + Dir * FVExpertLegendsImpl::LungeDist * 0.5f;
			return true;
		}
		return false;
	}
	switch (F.CurMove.Pattern)
	{
	case EFVExpertPattern::Dash:
		if (F.MoveStage <= 1)
		{
			// knows the throws and that letting a red fish go is safe: waits just short of the tip and takes the fish
			// when the throw is over
			OutAimY = F.MoveB - F.MoveP * (BarH * 0.5f - 14.f);
			return true;
		}
		return false;
	case EFVExpertPattern::Jump:
		// knows the «свеча»: leaves for the landing point as soon as the fish leaps
		if (F.MoveStage >= 1 && F.MoveStage <= 3) { OutAimY = F.MoveB; return true; }
		return false;
	case EFVExpertPattern::Dive:
		// stays on the fish while it lies still, then meets it where the bolt lands
		if (F.MoveStage == 2 || (F.MoveStage == 1 && F.StageT > F.MoveP - 0.35f)) { OutAimY = F.MoveB; return true; }
		return false;
	case EFVExpertPattern::Shuttle:
		OutAimY = (F.MoveA + F.MoveB) * 0.5f;   // covers both ends from the middle
		return true;
	case EFVExpertPattern::Hide:
		OutAimY = F.MoveB;                       // knows the hiding spot
		return true;
	default:
		// the rest (sweeps, the double...) is played by eye like any fish; «знаток» just never follows the double
		return false;
	}
}
