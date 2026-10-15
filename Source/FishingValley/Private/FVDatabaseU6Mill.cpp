// UPDATE 6 [MILL] — «Старая мельница» (Docs/UPDATE6_FOREST_CITY.md §18, Р53): static content and formulas of the mill
// (Public/FVDatabaseU6.h «FVMill»). The appenders run while FVDB builds its tables: never call FVDB table getters there.
// Economy law (§5): everything is bought dearer than it can be sold; the miller sells only rye seeds and buys only
// smoked fish (x1.4) and crafted lures — nothing he buys can be bought back from him.

#include "FVDatabase.h"
#include "FVSaveGame.h"

namespace FVMillDataImpl
{
	FFVIngredient Ing(const TCHAR* Id, int32 Count)
	{
		FFVIngredient I;
		I.ItemId = FName(Id);
		I.Count = FMath::Max(1, Count);
		return I;
	}

	template <typename T>
	bool HasId(const TArray<T>& Arr, FName Id)
	{
		for (const T& E : Arr)
		{
			if (E.Id == Id) { return true; }
		}
		return false;
	}

	FFVFishDef& AddFish(TArray<FFVFishDef>& Fish, const TCHAR* Id, const TCHAR* Name, EFVRarity Rarity, int32 Price, int32 Difficulty,
		EFVMotion Motion, float MinSize, float MaxSize, float Weight, uint8 TimeMask, float MinDepth, const TCHAR* Desc)
	{
		FFVFishDef& F = Fish.AddDefaulted_GetRef();
		F.Id = FName(Id);
		F.Name = Name;
		F.Description = Desc;
		F.Location = FVMill::MillId();
		F.Rarity = Rarity;
		F.BasePrice = Price;
		F.Difficulty = Difficulty;
		F.Motion = Motion;
		F.MinSize = MinSize;
		F.MaxSize = MaxSize;
		F.Weight = Weight;
		F.TimeMask = TimeMask;
		F.Weather = EFVWeatherReq::Any;
		F.MinLevel = FVMill::MinLevel;
		F.MinDepth = MinDepth;
		F.bLegendary = Rarity == EFVRarity::Legendary;
		return F;
	}

	void MillLook(FFVFishDef& F, EFVFishShape Shape, EFVFishPattern Pattern, FColor Body, FColor Belly, FColor Fin, FColor PatternColor)
	{
		F.Shape = Shape;
		F.Pattern = Pattern;
		F.BodyColor = Body;
		F.BellyColor = Belly;
		F.FinColor = Fin;
		F.PatternColor = PatternColor;
	}

	FFVBaitDef& AddBait(TArray<FFVBaitDef>& Baits, const TCHAR* Id, const TCHAR* Name, EFVBaitStyle Style, int32 PackSize,
		float Bite, float Rare, float JunkMult, float Size, const TCHAR* Desc)
	{
		FFVBaitDef& B = Baits.AddDefaulted_GetRef();
		B.Id = FName(Id);
		B.Name = Name;
		B.Description = Desc;
		B.Style = Style;
		B.PackPrice = 0;             // crafted only
		B.PackSize = FMath::Max(1, PackSize);
		B.MinLevel = FVMill::MinLevel;
		B.BiteSpeed = Bite;
		B.RareBonus = Rare;
		B.JunkMult = JunkMult;
		B.SizeBonus = Size;
		B.TraderMask = 0;
		return B;
	}

	FFVItemDef& AddItem(TArray<FFVItemDef>& Items, const TCHAR* Id, const TCHAR* Name, EFVItemKind Kind, EFVItemArt Art, int32 Sell,
		const TCHAR* Desc)
	{
		FFVItemDef& I = Items.AddDefaulted_GetRef();
		I.Id = FName(Id);
		I.Name = Name;
		I.Description = Desc;
		I.Kind = Kind;
		I.Art = Art;
		I.SellPrice = Sell;
		I.MaxStack = 99;
		I.MinLevel = FVMill::MinLevel;
		return I;
	}

