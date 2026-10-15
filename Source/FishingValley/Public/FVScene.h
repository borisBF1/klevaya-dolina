// The walkable fishing scene of the current location: static background, animated water & ambience,
// player character + pet, effects, day/night lighting and weather. Implemented in FVScene.cpp (+ FVSceneArt.cpp).
//
// Layout convention (virtual px): land on top, horizontal shoreline at GetShoreY(), water below it down to the
// bottom of the screen. Depth 0 at the shoreline, 1 at the bottom edge (FV::VH - FV::BottomBarH).
// Every location has a wooden dock/pier that extends down into the water (standing on it reaches deeper water).
// The player casts straight DOWN from the rod tip.

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"
#include "FVSprites.h"

class UFVGame;
class FVDraw;

class FVScene
{
public:
	explicit FVScene(UFVGame* InGame);
	~FVScene();

	/** Builds (or fetches from cache) the location's background and layout; puts the player at the spawn point. */
	void LoadLocation(FName LocationId);
	FName GetLocationId() const { return LocationId; }

	/** Movement (if bAllowMove), animation timers, pet AI, ambient critters & particles. */
	void Tick(float Dt, const FFVInput& In, bool bAllowMove);

	/** Background, water animation, ambient decor, pet & player (y-sorted), effects. */
	void DrawScene(FVDraw& D);
	/** Day/night tint, light glows, rain/fog overlays. Drawn after the fishing line/bobber. */
	void DrawLighting(FVDraw& D);
	/** Floating texts ("+12 XP"), drawn above lighting. */
	void DrawOverlay(FVDraw& D);

	// ------------------------------------------------------------------ player
	FVector2D GetPlayerPos() const { return PlayerPos; }      // feet, virtual px
	EFVFacing GetFacing() const { return Facing; }
	void SetFacing(EFVFacing F) { Facing = F; }
	/** Sets the character animation (fishing module drives CastWindup..HoldFish). bRestart resets the frame timer. */
	void SetPlayerAnim(EFVCharAnim Anim, bool bRestart = false);
	EFVCharAnim GetPlayerAnim() const { return Anim; }
	int32 GetPlayerFrame() const;
	bool IsPlayerMoving() const { return bMoving; }
	/** Rod tip in virtual px for the current animation frame (from UFVSprites::RodTipOffset). */
	FVector2D GetRodTip() const;
	/** Top of the player's head in virtual px (for "!" and the held fish). */
	FVector2D GetHeadTop() const;
	/** Sprite shown above the head during HoldFish (nullptr = none). */
	void SetHeldSprite(const FFVSprite* Sprite) { HeldSprite = Sprite ? *Sprite : FFVSprite(); }
	/** Called when the appearance/pet changes (equipment). Cheap; re-reads from the profile. */
	void RefreshAppearance();

	// ------------------------------------------------------------------ water queries
	float GetShoreY() const { return ShoreY; }
	/** Update 6: non-inline (big worlds with water return their own bottom). Classic: FV::VH - FV::BottomBarH. */
	float GetWaterBottomY() const;
	bool IsWater(const FVector2D& P) const;
	/** 0 at the shoreline .. 1 at the bottom of the screen. Negative on land. */
	float DepthAt(const FVector2D& P) const;
	/** True if the player stands close enough to water to cast (shore edge or anywhere on the dock). */
	bool CanFishFromHere() const;
	/**
	 * Landing point of a cast straight down from the rod tip. Power01 0..1, RodRange = FFVRodDef::CastRange.
	 * Minimum distance lands just past the shore/dock edge; max distance = RodRange * full water depth.
	 */
	FVector2D GetCastTarget(float Power01, float RodRange) const;

	// ------------------------------------------------------------------ effects
	void SpawnSplash(const FVector2D& P, float Size = 1.f);
	void SpawnRipple(const FVector2D& P, float Size = 1.f);
	void SpawnFloatingText(const FVector2D& P, const FString& Text, const FLinearColor& C);
	void SpawnSparkles(const FVector2D& P, const FLinearColor& C, int32 Count = 12);

	// ============================================================================================
	// UPDATE 2 (World module; FVSceneHome.cpp). See Docs/UPDATE_HOME.md §World.
	// ============================================================================================
	/** LoadLocation also accepts FVDB::HomeId() (yard: house exterior + garden, no water) and FVDB::HouseId()
	    (interior with the stations). Both have no water: CanFishFromHere()/IsWater() are false there. */
	bool IsHome() const;                     // yard
	bool IsInterior() const;                 // house interior
	bool HasWater() const { return !IsHome() && !IsInterior(); }

