// UPDATE 6 [SCREEN-YARD] — yard buildings (sawmill, forge) and garden beds (Docs/UPDATE6_FOREST_CITY.md §6.1–6.3). Owner: SCREEN-YARD.
// YardBuilding (context "yb_sawmill" / "yb_forge"): the building, its works (FVDBU6::RecipesForYard: quick sawing,
// the saw / forge mini-games, instant crafts) and the next level. GardenBed (context "bed_<n>"): plant a seed, watch
// it grow (real minutes of play), harvest. Both orientations: one scrolling column, «Назад» in the portrait dock.
// Р77: a work / seed row whose text does not fit opens on a tap (the whole text wrapped, one open row per list); the
// context "fv_expand" (alone or "yb_forge:fv_expand", "bed_1:fv_expand") opens the first such row for the autotest shot.

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"

namespace FVScreenYardImpl
{
	FV_HOME_UI_USING();

	float SmallS() { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }
	float BodyS() { return FV::IsPortrait() ? FV::Ui::Body() : 8.f; }

	// ---- Р77: a row whose text does not fit opens on a tap (the whole text wrapped, the row grows; one open row per list)
	constexpr int32 AllLines = 64;   // "no limit" for TextWrappedLimited: descriptions are shown in full

	bool Fits(const FVDraw& D, const FString& S, float W, float Size, bool bBold)
	{
		return D.MeasureText(S, Size, bBold).X <= W;
	}

	/** TextWrappedLimited's line step: the opened rows keep the look of the other wrapped texts of the screen. */
	float WrapStep(const FVDraw& D, float Size, bool bBold) { return FMath::RoundToFloat(D.LineHeight(Size, bBold) * 0.95f); }

	float WrapSpacing(const FVDraw& D, float Size, bool bBold)
	{
		const float Plain = D.MeasureWrapped(TEXT("Щ"), 10000.f, Size, bBold, 1.f);
		return Plain > 0.f ? WrapStep(D, Size, bBold) / Plain : 1.f;
	}

	float MeasureFull(const FVDraw& D, const FString& S, float W, float Size, bool bBold)
	{
		return D.MeasureWrapped(S, W, Size, bBold, WrapSpacing(D, Size, bBold));
	}

	float DrawFull(FVDraw& D, const FString& S, const FVector2D& Pos, float W, float Size, const FLinearColor& C, bool bBold)
	{
		return D.TextWrapped(S, Pos, W, Size, C, bBold, WrapSpacing(D, Size, bBold));
	}

	/** The «opens / closes» mark: a tiny soft triangle at the bottom-right of the text column (left of the row's button). */
	void ExpandMark(FVDraw& D, float ButtonX, float RowBottom, bool bOpen)
	{
		const bool bP = FV::IsPortrait();
		Triangle(D, FVector2D(ButtonX - 4.f, RowBottom - (bP ? 6.f : 4.f)), bOpen ? 0 : 1, bP ? 4 : 3, FVPal::InkSoft());
	}

	/** The context "fv_expand" (alone or joined to the usual one: "yb_forge:fv_expand") opens the first row whose text
	    does not fit (autotest shots). Strips it from Ctx; returns whether it was there. */
	bool TakeExpandContext(FString& Ctx)
	{
		if (!Ctx.Contains(TEXT("fv_expand"))) { return false; }
		Ctx.ReplaceInline(TEXT("fv_expand"), TEXT(""));
		Ctx.TrimCharInline(TEXT(':'), nullptr);
		Ctx.TrimCharInline(TEXT('_'), nullptr);
		return true;
	}

	/** The open row of one list + the scroll that keeps it in view once the column knows its new height. */
	struct FOpenRow
	{
		FName Id;
		int32 Show = 0;          // frames until ScrollToShow (the content height must include the opened row first)
		bool bInstant = false;
		bool bAuto = false;      // "fv_expand": open the first row whose text does not fit

		void Reset() { Id = NAME_None; Show = 0; bInstant = false; bAuto = false; }
		/** Per row, before its layout: the "fv_expand" opening. Returns true when it opened this row. */
		bool Auto(FName RowId, bool bCut)
		{
			if (!bAuto || !bCut || !Id.IsNone()) { return false; }
			Id = RowId;
			Show = 2;
			bInstant = true;
			bAuto = false;
			return true;
		}
		/** A tap on a row: opens it (its text is cut) or closes the open one (it, or another row, was tapped). */
		void Toggle(FName RowId, bool bCut)
		{
			Id = (Id == RowId || !bCut) ? NAME_None : RowId;
			Show = Id.IsNone() ? 0 : 2;
			bInstant = false;
		}
		/** Per row, once its height is known (before the tap is handled): keeps the opened row visible. */
		void Keep(FScrollColumn& Col, FName RowId, float RowY, float RowH)
		{
			if (Id != RowId || Show <= 0) { return; }
			if (--Show == 0) { Col.View.ScrollToShow(RowY - Col.Area.Y + Col.View.Offset(), RowH, bInstant); }
		}
	};

	bool YardFromId(FName Id, EFVYardBuilding& Out)
	{
		for (int32 b = 0; b < int32(EFVYardBuilding::Count); ++b)
		{
			if (FVDBU6::YardBuildingId(EFVYardBuilding(b)) == Id) { Out = EFVYardBuilding(b); return true; }
		}
		return false;
	}

