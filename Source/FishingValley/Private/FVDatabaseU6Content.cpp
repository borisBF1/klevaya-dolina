// UPDATE 6 [DATA-CONTENT] — City/Cave locations, new ordinary fish (§3а, §8, §9а), villagers, cosmetics (forester,
// boutique, legendary rewards), yard decor & furniture, achievements with tiers (Public/FVDatabaseU6.h «DATA-CONTENT»).
// Owner: DATA-CONTENT. Appenders run while FVDB builds its tables: never call FVDB table getters from them.
//
// Decor styles per slot (FFVDecorDef::Style, drawn by ART-DECOR in FVArtDecorU6.cpp):
//   YardRoof 0 черепица, 1 медь · YardWalls 0 крашеные брёвна, 1 белёные · YardShutters 0 резные, 1 голубые ·
//   YardFence 0 штакетник, 1 плетень, 2 камень · YardPath 0 доски, 1 камень · YardLantern 0 · YardMailbox 0 ·
//   YardBench 0 · YardFlowerbed 0 · YardScarecrow 0 · YardWell 0 · YardVane 0 петушок, 1 маяк ·
//   YardCenter 0 поленница, 1 фонтан, 2 статуя рыбы, 3 золотой рыбак (награда) ·
//   Cupboard 0 · Bookcase 0 · Clock 0 напольные, 1 золочёные · Fireplace 0 · Chandelier 0 бронза, 1 хрусталь ·
//   Statue 0 чучело Царь-Карпа, 1 аквариум-колонна · StoveSkin 0 изразцы · BedSkin 0 балдахин.

#include "FVDatabase.h"

namespace FVDBU6ContentLocal
{
	// Trader bits (spec §17.3)
	constexpr uint8 MaskForester = 4;
	constexpr uint8 MaskBuilder = 8;
	constexpr uint8 MaskBoutique = 64;

	/** Number of fish at the classic (non-expert) locations, counted by AppendFish; AppendAchievements runs after it
	    (the order of the appenders is part of the contract, FVDatabaseU6.cpp) and gives it to «Ихтиолог». */
	int32 GClassicSpecies = 0;

	// ------------------------------------------------------------------------------------------------ fish
	FFVFishDef& FishRow(TArray<FFVFishDef>& Fish, const TCHAR* Id, const TCHAR* Name, const TCHAR* Location, EFVRarity Rarity,
		int32 Price, int32 Difficulty, EFVMotion Motion, float MinSize, float MaxSize, float Weight, const TCHAR* Desc)
	{
		FFVFishDef& F = Fish.AddDefaulted_GetRef();
		F.Id = FName(Id);
		F.Name = Name;
		F.Description = Desc;
		F.Location = FName(Location);
		F.Rarity = Rarity;
		F.BasePrice = Price;
		F.Difficulty = Difficulty;
		F.Motion = Motion;
		F.MinSize = MinSize;
		F.MaxSize = MaxSize;
		F.Weight = Weight;
		F.bLegendary = false;   // ordinary fish only (Р39)
		F.bExpert = false;
		return F;
	}

	void FishWhen(FFVFishDef& F, uint8 TimeMask, EFVWeatherReq Weather, int32 MinLevel, float MinDepth)
	{
		F.TimeMask = TimeMask;
		F.Weather = Weather;
		F.MinLevel = MinLevel;
		F.MinDepth = MinDepth;
	}

	void FishLook(FFVFishDef& F, EFVFishShape Shape, EFVFishPattern Pattern, FColor Body, FColor Belly, FColor Fin, FColor PatternColor)
	{
		F.Shape = Shape;
		F.Pattern = Pattern;
		F.BodyColor = Body;
		F.BellyColor = Belly;
		F.FinColor = Fin;
		F.PatternColor = PatternColor;
	}

	// ------------------------------------------------------------------------------------------------ cosmetics
	FFVCosmeticDef& CosRow(TArray<FFVCosmeticDef>& Arr, const TCHAR* Id, const TCHAR* Name, EFVCosmeticSlot Slot, uint8 Style,
		int32 Price, int32 MinLevel, FColor Primary, FColor Secondary, uint8 TraderMask, bool bElite, const TCHAR* Desc)
	{
		FFVCosmeticDef& C = Arr.AddDefaulted_GetRef();
		C.Id = FName(Id);
		C.Name = Name;
		C.Description = Desc;
		C.Slot = Slot;
		C.Style = Style;
		C.Price = Price;
		C.MinLevel = MinLevel;
		C.Primary = Primary;
		C.Secondary = Secondary;
		C.TraderMask = TraderMask;
		C.bElite = bElite;
		return C;
	}

	// ------------------------------------------------------------------------------------------------ decor
	void DecorRow(TArray<FFVDecorDef>& Arr, const TCHAR* Id, const TCHAR* Name, EFVDecorSlot Slot, uint8 Style, int32 Price,
		int32 MinLevel, FColor Primary, FColor Secondary, uint8 TraderMask, const TCHAR* Desc)
	{
		FFVDecorDef& Dc = Arr.AddDefaulted_GetRef();
		Dc.Id = FName(Id);
		Dc.Name = Name;
		Dc.Description = Desc;
		Dc.Slot = Slot;
		Dc.Style = Style;
		Dc.Price = Price;
		Dc.MinLevel = MinLevel;
		Dc.bStarter = false;    // update-6 decor is never a starter (FVDB::StarterDecor is built by the classic table)
		Dc.Primary = Primary;
		Dc.Secondary = Secondary;
		Dc.TraderMask = TraderMask;
	}

	// ------------------------------------------------------------------------------------------------ achievements
	void AchRow(TArray<FFVAchievementDef>& Arr, const TCHAR* Id, const TCHAR* Name, EFVAchKind Kind, int32 Target, EFVAchTier Tier,
		int64 Money, const TCHAR* Cosmetic, const TCHAR* Decor, const TCHAR* Desc)
	{
		FFVAchievementDef& A = Arr.AddDefaulted_GetRef();
		A.Id = FName(Id);
		A.Name = Name;
		A.Description = Desc;
		A.Kind = Kind;
		A.Target = FMath::Max(1, Target);
		A.Param = NAME_None;
		A.RewardMoney = Money;
		A.RewardCosmetic = Cosmetic ? FName(Cosmetic) : NAME_None;
		A.RewardDecor = Decor ? FName(Decor) : NAME_None;
		A.Tier = uint8(Tier);
	}

	/** Tiers of the achievements that existed before update 6 (§13: spread over the first three tiers). Ids missing here
	    stay Normal. */
	struct FTierRow { const TCHAR* Id; EFVAchTier Tier; };
	const FTierRow ClassicTiers[] = {
		// Трудные
		{ TEXT("ach_catch_250"), EFVAchTier::Hard },
		{ TEXT("ach_perfect_50"), EFVAchTier::Hard },
		{ TEXT("ach_iridium_5"), EFVAchTier::Hard },
		{ TEXT("ach_level_10"), EFVAchTier::Hard },
		{ TEXT("ach_all_locations"), EFVAchTier::Hard },
		{ TEXT("ach_all_rods"), EFVAchTier::Hard },
		{ TEXT("ach_all_stations"), EFVAchTier::Hard },
		{ TEXT("ach_nets"), EFVAchTier::Hard },
		{ TEXT("ach_lures_all"), EFVAchTier::Hard },
		{ TEXT("ach_legend_river"), EFVAchTier::Hard },
		{ TEXT("ach_legend_mountain"), EFVAchTier::Hard },
		{ TEXT("ach_legend_ocean"), EFVAchTier::Hard },
		{ TEXT("ach_legend_swamp"), EFVAchTier::Hard },
		// Экспертные
		{ TEXT("ach_species_all"), EFVAchTier::Expert },
		{ TEXT("ach_legends"), EFVAchTier::Expert },
		{ TEXT("ach_stations_max"), EFVAchTier::Expert },
		{ TEXT("ach_legend_volcano"), EFVAchTier::Expert },
	};

