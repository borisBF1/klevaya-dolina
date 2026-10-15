// «Сундук» (backpack <-> house chest) and «Ледник» (keepnet <-> ice cellar): two-pane transfer screens.
// Select a stack in either pane (tap / click: clicks fire on release and never after a drag) and move one piece or the
// whole stack to the other pane, or move everything at once. Both orientations; portrait stacks the panes and puts the
// buttons in the footer. Update 3: both panes scroll smoothly (FFVScrollView: finger / mouse drag, fling, wheel).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "InputCoreTypes.h"

namespace FVStorageScreenImpl
{
	enum class EStorageKind : uint8 { Chest, IceCellar };

	// Landscape layout (virtual px)
	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr float PaneXs[2] = { 22.f, 326.f };
	constexpr float PaneWs[2] = { 288.f, 290.f };
	constexpr int32 GridCols = 10;
	constexpr float SlotSize = 26.f;
	constexpr float SlotPitch = 28.f;
	constexpr int32 MaxCells = 120;         // cells shown at most per pane (the grids scroll; max keepnet 60, chest 40, cellar 25, backpack 36)
	constexpr int32 LandVisRows = 6;        // landscape grid viewport height in rows (10 x 6 cells, scrolls beyond)
	constexpr float GridPad = 2.f;          // room for the selection frame inside the clipped grid viewport
	constexpr float HeadY = 40.f;
	constexpr float CapY = 56.f;
	constexpr float GridY = 70.f;
	constexpr float CardY = 246.f;
	constexpr float CardH = 42.f;
	constexpr float ButtonY = 292.f;

	namespace Port
	{
		constexpr int32 GridCols = 6;
		constexpr float SlotGap = 3.f;
		constexpr float CardH = 62.f;
		inline float SlotSize() { return FV::Ui::Slot(); }
	}

	/** "в сундук" / "в рюкзак" / "в ледник" / "в садок". */
	static FString IntoText(EFVContainer C)
	{
		switch (C)
		{
		case EFVContainer::Backpack: return TEXT("в рюкзак");
		case EFVContainer::Chest: return TEXT("в сундук");
		case EFVContainer::IceCellar: return TEXT("в ледник");
		case EFVContainer::Keepnet: return TEXT("в садок");
		default: return FString();
		}
	}

	/** "Рюкзак полон" / "сундук полон"... (bLower: for the middle of a sentence). */
	static FString FullText(EFVContainer C, bool bLower = false)
	{
		switch (C)
		{
		case EFVContainer::Backpack: return bLower ? TEXT("рюкзак полон") : TEXT("Рюкзак полон");
		case EFVContainer::Chest: return bLower ? TEXT("сундук полон") : TEXT("Сундук полон");
		case EFVContainer::IceCellar: return bLower ? TEXT("ледник полон") : TEXT("Ледник полон");
		case EFVContainer::Keepnet: return bLower ? TEXT("садок полон") : TEXT("Садок полон");
		default: return bLower ? TEXT("нет места") : TEXT("Нет места");
		}
	}

	/** "в рюкзаке" / "в сундуке" / "в леднике" / "в садке". */
	static FString WhereText(EFVContainer C)
	{
		switch (C)
		{
		case EFVContainer::Backpack: return TEXT("в рюкзаке");
		case EFVContainer::Chest: return TEXT("в сундуке");
		case EFVContainer::IceCellar: return TEXT("в леднике");
		case EFVContainer::Keepnet: return TEXT("в садке");
		default: return FString();
		}
	}

	class FStorageScreen final : public FVScreen
	{
	public:
		FStorageScreen(UFVGame* InGame, EStorageKind InKind)
			: FVScreen(InGame)
			, Kind(InKind)
		{
			Panes[0] = Kind == EStorageKind::Chest ? EFVContainer::Backpack : EFVContainer::Keepnet;
			Panes[1] = Kind == EStorageKind::Chest ? EFVContainer::Chest : EFVContainer::IceCellar;
		}

		virtual void OnOpen() override
		{
			TickCount = 0;
			Sel = FSel();
			ActivePane = 0;
			PaneScroll[0].Reset();
			PaneScroll[1].Reset();
			CardScroll.Reset();
			CardKey = 0;
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			// ←/→ pick the pane the "move" buttons take from
			if (In.WasPressed(EKeys::Left) && ActivePane != 0) { SwitchPane(0); }
			else if (In.WasPressed(EKeys::Right) && ActivePane != 1) { SwitchPane(1); }
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bPortrait = FV::IsPortrait();
			const FFVRect Win = bPortrait ? FV::WindowRect() : WinRect;
			bool bClose = D.Window(Win, Kind == EStorageKind::Chest ? TEXT("Сундук") : TEXT("Ледник"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			FAction Act;
			if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), 9.f, FVPal::InkSoft(), EFVAlign::Center);
			}
			else if (bPortrait)
			{
				DrawPortrait(D, *P, Win, Act);
			}
			else
			{
				DrawLandscape(D, *P, Act);
				if (!FV::IsTouchUI())
				{
					(void)0; /* update 3: no key hints at the bottom (see the guide) */
				}
			}
			if (bPortrait)
			{
				// footer drawn last (it may change the profile)
				if (DrawActionBarPortrait(D, P, Win, Act)) { bClose = true; }
			}

