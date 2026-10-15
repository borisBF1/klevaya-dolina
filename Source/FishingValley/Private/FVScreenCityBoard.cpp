// UPDATE 6 [CITY] — «Доска заданий Города» (Docs/UPDATE6_FOREST_CITY.md §8.2, Р33, Р34). Owner: CITY.
// Like the pond board: cards (villager portrait, request, quality, deposit, reward, deadline, «сведение» badge),
// «Взять (залог N)» (confirm) and «Сдать». A delivered task with a fact shows the villager's line (FFVFishFact::Tell)
// with the portrait (UFVSprites::NpcPortrait); the fact opens in the collection at once.
// Landscape: every card has its own button. Portrait: tap a card to select it, the action sits in the bottom bar.

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVSprites.h"

namespace FVScreenCityBoardImpl
{
	FV_HOME_UI_USING();

	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr int32 MaxTaken = 3;

	static EFVOrderState StateOf(const FFVCityTask& T)
	{
		return EFVOrderState(FMath::Clamp<int32>(T.State, 0, int32(EFVOrderState::Count) - 1));
	}

	static int32 StateRank(const FFVCityTask& T)
	{
		switch (StateOf(T))
		{
		case EFVOrderState::Taken: return 0;
		case EFVOrderState::Offered: return 1;
		case EFVOrderState::Done: return 2;
		default: return 3;
		}
	}

	static FString NpcName(FName Npc)
	{
		const FFVNpcDef* N = FVDBU6::FindNpc(Npc);
		return N ? N->Name : FString(TEXT("Житель Города"));
	}

	static bool IsFish(FName Id) { return FVDB::FindFish(Id) != nullptr; }

	/** Units the player holds for the task (the «есть N/M» hint; delivery uses CanDeliverCityTask). */
	static int32 HaveFor(const UFVSaveGame& P, const FFVCityTask& T)
	{
		if (IsFish(T.ItemId))
		{
			int32 N = 0;
			for (const FFVInvStack& S : P.Keepnet)
			{
				if (S.ItemId == T.ItemId && S.Quality >= T.MinQuality && (S.Flags & FVStackFlags::Smoked) == 0) { N += S.Count; }
			}
			return N;
		}
		if (FVDB::FindBait(T.ItemId)) { return P.BaitCount(T.ItemId); }
		return P.CountItem(T.ItemId);
	}

	static void DrawPortrait(FVDraw& D, FName Npc, const FFVRect& R)
	{
		D.Rect(R, FV::Col(60, 44, 30));
		D.Rect(R.Inset(1.f), FV::Col(170, 200, 220));
		if (UFVSprites* Sp = UFVSprites::Get())
		{
			FVUI::SpriteFit(D, Sp->NpcPortrait(Npc), R.Inset(1.f), FLinearColor::White, 3.f);
		}
	}

	/** «сведение» badge (scroll icon): a fact about a (surprise) fish comes with the reward. */
	static float FactBadge(FVDraw& D, const FVector2D& Pos, float Size)
	{
		const FString L = TEXT("сведение");
		const float TW = D.MeasureText(L, Size, true).X;
		const float H = FMath::RoundToFloat(D.LineHeight(Size, true) + 2.f);
		const FFVRect R(Pos.X, Pos.Y, H + TW + 6.f, H);
		D.Rect(R, FV::Col(120, 80, 20));
		D.Rect(R.Inset(1.f), FV::Col(250, 226, 150));
		DrawIcon(D, EFVIcon::Scroll, FFVRect(R.X + 1.f, R.Y + 1.f, H - 2.f, H - 2.f), 1.f);
		D.Text(L, FVector2D(R.X + H + 2.f, R.Y + 1.f), Size, FV::Col(110, 64, 10), EFVAlign::Left, false, true);
		if (D.Hover(R))
		{
			D.Tooltip(TEXT("Сведение о рыбе"), TEXT("Сдав задание, житель расскажет особенность рыбы — условие откроется в коллекции. О какой рыбе — сюрприз."));
		}
		return R.W;
	}

