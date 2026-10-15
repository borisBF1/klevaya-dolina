// UPDATE 6 [GAME-FLOW] — the game-flow hooks (Private/FVGameU6.h) and the update-6 part of UFVGame (FVGame.h
// «UPDATE 6»): interaction with the new objects, walk-through exits, first arrival in the City (level 11, banner,
// training fish), played minutes, artel mornings, City tasks at the new day, scrap junk, expert catches, facts from
// journal pages. Owner: GAME-FLOW.

#include "FVGameU6.h"
#include "FVGame.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVDraw.h"
#include "FVScene.h"
#include "FVFishing.h"
#include "FVScreens.h"
#include "FVAudio.h"
#include "FVWorldU6.h"

namespace FVGameU6Impl
{
	// big banner (first arrival in the City) — screen space, drawn by DrawOverlay
	FString GBanner;
	FString GBannerSub;
	float GBannerAge = 99.f;
	constexpr float BannerLife = 5.f;

	FName NodeKey(FName Loc, FName Id) { return FName(*FString::Printf(TEXT("%s:%s"), *Loc.ToString(), *Id.ToString())); }

	FString LootText(const TArray<FFVIngredient>& Loot)
	{
		TArray<FString> Parts;
		for (const FFVIngredient& I : Loot)
		{
			if (I.Count > 0) { Parts.Add(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(I.ItemId), I.Count)); }
		}
		return FString::Join(Parts, TEXT(", "));
	}

	void Refuse(UFVGame& G, const FString& Text, EFVIcon Icon)
	{
		G.Toast(Text, FV::Col(255, 150, 130), Icon);
		if (UFVAudio* A = G.GetAudio()) { A->Play(EFVSfx::Error, 0.6f); }
	}

	void Refuse(UFVGame& G, const FString& Text, EFVIcon Icon);

	/** Р69: walking into the next place — a short fade to black, the travel, a fade back (FVGameU6::Tick / DrawOverlay). */
	FName GWalkTarget, GWalkEntry;
	float GWalkT = -1.f;          // < 0 = no walk travel; 0..WalkFadeOut = darkening; then the travel and the fade-in
	bool bGWalkDone = false;
	constexpr float WalkFadeOut = 0.22f, WalkFadeIn = 0.3f;

	/** An exit (walked into, or tapped): Target "map" opens the map screen, a closed place refuses with its condition
	    (as the map shows it), an open one starts the fade and the travel. */
	void UseExit(UFVGame& G, FName Target, FName Entry)
	{
		if (G.IsWorldBusy() || GWalkT >= 0.f) { return; }
		if (Target == FName(TEXT("map"))) { G.TryOpenFromWorldWith(EFVScreen::Map, NAME_None); return; }
		FString Why;
		if (!FVWorldU6::WalkTargetOpen(G.GetProfile(), Target, nullptr, &Why))
		{
			Refuse(G, Why.IsEmpty() ? FString(TEXT("Это место ещё закрыто")) : Why, EFVIcon::Lock);
			return;
		}
		GWalkTarget = Target;
		GWalkEntry = Entry;
		GWalkT = 0.f;
		bGWalkDone = false;
	}

	void TickWalkTravel(UFVGame& G, float Dt)
	{
		if (GWalkT < 0.f) { return; }
		GWalkT += FMath::Clamp(Dt, 0.f, 0.05f);   // the first frame after loading a place must not eat the fade-in
		if (!bGWalkDone && GWalkT >= WalkFadeOut)
		{
			bGWalkDone = true;
			GWalkT = WalkFadeOut;
			if (!G.IsWorldBusy()) { G.TravelToEntry(GWalkTarget, GWalkEntry); }
		}
		else if (bGWalkDone && GWalkT >= WalkFadeOut + WalkFadeIn)
		{
			GWalkT = -1.f;
		}
	}

	/** Update-6 locations open by condition (forest — level 4, City — the boat, Cave — the rubble); toast on opening. */
	float GLocSyncT = 0.f;
	void SyncLocations(UFVGame& G, bool bToast)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		for (const FName LocId : { FVDBU6::ForestId(), FVDBU6::CityId(), FVDBU6::CaveId(), FVMill::MillId() })
		{
			if (P->IsLocationUnlocked(LocId) || P->UnlockLocation(LocId) != EFVBuyResult::Ok) { continue; }
			if (bToast)
			{
				const FFVLocationDef* L = FVDB::FindLocation(LocId);
				G.Toast(FString::Printf(TEXT("Открыта локация: %s"), L ? *L->Name : *LocId.ToString()), FVPal::Gold(), EFVIcon::Map);
			}
			G.NotifyProfileChanged();
		}
	}

	void Banner(const FString& Title, const FString& Sub)
	{
		GBanner = Title;
		GBannerSub = Sub;
		GBannerAge = 0.f;
	}

	// ============================================================================================
	// §18 «Старая мельница» [MILL]: bridge, mill stages, beavers, feeder decks, fish pen
	// ============================================================================================
	float GMillTeaT = 0.f;

	/** «Доска: 7 / 12» lines of a repair stage. */
	TArray<FString> CostLines(const UFVSaveGame& P, const FFVBoatStage& S)
	{
		TArray<FString> Lines;
		for (const FFVIngredient& I : S.Cost)
		{
			const int32 Have = P.CountIngredient(I.ItemId);
			Lines.Add(FString::Printf(TEXT("%s: %d / %d%s"), *FVDB::ItemName(I.ItemId), FMath::Min(Have, I.Count), I.Count,
				Have >= I.Count ? TEXT(" — есть") : TEXT("")));
		}
		if (S.Coins > 0) { Lines.Add(FString::Printf(TEXT("Монеты: %d"), S.Coins)); }
		return Lines;
	}

	void MillChanged(UFVGame& G)
	{
		G.NotifyProfileChanged();
		G.GetScene().RefreshWorldObjects();   // the scene lives as long as the game (interactions / modals run in game)
	}

	void RepairResult(UFVGame& G, EFVBuyResult R, const FString& OkText)
	{
		if (R == EFVBuyResult::Ok)
		{
			G.Toast(OkText, FVPal::Gold(), EFVIcon::Hammer);
			if (UFVAudio* A = G.GetAudio()) { A->Play(EFVSfx::Hammer, 1.f); }
			MillChanged(G);
		}
		else if (R == EFVBuyResult::NotEnoughMaterials) { Refuse(G, TEXT("Не хватает материалов"), EFVIcon::Backpack); }
		else if (R == EFVBuyResult::LevelTooLow) { Refuse(G, FString::Printf(TEXT("Нужен %d уровень"), FVMill::MinLevel), EFVIcon::Level); }
		else { Refuse(G, UFVSaveGame::BuyResultText(R), EFVIcon::Hammer); }
	}

	void AskBridge(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		if (P->IsBridgeRepaired())
		{
			G.Toast(P->IsLocationUnlocked(FVMill::MillId()) ? FString(TEXT("Мостик починен — тропа ведёт к Старой мельнице"))
				: FString::Printf(TEXT("Мостик починен. К мельнице — с %d уровня"), FVMill::MinLevel), FVPal::White(), EFVIcon::Map);
			return;
		}
		FFVModal M;
		M.Title = FVMill::BridgeRepair().Name;
		M.Body = FString::Printf(TEXT("За ручьём — Старая мельница (с %d уровня). Почините мостик, чтобы туда пройти."), FVMill::MinLevel);
		M.Lines = CostLines(*P, FVMill::BridgeRepair());
		M.Icon = EFVIcon::Hammer;
		M.Buttons = { TEXT("Починить"), TEXT("Отмена") };
		UFVGame* GP = &G;
		M.OnChoice = [GP](int32 Choice)
		{
			UFVSaveGame* Pr = GP ? GP->GetProfile() : nullptr;
			if (Choice != 0 || !Pr) { return; }
			RepairResult(*GP, Pr->RepairBridge(), TEXT("Мостик через ручей починен!"));
			if (Pr->IsBridgeRepaired()) { SyncLocations(*GP, true); }
		};
		G.ShowModal(MoveTemp(M));
	}

	void AskMill(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		if (P->IsMillWorking())
		{
			G.TryOpenFromWorldWith(EFVScreen::Mill, FName(TEXT("mill")));
			return;
		}
		const TArray<FFVBoatStage>& Stages = FVMill::MillStages();
		const int32 Cur = P->GetMillStage();
		if (!Stages.IsValidIndex(Cur)) { return; }
		FFVModal M;
		M.Title = FString::Printf(TEXT("Ремонт мельницы: %s (%d/%d)"), *Stages[Cur].Name, Cur + 1, Stages.Num());
		M.Body = TEXT("Работающая мельница мелет рожь в муку, а водяная пила даёт лишнюю доску с каждого бревна на лесопилке.");
		M.Lines = CostLines(*P, Stages[Cur]);
		M.Icon = EFVIcon::Hammer;
		M.Buttons = { TEXT("Починить"), TEXT("Отмена") };
		UFVGame* GP = &G;
		M.OnChoice = [GP](int32 Choice)
		{
			UFVSaveGame* Pr = GP ? GP->GetProfile() : nullptr;
			if (Choice != 0 || !Pr) { return; }
			const EFVBuyResult R = Pr->RepairMillStage();
			RepairResult(*GP, R, Pr->IsMillWorking() ? FString(TEXT("Мельница заработала! Колесо крутится"))
				: FString::Printf(TEXT("Этап ремонта готов (%d/%d)"), Pr->GetMillStage(), FVMill::MillStages().Num()));
			if (R == EFVBuyResult::Ok && Pr->IsMillWorking())
			{
				Banner(TEXT("Мельница заработала"), TEXT("Рожь — в муку, водяная пила: +1 доска с бревна"));
			}
		};
		G.ShowModal(MoveTemp(M));
	}

	void FeedBeavers(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		const EFVBuyResult R = P->FeedBeavers();
		switch (R)
		{
		case EFVBuyResult::Ok:
			if (UFVAudio* A = G.GetAudio()) { A->Play(EFVSfx::Pickup, 0.9f); }
			if (P->IsOmutOpen())
			{
				Banner(TEXT("Плотина достроена"), TEXT("Открыт Омут: глубокая вода, стерлядь и Хозяин омута"));
			}
			else
			{
				G.Toast(FString::Printf(TEXT("Бобры утащили ветки: плотина %d/%d"), P->U6.Mill.BeaverFeeds, FVMill::BeaverFeedsNeeded),
					FVPal::Green(), EFVIcon::Hand);
			}
			MillChanged(G);
			break;
		case EFVBuyResult::MaxLevel:
			G.Toast(TEXT("Плотина готова — Омут открыт"), FVPal::White(), EFVIcon::Check);
			break;
		case EFVBuyResult::Invalid:
			Refuse(G, FString::Printf(TEXT("Бобры заняты стройкой — загляните через %d мин"),
				FMath::Max(1, FMath::CeilToInt(P->BeaverCooldownLeft()))), EFVIcon::Clock);
			break;
		default:
			Refuse(G, FString::Printf(TEXT("Бобрам нужно веток: %d"), FVMill::BeaverBranches), EFVIcon::Backpack);
			break;
		}
	}

	void BaitDeck(UFVGame& G, int32 Deck)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P) { return; }
		const double Now = G.GetAbsMinutes();
		if (P->IsDeckBaited(Deck, Now))
		{
			G.Toast(FString::Printf(TEXT("Мосток прикормлен ещё %d мин"), FMath::Max(1, FMath::CeilToInt(P->DeckMinutesLeft(Deck, Now)))),
				FVPal::White(), EFVIcon::Clock);
			return;
		}
		for (const FName& Bait : FVMill::DeckBaits())
		{
			const int32* Have = P->Baits.Find(Bait);
			if (!Have || *Have <= 0) { continue; }
			if (P->BaitFeedDeck(Deck, Bait, Now) == EFVBuyResult::Ok)
			{
				G.Toast(FString::Printf(TEXT("%s на мостке: полчаса клюёт быстрее, редкая рыба чаще"), *FVDB::ItemName(Bait)),
					FVPal::Green(), EFVIcon::Bait);
				if (UFVAudio* A = G.GetAudio()) { A->Play(EFVSfx::Splash, 0.6f); }
				MillChanged(G);
				return;
			}
		}
		Refuse(G, TEXT("Нужна прикормка, хлебная прикормка или тесто"), EFVIcon::Bait);
	}

	void MillInteract(UFVGame& G, const FFVWorldObject& O)
	{
		if (O.Id == FVMill::ObjBridgeId()) { AskBridge(G); }
		else if (O.Id == FVMill::ObjMillId()) { AskMill(G); }
		else if (O.Id == FVMill::ObjBeaverId()) { FeedBeavers(G); }
		else if (O.Id == FVMill::ObjDeckId()) { BaitDeck(G, FMath::Clamp(O.Index, 0, FVMill::FeedDecks - 1)); }
		else if (O.Id == FVMill::ObjPenId()) { G.TryOpenFromWorldWith(EFVScreen::Mill, FName(TEXT("pen"))); }
	}
}