			if (P && Act.Kind != EActKind::None)
			{
				if (RunAction(*P, Act)) { return; }   // opened another screen: this object is gone
			}
			if (bClose && Game)
			{
				UFVGame* G = Game;
				G->CloseScreen();   // last statement: the screen may be destroyed here
				return;
			}
		}

	private:
		struct FSel
		{
			int32 Pane = 0;
			FName Id;
			uint8 Q = 0;
			uint8 Flags = 0;
			bool bHas = false;
			int32 LastIndex = 0;
		};

		enum class EActKind : uint8 { None, MoveOne, MoveStack, MoveAll, Upgrade };
		struct FAction
		{
			EActKind Kind = EActKind::None;
			int32 FromPane = 0;
		};

		EStorageKind Kind;
		EFVContainer Panes[2];
		FSel Sel;
		int32 ActivePane = 0;        // the pane the move buttons take from (the selection's pane)
		FFVScrollView PaneScroll[2];     // smooth scrolling of each pane's grid
		FFVScrollView CardScroll;        // the text of the selected stack's card (long descriptions scroll inside the card)
		uint32 CardKey = 0;              // which stack CardScroll belongs to (another stack starts at the top)
		int32 TickCount = 0;

		void SwitchPane(int32 Pane)
		{
			ActivePane = FMath::Clamp(Pane, 0, 1);
			if (Sel.bHas && Sel.Pane != ActivePane) { Sel.bHas = false; }
			FVUI::Sfx(EFVSfx::Click, 0.6f);
		}

		bool IsStorageBuilt(const UFVSaveGame& P) const
		{
			return Kind == EStorageKind::Chest || P.IsStationBuilt(EFVStation::IceCellar);
		}

		int32 FindSelIndex(const UFVSaveGame& P) const
		{
			if (!Sel.bHas) { return INDEX_NONE; }
			const TArray<FFVInvStack>& A = P.GetContainer(Panes[Sel.Pane]);
			for (int32 i = 0; i < A.Num() && i < MaxCells; ++i)
			{
				if (A[i].ItemId == Sel.Id && A[i].Quality == Sel.Q && A[i].Flags == Sel.Flags && A[i].Count > 0) { return i; }
			}
			return INDEX_NONE;
		}

		/** Selected index in Sel.Pane; bAuto (portrait) keeps a stack of the active pane selected while it has any. */
		int32 ResolveSelection(const UFVSaveGame& P, bool bAuto)
		{
			int32 Index = FindSelIndex(P);
			if (Index == INDEX_NONE && bAuto)
			{
				const int32 Pane = Sel.bHas ? Sel.Pane : ActivePane;
				const TArray<FFVInvStack>& A = P.GetContainer(Panes[Pane]);
				const int32 Num = FMath::Min(A.Num(), MaxCells);
				if (Num > 0)
				{
					const int32 Auto = FMath::Clamp(Sel.LastIndex, 0, Num - 1);
					if (A.IsValidIndex(Auto) && A[Auto].Count > 0)
					{
						Select(Pane, A[Auto], Auto);
						Index = Auto;
					}
				}
			}
			if (Index == INDEX_NONE) { Sel.bHas = false; }
			else { Sel.LastIndex = Index; }
			return Index;
		}

		void Select(int32 Pane, const FFVInvStack& S, int32 Index)
		{
			Sel.Pane = Pane;
			Sel.Id = S.ItemId;
			Sel.Q = S.Quality;
			Sel.Flags = S.Flags;
			Sel.bHas = true;
			Sel.LastIndex = Index;
			ActivePane = Pane;
		}

		int32 NumCells(const UFVSaveGame& P, int32 Pane) const
		{
			const EFVContainer C = Panes[Pane];
			const int32 NumStacks = FMath::Min(P.GetContainer(C).Num(), MaxCells);
			const int32 Cap = FMath::Max(0, P.ContainerCapacity(C));
			const int32 Used = FMath::Max(0, P.ContainerUsed(C));
			const bool bUnits = C == EFVContainer::Keepnet || C == EFVContainer::IceCellar;
			const int32 Free = bUnits ? FMath::Max(0, Cap - Used) : FMath::Max(0, Cap - NumStacks);
			return FMath::Clamp(NumStacks + Free, 0, MaxCells);
		}