	FFVRecipeDef& AddRecipe(TArray<FFVRecipeDef>& Recipes, const TCHAR* Id, EFVStation Station, int32 StationLevel,
		TArray<FFVIngredient> Inputs, const TCHAR* Output, int32 OutputCount, EFVCraftGame Game, int32 Difficulty)
	{
		FFVRecipeDef& R = Recipes.AddDefaulted_GetRef();
		R.Id = FName(Id);
		R.Station = Station;
		R.StationLevel = FMath::Max(1, StationLevel);
		R.MinLevel = FVMill::MinLevel;
		R.Inputs = MoveTemp(Inputs);
		R.OutputId = FName(Output);
		R.OutputCount = FMath::Max(1, OutputCount);
		R.Game = Game;
		R.GameDifficulty = Difficulty;
		return R;
	}

	FFVBoatStage Stage(const TCHAR* Name, TArray<FFVIngredient> Cost)
	{
		FFVBoatStage S;
		S.Name = Name;
		S.Cost = MoveTemp(Cost);
		S.Coins = 0;
		S.MinLevel = FVMill::MinLevel;
		return S;
	}

	bool IsId(FName Id, const TCHAR* S) { return Id == FName(S); }
}

// ============================================================================================
// Ids
// ============================================================================================
FName FVMill::MillId() { static const FName N(TEXT("mill")); return N; }
bool FVMill::IsMillId(FName LocationId) { return !LocationId.IsNone() && LocationId == MillId(); }
FName FVMill::MillerTraderId() { static const FName N(TEXT("tr_miller")); return N; }
FName FVMill::MillerNpcId() { static const FName N(TEXT("npc_prokop")); return N; }
FName FVMill::LegendFishId() { static const FName N(TEXT("fish_mill_master")); return N; }
FName FVMill::KolobokBaitId() { static const FName N(TEXT("bait_kolobok")); return N; }
FName FVMill::BreadBaitId() { static const FName N(TEXT("bait_bread")); return N; }
FName FVMill::ReedBaitId() { static const FName N(TEXT("bait_reed_bundle")); return N; }
FName FVMill::RyeSeedId() { static const FName N(TEXT("seed_rye")); return N; }
FName FVMill::RyeId() { static const FName N(TEXT("veg_rye")); return N; }
FName FVMill::FlourId() { static const FName N(TEXT("mat_flour")); return N; }
FName FVMill::TeaId() { static const FName N(TEXT("food_tea")); return N; }
FName FVMill::ObjBridgeId() { static const FName N(TEXT("mill_bridge")); return N; }
FName FVMill::ObjMillId() { static const FName N(TEXT("mill")); return N; }
FName FVMill::ObjBeaverId() { static const FName N(TEXT("beaver_lodge")); return N; }
FName FVMill::ObjDeckId() { static const FName N(TEXT("feed_deck")); return N; }
FName FVMill::ObjPenId() { static const FName N(TEXT("fish_pen")); return N; }
FName FVMill::ZoneOmutId() { static const FName N(TEXT("omut")); return N; }

FName FVMill::ZoneDeckId(int32 Deck)
{
	static const FName Ids[] = { FName(TEXT("deck_0")), FName(TEXT("deck_1")), FName(TEXT("deck_2")) };
	static_assert(UE_ARRAY_COUNT(Ids) == FeedDecks, "one zone per feeder deck");
	return (Deck >= 0 && Deck < FeedDecks) ? Ids[Deck] : NAME_None;
}

