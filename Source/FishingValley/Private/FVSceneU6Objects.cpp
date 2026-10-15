// UPDATE 6 [SCENE-OBJECTS] — world objects of big locations and the ocean boat (Docs/UPDATE6_FOREST_CITY.md §2, §4,
// §6, §7, §8, §9а): anchors of FVWorldU6::FWorldDef -> FFVWorldObject + drawing (FImpl::Vis) with state from the profile
// (U6.Nodes, YardBuildings, Garden, BoatStage...). Owner: SCENE-OBJECTS (also FVSceneHome.cpp, FVSceneU6.h).
// Called by FImpl::RebuildWorld (FVSceneHome.cpp) after the classic sections, before the reachability pass.

#include "FVScene.h"
#include "FVSceneArt.h"
#include "FVSceneU6.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVArtU6.h"
#include "FVGame.h"
#include "FVArtYardFront.h"

namespace FVSceneU6ObjImpl
{
	using namespace FVSceneRt;
	using FVWorldU6::EAnchor;
	using FVWorldU6::FAnchor;

	FORCEINLINE void Mix(uint32& H, uint32 V) { H = (H ^ V) * 16777619u; }

	FVector2D SpriteSize(const FFVSprite& S, float FallbackW, float FallbackH)
	{
		return (S.IsValid() && S.W > 0.f && S.H > 0.f) ? FVector2D(S.W, S.H) : FVector2D(FallbackW, FallbackH);
	}

	/** Rect of a bottom-centre anchored sprite (Size in texels), optionally narrowed to WFrac of its width. */
	FFVRect FootRect(const FVector2D& Base, const FVector2D& Size, float WFrac = 1.f)
	{
		const float W = Size.X * WS * WFrac, H = Size.Y * WS;
		return FFVRect(float(Base.X) - W * 0.5f, float(Base.Y) - H, W, H);
	}

	/** A foot blocker: WFrac of the sprite width, depth from the sprite height clamped to MinH..MaxH. */
	FFVRect FootBlocker(const FVector2D& Base, const FVector2D& Size, float WFrac, float MinH, float MaxH)
	{
		const float BW = Size.X * WS * WFrac;
		const float BH = FMath::Clamp(Size.Y * WS * 0.3f, MinH, MaxH);
		return FFVRect(float(Base.X) - BW * 0.5f, float(Base.Y) - BH, BW, BH + 2.f);
	}

	FString TreeName(EFVTreeKind K)
	{
		switch (K)
		{
		case EFVTreeKind::Birch: return TEXT("Берёза");
		case EFVTreeKind::Pine: return TEXT("Сосна");
		case EFVTreeKind::Oak: return TEXT("Дуб");
		case EFVTreeKind::Spruce: return TEXT("Вековая ель");
		default: return TEXT("Дерево");
		}
	}

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

	FString CropName(const UFVSaveGame* P, int32 Bed)
	{
		if (!P || !P->U6.Garden.IsValidIndex(Bed)) { return FString(); }
		const FName Seed = P->U6.Garden[Bed].Seed;
		if (Seed.IsNone()) { return FString(); }
		const FFVSeedDef* SD = FVDBU6::FindSeed(Seed);
		return FVDB::ItemName(SD ? SD->CropId : Seed);
	}

	/** Flat decor slots (lying on the ground) and wall-mounted ones (no shadow, no blocker). */
	bool IsFlatDecor(EFVDecorSlot S) { return S == EFVDecorSlot::YardPath; }
	bool IsWallDecor(EFVDecorSlot S)
	{
		return S == EFVDecorSlot::YardRoof || S == EFVDecorSlot::YardWalls || S == EFVDecorSlot::YardShutters
			|| S == EFVDecorSlot::Clock || S == EFVDecorSlot::Chandelier || S == EFVDecorSlot::YardVane;
	}
	bool IsSkinDecor(EFVDecorSlot S) { return S == EFVDecorSlot::StoveSkin || S == EFVDecorSlot::BedSkin; }
}

// ============================================================================================
// Р69: walking between all locations without the map — the graph of the one-screen locations and the open check
// ============================================================================================
namespace FVSceneU6ObjImpl
{
	/** Every place a walk exit can lead to (their open state is hashed into the world signature: the signs change). */
	const TArray<FName>& WalkTargets()
	{
		static const TArray<FName> T = { FName(TEXT("pond")), FName(TEXT("river")), FName(TEXT("mountain")), FName(TEXT("ocean")),
			FName(TEXT("swamp")), FName(TEXT("volcano")), FVDBU6::ForestId(), FVMill::MillId(), FVDBU6::CityId(), FVDBU6::CaveId() };
		return T;
	}
}

FVWorldU6::EWayPicto FVWorldU6::WayPictoFor(FName Target)
{
	const FString S = Target.ToString();
	if (S == TEXT("river")) { return EWayPicto::River; }
	if (S == TEXT("pond")) { return EWayPicto::Pond; }
	if (S == TEXT("mountain")) { return EWayPicto::Mountain; }
	if (S == TEXT("ocean")) { return EWayPicto::Ocean; }
	if (S == TEXT("swamp")) { return EWayPicto::Swamp; }
	if (S == TEXT("volcano")) { return EWayPicto::Volcano; }
	if (Target == FVDBU6::ForestId()) { return EWayPicto::Forest; }
	if (FVMill::IsMillId(Target)) { return EWayPicto::Mill; }
	if (Target == FVDBU6::CityId()) { return EWayPicto::City; }
	if (Target == FVDBU6::CaveId()) { return EWayPicto::Cave; }
	return EWayPicto::Home;
}

const TCHAR* FVWorldU6::WayArrow(EWayDir Dir)
{
	switch (Dir)
	{
	case EWayDir::Left: return TEXT("←");
	case EWayDir::Right: return TEXT("→");
	case EWayDir::Up: return TEXT("↑");
	default: return TEXT("↓");
	}
}

TArray<FVWorldU6::FWalkLink> FVWorldU6::ClassicWalkLinks(FName From)
{
	// the map's geography: river (168,188) — pond (330,246) — ocean (530,262); the yard (404,200) above the pond;
	// the forest (64,176) left of the river, the mountain lake (196,88) above it; the volcano (478,84) right of the
	// mountains, above the ocean; the swamp (112,292) below the forest
	// Hotfix 0.61: the pond is wider than the screen (SideWiden): its exits sit at the new walk edges, the signs are
	// hand-placed in the new side strips (FWalkLink::SignLand / SignPort) — clear of Mitrich's stall, the order board,
	// the hut path and the dock (Docs/HOTFIX_061_POND_PLAN.json «signs»)
	struct FRow
	{
		const TCHAR* From; const TCHAR* To; EWayDir Side; float Frac;
		float FracPort = -1.f;
		FVector2D SignLand = FVector2D::ZeroVector, SignPort = FVector2D::ZeroVector;
	};
	static const FRow Rows[] = {
		{ TEXT("pond"), TEXT("river"), EWayDir::Left, 0.55f, 0.70f, FVector2D(34.f, -12.f), FVector2D(22.f, -12.f) },
		{ TEXT("pond"), TEXT("ocean"), EWayDir::Right, 0.55f, 0.55f, FVector2D(22.f, -12.f), FVector2D(14.f, -10.f) },
		{ TEXT("pond"), TEXT("home"), EWayDir::Up, 0.50f, 0.f, FVector2D(-40.f, 6.f), FVector2D(-40.f, 6.f) },
		{ TEXT("river"), TEXT("forest"), EWayDir::Left, 0.55f },
		{ TEXT("river"), TEXT("pond"), EWayDir::Right, 0.55f },
		{ TEXT("river"), TEXT("mountain"), EWayDir::Up, 0.5f },
		{ TEXT("mountain"), TEXT("river"), EWayDir::Left, 0.55f },
		{ TEXT("mountain"), TEXT("volcano"), EWayDir::Right, 0.55f },
		{ TEXT("volcano"), TEXT("mountain"), EWayDir::Left, 0.55f },
		{ TEXT("volcano"), TEXT("ocean"), EWayDir::Right, 0.55f },
		{ TEXT("ocean"), TEXT("pond"), EWayDir::Left, 0.55f },
		{ TEXT("ocean"), TEXT("volcano"), EWayDir::Right, 0.55f },
		{ TEXT("swamp"), TEXT("forest"), EWayDir::Up, 0.5f },
	};
	TArray<FWalkLink> Out;
	const FString F = From.ToString();
	for (const FRow& R : Rows)
	{
		if (F != R.From) { continue; }
		FWalkLink K;
		K.To = FName(R.To);
		K.Side = R.Side;
		K.Frac = R.Frac;
		K.FracPort = R.FracPort;
		K.SignLand = R.SignLand;
		K.SignPort = R.SignPort;
		Out.Add(K);
	}
	return Out;
}

