// Update 2 content for "Клёвая Долина": generic items, recipes, house stations, traders, house decor, shovels,
// backpack levels, lure parts, gathering / digging rolls and the related presentation helpers.
// Tables are built lazily on first access (FVDB::Init() forces it) and never change afterwards, so pointers into them
// stay valid for the program lifetime. The base tables (fish, junk, baits...) live in FVDatabase.cpp; this file only
// reads them through the public FVDB API (the base build never calls back into this file's tables).

#include "FVDatabase.h"
#include "Misc/Crc.h"

namespace FVDBHome
{
	struct FData
	{
		bool bBuilt = false;
		bool bBuilding = false;

		TArray<FFVItemDef> Items;
		TArray<FFVRecipeDef> Recipes;
		TArray<FFVStationDef> Stations;
		TArray<FFVTraderDef> Traders;
		TArray<FFVDecorDef> Decor;
		TArray<FFVShovelDef> Shovels;
		TArray<FFVBackpackLevel> Backpack;
		TArray<FName> StarterDecor;

		TMap<FName, int32> ItemIdx, RecipeIdx, StationIdx, TraderIdx, DecorIdx;
	};

	// ============================================================================================
	// Small helpers
	// ============================================================================================
	FFVIngredient Ing(const TCHAR* Id, int32 Count)
	{
		FFVIngredient I;
		I.ItemId = FName(Id);
		I.Count = FMath::Max(1, Count);
		return I;
	}

	/** Modifiers in one line: bar, reel x, drain x, bite x, cast +, luck +, treasure +, size +, react +, junk x. */
	FFVStatMods Mods(float Bar, float Reel, float Drain, float Bite, float Cast, float Luck, float Treasure, float Size, float React, float Junk = 1.f)
	{
		FFVStatMods M;
		M.BarBonus = Bar;
		M.ReelMult = Reel;
		M.DrainMult = Drain;
		M.BiteSpeed = Bite;
		M.CastRange = Cast;
		M.Luck = Luck;
		M.TreasureBonus = Treasure;
		M.SizeBonus = Size;
		M.ReactBonus = React;
		M.JunkMult = Junk;
		return M;
	}

	template <typename T>
	void BuildIndex(const TArray<T>& Arr, TMap<FName, int32>& Out, const TCHAR* What)
	{
		Out.Reset();
		for (int32 i = 0; i < Arr.Num(); ++i)
		{
			if (Out.Contains(Arr[i].Id))
			{
				UE_LOG(LogFV, Error, TEXT("FV: duplicate %s id '%s'"), What, *Arr[i].Id.ToString());
				continue;
			}
			Out.Add(Arr[i].Id, i);
		}
	}

	template <typename T>
	const T* FindIn(const TArray<T>& Arr, const TMap<FName, int32>& Index, FName Id)
	{
		if (Id.IsNone()) { return nullptr; }
		const int32* Idx = Index.Find(Id);
		return (Idx && Arr.IsValidIndex(*Idx)) ? &Arr[*Idx] : nullptr;
	}

	FString JoinNames(const TArray<FString>& Names, int32 MaxShown)
	{
		FString Out;
		const int32 Shown = FMath::Min(Names.Num(), MaxShown);
		for (int32 i = 0; i < Shown; ++i)
		{
			if (i > 0) { Out += TEXT(", "); }
			Out += Names[i];
		}
		if (Names.Num() > Shown)
		{
			Out += FString::Printf(TEXT(" и ещё %d"), Names.Num() - Shown);
		}
		return Out;
	}

	/** Lower-cases Latin and Cyrillic letters without relying on the platform locale. */
	FString LowerRu(const FString& In)
	{
		FString Out = In;
		for (int32 i = 0; i < Out.Len(); ++i)
		{
			TCHAR& C = Out[i];
			if (C >= TEXT('A') && C <= TEXT('Z')) { C = TCHAR(C + 32); }
			else if (C >= 0x0410 && C <= 0x042F) { C = TCHAR(C + 0x20); }   // А..Я -> а..я
			else if (C == 0x0401) { C = TCHAR(0x0451); }                      // Ё -> ё
		}
		return Out;
	}

	/** Grammatical gender of a Russian fish name ("Хрустальная форель", "Жар-рыба", "Щука" -> feminine). */
	bool IsFeminineName(const FString& Name)
	{
		const FString L = LowerRu(Name.TrimStartAndEnd());
		if (L.IsEmpty()) { return false; }
		TArray<FString> Words;
		L.ParseIntoArray(Words, TEXT(" "), true);
		if (Words.Num() == 0) { return false; }
		if (Words.Num() > 1)
		{
			const FString& Adj = Words[0];
			if (Adj.EndsWith(TEXT("ая")) || Adj.EndsWith(TEXT("яя"))) { return true; }
			if (Adj.EndsWith(TEXT("ый")) || Adj.EndsWith(TEXT("ий")) || Adj.EndsWith(TEXT("ой"))) { return false; }
		}
		const FString& Noun = Words.Last();
		if (Noun.Contains(TEXT("-")))
		{
			TArray<FString> Parts;
			Noun.ParseIntoArray(Parts, TEXT("-"), true);
			for (const FString& P : Parts) { if (P == TEXT("рыба")) { return true; } }
			return false;
		}
		if (Noun == TEXT("форель") || Noun == TEXT("мелочь")) { return true; }
		return Noun.EndsWith(TEXT("а")) || Noun.EndsWith(TEXT("я"));
	}

	/** Deterministic hashing (never GetTypeHash(FName): name indices differ between runs). */
	uint32 Mix(uint32 H, uint32 V)
	{
		H ^= V + 0x9E3779B9u + (H << 6) + (H >> 2);
		return H;
	}
	uint32 Fmix(uint32 H)
	{
		H ^= H >> 16; H *= 0x85EBCA6Bu; H ^= H >> 13; H *= 0xC2B2AE35u; H ^= H >> 16;
		return H;
	}
	/** Resource slot / dig spot counts per location (world anchors are authored to match). */
	int32 ResourceSlotCount(FName Loc)
	{
		static const FName Pond(TEXT("pond")), River(TEXT("river")), Mountain(TEXT("mountain")), Ocean(TEXT("ocean")),
			Swamp(TEXT("swamp")), Volcano(TEXT("volcano")), Home(TEXT("home")), Forest(TEXT("forest"));
		if (Loc == Forest) { return 10; }   // update 6 (§4): the forest edge — branches and pebbles (FVArtForest: 10 pickups)
		if (Loc == Pond || Loc == River || Loc == Swamp) { return 6; }
		if (Loc == Mountain || Loc == Ocean) { return 5; }
		if (Loc == Volcano) { return 4; }
		if (Loc == Home) { return 8; }
		return 0;
	}

	// ============================================================================================
	// Builders
	// ============================================================================================
	FFVItemDef& AddItem(FData& D, const TCHAR* Id, const TCHAR* Name, EFVItemKind Kind, EFVItemArt Art, int32 Sell, int32 MaxStack, const TCHAR* Desc)
	{
		FFVItemDef& I = D.Items.AddDefaulted_GetRef();
		I.Id = FName(Id);
		I.Name = Name;
		I.Description = Desc;
		I.Kind = Kind;
		I.Art = Art;
		I.SellPrice = Sell;
		I.MaxStack = FMath::Max(1, MaxStack);
		return I;
	}

	/** Sold by traders (Mask bits) at Buy coins from fishing level MinLevel. */
	void Sold(FFVItemDef& I, int32 Buy, uint8 Mask, int32 MinLevel)
	{
		I.BuyPrice = Buy;
		I.TraderMask = Mask;
		I.MinLevel = MinLevel;
	}

	constexpr uint8 Mitrich = 1;
	constexpr uint8 Pier = 2;

