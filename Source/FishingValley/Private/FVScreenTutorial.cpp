// «Учебник» (update 3): an illustrated guide that opens up gradually. Entries are built from the live profile and the
// database: an entry appears once it is relevant (fishing level, first catches, unlocked places, built stations, owned
// items...) and inside it only the items / recipes / stations whose MinLevel <= the player's level are listed.
//   Landscape: the index (categories + entries) on the left, the entry page on the right.
//   Portrait:  the index list; tapping an entry slides its page in (and «Назад» slides back).
// Entry pages scroll smoothly (FFVScrollView) and swipe to the neighbouring entry with a slide (FFVPager); the tab
// buttons / arrows / index taps animate the same slide. Opening an entry marks it seen (UFVSaveGame::SeenTutorial).
//
// Context (UFVGame::GetScreenContext) = an entry id to open directly. Entry ids (for OpenScreenWith(Tutorial, Id)):
//   Основы:      g_controls g_fishing g_reeling g_perfect g_quality g_legends
//   Снаряжение:  g_inventory g_bait g_rods g_tackle g_food
//   Мир:         g_travel g_trade g_time g_gather g_worms g_nets g_orders
//   Дом:         g_home g_bed g_kitchen g_workshop g_knots g_lures g_smoke g_aquarium g_trophy g_decor
//   Коллекции:   g_collection g_achievements
//   Update 6 (Р43, §12.3): Мир g_bigmap g_forest g_axe g_forest_npc g_city_orders g_netkeeper; Снаряжение g_scrap;
//   Дом g_yard g_sawmill g_garden g_forge g_rooms g_upgrades2; Экспертная зона g_city g_expert g_expert_rods
//   g_expert_tackle g_hazards g_seaweather g_facts g_expert_legends g_mismatch g_cave; §18 Мир g_mill.
//   FVTutorialAnnounce = the toast.

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "InputCoreTypes.h"

namespace FVTutorialScreenImpl
{
	// =================================================================================================================
	// pictures
	// =================================================================================================================
	enum class EPic : uint8 { None, Icon, Item, Fish, Station, Lure, Shovel, Stars, Stall };

	struct FPic
	{
		EPic Kind = EPic::None;
		EFVIcon Icon = EFVIcon::Count;
		FName Id;
		uint8 A = 0, B = 0, C = 0;
	};

	FPic PIcon(EFVIcon I) { FPic P; P.Kind = EPic::Icon; P.Icon = I; return P; }
	FPic PItem(FName Id) { FPic P; P.Kind = EPic::Item; P.Id = Id; return P; }
	FPic PItem(const TCHAR* Id) { return PItem(FName(Id)); }
	FPic PFish(FName Id, bool bSilhouette) { FPic P; P.Kind = EPic::Fish; P.Id = Id; P.A = bSilhouette ? 1 : 0; return P; }
	FPic PStation(EFVStation S, int32 Level) { FPic P; P.Kind = EPic::Station; P.A = uint8(S); P.B = uint8(FMath::Clamp(Level, 0, 9)); return P; }
	FPic PLure(uint8 Base, uint8 Color, uint8 Glitter) { FPic P; P.Kind = EPic::Lure; P.A = Base; P.B = Color; P.C = Glitter; return P; }
	FPic PShovel(int32 Level) { FPic P; P.Kind = EPic::Shovel; P.A = uint8(FMath::Clamp(Level, 1, 9)); return P; }
	FPic PStars(EFVQuality Q) { FPic P; P.Kind = EPic::Stars; P.A = uint8(Q); return P; }
	FPic PStall(FName TraderId) { FPic P; P.Kind = EPic::Stall; P.Id = TraderId; return P; }

