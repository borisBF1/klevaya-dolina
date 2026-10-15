// UPDATE 6 [CITY] — the artel master Никифор: nets, locations, repair, report (Docs/UPDATE6_FOREST_CITY.md §8.3, Р35, Р36). Owner: CITY.
// Not hired: a pitch and «Нанять (20 000)». Hired: one smooth list — places (toggle chips), the artel nets on their spots
// (durability, catch, «порвана»), the artel's stock («Забрать»), the player's nets in the backpack («Отдать»);
// the bottom bar: «Починить всё» (rope + fiber for every damaged net) and «Выбросить порванные» (replaced from the stock).
// Р77: the header reply / summary / next collection and the list notes are read in full (wrapped, the header grows).

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVSprites.h"

namespace FVScreenArtelImpl
{
	FV_HOME_UI_USING();

	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr int64 HirePrice = 20000;   // = FVSGCity::ArtelHirePrice (FVSaveGameU6City.cpp)
	static const FName NpcId(TEXT("npc_nikifor"));

	/** Net spots of a location (same as the player's posts; the expert zones have none, Р35). */
	static int32 SpotsAt(FName Loc)
	{
		if (FVDBU6::IsExpertLocation(Loc) || FVDBU6::IsForestId(Loc)) { return 0; }
		const FString L = Loc.ToString().ToLower();
		if (L == TEXT("pond") || L == TEXT("river") || L == TEXT("swamp")) { return 2; }
		if (L == TEXT("mountain") || L == TEXT("ocean")) { return 1; }
		return 0;
	}

	static FString LocName(FName Loc)
	{
		const FFVLocationDef* L = FVDB::FindLocation(Loc);
		return L ? L->Name : Loc.ToString();
	}

	static float MaxDur(FName NetItem)
	{
		const FFVItemDef* D = FVDB::FindItem(NetItem);
		return (D && D->NetDurability > 0.f) ? D->NetDurability : 1.f;
	}

	static int32 CatchUnits(const FFVNetState& N)
	{
		int32 U = 0;
		for (const FFVInvStack& S : N.Catch) { U += FMath::Max(0, S.Count); }
		return U;
	}

	/** Р77: a descriptive line is read in full — one line as before when it fits, else wrapped (the lines below move
	    down; the gap after the last line stays that of a single line). Returns the height used. */
	static float FullText(FVDraw& D, const FString& S, const FVector2D& Pos, float W, float Size, const FLinearColor& C)
	{
		if (D.MeasureText(S, Size, false).X <= W)
		{
			D.Text(S, Pos, Size, C, EFVAlign::Left, false, false);
			return D.LineHeight(Size);
		}
		const float Step = FMath::RoundToFloat(D.LineHeight(Size, false) * 0.95f);   // TextWrappedLimited's line step
		return TextWrappedLimited(D, S, Pos, W, Size, C, 64) - Step + D.LineHeight(Size);
	}

	enum class EAct : uint8 { None, Hire, ToggleLoc, Give, GiveAll, Take, Repair, Discard };

	class FArtelScreen final : public FVScreen
	{
	public:
		explicit FArtelScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			TickCount = 0;
			if (UFVSaveGame* P = Game ? Game->GetProfile() : nullptr)
			{
				if (P->U6.bArtelHired && P->ArtelDeploy() > 0) { Game->NotifyProfileChanged(); }
			}
		}

