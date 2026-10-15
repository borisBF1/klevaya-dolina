// UPDATE 6 [SCREEN-YARD] — the boat: repair stages and the voyage scene (Docs/UPDATE6_FOREST_CITY.md §7). Owner: SCREEN-YARD.
// Boat (no context): the old boat of the ocean pier — three repair stages (FVDBU6::BoatStages, RepairBoatStage), the
// boat mends before the player's eyes; when repaired: «Отплыть в Город». Voyage (context "city" / "ocean" = the
// destination): a short full-screen sailing scene (VoyageSprite layers, BoatHorn), then TravelToEntry(dest, "pier").

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"

namespace FVScreenBoatImpl
{
	FV_HOME_UI_USING();

	float SmallS() { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }
	float BodyS() { return FV::IsPortrait() ? FV::Ui::Body() : 8.f; }

	// ============================================================================================
	// Boat repair
	// ============================================================================================
	class FBoatScreen final : public FVScreen
	{
	public:
		explicit FBoatScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override { Col.Reset(); }

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			bool bClose = D.Window(Win, TEXT("Старая лодка"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			bRepair = false;
			bSail = false;
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
					const FFVRect Left(Area.X, Area.Y, 250.f, Area.H);
					const FFVRect Right(Left.Right() + 6.f, Area.Y, Area.Right() - Left.Right() - 6.f, Area.H);
					D.Panel(Left, EFVPanel::Parchment);
					D.Panel(Right, EFVPanel::Parchment);
					DrawBoatPicture(D, *P, FFVRect(Left.X + 6.f, Left.Y + 6.f, Left.W - 12.f, 110.f));
					DrawStagesList(D, *P, Left.X + 8.f, Left.Y + 124.f, Left.W - 16.f);
					DrawColumn(D, *P, Right.Inset(5.f), false);
				}
			}
			if (bP && D.ActionBar(Win)) { bClose = true; }
			if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); return; }
			if (P) { RunActions(*P); }
		}

	private:
		FScrollColumn Col;
		bool bRepair = false;
		bool bSail = false;

		static int32 StageNum(const UFVSaveGame& P) { return FMath::Clamp(P.U6.BoatStage, 0, FVDBU6::BoatStages().Num()); }

		void DrawBoatPicture(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			D.GradientV(R, FV::Col(150, 206, 238), FV::Col(206, 234, 248), 4);
			const FFVRect Sea(R.X, R.Y + R.H * 0.62f, R.W, R.H * 0.38f);
			D.GradientV(Sea, FV::Col(64, 132, 186), FV::Col(34, 88, 146), 4);
			const float T = float(D.Time);
			for (int32 i = 0; i < 6; ++i)
			{
				const float X = R.X + FMath::Fmod(float(i) * 47.f + T * 9.f, R.W);
				D.Rect(FFVRect(FMath::RoundToFloat(X), FMath::RoundToFloat(Sea.Y + 4.f + float(i % 3) * 8.f), 8.f, 1.f), FV::Col(200, 230, 250, 150));
			}
			if (UFVSprites* Spr = UFVSprites::Get())
			{
				const FFVSprite& S = Spr->BoatSprite(StageNum(P));
				const float Bob = P.IsBoatRepaired() ? FMath::RoundToFloat(FMath::Sin(T * 1.8f) * 1.5f) : 0.f;
				FVUI::SpriteFit(D, S, FFVRect(R.X + 8.f, R.Y + 8.f + Bob, R.W - 16.f, R.H - 12.f), FLinearColor::White, 3.f);
			}
			D.Frame(R, FV::Col(110, 70, 40), 1.f);
		}

		/** Stage list: done / current / waiting. Returns the new Y. */
		float DrawStagesList(FVDraw& D, const UFVSaveGame& P, float X, float Y, float W)
		{
			const TArray<FFVBoatStage>& Stages = FVDBU6::BoatStages();
			const int32 Done = StageNum(P);
			for (int32 i = 0; i < Stages.Num(); ++i)
			{
				const bool bDone = i < Done;
				const bool bCur = i == Done;
				const FString L = FString::Printf(TEXT("%d. %s"), i + 1, *Stages[i].Name);
				Y += CondRow(D, X, Y, W, bCur ? L + TEXT(" — сейчас") : L, bDone, BodyS(), bDone ? EFVIcon::Check : EFVIcon::Hammer);
			}
			return Y + 4.f;
		}

		void DrawColumn(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, bool bWithPicture)
		{
			const bool bP = FV::IsPortrait();
			const float W = Area.W - Col.BarRoom() - 4.f;
			const float X = Area.X + 2.f;
			float Y = Col.Begin(D, Area, 4.f);
			const TArray<FFVBoatStage>& Stages = FVDBU6::BoatStages();
			const int32 Done = StageNum(P);
			if (bWithPicture)
			{
				DrawBoatPicture(D, P, FFVRect(X, Y, W, 96.f));
				Y += 104.f;
				Y = DrawStagesList(D, P, X, Y, W) + 4.f;
			}
			if (P.IsBoatRepaired() || Done >= Stages.Num())
			{
				Y = FVUI::SectionHeader(D, TEXT("Лодка готова"), FVector2D(X, Y), W, bP ? 11.f : 9.f) + 2.f;
				Y += TextWrappedLimited(D, TEXT("Лодка на плаву. На ней можно добраться до Города за морем — и вернуться обратно к этому причалу."),
					FVector2D(X, Y), W, SmallS(), FVPal::Ink(), 4) + 8.f;
				const float BH = FV::Ui::ButtonH();
				if (D.Button(FFVRect(X, Y, W, BH), TEXT("Отплыть в Город"), EFVButton::Primary, true, BodyS())) { bSail = true; }
				Y += BH + 8.f;
				Col.End(D, Y, 6.f);
				return;
			}
			const FFVBoatStage& St = Stages[Done];
			Y = FVUI::SectionHeader(D, FString::Printf(TEXT("Этап %d из %d: %s"), Done + 1, Stages.Num(), *St.Name), FVector2D(X, Y), W, bP ? 11.f : 9.f) + 2.f;
			if (Done == 0)
			{
				Y += TextWrappedLimited(D, TEXT("Каркас лодки делают на мини-лесопилке во дворе. Гвозди куют в кузне."),
					FVector2D(X, Y), W, SmallS(), FVPal::InkSoft(), 3) + 4.f;
			}
			for (const FFVIngredient& I : St.Cost)
			{
				Y += ReqRow(D, X, Y, W, I.ItemId, P.CountIngredient(I.ItemId), I.Count, SmallS() + 0.5f);
			}
			if (St.Coins > 0) { Y += CoinRow(D, X, Y, W, P.Money, St.Coins, SmallS() + 0.5f); }
			Y += CondRow(D, X, Y, W, FString::Printf(TEXT("Уровень рыбака %d"), St.MinLevel), P.GetLevel() >= St.MinLevel, SmallS() + 0.5f);
			Y += 6.f;
			const bool bCan = P.GetLevel() >= St.MinLevel && P.HasIngredients(St.Cost) && P.CanAfford(St.Coins);
			const float BH = FV::Ui::ButtonH();
			if (D.Button(FFVRect(X, Y, W, BH), FString::Printf(TEXT("Чинить: %s"), *St.Name), EFVButton::Primary, bCan, BodyS())) { bRepair = true; }
			Y += BH + 8.f;
			Col.End(D, Y, 6.f);
		}

		void RunActions(UFVSaveGame& P)
		{
			UFVGame* G = Game;
			if (!G) { return; }
			if (bRepair)
			{
				bRepair = false;
				const int32 Before = StageNum(P);
				const EFVBuyResult R = P.RepairBoatStage();
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				const TArray<FFVBoatStage>& Stages = FVDBU6::BoatStages();
				const FString Name = Stages.IsValidIndex(Before) ? Stages[Before].Name : FString(TEXT("ремонт"));
				Success(G, P.IsBoatRepaired() ? FString(TEXT("Лодка починена! Можно отплывать в Город")) : FString::Printf(TEXT("Готово: %s"), *Name),
					EFVSfx::Hammer, EFVIcon::Boat);
				Refresh(G);
				return;
			}
			if (bSail)
			{
				bSail = false;
				G->OpenScreenWith(EFVScreen::Voyage, FVDBU6::CityId());
			}
		}
	};

	// ============================================================================================
	// Voyage: a short sailing scene
	// ============================================================================================
	constexpr float VoyageSeconds = 3.6f;

	class FVoyageScreen final : public FVScreen
	{
	public:
		explicit FVoyageScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			Dest = Game ? Game->GetScreenContext() : NAME_None;
			if (Dest.IsNone()) { Dest = FVDBU6::CityId(); }
			Age = 0.f;
			bDone = false;
			bHorn = false;
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			Age += FMath::Min(Dt, 0.1f);
			if (!bHorn)
			{
				bHorn = true;
				FVUI::Sfx(EFVSfx::BoatHorn);
			}
			if (In.WasPressed(EKeys::SpaceBar) || In.WasPressed(EKeys::Enter) || In.WasPressed(EKeys::Escape)) { Age = FMath::Max(Age, VoyageSeconds); }
		}

		virtual bool HandlesEscape() const override { return true; }
		virtual bool DrawsWorldBelow() const override { return false; }

		virtual void Draw(FVDraw& D) override
		{
			const FFVRect Screen = D.ScreenRect();
			const float T = float(D.Time);
			const bool bToCity = Dest == FVDBU6::CityId();
			const float K = FMath::Clamp(Age / VoyageSeconds, 0.f, 1.f);
			UFVSprites* Spr = UFVSprites::Get();
			// sky + sea fallback, then the art layers with parallax
			D.GradientV(Screen, FV::Col(120, 186, 232), FV::Col(214, 238, 250), 6);
			const float SeaY = Screen.Y + Screen.H * 0.55f;
			D.GradientV(FFVRect(Screen.X, SeaY, Screen.W, Screen.Bottom() - SeaY), FV::Col(60, 130, 190), FV::Col(28, 80, 140), 6);
			const int32 Frame = int32(T * 4.f) & 3;
			if (Spr)
			{
				for (int32 Layer = 0; Layer < 3; ++Layer)
				{
					const FFVSprite& S = Spr->VoyageSprite(Layer, Frame);
					if (!S.IsValid() || S.W <= 0.f || S.H <= 0.f) { continue; }
					const float Sc = FMath::Max(1.f, FMath::CeilToFloat(Screen.H / S.H * (Layer == 0 ? 1.f : 0.5f)));
					const float LW = S.W * Sc, LH = S.H * Sc;
					const float Speed = Layer == 0 ? 4.f : (Layer == 1 ? 18.f : 60.f);
					const float Shift = FMath::Fmod(T * Speed * (bToCity ? 1.f : -1.f), LW);
					const float LY = Layer == 0 ? Screen.Y : (Layer == 1 ? SeaY - LH * 0.6f : Screen.Bottom() - LH);
					for (float X = Screen.X - LW - Shift; X < Screen.Right() + LW; X += LW)
					{
						D.SpriteRect(S, FFVRect(FMath::RoundToFloat(X), FMath::RoundToFloat(LY), LW, LH), FLinearColor::White, !bToCity);
					}
				}
				const FFVSprite& Boat = Spr->VoyageSprite(3, Frame);
				const FFVSprite& BoatS = Boat.IsValid() ? Boat : Spr->BoatSprite(3);
				if (BoatS.IsValid() && BoatS.W > 0.f)
				{
					const float Sc = FMath::Clamp(FMath::FloorToFloat(Screen.W * 0.3f / BoatS.W), 1.f, 4.f);
					const float Bob = FMath::RoundToFloat(FMath::Sin(T * 2.2f) * 3.f);
					const float BX = Screen.X + Screen.W * 0.5f - BoatS.W * Sc * 0.5f;
					const float BY = SeaY + Screen.H * 0.08f - BoatS.H * Sc * 0.7f + Bob;
					D.SpriteRect(BoatS, FFVRect(FMath::RoundToFloat(BX), BY, BoatS.W * Sc, BoatS.H * Sc), FLinearColor::White, !bToCity);
				}
			}
			// caption + progress
			const bool bP = FV::IsPortrait();
			const FString Cap = bToCity ? TEXT("Плывём в Город…") : TEXT("Плывём домой, к Морскому причалу…");
			D.TextOutlined(Cap, FVector2D(Screen.Center().X, Screen.Y + (bP ? 60.f : 26.f)), bP ? 14.f : 12.f, FV::Col(255, 248, 226), EFVAlign::Center);
			const float BW = FMath::Min(Screen.W - 40.f, 240.f);
			D.Bar(FFVRect(Screen.Center().X - BW * 0.5f, Screen.Y + (bP ? 84.f : 44.f), BW, 6.f), K, FV::Col(255, 220, 120));
			D.TextOutlined(FV::IsTouchUI() ? TEXT("Коснитесь, чтобы пропустить") : TEXT("Щёлкните, чтобы пропустить"),
				FVector2D(Screen.Center().X, Screen.Bottom() - (bP ? 40.f : 20.f)), FV::Ui::Small(), FV::Col(236, 244, 250), EFVAlign::Center);
			if (D.Clicked(Screen)) { Age = FMath::Max(Age, VoyageSeconds); }

			if (Age >= VoyageSeconds && !bDone && Game)
			{
				bDone = true;
				UFVGame* G = Game;          // the screen may be destroyed by the calls below
				const FName To = Dest;
				// the City opens with the repaired boat (it has no coin price): unlock it on the first voyage
				if (UFVSaveGame* P = G->GetProfile())
				{
					if (To == FVDBU6::CityId() && P->IsBoatRepaired() && !P->IsLocationUnlocked(To)) { P->UnlockLocation(To); }
				}
				const bool bOk = G->TravelToEntry(To, FName(TEXT("pier")));
				if (!bOk)
				{
					FVUI::Sfx(EFVSfx::Error);
					G->Toast(To == FVDBU6::CityId() ? TEXT("Город пока закрыт — нужен 10 уровень") : TEXT("Сейчас туда не доплыть"),
						FV::Col(255, 150, 130), EFVIcon::Boat);
				}
				if (G->GetScreen() == EFVScreen::Voyage) { G->CloseScreen(); }
				return;
			}
		}

	private:
		FName Dest;
		float Age = 0.f;
		bool bDone = false;
		bool bHorn = false;
	};
}

TUniquePtr<FVScreen> FVMakeBoatScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenBoatImpl::FBoatScreen>(Game);
}

TUniquePtr<FVScreen> FVMakeVoyageScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenBoatImpl::FVoyageScreen>(Game);
}