// ============================================================================================
// Appenders — classic tables
// ============================================================================================
void FVMill::AppendLocations(TArray<FFVLocationDef>& Locations)
{
	if (FVMillDataImpl::HasId(Locations, MillId())) { return; }
	FFVLocationDef& L = Locations.AddDefaulted_GetRef();
	L.Id = MillId();
	L.Name = TEXT("Старая мельница");
	L.Description = TEXT("Мельничная запруда за лесом: водяное колесо, бобровые хатки и утренний туман. Почините мостик "
		"через ручей, мельницу и помогите бобрам достроить плотину — за ней откроется глубокий Омут.");
	L.Theme = EFVTheme::Mill;
	L.MinLevel = MinLevel;        // + the repaired footbridge in the forest (UFVSaveGame::CanOpenMill), no coins
	L.UnlockPrice = 0;
	L.MapPos = FVector2D(-40.f, 212.f);   // §18: left of the forest (64,176) on the scrolling map
	L.JunkChance = 0.12f;
	L.TreasureChance = 0.12f;
	L.WaterShallow = FColor(96, 150, 138);
	L.WaterDeep = FColor(36, 78, 86);
	L.bExpertZone = false;
	L.bBigWorld = true;
}

void FVMill::AppendFish(TArray<FFVFishDef>& Fish)
{
	using namespace FVMillDataImpl;
	using R = EFVRarity;
	using M = EFVMotion;
	using S = EFVFishShape;
	using P = EFVFishPattern;
	if (HasId(Fish, LegendFishId())) { return; }
	const uint8 Any = FVTime::Any;
	{
		FFVFishDef& F = AddFish(Fish, TEXT("fish_ruffe"), TEXT("Ёрш"), R::Common, 250, 58, M::Mixed, 8, 18, 10, Any, 0.f,
			TEXT("Колючий и вечно голодный. Первым приплывает на запах прикормки."));
		MillLook(F, S::Perch, P::Spots, FColor(150, 142, 92), FColor(222, 214, 170), FColor(170, 150, 96), FColor(70, 62, 40));
	}
	{
		FFVFishDef& F = AddFish(Fish, TEXT("fish_ide"), TEXT("Язь"), R::Common, 380, 64, M::Smooth, 25, 60, 8, FVTime::Day, 0.1f,
			TEXT("Серебристый, с красноватыми плавниками. Днём охотно бросается на блесну."));
		MillLook(F, S::Carp, P::Plain, FColor(176, 184, 190), FColor(236, 238, 232), FColor(222, 104, 72), FColor(120, 130, 140));
	}
	{
		FFVFishDef& F = AddFish(Fish, TEXT("fish_rainbow_trout"), TEXT("Радужная форель"), R::Uncommon, 520, 74, M::Dart, 25, 60, 6,
			FVTime::Morning, 0.2f,
			TEXT("Ловится утром в быстрой воде у колеса — почти только на мушки. Бока переливаются радугой."));
		MillLook(F, S::Pike, P::Spots, FColor(150, 170, 132), FColor(240, 220, 210), FColor(214, 120, 136), FColor(226, 112, 128));
	}
	{
		FFVFishDef& F = AddFish(Fish, TEXT("fish_burbot"), TEXT("Налим"), R::Uncommon, 700, 78, M::Sinker, 30, 90, 6, FVTime::Night, 0.3f,
			TEXT("Ночной житель холодной воды. Любит выползков, а в дождь клюёт вдвое охотнее."));
		MillLook(F, S::Eel, P::Spots, FColor(110, 104, 66), FColor(200, 196, 150), FColor(92, 84, 52), FColor(58, 52, 34));
	}
	{
		FFVFishDef& F = AddFish(Fish, TEXT("fish_wild_carp"), TEXT("Сазан"), R::Rare, 950, 84, M::Mixed, 40, 100, 4, FVTime::Evening, 0.35f,
			TEXT("Дикий предок карпа, крупночешуйчатый и сильный. Вечером идёт на тесто и хлебную прикормку."));
		MillLook(F, S::Carp, P::Scales, FColor(176, 140, 64), FColor(236, 206, 120), FColor(160, 92, 50), FColor(120, 86, 36));
	}
	{
		FFVFishDef& F = AddFish(Fish, TEXT("fish_grass_carp"), TEXT("Белый амур"), R::Rare, 1100, 86, M::Smooth, 50, 120, 3.5f, FVTime::Day, 0.3f,
			TEXT("Травоядный великан пруда. Днём не устоит перед пучком свежего тростника."));
		MillLook(F, S::Carp, P::Scales, FColor(138, 150, 120), FColor(218, 222, 196), FColor(110, 118, 92), FColor(88, 98, 72));
	}
	{
		FFVFishDef& F = AddFish(Fish, TEXT("fish_sterlet"), TEXT("Стерлядь"), R::Epic, 1600, 92, M::Smooth, 40, 100, 2.5f, Any, 0.6f,
			TEXT("Царская рыба с костяными жучками на спине. Живёт только в глубоком Омуте за плотиной."));
		MillLook(F, S::Sturgeon, P::Plain, FColor(120, 124, 128), FColor(226, 220, 196), FColor(96, 100, 106), FColor(210, 204, 180));
	}
	{
		// §18 (Р53): the legend of the Omut — a water-spirit catfish, 10 % harder than the Zhar-ryba (110 x 1.1 = 121).
		// Preparation like the Zhar-ryba: its own bait (bait_kolobok) AND a strong line (braided or better) — both hard.
		FFVFishDef& F = AddFish(Fish, TEXT("fish_mill_master"), TEXT("Хозяин омута"), R::Legendary, 12000, 121, M::Sinker, 150, 260, 0.4f,
			FVTime::Night, 0.7f,
			TEXT("Старый сом-водяной из-под мельничного колеса. Говорят, это он крутит жернова по ночам."));
		MillLook(F, S::Legend, P::Spots, FColor(70, 76, 58), FColor(170, 164, 120), FColor(52, 58, 44), FColor(38, 42, 30));
		F.Legend = EFVLegend::MillMaster;
		FFVLegendPrep& Pr = F.Prep;
		Pr.Bait = KolobokBaitId();
		Pr.Lines = { FName(TEXT("line_braided")), FName(TEXT("line_silk")), FName(TEXT("line_fluoro")), FName(TEXT("line_titan")),
			FName(TEXT("line_heat")) };
		Pr.PrepItems = { TEXT("Мельничный колобок"), TEXT("прочная леска") };
		Pr.PrepText = TEXT("Хозяин омута берёт только мельничный колобок (Кухня, Печь) и рвёт тонкую леску: нужна прочная — "
			"плетёная или лучше. Ищи его ночью в Омуте за плотиной, на глубине.");
		Pr.FightText = TEXT("Четыре фазы. «Омут»: водоворот от колеса тянет полосу вниз. «Жернова»: тяжёлые рывки каждые две "
			"секунды — перед ними глухой стук. «Муть»: облака ила прячут рыбу. «Последний рывок»: сом рвётся к поверхности.");
		Pr.GatedToast = TEXT("В омуте что-то тяжело повернулось… Нужны колобок и прочная леска.");
	}
}

