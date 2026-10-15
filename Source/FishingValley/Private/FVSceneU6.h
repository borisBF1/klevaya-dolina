// UPDATE 6 — runtime state of the world objects of big locations (trees, junk piles, boulders, villagers, yard
// buildings, garden beds, the boat, blockage, rubble, exits, yard decor / furniture). [SCENE-OBJECTS] owns this file
// and FVSceneU6Objects.cpp; it may add anything here. FVScene::FImpl keeps it in FImpl::U6Objects.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVSprites.h"

class UFVSaveGame;
namespace FVWorldU6 { struct FWorldDef; }

struct FFVSceneU6State
{
	/** Incremented by every RebuildWorldU6. */
	int32 Revision = 0;
	float NpcAnimTime = 0.f;
	/** Villagers drawn by the last rebuild: FImpl::Vis index + npc id (the idle animation swaps the frame). */
	TArray<int32> NpcVis;
	TArray<FName> NpcIds;
	int32 NpcFrame = 0;
};

namespace FVSceneU6Objects
{
	/** Save key of a world node: "<loc>:<anchorId>" (same as FVGameU6). */
	FName NodeKey(FName LocationId, FName AnchorId);
	/** Growth stage 0..3 of a garden bed for GardenBedSprite (-1 = empty). */
	int32 BedStage(const UFVSaveGame* P, int32 Bed);
	/** Hash of every profile state the update-6 objects of this location are drawn from (FImpl::WorldSignature).
	    Def = the big world (nullptr = classic location); bBoat = the layout has a boat. */
	uint32 HashState(const FVWorldU6::FWorldDef* Def, bool bBoat, bool bInterior, const UFVSaveGame* P, FName LocationId);
}
