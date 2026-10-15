// The walkable fishing scene: background, animated water & ambience, player + pet, effects, lighting & weather.

#include "FVScene.h"
#include "FVSceneArt.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVAudio.h"
#include "Engine/Texture2D.h"

using namespace FVSceneArt;
using namespace FVSceneRt;

namespace FVScenePondImpl
{
	/** Horizontal offset of the rod tip from the feet when casting facing down (the cast lands straight below it). */
	float CastTipDX()
	{
		return float((UFVSprites::RodTipOffset(EFVCharAnim::CastRelease, EFVFacing::Down, 0) - UFVSprites::CharacterAnchor()).X) * WS;
	}
}

// =============================================================================================
// FImpl (declared in FVSceneArt.h; the UPDATE 2 half lives in FVSceneHome.cpp)
// =============================================================================================
void FVScene::FImpl::EnsureDecor()
{
	if (bDecorReady) { return; }
	UFVSprites* S = UFVSprites::Get();
	if (!S) { return; }
	for (int32 i = 0; i < int32(EDecor::Count); ++i)
	{
		const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::Scene, 0xFFF000u + uint32(i), 0, 2);
		if (const FFVSprite* Found = S->FindCached(Key)) { Decor[i] = *Found; }
		else { Decor[i] = S->AddCached(Key, BuildDecor(EDecor(i)), FString::Printf(TEXT("SceneDecor_%d"), i)); }
		if (EDecor(i) == EDecor::Fog && Decor[i].Tex && Decor[i].Tex->Filter != TF_Bilinear)
		{
			// Fog is stretched ~6x: bilinear filtering keeps its soft edges from turning into hard blocks.
			Decor[i].Tex->Filter = TF_Bilinear;
			Decor[i].Tex->UpdateResource();
		}
	}
	bDecorReady = true;
}

/** Active layout = the cached texture-space layout moved to the current canvas (portrait: shore at VH / 2). */
void FVScene::FImpl::ApplyLayout(const FLocCache& Cached)
{
	Bg = Cached.Bg;
	L = Cached.Layout;
	TranslateLayout(L, CanvasOrigin(Cached.Layout));
	WorldR = FFVRect(float(L.Origin.X), float(L.Origin.Y), float(L.TexW) * WS, float(L.TexH) * WS);
	LayoutCanvas = FVector2D(FV::VW, FV::VH);
	BaseBlockers = L.Blockers.Num();   // world objects append theirs (RebuildWorld)
}

/** Water surface rect in canvas coords (bleed included). */
FFVRect FVScene::FImpl::WaterRect() const
{
	if (bBig && L.WaterArea.W > 0.f)
	{
		return FFVRect(L.WaterArea.X, L.ShoreY, L.WaterArea.W, FMath::Max(1.f, L.WaterArea.Bottom() - L.ShoreY));
	}
	return FFVRect(WorldR.X, L.ShoreY, WorldR.W, FMath::Max(1.f, WorldR.Bottom() - L.ShoreY));
}

/** Spawn-rate multiplier for effects spread over the whole (bleed included) water instead of the visible part. */
float FVScene::FImpl::BleedMul() const
{
	const float VisArea = FV::VW * FMath::Max(1.f, FV::VH - L.ShoreY);
	const FFVRect W = WaterRect();
	return FMath::Clamp(W.W * W.H / FMath::Max(1.f, VisArea), 1.f, 3.f);
}

uint64 FVScene::FImpl::CacheKey(FName Id, const FIntPoint& Size)
{
	return (uint64(GetTypeHash(Id)) << 32) | (uint64(uint32(Size.X) & 0xFFFFu) << 16) | uint64(uint32(Size.Y) & 0xFFFFu);
}

/** Builds (or returns the cached) background + layout for a background size in texels. The texture is
    (re)created once UFVSprites exists. */
const FVScene::FImpl::FLocCache& FVScene::FImpl::EnsureLocation(const FFVLocationDef& InLoc, const FIntPoint& Size)
{
	const uint64 CKey = CacheKey(InLoc.Id, Size);
	FLocCache* Cached = Cache.Find(CKey);
	UFVSprites* S = UFVSprites::Get();
	if (Cached && (Cached->Bg.IsValid() || !S)) { return *Cached; }
	FFVImage Img;
	FLayout Lay;
	BuildLocation(InLoc, Size.X, Size.Y, Img, Lay);
	FFVSprite Sprite;
	if (S)
	{
		const uint32 B = ((uint32(InLoc.Theme) + 1u) & 0xFu) | ((uint32(Size.X) & 0x3FFu) << 4) | ((uint32(Size.Y) & 0x3FFu) << 14);
		const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::Scene, GetTypeHash(InLoc.Id) & 0xFFFFFFu, B, 1);
		if (const FFVSprite* Found = S->FindCached(Key)) { Sprite = *Found; }
		else { Sprite = S->AddCached(Key, Img, FString::Printf(TEXT("SceneBg_%s_%dx%d"), *InLoc.Id.ToString(), Size.X, Size.Y)); }
	}
	FLocCache NewEntry;
	NewEntry.Bg = Sprite;
	NewEntry.Layout = MoveTemp(Lay);
	UE_LOG(LogFV, Log, TEXT("FV: built scene for '%s' (theme %d, %dx%d texels, texture %s)"), *InLoc.Id.ToString(), int32(InLoc.Theme), Size.X, Size.Y, Sprite.IsValid() ? TEXT("ok") : TEXT("pending"));
	return Cache.Add(CKey, MoveTemp(NewEntry));
}

/** Ambient critters & particles for the current layout (on load and after a canvas change). */
void FVScene::FImpl::InitAmbient(float ShoreY)
{
	const bool bTall = L.bPortrait || L.bBigWorld;   // update 6: big worlds spread the ambience over the world
	Glints.Reset(); Particles.Reset(); Ripples.Reset(); Birds.Reset();
	Butterflies.Reset(); Fireflies.Reset(); Jumps.Reset(); Bubbles.Reset(); Fog.Reset();
	NextJump = Rng.FRandRange(3.f, 7.f);
	NextBird = Rng.FRandRange(2.f, 6.f);
	if (L.bNoWater)
	{
		// Home yard: fireflies drift over the whole garden.
		FlyTop = FMath::Max(FV::TopBarH + 16.f, L.WalkTop - 70.f);
		FlyBottom = FMath::Min(FV::VH - FV::BottomBarH - 16.f, L.WalkBottom);
		FlyX0 = bTall ? WorldR.X + 10.f : 10.f;
		FlyX1 = bTall ? WorldR.Right() - 10.f : 630.f;
	}
	else
	{
		FlyTop = bTall ? FMath::Max(FV::TopBarH + 16.f, ShoreY - 220.f) : ShoreY - 80.f;
		FlyBottom = bTall ? FMath::Min(FV::VH - FV::BottomBarH - 30.f, ShoreY + 170.f) : ShoreY + 50.f;
		FlyX0 = bTall ? WorldR.X + 10.f : 10.f;
		FlyX1 = bTall ? WorldR.Right() - 10.f : 630.f;
	}
	// hotfix 0.61: a scrolling classic layout (the landscape pond): the fireflies spread over the whole scroll range
	// (portrait already covers the wide background)
	const bool bScrollFlies = !bTall && !L.bNoWater && HasCamera();
	if (bScrollFlies)
	{
		FlyX0 = float(L.ScrollRect.X) + 10.f;
		FlyX1 = float(L.ScrollRect.Right()) - 10.f;
	}
	if (L.bBigWorld)
	{
		// update 6: fireflies over the walkable part of the world (world px)
		FlyTop = FMath::Max(0.f, L.WalkTop - 70.f);
		FlyBottom = FMath::Max(FlyTop + 20.f, L.WalkBottom);
		FlyX0 = L.WalkMinX + 4.f;
		FlyX1 = FMath::Max(FlyX0 + 20.f, L.WalkMaxX - 4.f);
	}

	if (L.bButterflies && L.FlowerSpots.Num() > 0)
	{
		const FColor Cols[5] = { FColor(255, 255, 255), FColor(255, 222, 90), FColor(255, 160, 70), FColor(130, 190, 255), FColor(250, 150, 210) };
		for (int32 i = 0; i < (L.bBigWorld ? 8 : (bTall ? 6 : 4)); ++i)
		{
			FButterfly B;
			B.Home = L.FlowerSpots[Rng.RandHelper(L.FlowerSpots.Num())];
			B.Target = B.Home;
			B.P = B.Home;
			B.Phase = Rng.FRandRange(0.f, 6.f);
			B.Speed = Rng.FRandRange(0.8f, 1.3f);
			B.Retarget = Rng.FRandRange(4.f, 10.f);
			B.C = FV::Col(Cols[Rng.RandHelper(5)]);
			Butterflies.Add(B);
		}
	}
	if (L.bFireflies)
	{
		const int32 NFlies = L.bBigWorld ? FMath::Clamp(FMath::RoundToInt(16.f * (FlyX1 - FlyX0) * (FlyBottom - FlyTop) / (640.f * 200.f)), 8, 90)
			: bTall ? FMath::RoundToInt(26.f * (FlyX1 - FlyX0) / FMath::Max(1.f, FV::VW - 20.f))
			: bScrollFlies ? FMath::RoundToInt(16.f * float(L.ScrollRect.W) / FMath::Max(1.f, FV::VW)) : 16;
		for (int32 i = 0; i < NFlies; ++i)
		{
			FFirefly F;
			F.P = (bTall || L.bNoWater) ? FVector2D(Rng.FRandRange(FlyX0, FlyX1), Rng.FRandRange(FlyTop + 10.f, FMath::Max(FlyTop + 12.f, FlyBottom - 10.f)))
				: bScrollFlies ? FVector2D(Rng.FRandRange(FlyX0, FlyX1), Rng.FRandRange(ShoreY - 70.f, ShoreY + 40.f))
				: FVector2D(Rng.FRandRange(10.f, 630.f), Rng.FRandRange(ShoreY - 70.f, ShoreY + 40.f));
			F.Heading = Rng.FRandRange(0.f, UE_TWO_PI);
			F.Phase = Rng.FRandRange(0.f, 6.f);
			F.Speed = Rng.FRandRange(6.f, 14.f);
			Fireflies.Add(F);
		}
	}
	if (L.bFog)
	{
		// Portrait: bands cover the whole background (bleed included) so letterbox areas are foggy too.
		const float Y0 = bTall ? WorldR.Y + 16.f : ShoreY - 70.f;
		const float Y1 = bTall ? WorldR.Bottom() - 60.f : FV::VH - 50.f;
		int32 N = bTall ? FMath::Max(10, FMath::RoundToInt((Y1 - Y0) / 70.f)) : 6;
		if (L.bBigWorld) { N = FMath::Clamp(N * FMath::Max(1, FMath::CeilToInt(WorldR.W / 700.f)), 6, 60); }   // update 6
		else if (!bTall && HasCamera()) { N = FMath::Max(N, FMath::RoundToInt(float(N) * float(L.ScrollRect.W) / FMath::Max(1.f, FV::VW))); }   // hotfix 0.61
		for (int32 i = 0; i < N; ++i)
		{
			FFogBand F;
			F.Scale = Rng.FRandRange(2.2f, 3.4f);
			F.X = Rng.FRandRange(WorldR.X - 200.f, WorldR.Right());
			F.Y = FMath::Lerp(Y0, Y1, float(i) / float(N - 1)) + Rng.FRandRange(-10.f, 10.f);
			F.Speed = Rng.FRandRange(4.f, 11.f) * (i % 2 ? 1.f : -1.f);
			F.Alpha = Rng.FRandRange(0.18f, 0.32f);
			Fog.Add(F);
		}
	}
}

/** Maps a feet position from an old layout (other canvas) to the current one: dock -> same spot of the new
    dock; land -> same distance from the shore, X scaled to the new width (hotfix 0.61: a layout wider than the screen
    — the same fraction of the walk range). Home scenes: same fraction of the walkable area. */