	// ------------------------------------------------------------------------------------------------ villagers
	FFVNpcDef NpcRow(const TCHAR* Id, const TCHAR* Name, const TCHAR* Role, const TCHAR* Location, const TCHAR* TraderId,
		EFVScreen Screen, const TCHAR* Greeting, FColor Skin, FColor Hair, FColor Cloth, FColor Cloth2)
	{
		FFVNpcDef N;
		N.Id = FName(Id);
		N.Name = Name;
		N.Role = Role;
		N.Location = FName(Location);
		N.TraderId = TraderId ? FName(TraderId) : NAME_None;
		N.Screen = Screen;
		N.Greeting = Greeting;
		N.Skin = Skin;
		N.Hair = Hair;
		N.Cloth = Cloth;
		N.Cloth2 = Cloth2;
		return N;
	}

	TArray<FFVNpcDef> BuildNpcTable()
	{
		TArray<FFVNpcDef> A;
		A.Reserve(13);
		const FColor SkinLight(236, 188, 150), SkinWarm(222, 170, 128), SkinTan(198, 146, 106), SkinPale(244, 204, 172);

		// ---------------------------------------------------------------- Лес, Чаща (§4)
		A.Add(NpcRow(TEXT("npc_potapych"), TEXT("Лесник Потапыч"), TEXT("покупает ветки, брёвна и доски; лесная одежда"),
			TEXT("forest"), TEXT("tr_forester"), EFVScreen::None,
			TEXT("Здорово, рыбак! Лес шума не любит, а вот брёвна — любит считать. Неси, куплю."),
			SkinWarm, FColor(170, 168, 160), FColor(82, 104, 62), FColor(150, 70, 52)));
		A.Add(NpcRow(TEXT("npc_gavrila"), TEXT("Прораб Гаврила"), TEXT("постройки двора, украшения двора и мебель"),
			TEXT("forest"), TEXT("tr_builder"), EFVScreen::Builder,
			TEXT("Строим на совесть! Лесопилку, огород, кузню — ты только брёвна подвози."),
			SkinTan, FColor(112, 72, 40), FColor(236, 162, 46), FColor(70, 86, 120)));

		// ---------------------------------------------------------------- Город: лавки и артель (§8, §8.3)
		A.Add(NpcRow(TEXT("npc_nikifor"), TEXT("Артельщик Никифор"), TEXT("ставит ваши сети и каждое утро продаёт улов"),
			TEXT("city"), nullptr, EFVScreen::Artel,
			TEXT("Отдай артели сети — утром будут деньги. Мы своё дело знаем."),
			SkinTan, FColor(60, 44, 32), FColor(66, 82, 110), FColor(214, 200, 170)));
		A.Add(NpcRow(TEXT("npc_agafya"), TEXT("Огородница Агафья"), TEXT("семена для огорода"),
			TEXT("city"), TEXT("tr_gardener"), EFVScreen::None,
			TEXT("Семечко в землю — глядишь, и на стол. Бери, милок, не пожалеешь!"),
			SkinLight, FColor(150, 130, 110), FColor(96, 140, 70), FColor(226, 86, 72)));
		A.Add(NpcRow(TEXT("npc_kuzma"), TEXT("Снастёвщик Кузьма"), TEXT("экспертные удочки, снасти и чертежи"),
			TEXT("city"), TEXT("tr_city_tackle"), EFVScreen::None,
			TEXT("Таких удочек в деревне не водится. «Бриз» — для начала, а там поглядим."),
			SkinWarm, FColor(40, 34, 30), FColor(58, 70, 84), FColor(196, 150, 70)));
		A.Add(NpcRow(TEXT("npc_zhanna"), TEXT("Хозяйка бутика Жанна"), TEXT("элитная одежда и украшения двора"),
			TEXT("city"), TEXT("tr_boutique"), EFVScreen::None,
			TEXT("Ах, какой гость! У нас только лучшее — и цены, разумеется, под стать."),
			SkinPale, FColor(40, 28, 36), FColor(150, 40, 90), FColor(242, 206, 90)));

		// ---------------------------------------------------------------- Город: заказчики доски заданий (§8.2)
		A.Add(NpcRow(TEXT("npc_burun"), TEXT("Капитан Бурун"), TEXT("задания Города"),
			TEXT("city"), nullptr, EFVScreen::CityBoard,
			TEXT("Полный вперёд! На доске — работа для настоящего рыбака."),
			SkinTan, FColor(210, 210, 205), FColor(34, 50, 92), FColor(236, 196, 72)));
		A.Add(NpcRow(TEXT("npc_henri"), TEXT("Шеф-повар Анри"), TEXT("задания Города: рыба и блюда"),
			TEXT("city"), nullptr, EFVScreen::CityBoard,
			TEXT("Моей кухне нужна рыба золотого качества, мон ами. Серебро оставьте чайкам!"),
			SkinPale, FColor(70, 44, 30), FColor(246, 246, 240), FColor(196, 50, 50)));
		A.Add(NpcRow(TEXT("npc_foma"), TEXT("Смотритель маяка Фома"), TEXT("задания Города; знает, когда шторм"),
			TEXT("city"), nullptr, EFVScreen::CityBoard,
			TEXT("С маяка всё видать: где рыба ходит, когда шторм идёт. Подсоби — расскажу."),
			SkinWarm, FColor(120, 100, 80), FColor(40, 60, 88), FColor(226, 226, 220)));
		A.Add(NpcRow(TEXT("npc_lidia"), TEXT("Учёная Лидия"), TEXT("задания Города; изучает повадки рыб"),
			TEXT("city"), nullptr, EFVScreen::CityBoard,
			TEXT("Каждая рыба — загадка. Принесите образец, и я поделюсь наблюдениями."),
			SkinLight, FColor(150, 90, 50), FColor(236, 238, 242), FColor(70, 110, 150)));
		A.Add(NpcRow(TEXT("npc_zoya"), TEXT("Рыбная торговка Зоя"), TEXT("задания Города: свежая рыба"),
			TEXT("city"), nullptr, EFVScreen::CityBoard,
			TEXT("Свежак беру, тухляк не беру! А иридиевую — с руками оторву."),
			SkinWarm, FColor(180, 60, 40), FColor(90, 130, 150), FColor(236, 222, 190)));
		A.Add(NpcRow(TEXT("npc_efim"), TEXT("Старый лоцман Ефим"), TEXT("задания Города; помнит все морские тропы"),
			TEXT("city"), nullptr, EFVScreen::CityBoard,
			TEXT("Полвека эти воды знаю. Уважь старика — и я тебе про марлина кой-чего поведаю."),
			SkinTan, FColor(236, 236, 230), FColor(70, 58, 50), FColor(40, 56, 80)));
		A.Add(NpcRow(TEXT("npc_marfa"), TEXT("Хозяйка трактира Марфа"), TEXT("задания Города: блюда для трактира"),
			TEXT("city"), nullptr, EFVScreen::CityBoard,
			TEXT("Гостям подавай уху да рыбник. Подсобишь с припасами — не обижу."),
			SkinLight, FColor(110, 60, 40), FColor(160, 70, 60), FColor(244, 236, 214)));

		// ---------------------------------------------------------------- Старая мельница (§18, Р53) [MILL]
		A.Add(NpcRow(TEXT("npc_prokop"), TEXT("Мельник Прокоп"), TEXT("покупает копчёную рыбу и самодельные блёсны; семена ржи"),
			TEXT("mill"), TEXT("tr_miller"), EFVScreen::None,
			TEXT("Мука да рыба — вот и вся наука. Копчёного принесёшь — заплачу щедро."),
			SkinWarm, FColor(214, 210, 200), FColor(236, 230, 212), FColor(120, 96, 70)));
		return A;
	}
}

