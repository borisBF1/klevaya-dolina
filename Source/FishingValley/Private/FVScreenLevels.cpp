// FVScreenLevels.cpp — «Уровни рыбака» (update 4, EFVScreen::Levels, opened from the HUD level badge).
//   A banner on top: the level medallion, the XP bar, how much XP is left to the next level and a small teaser of the
//   next level's first unlock. Below, a smoothly scrolling list of level cards: the current level first («Сейчас»),
//   then every previous level down to the very start. Each card lists FVDB::UnlocksAtLevel(L) + HomeUnlocksAtLevel(L)
//   with a picture where one exists (fish, rods, baits, locations, stations, recipes, goods, decor, clothes).
//   Landscape: two columns of unlocks per card. Portrait: one column, finger-sized rows, «Назад» in the action bar.
//   Keys: ↑/↓ scroll, Home/End jump, Esc closes (UFVGame).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "InputCoreTypes.h"

namespace FVLevelsScreenImpl
{
	// =================================================================================================================
	// pictures for the unlock lines
	// =================================================================================================================
	enum class EArt : uint8 { None, Icon, Item, Fish, Station, Cosmetic, Location, CatchBar };

	struct FUnlock
	{
		FString Text;
		EArt Art = EArt::None;
		EFVIcon Icon = EFVIcon::Check;
		FName Id;                        // item / fish / cosmetic / location id
		EFVStation Station = EFVStation::Kitchen;
		int32 StationLevel = 1;
		int32 CheckRows = 0;             // update 5: a legend's preparation checklist rows under the text (live, see DrawCard)
	};

	struct FLevelBlock
	{
		int32 Level = 1;
		TArray<FUnlock> Items;
	};

	inline FLinearColor InkGood() { return FV::Col(46, 132, 34); }
	inline FLinearColor InkInfo() { return FV::Col(36, 100, 176); }
	inline FLinearColor InkGoldText() { return FV::Col(168, 104, 12); }
	inline FLinearColor CardEdge() { return FV::Col(176, 122, 76); }

	static FUnlock MakeIcon(const FString& Text, EFVIcon I)
	{
		FUnlock U;
		U.Text = Text;
		U.Art = EArt::Icon;
		U.Icon = I;
		return U;
	}

	static FUnlock MakeArt(const FString& Text, EArt Art, FName Id)
	{
		FUnlock U;
		U.Text = Text;
		U.Art = Art;
		U.Id = Id;
		return U;
	}

	/** Finds the picture for one Russian unlock line of level L (the formats of FVDB::UnlocksAtLevel / HomeUnlocksAtLevel). */
	static FUnlock Resolve(const FString& Line, int32 L)
	{
		if (Line.StartsWith(TEXT("Полоса поимки")))
		{
			FUnlock U;
			U.Text = Line;
			U.Art = EArt::CatchBar;
			return U;
		}
		if (Line.StartsWith(TEXT("Новая локация: ")))
		{
			for (const FFVLocationDef& Loc : FVDB::Locations())
			{
				if (Line.StartsWith(FString::Printf(TEXT("Новая локация: %s ("), *Loc.Name))) { return MakeArt(Line, EArt::Location, Loc.Id); }
			}
			return MakeIcon(Line, EFVIcon::Map);
		}
		for (const FFVRodDef& R : FVDB::Rods())
		{
			// 0.6.2: «В лавке: Удочка «…» (Тип)» or «Удочка «…» (Тип): где взять» (FVDB::UnlocksAtLevel)
			if (R.MinLevel == L && (Line.StartsWith(FString::Printf(TEXT("В лавке: %s ("), *R.Name)) || Line.StartsWith(FString::Printf(TEXT("%s ("), *R.Name))))
			{
				return MakeArt(Line, EArt::Item, R.Id);
			}
		}
		if (Line.StartsWith(TEXT("Новая наживка: ")))
		{
			for (const FFVBaitDef& B : FVDB::Baits())
			{
				if (Line == FString::Printf(TEXT("Новая наживка: %s"), *B.Name)) { return MakeArt(Line, EArt::Item, B.Id); }
			}
			return MakeIcon(Line, EFVIcon::Bait);
		}
		if (Line.StartsWith(TEXT("Теперь клюёт: ")))
		{
			for (const FFVFishDef& F : FVDB::Fish())
			{
				if (F.MinLevel == L && Line.StartsWith(FString::Printf(TEXT("Теперь клюёт: %s ("), *F.Name))) { return MakeArt(Line, EArt::Fish, F.Id); }
			}
			return MakeIcon(Line, EFVIcon::Fish);
		}
		if (Line.StartsWith(TEXT("Проснулась легенда: ")))
		{
			const FString Where = Line.RightChop(FCString::Strlen(TEXT("Проснулась легенда: ")));
			for (const FFVFishDef& F : FVDB::Fish())
			{
				const FFVLocationDef* Loc = FVDB::FindLocation(F.Location);
				if (F.bLegendary && F.MinLevel == L && Loc && Loc->Name == Where)
				{
					FUnlock U = MakeArt(Line, EArt::Fish, F.Id);
					U.CheckRows = FVDB::LegendChecklist(F, FFVCatchContext()).Num();   // one row per rule, whatever the tackle
					return U;
				}
			}
			return MakeIcon(Line, EFVIcon::Trophy);
		}
		if (Line.StartsWith(TEXT("Обновки в лавке: ")))
		{
			for (const FFVCosmeticDef& C : FVDB::Cosmetics())
			{
				if (C.MinLevel == L && C.Price > 0) { return MakeArt(Line, EArt::Cosmetic, C.Id); }
			}
			return MakeIcon(Line, EFVIcon::Wardrobe);
		}
		for (const FFVStationDef& S : FVDB::Stations())
		{
			if (Line == FString::Printf(TEXT("Можно построить: %s"), *S.Name))
			{
				FUnlock U;
				U.Text = Line;
				U.Art = EArt::Station;
				U.Station = S.Station;
				U.StationLevel = 1;
				return U;
			}
			const FString Up = FString::Printf(TEXT("Улучшение: %s → "), *S.Name);
			if (Line.StartsWith(Up))
			{
				const FString LvName = Line.RightChop(Up.Len());
				FUnlock U;
				U.Text = Line;
				U.Art = EArt::Station;
				U.Station = S.Station;
				U.StationLevel = 1;
				for (int32 i = 0; i < S.Levels.Num(); ++i)
				{
					if (S.Levels[i].Name == LvName) { U.StationLevel = i + 1; }
				}
				return U;
			}
		}
		if (Line.StartsWith(TEXT("Новые рецепты: ")))
		{
			for (const FFVRecipeDef& R : FVDB::Recipes())
			{
				if (R.MinLevel == L) { return MakeArt(Line, EArt::Item, R.OutputId); }
			}
			return MakeIcon(Line, EFVIcon::Pot);
		}
		if (Line.StartsWith(TEXT("Для дома: ")))
		{
			for (const FFVDecorDef& Dc : FVDB::Decor())
			{
				if (Dc.MinLevel == L && Dc.Price > 0) { return MakeArt(Line, EArt::Item, Dc.Id); }
			}
			return MakeIcon(Line, EFVIcon::House);
		}
		for (const FFVTraderDef& T : FVDB::Traders())
		{
			if (!Line.StartsWith(T.ShopTitle + TEXT(": "))) { continue; }
			for (const FFVItemDef& It : FVDB::Items())
			{
				if (It.MinLevel == L && It.BuyPrice > 0 && (It.TraderMask & T.Mask) != 0) { return MakeArt(Line, EArt::Item, It.Id); }
			}
			const TArray<FFVShovelDef>& Sh = FVDB::Shovels();
			for (int32 i = 0; i < Sh.Num(); ++i)
			{
				if (Sh[i].MinLevel == L && Sh[i].Price > 0 && (Sh[i].TraderMask & T.Mask) != 0)
				{
					return MakeArt(Line, EArt::Item, FName(*FString::Printf(TEXT("shovel_%d"), i + 1)));
				}
			}
			return MakeIcon(Line, EFVIcon::Shop);
		}
		return MakeIcon(Line, EFVIcon::Check);
	}