FVector2D FVScene::FImpl::MapPoint(const FVector2D& P, const FLayout& Old, float OldVW) const
{
	if (L.bPond && Old.bPond)
	{
		// update 4b: around the practice pond (fishing from its plank / bank) the player keeps his spot at the pond
		if (OnPondPlank(Old, P))
		{
			const float Fy = FMath::Clamp(float(P.Y - Old.PondPlank.Y) / FMath::Max(1.f, Old.PondPlank.H), 0.f, 1.f);
			return FVector2D(L.PondPlank.Center().X + (P.X - Old.PondPlank.Center().X), L.PondPlank.Y + Fy * L.PondPlank.H);
		}
		const FVector2D N(float(P.X - Old.PondCenter.X) / FMath::Max(1.f, float(Old.PondRadii.X)), float(P.Y - Old.PondCenter.Y) / FMath::Max(1.f, float(Old.PondRadii.Y)));
		if (FMath::Abs(N.X) <= 1.5f && N.Y >= -2.2f && N.Y <= 1.8f)
		{
			return L.PondCenter + FVector2D(N.X * L.PondRadii.X, N.Y * L.PondRadii.Y);
		}
	}
	if (L.bNoWater || Old.bNoWater)
	{
		const float OldTop = Old.WalkTop, OldBot = Old.bNoWater ? Old.WalkBottom : Old.ShoreY - 2.f;
		const float NewTop = L.WalkTop, NewBot = L.bNoWater ? L.WalkBottom : L.ShoreY - 2.f;
		const float Fx = FMath::Clamp(float(P.X - Old.WalkMinX) / FMath::Max(1.f, Old.WalkMaxX - Old.WalkMinX), 0.f, 1.f);
		const float Fy = FMath::Clamp(float(P.Y - OldTop) / FMath::Max(1.f, OldBot - OldTop), 0.f, 1.f);
		return FVector2D(FMath::Lerp(L.WalkMinX + 4.f, L.WalkMaxX - 4.f, Fx), FMath::Lerp(NewTop, NewBot, Fy));
	}
	if (Old.Dock.W > 0.f && L.Dock.W > 0.f && P.Y > Old.ShoreY - 2.f && P.X >= Old.Dock.X && P.X <= Old.Dock.Right())
	{
		const float Fy = FMath::Clamp(float(P.Y - Old.Dock.Y) / FMath::Max(1.f, Old.Dock.H), 0.f, 1.f);
		return FVector2D(L.Dock.Center().X + (P.X - Old.Dock.Center().X), L.Dock.Y + Fy * L.Dock.H);
	}
	const float NewVW = FV::VW;
	float X = FMath::Clamp(float(P.X) * NewVW / FMath::Max(1.f, OldVW), L.WalkMinX + 4.f, L.WalkMaxX - 4.f);
	if (L.ScrollRect.W > 0.f || Old.ScrollRect.W > 0.f)
	{
		// hotfix 0.61: a layout wider than the screen (the pond) — the same fraction of the walk range (the side strips
		// sit at negative X / beyond VW, scaling by VW would squeeze the player towards the left)
		const float Fx = FMath::Clamp(float(P.X - Old.WalkMinX) / FMath::Max(1.f, Old.WalkMaxX - Old.WalkMinX), 0.f, 1.f);
		X = FMath::Lerp(L.WalkMinX + 4.f, FMath::Max(L.WalkMinX + 4.f, L.WalkMaxX - 4.f), Fx);
	}
	const float Y = FMath::Clamp(L.ShoreY - FMath::Max(2.f, float(Old.ShoreY - P.Y)), L.WalkTop, L.ShoreY - 2.f);
	return FVector2D(X, Y);
}

/** Nearest walkable point (rings around P), falling back to the spawn point. */
FVector2D FVScene::FImpl::FindWalkable(const FVector2D& P) const
{
	if (IsWalkable(L, P)) { return P; }
	for (float Rad = 6.f; Rad <= 160.f; Rad += 6.f)
	{
		for (int32 k = 0; k < 16; ++k)
		{
			const float A = float(k) / 16.f * UE_TWO_PI;
			const FVector2D Q = P + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rad;
			if (IsWalkable(L, Q)) { return Q; }
		}
	}
	return L.Spawn;
}

/** Re-places the scene when the virtual canvas changed (orientation / window aspect / portrait height). The first
    ticks run before the first draw, so a scene loaded on the default landscape canvas switches here. */
void FVScene::FImpl::SyncCanvas(FVScene& S)
{
	if (CanvasRev == FV::CanvasRevision || !Loc) { return; }
	CanvasRev = FV::CanvasRevision;
	if (bBig)
	{
		// update 6: a big world is one composition for every canvas; only the camera changes
		LayoutCanvas = FVector2D(FV::VW, FV::VH);
		bCamSnap = true;
		UpdateCamera(S, 0.f);
		return;
	}
	const FIntPoint NewSize = CanvasBgSize(*Loc);   // hotfix 0.61: per location (the pond is wider)
	const FVector2D NewCanvas(FV::VW, FV::VH);
	if (NewSize == BgSize && L.TexW == NewSize.X && L.TexH == NewSize.Y && NewCanvas == LayoutCanvas) { return; }
	const FLayout Old = L;
	const float OldVW = float(LayoutCanvas.X);
	const bool bSameArt = NewSize == BgSize && L.TexW == NewSize.X && L.TexH == NewSize.Y && Old.bPortrait == FV::IsPortrait();

	// The auto-walk target follows its object (kind + index + id) to the object's new spot.
	int32 WalkObj = INDEX_NONE;
	FFVWorldObject WalkKey;
	if (bWalk)
	{
		for (int32 k = 0; k < Objects.Num(); ++k)
		{
			if (FVector2D::Distance(Objects[k].Feet, WalkTarget) < 1.f) { WalkObj = k; WalkKey = Objects[k]; break; }
		}
	}

	const FLocCache& Cached = EnsureLocation(*Loc, NewSize);
	ApplyLayout(Cached);
	BgSize = NewSize;
	S.ShoreY = L.ShoreY;
	RebuildWorld(S);
	if (bSameArt)
	{
		// Same picture, only moved (portrait height changed a little): keep everybody on the same spot of the world.
		const FVector2D Delta = L.Origin - Old.Origin;
		S.PlayerPos = FindWalkable(S.PlayerPos + Delta);
		PetPos += Delta;
		for (FVector2D& P : Trail) { P += Delta; }
	}
	else
	{
		FVector2D Want;
		if (bAtSpawn) { Want = (bSpawnAtDoor && L.bDoor) ? L.DoorFeet + FVector2D(0.f, 8.f) : L.Spawn; }
		else { Want = MapPoint(S.PlayerPos, Old, OldVW); }
		S.PlayerPos = FindWalkable(Want);
		if (PetStyle != EFVPetStyle::None)
		{
			FVector2D P = MapPoint(PetPos, Old, OldVW);
			if (!IsWalkable(L, P) || FVector2D::Distance(P, S.PlayerPos) > 80.f) { P = S.PlayerPos + FVector2D(-26.f, -2.f); }
			if (!IsWalkable(L, P)) { P = S.PlayerPos + FVector2D(26.f, -2.f); }
			if (!IsWalkable(L, P)) { P = S.PlayerPos; }
			PetPos = P;
		}
		Trail.Reset();
	}
	if (bWalk)
	{
		const FFVWorldObject* NewObj = WalkObj != INDEX_NONE ? S.FindObject(WalkKey.Kind, WalkKey.Index, WalkKey.Id) : nullptr;
		if (NewObj) { WalkTarget = NewObj->Feet; WalkBest = 1.e9f; WalkStuck = 0.f; }
		else { bWalk = false; bWalkFailed = true; }
	}
	InitAmbient(S.ShoreY);
	// hotfix 0.61: a scrolling classic layout (the pond) — the camera jumps to the player on the new canvas instead of
	// easing across the whole view (one-screen layouts: stays at 0,0)
	bCamSnap = true;
	UpdateCamera(S, 0.f);
	UE_LOG(LogFV, Log, TEXT("FV: scene '%s' placed on the %.0fx%.0f canvas (shore %.1f, background %dx%d texels at %.1f,%.1f)"),
		*S.LocationId.ToString(), FV::VW, FV::VH, S.ShoreY, L.TexW, L.TexH, L.Origin.X, L.Origin.Y);
}

bool FVScene::FImpl::OnWaterSurface(const FVector2D& P) const
{
	if (L.bNoWater) { return L.bPond && InPondWater(L, P, -3.f); }
	if (bBig && !(P.X >= L.WaterArea.X && P.X < L.WaterArea.Right() && P.Y < L.WaterArea.Bottom())) { return false; }   // update 6
	return P.Y > L.ShoreY + 2.f && P.Y < WorldR.Bottom() && !(L.Dock.W > 0.f && P.X > L.Dock.X - 4.f && P.X < L.Dock.Right() + 4.f && P.Y < L.Dock.Bottom() + 6.f);
}

/** Random point of the water surface. bWide: anywhere on the background (bleed included), else on screen. */
FVector2D FVScene::FImpl::RandomWaterPoint(float MinDepthPx, float MaxY, bool bWide) const
{
	// hotfix 0.61: "on screen" of a scrolling classic layout (the pond) = the camera view
	const float CamX = (!bBig && HasCamera()) ? float(Cam.X) : 0.f;
	float X0 = bWide ? WorldR.X + 4.f : CamX + 8.f, X1 = bWide ? WorldR.Right() - 4.f : CamX + FV::VW - 8.f;
	if (bBig)
	{
		// update 6: the water of a big world (world px)
		X0 = L.WaterArea.X + 4.f;
		X1 = FMath::Max(X0 + 1.f, L.WaterArea.Right() - 4.f);
		MaxY = FMath::Max(L.ShoreY + MinDepthPx + 1.f, FMath::Min(MaxY, L.WaterArea.Bottom() - 4.f));
		if (bWide) { MaxY = FMath::Max(L.ShoreY + MinDepthPx + 1.f, L.WaterArea.Bottom() - 4.f); }
	}
	for (int32 Try = 0; Try < 12; ++Try)
	{
		const FVector2D P(Rng.FRandRange(X0, X1), Rng.FRandRange(L.ShoreY + MinDepthPx, MaxY));
		if (OnWaterSurface(P)) { return P; }
	}
	return FVector2D(bBig ? float(L.WaterArea.Center().X) : CamX + 40.f, FMath::Min(MaxY, L.ShoreY + MinDepthPx + 10.f));
}

/** Update 4b: random point of the practice pond's open water. */
FVector2D FVScene::FImpl::RandomPondPoint() const
{
	for (int32 Try = 0; Try < 16; ++Try)
	{
		const float A = Rng.FRandRange(0.f, UE_TWO_PI), R = FMath::Sqrt(Rng.FRand()) * 0.92f;
		const FVector2D P(L.PondCenter.X + FMath::Cos(A) * L.PondRadii.X * R, L.PondCenter.Y + FMath::Sin(A) * L.PondRadii.Y * R);
		if (InPondWater(L, P, -3.f)) { return P; }
	}
	return L.PondCenter + FVector2D(L.PondRadii.X * 0.3f, L.PondRadii.Y * 0.35f);
}

/** Update 4b: the practice pond is fished from its plank, or from the north bank where the cast (straight down from
    the rod tip, which is to the right of the feet) lands in the water. */
bool FVScene::FImpl::AtPondSpot(const FVector2D& Feet) const
{
	if (!L.bPond) { return false; }
	if (OnPondPlank(L, Feet)) { return true; }
	const float CX = float(L.PondCenter.X), RX = float(L.PondRadii.X);
	const float TipX = float(Feet.X) + FVScenePondImpl::CastTipDX();
	if (FMath::Abs(float(Feet.X) - CX) > RX * 0.9f || FMath::Abs(TipX - CX) > RX * 0.82f) { return false; }
	const float Top = float(L.PondCenter.Y) - PondHalfH(L, float(Feet.X));
	return Feet.Y <= Top + 1.f && Feet.Y >= Top - 14.f;
}

// =============================================================================================
// Lifecycle
// =============================================================================================
FVScene::FVScene(UFVGame* InGame)
	: Game(InGame)
	, Impl(MakeUnique<FImpl>())
{
}

FVScene::~FVScene() = default;