void FVMill::AppendBaits(TArray<FFVBaitDef>& Baits)
{
	using namespace FVMillDataImpl;
	if (HasId(Baits, KolobokBaitId())) { return; }
	AddBait(Baits, TEXT("bait_bread"), TEXT("Хлебная прикормка"), EFVBaitStyle::BreadGroundbait, 5, 0.40f, 0.08f, 1.00f, 0.05f,
		TEXT("Прикормка на ржаной муке: пахнет свежим хлебом. Рыба собирается быстрее, чем на обычную, и крупнее."));
	{
		FFVBaitDef& B = AddBait(Baits, TEXT("bait_kolobok"), TEXT("Мельничный колобок"), EFVBaitStyle::Kolobok, 3, 0.90f, 0.20f, 0.20f, 0.20f,
			TEXT("Тяжёлый шар из ржаной муки, налима и ягод. Хозяин омута не устоит, прочая рыба берёт лениво."));
		B.LegendLure = EFVLegend::MillMaster;
		B.LegendLureMult = 3.f;    // like bait_ember after §0.3 (Р45): ~18-20 % of the deep Omut bites with the best tackle
		B.OtherFishMult = 0.35f;
	}
	AddBait(Baits, TEXT("bait_reed_bundle"), TEXT("Пучок тростника"), EFVBaitStyle::ReedBundle, 5, 1.00f, 0.05f, 0.80f, 0.05f,
		TEXT("Связка молодого тростника. Белый амур клюёт почти только на неё."));
}