// ============================================================================================
// Hooks
// ============================================================================================
namespace FVGameU6Impl
{
	/** First arrival in the City (§8): level 11, banner, explanation, the training fish. */
	void FirstArrival(UFVGame& G)
	{
		UFVSaveGame* P = G.GetProfile();
		if (!P || P->U6.bVisitedCity) { return; }
		const int32 Gained = P->ArriveInCity();
		Banner(TEXT("Вы вступили в экспертную зону"), FString::Printf(TEXT("Уровень %d"), P->GetLevel()));
		if (UFVAudio* A = G.GetAudio()) { A->Play(EFVSfx::Achievement, 1.f); }
		FFVModal M;
		M.Title = TEXT("Экспертная зона");
		M.Body = TEXT("Город и Пещера — места экспертной рыбалки. Обычная удочка ловит здесь обычную рыбу, а экспертную рыбу "
			"поймать можно только экспертной удочкой. Условия клёва скрыты: их открывают задания Города и страницы "
			"рыбацкого журнала из сокровищ.");
		M.Lines = {
			FString::Printf(TEXT("Уровень %d: опыт снова копится"), P->GetLevel()),
			TEXT("Удочка «Бриз» — в снасти-магазине Города"),
			TEXT("Первая экспертная рыбка будет учебной"),
			TEXT("Сети в Городе и Пещере ставить нельзя — их расставит Артельщик"),
		};
		M.Icon = EFVIcon::ExpertRod;
		M.Buttons = { TEXT("Понятно") };
		G.ShowModal(MoveTemp(M));
		G.GetFishing().StartExpertTutorial();
		FVTutorialAnnounce(&G, FName(TEXT("g_expert")));
		G.NotifyProfileChanged();
		UE_LOG(LogFV, Log, TEXT("FV: first arrival in the City (+%d levels)"), Gained);
	}
}

