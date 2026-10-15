// UFVGame — update 2 (Core module): screens with a context, absolute game time (day rollover, passive systems,
// sleeping), the home (yard <-> house) and the world interaction (hover highlight, click / tap / E, walk-to,
// Interact dispatch), effective rod stats. See Docs/UPDATE_HOME.md §7-§10.

#include "FVGame.h"
#include "FVAudio.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"
#include "FVGameU6.h"          // update 6 hooks (GAME-FLOW)
#include "FVOnline.h"
#include "FVSaveGame.h"
#include "FVSprites.h"

namespace FVGameHomeImpl
{
	constexpr int32 MaxSleepLog = 48;
	constexpr int32 MaxSleepsInARow = 2;               // §0.2 (Р44)
	constexpr float SleepEveningMinutes = 18.f * 60.f;  // the streak resets when the clock reaches 18:00 awake

	/** Stable key of a world object (kind + index + id) for the walk-to target (objects are rebuilt often). */
	FName ObjectKey(const FFVWorldObject& O)
	{
		return FName(*FString::Printf(TEXT("%d|%d|%s"), int32(O.Kind), O.Index, *O.Id.ToString()));
	}

	const FFVWorldObject* FindByKey(const FVScene& S, FName Key)
	{
		if (Key.IsNone())
		{
			return nullptr;
		}
		for (const FFVWorldObject& O : S.GetWorldObjects())
		{
			if (ObjectKey(O) == Key)
			{
				return &O;
			}
		}
		return nullptr;
	}

	const FFVWorldObject* FindKind(const FVScene& S, EFVObjKind Kind)
	{
		for (const FFVWorldObject& O : S.GetWorldObjects())
		{
			if (O.Kind == Kind)
			{
				return &O;
			}
		}
		return nullptr;
	}

	/** Verb of the interaction prompt ("[E] Собрать: Ветка"). */
	FString VerbFor(const FFVWorldObject& O, bool bInHouse)
	{
		switch (O.Kind)
		{
		case EFVObjKind::Resource: return TEXT("Собрать");
		case EFVObjKind::Stall: return TEXT("Торговать");
		case EFVObjKind::OrderBoard: return TEXT("Посмотреть");
		case EFVObjKind::NetSpot: return TEXT("Осмотреть");
		case EFVObjKind::DigSpot: return TEXT("Копать");
		case EFVObjKind::Door: return bInHouse ? TEXT("Выйти") : TEXT("Войти");
		case EFVObjKind::Station:
			if (const FFVStationDef* Def = FVDB::FindStation(O.Id))
			{
				if (Def->Station == EFVStation::Bed) { return TEXT("Лечь спать"); }
			}
			return TEXT("Открыть");
		case EFVObjKind::DecorCatalog: return TEXT("Открыть");
		default: return TEXT("Открыть");
		}
	}

	/** Update 3: a desktop LMB press was used for a world object (interaction / walk-to). Until the button goes up, the
	    rest of that click (down / release) is hidden from the widgets, so it can never click a screen or a dialog that
	    the interaction opened under the cursor (widget clicks fire on release). */
	bool GSwallowLmb = false;

	FString LabelOf(const FFVWorldObject& O)
	{
		if (!O.Label.IsEmpty())
		{
			return O.Label;
		}
		return O.Id.IsNone() ? FString(TEXT("…")) : FVDB::ItemName(O.Id);
	}

	/** Russian plural form: 1 рыба, 2 рыбы, 5 рыб. */
	const TCHAR* Plural(int64 N, const TCHAR* One, const TCHAR* Few, const TCHAR* Many)
	{
		const int64 A = (N < 0 ? -N : N) % 100;
		const int64 B = A % 10;
		if (A > 10 && A < 20) { return Many; }
		if (B == 1) { return One; }
		if (B >= 2 && B <= 4) { return Few; }
		return Many;
	}

