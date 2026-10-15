// UPDATE 6 (Р61) — «Примерка»: a real fragment of the yard / house around the slot of a decor item (see FVDecorPreview.h).
// Mirrors the scene: background chunks = FVSceneCamera.cpp (ChunkTex 256, key WorldBg/hash(id)/k/1, same crops), interior
// back = FVScene::FImpl::DrawInteriorBack, the decor / stations / yard buildings = FVSceneHome.cpp (interior section) and
// FVSceneU6Objects.cpp (DecorSlot, BuildPlot, GardenBed, Station); layers 2 -> 0 -> 3 (y-sorted), shadows, frames, lights.

#include "FVDecorPreview.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVWorldU6.h"
#include "FVArtU6.h"
#include "FVSceneArt.h"   // FVSceneArt::BuildDecor (the shadow sprite), FVSceneU6Objects::BedStage
#include "FVArtYardFront.h"

namespace FVDecorPreviewImpl
{
	using FVWorldU6::EAnchor;
	using FVWorldU6::FAnchor;
	using FVWorldU6::FWorldDef;

	constexpr float PWS = FV::WorldScale;   // world px per texel
	constexpr int32 ChunkTex = 256;         // == FVSceneCamera.cpp: the same chunks, crops and cache keys as the scene

	// ------------------------------------------------------------------------------------------ the place (yard / house)
	struct FPlace
	{
		FName Id;
		bool bOk = false;
		bool bFailed = false;
		TSharedPtr<FWorldDef> Def;
		TArray<FIntRect> Chunks;   // texels
		TArray<bool> Empty;        // fully transparent chunk: no texture (the black beyond the world shows)
	};

	FPlace& PlaceOf(bool bYard)
	{
		static FPlace Yard, House;
		return bYard ? Yard : House;
	}

	uint64 ChunkKey(FName Id, int32 K)
	{
		return UFVSprites::MakeKey(FVSpriteCat::WorldBg, GetTypeHash(Id) & 0xFFFFFFu, uint32(K), 1);
	}

	/** The world description (built once) and the textures of its non-empty chunks: the scene's cache entries when it
	    has made them, otherwise made here from a build of the same deterministic image (the scene then reuses them). */
	const FPlace* EnsurePlace(bool bYard)
	{
		FPlace& Pl = PlaceOf(bYard);
		UFVSprites* Spr = UFVSprites::Get();
		if (!Spr || Pl.bFailed) { return Pl.bOk ? &Pl : nullptr; }
		bool bNeedImage = !Pl.bOk;
		for (int32 k = 0; Pl.bOk && k < Pl.Chunks.Num(); ++k)
		{
			if (!Pl.Empty[k] && !Spr->FindCached(ChunkKey(Pl.Id, k))) { bNeedImage = true; break; }
		}
		if (!bNeedImage) { return &Pl; }
		const FName Id = bYard ? FVDB::HomeId() : FVDB::HouseId();
		FFVImage Img;
		TSharedPtr<FWorldDef> Def = MakeShared<FWorldDef>();
		if (!FVWorldU6::BuildWorld(Id, Img, *Def) || !Img.IsValid())
		{
			Pl.bFailed = true;
			return Pl.bOk ? &Pl : nullptr;
		}
		Def->SizeTex = FIntPoint(Img.W, Img.H);   // the image is the truth for the size (as the scene)
		Pl.Id = Id;
		Pl.Def = Def;
		Pl.Chunks.Reset();
		Pl.Empty.Reset();
		const int32 NX = FMath::DivideAndRoundUp(Img.W, ChunkTex);
		const int32 NY = FMath::DivideAndRoundUp(Img.H, ChunkTex);
		for (int32 cy = 0; cy < NY; ++cy)
		{
			for (int32 cx = 0; cx < NX; ++cx)
			{
				const int32 X0 = cx * ChunkTex, Y0 = cy * ChunkTex;
				const int32 CW = FMath::Min(ChunkTex, Img.W - X0), CH = FMath::Min(ChunkTex, Img.H - Y0);
				const int32 K = Pl.Chunks.Add(FIntRect(X0, Y0, X0 + CW, Y0 + CH));
				const uint64 Key = ChunkKey(Id, K);
				if (Spr->FindCached(Key)) { Pl.Empty.Add(false); continue; }
				const FFVImage Part = Img.Cropped(X0, Y0, CW, CH);
				bool bAny = false;
				for (const FColor& C : Part.Px) { if (C.A > 0) { bAny = true; break; } }
				Pl.Empty.Add(!bAny);
				if (bAny) { Spr->AddCached(Key, Part, FString::Printf(TEXT("WorldBg_%s_%d"), *Id.ToString(), K)); }
			}
		}
		Pl.bOk = true;
		return &Pl;
	}

	// ------------------------------------------------------------------------------------------ visuals (like FImpl::Vis)
	struct FPVis
	{
		FFVSprite S;
		FVector2D Pos = FVector2D::ZeroVector;     // world px
		FVector2D Pivot = FVector2D(0.5f, 1.f);
		uint8 Layer = 3;                           // 0 ground, 2 wall, 3 y-sorted
		float SortY = 0.f;
		bool bShadow = false;
		bool bTry = false;                         // the tried item
		TArray<FFVSprite> Frames;
		float Fps = 0.f;
		float Phase = 0.f;
		FVector2D TopLeft() const { return Pos - FVector2D(S.W * Pivot.X, S.H * Pivot.Y) * PWS; }
		FFVRect Rect() const { const FVector2D TL = TopLeft(); return FFVRect(float(TL.X), float(TL.Y), S.W * PWS, S.H * PWS); }
	};
	struct FPLight { FVector2D Pos = FVector2D::ZeroVector; float Radius = 40.f; FColor Color = FColor(255, 196, 110); bool bFlicker = false; };

