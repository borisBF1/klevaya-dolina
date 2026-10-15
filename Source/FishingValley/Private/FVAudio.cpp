// Fully synthesized audio (no assets).
//  * SFX: rendered once at startup into PCM16 mono 44.1 kHz buffers (oscillators, envelopes, filtered noise,
//    Karplus-Strong plucks, bell partials). Every Play() creates a small USoundWaveProcedural fed with the buffer.
//  * Music: a gentle procedural loop per theme (pentatonic melody, chords, bass, arps, soft percussion, echo),
//    rendered in small chunks on the game thread and queued ~1 s ahead into one streaming procedural wave.
//  * Rain ambience: filtered noise + droplets, streamed the same way.
// Everything degrades gracefully without an audio device (-nosound): nothing is spawned, nothing crashes.

#include "FVAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"

namespace FVSynth
{
	constexpr int32 SR = 44100;
	constexpr float TwoPi = 6.28318530718f;

	FORCEINLINE float MidiHz(float Midi) { return 440.f * FMath::Pow(2.f, (Midi - 69.f) / 12.f); }

	/** One-pole low-pass coefficient for a cutoff in Hz. */
	FORCEINLINE float OnePole(float Hz)
	{
		return Hz <= 0.f ? 1.f : 1.f - FMath::Exp(-TwoPi * FMath::Min(Hz, SR * 0.45f) / SR);
	}

	/** Gentle limiter: linear below 0.7, smooth saturation above. */
	FORCEINLINE float SoftClip(float X)
	{
		const float A = FMath::Abs(X);
		if (A < 0.7f)
		{
			return X;
		}
		const float Y = 0.7f + 0.3f * FMath::Tanh((A - 0.7f) / 0.3f);
		return X < 0.f ? -Y : Y;
	}

	/** Tiny deterministic PRNG (xorshift32) for audio-rate noise. */
	struct FNoise
	{
		uint32 S = 0x9E3779B9u;
		FNoise() = default;
		explicit FNoise(uint32 Seed) : S(Seed ? Seed : 0x9E3779B9u) {}
		FORCEINLINE uint32 Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
		FORCEINLINE float Uni() { return float(Next() & 0xFFFFFF) / float(0x1000000); }
		FORCEINLINE float Bi() { return Uni() * 2.f - 1.f; }
	};

	enum class EWave : uint8 { Sine, Triangle, Square, Saw, Pulse };

	FORCEINLINE float Osc(EWave W, float P)   // P in [0,1)
	{
		switch (W)
		{
		case EWave::Sine: return FMath::Sin(P * TwoPi);
		case EWave::Triangle: return P < 0.5f ? (4.f * P - 1.f) : (3.f - 4.f * P);
		case EWave::Square: return P < 0.5f ? 0.7f : -0.7f;
		case EWave::Saw: return 1.f - 2.f * P;
		case EWave::Pulse: return P < 0.25f ? 0.9f : -0.3f;   // 25% duty, zero mean
		default: return 0.f;
		}
	}

	// ------------------------------------------------------------------------------------------
	// One-shot building blocks (render into a float buffer)
	// ------------------------------------------------------------------------------------------
	struct FTone
	{
		float Start = 0.f, Dur = 0.1f, F0 = 440.f, F1 = 0.f, Glide = 0.f, Amp = 0.5f;
		EWave Wave = EWave::Sine;
		float A = 0.003f, D = 0.f, S = 1.f, R = 0.02f;
		float VibHz = 0.f, VibSemis = 0.f, VibDelay = 0.f;
		float LpHz = 0.f;

		FTone(float InStart, float InDur, float InF0, float InAmp, EWave InWave = EWave::Sine)
			: Start(InStart), Dur(InDur), F0(InF0), Amp(InAmp), Wave(InWave) {}
		FTone& To(float InF1, float InGlide = 0.f) { F1 = InF1; Glide = InGlide; return *this; }
		/** Attack (s), exponential decay time constant (s, 0 = none), sustain level, release (s). */
		FTone& Env(float InA, float InD, float InS, float InR) { A = InA; D = InD; S = InS; R = InR; return *this; }
		FTone& Vib(float Hz, float Semis, float Delay = 0.f) { VibHz = Hz; VibSemis = Semis; VibDelay = Delay; return *this; }
		FTone& Lp(float Hz) { LpHz = Hz; return *this; }
	};

