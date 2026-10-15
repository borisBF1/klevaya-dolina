// FVScreenOrders.cpp — «Доска заказов» at the pond: cards (customer, requested item, count, quality / smoked,
// deposit, reward, deadline or duration, state chip), «Взять (залог N)» (confirm) and «Сдать».
// Landscape: every card has its own button. Portrait: tap a card to select it, the action sits in the bottom bar.
// Update 3: the notes scroll smoothly (FFVScrollView: drag 1:1, fling, wheel); clicks fire on a clean release.

#include "FVScreens.h"
#include "FVScreenHome.h"

namespace FVOrdersScreenImpl
{
	FV_HOME_UI_USING();

	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr int32 MaxTaken = 3;   // spec §6: at most 3 taken orders

	static EFVOrderState StateOf(const FFVOrder& O)
	{
		return EFVOrderState(FMath::Clamp<int32>(O.State, 0, int32(EFVOrderState::Count) - 1));
	}

	/** Display order: taken first, then new offers, then finished ones. */
	static int32 StateRank(const FFVOrder& O)
	{
		switch (StateOf(O))
		{
		case EFVOrderState::Taken: return 0;
		case EFVOrderState::Offered: return 1;
		case EFVOrderState::Done: return 2;
		default: return 3;
		}
	}

	/** Units the player holds that satisfy the order (for the "есть N/M" hint; delivery uses CanDeliver). */
	static int32 HaveFor(const UFVSaveGame& P, const FFVOrder& O)
	{
		if (FVDB::FindFish(O.ItemId))
		{
			int32 N = 0;
			for (const FFVInvStack& S : P.Keepnet)
			{
				if (S.ItemId == O.ItemId && S.Quality >= O.MinQuality && (!O.bSmoked || (S.Flags & FVStackFlags::Smoked))) { N += S.Count; }
			}
			return N;
		}
		if (FVDB::FindBait(O.ItemId)) { return P.BaitCount(O.ItemId); }
		return P.CountItem(O.ItemId);
	}

	static FString RequestName(const FFVOrder& O)
	{
		return FVDB::StackName(O.ItemId, O.bSmoked ? FVStackFlags::Smoked : 0);
	}

