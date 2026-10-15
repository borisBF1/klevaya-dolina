// UPDATE 6 [SCENE-CAMERA] — camera & big locations (Docs/UPDATE6_FOREST_CITY.md §2, Р38; contract: FVScene.h,
// Private/FVWorldU6.h, FVSceneArt.h «UPDATE 6»). Owner: SCENE-CAMERA.
// Big worlds (FVWorldU6::Build*) become an FLayout in world px (LayoutFromWorld); their background is cut into chunk
// textures (only the visible ones are drawn); the camera follows the player on X and Y, stops at the world's edges and
// centres a world smaller than the screen; beyond the world the screen is black. Classic locations keep the camera at
// (0,0): world == screen.

#include "FVScene.h"
#include "FVSceneArt.h"
#include "FVDatabase.h"
#include "FVDraw.h"
#include "Engine/Canvas.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

using namespace FVSceneArt;
using namespace FVSceneRt;

namespace FVSceneCameraImpl
{
	constexpr int32 ChunkTex = 256;          // chunk size in texels (512 world px)
	constexpr float CamEase = 7.f;           // 1/s: how fast the camera catches up
	constexpr float CamJump = 220.f;         // a player jump longer than this (teleport) snaps the camera
	constexpr float HeadLift = 22.f;         // the camera centres the body, not the feet
	constexpr int32 HoleTex = 64;            // darkness hole sprite (texels)

	FFVRect Union(const FFVRect& A, const FFVRect& B)
	{
		if (A.W <= 0.f || A.H <= 0.f) { return B; }
		if (B.W <= 0.f || B.H <= 0.f) { return A; }
		const float X0 = FMath::Min(A.X, B.X), Y0 = FMath::Min(A.Y, B.Y);
		return FFVRect(X0, Y0, FMath::Max(A.Right(), B.Right()) - X0, FMath::Max(A.Bottom(), B.Bottom()) - Y0);
	}

	bool Overlaps(const FFVRect& A, const FFVRect& B)
	{
		return A.X < B.Right() && B.X < A.Right() && A.Y < B.Bottom() && B.Y < A.Bottom();
	}

	/** Puts P at Index of Arr, filling the gaps with the "unplaced" point. */
	void PutAt(TArray<FVector2D>& Arr, int32 Index, const FVector2D& P)
	{
		if (Index < 0 || Index > 255) { return; }
		while (Arr.Num() <= Index) { Arr.Add(FVector2D(UnplacedCoord, UnplacedCoord)); }
		Arr[Index] = P;
	}

	/** Camera coordinate on one axis: a world shorter than the visible band is centred in it (black on both sides),
	    otherwise the focus is centred and clamped so the band never leaves the world. */
	float AxisCam(float WorldLen, float BandStart, float BandLen, float Focus)
	{
		if (WorldLen <= BandLen) { return (WorldLen - BandLen) * 0.5f - BandStart; }
		return FMath::Clamp(Focus - BandStart - BandLen * 0.5f, -BandStart, WorldLen - BandStart - BandLen);
	}

	/** Integration (update 6): near the ends of the walkable range the camera leans towards the world edge, so the strip
	    of scenery the player cannot walk to (the sea left of the City embankment, the lake bottom of the Cave, the roofs
	    above the City street in landscape) still comes into view. Returns the offset added to the focus. */
	float EdgeLean(float WorldLen, float BandLen, float Focus, float WalkMin, float WalkMax)
	{
		if (WorldLen <= BandLen || WalkMax <= WalkMin) { return 0.f; }
		const float Half = BandLen * 0.5f;
		float Off = 0.f;
		const float GapLo = WalkMin - Half;
		const float Full = 16.f;   // the whole lean within this distance of the walkable end
		const float Ramp = FMath::Max(1.f, Half - Full);
		if (GapLo > 0.f) { Off -= GapLo * FMath::Clamp(1.f - (Focus - WalkMin - Full) / Ramp, 0.f, 1.f); }
		const float GapHi = (WorldLen - Half) - WalkMax;
		if (GapHi > 0.f) { Off += GapHi * FMath::Clamp(1.f - (WalkMax - Focus - Full) / Ramp, 0.f, 1.f); }
		return Off;
	}

	/** The darkness around the light circle of the Cave: transparent centre, stepped edge, opaque corners. */
	FFVImage BuildHole()
	{
		FFVImage Img(HoleTex, HoleTex, FColor(0, 0, 0, 255));
		const float C = float(HoleTex) * 0.5f;
		for (int32 Y = 0; Y < HoleTex; ++Y)
		{
			for (int32 X = 0; X < HoleTex; ++X)
			{
				const float D = FMath::Sqrt(FMath::Square(float(X) + 0.5f - C) + FMath::Square(float(Y) + 0.5f - C)) / C;
				float A = FMath::Clamp((D - 0.5f) / 0.5f, 0.f, 1.f);
				A = FMath::RoundToFloat(A * A * (3.f - 2.f * A) * 6.f) / 6.f;   // 6 pixel-art steps
				Img.Set(X, Y, FColor(34, 34, 56, uint8(FMath::RoundToInt(A * 255.f))));   // = the linear (0.016, 0.016, 0.04) of the rects around it
			}
		}
		return Img;
	}
}