	void AddTone(TArray<float>& B, const FTone& T)
	{
		const int32 S0 = FMath::RoundToInt(T.Start * SR);
		const int32 N = FMath::Max(1, FMath::RoundToInt(T.Dur * SR));
		const int32 AN = FMath::Max(1, FMath::RoundToInt(T.A * SR));
		const int32 RN = FMath::Max(1, FMath::RoundToInt(T.R * SR));
		const float Kd = T.D > 0.f ? FMath::Exp(-1.f / (T.D * SR)) : 1.f;
		const bool bGlide = T.F1 > 0.f && !FMath::IsNearlyEqual(T.F1, T.F0);
		const float GlideN = FMath::Max(1.f, (T.Glide > 0.f ? T.Glide : T.Dur) * SR);
		const float Kg = bGlide ? FMath::Exp(FMath::Loge(T.F1 / T.F0) / GlideN) : 1.f;
		const float LpA = OnePole(T.LpHz);

		float F = T.F0, Dec = 1.f, Ph = 0.f, Lp = 0.f;
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Idx = S0 + i;
			if (Idx >= B.Num())
			{
				break;
			}
			float E;
			if (i < AN)
			{
				E = float(i) / float(AN);
			}
			else
			{
				E = T.S + (1.f - T.S) * Dec;
				Dec *= Kd;
			}
			if (i > N - RN)
			{
				E *= float(N - i) / float(RN);
			}
			float Freq = F;
			if (bGlide && float(i) < GlideN)
			{
				F *= Kg;
			}
			if (T.VibHz > 0.f)
			{
				const float Tm = float(i) / SR;
				if (Tm > T.VibDelay)
				{
					Freq *= FMath::Pow(2.f, T.VibSemis / 12.f * FMath::Sin(TwoPi * T.VibHz * (Tm - T.VibDelay)));
				}
			}
			Ph += Freq / SR;
			Ph -= FMath::FloorToFloat(Ph);
			float V = Osc(T.Wave, Ph);
			if (T.LpHz > 0.f)
			{
				Lp += LpA * (V - Lp);
				V = Lp;
			}
			if (Idx >= 0)
			{
				B[Idx] += V * E * T.Amp;
			}
		}
	}

	struct FNoiseSpec
	{
		float Start = 0.f, Dur = 0.1f, Amp = 0.5f;
		float A = 0.002f, D = 0.05f, S = 0.f, R = 0.01f;
		float Lp0 = 0.f, Lp1 = 0.f, HpHz = 0.f;
		float Bp0 = 0.f, Bp1 = 0.f, Q = 1.f;
		uint32 Seed = 12345u;

		FNoiseSpec(float InStart, float InDur, float InAmp) : Start(InStart), Dur(InDur), Amp(InAmp) {}
		FNoiseSpec& Env(float InA, float InD, float InS, float InR) { A = InA; D = InD; S = InS; R = InR; return *this; }
		FNoiseSpec& Lp(float From, float ToHz = 0.f) { Lp0 = From; Lp1 = ToHz > 0.f ? ToHz : From; return *this; }
		FNoiseSpec& Hp(float Hz) { HpHz = Hz; return *this; }
		FNoiseSpec& Band(float From, float ToHz, float InQ) { Bp0 = From; Bp1 = ToHz; Q = InQ; return *this; }
		FNoiseSpec& WithSeed(uint32 InSeed) { Seed = InSeed; return *this; }
	};

	void AddNoise(TArray<float>& B, const FNoiseSpec& Sp)
	{
		const int32 S0 = FMath::RoundToInt(Sp.Start * SR);
		const int32 N = FMath::Max(1, FMath::RoundToInt(Sp.Dur * SR));
		const int32 AN = FMath::Max(1, FMath::RoundToInt(Sp.A * SR));
		const int32 RN = FMath::Max(1, FMath::RoundToInt(Sp.R * SR));
		const float Kd = Sp.D > 0.f ? FMath::Exp(-1.f / (Sp.D * SR)) : 1.f;
		const float HpK = Sp.HpHz > 0.f ? FMath::Exp(-TwoPi * Sp.HpHz / SR) : 0.f;

		FNoise Rng(Sp.Seed);
		float Dec = 1.f, Lp = 0.f, HpY = 0.f, HpX = 0.f, Low = 0.f, Bnd = 0.f;
		float LpA = OnePole(Sp.Lp0), BpF = 0.f;
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Idx = S0 + i;
			if (Idx >= B.Num())
			{
				break;
			}
			// Update swept filter coefficients every 32 samples.
			if ((i & 31) == 0)
			{
				const float Fr = float(i) / float(N);
				if (Sp.Lp0 > 0.f)
				{
					LpA = OnePole(Sp.Lp0 * FMath::Pow(Sp.Lp1 / Sp.Lp0, Fr));
				}
				if (Sp.Bp0 > 0.f)
				{
					const float Fc = Sp.Bp0 * FMath::Pow(Sp.Bp1 / Sp.Bp0, Fr);
					BpF = 2.f * FMath::Sin(PI * FMath::Min(Fc, SR * 0.16f) / SR);
				}
			}
			float E;
			if (i < AN)
			{
				E = float(i) / float(AN);
			}
			else
			{
				E = Sp.S + (1.f - Sp.S) * Dec;
				Dec *= Kd;
			}
			if (i > N - RN)
			{
				E *= float(N - i) / float(RN);
			}

			float X = Rng.Bi();
			if (Sp.Bp0 > 0.f)
			{
				// Chamberlin state-variable band-pass
				Low += BpF * Bnd;
				const float High = X - Low - Bnd / FMath::Max(0.3f, Sp.Q);
				Bnd += BpF * High;
				X = Bnd;
			}
			if (Sp.Lp0 > 0.f)
			{
				Lp += LpA * (X - Lp);
				X = Lp;
			}
			if (Sp.HpHz > 0.f)
			{
				const float Y = HpK * (HpY + X - HpX);
				HpX = X;
				HpY = Y;
				X = Y;
			}
			if (Idx >= 0)
			{
				B[Idx] += X * E * Sp.Amp;
			}
		}
	}

	/** Bell / chime: inharmonic sine partials with individual decays. */
	void AddBell(TArray<float>& B, float Start, float Hz, float Amp, float Decay)
	{
		static const float Ratio[] = { 1.f, 2.f, 2.76f, 4.07f, 5.4f };
		static const float Gain[] = { 1.f, 0.4f, 0.32f, 0.16f, 0.1f };
		static const float DecK[] = { 1.f, 0.7f, 0.45f, 0.3f, 0.2f };
		for (int32 k = 0; k < 5; ++k)
		{
			const float F = Hz * Ratio[k];
			if (F > SR * 0.45f)
			{
				continue;
			}
			const float D = Decay * DecK[k];
			AddTone(B, FTone(Start, D * 5.f + 0.02f, F, Amp * Gain[k]).Env(0.0015f, D, 0.f, 0.01f));
		}
	}

	/** Karplus-Strong plucked string. */
	void AddPluck(TArray<float>& B, float Start, float Hz, float Amp, float Dur, float Damp, uint32 Seed)
	{
		const int32 L = FMath::Max(2, FMath::RoundToInt(SR / FMath::Max(Hz, 20.f) - 0.5f));
		TArray<float> Line;
		Line.SetNumUninitialized(L);
		FNoise Rng(Seed);
		float Lp = 0.f, Mean = 0.f;
		for (int32 i = 0; i < L; ++i)
		{
			Lp += 0.6f * (Rng.Bi() - Lp);
			Line[i] = Lp;
			Mean += Lp;
		}
		Mean /= float(L);
		for (float& V : Line)
		{
			V -= Mean;
		}
		const int32 S0 = FMath::RoundToInt(Start * SR);
		const int32 N = FMath::Max(1, FMath::RoundToInt(Dur * SR));
		const int32 RN = FMath::Min(N, SR / 50);
		int32 P = 0;
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Idx = S0 + i;
			if (Idx >= B.Num())
			{
				break;
			}
			const float Out = Line[P];
			const int32 Nx = (P + 1) % L;
			Line[P] = (Out + Line[Nx]) * 0.5f * Damp;
			P = Nx;
			float E = 1.f;
			if (i > N - RN)
			{
				E = float(N - i) / float(RN);
			}
			if (Idx >= 0)
			{
				B[Idx] += Out * E * Amp;
			}
		}
	}

	TArray<float> MakeBuf(float Seconds)
	{
		TArray<float> B;
		B.SetNumZeroed(FMath::Max(1, FMath::RoundToInt(Seconds * SR)));
		return B;
	}

	/** Peak-normalizes to Gain, fades the tail and converts to PCM16. */
	void ToPcm(const TArray<float>& B, float Gain, TArray<int16>& Out)
	{
		float Peak = 0.f;
		for (float V : B)
		{
			Peak = FMath::Max(Peak, FMath::Abs(V));
		}
		const float K = Peak > 1e-5f ? Gain / Peak : 0.f;
		const int32 N = B.Num();
		const int32 Fade = FMath::Clamp(N / 4, 1, 256);
		Out.SetNumUninitialized(N);
		for (int32 i = 0; i < N; ++i)
		{
			float V = B[i] * K;
			if (i >= N - Fade)
			{
				V *= float(N - i) / float(Fade);
			}
			Out[i] = int16(FMath::Clamp(V, -1.f, 1.f) * 32000.f);
		}
	}

	// ------------------------------------------------------------------------------------------
	// Sound effects
	// ------------------------------------------------------------------------------------------
	void BuildSfx(EFVSfx Sfx, TArray<int16>& Out)
	{
		switch (Sfx)
		{
		case EFVSfx::Click:
		{
			TArray<float> B = MakeBuf(0.07f);
			AddTone(B, FTone(0.f, 0.05f, 1500.f, 0.6f).To(900.f, 0.03f).Env(0.001f, 0.012f, 0.f, 0.005f));
			AddTone(B, FTone(0.f, 0.06f, 700.f, 0.4f, EWave::Triangle).To(480.f, 0.05f).Env(0.001f, 0.02f, 0.f, 0.005f));
			AddNoise(B, FNoiseSpec(0.f, 0.008f, 0.25f).Env(0.0005f, 0.002f, 0.f, 0.002f).Hp(2500.f));
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Hover:
		{
			TArray<float> B = MakeBuf(0.05f);
			AddTone(B, FTone(0.f, 0.035f, MidiHz(98.f), 0.5f).Env(0.001f, 0.01f, 0.f, 0.005f));
			AddTone(B, FTone(0.f, 0.03f, MidiHz(86.f), 0.2f, EWave::Triangle).Env(0.001f, 0.008f, 0.f, 0.005f));
			ToPcm(B, 0.18f, Out);
			break;
		}
		case EFVSfx::Open:
		{
			TArray<float> B = MakeBuf(0.26f);
			AddTone(B, FTone(0.00f, 0.10f, MidiHz(74.f), 0.5f, EWave::Triangle).Env(0.002f, 0.05f, 0.f, 0.02f));
			AddTone(B, FTone(0.06f, 0.18f, MidiHz(81.f), 0.5f, EWave::Triangle).Env(0.002f, 0.07f, 0.f, 0.03f));
			AddTone(B, FTone(0.06f, 0.16f, MidiHz(93.f), 0.12f).Env(0.002f, 0.05f, 0.f, 0.03f));
			AddNoise(B, FNoiseSpec(0.f, 0.1f, 0.12f).Env(0.02f, 0.03f, 0.f, 0.02f).Lp(4000.f).Hp(900.f).WithSeed(71u));
			ToPcm(B, 0.45f, Out);
			break;
		}
		case EFVSfx::Close:
		{
			TArray<float> B = MakeBuf(0.24f);
			AddTone(B, FTone(0.00f, 0.09f, MidiHz(81.f), 0.45f, EWave::Triangle).Env(0.002f, 0.045f, 0.f, 0.02f));
			AddTone(B, FTone(0.055f, 0.16f, MidiHz(74.f), 0.45f, EWave::Triangle).Env(0.002f, 0.06f, 0.f, 0.03f));
			AddNoise(B, FNoiseSpec(0.f, 0.08f, 0.1f).Env(0.015f, 0.025f, 0.f, 0.02f).Lp(3000.f).Hp(700.f).WithSeed(72u));
			ToPcm(B, 0.36f, Out);
			break;
		}
		case EFVSfx::Cast:
		{
			TArray<float> B = MakeBuf(0.5f);
			AddNoise(B, FNoiseSpec(0.f, 0.42f, 0.9f).Env(0.12f, 0.12f, 0.f, 0.08f).Band(350.f, 2600.f, 1.4f).WithSeed(11u));
			uint32 Seed = 200u;
			for (float T = 0.05f; T < 0.40f; T += 0.017f)
			{
				const float K = 1.f - (T - 0.05f) / 0.4f;
				AddNoise(B, FNoiseSpec(T, 0.006f, 0.22f * K).Env(0.0005f, 0.0015f, 0.f, 0.001f).Hp(3000.f).WithSeed(++Seed));
			}
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Splash:
		{
			TArray<float> B = MakeBuf(0.6f);
			AddTone(B, FTone(0.f, 0.08f, 300.f, 0.8f).To(1100.f, 0.06f).Env(0.002f, 0.035f, 0.f, 0.01f));
			AddNoise(B, FNoiseSpec(0.005f, 0.45f, 0.6f).Env(0.004f, 0.11f, 0.f, 0.05f).Lp(3000.f, 900.f).Hp(250.f).WithSeed(21u));
			const float DropT[] = { 0.13f, 0.21f, 0.33f };
			const float DropF[] = { 900.f, 1300.f, 1050.f };
			for (int32 i = 0; i < 3; ++i)
			{
				AddTone(B, FTone(DropT[i], 0.035f, DropF[i], 0.22f - 0.04f * i).To(DropF[i] * 1.8f, 0.02f).Env(0.001f, 0.012f, 0.f, 0.005f));
			}
			ToPcm(B, 0.6f, Out);
			break;
		}
		case EFVSfx::Bite:
		{
			TArray<float> B = MakeBuf(0.9f);
			AddNoise(B, FNoiseSpec(0.f, 0.01f, 0.4f).Env(0.0005f, 0.003f, 0.f, 0.002f).Hp(4000.f).WithSeed(31u));
			AddBell(B, 0.f, MidiHz(88.f), 0.8f, 0.28f);
			AddBell(B, 0.f, MidiHz(95.f), 0.35f, 0.18f);
			AddTone(B, FTone(0.f, 0.3f, MidiHz(76.f), 0.25f, EWave::Triangle).Env(0.001f, 0.08f, 0.f, 0.02f));
			ToPcm(B, 0.7f, Out);
			break;
		}
		case EFVSfx::Hook:
		{
			TArray<float> B = MakeBuf(0.35f);
			AddTone(B, FTone(0.f, 0.16f, 170.f, 0.9f).To(70.f, 0.12f).Env(0.001f, 0.05f, 0.f, 0.02f));
			AddTone(B, FTone(0.02f, 0.13f, 420.f, 0.3f, EWave::Saw).To(1500.f, 0.1f).Env(0.005f, 0.05f, 0.f, 0.02f).Lp(3500.f));
			AddNoise(B, FNoiseSpec(0.f, 0.03f, 0.35f).Env(0.0005f, 0.008f, 0.f, 0.005f).Hp(1800.f).WithSeed(41u));
			ToPcm(B, 0.6f, Out);
			break;
		}
		case EFVSfx::ReelTick:
		{
			TArray<float> B = MakeBuf(0.03f);
			AddNoise(B, FNoiseSpec(0.f, 0.02f, 0.8f).Env(0.0003f, 0.004f, 0.f, 0.003f).Hp(2500.f).Lp(8000.f).WithSeed(51u));
			AddTone(B, FTone(0.f, 0.015f, 2600.f, 0.3f, EWave::Triangle).Env(0.0005f, 0.004f, 0.f, 0.003f));
			ToPcm(B, 0.3f, Out);
			break;
		}
		case EFVSfx::Catch:
		{
			TArray<float> B = MakeBuf(1.25f);
			const float Arp[] = { 72.f, 76.f, 79.f, 84.f };
			for (int32 i = 0; i < 4; ++i)
			{
				const float T = 0.08f * i;
				AddTone(B, FTone(T, 0.25f, MidiHz(Arp[i]), 0.35f, EWave::Pulse).Env(0.003f, 0.1f, 0.15f, 0.05f).Lp(3500.f));
				AddTone(B, FTone(T, 0.25f, MidiHz(Arp[i] + 12.f), 0.12f).Env(0.003f, 0.08f, 0.f, 0.05f));
			}
			const float Chord[] = { 76.f, 79.f, 84.f };
			for (float Note : Chord)
			{
				AddTone(B, FTone(0.34f, 0.85f, MidiHz(Note), 0.22f, EWave::Triangle).Env(0.01f, 0.35f, 0.25f, 0.25f).Vib(5.5f, 0.08f, 0.2f));
			}
			AddBell(B, 0.34f, MidiHz(96.f), 0.2f, 0.3f);
			ToPcm(B, 0.6f, Out);
			break;
		}
		case EFVSfx::Perfect:
		{
			TArray<float> B = MakeBuf(1.2f);
			const float Notes[] = { 84.f, 88.f, 91.f, 96.f, 100.f, 103.f };
			for (int32 i = 0; i < 6; ++i)
			{
				AddBell(B, 0.055f * i, MidiHz(Notes[i]), 0.45f, 0.22f);
			}
			AddBell(B, 0.36f, MidiHz(108.f), 0.3f, 0.35f);
			AddNoise(B, FNoiseSpec(0.f, 0.8f, 0.12f).Env(0.05f, 0.25f, 0.f, 0.2f).Hp(6000.f).WithSeed(61u));
			ToPcm(B, 0.55f, Out);
			break;
		}
		case EFVSfx::Escape:
		{
			TArray<float> B = MakeBuf(1.15f);
			const float Notes[] = { 67.f, 66.f, 65.f };
			for (int32 i = 0; i < 3; ++i)
			{
				const float T = 0.2f * i;
				AddTone(B, FTone(T, 0.2f, MidiHz(Notes[i]), 0.5f, EWave::Triangle).Env(0.01f, 0.15f, 0.5f, 0.04f));
				AddTone(B, FTone(T, 0.2f, MidiHz(Notes[i] - 12.f), 0.2f).Env(0.01f, 0.15f, 0.5f, 0.04f));
			}
			AddTone(B, FTone(0.6f, 0.52f, MidiHz(64.f), 0.5f, EWave::Triangle).To(MidiHz(60.f), 0.45f).Env(0.01f, 0.3f, 0.4f, 0.12f).Vib(6.f, 0.3f, 0.1f));
			AddTone(B, FTone(0.6f, 0.52f, MidiHz(52.f), 0.2f).To(MidiHz(48.f), 0.45f).Env(0.01f, 0.3f, 0.4f, 0.12f));
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Coin:
		{
			TArray<float> B = MakeBuf(0.5f);
			AddTone(B, FTone(0.f, 0.075f, MidiHz(83.f), 0.35f, EWave::Square).Env(0.001f, 0.05f, 0.5f, 0.005f).Lp(5000.f));
			AddTone(B, FTone(0.07f, 0.42f, MidiHz(88.f), 0.35f, EWave::Square).Env(0.001f, 0.12f, 0.f, 0.05f).Lp(5000.f));
			AddBell(B, 0.07f, MidiHz(100.f), 0.12f, 0.15f);
			ToPcm(B, 0.42f, Out);
			break;
		}
		case EFVSfx::Buy:
		{
			TArray<float> B = MakeBuf(0.85f);
			const float Notes[] = { 79.f, 84.f, 88.f, 91.f };
			for (int32 i = 0; i < 4; ++i)
			{
				AddBell(B, 0.07f * i, MidiHz(Notes[i]), 0.5f, 0.3f);
			}
			AddNoise(B, FNoiseSpec(0.2f, 0.45f, 0.08f).Env(0.02f, 0.15f, 0.f, 0.1f).Hp(7000.f).WithSeed(81u));
			ToPcm(B, 0.55f, Out);
			break;
		}
		case EFVSfx::Error:
		{
			TArray<float> B = MakeBuf(0.32f);
			AddTone(B, FTone(0.f, 0.12f, 155.f, 0.5f, EWave::Square).Env(0.003f, 0.08f, 0.6f, 0.02f).Lp(900.f));
			AddTone(B, FTone(0.f, 0.12f, 158.f, 0.3f, EWave::Saw).Env(0.003f, 0.08f, 0.6f, 0.02f).Lp(700.f));
			AddTone(B, FTone(0.15f, 0.14f, 140.f, 0.5f, EWave::Square).Env(0.003f, 0.08f, 0.6f, 0.03f).Lp(900.f));
			AddTone(B, FTone(0.15f, 0.14f, 143.f, 0.3f, EWave::Saw).Env(0.003f, 0.08f, 0.6f, 0.03f).Lp(700.f));
			ToPcm(B, 0.42f, Out);
			break;
		}
		case EFVSfx::LevelUp:
		{
			TArray<float> B = MakeBuf(1.75f);
			const float Run[] = { 67.f, 72.f, 76.f, 79.f };
			for (int32 i = 0; i < 4; ++i)
			{
				AddTone(B, FTone(0.1f * i, 0.13f, MidiHz(Run[i]), 0.35f, EWave::Pulse).Env(0.003f, 0.08f, 0.3f, 0.03f).Lp(4000.f));
			}
			AddTone(B, FTone(0.42f, 1.1f, MidiHz(84.f), 0.35f, EWave::Pulse).Env(0.005f, 0.4f, 0.45f, 0.35f).Vib(5.5f, 0.12f, 0.25f).Lp(4200.f));
			const float Pad[] = { 72.f, 76.f, 79.f };
			for (float Note : Pad)
			{
				AddTone(B, FTone(0.42f, 1.2f, MidiHz(Note), 0.14f, EWave::Triangle).Env(0.06f, 0.6f, 0.3f, 0.4f));
			}
			AddBell(B, 0.5f, MidiHz(96.f), 0.15f, 0.2f);
			AddBell(B, 0.6f, MidiHz(100.f), 0.15f, 0.2f);
			AddBell(B, 0.7f, MidiHz(103.f), 0.15f, 0.25f);
			ToPcm(B, 0.65f, Out);
			break;
		}
		case EFVSfx::Treasure:
		{
			TArray<float> B = MakeBuf(1.25f);
			const float Up[] = { 84.f, 86.f, 88.f, 91.f, 93.f, 96.f, 98.f, 100.f, 103.f, 105.f };
			for (int32 i = 0; i < 10; ++i)
			{
				AddTone(B, FTone(0.045f * i, 0.09f, MidiHz(Up[i]), 0.3f).Env(0.001f, 0.035f, 0.f, 0.02f));
			}
			AddBell(B, 0.5f, MidiHz(88.f), 0.35f, 0.4f);
			AddBell(B, 0.5f, MidiHz(92.f), 0.3f, 0.4f);
			AddBell(B, 0.5f, MidiHz(95.f), 0.3f, 0.4f);
			AddNoise(B, FNoiseSpec(0.45f, 0.6f, 0.1f).Env(0.03f, 0.2f, 0.f, 0.15f).Hp(6500.f).WithSeed(91u));
			ToPcm(B, 0.55f, Out);
			break;
		}
		case EFVSfx::Achievement:
		{
			TArray<float> B = MakeBuf(1.7f);
			auto Brass = [&B](float T, float Dur, float Note, float Amp)
			{
				AddTone(B, FTone(T, Dur, MidiHz(Note), Amp, EWave::Saw).Env(0.015f, 0.1f, 0.7f, 0.04f).Lp(2200.f));
				AddTone(B, FTone(T, Dur, MidiHz(Note) * 1.003f, Amp * 0.5f, EWave::Saw).Env(0.015f, 0.1f, 0.7f, 0.04f).Lp(1800.f));
			};
			Brass(0.f, 0.11f, 72.f, 0.35f);
			Brass(0.13f, 0.08f, 72.f, 0.3f);
			Brass(0.23f, 0.08f, 72.f, 0.3f);
			AddTone(B, FTone(0.34f, 1.0f, MidiHz(79.f), 0.35f, EWave::Saw).Env(0.02f, 0.5f, 0.5f, 0.35f).Vib(5.f, 0.12f, 0.3f).Lp(2400.f));
			AddTone(B, FTone(0.34f, 1.0f, MidiHz(76.f), 0.18f, EWave::Saw).Env(0.02f, 0.5f, 0.5f, 0.35f).Lp(2000.f));
			AddTone(B, FTone(0.34f, 1.0f, MidiHz(72.f), 0.18f, EWave::Saw).Env(0.02f, 0.5f, 0.5f, 0.35f).Lp(2000.f));
			AddTone(B, FTone(0.34f, 1.0f, MidiHz(60.f), 0.25f, EWave::Triangle).Env(0.02f, 0.5f, 0.4f, 0.35f));
			AddNoise(B, FNoiseSpec(0.34f, 0.9f, 0.15f).Env(0.002f, 0.35f, 0.f, 0.1f).Hp(5000.f).WithSeed(101u));
			ToPcm(B, 0.6f, Out);
			break;
		}
		case EFVSfx::Step:
		{
			TArray<float> B = MakeBuf(0.09f);
			AddNoise(B, FNoiseSpec(0.f, 0.07f, 0.8f).Env(0.002f, 0.022f, 0.f, 0.01f).Lp(1100.f).Hp(120.f).WithSeed(111u));
			AddTone(B, FTone(0.f, 0.05f, 120.f, 0.35f).To(70.f, 0.04f).Env(0.002f, 0.018f, 0.f, 0.01f));
			ToPcm(B, 0.2f, Out);
			break;
		}
		case EFVSfx::Equip:
		{
			TArray<float> B = MakeBuf(0.32f);
			const float T[] = { 0.f, 0.06f, 0.13f };
			const float A[] = { 0.5f, 0.4f, 0.35f };
			for (int32 i = 0; i < 3; ++i)
			{
				AddNoise(B, FNoiseSpec(T[i], 0.05f, A[i]).Env(0.003f, 0.02f, 0.f, 0.01f).Lp(7000.f).Hp(1500.f).WithSeed(121u + i));
			}
			AddTone(B, FTone(0.19f, 0.05f, 900.f, 0.35f, EWave::Triangle).Env(0.001f, 0.012f, 0.f, 0.005f));
			AddTone(B, FTone(0.19f, 0.05f, 1350.f, 0.2f).Env(0.001f, 0.01f, 0.f, 0.005f));
			ToPcm(B, 0.42f, Out);
			break;
		}
		// ---------------------------------------------------------------- update 2
		case EFVSfx::Pickup:
		{
			// quick rising "blip" + a tiny sparkle: something small went into the backpack
			TArray<float> B = MakeBuf(0.24f);
			AddTone(B, FTone(0.f, 0.07f, MidiHz(79.f), 0.45f, EWave::Triangle).To(MidiHz(86.f), 0.05f).Env(0.001f, 0.03f, 0.f, 0.01f));
			AddTone(B, FTone(0.055f, 0.1f, MidiHz(91.f), 0.3f).Env(0.001f, 0.04f, 0.f, 0.02f));
			AddBell(B, 0.06f, MidiHz(98.f), 0.12f, 0.06f);
			AddNoise(B, FNoiseSpec(0.f, 0.03f, 0.15f).Env(0.001f, 0.01f, 0.f, 0.005f).Hp(3000.f).WithSeed(131u));
			ToPcm(B, 0.4f, Out);
			break;
		}
		case EFVSfx::Dig:
		{
			// shovel into soft soil: a dull thump + gritty crunch
			TArray<float> B = MakeBuf(0.3f);
			AddTone(B, FTone(0.f, 0.14f, 92.f, 0.9f).To(50.f, 0.1f).Env(0.002f, 0.05f, 0.f, 0.03f));
			AddNoise(B, FNoiseSpec(0.f, 0.22f, 0.7f).Env(0.003f, 0.06f, 0.f, 0.05f).Lp(900.f, 500.f).Hp(90.f).WithSeed(132u));
			AddNoise(B, FNoiseSpec(0.02f, 0.12f, 0.25f).Env(0.002f, 0.03f, 0.f, 0.02f).Band(1800.f, 900.f, 1.2f).WithSeed(133u));
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Chop:
		{
			// knife swipe + the thunk on the board
			TArray<float> B = MakeBuf(0.2f);
			AddNoise(B, FNoiseSpec(0.f, 0.08f, 0.6f).Env(0.01f, 0.03f, 0.f, 0.02f).Band(2000.f, 6000.f, 1.6f).WithSeed(134u));
			AddTone(B, FTone(0.07f, 0.07f, 240.f, 0.7f, EWave::Triangle).To(140.f, 0.05f).Env(0.001f, 0.02f, 0.f, 0.01f));
			AddNoise(B, FNoiseSpec(0.07f, 0.02f, 0.3f).Env(0.0005f, 0.006f, 0.f, 0.004f).Hp(2500.f).WithSeed(135u));
			ToPcm(B, 0.45f, Out);
			break;
		}
		case EFVSfx::Sizzle:
		{
			// short frying hiss with fat pops (retriggered while cooking)
			TArray<float> B = MakeBuf(0.55f);
			AddNoise(B, FNoiseSpec(0.f, 0.52f, 0.5f).Env(0.04f, 0.3f, 0.6f, 0.12f).Hp(3000.f).Lp(9000.f).WithSeed(136u));
			FNoise R(149u);
			uint32 Seed = 300u;
			for (float T = 0.03f; T < 0.45f; T += 0.045f + 0.035f * R.Uni())
			{
				const float Amp = 0.25f + 0.2f * R.Uni();
				AddNoise(B, FNoiseSpec(T, 0.006f, Amp).Env(0.0003f, 0.0015f, 0.f, 0.001f).Hp(4000.f).WithSeed(++Seed));
			}
			ToPcm(B, 0.3f, Out);
			break;
		}
		case EFVSfx::Flip:
		{
			// whoosh up + a soft landing in the pan
			TArray<float> B = MakeBuf(0.36f);
			AddNoise(B, FNoiseSpec(0.f, 0.2f, 0.6f).Env(0.06f, 0.06f, 0.f, 0.05f).Band(400.f, 1800.f, 1.3f).WithSeed(137u));
			AddTone(B, FTone(0.2f, 0.1f, 180.f, 0.6f).To(95.f, 0.07f).Env(0.002f, 0.03f, 0.f, 0.02f));
			AddNoise(B, FNoiseSpec(0.2f, 0.12f, 0.3f).Env(0.002f, 0.04f, 0.f, 0.03f).Hp(3000.f).Lp(8000.f).WithSeed(138u));
			ToPcm(B, 0.42f, Out);
			break;
		}
		case EFVSfx::Hammer:
		{
			// wooden-metal knock with a short ring (building / crafting)
			TArray<float> B = MakeBuf(0.45f);
			AddNoise(B, FNoiseSpec(0.f, 0.02f, 0.7f).Env(0.0003f, 0.005f, 0.f, 0.004f).Hp(1500.f).WithSeed(139u));
			AddTone(B, FTone(0.f, 0.09f, 160.f, 0.7f, EWave::Triangle).To(110.f, 0.06f).Env(0.001f, 0.025f, 0.f, 0.02f));
			AddBell(B, 0.002f, MidiHz(76.f), 0.35f, 0.08f);
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Knot:
		{
			// a rope pluck (Karplus-Strong) + a little creak of the fibre
			TArray<float> B = MakeBuf(0.5f);
			AddPluck(B, 0.f, 110.f, 0.8f, 0.48f, 0.992f, 140u);
			AddPluck(B, 0.f, 165.f, 0.3f, 0.3f, 0.985f, 141u);
			AddNoise(B, FNoiseSpec(0.f, 0.05f, 0.2f).Env(0.004f, 0.015f, 0.f, 0.01f).Band(900.f, 1400.f, 2.f).WithSeed(142u));
			ToPcm(B, 0.45f, Out);
			break;
		}
		case EFVSfx::NetTear:
		{
			// ripping mesh: a falling band-passed tear with snapping threads
			TArray<float> B = MakeBuf(0.5f);
			AddNoise(B, FNoiseSpec(0.f, 0.42f, 0.8f).Env(0.01f, 0.18f, 0.2f, 0.08f).Band(3000.f, 600.f, 3.f).WithSeed(143u));
			uint32 Seed = 400u;
			for (float T = 0.02f; T < 0.36f; T += 0.045f)
			{
				AddNoise(B, FNoiseSpec(T, 0.008f, 0.4f * (1.f - T)).Env(0.0003f, 0.002f, 0.f, 0.002f).Hp(2000.f).WithSeed(++Seed));
			}
			AddPluck(B, 0.05f, 330.f, 0.2f, 0.2f, 0.97f, 144u);
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Door:
		{
			// wooden door: creak, then two soft low knocks as it closes
			TArray<float> B = MakeBuf(0.62f);
			AddTone(B, FTone(0.f, 0.26f, 420.f, 0.14f, EWave::Saw).To(310.f, 0.24f).Env(0.03f, 0.1f, 0.5f, 0.05f).Vib(18.f, 0.6f).Lp(1400.f));
			AddTone(B, FTone(0.3f, 0.12f, 120.f, 0.8f, EWave::Triangle).To(80.f, 0.08f).Env(0.001f, 0.035f, 0.f, 0.02f));
			AddTone(B, FTone(0.42f, 0.14f, 96.f, 0.55f, EWave::Triangle).To(66.f, 0.1f).Env(0.001f, 0.04f, 0.f, 0.03f));
			AddNoise(B, FNoiseSpec(0.3f, 0.06f, 0.3f).Env(0.001f, 0.015f, 0.f, 0.01f).Lp(1200.f).WithSeed(145u));
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Sleep:
		{
			// descending music-box bells, then a soft low chord (good night)
			TArray<float> B = MakeBuf(1.9f);
			const float Notes[] = { 91.f, 88.f, 84.f, 79.f, 76.f, 72.f };
			for (int32 i = 0; i < 6; ++i)
			{
				AddBell(B, 0.14f * i, MidiHz(Notes[i]), 0.42f - 0.03f * i, 0.3f);
			}
			const float Chord[] = { 60.f, 64.f, 67.f };
			for (float Note : Chord)
			{
				AddTone(B, FTone(0.8f, 1.05f, MidiHz(Note), 0.14f, EWave::Triangle).Env(0.12f, 0.5f, 0.3f, 0.35f).Vib(4.5f, 0.06f, 0.3f));
			}
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Radio:
		{
			// tuning static that settles on a station: two 1 kHz beeps
			TArray<float> B = MakeBuf(0.8f);
			AddNoise(B, FNoiseSpec(0.f, 0.42f, 0.55f).Env(0.02f, 0.2f, 0.3f, 0.08f).Band(900.f, 2600.f, 1.1f).WithSeed(146u));
			AddTone(B, FTone(0.f, 0.36f, 700.f, 0.12f, EWave::Square).To(1300.f, 0.34f).Env(0.02f, 0.2f, 0.4f, 0.05f).Lp(2500.f));
			AddTone(B, FTone(0.46f, 0.08f, 1000.f, 0.4f).Env(0.002f, 0.f, 1.f, 0.01f));
			AddTone(B, FTone(0.62f, 0.12f, 1000.f, 0.4f).Env(0.002f, 0.f, 1.f, 0.015f));
			ToPcm(B, 0.42f, Out);
			break;
		}
		case EFVSfx::Fire:
		{
			// crackling wood fire: low rumble + random micro-clicks
			TArray<float> B = MakeBuf(0.6f);
			AddNoise(B, FNoiseSpec(0.f, 0.58f, 0.35f).Env(0.05f, 0.4f, 0.6f, 0.1f).Lp(500.f).WithSeed(147u));
			FNoise R(148u);
			uint32 Seed = 500u;
			for (float T = 0.02f; T < 0.52f; T += 0.03f + 0.05f * R.Uni())
			{
				const float Len = 0.005f + 0.006f * R.Uni();
				const float Amp = 0.25f + 0.35f * R.Uni();
				const float Hp = 1500.f + 2500.f * R.Uni();
				AddNoise(B, FNoiseSpec(T, Len, Amp).Env(0.0003f, 0.0015f, 0.f, 0.001f).Hp(Hp).WithSeed(++Seed));
			}
			ToPcm(B, 0.4f, Out);
			break;
		}
		case EFVSfx::OrderDone:
		{
			// delivered: a happy three-note fanfare with coins
			TArray<float> B = MakeBuf(1.1f);
			const float Notes[] = { 72.f, 76.f, 79.f };
			for (int32 i = 0; i < 3; ++i)
			{
				AddTone(B, FTone(0.09f * i, 0.16f, MidiHz(Notes[i]), 0.3f, EWave::Pulse).Env(0.003f, 0.08f, 0.3f, 0.04f).Lp(4200.f));
			}
			AddTone(B, FTone(0.3f, 0.6f, MidiHz(84.f), 0.3f, EWave::Triangle).Env(0.005f, 0.3f, 0.3f, 0.2f).Vib(5.5f, 0.1f, 0.15f));
			AddTone(B, FTone(0.32f, 0.075f, MidiHz(83.f), 0.2f, EWave::Square).Env(0.001f, 0.05f, 0.5f, 0.005f).Lp(5000.f));
			AddTone(B, FTone(0.39f, 0.35f, MidiHz(88.f), 0.2f, EWave::Square).Env(0.001f, 0.12f, 0.f, 0.05f).Lp(5000.f));
			AddBell(B, 0.3f, MidiHz(96.f), 0.2f, 0.25f);
			ToPcm(B, 0.55f, Out);
			break;
		}
		// ---------------------------------------------------------------- update 5 (legends)
		case EFVSfx::LegendWarn:
		{
			// a legend's trick is coming: a soft swell into two rising glassy pings (every legend plays it at its own pitch)
			TArray<float> B = MakeBuf(0.62f);
			AddTone(B, FTone(0.f, 0.3f, MidiHz(64.f), 0.22f, EWave::Triangle).To(MidiHz(69.f), 0.26f).Env(0.12f, 0.f, 1.f, 0.08f).Vib(9.f, 0.25f));
			AddBell(B, 0.1f, MidiHz(81.f), 0.55f, 0.16f);
			AddBell(B, 0.22f, MidiHz(88.f), 0.6f, 0.2f);
			AddTone(B, FTone(0.22f, 0.3f, MidiHz(76.f), 0.18f, EWave::Pulse).Env(0.004f, 0.09f, 0.f, 0.05f).Lp(2600.f));
			AddNoise(B, FNoiseSpec(0.f, 0.26f, 0.08f).Env(0.1f, 0.f, 1.f, 0.08f).Band(1200.f, 3200.f, 1.4f).WithSeed(151u));
			ToPcm(B, 0.55f, Out);
			break;
		}
		case EFVSfx::LegendHook:
		{
			// «Легенда на крючке!»: a raised brass fanfare with a golden shimmer
			TArray<float> B = MakeBuf(1.6f);
			auto Brass = [&B](float T, float Dur, float Note, float Amp)
			{
				AddTone(B, FTone(T, Dur, MidiHz(Note), Amp, EWave::Saw).Env(0.012f, 0.1f, 0.7f, 0.04f).Lp(2600.f));
				AddTone(B, FTone(T, Dur, MidiHz(Note) * 1.004f, Amp * 0.5f, EWave::Saw).Env(0.012f, 0.1f, 0.7f, 0.04f).Lp(2100.f));
			};
			Brass(0.f, 0.1f, 77.f, 0.34f);
			Brass(0.12f, 0.08f, 77.f, 0.3f);
			Brass(0.22f, 0.08f, 81.f, 0.3f);
			const float Chord[] = { 84.f, 81.f, 77.f };
			for (int32 i = 0; i < 3; ++i)
			{
				AddTone(B, FTone(0.32f, 0.95f, MidiHz(Chord[i]), i == 0 ? 0.32f : 0.17f, EWave::Saw).Env(0.02f, 0.45f, 0.5f, 0.3f).Vib(5.5f, 0.1f, 0.3f).Lp(2600.f));
			}
			AddTone(B, FTone(0.32f, 0.95f, MidiHz(65.f), 0.25f, EWave::Triangle).Env(0.02f, 0.45f, 0.4f, 0.3f));
			const float Sparkle[] = { 96.f, 100.f, 103.f, 108.f };
			for (int32 i = 0; i < 4; ++i)
			{
				AddBell(B, 0.36f + 0.07f * i, MidiHz(Sparkle[i]), 0.16f, 0.18f);
			}
			AddNoise(B, FNoiseSpec(0.32f, 0.8f, 0.12f).Env(0.02f, 0.3f, 0.f, 0.15f).Hp(6500.f).WithSeed(152u));
			ToPcm(B, 0.6f, Out);
			break;
		}
		case EFVSfx::LineSnap:
		{
			// the leader snaps: a sharp crack, a high twang whipping down, the slack line hissing back
			TArray<float> B = MakeBuf(0.55f);
			AddNoise(B, FNoiseSpec(0.f, 0.025f, 0.9f).Env(0.0003f, 0.006f, 0.f, 0.004f).Hp(2500.f).WithSeed(153u));
			AddPluck(B, 0.002f, 740.f, 0.55f, 0.3f, 0.975f, 154u);
			AddTone(B, FTone(0.004f, 0.2f, 1400.f, 0.3f, EWave::Saw).To(180.f, 0.18f).Env(0.001f, 0.06f, 0.f, 0.03f).Lp(3500.f));
			AddNoise(B, FNoiseSpec(0.03f, 0.36f, 0.35f).Env(0.01f, 0.12f, 0.f, 0.08f).Band(4500.f, 700.f, 2.2f).WithSeed(155u));
			AddTone(B, FTone(0.f, 0.1f, 120.f, 0.35f).To(60.f, 0.08f).Env(0.001f, 0.03f, 0.f, 0.02f));
			ToPcm(B, 0.55f, Out);
			break;
		}
		// ---------------------------------------------------------------- update 6 (forest, yard, city, cave, expert)
		case EFVSfx::AxeChop:
		{
			// steel bites into the notch: a sharp crack, a hollow woody knock, a few chips flying off
			TArray<float> B = MakeBuf(0.34f);
			AddNoise(B, FNoiseSpec(0.f, 0.018f, 0.9f).Env(0.0003f, 0.004f, 0.f, 0.003f).Hp(2200.f).WithSeed(161u));
			AddTone(B, FTone(0.f, 0.16f, 230.f, 0.85f, EWave::Triangle).To(120.f, 0.1f).Env(0.001f, 0.035f, 0.f, 0.02f));
			AddNoise(B, FNoiseSpec(0.002f, 0.2f, 0.6f).Env(0.001f, 0.045f, 0.f, 0.03f).Band(700.f, 450.f, 4.f).WithSeed(162u));
			AddTone(B, FTone(0.002f, 0.12f, 610.f, 0.25f).Env(0.001f, 0.03f, 0.f, 0.02f));
			for (int32 i = 0; i < 4; ++i)
			{
				AddNoise(B, FNoiseSpec(0.05f + 0.045f * i, 0.006f, 0.25f - 0.05f * i).Env(0.0003f, 0.0015f, 0.f, 0.001f).Hp(3000.f).WithSeed(163u + uint32(i)));
			}
			ToPcm(B, 0.55f, Out);
			break;
		}
		case EFVSfx::AxeStuck:
		{
			// a dull thunk, then the handle wobbling "boinng" in the log
			TArray<float> B = MakeBuf(0.6f);
			AddNoise(B, FNoiseSpec(0.f, 0.03f, 0.6f).Env(0.0005f, 0.008f, 0.f, 0.005f).Lp(1800.f).WithSeed(170u));
			AddTone(B, FTone(0.f, 0.14f, 150.f, 0.8f, EWave::Triangle).To(85.f, 0.1f).Env(0.001f, 0.04f, 0.f, 0.02f));
			AddTone(B, FTone(0.01f, 0.55f, 196.f, 0.45f, EWave::Triangle).Env(0.004f, 0.16f, 0.f, 0.06f).Vib(11.f, 0.9f).Lp(1500.f));
			AddTone(B, FTone(0.01f, 0.5f, 392.f, 0.12f).Env(0.004f, 0.1f, 0.f, 0.05f).Vib(11.f, 0.9f));
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::TreeFall:
		{
			// the trunk creaks, fibres snap faster and faster, the crown sweeps through the air, a heavy thud, leaves rustle
			TArray<float> B = MakeBuf(2.2f);
			AddTone(B, FTone(0.f, 0.75f, 260.f, 0.22f, EWave::Saw).To(170.f, 0.7f).Env(0.08f, 0.f, 1.f, 0.1f).Vib(16.f, 0.8f).Lp(1200.f));
			AddTone(B, FTone(0.05f, 0.6f, 390.f, 0.1f, EWave::Saw).To(300.f, 0.55f).Env(0.1f, 0.f, 1.f, 0.1f).Vib(23.f, 0.6f).Lp(1600.f));
			FNoise R(172u);
			uint32 Seed = 600u;
			float T = 0.1f, Gap = 0.12f;
			while (T < 0.95f)
			{
				AddNoise(B, FNoiseSpec(T, 0.008f + 0.006f * R.Uni(), 0.35f + 0.3f * R.Uni()).Env(0.0003f, 0.002f, 0.f, 0.002f)
					.Hp(1200.f + 1800.f * R.Uni()).WithSeed(++Seed));
				T += Gap * (0.6f + 0.6f * R.Uni());
				Gap = FMath::Max(0.025f, Gap * 0.82f);
			}
			AddNoise(B, FNoiseSpec(0.7f, 0.6f, 0.55f).Env(0.35f, 0.f, 1.f, 0.12f).Band(500.f, 2200.f, 1.2f).WithSeed(173u));
			AddTone(B, FTone(1.28f, 0.5f, 85.f, 1.f).To(38.f, 0.35f).Env(0.002f, 0.14f, 0.f, 0.1f));
			AddNoise(B, FNoiseSpec(1.28f, 0.55f, 0.8f).Env(0.002f, 0.12f, 0.f, 0.1f).Lp(900.f, 250.f).WithSeed(174u));
			AddNoise(B, FNoiseSpec(1.3f, 0.8f, 0.3f).Env(0.02f, 0.25f, 0.f, 0.2f).Hp(2500.f).Lp(7000.f).WithSeed(175u));
			ToPcm(B, 0.65f, Out);
			break;
		}
		case EFVSfx::Saw:
		{
			// one stroke of the saw: raspy teeth (noise bursts at the teeth rate) rising as the blade speeds up
			TArray<float> B = MakeBuf(0.38f);
			uint32 Seed = 700u;
			for (float T = 0.f; T < 0.32f; T += 0.011f)
			{
				const float Fr = T / 0.32f;
				const float Amp = 0.5f * FMath::Sin(PI * FMath::Clamp(Fr * 1.1f, 0.f, 1.f)) + 0.05f;
				AddNoise(B, FNoiseSpec(T, 0.009f, Amp).Env(0.0005f, 0.003f, 0.f, 0.002f).Band(1400.f + 1400.f * Fr, 1800.f + 1400.f * Fr, 2.5f).WithSeed(++Seed));
			}
			AddNoise(B, FNoiseSpec(0.f, 0.34f, 0.25f).Env(0.08f, 0.f, 1.f, 0.08f).Band(900.f, 1600.f, 1.5f).WithSeed(760u));
			AddTone(B, FTone(0.f, 0.34f, 1900.f, 0.05f).To(2300.f).Env(0.08f, 0.f, 1.f, 0.08f).Vib(7.f, 0.3f));
			ToPcm(B, 0.4f, Out);
			break;
		}
		case EFVSfx::Anvil:
		{
			// hammer on hot iron: a hard clank with a bright ring, a dull body under it, a faint hiss
			TArray<float> B = MakeBuf(1.0f);
			AddNoise(B, FNoiseSpec(0.f, 0.015f, 0.9f).Env(0.0002f, 0.003f, 0.f, 0.003f).Hp(3000.f).WithSeed(180u));
			AddBell(B, 0.f, MidiHz(91.f), 0.55f, 0.22f);
			AddBell(B, 0.f, MidiHz(91.f) * 1.41f, 0.3f, 0.14f);
			AddTone(B, FTone(0.f, 0.1f, 140.f, 0.6f, EWave::Triangle).To(90.f, 0.07f).Env(0.001f, 0.03f, 0.f, 0.02f));
			AddNoise(B, FNoiseSpec(0.02f, 0.35f, 0.08f).Env(0.02f, 0.12f, 0.f, 0.1f).Hp(5000.f).WithSeed(181u));
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::Pickaxe:
		{
			// a pick on stone: a sharp crack, a short stony "chink", grit trickling down
			TArray<float> B = MakeBuf(0.36f);
			AddNoise(B, FNoiseSpec(0.f, 0.02f, 0.9f).Env(0.0002f, 0.004f, 0.f, 0.003f).Hp(2500.f).WithSeed(185u));
			AddBell(B, 0.f, MidiHz(86.f), 0.4f, 0.045f);
			AddTone(B, FTone(0.f, 0.08f, 180.f, 0.55f, EWave::Triangle).To(110.f, 0.05f).Env(0.001f, 0.02f, 0.f, 0.015f));
			AddNoise(B, FNoiseSpec(0.005f, 0.2f, 0.25f).Env(0.002f, 0.05f, 0.f, 0.04f).Band(1800.f, 900.f, 1.5f).WithSeed(187u));
			FNoise R(186u);
			uint32 Seed = 800u;
			for (float T = 0.03f; T < 0.28f; T += 0.018f + 0.03f * R.Uni())
			{
				AddNoise(B, FNoiseSpec(T, 0.005f, 0.3f * (1.f - T / 0.3f)).Env(0.0003f, 0.0015f, 0.f, 0.001f).Hp(2000.f).WithSeed(++Seed));
			}
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::RockBreak:
		{
			// a boulder gives way: a deep crack and thump, a rumbling slide, pebbles bouncing away
			TArray<float> B = MakeBuf(1.3f);
			AddNoise(B, FNoiseSpec(0.f, 0.03f, 0.9f).Env(0.0003f, 0.006f, 0.f, 0.004f).Hp(1500.f).WithSeed(190u));
			AddTone(B, FTone(0.f, 0.35f, 110.f, 1.f).To(42.f, 0.25f).Env(0.002f, 0.09f, 0.f, 0.05f));
			AddNoise(B, FNoiseSpec(0.01f, 0.9f, 0.75f).Env(0.005f, 0.25f, 0.f, 0.15f).Lp(1400.f, 200.f).Hp(50.f).WithSeed(191u));
			FNoise R(192u);
			uint32 Seed = 850u;
			for (float T = 0.08f; T < 1.1f; T += 0.03f + 0.07f * R.Uni())
			{
				const float K = 1.f - T / 1.2f;
				AddTone(B, FTone(T, 0.03f, 1400.f + 1800.f * R.Uni(), 0.18f * K).Env(0.0005f, 0.008f, 0.f, 0.005f));
				AddNoise(B, FNoiseSpec(T, 0.006f, 0.25f * K).Env(0.0003f, 0.0015f, 0.f, 0.001f).Hp(2500.f).WithSeed(++Seed));
			}
			ToPcm(B, 0.6f, Out);
			break;
		}
		case EFVSfx::Find:
		{
			// something uncovered: a quick rising slide into two bells and a sparkle
			TArray<float> B = MakeBuf(0.95f);
			AddTone(B, FTone(0.f, 0.12f, MidiHz(76.f), 0.35f, EWave::Triangle).To(MidiHz(83.f), 0.08f).Env(0.002f, 0.05f, 0.f, 0.02f));
			AddBell(B, 0.09f, MidiHz(88.f), 0.5f, 0.22f);
			AddBell(B, 0.2f, MidiHz(95.f), 0.45f, 0.28f);
			AddTone(B, FTone(0.2f, 0.5f, MidiHz(83.f), 0.18f, EWave::Pulse).Env(0.004f, 0.18f, 0.f, 0.1f).Lp(3500.f));
			AddNoise(B, FNoiseSpec(0.18f, 0.6f, 0.1f).Env(0.03f, 0.2f, 0.f, 0.15f).Hp(7000.f).WithSeed(195u));
			ToPcm(B, 0.5f, Out);
			break;
		}
		case EFVSfx::BoatHorn:
		{
			// «Ту-туу!»: a short and a long toot of a ship's horn over lapping water
			TArray<float> B = MakeBuf(2.1f);
			auto Horn = [&B](float T, float Dur, float Amp)
			{
				AddTone(B, FTone(T, Dur, 104.f, Amp, EWave::Saw).To(110.f, 0.1f).Env(0.06f, 0.f, 1.f, 0.18f).Vib(4.f, 0.05f, 0.2f).Lp(900.f));
				AddTone(B, FTone(T, Dur, 156.f, Amp * 0.6f, EWave::Saw).To(165.f, 0.1f).Env(0.06f, 0.f, 1.f, 0.18f).Vib(4.f, 0.05f, 0.2f).Lp(1000.f));
				AddTone(B, FTone(T, Dur, 55.f, Amp * 0.5f).Env(0.06f, 0.f, 1.f, 0.18f));
			};
			Horn(0.f, 0.3f, 0.45f);
			Horn(0.42f, 1.25f, 0.5f);
			AddNoise(B, FNoiseSpec(0.f, 2.05f, 0.18f).Env(0.3f, 0.f, 1.f, 0.4f).Lp(700.f).Hp(120.f).WithSeed(197u));
			ToPcm(B, 0.6f, Out);
			break;
		}
		case EFVSfx::Tension:
		{
			// the line groans under load: stick-slip creaks (a train of tiny clicks) over a thin high whine
			TArray<float> B = MakeBuf(0.45f);
			FNoise R(200u);
			uint32 Seed = 900u;
			float T = 0.f, Gap = 0.035f;
			while (T < 0.38f)
			{
				AddTone(B, FTone(T, 0.02f, 420.f + 180.f * R.Uni(), 0.35f, EWave::Saw).Env(0.0005f, 0.006f, 0.f, 0.004f).Lp(2200.f));
				AddNoise(B, FNoiseSpec(T, 0.004f, 0.2f).Env(0.0002f, 0.001f, 0.f, 0.001f).Hp(2500.f).WithSeed(++Seed));
				T += Gap * (0.7f + 0.6f * R.Uni());
				Gap = FMath::Max(0.014f, Gap * 0.9f);
			}
			AddTone(B, FTone(0.f, 0.42f, 2100.f, 0.09f).To(2350.f, 0.4f).Env(0.06f, 0.f, 1.f, 0.08f).Vib(9.f, 0.25f));
			AddTone(B, FTone(0.f, 0.42f, 210.f, 0.18f, EWave::Saw).To(240.f, 0.4f).Env(0.05f, 0.f, 1.f, 0.08f).Vib(17.f, 0.7f).Lp(900.f));
			ToPcm(B, 0.35f, Out);
			break;
		}
		case EFVSfx::GravityFlip:
		{
			// the world turns over: a swooping "wo-oop" up and back down, a glassy ping at the turn
			TArray<float> B = MakeBuf(0.6f);
			AddTone(B, FTone(0.f, 0.2f, 220.f, 0.5f, EWave::Triangle).To(880.f, 0.18f).Env(0.01f, 0.f, 1.f, 0.03f).Vib(14.f, 0.3f));
			AddTone(B, FTone(0.18f, 0.32f, 880.f, 0.45f, EWave::Triangle).To(330.f, 0.28f).Env(0.005f, 0.12f, 0.3f, 0.06f).Vib(14.f, 0.3f));
			AddTone(B, FTone(0.f, 0.2f, 110.f, 0.3f).To(440.f, 0.18f).Env(0.01f, 0.f, 1.f, 0.03f));
			AddBell(B, 0.18f, MidiHz(93.f), 0.3f, 0.12f);
			AddNoise(B, FNoiseSpec(0.f, 0.45f, 0.18f).Env(0.15f, 0.1f, 0.f, 0.1f).Band(600.f, 2400.f, 1.5f).WithSeed(205u));
			ToPcm(B, 0.45f, Out);
			break;
		}
		case EFVSfx::CompassTick:
		{
			// a tiny brass click of the compass needle
			TArray<float> B = MakeBuf(0.035f);
			AddNoise(B, FNoiseSpec(0.f, 0.012f, 0.7f).Env(0.0002f, 0.002f, 0.f, 0.002f).Hp(3500.f).WithSeed(210u));
			AddTone(B, FTone(0.f, 0.025f, 3100.f, 0.35f, EWave::Triangle).Env(0.0003f, 0.005f, 0.f, 0.004f));
			AddTone(B, FTone(0.f, 0.02f, 4650.f, 0.15f).Env(0.0003f, 0.004f, 0.f, 0.003f));
			ToPcm(B, 0.25f, Out);
			break;
		}
		case EFVSfx::Whoosh:
		{
			// a fast rush of water / air: band-passed noise sweeping up, then back down
			TArray<float> B = MakeBuf(0.5f);
			AddNoise(B, FNoiseSpec(0.f, 0.22f, 0.7f).Env(0.15f, 0.f, 1.f, 0.07f).Band(350.f, 2200.f, 1.6f).WithSeed(215u));
			AddNoise(B, FNoiseSpec(0.2f, 0.26f, 0.6f).Env(0.01f, 0.08f, 0.f, 0.06f).Band(2200.f, 500.f, 1.6f).WithSeed(216u));
			AddNoise(B, FNoiseSpec(0.12f, 0.2f, 0.15f).Env(0.05f, 0.05f, 0.f, 0.05f).Hp(5000.f).WithSeed(217u));
			ToPcm(B, 0.45f, Out);
			break;
		}
		case EFVSfx::ExpertWarn:
		{
			// an expert legend's move is coming: a low "da-DUM" drum-and-horn (tritone) under a shimmering tremolo
			TArray<float> B = MakeBuf(0.75f);
			AddTone(B, FTone(0.f, 0.12f, MidiHz(45.f), 0.6f, EWave::Triangle).Env(0.002f, 0.05f, 0.f, 0.02f));
			AddTone(B, FTone(0.f, 0.08f, 140.f, 0.5f).To(60.f, 0.06f).Env(0.001f, 0.025f, 0.f, 0.02f));
			AddTone(B, FTone(0.16f, 0.45f, 150.f, 0.9f).To(55.f, 0.3f).Env(0.001f, 0.12f, 0.f, 0.05f));
			AddTone(B, FTone(0.16f, 0.5f, MidiHz(52.f), 0.3f, EWave::Saw).Env(0.02f, 0.2f, 0.4f, 0.12f).Lp(1200.f));
			AddTone(B, FTone(0.16f, 0.5f, MidiHz(58.f), 0.22f, EWave::Saw).Env(0.02f, 0.2f, 0.4f, 0.12f).Lp(1200.f));
			AddTone(B, FTone(0.f, 0.6f, MidiHz(88.f), 0.12f, EWave::Triangle).Env(0.2f, 0.f, 1.f, 0.12f).Vib(12.f, 0.5f));
			AddBell(B, 0.16f, MidiHz(94.f), 0.25f, 0.15f);
			ToPcm(B, 0.55f, Out);
			break;
		}
		case EFVSfx::Harvest:
		{
			// a root pulled out of the bed: a soil crumble, a round "plop", two happy notes
			TArray<float> B = MakeBuf(0.55f);
			AddNoise(B, FNoiseSpec(0.f, 0.12f, 0.5f).Env(0.01f, 0.04f, 0.f, 0.03f).Lp(1100.f).Hp(100.f).WithSeed(220u));
			AddTone(B, FTone(0.07f, 0.09f, 170.f, 0.8f).To(420.f, 0.05f).Env(0.001f, 0.03f, 0.f, 0.02f));
			AddTone(B, FTone(0.16f, 0.14f, MidiHz(79.f), 0.35f, EWave::Triangle).Env(0.002f, 0.06f, 0.f, 0.03f));
			AddTone(B, FTone(0.25f, 0.26f, MidiHz(84.f), 0.35f, EWave::Triangle).Env(0.002f, 0.1f, 0.f, 0.06f));
			AddBell(B, 0.25f, MidiHz(96.f), 0.1f, 0.1f);
			ToPcm(B, 0.45f, Out);
			break;
		}
		case EFVSfx::Plant:
		{
			// a seed into the soil: two soft pats and a tiny rising sprout "bloop"
			TArray<float> B = MakeBuf(0.4f);
			AddNoise(B, FNoiseSpec(0.f, 0.06f, 0.6f).Env(0.002f, 0.018f, 0.f, 0.01f).Lp(900.f).Hp(90.f).WithSeed(225u));
			AddNoise(B, FNoiseSpec(0.1f, 0.06f, 0.45f).Env(0.002f, 0.018f, 0.f, 0.01f).Lp(800.f).Hp(90.f).WithSeed(226u));
			AddTone(B, FTone(0.f, 0.05f, 110.f, 0.4f).To(70.f, 0.04f).Env(0.002f, 0.015f, 0.f, 0.01f));
			AddTone(B, FTone(0.19f, 0.14f, 520.f, 0.35f).To(1040.f, 0.1f).Env(0.003f, 0.05f, 0.f, 0.03f));
			ToPcm(B, 0.4f, Out);
			break;
		}
		case EFVSfx::Coins:
		{
			// income: a little cascade of coins pouring into the purse, then the familiar two-tone "ka-ching"
			TArray<float> B = MakeBuf(0.95f);
			FNoise R(230u);
			float T = 0.f;
			for (int32 k = 0; k < 12 && T < 0.42f; ++k)
			{
				const float F = MidiHz(88.f + float(R.Next() % 8u));
				AddTone(B, FTone(T, 0.06f, F, 0.22f, EWave::Square).Env(0.0005f, 0.018f, 0.f, 0.01f).Lp(6000.f));
				AddTone(B, FTone(T, 0.05f, F * 2.7f, 0.07f).Env(0.0005f, 0.012f, 0.f, 0.01f));
				T += 0.03f + 0.04f * R.Uni();
			}
			AddTone(B, FTone(0.45f, 0.075f, MidiHz(83.f), 0.35f, EWave::Square).Env(0.001f, 0.05f, 0.5f, 0.005f).Lp(5000.f));
			AddTone(B, FTone(0.52f, 0.42f, MidiHz(88.f), 0.35f, EWave::Square).Env(0.001f, 0.12f, 0.f, 0.05f).Lp(5000.f));
			AddBell(B, 0.52f, MidiHz(100.f), 0.12f, 0.15f);
			ToPcm(B, 0.45f, Out);
			break;
		}
		default:
		{
			TArray<float> B = MakeBuf(0.05f);
			AddTone(B, FTone(0.f, 0.04f, 1000.f, 0.3f).Env(0.001f, 0.01f, 0.f, 0.005f));
			ToPcm(B, 0.3f, Out);
			break;
		}
		}
	}

	// ------------------------------------------------------------------------------------------
	// Music
	// ------------------------------------------------------------------------------------------
	enum class EMInst : uint8 { Pluck, Bell, MusicBox, Marimba, Flute, Pad, Bass, Lead, Kick, Shaker, Frog,
		Accordion, WoodBlock, Drip, Bird, Gull, Woodpecker };   // update 6: city accordion, forest / cave / harbour ambience

	struct FMVoice
	{
		EMInst Inst = EMInst::Pluck;
		float Hz = 440.f;
		float Amp = 0.1f;
		int32 Delay = 0;     // samples before the note starts (relative to the current chunk)
		int32 Gate = 0;      // held length in samples
		int32 Total = 0;     // lifetime in samples (gate + tail)
		int32 Pos = 0;
		float Ph0 = 0.f, Ph1 = 0.f, Ph2 = 0.f;
		float Lp = 0.f, HpY = 0.f, HpX = 0.f;
		float Dec = 1.f, Kd = 1.f;
		FNoise Rng;
		TArray<float> Ks;
		int32 KsPos = 0;
	};

	struct FChord
	{
		int32 Root = 0;       // semitones above the key
		bool bMinor = false;
	};

	class FMusicGen
	{
	public:
		void Configure(EFVTheme Theme);
		void Render(TArray<int16>& Out, int32 N);

	private:
		// configuration
		float Bpm = 90.f;
		int32 Key = 60;                 // MIDI note of the key root in the melody octave
		bool bMinorKey = false;
		int32 StepsPerBar = 16;
		int32 BarsPerChord = 1;
		float Swing = 0.f;
		TArray<FChord> Prog;
		EMInst LeadInst = EMInst::Pluck;
		EMInst ArpInst = EMInst::MusicBox;
		float LeadAmp = 0.2f, ArpAmp = 0.05f, PadAmp = 0.045f, BassAmp = 0.16f;
		float LeadDensity = 0.5f;
		bool bArp = true, bPad = true, bBass = true;
		int32 ArpEvery = 2;
		int32 ArpOctave = 0;            // semitone offset of the arp relative to Key
		int32 BassStyle = 0;            // 0 = root/fifth, 1 = pulsing eighths, 2 = 6/8 sway, 3 = 3/4 waltz
		float ShakerAmp = 0.f, KickAmp = 0.f, FrogChance = 0.f;
		float EchoMix = 0.22f, EchoFb = 0.3f;
		float Master = 1.f;
		int32 Seed = 1;
		// update 6: ambience layers (all 0 = off, so the classic themes render exactly as before)
		float WoodAmp = 0.f;        // wood-block taps on the off-beats (forest)
		float HeartAmp = 0.f;       // a slow "lub-dub" heartbeat kick (cave)
		float DripChance = 0.f;     // water drops per step (cave)
		float BirdChance = 0.f;     // bird trills per even step (forest)
		float PeckerChance = 0.f;   // a woodpecker run per bar (forest)
		float GullChance = 0.f;     // a gull cry per bar (city)
		float SurfAmp = 0.f;        // a continuous surf wash (city)

		// state
		int64 Step = 0;
		double NextStepIn = 0.0;        // samples from the chunk start to the next step
		TArray<FMVoice> Voices;
		TArray<float> Echo;
		int32 EchoPos = 0;
		float EchoLp = 0.f;
		float OutLp = 0.f;
		FRandomStream Rng;
		struct FMelNote { int32 Step; int32 Len; int32 Midi; float Vel; };
		TArray<FMelNote> Melody;
		TArray<float> Mix;
		// surf state (update 6)
		FNoise SurfRng { 0x5EA5u };
		float SurfPhase = 0.6f, SurfPeriod = 7.f, SurfLp1 = 0.f, SurfLp2 = 0.f, SurfA = 0.f;
		int32 SurfCoefIn = 0;

		double StepBase() const { return double(SR) * 60.0 / double(Bpm) / 4.0; }
		double StepLen(int64 S) const
		{
			const double Base = StepBase();
			return Swing <= 0.f ? Base : ((S % 2 == 0) ? Base * (1.0 + Swing) : Base * (1.0 - Swing));
		}
		const FChord& ChordForBar(int64 Bar) const
		{
			static const FChord Tonic;
			if (Prog.Num() == 0)
			{
				return Tonic;
			}
			return Prog[int32((Bar / FMath::Max(1, BarsPerChord)) % Prog.Num())];
		}
		void ChordTones(const FChord& C, int32 Base, int32 Out[3]) const
		{
			Out[0] = Base + C.Root;
			Out[1] = Base + C.Root + (C.bMinor ? 3 : 4);
			Out[2] = Base + C.Root + 7;
		}
		int32 DegreeToMidi(int32 Deg) const
		{
			static const int32 Major[5] = { 0, 2, 4, 7, 9 };
			static const int32 Minor[5] = { 0, 3, 5, 7, 10 };
			const int32 D = FMath::Max(0, Deg);
			return Key + 12 * (D / 5) + (bMinorKey ? Minor[D % 5] : Major[D % 5]);
		}
		int32 SnapToChord(int32 Midi, const FChord& C) const
		{
			const int32 Pcs[3] = { C.Root % 12, (C.Root + (C.bMinor ? 3 : 4)) % 12, (C.Root + 7) % 12 };
			for (int32 D = 0; D <= 6; ++D)
			{
				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					const int32 M = Midi + D * Sign;
					const int32 Pc = ((M - Key) % 12 + 12) % 12;
					if (Pc == Pcs[0] || Pc == Pcs[1] || Pc == Pcs[2])
					{
						return M;
					}
				}
			}
			return Midi;
		}

		void BuildMelody(int64 Bar);
		void OnStep(int32 Offset);
		void OnStepAmbience(int32 SIB, int32 Offset, double StepS);
		float NextSurf();
		void AddVoice(EMInst Inst, float Midi, float Amp, int32 GateSamples, int32 Offset);
		float RenderSample(FMVoice& V);
	};

	void FMusicGen::Configure(EFVTheme Theme)
	{
		auto P = [](std::initializer_list<FChord> L) { return TArray<FChord>(L); };
		switch (Theme)
		{
		case EFVTheme::Pond:      // cheerful spring morning: guitar plucks, marimba arps
			Bpm = 96.f; Key = 65; bMinorKey = false; StepsPerBar = 16; BarsPerChord = 1; Swing = 0.1f;
			Prog = P({ {0, false}, {7, false}, {9, true}, {5, false} });
			LeadInst = EMInst::Pluck; LeadAmp = 0.3f; LeadDensity = 0.55f;
			ArpInst = EMInst::Marimba; ArpAmp = 0.055f; ArpEvery = 2; ArpOctave = 0;
			PadAmp = 0.04f; BassAmp = 0.17f; BassStyle = 0;
			ShakerAmp = 0.022f; KickAmp = 0.f; FrogChance = 0.f;
			EchoMix = 0.2f; EchoFb = 0.28f; Seed = 23;
			break;
		case EFVTheme::River:     // flowing: flute melody over rippling 16th plucks
			Bpm = 108.f; Key = 74; bMinorKey = false; StepsPerBar = 16; BarsPerChord = 1; Swing = 0.f;
			Prog = P({ {0, false}, {5, false}, {9, true}, {7, false} });
			LeadInst = EMInst::Flute; LeadAmp = 0.17f; LeadDensity = 0.5f;
			ArpInst = EMInst::Pluck; ArpAmp = 0.06f; ArpEvery = 1; ArpOctave = -12;
			PadAmp = 0.035f; BassAmp = 0.16f; BassStyle = 0;
			ShakerAmp = 0.025f; KickAmp = 0.1f; FrogChance = 0.f;
			EchoMix = 0.18f; EchoFb = 0.25f; Seed = 37;
			break;
		case EFVTheme::Mountain:  // calm and airy: bells, long pads
			Bpm = 72.f; Key = 69; bMinorKey = true; StepsPerBar = 16; BarsPerChord = 2; Swing = 0.f;
			Prog = P({ {0, true}, {8, false}, {3, false}, {10, false} });
			LeadInst = EMInst::Bell; LeadAmp = 0.16f; LeadDensity = 0.35f;
			ArpInst = EMInst::MusicBox; ArpAmp = 0.04f; ArpEvery = 4; ArpOctave = 12;
			PadAmp = 0.06f; BassAmp = 0.13f; BassStyle = 0;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.f;
			EchoMix = 0.34f; EchoFb = 0.42f; Seed = 41;
			break;
		case EFVTheme::Ocean:     // 6/8 sea shanty sway: marimba lead
			Bpm = 120.f; Key = 67; bMinorKey = false; StepsPerBar = 12; BarsPerChord = 1; Swing = 0.f;
			Prog = P({ {0, false}, {5, false}, {0, false}, {7, false} });
			LeadInst = EMInst::Marimba; LeadAmp = 0.26f; LeadDensity = 0.55f;
			ArpInst = EMInst::Pluck; ArpAmp = 0.05f; ArpEvery = 2; ArpOctave = -12;
			PadAmp = 0.04f; BassAmp = 0.17f; BassStyle = 2;
			ShakerAmp = 0.02f; KickAmp = 0.f; FrogChance = 0.f;
			EchoMix = 0.22f; EchoFb = 0.3f; Seed = 53;
			break;
		case EFVTheme::Swamp:     // mysterious: ocarina, dark pads, frogs
			Bpm = 76.f; Key = 64; bMinorKey = true; StepsPerBar = 16; BarsPerChord = 1; Swing = 0.08f;
			Prog = P({ {0, true}, {8, false}, {5, true}, {7, true} });
			LeadInst = EMInst::Flute; LeadAmp = 0.15f; LeadDensity = 0.35f;
			ArpInst = EMInst::Marimba; ArpAmp = 0.05f; ArpEvery = 4; ArpOctave = -12;
			PadAmp = 0.06f; BassAmp = 0.14f; BassStyle = 0;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.06f;
			EchoMix = 0.34f; EchoFb = 0.45f; Seed = 67;
			break;
		case EFVTheme::Volcano:   // driving but cute: pulse lead, pulsing bass, kick
			Bpm = 104.f; Key = 62; bMinorKey = true; StepsPerBar = 16; BarsPerChord = 1; Swing = 0.f;
			Prog = P({ {0, true}, {8, false}, {10, false}, {7, false} });
			LeadInst = EMInst::Lead; LeadAmp = 0.14f; LeadDensity = 0.55f;
			ArpInst = EMInst::Pluck; ArpAmp = 0.05f; ArpEvery = 2; ArpOctave = -12;
			PadAmp = 0.04f; BassAmp = 0.14f; BassStyle = 1;
			ShakerAmp = 0.02f; KickAmp = 0.18f; FrogChance = 0.f;
			EchoMix = 0.2f; EchoFb = 0.3f; Seed = 79;
			break;
		case EFVTheme::Home:      // the yard: a cozy 3/4 waltz, flute over flowing marimba, "oom-pah-pah" bass
			Bpm = 88.f; Key = 67; bMinorKey = false; StepsPerBar = 12; BarsPerChord = 1; Swing = 0.f;
			Prog = P({ {0, false}, {9, true}, {5, false}, {7, false} });
			LeadInst = EMInst::Flute; LeadAmp = 0.16f; LeadDensity = 0.5f;
			ArpInst = EMInst::Marimba; ArpAmp = 0.035f; ArpEvery = 2; ArpOctave = 0;
			PadAmp = 0.04f; BassAmp = 0.16f; BassStyle = 3;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.f;
			EchoMix = 0.24f; EchoFb = 0.3f; Seed = 97;
			break;
		case EFVTheme::HomeInterior:  // inside the house: a slower, softer waltz by the stove (guitar + bells)
			Bpm = 76.f; Key = 65; bMinorKey = false; StepsPerBar = 12; BarsPerChord = 1; Swing = 0.f;
			Prog = P({ {0, false}, {5, false}, {9, true}, {7, false} });
			LeadInst = EMInst::Pluck; LeadAmp = 0.26f; LeadDensity = 0.45f;
			ArpInst = EMInst::Bell; ArpAmp = 0.03f; ArpEvery = 4; ArpOctave = 12;
			PadAmp = 0.055f; BassAmp = 0.15f; BassStyle = 3;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.f;
			EchoMix = 0.3f; EchoFb = 0.36f; Seed = 101;
			break;
		// ---- update 6
		case EFVTheme::Forest:    // the forest: a lilting folk tune (I-bVII-IV-I), wooden flute over marimba, wood-block taps,
		                          // bird trills and now and then a woodpecker
			Bpm = 94.f; Key = 69; bMinorKey = false; StepsPerBar = 16; BarsPerChord = 1; Swing = 0.12f;
			Prog = P({ {0, false}, {10, false}, {5, false}, {0, false} });
			LeadInst = EMInst::Flute; LeadAmp = 0.16f; LeadDensity = 0.45f;
			ArpInst = EMInst::Marimba; ArpAmp = 0.045f; ArpEvery = 2; ArpOctave = -12;
			PadAmp = 0.04f; BassAmp = 0.15f; BassStyle = 0;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.f;
			WoodAmp = 0.05f; BirdChance = 0.05f; PeckerChance = 0.1f;
			EchoMix = 0.24f; EchoFb = 0.32f; Seed = 113;
			break;
		case EFVTheme::Mill:      // §18 the old mill: an autumn variation of the Forest tune — slower, a calm 3/4 in a warm minor
		                          // (i-VI-III-VII), wooden flute over soft marimba, the wheel's slow wood-block ticks, rare birds
			Bpm = 80.f; Key = 64; bMinorKey = true; StepsPerBar = 12; BarsPerChord = 1; Swing = 0.08f;
			Prog = P({ {0, true}, {8, false}, {3, false}, {10, false} });
			LeadInst = EMInst::Flute; LeadAmp = 0.15f; LeadDensity = 0.4f;
			ArpInst = EMInst::Marimba; ArpAmp = 0.04f; ArpEvery = 3; ArpOctave = -12;
			PadAmp = 0.05f; BassAmp = 0.14f; BassStyle = 3;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.f;
			WoodAmp = 0.035f; BirdChance = 0.025f; PeckerChance = 0.f;
			EchoMix = 0.28f; EchoFb = 0.34f; Seed = 137;
			break;
		case EFVTheme::City:      // the port town: an accordion polka (oom-pah bass + accordion chord stabs), a surf wash and gulls
			Bpm = 112.f; Key = 67; bMinorKey = false; StepsPerBar = 16; BarsPerChord = 1; Swing = 0.06f;
			Prog = P({ {0, false}, {9, true}, {2, true}, {7, false} });
			LeadInst = EMInst::Accordion; LeadAmp = 0.1f; LeadDensity = 0.55f;
			ArpInst = EMInst::Pluck; ArpAmp = 0.f; ArpEvery = 4; ArpOctave = -12; bArp = false;
			PadAmp = 0.025f; BassAmp = 0.15f; BassStyle = 4;
			ShakerAmp = 0.015f; KickAmp = 0.f; FrogChance = 0.f;
			SurfAmp = 0.55f; GullChance = 0.12f;
			EchoMix = 0.16f; EchoFb = 0.25f; Seed = 127;
			break;
		case EFVTheme::Cave:      // the cave: slow and tense (i-bII-bVI-v), a hollow low flute, sparse high bells, a heartbeat,
		                          // water drops, everything in a long echo
			Bpm = 66.f; Key = 60; bMinorKey = true; StepsPerBar = 16; BarsPerChord = 2; Swing = 0.f;
			Prog = P({ {0, true}, {1, false}, {8, false}, {7, true} });
			LeadInst = EMInst::Flute; LeadAmp = 0.15f; LeadDensity = 0.3f;
			ArpInst = EMInst::Bell; ArpAmp = 0.03f; ArpEvery = 4; ArpOctave = 24;
			PadAmp = 0.065f; BassAmp = 0.13f; BassStyle = 0;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.f;
			HeartAmp = 0.1f; DripChance = 0.09f;
			EchoMix = 0.4f; EchoFb = 0.5f; Seed = 131;
			break;
		default:                  // main menu: warm music box lullaby
			Bpm = 84.f; Key = 72; bMinorKey = false; StepsPerBar = 16; BarsPerChord = 1; Swing = 0.f;
			Prog = P({ {0, false}, {9, true}, {5, false}, {7, false} });
			LeadInst = EMInst::MusicBox; LeadAmp = 0.2f; LeadDensity = 0.45f;
			ArpInst = EMInst::Pluck; ArpAmp = 0.05f; ArpEvery = 2; ArpOctave = -12;
			PadAmp = 0.045f; BassAmp = 0.15f; BassStyle = 0;
			ShakerAmp = 0.f; KickAmp = 0.f; FrogChance = 0.f;
			EchoMix = 0.28f; EchoFb = 0.35f; Seed = 11;
			break;
		}
		Rng.Initialize(Seed * 31 + 7);
		Step = 0;
		NextStepIn = 0.0;
		Voices.Reset();
		Melody.Reset();
		// Echo: a dotted eighth (three 16th steps).
		const int32 EchoLen = FMath::Clamp(int32(StepBase() * 3.0), SR / 10, SR);
		Echo.SetNumZeroed(EchoLen);
		EchoPos = 0;
		EchoLp = 0.f;
		OutLp = 0.f;
		SurfA = OnePole(300.f);
		SurfCoefIn = 0;
	}

	void FMusicGen::AddVoice(EMInst Inst, float Midi, float Amp, int32 GateSamples, int32 Offset)
	{
		if (Voices.Num() >= 32)
		{
			Voices.RemoveAt(0);   // oldest note gives way
		}
		FMVoice& V = Voices.AddDefaulted_GetRef();
		V.Inst = Inst;
		V.Hz = MidiHz(Midi);
		V.Amp = Amp;
		V.Delay = FMath::Max(0, Offset);
		V.Gate = FMath::Max(1, GateSamples);
		V.Rng = FNoise(uint32(Rng.GetUnsignedInt()) | 1u);
		float Tail = 0.1f;
		float DecaySec = 0.f;
		switch (Inst)
		{
		case EMInst::Pluck:
		{
			Tail = 0.12f;
			const int32 L = FMath::Max(2, FMath::RoundToInt(SR / V.Hz - 0.5f));
			V.Ks.SetNumUninitialized(L);
			float Lp = 0.f, Mean = 0.f;
			for (int32 i = 0; i < L; ++i)
			{
				Lp += 0.55f * (V.Rng.Bi() - Lp);
				V.Ks[i] = Lp;
				Mean += Lp;
			}
			Mean /= float(L);
			for (float& S : V.Ks)
			{
				S -= Mean;
			}
			break;
		}
		case EMInst::Bell:     V.Gate = SR * 2; Tail = 0.05f; DecaySec = 0.8f; break;
		case EMInst::MusicBox: V.Gate = int32(SR * 1.4f); Tail = 0.05f; DecaySec = 0.6f; break;
		case EMInst::Marimba:  V.Gate = int32(SR * 0.7f); Tail = 0.03f; DecaySec = 0.22f; break;
		case EMInst::Flute:    Tail = 0.14f; break;
		case EMInst::Pad:      Tail = 0.8f; break;
		case EMInst::Bass:     Tail = 0.08f; DecaySec = 0.35f; break;
		case EMInst::Lead:     Tail = 0.06f; DecaySec = 0.25f; break;
		case EMInst::Kick:     V.Gate = int32(SR * 0.25f); Tail = 0.02f; DecaySec = 0.09f; break;
		case EMInst::Shaker:   V.Gate = int32(SR * 0.08f); Tail = 0.01f; DecaySec = 0.025f; break;
		case EMInst::Frog:     V.Gate = int32(SR * 0.22f); Tail = 0.03f; DecaySec = 0.09f; V.Hz = 150.f + 60.f * Rng.FRand(); break;
		case EMInst::Accordion:  Tail = 0.06f; DecaySec = 0.9f; break;
		case EMInst::WoodBlock:  V.Gate = int32(SR * 0.12f); Tail = 0.02f; DecaySec = 0.03f; break;
		case EMInst::Drip:       V.Gate = int32(SR * 0.2f); Tail = 0.02f; DecaySec = 0.04f; break;
		case EMInst::Bird:       V.Gate = int32(SR * 0.2f); Tail = 0.01f; break;
		case EMInst::Gull:       V.Gate = int32(SR * (Rng.FRand() < 0.5f ? 0.52f : 0.82f)); Tail = 0.02f; break;   // two or three cries
		case EMInst::Woodpecker: V.Gate = int32(SR * (0.4f + 0.3f * Rng.FRand())); Tail = 0.02f; break;
		default: break;
		}
		V.Kd = DecaySec > 0.f ? FMath::Exp(-1.f / (DecaySec * SR)) : 1.f;
		V.Total = V.Gate + FMath::Max(1, int32(Tail * SR));
	}

	float FMusicGen::RenderSample(FMVoice& V)
	{
		const float Tm = float(V.Pos) / SR;
		float Rel = 1.f;
		if (V.Pos >= V.Gate)
		{
			Rel = 1.f - float(V.Pos - V.Gate) / float(FMath::Max(1, V.Total - V.Gate));
		}
		auto Adv = [](float& Ph, float Hz)
		{
			Ph += Hz / SR;
			Ph -= FMath::FloorToFloat(Ph);
			return Ph;
		};
		const float Dec = V.Dec;
		V.Dec *= V.Kd;

		float Out = 0.f;
		switch (V.Inst)
		{
		case EMInst::Pluck:
		{
			const int32 L = V.Ks.Num();
			if (L >= 2)
			{
				const float S = V.Ks[V.KsPos];
				const int32 Nx = (V.KsPos + 1) % L;
				V.Ks[V.KsPos] = (S + V.Ks[Nx]) * 0.5f * 0.9965f;
				V.KsPos = Nx;
				Out = S * 1.4f;
			}
			break;
		}
		case EMInst::Bell:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.002f);
			Out = Atk * (FMath::Sin(TwoPi * Adv(V.Ph0, V.Hz)) * Dec
				+ 0.3f * FMath::Sin(TwoPi * Adv(V.Ph1, V.Hz * 2.76f)) * Dec * Dec
				+ 0.12f * FMath::Sin(TwoPi * Adv(V.Ph2, V.Hz * 5.4f)) * Dec * Dec * Dec * Dec);
			break;
		}
		case EMInst::MusicBox:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.001f);
			Out = Atk * (FMath::Sin(TwoPi * Adv(V.Ph0, V.Hz)) * Dec
				+ 0.22f * FMath::Sin(TwoPi * Adv(V.Ph1, V.Hz * 2.f)) * Dec * Dec
				+ 0.1f * FMath::Sin(TwoPi * Adv(V.Ph2, V.Hz * 4.16f)) * Dec * Dec * Dec);
			break;
		}
		case EMInst::Marimba:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.0015f);
			const float D4 = Dec * Dec * Dec * Dec;
			Out = Atk * (FMath::Sin(TwoPi * Adv(V.Ph0, V.Hz)) * Dec + 0.35f * FMath::Sin(TwoPi * Adv(V.Ph1, V.Hz * 4.f)) * D4 * D4);
			break;
		}
		case EMInst::Flute:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.07f);
			float Hz = V.Hz;
			if (Tm > 0.18f)
			{
				Hz *= 1.f + 0.0087f * FMath::Sin(TwoPi * 5.f * (Tm - 0.18f));   // ~0.15 semitone vibrato
			}
			const float P = Adv(V.Ph0, Hz);
			V.Lp += 0.08f * (V.Rng.Bi() - V.Lp);   // breath
			Out = Atk * (0.85f + 0.15f * (1.f - Atk)) * (FMath::Sin(TwoPi * P) + 0.12f * FMath::Sin(2.f * TwoPi * P) + 0.04f * FMath::Sin(3.f * TwoPi * P)) + V.Lp * 0.25f * Atk;
			break;
		}
		case EMInst::Pad:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.5f);
			const float X = 0.5f * (Osc(EWave::Triangle, Adv(V.Ph0, V.Hz)) + Osc(EWave::Triangle, Adv(V.Ph1, V.Hz * 1.004f)));
			V.Lp += 0.18f * (X - V.Lp);
			Out = V.Lp * Atk;
			break;
		}
		case EMInst::Bass:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.004f);
			const float P = Adv(V.Ph0, V.Hz);
			Out = Atk * (0.55f + 0.45f * Dec) * (FMath::Sin(TwoPi * P) + 0.25f * Osc(EWave::Triangle, P));
			break;
		}
		case EMInst::Lead:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.005f);
			float Hz = V.Hz;
			if (Tm > 0.2f)
			{
				Hz *= 1.f + 0.007f * FMath::Sin(TwoPi * 5.5f * (Tm - 0.2f));
			}
			const float X = Osc(EWave::Pulse, Adv(V.Ph0, Hz));
			V.Lp += 0.28f * (X - V.Lp);
			Out = Atk * (0.6f + 0.4f * Dec) * V.Lp;
			break;
		}
		case EMInst::Kick:
		{
			const float Hz = 45.f + 70.f * FMath::Exp(-Tm / 0.03f);
			Out = FMath::Sin(TwoPi * Adv(V.Ph0, Hz)) * Dec * FMath::Min(1.f, Tm / 0.001f);
			break;
		}
		case EMInst::Shaker:
		{
			const float X = V.Rng.Bi();
			const float Y = 0.5f * (V.HpY + X - V.HpX);   // crude high-pass
			V.HpX = X;
			V.HpY = Y;
			Out = Y * Dec * FMath::Min(1.f, Tm / 0.003f);
			break;
		}
		case EMInst::Frog:
		{
			const float Am = 0.5f + 0.5f * FMath::Sin(TwoPi * 26.f * Tm);
			const float X = Osc(EWave::Square, Adv(V.Ph0, V.Hz * (1.f + 0.2f * Dec)));
			V.Lp += 0.12f * (X - V.Lp);
			Out = V.Lp * Am * Dec * FMath::Min(1.f, Tm / 0.004f);
			break;
		}
		// ---- update 6
		case EMInst::Accordion:
		{
			// two free reeds, the second tuned a little sharp (the "musette" beating), plus a quiet reed an octave below;
			// two gentle low-pass poles take the edge off the saws (HpX serves as the second pole here)
			const float Atk = FMath::Min(1.f, Tm / 0.03f);
			const float R1 = Osc(EWave::Saw, Adv(V.Ph0, V.Hz));
			const float R2 = Osc(EWave::Saw, Adv(V.Ph1, V.Hz * 1.0065f));
			const float R3 = Osc(EWave::Square, Adv(V.Ph2, V.Hz * 0.5f));
			V.Lp += 0.22f * (0.4f * (R1 + R2) + 0.12f * R3 - V.Lp);
			V.HpX += 0.3f * (V.Lp - V.HpX);
			Out = Atk * (0.75f + 0.25f * Dec) * V.HpX;
			break;
		}
		case EMInst::WoodBlock:
		{
			const float Atk = FMath::Min(1.f, Tm / 0.0008f);
			Out = Atk * (FMath::Sin(TwoPi * Adv(V.Ph0, V.Hz)) * Dec + 0.35f * FMath::Sin(TwoPi * Adv(V.Ph1, V.Hz * 2.3f)) * Dec * Dec * Dec)
				+ V.Rng.Bi() * FMath::Exp(-Tm / 0.001f) * 0.25f;
			break;
		}
		case EMInst::Drip:
		{
			// a water "plink": the pitch jumps upward right after the drop lands
			const float Chirp = 1.f + 0.8f * (1.f - FMath::Exp(-Tm / 0.015f));
			Out = FMath::Sin(TwoPi * Adv(V.Ph0, V.Hz * Chirp)) * Dec * FMath::Min(1.f, Tm / 0.0008f);
			break;
		}
		case EMInst::Bird:
		{
			// a short trill: the pitch warbles fast and climbs, the level pulses with each warble
			const float U = FMath::Clamp(Tm / 0.2f, 0.f, 1.f);
			const float Wob = FMath::Sin(TwoPi * 13.f * Tm);
			const float F = V.Hz * (1.f + 0.18f * Wob + 0.25f * U);
			Out = FMath::Sin(TwoPi * Adv(V.Ph0, F)) * FMath::Sin(PI * U) * (0.55f + 0.45f * FMath::Abs(Wob));
			break;
		}
		case EMInst::Gull:
		{
			// "kee-ow" cries every 0.3 s, each a little lower: the pitch rises then falls, a reedy timbre
			const float Seg = FMath::Fmod(Tm, 0.3f);
			const float Cry = FMath::FloorToFloat(Tm / 0.3f);
			const float U = Seg / 0.22f;
			float Env = 0.f, F = V.Hz;
			if (U < 1.f)
			{
				Env = FMath::Sin(PI * U);
				F = V.Hz * (1.f + 0.3f * FMath::Sin(PI * U) - 0.3f * U) * (1.f - 0.05f * Cry);
			}
			const float P = Adv(V.Ph0, F);
			V.Lp += 0.3f * (Osc(EWave::Triangle, P) + 0.3f * Osc(EWave::Square, P) - V.Lp);
			Out = V.Lp * Env;
			break;
		}
		case EMInst::Woodpecker:
		{
			// a fast run of hollow knocks (one every 60 ms) that fades out
			const float K = FMath::Fmod(Tm, 0.06f);
			const float Life = float(V.Gate) / SR + 0.05f;
			Out = FMath::Sin(TwoPi * V.Hz * K) * FMath::Exp(-K / 0.007f) * FMath::Max(0.f, 1.f - Tm / Life);
			break;
		}
		default: break;
		}
		return Out * Rel;
	}

	void FMusicGen::BuildMelody(int64 Bar)
	{
		Melody.Reset();
		static const int32 PhraseMap[16] = { 0, 1, 2, 3, 0, 1, 2, 4, 5, 6, 7, 8, 0, 1, 2, 3 };
		static const int32 Moves[9] = { -2, -1, -1, 0, 1, 1, 2, -3, 3 };
		const int32 InCycle = int32(Bar % 16);
		const int32 Motif = PhraseMap[InCycle];
		const FChord& C = ChordForBar(Bar);
		FRandomStream R(Seed * 7919 + Motif * 104729 + 17);
		const bool bB = Motif >= 5;
		const float Density = LeadDensity * (bB ? 0.8f : 1.f);
		const int32 Positions = StepsPerBar / 2;
		const bool bCadence = InCycle == 15 || InCycle == 7;
		int32 Deg = R.RandRange(3, 6) + (bB ? 2 : 0);

		// The very first bar of the loop starts gently (lets the listener settle in).
		if (Bar == 0)
		{
			return;
		}

		int32 P = 0;
		while (P < Positions)
		{
			const bool bStrong = P == 0 || P == Positions / 2;
			if (bCadence && P > 0)
			{
				break;
			}
			if (R.FRand() < (bStrong ? 0.85f : Density))
			{
				Deg = FMath::Clamp(Deg + Moves[R.RandRange(0, 8)], 0, 10);
				int32 Midi = DegreeToMidi(Deg);
				if (bStrong)
				{
					Midi = SnapToChord(Midi, C);
				}
				int32 Len = bStrong ? R.RandRange(1, 3) : R.RandRange(1, 2);
				if (bCadence)
				{
					Midi = SnapToChord(Key + C.Root + (InCycle == 15 ? 0 : 7), C);
					Len = Positions;
				}
				Len = FMath::Clamp(Len, 1, Positions - P);
				const float Vel = bStrong ? 1.f : 0.78f + R.FRand() * 0.15f;
				if (!bStrong && Len == 1 && R.FRand() < 0.22f)
				{
					// two quick sixteenths
					const int32 Midi2 = DegreeToMidi(FMath::Clamp(Deg + (R.FRand() < 0.5f ? 1 : -1), 0, 10));
					Melody.Add({ P * 2, 1, Midi, Vel });
					Melody.Add({ P * 2 + 1, 1, Midi2, Vel * 0.8f });
				}
				else
				{
					Melody.Add({ P * 2, Len * 2, Midi, Vel });
				}
				P += Len;
			}
			else
			{
				P += 1;
			}
		}
	}

	void FMusicGen::OnStep(int32 Offset)
	{
		const int32 SIB = int32(Step % StepsPerBar);
		const int64 Bar = Step / StepsPerBar;
		if (SIB == 0)
		{
			BuildMelody(Bar);
		}
		const FChord& C = ChordForBar(Bar);
		const double StepS = StepBase();

		// Pad on every chord change
		if (bPad && SIB == 0 && Bar % FMath::Max(1, BarsPerChord) == 0)
		{
			int32 T[3];
			ChordTones(C, Key - 12, T);
			const int32 Gate = int32(StepS * StepsPerBar * BarsPerChord * 0.92);
			for (int32 i = 0; i < 3; ++i)
			{
				AddVoice(EMInst::Pad, float(T[i]), PadAmp, Gate, Offset);
			}
		}

		// Bass
		if (bBass)
		{
			const int32 Root = Key - 24 + C.Root;
			switch (BassStyle)
			{
			case 1:   // pulsing eighths, octave jumps
				if (SIB % 2 == 0)
				{
					const bool bUp = (SIB / 2) % 2 == 1;
					AddVoice(EMInst::Bass, float(Root + (bUp ? 12 : 0)), BassAmp * (SIB % 8 == 0 ? 1.f : 0.72f), int32(StepS * 1.6), Offset);
				}
				break;
			case 3:   // 3/4 waltz "oom-pah-pah": root (fifth on odd bars) on 1, a soft two-note chord on 2 and 3
				if (SIB == 0)
				{
					AddVoice(EMInst::Bass, float(Root + (Bar % 2 == 1 ? -5 : 0)), BassAmp, int32(StepS * 3.5), Offset);
				}
				else if (StepsPerBar >= 3 && (SIB == StepsPerBar / 3 || SIB == 2 * (StepsPerBar / 3)))
				{
					int32 T[3];
					ChordTones(C, Key - 12, T);
					const float Soft = SIB == StepsPerBar / 3 ? 0.34f : 0.28f;
					AddVoice(EMInst::Pluck, float(T[1]), BassAmp * Soft, int32(StepS * 2.2), Offset);
					AddVoice(EMInst::Pluck, float(T[2]), BassAmp * Soft * 0.85f, int32(StepS * 2.2), Offset);
				}
				break;
			case 4:   // update 6, polka "oom-pah": root on 1, the fifth below on 3, accordion chord stabs on the off-beats
				if (SIB == 0)
				{
					AddVoice(EMInst::Bass, float(Root), BassAmp, int32(StepS * 3.5), Offset);
				}
				else if (SIB == StepsPerBar / 2)
				{
					AddVoice(EMInst::Bass, float(Root - 5), BassAmp * 0.85f, int32(StepS * 3.5), Offset);
				}
				else if (StepsPerBar >= 4 && (SIB == StepsPerBar / 4 || SIB == 3 * (StepsPerBar / 4)))
				{
					int32 T[3];
					ChordTones(C, Key - 12, T);
					for (int32 i = 0; i < 3; ++i)
					{
						AddVoice(EMInst::Accordion, float(T[i]), BassAmp * 0.2f, int32(StepS * 1.4), Offset);
					}
				}
				break;
			case 2:   // 6/8: dotted quarter root + fifth
				if (SIB == 0)
				{
					AddVoice(EMInst::Bass, float(Root), BassAmp, int32(StepS * 5.0), Offset);
				}
				else if (SIB == StepsPerBar / 2)
				{
					AddVoice(EMInst::Bass, float(Root + 7), BassAmp * 0.8f, int32(StepS * 5.0), Offset);
				}
				break;
			default:  // root on 1, fifth on 3, pickup at the end of odd bars
				if (SIB == 0)
				{
					AddVoice(EMInst::Bass, float(Root), BassAmp, int32(StepS * 6.0), Offset);
				}
				else if (SIB == StepsPerBar / 2)
				{
					AddVoice(EMInst::Bass, float(Root + 7), BassAmp * 0.8f, int32(StepS * 5.0), Offset);
				}
				else if (SIB == StepsPerBar - 2 && Bar % 2 == 1)
				{
					AddVoice(EMInst::Bass, float(Root + 12), BassAmp * 0.55f, int32(StepS * 1.5), Offset);
				}
				break;
			}
		}

		// Arpeggio
		if (bArp && ArpEvery > 0 && SIB % ArpEvery == 0)
		{
			int32 T[3];
			ChordTones(C, Key + ArpOctave, T);
			const int32 Tones[4] = { T[0], T[1], T[2], T[0] + 12 };
			static const int32 Pattern[8] = { 0, 1, 2, 3, 2, 1, 2, 1 };
			const int32 Idx = (SIB / ArpEvery) % 8;
			const float Accent = SIB == 0 ? 1.2f : ((SIB % 4 == 0) ? 1.f : 0.8f);
			AddVoice(ArpInst, float(Tones[Pattern[Idx]]), ArpAmp * Accent, int32(StepS * ArpEvery * 1.5), Offset);
		}

		// Melody
		for (const FMelNote& Nn : Melody)
		{
			if (Nn.Step == SIB)
			{
				AddVoice(LeadInst, float(Nn.Midi), LeadAmp * Nn.Vel, int32(StepS * Nn.Len * 0.9), Offset);
			}
		}

		// Percussion
		if (ShakerAmp > 0.f && SIB % 2 == 0)
		{
			AddVoice(EMInst::Shaker, 60.f, ShakerAmp * ((SIB % 4 == 2) ? 1.f : 0.6f), int32(StepS), Offset);
		}
		if (KickAmp > 0.f && (SIB == 0 || (StepsPerBar == 16 && SIB == 8)))
		{
			AddVoice(EMInst::Kick, 36.f, KickAmp, int32(StepS * 2.0), Offset);
		}
		if (FrogChance > 0.f && SIB % 2 == 0 && Rng.FRand() < FrogChance)
		{
			AddVoice(EMInst::Frog, 48.f, 0.1f, int32(StepS), Offset);
		}
		OnStepAmbience(SIB, Offset, StepS);
	}

	void FMusicGen::OnStepAmbience(int32 SIB, int32 Offset, double StepS)
	{
		// Every layer is off (0) in the classic themes and draws nothing from Rng then: their music is unchanged.
		if (WoodAmp > 0.f)
		{
			// taps on the off-beats: high on 2 and 4, a low answer before 4, a pickup at the end of odd bars
			const int32 Hi = Key + 14, Lo = Key + 7;
			const int64 Bar = Step / FMath::Max(1, StepsPerBar);
			if (SIB == StepsPerBar / 4 || SIB == 3 * (StepsPerBar / 4))
			{
				AddVoice(EMInst::WoodBlock, float(Hi), WoodAmp, int32(StepS), Offset);
			}
			else if (SIB == 5 * StepsPerBar / 8 || (SIB == StepsPerBar - 2 && Bar % 2 == 1))
			{
				AddVoice(EMInst::WoodBlock, float(Lo), WoodAmp * 0.6f, int32(StepS), Offset);
			}
		}
		if (HeartAmp > 0.f && StepsPerBar >= 8)
		{
			const int32 Half = StepsPerBar / 2;
			if (SIB % Half == 0 || SIB % Half == 3)
			{
				AddVoice(EMInst::Kick, 36.f, HeartAmp * (SIB % Half == 0 ? 1.f : 0.65f), int32(StepS * 2.0), Offset);
			}
		}
		if (DripChance > 0.f && Rng.FRand() < DripChance)
		{
			const float Note = 86.f + float(Rng.RandRange(0, 12));
			const float Amp = 0.05f + 0.05f * Rng.FRand();
			const int32 Jitter = int32(Rng.FRand() * float(StepS));
			AddVoice(EMInst::Drip, Note, Amp, int32(SR * 0.2f), Offset + Jitter);
		}
		if (BirdChance > 0.f && SIB % 2 == 0 && Rng.FRand() < BirdChance)
		{
			const float Note = 98.f + float(Rng.RandRange(0, 6));
			const int32 Chirps = Rng.RandRange(1, 3);
			for (int32 c = 0; c < Chirps; ++c)
			{
				AddVoice(EMInst::Bird, Note + ((c % 2) ? 2.f : 0.f), 0.03f, int32(SR * 0.2f), Offset + c * int32(SR * 0.24f));
			}
		}
		if (SIB == 0 && PeckerChance > 0.f && Rng.FRand() < PeckerChance)
		{
			const float Note = 83.f + float(Rng.RandRange(0, 3));
			const int32 Jitter = int32(Rng.FRand() * float(StepS) * 8.f);
			AddVoice(EMInst::Woodpecker, Note, 0.05f, 1, Offset + Jitter);
		}
		if (SIB == 0 && GullChance > 0.f && Rng.FRand() < GullChance)
		{
			const float Note = 86.f + float(Rng.RandRange(0, 4));
			const int32 Jitter = int32(Rng.FRand() * float(StepS) * float(StepsPerBar) * 0.5f);
			AddVoice(EMInst::Gull, Note, 0.035f, 1, Offset + Jitter);
		}
	}

	float FMusicGen::NextSurf()
	{
		// a wave every 5.5..9 s: a quick swell as it breaks, a long hiss as it runs back; the filter opens with the crash
		SurfPhase += 1.f / (SurfPeriod * SR);
		if (SurfPhase >= 1.f)
		{
			SurfPhase -= 1.f;
			SurfPeriod = 5.5f + 3.5f * SurfRng.Uni();
		}
		const float Crash = SurfPhase < 0.15f ? SurfPhase / 0.15f : FMath::Exp(-(SurfPhase - 0.15f) * 4.f);
		if (--SurfCoefIn <= 0)
		{
			SurfCoefIn = 64;
			SurfA = OnePole(300.f + 1300.f * Crash);
		}
		const float W = SurfRng.Bi();
		SurfLp1 += SurfA * (W - SurfLp1);
		SurfLp2 += SurfA * (SurfLp1 - SurfLp2);
		return SurfLp2 * (0.3f + 0.7f * Crash) * SurfAmp;
	}

	void FMusicGen::Render(TArray<int16>& Out, int32 N)
	{
		N = FMath::Max(1, N);
		Mix.SetNumZeroed(N, EAllowShrinking::No);
		FMemory::Memzero(Mix.GetData(), sizeof(float) * N);

		// Schedule every step that starts inside this chunk.
		int32 Guard = 0;
		while (NextStepIn < double(N) && Guard++ < 256)
		{
			OnStep(FMath::Max(0, int32(NextStepIn)));
			NextStepIn += StepLen(Step);
			++Step;
		}
		NextStepIn -= double(N);

		for (FMVoice& V : Voices)
		{
			int32 i = 0;
			if (V.Delay > 0)
			{
				const int32 Skip = FMath::Min(V.Delay, N);
				V.Delay -= Skip;
				i = Skip;
			}
			for (; i < N && V.Pos < V.Total; ++i, ++V.Pos)
			{
				Mix[i] += RenderSample(V) * V.Amp;
			}
		}
		Voices.RemoveAll([](const FMVoice& V) { return V.Delay <= 0 && V.Pos >= V.Total; });

		// Echo (dotted eighth, darkened repeats), gentle low-pass, limiter.
		const int32 EchoLen = Echo.Num();
		const float LpA = OnePole(7000.f);
		Out.SetNumUninitialized(N);
		for (int32 i = 0; i < N; ++i)
		{
			float X = Mix[i] * Master;
			if (EchoLen > 0)
			{
				const float D = Echo[EchoPos];
				EchoLp += 0.35f * (D - EchoLp);
				Echo[EchoPos] = X * 0.8f + EchoLp * EchoFb;
				EchoPos = (EchoPos + 1) % EchoLen;
				X += EchoLp * EchoMix;
			}
			if (SurfAmp > 0.f)
			{
				X += NextSurf();   // update 6: the harbour surf stays dry (not echoed)
			}
			OutLp += LpA * (X - OutLp);
			Out[i] = int16(FMath::Clamp(SoftClip(OutLp), -1.f, 1.f) * 30000.f);
		}
	}

	// ------------------------------------------------------------------------------------------
	// Rain ambience
	// ------------------------------------------------------------------------------------------
	class FRainGen
	{
	public:
		void Render(TArray<int16>& Out, int32 N)
		{
			N = FMath::Max(1, N);
			Out.SetNumUninitialized(N);
			const float A1 = OnePole(1600.f);
			const float A2 = OnePole(2400.f);
			const float Hk = FMath::Exp(-TwoPi * 180.f / SR);

			// ~24 droplets per second at random positions.
			const float Expected = 24.f * float(N) / SR;
			int32 Count = int32(Expected);
			if (Rng.Uni() < Expected - float(Count))
			{
				++Count;
			}
			for (int32 k = 0; k < Count; ++k)
			{
				FDrop D;
				D.Start = int32(Rng.Uni() * N);
				D.Len = int32(SR * (0.012f + 0.02f * Rng.Uni()));
				D.Hz = 1400.f + 2800.f * Rng.Uni();
				D.Amp = 0.02f + 0.08f * Rng.Uni() * Rng.Uni();
				Drops.Add(D);
			}

			Buf.SetNumUninitialized(N, EAllowShrinking::No);
			for (int32 i = 0; i < N; ++i)
			{
				if (--SwellTimer <= 0)
				{
					SwellTarget = 0.75f + 0.35f * Rng.Uni();
					SwellTimer = SR / 2;
				}
				Swell += (SwellTarget - Swell) * 0.00005f;
				const float W = Rng.Bi();
				Lp1 += A1 * (W - Lp1);
				Lp2 += A2 * (Lp1 - Lp2);
				const float Hp = Hk * (HpY + Lp2 - HpX);
				HpX = Lp2;
				HpY = Hp;
				Buf[i] = Hp * 1.6f * Swell + W * 0.02f;
			}

			for (FDrop& D : Drops)
			{
				const int32 From = FMath::Max(0, D.Start);
				const int32 To = FMath::Min(N, D.Start + D.Len);
				for (int32 i = From; i < To; ++i)
				{
					const int32 T = i - D.Start;
					const float Fr = float(T) / float(FMath::Max(1, D.Len));
					D.Ph += D.Hz * (1.f - 0.3f * Fr) / SR;
					D.Ph -= FMath::FloorToFloat(D.Ph);
					Buf[i] += FMath::Sin(TwoPi * D.Ph) * D.Amp * (1.f - Fr) * (1.f - Fr);
				}
				D.Start -= N;
			}
			Drops.RemoveAll([](const FDrop& D) { return D.Start + D.Len <= 0; });

			for (int32 i = 0; i < N; ++i)
			{
				Out[i] = int16(FMath::Clamp(SoftClip(Buf[i]), -1.f, 1.f) * 30000.f);
			}
		}

	private:
		struct FDrop { int32 Start = 0; int32 Len = 1; float Hz = 2000.f; float Amp = 0.05f; float Ph = 0.f; };
		FNoise Rng { 0xA5A5u };
		float Lp1 = 0.f, Lp2 = 0.f, HpY = 0.f, HpX = 0.f;
		float Swell = 1.f, SwellTarget = 1.f;
		int32 SwellTimer = 0;
		TArray<FDrop> Drops;
		TArray<float> Buf;
	};
}