	/** Level 1 has no "unlock" lines in the database: the start of the journey. */
	static void StartBlock(TArray<FUnlock>& Out)
	{
		const TArray<FFVLocationDef>& Locs = FVDB::Locations();
		if (Locs.Num() > 0)
		{
			Out.Add(MakeArt(FString::Printf(TEXT("Первое место рыбалки: %s"), *Locs[0].Name), EArt::Location, Locs[0].Id));
			TArray<FString> Names;
			FName FirstFish;
			int32 NumFish = 0;
			for (const FFVFishDef* F : FVDB::FishAt(Locs[0].Id))
			{
				if (!F || F->bLegendary || F->MinLevel > 1) { continue; }
				if (FirstFish.IsNone()) { FirstFish = F->Id; }
				if (Names.Num() < 3) { Names.Add(F->Name); }
				++NumFish;
			}
			if (Names.Num() > 0)
			{
				Out.Add(MakeArt(FString::Printf(TEXT("Клюёт: %s%s"), *FString::Join(Names, TEXT(", ")), NumFish > Names.Num() ? TEXT(" и другие") : TEXT("")), EArt::Fish, FirstFish));
			}
		}
		const TArray<FFVRodDef>& Rods = FVDB::Rods();
		if (Rods.Num() > 0)
		{
			Out.Add(MakeArt(FString::Printf(TEXT("Своя удочка: %s"), *Rods[0].Name), EArt::Item, Rods[0].Id));
		}
		for (const FFVStationDef& S : FVDB::Stations())
		{
			if (S.bStartsBuilt || S.Levels.Num() == 0 || S.Levels[0].MinLevel > 1) { continue; }
			FUnlock U;
			U.Text = FString::Printf(TEXT("Можно построить: %s"), *S.Name);
			U.Art = EArt::Station;
			U.Station = S.Station;
			U.StationLevel = 1;
			Out.Add(U);
		}
	}

	static FLevelBlock BuildBlock(int32 L)
	{
		FLevelBlock B;
		B.Level = L;
		if (L <= 1)
		{
			StartBlock(B.Items);
			return B;
		}
		TArray<FString> Lines = FVDB::UnlocksAtLevel(L);
		Lines.Append(FVDB::HomeUnlocksAtLevel(L));
		for (const FString& Line : Lines) { B.Items.Add(Resolve(Line, L)); }
		return B;
	}

	static bool IsCaught(const UFVSaveGame& P, FName FishId)
	{
		const FFVFishRecord* Rec = P.GetRecord(FishId);
		return Rec && Rec->Caught > 0;
	}

