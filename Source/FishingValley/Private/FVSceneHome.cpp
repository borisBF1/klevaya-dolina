// UPDATE 2 (World module): the interactive objects of the scene — resource pickups, trader stalls, the order
// board, net spots (posts + nets in the water), dig spots, the house door, the house stations and decor — plus
// the auto-walk, the highlight / name plate, the pickup feedback and the house interior (wallpaper, floor, windows,
// aquarium fish, trophies, lamp and kitchen light, smokehouse smoke). Layout anchors come from FVSceneArt.cpp.

#include "FVScene.h"
#include "FVSceneArt.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"

using namespace FVSceneArt;
using namespace FVSceneRt;

namespace FVSceneHomeImpl
{
	constexpr float WorldSigPeriod = 0.25f;   // profile watch interval (seconds)
	constexpr float WalkGiveUp = 1.2f;        // auto-walk gives up after this long without progress
	constexpr float PickupLife = 0.9f;

	FORCEINLINE void HashAdd(uint32& H, uint32 V) { H = (H ^ V) * 16777619u; }

	/** Sprite size in texels, with a fallback while the art is not available. */
	FVector2D SizeOf(const FFVSprite& S, float FallbackW, float FallbackH)
	{
		return (S.IsValid() && S.W > 0.f && S.H > 0.f) ? FVector2D(S.W, S.H) : FVector2D(FallbackW, FallbackH);
	}

	/** Rect of a bottom-centre anchored sprite of Size texels. */
	FFVRect BaseRect(const FVector2D& Base, const FVector2D& Size)
	{
		return FFVRect(float(Base.X) - Size.X * WS * 0.5f, float(Base.Y) - Size.Y * WS, Size.X * WS, Size.Y * WS);
	}

	FFVRect Grow(const FFVRect& R, float D) { return FFVRect(R.X - D, R.Y - D, R.W + 2.f * D, R.H + 2.f * D); }

	FFVRect Union(const FFVRect& A, const FFVRect& B)
	{
		if (A.W <= 0.f || A.H <= 0.f) { return B; }
		if (B.W <= 0.f || B.H <= 0.f) { return A; }
		const float X0 = FMath::Min(A.X, B.X), Y0 = FMath::Min(A.Y, B.Y);
		return FFVRect(X0, Y0, FMath::Max(A.Right(), B.Right()) - X0, FMath::Max(A.Bottom(), B.Bottom()) - Y0);
	}

	FFVRect Intersect(const FFVRect& A, const FFVRect& B)
	{
		const float X0 = FMath::Max(A.X, B.X), Y0 = FMath::Max(A.Y, B.Y);
		const float X1 = FMath::Min(A.Right(), B.Right()), Y1 = FMath::Min(A.Bottom(), B.Bottom());
		return (X1 > X0 && Y1 > Y0) ? FFVRect(X0, Y0, X1 - X0, Y1 - Y0) : FFVRect();
	}

	/** Catch units of a net and its fill level 0..3 (NetWaterSprite). */
	int32 NetUnits(const FFVNetState& N)
	{
		int32 U = 0;
		for (const FFVInvStack& S : N.Catch) { U += FMath::Max(0, S.Count); }
		return U;
	}
	int32 NetFill(const FFVNetState& N)
	{
		const int32 U = NetUnits(N);
		if (U <= 0) { return 0; }
		const FFVItemDef* Def = FVDB::FindItem(N.NetItem);
		const int32 Cap = Def ? FMath::Max(1, Def->NetCapacity) : 10;
		return FMath::Clamp(FMath::CeilToInt(3.f * float(U) / float(Cap)), 1, 3);
	}

	int32 OfferedOrders(const UFVSaveGame* P)
	{
		if (!P) { return 0; }
		int32 N = 0;
		for (const FFVOrder& O : P->Orders) { if (O.State == uint8(EFVOrderState::Offered)) { ++N; } }
		return N;
	}

	FString StationName(EFVStation S)
	{
		if (const FFVStationDef* Def = FVDB::FindStation(FVDB::StationId(S)))
		{
			if (!Def->Name.IsEmpty()) { return Def->Name; }
		}
		static const TCHAR* Names[] = { TEXT("Кухня"), TEXT("Мастерская"), TEXT("Коптильня"), TEXT("Червятник"), TEXT("Аквариум"),
			TEXT("Ледник"), TEXT("Радио"), TEXT("Кровать"), TEXT("Стена трофеев"), TEXT("Сундук") };
		const int32 i = FMath::Clamp(int32(S), 0, int32(UE_ARRAY_COUNT(Names)) - 1);
		return Names[i];
	}

	/** "Кухня (не построена)" / "Червятник (не построен)" / "Радио (не построено)". */
	FString StationLabel(EFVStation S, int32 Level)
	{
		const FString Name = StationName(S);
		if (Level > 0) { return Name; }
		switch (S)
		{
		case EFVStation::Kitchen: case EFVStation::Workshop: case EFVStation::Smokehouse:
		case EFVStation::Bed: case EFVStation::TrophyWall:
			return Name + TEXT(" (не построена)");
		case EFVStation::Radio:
			return Name + TEXT(" (не построено)");
		default:
			return Name + TEXT(" (не построен)");
		}
	}

	FString StallLabel(const FFVTraderDef& Tr)
	{
		if (Tr.Id == FVDB::DefaultTraderId()) { return TEXT("Лавка Митрича"); }
		return Tr.ShopTitle.IsEmpty() ? FString(TEXT("Лавка")) : Tr.ShopTitle;
	}

	/** Water area of the aquarium sprite by level (fractions of the sprite rect). */
	FFVRect TankRect(const FFVRect& R, int32 Level)
	{
		float X = 0.1f, Y = 0.14f, W = 0.8f, H = 0.44f;
		if (Level <= 1) { X = 0.26f; Y = 0.34f; W = 0.48f; H = 0.42f; }
		else if (Level == 2) { X = 0.12f; Y = 0.16f; W = 0.76f; H = 0.42f; }
		return FFVRect(R.X + R.W * X, R.Y + R.H * Y, R.W * W, R.H * H);
	}

	/** Update 3: the verb of the "tap anywhere" prompt under the highlighted object's name plate. */
	FString InteractVerb(const FVScene& S, const FFVWorldObject& O, const UFVSaveGame* P)
	{
		switch (O.Kind)
		{
		case EFVObjKind::Resource: return TEXT("взять");
		case EFVObjKind::Stall: return TEXT("открыть лавку");
		case EFVObjKind::OrderBoard: return TEXT("открыть заказы");
		case EFVObjKind::NetSpot: return TEXT("осмотреть");
		case EFVObjKind::DigSpot: return TEXT("копать");
		case EFVObjKind::Door: return S.IsInterior() ? TEXT("выйти во двор") : TEXT("войти в дом");
		case EFVObjKind::DecorCatalog: return TEXT("открыть каталог");
		case EFVObjKind::Station:
		{
			const FFVStationDef* Def = FVDB::FindStation(O.Id);
			if (!Def) { return TEXT("открыть"); }
			if (P && !P->IsStationBuilt(Def->Station)) { return TEXT("построить"); }
			switch (Def->Station)
			{
			case EFVStation::Bed: return TEXT("лечь спать");
			case EFVStation::Kitchen: return TEXT("готовить");
			case EFVStation::Workshop: return TEXT("мастерить");
			case EFVStation::Smokehouse: return TEXT("коптить");
			case EFVStation::Radio: return TEXT("слушать");
			case EFVStation::Aquarium: case EFVStation::TrophyWall: return TEXT("смотреть");
			default: return TEXT("открыть");
			}
		}
		// update 6
		case EFVObjKind::Tree: return TEXT("рубить");
		case EFVObjKind::JunkPile: return TEXT("порыться");
		case EFVObjKind::Boulder: return TEXT("раскопать");
		case EFVObjKind::CaveRubble: return TEXT("разобрать завал");
		case EFVObjKind::Npc: return TEXT("поговорить");
		case EFVObjKind::BuildPlot: return TEXT("осмотреть");
		case EFVObjKind::YardBuilding: return TEXT("открыть");
		case EFVObjKind::GardenBed:
		{
			const int32 Stage = FVSceneU6Objects::BedStage(P, O.Index);
			return Stage < 0 ? TEXT("посадить") : (Stage >= 3 ? TEXT("собрать урожай") : TEXT("осмотреть"));
		}
		case EFVObjKind::Boat: return O.Label.Contains(TEXT("ремонт")) ? TEXT("чинить") : TEXT("отплыть");
		case EFVObjKind::Blockage: return TEXT("расчистить");
		case EFVObjKind::Exit: return O.Index == 1 ? TEXT("как открыть") : TEXT("идти");   // Р69: 1 = a closed place
		default: return TEXT("открыть");
		}
	}