	struct FBuild
	{
		TArray<FPVis> Vis;
		TArray<FPLight> Lights;
		bool bTryLight = false;

		int32 Add(const FFVSprite& S, const FVector2D& Pos, uint8 Layer, const FVector2D& Pivot, float SortY, bool bShadow, bool bTry)
		{
			FPVis V;
			V.S = S;
			V.Pos = Pos;
			V.Layer = Layer;
			V.Pivot = Pivot;
			V.SortY = SortY;
			V.bShadow = bShadow;
			V.bTry = bTry;
			V.Phase = float(Vis.Num()) * 1.37f;
			return Vis.Add(V);
		}
		void Light(const FVector2D& Pos, float Radius, const FColor& C, bool bFlicker, bool bTry)
		{
			FPLight L;
			L.Pos = Pos;
			L.Radius = Radius;
			L.Color = C;
			L.bFlicker = bFlicker;
			Lights.Add(L);
			bTryLight |= bTry;
		}
	};

	FVector2D SpriteSize(const FFVSprite& S, float FallbackW, float FallbackH)
	{
		return (S.IsValid() && S.W > 0.f && S.H > 0.f) ? FVector2D(S.W, S.H) : FVector2D(FallbackW, FallbackH);
	}

	FFVRect UnionR(const FFVRect& A, const FFVRect& B)
	{
		if (A.W <= 0.f || A.H <= 0.f) { return B; }
		if (B.W <= 0.f || B.H <= 0.f) { return A; }
		const float X0 = FMath::Min(A.X, B.X), Y0 = FMath::Min(A.Y, B.Y);
		return FFVRect(X0, Y0, FMath::Max(A.Right(), B.Right()) - X0, FMath::Max(A.Bottom(), B.Bottom()) - Y0);
	}

	FFVRect GrowR(const FFVRect& R, float L, float T, float Rt, float B) { return FFVRect(R.X - L, R.Y - T, R.W + L + Rt, R.H + T + B); }

	bool YardFromId(FName Id, EFVYardBuilding& Out)
	{
		for (int32 b = 0; b < int32(EFVYardBuilding::Count); ++b)
		{
			if (FVDBU6::YardBuildingId(EFVYardBuilding(b)) == Id) { Out = EFVYardBuilding(b); return true; }
		}
		return false;
	}

	bool StationFromId(FName Id, EFVStation& Out)
	{
		for (int32 s = 0; s < int32(EFVStation::Count); ++s)
		{
			if (FVDB::StationId(EFVStation(s)) == Id) { Out = EFVStation(s); return true; }
		}
		return false;
	}

	// FVSceneU6Objects.cpp: flat / wall-mounted / skin slots
	bool IsFlatDecor(EFVDecorSlot S) { return S == EFVDecorSlot::YardPath; }
	bool IsWallDecor(EFVDecorSlot S)
	{
		return S == EFVDecorSlot::YardRoof || S == EFVDecorSlot::YardWalls || S == EFVDecorSlot::YardShutters
			|| S == EFVDecorSlot::Clock || S == EFVDecorSlot::Chandelier || S == EFVDecorSlot::YardVane;
	}
	bool IsSkinDecor(EFVDecorSlot S) { return S == EFVDecorSlot::StoveSkin || S == EFVDecorSlot::BedSkin; }

