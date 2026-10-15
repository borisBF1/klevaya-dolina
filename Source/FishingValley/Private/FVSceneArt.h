// Procedural pixel art of the fishing locations: 320x180-texel backgrounds (terrain, water, dock, backdrop decor)
// plus small decor sprites used by the animated layers (lily pads, reeds, birds, props...). Implemented in FVSceneArt.cpp.
// Private to the world module (FVScene.cpp, FVSceneHome.cpp). UPDATE 2: home yard / house interior layouts, world-object
// anchors (FLayout) and the runtime state of FVScene (FVScene::FImpl, at the end of this header).

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVSprites.h"
#include "FVWorldU6.h"     // update 6: big worlds

namespace FVSceneArt
{
	constexpr int32 BgW = 320;   // landscape visible area in texels (drawn at FV::WorldScale -> 640x360 virtual)
	constexpr int32 BgH = 180;

	/** Bleed: every background extends beyond the canvas edges so letterbox areas and the space under the UI strips
	    always show the world. Landscape: the 320x180 picture is mirrored outwards. Portrait: painted for real. */
	constexpr int32 LandBleedX = 56;     // texels on the left and on the right (112 virtual px)
	constexpr int32 LandBleedY = 32;     // texels above and below (64 virtual px)
	constexpr int32 PortBleedX = 40;     // 80 virtual px
	constexpr int32 PortBleedY = 32;     // 64 virtual px
	constexpr int32 PortVisW = 180;      // portrait visible width in texels (360 virtual px)

	/** Background size (texels, bleed included) for the CURRENT virtual canvas: landscape (320+2*56) x (180+2*32);
	    portrait (180+2*40) x (Hv+2*32) where Hv = VH/2 rounded up to a multiple of 8 texels (few cache variants while
	    a desktop window is being resized; the scene aligns the shoreline to VH/2 exactly when it draws). */
	FIntPoint CanvasBgSize();
	/** Hotfix 0.61: the background size of Loc for the current canvas. Loc.SideWiden (pond only) widens the content
	    by SideStrip texels on each side, plus the HUD pad right of it (landscape: painted, 40 texels; portrait: inside
	    a 42-texel painted bleed); any other location = CanvasBgSize(). Landscape pond 536x244, portrait 300xH. */
	FIntPoint CanvasBgSize(const FFVLocationDef& Loc);
	/** Hotfix 0.61: side strip of Loc in texels (0 = one-screen layout; only EFVTheme::Pond honours SideWiden). */
	int32 SideStrip(const FFVLocationDef& Loc, bool bPortrait);
	/** Hotfix 0.61: the camera may over-scroll right by this many virtual px so the new right strip is never under the
	    HUD button column (landscape / portrait). */
	constexpr float ScrollPadLand = 80.f;
	constexpr float ScrollPadPort = 56.f;

	/** Small sprites used by the scene's animated layers and y-sorted props. */
	enum class EDecor : uint8
	{
		Shadow, ShadowSmall,
		LilyPad, LilyPadFlower, LilyPadSmall,
		Reed0, Reed1, Reed2,
		Cattail0, Cattail1, Cattail2,
		Bird0, Bird1, Gull0, Gull1,
		Butterfly0, Butterfly1,
		JumpFish,
		Ripple0, Ripple1, Ripple2, Ripple3, Ripple4, Ripple5,
		Fog,
		// y-sorted foreground props (feet = bottom-center pivot)
		LampPost, Barrel, Bush, Stump, Boulder, SnowBoulder, Campfire, Crates, Lifebuoy, DeadStump,
		GlowShrooms, Crystal, Brazier, BasaltRock, YoungPine, BeachUmbrella, Signpost,
		// update 2: home yard props
		Well, Woodpile, AppleTree, Scarecrow,
		Count
	};
	constexpr int32 RippleFrames = 6;

