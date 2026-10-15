// UPDATE 6 [DATA-ITEMS] — items, recipes, station levels, traders, junk, baits, tools, seeds, yard buildings, the boat
// (Public/FVDatabaseU6.h «DATA-ITEMS»; ids: Docs/UPDATE6_FOREST_CITY.md §17). Owner: DATA-ITEMS.
// Appenders run while FVDB builds its tables: never call FVDB table getters from them (see FVDatabase.cpp).
// Economy law (§5): every item is bought dearer than it can be sold anywhere (max markup: Лесник x1.25 on his list,
// Боцман / снасти Города x1.15 on everything else); seeds cost more than their expected harvest (§6.2).

#include "FVDatabase.h"

namespace FVDBU6ItemsImpl
{
	// Trader mask bits (§17.3). The classic file keeps its own Mitrich = 1 / Pier = 2.
	constexpr uint8 TrMitrich = 1;
	constexpr uint8 TrPier = 2;
	constexpr uint8 TrForester = 4;
	constexpr uint8 TrBuilder = 8;
	constexpr uint8 TrCityTackle = 16;
	constexpr uint8 TrGardener = 32;
	constexpr uint8 TrBoutique = 64;

	FFVIngredient Ing(const TCHAR* Id, int32 Count)
	{
		FFVIngredient I;
		I.ItemId = FName(Id);
		I.Count = FMath::Max(1, Count);
		return I;
	}

	/** Expert multipliers in one line: force, zone size, line strength, stability (1 = neutral). */
	FFVExpertStats XStats(float Force, float Zone, float Line, float Stability)
	{
		FFVExpertStats S;
		S.Force = Force;
		S.ZoneSize = Zone;
		S.LineStrength = Line;
		S.Stability = Stability;
		return S;
	}

	FFVItemDef* FindItemIn(TArray<FFVItemDef>& Items, const TCHAR* Id)
	{
		const FName Name(Id);
		for (FFVItemDef& I : Items)
		{
			if (I.Id == Name) { return &I; }
		}
		return nullptr;
	}

	bool HasItemId(const TArray<FFVItemDef>& Items, FName Id)
	{
		for (const FFVItemDef& I : Items)
		{
			if (I.Id == Id) { return true; }
		}
		return false;
	}

	FFVItemDef& AddItem(TArray<FFVItemDef>& Items, const TCHAR* Id, const TCHAR* Name, EFVItemKind Kind, EFVItemArt Art,
		int32 Sell, int32 MaxStack, const TCHAR* Desc)
	{
		FFVItemDef& I = Items.AddDefaulted_GetRef();
		I.Id = FName(Id);
		I.Name = Name;
		I.Description = Desc;
		I.Kind = Kind;
		I.Art = Art;
		I.SellPrice = Sell;
		I.MaxStack = FMath::Max(1, MaxStack);
		return I;
	}

	void Sold(FFVItemDef& I, int32 Buy, uint8 Mask, int32 MinLevel)
	{
		I.BuyPrice = Buy;
		I.TraderMask = Mask;
		I.MinLevel = MinLevel;
	}

	FFVRecipeDef& AddRecipe(TArray<FFVRecipeDef>& Recipes, const TCHAR* Id, EFVStation Station, int32 StationLevel, int32 MinLevel,
		TArray<FFVIngredient> Inputs, const TCHAR* Output, int32 OutputCount)
	{
		FFVRecipeDef& R = Recipes.AddDefaulted_GetRef();
		R.Id = FName(Id);
		R.Station = Station;
		R.StationLevel = FMath::Max(1, StationLevel);
		R.MinLevel = FMath::Max(1, MinLevel);
		R.Inputs = MoveTemp(Inputs);
		R.OutputId = FName(Output);
		R.OutputCount = FMath::Max(1, OutputCount);
		return R;
	}

	/** A recipe of a yard building: Station is ignored (contract), Count keeps it out of every house station list. */
	FFVRecipeDef& AddYardRecipe(TArray<FFVRecipeDef>& Recipes, const TCHAR* Id, EFVYardBuilding Yard, int32 BuildingLevel, int32 MinLevel,
		TArray<FFVIngredient> Inputs, const TCHAR* Output, int32 OutputCount)
	{
		FFVRecipeDef& R = AddRecipe(Recipes, Id, EFVStation::Count, BuildingLevel, MinLevel, MoveTemp(Inputs), Output, OutputCount);
		R.Yard = Yard;
		return R;
	}

	void AddLevel(TArray<FFVStationLevel>& Levels, const TCHAR* Name, int32 Price, TArray<FFVIngredient> Materials, int32 MinLevel,
		int32 Capacity, const TCHAR* Desc)
	{
		FFVStationLevel& L = Levels.AddDefaulted_GetRef();
		L.Name = Name;
		L.Description = Desc;
		L.Price = Price;
		L.Materials = MoveTemp(Materials);
		L.MinLevel = MinLevel;
		L.Capacity = Capacity;
	}

	FFVTraderDef& AddTrader(TArray<FFVTraderDef>& Traders, const TCHAR* Id, const TCHAR* Name, const TCHAR* Role, const TCHAR* Title,
		FName Location, uint8 Mask, const TCHAR* NpcId)
	{
		FFVTraderDef& T = Traders.AddDefaulted_GetRef();
		T.Id = FName(Id);
		T.Name = Name;
		T.Role = Role;
		T.ShopTitle = Title;
		T.Location = Location;
		T.Mask = Mask;
		T.NpcId = FName(NpcId);
		T.FishSellMult = 1.f;
		T.ItemSellMult = 1.f;
		T.bSellsClothes = false;
		T.bSellsUpgrades = false;
		return T;
	}

	FFVBaitDef& AddBait(TArray<FFVBaitDef>& Baits, const TCHAR* Id, const TCHAR* Name, EFVBaitStyle Style, int32 PackPrice, int32 PackSize,
		int32 MinLevel, float Bite, float Rare, float JunkMult, float Size, uint8 TraderMask, const TCHAR* Desc)
	{
		FFVBaitDef& B = Baits.AddDefaulted_GetRef();
		B.Id = FName(Id);
		B.Name = Name;
		B.Description = Desc;
		B.Style = Style;
		B.PackPrice = PackPrice;
		B.PackSize = FMath::Max(1, PackSize);
		B.MinLevel = MinLevel;
		B.BiteSpeed = Bite;
		B.RareBonus = Rare;
		B.JunkMult = JunkMult;
		B.SizeBonus = Size;
		B.TraderMask = TraderMask;
		return B;
	}

	FFVToolDef MakeTool(const TCHAR* Name, int32 Price, uint8 Mask, int32 MinLevel, int32 WorkshopLevel, bool bForge,
		TArray<FFVIngredient> Craft, EFVTreeKind MaxTree, float HitsMult)
	{
		FFVToolDef T;
		T.Name = Name;
		T.Price = Price;
		T.TraderMask = Mask;
		T.MinLevel = MinLevel;
		T.WorkshopLevel = WorkshopLevel;
		T.bForge = bForge;
		T.Craft = MoveTemp(Craft);
		T.MaxTree = MaxTree;
		T.HitsMult = HitsMult;
		return T;
	}