// ============================================================================================
// FVSceneArt::LayoutFromWorld — big world -> scene layout (world px)
// ============================================================================================
void FVSceneArt::LayoutFromWorld(const FVWorldU6::FWorldDef& Def, const FFVLocationDef& Loc, FLayout& Out)
{
	using namespace FVSceneCameraImpl;
	using FVWorldU6::EAnchor;
	const float WSc = FV::WorldScale;
	Out = FLayout();
	Out.bBigWorld = true;
	Out.TexW = FMath::Max(1, Def.SizeTex.X);
	Out.TexH = FMath::Max(1, Def.SizeTex.Y);
	Out.WorldSize = FVector2D(float(Out.TexW) * WSc, float(Out.TexH) * WSc);
	Out.Origin = FVector2D::ZeroVector;
	Out.bPortrait = false;
	Out.BleedX = 0;
	Out.BleedY = 0;
	Out.FarTop = FColor(0, 0, 0);
	Out.FarLand = FColor(0, 0, 0);
	Out.FarWater = FColor(0, 0, 0);
	const FFVRect WorldRect(0.f, 0.f, float(Out.WorldSize.X), float(Out.WorldSize.Y));

	// walkable area
	Out.WalkRects = Def.Walk;
	if (Out.WalkRects.Num() == 0) { Out.WalkRects.Add(WorldRect.Inset(12.f)); }
	Out.Blockers = Def.Blockers;
	FFVRect WalkBox;
	for (const FFVRect& R : Out.WalkRects) { WalkBox = Union(WalkBox, R); }
	if (Def.bWater && Def.Dock.W > 0.f) { WalkBox = Union(WalkBox, Def.Dock); }
	Out.WalkMinX = WalkBox.X;
	Out.WalkMaxX = WalkBox.Right();
	Out.WalkTop = WalkBox.Y;
	Out.WalkBottom = WalkBox.Bottom();

	// water
	Out.bNoWater = !Def.bWater;
	if (Def.bWater)
	{
		Out.ShoreY = Def.ShoreY;
		Out.WaterArea = Def.WaterArea.W > 0.f ? Def.WaterArea : FFVRect(0.f, Def.ShoreY, WorldRect.W, FMath::Max(1.f, WorldRect.H - Def.ShoreY));
		Out.Dock = Def.Dock;
		Out.Glint = FMath::Lerp(FLinearColor(Loc.WaterShallow), FLinearColor::White, 0.75f).ToFColor(false);
	}
	else
	{
		Out.ShoreY = WorldRect.H;   // no shore inside the world
	}

	// interior (big house): the wallpaper / floor tiles are drawn behind the background's holes
	if (Def.WallAreas.Num() > 0 || Def.FloorAreas.Num() > 0)
	{
		Out.bInterior = true;
		for (const FFVRect& R : Def.WallAreas) { Out.WallRect = Union(Out.WallRect, R); }
		for (const FFVRect& R : Def.FloorAreas) { Out.FloorRect = Union(Out.FloorRect, R); }
		Out.Windows = Def.Windows;
	}

	// practice pond (yard)
	Out.bPond = Def.bPond;
	Out.PondCenter = Def.PondCenter;
	Out.PondRadii = Def.PondRadii;
	Out.PondPlank = Def.PondPlank;

	// ambience
	Out.bBirds = Def.bBirds;
	Out.bButterflies = Def.bButterflies;
	Out.bFireflies = Def.bFireflies;
	Out.bGulls = Def.bGulls;
	Out.bFog = Def.bFog;

	// anchors
	const FVector2D Unplaced(UnplacedCoord, UnplacedCoord);
	Out.Stations.Init(Unplaced, int32(EFVStation::Count));
	Out.DresserFeet = Unplaced;
	Out.RugCenter = Unplaced;
	Out.LampFeet = Unplaced;
	Out.PlantFeet = Unplaced;
	Out.PaintingCenter = Unplaced;
	bool bDefaultSpawn = false, bAnySpawn = false;
	const FName DefaultId(TEXT("default"));
	const FName DresserId(TEXT("dresser"));
	const FName OrderBoardId(TEXT("order_board"));
	for (const FVWorldU6::FAnchor& A : Def.Anchors)
	{
		switch (A.Kind)
		{
		case EAnchor::Spawn:
			if (!bDefaultSpawn && (A.Id == DefaultId || !bAnySpawn))
			{
				Out.Spawn = A.Feet;
				bDefaultSpawn = A.Id == DefaultId;
			}
			bAnySpawn = true;
			break;
		case EAnchor::Pickup: PutAt(Out.ResourceSlots, A.Index, A.Feet); break;
		case EAnchor::DigSpot: PutAt(Out.DigSpots, A.Index, A.Feet); break;
		case EAnchor::NetSpot: PutAt(Out.NetSpots, A.Index, A.Feet); break;
		case EAnchor::Door:
			if (!Out.bDoor)
			{
				Out.bDoor = true;
				Out.DoorFeet = A.Feet;
				Out.DoorRect = A.Hit.W > 0.f ? A.Hit : FFVRect(float(A.Feet.X) - 16.f, float(A.Feet.Y) - 52.f, 32.f, 48.f);
			}
			break;
		case EAnchor::Station:
			if (A.Id == DresserId) { Out.DresserFeet = A.Feet; break; }
			for (int32 s = 0; s < int32(EFVStation::Count); ++s)
			{
				if (FVDB::StationId(EFVStation(s)) == A.Id) { Out.Stations[s] = A.Feet; break; }
			}
			break;
		case EAnchor::DecorSlot:
			if (A.Index == int32(EFVDecorSlot::Rug)) { Out.RugCenter = A.Feet; }
			else if (A.Index == int32(EFVDecorSlot::Lamp)) { Out.LampFeet = A.Feet; }
			else if (A.Index == int32(EFVDecorSlot::Plant)) { Out.PlantFeet = A.Feet; }
			else if (A.Index == int32(EFVDecorSlot::Painting)) { Out.PaintingCenter = A.Feet; }
			else if (A.Index == int32(EFVDecorSlot::Curtains)) { Out.CurtainSpots.Add(A.Feet); }
			break;
		case EAnchor::Board:
			if (A.Id == OrderBoardId) { Out.bBoard = true; Out.BoardFeet = A.Feet; }
			break;
		case EAnchor::Boat:
			Out.bBoat = true;
			Out.BoatFeet = A.Feet;
			break;
		case EAnchor::Light:
		{
			FLight Lt;
			Lt.Pos = A.Feet;
			Lt.Radius = A.Radius > 0.f ? A.Radius : 40.f;
			Lt.Color = A.Color;
			Lt.bFlicker = A.bFlag;
			Lt.bAlways = Def.DarkRadius > 0.f || Out.bInterior;
			Out.Lights.Add(Lt);
			break;
		}
		case EAnchor::Fire: Out.Fires.Add(A.Feet); break;
		case EAnchor::Smoke: Out.Smokes.Add(A.Feet); break;
		default: break;   // trees, piles, NPCs, exits... are world objects of SCENE-OBJECTS
		}
	}
	if (!bAnySpawn) { Out.Spawn = Out.WalkRects[0].Center(); }

	// butterflies need flower spots: deterministic points on the walkable ground
	if (Out.bButterflies)
	{
		FRandomStream R(int32(GetTypeHash(Def.LocationId) & 0x7FFFFFFF) ^ 0x0B077E7);
		for (int32 k = 0; k < 14; ++k)
		{
			const FFVRect& W = Out.WalkRects[R.RandHelper(Out.WalkRects.Num())];
			Out.FlowerSpots.Add(FVector2D(W.X + R.FRand() * W.W, W.Y + R.FRand() * W.H));
		}
	}
}