	/** Passive-system state compared before/after a clock advance (notifications & the sleep summary). */
	struct FPassiveSnapshot
	{
		int32 TornNets = 0;
		int32 SmokeReady = 0;
		int32 NetFish = 0;
	};

	FPassiveSnapshot Snapshot(const UFVSaveGame& P)
	{
		FPassiveSnapshot S;
		for (const FFVNetState& N : P.Nets)
		{
			S.TornNets += N.bTorn ? 1 : 0;
		}
		for (const FFVSmokeSlot& Slot : P.Smokehouse)
		{
			S.SmokeReady += Slot.MinutesLeft <= 0.f ? 1 : 0;
		}
		S.NetFish = P.Stats.NetFish;
		return S;
	}
}

// ============================================================================================
// Screens with a context
// ============================================================================================
void UFVGame::ReturnFromMiniGame(FName StationId)
{
	// Mini-games change the world (dug spots, gathered materials): rebuild the objects before the world shows again.
	if (Scene)
	{
		Scene->RefreshWorldObjects();
	}
	if (StationId.IsNone() || !FVDB::FindStation(StationId) || !bInGame)
	{
		CloseScreen();
		return;
	}
	OpenScreenWith(EFVScreen::Station, StationId);
}

// ============================================================================================
// Time
// ============================================================================================
double UFVGame::GetAbsMinutes() const
{
	return double(GetDay() - 1) * 1440.0 + double(GetClockMinutes());
}

EFVWeather UFVGame::GetWeatherTomorrow() const
{
	if (!Profile || Profile->WeatherTomorrow >= uint8(EFVWeather::Count))
	{
		return EFVWeather::Sunny;
	}
	return EFVWeather(Profile->WeatherTomorrow);
}

FString UFVGame::AbsTimeText(double AbsMinutes) const
{
	const double A = FMath::Max(0.0, AbsMinutes);
	const int32 Day = int32(A / 1440.0) + 1;
	const int32 M = FMath::Clamp(int32(A - double(Day - 1) * 1440.0), 0, 1439);
	return FString::Printf(TEXT("День %d, %02d:%02d"), Day, M / 60, M % 60);
}

FString UFVGame::DurationText(float Minutes)
{
	const int32 Total = FMath::Max(0, FMath::CeilToInt(Minutes - 0.001f));
	const int32 H = Total / 60;
	const int32 M = Total % 60;
	if (H > 0 && M > 0)
	{
		return FString::Printf(TEXT("%d ч %d мин"), H, M);
	}
	if (H > 0)
	{
		return FString::Printf(TEXT("%d ч"), H);
	}
	return FString::Printf(TEXT("%d мин"), M);
}