	/** A light source (virtual px). Glows at night; bAlways lights also glow (dimmer) during the day. */
	struct FLight
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float Radius = 30.f;
		FColor Color = FColor(255, 196, 110);
		bool bFlicker = false;
		bool bAlways = false;
		FFVRect Window;              // optional lit window pane (W == 0 -> none), filled with warm light at night
	};

	struct FProp
	{
		EDecor Kind = EDecor::Barrel;
		FVector2D Feet = FVector2D::ZeroVector;   // virtual px (bottom-center)
		bool bFlip = false;
	};

	/** Everything the scene needs to know about a location's geometry and ambience (virtual px). */
	struct FLayout
	{
		float ShoreY = 190.f;
		FFVRect Dock;                // Y = top (starts on land), Bottom() = end of the dock in the water
		bool bRockPier = false;
		float WalkTop = 126.f;       // min feet Y on land
		float WalkMinX = 12.f;
		float WalkMaxX = 628.f;
		TArray<FFVRect> Blockers;    // collision rects for the feet
		TArray<FLight> Lights;
		TArray<FProp> Props;
		TArray<FVector2D> LilyPads;  // centers
		TArray<uint8> LilyKinds;     // 0 = pad, 1 = pad with flower, 2 = small pad
		TArray<FVector2D> Reeds;     // bottom points (animated reeds / cattails)
		TArray<FVector2D> FlowerSpots;   // butterfly homes
		TArray<FVector2D> Fires;     // animated flames (base point)
		TArray<FVector2D> Smokes;    // chimney / crater smoke sources
		FVector2D Spawn = FVector2D(260, 180);

		bool bCattails = false;      // animated reeds are cattails
		bool bFireflies = false;
		bool bButterflies = false;
		bool bBirds = false;
		bool bGulls = false;
		bool bFog = false;
		bool bLava = false;
		bool bEmbers = false;
		bool bBubbles = false;
		bool bWaves = false;         // rolling foam waves (ocean)
		float Current = 4.f;         // horizontal drift of water glints, px/s
		FColor Glint = FColor(235, 250, 255);
		FColor Foam = FColor(240, 250, 255);
		FColor FogColor = FColor(190, 210, 190);
		FColor BirdColor = FColor(60, 50, 60);

		// ---- additive: canvas the layout was built for
		int32 TexW = BgW;            // background size in texels (bleed included)
		int32 TexH = BgH;
		bool bPortrait = false;      // tall composition (phones held upright)
		int32 BleedX = 0;            // texels of bleed on each side of the visible area (nominal frame; landscape: the texel of
		                             // canvas x = 0 — the wide pond's left strip lies left of it, see ScrollRect)
		int32 BleedY = 0;
		/** Canvas position (virtual px) of the background's texel (0,0). BuildLocation returns every coordinate of
		    the layout in TEXTURE space (texel * 2, Origin = 0); FVScene translates the layout by the origin of the
		    current canvas (TranslateLayout) — portrait puts the shoreline exactly at VH / 2. */
		FVector2D Origin = FVector2D::ZeroVector;
		FColor FarTop = FColor(90, 150, 70);     // fallback fills beyond the bleed (top rows / land sides / water)
		FColor FarLand = FColor(90, 150, 70);
		FColor FarWater = FColor(30, 80, 150);
		/** Hotfix 0.61: a classic layout wider than the screen (the pond with SideWiden): the rect the camera may show
		    (content + the right HUD pad). Texture px after BuildLocation, canvas px after TranslateLayout (translated
		    only when W > 0). W == 0 = one screen, no camera. */
		FFVRect ScrollRect;

		// ---- UPDATE 2: home scenes and world-object anchors. Same space as every other coordinate of the layout
		//      (texture space after BuildLocation, canvas after TranslateLayout: EVERY field below is translated).
		bool bNoWater = false;           // yard / house interior: no water, no fishing
		bool bInterior = false;          // house interior: wall band + floor drawn from the equipped decor tiles
		float WalkBottom = 0.f;          // no-water scenes: max feet Y (water scenes walk down to ShoreY - 2)
		TArray<FVector2D> ResourceSlots; // pickup spots (feet); counts per location = UPDATE_HOME.md §6
		TArray<FVector2D> DigSpots;      // soft soil centres
		TArray<FVector2D> NetSpots;      // net posts (feet at ShoreY - 4); the net hangs in the water below
		bool bStall = false;
		FVector2D StallFeet = FVector2D::ZeroVector;   // trader stall: bottom-centre of the sprite
		bool bBoard = false;
		FVector2D BoardFeet = FVector2D::ZeroVector;   // order board: bottom-centre of the sprite
		bool bDoor = false;
		FVector2D DoorFeet = FVector2D::ZeroVector;    // where the player stands to use the (baked) door
		FFVRect DoorRect;                              // the door in the background (click / tap rect)
		// interior
		FFVRect WallRect;                // wallpaper area (bleed included)
		FFVRect FloorRect;               // floor area (bleed included)
		TArray<FFVRect> Windows;         // window glass (sky drawn behind the baked frames, curtains on top)
		TArray<FVector2D> CurtainSpots;  // update 6: big house — Curtains DecorSlot anchors (feet = bottom-centre); empty = over Windows
		TArray<FVector2D> Stations;      // index = EFVStation: sprite bottom-centre (TrophyWall: centre on the wall)
		FVector2D DresserFeet = FVector2D::ZeroVector;
		FVector2D RugCenter = FVector2D::ZeroVector;
		FVector2D LampFeet = FVector2D::ZeroVector;
		FVector2D PlantFeet = FVector2D::ZeroVector;
		FVector2D PaintingCenter = FVector2D::ZeroVector;

		// ---- UPDATE 4b: the practice pond of the home yard (an oval of water inside a no-water scene). Translated.
		bool bPond = false;
		FVector2D PondCenter = FVector2D::ZeroVector;   // centre of the water oval
		FVector2D PondRadii = FVector2D::ZeroVector;    // half width / half height of the water (virtual px, not moved)
		FFVRect PondPlank;                              // the little wooden jetty (from the north bank into the water)

		// ---- UPDATE 6 [SCENE-CAMERA]: big worlds (FVWorldU6). World px, Origin = 0, never translated by the canvas.
		bool bBigWorld = false;
		FVector2D WorldSize = FVector2D::ZeroVector;    // world px
		TArray<FFVRect> WalkRects;                      // walkable = inside any of them (and not in Blockers)
		FFVRect WaterArea;                              // water inside a big world (W == 0 = none)
		// ---- UPDATE 6: the boat of the ocean pier (§7) replaces the right net spot. Translated like other anchors.
		//      (big worlds: the Boat anchor of the City pier, world px)
		bool bBoat = false;
		FVector2D BoatFeet = FVector2D::ZeroVector;
	};

	/** Update 6: an anchor slot the big world does not have (gaps of ResourceSlots / DigSpots / NetSpots / Stations,
	    interior decor points of a big house without them) is put here, far outside every world. */
	constexpr float UnplacedCoord = -10000.f;
	FORCEINLINE bool IsPlaced(const FVector2D& P) { return P.X > UnplacedCoord * 0.5f && P.Y > UnplacedCoord * 0.5f; }
	/** Update 6 [SCENE-CAMERA] (FVSceneCamera.cpp): the layout of a big world (world px, bBigWorld = true). Anchors ->
	    ResourceSlots (Pickup, by Index), DigSpots, NetSpots (by Index), Door, Stations (by station id; also "dresser"),
	    decor points (DecorSlot Rug / Lamp / Plant / Painting), order board, boat, lights, fires, smokes, pond. */
	void LayoutFromWorld(const FVWorldU6::FWorldDef& Def, const FFVLocationDef& Loc, FLayout& Out);

	/** Canvas origin of a layout's background for the current virtual canvas (see FLayout::Origin). */
	FVector2D CanvasOrigin(const FLayout& TexLayout);
	/** Moves every coordinate of the layout by Delta and adds Delta to Origin. */
	void TranslateLayout(FLayout& L, const FVector2D& Delta);

	/** Builds the landscape background of a location (320x180 + mirrored bleed) and its layout. Deterministic. */
	void BuildLocation(const FFVLocationDef& Loc, FFVImage& OutBg, FLayout& OutLayout);
	/** Builds the background for an explicit texel size (bleed included). TexW >= TexH -> the landscape composition
	    (320x180 picture + mirrored bleed); otherwise the tall portrait composition (land above the middle of the
	    screen, water below, long dock). Layout coordinates are in texture space (see FLayout::Origin).
	    Hotfix 0.61: the pond with SideWiden (SideStrip > 0) is wider than the screen — every old object keeps its
	    canvas px, the new strips lie at canvas x < 0 and x > VW (landscape -64..704, portrait -36..396, walkable
	    -52..692 / -18..378), FLayout::ScrollRect = content + the HUD pad; pass CanvasBgSize(Loc) as the size. */
	void BuildLocation(const FFVLocationDef& Loc, int32 TexW, int32 TexH, FFVImage& OutBg, FLayout& OutLayout);

	FFVImage BuildDecor(EDecor D);
	/** Anchor of a decor sprite as a fraction of its size: (0.5, 1) = bottom-center (props, reeds), (0.5, 0.5) = center. */
	FVector2D DecorPivot(EDecor D);
	/** Foot collision size in virtual px for props (0,0 = walk-through). */
	FVector2D DecorBlocker(EDecor D);
}

