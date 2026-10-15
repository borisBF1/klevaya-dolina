// UPDATE 6 [SCREEN-YARD] — the builder Гаврила: yard buildings, yard decor, furniture (Docs/UPDATE6_FOREST_CITY.md §4, §6, §6.4). Owner: SCREEN-YARD.
// Context = trader id (tr_builder). Three sliding pages (tabs / swipe / Left-Right): «Постройки» (sawmill, garden,
// forge: CanBuildYard / BuildYard with materials and conditions), «Двор» (yard decor slots) and «Мебель» (furniture
// slots): BuyDecor / EquipDecor / UnequipDecor with DecorIcon. Both orientations.
// Р61: a tapped decor row is TRIED ON (FVDecorPreview: the real yard / room fragment with the item at its place) —
// portrait: a big block above the list; landscape: the right panel. Context may also be a decor id (the builder opens
// on its page with it selected: autotest shots, links).
// Р77: a decor row whose text does not fit (or whose status hides the description) opens on a tap: the whole name,
// status and description wrapped, one open row per page. Context "fv_expand" (alone or "<decor id>:fv_expand") opens
// the Yard page (or the decor's page) with its first such row opened and tried on (autotest shot). The landscape info
// panel and the «Постройки» cards show the whole description.

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVDecorPreview.h"

