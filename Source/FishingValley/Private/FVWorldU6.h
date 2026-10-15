// UPDATE 6 — big locations (Docs/UPDATE6_FOREST_CITY.md §2, Р38): ONE composition for both orientations, in the
// style of the phone version; the camera follows the player; beyond the world edge the screen is BLACK.
// The art packages paint a world and describe it with FWorldDef; the scene (SCENE-CAMERA) turns it into its layout,
// SCENE-OBJECTS turns the anchors into interactive objects with state from the profile.
//   FVWorldU6::BuildForest  [ART-FOREST] FVArtForest.cpp
//   FVWorldU6::BuildYard    [ART-YARD]   FVArtYard.cpp
//   FVWorldU6::BuildHouse   [ART-HOUSE]  FVArtHouse.cpp
//   FVWorldU6::BuildCity    [ART-CITY]   FVArtCity.cpp
//   FVWorldU6::BuildCave    [ART-CAVE]   FVArtCave.cpp
//   FVWorldU6::BuildMill / MillZones  [MILL-ART] FVArtMill.cpp («Старая мельница», §18)
//   FVWorldU6::BuildWorld / IsBigWorldId  [SCENE-CAMERA] FVSceneCamera.cpp
// Rules for builders: deterministic (own FRandomStream seeded by a constant), pure CPU (no UObject / textures), world
// coordinates in WORLD PX = texel * FV::WorldScale, (0,0) = the world's top-left. Anchor ids are STABLE (saved).
// Only static things are painted into the image; everything with state (trees, piles, boulders, beds, buildings,
// villagers, the boat, decor) is an anchor drawn at runtime.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVSprites.h"

class UFVSaveGame;

namespace FVWorldU6
{
	enum class EAnchor : uint8
	{
		Spawn,        // Id = entry name ("default", "from_river", "from_map", "door", "pier", "from_forest"...)
		Exit,         // Id = entry name at the target; Target = target location id; Hit = trigger rect; Label = «В лес →»
		Tree,         // Id "tree_07"; Variant = EFVTreeKind; bFlag = choppable (marked) — others are decoration only
		Pickup,       // resource slot (Index = slot of FVDB::RollResource for this location; forest: branches / pebbles)
		JunkPile,     // Id "junk_03" (City)
		Boulder,      // Id "boulder_02" (Cave)
		Npc,          // Id = npc id (FVDBU6::Npcs)
		Stall,        // Id = trader id (a trader without an NPC sprite: counter / sign only)
		BuildPlot,    // Id = yard building id ("yb_sawmill" / "yb_garden" / "yb_forge"): plot or the building
		GardenBed,    // Index = bed 0..11
		Station,      // Id = house station id ("st_kitchen"...): the house interior and the yard (smokehouse, worm farm)
		Door,         // Id "house_door" (yard -> house) / "exit_door" (house -> yard); Hit = the door
		Boat,         // Id "boat"
		Blockage,     // Id "blockage" (forest)
		Rubble,       // Id "rubble_0..2" (City)
		NetSpot,      // Index = net spot (no net spots in the City / Cave, Р35)
		DigSpot,      // Index = dig spot (yard compost heap)
		DecorSlot,    // Index = EFVDecorSlot: where the equipped decor of this slot is drawn (yard decor, furniture)
		Board,        // Id "city_board" (City task board) / "order_board"
		Light,        // Radius = light radius, Color; bFlag = flickers
		Fire,         // animated flame (forge, hearth)
		Smoke,        // chimney smoke source
		// ---- update 6 (§18 «Старая мельница»)
		MillObj,      // Id = FVMill::Obj*Id() ("mill_bridge" in the forest; "mill", "beaver_lodge", "feed_deck" + Index 0..2,
		              // "fish_pen" at the mill) -> EFVObjKind::MillObj (sprite UFVSprites::MillObjSprite, label FVMill::ObjLabel)
		MillZone,     // Id "omut" / "deck_0".."deck_2"; Hit = where the player STANDS to fish from there (FVWorldU6::MillZones)
		Count
	};

	struct FAnchor
	{
		EAnchor Kind = EAnchor::Spawn;
		FName Id;
		int32 Index = 0;
		uint8 Variant = 0;
		bool bFlag = false;
		FVector2D Feet = FVector2D::ZeroVector;   // world px: bottom-centre of the sprite / where the player stands
		FFVRect Hit;                              // world px; W == 0 -> derived from the sprite
		FName Target;                             // Exit: target location
		FString Label;                            // Exit / Board label (Russian)
		FVector2D SignFeet = FVector2D::ZeroVector; // Exit (Р69): feet of the way sign with the picture of the target (0 = none)
		uint8 SignDir = 0;                        // Exit (Р69): arrow of the sign, FVWorldU6::EWayDir
		float Radius = 0.f;                       // Light
		FColor Color = FColor(255, 196, 110);     // Light
	};