		virtual void Tick(float Dt, const FFVInput& In) override { ++TickCount; }

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = bP ? FV::WindowRect() : WinRect;
			bool bClose = D.Window(Win, TEXT("Артельщик Никифор"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Act = EAct::None;
			if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { bClose = true; }
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}

			const float X = Win.X + (bP ? 10.f : 12.f);
			const float W = Win.W - (bP ? 20.f : 24.f);
			float Y = Win.Y + (bP ? 14.f : 8.f);
			Y = DrawHeader(D, *P, X, Y, W);

			const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 6.f : Win.Bottom() - 36.f;
			const FFVRect Area(X, Y, W, FMath::Max(20.f, Bottom - Y));
			if (!P->U6.bArtelHired) { DrawPitch(D, *P, Area); }
			else { DrawList(D, *P, Area); }

			// ---- actions
			if (bP)
			{
				FFVRect ActR;
				if (D.ActionBar(Win, &ActR)) { bClose = true; }
				DrawMainActions(D, *P, ActR);
			}
			else
			{
				DrawMainActions(D, *P, FFVRect(X, Win.Bottom() - 30.f, W, 22.f));
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
		FFVScrollView Scroll;
		float ContentH = 0.f;
		EAct Act = EAct::None;
		FName ActId;
		int32 ActIndex = INDEX_NONE;

		float SmallS() const { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }
		float BodyS() const { return FV::IsPortrait() ? FV::Ui::Body() : 7.5f; }

		void Want(EAct A, FName Id = NAME_None, int32 Index = INDEX_NONE)
		{
			if (Act == EAct::None) { Act = A; ActId = Id; ActIndex = Index; }
		}

		float DrawHeader(FVDraw& D, const UFVSaveGame& P, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			const float PS = bP ? 40.f : 34.f;
			const FFVRect Pr(X, Y, PS, PS);
			D.Rect(Pr, FV::Col(60, 44, 30));
			D.Rect(Pr.Inset(1.f), FV::Col(170, 200, 220));
			if (UFVSprites* Sp = UFVSprites::Get()) { FVUI::SpriteFit(D, Sp->NpcPortrait(NpcId), Pr.Inset(1.f), FLinearColor::White, 3.f); }
			const float TX = Pr.Right() + 8.f;
			const float HS = bP ? FV::Ui::Body() + 1.f : 9.f;
			D.Text(TEXT("Артельщик Никифор"), FVector2D(TX, Y), HS, FVPal::Ink(), EFVAlign::Left, false, true);
			FString Sub;
			if (!P.U6.bArtelHired) { Sub = TEXT("«Отдайте мне сети — каждое утро буду продавать улов»"); }
			else
			{
				int32 Stock = 0;
				for (const FFVInvStack& S : P.U6.ArtelStock) { Stock += FMath::Max(0, S.Count); }
				Sub = FString::Printf(TEXT("Сетей на местах: %d · в запасе: %d · комиссия 20 %%"), P.U6.ArtelNets.Num(), Stock);
			}
			// Р77: the reply / the artel summary and the next collection are read in full: under the name (the money label
			// stays on the name line, so they take the whole column), wrapped if still too long; the header grows past the portrait
			float TY = Y + D.LineHeight(HS) + 1.f;
			TY += FullText(D, Sub, FVector2D(TX, TY), W - (TX - X), SmallS(), FVPal::InkSoft());
			if (P.U6.bArtelHired)
			{
				const double Wait = 12.0 - (P.U6.PlayedMinutes - P.U6.ArtelLastCollect);
				const FString When = Wait > 0.0
					? FString::Printf(TEXT("Следующий сбор — утром, не раньше чем через %d мин игры"), FMath::Max(1, FMath::CeilToInt(float(Wait))))
					: FString(TEXT("Сбор и продажа улова — каждое утро после 06:00"));
				TY += 1.f;
				TY += FullText(D, When, FVector2D(TX, TY), W - (TX - X), SmallS(), FVPal::InkSoft());
			}
			D.MoneyLabel(FVector2D(X + W, Y + 1.f), P.Money, bP ? FV::Ui::Body() : 9.f, EFVAlign::Right);
			return FMath::Max(Y + PS, TY) + 6.f;
		}

		void DrawPitch(FVDraw& D, const UFVSaveGame& P, const FFVRect& A)
		{
			D.Panel(A, EFVPanel::Parchment);
			const FFVRect In = A.Inset(10.f);
			float Y = In.Y;
			const float TS = BodyS();
			Y += D.TextWrapped(TEXT("«Сети в Городе и Пещере не ставят — море там злое. Зато я расставлю ваши сети на пруду, реке, в горах, на причале и болоте, каждое утро соберу улов и продам его по цене лавки. Себе беру двадцатую часть… то есть 20 %.»"),
				FVector2D(In.X, Y), In.W, TS, FV::Col(70, 44, 20)) + 6.f;
			const TCHAR* Lines[] = {
				TEXT("Отдайте ему купленные сети и выберите места"),
				TEXT("Сбор улова — раз в игровое утро, но не чаще, чем раз в 12 минут игры"),
				TEXT("Чинить сети: у самой сети, «Починить всё» у артельщика или выбросить порванные"),
				TEXT("Экспертных рыб и легенд сети не ловят"),
			};
			for (const TCHAR* L : Lines)
			{
				D.Circle(FVector2D(In.X + 3.f, Y + D.LineHeight(SmallS()) * 0.5f), 1.5f, FV::Col(120, 80, 40));
				Y += D.TextWrapped(L, FVector2D(In.X + 9.f, Y), In.W - 9.f, SmallS() + 0.5f, FVPal::Ink()) + 2.f;
			}
			if (!P.U6.bVisitedCity)
			{
				D.Text(TEXT("Сначала доберитесь до Города"), FVector2D(In.X, Y + 4.f), SmallS() + 0.5f, InkBad(), EFVAlign::Left, false, true);
			}
		}

		/** Section title; returns the Y below it. */
		float Section(FVDraw& D, const FString& Title, float X, float Y, float W)
		{
			const float S = BodyS();
			D.Text(Title, FVector2D(X, Y), S, FV::Col(90, 56, 24), EFVAlign::Left, false, true);
			D.Rect(FFVRect(X, Y + D.LineHeight(S, true) + 1.f, W, 1.f), FV::Col(196, 150, 100));
			return Y + D.LineHeight(S, true) + 5.f;
		}

		void DrawList(FVDraw& D, UFVSaveGame& P, const FFVRect& A)
		{
			const bool bP = FV::IsPortrait();
			D.Rect(A, ListBg());
			const FFVRect L = A.Inset(4.f);
			Scroll.Update(D, L, ContentH);
			const float W = L.W - (Scroll.CanScroll() ? 8.f : 0.f);
			const float X = L.X + 2.f;
			const float CW = W - 4.f;
			float Y = L.Y + 2.f - Scroll.Offset();
			const float TS = BodyS();
			const float SS = SmallS();
			const float RowH = bP ? 34.f : 22.f;
			const float BtnW = bP ? 110.f : 86.f;
			const float BtnH = bP ? 28.f : 17.f;
			D.PushClip(L);

			// ---- places
			Y = Section(D, TEXT("Где ставить сети"), X, Y, CW);
			{
				float CX = X;
				const float ChipH = bP ? 30.f : 18.f;
				for (const FFVLocationDef& Loc : FVDB::Locations())
				{
					const int32 Spots = SpotsAt(Loc.Id);
					if (Spots <= 0) { continue; }
					const bool bOpen = P.IsLocationUnlocked(Loc.Id);
					const bool bOn = P.U6.ArtelLocations.Contains(Loc.Id);
					const FString Label = FString::Printf(TEXT("%s%s ·%d"), bOn ? TEXT("+ ") : TEXT(""), *Loc.Name, Spots);
					const float ChipW = D.MeasureText(Label, TS, true).X + 16.f;
					if (CX + ChipW > X + CW && CX > X) { CX = X; Y += ChipH + 4.f; }
					const FFVRect R(CX, Y, ChipW, ChipH);
					if (CoinButton(D, R, Label, -1, bOn ? EFVButton::Primary : EFVButton::Normal, bOpen, true, TS) && bOpen)
					{
						Want(EAct::ToggleLoc, Loc.Id);
					}
					if (!bOpen && !bP && D.Hover(R)) { D.Tooltip(Loc.Name, TEXT("Место ещё не открыто")); }
					CX += ChipW + 4.f;
				}
				Y += ChipH + 4.f;
				Y += FullText(D, TEXT("Точка занята либо вашей сетью, либо артельной. В Городе и Пещере сетей нет."), FVector2D(X, Y), CW, SS, FVPal::InkSoft()) + 6.f;
			}

			// ---- artel nets on their spots
			Y = Section(D, TEXT("Сети артели"), X, Y, CW);
			if (P.U6.ArtelNets.Num() == 0)
			{
				Y += FullText(D, TEXT("Пока ни одной: отдайте сети и выберите места"), FVector2D(X, Y), CW, SS, FVPal::InkSoft()) + 6.f;
			}
			for (const FFVNetState& N : P.U6.ArtelNets)
			{
				const FFVRect R(X, Y, CW, RowH);
				RowBack(D, R, false, false, false);
				const float IS = RowH - 6.f;
				IconBox(D, FFVRect(R.X + 3.f, R.Y + 3.f, IS, IS));
				DrawItemIcon(D, N.NetItem, FFVRect(R.X + 4.f, R.Y + 4.f, IS - 2.f, IS - 2.f), 1.f);
				const float TX = R.X + IS + 9.f;
				const float Frac = FMath::Clamp(N.Durability / MaxDur(N.NetItem), 0.f, 1.f);
				const FString Top = FString::Printf(TEXT("%s · место №%d"), *LocName(N.Location), N.Spot + 1);
				const float BarW = bP ? 70.f : 80.f;
				if (bP)
				{
					D.Text(D.Ellipsize(Top, R.Right() - BarW - 12.f - TX, TS, true), FVector2D(TX, R.Y + 2.f), TS, FVPal::Ink(), EFVAlign::Left, false, true);
					const FString Info = N.bTorn ? FString(TEXT("Порвана — не ловит")) : FString::Printf(TEXT("%s · в сети: %d"), *FVDB::ItemName(N.NetItem), CatchUnits(N));
					D.Text(D.Ellipsize(Info, R.Right() - TX - 6.f, SS), FVector2D(TX, R.Y + 4.f + D.LineHeight(TS, true)), SS, N.bTorn ? InkBad() : FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
				else
				{
					const FString Line = N.bTorn ? FString::Printf(TEXT("%s — порвана"), *Top)
						: FString::Printf(TEXT("%s · %s · в сети: %d"), *Top, *FVDB::ItemName(N.NetItem), CatchUnits(N));
					D.Text(D.Ellipsize(Line, R.Right() - BarW - 50.f - TX, TS), FVector2D(TX, R.Y + (RowH - D.LineHeight(TS)) * 0.5f), TS, N.bTorn ? InkBad() : FVPal::Ink(), EFVAlign::Left, false, false);
				}
				// durability bar
				const FFVRect Bar(R.Right() - BarW - 36.f, R.Y + (bP ? 6.f : (RowH - 6.f) * 0.5f), BarW, 6.f);
				D.Rect(Bar, FV::Col(80, 60, 40));
				const FLinearColor BarC = N.bTorn ? FV::Col(190, 60, 50) : (Frac < 0.25f ? FV::Col(220, 120, 50) : FV::Col(90, 170, 80));
				D.Rect(FFVRect(Bar.X + 1.f, Bar.Y + 1.f, FMath::Max(0.f, (Bar.W - 2.f) * Frac), Bar.H - 2.f), BarC);
				D.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Frac * 100.f)), FVector2D(R.Right() - 4.f, Bar.Y - 2.f), SS, FVPal::InkSoft(), EFVAlign::Right, false, true);
				Y += RowH + 3.f;
			}
			Y += 4.f;

			// ---- stock
			Y = Section(D, TEXT("Запас артельщика"), X, Y, CW);
			if (P.U6.ArtelStock.Num() == 0)
			{
				Y += FullText(D, TEXT("Пусто — порванные сети заменить будет нечем"), FVector2D(X, Y), CW, SS, FVPal::InkSoft()) + 6.f;
			}
			for (int32 i = 0; i < P.U6.ArtelStock.Num(); ++i)
			{
				const FFVInvStack& S = P.U6.ArtelStock[i];
				const FFVRect R(X, Y, CW, RowH);
				RowBack(D, R, false, false, false);
				const float IS = RowH - 6.f;
				IconBox(D, FFVRect(R.X + 3.f, R.Y + 3.f, IS, IS));
				DrawItemIcon(D, S.ItemId, FFVRect(R.X + 4.f, R.Y + 4.f, IS - 2.f, IS - 2.f), 1.f);
				const float TX = R.X + IS + 9.f;
				D.Text(D.Ellipsize(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(S.ItemId), S.Count), R.Right() - BtnW - 10.f - TX, TS),
					FVector2D(TX, R.Y + (RowH - D.LineHeight(TS)) * 0.5f), TS, FVPal::Ink(), EFVAlign::Left, false, false);
				const FFVRect B(R.Right() - BtnW - 3.f, R.Y + (RowH - BtnH) * 0.5f, BtnW, BtnH);
				if (CoinButton(D, B, TEXT("Забрать 1"), -1, EFVButton::Normal, true, true, bP ? FV::Ui::Body() : 7.f)) { Want(EAct::Take, S.ItemId, i); }
				Y += RowH + 3.f;
			}
			Y += 4.f;

			// ---- the player's nets in the backpack
			Y = Section(D, TEXT("Ваши сети в рюкзаке"), X, Y, CW);
			bool bAny = false;
			for (int32 i = 0; i < P.Backpack.Num(); ++i)
			{
				const FFVInvStack& S = P.Backpack[i];
				const FFVItemDef* Def = FVDB::FindItem(S.ItemId);
				if (!Def || Def->Kind != EFVItemKind::Net || S.Count <= 0) { continue; }
				bAny = true;
				const FFVRect R(X, Y, CW, RowH);
				RowBack(D, R, false, false, false);
				const float IS = RowH - 6.f;
				IconBox(D, FFVRect(R.X + 3.f, R.Y + 3.f, IS, IS));
				DrawItemIcon(D, S.ItemId, FFVRect(R.X + 4.f, R.Y + 4.f, IS - 2.f, IS - 2.f), 1.f);
				const float TX = R.X + IS + 9.f;
				const float Btns = S.Count > 1 ? BtnW * 2.f + 4.f : BtnW;
				D.Text(D.Ellipsize(FString::Printf(TEXT("%s ×%d"), *Def->Name, S.Count), R.Right() - Btns - 10.f - TX, TS),
					FVector2D(TX, R.Y + (RowH - D.LineHeight(TS)) * 0.5f), TS, FVPal::Ink(), EFVAlign::Left, false, false);
				FFVRect B(R.Right() - BtnW - 3.f, R.Y + (RowH - BtnH) * 0.5f, BtnW, BtnH);
				if (CoinButton(D, B, TEXT("Отдать 1"), -1, EFVButton::Primary, true, true, bP ? FV::Ui::Body() : 7.f)) { Want(EAct::Give, S.ItemId, i); }
				if (S.Count > 1)
				{
					B.X -= BtnW + 4.f;
					if (CoinButton(D, B, TEXT("Отдать все"), -1, EFVButton::Normal, true, true, bP ? FV::Ui::Body() : 7.f)) { Want(EAct::GiveAll, S.ItemId, i); }
				}
				Y += RowH + 3.f;
			}
			if (!bAny)
			{
				Y += FullText(D, TEXT("Сетей нет — купите их в лавке Боцмана"), FVector2D(X, Y), CW, SS, FVPal::InkSoft()) + 6.f;
			}

			D.PopClip();
			ContentH = Y + Scroll.Offset() - L.Y + 6.f;
			Scroll.DrawBar(D, L);
		}