	FFVSeedDef MakeSeed(const TCHAR* SeedId, const TCHAR* CropId, float GrowMinutes, int32 YieldMin, int32 YieldMax,
		int32 SeedsMin, int32 SeedsMax, int32 MinLevel)
	{
		FFVSeedDef S;
		S.SeedId = FName(SeedId);
		S.CropId = FName(CropId);
		S.GrowMinutes = GrowMinutes;
		S.YieldMin = YieldMin;
		S.YieldMax = FMath::Max(YieldMin, YieldMax);
		S.SeedsMin = SeedsMin;
		S.SeedsMax = FMath::Max(SeedsMin, SeedsMax);
		S.RainMult = 1.25f;
		S.MinLevel = MinLevel;
		return S;
	}

	/** Chops needed by an axe of HitsMult 1 (§4: 3–6 accurate hits by the kind of tree). */
	int32 BaseTreeHits(EFVTreeKind Kind)
	{
		switch (Kind)
		{
		case EFVTreeKind::Birch:  return 3;
		case EFVTreeKind::Pine:   return 4;
		case EFVTreeKind::Oak:    return 5;
		case EFVTreeKind::Spruce: return 6;
		default:                  return 4;
		}
	}

	/** The best price any trader pays for one item of normal quality (Лесник: his list only, x BuyListMult). */
	float MaxSellPrice(FName ItemId)
	{
		const FFVItemDef* It = FVDB::FindItem(ItemId);
		if (!It || It->SellPrice <= 0) { return 0.f; }
		const float Base = float(It->SellPrice);
		float Best = 0.f;
		for (const FFVTraderDef& T : FVDB::Traders())
		{
			float Mult = 0.f;
			if ((It->BuyerMask & T.Mask) != 0) { Mult = T.BuyListMult; }   // same rule as FVDB::TraderSellPrice
			else if (!T.bBuysOnlyList) { Mult = T.ItemSellMult; }
			Best = FMath::Max(Best, Base * FMath::Max(0.f, Mult));
		}
		return Best > 0.f ? Best : Base;
	}

	const TArray<FFVYardBuildingDef>& BuildYard()
	{
		static const TArray<FFVYardBuildingDef> Defs = []()
		{
			TArray<FFVYardBuildingDef> Out;
			const TCHAR* Names[] = { TEXT("Мини-лесопилка"), TEXT("Огород"), TEXT("Кузня") };
			const TCHAR* Descs[] = {
				TEXT("Пилит брёвна на доски и собирает каркас лодки. Место под неё — справа во дворе."),
				TEXT("Грядки для моркови, тростника, ржи и городских семян. Всё растёт, пока вы играете: сон рост не ускоряет."),
				TEXT("Переплавляет металлолом в МПК, куёт гвозди, кованый топор и катушки для экспертных удочек.") };
			static_assert(UE_ARRAY_COUNT(Names) == int32(EFVYardBuilding::Count), "one name per yard building");
			for (int32 i = 0; i < int32(EFVYardBuilding::Count); ++i)
			{
				FFVYardBuildingDef D;
				D.Building = EFVYardBuilding(i);
				D.Id = FVDBU6::YardBuildingId(D.Building);
				D.Name = Names[i];
				D.Description = Descs[i];
				D.bNeedsBlockageCleared = (D.Building == EFVYardBuilding::Sawmill);
				switch (D.Building)
				{
				case EFVYardBuilding::Sawmill:
					// Capacity = logs in the saw queue
					AddLevel(D.Levels, TEXT("Мини-лесопилка"), 2500, { Ing(TEXT("mat_log"), 4), Ing(TEXT("res_pebble"), 10) }, 4, 1,
						TEXT("Бревно — в доски: распил по линии даёт 2–4 доски, быстрый распил — 2. Каркас лодки."));
					AddLevel(D.Levels, TEXT("Пилорама"), 12000, { Ing(TEXT("mat_log"), 16) }, 11, 3,
						TEXT("+1 доска к каждому распилу — и по линии, и быстрому."));
					break;
				case EFVYardBuilding::Garden:
					// Capacity = beds
					AddLevel(D.Levels, TEXT("Огород"), 6000, { Ing(TEXT("mat_board"), 8), Ing(TEXT("res_clay"), 10) }, 7, 6,
						TEXT("6 грядок: морковь, тростник, рожь и семена из Города."));
					AddLevel(D.Levels, TEXT("Большой огород"), 9000,
						{ Ing(TEXT("mat_board"), 12), Ing(TEXT("res_clay"), 15), Ing(TEXT("mat_log"), 4) }, 8, 9,
						TEXT("9 грядок."));
					AddLevel(D.Levels, TEXT("Огород с парником"), 16000,
						{ Ing(TEXT("mat_board"), 16), Ing(TEXT("res_clay"), 20), Ing(TEXT("mat_nails"), 10) }, 10, 12,
						TEXT("12 грядок."));
					break;
				case EFVYardBuilding::Forge:
					AddLevel(D.Levels, TEXT("Кузня"), 15000,
						{ Ing(TEXT("res_pebble"), 30), Ing(TEXT("res_clay"), 20), Ing(TEXT("mat_scrap"), 6), Ing(TEXT("mat_log"), 6) }, 10, 0,
						TEXT("МПК и гвозди из металлолома, кованый топор, лёгкая и тяжёлая катушки."));
					AddLevel(D.Levels, TEXT("Закалочная кузня"), 25000, { Ing(TEXT("mat_hardrock"), 8), Ing(TEXT("mat_mpk"), 6) }, 12, 0,
						TEXT("Закалка: катушки второго уровня, закалённая кирка и экспертные удочки."));
					break;
				default:
					break;
				}
				Out.Add(MoveTemp(D));
			}
			return Out;
		}();
		return Defs;
	}
}