// =============================================================================================
// Runtime state of FVScene, shared by FVScene.cpp (water, ambience, player) and FVSceneHome.cpp (UPDATE 2:
// world objects, home yard, house interior). Private to the world module.
// =============================================================================================
#include "FVScene.h"
#include "FVSceneU6.h"     // update 6: world objects of big worlds (SCENE-OBJECTS)

class UFVSaveGame;

namespace FVSceneRt
{
	using namespace FVSceneArt;

	constexpr float WS = FV::WorldScale;
	constexpr int32 MaxParticles = 500;
	constexpr int32 MaxRipples = 120;

	enum class EPart : uint8 { Drop, Sparkle, Smoke, Ember };

	struct FGlint { FVector2D P = FVector2D::ZeroVector; float Age = 0.f; float Life = 1.f; float W = 4.f; bool bSparkle = false; };
	struct FParticle
	{
		FVector2D P = FVector2D::ZeroVector;
		FVector2D V = FVector2D::ZeroVector;
		float Age = 0.f, Life = 1.f, Size = 2.f, Gravity = 0.f;
		FLinearColor C = FLinearColor::White;
		EPart Kind = EPart::Drop;
	};
	struct FRippleFx { FVector2D P = FVector2D::ZeroVector; float Age = 0.f; float Life = 1.f; float Size = 1.f; };
	struct FFloatText { FVector2D P = FVector2D::ZeroVector; FString Text; FLinearColor C = FLinearColor::White; float Age = 0.f; float Life = 1.8f; };
	struct FBird { FVector2D P = FVector2D::ZeroVector; FVector2D V = FVector2D::ZeroVector; float Phase = 0.f; };
	struct FButterfly { FVector2D Home = FVector2D::ZeroVector; FVector2D Target = FVector2D::ZeroVector; FVector2D P = FVector2D::ZeroVector; float Phase = 0.f; float Speed = 1.f; float Retarget = 5.f; FLinearColor C = FLinearColor::White; };
	struct FFirefly { FVector2D P = FVector2D::ZeroVector; float Heading = 0.f; float Phase = 0.f; float Speed = 10.f; };
	struct FJump { FVector2D A = FVector2D::ZeroVector; FVector2D B = FVector2D::ZeroVector; float Age = 0.f; float Life = 0.8f; float H = 18.f; };
	struct FBubble { FVector2D P = FVector2D::ZeroVector; float Age = 0.f; float Life = 1.f; float R = 2.f; };
	struct FFogBand { float X = 0.f; float Y = 0.f; float Speed = 6.f; float Scale = 3.f; float Alpha = 0.25f; };