// ============================================================================================
// FImpl: big worlds
// ============================================================================================
bool FVScene::FImpl::ApplyBigWorld(const FFVLocationDef& InLoc)
{
	using namespace FVSceneCameraImpl;
	FBigCache* Found = BigCache.Find(InLoc.Id);
	if (!Found)
	{
		FBigCache NewCache;
		NewCache.Def = MakeShared<FVWorldU6::FWorldDef>();
		TSharedPtr<FFVImage> Img = MakeShared<FFVImage>();
		if (!FVWorldU6::BuildWorld(InLoc.Id, *Img, *NewCache.Def) || !Img->IsValid())
		{
			UE_LOG(LogFV, Warning, TEXT("FV: big world '%s' could not be built, using the classic scene"), *InLoc.Id.ToString());
			return false;
		}
		// the image is the truth for the size (a builder might forget SizeTex)
		NewCache.Def->SizeTex = FIntPoint(Img->W, Img->H);
		if (NewCache.Def->LocationId.IsNone()) { NewCache.Def->LocationId = InLoc.Id; }
		LayoutFromWorld(*NewCache.Def, InLoc, NewCache.Layout);
		const int32 NX = FMath::DivideAndRoundUp(Img->W, ChunkTex);
		const int32 NY = FMath::DivideAndRoundUp(Img->H, ChunkTex);
		for (int32 cy = 0; cy < NY; ++cy)
		{
			for (int32 cx = 0; cx < NX; ++cx)
			{
				const int32 X0 = cx * ChunkTex, Y0 = cy * ChunkTex;
				const int32 CW = FMath::Min(ChunkTex, Img->W - X0), CH = FMath::Min(ChunkTex, Img->H - Y0);
				NewCache.ChunkRects.Add(FFVRect(float(X0) * WS, float(Y0) * WS, float(CW) * WS, float(CH) * WS));
				NewCache.Chunks.Add(FFVSprite());
			}
		}
		NewCache.PendingImg = Img;
		UE_LOG(LogFV, Log, TEXT("FV: built big world '%s' (%dx%d texels, %d chunks, %d anchors)"), *InLoc.Id.ToString(), Img->W, Img->H,
			NewCache.Chunks.Num(), NewCache.Def->Anchors.Num());
		Found = &BigCache.Add(InLoc.Id, MoveTemp(NewCache));
	}
	bBig = true;
	BigId = InLoc.Id;
	BigDef = Found->Def;
	L = Found->Layout;
	Bg = FFVSprite();
	WorldR = FFVRect(0.f, 0.f, float(L.WorldSize.X), float(L.WorldSize.Y));
	LayoutCanvas = FVector2D(FV::VW, FV::VH);
	BaseBlockers = L.Blockers.Num();
	BgChunks.Reset();
	BgChunkRects.Reset();
	EnsureBigTextures();
	return true;
}