// ============================================================================================
// Table appenders (called by FVDBU6::AppendHomeTables / AppendClassicTables while FVDB builds)
// ============================================================================================
void FVDBU6::AppendItems(TArray<FFVItemDef>& Items)
{
	using namespace FVDBU6ItemsImpl;
	using K = EFVItemKind;
	using A = EFVItemArt;

	// ---------------------------------------------------------------- existing rows (§5): Лесник buys wood; Р6 boards x2
	if (FFVItemDef* Branch = FindItemIn(Items, TEXT("res_branch"))) { Branch->BuyerMask |= TrForester; }
	if (FFVItemDef* Board = FindItemIn(Items, TEXT("mat_board")))
	{
		Board->BuyerMask |= TrForester;
		Board->BuyPrice = 80;   // Р6: 40 -> 80 at Митрич
	}

	// Never append twice (a second build must not duplicate ids)
	if (HasItemId(Items, FName(TEXT("mat_log")))) { return; }

	// ---------------------------------------------------------------- materials
	AddItem(Items, TEXT("mat_log"), TEXT("Бревно"), K::Material, A::Log, 10, 99,
		TEXT("Ровное бревно из леса. На лесопилке из него выходят доски, а Лесник Потапыч охотно его покупает.")).BuyerMask = TrForester;
	AddItem(Items, TEXT("mat_hardrock"), TEXT("Твёрдая порода"), K::Material, A::HardRock, 20, 99,
		TEXT("Плотный камень из валунов Пещеры. В магазинах его не купить: из него делают экспертные лески, катушки и удочки.")).MinLevel = 12;
	AddItem(Items, TEXT("mat_mpk"), TEXT("МПК"), K::Material, A::Mpk, 28, 99,
		TEXT("Металл плохого качества: кузня выплавляет его из металлолома. Нужен для кирки, кованого топора и катушек.")).MinLevel = 10;
	AddItem(Items, TEXT("mat_nails"), TEXT("Гвозди"), K::Material, A::Nails, 3, 99,
		TEXT("Кованые гвозди из металлолома. Двадцать штук — и лодку можно обшивать.")).MinLevel = 10;
	AddItem(Items, TEXT("mat_boat_frame"), TEXT("Каркас лодки"), K::Material, A::BoatFrame, 0, 5,
		TEXT("Остов будущей лодки из брёвен, смолы и глины. Отнесите его к сломанной лодке на Морском причале."));

	// ---------------------------------------------------------------- seeds (Kind Seed; the gardener buys them back at her markup)
	uint8 SeedVariant = 0;   // FFVItemDef::Tier of a seed packet = the crop drawn in its window (FVArtItemsU6 SeedPacket)
	auto AddSeedItem = [&Items, &SeedVariant](const TCHAR* Id, const TCHAR* Name, FColor Tint, int32 Sell, int32 Buy, int32 MinLevel, const TCHAR* Desc)
	{
		FFVItemDef& I = AddItem(Items, Id, Name, EFVItemKind::Seed, EFVItemArt::SeedPacket, Sell, 99, Desc);
		I.Tint = Tint;
		I.Tier = SeedVariant++;
		I.BuyerMask = TrGardener;
		if (Buy > 0) { Sold(I, Buy, TrGardener, MinLevel); }
		else { I.MinLevel = MinLevel; }
	};
	AddSeedItem(TEXT("seed_carrot"), TEXT("Семена моркови"), FColor(236, 128, 40), 4, 0, 1,
		TEXT("Редкая находка у Горного озера. Посадите в огороде — вырастет морковь, без которой не сварить уху."));
	AddSeedItem(TEXT("seed_potato"), TEXT("Семенной картофель"), FColor(176, 136, 84), 5, 60, 10,
		TEXT("Посадите в огороде: через полчаса игры — пара картофелин для драников и рыбника."));
	AddSeedItem(TEXT("seed_onion"), TEXT("Лук-севок"), FColor(222, 184, 92), 4, 40, 10,
		TEXT("Маленькие луковички для грядки. Лук нужен в драники, луковый суп и рыбник."));
	AddSeedItem(TEXT("seed_dill"), TEXT("Семена укропа"), FColor(110, 170, 70), 3, 40, 10,
		TEXT("Душистый укроп растёт быстро. С ним готовят рыбу с укропом."));
	AddSeedItem(TEXT("seed_pumpkin"), TEXT("Семена тыквы"), FColor(240, 160, 50), 8, 120, 10,
		TEXT("Тыква растёт дольше всех, зато одна тыква — целая каша."));

	// ---------------------------------------------------------------- vegetables (Material: not gathered on locations)
	auto AddVeg = [&Items](const TCHAR* Id, const TCHAR* Name, EFVItemArt Art, int32 Sell, const TCHAR* Desc)
	{
		FFVItemDef& I = AddItem(Items, Id, Name, EFVItemKind::Material, Art, Sell, 99, Desc);
		I.BuyerMask = TrGardener;
	};
	AddVeg(TEXT("veg_carrot"), TEXT("Морковь"), A::Carrot, 8, TEXT("Сладкая морковь с грядки. Без неё настоящей ухи не сварить."));
	AddVeg(TEXT("veg_potato"), TEXT("Картофель"), A::Potato, 7, TEXT("Крепкая картофелина — для драников, супа и рыбника."));
	AddVeg(TEXT("veg_onion"), TEXT("Лук"), A::Onion, 6, TEXT("Золотистая луковица. Без лука не бывает ни драников, ни рыбника."));
	AddVeg(TEXT("veg_dill"), TEXT("Укроп"), A::Dill, 5, TEXT("Пучок свежего укропа — лучшая приправа к рыбе."));
	AddVeg(TEXT("veg_pumpkin"), TEXT("Тыква"), A::Pumpkin, 25, TEXT("Большая оранжевая тыква для тыквенной каши."));

	// ---------------------------------------------------------------- expert dishes (Русская печь; only in the expert fight, §6.2)
	auto AddDish = [&Items](const TCHAR* Id, const TCHAR* Name, EFVItemArt Art, int32 Sell, const FFVExpertStats& Buff, float Minutes,
		const TCHAR* Desc)
	{
		FFVItemDef& I = AddItem(Items, Id, Name, EFVItemKind::Food, Art, Sell, 20, Desc);
		I.ExpertBuff = Buff;
		I.BuffMinutes = Minutes;
		I.MinLevel = 11;
	};
	//                                                                                          force zone  line  stab
	AddDish(TEXT("food_draniki"), TEXT("Драники"), A::Draniki, 45, XStats(1.f, 1.f, 1.f, 1.25f), 240.f,
		TEXT("Хрустящие картофельные драники. В экспертной рыбалке течение и волны сносят вас меньше."));
	AddDish(TEXT("food_fish_dill"), TEXT("Рыба с укропом"), A::FishDill, 50, XStats(1.f, 1.f, 1.3f, 1.f), 240.f,
		TEXT("Рыба, запечённая с укропом. В экспертной рыбалке леска выдерживает больше."));
	AddDish(TEXT("food_onion_soup"), TEXT("Луковый суп"), A::OnionSoup, 55, XStats(1.25f, 1.f, 1.f, 1.f), 240.f,
		TEXT("Густой луковый суп с рыбой. В экспертной рыбалке прибавляет силы: разгон, толчок и тяга мощнее."));
	AddDish(TEXT("food_pumpkin_porridge"), TEXT("Тыквенная каша"), A::PumpkinPorridge, 60, XStats(1.f, 1.2f, 1.f, 1.f), 240.f,
		TEXT("Сладкая каша из печи. В экспертной рыбалке зона удочки становится больше."));
	AddDish(TEXT("food_fish_pie"), TEXT("Рыбник"), A::FishPie, 110, XStats(1.1f, 1.1f, 1.1f, 1.1f), 480.f,
		TEXT("Пирог с рыбой, луком и картошкой. В экспертной рыбалке понемногу помогает во всём, и надолго."));

	// ---------------------------------------------------------------- expert tackle (only on expert rods, §10.4–10.5)
	auto AddExpert = [&Items](const TCHAR* Id, const TCHAR* Name, EFVItemKind Kind, EFVItemArt Art, uint8 Tier, uint8 ExpertKind,
		FColor Tint, int32 Sell, int32 Buy, int32 MinLevel, const FFVExpertStats& Stats, const TCHAR* Desc)
	{
		FFVItemDef& I = AddItem(Items, Id, Name, Kind, Art, Sell, 5, Desc);
		I.Tier = Tier;
		I.Tint = Tint;
		I.bExpert = true;
		I.ExpertKind = ExpertKind;
		I.ExpertTackle = Stats;
		if (Buy > 0) { Sold(I, Buy, TrCityTackle, MinLevel); }
		else { I.MinLevel = MinLevel; }
	};
	const FColor NoTint = FColor::White;
	//                                                                                                        force  zone  line   stab
	AddExpert(TEXT("eline_thin"), TEXT("Тонкая экспертная леска"), K::Line, A::ExpertLine, 1, uint8(EFVExpertLine::Thin),
		FColor(196, 222, 238), 700, 3000, 11, XStats(1.f, 1.f, 0.85f, 1.f),
		TEXT("Почти невидима в воде — пугливая рыба её не замечает. Зато и рвётся легче."));
	AddExpert(TEXT("eline_heavy"), TEXT("Тяжёлая экспертная леска"), K::Line, A::ExpertLine, 2, uint8(EFVExpertLine::Heavy),
		FColor(72, 82, 98), 1600, 0, 12, XStats(1.f, 1.f, 1.35f, 1.f),
		TEXT("Свита с нитью из твёрдой породы: выдержит рывок крупной рыбы. Осторожную рыбу распугивает."));
	AddExpert(TEXT("efloat_surface"), TEXT("Поверхностный поплавок"), K::Float, A::FloatSurface, 1, uint8(EFVExpertFloat::Surface),
		NoTint, 600, 2500, 11, XStats(1.f, 1.08f, 1.f, 1.f),
		TEXT("Держит наживку у самой поверхности — для рыбы, что кормится наверху."));
	AddExpert(TEXT("efloat_deep"), TEXT("Глубинный поплавок"), K::Float, A::FloatDeep, 1, uint8(EFVExpertFloat::Deep),
		NoTint, 800, 3500, 11, XStats(1.f, 1.03f, 1.f, 1.f),
		TEXT("Тяжёлый поплавок с грузом: опускает наживку туда, где держится донная рыба."));
	AddExpert(TEXT("efloat_glow"), TEXT("Светящийся экспертный поплавок"), K::Float, A::FloatGlowExpert, 2, uint8(EFVExpertFloat::Glow),
		NoTint, 1800, 8000, 12, XStats(1.f, 1.05f, 1.f, 1.f),
		TEXT("Светится в темноте Пещеры и ночного моря. Некоторые рыбы идут только на свет."));
	AddExpert(TEXT("reel_light"), TEXT("Лёгкая катушка"), K::Reel, A::ReelLight, 1, uint8(EFVExpertReel::Light),
		NoTint, 1200, 0, 11, XStats(1.08f, 1.f, 1.f, 1.f),
		TEXT("Кованая катушка для экспертных удочек: быстрая и послушная."));
	AddExpert(TEXT("reel_heavy"), TEXT("Тяжёлая катушка"), K::Reel, A::ReelHeavy, 1, uint8(EFVExpertReel::Heavy),
		NoTint, 2000, 0, 11, XStats(1.2f, 1.f, 1.f, 1.15f),
		TEXT("Массивная катушка: больше силы и устойчивости против течения и волн."));
	AddExpert(TEXT("reel_light2"), TEXT("Закалённая лёгкая катушка"), K::Reel, A::ReelLight, 2, uint8(EFVExpertReel::Light),
		NoTint, 3200, 0, 12, XStats(1.14f, 1.f, 1.f, 1.08f),
		TEXT("Лёгкая катушка, закалённая в кузне с твёрдой породой."));
	AddExpert(TEXT("reel_heavy2"), TEXT("Закалённая тяжёлая катушка"), K::Reel, A::ReelHeavy, 2, uint8(EFVExpertReel::Heavy),
		NoTint, 4600, 0, 12, XStats(1.28f, 1.f, 1.f, 1.25f),
		TEXT("Тяжёлая катушка после закалки: для самых упрямых экспертных рыб."));

	// ---------------------------------------------------------------- blueprints (learnt on purchase, never stored)
	{
		FFVItemDef& I = AddItem(Items, TEXT("bp_pickaxe"), TEXT("Чертёж кирки"), K::Blueprint, A::Blueprint, 0, 1,
			TEXT("Чертёж кирки для Мастерской мастера: МПК, доски и ветки. Киркой пробивают завалы у входа в Пещеру."));
		Sold(I, 6000, TrCityTackle, 11);
	}
}