	/** A drawable world entity (UPDATE 2): stall, board, net post / net, pickup, dig spot, station, decor... */
	struct FVis
	{
		uint8 Layer = 3;                 // 0 ground (flat), 1 water (nets), 2 wall, 3 y-sorted
		uint8 Style = 0;                 // 0 sprite, 1 pickup (icon + bob + sparkle), 2 aquarium (sprite + fish)
		int32 Obj = INDEX_NONE;          // index into FImpl::Objects (INDEX_NONE = decoration only)
		FFVSprite S;
		FVector2D Pos = FVector2D::ZeroVector;     // anchor (canvas px)
		FVector2D Pivot = FVector2D(0.5f, 1.f);   // anchor as a fraction of the sprite size
		float Scale = WS;
		float SortY = 0.f;
		bool bShadow = true;
		int32 Count = 1;                 // pickups: 1..2
		float Phase = 0.f;               // animation phase (bob / sway)
		TArray<FFVSprite> Frames;        // update 6: optional animation frames of a sprite vis (same size as S)
		float FrameFps = 0.f;            // > 0 with 2+ Frames = animated
		/** Top-left corner / rect of the drawn sprite (canvas px). */
		FVector2D TopLeft() const { return Pos - FVector2D(S.W * Pivot.X, S.H * Pivot.Y) * Scale; }
		FFVRect Rect() const { const FVector2D TL = TopLeft(); return FFVRect(float(TL.X), float(TL.Y), S.W * Scale, S.H * Scale); }
	};
	struct FAquaFish { FFVSprite S; float Phase = 0.f; float Speed = 1.f; float YFrac = 0.5f; float Scale = 1.f; };
	struct FRtLight { FVector2D Pos = FVector2D::ZeroVector; float Radius = 40.f; FColor Color = FColor(255, 196, 110); bool bFlicker = false; float DayInt = 0.f; };

	FORCEINLINE float Snap(double V, float Step = 1.f) { return FMath::RoundToFloat(float(V) / Step) * Step; }
	FORCEINLINE FVector2D Snap2(const FVector2D& V, float Step = 1.f) { return FVector2D(Snap(V.X, Step), Snap(V.Y, Step)); }
	FORCEINLINE float Bump(float X, float C, float R) { return FMath::Max(0.f, 1.f - FMath::Abs(X - C) / R); }
	FORCEINLINE FLinearColor LinA(const FColor& C, float A) { FLinearColor L = FV::Col(C); L.A = FMath::Clamp(A, 0.f, 1.f); return L; }
	FORCEINLINE FLinearColor WithAlpha(FLinearColor C, float A) { C.A = FMath::Clamp(A, 0.f, 1.f); return C; }