void UFVGame::AdvanceClock(float Minutes, bool bQuiet)
{
	using namespace FVGameHomeImpl;
	if (!Profile || !(Minutes > 0.f))
	{
		return;
	}
	// Never more than a month at once (debug calls); the loop below walks midnight by midnight.
	double Remaining = FMath::Min(double(Minutes), 30.0 * 1440.0);
	int32 Guard = 0;
	while (Remaining > 1e-6 && Guard++ < 128)
	{
		const float Clock = FMath::Clamp(Profile->TimeMinutes, 0.f, 1440.f);
		const double Step = FMath::Min(Remaining, double(1440.f - Clock));
		const double FromAbs = double(FMath::Max(1, Profile->Day) - 1) * 1440.0 + double(Clock);
		const double ToAbs = FromAbs + Step;
		const FPassiveSnapshot Before = Snapshot(*Profile);

		Profile->TimeMinutes = float(double(Clock) + Step);
		Remaining -= Step;
		// §0.2 (Р44): waiting until the evening awake (not through sleep, which advances the clock quietly) allows the bed again
		if (!bQuiet && Clock < FVGameHomeImpl::SleepEveningMinutes && Profile->TimeMinutes >= FVGameHomeImpl::SleepEveningMinutes
			&& Profile->U6.SleepStreak != 0)
		{
			Profile->U6.SleepStreak = 0;
			bProfileDirty = true;
		}
		if (FMath::FloorToInt(Clock / 60.f) != FMath::FloorToInt(Profile->TimeMinutes / 60.f))
		{
			bProfileDirty = true;   // keep the clock in the autosave
		}

		// Nets, smokehouse, food: everything that runs on game time.
		if (Step > 0.0)
		{
			Profile->SimulatePassive(FromAbs, ToAbs, GetWeather());
			FVGameU6::OnAdvanceClock(*this, FromAbs, ToAbs, bQuiet);   // update 6: artel
		}

		// Order deadlines
		const TArray<FName> Expired = Profile->ExpireOrders(ToAbs);
		for (const FName& Id : Expired)
		{
			const FFVOrder* Ord = Profile->Orders.FindByPredicate([&Id](const FFVOrder& O) { return O.Id == Id; });
			const FString Text = Ord
				? FString::Printf(TEXT("Заказ сорван: %s — %s"), *Ord->Customer, *FVDB::StackName(Ord->ItemId, Ord->bSmoked ? FVStackFlags::Smoked : 0))
				: FString(TEXT("Заказ сорван: срок вышел, залог потерян"));
			if (bQuiet)
			{
				if (SleepLog.Num() < MaxSleepLog) { SleepLog.Add(Text); }
			}
			else
			{
				Toast(Text, FV::Col(255, 150, 130), EFVIcon::Order);
				if (Audio) { Audio->Play(EFVSfx::Error, 0.6f); }
			}
			bProfileDirty = true;
		}

		// Live notifications (the sleep summary reports these itself)
		if (!bQuiet && bInGame)
		{
			const FPassiveSnapshot After = Snapshot(*Profile);
			if (After.TornNets > Before.TornNets)
			{
				Toast(TEXT("Сеть порвалась — почините её верёвкой и волокном"), FV::Col(255, 150, 130), EFVIcon::Net);
				if (Audio) { Audio->Play(EFVSfx::NetTear, 0.8f); }
				bProfileDirty = true;
			}
			if (After.SmokeReady > Before.SmokeReady)
			{
				Toast(TEXT("Коптильня: рыба готова — заберите её"), FV::Col(255, 220, 160), EFVIcon::Smoke);
				bProfileDirty = true;
			}
		}

		if (Profile->TimeMinutes >= 1440.f - 1e-3f)
		{
			Profile->TimeMinutes = 0.f;
			StartNewDay(bQuiet);
		}
	}
}

void UFVGame::StartNewDay(bool bQuiet)
{
	using namespace FVGameHomeImpl;
	if (!Profile)
	{
		return;
	}
	Profile->Day = FMath::Max(1, Profile->Day + 1);
	// Today's weather = yesterday's forecast (the radio told the truth); roll the next forecast.
	const EFVWeather NewWeather = GetWeatherTomorrow();
	Profile->Weather = uint8(NewWeather);
	Profile->WeatherTomorrow = uint8(FMath::FRand() < 0.3f ? EFVWeather::Rain : EFVWeather::Sunny);
	bProfileDirty = true;

	// Worm farm, order board... (Data module)
	TArray<FString> Lines = Profile->OnNewDay(GetAbsMinutes());
	FVGameU6::OnNewDay(*this, Lines);   // update 6: City tasks, artel

	const bool bRain = NewWeather == EFVWeather::Rain;
	if (bQuiet)
	{
		for (const FString& L : Lines)
		{
			if (SleepLog.Num() < MaxSleepLog && !L.IsEmpty()) { SleepLog.Add(L); }
		}
	}
	else
	{
		Toast(FString::Printf(TEXT("День %d · %s"), Profile->Day, *FVDB::WeatherName(NewWeather)), FVPal::White(), bRain ? EFVIcon::Rain : EFVIcon::Moon);
		for (const FString& L : Lines)
		{
			Toast(L, FV::Col(214, 240, 190), EFVIcon::House);
		}
	}
	if (Audio && bInGame)
	{
		Audio->SetRain(bRain);
	}
	// New day: fresh resources on the ground, dig spots can be used again.
	if (Scene)
	{
		Scene->RefreshWorldObjects();
	}
	UE_LOG(LogFV, Log, TEXT("FV: new day %d, weather %s, tomorrow %s%s"), Profile->Day, bRain ? TEXT("rain") : TEXT("sunny"),
		GetWeatherTomorrow() == EFVWeather::Rain ? TEXT("rain") : TEXT("sunny"), bQuiet ? TEXT(" (quiet)") : TEXT(""));
}