void FVMill::AppendCosmetics(TArray<FFVCosmeticDef>& Cosmetics)
{
	if (FVMillDataImpl::HasId(Cosmetics, FName(TEXT("bobber_millstone")))) { return; }
	FFVCosmeticDef& C = Cosmetics.AddDefaulted_GetRef();
	C.Id = FName(TEXT("bobber_millstone"));
	C.Name = TEXT("Жёрнов");
	C.Description = TEXT("Каменный жёрнов с насечками, крутится на волне. Награда за Хозяина омута.");
	C.Slot = EFVCosmeticSlot::Bobber;
	C.Style = uint8(EFVBobberStyle::Millstone);
	C.Price = 0;
	C.MinLevel = 1;
	C.Primary = FColor(150, 146, 138);
	C.Secondary = FColor(96, 90, 84);
}

void FVMill::AppendAchievements(TArray<FFVAchievementDef>& Achievements)
{
	if (FVMillDataImpl::HasId(Achievements, FName(TEXT("ach_legend_mill")))) { return; }
	FFVAchievementDef& A = Achievements.AddDefaulted_GetRef();
	A.Id = FName(TEXT("ach_legend_mill"));
	A.Name = TEXT("Хозяин омута");
	A.Description = TEXT("Поймайте Хозяина омута у Старой мельницы. Награда — поплавок «Жёрнов»!");
	A.Kind = EFVAchKind::CatchFish;
	A.Target = 1;
	A.Param = LegendFishId();
	A.RewardMoney = 0;
	A.RewardCosmetic = FName(TEXT("bobber_millstone"));
	A.Tier = uint8(EFVAchTier::Expert);   // like «Укротитель пламени»
}

// ============================================================================================
// Appenders — home tables
// ============================================================================================
void FVMill::AppendItems(TArray<FFVItemDef>& Items)
{
	using namespace FVMillDataImpl;
	using K = EFVItemKind;
	using A = EFVItemArt;

	// §18 «Магазины» (Р53): less ready-made tackle — these are crafted at the workshop now (their recipes exist)
	{
		static const TCHAR* NoLongerSold[] = {
			TEXT("mat_glue"), TEXT("mat_sinker"), TEXT("float_feather"), TEXT("line_linen"), TEXT("line_braided"),
			TEXT("float_sliding"), TEXT("net_small"), TEXT("net_medium"),   // Митрич
			TEXT("float_glow"), TEXT("net_large"),                          // Боцман
		};
		for (const TCHAR* Id : NoLongerSold)
		{
			const FName N(Id);
			for (FFVItemDef& I : Items)
			{
				if (I.Id == N) { I.BuyPrice = 0; I.TraderMask = 0; }
			}
		}
	}

	if (HasId(Items, FlourId())) { return; }
	{
		FFVItemDef& I = AddItem(Items, TEXT("seed_rye"), TEXT("Семена ржи"), K::Seed, A::SeedPacket, 4,
			TEXT("Зерно озимой ржи от Мельника Прокопа. Посадите в огороде — вырастет рожь для мельницы."));
		I.Tint = FColor(214, 176, 96);
		I.Tier = 5;   // the seed packet shows rye ears (FVArtItemsU6 SeedPacket)
		I.BuyPrice = 40;
		I.TraderMask = MillerMask;
	}
	AddItem(Items, TEXT("veg_rye"), TEXT("Рожь"), K::Material, A::Rye, 6,
		TEXT("Спелые колосья ржи. На работающей мельнице из двух снопов выходит мешочек муки."));
	AddItem(Items, TEXT("mat_flour"), TEXT("Ржаная мука"), K::Material, A::Flour, 16,
		TEXT("Мука мельничного помола. Из неё пекут блины, рыбный пирог, хлебную прикормку и колобок."));
	{
		FFVItemDef& I = AddItem(Items, TEXT("food_bliny"), TEXT("Блины"), K::Food, A::Bliny, 40,
			TEXT("Стопка горячих ржаных блинов. Реакция на поклёвку дольше, клюёт чуть быстрее. 4 ч."));
		I.Buff.ReactBonus = 0.15f;
		I.Buff.BiteSpeed = 0.9f;
		I.BuffMinutes = 240.f;
	}
	{
		FFVItemDef& I = AddItem(Items, TEXT("food_rye_fish_pie"), TEXT("Рыбный пирог"), K::Food, A::RyeFishPie, 120,
			TEXT("Пирог на ржаной муке с рыбой и луком. Полоса шире, удачи больше. 6 ч."));
		I.Buff.BarBonus = 12.f;
		I.Buff.Luck = 0.1f;
		I.BuffMinutes = 360.f;
	}
}