		void DrawMainActions(FVDraw& D, UFVSaveGame& P, const FFVRect& R)
		{
			const bool bP = FV::IsPortrait();
			const float TS = bP ? FV::Ui::Body() + 0.5f : 7.5f;
			if (!P.U6.bArtelHired)
			{
				const bool bCan = P.U6.bVisitedCity && P.CanAfford(HirePrice);
				const FFVRect B = bP ? R : FFVRect(R.Right() - 200.f, R.Y, 200.f, R.H);
				if (CoinButton(D, B, TEXT("Нанять артельщика"), HirePrice, EFVButton::Primary, P.U6.bVisitedCity, P.CanAfford(HirePrice), TS) && bCan)
				{
					Want(EAct::Hire);
				}
				return;
			}
			int32 Torn = 0;
			for (const FFVNetState& N : P.U6.ArtelNets) { if (N.bTorn) { ++Torn; } }
			const TArray<FFVIngredient> Cost = P.ArtelRepairCost();
			const float Gap = 4.f;
			const float HalfW = (R.W - Gap) * 0.5f;
			const FFVRect A(R.X, R.Y, HalfW, R.H);
			const FFVRect B(R.X + HalfW + Gap, R.Y, HalfW, R.H);
			const FString RepairL = Cost.Num() == 0 ? FString(TEXT("Чинить нечего")) : (bP ? FString(TEXT("Починить всё")) : FString::Printf(TEXT("Починить всё: %s"), *IngredientsText(Cost)));
			if (CoinButton(D, A, RepairL, -1, EFVButton::Primary, Cost.Num() > 0, true, TS) && Cost.Num() > 0) { Want(EAct::Repair); }
			const FString DiscardL = Torn > 0 ? FString::Printf(TEXT("Выбросить порванные (%d)"), Torn) : FString(TEXT("Порванных нет"));
			if (CoinButton(D, B, DiscardL, -1, EFVButton::Normal, Torn > 0, true, TS) && Torn > 0) { Want(EAct::Discard); }
		}