void UFVGame::ConfirmSleep()
{
	if (!Profile || !bInGame)
	{
		return;
	}
	const float Clock = GetClockMinutes();
	const bool bDaylight = Clock >= FV::DayStartMinutes && Clock < 18.f * 60.f;
	TWeakObjectPtr<UFVGame> WeakThis(this);
	FFVModal M;
	M.Title = TEXT("Кровать");
	M.Icon = EFVIcon::Bed;
	// §0.2 (Р44): two sleeps in a row at most; then the bed waits for the evening (the streak resets at 18:00 awake)
	if (Profile->U6.SleepStreak >= FVGameHomeImpl::MaxSleepsInARow)
	{
		M.Body = TEXT("Вы уже выспались.");
		M.Lines = {
			TEXT("Два сна подряд — больше не уснуть"),
			TEXT("Лечь снова можно вечером — с 18:00"),
		};
		M.Buttons = { TEXT("Понятно") };
		ShowModal(MoveTemp(M));
		return;
	}
	M.Body = bDaylight ? TEXT("Ещё светло. Всё равно лечь спать?") : TEXT("Лечь спать до утра?");
	M.Lines = {
		TEXT("Проснётесь в 06:00"),
		TEXT("Пока вы спите, сети ловят рыбу, а коптильня работает"),
	};
	M.Buttons = { TEXT("Спать"), TEXT("Не сейчас") };
	M.OnChoice = [WeakThis](int32 Index)
	{
		if (Index == 0)
		{
			if (UFVGame* G = WeakThis.Get())
			{
				G->SleepUntilMorning();
			}
		}
	};
	ShowModal(MoveTemp(M));
}