void FVScene::FImpl::EnsureBigTextures()
{
	FBigCache* C = bBig ? BigCache.Find(BigId) : nullptr;
	if (!C) { return; }
	UFVSprites* S = UFVSprites::Get();
	bool bChanged = BgChunks.Num() != C->Chunks.Num();
	if (S && C->PendingImg.IsValid())
	{
		bChanged = true;
		const FFVImage& Img = *C->PendingImg;
		const uint32 IdHash = GetTypeHash(BigId) & 0xFFFFFFu;
		for (int32 k = 0; k < C->ChunkRects.Num() && k < C->Chunks.Num(); ++k)
		{
			const FFVRect& R = C->ChunkRects[k];
			const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::WorldBg, IdHash, uint32(k), 1);
			if (const FFVSprite* Cached = S->FindCached(Key)) { C->Chunks[k] = *Cached; continue; }
			const FFVImage Part = Img.Cropped(FMath::RoundToInt(R.X / WS), FMath::RoundToInt(R.Y / WS), FMath::RoundToInt(R.W / WS), FMath::RoundToInt(R.H / WS));
			bool bAny = false;
			for (const FColor& P : Part.Px) { if (P.A > 0) { bAny = true; break; } }
			if (!bAny) { continue; }   // fully transparent: the black beyond the world shows
			C->Chunks[k] = S->AddCached(Key, Part, FString::Printf(TEXT("WorldBg_%s_%d"), *BigId.ToString(), k));
		}
		C->PendingImg.Reset();   // the textures hold the picture now
	}
	if (bChanged)
	{
		BgChunks = C->Chunks;
		BgChunkRects = C->ChunkRects;
	}
}

FVector2D FVScene::FImpl::BigEntryFeet(FName Entry) const
{
	if (!BigDef.IsValid()) { return L.Spawn; }
	const FName DefaultId(TEXT("default"));
	const FVWorldU6::FAnchor* Default = nullptr;
	for (const FVWorldU6::FAnchor& A : BigDef->Anchors)
	{
		if (A.Kind != FVWorldU6::EAnchor::Spawn) { continue; }
		if (!Entry.IsNone() && A.Id == Entry) { return A.Feet; }
		if (A.Id == DefaultId && !Default) { Default = &A; }
	}
	return Default ? Default->Feet : L.Spawn;
}

void FVScene::FImpl::TickBigWorld(FVScene& S, float Dt)
{
	// Р69: the one-screen locations have exits too (WalkExits, canvas px, built by RebuildWorldU6)
	const TArray<FVWorldU6::FAnchor>* ExitList = (bBig && BigDef.IsValid()) ? &BigDef->Anchors : (bBig ? nullptr : &WalkExits);
	if (ExitList)
	{
		// Walking into an exit = travelling there (UFVGame polls GetPendingExit). An exit fires only after the player
		// stood outside every exit, so arriving on one (or still standing in it after ClearPendingExit) does nothing.
		bool bInAny = false;
		for (const FVWorldU6::FAnchor& A : *ExitList)
		{
			if (A.Kind != FVWorldU6::EAnchor::Exit || A.Target.IsNone()) { continue; }
			const FFVRect Hit = A.Hit.W > 0.f ? A.Hit : FFVRect(float(A.Feet.X) - 20.f, float(A.Feet.Y) - 20.f, 40.f, 40.f);
			if (!Hit.Contains(S.PlayerPos)) { continue; }
			bInAny = true;
			if (bExitArmed && !bExitPending)
			{
				bExitPending = true;
				ExitLocation = A.Target;
				ExitEntry = A.Id;
				bExitArmed = false;
			}
			break;
		}
		if (!bInAny) { bExitArmed = true; }
	}
	UpdateCamera(S, Dt);
}

FFVRect FVScene::FImpl::VisibleRect(const FVDraw& Dr) const
{
	FVector2D A = Cam, B = Cam + FVector2D(FV::VW, FV::VH);
	if (Dr.Canvas && Dr.Scale > 0.f)
	{
		A = Dr.ToVirtual(FVector2D(0.f, 0.f));
		B = Dr.ToVirtual(FVector2D(Dr.Canvas->ClipX, Dr.Canvas->ClipY));
	}
	return FFVRect(FMath::FloorToFloat(float(A.X)) - 2.f, FMath::FloorToFloat(float(A.Y)) - 2.f,
		FMath::CeilToFloat(float(B.X - A.X)) + 4.f, FMath::CeilToFloat(float(B.Y - A.Y)) + 4.f);
}