	/** Procedural customer portrait (face, hair, a hat / scarf / beard picked from the name). */
	static void DrawAvatar(FVDraw& D, const FString& Name, const FFVRect& R)
	{
		const uint32 H = GetTypeHash(Name);
		static const FColor Backs[] = { FColor(150, 190, 220), FColor(190, 220, 160), FColor(236, 200, 150), FColor(214, 176, 220), FColor(240, 180, 170) };
		static const FColor Skins[] = { FColor(250, 214, 180), FColor(236, 190, 150), FColor(214, 160, 120) };
		static const FColor Hairs[] = { FColor(90, 60, 36), FColor(40, 30, 26), FColor(200, 160, 90), FColor(170, 90, 50), FColor(210, 210, 210) };
		static const FColor Shirts[] = { FColor(70, 110, 180), FColor(180, 70, 60), FColor(80, 140, 80), FColor(150, 110, 60), FColor(110, 80, 150) };
		const FColor Back = Backs[H % UE_ARRAY_COUNT(Backs)];
		const FColor Skin = Skins[(H >> 4) % UE_ARRAY_COUNT(Skins)];
		FColor Hair = Hairs[(H >> 8) % UE_ARRAY_COUNT(Hairs)];
		const FColor Shirt = Shirts[(H >> 12) % UE_ARRAY_COUNT(Shirts)];
		const bool bGranny = Name.Contains(TEXT("Бабушка"));
		const bool bCaptain = Name.Contains(TEXT("Капитан"));
		const bool bCook = Name.Contains(TEXT("Повар"));
		const bool bPost = Name.Contains(TEXT("Почтальон"));
		const bool bSmith = Name.Contains(TEXT("Кузнец"));
		const bool bFisher = Name.Contains(TEXT("Рыбак"));
		const bool bTeacher = Name.Contains(TEXT("Учительница"));
		if (bGranny) { Hair = FColor(220, 220, 220); }

		D.Rect(R, FV::Col(92, 52, 26));
		D.Rect(R.Inset(1.f), FV::Col(Back));
		const float Rad = FMath::RoundToFloat(R.W * 0.26f);
		const FVector2D C(R.Center().X, R.Y + R.H * 0.5f);
		// shoulders
		D.Rect(FFVRect(R.X + R.W * 0.18f, R.Bottom() - R.H * 0.2f - 1.f, R.W * 0.64f, R.H * 0.2f), FV::Col(Shirt));
		// hair behind the face, face, eyes, smile
		D.Circle(C + FVector2D(0.f, -Rad * 0.28f), Rad * 1.08f, FV::Col(Hair));
		D.Circle(C, Rad, FV::Col(Skin));
		const float EyeY = C.Y - Rad * 0.08f;
		D.Circle(FVector2D(C.X - Rad * 0.38f, EyeY), FMath::Max(1.f, Rad * 0.12f), FV::Col(40, 28, 24));
		D.Circle(FVector2D(C.X + Rad * 0.38f, EyeY), FMath::Max(1.f, Rad * 0.12f), FV::Col(40, 28, 24));
		D.Line(FVector2D(C.X - Rad * 0.3f, C.Y + Rad * 0.42f), FVector2D(C.X + Rad * 0.3f, C.Y + Rad * 0.42f), FV::Col(150, 70, 60), 1.f);
		D.Circle(FVector2D(C.X - Rad * 0.62f, C.Y + Rad * 0.25f), Rad * 0.14f, FV::Col(240, 150, 140, 150));
		D.Circle(FVector2D(C.X + Rad * 0.62f, C.Y + Rad * 0.25f), Rad * 0.14f, FV::Col(240, 150, 140, 150));
		const float Top = C.Y - Rad;
		if (bCaptain)
		{
			D.Rect(FFVRect(C.X - Rad * 1.1f, Top - Rad * 0.35f, Rad * 2.2f, Rad * 0.55f), FV::Col(30, 40, 80));
			D.Rect(FFVRect(C.X - Rad * 1.1f, Top + Rad * 0.1f, Rad * 2.2f, 2.f), FV::Col(230, 190, 60));
			D.Rect(FFVRect(C.X - Rad * 1.3f, Top + Rad * 0.18f, Rad * 2.6f, 2.f), FV::Col(20, 24, 50));
			D.Circle(FVector2D(C.X, C.Y + Rad * 0.9f), Rad * 0.55f, FV::Col(236, 236, 236));   // white beard
		}
		else if (bCook)
		{
			D.Rect(FFVRect(C.X - Rad * 0.8f, Top - Rad * 0.9f, Rad * 1.6f, Rad * 1.0f), FV::Col(250, 250, 250));
			D.Circle(FVector2D(C.X, Top - Rad * 0.9f), Rad * 0.7f, FV::Col(250, 250, 250));
		}
		else if (bPost)
		{
			D.Rect(FFVRect(C.X - Rad * 1.0f, Top - Rad * 0.3f, Rad * 2.f, Rad * 0.5f), FV::Col(50, 90, 170));
			D.Rect(FFVRect(C.X - Rad * 0.2f, Top + Rad * 0.15f, Rad * 1.4f, 2.f), FV::Col(30, 50, 110));
		}
		else if (bGranny)
		{
			D.Rect(FFVRect(C.X - Rad * 1.15f, Top - Rad * 0.2f, Rad * 2.3f, Rad * 0.55f), FV::Col(200, 60, 60));
			D.Circle(FVector2D(C.X + Rad * 1.0f, C.Y + Rad * 0.6f), Rad * 0.25f, FV::Col(200, 60, 60));
		}
		else if (bFisher)
		{
			D.Rect(FFVRect(C.X - Rad * 1.2f, Top - Rad * 0.1f, Rad * 2.4f, Rad * 0.35f), FV::Col(90, 120, 60));
			D.Rect(FFVRect(C.X - Rad * 0.8f, Top - Rad * 0.55f, Rad * 1.6f, Rad * 0.5f), FV::Col(110, 140, 70));
		}
		if (bSmith)
		{
			D.Circle(FVector2D(C.X, C.Y + Rad * 0.85f), Rad * 0.6f, FV::Col(70, 44, 30));
		}
		if (bTeacher)
		{
			D.Ring(FVector2D(C.X - Rad * 0.38f, EyeY), Rad * 0.26f, Rad * 0.26f, FV::Col(60, 40, 30), 1.f);
			D.Ring(FVector2D(C.X + Rad * 0.38f, EyeY), Rad * 0.26f, Rad * 0.26f, FV::Col(60, 40, 30), 1.f);
		}
	}