// ================================================================================================ locations
void FVDBU6::AppendLocations(TArray<FFVLocationDef>& Locations)
{
	// MapPos: the scrolling map (§3) extends the classic 640x360 map space to the right, beyond the sea (X > 640).
	{
		FFVLocationDef& L = Locations.AddDefaulted_GetRef();
		L.Id = CityId();
		L.Name = TEXT("Город");
		L.Description = TEXT("Шумный порт за морем: пирс, лавки, бутик и доска заданий. Экспертная зона — здесь клюёт элитная рыба.");
		L.Theme = EFVTheme::City;
		L.UnlockPrice = 0;            // opened by the repaired boat (§7), not by coins
		L.MinLevel = FV::ClassicMaxLevel;
		L.MapPos = FVector2D(770.f, 238.f);
		L.JunkChance = 0.16f;
		L.TreasureChance = 0.14f;
		L.WaterShallow = FColor(62, 146, 200);
		L.WaterDeep = FColor(14, 50, 112);
		L.bExpertZone = true;
		L.bBigWorld = true;
	}
	{
		FFVLocationDef& L = Locations.AddDefaulted_GetRef();
		L.Id = CaveId();
		L.Name = TEXT("Пещера");
		L.Description = TEXT("Тёмные своды за завалами на окраине Города: подземное озеро, капель и валуны с твёрдой породой.");
		L.Theme = EFVTheme::Cave;
		L.UnlockPrice = 0;            // opened by breaking the rubble with a pickaxe (§9а)
		L.MinLevel = 12;
		L.MapPos = FVector2D(826.f, 168.f);
		L.JunkChance = 0.08f;
		L.TreasureChance = 0.16f;
		L.WaterShallow = FColor(58, 110, 128);
		L.WaterDeep = FColor(12, 30, 48);
		L.bExpertZone = true;
		L.bBigWorld = true;
	}
}

// ================================================================================================ ordinary fish
void FVDBU6::AppendFish(TArray<FFVFishDef>& Fish)
{
	using namespace FVDBU6ContentLocal;
	using S = EFVFishShape;
	using P = EFVFishPattern;
	using M = EFVMotion;
	using R = EFVRarity;
	using W = EFVWeatherReq;
	const uint8 Any = FVTime::Any;
	const uint8 Mor = FVTime::Morning, Day = FVTime::Day, Eve = FVTime::Evening, Ngt = FVTime::Night;

	// ---------------------------------------------------------------- §3а: +1 ordinary species per classic location (Р39)
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_rudd"), TEXT("Краснопёрка"), TEXT("pond"), R::Common, 35, 22, M::Mixed, 10, 30, 20,
			TEXT("Золотистая, с ярко-красными плавниками. Греется у кувшинок в тёплый полдень."));
		FishWhen(F, Day, W::Any, 1, 0.f);
		FishLook(F, S::Carp, P::Plain, FColor(184, 164, 88), FColor(244, 230, 170), FColor(226, 62, 46), FColor(128, 114, 60));
	}
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_asp"), TEXT("Жерех"), TEXT("river"), R::Uncommon, 120, 44, M::Dart, 30, 80, 12,
			TEXT("Речной хищник-торпеда. Бьёт по малькам с разгона — брызги летят до самого берега."));
		FishWhen(F, Mor | Day, W::Any, 1, 0.1f);
		FishLook(F, S::Pike, P::Plain, FColor(124, 144, 164), FColor(238, 242, 246), FColor(192, 112, 100), FColor(70, 90, 112));
	}
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_whitefish"), TEXT("Сиг"), TEXT("mountain"), R::Uncommon, 180, 48, M::Smooth, 25, 60, 13,
			TEXT("Серебристый житель холодных глубин. Подходит к берегу только на утренней заре."));
		FishWhen(F, Mor, W::Any, 1, 0.2f);
		FishLook(F, S::Minnow, P::Plain, FColor(172, 192, 204), FColor(244, 246, 248), FColor(142, 162, 176), FColor(102, 122, 142));
	}
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_flying_fish"), TEXT("Летучая рыба"), TEXT("ocean"), R::Uncommon, 240, 58, M::Dart, 15, 40, 12,
			TEXT("Расправляет плавники-крылья и перелетает над волнами. Ловится только в ясный день."));
		FishWhen(F, Day, W::Sunny, 1, 0.f);
		FishLook(F, S::FlyingFish, P::Plain, FColor(62, 112, 192), FColor(232, 242, 250), FColor(146, 204, 242), FColor(32, 62, 122));
	}
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_bowfin"), TEXT("Амия"), TEXT("swamp"), R::Rare, 800, 80, M::Sinker, 40, 90, 7,
			TEXT("Живое ископаемое с длинным спинным плавником. Дышит воздухом и охотится только ночью."));
		FishWhen(F, Ngt, W::Any, 1, 0.2f);
		FishLook(F, S::Pike, P::Spots, FColor(94, 102, 62), FColor(192, 188, 132), FColor(72, 112, 72), FColor(32, 36, 22));
	}
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_basalt_carp"), TEXT("Базальтовый карп"), TEXT("volcano"), R::Uncommon, 900, 78, M::Mixed, 30, 70, 14,
			TEXT("Чешуя тёмная и твёрдая, как застывшая лава, а в щелях между пластинами тлеет огонь."));
		FishWhen(F, Any, W::Any, 1, 0.1f);
		FishLook(F, S::Carp, P::Scales, FColor(60, 56, 62), FColor(122, 102, 98), FColor(232, 92, 42), FColor(255, 142, 52));
	}

	// ---------------------------------------------------------------- Город, пирс (§8): Морской причал + 2 вида
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_horse_mackerel"), TEXT("Ставрида"), TEXT("city"), R::Common, 180, 52, M::Dart, 20, 45, 26,
			TEXT("Стайная рыба гавани с колючей «дорожкой» вдоль бока. Клюёт жадно — только успевай."));
		FishWhen(F, Any, W::Any, 1, 0.f);
		FishLook(F, S::Mackerel, P::Stripes, FColor(92, 142, 162), FColor(232, 238, 240), FColor(222, 202, 92), FColor(42, 82, 102));
	}
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_sea_burbot"), TEXT("Морской налим"), TEXT("city"), R::Rare, 650, 72, M::Sinker, 30, 80, 8,
			TEXT("Скользкий донный житель с усом на подбородке. В темноте выходит из-под свай пирса."));
		FishWhen(F, Eve | Ngt, W::Any, 1, 0.3f);
		FishLook(F, S::Catfish, P::Spots, FColor(112, 88, 62), FColor(212, 192, 152), FColor(92, 72, 52), FColor(62, 48, 32));
	}

	// ---------------------------------------------------------------- Пещера, подземное озеро (§9а): Горное озеро + 2 вида
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_blind_char"), TEXT("Слепой голец"), TEXT("cave"), R::Uncommon, 420, 62, M::Smooth, 20, 50, 14,
			TEXT("Бледный, почти прозрачный и без глаз. Находит наживку по малейшему движению воды."));
		FishWhen(F, Any, W::Any, 1, 0.2f);
		FishLook(F, S::Tuna, P::Plain, FColor(228, 206, 210), FColor(250, 242, 242), FColor(242, 172, 182), FColor(202, 172, 182));
	}
	{
		FFVFishDef& F = FishRow(Fish, TEXT("fish_cave_gudgeon"), TEXT("Пещерный пескарь"), TEXT("cave"), R::Common, 160, 45, M::Mixed, 8, 18, 28,
			TEXT("Усатая крошка подземного озера. Пятнышки на боках слабо светятся в темноте."));
		FishWhen(F, Any, W::Any, 1, 0.f);
		FishLook(F, S::Minnow, P::Spots, FColor(172, 178, 182), FColor(232, 234, 228), FColor(152, 162, 162), FColor(122, 222, 212));
	}

	// «Ихтиолог» is for the species of the classic locations (§3а / §14): counted here, used by AppendAchievements.
	int32 N = 0;
	for (const FFVFishDef& F : Fish)
	{
		if (!F.bExpert && !IsExpertLocation(F.Location) && !IsForestId(F.Location)) { ++N; }
	}
	GClassicSpecies = N;
}