void FVDBU6::AppendRecipes(TArray<FFVRecipeDef>& Recipes)
{
	using namespace FVDBU6ItemsImpl;
	using S = EFVStation;
	using Y = EFVYardBuilding;
	using G = EFVCraftGame;

	// ---------------------------------------------------------------- Р41: the only уха needs a carrot (grown in the garden)
	for (FFVRecipeDef& R : Recipes)
	{
		if (R.Id != FName(TEXT("rcp_soup"))) { continue; }
		const FName Carrot(TEXT("veg_carrot"));
		bool bHas = false;
		for (const FFVIngredient& I : R.Inputs) { if (I.ItemId == Carrot) { bHas = true; } }
		if (!bHas) { R.Inputs.Add(Ing(TEXT("veg_carrot"), 1)); }
	}

	for (const FFVRecipeDef& R : Recipes)
	{
		if (R.Id == FName(TEXT("rcp_saw"))) { return; }   // already appended
	}

	// ---------------------------------------------------------------- sawmill (§6.1): the saw game gives 2 / 3 / 4 boards by accuracy
	// (< 70 % / >= 70 % / >= 90 %), «Пилорама» +1; OutputCount is the quick result.
	AddYardRecipe(Recipes, TEXT("rcp_saw"), Y::Sawmill, 1, 4, { Ing(TEXT("mat_log"), 1) }, TEXT("mat_board"), 2).bSawGame = true;
	AddYardRecipe(Recipes, TEXT("rcp_saw_quick"), Y::Sawmill, 1, 4, { Ing(TEXT("mat_log"), 1) }, TEXT("mat_board"), 2);
	AddYardRecipe(Recipes, TEXT("rcp_boat_frame"), Y::Sawmill, 1, 4,
		{ Ing(TEXT("mat_log"), 8), Ing(TEXT("res_resin"), 6), Ing(TEXT("res_clay"), 6) }, TEXT("mat_boat_frame"), 1);

	// ---------------------------------------------------------------- forge (§6.3)
	AddYardRecipe(Recipes, TEXT("rcp_mpk"), Y::Forge, 1, 10, { Ing(TEXT("mat_scrap"), 3) }, TEXT("mat_mpk"), 1).bForgeGame = true;
	AddYardRecipe(Recipes, TEXT("rcp_nails"), Y::Forge, 1, 10, { Ing(TEXT("mat_scrap"), 2) }, TEXT("mat_nails"), 5);
	AddYardRecipe(Recipes, TEXT("rcp_smelt_can"), Y::Forge, 1, 10, { Ing(TEXT("junk_can"), 1) }, TEXT("mat_scrap"), 1).bRecycle = true;
	AddYardRecipe(Recipes, TEXT("rcp_reel_light"), Y::Forge, 1, 11,
		{ Ing(TEXT("mat_mpk"), 2), Ing(TEXT("mat_board"), 1) }, TEXT("reel_light"), 1).bForgeGame = true;
	AddYardRecipe(Recipes, TEXT("rcp_reel_heavy"), Y::Forge, 1, 11,
		{ Ing(TEXT("mat_mpk"), 4), Ing(TEXT("mat_board"), 1), Ing(TEXT("mat_nails"), 5) }, TEXT("reel_heavy"), 1).bForgeGame = true;
	AddYardRecipe(Recipes, TEXT("rcp_reel_light2"), Y::Forge, 2, 12,
		{ Ing(TEXT("reel_light"), 1), Ing(TEXT("mat_hardrock"), 2), Ing(TEXT("mat_mpk"), 1) }, TEXT("reel_light2"), 1).bForgeGame = true;
	AddYardRecipe(Recipes, TEXT("rcp_reel_heavy2"), Y::Forge, 2, 12,
		{ Ing(TEXT("reel_heavy"), 1), Ing(TEXT("mat_hardrock"), 3), Ing(TEXT("mat_mpk"), 2) }, TEXT("reel_heavy2"), 1).bForgeGame = true;

	// ---------------------------------------------------------------- 0.6.2 (Р80–Р82): expert rods of «Закалочная кузня»
	// (a rod output goes to OwnedRods and into the hands; a rod input is перековка: the rod is given up, its tackle comes
	// back — only rods that a trader sells again are used as inputs). «Румб» and «Полюс» are sold (FVDatabaseU6Expert.cpp).
	AddYardRecipe(Recipes, TEXT("rcp_rod_surf"), Y::Forge, 2, 13,
		{ Ing(TEXT("rod_breeze"), 1), Ing(TEXT("mat_hardrock"), 6), Ing(TEXT("mat_mpk"), 4), Ing(TEXT("eline_thin"), 1) },
		TEXT("rod_surf"), 1).bForgeGame = true;
	AddYardRecipe(Recipes, TEXT("rcp_rod_mirror"), Y::Forge, 2, 14,
		{ Ing(TEXT("rod_twins"), 1), Ing(TEXT("junk_bottle"), 10), Ing(TEXT("mat_hardrock"), 8), Ing(TEXT("mat_mpk"), 5),
		  Ing(TEXT("efloat_surface"), 2) },
		TEXT("rod_mirror"), 1).bForgeGame = true;
	AddYardRecipe(Recipes, TEXT("rcp_rod_anchor"), Y::Forge, 2, 14,
		{ Ing(TEXT("eline_heavy"), 2), Ing(TEXT("reel_heavy"), 1), Ing(TEXT("mat_nails"), 20), Ing(TEXT("mat_mpk"), 10),
		  Ing(TEXT("mat_hardrock"), 10) },
		TEXT("rod_anchor"), 1).bForgeGame = true;
	{
		// the best rod of the game (Р82): перековка «Румба» + the best tackle + «Ихтиолог»
		FFVRecipeDef& R = AddYardRecipe(Recipes, TEXT("rcp_rod_helmsman"), Y::Forge, 2, 15,
			{ Ing(TEXT("rod_rhumb"), 1), Ing(TEXT("reel_light2"), 1), Ing(TEXT("eline_thin"), 2), Ing(TEXT("efloat_glow"), 1),
			  Ing(TEXT("mat_board"), 12), Ing(TEXT("mat_hardrock"), 12), Ing(TEXT("mat_mpk"), 8) },
			TEXT("rod_helmsman"), 1);
		R.bForgeGame = true;
		R.RequiredAchievement = FName(TEXT("ach_species_all"));
	}

	// ---------------------------------------------------------------- workshop 4 «Мастерская мастера» (§9б, §10.9)
	// (the pickaxe itself is a tool: FVDBU6::Pickaxes()[0], crafted by the blueprint)
	{
		FFVRecipeDef& R = AddRecipe(Recipes, TEXT("rcp_eline_heavy"), S::Workshop, 4, 12,
			{ Ing(TEXT("mat_hardrock"), 1), Ing(TEXT("mat_mpk"), 1), Ing(TEXT("mat_rope"), 2) }, TEXT("eline_heavy"), 1);
		R.Game = G::Knots;
		R.GameDifficulty = 7;
	}
	AddRecipe(Recipes, TEXT("rcp_bait_marlin"), S::Workshop, 4, 11,
		{ Ing(TEXT("efish_barracuda"), 1), Ing(TEXT("fish_horse_mackerel"), 3), Ing(TEXT("mat_hardrock"), 1) }, TEXT("bait_marlin"), 2);
	AddRecipe(Recipes, TEXT("rcp_bait_harbor"), S::Workshop, 4, 11,
		{ Ing(TEXT("efish_moray"), 1), Ing(TEXT("fish_sea_burbot"), 3), Ing(TEXT("mat_mpk"), 1) }, TEXT("bait_harbor"), 2);
	AddRecipe(Recipes, TEXT("rcp_bait_giant"), S::Workshop, 4, 12,
		{ Ing(TEXT("efish_stone_char"), 1), Ing(TEXT("fish_cave_gudgeon"), 3), Ing(TEXT("mat_hardrock"), 2) }, TEXT("bait_giant"), 2);
	AddRecipe(Recipes, TEXT("rcp_bait_dragon"), S::Workshop, 4, 12,
		{ Ing(TEXT("efish_crystal_eel"), 1), Ing(TEXT("fish_blind_char"), 2), Ing(TEXT("mat_hardrock"), 1), Ing(TEXT("mat_glitter"), 2) },
		TEXT("bait_dragon"), 2);

	// ---------------------------------------------------------------- kitchen 4 «Русская печь» (§6.2): expert dishes
	auto Dish = [&Recipes](const TCHAR* Id, TArray<FFVIngredient> In, const TCHAR* Out, int32 Cuts, float Speed)
	{
		FFVRecipeDef& R = AddRecipe(Recipes, Id, EFVStation::Kitchen, 4, 11, MoveTemp(In), Out, 1);
		R.Game = EFVCraftGame::Cooking;
		R.GameDifficulty = Cuts;
		R.CookSpeed = Speed;
	};
	Dish(TEXT("rcp_draniki"), { Ing(TEXT("veg_potato"), 2), Ing(TEXT("veg_onion"), 1) }, TEXT("food_draniki"), 3, 1.1f);
	Dish(TEXT("rcp_fish_dill"), { Ing(TEXT("any_fish"), 1), Ing(TEXT("veg_dill"), 2) }, TEXT("food_fish_dill"), 2, 1.1f);
	Dish(TEXT("rcp_onion_soup"), { Ing(TEXT("veg_onion"), 2), Ing(TEXT("any_fish"), 1), Ing(TEXT("veg_potato"), 1) }, TEXT("food_onion_soup"), 3, 1.15f);
	Dish(TEXT("rcp_pumpkin_porridge"), { Ing(TEXT("veg_pumpkin"), 1), Ing(TEXT("res_berries"), 2) }, TEXT("food_pumpkin_porridge"), 2, 1.1f);
	Dish(TEXT("rcp_fish_pie"), { Ing(TEXT("any_fish"), 2), Ing(TEXT("veg_onion"), 1), Ing(TEXT("veg_potato"), 1), Ing(TEXT("bait_dough"), 2) },
		TEXT("food_fish_pie"), 4, 1.2f);
}

