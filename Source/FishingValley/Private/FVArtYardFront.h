// UPDATE 6 [ART-YARD] — depth of the yard decor (Р61 fix, the fence over the cottage): the props painted into the yard
// background (FVWorldU6::BuildYard) that stand IN FRONT of the back fence line and overlap its band (the cottage, the
// apple trees, the edge trees). The tiled fence decor (DecorSlot YardFence) is drawn at runtime over the background, so
// the scene (FVSceneU6Objects.cpp) and the try-on (FVDecorPreview.cpp) redraw these props y-sorted by their feet (the
// same pixels as the background: nothing changes where no runtime object passes behind them). Implemented in FVArtYard.cpp.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVSprites.h"

namespace FVArtYardFront
{
	struct FPart
	{
		FFVSprite S;                               // drawn at FV::WorldScale, pivot top-left
		FVector2D TopLeft = FVector2D::ZeroVector; // world px
		float SortY = 0.f;                         // world px: the prop's feet
	};
	/** The redraw parts (texture cache: UFVSprites, category Yard). The cottage leaves out its door zone (below the fence
	    line), so the door highlight of the wall layer stays visible. Empty until UFVSprites exists. */
	const TArray<FPart>& Parts();
	/** Feet of the cottage (world px) and its redrawn rects in texels from the top-left of its 78x62 canvas: the facade
	    overlays (YardRoof / YardWalls / YardShutters use the same canvas at the same feet) are redrawn with these rects
	    right after the cottage (SortY = CottageFeet().Y + 0.1 / 0.2 / 0.3); the vane after them (+ 0.4). */
	FVector2D CottageFeet();
	const TArray<FIntRect>& CottageRects();
}