void FVMill::AppendRecipes(TArray<FFVRecipeDef>& Recipes)
{
	using namespace FVMillDataImpl;
	using St = EFVStation;
	using G = EFVCraftGame;
	if (HasId(Recipes, FName(TEXT("rcp_kolobok")))) { return; }
	AddRecipe(Recipes, TEXT("rcp_bread_bait"), St::Kitchen, 2, { Ing(TEXT("mat_flour"), 1), Ing(TEXT("bait_dough"), 2) },
		TEXT("bait_bread"), 5, G::None, 1);
	AddRecipe(Recipes, TEXT("rcp_bliny"), St::Kitchen, 2, { Ing(TEXT("mat_flour"), 1), Ing(TEXT("res_berries"), 1) },
		TEXT("food_bliny"), 1, G::Cooking, 2);
	AddRecipe(Recipes, TEXT("rcp_rye_fish_pie"), St::Kitchen, 3,
		{ Ing(TEXT("mat_flour"), 2), Ing(TEXT("any_fish"), 2), Ing(TEXT("veg_onion"), 1) }, TEXT("food_rye_fish_pie"), 1, G::Cooking, 3);
	AddRecipe(Recipes, TEXT("rcp_kolobok"), St::Kitchen, 3,
		{ Ing(TEXT("mat_flour"), 2), Ing(TEXT("fish_burbot"), 1), Ing(TEXT("res_berries"), 1) }, TEXT("bait_kolobok"), 3, G::Cooking, 3);
	AddRecipe(Recipes, TEXT("rcp_reed_bundle"), St::Workshop, 1, { Ing(TEXT("res_reed"), 3) }, TEXT("bait_reed_bundle"), 5, G::None, 1);
}

void FVMill::AppendTraders(TArray<FFVTraderDef>& Traders)
{
	if (FVMillDataImpl::HasId(Traders, MillerTraderId())) { return; }
	FFVTraderDef& T = Traders.AddDefaulted_GetRef();
	T.Id = MillerTraderId();
	T.Name = TEXT("Мельник Прокоп");
	T.Role = TEXT("покупает копчёную рыбу и самодельные блёсны, продаёт семена ржи");
	T.ShopTitle = TEXT("Мельница");
	T.Location = MillId();
	T.Mask = MillerMask;
	T.NpcId = MillerNpcId();
	T.FishSellMult = MillerSmokedMult;   // only smoked fish (FVMill::MillerSellPrice)
	T.ItemSellMult = 0.f;                // buys no items
	T.bBuysOnlyList = false;             // the «Продать» tab stays (smoked fish; lures: UFVSaveGame::SellLuresToMiller)
	T.bSellsClothes = false;
	T.bSellsUpgrades = false;
}