	/** A decor slot of update 6 at its anchor (FVSceneU6Objects.cpp, case EAnchor::DecorSlot). */
	void AddDecorU6(FBuild& B, UFVSprites& Spr, const FAnchor& A, const FFVDecorDef& DD, bool bTry, bool bYard)
	{
		const EFVDecorSlot Slot = DD.Slot;
		const FFVSprite Sp = Spr.DecorWorld(DD);
		if (!Sp.IsValid()) { return; }
		const FVector2D Sz = SpriteSize(Sp, 16.f, 24.f);
		const FVector2D Feet = A.Feet;
		if (Slot == EFVDecorSlot::YardFence && A.Hit.W > 0.f)
		{
			// a 36-texel section repeated every 30 texels along the Hit run, standing on the Hit's bottom line
			const float Step = 30.f * PWS, SecW = Sz.X * PWS;
			const int32 N = FMath::Clamp(FMath::CeilToInt(FMath::Max(0.f, A.Hit.W - SecW) / Step) + 1, 1, 256);
			for (int32 k = 0; k < N; ++k)
			{
				const float X = FMath::Min(A.Hit.X + Step * float(k), FMath::Max(A.Hit.X, A.Hit.Right() - SecW));
				const FVector2D Pos(FMath::RoundToFloat(X), FMath::RoundToFloat(float(A.Hit.Bottom())));
				B.Add(Sp, Pos, 3, FVector2D(0.f, 1.f), float(Pos.Y), false, bTry);
			}
			return;
		}
		if (Slot == EFVDecorSlot::YardPath && A.Hit.W > 0.f && A.Hit.H > 0.f && Sp.W > 0.f && Sp.H > 0.f)
		{
			// a seamless tile, pivot top-left, tiled over the Hit; the edge tiles are cropped through the UVs
			const float TW = Sp.W * PWS, TH = Sp.H * PWS;
			const FVector2D UVSpan = Sp.UV1 - Sp.UV0;
			for (float Y = A.Hit.Y; Y < A.Hit.Bottom() - 0.5f; Y += TH)
			{
				for (float X = A.Hit.X; X < A.Hit.Right() - 0.5f; X += TW)
				{
					const float CW = FMath::Min(TW, A.Hit.Right() - X), CH = FMath::Min(TH, A.Hit.Bottom() - Y);
					FFVSprite T = Sp;
					T.W = Sp.W * CW / TW;
					T.H = Sp.H * CH / TH;
					T.UV1 = Sp.UV0 + FVector2D(UVSpan.X * CW / TW, UVSpan.Y * CH / TH);
					B.Add(T, FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), 0, FVector2D(0.f, 0.f), Y, false, bTry);
				}
			}
			return;
		}
		int32 DV = INDEX_NONE;
		const float CottageY = bYard ? float(FVArtYardFront::CottageFeet().Y) : 0.f;
		if (IsFlatDecor(Slot)) { DV = B.Add(Sp, Feet, 0, FVector2D(0.5f, 0.5f), float(Feet.Y), false, bTry); }
		else if (bYard && Slot == EFVDecorSlot::YardVane)
		{
			DV = B.Add(Sp, Feet, 3, FVector2D(0.5f, 1.f), CottageY + 0.4f, false, bTry);   // on the ridge, after the redrawn roof
		}
		else if (IsWallDecor(Slot))
		{
			DV = B.Add(Sp, Feet, 2, Slot == EFVDecorSlot::Chandelier ? FVector2D(0.5f, 0.f) : FVector2D(0.5f, 1.f), float(Feet.Y), false, bTry);
			// the facade overlays share the cottage canvas: redrawn with its front parts right after it (as the scene)
			const bool bFacade = Slot == EFVDecorSlot::YardWalls || Slot == EFVDecorSlot::YardShutters || Slot == EFVDecorSlot::YardRoof;
			if (bYard && bFacade && Sp.W > 0.f && Sp.H > 0.f && FVector2D::Distance(Feet, FVArtYardFront::CottageFeet()) < 1.f)
			{
				const float Order = Slot == EFVDecorSlot::YardWalls ? 0.1f : (Slot == EFVDecorSlot::YardShutters ? 0.2f : 0.3f);
				const FVector2D TL = Feet - FVector2D(Sp.W * 0.5f, Sp.H) * PWS;
				const FVector2D UVSpan = Sp.UV1 - Sp.UV0;
				for (const FIntRect& CR : FVArtYardFront::CottageRects())
				{
					const float X0 = FMath::Clamp(float(CR.Min.X), 0.f, Sp.W), X1 = FMath::Clamp(float(CR.Max.X), X0, Sp.W);
					const float Y0 = FMath::Clamp(float(CR.Min.Y), 0.f, Sp.H), Y1 = FMath::Clamp(float(CR.Max.Y), Y0, Sp.H);
					if (X1 <= X0 || Y1 <= Y0) { continue; }
					FFVSprite T = Sp;
					T.W = X1 - X0;
					T.H = Y1 - Y0;
					T.UV0 = Sp.UV0 + FVector2D(UVSpan.X * X0 / Sp.W, UVSpan.Y * Y0 / Sp.H);
					T.UV1 = Sp.UV0 + FVector2D(UVSpan.X * X1 / Sp.W, UVSpan.Y * Y1 / Sp.H);
					B.Add(T, TL + FVector2D(X0, Y0) * PWS, 3, FVector2D(0.f, 0.f), CottageY + Order, false, false);
				}
			}
		}
		else { DV = B.Add(Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y) + (IsSkinDecor(Slot) ? 1.f : 0.f), true, bTry); }
		// animation frames (FVArtDecorU6::WorldFrames / BuildWorldFrame), the scene's cache keys
		const int32 NF = FMath::Clamp(FVArtDecorU6::WorldFrames(DD), 1, 4);
		if (NF > 1)
		{
			FPVis& V = B.Vis[DV];
			V.Frames.Add(Sp);
			const uint32 LookA = HashCombine(GetTypeHash(DD.Id), uint32(DD.Style));
			const uint32 LookB = HashCombine(DD.Primary.DWColor(), DD.Secondary.DWColor());
			for (int32 f = 1; f < NF; ++f)
			{
				const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::DecorU6, LookA, LookB, uint8(0x40 + f));
				const FFVSprite* Fr = Spr.FindCached(Key);
				if (!Fr)
				{
					const FFVImage Img = FVArtDecorU6::BuildWorldFrame(DD, f);
					if (!Img.IsValid() || float(Img.W) != Sp.W || float(Img.H) != Sp.H) { break; }
					Fr = &Spr.AddCached(Key, Img, FString::Printf(TEXT("DecorWorld_%s_f%d"), *DD.Id.ToString(), f));
				}
				V.Frames.Add(*Fr);
			}
			if (V.Frames.Num() > 1) { V.Fps = 5.f; }
			else { V.Frames.Reset(); }
		}
		if (Slot == EFVDecorSlot::YardLantern || Slot == EFVDecorSlot::Fireplace || Slot == EFVDecorSlot::Chandelier)
		{
			FVector2D LPos = Slot == EFVDecorSlot::Chandelier ? Feet + FVector2D(0.f, Sz.Y * PWS * 0.6f)
				: Feet - FVector2D(0.f, Sz.Y * PWS * (Slot == EFVDecorSlot::Fireplace ? 0.35f : 0.8f));
			const FVector2D LP = FVArtDecorU6::LightPoint(DD);
			if (LP.X >= 0.f && LP.Y >= 0.f) { LPos = B.Vis[DV].TopLeft() + LP * PWS; }
			B.Light(LPos, Slot == EFVDecorSlot::Chandelier ? 90.f : 60.f, DD.Secondary, Slot != EFVDecorSlot::Chandelier, bTry);
		}
	}

	/** Everything the scene draws at the anchors of the place, with Try in its slot instead of the equipped item. */
	void BuildVisuals(FBuild& B, const FWorldDef& Def, bool bYard, const FFVDecorDef& Try, const UFVSaveGame* P)
	{
		UFVSprites* Spr = UFVSprites::Get();
		if (!Spr) { return; }
		auto Pick = [&Try, P](EFVDecorSlot S) -> const FFVDecorDef*
		{
			if (S == Try.Slot) { return &Try; }
			return P ? FVDB::FindDecor(P->GetEquippedDecor(S)) : nullptr;
		};
		// the yard background's props in front of the back fence, redrawn y-sorted (as the scene: FVArtYardFront.h)
		if (bYard)
		{
			for (const FVArtYardFront::FPart& Pt : FVArtYardFront::Parts())
			{
				B.Add(Pt.S, Pt.TopLeft, 3, FVector2D(0.f, 0.f), Pt.SortY, false, false);
			}
		}
		for (const FAnchor& A : Def.Anchors)
		{
			const FVector2D Feet = A.Feet;
			switch (A.Kind)
			{
			case EAnchor::DecorSlot:
			{
				if (A.Index < 0 || A.Index >= int32(EFVDecorSlot::Count)) { break; }
				const EFVDecorSlot Slot = EFVDecorSlot(A.Index);
				const FFVDecorDef* DD = Pick(Slot);
				if (!DD) { break; }
				const bool bTry = Slot == Try.Slot;
				if (A.Index >= FVDecor::ClassicSlots) { AddDecorU6(B, *Spr, A, *DD, bTry, bYard); break; }
				// the classic room decor (FVSceneHome.cpp, interior section)
				const FFVSprite S = Spr->DecorWorld(*DD);
				if (!S.IsValid()) { break; }
				switch (Slot)
				{
				case EFVDecorSlot::Rug:      B.Add(S, Feet, 0, FVector2D(0.5f, 0.5f), float(Feet.Y), false, bTry); break;
				case EFVDecorSlot::Painting: B.Add(S, Feet, 2, FVector2D(0.5f, 0.5f), float(Feet.Y), false, bTry); break;
				case EFVDecorSlot::Curtains: B.Add(S, Feet, 2, FVector2D(0.5f, 1.f), float(Feet.Y), false, bTry); break;
				case EFVDecorSlot::Plant:    B.Add(S, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y), true, bTry); break;
				case EFVDecorSlot::Lamp:
				{
					B.Add(S, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y), true, bTry);
					const FVector2D LSz = SpriteSize(S, 12.f, 24.f);
					B.Light(Feet - FVector2D(0.f, LSz.Y * PWS * 0.78f), 64.f + 10.f * float(DD->Style), DD->Secondary, DD->Style <= 1, bTry);
					break;
				}
				default: break;
				}
				break;
			}
			case EAnchor::Station:
			{
				if (!bYard && A.Id == FName(TEXT("dresser")))
				{
					B.Add(Spr->DresserSprite(), Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y), true, false);
					break;
				}
				EFVStation St;
				if (!StationFromId(A.Id, St)) { break; }
				const int32 Level = P ? FMath::Max(0, P->GetStationLevel(St)) : 0;
				if (bYard)
				{
					B.Add(Spr->YardStationSprite(St, Level), Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y), true, false);
					break;
				}
				const FFVSprite S = Spr->StationSprite(St, Level);
				if (St == EFVStation::TrophyWall) { B.Add(S, Feet, 2, FVector2D(0.5f, 0.5f), float(Feet.Y), false, false); break; }
				if (St == EFVStation::IceCellar) { B.Add(S, Feet, 0, FVector2D(0.5f, 1.f), float(Feet.Y), false, false); break; }
				B.Add(S, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y), true, false);
				if (St == EFVStation::Kitchen && Level > 0)
				{
					const FVector2D Sz = SpriteSize(S, 32.f, 32.f);
					B.Light(Feet - FVector2D(0.f, Sz.Y * PWS * 0.4f), 50.f + 8.f * float(Level), FColor(255, 150, 60), true, false);
				}
				break;
			}
			case EAnchor::BuildPlot:
			{
				EFVYardBuilding YB;
				if (!bYard || !YardFromId(A.Id, YB)) { break; }
				const int32 Level = P ? FMath::Max(0, P->GetYardLevel(YB)) : 0;
				const FFVSprite S = Spr->YardBuildingSprite(YB, Level);
				B.Add(S, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y), Level > 0, false);
				if (YB == EFVYardBuilding::Forge && Level > 0)
				{
					const FVector2D Sz = SpriteSize(S, 72.f, 56.f);
					const FVector2D Ep = UFVSprites::YardEffectPoint(YB, Level);
					const FVector2D TL = Feet - FVector2D(Sz.X * PWS * 0.5f, Sz.Y * PWS);
					B.Light((Ep.X >= 0.f && Ep.Y >= 0.f) ? TL + Ep * PWS : Feet - FVector2D(0.f, Sz.Y * PWS * 0.35f), 46.f + 8.f * float(Level), FColor(255, 128, 48), true, false);
				}
				break;
			}
			case EAnchor::GardenBed:
			{
				const int32 Bed = A.Index;
				if (!bYard || !P || Bed < 0 || Bed >= P->GardenBeds()) { break; }
				const int32 Stage = FVSceneU6Objects::BedStage(P, Bed);
				FName Crop;
				if (Stage >= 0 && P->U6.Garden.IsValidIndex(Bed))
				{
					const FFVSeedDef* SD = FVDBU6::FindSeed(P->U6.Garden[Bed].Seed);
					Crop = SD ? SD->CropId : P->U6.Garden[Bed].Seed;
				}
				B.Add(Spr->GardenBedSprite(Crop, FMath::Max(0, Stage)), Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y) - 2.f, false, false);
				break;
			}
			default: break;
			}
		}
	}

	/** Room of the big house at a world point (FVArtHouse.cpp: FloorAreas = горница, кухня, сени; the rest = мастерская). */
	FString RoomAt(const FWorldDef& Def, const FVector2D& P)
	{
		static const TCHAR* Names[3] = { TEXT("горница"), TEXT("кухня"), TEXT("сени") };
		int32 Best = INDEX_NONE;
		for (int32 i = 0; i < Def.FloorAreas.Num() && i < 3; ++i)
		{
			const FFVRect& A = Def.FloorAreas[i];
			if (P.X < A.X - 8.f || P.X > A.Right() + 8.f || P.Y > A.Bottom() + 4.f) { continue; }
			if (Best == INDEX_NONE || A.Bottom() < Def.FloorAreas[Best].Bottom()) { Best = i; }
		}
		return Best == INDEX_NONE ? FString(TEXT("мастерская")) : FString(Names[Best]);
	}

	const FAnchor* FirstAnchor(const FWorldDef& Def, EAnchor Kind, int32 Index = INDEX_NONE)
	{
		for (const FAnchor& A : Def.Anchors)
		{
			if (A.Kind == Kind && (Index == INDEX_NONE || A.Index == Index)) { return &A; }
		}
		return nullptr;
	}

	FString YardPlace(EFVDecorSlot S)
	{
		switch (S)
		{
		case EFVDecorSlot::YardRoof:      return TEXT("крыша дома");
		case EFVDecorSlot::YardWalls:     return TEXT("стены дома");
		case EFVDecorSlot::YardShutters:  return TEXT("окна дома");
		case EFVDecorSlot::YardFence:     return TEXT("забор");
		case EFVDecorSlot::YardPath:      return TEXT("дорожка к крыльцу");
		case EFVDecorSlot::YardLantern:   return TEXT("у крыльца");
		case EFVDecorSlot::YardMailbox:   return TEXT("у калитки");
		case EFVDecorSlot::YardBench:     return TEXT("у дома");
		case EFVDecorSlot::YardFlowerbed: return TEXT("у крыльца");
		case EFVDecorSlot::YardScarecrow: return TEXT("у огорода");
		case EFVDecorSlot::YardWell:      return TEXT("место колодца");
		case EFVDecorSlot::YardVane:      return TEXT("конёк крыши");
		case EFVDecorSlot::YardCenter:    return TEXT("у пруда");
		default:                          return FString();
		}
	}

	/** Place of the slot in the house (the room of its first anchor; wallpaper / floor = the first wall / floor area). */
	FString HousePlace(const FWorldDef& Def, EFVDecorSlot S)
	{
		if (S == EFVDecorSlot::Wallpaper && Def.WallAreas.Num() > 0) { return RoomAt(Def, Def.WallAreas[0].Center() + FVector2D(0.f, Def.WallAreas[0].H)); }
		if (S == EFVDecorSlot::Floor && Def.FloorAreas.Num() > 0) { return RoomAt(Def, Def.FloorAreas[0].Center()); }
		const FAnchor* A = FirstAnchor(Def, EAnchor::DecorSlot, int32(S));
		return A ? RoomAt(Def, A->Feet) : FString(TEXT("комнаты"));
	}

	/** The part of the world the view must show (world px). TryRect = union of the tried item's sprites. */
	FFVRect FocusRect(const FWorldDef& Def, const FFVDecorDef& Try, const FFVRect& TryRect)
	{
		const float T = PWS;   // one texel in world px
		const EFVDecorSlot S = Try.Slot;
		const FAnchor* Own = FirstAnchor(Def, EAnchor::DecorSlot, int32(S));
		const FAnchor* Door = FirstAnchor(Def, EAnchor::Door);
		switch (S)
		{
		case EFVDecorSlot::Wallpaper:
			if (Def.WallAreas.Num() > 0)
			{
				const FFVRect& A = Def.WallAreas[0];
				return GrowR(FFVRect(A.X, A.Y, FMath::Min(A.W, 150.f * T), A.H), 8.f * T, 8.f * T, 0.f, 34.f * T);
			}
			break;
		case EFVDecorSlot::Floor:
			if (Def.FloorAreas.Num() > 0)
			{
				const FFVRect& A = Def.FloorAreas[0];
				return GrowR(FFVRect(A.X, A.Y, FMath::Min(A.W, 150.f * T), A.H), 8.f * T, 24.f * T, 0.f, 4.f * T);
			}
			break;
		case EFVDecorSlot::YardFence:
			if (Own && Own->Hit.W > 0.f)
			{
				// a stretch of the fence ending at the house door: the fence with the yard in front and the cottage at the side
				const FFVRect& H = Own->Hit;
				const float Len = FMath::Min(H.W, 124.f * T);
				const float DoorX = Door ? float(Door->Feet.X) : H.Center().X;
				const float X1 = FMath::Clamp(DoorX, H.X + Len, H.Right());
				const float SecH = TryRect.H > 0.f ? FMath::Min(TryRect.H, 40.f * T) : 24.f * T;
				return FFVRect(X1 - Len, H.Bottom() - SecH - 10.f * T, Len, SecH + 10.f * T + 30.f * T);
			}
			break;
		case EFVDecorSlot::YardPath:
			if (Own && Own->Hit.W > 0.f && Own->Hit.H > 0.f)
			{
				// the first stretch from the porch
				const FFVRect& H = Own->Hit;
				const FVector2D DoorP = Door ? Door->Feet : H.Center();
				if (H.H >= H.W)
				{
					const float Len = FMath::Min(H.H, 80.f * T);
					const float Y0 = FMath::Clamp(float(DoorP.Y), H.Y, H.Bottom() - Len);
					return FFVRect(H.X - 36.f * T, Y0 - 30.f * T, H.W + 72.f * T, Len + 30.f * T);
				}
				const float Len = FMath::Min(H.W, 124.f * T);
				const float X0 = FMath::Clamp(float(DoorP.X) - Len * 0.5f, H.X, H.Right() - Len);
				return FFVRect(X0, H.Y - 30.f * T, Len, H.H + 60.f * T);
			}
			break;
		case EFVDecorSlot::YardRoof:
		case EFVDecorSlot::YardWalls:
		case EFVDecorSlot::YardShutters:
			if (TryRect.W > 0.f) { return GrowR(TryRect, 10.f * T, 10.f * T, 10.f * T, 12.f * T); }
			break;
		case EFVDecorSlot::YardVane:
			if (TryRect.W > 0.f)
			{
				// the vane on the ridge: with the whole roof (the facade overlays are 78x62 texels at the roof anchor)
				FFVRect Roof;
				if (const FAnchor* RA = FirstAnchor(Def, EAnchor::DecorSlot, int32(EFVDecorSlot::YardRoof)))
				{
					Roof = FFVRect(float(RA->Feet.X) - 39.f * T, float(RA->Feet.Y) - 62.f * T, 78.f * T, 62.f * T);
				}
				return GrowR(UnionR(TryRect, Roof), 10.f * T, 8.f * T, 10.f * T, 10.f * T);
			}
			break;
		default:
			if (TryRect.W > 0.f)
			{
				const float M = FMath::Clamp(FMath::Max(TryRect.W, TryRect.H) * 0.6f, 24.f * T, 48.f * T);
				return GrowR(TryRect, M, M, M, M * 0.75f);
			}
			break;
		}
		if (Own) { return FFVRect(float(Own->Feet.X) - 60.f * T, float(Own->Feet.Y) - 60.f * T, 120.f * T, 90.f * T); }
		return FFVRect(0.f, 0.f, float(Def.SizeTex.X) * T, float(Def.SizeTex.Y) * T);
	}

	FLinearColor WithA(FLinearColor C, float A) { C.A = FMath::Clamp(A, 0.f, 1.f); return C; }
}