bool FVWorldU6::WalkTargetOpen(const UFVSaveGame* P, FName Target, FString* OutCaption, FString* OutWhy)
{
	const FFVLocationDef* L = FVDB::FindLocation(Target);
	const FString Name = L ? L->Name : Target.ToString();
	const int32 MinLevel = L ? L->MinLevel : 1;
	bool bOpen = true;
	FString Short, Why;
	if (FVDB::IsHomeId(Target) || !P || P->IsLocationUnlocked(Target))
	{
		bOpen = true;
	}
	else if (FVDBU6::IsForestId(Target))
	{
		bOpen = P->GetLevel() >= MinLevel;   // free at its level (TravelTo opens it)
		Short = FString::Printf(TEXT("с %d уровня"), MinLevel);
		Why = FString::Printf(TEXT("Тропа в лес откроется с %d уровня"), MinLevel);
	}
	else if (FVMill::IsMillId(Target))
	{
		bOpen = P->CanOpenMill();
		if (!P->IsBridgeRepaired())
		{
			Short = TEXT("почините мостик");
			Why = TEXT("Мостик через ручей сломан — почините его, чтобы пройти к мельнице");
		}
		else
		{
			Short = FString::Printf(TEXT("с %d уровня"), MinLevel);
			Why = FString::Printf(TEXT("К мельнице — с %d уровня"), MinLevel);
		}
	}
	else if (Target == FVDBU6::CityId())
	{
		bOpen = P->IsBoatRepaired();
		Short = TEXT("нужна лодка");
		Why = TEXT("В Город можно только уплыть: почините лодку у причала");
	}
	else if (Target == FVDBU6::CaveId())
	{
		bOpen = P->IsCaveOpen();
		Short = TEXT("вход завален");
		Why = TEXT("Вход в Пещеру завален — разберите завал киркой");
	}
	else
	{
		bOpen = false;   // the paid places are opened on the map (as before), then the road is open
		const int32 Price = L ? FMath::Max(0, L->UnlockPrice) : 0;
		if (P->GetLevel() < MinLevel)
		{
			Short = FString::Printf(TEXT("ур. %d, откройте на карте"), MinLevel);
			Why = Price > 0
				? FString::Printf(TEXT("«%s» — с %d уровня, потом откройте на Карте за %s монет"), *Name, MinLevel, *FV::Money(Price))
				: FString::Printf(TEXT("«%s» — с %d уровня, потом откройте на Карте"), *Name, MinLevel);
		}
		else
		{
			Short = Price > 0 ? FString::Printf(TEXT("откройте на карте за %s"), *FV::Money(Price)) : FString(TEXT("откройте на карте"));
			Why = Price > 0
				? FString::Printf(TEXT("Путь закрыт: откройте «%s» на Карте за %s монет"), *Name, *FV::Money(Price))
				: FString::Printf(TEXT("Путь закрыт: откройте «%s» на Карте"), *Name);
		}
	}
	if (OutCaption) { *OutCaption = bOpen ? Name : FString::Printf(TEXT("Закрыто: %s — %s"), *Name, *Short); }
	if (OutWhy) { *OutWhy = Why; }
	return bOpen;
}

FName FVSceneU6Objects::NodeKey(FName LocationId, FName AnchorId)
{
	return FName(*FString::Printf(TEXT("%s:%s"), *LocationId.ToString(), *AnchorId.ToString()));
}

int32 FVSceneU6Objects::BedStage(const UFVSaveGame* P, int32 Bed)
{
	if (!P) { return -1; }
	const float G = P->GetBedGrowth(Bed);
	if (G < 0.f) { return -1; }
	if (G >= 1.f) { return 3; }
	return FMath::Clamp(int32(G * 3.f), 0, 2);
}

uint32 FVSceneU6Objects::HashState(const FVWorldU6::FWorldDef* Def, bool bBoat, bool bInterior, const UFVSaveGame* P, FName LocId)
{
	using namespace FVSceneU6ObjImpl;
	uint32 H = 0x6A09E667u;
	if (!P) { return H; }
	// Р69: the way signs show a padlock while their place is closed
	for (const FName T : WalkTargets()) { Mix(H, FVWorldU6::WalkTargetOpen(P, T) ? 41u : 43u); }
	if (bBoat) { Mix(H, uint32(P->U6.BoatStage) + 101u); }
	if (!Def) { return H; }
	Mix(H, uint32(P->GardenBeds()));
	Mix(H, P->IsCaveOpen() ? 3u : 5u);
	for (const FAnchor& A : Def->Anchors)
	{
		switch (A.Kind)
		{
		case EAnchor::Tree:
			if (A.bFlag) { Mix(H, uint32(P->GetTreeState(NodeKey(LocId, A.Id), EFVTreeState::Marked))); }
			break;
		case EAnchor::JunkPile:
		case EAnchor::Boulder:
			Mix(H, P->IsNodeReady(NodeKey(LocId, A.Id)) ? 7u : 9u);
			break;
		case EAnchor::BuildPlot:
		{
			EFVYardBuilding B;
			if (YardFromId(A.Id, B)) { Mix(H, uint32(P->GetYardLevel(B))); }
			break;
		}
		case EAnchor::GardenBed:
			Mix(H, uint32(BedStage(P, A.Index) + 2));
			if (P->U6.Garden.IsValidIndex(A.Index)) { Mix(H, GetTypeHash(P->U6.Garden[A.Index].Seed)); }
			break;
		case EAnchor::Blockage: Mix(H, P->U6.bBlockageCleared ? 11u : 13u); break;
		case EAnchor::Rubble: Mix(H, P->U6.RubbleCleared.Contains(A.Id) ? 15u : 17u); break;
		case EAnchor::MillObj:   // §18 [MILL-ART]: no clock here — deck timers are zeroed by PruneMill, the pen shrinks by it
			if (A.Id == FVMill::ObjDeckId()) { Mix(H, (P->U6.Mill.DeckUntil.IsValidIndex(A.Index) && P->U6.Mill.DeckUntil[A.Index] > 0.0) ? 25u : 27u); }
			else if (A.Id == FVMill::ObjPenId()) { Mix(H, uint32(P->U6.Mill.Pen.Num()) + 29u); }
			else { Mix(H, uint32(FVMill::ObjState(P, A.Id, A.Index, 0.0)) + 31u); }
			break;
		case EAnchor::MillZone: Mix(H, P->IsOmutOpen() ? 33u : 35u); break;
		case EAnchor::DecorSlot:
			if (A.Index >= FVDecor::ClassicSlots && A.Index < int32(EFVDecorSlot::Count))
			{
				Mix(H, GetTypeHash(P->GetEquippedDecor(EFVDecorSlot(A.Index))));
			}
			break;
		case EAnchor::Station:
			if (!bInterior)
			{
				EFVStation St;
				if (StationFromId(A.Id, St)) { Mix(H, uint32(P->GetStationLevel(St)) + 19u); }
				Mix(H, P->SmokeFuel > 0.f ? 21u : 23u);
			}
			break;
		default: break;
		}
	}
	return H;
}