	/** Draws a picture fitted (integer up-scale up to MaxUp) into Box. */
	void DrawPic(FVDraw& D, const FPic& P, const FFVRect& Box, float MaxUp)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp || Box.W < 2.f || Box.H < 2.f) { return; }
		switch (P.Kind)
		{
		case EPic::Icon:
			if (const FFVSprite* S = FVUI::Icon(P.Icon)) { FVUI::SpriteFit(D, *S, Box, FLinearColor::White, MaxUp); }
			break;
		case EPic::Item:
		{
			const FFVSprite S = Sp->ItemIcon(P.Id);
			FVUI::SpriteFit(D, S, Box, FLinearColor::White, MaxUp);
			break;
		}
		case EPic::Fish:
			if (const FFVFishDef* F = FVDB::FindFish(P.Id))
			{
				const FFVSprite S = P.A ? Sp->FishSilhouette(*F) : Sp->Fish(*F);
				FVUI::SpriteFit(D, S, Box, P.A ? FLinearColor(1.f, 1.f, 1.f, 0.8f) : FLinearColor::White, MaxUp);
			}
			break;
		case EPic::Station:
		{
			const EFVStation St = EFVStation(FMath::Min<int32>(P.A, int32(EFVStation::Count) - 1));
			const FFVSprite S = Sp->StationSprite(St, P.B);
			FVUI::SpriteFit(D, S, Box, FLinearColor::White, MaxUp);
			break;
		}
		case EPic::Lure:
		{
			const FFVSprite S = Sp->LureIcon(P.A, P.B, P.C);
			FVUI::SpriteFit(D, S, Box, FLinearColor::White, MaxUp);
			break;
		}
		case EPic::Shovel:
		{
			const FFVSprite S = Sp->ShovelIcon(P.A);
			FVUI::SpriteFit(D, S, Box, FLinearColor::White, MaxUp);
			break;
		}
		case EPic::Stars:
		{
			const EFVQuality Q = EFVQuality(FMath::Min<int32>(P.A, int32(EFVQuality::Iridium)));
			if (Q == EFVQuality::Normal)
			{
				if (const FFVSprite* S = FVUI::Icon(EFVIcon::Coin)) { FVUI::SpriteFit(D, *S, Box, FLinearColor::White, MaxUp); }
			}
			else
			{
				const float K = FMath::Clamp(FMath::FloorToFloat(FMath::Min(Box.W, Box.H) / 9.f), 1.f, FMath::Max(1.f, MaxUp));
				const FVector2D C = Box.Center();
				D.Stars(FVector2D(FMath::RoundToFloat(C.X - 4.f * K), FMath::RoundToFloat(C.Y - 4.f * K)), Q, K);
			}
			break;
		}
		case EPic::Stall:
		{
			const FFVSprite S = Sp->StallSprite(P.Id);
			FVUI::SpriteFit(D, S, Box, FLinearColor::White, MaxUp);
			break;
		}
		default:
			break;
		}
	}

	// =================================================================================================================
	// entries: blocks of text and pictures
	// =================================================================================================================
	enum class EBlk : uint8 { Para, Sub, Bullet, Tip, Note, Row, Pics, Keys, MiniBar };

	struct FPicCap
	{
		FPic Pic;
		FString Caption;       // '\n' = second line
	};

	struct FBlock
	{
		EBlk Kind = EBlk::Para;
		FString Text;          // Para / Sub / Bullet / Tip / Note text; Row title; Keys key
		FString Text2;         // Row body; Keys description
		FString Right;         // Row: small right tag ("ранг 2", "построено")
		FLinearColor RightColor = FV::Col(120, 84, 60);
		int64 Price = -1;      // Row: coin price tag (-1 = none)
		FPic Pic;              // Row picture / Tip icon
		TArray<FPicCap> Pics;  // Pics
	};

	struct FEntry
	{
		FName Id;
		int32 Cat = 0;
		FString Title;
		FString Lead;          // one-line summary (hero + portrait index)
		FPic Icon;
		TArray<FBlock> Blocks;
	};

	struct FCategory { const TCHAR* Name; EFVIcon Icon; };
	const FCategory GCategories[] = {
		{ TEXT("Основы"), EFVIcon::Rod },
		{ TEXT("Снаряжение"), EFVIcon::Backpack },
		{ TEXT("Мир"), EFVIcon::Map },
		{ TEXT("Дом и ремёсла"), EFVIcon::House },
		{ TEXT("Коллекции"), EFVIcon::Book },
		{ TEXT("Экспертная зона"), EFVIcon::ExpertRod } };   // update 6
	constexpr int32 NumCategories = UE_ARRAY_COUNT(GCategories);

	FString CatName(int32 Cat) { return GCategories[FMath::Clamp(Cat, 0, NumCategories - 1)].Name; }
	EFVIcon CatIcon(int32 Cat) { return GCategories[FMath::Clamp(Cat, 0, NumCategories - 1)].Icon; }

	FORCEINLINE FLinearColor InkGood() { return FV::Col(52, 122, 36); }
	FORCEINLINE FLinearColor CardFill() { return FV::Col(255, 240, 208); }
	FORCEINLINE FLinearColor CardLine() { return FV::Col(222, 178, 118); }

	/** Block builder used by the entry texts. */
	struct FB
	{
		FEntry& E;
		explicit FB(FEntry& InE) : E(InE) {}
		void Head(const FString& Title, const FString& Lead, const FPic& Icon) { E.Title = Title; E.Lead = Lead; E.Icon = Icon; }
		FBlock& Add(EBlk K) { FBlock& B = E.Blocks.AddDefaulted_GetRef(); B.Kind = K; return B; }
		void Para(const FString& T) { Add(EBlk::Para).Text = T; }
		void Sub(const FString& T) { Add(EBlk::Sub).Text = T; }
		void Bullet(const FString& T) { Add(EBlk::Bullet).Text = T; }
		void Note(const FString& T) { Add(EBlk::Note).Text = T; }
		void Tip(const FString& T, EFVIcon I = EFVIcon::Exclaim) { FBlock& B = Add(EBlk::Tip); B.Text = T; B.Pic = PIcon(I); }
		FBlock& Row(const FPic& P, const FString& Title, const FString& Body)
		{
			FBlock& B = Add(EBlk::Row);
			B.Pic = P;
			B.Text = Title;
			B.Text2 = Body;
			return B;
		}
		FBlock& Pics() { return Add(EBlk::Pics); }
		void Keys(const FString& K, const FString& What) { FBlock& B = Add(EBlk::Keys); B.Text = K; B.Text2 = What; }
		void MiniBar() { Add(EBlk::MiniBar); }
	};

	void AddPic(FBlock& B, const FPic& P, const FString& Caption)
	{
		FPicCap& C = B.Pics.AddDefaulted_GetRef();
		C.Pic = P;
		C.Caption = Caption;
	}

	// =================================================================================================================
	// profile facts (unlock rules): cheap, recomputed on demand (the HUD badge asks every frame)
	// =================================================================================================================
	struct FFacts
	{
		int32 Level = 1;
		int32 Fish = 0, Treasures = 0, Gathered = 0, Crafted = 0, Cooked = 0, WormsDug = 0, Knots = 0, OrdersDone = 0;
		int64 Earned = 0;
		int32 Day = 1;
		float Minutes = 360.f;
		int32 Unlocked = 1, Rods = 1, Lures = 0, Achievements = 0, Species = 0;
		int32 St[int32(EFVStation::Count)] = {};
		int32 Shovel = 0, NetsPlaced = 0, OrdersTaken = 0;
		bool bTackle = false, bFood = false, bNetItem = false, bAnyStation = false;
		// update 6
		int32 Axe = 0, Pickaxe = 0, Boat = 0, ExpertRods = 0;
		int32 Yard[int32(EFVYardBuilding::Count)] = {};
		bool bBlockage = false, bCity = false, bPier = false, bExpertCaught = false;
		bool bMillBridge = false, bMillOpen = false;   // §18

		FFacts() = default;
		explicit FFacts(const UFVSaveGame& P)
		{
			Level = P.GetLevel();
			Fish = P.Stats.FishCaught;
			Treasures = P.Stats.Treasures;
			Gathered = P.Stats.Gathered;
			Crafted = P.Stats.Crafted;
			Cooked = P.Stats.Cooked;
			WormsDug = P.Stats.WormsDug;
			Knots = P.Stats.KnotsTied;
			OrdersDone = P.Stats.OrdersDone;
			Earned = P.Stats.Earned;
			Day = P.Day;
			Minutes = P.TimeMinutes;
			Unlocked = P.UnlockedLocations.Num();
			Rods = P.OwnedRods.Num();
			Lures = P.DiscoveredLures.Num();
			Achievements = P.CompletedAchievements.Num();
			Species = P.SpeciesCaught();
			Shovel = P.ShovelLevel;
			NetsPlaced = P.Nets.Num();
			OrdersTaken = P.TakenOrders();
			for (int32 s = 0; s < int32(EFVStation::Count); ++s)
			{
				St[s] = P.GetStationLevel(EFVStation(s));
				if (St[s] > 0 && EFVStation(s) != EFVStation::Bed && EFVStation(s) != EFVStation::Chest) { bAnyStation = true; }
			}
			for (const FFVInvStack& S : P.Backpack)
			{
				if (const FFVItemDef* It = FVDB::FindItem(S.ItemId))
				{
					if (It->Kind == EFVItemKind::Line || It->Kind == EFVItemKind::Float) { bTackle = true; }
					if (It->Kind == EFVItemKind::Food) { bFood = true; }
					if (It->Kind == EFVItemKind::Net) { bNetItem = true; }
				}
			}
			for (const auto& KV : P.RodLoadouts)
			{
				if (!KV.Value.Line.IsNone() || !KV.Value.Float.IsNone()) { bTackle = true; }
			}
			if (!P.ActiveFood.IsNone() || Cooked > 0) { bFood = true; }
			// update 6
			Axe = P.U6.AxeLevel;
			Pickaxe = P.U6.PickaxeLevel;
			Boat = P.U6.BoatStage;
			bBlockage = P.U6.bBlockageCleared;
			bCity = P.U6.bVisitedCity;
			bPier = P.IsLocationUnlocked(FName(TEXT("ocean")));
			bExpertCaught = P.U6.ExpertTypesCaught != 0;
			bMillBridge = P.IsBridgeRepaired();
			bMillOpen = P.IsLocationUnlocked(FVMill::MillId());
			for (int32 b = 0; b < int32(EFVYardBuilding::Count); ++b) { Yard[b] = P.GetYardLevel(EFVYardBuilding(b)); }
			for (const FName& Id : P.OwnedRods)
			{
				const FFVRodDef* R = FVDB::FindRod(Id);
				if (R && R->bExpert) { ++ExpertRods; }
			}
		}
		/** Update 6: the expert zone topics open at level 11 (or after the first arrival in the City). */
		bool Expert() const { return Level > FV::ClassicMaxLevel || bCity; }

		/** The house topics open once the player starts to live beyond the pond. */
		bool Home() const { return Gathered >= 3 || bAnyStation || Shovel > 0 || Crafted > 0 || Level >= 3 || Fish >= 15; }
		int32 Lvl(EFVStation S) const { return St[FMath::Clamp(int32(S), 0, int32(EFVStation::Count) - 1)]; }
		static int32 StationMin(EFVStation S, int32 StationLevel)
		{
			const FFVStationDef& Def = FVDB::Station(S);
			return Def.Levels.IsValidIndex(StationLevel - 1) ? Def.Levels[StationLevel - 1].MinLevel : 999;
		}
		/** Built to that level already, or buildable at the current fishing level. */
		bool CanBuild(EFVStation S, int32 StationLevel = 1) const { return Lvl(S) >= StationLevel || Level >= StationMin(S, StationLevel); }
		/** A location is "known" when the fishing level allows unlocking it. */
		bool KnowsPlace(FName LocId) const
		{
			const FFVLocationDef* L = FVDB::FindLocation(LocId);
			return L && L->MinLevel <= Level;
		}
		int32 MinNetLevel() const
		{
			int32 M = 999;
			for (const FFVItemDef* It : FVDB::ItemsOfKind(EFVItemKind::Net)) { if (It) { M = FMath::Min(M, It->MinLevel); } }
			return M;
		}
	};

	// =================================================================================================================
	// text helpers
	// =================================================================================================================
	FORCEINLINE bool Touch() { return FV::IsTouchUI(); }

	/** "подойдите — предмет подсветится — и ..." action wording for the current input. */
	FString UseWord() { return Touch() ? FString(TEXT("коснитесь экрана")) : FString(TEXT("кликните или нажмите E")); }

	FString InputsText(const TArray<FFVIngredient>& Ins)
	{
		TArray<FString> Parts;
		for (const FFVIngredient& I : Ins)
		{
			EFVRarity TokenRarity = EFVRarity::Common;   // update 5: «редкая рыба» / «эпическая рыба» / «легендарная рыба»
			const FString N = I.ItemId == FVDB::AnyFishToken() ? FString(TEXT("любая рыба"))
				: !FVDB::FishTokenRarity(I.ItemId, TokenRarity) ? FVDB::ItemName(I.ItemId)
				: TokenRarity == EFVRarity::Rare ? FString(TEXT("редкая рыба"))
				: TokenRarity == EFVRarity::Epic ? FString(TEXT("эпическая рыба")) : FString(TEXT("легендарная рыба"));
			Parts.Add(I.Count > 1 ? FString::Printf(TEXT("%s ×%d"), *N, I.Count) : N);
		}
		return Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("—"));
	}

	int32 Pct(float V) { return FMath::RoundToInt(V * 100.f); }

	FString BaitEffect(const FFVBaitDef& B)
	{
		TArray<FString> Parts;
		if (B.BiteSpeed < 0.99f) { Parts.Add(FString::Printf(TEXT("клёв быстрее на %d%%"), Pct(1.f - B.BiteSpeed))); }
		else if (B.BiteSpeed > 1.01f) { Parts.Add(FString::Printf(TEXT("клёв медленнее на %d%%"), Pct(B.BiteSpeed - 1.f))); }
		if (B.RareBonus > 0.001f) { Parts.Add(FString::Printf(TEXT("редкая рыба +%d%%"), Pct(B.RareBonus))); }
		if (B.SizeBonus > 0.001f) { Parts.Add(TEXT("рыба крупнее")); }
		if (B.JunkMult < 0.95f) { Parts.Add(TEXT("меньше мусора")); }
		else if (B.JunkMult > 1.05f) { Parts.Add(TEXT("больше мусора")); }
		return FString::Join(Parts, TEXT(" · "));
	}

	FString RodStats(const FFVRodDef& R)
	{
		TArray<FString> Parts;
		if (R.BarBonus > 0.5f) { Parts.Add(FString::Printf(TEXT("зона +%d"), FMath::RoundToInt(R.BarBonus))); }
		if (R.ReelMult > 1.001f) { Parts.Add(FString::Printf(TEXT("подтягивание +%d%%"), Pct(R.ReelMult - 1.f))); }
		if (R.BiteSpeed < 0.99f) { Parts.Add(FString::Printf(TEXT("клёв быстрее на %d%%"), Pct(1.f - R.BiteSpeed))); }
		Parts.Add(FString::Printf(TEXT("дальность %d%%"), Pct(R.CastRange)));
		if (R.Luck > 0.001f) { Parts.Add(FString::Printf(TEXT("удача +%d%%"), Pct(R.Luck))); }
		if (R.TreasureBonus > 0.001f) { Parts.Add(FString::Printf(TEXT("сундуки +%d%%"), Pct(R.TreasureBonus))); }
		return FString::Join(Parts, TEXT(" · "));
	}

	/** Recipe availability for the player: fishing level and (built or buildable) station level. */
	bool RecipeKnown(const FFacts& F, const FFVRecipeDef& R)
	{
		return R.MinLevel <= F.Level && F.CanBuild(R.Station, FMath::Max(1, R.StationLevel));
	}

	/** §18 «Магазины»: some tackle is no longer sold — any recipe (known or not yet) makes it. */
	bool HasAnyRecipe(FName OutputId)
	{
		for (const FFVRecipeDef& R : FVDB::Recipes())
		{
			if (R.OutputId == OutputId) { return true; }
		}
		return false;
	}

	bool IsCraftable(const FFacts& F, FName OutputId)
	{
		for (const FFVRecipeDef& R : FVDB::Recipes())
		{
			if (R.OutputId == OutputId && RecipeKnown(F, R)) { return true; }
		}
		return false;
	}

	/** Rows for every station level the player can see (built, or MinLevel <= level). */
	void LevelRows(FB& B, const FFacts& F, EFVStation S)
	{
		const FFVStationDef& Def = FVDB::Station(S);
		const int32 Cur = F.Lvl(S);
		for (int32 i = 0; i < Def.Levels.Num(); ++i)
		{
			const FFVStationLevel& L = Def.Levels[i];
			if (L.MinLevel > F.Level && Cur < i + 1) { continue; }
			FString Body = L.Description;
			if (Cur < i + 1 && L.Materials.Num() > 0) { Body += FString::Printf(TEXT("\nНужно: %s"), *InputsText(L.Materials)); }
			FBlock& R = B.Row(PStation(S, i + 1), L.Name.IsEmpty() ? Def.Name : L.Name, Body);
			if (Cur >= i + 1) { R.Right = TEXT("построено"); R.RightColor = InkGood(); }
			else if (L.Price > 0) { R.Price = L.Price; }
		}
	}

	/** Rows of the recipes of a station that the player can already make (or will once the station is built). */
	template<typename FilterType>
	int32 RecipeRows(FB& B, const FFacts& F, EFVStation S, FilterType&& Filter)
	{
		int32 N = 0;
		for (const FFVRecipeDef* R : FVDB::RecipesFor(S))
		{
			if (!R || !Filter(*R) || !RecipeKnown(F, *R)) { continue; }
			FString Body = FString::Printf(TEXT("Нужно: %s"), *InputsText(R->Inputs));
			if (R->CoinCost > 0) { Body += FString::Printf(TEXT(" + %s"), *FVUI::Coins(R->CoinCost)); }
			if (R->Game == EFVCraftGame::Knots) { Body += FString::Printf(TEXT("\nПлетение: узел из %d петель"), FMath::Max(3, R->GameDifficulty)); }
			else if (R->Game == EFVCraftGame::Cooking) { Body += TEXT("\nГотовка: мини-игра"); }
			FString Title = FVDB::ItemName(R->OutputId);
			if (R->OutputCount > 1) { Title += FString::Printf(TEXT(" ×%d"), R->OutputCount); }
			FBlock& Row = B.Row(PItem(R->OutputId), Title, Body);
			const int32 Need = FMath::Max(1, R->StationLevel);
			Row.Right = FVDB::StationLevelName(S, Need);
			Row.RightColor = F.Lvl(S) >= Need ? InkGood() : FVPal::InkSoft();
			++N;
		}
		return N;
	}

	void TackleRows(FB& B, const FFacts& F, EFVItemKind K)
	{
		for (const FFVItemDef* It : FVDB::ItemsOfKind(K))
		{
			if (!It || It->MinLevel > F.Level) { continue; }
			FString Body = FString::Join(FVDB::StatModsLines(It->Tackle), TEXT(" · "));
			TArray<FString> Src;
			if (It->BuyPrice > 0 && It->TraderMask != 0) { Src.Add(FString::Printf(TEXT("в лавке — %s"), *FVUI::Coins(It->BuyPrice))); }
			if (IsCraftable(F, It->Id)) { Src.Add(TEXT("в мастерской")); }
			else if (Src.Num() == 0 && HasAnyRecipe(It->Id)) { Src.Add(TEXT("не продаётся — сделайте в мастерской")); }
			if (Src.Num() > 0) { Body += TEXT("\n") + FString::Join(Src, TEXT(", ")); }
			FBlock& R = B.Row(PItem(It->Id), It->Name, Body);
			R.Right = FString::Printf(TEXT("ранг %d"), int32(It->Tier));
		}
	}

	// =================================================================================================================
	// the entries (Основы)
	// =================================================================================================================
	void BuildControls(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const bool bP = FV::IsPortrait();
		B.Head(TEXT("Управление"), TEXT("Как ходить, рыбачить и брать вещи"), PIcon(EFVIcon::Hand));
		if (Touch())
		{
			B.Row(PIcon(EFVIcon::ArrowRight), TEXT("Ходить"), TEXT("Проведите пальцем в нужную сторону — джойстик появится там, где вы коснулись."));
			B.Row(PIcon(EFVIcon::Rod), TEXT("Рыбачить"), TEXT("У воды зажмите палец на месте — начнётся замах. Отпустите, чтобы забросить. Подсечка и вываживание — тоже касанием."));
			B.Row(PIcon(EFVIcon::Hand), TEXT("Взять или открыть"), TEXT("Подойдите к предмету, прилавку или двери — они подсветятся. Теперь коснитесь экрана в любом месте: попадать точно не нужно."));
			B.Row(PIcon(EFVIcon::Menu), TEXT("Окна"), bP
				? TEXT("Кнопки внизу: карта, инвентарь, гардероб, коллекция и достижения. Меню и учебник — вверху справа.")
				: TEXT("Иконки вверху: карта, инвентарь, гардероб, коллекция, достижения, учебник и меню."));
			B.Tip(TEXT("В окнах смахивайте вбок, чтобы переключить вкладку, и вверх-вниз, чтобы листать списки. «Назад» закрывает окно."), EFVIcon::Hand);
		}
		else
		{
			B.Keys(TEXT("WASD / стрелки"), TEXT("ходить"));
			B.Keys(TEXT("Пробел / ЛКМ"), TEXT("зажать — замах, отпустить — заброс; подсечка и вываживание"));
			B.Keys(TEXT("E / ЛКМ"), TEXT("взять выделенный предмет, открыть лавку, войти в дом"));
			B.Keys(TEXT("M"), TEXT("карта и поездки"));
			B.Keys(TEXT("I / B"), TEXT("инвентарь: садок и рюкзак"));
			B.Keys(TEXT("G"), TEXT("гардероб"));
			B.Keys(TEXT("C / J"), TEXT("коллекция / достижения"));
			B.Keys(TEXT("F1"), TEXT("этот учебник"));
			B.Keys(TEXT("Esc"), TEXT("меню; отмена заброса"));
			B.Sub(TEXT("Выделенный предмет"));
			B.Row(PIcon(EFVIcon::Hand), TEXT("Клик в любом месте"), TEXT("Подойдите к предмету, прилавку или двери — вокруг появится подсветка, а внизу подсказка. Пока предмет выделен, хватит клика в любом месте экрана или клавиши E."));
			B.Tip(TEXT("Во вкладках окон работают стрелки ←/→ и перетаскивание мышью вбок, списки листаются колесом."));
		}
	}

	void BuildFishing(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Заброс и поклёвка"), TEXT("Как закинуть удочку и подсечь рыбу"), PIcon(EFVIcon::Rod));
		FBlock& Steps = B.Pics();
		AddPic(Steps, PIcon(EFVIcon::Rod), TEXT("1. Замах"));
		AddPic(Steps, PIcon(EFVIcon::Clock), TEXT("2. Ждём"));
		AddPic(Steps, PIcon(EFVIcon::Exclaim), TEXT("3. Клюёт!"));
		AddPic(Steps, PIcon(EFVIcon::Fish), TEXT("4. Улов"));
		B.Para(Touch()
			? TEXT("Подойдите к воде или встаньте на мостки. Зажмите палец на месте — над головой закачается шкала силы. Отпустите в нужный момент: чем сильнее заброс, тем дальше и глубже упадёт поплавок.")
			: TEXT("Подойдите к воде или встаньте на мостки. Зажмите Пробел или ЛКМ — над головой закачается шкала силы. Отпустите в нужный момент: чем сильнее заброс, тем дальше и глубже упадёт поплавок."));
		B.Para(Touch()
			? TEXT("Теперь ждите. Поплавок иногда вздрагивает — это ещё не клёв. Когда над головой вспыхнет «!», сразу коснитесь экрана — это подсечка.")
			: TEXT("Теперь ждите. Поплавок иногда вздрагивает — это ещё не клёв. Когда над головой вспыхнет «!», сразу нажмите — это подсечка."));
		B.Bullet(TEXT("Опоздали — рыба уйдёт, а наживка будет съедена."));
		B.Bullet(TEXT("Мусор цепляется сам, без вываживания, и попадает в рюкзак."));
		B.Bullet(TEXT("Редкая рыба держится глубже — забрасывайте дальше."));
		B.Bullet(Touch() ? TEXT("Передумали? Коснитесь экрана, пока ждёте, — леска смотается.") : TEXT("Передумали? Нажмите, пока ждёте, или Esc — леска смотается."));
		B.Tip(TEXT("Если садок полон, забросить не получится: сначала продайте улов или освободите место."), EFVIcon::Keepnet);
	}

	void BuildReeling(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Вываживание"), TEXT("Держите рыбу в зелёной зоне"), PIcon(EFVIcon::Fish));
		B.MiniBar();
		B.Para(Touch()
			? TEXT("После подсечки рядом с рыбаком появится шкала. Держите палец на экране — зелёная зона поднимается, отпустите — опускается.")
			: TEXT("После подсечки рядом с рыбаком появится шкала. Держите кнопку — зелёная зона поднимается, отпустите — опускается."));
		B.Para(TEXT("Пока рыба внутри зоны, шкала улова справа растёт, снаружи — убывает. Полная шкала — рыба ваша, пустая — сорвалась."));
		B.Bullet(TEXT("Зона скользит по инерции: короткие нажатия лучше долгого удержания."));
		B.Bullet(TEXT("Рыбы плавают по-разному: одни плавно, другие мечутся рывками, третьи тянут на дно или всплывают."));
		B.Bullet(TEXT("С каждым уровнем рыбалки зелёная зона становится больше."));
		if (F.Home()) { B.Bullet(TEXT("Хорошая удочка, леска и еда тоже увеличивают зону и не дают шкале таять.")); }
	}

	void BuildPerfect(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Идеальный улов и сокровища"), TEXT("Бонусы за чистую работу"), PIcon(EFVIcon::Treasure));
		B.Row(PStars(EFVQuality::Gold), TEXT("Идеальный улов"), TEXT("Рыба ни разу не вышла из зелёной зоны? Улов идеальный: качество на ступень выше и больше опыта."));
		B.Row(PIcon(EFVIcon::Treasure), TEXT("Сундук с сокровищем"), TEXT("Иногда во время вываживания на шкале появляется сундук. Наведите на него зону и держите, пока он не откроется: внутри монеты, наживка, снасти, а иногда — редкая одежда."));
		B.Tip(TEXT("Пока вы ловите сундук, рыба может выскользнуть из зоны — следите за обеими шкалами."));
		B.Bullet(F.Home() ? TEXT("Дорогие удочки и некоторые поплавки чаще приносят сундуки.") : TEXT("Дорогие удочки чаще приносят сундуки."));
		if (P) { B.Note(FString::Printf(TEXT("Идеальных поимок: %d · сундуков: %d"), P->Stats.Perfect, P->Stats.Treasures)); }
	}

	void BuildQuality(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Качество и цены"), TEXT("Почему одна рыба дороже другой"), PIcon(EFVIcon::StarGold));
		B.Para(TEXT("У каждой рыбы есть размер и качество. Крупные рыбы чаще бывают серебряными и золотыми, а идеальный улов поднимает качество ещё на ступень."));
		FBlock& Q = B.Pics();
		for (int32 q = 0; q < int32(EFVQuality::Count); ++q)
		{
			const EFVQuality QQ = EFVQuality(q);
			AddPic(Q, PStars(QQ), FString::Printf(TEXT("%s\nцена ×%s"), *FVDB::QualityName(QQ), *FVUI::Num(FVDB::QualityPriceMult(QQ))));
		}
		B.Para(TEXT("Цена зависит и от редкости: обычная, необычная, редкая, эпическая и легендарная рыба ценится всё выше."));
		if (F.CanBuild(EFVStation::Smokehouse)) { B.Bullet(TEXT("Копчёная рыба из домашней коптильни стоит вдвое дороже.")); }
		for (const FFVTraderDef& T : FVDB::Traders())
		{
			if (T.FishSellMult > 1.001f && F.KnowsPlace(T.Location))
			{
				B.Bullet(FString::Printf(TEXT("%s платит за рыбу на %d%% больше."), *T.Name, Pct(T.FishSellMult - 1.f)));
			}
		}
		B.Note(TEXT("Рекорды размера каждого вида хранятся в коллекции."));
	}

	// =================================================================================================================
	// the entries (Снаряжение)
	// =================================================================================================================
	void BuildInventory(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Садок и рюкзак"), TEXT("Где хранятся улов и вещи"), PIcon(EFVIcon::Keepnet));
		const int32 Cap = P ? P->KeepnetCapacity() : 12;
		const int32 Slots = P ? P->ContainerCapacity(EFVContainer::Backpack) : 16;
		B.Row(PIcon(EFVIcon::Keepnet), TEXT("Садок"), FString::Printf(TEXT("Вся пойманная рыба. Сейчас вмещает %d — у Митрича садок можно увеличить."), Cap));
		B.Row(PIcon(EFVIcon::Backpack), TEXT("Рюкзак"), FString::Printf(TEXT("Ресурсы, материалы, еда, снасти, сети и мусор: %d %s. Рюкзак тоже увеличивают у Митрича."),
			Slots, FVUI::Plural(Slots, TEXT("ячейка"), TEXT("ячейки"), TEXT("ячеек"))));
		B.Row(PIcon(EFVIcon::Bait), TEXT("Наживка"), TEXT("Лежит отдельно и места не занимает. Какую наживку насадить, выбирают в инвентаре."));
		B.Para(Touch()
			? TEXT("Кнопка «Инвентарь» открывает то, куда попала последняя находка: рыба — садок, всё остальное — рюкзак. Между ними переключайтесь свайпом вбок или вкладками.")
			: TEXT("Кнопка «Инвентарь» (I или B) открывает то, куда попала последняя находка: рыба — садок, всё остальное — рюкзак. Между ними переключайтесь вкладками, стрелками ←/→ или свайпом мышью."));
		B.Para(TEXT("Там же берут в руки другую удочку, а в рюкзаке можно съесть блюдо или выбросить ненужное."));
		B.Tip(TEXT("Продать улов можно только в лавке — во вкладке «Продать» у прилавка."), EFVIcon::Coin);
	}

	void BuildBait(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Наживка"), TEXT("Быстрее клёв и больше редкой рыбы"), PIcon(EFVIcon::Bait));
		B.Para(TEXT("На каждую поклёвку уходит одна наживка. Без неё тоже клюёт, но медленнее и чаще попадается мусор."));
		const FName Nightcrawler(TEXT("bait_nightcrawler")), Dough(TEXT("bait_dough")), Groundbait(TEXT("bait_groundbait"));
		for (const FFVBaitDef& Bt : FVDB::Baits())
		{
			if (Bt.bLure || Bt.MinLevel > F.Level) { continue; }
			int64 Price = -1;
			FString Source;
			if (Bt.PackPrice > 0 && Bt.TraderMask != 0) { Price = Bt.PackPrice; Source = FString::Printf(TEXT("Пачка ×%d в лавке"), Bt.PackSize); }
			else if (Bt.Id == Nightcrawler) { Source = TEXT("Выкапывается лопатой, растёт в большом червятнике"); }
			else if (Bt.Id == Dough) { if (!F.CanBuild(EFVStation::Kitchen, 1)) { continue; } Source = TEXT("Готовится на кухне"); }
			else if (Bt.Id == Groundbait) { if (!F.CanBuild(EFVStation::Kitchen, 2)) { continue; } Source = TEXT("Готовится на кухне"); }
			else { continue; }
			FString Body = BaitEffect(Bt);
			Body = Body.IsEmpty() ? Source : Body + TEXT("\n") + Source;
			FBlock& R = B.Row(PItem(Bt.Id), Bt.Name, Body);
			R.Price = Price;
			if (P && P->BaitCount(Bt.Id) > 0 && Price < 0) { R.Right = FString::Printf(TEXT("есть %d"), P->BaitCount(Bt.Id)); R.RightColor = InkGood(); }
		}
		if (F.Lures > 0 || (F.Lvl(EFVStation::Workshop) >= 1 && F.CanBuild(EFVStation::Workshop, 2)))
		{
			B.Para(TEXT("Самодельные блёсны и мушки собирают в мастерской блёсен. Каждая особенно нравится определённым рыбам."));
		}
		B.Tip(TEXT("Наживку выбирают в инвентаре. Закончилась — удочка ловит без неё."), EFVIcon::Bait);
	}

	void BuildRods(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Удочки"), TEXT("Чем лучше удочка, тем легче вываживание"), PIcon(EFVIcon::Rod));
		B.Para(TEXT("Удочки продаются в лавках. Самые сильные экспертные удочки куют в «Закалочной кузне», а некоторые продают только после достижения. Купленная удочка сразу оказывается в руках, а прежние можно снова взять в инвентаре."));
		for (const FFVRodDef& R : FVDB::Rods())
		{
			if (R.MinLevel > F.Level) { continue; }
			// 0.6.2 (Р84): every rod names its type; Р80/Р81: a rod that is not plainly sold shows where it comes from
			const bool bOwned = P && P->OwnsRod(R.Id);
			const bool bPlainSold = FVDBU6::IsRodSold(R) && R.RequiredAchievement.IsNone();
			FString Body = FString::Printf(TEXT("Тип «%s» · %s"), *FVDBU6::RodTypeLabel(R), *RodStats(R));
			if (!bOwned && !bPlainSold) { Body += TEXT("\n") + FVDBU6::RodSourceText(R); }
			FBlock& Row = B.Row(PItem(R.Id), R.Name, Body);
			if (bOwned) { Row.Right = TEXT("есть"); Row.RightColor = InkGood(); }
			else if (bPlainSold) { Row.Price = R.Price; }
		}
		B.Sub(TEXT("Что значат свойства"));
		B.Bullet(TEXT("Зона — насколько выше зелёная полоса поимки при вываживании."));
		B.Bullet(TEXT("Подтягивание — как быстро растёт шкала, пока рыба в зоне."));
		B.Bullet(TEXT("Клёв — как скоро рыба находит наживку."));
		B.Bullet(TEXT("Дальность — как далеко и глубоко летит поплавок."));
		B.Bullet(TEXT("Удача — чаще редкая рыба; сундуки — чаще сокровища."));
		if (F.Home()) { B.Tip(TEXT("Леска и поплавок из мастерской добавляют удочке бонусы — загляните в статью «Оснастка»."), EFVIcon::Line); }
	}

	void BuildTackle(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Оснастка: леска и поплавок"), TEXT("Два слота удочки для бонусов"), PIcon(EFVIcon::Line));
		B.Para(TEXT("У каждой удочки есть слоты «Леска» и «Поплавок». Снасти ставят в мастерской дома, во вкладке «Оснастка»: снасть уходит из рюкзака, а прежняя возвращается в него."));
		{
			TArray<FString> Ex;
			for (const FFVRodDef& R : FVDB::Rods())
			{
				if (R.MinLevel > F.Level || Ex.Num() >= 3) { continue; }
				Ex.Add(FString::Printf(TEXT("%s — до %d-го"), *R.Name, R.Tier + 1));
			}
			B.Tip(FString::Printf(TEXT("Ранг снасти не может быть выше номера удочки: %s."), *FString::Join(Ex, TEXT(", "))), EFVIcon::Rod);
		}
		B.Sub(TEXT("Лески"));
		TackleRows(B, F, EFVItemKind::Line);
		B.Sub(TEXT("Поплавки"));
		TackleRows(B, F, EFVItemKind::Float);
		B.Note(TEXT("Без снасти удочка ловит на простую леску и пробковый поплавок — без бонусов."));
		B.Tip(TEXT("Клей, грузила, сети, льняную и плетёную лески, поплавки из пера, скользящий и светящийся в лавках больше не продают — сделайте их в мастерской."), EFVIcon::Hammer);
	}

	FString ExpertStatsLine(const FFVExpertStats& St);   // update 6 (defined with the update-6 entries)

	void BuildFood(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Еда и бонусы"), TEXT("Поешьте перед рыбалкой"), PIcon(EFVIcon::Food));
		B.Para(TEXT("Блюда с кухни на несколько игровых часов помогают на рыбалке. Действует одно блюдо: новое заменяет прежнее. Съесть блюдо можно в инвентаре, во вкладке «Рюкзак»."));
		for (const FFVItemDef* It : FVDB::ItemsOfKind(EFVItemKind::Food))
		{
			if (!It || It->MinLevel > F.Level) { continue; }
			const FFVRecipeDef* Rcp = nullptr;
			for (const FFVRecipeDef& R : FVDB::Recipes()) { if (R.OutputId == It->Id) { Rcp = &R; break; } }
			if (Rcp && !RecipeKnown(F, *Rcp)) { continue; }
			FString Body = FString::Join(FVDB::StatModsLines(It->Buff), TEXT(" · "));
			{
				const FString Ex = ExpertStatsLine(It->ExpertBuff);   // update 6: expert buffs (only in the expert fight)
				if (!Ex.IsEmpty()) { Body += (Body.IsEmpty() ? FString() : FString(TEXT("\n"))) + FString::Printf(TEXT("Эксперт: %s"), *Ex); }
			}
			if (It->BuffMinutes > 0.f) { Body += FString::Printf(TEXT("\nДействует %s"), *UFVGame::DurationText(It->BuffMinutes)); }
			FBlock& R = B.Row(PItem(It->Id), It->Name, Body);
			if (Rcp)
			{
				R.Right = FVDB::StationLevelName(EFVStation::Kitchen, FMath::Max(1, Rcp->StationLevel));
				R.RightColor = F.Lvl(EFVStation::Kitchen) >= FMath::Max(1, Rcp->StationLevel) ? InkGood() : FVPal::InkSoft();
			}
		}
		B.Para(TEXT("Качество блюда зависит от мини-игры на кухне: серебро, золото и иридий дают эффект сильнее и дольше."));
		B.Para(TEXT("Для ухи нужна морковь — она растёт только в огороде. Блюда с пометкой «Эксперт» помогают лишь в экспертной рыбалке."));   // update 6 (Р41, Р42)
		B.Tip(TEXT("Значок активного блюда и оставшееся время видны рядом с часами."), EFVIcon::Clock);
	}

	// =================================================================================================================
	// the entries (Мир)
	// =================================================================================================================
	void BuildTravel(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Карта и поездки"), TEXT("Новые места — новая рыба"), PIcon(EFVIcon::Map));
		B.Para(TEXT("На карте можно отправиться в любое открытое место. Новые места открываются за монеты, когда хватает уровня рыбалки. Дом открыт всегда."));
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			if (L.MinLevel > F.Level) { continue; }
			FPic Pic = PIcon(EFVIcon::Map);
			for (const FFVFishDef* Fi : FVDB::FishAt(L.Id))
			{
				if (!Fi || Fi->bLegendary) { continue; }
				const FFVFishRecord* Rec = P ? P->GetRecord(Fi->Id) : nullptr;
				Pic = PFish(Fi->Id, !(Rec && Rec->Caught > 0));
				break;
			}
			FBlock& R = B.Row(Pic, L.Name, L.Description);
			if (P && P->IsLocationUnlocked(L.Id)) { R.Right = TEXT("открыто"); R.RightColor = InkGood(); }
			else if (L.UnlockPrice > 0) { R.Price = L.UnlockPrice; }
		}
		{
			const FFVLocationDef& Home = FVDB::HomeLocation();
			FBlock& R = B.Row(PIcon(EFVIcon::House), Home.Name.IsEmpty() ? FString(TEXT("Дом")) : Home.Name, TEXT("Двор с участками под постройки и дом со станками, сундуком и кроватью."));
			R.Right = TEXT("всегда");
			R.RightColor = InkGood();
		}
		B.Tip(Touch()
			? TEXT("Коснитесь булавки на карте или смахните карточку места вбок, чтобы посмотреть соседние места.")
			: TEXT("Кликните по булавке, нажмите ←/→ или перетащите карточку места вбок, чтобы посмотреть соседние места."), EFVIcon::Map);
	}

	void BuildTrade(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Лавки и торговля"), TEXT("Где продать улов и купить снасти"), PIcon(EFVIcon::Shop));
		for (const FFVTraderDef& T : FVDB::Traders())
		{
			if (!F.KnowsPlace(T.Location)) { continue; }
			const FFVLocationDef* L = FVDB::FindLocation(T.Location);
			TArray<FString> Wares = { TEXT("удочки"), TEXT("наживка"), TEXT("снасти") };
			if (T.bSellsClothes) { Wares.Add(TEXT("одежда")); }
			if (T.bSellsUpgrades) { Wares.Add(TEXT("улучшения садка и рюкзака")); }
			FString Body = FString::Printf(TEXT("«%s»%s. Здесь %s."), *T.ShopTitle, L ? *FString::Printf(TEXT(" — %s"), *L->Name) : TEXT(""), *FString::Join(Wares, TEXT(", ")));
			TArray<FString> Better;
			if (T.FishSellMult > 1.001f) { Better.Add(FString::Printf(TEXT("за рыбу — на %d%%"), Pct(T.FishSellMult - 1.f))); }
			if (T.ItemSellMult > 1.001f) { Better.Add(FString::Printf(TEXT("за вещи — на %d%%"), Pct(T.ItemSellMult - 1.f))); }
			if (Better.Num() > 0) { Body += FString::Printf(TEXT(" Платит больше: %s."), *FString::Join(Better, TEXT(", "))); }
			B.Row(PStall(T.Id), T.Name, Body);
		}
		B.Para(Touch()
			? TEXT("Подойдите к прилавку — он подсветится — и коснитесь экрана. Вкладки лавки переключаются свайпом вбок.")
			: TEXT("Подойдите к прилавку — он подсветится — и кликните или нажмите E. Вкладки лавки переключаются кнопками, стрелками ←/→ или свайпом мышью."));
		B.Para(TEXT("Продажа — во вкладке «Продать»: по одной, стопкой или весь улов сразу."));
		B.Tip(TEXT("Не спешите продавать всё подряд: рыба пригодится для готовки и заказов."), EFVIcon::Coin);
	}

	void BuildTime(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("День, ночь и погода"), TEXT("Время влияет на клёв"), PIcon(EFVIcon::Clock));
		FBlock& T = B.Pics();
		AddPic(T, PIcon(EFVIcon::Sun), TEXT("Утро\n6–12"));
		AddPic(T, PIcon(EFVIcon::Sun), TEXT("День\n12–18"));
		AddPic(T, PIcon(EFVIcon::Moon), TEXT("Вечер\n18–22"));
		AddPic(T, PIcon(EFVIcon::Moon), TEXT("Ночь\n22–6"));
		AddPic(T, PIcon(EFVIcon::Rain), TEXT("Дождь"));
		B.Para(TEXT("Игровой час длится полминуты. Время идёт, только пока вы в мире: в окнах и меню часы стоят."));
		B.Para(TEXT("Многие рыбы клюют лишь в своё время суток или только в дождь. Когда клюёт рыба, видно в коллекции и на карте."));
		B.Bullet(TEXT("В полночь начинается новый день и меняется погода: чаще ясно, иногда дождь."));
		B.Bullet(TEXT("Дома можно лечь спать и проснуться в 6 утра."));
		if (F.CanBuild(EFVStation::Radio))
		{
			const FFVStationDef& Def = FVDB::Station(EFVStation::Radio);
			FBlock& R = B.Row(PStation(EFVStation::Radio, 1), Def.Name, TEXT("Прогноз погоды на завтра и подсказки, какая рыба ждёт дождя или ночи."));
			if (F.Lvl(EFVStation::Radio) > 0) { R.Right = TEXT("построено"); R.RightColor = InkGood(); }
			else if (Def.Levels.Num() > 0) { R.Price = Def.Levels[0].Price; }
		}
	}

	void BuildGather(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Сбор ресурсов"), TEXT("Ветки, камешки, ягоды и другое"), PItem(TEXT("res_branch")));
		B.Para(FString::Printf(TEXT("На каждой локации и во дворе дома лежат ресурсы. Подойдите — ресурс подсветится — и %s. Всё собранное попадает в рюкзак."), *UseWord()));
		FBlock& R = B.Pics();
		for (const FFVItemDef* It : FVDB::ItemsOfKind(EFVItemKind::Resource))
		{
			if (It && It->MinLevel <= F.Level) { AddPic(R, PItem(It->Id), It->Name); }
		}
		struct FWhere { const TCHAR* Loc; const TCHAR* Text; };
		static const FWhere Where[] = {
			{ TEXT("pond"), TEXT("у пруда — ветки, волокно и тростник") },
			{ TEXT("river"), TEXT("у реки — глина и грибы") },
			{ TEXT("mountain"), TEXT("в горах — камешки и смола") },
			{ TEXT("ocean"), TEXT("у моря — перья") },
			{ TEXT("swamp"), TEXT("на болоте — тростник, грибы и глина") },
			{ TEXT("volcano"), TEXT("у вулкана — камешки и смола") } };
		TArray<FString> Parts;
		for (const FWhere& W : Where) { if (F.KnowsPlace(FName(W.Loc))) { Parts.Add(W.Text); } }
		Parts.Add(TEXT("во дворе дома — ягоды, грибы и перья"));
		B.Bullet(FString::Printf(TEXT("Каждый день ресурсы появляются заново: %s."), *FString::Join(Parts, TEXT("; "))));
		B.Bullet(TEXT("Ресурсы нужны для построек, материалов, снастей и еды."));
		B.Tip(TEXT("Рюкзак полон? Отнесите лишнее в сундук дома."), EFVIcon::Backpack);
	}

	void BuildWorms(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Черви: лопата и червятник"), TEXT("Бесплатная наживка своими руками"), PIcon(EFVIcon::Shovel));
		{
			TArray<FString> Places = { TEXT("у пруда") };
			if (F.KnowsPlace(FName(TEXT("river")))) { Places.Add(TEXT("у реки")); }
			if (F.KnowsPlace(FName(TEXT("swamp")))) { Places.Add(TEXT("на болоте")); }
			Places.Add(TEXT("в компостной куче во дворе"));
			B.Para(FString::Printf(TEXT("Мягкая земля есть %s. Нужна лопата — её продаёт Митрич. Каждое место копают раз в день."), *FString::Join(Places, TEXT(", "))));
		}
		B.Para(Touch()
			? TEXT("Копка — мини-игра на время: касайтесь клеток, пока идёт таймер. Выглянул червяк — хватайте, пока не спрятался! Не тыкайте в пустые ямки — каждая отнимает полсекунды.")
			: TEXT("Копка — мини-игра на время: кликайте по клеткам, пока идёт таймер. Выглянул червяк — хватайте, пока не спрятался! Не тыкайте в пустые ямки — каждая отнимает полсекунды."));
		const TArray<FFVShovelDef>& Shovels = FVDB::Shovels();
		for (int32 i = 0; i < Shovels.Num(); ++i)
		{
			const FFVShovelDef& S = Shovels[i];
			if (S.MinLevel > F.Level && F.Shovel < i + 1) { continue; }
			// update 5: the dig times have tenths (6,4 / 8,8 / 11,2 / 14,4 s)
			FString Body = FString::Printf(TEXT("%s с на копку"), *FVUI::Num(S.DigSeconds, 1));
			if (S.WorkshopLevel > 0 && F.CanBuild(EFVStation::Workshop, S.WorkshopLevel)) { Body += TEXT(" · можно сделать в мастерской"); }
			FBlock& R = B.Row(PShovel(i + 1), S.Name, Body);
			if (F.Shovel >= i + 1) { R.Right = TEXT("есть"); R.RightColor = InkGood(); }
			else if (S.Price > 0) { R.Price = S.Price; }
		}
		B.Bullet(TEXT("В земле попадаются выползки, глина, камешки и даже мусор."));
		if (F.CanBuild(EFVStation::WormFarm))
		{
			const FFVStationDef& Def = FVDB::Station(EFVStation::WormFarm);
			B.Sub(Def.Name);
			B.Para(Def.Description + TEXT(" Соберите их — черви сразу попадут в наживку."));
			LevelRows(B, F, EFVStation::WormFarm);
		}
	}

	void BuildNets(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Сети"), TEXT("Ловят рыбу, пока вас нет"), PIcon(EFVIcon::Net));
		B.Para(TEXT("На берегу многих мест есть колышки для сетей. Поставьте сеть из рюкзака — она сама ловит рыбу, даже пока вы спите. Заглядывайте за уловом: полная сеть больше не ловит."));
		B.Para(TEXT("Сети изнашиваются, а крупная и редкая рыба рвёт их сильнее. Порванная сеть не ловит — почините её верёвкой и волокном."));
		for (const FFVItemDef* It : FVDB::ItemsOfKind(EFVItemKind::Net))
		{
			if (!It || It->MinLevel > F.Level) { continue; }
			FString Body = FString::Printf(TEXT("Вмещает %d · прочность %d · рыба примерно раз в %s"), It->NetCapacity, FMath::RoundToInt(It->NetDurability),
				*UFVGame::DurationText(It->NetMinutesPerFish));
			if (IsCraftable(F, It->Id)) { Body += TEXT("\nМожно сплести в мастерской"); }
			else if ((It->BuyPrice <= 0 || It->TraderMask == 0) && HasAnyRecipe(It->Id)) { Body += TEXT("\nНе продаётся — сплетите в мастерской"); }
			FBlock& R = B.Row(PItem(It->Id), It->Name, Body);
			if (It->BuyPrice > 0 && It->TraderMask != 0) { R.Price = It->BuyPrice; }
		}
		B.Bullet(TEXT("Снять сеть можно, только если она почти целая и пустая."));
		B.Bullet(TEXT("В дождь и ночью сети ловят чуть лучше."));
	}

	void BuildOrders(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Доска заказов"), TEXT("Выполняйте просьбы жителей"), PIcon(EFVIcon::Order));
		B.Para(TEXT("Доска стоит у пруда, рядом с лавкой. Каждый день на ней появляются новые заказы: рыба, ресурсы, материалы или еда."));
		B.Row(PIcon(EFVIcon::Coin), TEXT("Залог"), TEXT("Взяв заказ, вы оставляете залог. Успели до срока — получаете награду и залог обратно."));
		B.Row(PIcon(EFVIcon::Clock), TEXT("Срок"), TEXT("Опоздали — заказ сорван, а залог пропадает."));
		B.Bullet(TEXT("Одновременно можно держать до трёх заказов."));
		B.Bullet(TEXT("Иногда кроме монет дают полезную вещь."));
		B.Tip(TEXT("Берите заказы, которые успеете выполнить: срок идёт, даже пока вы спите."), EFVIcon::Order);
	}

	// =================================================================================================================
	// the entries (Дом и ремёсла)
	// =================================================================================================================
	void BuildHome(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Дом и станции"), TEXT("Ваше хозяйство"), PIcon(EFVIcon::House));
		B.Para(FString::Printf(TEXT("Дом есть на карте. Во дворе — ресурсы и компостная куча с червями, в доме — станции. Подойдите к станции и %s, чтобы построить, улучшить или открыть её."), *UseWord()));
		B.Para(TEXT("Постройка стоит монет и материалов. С каждым уровнем станции открываются новые рецепты и возможности."));
		int32 Hidden = 0;
		for (int32 s = 0; s < int32(EFVStation::Count); ++s)
		{
			const EFVStation S = EFVStation(s);
			if (!F.CanBuild(S)) { ++Hidden; continue; }
			const FFVStationDef& Def = FVDB::Station(S);
			const int32 Lvl = F.Lvl(S);
			FBlock& R = B.Row(PStation(S, FMath::Max(1, Lvl)), Def.Name, Def.Description);
			if (Lvl > 0) { R.Right = FVDB::StationLevelName(S, Lvl); R.RightColor = InkGood(); }
			else if (Def.Levels.Num() > 0 && Def.Levels[0].Price > 0) { R.Price = Def.Levels[0].Price; }
		}
		if (Hidden > 0) { B.Note(TEXT("С ростом уровня рыбалки в доме появятся новые станции.")); }
	}

	void BuildBed(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Кровать и сундук"), TEXT("Отдых и хранение вещей"), PStation(EFVStation::Bed, 1));
		TArray<FString> Night;
		if (F.Level >= F.MinNetLevel() || F.NetsPlaced > 0) { Night.Add(TEXT("сети ловят рыбу")); }
		if (F.CanBuild(EFVStation::Smokehouse)) { Night.Add(TEXT("коптильня коптит")); }
		if (F.CanBuild(EFVStation::WormFarm)) { Night.Add(TEXT("в червятнике заводятся черви")); }
		FString Sleep = TEXT("Сон до 6 утра.");
		if (Night.Num() > 0) { Sleep += FString::Printf(TEXT(" Пока вы спите, %s."), *FString::Join(Night, TEXT(", "))); }
		Sleep += TEXT(" Утром — сводка новостей.");
		Sleep += TEXT("\nПоспать можно два раза подряд, потом кровать ждёт вечера: снова лечь можно после 18:00.");   // update 6 (Р44)
		B.Row(PStation(EFVStation::Bed, 1), FVDB::Station(EFVStation::Bed).Name, Sleep);
		const int32 ChestCap = P ? FMath::Max(1, P->StationCapacity(EFVStation::Chest)) : 40;
		B.Row(PStation(EFVStation::Chest, 1), FVDB::Station(EFVStation::Chest).Name,
			FString::Printf(TEXT("Хранит вещи из рюкзака: %d %s. Рыбу сюда положить нельзя."), ChestCap, FVUI::Plural(ChestCap, TEXT("ячейка"), TEXT("ячейки"), TEXT("ячеек"))));
		if (F.CanBuild(EFVStation::IceCellar))
		{
			const FFVStationDef& Def = FVDB::Station(EFVStation::IceCellar);
			B.Sub(Def.Name);
			B.Para(Def.Description);
			LevelRows(B, F, EFVStation::IceCellar);
		}
		B.Tip(TEXT("Когда вы ложитесь спать, игра сохраняется."), EFVIcon::Bed);
	}

	void BuildKitchen(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVStationDef& Def = FVDB::Station(EFVStation::Kitchen);
		B.Head(Def.Name, TEXT("Еда с бонусами, тесто и прикормка"), PStation(EFVStation::Kitchen, FMath::Max(1, F.Lvl(EFVStation::Kitchen))));
		B.Para(Def.Description);
		B.Para(Touch()
			? TEXT("Готовка — мини-игра. Сначала разрежьте рыбу, проводя пальцем поперёк по линиям. Потом ловите момент: переверните и снимите блюдо, пока стрелка в зелёной зоне. Чем точнее, тем выше качество.")
			: TEXT("Готовка — мини-игра. Сначала разрежьте рыбу: проведите мышью поперёк линий или жмите Пробел, когда нож над линией. Потом ловите момент: переверните и снимите блюдо, пока стрелка в зелёной зоне. Чем точнее, тем выше качество."));
		// update 5 (§10.2): the fried fish by rarity
		B.Para(TEXT("Чем реже рыба, тем сложнее блюдо: редкая, эпическая и легендарная рыба жарятся отдельно — больше надрезов, стрелка бежит быстрее, зато и сил блюдо даёт больше."));
		B.Sub(TEXT("Уровни кухни"));
		LevelRows(B, F, EFVStation::Kitchen);
		B.Sub(TEXT("Рецепты"));
		RecipeRows(B, F, EFVStation::Kitchen, [](const FFVRecipeDef&) { return true; });
		B.Tip(TEXT("«Любая рыба» — берётся самая дешёвая рыба из садка (копчёная — никогда)."), EFVIcon::Fish);
	}

	void BuildWorkshop(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Мастерская и материалы"), TEXT("Материалы, снасти и переработка"), PStation(EFVStation::Workshop, FMath::Max(1, F.Lvl(EFVStation::Workshop))));
		B.Para(TEXT("В мастерской из ресурсов делают материалы: доски, верёвку, клей и грузила. Они нужны для построек, снастей и сетей."));
		B.Sub(TEXT("Уровни мастерской"));
		LevelRows(B, F, EFVStation::Workshop);
		B.Sub(TEXT("Материалы"));
		RecipeRows(B, F, EFVStation::Workshop, [](const FFVRecipeDef& R)
		{
			const FFVItemDef* Out = FVDB::FindItem(R.OutputId);
			return !R.bRecycle && Out && Out->Kind == EFVItemKind::Material;
		});
		B.Sub(TEXT("Переработка мусора"));
		B.Para(TEXT("Мусор из рюкзака не пропадёт даром:"));
		RecipeRows(B, F, EFVStation::Workshop, [](const FFVRecipeDef& R) { return R.bRecycle; });
		B.Note(FString::Printf(TEXT("Ещё здесь ставят оснастку на удочки%s."), F.CanBuild(EFVStation::Workshop, 2) ? TEXT(" и делают лопаты") : TEXT("")));
	}

	void BuildKnots(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Плетение узлов"), TEXT("Лески и сети — мини-игра"), PIcon(EFVIcon::Knot));
		B.Para(TEXT("Лески и сети плетут в мастерской. Сначала смотрите: стрелки загораются по очереди. Потом повторите узор."));
		B.Para(Touch()
			? TEXT("Повторяйте смахиванием в нужную сторону или большими кнопками со стрелками.")
			: TEXT("Повторяйте стрелками или WASD, кликом по кнопкам со стрелками или смахиванием мышью."));
		B.Bullet(TEXT("Ошибка распускает узел — узор покажут снова. Попыток три."));
		B.Bullet(TEXT("Не вышло — материалы остаются целы."));
		B.Bullet(TEXT("Чем длиннее снасть, тем больше петель в узоре."));
		B.Sub(TEXT("Что можно сплести"));
		if (RecipeRows(B, F, EFVStation::Workshop, [](const FFVRecipeDef& R) { return R.Game == EFVCraftGame::Knots; }) == 0)
		{
			B.Note(TEXT("Рецепты появятся с ростом уровня."));
		}
		if (P) { B.Note(FString::Printf(TEXT("Завязано узлов: %d"), P->Stats.KnotsTied)); }
	}

	void BuildLures(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		constexpr int32 Total = FVDB::LureBases * FVDB::LureColors * FVDB::LureGlitters;
		B.Head(TEXT("Мастерская блёсен"), TEXT("Соберите приманку для нужной рыбы"), PLure(0, 1, 1));
		B.Para(FString::Printf(TEXT("На верстаке (мастерская 2-го уровня) из основы, цвета и блёсток собирают блёсны и мушки — всего %d %s. Одна сборка даёт %d шт."),
			Total, FVUI::Plural(Total, TEXT("рецепт"), TEXT("рецепта"), TEXT("рецептов")), FVDB::LurePackSize()));
		FBlock& Bases = B.Pics();
		for (uint8 b = 0; b < FVDB::LureBases; ++b) { AddPic(Bases, PLure(b, 0, 0), FVDB::LurePartName(0, b)); }
		FBlock& Colors = B.Pics();
		for (uint8 c = 0; c < FVDB::LureColors; ++c) { AddPic(Colors, PLure(0, c, 0), FVDB::LurePartName(1, c)); }
		FBlock& Glitters = B.Pics();
		for (uint8 g = 0; g < FVDB::LureGlitters; ++g) { AddPic(Glitters, PLure(0, 0, g), FVDB::LurePartName(2, g)); }
		B.Bullet(TEXT("Основа решает, кому понравится движение: блесна — рыбам с плавным ходом, мушка — вертким и всплывающим."));
		B.Bullet(TEXT("Цвет приманивает рыб определённой формы, а блёстки повышают шанс на редкую и крупную рыбу."));
		B.Bullet(TEXT("Нужные материалы видно прямо в мастерской: металлолом, перья, ресурсы для цвета и блёстки."));
		B.Para(TEXT("После первой сборки рецепт открыт — и видно, кто на него клюёт лучше всего."));
		if (P) { B.Note(FString::Printf(TEXT("Открыто рецептов: %d из %d"), F.Lures, Total)); }
	}

	void BuildSmoke(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVStationDef& Def = FVDB::Station(EFVStation::Smokehouse);
		B.Head(Def.Name, TEXT("Копчёная рыба — вдвое дороже"), PStation(EFVStation::Smokehouse, FMath::Max(1, F.Lvl(EFVStation::Smokehouse))));
		B.Para(TEXT("Загрузите в коптильню рыбу из садка и подбросьте дров — веток из рюкзака. Рыба коптится 6 игровых часов, пока горит огонь. Готовую рыбу заберите: она вернётся в садок уже копчёной."));
		LevelRows(B, F, EFVStation::Smokehouse);
		B.Bullet(TEXT("Кончились дрова — копчение замирает, но рыба не портится."));
		B.Bullet(TEXT("Копчёную рыбу часто просят в заказах."));
		B.Tip(TEXT("Коптильня работает и ночью — загрузите её перед сном."), EFVIcon::Smoke);
	}

	void BuildAquarium(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVStationDef& Def = FVDB::Station(EFVStation::Aquarium);
		B.Head(Def.Name, TEXT("Живые рыбки у вас дома"), PStation(EFVStation::Aquarium, FMath::Max(1, F.Lvl(EFVStation::Aquarium))));
		B.Para(TEXT("Выпустите рыбу из садка в аквариум — она будет плавать у вас дома. Вернуть её в садок можно в любой момент."));
		LevelRows(B, F, EFVStation::Aquarium);
		B.Note(TEXT("Бутылки для аквариума часто попадаются на крючок вместе с мусором."));
	}

	void BuildTrophy(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVStationDef& Def = FVDB::Station(EFVStation::TrophyWall);
		B.Head(Def.Name, TEXT("Чучела рекордных рыб"), PStation(EFVStation::TrophyWall, 1));
		const FString Cost = P ? InputsText(P->TrophyCost()) : FString(TEXT("доска и клей"));
		B.Para(FString::Printf(TEXT("Для чучела нужны %s и рыба этого вида из садка. Размер чучела — ваш рекорд для этого вида."), *Cost));
		const int32 Plaques = Def.Levels.Num() > 0 ? FMath::Max(1, Def.Levels[0].Capacity) : 6;
		B.Bullet(FString::Printf(TEXT("На стене %d %s — по одной на вид."), Plaques, FVUI::Plural(Plaques, TEXT("табличка"), TEXT("таблички"), TEXT("табличек"))));
		B.Bullet(TEXT("Поймали рыбу крупнее — чучело можно заменить."));
		LevelRows(B, F, EFVStation::TrophyWall);
	}

	void BuildDecor(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		FPic Icon = PIcon(EFVIcon::House);
		for (const FFVDecorDef& Dc : FVDB::Decor()) { if (Dc.Price > 0 && Dc.Slot == EFVDecorSlot::Lamp) { Icon = PItem(Dc.Id); break; } }
		B.Head(TEXT("Уют: декор"), TEXT("Обои, ковры, лампы и другое"), Icon);
		B.Para(FString::Printf(TEXT("Комод в доме — это каталог декора: обои, пол, ковры, светильники, растения, картины и шторы. Подойдите к комоду и %s."), *UseWord()));
		FBlock& Pc = B.Pics();
		int32 N = 0;
		for (const FFVDecorDef& Dc : FVDB::Decor())
		{
			if (Dc.Price <= 0 || Dc.MinLevel > F.Level || N >= 8) { continue; }
			AddPic(Pc, PItem(Dc.Id), Dc.Name);
			++N;
		}
		if (N == 0) { E.Blocks.RemoveAt(E.Blocks.Num() - 1); }
		B.Bullet(TEXT("Купленная вещь сразу появляется в комнате, и её можно сменить в любой момент."));
		B.Bullet(TEXT("Некоторые вещи не продаются — их дают за достижения."));
		B.Bullet(TEXT("Светильники по вечерам освещают комнату."));
	}

	// =================================================================================================================
	// the entries (Коллекции)
	// =================================================================================================================
	void BuildCollection(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Коллекция рыб"), TEXT("Виды, рекорды и где кто клюёт"), PIcon(EFVIcon::Book));
		B.Para(TEXT("В коллекции — каждый вид рыбы: рекорд размера, лучшее качество и сколько поймано. Непойманные рыбы видны как силуэты."));
		B.Para(FV::IsPortrait()
			? TEXT("Кнопки мест и видов — внизу, под большим портретом рыбы. Выберите рыбу, чтобы узнать, где, когда и в какую погоду она клюёт.")
			: TEXT("Выберите место и рыбу, чтобы узнать, где, когда и в какую погоду она клюёт."));
		if (P)
		{
			FBlock& Pc = B.Pics();
			int32 N = 0;
			for (const FFVFishDef& Fi : FVDB::Fish())
			{
				const FFVFishRecord* Rec = P->GetRecord(Fi.Id);
				if (!Rec || Rec->Caught <= 0 || N >= 8) { continue; }
				AddPic(Pc, PFish(Fi.Id, false), Fi.Name);
				++N;
			}
			if (N == 0) { E.Blocks.RemoveAt(E.Blocks.Num() - 1); }
			B.Note(FString::Printf(TEXT("Поймано видов: %d из %d"), F.Species, FVDB::Fish().Num()));
		}
		B.Tip(TEXT("Поймайте все виды одного места — получите достижение и кубок на карте."), EFVIcon::Trophy);
	}

	/** Update 5: «Легендарные рыбы» — the golden bar, «не клюёт» vs «обрывает поводок», the workshop rigging, a second rod. */
	void BuildLegends(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		auto Caught = [P](FName Id) { const FFVFishRecord* R = P ? P->GetRecord(Id) : nullptr; return R && R->Caught > 0; };
		const FFVFishDef* Carp = FVDB::FindLegend(EFVLegend::TsarCarp);
		B.Head(TEXT("Легендарные рыбы"), TEXT("Золотая полоса, подготовка и чек-лист"), Carp ? PFish(Carp->Id, !Caught(Carp->Id)) : PIcon(EFVIcon::Trophy));
		B.Para(TEXT("В каждом месте живёт своя легенда — огромная, редкая и хитрая. Когда она на крючке, полоса поимки становится золотой. Полоса ярко горит, пока рыба внутри, и тускнеет, когда рыба снаружи: держите легенду в золотой полосе!"));
		FBlock& Pc = B.Pics();
		int32 N = 0;
		for (uint8 L = 1; L < uint8(EFVLegend::Count); ++L)
		{
			const FFVFishDef* Leg = FVDB::FindLegend(EFVLegend(L));
			if (!Leg) { continue; }
			const FFVLocationDef* Loc = FVDB::FindLocation(Leg->Location);
			const bool bKnown = Caught(Leg->Id);
			AddPic(Pc, PFish(Leg->Id, !bKnown), FString::Printf(TEXT("%s\n%s"), bKnown ? *Leg->Name : TEXT("???"), Loc ? *Loc->Name : TEXT("")));
			++N;
		}
		if (N == 0) { E.Blocks.RemoveAt(E.Blocks.Num() - 1); }
		B.Para(TEXT("У каждой легенды свои приёмы в бою, и чем дальше место, тем сложнее бой. Описание приёмов появится в коллекции после первой встречи."));
		B.Sub(TEXT("Подготовка"));
		B.Bullet(TEXT("«Не клюёт»: без обязательной снасти — нужной наживки, поплавка или особой лески — легенда даже не подходит к крючку."));
		B.Bullet(TEXT("«Обрывает поводок»: легенда клюёт, но слабая леска не выдерживает, и поводок рвётся в начале боя. Леска на удочке цела, пропадает только наживка."));
		B.Bullet(TEXT("Леску и поплавок ставят только в Мастерской: «Мастерская → Оснастка». Первая легенда, которой это нужно, — Серебряный осётр (с 4-го уровня): ему нужна плетёная леска или крепче."));
		B.Bullet(TEXT("Оснастка у каждой удочки своя. Под легенду удобно держать вторую удочку — например, с шёлковой леской для Хрустальной форели."));
		B.Tip(TEXT("Чек-лист подготовки с вашей снастью — в коллекции рыб, в подсказке рыбы на карте и на экране уровней."), EFVIcon::Check);
		if (P)
		{
			int32 Legends = 0;
			for (uint8 L = 1; L < uint8(EFVLegend::Count); ++L)
			{
				const FFVFishDef* Leg = FVDB::FindLegend(EFVLegend(L));
				if (Leg && Caught(Leg->Id)) { ++Legends; }
			}
			B.Note(FString::Printf(TEXT("Поймано легенд: %d из %d. За легенду каждого места — достижение и свой поплавок."), Legends, int32(EFVLegend::Count) - 1));
		}
	}

	void BuildAchievements(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Достижения"), TEXT("Награды за успехи"), PIcon(EFVIcon::Trophy));
		B.Para(TEXT("Достижения выдаются за улов, редкие виды, идеальные поимки, заработок, постройки и ремёсла. За них дают монеты, а иногда — одежду или декор для дома."));
		B.Para(Touch()
			? TEXT("Вкладки «Все», «В процессе» и «Выполнены» переключаются свайпом вбок. У каждого достижения видно, сколько осталось.")
			: TEXT("Вкладки «Все», «В процессе» и «Выполнены» переключаются кнопками, стрелками ←/→ или свайпом. У каждого достижения видно, сколько осталось."));
		if (P) { B.Note(FString::Printf(TEXT("Выполнено: %d из %d"), F.Achievements, FVDB::Achievements().Num())); }
	}

	// =================================================================================================================
	// UPDATE 6 [SCREEN-MISC] (Р43, §12.3): the entries of the forest, the yard, the City and the expert zone.
	// Numbers come from the data (FVDBU6), never hard-coded.
	// =================================================================================================================
	FString TreeKindName(EFVTreeKind K)
	{
		switch (K)
		{
		case EFVTreeKind::Birch: return TEXT("берёза");
		case EFVTreeKind::Pine: return TEXT("сосна");
		case EFVTreeKind::Oak: return TEXT("дуб");
		case EFVTreeKind::Spruce: return TEXT("вековая ель");
		default: return TEXT("дерево");
		}
	}

	FString ToolCraftText(const FFVToolDef& T)
	{
		TArray<FString> Parts;
		if (T.TraderMask != 0 && T.Price > 0) { Parts.Add(FString::Printf(TEXT("в лавке за %s"), *FV::Money(T.Price))); }
		if (T.bForge) { Parts.Add(T.WorkshopLevel > 1 ? FString::Printf(TEXT("кузня ур. %d"), T.WorkshopLevel) : FString(TEXT("кузня"))); }
		else if (T.WorkshopLevel > 0) { Parts.Add(FString::Printf(TEXT("мастерская ур. %d"), T.WorkshopLevel)); }
		FString S = FString::Join(Parts, TEXT(" · "));
		if (T.Craft.Num() > 0) { S += FString::Printf(TEXT("\nНужно: %s"), *InputsText(T.Craft)); }
		return S;
	}

	FString ExpertStatsLine(const FFVExpertStats& St)
	{
		TArray<FString> Parts;
		auto Add = [&Parts](const TCHAR* N, float V) { if (!FMath::IsNearlyEqual(V, 1.f, 0.001f)) { Parts.Add(FString::Printf(TEXT("%s ×%s"), N, *FVUI::Num(V))); } };
		Add(TEXT("сила"), St.Force);
		Add(TEXT("размер зоны"), St.ZoneSize);
		Add(TEXT("прочность"), St.LineStrength);
		Add(TEXT("устойчивость"), St.Stability);
		return FString::Join(Parts, TEXT(" · "));
	}

	/** Rows of the recipes of a yard building. */
	void YardRecipeRows(FB& B, const FFacts& F, EFVYardBuilding Y)
	{
		for (const FFVRecipeDef* R : FVDBU6::RecipesForYard(Y))
		{
			if (!R || R->MinLevel > F.Level + 1) { continue; }
			FString Body = FString::Printf(TEXT("Нужно: %s"), *InputsText(R->Inputs));
			if (R->bSawGame) { Body += TEXT("\nМини-игра «Распил»: чем ровнее рез, тем больше досок."); }
			else if (R->bForgeGame) { Body += TEXT("\nМини-игра «Ковка»: бейте молотом в ритм, пока металл светится."); }
			FBlock& Row = B.Row(PItem(R->OutputId), FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(R->OutputId), FMath::Max(1, R->OutputCount)), Body);
			Row.Right = FString::Printf(TEXT("ур. %d"), FMath::Max(1, R->MinLevel));
			Row.RightColor = R->MinLevel <= F.Level ? InkGood() : FVPal::InkSoft();
		}
	}

	void YardLevelRows(FB& B, const FFacts& F, EFVYardBuilding Y)
	{
		const FFVYardBuildingDef& Def = FVDBU6::YardBuilding(Y);
		const int32 Cur = F.Yard[FMath::Clamp(int32(Y), 0, int32(EFVYardBuilding::Count) - 1)];
		for (int32 i = 0; i < Def.Levels.Num(); ++i)
		{
			const FFVStationLevel& L = Def.Levels[i];
			FString Body = L.Description;
			if (Cur < i + 1 && L.Materials.Num() > 0) { Body += FString::Printf(TEXT("\nНужно: %s · с %d ур."), *InputsText(L.Materials), L.MinLevel); }
			FBlock& R = B.Row(PIcon(i == 0 ? EFVIcon::Hammer : EFVIcon::Level), L.Name.IsEmpty() ? Def.Name : L.Name, Body);
			if (Cur >= i + 1) { R.Right = TEXT("построено"); R.RightColor = InkGood(); }
			else if (L.Price > 0) { R.Price = L.Price; }
		}
	}

	void BuildBigMap(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Большие места и карта"), TEXT("Камера идёт за вами"), PIcon(EFVIcon::Map));
		B.Para(TEXT("Лес, двор, дом, Город и Пещера больше экрана: камера идёт за рыбаком, за краем мира — темнота."));
		B.Para(TEXT("Все места связаны дорогами, как на карте: дойдите до тропы у края — и вы в соседнем месте. У каждой тропы стоит табличка с рисунком того места, куда она ведёт (на табличке к Лесной реке — река и лес по берегам)."));
		B.Para(Touch()
			? TEXT("Карта шире экрана: листайте её пальцем влево и вправо. Лес — слева от реки, Город — справа, за морем.")
			: TEXT("Карта шире экрана: тяните её мышью или крутите колесо. Лес — слева от реки, Город — справа, за морем."));
		B.Tip(TEXT("Табличка с замком — место ещё закрыто: подойдите к ней, и она подскажет, как его открыть (на карте написано то же)."), EFVIcon::Lock);
	}

	void BuildForest(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVLocationDef& Fo = FVDBU6::ForestLocation();
		B.Head(Fo.Name.IsEmpty() ? FString(TEXT("Лес")) : Fo.Name, TEXT("Опушка, завал и Чаща"), PItem(TEXT("mat_log")));
		B.Para(TEXT("В лес ведёт тропа от реки. Воды здесь нет — зато можно ходить где угодно."));
		B.Row(PItem(TEXT("res_branch")), TEXT("Опушка"), TEXT("Ветки и камешки на земле, первые берёзы и сосны."));
		{
			FBlock& R = B.Row(PIcon(EFVIcon::Axe), TEXT("Завал"), FString::Printf(TEXT("Поваленные стволы поперёк тропы. Расчищается топором один раз навсегда и даёт %s."), *InputsText(FVDBU6::BlockageLoot())));
			if (F.bBlockage) { R.Right = TEXT("расчищен"); R.RightColor = InkGood(); }
		}
		B.Row(PIcon(EFVIcon::House), TEXT("Чаща"), TEXT("За завалом: больше деревьев, домик Лесника и двор Прораба."));
		B.Tip(TEXT("Рубить можно только деревья с зарубкой на стволе."), EFVIcon::Axe);
	}

	void BuildAxe(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Топор и рубка"), TEXT("Брёвна для построек"), PIcon(EFVIcon::Axe));
		B.Para(TEXT("Рубка — мини-игра: топор качается, бейте, когда он над зарубкой. Промах — топор застревает. Серия точных ударов даёт лишнюю ветку."));
		B.Para(FString::Printf(TEXT("Дерево даёт бревно, 1–3 ветки, сосна — ещё смолу. На месте дерева остаётся пень, из него вырастает новое — за %d мин игры (сон не ускоряет)."),
			FMath::RoundToInt(FVDBU6::TreeRegrowMinutes())));
		const TArray<FFVToolDef>& Axes = FVDBU6::Axes();
		for (int32 i = 0; i < Axes.Num(); ++i)
		{
			const FFVToolDef& T = Axes[i];
			TArray<FString> Trees;
			for (int32 k = 0; k < int32(EFVTreeKind::Count); ++k)
			{
				const int32 Hits = FVDBU6::TreeHits(EFVTreeKind(k), i + 1);
				if (Hits > 0) { Trees.Add(FString::Printf(TEXT("%s (%d)"), *TreeKindName(EFVTreeKind(k)), Hits)); }
			}
			FString Body = FString::Printf(TEXT("Породы (ударов): %s"), *FString::Join(Trees, TEXT(", ")));
			const FString Where = ToolCraftText(T);
			if (!Where.IsEmpty()) { Body += TEXT("\n") + Where; }
			FBlock& R = B.Row(PIcon(EFVIcon::Axe), T.Name, Body);
			if (F.Axe >= i + 1) { R.Right = TEXT("есть"); R.RightColor = InkGood(); }
			else { R.Right = FString::Printf(TEXT("с %d ур."), T.MinLevel); }
		}
		B.Tip(TEXT("Первый топор продаёт Боцман на Морском причале."), EFVIcon::Shop);
	}

	void BuildForestNpc(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Жители леса"), TEXT("Лесник и Прораб"), PIcon(EFVIcon::House));
		for (const FFVNpcDef* N : FVDBU6::NpcsAt(FVDBU6::ForestId()))
		{
			if (N) { B.Row(PIcon(N->Screen == EFVScreen::Builder ? EFVIcon::Hammer : EFVIcon::Shop), N->Name, N->Role); }
		}
		B.Para(FString::Printf(TEXT("Подойдите к жителю и %s, чтобы поговорить."), *UseWord()));
	}

	/** §18 «Старая мельница» (Р53). */
	void BuildMill(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Старая мельница"), TEXT("Запруда за лесом: рожь, мука, бобры и Омут"), PIcon(EFVIcon::Map));
		B.Para(FString::Printf(TEXT("За лесной Чащей, мимо домика Лесника, тропа уходит к ручью, а за ним — мельничная запруда. Пройти туда можно с %d уровня, когда починен мостик через ручей."), FVMill::MinLevel));
		{
			const FFVBoatStage& Br = FVMill::BridgeRepair();
			FBlock& R = B.Row(PIcon(EFVIcon::Hammer), Br.Name, FString::Printf(TEXT("В лесу, у ручья. Нужно: %s"), *InputsText(Br.Cost)));
			if (F.bMillBridge) { R.Right = TEXT("готов"); R.RightColor = InkGood(); }
		}
		B.Sub(TEXT("Ремонт мельницы"));
		const TArray<FFVBoatStage>& Stages = FVMill::MillStages();
		const int32 Stage = P ? P->GetMillStage() : 0;
		for (int32 i = 0; i < Stages.Num(); ++i)
		{
			FBlock& R = B.Row(PIcon(EFVIcon::Hammer), FString::Printf(TEXT("%d. %s"), i + 1, *Stages[i].Name), InputsText(Stages[i].Cost));
			if (i < Stage) { R.Right = TEXT("готово"); R.RightColor = InkGood(); }
		}
		B.Para(FString::Printf(TEXT("Работающая мельница мелет рожь: %d снопа — мешочек ржаной муки. А водяная пила даёт +%d доску с каждого бревна на лесопилке двора."),
			FVMill::RyePerFlour, FVMill::SawBonusBoards));
		B.Row(PItem(FVMill::RyeSeedId()), TEXT("Рожь"), TEXT("Семена продаёт Мельник Прокоп. Сажайте в огороде, как морковь."));
		B.Row(PItem(FVMill::FlourId()), TEXT("Ржаная мука"), TEXT("Хлебная прикормка, блины, рыбный пирог и мельничный колобок."));
		B.Sub(TEXT("Что ещё есть у запруды"));
		B.Row(PItem(FVMill::BreadBaitId()), TEXT("Прикормочные мостки"), FString::Printf(TEXT("Бросьте на мосток прикормку, хлебную прикормку или тесто: %.0f игровых минут рыба с него клюёт быстрее, а редкая — чаще."),
			FVMill::DeckGameMinutes));
		B.Row(PIcon(EFVIcon::Backpack), TEXT("Бобровая хатка"), FString::Printf(TEXT("Отнесите бобрам %d веток (не чаще раза в %.0f минут игры). После %d подкормок плотина готова и открывается Омут — глубокая вода со стерлядью."),
			FVMill::BeaverBranches, FVMill::BeaverCooldownMinutes, FVMill::BeaverFeedsNeeded));
		B.Row(PIcon(EFVIcon::Aquarium), TEXT("Рыборазводный садок"), FString::Printf(TEXT("Выпустите живую рыбу из аквариума: %d игровых дня этот вид клюёт на мельнице чаще и крупнее. До %d видов сразу; рыба не возвращается."),
			FMath::RoundToInt(FVMill::PenGameMinutes / 1440.f), FVMill::PenSlots));
		B.Row(PIcon(EFVIcon::Coin), TEXT("Мельник Прокоп"), TEXT("Покупает копчёную рыбу дороже лавки и самодельные блёсны и мушки. Продаёт семена ржи."));
		B.Row(PIcon(EFVIcon::Net), TEXT("Сети"), FString::Printf(TEXT("%d колышка на пруду и ещё один в Омуте, когда плотина готова."), FVMill::NetSpots - 1));
		B.Tip(TEXT("Туманное утро (6–10 ч): травяной чай на мельнице действует вдвое дольше."), EFVIcon::Clock);
		B.Note(TEXT("В Омуте живёт Хозяин омута — ночью, на глубине. Без мельничного колобка и прочной лески он не подойдёт."));
	}

	void BuildYard(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Большой двор и постройки"), TEXT("Участки под лесопилку, огород и кузню"), PIcon(EFVIcon::Hammer));
		B.Para(TEXT("Двор стал шире: в центре — дом, учебный пруд и компостная куча с червями, слева — участок под огород, справа — под лесопилку и кузню. Пустые участки размечены колышками."));
		for (const FFVYardBuildingDef& Y : FVDBU6::YardBuildings())
		{
			const int32 Lvl = F.Yard[FMath::Clamp(int32(Y.Building), 0, int32(EFVYardBuilding::Count) - 1)];
			FString Body = Y.Description;
			if (Y.Levels.Num() > 0 && Lvl == 0) { Body += FString::Printf(TEXT("\nС %d ур. · нужно: %s"), Y.Levels[0].MinLevel, *InputsText(Y.Levels[0].Materials)); }
			if (Y.bNeedsBlockageCleared) { Body += TEXT("\nТолько после расчистки завала в лесу."); }
			FBlock& R = B.Row(PIcon(Y.Building == EFVYardBuilding::Sawmill ? EFVIcon::Saw : (Y.Building == EFVYardBuilding::Forge ? EFVIcon::Anvil : EFVIcon::Seed)), Y.Name, Body);
			if (Lvl > 0) { R.Right = FString::Printf(TEXT("ур. %d"), Lvl); R.RightColor = InkGood(); }
			else if (Y.Levels.Num() > 0 && Y.Levels[0].Price > 0) { R.Price = Y.Levels[0].Price; }
		}
		B.Tip(TEXT("Постройки заказывают у Прораба Гаврилы в лесной Чаще."), EFVIcon::Hammer);
	}

	void BuildSawmill(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVYardBuildingDef& Def = FVDBU6::YardBuilding(EFVYardBuilding::Sawmill);
		B.Head(Def.Name, TEXT("Брёвна в доски"), PIcon(EFVIcon::Saw));
		B.Para(Def.Description);
		B.Para(TEXT("Распил — мини-игра: бревно едет к пиле, держите и отпускайте, чтобы рез шёл по карандашной линии. Быстрый распил — без игры, но досок меньше."));
		B.Sub(TEXT("Работы"));
		YardRecipeRows(B, F, EFVYardBuilding::Sawmill);
		B.Sub(TEXT("Уровни"));
		YardLevelRows(B, F, EFVYardBuilding::Sawmill);
	}

	void BuildGarden(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVYardBuildingDef& Def = FVDBU6::YardBuilding(EFVYardBuilding::Garden);
		B.Head(Def.Name, TEXT("Овощи для кухни"), PIcon(EFVIcon::Seed));
		B.Para(TEXT("Подойдите к грядке и выберите семя. Всё растёт, пока вы играете: сон рост не ускоряет. В дождь растёт быстрее. Полив не нужен."));
		for (const FFVSeedDef& S : FVDBU6::Seeds())
		{
			FString Body = FString::Printf(TEXT("%s ×%d–%d за %d мин игры"), *FVDB::ItemName(S.CropId), S.YieldMin, S.YieldMax, FMath::RoundToInt(S.GrowMinutes));
			if (S.SeedsMax > 0) { Body += FString::Printf(TEXT(" · семена ×%d–%d"), S.SeedsMin, S.SeedsMax); }
			if (S.RainMult > 1.001f) { Body += FString::Printf(TEXT("\nВ дождь рост ×%s"), *FVUI::Num(S.RainMult)); }
			B.Row(PItem(S.SeedId), FVDB::ItemName(S.SeedId), Body);
		}
		B.Para(TEXT("Семена моркови иногда находятся у Горного озера, остальные продаёт огородница в Городе. Огород — для кухни: семена дороже, чем выручка с урожая."));
		B.Sub(TEXT("Уровни"));
		YardLevelRows(B, F, EFVYardBuilding::Garden);
	}

	void BuildForge(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		const FFVYardBuildingDef& Def = FVDBU6::YardBuilding(EFVYardBuilding::Forge);
		B.Head(Def.Name, TEXT("МПК, гвозди, катушки"), PIcon(EFVIcon::Anvil));
		B.Para(Def.Description);
		B.Sub(TEXT("Работы"));
		YardRecipeRows(B, F, EFVYardBuilding::Forge);
		B.Sub(TEXT("Уровни"));
		YardLevelRows(B, F, EFVYardBuilding::Forge);
	}

	void BuildRooms(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Дом внутри"), TEXT("Сени, кухня, горница, мастерская"), PIcon(EFVIcon::Door));
		B.Row(PStation(EFVStation::Chest, 1), TEXT("Сени"), TEXT("У входа: сундук, вешалка, полка с инструментами."));
		B.Row(PStation(EFVStation::Kitchen, FMath::Max(1, F.Lvl(EFVStation::Kitchen))), TEXT("Кухня"), TEXT("Печь (станция «Кухня»), стол, буфет и ледник — погреб с люком."));
		B.Row(PStation(EFVStation::Bed, 1), TEXT("Горница"), TEXT("Кровать, радио, аквариум у окна, стена трофеев, ковёр."));
		B.Row(PStation(EFVStation::Workshop, FMath::Max(1, F.Lvl(EFVStation::Workshop))), TEXT("Мастерская"), TEXT("Пристройка с верстаком."));
		B.Para(TEXT("Коптильня и червятник переехали во двор: коптильня — у забора, червятник — у компостной кучи. Уровни и запасы станций сохранились."));
		B.Tip(TEXT("Дорогую мебель продаёт Прораб Гаврила в лесу."), EFVIcon::Hammer);
	}

	void BuildUpgrades2(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Экспертные улучшения станций"), TEXT("Новые уровни станций дома"), PIcon(EFVIcon::Level));
		B.Para(FString::Printf(TEXT("С %d уровня у станций появляются новые уровни. Их строят так же — у самой станции."), FV::ClassicMaxLevel + 1));
		for (int32 s = 0; s < int32(EFVStation::Count); ++s)
		{
			const FFVStationDef& Def = FVDB::Station(EFVStation(s));
			for (int32 i = 0; i < Def.Levels.Num(); ++i)
			{
				const FFVStationLevel& L = Def.Levels[i];
				if (L.MinLevel <= FV::ClassicMaxLevel) { continue; }
				FString Body = L.Description;
				if (L.Materials.Num() > 0) { Body += FString::Printf(TEXT("\nНужно: %s · с %d ур."), *InputsText(L.Materials), L.MinLevel); }
				const FString LName = L.Name.IsEmpty() ? FString::Printf(TEXT("ур. %d"), i + 1) : L.Name;
				FBlock& R = B.Row(PStation(EFVStation(s), i + 1), FString::Printf(TEXT("%s: %s"), *Def.Name, *LName), Body);
				if (F.Lvl(EFVStation(s)) >= i + 1) { R.Right = TEXT("построено"); R.RightColor = InkGood(); }
				else if (L.Price > 0) { R.Price = L.Price; }
			}
		}
	}

	void BuildScrap(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Металлолом"), TEXT("Для кузни и снастей"), PItem(TEXT("mat_scrap")));
		B.Para(TEXT("Металлолом добывают только рыбалкой: иногда на крючок попадается ржавая железка, а на мусорную приманку — чаще."));
		if (const FFVBaitDef* Bt = FVDB::FindBait(FName(TEXT("bait_trash")))) { B.Row(PItem(Bt->Id), Bt->Name, Bt->Description); }
		B.Bullet(TEXT("Консервные банки переплавляются в металлолом в мастерской или в кузне."));
		B.Bullet(FString::Printf(TEXT("В Городе лежат кучи хлама: в них находится металлолом, снова наполняются через %d мин игры."), FMath::RoundToInt(FVDBU6::JunkPileRegrowMinutes())));
	}

	void BuildCity(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Лодка и Город"), TEXT("Путь в экспертную зону"), PIcon(EFVIcon::Boat));
		B.Para(TEXT("На Морском причале стоит старая лодка. Её чинят по этапам — лодка поправляется на глазах:"));
		const TArray<FFVBoatStage>& Stages = FVDBU6::BoatStages();
		for (int32 i = 0; i < Stages.Num(); ++i)
		{
			const FFVBoatStage& S = Stages[i];
			FBlock& R = B.Row(PIcon(EFVIcon::Hammer), FString::Printf(TEXT("%d. %s"), i + 1, *S.Name), FString::Printf(TEXT("Нужно: %s · с %d ур."), *InputsText(S.Cost), S.MinLevel));
			if (F.Boat > i) { R.Right = TEXT("готово"); R.RightColor = InkGood(); }
			else if (S.Coins > 0) { R.Price = S.Coins; }
		}
		B.Para(FString::Printf(TEXT("Опыт на %d уровне замирает на %s, пока вы не доплывёте до Города. Награды опытом в это время платятся монетами. При первом прибытии уровень сразу становится %d-м."),
			FV::ClassicMaxLevel, *FV::Money(FVDB::XpForLevel(FV::ClassicMaxLevel)), FV::ClassicMaxLevel + 1));
		B.Tip(TEXT("Готовая лодка: «Отплыть в Город» у лодки или на карте."), EFVIcon::Map);
	}

	void BuildExpert(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Экспертная рыбалка"), TEXT("Шкала, прогресс, натяжение"), PIcon(EFVIcon::ExpertRod));
		B.Para(TEXT("В Городе и Пещере рыбу ловят экспертной удочкой. Бой идёт на одной вертикальной шкале, как в обычной рыбалке, только она длиннее: рыба ходит вверх и вниз, вы держите её в зелёной полосе."));
		B.Para(TEXT("Полосой управляют по-разному — это и есть тип удочки: маятник, двойчатка, импульс, магнит или ползунок."));
		B.Bullet(TEXT("Прогресс начинается с 30 %: растёт, пока рыба в полосе, и падает, пока она вне её. 100 % — поймана, 0 % — ушла."));
		B.Bullet(TEXT("Перед приёмом рыбы вспыхивает знак. Рывок — рыба краснеет: удерживать её выгодно, но растёт натяжение, а при полном натяжении леска рвётся."));
		B.Bullet(TEXT("Уведите полосу с красной рыбы — прогресс не упадёт. Отпустить рыбу на рывке безопасно."));
		B.Bullet(TEXT("Другие приёмы: свеча (выпрыгивает к верху шкалы и падает), нырок (уходит на дно, замирает и бросается), челнок, двойник-обманка, прячется (видны только пузырьки). Ближе к концу боя рыба быстрее и хитрее."));
		B.Tip(FString::Printf(TEXT("Уровни %d–%d растут вдвое медленнее классических."), FV::ClassicMaxLevel + 1, FV::ExpertMaxLevel), EFVIcon::Xp);
	}

	void BuildExpertRods(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Типы удочек"), TEXT("Пять способов ловли"), PIcon(EFVIcon::ExpertRod));
		for (int32 t = 0; t < int32(EFVExpertRodType::Count); ++t)
		{
			// each type is shown by its own first expert rod (different colours), not one shared icon
			FName TypeRod = NAME_None;
			for (const FFVRodDef& R : FVDB::Rods()) { if (R.bExpert && R.ExpertType == EFVExpertRodType(t)) { TypeRod = R.Id; break; } }
			B.Row(TypeRod.IsNone() ? PIcon(EFVIcon::ExpertRod) : PItem(TypeRod), FVDBU6::RodTypeName(EFVExpertRodType(t)), FVDBU6::RodTypeHowTo(EFVExpertRodType(t)));
		}
		B.Sub(TEXT("Ваши удочки"));
		int32 Shown = 0;
		for (const FFVRodDef& R : FVDB::Rods())
		{
			if (!R.bExpert || !P || !P->OwnedRods.Contains(R.Id)) { continue; }
			FBlock& Row = B.Row(PItem(R.Id), R.Name, R.ExpertType < EFVExpertRodType::Count ? FString::Printf(TEXT("Тип «%s». %s"), *FVDBU6::RodTypeName(R.ExpertType), *R.Description) : R.Description);
			Row.Right = TEXT("есть");
			Row.RightColor = InkGood();
			++Shown;
		}
		if (Shown == 0) { B.Note(TEXT("Экспертные удочки продаёт снастёвщик Кузьма в Городе; другие делают из твёрдой породы Пещеры.")); }
		B.Tip(TEXT("Каждой удочке — своя рамка шкалы: цвета и узоры у всех разные."), EFVIcon::ExpertRod);
	}

	void BuildExpertTackle(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Оснастка эксперта"), TEXT("Леска, поплавок, катушка, наживка"), PIcon(EFVIcon::Line));
		B.Para(TEXT("Конфигурация — это удочка, экспертная леска, поплавок, катушка и наживка. От неё зависит, кто клюнет. Классические снасти на экспертную удочку не ставятся, и наоборот."));
		B.Para(TEXT("Характеристики: сила (разгон, толчок, тяга), размер зоны (высота полосы), прочность лески, устойчивость к течению и волнам."));
		for (const FFVItemDef& It : FVDB::Items())
		{
			const bool bTackle = FVDBU6::LineKindOf(It.Id) != EFVExpertLine::None || FVDBU6::FloatKindOf(It.Id) != EFVExpertFloat::None
				|| FVDBU6::ReelKindOf(It.Id) != EFVExpertReel::None || It.Kind == EFVItemKind::Reel;
			if (!bTackle) { continue; }
			B.Row(PItem(It.Id), It.Name, ExpertStatsLine(It.ExpertTackle));
		}
		B.Tip(TEXT("Сохраните набор в «Оснастке эксперта» — потом он ставится в одно касание прямо у воды."), EFVIcon::Check);
	}

	void BuildHazards(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Стихии"), TEXT("Что мешает на шкале"), PIcon(EFVIcon::WarnWave));
		B.Para(TEXT("В экспертном бою бывают стихии — не больше двух сразу. Они зависят от места и местной погоды, а знак стихии виден на панели. Устойчивость снасти ослабляет течение и волны."));
		static const EFVIcon Icons[] = { EFVIcon::WarnCurrent, EFVIcon::WarnWhirl, EFVIcon::WarnWave, EFVIcon::WarnDash, EFVIcon::WarnCloud, EFVIcon::Dark, EFVIcon::WarnDrip, EFVIcon::WarnSpring };
		static const TCHAR* Hints[] = {
			TEXT("Всё время сносит полосу вверх или вниз."),
			TEXT("Тянет полосу к середине шкалы."),
			TEXT("Время от времени резко толкает полосу."),
			TEXT("Участки шкалы: полоса в них вязнет, а натяжение растёт."),
			TEXT("Участок шкалы: рыба в нём не видна, полоса идёт медленнее."),
			TEXT("Видно только то, что рядом с полосой."),
			TEXT("Падают камни: сначала знак, потом камень — полоса от него отскакивает."),
			TEXT("Участок шкалы выталкивает полосу и рыбу вверх.") };
		for (int32 h = 0; h < int32(EFVHazard::Count); ++h)
		{
			B.Row(PIcon(Icons[FMath::Clamp(h, 0, int32(UE_ARRAY_COUNT(Icons)) - 1)]), FVDBU6::HazardName(EFVHazard(h)),
				h < int32(UE_ARRAY_COUNT(Hints)) ? FString(Hints[h]) : FString());
		}
	}

	void BuildSeaWeather(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Погода Города"), TEXT("Штиль, ветер, шторм, туман"), PIcon(EFVIcon::SeaWind));
		B.Para(TEXT("У Города и Пещеры своя погода: она меняется в течение дня и видна рядом с часами. Бывает прилив."));
		static const EFVIcon Icons[] = { EFVIcon::SeaCalm, EFVIcon::SeaWind, EFVIcon::SeaStorm, EFVIcon::SeaFog };
		for (int32 s = 0; s < int32(EFVSeaState::Count); ++s)
		{
			B.Row(PIcon(Icons[FMath::Clamp(s, 0, 3)]), FVDBU6::SeaStateName(EFVSeaState(s)), FString());
		}
		B.Tip(TEXT("Радио дома подскажет погоду."), EFVIcon::Radio);
	}

	void BuildFacts(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Сведения о рыбах"), TEXT("Условия скрыты"), PIcon(EFVIcon::Scroll));
		B.Para(TEXT("У экспертных рыб и легенд условия клёва скрыты: в коллекции вместо них «???». Каждое сведение открывает одну строку — место и время, погоду, типы удочек, леску, поплавок, катушку или наживку."));
		B.Bullet(TEXT("Сведения дают за задания с доски Города — заказчики рассказывают, что знают."));
		B.Bullet(TEXT("Страницы журнала попадаются в сокровищах экспертной рыбалки."));
		B.Bullet(TEXT("Поимка рыбы условий не раскрывает."));
		if (P) { B.Note(FString::Printf(TEXT("Узнано сведений: %d"), P->TotalFactsKnown())); }
	}

	void BuildExpertLegends(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Экспертные легенды"), TEXT("Партитура приёмов"), PIcon(EFVIcon::Trophy));
		B.Para(TEXT("У каждой экспертной легенды своя партитура — приёмы идут в одном порядке. Их можно выучить за несколько попыток: игра запоминает, до какого приёма вы дошли."));
		B.Para(TEXT("У легенды своя наживка: её рецепт — одно из сведений. С ней и при всех условиях легенда клюёт наверняка."));
		B.Bullet(TEXT("Каждый приём легенда начинает броском: краснеет, дрожит и бросается вверх или вниз. Встретьте её полосой — дёрнется леска; не успели — легенда отнимет немного прогресса."));
		FBlock& R = B.Pics();
		for (int32 l = 1; l < int32(EFVExpertLegend::Count); ++l)
		{
			if (const FFVFishDef* Fi = FVDBU6::FindExpertLegend(EFVExpertLegend(l)))
			{
				const FFVFishRecord* Rec = P ? P->GetRecord(Fi->Id) : nullptr;
				const bool bCaught = Rec && Rec->Caught > 0;
				AddPic(R, PFish(Fi->Id, !bCaught), bCaught ? Fi->Name : FString(TEXT("???")));
			}
		}
	}

	void BuildMismatch(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Не свой режим"), TEXT("Удочки и рыбы разных режимов"), PIcon(EFVIcon::Question));
		B.Bullet(TEXT("Экспертной удочкой можно ловить и в обычных местах — обычная рыба тогда идёт на экспертное поле."));
		B.Bullet(TEXT("Экспертная рыба на обычной удочке почти всегда срывается, зато срыв подскажет, на какие типы удочек она клюёт."));
		B.Bullet(TEXT("Классические легенды на экспертную удочку не ловятся."));
	}

	void BuildCave(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Пещера и раскопка"), TEXT("Кирка и твёрдая порода"), PIcon(EFVIcon::Pickaxe));
		B.Para(TEXT("Вход в Пещеру в Городе засыпан. Разберите завалы киркой — откроется подземное озеро с экспертной рыбой. В Пещере темно: свет только вокруг рыбака."));
		const TArray<FFVToolDef>& Picks = FVDBU6::Pickaxes();
		for (int32 i = 0; i < Picks.Num(); ++i)
		{
			FBlock& R = B.Row(PIcon(EFVIcon::Pickaxe), Picks[i].Name, ToolCraftText(Picks[i]));
			if (F.Pickaxe >= i + 1) { R.Right = TEXT("есть"); R.RightColor = InkGood(); }
			else { R.Right = FString::Printf(TEXT("с %d ур."), Picks[i].MinLevel); }
		}
		B.Para(FString::Printf(TEXT("Валуны в Пещере раскапывают — мини-игра: снимайте слои камня, не разбив находки. Внутри — твёрдая порода и редкости. Валун восстанавливается за %d мин игры."),
			FMath::RoundToInt(FVDBU6::BoulderRegrowMinutes())));
	}

	void BuildCityOrders(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Задания Города"), TEXT("Хорошая рыба и изделия"), PIcon(EFVIcon::Scroll));
		B.Para(TEXT("Доска заданий Города похожа на доску у пруда: взять задание — внести залог, принести — получить награду и залог обратно. Горожане просят рыбу высокого качества, блюда и снасти."));
		B.Bullet(TEXT("Некоторые задания вместо части монет дают сведение о рыбе."));
		B.Bullet(TEXT("Одновременно можно взять не больше трёх заданий."));
		if (P) { B.Note(FString::Printf(TEXT("Выполнено заданий Города: %d"), P->U6.CityTasksDone)); }
	}

	void BuildNetkeeper(FEntry& E, const FFacts& F, const UFVSaveGame* P)
	{
		FB B(E);
		B.Head(TEXT("Артельщик"), TEXT("Сети без хлопот"), PIcon(EFVIcon::Artel));
		B.Para(TEXT("Артельщик Никифор в Городе ставит ваши сети на выбранных водоёмах и каждое утро продаёт улов, оставляя себе долю."));
		B.Bullet(TEXT("Наймите его и отдайте сети из рюкзака; места выбираются на его экране."));
		B.Bullet(TEXT("Порванные сети можно починить все сразу или выбросить — он заменит их из запаса."));
		B.Bullet(TEXT("В экспертной зоне сетей нет, экспертные рыбы в сети не идут."));
	}

	// =================================================================================================================
	// the table: id, category, unlock rule, builder (display order = table order)
	// =================================================================================================================
	using FUnlockFn = bool (*)(const FFacts&);
	using FBuildFn = void (*)(FEntry&, const FFacts&, const UFVSaveGame*);
	struct FDef
	{
		FName Id;
		int32 Cat = 0;
		FUnlockFn Unlocked = nullptr;
		FBuildFn Build = nullptr;
	};

	const TArray<FDef>& Defs()
	{
		static TArray<FDef> Table;
		if (Table.Num() == 0)
		{
			auto Add = [](const TCHAR* Id, int32 Cat, FUnlockFn U, FBuildFn B)
			{
				FDef& D = Table.AddDefaulted_GetRef();
				D.Id = FName(Id);
				D.Cat = Cat;
				D.Unlocked = U;
				D.Build = B;
			};
			// Основы
			Add(TEXT("g_controls"), 0, [](const FFacts&) { return true; }, &BuildControls);
			Add(TEXT("g_fishing"), 0, [](const FFacts&) { return true; }, &BuildFishing);
			Add(TEXT("g_reeling"), 0, [](const FFacts&) { return true; }, &BuildReeling);
			Add(TEXT("g_perfect"), 0, [](const FFacts& F) { return F.Fish >= 1 || F.Treasures > 0 || F.Level >= 2; }, &BuildPerfect);
			Add(TEXT("g_quality"), 0, [](const FFacts& F) { return F.Fish >= 1 || F.Level >= 2; }, &BuildQuality);
			Add(TEXT("g_legends"), 0, [](const FFacts& F) { return F.Level >= 3; }, &BuildLegends);   // update 5: Царь-Карп wakes up at 3
			// Снаряжение
			Add(TEXT("g_inventory"), 1, [](const FFacts&) { return true; }, &BuildInventory);
			Add(TEXT("g_bait"), 1, [](const FFacts&) { return true; }, &BuildBait);
			Add(TEXT("g_rods"), 1, [](const FFacts& F) { return F.Level >= 2 || F.Rods > 1 || F.Fish >= 5; }, &BuildRods);
			Add(TEXT("g_tackle"), 1, [](const FFacts& F) { return F.bTackle || F.Lvl(EFVStation::Workshop) >= 1; }, &BuildTackle);
			Add(TEXT("g_food"), 1, [](const FFacts& F) { return F.bFood || F.Lvl(EFVStation::Kitchen) >= 1; }, &BuildFood);
			Add(TEXT("g_scrap"), 1, [](const FFacts& F) { return F.Level >= 5; }, &BuildScrap);
			// Мир
			Add(TEXT("g_travel"), 2, [](const FFacts& F) { return F.Fish >= 3 || F.Level >= 2 || F.Unlocked > 1; }, &BuildTravel);
			Add(TEXT("g_trade"), 2, [](const FFacts& F) { return F.Fish >= 1 || F.Level >= 2 || F.Earned > 0; }, &BuildTrade);
			Add(TEXT("g_time"), 2, [](const FFacts& F) { return F.Day >= 2 || F.Minutes >= 18.f * 60.f || F.Level >= 2; }, &BuildTime);
			Add(TEXT("g_gather"), 2, [](const FFacts& F) { return F.Gathered >= 1 || F.Fish >= 3 || F.Level >= 2; }, &BuildGather);
			Add(TEXT("g_worms"), 2, [](const FFacts& F) { return F.Home() || F.Shovel > 0 || F.WormsDug > 0; }, &BuildWorms);
			Add(TEXT("g_nets"), 2, [](const FFacts& F) { return F.Level >= F.MinNetLevel() || F.bNetItem || F.NetsPlaced > 0; }, &BuildNets);
			Add(TEXT("g_orders"), 2, [](const FFacts& F) { return F.Fish >= 5 || F.Level >= 2 || F.OrdersDone > 0 || F.OrdersTaken > 0; }, &BuildOrders);
			Add(TEXT("g_bigmap"), 2, [](const FFacts&) { return true; }, &BuildBigMap);
			Add(TEXT("g_forest"), 2, [](const FFacts& F) { return F.Level >= 4; }, &BuildForest);
			Add(TEXT("g_axe"), 2, [](const FFacts& F) { return F.Level >= 4 || F.Axe > 0; }, &BuildAxe);
			Add(TEXT("g_forest_npc"), 2, [](const FFacts& F) { return F.bBlockage; }, &BuildForestNpc);
			Add(TEXT("g_city_orders"), 2, [](const FFacts& F) { return F.Expert(); }, &BuildCityOrders);
			Add(TEXT("g_netkeeper"), 2, [](const FFacts& F) { return F.Expert(); }, &BuildNetkeeper);
			Add(TEXT("g_mill"), 2, [](const FFacts& F) { return F.Level >= FVMill::MinLevel || F.bMillBridge || F.bMillOpen; }, &BuildMill);
			// Дом и ремёсла
			Add(TEXT("g_home"), 3, [](const FFacts& F) { return F.Home(); }, &BuildHome);
			Add(TEXT("g_bed"), 3, [](const FFacts& F) { return F.Home(); }, &BuildBed);
			Add(TEXT("g_kitchen"), 3, [](const FFacts& F) { return F.Lvl(EFVStation::Kitchen) >= 1 || (F.Home() && F.CanBuild(EFVStation::Kitchen)); }, &BuildKitchen);
			Add(TEXT("g_workshop"), 3, [](const FFacts& F) { return F.Lvl(EFVStation::Workshop) >= 1 || (F.Home() && F.CanBuild(EFVStation::Workshop)); }, &BuildWorkshop);
			Add(TEXT("g_knots"), 3, [](const FFacts& F) { return F.Lvl(EFVStation::Workshop) >= 1 || F.Knots > 0; }, &BuildKnots);
			Add(TEXT("g_lures"), 3, [](const FFacts& F) { return F.Lures > 0 || (F.Lvl(EFVStation::Workshop) >= 1 && F.CanBuild(EFVStation::Workshop, 2)); }, &BuildLures);
			Add(TEXT("g_smoke"), 3, [](const FFacts& F) { return F.Lvl(EFVStation::Smokehouse) >= 1 || (F.Home() && F.CanBuild(EFVStation::Smokehouse)); }, &BuildSmoke);
			Add(TEXT("g_aquarium"), 3, [](const FFacts& F) { return F.Lvl(EFVStation::Aquarium) >= 1 || (F.Home() && F.Level >= 2 && F.CanBuild(EFVStation::Aquarium)); }, &BuildAquarium);
			Add(TEXT("g_trophy"), 3, [](const FFacts& F) { return F.Lvl(EFVStation::TrophyWall) >= 1 || (F.Home() && F.CanBuild(EFVStation::TrophyWall)); }, &BuildTrophy);
			Add(TEXT("g_decor"), 3, [](const FFacts& F) { return F.Home() && (F.Level >= 2 || F.bAnyStation); }, &BuildDecor);
			Add(TEXT("g_yard"), 3, [](const FFacts&) { return true; }, &BuildYard);
			Add(TEXT("g_rooms"), 3, [](const FFacts&) { return true; }, &BuildRooms);
			Add(TEXT("g_sawmill"), 3, [](const FFacts& F) { return F.bBlockage || F.Yard[int32(EFVYardBuilding::Sawmill)] > 0; }, &BuildSawmill);
			Add(TEXT("g_garden"), 3, [](const FFacts& F) { return F.Level >= 7 || F.Yard[int32(EFVYardBuilding::Garden)] > 0; }, &BuildGarden);
			Add(TEXT("g_forge"), 3, [](const FFacts& F) { return F.Level >= 10 || F.Yard[int32(EFVYardBuilding::Forge)] > 0; }, &BuildForge);
			Add(TEXT("g_upgrades2"), 3, [](const FFacts& F) { return F.Expert(); }, &BuildUpgrades2);
			// Коллекции
			Add(TEXT("g_collection"), 4, [](const FFacts& F) { return F.Fish >= 1; }, &BuildCollection);
			Add(TEXT("g_achievements"), 4, [](const FFacts& F) { return F.Fish >= 1 || F.Achievements > 0; }, &BuildAchievements);
			// Экспертная зона (update 6)
			Add(TEXT("g_city"), 5, [](const FFacts& F) { return F.bPier || F.bCity; }, &BuildCity);
			Add(TEXT("g_expert"), 5, [](const FFacts& F) { return F.Expert(); }, &BuildExpert);
			Add(TEXT("g_expert_rods"), 5, [](const FFacts& F) { return F.Expert() || F.ExpertRods > 0; }, &BuildExpertRods);
			Add(TEXT("g_expert_tackle"), 5, [](const FFacts& F) { return F.Expert(); }, &BuildExpertTackle);
			Add(TEXT("g_hazards"), 5, [](const FFacts& F) { return F.bExpertCaught; }, &BuildHazards);
			Add(TEXT("g_seaweather"), 5, [](const FFacts& F) { return F.Expert(); }, &BuildSeaWeather);
			Add(TEXT("g_facts"), 5, [](const FFacts& F) { return F.Expert(); }, &BuildFacts);
			Add(TEXT("g_expert_legends"), 5, [](const FFacts& F) { return F.Expert(); }, &BuildExpertLegends);
			Add(TEXT("g_mismatch"), 5, [](const FFacts& F) { return F.Expert(); }, &BuildMismatch);
			Add(TEXT("g_cave"), 5, [](const FFacts& F) { return F.Level >= 12 || F.Pickaxe > 0; }, &BuildCave);
		}
		return Table;
	}

	// =================================================================================================================
	// layout & drawing of blocks
	// =================================================================================================================
	struct FStyle
	{
		bool bP = false;
		float Body = 8.f, Small = 6.5f, Sub = 9.5f, Title = 11.f, KeyS = 7.f;
		float RowBox = 26.f, PicBox = 30.f, PicCell = 62.f, HeroBox = 36.f, TipIcon = 16.f, KeyCol = 88.f, Gap = 5.f;

		static FStyle Make()
		{
			FStyle S;
			S.bP = FV::IsPortrait();
			if (S.bP)
			{
				S.Body = FV::Ui::Body();
				S.Small = FV::Ui::Small();
				S.Sub = 11.5f;
				S.Title = FV::Ui::Header();
				S.KeyS = 8.5f;
				S.RowBox = 36.f;
				S.PicBox = 40.f;
				S.PicCell = 72.f;
				S.HeroBox = 48.f;
				S.TipIcon = 20.f;
				S.KeyCol = 112.f;
				S.Gap = 7.f;
			}
			return S;
		}
	};

	/** A small animated bobber-bar illustration (the reeling mini-game). */
	void DrawMiniBar(FVDraw& D, const FFVRect& Area, const FStyle& S)
	{
		if (Area.H < 40.f) { return; }
		const float T = float(D.Time);
		const float H = Area.H;
		const FFVRect Panel(Area.X + 4.f, Area.Y, S.bP ? 30.f : 24.f, H);
		D.Rect(Panel, FVPal::WoodDark());
		D.Rect(Panel.Inset(1.f), FVPal::Wood());
		D.Rect(FFVRect(Panel.X + 1.f, Panel.Y + 1.f, Panel.W - 2.f, 1.f), FVPal::WoodLight());
		const FFVRect Track(Panel.X + 5.f, Panel.Y + 4.f, Panel.W - 10.f, H - 8.f);
		D.Rect(Track, FV::Col(40, 90, 150));
		const float FishU = 0.5f + 0.38f * FMath::Sin(T * 0.9f) + 0.08f * FMath::Sin(T * 2.7f);
		const float BarU = 0.5f + 0.36f * FMath::Sin(T * 0.9f - 0.35f);
		const float BarH = Track.H * 0.34f;
		const float BarY = FMath::RoundToFloat(Track.Y + (Track.H - BarH) * FMath::Clamp(BarU, 0.f, 1.f));
		const float FishY = FMath::RoundToFloat(Track.Y + (Track.H - 12.f) * FMath::Clamp(FishU, 0.f, 1.f));
		const bool bIn = FishY + 6.f >= BarY && FishY + 6.f <= BarY + BarH;
		D.Rect(FFVRect(Track.X, BarY, Track.W, BarH), bIn ? FV::Col(120, 230, 90, 220) : FV::Col(90, 190, 70, 170));
		if (const FFVSprite* FishIcon = FVUI::Icon(EFVIcon::Fish))
		{
			D.SpriteCentered(*FishIcon, FVector2D(Track.X + Track.W * 0.5f, FishY + 6.f), FMath::Min(1.f, 16.f / FMath::Max(1.f, FishIcon->W)));
		}
		const float Prog = 0.5f + 0.45f * FMath::Sin(T * 0.5f);
		const FFVRect Meter(Panel.Right() + 2.f, Panel.Y + 2.f, 5.f, H - 4.f);
		D.Rect(Meter, FVPal::WoodDark());
		const float MH = FMath::RoundToFloat((Meter.H - 2.f) * Prog);
		D.Rect(FFVRect(Meter.X + 1.f, Meter.Bottom() - 1.f - MH, 3.f, MH), FVUI::LerpLinear(FVPal::Red(), FVPal::Green(), Prog));

		const float TX = Meter.Right() + 10.f;
		const float TW = Area.Right() - TX;
		float Y = Area.Y + 2.f;
		const bool bT = Touch();
		Y += D.TextWrapped(bT ? TEXT("Палец на экране — зона вверх") : TEXT("Держите — зона вверх"), FVector2D(TX, Y), TW, S.Body, FVPal::Ink(), true) + 2.f;
		Y += D.TextWrapped(bT ? TEXT("Отпустите палец — зона вниз") : TEXT("Отпустите — зона вниз"), FVector2D(TX, Y), TW, S.Body, FVPal::Ink(), true) + 4.f;
		D.TextWrapped(TEXT("Рыба в зоне — шкала справа растёт. Вне зоны — убывает. Полная шкала — рыба ваша!"), FVector2D(TX, Y), TW, S.Small + 0.5f, FVPal::InkSoft(), false, 1.05f);
	}

	/** Caption lines of a picture (at most 2): the name (bold ink) wraps once at a space when it is wider than MaxW;
	    text after a newline is a soft second line. OutTitleLines = how many lines belong to the name. Used for both
	    measuring and drawing, so the heights always match. */
	void CaptionLines(const FVDraw& D, const FString& Caption, float MaxW, float Size, TArray<FString>& Out, int32& OutTitleLines)
	{
		Out.Reset();
		OutTitleLines = 1;
		TArray<FString> Parts;
		Caption.ParseIntoArray(Parts, TEXT("\n"), false);
		if (Parts.Num() == 0) { Out.Add(FString()); return; }
		const FString& Name = Parts[0];
		int32 Cut = INDEX_NONE;
		if (Parts.Num() == 1 && D.MeasureText(Name, Size, true).X > MaxW)
		{
			// wrap at the last space that keeps the first line inside MaxW
			for (int32 i = Name.Len() - 1; i > 0; --i)
			{
				if (Name[i] == TEXT(' ') && D.MeasureText(Name.Left(i), Size, true).X <= MaxW) { Cut = i; break; }
			}
		}
		if (Cut != INDEX_NONE)
		{
			Out.Add(Name.Left(Cut));
			Out.Add(Name.Mid(Cut + 1));
			OutTitleLines = 2;
			return;
		}
		Out.Add(Name);
		if (Parts.Num() > 1) { Out.Add(Parts[1]); }
	}

	/** Measures (bDraw false) or draws a block at (X, Y) with width W. Returns its height including the gap below. */
	float DoBlock(FVDraw& D, const FBlock& B, float X, float Y, float W, const FStyle& S, bool bDraw)
	{
		switch (B.Kind)
		{
		case EBlk::Para:
		{
			const float H = D.MeasureWrapped(B.Text, W, S.Body, false, 1.12f);
			if (bDraw) { D.TextWrapped(B.Text, FVector2D(X, Y), W, S.Body, FVPal::Ink(), false, 1.12f); }
			return FMath::CeilToFloat(H) + S.Gap;
		}
		case EBlk::Sub:
		{
			const float LH = D.LineHeight(S.Sub, true);
			const float Top = S.Gap;
			if (bDraw)
			{
				D.Text(B.Text, FVector2D(X, Y + Top), S.Sub, FVPal::Ink(), EFVAlign::Left, false, true);
				D.Divider(FVector2D(X, FMath::RoundToFloat(Y + Top + LH + 1.f)), W);
			}
			return FMath::CeilToFloat(Top + LH + 1.f + 2.f + S.Gap);
		}
		case EBlk::Bullet:
		{
			const float Ind = S.bP ? 14.f : 11.f;
			const float H = D.MeasureWrapped(B.Text, W - Ind, S.Body, false, 1.1f);
			if (bDraw)
			{
				const float LH = D.LineHeight(S.Body, false);
				const float BY = FMath::RoundToFloat(Y + LH * 0.5f - 2.5f);
				D.Rect(FFVRect(X + 2.f, BY, 5.f, 5.f), FV::Col(150, 96, 40));
				D.Rect(FFVRect(X + 3.f, BY + 1.f, 3.f, 3.f), FVPal::Gold());
				D.TextWrapped(B.Text, FVector2D(X + Ind, Y), W - Ind, S.Body, FVPal::Ink(), false, 1.1f);
			}
			return FMath::CeilToFloat(H) + (S.bP ? 4.f : 3.f);
		}
		case EBlk::Tip:
		{
			const float Pad = S.bP ? 8.f : 6.f;
			const float IS = S.TipIcon;
			const float TS = S.Body - 0.5f;
			const float TW = W - 3.f - Pad * 3.f - IS;
			const float TH = D.MeasureWrapped(B.Text, TW, TS, false, 1.1f);
			const float H = FMath::CeilToFloat(FMath::Max(IS, TH) + Pad * 2.f);
			if (bDraw)
			{
				const FFVRect R(X, Y, W, H);
				D.Rect(R, FV::Col(196, 156, 92));
				D.Rect(R.Inset(1.f), FV::Col(255, 247, 216));
				D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 3.f, R.H - 2.f), FVPal::Green());
				DrawPic(D, B.Pic, FFVRect(R.X + 3.f + Pad, FMath::RoundToFloat(R.Y + (H - IS) * 0.5f), IS, IS), S.bP ? 2.f : 1.f);
				const float TY = R.Y + Pad + (TH < IS ? FMath::RoundToFloat((IS - TH) * 0.5f) : 0.f);
				D.TextWrapped(B.Text, FVector2D(R.X + 3.f + Pad * 2.f + IS, TY), TW, TS, FVPal::Ink(), false, 1.1f);
			}
			return H + S.Gap;
		}
		case EBlk::Note:
		{
			const float H = D.MeasureWrapped(B.Text, W, S.Small, false, 1.1f);
			if (bDraw) { D.TextWrapped(B.Text, FVector2D(X, Y), W, S.Small, FVPal::InkSoft(), false, 1.1f); }
			return FMath::CeilToFloat(H) + S.Gap;
		}
		case EBlk::Row:
		{
			const float Pad = S.bP ? 5.f : 4.f;
			const float Box = S.RowBox;
			const float TX = X + Pad + Box + (S.bP ? 8.f : 6.f);
			const float TW = X + W - Pad - TX;
			const float TitleLH = D.LineHeight(S.Body, true);
			const float BodyS = S.Small + (S.bP ? 0.5f : 0.f);
			const float BH = B.Text2.IsEmpty() ? 0.f : D.MeasureWrapped(B.Text2, TW, BodyS, false, 1.1f);
			const float H = FMath::CeilToFloat(FMath::Max(Box, TitleLH + (BH > 0.f ? 1.f + BH : 0.f)) + Pad * 2.f);
			if (bDraw)
			{
				const FFVRect R(X, Y, W, H);
				D.Rect(R, CardLine());
				D.Rect(R.Inset(1.f), CardFill());
				const FFVRect IB(X + Pad, Y + Pad, Box, Box);
				D.Rect(IB, FV::Col(214, 156, 94));
				D.Rect(IB.Inset(1.f), FV::Col(255, 250, 234));
				DrawPic(D, B.Pic, IB.Inset(S.bP ? 3.f : 2.f), S.bP ? 3.f : 2.f);
				const float TY = Y + Pad;
				const float TagS = S.Small + 0.5f;
				const float TagY = FMath::RoundToFloat(TY + (TitleLH - D.LineHeight(TagS, true)) * 0.5f);
				float TagW = 0.f;
				if (B.Price >= 0)
				{
					TagW = D.MoneyLabel(FVector2D(R.Right() - Pad - 1.f, TagY), B.Price, TagS, EFVAlign::Right, FVPal::Ink());
				}
				else if (!B.Right.IsEmpty())
				{
					TagW = D.MeasureText(B.Right, TagS, true).X;
					D.Text(B.Right, FVector2D(R.Right() - Pad - 1.f, TagY), TagS, B.RightColor, EFVAlign::Right, false, true);
				}
				D.Text(D.Ellipsize(B.Text, TW - (TagW > 0.f ? TagW + 6.f : 0.f), S.Body, true), FVector2D(TX, TY), S.Body, FVPal::Ink(), EFVAlign::Left, false, true);
				if (BH > 0.f) { D.TextWrapped(B.Text2, FVector2D(TX, TY + TitleLH + 1.f), TW, BodyS, FVPal::InkSoft(), false, 1.1f); }
			}
			return H + (S.bP ? 4.f : 3.f);
		}
		case EBlk::Pics:
		{
			const int32 N = B.Pics.Num();
			if (N == 0) { return 0.f; }
			const float CellW = S.PicCell, Box = S.PicBox, GapX = S.bP ? 6.f : 5.f;
			const float CapLH = D.LineHeight(S.Small, false);
			const int32 PerRow = FMath::Clamp(int32((W + GapX) / (CellW + GapX)), 1, N);
			const int32 Rows = (N + PerRow - 1) / PerRow;
			float H = 0.f;
			TArray<FString> Lines;
			int32 TitleLines = 1;
			for (int32 r = 0; r < Rows; ++r)
			{
				const int32 First = r * PerRow;
				const int32 Cnt = FMath::Min(PerRow, N - First);
				int32 MaxLines = 1;
				for (int32 k = 0; k < Cnt; ++k)
				{
					CaptionLines(D, B.Pics[First + k].Caption, CellW - 2.f, S.Small, Lines, TitleLines);
					MaxLines = FMath::Max(MaxLines, Lines.Num());
				}
				if (bDraw)
				{
					const float RowW = float(Cnt) * CellW + float(Cnt - 1) * GapX;
					const float RX = FMath::RoundToFloat(X + (W - RowW) * 0.5f);
					for (int32 k = 0; k < Cnt; ++k)
					{
						const FPicCap& PC = B.Pics[First + k];
						const float CX = RX + float(k) * (CellW + GapX);
						const FFVRect IB(FMath::RoundToFloat(CX + (CellW - Box) * 0.5f), Y + H, Box, Box);
						D.Rect(IB, FV::Col(214, 156, 94));
						D.Rect(IB.Inset(1.f), FV::Col(255, 250, 234));
						DrawPic(D, PC.Pic, IB.Inset(S.bP ? 4.f : 3.f), S.bP ? 3.f : 2.f);
						CaptionLines(D, PC.Caption, CellW - 2.f, S.Small, Lines, TitleLines);
						for (int32 l = 0; l < Lines.Num(); ++l)
						{
							const bool bFirst = l < TitleLines;
							D.Text(D.Ellipsize(Lines[l], CellW - 2.f, S.Small, bFirst), FVector2D(FMath::RoundToFloat(CX + CellW * 0.5f), IB.Bottom() + 3.f + float(l) * CapLH),
								S.Small, bFirst ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Center, false, bFirst);
						}
					}
				}
				H += Box + 3.f + float(MaxLines) * CapLH + (r < Rows - 1 ? 7.f : 0.f);
			}
			return FMath::CeilToFloat(H) + S.Gap;
		}
		case EBlk::Keys:
		{
			const float KW = S.KeyCol;
			const float DW = W - KW - 8.f;
			const float KeyH = D.LineHeight(S.KeyS, true) + 3.f;
			const float DH = D.MeasureWrapped(B.Text2, DW, S.Body, false, 1.05f);
			const float H = FMath::CeilToFloat(FMath::Max(KeyH, DH)) + (S.bP ? 6.f : 4.f);
			if (bDraw)
			{
				FVUI::KeyCap(D, D.Ellipsize(B.Text, KW - 10.f, S.KeyS, true), FVector2D(X, Y), S.KeyS);
				D.TextWrapped(B.Text2, FVector2D(X + KW + 8.f, Y + 1.f), DW, S.Body, FVPal::Ink(), false, 1.05f);
			}
			return H;
		}
		case EBlk::MiniBar:
		{
			const float H = S.bP ? 108.f : 86.f;
			if (bDraw) { DrawMiniBar(D, FFVRect(X, Y, W, H - 8.f), S); }
			return H;
		}
		default:
			return 0.f;
		}
	}

	/** Small gold "Новое" chip at (X, Y) top-left. Returns its width. */
	float NewChip(FVDraw& D, float X, float Y, float Size)
	{
		const FString L = TEXT("Новое");
		const FVector2D Sz = D.MeasureText(L, Size, true);
		const FFVRect R(FMath::RoundToFloat(X), FMath::RoundToFloat(Y), FMath::RoundToFloat(Sz.X + 8.f), FMath::RoundToFloat(Sz.Y + 2.f));
		D.Rect(R, FV::Col(176, 104, 16));
		D.Rect(R.Inset(1.f), FVPal::Gold());
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 240, 170));
		D.Text(L, FVector2D(R.X + 4.f, R.Y + 1.f), Size, FVPal::WoodDark(), EFVAlign::Left, false, true);
		return R.W;
	}

	float NewChipWidth(const FVDraw& D, float Size) { return FMath::RoundToFloat(D.MeasureText(TEXT("Новое"), Size, true).X + 8.f); }

	// =================================================================================================================
	// the screen
	// =================================================================================================================
	class FTutorialScreen final : public FVScreen
	{
	public:
		explicit FTutorialScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual bool HandlesEscape() const override { return true; }

		virtual void OnOpen() override
		{
			TickCount = 0;
			Build(NAME_None);
			const FName Ctx = Game ? Game->GetScreenContext() : NAME_None;
			int32 Start = FindEntry(Ctx);
			bEntryMode = Start != INDEX_NONE;
			if (Start == INDEX_NONE)
			{
				Start = FirstUnseen();
				if (Start == INDEX_NONE) { Start = 0; }
			}
			Pager = FFVPager();
			Pager.NumPagesCached = FMath::Max(1, Entries.Num());
			Pager.SetPage(FMath::Max(0, Start));
			ModeT = bEntryMode ? 1.f : 0.f;
			IndexScroll.Reset();
			bIndexFollow = true;
			bIndexInstant = true;
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal() || !bBuilt) { return; }
			const bool bP = FV::IsPortrait();
			if (In.WasPressed(EKeys::Escape) || (bP && bEntryMode && In.WasPressed(EKeys::BackSpace)))
			{
				if (bP && bEntryMode)
				{
					bEntryMode = false;
					FVUI::Sfx(EFVSfx::Close, 0.6f);
					return;
				}
				if (In.WasPressed(EKeys::Escape))
				{
					UFVGame* G = Game;
					G->CloseScreen();
					return;
				}
			}
			const int32 N = Entries.Num();
			if (N == 0) { return; }
			const bool bPaging = !bP || bEntryMode;
			if (bPaging)
			{
				if (In.WasPressed(EKeys::Left) || In.WasPressed(EKeys::A)) { GoToEntry(Pager.Page - 1); }
				if (In.WasPressed(EKeys::Right) || In.WasPressed(EKeys::D)) { GoToEntry(Pager.Page + 1); }
				FFVScrollView* S = PageScroll.IsValidIndex(Pager.Page) ? &PageScroll[Pager.Page] : nullptr;
				if (S)
				{
					if (In.WasPressed(EKeys::PageDown)) { S->ScrollTo(S->Target + S->View * 0.85f); }
					if (In.WasPressed(EKeys::PageUp)) { S->ScrollTo(S->Target - S->View * 0.85f); }
					if (In.WasPressed(EKeys::Home)) { S->ScrollTo(0.f); }
					if (In.WasPressed(EKeys::End)) { S->ScrollTo(S->MaxOffset()); }
					if (bP)
					{
						if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { S->ScrollTo(S->Target + 48.f); }
						if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { S->ScrollTo(S->Target - 48.f); }
					}
				}
			}
			if (!bP)
			{
				// landscape: ↑/↓ walk through the index (the page slides along)
				if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { GoToEntry(Pager.Page - 1); bIndexFollow = true; }
				if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { GoToEntry(Pager.Page + 1); bIndexFollow = true; }
			}
			else if (!bEntryMode && (In.WasPressed(EKeys::Enter) || In.WasPressed(EKeys::SpaceBar)))
			{
				const int32 U = FirstUnseen();
				OpenEntryPortrait(U != INDEX_NONE ? U : 0);
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bP = FV::IsPortrait();
			if (!bBuilt || bBuiltPortrait != bP)
			{
				// first frame / the phone was rotated: rebuild the texts (orientation-dependent wording), keep the entry
				const FName Keep = Entries.IsValidIndex(Pager.Page) ? Entries[Pager.Page].Id : NAME_None;
				Build(Keep);
				bIndexFollow = true;
				bIndexInstant = true;
			}
			const FStyle S = FStyle::Make();
			const FFVRect Win = bP ? FV::WindowRect() : FFVRect(10.f, 30.f, 620.f, 318.f);
			bool bClose = D.Window(Win, TEXT("Учебник"));
			if (bP) { DrawPortrait(D, Win, S, bClose); }
			else { DrawLandscape(D, Win, S); }

			// the entry on screen (at rest) counts as opened
			const bool bShown = !bP || (bEntryMode && ModeT > 0.99f);
			if (bShown && !Pager.IsAnimating() && Entries.IsValidIndex(Pager.Page)) { MarkSeen(Pager.Page); }

			if (bClose)
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
		}

	private:
		TArray<FEntry> Entries;
		TArray<FFVScrollView> PageScroll;   // one smooth scroll per entry page
		struct FLayout { float W = -1.f; bool bP = false; float Hero = 0.f; TArray<float> H; float Total = 0.f; };
		TArray<FLayout> Layouts;
		TSet<FName> Fresh;                  // entries that were unread when the guide was opened
		FFVPager Pager;                     // one page per entry (index order)
		FFVScrollView IndexScroll;
		bool bBuilt = false;
		bool bBuiltPortrait = false;
		bool bEntryMode = false;            // portrait: an entry page is open (else the index)
		float ModeT = 0.f;                  // portrait: 0 = index, 1 = entry (the slide between them)
		bool bIndexFollow = false;          // scroll the index to the current entry
		bool bIndexInstant = false;
		int32 TickCount = 0;

		// ------------------------------------------------------------------ data
		void Build(FName Keep)
		{
			bBuilt = true;
			bBuiltPortrait = FV::IsPortrait();
			Entries.Reset();
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			const FFacts F = P ? FFacts(*P) : FFacts();
			for (const FDef& Def : Defs())
			{
				const bool bSeen = P && P->SeenTutorial.Contains(Def.Id);
				if (!bSeen && !(Def.Unlocked && Def.Unlocked(F))) { continue; }   // opened once = stays in the guide
				FEntry& E = Entries.AddDefaulted_GetRef();
				E.Id = Def.Id;
				E.Cat = Def.Cat;
				if (Def.Build) { Def.Build(E, F, P); }
				if (!bSeen && P) { Fresh.Add(Def.Id); }
			}
			PageScroll.SetNum(Entries.Num());
			Layouts.Reset();
			Layouts.SetNum(Entries.Num());
			Pager.NumPagesCached = FMath::Max(1, Entries.Num());
			const int32 K = FindEntry(Keep);
			if (K != INDEX_NONE) { Pager.SetPage(K); }
			else { Pager.Page = FMath::Clamp(Pager.Page, 0, FMath::Max(0, Entries.Num() - 1)); Pager.SetPage(Pager.Page); }
		}

		int32 FindEntry(FName Id) const
		{
			if (Id.IsNone()) { return INDEX_NONE; }
			return Entries.IndexOfByPredicate([Id](const FEntry& E) { return E.Id == Id; });
		}

		bool IsUnseen(FName Id) const
		{
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			return P && !P->SeenTutorial.Contains(Id);
		}

		int32 FirstUnseen() const
		{
			for (int32 i = 0; i < Entries.Num(); ++i) { if (IsUnseen(Entries[i].Id)) { return i; } }
			return INDEX_NONE;
		}

		int32 UnseenCount() const
		{
			int32 N = 0;
			for (const FEntry& E : Entries) { if (IsUnseen(E.Id)) { ++N; } }
			return N;
		}

		void MarkSeen(int32 Idx)
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P || !Entries.IsValidIndex(Idx)) { return; }
			const FName Id = Entries[Idx].Id;
			if (P->SeenTutorial.Contains(Id)) { return; }
			P->SeenTutorial.Add(Id);
			Game->NotifyProfileChanged();
		}

		/** Slides the pager to entry Idx. A far jump starts one page away so it reads as a single slide. */
		void GoToEntry(int32 Idx)
		{
			const int32 N = Entries.Num();
			if (N == 0) { return; }
			Idx = FMath::Clamp(Idx, 0, N - 1);
			Pager.NumPagesCached = N;
			if (Idx == Pager.Page) { return; }
			const float Dist = float(Idx) - Pager.Visual;
			if (!Pager.bDrag && FMath::Abs(Dist) > 1.f) { Pager.Visual = float(Idx) - FMath::Sign(Dist); }
			Pager.GoTo(Idx);
		}

		/** Portrait: opens an entry from the index (the page slides in over the list). */
		void OpenEntryPortrait(int32 Idx)
		{
			if (!Entries.IsValidIndex(Idx)) { return; }
			Pager.NumPagesCached = Entries.Num();
			Pager.SetPage(Idx);
			if (PageScroll.IsValidIndex(Idx)) { PageScroll[Idx].Reset(); }
			bEntryMode = true;
			FVUI::Sfx(EFVSfx::Click, 0.7f, 1.05f);
		}

		const FLayout& LayoutFor(FVDraw& D, int32 Idx, float W, const FStyle& S)
		{
			FLayout& L = Layouts[Idx];
			if (L.W != W || L.bP != S.bP)
			{
				const FEntry& E = Entries[Idx];
				L.W = W;
				L.bP = S.bP;
				L.Hero = HeroHeight(D, E, W, S);
				L.H.SetNum(E.Blocks.Num());
				L.Total = L.Hero;
				for (int32 i = 0; i < E.Blocks.Num(); ++i)
				{
					L.H[i] = DoBlock(D, E.Blocks[i], 0.f, 0.f, W, S, false);
					L.Total += L.H[i];
				}
			}
			return L;
		}

		// ------------------------------------------------------------------ entry page
		float HeroHeight(FVDraw& D, const FEntry& E, float W, const FStyle& S) const
		{
			const float TX = S.HeroBox + (S.bP ? 10.f : 8.f);
			const float LeadS = S.Small + 0.5f;
			float TH = D.LineHeight(S.Small, true) + D.LineHeight(S.Title, true);
			if (!E.Lead.IsEmpty()) { TH += 1.f + D.MeasureWrapped(E.Lead, W - TX, LeadS, false, 1.05f); }
			return FMath::CeilToFloat(FMath::Max(S.HeroBox, TH)) + (S.bP ? 14.f : 11.f);
		}

		void DrawHero(FVDraw& D, const FEntry& E, float X, float Y, float W, float H, const FStyle& S) const
		{
			const FFVRect IB(X, Y, S.HeroBox, S.HeroBox);
			D.Rect(IB.Offset(1.f, 2.f), FV::Col(60, 30, 14, 60));
			D.Rect(IB, FVPal::WoodDark());
			D.Rect(IB.Inset(1.f), FVPal::ParchDark());
			D.Rect(IB.Inset(2.f), FV::Col(255, 236, 196));
			DrawPic(D, E.Icon, IB.Inset(S.bP ? 7.f : 5.f), 3.f);
			const float TX = X + S.HeroBox + (S.bP ? 10.f : 8.f);
			const float TW = X + W - TX;
			float TY = Y;
			const FString Cat = CatName(E.Cat);
			D.Text(Cat, FVector2D(TX, TY), S.Small, FVPal::InkSoft(), EFVAlign::Left, false, true);
			if (Fresh.Contains(E.Id))
			{
				NewChip(D, TX + D.MeasureText(Cat, S.Small, true).X + 6.f, TY, S.Small);
			}
			TY += D.LineHeight(S.Small, true);
			D.Text(D.Ellipsize(E.Title, TW, S.Title, true), FVector2D(TX, TY), S.Title, FVPal::Ink(), EFVAlign::Left, false, true);
			TY += D.LineHeight(S.Title, true) + 1.f;
			if (!E.Lead.IsEmpty()) { D.TextWrapped(E.Lead, FVector2D(TX, TY), TW, S.Small + 0.5f, FVPal::InkSoft(), false, 1.05f); }
			D.Divider(FVector2D(X, FMath::RoundToFloat(Y + H - (S.bP ? 8.f : 6.f))), W);
		}

		/** One page of the pager (local coords as if at rest): hero + blocks in the entry's own smooth scroll view. */
		void DrawEntryPage(FVDraw& D, int32 Idx, const FFVRect& Area, const FStyle& S)
		{
			if (!Entries.IsValidIndex(Idx)) { return; }
			const FEntry& E = Entries[Idx];
			const float PadX = S.bP ? 8.f : 10.f;
			const float X = Area.X + PadX;
			const float W = FMath::Max(80.f, Area.W - PadX * 2.f - 4.f);
			const FLayout& L = LayoutFor(D, Idx, W, S);
			FFVScrollView& Sc = PageScroll[Idx];
			const float TopPad = S.bP ? 6.f : 8.f;
			Sc.Update(D, Area, TopPad + L.Total + (S.bP ? 14.f : 8.f));
			D.PushClip(Area);
			float Y = Area.Y + TopPad - Sc.Offset();
			if (Y + L.Hero >= Area.Y) { DrawHero(D, E, X, Y, W, L.Hero, S); }
			Y += L.Hero;
			for (int32 i = 0; i < E.Blocks.Num() && i < L.H.Num(); ++i)
			{
				const float H = L.H[i];
				if (Y > Area.Bottom()) { break; }
				if (Y + H >= Area.Y) { DoBlock(D, E.Blocks[i], X, Y, W, S, true); }
				Y += H;
			}
			D.PopClip();
			Sc.DrawFades(D, Area, FVPal::Parchment());
			Sc.DrawBar(D, Area);
		}

		void DrawPager(FVDraw& D, const FFVRect& Area, const FStyle& S)
		{
			if (Entries.Num() == 0)
			{
				D.TextWrapped(TEXT("Статьи появятся, как только вы начнёте играть."), FVector2D(Area.X + 10.f, Area.Y + 10.f), Area.W - 20.f, S.Body, FVPal::InkSoft());
				return;
			}
			Pager.Update(D, Area, Entries.Num());
			if (Pager.PageChangedThisFrame()) { bIndexFollow = true; }
			Pager.DrawPages(D, Area, [this, &D, &Area, &S](int32 Page) { DrawEntryPage(D, Page, Area, S); });
			FVKit::SwipeHint(D, Area, Pager);
		}

		// ------------------------------------------------------------------ index
		struct FIndexRow { int32 Cat = -1; int32 Entry = -1; float Y = 0.f; float H = 0.f; };

		float BuildIndexRows(const FStyle& S, TArray<FIndexRow>& Out) const
		{
			Out.Reset();
			const float CatH = S.bP ? 32.f : 19.f;
			const float RowH = S.bP ? 50.f : 21.f;
			float Y = S.bP ? 0.f : 1.f;
			int32 LastCat = -1;
			for (int32 i = 0; i < Entries.Num(); ++i)
			{
				if (Entries[i].Cat != LastCat)
				{
					LastCat = Entries[i].Cat;
					FIndexRow& C = Out.AddDefaulted_GetRef();
					C.Cat = LastCat;
					C.Y = Y;
					C.H = CatH;
					Y += CatH;
				}
				FIndexRow& R = Out.AddDefaulted_GetRef();
				R.Entry = i;
				R.Y = Y;
				R.H = RowH;
				Y += RowH;
			}
			return Y + 6.f;
		}

		void DrawCatRow(FVDraw& D, int32 Cat, float X, float Y, float W, float H, const FStyle& S) const
		{
			const float IS = S.bP ? 16.f : 12.f;
			const float TS = S.bP ? S.Body + 1.f : 7.5f;
			const float LH = D.LineHeight(TS, true);
			const float TY = FMath::RoundToFloat(Y + H - LH - (S.bP ? 7.f : 5.f));
			if (const FFVSprite* Ic = FVUI::Icon(CatIcon(Cat)))
			{
				FVUI::SpriteFit(D, *Ic, FFVRect(X + 2.f, FMath::RoundToFloat(TY + LH * 0.5f - IS * 0.5f), IS, IS), FLinearColor::White, 1.f);
			}
			D.Text(CatName(Cat), FVector2D(X + IS + 6.f, TY), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			int32 New = 0;
			for (const FEntry& E : Entries) { if (E.Cat == Cat && IsUnseen(E.Id)) { ++New; } }
			if (New > 0)
			{
				const FString T = FString::Printf(TEXT("новых: %d"), New);
				D.Text(T, FVector2D(X + W - 2.f, TY + (S.bP ? 1.f : 0.5f)), S.Small, FV::Col(176, 104, 16), EFVAlign::Right, false, true);
			}
			D.Divider(FVector2D(X, FMath::RoundToFloat(Y + H - (S.bP ? 4.f : 3.f))), W);
		}

		/** Returns true when the row was tapped. */
		bool DrawEntryRow(FVDraw& D, int32 Idx, const FFVRect& R, const FStyle& S) const
		{
			const FEntry& E = Entries[Idx];
			const bool bSel = !S.bP && Idx == Pager.Page;
			const bool bHover = D.Hover(R);
			const bool bDown = bHover && D.IsPointerDown() && !D.IsDragGesture();
			const bool bNew = IsUnseen(E.Id);
			if (S.bP)
			{
				D.Rect(R, CardLine());
				D.Rect(R.Inset(1.f), bDown ? FV::Col(255, 226, 170) : CardFill());
				D.Rect(FFVRect(R.X + 1.f, R.Bottom() - 2.f, R.W - 2.f, 1.f), FV::Col(236, 204, 150));
			}
			else if (bSel)
			{
				D.Rect(R, FV::Col(226, 170, 96));
				D.Rect(R.Inset(1.f), FV::Col(255, 226, 158));
				D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 2.f, R.H - 2.f), FVPal::WoodLight());
			}
			else if (bHover)
			{
				D.Rect(R, FV::Col(255, 236, 196, 170));
			}
			const float Box = S.bP ? 36.f : 16.f;
			const FFVRect IB(R.X + (S.bP ? 6.f : 5.f), FMath::RoundToFloat(R.Y + (R.H - Box) * 0.5f), Box, Box);
			if (S.bP)
			{
				D.Rect(IB, FV::Col(214, 156, 94));
				D.Rect(IB.Inset(1.f), FV::Col(255, 250, 234));
				DrawPic(D, E.Icon, IB.Inset(4.f), 2.f);
			}
			else
			{
				DrawPic(D, E.Icon, IB, 1.f);
			}
			const float TX = IB.Right() + (S.bP ? 10.f : 5.f);
			float RightX = R.Right() - (S.bP ? 10.f : 5.f);
			if (S.bP)
			{
				const float CS = S.Body + 5.f;
				D.Text(TEXT("›"), FVector2D(RightX, FMath::RoundToFloat(R.Center().Y - D.LineHeight(CS, true) * 0.55f)), CS, FVPal::InkSoft(), EFVAlign::Right, false, true);
				RightX -= 14.f;
			}
			if (bNew)
			{
				if (S.bP)
				{
					const float CW = NewChipWidth(D, S.Small);
					NewChip(D, RightX - CW, FMath::RoundToFloat(R.Y + 7.f), S.Small);
					RightX -= CW + 6.f;
				}
				else
				{
					D.Circle(FVector2D(RightX - 3.f, R.Center().Y), 3.f, FV::Col(176, 104, 16));
					D.Circle(FVector2D(RightX - 3.f, R.Center().Y), 2.f, FVPal::Gold());
					RightX -= 10.f;
				}
			}
			if (S.bP)
			{
				const float TS = S.Body + 0.5f;
				const float LS = S.Small;
				const float TotalH = D.LineHeight(TS, true) + 1.f + D.LineHeight(LS, false);
				const float TY = FMath::RoundToFloat(R.Y + (R.H - TotalH) * 0.5f);
				D.Text(D.Ellipsize(E.Title, RightX - TX, TS, true), FVector2D(TX, TY), TS, FVPal::Ink(), EFVAlign::Left, false, true);
				const float LeadW = (bNew ? R.Right() - 24.f : RightX) - TX;
				D.Text(D.Ellipsize(E.Lead, LeadW, LS, false), FVector2D(TX, TY + D.LineHeight(TS, true) + 1.f), LS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			else
			{
				const float TS = 7.5f;
				const FString Shown = D.Ellipsize(E.Title, RightX - TX, TS, true);
				D.Text(Shown, FVector2D(TX, FMath::RoundToFloat(R.Y + (R.H - D.LineHeight(TS, true)) * 0.5f)), TS,
					bSel ? FVPal::Ink() : FV::Col(90, 58, 40), EFVAlign::Left, false, true);
				// mouse: the full title / «Новое» in a tooltip (the narrow index marks new entries with a gold dot)
				if (bHover && !Touch() && (bNew || Shown != E.Title))
				{
					D.Tooltip(E.Title, bNew ? FString::Printf(TEXT("Новое · %s"), *E.Lead) : E.Lead);
				}
			}
			return D.Clicked(R);
		}

		/** The scrolling index list in Area. Returns the tapped entry or INDEX_NONE. */
		int32 DrawIndexList(FVDraw& D, const FFVRect& Area, const FStyle& S, const FLinearColor& Bg)
		{
			TArray<FIndexRow> Rows;
			const float ContentH = BuildIndexRows(S, Rows);
			IndexScroll.Update(D, Area, ContentH);
			if (bIndexFollow)
			{
				for (int32 r = 0; r < Rows.Num(); ++r)
				{
					if (Rows[r].Entry != Pager.Page) { continue; }
					// include the category header right above the first entry of a category
					const bool bHeaderAbove = r > 0 && Rows[r - 1].Entry < 0;
					const float Top = bHeaderAbove ? Rows[r - 1].Y : Rows[r].Y;
					IndexScroll.ScrollToShow(Top, Rows[r].Y + Rows[r].H - Top + 4.f, bIndexInstant);
					break;
				}
				bIndexFollow = false;
				bIndexInstant = false;
			}
			int32 Picked = INDEX_NONE;
			D.PushClip(Area);
			const float Off = IndexScroll.Offset();
			for (const FIndexRow& Row : Rows)
			{
				const float Y = Area.Y - Off + Row.Y;
				if (Y + Row.H < Area.Y || Y > Area.Bottom()) { continue; }
				if (Row.Entry < 0)
				{
					DrawCatRow(D, Row.Cat, Area.X + 2.f, Y, Area.W - 8.f, Row.H, S);
				}
				else
				{
					const FFVRect RR(Area.X + (S.bP ? 0.f : 1.f), Y, Area.W - (S.bP ? 6.f : 7.f), Row.H - (S.bP ? 5.f : 1.f));
					if (DrawEntryRow(D, Row.Entry, RR, S)) { Picked = Row.Entry; }
				}
			}
			D.PopClip();
			IndexScroll.DrawFades(D, Area, Bg);
			IndexScroll.DrawBar(D, Area);
			return Picked;
		}

		// ------------------------------------------------------------------ landscape
		void DrawLandscape(FVDraw& D, const FFVRect& Win, const FStyle& S)
		{
			const FFVRect IndexR(Win.X + 8.f, Win.Y + 12.f, 184.f, Win.H - 20.f);
			const FFVRect PageR(IndexR.Right() + 6.f, IndexR.Y, Win.Right() - 8.f - (IndexR.Right() + 6.f), IndexR.H);

			// ---- index
			D.Panel(IndexR, EFVPanel::Parchment);
			const int32 Picked = DrawIndexList(D, IndexR.Inset(3.f), S, FVPal::Parchment());
			if (Picked != INDEX_NONE) { GoToEntry(Picked); }

			// ---- page + footer
			D.Panel(PageR, EFVPanel::Parchment);
			const float FootH = 24.f;
			const FFVRect PagerR(PageR.X + 2.f, PageR.Y + 2.f, PageR.W - 4.f, PageR.H - 4.f - FootH);
			DrawPager(D, PagerR, S);
			const int32 N = Entries.Num();
			if (N > 0)
			{
				const float FY = PageR.Bottom() - FootH + 1.f;
				D.Divider(FVector2D(PageR.X + 6.f, FY - 2.f), PageR.W - 12.f);
				const FFVRect Prev(PageR.X + 6.f, FY + 2.f, 84.f, 17.f);
				const FFVRect Next(PageR.Right() - 6.f - 84.f, FY + 2.f, 84.f, 17.f);
				if (D.Button(Prev, TEXT("‹ Раньше"), EFVButton::Normal, Pager.Page > 0, 7.5f)) { GoToEntry(Pager.Page - 1); }
				if (D.Button(Next, TEXT("Дальше ›"), EFVButton::Primary, Pager.Page < N - 1, 7.5f)) { GoToEntry(Pager.Page + 1); }
				const FString Mid = FString::Printf(TEXT("%d / %d"), Pager.Page + 1, N);
				D.Text(Mid, FVector2D(PageR.Center().X, FY + 5.f), 7.f, FVPal::InkSoft(), EFVAlign::Center, false, true);
			}
			if (!FV::IsTouchUI())
			{
				(void)0; /* update 3: no key hints at the bottom (see the guide) */
			}
		}

		// ------------------------------------------------------------------ portrait
		void DrawPortrait(FVDraw& D, const FFVRect& Win, const FStyle& S, bool& bClose)
		{
			const FFVRect Bar = FVDraw::ActionBarRect(Win);
			const float Top = Win.Y + 18.f;
			const float Bottom = Bar.Y - 10.f;
			const FFVRect Content(Win.X + 8.f, Top, Win.W - 16.f, FMath::Max(60.f, Bottom - Top));

			// index <-> entry: a horizontal push slide (interruptible like the pager)
			const float Target = bEntryMode ? 1.f : 0.f;
			ModeT = FVKit::Approach(ModeT, Target, D.FrameDt(), 0.07f);
			if (FMath::Abs(ModeT - Target) < 0.002f) { ModeT = Target; }
			const float Span = Content.W + 16.f;
			const float Slide = FMath::RoundToFloat(ModeT * Span);
			const bool bAnim = ModeT > 0.f && ModeT < 1.f;
			if (bAnim) { D.PushClip(FFVRect(Win.X + 5.f, Content.Y - 2.f, Win.W - 10.f, Content.H + 4.f)); }
			int32 Picked = INDEX_NONE;
			if (ModeT < 1.f)
			{
				const bool bWas = D.IsInputBlocked();
				if (bEntryMode) { D.SetInputBlocked(true); }
				D.PushTranslate(FVector2D(-Slide, 0.f));
				Picked = DrawIndexPortrait(D, Content, S);
				D.PopTranslate();
				D.SetInputBlocked(bWas);
			}
			if (ModeT > 0.f)
			{
				const bool bWas = D.IsInputBlocked();
				if (!bEntryMode) { D.SetInputBlocked(true); }
				D.PushTranslate(FVector2D(Span - Slide, 0.f));
				DrawEntryPortrait(D, Content, S);
				D.PopTranslate();
				D.SetInputBlocked(bWas);
			}
			if (bAnim) { D.PopClip(); }
			if (Picked != INDEX_NONE && !bEntryMode) { OpenEntryPortrait(Picked); }

			// ---- footer (static)
			FFVRect Actions;
			const bool bBack = D.ActionBar(Win, &Actions);
			const float TS = FV::Ui::Body() + 1.f;
			const int32 N = Entries.Num();
			if (bEntryMode)
			{
				if (D.Button(FVDraw::SplitH(Actions, 0, 2), TEXT("‹ Раньше"), EFVButton::Normal, Pager.Page > 0, TS)) { GoToEntry(Pager.Page - 1); }
				if (Pager.Page < N - 1)
				{
					if (D.Button(FVDraw::SplitH(Actions, 1, 2), TEXT("Дальше ›"), EFVButton::Primary, true, TS)) { GoToEntry(Pager.Page + 1); }
				}
				else if (D.Button(FVDraw::SplitH(Actions, 1, 2), TEXT("К списку"), EFVButton::Normal, true, TS))
				{
					bEntryMode = false;
				}
			}
			else
			{
				const int32 U = FirstUnseen();
				if (U != INDEX_NONE)
				{
					if (D.Button(FVDraw::SplitH(Actions, 0, 1), FString::Printf(TEXT("Читать новое (%d)"), UnseenCount()), EFVButton::Primary, true, TS)) { OpenEntryPortrait(U); }
				}
				else if (D.Button(FVDraw::SplitH(Actions, 0, 1), TEXT("К игре"), EFVButton::Normal, true, TS))
				{
					bClose = true;
				}
			}
			if (bBack)
			{
				if (bEntryMode) { bEntryMode = false; bIndexFollow = true; }
				else { bClose = true; }
			}
		}

		int32 DrawIndexPortrait(FVDraw& D, const FFVRect& Content, const FStyle& S)
		{
			// reading progress on top
			const int32 N = Entries.Num();
			const int32 Read = N - UnseenCount();
			const float HeadS = S.Small + 0.5f;
			const FString Left = FString::Printf(TEXT("Прочитано: %d из %d"), Read, N);
			D.Text(Left, FVector2D(Content.X + 2.f, Content.Y), HeadS, FVPal::Ink(), EFVAlign::Left, false, true);
			const float BX = Content.X + 2.f + D.MeasureText(Left, HeadS, true).X + 8.f;
			if (BX < Content.Right() - 30.f)
			{
				D.Bar(FFVRect(BX, Content.Y + 3.f, Content.Right() - 4.f - BX, 6.f), N > 0 ? float(Read) / float(N) : 0.f, FVPal::Green());
			}
			const float SubY = Content.Y + D.LineHeight(HeadS, true) + 1.f;
			D.Text(TEXT("Новые статьи появляются по мере игры"), FVector2D(Content.X + 2.f, SubY), S.Small, FVPal::InkSoft(), EFVAlign::Left, false, false);
			const float ListY = SubY + D.LineHeight(S.Small, false) + 6.f;
			const FFVRect List(Content.X, ListY, Content.W, Content.Bottom() - ListY);
			return DrawIndexList(D, List, S, FVPal::Parchment());
		}

		void DrawEntryPortrait(FVDraw& D, const FFVRect& Content, const FStyle& S)
		{
			const int32 N = Entries.Num();
			const float NavH = 18.f;
			if (Entries.IsValidIndex(Pager.Page))
			{
				// breadcrumb: category · position
				const float V = FMath::Clamp(Pager.VisualPos(), 0.f, float(FMath::Max(0, N - 1)));
				const int32 Near = FMath::Clamp(FMath::RoundToInt(V), 0, N - 1);
				const FEntry& E = Entries[Near];
				const float IS = 12.f;
				if (const FFVSprite* Ic = FVUI::Icon(CatIcon(E.Cat)))
				{
					FVUI::SpriteFit(D, *Ic, FFVRect(Content.X + 2.f, Content.Y, IS, IS), FLinearColor::White, 1.f);
				}
				D.Text(CatName(E.Cat), FVector2D(Content.X + IS + 7.f, Content.Y), S.Small, FVPal::InkSoft(), EFVAlign::Left, false, true);
				D.Text(FString::Printf(TEXT("%d из %d"), Pager.Page + 1, N), FVector2D(Content.Right() - 2.f, Content.Y), S.Small, FVPal::InkSoft(), EFVAlign::Right, false, true);
			}
			const FFVRect PagerR(Content.X, Content.Y + NavH, Content.W, Content.H - NavH);
			DrawPager(D, PagerR, S);
		}
	};
}