void FVDBU6::AppendStationLevels(TArray<FFVStationDef>& Stations)
{
	using namespace FVDBU6ItemsImpl;
	using S = EFVStation;

	// The levels of a station, or nullptr when it is missing / already has the update-6 level (append only once, at the end)
	auto Levels = [&Stations](S Which, const TCHAR* NewLevelName) -> TArray<FFVStationLevel>*
	{
		for (FFVStationDef& St : Stations)
		{
			if (St.Station != Which || St.Levels.Num() == 0) { continue; }
			for (const FFVStationLevel& L : St.Levels) { if (L.Name == NewLevelName) { return nullptr; } }
			return &St.Levels;
		}
		return nullptr;
	};

	if (TArray<FFVStationLevel>* L = Levels(S::Workshop, TEXT("Мастерская мастера")))
	{
		AddLevel(*L, TEXT("Мастерская мастера"), 20000,
			{ Ing(TEXT("mat_mpk"), 6), Ing(TEXT("mat_board"), 20), Ing(TEXT("fish_horse_mackerel"), 2), Ing(TEXT("fish_sea_burbot"), 2) }, 11, 0,
			TEXT("Кирка по чертежу, тяжёлая экспертная леска и наживки экспертных легенд."));
	}
	if (TArray<FFVStationLevel>* L = Levels(S::Kitchen, TEXT("Русская печь")))
	{
		AddLevel(*L, TEXT("Русская печь"), 15000, { Ing(TEXT("res_pebble"), 40), Ing(TEXT("res_clay"), 25), Ing(TEXT("mat_mpk"), 4) }, 11, 0,
			TEXT("Блюда из городских семян для экспертной рыбалки. Готовит две порции за раз."));
	}
	if (TArray<FFVStationLevel>* L = Levels(S::Radio, TEXT("Радиостанция")))
	{
		AddLevel(*L, TEXT("Радиостанция"), 12000, { Ing(TEXT("mat_mpk"), 3), Ing(TEXT("mat_board"), 8) }, 11, 0,
			TEXT("Прогноз местной погоды Города на завтра по часам: штиль, ветер, шторм, туман и прилив."));
	}
	if (TArray<FFVStationLevel>* L = Levels(S::Smokehouse, TEXT("Каменная коптильня")))
	{
		AddLevel(*L, TEXT("Каменная коптильня"), 18000, { Ing(TEXT("mat_hardrock"), 6), Ing(TEXT("mat_mpk"), 4) }, 12, 12,
			TEXT("12 мест. Коптит и экспертную рыбу — вдвое дороже."));
	}
	if (TArray<FFVStationLevel>* L = Levels(S::Aquarium, TEXT("Океанариум")))
	{
		AddLevel(*L, TEXT("Океанариум"), 30000,
			{ Ing(TEXT("mat_hardrock"), 8), Ing(TEXT("junk_bottle"), 12), Ing(TEXT("mat_glue"), 6) }, 12, 16,
			TEXT("До 16 рыб, в том числе экспертные рыбы и легенды."));
	}
	if (TArray<FFVStationLevel>* L = Levels(S::WormFarm, TEXT("Глубинный червятник")))
	{
		AddLevel(*L, TEXT("Глубинный червятник"), 10000, { Ing(TEXT("mat_hardrock"), 4), Ing(TEXT("res_clay"), 15) }, 12, 30,
			TEXT("Каждое утро ещё и глубинные черви для экспертной рыбалки, вмещает 30."));
	}
	if (TArray<FFVStationLevel>* L = Levels(S::IceCellar, TEXT("Глубокий ледник")))
	{
		AddLevel(*L, TEXT("Глубокий ледник"), 15000, { Ing(TEXT("mat_hardrock"), 6), Ing(TEXT("res_pebble"), 40) }, 12, 50,
			TEXT("Хранит 50 рыб."));
	}
	if (TArray<FFVStationLevel>* L = Levels(S::TrophyWall, TEXT("Большая стена трофеев")))
	{
		AddLevel(*L, TEXT("Большая стена трофеев"), 12000, { Ing(TEXT("mat_hardrock"), 4), Ing(TEXT("mat_board"), 12) }, 12, 12,
			TEXT("+6 табличек для экспертных рыб: всего 12."));
	}
}