	/** "Коснитесь — взять" (touch) / "Нажмите — открыть лавку" (mouse): a tap / click anywhere uses the highlighted object
	    in reach; out of reach: "… — подойти", or "Идём…" while the player already walks there. */
	FString InteractPrompt(const FVScene& S, const FFVWorldObject& O, bool bWalking, const UFVSaveGame* P)
	{
		const TCHAR* Act = FV::IsTouchUI() ? TEXT("Коснитесь") : TEXT("Нажмите");
		if (!S.IsInReach(O))
		{
			return bWalking ? FString(TEXT("Идём…")) : FString::Printf(TEXT("%s — подойти"), Act);
		}
		return FString::Printf(TEXT("%s — %s"), Act, *InteractVerb(S, O, P));
	}

	/** Sky colour seen through the house windows (top / bottom). */
	void SkyColors(float Night, float Clock, bool bRain, FLinearColor& OutTop, FLinearColor& OutBottom)
	{
		FLinearColor DayT = FV::Col(112, 176, 236), DayB = FV::Col(196, 230, 250);
		if (bRain) { DayT = FV::Col(116, 134, 156); DayB = FV::Col(160, 174, 188); }
		const float Warm = FMath::Max(Bump(Clock, 400.f, 100.f), Bump(Clock, 1170.f, 120.f)) * (bRain ? 0.4f : 1.f);
		DayT = FMath::Lerp(DayT, FV::Col(236, 150, 120), Warm * 0.6f);
		DayB = FMath::Lerp(DayB, FV::Col(255, 196, 130), Warm * 0.7f);
		OutTop = FMath::Lerp(DayT, FV::Col(14, 18, 48), Night);
		OutBottom = FMath::Lerp(DayB, FV::Col(34, 40, 82), Night);
	}

	/** Update 4b: the practice pond of the scene that built its layout last (there is one scene: UFVGame's). */
	const FVScene* GPondOwner = nullptr;
	FVPracticePond::FInfo GPond;

	void PublishPond(const FVScene& S, const FLayout& L)
	{
		GPondOwner = &S;
		GPond = FVPracticePond::FInfo();
		if (!L.bPond) { return; }
		GPond.bValid = true;
		GPond.Center = L.PondCenter;
		GPond.Radii = L.PondRadii;
		GPond.Plank = L.PondPlank;
		GPond.Spot = L.PondPlank.W > 0.f ? FVector2D(L.PondPlank.Center().X, L.PondPlank.Bottom() - 8.f) : L.PondCenter - FVector2D(0.f, L.PondRadii.Y + 4.f);
		const FFVRect Water(float(L.PondCenter.X - L.PondRadii.X) - 6.f, float(L.PondCenter.Y - L.PondRadii.Y) - 4.f, float(L.PondRadii.X) * 2.f + 12.f, float(L.PondRadii.Y) * 2.f + 10.f);
		GPond.Bounds = Union(Water, L.PondPlank);
	}
}

FVPracticePond::FInfo FVPracticePond::Get(const FVScene& Scene)
{
	return FVSceneHomeImpl::GPondOwner == &Scene ? FVSceneHomeImpl::GPond : FVPracticePond::FInfo();
}

using namespace FVSceneHomeImpl;

// =============================================================================================
// Building the objects from the layout anchors + the profile
// =============================================================================================
UFVSaveGame* FVScene::FImpl::Profile(const FVScene& S) const
{
	return S.Game ? S.Game->GetProfile() : nullptr;
}

uint32 FVScene::FImpl::WorldSignature(const FVScene& S) const
{
	const UFVSaveGame* P = Profile(S);
	if (!P || !Loc) { return 0u; }
	uint32 H = 2166136261u;
	const FName LocId = Loc->Id;
	HashAdd(H, uint32(S.Game ? S.Game->GetDay() : 0));
	for (int32 i = 0; i < L.ResourceSlots.Num(); ++i)
	{
		int32 Cnt = 0;
		const FName Id = P->ResourceAt(LocId, i, Cnt);
		HashAdd(H, GetTypeHash(Id));
		HashAdd(H, uint32(Cnt));
	}
	for (int32 i = 0; i < L.DigSpots.Num(); ++i) { HashAdd(H, P->IsDugToday(LocId, i) ? 11u : 13u); }
	for (int32 i = 0; i < L.NetSpots.Num(); ++i)
	{
		if (const FFVNetState* N = P->FindNet(LocId, i))
		{
			HashAdd(H, GetTypeHash(N->NetItem));
			HashAdd(H, N->bTorn ? 3u : 5u);
			HashAdd(H, uint32(NetFill(*N)));
			HashAdd(H, uint32(NetUnits(*N)));
		}
		else { HashAdd(H, 7u); }
	}
	if (L.bBoard) { HashAdd(H, uint32(OfferedOrders(P))); }
	if (L.bInterior)
	{
		for (int32 s = 0; s < int32(EFVStation::Count); ++s) { HashAdd(H, uint32(P->GetStationLevel(EFVStation(s)))); }
		HashAdd(H, P->SmokeFuel > 0.f ? 17u : 19u);
		HashAdd(H, uint32(P->Aquarium.Num()));
		for (const FFVInvStack& F : P->Aquarium) { HashAdd(H, GetTypeHash(F.ItemId)); }
		HashAdd(H, uint32(P->Trophies.Num()));
		for (const FFVTrophy& T : P->Trophies) { HashAdd(H, GetTypeHash(T.FishId)); }
		for (int32 d = 0; d < int32(EFVDecorSlot::Count); ++d) { HashAdd(H, GetTypeHash(P->GetEquippedDecor(EFVDecorSlot(d)))); }
	}
	// update 6: objects of big worlds and the pier boat (FVSceneU6Objects.cpp)
	HashAdd(H, FVSceneU6Objects::HashState(bBig ? BigDef.Get() : nullptr, L.bBoat, L.bInterior, P, LocId));
	return H;
}

