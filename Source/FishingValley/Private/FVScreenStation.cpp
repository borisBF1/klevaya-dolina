// FVScreenStation.cpp — a house station (screen context = station id "st_kitchen"...):
//   not built -> build panel (description, coins, materials have/need, min level, «Построить»);
//   built     -> the station's own content + «Улучшение» tab (next level or «Максимальный уровень»).
// Contents: Кухня (recipes, cooking mini-game), Мастерская (Материалы · Снасти · Сети · Переработка · Оснастка · Лопата ·
// Блёсны, knots mini-game), Коптильня, Червятник, Аквариум, Ледник, Радио, Стена трофеев; Кровать / Сундук are info-only.
// Both orientations.
// Update 4: the lure workshop is the «Блёсны» tab of the Мастерская (a teal-violet tab with a twinkling sparkle): the
// reusable FFVLureWorkshop panel (FVMiniGames.h) lives inside the page from workshop level 2; below that the page shows
// what the lures are and the верстак upgrade (with the «Улучшить» button right there).
// Update 3: every tab is a sliding page (FFVPager: swipe, tab click or Left/Right keys, interruptible animation);
// every list / grid / long text scrolls smoothly (FFVScrollView / FScrollColumn); clicks fire on a clean release.
// Every profile change is collected as an intent while drawing and executed at the very end of Draw().

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVMiniGames.h"
#include "FVDatabaseU6.h"

namespace FVStationScreenImpl
{
	FV_HOME_UI_USING();

	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr float SmokeFuelMax = 720.f;      // spec §4: max smokehouse fuel (game minutes)
	constexpr float SmokeMinutes = 360.f;      // spec §4: smoking time of one fish (game minutes)
	constexpr float NoLimit = 1.0e6f;          // "no bottom" for blocks drawn inside a scrolling column

	enum class ETab : uint8 { Build, Recipes, Materials, Tackle, Nets, Recycle, Rigging, Shovel, Smoke, Worms, Aquarium, Cellar, Radio, Trophies, Info, Lures, Tools, Upgrade };
	enum class EPicker : uint8 { None, Smoke, Aquarium, Trophy };
	enum class EAct : uint8
	{
		None, Upgrade, Craft, Play, Install, RemoveTackle, EquipBait, CraftShovel,
		SmokeLoad, AddFuel, CollectSmoked, CollectWorms, AquaPut, AquaTake, Trophy, OpenPicker, OpenCellar, OpenChest, RadioTune,
		CraftPickaxe, CraftAxe, Unmount
	};

	struct FTabDef
	{
		ETab Tab = ETab::Info;
		FString Label;
	};

	/** One action requested while drawing; executed at the end of the frame (first request wins). */
	struct FIntent
	{
		EAct Act = EAct::None;
		FName Id;
		FName Id2;
		int32 Index = -1;
		int32 Count = 1;
		float Value = 0.f;
	};

	// Remembered per station for this session: coming back from a mini-game lands on the same tab and recipe.
	static int32 GLastTab[int32(EFVStation::Count)] = {};
	static FName GLastRecipe[int32(EFVStation::Count)];

	// ---- rod stat table (rigging preview)
	struct FStatDef
	{
		const TCHAR* Label;
		bool bLowerBetter;
	};
	static const FStatDef GStats[] = {
		{ TEXT("Полоса поимки"), false },     // 0 BarBonus
		{ TEXT("Вываживание"), false },       // 1 ReelMult
		{ TEXT("Потеря при сходе"), true },   // 2 DrainMult
		{ TEXT("Ожидание клёва"), true },     // 3 BiteSpeed
		{ TEXT("Дальность заброса"), false }, // 4 CastRange
		{ TEXT("Удача"), false },             // 5 Luck
		{ TEXT("Сокровища"), false },         // 6 TreasureBonus
		{ TEXT("Размер рыбы"), false },       // 7 SizeBonus
		{ TEXT("Реакция"), false },           // 8 ReactBonus
		{ TEXT("Мусор"), true },              // 9 JunkMult
	};
	constexpr int32 NumStats = UE_ARRAY_COUNT(GStats);

	static float StatValue(const FFVRodStats& S, int32 K)
	{
		switch (K)
		{
		case 0: return S.BarBonus;
		case 1: return S.ReelMult;
		case 2: return S.DrainMult;
		case 3: return S.BiteSpeed;
		case 4: return S.CastRange;
		case 5: return S.Luck;
		case 6: return S.TreasureBonus;
		case 7: return S.SizeBonus;
		case 8: return S.ReactBonus;
		default: return S.JunkMult;
		}
	}

	static FString StatText(float V, int32 K)
	{
		switch (K)
		{
		case 0: return FString::Printf(TEXT("+%d"), FMath::RoundToInt(V));
		case 4: return FVUI::Percent(V);
		case 5: case 6: case 7: return FString(TEXT("+")) + FVUI::Percent(V);
		case 8: return FString::Printf(TEXT("+%s с"), *FVUI::Num(V));
		default: return FString(TEXT("×")) + FVUI::Num(V);
		}
	}

	static bool StatNeutral(float V, int32 K)
	{
		switch (K)
		{
		case 1: case 2: case 3: case 9: return FMath::IsNearlyEqual(V, 1.f, 0.001f);
		case 4: return false;
		default: return FMath::IsNearlyZero(V, 0.001f);
		}
	}

	static bool StatBetter(float V, float Ref, int32 K)
	{
		return GStats[K].bLowerBetter ? V < Ref - 0.0005f : V > Ref + 0.0005f;
	}

	static FLinearColor StatInk(float V, float Ref, int32 K)
	{
		if (StatBetter(V, Ref, K)) { return InkGood(); }
		if (StatBetter(Ref, V, K)) { return InkBad(); }
		return FVPal::Ink();
	}

	/** Height TextWrappedLimited() takes for S with no line cap (the same word wrap as MeasureWrapped, its own line step). */
	static float WrappedLimitedH(const FVDraw& D, const FString& S, float W, float Size, bool bBold = false)
	{
		if (S.IsEmpty()) { return 0.f; }
		const float One = D.MeasureWrapped(TEXT("Щ"), W, Size, bBold, 1.f);
		const int32 N = One > 0.f ? FMath::Max(1, FMath::RoundToInt(D.MeasureWrapped(S, W, Size, bBold, 1.f) / One)) : 1;
		return float(N) * FMath::RoundToFloat(D.LineHeight(Size, bBold) * 0.95f);
	}

	/** Parts packed «a · b · c» into as many lines as they need (FVUI::PackParts cuts after two lines); a part wider
	    than the line is word-wrapped. Draws when bDraw; returns the height used (line step = LineHeight(Size, false)). */
	static float DrawPartsAll(FVDraw& D, const TArray<FString>& Parts, float X, float Y, float W, float Size, const FLinearColor& C, bool bDraw = true)
	{
		const float LH = D.LineHeight(Size, false);
		float H = 0.f;
		FString Cur;
		auto Flush = [&]()
		{
			if (Cur.IsEmpty()) { return; }
			if (D.MeasureText(Cur, Size, false).X <= W)
			{
				if (bDraw) { D.Text(Cur, FVector2D(X, Y + H), Size, C, EFVAlign::Left, false, false); }
				H += LH;
			}
			else
			{
				H += bDraw ? TextWrappedLimited(D, Cur, FVector2D(X, Y + H), W, Size, C, 40) : WrappedLimitedH(D, Cur, W, Size);
			}
			Cur.Reset();
		};
		for (const FString& Part : Parts)
		{
			const FString Try = Cur.IsEmpty() ? Part : Cur + TEXT(" · ") + Part;
			if (Cur.IsEmpty() || D.MeasureText(Try, Size, false).X <= W) { Cur = Try; }
			else { Flush(); Cur = Part; }
		}
		Flush();
		return H;
	}

	static FFVStatMods ModsWith(FName Line, FName Float)
	{
		FFVStatMods M;
		if (const FFVItemDef* L = FVDB::FindItem(Line)) { M.Combine(L->Tackle); }
		if (const FFVItemDef* F = FVDB::FindItem(Float)) { M.Combine(F->Tackle); }
		return M;
	}

	/** Which workshop tab shows a recipe. */
	static ETab WorkshopTabOf(const FFVRecipeDef& R)
	{
		if (R.bRecycle) { return ETab::Recycle; }
		if (const FFVItemDef* It = FVDB::FindItem(R.OutputId))
		{
			if (It->Kind == EFVItemKind::Net) { return ETab::Nets; }
			if (It->Kind == EFVItemKind::Line || It->Kind == EFVItemKind::Float || It->Kind == EFVItemKind::Reel) { return ETab::Tackle; }
		}
		if (FVDB::FindBait(R.OutputId)) { return ETab::Tackle; }   // update 6: the baits of the expert legends (Мастерская 4)
		return ETab::Materials;
	}

	static bool IsRecipeTab(ETab T)
	{
		return T == ETab::Recipes || T == ETab::Materials || T == ETab::Tackle || T == ETab::Nets || T == ETab::Recycle;
	}

	/** Names of the traders whose mask matches ("«Лавка рыбака» (Дед Митрич)"). */
	static FString TraderNames(uint8 Mask)
	{
		TArray<FString> Names;
		for (const FFVTraderDef& T : FVDB::Traders())
		{
			if (Mask & T.Mask) { Names.Add(FString::Printf(TEXT("«%s» (%s)"), *T.ShopTitle, *T.Name)); }
		}
		return FString::Join(Names, TEXT(", "));
	}

	// ---- the lure workshop tab: its own deep teal -> violet colours (update 4)
	inline FLinearColor LureTeal()      { return FV::Col(38, 128, 134); }
	inline FLinearColor LureViolet()    { return FV::Col(106, 72, 164); }
	inline FLinearColor LureEdge()      { return FV::Col(46, 30, 84); }
	inline FLinearColor LureGlint()     { return FV::Col(176, 238, 228); }
	inline FLinearColor LureCream()     { return FV::Col(255, 248, 228); }
	constexpr int32 LureWorkshopLevel = 2;     // the lure workshop opens with the верстак (workshop level 2)

	/** Small four-point sparkle (twinkles with Time; Phase shifts neighbours). */
	static void Sparkle(FVDraw& D, const FVector2D& C, float Time, float Phase, float MaxLen, const FLinearColor& Col)
	{
		const float Tw = 0.5f + 0.5f * FMath::Sin(Time * 3.1f + Phase);
		const float L = FMath::Max(1.f, FMath::RoundToFloat(MaxLen * (0.45f + 0.55f * Tw)));
		const float X = FMath::RoundToFloat(C.X), Y = FMath::RoundToFloat(C.Y);
		D.Glow(C, 2.f + L * 1.6f, FLinearColor(Col.R * 0.35f, Col.G * 0.35f, Col.B * 0.35f, 0.5f * Tw + 0.2f));
		FLinearColor A = Col;
		A.A = 0.55f + 0.45f * Tw;
		D.Rect(FFVRect(X - L, Y, L * 2.f + 1.f, 1.f), A);
		D.Rect(FFVRect(X, Y - L, 1.f, L * 2.f + 1.f), A);
		D.Rect(FFVRect(X, Y, 1.f, 1.f), FLinearColor::White);
	}

	/** Left-to-right teal -> violet gradient made of pixel columns. */
	static void LureGradientH(FVDraw& D, const FFVRect& R, float Alpha = 1.f)
	{
		constexpr int32 Bands = 8;
		const float BW = R.W / float(Bands);
		for (int32 b = 0; b < Bands; ++b)
		{
			const float X0 = FMath::RoundToFloat(R.X + BW * float(b));
			const float X1 = b == Bands - 1 ? R.Right() : FMath::RoundToFloat(R.X + BW * float(b + 1));
			FLinearColor C = FMath::Lerp(LureTeal(), LureViolet(), float(b) / float(Bands - 1));
			C.A = Alpha;
			if (X1 > X0) { D.Rect(FFVRect(X0, R.Y, X1 - X0, R.H), C); }
		}
	}

	/** Lines of the radio host (one per day, the dial cycles through them). */
	static const TCHAR* GRadioLines[] = {
		TEXT("Доброе утро, долина! С вами «Клёвая волна» — самое рыбное радио в округе."),
		TEXT("Говорят, у Митрича снова завезли блёсны. Проверьте, пока не разобрали!"),
		TEXT("Слушатель Петрович передаёт привет всем, у кого сегодня клюёт. И тем, у кого нет, — тоже."),
		TEXT("Совет дня: подбросьте дров в коптильню, копчёная рыба стоит вдвое дороже."),
		TEXT("Бабушка Нюра ищет свежую рыбу. Загляните на доску заказов у пруда!"),
		TEXT("Сети лучше ставить на ночь: к утру в них уже кто-нибудь запутается."),
		TEXT("А теперь — минутка тишины. Слышите? Это клюёт."),
		// update 5: the legends
		TEXT("Рыбак Кузьмич уверяет: хрустальная форель толстую леску видит насквозь. Кузьмич зря не скажет — у него две удочки."),
		TEXT("Штормовое предупреждение с вулкана: там клюёт Жар-рыба. Без жаропрочной лески и Жар-наживки даже не суйтесь!"),
	};
	constexpr int32 NumRadioLines = UE_ARRAY_COUNT(GRadioLines);

	// ============================================================================================
	// The screen
	// ============================================================================================
	class FStationScreen final : public FVScreen
	{
	public:
		explicit FStationScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			TickCount = 0;
			const FName Ctx = Game ? Game->GetScreenContext() : NAME_None;
			const FFVStationDef* Found = FVDB::FindStation(Ctx);
			Station = Found ? Found->Station : EFVStation::Kitchen;
			const int32 SI = int32(Station);
			const bool bValidSI = SI >= 0 && SI < int32(EFVStation::Count);
			for (int32 i = 0; i < NumTabKinds; ++i)
			{
				SelRecipes[i] = NAME_None;
				Qtys[i] = 1;
				EnsureSel[i] = 2;   // first draw: show the selected row instantly
			}
			// the recipe used last time (e.g. before a mini-game) is preselected on its own tab
			if (bValidSI)
			{
				if (const FFVRecipeDef* R = FVDB::FindRecipe(GLastRecipe[SI]))
				{
					SelRecipes[int32(TabOfRecipe(*R))] = R->Id;
				}
			}
			int32 NumTabs = 1;
			if (UFVSaveGame* P = Game ? Game->GetProfile() : nullptr)
			{
				SelRod = P->EquippedRod;
				RadioLine = P->Day;
				NumTabs = Tabs(*P).Num();
			}
			Pager.NumPagesCached = FMath::Max(1, NumTabs);
			Pager.SetPage(bValidSI ? GLastTab[SI] : 0);
			if (Station == EFVStation::Workshop && Game) { Lures = MakeUnique<FFVLureWorkshop>(Game, true); }
			bLuresReady = false;
			if (Station == EFVStation::Radio) { FVUI::Sfx(EFVSfx::Radio, 0.6f); }
		}

