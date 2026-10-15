// UPDATE 6 [MILL-SCREENS] — «Старая мельница» screen (Docs/UPDATE6_FOREST_CITY.md §18): EFVScreen::Mill.
// Context "mill" = the mill: a painted picture (the wheel turns while it works, flour sacks pile up), the repair stages
//   with their materials (a stage can be repaired right here while the mill is broken — the world opens a modal first),
//   grinding veg_rye x2 -> mat_flour x1 (UFVSaveGame::GrindRye / MaxGrindTimes, no mini-game, ×1 / ×5 / ×10 / всё),
//   the water saw status (+1 board per log at the yard sawmill) and the pond overview (dam / Omut, feeder decks, pen).
// Context "pen"  = the fish pen: water with the released species swimming, 3 slot cards with the time left, and the
//   aquarium list — «Выпустить» asks first (the fish never comes back), UFVSaveGame::ReleaseToPen.
// Landscape: picture + status on the left, the action column on the right. Portrait: one scrolling column + action bar.
// The footbridge and the feeder decks are modal windows of the world (FVGameU6.cpp), not this screen.

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVAudio.h"

namespace FVScreenMillImpl
{
	FV_HOME_UI_USING();

	float SmallS() { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }
	float BodyS() { return FV::IsPortrait() ? FV::Ui::Body() : 8.f; }
	float HeadS() { return FV::IsPortrait() ? 11.f : 9.f; }

	FString HoursText(float GameMinutes)
	{
		const int32 M = FMath::Max(0, FMath::CeilToInt(GameMinutes));
		if (M >= 1440) { return FString::Printf(TEXT("%d д %d ч"), M / 1440, (M % 1440) / 60); }
		if (M >= 60) { return FString::Printf(TEXT("%d ч %02d мин"), M / 60, M % 60); }
		return FString::Printf(TEXT("%d мин"), M);
	}

	class FMillScreen final : public FVScreen
	{
	public:
		explicit FMillScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			bPen = Game && Game->GetScreenContext() == FName(TEXT("pen"));
			Col.Reset();
			Side.Reset();
			Qty = 1;
			GrindFx = 0.f;
			ResetActions();
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			GrindFx = FMath::Max(0.f, GrindFx - Dt);
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			bool bClose = D.Window(Win, bPen ? TEXT("Рыборазводный садок") : TEXT("Старая мельница"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			ResetActions();
			if (P)
			{
				const float Top = Win.Y + (bP ? 16.f : 10.f);
				const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 8.f : Win.Bottom() - 10.f;
				const FFVRect Area(Win.X + 10.f, Top, Win.W - 20.f, FMath::Max(40.f, Bottom - Top));
				if (bP)
				{
					D.Panel(Area, EFVPanel::Parchment);
					DrawColumn(D, *P, Area.Inset(5.f), true);
				}
				else
				{
					const float LW = FMath::Min(250.f, FMath::FloorToFloat(Area.W * 0.46f));
					const FFVRect Left(Area.X, Area.Y, LW, Area.H);
					const FFVRect Right(Left.Right() + 6.f, Area.Y, Area.Right() - Left.Right() - 6.f, Area.H);
					D.Panel(Left, EFVPanel::Parchment);
					D.Panel(Right, EFVPanel::Parchment);
					const FFVRect Pic(Left.X + 6.f, Left.Y + 6.f, Left.W - 12.f, FMath::Min(110.f, Left.H * 0.45f));
					if (bPen) { DrawPenPicture(D, *P, Pic); } else { DrawMillPicture(D, *P, Pic); }
					const FFVRect Status(Left.X + 5.f, Pic.Bottom() + 6.f, Left.W - 10.f, Left.Bottom() - Pic.Bottom() - 11.f);
					DrawSideStatus(D, *P, Status);
					DrawColumn(D, *P, Right.Inset(5.f), false);
				}
			}
			if (bP && D.ActionBar(Win)) { bClose = true; }
			if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); return; }
			if (P) { RunActions(*P); }
		}