	// =================================================================================================================
	// drawing helpers
	// =================================================================================================================
	/** The picture of an unlock inside Box (a light slot). */
	static void DrawArt(FVDraw& D, const UFVSaveGame& P, const FUnlock& U, const FFVRect& Box, float Time)
	{
		D.Rect(Box, FV::Col(255, 238, 204));
		D.Rect(FFVRect(Box.X, Box.Y, Box.W, 1.f), FV::Col(196, 140, 88));
		D.Frame(Box, CardEdge());
		UFVSprites* Sp = UFVSprites::Get();
		const FFVRect In = Box.Inset(2.f);
		switch (U.Art)
		{
		case EArt::Item:
			if (Sp) { FVUI::SpriteFit(D, Sp->ItemIcon(U.Id), In, FLinearColor::White, 2.f); }
			break;
		case EArt::Fish:
			if (const FFVFishDef* F = FVDB::FindFish(U.Id))
			{
				if (Sp)
				{
					const bool bCaught = IsCaught(P, U.Id);
					FVUI::SpriteFit(D, bCaught ? Sp->Fish(*F) : Sp->FishSilhouette(*F), In, FLinearColor::White, 2.f);
					if (F->bLegendary)
					{
						const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 3.f);
						D.Frame(Box.Inset(1.f), FV::Col(255, 204, 51, uint8(120.f + 110.f * Pulse)), 1.f);
					}
				}
			}
			break;
		case EArt::Station:
			if (Sp) { FVUI::SpriteFit(D, Sp->StationSprite(U.Station, FMath::Max(1, U.StationLevel)), In, FLinearColor::White, 2.f); }
			break;
		case EArt::Cosmetic:
			if (const FFVCosmeticDef* C = FVDB::FindCosmetic(U.Id))
			{
				if (!Sp) { break; }
				switch (C->Slot)
				{
				case EFVCosmeticSlot::Hat:
					FVUI::SpriteFit(D, Sp->HatIcon(EFVHatStyle(FMath::Min<int32>(C->Style, int32(EFVHatStyle::Count) - 1)), C->Primary, C->Secondary), In, FLinearColor::White, 2.f);
					break;
				case EFVCosmeticSlot::Outfit:
					FVUI::SpriteFit(D, Sp->OutfitIcon(EFVOutfitStyle(FMath::Min<int32>(C->Style, int32(EFVOutfitStyle::Count) - 1)), C->Primary, C->Secondary), In, FLinearColor::White, 2.f);
					break;
				case EFVCosmeticSlot::Bobber:
					FVUI::SpriteFit(D, Sp->Bobber(EFVBobberStyle(FMath::Min<int32>(C->Style, int32(EFVBobberStyle::Count) - 1)), 0), In, FLinearColor::White, 3.f);
					break;
				case EFVCosmeticSlot::Pet:
					FVUI::SpriteFit(D, Sp->Pet(EFVPetStyle(FMath::Min<int32>(C->Style, int32(EFVPetStyle::Count) - 1)), EFVPetAnim::Idle, 0), In, FLinearColor::White, 2.f);
					break;
				default:
					break;
				}
			}
			break;
		case EArt::Location:
		{
			const FFVLocationDef* Loc = FVDB::FindLocation(U.Id);
			const FLinearColor Sh = Loc ? FV::Col(Loc->WaterShallow) : FV::Col(90, 170, 220);
			const FLinearColor Dp = Loc ? FV::Col(Loc->WaterDeep) : FV::Col(30, 80, 150);
			D.GradientV(Box.Inset(1.f), Sh, Dp, 4);
			const float WY = FMath::RoundToFloat(Box.Y + Box.H * 0.35f + FMath::Sin(Time * 1.6f) * 1.f);
			D.Rect(FFVRect(Box.X + 3.f, WY, FMath::RoundToFloat(Box.W * 0.4f), 1.f), FV::Col(220, 244, 255, 150));
			if (const FFVSprite* M = FVUI::Icon(EFVIcon::Map)) { FVUI::SpriteFit(D, *M, Box.Inset(Box.W >= 22.f ? 4.f : 2.f), FLinearColor::White, 1.f); }
			break;
		}
		case EArt::CatchBar:
		{
			// the fishing mini-game's catch bar: a dark track with the green zone growing
			const float TW = FMath::Max(4.f, FMath::RoundToFloat(Box.W * 0.34f));
			const FFVRect Track(FMath::RoundToFloat(Box.Center().X - TW * 0.5f), Box.Y + 2.f, TW, Box.H - 4.f);
			D.Rect(Track, FV::Col(60, 40, 30));
			const float Grow = 0.45f + 0.15f * (0.5f + 0.5f * FMath::Sin(Time * 2.f));
			const float BH = FMath::RoundToFloat((Track.H - 2.f) * Grow);
			D.Rect(FFVRect(Track.X + 1.f, Track.Bottom() - 1.f - BH, Track.W - 2.f, BH), FVPal::Green());
			D.Rect(FFVRect(Track.X + 1.f, Track.Bottom() - 1.f - BH, Track.W - 2.f, 1.f), FV::Col(190, 240, 150));
			break;
		}
		case EArt::Icon:
		default:
			if (const FFVSprite* I = FVUI::Icon(U.Icon)) { FVUI::SpriteFit(D, *I, In, FLinearColor::White, 2.f); }
			break;
		}
	}

	/** Round level medallion (gold ring, the number inside). */
	static void Medallion(FVDraw& D, const FVector2D& C, float R, int32 Level, bool bBright, float Time)
	{
		D.Circle(C + FVector2D(1.f, 2.f), R, FV::Col(60, 30, 14, 90));
		D.Circle(C, R, bBright ? FV::Col(150, 92, 20) : FV::Col(140, 104, 72));
		D.Circle(C, R - 1.f, bBright ? FVPal::Gold() : FV::Col(222, 186, 138));
		D.Circle(C, R - 3.f, bBright ? FV::Col(255, 236, 170) : FV::Col(246, 222, 180));
		if (bBright)
		{
			const float Sh = FMath::Frac(Time / 3.2f);
			if (Sh < 0.3f) { D.Glow(C + FVector2D((Sh / 0.3f - 0.5f) * R * 1.4f, -R * 0.35f), R * 0.5f, FLinearColor(0.6f, 0.55f, 0.35f, 0.7f)); }
		}
		const float S = FMath::Max(7.f, FMath::RoundToFloat(R * 0.95f));
		const FString N = FString::FromInt(Level);
		D.Text(N, FVector2D(C.X, FMath::RoundToFloat(C.Y - D.LineHeight(S) * 0.5f)), S, bBright ? FV::Col(110, 56, 10) : FVPal::Ink(), EFVAlign::Center, false, true);
	}

	/** Small rounded chip with a label (the «Сейчас» / «Пройден» markers). Pos = right-top. Returns its width. */
	static float Chip(FVDraw& D, const FString& Label, const FVector2D& RightTop, float Size, const FLinearColor& Fill, const FLinearColor& Edge, const FLinearColor& Ink)
	{
		const FVector2D Sz = D.MeasureText(Label, Size, true);
		const float W = FMath::RoundToFloat(Sz.X + 10.f), H = FMath::RoundToFloat(Sz.Y + 3.f);
		const FFVRect R(FMath::RoundToFloat(RightTop.X - W), FMath::RoundToFloat(RightTop.Y), W, H);
		D.Rect(R.Offset(0.f, 1.f), FV::Col(60, 30, 14, 70));
		D.Rect(R, Edge);
		D.Rect(R.Inset(1.f), Fill);
		D.Text(Label, FVector2D(R.Center().X, FMath::RoundToFloat(R.Y + (H - Sz.Y) * 0.5f)), Size, Ink, EFVAlign::Center, false, true);
		return W;
	}

	// =================================================================================================================
	// the screen
	// =================================================================================================================
	class FLevelsScreen final : public FVScreen
	{
	public:
		explicit FLevelsScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			List.Reset();
			BuiltFor = -1;
			if (const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr) { Rebuild(*P); }
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			if (!Game || Game->HasModal()) { return; }
			const float Step = FV::IsPortrait() ? 60.f : 40.f;
			if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { List.ScrollTo(List.Target - Step); }
			if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { List.ScrollTo(List.Target + Step); }
			if (In.WasPressed(EKeys::PageUp)) { List.ScrollTo(List.Target - Step * 4.f); }
			if (In.WasPressed(EKeys::PageDown)) { List.ScrollTo(List.Target + Step * 4.f); }
			if (In.WasPressed(EKeys::Home)) { List.ScrollTo(0.f); }
			if (In.WasPressed(EKeys::End)) { List.ScrollTo(List.MaxOffset()); }
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bP = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			if (D.Window(Win, TEXT("Уровни рыбака")))
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;   // this screen is destroyed
			}
			const UFVSaveGame* P = Game->GetProfile();
			if (!P)
			{
				D.Text(TEXT("Профиль не загружен"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}
			if (BuiltFor != P->GetLevel()) { Rebuild(*P); }
			Time = float(FMath::Fmod(D.Time, 10000.0));

			float ListTop = 0.f, ListBottom = 0.f;
			const float X = Win.X + (bP ? 10.f : 12.f);
			const float W = Win.W - (bP ? 20.f : 24.f);
			if (bP)
			{
				const FFVRect Banner(X, Win.Y + 16.f, W, 104.f);
				DrawBannerPortrait(D, *P, Banner);
				ListTop = Banner.Bottom() + 8.f;
				ListBottom = FVDraw::ActionBarRect(Win).Y - 10.f;
			}
			else
			{
				const FFVRect Banner(X, Win.Y + 12.f, W, 50.f);
				DrawBannerLandscape(D, *P, Banner);
				ListTop = Banner.Bottom() + 6.f;
				ListBottom = Win.Bottom() - 10.f;
			}
			const FFVRect ListPanel(X, ListTop, W, FMath::Max(40.f, ListBottom - ListTop));
			DrawList(D, *P, ListPanel);

			if (bP)
			{
				FFVRect Actions;
				if (D.ActionBar(Win, &Actions))
				{
					UFVGame* G = Game;
					G->CloseScreen();
					return;   // this screen is destroyed
				}
				// the thumb's shortcut back to the current level when scrolled away
				if (List.Offset() > 40.f && D.Button(FVDraw::SplitH(Actions, 0, 1), TEXT("↑ К текущему"), EFVButton::Normal, true, FV::Ui::Body() + 1.f))
				{
					List.ScrollTo(0.f);
				}
			}
		}

	private:
		TArray<FLevelBlock> Blocks;      // [0] = the current level, then previous levels down to 1
		FLevelBlock Next;                // the next level (teaser); Level 0 = none (max level)
		int32 BuiltFor = -1;
		FFVScrollView List;
		float Time = 0.f;

		void Rebuild(const UFVSaveGame& P)
		{
			const int32 Cur = FMath::Clamp(P.GetLevel(), 1, FV::MaxLevel);
			BuiltFor = P.GetLevel();
			Blocks.Reset();
			for (int32 L = Cur; L >= 1; --L) { Blocks.Add(BuildBlock(L)); }
			Next = FLevelBlock();
			Next.Level = 0;
			if (Cur < FV::MaxLevel) { Next = BuildBlock(Cur + 1); }
		}

		/** The next level's first real unlock (the generic catch-bar line only when there is nothing else). */
		const FUnlock* Teaser() const
		{
			if (Next.Level <= 0 || Next.Items.Num() == 0) { return nullptr; }
			for (const FUnlock& U : Next.Items)
			{
				if (U.Art != EArt::CatchBar) { return &U; }
			}
			return &Next.Items[0];
		}

		struct FXpInfo
		{
			int32 Level = 1;
			int32 Into = 0;       // XP gathered inside this level
			int32 Span = 1;       // XP this level needs
			int32 Left = 0;       // XP left to the next level
			float Frac = 1.f;
			bool bMax = false;
			bool bFrozen = false;   // update 6 (Р1): XP stops at the level-10 threshold until the first arrival in the City
		};

		/** Update 6: the banner line while XP is frozen before the City. */
		static FString FrozenText()
		{
			return FString::Printf(TEXT("Опыт замер до Города: почините лодку на причале и доплывите — сразу станет %d уровень"), FV::ClassicMaxLevel + 1);
		}

		static FXpInfo XpInfo(const UFVSaveGame& P)
		{
			FXpInfo I;
			I.Level = FMath::Clamp(P.GetLevel(), 1, FV::MaxLevel);
			I.bMax = I.Level >= FV::MaxLevel;
			if (I.bMax) { return I; }
			const int32 Base = FVDB::XpForLevel(I.Level);
			const int32 Need = FVDB::XpForLevel(I.Level + 1);
			I.Span = FMath::Max(1, Need - Base);
			I.Into = FMath::Clamp(P.Xp - Base, 0, I.Span);
			I.Left = FMath::Max(0, Need - P.Xp);
			I.Frac = float(I.Into) / float(I.Span);
			I.bFrozen = P.IsXpFrozen();
			if (I.bFrozen) { I.Frac = 1.f; I.Into = I.Span; I.Left = 0; }
			return I;
		}

		void DrawXpBar(FVDraw& D, const FFVRect& R, float Frac)
		{
			D.Rect(R.Offset(0.f, 1.f), FV::Col(60, 30, 14, 60));
			D.Rect(R, FVPal::WoodDark());
			const FFVRect In = R.Inset(1.f);
			D.Rect(In, FV::Col(70, 48, 34));
			const float FW = FMath::RoundToFloat(In.W * FMath::Clamp(Frac, 0.f, 1.f));
			if (FW > 0.f)
			{
				D.Rect(FFVRect(In.X, In.Y, FW, In.H), FV::Col(92, 176, 232));
				D.Rect(FFVRect(In.X, In.Y, FW, 1.f), FV::Col(190, 230, 255));
				// a soft shine running along the filled part
				const float Sh = FMath::Frac(Time / 2.8f);
				if (Sh < 0.35f && FW > 8.f) { D.Glow(FVector2D(In.X + FW * (Sh / 0.35f), In.Center().Y), In.H * 1.4f, FLinearColor(0.35f, 0.4f, 0.45f, 0.6f)); }
			}
		}

		void DrawTeaserRow(FVDraw& D, const UFVSaveGame& P, const FUnlock& U, float X, float Y, float W, float BoxS, float TextS, int32 MaxLines)
		{
			const FString Lead = TEXT("Дальше:");
			D.Text(Lead, FVector2D(X, FMath::RoundToFloat(Y + (BoxS - D.LineHeight(TextS)) * 0.5f)), TextS, InkInfo(), EFVAlign::Left, false, true);
			const float LW = D.MeasureText(Lead, TextS, true).X + 5.f;
			DrawArt(D, P, U, FFVRect(X + LW, Y, BoxS, BoxS), Time);
			const float TX = X + LW + BoxS + 5.f;
			const float TW = FMath::Max(20.f, X + W - TX);
			if (MaxLines <= 1 || D.MeasureText(U.Text, TextS, false).X <= TW)
			{
				D.Text(D.Ellipsize(U.Text, TW, TextS, false), FVector2D(TX, FMath::RoundToFloat(Y + (BoxS - D.LineHeight(TextS, false)) * 0.5f)), TextS, FVPal::Ink(), EFVAlign::Left, false, false);
			}
			else
			{
				TextLimited(D, U.Text, FVector2D(TX, Y), TW, TextS, FVPal::Ink(), MaxLines);
			}
		}

		/** Word-wrapped text limited to MaxLines (the last visible line ends with "…"). Returns the height used. */
		static float TextLimited(FVDraw& D, const FString& S, const FVector2D& Pos, float MaxW, float Size, const FLinearColor& C, int32 MaxLines)
		{
			TArray<FString> Words;
			S.ParseIntoArrayWS(Words);
			TArray<FString> Lines;
			FString Cur;
			for (int32 i = 0; i < Words.Num(); ++i)
			{
				const FString Try = Cur.IsEmpty() ? Words[i] : Cur + TEXT(" ") + Words[i];
				if (!Cur.IsEmpty() && D.MeasureText(Try, Size, false).X > MaxW)
				{
					Lines.Add(Cur);
					Cur = Words[i];
				}
				else
				{
					Cur = Try;
				}
			}
			if (!Cur.IsEmpty()) { Lines.Add(Cur); }
			const float Step = FMath::RoundToFloat(D.LineHeight(Size, false) * 1.05f);
			const int32 N = FMath::Min(Lines.Num(), FMath::Max(1, MaxLines));
			for (int32 i = 0; i < N; ++i)
			{
				FString L = Lines[i];
				if (i == N - 1 && N < Lines.Num()) { L = D.Ellipsize(L + TEXT(" …"), MaxW, Size, false); }
				D.Text(D.Ellipsize(L, MaxW, Size, false), FVector2D(Pos.X, Pos.Y + float(i) * Step), Size, C, EFVAlign::Left, false, false);
			}
			return float(N) * Step;
		}

		void DrawBannerLandscape(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			const FXpInfo I = XpInfo(P);
			D.Panel(R, EFVPanel::Parchment);
			Medallion(D, FVector2D(R.X + 26.f, R.Center().Y), 19.f, I.Level, true, Time);
			const float LX = R.X + 52.f;
			const float LW = 230.f;
			D.Text(FString::Printf(TEXT("Уровень %d"), I.Level), FVector2D(LX, R.Y + 5.f), 11.f, FVPal::Ink(), EFVAlign::Left, false, true);
			DrawXpBar(D, FFVRect(LX, R.Y + 21.f, LW, 8.f), I.Frac);
			const FString XpText = I.bMax ? FString::Printf(TEXT("Опыт: %d"), P.Xp) : FString::Printf(TEXT("%d / %d опыта"), I.Into, I.Span);
			D.Text(XpText, FVector2D(LX, R.Y + 32.f), 6.5f, FVPal::InkSoft(), EFVAlign::Left, false, false);

			const float RX = LX + LW + 16.f;
			const float RW = R.Right() - 8.f - RX;
			D.Rect(FFVRect(RX - 8.f, R.Y + 6.f, 1.f, R.H - 12.f), FVPal::ParchDark());
			if (I.bMax)
			{
				D.Text(TEXT("Максимальный уровень!"), FVector2D(RX, R.Y + 7.f), 9.f, InkGoldText(), EFVAlign::Left, false, true);
				D.Text(D.Ellipsize(TEXT("Все награды открыты — теперь только трофеи и рекорды."), RW, 7.f, false), FVector2D(RX, R.Y + 21.f), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
				return;
			}
			if (I.bFrozen)
			{
				D.Text(TEXT("Опыт замер до Города"), FVector2D(RX, R.Y + 5.f), 8.5f, InkInfo(), EFVAlign::Left, false, true);
				TextLimited(D, FrozenText(), FVector2D(RX, R.Y + 19.f), RW, 7.f, FVPal::InkSoft(), 2);
				return;
			}
			const FString LeftText = FString::Printf(TEXT("До уровня %d — ещё %d опыта"), I.Level + 1, I.Left);
			D.Text(D.Ellipsize(LeftText, RW, 8.5f), FVector2D(RX, R.Y + 5.f), 8.5f, InkInfo(), EFVAlign::Left, false, true);
			if (const FUnlock* T = Teaser()) { DrawTeaserRow(D, P, *T, RX, R.Y + 21.f, RW, 20.f, 7.f, 1); }
		}

		void DrawBannerPortrait(FVDraw& D, const UFVSaveGame& P, const FFVRect& R)
		{
			const FXpInfo I = XpInfo(P);
			D.Panel(R, EFVPanel::Parchment);
			const float MR = 22.f;
			Medallion(D, FVector2D(R.X + 8.f + MR, R.Y + 8.f + MR), MR, I.Level, true, Time);
			const float LX = R.X + 16.f + MR * 2.f;
			const float LW = R.Right() - 8.f - LX;
			D.Text(FString::Printf(TEXT("Уровень %d"), I.Level), FVector2D(LX, R.Y + 6.f), FV::Ui::Header(), FVPal::Ink(), EFVAlign::Left, false, true);
			DrawXpBar(D, FFVRect(LX, R.Y + 26.f, LW, 10.f), I.Frac);
			const FString XpText = I.bMax ? FString::Printf(TEXT("Опыт: %d · максимальный уровень!"), P.Xp)
				: (I.bFrozen ? FString(TEXT("Опыт замер до Города"))
				: FString::Printf(TEXT("%d / %d · до уровня %d ещё %d"), I.Into, I.Span, I.Level + 1, I.Left));
			D.Text(D.Ellipsize(XpText, LW, FV::Ui::Small(), true), FVector2D(LX, R.Y + 39.f), FV::Ui::Small(), I.bMax ? InkGoldText() : InkInfo(), EFVAlign::Left, false, true);

			// teaser strip
			const FFVRect Strip(R.X + 6.f, R.Y + 60.f, R.W - 12.f, R.H - 66.f);
			D.Rect(Strip, FV::Col(240, 214, 164));
			D.Frame(Strip, FVPal::ParchDark(), 1.f);
			if (I.bMax)
			{
				D.TextWrapped(TEXT("Все награды открыты — теперь только трофеи и рекорды."), FVector2D(Strip.X + 8.f, Strip.Y + 6.f), Strip.W - 16.f, FV::Ui::Small(), FVPal::InkSoft());
				return;
			}
			if (I.bFrozen)
			{
				D.TextWrapped(FrozenText(), FVector2D(Strip.X + 8.f, Strip.Y + 6.f), Strip.W - 16.f, FV::Ui::Small(), FVPal::InkSoft());
				return;
			}
			if (const FUnlock* T = Teaser())
			{
				DrawTeaserRow(D, P, *T, Strip.X + 6.f, Strip.Y + 5.f, Strip.W - 12.f, 28.f, FV::Ui::Small(), 2);
			}
		}

		// ------------------------------------------------------------------------------------------------ the list
		struct FCardMetrics
		{
			float Pad = 6.f;
			float HeadH = 22.f;
			float BoxS = 18.f;
			float TextS = 7.f;
			int32 Cols = 2;
			float ColGap = 10.f;
			float RowGap = 3.f;
		};

		static FCardMetrics Metrics()
		{
			FCardMetrics M;
			if (FV::IsPortrait())
			{
				M.Pad = 8.f;
				M.HeadH = 30.f;
				M.BoxS = 28.f;
				M.TextS = FV::Ui::Body();
				M.Cols = 1;
				M.RowGap = 4.f;
			}
			return M;
		}

		/** Height of one checklist row (update 5). */
		static float CheckRowH(const FVDraw& D, const FCardMetrics& M) { return FMath::RoundToFloat(D.LineHeight(M.TextS, true) + 1.f); }

		/** Height of one item row pair / row (text wraps next to the picture; a legend's checklist rows go under the text). */
		static float ItemH(const FVDraw& D, const FUnlock& U, float ColW, const FCardMetrics& M)
		{
			const float TW = FMath::Max(20.f, ColW - M.BoxS - 6.f);
			return FMath::Max(M.BoxS, D.MeasureWrapped(U.Text, TW, M.TextS, false) + float(U.CheckRows) * CheckRowH(D, M)) + M.RowGap;
		}

		static float CardH(const FVDraw& D, const FLevelBlock& B, float W, const FCardMetrics& M)
		{
			const float InnerW = W - M.Pad * 2.f;
			const float ColW = (InnerW - M.ColGap * float(M.Cols - 1)) / float(M.Cols);
			float H = M.Pad + M.HeadH + 2.f;
			for (int32 i = 0; i < B.Items.Num(); i += M.Cols)
			{
				float RowH = 0.f;
				for (int32 c = 0; c < M.Cols && i + c < B.Items.Num(); ++c) { RowH = FMath::Max(RowH, ItemH(D, B.Items[i + c], ColW, M)); }
				H += RowH;
			}
			if (B.Items.Num() == 0) { H += D.LineHeight(M.TextS, false) + 4.f; }
			return FMath::RoundToFloat(H + M.Pad);
		}

		void DrawList(FVDraw& D, const UFVSaveGame& P, const FFVRect& Panel)
		{
			D.Panel(Panel, EFVPanel::Parchment);
			const FFVRect Area = Panel.Inset(2.f);
			const FCardMetrics M = Metrics();
			const float Gap = FV::IsPortrait() ? 8.f : 6.f;
			const float CardW = Area.W - 12.f - (List.CanScroll() ? 4.f : 0.f);
			TArray<float> Heights;
			float ContentH = 6.f;
			for (const FLevelBlock& B : Blocks)
			{
				Heights.Add(CardH(D, B, CardW, M));
				ContentH += Heights.Last() + Gap;
			}
			ContentH += 4.f;
			List.Update(D, Area, ContentH);
			D.PushClip(Area);
			float Y = Area.Y + 6.f - List.Offset();
			for (int32 i = 0; i < Blocks.Num(); ++i)
			{
				const FFVRect Card(Area.X + 6.f, Y, CardW, Heights[i]);
				if (Card.Bottom() >= Area.Y - 2.f && Card.Y <= Area.Bottom() + 2.f) { DrawCard(D, P, Blocks[i], i == 0, Card, M); }
				Y += Heights[i] + Gap;
			}
			D.PopClip();
			List.DrawFades(D, Area, FVPal::Parchment());
			List.DrawBar(D, Area);
		}

		/** Update 5: the live preparation checklist of the legend of U (the equipped rod + current bait): Check / Close icons. */
		void DrawChecklist(FVDraw& D, const FUnlock& U, float X, float Y, float W, const FCardMetrics& M)
		{
			const FFVFishDef* F = FVDB::FindFish(U.Id);
			if (!F || !Game) { return; }
			const float RowH = CheckRowH(D, M);
			const float IconS = FMath::Clamp(FMath::FloorToFloat(RowH - 1.f), 8.f, 16.f);
			for (const FFVLegendCheckItem& It : FVDB::LegendChecklist(*F, Game->MakeCatchContext(0.f)))
			{
				if (const FFVSprite* S = FVUI::Icon(It.bOk ? EFVIcon::Check : EFVIcon::Close))
				{
					D.SpriteCentered(*S, FVector2D(X + IconS * 0.5f, Y + RowH * 0.5f), IconS / FMath::Max(1.f, S->H));
				}
				const FString Label = It.bOk ? It.Label : It.Label + (It.bHard ? TEXT(" — не клюёт") : TEXT(" — обрыв"));
				const FLinearColor C = It.bOk ? InkGood() : (It.bHard ? FV::Col(190, 50, 40) : FV::Col(196, 112, 20));
				D.Text(D.Ellipsize(Label, W - IconS - 3.f, M.TextS), FVector2D(X + IconS + 3.f, Y), M.TextS, C, EFVAlign::Left, false, true);
				Y += RowH;
			}
		}

		void DrawCard(FVDraw& D, const UFVSaveGame& P, const FLevelBlock& B, bool bCurrent, const FFVRect& R, const FCardMetrics& M)
		{
			const bool bP = FV::IsPortrait();
			// card body
			D.Rect(R.Offset(1.f, 2.f), FV::Col(90, 50, 20, 50));
			D.Rect(R, bCurrent ? FV::Col(206, 146, 30) : CardEdge());
			D.Rect(R.Inset(1.f), bCurrent ? FV::Col(255, 246, 222) : FV::Col(251, 232, 196));
			if (bCurrent)
			{
				D.Frame(R.Inset(1.f), FVPal::Gold(), 1.f);
				// a slow sheen sweeping over the current level's header
				const float Sh = FMath::Frac(Time / 4.f);
				if (Sh < 0.4f) { D.Glow(FVector2D(R.X + R.W * (Sh / 0.4f), R.Y + M.Pad + M.HeadH * 0.5f), M.HeadH, FLinearColor(0.25f, 0.22f, 0.12f, 0.55f)); }
			}
			// header: medallion, title, marker chip
			const float HR = bP ? 12.f : 9.f;
			Medallion(D, FVector2D(R.X + M.Pad + HR, R.Y + M.Pad + M.HeadH * 0.5f - 1.f), HR, B.Level, bCurrent, Time);
			const float TX = R.X + M.Pad + HR * 2.f + 6.f;
			const float TitleS = bP ? FV::Ui::Body() + 2.f : 9.f;
			const float SubS = FV::Ui::Small();
			const FString Title = B.Level <= 1 ? FString(TEXT("Уровень 1 · начало пути")) : FString::Printf(TEXT("Уровень %d"), B.Level);
			const FString Sub = bCurrent ? FString(TEXT("Ваш уровень — вот что он открыл")) : FString(TEXT("Открыто раньше"));
			const float ChipS = bP ? FV::Ui::Small() : 6.5f;
			const float ChipW = bCurrent
				? Chip(D, TEXT("Сейчас"), FVector2D(R.Right() - M.Pad, R.Y + M.Pad), ChipS, FVPal::Gold(), FV::Col(170, 108, 16), FV::Col(96, 50, 8))
				: Chip(D, TEXT("Пройден"), FVector2D(R.Right() - M.Pad, R.Y + M.Pad), ChipS, FV::Col(214, 238, 186), FV::Col(92, 150, 60), InkGood());
			const float TitleW = R.Right() - M.Pad - ChipW - 8.f - TX;
			const float TitleY = R.Y + M.Pad + (bP ? 0.f : -1.f);
			D.Text(D.Ellipsize(Title, TitleW, TitleS), FVector2D(TX, TitleY), TitleS, FVPal::Ink(), EFVAlign::Left, false, true);
			D.Text(D.Ellipsize(Sub, R.Right() - M.Pad - TX, SubS, false), FVector2D(TX, TitleY + D.LineHeight(TitleS) - 1.f), SubS, bCurrent ? InkGoldText() : FVPal::InkSoft(), EFVAlign::Left, false, false);
			float Y = R.Y + M.Pad + M.HeadH;
			D.Rect(FFVRect(R.X + M.Pad, Y - 1.f, R.W - M.Pad * 2.f, 1.f), bCurrent ? FV::Col(236, 196, 110) : FVPal::ParchDark());
			Y += 2.f;

			// unlocks: rows of M.Cols items
			const float InnerW = R.W - M.Pad * 2.f;
			const float ColW = (InnerW - M.ColGap * float(M.Cols - 1)) / float(M.Cols);
			if (B.Items.Num() == 0)
			{
				D.Text(TEXT("Ничего нового — просто рыбачьте!"), FVector2D(R.X + M.Pad, Y + 2.f), M.TextS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				return;
			}
			for (int32 i = 0; i < B.Items.Num(); i += M.Cols)
			{
				float RowH = 0.f;
				for (int32 c = 0; c < M.Cols && i + c < B.Items.Num(); ++c)
				{
					const FUnlock& U = B.Items[i + c];
					const float CX = FMath::RoundToFloat(R.X + M.Pad + float(c) * (ColW + M.ColGap));
					const FFVRect Box(CX, FMath::RoundToFloat(Y), M.BoxS, M.BoxS);
					DrawArt(D, P, U, Box, Time);
					const float TW = FMath::Max(20.f, ColW - M.BoxS - 6.f);
					const float TH = D.MeasureWrapped(U.Text, TW, M.TextS, false);
					const float ChecksH = float(U.CheckRows) * CheckRowH(D, M);
					const float TY = FMath::RoundToFloat(Y + FMath::Max(0.f, (M.BoxS - TH - ChecksH) * 0.5f));
					D.TextWrapped(U.Text, FVector2D(Box.Right() + 6.f, TY), TW, M.TextS, FVPal::Ink(), false);
					if (U.CheckRows > 0) { DrawChecklist(D, U, Box.Right() + 6.f, TY + TH, TW, M); }
					RowH = FMath::Max(RowH, ItemH(D, U, ColW, M));
				}
				Y += RowH;
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeLevelsScreen(UFVGame* Game)
{
	return MakeUnique<FVLevelsScreenImpl::FLevelsScreen>(Game);
}
