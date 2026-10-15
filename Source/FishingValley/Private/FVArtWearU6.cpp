// UPDATE 6 [ART-WEAR] — new hats, outfits, bobbers and pets (forester, boutique, legendary rewards) (Docs/UPDATE6_FOREST_CITY.md §4, §8, §13, Р20). Owner: ART-WEAR.
// Contract: Public/FVSprites.h «UPDATE 6» (new enum values of EFVHatStyle / EFVOutfitStyle / EFVBobberStyle / EFVPetStyle are drawn by FVArtCharacter.cpp, which ART-WEAR owns; helpers may live here).
// Style: pixel art like FVSceneArt.cpp / FVArtHome.cpp (Stardew-like shading via FVArt::Shade/Light, 1px dark outline
// on props), deterministic (own FRandomStream), cached through UFVSprites::AddCached(MakeKey(FVSpriteCat::<cat>, ...)).

#include "FVSprites.h"
#include "FVDatabase.h"

namespace FVArtWearU6Impl
{
	// The new styles live next to the classic templates in FVArtCharacter.cpp (they share its Stamp / palette code):
	//   hats     Pompom, GoldHat, CaptainCap, Bicorne, MasterCrown     -> *Front / *Back / *Side + HatFor()
	//   outfits  Flannel, LumberVest, CaptainCoat, AdmiralUniform, LegendCloak -> *Down / *Up / *Right + TorsoTpl(), MakeStyle()
	//   bobbers  Diamond, Marlin, Kraken, Aurora, Anchor (2 frames)   -> BuildBobberImpl()
	//   pets     Squirrel, Hedgehog, CaptainCat, Peacock (Idle 2, Walk 4, Sit 2) -> PetColors(), BuildPetImpl()
	// HatIcon / OutfitIcon are built from the same templates. Elite pieces carry gold 'G' / glints 'Q' / 'w'.
}