	private:
		FScrollColumn Col;
		FScrollColumn Side;
		bool bPen = false;
		int32 Qty = 1;
		float GrindFx = 0.f;          // seconds of the flour dust after a grind
		// deferred actions (run after drawing)
		int32 GrindTimes = 0;
		bool bRepair = false;
		int32 ReleaseIndex = INDEX_NONE;

		void ResetActions() { GrindTimes = 0; bRepair = false; ReleaseIndex = INDEX_NONE; }

		// ---------------------------------------------------------------------------------------- pictures

		void DrawMillPicture(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			const float T = float(D.Time);
			const bool bWork = P.IsMillWorking();
			D.PushClip(R);
			D.GradientV(R, FV::Col(236, 206, 150), FV::Col(246, 232, 200), 4);   // autumn morning
			// fog bands
			for (int32 i = 0; i < 3; ++i)
			{
				const float FX = R.X + FMath::Fmod(float(i) * 73.f + T * 4.f, R.W + 60.f) - 60.f;
				D.Rect(FFVRect(FMath::RoundToFloat(FX), R.Y + R.H * (0.30f + 0.08f * float(i)), 60.f, 3.f), FV::Col(255, 250, 240, 110));
			}
			// the far copse
			for (int32 i = 0; i < 9; ++i)
			{
				const float CX = R.X + 6.f + float(i) * (R.W - 12.f) / 8.f;
				const FLinearColor Leaf = (i % 3 == 0) ? FV::Col(196, 110, 46) : ((i % 3 == 1) ? FV::Col(214, 158, 60) : FV::Col(150, 120, 60));
				D.Circle(FVector2D(CX, R.Y + R.H * 0.50f), 9.f + float(i % 2) * 3.f, Leaf);
			}
			const FFVRect Pond(R.X, R.Y + R.H * 0.64f, R.W, R.H * 0.36f);
			D.GradientV(Pond, FV::Col(76, 130, 150), FV::Col(44, 88, 110), 4);
			for (int32 i = 0; i < 5; ++i)
			{
				const float X = R.X + FMath::Fmod(float(i) * 53.f + T * (bWork ? 14.f : 5.f), R.W);
				D.Rect(FFVRect(FMath::RoundToFloat(X), FMath::RoundToFloat(Pond.Y + 4.f + float(i % 3) * 7.f), 7.f, 1.f), FV::Col(200, 230, 240, 140));
			}
			if (UFVSprites* Spr = UFVSprites::Get())
			{
				const int32 Frame = bWork ? (int32(T * 6.f) & 3) : 0;
				const FFVSprite& S = Spr->MillObjSprite(FVMill::ObjMillId(), FMath::Clamp(P.GetMillStage(), 0, 3), Frame);
				FVUI::SpriteFit(D, S, FFVRect(R.X + R.W * 0.12f, R.Y + 4.f, R.W * 0.62f, R.H - 8.f), FLinearColor::White, 3.f);
			}
			// flour sacks by the door
			const int32 Flour = P.CountItem(FVMill::FlourId());
			const int32 Sacks = FMath::Clamp((Flour + 1) / 2, 0, 6);
			for (int32 s = 0; s < Sacks; ++s)
			{
				const float SX = R.Right() - 16.f - float(s % 3) * 11.f;
				const float SY = Pond.Y - 12.f - float(s / 3) * 8.f;
				D.Rect(FFVRect(SX, SY, 10.f, 10.f), FV::Col(232, 220, 190));
				D.Rect(FFVRect(SX + 2.f, SY - 2.f, 6.f, 2.f), FV::Col(200, 180, 140));
				D.Frame(FFVRect(SX, SY, 10.f, 10.f), FV::Col(150, 120, 80), 1.f);
			}
			// flour dust after a grind
			if (GrindFx > 0.f)
			{
				const float A = FMath::Clamp(GrindFx / 1.2f, 0.f, 1.f);
				for (int32 i = 0; i < 10; ++i)
				{
					const float Ph = FMath::Fmod(T * 0.9f + float(i) * 0.37f, 1.f);
					const FVector2D C(R.X + R.W * 0.45f + FMath::Sin(float(i) * 2.1f + T) * 14.f, R.Y + R.H * 0.62f - Ph * 30.f);
					D.Circle(C, 1.5f + Ph * 1.5f, FV::Col(255, 250, 235, int32(200.f * A * (1.f - Ph))));
				}
			}
			D.PopClip();
			if (!bWork)
			{
				D.TextOutlined(FString::Printf(TEXT("Ремонт %d/%d"), P.GetMillStage(), FVMill::MillStages().Num()),
					FVector2D(R.X + 6.f, R.Y + 4.f), SmallS(), FVPal::White(), EFVAlign::Left);
			}
			D.Frame(R, FV::Col(110, 70, 40), 1.f);
		}