void UFVGame::SleepUntilMorning()
{
	using namespace FVGameHomeImpl;
	if (!Profile || !Fishing || !bInGame)
	{
		return;
	}
	Fishing->Reset();
	PendingInteractKey = NAME_None;
	if (Scene)
	{
		Scene->ClearWalkTarget();
	}

	float Minutes = FV::DayStartMinutes - Profile->TimeMinutes;
	if (Minutes <= 0.f)
	{
		Minutes += 1440.f;
	}
	const FPassiveSnapshot Before = Snapshot(*Profile);
	SleepLog.Reset();
	Profile->U6.SleepStreak = FMath::Max(0, Profile->U6.SleepStreak) + 1;   // §0.2 (Р44)
	if (Audio)
	{
		Audio->Play(EFVSfx::Sleep);
	}
	AdvanceClock(Minutes, true);
	Profile->TimeMinutes = FV::DayStartMinutes;   // exact 06:00 (no float drift)
	const FPassiveSnapshot After = Snapshot(*Profile);

	FFVModal M;
	M.Title = TEXT("Доброе утро!");
	M.Icon = EFVIcon::Sun;
	M.Body = FString::Printf(TEXT("День %d · %s. Вы отлично выспались!"), GetDay(), *FVDB::WeatherName(GetWeather()));
	M.Lines = SleepLog;
	const int32 NetFish = After.NetFish - Before.NetFish;
	if (NetFish > 0)
	{
		M.Lines.Add(FString::Printf(TEXT("Сети: поймано %d %s — заберите улов"), NetFish, Plural(NetFish, TEXT("рыба"), TEXT("рыбы"), TEXT("рыб"))));
	}
	const int32 Torn = After.TornNets - Before.TornNets;
	if (Torn > 0)
	{
		M.Lines.Add(FString::Printf(TEXT("Порвалось сетей: %d — почините их"), Torn));
	}
	const int32 Smoked = After.SmokeReady - Before.SmokeReady;
	if (Smoked > 0)
	{
		M.Lines.Add(FString::Printf(TEXT("Коптильня: готово %d %s"), Smoked, Plural(Smoked, TEXT("рыба"), TEXT("рыбы"), TEXT("рыб"))));
	}
	if (M.Lines.Num() == 0)
	{
		M.Lines.Add(TEXT("Ночь прошла спокойно"));
	}
	M.Buttons = { TEXT("Новый день!") };
	SleepLog.Reset();
	ShowModal(MoveTemp(M));

	NotifyProfileChanged();   // achievements (nets, worms...) are announced after waking up
	if (Scene)
	{
		Scene->RefreshWorldObjects();
	}
	bProfileDirty = true;
	SaveNow();
	if (Online)
	{
		// update 4: sync soon and back the save up after the night
		Online->NotifyImportantEvent();
		Online->RequestSync(true);
	}
	UE_LOG(LogFV, Log, TEXT("FV: slept %.0f min -> day %d 06:00 (net fish %d, torn %d, smoked %d)"), Minutes, GetDay(), NetFish, Torn, Smoked);
}

// ============================================================================================
// Home
// ============================================================================================
bool UFVGame::IsAtHome() const
{
	return Scene && FVDB::IsHomeId(Scene->GetLocationId());
}

bool UFVGame::IsInHouse() const
{
	return Scene && Scene->GetLocationId() == FVDB::HouseId();
}

EFVTheme UFVGame::MusicThemeFor(FName LocationId) const
{
	if (FVDB::IsHomeId(LocationId))
	{
		return EFVTheme::Home;   // yard and house share the waltz: no crossfade at the door
	}
	const FFVLocationDef* Loc = FVDB::FindLocation(LocationId);
	return Loc ? Loc->Theme : EFVTheme::Pond;
}

void UFVGame::LoadHomeScene(FName LocationId, bool bSpawnAtDoor)
{
	if (!Scene || !Fishing || !Profile || !bInGame)
	{
		return;
	}
	Fishing->Reset();
	PendingInteractKey = NAME_None;
	Scene->LoadLocation(LocationId);
	Scene->RefreshAppearance();
	Profile->CurrentLocation = LocationId;
	if (bSpawnAtDoor)
	{
		if (const FFVWorldObject* Door = FVGameHomeImpl::FindKind(*Scene, EFVObjKind::Door))
		{
			Scene->DebugTeleport(Door->Feet);
		}
	}
	if (Audio)
	{
		Audio->SetReeling(false);
		Audio->SetMusicTheme(MusicThemeFor(LocationId));
		Audio->SetRain(GetWeather() == EFVWeather::Rain);
	}
	bProfileDirty = true;
}

void UFVGame::EnterHouse()
{
	if (!bInGame || IsWorldBusy() || IsInHouse())
	{
		return;
	}
	LoadHomeScene(FVDB::HouseId(), true);
	if (Audio)
	{
		Audio->Play(EFVSfx::Door);
	}
	UE_LOG(LogFV, Log, TEXT("FV: entered the house"));
}

void UFVGame::ExitHouse()
{
	if (!bInGame || IsWorldBusy() || !IsInHouse())
	{
		return;
	}
	LoadHomeScene(FVDB::HomeId(), true);
	if (Audio)
	{
		Audio->Play(EFVSfx::Door);
	}
	UE_LOG(LogFV, Log, TEXT("FV: left the house"));
}