void FVDBU6::AppendTraders(TArray<FFVTraderDef>& Traders)
{
	using namespace FVDBU6ItemsImpl;
	for (const FFVTraderDef& T : Traders)
	{
		if (T.Id == FName(TEXT("tr_forester"))) { return; }   // already appended
	}
	const FName Forest(TEXT("forest"));   // FVDBU6::ForestId() (not called here: FVDB is being built)
	const FName City(TEXT("city"));

	{
		FFVTraderDef& T = AddTrader(Traders, TEXT("tr_forester"), TEXT("Лесник Потапыч"), TEXT("покупает ветки, брёвна и доски"),
			TEXT("Лесная лавка"), Forest, TrForester, TEXT("npc_potapych"));
		T.BuyListMult = 1.25f;
		T.bBuysOnlyList = true;
		T.bSellsClothes = true;
	}
	{
		FFVTraderDef& T = AddTrader(Traders, TEXT("tr_builder"), TEXT("Прораб Гаврила"), TEXT("постройки, декор двора и мебель"),
			TEXT("Стройдвор"), Forest, TrBuilder, TEXT("npc_gavrila"));
		T.bBuysOnlyList = true;   // buys nothing
		T.bSellsBuildings = true;
	}
	// the tackle shop is the first City trader: FVDB::TraderAt("city") returns it (fish are sold here)
	{
		FFVTraderDef& T = AddTrader(Traders, TEXT("tr_city_tackle"), TEXT("Снастёвщик Кузьма"), TEXT("снасти эксперта и чертежи"),
			TEXT("Снасти Города"), City, TrCityTackle, TEXT("npc_kuzma"));
		T.FishSellMult = 1.25f;
		T.ItemSellMult = 1.15f;
	}
	{
		FFVTraderDef& T = AddTrader(Traders, TEXT("tr_gardener"), TEXT("Огородница Агафья"), TEXT("семена для огорода"),
			TEXT("Семена и рассада"), City, TrGardener, TEXT("npc_agafya"));
		T.BuyListMult = 1.1f;     // vegetables and seeds only
		T.bBuysOnlyList = true;
		T.bSellsSeeds = true;
	}
	{
		FFVTraderDef& T = AddTrader(Traders, TEXT("tr_boutique"), TEXT("Хозяйка бутика Жанна"), TEXT("элитная одежда и украшения двора"),
			TEXT("Бутик «Жемчужина»"), City, TrBoutique, TEXT("npc_zhanna"));
		T.bBuysOnlyList = true;   // buys nothing
		T.bBoutique = true;
		T.bSellsClothes = true;
	}
	static_assert(TrMitrich == 1 && TrPier == 2, "classic trader bits");
}