	/** Asks, then takes the order (pays the deposit). The lambda keeps only a weak game pointer and the id. */
	static void ConfirmTake(UFVGame* Game, const FFVOrder& O)
	{
		if (!Game) { return; }
		TWeakObjectPtr<UFVGame> Weak(Game);
		const FName Id = O.Id;
		FString Body = FString::Printf(TEXT("%s просит: %s ×%d."), *O.Customer, *RequestName(O), O.Count);
		if (O.MinQuality > 0)
		{
			Body += FString::Printf(TEXT(" Качество — не ниже «%s»."), *FVDB::QualityName(QualityOf(O.MinQuality)));
		}
		Body += FString::Printf(TEXT("\nЗалог %s вернётся вместе с наградой (%s), если сдать заказ за %s. Не успеете — залог пропадёт."),
			*FVUI::Coins(O.Deposit), *FVUI::Coins(O.Reward), *UFVGame::DurationText(float(O.DurationHours) * 60.f));
		Game->Confirm(TEXT("Взять заказ?"), Body, [Weak, Id]()
		{
			UFVGame* G = Weak.Get();
			UFVSaveGame* P = G ? G->GetProfile() : nullptr;
			if (!P) { return; }
			if (P->TakenOrders() >= MaxTaken)
			{
				Fail(G, TEXT("Можно держать не больше трёх заказов"));
				return;
			}
			const EFVBuyResult R = P->TakeOrder(Id, G->GetAbsMinutes());
			if (R != EFVBuyResult::Ok)
			{
				Fail(G, R);
				return;
			}
			Success(G, TEXT("Заказ взят — не опоздайте!"), EFVSfx::Coin, EFVIcon::Order);
			Refresh(G);
		}, TEXT("Взять"), TEXT("Отмена"));
	}

	static void DoDeliver(UFVGame* G, UFVSaveGame& P, FName Id)
	{
		const FFVOrder* O = P.Orders.FindByPredicate([Id](const FFVOrder& X) { return X.Id == Id; });
		if (!O) { return; }
		// read everything BEFORE the profile changes (the order may be pruned)
		const int32 Reward = O->Reward;
		const int32 Deposit = O->Deposit;
		const FName RewardItem = O->RewardItem;
		const int32 RewardCount = O->RewardItemCount;
		const FString Customer = O->Customer;
		const EFVBuyResult R = P.DeliverOrder(Id);
		if (R != EFVBuyResult::Ok)
		{
			Fail(G, R == EFVBuyResult::NotEnoughMaterials ? FString(TEXT("Не хватает товара для заказа")) : ResultText(R));
			return;
		}
		FString Msg = FString::Printf(TEXT("%s: спасибо! +%s"), *Customer, *FVUI::Coins(Reward));
		if (Deposit > 0) { Msg += FString::Printf(TEXT(", залог %s вернулся"), *FV::Money(Deposit)); }
		if (!RewardItem.IsNone() && RewardCount > 0) { Msg += FString::Printf(TEXT(", %s ×%d"), *FVDB::ItemName(RewardItem), RewardCount); }
		Success(G, Msg, EFVSfx::OrderDone, EFVIcon::Order);
		Refresh(G);
	}