namespace FVSynth
{
	UFVAudio* GFVAudio = nullptr;

	constexpr int32 MaxSfxVoices = 12;
	constexpr int32 StreamChunk = SR / 5;          // 0.2 s per rendered chunk
	constexpr int32 StreamTargetBytes = SR * 2;    // keep ~1 s of PCM16 queued ahead
	constexpr float MusicGain = 0.9f;
	constexpr float RainGainMax = 0.55f;
	constexpr double MusicRetrySeconds = 3.0;
}

// ============================================================================================
// Implementation state
// ============================================================================================
struct UFVAudio::FImpl
{
	UFVAudio* Owner = nullptr;
	bool bAudioOk = false;
	double Now = 0.0;
	FRandomStream Rng { 4242 };

	TArray<int16> Pcm[int32(EFVSfx::Count)];
	double LastPlay[int32(EFVSfx::Count)] = {};

	struct FVoice
	{
		TWeakObjectPtr<UAudioComponent> Comp;
		TWeakObjectPtr<USoundWaveProcedural> Wave;
		double EndTime = 0.0;
	};
	TArray<FVoice> Voices;

	struct FStream
	{
		TWeakObjectPtr<UAudioComponent> Comp;
		TWeakObjectPtr<USoundWaveProcedural> Wave;
		bool IsValid() const { return Comp.IsValid() && Wave.IsValid(); }
	};
	FStream Music;
	TUniquePtr<FVSynth::FMusicGen> Gen;
	EFVTheme Theme = EFVTheme::Count;
	bool bHasTheme = false;
	double MusicRetryAt = 0.0;
	struct FDying { FStream S; double EndTime = 0.0; };
	TArray<FDying> Dying;