		void DrawPenPicture(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			const float T = float(D.Time);
			D.PushClip(R);
			D.GradientV(R, FV::Col(84, 150, 160), FV::Col(34, 84, 100), 5);
			// the wicker fence of the pen
			for (float X = R.X + 3.f; X < R.Right(); X += 9.f)
			{
				D.Rect(FFVRect(FMath::RoundToFloat(X), R.Y, 2.f, R.H), FV::Col(120, 86, 50, 150));
			}
			D.Rect(FFVRect(R.X, R.Y + 6.f, R.W, 2.f), FV::Col(140, 100, 60, 190));
			D.Rect(FFVRect(R.X, R.Bottom() - 8.f, R.W, 2.f), FV::Col(140, 100, 60, 190));
			// light ripples
			for (int32 i = 0; i < 6; ++i)
			{
				const float X = R.X + FMath::Fmod(float(i) * 41.f + T * 7.f, R.W);
				D.Rect(FFVRect(FMath::RoundToFloat(X), FMath::RoundToFloat(R.Y + 12.f + float(i % 3) * 9.f), 8.f, 1.f), FV::Col(210, 240, 245, 120));
			}
			const TArray<FName> Active = P.ActivePenFish(Game ? Game->GetAbsMinutes() : 0.0);
			UFVSprites* Spr = UFVSprites::Get();
			for (int32 i = 0; i < Active.Num(); ++i)
			{
				const FFVFishDef* F = FVDB::FindFish(Active[i]);
				if (!F || !Spr) { continue; }
				const float Speed = 0.18f + 0.05f * float(i);
				const float Ph = FMath::Fmod(T * Speed + float(i) * 0.33f, 2.f);
				const bool bBack = Ph > 1.f;
				const float U = bBack ? 2.f - Ph : Ph;
				const float X = R.X + 14.f + U * (R.W - 28.f);
				const float Y = R.Y + R.H * (0.28f + 0.22f * float(i)) + FMath::Sin(T * 1.7f + float(i)) * 2.f;
				const FFVSprite& S = Spr->Fish(*F);
				const float Sc = FMath::Clamp(26.f / FMath::Max(1.f, float(S.W)), 0.5f, 2.f);
				D.SpriteCentered(S, FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), Sc, FLinearColor::White, bBack);
			}
			if (Active.Num() == 0)
			{
				D.TextOutlined(TEXT("Садок пуст"), FVector2D(R.Center().X, R.Center().Y - 5.f), BodyS(), FVPal::White(), EFVAlign::Center);
			}
			D.PopClip();
			D.Frame(R, FV::Col(110, 70, 40), 1.f);
		}

		// ---------------------------------------------------------------------------------------- status blocks

		/** Repair stages as a checklist. Returns the new Y. */
		float DrawStagesList(FVDraw& D, const UFVSaveGame& P, float X, float Y, float W)
		{
			const TArray<FFVBoatStage>& Stages = FVMill::MillStages();
			const int32 Done = FMath::Clamp(P.GetMillStage(), 0, Stages.Num());
			for (int32 i = 0; i < Stages.Num(); ++i)
			{
				const bool bDone = i < Done;
				const FString L = FString::Printf(TEXT("%d. %s"), i + 1, *Stages[i].Name);
				Y += CondRow(D, X, Y, W, i == Done ? L + TEXT(" — сейчас") : L, bDone, SmallS() + 0.5f, bDone ? EFVIcon::Check : EFVIcon::Hammer);
			}
			return Y;
		}

		/** Pond overview: the dam / Omut, the feeder decks, the pen, the water saw. Returns the new Y. */
		float DrawPondStatus(FVDraw& D, const UFVSaveGame& P, float X, float Y, float W)
		{
			const double Now = Game ? Game->GetAbsMinutes() : 0.0;
			const float S = SmallS() + 0.5f;
			const int32 Feeds = FMath::Clamp(P.U6.Mill.BeaverFeeds, 0, FVMill::BeaverFeedsNeeded);
			const bool bOmut = P.IsOmutOpen();
			FString Dam = bOmut ? FString(TEXT("Плотина готова — Омут открыт"))
				: FString::Printf(TEXT("Плотина бобров: %d/%d"), Feeds, FVMill::BeaverFeedsNeeded);
			if (!bOmut)
			{
				const float Cd = P.BeaverCooldownLeft();
				Dam += Cd > 0.f ? FString::Printf(TEXT(" · ещё %d мин"), FMath::CeilToInt(Cd)) : FString(TEXT(" · бобры ждут веток"));
			}
			Y += CondRow(D, X, Y, W, Dam, bOmut, S, EFVIcon::Net);
			for (int32 d = 0; d < FVMill::FeedDecks; ++d)
			{
				const bool bOn = P.IsDeckBaited(d, Now);
				const FString L = bOn ? FString::Printf(TEXT("Мосток %d: прикормлен, %s"), d + 1, *HoursText(P.DeckMinutesLeft(d, Now)))
					: FString::Printf(TEXT("Мосток %d: без прикормки"), d + 1);
				Y += CondRow(D, X, Y, W, L, bOn, S, EFVIcon::Bait);
			}
			const int32 Pen = P.ActivePenFish(Now).Num();
			Y += CondRow(D, X, Y, W, FString::Printf(TEXT("Садок: %d из %d видов"), Pen, FVMill::PenSlots), Pen > 0, S, EFVIcon::Aquarium);
			return Y;
		}

		/** Landscape left panel under the picture. */
		void DrawSideStatus(FVDraw& D, const UFVSaveGame& P, const FFVRect& Area)
		{
			const float W = Area.W - Side.BarRoom() - 4.f;
			const float X = Area.X + 2.f;
			float Y = Side.Begin(D, Area, 2.f);
			if (bPen)
			{
				Y = DrawPenSlots(D, P, X, Y, W);
			}
			else
			{
				Y = FVUI::SectionHeader(D, TEXT("Ремонт"), FVector2D(X, Y), W, HeadS()) + 1.f;
				Y = DrawStagesList(D, P, X, Y, W) + 6.f;
				Y = FVUI::SectionHeader(D, TEXT("Запруда"), FVector2D(X, Y), W, HeadS()) + 1.f;
				Y = DrawPondStatus(D, P, X, Y, W) + 4.f;
			}
			Side.End(D, Y, 4.f);
		}

		/** The pen: one card per slot with the species and the time left. Returns the new Y. */
		float DrawPenSlots(FVDraw& D, const UFVSaveGame& P, float X, float Y, float W)
		{
			const double Now = Game ? Game->GetAbsMinutes() : 0.0;
			const TArray<FName> Active = P.ActivePenFish(Now);
			Y = FVUI::SectionHeader(D, FString::Printf(TEXT("В садке: %d из %d"), Active.Num(), FVMill::PenSlots), FVector2D(X, Y), W, HeadS()) + 2.f;
			const float CardH = FV::IsPortrait() ? 40.f : 26.f;
			for (int32 i = 0; i < FVMill::PenSlots; ++i)
			{
				const FFVRect Card(X, Y, W, CardH);
				D.Rect(Card, BoxFill());
				D.Frame(Card, BoxEdge(), 1.f);
				const float IconS = CardH - 6.f;
				if (Active.IsValidIndex(i))
				{
					const FName Fish = Active[i];
					DrawItemIcon(D, Fish, FFVRect(Card.X + 3.f, Card.Y + 3.f, IconS, IconS), 2.f);
					const float TX = Card.X + IconS + 8.f;
					const float TW = Card.Right() - TX - 4.f;
					const float Left = P.PenMinutesLeft(Fish, Now);
					D.Text(D.Ellipsize(FVDB::ItemName(Fish), TW, SmallS() + 0.5f), FVector2D(TX, Card.Y + 2.f), SmallS() + 0.5f, FVPal::Ink(), EFVAlign::Left, false, true);
					const float BarY = Card.Bottom() - (FV::IsPortrait() ? 12.f : 8.f);
					D.Bar(FFVRect(TX, BarY, TW * 0.55f, FV::IsPortrait() ? 6.f : 4.f), FMath::Clamp(Left / FVMill::PenGameMinutes, 0.f, 1.f), FV::Col(90, 170, 200));
					D.Text(HoursText(Left), FVector2D(Card.Right() - 4.f, BarY - (FV::IsPortrait() ? 5.f : 3.f)), SmallS(), FVPal::InkSoft(), EFVAlign::Right, false, false);
				}
				else
				{
					D.Text(TEXT("Свободное место"), FVector2D(Card.X + 8.f, Card.Y + FMath::RoundToFloat((CardH - D.LineHeight(SmallS(), false)) * 0.5f)),
						SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
				Y += CardH + 4.f;
			}
			return Y;
		}

		// ---------------------------------------------------------------------------------------- action column

		void DrawColumn(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, bool bWithPicture)
		{
			const float W = Area.W - Col.BarRoom() - 4.f;
			const float X = Area.X + 2.f;
			float Y = Col.Begin(D, Area, 4.f);
			if (bWithPicture)
			{
				const FFVRect Pic(X, Y, W, 110.f);
				if (bPen) { DrawPenPicture(D, P, Pic); } else { DrawMillPicture(D, P, Pic); }
				Y += Pic.H + 8.f;
			}
			if (bPen) { Y = DrawPenColumn(D, P, X, Y, W, bWithPicture); }
			else { Y = DrawMillColumn(D, P, X, Y, W, bWithPicture); }
			Col.End(D, Y, 6.f);
		}

		float DrawMillColumn(FVDraw& D, UFVSaveGame& P, float X, float Y, float W, bool bPortraitColumn)
		{
			const float BH = FV::Ui::ButtonH();
			if (!P.IsMillWorking())
			{
				const TArray<FFVBoatStage>& Stages = FVMill::MillStages();
				const int32 Done = FMath::Clamp(P.GetMillStage(), 0, Stages.Num());
				if (bPortraitColumn) { Y = DrawStagesList(D, P, X, Y, W) + 6.f; }
				if (!Stages.IsValidIndex(Done)) { return Y; }
				const FFVBoatStage& St = Stages[Done];
				Y = FVUI::SectionHeader(D, FString::Printf(TEXT("Этап %d из %d: %s"), Done + 1, Stages.Num(), *St.Name), FVector2D(X, Y), W, HeadS()) + 2.f;
				Y += TextWrappedLimited(D, TEXT("Мельница стоит без дела. Почините её — и она будет молоть рожь в муку, а водяная пила поможет лесопилке во дворе."),
					FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 4) + 4.f;
				for (const FFVIngredient& I : St.Cost)
				{
					Y += ReqRow(D, X, Y, W, I.ItemId, P.CountIngredient(I.ItemId), I.Count, SmallS() + 0.5f);
				}
				if (St.Coins > 0) { Y += CoinRow(D, X, Y, W, P.Money, St.Coins, SmallS() + 0.5f); }
				Y += CondRow(D, X, Y, W, FString::Printf(TEXT("Уровень рыбака %d"), St.MinLevel), P.GetLevel() >= St.MinLevel, SmallS() + 0.5f);
				Y += 6.f;
				const bool bCan = P.CanRepairMillStage() == EFVBuyResult::Ok;
				if (D.Button(FFVRect(X, Y, W, BH), FString::Printf(TEXT("Чинить: %s"), *St.Name), EFVButton::Primary, bCan, BodyS())) { bRepair = true; }
				return Y + BH + 8.f;
			}

			// ---- grinding
			const int32 Rye = P.CountItem(FVMill::RyeId());
			const int32 Flour = P.CountItem(FVMill::FlourId());
			const int32 Max = P.MaxGrindTimes();
			Y = FVUI::SectionHeader(D, TEXT("Помол ржи"), FVector2D(X, Y), W, HeadS()) + 2.f;
			Y += TextWrappedLimited(D, FString::Printf(TEXT("Жернова мелют без спешки: %d снопа ржи — мешочек ржаной муки. Рожь растёт в огороде, семена — у Мельника Прокопа."),
				FVMill::RyePerFlour), FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 4) + 4.f;
			const int32 N = FMath::Clamp(Qty, 1, FMath::Max(1, Max));
			Y += ReqRow(D, X, Y, W, FVMill::RyeId(), Rye, FVMill::RyePerFlour * N, SmallS() + 0.5f);
			{
				const float IconS = FV::IsPortrait() ? 16.f : 12.f;
				const float RowH = ReqRowH(D, SmallS() + 0.5f);
				DrawItemIcon(D, FVMill::FlourId(), FFVRect(X, FMath::RoundToFloat(Y + (RowH - IconS) * 0.5f), IconS, IconS), 1.f);
				const float TY = FMath::RoundToFloat(Y + (RowH - D.LineHeight(SmallS() + 0.5f, false)) * 0.5f);
				D.Text(TEXT("Ржаная мука в рюкзаке"), FVector2D(X + IconS + 4.f, TY), SmallS() + 0.5f, FVPal::Ink(), EFVAlign::Left, false, false);
				D.Text(FString::Printf(TEXT("%d"), Flour), FVector2D(X + W, TY), SmallS() + 0.5f, FVPal::Ink(), EFVAlign::Right, false, true);
				Y += RowH + 4.f;
			}
			const float ChipH = FV::IsPortrait() ? 30.f : 16.f;
			const float ChipW = FMath::Min(FV::IsPortrait() ? 56.f : 34.f, FMath::FloorToFloat((W - 8.f) / 3.f));
			Qty = QtyChips(D, FVector2D(X, Y), { 1, 5, 10 }, Qty, ChipW, ChipH, SmallS() + 0.5f, Max >= 1);
			Y += ChipH + 6.f;
			const float Half = FMath::FloorToFloat((W - 6.f) * 0.5f);
			if (D.Button(FFVRect(X, Y, Half, BH), FString::Printf(TEXT("Смолоть ×%d"), N), EFVButton::Primary, Max >= N && Max >= 1, BodyS())) { GrindTimes = N; }
			if (D.Button(FFVRect(X + Half + 6.f, Y, W - Half - 6.f, BH), FString::Printf(TEXT("Всё (%d)"), Max), EFVButton::Normal, Max >= 1, BodyS())) { GrindTimes = Max; }
			Y += BH + 4.f;
			if (Max < 1)
			{
				Y += TextWrappedLimited(D, FString::Printf(TEXT("Нужно хотя бы %d снопа ржи."), FVMill::RyePerFlour), FVector2D(X, Y), W, SmallS(), InkBad(), 2) + 2.f;
			}
			Y += 6.f;

			// ---- the water saw
			Y = FVUI::SectionHeader(D, TEXT("Водяная пила"), FVector2D(X, Y), W, HeadS()) + 2.f;
			const bool bSawmill = P.GetYardLevel(EFVYardBuilding::Sawmill) > 0;
			Y += CondRow(D, X, Y, W, FString::Printf(TEXT("+%d доска с каждого бревна"), P.MillSawBonus()), P.MillSawBonus() > 0, SmallS() + 0.5f, EFVIcon::Saw);
			Y += TextWrappedLimited(D, bSawmill ? FString(TEXT("Пока колесо крутится, лесопилка во дворе выпиливает из каждого бревна на доску больше."))
				: FString(TEXT("Работает вместе с лесопилкой во дворе — её строит Прораб Гаврила в лесу.")),
				FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 3) + 6.f;

			if (bPortraitColumn)
			{
				Y = FVUI::SectionHeader(D, TEXT("Запруда"), FVector2D(X, Y), W, HeadS()) + 1.f;
				Y = DrawPondStatus(D, P, X, Y, W) + 6.f;
				Y = FVUI::SectionHeader(D, TEXT("Ремонт"), FVector2D(X, Y), W, HeadS()) + 1.f;
				Y = DrawStagesList(D, P, X, Y, W) + 4.f;
			}
			return Y;
		}

		float DrawPenColumn(FVDraw& D, UFVSaveGame& P, float X, float Y, float W, bool bPortraitColumn)
		{
			const double Now = Game ? Game->GetAbsMinutes() : 0.0;
			const TArray<FName> Active = P.ActivePenFish(Now);
			if (bPortraitColumn) { Y = DrawPenSlots(D, P, X, Y, W) + 4.f; }
			Y = FVUI::SectionHeader(D, TEXT("Выпустить из аквариума"), FVector2D(X, Y), W, HeadS()) + 2.f;
			Y += TextWrappedLimited(D, FString::Printf(TEXT("Выпущенный вид %d игровых дня клюёт на мельнице в %.1f раза чаще и чуть крупнее. Тот же вид — продлевает срок. Рыба в аквариум не вернётся."),
				FMath::RoundToInt(FVMill::PenGameMinutes / 1440.f), FVMill::PenWeightMult), FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 5) + 6.f;
			const TArray<FFVInvStack>& Aq = P.GetContainer(EFVContainer::Aquarium);
			const float RowH = FV::IsPortrait() ? 44.f : 26.f;
			const float BtnW = FV::IsPortrait() ? 118.f : 78.f;
			int32 Shown = 0;
			for (int32 i = 0; i < Aq.Num(); ++i)
			{
				const FFVInvStack& S = Aq[i];
				if (S.Count <= 0 || !FVDB::FindFish(S.ItemId)) { continue; }
				++Shown;
				const FFVRect Row(X, Y, W, RowH);
				D.Rect(Row, RowFill());
				const float IconS = RowH - 6.f;
				DrawItemIcon(D, S.ItemId, FFVRect(Row.X + 3.f, Row.Y + 3.f, IconS, IconS), 2.f);
				const float TX = Row.X + IconS + 8.f;
				const float TW = Row.Right() - BtnW - 6.f - TX;
				const bool bIn = Active.Contains(S.ItemId);
				D.Text(D.Ellipsize(FVDB::ItemName(S.ItemId), TW, SmallS() + 0.5f), FVector2D(TX, Row.Y + 3.f), SmallS() + 0.5f, FVPal::Ink(), EFVAlign::Left, false, true);
				const FString Sub = bIn ? FString::Printf(TEXT("×%d · уже в садке, %s"), S.Count, *HoursText(P.PenMinutesLeft(S.ItemId, Now)))
					: FString::Printf(TEXT("×%d в аквариуме"), S.Count);
				D.Text(D.Ellipsize(Sub, TW, SmallS(), false), FVector2D(TX, Row.Y + 3.f + D.LineHeight(SmallS() + 0.5f)), SmallS(), bIn ? InkGood() : FVPal::InkSoft(), EFVAlign::Left, false, false);
				const bool bRoom = bIn || Active.Num() < FVMill::PenSlots;
				const float BH = RowH - 6.f;
				if (D.Button(FFVRect(Row.Right() - BtnW - 3.f, Row.Y + 3.f, BtnW, BH), bIn ? TEXT("Продлить") : TEXT("Выпустить"),
					bRoom ? EFVButton::Primary : EFVButton::Normal, bRoom, SmallS() + 0.5f))
				{
					ReleaseIndex = i;
				}
				Y += RowH + 4.f;
			}
			if (Shown == 0)
			{
				Y += TextWrappedLimited(D, TEXT("В аквариуме нет живой рыбы. Поймайте рыбу и отнесите её в аквариум дома — потом её можно выпустить здесь."),
					FVector2D(X, Y), W, SmallS() + 0.5f, FVPal::InkSoft(), 4) + 4.f;
			}
			else if (Active.Num() >= FVMill::PenSlots)
			{
				Y += TextWrappedLimited(D, TEXT("В садке уже три вида: новый можно выпустить, когда у кого-то закончится срок."),
					FVector2D(X, Y), W, SmallS(), InkWarn(), 3) + 4.f;
			}
			return Y;
		}

		// ---------------------------------------------------------------------------------------- actions

		void RunActions(UFVSaveGame& P)
		{
			UFVGame* G = Game;
			if (!G) { return; }
			if (bRepair)
			{
				bRepair = false;
				const int32 Before = P.GetMillStage();
				const EFVBuyResult R = P.RepairMillStage();
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				const TArray<FFVBoatStage>& Stages = FVMill::MillStages();
				const FString Name = Stages.IsValidIndex(Before) ? Stages[Before].Name : FString(TEXT("ремонт"));
				Success(G, P.IsMillWorking() ? FString(TEXT("Мельница заработала! Колесо крутится")) : FString::Printf(TEXT("Готово: %s"), *Name),
					EFVSfx::Hammer, EFVIcon::Hammer);
				Refresh(G);
				return;
			}
			if (GrindTimes > 0)
			{
				const int32 Times = GrindTimes;
				GrindTimes = 0;
				const EFVBuyResult R = P.GrindRye(Times);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				GrindFx = 1.6f;
				if (UFVAudio* A = G->GetAudio()) { A->Play(EFVSfx::Hammer, 0.6f); }
				G->Toast(FString::Printf(TEXT("Ржаная мука +%d"), Times), ToastGood(), EFVIcon::Hand);
				G->NotifyProfileChanged();
				return;
			}
			if (ReleaseIndex != INDEX_NONE)
			{
				const int32 Index = ReleaseIndex;
				ReleaseIndex = INDEX_NONE;
				const TArray<FFVInvStack>& Aq = P.GetContainer(EFVContainer::Aquarium);
				if (!Aq.IsValidIndex(Index)) { return; }
				const FName Fish = Aq[Index].ItemId;
				TWeakObjectPtr<UFVGame> WeakGame(G);
				G->Confirm(TEXT("Выпустить в садок?"),
					FString::Printf(TEXT("%s уйдёт из аквариума насовсем. %d игровых дня этот вид будет клевать на мельнице чаще и крупнее."),
						*FVDB::ItemName(Fish), FMath::RoundToInt(FVMill::PenGameMinutes / 1440.f)),
					[WeakGame, Index, Fish]()
					{
						UFVGame* GG = WeakGame.Get();
						UFVSaveGame* Pr = GG ? GG->GetProfile() : nullptr;
						if (!Pr) { return; }
						const TArray<FFVInvStack>& A = Pr->GetContainer(EFVContainer::Aquarium);
						int32 Idx = A.IsValidIndex(Index) && A[Index].ItemId == Fish ? Index : INDEX_NONE;
						for (int32 k = 0; Idx == INDEX_NONE && k < A.Num(); ++k) { if (A[k].ItemId == Fish && A[k].Count > 0) { Idx = k; } }
						if (Idx == INDEX_NONE) { return; }
						FName Out;
						const EFVBuyResult R = Pr->ReleaseToPen(Idx, GG->GetAbsMinutes(), Out);
						if (R == EFVBuyResult::Ok)
						{
							Success(GG, FString::Printf(TEXT("%s в садке: 3 дня клюёт чаще"), *FVDB::ItemName(Out)), EFVSfx::Splash, EFVIcon::Aquarium);
							Refresh(GG);
						}
						else
						{
							Fail(GG, R == EFVBuyResult::NoRoom ? FString(TEXT("В садке уже три вида")) : FString(TEXT("Эту рыбу не выпустить")));
						}
					}, TEXT("Выпустить"), TEXT("Отмена"));
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeMillScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenMillImpl::FMillScreen>(Game);
}
