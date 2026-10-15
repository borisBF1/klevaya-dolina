// Thin Unreal framework layer: GameMode (no pawn), PlayerController (input snapshot), HUD (canvas drawing).
// Implemented in FVFramework.cpp.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "FVTypes.h"
#include "FVFramework.generated.h"

class UFVGame;

UCLASS()
class AFVGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AFVGameMode();
};

UCLASS()
class AFVPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AFVPlayerController();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PlayerTick(float DeltaTime) override;

	UFVGame* GetGame() const { return Game; }

	/** Virtual-canvas mapping from the last HUD frame (set by AFVHUD) so the mouse can be converted to virtual px. */
	float ViewScale = 1.f;
	FVector2D ViewOffset = FVector2D::ZeroVector;

private:
	UPROPERTY(Transient) TObjectPtr<UFVGame> Game;
	FFVInput BuildInput();

	// ---- additions (Core module)
	/** Installs the pixel-art hardware mouse cursor (falls back to the default arrow). */
	void SetupCursor();
	bool bCustomCursor = false;
};

UCLASS()
class AFVHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};