void FVScene::FImpl::DrawBigBackground(FVDraw& Dr) const
{
	const FFVRect VisR = VisibleRect(Dr);
	for (int32 k = 0; k < BgChunks.Num() && k < BgChunkRects.Num(); ++k)
	{
		if (!BgChunks[k].IsValid() || !FVSceneCameraImpl::Overlaps(BgChunkRects[k], VisR)) { continue; }
		Dr.Sprite(BgChunks[k], BgChunkRects[k].Pos(), WS);
	}
}

void FVScene::FImpl::DrawBigDarkness(FVDraw& Dr, const FVScene& S) const
{
	if (!bBig || !BigDef.IsValid()) { return; }
	const FFVRect VisR = VisibleRect(Dr);
	// the ambient tint of the world (a multiplier colour, white = none), approximated by a dark coloured veil
	const FColor Amb = BigDef->Ambient;
	const float AmbK = 1.f - float(FMath::Min3(Amb.R, Amb.G, Amb.B)) / 255.f;
	if (AmbK > 0.01f)
	{
		Dr.Rect(VisR, LinA(FColor(uint8(Amb.R / 5), uint8(Amb.G / 5), uint8(Amb.B / 5)), AmbK * 0.55f));
	}
	const float Rad = S.GetDarkRadius();
	if (Rad <= 0.f) { return; }
	FFVSprite Hole;
	if (UFVSprites* Spr = UFVSprites::Get())
	{
		const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::WorldBg, 0xDA4C00u, 0, 5);
		const FFVSprite* Cached = Spr->FindCached(Key);
		Hole = Cached ? *Cached : Spr->AddCached(Key, FVSceneCameraImpl::BuildHole(), TEXT("WorldBg_DarkHole"));
	}
	constexpr float Dark = 0.93f;
	const float R = Rad * (1.f + 0.025f * FMath::Sin(Time * 6.3f) * FMath::Sin(Time * 2.1f));   // the lantern breathes
	const FVector2D C = Snap2(S.GetPlayerPos() - FVector2D(0.f, FVSceneCameraImpl::HeadLift), 2.f);
	const FFVRect Box(float(C.X) - R, float(C.Y) - R, R * 2.f, R * 2.f);
	const FLinearColor Black(0.016f, 0.016f, 0.04f, Dark);
	if (Hole.IsValid()) { Dr.SpriteRect(Hole, Box, FLinearColor(1.f, 1.f, 1.f, Dark)); }
	else { Dr.Rect(Box, WithAlpha(Black, Dark * 0.5f)); }
	// everything around the circle
	if (Box.Y > VisR.Y) { Dr.Rect(FFVRect(VisR.X, VisR.Y, VisR.W, Box.Y - VisR.Y), Black); }
	if (VisR.Bottom() > Box.Bottom()) { Dr.Rect(FFVRect(VisR.X, Box.Bottom(), VisR.W, VisR.Bottom() - Box.Bottom()), Black); }
	const float Y0 = FMath::Max(VisR.Y, Box.Y), Y1 = FMath::Min(VisR.Bottom(), Box.Bottom());
	if (Y1 > Y0)
	{
		if (Box.X > VisR.X) { Dr.Rect(FFVRect(VisR.X, Y0, Box.X - VisR.X, Y1 - Y0), Black); }
		if (VisR.Right() > Box.Right()) { Dr.Rect(FFVRect(Box.Right(), Y0, VisR.Right() - Box.Right(), Y1 - Y0), Black); }
	}
}