	void BuildItems(FData& D)
	{
		using K = EFVItemKind;
		using A = EFVItemArt;

		// ---------------------------------------------------------------- resources (gathered)
		AddItem(D, TEXT("res_branch"), TEXT("Ветка"), K::Resource, A::Branch, 2, 99,
			TEXT("Сухая ветка. Из веток делают доски, а в коптильне они служат дровами."));
		AddItem(D, TEXT("res_pebble"), TEXT("Камешек"), K::Resource, A::Pebble, 2, 99,
			TEXT("Гладкий камешек. Пригодится для очага, ледника и грузил."));
		AddItem(D, TEXT("res_fiber"), TEXT("Волокно"), K::Resource, A::Fiber, 3, 99,
			TEXT("Крепкие растительные волокна: из них вьют верёвки, лески и сети."));
		AddItem(D, TEXT("res_feather"), TEXT("Перо"), K::Resource, A::Feather, 5, 99,
			TEXT("Лёгкое птичье перо — для поплавков и мушек."));
		AddItem(D, TEXT("res_mushroom"), TEXT("Гриб"), K::Resource, A::Mushroom, 8, 99,
			TEXT("Крепкий лесной гриб. Без него не сварить настоящую уху."));
		AddItem(D, TEXT("res_berries"), TEXT("Ягоды"), K::Resource, A::Berries, 6, 99,
			TEXT("Спелые лесные ягоды — для чая, пирога и красных блёсен."));
		AddItem(D, TEXT("res_reed"), TEXT("Тростник"), K::Resource, A::Reed, 3, 99,
			TEXT("Полый стебель тростника: поплавки, тесто для наживки и зелёные блёсны."));
		AddItem(D, TEXT("res_clay"), TEXT("Глина"), K::Resource, A::Clay, 4, 99,
			TEXT("Вязкая глина для печи, червятника и прикормки."));
		AddItem(D, TEXT("res_resin"), TEXT("Смола"), K::Resource, A::Resin, 6, 99,
			TEXT("Липкая сосновая смола. Из неё варят клей."));

		// ---------------------------------------------------------------- materials (crafted / recycled)
		Sold(AddItem(D, TEXT("mat_board"), TEXT("Доска"), K::Material, A::Board, 12, 99,
			TEXT("Ровная струганая доска — главный строительный материал.")), 80, Mitrich, 1);   // update 6 (Р6): 40 -> 80
		Sold(AddItem(D, TEXT("mat_rope"), TEXT("Верёвка"), K::Material, A::Rope, 14, 99,
			TEXT("Крепкая верёвка для сетей, лесок и ремонта.")), 45, Mitrich, 1);
		Sold(AddItem(D, TEXT("mat_glue"), TEXT("Клей"), K::Material, A::Glue, 18, 99,
			TEXT("Смоляной клей. Держит намертво.")), 60, Mitrich, 1);
		AddItem(D, TEXT("mat_scrap"), TEXT("Металлолом"), K::Material, A::Scrap, 8, 99,
			TEXT("Ржавые железки из переработанного мусора. Для блёсен, грузил и лопат."));
		Sold(AddItem(D, TEXT("mat_sinker"), TEXT("Грузило"), K::Material, A::Sinker, 6, 99,
			TEXT("Маленький свинцовый груз для поплавков и сетей.")), 20, Mitrich, 1);
		Sold(AddItem(D, TEXT("mat_glitter"), TEXT("Блёстки"), K::Material, A::Glitter, 15, 99,
			TEXT("Сверкающая крошка из битого стекла. Делает блёсны заметнее.")), 50, Pier, 1);

		// ---------------------------------------------------------------- food (buff scaled by FoodPower(quality))
		{
			FFVItemDef& F = AddItem(D, TEXT("food_fried"), TEXT("Жареная рыба"), K::Food, A::FriedFish, 35, 20,
				TEXT("Румяная, хрустящая. Руки сами крутят катушку."));
			F.Buff = Mods(0.f, 1.10f, 0.92f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			F.BuffMinutes = 180.f;
		}
		// update 5 (§10.2): fried fish by rarity (rcp_fried_rare / _epic / _legend: a fish of exactly that rarity)
		{
			FFVItemDef& F = AddItem(D, TEXT("food_fried_rare"), TEXT("Рыба по-монастырски"), K::Food, A::FriedFishRare, 80, 20,
				TEXT("Редкая рыба, томлённая с травами по старому монастырскому рецепту. Катушка идёт легко, рука твёрже."));
			F.Buff = Mods(4.f, 1.14f, 0.89f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			F.BuffMinutes = 210.f;
		}
		{
			FFVItemDef& F = AddItem(D, TEXT("food_fried_epic"), TEXT("Рыба по-царски"), K::Food, A::FriedFishEpic, 180, 20,
				TEXT("Эпическая рыба под царским соусом — такую подавали к столу государя. Сил хватит на самую упрямую добычу."));
			F.Buff = Mods(8.f, 1.18f, 0.86f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			F.BuffMinutes = 240.f;
		}
		{
			FFVItemDef& F = AddItem(D, TEXT("food_fried_legend"), TEXT("Пир на весь мир"), K::Food, A::FriedFishLegend, 400, 20,
				TEXT("Легендарная рыба на золотом блюде — праздник на всю долину. После такого пира и удача на твоей стороне."));
			F.Buff = Mods(12.f, 1.25f, 0.80f, 1.f, 0.f, 0.05f, 0.f, 0.f, 0.f);
			F.BuffMinutes = 300.f;
		}
		{
			FFVItemDef& F = AddItem(D, TEXT("food_tea"), TEXT("Травяной чай"), K::Food, A::HerbTea, 20, 20,
				TEXT("Бодрит и обостряет внимание: поклёвку не проморгаешь."));
			F.Buff = Mods(0.f, 1.f, 1.f, 0.85f, 0.f, 0.f, 0.f, 0.f, 0.15f);
			F.BuffMinutes = 240.f;
		}
		{
			FFVItemDef& F = AddItem(D, TEXT("food_soup"), TEXT("Уха"), K::Food, A::FishSoup, 60, 20,
				TEXT("Наваристая рыбацкая уха. После неё любая рыба по плечу."));
			F.Buff = Mods(16.f, 1.f, 0.94f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			F.BuffMinutes = 240.f;
		}
		{
			FFVItemDef& F = AddItem(D, TEXT("food_pie"), TEXT("Ягодный пирог"), K::Food, A::BerryPie, 70, 20,
				TEXT("Бабушкин рецепт. Говорят, приносит удачу."));
			F.Buff = Mods(0.f, 1.f, 1.f, 1.f, 0.f, 0.25f, 0.04f, 0.05f, 0.f);
			F.BuffMinutes = 300.f;
		}

		// ---------------------------------------------------------------- lines (rod slot "Леска")
		auto AddLine = [&D](const TCHAR* Id, const TCHAR* Name, uint8 Tier, FColor Tint, int32 Sell, int32 Buy, uint8 Mask,
			int32 MinLevel, const FFVStatMods& Tk, const TCHAR* Desc)
		{
			FFVItemDef& I = AddItem(D, Id, Name, EFVItemKind::Line, EFVItemArt::Line, Sell, 5, Desc);
			I.Tier = Tier;
			I.Tint = Tint;
			I.Tackle = Tk;
			Sold(I, Buy, Mask, MinLevel);
		};
		//                                                                                   bar   reel   drain  bite   cast  luck  treas  size react
		AddLine(TEXT("line_linen"), TEXT("Льняная леска"), 1, FColor(214, 196, 150), 40, 150, Mitrich, 1,
			Mods(0.f, 1.04f, 0.97f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f), TEXT("Простая, но надёжная леска из льна."));
		AddLine(TEXT("line_braided"), TEXT("Плетёная леска"), 2, FColor(90, 140, 80), 250, 900, Mitrich, 3,
			Mods(4.f, 1.08f, 0.94f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f), TEXT("Прочное плетение почти не тянется — каждая поклёвка чувствуется."));
		AddLine(TEXT("line_silk"), TEXT("Шёлковая леска"), 3, FColor(240, 236, 220), 600, 2500, Mitrich, 5,
			Mods(0.f, 1.12f, 0.90f, 1.f, 0.f, 0.f, 0.02f, 0.f, 0.f), TEXT("Тонкая и мягкая. Рыба её не замечает."));
		AddLine(TEXT("line_fluoro"), TEXT("Флюорокарбоновая леска"), 4, FColor(150, 210, 235), 1500, 6000, Pier, 7,
			Mods(0.f, 1.15f, 0.86f, 0.95f, 0.f, 0.f, 0.f, 0.f, 0.f), TEXT("Невидима в воде и не боится зубов хищников."));
		AddLine(TEXT("line_titan"), TEXT("Титановая струна"), 5, FColor(170, 170, 190), 4500, 18000, Pier, 9,
			Mods(8.f, 1.20f, 0.80f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f), TEXT("Выдержит даже легендарного гиганта."));
		// update 5: crafted only (Мастерская → Станок, rcp_line_heat), never sold; a bit weaker than the titanium string —
		// its worth is the heat: the Zhar-ryba ignores any other line
		AddLine(TEXT("line_heat"), TEXT("Жаропрочная леска"), 5, FColor(230, 90, 40), 6000, 0, 0, 10,
			Mods(4.f, 1.16f, 0.84f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f), TEXT("Сплетена с обсидиановой нитью и не плавится даже в лаве. Без неё к Жар-рыбе не подступиться."));

		// ---------------------------------------------------------------- floats (rod slot "Поплавок")
		auto AddFloat = [&D](const TCHAR* Id, const TCHAR* Name, EFVItemArt Art, uint8 Tier, int32 Sell, int32 Buy, uint8 Mask,
			int32 MinLevel, const FFVStatMods& Tk, const TCHAR* Desc)
		{
			FFVItemDef& I = AddItem(D, Id, Name, EFVItemKind::Float, Art, Sell, 5, Desc);
			I.Tier = Tier;
			I.Tackle = Tk;
			Sold(I, Buy, Mask, MinLevel);
		};
		AddFloat(TEXT("float_feather"), TEXT("Поплавок из пера"), A::FloatFeather, 1, 50, 200, Mitrich, 1,
			Mods(0.f, 1.f, 1.f, 0.94f, 0.f, 0.f, 0.f, 0.f, 0.10f), TEXT("Чуткий поплавок: отзывается на малейшее касание."));
		AddFloat(TEXT("float_reed"), TEXT("Тростниковый поплавок"), A::FloatReed, 1, 40, 0, 0, 1,
			Mods(0.f, 1.f, 1.f, 0.97f, 0.05f, 0.f, 0.f, 0.f, 0.f), TEXT("Самодельный лёгкий поплавок — летит чуть дальше."));
		// update 6 (§0.1): sell 300 -> 220 — the recipe (feather float 200 + sinker 20 + glue 60 = 280) sold at a profit
		AddFloat(TEXT("float_sliding"), TEXT("Скользящий поплавок"), A::FloatSliding, 2, 220, 1200, Mitrich, 3,
			Mods(0.f, 1.f, 1.f, 1.f, 0.08f, 0.06f, 0.f, 0.f, 0.f), TEXT("Позволяет ловить на глубине и забрасывать дальше."));
		AddFloat(TEXT("float_glow"), TEXT("Светящийся поплавок"), A::FloatGlow, 3, 900, 3500, Pier, 5,
			Mods(0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.03f, 0.f, 0.18f), TEXT("Мягко светится в темноте: поклёвку видно даже ночью."));
		AddFloat(TEXT("float_sport"), TEXT("Спортивный поплавок"), A::FloatSport, 4, 1800, 7500, Pier, 7,
			Mods(0.f, 1.f, 1.f, 0.85f, 0.f, 0.f, 0.f, 0.f, 0.15f), TEXT("Для соревнований: быстрый клёв и мгновенная реакция."));
		AddFloat(TEXT("float_gold"), TEXT("Золотой поплавок"), A::FloatGold, 5, 5000, 20000, Pier, 9,
			Mods(0.f, 1.f, 1.f, 1.f, 0.f, 0.18f, 0.05f, 0.f, 0.f), TEXT("Приманивает удачу и сокровища."));

		// ---------------------------------------------------------------- nets (placed at net spots)
		auto AddNet = [&D](const TCHAR* Id, const TCHAR* Name, uint8 Tier, FColor Tint, int32 Cap, float Dur, float MinPerFish,
			int32 Sell, int32 Buy, uint8 Mask, int32 MinLevel, const TCHAR* Desc)
		{
			FFVItemDef& I = AddItem(D, Id, Name, EFVItemKind::Net, EFVItemArt::Net, Sell, 3, Desc);
			I.Tier = Tier;
			I.Tint = Tint;
			I.NetCapacity = Cap;
			I.NetDurability = Dur;
			I.NetMinutesPerFish = MinPerFish;
			Sold(I, Buy, Mask, MinLevel);
		};
		AddNet(TEXT("net_small"), TEXT("Малая сеть"), 1, FColor(200, 180, 130), 4, 100.f, 120.f, 150, 600, Mitrich, 2,
			TEXT("Небольшая сеть: сама ловит рыбу, пока вы заняты. Вмещает 4 рыбы."));
		AddNet(TEXT("net_medium"), TEXT("Средняя сеть"), 2, FColor(150, 170, 110), 8, 160.f, 90.f, 500, 2000, Mitrich, 4,
			TEXT("Ловит чаще и вмещает 8 рыб."));
		AddNet(TEXT("net_strong"), TEXT("Прочная сеть"), 2, FColor(160, 110, 80), 8, 400.f, 90.f, 1200, 5000, Pier, 5,
			TEXT("Морская сеть из просмолённой нити. Служит очень долго."));
		AddNet(TEXT("net_large"), TEXT("Большая сеть"), 3, FColor(110, 150, 170), 14, 220.f, 70.f, 1500, 6000, Pier, 6,
			TEXT("Огромная сеть на 14 рыб. Ловит быстрее всех."));
	}

	FFVRecipeDef& AddRecipe(FData& D, const TCHAR* Id, EFVStation Station, int32 StationLevel, int32 MinLevel,
		TArray<FFVIngredient> Inputs, const TCHAR* Output, int32 OutputCount,
		EFVCraftGame Game = EFVCraftGame::None, int32 Difficulty = 1, bool bRecycle = false)
	{
		FFVRecipeDef& R = D.Recipes.AddDefaulted_GetRef();
		R.Id = FName(Id);
		R.Station = Station;
		R.StationLevel = StationLevel;
		R.MinLevel = MinLevel;
		R.Inputs = MoveTemp(Inputs);
		R.OutputId = FName(Output);
		R.OutputCount = FMath::Max(1, OutputCount);
		R.Game = Game;
		R.GameDifficulty = Difficulty;
		R.bRecycle = bRecycle;
		return R;
	}

	void BuildRecipes(FData& D)
	{
		using S = EFVStation;
		using G = EFVCraftGame;
		// ---------------------------------------------------------------- workshop
		AddRecipe(D, TEXT("rcp_board"), S::Workshop, 1, 1, { Ing(TEXT("res_branch"), 3) }, TEXT("mat_board"), 1);
		AddRecipe(D, TEXT("rcp_rope"), S::Workshop, 1, 1, { Ing(TEXT("res_fiber"), 3) }, TEXT("mat_rope"), 1);
		AddRecipe(D, TEXT("rcp_glue"), S::Workshop, 1, 1, { Ing(TEXT("res_resin"), 2) }, TEXT("mat_glue"), 1);
		AddRecipe(D, TEXT("rcp_sinker"), S::Workshop, 1, 1, { Ing(TEXT("res_pebble"), 2) }, TEXT("mat_sinker"), 2);
		AddRecipe(D, TEXT("rcp_float_feather"), S::Workshop, 1, 1, { Ing(TEXT("res_feather"), 2), Ing(TEXT("mat_glue"), 1) }, TEXT("float_feather"), 1);
		AddRecipe(D, TEXT("rcp_float_reed"), S::Workshop, 1, 1, { Ing(TEXT("res_reed"), 2), Ing(TEXT("mat_glue"), 1) }, TEXT("float_reed"), 1);
		AddRecipe(D, TEXT("rcp_line_linen"), S::Workshop, 1, 1, { Ing(TEXT("res_fiber"), 4) }, TEXT("line_linen"), 1, G::Knots, 4);
		AddRecipe(D, TEXT("rcp_rec_can"), S::Workshop, 1, 1, { Ing(TEXT("junk_can"), 2) }, TEXT("mat_scrap"), 1, G::None, 1, true);
		AddRecipe(D, TEXT("rcp_rec_boot"), S::Workshop, 1, 1, { Ing(TEXT("junk_boot"), 1) }, TEXT("mat_scrap"), 1, G::None, 1, true);
		AddRecipe(D, TEXT("rcp_rec_weed"), S::Workshop, 1, 1, { Ing(TEXT("junk_weed"), 3) }, TEXT("res_fiber"), 1, G::None, 1, true);
		AddRecipe(D, TEXT("rcp_rec_wood"), S::Workshop, 1, 1, { Ing(TEXT("junk_driftwood"), 1) }, TEXT("mat_board"), 1, G::None, 1, true);
		AddRecipe(D, TEXT("rcp_rec_bottle"), S::Workshop, 2, 1, { Ing(TEXT("junk_bottle"), 1) }, TEXT("mat_glitter"), 1, G::None, 1, true);
		AddRecipe(D, TEXT("rcp_sinker_metal"), S::Workshop, 2, 1, { Ing(TEXT("mat_scrap"), 1) }, TEXT("mat_sinker"), 3);
		AddRecipe(D, TEXT("rcp_float_sliding"), S::Workshop, 2, 3,
			{ Ing(TEXT("float_feather"), 1), Ing(TEXT("mat_sinker"), 1), Ing(TEXT("mat_glue"), 1) }, TEXT("float_sliding"), 1);
		AddRecipe(D, TEXT("rcp_line_braided"), S::Workshop, 2, 3,
			{ Ing(TEXT("mat_rope"), 1), Ing(TEXT("res_fiber"), 3), Ing(TEXT("res_resin"), 1) }, TEXT("line_braided"), 1, G::Knots, 5);
		AddRecipe(D, TEXT("rcp_net_small"), S::Workshop, 2, 2,
			{ Ing(TEXT("mat_rope"), 2), Ing(TEXT("res_fiber"), 4), Ing(TEXT("mat_sinker"), 2) }, TEXT("net_small"), 1, G::Knots, 5);
		AddRecipe(D, TEXT("rcp_line_silk"), S::Workshop, 3, 5,
			{ Ing(TEXT("mat_rope"), 2), Ing(TEXT("res_fiber"), 5), Ing(TEXT("mat_glue"), 1) }, TEXT("line_silk"), 1, G::Knots, 6);
		AddRecipe(D, TEXT("rcp_float_glow"), S::Workshop, 3, 5,
			{ Ing(TEXT("res_resin"), 2), Ing(TEXT("mat_glitter"), 1), Ing(TEXT("mat_glue"), 1) }, TEXT("float_glow"), 1);
		AddRecipe(D, TEXT("rcp_net_medium"), S::Workshop, 3, 4,
			{ Ing(TEXT("mat_rope"), 4), Ing(TEXT("res_fiber"), 6), Ing(TEXT("mat_sinker"), 4) }, TEXT("net_medium"), 1, G::Knots, 6);
		AddRecipe(D, TEXT("rcp_net_large"), S::Workshop, 3, 6,
			{ Ing(TEXT("mat_rope"), 6), Ing(TEXT("res_fiber"), 10), Ing(TEXT("mat_sinker"), 6), Ing(TEXT("mat_glue"), 2) }, TEXT("net_large"), 1, G::Knots, 7);
		// update 5: the heat-proof line for the Zhar-ryba (the titanium string must lie in the backpack, not on a rod)
		AddRecipe(D, TEXT("rcp_line_heat"), S::Workshop, 3, 10,
			{ Ing(TEXT("line_titan"), 1), Ing(TEXT("fish_obsidian_crab"), 2), Ing(TEXT("res_clay"), 4), Ing(TEXT("mat_glue"), 2) }, TEXT("line_heat"), 1, G::Knots, 7);

		// ---------------------------------------------------------------- kitchen
		AddRecipe(D, TEXT("rcp_fried"), S::Kitchen, 1, 1, { Ing(TEXT("any_fish"), 1) }, TEXT("food_fried"), 1, G::Cooking, 2);
		// update 5 (§10.2): the rarer the fish, the more cuts and the faster the pan arrow (CookSpeed)
		AddRecipe(D, TEXT("rcp_fried_rare"), S::Kitchen, 1, 1, { Ing(TEXT("any_fish_rare"), 1) }, TEXT("food_fried_rare"), 1, G::Cooking, 2).CookSpeed = 1.1f;
		AddRecipe(D, TEXT("rcp_fried_epic"), S::Kitchen, 2, 1, { Ing(TEXT("any_fish_epic"), 1) }, TEXT("food_fried_epic"), 1, G::Cooking, 3).CookSpeed = 1.2f;
		AddRecipe(D, TEXT("rcp_fried_legend"), S::Kitchen, 3, 1, { Ing(TEXT("any_fish_legend"), 1) }, TEXT("food_fried_legend"), 1, G::Cooking, 4).CookSpeed = 1.4f;
		AddRecipe(D, TEXT("rcp_tea"), S::Kitchen, 1, 1, { Ing(TEXT("res_berries"), 1), Ing(TEXT("res_fiber"), 2) }, TEXT("food_tea"), 1, G::Cooking, 0);
		AddRecipe(D, TEXT("rcp_dough"), S::Kitchen, 1, 1, { Ing(TEXT("res_reed"), 2) }, TEXT("bait_dough"), 5);
		// update 6 (Р41, Р42): the carrot is required; it grows only in the garden (level 7), Митрич does not sell it
		AddRecipe(D, TEXT("rcp_soup"), S::Kitchen, 2, 1,
			{ Ing(TEXT("any_fish"), 2), Ing(TEXT("res_mushroom"), 1), Ing(TEXT("res_reed"), 1), Ing(TEXT("veg_carrot"), 1) },
			TEXT("food_soup"), 1, G::Cooking, 3);
		AddRecipe(D, TEXT("rcp_groundbait"), S::Kitchen, 2, 1,
			{ Ing(TEXT("res_clay"), 1), Ing(TEXT("res_berries"), 1), Ing(TEXT("bait_dough"), 2) }, TEXT("bait_groundbait"), 5);
		AddRecipe(D, TEXT("rcp_pie"), S::Kitchen, 3, 1, { Ing(TEXT("res_berries"), 4), Ing(TEXT("res_reed"), 2) }, TEXT("food_pie"), 1, G::Cooking, 0);
		// update 5: the Жар-наживка for the Zhar-ryba, baked from fiery volcano fish (Печь)
		AddRecipe(D, TEXT("rcp_bait_ember"), S::Kitchen, 3, 10,
			{ Ing(TEXT("fish_sparkfin"), 2), Ing(TEXT("fish_fire_perch"), 1), Ing(TEXT("res_berries"), 1) }, TEXT("bait_ember"), 4, G::Cooking, 3);
	}

	FFVStationDef& AddStation(FData& D, EFVStation S, const TCHAR* Id, const TCHAR* Name, bool bStartsBuilt, const TCHAR* Desc)
	{
		FFVStationDef& St = D.Stations.AddDefaulted_GetRef();
		St.Station = S;
		St.Id = FName(Id);
		St.Name = Name;
		St.Description = Desc;
		St.bStartsBuilt = bStartsBuilt;
		return St;
	}

	void AddLevel(FFVStationDef& St, const TCHAR* Name, int32 Price, TArray<FFVIngredient> Materials, int32 MinLevel, int32 Capacity, const TCHAR* Desc)
	{
		FFVStationLevel& L = St.Levels.AddDefaulted_GetRef();
		L.Name = Name;
		L.Description = Desc;
		L.Price = Price;
		L.Materials = MoveTemp(Materials);
		L.MinLevel = MinLevel;
		L.Capacity = Capacity;
	}

	void BuildStations(FData& D)
	{
		using S = EFVStation;
		// Order == EFVStation.
		{
			FFVStationDef& St = AddStation(D, S::Kitchen, TEXT("st_kitchen"), TEXT("Кухня"), false,
				TEXT("Здесь готовят еду, которая помогает на рыбалке, а ещё тесто и прикормку."));
			AddLevel(St, TEXT("Очаг"), 250, { Ing(TEXT("res_pebble"), 10), Ing(TEXT("res_clay"), 5) }, 1, 0,
				TEXT("Жареная рыба, рыба по-монастырски, травяной чай и тесто для наживки."));
			AddLevel(St, TEXT("Плита"), 1500, { Ing(TEXT("mat_scrap"), 6), Ing(TEXT("res_clay"), 6), Ing(TEXT("mat_board"), 2) }, 3, 0,
				TEXT("Уха, прикормка и рыба по-царски."));
			AddLevel(St, TEXT("Печь"), 5000, { Ing(TEXT("res_pebble"), 20), Ing(TEXT("res_clay"), 12), Ing(TEXT("mat_scrap"), 10), Ing(TEXT("mat_board"), 4) }, 6, 0,
				TEXT("Ягодный пирог, «Пир на весь мир» и Жар-наживка."));
		}
		{
			FFVStationDef& St = AddStation(D, S::Workshop, TEXT("st_workshop"), TEXT("Мастерская"), false,
				TEXT("Доски, верёвки, клей, снасти и сети. Переработка мусора, оснастка удочек и лопаты."));
			AddLevel(St, TEXT("Стол"), 150, { Ing(TEXT("res_branch"), 8), Ing(TEXT("res_pebble"), 4) }, 1, 0,
				TEXT("Материалы, простые поплавки, льняная леска, переработка мусора и оснастка удочек."));
			AddLevel(St, TEXT("Верстак"), 1200, { Ing(TEXT("mat_board"), 6), Ing(TEXT("mat_rope"), 3), Ing(TEXT("mat_scrap"), 3) }, 2, 0,
				TEXT("Мастерская блёсен, плетёная леска, малая сеть, скользящий поплавок, железная лопата."));
			AddLevel(St, TEXT("Станок"), 4500, { Ing(TEXT("mat_board"), 12), Ing(TEXT("mat_scrap"), 12), Ing(TEXT("mat_glue"), 4) }, 5, 0,
				TEXT("Шёлковая леска, светящийся поплавок, средняя и большая сети, стальная лопата, жаропрочная леска."));
		}
		{
			FFVStationDef& St = AddStation(D, S::Smokehouse, TEXT("st_smokehouse"), TEXT("Коптильня"), false,
				TEXT("Копчёная рыба стоит вдвое дороже. Рыба коптится 6 часов, пока в топке есть дрова."));
			AddLevel(St, TEXT("Коптильня"), 800, { Ing(TEXT("res_pebble"), 12), Ing(TEXT("res_clay"), 8), Ing(TEXT("mat_board"), 3) }, 2, 4,
				TEXT("4 места для рыбы. Ветка — 1 час топлива."));
			AddLevel(St, TEXT("Большая коптильня"), 3000, { Ing(TEXT("mat_scrap"), 8), Ing(TEXT("mat_board"), 6), Ing(TEXT("res_clay"), 10) }, 5, 8,
				TEXT("8 мест для рыбы. Ветка — полтора часа топлива."));
		}
		{
			FFVStationDef& St = AddStation(D, S::WormFarm, TEXT("st_wormfarm"), TEXT("Червятник"), false,
				TEXT("Каждое утро в червятнике заводятся свежие черви."));
			AddLevel(St, TEXT("Ящик с землёй"), 300, { Ing(TEXT("mat_board"), 3), Ing(TEXT("res_clay"), 3) }, 1, 12,
				TEXT("2–3 червя каждое утро, вмещает 12."));
			AddLevel(St, TEXT("Большой червятник"), 1500, { Ing(TEXT("mat_board"), 6), Ing(TEXT("res_clay"), 6), Ing(TEXT("res_mushroom"), 3) }, 4, 20,
				TEXT("3–4 червя и иногда выползок каждое утро, вмещает 20."));
		}
		{
			FFVStationDef& St = AddStation(D, S::Aquarium, TEXT("st_aquarium"), TEXT("Аквариум"), false,
				TEXT("Живые рыбы плавают прямо у вас дома."));
			AddLevel(St, TEXT("Банка"), 200, { Ing(TEXT("junk_bottle"), 2), Ing(TEXT("mat_glue"), 1) }, 1, 1,
				TEXT("Одна рыбка в стеклянной банке."));
			AddLevel(St, TEXT("Аквариум"), 2000, { Ing(TEXT("junk_bottle"), 4), Ing(TEXT("mat_board"), 4), Ing(TEXT("mat_glue"), 3) }, 3, 4,
				TEXT("До 4 рыб."));
			AddLevel(St, TEXT("Большой аквариум"), 7000,
				{ Ing(TEXT("junk_bottle"), 8), Ing(TEXT("mat_board"), 8), Ing(TEXT("mat_glue"), 6), Ing(TEXT("mat_scrap"), 6) }, 6, 10,
				TEXT("До 10 рыб."));
		}
		{
			FFVStationDef& St = AddStation(D, S::IceCellar, TEXT("st_icecellar"), TEXT("Ледник"), false,
				TEXT("Холодный погреб: рыба хранится здесь и не занимает садок."));
			AddLevel(St, TEXT("Погребок"), 600, { Ing(TEXT("res_pebble"), 15), Ing(TEXT("res_clay"), 6) }, 2, 10,
				TEXT("Хранит 10 рыб."));
			AddLevel(St, TEXT("Ледник"), 2500, { Ing(TEXT("res_pebble"), 25), Ing(TEXT("res_clay"), 10), Ing(TEXT("mat_board"), 6) }, 4, 25,
				TEXT("Хранит 25 рыб."));
		}
		{
			FFVStationDef& St = AddStation(D, S::Radio, TEXT("st_radio"), TEXT("Радио"), false,
				TEXT("Прогноз погоды на завтра и советы рыбакам."));
			AddLevel(St, TEXT("Радиоприёмник"), 1200, { Ing(TEXT("mat_scrap"), 6), Ing(TEXT("mat_rope"), 2), Ing(TEXT("mat_board"), 2) }, 3, 0,
				TEXT("Погода на завтра и подсказки: какая рыба ждёт дождя или ночи."));
		}
		{
			FFVStationDef& St = AddStation(D, S::Bed, TEXT("st_bed"), TEXT("Кровать"), true,
				TEXT("Сон до утра. Пока вы спите, сети ловят рыбу, а коптильня работает."));
			AddLevel(St, TEXT("Кровать"), 0, {}, 1, 0, TEXT("Можно спать до утра."));
		}
		{
			FFVStationDef& St = AddStation(D, S::TrophyWall, TEXT("st_trophy"), TEXT("Стена трофеев"), false,
				TEXT("Чучела рекордных рыб — гордость любого рыбака."));
			AddLevel(St, TEXT("Стена трофеев"), 500, { Ing(TEXT("mat_board"), 6), Ing(TEXT("mat_glue"), 2) }, 2, 6,
				TEXT("6 табличек, по одной на вид рыбы."));
		}
		{
			FFVStationDef& St = AddStation(D, S::Chest, TEXT("st_chest"), TEXT("Сундук"), true,
				TEXT("Сундук для вещей, которые не помещаются в рюкзак."));
			AddLevel(St, TEXT("Сундук"), 0, {}, 1, 40, TEXT("40 ячеек для вещей."));
		}
	}

	void BuildTraders(FData& D)
	{
		{
			FFVTraderDef& T = D.Traders.AddDefaulted_GetRef();
			T.Id = FName(TEXT("tr_mitrich"));
			T.Name = TEXT("Дед Митрич");
			T.Role = TEXT("хозяин лавки");
			T.ShopTitle = TEXT("Лавка рыбака");
			T.Location = FName(TEXT("pond"));
			T.FishSellMult = 1.f;
			T.ItemSellMult = 1.f;
			T.Mask = Mitrich;
			T.bSellsClothes = true;
			T.bSellsUpgrades = true;
		}
		{
			FFVTraderDef& T = D.Traders.AddDefaulted_GetRef();
			T.Id = FName(TEXT("tr_pier"));
			T.Name = TEXT("Боцман Семёныч");
			T.Role = TEXT("торговец на причале");
			T.ShopTitle = TEXT("Лавка на причале");
			T.Location = FName(TEXT("ocean"));
			T.FishSellMult = 1.25f;
			T.ItemSellMult = 1.15f;
			T.Mask = Pier;
			T.bSellsClothes = false;
			T.bSellsUpgrades = false;
		}
	}

	void AddDecor(FData& D, const TCHAR* Id, const TCHAR* Name, EFVDecorSlot Slot, uint8 Style, int32 Price, int32 MinLevel,
		bool bStarter, FColor Primary, FColor Secondary, const TCHAR* Desc)
	{
		FFVDecorDef& Dc = D.Decor.AddDefaulted_GetRef();
		Dc.Id = FName(Id);
		Dc.Name = Name;
		Dc.Description = Desc;
		Dc.Slot = Slot;
		Dc.Style = Style;
		Dc.Price = Price;
		Dc.MinLevel = MinLevel;
		Dc.bStarter = bStarter;
		Dc.Primary = Primary;
		Dc.Secondary = Secondary;
		if (bStarter) { D.StarterDecor.Add(Dc.Id); }
	}

	void BuildDecor(FData& D)
	{
		using S = EFVDecorSlot;
		// ---------------------------------------------------------------- wallpaper
		AddDecor(D, TEXT("decor_wall_logs"), TEXT("Бревенчатые стены"), S::Wallpaper, 0, 0, 1, true, FColor(170, 120, 70), FColor(130, 85, 50),
			TEXT("Тёплые сосновые брёвна. Пахнет смолой."));
		AddDecor(D, TEXT("decor_wall_flowers"), TEXT("Обои в цветочек"), S::Wallpaper, 1, 400, 1, false, FColor(236, 214, 170), FColor(210, 110, 120),
			TEXT("Уютные обои с мелкими цветочками."));
		AddDecor(D, TEXT("decor_wall_blue"), TEXT("Голубые обои"), S::Wallpaper, 2, 600, 1, false, FColor(150, 190, 220), FColor(110, 150, 190),
			TEXT("Спокойный небесный цвет."));
		AddDecor(D, TEXT("decor_wall_sea"), TEXT("Морские обои"), S::Wallpaper, 3, 1500, 4, false, FColor(60, 120, 170), FColor(240, 240, 230),
			TEXT("Полоски, как на тельняшке. Для настоящих морских волков."));
		AddDecor(D, TEXT("decor_wall_fish"), TEXT("Обои с рыбками"), S::Wallpaper, 4, 0, 1, false, FColor(220, 230, 200), FColor(240, 140, 60),
			TEXT("По стенам плывут весёлые рыбки."));
		// ---------------------------------------------------------------- floor
		AddDecor(D, TEXT("decor_floor_planks"), TEXT("Дощатый пол"), S::Floor, 0, 0, 1, true, FColor(150, 100, 60), FColor(120, 78, 46),
			TEXT("Крепкие половицы. Слегка скрипят."));
		AddDecor(D, TEXT("decor_floor_parquet"), TEXT("Паркет"), S::Floor, 1, 800, 1, false, FColor(170, 110, 60), FColor(120, 70, 40),
			TEXT("Натёртый до блеска паркет ёлочкой."));
		AddDecor(D, TEXT("decor_floor_tiles"), TEXT("Плитка"), S::Floor, 2, 1200, 1, false, FColor(220, 220, 210), FColor(150, 170, 190),
			TEXT("Чистая светлая плитка в шахматном порядке."));
		AddDecor(D, TEXT("decor_floor_stone"), TEXT("Каменный пол"), S::Floor, 3, 2000, 5, false, FColor(150, 150, 150), FColor(110, 110, 120),
			TEXT("Прохладный каменный пол, как в старинном замке."));
		// ---------------------------------------------------------------- rug
		AddDecor(D, TEXT("decor_rug_woven"), TEXT("Плетёный коврик"), S::Rug, 0, 300, 1, false, FColor(200, 170, 110), FColor(150, 110, 70),
			TEXT("Плетёный из тростника коврик у порога."));
		AddDecor(D, TEXT("decor_rug_red"), TEXT("Красный ковёр"), S::Rug, 1, 900, 1, false, FColor(180, 50, 50), FColor(230, 190, 90),
			TEXT("Пушистый ковёр с золотым узором."));
		AddDecor(D, TEXT("decor_rug_fish"), TEXT("Ковёр-рыба"), S::Rug, 2, 0, 1, false, FColor(70, 130, 200), FColor(240, 200, 90),
			TEXT("Ковёр в форме большой рыбы. Мечта сетевика."));
		// ---------------------------------------------------------------- lamp
		AddDecor(D, TEXT("decor_lamp_candle"), TEXT("Свеча"), S::Lamp, 0, 0, 1, true, FColor(240, 230, 200), FColor(255, 190, 90),
			TEXT("Простая свеча в подсвечнике."));
		AddDecor(D, TEXT("decor_lamp_kerosene"), TEXT("Керосиновая лампа"), S::Lamp, 1, 350, 1, false, FColor(180, 140, 60), FColor(255, 200, 110),
			TEXT("Старая добрая лампа: светит ярко и ровно."));
		AddDecor(D, TEXT("decor_lamp_float"), TEXT("Фонарь-поплавок"), S::Lamp, 2, 1000, 1, false, FColor(220, 60, 50), FColor(250, 250, 240),
			TEXT("Фонарь в виде огромного поплавка."));
		AddDecor(D, TEXT("decor_lamp_jelly"), TEXT("Светильник-медуза"), S::Lamp, 3, 0, 1, false, FColor(200, 120, 230), FColor(140, 220, 255),
			TEXT("Мягко мерцает, как настоящая медуза."));
		// ---------------------------------------------------------------- plant
		AddDecor(D, TEXT("decor_plant_ficus"), TEXT("Фикус"), S::Plant, 0, 250, 1, false, FColor(60, 140, 60), FColor(170, 100, 60),
			TEXT("Неприхотливый фикус в глиняном горшке."));
		AddDecor(D, TEXT("decor_plant_cactus"), TEXT("Кактус"), S::Plant, 1, 300, 1, false, FColor(90, 160, 80), FColor(200, 120, 80),
			TEXT("Колючий, но милый. Поливать раз в месяц."));
		AddDecor(D, TEXT("decor_plant_fern"), TEXT("Папоротник"), S::Plant, 2, 700, 1, false, FColor(70, 150, 70), FColor(120, 90, 70),
			TEXT("Пышный папоротник из лесной чащи."));
		AddDecor(D, TEXT("decor_plant_bonsai"), TEXT("Бонсай"), S::Plant, 3, 0, 1, false, FColor(80, 130, 60), FColor(90, 70, 120),
			TEXT("Крошечное вековое деревце. Награда мастеру на все руки."));
		// ---------------------------------------------------------------- painting
		AddDecor(D, TEXT("decor_paint_pond"), TEXT("Картина «Пруд»"), S::Painting, 0, 500, 1, false, FColor(90, 170, 200), FColor(150, 110, 60),
			TEXT("Деревенский пруд на рассвете."));
		AddDecor(D, TEXT("decor_paint_map"), TEXT("Карта долины"), S::Painting, 1, 900, 1, false, FColor(230, 210, 160), FColor(140, 90, 50),
			TEXT("Старая карта с отмеченными рыбными местами."));
		AddDecor(D, TEXT("decor_paint_mitrich"), TEXT("Портрет Митрича"), S::Painting, 2, 0, 1, false, FColor(200, 160, 120), FColor(120, 80, 50),
			TEXT("Дед Митрич с самым большим карасём в жизни."));
		AddDecor(D, TEXT("decor_paint_goldfish"), TEXT("Золотая рыбка"), S::Painting, 3, 0, 1, false, FColor(250, 170, 40), FColor(60, 120, 170),
			TEXT("Та самая, что исполняет желания."));
		// ---------------------------------------------------------------- curtains
		AddDecor(D, TEXT("decor_curt_linen"), TEXT("Льняные шторы"), S::Curtains, 0, 300, 1, false, FColor(220, 205, 170), FColor(180, 160, 120),
			TEXT("Лёгкие льняные занавески."));
		AddDecor(D, TEXT("decor_curt_check"), TEXT("Клетчатые шторы"), S::Curtains, 1, 500, 1, false, FColor(200, 60, 60), FColor(240, 240, 230),
			TEXT("Весёлая красная клетка."));
		AddDecor(D, TEXT("decor_curt_velvet"), TEXT("Бархатные шторы"), S::Curtains, 2, 1500, 4, false, FColor(120, 30, 60), FColor(230, 190, 90),
			TEXT("Тяжёлый бархат с золотыми кистями."));
	}

	void BuildShovels(FData& D)
	{
		auto Add = [&D](const TCHAR* Name, int32 Price, uint8 Mask, int32 MinLevel, float Seconds, int32 WorkshopLevel, TArray<FFVIngredient> Craft)
		{
			FFVShovelDef& S = D.Shovels.AddDefaulted_GetRef();
			S.Name = Name;
			S.Price = Price;
			S.TraderMask = Mask;
			S.MinLevel = MinLevel;
			S.DigSeconds = Seconds;
			S.WorkshopLevel = WorkshopLevel;
			S.Craft = MoveTemp(Craft);
		};
		// update 5 (§10.1): every dig time -20 % (was 8 / 11 / 14 / 18 s), rounded to 0.1 s
		Add(TEXT("Лопата"), 150, Mitrich, 1, 6.4f, 0, {});
		Add(TEXT("Железная лопата"), 1200, Mitrich, 2, 8.8f, 2, { Ing(TEXT("mat_scrap"), 4), Ing(TEXT("mat_board"), 1) });
		Add(TEXT("Стальная лопата"), 4000, Pier, 4, 11.2f, 3, { Ing(TEXT("mat_scrap"), 8), Ing(TEXT("mat_glue"), 2) });
		Add(TEXT("Иридиевая лопата"), 15000, Pier, 7, 14.4f, 0, {});
	}

	void BuildBackpack(FData& D)
	{
		const int32 Slots[] = { 16, 24, 36 };
		const int32 Prices[] = { 0, 800, 3000 };
		for (int32 i = 0; i < int32(UE_ARRAY_COUNT(Slots)); ++i)
		{
			FFVBackpackLevel& B = D.Backpack.AddDefaulted_GetRef();
			B.Slots = Slots[i];
			B.Price = Prices[i];
		}
	}

	/** Every id referenced by the new tables must exist (logs errors; never fatal). */
	void Validate(const FData& D)
	{
		auto Known = [&D](FName Id)
		{
			return FVDB::IsAnyFishToken(Id) || D.ItemIdx.Contains(Id) || FVDB::FindBait(Id) || FVDB::FindJunk(Id) || FVDB::FindFish(Id)
				|| FVDB::FindRod(Id);   // 0.6.2: a rod is a recipe output (ковка) or input (перековка)
		};
		auto CheckList = [&Known](const TArray<FFVIngredient>& List, const FString& Where)
		{
			for (const FFVIngredient& I : List)
			{
				if (!Known(I.ItemId)) { UE_LOG(LogFV, Error, TEXT("FV: %s uses unknown ingredient '%s'"), *Where, *I.ItemId.ToString()); }
				if (I.Count <= 0) { UE_LOG(LogFV, Error, TEXT("FV: %s has a non-positive ingredient count"), *Where); }
			}
		};

		for (const FFVRecipeDef& R : D.Recipes)
		{
			CheckList(R.Inputs, FString::Printf(TEXT("recipe '%s'"), *R.Id.ToString()));
			if (FVDB::IsAnyFishToken(R.OutputId) || !Known(R.OutputId) || FVDB::FindFish(R.OutputId))
			{
				UE_LOG(LogFV, Error, TEXT("FV: recipe '%s' has an invalid output '%s'"), *R.Id.ToString(), *R.OutputId.ToString());
			}
			if (FVDB::FindRod(R.OutputId) && R.OutputCount != 1)
			{
				UE_LOG(LogFV, Error, TEXT("FV: rod recipe '%s' must make exactly one rod"), *R.Id.ToString());
			}
			if (!R.RequiredAchievement.IsNone() && !FVDB::FindAchievement(R.RequiredAchievement))
			{
				UE_LOG(LogFV, Error, TEXT("FV: recipe '%s' is locked by an unknown achievement '%s'"), *R.Id.ToString(), *R.RequiredAchievement.ToString());
			}
			const int32 SI = int32(R.Station);
			if (R.Yard != EFVYardBuilding::Count)
			{
				// update 6: a yard recipe needs a real building level (FVDBU6::YardBuildings is a separate static table)
				const FFVYardBuildingDef& Y = FVDBU6::YardBuilding(R.Yard);
				if (R.Yard >= EFVYardBuilding::Count || R.StationLevel < 1 || (Y.Levels.Num() > 0 && R.StationLevel > Y.Levels.Num()))
				{
					UE_LOG(LogFV, Error, TEXT("FV: yard recipe '%s' needs an impossible building level"), *R.Id.ToString());
				}
			}
			else if (!D.Stations.IsValidIndex(SI) || R.StationLevel < 1 || R.StationLevel > D.Stations[SI].Levels.Num())
			{
				UE_LOG(LogFV, Error, TEXT("FV: recipe '%s' needs an impossible station level"), *R.Id.ToString());
			}
		}
		if (D.Stations.Num() != int32(EFVStation::Count))
		{
			UE_LOG(LogFV, Error, TEXT("FV: %d stations defined, expected %d"), D.Stations.Num(), int32(EFVStation::Count));
		}
		for (int32 i = 0; i < D.Stations.Num(); ++i)
		{
			const FFVStationDef& S = D.Stations[i];
			if (int32(S.Station) != i) { UE_LOG(LogFV, Error, TEXT("FV: station '%s' is out of EFVStation order"), *S.Id.ToString()); }
			if (S.Levels.Num() == 0) { UE_LOG(LogFV, Error, TEXT("FV: station '%s' has no levels"), *S.Id.ToString()); }
			for (int32 L = 0; L < S.Levels.Num(); ++L)
			{
				CheckList(S.Levels[L].Materials, FString::Printf(TEXT("station '%s' level %d"), *S.Id.ToString(), L + 1));
			}
		}
		for (int32 i = 0; i < D.Shovels.Num(); ++i)
		{
			CheckList(D.Shovels[i].Craft, FString::Printf(TEXT("shovel %d"), i + 1));
		}
		for (uint8 B = 0; B < FVDB::LureBases; ++B)
		{
			for (uint8 C = 0; C < FVDB::LureColors; ++C)
			{
				for (uint8 G = 0; G < FVDB::LureGlitters; ++G)
				{
					const FName Id = FVDB::LureId(B, C, G);
					const FFVBaitDef* Bait = FVDB::FindBait(Id);
					if (!Bait || !Bait->bLure) { UE_LOG(LogFV, Error, TEXT("FV: lure bait '%s' is missing"), *Id.ToString()); }
					CheckList(FVDB::LureInputs(B, C, G), FString::Printf(TEXT("lure '%s'"), *Id.ToString()));
				}
			}
		}
		for (const FFVTraderDef& T : D.Traders)
		{
			if (!FVDB::FindLocation(T.Location)) { UE_LOG(LogFV, Error, TEXT("FV: trader '%s' has unknown location '%s'"), *T.Id.ToString(), *T.Location.ToString()); }
		}
		for (const FFVAchievementDef& A : FVDB::Achievements())
		{
			if (!A.RewardDecor.IsNone() && !D.DecorIdx.Contains(A.RewardDecor))
			{
				UE_LOG(LogFV, Error, TEXT("FV: achievement '%s' rewards unknown decor '%s'"), *A.Id.ToString(), *A.RewardDecor.ToString());
			}
		}
		bool bWall = false, bFloor = false;
		for (const FName& Id : D.StarterDecor)
		{
			const FFVDecorDef* Dc = FindIn(D.Decor, D.DecorIdx, Id);
			if (Dc && Dc->Slot == EFVDecorSlot::Wallpaper) { bWall = true; }
			if (Dc && Dc->Slot == EFVDecorSlot::Floor) { bFloor = true; }
		}
		if (!bWall || !bFloor) { UE_LOG(LogFV, Error, TEXT("FV: starter decor must contain a wallpaper and a floor")); }
		// ids must stay unique across all tables (ItemName/ItemPrice resolve by trying tables in order)
		auto Clash = [](FName Id)
		{
			return FVDB::FindFish(Id) || FVDB::FindJunk(Id) || FVDB::FindRod(Id) || FVDB::FindBait(Id) || FVDB::FindCosmetic(Id)
				|| FVDB::FindAchievement(Id) || FVDB::LocationIndex(Id) != INDEX_NONE;
		};
		for (const FFVItemDef& I : D.Items)
		{
			if (Clash(I.Id)) { UE_LOG(LogFV, Error, TEXT("FV: item id '%s' clashes with another table"), *I.Id.ToString()); }
			if (!I.bExpert && (I.Kind == EFVItemKind::Line || I.Kind == EFVItemKind::Float) && (I.Tier < 1 || I.Tier > 5))
			{
				UE_LOG(LogFV, Error, TEXT("FV: tackle '%s' has tier %d"), *I.Id.ToString(), int32(I.Tier));
			}
			if (I.Kind == EFVItemKind::Net && (I.NetCapacity <= 0 || I.NetDurability <= 0.f || I.NetMinutesPerFish <= 0.f))
			{
				UE_LOG(LogFV, Error, TEXT("FV: net '%s' has invalid stats"), *I.Id.ToString());
			}
		}
		for (const FFVDecorDef& Dc : D.Decor)
		{
			if (Clash(Dc.Id) || D.ItemIdx.Contains(Dc.Id)) { UE_LOG(LogFV, Error, TEXT("FV: decor id '%s' clashes with another table"), *Dc.Id.ToString()); }
		}
		// update 5: the tackle a legend asks for must be real lines / floats (the bait rule is checked by the base build)
		auto IsKind = [&D](FName Id, EFVItemKind Kind)
		{
			const FFVItemDef* It = FindIn(D.Items, D.ItemIdx, Id);
			return It && It->Kind == Kind;
		};
		for (const FFVFishDef& F : FVDB::Fish())
		{
			if (F.Legend == EFVLegend::None) { continue; }
			if (!F.Prep.Float.IsNone() && !IsKind(F.Prep.Float, EFVItemKind::Float))
			{
				UE_LOG(LogFV, Error, TEXT("FV: legend '%s' requires unknown float '%s'"), *F.Id.ToString(), *F.Prep.Float.ToString());
			}
			for (const FName& L : F.Prep.Lines)
			{
				if (!IsKind(L, EFVItemKind::Line)) { UE_LOG(LogFV, Error, TEXT("FV: legend '%s' allows unknown line '%s'"), *F.Id.ToString(), *L.ToString()); }
			}
		}
	}

	void Build(FData& D)
	{
		FVDB::Init();   // base tables first (fish, junk, baits...); they never call back into this file's tables

		BuildItems(D);
		BuildRecipes(D);
		BuildStations(D);
		BuildTraders(D);
		BuildDecor(D);
		BuildShovels(D);
		BuildBackpack(D);

		// update 6: new rows are APPENDED here (FVDatabaseU6.h). The classic FVDB tables are ready; the home getters
		// (FVDB::Items(), FindItem...) are NOT (this table is being built) — appenders must not call them.
		{
			FVDBU6::FHomeTables T;
			T.Items = &D.Items; T.Recipes = &D.Recipes; T.Stations = &D.Stations; T.Traders = &D.Traders; T.Decor = &D.Decor;
			FVDBU6::AppendHomeTables(T);
		}

		BuildIndex(D.Items, D.ItemIdx, TEXT("item"));
		BuildIndex(D.Recipes, D.RecipeIdx, TEXT("recipe"));
		BuildIndex(D.Stations, D.StationIdx, TEXT("station"));
		BuildIndex(D.Traders, D.TraderIdx, TEXT("trader"));
		BuildIndex(D.Decor, D.DecorIdx, TEXT("decor"));

		Validate(D);

		UE_LOG(LogFV, Log, TEXT("FV: home database ready — %d items, %d recipes, %d stations, %d traders, %d decor, %d shovels"),
			D.Items.Num(), D.Recipes.Num(), D.Stations.Num(), D.Traders.Num(), D.Decor.Num(), D.Shovels.Num());
	}

	const FData& Data()
	{
		static FData Instance;
		if (!Instance.bBuilt && !Instance.bBuilding)
		{
			Instance.bBuilding = true;
			Build(Instance);
			Instance.bBuilding = false;
			Instance.bBuilt = true;
		}
		return Instance;
	}

	FFVLocationDef MakeHomeDef(bool bInterior)
	{
		FFVLocationDef L;
		L.Id = bInterior ? FName(TEXT("house")) : FName(TEXT("home"));
		L.Name = bInterior ? TEXT("В доме") : TEXT("Дом");
		L.Description = bInterior
			? TEXT("Уютная комната: кухня, мастерская, коптильня и всё рыбацкое хозяйство.")
			: TEXT("Ваш дом и двор. Во дворе растут ягоды и грибы, в компостной куче водятся черви, а по сторонам есть место "
				"под постройки.");   // update 6 (§6): no garden until it is built
		L.Theme = bInterior ? EFVTheme::HomeInterior : EFVTheme::Home;
		L.UnlockPrice = 0;
		L.MinLevel = 1;
		L.MapPos = FVector2D(404, 200);
		L.JunkChance = 0.f;
		L.TreasureChance = 0.f;
		L.WaterShallow = FColor(96, 176, 190);
		L.WaterDeep = FColor(36, 104, 132);
		L.bBigWorld = true;   // update 6 (§2, §6.4): the yard and the house are big locations with a camera
		return L;
	}

	int32 Pct(float V) { return FMath::RoundToInt(V * 100.f); }

	FString Signed(int32 V) { return V >= 0 ? FString::Printf(TEXT("+%d"), V) : FString::Printf(TEXT("-%d"), -V); }

	/** 0.15 -> "0,15", 0.1 -> "0,1". */
	FString Decimal(float V)
	{
		FString S = FString::Printf(TEXT("%.2f"), FMath::Abs(V));
		while (S.Contains(TEXT(".")) && (S.EndsWith(TEXT("0")) || S.EndsWith(TEXT("."))))
		{
			const bool bDot = S.EndsWith(TEXT("."));
			S.LeftChopInline(1);
			if (bDot) { break; }
		}
		return S.Replace(TEXT("."), TEXT(","));
	}
}

// ================================================================================================
// FVDB (update 2)
// ================================================================================================
namespace FVDB
{
	// ---------------------------------------------------------------- special ids
	FName HomeId() { static const FName Id(TEXT("home")); return Id; }
	FName HouseId() { static const FName Id(TEXT("house")); return Id; }
	bool IsHomeId(FName LocationId) { return !LocationId.IsNone() && (LocationId == HomeId() || LocationId == HouseId()); }
	const FFVLocationDef& HomeLocation() { static const FFVLocationDef L = FVDBHome::MakeHomeDef(false); return L; }
	const FFVLocationDef& HouseLocation() { static const FFVLocationDef L = FVDBHome::MakeHomeDef(true); return L; }
	FName AnyFishToken() { static const FName Id(TEXT("any_fish")); return Id; }
	// update 5 (§10.2): «a fish of exactly this rarity» recipe tokens
	FName AnyFishRareToken() { static const FName Id(TEXT("any_fish_rare")); return Id; }
	FName AnyFishEpicToken() { static const FName Id(TEXT("any_fish_epic")); return Id; }
	FName AnyFishLegendToken() { static const FName Id(TEXT("any_fish_legend")); return Id; }

	bool FishTokenRarity(FName Id, EFVRarity& OutRarity)
	{
		if (Id.IsNone()) { return false; }
		if (Id == AnyFishRareToken()) { OutRarity = EFVRarity::Rare; return true; }
		if (Id == AnyFishEpicToken()) { OutRarity = EFVRarity::Epic; return true; }
		if (Id == AnyFishLegendToken()) { OutRarity = EFVRarity::Legendary; return true; }
		return false;
	}

	bool IsAnyFishToken(FName Id)
	{
		EFVRarity Unused = EFVRarity::Common;
		return !Id.IsNone() && (Id == AnyFishToken() || FishTokenRarity(Id, Unused));
	}

	// ---------------------------------------------------------------- items
	const TArray<FFVItemDef>& Items() { return FVDBHome::Data().Items; }

	const FFVItemDef* FindItem(FName Id)
	{
		const FVDBHome::FData& D = FVDBHome::Data();
		return FVDBHome::FindIn(D.Items, D.ItemIdx, Id);
	}

	TArray<const FFVItemDef*> ItemsOfKind(EFVItemKind Kind)
	{
		TArray<const FFVItemDef*> Out;
		for (const FFVItemDef& I : FVDBHome::Data().Items)
		{
			if (I.Kind == Kind) { Out.Add(&I); }
		}
		return Out;
	}

	FString ItemKindName(EFVItemKind Kind)
	{
		switch (Kind)
		{
		case EFVItemKind::Resource: return TEXT("Ресурс");
		case EFVItemKind::Material: return TEXT("Материал");
		case EFVItemKind::Food:     return TEXT("Еда");
		case EFVItemKind::Line:     return TEXT("Леска");
		case EFVItemKind::Float:    return TEXT("Поплавок");
		case EFVItemKind::Net:      return TEXT("Сеть");
		case EFVItemKind::Seed:     return TEXT("Семена");      // update 6
		case EFVItemKind::Reel:     return TEXT("Катушка");
		case EFVItemKind::Blueprint: return TEXT("Чертёж");
		default:                    return TEXT("—");
		}
	}

	// ---------------------------------------------------------------- classification
	bool IsFishId(FName Id) { return FindFish(Id) != nullptr; }
	bool IsJunkId(FName Id) { return FindJunk(Id) != nullptr; }
	bool IsBaitId(FName Id) { return FindBait(Id) != nullptr; }
	bool IsItemId(FName Id) { return FindItem(Id) != nullptr; }

	EFVContainer HomeContainerFor(FName Id)
	{
		if (IsFishId(Id)) { return EFVContainer::Keepnet; }
		if (IsJunkId(Id) || IsItemId(Id)) { return EFVContainer::Backpack; }
		return EFVContainer::Count;
	}

	// ---------------------------------------------------------------- prices
	int32 ItemPriceEx(FName ItemId, EFVQuality Q, uint8 Flags)
	{
		const int32 Base = ItemPrice(ItemId, Q);
		if (Base <= 0) { return 0; }
		if ((Flags & FVStackFlags::Smoked) != 0 && IsFishId(ItemId))
		{
			return int32(FMath::Min<int64>(int64(Base) * 2, MAX_int32));
		}
		return Base;
	}

	int32 TraderSellPrice(FName TraderId, FName ItemId, EFVQuality Q, uint8 Flags)
	{
		const int32 Base = ItemPriceEx(ItemId, Q, Flags);
		if (Base <= 0) { return 0; }
		const FFVTraderDef* T = FindTrader(TraderId);
		if (!T) { T = FindTrader(DefaultTraderId()); }
		if (T && T->Id == FVMill::MillerTraderId()) { return FVMill::MillerSellPrice(ItemId, Q, Flags); }   // update 6 (§18)
		float Mult = T ? FMath::Max(0.f, IsFishId(ItemId) ? T->FishSellMult : T->ItemSellMult) : 1.f;
		if (T)
		{
			// update 6 (§4 «Проблема (код)»): a trader's buy list has its own markup; a list-only buyer (Лесник) buys
			// nothing else (0 = does not buy)
			const FFVItemDef* I = FindItem(ItemId);
			const bool bListed = I && (I->BuyerMask & T->Mask) != 0;
			if (bListed) { Mult = FMath::Max(0.f, T->BuyListMult); }
			else if (T->bBuysOnlyList) { return 0; }
		}
		return FMath::Max(1, FMath::RoundToInt(float(Base) * Mult));
	}

	FString StackName(FName ItemId, uint8 Flags)
	{
		if ((Flags & FVStackFlags::Smoked) != 0)
		{
			if (const FFVFishDef* F = FindFish(ItemId))
			{
				const bool bFem = FVDBHome::IsFeminineName(F->Name);
				return FString::Printf(TEXT("%s %s"), bFem ? TEXT("Копчёная") : TEXT("Копчёный"), *FVDBHome::LowerRu(F->Name));
			}
		}
		return ItemName(ItemId);
	}

	// ---------------------------------------------------------------- food
	float FoodPower(EFVQuality Q)
	{
		switch (Q)
		{
		case EFVQuality::Silver:  return 1.0f;
		case EFVQuality::Gold:    return 1.2f;
		case EFVQuality::Iridium: return 1.4f;
		default:                  return 0.8f;
		}
	}

	TArray<FString> StatModsLines(const FFVStatMods& M)
	{
		using FVDBHome::Pct;
		using FVDBHome::Signed;
		using FVDBHome::Decimal;
		TArray<FString> Lines;
		const int32 Bar = FMath::RoundToInt(M.BarBonus);
		if (Bar != 0) { Lines.Add(FString::Printf(TEXT("Полоса поимки %s"), *Signed(Bar))); }
		const int32 Reel = Pct(M.ReelMult - 1.f);
		if (Reel != 0) { Lines.Add(FString::Printf(TEXT("Подтягивание %s%%"), *Signed(Reel))); }
		const int32 Drain = Pct(1.f - M.DrainMult);
		if (Drain > 0) { Lines.Add(FString::Printf(TEXT("Шкала тает медленнее на %d%%"), Drain)); }
		else if (Drain < 0) { Lines.Add(FString::Printf(TEXT("Шкала тает быстрее на %d%%"), -Drain)); }
		const int32 Bite = Pct(1.f - M.BiteSpeed);
		if (Bite > 0) { Lines.Add(FString::Printf(TEXT("Клёв быстрее на %d%%"), Bite)); }
		else if (Bite < 0) { Lines.Add(FString::Printf(TEXT("Клёв медленнее на %d%%"), -Bite)); }
		const int32 Cast = Pct(M.CastRange);
		if (Cast != 0) { Lines.Add(FString::Printf(TEXT("Дальность заброса %s%%"), *Signed(Cast))); }
		const int32 Luck = Pct(M.Luck);
		if (Luck != 0) { Lines.Add(FString::Printf(TEXT("Удача %s%%"), *Signed(Luck))); }
		const int32 Treasure = Pct(M.TreasureBonus);
		if (Treasure != 0) { Lines.Add(FString::Printf(TEXT("Шанс сокровища %s%%"), *Signed(Treasure))); }
		const int32 Size = Pct(M.SizeBonus);
		if (Size != 0) { Lines.Add(FString::Printf(TEXT("Размер рыбы %s%%"), *Signed(Size))); }
		if (FMath::Abs(M.ReactBonus) >= 0.005f)
		{
			Lines.Add(FString::Printf(TEXT("Реакция %s%s с"), M.ReactBonus >= 0.f ? TEXT("+") : TEXT("-"), *Decimal(M.ReactBonus)));
		}
		const int32 Junk = Pct(1.f - M.JunkMult);
		if (Junk > 0) { Lines.Add(FString::Printf(TEXT("Мусора меньше на %d%%"), Junk)); }
		else if (Junk < 0) { Lines.Add(FString::Printf(TEXT("Мусора больше на %d%%"), -Junk)); }
		return Lines;
	}

	// ---------------------------------------------------------------- recipes
	const TArray<FFVRecipeDef>& Recipes() { return FVDBHome::Data().Recipes; }

	const FFVRecipeDef* FindRecipe(FName Id)
	{
		const FVDBHome::FData& D = FVDBHome::Data();
		return FVDBHome::FindIn(D.Recipes, D.RecipeIdx, Id);
	}

	TArray<const FFVRecipeDef*> RecipesFor(EFVStation Station)
	{
		TArray<const FFVRecipeDef*> Out;
		for (const FFVRecipeDef& R : FVDBHome::Data().Recipes)
		{
			if (R.Station == Station && R.Yard == EFVYardBuilding::Count) { Out.Add(&R); }   // update 6: yard recipes -> FVDBU6::RecipesForYard
		}
		return Out;
	}

	// ---------------------------------------------------------------- stations
	const TArray<FFVStationDef>& Stations() { return FVDBHome::Data().Stations; }

	const FFVStationDef& Station(EFVStation S)
	{
		const TArray<FFVStationDef>& All = FVDBHome::Data().Stations;
		const int32 I = int32(S);
		if (All.IsValidIndex(I) && All[I].Station == S) { return All[I]; }
		for (const FFVStationDef& D : All)
		{
			if (D.Station == S) { return D; }
		}
		static const FFVStationDef Fallback;
		return Fallback;
	}

	const FFVStationDef* FindStation(FName Id)
	{
		const FVDBHome::FData& D = FVDBHome::Data();
		return FVDBHome::FindIn(D.Stations, D.StationIdx, Id);
	}

	FName StationId(EFVStation S) { return Station(S).Id; }
	int32 StationMaxLevel(EFVStation S) { return Station(S).Levels.Num(); }

	FString StationLevelName(EFVStation S, int32 Level)
	{
		if (Level <= 0) { return TEXT("Не построено"); }
		const FFVStationDef& D = Station(S);
		if (D.Levels.Num() == 0) { return D.Name; }
		return D.Levels[FMath::Clamp(Level, 1, D.Levels.Num()) - 1].Name;
	}

	// ---------------------------------------------------------------- traders
	const TArray<FFVTraderDef>& Traders() { return FVDBHome::Data().Traders; }

	const FFVTraderDef* FindTrader(FName Id)
	{
		const FVDBHome::FData& D = FVDBHome::Data();
		return FVDBHome::FindIn(D.Traders, D.TraderIdx, Id);
	}

	const FFVTraderDef* TraderAt(FName LocationId)
	{
		if (LocationId.IsNone()) { return nullptr; }
		for (const FFVTraderDef& T : FVDBHome::Data().Traders)
		{
			if (T.Location == LocationId) { return &T; }
		}
		return nullptr;
	}

	FName DefaultTraderId() { static const FName Id(TEXT("tr_mitrich")); return Id; }

	// ---------------------------------------------------------------- decor
	const TArray<FFVDecorDef>& Decor() { return FVDBHome::Data().Decor; }

	const FFVDecorDef* FindDecor(FName Id)
	{
		const FVDBHome::FData& D = FVDBHome::Data();
		return FVDBHome::FindIn(D.Decor, D.DecorIdx, Id);
	}

	TArray<const FFVDecorDef*> DecorFor(EFVDecorSlot Slot)
	{
		TArray<const FFVDecorDef*> Out;
		for (const FFVDecorDef& D : FVDBHome::Data().Decor)
		{
			if (D.Slot == Slot) { Out.Add(&D); }
		}
		return Out;
	}

	TArray<FName> StarterDecor() { return FVDBHome::Data().StarterDecor; }

	FString DecorSlotName(EFVDecorSlot Slot)
	{
		switch (Slot)
		{
		case EFVDecorSlot::Wallpaper: return TEXT("Обои");
		case EFVDecorSlot::Floor:     return TEXT("Пол");
		case EFVDecorSlot::Rug:       return TEXT("Ковёр");
		case EFVDecorSlot::Lamp:      return TEXT("Светильник");
		case EFVDecorSlot::Plant:     return TEXT("Растение");
		case EFVDecorSlot::Painting:  return TEXT("Картина");
		case EFVDecorSlot::Curtains:  return TEXT("Шторы");
		default:
		{
			// update 6: the yard / furniture slots (the builder's section headers, «Декор · …» in the shops)
			const FString U6 = FVDBU6::DecorSlotNameU6(Slot);
			return U6.IsEmpty() ? FString(TEXT("—")) : U6;
		}
		}
	}

	FString DecorSourceText(FName DecorId)
	{
		const FFVDecorDef* D = FindDecor(DecorId);
		if (!D) { return FString(); }
		if (D->bStarter) { return TEXT("Есть с самого начала"); }
		if (D->Price > 0)
		{
			return D->MinLevel > 1
				? FString::Printf(TEXT("В каталоге: %s монет, с %d-го уровня"), *FV::Money(D->Price), D->MinLevel)
				: FString::Printf(TEXT("В каталоге: %s монет"), *FV::Money(D->Price));
		}
		for (const FFVAchievementDef& A : Achievements())
		{
			if (A.RewardDecor == DecorId)
			{
				return FString::Printf(TEXT("Награда за достижение «%s»"), *A.Name);
			}
		}
		return TEXT("Особая награда");
	}

	// ---------------------------------------------------------------- tools & upgrades
	const TArray<FFVShovelDef>& Shovels() { return FVDBHome::Data().Shovels; }
	const TArray<FFVBackpackLevel>& BackpackLevels() { return FVDBHome::Data().Backpack; }

	// ---------------------------------------------------------------- lures (pure: no table access, safe during the base build)
	FName LureId(uint8 Base, uint8 Color, uint8 Glitter)
	{
		return FName(*FString::Printf(TEXT("lure_%d_%d_%d"), int32(Base), int32(Color), int32(Glitter)));
	}

	FString LurePartName(int32 Axis, uint8 Value)
	{
		static const TCHAR* Bases[] = { TEXT("Блесна"), TEXT("Мушка") };
		static const TCHAR* Colors[] = { TEXT("Красный"), TEXT("Жёлтый"), TEXT("Зелёный"), TEXT("Чёрный") };
		static const TCHAR* Glitters[] = { TEXT("Без блёсток"), TEXT("Серебро"), TEXT("Золото") };
		switch (Axis)
		{
		case 0: return int32(Value) < int32(UE_ARRAY_COUNT(Bases)) ? Bases[Value] : TEXT("—");
		case 1: return int32(Value) < int32(UE_ARRAY_COUNT(Colors)) ? Colors[Value] : TEXT("—");
		case 2: return int32(Value) < int32(UE_ARRAY_COUNT(Glitters)) ? Glitters[Value] : TEXT("—");
		default: return TEXT("—");
		}
	}

	TArray<FFVIngredient> LureInputs(uint8 Base, uint8 Color, uint8 Glitter)
	{
		using FVDBHome::Ing;
		TArray<FFVIngredient> Out;
		if (Base == 0) { Out.Add(Ing(TEXT("mat_scrap"), 1)); }
		else { Out.Add(Ing(TEXT("res_feather"), 1)); Out.Add(Ing(TEXT("res_fiber"), 1)); }
		switch (Color)
		{
		case 0:  Out.Add(Ing(TEXT("res_berries"), 1)); break;
		case 1:  Out.Add(Ing(TEXT("res_resin"), 1)); break;
		case 2:  Out.Add(Ing(TEXT("res_reed"), 1)); break;
		default: Out.Add(Ing(TEXT("res_clay"), 1)); break;
		}
		if (Glitter == 1) { Out.Add(Ing(TEXT("mat_glitter"), 1)); }
		else if (Glitter >= 2) { Out.Add(Ing(TEXT("mat_glitter"), 2)); }
		return Out;
	}

	int32 LurePackSize() { return 5; }

	float LureAffinity(const FFVBaitDef& Bait, const FFVFishDef& Fish)
	{
		if (Bait.AffinityMult <= 0.f) { return 1.f; }
		int32 Matches = 0;
		if (uint8(Fish.Shape) < 16 && (Bait.AffinityShapes & uint16(1u << uint8(Fish.Shape))) != 0) { ++Matches; }
		if (uint8(Fish.Motion) < 8 && (Bait.AffinityMotions & uint8(1u << uint8(Fish.Motion))) != 0) { ++Matches; }
		return 1.f + Bait.AffinityMult * float(Matches);
	}

	TArray<const FFVFishDef*> LureFavorites(const FFVBaitDef& Bait, int32 Max)
	{
		struct FCand { const FFVFishDef* Fish; float Aff; int32 Order; };
		TArray<FCand> Cands;
		const TArray<FFVFishDef>& All = Fish();
		for (int32 i = 0; i < All.Num(); ++i)
		{
			const float A = LureAffinity(Bait, All[i]);
			if (A > 1.001f) { Cands.Add({ &All[i], A, i }); }
		}
		Cands.Sort([](const FCand& A, const FCand& B)
		{
			if (!FMath::IsNearlyEqual(A.Aff, B.Aff)) { return A.Aff > B.Aff; }
			if (A.Fish->Rarity != B.Fish->Rarity) { return uint8(A.Fish->Rarity) > uint8(B.Fish->Rarity); }
			return A.Order < B.Order;
		});
		TArray<const FFVFishDef*> Out;
		for (const FCand& C : Cands)
		{
			if (Out.Num() >= FMath::Max(0, Max)) { break; }
			Out.Add(C.Fish);
		}
		return Out;
	}

	// ---------------------------------------------------------------- gathering & digging
	FName RollResource(FName LocationId, int32 Day, int32 Slot, int32& OutCount)
	{
		OutCount = 0;
		if (Slot < 0 || Slot >= FVDBHome::ResourceSlotCount(LocationId)) { return NAME_None; }

		struct FW { const TCHAR* Id; float W; };
		static const FW Pond[] = { {TEXT("res_branch"), 3}, {TEXT("res_fiber"), 3}, {TEXT("res_reed"), 3}, {TEXT("res_berries"), 2}, {TEXT("res_pebble"), 2}, {TEXT("res_feather"), 1} };
		static const FW River[] = { {TEXT("res_branch"), 3}, {TEXT("res_pebble"), 3}, {TEXT("res_clay"), 2}, {TEXT("res_mushroom"), 2}, {TEXT("res_reed"), 1}, {TEXT("res_feather"), 1} };
		static const FW Mountain[] = { {TEXT("res_pebble"), 4}, {TEXT("res_resin"), 3}, {TEXT("res_berries"), 2}, {TEXT("res_branch"), 1}, {TEXT("res_feather"), 1} };
		static const FW Ocean[] = { {TEXT("res_feather"), 3}, {TEXT("res_pebble"), 2}, {TEXT("res_fiber"), 2}, {TEXT("res_reed"), 1} };
		static const FW Swamp[] = { {TEXT("res_reed"), 3}, {TEXT("res_mushroom"), 3}, {TEXT("res_clay"), 3}, {TEXT("res_resin"), 1}, {TEXT("res_fiber"), 1} };
		static const FW Volcano[] = { {TEXT("res_pebble"), 4}, {TEXT("res_resin"), 2}, {TEXT("res_clay"), 1} };
		static const FW Home[] = { {TEXT("res_branch"), 3}, {TEXT("res_berries"), 3}, {TEXT("res_fiber"), 2}, {TEXT("res_mushroom"), 2}, {TEXT("res_feather"), 2}, {TEXT("res_pebble"), 1} };
		// update 6 (§4, Р5): only branches and pebbles lie in the forest
		static const FW Forest[] = { {TEXT("res_branch"), 3}, {TEXT("res_pebble"), 2} };

		const FW* Table = nullptr;
		int32 Num = 0;
		const FString L = LocationId.ToString().ToLower();
		if (L == TEXT("pond")) { Table = Pond; Num = int32(UE_ARRAY_COUNT(Pond)); }
		else if (L == TEXT("river")) { Table = River; Num = int32(UE_ARRAY_COUNT(River)); }
		else if (L == TEXT("mountain")) { Table = Mountain; Num = int32(UE_ARRAY_COUNT(Mountain)); }
		else if (L == TEXT("ocean")) { Table = Ocean; Num = int32(UE_ARRAY_COUNT(Ocean)); }
		else if (L == TEXT("swamp")) { Table = Swamp; Num = int32(UE_ARRAY_COUNT(Swamp)); }
		else if (L == TEXT("volcano")) { Table = Volcano; Num = int32(UE_ARRAY_COUNT(Volcano)); }
		else if (L == TEXT("home")) { Table = Home; Num = int32(UE_ARRAY_COUNT(Home)); }
		else if (L == TEXT("forest")) { Table = Forest; Num = int32(UE_ARRAY_COUNT(Forest)); }
		if (!Table || Num <= 0) { return NAME_None; }

		uint32 H = FCrc::StrCrc32(*L);
		H = FVDBHome::Mix(H, uint32(Day) * 2654435761u);
		H = FVDBHome::Mix(H, uint32(Slot) * 40503u + 0x51u);
		FRandomStream Rng(int32(FVDBHome::Fmix(H)));

		const float Spawn = (L == TEXT("home") || L == TEXT("forest")) ? 0.75f : 0.60f;
		if (Rng.FRand() >= Spawn) { return NAME_None; }

		float Total = 0.f;
		for (int32 i = 0; i < Num; ++i) { Total += Table[i].W; }
		float Roll = Rng.FRandRange(0.f, Total);
		const TCHAR* Pick = Table[Num - 1].Id;
		for (int32 i = 0; i < Num; ++i)
		{
			Roll -= Table[i].W;
			if (Roll <= 0.f) { Pick = Table[i].Id; break; }
		}
		OutCount = Rng.FRand() < 0.2f ? 2 : 1;
		// update 6 (§6.2, Р42): carrot seeds are a rare find at the Mountain lake (~1 per 2-3 game days over 5 slots);
		// without them there is no carrot, hence no уха
		if (L == TEXT("mountain") && Rng.FRand() < 0.12f)
		{
			static const FName CarrotSeed(TEXT("seed_carrot"));
			if (FindItem(CarrotSeed)) { OutCount = 1; return CarrotSeed; }
		}
		return FName(Pick);
	}

	FName RollDigLoot(FName /*LocationId*/, int32 ShovelLevel, FRandomStream& Rng, int32& OutCount)
	{
		OutCount = 0;
		const float Crawler = 0.08f + 0.03f * float(FMath::Max(0, ShovelLevel - 1));
		float R = Rng.FRand();
		OutCount = 1;
		if (R < 0.45f) { return FName(TEXT("bait_worm")); }
		R -= 0.45f;
		if (R < Crawler) { return FName(TEXT("bait_nightcrawler")); }
		R -= Crawler;
		if (R < 0.10f) { return FName(TEXT("res_pebble")); }
		R -= 0.10f;
		if (R < 0.08f) { return FName(TEXT("res_clay")); }
		R -= 0.08f;
		if (R < 0.04f) { return FName(TEXT("junk_can")); }
		OutCount = 0;
		return NAME_None;
	}

	// ---------------------------------------------------------------- nets
	float NetWear(const FFVFishDef& F, EFVQuality Q)
	{
		static const float QMult[] = { 1.f, 1.5f, 2.2f, 3.2f };
		static const float RMult[] = { 1.f, 1.3f, 1.8f, 2.6f, 4.f };
		const int32 QI = FMath::Clamp(int32(Q), 0, int32(UE_ARRAY_COUNT(QMult)) - 1);
		const int32 RI = FMath::Clamp(int32(F.Rarity), 0, int32(UE_ARRAY_COUNT(RMult)) - 1);
		return 5.f * QMult[QI] * RMult[RI];
	}

	// ---------------------------------------------------------------- level-up lines
	TArray<FString> HomeUnlocksAtLevel(int32 Level)
	{
		TArray<FString> Lines;
		if (Level < 2 || Level > FV::MaxLevel) { return Lines; }
		const FVDBHome::FData& D = FVDBHome::Data();

		// stations: new buildings and upgrades
		for (const FFVStationDef& S : D.Stations)
		{
			for (int32 i = 0; i < S.Levels.Num(); ++i)
			{
				const FFVStationLevel& L = S.Levels[i];
				if (L.MinLevel != Level || (S.bStartsBuilt && i == 0)) { continue; }
				Lines.Add(i == 0
					? FString::Printf(TEXT("Можно построить: %s"), *S.Name)
					: FString::Printf(TEXT("Улучшение: %s → %s"), *S.Name, *L.Name));
			}
		}
		// update 6: yard buildings (sold by the builder in the forest)
		for (const FFVYardBuildingDef& Y : FVDBU6::YardBuildings())
		{
			for (int32 i = 0; i < Y.Levels.Num(); ++i)
			{
				if (Y.Levels[i].MinLevel != Level || Y.Levels[i].MinLevel <= 1) { continue; }
				Lines.Add(i == 0
					? FString::Printf(TEXT("Можно построить во дворе: %s (у Прораба)"), *Y.Name)
					: FString::Printf(TEXT("Улучшение: %s → %s"), *Y.Name, *Y.Levels[i].Name));
			}
		}
		// recipes gated by the fishing level
		TArray<FString> RecipeNames;
		for (const FFVRecipeDef& R : D.Recipes)
		{
			if (R.MinLevel == Level) { RecipeNames.AddUnique(ItemName(R.OutputId)); }
		}
		if (RecipeNames.Num() > 0)
		{
			Lines.Add(FString::Printf(TEXT("Новые рецепты: %s"), *FVDBHome::JoinNames(RecipeNames, 3)));
		}
		// goods at the traders
		for (const FFVTraderDef& T : D.Traders)
		{
			TArray<FString> Goods;
			for (const FFVItemDef& I : D.Items)
			{
				if (I.MinLevel == Level && I.BuyPrice > 0 && (I.TraderMask & T.Mask) != 0) { Goods.Add(I.Name); }
			}
			for (const FFVShovelDef& S : D.Shovels)
			{
				if (S.MinLevel == Level && S.Price > 0 && (S.TraderMask & T.Mask) != 0) { Goods.Add(S.Name); }
			}
			if (Goods.Num() > 0)
			{
				Lines.Add(FString::Printf(TEXT("%s: %s"), *T.ShopTitle, *FVDBHome::JoinNames(Goods, 3)));
			}
		}
		// house decor
		TArray<FString> DecorNames;
		for (const FFVDecorDef& Dc : D.Decor)
		{
			if (Dc.MinLevel == Level && Dc.Price > 0) { DecorNames.Add(Dc.Name); }
		}
		if (DecorNames.Num() > 0)
		{
			Lines.Add(FString::Printf(TEXT("Для дома: %s"), *FVDBHome::JoinNames(DecorNames, 3)));
		}
		return Lines;
	}
}