namespace FVScreenBuilderImpl
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

	/** The context "fv_expand" (alone or joined to a decor id: "decor_yard_roof_copper:fv_expand") opens the first row
	    whose text does not fit (autotest shots). Strips it from Ctx; returns whether it was there. */
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

	enum class EPage : int32 { Buildings, Yard, Furniture, Count };
	enum class EAct : uint8 { None, Build, Buy, Equip, Unequip };

	class FBuilderScreen final : public FVScreen
	{
	public:
		explicit FBuilderScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			FString Ctx = Game ? Game->GetScreenContext().ToString() : FString();
			const bool bExpand = TakeExpandContext(Ctx);   // Р77
			TraderId = Ctx.IsEmpty() ? NAME_None : FName(*Ctx);
			for (FName& S : SelId) { S = NAME_None; }
			for (uint8& E : EnsureSel) { E = 0; }
			for (FOpenRow& O : Opens) { O.Reset(); }
			for (FScrollColumn& C : InfoCols) { C.Reset(); }
			for (FName& I : InfoFor) { I = NAME_None; }
			int32 StartPage = 0;
			if (const FFVDecorDef* Pre = FVDB::FindDecor(TraderId))
			{
				// context = a decor id: open its page with it selected (tried on)
				StartPage = FVDecor::IsYardSlot(Pre->Slot) ? int32(EPage::Yard) : int32(EPage::Furniture);
				SelId[StartPage] = Pre->Id;
				EnsureSel[StartPage] = 2;
				TraderId = NAME_None;
			}
			if (TraderId.IsNone()) { TraderId = FName(TEXT("tr_builder")); }
			if (bExpand)
			{
				// Р77: the decor pages have the rows that open: the Yard one with its first such row, or the given decor's row
				FOpenRow& O = Opens[StartPage == int32(EPage::Buildings) ? int32(EPage::Yard) : StartPage];
				if (StartPage == int32(EPage::Buildings)) { StartPage = int32(EPage::Yard); O.bAuto = true; }
				else { O.Id = SelId[StartPage]; O.Show = 2; O.bInstant = true; }
			}
			const FFVTraderDef* Tr = FVDB::FindTrader(TraderId);
			TraderMask = Tr ? Tr->Mask : uint8(8);
			for (FScrollColumn& C : Cols) { C.Reset(); }
			Pager.NumPagesCached = int32(EPage::Count);
			Pager.SetPage(StartPage);
			TickCount = 0;
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			PagerKeys(In, Pager);
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			bool bClose = D.Window(Win, TEXT("Прораб Гаврила"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Act = EAct::None;
			if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center(), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { bClose = true; }
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}

			const float X = Win.X + (bP ? 10.f : 12.f);
			const float W = Win.W - (bP ? 20.f : 24.f);
			float Y = Win.Y + (bP ? 16.f : 10.f);
			{
				DrawIcon(D, EFVIcon::Hammer, FFVRect(X, Y - 1.f, 18.f, 18.f), 1.f);
				const float MoneyW = D.MoneyLabel(FVector2D(X + W, Y), P->Money, bP ? FV::Ui::Body() : 9.f, EFVAlign::Right);
				const float QuoteS = bP ? FV::Ui::Small() : 7.f;
				const FString Quote = TEXT("«Построю что надо — были бы брёвна да монеты»");
				const float QuoteW = FMath::Max(40.f, W - 22.f - MoneyW - 8.f);
				float BandH = bP ? 22.f : 18.f;
				if (!Fits(D, Quote, QuoteW, QuoteS, false))
				{
					// Р77: the narrow (portrait) header wraps the whole quote instead of cutting it: two lines fit its band,
					// past that (a very long money label) the band grows and the tabs move down
					const float QuoteH = TextWrappedLimited(D, Quote, FVector2D(X + 22.f, Y), QuoteW, QuoteS, FVPal::InkSoft(), AllLines);
					BandH = FMath::Max(BandH, QuoteH + 5.f);
				}
				else
				{
					D.Text(Quote, FVector2D(X + 22.f, Y), QuoteS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
				Y += BandH;
			}
			TArray<FString> Labels = { TEXT("Постройки"), TEXT("Двор"), TEXT("Мебель") };
			FFVRect Area;
			if (bP)
			{
				Y += PagerTabs(D, Labels, Pager, X, Y, W);
				const float Bottom = FVDraw::ActionBarRect(Win).Y - 8.f;
				Area = FFVRect(Win.X + 10.f, Y, Win.W - 20.f, FMath::Max(60.f, Bottom - Y));
			}
			else
			{
				Y += 16.f;   // landscape folder tabs stand above Y — keep them off the quote line
				PagerTabs(D, Labels, Pager, Win.X + 10.f, Y, Win.W - 20.f);
				Area = FFVRect(Win.X + 10.f, Y, Win.W - 20.f, Win.Bottom() - 10.f - Y);
			}
			Pager.Update(D, Area, int32(EPage::Count));
			Pager.DrawPages(D, Area, [&](int32 Page) { DrawPage(D, *P, Page, Area); });

			if (bP && D.ActionBar(Win)) { bClose = true; }
			if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); return; }
			RunAct(*P);
		}

	private:
		FName TraderId;
		uint8 TraderMask = 8;
		FFVPager Pager;
		FScrollColumn Cols[int32(EPage::Count)];
		int32 TickCount = 0;
		EAct Act = EAct::None;
		FName ActId;
		EFVYardBuilding ActBuilding = EFVYardBuilding::Sawmill;
		FName SelId[int32(EPage::Count)];          // Р61: the tried decor item of the Yard / Furniture pages
		uint8 EnsureSel[int32(EPage::Count)] = {};  // 2 = scroll the list to the selected row once it has a height
		FOpenRow Opens[int32(EPage::Count)];        // Р77: the opened decor row of each page
		FScrollColumn InfoCols[int32(EPage::Count)]; // Р77: landscape info panel: the description scrolls if half the panel is not enough
		FName InfoFor[int32(EPage::Count)];         // the item InfoCols[] were scrolled for

		void Want(EAct A, FName Id) { if (Act == EAct::None) { Act = A; ActId = Id; } }

		void DrawPage(FVDraw& D, UFVSaveGame& P, int32 Page, const FFVRect& Area)
		{
			if (Page != int32(EPage::Buildings)) { DrawDecorPage(D, P, Page, Area); return; }
			D.Panel(Area, EFVPanel::Parchment);
			const FFVRect In = Area.Inset(5.f);
			FScrollColumn& Col = Cols[int32(EPage::Buildings)];
			const float W = In.W - Col.BarRoom() - 4.f;
			const float X = In.X + 2.f;
			float Y = Col.Begin(D, In, 4.f);
			for (int32 b = 0; b < int32(EFVYardBuilding::Count); ++b) { Y = DrawBuildingCard(D, P, EFVYardBuilding(b), X, Y, W); }
			Col.End(D, Y, 6.f);
		}

		/** The decor sold here (or owned) for the Yard / Furniture page, grouped by slot in slot order. */
		void DecorGroups(const UFVSaveGame& P, bool bYard, TArray<TPair<EFVDecorSlot, TArray<const FFVDecorDef*>>>& Out) const
		{
			Out.Reset();
			for (int32 s = FVDecor::ClassicSlots; s < int32(EFVDecorSlot::Count); ++s)
			{
				const EFVDecorSlot Slot = EFVDecorSlot(s);
				if (bYard ? !FVDecor::IsYardSlot(Slot) : !FVDecor::IsFurnitureSlot(Slot)) { continue; }
				TArray<const FFVDecorDef*> Items;
				for (const FFVDecorDef* Dd : FVDB::DecorFor(Slot))
				{
					if (Dd && ((Dd->TraderMask & TraderMask) != 0 || P.OwnsDecor(Dd->Id))) { Items.Add(Dd); }
				}
				if (Items.Num() > 0) { Out.Emplace(Slot, MoveTemp(Items)); }
			}
		}

		/** Р61: the tried item of a page (kept by id; defaults to the first equipped item, else the first one). */
		const FFVDecorDef* SelectedOf(const UFVSaveGame& P, int32 Page, const TArray<TPair<EFVDecorSlot, TArray<const FFVDecorDef*>>>& Groups)
		{
			const FFVDecorDef* First = nullptr;
			const FFVDecorDef* Equipped = nullptr;
			for (const TPair<EFVDecorSlot, TArray<const FFVDecorDef*>>& G : Groups)
			{
				for (const FFVDecorDef* Dd : G.Value)
				{
					if (Dd->Id == SelId[Page]) { return Dd; }
					if (!First) { First = Dd; }
					if (!Equipped && P.GetEquippedDecor(Dd->Slot) == Dd->Id) { Equipped = Dd; }
				}
			}
			const FFVDecorDef* Pick = Equipped ? Equipped : First;
			SelId[Page] = Pick ? Pick->Id : NAME_None;
			return Pick;
		}

		void DrawDecorPage(FVDraw& D, UFVSaveGame& P, int32 Page, const FFVRect& Area)
		{
			const bool bP = FV::IsPortrait();
			const bool bYard = Page == int32(EPage::Yard);
			TArray<TPair<EFVDecorSlot, TArray<const FFVDecorDef*>>> Groups;
			DecorGroups(P, bYard, Groups);
			if (Groups.Num() == 0)
			{
				D.Panel(Area, EFVPanel::Parchment);
				EmptyState(D, FFVRect(Area.X + 5.f, Area.Y + 9.f, Area.W - 10.f, 90.f), bYard ? EFVIcon::House : EFVIcon::Hammer, TEXT("Прораб пока ничего не предлагает."), SmallS());
				return;
			}
			const FFVDecorDef* Sel = SelectedOf(P, Page, Groups);
			// the try-on: portrait = a big block above the list, landscape = the right panel (name + description under it)
			FFVRect ListR = Area;
			if (bP)
			{
				const float PH = FMath::Clamp(FMath::RoundToFloat(Area.H * 0.42f), 150.f, 250.f);
				if (Sel) { FVDecorPreview::Draw(D, FFVRect(Area.X, Area.Y, Area.W, PH), *Sel, &P); }
				ListR = FFVRect(Area.X, Area.Y + PH + 6.f, Area.W, FMath::Max(60.f, Area.H - PH - 6.f));
			}
			else
			{
				ListR = FFVRect(Area.X, Area.Y, FMath::RoundToFloat(Area.W * 0.54f), Area.H);
				const FFVRect RightR(ListR.Right() + 6.f, Area.Y, Area.Right() - ListR.Right() - 6.f, Area.H);
				D.Panel(RightR, EFVPanel::Parchment);
				const FFVRect In = RightR.Inset(6.f);
				if (Sel)
				{
					// Р77: the whole description: past two lines the info grows (the preview gives way, down to half the
					// panel), past half the panel it scrolls
					const int32 Pg = FMath::Clamp(Page, 0, int32(EPage::Count) - 1);
					const float DescH = MeasureFull(D, Sel->Description, In.W, SmallS(), false);
					const float Extra = FMath::Max(0.f, DescH - 2.f * WrapStep(D, SmallS(), false));
					const float InfoCap = FMath::Max(36.f, FMath::RoundToFloat(In.H * 0.5f));
					const float InfoH = FMath::Min(36.f + Extra, InfoCap);
					FVDecorPreview::Draw(D, FFVRect(In.X, In.Y, In.W, In.H - InfoH), *Sel, &P);
					float TY = In.Bottom() - InfoH + 4.f;
					D.Text(D.Ellipsize(Sel->Name, In.W, BodyS()), FVector2D(In.X, TY), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
					TY += D.LineHeight(BodyS()) + 1.f;
					if (36.f + Extra <= InfoCap)
					{
						DrawFull(D, Sel->Description, FVector2D(In.X, TY), In.W, SmallS(), FVPal::InkSoft(), false);
					}
					else
					{
						FScrollColumn& IC = InfoCols[Pg];
						if (InfoFor[Pg] != Sel->Id) { IC.Reset(); InfoFor[Pg] = Sel->Id; }
						const FFVRect DescR(In.X, TY, In.W, FMath::Max(10.f, In.Bottom() - TY));
						float DY = IC.Begin(D, DescR);
						DY += DrawFull(D, Sel->Description, FVector2D(In.X, DY), DescR.W - IC.BarRoom() - 2.f, SmallS(), FVPal::InkSoft(), false);
						IC.End(D, DY, 2.f);
					}
				}
			}
			D.Panel(ListR, EFVPanel::Parchment);
			const FFVRect In = ListR.Inset(5.f);
			FScrollColumn& Col = Cols[FMath::Clamp(Page, 0, int32(EPage::Count) - 1)];
			const float W = In.W - Col.BarRoom() - 4.f;
			const float X = In.X + 2.f;
			const float Y0 = Col.Begin(D, In, 4.f);
			float Y = Y0;
			for (const TPair<EFVDecorSlot, TArray<const FFVDecorDef*>>& G : Groups)
			{
				Y = FVUI::SectionHeader(D, FVDB::DecorSlotName(G.Key), FVector2D(X, Y), W, bP ? 11.f : 9.f) + 2.f;
				for (const FFVDecorDef* Dd : G.Value)
				{
					const bool bSel = Sel == Dd;
					const float RowY = Y;
					Y = DrawDecorRow(D, P, *Dd, X, Y, W, bSel, Page);
					if (bSel && EnsureSel[Page] != 0 && Col.ContentH > 0.f)
					{
						Col.View.ScrollToShow(RowY - Y0, Y - RowY, EnsureSel[Page] == 2);   // the row + its gap (it may be opened)
						EnsureSel[Page] = 0;
					}
				}
				Y += 4.f;
			}
			Col.End(D, Y, 6.f);
		}

		float DrawBuildingCard(FVDraw& D, UFVSaveGame& P, EFVYardBuilding B, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			const FFVYardBuildingDef& YD = FVDBU6::YardBuilding(B);
			const int32 Level = FMath::Max(0, P.GetYardLevel(B));
			const int32 MaxL = YD.Levels.Num();
			const float Y0 = Y;
			const float PicS = bP ? 56.f : 46.f;
			const FFVRect Pic(X + 4.f, Y + 4.f, PicS, PicS);
			// measure first: the card background goes under everything
			const bool bMax = !YD.Levels.IsValidIndex(Level);
			const FFVStationLevel* Next = bMax ? nullptr : &YD.Levels[Level];
			const float RH = ReqRowH(D, SmallS());
			const int32 ReqRows = Next ? (Next->Materials.Num() + (Next->Price > 0 ? 1 : 0) + 1 + ((YD.bNeedsBlockageCleared && Level == 0) ? 1 : 0)) : 0;
			const float BH = FV::Ui::ButtonH();
			const float TX = Pic.Right() + 8.f;
			const float TW = X + W - 6.f - TX;
			const FString Desc = Next ? (Next->Description.IsEmpty() ? YD.Description : Next->Description) : FString(TEXT("Построено и улучшено до предела."));
			// Р77: the whole description (the card grows past two lines)
			const float FullH = MeasureFull(D, Desc, TW, SmallS(), false);
			const float DescH = FullH > 2.f * WrapStep(D, SmallS(), false) + 0.5f ? FullH + 2.f : D.LineHeight(SmallS(), false) * 2.f;
			const float TextH = D.LineHeight(BodyS()) + DescH + 6.f;
			const float CardH = FMath::Max(PicS + 8.f, TextH) + (Next ? float(ReqRows) * RH + BH + 12.f : 6.f);
			D.Rect(FFVRect(X, Y, W, CardH), FV::Col(214, 162, 104));
			D.Rect(FFVRect(X + 1.f, Y + 1.f, W - 2.f, CardH - 2.f), RowFill());
			D.Rect(Pic, FV::Col(226, 190, 136));
			if (UFVSprites* Spr = UFVSprites::Get())
			{
				const FLinearColor Tint = Level > 0 ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.55f);
				if (B == EFVYardBuilding::Garden && FVDBU6::Seeds().Num() > 0)
				{
					// the garden's yard sprite is a long low fence strip — unreadable in a square card: show a ripe bed instead
					FVUI::SpriteFit(D, Spr->GardenBedSprite(FVDBU6::Seeds()[0].CropId, 3), Pic.Inset(4.f), Tint, 3.f);
				}
				else
				{
					FVUI::SpriteFit(D, Spr->YardBuildingSprite(B, FMath::Max(1, Level)), Pic.Inset(2.f), Tint, 1.f);
				}
			}
			float TY = Y + 5.f;
			FString Title = YD.Name;
			if (Level > 0) { Title += FString::Printf(TEXT(" — ур. %d из %d"), Level, FMath::Max(1, MaxL)); }
			D.Text(D.Ellipsize(Title, TW, BodyS()), FVector2D(TX, TY), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
			TY += D.LineHeight(BodyS()) + 1.f;
			TY += TextWrappedLimited(D, Desc, FVector2D(TX, TY), TW, SmallS(), bMax ? InkGood() : FVPal::InkSoft(), AllLines);
			Y = FMath::Max(Pic.Bottom() + 4.f, Y0 + TextH);
			if (Next)
			{
				const float RX = X + 6.f, RW = W - 12.f;
				for (const FFVIngredient& I : Next->Materials) { Y += ReqRow(D, RX, Y, RW, I.ItemId, P.CountIngredient(I.ItemId), I.Count, SmallS()); }
				if (Next->Price > 0) { Y += CoinRow(D, RX, Y, RW, P.Money, Next->Price, SmallS()); }
				Y += CondRow(D, RX, Y, RW, FString::Printf(TEXT("Уровень рыбака %d"), Next->MinLevel), P.GetLevel() >= Next->MinLevel, SmallS());
				if (YD.bNeedsBlockageCleared && Level == 0)
				{
					Y += CondRow(D, RX, Y, RW, TEXT("Завал в лесу расчищен"), P.U6.bBlockageCleared, SmallS(), EFVIcon::Axe);
				}
				Y += 4.f;
				const EFVBuyResult Can = P.CanBuildYard(B);
				const FFVRect BR(RX, Y, bP ? RW : FMath::Min(RW, 220.f), BH);
				const FString Label = Level == 0 ? TEXT("Построить") : FString::Printf(TEXT("Улучшить до ур. %d"), Level + 1);
				if (Can == EFVBuyResult::Ok)
				{
					if (CoinButton(D, BR, Label, Next->Price, EFVButton::Primary, true, true, BodyS())) { Want(EAct::Build, YD.Id); ActBuilding = B; }
				}
				else
				{
					D.Button(BR, ResultText(Can), EFVButton::Normal, false, SmallS() + 0.5f);
				}
				Y += BH + 6.f;
			}
			Y = FMath::Max(Y, Y0 + CardH);
			return Y + 6.f;
		}

		float DrawDecorRow(FVDraw& D, UFVSaveGame& P, const FFVDecorDef& Dd, float X, float Y, float W, bool bSel, int32 Page)
		{
			const bool bP = FV::IsPortrait();
			const int32 Pg = FMath::Clamp(Page, 0, int32(EPage::Count) - 1);
			FOpenRow& Open = Opens[Pg];
			const float RowH0 = bP ? 52.f : 32.f;
			const FFVRect Row0(X, Y, W, RowH0);   // the closed row: the icon and the button keep their places when it opens
			const bool bEquipped = P.GetEquippedDecor(Dd.Slot) == Dd.Id;
			const bool bOwned = P.OwnsDecor(Dd.Id);
			const bool bLocked = !bOwned && P.GetLevel() < Dd.MinLevel;
			const float IconS = bP ? 40.f : 26.f;
			const FFVRect IconR(Row0.X + 4.f, FMath::RoundToFloat(Row0.Center().Y - IconS * 0.5f), IconS, IconS);
			const bool bRemovable = bEquipped;
			const float BW = bP ? 100.f : 76.f, BH = bP ? 34.f : 20.f;
			const FFVRect BR(Row0.Right() - BW - 4.f, FMath::RoundToFloat(Row0.Center().Y - BH * 0.5f), BW, BH);
			const float TX = IconR.Right() + 6.f;
			const float TW = BR.X - 6.f - TX;
			FString Status;
			if (bEquipped) { Status = TEXT("Стоит на месте"); }
			else if (bOwned) { Status = TEXT("Куплено"); }
			else if (bLocked) { Status = FString::Printf(TEXT("Нужен уровень %d"), Dd.MinLevel); }
			const FString& Sub = Status.IsEmpty() ? Dd.Description : Status;
			const FLinearColor SubC = bLocked ? InkWarn() : (bEquipped || bOwned ? InkGood() : FVPal::InkSoft());
			// Р77: the text does not fit (or the status line hides the description: portrait shows it nowhere else) →
			// a tap opens the row: the whole name, the status and the whole description, wrapped
			const bool bHidden = !Status.IsEmpty() && !Dd.Description.IsEmpty();
			const bool bCut = bHidden || !Fits(D, Dd.Name, TW, BodyS(), true) || !Fits(D, Sub, TW, SmallS(), false);
			if (Open.Auto(Dd.Id, bCut)) { SelId[Pg] = Dd.Id; }   // "fv_expand": the opened row is tried on too
			const bool bOpen = bCut && Open.Id == Dd.Id;
			const float TY0 = Y + (bP ? 6.f : 3.f);
			const float SubGap = D.LineHeight(BodyS()) + (bP ? 2.f : 0.f);
			float SubY = TY0 + SubGap;
			float DescY = SubY;
			float RowH = RowH0;
			if (bOpen)
			{
				SubY = TY0 + MeasureFull(D, Dd.Name, TW, BodyS(), true) - WrapStep(D, BodyS(), true) + SubGap;
				float EndY = SubY + MeasureFull(D, Sub, TW, SmallS(), false);
				if (bHidden)
				{
					DescY = EndY + 1.f;
					EndY = DescY + MeasureFull(D, Dd.Description, TW, SmallS(), false);
				}
				RowH = FMath::Max(RowH0, FMath::CeilToFloat(EndY - Y) + (bP ? 10.f : 6.f));
			}
			const FFVRect Row(X, Y, W, RowH);
			D.Rect(Row, bEquipped ? FV::Col(190, 120, 60) : FV::Col(214, 162, 104));
			D.Rect(Row.Inset(1.f), bEquipped ? RowSel() : (bSel ? FV::Col(246, 222, 178) : RowFill()));
			if (bSel)
			{
				// Р61: the tried row (the preview shows it)
				D.Frame(Row, FV::Col(214, 52, 52), 1.f);
				D.Rect(FFVRect(Row.X + 1.f, Row.Y + 1.f, 2.f, Row.H - 2.f), FV::Col(214, 52, 52));
			}
			IconBox(D, IconR);
			if (UFVSprites* Spr = UFVSprites::Get())
			{
				FVUI::SpriteFit(D, Spr->DecorIcon(Dd), IconR.Inset(2.f), bLocked ? FLinearColor(1.f, 1.f, 1.f, 0.45f) : FLinearColor::White, 2.f);
			}
			if (bEquipped) { CheckBadge(D, FVector2D(IconR.Right() - 4.f, IconR.Y + 4.f), 4.f); }
			if (bOpen)
			{
				DrawFull(D, Dd.Name, FVector2D(TX, TY0), TW, BodyS(), FVPal::Ink(), true);
				DrawFull(D, Sub, FVector2D(TX, SubY), TW, SmallS(), SubC, false);
				if (bHidden) { DrawFull(D, Dd.Description, FVector2D(TX, DescY), TW, SmallS(), FVPal::InkSoft(), false); }
			}
			else
			{
				D.Text(D.Ellipsize(Dd.Name, TW, BodyS()), FVector2D(TX, TY0), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(D.Ellipsize(Sub, TW, SmallS(), false), FVector2D(TX, SubY), SmallS(), SubC, EFVAlign::Left, false, false);
			}
			if (bCut) { ExpandMark(D, BR.X, Row.Bottom(), bOpen); }
			const float BS = bP ? FV::Ui::Body() : 7.5f;
			if (bRemovable)
			{
				if (D.Button(BR, TEXT("Убрать"), EFVButton::Normal, true, BS)) { Want(EAct::Unequip, Dd.Id); }
			}
			else if (bOwned)
			{
				if (D.Button(BR, TEXT("Поставить"), EFVButton::Primary, true, BS)) { Want(EAct::Equip, Dd.Id); }
			}
			else if (Dd.Price <= 0)
			{
				D.Button(BR, TEXT("Награда"), EFVButton::Normal, false, BS);
			}
			else
			{
				const bool bAfford = P.CanAfford(Dd.Price);
				if (CoinButton(D, BR, TEXT("Купить"), Dd.Price, EFVButton::Primary, !bLocked && bAfford, bAfford, BS)) { Want(EAct::Buy, Dd.Id); }
			}
			Open.Keep(Cols[Pg], Dd.Id, Y, RowH);
			// a tap on the row (not on its button) tries the item on; Р77: and opens / closes its text
			if (D.Clicked(Row))
			{
				const bool bToggle = bCut || !Open.Id.IsNone();
				if (bToggle) { Open.Toggle(Dd.Id, bCut); }
				if (!bSel) { SelId[Pg] = Dd.Id; }
				if (!bSel || bToggle) { FVUI::Sfx(EFVSfx::Click); }
			}
			return Y + RowH + 4.f;
		}

		void RunAct(UFVSaveGame& P)
		{
			const EAct A = Act;
			const FName Id = ActId;
			Act = EAct::None;
			UFVGame* G = Game;
			if (A == EAct::None || !G) { return; }
			if (A == EAct::Build)
			{
				const EFVYardBuilding B = ActBuilding;
				const EFVBuyResult R = P.BuildYard(B);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				const int32 Lv = P.GetYardLevel(B);
				Success(G, Lv <= 1 ? FString::Printf(TEXT("Построено: %s — загляните во двор"), *FVDBU6::YardBuilding(B).Name)
					: FString::Printf(TEXT("%s: уровень %d"), *FVDBU6::YardBuilding(B).Name, Lv), EFVSfx::Hammer, EFVIcon::Hammer);
				Refresh(G);
				return;
			}
			const FFVDecorDef* Def = FVDB::FindDecor(Id);
			if (!Def) { return; }
			if (A == EAct::Buy || A == EAct::Equip)
			{
				SelId[FVDecor::IsYardSlot(Def->Slot) ? int32(EPage::Yard) : int32(EPage::Furniture)] = Id;   // the preview follows it
			}
			switch (A)
			{
			case EAct::Buy:
			{
				const EFVBuyResult R = P.BuyDecor(Id);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				Success(G, FString::Printf(TEXT("Куплено и поставлено: %s"), *Def->Name), EFVSfx::Buy, EFVIcon::House);
				Refresh(G);
				return;
			}
			case EAct::Equip:
				if (!P.EquipDecor(Id)) { Fail(G, EFVBuyResult::Invalid); return; }
				Success(G, FString::Printf(TEXT("Поставлено: %s"), *Def->Name), EFVSfx::Equip, EFVIcon::House);
				Refresh(G);
				return;
			case EAct::Unequip:
				P.UnequipDecor(Def->Slot);
				Success(G, FString::Printf(TEXT("Убрано: %s"), *Def->Name), EFVSfx::Equip, EFVIcon::House);
				Refresh(G);
				return;
			default:
				return;
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeBuilderScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenBuilderImpl::FBuilderScreen>(Game);
}