	inline float AnimFps(EFVCharAnim A)
	{
		switch (A)
		{
		case EFVCharAnim::Walk: return 8.f * FV::PlayerSpeed / 150.f;   // the step keeps up with the walking speed (0.6: +15 %)
		case EFVCharAnim::Idle: return 2.f;
		case EFVCharAnim::FishWait: return 2.f;
		case EFVCharAnim::Reel: return 8.f;
		default: return 1.f;
		}
	}

	inline float PetFps(EFVPetAnim A)
	{
		switch (A)
		{
		case EFVPetAnim::Walk: return 8.f;
		case EFVPetAnim::Sit: return 1.5f;
		default: return 2.f;
		}
	}

	// ---- UPDATE 4b: practice pond geometry (see FLayout::bPond). All in canvas px.
	/** Normalised squared distance from the pond centre (< 1 = inside the water oval grown by Grow px). */
	inline float PondDist2(const FLayout& L, const FVector2D& P, float Grow = 0.f)
	{
		const float RX = FMath::Max(1.f, float(L.PondRadii.X) + Grow), RY = FMath::Max(1.f, float(L.PondRadii.Y) + Grow * 0.7f);
		const float DX = float(P.X - L.PondCenter.X) / RX, DY = float(P.Y - L.PondCenter.Y) / RY;
		return DX * DX + DY * DY;
	}
	/** Half height of the water oval at column X (0 outside). */
	inline float PondHalfH(const FLayout& L, float X)
	{
		const float DX = (X - float(L.PondCenter.X)) / FMath::Max(1.f, float(L.PondRadii.X));
		return DX * DX >= 1.f ? 0.f : float(L.PondRadii.Y) * FMath::Sqrt(1.f - DX * DX);
	}
	/** Feet on the plank jetty (it reaches into the water). */
	inline bool OnPondPlank(const FLayout& L, const FVector2D& P)
	{
		return L.bPond && L.PondPlank.W > 0.f && P.X >= L.PondPlank.X + 6.f && P.X <= L.PondPlank.Right() - 6.f
			&& P.Y >= L.PondPlank.Y && P.Y <= L.PondPlank.Bottom() - 6.f;
	}
	/** Open water of the pond: inside the oval (Grow < 0 = keep away from the banks), below the earth bank of the far
	    side, not under the plank. */
	inline bool InPondWater(const FLayout& L, const FVector2D& P, float Grow = 0.f)
	{
		if (!L.bPond || PondDist2(L, P, Grow) >= 1.f) { return false; }
		if (P.Y < L.PondCenter.Y - PondHalfH(L, float(P.X)) + 5.f) { return false; }   // the brown bank face
		const FFVRect& K = L.PondPlank;
		return !(K.W > 0.f && P.X >= K.X - 2.f && P.X <= K.Right() + 2.f && P.Y <= K.Bottom() + 3.f);
	}

	/** Feet walkability: the land band (or the floor / yard of a home scene), the dock, minus blockers. */
	inline bool IsWalkable(const FLayout& L, const FVector2D& P)
	{
		if (L.bBigWorld)
		{
			// update 6: big worlds walk inside WalkRects (world px), no screen-space safety net (the camera follows)
			// 0.6: a walk rect that starts at the shore line is a jetty into the water (the City berth jetty: the player
			// stopped at its root, the water rule below ate it) — walkable like the dock
			bool bIn = false, bJetty = false;
			for (const FFVRect& R : L.WalkRects)
			{
				if (!R.Contains(P)) { continue; }
				bIn = true;
				if (!L.bNoWater && R.Y >= L.ShoreY - 32.f) { bJetty = true; break; }
			}
			const bool bOnDock = bJetty || (L.Dock.W > 0.f && P.X >= L.Dock.X + 8.f && P.X <= L.Dock.Right() - 8.f && P.Y >= L.Dock.Y && P.Y <= L.Dock.Bottom() - 6.f);
			if (!bIn && !bOnDock) { return false; }
			// the water of the world is never walkable (except the dock)
			if (!bOnDock && !L.bNoWater && L.WaterArea.W > 0.f && P.Y > L.ShoreY - 2.f && P.X >= L.WaterArea.X && P.X < L.WaterArea.Right()
				&& P.Y < L.WaterArea.Bottom()) { return false; }
			if (L.bPond && PondDist2(L, P, 1.f) < 1.f && !OnPondPlank(L, P)) { return false; }
			for (const FFVRect& B : L.Blockers) { if (B.Contains(P)) { return false; } }
			return true;
		}
		if (P.X < L.WalkMinX || P.X > L.WalkMaxX) { return false; }
		// Safety net: the feet never go under the bottom strip / button dock, the head never under the top strip.
		if (P.Y > FV::VH - FV::BottomBarH - 10.f || P.Y < FV::TopBarH + 66.f) { return false; }
		bool bOk;
		if (L.bNoWater)
		{
			bOk = P.Y >= L.WalkTop && P.Y <= L.WalkBottom;
			// the practice pond: the feet stay on its banks (or on the plank)
			if (bOk && L.bPond && PondDist2(L, P, 1.f) < 1.f && !OnPondPlank(L, P)) { bOk = false; }
		}
		else
		{
			bOk = P.Y >= L.WalkTop && P.Y <= L.ShoreY - 2.f;
			if (!bOk && L.Dock.W > 0.f)
			{
				bOk = P.X >= L.Dock.X + 8.f && P.X <= L.Dock.Right() - 8.f && P.Y >= L.Dock.Y && P.Y <= L.Dock.Bottom() - 6.f;
			}
		}
		if (!bOk) { return false; }
		for (const FFVRect& B : L.Blockers) { if (B.Contains(P)) { return false; } }
		return true;
	}