void FVScene::FImpl::RebuildWorldU6(FVScene& S)
{
	using namespace FVSceneU6ObjImpl;
	if (!U6Objects.IsValid()) { U6Objects = MakeShared<FFVSceneU6State>(); }
	FFVSceneU6State& St6 = *U6Objects;
	++St6.Revision;
	St6.NpcVis.Reset();
	St6.NpcIds.Reset();
	if (!Loc) { return; }

	UFVSaveGame* P = Profile(S);
	UFVSprites* Spr = UFVSprites::Get();
	const FName LocId = Loc->Id;

	auto AddObj = [this](EFVObjKind Kind, int32 Index, FName Id, const FVector2D& Feet, const FFVRect& Hit, float Reach, const FString& Label) -> int32
	{
		FFVWorldObject O;
		O.Kind = Kind;
		O.Index = Index;
		O.Id = Id;
		O.Feet = Feet;
		O.Hit = Hit;
		O.Reach = Reach;
		O.Label = Label;
		return Objects.Add(O);
	};
	auto AddVis = [this](int32 Obj, const FFVSprite& Sp, const FVector2D& Pos, uint8 Layer, const FVector2D& Pivot, float SortY) -> int32
	{
		FVis V;
		V.Obj = Obj;
		V.S = Sp;
		V.Pos = Pos;
		V.Layer = Layer;
		V.Pivot = Pivot;
		V.SortY = SortY;
		V.bShadow = Layer == 3;
		V.Phase = float(Vis.Num()) * 1.37f;
		if (!Sp.IsValid()) { bArtPending = true; }
		return Vis.Add(V);
	};
	auto Block = [this](const FFVRect& R) { if (R.W > 0.f && R.H > 0.f) { L.Blockers.Add(R); } };
	auto AddLight = [this](const FVector2D& Pos, float Radius, const FColor& C, bool bFlicker, float DayInt)
	{
		FRtLight Lt;
		Lt.Pos = Pos;
		Lt.Radius = Radius;
		Lt.Color = C;
		Lt.bFlicker = bFlicker;
		Lt.DayInt = DayInt;
		RtLights.Add(Lt);
	};

	// ---------------------------------------------------------------- the boat (ocean pier: bBoat; the City pier)
	if (L.bBoat && FVSceneArt::IsPlaced(L.BoatFeet))
	{
		const bool bCity = LocId == FVDBU6::CityId();
		const int32 Stages = FVDBU6::BoatStages().Num();
		const int32 Stage = bCity ? 3 : FMath::Clamp(P ? P->U6.BoatStage : 0, 0, 3);
		const bool bReady = bCity || (P && P->IsBoatRepaired());
		const FFVSprite Sp = Spr ? Spr->BoatSprite(Stage) : FFVSprite();
		const FVector2D Sz = SpriteSize(Sp, 72.f, 40.f);
		const FVector2D Feet = L.BoatFeet;
		// the hull floats in the water right below the post; its waterline sits on the shore line
		const float WaterY = FMath::Max(float(Feet.Y), L.ShoreY) + Sz.Y * WS * 0.3f;
		const FVector2D Pos(float(Feet.X), WaterY);
		const FFVRect BoatR = FootRect(Pos, Sz);
		FString Label;
		if (bCity) { Label = TEXT("Лодка — домой, к причалу"); }
		else if (bReady) { Label = TEXT("Лодка — отплыть в Город"); }
		else { Label = FString::Printf(TEXT("Старая лодка (ремонт %d/%d)"), FMath::Clamp(Stage, 0, FMath::Max(1, Stages)), FMath::Max(1, Stages)); }
		const FFVRect FeetR(float(Feet.X) - 16.f, float(Feet.Y) - 20.f, 32.f, 24.f);
		const int32 O = AddObj(EFVObjKind::Boat, Stage, FName(TEXT("boat")), Feet, BoatR.W > 0.f ? FFVRect(
			FMath::Min(BoatR.X, FeetR.X), FMath::Min(BoatR.Y, FeetR.Y),
			FMath::Max(BoatR.Right(), FeetR.Right()) - FMath::Min(BoatR.X, FeetR.X),
			FMath::Max(BoatR.Bottom(), FeetR.Bottom()) - FMath::Min(BoatR.Y, FeetR.Y)) : FeetR, 40.f, Label);
		const int32 V = AddVis(O, Sp, Pos, 1, FVector2D(0.5f, 1.f), L.ShoreY);
		Vis[V].bShadow = false;
	}

	// ---------------------------------------------------------------- Р69: walk exits of the one-screen locations
	WalkExits.Reset();
	if (!bBig && !L.bInterior)
	{
		using FVWorldU6::EWayDir;
		const TArray<FVWorldU6::FWalkLink> Links = FVWorldU6::ClassicWalkLinks(LocId);
		const float EffTop = FMath::Max(L.WalkTop, FV::TopBarH + 66.f);
		const float Bot = L.bNoWater ? FMath::Min(L.WalkBottom, FV::VH - FV::BottomBarH - 10.f) : FMath::Min(L.ShoreY - 2.f, FV::VH - FV::BottomBarH - 10.f);
		// free ground: walkable and off the pickup / dig / net spots (static layout data: the exit never moves when a
		// pickup is taken)
		auto Free = [this](const FVector2D& Pt) -> bool
		{
			if (!IsWalkable(L, Pt)) { return false; }
			for (const TArray<FVector2D>* Spots : { &L.ResourceSlots, &L.DigSpots, &L.NetSpots })
			{
				for (const FVector2D& Sp : *Spots) { if (FVSceneArt::IsPlaced(Sp) && FVector2D::Distance(Sp, Pt) < 22.f) { return false; } }
			}
			return true;
		};
		// static objects the sign must not cover
		TArray<FFVRect> Solid;
		for (const FFVWorldObject& SO : Objects)
		{
			if (SO.Kind == EFVObjKind::Stall || SO.Kind == EFVObjKind::OrderBoard || SO.Kind == EFVObjKind::Door || SO.Kind == EFVObjKind::Boat) { Solid.Add(SO.Hit); }
		}
		// tall props drawn over the sign (a beach umbrella, a lamp post, a bush): their sprite rect (flat ones skipped)
		for (const FVSceneArt::FProp& Pr : L.Props)
		{
			const FFVSprite& PS = D(Pr.Kind);
			if (!PS.IsValid() || PS.H < 12.f || FVSceneArt::DecorPivot(Pr.Kind).Y < 0.9f) { continue; }
			Solid.Add(FFVRect(float(Pr.Feet.X) - PS.W * WS * 0.4f, float(Pr.Feet.Y) - PS.H * WS, PS.W * WS * 0.8f, PS.H * WS));
		}
		// Hotfix 0.61: a classic layout wider than the screen (the pond, ScrollRect) — the camera scrolls, so the HUD
		// column only ever covers the right end of the scroll range (the pad beyond the walk edge): its real width there
		const bool bScrolled = HasCamera();
		const float ViewR = bScrolled ? float(L.ScrollRect.Right()) : FV::VW;
		const float HudW = bScrolled ? (L.bPortrait ? 50.f : 72.f) : 80.f;
		// the HUD button column (top right) must not cover a sign (autotest «HUD column covers nothing»); kept LAST
		Solid.Add(FFVRect(ViewR - HudW, 0.f, HudW, FV::TopBarH + 170.f));
		const FVector2D SignSz(32.f, 32.f);
		// the HUD button column (top right, the same box as Solid's last rect): a right exit's trigger stays out of it
		const float ColX = ViewR - HudW, ColBottom = FV::TopBarH + 170.f;
		for (const FVWorldU6::FWalkLink& K : Links)
		{
			const bool bSide = K.Side != EWayDir::Up;
			const float Frac = (L.bPortrait && K.FracPort >= 0.f) ? K.FracPort : K.Frac;   // hotfix 0.61
			const FVector2D SignOff = L.bPortrait ? K.SignPort : K.SignLand;
			const float In = K.Side == EWayDir::Right ? -1.f : 1.f;   // inward along X
			float EdgeX = K.Side == EWayDir::Right ? L.WalkMaxX : L.WalkMinX;
			FVector2D Edge;
			bool bFound = false;
			if (bSide)
			{
				const float Pref = FMath::Lerp(EffTop + 8.f, Bot - 6.f, FMath::Clamp(Frac, 0.f, 1.f));
				Edge = FVector2D(EdgeX + In * 2.f, Pref);
				// a right exit: first below the HUD column at the very edge; where the column covers the whole band
				// (landscape: the band is short) the trigger moves in front of the column (the path runs on under it)
				const bool bRight = K.Side == EWayDir::Right;
				// passes: (below the column) x (the whole row free for 84 px — the arrival spot and the straight way back
				// into the exit, no umbrella / rock on it; then the old three points). A scrolling place: the column lies in
				// the pad beyond the walk edge, the right exit starts like a left one
				for (int32 Pass = (bRight && !bScrolled) ? 0 : 2; Pass < 4 && !bFound; ++Pass)
				{
					if (Pass == 2 && bRight && EdgeX + 12.f > ColX) { EdgeX = FMath::Max(L.WalkMinX + 120.f, ColX - 12.f); }
					const bool bDense = (Pass % 2) == 0;
					for (int32 i = 0; i < 30 && !bFound; ++i)
					{
						const float Y = Pref + float((i + 1) / 2) * 4.f * ((i % 2) ? -1.f : 1.f);
						if (Y < EffTop + 4.f || Y > Bot - 3.f) { continue; }
						if (Pass < 2 && Y - 20.f < ColBottom) { continue; }
						const FVector2D Pt(EdgeX + In * 2.f, Y);
						bool bFree = Free(Pt) && Free(Pt + FVector2D(In * 24.f, 0.f)) && Free(Pt + FVector2D(In * 48.f, 0.f));
						for (float t = 1.f; bDense && bFree && t <= 84.f; t += 1.f) { bFree = IsWalkable(L, Pt + FVector2D(In * t, 0.f)); }
						if (bFree) { Edge = Pt; bFound = true; }
					}
				}
				if (!bFound) { Edge.X = EdgeX + In * 2.f; }
			}
			else
			{
				const float X0 = L.WalkMinX + 60.f, X1 = FMath::Max(X0, L.WalkMaxX - 60.f);
				const float Pref = FMath::Lerp(X0, X1, FMath::Clamp(Frac, 0.f, 1.f));
				Edge = FVector2D(Pref, EffTop + 2.f);
				for (int32 i = 0; i < 60 && !bFound; ++i)
				{
					const float X = Pref + float((i + 1) / 2) * 6.f * ((i % 2) ? -1.f : 1.f);
					if (X < X0 - 30.f || X > X1 + 30.f) { continue; }
					const FVector2D Pt(X, EffTop + 2.f);
					if (Free(Pt) && Free(Pt + FVector2D(0.f, 20.f)) && Free(Pt + FVector2D(0.f, 38.f))) { Edge = Pt; bFound = true; }
				}
			}
			// the trigger: the very edge of the walk band (the player has to walk right into it)
			const FFVRect Trigger = bSide
				? (K.Side == EWayDir::Left ? FFVRect(EdgeX - 12.f, float(Edge.Y) - 20.f, 19.f, 40.f) : FFVRect(EdgeX - 7.f, float(Edge.Y) - 20.f, 19.f, 40.f))
				: FFVRect(float(Edge.X) - 18.f, EffTop - 12.f, 36.f, 19.f);
			// the sign beside the path: the first candidate that covers no stall / board / door / boat
			TArray<FVector2D> Cands;
			if (bSide)
			{
				Cands = { FVector2D(EdgeX + In * 34.f, float(Edge.Y) - 12.f), FVector2D(EdgeX + In * 34.f, float(Edge.Y) + 44.f), FVector2D(EdgeX + In * 70.f, float(Edge.Y) - 12.f),
					FVector2D(EdgeX + In * 104.f, float(Edge.Y) - 12.f), FVector2D(EdgeX + In * 34.f, float(Edge.Y) + 26.f),
					FVector2D(EdgeX + In * 140.f, float(Edge.Y) - 12.f), FVector2D(EdgeX + In * 176.f, float(Edge.Y) - 12.f) };
			}
			else
			{
				Cands = { FVector2D(float(Edge.X) + 34.f, EffTop + 6.f), FVector2D(float(Edge.X) - 34.f, EffTop + 6.f), FVector2D(float(Edge.X) + 60.f, EffTop + 6.f) };
			}
			if (bSide)
			{
				// crowded right edge (a stall under the HUD column): in front of the stall, off the column
				Cands.Add(FVector2D(EdgeX + In * 104.f, float(Edge.Y) + 26.f));
				Cands.Add(FVector2D(EdgeX + In * 140.f, float(Edge.Y) + 26.f));
			}
			// hotfix 0.61: a hand-placed sign (FWalkLink::SignLand / SignPort) is tried first; it skips the distance filters
			// below but still has to cover no stall / board / door / boat / tall prop / HUD column
			const bool bHandSign = !SignOff.IsZero();
			if (bHandSign)
			{
				Cands.Insert(bSide ? FVector2D(EdgeX + In * float(SignOff.X), float(Edge.Y) + float(SignOff.Y))
					: FVector2D(float(Edge.X) + float(SignOff.X), EffTop + float(SignOff.Y)), 0);
			}
			// pass 0: covers nothing; pass 1: only off the HUD column (Solid[0] is the column, added last -> checked by index)
			FVector2D SignFeet = Cands[0];
			bool bPicked = false;
			for (int32 Pass = 0; Pass < 2 && !bPicked; ++Pass)
			{
				for (int32 c = 0; c < Cands.Num(); ++c)
				{
					const FVector2D& Cd = Cands[c];
					const bool bFilter = bSide && !(bHandSign && c == 0);
					if (bFilter && Cd.Y > Bot + 4.f) { continue; }
					// the sign stays by its own edge (far in, it stood beside the opposite exit's sign: two arrows apart)
					if (bFilter && FMath::Abs(float(Cd.X) - EdgeX) > 110.f) { continue; }
					const FFVRect CR = FootRect(Cd, SignSz, 0.8f);
					bool bHit = false;
					for (int32 r = (Pass == 0 ? 0 : Solid.Num() - 1); r < Solid.Num(); ++r)
					{
						const FFVRect& R = Solid[r];
						bHit = bHit || (R.X < CR.Right() && CR.X < R.Right() && R.Y < CR.Bottom() && CR.Y < R.Bottom());
					}
					if (!bHit) { SignFeet = Cd; bPicked = true; break; }
				}
			}

			FVWorldU6::FAnchor A;
			A.Kind = FVWorldU6::EAnchor::Exit;
			A.Id = FName(*FString::Printf(TEXT("from_%s"), *LocId.ToString()));
			A.Target = K.To;
			A.Feet = Edge;
			A.Hit = Trigger;
			A.SignFeet = SignFeet;
			A.SignDir = uint8(K.Side);
			WalkExits.Add(A);

			FString Caption;
			const bool bOpen = FVWorldU6::WalkTargetOpen(P, K.To, &Caption);
			const FString Label = FString::Printf(TEXT("%s %s"), FVWorldU6::WayArrow(K.Side), *Caption);
			const FFVSprite SignSp = Spr ? Spr->WaySignSprite(uint8(FVWorldU6::WayPictoFor(K.To)), uint8(K.Side), !bOpen) : FFVSprite();
			// the tap / click rect is the sign (the trigger at the very edge is WalkExits' business)
			const FFVRect SR = FootRect(SignFeet, SpriteSize(SignSp, SignSz.X, SignSz.Y), 0.8f);
			const int32 EO = AddObj(EFVObjKind::Exit, bOpen ? 0 : 1, K.To, Edge, SR, 20.f, Label);
			AddVis(EO, SignSp, SignFeet, 3, FVector2D(0.5f, 1.f), float(SignFeet.Y));
			// the trodden path off the screen edge (flat)
			const FFVSprite PathSp = Spr ? Spr->WayPathSprite(uint8(K.Side)) : FFVSprite();
			int32 PV;
			if (K.Side == EWayDir::Left) { PV = AddVis(INDEX_NONE, PathSp, FVector2D(EdgeX - 26.f, float(Edge.Y) + 2.f), 0, FVector2D(0.f, 0.5f), 0.f); }
			else if (K.Side == EWayDir::Right)
			{
				PV = AddVis(INDEX_NONE, PathSp, FVector2D(EdgeX + 26.f, float(Edge.Y) + 2.f), 0, FVector2D(1.f, 0.5f), 0.f);
				if (EdgeX < L.WalkMaxX - 8.f)
				{
					const int32 PV2 = AddVis(INDEX_NONE, PathSp, FVector2D(L.WalkMaxX + 26.f, float(Edge.Y) + 2.f), 0, FVector2D(1.f, 0.5f), 0.f);
					Vis[PV2].bShadow = false;
				}
				// hotfix 0.61: a scrolling place — the path runs on through the pad to the right end of the view (no
				// invisible wall on the grass there)
				const float PathW = PathSp.IsValid() && PathSp.W > 0.f ? PathSp.W * WS : 80.f;
				for (float PX = FMath::Max(EdgeX, L.WalkMaxX) + 26.f + PathW; bScrolled && PX - PathW < ViewR; PX += PathW)
				{
					const int32 PV3 = AddVis(INDEX_NONE, PathSp, FVector2D(PX, float(Edge.Y) + 2.f), 0, FVector2D(1.f, 0.5f), 0.f);
					Vis[PV3].bShadow = false;
				}
			}
			else { PV = AddVis(INDEX_NONE, PathSp, FVector2D(float(Edge.X), EffTop - 18.f), 0, FVector2D(0.5f, 0.f), 0.f); }
			Vis[PV].bShadow = false;
		}
	}

	if (!bBig || !BigDef.IsValid()) { return; }
	const FVWorldU6::FWorldDef& Def = *BigDef;
	const FName CaveId = FVDBU6::CaveId();

	// Р61 fix (the fence over the cottage): the props painted into the yard background in front of the back fence line
	// (the cottage, the apple trees, the edge trees) are redrawn y-sorted by their feet, so the tiled fence decor and
	// anything else behind them stays behind them (FVArtYardFront.h; the same pixels as the background)
	const bool bYardWorld = LocId == FVDB::HomeId();
	const float CottageY = bYardWorld ? float(FVArtYardFront::CottageFeet().Y) : 0.f;
	if (bYardWorld)
	{
		for (const FVArtYardFront::FPart& Pt : FVArtYardFront::Parts())
		{
			const int32 V = AddVis(INDEX_NONE, Pt.S, Pt.TopLeft, 3, FVector2D(0.f, 0.f), Pt.SortY);
			Vis[V].bShadow = false;
		}
	}

	for (const FAnchor& A : Def.Anchors)
	{
		const FVector2D Feet = A.Feet;
		switch (A.Kind)
		{
		// ------------------------------------------------------------ trees (forest): only Marked ones are interactive
		case EAnchor::Tree:
		{
			const EFVTreeKind Kind = EFVTreeKind(FMath::Clamp(int32(A.Variant), 0, int32(EFVTreeKind::Count) - 1));
			const EFVTreeState State = (A.bFlag && P) ? P->GetTreeState(FVSceneU6Objects::NodeKey(LocId, A.Id), EFVTreeState::Marked)
				: (A.bFlag ? EFVTreeState::Marked : EFVTreeState::Full);
			const int32 Var = int32(GetTypeHash(A.Id) % 4u);
			const FFVSprite Sp = Spr ? Spr->TreeSprite(Kind, State, Var) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, State == EFVTreeState::Stump ? 16.f : (State == EFVTreeState::Sapling ? 12.f : 40.f),
				State == EFVTreeState::Stump ? 10.f : (State == EFVTreeState::Sapling ? 20.f : 64.f));
			int32 O = INDEX_NONE;
			if (A.bFlag && State == EFVTreeState::Marked)
			{
				const FFVRect Hit = A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz, 0.55f);
				O = AddObj(EFVObjKind::Tree, int32(Kind), A.Id, Feet + FVector2D(0.f, 12.f), Hit, 34.f,
					FString::Printf(TEXT("%s с зарубкой"), *TreeName(Kind)));
			}
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			if (State == EFVTreeState::Full || State == EFVTreeState::Marked) { Block(FFVRect(float(Feet.X) - 7.f, float(Feet.Y) - 6.f, 14.f, 8.f)); }
			else if (State == EFVTreeState::Stump) { Block(FFVRect(float(Feet.X) - 8.f, float(Feet.Y) - 5.f, 16.f, 6.f)); }
			break;
		}
		// ------------------------------------------------------------ City junk piles
		case EAnchor::JunkPile:
		{
			const bool bReady = !P || P->IsNodeReady(FVSceneU6Objects::NodeKey(LocId, A.Id));
			const FFVSprite Sp = Spr ? Spr->JunkPileSprite(A.Variant, !bReady) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 28.f, 20.f);
			const int32 O = AddObj(EFVObjKind::JunkPile, A.Index, A.Id, Feet + FVector2D(0.f, 12.f), A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz),
				32.f, bReady ? FString(TEXT("Куча хлама")) : FString(TEXT("Куча хлама (разобрана)")));
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			Block(FootBlocker(Feet, Sz, 0.7f, 6.f, 12.f));
			break;
		}
		// ------------------------------------------------------------ Cave boulders
		case EAnchor::Boulder:
		{
			const bool bReady = !P || P->IsNodeReady(FVSceneU6Objects::NodeKey(LocId, A.Id));
			const FFVSprite Sp = Spr ? Spr->BoulderSprite(A.Variant, !bReady) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 32.f, 28.f);
			int32 O = INDEX_NONE;
			if (bReady)
			{
				O = AddObj(EFVObjKind::Boulder, A.Index, A.Id, Feet + FVector2D(0.f, 12.f), A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz), 34.f,
					TEXT("Валун с прожилками"));
			}
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			Block(FootBlocker(Feet, Sz, bReady ? 0.8f : 0.5f, 6.f, 14.f));
			break;
		}
		// ------------------------------------------------------------ villagers (idle animation in TickWorld)
		case EAnchor::Npc:
		{
			const FFVNpcDef* N = FVDBU6::FindNpc(A.Id);
			// Р66: task givers do not stand on the streets — only the task board (city_board) speaks for them
			if (!N || N->Screen == EFVScreen::CityBoard) { break; }
			const FFVSprite Sp = Spr ? Spr->NpcSprite(N->Id, 0) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 32.f, 48.f);
			const FFVRect Hit = A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz, 0.6f);
			const int32 O = AddObj(EFVObjKind::Npc, A.Index, N->Id, Feet + FVector2D(0.f, 14.f), Hit, 36.f, N->Name);
			const int32 V = AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			St6.NpcVis.Add(V);
			St6.NpcIds.Add(N->Id);
			Block(FFVRect(float(Feet.X) - 8.f, float(Feet.Y) - 6.f, 16.f, 8.f));
			break;
		}
		// ------------------------------------------------------------ a trader counter without a villager
		case EAnchor::Stall:
		{
			const FFVTraderDef* Tr = FVDB::FindTrader(A.Id);
			if (!Tr) { break; }
			const FFVSprite Sp = Spr ? Spr->StallSprite(Tr->Id) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 56.f, 48.f);
			const FString Label = Tr->ShopTitle.IsEmpty() ? Tr->Name : Tr->ShopTitle;
			const int32 O = AddObj(EFVObjKind::Stall, 0, Tr->Id, Feet + FVector2D(0.f, 14.f), A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz), 36.f, Label);
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			Block(FootBlocker(Feet, Sz, 0.84f, 8.f, 16.f));
			break;
		}
		// ------------------------------------------------------------ yard plots / buildings (+ forge glow and smoke)
		case EAnchor::BuildPlot:
		{
			EFVYardBuilding B;
			if (!YardFromId(A.Id, B)) { break; }
			const FFVYardBuildingDef& YD = FVDBU6::YardBuilding(B);
			const int32 Level = P ? FMath::Max(0, P->GetYardLevel(B)) : 0;
			const FFVSprite Sp = Spr ? Spr->YardBuildingSprite(B, Level) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, Level > 0 ? 72.f : 48.f, Level > 0 ? 56.f : 24.f);
			const FFVRect Hit = A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz, 0.9f);
			if (Level <= 0)
			{
				const int32 O = AddObj(EFVObjKind::BuildPlot, int32(B), A.Id, Feet + FVector2D(0.f, 10.f), Hit, 40.f,
					FString::Printf(TEXT("Участок: %s"), *YD.Name));
				const int32 V = AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
				Vis[V].bShadow = false;
				break;
			}
			const int32 O = AddObj(EFVObjKind::YardBuilding, int32(B), A.Id, Feet + FVector2D(0.f, 14.f), Hit, 44.f,
				Level > 1 ? FString::Printf(TEXT("%s (ур. %d)"), *YD.Name, Level) : YD.Name);
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			if (B != EFVYardBuilding::Garden) { Block(FootBlocker(Feet, Sz, 0.84f, 10.f, 30.f)); }
			if (B == EFVYardBuilding::Forge)
			{
				const FVector2D Ep = UFVSprites::YardEffectPoint(B, Level);
				const FVector2D TL = Feet - FVector2D(Sz.X * WS * 0.5f, Sz.Y * WS);
				const FVector2D Glow = (Ep.X >= 0.f && Ep.Y >= 0.f) ? TL + Ep * WS : Feet - FVector2D(0.f, Sz.Y * WS * 0.35f);
				AddLight(Glow, 46.f + 8.f * float(Level), FColor(255, 128, 48), true, 0.35f);
				RtSmokes.Add(TL + FVector2D(Sz.X * WS * 0.78f, 4.f));
			}
			break;
		}
		// ------------------------------------------------------------ garden beds (only the beds the garden has)
		case EAnchor::GardenBed:
		{
			const int32 Bed = A.Index;
			if (!P || Bed < 0 || Bed >= P->GardenBeds()) { break; }
			const int32 Stage = FVSceneU6Objects::BedStage(P, Bed);
			FName Crop;
			if (Stage >= 0 && P->U6.Garden.IsValidIndex(Bed))
			{
				const FFVSeedDef* SD = FVDBU6::FindSeed(P->U6.Garden[Bed].Seed);
				Crop = SD ? SD->CropId : P->U6.Garden[Bed].Seed;
			}
			const FFVSprite Sp = Spr ? Spr->GardenBedSprite(Crop, FMath::Max(0, Stage)) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 24.f, 14.f);
			FString Label;
			if (Stage < 0) { Label = TEXT("Грядка — можно посадить"); }
			else if (Stage >= 3) { Label = FString::Printf(TEXT("Грядка: %s — урожай готов"), *CropName(P, Bed)); }
			else { Label = FString::Printf(TEXT("Грядка: %s (%d%%)"), *CropName(P, Bed), FMath::Clamp(FMath::RoundToInt(P->GetBedGrowth(Bed) * 100.f), 0, 99)); }
			const int32 O = AddObj(EFVObjKind::GardenBed, Bed, NAME_None, Feet + FVector2D(0.f, 10.f), A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz), 30.f, Label);
			const int32 V = AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y) - 2.f);
			Vis[V].bShadow = false;
			break;
		}
		// ------------------------------------------------------------ stations standing in the yard (smokehouse, worm farm)
		case EAnchor::Station:
		{
			if (L.bInterior) { break; }   // the house interior section of RebuildWorld draws them
			EFVStation Stn;
			if (!StationFromId(A.Id, Stn)) { break; }
			const int32 Level = P ? FMath::Max(0, P->GetStationLevel(Stn)) : 0;
			const FFVSprite Sp = Spr ? Spr->YardStationSprite(Stn, Level) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 32.f, 32.f);
			const FFVStationDef* SD = FVDB::FindStation(A.Id);
			FString Label = (SD && !SD->Name.IsEmpty()) ? SD->Name : FVDB::ItemName(A.Id);
			if (Level <= 0) { Label += (Stn == EFVStation::Smokehouse) ? TEXT(" (не построена)") : TEXT(" (не построен)"); }
			const int32 O = AddObj(EFVObjKind::Station, int32(Stn), A.Id, Feet + FVector2D(0.f, 14.f), A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz), 34.f, Label);
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			Block(FootBlocker(Feet, Sz, 0.84f, 8.f, 22.f));
			if (Stn == EFVStation::Smokehouse && Level > 0 && P && P->SmokeFuel > 0.f)
			{
				RtSmokes.Add(Feet - FVector2D(-Sz.X * WS * 0.18f, Sz.Y * WS - 4.f));
				AddLight(Feet - FVector2D(0.f, Sz.Y * WS * 0.25f), 30.f, FColor(255, 120, 50), true, 0.2f);
			}
			break;
		}
		// ------------------------------------------------------------ forest windfall (one-off)
		case EAnchor::Blockage:
		{
			const bool bCleared = P && P->U6.bBlockageCleared;
			const FFVSprite Sp = Spr ? Spr->BlockageSprite(bCleared) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 96.f, 40.f);
			if (bCleared)
			{
				AddVis(INDEX_NONE, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
				break;
			}
			const FFVRect R = FootRect(Feet, Sz);
			// 0.6 fix: the windfall closes the WHOLE gap of the ravine — its Hit rect is the gap (FVArtForest.cpp). The old
			// band at the sprite's feet left the upper half of the gap open and the player walked through the windfall.
			// The player clears it from the edge side (the river / the map arrive there): left of the Hit, mid-gap (the
			// old point below the sprite was inside the gully blocker).
			const FFVRect Gap = A.Hit.W > 0.f ? A.Hit : R;
			const FVector2D Stand(Gap.X - 14.f, Gap.Y + Gap.H * 0.5f);
			const int32 O = AddObj(EFVObjKind::Blockage, 0, A.Id, Stand, Gap, 44.f, TEXT("Завал — расчистить топором"));
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			const float BandY = float(Feet.Y) - FMath::Max(16.f, R.H * 0.45f);
			const float BX0 = FMath::Min(R.X, Gap.X), BX1 = FMath::Max(R.Right(), Gap.Right());
			const float BY0 = FMath::Min(Gap.Y, BandY), BY1 = FMath::Max(Gap.Bottom(), float(Feet.Y) + 2.f);
			Block(FFVRect(BX0, BY0, BX1 - BX0, BY1 - BY0));
			break;
		}
		// ------------------------------------------------------------ City rubble in front of the cave (one-off)
		case EAnchor::Rubble:
		{
			const bool bCleared = P && P->U6.RubbleCleared.Contains(A.Id);
			const FFVSprite Sp = Spr ? Spr->RubbleSprite(A.Index, bCleared) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 40.f, 28.f);
			if (bCleared)
			{
				const int32 V = AddVis(INDEX_NONE, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
				Vis[V].bShadow = false;
				break;
			}
			const FFVRect R = FootRect(Feet, Sz);
			const int32 O = AddObj(EFVObjKind::CaveRubble, A.Index, A.Id, Feet + FVector2D(0.f, 14.f), A.Hit.W > 0.f ? A.Hit : R, 38.f, TEXT("Завал у пещеры"));
			AddVis(O, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y));
			// the band closes the whole adit path (x 1094..1126 texels): the anchor's Hit spans it, the sprite may not
			const FFVRect Span = A.Hit.W > 0.f ? A.Hit : R;
			const float BX0 = FMath::Min(R.X, Span.X), BX1 = FMath::Max(R.Right(), Span.Right());
			const float BandH = FMath::Max(12.f, R.H * 0.4f);
			Block(FFVRect(BX0, float(Feet.Y) - BandH, BX1 - BX0, BandH + 2.f));
			break;
		}
		// ------------------------------------------------------------ §18 «Старая мельница» objects [MILL-ART]
		case EAnchor::MillObj:
		{
			const double Now = S.Game ? S.Game->GetAbsMinutes() : 0.0;
			const int32 State = FVMill::ObjState(P, A.Id, A.Index, Now);
			const FFVSprite Sp = Spr ? Spr->MillObjSprite(A.Id, State, 0) : FFVSprite();
			const FVector2D Sz = SpriteSize(Sp, 32.f, 24.f);
			const bool bMill = A.Id == FVMill::ObjMillId();
			const bool bWaterObj = A.Id == FVMill::ObjBeaverId() || A.Id == FVMill::ObjDeckId() || A.Id == FVMill::ObjPenId();
			const FFVRect R = FootRect(Feet, Sz);
			const FFVRect Hit = A.Hit.W > 0.f ? A.Hit : R;
			// where the player stands: on the bank above the water objects, in front of the mill, on the path below the bridge
			FVector2D Stand = Feet + FVector2D(0.f, bMill ? 14.f : 10.f);
			if (bWaterObj) { Stand = FVector2D(Hit.X + Hit.W * 0.5f, FMath::Min(float(Feet.Y), L.ShoreY) - 8.f); }
			const int32 O = AddObj(EFVObjKind::MillObj, A.Index, A.Id, Stand, Hit, 40.f, FVMill::ObjLabel(P, A.Id, A.Index, Now));
			// the water objects and the footbridge are flat (the player stands / walks over them), the mill is y-sorted
			const int32 V = AddVis(O, Sp, Feet, bMill ? 3 : 0, FVector2D(0.5f, 1.f), float(Feet.Y));
			if (!bMill) { Vis[V].bShadow = false; }
			if (bMill) { Block(FootBlocker(Feet, Sz, 0.66f, 10.f, 26.f)); }
			// 0.6 fix: the broken footbridge physically closes the path over the brook (the player walked over it and only
			// the exit refused); repaired, it is walkable. The block ends 4 px above the feet (the stand point stays free).
			if (A.Id == FVMill::ObjBridgeId() && !(P && P->IsBridgeRepaired()))
			{
				Block(FFVRect(Hit.X - 4.f, Hit.Y - 4.f, Hit.W + 8.f, FMath::Max(24.f, float(Feet.Y) - 4.f - (Hit.Y - 4.f))));
			}
			// animation: the working mill (the wheel turns, 4 frames), a baited deck, the pen with fish, the beavers (2 frames)
			const int32 NF = (bMill && State >= FVMill::MillStages().Num()) ? 4 : ((bWaterObj && (State > 0 || A.Id == FVMill::ObjBeaverId())) ? 2 : 1);
			if (NF > 1 && Spr && Sp.IsValid())
			{
				FVis& MV = Vis[V];
				MV.Frames.Add(Sp);
				for (int32 f = 1; f < NF; ++f) { MV.Frames.Add(Spr->MillObjSprite(A.Id, State, f)); }
				MV.FrameFps = bMill ? 8.f : 2.f;
			}
			if (bMill && State >= FVMill::MillStages().Num())
			{
				AddLight(Feet - FVector2D(-Sz.X * WS * 0.08f, Sz.Y * WS * 0.4f), 36.f, FColor(255, 206, 130), false, 0.f);   // the lit window
			}
			break;
		}
		// the Omut is closed for the player until the beavers finish the dam
		case EAnchor::MillZone:
			if (A.Id == FVMill::ZoneOmutId() && P && !P->IsOmutOpen()) { Block(A.Hit); }
			break;
		// ------------------------------------------------------------ exits (tap = walk there; the trigger is SCENE-CAMERA's)
		case EAnchor::Exit:
		{
			if (A.Target.IsNone()) { break; }
			// Р69: the way sign with the picture of the target (a padlock while it is closed)
			FString Caption;
			const bool bOpen = A.Target == FName(TEXT("map")) || FVWorldU6::WalkTargetOpen(P, A.Target, &Caption);
			const FVWorldU6::EWayDir Dir = FVWorldU6::EWayDir(FMath::Min<uint8>(A.SignDir, 3));
			const bool bSign = !A.SignFeet.IsZero();
			const FFVSprite SignSp = (bSign && Spr) ? Spr->WaySignSprite(uint8(FVWorldU6::WayPictoFor(A.Target)), uint8(Dir), !bOpen) : FFVSprite();
			const FFVRect SR = bSign ? FootRect(A.SignFeet, SpriteSize(SignSp, 32.f, 32.f), 0.8f) : FFVRect();
			int32 EO = INDEX_NONE;
			// the closed Cave / the path over the broken footbridge: no walk-to object (the rubble / the bridge offer the
			// repair), only the locked sign
			const bool bNoObj = (A.Target == CaveId && P && !P->IsCaveOpen()) || (FVMill::IsMillId(A.Target) && !(P && P->IsBridgeRepaired()));
			if (!bNoObj)
			{
				FFVRect Hit = A.Hit.W > 0.f ? A.Hit : FFVRect(float(Feet.X) - 20.f, float(Feet.Y) - 20.f, 40.f, 40.f);
				if (bSign)
				{
					const float HX0 = FMath::Min(Hit.X, SR.X), HY0 = FMath::Min(Hit.Y, SR.Y);
					Hit = FFVRect(HX0, HY0, FMath::Max(Hit.Right(), SR.Right()) - HX0, FMath::Max(Hit.Bottom(), SR.Bottom()) - HY0);
				}
				const FString Label = Caption.IsEmpty() ? (A.Label.IsEmpty() ? FString::Printf(TEXT("Путь: %s"), *FVDB::ItemName(A.Target)) : A.Label)
					: FString::Printf(TEXT("%s %s"), FVWorldU6::WayArrow(Dir), *Caption);
				EO = AddObj(EFVObjKind::Exit, bOpen ? 0 : 1, A.Target, Feet, Hit, 24.f, Label);
			}
			if (bSign) { AddVis(EO, SignSp, A.SignFeet, 3, FVector2D(0.5f, 1.f), float(A.SignFeet.Y)); }
			break;
		}
		// ------------------------------------------------------------ City task board (opens through a board villager)
		case EAnchor::Board:
		{
			if (A.Id != FName(TEXT("city_board"))) { break; }   // "order_board" -> L.bBoard (RebuildWorld)
			FName BoardNpc;
			for (const FFVNpcDef* N : FVDBU6::NpcsAt(LocId))
			{
				if (N && N->Screen == EFVScreen::CityBoard) { BoardNpc = N->Id; break; }
			}
			// the board is baked into the City background (ART-CITY): no sprite, only the approach highlight
			// (DrawWorldOverlay glows the Hit of a highlighted object that has no sprite)
			const FVector2D Sz(30.f, 34.f);
			if (!BoardNpc.IsNone())
			{
				AddObj(EFVObjKind::Npc, 0, BoardNpc, Feet + FVector2D(0.f, 12.f), A.Hit.W > 0.f ? A.Hit : FootRect(Feet, Sz), 32.f, TEXT("Доска заданий Города"));
			}
			Block(FootBlocker(Feet, Sz, 0.72f, 6.f, 10.f));
			break;
		}
		// ------------------------------------------------------------ yard decor / furniture of the new slots
		case EAnchor::DecorSlot:
		{
			if (!P || !Spr || A.Index < FVDecor::ClassicSlots || A.Index >= int32(EFVDecorSlot::Count)) { break; }
			const EFVDecorSlot Slot = EFVDecorSlot(A.Index);
			const FFVDecorDef* DD = FVDB::FindDecor(P->GetEquippedDecor(Slot));
			if (!DD) { break; }
			const FFVSprite Sp = Spr->DecorWorld(*DD);
			const FVector2D Sz = SpriteSize(Sp, 16.f, 24.f);
			if (Slot == EFVDecorSlot::YardFence && A.Hit.W > 0.f && Sp.IsValid())
			{
				// ART-DECOR: a 36-texel section repeated every 30 texels (the posts overlap exactly), along the Hit run,
				// the sections stand on the Hit's bottom line
				const float Step = 30.f * WS, SecW = Sz.X * WS;
				const int32 N = FMath::Clamp(FMath::CeilToInt(FMath::Max(0.f, A.Hit.W - SecW) / Step) + 1, 1, 256);
				for (int32 k = 0; k < N; ++k)
				{
					const float X = FMath::Min(A.Hit.X + Step * float(k), FMath::Max(A.Hit.X, A.Hit.Right() - SecW));
					const FVector2D Pos(FMath::RoundToFloat(X), FMath::RoundToFloat(float(A.Hit.Bottom())));
					const int32 V = AddVis(INDEX_NONE, Sp, Pos, 3, FVector2D(0.f, 1.f), float(Pos.Y));
					Vis[V].bShadow = false;
				}
				break;
			}
			if (Slot == EFVDecorSlot::YardPath && A.Hit.W > 0.f && A.Hit.H > 0.f && Sp.IsValid() && Sp.W > 0.f && Sp.H > 0.f)
			{
				// ART-DECOR: a seamless tile, pivot top-left, tiled over the Hit (the path mask); edge tiles are cropped by UV
				const float TW = Sp.W * WS, TH = Sp.H * WS;
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
						const int32 V = AddVis(INDEX_NONE, T, FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), 0, FVector2D(0.f, 0.f), Y);
						Vis[V].bShadow = false;
					}
				}
				break;
			}
			int32 DV = INDEX_NONE;
			if (IsFlatDecor(Slot))
			{
				DV = AddVis(INDEX_NONE, Sp, Feet, 0, FVector2D(0.5f, 0.5f), float(Feet.Y));
				Vis[DV].bShadow = false;
			}
			else if (bYardWorld && Slot == EFVDecorSlot::YardVane)
			{
				// on the ridge: after the redrawn cottage and its roof (below)
				DV = AddVis(INDEX_NONE, Sp, Feet, 3, FVector2D(0.5f, 1.f), CottageY + 0.4f);
				Vis[DV].bShadow = false;
			}
			else if (IsWallDecor(Slot))
			{
				DV = AddVis(INDEX_NONE, Sp, Feet, 2, Slot == EFVDecorSlot::Chandelier ? FVector2D(0.5f, 0.f) : FVector2D(0.5f, 1.f), float(Feet.Y));
				Vis[DV].bShadow = false;
				// the facade overlays share the cottage canvas: redrawn with the cottage's front parts right after it
				// (walls -> shutters -> roof), so the fence decor behind the house is hidden by the whole facade
				const bool bFacade = Slot == EFVDecorSlot::YardWalls || Slot == EFVDecorSlot::YardShutters || Slot == EFVDecorSlot::YardRoof;
				if (bYardWorld && bFacade && Sp.W > 0.f && Sp.H > 0.f && FVector2D::Distance(Feet, FVArtYardFront::CottageFeet()) < 1.f)
				{
					const float Order = Slot == EFVDecorSlot::YardWalls ? 0.1f : (Slot == EFVDecorSlot::YardShutters ? 0.2f : 0.3f);
					const FVector2D TL = Feet - FVector2D(Sp.W * 0.5f, Sp.H) * WS;
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
						const int32 V = AddVis(INDEX_NONE, T, TL + FVector2D(X0, Y0) * WS, 3, FVector2D(0.f, 0.f), CottageY + Order);
						Vis[V].bShadow = false;
					}
				}
			}
			else
			{
				DV = AddVis(INDEX_NONE, Sp, Feet, 3, FVector2D(0.5f, 1.f), float(Feet.Y) + (IsSkinDecor(Slot) ? 1.f : 0.f));
				if (!IsSkinDecor(Slot)) { Block(FootBlocker(Feet, Sz, 0.6f, 4.f, 10.f)); }
			}
			// animation frames (ART-DECOR hook FVArtDecorU6::WorldFrames / BuildWorldFrame), cached per look + frame
			const int32 NF = FMath::Clamp(FVArtDecorU6::WorldFrames(*DD), 1, 4);
			if (NF > 1 && Sp.IsValid())
			{
				FVis& DVis = Vis[DV];
				DVis.Frames.Add(Sp);
				const uint32 LookA = HashCombine(GetTypeHash(DD->Id), uint32(DD->Style));
				const uint32 LookB = HashCombine(DD->Primary.DWColor(), DD->Secondary.DWColor());
				for (int32 f = 1; f < NF; ++f)
				{
					const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::DecorU6, LookA, LookB, uint8(0x40 + f));
					const FFVSprite* Fr = Spr->FindCached(Key);
					if (!Fr)
					{
						const FFVImage Img = FVArtDecorU6::BuildWorldFrame(*DD, f);
						if (!Img.IsValid() || float(Img.W) != Sp.W || float(Img.H) != Sp.H) { break; }
						Fr = &Spr->AddCached(Key, Img, FString::Printf(TEXT("DecorWorld_%s_f%d"), *DD->Id.ToString(), f));
					}
					DVis.Frames.Add(*Fr);
				}
				if (DVis.Frames.Num() > 1) { DVis.FrameFps = 5.f; }
				else { DVis.Frames.Reset(); }
			}
			if (Slot == EFVDecorSlot::YardLantern || Slot == EFVDecorSlot::Fireplace || Slot == EFVDecorSlot::Chandelier)
			{
				FVector2D LPos = Slot == EFVDecorSlot::Chandelier ? Feet + FVector2D(0.f, Sz.Y * WS * 0.6f)
					: Feet - FVector2D(0.f, Sz.Y * WS * (Slot == EFVDecorSlot::Fireplace ? 0.35f : 0.8f));
				// the art's own light point (texels from the sprite's top-left) when it has one
				const FVector2D LP = FVArtDecorU6::LightPoint(*DD);
				if (LP.X >= 0.f && LP.Y >= 0.f && Vis.IsValidIndex(DV)) { LPos = Vis[DV].TopLeft() + LP * WS; }
				AddLight(LPos, Slot == EFVDecorSlot::Chandelier ? 90.f : 60.f, DD->Secondary, Slot != EFVDecorSlot::Chandelier,
					Slot == EFVDecorSlot::Fireplace ? 0.25f : 0.f);
			}
			break;
		}
		default: break;
		}
	}
}