void FVScene::FImpl::RebuildWorld(FVScene& S)
{
	Objects.Reset();
	Vis.Reset();
	NetRects.Reset();
	RtLights.Reset();
	RtSmokes.Reset();
	AquaFish.Reset();
	AquaVis = INDEX_NONE;
	AquaLevel = 0;
	WallTile = FFVSprite();
	FloorTile = FFVSprite();
	bArtPending = false;
	L.Blockers.SetNum(FMath::Clamp(BaseBlockers, 0, L.Blockers.Num()));
	WorldSig = WorldSignature(S);
	WorldSigTimer = WorldSigPeriod;
	PublishPond(S, L);   // update 4b (runs after every ApplyLayout: load / canvas change)
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
	/** Standard floor object: sprite standing on Base, a foot blocker, interaction spot in front of it. */
	auto FloorBlocker = [](const FVector2D& Base, const FVector2D& Size, float WFrac, float MaxH) -> FFVRect
	{
		const float BW = Size.X * WS * WFrac;
		const float BH = FMath::Clamp(Size.Y * WS * 0.35f, 8.f, MaxH);
		return FFVRect(float(Base.X) - BW * 0.5f, float(Base.Y) - BH, BW, BH + 2.f);
	};

	// ---------------------------------------------------------------- resource pickups
	for (int32 i = 0; i < L.ResourceSlots.Num(); ++i)
	{
		int32 Cnt = 0;
		const FName Id = P ? P->ResourceAt(LocId, i, Cnt) : NAME_None;
		if (Id.IsNone() || Cnt <= 0) { continue; }
		const FVector2D At = L.ResourceSlots[i];
		FString Label = FVDB::ItemName(Id);
		if (Cnt > 1) { Label += FString::Printf(TEXT(" x%d"), Cnt); }
		const int32 O = AddObj(EFVObjKind::Resource, i, Id, At, FFVRect(float(At.X) - 18.f, float(At.Y) - 36.f, 36.f, 40.f), 28.f, Label);
		const int32 V = AddVis(O, Spr ? Spr->ItemIcon(Id) : FFVSprite(), At, 3, FVector2D(0.5f, 1.f), float(At.Y));
		Vis[V].Style = 1;
		Vis[V].Count = FMath::Clamp(Cnt, 1, 2);
		Vis[V].Phase = float(i) * 1.7f;
	}

	// ---------------------------------------------------------------- trader stall
	if (L.bStall)
	{
		if (const FFVTraderDef* Tr = FVDB::TraderAt(LocId))
		{
			const FFVSprite Sp = Spr ? Spr->StallSprite(Tr->Id) : FFVSprite();
			const FVector2D Sz = SizeOf(Sp, 56.f, 48.f);
			const FVector2D Base = L.StallFeet;
			const int32 O = AddObj(EFVObjKind::Stall, 0, Tr->Id, Base + FVector2D(0.f, 14.f), BaseRect(Base, Sz), 36.f, StallLabel(*Tr));
			AddVis(O, Sp, Base, 3, FVector2D(0.5f, 1.f), float(Base.Y));
			Block(FloorBlocker(Base, Sz, 0.84f, 16.f));
		}
	}

	// ---------------------------------------------------------------- order board
	if (L.bBoard)
	{
		const int32 Papers = FMath::Clamp(OfferedOrders(P), 0, 3);
		const FFVSprite Sp = Spr ? Spr->OrderBoardSprite(Papers) : FFVSprite();
		const FVector2D Sz = SizeOf(Sp, 30.f, 34.f);
		const FVector2D Base = L.BoardFeet;
		const FString Label = Papers > 0 ? FString::Printf(TEXT("Доска заказов (%d)"), Papers) : FString(TEXT("Доска заказов"));
		const int32 O = AddObj(EFVObjKind::OrderBoard, 0, NAME_None, Base + FVector2D(0.f, 12.f), BaseRect(Base, Sz), 32.f, Label);
		AddVis(O, Sp, Base, 3, FVector2D(0.5f, 1.f), float(Base.Y));
		Block(FloorBlocker(Base, Sz, 0.72f, 10.f));
	}

	// ---------------------------------------------------------------- net spots: posts on the shore, the net in the water
	for (int32 i = 0; i < L.NetSpots.Num(); ++i)
	{
		const FFVNetState* N = P ? P->FindNet(LocId, i) : nullptr;
		const FFVSprite Post = Spr ? Spr->NetPostSprite(N != nullptr) : FFVSprite();
		const FVector2D Sz = SizeOf(Post, 22.f, 18.f);
		const FVector2D Base = L.NetSpots[i];
		FFVRect Hit = BaseRect(Base, Sz);
		FString Label = TEXT("Место для сети");
		FFVSprite NetSp;
		FFVRect NetR;
		if (N)
		{
			const FFVItemDef* Def = FVDB::FindItem(N->NetItem);
			const int32 Tier = Def ? FMath::Clamp(int32(Def->Tier), 1, 3) : 1;
			NetSp = Spr ? Spr->NetWaterSprite(Tier, NetFill(*N), N->bTorn) : FFVSprite();
			const FVector2D NSz = SizeOf(NetSp, 28.f, 14.f);
			NetR = FFVRect(float(Base.X) - NSz.X * WS * 0.5f, L.ShoreY + 2.f, NSz.X * WS, NSz.Y * WS);
			NetRects.Add(NetR);
			Hit = Union(Hit, NetR);
			const int32 Units = NetUnits(*N);
			if (N->bTorn) { Label = TEXT("Порванная сеть"); }
			else if (Units > 0) { Label = FString::Printf(TEXT("Сеть — улов: %d"), Units); }
			else { Label = TEXT("Сеть"); }
		}
		const int32 O = AddObj(EFVObjKind::NetSpot, i, NAME_None, Base - FVector2D(0.f, 14.f), Hit, 32.f, Label);
		AddVis(O, Post, Base, 3, FVector2D(0.5f, 1.f), float(Base.Y));
		if (N) { AddVis(O, NetSp, FVector2D(float(Base.X), L.ShoreY + 2.f), 1, FVector2D(0.5f, 0.f), L.ShoreY); }
		Block(FFVRect(float(Base.X) - Sz.X * WS * 0.35f, float(Base.Y) - 6.f, Sz.X * WS * 0.7f, 8.f));
	}

	// ---------------------------------------------------------------- dig spots (soft soil, flat on the ground)
	for (int32 i = 0; i < L.DigSpots.Num(); ++i)
	{
		const bool bDug = P && P->IsDugToday(LocId, i);
		const FFVSprite Sp = Spr ? Spr->DigSpotSprite(bDug) : FFVSprite();
		const FVector2D Sz = SizeOf(Sp, 18.f, 12.f);
		const FVector2D C = L.DigSpots[i];
		const float HW = FMath::Max(Sz.X * WS * 0.5f, 20.f), HH = FMath::Max(Sz.Y * WS * 0.5f, 14.f);
		const int32 O = AddObj(EFVObjKind::DigSpot, i, NAME_None, C, FFVRect(float(C.X) - HW, float(C.Y) - HH, HW * 2.f, HH * 2.f), 30.f,
			bDug ? FString(TEXT("Вскопанная земля")) : FString(TEXT("Мягкая земля")));
		const int32 V = AddVis(O, Sp, C, 0, FVector2D(0.5f, 0.5f), float(C.Y));
		Vis[V].bShadow = false;
	}

	// ---------------------------------------------------------------- the house door (baked into the background)
	if (L.bDoor)
	{
		AddObj(EFVObjKind::Door, 0, NAME_None, L.DoorFeet, Grow(L.DoorRect, 3.f), 30.f,
			L.bInterior ? FString(TEXT("Дверь во двор")) : FString(TEXT("Дверь дома")));
	}

	// ---------------------------------------------------------------- house interior: stations, dresser, decor
	if (L.bInterior)
	{
		// wallpaper & floor tiles
		auto EquippedDef = [P](EFVDecorSlot Slot) -> const FFVDecorDef*
		{
			return P ? FVDB::FindDecor(P->GetEquippedDecor(Slot)) : nullptr;
		};
		if (const FFVDecorDef* W = EquippedDef(EFVDecorSlot::Wallpaper))
		{
			WallCol = W->Primary;
			if (Spr) { WallTile = Spr->DecorWorld(*W); }
		}
		else { WallCol = FColor(170, 120, 70); }
		if (const FFVDecorDef* Fl = EquippedDef(EFVDecorSlot::Floor))
		{
			FloorCol = Fl->Primary;
			if (Spr) { FloorTile = Spr->DecorWorld(*Fl); }
		}
		else { FloorCol = FColor(150, 100, 60); }

		// stations
		if (L.Stations.Num() == int32(EFVStation::Count))
		{
			for (int32 s = 0; s < int32(EFVStation::Count); ++s)
			{
				const EFVStation St = EFVStation(s);
				const int32 Level = P ? FMath::Max(0, P->GetStationLevel(St)) : 0;
				const FFVSprite Sp = Spr ? Spr->StationSprite(St, Level) : FFVSprite();
				const FVector2D Sz = SizeOf(Sp, 32.f, 32.f);
				const FName Id = FVDB::StationId(St);
				const FString Label = StationLabel(St, Level);
				const FVector2D Anchor = L.Stations[s];
				if (!IsPlaced(Anchor)) { continue; }   // update 6: a big house without this station (moved to the yard)
				if (St == EFVStation::TrophyWall && (Level > 0 || bBig))
				{
					// mounted on the wall (anchor = the board's centre); the player uses it from the floor below
					const FFVRect R(float(Anchor.X) - Sz.X * WS * 0.5f, float(Anchor.Y) - Sz.Y * WS * 0.5f, Sz.X * WS, Sz.Y * WS);
					FVector2D Use(float(Anchor.X), L.WalkTop + 14.f);
					if (bBig)
					{
						// update 6: the big house — the nearest walkable floor straight below the board
						Use = FVector2D(float(Anchor.X), R.Bottom() + 10.f);
						for (float DY = 0.f; DY < 240.f; DY += 4.f)
						{
							const FVector2D T(float(Anchor.X), R.Bottom() + 4.f + DY);
							if (FVSceneRt::IsWalkable(L, T)) { Use = T + FVector2D(0.f, 6.f); break; }
						}
					}
					const int32 O = AddObj(EFVObjKind::Station, s, Id, Use, R, 34.f, Label);
					const int32 V = AddVis(O, Sp, Anchor, 2, FVector2D(0.5f, 0.5f), float(Anchor.Y));
					Vis[V].bShadow = false;
					// plaques: at the wall's slot centres (TrophySlotCenter: 6 on L1, 12 on the L2 board)
					if (P && Spr && Level > 0)
					{
						const int32 N = FMath::Min(P->Trophies.Num(), Level >= 2 ? 12 : 6);
						const float TexToPx = Sp.IsValid() && Sp.W > 0.f ? R.W / Sp.W : WS;
						for (int32 k = 0; k < N; ++k)
						{
							const FFVFishDef* Fish = FVDB::FindFish(P->Trophies[k].FishId);
							if (!Fish) { continue; }
							const FFVSprite Pl = Spr->TrophyPlaque(*Fish);
							const FVector2D PSz = SizeOf(Pl, 16.f, 16.f);
							// one slot = 16x12 texels of the wall
							const float Sc = FMath::Clamp(FMath::FloorToFloat(FMath::Min(16.f * TexToPx / PSz.X, 12.f * TexToPx / PSz.Y) * 2.f) * 0.5f, 0.5f, WS);
							const FVector2D C = FVector2D(R.X, R.Y) + UFVSprites::TrophySlotCenter(k) * TexToPx;
							const int32 PV = AddVis(INDEX_NONE, Pl, C, 2, FVector2D(0.5f, 0.5f), float(C.Y));
							Vis[PV].Scale = Sc;
							Vis[PV].bShadow = false;
						}
					}
					continue;
				}
				if (St == EFVStation::IceCellar && bBig)
				{
					// update 6: the ice cellar of the big house is a flat floor hatch (top view): no blocker, no shadow
					// (the anchor is the hatch's feet = bottom-centre, like every station of the house)
					const FFVRect R = BaseRect(Anchor, Sz);
					const int32 O = AddObj(EFVObjKind::Station, s, Id, Anchor + FVector2D(0.f, 6.f), R, 34.f, Label);
					const int32 V = AddVis(O, Sp, Anchor, 0, FVector2D(0.5f, 1.f), float(Anchor.Y));
					Vis[V].bShadow = false;
					continue;
				}
				// standing on the floor (the trophy wall construction site stands against the wall)
				const FVector2D Base = (St == EFVStation::TrophyWall) ? FVector2D(float(Anchor.X), L.WalkTop + 4.f) : Anchor;
				const int32 O = AddObj(EFVObjKind::Station, s, Id, Base + FVector2D(0.f, 14.f), BaseRect(Base, Sz), 34.f, Label);
				const int32 V = AddVis(O, Sp, Base, 3, FVector2D(0.5f, 1.f), float(Base.Y));
				Block(FloorBlocker(Base, Sz, 0.84f, 22.f));
				if (St == EFVStation::Aquarium && Level > 0 && P)
				{
					Vis[V].Style = 2;
					AquaVis = V;
					AquaLevel = Level;
					const FFVRect Tank = TankRect(BaseRect(Base, Sz), Level);
					const int32 N = FMath::Min(P->Aquarium.Num(), 10);
					FRandomStream R(int32(GetTypeHash(LocId)) + 77);
					for (int32 k = 0; k < N; ++k)
					{
						const FFVFishDef* Fish = FVDB::FindFish(P->Aquarium[k].ItemId);
						if (!Fish || !Spr) { continue; }
						FAquaFish F;
						F.S = Spr->Fish(*Fish);
						const FVector2D FSz = SizeOf(F.S, 16.f, 8.f);
						// update 5 (§4): a sprite wider than 36 texels (the sturgeon, the fog serpent) may shrink to 0.4
						// when at 0.5 it would not leave 4 px on each side of the tank (the level 1 jar)
						const float MinScale = (FSz.X > 36.f && FSz.X * 0.5f > Tank.W - 8.f) ? 0.4f : 0.5f;
						F.Scale = FMath::Clamp(FMath::Min(Tank.W * 0.34f / FSz.X, Tank.H * 0.42f / FSz.Y), MinScale, 1.f);
						F.Phase = R.FRandRange(0.f, 6.28f);
						F.Speed = R.FRandRange(0.35f, 0.8f);
						F.YFrac = N > 1 ? (float(k) + R.FRandRange(0.1f, 0.9f)) / float(N) : 0.5f;
						AquaFish.Add(F);
					}
				}
				if (St == EFVStation::Kitchen && Level > 0)
				{
					FRtLight Lt;
					Lt.Pos = Base - FVector2D(0.f, Sz.Y * WS * 0.4f);
					Lt.Radius = 50.f + 8.f * float(Level);
					Lt.Color = FColor(255, 150, 60);
					Lt.bFlicker = true;
					Lt.DayInt = 0.3f;
					RtLights.Add(Lt);
				}
				if (St == EFVStation::Smokehouse && Level > 0 && P && P->SmokeFuel > 0.f)
				{
					RtSmokes.Add(Base - FVector2D(-Sz.X * WS * 0.18f, Sz.Y * WS - 4.f));
					FRtLight Lt;
					Lt.Pos = Base - FVector2D(0.f, Sz.Y * WS * 0.25f);
					Lt.Radius = 30.f;
					Lt.Color = FColor(255, 120, 50);
					Lt.bFlicker = true;
					Lt.DayInt = 0.2f;
					RtLights.Add(Lt);
				}
			}
		}

		// dresser with the decor catalog
		if (IsPlaced(L.DresserFeet))
		{
			const FFVSprite Sp = Spr ? Spr->DresserSprite() : FFVSprite();
			const FVector2D Sz = SizeOf(Sp, 28.f, 30.f);
			const FVector2D Base = L.DresserFeet;
			const int32 O = AddObj(EFVObjKind::DecorCatalog, 0, NAME_None, Base + FVector2D(0.f, 14.f), BaseRect(Base, Sz), 34.f, TEXT("Комод: каталог декора"));
			AddVis(O, Sp, Base, 3, FVector2D(0.5f, 1.f), float(Base.Y));
			Block(FloorBlocker(Base, Sz, 0.84f, 20.f));
		}

		// decor props
		if (Spr)
		{
			if (const FFVDecorDef* Rug = IsPlaced(L.RugCenter) ? EquippedDef(EFVDecorSlot::Rug) : nullptr)
			{
				const int32 V = AddVis(INDEX_NONE, Spr->DecorWorld(*Rug), L.RugCenter, 0, FVector2D(0.5f, 0.5f), float(L.RugCenter.Y));
				Vis[V].bShadow = false;
			}
			if (const FFVDecorDef* Pt = IsPlaced(L.PaintingCenter) ? EquippedDef(EFVDecorSlot::Painting) : nullptr)
			{
				const int32 V = AddVis(INDEX_NONE, Spr->DecorWorld(*Pt), L.PaintingCenter, 2, FVector2D(0.5f, 0.5f), float(L.PaintingCenter.Y));
				Vis[V].bShadow = false;
			}
			if (const FFVDecorDef* Cu = EquippedDef(EFVDecorSlot::Curtains))
			{
				const FFVSprite CS = Spr->DecorWorld(*Cu);
				// update 6: the big house hangs curtains only at its Curtains anchors (two windows), not at every glass
				for (const FVector2D& CP : L.CurtainSpots)
				{
					const int32 V = AddVis(INDEX_NONE, CS, CP, 2, FVector2D(0.5f, 1.f), float(CP.Y));
					Vis[V].bShadow = false;
				}
				if (L.CurtainSpots.Num() == 0) for (const FFVRect& W : L.Windows)
				{
					const int32 V = AddVis(INDEX_NONE, CS, FVector2D(W.Center().X, W.Y - 6.f), 2, FVector2D(0.5f, 0.f), W.Y);
					Vis[V].bShadow = false;
				}
			}
			if (const FFVDecorDef* Pl = IsPlaced(L.PlantFeet) ? EquippedDef(EFVDecorSlot::Plant) : nullptr)
			{
				const FFVSprite PS = Spr->DecorWorld(*Pl);
				AddVis(INDEX_NONE, PS, L.PlantFeet, 3, FVector2D(0.5f, 1.f), float(L.PlantFeet.Y));
				Block(FloorBlocker(L.PlantFeet, SizeOf(PS, 14.f, 20.f), 0.6f, 8.f));
			}
			if (const FFVDecorDef* Lp = IsPlaced(L.LampFeet) ? EquippedDef(EFVDecorSlot::Lamp) : nullptr)
			{
				const FFVSprite LS = Spr->DecorWorld(*Lp);
				const FVector2D LSz = SizeOf(LS, 12.f, 24.f);
				AddVis(INDEX_NONE, LS, L.LampFeet, 3, FVector2D(0.5f, 1.f), float(L.LampFeet.Y));
				Block(FloorBlocker(L.LampFeet, LSz, 0.6f, 8.f));
				FRtLight Lt;
				Lt.Pos = L.LampFeet - FVector2D(0.f, LSz.Y * WS * 0.78f);
				Lt.Radius = 64.f + 10.f * float(Lp->Style);
				Lt.Color = Lp->Secondary;
				Lt.bFlicker = Lp->Style <= 1;   // candle / kerosene flame
				Lt.DayInt = 0.f;
				RtLights.Add(Lt);
			}
		}
	}

	// update 6: trees, villagers, yard buildings, beds, the boat... of big worlds and the pier boat (FVSceneU6Objects.cpp)
	RebuildWorldU6(S);

	// every interaction spot must be reachable
	for (FFVWorldObject& O : Objects)
	{
		if (!IsWalkable(L, O.Feet)) { O.Feet = FindWalkable(O.Feet); }
	}
}

