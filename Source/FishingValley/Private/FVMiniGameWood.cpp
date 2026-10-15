// UPDATE 6 [MINI-WOOD] — mini-games «Рубка» and «Распил» (Docs/UPDATE6_FOREST_CITY.md §4, §6.1). Owner: MINI-WOOD.
//   Chopping (context "<loc>:<treeId>" or "forest:blockage"): the axe swings like a pendulum over the notch; strike when
//            it is over the notch. FVDBU6::TreeHits(kind, axe level) accurate strikes fell the tree; a miss sticks the
//            axe for 0.5 s; a series of 3 accurate strikes gives a bonus branch. -> ChopTree / ClearBlockage.
//   Sawing   (context = recipe id with bSawGame): the log rides to the saw; hold to raise the cut, release to lower it,
//            follow the pencil line. >= 90 % -> 4 boards, >= 70 % -> 3, else 2 (+1 «Пилорама»): Craft + extra boards.
// Both: Space / Enter / LMB / finger, both orientations, a result card inside the window, Esc = back (nothing spent).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"
#include "FVScene.h"
#include "FVSprites.h"

namespace FVMiniGameWoodImpl
{
	FORCEINLINE float Clamp01(float V) { return FMath::Clamp(V, 0.f, 1.f); }
	inline void Sfx(EFVSfx S, float Volume = 1.f, float Pitch = 1.f) { FVUI::Sfx(S, Volume, Pitch); }
	inline bool KeyPressed(const FFVInput& In)
	{
		return In.WasPressed(EKeys::SpaceBar) || In.WasPressed(EKeys::Enter) || In.WasPressed(EKeys::Gamepad_FaceButton_Bottom);
	}
	inline FLinearColor InkGood() { return FV::Col(52, 122, 36); }
	inline FLinearColor InkBad() { return FV::Col(178, 46, 32); }
	inline FName SawmillId() { return FName(TEXT("yb_sawmill")); }

