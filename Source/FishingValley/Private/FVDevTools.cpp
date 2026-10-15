// Update 6 (Р47): the extended developer mode. See FVDevTools.h. Test builds only: the whole file is empty in Shipping.

#include "FVDevTools.h"

#if !UE_BUILD_SHIPPING

#include "FVScreenCommon.h"
#include "FVFishing.h"
#include "FVScene.h"
#include "FVWorldU6.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogFVDev, Log, All);

namespace FVDevImpl
{
	// ------------------------------------------------------------------ expert fight on demand
	// FVFishing::DebugForceExpertBite needs the bobber in the water (state Waiting): a core ticker waits for it.
	void StopQuick(UFVGame* G, bool bBotOff);   // Р59 quick legend fight (below)
	FName GPendingFish;
	FTSTicker::FDelegateHandle GTicker;

	void CancelPendingBite()
	{
		GPendingFish = NAME_None;
		if (GTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GTicker);
			GTicker.Reset();
		}
	}

	void QueueExpertBite(FName FishId)
	{
		CancelPendingBite();
		GPendingFish = FishId;
		GTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
		{
			UFVGame* G = UFVGame::Get();
			if (GPendingFish.IsNone() || !G || !G->IsInGame())
			{
				GPendingFish = NAME_None;
				GTicker.Reset();
				return false;
			}
			if (G->GetFishing().GetState() != EFVFishState::Waiting) { return true; }
			G->GetFishing().DebugForceExpertBite(GPendingFish);
			UE_LOG(LogFVDev, Log, TEXT("FV dev: forced expert bite %s"), *GPendingFish.ToString());
			GPendingFish = NAME_None;
			GTicker.Reset();
			return false;
		}), 0.f);
	}

	// ------------------------------------------------------------------ layout: a grid of big buttons in a scroll area
	struct FLay
	{
		FVDraw& D;
		FFVRect Area;
		float X = 0.f, W = 0.f, Y = 0.f;
		int32 Cols = 2;
		float BH = 20.f, Gap = 4.f, TS = 8.f, HS = 10.f, SS = 6.5f;
		int32 Col = 0;

		FLay(FVDraw& InD, const FFVRect& InArea) : D(InD), Area(InArea) {}

		bool Visible(float Top, float H) const { return Top + H >= Area.Y && Top <= Area.Bottom(); }
		void Flush() { if (Col > 0) { Y += BH + Gap; Col = 0; } }
		void SetCols(int32 N) { Flush(); Cols = FMath::Max(1, N); }
		FFVRect Cell(int32 Span = 1)
		{
			Span = FMath::Clamp(Span, 1, Cols);
			if (Col + Span > Cols) { Flush(); }
			const float CW = (W - Gap * float(Cols - 1)) / float(Cols);
			const FFVRect R(FMath::RoundToFloat(X + float(Col) * (CW + Gap)), Y, FMath::RoundToFloat(CW * float(Span) + Gap * float(Span - 1)), BH);
			Col += Span;
			return R;
		}
		bool Btn(const FString& Label, EFVButton Style = EFVButton::Normal, int32 Span = 1, bool bEnabled = true)
		{
			const FFVRect R = Cell(Span);
			if (!Visible(R.Y, R.H)) { return false; }
			const FString L = D.Ellipsize(Label, R.W - 6.f, TS, true);
			return D.Button(R, L, Style, bEnabled, TS);
		}
		void Label(const FString& S, int32 Span = 1, const FLinearColor& C = FVPal::Ink())
		{
			const FFVRect R = Cell(Span);
			if (!Visible(R.Y, R.H)) { return; }
			const float LH = D.LineHeight(TS, true);
			D.Text(D.Ellipsize(S, R.W - 2.f, TS, true), FVector2D(R.X + 1.f, FMath::RoundToFloat(R.Y + (R.H - LH) * 0.5f)), TS, C, EFVAlign::Left, false, true);
		}
		void Header(const FString& S)
		{
			Flush();
			Y += 6.f;
			if (Visible(Y, HS * 1.45f + 6.f)) { FVUI::SectionHeader(D, S, FVector2D(X, Y), W, HS); }
			Y += HS * 1.45f + 6.f;
		}
		void Note(const FString& S, const FLinearColor& C = FVPal::InkSoft())
		{
			Flush();
			const float H = D.MeasureWrapped(S, W, SS);
			if (Visible(Y, H)) { D.TextWrapped(S, FVector2D(X, Y), W, SS, C); }
			Y += H + 4.f;
		}
	};

	// ------------------------------------------------------------------ helpers of the actions
	void Done(UFVGame& G, const FString& Msg, bool bWorld = false)
	{
		G.NotifyProfileChanged();
		if (bWorld && G.IsInGame()) { G.GetScene().RefreshWorldObjects(); }
		G.SaveNow();
		G.Toast(Msg, FVPal::Gold(), EFVIcon::Check);
		UE_LOG(LogFVDev, Log, TEXT("FV dev: %s"), *Msg);
	}

	void Unlock(UFVSaveGame& P, FName Id)
	{
		if (!Id.IsNone() && !P.IsLocationUnlocked(Id)) { P.UnlockedLocations.AddUnique(Id); }
	}

	FString EntryName(FName E)
	{
		const FString S = E.ToString();
		if (S == TEXT("default")) { return TEXT("Обычный вход"); }
		if (S == TEXT("door")) { return TEXT("У двери"); }
		if (S == TEXT("from_map")) { return TEXT("С карты"); }
		if (S == TEXT("from_house")) { return TEXT("Из дома"); }
		if (S == TEXT("pier")) { return TEXT("Причал"); }
		if (S == TEXT("from_cave")) { return TEXT("От пещеры"); }
		if (S == TEXT("from_city")) { return TEXT("Из Города"); }
		if (S == TEXT("from_river")) { return TEXT("От реки"); }
		return S;
	}

	/** Entry points (spawn anchors of the big worlds); classic locations have one fixed spawn (empty list). */
	TArray<FName> EntriesOf(FName Loc)
	{
		TArray<FName> Out;
		auto Add = [&Out](const TCHAR* S) { Out.Add(FName(S)); };
		if (Loc == FVDB::HomeId()) { Add(TEXT("from_map")); Add(TEXT("door")); Add(TEXT("default")); }
		else if (Loc == FVDB::HouseId()) { Add(TEXT("door")); }
		else if (Loc == FVDBU6::ForestId()) { Add(TEXT("default")); Add(TEXT("from_river")); }
		else if (Loc == FVDBU6::CityId()) { Add(TEXT("pier")); Add(TEXT("from_cave")); }
		else if (Loc == FVDBU6::CaveId()) { Add(TEXT("from_city")); }
		return Out;
	}

	/** Every place of the game in map order: 6 classic + the yard, the house, the forest, the City, the Cave. */
	TArray<FName> AllPlaces()
	{
		TArray<FName> Out;
		Out.Add(FVDB::HomeId());
		Out.Add(FVDB::HouseId());
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			if (L.Id == FVDBU6::CityId() || L.Id == FVDBU6::CaveId()) { continue; }
			Out.Add(L.Id);
		}
		Out.Add(FVDBU6::ForestId());
		Out.Add(FVDBU6::CityId());
		Out.Add(FVDBU6::CaveId());
		return Out;
	}

	void Teleport(UFVGame& G, FName Loc, FName Entry)
	{
		if (UFVSaveGame* P = G.GetProfile()) { Unlock(*P, Loc); }
		G.CloseScreen();
		if (Loc == FVDB::HouseId())
		{
			G.TravelToEntry(FVDB::HomeId(), FName(TEXT("door")));
			G.EnterHouse();
		}
		else
		{
			G.TravelToEntry(Loc, Entry);
		}
	}

	void SetLevel(UFVGame& G, int32 L)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		L = FMath::Clamp(L, 1, FV::ExpertMaxLevel);
		P->Xp = FVDB::XpForLevel(L);
		P->U6.bVisitedCity = L > FV::ClassicMaxLevel;   // levels 11+ exist only after the first arrival in the City
		P->U6.XpEarned = FMath::Max<int64>(P->U6.XpEarned, P->Xp);
		Done(G, FString::Printf(TEXT("Уровень %d (опыт %d)%s"), P->GetLevel(), P->Xp, P->U6.bVisitedCity ? TEXT(", прибытие в Город отмечено") : TEXT("")));
	}

	/** Gives Count of every id (backpack at its largest first). Returns the ids that did not fit. */
	int32 GiveAll(UFVSaveGame& P, const TArray<FName>& Ids, int32 Count)
	{
		P.BackpackLevel = FMath::Max(P.BackpackLevel, FVDB::BackpackLevels().Num() - 1);
		int32 Failed = 0;
		for (const FName Id : Ids)
		{
			if (!P.GiveItem(Id, Count)) { ++Failed; }
		}
		return Failed;
	}

	void GiveSet(UFVGame& G, const TArray<FName>& Ids, int32 Count, const FString& What)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		const int32 Failed = GiveAll(*P, Ids, Count);
		Done(G, Failed > 0 ? FString::Printf(TEXT("%s: выдано, не поместилось позиций: %d"), *What, Failed) : FString::Printf(TEXT("%s: выдано"), *What));
	}

	TArray<FName> ItemsWhere(TFunctionRef<bool(const FFVItemDef&)> Pred)
	{
		TArray<FName> Out;
		for (const FFVItemDef& I : FVDB::Items()) { if (Pred(I)) { Out.Add(I.Id); } }
		return Out;
	}

	void GiveTools(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		P->U6.AxeLevel = FVDBU6::Axes().Num();
		P->U6.PickaxeLevel = FVDBU6::Pickaxes().Num();
		P->ShovelLevel = FVDB::Shovels().Num();
		for (const FFVItemDef& I : FVDB::Items()) { if (I.Kind == EFVItemKind::Blueprint) { P->LearnBlueprint(I.Id); } }
		Done(G, FString::Printf(TEXT("Инструменты: топор %d, кирка %d, лопата %d, все чертежи"), P->U6.AxeLevel, P->U6.PickaxeLevel, P->ShovelLevel));
	}

	void GiveExpertRods(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		for (const FFVExpertRodDef& R : FVDBU6::ExpertRods()) { if (FVDB::FindRod(R.RodId)) { P->OwnedRods.AddUnique(R.RodId); } }
		const int32 Failed = GiveAll(*P, ItemsWhere([](const FFVItemDef& I)
			{ return I.bExpert && (I.Kind == EFVItemKind::Line || I.Kind == EFVItemKind::Float || I.Kind == EFVItemKind::Reel); }), 2);
		Done(G, FString::Printf(TEXT("Экспертные удочки: %d, оснастка по 2%s"), FVDBU6::ExpertRods().Num(),
			Failed > 0 ? *FString::Printf(TEXT(" (не поместилось: %d)"), Failed) : TEXT("")));
	}

	void GiveLegendBaits(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		TArray<FName> Ids;
		for (const FFVBaitDef& B : FVDB::Baits()) { if (B.LegendLure != EFVLegend::None) { Ids.AddUnique(B.Id); } }
		for (const FFVExpertFishDef& F : FVDBU6::ExpertFish())
		{
			if (!F.OwnBait.IsNone()) { Ids.AddUnique(F.OwnBait); }
			if (!F.Bait.IsNone()) { Ids.AddUnique(F.Bait); }
		}
		for (const FName Id : Ids) { P->AddBait(Id, 10); }
		Done(G, FString::Printf(TEXT("Наживки легенд: %d видов по 10"), Ids.Num()));
	}

	void BuildEverything(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		for (int32 b = 0; b < int32(EFVYardBuilding::Count); ++b)
		{
			const FFVYardBuildingDef& Y = FVDBU6::YardBuilding(EFVYardBuilding(b));
			if (!Y.Id.IsNone() && Y.Levels.Num() > 0) { P->U6.YardBuildings.Add(Y.Id, Y.Levels.Num()); }
		}
		if (P->U6.Garden.Num() < P->GardenBeds()) { P->U6.Garden.SetNum(P->GardenBeds()); }
		for (int32 s = 0; s < int32(EFVStation::Count); ++s)
		{
			const FFVStationDef& S = FVDB::Station(EFVStation(s));
			if (!S.Id.IsNone() && S.Levels.Num() > 0) { P->StationLevels.Add(S.Id, S.Levels.Num()); }
		}
		Done(G, TEXT("Все постройки двора и станции дома — на максимальном уровне"), true);
	}

	void RepairBoat(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		P->U6.BoatStage = FVDBU6::BoatStages().Num();
		if (P->IsBoatRepaired()) { P->UnlockLocation(FVDBU6::CityId()); }
		Done(G, TEXT("Лодка починена, Город открыт"), true);
	}

	void ClearObstacles(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		P->U6.bBlockageCleared = true;
		for (int32 i = 0; i < 3; ++i) { P->U6.RubbleCleared.AddUnique(FName(*FString::Printf(TEXT("rubble_%d"), i))); }
		if (P->IsCaveOpen()) { P->UnlockLocation(FVDBU6::CaveId()); }
		Done(G, TEXT("Завал в лесу и завалы у пещеры расчищены, Пещера открыта"), true);
	}

	void RipenGarden(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		int32 N = 0;
		for (FFVGardenBed& B : P->U6.Garden) { if (!B.Seed.IsNone()) { B.Growth = 1.f; ++N; } }
		Done(G, N > 0 ? FString::Printf(TEXT("Огород: созрело грядок — %d"), N) : FString(TEXT("Огород: ничего не посажено")), true);
	}

	void RestoreNodes(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		P->U6.Nodes.Reset();   // no record = the node's state before any chop / collection
		Done(G, TEXT("Деревья, кучи мусора и валуны восстановлены"), true);
	}

	/** Moves the clock FORWARD to Hour (the next such hour; day rollovers and passive systems run quietly). */
	void ClockTo(UFVGame& G, int32 Hour)
	{
		const float Now = G.GetClockMinutes();
		float Delta = FMath::Fmod(float(Hour) * 60.f - Now + 1440.f, 1440.f);
		if (Delta < 1.f) { Delta = 0.f; }
		if (Delta > 0.f) { G.AdvanceClock(Delta, true); }
		Done(G, FString::Printf(TEXT("Время: день %d, %s"), G.GetDay(), *G.GetClockText()), true);
	}

	void NextDay(UFVGame& G)
	{
		const float Delta = 1440.f - G.GetClockMinutes() + 360.f;
		G.AdvanceClock(Delta, true);
		Done(G, FString::Printf(TEXT("Следующий день: день %d, %s"), G.GetDay(), *G.GetClockText()), true);
	}

	void SetGlobalWeather(UFVGame& G, EFVWeather W)
	{
		G.SetWeather(W);
		Done(G, W == EFVWeather::Rain ? FString(TEXT("Погода: дождь")) : FString(TEXT("Погода: ясно")));
	}

	/** The City's sea is deterministic (day, clock, rain): finds the nearest future hour with this sea state and moves the
	    clock there (with the rain / clear sky it needs). */
	void CitySea(UFVGame& G, EFVSeaState Want)
	{
		const FName City = FVDBU6::CityId();
		const int32 Day0 = G.GetDay();
		const double Now = G.GetAbsMinutes();
		const EFVWeather Cur = G.GetWeather();
		const EFVWeather Other = Cur == EFVWeather::Rain ? EFVWeather::Sunny : EFVWeather::Rain;
		for (int32 d = Day0; d < Day0 + 25; ++d)
		{
			for (int32 h = 0; h < 24; ++h)
			{
				const float Clock = float(h) * 60.f + 30.f;
				const double Abs = double(d - 1) * 1440.0 + Clock;
				if (Abs < Now) { continue; }
				for (const EFVWeather W : { Cur, Other })
				{
					if (FVDBU6::LocalWeather(City, d, Clock, W).Sea != Want) { continue; }
					if (Abs > Now + 0.5) { G.AdvanceClock(float(Abs - Now), true); }
					G.SetWeather(W);
					Done(G, FString::Printf(TEXT("Город: %s — день %d, %s%s"), *FVDBU6::SeaStateName(Want), G.GetDay(), *G.GetClockText(),
						W == EFVWeather::Rain ? TEXT(", дождь") : TEXT("")), true);
					return;
				}
			}
		}
		G.Toast(TEXT("Не нашлось такого часа в ближайшие дни"), FVPal::Red(), EFVIcon::Close);
	}

	void LearnAllFacts(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		int32 N = 0;
		for (const FFVExpertFishDef& F : FVDBU6::ExpertFish())
		{
			const int32 Num = FVDBU6::Facts(F.FishId).Num();
			for (int32 i = 0; i < Num; ++i) { if (P->LearnFact(F.FishId, i)) { ++N; } }
		}
		Done(G, FString::Printf(TEXT("Узнано новых фактов: %d (всего %d)"), N, P->TotalFactsKnown()));
	}

	void ResetFacts(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		P->U6.KnownFacts.Reset();
		Done(G, TEXT("Факты об экспертной рыбе сброшены"));
	}

	/** The first expert rod of this type (NAME_None = none in the tables). */
	FName RodOfType(int32 Type)
	{
		for (const FFVExpertRodDef& R : FVDBU6::ExpertRods()) { if (int32(R.Type) == Type && FVDB::FindRod(R.RodId)) { return R.RodId; } }
		return NAME_None;
	}

	void ExpertFight(UFVGame& G, FName Fish, int32 Type)
	{
		UFVSaveGame* P = G.GetProfile();
		const FName Rod = RodOfType(Type);
		const FFVFishDef* F = FVDB::FindFish(Fish);
		if (!P || Rod.IsNone() || !F) { return; }
		P->OwnedRods.AddUnique(Rod);
		P->EquipRod(Rod);
		G.NotifyProfileChanged();
		G.SaveNow();
		StopQuick(&G, true);
		QueueExpertBite(Fish);
		G.CloseScreen();
		const FFVRodDef* RD = FVDB::FindRod(Rod);
		G.Toast(FString::Printf(TEXT("Тест: «%s» в руках — забросьте у любой воды, клюнет «%s»"), RD ? *RD->Name : TEXT("?"), *F->Name),
			FVPal::Gold(), EFVIcon::Fish);
	}

	// ------------------------------------------------------------------ Р59: «Легенды» — one tap, the fight starts at once
	// The legend's place (teleport when needed; the Хозяин омута: the Omut behind the finished dam), its rod / tackle /
	// bait given and put on, the clock moved to its hour; then a core ticker finds the water's edge, lets the fishing
	// bot cast and hook (FVFishing::SetAutoPlay — the game's own bot, no system input) with the forced bite, and hands
	// the fight to the player as soon as it starts (bot off). The update 5 legends and the Хозяин омута run as
	// «Бой с легендой (тест)» (StartLegendTrial: the tackle for their level, nothing recorded); expert legends as
	// DebugForceExpertBite (the catch counts as usual).
	struct FQuickLegend
	{
		const TCHAR* Fish;
		const TCHAR* Rod;     // classic legends: the trial rod / line / float (FVFishing.cpp FVTrialImpl::Setups)
		const TCHAR* Line;
		const TCHAR* Float;
	};
	const FQuickLegend ClassicLegends[] = {
		{ TEXT("fish_tsar_carp"),       TEXT("rod_carbon"),   TEXT("line_braided"), TEXT("float_sliding") },
		{ TEXT("fish_silver_sturgeon"), TEXT("rod_carbon"),   TEXT("line_silk"),    TEXT("float_glow") },
		{ TEXT("fish_crystal_trout"),   TEXT("rod_titanium"), TEXT("line_silk"),    TEXT("float_glow") },
		{ TEXT("fish_giant_angler"),    TEXT("rod_iridium"),  TEXT("line_fluoro"),  TEXT("float_glow") },
		{ TEXT("fish_fog_serpent"),     TEXT("rod_iridium"),  TEXT("line_titan"),   TEXT("float_gold") },
		{ TEXT("fish_zhar_ryba"),       TEXT("rod_poseidon"), TEXT("line_heat"),    TEXT("float_gold") },
		{ TEXT("fish_mill_master"),     TEXT("rod_poseidon"), TEXT("line_titan"),   TEXT("float_gold") },
	};

	struct FQuickState
	{
		FName Fish;
		FName Loc;
		bool bTrial = false;      // classic legend: StartLegendTrial
		bool bOmut = false;       // stand in the Omut zone of the mill
		int32 Stage = 0;          // 0 arrive + find the water, 1 the bot casts / hooks until the fight starts
		float T = 0.f;
	};
	FQuickState GQuick;
	FTSTicker::FDelegateHandle GQuickTicker;

	void StopQuick(UFVGame* G, bool bBotOff)
	{
		if (G && bBotOff && G->IsInGame() && !GQuick.Fish.IsNone()) { G->GetFishing().SetAutoPlay(false); }
		GQuick = FQuickState();
		if (GQuickTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GQuickTicker);
			GQuickTicker.Reset();
		}
	}

	bool IsQuickPending() { return !GQuick.Fish.IsNone(); }

	/** Probes P: walkable there (the teleport did not snap elsewhere) and the water is in reach. */
	bool TrySpot(FVScene& S, const FVector2D& P, const FFVRect* Zone)
	{
		S.DebugTeleport(P);
		const FVector2D At = S.GetPlayerPos();
		return FVector2D::DistSquared(At, P) <= 4.f && S.CanFishFromHere() && (!Zone || Zone->Contains(At));
	}

	/** Puts the player at the water: the Omut zone of the mill, or the nearest fishing spot around the player. */
	bool FindWater(UFVGame& G, bool bOmut)
	{
		FVScene& S = G.GetScene();
		S.ClearWalkTarget();
		const FVector2D Start = S.GetPlayerPos();
		if (bOmut)
		{
			for (const FVWorldU6::FAnchor& Z : FVWorldU6::MillZones())
			{
				if (Z.Kind != FVWorldU6::EAnchor::MillZone || Z.Id != FVMill::ZoneOmutId() || Z.Hit.W <= 0.f) { continue; }
				for (float Y = Z.Hit.Y + 1.f; Y < Z.Hit.Bottom(); Y += 3.f)
				{
					for (float X = Z.Hit.X + 2.f; X < Z.Hit.Right(); X += 3.f)
					{
						if (TrySpot(S, FVector2D(X, Y), &Z.Hit)) { return true; }
					}
				}
			}
			S.DebugTeleport(Start);   // no Omut spot: any water of the mill (the fight still starts)
		}
		if (S.CanFishFromHere()) { return true; }
		// classic: the canvas under the top strip (hotfix 0.61: the wide pond — its whole scroll range, X may be negative)
		const FFVRect WB = S.GetWorldBounds();
		const FFVRect B = S.IsBigWorld() ? WB : FFVRect(WB.X + 2.f, FV::TopBarH + 4.f, WB.W - 4.f, FV::VH - FV::TopBarH - 6.f);
		for (float R = 6.f; R < 700.f; R += 6.f)
		{
			const int32 N = FMath::Clamp(FMath::RoundToInt(R / 4.f), 12, 96);
			for (int32 k = 0; k < N; ++k)
			{
				const float A = 2.f * PI * float(k) / float(N);
				const FVector2D P = Start + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
				if (B.Contains(P) && TrySpot(S, P, nullptr)) { return true; }
			}
		}
		S.DebugTeleport(Start);
		return false;
	}

	/** Moves the clock FORWARD to the nearest hour of the legend (phase; expert: sea, tide, dark, the Cave spring). */
	void ClockForLegend(UFVGame& G, FName Loc, uint8 TimeMask, const FFVExpertFishDef* E)
	{
		const int32 Day0 = G.GetDay();
		const double Now = G.GetAbsMinutes();
		const EFVWeather Cur = G.GetWeather();
		const EFVWeather Other = Cur == EFVWeather::Rain ? EFVWeather::Sunny : EFVWeather::Rain;
		auto PhaseBit = [](int32 H) -> uint8
		{
			if (H >= 6 && H < 12) { return FVTime::Morning; }
			if (H >= 12 && H < 18) { return FVTime::Day; }
			if (H >= 18 && H < 22) { return FVTime::Evening; }
			return FVTime::Night;
		};
		for (int32 d = Day0; d < Day0 + 25; ++d)
		{
			for (int32 h = 0; h < 24; ++h)
			{
				const float Clock = float(h) * 60.f + 30.f;   // :30 is inside the Cave spring window (:20–:40)
				const double Abs = double(d - 1) * 1440.0 + Clock;
				if (Abs < Now) { continue; }
				if (TimeMask != 0 && (TimeMask & PhaseBit(h)) == 0) { continue; }
				for (const EFVWeather W : { Cur, Other })
				{
					if (!E && W != Cur) { continue; }            // classic legends: the trial ignores the weather
					if (E)
					{
						const FFVLocalWeather LW = FVDBU6::LocalWeather(Loc, d, Clock, W);
						if (E->SeaMask != 0 && (E->SeaMask & (1u << uint8(LW.Sea))) == 0) { continue; }
						if ((E->bNeedTide && !LW.bTide) || (E->bNeedDark && !LW.bDark) || (E->bNeedSpring && !LW.bSpring)) { continue; }
					}
					if (Abs > Now + 0.5) { G.AdvanceClock(float(Abs - Now), true); }
					if (W != Cur) { G.SetWeather(W); }
					return;
				}
			}
		}
	}

	FName ExpertItem(EFVItemKind Kind, uint8 ExpertKind)
	{
		for (const FFVItemDef& I : FVDB::Items()) { if (I.bExpert && I.Kind == Kind && I.ExpertKind == ExpertKind) { return I.Id; } }
		return NAME_None;
	}

	/** Gives the bait (up to 10) and puts it on the hook. */
	void EquipLegendBait(UFVSaveGame& P, FName Bait)
	{
		if (Bait.IsNone() || !FVDB::FindBait(Bait)) { return; }
		if (P.BaitCount(Bait) < 10) { P.AddBait(Bait, 10 - P.BaitCount(Bait)); }
		P.EquipBait(Bait);
	}

	void QuickFail(UFVGame* G, const TCHAR* Why)
	{
		UE_LOG(LogFVDev, Warning, TEXT("FV dev: legend fight %s: %s"), *GQuick.Fish.ToString(), Why);
		if (G) { G->Toast(Why, FVPal::Red(), EFVIcon::Close); }
		StopQuick(G, true);
	}

	bool QuickTick(float Dt)
	{
		UFVGame* G = UFVGame::Get();
		if (GQuick.Fish.IsNone() || !G || !G->IsInGame())
		{
			StopQuick(G, true);
			return false;
		}
		GQuick.T += FMath::Clamp(Dt, 0.f, 0.1f);
		FVFishing& F = G->GetFishing();
		if (GQuick.T > 30.f) { QuickFail(G, TEXT("Бой не начался — попробуйте ещё раз")); return false; }
		if (GQuick.Stage == 0)
		{
			if (G->GetScene().GetLocationId() != GQuick.Loc || G->GetScreen() != EFVScreen::None || G->HasModal() || GQuick.T < 0.35f) { return true; }
			if (F.IsBusy()) { F.Reset(); }
			if (!FindWater(*G, GQuick.bOmut)) { QuickFail(G, TEXT("Не нашлось места у воды")); return false; }
			if (GQuick.bTrial) { F.StartLegendTrial(GQuick.Fish); }
			F.SetAutoPlay(true);
			GQuick.Stage = 1;
			return true;
		}
		// the player opened a screen before the bite: the bot stops, the test fight is off
		if (G->GetScreen() != EFVScreen::None)
		{
			F.SetAutoPlay(false);
			F.StartLegendTrial(NAME_None);
			StopQuick(G, false);
			return false;
		}
		const EFVFishState S = F.GetState();
		if (S == EFVFishState::Waiting)
		{
			if (GQuick.bTrial) { F.DebugForceBite(); }   // the trial legend takes this bite
			else { F.DebugForceExpertBite(GQuick.Fish); }
		}
		if (S == EFVFishState::Minigame)
		{
			UE_LOG(LogFVDev, Log, TEXT("FV dev: legend fight %s started after %.1f s"), *GQuick.Fish.ToString(), GQuick.T);
			StopQuick(G, true);   // the fight is the player's from its first tick
			return false;
		}
		return true;
	}

	void QuickLegendFight(UFVGame& G, FName FishId)
	{
		UFVSaveGame* P = G.GetProfile();
		const FFVFishDef* F = FVDB::FindFish(FishId);
		if (!P || !F) { return; }
		CancelPendingBite();
		StopQuick(&G, true);
		const FFVExpertFishDef* E = FVDBU6::FindExpertFish(FishId);
		const bool bExpert = F->bExpert && E && E->Legend != EFVExpertLegend::None;
		FQuickState Q;
		Q.Fish = FishId;
		Q.Loc = F->Location;
		Q.bOmut = FishId == FVMill::LegendFishId();
		Q.bTrial = !bExpert;
		FString Rig;
		if (bExpert)
		{
			// the rod of its type, the tackle its rules ask for (None = keep what is on), its own bait
			int32 Type = 0;
			for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t) { if (E->TypeMask & (1u << t)) { Type = t; break; } }
			const FName Rod = RodOfType(Type);
			if (Rod.IsNone()) { G.Toast(TEXT("Нет удочки нужного типа"), FVPal::Red(), EFVIcon::Close); return; }
			P->OwnedRods.AddUnique(Rod);
			P->EquipRod(Rod);
			FFVExpertLoadout& Lo = P->U6.ExpertLoadouts.FindOrAdd(Rod);
			if (E->Line != EFVExpertLine::None) { const FName I = ExpertItem(EFVItemKind::Line, uint8(E->Line)); if (!I.IsNone()) { Lo.Line = I; } }
			if (E->Float != EFVExpertFloat::None) { const FName I = ExpertItem(EFVItemKind::Float, uint8(E->Float)); if (!I.IsNone()) { Lo.Float = I; } }
			if (E->Reel != EFVExpertReel::None) { const FName I = ExpertItem(EFVItemKind::Reel, uint8(E->Reel)); if (!I.IsNone()) { Lo.Reel = I; } }
			EquipLegendBait(*P, !E->OwnBait.IsNone() ? E->OwnBait : E->Bait);
			const FFVRodDef* RD = FVDB::FindRod(Rod);
			Rig = RD ? RD->Name : Rod.ToString();
		}
		else
		{
			const FQuickLegend* S = nullptr;
			for (const FQuickLegend& C : ClassicLegends) { if (FishId == FName(C.Fish)) { S = &C; } }
			const FFVRodDef* RD = S ? FVDB::FindRod(FName(S->Rod)) : nullptr;
			if (RD)
			{
				P->OwnedRods.AddUnique(RD->Id);
				P->EquipRod(RD->Id);   // a classic rod: on an expert one the legend would be the expert fight (Р25)
				FFVRodLoadout& Lo = P->RodLoadouts.FindOrAdd(RD->Id);
				if (FVDB::FindItem(FName(S->Line))) { Lo.Line = FName(S->Line); }
				if (FVDB::FindItem(FName(S->Float))) { Lo.Float = FName(S->Float); }
				Rig = RD->Name;
			}
			FName Bait;
			for (const FFVBaitDef& B : FVDB::Baits()) { if (F->Legend != EFVLegend::None && B.LegendLure == F->Legend) { Bait = B.Id; break; } }
			if (Bait.IsNone() && Q.bOmut) { Bait = FVMill::KolobokBaitId(); }
			EquipLegendBait(*P, Bait);
		}
		if (Q.bOmut) { P->U6.Mill.BeaverFeeds = FVMill::BeaverFeedsNeeded; }   // the dam finished: the Omut is open
		Unlock(*P, Q.Loc);
		ClockForLegend(G, Q.Loc, F->TimeMask, bExpert ? E : nullptr);
		G.NotifyProfileChanged();
		G.SaveNow();
		G.CloseScreen();
		if (G.GetFishing().IsBusy()) { G.GetFishing().Reset(); }
		G.GetFishing().StartLegendTrial(NAME_None);
		// teleport when needed (the Omut: always — the scene rebuilds the finished dam)
		if (!G.IsInGame() || G.GetScene().GetLocationId() != Q.Loc || Q.bOmut)
		{
			FName Entry = NAME_None;
			if (Q.Loc == FVDBU6::CityId()) { Entry = FName(TEXT("pier")); }
			else if (Q.Loc == FVDBU6::CaveId()) { Entry = FName(TEXT("from_city")); }
			G.TravelToEntry(Q.Loc, Entry);
		}
		else
		{
			G.GetScene().RefreshAppearance();
		}
		GQuick = Q;
		GQuickTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&QuickTick), 0.f);
		G.Toast(FString::Printf(TEXT("Тест: «%s»%s%s — бой начинается"), *F->Name, Rig.IsEmpty() ? TEXT("") : TEXT(", "), *Rig), FVPal::Gold(), EFVIcon::Fish);
		UE_LOG(LogFVDev, Log, TEXT("FV dev: quick legend fight %s at %s (%s)"), *FishId.ToString(), *Q.Loc.ToString(), bExpert ? TEXT("expert") : TEXT("trial"));
	}

	/** The 13 buttons of «Легенды»: 6 legends of update 5, the Хозяин омута, 6 expert legends. */
	TArray<FName> QuickLegendIds()
	{
		TArray<FName> Out;
		for (const FQuickLegend& C : ClassicLegends) { if (FVDB::FindFish(FName(C.Fish))) { Out.Add(FName(C.Fish)); } }
		for (const FFVExpertFishDef& EF : FVDBU6::ExpertFish()) { if (EF.Legend != EFVExpertLegend::None && FVDB::FindFish(EF.FishId)) { Out.Add(EF.FishId); } }
		return Out;
	}

	void CityTasks(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		P->U6.CityTasksDay = 0;   // RefreshCityTasks regenerates the offers when the day differs
		P->RefreshCityTasks();
		int32 Offers = 0;
		for (const FFVCityTask& T : P->U6.CityTasks) { if (T.State != uint8(EFVOrderState::Taken)) { ++Offers; } }
		Done(G, FString::Printf(TEXT("Доска Города: новых заданий — %d"), Offers));
	}

	void ArtelWithNets(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		if (!P->U6.bVisitedCity) { P->ArriveInCity(); }
		if (!P->U6.bArtelHired)
		{
			const int64 Before = P->Money;
			P->Money += 100000000;   // the hire is free in the developer mode
			const EFVBuyResult R = P->HireArtel();
			P->Money = Before;
			if (R != EFVBuyResult::Ok)
			{
				G.Toast(UFVSaveGame::BuyResultText(R), FVPal::Red(), EFVIcon::Close);
				return;
			}
		}
		GiveAll(*P, ItemsWhere([](const FFVItemDef& I) { return I.Kind == EFVItemKind::Net; }), 3);
		int32 Given = 0;
		for (int32 i = P->Backpack.Num() - 1; i >= 0; --i)
		{
			const FFVItemDef* D = FVDB::FindItem(P->Backpack[i].ItemId);
			if (!D || D->Kind != EFVItemKind::Net) { continue; }
			const int32 N = P->Backpack[i].Count;
			if (P->GiveNetsToArtel(i, N) == EFVBuyResult::Ok) { Given += N; }
		}
		Done(G, FString::Printf(TEXT("Артельщик нанят, сетей передано: %d"), Given));
	}
}