		/** One pane's grid inside Area: a smooth-scrolling viewport (the first row starts GridPad below Area.Y).
		    Taps select. Returns the hovered stack index. */
		int32 DrawPaneGrid(FVDraw& D, const UFVSaveGame& P, int32 Pane, const FFVRect& Area, int32 Cols, float Slot, float Pitch, bool bBig, int32 SelIndex)
		{
			const TArray<FFVInvStack>& A = P.GetContainer(Panes[Pane]);
			const int32 NumStacks = FMath::Min(A.Num(), MaxCells);
			const int32 Cells = NumCells(P, Pane);
			const int32 Rows = Cells > 0 ? FMath::DivideAndRoundUp(Cells, Cols) : 0;
			FFVScrollView& Scroll = PaneScroll[Pane];
			Scroll.Update(D, Area, Rows > 0 ? Rows * Pitch - (Pitch - Slot) + 2.f * GridPad : 0.f);

			// centred; when it scrolls, the grid moves left just enough to keep the scroll bar off the cells
			const float GridW = Cols * Pitch - (Pitch - Slot);
			const float Room = Area.W - GridW;
			const float BarRoom = bBig ? 6.f : 5.f;
			const float GX = FMath::RoundToFloat(Area.X + (Scroll.CanScroll() ? FMath::Max(0.f, FMath::Min(Room * 0.5f, Room - BarRoom)) : Room * 0.5f));
			const float Y0 = Area.Y + GridPad - Scroll.Offset();

			int32 Hovered = INDEX_NONE;
			D.PushClip(FFVRect(Area.X - 2.f, Area.Y, Area.W + 4.f, Area.H));
			for (int32 Index = 0; Index < Cells; ++Index)
			{
				const FFVRect R(GX + (Index % Cols) * Pitch, Y0 + (Index / Cols) * Pitch, Slot, Slot);
				if (R.Bottom() + 2.f < Area.Y || R.Y - 2.f > Area.Bottom()) { continue; }
				if (Index < NumStacks && A.IsValidIndex(Index))
				{
					const FFVInvStack& S = A[Index];
					const bool bSelected = Sel.bHas && Sel.Pane == Pane && Index == SelIndex;
					if (D.Slot(R, bSelected, true))
					{
						if (bSelected && !bBig) { Sel.bHas = false; ActivePane = Pane; }
						else if (!bSelected) { Select(Pane, S, Index); }
					}
					FVUI::StackCellContent(D, R, S, bBig);
					if (D.Hover(R)) { Hovered = Index; }
				}
				else
				{
					FVUI::EmptyCell(D, R);
				}
			}
			D.PopClip();
			Scroll.DrawBar(D, Area);
			Scroll.DrawFades(D, Area, FVPal::Parchment());
			return Hovered;
		}

		void StackTooltip(FVDraw& D, const UFVSaveGame& P, const FFVInvStack& S, int32 Pane)
		{
			FString Body = FVUI::StackKindText(S);
			for (const FString& L : FVUI::StackInfoLines(S, &P)) { Body += TEXT("\n") + L; }
			const int32 Unit = FVUI::MitrichPrice(S);
			if (Unit > 0) { Body += FString::Printf(TEXT("\nУ Митрича: %s за шт."), *FVUI::Coins(Unit)); }
			Body += FString::Printf(TEXT("\nВыберите и нажмите «%s»"), *(Pane == 0 ? FString(TEXT("→")) : FString(TEXT("←"))));
			const FFVFishDef* F = FVDB::FindFish(S.ItemId);
			D.Tooltip(FVUI::StackTitle(S), Body, F ? FVDB::RarityColor(F->Rarity) : FVPal::White());
		}

