// «Инвентарь»: two swipeable pages — Садок (fish) and Рюкзак (resources, materials, food, tackle, nets, junk) — plus
// the gear column (rods, the tackle of the rod in hand, bait, active food, shovel). Nothing is sold here: trade happens
// at the stalls; the cards show what Дед Митрич would pay. Context (UFVGame::GetScreenContext): "keepnet" (default) /
// "backpack" = the page the screen opens on; "fv_expand" (autotest) = keepnet with the first gear tile whose text does
// not fit already open (Р77: a tap opens a gear tile to its whole text).
// Update 3: the pages slide (FFVPager: swipe, tab buttons, ←/→, I / Tab / B); every list — the grids, the gear column
// and the selected stack's card — scrolls smoothly (FFVScrollView: finger / mouse drag, fling, wheel).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "InputCoreTypes.h"

namespace FVInventoryScreenImpl
{
	// Landscape layout (virtual px)
	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr float LeftX = 22.f;
	constexpr float LeftW = 288.f;
	constexpr float RightX = 326.f;
	constexpr float RightW = 290.f;
	constexpr int32 GridCols = 10;
	constexpr float SlotSize = 26.f;
	constexpr float SlotPitch = 28.f;
	constexpr int32 MaxSlots = 120;         // cells shown at most (the grids scroll; max keepnet 60 and backpack 36 today)
	constexpr int32 LandVisRows = 6;        // landscape grid viewport height in rows (10 x 6 cells, scrolls beyond)
	constexpr float GridPad = 2.f;          // room for the selection frame inside the clipped grid viewport
	constexpr float TabY = 40.f;
	constexpr float GridY = 62.f;
	constexpr float CapY = 232.f;
	constexpr float CardY = 246.f;
	constexpr float CardH = 42.f;
	constexpr float ButtonY = 292.f;
	constexpr int32 NumPages = 2;           // 0 = Садок, 1 = Рюкзак

	static const FName CtxBackpack(TEXT("backpack"));
	static const FName CtxExpand(TEXT("fv_expand"));

	static EFVContainer PageContainer(int32 Page) { return Page == 1 ? EFVContainer::Backpack : EFVContainer::Keepnet; }

	static const TArray<FString>& PageLabels()
	{
		static const TArray<FString> Labels = { TEXT("Садок"), TEXT("Рюкзак") };
		return Labels;
	}

	// ---- update 6 [SCREEN-TACKLE]: expert rods (§10.3) show their type and screen theme instead of the classic stats
	static FString ExpertThemeName(EFVExpertTheme T)
	{
		switch (T)
		{
		case EFVExpertTheme::Breeze: return TEXT("морское стекло");
		case EFVExpertTheme::Twins: return TEXT("серебро и синь");
		case EFVExpertTheme::Surf: return TEXT("тёмная бирюза");
		case EFVExpertTheme::Rhumb: return TEXT("латунь и карта");
		case EFVExpertTheme::Mirror: return TEXT("полированный хром");
		case EFVExpertTheme::Anchor: return TEXT("тёмное железо");
		case EFVExpertTheme::Helmsman: return TEXT("красное дерево и золото");
		case EFVExpertTheme::Pole: return TEXT("северное сияние");
		default: return TEXT("классика");
		}
	}

	/** bType: also «Тип «…»» (the tiles leave it out: their type plate shows it, 0.6.2 Р84). */
	static TArray<FString> ExpertRodParts(const FFVRodDef& R, bool bType = true)
	{
		TArray<FString> Parts;
		Parts.Add(TEXT("Экспертная"));
		if (bType && R.ExpertType < EFVExpertRodType::Count) { Parts.Add(FString::Printf(TEXT("Тип «%s»"), *FVDBU6::RodTypeName(R.ExpertType))); }
		Parts.Add(FString::Printf(TEXT("Экран: %s"), *ExpertThemeName(R.Theme)));
		return Parts;
	}