	static void ConfirmTake(UFVGame* Game, const FFVCityTask& T)
	{
		if (!Game) { return; }
		TWeakObjectPtr<UFVGame> Weak(Game);
		const FName Id = T.Id;
		FString Body = FString::Printf(TEXT("%s просит: %s ×%d."), *NpcName(T.Npc), *FVDB::ItemName(T.ItemId), T.Count);
		if (T.MinQuality > 0)
		{
			Body += FString::Printf(TEXT(" Качество — не ниже «%s» (подойдёт и рыба с обычной удочки)."), *FVDB::QualityName(QualityOf(T.MinQuality)));
		}
		Body += FString::Printf(TEXT("\nЗалог %s вернётся вместе с наградой (%s), если сдать за %s. Не успеете — залог пропадёт."),
			*FVUI::Coins(T.Deposit), *FVUI::Coins(T.Reward), *UFVGame::DurationText(float(T.DurationHours) * 60.f));
		if (T.bFact) { Body += TEXT("\nВ награду — сведение о рыбе."); }
		Game->Confirm(TEXT("Взять задание?"), Body, [Weak, Id]()
		{
			UFVGame* G = Weak.Get();
			UFVSaveGame* P = G ? G->GetProfile() : nullptr;
			if (!P) { return; }
			if (P->TakenCityTasks() >= MaxTaken)
			{
				Fail(G, TEXT("Можно держать не больше трёх заданий"));
				return;
			}
			const EFVBuyResult R = P->TakeCityTask(Id, G->GetAbsMinutes());
			if (R != EFVBuyResult::Ok)
			{
				Fail(G, R);
				return;
			}
			Success(G, TEXT("Задание взято — не опоздайте!"), EFVSfx::Coin, EFVIcon::Scroll);
			Refresh(G);
		}, TEXT("Взять"), TEXT("Отмена"));
	}

	enum class EAct : uint8 { None, Take, Deliver };