// ================================================================================================ cosmetics
void FVDBU6::AppendCosmetics(TArray<FFVCosmeticDef>& Cosmetics)
{
	using namespace FVDBU6ContentLocal;
	using C = EFVCosmeticSlot;
	using H = EFVHatStyle;
	using O = EFVOutfitStyle;
	using B = EFVBobberStyle;
	using Pt = EFVPetStyle;

	// ---------------------------------------------------------------- Лесник Потапыч (§4), tr_forester
	CosRow(Cosmetics, TEXT("hat_pompom"), TEXT("Шапка с помпоном"), C::Hat, uint8(H::Pompom), 2200, 4,
		FColor(196, 52, 48), FColor(246, 240, 226), MaskForester, false,
		TEXT("Вязаная шапка с пушистым помпоном. Лесник уверяет, что в ней не мёрзнут даже уши."));
	CosRow(Cosmetics, TEXT("hat_ushanka_forest"), TEXT("Ушанка лесника"), C::Hat, uint8(H::Ushanka), 5000, 5,
		FColor(92, 104, 78), FColor(196, 178, 146), MaskForester, false,
		TEXT("Тёплая ушанка болотного цвета. Пахнет хвоей и костром."));
	CosRow(Cosmetics, TEXT("outfit_flannel_red"), TEXT("Клетчатая рубаха (красная)"), C::Outfit, uint8(O::Flannel), 2500, 4,
		FColor(186, 48, 44), FColor(40, 34, 38), MaskForester, false,
		TEXT("Плотная фланель в крупную красную клетку — классика лесоруба."));
	CosRow(Cosmetics, TEXT("outfit_flannel_green"), TEXT("Клетчатая рубаха (зелёная)"), C::Outfit, uint8(O::Flannel), 2500, 4,
		FColor(58, 116, 64), FColor(34, 42, 34), MaskForester, false,
		TEXT("Зелёная клетка под цвет Чащи. В ней вас не заметит ни одна белка."));
	CosRow(Cosmetics, TEXT("outfit_flannel_blue"), TEXT("Клетчатая рубаха (синяя)"), C::Outfit, uint8(O::Flannel), 2500, 4,
		FColor(52, 84, 152), FColor(30, 34, 52), MaskForester, false,
		TEXT("Синяя клетка для рыбака, который знает толк и в рубке."));
	CosRow(Cosmetics, TEXT("outfit_lumber_vest"), TEXT("Жилет лесоруба"), C::Outfit, uint8(O::LumberVest), 6000, 5,
		FColor(122, 84, 50), FColor(214, 96, 40), MaskForester, false,
		TEXT("Кожаный жилет с ярким воротом и петлёй для топорища."));
	CosRow(Cosmetics, TEXT("pet_squirrel"), TEXT("Белка Шишка"), C::Pet, uint8(Pt::Squirrel), 9000, 5,
		FColor(206, 104, 44), FColor(246, 226, 196), MaskForester, false,
		TEXT("Рыжая непоседа. Прячет орешки в ваш рюкзак — иногда вместе с блёснами."));
	CosRow(Cosmetics, TEXT("pet_hedgehog"), TEXT("Ёжик Колючка"), C::Pet, uint8(Pt::Hedgehog), 12000, 6,
		FColor(122, 100, 78), FColor(222, 196, 160), MaskForester, false,
		TEXT("Фыркает, топает и сворачивается клубком, если клюёт что-то крупное."));

	// ---------------------------------------------------------------- Бутик Жанны (§8, Р20, Р29), tr_boutique: 100 000 – 1 000 000
	CosRow(Cosmetics, TEXT("hat_captain_cap"), TEXT("Капитанская фуражка"), C::Hat, uint8(H::CaptainCap), 120000, 11,
		FColor(246, 246, 242), FColor(28, 38, 70), MaskBoutique, true,
		TEXT("Белоснежная фуражка с золотым якорем. Встречные чайки отдают честь."));
	CosRow(Cosmetics, TEXT("outfit_captain_coat"), TEXT("Капитанский китель"), C::Outfit, uint8(O::CaptainCoat), 180000, 11,
		FColor(30, 42, 78), FColor(236, 196, 72), MaskBoutique, true,
		TEXT("Двубортный китель с медными пуговицами. Сшит по мерке портовым портным."));
	CosRow(Cosmetics, TEXT("hat_gold"), TEXT("Золотая шляпа"), C::Hat, uint8(H::GoldHat), 250000, 12,
		FColor(250, 204, 62), FColor(255, 244, 182), MaskBoutique, true,
		TEXT("Сияет так, что рыба всплывает посмотреть. Главный предмет гордости бутика."));
	CosRow(Cosmetics, TEXT("pet_captain_cat"), TEXT("Кот-капитан"), C::Pet, uint8(Pt::CaptainCat), 300000, 12,
		FColor(62, 60, 70), FColor(246, 246, 240), MaskBoutique, true,
		TEXT("Важный кот в капитанской фуражке. Командует уловом и лично проверяет каждую рыбу."));
	CosRow(Cosmetics, TEXT("bobber_diamond"), TEXT("Бриллиантовый поплавок"), C::Bobber, uint8(B::Diamond), 350000, 12,
		FColor(206, 240, 255), FColor(255, 255, 255), MaskBoutique, true,
		TEXT("Гранёный, с радужными бликами. Рыба клюёт на него не лучше — но как красиво!"));
	CosRow(Cosmetics, TEXT("hat_bicorne"), TEXT("Адмиральская двууголка"), C::Hat, uint8(H::Bicorne), 450000, 13,
		FColor(34, 34, 46), FColor(236, 196, 72), MaskBoutique, true,
		TEXT("Двууголка с золотым галуном и кокардой. Для тех, кто командует флотом."));
	CosRow(Cosmetics, TEXT("outfit_admiral"), TEXT("Адмиральский мундир"), C::Outfit, uint8(O::AdmiralUniform), 600000, 14,
		FColor(26, 34, 66), FColor(246, 214, 96), MaskBoutique, true,
		TEXT("Эполеты, аксельбанты и ряд орденов «За доблестный улов»."));
	CosRow(Cosmetics, TEXT("pet_peacock"), TEXT("Павлин"), C::Pet, uint8(Pt::Peacock), 1000000, 15,
		FColor(40, 110, 170), FColor(90, 180, 110), MaskBoutique, true,
		TEXT("Раскрывает хвост при каждой поклёвке. Самая дорогая вещь в Городе."));

	// ---------------------------------------------------------------- бутик: коллекция «Эксперт» (Р64) — продаётся только после
	// нужного экспертного достижения (FFVCosmeticDef::RequiredAch; до него видна в лавке закрытой с условием)
	CosRow(Cosmetics, TEXT("bobber_helm"), TEXT("Штурвал"), C::Bobber, uint8(B::Helm), 200000, 11,
		FColor(150, 92, 50), FColor(236, 196, 72), MaskBoutique, true,
		TEXT("Резной штурвал красного дерева с медной ступицей. Город доверяет его только почётным горожанам."))
		.RequiredAch = FName(TEXT("ach_city_tasks_50"));
	CosRow(Cosmetics, TEXT("bobber_dragon"), TEXT("Драконий глаз"), C::Bobber, uint8(B::Dragon), 300000, 11,
		FColor(200, 44, 36), FColor(255, 200, 60), MaskBoutique, true,
		TEXT("Алая чешуя, рожки и горящий золотой глаз. Для тех, кто укротил пламя вулкана."))
		.RequiredAch = FName(TEXT("ach_legend_volcano"));
	CosRow(Cosmetics, TEXT("hat_marlin_helm"), TEXT("Шлем марлина"), C::Hat, uint8(H::MarlinHelm), 350000, 11,
		FColor(40, 80, 170), FColor(150, 220, 255), MaskBoutique, true,
		TEXT("Синий шлем с копьём-носом и парусным гребнем. Носят лишь те, кто собрал весь элитный улов."))
		.RequiredAch = FName(TEXT("ach_elite_catch"));
	CosRow(Cosmetics, TEXT("hat_dragon_helm"), TEXT("Драконий шлем"), C::Hat, uint8(H::DragonHelm), 450000, 11,
		FColor(176, 40, 40), FColor(250, 206, 80), MaskBoutique, true,
		TEXT("Чешуйчатый шлем с золотыми рогами и гребнем. Трофей охотника за легендами."))
		.RequiredAch = FName(TEXT("ach_legends"));
	CosRow(Cosmetics, TEXT("outfit_rodmaster"), TEXT("Мундир мастера снастей"), C::Outfit, uint8(O::RodMaster), 550000, 11,
		FColor(34, 58, 58), FColor(236, 196, 72), MaskBoutique, true,
		TEXT("На груди — скрещённые удочки всех типов снастей. Шит серебром и золотом."))
		.RequiredAch = FName(TEXT("ach_all_rod_types"));
	CosRow(Cosmetics, TEXT("outfit_manta"), TEXT("Плащ-скат"), C::Outfit, uint8(O::MantaCape), 700000, 11,
		FColor(34, 44, 90), FColor(210, 230, 246), MaskBoutique, true,
		TEXT("Широкий плащ-крылья цвета ночного моря с узором ската. Для полусотни пойманных легенд."))
		.RequiredAch = FName(TEXT("ach_legends_50"));
	CosRow(Cosmetics, TEXT("pet_dragonet"), TEXT("Дракончик Уголёк"), C::Pet, uint8(Pt::Dragonet), 1000000, 11,
		FColor(196, 52, 40), FColor(255, 206, 90), MaskBoutique, true,
		TEXT("Машет крылышками и пускает искры, когда клюёт. Спутник гроссмейстера рыбалки."))
		.RequiredAch = FName(TEXT("ach_level_15"));

	// ---------------------------------------------------------------- rewards (Price 0: never sold; §13 «Легендарные» and more)
	CosRow(Cosmetics, TEXT("bobber_anchor"), TEXT("Якорь"), C::Bobber, uint8(B::Anchor), 0, 1,
		FColor(70, 78, 92), FColor(236, 196, 72), 0, false,
		TEXT("Маленький чугунный якорь на красном шнурке. Награда за починку лодки."));
	CosRow(Cosmetics, TEXT("hat_master_crown"), TEXT("Корона хозяина долины"), C::Hat, uint8(H::MasterCrown), 0, 1,
		FColor(250, 206, 70), FColor(40, 170, 120), 0, false,
		TEXT("Корона из шести зубцов — по одному за каждую легенду долины. Награда «Хозяина долины»."));
	CosRow(Cosmetics, TEXT("outfit_legend_cloak"), TEXT("Плащ легенд"), C::Outfit, uint8(O::LegendCloak), 0, 1,
		FColor(92, 40, 120), FColor(250, 206, 80), 0, false,
		TEXT("Расшит силуэтами пойманных гигантов. Награда за сотню легенд."));
	CosRow(Cosmetics, TEXT("bobber_marlin"), TEXT("Марлин"), C::Bobber, uint8(B::Marlin), 0, 1,
		FColor(42, 72, 150), FColor(160, 220, 250), 0, false,
		TEXT("Синий поплавок с копьём-носом. Награда за безупречный бой с экспертной легендой."));
	CosRow(Cosmetics, TEXT("bobber_kraken"), TEXT("Кракен"), C::Bobber, uint8(B::Kraken), 0, 1,
		FColor(122, 40, 70), FColor(236, 150, 170), 0, false,
		TEXT("Щупальца обнимают поплавок со всех сторон. Награда «Повелителя глубин»."));
	CosRow(Cosmetics, TEXT("bobber_aurora"), TEXT("Северное сияние"), C::Bobber, uint8(B::Aurora), 0, 1,
		FColor(80, 220, 170), FColor(170, 110, 240), 0, false,
		TEXT("По стеклу бегут зелёные и лиловые сполохи. Награда за иридиевую легенду."));
}