	inline const FFVLocationDef& FallbackLocation()
	{
		static FFVLocationDef Def;
		static bool bInit = false;
		if (!bInit)
		{
			bInit = true;
			Def.Id = FName(TEXT("pond"));
			Def.Name = TEXT("Деревенский пруд");
			Def.Theme = EFVTheme::Pond;
		}
		return Def;
	}
}

struct FVScene::FImpl
{
	using FLayout = FVSceneArt::FLayout;
	using EDecor = FVSceneArt::EDecor;

	struct FLocCache { FFVSprite Bg; FLayout Layout; };   // layout in texture space (see FLayout::Origin)
	TMap<uint64, FLocCache> Cache;   // per location AND background size (bleed included)
	int32 CanvasRev = -1;            // FV::CanvasRevision the layout was synced with
	FIntPoint BgSize = FIntPoint(FVSceneArt::BgW + 2 * FVSceneArt::LandBleedX, FVSceneArt::BgH + 2 * FVSceneArt::LandBleedY);
	FFVRect WorldR = FFVRect(0.f, 0.f, 640.f, 360.f);   // canvas rect covered by the background (bleed included)
	FVector2D LayoutCanvas = FVector2D(640.f, 360.f);    // VW x VH the active layout was placed for
	bool bAtSpawn = true;            // the player has not moved since the location was loaded
	bool bSpawnAtDoor = false;       // ...and that spot is the door (came in / out of the house)
	float FlyTop = 110.f, FlyBottom = 240.f;   // firefly band (virtual px)
	float FlyX0 = 10.f, FlyX1 = 630.f;

	const FFVLocationDef* Loc = nullptr;
	FFVSprite Bg;
	FLayout L;
	FFVSprite Decor[int32(EDecor::Count)];
	bool bDecorReady = false;

	// pet
	EFVPetStyle PetStyle = EFVPetStyle::None;
	FVector2D PetPos = FVector2D::ZeroVector;
	bool bPetFlip = false;
	EFVPetAnim PetAnim = EFVPetAnim::Idle;
	float PetAnimTime = 0.f;
	float PetIdleTime = 0.f;
	TArray<FVector2D> Trail;

	// effects & ambience
	TArray<FVSceneRt::FGlint> Glints;
	TArray<FVSceneRt::FParticle> Particles;
	TArray<FVSceneRt::FRippleFx> Ripples;
	TArray<FVSceneRt::FFloatText> Texts;
	TArray<FVSceneRt::FBird> Birds;
	TArray<FVSceneRt::FButterfly> Butterflies;
	TArray<FVSceneRt::FFirefly> Fireflies;
	TArray<FVSceneRt::FJump> Jumps;
	TArray<FVSceneRt::FBubble> Bubbles;
	TArray<FVSceneRt::FFogBand> Fog;
	float NextJump = 5.f, NextBird = 4.f, NextBubble = 1.f, NextSmoke = 0.f, NextRainRipple = 0.f, NextEmber = 0.f, NextPostRipple = 0.f;
	float Time = 0.f;
	int32 LastWalkFrame = -1;
	FRandomStream Rng;