	FStream Rain;
	FVSynth::FRainGen RainGen;
	bool bRainWanted = false;
	float RainLevel = 0.f;

	bool bReeling = false;
	float ReelIntensity = 0.5f;
	float ReelTimer = 0.f;

	TArray<int16> Scratch;
	TArray<int16> Silence;

	// ------------------------------------------------------------------ helpers
	UWorld* World() const
	{
		UObject* Ctx = Owner ? Owner->WorldContextObj.Get() : nullptr;
		return Ctx ? Ctx->GetWorld() : nullptr;
	}

	bool CanPlay() const
	{
		if (!bAudioOk || !GEngine || !GEngine->UseSound())
		{
			return false;
		}
		UWorld* W = World();
		return W && W->GetAudioDeviceRaw() != nullptr;
	}

	USoundWaveProcedural* NewWave(float Duration, bool bStream)
	{
		USoundWaveProcedural* W = NewObject<USoundWaveProcedural>(Owner);
		W->SetSampleRate(uint32(FVSynth::SR));
		W->NumChannels = 1;
		W->Duration = bStream ? INDEFINITELY_LOOPING_DURATION : Duration;
		W->bLooping = false;
		W->SoundGroup = bStream ? SOUNDGROUP_Music : SOUNDGROUP_Default;
		W->VirtualizationMode = bStream ? EVirtualizationMode::PlayWhenSilent : EVirtualizationMode::Disabled;
		return W;
	}