void FVScene::FImpl::UpdateCamera(const FVScene& S, float Dt)
{
	using namespace FVSceneCameraImpl;
	if (!bBig && !HasCamera())
	{
		Cam = FVector2D::ZeroVector;
		CamSmooth = FVector2D::ZeroVector;
		bCamSnap = false;
		return;
	}
	const FVector2D Feet = S.PlayerPos;
	if (!bBig)
	{
		// Hotfix 0.61: a classic layout wider than the screen (the pond): the camera moves along X only, clamped to the
		// scroll range (the content + the HUD pad on the right; the painted bleed shows beyond). No edge lean and no
		// HUD-column lean: Y stays 0 (the classic composition is one screen tall), the pad keeps the column off the right
		// walk edge. It RESTS on the classic framing (x 0) while the player walks the old middle and is pushed only by a
		// player near a side (a centred follow put Mitrich's stall under the landscape button column whenever the player
		// stood left of the middle); landscape keeps more room on the right, where the column covers the walk band.
		const bool bPort = FV::IsPortrait();
		const float MarginL = bPort ? 80.f : 128.f, MarginR = bPort ? 80.f : 170.f;
		const float PushL = float(Feet.X) - MarginL, PushR = float(Feet.X) - (FV::VW - MarginR);
		const float X0 = float(L.ScrollRect.X), X1 = FMath::Max(X0, float(L.ScrollRect.Right()) - FV::VW);
		const float TargetX = FMath::Clamp(FMath::Clamp(0.f, PushR, PushL), X0, X1);
		if (FVector2D::Distance(Feet, CamLastPlayer) > CamJump) { bCamSnap = true; }
		CamLastPlayer = Feet;
		float X = float(CamSmooth.X);
		if (bCamSnap) { X = TargetX; }
		else if (Dt > 0.f)
		{
			X += (TargetX - X) * (1.f - FMath::Exp(-CamEase * FMath::Min(Dt, 0.1f)));
			if (FMath::Abs(TargetX - X) < 0.3f) { X = TargetX; }
		}
		// the lagging camera never lets the feet leave the screen sideways; the scroll range is applied last and wins
		const float LoX = float(Feet.X) + 16.f - FV::VW, HiX = FMath::Max(LoX, float(Feet.X) - 16.f);
		X = FMath::Clamp(FMath::Clamp(X, LoX, HiX), X0, X1);
		CamSmooth = FVector2D(X, 0.f);
		bCamSnap = false;
		Cam = FVector2D(FMath::RoundToFloat(X), 0.f);
		return;
	}
	// the player is kept in the band between the HUD strip and the bottom strip / button dock
	const float BandTop = FV::TopBarH, BandH = FMath::Max(40.f, FV::VH - FV::TopBarH - FV::BottomBarH);
	// the walkable extent the edge lean works with (0.6 fix: «the camera breaks on the City pier / the path to the
	// adit»). X: the whole walkable box — the extent along the player's row made a narrow strip (the pier, the berth
	// jetty, the adit path, the Cave planks, the forest path to the mill, the house passages) a «walkable end» on both
	// sides, and the lean threw the camera 1000+ px sideways the moment the player stepped onto it. Y: along the
	// player's column, a neighbouring column (a pier, a path) fading in over ColBlend px of X instead of switching on
	// at its edge (the camera lurched when the player walked past the pier): the lean is continuous in position.
	constexpr float ColBlend = 64.f;
	const float WX0 = L.WalkMinX, WX1 = L.WalkMaxX;
	float WY0 = float(Feet.Y), WY1 = float(Feet.Y);
	for (const FFVRect& R : L.WalkRects)
	{
		const float Dx = FMath::Max3(0.f, R.X - float(Feet.X), float(Feet.X) - R.Right());
		const float Wt = FMath::Clamp(1.f - Dx / ColBlend, 0.f, 1.f);
		if (Wt <= 0.f) { continue; }
		WY0 = FMath::Min(WY0, FMath::Lerp(float(Feet.Y), R.Y, Wt));
		WY1 = FMath::Max(WY1, FMath::Lerp(float(Feet.Y), R.Bottom(), Wt));
	}
	const float FocusX = float(Feet.X), FocusY = float(Feet.Y) - HeadLift;
	const float LeanX = WX1 > WX0 ? EdgeLean(float(L.WorldSize.X), FV::VW, FocusX, WX0, WX1) : 0.f;
	float LeanY = WY1 > WY0 ? EdgeLean(float(L.WorldSize.Y), BandH, FocusY, WY0 - HeadLift, WY1 - HeadLift) : 0.f;
	// integration: the lean never pushes the player out of the band (the Mill's shore: the head went under the top bar
	// while fishing) — the whole body (~60 px with the hat, the focus 22 px above the feet) keeps a margin inside it
	LeanY = FMath::Clamp(LeanY, -FMath::Max(0.f, BandH * 0.5f - HeadLift - 10.f), FMath::Max(0.f, BandH * 0.5f - (60.f - HeadLift) - 6.f));
	FVector2D Target(
		AxisCam(float(L.WorldSize.X), 0.f, FV::VW, FocusX + LeanX),
		AxisCam(float(L.WorldSize.Y), BandTop, BandH, FocusY + LeanY));
	// integration (§12.1): the HUD button column at the top-right (portrait: one 40 px column, landscape: two 28 px ones)
	// must not cover the player. The camera never leaves the world (no voids, §2), so near the right edge it lowers the
	// player on the screen instead (the view slides up) until the head clears the column; ramped over 32 px of X.
	if (float(L.WorldSize.Y) > BandH)
	{
		const bool bPort = FV::IsPortrait();
		const float ColW = bPort ? 4.f + 40.f + 6.f : 4.f + 28.f + 6.f + 28.f + 6.f;
		const float ColBottom = bPort ? FV::TopBarH + 6.f + 4.f * 40.f + 3.f * 4.f + 4.f : FV::TopBarH + 5.f + 5.f * 28.f + 4.f * 3.f + 4.f;
		const float BodyHalfW = 10.f, BodyH = 60.f;
		const float RightScrX = float(Feet.X) + BodyHalfW - float(Target.X);
		const float K = FMath::Clamp((RightScrX - (FV::VW - ColW - 32.f)) / 32.f, 0.f, 1.f);
		const float WantY = float(Feet.Y) - BodyH - ColBottom;               // camera Y that puts the head under the column
		const float MaxFeetY = BandTop + BandH - 4.f;                        // the feet stay above the bottom dock
		const float LowY = FMath::Max(-BandTop, float(Feet.Y) - MaxFeetY);  // world top clamp / the dock
		if (K > 0.f && WantY < float(Target.Y))
		{
			Target.Y = FMath::Lerp(float(Target.Y), FMath::Max(WantY, LowY), K);
		}
	}
	if (FVector2D::Distance(Feet, CamLastPlayer) > CamJump) { bCamSnap = true; }
	CamLastPlayer = Feet;
	if (bCamSnap)
	{
		CamSmooth = Target;
	}
	else if (Dt > 0.f)
	{
		CamSmooth += (Target - CamSmooth) * (1.f - FMath::Exp(-CamEase * FMath::Min(Dt, 0.1f)));
		if (FVector2D::Distance(CamSmooth, Target) < 0.3f) { CamSmooth = Target; }
	}
	// 0.6: the eased camera never lets the player's body leave the band under the HUD strip / above the bottom dock, nor
	// the screen sideways: a lean that ramps faster than the walk (the Cave shore in landscape) made the lagging camera
	// push the head under the top bar. The world clamp (no voids) is applied last and wins.
	{
		const float BodyH = 62.f;
		const float LoY = float(Feet.Y) - BandTop - BandH + 4.f, HiY = FMath::Max(LoY, float(Feet.Y) - BodyH - BandTop);
		CamSmooth.Y = FMath::Clamp(float(CamSmooth.Y), LoY, HiY);
		const float LoX = float(Feet.X) + 16.f - FV::VW, HiX = FMath::Max(LoX, float(Feet.X) - 16.f);
		CamSmooth.X = FMath::Clamp(float(CamSmooth.X), LoX, HiX);
		if (float(L.WorldSize.Y) > BandH) { CamSmooth.Y = FMath::Clamp(float(CamSmooth.Y), -BandTop, float(L.WorldSize.Y) - BandTop - BandH); }
		else { CamSmooth.Y = Target.Y; }
		if (float(L.WorldSize.X) > FV::VW) { CamSmooth.X = FMath::Clamp(float(CamSmooth.X), 0.f, float(L.WorldSize.X) - FV::VW); }
		else { CamSmooth.X = Target.X; }
	}
	bCamSnap = false;
	Cam = FVector2D(FMath::RoundToFloat(float(CamSmooth.X)), FMath::RoundToFloat(float(CamSmooth.Y)));
}