// ============================================================================================
// World interaction
// ============================================================================================
bool UFVGame::IsWorldBusy() const
{
	return Fishing && Fishing->IsBusy();
}

FName UFVGame::GetNearbyTrader() const
{
	if (!Scene)
	{
		return NAME_None;
	}
	for (const FFVWorldObject& O : Scene->GetWorldObjects())
	{
		if (O.Kind == EFVObjKind::Stall && Scene->IsInReach(O))
		{
			return O.Id.IsNone() ? FVDB::DefaultTraderId() : O.Id;
		}
	}
	return NAME_None;
}

FFVRodStats UFVGame::GetEffectiveRodStats() const
{
	FFVRodStats S;
	if (Profile)
	{
		// update 5: one pure composition shared with the autotest (rod + installed line & float + active food buff)
		const FFVRodDef& Rod = Profile->GetRod();
		const FFVRodLoadout L = Profile->GetLoadout(Rod.Id);
		const FFVStatMods Food = Profile->GetFoodMods(GetAbsMinutes());   // neutral when none
		S = FVDB::ComposeRodStats(Rod, L.Line, L.Float, &Food);
		// §18.1: a baited feeder deck at the mill -> faster bites (rare / epic x1.5 is applied in FVMill::CatchWeightMult)
		if (Scene && FVMill::IsMillId(Scene->GetLocationId()))
		{
			FFVCatchContext MillCtx;
			MillCtx.Location = Scene->GetLocationId();
			FVGameU6::FillCatchContext(*this, MillCtx);
			if (MillCtx.bMillDeck) { S.BiteSpeed *= FVMill::DeckBiteSpeed; }
		}
	}
	else if (FVDB::Rods().Num() > 0)
	{
		S = FFVRodStats::FromRod(FVDB::Rods()[0]);
	}
	return S;
}