	void Queue(USoundWaveProcedural* W, const TArray<int16>& Data)
	{
		if (W && Data.Num() > 0)
		{
			W->QueueAudio(reinterpret_cast<const uint8*>(Data.GetData()), Data.Num() * int32(sizeof(int16)));
		}
	}

	void Release(UAudioComponent* C, USoundWaveProcedural* W)
	{
		if (C)
		{
			if (IsValid(C))
			{
				C->Stop();
			}
			Owner->Components.Remove(C);
		}
		if (W)
		{
			if (IsValid(W))
			{
				W->ResetAudio();
			}
			Owner->Waves.Remove(W);
		}
	}

	void ReleaseStream(FStream& S)
	{
		Release(S.Comp.Get(), S.Wave.Get());
		S = FStream();
	}

	void ReleaseVoice(int32 Index)
	{
		if (!Voices.IsValidIndex(Index))
		{
			return;
		}
		FVoice V = Voices[Index];
		Voices.RemoveAt(Index);
		Release(V.Comp.Get(), V.Wave.Get());
	}

	// ------------------------------------------------------------------ sfx
	void BuildAll()
	{
		const double T0 = FPlatformTime::Seconds();
		int32 Total = 0;
		for (int32 i = 0; i < int32(EFVSfx::Count); ++i)
		{
			FVSynth::BuildSfx(EFVSfx(i), Pcm[i]);
			Total += Pcm[i].Num();
		}
		Silence.SetNumZeroed(1024);
		for (double& T : LastPlay)
		{
			T = -1000.0;
		}
		UE_LOG(LogFV, Log, TEXT("FV: audio: %d sfx synthesized (%.1f s of PCM) in %.0f ms"), int32(EFVSfx::Count),
			float(Total) / FVSynth::SR, (FPlatformTime::Seconds() - T0) * 1000.0);
	}