// ================================================================================================ decor
void FVDBU6::AppendDecor(TArray<FFVDecorDef>& Decor)
{
	using namespace FVDBU6ContentLocal;
	using S = EFVDecorSlot;

	// ---------------------------------------------------------------- двор, Прораб Гаврила (§4, §6), tr_builder
	// Р74 (owner, after 0.6): the yard decor was far too cheap next to the furniture — every price x10 (10 000 – 90 000)
	DecorRow(Decor, TEXT("decor_yard_roof_tile"), TEXT("Черепичная крыша"), S::YardRoof, 0, 30000, 4,
		FColor(176, 72, 52), FColor(120, 46, 36), MaskBuilder, TEXT("Ровные ряды красной черепицы. Дождь по ней стучит особенно уютно."));
	DecorRow(Decor, TEXT("decor_yard_roof_copper"), TEXT("Медная крыша"), S::YardRoof, 1, 90000, 6,
		FColor(80, 156, 130), FColor(184, 112, 64), MaskBuilder, TEXT("Благородная медь, позеленевшая от времени и морского ветра."));
	DecorRow(Decor, TEXT("decor_yard_walls_paint"), TEXT("Крашеные брёвна"), S::YardWalls, 0, 25000, 4,
		FColor(196, 150, 80), FColor(150, 100, 56), MaskBuilder, TEXT("Брёвна, покрытые медовой олифой. Дом словно светится."));
	DecorRow(Decor, TEXT("decor_yard_walls_white"), TEXT("Белёные стены"), S::YardWalls, 1, 60000, 5,
		FColor(238, 234, 222), FColor(150, 120, 90), MaskBuilder, TEXT("Стены под известью, с тёмными балками, как в южной мазанке."));
	DecorRow(Decor, TEXT("decor_yard_shutters_carved"), TEXT("Резные ставни"), S::YardShutters, 0, 20000, 4,
		FColor(236, 226, 200), FColor(62, 118, 162), MaskBuilder, TEXT("Наличники с кружевной резьбой — работа старого мастера."));
	DecorRow(Decor, TEXT("decor_yard_shutters_blue"), TEXT("Голубые ставни"), S::YardShutters, 1, 12000, 4,
		FColor(96, 156, 206), FColor(240, 240, 232), MaskBuilder, TEXT("Простые ставни, выкрашенные в небесный цвет."));
	DecorRow(Decor, TEXT("decor_yard_fence_picket"), TEXT("Штакетник"), S::YardFence, 0, 15000, 4,
		FColor(236, 232, 220), FColor(170, 160, 140), MaskBuilder, TEXT("Аккуратный белый штакетник с острыми верхушками."));
	DecorRow(Decor, TEXT("decor_yard_fence_wattle"), TEXT("Плетень"), S::YardFence, 1, 12000, 4,
		FColor(150, 116, 70), FColor(104, 78, 46), MaskBuilder, TEXT("Плетёная изгородь из ивовых прутьев. На кольях сушатся кринки."));
	DecorRow(Decor, TEXT("decor_yard_fence_stone"), TEXT("Каменная ограда"), S::YardFence, 2, 70000, 7,
		FColor(150, 146, 140), FColor(96, 92, 88), MaskBuilder, TEXT("Невысокая ограда из дикого камня, поросшая мхом."));
	DecorRow(Decor, TEXT("decor_yard_path_planks"), TEXT("Дощатая дорожка"), S::YardPath, 0, 10000, 4,
		FColor(170, 124, 76), FColor(120, 84, 50), MaskBuilder, TEXT("Дорожка из широких досок — ноги не промокнут."));
	DecorRow(Decor, TEXT("decor_yard_path_stone"), TEXT("Каменная дорожка"), S::YardPath, 1, 40000, 6,
		FColor(172, 168, 158), FColor(110, 106, 98), MaskBuilder, TEXT("Плоские речные камни, уложенные один к одному."));
	DecorRow(Decor, TEXT("decor_yard_lantern"), TEXT("Садовые фонари"), S::YardLantern, 0, 35000, 5,
		FColor(60, 56, 52), FColor(255, 206, 110), MaskBuilder, TEXT("Кованые фонари вдоль дорожки. Вечером двор как на открытке."));
	DecorRow(Decor, TEXT("decor_yard_mailbox"), TEXT("Почтовый ящик"), S::YardMailbox, 0, 12000, 4,
		FColor(62, 110, 170), FColor(226, 200, 90), MaskBuilder, TEXT("Синий ящик на столбике. Письма от Митрича теперь не промокнут."));
	DecorRow(Decor, TEXT("decor_yard_bench"), TEXT("Резная скамейка"), S::YardBench, 0, 20000, 4,
		FColor(160, 110, 64), FColor(110, 74, 42), MaskBuilder, TEXT("Скамейка у крыльца — посидеть после рыбалки."));
	DecorRow(Decor, TEXT("decor_yard_flowerbed"), TEXT("Клумба"), S::YardFlowerbed, 0, 18000, 4,
		FColor(226, 80, 110), FColor(250, 210, 70), MaskBuilder, TEXT("Бархатцы, мальвы и астры в обрамлении камней."));
	DecorRow(Decor, TEXT("decor_yard_scarecrow"), TEXT("Пугало"), S::YardScarecrow, 0, 15000, 4,
		FColor(206, 170, 90), FColor(170, 60, 50), MaskBuilder, TEXT("Соломенный сторож в дедовой шляпе. Вороны обходят двор стороной."));
	DecorRow(Decor, TEXT("decor_yard_well"), TEXT("Колодец"), S::YardWell, 0, 60000, 5,
		FColor(140, 136, 128), FColor(130, 90, 54), MaskBuilder, TEXT("Бревенчатый сруб с воротом и ведром. Вода ледяная и вкусная."));
	DecorRow(Decor, TEXT("decor_yard_vane"), TEXT("Флюгер-петушок"), S::YardVane, 0, 25000, 4,
		FColor(70, 66, 62), FColor(230, 180, 60), MaskBuilder, TEXT("Жестяной петушок на коньке: всегда знает, откуда ветер."));
	DecorRow(Decor, TEXT("decor_yard_woodpile"), TEXT("Поленница"), S::YardCenter, 0, 15000, 4,
		FColor(170, 120, 72), FColor(222, 190, 140), MaskBuilder, TEXT("Ровная поленница берёзовых дров. Хозяйство налицо."));

	// ---------------------------------------------------------------- мебель дома, Прораб Гаврила (§6.4, Р37): 50 000 – 500 000
	DecorRow(Decor, TEXT("decor_furn_bookcase"), TEXT("Книжный шкаф"), S::Bookcase, 0, 60000, 7,
		FColor(122, 78, 44), FColor(176, 60, 50), MaskBuilder, TEXT("Дубовый шкаф с лоциями, рыбацкими справочниками и старыми журналами."));
	DecorRow(Decor, TEXT("decor_furn_cupboard"), TEXT("Резной буфет"), S::Cupboard, 0, 80000, 8,
		FColor(140, 90, 52), FColor(220, 226, 236), MaskBuilder, TEXT("Буфет с резными дверцами и стеклянными витринами для фарфора."));
	DecorRow(Decor, TEXT("decor_furn_clock"), TEXT("Напольные часы"), S::Clock, 0, 90000, 8,
		FColor(110, 66, 38), FColor(226, 196, 110), MaskBuilder, TEXT("Высокие часы с маятником. Бьют каждый час — и на утренний клёв."));
	DecorRow(Decor, TEXT("decor_furn_chandelier"), TEXT("Бронзовая люстра"), S::Chandelier, 0, 120000, 9,
		FColor(176, 124, 62), FColor(255, 214, 130), MaskBuilder, TEXT("Люстра на шесть свечей с бронзовыми рыбками на рожках."));
	DecorRow(Decor, TEXT("decor_furn_fireplace"), TEXT("Камин"), S::Fireplace, 0, 150000, 9,
		FColor(160, 150, 140), FColor(255, 140, 50), MaskBuilder, TEXT("Камин из тёсаного камня. Над ним так и просится трофей."));
	DecorRow(Decor, TEXT("decor_furn_tiled_stove"), TEXT("Изразцовая печь"), S::StoveSkin, 0, 180000, 9,
		FColor(236, 236, 226), FColor(52, 96, 170), MaskBuilder, TEXT("Печь в бело-синих изразцах с рыбками и парусниками."));
	DecorRow(Decor, TEXT("decor_furn_stuffed_carp"), TEXT("Чучело Царь-Карпа"), S::Statue, 0, 200000, 10,
		FColor(242, 188, 40), FColor(120, 80, 46), MaskBuilder, TEXT("Золотой гигант на дубовой подставке. Гости не верят своим глазам."));
	DecorRow(Decor, TEXT("decor_furn_canopy_bed"), TEXT("Кровать с балдахином"), S::BedSkin, 0, 250000, 10,
		FColor(130, 40, 60), FColor(236, 206, 120), MaskBuilder, TEXT("Резная кровать под бархатным пологом. Сны о клёве гарантированы."));
	DecorRow(Decor, TEXT("decor_furn_crystal_chandelier"), TEXT("Хрустальная люстра"), S::Chandelier, 1, 350000, 12,
		FColor(210, 236, 250), FColor(255, 246, 210), MaskBuilder, TEXT("Сотни хрустальных подвесок, как капли воды на солнце."));
	DecorRow(Decor, TEXT("decor_furn_aquarium_column"), TEXT("Аквариум-колонна"), S::Statue, 1, 500000, 13,
		FColor(90, 180, 220), FColor(200, 170, 90), MaskBuilder, TEXT("Стеклянная колонна от пола до потолка, в ней кружат пёстрые рыбки."));

	// ---------------------------------------------------------------- бутик Жанны (§8): украшения двора и дома, tr_boutique
	DecorRow(Decor, TEXT("decor_yard_lighthouse_vane"), TEXT("Маяк-флюгер"), S::YardVane, 1, 150000, 11,
		FColor(236, 236, 230), FColor(200, 50, 50), MaskBoutique, TEXT("Маленький маяк на коньке крыши: ночью мигает огоньком."));
	DecorRow(Decor, TEXT("decor_yard_fish_statue"), TEXT("Статуя рыбы"), S::YardCenter, 2, 250000, 11,
		FColor(150, 170, 180), FColor(90, 110, 120), MaskBoutique, TEXT("Мраморная рыба, выпрыгивающая из волны. Работа столичного скульптора."));
	DecorRow(Decor, TEXT("decor_furn_gilded_clock"), TEXT("Золочёные часы"), S::Clock, 1, 300000, 12,
		FColor(236, 190, 70), FColor(40, 30, 40), MaskBoutique, TEXT("Часы в позолоченном корпусе; в полдень из дверцы выплывает рыбка."));
	DecorRow(Decor, TEXT("decor_yard_fountain"), TEXT("Фонтан"), S::YardCenter, 1, 400000, 12,
		FColor(200, 196, 186), FColor(110, 190, 230), MaskBoutique, TEXT("Трёхъярусный фонтан с дельфинами. Журчит днём и ночью."));

	// ---------------------------------------------------------------- reward (ach_millionaire)
	DecorRow(Decor, TEXT("decor_yard_gold_statue"), TEXT("Золотой рыбак"), S::YardCenter, 3, 0, 1,
		FColor(250, 204, 62), FColor(160, 110, 40), 0, TEXT("Статуя рыбака с уловом, отлитая из чистого золота. Награда «Миллионера»."));
}