void FVScene::FImpl::RebuildKeepingState(FVScene& S)
{
	FFVWorldObject WalkKey;
	bool bWalkOnObj = false;
	if (bWalk)
	{
		for (const FFVWorldObject& O : Objects)
		{
			if (FVector2D::Distance(O.Feet, WalkTarget) < 1.f) { WalkKey = O; bWalkOnObj = true; break; }
		}
	}
	RebuildWorld(S);
	if (bWalk && bWalkOnObj)
	{
		if (const FFVWorldObject* O = S.FindObject(WalkKey.Kind, WalkKey.Index, WalkKey.Id)) { WalkTarget = O->Feet; }
	}
	if (!IsWalkable(L, S.PlayerPos) && IsWalkable(L, FindWalkable(S.PlayerPos))) { S.PlayerPos = FindWalkable(S.PlayerPos); }
}

// =============================================================================================
// Tick: profile watch, pickup feedback, smoke, auto-walk
// =============================================================================================
void FVScene::FImpl::TickWorld(FVScene& S, float Dt)
{
	WorldSigTimer -= Dt;
	if (WorldSigTimer <= 0.f)
	{
		WorldSigTimer = WorldSigPeriod;
		const bool bArtNow = bArtPending && UFVSprites::Get() != nullptr;
		if (bArtNow || WorldSignature(S) != WorldSig) { RebuildKeepingState(S); }
	}
	// update 6: idle animation of the villagers (2 frames)
	if (U6Objects.IsValid() && U6Objects->NpcVis.Num() > 0)
	{
		FFVSceneU6State& St6 = *U6Objects;
		St6.NpcAnimTime += Dt;
		const int32 Frame = int32(St6.NpcAnimTime * 1.6f) & 1;
		if (Frame != St6.NpcFrame)
		{
			St6.NpcFrame = Frame;
			if (UFVSprites* Spr = UFVSprites::Get())
			{
				for (int32 k = 0; k < St6.NpcVis.Num() && k < St6.NpcIds.Num(); ++k)
				{
					const int32 VI = St6.NpcVis[k];
					if (!Vis.IsValidIndex(VI)) { continue; }
					const FFVSprite& Sp = Spr->NpcSprite(St6.NpcIds[k], (Frame + k) & 1);
					if (Sp.IsValid()) { Vis[VI].S = Sp; }
				}
			}
		}
	}
	if (PickupAge >= 0.f)
	{
		PickupAge += Dt;
		if (PickupAge > PickupLife) { PickupAge = -1.f; }
	}
	if (RtSmokes.Num() > 0)
	{
		NextRtSmoke -= Dt;
		if (NextRtSmoke <= 0.f)
		{
			NextRtSmoke = 0.4f;
			for (const FVector2D& Src : RtSmokes)
			{
				FParticle Pt;
				Pt.P = Src + FVector2D(Rng.FRandRange(-2.f, 2.f), 0.f);
				Pt.V = FVector2D(Rng.FRandRange(-3.f, 5.f), -Rng.FRandRange(12.f, 18.f));
				Pt.Life = Rng.FRandRange(2.2f, 3.2f);
				Pt.Size = Rng.FRandRange(2.5f, 4.f);
				Pt.C = FV::Col(200, 196, 190);
				Pt.Kind = EPart::Smoke;
				Particles.Add(Pt);
			}
		}
	}
}

