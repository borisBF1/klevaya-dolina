// FVScreenDecor.cpp — «Каталог для дома» (the dresser in the house): slot tabs (обои, пол, ковёр, светильник,
// растение, картина, шторы), a grid of decor items, a live room preview with the chosen item in place, the source
// text, and «Купить» / «Поставить» / «Убрать». Both orientations.
// Update 3: the sections are sliding pages (swipe / tabs / Left-Right keys), each with its own smooth grid.
// Update 6 (Р61): the preview is the try-on of FVDecorPreview — a real fragment of the big house with the chosen item at
// its place. Context may be a decor id (the catalog opens on its section with it selected: autotest shots, links).

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVDecorPreview.h"

namespace FVDecorScreenImpl
{
	FV_HOME_UI_USING();

	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr int32 NumSlots = FVDecor::ClassicSlots;   // update 6: new slots belong to the builder / yard (SCREEN-YARD)

	enum class EState : uint8 { Equipped, Owned, ForSale, Locked, Reward };
	enum class EAct : uint8 { None, Buy, Equip, Unequip };

	static EState StateOf(const UFVSaveGame& P, const FFVDecorDef& Def)
	{
		if (P.GetEquippedDecor(Def.Slot) == Def.Id) { return EState::Equipped; }
		if (P.OwnsDecor(Def.Id)) { return EState::Owned; }
		if (Def.Price <= 0) { return EState::Reward; }
		if (P.GetLevel() < Def.MinLevel) { return EState::Locked; }
		return EState::ForSale;
	}

	static bool CanBeEmpty(EFVDecorSlot S)
	{
		return S != EFVDecorSlot::Wallpaper && S != EFVDecorSlot::Floor;
	}

	class FDecorScreen final : public FVScreen
	{
	public:
		explicit FDecorScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			TickCount = 0;
			for (int32 i = 0; i < NumSlots; ++i)
			{
				SelId[i] = NAME_None;
				Grids[i].Reset();
				EnsureSel[i] = 2;   // first draw: show the selected item instantly
			}
			Pager.NumPagesCached = NumSlots;
			int32 StartPage = 0;
			if (const FFVDecorDef* Pre = FVDB::FindDecor(Game ? Game->GetScreenContext() : NAME_None))
			{
				if (int32(Pre->Slot) < NumSlots) { StartPage = int32(Pre->Slot); SelId[StartPage] = Pre->Id; }
			}
			Pager.SetPage(StartPage);
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (PagerKeys(In, Pager)) { return; }   // Left/Right: slide to the neighbouring section (animated)
			if (In.WasPressed(EKeys::Up)) { PendingMove = -1; }
			else if (In.WasPressed(EKeys::Down)) { PendingMove = 1; }
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = bP ? FV::WindowRect() : WinRect;
			bool bClose = D.Window(Win, TEXT("Каталог для дома"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Act = EAct::None;
			if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { bClose = true; }
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}

			// keyboard Up/Down moves the selection of the current section
			if (PendingMove != 0)
			{
				const int32 Pg = FMath::Clamp(Pager.Page, 0, NumSlots - 1);
				const TArray<const FFVDecorDef*> Items = FVDB::DecorFor(EFVDecorSlot(Pg));
				if (Items.Num() > 0)
				{
					const int32 Cur = SelIndex(*P, Pg, Items);
					const int32 Next = FMath::Clamp(Cur + PendingMove, 0, Items.Num() - 1);
					SelId[Pg] = Items[Next] ? Items[Next]->Id : NAME_None;
					EnsureSel[Pg] = 1;
					FVUI::Sfx(EFVSfx::Click);
				}
				PendingMove = 0;
			}

			// ---- header: owned count + purse
			const float X = Win.X + (bP ? 10.f : 12.f);
			const float W = Win.W - (bP ? 20.f : 24.f);
			float Y = Win.Y + (bP ? 16.f : 10.f);
			{
				int32 Owned = 0, Total = 0;
				for (const FFVDecorDef& Dd : FVDB::Decor())
				{
					++Total;
					if (P->OwnsDecor(Dd.Id)) { ++Owned; }
				}
				const float HS = bP ? FV::Ui::Body() : 8.5f;
				DrawIcon(D, EFVIcon::House, FFVRect(X, Y - 1.f, 18.f, 18.f), 1.f);
				D.Text(FString::Printf(TEXT("В доме: %d из %d вещей"), Owned, Total), FVector2D(X + 22.f, Y), HS, FVPal::Ink(), EFVAlign::Left, false, true);
				D.MoneyLabel(FVector2D(X + W, Y), P->Money, bP ? FV::Ui::Body() : 9.f, EFVAlign::Right);
				Y += bP ? 24.f : 20.f;
			}

			// ---- slot tabs (a swipe over the content or a tab click slides the sections)
			TArray<FString> Labels;
			for (int32 i = 0; i < NumSlots; ++i) { Labels.Add(FVDB::DecorSlotName(EFVDecorSlot(i))); }
			FFVRect Area;
			if (bP)
			{
				Y += PagerTabs(D, Labels, Pager, X, Y, W);
				const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;
				Area = FFVRect(Win.X + 10.f, Y, Win.W - 20.f, FMath::Max(60.f, Bottom - Y));
			}
			else
			{
				Y += 16.f;
				PagerTabs(D, Labels, Pager, Win.X + 10.f, Y, Win.W - 20.f);
				Area = FFVRect(Win.X + 10.f, Y, Win.W - 20.f, Win.Bottom() - 10.f - Y);
			}
			Pager.Update(D, Area, NumSlots);
			Pager.DrawPages(D, Area, [&](int32 Page) { DrawSection(D, *P, Page, Area); });

			if (bP)
			{
				// footer: the action of the section the pager is heading to
				const int32 Pg = FMath::Clamp(Pager.Page, 0, NumSlots - 1);
				const TArray<const FFVDecorDef*> Items = FVDB::DecorFor(EFVDecorSlot(Pg));
				const int32 Sel = SelIndex(*P, Pg, Items);
				FFVRect ActR;
				if (D.ActionBar(Win, &ActR)) { bClose = true; }
				DrawButtons(D, *P, Items.IsValidIndex(Sel) ? Items[Sel] : nullptr, ActR);
			}

			if (bClose && Game)
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
			RunAct(*P);
		}