// ================================================================================================ achievements
void FVDBU6::AppendAchievements(TArray<FFVAchievementDef>& Achievements)
{
	using namespace FVDBU6ContentLocal;
	using K = EFVAchKind;
	using T = EFVAchTier;

	// ---------------------------------------------------------------- tiers of the existing achievements (+ two fixes)
	for (FFVAchievementDef& A : Achievements)
	{
		A.Tier = uint8(T::Normal);
		for (const FTierRow& Row : ClassicTiers)
		{
			if (A.Id == FName(Row.Id)) { A.Tier = uint8(Row.Tier); break; }
		}
		// «Мастер-рыболов» stays the last classic level (§9: FV::MaxLevel is 15 now)
		if (A.Id == FName(TEXT("ach_level_10")))
		{
			A.Target = FV::ClassicMaxLevel;
			A.Description = FString::Printf(TEXT("Достигните %d-го уровня рыбалки."), FV::ClassicMaxLevel);
		}
		// «Ихтиолог»: every species of the six classic locations (§3а: now 46), not the expert ones (§14)
		if (A.Id == FName(TEXT("ach_species_all")) && GClassicSpecies > 0)
		{
			A.Target = GClassicSpecies;
			A.Description = FString::Printf(TEXT("Соберите полную коллекцию долины: %d видов рыб."), GClassicSpecies);
		}
	}

	// ---------------------------------------------------------------- Обычные
	AchRow(Achievements, TEXT("ach_captain"), TEXT("Капитан"), K::BoatRepaired, 1, T::Normal, 3000, TEXT("bobber_anchor"), nullptr,
		TEXT("Почините лодку на Морском причале. Награда — поплавок «Якорь»!"));
	AchRow(Achievements, TEXT("ach_first_tree"), TEXT("Первое бревно"), K::TreesChopped, 1, T::Normal, 300, nullptr, nullptr,
		TEXT("Срубите первое дерево в лесу."));
	AchRow(Achievements, TEXT("ach_first_harvest"), TEXT("Первый урожай"), K::Harvests, 1, T::Normal, 300, nullptr, nullptr,
		TEXT("Соберите первый урожай на огороде."));

	// ---------------------------------------------------------------- Трудные
	AchRow(Achievements, TEXT("ach_expert_zone"), TEXT("Экспертная зона"), K::ReachLevel, FV::ClassicMaxLevel + 1, T::Hard, 5000, nullptr, nullptr,
		*FString::Printf(TEXT("Доплывите до Города и достигните %d-го уровня."), FV::ClassicMaxLevel + 1));
	AchRow(Achievements, TEXT("ach_expert_first"), TEXT("Элитный трофей"), K::ExpertSpecies, 1, T::Hard, 3000, nullptr, nullptr,
		TEXT("Поймайте первую экспертную рыбу."));
	AchRow(Achievements, TEXT("ach_legends_25"), TEXT("Легендарный улов I"), K::CatchLegendary, 25, T::Hard, 15000, nullptr, nullptr,
		TEXT("Поймайте 25 легенд."));
	AchRow(Achievements, TEXT("ach_lumberjack"), TEXT("Лесоруб"), K::TreesChopped, 200, T::Hard, 10000, nullptr, nullptr,
		TEXT("Срубите 200 деревьев."));
	AchRow(Achievements, TEXT("ach_gardener"), TEXT("Огородник"), K::Harvests, 100, T::Hard, 10000, nullptr, nullptr,
		TEXT("Соберите 100 урожаев на огороде."));
	AchRow(Achievements, TEXT("ach_smith"), TEXT("Кузнец"), K::Forgings, 50, T::Hard, 10000, nullptr, nullptr,
		TEXT("Выкуйте 50 изделий в кузне."));
	AchRow(Achievements, TEXT("ach_city_tasks"), TEXT("Друг Города"), K::CityTasksDone, 10, T::Hard, 8000, nullptr, nullptr,
		TEXT("Выполните 10 заданий Города."));

	// ---------------------------------------------------------------- Экспертные
	AchRow(Achievements, TEXT("ach_level_15"), TEXT("Гроссмейстер"), K::ReachLevel, FV::ExpertMaxLevel, T::Expert, 50000, nullptr, nullptr,
		*FString::Printf(TEXT("Достигните %d-го уровня рыбалки."), FV::ExpertMaxLevel));
	AchRow(Achievements, TEXT("ach_legends_50"), TEXT("Легендарный улов II"), K::CatchLegendary, 50, T::Expert, 40000, nullptr, nullptr,
		TEXT("Поймайте 50 легенд."));
	AchRow(Achievements, TEXT("ach_elite_catch"), TEXT("Элитный улов"), K::ExpertSpecies, 17, T::Expert, 60000, nullptr, nullptr,
		TEXT("Поймайте все 17 видов экспертных рыб."));
	AchRow(Achievements, TEXT("ach_all_rod_types"), TEXT("Мастер всех снастей"), K::ExpertRodTypes, int32(EFVExpertRodType::Count), T::Expert, 40000, nullptr, nullptr,
		*FString::Printf(TEXT("Поймайте экспертную рыбу удочкой каждого из %d типов."), int32(EFVExpertRodType::Count)));
	AchRow(Achievements, TEXT("ach_city_tasks_50"), TEXT("Почётный горожанин"), K::CityTasksDone, 50, T::Expert, 40000, nullptr, nullptr,
		TEXT("Выполните 50 заданий Города."));
	AchRow(Achievements, TEXT("ach_facts"), TEXT("Знаток повадок"), K::FactsKnown, 25, T::Expert, 15000, nullptr, nullptr,
		TEXT("Узнайте 25 сведений о рыбах."));
	AchRow(Achievements, TEXT("ach_facts_60"), TEXT("Живая энциклопедия"), K::FactsKnown, 60, T::Expert, 50000, nullptr, nullptr,
		TEXT("Узнайте 60 сведений о рыбах."));

	// ---------------------------------------------------------------- Легендарные (уникальная косметика)
	AchRow(Achievements, TEXT("ach_valley_master"), TEXT("Хозяин долины"), K::LegendsClassicAll, 1, T::Legendary, 50000, TEXT("hat_master_crown"), nullptr,
		TEXT("Поймайте всех семь легенд долины (и Хозяина омута у Старой мельницы). Награда — корона хозяина долины!"));
	AchRow(Achievements, TEXT("ach_legends_100"), TEXT("Легендарный улов III"), K::CatchLegendary, 100, T::Legendary, 100000, TEXT("outfit_legend_cloak"), nullptr,
		TEXT("Поймайте 100 легенд. Награда — плащ легенд!"));
	AchRow(Achievements, TEXT("ach_depths_lord"), TEXT("Повелитель глубин"), K::LegendsExpertAll, 1, T::Legendary, 200000, TEXT("bobber_kraken"), nullptr,
		TEXT("Поймайте всех шесть экспертных легенд. Награда — поплавок «Кракен»!"));
	AchRow(Achievements, TEXT("ach_iridium_legend"), TEXT("Иридиевая легенда"), K::IridiumLegend, 1, T::Legendary, 100000, TEXT("bobber_aurora"), nullptr,
		TEXT("Поймайте легенду иридиевого качества. Награда — поплавок «Северное сияние»!"));
	AchRow(Achievements, TEXT("ach_flawless"), TEXT("Без единой ошибки"), K::FlawlessExpertLegend, 1, T::Legendary, 150000, TEXT("bobber_marlin"), nullptr,
		TEXT("Поймайте экспертную легенду, ни разу не упустив её из зоны. Награда — поплавок «Марлин»!"));
	AchRow(Achievements, TEXT("ach_millionaire"), TEXT("Миллионер"), K::BoutiqueSpent, 1000000, T::Legendary, 100000, nullptr, TEXT("decor_yard_gold_statue"),
		TEXT("Потратьте 1 000 000 монет в бутике. Награда — статуя «Золотой рыбак» для двора!"));
}