		/** "Сундук" + "Улучшить →" for the ice cellar. Returns true when the upgrade button was pressed. */
		bool PaneTitle(FVDraw& D, const UFVSaveGame& P, int32 Pane, float X, float Y, float W, float Size, float BtnH)
		{
			const EFVContainer C = Panes[Pane];
			FString Title = FVUI::ContainerName(C);
			if (C == EFVContainer::IceCellar && P.IsStationBuilt(EFVStation::IceCellar))
			{
				const FString LevelName = FVDB::StationLevelName(EFVStation::IceCellar, P.GetStationLevel(EFVStation::IceCellar));
				if (!LevelName.IsEmpty() && LevelName != Title) { Title += FString::Printf(TEXT(" · %s"), *LevelName); }
			}
			const bool bActive = ActivePane == Pane;
			if (bActive)
			{
				// marker of the pane the move buttons take from
				D.Rect(FFVRect(X - 6.f, Y + 2.f, 3.f, D.LineHeight(Size) - 3.f), FVPal::WoodLight());
			}
			bool bUpgrade = false;
			float TitleW = W;
			if (C == EFVContainer::IceCellar)
			{
				const bool bBuilt = P.IsStationBuilt(EFVStation::IceCellar);
				const bool bMax = bBuilt && P.GetStationLevel(EFVStation::IceCellar) >= FVDB::StationMaxLevel(EFVStation::IceCellar);
				const FString Label = !bBuilt ? FString(TEXT("Построить →")) : (bMax ? FString(TEXT("Ледник →")) : FString(TEXT("Улучшить →")));
				const float BW = FMath::Max(D.MeasureText(Label, Size - 2.f).X + 14.f, BtnH * 2.4f);
				const FFVRect Btn(X + W - BW, Y + FMath::RoundToFloat((D.LineHeight(Size) - BtnH) * 0.5f), BW, BtnH);
				bUpgrade = D.Button(Btn, Label, bMax ? EFVButton::Normal : EFVButton::Primary, true, Size - 2.f);
				TitleW = W - BW - 6.f;
			}
			const FFVRect TitleHit(X, Y, TitleW, D.LineHeight(Size));
			D.Text(D.Ellipsize(Title, TitleW, Size), FVector2D(X, Y), Size, bActive ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Left, bActive, true);
			if (!bActive && D.Clicked(TitleHit)) { SwitchPane(Pane); }
			return bUpgrade;
		}