void UFVGame::Interact(const FFVWorldObject& Obj)
{
	if (!Profile || !Scene || !bInGame)
	{
		return;
	}
	// update 6: new object kinds (trees, villagers, yard buildings, boat...) and Р35 (no nets in the expert zone)
	if (FVGameU6::Interact(*this, Obj))
	{
		return;
	}
	// Copy: RefreshWorldObjects() below rebuilds the array Obj may point into.
	const FFVWorldObject O = Obj;
	const FName Loc = Scene->GetLocationId();
	const FName SpotCtx(*FString::Printf(TEXT("%s:%d"), *Loc.ToString(), O.Index));
	auto Refuse = [this](const FString& Text, EFVIcon Icon)
	{
		Toast(Text, FV::Col(255, 150, 130), Icon);
		if (Audio) { Audio->Play(EFVSfx::Error, 0.6f); }
	};

	switch (O.Kind)
	{
	case EFVObjKind::Resource:
	{
		if (IsWorldBusy())
		{
			break;
		}
		int32 Lying = 0;
		if (Profile->ResourceAt(Loc, O.Index, Lying).IsNone())
		{
			Scene->RefreshWorldObjects();   // already taken (stale object)
			break;
		}
		FName Item;
		int32 Count = 0;
		if (Profile->Gather(Loc, O.Index, Item, Count))
		{
			Toast(FString::Printf(TEXT("+%d %s"), FMath::Max(1, Count), *FVDB::ItemName(Item)), FV::Col(200, 240, 170), EFVIcon::Hand);
			if (Sprites)
			{
				Scene->PlayPickup(O.Hit.W > 0.f ? O.Hit.Center() : O.Feet, Sprites->ItemIcon(Item));
			}
			if (Audio) { Audio->Play(EFVSfx::Pickup); }
			NotifyProfileChanged();
			Scene->RefreshWorldObjects();
		}
		else
		{
			Refuse(TEXT("Рюкзак полон — освободите место"), EFVIcon::Backpack);
		}
		break;
	}
	case EFVObjKind::Stall:
		TryOpenFromWorldWith(EFVScreen::Shop, O.Id.IsNone() ? FVDB::DefaultTraderId() : O.Id);
		break;
	case EFVObjKind::OrderBoard:
		TryOpenFromWorldWith(EFVScreen::Orders, NAME_None);
		break;
	case EFVObjKind::NetSpot:
		TryOpenFromWorldWith(EFVScreen::NetSpot, SpotCtx);
		break;
	case EFVObjKind::DigSpot:
		if (Profile->ShovelLevel < 1)
		{
			Refuse(TEXT("Нужна лопата — купите у Митрича"), EFVIcon::Shovel);
		}
		else if (Profile->IsDugToday(Loc, O.Index))
		{
			Refuse(TEXT("Здесь уже копали сегодня"), EFVIcon::Shovel);
		}
		else
		{
			TryOpenFromWorldWith(EFVScreen::Digging, SpotCtx);
		}
		break;
	case EFVObjKind::Door:
		if (IsInHouse())
		{
			ExitHouse();
		}
		else
		{
			EnterHouse();
		}
		break;
	case EFVObjKind::Station:
	{
		const FFVStationDef* Def = FVDB::FindStation(O.Id);
		if (!Def)
		{
			break;
		}
		switch (Def->Station)
		{
		case EFVStation::Bed:
			if (!IsWorldBusy())
			{
				ConfirmSleep();
			}
			break;
		case EFVStation::Chest:
			TryOpenFromWorldWith(EFVScreen::Chest, NAME_None);
			break;
		case EFVStation::IceCellar:
			if (Profile->IsStationBuilt(EFVStation::IceCellar))
			{
				TryOpenFromWorldWith(EFVScreen::IceCellar, NAME_None);
			}
			else
			{
				TryOpenFromWorldWith(EFVScreen::Station, Def->Id);
			}
			break;
		default:
			TryOpenFromWorldWith(EFVScreen::Station, Def->Id);
			break;
		}
		break;
	}
	case EFVObjKind::DecorCatalog:
		TryOpenFromWorldWith(EFVScreen::HomeDecor, NAME_None);
		break;
	default:
		break;
	}
}