	void PlaySfx(EFVSfx Sfx, float Volume, float Pitch)
	{
		const int32 Idx = int32(Sfx);
		if (Idx < 0 || Idx >= int32(EFVSfx::Count) || Pcm[Idx].Num() == 0 || !Owner)
		{
			return;
		}
		const float Vol = FMath::Clamp(Volume, 0.f, 2.f) * Owner->SfxVolume;
		if (Vol <= 0.001f)
		{
			return;
		}
		double MinGap = 0.025;
		switch (Sfx)
		{
		case EFVSfx::ReelTick: MinGap = 0.035; break;
		case EFVSfx::Step: MinGap = 0.1; break;
		case EFVSfx::Hover: MinGap = 0.04; break;
		// update 2: rapid / retriggered sounds
		case EFVSfx::Pickup: MinGap = 0.06; break;
		case EFVSfx::Dig: MinGap = 0.08; break;
		case EFVSfx::Chop: MinGap = 0.05; break;
		case EFVSfx::Sizzle: MinGap = 0.3; break;
		case EFVSfx::Fire: MinGap = 0.35; break;
		case EFVSfx::Knot: MinGap = 0.06; break;
		case EFVSfx::Hammer: MinGap = 0.08; break;
		case EFVSfx::Door: MinGap = 0.3; break;
		case EFVSfx::Sleep: MinGap = 1.0; break;
		// update 6
		case EFVSfx::AxeChop: MinGap = 0.06; break;
		case EFVSfx::AxeStuck: MinGap = 0.15; break;
		case EFVSfx::TreeFall: MinGap = 0.6; break;
		case EFVSfx::Saw: MinGap = 0.12; break;
		case EFVSfx::Anvil: MinGap = 0.07; break;
		case EFVSfx::Pickaxe: MinGap = 0.07; break;
		case EFVSfx::RockBreak: MinGap = 0.3; break;
		case EFVSfx::BoatHorn: MinGap = 1.5; break;
		case EFVSfx::Tension: MinGap = 0.25; break;
		case EFVSfx::GravityFlip: MinGap = 0.15; break;
		case EFVSfx::CompassTick: MinGap = 0.03; break;
		case EFVSfx::Whoosh: MinGap = 0.08; break;
		case EFVSfx::Harvest: MinGap = 0.06; break;
		case EFVSfx::Plant: MinGap = 0.06; break;
		case EFVSfx::Coins: MinGap = 0.2; break;
		default: break;
		}
		if (Now - LastPlay[Idx] < MinGap)
		{
			return;
		}
		LastPlay[Idx] = Now;

		if (!CanPlay())
		{
			return;
		}
		UWorld* W = World();

		// Voice limit: the oldest one-shot gives way.
		for (int32 i = Voices.Num() - 1; i >= 0; --i)
		{
			if (!Voices[i].Comp.IsValid())
			{
				ReleaseVoice(i);
			}
		}
		while (Voices.Num() >= FVSynth::MaxSfxVoices)
		{
			ReleaseVoice(0);
		}

		// Small random pitch variation keeps repeated sounds lively.
		if (Sfx == EFVSfx::Step || Sfx == EFVSfx::ReelTick || Sfx == EFVSfx::Splash || Sfx == EFVSfx::Hover || Sfx == EFVSfx::Click
			|| Sfx == EFVSfx::Pickup || Sfx == EFVSfx::Dig || Sfx == EFVSfx::Chop || Sfx == EFVSfx::Sizzle || Sfx == EFVSfx::Fire
			|| Sfx == EFVSfx::Knot || Sfx == EFVSfx::Hammer)
		{
			Pitch *= Rng.FRandRange(0.94f, 1.06f);
		}
		// update 6: repeated work strokes vary a little too
		if (Sfx == EFVSfx::AxeChop || Sfx == EFVSfx::Saw || Sfx == EFVSfx::Anvil || Sfx == EFVSfx::Pickaxe || Sfx == EFVSfx::CompassTick
			|| Sfx == EFVSfx::Whoosh || Sfx == EFVSfx::Tension || Sfx == EFVSfx::Harvest || Sfx == EFVSfx::Plant)
		{
			Pitch *= Rng.FRandRange(0.94f, 1.06f);
		}
		Pitch = FMath::Clamp(Pitch, 0.4f, 2.5f);

		const float Dur = float(Pcm[Idx].Num()) / FVSynth::SR;
		USoundWaveProcedural* Wave = NewWave(Dur + 0.05f, false);
		Queue(Wave, Pcm[Idx]);
		Queue(Wave, Silence);
		UAudioComponent* C = UGameplayStatics::CreateSound2D(W, Wave, Vol, Pitch, 0.f, nullptr, false, true);
		if (!C)
		{
			return;
		}
		C->bIsUISound = true;
		C->Play();
		Owner->Components.Add(C);
		Owner->Waves.Add(Wave);
		FVoice V;
		V.Comp = C;
		V.Wave = Wave;
		V.EndTime = Now + double(Dur) / double(Pitch) + 0.12;
		Voices.Add(V);
	}