// ============================================================================================
// FVScene: camera API
// ============================================================================================
bool FVScene::IsBigWorld() const
{
	return Impl.IsValid() && Impl->bBig;
}

bool FVScene::HasCamera() const
{
	return Impl.IsValid() && Impl->HasCamera();
}

FVector2D FVScene::GetCamera() const
{
	return (Impl.IsValid() && Impl->HasCamera()) ? Impl->Cam : FVector2D::ZeroVector;
}

FFVRect FVScene::GetWorldBounds() const
{
	if (Impl.IsValid() && Impl->bBig)
	{
		return FFVRect(0.f, 0.f, float(Impl->L.WorldSize.X), float(Impl->L.WorldSize.Y));
	}
	if (Impl.IsValid() && Impl->HasCamera())
	{
		// hotfix 0.61: a scrolling classic place (the pond) — its scroll range (content + HUD pad) x the canvas height
		const FFVRect& R = Impl->L.ScrollRect;
		return FFVRect(float(R.X), 0.f, float(R.W), FV::VH);
	}
	return FFVRect(0.f, 0.f, FV::VW, FV::VH);
}

void FVScene::BeginWorldView(FVDraw& D)
{
	if (!Impl.IsValid() || (!Impl->bBig && !Impl->Loc))
	{
		return;
	}
	// a canvas change re-centres the camera before this frame's translate (hotfix 0.61: a classic layout is rebuilt
	// here too — the pond may start / stop scrolling with the orientation; DrawScene would sync it a moment later anyway)
	Impl->SyncCanvas(*this);
	if (!Impl->HasCamera())
	{
		return;
	}
	D.PushTranslate(-Impl->Cam);
	++Impl->WorldViewDepth;
}

void FVScene::EndWorldView(FVDraw& D)
{
	if (!Impl.IsValid() || Impl->WorldViewDepth <= 0)
	{
		return;
	}
	D.PopTranslate();
	--Impl->WorldViewDepth;
}

void FVScene::SnapCamera()
{
	if (Impl.IsValid())
	{
		Impl->bCamSnap = true;
		Impl->UpdateCamera(*this, 0.f);
	}
}

void FVScene::LoadLocationAt(FName InLocationId, FName Entry)
{
	if (Impl.IsValid())
	{
		Impl->PendingEntry = Entry;   // LoadLocation puts the player at this entry of a big world
	}
	LoadLocation(InLocationId);
	if (Impl.IsValid())
	{
		Impl->PendingEntry = NAME_None;
	}
}

bool FVScene::GetPendingExit(FName& OutLocation, FName& OutEntry) const
{
	if (!Impl.IsValid() || !Impl->bExitPending)
	{
		return false;
	}
	OutLocation = Impl->ExitLocation;
	OutEntry = Impl->ExitEntry;
	return true;
}