	EFVIcon YardIcon(EFVYardBuilding B)
	{
		switch (B)
		{
		case EFVYardBuilding::Sawmill: return EFVIcon::Saw;
		case EFVYardBuilding::Forge: return EFVIcon::Anvil;
		default: return EFVIcon::Seed;
		}
	}

	FString LootText(const TArray<FFVIngredient>& Loot)
	{
		FString S;
		for (const FFVIngredient& I : Loot)
		{
			if (I.Count <= 0) { continue; }
			if (!S.IsEmpty()) { S += TEXT(", "); }
			S += FString::Printf(TEXT("%s ×%d"), *IngredientName(I.ItemId), I.Count);
		}
		return S.IsEmpty() ? FString(TEXT("ничего")) : S;
	}

	/** Content area under the header: portrait ends above the action dock. */
	FFVRect ContentArea(const FFVRect& Win, float Top)
	{
		const bool bP = FV::IsPortrait();
		const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 8.f : Win.Bottom() - 10.f;
		return FFVRect(Win.X + 10.f, Top, Win.W - 20.f, FMath::Max(40.f, Bottom - Top));
	}

	/** The requirement block of a level (materials, coins, fishing level). Returns the new Y. */
	float DrawRequirements(FVDraw& D, const UFVSaveGame& P, const FFVStationLevel& Lv, float X, float Y, float W)
	{
		const float S = SmallS();
		for (const FFVIngredient& I : Lv.Materials)
		{
			Y += ReqRow(D, X, Y, W, I.ItemId, P.CountIngredient(I.ItemId), I.Count, S);
		}
		if (Lv.Price > 0) { Y += CoinRow(D, X, Y, W, P.Money, Lv.Price, S); }
		Y += CondRow(D, X, Y, W, FString::Printf(TEXT("Уровень рыбака %d"), Lv.MinLevel), P.GetLevel() >= Lv.MinLevel, S);
		return Y;
	}

	// ---- 0.6.2 (Р80–Р82): rod recipes of the «Закалочная кузня»
	/** «Удочка «Бриз»» -> «Бриз» (with the quotes). */
	FString RodShort(const FFVRodDef& Rod)
	{
		FString S = Rod.Name;
		S.RemoveFromStart(TEXT("Удочка "));
		return S;
	}

	/** The rod a recipe re-forges (its rod input, перековка); nullptr = none. */
	const FFVRodDef* ReforgedRod(const FFVRecipeDef& R)
	{
		for (const FFVIngredient& I : R.Inputs)
		{
			if (const FFVRodDef* Rod = FVDB::FindRod(I.ItemId)) { return Rod; }
		}
		return nullptr;
	}

	/** Recipe inputs: like IngredientsText, but a rod input reads «Удочка «Бриз» (перековка)» (no «×1»). */
	FString RecipeInputsText(const TArray<FFVIngredient>& In)
	{
		TArray<FString> Parts;
		for (const FFVIngredient& I : In)
		{
			if (const FFVRodDef* Rod = FVDB::FindRod(I.ItemId)) { Parts.Add(FString::Printf(TEXT("%s (перековка)"), *Rod->Name)); }
			else { Parts.Add(FString::Printf(TEXT("%s ×%d"), *IngredientName(I.ItemId), I.Count)); }
		}
		return FString::Join(Parts, TEXT(" · "));
	}

	/** Starts a work: the saw / forge mini-game or the instant craft (re-checks CanCraft: it also runs from a confirm). */
	void StartWork(UFVGame* G, const FFVRecipeDef* R, EFVYardBuilding Building)
	{
		UFVSaveGame* PP = G ? G->GetProfile() : nullptr;
		if (!PP || !R) { return; }
		UFVSaveGame& P = *PP;
		const EFVBuyResult Can = P.CanCraft(*R);
		if (Can != EFVBuyResult::Ok) { Fail(G, Can); return; }
		if (R->bSawGame) { G->OpenScreenWith(EFVScreen::Sawing, R->Id); return; }
		if (R->bForgeGame) { G->OpenScreenWith(EFVScreen::Forging, R->Id); return; }
		const EFVBuyResult Res = P.Craft(*R);
		if (Res != EFVBuyResult::Ok) { Fail(G, Res); return; }
		if (const FFVRodDef* OutRod = FVDB::FindRod(R->OutputId))
		{
			// 0.6.2 (Р80): Craft gives the rod to OwnedRods and equips it
			Success(G, FString::Printf(TEXT("Выкована удочка %s — уже в руках"), *RodShort(*OutRod)), EFVSfx::Anvil, EFVIcon::Anvil);
			Refresh(G);
			return;
		}
		int32 Got = FMath::Max(1, R->OutputCount);
		if (R->Id == FName(TEXT("rcp_saw_quick"))) { Got += P.MillSawBonus(); }   // the water saw's board is given by Craft
		// «Пилорама» (sawmill level 2): +1 board to every cut, the quick one too (the mini-game adds it itself)
		if (R->Id == FName(TEXT("rcp_saw_quick")) && P.GetYardLevel(EFVYardBuilding::Sawmill) >= 2 && P.GiveItem(R->OutputId, 1)) { ++Got; }
		Success(G, FString::Printf(TEXT("Готово: %s ×%d"), *IngredientName(R->OutputId), Got),
			Building == EFVYardBuilding::Forge ? EFVSfx::Anvil : EFVSfx::Saw, YardIcon(Building));
		Refresh(G);
	}