void FVScene::LoadLocation(FName InLocationId)
{
	FImpl& I = *Impl;
	const FFVLocationDef* Loc = FVDB::FindLocation(InLocationId);
	if (!Loc && FVDBU6::IsForestId(InLocationId)) { Loc = &FVDBU6::ForestLocation(); }   // update 6: not in Locations()
	if (!Loc && FVDB::Locations().Num() > 0) { Loc = &FVDB::Locations()[0]; }
	if (!Loc) { Loc = &FallbackLocation(); }
	const FName PrevId = LocationId;
	I.Loc = Loc;
	LocationId = Loc->Id;

	I.CanvasRev = FV::CanvasRevision;
	I.BgSize = CanvasBgSize(*Loc);   // hotfix 0.61: per location (the pond is wider)
	// update 6: big worlds (forest, City, Cave, yard, house; -fvsmallyard = old yard / house) — one composition, the camera follows
	if (!(FVWorldU6::IsBigWorldId(Loc->Id) && I.ApplyBigWorld(*Loc)))
	{
		I.bBig = false;
		I.BigDef.Reset();
		I.BigId = NAME_None;
		I.BgChunks.Reset();
		I.BgChunkRects.Reset();
		const FImpl::FLocCache& Cached = I.EnsureLocation(*Loc, I.BgSize);
		I.ApplyLayout(Cached);
	}
	I.bExitPending = false;
	I.ExitLocation = NAME_None;
	I.ExitEntry = NAME_None;
	I.bExitArmed = false;
	I.EnsureDecor();
	I.bAtSpawn = true;
	I.bWalk = false; I.bWalkReached = false; I.bWalkFailed = false;
	I.bHighlight = false;
	I.PickupAge = -1.f;

	ShoreY = I.L.ShoreY;
	I.RebuildWorld(*this);
	// Walking through the house door: appear at the door on the other side (the yard from the map: at the spawn).
	const bool bOutOfHouse = Loc->Theme == EFVTheme::Home && PrevId == FVDB::HouseId();
	const bool bIntoHouse = Loc->Theme == EFVTheme::HomeInterior;
	I.bSpawnAtDoor = (bOutOfHouse || bIntoHouse) && I.L.bDoor;
	PlayerPos = I.bSpawnAtDoor ? I.L.DoorFeet + FVector2D(0.f, 8.f) : I.L.Spawn;
	if (I.bBig)
	{
		// update 6: the entry of LoadLocationAt; through the house door: the "door" entry
		FName Entry = I.PendingEntry;
		if (Entry.IsNone() && (bOutOfHouse || bIntoHouse)) { Entry = FName(TEXT("door")); }
		PlayerPos = I.BigEntryFeet(Entry);
		I.bSpawnAtDoor = false;
	}
	// Р69: walked in from a neighbouring place (LoadLocationAt "from_<place>"): appear beside the exit back there
	bool bFromExit = false;
	EFVFacing EntryFacing = EFVFacing::Down;
	if (!I.bBig && !I.PendingEntry.IsNone())
	{
		const FString E = I.PendingEntry.ToString();
		if (E.StartsWith(TEXT("from_")))
		{
			const FName Src(*E.Mid(5));
			for (const FVWorldU6::FAnchor& A : I.WalkExits)
			{
				if (A.Target != Src) { continue; }
				const FVWorldU6::EWayDir D = FVWorldU6::EWayDir(A.SignDir);
				const FVector2D Inward = D == FVWorldU6::EWayDir::Left ? FVector2D(1.f, 0.f) : (D == FVWorldU6::EWayDir::Right ? FVector2D(-1.f, 0.f) : FVector2D(0.f, 1.f));
				// beside the exit, clear of its way sign (64 px wide, drawn behind the player): the body never hides its picture
				float Off = 62.f;
				const float SDx = FMath::Abs(float(A.SignFeet.X - A.Feet.X)), SDy = float(A.SignFeet.Y - A.Feet.Y) - float(Inward.Y) * Off;
				if (D != FVWorldU6::EWayDir::Up && !A.SignFeet.IsZero() && SDy > -32.f && SDy < 64.f && FMath::Abs(Off - SDx) < 46.f)
				{
					Off = SDx - 46.f >= 24.f ? SDx - 46.f : SDx + 46.f;
				}
				// the way straight back into the exit must be free (a beach umbrella / a rock on the row: come closer)
				float Run = 0.f;   // how far the row from the exit is free
				while (Run < Off && IsWalkable(I.L, A.Feet + Inward * (Run + 1.f))) { Run += 1.f; }
				if (Run < Off && Run >= 24.f) { Off = Run - 4.f; }
				PlayerPos = A.Feet + Inward * Off;
				EntryFacing = D == FVWorldU6::EWayDir::Left ? EFVFacing::Right : (D == FVWorldU6::EWayDir::Right ? EFVFacing::Left : EFVFacing::Down);
				bFromExit = true;
				break;
			}
		}
	}
	if (!IsWalkable(I.L, PlayerPos))
	{
		PlayerPos = I.FindWalkable(PlayerPos);
		if (!IsWalkable(I.L, PlayerPos) && !I.L.bNoWater && !I.bBig) { PlayerPos = FVector2D(FMath::Clamp(float(PlayerPos.X), I.L.WalkMinX + 4.f, I.L.WalkMaxX - 4.f), ShoreY - 8.f); }
	}
	Facing = bFromExit ? EntryFacing : EFVFacing::Down;
	Anim = EFVCharAnim::Idle;
	AnimTime = 0.f;
	bMoving = false;
	HeldSprite = FFVSprite();

	I.Texts.Reset(); I.Trail.Reset();
	I.LastWalkFrame = -1;
	I.InitAmbient(ShoreY);

	I.PetStyle = EFVPetStyle::None;
	RefreshAppearance();
	// update 6: the camera starts on the player (classic one-screen: stays at 0,0; hotfix 0.61: the wide pond scrolls)
	I.bCamSnap = true;
	I.UpdateCamera(*this, 0.f);
}

void FVScene::RefreshAppearance()
{
	FImpl& I = *Impl;
	UFVSaveGame* Profile = Game ? Game->GetProfile() : nullptr;
	if (!Profile) { return; }
	Look = Profile->BuildAppearance();
	const EFVPetStyle NewPet = Profile->GetPetStyle();
	if (NewPet != I.PetStyle)
	{
		I.PetStyle = NewPet;
		FVector2D P = PlayerPos + FVector2D(-26.f, -2.f);
		if (!IsWalkable(I.L, P)) { P = PlayerPos + FVector2D(26.f, -2.f); }
		if (!IsWalkable(I.L, P)) { P = PlayerPos; }
		I.PetPos = P;
		I.Trail.Reset();
		I.PetAnim = EFVPetAnim::Idle;
		I.PetAnimTime = 0.f;
		I.PetIdleTime = 0.f;
		I.bPetFlip = P.X > PlayerPos.X;
	}
}

// =============================================================================================
// Player
// =============================================================================================
void FVScene::SetPlayerAnim(EFVCharAnim NewAnim, bool bRestart)
{
	if (NewAnim != Anim || bRestart)
	{
		Anim = NewAnim;
		AnimTime = 0.f;
	}
}

int32 FVScene::GetPlayerFrame() const
{
	const int32 N = FMath::Max(1, UFVSprites::CharacterFrameCount(Anim));
	return FMath::Max(0, FMath::FloorToInt(AnimTime * AnimFps(Anim))) % N;
}

FVector2D FVScene::GetRodTip() const
{
	const FVector2D Off = UFVSprites::RodTipOffset(Anim, Facing, GetPlayerFrame()) - UFVSprites::CharacterAnchor();
	return PlayerPos + Off * WS;
}

FVector2D FVScene::GetHeadTop() const
{
	return PlayerPos + (UFVSprites::HeadTopOffset() - UFVSprites::CharacterAnchor()) * WS;
}

// =============================================================================================
// Water queries
// =============================================================================================
bool FVScene::IsWater(const FVector2D& P) const
{
	if (Impl->L.bPond) { return InPondWater(Impl->L, P, -1.f); }   // update 4b: the practice pond of the yard
	if (!HasWater() || Impl->L.bNoWater) { return false; }
	if (Impl->bBig)
	{
		// update 6: the water of a big world (City pier, Cave lake), world px
		const FFVRect& WA = Impl->L.WaterArea;
		if (WA.W <= 0.f || P.X < WA.X || P.X > WA.Right()) { return false; }
	}
	else
	{
		// classic: the canvas (hotfix 0.61: a scrolling classic place — its scroll range)
		const FFVRect WB = GetWorldBounds();
		if (P.X < WB.X || P.X > WB.Right()) { return false; }
	}
	if (P.Y < ShoreY || P.Y > GetWaterBottomY()) { return false; }
	const FFVRect& Dock = Impl->L.Dock;
	if (Dock.W > 0.f && P.X >= Dock.X && P.X <= Dock.Right() && P.Y <= Dock.Bottom() + 2.f) { return false; }
	// A cast into a net tangles: the net is not open water.
	for (const FFVRect& N : Impl->NetRects) { if (N.Contains(P)) { return false; } }
	return true;
}

float FVScene::DepthAt(const FVector2D& P) const
{
	if (Impl->L.bPond)
	{
		// the practice pond is deepest in the middle
		return InPondWater(Impl->L, P, 0.f) ? FMath::Clamp(1.f - FMath::Sqrt(PondDist2(Impl->L, P)), 0.f, 1.f) : -1.f;
	}
	if (!HasWater() || Impl->L.bNoWater) { return -1.f; }
	const float Span = FMath::Max(1.f, GetWaterBottomY() - ShoreY);
	return FMath::Min(1.f, float(P.Y - ShoreY) / Span);
}

bool FVScene::CanFishFromHere() const
{
	if (Impl->L.bPond) { return Impl->AtPondSpot(PlayerPos); }   // update 4b: only at the practice pond at home
	if (!HasWater() || Impl->L.bNoWater) { return false; }
	const FFVRect& Dock = Impl->L.Dock;
	const bool bOnDock = Dock.W > 0.f && PlayerPos.X >= Dock.X + 4.f && PlayerPos.X <= Dock.Right() - 4.f
		&& PlayerPos.Y >= Dock.Y && PlayerPos.Y <= Dock.Bottom();
	if (bOnDock) { return true; }
	if (Impl->bBig)
	{
		// update 6: only where the cast (straight down from the rod tip) lands in the world's water
		const FFVRect& WA = Impl->L.WaterArea;
		const float TipX = float(PlayerPos.X) + FVScenePondImpl::CastTipDX();
		if (WA.W <= 0.f || TipX < WA.X + 8.f || TipX > WA.Right() - 8.f) { return false; }
	}
	return PlayerPos.Y >= ShoreY - 12.f && PlayerPos.Y <= ShoreY + 1.f;
}

FVector2D FVScene::GetCastTarget(float Power01, float RodRange) const
{
	const FVector2D Tip = GetRodTip();
	const FLayout& PL = Impl->L;
	if (PL.bPond)
	{
		// Update 4b: the practice pond is small — any rod reaches across it; the power picks the spot between just past
		// the bank (or the plank's end) and the near shore.
		const float CX = float(PL.PondCenter.X), CY = float(PL.PondCenter.Y);
		const float X = FMath::Clamp(float(Tip.X), CX - PL.PondRadii.X * 0.82f, CX + PL.PondRadii.X * 0.82f);
		const float Half = FMath::Max(10.f, PondHalfH(PL, X));
		const float Top = CY - Half, Bot = CY + Half;
		const FFVRect& K = PL.PondPlank;
		float MinY = FMath::Max(Top + 9.f, float(Tip.Y) + 10.f);
		if (K.W > 0.f && X >= K.X - 3.f && X <= K.Right() + 3.f) { MinY = FMath::Max(MinY, K.Bottom() + 6.f); }
		const float MaxY = Bot - 5.f;
		MinY = FMath::Min(MinY, MaxY - 6.f);
		return FVector2D(X, FMath::Lerp(MinY, MaxY, FMath::Clamp(Power01, 0.f, 1.f)));
	}
	const FFVRect& Dock = Impl->L.Dock;
	const bool bBigWater = Impl->bBig && PL.WaterArea.W > 16.f;   // update 6: the water of a big world
	const FFVRect WB = GetWorldBounds();   // classic: the canvas (hotfix 0.61: the scroll range of the wide pond)
	const float X = bBigWater ? FMath::Clamp(float(Tip.X), PL.WaterArea.X + 8.f, PL.WaterArea.Right() - 8.f) : FMath::Clamp(float(Tip.X), WB.X + 8.f, WB.Right() - 8.f);
	const float Bottom = GetWaterBottomY();
	const float FullDepth = FMath::Max(1.f, Bottom - ShoreY);
	const bool bOverDock = Dock.W > 0.f && X >= Dock.X - 3.f && X <= Dock.Right() + 3.f;
	float MinY = (bOverDock ? Dock.Bottom() + 4.f : ShoreY) + 10.f;
	MinY = FMath::Min(MinY, Bottom - 20.f);
	// Standing further out (on the dock) reaches deeper water; on land the reach is exactly RodRange of the depth.
	const float Base = ShoreY + FMath::Max(0.f, float(PlayerPos.Y) - ShoreY) * 0.5f;
	float MaxY = Base + FMath::Clamp(RodRange, 0.05f, 1.f) * FullDepth;
	MaxY = FMath::Clamp(MaxY, MinY + 12.f, Bottom - 6.f);
	const float Y = FMath::Lerp(MinY, MaxY, FMath::Clamp(Power01, 0.f, 1.f));
	return FVector2D(X, Y);
}

