// UPDATE 6 — art hooks called from the update-2 art files (Docs/UPDATE6_FOREST_CITY.md §17). Pure CPU builders.
// Call sites (frozen):
//   UFVSprites::BuildFish       (FVArtItems.cpp)  -> FVArtFishSide::BuildFish     for Shape >= EFVFishShape::FlyingFish
//   UFVSprites::BuildDecorIcon  (FVArtHome.cpp)   -> FVArtDecorU6::BuildIcon      for Slot >= FVDecor::ClassicSlots
//   UFVSprites::BuildDecorWorld (FVArtHome.cpp)   -> FVArtDecorU6::BuildWorld     for Slot >= FVDecor::ClassicSlots
// Other new art (items, icons, baits, junk, expert rod icons) is added by ART-ITEMS directly in the files it owns
// (FVArtItems.cpp, FVSprites.cpp, FVArtItemsU6.cpp).

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVSprites.h"

/** [ART-FISH-SIDE] FVArtFishSide.cpp — side views (facing right, 1px dark outline) of the update-6 shapes:
    FlyingFish .. Sturgeon and ExpertLegend (by FFVFishDef::ExpertLegend). Sizes like the classic fish: 12x8 .. 26x16;
    expert legends up to 44x22. bSilhouette = flat dark fill of the same outline. */
namespace FVArtFishSide
{
	FFVImage BuildFish(const FFVFishDef& Def, bool bSilhouette);
}

/** [ART-DECOR] FVArtDecorU6.cpp — yard decor and furniture (slots >= FVDecor::ClassicSlots). */
namespace FVArtDecorU6
{
	FFVImage BuildIcon(const FFVDecorDef& Def);     // 16x16 catalog icon
	/** World sprite, pivot bottom-centre (drawn at FV::WorldScale at the DecorSlot anchor of the yard / house). */
	FFVImage BuildWorld(const FFVDecorDef& Def);
	/** 1..4 animation frames of the world sprite (same size as BuildWorld). */
	int32 WorldFrames(const FFVDecorDef& Def);
	/** Frame F of the world sprite (BuildWorld == frame 0). */
	FFVImage BuildWorldFrame(const FFVDecorDef& Def, int32 Frame);
	/** Light source in texels from the top-left of the world sprite; (-1,-1) = none. */
	FVector2D LightPoint(const FFVDecorDef& Def);
}