	// ============================================================================================
	// Yard building (sawmill / forge)
	// ============================================================================================
	class FYardScreen final : public FVScreen
	{
	public:
		explicit FYardScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			FString Ctx = Game ? Game->GetScreenContext().ToString() : FString();
			Open.Reset();
			Open.bAuto = TakeExpandContext(Ctx);
			if (Ctx.IsEmpty() || !YardFromId(FName(*Ctx), Building)) { Building = EFVYardBuilding::Sawmill; }
			Col.Reset();
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			const FFVYardBuildingDef& YD = FVDBU6::YardBuilding(Building);
			bool bClose = D.Window(Win, YD.Name.IsEmpty() ? FString(TEXT("Постройка")) : YD.Name);
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			PendingRecipe = nullptr;
			bPendingUpgrade = false;
			if (P)
			{
				const FFVRect Area = ContentArea(Win, Win.Y + (bP ? 16.f : 10.f));
				D.Panel(Area, EFVPanel::Parchment);
				DrawContent(D, *P, YD, Area.Inset(5.f));
			}
			else
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center(), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
			}
			if (bP && D.ActionBar(Win)) { bClose = true; }
			if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); return; }
			if (P) { RunActions(*P); }
		}

	private:
		EFVYardBuilding Building = EFVYardBuilding::Sawmill;
		FScrollColumn Col;
		FOpenRow Open;   // Р77: the opened work row
		const FFVRecipeDef* PendingRecipe = nullptr;
		bool bPendingUpgrade = false;

		void DrawContent(FVDraw& D, UFVSaveGame& P, const FFVYardBuildingDef& YD, const FFVRect& Area)
		{
			const bool bP = FV::IsPortrait();
			const int32 Level = FMath::Max(0, P.GetYardLevel(Building));
			const float W = Area.W - Col.BarRoom() - 4.f;
			const float X = Area.X + 2.f;
			float Y = Col.Begin(D, Area, 4.f);

			// ---- header: the building picture, name, level pips, description
			{
				const float PicS = bP ? 64.f : 48.f;
				const FFVRect Pic(X, Y, PicS, PicS);
				D.Rect(Pic, FV::Col(214, 170, 116));
				if (UFVSprites* Spr = UFVSprites::Get())
				{
					if (Building == EFVYardBuilding::Garden && FVDBU6::Seeds().Num() > 0)
					{
						FVUI::SpriteFit(D, Spr->GardenBedSprite(FVDBU6::Seeds()[0].CropId, 3), Pic.Inset(4.f), FLinearColor::White, 3.f);
					}
					else
					{
						FVUI::SpriteFit(D, Spr->YardBuildingSprite(Building, Level), Pic.Inset(2.f), FLinearColor::White, 1.f);
					}
				}
				const float TX = Pic.Right() + 8.f;
				const float TW = X + W - TX;
				D.Text(Level > 0 ? FString::Printf(TEXT("%s — уровень %d"), *YD.Name, Level) : FString::Printf(TEXT("%s — не построено"), *YD.Name),
					FVector2D(TX, Y), BodyS() + 0.5f, FVPal::Ink(), EFVAlign::Left, false, true);
				float TY = Y + D.LineHeight(BodyS() + 0.5f) + 1.f;
				LevelPips(D, FVector2D(TX, TY), Level, FMath::Max(1, YD.Levels.Num()), bP ? 6.f : 5.f);
				TY += (bP ? 6.f : 5.f) + 3.f;
				const FString Desc = (YD.Levels.IsValidIndex(Level - 1) && !YD.Levels[Level - 1].Description.IsEmpty())
					? YD.Levels[Level - 1].Description : YD.Description;
				TY += TextWrappedLimited(D, Desc, FVector2D(TX, TY), TW, SmallS(), FVPal::InkSoft(), AllLines);   // Р77: in full (the column scrolls)
				Y = FMath::Max(Pic.Bottom(), TY) + 8.f;
			}

			if (Level <= 0)
			{
				Y += TextWrappedLimited(D, TEXT("Постройка ещё не возведена. Её строит Прораб Гаврила — он живёт в лесу."),
					FVector2D(X, Y), W, BodyS(), FVPal::Ink(), AllLines) + 8.f;
			}
			else
			{
				// ---- works
				Y = FVUI::SectionHeader(D, Building == EFVYardBuilding::Forge ? TEXT("Ковка") : TEXT("Работы"), FVector2D(X, Y), W, bP ? 11.f : 9.f) + 2.f;
				const TArray<const FFVRecipeDef*> Recipes = FVDBU6::RecipesForYard(Building);
				if (Recipes.Num() == 0)
				{
					D.Text(TEXT("Здесь пока нечего делать."), FVector2D(X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
					Y += D.LineHeight(SmallS(), false) + 6.f;
				}
				for (const FFVRecipeDef* R : Recipes)
				{
					if (R) { Y = DrawRecipeRow(D, P, *R, Level, X, Y, W); }
				}
				if (Building == EFVYardBuilding::Sawmill && Level >= 2)
				{
					Y += 2.f;
					D.Text(TEXT("Пилорама: +1 доска к каждому распилу"),
						FVector2D(X, Y), SmallS(), InkInfo(), EFVAlign::Left, false, false);
					Y += D.LineHeight(SmallS(), false) + 4.f;
				}
				Y += 6.f;
			}

			// ---- the next level
			Y = FVUI::SectionHeader(D, Level <= 0 ? TEXT("Постройка") : TEXT("Улучшение"), FVector2D(X, Y), W, bP ? 11.f : 9.f) + 2.f;
			if (!YD.Levels.IsValidIndex(Level))
			{
				D.Text(TEXT("Постройка улучшена до предела."), FVector2D(X, Y), SmallS(), InkGood(), EFVAlign::Left, false, true);
				Y += D.LineHeight(SmallS()) + 6.f;
			}
			else if (Level <= 0)
			{
				D.Text(TEXT("Заказать постройку можно у Прораба в лесу."), FVector2D(X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += D.LineHeight(SmallS(), false) + 4.f;
				Y = DrawRequirements(D, P, YD.Levels[0], X, Y, W) + 6.f;
			}
			else
			{
				const FFVStationLevel& Next = YD.Levels[Level];
				D.Text(FString::Printf(TEXT("Ур. %d: %s"), Level + 1, *Next.Name), FVector2D(X, Y), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(BodyS()) + 1.f;
				Y += TextWrappedLimited(D, Next.Description, FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), AllLines) + 3.f;
				Y = DrawRequirements(D, P, Next, X, Y, W) + 4.f;
				const EFVBuyResult Can = P.CanBuildYard(Building);
				const float BH = FV::Ui::ButtonH();
				const FFVRect BR(X, Y, FMath::Min(W, bP ? W : 200.f), BH);
				if (Can == EFVBuyResult::Ok)
				{
					if (CoinButton(D, BR, TEXT("Улучшить"), Next.Price, EFVButton::Primary, true, true, BodyS())) { bPendingUpgrade = true; }
				}
				else
				{
					D.Button(BR, ResultText(Can), EFVButton::Normal, false, SmallS() + 0.5f);
				}
				Y += BH + 8.f;
			}
			Col.End(D, Y, 6.f);
		}

		float DrawRecipeRow(FVDraw& D, UFVSaveGame& P, const FFVRecipeDef& R, int32 Level, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			const float RowH0 = bP ? 52.f : 34.f;
			const FFVRect Row0(X, Y, W, RowH0);   // the closed row: the icon and the button keep their places when it opens
			const float IconS = bP ? 32.f : 22.f;
			const FFVRect IconR(Row0.X + 4.f, FMath::RoundToFloat(Row0.Center().Y - IconS * 0.5f), IconS, IconS);
			const bool bLevelOk = Level >= R.StationLevel;
			const bool bFishOk = P.GetLevel() >= R.MinLevel;
			const EFVBuyResult Can = (bLevelOk && bFishOk) ? P.CanCraft(R) : EFVBuyResult::LevelTooLow;
			const bool bQuick = !R.bSawGame && !R.bForgeGame;
			FString Btn = R.bSawGame ? TEXT("Пилить") : (R.bForgeGame ? TEXT("Ковать") : TEXT("Сделать"));
			if (R.Id == FName(TEXT("rcp_saw_quick"))) { Btn = TEXT("Быстро"); }
			const float BW = bP ? 88.f : 64.f;
			const float BH = bP ? 34.f : 20.f;
			const FFVRect BR(Row0.Right() - BW - 4.f, FMath::RoundToFloat(Row0.Center().Y - BH * 0.5f), BW, BH);
			const float TX = IconR.Right() + 6.f;
			const float TW = BR.X - 6.f - TX;
			FString Title = FString::Printf(TEXT("%s ×%d"), *IngredientName(R.OutputId), FMath::Max(1, R.OutputCount));
			// 0.6.2 (Р80/Р84): a rod recipe — the rod name (no «×1») + its type plate at the right end of the title line
			const FFVRodDef* OutRod = FVDB::FindRod(R.OutputId);
			const float ChipS = bP ? 7.f : 5.5f;
			float ChipW = 0.f;
			if (OutRod)
			{
				Title = OutRod->Name;
				ChipW = FMath::RoundToFloat(D.MeasureText(FVDBU6::RodTypeLabel(*OutRod), ChipS, true).X + 8.f);
			}
			const float TitleW = OutRod ? FMath::Max(20.f, TW - ChipW - 4.f) : TW;
			const bool bOwned = bLevelOk && bFishOk && Can == EFVBuyResult::AlreadyOwned;
			const bool bLocked = bLevelOk && bFishOk && Can == EFVBuyResult::Locked;
			// boards per log: + «Пилорама» (sawmill level 2) + the mill's water saw
			const int32 SawBonus = (P.GetYardLevel(EFVYardBuilding::Sawmill) >= 2 ? 1 : 0) + P.MillSawBonus();
			if (R.Id == FName(TEXT("rcp_saw_quick"))) { Title = FString::Printf(TEXT("Быстрый распил: %s ×%d"), *IngredientName(R.OutputId), FMath::Max(1, R.OutputCount) + SawBonus); }
			else if (R.bSawGame) { Title = FString::Printf(TEXT("Распил: %s ×%d–%d"), *IngredientName(R.OutputId), 2 + SawBonus, 4 + SawBonus); }
			FString Sub;
			if (!bLevelOk) { Sub = FString::Printf(TEXT("Нужна постройка ур. %d"), R.StationLevel); }
			else if (!bFishOk) { Sub = FString::Printf(TEXT("Нужен уровень рыбака %d"), R.MinLevel); }
			else
			{
				Sub = RecipeInputsText(R.Inputs);
				if (R.CoinCost > 0) { Sub += FString::Printf(TEXT(" + %s"), *FV::Money(R.CoinCost)); }
				if (R.bSawGame) { Sub += TEXT(" · мини-игра: ровный рез — больше досок"); }
				else if (R.bForgeGame) { Sub += TEXT(" · мини-игра «Ковка»"); }
			}
			const FLinearColor SubC = (bLevelOk && bFishOk) ? ((Can == EFVBuyResult::Ok || bOwned || bLocked) ? FVPal::InkSoft() : InkBad()) : InkWarn();
			// 0.6.2 (Р80): the recipe is closed until an achievement — a lock plate «Достижение «…»: 7 / 10» under the cost
			const float LockS = bP ? 7.f : 5.5f;
			FString LockLabel;
			if (bLocked)
			{
				const FFVAchievementDef* A = FVDB::FindAchievement(R.RequiredAchievement);
				LockLabel = A ? FString::Printf(TEXT("Достижение «%s»: %s"), *A->Name, *P.AchievementProgressText(*A))
					: FString(TEXT("Нужно достижение"));
				LockLabel = D.Ellipsize(LockLabel, FMath::Max(20.f, TW - 21.f), LockS, true);
			}
			const float LockH = bLocked ? FMath::RoundToFloat(D.MeasureText(TEXT("Щ"), LockS, true).Y + 4.f) : 0.f;
			// Р77: the text does not fit → a tap opens the row (the whole title and the whole cost / note, wrapped)
			const bool bCut = !Fits(D, Title, TitleW, BodyS(), true) || !Fits(D, Sub, TW, SmallS(), false);
			Open.Auto(R.Id, bCut);
			const bool bOpen = bCut && Open.Id == R.Id;
			const float TY0 = Y + (bP ? 5.f : 3.f);
			const float SubGap = D.LineHeight(BodyS()) + (bP ? 2.f : 0.f);
			float SubY = TY0 + SubGap;
			float RowH = RowH0;
			if (bOpen)
			{
				SubY = TY0 + MeasureFull(D, Title, TitleW, BodyS(), true) - WrapStep(D, BodyS(), true) + SubGap;
				RowH = FMath::Max(RowH0, FMath::CeilToFloat(SubY + MeasureFull(D, Sub, TW, SmallS(), false) - Y) + (bP ? 10.f : 6.f));
			}
			const float LockY = FMath::RoundToFloat(SubY + (bOpen ? MeasureFull(D, Sub, TW, SmallS(), false) : D.LineHeight(SmallS(), false)) + 2.f);
			if (bLocked) { RowH = FMath::Max(RowH, FMath::CeilToFloat(LockY + LockH - Y) + (bP ? 6.f : 4.f)); }
			const FFVRect Row(X, Y, W, RowH);
			D.Rect(Row, FV::Col(214, 162, 104));
			D.Rect(Row.Inset(1.f), RowFill());
			DrawItemIcon(D, R.OutputId, IconR, 2.f);
			if (bOpen)
			{
				DrawFull(D, Title, FVector2D(TX, TY0), TitleW, BodyS(), FVPal::Ink(), true);
				DrawFull(D, Sub, FVector2D(TX, SubY), TW, SmallS(), SubC, false);
			}
			else
			{
				D.Text(D.Ellipsize(Title, TitleW, BodyS()), FVector2D(TX, TY0), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(D.Ellipsize(Sub, TW, SmallS(), false), FVector2D(TX, SubY), SmallS(), SubC, EFVAlign::Left, false, false);
			}
			if (OutRod)
			{
				const float ChipH = FMath::RoundToFloat(D.MeasureText(TEXT("Щ"), ChipS, true).Y + 3.f);
				FVUI::RodTypeChip(D, *OutRod, FVector2D(TX + TW - ChipW, TY0 + FMath::RoundToFloat((D.LineHeight(BodyS()) - ChipH) * 0.5f)), ChipS);
			}
			if (bLocked)
			{
				const float LockW = FMath::RoundToFloat(D.MeasureText(LockLabel, LockS, true).X + 10.f + 11.f);
				FVUI::StatusChip(D, LockLabel, FVector2D(TX + LockW, LockY), FVUI::EChip::Bad, true, LockS);
			}
			if (bCut) { ExpandMark(D, BR.X, Row.Bottom(), bOpen); }
			if (bOwned)
			{
				// 0.6.2 (Р80): the rod is already owned — a «Есть» plate in the button's place
				const float OwnS = bP ? FV::Ui::Small() : 6.5f;
				const float OwnH = FMath::RoundToFloat(D.MeasureText(TEXT("Есть"), OwnS, true).Y + 4.f);
				FVUI::StatusChip(D, TEXT("Есть"), FVector2D(BR.Right(), FMath::RoundToFloat(BR.Center().Y - OwnH * 0.5f)), FVUI::EChip::Good, false, OwnS);
			}
			else if (D.Button(BR, Btn, bQuick ? EFVButton::Normal : EFVButton::Primary, bLevelOk && bFishOk && !bLocked, bP ? FV::Ui::Body() : 7.5f))
			{
				PendingRecipe = &R;
			}
			Open.Keep(Col, R.Id, Y, RowH);
			// a tap on the row (not on its button) opens / closes it
			if (D.Clicked(Row) && (bCut || !Open.Id.IsNone()))
			{
				Open.Toggle(R.Id, bCut);
				FVUI::Sfx(EFVSfx::Click);
			}
			return Y + RowH + 4.f;
		}

		void RunActions(UFVSaveGame& P)
		{
			UFVGame* G = Game;
			if (!G) { return; }
			if (bPendingUpgrade)
			{
				bPendingUpgrade = false;
				const EFVBuyResult Res = P.BuildYard(Building);
				if (Res != EFVBuyResult::Ok) { Fail(G, Res); return; }
				Success(G, FString::Printf(TEXT("%s: уровень %d"), *FVDBU6::YardBuilding(Building).Name, P.GetYardLevel(Building)), EFVSfx::Hammer, EFVIcon::Hammer);
				Refresh(G);
				return;
			}
			const FFVRecipeDef* R = PendingRecipe;
			PendingRecipe = nullptr;
			if (!R) { return; }
			const EFVBuyResult Can = P.CanCraft(*R);
			if (Can != EFVBuyResult::Ok) { Fail(G, Can); return; }
			// 0.6.2 (Р82): перековка gives the old rod up, so ask first (before the forge mini-game too)
			if (const FFVRodDef* OldRod = ReforgedRod(*R))
			{
				const FFVRodDef* NewRod = FVDB::FindRod(R->OutputId);
				FString Body = FString::Printf(TEXT("Удочка %s будет перекована в %s."), *RodShort(*OldRod),
					NewRod ? *RodShort(*NewRod) : *IngredientName(R->OutputId));
				if (FVDBU6::IsRodSold(*OldRod)) { Body += FString::Printf(TEXT(" %s можно снова купить у Кузьмы."), *RodShort(*OldRod)); }
				Body += TEXT(" Снасти с неё вернутся в рюкзак (полон — в сундук).\nПерековать?");
				TWeakObjectPtr<UFVGame> Weak(G);
				const EFVYardBuilding B = Building;
				G->Confirm(TEXT("Перековать удочку?"), Body, [Weak, R, B]()
				{
					StartWork(Weak.Get(), R, B);
				}, TEXT("Перековать"), TEXT("Отмена"));
				return;
			}
			StartWork(G, R, Building);
		}
	};

	// ============================================================================================
	// Garden bed
	// ============================================================================================
	class FBedScreen final : public FVScreen
	{
	public:
		explicit FBedScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			Bed = 0;
			FString Ctx = Game ? Game->GetScreenContext().ToString() : FString();
			Open.Reset();
			Open.bAuto = TakeExpandContext(Ctx);
			FString Num;
			if (Ctx.Split(TEXT("_"), nullptr, &Num, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) { Bed = FMath::Max(0, FCString::Atoi(*Num)); }
			Col.Reset();
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			bool bClose = D.Window(Win, FString::Printf(TEXT("Грядка %d"), Bed + 1));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			PendingSeed = NAME_None;
			bPendingHarvest = false;
			if (P)
			{
				const FFVRect Area = ContentArea(Win, Win.Y + (bP ? 16.f : 10.f));
				D.Panel(Area, EFVPanel::Parchment);
				DrawContent(D, *P, Area.Inset(5.f));
			}
			if (bP && D.ActionBar(Win)) { bClose = true; }
			if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); return; }
			if (P) { RunActions(*P); }
		}

	private:
		int32 Bed = 0;
		FScrollColumn Col;
		FOpenRow Open;   // Р77: the opened seed row
		FName PendingSeed;
		bool bPendingHarvest = false;

		void DrawBedPicture(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			D.Rect(R, FV::Col(150, 108, 64));
			D.Rect(R.Inset(2.f), FV::Col(120, 84, 50));
			UFVSprites* Spr = UFVSprites::Get();
			if (!Spr) { return; }
			const float G = P.GetBedGrowth(Bed);
			FName Crop;
			int32 Stage = 0;
			if (G >= 0.f && P.U6.Garden.IsValidIndex(Bed))
			{
				const FFVSeedDef* SD = FVDBU6::FindSeed(P.U6.Garden[Bed].Seed);
				Crop = SD ? SD->CropId : P.U6.Garden[Bed].Seed;
				Stage = G >= 1.f ? 3 : FMath::Clamp(int32(G * 3.f), 0, 2);
			}
			FVUI::SpriteFit(D, Spr->GardenBedSprite(Crop, Stage), R.Inset(4.f), FLinearColor::White, 5.f);
		}

		void DrawContent(FVDraw& D, UFVSaveGame& P, const FFVRect& Area)
		{
			const bool bP = FV::IsPortrait();
			const float W = Area.W - Col.BarRoom() - 4.f;
			const float X = Area.X + 2.f;
			float Y = Col.Begin(D, Area, 4.f);

			if (Bed >= P.GardenBeds())
			{
				Y += TextWrappedLimited(D, TEXT("Эта грядка ещё не вскопана. Улучшите огород у Прораба Гаврилы, чтобы грядок стало больше."),
					FVector2D(X, Y), W, BodyS(), FVPal::Ink(), AllLines) + 8.f;
				Col.End(D, Y, 6.f);
				return;
			}

			const float G = P.GetBedGrowth(Bed);
			const float PicH = bP ? 120.f : 72.f;
			const FFVRect Pic(X, Y, bP ? W : 150.f, PicH);
			DrawBedPicture(D, P, Pic);
			float TY = bP ? Pic.Bottom() + 8.f : Y;
			const float TX = bP ? X : Pic.Right() + 10.f;
			const float TW = bP ? W : (X + W - TX);

			if (G < 0.f)
			{
				D.Text(TEXT("Грядка пустая"), FVector2D(TX, TY), BodyS() + 0.5f, FVPal::Ink(), EFVAlign::Left, false, true);
				TY += D.LineHeight(BodyS() + 0.5f) + 2.f;
				TY += TextWrappedLimited(D, TEXT("Выберите, что посадить. Растёт, пока вы играете (сон не ускоряет), в дождь — быстрее."),
					FVector2D(TX, TY), TW, SmallS(), FVPal::InkSoft(), AllLines);
				Y = FMath::Max(Pic.Bottom(), TY) + 8.f;
				Y = FVUI::SectionHeader(D, TEXT("Семена"), FVector2D(X, Y), W, bP ? 11.f : 9.f) + 2.f;
				for (const FFVSeedDef& SD : FVDBU6::Seeds())
				{
					Y = DrawSeedRow(D, P, SD, X, Y, W);
				}
				Col.End(D, Y, 6.f);
				return;
			}

			FString Crop;
			if (P.U6.Garden.IsValidIndex(Bed))
			{
				const FFVSeedDef* CropSeed = FVDBU6::FindSeed(P.U6.Garden[Bed].Seed);
				Crop = IngredientName(CropSeed ? CropSeed->CropId : P.U6.Garden[Bed].Seed);
			}
			D.Text(FString::Printf(TEXT("Растёт: %s"), *Crop), FVector2D(TX, TY), BodyS() + 0.5f, FVPal::Ink(), EFVAlign::Left, false, true);
			TY += D.LineHeight(BodyS() + 0.5f) + 3.f;
			const float BarH = bP ? 12.f : 8.f;
			D.Bar(FFVRect(TX, TY, TW, BarH), FMath::Clamp(G, 0.f, 1.f), G >= 1.f ? FV::Col(120, 200, 70) : FV::Col(90, 170, 60));
			TY += BarH + 3.f;
			if (G >= 1.f)
			{
				D.Text(TEXT("Урожай созрел!"), FVector2D(TX, TY), SmallS(), InkGood(), EFVAlign::Left, false, true);
			}
			else
			{
				const FFVSeedDef* SD = P.U6.Garden.IsValidIndex(Bed) ? FVDBU6::FindSeed(P.U6.Garden[Bed].Seed) : nullptr;
				const int32 Left = SD ? FMath::Max(1, FMath::CeilToInt((1.f - G) * SD->GrowMinutes)) : 0;
				const FString T = SD ? FString::Printf(TEXT("%d%% · осталось ~%d мин игры"), FMath::RoundToInt(G * 100.f), Left)
					: FString::Printf(TEXT("%d%%"), FMath::RoundToInt(G * 100.f));
				D.Text(T, FVector2D(TX, TY), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			TY += D.LineHeight(SmallS()) + 8.f;
			Y = FMath::Max(Pic.Bottom(), TY) + 6.f;
			const float BH = FV::Ui::ButtonH();
			const FFVRect BR(X, Y, bP ? W : FMath::Min(W, 220.f), BH);
			if (D.Button(BR, G >= 1.f ? TEXT("Собрать урожай") : TEXT("Ещё растёт"), EFVButton::Primary, G >= 1.f, BodyS())) { bPendingHarvest = true; }
			Y += BH + 8.f;
			// the growth stages: what the bed will look like (the current one is framed)
			if (UFVSprites* Spr = UFVSprites::Get())
			{
				Y = FVUI::SectionHeader(D, TEXT("Как растёт"), FVector2D(X, Y), W, bP ? 11.f : 9.f) + 4.f;
				FName CropId;
				if (P.U6.Garden.IsValidIndex(Bed))
				{
					const FFVSeedDef* SD = FVDBU6::FindSeed(P.U6.Garden[Bed].Seed);
					CropId = SD ? SD->CropId : P.U6.Garden[Bed].Seed;
				}
				const int32 Cur = G >= 1.f ? 3 : FMath::Clamp(int32(G * 3.f), 0, 2);
				static const TCHAR* StageNames[4] = { TEXT("Посажено"), TEXT("Всходы"), TEXT("Растёт"), TEXT("Созрело") };
				const float Gap = 4.f;
				const float CW = FMath::Min(bP ? 200.f : 90.f, (W - Gap * 3.f) / 4.f);
				const float CH = CW * 0.8f;
				for (int32 k = 0; k < 4; ++k)
				{
					const FFVRect C(X + float(k) * (CW + Gap), Y, CW, CH);
					D.Rect(C, k == Cur ? FV::Col(120, 170, 70) : FV::Col(200, 160, 110));
					D.Rect(C.Inset(k == Cur ? 2.f : 1.f), FV::Col(150, 108, 64));
					FVUI::SpriteFit(D, Spr->GardenBedSprite(CropId, k), C.Inset(3.f), k <= Cur ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.55f), 3.f);
					D.Text(D.Ellipsize(StageNames[k], CW, SmallS(), false), FVector2D(C.Center().X, C.Bottom() + 2.f), SmallS(), k == Cur ? FVPal::Ink() : FVPal::InkSoft(),
						EFVAlign::Center, false, k == Cur);
				}
				Y += CH + D.LineHeight(SmallS()) + 8.f;
			}
			Col.End(D, Y, 6.f);
		}

		float DrawSeedRow(FVDraw& D, UFVSaveGame& P, const FFVSeedDef& SD, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			const float RowH0 = bP ? 50.f : 32.f;
			const FFVRect Row0(X, Y, W, RowH0);   // the closed row: the icon and the button keep their places when it opens
			const int32 Have = P.CountItem(SD.SeedId);
			const bool bLevel = P.GetLevel() >= SD.MinLevel;
			const float IconS = bP ? 30.f : 22.f;
			const FFVRect IconR(Row0.X + 4.f, FMath::RoundToFloat(Row0.Center().Y - IconS * 0.5f), IconS, IconS);
			const float BW = bP ? 96.f : 70.f, BH = bP ? 34.f : 20.f;
			const FFVRect BR(Row0.Right() - BW - 4.f, FMath::RoundToFloat(Row0.Center().Y - BH * 0.5f), BW, BH);
			const float TX = IconR.Right() + 6.f;
			const float TW = BR.X - 6.f - TX;
			const FString Title = FString::Printf(TEXT("%s (есть: %d)"), *IngredientName(SD.SeedId), Have);
			const FString Yield = SD.YieldMin == SD.YieldMax ? FString::Printf(TEXT("%d"), SD.YieldMin) : FString::Printf(TEXT("%d–%d"), SD.YieldMin, SD.YieldMax);
			FString Sub = FString::Printf(TEXT("→ %s ×%s · %d мин игры"), *IngredientName(SD.CropId), *Yield, FMath::RoundToInt(SD.GrowMinutes));
			if (SD.SeedsMax > 0) { Sub += TEXT(" · бывают семена"); }
			if (!bLevel) { Sub = FString::Printf(TEXT("Нужен уровень %d"), SD.MinLevel); }
			const FLinearColor SubC = bLevel ? FVPal::InkSoft() : InkWarn();
			// Р77: the text does not fit → a tap opens the row (the whole name and the whole harvest line, wrapped)
			const bool bCut = !Fits(D, Title, TW, BodyS(), true) || !Fits(D, Sub, TW, SmallS(), false);
			Open.Auto(SD.SeedId, bCut);
			const bool bOpen = bCut && Open.Id == SD.SeedId;
			const float TY0 = Y + (bP ? 5.f : 3.f);
			const float SubGap = D.LineHeight(BodyS()) + (bP ? 2.f : 0.f);
			float SubY = TY0 + SubGap;
			float RowH = RowH0;
			if (bOpen)
			{
				SubY = TY0 + MeasureFull(D, Title, TW, BodyS(), true) - WrapStep(D, BodyS(), true) + SubGap;
				RowH = FMath::Max(RowH0, FMath::CeilToFloat(SubY + MeasureFull(D, Sub, TW, SmallS(), false) - Y) + (bP ? 10.f : 6.f));
			}
			const FFVRect Row(X, Y, W, RowH);
			D.Rect(Row, FV::Col(214, 162, 104));
			D.Rect(Row.Inset(1.f), Have > 0 ? RowFill() : FV::Col(238, 214, 176));
			DrawItemIcon(D, SD.SeedId, IconR, 2.f, Have > 0 ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.5f));
			if (bOpen)
			{
				DrawFull(D, Title, FVector2D(TX, TY0), TW, BodyS(), FVPal::Ink(), true);
				DrawFull(D, Sub, FVector2D(TX, SubY), TW, SmallS(), SubC, false);
			}
			else
			{
				D.Text(D.Ellipsize(Title, TW, BodyS()), FVector2D(TX, TY0), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(D.Ellipsize(Sub, TW, SmallS(), false), FVector2D(TX, SubY), SmallS(), SubC, EFVAlign::Left, false, false);
			}
			if (bCut) { ExpandMark(D, BR.X, Row.Bottom(), bOpen); }
			if (D.Button(BR, TEXT("Посадить"), EFVButton::Primary, Have > 0 && bLevel, bP ? FV::Ui::Body() : 7.5f)) { PendingSeed = SD.SeedId; }
			Open.Keep(Col, SD.SeedId, Y, RowH);
			// a tap on the row (not on its button) opens / closes it
			if (D.Clicked(Row) && (bCut || !Open.Id.IsNone()))
			{
				Open.Toggle(SD.SeedId, bCut);
				FVUI::Sfx(EFVSfx::Click);
			}
			return Y + RowH + 4.f;
		}

		void RunActions(UFVSaveGame& P)
		{
			UFVGame* G = Game;
			if (!G) { return; }
			if (!PendingSeed.IsNone())
			{
				const FName Seed = PendingSeed;
				PendingSeed = NAME_None;
				const EFVBuyResult Res = P.PlantSeed(Bed, Seed);
				if (Res != EFVBuyResult::Ok) { Fail(G, Res); return; }
				Success(G, FString::Printf(TEXT("Посажено: %s"), *IngredientName(Seed)), EFVSfx::Plant, EFVIcon::Seed);
				Refresh(G);
				G->CloseScreen();
				return;
			}
			if (bPendingHarvest)
			{
				bPendingHarvest = false;
				TArray<FFVIngredient> Loot;
				const EFVBuyResult Res = P.HarvestBed(Bed, Loot);
				if (Res != EFVBuyResult::Ok) { Fail(G, Res); return; }
				Success(G, FString::Printf(TEXT("Урожай: %s"), *LootText(Loot)), EFVSfx::Harvest, EFVIcon::Seed);
				Refresh(G);
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeYardBuildingScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenYardImpl::FYardScreen>(Game);
}

TUniquePtr<FVScreen> FVMakeGardenBedScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenYardImpl::FBedScreen>(Game);
}