// =============================================================================================
// Effects
// =============================================================================================
void FVScene::SpawnSplash(const FVector2D& P, float Size)
{
	FImpl& I = *Impl;
	Size = FMath::Clamp(Size, 0.2f, 3.f);
	const bool bLava = I.L.bLava;
	const int32 N = FMath::RoundToInt(10.f * Size);
	for (int32 k = 0; k < N; ++k)
	{
		FParticle Pt;
		Pt.P = P + FVector2D(I.Rng.FRandRange(-4.f, 4.f) * Size, I.Rng.FRandRange(-2.f, 1.f));
		Pt.V = FVector2D(I.Rng.FRandRange(-45.f, 45.f) * Size, -I.Rng.FRandRange(60.f, 130.f) * FMath::Sqrt(Size));
		Pt.Gravity = 340.f;
		Pt.Life = I.Rng.FRandRange(0.35f, 0.7f);
		Pt.Size = I.Rng.FRand() < 0.3f ? 4.f : 2.f;
		Pt.C = bLava ? (I.Rng.FRand() < 0.5f ? FV::Col(255, 220, 110) : FV::Col(255, 120, 40))
			: (I.Rng.FRand() < 0.6f ? FV::Col(I.L.Foam) : FV::Col(I.Loc ? I.Loc->WaterShallow : FColor(120, 190, 230)));
		Pt.Kind = EPart::Drop;
		I.Particles.Add(Pt);
	}
	SpawnRipple(P, Size);
	if (Size > 0.7f) { SpawnRipple(P + FVector2D(0.f, 1.f), Size * 0.6f); }
}

void FVScene::SpawnRipple(const FVector2D& P, float Size)
{
	FImpl& I = *Impl;
	if (I.Ripples.Num() >= MaxRipples) { I.Ripples.RemoveAt(0); }
	FRippleFx R;
	R.P = P;
	R.Size = FMath::Clamp(Size, 0.2f, 3.f);
	R.Life = 0.7f + 0.45f * R.Size;
	I.Ripples.Add(R);
}

void FVScene::SpawnFloatingText(const FVector2D& P, const FString& Text, const FLinearColor& C)
{
	FFloatText T;
	T.P = P;
	T.Text = Text;
	T.C = C;
	Impl->Texts.Add(T);
	if (Impl->Texts.Num() > 16) { Impl->Texts.RemoveAt(0); }
}

void FVScene::SpawnSparkles(const FVector2D& P, const FLinearColor& C, int32 Count)
{
	FImpl& I = *Impl;
	Count = FMath::Clamp(Count, 1, 80);
	for (int32 k = 0; k < Count; ++k)
	{
		FParticle Pt;
		const float A = I.Rng.FRandRange(0.f, UE_TWO_PI);
		const float R = I.Rng.FRandRange(2.f, 18.f);
		Pt.P = P + FVector2D(FMath::Cos(A) * R, FMath::Sin(A) * R * 0.8f);
		Pt.V = FVector2D(FMath::Cos(A) * 14.f, -I.Rng.FRandRange(12.f, 34.f));
		Pt.Life = I.Rng.FRandRange(0.6f, 1.2f);
		Pt.Size = I.Rng.FRand() < 0.35f ? 4.f : 2.f;
		Pt.C = C;
		Pt.Kind = EPart::Sparkle;
		I.Particles.Add(Pt);
	}
}