	// ------------------------------------------------------------------ streams
	template <typename TRender>
	FStream StartStream(float Volume, TRender&& RenderChunk, int32 PrefillChunks)
	{
		FStream S;
		if (!CanPlay())
		{
			return S;
		}
		USoundWaveProcedural* Wave = NewWave(0.f, true);
		for (int32 i = 0; i < PrefillChunks; ++i)
		{
			RenderChunk(Scratch, FVSynth::StreamChunk);
			Queue(Wave, Scratch);
		}
		UAudioComponent* C = UGameplayStatics::CreateSound2D(World(), Wave, Volume, 1.f, 0.f, nullptr, false, true);
		if (!C)
		{
			return S;
		}
		C->bIsUISound = true;
		Owner->Components.Add(C);
		Owner->Waves.Add(Wave);
		S.Comp = C;
		S.Wave = Wave;
		return S;
	}

	template <typename TRender>
	void Feed(FStream& S, TRender&& RenderChunk)
	{
		USoundWaveProcedural* Wave = S.Wave.Get();
		if (!Wave || !S.Comp.IsValid())
		{
			return;
		}
		int32 Guard = 0;
		while (Wave->GetAvailableAudioByteCount() < FVSynth::StreamTargetBytes && Guard++ < 8)
		{
			RenderChunk(Scratch, FVSynth::StreamChunk);
			Queue(Wave, Scratch);
		}
	}