void FVGameU6::OnEnterWorld(UFVGame& G)
{
	UFVSaveGame* P = G.GetProfile();
	if (!P) { return; }
	for (const FString& L : P->U6.ArtelReport)
	{
		if (!L.IsEmpty()) { G.Toast(L, FVPal::Gold(), EFVIcon::Artel); }
	}
	if (P->U6.ArtelReport.Num() > 0) { P->U6.ArtelReport.Reset(); G.bProfileDirty = true; }
	FVGameU6Impl::SyncLocations(G, false);
	if (G.Scene && G.Scene->GetLocationId() == FVDBU6::CityId() && !P->U6.bVisitedCity)
	{
		FVGameU6Impl::FirstArrival(G);
	}
}

void FVGameU6::Tick(UFVGame& G, float Dt, bool bWorldRuns)
{
	using namespace FVGameU6Impl;
	GBannerAge += Dt;
	if (G.Scene) { TickWalkTravel(G, Dt); }   // Р69: runs under menus too (the fade must not freeze)
	UFVSaveGame* P = G.GetProfile();
	if (!P || !bWorldRuns || !G.Scene) { return; }
	const double Minutes = double(FMath::Clamp(Dt, 0.f, 0.25f)) / 60.0;
	P->AddPlayedMinutes(Minutes);
	P->TickGarden(Minutes, G.GetWeather() == EFVWeather::Rain);
	// §18: the fog morning at the mill (06-10) — the herbal tea lasts twice as long
	GMillTeaT += Dt;
	if (GMillTeaT >= 1.f)
	{
		GMillTeaT = 0.f;
		if (FVMill::IsMillId(G.Scene->GetLocationId()) && P->ApplyFogTea(G.GetAbsMinutes(), G.GetClockMinutes()))
		{
			G.Toast(TEXT("Туманное утро: травяной чай действует вдвое дольше"), FVPal::White(), EFVIcon::Food);
			G.bProfileDirty = true;
		}
	}
	GLocSyncT += Dt;
	if (GLocSyncT >= 1.f) { GLocSyncT = 0.f; SyncLocations(G, true); }

	FName ExitLoc, ExitEntry;
	if (G.Scene->GetPendingExit(ExitLoc, ExitEntry))
	{
		G.Scene->ClearPendingExit();
		UseExit(G, ExitLoc, ExitEntry);   // the exit anchor id is the spawn anchor id in the target ("from_city"...)
	}
}