bool FVScene::DebugFindExit(FName Target, FFVRect& OutHit, FVector2D& OutFeet, FVector2D& OutSignFeet) const
{
	if (!Impl.IsValid()) { return false; }
	const TArray<FVWorldU6::FAnchor>* ExitList = (Impl->bBig && Impl->BigDef.IsValid()) ? &Impl->BigDef->Anchors : (Impl->bBig ? nullptr : &Impl->WalkExits);
	if (!ExitList) { return false; }
	for (const FVWorldU6::FAnchor& A : *ExitList)
	{
		if (A.Kind != FVWorldU6::EAnchor::Exit || A.Target != Target) { continue; }
		OutHit = A.Hit.W > 0.f ? A.Hit : FFVRect(float(A.Feet.X) - 20.f, float(A.Feet.Y) - 20.f, 40.f, 40.f);
		OutFeet = A.Feet;
		OutSignFeet = A.SignFeet;
		return true;
	}
	return false;
}

void FVScene::ClearPendingExit()
{
	if (Impl.IsValid())
	{
		Impl->bExitPending = false;
		Impl->ExitLocation = NAME_None;
		Impl->ExitEntry = NAME_None;
	}
}

float FVScene::GetDarkRadius() const
{
	return (Impl.IsValid() && Impl->bBig && Impl->BigDef.IsValid()) ? FMath::Max(0.f, Impl->BigDef->DarkRadius) : 0.f;
}

float FVScene::GetWaterBottomY() const
{
	if (Impl.IsValid() && Impl->bBig)
	{
		// big worlds with water (City pier, Cave lake): the bottom of their WaterArea; dry worlds: the world's bottom
		const FFVRect& WA = Impl->L.WaterArea;
		return WA.W > 0.f ? WA.Bottom() : float(Impl->L.WorldSize.Y);
	}
	return FV::VH - FV::BottomBarH;
}

// ============================================================================================
// FVWorldU6: dispatch + the placeholder world
// ============================================================================================
bool FVWorldU6::IsBigWorldId(FName LocationId)
{
	if (LocationId == FVDBU6::ForestId() || LocationId == FVDBU6::CityId() || LocationId == FVDBU6::CaveId()
		|| FVMill::IsMillId(LocationId))   // update 6 (§18): «Старая мельница»
	{
		return true;
	}
	// update 6 integration: the big yard / house are on by default; -fvsmallyard is the fallback switch to the old screens
	static const bool bSmallYard = FParse::Param(FCommandLine::Get(), TEXT("fvsmallyard"));
	return !bSmallYard && (LocationId == FVDB::HomeId() || LocationId == FVDB::HouseId());
}

bool FVWorldU6::BuildWorld(FName LocationId, FFVImage& OutBg, FWorldDef& OutDef)
{
	if (LocationId == FVDBU6::ForestId()) { BuildForest(OutBg, OutDef); return true; }
	if (LocationId == FVDB::HomeId()) { BuildYard(OutBg, OutDef); return true; }
	if (LocationId == FVDB::HouseId()) { BuildHouse(OutBg, OutDef); return true; }
	if (LocationId == FVDBU6::CityId()) { BuildCity(OutBg, OutDef); return true; }
	if (LocationId == FVDBU6::CaveId()) { BuildCave(OutBg, OutDef); return true; }
	if (FVMill::IsMillId(LocationId)) { BuildMill(OutBg, OutDef); return true; }
	return false;
}

void FVWorldU6::MakePlaceholderWorld(FName LocationId, FIntPoint SizeTex, FColor Ground, FFVImage& OutBg, FWorldDef& OutDef)
{
	SizeTex.X = FMath::Clamp(SizeTex.X, 64, 4096);
	SizeTex.Y = FMath::Clamp(SizeTex.Y, 64, 2048);
	OutBg.Init(SizeTex.X, SizeTex.Y, Ground);
	const FColor Dark = FVArt::Shade(Ground, 0.15f);
	for (int32 Y = 0; Y < SizeTex.Y; ++Y)
	{
		for (int32 X = 0; X < SizeTex.X; ++X)
		{
			if (((X / 16) + (Y / 16)) % 2 == 1)
			{
				OutBg.Set(X, Y, Dark);
			}
		}
	}
	const FColor Edge = FVArt::Shade(Ground, 0.5f);
	OutBg.FillRect(0, 0, SizeTex.X, 3, Edge);
	OutBg.FillRect(0, SizeTex.Y - 3, SizeTex.X, 3, Edge);
	OutBg.FillRect(0, 0, 3, SizeTex.Y, Edge);
	OutBg.FillRect(SizeTex.X - 3, 0, 3, SizeTex.Y, Edge);

	OutDef = FWorldDef();
	OutDef.LocationId = LocationId;
	OutDef.SizeTex = SizeTex;
	const float WSc = FV::WorldScale;
	OutDef.Walk.Add(FFVRect(12.f, 12.f, SizeTex.X * WSc - 24.f, SizeTex.Y * WSc - 24.f));
	FAnchor Spawn;
	Spawn.Kind = EAnchor::Spawn;
	Spawn.Id = FName(TEXT("default"));
	Spawn.Feet = FVector2D(SizeTex.X * WSc * 0.5f, SizeTex.Y * WSc * 0.5f);
	OutDef.Anchors.Add(Spawn);
}