FVector2D FVScene::FImpl::WalkSteer(const FVScene& S, float Dt)
{
	if (!bWalk) { return FVector2D::ZeroVector; }
	const FVector2D To = WalkTarget - S.PlayerPos;
	const float Dist = float(To.Size());
	if (Dist <= FMath::Max(4.f, WalkReach * 0.8f))
	{
		bWalk = false;
		bWalkReached = true;
		return FVector2D::ZeroVector;
	}
	const FVector2D Dir = To / Dist;
	const float Step = FMath::Max(4.f, FV::PlayerSpeed * FMath::Max(Dt, 1.f / 60.f) * 2.f);
	// straight at the target, else the smallest detour (keeps turning to the same side around an obstacle)
	static const float Angles[] = { 0.f, 30.f, 60.f, 90.f, 120.f, 150.f };
	for (float A : Angles)
	{
		for (int32 Side = 0; Side < (A == 0.f ? 1 : 2); ++Side)
		{
			const float Sgn = Side == 0 ? WalkSide : -WalkSide;
			const float Rad = FMath::DegreesToRadians(A * Sgn);
			const float C = FMath::Cos(Rad), Sn = FMath::Sin(Rad);
			const FVector2D D2(Dir.X * C - Dir.Y * Sn, Dir.X * Sn + Dir.Y * C);
			if (IsWalkable(L, S.PlayerPos + D2 * Step) && IsWalkable(L, S.PlayerPos + D2 * (Step * 0.5f)))
			{
				if (A > 0.f) { WalkSide = Sgn; }
				return D2;
			}
		}
	}
	return Dir;
}

void FVScene::FImpl::UpdateWalkProgress(const FVScene& S, float Dt)
{
	if (!bWalk) { return; }
	const float Dist = float(FVector2D::Distance(S.PlayerPos, WalkTarget));
	if (Dist <= FMath::Max(4.f, WalkReach * 0.8f))
	{
		bWalk = false;
		bWalkReached = true;
		return;
	}
	if (Dist < WalkBest - 1.f)
	{
		WalkBest = Dist;
		WalkStuck = 0.f;
		return;
	}
	WalkStuck += Dt;
	if (Dist <= WalkReach && WalkStuck > 0.25f)
	{
		// cannot get closer but already in reach
		bWalk = false;
		bWalkReached = true;
	}
	else if (WalkStuck > WalkGiveUp)
	{
		bWalk = false;
		bWalkFailed = true;
	}
}

bool FVScene::FImpl::IsHighlighted(int32 ObjIndex) const
{
	if (!bHighlight || !Objects.IsValidIndex(ObjIndex)) { return false; }
	const FFVWorldObject& O = Objects[ObjIndex];
	return O.Kind == Highlight.Kind && O.Index == Highlight.Index && O.Id == Highlight.Id;
}