	class FCityBoardScreen final : public FVScreen
	{
	public:
		explicit FCityBoardScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			TickCount = 0;
			bSpeech = false;
			if (UFVSaveGame* P = Game ? Game->GetProfile() : nullptr)
			{
				if (P->U6.bVisitedCity) { P->RefreshCityTasks(); }
				Game->NotifyProfileChanged();
			}
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (bSpeech && (In.WasPressed(EKeys::Enter) || In.WasPressed(EKeys::SpaceBar))) { bSpeech = false; return; }
			if (In.WasPressed(EKeys::Up)) { PendingMove = -1; }
			else if (In.WasPressed(EKeys::Down)) { PendingMove = 1; }
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = bP ? FV::WindowRect() : WinRect;
			bool bClose = D.Window(Win, TEXT("Доска заданий Города"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Act = EAct::None;
			if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { bClose = true; }
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}

			const TArray<FFVCityTask>& Tasks = P->U6.CityTasks;
			TArray<int32> Idx;
			for (int32 i = 0; i < Tasks.Num(); ++i) { Idx.Add(i); }
			Idx.StableSort([&Tasks](int32 A, int32 B) { return StateRank(Tasks[A]) < StateRank(Tasks[B]); });
			int32 Sel = Idx.IndexOfByPredicate([&Tasks, this](int32 i) { return Tasks[i].Id == SelTask; });
			if (Sel == INDEX_NONE && Idx.Num() > 0)
			{
				Sel = 0;
				SelTask = Tasks[Idx[0]].Id;
			}
			if (PendingMove != 0 && Idx.Num() > 0 && !bSpeech)
			{
				Sel = FMath::Clamp(Sel + PendingMove, 0, Idx.Num() - 1);
				SelTask = Tasks[Idx[Sel]].Id;
				bEnsureSel = true;
				FVUI::Sfx(EFVSfx::Click);
			}
			PendingMove = 0;
			const double Now = Game->GetAbsMinutes();

			// ---- header
			const float X = Win.X + (bP ? 10.f : 12.f);
			const float W = Win.W - (bP ? 20.f : 24.f);
			float Y = Win.Y + (bP ? 16.f : 10.f);
			{
				const float HS = bP ? FV::Ui::Body() + 1.f : 9.f;
				const float SS = bP ? FV::Ui::Small() : 6.5f;
				const int32 Taken = P->TakenCityTasks();
				DrawIcon(D, EFVIcon::Scroll, FFVRect(X, Y, bP ? 24.f : 20.f, bP ? 24.f : 20.f), 2.f);
				const float TX = X + (bP ? 30.f : 26.f);
				D.Text(FString::Printf(TEXT("Взято заданий: %d / %d"), Taken, MaxTaken), FVector2D(TX, Y - 1.f), HS, Taken >= MaxTaken ? InkWarn() : FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(FString::Printf(TEXT("Сейчас: %s · сведений: %d"), *Game->AbsTimeText(Now), P->TotalFactsKnown()),
					FVector2D(TX, Y + D.LineHeight(HS)), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				D.MoneyLabel(FVector2D(X + W, Y + 1.f), P->Money, bP ? FV::Ui::Body() : 9.f, EFVAlign::Right);
				Y += bP ? 32.f : 26.f;
			}

			// ---- board with the notes
			const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 10.f : Win.Bottom() - 10.f;
			const FFVRect Board(Win.X + 10.f, Y, Win.W - 20.f, Bottom - Y);
			DrawBoard(D, Board);
			const FFVRect ListA = Board.Inset(bP ? 6.f : 7.f);
			if (bSpeech)
			{
				DrawSpeech(D, ListA);
			}
			else if (Idx.Num() == 0)
			{
				D.Panel(FFVRect(ListA.Center().X - 130.f, ListA.Center().Y - 42.f, 260.f, 84.f), EFVPanel::Parchment);
				EmptyState(D, FFVRect(ListA.Center().X - 126.f, ListA.Center().Y - 40.f, 252.f, 80.f), EFVIcon::Scroll,
					P->U6.bVisitedCity ? TEXT("Сегодня заданий нет. Новые появятся завтра утром.") : TEXT("Жители ждут вас в Городе."),
					bP ? FV::Ui::Small() : 7.f);
			}
			else
			{
				const float CardH = bP ? 108.f : 60.f;
				const float Pitch = CardH + (bP ? 6.f : 5.f);
				const float Pad = 2.f;
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
					DrawCard(D, *P, Tasks[Idx[k]], FFVRect(ListA.X, CY, CW, CardH), k == Sel, Now);
				}
				D.PopClip();
				Scroll.DrawFades(D, ListA, FV::Col(176, 150, 118));
				Scroll.DrawBar(D, ListA);
			}

			if (bP)
			{
				FFVRect ActR;
				if (D.ActionBar(Win, &ActR)) { bClose = true; }
				if (bSpeech)
				{
					if (D.Button(ActR, TEXT("Спасибо!"), EFVButton::Primary, true, FV::Ui::Body() + 0.5f)) { bSpeech = false; }
				}
				else if (Idx.IsValidIndex(Sel)) { DrawAction(D, *P, Tasks[Idx[Sel]], ActR); }
				else { D.Button(ActR, TEXT("Нет заданий"), EFVButton::Normal, false, FV::Ui::Body() + 0.5f); }
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
		FName SelTask;
		FFVScrollView Scroll;
		bool bEnsureSel = false;
		int32 PendingMove = 0;
		EAct Act = EAct::None;
		FName ActId;
		// the villager's line after a delivery with a fact
		bool bSpeech = false;
		FName SpeechNpc;
		FString SpeechText;
		FString SpeechSub;

		/** City board: a stone frame with light planks (not the pond cork). */
		void DrawBoard(FVDraw& D, const FFVRect& R)
		{
			D.Rect(R, FV::Col(58, 62, 70));
			D.Rect(R.Inset(2.f), FV::Col(110, 116, 126));
			const FFVRect In = R.Inset(4.f);
			D.Rect(In, FV::Col(176, 150, 118));
			for (float PY = In.Y + 14.f; PY < In.Bottom(); PY += 14.f)
			{
				D.Rect(FFVRect(In.X, FMath::RoundToFloat(PY), In.W, 1.f), FV::Col(150, 124, 94));
			}
		}

		void DrawSpeech(FVDraw& D, const FFVRect& A)
		{
			const bool bP = FV::IsPortrait();
			const float PW = FMath::Min(A.W - 8.f, bP ? 330.f : 440.f);
			const float TS = bP ? FV::Ui::Body() : 8.f;
			const float SS = bP ? FV::Ui::Small() : 7.f;
			const float PS = bP ? 56.f : 48.f;
			const float TextW = PW - PS - 30.f;
			const float TextH = D.MeasureWrapped(SpeechText, TextW, TS);
			const float PH = FMath::Min(A.H - 8.f, FMath::Max(PS + 20.f, TextH + D.LineHeight(TS, true) + D.LineHeight(SS) * 2.f + (bP ? 26.f : 50.f)));
			const FFVRect Pn(A.Center().X - PW * 0.5f, A.Center().Y - PH * 0.5f, PW, PH);
			D.Panel(Pn, EFVPanel::Parchment);
			const FFVRect Pr(Pn.X + 10.f, Pn.Y + 10.f, PS, PS);
			DrawPortrait(D, SpeechNpc, Pr);
			const float TX = Pr.Right() + 10.f;
			float Y = Pn.Y + 10.f;
			D.Text(NpcName(SpeechNpc), FVector2D(TX, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			Y += D.LineHeight(TS, true) + 3.f;
			Y += D.TextWrapped(SpeechText, FVector2D(TX, Y), TextW, TS, FV::Col(70, 44, 20)) + 4.f;
			if (!SpeechSub.IsEmpty())
			{
				DrawIcon(D, EFVIcon::Scroll, FFVRect(TX, Y, 12.f, 12.f), 1.f);
				D.Text(D.Ellipsize(SpeechSub, TextW - 16.f, SS), FVector2D(TX + 15.f, Y + 1.f), SS, InkGood(), EFVAlign::Left, false, true);
			}
			if (!bP)
			{
				const FFVRect Btn(Pn.Right() - 110.f, Pn.Bottom() - 28.f, 100.f, 20.f);
				if (D.Button(Btn, TEXT("Спасибо!"), EFVButton::Primary, true, 8.f)) { bSpeech = false; }
			}
		}

		void DrawAction(FVDraw& D, UFVSaveGame& P, const FFVCityTask& T, const FFVRect& R)
		{
			const bool bP = FV::IsPortrait();
			const float TS = bP ? FV::Ui::Body() + 0.5f : 7.5f;
			FString Label;
			bool bEnabled = false;
			EFVButton Style = EFVButton::Normal;
			EAct What = EAct::None;
			switch (StateOf(T))
			{
			case EFVOrderState::Offered:
				if (P.TakenCityTasks() >= MaxTaken) { Label = TEXT("Уже 3 задания"); }
				else if (!P.CanAfford(T.Deposit)) { Label = TEXT("Нет денег на залог"); }
				else { Label = FString::Printf(TEXT("Взять (залог %s)"), *FV::Money(T.Deposit)); bEnabled = true; Style = EFVButton::Primary; What = EAct::Take; }
				break;
			case EFVOrderState::Taken:
				bEnabled = P.CanDeliverCityTask(T);
				Label = bEnabled ? TEXT("Сдать задание") : TEXT("Не хватает товара");
				Style = bEnabled ? EFVButton::Primary : EFVButton::Normal;
				What = bEnabled ? EAct::Deliver : EAct::None;
				break;
			case EFVOrderState::Done: Label = TEXT("Выполнено"); break;
			default: Label = TEXT("Сорвано"); break;
			}
			const bool bPressed = CoinButton(D, R, Label, -1, Style, bEnabled, true, TS);
			if (bPressed && bEnabled && What != EAct::None && Act == EAct::None)
			{
				Act = What;
				ActId = T.Id;
				SelTask = T.Id;
			}
		}

		void DrawCard(FVDraw& D, UFVSaveGame& P, const FFVCityTask& T, const FFVRect& R, bool bSel, double Now)
		{
			const bool bP = FV::IsPortrait();
			const EFVOrderState St = StateOf(T);
			const bool bClosed = St == EFVOrderState::Done || St == EFVOrderState::Failed;
			D.Rect(R.Offset(2.f, 2.f), FV::Col(40, 30, 20, 110));
			D.Rect(R, bSel ? FV::Col(40, 110, 170) : FV::Col(130, 110, 86));
			D.Rect(R.Inset(bSel ? 2.f : 1.f), bClosed ? FV::Col(226, 220, 204) : (bSel ? FV::Col(250, 252, 255) : FV::Col(244, 238, 222)));
			D.Rect(FFVRect(R.X + 2.f, R.Y + 2.f, R.W - 4.f, 1.f), FV::Col(255, 255, 255, 120));

			const float Pad = bP ? 8.f : 6.f;
			const float AvS = bP ? 40.f : 46.f;
			const FFVRect Av(R.X + Pad, R.Y + (bP ? Pad : (R.H - AvS) * 0.5f), AvS, AvS);
			DrawPortrait(D, T.Npc, Av);
			const float TX = Av.Right() + 8.f;
			const float NameS = bP ? FV::Ui::Body() : 8.f;
			const float TextS = bP ? FV::Ui::Body() : 7.5f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;

			FString Chip;
			FVUI::EChip ChipK = FVUI::EChip::Neutral;
			const bool bCanDeliver = St == EFVOrderState::Taken && P.CanDeliverCityTask(T);
			switch (St)
			{
			case EFVOrderState::Offered: Chip = TEXT("Новое"); break;
			case EFVOrderState::Taken: Chip = bCanDeliver ? TEXT("Можно сдать") : TEXT("Взято"); ChipK = FVUI::EChip::Good; break;
			case EFVOrderState::Done: Chip = TEXT("Выполнено"); ChipK = FVUI::EChip::Good; break;
			default: Chip = TEXT("Сорвано"); ChipK = FVUI::EChip::Bad; break;
			}
			const float RightW = bP ? 0.f : 150.f;
			const float MidRight = R.Right() - Pad - RightW - (bP ? 0.f : 6.f);
			const float ChipW = FVUI::StatusChip(D, Chip, FVector2D(MidRight, R.Y + (bP ? 7.f : 5.f)), ChipK, false, bP ? 7.f : 6.f);
			float NameRight = MidRight - ChipW - 6.f;
			if (T.bFact && !bClosed)
			{
				const float BS = bP ? 7.f : 6.f;
				const float BW = D.MeasureText(TEXT("сведение"), BS, true).X + FMath::RoundToFloat(D.LineHeight(BS, true) + 2.f) + 6.f;
				FactBadge(D, FVector2D(NameRight - BW, R.Y + (bP ? 6.f : 4.f)), BS);
				NameRight -= BW + 4.f;
			}
			D.Text(D.Ellipsize(NpcName(T.Npc), FMath::Max(20.f, NameRight - TX), NameS), FVector2D(TX, R.Y + (bP ? 6.f : 4.f)), NameS, FVPal::Ink(), EFVAlign::Left, false, true);

			// request
			const float ReqY = R.Y + (bP ? 30.f : 19.f);
			const float IS = bP ? 20.f : 14.f;
			IconBox(D, FFVRect(TX, ReqY, IS, IS));
			DrawItemIcon(D, T.ItemId, FFVRect(TX + 1.f, ReqY + 1.f, IS - 2.f, IS - 2.f), 1.f);
			const int32 Have = HaveFor(P, T);
			// enough -> a check mark + «N/N» (never «есть 5/2»); not enough -> «есть 1/2»
			const bool bEnough = Have >= T.Count;
			const FString HaveText = bEnough ? FString::Printf(TEXT("%d/%d"), T.Count, T.Count) : FString::Printf(TEXT("есть %d/%d"), FMath::Max(Have, 0), T.Count);
			const FFVSprite* CheckIcon = bEnough ? FVUI::Icon(EFVIcon::Check) : nullptr;
			const float CkS = bP ? 11.f : 8.f;
			const float TW = D.MeasureText(HaveText, SS, true).X;
			const float HW = bClosed ? 0.f : TW + (CheckIcon ? CkS + 2.f : 0.f);
			if (!bClosed)
			{
				D.Text(HaveText, FVector2D(MidRight, ReqY + (IS - D.LineHeight(SS)) * 0.5f), SS, bEnough ? InkGood() : InkBad(), EFVAlign::Right, false, true);
				if (CheckIcon) { D.SpriteRect(*CheckIcon, FFVRect(FMath::RoundToFloat(MidRight - TW - 2.f - CkS), FMath::RoundToFloat(ReqY + (IS - CkS) * 0.5f), CkS, CkS)); }
			}
			float QX = TX + IS + 5.f;
			const float QualW = T.MinQuality > 0 ? 12.f : 0.f;
			const FString ReqShown = D.Ellipsize(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(T.ItemId), T.Count), FMath::Max(20.f, MidRight - HW - 8.f - QualW - QX), TextS, true);
			D.Text(ReqShown, FVector2D(QX, ReqY + (IS - D.LineHeight(TextS)) * 0.5f), TextS, FVPal::Ink(), EFVAlign::Left, false, true);
			QX += D.MeasureText(ReqShown, TextS, true).X + 3.f;
			if (T.MinQuality > 0)
			{
				D.Stars(FVector2D(QX, ReqY + (IS - 8.f) * 0.5f), QualityOf(T.MinQuality), 1.f);
				if (!bP && D.Hover(FFVRect(QX - 2.f, ReqY, 12.f, IS)))
				{
					D.Tooltip(TEXT("Качество"), FString::Printf(TEXT("Не ниже: %s. Подойдёт рыба и с обычной удочки."), *FVDB::QualityName(QualityOf(T.MinQuality))));
				}
			}

			// time / result line
			FString TimeText;
			FLinearColor TimeC = FVPal::InkSoft();
			if (St == EFVOrderState::Offered)
			{
				TimeText = FString::Printf(TEXT("Срок: %s после взятия"), *UFVGame::DurationText(float(T.DurationHours) * 60.f));
				if (T.MinQuality > 0) { TimeText += FString::Printf(TEXT(" · качество от «%s»"), *FVDB::QualityName(QualityOf(T.MinQuality))); }
			}
			else if (St == EFVOrderState::Taken)
			{
				const float Left = float(T.Deadline - Now);
				TimeText = Left > 0.f ? FString::Printf(TEXT("Сдать до: %s · осталось %s"), *Game->AbsTimeText(T.Deadline), *UFVGame::DurationText(Left))
					: FString(TEXT("Срок вышел"));
				TimeC = Left < 360.f ? InkBad() : (Left < 720.f ? InkWarn() : FVPal::Ink());
			}
			else if (St == EFVOrderState::Done)
			{
				TimeText = T.FactFish.IsNone() ? FString(TEXT("Задание выполнено — спасибо!"))
					: FString::Printf(TEXT("Выполнено · сведение: %s"), *FVDB::ItemName(T.FactFish));
				TimeC = InkGood();
			}
			else { TimeText = TEXT("Не успели — залог потерян"); TimeC = InkBad(); }

			if (bP)
			{
				const float RY = R.Y + 58.f;
				D.Text(TEXT("Награда:"), FVector2D(TX, RY + 1.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				const float MX = TX + D.MeasureText(TEXT("Награда:"), SS, false).X + 5.f;
				D.MoneyLabel(FVector2D(MX, RY), T.Reward, TextS, EFVAlign::Left, FV::Col(120, 72, 20));
				D.Text(FString::Printf(TEXT("Залог %s"), *FV::Money(T.Deposit)), FVector2D(R.Right() - Pad, RY + 1.f), SS, FVPal::InkSoft(), EFVAlign::Right, false, true);
				D.Text(D.Ellipsize(TimeText, R.Right() - Pad - TX, SS, false), FVector2D(TX, R.Y + 82.f), SS, TimeC, EFVAlign::Left, false, false);
				if (D.Clicked(R) && !bSel)
				{
					SelTask = T.Id;
					FVUI::Sfx(EFVSfx::Click);
				}
				return;
			}

			D.Text(D.Ellipsize(TimeText, MidRight - TX, SS, false), FVector2D(TX, R.Y + 40.f), SS, TimeC, EFVAlign::Left, false, false);
			const float CX = R.Right() - Pad - RightW;
			D.Rect(FFVRect(CX - 5.f, R.Y + 6.f, 1.f, R.H - 12.f), FV::Col(190, 170, 140));
			D.Text(TEXT("Награда"), FVector2D(CX, R.Y + 5.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			const float MX = CX + D.MeasureText(TEXT("Награда"), SS, false).X + 5.f;
			D.MoneyLabel(FVector2D(MX, R.Y + 4.f), T.Reward, 8.f, EFVAlign::Left, FV::Col(120, 72, 20));
			D.Text(FString::Printf(TEXT("Залог: %s"), *FV::Money(T.Deposit)), FVector2D(CX, R.Y + 17.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			DrawAction(D, P, T, FFVRect(CX, R.Bottom() - 26.f, RightW, 20.f));
			if (D.Clicked(R) && !bSel)
			{
				SelTask = T.Id;
				FVUI::Sfx(EFVSfx::Click);
			}
		}

		void DoDeliver(UFVSaveGame& P, FName Id)
		{
			UFVGame* G = Game;
			const FFVCityTask* T = P.U6.CityTasks.FindByPredicate([Id](const FFVCityTask& X) { return X.Id == Id; });
			if (!T || !G) { return; }
			const int32 Reward = T->Reward;
			const int32 Deposit = T->Deposit;
			const FName Npc = T->Npc;
			FName Fish;
			int32 Fact = INDEX_NONE;
			const EFVBuyResult R = P.DeliverCityTask(Id, Fish, Fact);
			if (R != EFVBuyResult::Ok)
			{
				Fail(G, R == EFVBuyResult::NotEnoughMaterials ? FString(TEXT("Не хватает товара для задания")) : ResultText(R));
				return;
			}
			FString Msg = FString::Printf(TEXT("%s: спасибо! +%s"), *NpcName(Npc), *FVUI::Coins(Reward));
			if (Deposit > 0) { Msg += FString::Printf(TEXT(", залог %s вернулся"), *FV::Money(Deposit)); }
			Success(G, Msg, EFVSfx::OrderDone, EFVIcon::Scroll);
			const TArray<FFVFishFact>& Facts = FVDBU6::Facts(Fish);
			if (!Fish.IsNone() && Facts.IsValidIndex(Fact))
			{
				const FFVFishFact& F = Facts[Fact];
				SpeechNpc = Npc;
				SpeechText = FString::Printf(TEXT("«%s»"), F.Tell.IsEmpty() ? *F.Row : *F.Tell);
				SpeechSub = FString::Printf(TEXT("Сведение открыто в коллекции: %s"), *FVDB::ItemName(Fish));
				bSpeech = true;
				G->Toast(FString::Printf(TEXT("Новое сведение: %s"), *FVDB::ItemName(Fish)), FVPal::Gold(), EFVIcon::Scroll);
			}
			Refresh(G);
		}

		void RunAct(UFVSaveGame& P)
		{
			const EAct A = Act;
			Act = EAct::None;
			if (A == EAct::None || !Game) { return; }
			const FFVCityTask* T = P.U6.CityTasks.FindByPredicate([this](const FFVCityTask& X) { return X.Id == ActId; });
			if (!T) { return; }
			if (A == EAct::Take) { ConfirmTake(Game, *T); }
			else { DoDeliver(P, ActId); }
		}
	};
}

TUniquePtr<FVScreen> FVMakeCityBoardScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenCityBoardImpl::FCityBoardScreen>(Game);
}