// =============================================================================================
// Tick
// =============================================================================================
void FVScene::Tick(float Dt, const FFVInput& In, bool bAllowMove)
{
	FImpl& I = *Impl;
	// "reached" / "failed" are true for exactly one frame (UFVGame polls them before the next scene tick)
	I.bWalkReached = false;
	I.bWalkFailed = false;
	I.SyncCanvas(*this);
	Dt = FMath::Clamp(Dt, 0.f, 0.1f);
	I.Time += Dt;
	I.TickWorld(*this, Dt);
	const FLayout& L = I.L;
	const float Night = Game ? FMath::Clamp(Game->GetNightFactor(), 0.f, 1.f) : 0.f;
	const bool bRain = Game && Game->GetWeather() == EFVWeather::Rain;
	// Water area relative to the landscape scene: portrait has more water, so more glints/ripples/jumps.
	const float WaterArea = FMath::Clamp(FV::VW * FMath::Max(1.f, GetWaterBottomY() - ShoreY) / (640.f * 154.f), 0.5f, 2.5f);
	const float AreaMul = L.bPortrait ? WaterArea : 1.f;
	const float WideMul = I.BleedMul();   // glints / rain ripples are spread over the bleed too
	const FFVRect WorldR = I.WorldR;

	// ---------------------------------------------------------------- player movement
	FVector2D Move(In.MoveX, In.MoveY);
	if (!bAllowMove) { Move = FVector2D::ZeroVector; }
	if (Move.SizeSquared() > 1.f) { Move.Normalize(); }
	bool bWantsMove = Move.SizeSquared() > 0.0025f;
	if (I.bWalk)
	{
		if (bWantsMove)
		{
			// any manual movement cancels the auto-walk
			I.bWalk = false;
			I.bWalkFailed = true;
		}
		else if (bAllowMove)
		{
			const FVector2D Auto = I.WalkSteer(*this, Dt);
			if (!Auto.IsNearlyZero()) { Move = Auto; bWantsMove = true; }
		}
	}
	bMoving = false;
	if (bWantsMove)
	{
		if (FMath::Abs(Move.X) > FMath::Abs(Move.Y) + 0.01f) { Facing = Move.X < 0.f ? EFVFacing::Left : EFVFacing::Right; }
		else { Facing = Move.Y < 0.f ? EFVFacing::Up : EFVFacing::Down; }
		FVector2D Delta = Move * (FV::PlayerSpeed * Dt);
		if (L.bPond && Move.Y > 0.3f && !IsWalkable(L, PlayerPos + FVector2D(0.f, Delta.Y)))
		{
			// update 4b: walking down next to the practice pond's plank slides the feet onto it
			const FFVRect& K = L.PondPlank;
			const float KC = K.Center().X;
			const float Off = float(KC - PlayerPos.X);
			if (K.W > 0.f && FMath::Abs(Off) > 4.f && FMath::Abs(Off) < K.W * 0.5f + 10.f && PlayerPos.Y >= K.Y - 16.f && PlayerPos.Y <= K.Bottom())
			{
				Delta.X = FMath::Sign(Off) * FMath::Min(FMath::Abs(Off) - 3.f, FV::PlayerSpeed * Dt);
			}
		}
		if (!IsWalkable(L, PlayerPos) && L.bBigWorld && IsWalkable(L, I.FindWalkable(PlayerPos)))
		{
			// 0.6: big worlds rescue by a snap to the nearest walkable spot — moving freely let the feet pass through a
			// blocker (a windfall / rubble / the broken footbridge) the player stood in
			PlayerPos = I.FindWalkable(PlayerPos);
		}
		else if (!IsWalkable(L, PlayerPos))
		{
			// rescue: somehow outside the walkable area -> move freely until back inside
			PlayerPos += Delta;
			PlayerPos.X = FMath::Clamp(float(PlayerPos.X), L.WalkMinX, FMath::Max(L.WalkMinX, L.WalkMaxX));
			const float MaxY = (L.bNoWater || L.bBigWorld) ? L.WalkBottom : (L.Dock.W > 0.f ? float(L.Dock.Bottom()) - 6.f : L.ShoreY - 2.f);
			PlayerPos.Y = FMath::Clamp(float(PlayerPos.Y), L.WalkTop, FMath::Max(L.WalkTop, MaxY));
			if (!L.bBigWorld) { PlayerPos.Y = FMath::Min(float(PlayerPos.Y), FV::VH - FV::BottomBarH - 12.f); }
		}
		else if (IsWalkable(L, PlayerPos + Delta)) { PlayerPos += Delta; }
		else if (IsWalkable(L, PlayerPos + FVector2D(Delta.X, 0.f))) { PlayerPos.X += Delta.X; }
		else if (IsWalkable(L, PlayerPos + FVector2D(0.f, Delta.Y))) { PlayerPos.Y += Delta.Y; }
		bMoving = true;
		I.bAtSpawn = false;
	}
	if (I.bWalk && bAllowMove) { I.UpdateWalkProgress(*this, Dt); }
	// update 6: exits of big worlds + the camera (after the movement step)
	I.TickBigWorld(*this, Dt);
	if (I.bWalkReached)
	{
		// arrived: turn towards the object the player walked to
		for (const FFVWorldObject& O : I.Objects)
		{
			if (FVector2D::Distance(O.Feet, I.WalkTarget) > 1.f) { continue; }
			const FVector2D To = O.Hit.Center() - PlayerPos;
			if (FMath::Abs(To.X) > FMath::Abs(To.Y) * 1.4f) { Facing = To.X < 0.f ? EFVFacing::Left : EFVFacing::Right; }
			else { Facing = To.Y < 0.f ? EFVFacing::Up : EFVFacing::Down; }
			break;
		}
	}
	if (bMoving) { SetPlayerAnim(EFVCharAnim::Walk); }
	else if (Anim == EFVCharAnim::Walk) { SetPlayerAnim(EFVCharAnim::Idle); }
	AnimTime += Dt;

	if (Anim == EFVCharAnim::Walk)
	{
		const int32 Frame = GetPlayerFrame();
		if (Frame != I.LastWalkFrame && (Frame % 2) == 0)
		{
			if (UFVAudio* A = UFVAudio::Get()) { A->Play(EFVSfx::Step, 0.22f, I.Rng.FRandRange(0.9f, 1.1f)); }
		}
		I.LastWalkFrame = Frame;
	}
	else { I.LastWalkFrame = -1; }

	// ---------------------------------------------------------------- pet (follows the player's trail)
	if (I.PetStyle != EFVPetStyle::None)
	{
		if (I.Trail.Num() == 0 || FVector2D::Distance(I.Trail.Last(), PlayerPos) >= 5.f)
		{
			if (FVector2D::Distance(I.PetPos, PlayerPos) > 4.f) { I.Trail.Add(PlayerPos); }
			if (I.Trail.Num() > 96) { I.Trail.RemoveAt(0); }
		}
		if (FVector2D::Distance(I.PetPos, PlayerPos) > 300.f)
		{
			I.PetPos = PlayerPos;
			I.Trail.Reset();
		}
		float PathLen = 0.f;
		FVector2D Prev = I.PetPos;
		for (const FVector2D& P : I.Trail) { PathLen += float(FVector2D::Distance(Prev, P)); Prev = P; }
		PathLen += float(FVector2D::Distance(Prev, PlayerPos));

		constexpr float Follow = 30.f;
		bool bPetMoving = false;
		if (PathLen > Follow + 1.f)
		{
			const float Speed = FV::PlayerSpeed * (PathLen > Follow + 60.f ? 1.4f : 0.95f);
			float Budget = FMath::Min(Speed * Dt, PathLen - Follow);
			int32 Guard = 0;
			while (Budget > 0.01f && Guard++ < 64)
			{
				const FVector2D Target = I.Trail.Num() > 0 ? I.Trail[0] : PlayerPos;
				const FVector2D Dir = Target - I.PetPos;
				const float Len = float(Dir.Size());
				if (FMath::Abs(Dir.X) > 0.3f) { I.bPetFlip = Dir.X < 0.f; }
				if (Len <= Budget)
				{
					I.PetPos = Target;
					Budget -= Len;
					if (I.Trail.Num() > 0) { I.Trail.RemoveAt(0); } else { break; }
				}
				else
				{
					I.PetPos += Dir / Len * Budget;
					Budget = 0.f;
				}
			}
			bPetMoving = true;
		}
		EFVPetAnim NewAnim;
		if (bPetMoving) { NewAnim = EFVPetAnim::Walk; I.PetIdleTime = 0.f; }
		else
		{
			I.PetIdleTime += Dt;
			NewAnim = I.PetIdleTime > 3.f ? EFVPetAnim::Sit : EFVPetAnim::Idle;
			if (FMath::Abs(PlayerPos.X - I.PetPos.X) > 6.f) { I.bPetFlip = PlayerPos.X < I.PetPos.X; }
		}
		if (NewAnim != I.PetAnim) { I.PetAnim = NewAnim; I.PetAnimTime = 0.f; }
		I.PetAnimTime += Dt;
	}

	// ---------------------------------------------------------------- water glints (at home: only the practice pond)
	int32 WantGlints = L.bNoWater ? (L.bPond ? (bRain ? 3 : 7) : 0) : FMath::RoundToInt(float(L.bLava ? 46 : (bRain ? 30 : 56)) * AreaMul * WideMul);
	if (L.bBigWorld && !L.bNoWater)
	{
		// update 6: the density of the classic scene over the whole water of the world
		const FFVRect WR = I.WaterRect();
		WantGlints = FMath::Clamp(FMath::RoundToInt(float(bRain ? 30 : 56) * WR.W * WR.H / (640.f * 154.f)), 8, 220);
	}
	while (I.Glints.Num() < WantGlints)
	{
		FGlint G;
		G.P = L.bNoWater ? I.RandomPondPoint() : I.RandomWaterPoint(6.f, WorldR.Bottom() - 4.f, true);
		G.Life = I.Rng.FRandRange(1.2f, 3.2f);
		G.Age = I.Rng.FRandRange(0.f, G.Life * 0.5f);
		G.bSparkle = !L.bLava && I.Rng.FRand() < 0.22f;
		G.W = L.bNoWater ? 2.f * float(I.Rng.RandRange(1, 3)) : 2.f * float(I.Rng.RandRange(2, 5 + FMath::RoundToInt(float((G.P.Y - L.ShoreY) / 40.0))));
		I.Glints.Add(G);
	}
	for (int32 k = I.Glints.Num() - 1; k >= 0; --k)
	{
		FGlint& G = I.Glints[k];
		G.Age += Dt;
		const float DepthF = FMath::Clamp(float((G.P.Y - L.ShoreY) / 150.0), 0.f, 1.f);
		G.P.X += L.Current * (0.5f + DepthF) * Dt;
		if (G.Age >= G.Life || G.P.X > WorldR.Right() + 10.f || !I.OnWaterSurface(G.P)) { I.Glints.RemoveAtSwap(k); }
	}

	// ---------------------------------------------------------------- jumping fish
	I.NextJump -= Dt;
	if (I.NextJump <= 0.f && L.bNoWater) { I.NextJump = 5.f; }
	if (I.NextJump <= 0.f)
	{
		I.NextJump = (L.bLava ? I.Rng.FRandRange(9.f, 18.f) : I.Rng.FRandRange(5.f, 13.f)) / AreaMul;
		const FVector2D A = I.RandomWaterPoint(40.f, FV::VH - 26.f);
		const float Dir = I.Rng.FRand() < 0.5f ? -1.f : 1.f;
		FJump J;
		J.A = A;
		J.B = A + FVector2D(Dir * I.Rng.FRandRange(26.f, 46.f), I.Rng.FRandRange(-4.f, 4.f));
		if (I.OnWaterSurface(J.B))
		{
			J.Life = I.Rng.FRandRange(0.65f, 0.85f);
			J.H = I.Rng.FRandRange(14.f, 24.f);
			I.Jumps.Add(J);
			SpawnSplash(J.A, 0.6f);
		}
	}
	for (int32 k = I.Jumps.Num() - 1; k >= 0; --k)
	{
		FJump& J = I.Jumps[k];
		J.Age += Dt;
		if (J.Age >= J.Life)
		{
			const FVector2D End = J.B;
			I.Jumps.RemoveAtSwap(k);
			SpawnSplash(End, 0.7f);
		}
	}

	// ---------------------------------------------------------------- birds
	if ((L.bBirds || L.bGulls) && Night < 0.45f && !bRain)
	{
		I.NextBird -= Dt;
		if (I.NextBird <= 0.f)
		{
			I.NextBird = I.Rng.FRandRange(7.f, 16.f);
			const bool bFromLeft = I.Rng.FRand() < 0.5f;
			const int32 Flock = I.Rng.RandRange(1, 3);
			float BaseY = I.Rng.FRandRange(40.f, 120.f);
			if (L.bPortrait)
			{
				// Tall screens: birds cross the whole land area; gulls also glide over the sea.
				BaseY = (L.bGulls && I.Rng.FRand() < 0.45f)
					? I.Rng.FRandRange(ShoreY + 30.f, FMath::Max(ShoreY + 40.f, GetWaterBottomY() - 60.f))
					: I.Rng.FRandRange(FV::TopBarH + 12.f, FMath::Max(FV::TopBarH + 20.f, ShoreY - 110.f));
			}
			const float Speed = I.Rng.FRandRange(45.f, 70.f);
			// update 6: big worlds — the flock crosses the part of the world the camera shows
			const float BirdX0 = I.bBig ? float(I.Cam.X) : float(WorldR.X), BirdX1 = I.bBig ? float(I.Cam.X) + FV::VW : float(WorldR.Right());
			if (I.bBig) { BaseY = float(I.Cam.Y) + I.Rng.FRandRange(FV::TopBarH + 12.f, FMath::Max(FV::TopBarH + 20.f, FV::VH * 0.45f)); }
			for (int32 k = 0; k < Flock; ++k)
			{
				FBird B;
				B.P = FVector2D(bFromLeft ? BirdX0 - 20.f - k * 16.f : BirdX1 + 20.f + k * 16.f, BaseY + k * 9.f);
				B.V = FVector2D(bFromLeft ? Speed : -Speed, I.Rng.FRandRange(-6.f, 6.f));
				B.Phase = I.Rng.FRandRange(0.f, 1.f);
				I.Birds.Add(B);
			}
		}
	}
	for (int32 k = I.Birds.Num() - 1; k >= 0; --k)
	{
		FBird& B = I.Birds[k];
		B.P += B.V * Dt;
		B.P.Y += FMath::Sin(I.Time * 2.f + B.Phase * 6.f) * 6.f * Dt;
		if (B.P.X < WorldR.X - 60.f || B.P.X > WorldR.Right() + 60.f
			|| (I.bBig && FMath::Abs(float(B.P.X - I.Cam.X) - FV::VW * 0.5f) > FV::VW * 1.5f + 60.f)) { I.Birds.RemoveAtSwap(k); }
	}

	// ---------------------------------------------------------------- butterflies
	for (FButterfly& B : I.Butterflies)
	{
		B.Retarget -= Dt;
		if (B.Retarget <= 0.f && L.FlowerSpots.Num() > 0)
		{
			B.Target = L.FlowerSpots[I.Rng.RandHelper(L.FlowerSpots.Num())];
			B.Retarget = I.Rng.FRandRange(6.f, 12.f);
		}
		B.Home = FMath::Lerp(B.Home, B.Target, FMath::Min(1.f, Dt * 0.35f));
		const float T = I.Time * B.Speed + B.Phase;
		B.P = B.Home + FVector2D(FMath::Sin(T * 0.7f) * 22.f + FMath::Sin(T * 2.3f) * 4.f, FMath::Sin(T * 1.3f + B.Phase) * 9.f - 14.f);
	}

	// ---------------------------------------------------------------- fireflies
	for (FFirefly& F : I.Fireflies)
	{
		F.Heading += I.Rng.FRandRange(-2.5f, 2.5f) * Dt;
		F.P += FVector2D(FMath::Cos(F.Heading), FMath::Sin(F.Heading)) * F.Speed * Dt;
		if (F.P.X < I.FlyX0 - 4.f) { F.Heading = 0.f; }
		if (F.P.X > I.FlyX1 + 4.f) { F.Heading = UE_PI; }
		if (F.P.Y < I.FlyTop) { F.Heading = UE_HALF_PI; }
		if (F.P.Y > I.FlyBottom) { F.Heading = -UE_HALF_PI; }
	}

	// ---------------------------------------------------------------- bubbles (swamp gas, lava)
	if (L.bBubbles || L.bLava)
	{
		I.NextBubble -= Dt;
		if (I.NextBubble <= 0.f)
		{
			I.NextBubble = (L.bLava ? I.Rng.FRandRange(0.25f, 0.8f) : I.Rng.FRandRange(0.6f, 1.6f)) / AreaMul;
			FBubble B;
			B.P = I.RandomWaterPoint(10.f, FV::VH - 20.f);
			B.Life = I.Rng.FRandRange(1.f, 2.f);
			B.R = I.Rng.FRandRange(2.f, L.bLava ? 5.f : 3.5f);
			I.Bubbles.Add(B);
		}
	}
	for (int32 k = I.Bubbles.Num() - 1; k >= 0; --k)
	{
		FBubble& B = I.Bubbles[k];
		B.Age += Dt;
		if (B.Age >= B.Life)
		{
			const FVector2D P = B.P;
			const bool bBig = B.R > 3.5f;
			I.Bubbles.RemoveAtSwap(k);
			SpawnRipple(P, bBig ? 0.6f : 0.4f);
			if (L.bLava)
			{
				for (int32 j = 0; j < (bBig ? 6 : 3); ++j)
				{
					FParticle Pt;
					Pt.P = P;
					Pt.V = FVector2D(I.Rng.FRandRange(-30.f, 30.f), -I.Rng.FRandRange(40.f, 90.f));
					Pt.Gravity = 260.f;
					Pt.Life = I.Rng.FRandRange(0.4f, 0.7f);
					Pt.Size = 2.f;
					Pt.C = I.Rng.FRand() < 0.5f ? FV::Col(255, 220, 110) : FV::Col(255, 120, 40);
					I.Particles.Add(Pt);
				}
			}
		}
	}

	// ---------------------------------------------------------------- smoke & embers
	I.NextSmoke -= Dt;
	if (I.NextSmoke <= 0.f)
	{
		I.NextSmoke = 0.45f;
		for (const FVector2D& S : L.Smokes)
		{
			FParticle Pt;
			Pt.P = S + FVector2D(I.Rng.FRandRange(-2.f, 2.f), 0.f);
			Pt.V = FVector2D(I.Rng.FRandRange(4.f, 9.f), -I.Rng.FRandRange(10.f, 16.f));
			Pt.Life = I.Rng.FRandRange(2.8f, 4.f);
			Pt.Size = I.Rng.FRandRange(3.f, 4.5f);
			Pt.C = L.bLava ? FV::Col(90, 80, 86) : FV::Col(214, 214, 214);
			Pt.Kind = EPart::Smoke;
			I.Particles.Add(Pt);
		}
	}
	if (L.bEmbers || L.Fires.Num() > 0)
	{
		I.NextEmber -= Dt;
		if (I.NextEmber <= 0.f)
		{
			I.NextEmber = (L.bEmbers ? 0.12f : 0.4f) / AreaMul;
			FParticle Pt;
			if (L.Fires.Num() > 0 && (!L.bEmbers || I.Rng.FRand() < 0.3f))
			{
				Pt.P = L.Fires[I.Rng.RandHelper(L.Fires.Num())] + FVector2D(I.Rng.FRandRange(-4.f, 4.f), -6.f);
			}
			else
			{
				const float CamX = (!I.bBig && I.HasCamera()) ? float(I.Cam.X) : 0.f;   // hotfix 0.61: the view of a scrolling place
				Pt.P = FVector2D(CamX + I.Rng.FRandRange(0.f, FV::VW), I.Rng.FRandRange(L.ShoreY + 4.f, FV::VH - FV::BottomBarH * 0.5f));
				if (!I.OnWaterSurface(Pt.P)) { Pt.P.Y = L.ShoreY + 8.f; }
			}
			Pt.V = FVector2D(I.Rng.FRandRange(-8.f, 8.f), -I.Rng.FRandRange(18.f, 40.f));
			Pt.Life = I.Rng.FRandRange(1.4f, 2.8f);
			Pt.Size = 2.f;
			Pt.C = I.Rng.FRand() < 0.5f ? FV::Col(255, 200, 90) : FV::Col(255, 110, 40);
			Pt.Kind = EPart::Ember;
			I.Particles.Add(Pt);
		}
	}

	// ---------------------------------------------------------------- rain ripples, dock post ripples
	if (bRain && !L.bNoWater)
	{
		I.NextRainRipple -= Dt;
		while (I.NextRainRipple <= 0.f)
		{
			I.NextRainRipple += 1.f / (16.f * AreaMul * WideMul);
			SpawnRipple(I.RandomWaterPoint(4.f, WorldR.Bottom() - 4.f, true), I.Rng.FRandRange(0.25f, 0.5f));
		}
	}
	I.NextPostRipple -= Dt;
	if (I.NextPostRipple <= 0.f && L.Dock.W > 0.f && !L.bLava && !L.bNoWater)
	{
		I.NextPostRipple = I.Rng.FRandRange(1.2f, 2.2f);
		const float PX = I.Rng.FRand() < 0.5f ? L.Dock.X + 1.f : L.Dock.Right() - 1.f;
		SpawnRipple(FVector2D(PX, L.Dock.Bottom() + 8.f), 0.5f);
	}
	if (L.bPond)
	{
		// update 4b: the practice pond — rain rings, soft rings around the plank's end posts
		if (bRain)
		{
			I.NextRainRipple -= Dt;
			while (I.NextRainRipple <= 0.f)
			{
				I.NextRainRipple += I.Rng.FRandRange(0.12f, 0.3f);
				SpawnRipple(I.RandomPondPoint(), I.Rng.FRandRange(0.25f, 0.45f));
			}
		}
		I.NextPondRipple -= Dt;
		if (I.NextPondRipple <= 0.f && L.PondPlank.W > 0.f)
		{
			I.NextPondRipple = I.Rng.FRandRange(1.6f, 2.8f);
			const float PX = I.Rng.FRand() < 0.5f ? L.PondPlank.X + 1.f : L.PondPlank.Right() - 1.f;
			SpawnRipple(FVector2D(PX, L.PondPlank.Bottom() + 7.f), 0.4f);
		}
	}

	// ---------------------------------------------------------------- particles, ripples, texts
	for (int32 k = I.Particles.Num() - 1; k >= 0; --k)
	{
		FParticle& Pt = I.Particles[k];
		Pt.Age += Dt;
		Pt.V.Y += Pt.Gravity * Dt;
		if (Pt.Kind == EPart::Ember) { Pt.V.X += FMath::Sin(I.Time * 4.f + float(k)) * 20.f * Dt; }
		Pt.P += Pt.V * Dt;
		if (Pt.Age >= Pt.Life) { I.Particles.RemoveAtSwap(k); }
	}
	if (I.Particles.Num() > MaxParticles) { I.Particles.RemoveAt(0, I.Particles.Num() - MaxParticles); }
	for (int32 k = I.Ripples.Num() - 1; k >= 0; --k)
	{
		I.Ripples[k].Age += Dt;
		if (I.Ripples[k].Age >= I.Ripples[k].Life) { I.Ripples.RemoveAt(k); }
	}
	for (int32 k = I.Texts.Num() - 1; k >= 0; --k)
	{
		I.Texts[k].Age += Dt;
		if (I.Texts[k].Age >= I.Texts[k].Life) { I.Texts.RemoveAt(k); }
	}
	for (FFogBand& F : I.Fog)
	{
		F.X += F.Speed * Dt;
		const float W = 96.f * F.Scale * WS;
		const bool bWideFog = L.bPortrait || L.bBigWorld;
		const bool bScrollFog = !bWideFog && I.HasCamera();   // hotfix 0.61: the landscape pond scrolls
		const float X0 = bWideFog ? WorldR.X : (bScrollFog ? float(L.ScrollRect.X) : 0.f);
		const float X1 = bWideFog ? WorldR.Right() : (bScrollFog ? float(L.ScrollRect.Right()) : FV::VW);
		if (F.Speed > 0.f && F.X > X1 + 10.f) { F.X = X0 - W - 10.f; }
		if (F.Speed < 0.f && F.X < X0 - W - 10.f) { F.X = X1 + 10.f; }
	}
}