	/** Interactive objects of the current scene (resources lying today, stalls, board, net spots, dig spots, door,
	    stations, decor catalog). Rebuilt on load, canvas change and RefreshWorldObjects(). Positions = current canvas. */
	const TArray<FFVWorldObject>& GetWorldObjects() const;
	/** Re-reads the profile (resources taken, stations built, nets placed, decor...) and rebuilds objects/blockers. */
	void RefreshWorldObjects();
	/** Topmost object whose Hit rect contains P (virtual px), nullptr if none. */
	const FFVWorldObject* ObjectAt(const FVector2D& P) const;
	/** Nearest object within its Reach of the player's feet, nullptr if none. */
	const FFVWorldObject* NearestInReach() const;
	bool IsInReach(const FFVWorldObject& Obj) const;
	/** Finds an object by kind + index (+ id for stations/stalls). */
	const FFVWorldObject* FindObject(EFVObjKind Kind, int32 Index, FName Id = NAME_None) const;
	/** Highlight (outline + name plate) drawn for this object in DrawOverlay; nullptr = none. Set every frame. */
	void SetHighlight(const FFVWorldObject* Obj);

	/** Auto-walk: the player walks towards Feet (with collision sliding) until within Reach or stuck (~1.2 s without
	    progress) or any manual movement input. Checked by UFVGame each frame. */
	void SetWalkTarget(const FVector2D& Feet, float Reach);
	void ClearWalkTarget();
	bool HasWalkTarget() const;
	bool WalkTargetReached() const;          // true for the frame the target was reached
	bool WalkTargetFailed() const;           // true for the frame it gave up (stuck / cancelled)

	/** Short "gather" animation + sparkles at P (pickup feedback). */
	void PlayPickup(const FVector2D& P, const FFVSprite& Icon);
	/** Debug / autotest: puts the player's feet at P (snapped to the nearest walkable point). */
	void DebugTeleport(const FVector2D& P);
	/** Debug / autotest (0.6): the feet may stand at P (walk rects, water, blockers of the current scene). */
	bool DebugIsWalkable(const FVector2D& P) const;
	/** Debug / autotest (Р69): the walk exit of the current place into Target — its trigger rect, the feet at the edge and
	    the feet of its way sign (zero = no sign); world px. False when the place has no such exit. */
	bool DebugFindExit(FName Target, FFVRect& OutHit, FVector2D& OutFeet, FVector2D& OutSignFeet) const;

	// ============================================================================================
	// UPDATE 6 — camera & big locations (Docs/UPDATE6_FOREST_CITY.md §2, Р38). [SCENE-CAMERA] FVSceneCamera.cpp.
	// Every position of this API is in WORLD px. On the classic one-screen locations the camera is (0,0), so world ==
	// screen and nothing changes. On big worlds (forest, yard, house, City, Cave) the camera follows the player, clamps
	// to the world, centres a world smaller than the screen, and everything outside the world is BLACK.
	// Hotfix 0.61: a classic location may be wider than the screen (the pond, FFVLocationDef::SideWiden): the camera
	// then follows the player along X only, inside the layout's scroll range (its content + a pad on the right so the
	// HUD button column never covers the right end); Y stays 0 and the painted bleed shows beyond — never black.
	// Screen -> world: ScreenToWorld (mouse, taps); world -> screen: WorldToScreen (UI anchored to world objects).
	// ============================================================================================
	bool IsBigWorld() const;
	/** Hotfix 0.61: the camera follows the player — a big world, or a classic location wider than the screen. */
	bool HasCamera() const;
	/** World px shown at the screen's top-left (0,0 on classic one-screen locations). */
	FVector2D GetCamera() const;
	FVector2D WorldToScreen(const FVector2D& W) const { return W - GetCamera(); }
	FVector2D ScreenToWorld(const FVector2D& S) const { return S + GetCamera(); }
	/** The world rect in world px (classic one-screen: the canvas; a scrolling classic place: its scroll range x the
	    canvas height — X may be negative). */
	FFVRect GetWorldBounds() const;
	/** UFVGame::Draw wraps DrawScene .. DrawOverlay (and Fishing::DrawWorld) in these: they push / pop the camera
	    translate of FVDraw (drawing AND hit tests move together). No-ops on classic one-screen locations. */
	void BeginWorldView(FVDraw& D);
	void EndWorldView(FVDraw& D);
	/** The camera jumps to the player (after a load / teleport) instead of easing. */
	void SnapCamera();
	/** LoadLocation + put the player at the named entry of the big world (FVWorldU6 Spawn anchor Id, e.g. "from_river",
	    "door", "pier"); NAME_None or unknown = the default spawn. */
	void LoadLocationAt(FName LocationId, FName Entry);
	/** The player walked into an exit of a big world this frame: target location + its entry. UFVGame travels. */
	bool GetPendingExit(FName& OutLocation, FName& OutEntry) const;
	void ClearPendingExit();
	/** Cave: radius (world px) of the light circle around the player; 0 = no darkness. */
	float GetDarkRadius() const;

private:
	UFVGame* Game = nullptr;
	FName LocationId;
	float ShoreY = 190.f;

	FVector2D PlayerPos = FVector2D(320, 170);
	EFVFacing Facing = EFVFacing::Down;
	EFVCharAnim Anim = EFVCharAnim::Idle;
	float AnimTime = 0.f;
	bool bMoving = false;
	FFVSprite HeldSprite;
	FFVAppearance Look;

	// Implementation state (layout, cached backgrounds, particles, pet, ambient...) is private to FVScene.cpp.
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