	FString LootText(const TArray<FFVIngredient>& Loot)
	{
		TArray<FString> Parts;
		for (const FFVIngredient& I : Loot)
		{
			if (I.Count > 0) { Parts.Add(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(I.ItemId), I.Count)); }
		}
		return FString::Join(Parts, TEXT(", "));
	}

	// =====================================================================================================
	// shared frame: window, play area, footer, result card
	// =====================================================================================================
	class FWoodGame : public FVScreen
	{
	public:
		FWoodGame(UFVGame* InGame, bool bInSaw) : FVScreen(InGame), bSawScreen(bInSaw) { bBot = InGame && InGame->IsAutoTest(); }
		virtual bool HandlesEscape() const override { return true; }

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			if (!Game || bLeaving) { return; }
			Dt = FMath::Clamp(Dt, 0.f, 0.1f);
			Time += Dt;
			PopT += Dt;
			if (bCard)
			{
				CardAge += Dt;
				if (CardAge > 0.35f && (KeyPressed(In) || In.WasPressed(EKeys::Escape))) { Leave(); return; }
				if (bBot && CardAge > 1.3f) { Leave(); }
				return;
			}
			if (In.WasPressed(EKeys::Escape)) { RequestBack(); return; }
			TickGame(Dt, In);
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bPortrait = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			bool bBack = D.Window(Win, Title());
			const FString HintS = bCard ? FString() : Hint();
			FFVRect Area;
			if (bPortrait)
			{
				const FFVRect Bar = FVDraw::ActionBarRect(Win);
				const float Top = Win.Y + 22.f;
				const float HintH = HintS.IsEmpty() ? 0.f : D.MeasureWrapped(HintS, Win.W - 36.f, FV::Ui::Small(), false) + 6.f;
				Area = FFVRect(Win.X + 10.f, Top, Win.W - 20.f, FMath::Max(60.f, Bar.Y - 12.f - HintH - Top));
				if (!HintS.IsEmpty()) { D.TextWrapped(HintS, FVector2D(Win.X + 18.f, Area.Bottom() + 6.f), Win.W - 36.f, FV::Ui::Small(), FVPal::InkSoft()); }
				if (D.ActionBar(Win)) { bBack = true; }
			}
			else
			{
				Area = FFVRect(Win.X + 10.f, Win.Y + 14.f, Win.W - 20.f, Win.H - 50.f);
				const FFVRect Row(Win.X + 10.f, Win.Bottom() - 30.f, Win.W - 20.f, 20.f);
				D.Divider(FVector2D(Row.X, Row.Y - 5.f), Row.W);
				if (D.BackButton(FFVRect(Row.X, Row.Y, 90.f, Row.H), TEXT("Назад"), 8.f)) { bBack = true; }
				if (!HintS.IsEmpty())
				{
					const float L = Row.X + 98.f, R = Row.Right();
					D.Text(D.Ellipsize(HintS, R - L, 6.5f, false), FVector2D((L + R) * 0.5f, Row.Y + 5.f), 6.5f, FVPal::InkSoft(), EFVAlign::Center, false, false);
				}
			}

			const bool bWasBlocked = D.IsInputBlocked();
			if (bCard || bLeaving) { D.SetInputBlocked(true); }
			const bool bLive = !bCard && !bLeaving && !D.IsInputBlocked();
			bPtrHold = bLive && D.IsPointerDown() && Area.Contains(D.PressPos());
			const bool bPtrPress = bLive && D.PressedIn(Area);
			const float Shake = ShakeT > 0.f ? FMath::Sin(Time * 70.f) * 3.f * Clamp01(ShakeT / 0.25f) : 0.f;
			DrawGame(D, Area.Offset(Shake, 0.f));
			D.SetInputBlocked(bWasBlocked);
			if (bPtrPress) { OnPointerPress(); }
			DrawPop(D, Area);

			if (bCard && DrawCard(D, Area)) { Leave(); return; }
			if (bBack) { RequestBack(); }
		}

	protected:
		bool bSawScreen = false;
		bool bBot = false;
		bool bLeaving = false;
		float Time = 0.f;
		float ShakeT = 0.f;
		bool bPtrHold = false;
		// result card
		bool bCard = false;
		bool bCardGood = false;
		float CardAge = 0.f;
		FString CardTitle, CardSub, CardLoot;
		EFVIcon CardIcon = EFVIcon::Check;
		// floating word
		FString PopText;
		FLinearColor PopColor = FLinearColor::White;
		float PopT = 99.f;

		virtual FString Title() const = 0;
		virtual void TickGame(float Dt, const FFVInput& In) = 0;
		virtual void DrawGame(FVDraw& D, const FFVRect& Area) = 0;
		virtual void OnPointerPress() {}
		virtual FString Hint() const { return FString(); }
		virtual bool QuitNeedsConfirm() const { return false; }

		static void ReturnTo(UFVGame* G, bool bSaw)
		{
			if (!G) { return; }
			if (bSaw) { G->OpenScreenWith(EFVScreen::YardBuilding, SawmillId()); }
			else { G->ReturnFromMiniGame(NAME_None); }
		}

		void Pop(const FString& S, const FLinearColor& C) { PopText = S; PopColor = C; PopT = 0.f; }

		void ShowCard(bool bGood, const FString& InTitle, const FString& Sub, const FString& Loot, EFVIcon Icon, EFVSfx Sound)
		{
			bCard = true; bCardGood = bGood; CardAge = 0.f;
			CardTitle = InTitle; CardSub = Sub; CardLoot = Loot; CardIcon = Icon;
			Sfx(Sound, 0.9f);
			if (bBot && Game) { Game->RequestShot(FString::Printf(TEXT("minigame_%s_result"), bSawScreen ? TEXT("sawing") : TEXT("chopping"))); }
		}
		void Fail(const FString& Reason, EFVIcon Icon, const FString& InTitle = TEXT("Не получится"))
		{
			ShowCard(false, InTitle, Reason, FString(), Icon, EFVSfx::Error);
		}

		/** Last statement of any caller: the screen is retired here. */
		void Leave()
		{
			if (bLeaving || !Game) { return; }
			bLeaving = true;
			ReturnTo(Game, bSawScreen);
		}

		void RequestBack()
		{
			if (bCard || !QuitNeedsConfirm()) { Leave(); return; }
			if (!Game || Game->HasModal()) { return; }
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			const bool bSaw = bSawScreen;
			Game->Confirm(TEXT("Бросить?"), TEXT("Ничего не потратится."), [WeakGame, bSaw]()
			{
				if (UFVGame* G = WeakGame.Get()) { ReturnTo(G, bSaw); }
			}, TEXT("Бросить"), TEXT("Остаться"));
		}

	private:
		void DrawPop(FVDraw& D, const FFVRect& Area) const
		{
			constexpr float Life = 0.9f;
			if (PopText.IsEmpty() || PopT >= Life) { return; }
			const float T = PopT / Life;
			const float A = T > 0.6f ? (1.f - T) / 0.4f : 1.f;
			FLinearColor C = PopColor; C.A = Clamp01(A);
			const float Size = FV::IsPortrait() ? 18.f : 14.f;
			D.TextOutlined(PopText, FVector2D(Area.Center().X, Area.Y + Area.H * 0.2f - T * 10.f), Size, C, EFVAlign::Center,
				FV::Col(40, 22, 12, uint8(255.f * Clamp01(A))));
		}

		/** Returns true when the button is clicked. */
		bool DrawCard(FVDraw& D, const FFVRect& Area)
		{
			const bool bPortrait = FV::IsPortrait();
			const float W = FMath::Min(Area.W - 16.f, bPortrait ? 320.f : 300.f);
			const float TextW = W - 28.f;
			const float SubH = CardSub.IsEmpty() ? 0.f : D.MeasureWrapped(CardSub, TextW, FV::Ui::Body(), false);
			const float LootH = CardLoot.IsEmpty() ? 0.f : D.MeasureWrapped(CardLoot, TextW, FV::Ui::Body(), true) + 6.f;
			const float BtnH = bPortrait ? 34.f : 22.f;
			const float TitleH = D.LineHeight(FV::Ui::Body() + 3.f, true);
			const float H = 34.f + TitleH + SubH + LootH + BtnH + 20.f;
			const FVector2D C = Area.Center();
			const FFVRect R(C.X - W * 0.5f, C.Y - H * 0.5f, W, H);
			D.Panel(R, EFVPanel::Parchment);
			float Y = R.Y + 10.f;
			if (const FFVSprite* Ic = FVUI::Icon(CardIcon)) { D.SpriteCentered(*Ic, FVector2D(R.Center().X, Y + 8.f), 1.f); }
			Y += 20.f;
			D.Text(CardTitle, FVector2D(R.Center().X, Y), FV::Ui::Body() + 3.f, bCardGood ? InkGood() : InkBad(), EFVAlign::Center, false, true);
			Y += TitleH + 4.f;
			if (!CardSub.IsEmpty()) { Y += D.TextWrapped(CardSub, FVector2D(R.X + 14.f, Y), TextW, FV::Ui::Body(), FVPal::Ink()); }
			if (!CardLoot.IsEmpty()) { Y += 6.f; D.TextWrapped(CardLoot, FVector2D(R.X + 14.f, Y), TextW, FV::Ui::Body(), FVPal::Ink(), true); }
			const float BW = bPortrait ? 160.f : 120.f;
			const FFVRect Btn(R.Center().X - BW * 0.5f, R.Bottom() - BtnH - 10.f, BW, BtnH);
			const bool bWasBlocked = D.IsInputBlocked();
			D.SetInputBlocked(false);
			const bool bClick = D.Button(Btn, bCardGood ? TEXT("Готово") : TEXT("Назад"), EFVButton::Primary, CardAge > 0.3f, FV::Ui::Body() + 1.f);
			D.SetInputBlocked(bWasBlocked);
			return bClick;
		}
	};

	// =====================================================================================================
	// CHOPPING
	// =====================================================================================================
	class FChopScreen final : public FWoodGame
	{
	public:
		explicit FChopScreen(UFVGame* InGame) : FWoodGame(InGame, false) {}

		virtual void OnOpen() override
		{
			NodeId = Game ? Game->GetScreenContext() : NAME_None;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			FString Loc, Anchor;
			if (!P || !NodeId.ToString().Split(TEXT(":"), &Loc, &Anchor)) { Fail(TEXT("Здесь нечего рубить."), EFVIcon::Axe); return; }
			bBlockage = Anchor == TEXT("blockage");
			if (P->U6.AxeLevel <= 0) { Fail(TEXT("Нужен топор — спросите у Боцмана на причале."), EFVIcon::Axe, TEXT("Нечем рубить")); return; }
			if (bBlockage)
			{
				if (P->U6.bBlockageCleared) { Fail(TEXT("Завал уже расчищен."), EFVIcon::Check, TEXT("Путь свободен")); return; }
				Kind = EFVTreeKind::Pine;
				Required = FVDBU6::TreeHits(Kind, P->U6.AxeLevel) + 2;
			}
			else
			{
				const FName AnchorId(*Anchor);
				for (const FFVWorldObject& O : Game->GetScene().GetWorldObjects())
				{
					if (O.Kind == EFVObjKind::Tree && O.Id == AnchorId)
					{
						Kind = EFVTreeKind(FMath::Clamp(O.Index, 0, int32(EFVTreeKind::Count) - 1));
						break;
					}
				}
				if (P->GetTreeState(NodeId, EFVTreeState::Marked) != EFVTreeState::Marked)
				{
					Fail(TEXT("Это дерево ещё не выросло. Загляните попозже."), EFVIcon::Clock, TEXT("Рано"));
					return;
				}
				Required = FVDBU6::TreeHits(Kind, P->U6.AxeLevel);
				if (Required <= 0) { Fail(TEXT("Этот топор не берёт такую породу. Нужен топор покрепче."), EFVIcon::Axe, TEXT("Слишком твёрдо")); return; }
			}
			Required = FMath::Clamp(Required, 1, 20);
			bReady = true;
		}

	protected:
		static constexpr float MaxAngle = 1.05f;   // radians
		static constexpr float StuckTime = 0.5f;
		FName NodeId;
		bool bBlockage = false;
		bool bReady = false;
		bool bFinished = false;
		EFVTreeKind Kind = EFVTreeKind::Birch;
		int32 Required = 4;
		int32 Hits = 0;
		int32 Streak = 0;
		int32 Bonus = 0;
		int32 Misses = 0;
		float Phase = 0.f;
		float StuckT = 0.f;
		float Cooldown = 0.f;
		float SwingT = 99.f;          // since the last strike (axe drop animation)
		float FallT = -1.f;           // tree falling animation

		virtual FString Title() const override { return bBlockage ? FString(TEXT("Расчистка завала")) : FString(TEXT("Рубка")); }
		virtual bool QuitNeedsConfirm() const override { return bReady && !bFinished && Hits > 0; }
		virtual FString Hint() const override
		{
			if (!bReady || bFinished) { return FString(); }
			return FV::IsTouchUI() ? FString(TEXT("Коснитесь, когда топор над зарубкой. Три точных удара подряд — бонусная ветка."))
				: FString(TEXT("Пробел или клик — когда топор над зарубкой. Три точных удара подряд — бонусная ветка."));
		}

		float Speed() const { return 2.4f + 0.12f * float(Hits) + (bBlockage ? 0.3f : 0.f); }
		float Angle() const { return FMath::Sin(Phase) * MaxAngle; }
		float HitWindow() const { return 0.2f - 0.012f * float(FMath::Min(Hits, 6)); }

		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			if (!bReady) { return; }
			SwingT += Dt;
			ShakeT = FMath::Max(0.f, ShakeT - Dt);
			Cooldown = FMath::Max(0.f, Cooldown - Dt);
			if (FallT >= 0.f)
			{
				FallT += Dt;
				if (FallT > 1.0f && !bFinished) { Finish(); }
				return;
			}
			if (StuckT > 0.f) { StuckT -= Dt; return; }
			Phase += Dt * Speed();
			if (bBot) { if (FMath::Abs(Angle()) < HitWindow() * 0.5f && Cooldown <= 0.f) { Strike(); } return; }
			if (KeyPressed(In)) { Strike(); }
		}

		virtual void OnPointerPress() override { Strike(); }

		void Strike()
		{
			if (!bReady || bFinished || FallT >= 0.f || StuckT > 0.f || Cooldown > 0.f) { return; }
			Cooldown = 0.15f;
			SwingT = 0.f;
			if (FMath::Abs(Angle()) <= HitWindow())
			{
				++Hits; ++Streak;
				Sfx(EFVSfx::AxeChop, 1.f, 0.95f + 0.05f * float(Streak % 4));
				ShakeT = 0.12f;
				if (Streak % 3 == 0 && Bonus < 3)
				{
					++Bonus;
					Pop(TEXT("Серия! +ветка"), FVPal::Gold());
				}
				else
				{
					Pop(FMath::Abs(Angle()) < HitWindow() * 0.4f ? TEXT("Точно!") : TEXT("Есть!"), FVPal::Green());
				}
				if (Hits >= Required)
				{
					FallT = 0.f;
					Sfx(EFVSfx::TreeFall, 1.f);
				}
			}
			else
			{
				Streak = 0; ++Misses;
				StuckT = StuckTime;
				ShakeT = 0.25f;
				Sfx(EFVSfx::AxeStuck, 1.f);
				Pop(TEXT("Застрял!"), FVPal::Red());
			}
		}

		void Finish()
		{
			bFinished = true;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { Leave(); return; }
			TArray<FFVIngredient> Loot;
			const EFVBuyResult R = bBlockage ? P->ClearBlockage(Loot) : P->ChopTree(NodeId, Kind, Bonus, Loot);
			if (R != EFVBuyResult::Ok)
			{
				const FString Why = R == EFVBuyResult::NoRoom ? FString(TEXT("Рюкзак полон — освободите место и попробуйте снова."))
					: UFVSaveGame::BuyResultText(R);
				Fail(Why, EFVIcon::Backpack, TEXT("Не вышло"));
				return;
			}
			Game->NotifyProfileChanged();
			const FString LootS = LootText(Loot);
			if (!LootS.IsEmpty()) { Game->Toast(FString::Printf(TEXT("Добыто: %s"), *LootS), FVPal::Green(), EFVIcon::Axe); }
			FString Sub;
			if (bBlockage) { Sub = TEXT("Тропа в чащу свободна!"); }
			else
			{
				Sub = FString::Printf(TEXT("Точных ударов: %d, промахов: %d"), Hits, Misses);
				if (Bonus > 0) { Sub += FString::Printf(TEXT(", бонусных веток: %d"), Bonus); }
			}
			ShowCard(true, bBlockage ? TEXT("Завал расчищен") : TEXT("Дерево срублено"), Sub, LootS, EFVIcon::Axe, EFVSfx::Perfect);
		}

		virtual void DrawGame(FVDraw& D, const FFVRect& A) override
		{
			D.GradientV(A, FV::Col(168, 206, 150), FV::Col(98, 142, 76), 12);
			const float GroundY = A.Bottom() - FMath::Max(10.f, A.H * 0.08f);
			D.Rect(FFVRect(A.X, GroundY, A.W, A.Bottom() - GroundY), FV::Col(92, 70, 44));
			const float CX = A.Center().X;
			const float NotchY = GroundY - A.H * 0.24f;

			if (bBlockage)
			{
				for (int32 i = 0; i < 3; ++i)
				{
					const float LW = A.W * 0.55f - float(i) * 18.f, LH = FMath::Max(10.f, A.H * 0.08f);
					const FFVRect Log(CX - LW * 0.5f + float(i) * 6.f, GroundY - LH * float(i + 1), LW, LH - 1.f);
					D.Rect(Log, FV::Col(128, 84, 46));
					D.Frame(Log, FV::Col(64, 38, 22));
					D.Circle(FVector2D(Log.X + 4.f, Log.Center().Y), LH * 0.45f, FV::Col(214, 172, 110));
				}
			}
			else
			{
				const float Tilt = FallT >= 0.f ? FMath::Min(1.f, FallT / 0.8f) : 0.f;
				UFVSprites* Sp = UFVSprites::Get();
				const FFVSprite* S = Sp ? &Sp->TreeSprite(Kind, EFVTreeState::Marked, 0) : nullptr;
				const FLinearColor Tint(1.f, 1.f, 1.f, 1.f - Tilt * 0.8f);
				if (S && S->IsValid() && S->H > 0.f)
				{
					const float Scale = FMath::Clamp((GroundY - A.Y - 6.f) / S->H, 0.5f, 6.f);
					D.Sprite(*S, FVector2D(CX - S->W * Scale * 0.5f + Tilt * A.W * 0.2f, GroundY - S->H * Scale + Tilt * 20.f), Scale, Tint);
				}
				else
				{
					const uint8 Al = uint8(255.f * Tint.A);
					const float TW = FMath::Max(14.f, A.W * 0.06f);
					D.Rect(FFVRect(CX - TW * 0.5f, A.Y + A.H * 0.3f, TW, GroundY - A.Y - A.H * 0.3f), FV::Col(120, 82, 50, Al));
					D.Circle(FVector2D(CX, A.Y + A.H * 0.3f), A.H * 0.22f, FV::Col(60, 128, 60, Al));
				}
			}
			// the notch
			const float NW = FMath::Max(8.f, A.W * 0.03f);
			D.Rect(FFVRect(CX - NW, NotchY - 3.f, NW * 2.f, 6.f), FV::Col(236, 204, 150));
			D.Rect(FFVRect(CX - NW, NotchY - 1.f, NW * 2.f, 2.f), FV::Col(120, 70, 30));

			// the pendulum axe: at angle 0 the head is on the notch
			const float Radius = FMath::Clamp(A.H * 0.55f, 60.f, 260.f);
			const FVector2D Pivot(CX, NotchY - Radius);
			const float Ang = Angle();
			for (int32 s = -4; s <= 4; ++s)
			{
				const float a = HitWindow() * float(s) / 4.f;
				D.Circle(Pivot + FVector2D(FMath::Sin(a), FMath::Cos(a)) * (Radius + 8.f), 2.f, FV::Col(96, 204, 64, 200));
			}
			const float Drop = SwingT < 0.12f ? (1.f - SwingT / 0.12f) * 6.f : 0.f;
			const FVector2D Head = Pivot + FVector2D(FMath::Sin(Ang), FMath::Cos(Ang)) * (Radius + Drop);
			const FLinearColor Handle = StuckT > 0.f ? FV::Col(170, 70, 50) : FV::Col(150, 100, 56);
			D.Line(Pivot, Head, Handle, FV::IsPortrait() ? 4.f : 3.f);
			D.Circle(Pivot, 4.f, FV::Col(80, 60, 40));
			const FVector2D Dir(FMath::Cos(Ang), -FMath::Sin(Ang));
			const float HS = FV::IsPortrait() ? 12.f : 9.f;
			D.Line(Head - Dir * HS * 0.2f, Head + Dir * HS, FV::Col(170, 176, 186), HS * 0.9f);
			D.Line(Head + Dir * HS * 0.8f, Head + Dir * HS * 1.05f, FV::Col(236, 240, 246), HS * 0.9f);

			// progress
			const FFVRect Bar(A.X + 12.f, A.Y + 8.f, A.W - 24.f, FV::IsPortrait() ? 12.f : 9.f);
			D.Bar(Bar, float(Hits) / float(FMath::Max(1, Required)), FVPal::Green());
			D.Text(FString::Printf(TEXT("%d / %d"), Hits, Required), FVector2D(Bar.Center().X, Bar.Bottom() + 2.f), FV::Ui::Small(), FVPal::Ink(), EFVAlign::Center);
			if (Bonus > 0)
			{
				D.Text(FString::Printf(TEXT("Бонус: +%d"), Bonus), FVector2D(Bar.Right(), Bar.Bottom() + 2.f), FV::Ui::Small(), FV::Col(172, 108, 14), EFVAlign::Right);
			}
		}
	};

	// =====================================================================================================
	// SAWING
	// =====================================================================================================
	class FSawScreen final : public FWoodGame
	{
	public:
		explicit FSawScreen(UFVGame* InGame) : FWoodGame(InGame, true) {}

		virtual void OnOpen() override
		{
			RecipeId = Game ? Game->GetScreenContext() : NAME_None;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Recipe = FVDB::FindRecipe(RecipeId);
			if (!P || !Recipe || !Recipe->bSawGame) { Fail(TEXT("Нечего пилить."), EFVIcon::Saw); return; }
			const EFVBuyResult R = P->CanCraft(*Recipe);
			if (R != EFVBuyResult::Ok) { Fail(UFVSaveGame::BuyResultText(R), EFVIcon::Saw); return; }
			PhaseA = float(P->U6.TreesChopped % 7) * 0.9f + float(P->Day % 5) * 0.5f;
			bReady = true;
		}

	protected:
		static constexpr float Duration = 7.f;     // seconds of cutting
		static constexpr float Tol = 0.2f;         // line tolerance (of the half height)
		FName RecipeId;
		const FFVRecipeDef* Recipe = nullptr;
		bool bReady = false;
		bool bFinished = false;
		bool bStarted = false;
		float T = 0.f;
		float Cut = 0.f;            // -1..1, + = up
		float Vel = 0.f;
		float Good = 0.f, Total = 0.f;
		float SawSfxT = 0.f;
		float PhaseA = 0.f;
		TArray<FVector2D> Trail;    // (t, cut)

		virtual FString Title() const override { return TEXT("Распил"); }
		virtual bool QuitNeedsConfirm() const override { return bReady && !bFinished && bStarted; }
		virtual FString Hint() const override
		{
			if (!bReady || bFinished) { return FString(); }
			return FV::IsTouchUI() ? FString(TEXT("Держите палец — пила идёт вверх, отпустите — вниз. Ведите рез по карандашной линии."))
				: FString(TEXT("Держите пробел или кнопку мыши — пила идёт вверх, отпустите — вниз. Ведите по линии."));
		}

		float LineAt(float t) const { return 0.5f * FMath::Sin(t * 1.3f + PhaseA) + 0.2f * FMath::Sin(t * 2.9f + PhaseA * 1.7f); }
		float Accuracy() const { return Total > 0.f ? Good / Total : 0.f; }

		virtual void TickGame(float Dt, const FFVInput& In) override
		{
			if (!bReady || bFinished) { return; }
			bool bHold = In.bActionDown || bPtrHold;
			if (bBot) { bHold = Cut < LineAt(T + 0.15f); }
			if (!bStarted)
			{
				if (!bHold) { return; }
				bStarted = true;
			}
			Vel = FMath::Clamp(Vel + (bHold ? 5.f : -5.f) * Dt, -1.6f, 1.6f);
			Cut += Vel * Dt;
			if (Cut > 1.f) { Cut = 1.f; Vel = FMath::Min(Vel, 0.f); }
			if (Cut < -1.f) { Cut = -1.f; Vel = FMath::Max(Vel, 0.f); }
			T += Dt;
			Total += Dt;
			if (FMath::Abs(Cut - LineAt(T)) <= Tol) { Good += Dt; }
			if (Trail.Num() == 0 || T - float(Trail.Last().X) > 0.03f) { Trail.Add(FVector2D(T, Cut)); }
			SawSfxT -= Dt;
			if (SawSfxT <= 0.f) { SawSfxT = 0.38f; Sfx(EFVSfx::Saw, 0.7f, 0.9f + 0.2f * Accuracy()); }
			if (T >= Duration) { Finish(); }
		}

		void Finish()
		{
			bFinished = true;
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P || !Recipe) { Leave(); return; }
			const float Acc = Accuracy();
			int32 Boards = Acc >= 0.9f ? 4 : (Acc >= 0.7f ? 3 : 2);
			if (P->GetYardLevel(EFVYardBuilding::Sawmill) >= 2) { ++Boards; }   // «Пилорама»
			const EFVBuyResult R = P->Craft(*Recipe);
			if (R != EFVBuyResult::Ok) { Fail(UFVSaveGame::BuyResultText(R), EFVIcon::Saw, TEXT("Не вышло")); return; }
			int32 Got = FMath::Max(0, Recipe->OutputCount);
			const int32 Extra = Boards - Got;
			// §18.1: the water saw's +1 board per log is already given by Craft (on top of the cut's result)
			static const FName LogId(TEXT("mat_log"));
			for (const FFVIngredient& I : Recipe->Inputs)
			{
				if (I.ItemId == LogId && Recipe->OutputId == FName(TEXT("mat_board"))) { Got += FMath::Max(0, I.Count) * P->MillSawBonus(); }
			}
			FString Note;
			if (Extra > 0)
			{
				if (P->GiveItem(Recipe->OutputId, Extra)) { Got += Extra; }
				else if (P->AddTo(EFVContainer::Chest, Recipe->OutputId, Extra)) { Got += Extra; Note = TEXT(" Часть досок убрана в сундук."); }
				else { Note = TEXT(" Лишние доски не поместились ни в рюкзак, ни в сундук."); }
			}
			Game->NotifyProfileChanged();
			const TArray<FFVIngredient> Loot = { FFVIngredient{ Recipe->OutputId, Got } };
			const FString LootS = LootText(Loot);
			Game->Toast(FString::Printf(TEXT("Получено: %s"), *LootS), FVPal::Green(), EFVIcon::Saw);
			const int32 Pct = FMath::RoundToInt(Acc * 100.f);
			ShowCard(true, Pct >= 90 ? TEXT("Ровный рез!") : (Pct >= 70 ? TEXT("Хороший рез") : TEXT("Готово")),
				FString::Printf(TEXT("Точность: %d %%.%s"), Pct, *Note), LootS, EFVIcon::Saw, Pct >= 70 ? EFVSfx::Perfect : EFVSfx::Click);
		}

		virtual void DrawGame(FVDraw& D, const FFVRect& A) override
		{
			D.GradientV(A, FV::Col(214, 186, 140), FV::Col(170, 132, 88), 10);
			const float PxPerSec = FMath::Clamp(A.W * 0.22f, 60.f, 140.f);
			const float SawX = A.X + A.W * 0.38f;
			const float CY = A.Center().Y + 6.f;
			const float Half = FMath::Clamp(A.H * 0.3f, 24.f, 110.f);
			const FFVRect Log(SawX - T * PxPerSec - 10.f, CY - Half, Duration * PxPerSec + 20.f, Half * 2.f);
			D.PushClip(A);
			D.Rect(Log, FV::Col(186, 132, 76));
			D.Frame(Log, FV::Col(96, 58, 30), 2.f);
			for (int32 i = 1; i < 4; ++i)
			{
				const float Y = Log.Y + Log.H * float(i) / 4.f;
				D.Line(FVector2D(Log.X, Y), FVector2D(Log.Right(), Y), FV::Col(160, 108, 60), 1.f);
			}
			FVector2D Prev = FVector2D::ZeroVector;
			for (int32 i = 0; i <= 80; ++i)
			{
				const float t = Duration * float(i) / 80.f;
				const FVector2D Pt(SawX + (t - T) * PxPerSec, CY - LineAt(t) * Half);
				if (i > 0) { D.Line(Prev, Pt, FV::Col(60, 60, 70), 1.5f); }
				Prev = Pt;
			}
			for (int32 i = 1; i < Trail.Num(); ++i)
			{
				const FVector2D P0(SawX + (float(Trail[i - 1].X) - T) * PxPerSec, CY - float(Trail[i - 1].Y) * Half);
				const FVector2D P1(SawX + (float(Trail[i].X) - T) * PxPerSec, CY - float(Trail[i].Y) * Half);
				const bool bOk = FMath::Abs(float(Trail[i].Y) - LineAt(float(Trail[i].X))) <= Tol;
				D.Line(P0, P1, bOk ? FV::Col(60, 150, 40) : FV::Col(200, 60, 40), 3.f);
			}
			D.PopClip();
			// the saw blade
			const FVector2D Blade(SawX, CY - Cut * Half);
			const float BR = FV::IsPortrait() ? 16.f : 12.f;
			D.Line(FVector2D(SawX, A.Y + 24.f), Blade, FV::Col(90, 90, 100), 3.f);
			D.Circle(Blade, BR, FV::Col(190, 196, 206));
			for (int32 k = 0; k < 8; ++k)
			{
				const float a = (bStarted && !bFinished ? T * 18.f : 0.f) + float(k) * UE_PI / 4.f;
				D.Circle(Blade + FVector2D(FMath::Cos(a), FMath::Sin(a)) * BR, 2.f, FV::Col(120, 124, 134));
			}
			D.Circle(Blade, 3.f, FV::Col(70, 70, 80));
			// meters
			const FFVRect Bar(A.X + 12.f, A.Y + 8.f, A.W - 24.f, FV::IsPortrait() ? 12.f : 9.f);
			D.Bar(Bar, Clamp01(T / Duration), FVPal::WoodLight());
			const int32 Pct = FMath::RoundToInt(Accuracy() * 100.f);
			D.Text(FString::Printf(TEXT("Точность: %d %%"), Pct), FVector2D(Bar.Center().X, Bar.Bottom() + 2.f), FV::Ui::Small(),
				Pct >= 90 ? InkGood() : (Pct >= 70 ? FV::Col(172, 108, 14) : FVPal::Ink()), EFVAlign::Center);
			if (!bStarted && bReady)
			{
				D.TextOutlined(FV::IsTouchUI() ? TEXT("Коснитесь и держите") : TEXT("Нажмите и держите"),
					FVector2D(A.Center().X, A.Bottom() - 28.f), FV::IsPortrait() ? 14.f : 11.f, FVPal::White(), EFVAlign::Center, FV::Col(40, 22, 12));
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeChoppingScreen(UFVGame* Game)
{
	return MakeUnique<FVMiniGameWoodImpl::FChopScreen>(Game);
}

TUniquePtr<FVScreen> FVMakeSawingScreen(UFVGame* Game)
{
	return MakeUnique<FVMiniGameWoodImpl::FSawScreen>(Game);
}