bool FVGameU6::Interact(UFVGame& G, const FFVWorldObject& Obj)
{
	using namespace FVGameU6Impl;
	UFVSaveGame* P = G.GetProfile();
	if (!P || !G.Scene) { return false; }
	const FFVWorldObject O = Obj;   // copy: a refresh may rebuild the array Obj points into
	const FName Loc = G.Scene->GetLocationId();

	switch (O.Kind)
	{
	case EFVObjKind::Tree:
	case EFVObjKind::Blockage:
	{
		if (P->U6.AxeLevel <= 0)
		{
			Refuse(G, TEXT("Нужен топор — спросите у Боцмана на причале"), EFVIcon::Axe);
			return true;
		}
		if (O.Kind == EFVObjKind::Blockage && P->U6.bBlockageCleared) { return true; }
		const FName Key = NodeKey(Loc, O.Id);
		if (O.Kind == EFVObjKind::Tree && P->GetTreeState(Key, EFVTreeState::Marked) != EFVTreeState::Marked)
		{
			Refuse(G, TEXT("Дерево ещё растёт"), EFVIcon::Clock);
			return true;
		}
		if (O.Kind == EFVObjKind::Tree && FVDBU6::TreeHits(EFVTreeKind(FMath::Clamp(O.Index, 0, int32(EFVTreeKind::Count) - 1)), P->U6.AxeLevel) <= 0)
		{
			Refuse(G, TEXT("Этот топор не берёт такую породу — нужен топор покрепче"), EFVIcon::Axe);
			return true;
		}
		G.TryOpenFromWorldWith(EFVScreen::Chopping, Key);
		FVTutorialAnnounce(&G, FName(TEXT("g_axe")));
		return true;
	}
	case EFVObjKind::JunkPile:
	{
		TArray<FFVIngredient> Loot;
		const EFVBuyResult R = P->CollectJunkPile(NodeKey(Loc, O.Id), Loot);
		if (R == EFVBuyResult::Ok)
		{
			G.Toast(FString::Printf(TEXT("Найдено: %s"), *LootText(Loot)), FVPal::Green(), EFVIcon::Hand);
			if (UFVAudio* A = G.GetAudio()) { A->Play(EFVSfx::Find, 0.8f); }
			G.NotifyProfileChanged();
			G.Scene->RefreshWorldObjects();
			FVTutorialAnnounce(&G, FName(TEXT("g_scrap")));
		}
		else if (R == EFVBuyResult::NoRoom) { Refuse(G, TEXT("Рюкзак полон"), EFVIcon::Backpack); }
		else { Refuse(G, TEXT("Здесь пусто — загляните позже"), EFVIcon::Clock); }
		return true;
	}
	case EFVObjKind::Boulder:
	case EFVObjKind::CaveRubble:
		G.TryOpenFromWorldWith(EFVScreen::Excavation, NodeKey(Loc, O.Id));
		return true;
	case EFVObjKind::Npc:
	{
		const FFVNpcDef* N = FVDBU6::FindNpc(O.Id);
		if (!N) { return true; }
		// the City task board (city_board anchor → an Npc object of the board villager): the board itself, no greeting
		if (N->Screen == EFVScreen::CityBoard && O.Label == TEXT("Доска заданий Города"))
		{
			G.TryOpenFromWorldWith(EFVScreen::CityBoard, NAME_None);
			FVTutorialAnnounce(&G, FName(TEXT("g_city_orders")));
			return true;
		}
		if (!N->Greeting.IsEmpty()) { G.Toast(FString::Printf(TEXT("%s: «%s»"), *N->Name, *N->Greeting), FVPal::White(), EFVIcon::Hand); }
		if (N->Screen != EFVScreen::None) { G.TryOpenFromWorldWith(N->Screen, N->TraderId); }
		else if (!N->TraderId.IsNone()) { G.TryOpenFromWorldWith(EFVScreen::Shop, N->TraderId); }
		return true;
	}
	case EFVObjKind::BuildPlot:
	{
		const FFVYardBuildingDef* Y = FVDBU6::FindYardBuilding(O.Id);
		G.Toast(FString::Printf(TEXT("Здесь можно построить: %s — у Прораба в лесу"), Y ? *Y->Name : TEXT("постройку")),
			FVPal::White(), EFVIcon::Hammer);
		return true;
	}
	case EFVObjKind::YardBuilding:
	{
		if (O.Id == FVDBU6::YardBuildingId(EFVYardBuilding::Garden))
		{
			G.Toast(TEXT("Подойдите к грядке, чтобы посадить или собрать урожай"), FVPal::White(), EFVIcon::Seed);
			return true;
		}
		G.TryOpenFromWorldWith(EFVScreen::YardBuilding, O.Id);
		return true;
	}
	case EFVObjKind::GardenBed:
		G.TryOpenFromWorldWith(EFVScreen::GardenBed, FName(*FString::Printf(TEXT("bed_%d"), O.Index)));
		return true;
	case EFVObjKind::Boat:
		if (Loc == FVDBU6::CityId()) { G.TryOpenFromWorldWith(EFVScreen::Voyage, FName(TEXT("ocean"))); }
		else { G.TryOpenFromWorldWith(EFVScreen::Boat, NAME_None); }
		return true;
	case EFVObjKind::Exit:
		UseExit(G, O.Id, FName(*FString::Printf(TEXT("from_%s"), *Loc.ToString())));
		return true;
	case EFVObjKind::MillObj:
		MillInteract(G, O);
		return true;
	case EFVObjKind::NetSpot:
		if (FVMill::IsMillId(Loc) && O.Index == FVMill::OmutNetSpot && !P->IsOmutOpen())
		{
			Refuse(G, TEXT("Омут откроется, когда бобры достроят плотину"), EFVIcon::Net);
			return true;
		}
		if (FVDBU6::IsExpertLocation(Loc))
		{
			Refuse(G, TEXT("В экспертной зоне сети ставить нельзя — их расставит Артельщик на других водоёмах"), EFVIcon::Net);
			return true;
		}
		return false;
	default:
		return false;
	}
}