// ================================================================================================ villagers
const TArray<FFVNpcDef>& FVDBU6::Npcs()
{
	static const TArray<FFVNpcDef> A = FVDBU6ContentLocal::BuildNpcTable();
	return A;
}

const FFVNpcDef* FVDBU6::FindNpc(FName Id)
{
	for (const FFVNpcDef& N : Npcs())
	{
		if (N.Id == Id) { return &N; }
	}
	return nullptr;
}

TArray<const FFVNpcDef*> FVDBU6::NpcsAt(FName LocationId)
{
	TArray<const FFVNpcDef*> Out;
	for (const FFVNpcDef& N : Npcs())
	{
		if (N.Location == LocationId) { Out.Add(&N); }
	}
	return Out;
}

FString FVDBU6::AchTierName(EFVAchTier T)
{
	switch (T)
	{
	case EFVAchTier::Normal: return TEXT("Обычные");
	case EFVAchTier::Hard: return TEXT("Трудные");
	case EFVAchTier::Expert: return TEXT("Экспертные");
	case EFVAchTier::Legendary: return TEXT("Легендарные");
	default: return FString();
	}
}

FLinearColor FVDBU6::AchTierColor(EFVAchTier T)
{
	switch (T)
	{
	case EFVAchTier::Hard: return FV::Col(90, 150, 220);
	case EFVAchTier::Expert: return FV::Col(170, 90, 210);
	case EFVAchTier::Legendary: return FV::Col(240, 190, 60);
	default: return FV::Col(150, 120, 90);
	}
}