	struct FWorldDef
	{
		FName LocationId;
		FIntPoint SizeTex = FIntPoint(640, 360);  // world size in texels (px = texel * FV::WorldScale)
		/** Walkable area = union of these rects minus Blockers (world px). */
		TArray<FFVRect> Walk;
		TArray<FFVRect> Blockers;
		/** Water for fishing (City pier, Cave lake). bWater = false for forest / yard / house. */
		bool bWater = false;
		float ShoreY = 0.f;                       // horizontal shoreline (world px); water below it inside WaterArea
		FFVRect WaterArea;                        // world px
		FFVRect Dock;                             // pier / plank into the water (world px; W == 0 = none)
		bool bNearPilesArea = false;              // City: fishing from PilesArea counts as «у свай»
		FFVRect PilesArea;
		// interiors (house): wallpaper / floor areas get the equipped decor tiles, windows get the sky
		TArray<FFVRect> WallAreas;
		TArray<FFVRect> FloorAreas;
		TArray<FFVRect> Windows;
		// the practice pond of the yard (update 4b onboarding: FVPracticePond) — world px
		bool bPond = false;
		FVector2D PondCenter = FVector2D::ZeroVector;
		FVector2D PondRadii = FVector2D::ZeroVector;
		FFVRect PondPlank;
		// ambience
		bool bBirds = false, bButterflies = false, bFireflies = false, bGulls = false, bFog = false, bDust = false;
		float DarkRadius = 0.f;                   // Cave: light circle around the player (0 = no darkness)
		FColor Ambient = FColor(255, 255, 255);   // tint multiplier of the night / cave lighting
		TArray<FAnchor> Anchors;
	};

	/** Builders (one per art package). OutBg = the whole world (SizeTex), opaque where the world is, transparent
	    (or black) outside irregular edges. Must fill OutDef.LocationId, SizeTex, Walk and at least one Spawn "default". */
	void BuildForest(FFVImage& OutBg, FWorldDef& OutDef);
	void BuildYard(FFVImage& OutBg, FWorldDef& OutDef);
	void BuildHouse(FFVImage& OutBg, FWorldDef& OutDef);
	void BuildCity(FFVImage& OutBg, FWorldDef& OutDef);
	void BuildCave(FFVImage& OutBg, FWorldDef& OutDef);
	/** [MILL-ART] «Старая мельница» (§18): pond with bWater (4 NetSpot anchors: 0..2 the pond, 3 = the Omut), the mill,
	    beaver lodge, 3 feeder decks, fish pen (MillObj anchors), the miller npc_prokop (Npc), MillZone anchors (the same
	    rects as MillZones()), Spawn "default" + "from_forest", Exit -> forest (Id "from_mill", Label «В лес»). */
	void BuildMill(FFVImage& OutBg, FWorldDef& OutDef);
	/** [MILL-ART] The fishing zones of the mill in world px (MillZone anchors: "omut", "deck_0".."deck_2"). Pure, cached;
	    FVGameU6::FillCatchContext tests the player's feet against them (no world image needed). */
	const TArray<FAnchor>& MillZones();

	/** [SCENE-CAMERA] Dispatch by location id (forest, home, house, city, cave, mill). false = not a big world. */
	bool BuildWorld(FName LocationId, FFVImage& OutBg, FWorldDef& OutDef);
	/** [SCENE-CAMERA] Big-world locations: forest, city, cave, yard, house (on by default; the command line flag
	    -fvsmallyard switches the yard / house back to the old one-screen scenes as a fallback). */
	bool IsBigWorldId(FName LocationId);

	// ---- Р69: walking between ALL locations without the map (FVSceneU6Objects.cpp; art: FVArtSigns.cpp)
	/** Arrow of a way sign / side of a classic exit. */
	enum class EWayDir : uint8 { Left, Right, Up, Down };
	/** Picture on a way sign (UFVSprites::WaySignSprite). */
	enum class EWayPicto : uint8 { River, Forest, Pond, Mountain, Ocean, Swamp, Volcano, Mill, City, Cave, Home, Count };
	EWayPicto WayPictoFor(FName Target);
	/** A walk link of a one-screen (classic) location: Side = edge of the walk band (Left / Right / Up = a path up);
	    Frac = where along the edge (0..1 of the band height for Left / Right, of the width for Up).
	    Hotfix 0.61: FracPort = Frac of the portrait composition (< 0 = Frac); SignLand / SignPort = a hand-placed way sign
	    (landscape / portrait; zero = the automatic search): side exits — X inward from the walk edge, Y from the exit's
	    feet; Up — X from the exit's feet, Y from the top of the walk band. Still checked against the stall / board / HUD. */
	struct FWalkLink
	{
		FName To;
		EWayDir Side = EWayDir::Left;
		float Frac = 0.5f;
		float FracPort = -1.f;
		FVector2D SignLand = FVector2D::ZeroVector, SignPort = FVector2D::ZeroVector;
	};
	/** Р69 graph (the map's geography) for the classic locations; empty for big worlds (their Exit anchors). */
	TArray<FWalkLink> ClassicWalkLinks(FName From);
	/** Can the player walk into Target now (unlocked, or the forest / mill / City / Cave conditions as on the map)?
	    OutCaption = «Лесная река» or «Закрыто: Горное озеро — ур. 3, откройте на карте»; OutWhy = the refusal toast. */
	bool WalkTargetOpen(const UFVSaveGame* P, FName Target, FString* OutCaption = nullptr, FString* OutWhy = nullptr);
	/** Arrow glyph of a direction («←» ...). */
	const TCHAR* WayArrow(EWayDir Dir);

	/** Shared helper for placeholders (implemented in FVSceneCamera.cpp): a flat world of SizeTex with a checker, a
	    frame and a "default" spawn in the middle. Builders call it until their art is ready. */
	void MakePlaceholderWorld(FName LocationId, FIntPoint SizeTex, FColor Ground, FFVImage& OutBg, FWorldDef& OutDef);
}