	static FString RodShortStats(const FFVRodDef& R)
	{
		if (R.bExpert) { return FString::Join(ExpertRodParts(R, false), TEXT(" · ")); }
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("Зона +%d"), FMath::RoundToInt(R.BarBonus)));
		Parts.Add(FString::Printf(TEXT("Вываж. ×%s"), *FVUI::Num(R.ReelMult)));
		Parts.Add(FString::Printf(TEXT("Заброс %s"), *FVUI::Percent(R.CastRange)));
		if (R.Luck > 0.f) { Parts.Add(FString::Printf(TEXT("Удача +%s"), *FVUI::Percent(R.Luck))); }
		return FString::Join(Parts, TEXT(" · "));
	}

	static FString RodFullStats(const FFVRodDef& R)
	{
		FString S = R.Description;
		if (!S.IsEmpty()) { S += TEXT("\n"); }
		if (R.bExpert)
		{
			S += FString::Join(ExpertRodParts(R), TEXT(" · "));
			if (R.ExpertType < EFVExpertRodType::Count) { S += TEXT("\n") + FVDBU6::RodTypeHowTo(R.ExpertType); }
			S += TEXT("\nЭкспертные снасти — леска, поплавок и катушка — ставятся только на экспертные удочки.");
			return S;
		}
		S += FString::Printf(TEXT("Зона поимки: +%d\n"), FMath::RoundToInt(R.BarBonus));
		S += FString::Printf(TEXT("Скорость вываживания: ×%s\n"), *FVUI::Num(R.ReelMult));
		S += FString::Printf(TEXT("Потеря при сходе: ×%s\n"), *FVUI::Num(R.DrainMult));
		S += FString::Printf(TEXT("Ожидание поклёвки: ×%s\n"), *FVUI::Num(R.BiteSpeed));
		S += FString::Printf(TEXT("Дальность заброса: %s"), *FVUI::Percent(R.CastRange));
		if (R.Luck > 0.f) { S += FString::Printf(TEXT("\nУдача (редкая рыба): +%s"), *FVUI::Percent(R.Luck)); }
		if (R.TreasureBonus > 0.f) { S += FString::Printf(TEXT("\nШанс сокровища: +%s"), *FVUI::Percent(R.TreasureBonus)); }
		return S;
	}

	/** 0.6.2 (Р84): the width FVUI::RodTypeChip will take (to reserve room before drawing it). */
	static float RodChipW(FVDraw& D, const FFVRodDef& R, float Size)
	{
		return FMath::RoundToFloat(D.MeasureText(FVDBU6::RodTypeLabel(R), Size, true).X + 8.f);
	}

	static TArray<FString> BaitStatParts(const FFVBaitDef& B)
	{
		TArray<FString> Parts;
		if (!FMath::IsNearlyEqual(B.BiteSpeed, 1.f)) { Parts.Add(FString::Printf(TEXT("Поклёвка ×%s"), *FVUI::Num(B.BiteSpeed))); }
		if (B.RareBonus > 0.f) { Parts.Add(FString::Printf(TEXT("Редкие +%s"), *FVUI::Percent(B.RareBonus))); }
		if (!FMath::IsNearlyEqual(B.JunkMult, 1.f)) { Parts.Add(FString::Printf(TEXT("Мусор ×%s"), *FVUI::Num(B.JunkMult))); }
		if (B.SizeBonus > 0.f) { Parts.Add(FString::Printf(TEXT("Размер +%s"), *FVUI::Percent(B.SizeBonus))); }
		return Parts;
	}

	static FString BaitShortStats(const FFVBaitDef& B)
	{
		const TArray<FString> Parts = BaitStatParts(B);
		return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" · ")) : FString(TEXT("Обычная наживка"));
	}

	/** Every rod stat as a short label (portrait rows show them all: phones have no hover tooltips). */
	static TArray<FString> RodStatParts(const FFVRodDef& R)
	{
		if (R.bExpert) { return ExpertRodParts(R, false); }
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("Зона +%d"), FMath::RoundToInt(R.BarBonus)));
		Parts.Add(FString::Printf(TEXT("Вываж. ×%s"), *FVUI::Num(R.ReelMult)));
		Parts.Add(FString::Printf(TEXT("Сход ×%s"), *FVUI::Num(R.DrainMult)));
		Parts.Add(FString::Printf(TEXT("Клёв ×%s"), *FVUI::Num(R.BiteSpeed)));
		Parts.Add(FString::Printf(TEXT("Заброс %s"), *FVUI::Percent(R.CastRange)));
		if (R.Luck > 0.f) { Parts.Add(FString::Printf(TEXT("Удача +%s"), *FVUI::Percent(R.Luck))); }
		if (R.TreasureBonus > 0.f) { Parts.Add(FString::Printf(TEXT("Сокровища +%s"), *FVUI::Percent(R.TreasureBonus))); }
		return Parts;
	}

	/** Name of an installed tackle item ("—" when the default простая леска / пробковый поплавок is on). */
	static FString TackleName(FName ItemId)
	{
		if (ItemId.IsNone()) { return TEXT("—"); }
		const FFVItemDef* It = FVDB::FindItem(ItemId);
		return It ? It->Name : ItemId.ToString();
	}

	/** Update 6: «Сила ×1,1 · Зона ×0,9…» of an expert effect (only the parts that differ from 1). */
	static FString ExpertStatsText(const FFVExpertStats& St)
	{
		TArray<FString> Parts;
		auto Add = [&Parts](const TCHAR* Name, float V) { if (!FMath::IsNearlyEqual(V, 1.f, 0.001f)) { Parts.Add(FString::Printf(TEXT("%s ×%s"), Name, *FVUI::Num(V))); } };
		Add(TEXT("Сила"), St.Force);
		Add(TEXT("Размер зоны"), St.ZoneSize);
		Add(TEXT("Прочность"), St.LineStrength);
		Add(TEXT("Устойчивость"), St.Stability);
		return FString::Join(Parts, TEXT(" · "));
	}

	/** Update 6: extra card lines for the new backpack items (seeds, expert tackle and reels, expert food). */
	static void AppendU6StackLines(const FFVInvStack& S, TArray<FString>& Lines)
	{
		if (const FFVSeedDef* Seed = FVDBU6::FindSeed(S.ItemId))
		{
			const FString Yield = Seed->YieldMin == Seed->YieldMax ? FString::Printf(TEXT("%d"), Seed->YieldMin) : FString::Printf(TEXT("%d–%d"), Seed->YieldMin, Seed->YieldMax);
			Lines.Add(FString::Printf(TEXT("Сажают на грядку огорода: %s ×%s за ~%d мин игры (в дождь быстрее)."),
				*FVDB::ItemName(Seed->CropId), *Yield, FMath::RoundToInt(Seed->GrowMinutes)));
		}
		const FFVItemDef* It = FVDB::FindItem(S.ItemId);
		if (!It) { return; }
		const bool bExpertTackle = FVDBU6::LineKindOf(S.ItemId) != EFVExpertLine::None || FVDBU6::FloatKindOf(S.ItemId) != EFVExpertFloat::None
			|| FVDBU6::ReelKindOf(S.ItemId) != EFVExpertReel::None || It->Kind == EFVItemKind::Reel;
		if (bExpertTackle)
		{
			Lines.Add(TEXT("Снасть для экспертной удочки: ставится в «Оснастке эксперта». На обычную удочку не ставится."));
			const FString St = ExpertStatsText(It->ExpertTackle);
			if (!St.IsEmpty()) { Lines.Add(St); }
		}
		if (It->Kind == EFVItemKind::Food)
		{
			const FString Buff = ExpertStatsText(It->ExpertBuff);
			if (!Buff.IsEmpty()) { Lines.Add(FString::Printf(TEXT("В экспертной рыбалке: %s"), *Buff)); }
		}
	}

	static bool IsFood(const FFVInvStack& S)
	{
		const FFVItemDef* It = FVDB::FindItem(S.ItemId);
		return It && It->Kind == EFVItemKind::Food;
	}

	// Portrait (phone) layout: tabs, then each page: capacity, the container grid, the selected stack's card and the
	// gear list; the actions and "← Назад" live in the bottom action bar (thumb zone).
	namespace Port
	{
		constexpr int32 GridCols = 6;
		constexpr float SlotGap = 3.f;
		constexpr float CardH = 62.f;         // selected stack details
		constexpr float RowGap = 3.f;
		constexpr float HintH = 32.f;         // "no rods" / "no bait" hint entries
		constexpr float TabH = 30.f;
		inline float SlotSize() { return FV::Ui::Slot(); }          // container cells (52)
		inline float RowH() { return FV::Ui::RowH(); }              // gear rows (40)
		inline float HeaderH() { return FMath::RoundToFloat(FV::Ui::Header() * 1.45f + 9.f); }   // gear section header
	}

	// ============================================================================================
	// Gear column entries (shared by both layouts; the list scrolls when it does not fit)
	// ============================================================================================
	enum class EGearKind : uint8 { Header, Rod, NoRods, Tackle, Bait, NoBaitHint, Food, Shovel };
	enum class EGearHeader : uint8 { Rods, Bait, Supplies };

	struct FGearEntry
	{
		EGearKind Kind = EGearKind::Rod;
		EGearHeader Header = EGearHeader::Rods;
		const FFVRodDef* Rod = nullptr;
		const FFVBaitDef* Bait = nullptr;    // Kind == Bait with Bait == nullptr: "fish without bait"
		int32 Count = 0;
	};

	// ---- Р77: a tap on a gear tile whose text does not fit opens it (the tile grows to its whole text, the rows below
	// move down); one open tile per list, a second tap (or a tap on another tile) closes it. The buttons of the tile
	// («Взять», «Настроить») are drawn first and keep their taps.

	static bool IsTileKind(EGearKind K)
	{
		return K == EGearKind::Rod || K == EGearKind::Bait || K == EGearKind::Tackle || K == EGearKind::Food || K == EGearKind::Shovel;
	}

	/** The open tile of one gear list: entry kind + id (rod / bait id; none for «без наживки» and the summary rows). */
	struct FGearExpand
	{
		bool bOn = false;
		EGearKind Kind = EGearKind::Rod;
		FName Id;
		bool bShow = false;      // scroll the open tile into view once the list is laid out with its new height
		bool bInstant = false;   // ... without the animation (the autotest opens it before the screenshot)
		bool bAuto = false;      // context "fv_expand": open the first tile whose text does not fit

		static FName KeyOf(const FGearEntry& E) { return E.Rod ? E.Rod->Id : (E.Bait ? E.Bait->Id : NAME_None); }
		bool Is(const FGearEntry& E) const { return bOn && E.Kind == Kind && KeyOf(E) == Id; }
		void Open(const FGearEntry& E) { bOn = true; Kind = E.Kind; Id = KeyOf(E); bShow = true; }
	};

	/** One tile pass: in — open / measure only (draw nothing); out — the height the open tile needs and whether the
	    collapsed tile cuts some of its text (then it shows the «▾» mark and a tap opens it). */
	struct FRowExp
	{
		bool bOpen = false;
		bool bMeasure = false;
		float Height = 0.f;
		bool bTrunc = false;
	};

	static float ExpBotPad(bool bPortrait) { return bPortrait ? 7.f : 5.f; }   // under the text of an open tile: the «▴» mark

	/** The «there is more» mark: a tiny ▾ (▴ on an open tile), right / bottom edges at EdgeX / Bottom. Drawn in pixels
	    (the UI font has no arrow glyphs). */
	static void ExpandMark(FVDraw& D, float EdgeX, float Bottom, bool bOpen, bool bPortrait)
	{
		const int32 Rows = bPortrait ? 4 : 3;   // 7 x 4 / 5 x 3 px
		const float W = float(Rows * 2 - 1);
		const float X0 = FMath::RoundToFloat(EdgeX - W);
		const float Y0 = FMath::RoundToFloat(Bottom - Rows);
		for (int32 k = 0; k < Rows; ++k)
		{
			const int32 Line = bOpen ? Rows - 1 - k : k;   // ▾: the wide line on top; ▴: at the bottom
			D.Rect(FFVRect(X0 + k, Y0 + Line, W - 2.f * k, 1.f), FVPal::InkSoft());
		}
	}

	/** All the parts on as many «a · b · c» lines as they need (FVUI::PackParts stops at two and cuts the rest). */
	static TArray<FString> PackAllLines(const FVDraw& D, const TArray<FString>& Parts, float MaxWidth, float Size)
	{
		TArray<FString> Lines;
		FString Cur;
		for (const FString& Part : Parts)
		{
			const FString Try = Cur.IsEmpty() ? Part : Cur + TEXT(" · ") + Part;
			if (Cur.IsEmpty() || D.MeasureText(Try, Size, false).X <= MaxWidth) { Cur = Try; }
			else
			{
				Lines.Add(Cur);
				Cur = Part;
			}
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
		return Lines;
	}

	/** True when a collapsed tile showing at most MaxLines of these packed lines (the last one cut) loses something. */
	static bool LinesCut(const FVDraw& D, const TArray<FString>& Lines, int32 MaxLines, float MaxWidth, float Size)
	{
		if (Lines.Num() > MaxLines) { return true; }
		for (const FString& L : Lines)
		{
			if (D.MeasureText(L, Size, false).X > MaxWidth) { return true; }
		}
		return false;
	}

	/** Text column of an open tile: paragraphs one under another, each wrapped to the column width with its own line
	    step (the collapsed tile's rhythm, so opening a tile only adds lines under the ones it already shows). */
	struct FGearText
	{
		struct FPara { FString S; float Size; bool bBold; FLinearColor C; float Step; };
		TArray<FPara> Paras;

		void Add(const FString& S, float Size, bool bBold, const FLinearColor& C, float Step)
		{
			if (!S.IsEmpty()) { Paras.Add({ S, Size, bBold, C, Step }); }
		}
		/** TextWrapped's line spacing that gives the paragraph's line step. */
		static float Spacing(const FVDraw& D, const FPara& Pa)
		{
			const float One = D.MeasureWrapped(TEXT("Щ"), 1000.f, Pa.Size, Pa.bBold, 1.f);
			return One > 0.f ? Pa.Step / One : 1.f;
		}
		float Measure(const FVDraw& D, float W) const
		{
			float H = 0.f;
			for (const FPara& Pa : Paras) { H += D.MeasureWrapped(Pa.S, W, Pa.Size, Pa.bBold, Spacing(D, Pa)); }
			return H;
		}
		void Draw(FVDraw& D, const FVector2D& Pos, float W) const
		{
			float Y = Pos.Y;
			for (const FPara& Pa : Paras) { Y += D.TextWrapped(Pa.S, FVector2D(Pos.X, Y), W, Pa.Size, Pa.C, Pa.bBold, Spacing(D, Pa)); }
		}
	};

	static const TCHAR* NoBaitText() { return TEXT("Наживки нет. Червей продают в лавке, а ещё их можно накопать лопатой — с наживкой клюёт быстрее."); }

	static float EntryHeight(const FGearEntry& E, bool bPortrait)
	{
		if (bPortrait)
		{
			switch (E.Kind)
			{
			case EGearKind::Header: return Port::HeaderH();
			case EGearKind::NoRods:
			case EGearKind::NoBaitHint: return Port::HintH;
			default: return Port::RowH() + Port::RowGap;
			}
		}
		switch (E.Kind)
		{
		case EGearKind::Header: return 19.f;
		case EGearKind::Rod: return 22.f;
		case EGearKind::NoRods: return 16.f;
		case EGearKind::Bait: return 19.f;
		case EGearKind::NoBaitHint: return 26.f;
		default: return 26.f;   // Tackle / Food / Shovel summary rows
		}
	}

	static void BuildGear(const UFVSaveGame& P, TArray<FGearEntry>& Out)
	{
		auto Add = [&Out](EGearKind Kind) -> FGearEntry& { FGearEntry& E = Out.AddDefaulted_GetRef(); E.Kind = Kind; return E; };
		Add(EGearKind::Header).Header = EGearHeader::Rods;
		int32 NumRods = 0;
		for (const FFVRodDef& Rod : FVDB::Rods())
		{
			if (!P.OwnsRod(Rod.Id)) { continue; }
			Add(EGearKind::Rod).Rod = &Rod;
			++NumRods;
		}
		if (NumRods == 0) { Add(EGearKind::NoRods); }
		Add(EGearKind::Tackle);

		Add(EGearKind::Header).Header = EGearHeader::Bait;
		Add(EGearKind::Bait);   // "Без наживки"
		int32 NumBaits = 0;
		for (const FFVBaitDef& B : FVDB::Baits())
		{
			const int32 Count = P.BaitCount(B.Id);
			if (Count <= 0) { continue; }
			FGearEntry& E = Add(EGearKind::Bait);
			E.Bait = &B;
			E.Count = Count;
			++NumBaits;
		}
		if (NumBaits == 0) { Add(EGearKind::NoBaitHint); }

		Add(EGearKind::Header).Header = EGearHeader::Supplies;
		Add(EGearKind::Food);
		Add(EGearKind::Shovel);
	}

	// ============================================================================================
	// The screen
	// ============================================================================================
	class FVInventoryScreen final : public FVScreen
	{
	public:
		explicit FVInventoryScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			TickCount = 0;
			const FName Ctx = Game ? Game->GetScreenContext() : NAME_None;
			Pager.NumPagesCached = NumPages;                 // before the first Update: SetPage clamps to it
			Pager.SetPage(Ctx == CtxBackpack ? 1 : 0);       // no slide when the screen opens
			Tab = PageContainer(Pager.Page);
			for (int32 i = 0; i < NumPages; ++i)
			{
				Sel[i] = FSel();
				GridScroll[i].Reset();
				GearScroll[i].Reset();
				CardScroll[i].Reset();
				CardKey[i] = 0;
				GearExp[i] = FGearExpand();
				GearExp[i].bAuto = Ctx == CtxExpand;
			}
			GearScrollLand.Reset();
			GearExpLand = FGearExpand();
			GearExpLand.bAuto = Ctx == CtxExpand;
		}

		/** The inventory owns its keys: I / Tab / B and ←/→ switch the pages; the other screen hotkeys and Esc are
		    handled here as well (the global toggles would close the screen). */
		virtual bool HandlesEscape() const override { return true; }

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal())
			{
				return;   // ignore the key press that opened the screen; modals own the keyboard
			}
			if (In.WasPressed(EKeys::Escape))
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;   // this screen is destroyed
			}
			if (In.WasPressed(EKeys::I) || In.WasPressed(EKeys::Tab) || In.WasPressed(EKeys::B))
			{
				Pager.GoTo(1 - Pager.Page);   // the other page (slides; the pager plays the click)
				return;
			}
			if (In.WasPressed(EKeys::Left)) { Pager.GoTo(Pager.Page - 1); }
			else if (In.WasPressed(EKeys::Right)) { Pager.GoTo(Pager.Page + 1); }

			// the other screens (same keys as in the world)
			EFVScreen Other = EFVScreen::None;
			if (In.WasPressed(EKeys::M)) { Other = EFVScreen::Map; }
			else if (In.WasPressed(EKeys::G)) { Other = EFVScreen::Wardrobe; }
			else if (In.WasPressed(EKeys::C)) { Other = EFVScreen::Collection; }
			else if (In.WasPressed(EKeys::J)) { Other = EFVScreen::Achievements; }
			if (Other != EFVScreen::None)
			{
				UFVGame* G = Game;
				G->OpenScreen(Other);
				return;   // this screen is destroyed
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bPortrait = FV::IsPortrait();
			const FFVRect Win = bPortrait ? FV::WindowRect() : WinRect;
			bool bClose = D.Window(Win, TEXT("Инвентарь"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Tab = PageContainer(Pager.Page);
			if (bPortrait)
			{
				// tabs + sliding pages + the bottom action bar (drawn last: it may change the profile)
				if (DrawPortrait(D, P, Win)) { bClose = true; }
			}
			else if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), 9.f, FVPal::InkSoft(), EFVAlign::Center);
			}
			else
			{
				// vertical separator
				D.Rect(FFVRect(RightX - 9.f, WinRect.Y + 10.f, 1.f, WinRect.H - 20.f), FVPal::ParchDark());
				D.Rect(FFVRect(RightX - 8.f, WinRect.Y + 10.f, 1.f, WinRect.H - 20.f), FV::Col(255, 236, 196));
				DrawLandscape(D, *P);
				Tab = PageContainer(Pager.Page);
				DrawGearList(D, *P, FFVRect(RightX, WinRect.Y + 10.f, RightW, WinRect.H - 18.f), false, GearScrollLand, GearExpLand);
				if (!FV::IsTouchUI())
				{
					(void)0; /* update 3: no key hints at the bottom (see the guide) */
				}
			}
			Tab = PageContainer(Pager.Page);   // between frames Tab is the active page (Tick, modal lambdas)

			if (bClose && Game)
			{
				UFVGame* G = Game;
				G->CloseScreen();   // last statement: the screen may be destroyed here
				return;
			}
		}

	private:
		/** Selected stack of a page: kept by identity (id, quality, flags), resolved to an index every frame. */
		struct FSel
		{
			FName Id;
			uint8 Q = 0;
			uint8 Flags = 0;
			bool bHas = false;
			int32 LastIndex = 0;     // keeps the selection near the same cell after a stack is gone
		};

		FFVPager Pager;                      // Pager.Page = the active page (0 keepnet, 1 backpack)
		EFVContainer Tab = EFVContainer::Keepnet;   // container of the page being drawn (= the active page between frames)
		FSel Sel[NumPages];
		FFVScrollView GridScroll[NumPages];  // per page: the container grid
		FFVScrollView GearScroll[NumPages];  // per page (portrait): the gear list under the card
		FFVScrollView CardScroll[NumPages];  // per page: the text column of the selected stack's card
		uint32 CardKey[NumPages] = { 0, 0 }; // the stack shown by the card (its scroll restarts when it changes)
		FFVScrollView GearScrollLand;        // landscape: the fixed gear column
		FGearExpand GearExp[NumPages];       // per page (portrait): the open gear tile
		FGearExpand GearExpLand;             // landscape: the open gear tile
		int32 TickCount = 0;

		int32 TabIndex() const { return Tab == EFVContainer::Backpack ? 1 : 0; }
		FSel& CurSel() { return Sel[TabIndex()]; }
		const FSel& CurSel() const { return Sel[TabIndex()]; }

		void Select(const FFVInvStack& S, int32 Index)
		{
			FSel& Cur = CurSel();
			Cur.bHas = true;
			Cur.Id = S.ItemId;
			Cur.Q = S.Quality;
			Cur.Flags = S.Flags;
			Cur.LastIndex = Index;
		}

		const TArray<FFVInvStack>& Stacks(const UFVSaveGame& P) const { return P.GetContainer(Tab); }

		int32 FindSelIndex(const UFVSaveGame& P) const
		{
			const FSel& Cur = CurSel();
			if (!Cur.bHas) { return INDEX_NONE; }
			const TArray<FFVInvStack>& A = Stacks(P);
			for (int32 i = 0; i < A.Num() && i < MaxSlots; ++i)
			{
				const FFVInvStack& S = A[i];
				if (S.ItemId == Cur.Id && S.Quality == Cur.Q && S.Flags == Cur.Flags && S.Count > 0) { return i; }
			}
			return INDEX_NONE;
		}

		/** Selected index; with bAuto (portrait, no hover) something stays selected whenever the container has stacks. */
		int32 ResolveSelection(const UFVSaveGame& P, bool bAuto)
		{
			const TArray<FFVInvStack>& A = Stacks(P);
			const int32 NumStacks = FMath::Min(A.Num(), MaxSlots);
			int32 Index = FindSelIndex(P);
			FSel& Cur = CurSel();
			if (Index == INDEX_NONE && bAuto && NumStacks > 0)
			{
				const int32 Auto = FMath::Clamp(Cur.LastIndex, 0, NumStacks - 1);
				if (A.IsValidIndex(Auto) && A[Auto].Count > 0)
				{
					Select(A[Auto], Auto);
					Index = Auto;
				}
			}
			if (Index == INDEX_NONE) { Cur.bHas = false; }
			else { Cur.LastIndex = Index; }
			return Index;
		}

		/** Cells of the grid: the stacks and the free places (keepnet: free units, backpack: free slots). */
		int32 NumCells(const UFVSaveGame& P) const
		{
			const int32 NumStacks = FMath::Min(Stacks(P).Num(), MaxSlots);
			const int32 Cap = FMath::Max(0, P.ContainerCapacity(Tab));
			const int32 Used = FMath::Max(0, P.ContainerUsed(Tab));
			const int32 Free = Tab == EFVContainer::Keepnet ? FMath::Max(0, Cap - Used) : FMath::Max(0, Cap - NumStacks);
			return FMath::Clamp(NumStacks + Free, 0, MaxSlots);
		}

		// ---------------------------------------------------------------- actions (shared by both layouts)

		void EquipRodAction(UFVSaveGame& P, const FFVRodDef& Rod)
		{
			if (P.EquipRod(Rod.Id))
			{
				FVUI::Sfx(EFVSfx::Equip);
				if (Game)
				{
					Game->Toast(FString::Printf(TEXT("В руках: %s"), *Rod.Name), FVPal::White(), EFVIcon::Rod);
					Game->GetScene().RefreshAppearance();
					Game->NotifyProfileChanged();
				}
			}
			else
			{
				FVUI::Sfx(EFVSfx::Error);
			}
		}

		void EquipBaitAction(UFVSaveGame& P, const FFVBaitDef* Bait)
		{
			if (P.EquipBait(Bait ? Bait->Id : NAME_None))
			{
				FVUI::Sfx(EFVSfx::Equip);
				if (Game)
				{
					Game->Toast(Bait ? FString::Printf(TEXT("Наживка: %s"), *Bait->Name) : FString(TEXT("Рыбачим без наживки")), FVPal::White(), EFVIcon::Bait);
					Game->NotifyProfileChanged();
				}
			}
			else
			{
				FVUI::Sfx(EFVSfx::Error);
			}
		}

		/** Index of the stack (Id, Q, Flags) in container C of the current profile (lambdas re-resolve after a modal). */
		static int32 FindStack(const UFVSaveGame& P, EFVContainer C, FName Id, uint8 Q, uint8 Flags)
		{
			const TArray<FFVInvStack>& A = P.GetContainer(C);
			for (int32 i = 0; i < A.Num(); ++i)
			{
				if (A[i].ItemId == Id && A[i].Quality == Q && A[i].Flags == Flags && A[i].Count > 0) { return i; }
			}
			return INDEX_NONE;
		}

		/** Keepnet «Выпустить» / backpack «Выбросить»: asks first, then removes Count units of the stack. */
		void ConfirmRemove(const FFVInvStack& S, int32 Count)
		{
			if (!Game || S.Count <= 0) { return; }
			const bool bRelease = Tab == EFVContainer::Keepnet;
			const EFVContainer C = Tab;
			const int32 N = FMath::Clamp(Count, 1, S.Count);
			const FString Name = FVUI::StackTitle(S);
			const int64 Worth = int64(FVUI::MitrichPrice(S)) * N;
			const FName Id = S.ItemId;
			const uint8 Q = S.Quality;
			const uint8 Flags = S.Flags;
			FString Body = N > 1 ? FString::Printf(TEXT("«%s» ×%d"), *Name, N) : FString::Printf(TEXT("«%s»"), *Name);
			Body += bRelease ? TEXT(" вернётся в воду.") : TEXT(" пропадёт навсегда.");
			if (Worth > 0) { Body += FString::Printf(TEXT(" У Митрича за это дали бы %s."), *FVUI::Coins(Worth)); }
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			Game->Confirm(bRelease ? TEXT("Выпустить рыбу?") : TEXT("Выбросить?"), Body,
				[WeakGame, C, Id, Q, Flags, N, Name, bRelease]()
				{
					UFVGame* G = WeakGame.Get();
					UFVSaveGame* Prof = G ? G->GetProfile() : nullptr;
					if (!Prof) { return; }
					const int32 Index = FindStack(*Prof, C, Id, Q, Flags);
					if (Index == INDEX_NONE) { return; }
					const int32 Take = FMath::Min(N, Prof->GetContainer(C)[Index].Count);
					if (Take <= 0 || !Prof->RemoveFrom(C, Index, Take))
					{
						FVUI::Sfx(EFVSfx::Error);
						return;
					}
					FVUI::Sfx(bRelease ? EFVSfx::Splash : EFVSfx::Close, 0.8f);
					const FString What = Take > 1 ? FString::Printf(TEXT("%s ×%d"), *Name, Take) : Name;
					G->Toast(bRelease ? FString::Printf(TEXT("Отпущено в воду: %s"), *What) : FString::Printf(TEXT("Выброшено: %s"), *What),
						FVPal::White(), bRelease ? EFVIcon::Fish : EFVIcon::Backpack);
					G->NotifyProfileChanged();
				},
				bRelease ? TEXT("Выпустить") : TEXT("Выбросить"), TEXT("Отмена"));
		}

		/** «Съесть»: eats one dish of the selected backpack stack (asks first when another dish is still active). */
		void EatAction(UFVSaveGame& P, const FFVInvStack& S)
		{
			if (!Game || !IsFood(S)) { return; }
			const FName Id = S.ItemId;
			const uint8 Q = S.Quality;
			const uint8 Flags = S.Flags;
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			auto Eat = [WeakGame, Id, Q, Flags]()
			{
				UFVGame* G = WeakGame.Get();
				UFVSaveGame* Prof = G ? G->GetProfile() : nullptr;
				if (!Prof) { return; }
				const int32 Index = FindStack(*Prof, EFVContainer::Backpack, Id, Q, Flags);
				const double Now = G->GetAbsMinutes();
				if (Index == INDEX_NONE || !Prof->EatFood(Index, Now))
				{
					FVUI::Sfx(EFVSfx::Error);
					G->Toast(TEXT("Не получилось поесть"), FV::Col(255, 150, 130), EFVIcon::Exclaim);
					return;
				}
				FVUI::Sfx(EFVSfx::Pickup);
				const FFVItemDef* Food = FVDB::FindItem(Id);
				const float Left = Prof->FoodMinutesLeft(Now);
				G->Toast(FString::Printf(TEXT("Съедено: %s — бонус на %s"), Food ? *Food->Name : *Id.ToString(), *UFVGame::DurationText(Left)),
					FV::Col(255, 228, 120), EFVIcon::Food);
				G->NotifyProfileChanged();
			};
			const double Now = Game->GetAbsMinutes();
			const FFVItemDef* Active = P.GetActiveFood(Now);
			if (Active)
			{
				const FFVItemDef* NewFood = FVDB::FindItem(Id);
				Game->Confirm(TEXT("Заменить угощение?"),
					FString::Printf(TEXT("Сейчас действует «%s» (ещё %s). Если съесть «%s», прежний бонус пропадёт."),
						*Active->Name, *UFVGame::DurationText(P.FoodMinutesLeft(Now)), NewFood ? *NewFood->Name : *Id.ToString()),
					Eat, TEXT("Съесть"), TEXT("Отмена"));
				return;
			}
			Eat();
		}

		/** The selected stack of the page being drawn, copied (the buttons read it BEFORE any change). */
		bool SelectedStack(const UFVSaveGame& P, FFVInvStack& Out) const
		{
			const int32 SelIndex = FindSelIndex(P);
			const TArray<FFVInvStack>& A = Stacks(P);
			if (SelIndex == INDEX_NONE || !A.IsValidIndex(SelIndex) || A[SelIndex].Count <= 0) { return false; }
			Out = A[SelIndex];
			return true;
		}

		/** [Выпустить 1][Выпустить ×N] (keepnet) / [Выбросить 1][Выбросить ×N] (backpack) in Row (portrait). */
		void DrawRemoveButtons(FVDraw& D, const UFVSaveGame& P, const FFVRect& Row)
		{
			FFVInvStack S;
			const bool bSel = SelectedStack(P, S);
			const float TS = FV::Ui::Body() + 0.5f;
			const FString Verb = Tab == EFVContainer::Keepnet ? TEXT("Выпустить") : TEXT("Выбросить");
			const FString StackLabel = S.Count > 1 ? FString::Printf(TEXT("%s ×%d"), *Verb, S.Count) : FString::Printf(TEXT("%s всё"), *Verb);
			const bool bOne = D.Button(FVDraw::SplitH(Row, 0, 2, 8.f), FString::Printf(TEXT("%s 1"), *Verb), EFVButton::Normal, bSel, TS);
			const bool bAll = D.Button(FVDraw::SplitH(Row, 1, 2, 8.f), StackLabel, EFVButton::Normal, bSel, TS);
			if (bSel && bOne) { ConfirmRemove(S, 1); }
			else if (bSel && bAll) { ConfirmRemove(S, S.Count); }
		}

		/** «Съесть» of the backpack page (portrait footer). */
		void DrawEatButton(FVDraw& D, UFVSaveGame& P, const FFVRect& R)
		{
			FFVInvStack S;
			const bool bFood = SelectedStack(P, S) && IsFood(S);
			if (D.Button(R, TEXT("Съесть"), bFood ? EFVButton::Primary : EFVButton::Normal, bFood, FV::Ui::Body() + 0.5f) && bFood)
			{
				EatAction(P, S);
			}
		}

		// ---------------------------------------------------------------- shared drawing

		void StackTooltip(FVDraw& D, const UFVSaveGame& P, const FFVInvStack& S)
		{
			FString Body = FVUI::StackKindText(S);
			for (const FString& L : FVUI::StackInfoLines(S, &P))
			{
				Body += TEXT("\n") + L;
			}
			const int32 Unit = FVUI::MitrichPrice(S);
			if (Unit > 0)
			{
				Body += FString::Printf(TEXT("\nУ Митрича: %s за шт."), *FVUI::Coins(Unit));
				if (S.Count > 1) { Body += FString::Printf(TEXT("\nВся стопка (%d шт.): %s"), S.Count, *FVUI::Coins(int64(Unit) * S.Count)); }
			}
			else
			{
				Body += TEXT("\nВ лавке это не покупают");
			}
			const FFVFishDef* F = FVDB::FindFish(S.ItemId);
			D.Tooltip(FVUI::StackTitle(S), Body, F ? FVDB::RarityColor(F->Rarity) : FVPal::White());
		}

		/** The active container's grid inside Area (a smooth-scrolling viewport; the first row starts GridPad below
		    Area.Y). Taps select (clicks fire on release and never after a drag). Returns the hovered stack index. */
		int32 DrawGrid(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, int32 Cols, float Slot, float Pitch, bool bBig, int32& InOutSel)
		{
			const TArray<FFVInvStack>& A = Stacks(P);
			const int32 NumStacks = FMath::Min(A.Num(), MaxSlots);
			const int32 Cells = NumCells(P);
			const int32 Rows = Cells > 0 ? FMath::DivideAndRoundUp(Cells, Cols) : 0;
			FFVScrollView& Scroll = GridScroll[TabIndex()];
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
					if (D.Slot(R, Index == InOutSel, true))
					{
						if (Index == InOutSel && !bBig)
						{
							CurSel().bHas = false;   // landscape: clicking the selected cell again deselects it
							InOutSel = INDEX_NONE;
						}
						else if (Index != InOutSel)
						{
							Select(S, Index);
							InOutSel = Index;
						}
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

		/** "Садок: 12 / 20 [bar]" and, at the right end, what Дед Митрич would pay for everything in the container. */
		void DrawCapacityRow(FVDraw& D, const UFVSaveGame& P, float X, float Y, float W, float Size, float BarH, const FString& Label)
		{
			const int32 Cap = FMath::Max(0, P.ContainerCapacity(Tab));
			const int32 Used = FMath::Max(0, P.ContainerUsed(Tab));
			const int64 Value = P.ContainerValue(Tab, FVDB::DefaultTraderId());
			const float MW = D.MoneyLabel(FVector2D(X + W, Y), Value, Size, EFVAlign::Right);
			const FString Who = TEXT("у Митрича");
			const float WhoW = D.MeasureText(Who, Size, false).X;
			D.Text(Who, FVector2D(X + W - MW - 4.f, Y), Size, FVPal::InkSoft(), EFVAlign::Right, false, false);
			FVUI::CapacityBar(D, Label, Used, Cap, FVector2D(X, Y), W - MW - WhoW - 14.f, Size, BarH);
		}

		/** Card of the selected stack: the icon, and a text column that scrolls (name, kind, Mitrich's price and every
		    detail line — the description, food bonuses, tackle stats, the record...). */
		void DrawStackCard(FVDraw& D, const UFVSaveGame& P, const FFVRect& Card, const FFVInvStack& S, bool bPortrait)
		{
			const int32 Pg = TabIndex();
			const uint32 Key = GetTypeHash(S.ItemId) * 31u + (uint32(S.Quality) | (uint32(S.Flags) << 8)) + 0x9E3779B9u;
			if (Key != CardKey[Pg])
			{
				CardKey[Pg] = Key;
				CardScroll[Pg].Reset();   // another stack: its text starts at the top
			}
			FFVScrollView& Scroll = CardScroll[Pg];

			const float NameS = bPortrait ? FV::Ui::Body() + 1.f : 9.f;
			const float SmallS = bPortrait ? FV::Ui::Small() : 7.f;
			const float InfoS = bPortrait ? FV::Ui::Small() : 6.5f;
			const EFVQuality Q = FVUI::QualityOf(S.Quality);
			const FFVRect IconBox(Card.X + 4.f, Card.Y + 4.f, bPortrait ? Card.H - 8.f : 40.f, Card.H - 8.f);
			D.Rect(IconBox, FV::Col(255, 236, 196, 150));
			FFVSprite Spr;
			if (FVUI::ItemIconOf(S.ItemId, Spr)) { FVUI::SpriteFit(D, Spr, IconBox.Inset(bPortrait ? 1.f : 2.f), FLinearColor::White, 2.f); }

			const float AX = IconBox.Right() + (bPortrait ? 4.f : 3.f);
			const FFVRect Area(AX, Card.Y + 2.f, Card.Right() - 3.f - AX, Card.H - 4.f);
			const float TX = Area.X + (bPortrait ? 4.f : 3.f);
			const float TW = Area.Right() - 6.f - TX;   // the scroll bar lives in the last px
			TArray<FString> Info = FVUI::StackInfoLines(S, &P);
			AppendU6StackLines(S, Info);   // update 6: seeds, expert tackle, expert food
			const float NameLH = FMath::RoundToFloat(D.LineHeight(NameS) + (bPortrait ? 1.f : 0.f));
			const float SmallLH = FMath::RoundToFloat(D.LineHeight(SmallS) + 1.f);
			float ContentH = 1.f + NameLH + 2.f * SmallLH + 2.f;
			for (const FString& L : Info) { ContentH += D.MeasureWrapped(L, TW, InfoS) + 1.f; }
			Scroll.Update(D, Area, ContentH);

			D.PushClip(Area);
			float Y = Area.Y + 1.f - Scroll.Offset();
			D.Text(D.Ellipsize(FVUI::StackTitle(S), TW, NameS), FVector2D(TX, Y), NameS, FVUI::StackInk(S), EFVAlign::Left, false, true);
			Y += NameLH;
			float QX = TX;
			if (Q != EFVQuality::Normal)
			{
				D.Stars(FVector2D(QX, FMath::RoundToFloat(Y + D.LineHeight(SmallS) * 0.5f - 4.f)), Q);
				QX += 10.f;
			}
			const FString Kind = FString::Printf(TEXT("%s · ×%d"), *FVUI::StackKindText(S), S.Count);
			if (bPortrait)
			{
				D.Text(D.Ellipsize(Kind, TX + TW - QX, SmallS, true), FVector2D(QX, Y), SmallS,
					FVDB::QualityColor(Q) * 0.6f + FVPal::InkSoft() * 0.4f, EFVAlign::Left, false, true);
			}
			else
			{
				D.Text(D.Ellipsize(Kind, TX + TW - QX, SmallS, false), FVector2D(QX, Y), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			Y += SmallLH;
			const int32 Unit = FVUI::MitrichPrice(S);
			if (Unit > 0) { FVUI::PriceLine(D, TEXT("У Митрича:"), Unit, S.Count, FVector2D(TX, Y), SmallS); }
			else { D.Text(TEXT("В лавке это не покупают"), FVector2D(TX, Y), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false); }
			Y += SmallLH + 2.f;
			for (const FString& L : Info)
			{
				Y += D.TextWrapped(L, FVector2D(TX, Y), TW, InfoS, FVPal::InkSoft()) + 1.f;
			}
			D.PopClip();
			Scroll.DrawBar(D, Area);
			Scroll.DrawFades(D, Area, FVPal::Parchment());
		}

		// ========================================================================================
		// Landscape: the tabs + the sliding page in the left column, the gear column on the right
		// ========================================================================================

		void DrawLandscape(FVDraw& D, UFVSaveGame& P)
		{
			// the rule under the tab row (the active folder tab merges with the page below it)
			D.Rect(FFVRect(LeftX, TabY + 16.f, LeftW, 1.f), FVPal::WoodDark());
			D.Rect(FFVRect(LeftX, TabY + 17.f, LeftW, 1.f), FV::Col(255, 236, 196));
			FVKit::TabStrip(D, FFVRect(LeftX, TabY, 2.f * 92.f + 2.f, 16.f), PageLabels(), Pager);
			const FFVRect PageArea(LeftX - 6.f, TabY + 18.f, LeftW + 12.f, WinRect.Bottom() - 5.f - (TabY + 18.f));
			Pager.Update(D, PageArea, NumPages);
			Pager.DrawPages(D, PageArea, [this, &D, &P](int32 Page)
			{
				Tab = PageContainer(Page);
				DrawPageLandscape(D, P);
			});
			Tab = PageContainer(Pager.Page);
		}

		void DrawPageLandscape(FVDraw& D, UFVSaveGame& P)
		{
			const bool bKeepnet = Tab == EFVContainer::Keepnet;
			const TArray<FFVInvStack>& A = Stacks(P);
			const int32 NumStacks = FMath::Min(A.Num(), MaxSlots);
			const int32 Cells = NumCells(P);
			const int32 Rows = Cells > 0 ? FMath::DivideAndRoundUp(Cells, GridCols) : 0;
			int32 SelIndex = ResolveSelection(P, false);
			const FFVRect GridArea(LeftX, GridY - GridPad, LeftW, LandVisRows * SlotPitch - (SlotPitch - SlotSize) + 2.f * GridPad);
			const int32 Hovered = DrawGrid(D, P, GridArea, GridCols, SlotSize, SlotPitch, false, SelIndex);

			const float BelowGrid = GridY + FMath::Min(Rows, LandVisRows) * SlotPitch + 4.f;
			if (NumStacks == 0)
			{
				const FString Msg = bKeepnet ? FString(TEXT("Садок пуст — самое время порыбачить!"))
					: FString(TEXT("Рюкзак пуст. Сюда попадают ресурсы, мусор, еда и снасти."));
				D.TextWrapped(Msg, FVector2D(LeftX + 4.f, FMath::Min(BelowGrid + 2.f, CapY - 20.f)), LeftW - 8.f, 7.5f, FVPal::InkSoft());
			}
			else if (BelowGrid < CapY - 12.f)
			{
				const FString Hint = bKeepnet ? FString(TEXT("Рыбу продают в лавках: у Митрича у пруда и на морском причале."))
					: FString(TEXT("Рюкзак можно расширить в лавке Митрича («Улучшения»)."));
				D.TextWrapped(Hint, FVector2D(LeftX + 4.f, BelowGrid), LeftW - 8.f, 6.5f, FV::Alpha(FVPal::InkSoft(), 0.85f));
			}
			if (Hovered != INDEX_NONE && A.IsValidIndex(Hovered) && !FV::IsTouchUI())
			{
				StackTooltip(D, P, A[Hovered]);
			}

			// capacity + what Mitrich would pay for the whole container
			DrawCapacityRow(D, P, LeftX, CapY, LeftW, 7.f, 8.f, bKeepnet ? TEXT("Садок:") : TEXT("Рюкзак, мест:"));

			// selected stack card
			const FFVRect Card(LeftX, CardY, LeftW, CardH);
			D.Panel(Card, EFVPanel::Parchment);
			const bool bValidSel = SelIndex != INDEX_NONE && A.IsValidIndex(SelIndex);
			if (bValidSel)
			{
				DrawStackCard(D, P, Card, A[SelIndex], false);
			}
			else
			{
				const FString Msg = NumStacks > 0
					? (bKeepnet ? FString(TEXT("Выберите рыбу: здесь появятся её цена у Митрича и рекорд. Лишнюю можно выпустить."))
						: FString(TEXT("Выберите вещь: еду можно съесть (бонус к рыбалке), ненужное — выбросить.")))
					: (bKeepnet ? FString(TEXT("Пойманная рыба попадает в садок. Продавайте улов в лавке у пруда."))
						: FString(TEXT("Собирайте ветки, ягоды и камешки у воды и во дворе — из них мастерят снасти.")));
				D.TextWrapped(Msg, FVector2D(Card.X + 8.f, Card.Y + 7.f), Card.W - 16.f, 7.f, FVPal::InkSoft());
			}

			// actions (read everything before the profile changes)
			const bool bCan = bValidSel && A[SelIndex].Count > 0;
			const FFVInvStack SelStack = bCan ? A[SelIndex] : FFVInvStack();
			const FString StackLabel = SelStack.Count > 1 ? FString::Printf(TEXT("%s стопку ×%d"), bKeepnet ? TEXT("Выпустить") : TEXT("Выбросить"), SelStack.Count)
				: FString::Printf(TEXT("%s стопку"), bKeepnet ? TEXT("Выпустить") : TEXT("Выбросить"));
			if (bKeepnet)
			{
				const bool bOne = D.Button(FFVRect(LeftX, ButtonY, 110.f, 20.f), TEXT("Выпустить 1"), EFVButton::Normal, bCan);
				const bool bAll = D.Button(FFVRect(LeftX + 116.f, ButtonY, LeftW - 116.f, 20.f), StackLabel, EFVButton::Normal, bCan);
				if (bCan && bOne) { ConfirmRemove(SelStack, 1); }
				else if (bCan && bAll) { ConfirmRemove(SelStack, SelStack.Count); }
			}
			else
			{
				const bool bFood = bCan && IsFood(SelStack);
				const bool bEat = D.Button(FFVRect(LeftX, ButtonY, 76.f, 20.f), TEXT("Съесть"), bFood ? EFVButton::Primary : EFVButton::Normal, bFood);
				const bool bOne = D.Button(FFVRect(LeftX + 82.f, ButtonY, 92.f, 20.f), TEXT("Выбросить 1"), EFVButton::Normal, bCan);
				const bool bAll = D.Button(FFVRect(LeftX + 180.f, ButtonY, LeftW - 180.f, 20.f), StackLabel, EFVButton::Normal, bCan);
				if (bFood && bEat) { EatAction(P, SelStack); }
				else if (bCan && bOne) { ConfirmRemove(SelStack, 1); }
				else if (bCan && bAll) { ConfirmRemove(SelStack, SelStack.Count); }
			}
			const FString Foot = bKeepnet ? FString(TEXT("Продать улов можно только в лавке — подойдите к прилавку."))
				: FString(TEXT("Снасти ставят в мастерской, сети — на берегу, еда даёт бонус к рыбалке."));
			// hotfix 061: the whole hint — a second line when it is too long (there are ~26 px under the buttons)
			const bool bFootWraps = D.MeasureText(Foot, 6.5f).X > LeftW;
			D.TextWrapped(Foot, FVector2D(LeftX, ButtonY + (bFootWraps ? 23.f : 25.f)), LeftW, 6.5f, FV::Alpha(FVPal::InkSoft(), 0.9f), false, bFootWraps ? 1.f : 1.1f);
		}

		// ---------------------------------------------------------------- gear column (both layouts)

		/** «Взять» / «В руках» of a rod or bait tile: at the right, as tall as the collapsed tile (BaseH; an open tile
		    keeps it at its top). */
		static FFVRect GearBtnRect(const FFVRect& Row, float BaseH, bool bPortrait)
		{
			const float BtnW = bPortrait ? 74.f : 58.f;
			const float Pad = bPortrait ? 4.f : 2.f;
			return FFVRect(Row.Right() - BtnW - (bPortrait ? 4.f : 3.f), Row.Y + Pad, BtnW, BaseH - Pad * 2.f);
		}

		/** Background + the equipped mark / "Взять" button of a rod or bait row. Returns true when "Взять" was tapped
		    (clicks fire on release and never after a drag, so scrolling the list never takes a rod). */
		bool GearRowFrame(FVDraw& D, const FFVRect& Row, float BaseH, bool bEq, bool bPortrait, FFVRect& OutBtn)
		{
			const bool bHover = D.Hover(Row);
			D.Rect(Row, bEq ? FV::Col(206, 232, 150) : (bHover ? FV::Col(255, 236, 196) : FV::Col(252, 226, 176)));
			D.Frame(Row, bEq ? FV::Col(96, 150, 56) : FV::Col(214, 156, 94), 1.f);
			OutBtn = GearBtnRect(Row, BaseH, bPortrait);
			if (bEq)
			{
				const float LS = bPortrait ? FV::Ui::Small() + 0.5f : 7.f;
				const FString Label = TEXT("В руках");
				const float LW = D.MeasureText(Label, LS, true).X;
				const float IconS = bPortrait ? 16.f : 12.f;
				const float LX = OutBtn.X + (OutBtn.W - LW - IconS) * 0.5f;
				if (const FFVSprite* Check = FVUI::Icon(EFVIcon::Check))
				{
					FVUI::SpriteFit(D, *Check, FFVRect(LX - 2.f, OutBtn.Y + (OutBtn.H - IconS) * 0.5f, IconS, IconS), FLinearColor::White, 1.f);
				}
				D.Text(Label, FVector2D(LX + IconS, OutBtn.Y + FMath::RoundToFloat((OutBtn.H - D.LineHeight(LS)) * 0.5f)), LS, FV::Col(60, 120, 40), EFVAlign::Left, false, true);
				return false;
			}
			return D.Button(OutBtn, TEXT("Взять"), EFVButton::Normal, true, bPortrait ? FV::Ui::Body() : 7.f);
		}

		/** Rod tile (Row.H = its height now, BaseH = collapsed). Open (X.bOpen) it shows its whole text: the name, every
		    stat, the description (expert rods: how the type is handled). X gets the open height and whether the collapsed
		    tile cuts text; with X.bMeasure nothing is drawn. */
		void RodRow(FVDraw& D, UFVSaveGame& P, const FFVRodDef& Rod, const FFVRect& Row, float BaseH, bool bEq, bool bPortrait, FRowExp& X)
		{
			const float IconSz = BaseH - 4.f;
			const FFVRect IconBox(Row.X + 3.f, Row.Y + 2.f, IconSz, IconSz);
			const float TX = IconBox.Right() + (bPortrait ? 5.f : 4.f);
			const float TW = GearBtnRect(Row, BaseH, bPortrait).X - 6.f - TX;
			const float NameS = bPortrait ? FV::Ui::Body() : 8.f;
			const float StatS = bPortrait ? FV::Ui::Small() : 6.f;
			const float TopY = bPortrait ? 2.f : 0.f;
			const float NameStep = bPortrait ? FMath::RoundToFloat(D.LineHeight(NameS)) : 10.f;
			const float StatLH = FMath::RoundToFloat(D.LineHeight(StatS, false));
			const TArray<FString> StatLines = PackAllLines(D, RodStatParts(Rod), TW, StatS);
			const FString HowTo = Rod.bExpert && Rod.ExpertType < EFVExpertRodType::Count ? FVDBU6::RodTypeHowTo(Rod.ExpertType) : FString();
			const FLinearColor DescC = FV::Alpha(FVPal::InkSoft(), 0.85f);
			// 0.6.2 (Р84): the type plate sits at the right end of the name line; the name keeps the rest
			const float ChipS = bPortrait ? 7.f : 6.f;
			const float ChipW = RodChipW(D, Rod, ChipS);
			const float NW = FMath::Max(20.f, TW - ChipW - 4.f);
			FGearText NameT;
			NameT.Add(Rod.Name, NameS, true, FVPal::Ink(), NameStep);
			FGearText Full;
			Full.Add(FString::Join(StatLines, TEXT("\n")), StatS, false, FVPal::InkSoft(), StatLH);
			Full.Add(Rod.Description, StatS, false, DescC, StatLH);
			Full.Add(HowTo, StatS, false, DescC, StatLH);
			// collapsed: the name on one line + two stat lines (portrait) / the short stats (landscape leaves out «Сход»,
			// «Клёв»...); the description never
			const int32 ShownStat = FMath::Clamp(FMath::FloorToInt((BaseH + 1.f - TopY - NameStep) / FMath::Max(1.f, StatLH)), 0, 2);
			const bool bStatsCut = bPortrait ? LinesCut(D, StatLines, ShownStat, TW, StatS)
				: (!Rod.bExpert || D.MeasureText(RodShortStats(Rod), StatS, false).X > TW);
			X.bTrunc = bStatsCut || !Rod.Description.IsEmpty() || !HowTo.IsEmpty() || D.MeasureText(Rod.Name, NameS, true).X > NW;
			const float NameH = NameT.Measure(D, NW);
			X.Height = FMath::Max(BaseH, FMath::RoundToFloat(TopY + NameH + Full.Measure(D, TW) + ExpBotPad(bPortrait)));
			if (X.bMeasure) { return; }

			FFVRect Btn;
			const bool bTake = GearRowFrame(D, Row, BaseH, bEq, bPortrait, Btn);
			if (UFVSprites* Sp = UFVSprites::Get())
			{
				const FFVSprite Icon = Sp->RodIcon(Rod);
				FVUI::SpriteFit(D, Icon, IconBox, FLinearColor::White, bPortrait ? 2.f : 1.f);
			}
			FVUI::RodTypeChip(D, Rod, FVector2D(TX + TW - ChipW, Row.Y + TopY + (bPortrait ? 1.f : 0.5f)), ChipS);
			if (X.bOpen)
			{
				NameT.Draw(D, FVector2D(TX, Row.Y + TopY), NW);
				Full.Draw(D, FVector2D(TX, Row.Y + TopY + NameH), TW);
			}
			else if (bPortrait)
			{
				D.Text(D.Ellipsize(Rod.Name, NW, NameS), FVector2D(TX, Row.Y + TopY), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
				const float StatY = Row.Y + TopY + NameStep;
				const TArray<FString> Lines = FVUI::PackParts(D, RodStatParts(Rod), TW, StatS);
				for (int32 k = 0; k < Lines.Num() && k < 2; ++k)
				{
					if (StatY + (k + 1) * StatLH > Row.Bottom() + 1.f) { break; }
					D.Text(Lines[k], FVector2D(TX, StatY + k * StatLH), StatS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
			}
			else
			{
				D.Text(D.Ellipsize(Rod.Name, NW, NameS), FVector2D(TX, Row.Y), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(D.Ellipsize(RodShortStats(Rod), TW, StatS, false), FVector2D(TX, Row.Y + NameStep), StatS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				if (D.Hover(Row) && !Btn.Contains(D.Mouse()) && !FV::IsTouchUI())
				{
					const FFVRodLoadout L = P.GetLoadout(Rod.Id);
					D.Tooltip(Rod.Name, RodFullStats(Rod) + FString::Printf(TEXT("\nОснастка: леска %s, поплавок %s"), *TackleName(L.Line), *TackleName(L.Float)));
				}
			}
			if (X.bTrunc || X.bOpen) { ExpandMark(D, Btn.X - 1.f, Row.Bottom() - 2.f, X.bOpen, bPortrait); }
			if (bTake) { EquipRodAction(P, Rod); }
		}

		/** Bait tile (Bait == nullptr: «без наживки»); open it shows the whole name, stats and description. */
		void BaitRow(FVDraw& D, UFVSaveGame& P, const FFVBaitDef* Bait, int32 Count, const FFVRect& Row, float BaseH, bool bEq, bool bPortrait, FRowExp& X)
		{
			const float IconSz = BaseH - (bPortrait ? 4.f : 2.f);
			const FFVRect IconBox(Row.X + (bPortrait ? 3.f : 2.f), Row.Y + (bPortrait ? 2.f : 1.f), IconSz, IconSz);
			const float TX = IconBox.Right() + (bPortrait ? 5.f : 4.f);
			const float TW = GearBtnRect(Row, BaseH, bPortrait).X - 6.f - TX;
			const FString Name = Bait ? FString::Printf(TEXT("%s  ×%d"), *Bait->Name, Count) : FString(TEXT("Без наживки"));
			const FString Stats = Bait ? BaitShortStats(*Bait) : FString(TEXT("Клюёт медленнее, зато бесплатно"));
			const FString Desc = Bait ? Bait->Description : FString();
			const float NameS = bPortrait ? FV::Ui::Body() : 7.5f;
			const float StatS = bPortrait ? FV::Ui::Small() : 6.f;
			const float TopY = bPortrait ? 2.f : 0.f;
			const float NameStep = bPortrait ? FMath::RoundToFloat(D.LineHeight(NameS)) : 9.f;
			const float StatLH = FMath::RoundToFloat(D.LineHeight(StatS, false));
			TArray<FString> Parts = Bait ? BaitStatParts(*Bait) : TArray<FString>();
			if (Parts.Num() == 0) { Parts.Add(Stats); }
			const TArray<FString> StatLines = PackAllLines(D, Parts, TW, StatS);
			const FLinearColor DescC = FV::Alpha(FVPal::InkSoft(), 0.85f);
			FGearText Full;
			Full.Add(Name, NameS, true, FVPal::Ink(), NameStep);
			Full.Add(FString::Join(StatLines, TEXT("\n")), StatS, false, FVPal::InkSoft(), StatLH);
			Full.Add(Desc, StatS, false, DescC, StatLH);
			// collapsed: one line each for the name, the stats and (portrait, when there is room) the description
			const bool bDescShown = bPortrait && TopY + NameStep + 2.f * StatLH <= BaseH + 1.f;
			X.bTrunc = D.MeasureText(Name, NameS, true).X > TW || LinesCut(D, StatLines, 1, TW, StatS)
				|| (!Desc.IsEmpty() && (!bDescShown || D.MeasureText(Desc, StatS, false).X > TW));
			X.Height = FMath::Max(BaseH, FMath::RoundToFloat(TopY + Full.Measure(D, TW) + ExpBotPad(bPortrait)));
			if (X.bMeasure) { return; }

			FFVRect Btn;
			const bool bTake = GearRowFrame(D, Row, BaseH, bEq, bPortrait, Btn);
			FFVSprite Icon;
			if (Bait && FVUI::ItemIconOf(Bait->Id, Icon))
			{
				FVUI::SpriteFit(D, Icon, IconBox, FLinearColor::White, bPortrait ? 2.f : 1.f);
			}
			else
			{
				// "no bait": crossed circle
				const float Rr = bPortrait ? 9.f : 5.f;
				D.Ring(IconBox.Center(), Rr, Rr, FVPal::InkSoft(), bPortrait ? 1.5f : 1.f);
				D.Line(IconBox.Center() + FVector2D(-Rr * 0.7f, Rr * 0.7f), IconBox.Center() + FVector2D(Rr * 0.7f, -Rr * 0.7f), FVPal::InkSoft(), bPortrait ? 1.5f : 1.f);
			}
			if (X.bOpen)
			{
				Full.Draw(D, FVector2D(TX, Row.Y + TopY), TW);
			}
			else if (bPortrait)
			{
				D.Text(D.Ellipsize(Name, TW, NameS), FVector2D(TX, Row.Y + TopY), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
				const float L1 = Row.Y + TopY + NameStep;
				D.Text(D.Ellipsize(Stats, TW, StatS, false), FVector2D(TX, L1), StatS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				if (bDescShown && !Desc.IsEmpty())
				{
					D.Text(D.Ellipsize(Desc, TW, StatS, false), FVector2D(TX, L1 + StatLH), StatS, DescC, EFVAlign::Left, false, false);
				}
			}
			else
			{
				D.Text(D.Ellipsize(Name, TW, NameS), FVector2D(TX, Row.Y), NameS, FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(D.Ellipsize(Stats, TW, StatS, false), FVector2D(TX, Row.Y + NameStep), StatS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				if (Bait && D.Hover(Row) && !Btn.Contains(D.Mouse()) && !FV::IsTouchUI())
				{
					FString Body = Bait->Description;
					if (!Body.IsEmpty()) { Body += TEXT("\n"); }
					Body += BaitShortStats(*Bait);
					Body += FString::Printf(TEXT("\nОсталось: %d шт. (1 шт. тратится на поклёвку)"), Count);
					D.Tooltip(Bait->Name, Body);
				}
			}
			if (X.bTrunc || X.bOpen) { ExpandMark(D, Btn.X - 1.f, Row.Bottom() - 2.f, X.bOpen, bPortrait); }
			if (bTake) { EquipBaitAction(P, Bait); }
		}

		/** Summary row (tackle / food / shovel): icon, a title line and a detail line; tooltip with all lines. Open it
		    shows the whole title and every detail part (BaseH = the collapsed height, X as in RodRow). */
		void InfoRow(FVDraw& D, const FFVRect& Row, float BaseH, const FFVSprite* Icon, const FString& Title, const FLinearColor& TitleC,
			const TArray<FString>& SubParts, const FLinearColor& SubC, const FString& TipTitle, const FString& TipBody, bool bPortrait, bool bActive, FRowExp& X)
		{
			const float IconSz = BaseH - (bPortrait ? 6.f : 4.f);
			const FFVRect IconBox(Row.X + 3.f, Row.Y + (BaseH - IconSz) * 0.5f, IconSz, IconSz);
			const float TX = IconBox.Right() + (bPortrait ? 6.f : 4.f);
			const float TW = Row.Right() - 4.f - TX;
			const float TS = bPortrait ? FV::Ui::Body() : 7.5f;
			const float SS = bPortrait ? FV::Ui::Small() : 6.f;
			const float TopY = bPortrait ? 3.f : 1.f;
			const float TitleStep = FMath::RoundToFloat(D.LineHeight(TS)) + (bPortrait ? 1.f : 0.f);
			const float LH = FMath::RoundToFloat(D.LineHeight(SS, false));
			const TArray<FString> SubLines = PackAllLines(D, SubParts, TW, SS);
			FGearText Full;
			Full.Add(Title, TS, true, TitleC, TitleStep);
			Full.Add(FString::Join(SubLines, TEXT("\n")), SS, false, SubC, LH);
			// collapsed: the title on one line + up to two detail lines (portrait) / one (landscape)
			const int32 ShownSub = bPortrait ? FMath::Clamp(FMath::FloorToInt((BaseH + 1.f - TopY - TitleStep) / FMath::Max(1.f, LH)), 0, 2) : 1;
			X.bTrunc = D.MeasureText(Title, TS, true).X > TW || LinesCut(D, SubLines, ShownSub, TW, SS);
			X.Height = FMath::Max(BaseH, FMath::RoundToFloat(TopY + Full.Measure(D, TW) + ExpBotPad(bPortrait)));
			if (X.bMeasure) { return; }

			D.Rect(Row, bActive ? FV::Col(255, 240, 206) : FV::Col(246, 222, 176));
			D.Frame(Row, bActive ? FV::Col(96, 150, 56) : FV::Col(214, 156, 94), 1.f);
			if (Icon && Icon->IsValid())
			{
				FVUI::SpriteFit(D, *Icon, IconBox, bActive ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.55f), bPortrait ? 2.f : 1.f);
			}
			if (X.bOpen)
			{
				Full.Draw(D, FVector2D(TX, Row.Y + TopY), TW);
			}
			else
			{
				const float MarkRoom = X.bTrunc ? (bPortrait ? 10.f : 8.f) : 0.f;   // the detail lines leave the «▾» its corner
				const float TitleY = Row.Y + TopY;
				D.Text(D.Ellipsize(Title, TW, TS), FVector2D(TX, TitleY), TS, TitleC, EFVAlign::Left, false, true);
				const float SubY = TitleY + TitleStep;
				if (bPortrait)
				{
					const TArray<FString> Lines = FVUI::PackParts(D, SubParts, TW - MarkRoom, SS);
					for (int32 k = 0; k < Lines.Num() && SubY + (k + 1) * LH <= Row.Bottom() + 1.f; ++k)
					{
						D.Text(Lines[k], FVector2D(TX, SubY + k * LH), SS, SubC, EFVAlign::Left, false, false);
					}
				}
				else
				{
					D.Text(D.Ellipsize(FString::Join(SubParts, TEXT(" · ")), TW - MarkRoom, SS, false), FVector2D(TX, SubY), SS, SubC, EFVAlign::Left, false, false);
					if (D.Hover(Row) && !FV::IsTouchUI() && !TipBody.IsEmpty())
					{
						D.Tooltip(TipTitle, TipBody);
					}
				}
			}
			if (X.bTrunc || X.bOpen) { ExpandMark(D, Row.Right() - 4.f, Row.Bottom() - 2.f, X.bOpen, bPortrait); }
		}

		/** Update 6: the equipped expert rod — its expert tackle and characteristics; «Настроить» opens the tackle screen. */
		void ExpertTackleRow(FVDraw& D, const UFVSaveGame& P, const FFVRodDef& Rod, const FFVRect& Row, float BaseH, bool bPortrait, FRowExp& X)
		{
			const FFVExpertLoadout L = P.GetExpertLoadout(Rod.Id);
			const FFVExpertStats St = P.GetExpertStats(Rod.Id, Game ? Game->GetAbsMinutes() : 0.0);
			const bool bAny = !L.Line.IsNone() || !L.Float.IsNone() || !L.Reel.IsNone();
			FFVSprite Icon;
			bool bIcon = false;
			if (!L.Float.IsNone()) { bIcon = FVUI::ItemIconOf(L.Float, Icon); }
			else if (!L.Line.IsNone()) { bIcon = FVUI::ItemIconOf(L.Line, Icon); }
			else if (!L.Reel.IsNone()) { bIcon = FVUI::ItemIconOf(L.Reel, Icon); }
			else if (const FFVSprite* Fl = FVUI::Icon(EFVIcon::Float)) { Icon = *Fl; bIcon = true; }
			const float BtnW = bPortrait ? 92.f : 60.f;
			const FFVRect Btn(Row.Right() - BtnW - 3.f, Row.Y + 3.f, BtnW, BaseH - 6.f);   // stays at the top of an open tile
			const FFVRect Info(Row.X, Row.Y, Row.W - BtnW - 6.f, Row.H);
			const FString Title = FString::Printf(TEXT("Леска: %s · Поплавок: %s · Катушка: %s"), *TackleName(L.Line), *TackleName(L.Float), *TackleName(L.Reel));
			TArray<FString> Sub;
			Sub.Add(FString::Printf(TEXT("Сила ×%s"), *FVUI::Num(St.Force)));
			Sub.Add(FString::Printf(TEXT("Зона ×%s"), *FVUI::Num(St.ZoneSize)));
			Sub.Add(FString::Printf(TEXT("Прочность ×%s"), *FVUI::Num(St.LineStrength)));
			Sub.Add(FString::Printf(TEXT("Устойчивость ×%s"), *FVUI::Num(St.Stability)));
			const FString Tip = FString::Printf(TEXT("Экспертная оснастка удочки «%s»\nЛеска: %s\nПоплавок: %s\nКатушка: %s\nНажмите «Настроить», чтобы сменить снасти или применить сохранённую конфигурацию."),
				*Rod.Name, *TackleName(L.Line), *TackleName(L.Float), *TackleName(L.Reel));
			InfoRow(D, Info, BaseH, bIcon ? &Icon : nullptr, Title, FVPal::Ink(), Sub, bAny ? FV::Col(52, 116, 34) : FVPal::InkSoft(),
				TEXT("Оснастка"), Tip, bPortrait, bAny, X);
			if (X.bMeasure) { return; }
			if (D.Button(Btn, TEXT("Настроить"), EFVButton::Normal, true, bPortrait ? FV::Ui::Body() : 7.f) && Game)
			{
				Game->OpenScreenWith(EFVScreen::ExpertTackle, Rod.Id);
			}
		}

		void TackleRow(FVDraw& D, const UFVSaveGame& P, const FFVRect& Row, float BaseH, bool bPortrait, FRowExp& X)
		{
			const FFVRodDef& Rod = P.GetRod();
			if (Rod.bExpert) { ExpertTackleRow(D, P, Rod, Row, BaseH, bPortrait, X); return; }
			const FFVRodLoadout L = P.GetLoadout(Rod.Id);
			const TArray<FString> Mods = FVDB::StatModsLines(P.GetTackleMods(Rod.Id));
			const bool bAny = !L.Line.IsNone() || !L.Float.IsNone();
			FFVSprite Icon;
			bool bIcon = false;
			if (!L.Float.IsNone()) { bIcon = FVUI::ItemIconOf(L.Float, Icon); }
			else if (!L.Line.IsNone()) { bIcon = FVUI::ItemIconOf(L.Line, Icon); }
			else if (const FFVSprite* Fl = FVUI::Icon(EFVIcon::Float)) { Icon = *Fl; bIcon = true; }
			const FString Title = FString::Printf(TEXT("Леска: %s · Поплавок: %s"), *TackleName(L.Line), *TackleName(L.Float));
			const TArray<FString> Sub = bAny ? (Mods.Num() > 0 ? Mods : TArray<FString>{ TEXT("Без бонусов") })
				: TArray<FString>{ TEXT("Простая оснастка — поставьте леску и поплавок в мастерской") };
			FString Tip = FString::Printf(TEXT("Оснастка удочки «%s»\nЛеска: %s\nПоплавок: %s"), *Rod.Name, *TackleName(L.Line), *TackleName(L.Float));
			for (const FString& M : Mods) { Tip += TEXT("\n") + M; }
			Tip += TEXT("\nЛеску и поплавок меняют в мастерской дома (вкладка «Оснастка»).");
			InfoRow(D, Row, BaseH, bIcon ? &Icon : nullptr, Title, FVPal::Ink(), Sub, bAny ? FV::Col(52, 116, 34) : FVPal::InkSoft(),
				TEXT("Оснастка"), Tip, bPortrait, bAny, X);
		}

		void FoodRow(FVDraw& D, const UFVSaveGame& P, const FFVRect& Row, float BaseH, bool bPortrait, FRowExp& X)
		{
			const double Now = Game ? Game->GetAbsMinutes() : 0.0;
			const FFVItemDef* Food = P.GetActiveFood(Now);
			if (!Food)
			{
				const FFVSprite* Pot = FVUI::Icon(EFVIcon::Food);
				InfoRow(D, Row, BaseH, Pot, TEXT("Еда: нет бонуса"), FVPal::InkSoft(), { TEXT("Приготовьте блюдо на кухне дома"), TEXT("съешьте его из рюкзака") }, FVPal::InkSoft(),
					TEXT("Еда"), TEXT("Блюда дают бонусы к рыбалке на несколько часов. Действует одно блюдо за раз."), bPortrait, false, X);
				return;
			}
			const EFVQuality Q = FVUI::QualityOf(P.ActiveFoodQuality);
			const TArray<FString> Lines = FVDB::StatModsLines(Food->Buff.Scaled(FVDB::FoodPower(Q)));
			const FString Left = UFVGame::DurationText(P.FoodMinutesLeft(Now));
			FFVSprite Icon;
			const bool bIcon = FVUI::ItemIconOf(Food->Id, Icon);
			FString Tip = FString::Printf(TEXT("%s · %s\nОсталось: %s"), *Food->Name, *FVDB::QualityName(Q), *Left);
			for (const FString& L : Lines) { Tip += TEXT("\n") + L; }
			InfoRow(D, Row, BaseH, bIcon ? &Icon : nullptr, FString::Printf(TEXT("%s · ещё %s"), *Food->Name, *Left), FVPal::Ink(),
				Lines.Num() > 0 ? Lines : TArray<FString>{ TEXT("Сытость") }, FV::Col(52, 116, 34), TEXT("Еда"), Tip, bPortrait, true, X);
		}

		void ShovelRow(FVDraw& D, const UFVSaveGame& P, const FFVRect& Row, float BaseH, bool bPortrait, FRowExp& X)
		{
			const TArray<FFVShovelDef>& Sh = FVDB::Shovels();
			const int32 Level = P.ShovelLevel;
			if (Level <= 0 || !Sh.IsValidIndex(Level - 1))
			{
				InfoRow(D, Row, BaseH, FVUI::Icon(EFVIcon::Shovel), TEXT("Лопаты нет"), FVPal::InkSoft(), { TEXT("Лопату продаёт Митрич"), TEXT("копать червей на грядках") }, FVPal::InkSoft(),
					TEXT("Лопата"), TEXT("С лопатой можно копать червей на мягкой земле у воды и на грядках во дворе (раз в день на каждом месте)."), bPortrait, false, X);
				return;
			}
			FFVSprite Icon;
			bool bIcon = false;
			if (UFVSprites* Sp = UFVSprites::Get()) { Icon = Sp->ShovelIcon(Level); bIcon = Icon.IsValid(); }
			const TArray<FString> Sub = { FString::Printf(TEXT("Уровень %d из %d"), Level, Sh.Num()), FString::Printf(TEXT("на раскопки %s с"), *FVUI::Num(P.DigSeconds(), 1)) };
			FString Tip = FString::Printf(TEXT("%s\n%s"), *Sh[Level - 1].Name, *FString::Join(Sub, TEXT(" · ")));
			if (Sh.IsValidIndex(Level)) { Tip += FString::Printf(TEXT("\nСледующая: %s"), *Sh[Level].Name); }
			InfoRow(D, Row, BaseH, bIcon ? &Icon : nullptr, Sh[Level - 1].Name, FVPal::Ink(), Sub, FVPal::InkSoft(), TEXT("Лопата"), Tip, bPortrait, true, X);
		}

		/** Draws (with X.bMeasure only measures) the tile of a gear entry: Row.H = its height now, BaseH = collapsed. */
		void GearRow(FVDraw& D, UFVSaveGame& P, const FGearEntry& E, const FFVRect& Row, float BaseH, bool bPortrait, FRowExp& X)
		{
			switch (E.Kind)
			{
			case EGearKind::Rod:
				if (E.Rod) { RodRow(D, P, *E.Rod, Row, BaseH, E.Rod->Id == P.GetRod().Id, bPortrait, X); }
				break;
			case EGearKind::Bait:
			{
				const FFVBaitDef* CurBait = P.GetBait();
				const bool bEq = E.Bait ? (CurBait && CurBait->Id == E.Bait->Id) : (CurBait == nullptr);
				BaitRow(D, P, E.Bait, E.Count, Row, BaseH, bEq, bPortrait, X);
				break;
			}
			case EGearKind::Tackle: TackleRow(D, P, Row, BaseH, bPortrait, X); break;
			case EGearKind::Food: FoodRow(D, P, Row, BaseH, bPortrait, X); break;
			case EGearKind::Shovel: ShovelRow(D, P, Row, BaseH, bPortrait, X); break;
			default: break;
			}
		}

		/** Tops and heights of the gear entries for tiles RowW wide (the open tile measured with its whole text); returns
		    the list height. Forgets an open tile that is gone (e.g. the last worm of a bait was used). */
		float LayoutGear(FVDraw& D, UFVSaveGame& P, const TArray<FGearEntry>& Gear, float X, float RowW, bool bPortrait, FGearExpand& Exp,
			TArray<float>& Offsets, TArray<float>& Heights)
		{
			const float RowGap = bPortrait ? Port::RowGap : 2.f;
			Offsets.Reset(Gear.Num());
			Heights.Reset(Gear.Num());
			bool bFound = false;
			float ListH = 0.f;
			for (const FGearEntry& E : Gear)
			{
				float H = EntryHeight(E, bPortrait);
				if (E.Kind == EGearKind::NoBaitHint)
				{
					H = FMath::Max(H, FMath::RoundToFloat(D.MeasureWrapped(NoBaitText(), RowW - 8.f, bPortrait ? FV::Ui::Small() : 6.5f) + 4.f));
				}
				else if (Exp.Is(E))
				{
					bFound = true;
					FRowExp M;
					M.bOpen = true;
					M.bMeasure = true;
					GearRow(D, P, E, FFVRect(X, 0.f, RowW, H - RowGap), H - RowGap, bPortrait, M);
					H = M.Height + RowGap;
				}
				Offsets.Add(ListH);
				Heights.Add(H);
				ListH += H;
			}
			if (!bFound) { Exp.bOn = false; }
			return ListH;
		}

		/** Rods, the tackle of the rod in hand, bait, food & shovel (portrait: then the full stats of the gear in hand)
		    in a smooth-scrolling viewport (finger / mouse drag, fling, wheel). A tap on a tile whose text does not fit
		    opens it to the whole text (Exp = the open tile of this list). */
		void DrawGearList(FVDraw& D, UFVSaveGame& P, const FFVRect& Area, bool bPortrait, FFVScrollView& Scroll, FGearExpand& Exp)
		{
			if (Area.H < 24.f) { return; }
			TArray<FGearEntry> Gear;
			BuildGear(P, Gear);
			if (Gear.Num() == 0) { return; }
			const float BarRoom = bPortrait ? 7.f : 6.f;
			const float RowGap = bPortrait ? Port::RowGap : 2.f;
			constexpr float InHandGap = 6.f;

			// context "fv_expand" (autotest): the first tile whose text does not fit starts open
			if (Exp.bAuto)
			{
				Exp.bAuto = false;
				for (const FGearEntry& E : Gear)
				{
					if (!IsTileKind(E.Kind)) { continue; }
					const float BaseH = EntryHeight(E, bPortrait) - RowGap;
					FRowExp M;
					M.bMeasure = true;
					GearRow(D, P, E, FFVRect(Area.X, 0.f, Area.W - BarRoom, BaseH), BaseH, bPortrait, M);
					if (M.bTrunc)
					{
						Exp.Open(E);
						Exp.bInstant = true;
						break;
					}
				}
			}

			TArray<float> Offsets;
			TArray<float> Heights;
			float ListH = LayoutGear(D, P, Gear, Area.X, Area.W, bPortrait, Exp, Offsets, Heights);

			// Portrait: the full stats of the gear in hand close the list (desktop shows them in tooltips).
			float InHandH = bPortrait ? InHandPanel(D, P, FFVRect(Area.X, 0.f, Area.W, 0.f), false) : 0.f;
			const bool bScroll = ListH + (InHandH > 0.f ? InHandGap + InHandH : 0.f) > Area.H + 0.5f;
			const float RowW = bScroll ? Area.W - BarRoom : Area.W;
			if (bScroll)
			{
				ListH = LayoutGear(D, P, Gear, Area.X, RowW, bPortrait, Exp, Offsets, Heights);   // narrower: the open tile may grow
				if (bPortrait) { InHandH = InHandPanel(D, P, FFVRect(Area.X, 0.f, RowW, 0.f), false); }
			}
			Scroll.Update(D, Area, ListH + (InHandH > 0.f ? InHandGap + InHandH : 0.f));
			if (Exp.bShow)
			{
				// a tile opened last frame: keep it in view (its top first when it is taller than the list)
				for (int32 i = 0; i < Gear.Num(); ++i)
				{
					if (Exp.Is(Gear[i]))
					{
						Scroll.ScrollToShow(Offsets[i], FMath::Min(Heights[i], Area.H), Exp.bInstant);
						break;
					}
				}
				Exp.bShow = false;
				Exp.bInstant = false;
			}

			int32 NumOwnedRods = 0;
			for (const FGearEntry& E : Gear) { if (E.Kind == EGearKind::Rod) { ++NumOwnedRods; } }
			const float HeadS = bPortrait ? FV::Ui::Header() : 10.f;
			const float SmallS = bPortrait ? FV::Ui::Small() : 6.5f;
			const float BodyS = bPortrait ? FV::Ui::Body() : 7.5f;
			const float HeadRightY = FMath::RoundToFloat((D.LineHeight(HeadS) - D.LineHeight(SmallS)) * 0.5f) + 1.f;
			const float Off = Scroll.Offset();

			D.PushClip(Area);
			for (int32 i = 0; i < Gear.Num(); ++i)
			{
				const FGearEntry& E = Gear[i];
				const float H = Heights[i];
				const float Y = Area.Y - Off + Offsets[i];
				if (Y + H <= Area.Y) { continue; }
				if (Y >= Area.Bottom()) { break; }
				switch (E.Kind)
				{
				case EGearKind::Header:
				{
					const TCHAR* Title = E.Header == EGearHeader::Rods ? TEXT("Удочки") : (E.Header == EGearHeader::Bait ? TEXT("Наживка") : TEXT("Припасы"));
					FVUI::SectionHeader(D, Title, FVector2D(Area.X, Y + 1.f), RowW, HeadS);
					FString Right;
					if (E.Header == EGearHeader::Rods) { Right = FString::Printf(TEXT("%d из %d"), NumOwnedRods, FVDB::Rods().Num()); }
					else if (E.Header == EGearHeader::Bait) { Right = TEXT("1 шт. тратится на поклёвку"); }
					else { Right = TEXT("еда и лопата"); }
					D.Text(Right, FVector2D(Area.X + RowW, Y + HeadRightY), SmallS, FVPal::InkSoft(), EFVAlign::Right, false, false);
					break;
				}
				case EGearKind::NoRods:
					D.Text(TEXT("Нет удочек"), FVector2D(Area.X + 4.f, Y + (bPortrait ? 8.f : 2.f)), BodyS, FVPal::InkSoft());
					break;
				case EGearKind::NoBaitHint:
					D.TextWrapped(NoBaitText(), FVector2D(Area.X + 4.f, Y + 2.f), RowW - 8.f, SmallS, FVPal::InkSoft());
					break;
				default:
				{
					const bool bOpen = Exp.Is(E);
					const FFVRect Row(Area.X, Y, RowW, H - RowGap);
					FRowExp X;
					X.bOpen = bOpen;
					GearRow(D, P, E, Row, EntryHeight(E, bPortrait) - RowGap, bPortrait, X);
					// a tap on the tile itself (its buttons took their taps above): open it / close it / close the open one
					if ((X.bTrunc || Exp.bOn) && D.Clicked(Row))
					{
						if (!bOpen && X.bTrunc) { Exp.Open(E); }
						else { Exp.bOn = false; }
						FVUI::Sfx(EFVSfx::Click, 0.7f);
					}
					break;
				}
				}
			}
			if (InHandH > 0.f)
			{
				const float Y = Area.Y - Off + ListH + InHandGap;
				if (Y < Area.Bottom() && Y + InHandH > Area.Y)
				{
					InHandPanel(D, P, FFVRect(Area.X, Y, RowW, InHandH), true);
				}
			}
			D.PopClip();
			Scroll.DrawBar(D, Area);
			Scroll.DrawFades(D, Area, FVPal::Parchment());
		}

		// ========================================================================================
		// Portrait (phones): tabs, sliding pages stacked vertically (no hover needed), actions in the bottom bar
		// ========================================================================================

		/** Returns true for "← Назад". */
		bool DrawPortrait(FVDraw& D, UFVSaveGame* P, const FFVRect& Win)
		{
			const float X = Win.X + 8.f;
			const float W = Win.W - 16.f;
			const float Top = Win.Y + 16.f;
			const FFVRect Bar = FVDraw::ActionBarRect(Win);

			// ---- tabs (animated indicator follows the slide)
			const FFVRect TabRow(X + 4.f, Top, W - 8.f, Port::TabH);
			FVKit::TabStrip(D, TabRow, PageLabels(), Pager);

			// ---- the pages: everything between the tabs and the footer divider (the backpack's extra button row
			// belongs to its page and slides with it)
			const float PageTop = TabRow.Bottom() + 7.f;
			const FFVRect PageArea(Win.X + 5.f, PageTop, Win.W - 10.f, FMath::Max(10.f, Bar.Y - 8.f - PageTop));
			if (P)
			{
				Pager.Update(D, PageArea, NumPages);
				Pager.DrawPages(D, PageArea, [this, &D, P, &Win, PageTop](int32 Page)
				{
					Tab = PageContainer(Page);
					DrawPagePortrait(D, *P, Win, PageTop + 1.f);
				});
				Tab = PageContainer(Pager.Page);
			}
			else
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
			}

			// ---- footer: "← Назад" stays put, the page's own actions slide along with the page
			FFVRect Actions;
			const bool bBack = D.ActionBar(Win, &Actions);
			if (P)
			{
				const FFVRect Slide(Actions.X, Actions.Y - 2.f, Actions.W, Actions.H + 4.f);   // + the buttons' shadow
				Pager.DrawPages(D, Slide, [this, &D, P, &Actions](int32 Page)
				{
					Tab = PageContainer(Page);
					if (Tab == EFVContainer::Keepnet) { DrawRemoveButtons(D, *P, Actions); }
					else { DrawEatButton(D, *P, Actions); }
				});
				Tab = PageContainer(Pager.Page);
			}
			return bBack;
		}

		/** One page (portrait): capacity, the grid, the selected stack's card, the gear list; the backpack page adds
		    [Выбросить 1][Выбросить ×N] above the footer (keepnet: those buttons are in the footer itself). */
		void DrawPagePortrait(FVDraw& D, UFVSaveGame& P, const FFVRect& Win, float Top)
		{
			const float X = Win.X + 8.f;
			const float W = Win.W - 16.f;
			const bool bKeepnet = Tab == EFVContainer::Keepnet;
			const float Bottom = (bKeepnet ? FVDraw::ActionBarRect(Win).Y : FVUI::ActionBarExtraRowRect(Win).Y) - 10.f;
			const float SmallS = FV::Ui::Small();
			float Y = Top;

			// ---- capacity + what Mitrich would pay for everything on this page
			DrawCapacityRow(D, P, X, Y, W, SmallS, 10.f, bKeepnet ? TEXT("Занято:") : TEXT("Мест:"));
			Y += FMath::RoundToFloat(FMath::Max(D.LineHeight(SmallS), 10.f) + 6.f);

			// ---- how many grid rows are visible: the card and a few gear rows must stay visible below
			const int32 Cells = NumCells(P);
			const TArray<FFVInvStack>& A = Stacks(P);
			const int32 NumStacks = FMath::Min(A.Num(), MaxSlots);
			int32 SelIndex = ResolveSelection(P, true);
			const float SlotSz = Port::SlotSize();
			const float Pitch = SlotSz + Port::SlotGap;
			const int32 TotalRows = FMath::Max(1, FMath::DivideAndRoundUp(Cells, Port::GridCols));
			const int32 MaxRows = FV::VH >= 860.f ? 4 : (FV::VH >= 720.f ? 3 : 2);
			const float BelowGrid = 6.f + Port::CardH + 8.f + Port::HeaderH() + 1.f * (Port::RowH() + Port::RowGap);
			const int32 FitRows = FMath::FloorToInt((Bottom - Y - BelowGrid + Port::SlotGap) / Pitch);
			const int32 VisRows = FMath::Clamp(FMath::Min(TotalRows, FMath::Min(MaxRows, FitRows)), 1, TotalRows);

			// ---- grid (smooth-scrolling viewport)
			const FFVRect GridArea(X, Y - GridPad, W, VisRows * Pitch - Port::SlotGap + 2.f * GridPad);
			DrawGrid(D, P, GridArea, Port::GridCols, SlotSz, Pitch, true, SelIndex);
			Y = GridArea.Bottom() - GridPad + 6.f;

			// ---- selected stack (always visible); its buttons are in the footer
			const FFVRect Card(X, Y, W, Port::CardH);
			D.Panel(Card, EFVPanel::Parchment);
			if (SelIndex != INDEX_NONE && A.IsValidIndex(SelIndex))
			{
				DrawStackCard(D, P, Card, A[SelIndex], true);
			}
			else
			{
				const FFVRect IconBox(Card.X + 4.f, Card.Y + 4.f, Card.H - 8.f, Card.H - 8.f);
				D.Rect(IconBox, FV::Col(255, 236, 196, 150));
				if (const FFVSprite* Ico = FVUI::Icon(bKeepnet ? EFVIcon::Keepnet : EFVIcon::Backpack))
				{
					FVUI::SpriteFit(D, *Ico, IconBox.Inset(4.f), FLinearColor(1.f, 1.f, 1.f, 0.8f), 2.f);
				}
				const float TX = IconBox.Right() + 8.f;
				const float TW = Card.Right() - 8.f - TX;
				const FString Msg = NumStacks > 0 ? FString(TEXT("Коснитесь предмета — здесь появятся его цена у Митрича и подробности."))
					: (bKeepnet ? FString(TEXT("Садок пуст — самое время порыбачить! Улов продают в лавке у пруда."))
						: FString(TEXT("Рюкзак пуст. Сюда попадают ресурсы, мусор, еда и снасти.")));
				float MS = SmallS;
				float MH = D.MeasureWrapped(Msg, TW, MS);
				if (MH > Card.H - 6.f) { MS = SmallS - 0.5f; MH = D.MeasureWrapped(Msg, TW, MS); }
				D.TextWrapped(Msg, FVector2D(TX, FMath::RoundToFloat(Card.Y + FMath::Max(3.f, (Card.H - MH) * 0.5f))), TW, MS, FVPal::InkSoft());
			}
			Y = Card.Bottom() + 8.f;

			// ---- rods, tackle, bait, food, shovel, the gear in hand (smooth-scrolling)
			DrawGearList(D, P, FFVRect(X, Y, W, FMath::Max(0.f, Bottom - Y)), true, GearScroll[TabIndex()], GearExp[TabIndex()]);

			// ---- backpack: [Выбросить 1][Выбросить ×N] right above the footer
			if (!bKeepnet)
			{
				const FFVRect Row = FVUI::ActionBarExtraRow(D, Win);
				DrawRemoveButtons(D, P, Row);
			}
		}

		/** The full stats of the gear in hand (portrait). Returns the panel height for width R.W (0 = too narrow);
		    draws it at R when bDraw. */
		float InHandPanel(FVDraw& D, const UFVSaveGame& P, const FFVRect& R, bool bDraw)
		{
			const FFVRodDef& Rod = P.GetRod();
			const FFVRodStats St = P.GetRodStats();   // rod + installed tackle
			const FFVBaitDef* Bait = P.GetBait();
			const float BodyS = FV::Ui::Body();
			const float SmallS = FV::Ui::Small();
			const float Pad = 7.f;
			const float TW = R.W - Pad * 2.f;
			if (TW < 120.f) { return 0.f; }
			const float TitleLH = D.LineHeight(BodyS);
			const float DescH = Rod.Description.IsEmpty() ? 0.f : D.MeasureWrapped(Rod.Description, TW, SmallS, false, 1.05f) + 3.f;
			struct FStat { FString Label; FString Value; };
			TArray<FStat> Stats;
			Stats.Add({ TEXT("Зона поимки"), FString::Printf(TEXT("+%d"), FMath::RoundToInt(St.BarBonus)) });
			Stats.Add({ TEXT("Вываживание"), FString::Printf(TEXT("×%s"), *FVUI::Num(St.ReelMult)) });
			Stats.Add({ TEXT("Потеря при сходе"), FString::Printf(TEXT("×%s"), *FVUI::Num(St.DrainMult)) });
			Stats.Add({ TEXT("Ожидание поклёвки"), FString::Printf(TEXT("×%s"), *FVUI::Num(St.BiteSpeed)) });
			Stats.Add({ TEXT("Дальность заброса"), FVUI::Percent(St.CastRange) });
			Stats.Add({ TEXT("Удача"), FString::Printf(TEXT("+%s"), *FVUI::Percent(St.Luck)) });
			Stats.Add({ TEXT("Шанс сокровища"), FString::Printf(TEXT("+%s"), *FVUI::Percent(St.TreasureBonus)) });
			if (St.ReactBonus > 0.f) { Stats.Add({ TEXT("Реакция"), FString::Printf(TEXT("+%s с"), *FVUI::Num(St.ReactBonus)) }); }
			const float StatRowH = FMath::RoundToFloat(D.LineHeight(SmallS) + 1.f);
			const int32 StatRows = FMath::DivideAndRoundUp(Stats.Num(), 2);
			const FString BaitText = Bait
				? FString::Printf(TEXT("Наживка: %s ×%d · %s"), *Bait->Name, P.BaitCount(Bait->Id), *BaitShortStats(*Bait))
				: FString(TEXT("Наживка: нет — клюёт медленнее"));
			// Р77: the bait line wraps instead of being cut (one line as before when it fits)
			const bool bBaitOneLine = D.MeasureText(BaitText, SmallS, false).X <= TW;
			const float BaitH = bBaitOneLine ? StatRowH : FMath::RoundToFloat(D.MeasureWrapped(BaitText, TW, SmallS, false, 1.05f)) + 1.f;
			const float PanelH = FMath::RoundToFloat(Pad + TitleLH + 3.f + DescH + StatRows * StatRowH + 5.f + BaitH + Pad - 2.f);
			if (!bDraw) { return PanelH; }

			const FFVRect PR(R.X, R.Y, R.W, PanelH);
			D.Panel(PR, EFVPanel::Parchment);
			const float X = PR.X + Pad;
			float Y = PR.Y + Pad - 1.f;
			const FString Title = TEXT("В руках: ");
			D.Text(Title, FVector2D(X, Y), BodyS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			const float NX = X + D.MeasureText(Title, BodyS, true).X;
			// 0.6.2 (Р84): the type plate right after the rod name
			const float ChipW = RodChipW(D, Rod, SmallS);
			const FString RodName = D.Ellipsize(Rod.Name, FMath::Max(20.f, X + TW - NX - ChipW - 5.f), BodyS);
			D.Text(RodName, FVector2D(NX, Y), BodyS, FVPal::Ink(), EFVAlign::Left, false, true);
			FVUI::RodTypeChip(D, Rod, FVector2D(NX + D.MeasureText(RodName, BodyS, true).X + 5.f, Y + 1.f), SmallS);
			Y += TitleLH + 3.f;
			if (DescH > 0.f)
			{
				D.TextWrapped(Rod.Description, FVector2D(X, Y), TW, SmallS, FVPal::InkSoft(), false, 1.05f);
				Y += DescH;
			}
			const float ColW = FMath::FloorToFloat((TW - 12.f) * 0.5f);
			for (int32 i = 0; i < Stats.Num(); ++i)
			{
				const float CX = X + (i % 2) * (ColW + 12.f);
				const float CY = Y + (i / 2) * StatRowH;
				const float ValW = D.MeasureText(Stats[i].Value, SmallS, true).X;
				D.Text(D.Ellipsize(Stats[i].Label, ColW - ValW - 4.f, SmallS, false), FVector2D(CX, CY), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				D.Text(Stats[i].Value, FVector2D(CX + ColW, CY), SmallS, FVPal::Ink(), EFVAlign::Right, false, true);
			}
			Y += StatRows * StatRowH + 5.f;
			D.Divider(FVector2D(X, Y - 3.f), TW);
			if (bBaitOneLine) { D.Text(BaitText, FVector2D(X, Y), SmallS, FVPal::Ink(), EFVAlign::Left, false, false); }
			else { D.TextWrapped(BaitText, FVector2D(X, Y), TW, SmallS, FVPal::Ink(), false, 1.05f); }
			return PanelH;
		}
	};
}

TUniquePtr<FVScreen> FVMakeInventoryScreen(UFVGame* Game)
{
	return MakeUnique<FVInventoryScreenImpl::FVInventoryScreen>(Game);
}