// =============================================================================================
// Drawing
// =============================================================================================
void FVScene::FImpl::DrawInteriorBack(FVDraw& Dr, const FVScene& S)
{
	const FFVRect Scr = Dr.ScreenRect();
	auto Tiles = [&](const FFVRect& Area, const FFVSprite& T, const FColor& Fallback)
	{
		const FFVRect R = Intersect(Area, Scr);
		if (R.W <= 0.f) { return; }
		if (!T.IsValid() || T.W <= 0.f || T.H <= 0.f)
		{
			Dr.Rect(R, FV::Col(Fallback));
			return;
		}
		const float TW = T.W * WS, TH = T.H * WS;
		// tiles are aligned to the background, so they never swim when the canvas moves
		const float X0 = float(L.Origin.X) + FMath::FloorToFloat((R.X - float(L.Origin.X)) / TW) * TW;
		const float Y0 = float(L.Origin.Y) + FMath::FloorToFloat((R.Y - float(L.Origin.Y)) / TH) * TH;
		const FVector2D UVSpan = T.UV1 - T.UV0;
		for (float Y = Y0; Y < R.Bottom(); Y += TH)
		{
			for (float X = X0; X < R.Right(); X += TW)
			{
				const float CX0 = FMath::Max(X, R.X), CY0 = FMath::Max(Y, R.Y);
				const float CX1 = FMath::Min(X + TW, R.Right()), CY1 = FMath::Min(Y + TH, R.Bottom());
				if (CX1 <= CX0 || CY1 <= CY0) { continue; }
				const FVector2D U0((CX0 - X) / TW, (CY0 - Y) / TH), U1((CX1 - X) / TW, (CY1 - Y) / TH);
				Dr.Texture(T.Tex, FFVRect(CX0, CY0, CX1 - CX0, CY1 - CY0), T.UV0 + UVSpan * U0, T.UV0 + UVSpan * U1);
			}
		}
	};
	Tiles(L.WallRect, WallTile, WallCol);
	Tiles(L.FloorRect, FloorTile, FloorCol);

	// gentle shading: the wall darkens towards the ceiling, the floor towards the front of the room
	const FFVRect WallVis = Intersect(L.WallRect, Scr);
	if (WallVis.W > 0.f) { Dr.GradientV(L.WallRect, FLinearColor(0.08f, 0.04f, 0.02f, 0.22f), FLinearColor(0.08f, 0.04f, 0.02f, 0.f), 12); }
	const FFVRect FloorVis = Intersect(L.FloorRect, Scr);
	if (FloorVis.W > 0.f) { Dr.GradientV(FloorVis, FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.06f, 0.03f, 0.02f, 0.16f), 12); }

	// the sky behind the window glass
	const float Night = S.Game ? FMath::Clamp(S.Game->GetNightFactor(), 0.f, 1.f) : 0.f;
	const float Clock = S.Game ? S.Game->GetClockMinutes() : 720.f;
	const bool bRain = S.Game && S.Game->GetWeather() == EFVWeather::Rain;
	FLinearColor Top, Bottom;
	SkyColors(Night, Clock, bRain, Top, Bottom);
	for (int32 w = 0; w < L.Windows.Num(); ++w)
	{
		const FFVRect& G = L.Windows[w];
		Dr.GradientV(G, Top, Bottom, 8);
		// distant treetops
		const FLinearColor Trees = FMath::Lerp(FV::Col(70, 132, 70), FV::Col(16, 30, 30), Night);
		for (float X = G.X; X < G.Right(); X += 4.f)
		{
			const float Hh = Snap(6.f + 4.f * FMath::Sin(X * 0.21f + float(w) * 2.f) + 2.f * FMath::Sin(X * 0.57f), 2.f);
			Dr.Rect(FFVRect(X, G.Bottom() - Hh, FMath::Min(4.f, G.Right() - X), Hh), Trees);
		}
		if (Night > 0.5f)
		{
			// a few stars
			for (int32 k = 0; k < 5; ++k)
			{
				const float Fx = FMath::Frac(FMath::Sin(float(k * 7 + w * 13) * 12.9898f) * 43758.5453f);
				const float Fy = FMath::Frac(FMath::Sin(float(k * 11 + w * 5) * 78.233f) * 12543.123f);
				const float Tw = 0.5f + 0.5f * FMath::Sin(Time * 2.f + float(k) * 1.7f);
				Dr.Rect(FFVRect(Snap(G.X + 2.f + Fx * (G.W - 6.f), 2.f), Snap(G.Y + 2.f + Fy * G.H * 0.5f, 2.f), 2.f, 2.f),
					FLinearColor(1.f, 1.f, 0.9f, (Night - 0.5f) * 2.f * Tw));
			}
		}
		else if (!bRain)
		{
			// a small drifting cloud
			const float CX = G.X + FMath::Fmod(Time * 3.f + float(w) * 23.f, G.W + 24.f) - 12.f;
			const FFVRect Cl = Intersect(FFVRect(Snap(CX, 2.f), G.Y + 6.f, 14.f, 4.f), G);
			if (Cl.W > 0.f) { Dr.Rect(Cl, FLinearColor(1.f, 1.f, 1.f, 0.7f * (1.f - Night))); }
		}
		if (bRain)
		{
			for (int32 k = 0; k < 6; ++k)
			{
				const float Fx = FMath::Frac(float(k) * 0.37f + float(w) * 0.13f);
				const float Fy = FMath::Frac(Time * 1.6f + float(k) * 0.29f);
				const FVector2D A(G.X + 3.f + Fx * (G.W - 6.f), G.Y + Fy * (G.H - 8.f));
				Dr.Line(A, A + FVector2D(-1.f, 6.f), FLinearColor(0.85f, 0.9f, 1.f, 0.55f), 1.f);
			}
		}
	}
}

void FVScene::FImpl::DrawWorldLayer(FVDraw& Dr, const FVScene& S, uint8 Layer)
{
	for (int32 i = 0; i < Vis.Num(); ++i)
	{
		if (Vis[i].Layer == Layer) { DrawVis(Dr, S, i); }
	}
	if (Layer == 2)
	{
		// the door is part of the background: a warm pulsing frame when highlighted
		for (int32 k = 0; k < Objects.Num(); ++k)
		{
			if (Objects[k].Kind != EFVObjKind::Door || !IsHighlighted(k)) { continue; }
			const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 5.f);
			const FFVRect R = Grow(L.DoorRect, 1.f);
			Dr.Glow(R.Center(), FMath::Max(R.W, R.H) * 0.8f, FLinearColor(0.45f, 0.38f, 0.18f, 0.4f + 0.2f * Pulse));
			Dr.Frame(R, FLinearColor(1.f, 0.88f, 0.5f, 0.55f + 0.35f * Pulse), 2.f);
		}
	}
}

void FVScene::FImpl::DrawVis(FVDraw& Dr, const FVScene& S, int32 VisIndex)
{
	if (!Vis.IsValidIndex(VisIndex)) { return; }
	const FVis& V = Vis[VisIndex];
	if (!V.S.IsValid()) { return; }
	const float T = Time;
	const bool bHi = IsHighlighted(V.Obj);
	const float Pulse = 0.5f + 0.5f * FMath::Sin(T * 5.f);
	const FFVSprite& Sh = D(EDecor::Shadow);

	if (V.Style == 1)
	{
		// ---- resource pickup: bobbing icon with a shadow and an occasional sparkle
		const float Bob = Snap((0.5f + 0.5f * FMath::Sin(T * 2.6f + V.Phase)) * 3.f, 1.f);
		const FFVSprite& ShS = D(EDecor::ShadowSmall);
		if (ShS.IsValid()) { Dr.SpriteCentered(ShS, V.Pos + FVector2D(0.f, -1.f), WS * (1.2f - Bob * 0.05f), FLinearColor(1.f, 1.f, 1.f, 0.85f)); }
		if (bHi) { Dr.Ring(V.Pos + FVector2D(0.f, -1.f), 15.f + Pulse * 2.f, 5.f + Pulse, FLinearColor(1.f, 0.9f, 0.5f, 0.55f + 0.35f * Pulse), 1.5f); }
		const FVector2D C = V.Pos + FVector2D(0.f, -V.S.H * WS * 0.5f - 2.f - Bob);
		if (V.Count > 1) { Dr.SpriteCentered(V.S, Snap2(C + FVector2D(8.f, 3.f), 1.f), WS); }
		if (bHi) { Dr.Glow(C, 26.f, FLinearColor(0.5f, 0.42f, 0.2f, 0.5f)); }
		Dr.SpriteCentered(V.S, Snap2(C, 1.f), WS);
		if (bHi) { Dr.SpriteAdditive(V.S, Snap2(C, 1.f), WS, FLinearColor(1.f, 0.92f, 0.7f, 0.12f + 0.1f * Pulse)); }
		const float F = FMath::Frac(T * 0.42f + V.Phase * 0.13f);
		if (F < 0.14f)
		{
			const float A = FMath::Sin(F / 0.14f * UE_PI);
			const FVector2D P = Snap2(C + FVector2D(V.S.W * WS * 0.3f, -V.S.H * WS * 0.3f), 1.f);
			Dr.Rect(FFVRect(P.X - 1.f, P.Y - 4.f, 2.f, 8.f), FLinearColor(1.f, 1.f, 0.85f, A));
			Dr.Rect(FFVRect(P.X - 4.f, P.Y - 1.f, 8.f, 2.f), FLinearColor(1.f, 1.f, 0.85f, A));
		}
		return;
	}

	// ---- sprite (stall, board, net post / net, dig spot, station, dresser, decor)
	FVector2D TL = V.TopLeft();
	if (V.Layer == 1) { TL.Y += Snap(FMath::Sin(T * 1.4f + V.Phase) * 1.2f, 1.f); }   // the net sways on the water
	const float SW = V.S.W * V.Scale, SH = V.S.H * V.Scale;
	const FVector2D Center = TL + FVector2D(SW * 0.5f, SH * 0.5f);
	if (V.bShadow && V.Layer == 3 && Sh.IsValid())
	{
		Dr.SpriteCentered(Sh, V.Pos + FVector2D(2.f, -1.f), WS * FMath::Clamp(V.S.W / 14.f, 0.6f, 2.6f), FLinearColor(1.f, 1.f, 1.f, 0.8f));
	}
	if (bHi)
	{
		if (V.Layer == 3 || V.Layer == 0)
		{
			const float RX = FMath::Clamp(SW * 0.45f, 12.f, 70.f);
			Dr.Ring(V.Layer == 0 ? V.Pos : V.Pos + FVector2D(0.f, -1.f), RX + Pulse * 2.f, FMath::Max(5.f, RX * 0.28f) + Pulse, FLinearColor(1.f, 0.9f, 0.5f, 0.5f + 0.35f * Pulse), 1.5f);
		}
		Dr.Glow(Center, FMath::Clamp(FMath::Max(SW, SH) * 0.7f, 20.f, 90.f), FLinearColor(0.45f, 0.38f, 0.18f, 0.45f));
	}
	{
		// update 6: animated decor (yard lantern flame, weather vane...) cycles its frames
		const int32 NF = V.Frames.Num();
		const FFVSprite* Cur = &V.S;
		if (NF > 1 && V.FrameFps > 0.f)
		{
			const FFVSprite& Fr = V.Frames[FMath::Abs(FMath::FloorToInt((T + V.Phase) * V.FrameFps)) % NF];
			if (Fr.IsValid()) { Cur = &Fr; }
		}
		Dr.Sprite(*Cur, Snap2(TL, 1.f), V.Scale);
	}
	if (V.Style == 2 && VisIndex == AquaVis)
	{
		// aquarium: the fish swim to and fro behind the glass
		const FFVRect Tank = TankRect(FFVRect(float(TL.X), float(TL.Y), SW, SH), AquaLevel);
		for (int32 k = 0; k < AquaFish.Num(); ++k)
		{
			const FAquaFish& F = AquaFish[k];
			if (!F.S.IsValid()) { continue; }
			const float FW = F.S.W * F.Scale, FH = F.S.H * F.Scale;
			const float Half = FMath::Max(0.f, (Tank.W - FW) * 0.5f);
			const float Ph = T * F.Speed + F.Phase;
			const float X = Tank.Center().X + FMath::Sin(Ph) * Half;
			const bool bRight = FMath::Cos(Ph) > 0.f;
			const float Y = Tank.Y + FH * 0.5f + FMath::Max(0.f, Tank.H - FH) * F.YFrac + FMath::Sin(T * 1.7f + F.Phase * 3.f) * 1.5f;
			Dr.SpriteCentered(F.S, Snap2(FVector2D(X, Y), 1.f), F.Scale, FLinearColor(0.92f, 0.97f, 1.f, 0.95f), !bRight);
		}
		// rising bubbles
		for (int32 k = 0; k < 3; ++k)
		{
			const float Fr = FMath::Frac(T * 0.5f + float(k) * 0.33f);
			const float BX = Tank.X + Tank.W * (0.2f + 0.3f * float(k)) + FMath::Sin(T * 3.f + float(k)) * 2.f;
			const float BY = Tank.Bottom() - Fr * Tank.H;
			Dr.Rect(FFVRect(Snap(BX), Snap(BY), 2.f, 2.f), FLinearColor(0.9f, 0.97f, 1.f, 0.7f * (1.f - Fr)));
		}
	}
	if (bHi) { Dr.SpriteAdditive(V.S, Center, V.Scale, FLinearColor(1.f, 0.92f, 0.7f, 0.1f + 0.08f * Pulse)); }
}