FFVSeedDef FVMill::RyeSeed()
{
	FFVSeedDef S;
	S.SeedId = RyeSeedId();
	S.CropId = RyeId();
	S.GrowMinutes = 25.f;        // real minutes of play
	S.YieldMin = 2;
	S.YieldMax = 4;
	S.SeedsMin = 0;
	S.SeedsMax = 1;
	S.RainMult = 1.25f;
	S.MinLevel = MinLevel;
	return S;
}

// ============================================================================================
// Repairs
// ============================================================================================
const FFVBoatStage& FVMill::BridgeRepair()
{
	using namespace FVMillDataImpl;
	static const FFVBoatStage S = Stage(TEXT("Мостик через ручей"),
		{ Ing(TEXT("mat_board"), 12), Ing(TEXT("mat_rope"), 6), Ing(TEXT("mat_glue"), 2) });
	return S;
}

const TArray<FFVBoatStage>& FVMill::MillStages()
{
	using namespace FVMillDataImpl;
	static const TArray<FFVBoatStage> A = {
		Stage(TEXT("Колесо"), { Ing(TEXT("mat_board"), 15), Ing(TEXT("mat_sinker"), 10), Ing(TEXT("mat_rope"), 4) }),
		Stage(TEXT("Жернова"), { Ing(TEXT("res_pebble"), 30), Ing(TEXT("mat_mpk"), 2) }),
		Stage(TEXT("Крыша"), { Ing(TEXT("mat_board"), 10), Ing(TEXT("res_resin"), 6), Ing(TEXT("mat_glue"), 3) }),
	};
	return A;
}

// ============================================================================================
// Fishing
// ============================================================================================
bool FVMill::IsOmutOnly(const FFVFishDef& F)
{
	return F.Id == LegendFishId() || F.Id == FName(TEXT("fish_sterlet"));
}

bool FVMill::IsFishAllowed(const FFVCatchContext& Ctx, const FFVFishDef& F)
{
	if (!IsMillId(F.Location)) { return true; }
	return !IsOmutOnly(F) || Ctx.bMillOmut;
}

float FVMill::CatchWeightMult(const FFVCatchContext& Ctx, const FFVFishDef& F)
{
	if (!IsMillId(F.Location)) { return 1.f; }
	using namespace FVMillDataImpl;
	float W = 1.f;
	const FFVBaitDef* B = Ctx.bNet ? nullptr : Ctx.Bait;
	const EFVBaitStyle Style = B ? B->Style : EFVBaitStyle::Count;
	const bool bGround = Style == EFVBaitStyle::Groundbait || Style == EFVBaitStyle::BreadGroundbait;
	if (IsId(F.Id, TEXT("fish_ruffe")))
	{
		if (bGround) { W *= 1.8f; }                                            // любит прикормку
	}
	else if (IsId(F.Id, TEXT("fish_ide")))
	{
		if (Style == EFVBaitStyle::Lure) { W *= 2.f; }                         // родство к блёснам
	}
	else if (IsId(F.Id, TEXT("fish_rainbow_trout")))
	{
		if (!Ctx.bNet) { W *= Style == EFVBaitStyle::Fly ? 3.f : 0.25f; }      // почти только на мушки (x3)
	}
	else if (IsId(F.Id, TEXT("fish_burbot")))
	{
		if (Style == EFVBaitStyle::Nightcrawler) { W *= 2.f; }                 // выползок
		if (Ctx.Weather == EFVWeather::Rain) { W *= 2.f; }                     // в дождь охотнее
	}
	else if (IsId(F.Id, TEXT("fish_wild_carp")))
	{
		if (Style == EFVBaitStyle::Dough || Style == EFVBaitStyle::BreadGroundbait) { W *= 2.f; }
	}
	else if (IsId(F.Id, TEXT("fish_grass_carp")))
	{
		if (!Ctx.bNet) { W *= Style == EFVBaitStyle::ReedBundle ? 4.f : 0.2f; }   // наживка «пучок тростника»
	}
	// feeder deck: rare / epic x1.5 (not the legend)
	if (Ctx.bMillDeck && (F.Rarity == EFVRarity::Rare || F.Rarity == EFVRarity::Epic)) { W *= DeckRareMult; }
	// fish pen: released species bite x1.5 for 3 game days
	if (!F.bLegendary && Ctx.MillPenFish.Contains(F.Id)) { W *= PenWeightMult; }
	return W;
}