// =============================================================================================
// Drawing
// =============================================================================================
void FVScene::DrawScene(FVDraw& D)
{
	FImpl& I = *Impl;
	I.SyncCanvas(*this);
	I.EnsureDecor();
	const FLayout& L = I.L;
	const float T = I.Time;
	const float Night = Game ? FMath::Clamp(Game->GetNightFactor(), 0.f, 1.f) : 0.f;
	const bool bRain = Game && Game->GetWeather() == EFVWeather::Rain;
	const FColor Deep = I.Loc ? I.Loc->WaterDeep : FColor(30, 80, 150);

	// ---------------------------------------------------------------- background (+ bleed beyond every edge)
	if (!I.bBig && !I.Bg.IsValid() && I.Loc && UFVSprites::Get()) { I.Bg = I.EnsureLocation(*I.Loc, I.BgSize).Bg; }
	const FFVRect WorldR = I.WorldR;
	if (I.bBig)
	{
		// update 6 (Р38): beyond the edge of a big world the screen is black; only visible chunks are drawn
		I.EnsureBigTextures();
		D.Rect(I.VisibleRect(D), FLinearColor::Black);
	}
	else
	{
		// Flat fills for anything even further out than the painted bleed (extreme letterboxes): never black.
		constexpr float Far = 4000.f;
		if (L.bNoWater)
		{
			D.Rect(FFVRect(-Far, -Far, FV::VW + 2.f * Far, FV::VH + 2.f * Far), FV::Col(L.FarLand));
			if (WorldR.Y > -Far + 1.f) { D.Rect(FFVRect(-Far, -Far, FV::VW + 2.f * Far, WorldR.Y + Far + 1.f), FV::Col(L.FarTop)); }
		}
		else
		{
			D.Rect(FFVRect(-Far, -Far, FV::VW + 2.f * Far, ShoreY + Far), FV::Col(L.FarLand));
			if (WorldR.Y > -Far + 1.f) { D.Rect(FFVRect(-Far, -Far, FV::VW + 2.f * Far, WorldR.Y + Far + 1.f), FV::Col(L.FarTop)); }
			D.Rect(FFVRect(-Far, ShoreY, FV::VW + 2.f * Far, Far), I.Bg.IsValid() ? FV::Col(L.FarWater) : FV::Col(Deep));
		}
	}
	// house interior: wallpaper, floor and the sky in the windows show through holes of the background
	if (L.bInterior) { I.DrawInteriorBack(D, *this); }
	if (I.bBig) { I.DrawBigBackground(D); }
	else if (I.Bg.IsValid()) { D.Sprite(I.Bg, L.Origin, WS); }
	// wall decor (painting, trophy wall, curtains) and ground decals (dig spots, rug)
	I.DrawWorldLayer(D, *this, 2);
	I.DrawWorldLayer(D, *this, 0);
	auto OverDock = [&L](float X0, float X1, float Y) -> bool
	{
		return L.Dock.W > 0.f && X1 > L.Dock.X - 6.f && X0 < L.Dock.Right() + 8.f && Y < L.Dock.Bottom() + 8.f;
	};

	// ---------------------------------------------------------------- rolling waves (ocean)
	const FLinearColor Foam = FV::Col(L.Foam);
	if (L.bWaves)
	{
		// Portrait: the sea is much taller, so the swell starts further out and more rollers are visible.
		const int32 NWaves = L.bPortrait ? 4 : 3;
		const float Reach = L.bPortrait ? 110.f : 44.f;
		const float Travel = L.bPortrait ? 104.f : 40.f;
		const int32 NX0 = FMath::FloorToInt(WorldR.X / 8.f), NX1 = FMath::CeilToInt(WorldR.Right() / 8.f);
		for (int32 k = 0; k < NWaves; ++k)
		{
			const float Ph = FMath::Frac(T / (L.bPortrait ? 7.f : 5.5f) + float(k) / float(NWaves));
			const float Y = Snap(L.ShoreY + Reach - Ph * Travel, 2.f);
			const float A = FMath::Sin(Ph * UE_PI) * 0.5f;
			for (int32 Xi = NX0; Xi < NX1; ++Xi)
			{
				const float X = float(Xi) * 8.f;
				if (FMath::Frac(FMath::Sin(float(Xi) * 12.9898f + float(k) * 78.233f) * 43758.5453f) < 0.3f) { continue; }
				const float Wy = Y + Snap(FMath::Sin(X * 0.03f + T * 0.8f + float(k)) * 2.f, 2.f);
				if (OverDock(X, X + 8.f, Wy)) { continue; }
				D.Rect(FFVRect(X, Wy, 8.f, 2.f), WithAlpha(Foam, A));
				D.Rect(FFVRect(X + 2.f, Wy + 2.f, 4.f, 2.f), WithAlpha(Foam, A * 0.5f));
			}
		}
	}

	// ---------------------------------------------------------------- shore foam line (water scenes only)
	// (update 6: a big world has foam only along its water)
	const FFVRect FoamR = (I.bBig && L.WaterArea.W > 0.f) ? L.WaterArea : WorldR;
	const int32 Foam0 = FMath::FloorToInt(FoamR.X / 6.f), Foam1 = L.bNoWater ? Foam0
		: (I.bBig ? FMath::FloorToInt((FoamR.Right() - 4.f) / 6.f) + 1 : FMath::CeilToInt(WorldR.Right() / 6.f));
	for (int32 Xi = Foam0; Xi < Foam1; ++Xi)
	{
		const float X = float(Xi) * 6.f;
		const float S = FMath::Sin(T * 1.6f + X * 0.09f);
		const float Y = Snap(L.ShoreY + 3.f + S * 1.5f, 2.f);
		if (OverDock(X, X + 4.f, Y)) { continue; }
		const float A = (L.bLava ? 0.35f : 0.28f) + 0.18f * FMath::Sin(T * 2.3f + X * 0.21f);
		D.Rect(FFVRect(X, Y, 4.f, 2.f), WithAlpha(Foam, A));
	}
	if (L.bPond)
	{
		// update 4b: the practice pond — water gently lapping at the near (south) shore
		const float CX = float(L.PondCenter.X), RX = float(L.PondRadii.X);
		for (float X = Snap(CX - RX + 10.f, 2.f); X <= CX + RX - 12.f; X += 6.f)
		{
			const float Y = Snap(float(L.PondCenter.Y) + PondHalfH(L, X + 2.f) - 5.f + FMath::Sin(T * 1.6f + X * 0.09f) * 1.2f, 2.f);
			const float A = 0.2f + 0.14f * FMath::Sin(T * 2.3f + X * 0.21f);
			D.Rect(FFVRect(X, Y, 4.f, 2.f), WithAlpha(Foam, A));
		}
	}

	// ---------------------------------------------------------------- glints & sparkles
	{
		const FLinearColor GC = FV::Col(L.Glint);
		const float DayA = L.bLava ? 0.85f : (1.f - 0.55f * Night) * (bRain ? 0.6f : 1.f);
		for (const FGlint& G : I.Glints)
		{
			const float A = FMath::Sin(G.Age / G.Life * UE_PI) * DayA;
			if (A <= 0.02f) { continue; }
			const FVector2D P = Snap2(G.P, 2.f);
			if (G.bSparkle)
			{
				if (Night > 0.6f) { continue; }
				D.Rect(FFVRect(P.X, P.Y - 2.f, 2.f, 6.f), WithAlpha(FLinearColor::White, A));
				D.Rect(FFVRect(P.X - 2.f, P.Y, 6.f, 2.f), WithAlpha(FLinearColor::White, A));
			}
			else
			{
				D.Rect(FFVRect(P.X, P.Y, G.W, 2.f), WithAlpha(GC, A * 0.55f));
			}
		}
	}

	// ---------------------------------------------------------------- lily pads & reeds
	{
		const EDecor Pads[3] = { EDecor::LilyPad, EDecor::LilyPadFlower, EDecor::LilyPadSmall };
		for (int32 k = 0; k < L.LilyPads.Num(); ++k)
		{
			const uint8 Kind = L.LilyKinds.IsValidIndex(k) ? L.LilyKinds[k] : 0;
			const float Bob = FMath::Sin(T * 1.3f + float(k) * 1.7f) > 0.3f ? 2.f : 0.f;
			const FVector2D P = Snap2(L.LilyPads[k], 2.f) + FVector2D(0.f, Bob);
			const FFVSprite& S = I.D(Pads[FMath::Min<int32>(Kind, 2)]);
			if (S.IsValid()) { D.SpriteCentered(S, P, WS, FLinearColor::White, (k % 2) == 1); }
		}
		const float Wind = (bRain ? 1.8f : 1.f) * (L.bWaves ? 1.4f : 1.f);
		for (int32 k = 0; k < L.Reeds.Num(); ++k)
		{
			const float S = FMath::Sin(T * 1.1f * Wind + float(k) * 0.9f) + 0.4f * FMath::Sin(T * 2.7f + float(k));
			const int32 Frame = S < -0.45f ? 0 : (S > 0.45f ? 2 : 1);
			const EDecor K = EDecor(int32(L.bCattails ? EDecor::Cattail0 : EDecor::Reed0) + Frame);
			const FFVSprite& Spr = I.D(K);
			if (!Spr.IsValid()) { continue; }
			const FVector2D Pv = DecorPivot(K);
			const FVector2D P = Snap2(L.Reeds[k], 2.f);
			D.Sprite(Spr, P - FVector2D(Spr.W * Pv.X, Spr.H * Pv.Y) * WS, WS);
		}
	}

	// ---------------------------------------------------------------- nets hanging in the water
	if (!L.bNoWater) { I.DrawWorldLayer(D, *this, 1); }

	// ---------------------------------------------------------------- ripples & bubbles
	{
		for (const FRippleFx& R : I.Ripples)
		{
			const float F = R.Age / R.Life;
			const int32 Frame = FMath::Clamp(FMath::FloorToInt(F * float(RippleFrames) * FMath::Min(1.f, 0.45f + R.Size * 0.55f)), 0, RippleFrames - 1);
			const FFVSprite& S = I.D(EDecor(int32(EDecor::Ripple0) + Frame));
			if (S.IsValid()) { D.SpriteCentered(S, Snap2(R.P, 2.f), WS, WithAlpha(Foam, (1.f - F) * 0.75f)); }
		}
		for (const FBubble& B : I.Bubbles)
		{
			const float F = B.Age / B.Life;
			const float Rr = FMath::Max(1.f, B.R * FMath::Min(1.f, F * 1.6f));
			const FVector2D P = Snap2(B.P, 2.f);
			if (L.bLava)
			{
				D.Circle(P, Rr * 2.f, FV::Col(255, 190, 80, 220));
				D.Circle(P + FVector2D(-Rr * 0.5f, -Rr * 0.5f), Rr * 0.8f, FV::Col(255, 240, 170, 220));
			}
			else
			{
				D.Circle(P, Rr * 2.f, FV::Col(Deep.R, Deep.G, Deep.B, 160));
				D.Circle(P + FVector2D(-Rr * 0.6f, -Rr * 0.6f), FMath::Max(1.f, Rr * 0.6f), FV::Col(220, 240, 200, 200));
			}
		}
	}

	// ---------------------------------------------------------------- jumping fish
	{
		const FFVSprite& S = I.D(EDecor::JumpFish);
		const FLinearColor FishC = L.bLava ? FV::Col(70, 26, 24) : FMath::Lerp(FV::Col(Deep), FV::Col(24, 30, 44), 0.45f);
		for (const FJump& J : I.Jumps)
		{
			if (!S.IsValid()) { break; }
			const float F = FMath::Clamp(J.Age / J.Life, 0.f, 1.f);
			const FVector2D P = FMath::Lerp(J.A, J.B, F) - FVector2D(0.f, FMath::Sin(F * UE_PI) * J.H);
			const bool bRight = J.B.X > J.A.X;
			const float Ang = -FMath::Cos(F * UE_PI) * 50.f * (bRight ? 1.f : -1.f);
			D.SpriteCentered(S, Snap2(P, 1.f), WS, FishC, !bRight, Ang);
		}
	}

	// ---------------------------------------------------------------- y-sorted: props, flames, pet, player
	{
		struct FEnt { float Y; uint8 Kind; int32 Index; };
		TArray<FEnt, TInlineAllocator<32>> Ents;
		for (int32 k = 0; k < L.Props.Num(); ++k) { Ents.Add({ float(L.Props[k].Feet.Y), 0, k }); }
		for (int32 k = 0; k < L.Fires.Num(); ++k) { Ents.Add({ float(L.Fires[k].Y) + 7.f, 1, k }); }
		for (int32 k = 0; k < I.Vis.Num(); ++k) { if (I.Vis[k].Layer == 3) { Ents.Add({ I.Vis[k].SortY, 4, k }); } }
		if (I.PetStyle != EFVPetStyle::None) { Ents.Add({ float(I.PetPos.Y), 2, 0 }); }
		Ents.Add({ float(PlayerPos.Y) + 0.01f, 3, 0 });
		Ents.StableSort([](const FEnt& A, const FEnt& B) { return A.Y < B.Y; });

		UFVSprites* Sprites = UFVSprites::Get();
		for (const FEnt& E : Ents)
		{
			if (E.Kind == 0)
			{
				const FProp& P = L.Props[E.Index];
				const FFVSprite& S = I.D(P.Kind);
				if (!S.IsValid()) { continue; }
				const FVector2D Pv = DecorPivot(P.Kind);
				const FFVSprite& Sh = I.D(EDecor::Shadow);
				if (Sh.IsValid() && P.Kind != EDecor::BeachUmbrella && P.Kind != EDecor::GlowShrooms)
				{
					D.SpriteCentered(Sh, P.Feet + FVector2D(2.f, -1.f), WS * FMath::Clamp(S.W / 14.f, 0.6f, 1.6f), FLinearColor(1.f, 1.f, 1.f, 0.8f));
				}
				D.Sprite(S, Snap2(P.Feet - FVector2D(S.W * Pv.X, S.H * Pv.Y) * WS, 1.f), WS, FLinearColor::White, P.bFlip);
			}
			else if (E.Kind == 1)
			{
				const FVector2D Base = Snap2(L.Fires[E.Index], 2.f);
				const float Sd = float(E.Index) * 3.7f;
				struct FLayer { int32 Half; float HMul; FLinearColor C; };
				const FLayer Layers[3] = { { 3, 1.f, FV::Col(236, 88, 30) }, { 2, 0.7f, FV::Col(255, 176, 50) }, { 1, 0.4f, FV::Col(255, 244, 190) } };
				const float H = 12.f + 3.f * FMath::Sin(T * 9.f + Sd) + 2.f * FMath::Sin(T * 23.f + Sd * 3.f);
				for (const FLayer& Ly : Layers)
				{
					for (int32 Dx = -Ly.Half; Dx <= Ly.Half; ++Dx)
					{
						const float Prof = 1.f - FMath::Abs(float(Dx)) / float(Ly.Half + 1);
						const float Hh = Snap(H * Ly.HMul * Prof * (0.75f + 0.25f * FMath::Sin(T * 17.f + float(Dx) * 2.1f + Sd)), 2.f);
						if (Hh <= 0.f) { continue; }
						D.Rect(FFVRect(Base.X + float(Dx) * 2.f - 1.f, Base.Y - Hh, 2.f, Hh), Ly.C);
					}
				}
			}
			else if (E.Kind == 4)
			{
				I.DrawVis(D, *this, E.Index);
			}
			else if (E.Kind == 2)
			{
				if (!Sprites) { continue; }
				const int32 N = FMath::Max(1, UFVSprites::PetFrameCount(I.PetAnim));
				const int32 Frame = FMath::Max(0, FMath::FloorToInt(I.PetAnimTime * PetFps(I.PetAnim))) % N;
				const FFVSprite& S = Sprites->Pet(I.PetStyle, I.PetAnim, Frame);
				const FFVSprite& Sh = I.D(EDecor::ShadowSmall);
				if (Sh.IsValid()) { D.SpriteCentered(Sh, I.PetPos + FVector2D(0.f, -1.f), WS, FLinearColor(1.f, 1.f, 1.f, 0.9f)); }
				if (S.IsValid())
				{
					const FVector2D TopLeft = I.PetPos - FVector2D(S.W * 0.5f, S.H - 1.f) * WS;
					D.Sprite(S, Snap2(TopLeft, 1.f), WS, FLinearColor::White, I.bPetFlip);
				}
			}
			else
			{
				if (!Sprites) { continue; }
				const FFVSprite& Sh = I.D(EDecor::Shadow);
				if (Sh.IsValid()) { D.SpriteCentered(Sh, PlayerPos + FVector2D(0.f, -1.f), WS, FLinearColor(1.f, 1.f, 1.f, 0.9f)); }
				const FFVSprite& S = Sprites->Character(Look, Anim, Facing, GetPlayerFrame());
				if (S.IsValid())
				{
					// pickup: a quick crouch while the item flies up
					const float Dip = (I.PickupAge >= 0.f && I.PickupAge < 0.22f) ? 2.f : 0.f;
					const FVector2D TopLeft = PlayerPos - UFVSprites::CharacterAnchor() * WS + FVector2D(0.f, Dip);
					D.Sprite(S, Snap2(TopLeft, 1.f), WS);
				}
				if (Anim == EFVCharAnim::HoldFish && HeldSprite.IsValid())
				{
					const float Lift = Snap(FMath::Min(1.f, AnimTime * 5.f) * 6.f, 1.f);
					const FVector2D C = GetHeadTop() - FVector2D(0.f, HeldSprite.H * WS * 0.5f + 2.f + Lift);
					D.SpriteCentered(HeldSprite, Snap2(C, 1.f), WS);
					if (FMath::Frac(T * 0.8f) < 0.15f)
					{
						D.Rect(FFVRect(C.X + HeldSprite.W * 0.3f * WS, C.Y - 6.f, 2.f, 6.f), FV::Col(255, 255, 255, 200));
						D.Rect(FFVRect(C.X + HeldSprite.W * 0.3f * WS - 2.f, C.Y - 4.f, 6.f, 2.f), FV::Col(255, 255, 255, 200));
					}
				}
			}
		}
	}

	I.DrawWorldAfter(D, *this);

	// ---------------------------------------------------------------- particles (drops, sparkles, smoke)
	for (const FParticle& Pt : I.Particles)
	{
		const float F = Pt.Age / Pt.Life;
		switch (Pt.Kind)
		{
		case EPart::Drop:
			D.Rect(FFVRect(Snap(Pt.P.X), Snap(Pt.P.Y), Pt.Size, Pt.Size), WithAlpha(Pt.C, 1.f - F * F));
			break;
		case EPart::Sparkle:
		{
			const float A = (1.f - F) * (0.6f + 0.4f * FMath::Sin(Pt.Age * 22.f));
			const FVector2D P = Snap2(Pt.P, 1.f);
			const float S = Pt.Size;
			D.Rect(FFVRect(P.X - S * 0.5f, P.Y - S * 1.5f, S, S * 3.f), WithAlpha(Pt.C, A));
			D.Rect(FFVRect(P.X - S * 1.5f, P.Y - S * 0.5f, S * 3.f, S), WithAlpha(Pt.C, A));
			break;
		}
		case EPart::Smoke:
			D.Circle(Pt.P, Pt.Size + F * 7.f, WithAlpha(Pt.C, 0.38f * (1.f - F) * FMath::Min(1.f, Pt.Age * 3.f)));
			break;
		default: break;
		}
	}

	// ---------------------------------------------------------------- birds & butterflies
	{
		const bool bGull = L.bGulls;
		for (const FBird& B : I.Birds)
		{
			const int32 Frame = FMath::Frac(T * 5.f + B.Phase) < 0.5f ? 0 : 1;
			const FFVSprite& S = I.D(bGull ? (Frame ? EDecor::Gull1 : EDecor::Gull0) : (Frame ? EDecor::Bird1 : EDecor::Bird0));
			if (S.IsValid()) { D.SpriteCentered(S, Snap2(B.P, 1.f), WS, bGull ? FLinearColor::White : FV::Col(L.BirdColor), B.V.X < 0.f); }
		}
		const float Vis = FMath::Clamp(1.f - Night * 2.5f, 0.f, 1.f) * (bRain ? 0.f : 1.f);
		if (Vis > 0.01f)
		{
			for (int32 k = 0; k < I.Butterflies.Num(); ++k)
			{
				const FButterfly& B = I.Butterflies[k];
				const int32 Frame = FMath::Frac(T * 7.f + B.Phase) < 0.5f ? 0 : 1;
				const FFVSprite& S = I.D(Frame ? EDecor::Butterfly1 : EDecor::Butterfly0);
				if (S.IsValid()) { D.SpriteCentered(S, Snap2(B.P, 1.f), WS, WithAlpha(B.C, Vis)); }
			}
		}
	}
}

