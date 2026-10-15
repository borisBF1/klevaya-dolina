// Fully synthesized audio (no assets): SFX rendered to PCM at startup, played through USoundWaveProcedural;
// a gentle procedural music loop per location theme. Implemented in FVAudio.cpp.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FVTypes.h"
#include "FVAudio.generated.h"

class UAudioComponent;
class USoundWaveProcedural;

UCLASS()
class UFVAudio : public UObject
{
	GENERATED_BODY()

public:
	static UFVAudio* Get();

	/** Renders all SFX buffers. WorldContext is used to spawn 2D audio components. */
	void Init(UObject* WorldContext);
	void Shutdown();
	/** Stops finished voices, keeps music fed, fades music between themes. */
	void Tick(float Dt);

	void Play(EFVSfx Sfx, float Volume = 1.f, float Pitch = 1.f);
	void SetSfxVolume(float V);
	void SetMusicVolume(float V);
	float GetSfxVolume() const { return SfxVolume; }
	float GetMusicVolume() const { return MusicVolume; }

	/** Starts/crossfades the music loop for a theme. Menu music: pass EFVTheme::Count. */
	void SetMusicTheme(EFVTheme Theme);
	/** Rain ambience on/off. */
	void SetRain(bool bRain);
	/** Continuous reel clicking during the minigame; Intensity 0..1 (fish in bar = faster clicks). */
	void SetReeling(bool bOn, float Intensity = 0.5f);

private:
	UPROPERTY(Transient) TObjectPtr<UObject> WorldContextObj;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Components;
	UPROPERTY(Transient) TArray<TObjectPtr<USoundWaveProcedural>> Waves;

	float SfxVolume = 0.8f;
	float MusicVolume = 0.45f;

	// Implementation state (PCM buffers, voice bookkeeping, music generator...) is private to FVAudio.cpp.
	struct FImpl;
	TSharedPtr<FImpl> Impl;
};