void FVScene::FImpl::DrawWorldAfter(FVDraw& Dr, const FVScene& S)
{
	if (PickupAge < 0.f || !PickupIcon.IsValid()) { return; }
	// the picked item hops up from the ground to above the head, then floats up and fades
	const FVector2D Head = S.GetHeadTop() - FVector2D(0.f, 12.f);
	const float Up = FMath::Clamp(PickupAge / 0.32f, 0.f, 1.f);
	const float Ease = 1.f - (1.f - Up) * (1.f - Up);
	FVector2D P = FMath::Lerp(PickupFrom - FVector2D(0.f, 16.f), Head, Ease) - FVector2D(0.f, FMath::Sin(Up * UE_PI) * 14.f);
	if (PickupAge > 0.32f) { P = Head - FVector2D(0.f, (PickupAge - 0.32f) * 18.f); }
	const float A = PickupAge < 0.6f ? 1.f : FMath::Clamp(1.f - (PickupAge - 0.6f) / (PickupLife - 0.6f), 0.f, 1.f);
	const float Pop = (PickupAge > 0.32f && PickupAge < 0.42f) ? 1.15f : 1.f;
	Dr.SpriteCentered(PickupIcon, Snap2(P, 1.f), WS * Pop, FLinearColor(1.f, 1.f, 1.f, A));
}

void FVScene::FImpl::DrawWorldLights(FVDraw& Dr, const FVScene& S, float Night)
{
	const float T = Time;
	for (int32 k = 0; k < RtLights.Num(); ++k)
	{
		const FRtLight& Lt = RtLights[k];
		float Int = FMath::Max(Lt.DayInt, Night);
		if (Int <= 0.02f) { continue; }
		if (Lt.bFlicker) { Int *= 0.85f + 0.15f * FMath::Sin(T * 11.f + float(k) * 7.f) * FMath::Sin(T * 6.3f + float(k)); }
		FLinearColor C = FV::Col(Lt.Color) * (0.55f * Int);
		C.A = Int;
		Dr.Glow(Lt.Pos, Lt.Radius * (Lt.bFlicker ? 1.f + 0.04f * FMath::Sin(T * 9.f + float(k)) : 1.f), C);
	}
	if (L.bInterior)
	{
		// daylight falls through the windows onto the floor
		const bool bRain = S.Game && S.Game->GetWeather() == EFVWeather::Rain;
		const float Day = (1.f - Night) * (bRain ? 0.45f : 1.f);
		if (Day > 0.05f)
		{
			for (const FFVRect& W : L.Windows)
			{
				const float I = 0.16f * Day;
				Dr.Glow(W.Center(), W.W * 0.9f, FLinearColor(I, I * 0.95f, I * 0.8f, I));
				Dr.Glow(FVector2D(W.Center().X + 8.f, L.WalkTop + 34.f), W.W * 1.1f, FLinearColor(I, I * 0.92f, I * 0.75f, I));
			}
		}
	}
}

void FVScene::FImpl::DrawWorldOverlay(FVDraw& Dr, const FVScene& S)
{
	// update 6: names of the villagers near the player (small, above the head); Р69: the caption of a way sign
	// («→ Лесная река» / «Закрыто: …») as soon as the player comes near it, big worlds and one-screen places alike
	{
		const float NSz = FV::IsPortrait() ? 7.5f : 6.5f;
		for (const FFVWorldObject& O : Objects)
		{
			const bool bExit = O.Kind == EFVObjKind::Exit;
			if ((!bExit && (!bBig || O.Kind != EFVObjKind::Npc)) || O.Label.IsEmpty()) { continue; }
			if (bHighlight && Highlight.Kind == O.Kind && Highlight.Id == O.Id) { continue; }
			if (FVector2D::Distance(S.PlayerPos, O.Feet) > (bExit ? 130.f : 150.f)) { continue; }
			// clamp into the camera view (a villager at the screen edge: the name must not be cut by it)
			const float HalfW = float(Dr.MeasureText(O.Label, NSz, false).X) * 0.5f + 3.f;
			const float VX0 = float(Cam.X), VY0 = float(Cam.Y);
			const float LX = FMath::Clamp(float(O.Hit.Center().X), VX0 + HalfW, FMath::Max(VX0 + HalfW, VX0 + FV::VW - HalfW));
			const float LY = FMath::Max(float(O.Hit.Y - NSz - 4.f), VY0 + FV::TopBarH + 2.f);
			const FVector2D TP(FMath::RoundToFloat(LX), FMath::RoundToFloat(LY));
			if (bExit)
			{
				// a way sign's caption: a dark plate under it (readable on sand, snow and pavement too)
				const FVector2D M = Dr.MeasureText(O.Label, NSz, false);
				Dr.Rect(FFVRect(float(TP.X) - float(M.X) * 0.5f - 3.f, float(TP.Y) - 1.f, float(M.X) + 6.f, float(M.Y) + 2.f), FV::Col(24, 18, 12, 150));
			}
			Dr.Text(O.Label, TP + FVector2D(1.f, 1.f), NSz, FV::Col(20, 16, 12, 170), EFVAlign::Center, false, false);
			Dr.Text(O.Label, TP, NSz, (bExit && O.Index == 1) ? FV::Col(255, 196, 176) : FV::Col(255, 244, 214), EFVAlign::Center, false, false);
		}
	}
	if (!bHighlight) { return; }
	const FFVWorldObject* O = S.FindObject(Highlight.Kind, Highlight.Index, Highlight.Id);
	if (!O || O->Label.IsEmpty()) { return; }
	if (bBig && O->Hit.W > 0.f)
	{
		// update 6: an object baked into the big world's background (City task board) has no sprite to outline —
		// a soft pulsing glow + a thin frame on its Hit instead
		const int32 OIdx = int32(O - Objects.GetData());
		bool bHasVis = false;
		for (const FVis& V : Vis) { if (V.Obj == OIdx) { bHasVis = true; break; } }
		if (!bHasVis)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 3.f);
			const FFVRect& HR = O->Hit;
			Dr.Glow(HR.Center(), FMath::Max(HR.W, HR.H) * 0.75f, FLinearColor(0.45f, 0.38f, 0.2f, 0.25f + 0.2f * Pulse));
			const FLinearColor FC = FV::Alpha(FV::Col(255, 226, 150), 0.45f + 0.35f * Pulse);
			Dr.Rect(FFVRect(HR.X, HR.Y, HR.W, 1.f), FC);
			Dr.Rect(FFVRect(HR.X, HR.Bottom() - 1.f, HR.W, 1.f), FC);
			Dr.Rect(FFVRect(HR.X, HR.Y + 1.f, 1.f, HR.H - 2.f), FC);
			Dr.Rect(FFVRect(HR.Right() - 1.f, HR.Y + 1.f, 1.f, HR.H - 2.f), FC);
		}
	}
	// Name plate + the "tap anywhere" prompt under the name (update 3: the HUD hint strip is gone).
	const bool bPortrait = FV::IsPortrait();
	const float Sz = bPortrait ? 9.f : 8.f;
	const float PSz = bPortrait ? 7.5f : 6.5f;
	const FString Prompt = InteractPrompt(S, *O, bWalk, Profile(S));
	const FVector2D TS = Dr.MeasureText(O->Label, Sz);
	const FVector2D PS = Dr.MeasureText(Prompt, PSz, false);
	const float NameH = FMath::RoundToFloat(float(TS.Y));
	const float PromptH = FMath::RoundToFloat(float(PS.Y));
	const float W = FMath::RoundToFloat(FMath::Max(float(TS.X), float(PS.X)) + 14.f);
	const float H = NameH + PromptH + 6.f;
	const float Bob = Snap(FMath::Sin(Time * 3.f) * 1.f, 1.f);
	float X = FMath::RoundToFloat(O->Hit.Center().X - W * 0.5f);
	float Y = FMath::RoundToFloat(O->Hit.Y - H - 8.f) + Bob;
	// update 6: big worlds draw the overlay in world px — clamp to the camera view (hotfix 0.61: the scrolling pond too)
	const float CX0 = HasCamera() ? float(Cam.X) : 0.f, CY0 = HasCamera() ? float(Cam.Y) : 0.f;
	X = FMath::Clamp(X, CX0 + 4.f, FMath::Max(CX0 + 4.f, CX0 + FV::VW - W - 4.f));
	const float MinY = CY0 + FV::TopBarH + 4.f;
	const bool bBelow = Y < MinY;
	if (bBelow) { Y = FMath::RoundToFloat(O->Hit.Bottom() + 8.f) + Bob; }
	const FFVRect R(X, Y, W, H);
	// little pointer towards the object
	const float PX = FMath::Clamp(FMath::RoundToFloat(O->Hit.Center().X), R.X + 6.f, R.Right() - 6.f);
	for (int32 r = 0; r < 4; ++r)
	{
		const float Half = float(4 - r);
		const float RowY = bBelow ? R.Y - 1.f - float(r) : R.Bottom() + float(r);
		Dr.Rect(FFVRect(PX - Half, RowY, Half * 2.f, 1.f), FVPal::WoodDark());
	}
	Dr.Panel(R, EFVPanel::Tooltip);
	const float CX = R.X + R.W * 0.5f;
	Dr.Text(O->Label, FVector2D(CX, R.Y + 2.f), Sz, FVPal::Ink(), EFVAlign::Center);
	// thin engraved line between the name and the prompt
	const float LineY = R.Y + 2.f + NameH;
	Dr.Rect(FFVRect(R.X + 5.f, LineY, R.W - 10.f, 1.f), FV::Col(190, 140, 90, 110));
	// the prompt: warm brown, gently pulsing so it reads as "you can do this now"
	const float Pulse = 0.82f + 0.18f * (0.5f + 0.5f * FMath::Sin(Time * 4.f));
	Dr.Text(Prompt, FVector2D(CX, LineY + 1.f), PSz, FV::Alpha(FV::Col(150, 70, 24), Pulse), EFVAlign::Center, false, false);
}