FFVDevPanel::EResult FFVDevPanel::Draw(FVDraw& D, UFVGame* Game)
{
	using namespace FVDevImpl;
	UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
	const bool bP = FV::IsPortrait();
	const FFVRect R = FV::WindowRect();
	EResult Res = EResult::None;
	if (D.Window(R, TEXT("Режим разработчика"))) { Res = EResult::Back; }
	if (!P || !Game)
	{
		return Res;
	}

	const float Pad = bP ? 14.f : 16.f;
	const float BH = bP ? 34.f : 18.f;
	const float BackH = bP ? FV::Ui::ButtonH() : 20.f;
	const FFVRect Area(R.X + Pad - 4.f, R.Y + (bP ? 30.f : 24.f), R.W - 2.f * Pad + 8.f, R.Bottom() - (bP ? 30.f : 24.f) - BackH - 16.f - R.Y);
	Scroll.Update(D, Area, LastContentH);

	FLay L(D, Area);
	L.X = Area.X + 4.f;
	L.W = Area.W - 12.f;
	L.Y = Area.Y + 2.f - Scroll.Offset();
	L.BH = BH;
	L.Gap = bP ? 6.f : 4.f;
	L.TS = bP ? FV::Ui::Body() : 7.5f;
	L.HS = bP ? FV::Ui::Header() - 1.f : 9.5f;
	L.SS = bP ? FV::Ui::Small() : 6.5f;
	const int32 Cols = bP ? 2 : 4;
	L.SetCols(Cols);

	TFunction<void(UFVGame&)> Act;   // run after drawing (one per frame)
	bool bCloses = false;

	D.PushClip(Area);
	L.Note(FString::Printf(TEXT("Уровень %d · опыт %d%s · %s · день %d, %s · %s"), P->GetLevel(), P->Xp, P->IsXpFrozen() ? TEXT(" (заморожен)") : TEXT(""),
		*FVUI::Coins(P->Money), Game->GetDay(), *Game->GetClockText(), Game->GetWeather() == EFVWeather::Rain ? TEXT("дождь") : TEXT("ясно")));

	// ---- Р59: legends — one tap: the place, the tackle, the bait, and the fight starts at once
	L.Header(TEXT("Легенды — бой сразу"));
	if (IsQuickPending())
	{
		const FFVFishDef* QF = FVDB::FindFish(GQuick.Fish);
		L.Label(FString::Printf(TEXT("Готовим бой: %s"), QF ? *QF->Name : *GQuick.Fish.ToString()), Cols - 1, FVPal::Gold());
		if (L.Btn(TEXT("Отменить"), EFVButton::Danger)) { Act = [](UFVGame& G) { StopQuick(&G, true); G.Toast(TEXT("Тестовый бой отменён")); }; }
	}
	L.SetCols(bP ? 2 : 3);
	for (const FName Id : QuickLegendIds())
	{
		const FFVFishDef* F = FVDB::FindFish(Id);
		if (!F) { continue; }
		const FFVLocationDef* LD = FVDB::FindLocation(F->Location);
		const FString Lbl = bP ? FString::Printf(TEXT("%s%s"), F->bExpert ? TEXT("★ ") : TEXT(""), *F->Name)
			: FString::Printf(TEXT("%s%s · %s"), F->bExpert ? TEXT("★ ") : TEXT(""), *F->Name, LD ? *LD->Name : TEXT("?"));
		if (L.Btn(Lbl, EFVButton::Primary))
		{
			Act = [Id](UFVGame& G) { QuickLegendFight(G, Id); };
			bCloses = true;
		}
	}
	L.SetCols(Cols);
	L.Note(TEXT("Одно нажатие: локация, удочка, оснастка и наживка — сами, бот забрасывает и подсекает, дальше бой ваш. ★ — экспертная легенда (улов засчитывается); остальные — как «Бой с легендой (тест)», без записи улова."));

	// ---- teleport
	L.Header(TEXT("Телепорт"));
	if (L.Btn(TEXT("Открыть все локации"), EFVButton::Primary, Cols))
	{
		Act = [](UFVGame& G)
		{
			if (UFVSaveGame* PP = G.GetProfile()) { for (const FName Id : AllPlaces()) { Unlock(*PP, Id); } }
			Done(G, TEXT("Все локации открыты"));
		};
	}
	const FName Here = Game->IsInGame() ? Game->GetScene().GetLocationId() : NAME_None;
	for (const FName Loc : AllPlaces())
	{
		const FFVLocationDef* Def = FVDB::FindLocation(Loc);
		const FString Name = Def ? Def->Name : Loc.ToString();
		const TArray<FName> Entries = EntriesOf(Loc);
		const bool bOpen = P->IsLocationUnlocked(Loc);
		const FString Lbl = FString::Printf(TEXT("%s%s%s"), *Name, Loc == Here ? TEXT(" (здесь)") : TEXT(""), bOpen ? TEXT("") : TEXT(" · закрыта"));
		if (Entries.Num() == 0)
		{
			L.Label(Lbl, bP ? 1 : 2, bOpen ? FVPal::Ink() : FVPal::InkSoft());
			if (L.Btn(TEXT("Перейти"), EFVButton::Primary, bP ? 1 : 2)) { Act = [Loc](UFVGame& G) { Teleport(G, Loc, NAME_None); }; bCloses = true; }
			continue;
		}
		L.Flush();
		L.Label(Lbl, bP ? Cols : 1, bOpen ? FVPal::Ink() : FVPal::InkSoft());
		for (const FName E : Entries)
		{
			if (L.Btn(EntryName(E), EFVButton::Primary)) { Act = [Loc, E](UFVGame& G) { Teleport(G, Loc, E); }; bCloses = true; }
		}
		L.Flush();
	}

	// ---- progress
	L.Header(TEXT("Прогресс"));
	L.Note(TEXT("Уровень ставит опыт ровно на порог; 11+ отмечает прибытие в Город, 1–10 снимает отметку."));
	L.SetCols(bP ? 5 : 8);
	for (int32 Lv = 1; Lv <= FV::ExpertMaxLevel; ++Lv)
	{
		if (L.Btn(FString::Printf(TEXT("%d"), Lv), Lv == P->GetLevel() ? EFVButton::Primary : EFVButton::Normal)) { Act = [Lv](UFVGame& G) { SetLevel(G, Lv); }; }
	}
	L.SetCols(Cols);
	const int64 Sums[] = { 1000, 100000, 1000000 };
	for (const int64 S : Sums)
	{
		if (L.Btn(FString::Printf(TEXT("+%s"), *FV::Money(S))))
		{
			Act = [S](UFVGame& G) { if (UFVSaveGame* PP = G.GetProfile()) { PP->Earn(S, false); Done(G, FString::Printf(TEXT("+%s"), *FVUI::Coins(S))); } };
		}
	}
	if (L.Btn(TEXT("Снять заморозку опыта"), EFVButton::Normal, 1, !P->U6.bVisitedCity))
	{
		Act = [](UFVGame& G) { if (UFVSaveGame* PP = G.GetProfile()) { PP->U6.bVisitedCity = true; Done(G, TEXT("Заморозка опыта снята (прибытие в Город отмечено)")); } };
	}

	// ---- items
	L.Header(TEXT("Предметы"));
	L.Note(TEXT("Рюкзак расширяется до максимума; что не поместилось — в сообщении."));
	if (L.Btn(TEXT("Инструменты"))) { Act = [](UFVGame& G) { GiveTools(G); }; }
	if (L.Btn(TEXT("Материалы ×50")))
	{
		Act = [](UFVGame& G)
		{
			GiveSet(G, { FName(TEXT("mat_log")), FName(TEXT("mat_board")), FName(TEXT("mat_mpk")), FName(TEXT("mat_nails")),
				FName(TEXT("mat_hardrock")), FName(TEXT("mat_scrap")) }, 50, TEXT("Материалы"));
		};
	}
	if (L.Btn(TEXT("Семена и овощи")))
	{
		Act = [](UFVGame& G)
		{
			GiveSet(G, ItemsWhere([](const FFVItemDef& I) { return I.Kind == EFVItemKind::Seed || I.Id.ToString().StartsWith(TEXT("veg_")); }), 20, TEXT("Семена и овощи"));
		};
	}
	if (L.Btn(TEXT("Еда ×5")))
	{
		Act = [](UFVGame& G) { GiveSet(G, ItemsWhere([](const FFVItemDef& I) { return I.Kind == EFVItemKind::Food; }), 5, TEXT("Еда")); };
	}
	if (L.Btn(TEXT("Экспертные удочки и оснастка"), EFVButton::Normal, 2)) { Act = [](UFVGame& G) { GiveExpertRods(G); }; }
	if (L.Btn(TEXT("Наживки легенд"))) { Act = [](UFVGame& G) { GiveLegendBaits(G); }; }
	if (L.Btn(TEXT("Сети ×3")))
	{
		Act = [](UFVGame& G) { GiveSet(G, ItemsWhere([](const FFVItemDef& I) { return I.Kind == EFVItemKind::Net; }), 3, TEXT("Сети")); };
	}

	// ---- world
	L.Header(TEXT("Мир"));
	if (L.Btn(TEXT("Построить всё (двор и станции)"), EFVButton::Normal, 2)) { Act = [](UFVGame& G) { BuildEverything(G); }; }
	if (L.Btn(TEXT("Починить лодку"), EFVButton::Normal, 1, !P->IsBoatRepaired())) { Act = [](UFVGame& G) { RepairBoat(G); }; }
	if (L.Btn(TEXT("Расчистить завалы"), EFVButton::Normal, 1, !(P->U6.bBlockageCleared && P->IsCaveOpen()))) { Act = [](UFVGame& G) { ClearObstacles(G); }; }
	if (L.Btn(TEXT("Дорастить огород"))) { Act = [](UFVGame& G) { RipenGarden(G); }; }
	if (L.Btn(TEXT("Деревья, мусор, валуны"))) { Act = [](UFVGame& G) { RestoreNodes(G); }; }
	if (L.Btn(FString::Printf(TEXT("Сбросить сон (%d)"), P->U6.SleepStreak), EFVButton::Normal, 1, P->U6.SleepStreak > 0))
	{
		Act = [](UFVGame& G) { if (UFVSaveGame* PP = G.GetProfile()) { PP->U6.SleepStreak = 0; Done(G, TEXT("Счётчик сна сброшен: можно спать")); } };
	}

	// ---- time & weather
	L.Header(TEXT("Время и погода"));
	const int32 Hours[] = { 6, 12, 18, 22 };
	for (const int32 H : Hours)
	{
		if (L.Btn(FString::Printf(TEXT("%02d:00"), H))) { Act = [H](UFVGame& G) { ClockTo(G, H); }; }
	}
	if (L.Btn(TEXT("Следующий день"))) { Act = [](UFVGame& G) { NextDay(G); }; }
	if (L.Btn(TEXT("Ясно"), Game->GetWeather() == EFVWeather::Sunny ? EFVButton::Primary : EFVButton::Normal)) { Act = [](UFVGame& G) { SetGlobalWeather(G, EFVWeather::Sunny); }; }
	if (L.Btn(TEXT("Дождь"), Game->GetWeather() == EFVWeather::Rain ? EFVButton::Primary : EFVButton::Normal)) { Act = [](UFVGame& G) { SetGlobalWeather(G, EFVWeather::Rain); }; }
	L.Flush();
	const FFVLocalWeather CityNow = FVDBU6::LocalWeather(FVDBU6::CityId(), Game->GetDay(), Game->GetClockMinutes(), Game->GetWeather());
	L.Note(FString::Printf(TEXT("Море в Городе сейчас: %s. Кнопка переводит часы вперёд до ближайшего часа с такой погодой."), *FVDBU6::SeaStateName(CityNow.Sea)));
	for (int32 s = 0; s < int32(EFVSeaState::Count); ++s)
	{
		const EFVSeaState Sea = EFVSeaState(s);
		if (L.Btn(FVDBU6::SeaStateName(Sea), CityNow.Sea == Sea ? EFVButton::Primary : EFVButton::Normal)) { Act = [Sea](UFVGame& G) { CitySea(G, Sea); }; }
	}

	// ---- expert
	L.Header(TEXT("Эксперт"));
	L.Note(FString::Printf(TEXT("Известно фактов: %d."), P->TotalFactsKnown()));
	if (L.Btn(TEXT("Узнать все факты"), EFVButton::Normal, 2)) { Act = [](UFVGame& G) { LearnAllFacts(G); }; }
	if (L.Btn(TEXT("Сбросить факты"), EFVButton::Danger, 2)) { Act = [](UFVGame& G) { ResetFacts(G); }; }
	L.Note(TEXT("Бой: выберите тип удочки (она выдаётся и берётся в руки), затем рыбу; забросьте у любой воды — клюнет она. Улов засчитывается как обычный."));
	if (!GPendingFish.IsNone())
	{
		const FFVFishDef* PF = FVDB::FindFish(GPendingFish);
		L.Label(FString::Printf(TEXT("Ждёт поклёвки: %s"), PF ? *PF->Name : *GPendingFish.ToString()), Cols - 1, FVPal::Gold());
		if (L.Btn(TEXT("Отменить"), EFVButton::Danger)) { Act = [](UFVGame& G) { CancelPendingBite(); G.Toast(TEXT("Тестовая поклёвка отменена")); }; }
	}
	L.SetCols(bP ? 3 : 5);
	for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
	{
		if (RodOfType(t).IsNone()) { continue; }
		if (L.Btn(FVDBU6::RodTypeName(EFVExpertRodType(t)), RodType == t ? EFVButton::Primary : EFVButton::Normal)) { RodType = t; }
	}
	L.SetCols(bP ? 3 : 6);
	for (const FFVExpertFishDef& EF : FVDBU6::ExpertFish())
	{
		const FFVFishDef* F = FVDB::FindFish(EF.FishId);
		if (!F) { continue; }
		const bool bLegend = EF.Legend != EFVExpertLegend::None;
		const bool bNative = EF.bUniversal || (EF.TypeMask & (1u << RodType)) != 0;
		const FString Lbl = FString::Printf(TEXT("%s%s%s"), bLegend ? TEXT("★ ") : TEXT(""), *F->Name, bNative ? TEXT("") : TEXT(" *"));
		if (L.Btn(Lbl, bLegend ? EFVButton::Primary : EFVButton::Normal))
		{
			const FName Id = EF.FishId;
			const int32 Type = RodType;
			Act = [Id, Type](UFVGame& G) { ExpertFight(G, Id, Type); };
			bCloses = true;
		}
	}
	L.SetCols(Cols);
	L.Note(TEXT("★ — легенда; * — рыба обычно не клюёт на этот тип удочки."));

	// ---- City
	L.Header(TEXT("Город"));
	if (L.Btn(TEXT("Сгенерировать задания"), EFVButton::Normal, 2)) { Act = [](UFVGame& G) { CityTasks(G); }; }
	if (L.Btn(TEXT("Нанять артельщика и дать сети"), EFVButton::Normal, 2)) { Act = [](UFVGame& G) { ArtelWithNets(G); }; }
	L.Flush();
	L.Y += 6.f;
	D.PopClip();
	LastContentH = L.Y + Scroll.Offset() - Area.Y;
	Scroll.DrawFades(D, Area, FVPal::Parchment());
	Scroll.DrawBar(D, Area);

	// ---- bottom row: back + switch the mode off
	const float BY = R.Bottom() - BackH - 10.f;
	const float BW = FMath::RoundToFloat((R.W - 2.f * Pad - 8.f) * 0.5f);
	const float TS = bP ? FV::Ui::Body() + 1.f : 8.f;
	if (D.Button(FFVRect(R.X + Pad, BY, BW, BackH), TEXT("Назад"), EFVButton::Normal, true, TS)) { Res = EResult::Back; }
	if (D.Button(FFVRect(R.X + Pad + BW + 8.f, BY, BW, BackH), TEXT("Выключить режим"), EFVButton::Danger, true, TS - 0.5f))
	{
		P->bDevTools = false;
		CancelPendingBite();
		StopQuick(Game, true);
		Game->GetFishing().StartLegendTrial(NAME_None);
		Game->SaveNow();
		Res = EResult::Back;
	}

	if (Act)
	{
		FVUI::Sfx(EFVSfx::Click);
		UFVGame* G = Game;
		if (bCloses) { Res = EResult::Closed; }
		Act(*G);   // may close the screen: nothing below touches members
	}
	return Res;
}

#endif // !UE_BUILD_SHIPPING