		virtual bool HandlesEscape() const override { return true; }

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal())
			{
				return;   // ignore the key press that opened the screen; modals own the keyboard
			}
			const bool bLurePage = IsLurePageActive();
			if (Lures)
			{
				Lures->Tick(Dt, In, bLurePage && bLuresReady && Picker == EPicker::None);   // assembly timer, sparkles, keys
			}
			if (In.WasPressed(EKeys::Escape))
			{
				if (bLurePage && Lures && Lures->IsCardShown())
				{
					Lures->CloseCard();   // Esc first dismisses the discovery card
					return;
				}
				if (Picker != EPicker::None)
				{
					ClosePicker();
					FVUI::Sfx(EFVSfx::Close);
					return;
				}
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
			UFVSaveGame* P = Game->GetProfile();
			if (!P || Picker != EPicker::None)
			{
				return;
			}
			const TArray<FTabDef> T = Tabs(*P);
			Pager.NumPagesCached = FMath::Max(1, T.Num());
			if (PagerKeys(In, Pager)) { return; }   // Left/Right: slide to the neighbouring tab (animated)
			const int32 Dir = In.WasPressed(EKeys::Up) ? -1 : (In.WasPressed(EKeys::Down) ? 1 : 0);
			if (Dir != 0 && T.IsValidIndex(Pager.Page) && IsRecipeTab(T[Pager.Page].Tab))
			{
				const ETab Tab = T[Pager.Page].Tab;
				const int32 TI = int32(Tab);
				const TArray<const FFVRecipeDef*> List = RecipesOf(Tab);
				if (List.Num() > 0)
				{
					int32 Cur = List.IndexOfByPredicate([this, TI](const FFVRecipeDef* R) { return R && R->Id == SelRecipes[TI]; });
					Cur = Cur == INDEX_NONE ? 0 : FMath::Clamp(Cur + Dir, 0, List.Num() - 1);
					SelectRecipe(Tab, List[Cur]->Id);
					EnsureSel[TI] = 1;
					FVUI::Sfx(EFVSfx::Click, 0.6f);
				}
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = bP ? FV::WindowRect() : WinRect;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			const FFVStationDef* Def = GetDef();
			bool bClose = D.Window(Win, Def ? Def->Name : FString(TEXT("Станция")));
			Intent = FIntent();

			if (!P || !Def)
			{
				D.Text(TEXT("Станция недоступна"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { bClose = true; }
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}

			const TArray<FTabDef> T = Tabs(*P);
			if (bP)
			{
				if (DrawPortrait(D, *P, *Def, Win, T)) { bClose = true; }
			}
			else
			{
				DrawLandscape(D, *P, *Def, Win, T);
			}

			if (bClose && Game)
			{
				UFVGame* G = Game;
				G->CloseScreen();   // the screen may be destroyed here
				return;
			}
			RunIntent(*P);         // last statement: it may open another screen
		}

	private:
		static constexpr int32 NumTabKinds = int32(ETab::Upgrade) + 1;

		EFVStation Station = EFVStation::Kitchen;
		int32 TickCount = 0;
		FIntent Intent;
		TUniquePtr<FFVLureWorkshop> Lures;       // workshop only: the lure workshop panel of the «Блёсны» tab
		bool bLuresReady = false;                // the panel picked its default recipe (on the first unlocked draw)
		FScrollColumn LureLockCol;               // the locked «Блёсны» page
		FFVPager Pager;                          // Pager.Page = index into Tabs(): every tab is a sliding page
		// recipe tabs: selection, quantity, list + detail scrolling per tab (neighbouring pages are drawn together)
		FName SelRecipes[NumTabKinds];
		int32 Qtys[NumTabKinds] = {};
		uint8 EnsureSel[NumTabKinds] = {};       // 1 = scroll the selected row into view (animated), 2 = instantly
		FFVScrollView RecipeLists[NumTabKinds];
		FScrollColumn RecipeDetails[NumTabKinds];
		// other smooth lists / grids / scrolling text columns
		FFVScrollView RodList;                   // landscape rods list (rigging)
		FFVScrollView CandList;                  // rigging candidates
		FFVScrollView AquaList;                  // fish in the aquarium
		FFVScrollView CellarGrid;                // ice cellar preview grid
		FFVScrollView PickGrid;                  // keepnet picker grid
		FScrollColumn RigStatsCol, UpgradeCol, BuildCol, ShovelCol, WormsCol, RadioCol, ToolsCol;
		FScrollColumn NextCol, LureNextCol, InfoCol;   // landscape «Следующий уровень» panels, bed / chest info
		// rigging
		FName SelRod;
		int32 TackleSlot = 0;       // 0 line, 1 float, 2 bait
		FName CandId;
		bool bCandNone = false;     // "без наживки" candidate selected
		bool bHasCand = false;
		// picker / aquarium / radio
		EPicker Picker = EPicker::None;
		FStackKey PickKey;
		int32 AquaSel = 0;
		int32 RadioLine = 0;
		// radio forecast column: its height at the normal spacing and what the tight spacing saves (last frame)
		float RadioNormalH = 0.f;
		float RadioTightSave = 0.f;

		// ---------------------------------------------------------------------------------------- data
		const FFVStationDef* GetDef() const
		{
			const TArray<FFVStationDef>& All = FVDB::Stations();
			const int32 I = int32(Station);
			return All.IsValidIndex(I) ? &All[I] : nullptr;
		}

		bool IsInfoOnly() const { return Station == EFVStation::Bed || Station == EFVStation::Chest; }

		/** Name of a station level ("Плита"); empty when the level has no own name (Коптильня 1/2...). */
		FString LevelName(int32 Lv) const
		{
			const FFVStationDef* Def = GetDef();
			if (Def && Def->Levels.IsValidIndex(Lv - 1)) { return Def->Levels[Lv - 1].Name; }
			return FString();
		}

		TArray<FTabDef> Tabs(const UFVSaveGame& P) const
		{
			TArray<FTabDef> T;
			auto Add = [&T](ETab Tab, const TCHAR* Label) { FTabDef D; D.Tab = Tab; D.Label = Label; T.Add(D); };
			if (IsInfoOnly())
			{
				Add(ETab::Info, TEXT("Сведения"));
				return T;
			}
			if (!P.IsStationBuilt(Station))
			{
				Add(ETab::Build, TEXT("Постройка"));
				return T;
			}
			switch (Station)
			{
			case EFVStation::Kitchen: Add(ETab::Recipes, TEXT("Рецепты")); break;
			case EFVStation::Workshop:
				Add(ETab::Materials, TEXT("Материалы"));
				Add(ETab::Tackle, TEXT("Снасти"));
				Add(ETab::Nets, TEXT("Сети"));
				Add(ETab::Recycle, TEXT("Переработка"));
				Add(ETab::Rigging, TEXT("Оснастка"));
				Add(ETab::Shovel, TEXT("Лопата"));
				Add(ETab::Lures, TEXT("Блёсны"));
				// update 6 (§9б): the pickaxe by the blueprint (Мастерская 4) and the next axes
				if (P.GetLevel() >= 11 || P.U6.AxeLevel > 0 || P.HasBlueprint(FName(TEXT("bp_pickaxe")))) { Add(ETab::Tools, TEXT("Инструмент")); }
				break;
			case EFVStation::Smokehouse: Add(ETab::Smoke, TEXT("Копчение")); break;
			case EFVStation::WormFarm: Add(ETab::Worms, TEXT("Червятник")); break;
			case EFVStation::Aquarium: Add(ETab::Aquarium, TEXT("Аквариум")); break;
			case EFVStation::IceCellar: Add(ETab::Cellar, TEXT("Хранилище")); break;
			case EFVStation::Radio: Add(ETab::Radio, TEXT("Прогноз")); break;
			case EFVStation::TrophyWall: Add(ETab::Trophies, TEXT("Трофеи")); break;
			default: break;
			}
			Add(ETab::Upgrade, TEXT("Улучшение"));
			return T;
		}

		/** Remembers the tab for this session (coming back from a mini-game lands on the same tab). */
		void RememberTab(int32 I)
		{
			const int32 SI = int32(Station);
			if (SI >= 0 && SI < int32(EFVStation::Count)) { GLastTab[SI] = I; }
		}

		/** The tab that lists a recipe of this station. */
		ETab TabOfRecipe(const FFVRecipeDef& R) const
		{
			return Station == EFVStation::Workshop ? WorkshopTabOf(R) : ETab::Recipes;
		}

		void SelectRecipe(ETab Tab, FName Id)
		{
			const int32 TI = FMath::Clamp(int32(Tab), 0, NumTabKinds - 1);
			if (SelRecipes[TI] != Id) { RecipeDetails[TI].View.ScrollTo(0.f, true); }
			SelRecipes[TI] = Id;
			Qtys[TI] = 1;
			const int32 SI = int32(Station);
			if (SI >= 0 && SI < int32(EFVStation::Count)) { GLastRecipe[SI] = Id; }
		}

		TArray<const FFVRecipeDef*> RecipesOf(ETab Tab) const
		{
			TArray<const FFVRecipeDef*> Out;
			for (const FFVRecipeDef* R : FVDB::RecipesFor(Station))
			{
				if (!R) { continue; }
				if (Station != EFVStation::Workshop || WorkshopTabOf(*R) == Tab) { Out.Add(R); }
			}
			return Out;
		}

		void Want(EAct A, FName Id = NAME_None, int32 Index = -1, int32 Count = 1, FName Id2 = NAME_None, float Value = 0.f)
		{
			if (Intent.Act != EAct::None) { return; }
			Intent.Act = A;
			Intent.Id = Id;
			Intent.Id2 = Id2;
			Intent.Index = Index;
			Intent.Count = Count;
			Intent.Value = Value;
		}

		void OpenPicker(EPicker K)
		{
			Picker = K;
			PickKey.Clear();
			PickGrid.Reset();
		}
		void ClosePicker()
		{
			Picker = EPicker::None;
			PickKey.Clear();
		}

		float BodyS() const { return FV::IsPortrait() ? FV::Ui::Body() : 7.5f; }
		float SmallS() const { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }

		// ---------------------------------------------------------------------------------------- frame layouts
		void DrawLandscape(FVDraw& D, UFVSaveGame& P, const FFVStationDef& Def, const FFVRect& Win, const TArray<FTabDef>& T)
		{
			const FFVRect Header(Win.X + 12.f, Win.Y + 10.f, Win.W - 24.f, 34.f);
			DrawHeader(D, P, Def, Header);
			float PanelY = Header.Bottom() + 8.f;
			if (Picker != EPicker::None)
			{
				DrawPicker(D, P, FFVRect(Win.X + 10.f, PanelY, Win.W - 20.f, Win.Bottom() - 10.f - PanelY), Win);
				return;
			}
			if (T.Num() > 1)
			{
				PanelY += 16.f;
				StationTabs(D, T, Win.X + 10.f, PanelY, Win.W - 20.f);
			}
			DrawPages(D, P, T, FFVRect(Win.X + 10.f, PanelY, Win.W - 20.f, Win.Bottom() - 10.f - PanelY));
		}

		/** Returns true when "← Назад" closes the screen. */
		bool DrawPortrait(FVDraw& D, UFVSaveGame& P, const FFVStationDef& Def, const FFVRect& Win, const TArray<FTabDef>& T)
		{
			const float X = Win.X + 10.f;
			const float W = Win.W - 20.f;
			const FFVRect Header(X, Win.Y + 16.f, W, 42.f);
			DrawHeader(D, P, Def, Header);
			float Y = Header.Bottom() + 8.f;
			const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;
			if (Picker != EPicker::None)
			{
				DrawPicker(D, P, FFVRect(X, Y, W, Bottom - Y), Win);
				return false;   // the picker's "Отмена" only leaves the picker
			}
			if (T.Num() > 1)
			{
				Y += StationTabs(D, T, X, Y, W);
			}
			const FFVRect Area(X, Y, W, FMath::Max(40.f, Bottom - Y));
			DrawPages(D, P, T, Area);
			// fixed footer: "← Назад" + the action(s) of the page the pager is heading to
			FFVRect Act;
			const bool bBack = D.ActionBar(Win, &Act);
			if (T.IsValidIndex(Pager.Page)) { DrawTab(D, P, T[Pager.Page].Tab, Area, &Act); }
			return bBack;
		}

		/** The tabs as sliding pages: a swipe over the content, a tab click or Left/Right slide them (interruptible). */
		void DrawPages(FVDraw& D, UFVSaveGame& P, const TArray<FTabDef>& T, const FFVRect& Area)
		{
			Pager.Update(D, Area, T.Num());
			if (Pager.PageChangedThisFrame()) { RememberTab(Pager.Page); }
			Pager.DrawPages(D, Area, [&](int32 Page)
			{
				if (T.IsValidIndex(Page)) { DrawTab(D, P, T[Page].Tab, Area, nullptr); }
			});
		}

		/** One tab. Foot == nullptr: the page content in Area (local coordinates of the page at rest).
		    Foot != nullptr (portrait): only the primary action(s) of the tab, in the fixed footer rect *Foot. */
		void DrawTab(FVDraw& D, UFVSaveGame& P, ETab Tab, const FFVRect& Area, const FFVRect* Foot)
		{
			switch (Tab)
			{
			case ETab::Build: TabBuild(D, P, Area, Foot); return;
			case ETab::Upgrade: TabUpgrade(D, P, Area, Foot); return;
			case ETab::Recipes:
			case ETab::Materials:
			case ETab::Tackle:
			case ETab::Nets:
			case ETab::Recycle: TabRecipes(D, P, Tab, Area, Foot); return;
			case ETab::Rigging: TabRigging(D, P, Area, Foot); return;
			case ETab::Shovel: TabShovel(D, P, Area, Foot); return;
			case ETab::Smoke: TabSmoke(D, P, Area, Foot); return;
			case ETab::Worms: TabWorms(D, P, Area, Foot); return;
			case ETab::Aquarium: TabAquarium(D, P, Area, Foot); return;
			case ETab::Cellar: TabCellar(D, P, Area, Foot); return;
			case ETab::Radio: TabRadio(D, P, Area, Foot); return;
			case ETab::Trophies: TabTrophies(D, P, Area, Foot); return;
			case ETab::Lures: TabLures(D, P, Area, Foot); return;
			case ETab::Tools: TabTools(D, P, Area, Foot); return;
			case ETab::Info:
			default: TabInfo(D, P, Area, Foot); return;
			}
		}

		// ---------------------------------------------------------------------------------------- tab row (update 4)
		/** The «Блёсны» page is the one the pager shows / heads to. */
		bool IsLurePageActive() const
		{
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P || !Lures) { return false; }
			const TArray<FTabDef> T = Tabs(*P);
			return T.IsValidIndex(Pager.Page) && T[Pager.Page].Tab == ETab::Lures;
		}

		/** PagerTabs (FVScreenHome.h) with the «Блёсны» tab in its own teal-violet look. Landscape: folder tabs standing
		    on the panel whose top edge is Y. Portrait: big folder tabs (<= 4) or balanced rows of chip buttons.
		    Returns the height used in portrait (content starts at Y + result), 0 in landscape. */
		float StationTabs(FVDraw& D, const TArray<FTabDef>& T, float X, float Y, float W)
		{
			const int32 N = T.Num();
			if (N <= 0) { return 0.f; }
			TArray<FFVRect> Cells;
			int32 Hit = -1;
			float OutH = 0.f;
			bool bTop = true;
			if (!FV::IsPortrait())
			{
				const float Gap = 2.f;
				const float TabW = FMath::Min(104.f, FMath::FloorToFloat((W - 8.f - Gap * float(N - 1)) / float(N)));
				for (int32 i = 0; i < N; ++i)
				{
					const FFVRect R(X + 4.f + float(i) * (TabW + Gap), Y - 15.f, TabW, 16.f);
					Cells.Add(R);
					const bool bActive = i == Pager.Page;
					if (T[i].Tab == ETab::Lures)
					{
						if (LureTab(D, R, T[i].Label, bActive, false)) { Hit = i; }
						if (!FV::IsTouchUI() && D.Hover(R)) { LureTabTooltip(D); }
					}
					else if (D.Tab(R, T[i].Label, bActive)) { Hit = i; }
				}
			}
			else if (N <= 4)
			{
				const float TabH = 30.f, Gap = 3.f;
				const float TabW = FMath::FloorToFloat((W - 8.f - Gap * float(N - 1)) / float(N));
				for (int32 i = 0; i < N; ++i)
				{
					const FFVRect R(X + 4.f + float(i) * (TabW + Gap), Y, TabW, TabH);
					Cells.Add(R);
					const bool bActive = i == Pager.Page;
					if (T[i].Tab == ETab::Lures ? LureTab(D, R, T[i].Label, bActive, false) : BigTab(D, R, T[i].Label, bActive, FV::Ui::Body())) { Hit = i; }
				}
				OutH = TabH;
			}
			else
			{
				bTop = false;
				const int32 Rows = FMath::DivideAndRoundUp(N, 4);
				const int32 PerRow = FMath::DivideAndRoundUp(N, Rows);
				const float ChipH = 30.f, Gap = 5.f;
				const float CellW = FMath::FloorToFloat((W - Gap * float(PerRow - 1)) / float(PerRow));
				for (int32 i = 0; i < N; ++i)
				{
					const int32 Row = i / PerRow;
					const int32 Col = i % PerRow;
					const int32 InRow = FMath::Min(PerRow, N - Row * PerRow);
					const float RowW = float(InRow) * CellW + float(InRow - 1) * Gap;
					const float RX = FMath::RoundToFloat(X + (W - RowW) * 0.5f + float(Col) * (CellW + Gap));
					const FFVRect R(RX, Y + float(Row) * (ChipH + Gap), CellW, ChipH);
					Cells.Add(R);
					const bool bActive = i == Pager.Page;
					if (T[i].Tab == ETab::Lures) { if (LureTab(D, R, T[i].Label, bActive, true)) { Hit = i; } }
					else if (D.Button(R, T[i].Label, bActive ? EFVButton::Primary : EFVButton::Normal, true, FV::Ui::Body())) { Hit = i; }
				}
				OutH = float(Rows) * ChipH + float(Rows - 1) * Gap + 8.f;
			}
			if (Hit >= 0 && Hit != Pager.Page) { Pager.GoTo(Hit); }
			PagerMarker(D, Cells, Pager, bTop);
			return OutH;
		}

		bool LuresOpen(const UFVSaveGame& P) const { return P.GetStationLevel(EFVStation::Workshop) >= LureWorkshopLevel; }

		void LureTabTooltip(FVDraw& D) const
		{
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			const bool bOpen = P && LuresOpen(*P);
			D.Tooltip(TEXT("Мастерская блёсен"), bOpen ? FString(TEXT("Блёсны и мушки из основы, краски и блёсток — 24 рецепта."))
				: FString::Printf(TEXT("Откроется с верстаком — мастерской %d-го уровня."), LureWorkshopLevel), LureTeal());
		}

		/** The «Блёсны» tab: deep teal -> violet with a twinkling sparkle. bChip: a portrait chip button, otherwise a
		    folder tab (the active one opens into the page like FVDraw::Tab). Returns the click (fires on release). */
		bool LureTab(FVDraw& D, const FFVRect& R, const FString& Label, bool bActive, bool bChip)
		{
			const bool bP = FV::IsPortrait();
			const bool bHover = D.Hover(R);
			const float Tm = float(FMath::Fmod(D.Time, 10000.0));
			FFVRect TextR = R;
			FLinearColor TextC = LureCream();
			bool bShadow = true;
			FVector2D SparkC;
			FLinearColor SparkCol = FV::Col(255, 244, 190);
			bool bIcon = false;
			if (bChip)
			{
				const bool bDown = bHover && D.Input().bLmbDown;
				const float DY = bDown ? 1.f : 0.f;
				if (!bDown) { D.Rect(FFVRect(R.X + 1.f, R.Bottom(), R.W - 1.f, 1.f), FV::Col(0, 0, 0, 60)); }
				D.Rect(R, LureEdge());
				const FFVRect F = R.Inset(1.f);
				D.Rect(F, FMath::Lerp(LureEdge(), LureViolet(), 0.45f));
				const FFVRect Face(F.X, F.Y + DY, F.W, bDown ? F.H - 1.f : F.H - 2.f);
				LureGradientH(D, Face);
				if (bActive || bHover) { D.Rect(Face, FLinearColor(1.f, 1.f, 1.f, bActive ? 0.16f : 0.1f)); }
				if (!bDown) { D.Rect(FFVRect(F.X, F.Y, F.W, 1.f), LureGlint()); }
				if (bActive) { D.Frame(R.Inset(-1.f), FVPal::Gold(), 1.f); }
				TextR = Face;
				SparkC = FVector2D(Face.Right() - 6.f, Face.Y + 6.f);
				bIcon = true;
			}
			else if (bActive)
			{
				D.Rect(FFVRect(R.X, R.Y, R.W, R.H + 2.f), LureEdge());
				D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, R.H + 2.f), FVPal::Parchment());
				LureGradientH(D, FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 4.f));   // peeks out around the gold marker
				TextR = FFVRect(R.X, R.Y + 4.f, R.W, R.H - 4.f);
				TextC = FVPal::Ink();
				SparkC = FVector2D(R.Right() - 6.f, R.Y + 8.f);
				SparkCol = LureViolet();
			}
			else
			{
				const FFVRect Tb(R.X, R.Y + 2.f, R.W, R.H - 2.f);
				D.Rect(Tb, LureEdge());
				LureGradientH(D, Tb.Inset(1.f));
				if (bHover) { D.Rect(Tb.Inset(1.f), FLinearColor(1.f, 1.f, 1.f, 0.12f)); }
				D.Rect(FFVRect(Tb.X + 1.f, Tb.Y + 1.f, Tb.W - 2.f, 1.f), LureGlint());
				TextR = Tb;
				SparkC = FVector2D(Tb.Right() - 5.f, Tb.Y + 5.f);
			}
			// label (+ a little lure on the portrait chips), shrinks to fit like the other tabs
			const float SparkRoom = 8.f;
			float S = (bP && R.H >= 22.f) ? FV::Ui::Body() : 8.f;
			FVector2D Sz = D.MeasureText(Label, S, true);
			const float IconW = 16.f, IconGap = 2.f;
			if (bIcon && Sz.X + IconW + IconGap > TextR.W - SparkRoom - 4.f) { bIcon = false; }
			const float Avail = TextR.W - SparkRoom - 4.f - (bIcon ? IconW + IconGap : 0.f);
			while (Sz.X > Avail && S > 5.5f)
			{
				S -= 0.5f;
				Sz = D.MeasureText(Label, S, true);
			}
			const float TotalW = Sz.X + (bIcon ? IconW + IconGap : 0.f);
			const float X0 = FMath::RoundToFloat(TextR.X + (TextR.W - SparkRoom * 0.5f - TotalW) * 0.5f);
			const float TY = FMath::RoundToFloat(TextR.Y + (TextR.H - Sz.Y) * 0.5f);
			if (bIcon)
			{
				if (UFVSprites* Sp = UFVSprites::Get())
				{
					const float Bob = FMath::RoundToFloat(FMath::Sin(Tm * 2.4f) * 1.f);
					D.SpriteCentered(Sp->LureIcon(0, 2, 2), FVector2D(X0 + IconW * 0.5f, TextR.Center().Y + Bob), 1.f, FLinearColor::White, false, FMath::Sin(Tm * 1.7f) * 10.f);
				}
			}
			D.Text(Label, FVector2D(X0 + (bIcon ? IconW + IconGap : 0.f), TY), S, TextC, EFVAlign::Left, bShadow, true);
			Sparkle(D, SparkC, Tm, 0.f, bP ? 3.f : 2.f, SparkCol);
			if (bChip) { Sparkle(D, FVector2D(R.X + 5.f, R.Bottom() - 7.f), Tm, 2.2f, 1.5f, SparkCol); }
			if (D.Clicked(R))
			{
				if (!bActive) { FVUI::Sfx(EFVSfx::Click); }
				return true;
			}
			return false;
		}

		// ---------------------------------------------------------------------------------------- lure workshop tab
		void TabLures(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bOpen = LuresOpen(P) && Lures.IsValid();
			if (Foot)
			{
				if (bOpen) { Lures->DrawAssembleButton(D, *Foot, !D.IsInputBlocked(), FV::Ui::Body() + 1.f); }
				else { DrawUpgradeButton(D, P, *Foot, false); }
				return;
			}
			// the page's own colour: a faint teal-violet wash (the panel keeps the parchment look on top)
			LureGradientH(D, Area, 0.08f);
			if (bOpen)
			{
				if (!bLuresReady)
				{
					Lures->Open();     // preselects an affordable, preferably undiscovered recipe
					bLuresReady = true;
				}
				Lures->Draw(D, Area, !D.IsInputBlocked(), !FV::IsPortrait());
				return;
			}
			TabLuresLocked(D, P, Area);
		}

		/** Below workshop level 2: what the lure workshop is + the верстак upgrade (the «Улучшить» button is right here). */
		void TabLuresLocked(FVDraw& D, UFVSaveGame& P, const FFVRect& Area)
		{
			const FFVStationDef* Def = GetDef();
			const int32 Lv = P.GetStationLevel(Station);
			const FFVStationLevel* Next = (Def && Def->Levels.IsValidIndex(Lv)) ? &Def->Levels[Lv] : nullptr;
			const FString Title = TEXT("Мастерская блёсен");
			const FString About = TEXT("Здесь собирают блёсны и мушки: основа, краска и блёстки дают 24 рецепта, и у каждой приманки свои любимые рыбы.");
			const FString Need = FString::Printf(TEXT("Откроется с верстаком — мастерской %d-го уровня."), LureWorkshopLevel);
			if (FV::IsPortrait())
			{
				D.Panel(Area, EFVPanel::Parchment);
				float Y = LureLockCol.Begin(D, Area.Inset(2.f), 6.f);
				const float X = Area.X + 8.f, W = Area.W - 16.f - LureLockCol.BarRoom();
				DrawLureShowcase(D, FFVRect(X, Y, W, 104.f), true);
				Y += 110.f;
				D.Text(Title, FVector2D(X, Y), FV::Ui::Header(), FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(FV::Ui::Header()) + 2.f;
				Y += D.TextWrapped(About, FVector2D(X, Y), W, BodyS(), FVPal::Ink()) + 4.f;
				Y += D.TextWrapped(Need, FVector2D(X, Y), W, BodyS(), InkWarn(), true) + 6.f;
				D.Divider(FVector2D(X, Y), W);
				Y += 8.f;
				Y = DrawNextLevelBlock(D, P, Next, Lv, X, Y, W, NoLimit);
				LureLockCol.End(D, Y, 6.f);
				return;
			}
			const FFVRect Left(Area.X, Area.Y, 300.f, Area.H);
			const FFVRect Right(Area.X + 306.f, Area.Y, Area.W - 306.f, Area.H);
			D.Panel(Left, EFVPanel::Parchment);
			D.Panel(Right, EFVPanel::Parchment);
			{
				const float X = Left.X + 8.f, W = Left.W - 16.f;
				DrawLureShowcase(D, FFVRect(X, Left.Y + 8.f, W, 104.f), true);
				float Y = Left.Y + 118.f;
				D.Text(Title, FVector2D(X, Y), 10.f, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(10.f) + 2.f;
				Y += TextWrappedLimited(D, About, FVector2D(X, Y), W, SmallS(), FVPal::Ink(), 4) + 4.f;
				TextWrappedLimited(D, Need, FVector2D(X, Y), W, SmallS(), InkWarn(), 2, true);
			}
			DrawNextLevelPanel(D, P, Next, Lv, Right, LureNextCol);
		}

		/** A little tank of teal water sinking to violet with three lures swaying on their lines (+ a lock badge). */
		void DrawLureShowcase(FVDraw& D, const FFVRect& R, bool bLocked)
		{
			const float Tm = float(FMath::Fmod(D.Time, 10000.0));
			D.Rect(R.Offset(2.f, 2.f), FV::Col(40, 20, 50, 60));
			D.Rect(R.Inset(-1.f), LureEdge());
			D.GradientV(R, FV::Col(78, 164, 176), FV::Col(52, 48, 118), 9);
			for (int32 k = 0; k < 3; ++k)   // light ripples near the surface
			{
				const float WY = R.Y + 6.f + float(k) * 9.f + FMath::Sin(Tm * 1.4f + float(k)) * 1.5f;
				D.Rect(FFVRect(FMath::RoundToFloat(R.X + 10.f + float(k) * 23.f), FMath::RoundToFloat(WY), FMath::RoundToFloat(R.W * 0.22f), 1.f), FV::Col(190, 236, 240, 90));
			}
			for (int32 b = 0; b < 5; ++b)   // bubbles
			{
				const float Ph = FMath::Frac(Tm * 0.22f + float(b) * 0.23f);
				const FVector2D BP(R.X + 12.f + float((b * 53) % int32(FMath::Max(24.f, R.W - 24.f))) + FMath::Sin(Ph * 11.f) * 2.f, R.Bottom() - 4.f - Ph * (R.H - 8.f));
				D.Circle(BP, 1.2f + float(b % 2) * 0.6f, FV::Col(220, 246, 255, uint8(150.f * (1.f - Ph))));
			}
			if (UFVSprites* Sp = UFVSprites::Get())
			{
				static const uint8 Looks[3][3] = { { 0, 1, 2 }, { 1, 3, 1 }, { 0, 2, 0 } };
				for (int32 i = 0; i < 3; ++i)
				{
					const float AX = R.X + R.W * (0.25f + 0.25f * float(i));
					const float Len = R.H * (0.3f + 0.13f * float((i + 1) % 3));
					const float Sw = FMath::Sin(Tm * 1.3f + float(i) * 2.1f) * 4.f;
					const FVector2D Tip(AX + Sw, R.Y + Len);
					D.Line(FVector2D(AX, R.Y), Tip, FV::Col(236, 236, 236, 150), 1.f);
					const FFVSprite& LS = Sp->LureIcon(Looks[i][0], Looks[i][1], Looks[i][2]);
					const float Sc = FVUI::FitScale(LS, R.H * 0.36f, R.H * 0.36f, 3.f);
					D.SpriteCentered(LS, Tip + FVector2D(0.f, FMath::RoundToFloat(LS.H * Sc * 0.42f)), Sc, FLinearColor::White, false, Sw * 2.f);
				}
			}
			Sparkle(D, FVector2D(R.X + R.W * 0.14f, R.Y + R.H * 0.62f), Tm, 0.f, 3.f, FV::Col(255, 244, 190));
			Sparkle(D, FVector2D(R.X + R.W * 0.88f, R.Y + R.H * 0.3f), Tm, 2.3f, 2.f, LureGlint());
			D.Frame(R, LureEdge());
			if (bLocked)
			{
				const FVector2D C(R.Right() - 13.f, R.Y + 13.f);
				D.Circle(C, 10.f, LureEdge());
				D.Circle(C, 9.f, FVPal::Parchment());
				if (const FFVSprite* L = FVUI::Icon(EFVIcon::Lock)) { D.SpriteCentered(*L, C, 1.f); }
			}
		}

		// ---------------------------------------------------------------------------------------- header
		void DrawHeader(FVDraw& D, UFVSaveGame& P, const FFVStationDef& Def, const FFVRect& R)
		{
			const bool bP = FV::IsPortrait();
			const int32 Lv = P.GetStationLevel(Station);
			const int32 Max = FMath::Max(1, FVDB::StationMaxLevel(Station));
			const FFVRect Box(R.X, R.Y, R.H, R.H);
			IconBox(D, Box);
			if (UFVSprites* Sp = UFVSprites::Get())
			{
				const FFVSprite S = Sp->StationSprite(Station, IsInfoOnly() ? FMath::Max(1, Lv) : Lv);
				FVUI::SpriteFit(D, S, Box.Inset(2.f), FLinearColor::White, 2.f);
			}
			const float TX = Box.Right() + 8.f;
			const float NameS = bP ? FV::Ui::Header() : 11.f;
			const float LineS = SmallS();

			// right: purse (update 4: the lure workshop is a tab now, not a header button)
			const float MoneyS = bP ? FV::Ui::Body() : 9.f;
			const float MW = D.MoneyLabel(FVector2D(R.Right(), R.Y + 1.f), P.Money, MoneyS, EFVAlign::Right);
			const float RightLimit = R.Right() - MW - 10.f;

			const FString Name = D.Ellipsize(Def.Name, FMath::Max(40.f, RightLimit - TX), NameS, true);
			D.Text(Name, FVector2D(TX, R.Y - 1.f), NameS, FVPal::Ink(), EFVAlign::Left, true, true);
			const float LineY = R.Y + D.LineHeight(NameS) + (bP ? 1.f : 0.f);
			FString LevelText;
			FLinearColor LevelC = FVPal::InkSoft();
			if (IsInfoOnly()) { LevelText = TEXT("Есть в доме с самого начала"); }
			else if (Lv <= 0) { LevelText = TEXT("Не построено"); LevelC = InkWarn(); }
			else if (Max <= 1) { LevelText = TEXT("Построено"); LevelC = InkGood(); }
			else
			{
				const FString LvName = LevelName(Lv);
				LevelText = LvName.IsEmpty() ? FString::Printf(TEXT("Уровень %d из %d"), Lv, Max) : FString::Printf(TEXT("%s · уровень %d из %d"), *LvName, Lv, Max);
				if (Lv >= Max) { LevelText += TEXT(" · максимум"); LevelC = InkGood(); }
			}
			const float LineW = FMath::Max(40.f, R.Right() - TX);
			D.Text(D.Ellipsize(LevelText, LineW, LineS, false), FVector2D(TX, LineY), LineS, LevelC, EFVAlign::Left, false, false);
			if (!IsInfoOnly() && Max > 1)
			{
				const float PipS = bP ? 8.f : 6.f;
				LevelPips(D, FVector2D(TX, LineY + D.LineHeight(LineS, false) + 2.f), Lv, Max, PipS);
			}
		}

		// ---------------------------------------------------------------------------------------- build / upgrade
		/** "Нужно" block for a station level: coins, materials, fishing level. Returns Y below it. */
		float DrawLevelRequirements(FVDraw& D, UFVSaveGame& P, const FFVStationLevel& L, float X, float Y, float W, float MaxY)
		{
			const float S = BodyS();
			const float RH = ReqRowH(D, S) + (FV::IsPortrait() ? 1.f : 0.f);
			D.Text(TEXT("Нужно:"), FVector2D(X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, true);
			Y += D.LineHeight(SmallS()) + 1.f;
			if (L.Price > 0 && Y + RH <= MaxY)
			{
				CoinRow(D, X, Y, W, P.Money, L.Price, S);
				Y += RH;
			}
			for (const FFVIngredient& M : L.Materials)
			{
				if (Y + RH > MaxY) { break; }
				ReqRow(D, X, Y, W, M.ItemId, P.CountIngredient(M.ItemId), M.Count, S);
				Y += RH;
			}
			if (L.MinLevel > 1 && Y + RH <= MaxY)
			{
				CondRow(D, X, Y, W, FString::Printf(TEXT("Уровень рыбалки %d"), L.MinLevel), P.GetLevel() >= L.MinLevel, S, EFVIcon::Level);
				Y += RH;
			}
			return Y;
		}

		/** The build / upgrade button (reason label when not possible). */
		void DrawUpgradeButton(FVDraw& D, UFVSaveGame& P, const FFVRect& R, bool bBuild)
		{
			const int32 Lv = P.GetStationLevel(Station);
			const FFVStationDef* Def = GetDef();
			if (!Def) { return; }
			const EFVBuyResult St = P.CanUpgradeStation(Station);
			const float TS = FV::IsPortrait() ? FV::Ui::Body() + 0.5f : 8.f;
			if (St == EFVBuyResult::MaxLevel || !Def->Levels.IsValidIndex(Lv))
			{
				D.Button(R, TEXT("Максимальный уровень"), EFVButton::Normal, false, TS);
				return;
			}
			const FFVStationLevel& L = Def->Levels[Lv];
			if (St == EFVBuyResult::Ok)
			{
				if (CoinButton(D, R, bBuild ? TEXT("Построить") : TEXT("Улучшить"), L.Price > 0 ? L.Price : -1, EFVButton::Primary, true, true, TS))
				{
					Want(EAct::Upgrade);
				}
				return;
			}
			FString Why;
			switch (St)
			{
			case EFVBuyResult::LevelTooLow: Why = FString::Printf(TEXT("Нужен уровень %d"), L.MinLevel); break;
			case EFVBuyResult::NotEnoughMoney: Why = TEXT("Не хватает монет"); break;
			case EFVBuyResult::NotEnoughMaterials: Why = TEXT("Не хватает материалов"); break;
			default: Why = ResultText(St); break;
			}
			D.Button(R, Why, EFVButton::Normal, false, TS);
		}

		/** Level ladder: one row per level (built = green tick, next = gold frame). Returns Y below it. */
		float DrawLevelTrack(FVDraw& D, UFVSaveGame& P, const FFVStationDef& Def, float X, float Y, float W, float MaxY)
		{
			const bool bP = FV::IsPortrait();
			const int32 Lv = P.GetStationLevel(Station);
			const float NameS = bP ? FV::Ui::Body() : 7.5f;
			const float DescS = SmallS();
			for (int32 i = 0; i < Def.Levels.Num(); ++i)
			{
				const FFVStationLevel& L = Def.Levels[i];
				const bool bHave = i < Lv;
				const bool bNext = i == Lv;
				FString Desc = L.Description;
				if (L.Capacity > 0 && Station != EFVStation::Chest)
				{
					Desc = FString::Printf(TEXT("%s%sВместимость: %d"), *Desc, Desc.IsEmpty() ? TEXT("") : TEXT(" · "), L.Capacity);
				}
				const float DescW = W - 26.f;
				// up to two lines: the classic row height; a longer description grows the row (no line cap)
				const float DescLH = D.LineHeight(DescS, false);
				const float FullH = WrappedLimitedH(D, Desc, DescW, DescS);
				const float DescH = FullH > FMath::RoundToFloat(DescLH * 0.95f) * 2.f + 0.5f ? FullH + DescLH * 0.15f
					: FMath::Min(D.MeasureWrapped(Desc, DescW, DescS), DescLH * 2.1f);
				const float RowH = FMath::RoundToFloat(D.LineHeight(NameS) + DescH + 6.f);
				if (Y + RowH > MaxY) { break; }
				const FFVRect Row(X, Y, W, RowH);
				D.Rect(Row, bNext ? FV::Col(255, 244, 214) : (bHave ? FV::Col(226, 240, 196) : FV::Col(240, 214, 170)));
				D.Frame(Row, bNext ? FVPal::Gold() : BoxEdge(), bNext ? 2.f : 1.f);
				// level badge
				const FVector2D C(Row.X + 11.f, Row.Y + 11.f);
				D.Circle(C, 8.f, bHave ? FV::Col(52, 110, 30) : FV::Col(150, 110, 70));
				D.Circle(C, 7.f, bHave ? FVPal::Green() : FV::Col(230, 196, 146));
				if (bHave) { CheckBadge(D, C, 6.f); }
				else { D.Text(FString::FromInt(i + 1), FVector2D(C.X, C.Y - 5.f), 7.f, FVPal::Ink(), EFVAlign::Center, false, true); }
				FString Title = L.Name.IsEmpty() ? FString::Printf(TEXT("Уровень %d"), i + 1) : FString::Printf(TEXT("%d. %s"), i + 1, *L.Name);
				if (bNext) { Title += Lv == 0 ? TEXT(" — постройка") : TEXT(" — следующий"); }
				D.Text(D.Ellipsize(Title, W - 26.f, NameS), FVector2D(Row.X + 22.f, Row.Y + 2.f), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
				TextWrappedLimited(D, Desc, FVector2D(Row.X + 22.f, Row.Y + 3.f + D.LineHeight(NameS)), DescW, DescS, FVPal::InkSoft(), 40);
				Y += RowH + 3.f;
			}
			return Y;
		}

		void TabBuild(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const FFVStationDef* Def = GetDef();
			if (!Def || !Def->Levels.IsValidIndex(0)) { return; }
			if (Foot)
			{
				DrawUpgradeButton(D, P, *Foot, true);
				return;
			}
			const FFVStationLevel& L0 = Def->Levels[0];
			const bool bP = FV::IsPortrait();
			if (bP)
			{
				// one smoothly scrolling column: preview, description, what it gives, requirements
				D.Panel(Area, EFVPanel::Parchment);
				float Y = BuildCol.Begin(D, Area.Inset(2.f), 6.f);
				const float X = Area.X + 8.f, W = Area.W - 16.f - BuildCol.BarRoom();
				const float PrevH = FMath::Clamp(FMath::RoundToFloat(Area.H * 0.24f), 70.f, 110.f);
				DrawBuildPreview(D, FFVRect(X, Y, W, PrevH));
				Y += PrevH + 6.f;
				const FString Title = L0.Name.IsEmpty() ? Def->Name : FString::Printf(TEXT("%s: %s"), *Def->Name, *L0.Name);
				D.Text(D.Ellipsize(Title, W, FV::Ui::Body() + 1.f), FVector2D(X, Y), FV::Ui::Body() + 1.f, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(FV::Ui::Body() + 1.f) + 1.f;
				Y += TextWrappedLimited(D, Def->Description, FVector2D(X, Y), W, SmallS(), FVPal::Ink(), 12) + 2.f;
				if (!L0.Description.IsEmpty())
				{
					Y += TextWrappedLimited(D, FString::Printf(TEXT("Даёт: %s"), *L0.Description), FVector2D(X, Y), W, SmallS(), InkInfo(), 8) + 2.f;
				}
				D.Divider(FVector2D(X, Y + 2.f), W);
				Y += 8.f;
				Y = DrawLevelRequirements(D, P, L0, X, Y, W, NoLimit);
				BuildCol.End(D, Y, 6.f);
				return;
			}
			const FFVRect Left(Area.X, Area.Y, 290.f, Area.H);
			const FFVRect Right(Area.X + 296.f, Area.Y, Area.W - 296.f, Area.H);
			D.Panel(Left, EFVPanel::Parchment);
			D.Panel(Right, EFVPanel::Parchment);
			{
				float Y = BuildCol.Begin(D, Left.Inset(2.f), 6.f);
				const float X = Left.X + 8.f, W = Left.W - 16.f - BuildCol.BarRoom();
				DrawBuildPreview(D, FFVRect(X, Y, W, 96.f));
				Y += 102.f;
				const FString Title = L0.Name.IsEmpty() ? Def->Name : FString::Printf(TEXT("%s: %s"), *Def->Name, *L0.Name);
				D.Text(D.Ellipsize(Title, W, 9.f), FVector2D(X, Y), 9.f, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += 13.f;
				Y += TextWrappedLimited(D, Def->Description, FVector2D(X, Y), W, 7.f, FVPal::Ink(), 12) + 3.f;
				if (!L0.Description.IsEmpty())
				{
					Y += TextWrappedLimited(D, FString::Printf(TEXT("Даёт: %s"), *L0.Description), FVector2D(X, Y), W, 7.f, InkInfo(), 8);
				}
				BuildCol.End(D, Y, 6.f);
			}
			{
				const float X = Right.X + 8.f, W = Right.W - 16.f;
				float Y = Right.Y + 8.f;
				D.Text(TEXT("Постройка"), FVector2D(X, Y), 10.f, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += 15.f;
				D.Divider(FVector2D(X, Y), W);
				Y += 6.f;
				DrawLevelRequirements(D, P, L0, X, Y, W, Right.Bottom() - 34.f);
				DrawUpgradeButton(D, P, FFVRect(X, Right.Bottom() - 28.f, W, 20.f), true);
			}
		}

		/** Construction site -> the built station (level 1). */
		void DrawBuildPreview(FVDraw& D, const FFVRect& R)
		{
			IconBox(D, R);
			UFVSprites* Sp = UFVSprites::Get();
			if (!Sp) { return; }
			const float Half = FMath::FloorToFloat((R.W - 24.f) * 0.5f);
			const FFVRect A(R.X + 4.f, R.Y + 4.f, Half, R.H - 8.f);
			const FFVRect B(R.Right() - 4.f - Half, R.Y + 4.f, Half, R.H - 8.f);
			const FFVSprite S0 = Sp->StationSprite(Station, 0);
			FVUI::SpriteFit(D, S0, A, FLinearColor(1.f, 1.f, 1.f, 0.85f), 3.f);
			const FFVSprite S1 = Sp->StationSprite(Station, 1);
			FVUI::SpriteFit(D, S1, B, FLinearColor::White, 3.f);
			Triangle(D, R.Center(), 3, 7, FVPal::Wood());
		}

		void TabUpgrade(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const FFVStationDef* Def = GetDef();
			if (!Def) { return; }
			if (Foot)
			{
				DrawUpgradeButton(D, P, *Foot, false);
				return;
			}
			const int32 Lv = P.GetStationLevel(Station);
			const FFVStationLevel* Next = Def->Levels.IsValidIndex(Lv) ? &Def->Levels[Lv] : nullptr;
			if (FV::IsPortrait())
			{
				// the whole ladder + the next level in one smoothly scrolling column
				D.Panel(Area, EFVPanel::Parchment);
				float Y = UpgradeCol.Begin(D, Area.Inset(2.f), 6.f);
				const float X = Area.X + 8.f, W = Area.W - 16.f - UpgradeCol.BarRoom();
				Y = DrawLevelTrack(D, P, *Def, X, Y, W, NoLimit);
				Y += 2.f;
				D.Divider(FVector2D(X, Y), W);
				Y += 6.f;
				Y = DrawNextLevelBlock(D, P, Next, Lv, X, Y, W, NoLimit);
				UpgradeCol.End(D, Y, 6.f);
				return;
			}
			const FFVRect Left(Area.X, Area.Y, 300.f, Area.H);
			const FFVRect Right(Area.X + 306.f, Area.Y, Area.W - 306.f, Area.H);
			D.Panel(Left, EFVPanel::Parchment);
			D.Panel(Right, EFVPanel::Parchment);
			{
				D.Text(TEXT("Уровни станции"), FVector2D(Left.X + 8.f, Left.Y + 7.f), 9.f, FVPal::Ink(), EFVAlign::Left, false, true);
				const FFVRect ColR(Left.X + 2.f, Left.Y + 21.f, Left.W - 4.f, Left.H - 23.f);
				float Y = UpgradeCol.Begin(D, ColR, 1.f);
				Y = DrawLevelTrack(D, P, *Def, Left.X + 8.f, Y, Left.W - 16.f - UpgradeCol.BarRoom(), NoLimit);
				UpgradeCol.End(D, Y, 3.f);
			}
			DrawNextLevelPanel(D, P, Next, Lv, Right, NextCol);
		}

		/** Landscape right panel: the next level block scrolls above the button (a long description never pushes the
		    requirements out of the panel). */
		void DrawNextLevelPanel(FVDraw& D, UFVSaveGame& P, const FFVStationLevel* Next, int32 Lv, const FFVRect& Right, FScrollColumn& Col)
		{
			float Y = Col.Begin(D, FFVRect(Right.X + 2.f, Right.Y + 2.f, Right.W - 4.f, Right.H - 36.f), 5.f);
			Y = DrawNextLevelBlock(D, P, Next, Lv, Right.X + 8.f, Y, Right.W - 16.f - Col.BarRoom(), NoLimit);
			Col.End(D, Y, 3.f);
			DrawUpgradeButton(D, P, FFVRect(Right.X + 8.f, Right.Bottom() - 28.f, Right.W - 16.f, 20.f), false);
		}

		/** "Следующий уровень" title + capacity change + requirements, or the max-level note. Returns Y below. */
		float DrawNextLevelBlock(FVDraw& D, UFVSaveGame& P, const FFVStationLevel* Next, int32 Lv, float X, float Y, float W, float MaxY)
		{
			const bool bP = FV::IsPortrait();
			const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
			if (!Next)
			{
				D.Text(TEXT("Максимальный уровень"), FVector2D(X, Y), TS, InkGood(), EFVAlign::Left, false, true);
				Y += D.LineHeight(TS) + 2.f;
				Y += TextWrappedLimited(D, TEXT("Станция улучшена до предела. Отличная работа, хозяин!"), FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 3);
				return Y;
			}
			const FString Title = Next->Name.IsEmpty() ? FString::Printf(TEXT("Уровень %d"), Lv + 1) : FString::Printf(TEXT("Уровень %d: %s"), Lv + 1, *Next->Name);
			D.Text(D.Ellipsize(Title, W, TS), FVector2D(X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS) + 1.f;
			if (!Next->Description.IsEmpty())
			{
				Y += TextWrappedLimited(D, Next->Description, FVector2D(X, Y), W, SmallS(), InkInfo(), 40) + 2.f;
			}
			const int32 CurCap = P.StationCapacity(Station);
			if (Next->Capacity > 0 && Next->Capacity != CurCap && Station != EFVStation::Chest)
			{
				D.Text(FString::Printf(TEXT("Вместимость: %d → %d"), CurCap, Next->Capacity), FVector2D(X, Y), SmallS(), InkGood(), EFVAlign::Left, false, true);
				Y += D.LineHeight(SmallS()) + 2.f;
			}
			Y += 2.f;
			return DrawLevelRequirements(D, P, *Next, X, Y, W, MaxY);
		}

		// ---------------------------------------------------------------------------------------- recipes
		void TabRecipes(FVDraw& D, UFVSaveGame& P, ETab Tab, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			const int32 TI = FMath::Clamp(int32(Tab), 0, NumTabKinds - 1);
			const TArray<const FFVRecipeDef*> List = RecipesOf(Tab);
			int32 Sel = List.IndexOfByPredicate([this, TI](const FFVRecipeDef* R) { return R && R->Id == SelRecipes[TI]; });
			if (Sel == INDEX_NONE && List.Num() > 0)
			{
				Sel = 0;
				SelRecipes[TI] = List[0]->Id;   // default selection of this page (not remembered)
				Qtys[TI] = 1;
			}
			const FFVRecipeDef* R = List.IsValidIndex(Sel) ? List[Sel] : nullptr;
			if (Foot)
			{
				if (R) { DrawRecipeAction(D, P, Tab, *R, *Foot); }
				else { D.Button(*Foot, TEXT("Нет рецептов"), EFVButton::Normal, false, FV::Ui::Body() + 0.5f); }
				return;
			}
			if (bP)
			{
				const float DetailH = FMath::Clamp(FMath::RoundToFloat(200.f + (FV::VH - 640.f) * 0.12f), 196.f, 228.f);
				const FFVRect ListR(Area.X, Area.Y, Area.W, FMath::Max(44.f, Area.H - DetailH - 6.f));
				const FFVRect DetR(Area.X, ListR.Bottom() + 6.f, Area.W, Area.Bottom() - ListR.Bottom() - 6.f);
				D.Panel(ListR, EFVPanel::Parchment);
				DrawRecipeList(D, P, Tab, List, Sel, ListR.Inset(4.f));
				D.Panel(DetR, EFVPanel::Parchment);
				if (R) { DrawRecipeDetail(D, P, Tab, *R, DetR); }
				return;
			}
			const FFVRect ListR(Area.X, Area.Y, 300.f, Area.H);
			const FFVRect DetR(Area.X + 306.f, Area.Y, Area.W - 306.f, Area.H);
			D.Panel(ListR, EFVPanel::Parchment);
			D.Panel(DetR, EFVPanel::Parchment);
			DrawRecipeList(D, P, Tab, List, Sel, ListR.Inset(4.f));
			if (R)
			{
				DrawRecipeDetail(D, P, Tab, *R, DetR);
				const float BY = DetR.Bottom() - 28.f;
				float BX = DetR.X + 8.f;
				if (R->Game == EFVCraftGame::None)
				{
					Qtys[TI] = QtyChips(D, FVector2D(BX, BY), { 1, 5 }, Qtys[TI], 30.f, 20.f, 7.5f);
					BX += 2.f * 34.f + 2.f;
				}
				DrawRecipeAction(D, P, Tab, *R, FFVRect(BX, BY, DetR.Right() - 8.f - BX, 20.f));
			}
		}

		void DrawRecipeList(FVDraw& D, UFVSaveGame& P, ETab Tab, const TArray<const FFVRecipeDef*>& List, int32 Sel, const FFVRect& Area)
		{
			const bool bP = FV::IsPortrait();
			if (List.Num() == 0)
			{
				EmptyState(D, Area, EFVIcon::Hammer, TEXT("Здесь пока нет рецептов."), BodyS());
				return;
			}
			const int32 TI = FMath::Clamp(int32(Tab), 0, NumTabKinds - 1);
			FFVScrollView& Scroll = RecipeLists[TI];
			const float RowH = bP ? 38.f : 26.f;
			const float Pitch = RowH + 2.f;
			Scroll.Update(D, Area, float(List.Num()) * Pitch - 2.f);
			if (EnsureSel[TI] != 0 && Sel >= 0)
			{
				Scroll.ScrollToShow(float(Sel) * Pitch, RowH, EnsureSel[TI] == 2);
			}
			EnsureSel[TI] = 0;
			const float RW = Area.W - (Scroll.CanScroll() ? 7.f : 0.f);
			const float NameS = bP ? FV::Ui::Body() : 8.f;
			const float SubS = SmallS();
			D.PushClip(Area);
			for (int32 i = 0; i < List.Num(); ++i)
			{
				const FFVRect Row(Area.X, Area.Y - Scroll.Offset() + float(i) * Pitch, RW, RowH);
				if (!RowVisible(Area, Row.Y, RowH)) { continue; }
				const FFVRecipeDef& R = *List[i];
				const bool bSel = i == Sel;
				RowBack(D, Row, bSel, !bP && D.Hover(Row), PressingIn(D, Row));
				if (D.Clicked(Row) && !bSel)
				{
					SelectRecipe(Tab, R.Id);
					FVUI::Sfx(EFVSfx::Click);
				}
				const EFVBuyResult St = P.CanCraft(R, 1);
				const bool bLocked = St == EFVBuyResult::StationRequired || St == EFVBuyResult::LevelTooLow;
				const float IS = RowH - 4.f;
				const FFVRect IB(Row.X + 3.f, Row.Y + 2.f, IS, IS);
				IconBox(D, IB);
				DrawItemIcon(D, R.OutputId, IB.Inset(bP ? 3.f : 2.f), 2.f, bLocked ? FLinearColor(1.f, 1.f, 1.f, 0.45f) : FLinearColor::White);
				// status chip
				FString Chip;
				FVUI::EChip Kind = FVUI::EChip::Neutral;
				switch (St)
				{
				case EFVBuyResult::Ok: Chip = TEXT("Можно"); Kind = FVUI::EChip::Good; break;
				case EFVBuyResult::StationRequired: Chip = NeedStationText(Station, R.StationLevel); break;
				case EFVBuyResult::LevelTooLow: Chip = FString::Printf(TEXT("Ур. %d"), R.MinLevel); break;
				case EFVBuyResult::NoRoom: Chip = TEXT("Нет места"); Kind = FVUI::EChip::Bad; break;
				default: Chip = TEXT("Не хватает"); Kind = FVUI::EChip::Bad; break;
				}
				const float CW = FVUI::StatusChip(D, Chip, FVector2D(Row.Right() - 4.f, Row.Y + (bP ? 4.f : 3.f)), Kind, bLocked, bP ? 7.f : 6.f);
				// name (+ mini-game mark)
				const float TX = IB.Right() + 6.f;
				FString Name = FVDB::ItemName(R.OutputId);
				const int32 OutN = FVCookedCount(P, R);   // «Русская печь»: two portions
				if (OutN > 1) { Name += FString::Printf(TEXT(" ×%d"), OutN); }
				const float GameW = R.Game != EFVCraftGame::None ? (bP ? 16.f : 12.f) : 0.f;
				const FString Shown = D.Ellipsize(Name, FMath::Max(20.f, Row.Right() - 10.f - CW - TX - GameW), NameS, true);
				D.Text(Shown, FVector2D(TX, Row.Y + (bP ? 3.f : 2.f)), NameS, bLocked ? FVPal::InkSoft() : FVPal::Ink(), EFVAlign::Left, false, true);
				if (GameW > 0.f)
				{
					const float NW = D.MeasureText(Shown, NameS, true).X;
					const float GS = GameW - 3.f;
					DrawIcon(D, R.Game == EFVCraftGame::Cooking ? EFVIcon::Pot : EFVIcon::Knot, FFVRect(TX + NW + 3.f, Row.Y + (bP ? 3.f : 2.f), GS, GS), 1.f);
				}
				D.Text(D.Ellipsize(IngredientsText(R.Inputs), Row.Right() - 6.f - TX, SubS, false), FVector2D(TX, Row.Y + (bP ? 21.f : 14.f)), SubS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			EndList(D, Scroll, Area);
		}

		/** Recipe card (title, description, effects, requirements) as a smoothly scrolling column inside Panel
		    (landscape: the bottom 32 px stay free for the quantity chips + the action button). */
		void DrawRecipeDetail(FVDraw& D, UFVSaveGame& P, ETab Tab, const FFVRecipeDef& R, const FFVRect& Panel)
		{
			const bool bP = FV::IsPortrait();
			const int32 TI = FMath::Clamp(int32(Tab), 0, NumTabKinds - 1);
			FScrollColumn& Col = RecipeDetails[TI];
			const FFVRect ColR = bP ? Panel.Inset(2.f) : FFVRect(Panel.X + 2.f, Panel.Y + 2.f, Panel.W - 4.f, Panel.H - 34.f);
			float Y = Col.Begin(D, ColR, 4.f);
			const float X = Panel.X + 8.f;
			const float W = Panel.W - 16.f - Col.BarRoom();
			const FFVItemDef* Out = FVDB::FindItem(R.OutputId);
			const FFVBaitDef* OutBait = Out ? nullptr : FVDB::FindBait(R.OutputId);
			const bool bQty = R.Game == EFVCraftGame::None;
			const int32 Times = bQty ? FMath::Max(1, Qtys[TI]) : 1;

			// title (+ portrait quantity chips at the top-right)
			const float TS = bP ? FV::Ui::Body() + 1.5f : 10.f;
			float TitleW = W;
			if (bP && bQty)
			{
				const float ChipW = 40.f, ChipH = 26.f;
				Qtys[TI] = QtyChips(D, FVector2D(X + W - 2.f * ChipW - 4.f, Y - 1.f), { 1, 5 }, Qtys[TI], ChipW, ChipH, FV::Ui::Body());
				TitleW -= 2.f * ChipW + 10.f;
			}
			FString Title = FVDB::ItemName(R.OutputId);
			const int32 OutN = FVCookedCount(P, R);   // «Русская печь»: two portions
			if (OutN > 1) { Title += FString::Printf(TEXT(" ×%d"), OutN); }
			D.Text(D.Ellipsize(Title, TitleW, TS), FVector2D(X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS);
			FString Cat = KindText(R.OutputId);
			if (R.bRecycle) { Cat += TEXT(" · переработка"); }
			if (R.Game == EFVCraftGame::Cooking) { Cat += R.GameDifficulty > 0 ? TEXT(" · готовка: нарезка и жарка") : TEXT(" · готовка"); }
			else if (R.Game == EFVCraftGame::Knots) { Cat += FString::Printf(TEXT(" · вязка узлов (%d)"), FMath::Max(1, R.GameDifficulty)); }
			D.Text(D.Ellipsize(Cat, TitleW, SmallS(), false), FVector2D(X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
			Y += D.LineHeight(SmallS(), false) + 2.f;
			if (bP && bQty) { Y = FMath::Max(Y, Col.Area.Y - Col.View.Offset() + 4.f + 28.f); }
			D.Divider(FVector2D(X, Y), W);
			Y += 5.f;

			// icon + the full description
			const float BoxS = bP ? 40.f : 34.f;
			const FFVRect Box(X, Y, BoxS, BoxS);
			IconBox(D, Box);
			DrawItemIcon(D, R.OutputId, Box.Inset(3.f), 2.f);
			const float DescH = TextWrappedLimited(D, DescriptionOf(R.OutputId), FVector2D(Box.Right() + 6.f, Y), W - BoxS - 6.f, SmallS(), FVPal::Ink(), 12);
			Y += FMath::Max(BoxS, DescH) + 4.f;
			// update 6 (Р41, Р42): the уха needs a carrot, and carrots grow only in the garden
			{
				static const FName Carrot(TEXT("veg_carrot"));
				const bool bNeedsCarrot = R.Inputs.ContainsByPredicate([](const FFVIngredient& I) { return I.ItemId == Carrot; });
				if (bNeedsCarrot && P.CountIngredient(Carrot) <= 0)
				{
					Y += TextWrappedLimited(D, TEXT("Нужна морковь — растёт в огороде (Прораб, 7 ур.)"), FVector2D(X, Y), W, SmallS(), InkInfo(), 3) + 4.f;
				}
			}
			if (Station == EFVStation::Kitchen && FVCookedCount(P, R) > FMath::Max(1, R.OutputCount))
			{
				Y += TextWrappedLimited(D, TEXT("Русская печь: две порции за раз."), FVector2D(X, Y), W, SmallS(), InkGood(), 2) + 4.f;
			}

			// effects
			TArray<FString> Fx;
			if (Out) { Fx = ItemEffectLines(*Out); }
			else if (OutBait) { Fx = BaitParts(*OutBait); }
			if (Out && Out->Kind == EFVItemKind::Food) { Fx.Add(TEXT("сила 80–140% по качеству")); }
			if (Fx.Num() > 0)
			{
				Y += DrawPartsAll(D, Fx, X, Y, W, SmallS(), InkInfo()) + 2.f;   // every effect, as many lines as needed
			}

			// requirements
			const float S = BodyS();
			const float RH = ReqRowH(D, S);
			D.Text(Times > 1 ? FString::Printf(TEXT("Нужно на ×%d:"), Times) : FString(TEXT("Нужно:")), FVector2D(X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, true);
			Y += D.LineHeight(SmallS()) + 1.f;
			struct FRowItem { int32 Kind; FName Id; int32 Have; int32 Need; FString Label; bool bOk; EFVIcon Icon; };
			TArray<FRowItem> Rows;
			TArray<FString> FishNotes;   // what the "any fish" tokens take (update 5: the rarity tokens too)
			for (const FFVIngredient& I : R.Inputs)
			{
				Rows.Add({ 0, I.ItemId, P.CountIngredient(I.ItemId), I.Count * Times, FString(), true, EFVIcon::Count });
				EFVRarity TokenRarity = EFVRarity::Common;
				if (FVDB::FishTokenRarity(I.ItemId, TokenRarity))
				{
					FishNotes.AddUnique(FString::Printf(TEXT("%s = самая дешёвая некопчёная рыба редкости «%s» из садка; рыба другой редкости не подойдёт"),
						*FVDB::ItemName(I.ItemId), *FVDB::RarityName(TokenRarity)));
				}
				else if (I.ItemId == FVDB::AnyFishToken())
				{
					FishNotes.AddUnique(TEXT("Любая рыба = самая дешёвая некопчёная рыба из садка"));
				}
			}
			if (R.CoinCost > 0) { Rows.Add({ 1, NAME_None, 0, R.CoinCost * Times, FString(), true, EFVIcon::Coin }); }
			if (P.GetStationLevel(Station) < R.StationLevel) { Rows.Add({ 2, NAME_None, 0, 0, NeedStationText(Station, R.StationLevel), false, EFVIcon::Hammer }); }
			if (P.GetLevel() < R.MinLevel) { Rows.Add({ 2, NAME_None, 0, 0, FString::Printf(TEXT("Уровень рыбалки %d"), R.MinLevel), false, EFVIcon::Level }); }
			const bool bTwoCols = Rows.Num() > 2 || bP;
			const float ColW = bTwoCols ? FMath::FloorToFloat((W - 10.f) * 0.5f) : W;
			// a condition too long for its grid cell («Нужна мастерская мастера») gets a full-width row under the grid
			TArray<FRowItem> WideRows;
			if (bTwoCols)
			{
				const float CondRoom = ColW - (bP ? 16.f : 12.f) - 8.f - D.MeasureText(TEXT("нет"), S, true).X;
				for (int32 i = Rows.Num() - 1; i >= 0; --i)
				{
					if (Rows[i].Kind == 2 && D.MeasureText(Rows[i].Label, S, false).X > CondRoom)
					{
						WideRows.Insert(Rows[i], 0);
						Rows.RemoveAt(i);
					}
				}
			}
			for (int32 i = 0; i < Rows.Num(); ++i)
			{
				const int32 C = bTwoCols ? i % 2 : 0;
				const float RX = X + float(C) * (ColW + 10.f);
				const float RY = Y + float(bTwoCols ? i / 2 : i) * RH;
				const FRowItem& It = Rows[i];
				if (It.Kind == 0) { ReqRow(D, RX, RY, ColW, It.Id, It.Have, It.Need, S); }
				else if (It.Kind == 1) { CoinRow(D, RX, RY, ColW, P.Money, It.Need, S); }
				else { CondRow(D, RX, RY, ColW, It.Label, It.bOk, S, It.Icon); }
			}
			Y += float(bTwoCols ? FMath::DivideAndRoundUp(Rows.Num(), 2) : Rows.Num()) * RH + 2.f;
			for (const FRowItem& It : WideRows)
			{
				CondRow(D, X, Y, W, It.Label, It.bOk, S, It.Icon);
				Y += RH;
			}
			if (R.Id == FName(TEXT("rcp_line_heat")))
			{
				// update 5: recipes take ingredients from the backpack, and a string on a rod is not there
				Y += TextWrappedLimited(D, TEXT("Титановая струна должна лежать в рюкзаке: сними её с удочки (Мастерская → Оснастка)."),
					FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 40);
			}
			for (const FString& Note : FishNotes)
			{
				Y += TextWrappedLimited(D, Note, FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 40);
			}
			Col.End(D, Y, 4.f);
		}

		void DrawRecipeAction(FVDraw& D, UFVSaveGame& P, ETab Tab, const FFVRecipeDef& R, const FFVRect& Btn)
		{
			const int32 TI = FMath::Clamp(int32(Tab), 0, NumTabKinds - 1);
			const bool bQty = R.Game == EFVCraftGame::None;
			const int32 Times = bQty ? FMath::Max(1, Qtys[TI]) : 1;
			const EFVBuyResult St = P.CanCraft(R, Times);
			const float TS = FV::IsPortrait() ? FV::Ui::Body() + 0.5f : 8.f;
			if (St != EFVBuyResult::Ok)
			{
				FString Why;
				switch (St)
				{
				case EFVBuyResult::StationRequired: Why = NeedStationText(Station, R.StationLevel); break;
				case EFVBuyResult::LevelTooLow: Why = FString::Printf(TEXT("Нужен уровень %d"), R.MinLevel); break;
				case EFVBuyResult::NotEnoughMoney: Why = TEXT("Не хватает монет"); break;
				case EFVBuyResult::NoRoom: Why = TEXT("Нет места в рюкзаке"); break;
				case EFVBuyResult::NotEnoughMaterials: Why = TEXT("Не хватает материалов"); break;
				default: Why = ResultText(St); break;
				}
				D.Button(Btn, Why, EFVButton::Normal, false, TS);
				return;
			}
			FString Label;
			if (R.Game == EFVCraftGame::Cooking) { Label = TEXT("Готовить"); }
			else if (R.Game == EFVCraftGame::Knots) { Label = TEXT("Вязать узлы"); }
			else
			{
				Label = Station == EFVStation::Kitchen ? TEXT("Приготовить") : (R.bRecycle ? TEXT("Переработать") : TEXT("Сделать"));
				if (Times > 1) { Label += FString::Printf(TEXT(" ×%d"), Times); }
			}
			const int64 Price = R.CoinCost > 0 ? int64(R.CoinCost) * Times : -1;
			if (CoinButton(D, Btn, Label, Price, EFVButton::Primary, true, true, TS))
			{
				Want(R.Game == EFVCraftGame::None ? EAct::Craft : EAct::Play, R.Id, -1, Times);
			}
		}

		// ---------------------------------------------------------------------------------------- rigging (Оснастка)
		struct FCand
		{
			FName Id;
			bool bInstalled = false;
			bool bNone = false;       // "без наживки"
			int32 Count = 0;
			int32 Tier = 0;
		};

		TArray<FCand> Candidates(const UFVSaveGame& P, const FFVRodLoadout& L) const
		{
			TArray<FCand> Out;
			if (TackleSlot < 2)
			{
				const EFVItemKind K = TackleSlot == 0 ? EFVItemKind::Line : EFVItemKind::Float;
				const FName Cur = TackleSlot == 0 ? L.Line : L.Float;
				if (!Cur.IsNone())
				{
					FCand C;
					C.Id = Cur;
					C.bInstalled = true;
					C.Count = 1;
					if (const FFVItemDef* It = FVDB::FindItem(Cur)) { C.Tier = It->Tier; }
					Out.Add(C);
				}
				TArray<FCand> Pack;
				for (const FFVInvStack& S : P.Backpack)
				{
					const FFVItemDef* It = FVDB::FindItem(S.ItemId);
					if (!It || It->Kind != K || S.Count <= 0) { continue; }
					FCand* Found = Pack.FindByPredicate([&S](const FCand& C) { return C.Id == S.ItemId; });
					if (Found) { Found->Count += S.Count; continue; }
					FCand C;
					C.Id = S.ItemId;
					C.Count = S.Count;
					C.Tier = It->Tier;
					Pack.Add(C);
				}
				Pack.StableSort([](const FCand& A, const FCand& B) { return A.Tier < B.Tier; });
				Out.Append(Pack);
				return Out;
			}
			FCand None;
			None.bNone = true;
			None.bInstalled = P.EquippedBait.IsNone() || P.BaitCount(P.EquippedBait) <= 0;
			Out.Add(None);
			for (const FFVBaitDef& B : FVDB::Baits())
			{
				const int32 N = P.BaitCount(B.Id);
				if (N <= 0) { continue; }
				FCand C;
				C.Id = B.Id;
				C.Count = N;
				C.bInstalled = P.EquippedBait == B.Id;
				Out.Add(C);
			}
			return Out;
		}

		int32 FindCand(const TArray<FCand>& C) const
		{
			if (!bHasCand) { return INDEX_NONE; }
			for (int32 i = 0; i < C.Num(); ++i)
			{
				if (bCandNone ? C[i].bNone : (!C[i].bNone && C[i].Id == CandId && (TackleSlot == 2 || C[i].bInstalled == bCandInstalled))) { return i; }
			}
			return INDEX_NONE;
		}
		bool bCandInstalled = false;

		void SelectCand(const FCand& C)
		{
			bHasCand = true;
			CandId = C.Id;
			bCandNone = C.bNone;
			bCandInstalled = C.bInstalled;
		}

		/** Label + enabled state of the main rigging action for a candidate. */
		void CandAction(const UFVSaveGame& P, const FCand& C, FString& OutLabel, bool& bOutEnabled, EAct& OutAct) const
		{
			bOutEnabled = false;
			OutAct = EAct::None;
			if (TackleSlot == 2)
			{
				if (C.bInstalled) { OutLabel = C.bNone ? TEXT("Уже без наживки") : TEXT("Уже насажена"); return; }
				OutLabel = C.bNone ? TEXT("Снять наживку") : TEXT("Насадить");
				bOutEnabled = true;
				OutAct = EAct::EquipBait;
				return;
			}
			if (C.bInstalled)
			{
				OutLabel = TEXT("Снять в рюкзак");
				bOutEnabled = true;
				OutAct = EAct::RemoveTackle;
				return;
			}
			const EFVBuyResult St = P.CanInstallTackle(SelRod, C.Id);
			switch (St)
			{
			case EFVBuyResult::Ok: OutLabel = TEXT("Установить"); bOutEnabled = true; OutAct = EAct::Install; break;
			case EFVBuyResult::LevelTooLow: OutLabel = TEXT("Нужна удочка получше"); break;
			case EFVBuyResult::StationRequired: OutLabel = TEXT("Нужна мастерская"); break;
			default: OutLabel = ResultText(St); break;
			}
		}

		void TabRigging(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			// rods
			TArray<const FFVRodDef*> Rods;
			for (const FName& Id : P.OwnedRods)
			{
				if (const FFVRodDef* R = FVDB::FindRod(Id)) { Rods.Add(R); }
			}
			if (Rods.Num() == 0)
			{
				if (Foot)
				{
					D.Button(*Foot, TEXT("Нет удочек"), EFVButton::Normal, false, FV::Ui::Body() + 0.5f);
					return;
				}
				D.Panel(Area, EFVPanel::Parchment);
				EmptyState(D, Area, EFVIcon::Rod, TEXT("У вас нет удочек."), BodyS());
				return;
			}
			int32 RodIdx = Rods.IndexOfByPredicate([this](const FFVRodDef* R) { return R->Id == SelRod; });
			if (RodIdx == INDEX_NONE)
			{
				RodIdx = FMath::Max(0, Rods.IndexOfByPredicate([&P](const FFVRodDef* R) { return R->Id == P.EquippedRod; }));
				SelRod = Rods[RodIdx]->Id;
			}
			const FFVRodDef& Rod = *Rods[RodIdx];
			const FFVRodLoadout L = P.GetLoadout(Rod.Id);
			TArray<FCand> Cands = Candidates(P, L);
			int32 CI = FindCand(Cands);
			if (CI == INDEX_NONE && Cands.Num() > 0)
			{
				CI = 0;
				SelectCand(Cands[0]);
			}
			const FCand* Cand = Cands.IsValidIndex(CI) ? &Cands[CI] : nullptr;
			if (Foot)
			{
				DrawCandButton(D, P, Cand, *Foot);
				return;
			}

			// stats: base, current, preview with the candidate
			const FFVRodStats Base = FFVRodStats::FromRod(Rod);
			FFVRodStats Cur = Base;
			Cur.Apply(P.GetTackleMods(Rod.Id));
			bool bPreview = false;
			FFVRodStats Prev = Base;
			if (Cand && TackleSlot < 2)
			{
				FName NewLine = L.Line, NewFloat = L.Float;
				FName& Slot = TackleSlot == 0 ? NewLine : NewFloat;
				Slot = Cand->bInstalled ? FName(NAME_None) : Cand->Id;
				Prev.Apply(ModsWith(NewLine, NewFloat));
				bPreview = true;
			}

			if (bP)
			{
				float Y = Area.Y;
				// rod selector
				const float SelH = 40.f;
				{
					const FFVRect Sel(Area.X, Y, Area.W, SelH);
					D.Panel(Sel, EFVPanel::Parchment);
					const FFVRect Prv(Sel.X + 3.f, Sel.Y + 3.f, 34.f, SelH - 6.f);
					const FFVRect Nxt(Sel.Right() - 37.f, Sel.Y + 3.f, 34.f, SelH - 6.f);
					const bool bMany = Rods.Num() > 1;
					if (D.Button(Prv, FString(), EFVButton::Normal, bMany)) { SelectRod(Rods[(RodIdx + Rods.Num() - 1) % Rods.Num()]->Id); }
					if (D.Button(Nxt, FString(), EFVButton::Normal, bMany)) { SelectRod(Rods[(RodIdx + 1) % Rods.Num()]->Id); }
					Triangle(D, Prv.Center(), 2, 9, bMany ? FVPal::Ink() : FVPal::Disabled());
					Triangle(D, Nxt.Center(), 3, 9, bMany ? FVPal::Ink() : FVPal::Disabled());
					const FFVRect IB(Prv.Right() + 6.f, Sel.Y + 4.f, SelH - 8.f, SelH - 8.f);
					DrawItemIcon(D, Rod.Id, IB, 2.f);
					const float TX = IB.Right() + 6.f;
					const float TW = Nxt.X - 6.f - TX;
					D.Text(D.Ellipsize(Rod.Name, TW, FV::Ui::Body(), true), FVector2D(TX, Sel.Y + 5.f), FV::Ui::Body(), FVPal::Ink(), EFVAlign::Left, false, true);
					// 0.6.2 (Р84): the rod type in every rod line
					const FString Type = FVDBU6::RodTypeLabel(Rod);
					const FString Sub = Rod.Id == P.EquippedRod ? FString::Printf(TEXT("%s · в руках · ранг %d · %d/%d"), *Type, Rod.Tier, RodIdx + 1, Rods.Num())
						: FString::Printf(TEXT("%s · ранг %d · %d/%d"), *Type, Rod.Tier, RodIdx + 1, Rods.Num());
					D.Text(D.Ellipsize(Sub, TW, SmallS(), false), FVector2D(TX, Sel.Y + 21.f), SmallS(), Rod.Id == P.EquippedRod ? InkGood() : FVPal::InkSoft(), EFVAlign::Left, false, false);
					Y += SelH + 6.f;
				}
				// slots
				const float SlotH = 62.f;
				DrawTackleSlots(D, P, L, FFVRect(Area.X, Y, Area.W, SlotH));
				Y += SlotH + 6.f;
				// stats card (bottom) + candidates list (middle)
				const float StatsH = TackleSlot < 2 ? 104.f : 58.f;
				const FFVRect StatsR(Area.X, Area.Bottom() - StatsH, Area.W, StatsH);
				const FFVRect ListR(Area.X, Y, Area.W, FMath::Max(44.f, StatsR.Y - 6.f - Y));
				D.Panel(ListR, EFVPanel::Parchment);
				DrawCandidates(D, P, Cands, CI, ListR.Inset(5.f));
				D.Panel(StatsR, EFVPanel::Parchment);
				DrawRigStats(D, P, Base, Cur, bPreview ? &Prev : nullptr, Cand, StatsR);
				return;
			}

			// landscape: rods | slots + candidates + stats
			const FFVRect RodsR(Area.X, Area.Y, 172.f, Area.H);
			D.Panel(RodsR, EFVPanel::Parchment);
			{
				const FFVRect In = RodsR.Inset(5.f);
				D.Text(TEXT("Удочки"), FVector2D(In.X + 2.f, In.Y), 8.f, FVPal::Ink(), EFVAlign::Left, false, true);
				const FFVRect ListA(In.X, In.Y + 13.f, In.W, In.H - 13.f);
				const float RowH = 26.f;
				const float Pitch = RowH + 2.f;
				RodList.Update(D, ListA, float(Rods.Num()) * Pitch - 2.f);
				const float RW = ListA.W - (RodList.CanScroll() ? 6.f : 0.f);
				D.PushClip(ListA);
				for (int32 i = 0; i < Rods.Num(); ++i)
				{
					const FFVRect Row(ListA.X, ListA.Y - RodList.Offset() + float(i) * Pitch, RW, RowH);
					if (!RowVisible(ListA, Row.Y, RowH)) { continue; }
					const FFVRodDef& R = *Rods[i];
					RowBack(D, Row, i == RodIdx, D.Hover(Row), PressingIn(D, Row));
					if (D.Clicked(Row) && i != RodIdx) { SelectRod(R.Id); FVUI::Sfx(EFVSfx::Click); }
					DrawItemIcon(D, R.Id, FFVRect(Row.X + 3.f, Row.Y + 3.f, 20.f, 20.f), 1.f);
					D.Text(D.Ellipsize(R.Name, Row.W - 32.f, 7.f), FVector2D(Row.X + 27.f, Row.Y + 3.f), 7.f, FVPal::Ink(), EFVAlign::Left, false, true);
					const FFVRodLoadout RL = P.GetLoadout(R.Id);
					const int32 NumTackle = (RL.Line.IsNone() ? 0 : 1) + (RL.Float.IsNone() ? 0 : 1);
					const FString Sub = FString::Printf(TEXT("%sснасти: %d/2"), R.Id == P.EquippedRod ? TEXT("в руках · ") : TEXT(""), NumTackle);
					D.Text(Sub, FVector2D(Row.X + 27.f, Row.Y + 14.f), 6.f, R.Id == P.EquippedRod ? InkGood() : FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
				EndList(D, RodList, ListA);
			}
			const float RX = RodsR.Right() + 6.f;
			const float RW = Area.Right() - RX;
			DrawTackleSlots(D, P, L, FFVRect(RX, Area.Y, RW, 56.f));
			const float BY = Area.Y + 62.f;
			const FFVRect CandR(RX, BY, 208.f, Area.Bottom() - BY);
			const FFVRect StatsR(CandR.Right() + 6.f, BY, Area.Right() - CandR.Right() - 6.f, Area.Bottom() - BY);
			D.Panel(CandR, EFVPanel::Parchment);
			D.Panel(StatsR, EFVPanel::Parchment);
			DrawCandidates(D, P, Cands, CI, FFVRect(CandR.X + 5.f, CandR.Y + 5.f, CandR.W - 10.f, CandR.H - 36.f));
			DrawCandButton(D, P, Cand, FFVRect(CandR.X + 6.f, CandR.Bottom() - 26.f, CandR.W - 12.f, 20.f));
			DrawRigStats(D, P, Base, Cur, bPreview ? &Prev : nullptr, Cand, StatsR);
		}

		void SelectRod(FName Id)
		{
			if (Id == SelRod) { return; }
			SelRod = Id;
			bHasCand = false;
			CandList.Reset();
		}

		void DrawTackleSlots(FVDraw& D, UFVSaveGame& P, const FFVRodLoadout& L, const FFVRect& R)
		{
			const bool bP = FV::IsPortrait();
			static const TCHAR* Labels[3] = { TEXT("Леска"), TEXT("Поплавок"), TEXT("Наживка") };
			static const TCHAR* Defaults[3] = { TEXT("Простая леска"), TEXT("Пробковый поплавок"), TEXT("Без наживки") };
			const EFVIcon DefIcons[3] = { EFVIcon::Line, EFVIcon::Float, EFVIcon::Bait };
			for (int32 i = 0; i < 3; ++i)
			{
				const FFVRect C = FVDraw::SplitH(R, i, 3, 5.f);
				if (D.Slot(C, TackleSlot == i, true) && TackleSlot != i)
				{
					TackleSlot = i;
					bHasCand = false;
					CandList.Reset();
				}
				FName Id = i == 0 ? L.Line : (i == 1 ? L.Float : P.EquippedBait);
				if (i == 2 && P.BaitCount(Id) <= 0) { Id = NAME_None; }
				const float IS = bP ? 28.f : 24.f;
				const FFVRect IB = bP ? FFVRect(C.Center().X - IS * 0.5f, C.Y + 16.f, IS, IS) : FFVRect(C.X + 5.f, C.Y + (C.H - IS) * 0.5f, IS, IS);
				if (Id.IsNone()) { DrawIcon(D, DefIcons[i], IB, 2.f, FLinearColor(1.f, 1.f, 1.f, 0.4f)); }
				else { DrawItemIcon(D, Id, IB, 2.f); }
				const FString Name = Id.IsNone() ? FString(Defaults[i]) : FVDB::ItemName(Id);
				const float LS = bP ? FV::Ui::Small() : 7.f;
				if (bP)
				{
					D.Text(Labels[i], FVector2D(C.Center().X, C.Y + 3.f), LS, FVPal::InkSoft(), EFVAlign::Center, false, true);
					D.Text(D.Ellipsize(Name, C.W - 6.f, LS, false), FVector2D(C.Center().X, IB.Bottom() + 2.f), LS, Id.IsNone() ? FVPal::InkSoft() : FVPal::Ink(), EFVAlign::Center, false, false);
				}
				else
				{
					const float TX = IB.Right() + 5.f;
					D.Text(Labels[i], FVector2D(TX, C.Y + 8.f), LS, FVPal::InkSoft(), EFVAlign::Left, false, true);
					TextWrappedLimited(D, Name, FVector2D(TX, C.Y + 19.f), C.Right() - 4.f - TX, 6.5f, Id.IsNone() ? FVPal::InkSoft() : FVPal::Ink(), 2);
				}
			}
		}

		void DrawCandidates(FVDraw& D, UFVSaveGame& P, const TArray<FCand>& Cands, int32 CI, const FFVRect& Area)
		{
			const bool bP = FV::IsPortrait();
			const float TitleS = bP ? FV::Ui::Small() : 7.f;
			static const TCHAR* Titles[3] = { TEXT("Лески: установлена и в рюкзаке"), TEXT("Поплавки: установлен и в рюкзаке"), TEXT("Наживка") };
			D.Text(Titles[FMath::Clamp(TackleSlot, 0, 2)], FVector2D(Area.X + 2.f, Area.Y), TitleS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			const float Top = Area.Y + D.LineHeight(TitleS) + 2.f;
			const FFVRect ListA(Area.X, Top, Area.W, Area.Bottom() - Top);
			if (Cands.Num() == 0)
			{
				EmptyState(D, ListA, TackleSlot == 0 ? EFVIcon::Line : EFVIcon::Float,
					TackleSlot == 0 ? TEXT("В рюкзаке нет лесок. Сплетите леску на вкладке «Снасти» или купите в лавке.")
					: TEXT("В рюкзаке нет поплавков. Сделайте поплавок на вкладке «Снасти» или купите в лавке."), SmallS());
				return;
			}
			const float RowH = bP ? 38.f : 24.f;
			const float Pitch = RowH + 2.f;
			CandList.Update(D, ListA, float(Cands.Num()) * Pitch - 2.f);
			const float RW = ListA.W - (CandList.CanScroll() ? 7.f : 0.f);
			D.PushClip(ListA);
			for (int32 i = 0; i < Cands.Num(); ++i)
			{
				const FFVRect Row(ListA.X, ListA.Y - CandList.Offset() + float(i) * Pitch, RW, RowH);
				if (!RowVisible(ListA, Row.Y, RowH)) { continue; }
				const FCand& C = Cands[i];
				RowBack(D, Row, i == CI, !bP && D.Hover(Row), PressingIn(D, Row));
				if (D.Clicked(Row) && i != CI) { SelectCand(C); FVUI::Sfx(EFVSfx::Click); }
				const float IS = RowH - 6.f;
				const FFVRect IB(Row.X + 3.f, Row.Y + 3.f, IS, IS);
				if (C.bNone) { DrawIcon(D, EFVIcon::Close, IB.Inset(3.f), 1.f, FLinearColor(1.f, 1.f, 1.f, 0.6f)); }
				else { DrawItemIcon(D, C.Id, IB, 2.f); }
				const float NameS = bP ? FV::Ui::Body() : 7.f;
				const float SubS = SmallS();
				FString Chip;
				if (C.bInstalled) { Chip = TackleSlot == 2 ? TEXT("Сейчас") : TEXT("Стоит"); }
				else if (C.Count > 1) { Chip = FString::Printf(TEXT("×%d"), C.Count); }
				float CW = 0.f;
				if (!Chip.IsEmpty())
				{
					CW = FVUI::StatusChip(D, Chip, FVector2D(Row.Right() - 3.f, Row.Y + 3.f), C.bInstalled ? FVUI::EChip::Good : FVUI::EChip::Neutral, false, bP ? 7.f : 6.f);
				}
				const float TX = IB.Right() + 5.f;
				const FString Name = C.bNone ? FString(TEXT("Без наживки")) : FVDB::ItemName(C.Id);
				D.Text(D.Ellipsize(Name, Row.Right() - 8.f - CW - TX, NameS), FVector2D(TX, Row.Y + (bP ? 3.f : 2.f)), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
				TArray<FString> Parts;
				if (C.bNone) { Parts.Add(TEXT("Клёв медленнее, больше мусора")); }
				else if (const FFVItemDef* It = FVDB::FindItem(C.Id))
				{
					Parts = FVDB::StatModsLines(It->Tackle);
					if (P.CanInstallTackle(SelRod, C.Id) == EFVBuyResult::LevelTooLow && !C.bInstalled)
					{
						Parts.Insert(FString::Printf(TEXT("ранг %d — не для этой удочки"), int32(It->Tier)), 0);
					}
				}
				else if (const FFVBaitDef* B = FVDB::FindBait(C.Id)) { Parts = BaitParts(*B); }
				const bool bBad = !C.bNone && !C.bInstalled && TackleSlot < 2 && P.CanInstallTackle(SelRod, C.Id) != EFVBuyResult::Ok;
				D.Text(D.Ellipsize(FString::Join(Parts, TEXT(" · ")), Row.Right() - 5.f - TX, SubS, false), FVector2D(TX, Row.Y + (bP ? 21.f : 13.f)), SubS, bBad ? InkBad() : FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			EndList(D, CandList, ListA);
		}

		void DrawCandButton(FVDraw& D, UFVSaveGame& P, const FCand* Cand, const FFVRect& R)
		{
			const float TS = FV::IsPortrait() ? FV::Ui::Body() + 0.5f : 8.f;
			if (!Cand)
			{
				D.Button(R, TackleSlot == 2 ? TEXT("Нет наживки") : TEXT("Нечего установить"), EFVButton::Normal, false, TS);
				return;
			}
			FString Label;
			bool bEnabled = false;
			EAct Act = EAct::None;
			CandAction(P, *Cand, Label, bEnabled, Act);
			if (D.Button(R, Label, Act == EAct::RemoveTackle ? EFVButton::Normal : EFVButton::Primary, bEnabled, TS) && bEnabled)
			{
				Want(Act, Cand->bNone ? FName(NAME_None) : Cand->Id, TackleSlot, 1, SelRod);
			}
		}

		/** The stats card (Panel = the parchment panel): a smoothly scrolling column. */
		void DrawRigStats(FVDraw& D, UFVSaveGame& P, const FFVRodStats& Base, const FFVRodStats& Cur, const FFVRodStats* Prev, const FCand* Cand, const FFVRect& Panel)
		{
			const bool bP = FV::IsPortrait();
			const float S = bP ? FV::Ui::Small() : 6.5f;
			const float Step = FMath::RoundToFloat(D.LineHeight(S, false) + (bP ? 1.f : 0.f));
			const float Pad = bP ? 7.f : 6.f;
			float Y = RigStatsCol.Begin(D, Panel.Inset(2.f), Pad - 2.f);
			const FFVRect R(Panel.X + Pad, Y, Panel.W - 2.f * Pad - RigStatsCol.BarRoom(), 0.f);
			if (TackleSlot == 2)
			{
				// bait: current vs chosen
				const FFVBaitDef* CurB = P.GetBait();
				D.Text(TEXT("Наживка сейчас:"), FVector2D(R.X, Y), S, FVPal::InkSoft(), EFVAlign::Left, false, true);
				Y += Step;
				const FString CurText = CurB ? FString::Printf(TEXT("%s — %s"), *CurB->Name, *FString::Join(BaitParts(*CurB), TEXT(" · "))) : FString(TEXT("без наживки"));
				Y += TextWrappedLimited(D, CurText, FVector2D(R.X, Y), R.W, S, FVPal::Ink(), 4) + 2.f;
				if (Cand && !Cand->bInstalled)
				{
					const FFVBaitDef* NB = Cand->bNone ? nullptr : FVDB::FindBait(Cand->Id);
					const FString NewText = NB ? FString::Printf(TEXT("Выбрана: %s — %s"), *NB->Name, *FString::Join(BaitParts(*NB), TEXT(" · "))) : FString(TEXT("Выбрано: без наживки"));
					Y += TextWrappedLimited(D, NewText, FVector2D(R.X, Y), R.W, S, InkInfo(), 4);
				}
				RigStatsCol.End(D, Y, Pad);
				return;
			}
			const float ColW = bP ? 62.f : 36.f;
			const float C3 = R.Right();
			const float C2 = Prev ? C3 - ColW : C3;
			const float C1 = C2 - ColW;
			D.Text(TEXT("Удочка"), FVector2D(C1, Y), S, FVPal::InkSoft(), EFVAlign::Right, false, true);
			D.Text(TEXT("Сейчас"), FVector2D(C2, Y), S, FVPal::InkSoft(), EFVAlign::Right, false, true);
			if (Prev) { D.Text(Cand && Cand->bInstalled ? TEXT("Без неё") : TEXT("С выбранной"), FVector2D(C3, Y), S, InkInfo(), EFVAlign::Right, false, true); }
			Y += Step + 1.f;
			for (int32 K = 0; K < NumStats; ++K)
			{
				const float V0 = StatValue(Base, K);
				const float V1 = StatValue(Cur, K);
				const float V2 = Prev ? StatValue(*Prev, K) : V1;
				if (K > 4 && StatNeutral(V0, K) && StatNeutral(V1, K) && StatNeutral(V2, K)) { continue; }
				D.Text(D.Ellipsize(GStats[K].Label, C1 - ColW + 6.f - R.X, S, false), FVector2D(R.X, Y), S, FVPal::InkSoft(), EFVAlign::Left, false, false);
				D.Text(StatText(V0, K), FVector2D(C1, Y), S, FVPal::InkSoft(), EFVAlign::Right, false, false);
				D.Text(StatText(V1, K), FVector2D(C2, Y), S, StatInk(V1, V0, K), EFVAlign::Right, false, true);
				if (Prev) { D.Text(StatText(V2, K), FVector2D(C3, Y), S, StatInk(V2, V1, K), EFVAlign::Right, false, true); }
				Y += Step;
			}
			RigStatsCol.End(D, Y, Pad);
		}

		// ---------------------------------------------------------------------------------------- shovel
		void TabShovel(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			const TArray<FFVShovelDef>& Sh = FVDB::Shovels();
			const int32 Cur = FMath::Clamp(P.ShovelLevel, 0, Sh.Num());
			const FFVShovelDef* Next = Sh.IsValidIndex(Cur) ? &Sh[Cur] : nullptr;
			const int32 WL = Next ? Next->WorkshopLevel : 0;
			const int32 CraftPrice = Next ? Next->Price / 2 : 0;
			const bool bCraftable = Next && WL > 0;
			const bool bCanCraft = bCraftable && P.GetStationLevel(EFVStation::Workshop) >= WL && P.GetLevel() >= Next->MinLevel
				&& P.CanAfford(CraftPrice) && P.HasIngredients(Next->Craft, 1);

			auto Button = [&](const FFVRect& R)
			{
				const float BTS = bP ? FV::Ui::Body() + 0.5f : 8.f;
				if (!Next) { D.Button(R, TEXT("Лучшая лопата"), EFVButton::Normal, false, BTS); return; }
				if (!bCraftable) { D.Button(R, TEXT("Только в лавке"), EFVButton::Normal, false, BTS); return; }
				if (!bCanCraft)
				{
					FString Why = TEXT("Не хватает материалов");
					if (P.GetStationLevel(EFVStation::Workshop) < WL) { Why = NeedStationText(EFVStation::Workshop, WL); }
					else if (P.GetLevel() < Next->MinLevel) { Why = FString::Printf(TEXT("Нужен уровень %d"), Next->MinLevel); }
					else if (!P.CanAfford(CraftPrice)) { Why = TEXT("Не хватает монет"); }
					D.Button(R, Why, EFVButton::Normal, false, BTS);
					return;
				}
				if (CoinButton(D, R, TEXT("Выковать"), CraftPrice > 0 ? CraftPrice : -1, EFVButton::Primary, true, true, BTS)) { Want(EAct::CraftShovel); }
			};
			if (Foot)
			{
				Button(*Foot);
				return;
			}

			// the ladder + the next shovel in one smoothly scrolling column (landscape: the button row stays below)
			D.Panel(Area, EFVPanel::Parchment);
			const FFVRect ColR = bP ? Area.Inset(2.f) : FFVRect(Area.X + 2.f, Area.Y + 2.f, Area.W - 4.f, Area.H - 34.f);
			float Y = ShovelCol.Begin(D, ColR, 6.f);
			const float X = Area.X + 8.f, W = Area.W - 16.f - ShovelCol.BarRoom();
			// shovel ladder
			const int32 N = Sh.Num();
			const int32 Cols = bP ? 2 : FMath::Max(1, N);
			const float CardH = bP ? 52.f : 58.f;
			const int32 Rows = FMath::DivideAndRoundUp(FMath::Max(1, N), Cols);
			for (int32 i = 0; i < N; ++i)
			{
				const int32 Row = i / Cols;
				const FFVRect C = FVDraw::SplitH(FFVRect(X, Y + float(Row) * (CardH + 5.f), W, CardH), i % Cols, Cols, 5.f);
				const bool bHave = i < Cur;
				const bool bNext = i == Cur;
				D.Rect(C, bNext ? FVPal::Gold() : BoxEdge());
				D.Rect(C.Inset(bNext ? 2.f : 1.f), bHave ? FV::Col(226, 240, 196) : (bNext ? FV::Col(255, 244, 214) : FV::Col(240, 214, 170)));
				const float IS = bP ? 32.f : 32.f;
				const FFVRect IB(C.X + 6.f, C.Y + (C.H - IS) * 0.5f, IS, IS);
				if (UFVSprites* Sp = UFVSprites::Get())
				{
					const FFVSprite S = Sp->ShovelIcon(i + 1);
					FVUI::SpriteFit(D, S, IB, bHave || bNext ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.5f), 2.f);
				}
				const float TX = IB.Right() + 5.f;
				const float NS = bP ? FV::Ui::Small() + 0.5f : 7.f;
				TextWrappedLimited(D, Sh[i].Name, FVector2D(TX, C.Y + 5.f), C.Right() - 4.f - TX, NS, FVPal::Ink(), 2, true);
				const FString Sub = FString::Printf(TEXT("%s с на копку"), *FVUI::Num(Sh[i].DigSeconds, 1));
				D.Text(D.Ellipsize(Sub, C.Right() - 4.f - TX, SmallS(), false), FVector2D(TX, C.Bottom() - D.LineHeight(SmallS(), false) - 4.f), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				if (bHave && i == Cur - 1) { CheckBadge(D, FVector2D(C.Right() - 8.f, C.Y + 8.f), 5.f); }
			}
			Y += float(Rows) * (CardH + 5.f) + 4.f;
			D.Divider(FVector2D(X, Y), W);
			Y += 6.f;

			const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
			if (!Next)
			{
				D.Text(TEXT("У вас лучшая лопата в долине!"), FVector2D(X, Y), TS, InkGood(), EFVAlign::Left, false, true);
				Y += D.LineHeight(TS) + 2.f;
				Y += TextWrappedLimited(D, TEXT("Копайте червей на грядках у дома и в мягкой земле у воды — раз в день на каждом месте."), FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 6);
			}
			else
			{
				D.Text(D.Ellipsize(FString::Printf(TEXT("Следующая: %s"), *Next->Name), W, TS), FVector2D(X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(TS) + 1.f;
				const FString Info = Cur > 0 && Sh.IsValidIndex(Cur - 1)
					? FString::Printf(TEXT("Время копания: %s с (сейчас %s с)."), *FVUI::Num(Next->DigSeconds, 1), *FVUI::Num(Sh[Cur - 1].DigSeconds, 1))
					: FString(TEXT("Лопата нужна, чтобы копать червей на грядках у дома и в мягкой земле у воды."));
				Y += TextWrappedLimited(D, Info, FVector2D(X, Y), W, SmallS(), InkInfo(), 6) + 3.f;
				if (bCraftable)
				{
					const float S = BodyS();
					const float RH = ReqRowH(D, S);
					const bool bTwo = !bP;
					const float ColW = bTwo ? FMath::FloorToFloat((W - 10.f) * 0.5f) : W;
					int32 k = 0;
					auto NextPos = [&](float& OX, float& OY) { const int32 C = bTwo ? k % 2 : 0; const int32 Rw = bTwo ? k / 2 : k; OX = X + float(C) * (ColW + 10.f); OY = Y + float(Rw) * RH; ++k; };
					float RX = 0.f, RY = 0.f;
					NextPos(RX, RY);
					CoinRow(D, RX, RY, ColW, P.Money, CraftPrice, S);
					for (const FFVIngredient& I : Next->Craft)
					{
						NextPos(RX, RY);
						ReqRow(D, RX, RY, ColW, I.ItemId, P.CountIngredient(I.ItemId), I.Count, S);
					}
					NextPos(RX, RY);
					CondRow(D, RX, RY, ColW, NeedStationText(EFVStation::Workshop, WL), P.GetStationLevel(EFVStation::Workshop) >= WL, S, EFVIcon::Hammer);
					if (Next->MinLevel > 1)
					{
						NextPos(RX, RY);
						CondRow(D, RX, RY, ColW, FString::Printf(TEXT("Уровень рыбалки %d"), Next->MinLevel), P.GetLevel() >= Next->MinLevel, S, EFVIcon::Level);
					}
					Y += float(bTwo ? FMath::DivideAndRoundUp(k, 2) : k) * RH;
				}
				else
				{
					const FString Where = TraderNames(Next->TraderMask);
					const FString Txt = Where.IsEmpty() ? FString(TEXT("Эту лопату нельзя сделать в мастерской."))
						: FString::Printf(TEXT("Эту лопату можно только купить: %s — %s."), *Where, *FVUI::Coins(Next->Price));
					Y += TextWrappedLimited(D, Txt, FVector2D(X, Y), W, BodyS(), FVPal::Ink(), 6);
				}
			}
			ShovelCol.End(D, Y, 6.f);
			if (!bP) { Button(FFVRect(Area.Right() - 8.f - 220.f, Area.Bottom() - 28.f, 220.f, 20.f)); }
		}

		// ---------------------------------------------------------------------------------------- tools (update 6)
		/** Next level of a tool ladder: why it cannot be made now (empty = can), the coins (half price, as the shovel). */
		static FString ToolBlock(const UFVSaveGame& P, const FFVToolDef* Next, bool bPickaxe, int32 Cur)
		{
			if (!Next) { return bPickaxe ? TEXT("Лучшая кирка") : TEXT("Лучший топор"); }
			if (bPickaxe && Cur <= 0 && !P.HasBlueprint(FName(TEXT("bp_pickaxe")))) { return TEXT("Нужен чертёж кирки"); }
			if (!bPickaxe && Cur <= 0) { return TEXT("Первый топор — у Боцмана"); }
			if (Next->bForge) { const int32 FL = FMath::Max(1, Next->WorkshopLevel); if (P.GetYardLevel(EFVYardBuilding::Forge) < FL) { return FL > 1 ? FString::Printf(TEXT("Нужна кузня %d-го уровня"), FL) : FString(TEXT("Нужна кузня во дворе")); } }
			else if (Next->WorkshopLevel <= 0) { return TEXT("Не делается в мастерской"); }
			else if (P.GetStationLevel(EFVStation::Workshop) < Next->WorkshopLevel) { return NeedStationText(EFVStation::Workshop, Next->WorkshopLevel); }
			if (P.GetLevel() < Next->MinLevel) { return FString::Printf(TEXT("Нужен уровень %d"), Next->MinLevel); }
			if (!P.CanAfford(FMath::Max(0, Next->Price / 2))) { return TEXT("Не хватает монет"); }
			if (!P.HasIngredients(Next->Craft, 1)) { return TEXT("Не хватает материалов"); }
			return FString();
		}

		/** One tool block (ladder + the next level's needs + its button). Returns the new Y. */
		float ToolSection(FVDraw& D, const UFVSaveGame& P, bool bPickaxe, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			const TArray<FFVToolDef>& Tools = bPickaxe ? FVDBU6::Pickaxes() : FVDBU6::Axes();
			const int32 Cur = FMath::Clamp(bPickaxe ? P.U6.PickaxeLevel : P.U6.AxeLevel, 0, Tools.Num());
			const FFVToolDef* Next = Tools.IsValidIndex(Cur) ? &Tools[Cur] : nullptr;
			const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
			D.Text(bPickaxe ? TEXT("Кирка") : TEXT("Топор"), FVector2D(X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS) + 3.f;
			const int32 N = Tools.Num();
			const int32 Cols = FMath::Max(1, bP ? FMath::Min(N, 2) : N);
			const float CardH = 44.f;
			for (int32 i = 0; i < N; ++i)
			{
				const FFVRect C = FVDraw::SplitH(FFVRect(X, Y + float(i / Cols) * (CardH + 5.f), W, CardH), i % Cols, Cols, 5.f);
				const bool bHave = i < Cur;
				const bool bNext = i == Cur;
				D.Rect(C, bNext ? FVPal::Gold() : BoxEdge());
				D.Rect(C.Inset(bNext ? 2.f : 1.f), bHave ? FV::Col(226, 240, 196) : (bNext ? FV::Col(255, 244, 214) : FV::Col(240, 214, 170)));
				const FFVRect IB(C.X + 5.f, C.Y + (C.H - 32.f) * 0.5f, 32.f, 32.f);
				if (UFVSprites* Sp = UFVSprites::Get())
				{
					const FFVSprite S = bPickaxe ? Sp->PickaxeIcon(i + 1) : Sp->AxeIcon(i + 1);
					FVUI::SpriteFit(D, S, IB, bHave || bNext ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.5f), 2.f);
				}
				const float TX = IB.Right() + 5.f;
				TextWrappedLimited(D, Tools[i].Name, FVector2D(TX, C.Y + 5.f), C.Right() - 4.f - TX, bP ? FV::Ui::Small() + 0.5f : 7.f, FVPal::Ink(), 2, true);
				const FString Where = Tools[i].bForge ? (Tools[i].WorkshopLevel > 1 ? FString::Printf(TEXT("кузня %d"), Tools[i].WorkshopLevel) : FString(TEXT("кузня"))) : (Tools[i].WorkshopLevel > 0 ? FString::Printf(TEXT("мастерская %d"), Tools[i].WorkshopLevel)
					: FString(TEXT("в лавке")));
				D.Text(D.Ellipsize(Where, C.Right() - 4.f - TX, SmallS(), false), FVector2D(TX, C.Bottom() - D.LineHeight(SmallS(), false) - 4.f), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				if (bHave && i == Cur - 1) { CheckBadge(D, FVector2D(C.Right() - 8.f, C.Y + 8.f), 5.f); }
			}
			Y += float(FMath::DivideAndRoundUp(FMath::Max(1, N), Cols)) * (CardH + 5.f) + 2.f;
			if (!Next)
			{
				Y += TextWrappedLimited(D, bPickaxe ? TEXT("У вас лучшая кирка: любая порода по зубам.") : TEXT("У вас лучший топор: рубит даже вековые ели."),
					FVector2D(X, Y), W, SmallS(), InkGood(), 3) + 4.f;
				return Y;
			}
			const FString Info = bPickaxe
				? FString(TEXT("Кирка колет валуны у пещеры и твёрдую породу — без неё не сделать экспертные снасти."))
				: FString(TEXT("Топор получше рубит более твёрдые деревья Леса."));
			Y += TextWrappedLimited(D, Info, FVector2D(X, Y), W, SmallS(), InkInfo(), 3) + 3.f;
			const bool bCanTry = !(bPickaxe && Cur <= 0 && !P.HasBlueprint(FName(TEXT("bp_pickaxe")))) && !(!bPickaxe && Cur <= 0);
			if (bCanTry)
			{
				const float S = BodyS();
				const float RH = ReqRowH(D, S);
				const bool bTwo = !bP;
				const float ColW = bTwo ? FMath::FloorToFloat((W - 10.f) * 0.5f) : W;
				int32 k = 0;
				auto NextPos = [&](float& OX, float& OY) { const int32 Cc = bTwo ? k % 2 : 0; const int32 Rw = bTwo ? k / 2 : k; OX = X + float(Cc) * (ColW + 10.f); OY = Y + float(Rw) * RH; ++k; };
				float RX = 0.f, RY = 0.f;
				NextPos(RX, RY);
				CoinRow(D, RX, RY, ColW, P.Money, FMath::Max(0, Next->Price / 2), S);
				for (const FFVIngredient& I : Next->Craft)
				{
					NextPos(RX, RY);
					ReqRow(D, RX, RY, ColW, I.ItemId, P.CountIngredient(I.ItemId), I.Count, S);
				}
				NextPos(RX, RY);
				if (Next->bForge) { const int32 FL = FMath::Max(1, Next->WorkshopLevel); CondRow(D, RX, RY, ColW, FL > 1 ? FString::Printf(TEXT("Кузня %d-го уровня"), FL) : FString(TEXT("Кузня во дворе")), P.GetYardLevel(EFVYardBuilding::Forge) >= FL, S, EFVIcon::Anvil); }
				else { CondRow(D, RX, RY, ColW, NeedStationText(EFVStation::Workshop, Next->WorkshopLevel), P.GetStationLevel(EFVStation::Workshop) >= Next->WorkshopLevel, S, EFVIcon::Hammer); }
				if (bPickaxe && Cur <= 0)
				{
					NextPos(RX, RY);
					CondRow(D, RX, RY, ColW, TEXT("Чертёж кирки"), P.HasBlueprint(FName(TEXT("bp_pickaxe"))), S, EFVIcon::Scroll);
				}
				if (Next->MinLevel > 1)
				{
					NextPos(RX, RY);
					CondRow(D, RX, RY, ColW, FString::Printf(TEXT("Уровень рыбалки %d"), Next->MinLevel), P.GetLevel() >= Next->MinLevel, S, EFVIcon::Level);
				}
				Y += float(bTwo ? FMath::DivideAndRoundUp(k, 2) : k) * RH + 3.f;
			}
			else
			{
				Y += TextWrappedLimited(D, bPickaxe ? TEXT("Чертёж кирки продаёт Снастёвщик в Городе.") : TEXT("Первый топор продаёт Боцман на причале."),
					FVector2D(X, Y), W, BodyS(), FVPal::Ink(), 3) + 3.f;
			}
			const FString Why = ToolBlock(P, Next, bPickaxe, Cur);
			const FFVRect Btn(X, Y, FMath::Min(W, 240.f), bP ? 30.f : 20.f);
			const float BTS = bP ? FV::Ui::Body() + 0.5f : 8.f;
			if (!Why.IsEmpty()) { D.Button(Btn, Why, EFVButton::Normal, false, BTS); }
			else if (CoinButton(D, Btn, bPickaxe ? TEXT("Сделать кирку") : TEXT("Сделать топор"), Next->Price / 2 > 0 ? Next->Price / 2 : -1, EFVButton::Primary, true, true, BTS))
			{
				Want(bPickaxe ? EAct::CraftPickaxe : EAct::CraftAxe);
			}
			Y += Btn.H + 8.f;
			return Y;
		}

		void TabTools(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			if (Foot)
			{
				// portrait footer: the pickaxe first (the key tool of update 6), else the axe
				const TArray<FFVToolDef>& Pk = FVDBU6::Pickaxes();
				const TArray<FFVToolDef>& Ax = FVDBU6::Axes();
				const FFVToolDef* NP = Pk.IsValidIndex(P.U6.PickaxeLevel) ? &Pk[P.U6.PickaxeLevel] : nullptr;
				const FFVToolDef* NA = Ax.IsValidIndex(P.U6.AxeLevel) ? &Ax[P.U6.AxeLevel] : nullptr;
				const FString WP = ToolBlock(P, NP, true, P.U6.PickaxeLevel);
				const FString WA = ToolBlock(P, NA, false, P.U6.AxeLevel);
				const float BTS = FV::Ui::Body() + 0.5f;
				if (WP.IsEmpty() && NP) { if (CoinButton(D, *Foot, TEXT("Сделать кирку"), NP->Price / 2 > 0 ? NP->Price / 2 : -1, EFVButton::Primary, true, true, BTS)) { Want(EAct::CraftPickaxe); } }
				else if (WA.IsEmpty() && NA) { if (CoinButton(D, *Foot, TEXT("Сделать топор"), NA->Price / 2 > 0 ? NA->Price / 2 : -1, EFVButton::Primary, true, true, BTS)) { Want(EAct::CraftAxe); } }
				else { D.Button(*Foot, WP, EFVButton::Normal, false, BTS); }
				return;
			}
			D.Panel(Area, EFVPanel::Parchment);
			float Y = ToolsCol.Begin(D, Area.Inset(2.f), 6.f);
			const float X = Area.X + 8.f, W = Area.W - 16.f - ToolsCol.BarRoom();
			Y = ToolSection(D, P, true, X, Y, W);
			D.Divider(FVector2D(X, Y - 3.f), W);
			Y += 4.f;
			Y = ToolSection(D, P, false, X, Y, W);
			ToolsCol.End(D, Y, 6.f);
		}

		// ---------------------------------------------------------------------------------------- smokehouse
		void TabSmoke(FVDraw& D, UFVSaveGame& P, const FFVRect& InArea, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			const int32 Cap = FMath::Max(1, P.StationCapacity(EFVStation::Smokehouse));
			const TArray<FFVSmokeSlot>& Slots = P.Smokehouse;
			int32 Ready = 0;
			float NeedFuel = 0.f;
			for (const FFVSmokeSlot& S : Slots)
			{
				if (S.MinutesLeft <= 0.f) { ++Ready; }
				else { NeedFuel = FMath::Max(NeedFuel, S.MinutesLeft); }
			}
			const float Fuel = FMath::Max(0.f, P.SmokeFuel);
			const float PerBranch = FMath::Max(1.f, P.FuelMinutesPerBranch());
			const int32 Branches = P.CountItem(FName(TEXT("res_branch")));
			const bool bCanFuel = Branches > 0 && Fuel < SmokeFuelMax - 0.5f;
			const bool bHasFish = HasEligibleFish(P, EPicker::Smoke);
			const bool bCanLoad = Slots.Num() < Cap && bHasFish;

			if (Foot)
			{
				if (D.Button(*Foot, Ready > 0 ? FString::Printf(TEXT("Забрать (%d)"), Ready) : FString(TEXT("Забрать")), EFVButton::Primary, Ready > 0, FV::Ui::Body() + 0.5f))
				{
					Want(EAct::CollectSmoked);
				}
				return;
			}
			// portrait: the two secondary buttons are part of the page (bottom row); the footer holds «Забрать»
			FFVRect Area = InArea;
			FFVRect BtnRow;
			if (bP)
			{
				BtnRow = FFVRect(Area.X, Area.Bottom() - FV::Ui::ButtonH(), Area.W, FV::Ui::ButtonH());
				Area.H = BtnRow.Y - 6.f - Area.Y;
			}

			// slots grid
			const int32 Cols = 4;
			const int32 Rows = FMath::DivideAndRoundUp(Cap, Cols);
			const FFVRect GridPanel = bP ? FFVRect(Area.X, Area.Y, Area.W, FMath::Min(Area.H * 0.58f, 22.f + float(Rows) * 80.f)) : FFVRect(Area.X, Area.Y, 372.f, Area.H);
			D.Panel(GridPanel, EFVPanel::Parchment);
			{
				const FFVRect In = GridPanel.Inset(6.f);
				const float HS = bP ? FV::Ui::Small() : 7.f;
				D.Text(FString::Printf(TEXT("Рыба в коптильне: %d / %d"), Slots.Num(), Cap), FVector2D(In.X, In.Y), HS, FVPal::Ink(), EFVAlign::Left, false, true);
				if (Ready > 0) { FVUI::StatusChip(D, FString::Printf(TEXT("Готово: %d"), Ready), FVector2D(In.Right(), In.Y - 1.f), FVUI::EChip::Good, false, bP ? 7.f : 6.f); }
				const float GY = In.Y + D.LineHeight(HS) + 4.f;
				const float CellH = FMath::Min(bP ? 76.f : 70.f, FMath::FloorToFloat((In.Bottom() - GY - float(Rows - 1) * 5.f) / float(Rows)));
				for (int32 i = 0; i < Cap; ++i)
				{
					const FFVRect C = FVDraw::SplitH(FFVRect(In.X, GY + float(i / Cols) * (CellH + 5.f), In.W, CellH), i % Cols, Cols, 5.f);
					DrawSmokeCell(D, C, Slots.IsValidIndex(i) ? &Slots[i] : nullptr, Fuel);
				}
			}

			// fuel card
			const FFVRect FuelPanel = bP ? FFVRect(Area.X, GridPanel.Bottom() + 6.f, Area.W, Area.Bottom() - GridPanel.Bottom() - 6.f)
				: FFVRect(GridPanel.Right() + 6.f, Area.Y, Area.Right() - GridPanel.Right() - 6.f, Area.H);
			D.Panel(FuelPanel, EFVPanel::Parchment);
			{
				const FFVRect In = FuelPanel.Inset(7.f);
				float Y = In.Y;
				const float TS = bP ? FV::Ui::Body() : 8.f;
				// fire + smoke animation
				const FFVRect FireBox(In.X, Y, 30.f, 30.f);
				IconBox(D, FireBox);
				const bool bBurning = Fuel > 0.f && NeedFuel > 0.f;
				if (bBurning)
				{
					const float T = float(D.Time);
					D.Glow(FireBox.Center() + FVector2D(0.f, 5.f), 12.f + 2.f * FMath::Sin(T * 9.f), FLinearColor(1.f, 0.55f, 0.15f, 0.55f));
					for (int32 k = 0; k < 3; ++k)
					{
						const float Ph = FMath::Fmod(T * 0.45f + float(k) / 3.f, 1.f);
						D.Circle(FireBox.Center() + FVector2D(FMath::Sin(T * 1.7f + k) * 3.f, 4.f - Ph * 16.f), 2.5f + Ph * 3.f, FLinearColor(0.55f, 0.52f, 0.5f, 0.5f * (1.f - Ph)));
					}
				}
				DrawIcon(D, EFVIcon::Smoke, FireBox.Inset(4.f), 1.f, bBurning ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.55f));
				const float TX = FireBox.Right() + 6.f;
				D.Text(TEXT("Топливо"), FVector2D(TX, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
				const FString FuelText = Fuel > 0.5f ? FString::Printf(TEXT("Топлива на %s"), *UFVGame::DurationText(Fuel)) : FString(TEXT("Нет топлива — копчение стоит"));
				D.Text(D.Ellipsize(FuelText, In.Right() - TX, SmallS(), false), FVector2D(TX, Y + D.LineHeight(TS)), SmallS(), Fuel > 0.5f ? FVPal::InkSoft() : InkBad(), EFVAlign::Left, false, false);
				Y = FireBox.Bottom() + 5.f;
				const float BarH = bP ? 10.f : 8.f;
				D.Bar(FFVRect(In.X, Y, In.W, BarH), FMath::Clamp(Fuel / SmokeFuelMax, 0.f, 1.f), FV::Col(236, 120, 40));
				Y += BarH + 5.f;
				D.Text(D.Ellipsize(FString::Printf(TEXT("Ветки в рюкзаке: %d · одна = %s"), Branches, *UFVGame::DurationText(PerBranch)), In.W, SmallS(), false),
					FVector2D(In.X, Y), SmallS(), FVPal::Ink(), EFVAlign::Left, false, false);
				Y += D.LineHeight(SmallS(), false) + 1.f;
				FString Note;
				FLinearColor NoteC = FVPal::InkSoft();
				if (NeedFuel > 0.f && Fuel + 0.5f < NeedFuel)
				{
					Note = FString::Printf(TEXT("Не хватит: нужно ещё %s"), *UFVGame::DurationText(NeedFuel - Fuel));
					NoteC = InkBad();
				}
				else if (NeedFuel > 0.f) { Note = FString::Printf(TEXT("Всё будет готово через %s"), *UFVGame::DurationText(NeedFuel)); NoteC = InkGood(); }
				else if (Slots.Num() == 0) { Note = TEXT("Коптится 6 ч, копчёная рыба стоит вдвое дороже"); }
				else { Note = TEXT("Заберите готовую рыбу в садок"); NoteC = InkGood(); }
				TextWrappedLimited(D, Note, FVector2D(In.X, Y), In.W, SmallS(), NoteC, 2);

				if (!bP)
				{
					// landscape buttons at the bottom of the card
					const float BW = In.W;
					float BY = In.Bottom() - 20.f;
					if (D.Button(FFVRect(In.X, BY, BW, 20.f), Ready > 0 ? FString::Printf(TEXT("Забрать готовое (%d)"), Ready) : FString(TEXT("Забрать готовое")), EFVButton::Primary, Ready > 0, 8.f))
					{
						Want(EAct::CollectSmoked);
					}
					BY -= 24.f;
					if (D.Button(FFVRect(In.X, BY, BW, 20.f), Slots.Num() >= Cap ? FString(TEXT("Все места заняты")) : (bHasFish ? FString(TEXT("Загрузить рыбу")) : FString(TEXT("Нет рыбы в садке"))), EFVButton::Normal, bCanLoad, 8.f))
					{
						Want(EAct::OpenPicker, NAME_None, int32(EPicker::Smoke));
					}
					BY -= 24.f;
					if (D.Button(FFVRect(In.X, BY, BW, 20.f), Branches > 0 ? TEXT("Подбросить дрова (ветка)") : TEXT("Нет веток в рюкзаке"), EFVButton::Normal, bCanFuel, 8.f))
					{
						Want(EAct::AddFuel);
					}
				}
			}
			if (!bP) { return; }
			if (D.Button(FVDraw::SplitH(BtnRow, 0, 2, 8.f), Branches > 0 ? FString(TEXT("Подбросить дрова")) : FString(TEXT("Нет веток")), EFVButton::Normal, bCanFuel, FV::Ui::Body()))
			{
				Want(EAct::AddFuel);
			}
			if (D.Button(FVDraw::SplitH(BtnRow, 1, 2, 8.f), Slots.Num() >= Cap ? FString(TEXT("Мест нет")) : FString(TEXT("Загрузить рыбу")), EFVButton::Normal, bCanLoad, FV::Ui::Body()))
			{
				Want(EAct::OpenPicker, NAME_None, int32(EPicker::Smoke));
			}
		}

		void DrawSmokeCell(FVDraw& D, const FFVRect& C, const FFVSmokeSlot* S, float Fuel)
		{
			const bool bP = FV::IsPortrait();
			if (!S)
			{
				D.Rect(C, FV::Col(176, 110, 56, 110));
				D.Rect(C.Inset(1.f), FV::Col(255, 226, 170, 120));
				D.Text(TEXT("пусто"), FVector2D(C.Center().X, C.Center().Y - 5.f), SmallS(), FV::Col(176, 130, 90), EFVAlign::Center, false, false);
				return;
			}
			const bool bDone = S->MinutesLeft <= 0.f;
			D.Rect(C, bDone ? FV::Col(96, 150, 56) : BoxEdge());
			D.Rect(C.Inset(1.f), bDone ? FV::Col(226, 240, 196) : BoxFill());
			const float IS = FMath::Min(C.W - 8.f, bP ? 30.f : 26.f);
			const FFVRect IB(C.Center().X - IS * 0.5f, C.Y + 3.f, IS, IS * 0.75f);
			DrawItemIcon(D, S->FishId, IB, 2.f, bDone ? FLinearColor(0.85f, 0.7f, 0.55f, 1.f) : FLinearColor::White);
			const EFVQuality Q = QualityOf(S->Quality);
			if (Q != EFVQuality::Normal) { D.Stars(FVector2D(C.X + 2.f, C.Y + 2.f), Q, 1.f); }
			const float NS = bP ? 7.f : 6.f;
			float Y = IB.Bottom() + 1.f;
			D.Text(D.Ellipsize(FVDB::ItemName(S->FishId), C.W - 4.f, NS, false), FVector2D(C.Center().X, Y), NS, FVPal::Ink(), EFVAlign::Center, false, false);
			Y += D.LineHeight(NS, false);
			const float Frac = FMath::Clamp(1.f - S->MinutesLeft / SmokeMinutes, 0.f, 1.f);
			D.Bar(FFVRect(C.X + 4.f, Y + 1.f, C.W - 8.f, 5.f), Frac, bDone ? FVPal::Green() : FV::Col(206, 128, 56));
			Y += 8.f;
			const FString T = bDone ? FString(TEXT("Готово!")) : (Fuel > 0.f ? UFVGame::DurationText(S->MinutesLeft) : FString(TEXT("ждёт дров")));
			if (Y + D.LineHeight(NS, false) <= C.Bottom() + 1.f)
			{
				D.Text(D.Ellipsize(T, C.W - 4.f, NS, true), FVector2D(C.Center().X, Y), NS, bDone ? InkGood() : (Fuel > 0.f ? FVPal::InkSoft() : InkBad()), EFVAlign::Center, false, true);
			}
		}

		// ---------------------------------------------------------------------------------------- worm farm
		void TabWorms(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			const int32 Lv = P.GetStationLevel(EFVStation::WormFarm);
			const int32 Cap = P.StationCapacity(EFVStation::WormFarm);
			const int32 Worms = FMath::Max(0, P.WormFarmWorms);
			const int32 Crawlers = FMath::Max(0, P.WormFarmCrawlers);
			const int32 Total = Worms + Crawlers;
			const FString Label = Total > 0 ? FString::Printf(TEXT("Собрать (%d)"), Total) : FString(TEXT("Пока нечего собрать"));
			if (Foot)
			{
				if (D.Button(*Foot, Label, EFVButton::Primary, Total > 0, FV::Ui::Body() + 0.5f)) { Want(EAct::CollectWorms); }
				return;
			}
			const FFVRect Box = bP ? FFVRect(Area.X, Area.Y, Area.W, FMath::Clamp(FMath::RoundToFloat(Area.H * 0.42f), 110.f, 190.f)) : FFVRect(Area.X, Area.Y, 300.f, Area.H);
			DrawWormBox(D, Box, Worms, Crawlers);
			const FFVRect Info = bP ? FFVRect(Area.X, Box.Bottom() + 6.f, Area.W, Area.Bottom() - Box.Bottom() - 6.f) : FFVRect(Box.Right() + 6.f, Area.Y, Area.Right() - Box.Right() - 6.f, Area.H);
			D.Panel(Info, EFVPanel::Parchment);
			const FFVRect ColR = bP ? Info.Inset(2.f) : FFVRect(Info.X + 2.f, Info.Y + 2.f, Info.W - 4.f, Info.H - 32.f);
			float Y = WormsCol.Begin(D, ColR, 6.f);
			const FFVRect In(Info.X + 8.f, Y, Info.W - 16.f - WormsCol.BarRoom(), Info.H - 16.f);
			const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
			const float S = BodyS();
			const float Step = D.LineHeight(S, false) + 2.f;
			D.Text(FString::Printf(TEXT("Урожай: %d / %d"), Total, Cap), FVector2D(In.X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS) + 2.f;
			D.Bar(FFVRect(In.X, Y, In.W, bP ? 10.f : 7.f), Cap > 0 ? FMath::Clamp(float(Total) / float(Cap), 0.f, 1.f) : 0.f, FV::Col(206, 120, 130));
			Y += (bP ? 10.f : 7.f) + 6.f;
			const float IS = bP ? 16.f : 12.f;
			DrawItemIcon(D, FName(TEXT("bait_worm")), FFVRect(In.X, Y, IS, IS), 1.f);
			D.Text(FString::Printf(TEXT("Червяки: %d"), Worms), FVector2D(In.X + IS + 4.f, Y + 1.f), S, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += FMath::Max(IS, Step) + 2.f;
			DrawItemIcon(D, FName(TEXT("bait_nightcrawler")), FFVRect(In.X, Y, IS, IS), 1.f);
			D.Text(FString::Printf(TEXT("Выползки: %d"), Crawlers), FVector2D(In.X + IS + 4.f, Y + 1.f), S, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += FMath::Max(IS, Step) + 4.f;
			const FString Prod = Lv >= 2 ? FString(TEXT("Каждое утро: +3–4 червя, иногда выползок.")) : FString(TEXT("Каждое утро: +2–3 червя."));
			Y += TextWrappedLimited(D, Prod, FVector2D(In.X, Y), In.W, SmallS(), InkInfo(), 6) + 1.f;
			const FString Today = P.WormFarmDay == P.Day ? FString(TEXT("Сегодня червятник уже потрудился — новые черви появятся утром."))
				: FString(TEXT("Новые черви появятся утром."));
			Y += TextWrappedLimited(D, Today, FVector2D(In.X, Y), In.W, SmallS(), FVPal::InkSoft(), 6) + 3.f;
			const FString Have = FString::Printf(TEXT("В наживке: червяки ×%d, выползки ×%d"), P.BaitCount(FName(TEXT("bait_worm"))), P.BaitCount(FName(TEXT("bait_nightcrawler"))));
			Y += TextWrappedLimited(D, Have, FVector2D(In.X, Y), In.W, SmallS(), FVPal::Ink(), 6);
			// update 6 (§9б): «Глубинный червятник» (level 3) — the expert bait
			const TArray<FFVStationDef>& Defs = FVDB::Stations();
			const FFVStationDef* WDef = Defs.IsValidIndex(int32(EFVStation::WormFarm)) ? &Defs[int32(EFVStation::WormFarm)] : nullptr;
			if (WDef && WDef->Levels.Num() >= 3)
			{
				Y += 5.f;
				static const FName Deep(TEXT("bait_deepworm"));
				DrawItemIcon(D, Deep, FFVRect(In.X, Y, IS, IS), 1.f, Lv >= 3 ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f));
				const FString DL = Lv >= 3 ? FString::Printf(TEXT("Глубинные черви в наживке: %d"), P.BaitCount(Deep))
					: FString::Printf(TEXT("Глубинные черви — с уровня 3 «%s»"), *WDef->Levels[2].Name);
				Y += TextWrappedLimited(D, DL, FVector2D(In.X + IS + 4.f, Y + 1.f), In.W - IS - 4.f, SmallS(), Lv >= 3 ? FVPal::Ink() : FVPal::InkSoft(), 3) + 2.f;
				if (Lv >= 3 && !WDef->Levels[2].Description.IsEmpty())
				{
					Y += TextWrappedLimited(D, WDef->Levels[2].Description, FVector2D(In.X, Y), In.W, SmallS(), InkInfo(), 4);
				}
			}
			WormsCol.End(D, Y, 6.f);
			if (!bP)
			{
				if (D.Button(FFVRect(Info.X + 8.f, Info.Bottom() - 28.f, Info.W - 16.f, 20.f), Label, EFVButton::Primary, Total > 0, 8.f)) { Want(EAct::CollectWorms); }
			}
		}

		void DrawWormBox(FVDraw& D, const FFVRect& R, int32 Worms, int32 Crawlers)
		{
			// wooden crate with soil
			D.Rect(R, FV::Col(92, 52, 26));
			D.Rect(R.Inset(2.f), FV::Col(150, 92, 48));
			const FFVRect Soil = R.Inset(8.f);
			D.GradientV(Soil, FV::Col(110, 72, 44), FV::Col(70, 44, 28), 8);
			for (int32 i = 0; i < 90; ++i)
			{
				const float X = Soil.X + FVUI::Hash01(i, 3, 77) * (Soil.W - 2.f);
				const float Y = Soil.Y + FVUI::Hash01(i, 9, 77) * (Soil.H - 2.f);
				D.Rect(FFVRect(FMath::RoundToFloat(X), FMath::RoundToFloat(Y), 2.f, 1.f), FVUI::Hash01(i, 1, 5) > 0.5f ? FV::Col(132, 92, 58) : FV::Col(58, 36, 22));
			}
			// planks
			for (int32 k = 1; k < 4; ++k) { D.Rect(FFVRect(R.X + 2.f, R.Y + R.H * float(k) / 4.f, 6.f, 1.f), FV::Col(92, 52, 26)); }
			const float T = float(D.Time);
			const float Sc = FV::IsPortrait() ? 1.25f : 1.f;
			const int32 NW = FMath::Min(Worms, 24);
			const int32 NC = FMath::Min(Crawlers, 8);
			for (int32 i = 0; i < NW + NC; ++i)
			{
				const bool bCrawler = i >= NW;
				const int32 Segs = bCrawler ? 8 : 6;
				const float Rad = (bCrawler ? 2.4f : 1.8f) * Sc;
				const FLinearColor Col = bCrawler ? FV::Col(178, 84, 96) : FV::Col(232, 140, 150);
				const FLinearColor Dark = bCrawler ? FV::Col(120, 50, 60) : FV::Col(180, 90, 100);
				const float BX = Soil.X + 10.f + FVUI::Hash01(i, 11, 31) * (Soil.W - 20.f - float(Segs) * 3.f * Sc);
				const float BY = Soil.Y + 8.f + FVUI::Hash01(i, 17, 31) * (Soil.H - 16.f);
				const float Dir = FVUI::Hash01(i, 5, 2) > 0.5f ? 1.f : -1.f;
				const float Crawl = FMath::Sin(T * (0.6f + FVUI::Hash01(i, 2, 9)) + float(i)) * 3.f;
				D.Circle(FVector2D(BX + (Dir > 0.f ? -2.f : float(Segs) * 3.f * Sc + 2.f), BY + 1.f), Rad + 1.f, FV::Col(40, 24, 14, 200));   // hole
				for (int32 s = 0; s < Segs; ++s)
				{
					const float SX = BX + Crawl + Dir * float(s) * 3.f * Sc + (Dir < 0.f ? float(Segs) * 3.f * Sc : 0.f);
					const float SY = BY + FMath::Sin(T * 4.f + float(s) * 0.9f + float(i) * 1.3f) * 1.5f * Sc;
					D.Circle(FVector2D(SX, SY + 0.6f), Rad, Dark);
					D.Circle(FVector2D(SX, SY), Rad, Col);
				}
			}
			if (NW + NC == 0)
			{
				D.TextOutlined(TEXT("Пока пусто — черви появятся утром"), FVector2D(Soil.Center().X, Soil.Center().Y - 5.f), SmallS(), FV::Col(255, 236, 200), EFVAlign::Center);
			}
		}

		// ---------------------------------------------------------------------------------------- aquarium
		void TabAquarium(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			const int32 Cap = FMath::Max(1, P.StationCapacity(EFVStation::Aquarium));
			const TArray<FFVInvStack>& Fish = P.Aquarium;
			AquaSel = Fish.Num() > 0 ? FMath::Clamp(AquaSel, 0, Fish.Num() - 1) : -1;
			const bool bCanPut = Fish.Num() < Cap && HasEligibleFish(P, EPicker::Aquarium);
			const bool bCanTake = Fish.IsValidIndex(AquaSel);
			const FString PutLabel = Fish.Num() >= Cap ? FString(TEXT("Нет места")) : FString(TEXT("Запустить рыбу"));
			if (Foot)
			{
				if (D.Button(FVDraw::SplitH(*Foot, 0, 2, 6.f), PutLabel, EFVButton::Primary, bCanPut, FV::Ui::Body())) { Want(EAct::OpenPicker, NAME_None, int32(EPicker::Aquarium)); }
				if (D.Button(FVDraw::SplitH(*Foot, 1, 2, 6.f), TEXT("В садок"), EFVButton::Normal, bCanTake, FV::Ui::Body())) { Want(EAct::AquaTake, NAME_None, AquaSel); }
				return;
			}
			const FFVRect TankArea = bP ? FFVRect(Area.X, Area.Y, Area.W, FMath::Clamp(FMath::RoundToFloat(170.f + (FV::VH - 640.f) * 0.3f), 150.f, 240.f))
				: FFVRect(Area.X, Area.Y, 372.f, Area.H);
			DrawTank(D, P, TankArea);
			const FFVRect ListP = bP ? FFVRect(Area.X, TankArea.Bottom() + 6.f, Area.W, Area.Bottom() - TankArea.Bottom() - 6.f)
				: FFVRect(TankArea.Right() + 6.f, Area.Y, Area.Right() - TankArea.Right() - 6.f, Area.H);
			D.Panel(ListP, EFVPanel::Parchment);
			const FFVRect In = ListP.Inset(6.f);
			const float HS = bP ? FV::Ui::Small() : 7.f;
			D.Text(FString::Printf(TEXT("Рыб в аквариуме: %d / %d"), Fish.Num(), Cap), FVector2D(In.X, In.Y), HS, FVPal::Ink(), EFVAlign::Left, false, true);
			const float LTop = In.Y + D.LineHeight(HS) + 3.f;
			const FFVRect ListA(In.X, LTop, In.W, (bP ? In.Bottom() : In.Bottom() - 48.f) - LTop);
			if (Fish.Num() == 0)
			{
				EmptyState(D, ListA, EFVIcon::Aquarium, TEXT("Пусто. Запустите рыбу из садка — она будет жить здесь."), SmallS());
			}
			else
			{
				const float RowH = bP ? 36.f : 24.f;
				const float Pitch = RowH + 2.f;
				AquaList.Update(D, ListA, float(Fish.Num()) * Pitch - 2.f);
				const float RW = ListA.W - (AquaList.CanScroll() ? 7.f : 0.f);
				D.PushClip(ListA);
				for (int32 i = 0; i < Fish.Num(); ++i)
				{
					const FFVRect Row(ListA.X, ListA.Y - AquaList.Offset() + float(i) * Pitch, RW, RowH);
					if (!RowVisible(ListA, Row.Y, RowH)) { continue; }
					const FFVInvStack& S = Fish[i];
					RowBack(D, Row, i == AquaSel, !bP && D.Hover(Row), PressingIn(D, Row));
					if (D.Clicked(Row) && i != AquaSel) { AquaSel = i; FVUI::Sfx(EFVSfx::Click); }
					DrawItemIcon(D, S.ItemId, FFVRect(Row.X + 3.f, Row.Y + 3.f, RowH * 1.3f, RowH - 6.f), 2.f);
					const float TX = Row.X + RowH * 1.3f + 8.f;
					const FFVFishDef* F = FVDB::FindFish(S.ItemId);
					const float NS = bP ? FV::Ui::Body() : 7.5f;
					D.Text(D.Ellipsize(FVDB::ItemName(S.ItemId), Row.Right() - 20.f - TX, NS), FVector2D(TX, Row.Y + (bP ? 4.f : 2.f)), NS, F ? FVUI::RarityInk(F->Rarity) : FVPal::Ink(), EFVAlign::Left, false, true);
					D.Text(F ? FVDB::RarityName(F->Rarity) : FString(), FVector2D(TX, Row.Y + (bP ? 20.f : 13.f)), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
					const EFVQuality Q = QualityOf(S.Quality);
					if (Q != EFVQuality::Normal) { D.Stars(FVector2D(Row.Right() - 12.f, Row.Y + (RowH - 8.f) * 0.5f), Q, 1.f); }
				}
				EndList(D, AquaList, ListA);
			}
			if (bP) { return; }
			if (D.Button(FFVRect(In.X, In.Bottom() - 44.f, In.W, 20.f), PutLabel, EFVButton::Primary, bCanPut, 8.f)) { Want(EAct::OpenPicker, NAME_None, int32(EPicker::Aquarium)); }
			if (D.Button(FFVRect(In.X, In.Bottom() - 20.f, In.W, 20.f), bCanTake ? FString::Printf(TEXT("В садок: %s"), *FVDB::ItemName(Fish[AquaSel].ItemId)) : FString(TEXT("В садок")), EFVButton::Normal, bCanTake, 8.f))
			{
				Want(EAct::AquaTake, NAME_None, AquaSel);
			}
		}

		void DrawTank(FVDraw& D, UFVSaveGame& P, const FFVRect& Area)
		{
			const int32 Lv = FMath::Max(1, P.GetStationLevel(EFVStation::Aquarium));
			const float WFrac = Lv <= 1 ? 0.62f : (Lv == 2 ? 0.86f : 1.f);
			const float TW = FMath::RoundToFloat(Area.W * WFrac);
			const FFVRect R(FMath::RoundToFloat(Area.Center().X - TW * 0.5f), Area.Y, TW, Area.H);
			// stand / frame
			const bool bOcean = Lv >= 4;   // update 6: «Океанариум» — brass frame, deep sea water
			D.Rect(R, bOcean ? FV::Col(96, 66, 20) : FV::Col(70, 40, 22));
			D.Rect(R.Inset(2.f), bOcean ? FV::Col(206, 160, 64) : FV::Col(122, 72, 38));
			if (bOcean) { D.Rect(FFVRect(R.X + 2.f, R.Y + 2.f, R.W - 4.f, 1.f), FV::Col(250, 220, 130)); }
			const FFVRect Glass = R.Inset(6.f);
			const float T = float(D.Time);
			if (bOcean) { D.GradientV(Glass, FV::Col(70, 170, 200), FV::Col(12, 44, 96), 16); }
			else { D.GradientV(Glass, FV::Col(128, 206, 236), FV::Col(34, 102, 164), 14); }
			// light caustics
			for (int32 k = 0; k < 5; ++k)
			{
				const float X0 = Glass.X + Glass.W * (0.1f + 0.2f * float(k)) + FMath::Sin(T * 0.7f + k) * 6.f;
				D.Line(FVector2D(X0, Glass.Y), FVector2D(X0 - 18.f, Glass.Bottom() - 14.f), FLinearColor(1.f, 1.f, 1.f, 0.06f), 5.f);
			}
			// sand + pebbles
			const float SandH = FMath::RoundToFloat(Glass.H * 0.14f);
			const FFVRect Sand(Glass.X, Glass.Bottom() - SandH, Glass.W, SandH);
			D.Rect(Sand, FV::Col(226, 196, 130));
			D.Rect(FFVRect(Sand.X, Sand.Y, Sand.W, 1.f), FV::Col(246, 222, 160));
			for (int32 k = 0; k < 14; ++k)
			{
				const float PX = Sand.X + 4.f + FVUI::Hash01(k, 1, 44) * (Sand.W - 8.f);
				const float PY = Sand.Y + 3.f + FVUI::Hash01(k, 2, 44) * (Sand.H - 5.f);
				D.Circle(FVector2D(PX, PY), 1.5f + FVUI::Hash01(k, 3, 44) * 1.5f, FVUI::Hash01(k, 4, 44) > 0.5f ? FV::Col(150, 140, 130) : FV::Col(190, 150, 110));
			}
			// seaweed
			const int32 Weeds = Lv + 2;
			for (int32 k = 0; k < Weeds; ++k)
			{
				const float BX = Glass.X + Glass.W * (0.08f + 0.84f * FVUI::Hash01(k, 7, 12));
				const float H = Glass.H * (0.3f + 0.3f * FVUI::Hash01(k, 8, 12));
				FVector2D Prev(BX, Sand.Y + 2.f);
				const int32 Segs = 6;
				for (int32 s = 1; s <= Segs; ++s)
				{
					const float F = float(s) / float(Segs);
					const FVector2D Pt(BX + FMath::Sin(T * 1.2f + float(k) + F * 2.5f) * 4.f * F, Sand.Y + 2.f - H * F);
					D.Line(Prev, Pt, k % 2 ? FV::Col(52, 140, 70) : FV::Col(80, 170, 60), 3.f);
					Prev = Pt;
				}
			}
			// fish
			UFVSprites* Sp = UFVSprites::Get();
			const TArray<FFVInvStack>& Fish = P.Aquarium;
			for (int32 i = 0; i < Fish.Num(); ++i)
			{
				const FFVFishDef* F = FVDB::FindFish(Fish[i].ItemId);
				if (!F || !Sp) { continue; }
				const FFVSprite S = Sp->Fish(*F);
				if (!S.IsValid()) { continue; }
				const float Sc = (S.W * 2.f <= Glass.W * 0.28f && S.H * 2.f <= Glass.H * 0.3f) ? 2.f : 1.f;
				const float Speed = 0.25f + 0.25f * FVUI::Hash01(i, 21, 3);
				const float Ph = FVUI::Hash01(i, 22, 3) * 6.28f;
				const float Margin = S.W * Sc * 0.5f + 4.f;
				const float U = FMath::Sin(T * Speed + Ph);
				const float CX = Glass.X + Margin + (Glass.W - 2.f * Margin) * (0.5f + 0.5f * U);
				const bool bRight = FMath::Cos(T * Speed + Ph) > 0.f;
				const float CY = Glass.Y + 8.f + S.H * Sc * 0.5f + (Glass.H - SandH - 16.f - S.H * Sc) * FVUI::Hash01(i, 23, 3) + FMath::Sin(T * 1.3f + Ph) * 3.f;
				const FFVRect FishR(CX - S.W * Sc * 0.5f - 3.f, CY - S.H * Sc * 0.5f - 3.f, S.W * Sc + 6.f, S.H * Sc + 6.f);
				if (i == AquaSel) { D.Ring(FVector2D(CX, CY), S.W * Sc * 0.5f + 5.f, S.H * Sc * 0.5f + 5.f, FLinearColor(1.f, 1.f, 0.7f, 0.7f), 1.f); }
				D.SpriteCentered(S, FVector2D(FMath::RoundToFloat(CX), FMath::RoundToFloat(CY)), Sc, FLinearColor::White, !bRight);
				if (D.Clicked(FishR) && i != AquaSel) { AquaSel = i; FVUI::Sfx(EFVSfx::Click); }
			}
			// bubbles
			for (int32 k = 0; k < 8; ++k)
			{
				const float Speed = 14.f + 10.f * FVUI::Hash01(k, 31, 9);
				const float Y = Glass.Bottom() - SandH - FMath::Fmod(T * Speed + FVUI::Hash01(k, 32, 9) * 200.f, Glass.H - SandH);
				const float X = Glass.X + 6.f + FVUI::Hash01(k, 33, 9) * (Glass.W - 12.f) + FMath::Sin(T * 2.f + k) * 2.f;
				D.Ring(FVector2D(X, Y), 1.5f + FVUI::Hash01(k, 34, 9), 1.5f + FVUI::Hash01(k, 34, 9), FLinearColor(1.f, 1.f, 1.f, 0.55f), 1.f);
			}
			// glass shine + water line
			D.Rect(FFVRect(Glass.X + 3.f, Glass.Y + 4.f, 2.f, Glass.H * 0.5f), FLinearColor(1.f, 1.f, 1.f, 0.18f));
			D.Rect(FFVRect(Glass.X, Glass.Y + 3.f, Glass.W, 1.f), FLinearColor(1.f, 1.f, 1.f, 0.3f));
			if (Fish.Num() == 0)
			{
				D.TextOutlined(TEXT("Аквариум пуст"), FVector2D(Glass.Center().X, Glass.Center().Y - 8.f), FV::IsPortrait() ? FV::Ui::Body() : 8.f, FV::Col(236, 248, 255), EFVAlign::Center, FV::Col(20, 50, 80));
			}
		}

		// ---------------------------------------------------------------------------------------- ice cellar
		void TabCellar(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			if (Foot)
			{
				if (D.Button(*Foot, TEXT("Открыть ледник →"), EFVButton::Primary, true, FV::Ui::Body() + 0.5f)) { Want(EAct::OpenCellar); }
				return;
			}
			const int32 Cap = P.ContainerCapacity(EFVContainer::IceCellar);
			const int32 Used = P.ContainerUsed(EFVContainer::IceCellar);
			const TArray<FFVInvStack>& Stacks = P.IceCellar;
			D.Panel(Area, EFVPanel::Parchment);
			const FFVRect In = Area.Inset(8.f);
			float Y = In.Y;
			DrawIcon(D, EFVIcon::Snowflake, FFVRect(In.X, Y, 20.f, 20.f), 1.f);
			const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
			D.Text(FString::Printf(TEXT("В леднике: %d / %d"), Used, Cap), FVector2D(In.X + 26.f, Y + 2.f), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += 24.f;
			D.Bar(FFVRect(In.X, Y, In.W, bP ? 10.f : 7.f), Cap > 0 ? FMath::Clamp(float(Used) / float(Cap), 0.f, 1.f) : 0.f, FV::Col(110, 190, 236));
			Y += (bP ? 10.f : 7.f) + 5.f;
			Y += TextWrappedLimited(D, TEXT("Рыба во льду не портится и не занимает место в садке. Перекладывайте её в ледник и обратно, а продавайте — из садка."), FVector2D(In.X, Y), In.W, SmallS(), FVPal::InkSoft(), 3) + 5.f;
			const FFVRect GridA(In.X, Y, In.W, (bP ? In.Bottom() : In.Bottom() - 26.f) - Y);
			if (Stacks.Num() == 0)
			{
				EmptyState(D, GridA, EFVIcon::Snowflake, TEXT("Ледник пуст."), SmallS());
			}
			else
			{
				// every stack, smoothly scrolling (a preview: moving fish happens in «Открыть ледник»)
				const float Cell = bP ? 44.f : 28.f;
				const float Pitch = Cell + 3.f;
				const int32 Cols = FMath::Max(1, FMath::FloorToInt((GridA.W - 6.f + 3.f) / Pitch));
				const int32 Rows = FMath::DivideAndRoundUp(Stacks.Num(), Cols);
				CellarGrid.Update(D, GridA, float(Rows) * Pitch - 3.f);
				D.PushClip(GridA);
				for (int32 i = 0; i < Stacks.Num(); ++i)
				{
					const FFVRect C(In.X + float(i % Cols) * Pitch, GridA.Y - CellarGrid.Offset() + float(i / Cols) * Pitch, Cell, Cell);
					if (!RowVisible(GridA, C.Y, Cell)) { continue; }
					D.Rect(C, FV::Col(150, 196, 220));
					D.Rect(C.Inset(1.f), FV::Col(222, 240, 250));
					StackCell(D, C, Stacks[i].ItemId, Stacks[i].Quality, Stacks[i].Flags, Stacks[i].Count);
					if (!bP && D.Hover(C))
					{
						const EFVQuality Q = QualityOf(Stacks[i].Quality);
						D.Tooltip(FVDB::StackName(Stacks[i].ItemId, Stacks[i].Flags), Q != EFVQuality::Normal ? FString::Printf(TEXT("%s · ×%d"), *FVDB::QualityName(Q), Stacks[i].Count) : FString::Printf(TEXT("×%d"), Stacks[i].Count));
					}
				}
				EndList(D, CellarGrid, GridA);
			}
			if (!bP)
			{
				if (D.Button(FFVRect(In.Right() - 200.f, In.Bottom() - 20.f, 200.f, 20.f), TEXT("Открыть ледник →"), EFVButton::Primary, true, 8.f)) { Want(EAct::OpenCellar); }
			}
		}

		// ---------------------------------------------------------------------------------------- radio
		void TabRadio(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			if (Foot)
			{
				if (D.Button(*Foot, TEXT("Покрутить ручку"), EFVButton::Normal, true, FV::Ui::Body() + 0.5f)) { Want(EAct::RadioTune); }
				return;
			}
			const EFVWeather Today = Game ? Game->GetWeather() : EFVWeather::Sunny;
			const EFVWeather Tomorrow = Game ? Game->GetWeatherTomorrow() : EFVWeather::Sunny;
			// fish that depend on weather / night at unlocked locations
			TArray<FString> RainFish, SunFish, NightFish;
			for (const FFVFishDef& F : FVDB::Fish())
			{
				if (!P.IsLocationUnlocked(F.Location) || F.bExpert) { continue; }   // expert fish: only by learnt facts (Р33, below)
				const FFVLocationDef* L = FVDB::FindLocation(F.Location);
				const FFVFishRecord* Rec = P.GetRecord(F.Id);
				const bool bKnown = Rec && Rec->Caught > 0;
				const FString Name = FString::Printf(TEXT("%s (%s)"), bKnown ? *F.Name : TEXT("???"), L ? *L->Name : TEXT("?"));
				if (F.Weather == EFVWeatherReq::Rain) { RainFish.Add(Name); }
				else if (F.Weather == EFVWeatherReq::Sunny) { SunFish.Add(Name); }
				if ((F.TimeMask & FVTime::Night) && !(F.TimeMask & (FVTime::Day | FVTime::Morning))) { NightFish.Add(Name); }
			}
			FString Forecast;
			if (Tomorrow == EFVWeather::Rain)
			{
				Forecast = RainFish.Num() > 0 ? FString::Printf(TEXT("Завтра дождь! Клюнет рыба, которая любит непогоду: %s."), *FString::Join(RainFish, TEXT(", ")))
					: FString(TEXT("Завтра дождь. Рыба активнее, а сети ловят лучше."));
			}
			else
			{
				Forecast = SunFish.Num() > 0 ? FString::Printf(TEXT("Завтра ясно. В солнечную погоду клюют: %s."), *FString::Join(SunFish, TEXT(", ")))
					: FString(TEXT("Завтра ясно — хороший день для рыбалки."));
				if (RainFish.Num() > 0) { Forecast += FString::Printf(TEXT(" Дождевой рыбе (%d) придётся подождать."), RainFish.Num()); }
			}
			const FString Night = NightFish.Num() > 0 ? FString::Printf(TEXT("Ночью (22:00–06:00) клюют: %s."), *FString::Join(NightFish, TEXT(", ")))
				: FString(TEXT("Ночных рыб в открытых местах пока нет."));

			const FFVRect ArtR = bP ? FFVRect(Area.X, Area.Y, Area.W, 104.f) : FFVRect(Area.X, Area.Y, 214.f, Area.H);
			const FFVRect InfoR = bP ? FFVRect(Area.X, ArtR.Bottom() + 6.f, Area.W, Area.Bottom() - ArtR.Bottom() - 6.f) : FFVRect(ArtR.Right() + 6.f, Area.Y, Area.Right() - ArtR.Right() - 6.f, Area.H);
			D.Panel(ArtR, EFVPanel::Parchment);
			{
				const FFVRect In = ArtR.Inset(7.f);
				const FFVRect Box = bP ? FFVRect(In.X, In.Y, 90.f, In.H) : FFVRect(In.X, In.Y, In.W, 100.f);
				IconBox(D, Box);
				const FVector2D C = Box.Center();
				const float T = float(D.Time);
				for (int32 k = 0; k < 3; ++k)
				{
					const float Ph = FMath::Fmod(T * 0.7f + float(k) / 3.f, 1.f);
					const float Rad = 18.f + Ph * (bP ? 22.f : 30.f);
					const FLinearColor WC = FLinearColor(0.45f, 0.25f, 0.1f, 0.55f * (1.f - Ph));
					for (int32 Side = -1; Side <= 1; Side += 2)
					{
						FVector2D Prev = C + FVector2D(Side * Rad * FMath::Cos(-0.8f), Rad * FMath::Sin(-0.8f));
						for (int32 s = 1; s <= 6; ++s)
						{
							const float A = -0.8f + 1.6f * float(s) / 6.f;
							const FVector2D Pt = C + FVector2D(Side * Rad * FMath::Cos(A), Rad * FMath::Sin(A));
							if (Box.Inset(2.f).Contains(Pt) && Box.Inset(2.f).Contains(Prev)) { D.Line(Prev, Pt, WC, 1.5f); }
							Prev = Pt;
						}
					}
				}
				DrawIcon(D, EFVIcon::Radio, FFVRect(C.X - 24.f, C.Y - 24.f, 48.f, 48.f), 3.f);
				const float TX = bP ? Box.Right() + 8.f : In.X;
				float Y = bP ? In.Y : Box.Bottom() + 6.f;
				const float TW = bP ? In.Right() - TX : In.W;
				D.Text(TEXT("Радио «Клёвая волна»"), FVector2D(TX, Y), bP ? FV::Ui::Body() : 8.f, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(bP ? FV::Ui::Body() : 8.f) + 2.f;
				const int32 LineIdx = ((RadioLine % NumRadioLines) + NumRadioLines) % NumRadioLines;
				TextWrappedLimited(D, FString::Printf(TEXT("«%s»"), GRadioLines[LineIdx]), FVector2D(TX, Y), TW, SmallS(), FVPal::InkSoft(), bP ? 4 : 6);
				if (!bP)
				{
					if (D.Button(FFVRect(In.X, In.Bottom() - 20.f, In.W, 20.f), TEXT("Покрутить ручку"), EFVButton::Normal, true, 8.f)) { Want(EAct::RadioTune); }
				}
			}
			D.Panel(InfoR, EFVPanel::Parchment);
			{
				// the full forecast in a smoothly scrolling column (fish lists can be long). A forecast taller than the column
				// by no more than the tight spacing saves (late game: long fish lists + the City grid) is set a little tighter,
				// so it fits whole and no line is cut at the bottom at rest; a longer one keeps the normal spacing and scrolls.
				const FFVRect ColR = InfoR.Inset(2.f);
				const bool bTight = RadioNormalH > ColR.H + 0.5f && RadioNormalH - RadioTightSave <= ColR.H;
				float Save = 0.f;   // what the tight spacing saves this frame (counted whether it is used or not)
				float Y = RadioCol.Begin(D, ColR, 6.f);
				const FFVRect In(InfoR.X + 8.f, Y, InfoR.W - 16.f - RadioCol.BarRoom(), 0.f);
				const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
				D.Text(TEXT("Прогноз погоды"), FVector2D(In.X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(TS) + 3.f;
				// today (small) | tomorrow (big)
				const float Half = FMath::FloorToFloat((In.W - 8.f) * 0.5f);
				const float CardH = (bP ? 50.f : 44.f) - (bTight ? 6.f : 0.f);
				Save += 6.f;
				const FFVRect TodayR(In.X, Y, Half, CardH);
				const FFVRect TomR(In.X + Half + 8.f, Y, Half, CardH);
				auto WeatherCard = [&](const FFVRect& R, const TCHAR* Label, EFVWeather W, bool bMain)
				{
					D.Rect(R, bMain ? FVPal::Gold() : BoxEdge());
					D.Rect(R.Inset(bMain ? 2.f : 1.f), bMain ? FV::Col(255, 244, 214) : BoxFill());
					const float IS = R.H - 12.f;
					DrawIcon(D, W == EFVWeather::Rain ? EFVIcon::Rain : EFVIcon::Sun, FFVRect(R.X + 6.f, R.Y + 6.f, IS, IS), 2.f);
					const float TX = R.X + IS + 12.f;
					D.Text(Label, FVector2D(TX, R.Y + 6.f), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, true);
					const float WS = bP ? FV::Ui::Body() + (bMain ? 1.f : 0.f) : (bMain ? 9.f : 8.f);
					D.Text(D.Ellipsize(FVDB::WeatherName(W), R.Right() - 4.f - TX, WS), FVector2D(TX, R.Y + 8.f + D.LineHeight(SmallS())), WS, W == EFVWeather::Rain ? InkInfo() : FV::Col(176, 110, 10), EFVAlign::Left, false, true);
				};
				WeatherCard(TodayR, TEXT("Сегодня"), Today, false);
				WeatherCard(TomR, TEXT("Завтра"), Tomorrow, true);
				Y = TodayR.Bottom() + (bTight ? 5.f : 7.f);
				Save += 2.f;
				Y += TextWrappedLimited(D, Forecast, FVector2D(In.X, Y), In.W, SmallS(), FVPal::Ink(), 40) + 4.f;
				Y += TextWrappedLimited(D, Night, FVector2D(In.X, Y), In.W, SmallS(), FVPal::InkSoft(), 40);
				Y = DrawCityForecast(D, P, In, Y, Tomorrow, bTight, Save);
				RadioCol.End(D, Y, bTight ? 2.f : 6.f);
				Save += 4.f;
				// the next frame decides the spacing from the height at the normal spacing
				RadioTightSave = Save;
				RadioNormalH = RadioCol.ContentH + (bTight ? Save : 0.f);
			}
		}

		/** Update 6 (§9б «Радиостанция»): the local weather of the City tomorrow, every two hours (FVDBU6::LocalWeather),
		    and the expert fish / legends whose weather fact is known (Р33: never names what the player has not learnt).
		    bTight: the tighter spacing of TabRadio (a few px less around the grid); InOutSave += what it saves. */
		float DrawCityForecast(FVDraw& D, const UFVSaveGame& P, const FFVRect& In, float Y, EFVWeather Tomorrow, bool bTight, float& InOutSave)
		{
			const FName City = FVDBU6::CityId();
			if (P.GetStationLevel(EFVStation::Radio) < 2 || !P.IsLocationUnlocked(City))
			{
				if (P.GetStationLevel(EFVStation::Radio) < 2 && P.GetLevel() >= 11)
				{
					Y += 6.f;
					Y += TextWrappedLimited(D, TEXT("«Радиостанция» (улучшение) ловит прогноз Города по часам."), FVector2D(In.X, Y), In.W, SmallS(), FVPal::InkSoft(), 12);
				}
				return Y;
			}
			const bool bP = FV::IsPortrait();
			Y += bTight ? 6.f : 8.f;
			InOutSave += 2.f;
			D.Rect(FFVRect(In.X, Y - 3.f, In.W, 1.f), BoxEdge());
			D.Text(TEXT("Город завтра, по часам"), FVector2D(In.X, Y + 1.f), bP ? FV::Ui::Body() : 8.f, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(bP ? FV::Ui::Body() : 8.f) + 4.f;
			const int32 Day = P.Day + 1;
			constexpr int32 Cells = 12;   // the sea of the City changes every two hours
			const int32 PerRow = bP ? 6 : 12;
			const float Gap = 2.f;
			const float CW = FMath::FloorToFloat((In.W - Gap * float(PerRow - 1)) / float(PerRow));
			const float CH = bTight ? 31.f : 34.f;   // tight: the hour label moves up to the icon
			InOutSave += 3.f * float(FMath::DivideAndRoundUp(Cells, PerRow));
			uint8 SeaSeen = 0;
			bool bTideShown = false;
			for (int32 c = 0; c < Cells; ++c)
			{
				const int32 Hour = c * 2;
				const FFVLocalWeather W = FVDBU6::LocalWeather(City, Day, float(Hour) * 60.f + 30.f, Tomorrow);
				SeaSeen |= uint8(1u << uint32(W.Sea));
				bTideShown |= W.bTide;
				const FFVRect R(In.X + float(c % PerRow) * (CW + Gap), Y + float(c / PerRow) * (CH + Gap), CW, CH);
				const FLinearColor Fill = W.Sea == EFVSeaState::Storm ? FV::Col(200, 208, 222) : (W.Sea == EFVSeaState::Fog ? FV::Col(226, 226, 222)
					: (W.Sea == EFVSeaState::Wind ? FV::Col(214, 232, 240) : FV::Col(232, 244, 250)));
				D.Rect(R, W.bDark ? FV::Col(92, 104, 140) : BoxEdge());
				D.Rect(R.Inset(1.f), W.bDark ? FV::Col(150, 160, 196) : Fill);
				static const EFVIcon SeaIcons[] = { EFVIcon::SeaCalm, EFVIcon::SeaWind, EFVIcon::SeaStorm, EFVIcon::SeaFog };
				DrawIcon(D, SeaIcons[FMath::Clamp(int32(W.Sea), 0, 3)], FFVRect(R.Center().X - 8.f, R.Y + 3.f, 16.f, 16.f), 1.f);
				if (W.bTide) { DrawIcon(D, EFVIcon::Tide, FFVRect(R.Right() - 9.f, R.Y + 1.f, 8.f, 8.f), 1.f); }
				D.Text(FString::Printf(TEXT("%02d"), Hour), FVector2D(R.Center().X, R.Bottom() - 12.f), SmallS(), W.bDark ? FV::Col(250, 250, 255) : FVPal::InkSoft(), EFVAlign::Center, false, true);
			}
			Y += float(FMath::DivideAndRoundUp(Cells, PerRow)) * (CH + Gap) + 3.f;
			// legend of the states that occur
			TArray<FString> States;
			for (int32 s = 0; s < int32(EFVSeaState::Count); ++s)
			{
				if (SeaSeen & (1u << uint32(s))) { States.Add(FVDBU6::SeaStateName(EFVSeaState(s))); }
			}
			FString Sum = FString::Printf(TEXT("Завтра: %s."), *FString::Join(States, TEXT(", ")));
			if (bTideShown) { Sum += TEXT(" Прилив 18:00–22:00."); }
			Sum += TEXT(" Темно с 22:00 до 06:00.");
			Y += TextWrappedLimited(D, Sum, FVector2D(In.X, Y), In.W, SmallS(), FVPal::Ink(), 12) + 3.f;
			// expert fish and legends: only those whose weather fact the player knows
			TArray<FString> Expert;
			for (const FFVExpertFishDef& E : FVDBU6::ExpertFish())
			{
				const FFVFishDef* F = FVDB::FindFish(E.FishId);
				if (!F || F->Location != City) { continue; }
				bool bKnowsWeather = false;
				for (int32 i = 0; i < E.Facts.Num(); ++i)
				{
					if (E.Facts[i].Kind == EFVFactKind::Weather && P.KnowsFact(E.FishId, i)) { bKnowsWeather = true; break; }
				}
				if (!bKnowsWeather) { continue; }
				const bool bSea = E.SeaMask == 0 || (E.SeaMask & SeaSeen) != 0;
				const bool bTideOk = !E.bNeedTide || bTideShown;
				if (bSea && bTideOk) { Expert.Add(F->Name); }
			}
			const FString ExLine = Expert.Num() > 0
				? FString::Printf(TEXT("По вашим сведениям завтра подойдёт погода для: %s."), *FString::Join(Expert, TEXT(", ")))
				: FString(TEXT("О погоде для экспертных рыб вы пока ничего не знаете — сведения дают задания Города и сокровища."));
			Y += TextWrappedLimited(D, ExLine, FVector2D(In.X, Y), In.W, SmallS(), Expert.Num() > 0 ? InkInfo() : FVPal::InkSoft(), 40);
			return Y;
		}

		// ---------------------------------------------------------------------------------------- trophy wall
		void TabTrophies(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			const int32 Cap = FMath::Max(1, P.StationCapacity(EFVStation::TrophyWall));
			const TArray<FFVTrophy>& Tr = P.Trophies;
			const TArray<FFVIngredient> Cost = P.TrophyCost();
			const bool bHaveCost = P.HasIngredients(Cost, 1);
			const bool bAny = HasEligibleFish(P, EPicker::Trophy);
			const FString Label = !bHaveCost ? FString(TEXT("Нужны доска и клей")) : (bAny ? FString(TEXT("Сделать чучело")) : FString(TEXT("Нет подходящей рыбы")));
			const bool bEnabled = bHaveCost && bAny;
			if (Foot)
			{
				if (D.Button(*Foot, Label, EFVButton::Primary, bEnabled, FV::Ui::Body() + 0.5f)) { Want(EAct::OpenPicker, NAME_None, int32(EPicker::Trophy)); }
				return;
			}
			const FFVRect WallR = bP ? FFVRect(Area.X, Area.Y, Area.W, FMath::Min(Area.H - 96.f, FMath::RoundToFloat(Area.H * 0.66f))) : FFVRect(Area.X, Area.Y, 390.f, Area.H);
			// wooden wall
			D.Rect(WallR, FV::Col(92, 52, 26));
			const FFVRect Planks = WallR.Inset(3.f);
			D.Rect(Planks, FV::Col(168, 108, 60));
			const int32 NPl = FMath::Max(3, FMath::RoundToInt(Planks.H / 18.f));
			for (int32 k = 1; k < NPl; ++k) { D.Rect(FFVRect(Planks.X, FMath::RoundToFloat(Planks.Y + Planks.H * float(k) / float(NPl)), Planks.W, 1.f), FV::Col(128, 78, 40)); }
			const int32 Cols = bP ? 2 : 3;
			const int32 Rows = FMath::DivideAndRoundUp(Cap, Cols);
			const float Gap = 6.f;
			const FFVRect GridA = Planks.Inset(6.f);
			const float CellH = FMath::FloorToFloat((GridA.H - float(Rows - 1) * Gap) / float(Rows));
			UFVSprites* Sp = UFVSprites::Get();
			for (int32 i = 0; i < Cap; ++i)
			{
				const FFVRect C = FVDraw::SplitH(FFVRect(GridA.X, GridA.Y + float(i / Cols) * (CellH + Gap), GridA.W, CellH), i % Cols, Cols, Gap);
				// plaque board
				D.Rect(C, FV::Col(70, 40, 20));
				D.Rect(C.Inset(1.f), FV::Col(120, 70, 36));
				D.Rect(FFVRect(C.X + 1.f, C.Y + 1.f, C.W - 2.f, 1.f), FV::Col(170, 110, 60));
				const FFVFishDef* F = Tr.IsValidIndex(i) ? FVDB::FindFish(Tr[i].FishId) : nullptr;
				const float LabelS = bP ? 7.f : 6.f;
				if (!F)
				{
					D.Text(TEXT("пусто"), FVector2D(C.Center().X, C.Center().Y - 5.f), LabelS, FV::Col(200, 150, 100), EFVAlign::Center, false, false);
					continue;
				}
				const float LabelH = D.LineHeight(LabelS, false) * 2.f + 2.f;
				const FFVRect Art(C.X + 3.f, C.Y + 3.f, C.W - 6.f, C.H - 6.f - LabelH);
				if (Sp)
				{
					const FFVSprite Plaque = Sp->TrophyPlaque(*F);
					if (Plaque.IsValid()) { FVUI::SpriteFit(D, Plaque, Art, FLinearColor::White, 3.f); }
					else { const FFVSprite FS = Sp->Fish(*F); FVUI::SpriteFit(D, FS, Art, FLinearColor::White, 3.f); }
				}
				const EFVQuality Q = QualityOf(Tr[i].Quality);
				if (Q != EFVQuality::Normal) { D.Stars(FVector2D(C.X + 3.f, C.Y + 3.f), Q, 1.f); }
				// brass label
				const FFVRect Lab(C.X + 4.f, C.Bottom() - LabelH - 2.f, C.W - 8.f, LabelH);
				D.Rect(Lab, FV::Col(150, 110, 40));
				D.Rect(Lab.Inset(1.f), FV::Col(226, 190, 100));
				D.Text(D.Ellipsize(F->Name, Lab.W - 4.f, LabelS, true), FVector2D(Lab.Center().X, Lab.Y + 1.f), LabelS, FV::Col(70, 44, 16), EFVAlign::Center, false, true);
				D.Text(FString::Printf(TEXT("%s см"), *FVUI::Num(Tr[i].SizeCm, 1)), FVector2D(Lab.Center().X, Lab.Y + 1.f + D.LineHeight(LabelS, false)), LabelS, FV::Col(90, 60, 24), EFVAlign::Center, false, false);
				// Р48: take the trophy down (fish back to the keepnet)
				{
					const float BH = FMath::Min(bP ? 24.f : 17.f, FMath::Max(12.f, C.H - LabelH - 8.f));
					const float BW = FMath::Min(C.W - 6.f, bP ? 62.f : 46.f);
					if (D.Button(FFVRect(C.Right() - BW - 3.f, C.Y + 3.f, BW, BH), TEXT("Снять"), EFVButton::Normal, true, bP ? FV::Ui::Body() : 7.f))
					{
						Want(EAct::Unmount, Tr[i].FishId);
					}
				}
			}
			const FFVRect InfoR = bP ? FFVRect(Area.X, WallR.Bottom() + 6.f, Area.W, Area.Bottom() - WallR.Bottom() - 6.f) : FFVRect(WallR.Right() + 6.f, Area.Y, Area.Right() - WallR.Right() - 6.f, Area.H);
			D.Panel(InfoR, EFVPanel::Parchment);
			const FFVRect In = InfoR.Inset(7.f);
			float Y = In.Y;
			const float TS = bP ? FV::Ui::Body() : 8.f;
			D.Text(FString::Printf(TEXT("Трофеев: %d / %d"), Tr.Num(), Cap), FVector2D(In.X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS) + 2.f;
			Y += TextWrappedLimited(D, TEXT("Чучело делается по вашему рекорду вида. Висящий трофей заменит только рыба покрупнее."), FVector2D(In.X, Y), In.W, SmallS(), FVPal::InkSoft(), bP ? 2 : 4) + 3.f;
			const float S = BodyS();
			const float RH = ReqRowH(D, S);
			const bool bTwo = bP;
			const float ColW = bTwo ? FMath::FloorToFloat((In.W - 10.f) * 0.5f) : In.W;
			for (int32 i = 0; i < Cost.Num(); ++i)
			{
				const float RX = In.X + (bTwo ? float(i % 2) * (ColW + 10.f) : 0.f);
				const float RY = Y + float(bTwo ? i / 2 : i) * RH;
				if (RY + RH > (bP ? In.Bottom() : In.Bottom() - 24.f)) { break; }
				ReqRow(D, RX, RY, ColW, Cost[i].ItemId, P.CountIngredient(Cost[i].ItemId), Cost[i].Count, S);
			}
			if (!bP)
			{
				if (D.Button(FFVRect(In.X, In.Bottom() - 20.f, In.W, 20.f), Label, EFVButton::Primary, bEnabled, 8.f)) { Want(EAct::OpenPicker, NAME_None, int32(EPicker::Trophy)); }
			}
		}

		// ---------------------------------------------------------------------------------------- bed / chest
		void TabInfo(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect* Foot)
		{
			const bool bP = FV::IsPortrait();
			const bool bChest = Station == EFVStation::Chest;
			if (Foot)
			{
				if (bChest && D.Button(*Foot, TEXT("Открыть сундук →"), EFVButton::Primary, true, FV::Ui::Body() + 0.5f)) { Want(EAct::OpenChest); }
				return;
			}
			const FFVStationDef* Def = GetDef();
			D.Panel(Area, EFVPanel::Parchment);
			const FFVRect In = Area.Inset(10.f);
			const float BoxH = bP ? 110.f : 96.f;
			auto DrawBox = [this, &D](const FFVRect& Box)
			{
				IconBox(D, Box);
				if (UFVSprites* Sp = UFVSprites::Get())
				{
					const FFVSprite S = Sp->StationSprite(Station, 1);
					FVUI::SpriteFit(D, S, Box.Inset(6.f), FLinearColor::White, 3.f);
				}
			};
			// every line is readable: portrait scrolls the whole column, landscape the text right of the picture
			if (!bP) { DrawBox(FFVRect(In.X, In.Y, 180.f, BoxH)); }
			const float TX = bP ? In.X : In.X + 192.f;
			const float ColBottom = bP ? Area.Bottom() - 2.f : (bChest ? In.Bottom() - 26.f : Area.Bottom() - 2.f);
			const FFVRect ColR = bP ? Area.Inset(2.f) : FFVRect(TX - 2.f, Area.Y + 2.f, Area.Right() - TX, ColBottom - Area.Y - 2.f);
			float Y = InfoCol.Begin(D, ColR, In.Y - ColR.Y);
			const float TW = In.Right() - TX - InfoCol.BarRoom();
			if (bP)
			{
				DrawBox(FFVRect(In.X, Y, TW, BoxH));
				Y += BoxH + 8.f;
			}
			const float TS = bP ? FV::Ui::Body() + 1.f : 10.f;
			D.Text(Def ? Def->Name : FString(), FVector2D(TX, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS) + 2.f;
			if (Def) { Y += TextWrappedLimited(D, Def->Description, FVector2D(TX, Y), TW, BodyS(), FVPal::Ink(), 40) + 4.f; }
			TArray<FString> Lines;
			if (Station == EFVStation::Bed)
			{
				Lines.Add(TEXT("Сон переносит вас в 06:00 следующего утра."));
				Lines.Add(TEXT("Пока вы спите, сети ловят рыбу, коптильня коптит, а червятник растит червей."));
				Lines.Add(FV::IsTouchUI() ? TEXT("Чтобы лечь спать, коснитесь кровати в комнате.") : TEXT("Чтобы лечь спать, подойдите к кровати и нажмите E."));
			}
			else
			{
				const int32 Cap = P.ContainerCapacity(EFVContainer::Chest);
				const int32 Used = P.ContainerUsed(EFVContainer::Chest);
				Lines.Add(FString::Printf(TEXT("Занято ячеек: %d из %d."), Used, Cap));
				Lines.Add(TEXT("Храните здесь ресурсы, материалы, снасти и сети — рюкзак останется свободным."));
			}
			for (const FString& L : Lines)
			{
				D.Circle(FVector2D(TX + 3.f, Y + D.LineHeight(SmallS(), false) * 0.5f), 2.f, FVPal::Wood());
				Y += TextWrappedLimited(D, L, FVector2D(TX + 10.f, Y), TW - 10.f, SmallS(), FVPal::InkSoft(), 40) + 3.f;
			}
			InfoCol.End(D, Y, 7.f);
			if (!bP && bChest && D.Button(FFVRect(In.Right() - 200.f, In.Bottom() - 20.f, 200.f, 20.f), TEXT("Открыть сундук →"), EFVButton::Primary, true, 8.f)) { Want(EAct::OpenChest); }
		}

		// ---------------------------------------------------------------------------------------- keepnet fish picker
		/** Can this keepnet stack go into the current picker target? OutWhy = reason when not. */
		bool Eligible(const UFVSaveGame& P, EPicker K, const FFVInvStack& S, FString* OutWhy = nullptr) const
		{
			auto Why = [OutWhy](const TCHAR* T) { if (OutWhy) { *OutWhy = T; } return false; };
			const FFVFishDef* FD = FVDB::FindFish(S.ItemId);
			if (!FD || S.Count <= 0) { return Why(TEXT("Это не рыба")); }
			if (S.Flags & FVStackFlags::Smoked) { return Why(K == EPicker::Smoke ? TEXT("Уже копчёная") : TEXT("Копчёная рыба не подойдёт")); }
			if (FD->bExpert)
			{
				// update 6 (§9б): expert fish need the expert level of the station — its last level in the data
				const EFVStation St = K == EPicker::Smoke ? EFVStation::Smokehouse : (K == EPicker::Aquarium ? EFVStation::Aquarium : EFVStation::TrophyWall);
				const TArray<FFVStationDef>& Defs = FVDB::Stations();
				const int32 Need = Defs.IsValidIndex(int32(St)) ? FMath::Max(1, Defs[int32(St)].Levels.Num()) : 1;
				if (P.GetStationLevel(St) < Need)
				{
					return Why(K == EPicker::Smoke ? TEXT("Экспертную рыбу коптит только каменная коптильня")
						: (K == EPicker::Aquarium ? TEXT("Экспертной рыбе нужен Океанариум") : TEXT("Нужна Большая стена трофеев")));
				}
			}
			if (K == EPicker::Trophy)
			{
				const FFVFishRecord* Rec = P.GetRecord(S.ItemId);
				if (!Rec || Rec->MaxSize <= 0.f) { return Why(TEXT("Нет рекорда этого вида")); }
				const FFVTrophy* Mounted = P.Trophies.FindByPredicate([&S](const FFVTrophy& T) { return T.FishId == S.ItemId; });
				if (Mounted && Mounted->SizeCm >= Rec->MaxSize - 0.05f) { return Why(TEXT("Этот трофей уже висит")); }
				if (!Mounted && P.Trophies.Num() >= P.StationCapacity(EFVStation::TrophyWall)) { return Why(TEXT("На стене нет места")); }
			}
			return true;
		}

		bool HasEligibleFish(const UFVSaveGame& P, EPicker K) const
		{
			for (const FFVInvStack& S : P.Keepnet)
			{
				if (Eligible(P, K, S)) { return true; }
			}
			return false;
		}

		void DrawPicker(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, const FFVRect& Win)
		{
			const bool bP = FV::IsPortrait();
			const TArray<FFVInvStack>& K = P.Keepnet;
			int32 Sel = PickKey.Find(K);   // an ineligible selection stays selected: the card explains why
			const FFVRect Panel = bP ? FFVRect(Area.X, Area.Y, Area.W, Area.H - 70.f) : FFVRect(Area.X, Area.Y, Area.W, Area.H - 54.f);
			D.Panel(Panel, EFVPanel::Parchment);
			const FFVRect In = Panel.Inset(7.f);
			float Y = In.Y;
			const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
			const TCHAR* Title = Picker == EPicker::Smoke ? TEXT("Какую рыбу закоптить?") : (Picker == EPicker::Aquarium ? TEXT("Кого запустить в аквариум?") : TEXT("Из какой рыбы сделать чучело?"));
			const TCHAR* Sub = Picker == EPicker::Smoke ? TEXT("6 ч с топливом · копчёная рыба стоит вдвое дороже")
				: (Picker == EPicker::Aquarium ? TEXT("Рыба будет жить в аквариуме, её можно вернуть в садок") : TEXT("Размер чучела = ваш рекорд вида · доска и клей"));
			D.Text(Title, FVector2D(In.X, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS);
			if (D.MeasureText(Sub, SmallS(), false).X <= In.W)
			{
				D.Text(Sub, FVector2D(In.X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += D.LineHeight(SmallS(), false) + 4.f;
			}
			else { Y += TextWrappedLimited(D, Sub, FVector2D(In.X, Y), In.W, SmallS(), FVPal::InkSoft(), 40) + 5.f; }   // narrow phone: wraps
			const FFVRect GridA(In.X, Y, In.W, In.Bottom() - Y);
			if (K.Num() == 0)
			{
				EmptyState(D, GridA, EFVIcon::Keepnet, TEXT("Садок пуст — сначала порыбачьте."), SmallS());
			}
			else
			{
				const float Cell = bP ? FV::Ui::Slot() : 30.f;
				const float Pitch = Cell + 3.f;
				const float Pad = 2.f;   // room for the selection frame
				const int32 Cols = FMath::Max(1, FMath::FloorToInt((GridA.W - 6.f + 3.f) / Pitch));
				const int32 TotalRows = FMath::DivideAndRoundUp(K.Num(), Cols);
				PickGrid.Update(D, GridA, 2.f * Pad + float(TotalRows) * Pitch - 3.f);
				D.PushClip(GridA);
				for (int32 i = 0; i < K.Num(); ++i)
				{
					const FFVRect C(GridA.X + Pad + float(i % Cols) * Pitch, GridA.Y + Pad - PickGrid.Offset() + float(i / Cols) * Pitch, Cell, Cell);
					if (!RowVisible(GridA, C.Y, Cell)) { continue; }
					const bool bOk = Eligible(P, Picker, K[i]);
					if (D.Slot(C, i == Sel, true) && i != Sel)
					{
						PickKey.Set(K[i]);
						Sel = i;
					}
					StackCell(D, C, K[i].ItemId, K[i].Quality, K[i].Flags, K[i].Count, !bOk);
				}
				EndList(D, PickGrid, GridA);
			}

			// selection card + buttons
			const FFVRect Card = bP ? FFVRect(Area.X, Panel.Bottom() + 6.f, Area.W, Area.Bottom() - Panel.Bottom() - 6.f)
				: FFVRect(Area.X, Panel.Bottom() + 6.f, Area.W - 300.f, Area.Bottom() - Panel.Bottom() - 6.f);
			D.Panel(Card, EFVPanel::Parchment);
			FString Why;
			const bool bSel = K.IsValidIndex(Sel);
			const bool bOk = bSel && Eligible(P, Picker, K[Sel], &Why);
			{
				const FFVRect CI = Card.Inset(6.f);
				if (!bSel)
				{
					D.Text(D.Ellipsize(FV::IsTouchUI() ? TEXT("Коснитесь рыбы в садке") : TEXT("Выберите рыбу в садке"), CI.W, BodyS(), false),
						FVector2D(CI.X, CI.Center().Y - D.LineHeight(BodyS(), false) * 0.5f), BodyS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
				else
				{
					const FFVInvStack& S = K[Sel];
					const FFVRect IB(CI.X, CI.Y, CI.H * 1.4f, CI.H);
					IconBox(D, IB);
					DrawItemIcon(D, S.ItemId, IB.Inset(2.f), 2.f);
					const float TX = IB.Right() + 6.f;
					const float NS = bP ? FV::Ui::Body() : 8.f;
					const EFVQuality Q = QualityOf(S.Quality);
					FString Name = FVDB::ItemName(S.ItemId);
					if (Q != EFVQuality::Normal) { Name += FString::Printf(TEXT(" · %s"), *FVDB::QualityName(Q)); }
					D.Text(D.Ellipsize(Name, CI.Right() - TX, NS), FVector2D(TX, CI.Y), NS, FVPal::Ink(), EFVAlign::Left, false, true);
					FString Info;
					FLinearColor InfoC = FVPal::InkSoft();
					if (!bOk) { Info = Why; InfoC = InkBad(); }
					else if (Picker == EPicker::Trophy)
					{
						const FFVFishRecord* Rec = P.GetRecord(S.ItemId);
						const FFVTrophy* Mounted = P.Trophies.FindByPredicate([&S](const FFVTrophy& T) { return T.FishId == S.ItemId; });
						Info = FString::Printf(TEXT("Рекорд: %s см"), Rec ? *FVUI::Num(Rec->MaxSize, 1) : TEXT("?"));
						if (Mounted) { Info += FString::Printf(TEXT(" · заменит трофей %s см"), *FVUI::Num(Mounted->SizeCm, 1)); }
						InfoC = InkGood();
					}
					else { Info = FString::Printf(TEXT("В садке: %d"), S.Count); }
					// the reason / record line wraps over the free height of the card instead of being cut
					const FVector2D IP(TX, CI.Y + D.LineHeight(NS) + 1.f);
					const float IStep = FMath::Max(1.f, FMath::RoundToFloat(D.LineHeight(SmallS(), false) * 0.95f));
					if (D.MeasureText(Info, SmallS(), false).X <= CI.Right() - TX) { D.Text(Info, IP, SmallS(), InfoC, EFVAlign::Left, false, false); }
					else { TextWrappedLimited(D, Info, IP, CI.Right() - TX, SmallS(), InfoC, FMath::Max(1, FMath::FloorToInt((Card.Bottom() - 2.f - IP.Y) / IStep))); }
				}
			}
			const TCHAR* ActLabel = Picker == EPicker::Smoke ? TEXT("Закоптить") : (Picker == EPicker::Aquarium ? TEXT("Запустить") : TEXT("Сделать чучело"));
			const EAct Act = Picker == EPicker::Smoke ? EAct::SmokeLoad : (Picker == EPicker::Aquarium ? EAct::AquaPut : EAct::Trophy);
			if (bP)
			{
				FFVRect ActR;
				if (D.ActionBar(Win, &ActR, TEXT("Отмена"))) { ClosePicker(); return; }
				if (D.Button(ActR, ActLabel, EFVButton::Primary, bOk, FV::Ui::Body() + 0.5f)) { Want(Act, NAME_None, Sel); }
				return;
			}
			const float BY = Card.Y + (Card.H - 20.f) * 0.5f;
			if (D.Button(FFVRect(Card.Right() + 8.f, BY, 100.f, 20.f), TEXT("Отмена"), EFVButton::Normal, true, 8.f)) { ClosePicker(); return; }
			if (D.Button(FFVRect(Area.Right() - 184.f, BY, 184.f, 20.f), ActLabel, EFVButton::Primary, bOk, 8.f)) { Want(Act, NAME_None, Sel); }
		}

		// ---------------------------------------------------------------------------------------- actions
		void RunIntent(UFVSaveGame& P)
		{
			const FIntent I = Intent;
			Intent = FIntent();
			UFVGame* G = Game;
			if (I.Act == EAct::None || !G) { return; }
			switch (I.Act)
			{
			case EAct::Upgrade:
			{
				const int32 Before = P.GetStationLevel(Station);
				const EFVBuyResult R = P.UpgradeStation(Station);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				const FFVStationDef* Def = GetDef();
				const int32 Lv = P.GetStationLevel(Station);
				const FString Name = Def ? Def->Name : FString();
				const FString LvName = LevelName(Lv);
				Success(G, Before <= 0 ? FString::Printf(TEXT("Построено: %s!"), *Name)
					: (LvName.IsEmpty() ? FString::Printf(TEXT("%s: уровень %d!"), *Name, Lv) : FString::Printf(TEXT("%s: теперь %s!"), *Name, *LvName)),
					EFVSfx::Hammer, EFVIcon::Hammer);
				Refresh(G);
				if (Before <= 0) { Pager.SetPage(0); RememberTab(0); }
				return;
			}
			case EAct::Craft:
			{
				const FFVRecipeDef* R = FVDB::FindRecipe(I.Id);
				if (!R) { return; }
				const int32 Times = FMath::Max(1, I.Count);
				const FFVRecipeDef Cooked = FVCookedRecipe(P, *R);   // «Русская печь»: two portions at once
				const EFVBuyResult Res = P.Craft(Cooked, Times);
				if (Res != EFVBuyResult::Ok) { Fail(G, Res); return; }
				const int32 Units = Cooked.OutputCount * Times;
				const bool bBait = FVDB::FindBait(R->OutputId) != nullptr;
				Success(G, FString::Printf(TEXT("Готово: %s ×%d%s"), *FVDB::ItemName(R->OutputId), Units, bBait ? TEXT(" (в наживку)") : TEXT(" (в рюкзак)")),
					Station == EFVStation::Workshop ? EFVSfx::Hammer : EFVSfx::Pickup, Station == EFVStation::Workshop ? EFVIcon::Hammer : EFVIcon::Pot);
				Refresh(G);
				return;
			}
			case EAct::Play:
			{
				const FFVRecipeDef* R = FVDB::FindRecipe(I.Id);
				if (!R) { return; }
				const EFVBuyResult Res = P.CanCraft(*R, 1);
				if (Res != EFVBuyResult::Ok) { Fail(G, Res); return; }
				SelectRecipe(TabOfRecipe(*R), R->Id);
				const EFVScreen S = R->Game == EFVCraftGame::Cooking ? EFVScreen::Cooking : EFVScreen::Knots;
				G->OpenScreenWith(S, R->Id);   // this screen is retired: return immediately
				return;
			}
			case EAct::OpenCellar:
				G->OpenScreen(EFVScreen::IceCellar);
				return;
			case EAct::OpenChest:
				G->OpenScreen(EFVScreen::Chest);
				return;
			case EAct::Install:
			{
				const EFVBuyResult R = P.InstallTackle(I.Id2, I.Id);
				if (R != EFVBuyResult::Ok) { Fail(G, R == EFVBuyResult::LevelTooLow ? FString(TEXT("Эта снасть слишком хороша для удочки")) : ResultText(R)); return; }
				Success(G, FString::Printf(TEXT("Установлено: %s"), *FVDB::ItemName(I.Id)), EFVSfx::Equip, EFVIcon::Rod);
				bHasCand = false;
				Refresh(G);
				return;
			}
			case EAct::RemoveTackle:
			{
				const FFVRodLoadout L = P.GetLoadout(I.Id2);
				const FName Old = I.Index == 0 ? L.Line : L.Float;
				const EFVBuyResult R = P.RemoveTackle(I.Id2, I.Index == 0 ? EFVItemKind::Line : EFVItemKind::Float);
				if (R != EFVBuyResult::Ok) { Fail(G, R == EFVBuyResult::NoRoom ? FString(TEXT("В рюкзаке нет места")) : ResultText(R)); return; }
				Success(G, FString::Printf(TEXT("Снято в рюкзак: %s"), *FVDB::ItemName(Old)), EFVSfx::Equip, EFVIcon::Backpack);
				bHasCand = false;
				Refresh(G);
				return;
			}
			case EAct::EquipBait:
			{
				if (!P.EquipBait(I.Id)) { Fail(G, EFVBuyResult::Invalid); return; }
				Success(G, I.Id.IsNone() ? FString(TEXT("Рыбачим без наживки")) : FString::Printf(TEXT("Насажено: %s"), *FVDB::ItemName(I.Id)), EFVSfx::Equip, EFVIcon::Bait);
				bHasCand = false;
				Refresh(G);
				return;
			}
			case EAct::CraftPickaxe:
			case EAct::CraftAxe:
			{
				const bool bPick = I.Act == EAct::CraftPickaxe;
				const EFVBuyResult R = bPick ? P.CraftPickaxe() : P.CraftAxe();
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				const TArray<FFVToolDef>& T = bPick ? FVDBU6::Pickaxes() : FVDBU6::Axes();
				const int32 Idx = (bPick ? P.U6.PickaxeLevel : P.U6.AxeLevel) - 1;
				Success(G, FString::Printf(TEXT("Готово: %s!"), T.IsValidIndex(Idx) ? *T[Idx].Name : TEXT("")), EFVSfx::Hammer, bPick ? EFVIcon::Pickaxe : EFVIcon::Axe);
				Refresh(G);
				return;
			}
			case EAct::CraftShovel:
			{
				const EFVBuyResult R = P.CraftShovel();
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				const TArray<FFVShovelDef>& Sh = FVDB::Shovels();
				const int32 Idx = P.ShovelLevel - 1;
				Success(G, FString::Printf(TEXT("Новая лопата: %s!"), Sh.IsValidIndex(Idx) ? *Sh[Idx].Name : TEXT("")), EFVSfx::Hammer, EFVIcon::Shovel);
				Refresh(G);
				return;
			}
			case EAct::AddFuel:
			{
				const EFVBuyResult R = P.AddSmokeFuel(1);
				if (R != EFVBuyResult::Ok) { Fail(G, R == EFVBuyResult::NoRoom || R == EFVBuyResult::MaxLevel ? FString(TEXT("Топки больше не вмещают")) : ResultText(R)); return; }
				Success(G, FString::Printf(TEXT("Дрова подброшены: топлива на %s"), *UFVGame::DurationText(P.SmokeFuel)), EFVSfx::Fire, EFVIcon::Smoke);
				Refresh(G);
				return;
			}
			case EAct::CollectSmoked:
			{
				const int32 N = P.CollectSmoked();
				if (N <= 0) { Fail(G, TEXT("Садок полон — освободите место")); return; }
				Success(G, FString::Printf(TEXT("Копчёная рыба в садке: +%d"), N), EFVSfx::Pickup, EFVIcon::Keepnet);
				Refresh(G);
				return;
			}
			case EAct::SmokeLoad:
			{
				if (!P.Keepnet.IsValidIndex(I.Index)) { return; }
				const FName Fish = P.Keepnet[I.Index].ItemId;
				const EFVBuyResult R = P.LoadSmokehouse(I.Index);
				if (R != EFVBuyResult::Ok) { Fail(G, R == EFVBuyResult::NoRoom ? FString(TEXT("В коптильне нет места")) : ResultText(R)); return; }
				Success(G, FString::Printf(TEXT("В коптильне: %s"), *FVDB::ItemName(Fish)), EFVSfx::Fire, EFVIcon::Smoke);
				Refresh(G);
				if (P.Smokehouse.Num() >= P.StationCapacity(EFVStation::Smokehouse) || !HasEligibleFish(P, EPicker::Smoke)) { ClosePicker(); }
				else { PickKey.Clear(); }
				return;
			}
			case EAct::CollectWorms:
			{
				const int32 N = P.CollectWorms();
				if (N <= 0) { Fail(G, TEXT("Пока нечего собирать")); return; }
				Success(G, FString::Printf(TEXT("Наживка из червятника: +%d"), N), EFVSfx::Pickup, EFVIcon::Bait);
				Refresh(G);
				return;
			}
			case EAct::AquaPut:
			{
				if (!P.Keepnet.IsValidIndex(I.Index)) { return; }
				const FName Fish = P.Keepnet[I.Index].ItemId;
				const EFVBuyResult R = P.PutInAquarium(I.Index);
				if (R != EFVBuyResult::Ok) { Fail(G, R == EFVBuyResult::NoRoom ? FString(TEXT("В аквариуме нет места")) : ResultText(R)); return; }
				Success(G, FString::Printf(TEXT("%s плавает в аквариуме"), *FVDB::ItemName(Fish)), EFVSfx::Splash, EFVIcon::Aquarium);
				Refresh(G);
				AquaSel = P.Aquarium.Num() - 1;
				ClosePicker();
				return;
			}
			case EAct::AquaTake:
			{
				if (!P.Aquarium.IsValidIndex(I.Index)) { return; }
				const FName Fish = P.Aquarium[I.Index].ItemId;
				const EFVBuyResult R = P.TakeFromAquarium(I.Index);
				if (R != EFVBuyResult::Ok) { Fail(G, R == EFVBuyResult::NoRoom ? FString(TEXT("Садок полон")) : ResultText(R)); return; }
				Success(G, FString::Printf(TEXT("%s — обратно в садок"), *FVDB::ItemName(Fish)), EFVSfx::Splash, EFVIcon::Keepnet);
				Refresh(G);
				return;
			}
			case EAct::Trophy:
			{
				if (!P.Keepnet.IsValidIndex(I.Index)) { return; }
				const FName Fish = P.Keepnet[I.Index].ItemId;
				const FFVFishRecord* Rec = P.GetRecord(Fish);
				const float Size = Rec ? Rec->MaxSize : 0.f;
				const EFVBuyResult R = P.MountTrophy(I.Index, Size);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				Success(G, FString::Printf(TEXT("Новый трофей: %s, %s см!"), *FVDB::ItemName(Fish), *FVUI::Num(Size, 1)), EFVSfx::Hammer, EFVIcon::Trophy);
				Refresh(G);
				ClosePicker();
				return;
			}
			case EAct::Unmount:
			{
				if (I.Id.IsNone()) { return; }
				const FName Fish = I.Id;
				TWeakObjectPtr<UFVGame> WeakG(G);
				G->Confirm(TEXT("Снять трофей?"), TEXT("Рыба вернётся в садок, доска и клей — нет."), [WeakG, Fish]()
				{
					UFVGame* GG = WeakG.Get();
					UFVSaveGame* PP = GG ? GG->GetProfile() : nullptr;
					if (!PP) { return; }
					const EFVBuyResult R = PP->UnmountTrophy(Fish);
					if (R != EFVBuyResult::Ok) { Fail(GG, R == EFVBuyResult::NoRoom ? FString(TEXT("Садок полон — трофей не снять")) : ResultText(R)); return; }
					Success(GG, FString::Printf(TEXT("Трофей снят: %s — в садке"), *FVDB::ItemName(Fish)), EFVSfx::Hammer, EFVIcon::Keepnet);
					Refresh(GG);
				}, TEXT("Снять"), TEXT("Отмена"));
				return;
			}
			case EAct::OpenPicker:
				OpenPicker(EPicker(FMath::Clamp(I.Index, 0, int32(EPicker::Trophy))));
				FVUI::Sfx(EFVSfx::Open, 0.6f);
				return;
			case EAct::RadioTune:
				++RadioLine;
				FVUI::Sfx(EFVSfx::Radio, 0.7f);
				return;
			default:
				return;
			}
		}

	};
}

TUniquePtr<FVScreen> FVMakeStationScreen(UFVGame* Game)
{
	return MakeUnique<FVStationScreenImpl::FStationScreen>(Game);
}