// =============================================================================================
// Public API (FVScene.h "UPDATE 2")
// =============================================================================================
bool FVScene::IsHome() const
{
	return Impl->Loc && Impl->Loc->Theme == EFVTheme::Home;
}

bool FVScene::IsInterior() const
{
	return Impl->Loc && Impl->Loc->Theme == EFVTheme::HomeInterior;
}

const TArray<FFVWorldObject>& FVScene::GetWorldObjects() const
{
	return Impl->Objects;
}

void FVScene::RefreshWorldObjects()
{
	Impl->SyncCanvas(*this);
	Impl->RebuildKeepingState(*this);
}

const FFVWorldObject* FVScene::ObjectAt(const FVector2D& P) const
{
	const FImpl& I = *Impl;
	const float Pad = FV::IsTouchUI() ? 6.f : 0.f;
	const FFVWorldObject* Best = nullptr;
	float BestKey = -1.e9f;
	for (int32 k = 0; k < I.Objects.Num(); ++k)
	{
		const FFVWorldObject& O = I.Objects[k];
		if (!Grow(O.Hit, Pad).Contains(P)) { continue; }
		// topmost = drawn last: y-sorted entities above flat ones, then by their sort Y
		float Key = -5.e5f + float(O.Feet.Y);   // the baked door
		for (const FVis& V : I.Vis)
		{
			if (V.Obj == k)
			{
				Key = (V.Layer == 3 ? 0.f : -1.e5f * float(4 - V.Layer)) + V.SortY;
				break;
			}
		}
		if (!Best || Key > BestKey) { Best = &O; BestKey = Key; }
	}
	return Best;
}

bool FVScene::IsInReach(const FFVWorldObject& Obj) const
{
	return FVector2D::Distance(PlayerPos, Obj.Feet) <= Obj.Reach;
}

const FFVWorldObject* FVScene::NearestInReach() const
{
	const FFVWorldObject* Best = nullptr;
	float BestD = 1.e9f;
	for (const FFVWorldObject& O : Impl->Objects)
	{
		const float Dd = float(FVector2D::Distance(PlayerPos, O.Feet));
		if (Dd <= O.Reach && Dd < BestD) { Best = &O; BestD = Dd; }
	}
	return Best;
}

const FFVWorldObject* FVScene::FindObject(EFVObjKind Kind, int32 Index, FName Id) const
{
	for (const FFVWorldObject& O : Impl->Objects)
	{
		if (O.Kind == Kind && O.Index == Index && (Id.IsNone() || O.Id == Id)) { return &O; }
	}
	return nullptr;
}

void FVScene::SetHighlight(const FFVWorldObject* Obj)
{
	Impl->bHighlight = Obj != nullptr;
	if (Obj) { Impl->Highlight = *Obj; }
}

void FVScene::SetWalkTarget(const FVector2D& Feet, float Reach)
{
	FImpl& I = *Impl;
	I.bWalk = true;
	I.WalkTarget = Feet;
	I.WalkReach = FMath::Max(4.f, Reach);
	I.WalkBest = 1.e9f;
	I.WalkStuck = 0.f;
	I.bWalkReached = false;
	I.bWalkFailed = false;
}

void FVScene::ClearWalkTarget()
{
	Impl->bWalk = false;
}

bool FVScene::HasWalkTarget() const
{
	return Impl->bWalk;
}

bool FVScene::WalkTargetReached() const
{
	return Impl->bWalkReached;
}

bool FVScene::WalkTargetFailed() const
{
	return Impl->bWalkFailed;
}

void FVScene::PlayPickup(const FVector2D& P, const FFVSprite& Icon)
{
	FImpl& I = *Impl;
	I.PickupAge = 0.f;
	I.PickupFrom = P;
	I.PickupIcon = Icon;
	I.WorldSigTimer = 0.f;   // the gathered pickup disappears on the next tick
	SpawnSparkles(P + FVector2D(0.f, -12.f), FV::Col(255, 240, 170), 10);
	// face the item
	const FVector2D D = P - PlayerPos;
	if (FMath::Abs(D.X) > FMath::Abs(D.Y) + 2.f) { Facing = D.X < 0.f ? EFVFacing::Left : EFVFacing::Right; }
	else if (FMath::Abs(D.Y) > 2.f) { Facing = D.Y < 0.f ? EFVFacing::Up : EFVFacing::Down; }
}

bool FVScene::DebugIsWalkable(const FVector2D& P) const
{
	return Impl.IsValid() && IsWalkable(Impl->L, P);
}

void FVScene::DebugTeleport(const FVector2D& P)
{
	FImpl& I = *Impl;
	I.SyncCanvas(*this);
	PlayerPos = I.FindWalkable(P);
	I.bAtSpawn = false;
	I.bWalk = false;
	I.Trail.Reset();
	if (I.PetStyle != EFVPetStyle::None)
	{
		FVector2D Pet = PlayerPos + FVector2D(-26.f, -2.f);
		if (!IsWalkable(I.L, Pet)) { Pet = PlayerPos + FVector2D(26.f, -2.f); }
		if (!IsWalkable(I.L, Pet)) { Pet = PlayerPos; }
		I.PetPos = Pet;
	}
	bMoving = false;
	if (Anim == EFVCharAnim::Walk) { SetPlayerAnim(EFVCharAnim::Idle); }
}