void FVGameU6::OnTravel(UFVGame& G, FName From, FName To)
{
	(void)From;
	UFVSaveGame* P = G.GetProfile();
	if (!P) { return; }
	if (To == FVDBU6::CityId())
	{
		if (!P->U6.bVisitedCity) { FVGameU6Impl::FirstArrival(G); }
		FVTutorialAnnounce(&G, FName(TEXT("g_city")));
	}
	else if (To == FVDBU6::CaveId()) { FVTutorialAnnounce(&G, FName(TEXT("g_cave"))); }
	else if (FVDBU6::IsForestId(To)) { FVTutorialAnnounce(&G, FName(TEXT("g_forest"))); }
	else if (FVMill::IsMillId(To)) { FVTutorialAnnounce(&G, FName(TEXT("g_mill"))); }
}

void FVGameU6::OnAdvanceClock(UFVGame& G, double FromAbs, double ToAbs, bool bQuiet)
{
	UFVSaveGame* P = G.GetProfile();
	if (!P || ToAbs <= FromAbs) { return; }
	P->SimulateArtel(FromAbs, ToAbs, G.GetWeather());
	TArray<FString> Report;
	const int64 Coins = P->ArtelMorningCollect(ToAbs, Report);
	if (Coins > 0 || Report.Num() > 0)
	{
		if (bQuiet) { G.SleepLog.Append(Report); }
		else
		{
			for (const FString& L : Report) { G.Toast(L, FVPal::Gold(), EFVIcon::Artel); }
		}
		G.bProfileDirty = true;
	}
}