void FVDBU6::AppendJunk(TArray<FFVJunkDef>& Junk)
{
	for (const FFVJunkDef& J : Junk)
	{
		if (J.Id == FName(TEXT("junk_scrap"))) { return; }
	}
	// §5.1 (Р7): scrap is caught as junk; catching it gives mat_scrap (FVGame), the price equals mat_scrap
	FFVJunkDef& J = Junk.AddDefaulted_GetRef();
	J.Id = FName(TEXT("junk_scrap"));
	J.Name = TEXT("Ржавая железка");
	J.Description = TEXT("Кусок ржавого металла со дна. Годится в металлолом.");
	J.Style = EFVJunkStyle::Scrap;
	J.Price = 8;
}

void FVDBU6::AppendBaits(TArray<FFVBaitDef>& Baits)
{
	using namespace FVDBU6ItemsImpl;
	using BS = EFVBaitStyle;
	for (const FFVBaitDef& B : Baits)
	{
		if (B.Id == FName(TEXT("bait_trash"))) { return; }   // already appended (bait_worm stays [0]: we only append)
	}
	//                                                                                          pack size lvl  bite   rare   junk   size
	AddBait(Baits, TEXT("bait_trash"), TEXT("Мусорная приманка"), BS::Trash, 150, 5, 4, 1.15f, 0.00f, 4.00f, 0.00f, TrPier,
		TEXT("Пахнет ржавчиной и тиной. Рыба клюёт на неё неохотно, зато мусора — вчетверо больше, и среди него часто попадается металлолом."));
	AddBait(Baits, TEXT("bait_deepworm"), TEXT("Глубинный червь"), BS::DeepWorm, 0, 5, 12, 0.80f, 0.20f, 0.80f, 0.10f, 0,
		TEXT("Бледный червь из глубинного червятника. Экспертная наживка: на неё идёт рыба с самого дна."));
	// Р17: legend baits — crafted at «Мастерская мастера» (2 per recipe), spent one per bite like any bait (Р27)
	AddBait(Baits, TEXT("bait_marlin"), TEXT("Приманка на марлина"), BS::LegendMarlin, 0, 2, 11, 1.f, 0.f, 1.f, 0.f, 0,
		TEXT("Барракуда со ставридой на крючке из твёрдой породы. Царь-марлин клюёт на неё наверняка — если выполнены все его условия."));
	AddBait(Baits, TEXT("bait_harbor"), TEXT("Приманка Хозяина гавани"), BS::LegendHarbor, 0, 2, 11, 1.f, 0.f, 1.f, 0.f, 0,
		TEXT("Мурена и морской налим в железной оплётке. Хозяин гавани от неё не откажется — при всех своих условиях."));
	AddBait(Baits, TEXT("bait_giant"), TEXT("Приманка на великана"), BS::LegendGiant, 0, 2, 12, 1.f, 0.f, 1.f, 0.f, 0,
		TEXT("Каменный голец с пещерными пескарями, утяжелённые породой. Для Каменного великана у подводного ключа."));
	AddBait(Baits, TEXT("bait_dragon"), TEXT("Приманка на дракона"), BS::LegendDragon, 0, 2, 12, 1.f, 0.f, 1.f, 0.f, 0,
		TEXT("Кристальный угорь в блёстках — сверкает в темноте. Хрустальный дракон идёт на неё, если снасть подобрана верно."));
}

// ============================================================================================
// Tools, seeds, yard, boat
// ============================================================================================
const TArray<FFVToolDef>& FVDBU6::Axes()
{
	using namespace FVDBU6ItemsImpl;
	using T = EFVTreeKind;
	// §4: crafted levels cost Price / 2 coins + materials (like the shovels)
	static const TArray<FFVToolDef> A = {
		MakeTool(TEXT("Топор"), 5000, TrPier, 4, 0, false, {}, T::Pine, 1.f),
		MakeTool(TEXT("Железный топор"), 3000, 0, 5, 2, false, { Ing(TEXT("mat_log"), 4), Ing(TEXT("mat_scrap"), 3) }, T::Oak, 0.9f),
		MakeTool(TEXT("Стальной топор"), 8000, 0, 7, 3, false,
			{ Ing(TEXT("mat_log"), 6), Ing(TEXT("mat_scrap"), 6), Ing(TEXT("mat_glue"), 2) }, T::Spruce, 0.8f),
		MakeTool(TEXT("Кованый топор"), 16000, 0, 10, 0, true, { Ing(TEXT("mat_mpk"), 4), Ing(TEXT("mat_log"), 4) }, T::Spruce, 0.6f),
	};
	return A;
}

const TArray<FFVToolDef>& FVDBU6::Pickaxes()
{
	using namespace FVDBU6ItemsImpl;
	// [0]: Мастерская 4 by the blueprint bp_pickaxe (Р3: МПК, доски, палки); [1]: tempered at the forge (hard rock + МПК).
	// HitsMult of a pickaxe = x boulder shake.
	static const TArray<FFVToolDef> A = {
		MakeTool(TEXT("Кирка"), 4000, 0, 11, 4, false,
			{ Ing(TEXT("mat_mpk"), 5), Ing(TEXT("mat_board"), 4), Ing(TEXT("res_branch"), 6) }, EFVTreeKind::Pine, 1.f),
		MakeTool(TEXT("Закалённая кирка"), 12000, 0, 12, 2, true,   // forge level 2 «Закалочная кузня»
			{ Ing(TEXT("mat_hardrock"), 6), Ing(TEXT("mat_mpk"), 4) }, EFVTreeKind::Pine, 0.7f),
	};
	return A;
}