	float MusicVolume() const { return Owner ? Owner->MusicVolume * FVSynth::MusicGain : 0.f; }

	void StartMusic(EFVTheme NewTheme)
	{
		if (Music.IsValid())
		{
			if (UAudioComponent* C = Music.Comp.Get())
			{
				C->FadeOut(1.2f, 0.f);
			}
			Dying.Add({ Music, Now + 1.5 });
		}
		Music = FStream();

		Gen = MakeUnique<FVSynth::FMusicGen>();
		Gen->Configure(NewTheme);
		FVSynth::FMusicGen* G = Gen.Get();
		Music = StartStream(MusicVolume(), [G](TArray<int16>& Out, int32 N) { G->Render(Out, N); }, 5);
		if (UAudioComponent* C = Music.Comp.Get())
		{
			C->FadeIn(1.5f, 1.f);
		}
	}

	void StartRain()
	{
		if (Rain.IsValid())
		{
			return;
		}
		Rain = StartStream(0.001f, [this](TArray<int16>& Out, int32 N) { RainGen.Render(Out, N); }, 4);
		if (UAudioComponent* C = Rain.Comp.Get())
		{
			C->Play();
		}
	}

	void Tick(float Dt)
	{
		Now += Dt;

		// Finished one-shots
		for (int32 i = Voices.Num() - 1; i >= 0; --i)
		{
			if (Now >= Voices[i].EndTime || !Voices[i].Comp.IsValid())
			{
				ReleaseVoice(i);
			}
		}

		// Reel clicking
		if (bReeling)
		{
			ReelTimer -= Dt;
			if (ReelTimer <= 0.f)
			{
				const float I = FMath::Clamp(ReelIntensity, 0.f, 1.f);
				PlaySfx(EFVSfx::ReelTick, 0.35f + 0.4f * I, 0.9f + 0.25f * I);
				ReelTimer = FMath::Lerp(0.16f, 0.055f, I);
			}
		}

		// Music: keep the stream fed, retire faded-out streams
		if (Gen && Music.IsValid())
		{
			FVSynth::FMusicGen* G = Gen.Get();
			Feed(Music, [G](TArray<int16>& Out, int32 N) { G->Render(Out, N); });
		}
		else if (bHasTheme && !Music.IsValid() && Now >= MusicRetryAt && CanPlay())
		{
			// No music component (device reset, or it could not be created): try again now and then.
			MusicRetryAt = Now + FVSynth::MusicRetrySeconds;
			StartMusic(Theme);
		}
		for (int32 i = Dying.Num() - 1; i >= 0; --i)
		{
			if (Now >= Dying[i].EndTime || !Dying[i].S.Comp.IsValid())
			{
				ReleaseStream(Dying[i].S);
				Dying.RemoveAt(i);
			}
		}

		// Rain: fade the level, feed the stream
		const float Target = bRainWanted ? 1.f : 0.f;
		RainLevel = FMath::FInterpConstantTo(RainLevel, Target, Dt, 0.6f);
		if (bRainWanted && !Rain.IsValid())
		{
			StartRain();
		}
		if (Rain.IsValid())
		{
			if (UAudioComponent* C = Rain.Comp.Get())
			{
				C->SetVolumeMultiplier(FMath::Max(0.001f, RainLevel * FVSynth::RainGainMax * (Owner ? Owner->SfxVolume : 0.f)));
			}
			Feed(Rain, [this](TArray<int16>& Out, int32 N) { RainGen.Render(Out, N); });
			if (!bRainWanted && RainLevel <= 0.f)
			{
				ReleaseStream(Rain);
			}
		}

		// Drop references to components destroyed elsewhere (world teardown).
		Owner->Components.RemoveAll([](const TObjectPtr<UAudioComponent>& C) { return !IsValid(C); });
	}

	void StopAll()
	{
		for (int32 i = Voices.Num() - 1; i >= 0; --i)
		{
			ReleaseVoice(i);
		}
		ReleaseStream(Music);
		for (FDying& D : Dying)
		{
			ReleaseStream(D.S);
		}
		Dying.Reset();
		ReleaseStream(Rain);
	}
};

// ============================================================================================
// UFVAudio
// ============================================================================================
UFVAudio* UFVAudio::Get()
{
	return FVSynth::GFVAudio;
}

void UFVAudio::Init(UObject* WorldContext)
{
	FVSynth::GFVAudio = this;
	WorldContextObj = WorldContext;
	Impl = MakeShared<FImpl>();
	Impl->Owner = this;
	Impl->BuildAll();

	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	Impl->bAudioOk = GEngine && GEngine->UseSound() && World && World->GetAudioDeviceRaw() != nullptr;
	if (!Impl->bAudioOk)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: audio device unavailable, the game runs silently"));
	}
}

void UFVAudio::Shutdown()
{
	if (Impl)
	{
		Impl->StopAll();
		Impl.Reset();
	}
	Components.Reset();
	Waves.Reset();
	WorldContextObj = nullptr;
	if (FVSynth::GFVAudio == this)
	{
		FVSynth::GFVAudio = nullptr;
	}
}

void UFVAudio::Tick(float Dt)
{
	if (Impl)
	{
		Impl->Tick(FMath::Clamp(Dt, 0.f, 0.25f));
	}
}

void UFVAudio::Play(EFVSfx Sfx, float Volume, float Pitch)
{
	if (Impl)
	{
		Impl->PlaySfx(Sfx, Volume, Pitch);
	}
}

void UFVAudio::SetSfxVolume(float V)
{
	SfxVolume = FMath::Clamp(V, 0.f, 1.f);
}

void UFVAudio::SetMusicVolume(float V)
{
	MusicVolume = FMath::Clamp(V, 0.f, 1.f);
	if (Impl)
	{
		if (UAudioComponent* C = Impl->Music.Comp.Get())
		{
			C->SetVolumeMultiplier(Impl->MusicVolume());
		}
	}
}

void UFVAudio::SetMusicTheme(EFVTheme Theme)
{
	if (!Impl)
	{
		return;
	}
	if (Impl->bHasTheme && Impl->Theme == Theme && Impl->Music.IsValid())
	{
		return;
	}
	Impl->Theme = Theme;
	Impl->bHasTheme = true;
	Impl->StartMusic(Theme);
}

void UFVAudio::SetRain(bool bRain)
{
	if (Impl)
	{
		Impl->bRainWanted = bRain;
	}
}

void UFVAudio::SetReeling(bool bOn, float Intensity)
{
	if (!Impl)
	{
		return;
	}
	if (bOn && !Impl->bReeling)
	{
		Impl->ReelTimer = 0.f;
	}
	Impl->bReeling = bOn;
	Impl->ReelIntensity = FMath::Clamp(Intensity, 0.f, 1.f);
}