	enum class EAct : uint8 { None, Take, Deliver };

	class FOrdersScreen final : public FVScreen
	{
	public:
		explicit FOrdersScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			TickCount = 0;
			if (UFVSaveGame* P = Game ? Game->GetProfile() : nullptr)
			{
				P->RefreshOrders();   // today's offers (no-op when the board is up to date)
				Game->NotifyProfileChanged();
				Game->GetScene().RefreshWorldObjects();
			}
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal())
			{
				return;
			}
			if (In.WasPressed(EKeys::Up)) { PendingMove = -1; }
			else if (In.WasPressed(EKeys::Down)) { PendingMove = 1; }
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = bP ? FV::WindowRect() : WinRect;
			bool bClose = D.Window(Win, TEXT("Доска заказов"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Act = EAct::None;
			if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { bClose = true; }
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}

			// sorted view of the board + selection by id
			const TArray<FFVOrder>& Orders = P->Orders;
			TArray<int32> Idx;
			for (int32 i = 0; i < Orders.Num(); ++i) { Idx.Add(i); }
			Idx.StableSort([&Orders](int32 A, int32 B) { return StateRank(Orders[A]) < StateRank(Orders[B]); });
			int32 Sel = Idx.IndexOfByPredicate([&Orders, this](int32 i) { return Orders[i].Id == SelOrder; });
			if (Sel == INDEX_NONE && Idx.Num() > 0)
			{
				Sel = 0;
				SelOrder = Orders[Idx[0]].Id;
			}
			if (PendingMove != 0 && Idx.Num() > 0)
			{
				Sel = FMath::Clamp(Sel + PendingMove, 0, Idx.Num() - 1);
				SelOrder = Orders[Idx[Sel]].Id;
				bEnsureSel = true;
				FVUI::Sfx(EFVSfx::Click);
			}
			PendingMove = 0;
			const double Now = Game->GetAbsMinutes();

			// ---- header: taken count, time, purse
			const float X = Win.X + (bP ? 10.f : 12.f);
			const float W = Win.W - (bP ? 20.f : 24.f);
			float Y = Win.Y + (bP ? 16.f : 10.f);
			{
				const float HS = bP ? FV::Ui::Body() + 1.f : 9.f;
				const float SS = bP ? FV::Ui::Small() : 6.5f;
				const int32 Taken = P->TakenOrders();
				DrawIcon(D, EFVIcon::Order, FFVRect(X, Y, bP ? 24.f : 20.f, bP ? 24.f : 20.f), 2.f);
				const float TX = X + (bP ? 30.f : 26.f);
				D.Text(FString::Printf(TEXT("Взято заказов: %d / %d"), Taken, MaxTaken), FVector2D(TX, Y - 1.f), HS, Taken >= MaxTaken ? InkWarn() : FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(FString::Printf(TEXT("Сейчас: %s"), *Game->AbsTimeText(Now)), FVector2D(TX, Y + D.LineHeight(HS)), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				D.MoneyLabel(FVector2D(X + W, Y + 1.f), P->Money, bP ? FV::Ui::Body() : 9.f, EFVAlign::Right);
				if (!bP)
				{
					D.Text(TEXT("Сдайте до срока — награда и залог ваши"), FVector2D(X + W, Y + 14.f), SS, FVPal::InkSoft(), EFVAlign::Right, false, false);
				}
				Y += bP ? 32.f : 26.f;
			}

			// ---- the cork board with the notes
			const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 10.f : Win.Bottom() - 10.f;
			const FFVRect Board(Win.X + 10.f, Y, Win.W - 20.f, Bottom - Y);
			DrawBoard(D, Board);
			const FFVRect ListA = Board.Inset(bP ? 6.f : 7.f);
			if (Idx.Num() == 0)
			{
				D.Panel(FFVRect(ListA.Center().X - 130.f, ListA.Center().Y - 42.f, 260.f, 84.f), EFVPanel::Parchment);
				EmptyState(D, FFVRect(ListA.Center().X - 126.f, ListA.Center().Y - 40.f, 252.f, 80.f), EFVIcon::Order,
					TEXT("Сегодня заказов нет. Новые появятся завтра утром."), bP ? FV::Ui::Small() : 7.f);
			}
			else
			{
				// smooth kinetic list of notes (drag / fling / wheel); cards act on a clean release only
				const float CardH = bP ? 102.f : 58.f;
				const float Pitch = CardH + (bP ? 6.f : 5.f);
				const float Pad = 2.f;   // room for the selection frame / pin at the top
				Scroll.Update(D, ListA, Pad + float(Idx.Num()) * Pitch - (Pitch - CardH) + 4.f);
				if (bEnsureSel && Sel >= 0)
				{
					Scroll.ScrollToShow(float(Sel) * Pitch, CardH + Pad + 4.f);
					bEnsureSel = false;
				}
				const float CW = ListA.W - (Scroll.CanScroll() ? 8.f : 0.f);
				D.PushClip(ListA);
				for (int32 k = 0; k < Idx.Num(); ++k)
				{
					const float CY = ListA.Y + Pad - Scroll.Offset() + float(k) * Pitch;
					if (!RowVisible(ListA, CY, CardH + 3.f)) { continue; }
					const FFVOrder& O = Orders[Idx[k]];
					DrawCard(D, *P, O, FFVRect(ListA.X, CY, CW, CardH), k == Sel, Now, k);
				}
				D.PopClip();
				Scroll.DrawFades(D, ListA, FV::Col(196, 150, 96));
				Scroll.DrawBar(D, ListA);
			}

			// ---- footer (portrait) / key hint (landscape)
			if (bP)
			{
				FFVRect ActR;
				if (D.ActionBar(Win, &ActR)) { bClose = true; }
				if (Idx.IsValidIndex(Sel)) { DrawAction(D, *P, Orders[Idx[Sel]], ActR); }
				else { D.Button(ActR, TEXT("Нет заказов"), EFVButton::Normal, false, FV::Ui::Body() + 0.5f); }
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
		FName SelOrder;
		FFVScrollView Scroll;     // smooth list of notes
		bool bEnsureSel = false;
		int32 PendingMove = 0;
		EAct Act = EAct::None;
		FName ActId;

		void DrawBoard(FVDraw& D, const FFVRect& R)
		{
			D.Rect(R, FV::Col(92, 52, 26));
			D.Rect(R.Inset(2.f), FV::Col(146, 90, 46));
			const FFVRect Cork = R.Inset(4.f);
			D.Rect(Cork, FV::Col(196, 150, 96));
			for (int32 i = 0; i < 160; ++i)
			{
				const float PX = Cork.X + FVUI::Hash01(i, 1, 404) * (Cork.W - 2.f);
				const float PY = Cork.Y + FVUI::Hash01(i, 2, 404) * (Cork.H - 2.f);
				D.Rect(FFVRect(FMath::RoundToFloat(PX), FMath::RoundToFloat(PY), 1.f, 1.f), FVUI::Hash01(i, 3, 404) > 0.5f ? FV::Col(170, 120, 70) : FV::Col(216, 176, 120));
			}
		}

		/** The main button of an order (inline on landscape cards, in the bottom bar on portrait). */
		void DrawAction(FVDraw& D, UFVSaveGame& P, const FFVOrder& O, const FFVRect& R)
		{
			const bool bP = FV::IsPortrait();
			const float TS = bP ? FV::Ui::Body() + 0.5f : 7.5f;
			const EFVOrderState St = StateOf(O);
			FString Label;
			bool bEnabled = false;
			EFVButton Style = EFVButton::Normal;
			EAct What = EAct::None;
			int64 Price = -1;
			switch (St)
			{
			case EFVOrderState::Offered:
				if (P.TakenOrders() >= MaxTaken) { Label = TEXT("Уже 3 заказа"); }
				else if (!P.CanAfford(O.Deposit)) { Label = TEXT("Нет денег на залог"); }
				else { Label = FString::Printf(TEXT("Взять (залог %s)"), *FV::Money(O.Deposit)); bEnabled = true; Style = EFVButton::Primary; What = EAct::Take; }
				break;
			case EFVOrderState::Taken:
				bEnabled = P.CanDeliver(O);
				Label = bEnabled ? TEXT("Сдать заказ") : TEXT("Не хватает товара");
				Style = bEnabled ? EFVButton::Primary : EFVButton::Normal;
				What = bEnabled ? EAct::Deliver : EAct::None;
				break;
			case EFVOrderState::Done: Label = TEXT("Выполнен"); break;
			default: Label = TEXT("Сорван"); break;
			}
			// Buttons act on a clean release (update 3): dragging the list over a card's button never triggers it.
			const bool bPressed = CoinButton(D, R, Label, Price, Style, bEnabled, true, TS);
			if (bPressed && bEnabled && What != EAct::None && Act == EAct::None)
			{
				Act = What;
				ActId = O.Id;
				SelOrder = O.Id;
			}
		}

		void DrawCard(FVDraw& D, UFVSaveGame& P, const FFVOrder& O, const FFVRect& R, bool bSel, double Now, int32 Index)
		{
			const bool bP = FV::IsPortrait();
			const EFVOrderState St = StateOf(O);
			const bool bClosed = St == EFVOrderState::Done || St == EFVOrderState::Failed;
			// note paper: shadow, border, paper, pin (slightly tilted look via an offset stripe)
			D.Rect(R.Offset(2.f, 2.f), FV::Col(60, 34, 16, 110));
			D.Rect(R, bSel ? FV::Col(214, 52, 52) : FV::Col(170, 120, 70));
			D.Rect(R.Inset(bSel ? 2.f : 1.f), bClosed ? FV::Col(232, 218, 190) : (bSel ? FV::Col(255, 250, 230) : FV::Col(255, 242, 212)));
			D.Rect(FFVRect(R.X + 2.f, R.Y + 2.f, R.W - 4.f, 1.f), FV::Col(255, 255, 255, 120));
			const FVector2D Pin(R.Center().X + (Index % 2 ? 40.f : -30.f), R.Y + 3.f);
			D.Circle(Pin + FVector2D(1.f, 1.f), 3.f, FV::Col(60, 20, 10, 120));
			D.Circle(Pin, 3.f, St == EFVOrderState::Taken ? FV::Col(60, 150, 60) : FV::Col(200, 50, 44));
			D.Circle(Pin + FVector2D(-1.f, -1.f), 1.f, FV::Col(255, 220, 210));

			const float Pad = bP ? 8.f : 6.f;
			const float AvS = bP ? 36.f : 42.f;
			const FFVRect Av(R.X + Pad, R.Y + (bP ? Pad : (R.H - AvS) * 0.5f), AvS, AvS);
			DrawAvatar(D, O.Customer, Av);
			const float TX = Av.Right() + 8.f;
			const float NameS = bP ? FV::Ui::Body() : 8.f;
			const float TextS = bP ? FV::Ui::Body() : 7.5f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;

			// state chip
			FString Chip;
			FVUI::EChip ChipK = FVUI::EChip::Neutral;
			const bool bCanDeliver = St == EFVOrderState::Taken && P.CanDeliver(O);
			switch (St)
			{
			case EFVOrderState::Offered: Chip = TEXT("Новый"); break;
			case EFVOrderState::Taken: Chip = bCanDeliver ? TEXT("Можно сдать") : TEXT("Взят"); ChipK = FVUI::EChip::Good; break;
			case EFVOrderState::Done: Chip = TEXT("Выполнен"); ChipK = FVUI::EChip::Good; break;
			default: Chip = TEXT("Сорван"); ChipK = FVUI::EChip::Bad; break;
			}

			// right column (landscape): reward, deposit, button
			const float RightW = bP ? 0.f : 150.f;
			const float MidRight = R.Right() - Pad - RightW - (bP ? 0.f : 6.f);
			const float ChipW = FVUI::StatusChip(D, Chip, FVector2D(MidRight, R.Y + (bP ? 7.f : 5.f)), ChipK, false, bP ? 7.f : 6.f);
			D.Text(D.Ellipsize(O.Customer, MidRight - ChipW - 6.f - TX, NameS), FVector2D(TX, R.Y + (bP ? 6.f : 4.f)), NameS, FVPal::Ink(), EFVAlign::Left, false, true);

			// request: icon + name ×count (+ quality) and "есть N/M"
			const float ReqY = R.Y + (bP ? 28.f : 18.f);
			const float IS = bP ? 20.f : 14.f;
			IconBox(D, FFVRect(TX, ReqY, IS, IS));
			DrawItemIcon(D, O.ItemId, FFVRect(TX + 1.f, ReqY + 1.f, IS - 2.f, IS - 2.f), 1.f);
			const int32 Have = HaveFor(P, O);
			const FString HaveText = FString::Printf(TEXT("есть %d/%d"), FMath::Min(Have, 999), O.Count);
			const float HW = bClosed ? 0.f : D.MeasureText(HaveText, SS, true).X;
			if (!bClosed)
			{
				D.Text(HaveText, FVector2D(MidRight, ReqY + (IS - D.LineHeight(SS)) * 0.5f), SS, Have >= O.Count ? InkGood() : InkBad(), EFVAlign::Right, false, true);
			}
			float QX = TX + IS + 5.f;
			const FString Req = FString::Printf(TEXT("%s ×%d"), *RequestName(O), O.Count);
			const float QualW = O.MinQuality > 0 ? 12.f : 0.f;
			const FString ReqShown = D.Ellipsize(Req, FMath::Max(20.f, MidRight - HW - 8.f - QualW - QX), TextS, true);
			D.Text(ReqShown, FVector2D(QX, ReqY + (IS - D.LineHeight(TextS)) * 0.5f), TextS, FVPal::Ink(), EFVAlign::Left, false, true);
			QX += D.MeasureText(ReqShown, TextS, true).X + 3.f;
			if (O.MinQuality > 0)
			{
				D.Stars(FVector2D(QX, ReqY + (IS - 8.f) * 0.5f), QualityOf(O.MinQuality), 1.f);
				if (!bP && D.Hover(FFVRect(QX - 2.f, ReqY, 12.f, IS)))
				{
					D.Tooltip(TEXT("Качество"), FString::Printf(TEXT("Не ниже: %s"), *FVDB::QualityName(QualityOf(O.MinQuality))));
				}
			}

			// time line
			FString TimeText;
			FLinearColor TimeC = FVPal::InkSoft();
			if (St == EFVOrderState::Offered)
			{
				TimeText = FString::Printf(TEXT("Срок: %s после взятия"), *UFVGame::DurationText(float(O.DurationHours) * 60.f));
			}
			else if (St == EFVOrderState::Taken)
			{
				const float Left = float(O.Deadline - Now);
				TimeText = Left > 0.f ? FString::Printf(TEXT("Сдать до: %s · осталось %s"), *Game->AbsTimeText(O.Deadline), *UFVGame::DurationText(Left))
					: FString(TEXT("Срок вышел"));
				TimeC = Left < 360.f ? InkBad() : (Left < 720.f ? InkWarn() : FVPal::Ink());
			}
			else if (St == EFVOrderState::Done) { TimeText = TEXT("Заказ выполнен — спасибо!"); TimeC = InkGood(); }
			else { TimeText = TEXT("Не успели — залог потерян"); TimeC = InkBad(); }
			if (O.MinQuality > 0 && bP && St == EFVOrderState::Offered)
			{
				TimeText = FString::Printf(TEXT("%s · качество от «%s»"), *TimeText, *FVDB::QualityName(QualityOf(O.MinQuality)));
			}

			if (bP)
			{
				// reward row + time row
				const float RY = R.Y + 54.f;
				D.Text(TEXT("Награда:"), FVector2D(TX, RY + 1.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				float MX = TX + D.MeasureText(TEXT("Награда:"), SS, false).X + 5.f;
				MX += D.MoneyLabel(FVector2D(MX, RY), O.Reward, TextS, EFVAlign::Left, FV::Col(120, 72, 20)) + 6.f;
				if (!O.RewardItem.IsNone() && O.RewardItemCount > 0)
				{
					DrawItemIcon(D, O.RewardItem, FFVRect(MX, RY - 2.f, 16.f, 16.f), 1.f);
					D.Text(FString::Printf(TEXT("×%d"), O.RewardItemCount), FVector2D(MX + 17.f, RY + 1.f), SS, FVPal::Ink(), EFVAlign::Left, false, true);
				}
				D.Text(FString::Printf(TEXT("Залог %s"), *FV::Money(O.Deposit)), FVector2D(R.Right() - Pad, RY + 1.f), SS, FVPal::InkSoft(), EFVAlign::Right, false, true);
				D.Text(D.Ellipsize(TimeText, R.Right() - Pad - TX, SS, false), FVector2D(TX, R.Y + 76.f), SS, TimeC, EFVAlign::Left, false, false);
				if (D.Clicked(R) && !bSel)
				{
					SelOrder = O.Id;
					FVUI::Sfx(EFVSfx::Click);
				}
				return;
			}

			D.Text(D.Ellipsize(TimeText, MidRight - TX, SS, false), FVector2D(TX, R.Y + 38.f), SS, TimeC, EFVAlign::Left, false, false);
			// right column
			const float CX = R.Right() - Pad - RightW;
			D.Rect(FFVRect(CX - 5.f, R.Y + 6.f, 1.f, R.H - 12.f), FV::Col(214, 170, 120));
			D.Text(TEXT("Награда"), FVector2D(CX, R.Y + 5.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			float MX = CX + D.MeasureText(TEXT("Награда"), SS, false).X + 5.f;
			MX += D.MoneyLabel(FVector2D(MX, R.Y + 4.f), O.Reward, 8.f, EFVAlign::Left, FV::Col(120, 72, 20)) + 4.f;
			if (!O.RewardItem.IsNone() && O.RewardItemCount > 0)
			{
				const FFVRect RI(MX, R.Y + 2.f, 14.f, 14.f);
				DrawItemIcon(D, O.RewardItem, RI, 1.f);
				D.Text(FString::Printf(TEXT("×%d"), O.RewardItemCount), FVector2D(RI.Right() + 1.f, R.Y + 5.f), SS, FVPal::Ink(), EFVAlign::Left, false, true);
				if (D.Hover(RI)) { D.Tooltip(TEXT("Бонус"), FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(O.RewardItem), O.RewardItemCount)); }
			}
			D.Text(FString::Printf(TEXT("Залог: %s"), *FV::Money(O.Deposit)), FVector2D(CX, R.Y + 17.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			const FFVRect Btn(CX, R.Bottom() - 26.f, RightW, 20.f);
			DrawAction(D, P, O, Btn);            // the button consumes its own click first
			if (D.Clicked(R) && !bSel)
			{
				SelOrder = O.Id;
				FVUI::Sfx(EFVSfx::Click);
			}
		}

		void RunAct(UFVSaveGame& P)
		{
			const EAct A = Act;
			Act = EAct::None;
			if (A == EAct::None || !Game) { return; }
			const FFVOrder* O = P.Orders.FindByPredicate([this](const FFVOrder& X) { return X.Id == ActId; });
			if (!O) { return; }
			if (A == EAct::Take) { ConfirmTake(Game, *O); }
			else { DoDeliver(Game, P, ActId); }
		}
	};
}

TUniquePtr<FVScreen> FVMakeOrdersScreen(UFVGame* Game)
{
	return MakeUnique<FVOrdersScreenImpl::FOrdersScreen>(Game);
}