		void RunAct(UFVSaveGame& P)
		{
			const EAct A = Act;
			Act = EAct::None;
			UFVGame* G = Game;
			if (A == EAct::None || !G) { return; }
			TWeakObjectPtr<UFVGame> Weak(G);
			switch (A)
			{
			case EAct::Hire:
				G->Confirm(TEXT("Нанять артельщика?"),
					FString::Printf(TEXT("Никифор возьмёт %s один раз. Потом он сам ставит ваши сети и каждое утро продаёт улов, оставляя себе 20 %%."), *FVUI::Coins(HirePrice)),
					[Weak]()
					{
						UFVGame* GG = Weak.Get();
						UFVSaveGame* PP = GG ? GG->GetProfile() : nullptr;
						if (!PP) { return; }
						const EFVBuyResult R = PP->HireArtel();
						if (R != EFVBuyResult::Ok)
						{
							Fail(GG, R == EFVBuyResult::LevelTooLow ? FString(TEXT("Сначала доберитесь до Города")) : ResultText(R));
							return;
						}
						Success(GG, TEXT("Артельщик нанят! Отдайте ему сети и выберите места"), EFVSfx::Coin, EFVIcon::Artel);
						Refresh(GG);
					}, TEXT("Нанять"), TEXT("Отмена"));
				break;
			case EAct::ToggleLoc:
			{
				const bool bOn = !P.U6.ArtelLocations.Contains(ActId);
				P.SetArtelLocation(ActId, bOn);
				const int32 Placed = bOn ? P.ArtelDeploy() : 0;
				FVUI::Sfx(EFVSfx::Click);
				if (Placed > 0) { G->Toast(FString::Printf(TEXT("Артельщик поставил сетей: %d"), Placed), FVPal::Gold(), EFVIcon::Artel); }
				else if (!bOn) { G->Toast(TEXT("Целые сети с этого места вернутся в запас утром"), FVPal::White(), EFVIcon::Artel); }
				Refresh(G);
				break;
			}
			case EAct::Give:
			case EAct::GiveAll:
			{
				if (!P.Backpack.IsValidIndex(ActIndex) || P.Backpack[ActIndex].ItemId != ActId) { return; }
				const int32 N = A == EAct::GiveAll ? P.Backpack[ActIndex].Count : 1;
				const EFVBuyResult R = P.GiveNetsToArtel(ActIndex, N);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				const int32 Placed = P.ArtelDeploy();
				Success(G, Placed > 0 ? FString::Printf(TEXT("Сети у артельщика; поставлено: %d"), Placed) : FString(TEXT("Сети в запасе артельщика")), EFVSfx::Click, EFVIcon::Artel);
				Refresh(G);
				break;
			}
			case EAct::Take:
			{
				if (!P.U6.ArtelStock.IsValidIndex(ActIndex) || P.U6.ArtelStock[ActIndex].ItemId != ActId) { return; }
				const EFVBuyResult R = P.TakeNetsFromArtel(ActIndex, 1);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				Success(G, TEXT("Сеть в рюкзаке"), EFVSfx::Click, EFVIcon::Artel);
				Refresh(G);
				break;
			}
			case EAct::Repair:
			{
				const TArray<FFVIngredient> Cost = P.ArtelRepairCost();
				if (Cost.Num() == 0) { return; }
				G->Confirm(TEXT("Починить все сети?"),
					FString::Printf(TEXT("Никифору нужно: %s. Все повреждённые и порванные сети артели станут как новые."), *IngredientsText(Cost)),
					[Weak]()
					{
						UFVGame* GG = Weak.Get();
						UFVSaveGame* PP = GG ? GG->GetProfile() : nullptr;
						if (!PP) { return; }
						const EFVBuyResult R = PP->ArtelRepairAll();
						if (R != EFVBuyResult::Ok) { Fail(GG, R); return; }
						Success(GG, TEXT("Все сети артели починены"), EFVSfx::Knot, EFVIcon::Artel);
						Refresh(GG);
					}, TEXT("Починить"), TEXT("Отмена"));
				break;
			}
			case EAct::Discard:
				G->Confirm(TEXT("Выбросить порванные сети?"),
					TEXT("Порванные сети артели выбрасываются вместе с уловом в них. Артельщик поставит на их место сети из своего запаса."),
					[Weak]()
					{
						UFVGame* GG = Weak.Get();
						UFVSaveGame* PP = GG ? GG->GetProfile() : nullptr;
						if (!PP) { return; }
						const int32 N = PP->ArtelDiscardTorn();
						if (N <= 0) { return; }
						Success(GG, FString::Printf(TEXT("Выброшено сетей: %d"), N), EFVSfx::Click, EFVIcon::Artel);
						Refresh(GG);
					}, TEXT("Выбросить"), TEXT("Отмена"));
				break;
			default: break;
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeArtelScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenArtelImpl::FArtelScreen>(Game);
}