void FVGameU6::OnNewDay(UFVGame& G, TArray<FString>& Lines)
{
	UFVSaveGame* P = G.GetProfile();
	if (!P) { return; }
	const TArray<FName> Expired = P->ExpireCityTasks(G.GetAbsMinutes());
	if (Expired.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("Заданий Города просрочено: %d"), Expired.Num()));
	}
	if (P->U6.bVisitedCity) { P->RefreshCityTasks(); }
	P->PruneMill(G.GetAbsMinutes());   // §18: expired pen species / deck timers
}

void FVGameU6::BeforeCatch(UFVGame& G, FFVCatchResult& Result)
{
	UFVSaveGame* P = G.GetProfile();
	// §18: a species released into the fish pen is caught bigger at the mill (+0.05 of its size range)
	if (P && Result.Fish && !Result.bExpert && G.Scene && FVMill::IsMillId(G.Scene->GetLocationId())
		&& P->PenMinutesLeft(Result.Fish->Id, G.GetAbsMinutes()) > 0.f)
	{
		const FFVFishDef& F = *Result.Fish;
		const float Span = F.MaxSize - F.MinSize;
		if (Span > KINDA_SMALL_NUMBER)
		{
			const float Frac = FMath::Clamp((Result.SizeCm - F.MinSize) / Span + FVMill::PenSizeBonus, 0.f, 1.f);
			FRandomStream QRng(int32(GetTypeHash(F.Id)));
			Result.SizeCm = FMath::RoundToFloat(FMath::Lerp(F.MinSize, F.MaxSize, Frac) * 10.f) / 10.f;
			const EFVQuality Q = FVDB::RollQuality(Frac, Result.bPerfect, QRng);
			if (Q > Result.Quality)
			{
				Result.Quality = Q;
				Result.UnitPrice = FVDB::FishPrice(F, Q);
				Result.Xp = FMath::Max(Result.Xp, FVDB::CatchXp(F, Q, Result.bPerfect));
			}
		}
	}
	if (!P || !Result.bExpert || !Result.Fish) { return; }
	// expert catch: its own XP and a treasure of the expert fight (may carry a page of the fisherman's journal)
	Result.Xp = FMath::Max(Result.Xp, FVDBU6::ExpertCatchXp(*Result.Fish, Result.Quality, Result.bPerfect));
	if (Result.bTreasure)
	{
		FRandomStream Rng(FMath::Rand());
		bool bPage = false;
		const FName Loc = G.Scene ? G.Scene->GetLocationId() : P->CurrentLocation;
		Result.Treasure = FVDBU6::RollExpertTreasure(Loc, P->GetLevel(), Rng, bPage);
		if (bPage)
		{
			FName FactFish;
			int32 FactIndex = INDEX_NONE;
			if (P->PickNextFact(Result.Fish->Id, Rng, FactFish, FactIndex))
			{
				Result.Treasure.FactFish = FactFish;
				Result.Treasure.FactIndex = FactIndex;
			}
		}
	}
}