	private:
		int32 TickCount = 0;
		FName SelId[NumSlots];
		FFVScrollView Grids[NumSlots];   // smooth grid scrolling, one per section
		uint8 EnsureSel[NumSlots] = {};  // 1 = scroll the selection into view (animated), 2 = instantly
		FFVPager Pager;                  // Pager.Page = the decor slot section
		int32 PendingMove = 0;
		EAct Act = EAct::None;
		FName ActId;

		float SmallS() const { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }

		void Want(EAct A, FName Id)
		{
			if (Act == EAct::None) { Act = A; ActId = Id; }
		}

		/** Selected index of a section (defaults to the equipped item, remembered in SelId). */
		int32 SelIndex(const UFVSaveGame& P, int32 Page, const TArray<const FFVDecorDef*>& Items)
		{
			if (Items.Num() == 0 || Page < 0 || Page >= NumSlots) { return INDEX_NONE; }
			int32 Sel = Items.IndexOfByPredicate([this, Page](const FFVDecorDef* Dd) { return Dd && Dd->Id == SelId[Page]; });
			if (Sel == INDEX_NONE)
			{
				const FName Eq = P.GetEquippedDecor(EFVDecorSlot(Page));
				Sel = FMath::Max(0, Items.IndexOfByPredicate([Eq](const FFVDecorDef* Dd) { return Dd && Dd->Id == Eq; }));
				SelId[Page] = Items[Sel] ? Items[Sel]->Id : NAME_None;
			}
			return Sel;
		}