void FVScene::DrawLighting(FVDraw& D)
{
	FImpl& I = *Impl;
	const FLayout& L = I.L;
	const float T = I.Time;
	const float Night = Game ? FMath::Clamp(Game->GetNightFactor(), 0.f, 1.f) : 0.f;
	const bool bRain = Game && Game->GetWeather() == EFVWeather::Rain;
	const float Clock = Game ? Game->GetClockMinutes() : 720.f;
	// Tints cover everything the world covers (bleed and far fills included).
	// (update 6: big worlds — the visible part of the world, wherever the camera is)
	const FFVRect Full = I.bBig ? I.VisibleRect(D) : FFVRect(-4000.f, -4000.f, FV::VW + 8000.f, FV::VH + 8000.f);
	const FFVRect WorldR = I.WorldR;
	const bool bDark = I.bBig && I.BigDef.IsValid() && I.BigDef->DarkRadius > 0.f;   // update 6: the Cave

	// ---------------------------------------------------------------- swamp fog
	if (I.Fog.Num() > 0)
	{
		const FFVSprite& S = I.D(EDecor::Fog);
		if (S.IsValid())
		{
			const float Morning = Bump(Clock, 420.f, 150.f);
			// §18: the fog morning at the mill (06-10) reads as a real fog, not a haze (it doubles the herbal tea)
			const float MillMorning = (FVMill::IsMillId(LocationId) && FVMill::IsFogMorning(Clock)) ? FMath::Min(1.f, Bump(Clock, 480.f, 150.f) * 1.5f) : 0.f;
			// Two soft layers per band (no hard edges, one texture => batched tiles): the main wisp and a
			// flipped, taller, fainter wisp that slowly sways against it, so the bands never read as strips.
			// The logical box stays 96 texels wide (the drift wrap in Tick uses 96); 24 tall, the texture is 4:1.
			for (const FFogBand& F : I.Fog)
			{
				const float A = F.Alpha * (1.f + 0.4f * Night + 0.6f * Morning + 0.8f * MillMorning) * (0.85f + 0.15f * FMath::Sin(T * 0.4f + F.Y));
				const float BW = 96.f * WS * F.Scale, BH = 24.f * WS * F.Scale;
				const FLinearColor C = LinA(L.FogColor, A);
				D.SpriteRect(S, FFVRect(F.X, F.Y - 2.f * WS * F.Scale, BW, BH), C);
				const float Sway = FMath::Sin(T * 0.07f + F.Y * 0.013f) * BW * 0.14f;
				const float W2 = BW * 0.78f, H2 = BH * 1.35f;
				D.SpriteRect(S, FFVRect(F.X + (BW - W2) * 0.5f + Sway, F.Y - 2.f * WS * F.Scale + (BH - H2) * 0.5f + BH * 0.08f, W2, H2),
					LinA(L.FogColor, A * 0.5f), true);
			}
		}
	}

	// ---------------------------------------------------------------- weather & time-of-day tints
	// Indoors the weather only dims the room a little and the night is softer (the lamps are on).
	const bool bIndoor = L.bInterior;
	if (bRain) { D.Rect(Full, FV::Col(34, 44, 66, bIndoor ? 28 : 64)); }
	const float Warm = FMath::Max(Bump(Clock, 400.f, 100.f), Bump(Clock, 1170.f, 120.f)) * (1.f - Night * 0.5f);
	// update 6: under the ground (the Cave) there is no dawn / dusk / night: its own darkness instead
	if (Warm > 0.01f && !bDark) { D.Rect(Full, LinA(FColor(255, 140, 60), Warm * (bRain ? 0.08f : 0.16f) * (bIndoor ? 0.6f : 1.f))); }
	if (Night > 0.01f && !bDark) { D.Rect(Full, LinA(FColor(14, 20, 62), Night * (bIndoor ? 0.46f : 0.6f))); }
	if (I.bBig) { I.DrawBigDarkness(D, *this); }

	// ---------------------------------------------------------------- lit windows & light glows
	for (int32 k = 0; k < L.Lights.Num(); ++k)
	{
		const FLight& Lt = L.Lights[k];
		float Int = bDark ? 0.9f : (Lt.bAlways ? FMath::Max(0.4f, Night) : Night);
		if (Int <= 0.02f) { continue; }
		if (Lt.bFlicker) { Int *= 0.85f + 0.15f * FMath::Sin(T * 11.f + float(k) * 7.f) * FMath::Sin(T * 6.3f + float(k)); }
		if (Lt.Window.W > 0.f) { D.Rect(Lt.Window, LinA(FColor(255, 214, 130), FMath::Min(1.f, Int * 1.2f))); }
		FLinearColor C = FV::Col(Lt.Color) * (0.55f * Int);
		C.A = Int;
		D.Glow(Lt.Pos, Lt.Radius * (Lt.bFlicker ? 1.f + 0.04f * FMath::Sin(T * 9.f + float(k)) : 1.f), C);
	}
	// lava glow along the shore
	if (L.bLava)
	{
		const float Int = 0.22f + 0.3f * Night;
		const int32 NX0 = FMath::FloorToInt((WorldR.X - 36.f) / 72.f), NX1 = FMath::CeilToInt(WorldR.Right() / 72.f);
		// Landscape: one row along the shore. Portrait: rows down the whole (taller) lava lake, bleed included.
		const float RowStep = L.bPortrait ? 104.f : 1000.f;
		const float GYEnd = L.bPortrait ? WorldR.Bottom() + 20.f : GetWaterBottomY() + 20.f;
		int32 Row = 0;
		for (float GY = L.ShoreY + 26.f; GY < GYEnd; GY += RowStep, ++Row)
		{
			const float RowInt = Row == 0 ? Int : Int * 0.8f;
			for (int32 k = NX0; k < NX1; ++k)
			{
				const float X = 36.f + float(k) * 72.f + ((Row % 2) ? 36.f : 0.f);
				const float Pulse = 0.8f + 0.2f * FMath::Sin(T * 1.7f + float(k) * 1.3f + float(Row) * 2.1f);
				FLinearColor C = FV::Col(255, 110, 40) * (RowInt * Pulse);
				C.A = RowInt * Pulse;
				D.Glow(FVector2D(X, GY), 70.f, C);
			}
		}
		for (const FBubble& B : I.Bubbles)
		{
			FLinearColor C = FV::Col(255, 170, 60) * 0.35f;
			C.A = 0.35f;
			D.Glow(B.P, 14.f + B.R * 2.f, C);
		}
	}
	// runtime lights of world objects (house lamp, kitchen fire...)
	I.DrawWorldLights(D, *this, Night);
	// fires glow a bit more
	for (const FVector2D& F : L.Fires)
	{
		const float Int = 0.25f + 0.35f * Night;
		FLinearColor C = FV::Col(255, 150, 60) * Int;
		C.A = Int;
		D.Glow(F + FVector2D(0.f, -8.f), 26.f + 3.f * FMath::Sin(T * 13.f), C);
	}

	// ---------------------------------------------------------------- embers
	for (const FParticle& Pt : I.Particles)
	{
		if (Pt.Kind != EPart::Ember) { continue; }
		const float F = Pt.Age / Pt.Life;
		const float A = (1.f - F) * (0.7f + 0.3f * FMath::Sin(Pt.Age * 15.f));
		D.Rect(FFVRect(Snap(Pt.P.X), Snap(Pt.P.Y), 2.f, 2.f), WithAlpha(Pt.C, A));
		FLinearColor G = Pt.C * (0.25f * A);
		G.A = 0.25f * A;
		D.Glow(Pt.P, 6.f, G);
	}

	// ---------------------------------------------------------------- fireflies
	if (Night > 0.3f)
	{
		const float Vis = FMath::Clamp((Night - 0.3f) / 0.4f, 0.f, 1.f) * (bRain ? 0.4f : 1.f);
		for (const FFirefly& F : I.Fireflies)
		{
			const float Blink = FMath::Clamp(FMath::Sin(T * 2.2f + F.Phase * 3.f) * 1.4f, 0.f, 1.f) * Vis;
			if (Blink <= 0.03f) { continue; }
			FLinearColor G = FV::Col(210, 255, 120) * (0.6f * Blink);
			G.A = Blink;
			D.Glow(F.P, 9.f, G);
			D.Rect(FFVRect(Snap(F.P.X), Snap(F.P.Y), 2.f, 2.f), FV::Col(246, 255, 190, uint8(FMath::RoundToInt(Blink * 255.f))));
		}
	}

	// ---------------------------------------------------------------- rain streaks (outdoors)
	if (bRain && !bIndoor && !bDark)
	{
		// update 6: big worlds — the rain falls over the visible part of the world only
		FFVRect RainR = WorldR;
		if (I.bBig)
		{
			const float RX0 = FMath::Max(WorldR.X, Full.X), RY0 = FMath::Max(WorldR.Y, Full.Y);
			const float RX1 = FMath::Min(WorldR.Right(), Full.Right()), RY1 = FMath::Min(WorldR.Bottom(), Full.Bottom());
			RainR = FFVRect(RX0, RY0, FMath::Max(1.f, RX1 - RX0), FMath::Max(1.f, RY1 - RY0));
		}
		const FLinearColor RC = FV::Col(190, 210, 240, 110);
		// The rain falls over the whole background (bleed included), same density as the classic 110 streaks on 640x360.
		const int32 NStreaks = FMath::Clamp(FMath::RoundToInt(110.f * RainR.W * RainR.H / (640.f * 360.f)), 110, 600);
		// Streaks lean left while falling: the spawn band is widened so the bottom-right stays covered.
		const float SpanX = RainR.W + 40.f + RainR.H * 0.25f;
		for (int32 k = 0; k < NStreaks; ++k)
		{
			const float H1 = FMath::Frac(FMath::Sin(float(k) * 12.9898f) * 43758.5453f);
			const float H2 = FMath::Frac(FMath::Sin(float(k) * 78.233f) * 12543.123f);
			const float Speed = 360.f + H2 * 140.f;
			const float Fy = FMath::Fmod(T * Speed + H1 * 1000.f, RainR.H + 40.f) - 20.f;
			const float Y = RainR.Y + Fy;
			const float X = RainR.X - 20.f + FMath::Fmod(H1 * 1234.f + H2 * 640.f + 400.f + float(k) * 7.31f, SpanX) - Fy * 0.25f;
			D.Line(FVector2D(X, Y), FVector2D(X - 3.f, Y + 11.f), RC, 1.f);
		}
	}
}

void FVScene::DrawOverlay(FVDraw& D)
{
	Impl->DrawWorldOverlay(D, *this);
	for (const FFloatText& Tx : Impl->Texts)
	{
		const float F = Tx.Age / Tx.Life;
		const float A = F < 0.7f ? 1.f : FMath::Max(0.f, 1.f - (F - 0.7f) / 0.3f);
		const float Rise = 26.f * (1.f - FMath::Pow(1.f - F, 2.f));
		const float Pop = F < 0.08f ? 0.6f + F * 5.f : 1.f;
		D.TextOutlined(Tx.Text, FVector2D(Tx.P.X, Tx.P.Y - Rise), 9.f * Pop, WithAlpha(Tx.C, A), EFVAlign::Center, FV::Col(30, 18, 12, uint8(FMath::RoundToInt(A * 255.f))));
	}
}