void FVGameU6::AfterCatch(UFVGame& G, FFVCatchResult& Result)
{
	UFVSaveGame* P = G.GetProfile();
	if (!P) { return; }
	// §5.1: scrap caught on the rod becomes the material mat_scrap
	if (Result.Junk && Result.Junk->Style == EFVJunkStyle::Scrap && Result.bStored)
	{
		static const FName Scrap(TEXT("mat_scrap"));
		const TArray<FFVInvStack>& Pack = P->GetContainer(EFVContainer::Backpack);
		const int32 Idx = Pack.IndexOfByPredicate([&Result](const FFVInvStack& S) { return S.ItemId == Result.Junk->Id; });
		if (FVDB::FindItem(Scrap) && Idx != INDEX_NONE && P->RemoveFrom(EFVContainer::Backpack, Idx, 1))
		{
			if (!P->AddTo(EFVContainer::Backpack, Scrap, 1)) { P->AddTo(EFVContainer::Backpack, Result.Junk->Id, 1); }
		}
	}
	if (Result.bExpert && Result.Fish) { P->RecordExpertCatch(Result); }
	if (Result.bTreasure && !Result.Treasure.FactFish.IsNone())
	{
		if (P->LearnFact(Result.Treasure.FactFish, Result.Treasure.FactIndex))
		{
			G.Toast(FString::Printf(TEXT("Страница журнала: новое сведение — «%s»"), *FVDB::ItemName(Result.Treasure.FactFish)),
				FVPal::Gold(), EFVIcon::Scroll);
			FVTutorialAnnounce(&G, FName(TEXT("g_facts")));
		}
	}
	G.bProfileDirty = true;
}

void FVGameU6::FillCatchContext(const UFVGame& G, FFVCatchContext& Ctx)
{
	const UFVSaveGame* P = G.GetProfile();
	Ctx.bExpertRod = P && P->IsExpertRodEquipped();
	Ctx.bExpertZone = FVDBU6::IsExpertLocation(Ctx.Location);
	// §18 «Старая мельница»: the zone the player stands in (Omut / a baited feeder deck) and the fish pen
	if (P && G.Scene && FVMill::IsMillId(Ctx.Location))
	{
		const double Now = G.GetAbsMinutes();
		const FVector2D Feet = G.Scene->GetPlayerPos();
		for (const FVWorldU6::FAnchor& Z : FVWorldU6::MillZones())
		{
			if (Z.Kind != FVWorldU6::EAnchor::MillZone || !Z.Hit.Contains(Feet)) { continue; }
			if (Z.Id == FVMill::ZoneOmutId()) { Ctx.bMillOmut = P->IsOmutOpen(); continue; }
			for (int32 d = 0; d < FVMill::FeedDecks; ++d)
			{
				if (Z.Id == FVMill::ZoneDeckId(d) && P->IsDeckBaited(d, Now)) { Ctx.bMillDeck = true; }
			}
		}
		Ctx.MillPenFish = P->ActivePenFish(Now);
	}
}