		// ---------------------------------------------------------------------------------------- layouts
		/** One section (decor slot) as a sliding page: local coordinates, Area = the page rect at rest. */
		void DrawSection(FVDraw& D, UFVSaveGame& P, int32 Page, const FFVRect& Area)
		{
			const TArray<const FFVDecorDef*> Items = FVDB::DecorFor(EFVDecorSlot(Page));
			const int32 Sel = SelIndex(P, Page, Items);
			const FFVDecorDef* Def = Items.IsValidIndex(Sel) ? Items[Sel] : nullptr;
			if (FV::IsPortrait()) { DrawPortrait(D, P, Page, Items, Sel, Def, Area); }
			else { DrawLandscape(D, P, Page, Items, Sel, Def, Area); }
		}

		void DrawLandscape(FVDraw& D, UFVSaveGame& P, int32 Page, const TArray<const FFVDecorDef*>& Items, int32 Sel, const FFVDecorDef* Def, const FFVRect& Area)
		{
			const FFVRect GridP(Area.X, Area.Y, 250.f, Area.H);
			const FFVRect RightP(GridP.Right() + 6.f, Area.Y, Area.Right() - GridP.Right() - 6.f, Area.H);
			D.Panel(GridP, EFVPanel::Parchment);
			D.Panel(RightP, EFVPanel::Parchment);
			DrawGrid(D, P, Page, Items, Sel, GridP.Inset(5.f));
			const FFVRect In = RightP.Inset(7.f);
			const FFVRect Room(In.X, In.Y, In.W, 142.f);
			DrawRoom(D, P, Room, Def);
			const float Y = Room.Bottom() + 6.f;
			if (Def) { DrawDetail(D, P, *Def, FFVRect(In.X, Y, In.W, In.Bottom() - 26.f - Y)); }
			DrawButtons(D, P, Def, FFVRect(In.X, In.Bottom() - 20.f, In.W, 20.f));
		}

		void DrawPortrait(FVDraw& D, UFVSaveGame& P, int32 Page, const TArray<const FFVDecorDef*>& Items, int32 Sel, const FFVDecorDef* Def, const FFVRect& Area)
		{
			const float RoomH = FMath::Clamp(FMath::RoundToFloat(150.f + (FV::VH - 640.f) * 0.25f), 140.f, 210.f);
			const FFVRect Room(Area.X, Area.Y, Area.W, RoomH);
			DrawRoom(D, P, Room, Def);
			const float DetailH = 66.f;
			const FFVRect DetR(Area.X, Area.Bottom() - DetailH, Area.W, DetailH);
			const FFVRect GridP(Area.X, Room.Bottom() + 6.f, Area.W, FMath::Max(60.f, DetR.Y - 6.f - Room.Bottom() - 6.f));
			D.Panel(GridP, EFVPanel::Parchment);
			DrawGrid(D, P, Page, Items, Sel, GridP.Inset(5.f));
			D.Panel(DetR, EFVPanel::Parchment);
			if (Def) { DrawDetail(D, P, *Def, DetR.Inset(6.f)); }
		}