const TArray<FName>& FVMill::DeckBaits()
{
	static const TArray<FName> A = { FName(TEXT("bait_bread")), FName(TEXT("bait_groundbait")), FName(TEXT("bait_dough")) };
	return A;
}

bool FVMill::IsFogMorning(float ClockMinutes)
{
	return ClockMinutes >= 6.f * 60.f && ClockMinutes < 10.f * 60.f;
}

// ============================================================================================
// Trading
// ============================================================================================
int32 FVMill::MillerSellPrice(FName ItemId, EFVQuality Q, uint8 Flags)
{
	if ((Flags & FVStackFlags::Smoked) == 0 || !FVDB::FindFish(ItemId)) { return 0; }
	const int32 Base = FVDB::ItemPriceEx(ItemId, Q, Flags);
	if (Base <= 0) { return 0; }
	const double V = FMath::RoundToDouble(double(Base) * double(MillerSmokedMult));
	return FMath::Max(1, int32(FMath::Min(V, double(MAX_int32))));
}

int32 FVMill::MillerLurePrice(FName BaitId)
{
	const FFVBaitDef* B = FVDB::FindBait(BaitId);
	if (!B || !B->bLure) { return 0; }
	// a piece: spoon 12 / fly 10, +10 per glitter tier (materials of a pack of 5 cost ~20-60)
	return (B->LureBase == 0 ? 12 : 10) + 10 * FMath::Clamp<int32>(B->LureGlitter, 0, 2);
}

// ============================================================================================
// World objects
// ============================================================================================
int32 FVMill::ObjState(const UFVSaveGame* P, FName ObjId, int32 Index, double NowAbs)
{
	if (!P) { return 0; }
	if (ObjId == ObjBridgeId()) { return P->IsBridgeRepaired() ? 1 : 0; }
	if (ObjId == ObjMillId()) { return P->GetMillStage(); }
	if (ObjId == ObjBeaverId()) { return FMath::Clamp(P->U6.Mill.BeaverFeeds, 0, BeaverFeedsNeeded); }
	if (ObjId == ObjDeckId()) { return P->IsDeckBaited(Index, NowAbs) ? 1 : 0; }
	if (ObjId == ObjPenId()) { return FMath::Min(PenSlots, P->ActivePenFish(NowAbs).Num()); }
	return 0;
}

FString FVMill::ObjLabel(const UFVSaveGame* P, FName ObjId, int32 Index, double NowAbs)
{
	const int32 State = ObjState(P, ObjId, Index, NowAbs);
	if (ObjId == ObjBridgeId())
	{
		return State > 0 ? FString(TEXT("Мостик к мельнице")) : FString(TEXT("Сломанный мостик через ручей"));
	}
	if (ObjId == ObjMillId())
	{
		const int32 N = MillStages().Num();
		return State >= N ? FString(TEXT("Мельница — молоть рожь")) : FString::Printf(TEXT("Старая мельница (ремонт %d/%d)"), State, N);
	}
	if (ObjId == ObjBeaverId())
	{
		return State >= BeaverFeedsNeeded ? FString(TEXT("Бобровая хатка — плотина готова"))
			: FString::Printf(TEXT("Бобровая хатка (плотина %d/%d)"), State, BeaverFeedsNeeded);
	}
	if (ObjId == ObjDeckId())
	{
		return State > 0 ? FString::Printf(TEXT("Мосток %d — прикормлен"), Index + 1)
			: FString::Printf(TEXT("Прикормочный мосток %d"), Index + 1);
	}
	if (ObjId == ObjPenId())
	{
		return FString::Printf(TEXT("Рыборазводный садок (%d/%d)"), State, PenSlots);
	}
	return FString();
}