void UFVGame::TickWorldInteraction(FFVInput& In)
{
	using namespace FVGameHomeImpl;
	// The rest of a click that was used for a world object (see GSwallowLmb). Runs before any early return: the
	// interaction may have opened a screen or a dialog.
	if (GSwallowLmb)
	{
		const bool bStillDown = In.bLmbDown && !In.bLmbReleased;
		In.bLmbPressed = false;
		In.bLmbDown = false;
		In.bLmbReleased = false;
		GSwallowLmb = bStillDown && !Touch.IsValid();
	}
	InteractHint.Reset();
	if (!Scene || !Fishing || !Profile)
	{
		return;
	}
	const bool bWorldInteractive = bInGame && CurrentScreen == EFVScreen::None && !HasModal();
	if (!bWorldInteractive || Fishing->GetState() != EFVFishState::Idle)
	{
		Scene->SetHighlight(nullptr);
		if (bWorldInteractive && !PendingInteractKey.IsNone())
		{
			// Started fishing (or anything else) on the way: forget the walk-to target.
			PendingInteractKey = NAME_None;
			Scene->ClearWalkTarget();
		}
		return;
	}
	const bool bInHouse = IsInHouse();
	const bool bTouchUI = Touch.IsValid();

	// ---- walking to an object: interact on arrival; manual movement / getting stuck cancels (FVScene)
	if (!PendingInteractKey.IsNone())
	{
		const FFVWorldObject* Target = FindByKey(*Scene, PendingInteractKey);
		if (Target && Scene->IsInReach(*Target))
		{
			const FFVWorldObject Copy = *Target;
			PendingInteractKey = NAME_None;
			Scene->ClearWalkTarget();
			Scene->SetHighlight(nullptr);
			Interact(Copy);
			return;
		}
		if (!Target || Scene->WalkTargetFailed() || !Scene->HasWalkTarget())
		{
			PendingInteractKey = NAME_None;
			Scene->ClearWalkTarget();
		}
	}

	// ---- what is under the cursor / in reach. Update 6 (§2): the input is in screen px, the scene in world px (big
	// locations scroll with the camera; on the classic one-screen locations both are the same).
	const FFVWorldObject* Hovered = nullptr;
	if (!bTouchUI && In.bMouseValid && !bUIHoveredLastFrame)
	{
		Hovered = Scene->ObjectAt(Scene->ScreenToWorld(In.Mouse));
	}
	const FFVWorldObject* Nearest = Scene->NearestInReach();

	// ---- input (update 3: "tap anywhere"): the highlighted object in reach is used by a plain click / tap anywhere on
	// the world; a click / tap ON another object uses that one (walks there first when it is out of reach).
	//   desktop: LMB on an object, else LMB anywhere on the world with an object in reach (Space still casts)
	//   touch:   a short tap on an object, else a short tap anywhere on the world with an object in reach
	//   E / RMB / gamepad X: the nearest object in reach
	// (While walking to an object, a click / tap on empty ground does not redirect the walk.)
	const bool bCanUseNearest = Nearest && PendingInteractKey.IsNone();
	const FFVWorldObject* Chosen = nullptr;
	if (!bTouchUI && In.bLmbPressed && In.bMouseValid && !bUIHoveredLastFrame)
	{
		Chosen = Hovered ? Hovered : (bCanUseNearest ? Nearest : nullptr);
		if (Chosen)
		{
			// consumed: no cast, and the release can't click a screen / dialog opened by the interaction
			In.bLmbPressed = false;
			In.bLmbDown = false;
			GSwallowLmb = true;
		}
	}
	if (!Chosen && In.bWorldTap)
	{
		Chosen = Scene->ObjectAt(Scene->ScreenToWorld(In.WorldTapPos));   // WorldTapPos is in screen px (FVTouch)
		if (!Chosen && bCanUseNearest)
		{
			Chosen = Nearest;
		}
	}
	const bool bInteractKey = In.bInteractPressed || In.bRmbPressed || In.WasPressed(EKeys::E);
	if (!Chosen && bInteractKey)
	{
		Chosen = Nearest ? Nearest : Hovered;
	}

	if (Chosen)
	{
		if (Scene->IsInReach(*Chosen))
		{
			const FFVWorldObject Copy = *Chosen;
			PendingInteractKey = NAME_None;
			Scene->ClearWalkTarget();
			Scene->SetHighlight(nullptr);
			Interact(Copy);
			return;
		}
		// Out of reach: walk there first (the scene steers around blockers), interact on arrival.
		PendingInteractKey = ObjectKey(*Chosen);
		Scene->SetWalkTarget(Chosen->Feet, FMath::Max(4.f, Chosen->Reach * 0.8f));
	}

	// ---- highlight (the scene draws the outline, the name plate and the "Коснитесь — …" prompt under it)
	const FFVWorldObject* Pending = FindByKey(*Scene, PendingInteractKey);
	const FFVWorldObject* Shown = Pending ? Pending : (Hovered ? Hovered : Nearest);
	Scene->SetHighlight(Shown);
	// Text form of the same prompt (debug / logs; the HUD hint strip is gone in update 3).
	if (Pending)
	{
		InteractHint = FString::Printf(TEXT("Идём: %s…"), *LabelOf(*Pending));
	}
	else if (Shown)
	{
		InteractHint = Scene->IsInReach(*Shown)
			? FString::Printf(TEXT("%s: %s"), *VerbFor(*Shown, bInHouse), *LabelOf(*Shown))
			: FString::Printf(TEXT("Подойти: %s"), *LabelOf(*Shown));
	}
}