bool FVDecorPreview::IsYardPlace(EFVDecorSlot Slot)
{
	return FVDecor::IsYardSlot(Slot);
}

FString FVDecorPreview::PlaceName(EFVDecorSlot Slot)
{
	using namespace FVDecorPreviewImpl;
	if (IsYardPlace(Slot)) { return FString(TEXT("двор · ")) + YardPlace(Slot); }
	const FPlace& Pl = PlaceOf(false);
	if (Pl.bOk && Pl.Def.IsValid()) { return FString(TEXT("дом · ")) + HousePlace(*Pl.Def, Slot); }
	return FString(TEXT("дом"));
}

void FVDecorPreview::Draw(FVDraw& D, const FFVRect& R, const FFVDecorDef& Def, const UFVSaveGame* P)
{
	using namespace FVDecorPreviewImpl;
	const bool bP = FV::IsPortrait();
	const bool bYard = IsYardPlace(Def.Slot);
	const float T = float(D.Time);

	// ---- frame + caption
	D.Rect(R, FV::Col(70, 40, 22));
	const FFVRect In = R.Inset(2.f);
	const float CapS = bP ? FV::Ui::Small() : 6.5f;
	const float CapH = FMath::RoundToFloat(D.LineHeight(CapS) + (bP ? 4.f : 3.f));
	const FFVRect Cap(In.X, In.Y, In.W, CapH);
	D.Rect(Cap, FV::Col(96, 58, 32));
	D.Rect(FFVRect(Cap.X, Cap.Bottom() - 1.f, Cap.W, 1.f), FV::Col(52, 30, 18));
	const FFVRect View(In.X, Cap.Bottom(), In.W, FMath::Max(8.f, In.Bottom() - Cap.Bottom()));
	const FPlace* Pl = EnsurePlace(bYard);
	const FString Caption = FString(TEXT("Примерка: ")) + PlaceName(Def.Slot);
	D.Text(D.Ellipsize(Caption, Cap.W - 8.f, CapS), FVector2D(Cap.X + 4.f, Cap.Y + FMath::RoundToFloat((CapH - D.LineHeight(CapS)) * 0.5f)),
		CapS, FV::Col(255, 238, 200), EFVAlign::Left, false, true);
	UFVSprites* Spr = UFVSprites::Get();
	if (!Pl || !Pl->Def.IsValid() || !Spr)
	{
		D.Rect(View, FV::Col(20, 14, 10));
		D.Text(TEXT("Примерка недоступна"), View.Center(), CapS, FV::Col(220, 200, 170), EFVAlign::Center, false, false);
		return;
	}
	const FWorldDef& WD = *Pl->Def;

	// ---- what stands there (the scene's rules), the tried item in its slot
	FBuild B;
	BuildVisuals(B, WD, bYard, Def, P);
	FFVRect TryRect;
	for (const FPVis& V : B.Vis) { if (V.bTry && V.S.IsValid()) { TryRect = UnionR(TryRect, V.Rect()); } }
	const FFVRect F = FocusRect(WD, Def, TryRect);

	// ---- integer pixel scale: K whole DEVICE px per texel (nearest textures, no blur), the view snapped to texels
	const FVector2D Dev0 = D.ToScreen(View.Pos());
	const FVector2D Dev1 = D.ToScreen(FVector2D(View.Right(), View.Bottom()));
	const float Wd = FMath::Max(8.f, float(Dev1.X - Dev0.X)), Hd = FMath::Max(8.f, float(Dev1.Y - Dev0.Y));
	const float Fit = FMath::Min(Wd / FMath::Max(8.f, F.W / PWS), Hd / FMath::Max(8.f, F.H / PWS));
	const int32 KMax = FMath::Max(1, FMath::RoundToInt(3.f * D.Scale));   // at most 3 virtual px per texel (1.5x the game)
	const int32 K = FMath::Clamp(FMath::FloorToInt(Fit), 1, KMax);
	const float VWt = Wd / float(K), VHt = Hd / float(K);                // the view in texels
	const FVector2D FC = F.Center() / PWS;
	auto Place1D = [](float Center, float ViewLen, float WorldLen)
	{
		const float V0 = Center - ViewLen * 0.5f;
		return FMath::FloorToFloat(WorldLen > ViewLen ? FMath::Clamp(V0, 0.f, WorldLen - ViewLen) : (WorldLen - ViewLen) * 0.5f);
	};
	const float TX = Place1D(float(FC.X), VWt, float(WD.SizeTex.X));
	const float TY = Place1D(float(FC.Y), VHt, float(WD.SizeTex.Y));
	const FVector2D O(FMath::RoundToFloat(Dev0.X), FMath::RoundToFloat(Dev0.Y));
	const float SprScale = float(K) / FMath::Max(0.01f, D.Scale);   // virtual px per texel
	const float Z = SprScale / PWS;                                   // virtual px per world px
	auto ToV = [&](const FVector2D& W) -> FVector2D
	{
		const FVector2D Dv = O + (W / PWS - FVector2D(TX, TY)) * float(K);
		return D.ToVirtual(FVector2D(FMath::RoundToFloat(Dv.X), FMath::RoundToFloat(Dv.Y)));
	};
	auto RectV = [&](const FFVRect& W) -> FFVRect
	{
		const FVector2D A = ToV(W.Pos()), C = ToV(W.Pos() + W.Size());
		return FFVRect(float(A.X), float(A.Y), float(C.X - A.X), float(C.Y - A.Y));
	};
	const FFVRect ViewTex(TX, TY, VWt, VHt);
	auto Overlaps = [](const FFVRect& A, const FFVRect& C) { return A.X < C.Right() && C.X < A.Right() && A.Y < C.Bottom() && C.Y < A.Bottom(); };

	D.PushClip(View);
	D.Rect(View, FLinearColor::Black);   // beyond the world: black (Р38)

	// ---- house: wallpaper, floor, the sky in the windows (FImpl::DrawInteriorBack, daytime)
	if (!bYard)
	{
		auto Pick = [&Def, P](EFVDecorSlot S) -> const FFVDecorDef*
		{
			if (S == Def.Slot) { return &Def; }
			return P ? FVDB::FindDecor(P->GetEquippedDecor(S)) : nullptr;
		};
		FFVRect WallR, FloorR;
		for (const FFVRect& A : WD.WallAreas) { WallR = UnionR(WallR, A); }
		for (const FFVRect& A : WD.FloorAreas) { FloorR = UnionR(FloorR, A); }
		auto Tiles = [&](const FFVRect& AreaW, const FFVDecorDef* TD, const FColor& Fallback)
		{
			if (AreaW.W <= 0.f || AreaW.H <= 0.f) { return; }
			const FFVSprite Tl = TD ? Spr->DecorWorld(*TD) : FFVSprite();
			if (!Tl.IsValid() || Tl.W <= 0.f || Tl.H <= 0.f)
			{
				D.Rect(RectV(AreaW), FV::Col(TD ? TD->Primary : Fallback));
				return;
			}
			// texel space, tiles aligned to the world origin (as the scene)
			const float VX0 = FMath::Max(AreaW.X / PWS, TX), VY0 = FMath::Max(AreaW.Y / PWS, TY);
			const float VX1 = FMath::Min(AreaW.Right() / PWS, TX + VWt), VY1 = FMath::Min(AreaW.Bottom() / PWS, TY + VHt);
			if (VX1 <= VX0 || VY1 <= VY0) { return; }
			const float TW = Tl.W, TH = Tl.H;
			const FVector2D UVSpan = Tl.UV1 - Tl.UV0;
			for (float Y = FMath::FloorToFloat(VY0 / TH) * TH; Y < VY1; Y += TH)
			{
				for (float X = FMath::FloorToFloat(VX0 / TW) * TW; X < VX1; X += TW)
				{
					const float CX0 = FMath::Max(X, VX0), CY0 = FMath::Max(Y, VY0), CX1 = FMath::Min(X + TW, VX1), CY1 = FMath::Min(Y + TH, VY1);
					if (CX1 <= CX0 || CY1 <= CY0) { continue; }
					const FVector2D U0((CX0 - X) / TW, (CY0 - Y) / TH), U1((CX1 - X) / TW, (CY1 - Y) / TH);
					D.Texture(Tl.Tex, RectV(FFVRect(CX0 * PWS, CY0 * PWS, (CX1 - CX0) * PWS, (CY1 - CY0) * PWS)), Tl.UV0 + UVSpan * U0, Tl.UV0 + UVSpan * U1);
				}
			}
		};
		Tiles(WallR, Pick(EFVDecorSlot::Wallpaper), FColor(170, 120, 70));
		Tiles(FloorR, Pick(EFVDecorSlot::Floor), FColor(150, 100, 60));
		if (WallR.W > 0.f) { D.GradientV(RectV(WallR), FLinearColor(0.08f, 0.04f, 0.02f, 0.22f), FLinearColor(0.08f, 0.04f, 0.02f, 0.f), 12); }
		if (FloorR.W > 0.f) { D.GradientV(RectV(FloorR), FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.06f, 0.03f, 0.02f, 0.16f), 12); }
		for (int32 w = 0; w < WD.Windows.Num(); ++w)
		{
			const FFVRect& G = WD.Windows[w];
			D.GradientV(RectV(G), FV::Col(112, 176, 236), FV::Col(196, 230, 250), 8);
			for (float X = G.X; X < G.Right(); X += 4.f)
			{
				const float Hh = FMath::RoundToFloat((6.f + 4.f * FMath::Sin(X * 0.21f + float(w) * 2.f) + 2.f * FMath::Sin(X * 0.57f)) / 2.f) * 2.f;
				D.Rect(RectV(FFVRect(X, G.Bottom() - Hh, FMath::Min(4.f, G.Right() - X), Hh)), FV::Col(70, 132, 70));
			}
		}
	}

	// ---- the background chunks (the scene's textures)
	for (int32 k = 0; k < Pl->Chunks.Num(); ++k)
	{
		if (Pl->Empty[k]) { continue; }
		const FIntRect& C = Pl->Chunks[k];
		if (!Overlaps(FFVRect(float(C.Min.X), float(C.Min.Y), float(C.Width()), float(C.Height())), ViewTex)) { continue; }
		const FFVSprite* S = Spr->FindCached(ChunkKey(Pl->Id, k));
		if (S && S->IsValid()) { D.Sprite(*S, ToV(FVector2D(float(C.Min.X), float(C.Min.Y)) * PWS), SprScale); }
	}

	// ---- objects: wall layer, ground layer, then y-sorted (shadows under them)
	FFVSprite Shadow;
	{
		const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::Scene, 0xFFF000u + uint32(FVSceneArt::EDecor::Shadow), 0, 2);   // FImpl::EnsureDecor
		const FFVSprite* Found = Spr->FindCached(Key);
		Shadow = Found ? *Found : Spr->AddCached(Key, FVSceneArt::BuildDecor(FVSceneArt::EDecor::Shadow), FString::Printf(TEXT("SceneDecor_%d"), int32(FVSceneArt::EDecor::Shadow)));
	}
	auto DrawVis = [&](const FPVis& V)
	{
		if (!V.S.IsValid()) { return; }
		const FFVRect WR = V.Rect();
		if (!Overlaps(FFVRect(WR.X / PWS - 2.f, WR.Y / PWS - 2.f, WR.W / PWS + 4.f, WR.H / PWS + 4.f), ViewTex)) { return; }
		if (V.bShadow && V.Layer == 3 && Shadow.IsValid())
		{
			D.SpriteCentered(Shadow, ToV(V.Pos + FVector2D(2.f, -1.f)), PWS * FMath::Clamp(V.S.W / 14.f, 0.6f, 2.6f) * Z, FLinearColor(1.f, 1.f, 1.f, 0.8f));
		}
		const FFVSprite* Cur = &V.S;
		if (V.Frames.Num() > 1 && V.Fps > 0.f)
		{
			const FFVSprite& Fr = V.Frames[FMath::Abs(FMath::FloorToInt((T + V.Phase) * V.Fps)) % V.Frames.Num()];
			if (Fr.IsValid()) { Cur = &Fr; }
		}
		D.Sprite(*Cur, ToV(V.TopLeft()), SprScale);
	};
	for (const FPVis& V : B.Vis) { if (V.Layer == 2) { DrawVis(V); } }
	for (const FPVis& V : B.Vis) { if (V.Layer == 0) { DrawVis(V); } }
	{
		TArray<int32> Order;
		for (int32 i = 0; i < B.Vis.Num(); ++i) { if (B.Vis[i].Layer == 3) { Order.Add(i); } }
		Order.StableSort([&B](int32 A, int32 C) { return B.Vis[A].SortY < B.Vis[C].SortY; });
		for (int32 i : Order) { DrawVis(B.Vis[i]); }
	}

	// ---- light: an item with a flame / lamp is shown at dusk with its glow (and the other lights of the fragment)
	if (B.bTryLight)
	{
		D.Rect(View, FLinearColor(0.03f, 0.04f, 0.12f, 0.30f));
		for (int32 k = 0; k < B.Lights.Num(); ++k)
		{
			const FPLight& Lt = B.Lights[k];
			float Int = 0.85f;
			if (Lt.bFlicker) { Int *= 0.85f + 0.15f * FMath::Sin(T * 11.f + float(k) * 7.f) * FMath::Sin(T * 6.3f + float(k)); }
			FLinearColor C = FV::Col(Lt.Color) * (0.55f * Int);
			C.A = Int;
			D.Glow(ToV(Lt.Pos), Lt.Radius * Z * (Lt.bFlicker ? 1.f + 0.04f * FMath::Sin(T * 9.f + float(k)) : 1.f), C);
		}
	}
	else if (!bYard)
	{
		// daylight through the windows
		for (const FFVRect& G : WD.Windows)
		{
			const float I = 0.16f;
			D.Glow(ToV(G.Center()), G.W * 0.9f * Z, FLinearColor(I, I * 0.95f, I * 0.8f, I));
		}
	}

	// ---- the tried item: soft pulsing corner marks (not for tiled surfaces and long runs)
	const EFVDecorSlot S = Def.Slot;
	if (TryRect.W > 0.f && S != EFVDecorSlot::Wallpaper && S != EFVDecorSlot::Floor && S != EFVDecorSlot::YardFence && S != EFVDecorSlot::YardPath)
	{
		const FFVRect M = RectV(TryRect);
		const FFVRect Mk(M.X - 2.f, M.Y - 2.f, M.W + 4.f, M.H + 4.f);
		const float Arm = FMath::RoundToFloat(FMath::Clamp(FMath::Min(Mk.W, Mk.H) * 0.25f, 3.f, 10.f));
		const FLinearColor C = WithA(FV::Col(255, 230, 140), 0.45f + 0.35f * (0.5f + 0.5f * FMath::Sin(T * 4.f)));
		const float Th = 1.f;
		D.Rect(FFVRect(Mk.X, Mk.Y, Arm, Th), C);                       D.Rect(FFVRect(Mk.X, Mk.Y, Th, Arm), C);
		D.Rect(FFVRect(Mk.Right() - Arm, Mk.Y, Arm, Th), C);           D.Rect(FFVRect(Mk.Right() - Th, Mk.Y, Th, Arm), C);
		D.Rect(FFVRect(Mk.X, Mk.Bottom() - Th, Arm, Th), C);           D.Rect(FFVRect(Mk.X, Mk.Bottom() - Arm, Th, Arm), C);
		D.Rect(FFVRect(Mk.Right() - Arm, Mk.Bottom() - Th, Arm, Th), C); D.Rect(FFVRect(Mk.Right() - Th, Mk.Bottom() - Arm, Th, Arm), C);
	}
	D.PopClip();
}