const TArray<FFVSeedDef>& FVDBU6::Seeds()
{
	using namespace FVDBU6ItemsImpl;
	// §6.2: carrot 20 min -> 1–2 carrots + 1–2 seeds (more than one on average); reed 10 min -> 3 reeds;
	// City seeds 20–40 min -> vegetables + sometimes a seed. Seed price > expected harvest (ExpectedSeedHarvestValue).
	static const TArray<FFVSeedDef> A = {
		MakeSeed(TEXT("seed_carrot"), TEXT("veg_carrot"), 20.f, 1, 2, 1, 2, 7),
		MakeSeed(TEXT("res_reed"), TEXT("res_reed"), 10.f, 3, 3, 0, 0, 7),
		MakeSeed(TEXT("seed_potato"), TEXT("veg_potato"), 30.f, 2, 3, 0, 1, 7),
		MakeSeed(TEXT("seed_onion"), TEXT("veg_onion"), 25.f, 1, 3, 0, 1, 7),
		MakeSeed(TEXT("seed_dill"), TEXT("veg_dill"), 20.f, 2, 3, 0, 1, 7),
		MakeSeed(TEXT("seed_pumpkin"), TEXT("veg_pumpkin"), 40.f, 1, 1, 0, 1, 7),
		FVMill::RyeSeed(),   // §18: seed_rye -> veg_rye (the miller sells the seeds)
	};
	return A;
}

const FFVSeedDef* FVDBU6::FindSeed(FName SeedId)
{
	for (const FFVSeedDef& S : Seeds())
	{
		if (S.SeedId == SeedId) { return &S; }
	}
	return nullptr;
}

const TArray<FFVYardBuildingDef>& FVDBU6::YardBuildings()
{
	return FVDBU6ItemsImpl::BuildYard();
}

const FFVYardBuildingDef& FVDBU6::YardBuilding(EFVYardBuilding B)
{
	const TArray<FFVYardBuildingDef>& All = YardBuildings();
	return All[FMath::Clamp(int32(B), 0, All.Num() - 1)];
}

const FFVYardBuildingDef* FVDBU6::FindYardBuilding(FName Id)
{
	for (const FFVYardBuildingDef& D : YardBuildings())
	{
		if (D.Id == Id) { return &D; }
	}
	return nullptr;
}

TArray<const FFVRecipeDef*> FVDBU6::RecipesForYard(EFVYardBuilding B)
{
	TArray<const FFVRecipeDef*> Out;
	for (const FFVRecipeDef& R : FVDB::Recipes())
	{
		if (R.Yard == B) { Out.Add(&R); }
	}
	return Out;
}

const TArray<FFVBoatStage>& FVDBU6::BoatStages()
{
	using namespace FVDBU6ItemsImpl;
	// §7 (Р9): 1 frame, 2 planks + nails (20 nails = 8 scrap), 3 tar. Each stage is paid by its part of the materials.
	static const TArray<FFVBoatStage> A = []()
	{
		TArray<FFVBoatStage> Out;
		auto Add = [&Out](const TCHAR* Name, TArray<FFVIngredient> Cost)
		{
			FFVBoatStage& S = Out.AddDefaulted_GetRef();
			S.Name = Name;
			S.Cost = MoveTemp(Cost);
			S.Coins = 0;
			S.MinLevel = FV::ClassicMaxLevel;
		};
		Add(TEXT("Каркас"), { Ing(TEXT("mat_boat_frame"), 1) });
		Add(TEXT("Обшивка"), { Ing(TEXT("mat_board"), 40), Ing(TEXT("mat_nails"), 20) });
		Add(TEXT("Смоление"), { Ing(TEXT("res_resin"), 20) });
		return Out;
	}();
	return A;
}

// ============================================================================================
// Forest / City / Cave nodes (§4, §8, §9а)
// ============================================================================================
int32 FVDBU6::TreeHits(EFVTreeKind Kind, int32 AxeLevel)
{
	const TArray<FFVToolDef>& All = Axes();
	if (AxeLevel < 1 || All.Num() == 0 || Kind >= EFVTreeKind::Count) { return 0; }
	const FFVToolDef& Axe = All[FMath::Min(AxeLevel, All.Num()) - 1];
	if (uint8(Kind) > uint8(Axe.MaxTree)) { return 0; }
	const float Hits = float(FVDBU6ItemsImpl::BaseTreeHits(Kind)) * FMath::Max(0.1f, Axe.HitsMult);
	return FMath::Max(2, FMath::RoundToInt(Hits));
}

TArray<FFVIngredient> FVDBU6::RollTreeLoot(EFVTreeKind Kind, int32 BonusBranches, FRandomStream& Rng)
{
	using namespace FVDBU6ItemsImpl;
	// §4: one log always (owner's rule), 1–3 branches (oak / spruce 2–3) + the chopping bonus; pine / spruce: resin 0–1
	TArray<FFVIngredient> Out;
	Out.Add(Ing(TEXT("mat_log"), 1));
	const bool bBig = (Kind == EFVTreeKind::Oak || Kind == EFVTreeKind::Spruce);
	const int32 Branches = Rng.RandRange(bBig ? 2 : 1, 3) + FMath::Clamp(BonusBranches, 0, 5);
	Out.Add(Ing(TEXT("res_branch"), Branches));
	if (Kind == EFVTreeKind::Pine || Kind == EFVTreeKind::Spruce)
	{
		const int32 Resin = Rng.RandRange(0, 1);
		if (Resin > 0) { Out.Add(Ing(TEXT("res_resin"), Resin)); }
	}
	return Out;
}

TArray<FFVIngredient> FVDBU6::BlockageLoot()
{
	// §4: the windfall gives 3 logs once (no scrap in the forest — Р7)
	return { FVDBU6ItemsImpl::Ing(TEXT("mat_log"), 3) };
}

TArray<FFVIngredient> FVDBU6::RollJunkPileLoot(FRandomStream& Rng)
{
	// §8 (Р2, Р21): a City junk pile gives 1–2 scrap
	return { FVDBU6ItemsImpl::Ing(TEXT("mat_scrap"), Rng.RandRange(1, 2)) };
}

float FVDBU6::TreeRegrowMinutes() { return 20.f; }
float FVDBU6::JunkPileRegrowMinutes() { return 15.f; }
float FVDBU6::BoulderRegrowMinutes() { return 30.f; }

float FVDBU6::ExpectedSeedHarvestValue(const FFVSeedDef& Seed)
{
	// Average crop + average returned seeds, each at the best price any trader pays (§5.3: must stay below the seed price)
	const float Crop = 0.5f * float(Seed.YieldMin + Seed.YieldMax) * FVDBU6ItemsImpl::MaxSellPrice(Seed.CropId);
	const float Back = 0.5f * float(Seed.SeedsMin + Seed.SeedsMax) * FVDBU6ItemsImpl::MaxSellPrice(Seed.SeedId);
	return FMath::Max(0.f, Crop + Back);
}
