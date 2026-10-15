// UPDATE 6 (Р61) — «Примерка» of yard decor / furniture / house decor: a REAL fragment of the location where the slot of
// the item is (the big yard FVWorldU6::BuildYard or the big house FVWorldU6::BuildHouse) drawn inside a screen rect: the
// same background chunks (UFVSprites cache, the scene's keys), the equipped decor of the other slots in that window
// (DecorWorld at the DecorSlot anchors, y-sorted like the scene), the stations / yard buildings, and the tried item at its
// own place (animation frames, light). Integer pixel scale (whole device pixels per texel, nearest filtering). Works
// anywhere (forest, City...): it never touches the scene, the save or the world. Used by FVScreenBuilder (Прораб),
// FVScreenShop (boutique decor) and FVScreenDecor (the house decor catalog).

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class FVDraw;
class UFVSaveGame;
struct FFVDecorDef;

namespace FVDecorPreview
{
	/** true = the slot's place is the yard; false = the big house (classic slots and furniture). */
	bool IsYardPlace(EFVDecorSlot Slot);
	/** Russian place name for the caption: «двор · крыша дома», «дом · горница»... */
	FString PlaceName(EFVDecorSlot Slot);
	/** Draws the try-on block in R (frame + caption «Примерка: <место>» + the location fragment). Def = the tried item
	    (replaces the equipped item of its slot); P = the profile (equipped decor, station / building levels; may be null). */
	void Draw(FVDraw& D, const FFVRect& R, const FFVDecorDef& Def, const UFVSaveGame* P);
}