void FVGameU6::DrawOverlay(UFVGame& G, FVDraw& D)
{
	using namespace FVGameU6Impl;
	(void)G;
	// Р69: the fade of a walk between places (over the world and the HUD)
	if (GWalkT >= 0.f)
	{
		const float FA = GWalkT < WalkFadeOut ? GWalkT / WalkFadeOut : 1.f - (GWalkT - WalkFadeOut) / WalkFadeIn;
		const FFVRect Scr = D.ScreenRect();
		D.Rect(FFVRect(Scr.X - 8.f, Scr.Y - 8.f, Scr.W + 16.f, Scr.H + 16.f), FLinearColor(0.f, 0.f, 0.f, FMath::Clamp(FA, 0.f, 1.f)));
	}
	if (GBanner.IsEmpty() || GBannerAge >= BannerLife) { return; }
	const float T = GBannerAge / BannerLife;
	const float A = T < 0.08f ? T / 0.08f : (T > 0.8f ? (1.f - T) / 0.2f : 1.f);
	const bool bPortrait = FV::IsPortrait();
	const FFVRect Scr = D.ScreenRect();
	const float W = FMath::Min(Scr.W - 24.f, bPortrait ? 340.f : 420.f);
	const float H = bPortrait ? 70.f : 56.f;
	const FFVRect R(Scr.Center().X - W * 0.5f, Scr.Y + Scr.H * (bPortrait ? 0.22f : 0.18f), W, H);
	D.Rect(R, FV::Col(20, 16, 30, uint8(210.f * FMath::Clamp(A, 0.f, 1.f))));
	D.Frame(R, FV::Col(255, 204, 51, uint8(255.f * FMath::Clamp(A, 0.f, 1.f))), 2.f);
	FLinearColor Gold = FVPal::Gold(); Gold.A = FMath::Clamp(A, 0.f, 1.f);
	FLinearColor White = FVPal::White(); White.A = Gold.A;
	const float TS = bPortrait ? 17.f : 15.f;
	D.TextOutlined(GBanner, FVector2D(R.Center().X, R.Y + 10.f), TS, Gold, EFVAlign::Center, FV::Col(40, 22, 12, uint8(255.f * Gold.A)));
	if (!GBannerSub.IsEmpty())
	{
		D.Text(GBannerSub, FVector2D(R.Center().X, R.Bottom() - (bPortrait ? 22.f : 18.f)), FV::Ui::Body(), White, EFVAlign::Center, true);
	}
}

// ============================================================================================
// UFVGame — update 6 members
// ============================================================================================
FFVLocalWeather UFVGame::GetLocalWeather() const
{
	const FName Loc = Scene ? Scene->GetLocationId() : NAME_None;
	if (!FVDBU6::IsExpertLocation(Loc)) { return FFVLocalWeather(); }
	return FVDBU6::LocalWeather(Loc, GetDay(), GetClockMinutes(), GetWeather());
}

double UFVGame::GetPlayedMinutes() const
{
	return Profile ? Profile->U6.PlayedMinutes : 0.0;
}

bool UFVGame::TravelToEntry(FName LocationId, FName Entry)
{
	if (!TravelTo(LocationId)) { return false; }
	// Р69: a one-screen place walked into from a neighbour ("from_<place>"): appear at the exit back there
	const bool bWalkEntry = !Entry.IsNone() && Entry.ToString().StartsWith(TEXT("from_")) && Entry != FName(TEXT("from_map"));
	if (Scene && !Entry.IsNone() && (Scene->IsBigWorld() || bWalkEntry))
	{
		Scene->LoadLocationAt(LocationId, Entry);
		Scene->RefreshAppearance();
		Scene->SnapCamera();
	}
	return true;
}

FFVExpertCatchContext UFVGame::MakeExpertContext(float Depth) const
{
	FFVExpertCatchContext Ctx;
	Ctx.Location = Scene ? Scene->GetLocationId() : NAME_None;
	Ctx.PhaseMask = GetPhaseMask();
	Ctx.Weather = GetWeather();
	Ctx.Local = GetLocalWeather();
	Ctx.Level = Profile ? Profile->GetLevel() : 1;
	Ctx.Depth = FMath::Clamp(Depth, 0.f, 1.f);
	if (Profile)
	{
		Ctx.RodId = Profile->EquippedRod;
		Ctx.Bait = Profile->EquippedBait;
		if (const FFVExpertRodDef* R = FVDBU6::FindExpertRod(Ctx.RodId))
		{
			Ctx.Type = R->Type;
			const FFVExpertLoadout L = Profile->GetExpertLoadout(Ctx.RodId);
			Ctx.Line = FVDBU6::LineKindOf(L.Line);
			Ctx.Float = FVDBU6::FloatKindOf(L.Float);
			Ctx.Reel = FVDBU6::ReelKindOf(L.Reel);
		}
	}
	return Ctx;
}

FFVExpertStats UFVGame::GetEffectiveExpertStats() const
{
	if (!Profile || !Profile->IsExpertRodEquipped()) { return FFVExpertStats(); }
	return Profile->GetExpertStats(Profile->EquippedRod, GetAbsMinutes());
}

bool UFVGame::IsExpertRodEquipped() const
{
	return Profile && Profile->IsExpertRodEquipped();
}

bool UFVGame::IsHudColumnHidden() const
{
	return Fishing.IsValid() && Fishing->IsExpertFight();
}
