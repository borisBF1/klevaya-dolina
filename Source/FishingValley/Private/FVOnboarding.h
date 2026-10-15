// First-fishing onboarding (update 4b): coach marks guiding a new player from the practice pond in the Home yard to the
// first real trip to the village pond. State: UFVSaveGame::OnboardingStep (0 = not started .. 100 = done).
// Spec: Docs/UPDATE4_ONLINE.md §11. Implemented in FVOnboarding.cpp (World+Fishing module); UFVGame calls Tick every
// frame and Draw after the world HUD (before overlay screens).

#pragma once

#include "CoreMinimal.h"

class UFVGame;
class FVDraw;

namespace FVOnboarding
{
	void Tick(UFVGame* Game, float Dt);
	void Draw(UFVGame* Game, FVDraw& D);
	/** True while the coach marks are shown (OnboardingStep < 100 and the player is in the world). */
	bool IsActive(UFVGame* Game);
	/** «Пропустить обучение»: marks it done. */
	void Skip(UFVGame* Game);
}