	// ---- UPDATE 2: world objects (FVSceneHome.cpp)
	TArray<FFVWorldObject> Objects;          // interactive objects of the current scene (canvas px)
	TArray<FVSceneRt::FVis> Vis;             // what is drawn for them (+ decor of the house)
	int32 BaseBlockers = 0;                  // L.Blockers[0..BaseBlockers) come from the layout, the rest from objects
	uint32 WorldSig = 0;                     // profile state the objects were built from
	float WorldSigTimer = 0.f;
	bool bHighlight = false;
	FFVWorldObject Highlight;                // copy (object pointers may be rebuilt)
	// auto-walk
	bool bWalk = false, bWalkReached = false, bWalkFailed = false;
	FVector2D WalkTarget = FVector2D::ZeroVector;
	float WalkReach = 30.f, WalkBest = 1.e9f, WalkStuck = 0.f;
	float WalkSide = 1.f;
	// pickup feedback
	float PickupAge = -1.f;
	FVector2D PickupFrom = FVector2D::ZeroVector;
	FFVSprite PickupIcon;
	// interior
	FFVSprite WallTile, FloorTile;
	FColor WallCol = FColor(170, 120, 70), FloorCol = FColor(150, 100, 60);
	TArray<FVSceneRt::FAquaFish> AquaFish;
	int32 AquaVis = INDEX_NONE;              // Vis index of the aquarium
	int32 AquaLevel = 0;
	TArray<FVSceneRt::FRtLight> RtLights;    // lamp, kitchen fire (runtime, profile dependent)
	TArray<FVector2D> RtSmokes;              // smokehouse smoke sources
	float NextRtSmoke = 0.f;
	TArray<FFVRect> NetRects;                // nets hanging in the water (casts there are not "in the water")
	bool bArtPending = false;                // some world sprites were not available yet: rebuild when they are

	FImpl() { Rng.Initialize(0x5EA5CE); }

	const FFVSprite& D(EDecor K) const { return Decor[FMath::Clamp(int32(K), 0, int32(EDecor::Count) - 1)]; }

	// ---- implemented in FVScene.cpp
	void EnsureDecor();
	void ApplyLayout(const FLocCache& Cached);
	FFVRect WaterRect() const;
	float BleedMul() const;
	static uint64 CacheKey(FName Id, const FIntPoint& Size);
	const FLocCache& EnsureLocation(const FFVLocationDef& InLoc, const FIntPoint& Size);
	void InitAmbient(float ShoreY);
	FVector2D MapPoint(const FVector2D& P, const FLayout& Old, float OldVW) const;
	FVector2D FindWalkable(const FVector2D& P) const;
	void SyncCanvas(FVScene& S);
	bool OnWaterSurface(const FVector2D& P) const;
	FVector2D RandomWaterPoint(float MinDepthPx, float MaxY, bool bWide = false) const;

	// ---- implemented in FVSceneHome.cpp (UPDATE 2)
	UFVSaveGame* Profile(const FVScene& S) const;
	/** Rebuilds Objects / Vis / dynamic blockers / runtime lights from the layout and the profile. */
	void RebuildWorld(FVScene& S);
	/** RebuildWorld keeping the auto-walk target on its object and the player on walkable ground. */
	void RebuildKeepingState(FVScene& S);
	uint32 WorldSignature(const FVScene& S) const;
	/** Profile watch (rebuild when the state changed), pickup animation, smokehouse smoke. */
	void TickWorld(FVScene& S, float Dt);
	/** Auto-walk direction for this frame (zero = none). */
	FVector2D WalkSteer(const FVScene& S, float Dt);
	/** After the movement step: reached / stuck. */
	void UpdateWalkProgress(const FVScene& S, float Dt);
	/** Interior: wallpaper, floor, window sky (drawn before the background, which has holes there). */
	void DrawInteriorBack(FVDraw& Dr, const FVScene& S);
	/** Flat layers: 0 ground, 1 water (nets), 2 wall. */
	void DrawWorldLayer(FVDraw& Dr, const FVScene& S, uint8 Layer);
	/** One entity of Vis (y-sorted or flat). */
	void DrawVis(FVDraw& Dr, const FVScene& S, int32 VisIndex);
	/** Pickup feedback (above the y-sorted block). */
	void DrawWorldAfter(FVDraw& Dr, const FVScene& S);
	void DrawWorldLights(FVDraw& Dr, const FVScene& S, float Night);
	void DrawWorldOverlay(FVDraw& Dr, const FVScene& S);
	bool IsHighlighted(int32 ObjIndex) const;

	// ---- UPDATE 4b: practice pond (FVScene.cpp)
	float NextPondRipple = 1.f;
	/** Random point of the open pond water (canvas px). */
	FVector2D RandomPondPoint() const;
	/** Player feet at a spot the practice pond can be fished from (the plank or its north bank). */
	bool AtPondSpot(const FVector2D& Feet) const;