TUniquePtr<FVScreen> FVMakeTutorialScreen(UFVGame* Game)
{
	return MakeUnique<FVTutorialScreenImpl::FTutorialScreen>(Game);
}

int32 FVTutorialUnseenCount(UFVGame* Game)
{
	const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
	if (!P) { return 0; }
	const FVTutorialScreenImpl::FFacts F(*P);
	int32 N = 0;
	for (const FVTutorialScreenImpl::FDef& Def : FVTutorialScreenImpl::Defs())
	{
		if (Def.Unlocked && Def.Unlocked(F) && !P->SeenTutorial.Contains(Def.Id)) { ++N; }
	}
	return N;
}

// ============================================================================================
// UPDATE 6 [SCREEN-MISC] (Р43, §12.3)
// ============================================================================================
void FVTutorialAnnounce(UFVGame* Game, FName EntryId)
{
	// «Новое в учебнике: …» once per entry for the whole save: never for an entry already read (SeenTutorial), already
	// announced (U6.TutorialAnnounced) or not unlocked yet.
	using namespace FVTutorialScreenImpl;
	UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
	if (!P || EntryId.IsNone() || P->SeenTutorial.Contains(EntryId) || P->U6.TutorialAnnounced.Contains(EntryId)) { return; }
	const FDef* Def = Defs().FindByPredicate([EntryId](const FDef& D) { return D.Id == EntryId; });
	if (!Def || !Def->Build) { return; }
	const FFacts F(*P);
	if (Def->Unlocked && !Def->Unlocked(F)) { return; }
	FEntry E;
	E.Id = Def->Id;
	E.Cat = Def->Cat;
	Def->Build(E, F, P);
	P->U6.TutorialAnnounced.AddUnique(EntryId);
	Game->NotifyProfileChanged();
	Game->Toast(FString::Printf(TEXT("Новое в учебнике: %s"), *E.Title), FVPal::White(), EFVIcon::Book);
}