		/** Card of the selected stack (both layouts). */
		void DrawCard(FVDraw& D, const UFVSaveGame& P, const FFVRect& Card, int32 SelIndex, bool bPortrait)
		{
			D.Panel(Card, EFVPanel::Parchment);
			const TArray<FFVInvStack>& A = P.GetContainer(Panes[Sel.Pane]);
			const float BodyS = bPortrait ? FV::Ui::Body() + 1.f : 9.f;
			const float SmallS = bPortrait ? FV::Ui::Small() : 7.f;
			if (!Sel.bHas || !A.IsValidIndex(SelIndex))
			{
				const FString Msg = Kind == EStorageKind::Chest
					? FString(TEXT("Выберите вещь в рюкзаке или в сундуке и переложите её. В сундуке всё хранится сколько угодно."))
					: FString(TEXT("Выберите рыбу в садке или в леднике. В леднике рыба ждёт, пока вы не соберётесь её продать."));
				D.TextWrapped(Msg, FVector2D(Card.X + 8.f, Card.Y + 6.f), Card.W - 16.f, SmallS, FVPal::InkSoft());
				return;
			}
			const FFVInvStack S = A[SelIndex];
			const uint32 Key = GetTypeHash(S.ItemId) * 31u + (uint32(S.Quality) | (uint32(S.Flags) << 8) | (uint32(Sel.Pane) << 16)) + 0x9E3779B9u;
			if (Key != CardKey)
			{
				CardKey = Key;
				CardScroll.Reset();   // another stack: its text starts at the top
			}
			const FFVRect IconBox(Card.X + 4.f, Card.Y + 4.f, bPortrait ? Card.H - 8.f : 40.f, Card.H - 8.f);
			D.Rect(IconBox, FV::Col(255, 236, 196, 150));
			FFVSprite Spr;
			if (FVUI::ItemIconOf(S.ItemId, Spr)) { FVUI::SpriteFit(D, Spr, IconBox.Inset(2.f), FLinearColor::White, 2.f); }
			const TArray<FString> Info = FVUI::StackInfoLines(S, &P);
			const int32 Unit = FVUI::MitrichPrice(S);
			const float NameLH = FMath::RoundToFloat(D.LineHeight(BodyS) + (bPortrait ? 1.f : 0.f));
			const float SmallLH = FMath::RoundToFloat(D.LineHeight(SmallS) + 1.f);

			// the name, the kind line and Mitrich's price (portrait: inside the scrolling text column)
			auto DrawHead = [&](float TX, float TW, float Y)
			{
				D.Text(D.Ellipsize(FVUI::StackTitle(S), TW, BodyS), FVector2D(TX, Y), BodyS, FVUI::StackInk(S), EFVAlign::Left, false, true);
				Y += NameLH;
				const EFVQuality Q = FVUI::QualityOf(S.Quality);
				float QX = TX;
				if (Q != EFVQuality::Normal)
				{
					D.Stars(FVector2D(QX, FMath::RoundToFloat(Y + D.LineHeight(SmallS) * 0.5f - 4.f)), Q);
					QX += 10.f;
				}
				D.Text(D.Ellipsize(FString::Printf(TEXT("%s · ×%d · %s"), *FVUI::StackKindText(S), S.Count, *WhereText(Panes[Sel.Pane])), TX + TW - QX, SmallS, false),
					FVector2D(QX, Y), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += SmallLH;
				if (Unit > 0)
				{
					FVUI::PriceLine(D, TEXT("У Митрича:"), Unit, S.Count, FVector2D(TX, Y), SmallS);
					Y += SmallLH;
				}
				return Y;
			};

			if (!bPortrait)
			{
				DrawHead(IconBox.Right() + 7.f, Card.X + Card.W * 0.55f - 8.f - (IconBox.Right() + 7.f), Card.Y + 2.f);
				// landscape: every detail line wrapped in full in the right part of the wide card (it scrolls when long)
				const float IX = Card.X + Card.W * 0.55f + 4.f;
				const FFVRect Area(IX - 2.f, Card.Y + 2.f, Card.Right() - 3.f - (IX - 2.f), Card.H - 4.f);
				const float IW = Area.Right() - 6.f - IX;   // the scroll bar lives in the last px
				float ContentH = 3.f;
				for (const FString& L : Info) { ContentH += D.MeasureWrapped(L, IW, 6.5f) + 2.f; }
				CardScroll.Update(D, Area, ContentH);
				D.PushClip(Area);
				float IY = Area.Y + 2.f - CardScroll.Offset();
				for (const FString& L : Info)
				{
					IY += D.TextWrapped(L, FVector2D(IX, IY), IW, 6.5f, FVPal::InkSoft()) + 2.f;
				}
				D.PopClip();
				CardScroll.DrawBar(D, Area);
				CardScroll.DrawFades(D, Area, FVPal::Parchment());
				return;
			}
			// portrait: the whole text column scrolls (the name, the kind, the price and every detail line wrapped in full)
			const FFVRect Area(IconBox.Right() + 3.f, Card.Y + 2.f, Card.Right() - 3.f - (IconBox.Right() + 3.f), Card.H - 4.f);
			const float TX = Area.X + 4.f;
			const float TW = Area.Right() - 6.f - TX;   // the scroll bar lives in the last px
			float ContentH = 1.f + NameLH + SmallLH + (Unit > 0 ? SmallLH : 0.f) + 2.f;
			for (const FString& L : Info) { ContentH += D.MeasureWrapped(L, TW, SmallS) + 1.f; }
			CardScroll.Update(D, Area, ContentH);
			D.PushClip(Area);
			float Y = DrawHead(TX, TW, Area.Y + 1.f - CardScroll.Offset());
			for (const FString& L : Info)
			{
				Y += D.TextWrapped(L, FVector2D(TX, Y), TW, SmallS, FVPal::InkSoft()) + 1.f;
			}
			D.PopClip();
			CardScroll.DrawBar(D, Area);
			CardScroll.DrawFades(D, Area, FVPal::Parchment());
		}

		// ---------------------------------------------------------------- landscape

		void DrawLandscape(FVDraw& D, UFVSaveGame& P, FAction& Act)
		{
			// separator between the panes
			D.Rect(FFVRect(PaneXs[1] - 9.f, WinRect.Y + 10.f, 1.f, CardY - WinRect.Y - 16.f), FVPal::ParchDark());
			D.Rect(FFVRect(PaneXs[1] - 8.f, WinRect.Y + 10.f, 1.f, CardY - WinRect.Y - 16.f), FV::Col(255, 236, 196));
			int32 SelIndex = ResolveSelection(P, false);
			int32 HoveredPane = INDEX_NONE;
			int32 Hovered = INDEX_NONE;
			for (int32 Pane = 0; Pane < 2; ++Pane)
			{
				const float X = PaneXs[Pane];
				const float W = PaneWs[Pane];
				if (PaneTitle(D, P, Pane, X, HeadY, W, 10.f, 15.f)) { Act.Kind = EActKind::Upgrade; }
				const EFVContainer C = Panes[Pane];
				if (Pane == 1 && !IsStorageBuilt(P))
				{
					D.TextWrapped(TEXT("Ледник ещё не построен. Постройте его — и рыба будет храниться здесь, пока вы не соберётесь её продать."),
						FVector2D(X + 4.f, GridY), W - 8.f, 7.5f, FVPal::InkSoft());
					continue;
				}
				const bool bUnits = C == EFVContainer::Keepnet || C == EFVContainer::IceCellar;
				FVUI::CapacityBar(D, bUnits ? TEXT("Занято:") : TEXT("Мест:"), P.ContainerUsed(C), P.ContainerCapacity(C), FVector2D(X, CapY), W, 7.f, 6.f);
				const int32 Cells = NumCells(P, Pane);
				const int32 Rows = FMath::Min(Cells > 0 ? FMath::DivideAndRoundUp(Cells, GridCols) : 0, LandVisRows);
				const FFVRect GridArea(X, GridY - GridPad, W, LandVisRows * SlotPitch - (SlotPitch - SlotSize) + 2.f * GridPad);
				const int32 H = DrawPaneGrid(D, P, Pane, GridArea, GridCols, SlotSize, SlotPitch, false, Sel.Pane == Pane ? SelIndex : INDEX_NONE);
				if (H != INDEX_NONE) { Hovered = H; HoveredPane = Pane; }
				if (P.GetContainer(C).Num() == 0)
				{
					const float EY = GridY + Rows * SlotPitch + 2.f;
					if (EY < CardY - 14.f)
					{
						D.Text(FString::Printf(TEXT("%s пуст"), *FVUI::ContainerName(C)), FVector2D(X + W * 0.5f, EY), 7.f, FVPal::InkSoft(), EFVAlign::Center, false, false);
					}
				}
			}
			SelIndex = FindSelIndex(P);   // a tap may have changed the selection
			if (Hovered != INDEX_NONE && !FV::IsTouchUI())
			{
				const TArray<FFVInvStack>& A = P.GetContainer(Panes[HoveredPane]);
				if (A.IsValidIndex(Hovered)) { StackTooltip(D, P, A[Hovered], HoveredPane); }
			}

			DrawCard(D, P, FFVRect(PaneXs[0], CardY, PaneXs[1] + PaneWs[1] - PaneXs[0], CardH), SelIndex, false);

			// ---- buttons: [1 шт. →][Стопку →]   [Всё в сундук →][← Всё в рюкзак]
			const bool bBuilt = IsStorageBuilt(P);
			const bool bSel = Sel.bHas && SelIndex != INDEX_NONE && bBuilt;
			const int32 From = bSel ? Sel.Pane : ActivePane;
			const FString Arrow = From == 0 ? TEXT("→") : TEXT("←");
			const FString Into = IntoText(Panes[1 - From]);
			const float X0 = PaneXs[0];
			const float FullW = PaneXs[1] + PaneWs[1] - PaneXs[0];
			const float Gap = 6.f;
			const float BW = FMath::FloorToFloat((FullW - Gap * 3.f) / 4.f);
			auto Btn = [&](int32 i) { return FFVRect(X0 + i * (BW + Gap), ButtonY, BW, 20.f); };
			const FString OneLabel = From == 0 ? FString::Printf(TEXT("1 шт. %s %s"), *Into, *Arrow) : FString::Printf(TEXT("%s 1 шт. %s"), *Arrow, *Into);
			const FString StackLabel = From == 0 ? FString::Printf(TEXT("Стопку %s %s"), *Into, *Arrow) : FString::Printf(TEXT("%s Стопку %s"), *Arrow, *Into);
			if (D.Button(Btn(0), OneLabel, EFVButton::Normal, bSel, 7.5f) && bSel) { Act.Kind = EActKind::MoveOne; Act.FromPane = From; }
			if (D.Button(Btn(1), StackLabel, EFVButton::Normal, bSel, 7.5f) && bSel) { Act.Kind = EActKind::MoveStack; Act.FromPane = From; }
			const bool bAny0 = bBuilt && P.GetContainer(Panes[0]).Num() > 0;
			const bool bAny1 = bBuilt && P.GetContainer(Panes[1]).Num() > 0;
			if (D.Button(Btn(2), FString::Printf(TEXT("Всё %s →"), *IntoText(Panes[1])), EFVButton::Primary, bAny0, 7.5f) && bAny0) { Act.Kind = EActKind::MoveAll; Act.FromPane = 0; }
			if (D.Button(Btn(3), FString::Printf(TEXT("← Всё %s"), *IntoText(Panes[0])), EFVButton::Primary, bAny1, 7.5f) && bAny1) { Act.Kind = EActKind::MoveAll; Act.FromPane = 1; }
			const FString Hint = Kind == EStorageKind::Chest
				? FString(TEXT("Сундук хранит ресурсы, материалы, еду, снасти и мусор. Рыба живёт в садке и леднике."))
				: FString(TEXT("Ледник хранит только рыбу. Больше места — улучшение ледника («Улучшить →»)."));
			D.Text(D.Ellipsize(Hint, FullW, 6.5f, false), FVector2D(X0, ButtonY + 25.f), 6.5f, FV::Alpha(FVPal::InkSoft(), 0.9f), EFVAlign::Left, false, false);
		}

		// ---------------------------------------------------------------- portrait

		void DrawPortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win, FAction& Act)
		{
			const float X = Win.X + 12.f;   // room for the active-pane marker on the left
			const float W = Win.W - 20.f;
			const float Top = Win.Y + 16.f;
			const float Bottom = FVUI::ActionBarExtraRowRect(Win).Y - 10.f;
			const FFVRect Card(Win.X + 8.f, Bottom - Port::CardH, Win.W - 16.f, Port::CardH);
			const float HeadS = FV::Ui::Header();
			const float SmallS = FV::Ui::Small();
			const float HeadH = FMath::RoundToFloat(D.LineHeight(HeadS) + 3.f);
			const float CapH = FMath::RoundToFloat(FMath::Max(D.LineHeight(SmallS), 10.f) + 5.f);
			const float SlotSz = Port::SlotSize();
			const float Pitch = SlotSz + Port::SlotGap;
			const float PaneGap = 10.f;
			const float PaneH = FMath::FloorToFloat((Card.Y - 8.f - Top - PaneGap) * 0.5f);
			int32 SelIndex = ResolveSelection(P, true);

			float Y = Top;
			for (int32 Pane = 0; Pane < 2; ++Pane)
			{
				const EFVContainer C = Panes[Pane];
				const float PaneTop = Y;
				if (PaneTitle(D, P, Pane, X, Y, W, HeadS, 26.f)) { Act.Kind = EActKind::Upgrade; }
				Y += HeadH;
				if (Pane == 1 && !IsStorageBuilt(P))
				{
					D.TextWrapped(TEXT("Ледник ещё не построен. Постройте его — и рыба будет храниться здесь, пока вы не соберётесь её продать."),
						FVector2D(X, Y + 4.f), W, FV::Ui::Body(), FVPal::InkSoft());
					Y = PaneTop + PaneH + PaneGap;
					continue;
				}
				const bool bUnits = C == EFVContainer::Keepnet || C == EFVContainer::IceCellar;
				FVUI::CapacityBar(D, bUnits ? TEXT("Занято:") : TEXT("Мест:"), P.ContainerUsed(C), P.ContainerCapacity(C), FVector2D(X, Y), W, SmallS, 10.f);
				Y += CapH;
				const int32 Cells = NumCells(P, Pane);
				const int32 TotalRows = FMath::Max(1, FMath::DivideAndRoundUp(Cells, Port::GridCols));
				const int32 FitRows = FMath::Max(1, FMath::FloorToInt((PaneTop + PaneH - Y + Port::SlotGap) / Pitch));
				const int32 VisRows = FMath::Clamp(FitRows, 1, TotalRows);
				const FFVRect GridArea(X, Y - GridPad, W, VisRows * Pitch - Port::SlotGap + 2.f * GridPad);
				DrawPaneGrid(D, P, Pane, GridArea, Port::GridCols, SlotSz, Pitch, true, Sel.Pane == Pane ? SelIndex : INDEX_NONE);
				if (Cells == 0)
				{
					D.Text(FString::Printf(TEXT("%s пуст"), *FVUI::ContainerName(C)), FVector2D(X + W * 0.5f, Y + 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center, false, false);
				}
				Y = PaneTop + PaneH + PaneGap;
				if (Pane == 0)
				{
					D.Divider(FVector2D(Win.X + 8.f, Y - PaneGap * 0.5f - 1.f), Win.W - 16.f);
				}
			}
			SelIndex = FindSelIndex(P);
			DrawCard(D, P, Card, SelIndex, true);
		}

		/** Footer: [1 шт. в сундук][Стопку в сундук] over [← Назад][Всё ↓][Всё ↑]. Returns true for "← Назад". */
		bool DrawActionBarPortrait(FVDraw& D, UFVSaveGame* P, const FFVRect& Win, FAction& Act)
		{
			const FFVRect Row0 = FVUI::ActionBarExtraRow(D, Win);
			FFVRect Actions;
			const bool bBack = D.ActionBar(Win, &Actions);
			const float TS = FV::Ui::Body() + 0.5f;
			const bool bBuilt = P && IsStorageBuilt(*P);
			const int32 SelIndex = P ? FindSelIndex(*P) : INDEX_NONE;
			const bool bSel = P && Sel.bHas && SelIndex != INDEX_NONE && bBuilt;
			const int32 From = bSel ? Sel.Pane : ActivePane;
			const FString Into = IntoText(Panes[1 - From]);
			const FString Arrow = From == 0 ? TEXT("↓") : TEXT("↑");
			if (D.Button(FVDraw::SplitH(Row0, 0, 2, 8.f), FString::Printf(TEXT("1 шт. %s %s"), *Into, *Arrow), EFVButton::Normal, bSel, TS) && bSel)
			{
				Act.Kind = EActKind::MoveOne;
				Act.FromPane = From;
			}
			if (D.Button(FVDraw::SplitH(Row0, 1, 2, 8.f), FString::Printf(TEXT("Стопку %s %s"), *Into, *Arrow), EFVButton::Normal, bSel, TS) && bSel)
			{
				Act.Kind = EActKind::MoveStack;
				Act.FromPane = From;
			}
			const bool bAny0 = bBuilt && P->GetContainer(Panes[0]).Num() > 0;
			const bool bAny1 = bBuilt && P->GetContainer(Panes[1]).Num() > 0;
			if (D.Button(FVDraw::SplitH(Actions, 0, 2, 6.f), FString::Printf(TEXT("Всё %s ↓"), *IntoText(Panes[1])), EFVButton::Primary, bAny0, TS) && bAny0)
			{
				Act.Kind = EActKind::MoveAll;
				Act.FromPane = 0;
			}
			if (D.Button(FVDraw::SplitH(Actions, 1, 2, 6.f), FString::Printf(TEXT("Всё %s ↑"), *IntoText(Panes[0])), EFVButton::Primary, bAny1, TS) && bAny1)
			{
				Act.Kind = EActKind::MoveAll;
				Act.FromPane = 1;
			}
			return bBack;
		}

		// ---------------------------------------------------------------- actions

		/** Runs the collected action. Returns true when another screen was opened (this object is destroyed). */
		bool RunAction(UFVSaveGame& P, const FAction& Act)
		{
			if (!Game) { return false; }
			if (Act.Kind == EActKind::Upgrade)
			{
				UFVGame* G = Game;
				G->OpenScreenWith(EFVScreen::Station, FVDB::StationId(EFVStation::IceCellar));
				return true;
			}
			const int32 FromPane = FMath::Clamp(Act.FromPane, 0, 1);
			const EFVContainer From = Panes[FromPane];
			const EFVContainer To = Panes[1 - FromPane];
			if (Act.Kind == EActKind::MoveAll)
			{
				const int32 Before = P.ContainerUsed(From);
				const int32 Moved = P.TransferAll(From, To);
				if (Moved > 0)
				{
					FVUI::Sfx(EFVSfx::Pickup);
					const bool bAll = P.GetContainer(From).Num() == 0;
					Game->Toast(bAll ? FString::Printf(TEXT("Переложено %s: %d шт."), *IntoText(To), Moved)
						: FString::Printf(TEXT("Переложено %s: %d шт. — %s"), *IntoText(To), Moved, *FullText(To, true)),
						bAll ? FVPal::White() : FV::Col(255, 200, 140), Kind == EStorageKind::Chest ? EFVIcon::Chest : EFVIcon::Snowflake);
					Game->NotifyProfileChanged();
				}
				else
				{
					FVUI::Sfx(EFVSfx::Error);
					Game->Toast(Before > 0 ? FullText(To) : FString::Printf(TEXT("%s пуст"), *FVUI::ContainerName(From)), FV::Col(255, 150, 130), EFVIcon::Exclaim);
				}
				return false;
			}
			const int32 Index = FindSelIndex(P);
			const TArray<FFVInvStack>& A = P.GetContainer(From);
			if (Index == INDEX_NONE || Sel.Pane != FromPane || !A.IsValidIndex(Index)) { return false; }
			const int32 Count = Act.Kind == EActKind::MoveOne ? 1 : A[Index].Count;
			const EFVBuyResult R = P.Transfer(From, Index, Count, To);
			if (R == EFVBuyResult::Ok)
			{
				FVUI::Sfx(EFVSfx::Pickup, 0.8f);
				Game->NotifyProfileChanged();
				return false;
			}
			// a whole stack that does not fit: move what fits piece by piece (all-or-nothing per piece)
			int32 Moved = 0;
			if (R == EFVBuyResult::NoRoom && Count > 1)
			{
				for (int32 k = 0; k < Count; ++k)
				{
					const int32 Idx = FindSelIndex(P);
					if (Idx == INDEX_NONE || P.Transfer(From, Idx, 1, To) != EFVBuyResult::Ok) { break; }
					++Moved;
				}
			}
			if (Moved > 0)
			{
				FVUI::Sfx(EFVSfx::Pickup, 0.8f);
				Game->Toast(FString::Printf(TEXT("Поместилось %d шт. — %s"), Moved, *FullText(To, true)), FV::Col(255, 200, 140), EFVIcon::Exclaim);
				Game->NotifyProfileChanged();
				return false;
			}
			FVUI::Sfx(EFVSfx::Error);
			Game->Toast(R == EFVBuyResult::NoRoom ? FullText(To) : FVUI::BuyErrorText(R), FV::Col(255, 150, 130), EFVIcon::Exclaim);
			return false;
		}
	};
}

TUniquePtr<FVScreen> FVMakeChestScreen(UFVGame* Game)
{
	return MakeUnique<FVStorageScreenImpl::FStorageScreen>(Game, FVStorageScreenImpl::EStorageKind::Chest);
}

TUniquePtr<FVScreen> FVMakeIceCellarScreen(UFVGame* Game)
{
	return MakeUnique<FVStorageScreenImpl::FStorageScreen>(Game, FVStorageScreenImpl::EStorageKind::IceCellar);
}