		// ---------------------------------------------------------------------------------------- pieces
		void DrawGrid(FVDraw& D, UFVSaveGame& P, int32 Page, const TArray<const FFVDecorDef*>& Items, int32 Sel, const FFVRect& Area)
		{
			const bool bP = FV::IsPortrait();
			if (Items.Num() == 0)
			{
				EmptyState(D, Area, EFVIcon::House, TEXT("В этом разделе пока ничего нет."), SmallS());
				return;
			}
			const int32 Pg = FMath::Clamp(Page, 0, NumSlots - 1);
			FFVScrollView& Scroll = Grids[Pg];
			UFVSprites* Sp = UFVSprites::Get();
			const float Cell = bP ? FV::Ui::Slot() : 44.f;
			const float Gap = 4.f;
			const float Pitch = Cell + Gap;
			const float Pad = 2.f;   // room for the selection frame
			const int32 Cols = FMath::Max(1, FMath::FloorToInt((Area.W - 8.f + Gap) / Pitch));
			const int32 TotalRows = FMath::DivideAndRoundUp(Items.Num(), Cols);
			Scroll.Update(D, Area, 2.f * Pad + float(TotalRows) * Pitch - Gap);
			if (EnsureSel[Pg] != 0 && Sel >= 0)
			{
				Scroll.ScrollToShow(float(Sel / Cols) * Pitch, Cell + 2.f * Pad, EnsureSel[Pg] == 2);
			}
			EnsureSel[Pg] = 0;
			const float GridW = float(Cols) * Pitch - Gap;
			const float GX = FMath::RoundToFloat(Area.X + (Area.W - (Scroll.CanScroll() ? 6.f : 0.f) - GridW) * 0.5f);
			D.PushClip(Area);
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				if (!Items[i]) { continue; }
				const FFVRect C(GX + float(i % Cols) * Pitch, Area.Y + Pad - Scroll.Offset() + float(i / Cols) * Pitch, Cell, Cell);
				if (!RowVisible(Area, C.Y, Cell)) { continue; }
				const FFVDecorDef& Dd = *Items[i];
				const EState St = StateOf(P, Dd);
				if (D.Slot(C, i == Sel, true) && i != Sel)
				{
					SelId[Pg] = Dd.Id;
				}
				const bool bDim = St == EState::Locked || St == EState::Reward;
				if (Sp)
				{
					const FFVSprite Ic = Sp->DecorIcon(Dd);
					FVUI::SpriteFit(D, Ic, C.Inset(bP ? 7.f : 6.f), bDim ? FLinearColor(1.f, 1.f, 1.f, 0.45f) : FLinearColor::White, 2.f);
				}
				if (St == EState::Equipped) { CheckBadge(D, FVector2D(C.Right() - 7.f, C.Y + 7.f), 5.f); }
				else if (St == EState::Locked) { DrawIcon(D, EFVIcon::Lock, FFVRect(C.Right() - 12.f, C.Y + 2.f, 10.f, 10.f), 1.f); }
				else if (St == EState::Reward) { DrawIcon(D, EFVIcon::Trophy, FFVRect(C.Right() - 12.f, C.Y + 2.f, 10.f, 10.f), 1.f); }
				else if (St == EState::ForSale && !bP)
				{
					D.TextOutlined(FV::Money(Dd.Price), FVector2D(C.Center().X, C.Bottom() - 9.f), 6.f, P.CanAfford(Dd.Price) ? FV::Col(255, 236, 140) : FV::Col(255, 160, 140), EFVAlign::Center);
				}
				if (!bP && D.Hover(C))
				{
					D.Tooltip(Dd.Name, FVDB::DecorSourceText(Dd.Id));
				}
			}
			EndList(D, Scroll, Area);
		}

		/** Р61: the try-on — the real room of the big house with every equipped item and Preview in its slot. */
		void DrawRoom(FVDraw& D, const UFVSaveGame& P, const FFVRect& R, const FFVDecorDef* Preview)
		{
			if (Preview) { FVDecorPreview::Draw(D, R, *Preview, &P); return; }
			D.Rect(R, FV::Col(70, 40, 22));
			D.Rect(R.Inset(2.f), FV::Col(20, 14, 10));
			D.Text(TEXT("Выберите вещь"), R.Center(), SmallS(), FV::Col(220, 200, 170), EFVAlign::Center, false, false);
		}