FString FVDBU6::DecorSlotNameU6(EFVDecorSlot Slot)
{
	switch (Slot)
	{
	case EFVDecorSlot::YardRoof: return TEXT("Крыша");
	case EFVDecorSlot::YardWalls: return TEXT("Стены дома");
	case EFVDecorSlot::YardShutters: return TEXT("Ставни");
	case EFVDecorSlot::YardFence: return TEXT("Забор");
	case EFVDecorSlot::YardPath: return TEXT("Дорожка");
	case EFVDecorSlot::YardLantern: return TEXT("Фонари");
	case EFVDecorSlot::YardMailbox: return TEXT("Почтовый ящик");
	case EFVDecorSlot::YardBench: return TEXT("Скамейка");
	case EFVDecorSlot::YardFlowerbed: return TEXT("Клумба");
	case EFVDecorSlot::YardScarecrow: return TEXT("Пугало");
	case EFVDecorSlot::YardWell: return TEXT("Колодец");
	case EFVDecorSlot::YardVane: return TEXT("Флюгер");
	case EFVDecorSlot::YardCenter: return TEXT("Украшение двора");
	case EFVDecorSlot::Cupboard: return TEXT("Буфет");
	case EFVDecorSlot::Bookcase: return TEXT("Книжный шкаф");
	case EFVDecorSlot::Clock: return TEXT("Часы");
	case EFVDecorSlot::Fireplace: return TEXT("Камин");
	case EFVDecorSlot::Chandelier: return TEXT("Люстра");
	case EFVDecorSlot::Statue: return TEXT("Чучело и диковины");
	case EFVDecorSlot::StoveSkin: return TEXT("Печь");
	case EFVDecorSlot::BedSkin: return TEXT("Кровать");
	default: return FString();
	}
}