	// ---- UPDATE 6 [SCENE-CAMERA] (FVSceneCamera.cpp): camera & big worlds
	bool bBig = false;                               // the current location is a big world (FVWorldU6)
	FVector2D Cam = FVector2D::ZeroVector;           // world px at the screen's top-left
	bool bCamSnap = true;                            // next update jumps instead of easing
	int32 WorldViewDepth = 0;                        // BeginWorldView / EndWorldView balance
	TArray<FFVSprite> BgChunks;                      // big background split in chunks (only visible ones are drawn)
	TArray<FFVRect> BgChunkRects;                    // world px of each chunk
	TSharedPtr<FVWorldU6::FWorldDef> BigDef;         // the anchors etc. of the current big world (world px)
	bool bExitPending = false;
	FName ExitLocation, ExitEntry;
	FName PendingEntry;                              // LoadLocationAt: entry to place the player at
	void UpdateCamera(const FVScene& S, float Dt);
	/** Hotfix 0.61: the view follows the player — a big world, or a classic layout wider than the screen (ScrollRect). */
	bool HasCamera() const { return bBig || L.ScrollRect.W > FV::VW + 0.5f; }
	// (implementation details of SCENE-CAMERA)
	FVector2D CamSmooth = FVector2D::ZeroVector;     // un-rounded camera (Cam = rounded CamSmooth)
	FVector2D CamLastPlayer = FVector2D::ZeroVector; // a jump of the player (teleport) snaps the camera
	bool bExitArmed = false;                         // exits fire only after the player stood outside all of them
	/** Р69: exits of a one-screen (classic) location (canvas px), rebuilt by RebuildWorldU6 from FVWorldU6::ClassicWalkLinks:
	    Kind Exit, Id = entry at the target ("from_<this>"), Target, Hit = trigger at the walk edge, Feet = the edge point,
	    SignFeet / SignDir. TickBigWorld fires them like the Exit anchors of big worlds. */
	TArray<FVWorldU6::FAnchor> WalkExits;
	struct FBigCache
	{
		FLayout Layout;                              // world px
		TSharedPtr<FVWorldU6::FWorldDef> Def;
		TArray<FFVSprite> Chunks;                    // invalid = fully transparent chunk (or texture pending)
		TArray<FFVRect> ChunkRects;
		TSharedPtr<FFVImage> PendingImg;             // kept only until the chunk textures exist
	};
	TMap<FName, FBigCache> BigCache;                 // per big location (one composition for every canvas)
	FName BigId;                                     // key of the active big world in BigCache
	float NextDarkFlick = 0.f;
	/** Builds (or fetches) the big world of Loc and makes it active. false = not buildable (stay classic). */
	bool ApplyBigWorld(const FFVLocationDef& InLoc);
	/** Creates missing chunk textures once UFVSprites exists. */
	void EnsureBigTextures();
	/** Feet of the named entry (Spawn anchor Id); unknown / None -> "default" -> L.Spawn. */
	FVector2D BigEntryFeet(FName Entry) const;
	/** Exits + camera, after the movement step of FVScene::Tick. */
	void TickBigWorld(FVScene& S, float Dt);
	/** The rect of the world visible on the device screen (local coords of D, letterbox included). */
	FFVRect VisibleRect(const FVDraw& Dr) const;
	/** The background chunks that intersect the visible rect (the black beyond the world is filled by DrawScene). */
	void DrawBigBackground(FVDraw& Dr) const;
	/** Cave darkness (a light circle around the player) + ambient tint of big worlds. */
	void DrawBigDarkness(FVDraw& Dr, const FVScene& S) const;

	// ---- UPDATE 6 [SCENE-OBJECTS] (FVSceneU6Objects.cpp): objects of big worlds + the ocean boat
	TSharedPtr<FFVSceneU6State> U6Objects;
	/** Called by RebuildWorld (FVSceneHome.cpp) for big worlds and the ocean pier. */
	void RebuildWorldU6(FVScene& S);
};

// =============================================================================================
// UPDATE 4b: the practice pond of the home yard, for the fishing module and the onboarding (FVFishing.cpp,
// FVOnboarding.cpp). Implemented in FVSceneHome.cpp; refreshed whenever the scene (re)builds its layout.
// =============================================================================================
namespace FVPracticePond
{
	struct FInfo
	{
		bool bValid = false;                          // the current scene has a practice pond (the home yard)
		FVector2D Center = FVector2D::ZeroVector;     // water oval (canvas px)
		FVector2D Radii = FVector2D::ZeroVector;
		FFVRect Plank;                                // the wooden jetty
		FVector2D Spot = FVector2D::ZeroVector;       // feet at the end of the plank: the best place to fish from
		FFVRect Bounds;                               // water + banks + plank (for coach marks / avoidance)
	};
	/** The practice pond of Scene (bValid = false when its current location has none). */
	FInfo Get(const FVScene& Scene);
}