		void DrawDetail(FVDraw& D, UFVSaveGame& P, const FFVDecorDef& Def, const FFVRect& R)
		{
			const bool bP = FV::IsPortrait();
			const EState St = StateOf(P, Def);
			const float NS = bP ? FV::Ui::Body() + 0.5f : 9.f;
			FString Chip;
			FVUI::EChip K = FVUI::EChip::Neutral;
			switch (St)
			{
			case EState::Equipped: Chip = TEXT("Стоит в доме"); K = FVUI::EChip::Good; break;
			case EState::Owned: Chip = TEXT("Куплено"); K = FVUI::EChip::Good; break;
			case EState::Locked: Chip = FString::Printf(TEXT("Ур. %d"), Def.MinLevel); break;
			case EState::Reward: Chip = TEXT("Награда"); break;
			default: Chip = FV::Money(Def.Price); break;
			}
			const float CW = FVUI::StatusChip(D, Chip, FVector2D(R.Right(), R.Y), K, St == EState::Locked, bP ? 7.f : 6.5f);
			float Y = R.Y;
			D.Text(D.Ellipsize(Def.Name, R.W - CW - 6.f, NS), FVector2D(R.X, Y), NS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(NS);
			const FString Source = FVDB::DecorSourceText(Def.Id);
			const FString Line2 = FString::Printf(TEXT("%s · %s"), *FVDB::DecorSlotName(Def.Slot), *Source);
			D.Text(D.Ellipsize(Line2, R.W, SmallS(), false), FVector2D(R.X, Y), SmallS(), St == EState::Reward ? FV::Col(132, 62, 184) : FVPal::InkSoft(), EFVAlign::Left, false, false);
			Y += D.LineHeight(SmallS(), false) + 2.f;
			const int32 MaxLines = FMath::Max(1, FMath::FloorToInt((R.Bottom() - Y) / FMath::Max(1.f, D.LineHeight(SmallS(), false) * 0.95f)));
			TextWrappedLimited(D, Def.Description, FVector2D(R.X, Y), R.W, SmallS(), FVPal::Ink(), FMath::Min(MaxLines, bP ? 2 : 4));
		}

		void DrawButtons(FVDraw& D, UFVSaveGame& P, const FFVDecorDef* Def, const FFVRect& Row)
		{
			const bool bP = FV::IsPortrait();
			const float TS = bP ? FV::Ui::Body() + 0.5f : 8.f;
			if (!Def)
			{
				D.Button(Row, TEXT("Выберите вещь"), EFVButton::Normal, false, TS);
				return;
			}
			const EState St = StateOf(P, *Def);
			const bool bRemovable = St == EState::Equipped && CanBeEmpty(Def->Slot);
			const FFVRect Main = bRemovable ? FVDraw::SplitH(Row, 1, 2, 6.f) : Row;
			if (bRemovable)
			{
				if (D.Button(FVDraw::SplitH(Row, 0, 2, 6.f), TEXT("Убрать"), EFVButton::Normal, true, TS)) { Want(EAct::Unequip, Def->Id); }
			}
			switch (St)
			{
			case EState::Equipped:
				D.Button(Main, TEXT("Уже стоит"), EFVButton::Normal, false, TS);
				break;
			case EState::Owned:
				if (D.Button(Main, TEXT("Поставить"), EFVButton::Primary, true, TS)) { Want(EAct::Equip, Def->Id); }
				break;
			case EState::Locked:
				D.Button(Main, FString::Printf(TEXT("Нужен уровень %d"), Def->MinLevel), EFVButton::Normal, false, TS);
				break;
			case EState::Reward:
				D.Button(Main, TEXT("Награда за достижение"), EFVButton::Normal, false, TS);
				break;
			default:
			{
				const bool bAfford = P.CanAfford(Def->Price);
				if (CoinButton(D, Main, TEXT("Купить"), Def->Price, EFVButton::Primary, bAfford, bAfford, TS)) { Want(EAct::Buy, Def->Id); }
				break;
			}
			}
		}

		// ---------------------------------------------------------------------------------------- actions
		void RunAct(UFVSaveGame& P)
		{
			const EAct A = Act;
			const FName Id = ActId;
			Act = EAct::None;
			UFVGame* G = Game;
			const FFVDecorDef* Def = FVDB::FindDecor(Id);
			if (A == EAct::None || !G || !Def) { return; }
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
			{
				if (!P.EquipDecor(Id)) { Fail(G, EFVBuyResult::Invalid); return; }
				Success(G, FString::Printf(TEXT("Поставлено: %s"), *Def->Name), EFVSfx::Equip, EFVIcon::House);
				Refresh(G);
				return;
			}
			case EAct::Unequip:
			{
				P.UnequipDecor(Def->Slot);
				Success(G, FString::Printf(TEXT("Убрано: %s"), *Def->Name), EFVSfx::Equip, EFVIcon::House);
				Refresh(G);
				return;
			}
			default:
				return;
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeHomeDecorScreen(UFVGame* Game)
{
	return MakeUnique<FVDecorScreenImpl::FDecorScreen>(Game);
}
