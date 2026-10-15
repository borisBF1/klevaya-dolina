// Static game content & balance formulas for "Клёвая Долина".
// All tables are built lazily on first access (FVDB::Init() just forces it) and never change afterwards,
// so pointers into them stay valid for the program lifetime.

#include "FVDatabase.h"
#include "Algo/StableSort.h"

namespace
{
	struct FFVDatabaseData
	{
		bool bBuilt = false;

		TArray<FFVLocationDef> Locations;
		TArray<FFVFishDef> Fish;
		TArray<FFVJunkDef> Junk;
		TArray<FFVRodDef> Rods;
		TArray<FFVBaitDef> Baits;
		TArray<FFVCosmeticDef> Cosmetics;
		TArray<FFVAchievementDef> Achievements;
		TArray<FFVKeepnetLevel> Keepnet;

		TMap<FName, int32> LocationIdx, FishIdx, JunkIdx, RodIdx, BaitIdx, CosmeticIdx, AchievementIdx;

		TArray<FName> StartCosmetics;       // owned & equipped on a new game
		TArray<FName> TreasureCosmetics;    // reward-only items that drop from treasure chests
	};

	// ============================================================================================
	// Builders
	// ============================================================================================
	void AddLocation(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVTheme Theme, int32 Price, int32 MinLevel,
		FVector2D MapPos, float JunkChance, float TreasureChance, FColor Shallow, FColor Deep, const TCHAR* Desc)
	{
		FFVLocationDef& L = D.Locations.AddDefaulted_GetRef();
		L.Id = FName(Id);
		L.Name = Name;
		L.Description = Desc;
		L.Theme = Theme;
		L.UnlockPrice = Price;
		L.MinLevel = MinLevel;
		L.MapPos = MapPos;
		L.JunkChance = JunkChance;
		L.TreasureChance = TreasureChance;
		L.WaterShallow = Shallow;
		L.WaterDeep = Deep;
	}

	FFVFishDef& AddFish(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, const TCHAR* Location, EFVRarity Rarity,
		int32 Price, int32 Difficulty, EFVMotion Motion, float MinSize, float MaxSize, float Weight, const TCHAR* Desc)
	{
		FFVFishDef& F = D.Fish.AddDefaulted_GetRef();
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
		F.bLegendary = (Rarity == EFVRarity::Legendary);
		return F;
	}

	/** Time of day / weather / level / depth requirements. */
	void When(FFVFishDef& F, uint8 TimeMask, EFVWeatherReq Weather, int32 MinLevel, float MinDepth)
	{
		F.TimeMask = TimeMask;
		F.Weather = Weather;
		F.MinLevel = MinLevel;
		F.MinDepth = MinDepth;
	}

	/** Procedural sprite description. */
	void Look(FFVFishDef& F, EFVFishShape Shape, EFVFishPattern Pattern, FColor Body, FColor Belly, FColor Fin, FColor PatternColor)
	{
		F.Shape = Shape;
		F.Pattern = Pattern;
		F.BodyColor = Body;
		F.BellyColor = Belly;
		F.FinColor = Fin;
		F.PatternColor = PatternColor;
	}

	/** Update 5: makes the fish one of the six legends (fight rules, silhouette) with its «Подготовка» / «Приёмы» texts.
	    The tackle rules and the toast / snap texts are set on the returned FFVLegendPrep (Docs/UPDATE5_LEGENDS.md §3). */
	FFVLegendPrep& Legend(FFVFishDef& F, EFVLegend L, const TCHAR* PrepText, const TCHAR* FightText)
	{
		F.Legend = L;
		F.Prep.PrepText = PrepText;
		F.Prep.FightText = FightText;
		return F.Prep;
	}

	void AddJunk(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVJunkStyle Style, int32 Price, const TCHAR* Desc)
	{
		FFVJunkDef& J = D.Junk.AddDefaulted_GetRef();
		J.Id = FName(Id);
		J.Name = Name;
		J.Description = Desc;
		J.Style = Style;
		J.Price = Price;
	}

	void AddRod(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, int32 Tier, int32 Price, int32 MinLevel,
		float Bar, float Reel, float Drain, float Bite, float Range, float Luck, float Treasure,
		FColor RodColor, FColor Accent, const TCHAR* Desc, uint8 TraderMask)
	{
		FFVRodDef& R = D.Rods.AddDefaulted_GetRef();
		R.Id = FName(Id);
		R.Name = Name;
		R.Description = Desc;
		R.Tier = Tier;
		R.Price = Price;
		R.MinLevel = MinLevel;
		R.BarBonus = Bar;
		R.ReelMult = Reel;
		R.DrainMult = Drain;
		R.BiteSpeed = Bite;
		R.CastRange = Range;
		R.Luck = Luck;
		R.TreasureBonus = Treasure;
		R.RodColor = RodColor;
		R.AccentColor = Accent;
		R.TraderMask = TraderMask;
	}

	FFVBaitDef& AddBait(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVBaitStyle Style, int32 PackPrice, int32 PackSize,
		int32 MinLevel, float Bite, float Rare, float JunkMult, float Size, const TCHAR* Desc, uint8 TraderMask)
	{
		FFVBaitDef& B = D.Baits.AddDefaulted_GetRef();
		B.Id = FName(Id);
		B.Name = Name;
		B.Description = Desc;
		B.Style = Style;
		B.PackPrice = PackPrice;
		B.PackSize = PackSize;
		B.MinLevel = MinLevel;
		B.BiteSpeed = Bite;
		B.RareBonus = Rare;
		B.JunkMult = JunkMult;
		B.SizeBonus = Size;
		B.TraderMask = TraderMask;
		return B;
	}

	void AddCosmetic(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVCosmeticSlot Slot, uint8 Style,
		int32 Price, int32 MinLevel, FColor Primary, FColor Secondary, const TCHAR* Desc)
	{
		FFVCosmeticDef& C = D.Cosmetics.AddDefaulted_GetRef();
		C.Id = FName(Id);
		C.Name = Name;
		C.Description = Desc;
		C.Slot = Slot;
		C.Style = Style;
		C.Price = Price;
		C.MinLevel = MinLevel;
		C.Primary = Primary;
		C.Secondary = Secondary;
	}

	void AddHat(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVHatStyle Style, int32 Price, int32 MinLevel, FColor P, FColor S, const TCHAR* Desc)
	{
		AddCosmetic(D, Id, Name, EFVCosmeticSlot::Hat, uint8(Style), Price, MinLevel, P, S, Desc);
	}
	void AddOutfit(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVOutfitStyle Style, int32 Price, int32 MinLevel, FColor P, FColor S, const TCHAR* Desc)
	{
		AddCosmetic(D, Id, Name, EFVCosmeticSlot::Outfit, uint8(Style), Price, MinLevel, P, S, Desc);
	}
	void AddBobber(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVBobberStyle Style, int32 Price, int32 MinLevel, FColor P, FColor S, const TCHAR* Desc)
	{
		AddCosmetic(D, Id, Name, EFVCosmeticSlot::Bobber, uint8(Style), Price, MinLevel, P, S, Desc);
	}
	void AddPet(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVPetStyle Style, int32 Price, int32 MinLevel, FColor P, FColor S, const TCHAR* Desc)
	{
		AddCosmetic(D, Id, Name, EFVCosmeticSlot::Pet, uint8(Style), Price, MinLevel, P, S, Desc);
	}

	void AddAch(FFVDatabaseData& D, const TCHAR* Id, const TCHAR* Name, EFVAchKind Kind, int32 Target, const TCHAR* Param,
		int64 Money, const TCHAR* Cosmetic, const TCHAR* Desc, const TCHAR* Decor = nullptr)
	{
		FFVAchievementDef& A = D.Achievements.AddDefaulted_GetRef();
		A.Id = FName(Id);
		A.Name = Name;
		A.Description = Desc;
		A.Kind = Kind;
		A.Target = FMath::Max(1, Target);
		A.Param = Param ? FName(Param) : NAME_None;
		A.RewardMoney = Money;
		A.RewardCosmetic = Cosmetic ? FName(Cosmetic) : NAME_None;
		A.RewardDecor = Decor ? FName(Decor) : NAME_None;
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

	// ============================================================================================
	// Content
	// ============================================================================================
	void BuildLocations(FFVDatabaseData& D)
	{
		AddLocation(D, TEXT("pond"), TEXT("Деревенский пруд"), EFVTheme::Pond, 0, 1, FVector2D(330, 246), 0.18f, 0.08f,
			FColor(96, 176, 190), FColor(36, 104, 132),
			TEXT("Тихий пруд у самой деревни, заросший камышом и кувшинками. Лучшее место, чтобы научиться рыбачить."));
		D.Locations.Last().SideWiden = 0.1f;   // hotfix 0.61: +10 % of the screen width on each side (scroll camera)
		AddLocation(D, TEXT("river"), TEXT("Лесная река"), EFVTheme::River, 400, 2, FVector2D(168, 188), 0.15f, 0.09f,
			FColor(104, 186, 220), FColor(34, 92, 156),
			TEXT("Быстрая река среди старых елей. В омутах караулят добычу щуки, а на перекатах играют голавли."));
		AddLocation(D, TEXT("mountain"), TEXT("Горное озеро"), EFVTheme::Mountain, 1500, 3, FVector2D(196, 88), 0.12f, 0.10f,
			FColor(122, 212, 226), FColor(28, 104, 150),
			TEXT("Ледяное прозрачное озеро у подножия снежных вершин. Дом хариуса, гольца и грозного тайменя."));
		AddLocation(D, TEXT("ocean"), TEXT("Морской причал"), EFVTheme::Ocean, 5000, 5, FVector2D(530, 262), 0.14f, 0.12f,
			FColor(66, 158, 214), FColor(16, 56, 128),
			TEXT("Солёный ветер, крики чаек и длинный причал над морской глубиной. Здесь клюют настоящие гиганты."));
		AddLocation(D, TEXT("swamp"), TEXT("Туманное болото"), EFVTheme::Swamp, 12000, 7, FVector2D(112, 292), 0.20f, 0.13f,
			FColor(112, 138, 78), FColor(46, 70, 44),
			TEXT("Мрачное болото в вечном тумане. Говорят, в его чёрных омутах живёт нечто очень древнее."));
		AddLocation(D, TEXT("volcano"), TEXT("Вулканическое озеро"), EFVTheme::Volcano, 30000, 9, FVector2D(478, 84), 0.10f, 0.15f,
			FColor(255, 168, 48), FColor(196, 56, 20),
			TEXT("Озеро кипящей лавы в жерле спящего вулкана. Нужна железная выдержка — и жаропрочная леска!"));
	}

	void BuildFish(FFVDatabaseData& D)
	{
		using S = EFVFishShape;
		using P = EFVFishPattern;
		using M = EFVMotion;
		using R = EFVRarity;
		using W = EFVWeatherReq;
		const uint8 Any = FVTime::Any;
		const uint8 Mor = FVTime::Morning, Day = FVTime::Day, Eve = FVTime::Evening, Ngt = FVTime::Night;

		// ---------------------------------------------------------------- Деревенский пруд (15–120, legend 800)
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_crucian"), TEXT("Карась"), TEXT("pond"), R::Common, 15, 15, M::Mixed, 8, 30, 30,
				TEXT("Золотистый и неприхотливый. С него начинается путь любого рыбака."));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Carp, P::Plain, FColor(200, 160, 64), FColor(244, 224, 150), FColor(184, 110, 56), FColor(150, 116, 48));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_roach"), TEXT("Плотва"), TEXT("pond"), R::Common, 20, 20, M::Smooth, 10, 25, 26,
				TEXT("Шустрая серебристая рыбка с красными глазами. Клюёт почти весь день."));
			When(F, Mor | Day | Eve, W::Any, 1, 0.f);
			Look(F, S::Minnow, P::Plain, FColor(150, 170, 186), FColor(236, 240, 238), FColor(214, 72, 52), FColor(92, 110, 132));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_rotan"), TEXT("Ротан"), TEXT("pond"), R::Common, 25, 24, M::Mixed, 8, 22, 22,
				TEXT("Большеротый обжора-головешка. Выходит на охоту в сумерках."));
			When(F, Eve | Ngt, W::Any, 1, 0.f);
			Look(F, S::Perch, P::Spots, FColor(104, 92, 60), FColor(176, 160, 112), FColor(84, 72, 46), FColor(56, 46, 30));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_perch"), TEXT("Окунь"), TEXT("pond"), R::Uncommon, 40, 30, M::Dart, 12, 40, 16,
				TEXT("Полосатый разбойник пруда. Бросается на наживку резко и без раздумий."));
			When(F, Mor | Day | Eve, W::Any, 1, 0.f);
			Look(F, S::Perch, P::Stripes, FColor(150, 168, 62), FColor(236, 226, 160), FColor(232, 96, 44), FColor(54, 74, 32));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_tench"), TEXT("Линь"), TEXT("pond"), R::Uncommon, 65, 36, M::Sinker, 20, 50, 12,
				TEXT("Скользкий и неторопливый. Любит тихие рассветы и вечера у камышей."));
			When(F, Mor | Eve, W::Any, 1, 0.1f);
			Look(F, S::Carp, P::Plain, FColor(84, 104, 48), FColor(196, 176, 84), FColor(64, 76, 40), FColor(120, 140, 60));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_carp"), TEXT("Карп"), TEXT("pond"), R::Rare, 120, 45, M::Floater, 30, 90, 6,
				TEXT("Крупный и упрямый. Живёт на глубине и не сдаётся без долгого боя."));
			When(F, Any, W::Any, 1, 0.3f);
			Look(F, S::Carp, P::Spots, FColor(168, 124, 58), FColor(234, 204, 128), FColor(150, 78, 40), FColor(214, 178, 96));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_tsar_carp"), TEXT("Царь-Карп"), TEXT("pond"), R::Legendary, 800, 70, M::Mixed, 90, 150, 1.2f,
				TEXT("Говорят, он старше самой деревни. Чешуя сияет, как червонное золото."));
			When(F, Mor | Day, W::Any, 3, 0.55f);
			Look(F, S::Legend, P::Glow, FColor(242, 188, 40), FColor(255, 242, 170), FColor(226, 92, 40), FColor(255, 250, 210));
			// no preparation: the gentle introduction to the legends (classic fight, favourite bait = тесто)
			Legend(F, EFVLegend::TsarCarp, TEXT("Особой снасти не нужно. Говорят, неравнодушен к тесту."),
				TEXT("Величав и непредсказуем, без хитростей. Держи его в золотой полосе!"));
		}

		// ---------------------------------------------------------------- Лесная река (30–220, legend 1500)
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_bleak"), TEXT("Уклейка"), TEXT("river"), R::Common, 30, 25, M::Dart, 7, 15, 30,
				TEXT("Крошечная и юркая. Держится стайкой у самой поверхности."));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Minnow, P::Stripes, FColor(176, 196, 214), FColor(246, 248, 250), FColor(170, 182, 196), FColor(112, 142, 182));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_chub"), TEXT("Голавль"), TEXT("river"), R::Common, 55, 32, M::Mixed, 20, 55, 26,
				TEXT("Лобастый и осторожный. Обожает жуков, упавших в воду с веток."));
			When(F, Mor | Day | Eve, W::Any, 1, 0.f);
			Look(F, S::Carp, P::Plain, FColor(74, 84, 90), FColor(226, 220, 196), FColor(206, 94, 60), FColor(50, 56, 62));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_crayfish"), TEXT("Речной рак"), TEXT("river"), R::Uncommon, 75, 30, M::Sinker, 8, 18, 14,
				TEXT("Пятится назад и больно щиплется. Выползает из нор, когда темнеет."));
			When(F, Eve | Ngt, W::Any, 1, 0.f);
			Look(F, S::Crayfish, P::Plain, FColor(118, 74, 52), FColor(196, 140, 100), FColor(150, 64, 44), FColor(78, 44, 30));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_bream"), TEXT("Лещ"), TEXT("river"), R::Uncommon, 90, 38, M::Smooth, 25, 65, 13,
				TEXT("Широкий, как сковорода. Неспешно кормится на дне тихих омутов."));
			When(F, Mor | Eve | Ngt, W::Any, 1, 0.1f);
			Look(F, S::Flounder, P::Plain, FColor(170, 146, 88), FColor(226, 206, 150), FColor(104, 92, 72), FColor(140, 118, 72));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_pike"), TEXT("Щука"), TEXT("river"), R::Rare, 160, 52, M::Dart, 40, 120, 7,
				TEXT("Зубастая хозяйка реки. Часами караулит добычу в засаде у коряг."));
			When(F, Any, W::Any, 1, 0.3f);
			Look(F, S::Pike, P::Spots, FColor(86, 124, 62), FColor(222, 222, 172), FColor(182, 122, 62), FColor(196, 206, 126));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_zander"), TEXT("Судак"), TEXT("river"), R::Rare, 220, 60, M::Mixed, 35, 95, 6,
				TEXT("Клыкастый ночной охотник. Его глаза светятся в тёмной воде."));
			When(F, Mor | Eve | Ngt, W::Any, 2, 0.35f);
			Look(F, S::Perch, P::Stripes, FColor(150, 160, 150), FColor(232, 232, 222), FColor(172, 170, 140), FColor(72, 82, 72));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_silver_sturgeon"), TEXT("Серебряный осётр"), TEXT("river"), R::Legendary, 1500, 80, M::Smooth, 120, 250, 1.2f,
				TEXT("Древняя рыба в костяной броне. Поднимается из глубин только в дождь."));
			When(F, Any, W::Rain, 4, 0.55f);
			Look(F, S::Legend, P::Spots, FColor(180, 196, 212), FColor(240, 244, 248), FColor(120, 140, 162), FColor(250, 252, 255));
			// soft rule: a line below tier 2 (плетёная) snaps the leader — the first legend that needs the workshop
			FFVLegendPrep& Pr = Legend(F, EFVLegend::Sturgeon,
				TEXT("Тяжёл, как бревно: льняную леску оборвёт. Нужна плетёная или крепче (Мастерская → Оснастка). Любит мотыля."),
				TEXT("Идёт против течения: полосу сносит вниз, а осётр рвётся вверх. Увидел синие стрелки — начинай держать заранее."));
			Pr.SnapBelowLineTier = 2;
			Pr.PrepItems = { TEXT("леска: плетёная+") };
			Pr.SnapText = TEXT("Осётр оборвал поводок! Леска слабовата — нужна плетёная или крепче (Мастерская → Оснастка).");
		}

		// ---------------------------------------------------------------- Горное озеро (60–400, legend 2500)
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_grayling"), TEXT("Хариус"), TEXT("mountain"), R::Common, 60, 35, M::Dart, 20, 45, 30,
				TEXT("Щёголь горных вод с огромным пёстрым спинным плавником."));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Perch, P::Spots, FColor(112, 120, 146), FColor(214, 214, 226), FColor(186, 80, 128), FColor(48, 50, 72));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_brook_trout"), TEXT("Ручьевая форель"), TEXT("mountain"), R::Common, 85, 42, M::Mixed, 20, 55, 25,
				TEXT("Любит ледяную воду и быстрое течение. Бока усыпаны красными крапинками."));
			When(F, Mor | Day | Eve, W::Any, 1, 0.f);
			Look(F, S::Minnow, P::Spots, FColor(150, 132, 84), FColor(236, 214, 164), FColor(172, 142, 92), FColor(206, 62, 52));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_char"), TEXT("Арктический голец"), TEXT("mountain"), R::Uncommon, 150, 50, M::Smooth, 30, 70, 14,
				TEXT("Морозостойкий родич форели с огненно-оранжевым брюшком."));
			When(F, Any, W::Any, 1, 0.1f);
			Look(F, S::Tuna, P::Plain, FColor(70, 92, 104), FColor(242, 124, 62), FColor(232, 104, 62), FColor(232, 232, 212));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_lenok"), TEXT("Ленок"), TEXT("mountain"), R::Rare, 250, 60, M::Dart, 35, 75, 7,
				TEXT("Бронзовый хищник перекатов. Схватив наживку, сразу уходит в глубину."));
			When(F, Mor | Eve, W::Any, 3, 0.3f);
			Look(F, S::Pike, P::Spots, FColor(132, 98, 62), FColor(224, 184, 124), FColor(184, 94, 62), FColor(62, 42, 30));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_taimen"), TEXT("Таймень"), TEXT("mountain"), R::Epic, 400, 70, M::Mixed, 60, 160, 3.5f,
				TEXT("Гроза горных озёр с алым хвостом. Настоящие великаны берут в ненастье."));
			When(F, Any, W::Rain, 4, 0.45f);
			Look(F, S::Tuna, P::Spots, FColor(120, 86, 80), FColor(220, 196, 176), FColor(212, 58, 48), FColor(52, 36, 36));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_crystal_trout"), TEXT("Хрустальная форель"), TEXT("mountain"), R::Legendary, 2500, 90, M::Dart, 50, 90, 1.2f,
				TEXT("Прозрачна, как талый лёд. Показывается лишь в лучах ясного утра."));
			When(F, Mor | Day, W::Sunny, 5, 0.6f);
			Look(F, S::Legend, P::Glow, FColor(172, 222, 246), FColor(242, 250, 255), FColor(122, 182, 242), FColor(232, 250, 255));
			// hard rule: only a thin line (silk / fluoro); the titanium string does NOT work here (owner's decision)
			FFVLegendPrep& Pr = Legend(F, EFVLegend::CrystalTrout,
				TEXT("Пугается толстой лески — только шёлковая или флюорокарбоновая, титан не годится. Держи для неё отдельную удочку. Ловят на мушку."),
				TEXT("Становится прозрачной. Значок-глаз — сейчас исчезнет; следи за бликами, они выдают, где она."));
			Pr.Lines = { FName(TEXT("line_silk")), FName(TEXT("line_fluoro")) };
			Pr.PrepItems = { TEXT("леска: шёлк/флюоро") };
			Pr.GatedToast = TEXT("Под бликом мелькнула хрустальная тень — и шарахнулась от толстой лески. Нужна шёлковая или флюоро (Мастерская → Оснастка).");
		}

		// ---------------------------------------------------------------- Морской причал (100–700, legend 4000)
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_mackerel"), TEXT("Скумбрия"), TEXT("ocean"), R::Common, 100, 45, M::Dart, 25, 50, 30,
				TEXT("Стремительная и полосатая. Ходит у причала огромными косяками."));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Minnow, P::Stripes, FColor(58, 122, 142), FColor(230, 236, 240), FColor(72, 102, 112), FColor(28, 50, 72));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_flounder"), TEXT("Камбала"), TEXT("ocean"), R::Common, 130, 48, M::Sinker, 25, 60, 24,
				TEXT("Плоская рыба, оба глаза которой смотрят в небо. Прячется в песке."));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Flounder, P::Spots, FColor(150, 120, 80), FColor(240, 230, 210), FColor(130, 100, 70), FColor(92, 70, 50));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_sea_bass"), TEXT("Морской окунь"), TEXT("ocean"), R::Uncommon, 200, 55, M::Mixed, 20, 55, 15,
				TEXT("Ярко-красный и колючий. Держится у камней под причалом."));
			When(F, Mor | Day | Eve, W::Any, 1, 0.1f);
			Look(F, S::Perch, P::Plain, FColor(222, 88, 68), FColor(250, 190, 160), FColor(242, 122, 82), FColor(170, 50, 40));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_squid"), TEXT("Кальмар"), TEXT("ocean"), R::Uncommon, 280, 60, M::Sinker, 20, 60, 13,
				TEXT("Десять щупалец и облако чернил. По ночам поднимается к огням причала."));
			When(F, Eve | Ngt, W::Any, 1, 0.15f);
			Look(F, S::Squid, P::Spots, FColor(232, 160, 172), FColor(250, 222, 222), FColor(202, 112, 132), FColor(170, 62, 92));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_pufferfish"), TEXT("Рыба-фугу"), TEXT("ocean"), R::Rare, 450, 70, M::Floater, 15, 40, 7,
				TEXT("При опасности раздувается в колючий шар. Греется у поверхности в ясные дни."));
			When(F, Day, W::Sunny, 5, 0.3f);
			Look(F, S::Puffer, P::Spots, FColor(222, 200, 92), FColor(250, 242, 192), FColor(202, 160, 62), FColor(120, 100, 50));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_tuna"), TEXT("Тунец"), TEXT("ocean"), R::Epic, 700, 80, M::Smooth, 80, 220, 3.5f,
				TEXT("Стальная торпеда открытого моря. Способен утащить за собой лодку."));
			When(F, Mor | Day, W::Any, 6, 0.5f);
			Look(F, S::Tuna, P::Plain, FColor(42, 62, 122), FColor(212, 222, 232), FColor(242, 202, 62), FColor(72, 92, 152));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_giant_angler"), TEXT("Удильщик-исполин"), TEXT("ocean"), R::Legendary, 4000, 95, M::Smooth, 100, 180, 1.2f,
				TEXT("Во тьме мерцает приманка-фонарик, а за ней — пасть размером с бочку."));
			When(F, Ngt, W::Any, 7, 0.6f);
			Look(F, S::Legend, P::Glow, FColor(52, 60, 92), FColor(122, 130, 162), FColor(42, 50, 82), FColor(180, 255, 220));
			// hard rule: the glowing float; soft rule: a line of tier 4+ (fluoro / titanium), otherwise it bites the leader off
			FFVLegendPrep& Pr = Legend(F, EFVLegend::Angler,
				TEXT("Во тьме ищет свет — без светящегося поплавка не найдёт наживку. Зубы режут всё, кроме флюорокарбона и титана. Любит волшебную наживку."),
				TEXT("До поклёвки горит огонёк — жди «!». В бою бросается к фонарику, когда кольцо замкнётся. Красный мигающий огонёк — обман."));
			Pr.Float = FName(TEXT("float_glow"));
			Pr.SnapBelowLineTier = 4;
			Pr.PrepItems = { TEXT("светящ. поплавок"), TEXT("леска: флюоро+") };
			Pr.GatedToast = TEXT("Где-то в темноте мигнул огонёк… Удильщик не нашёл твой поплавок. Нужен светящийся (Мастерская → Оснастка).");
			Pr.SnapText = TEXT("Удильщик перекусил поводок! Нужна флюорокарбоновая или титановая леска.");
		}

		// ---------------------------------------------------------------- Туманное болото (180–1000, legend 6000)
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_loach"), TEXT("Вьюн"), TEXT("swamp"), R::Common, 180, 55, M::Dart, 10, 30, 30,
				TEXT("Скользкий, как мыло. Перед грозой беспокойно мечется у поверхности."));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Eel, P::Stripes, FColor(122, 100, 60), FColor(202, 182, 122), FColor(100, 80, 50), FColor(62, 50, 30));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_mudskipper"), TEXT("Илистый прыгун"), TEXT("swamp"), R::Common, 210, 58, M::Floater, 10, 25, 25,
				TEXT("Рыба, которая гуляет по грязи на плавниках и умеет моргать."));
			When(F, Mor | Day | Eve, W::Any, 1, 0.f);
			Look(F, S::Minnow, P::Spots, FColor(124, 112, 82), FColor(192, 182, 152), FColor(92, 142, 202), FColor(72, 62, 42));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_eel"), TEXT("Угорь"), TEXT("swamp"), R::Uncommon, 320, 65, M::Smooth, 50, 130, 14,
				TEXT("Длинный и изворотливый. Выползает из ила, едва стемнеет."));
			When(F, Eve | Ngt, W::Any, 1, 0.1f);
			Look(F, S::Eel, P::Plain, FColor(62, 72, 42), FColor(182, 172, 112), FColor(82, 92, 52), FColor(40, 48, 28));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_snakehead"), TEXT("Змееголов"), TEXT("swamp"), R::Uncommon, 450, 72, M::Dart, 40, 100, 12,
				TEXT("Дышит воздухом и может проползти по суше до соседнего болота."));
			When(F, Any, W::Any, 1, 0.2f);
			Look(F, S::Pike, P::Spots, FColor(82, 92, 52), FColor(172, 172, 112), FColor(62, 72, 42), FColor(30, 36, 20));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_catfish"), TEXT("Сом"), TEXT("swamp"), R::Rare, 700, 82, M::Mixed, 60, 250, 7,
				TEXT("Усатый великан болотных ям. Выходит на охоту, когда идёт дождь."));
			When(F, Any, W::Rain, 1, 0.35f);
			Look(F, S::Catfish, P::Plain, FColor(72, 72, 62), FColor(182, 172, 152), FColor(62, 56, 46), FColor(42, 42, 36));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_gar"), TEXT("Панцирная щука"), TEXT("swamp"), R::Epic, 1000, 90, M::Mixed, 80, 200, 3.5f,
				TEXT("Живое ископаемое: чешуя крепче брони, а зубы — как у ящера."));
			When(F, Mor | Day, W::Any, 8, 0.5f);
			Look(F, S::Pike, P::Stripes, FColor(112, 112, 72), FColor(202, 192, 142), FColor(132, 102, 62), FColor(52, 52, 32));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_fog_serpent"), TEXT("Туманный змей"), TEXT("swamp"), R::Legendary, 6000, 100, M::Sinker, 200, 400, 1.2f,
				TEXT("Болотная легенда. Его видят лишь в ночном тумане — и то не все."));
			When(F, Ngt, W::Any, 8, 0.6f);
			Look(F, S::Legend, P::Glow, FColor(92, 142, 112), FColor(192, 232, 202), FColor(62, 112, 92), FColor(172, 255, 202));
			// hard rule: live bait only (выползок); soft rule: a line of tier 4+, otherwise the snag takes the tackle
			FFVLegendPrep& Pr = Legend(F, EFVLegend::FogSerpent,
				TEXT("Берёт только живого выползка. Слабую леску утащит в коряги — нужна флюорокарбоновая или крепче."),
				TEXT("Прячется в тумане и вьётся петлями. Зелёные пузырьки выдают его путь. После 60 % петли шире."));
			Pr.Bait = FName(TEXT("bait_nightcrawler"));
			Pr.SnapBelowLineTier = 4;
			Pr.PrepItems = { TEXT("наживка: выползок"), TEXT("леска: флюоро+") };
			Pr.GatedToast = TEXT("Туман колыхнулся — змей обнюхал наживку и отвернулся. Ему нужен живой выползок.");
			Pr.SnapText = TEXT("Змей утащил снасть в коряги! Нужна леска не слабее флюорокарбоновой.");
		}

		// ---------------------------------------------------------------- Вулканическое озеро (300–1800, legend 10000)
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_sparkfin"), TEXT("Искрянка"), TEXT("volcano"), R::Common, 300, 65, M::Dart, 5, 15, 30,
				TEXT("Мальки, что искрятся в лаве, словно угольки в костре."));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Minnow, P::Glow, FColor(72, 52, 52), FColor(255, 162, 62), FColor(255, 92, 32), FColor(255, 202, 82));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_fire_perch"), TEXT("Огнепёрый окунь"), TEXT("volcano"), R::Common, 420, 70, M::Mixed, 20, 45, 24,
				TEXT("Плавники полыхают настоящим пламенем. Голыми руками не брать!"));
			When(F, Any, W::Any, 1, 0.f);
			Look(F, S::Perch, P::Stripes, FColor(202, 72, 42), FColor(255, 192, 102), FColor(255, 152, 32), FColor(122, 32, 22));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_obsidian_crab"), TEXT("Обсидиановый рак"), TEXT("volcano"), R::Uncommon, 650, 75, M::Sinker, 12, 30, 15,
				TEXT("Панцирь из чёрного вулканического стекла, клешни раскалены докрасна."));
			When(F, Any, W::Any, 1, 0.1f);
			Look(F, S::Crayfish, P::Spots, FColor(52, 42, 62), FColor(122, 92, 112), FColor(255, 122, 42), FColor(255, 172, 62));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_lava_eel"), TEXT("Лавовый угорь"), TEXT("volcano"), R::Rare, 1100, 88, M::Mixed, 60, 150, 7,
				TEXT("Плавает в расплавленном камне, будто в тёплой ванне."));
			When(F, Any, W::Any, 9, 0.3f);
			Look(F, S::Eel, P::Glow, FColor(182, 42, 32), FColor(255, 162, 62), FColor(255, 92, 42), FColor(255, 232, 122));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_magma_puffer"), TEXT("Магмовый иглобрюх"), TEXT("volcano"), R::Epic, 1800, 95, M::Floater, 20, 50, 3.5f,
				TEXT("Раздувается, набирая раскалённый газ. Лучше его не злить."));
			When(F, Eve | Ngt, W::Any, 9, 0.45f);
			Look(F, S::Puffer, P::Spots, FColor(92, 62, 62), FColor(255, 132, 52), FColor(255, 182, 62), FColor(255, 222, 92));
		}
		{
			FFVFishDef& F = AddFish(D, TEXT("fish_zhar_ryba"), TEXT("Жар-рыба"), TEXT("volcano"), R::Legendary, 10000, 110, M::Dart, 80, 140, 1.2f,
				TEXT("Родня сказочной Жар-птицы. Одна её чешуйка стоит целого состояния."));
			When(F, Any, W::Any, 10, 0.65f);
			Look(F, S::Legend, P::Glow, FColor(255, 122, 32), FColor(255, 232, 122), FColor(255, 62, 42), FColor(255, 250, 202));
			// THE TRIAL: both hard rules together — the Жар-наживка and the heat-proof line (checklist order = rule order:
			// bait first, then the line, see FFVLegendPrep::PrepItems)
			FFVLegendPrep& Pr = Legend(F, EFVLegend::ZharRyba,
				TEXT("Обычная леска плавится: нужны жаропрочная леска (Мастерская, Станок) и Жар-наживка (Кухня, Печь). Без них даже не подойдёт. Бросай на глубину."),
				TEXT("Три фазы. Извержения жгут полосу — уходи из лавы. В ярости полоса сужается. В конце рыба прыгает сквозь огонь — веди полосу к искре."));
			Pr.Bait = FName(TEXT("bait_ember"));
			Pr.Lines = { FName(TEXT("line_heat")) };
			Pr.PrepItems = { TEXT("наживка: Жар-наживка"), TEXT("леска: жаропрочная") };
			Pr.GatedToast = TEXT("Вода закипела у поплавка — Жар-рыба прошла мимо. Нужны жаропрочная леска и Жар-наживка.");
		}

		// Guarantee "grouped by location (progression order), then by rarity" regardless of the order above.
		TMap<FName, int32> LocOrder;
		for (int32 i = 0; i < D.Locations.Num(); ++i) { LocOrder.Add(D.Locations[i].Id, i); }
		D.Fish.StableSort([&LocOrder](const FFVFishDef& A, const FFVFishDef& B)
		{
			const int32* LA = LocOrder.Find(A.Location);
			const int32* LB = LocOrder.Find(B.Location);
			const int32 IA = LA ? *LA : MAX_int32;
			const int32 IB = LB ? *LB : MAX_int32;
			if (IA != IB) { return IA < IB; }
			return uint8(A.Rarity) < uint8(B.Rarity);
		});
	}

	void BuildJunk(FFVDatabaseData& D)
	{
		AddJunk(D, TEXT("junk_weed"), TEXT("Водоросли"), EFVJunkStyle::Weed, 1,
			TEXT("Мокрые, скользкие и совершенно бесполезные."));
		AddJunk(D, TEXT("junk_boot"), TEXT("Старый ботинок"), EFVJunkStyle::Boot, 6,
			TEXT("Кто-то явно ушёл домой босиком."));
		AddJunk(D, TEXT("junk_can"), TEXT("Консервная банка"), EFVJunkStyle::Can, 3,
			TEXT("Внутри плещется немного воды и чуть-чуть надежды."));
		AddJunk(D, TEXT("junk_driftwood"), TEXT("Коряга"), EFVJunkStyle::Driftwood, 5,
			TEXT("Причудливая коряга. Сгодится разве что на дрова."));
		AddJunk(D, TEXT("junk_bottle"), TEXT("Бутылка"), EFVJunkStyle::Bottle, 12,
			TEXT("Внутри была записка, но чернила давно расплылись."));
	}

	void BuildRods(FFVDatabaseData& D)
	{
		//                                                                   tier  price   lvl  bar   reel   drain  bite   range  luck   treas
		AddRod(D, TEXT("rod_bamboo"), TEXT("Бамбуковая удочка"), 0, 0, 1, 0.f, 1.00f, 1.00f, 1.00f, 0.55f, 0.00f, 0.00f,
			FColor(214, 186, 110), FColor(120, 88, 48),
			TEXT("Лёгкая и простая. Верой и правдой служила ещё деду."), 0);
		AddRod(D, TEXT("rod_fiberglass"), TEXT("Стеклопластиковая удочка"), 1, 800, 2, 8.f, 1.05f, 0.95f, 0.90f, 0.64f, 0.10f, 0.02f,
			FColor(236, 226, 196), FColor(190, 60, 50),
			TEXT("Гибкая и прочная. Забрасывает дальше и прощает ошибки."), 1);
		AddRod(D, TEXT("rod_carbon"), TEXT("Карбоновая удочка"), 2, 3000, 4, 16.f, 1.10f, 0.90f, 0.80f, 0.73f, 0.25f, 0.04f,
			FColor(62, 64, 76), FColor(64, 128, 210),
			TEXT("Лёгкий карбон передаёт в руку малейшую поклёвку."), 1);
		AddRod(D, TEXT("rod_titanium"), TEXT("Титановая удочка"), 3, 9000, 6, 24.f, 1.15f, 0.85f, 0.71f, 0.82f, 0.40f, 0.07f,
			FColor(190, 200, 212), FColor(78, 90, 112),
			TEXT("Не гнётся даже под весом самых крупных трофеев."), 1);
		AddRod(D, TEXT("rod_iridium"), TEXT("Иридиевая удочка"), 4, 25000, 8, 32.f, 1.20f, 0.80f, 0.62f, 0.91f, 0.58f, 0.09f,
			FColor(172, 92, 212), FColor(92, 40, 124),
			TEXT("Переливается фиолетовым. Редкая рыба к ней так и тянется."), 2);
		AddRod(D, TEXT("rod_poseidon"), TEXT("Удочка «Посейдон»"), 5, 60000, 10, 40.f, 1.28f, 0.74f, 0.55f, 1.00f, 0.75f, 0.12f,
			FColor(56, 196, 210), FColor(244, 200, 60),
			TEXT("Легендарная снасть морского владыки. Ей покорны любые воды — даже огненные."), 2);
	}

	void BuildBaits(FFVDatabaseData& D)
	{
		//                                                                         pack  size lvl  bite   rare   junk   size
		// bait_worm MUST stay first (new-game gift, treasure weights by table order); new baits are appended at the end.
		AddBait(D, TEXT("bait_worm"), TEXT("Червяки"), EFVBaitStyle::Worm, 25, 10, 1, 0.85f, 0.00f, 1.00f, 0.00f,
			TEXT("Классика. Клюёт чуть быстрее, чем на пустой крючок."), 1);
		AddBait(D, TEXT("bait_grub"), TEXT("Мотыль"), EFVBaitStyle::Grub, 60, 10, 3, 0.60f, 0.05f, 1.15f, 0.00f,
			TEXT("Рыба его обожает: клёв очень частый, но и мусор цепляется чаще."), 1);
		AddBait(D, TEXT("bait_lure"), TEXT("Блесна"), EFVBaitStyle::Lure, 180, 10, 5, 1.05f, 0.35f, 0.45f, 0.15f,
			TEXT("Для хищников: меньше мусора, больше редких и крупных рыб, но клюёт не сразу."), 2);
		AddBait(D, TEXT("bait_magic"), TEXT("Волшебная наживка"), EFVBaitStyle::Magic, 500, 5, 7, 0.70f, 0.60f, 0.25f, 0.25f,
			TEXT("Мерцает в воде. Быстрый клёв, редкие и крупные рыбы, почти никакого мусора."), 2);

		// ---- update 2: dug / farmed / cooked baits (not sold: PackPrice 0, PackSize = display pack)
		AddBait(D, TEXT("bait_nightcrawler"), TEXT("Выползок"), EFVBaitStyle::Nightcrawler, 0, 5, 1, 0.80f, 0.15f, 0.90f, 0.12f,
			TEXT("Крупный жирный червь. Выкапывается лопатой или заводится в большом червятнике."), 0);
		{
			FFVBaitDef& B = AddBait(D, TEXT("bait_dough"), TEXT("Тесто"), EFVBaitStyle::Dough, 0, 5, 1, 0.70f, 0.00f, 1.10f, 0.00f,
				TEXT("Мягкое тесто с кухни. Карпы и мелкая рыбёшка его просто обожают."), 0);
			B.AffinityShapes = uint16((1u << uint8(EFVFishShape::Carp)) | (1u << uint8(EFVFishShape::Minnow)));
			B.AffinityMult = 1.0f;
		}
		AddBait(D, TEXT("bait_groundbait"), TEXT("Прикормка"), EFVBaitStyle::Groundbait, 0, 5, 1, 0.45f, 0.05f, 1.00f, 0.05f,
			TEXT("Ароматная прикормка: рыба собирается к поплавку и клюёт очень быстро."), 0);

		// ---- update 2: 24 crafted lures "lure_<base>_<color>_<glitter>" (lure workshop)
		static const TCHAR* ColorAdj[] = { TEXT("Красная"), TEXT("Жёлтая"), TEXT("Зелёная"), TEXT("Чёрная") };
		static const TCHAR* BaseNoun[] = { TEXT("блесна"), TEXT("мушка") };
		static const TCHAR* GlitterSuffix[] = { TEXT(""), TEXT(" с серебром"), TEXT(" с золотом") };
		static const TCHAR* BaseDesc[] = {
			TEXT("Самодельная металлическая блесна: играет в воде и манит рыбу с плавными и непредсказуемыми повадками."),
			TEXT("Самодельная мушка из пера: по душе шустрой рыбе и той, что держится у поверхности.") };
		static const TCHAR* ColorDesc[] = {
			TEXT(" Красный цвет дразнит хищников."),
			TEXT(" Жёлтый цвет хорошо заметен в мутной воде."),
			TEXT(" Зелёный цвет похож на водоросли и мелкую живность."),
			TEXT(" Чёрный силуэт хорошо виден снизу — на него идут обитатели глубин.") };
		static const TCHAR* GlitterDesc[] = { TEXT(""),
			TEXT(" Серебряные блёстки привлекают редкую рыбу."),
			TEXT(" Золотые блёстки манят самые редкие и крупные трофеи.") };
		const uint16 ColorShapes[] = {
			uint16((1u << uint8(EFVFishShape::Perch)) | (1u << uint8(EFVFishShape::Pike)) | (1u << uint8(EFVFishShape::Tuna))),
			uint16((1u << uint8(EFVFishShape::Carp)) | (1u << uint8(EFVFishShape::Flounder)) | (1u << uint8(EFVFishShape::Puffer))),
			uint16((1u << uint8(EFVFishShape::Minnow)) | (1u << uint8(EFVFishShape::Eel)) | (1u << uint8(EFVFishShape::Crayfish))),
			uint16((1u << uint8(EFVFishShape::Catfish)) | (1u << uint8(EFVFishShape::Squid)) | (1u << uint8(EFVFishShape::Legend))) };
		const uint8 BaseMotions[] = {
			uint8((1u << uint8(EFVMotion::Mixed)) | (1u << uint8(EFVMotion::Smooth))),
			uint8((1u << uint8(EFVMotion::Dart)) | (1u << uint8(EFVMotion::Floater))) };
		//                        spoon  fly
		const float BaseBite[] = { 1.00f, 0.85f };
		const float BaseRare[] = { 0.15f, 0.10f };
		const float BaseJunk[] = { 0.50f, 0.70f };
		const float BaseSize[] = { 0.10f, 0.05f };
		const float GlitterRare[] = { 0.f, 0.15f, 0.30f };
		const float GlitterSize[] = { 0.f, 0.05f, 0.10f };
		for (uint8 Base = 0; Base < FVDB::LureBases; ++Base)
		{
			for (uint8 Color = 0; Color < FVDB::LureColors; ++Color)
			{
				for (uint8 Glitter = 0; Glitter < FVDB::LureGlitters; ++Glitter)
				{
					const FString Id = FVDB::LureId(Base, Color, Glitter).ToString();
					const FString Name = FString::Printf(TEXT("%s %s%s"), ColorAdj[Color], BaseNoun[Base], GlitterSuffix[Glitter]);
					const FString Desc = FString(BaseDesc[Base]) + ColorDesc[Color] + GlitterDesc[Glitter];
					FFVBaitDef& B = AddBait(D, *Id, *Name, Base == 0 ? EFVBaitStyle::Lure : EFVBaitStyle::Fly, 0, FVDB::LurePackSize(), 1,
						BaseBite[Base], BaseRare[Base] + GlitterRare[Glitter], BaseJunk[Base], BaseSize[Base] + GlitterSize[Glitter], *Desc, 0);
					B.bLure = true;
					B.LureBase = Base;
					B.LureColor = Color;
					B.LureGlitter = Glitter;
					B.AffinityShapes = ColorShapes[Color];
					B.AffinityMotions = BaseMotions[Base];
					B.AffinityMult = 1.5f;
				}
			}
		}

		// ---- update 5: Жар-наживка (crafted only: Кухня → Печь, rcp_bait_ember). Appended at the END (see bait_worm above).
		{
			FFVBaitDef& B = AddBait(D, TEXT("bait_ember"), TEXT("Жар-наживка"), EFVBaitStyle::Ember, 0, 4, 1, 0.90f, 0.30f, 0.20f, 0.20f,
				TEXT("Тлеющий шарик из огненной рыбы. Жар-рыба идёт на него охотно, прочая рыба — вяло."), 0);
			B.LegendLure = EFVLegend::ZharRyba;
			B.LegendLureMult = 3.f;    // update 6 (Р45): was 12 (~50 % of the deep bites with the best tackle) -> ~18-20 %
			B.OtherFishMult = 0.35f;   // with LegendLureMult 3: ~every fifth bite in the deep volcano water is the Zhar-ryba
		}

		// ---- update 5: every legend's favourite bait (PickCatch: max(LureAffinity, LegendLureMult), never their product)
		for (FFVBaitDef& B : D.Baits)
		{
			const FString Id = B.Id.ToString();
			if (Id == TEXT("bait_dough"))                { B.LegendLure = EFVLegend::TsarCarp;     B.LegendLureMult = 2.5f; }
			else if (Id == TEXT("bait_grub"))            { B.LegendLure = EFVLegend::Sturgeon;     B.LegendLureMult = 2.f; }
			else if (B.Style == EFVBaitStyle::Fly)       { B.LegendLure = EFVLegend::CrystalTrout; B.LegendLureMult = 2.f; }   // every мушка
			else if (Id == TEXT("bait_magic"))           { B.LegendLure = EFVLegend::Angler;       B.LegendLureMult = 2.f; }
			// the serpent: spec x7 aimed at ~20 % of the night bites on the swamp; x7 measured 16 % (6.2 nightcrawlers per
			// serpent bite at the level-9 tackle, the §5 limit is 6) -> x8 gives ~18 % (about 5.6 per bite)
			else if (Id == TEXT("bait_nightcrawler"))    { B.LegendLure = EFVLegend::FogSerpent;   B.LegendLureMult = 8.f; }
		}
	}

	void BuildCosmetics(FFVDatabaseData& D)
	{
		// ---------------------------------------------------------------- hats
		AddHat(D, TEXT("hat_straw"), TEXT("Соломенная шляпа"), EFVHatStyle::Straw, 0, 1, FColor(232, 200, 112), FColor(200, 60, 50),
			TEXT("Старая добрая шляпа от солнца. Пахнет летним сеном."));
		AddHat(D, TEXT("hat_cap_red"), TEXT("Красная кепка"), EFVHatStyle::Cap, 150, 1, FColor(210, 52, 50), FColor(242, 242, 242),
			TEXT("Яркая кепка с длинным козырьком — солнце не слепит."));
		AddHat(D, TEXT("hat_cap_club"), TEXT("Кепка рыбклуба"), EFVHatStyle::Cap, 300, 1, FColor(60, 110, 190), FColor(250, 222, 90),
			TEXT("Кепка клуба рыболовов «Клёвая Долина». Носить с гордостью."));
		AddHat(D, TEXT("hat_bandana"), TEXT("Бандана"), EFVHatStyle::Bandana, 500, 2, FColor(202, 44, 54), FColor(250, 242, 232),
			TEXT("Повязал — и сразу чувствуешь себя морским волком."));
		AddHat(D, TEXT("hat_bucket"), TEXT("Панама рыбака"), EFVHatStyle::Bucket, 800, 2, FColor(112, 132, 82), FColor(82, 96, 60),
			TEXT("Мягкая панама с широкими полями. Классика жанра."));
		AddHat(D, TEXT("hat_beanie"), TEXT("Вязаная шапочка"), EFVHatStyle::Beanie, 1200, 3, FColor(232, 122, 52), FColor(250, 242, 222),
			TEXT("Тёплая шапочка с помпоном для рассветной рыбалки."));
		AddHat(D, TEXT("hat_cowboy"), TEXT("Ковбойская шляпа"), EFVHatStyle::Cowboy, 2500, 4, FColor(152, 102, 62), FColor(90, 56, 30),
			TEXT("Широкие поля, кожаный ремешок и немного Дикого Запада."));
		AddHat(D, TEXT("hat_ushanka"), TEXT("Ушанка"), EFVHatStyle::Ushanka, 4500, 5, FColor(112, 82, 62), FColor(212, 192, 162),
			TEXT("Настоящая меховая ушанка. В ней не страшен никакой мороз."));
		AddHat(D, TEXT("hat_pirate"), TEXT("Пиратская треуголка"), EFVHatStyle::Pirate, 8000, 6, FColor(50, 46, 62), FColor(232, 192, 72),
			TEXT("Йо-хо-хо! Рыба сама прыгает в садок от страха."));
		AddHat(D, TEXT("hat_wizard"), TEXT("Колпак волшебника"), EFVHatStyle::Wizard, 30000, 9, FColor(82, 62, 172), FColor(250, 222, 92),
			TEXT("Расшит звёздами. Говорят, помогает заговаривать клёв."));
		AddHat(D, TEXT("hat_crown"), TEXT("Корона"), EFVHatStyle::Crown, 0, 1, FColor(250, 202, 60), FColor(202, 50, 72),
			TEXT("Венец истинного рыбака. Вручается за первую пойманную легенду."));
		AddHat(D, TEXT("hat_captain"), TEXT("Треуголка капитана"), EFVHatStyle::Pirate, 0, 1, FColor(122, 32, 42), FColor(250, 212, 82),
			TEXT("Найдена в старом сундуке. Пахнет морем и приключениями."));

		// ---------------------------------------------------------------- outfits
		AddOutfit(D, TEXT("outfit_overalls"), TEXT("Рабочий комбинезон"), EFVOutfitStyle::Overalls, 0, 1, FColor(70, 110, 180), FColor(230, 220, 200),
			TEXT("Надёжный, как старый друг, и с кучей карманов для крючков."));
		AddOutfit(D, TEXT("outfit_shirt"), TEXT("Клетчатая рубашка"), EFVOutfitStyle::Shirt, 350, 1, FColor(196, 58, 50), FColor(60, 48, 70),
			TEXT("Уютная фланель в красную клетку."));
		AddOutfit(D, TEXT("outfit_hoodie"), TEXT("Уютное худи"), EFVOutfitStyle::Hoodie, 900, 2, FColor(118, 168, 90), FColor(240, 230, 210),
			TEXT("Мягкое худи с капюшоном — спасает от ветра и комаров."));
		AddOutfit(D, TEXT("outfit_vest"), TEXT("Рыбацкий жилет"), EFVOutfitStyle::Vest, 1600, 3, FColor(122, 130, 80), FColor(222, 202, 150),
			TEXT("Двадцать карманов! Где-то там точно завалялась блесна."));
		AddOutfit(D, TEXT("outfit_overalls_sunset"), TEXT("Комбинезон «Закат»"), EFVOutfitStyle::Overalls, 2800, 4, FColor(202, 98, 138), FColor(250, 222, 182),
			TEXT("Тот же удобный крой, но в цвет вечернего неба."));
		AddOutfit(D, TEXT("outfit_raincoat"), TEXT("Жёлтый дождевик"), EFVOutfitStyle::Raincoat, 3500, 5, FColor(248, 200, 50), FColor(70, 70, 82),
			TEXT("Дождь? Отличная погода, чтобы поймать сома!"));
		AddOutfit(D, TEXT("outfit_sailor"), TEXT("Матросский костюм"), EFVOutfitStyle::Sailor, 6000, 6, FColor(242, 242, 250), FColor(40, 70, 152),
			TEXT("Тельняшка и синий воротник-гюйс. Море зовёт!"));
		AddOutfit(D, TEXT("outfit_suit"), TEXT("Выходной костюм"), EFVOutfitStyle::Suit, 15000, 7, FColor(48, 54, 80), FColor(232, 232, 238),
			TEXT("Для торжественных случаев. Например, для встречи с легендой."));

		// ---------------------------------------------------------------- bobbers
		AddBobber(D, TEXT("bobber_classic"), TEXT("Классический поплавок"), EFVBobberStyle::Classic, 0, 1, FColor(224, 52, 52), FColor(246, 246, 246),
			TEXT("Красно-белый, точь-в-точь как у деда."));
		AddBobber(D, TEXT("bobber_duck"), TEXT("Поплавок-уточка"), EFVBobberStyle::Duck, 300, 1, FColor(250, 210, 60), FColor(240, 130, 40),
			TEXT("Резиновая уточка. Рыбы находят её очень забавной."));
		AddBobber(D, TEXT("bobber_star"), TEXT("Звёздочка"), EFVBobberStyle::Star, 1200, 3, FColor(250, 222, 80), FColor(255, 250, 200),
			TEXT("Сияет на воде, как упавшая звезда."));
		AddBobber(D, TEXT("bobber_heart"), TEXT("Сердечко"), EFVBobberStyle::Heart, 2000, 4, FColor(232, 80, 132), FColor(255, 202, 222),
			TEXT("Для тех, кто рыбачит с любовью."));
		AddBobber(D, TEXT("bobber_mushroom"), TEXT("Мухомор"), EFVBobberStyle::Mushroom, 3500, 5, FColor(204, 50, 50), FColor(250, 240, 222),
			TEXT("Красная шляпка в белый горошек. Есть не рекомендуется."));
		AddBobber(D, TEXT("bobber_golden"), TEXT("Золотой поплавок"), EFVBobberStyle::Golden, 0, 1, FColor(250, 200, 60), FColor(255, 240, 160),
			TEXT("Блестит даже в мутной воде. Встречается только в сундуках с сокровищами."));
		// update 5: the reward skins of the six legend achievements (Price 0 = never sold; RewardCosmetic of ach_legend_*)
		AddBobber(D, TEXT("bobber_tsar"), TEXT("Царский"), EFVBobberStyle::Tsar, 0, 1, FColor(250, 204, 64), FColor(255, 244, 180),
			TEXT("Золотой, с короной из трёх зубцов. Награда за Царь-Карпа."));
		AddBobber(D, TEXT("bobber_sturgeon"), TEXT("Осетровый"), EFVBobberStyle::Sturgeon, 0, 1, FColor(190, 204, 218), FColor(118, 138, 160),
			TEXT("Серебристый, с рядом костяных щитков. Награда за осётра."));
		AddBobber(D, TEXT("bobber_crystal"), TEXT("Хрустальный"), EFVBobberStyle::Crystal, 0, 1, FColor(170, 226, 250), FColor(240, 252, 255),
			TEXT("Прозрачный кристалл с бегущим бликом. Награда за форель."));
		AddBobber(D, TEXT("bobber_lantern"), TEXT("Фонарь удильщика"), EFVBobberStyle::Lantern, 0, 1, FColor(52, 60, 92), FColor(255, 240, 170),
			TEXT("С удилищем и шариком-фонарём, ночью ярче. Награда за Удильщика."));
		AddBobber(D, TEXT("bobber_fog"), TEXT("Туманный"), EFVBobberStyle::Fog, 0, 1, FColor(120, 160, 136), FColor(200, 232, 210),
			TEXT("Дымчато-зелёный, с завитком-змейкой. Награда за Змея."));
		AddBobber(D, TEXT("bobber_zhar"), TEXT("Жар-птица"), EFVBobberStyle::Zhar, 0, 1, FColor(255, 120, 32), FColor(255, 226, 110),
			TEXT("Огненный, с хохолком пламени и искрами. Награда за Жар-рыбу."));

		// ---------------------------------------------------------------- pets
		AddPet(D, TEXT("pet_duck"), TEXT("Утка Кряква"), EFVPetStyle::Duck, 1000, 2, FColor(250, 250, 240), FColor(250, 170, 50),
			TEXT("Важно вышагивает следом и крякает на каждую поклёвку."));
		AddPet(D, TEXT("pet_cat"), TEXT("Кот Мурзик"), EFVPetStyle::Cat, 1500, 2, FColor(232, 150, 70), FColor(250, 240, 222),
			TEXT("Рыжий кот. Сторожит улов — и иногда потихоньку его ворует."));
		AddPet(D, TEXT("pet_dog"), TEXT("Пёс Шарик"), EFVPetStyle::Dog, 2500, 3, FColor(172, 122, 72), FColor(240, 222, 192),
			TEXT("Верный друг, искренне рад любому улову."));
		AddPet(D, TEXT("pet_frog"), TEXT("Лягушка Квакша"), EFVPetStyle::Frog, 4000, 4, FColor(112, 190, 80), FColor(232, 240, 172),
			TEXT("Знает все болотные тропки и песни."));
		AddPet(D, TEXT("pet_fox"), TEXT("Лисёнок Огонёк"), EFVPetStyle::Fox, 8000, 6, FColor(232, 112, 42), FColor(250, 246, 236),
			TEXT("Хитрый и любопытный. Обожает сидеть на причале."));
		AddPet(D, TEXT("pet_penguin"), TEXT("Пингвинёнок Пинг"), EFVPetStyle::Penguin, 20000, 8, FColor(42, 46, 62), FColor(246, 246, 246),
			TEXT("Настоящий знаток рыбы. Приехал издалека ради ваших трофеев."));

		D.StartCosmetics = { FName(TEXT("hat_straw")), FName(TEXT("outfit_overalls")), FName(TEXT("bobber_classic")) };
		D.TreasureCosmetics = { FName(TEXT("bobber_golden")), FName(TEXT("hat_captain")) };
	}

	void BuildAchievements(FFVDatabaseData& D)
	{
		using K = EFVAchKind;
		const int32 NumFish = D.Fish.Num();
		const int32 NumLegends = D.Fish.FilterByPredicate([](const FFVFishDef& F) { return F.bLegendary; }).Num();
		const int32 NumRods = D.Rods.Num();
		const int32 NumLocations = D.Locations.Num();

		AddAch(D, TEXT("ach_first_catch"), TEXT("Первая поклёвка"), K::CatchTotal, 1, nullptr, 50, nullptr,
			TEXT("Поймайте свою первую рыбу."));
		AddAch(D, TEXT("ach_catch_50"), TEXT("Заправский рыбак"), K::CatchTotal, 50, nullptr, 500, nullptr,
			TEXT("Поймайте 50 рыб."));
		AddAch(D, TEXT("ach_catch_250"), TEXT("Рыбак от бога"), K::CatchTotal, 250, nullptr, 3000, nullptr,
			TEXT("Поймайте 250 рыб."));
		AddAch(D, TEXT("ach_crucian_25"), TEXT("Карасиный король"), K::CatchFish, 25, TEXT("fish_crucian"), 300, nullptr,
			TEXT("Поймайте 25 карасей."));
		AddAch(D, TEXT("ach_species_10"), TEXT("Натуралист"), K::CatchSpecies, 10, nullptr, 800, nullptr,
			TEXT("Поймайте 10 разных видов рыб."));
		AddAch(D, TEXT("ach_species_all"), TEXT("Ихтиолог"), K::CatchSpecies, NumFish, nullptr, 20000, nullptr,
			*FString::Printf(TEXT("Соберите полную коллекцию: все %d видов рыб."), NumFish));
		AddAch(D, TEXT("ach_complete_pond"), TEXT("Знаток пруда"), K::CompleteLocation, 1, TEXT("pond"), 800, nullptr,
			TEXT("Поймайте все виды рыб Деревенского пруда."));
		AddAch(D, TEXT("ach_first_legend"), TEXT("Легендарный улов"), K::CatchLegendary, 1, nullptr, 1000, TEXT("hat_crown"),
			TEXT("Поймайте легендарную рыбу. Награда — королевская корона!"));
		AddAch(D, TEXT("ach_legends"), TEXT("Охотник за легендами"), K::CatchLegendary, NumLegends, nullptr, 15000, nullptr,
			*FString::Printf(TEXT("Поймайте %d легендарных рыб."), NumLegends));
		// update 5: the legend of every location; the prize is a themed bobber skin (reward-only, see BuildCosmetics).
		// Players who caught a legend before 0.5 complete these at the next achievement check.
		AddAch(D, TEXT("ach_legend_pond"), TEXT("Повелитель пруда"), K::CatchFish, 1, TEXT("fish_tsar_carp"), 0, TEXT("bobber_tsar"),
			TEXT("Поймайте Царь-Карпа в Деревенском пруду."));
		AddAch(D, TEXT("ach_legend_river"), TEXT("Против течения"), K::CatchFish, 1, TEXT("fish_silver_sturgeon"), 0, TEXT("bobber_sturgeon"),
			TEXT("Поймайте Серебряного осётра в Лесной реке."));
		AddAch(D, TEXT("ach_legend_mountain"), TEXT("Хрустальная тишина"), K::CatchFish, 1, TEXT("fish_crystal_trout"), 0, TEXT("bobber_crystal"),
			TEXT("Поймайте Хрустальную форель в Горном озере."));
		AddAch(D, TEXT("ach_legend_ocean"), TEXT("Свет во тьме"), K::CatchFish, 1, TEXT("fish_giant_angler"), 0, TEXT("bobber_lantern"),
			TEXT("Поймайте Удильщика-исполина у Морского причала."));
		AddAch(D, TEXT("ach_legend_swamp"), TEXT("Сквозь туман"), K::CatchFish, 1, TEXT("fish_fog_serpent"), 0, TEXT("bobber_fog"),
			TEXT("Поймайте Туманного змея на Туманном болоте."));
		AddAch(D, TEXT("ach_legend_volcano"), TEXT("Укротитель пламени"), K::CatchFish, 1, TEXT("fish_zhar_ryba"), 0, TEXT("bobber_zhar"),
			TEXT("Поймайте Жар-рыбу в Вулканическом озере."));
		AddAch(D, TEXT("ach_perfect_10"), TEXT("Твёрдая рука"), K::PerfectTotal, 10, nullptr, 600, nullptr,
			TEXT("Поймайте 10 рыб идеально, ни разу не упустив их из полосы."));
		AddAch(D, TEXT("ach_perfect_50"), TEXT("Безупречный хват"), K::PerfectTotal, 50, nullptr, 3000, nullptr,
			TEXT("Поймайте 50 рыб идеально."));
		AddAch(D, TEXT("ach_treasure_10"), TEXT("Кладоискатель"), K::TreasureTotal, 10, nullptr, 1500, nullptr,
			TEXT("Выловите 10 сундуков с сокровищами."));
		AddAch(D, TEXT("ach_iridium_5"), TEXT("Иридиевый блеск"), K::IridiumTotal, 5, nullptr, 2500, nullptr,
			TEXT("Поймайте 5 рыб иридиевого качества."));
		AddAch(D, TEXT("ach_earn_50k"), TEXT("Рыбный магнат"), K::EarnTotal, 50000, nullptr, 5000, nullptr,
			TEXT("Заработайте 50 000 монет на продаже улова."));
		AddAch(D, TEXT("ach_level_5"), TEXT("Бывалый рыболов"), K::ReachLevel, 5, nullptr, 1000, nullptr,
			TEXT("Достигните 5-го уровня рыбалки."));
		AddAch(D, TEXT("ach_level_10"), TEXT("Мастер-рыболов"), K::ReachLevel, FV::ClassicMaxLevel, nullptr, 10000, nullptr,
			*FString::Printf(TEXT("Достигните %d-го уровня рыбалки."), FV::ClassicMaxLevel));
		AddAch(D, TEXT("ach_all_locations"), TEXT("Путешественник"), K::UnlockLocations, NumLocations, nullptr, 5000, nullptr,
			TEXT("Откройте все места для рыбалки."));
		AddAch(D, TEXT("ach_all_rods"), TEXT("Коллекционер удочек"), K::OwnRods, NumRods, nullptr, 10000, nullptr,
			TEXT("Соберите все удочки."));
		AddAch(D, TEXT("ach_fashion"), TEXT("Модник"), K::OwnCosmetics, 12, nullptr, 2000, nullptr,
			TEXT("Соберите 12 предметов гардероба."));

		// ---- update 2: house, crafts & trade (RewardDecor = reward-only house decor)
		AddAch(D, TEXT("ach_first_station"), TEXT("Новоселье"), K::StationsBuilt, 1, nullptr, 300, nullptr,
			TEXT("Постройте в доме первую станцию."));
		AddAch(D, TEXT("ach_all_stations"), TEXT("Хозяйство"), K::StationsBuilt, 8, nullptr, 5000, nullptr,
			TEXT("Постройте все 8 станций в доме."));
		AddAch(D, TEXT("ach_stations_max"), TEXT("Мастер на все руки"), K::StationsMaxed, 8, nullptr, 15000, nullptr,
			TEXT("Улучшите все 8 станций до максимального уровня. Награда — бонсай!"), TEXT("decor_plant_bonsai"));
		AddAch(D, TEXT("ach_gather_100"), TEXT("Собиратель"), K::GatherTotal, 100, nullptr, 800, nullptr,
			TEXT("Соберите 100 ресурсов."));
		AddAch(D, TEXT("ach_craft_50"), TEXT("Рукодельник"), K::CraftTotal, 50, nullptr, 1500, nullptr,
			TEXT("Смастерите 50 предметов в мастерской."));
		AddAch(D, TEXT("ach_chef"), TEXT("Шеф-повар"), K::CookTotal, 25, nullptr, 2000, nullptr,
			TEXT("Приготовьте 25 блюд. Награда — картина «Золотая рыбка»!"), TEXT("decor_paint_goldfish"));
		AddAch(D, TEXT("ach_smoke_30"), TEXT("Коптильщик"), K::SmokeTotal, 30, nullptr, 2500, nullptr,
			TEXT("Закоптите 30 рыб."));
		AddAch(D, TEXT("ach_dig_100"), TEXT("Землекоп"), K::DigTotal, 100, nullptr, 1000, nullptr,
			TEXT("Выкопайте 100 червей."));
		AddAch(D, TEXT("ach_nets"), TEXT("Сетевик"), K::NetFishTotal, 100, nullptr, 3000, nullptr,
			TEXT("Поймайте сетями 100 рыб. Награда — ковёр-рыба!"), TEXT("decor_rug_fish"));
		AddAch(D, TEXT("ach_orders"), TEXT("Надёжный поставщик"), K::OrdersDone, 10, nullptr, 3000, nullptr,
			TEXT("Выполните 10 заказов в срок. Награда — портрет Митрича!"), TEXT("decor_paint_mitrich"));
		AddAch(D, TEXT("ach_knots"), TEXT("Морской узел"), K::KnotsTotal, 20, nullptr, 1500, nullptr,
			TEXT("Свяжите 20 узлов без ошибок."));
		AddAch(D, TEXT("ach_lures"), TEXT("Исследователь блёсен"), K::LuresDiscovered, 12, nullptr, 3000, nullptr,
			TEXT("Откройте 12 рецептов блёсен и мушек."));
		AddAch(D, TEXT("ach_lures_all"), TEXT("Все блёсны"), K::LuresDiscovered, 24, nullptr, 8000, nullptr,
			TEXT("Откройте все 24 рецепта блёсен и мушек."));
		AddAch(D, TEXT("ach_aquarium"), TEXT("Аквариумист"), K::AquariumFish, 10, nullptr, 4000, nullptr,
			TEXT("Поселите в аквариуме 10 рыб. Награда — светильник-медуза!"), TEXT("decor_lamp_jelly"));
		AddAch(D, TEXT("ach_trophies"), TEXT("Трофейщик"), K::TrophiesMounted, 5, nullptr, 3000, nullptr,
			TEXT("Повесьте на стену 5 трофеев. Награда — обои с рыбками!"), TEXT("decor_wall_fish"));
		AddAch(D, TEXT("ach_decor"), TEXT("Уютный дом"), K::OwnDecor, 10, nullptr, 3000, nullptr,
			TEXT("Соберите 10 предметов обстановки для дома."));
	}

	void BuildKeepnet(FFVDatabaseData& D)
	{
		const int32 Caps[] = { 12, 20, 30, 45, 60 };
		const int32 Prices[] = { 0, 500, 2000, 6000, 15000 };
		for (int32 i = 0; i < int32(UE_ARRAY_COUNT(Caps)); ++i)
		{
			FFVKeepnetLevel& K = D.Keepnet.AddDefaulted_GetRef();
			K.Capacity = Caps[i];
			K.Price = Prices[i];
		}
	}

	void Validate(const FFVDatabaseData& D)
	{
		for (const FFVFishDef& F : D.Fish)
		{
			if (!D.LocationIdx.Contains(F.Location))
			{
				UE_LOG(LogFV, Error, TEXT("FV: fish '%s' has unknown location '%s'"), *F.Id.ToString(), *F.Location.ToString());
			}
		}
		for (const FFVAchievementDef& A : D.Achievements)
		{
			if (!A.RewardCosmetic.IsNone() && !D.CosmeticIdx.Contains(A.RewardCosmetic))
			{
				UE_LOG(LogFV, Error, TEXT("FV: achievement '%s' rewards unknown cosmetic '%s'"), *A.Id.ToString(), *A.RewardCosmetic.ToString());
			}
			if (A.Kind == EFVAchKind::CatchFish && !D.FishIdx.Contains(A.Param))
			{
				UE_LOG(LogFV, Error, TEXT("FV: achievement '%s' references unknown fish '%s'"), *A.Id.ToString(), *A.Param.ToString());
			}
			if (A.Kind == EFVAchKind::CompleteLocation && !D.LocationIdx.Contains(A.Param))
			{
				UE_LOG(LogFV, Error, TEXT("FV: achievement '%s' references unknown location '%s'"), *A.Id.ToString(), *A.Param.ToString());
			}
		}
		for (const FName& Id : D.StartCosmetics)
		{
			if (!D.CosmeticIdx.Contains(Id)) { UE_LOG(LogFV, Error, TEXT("FV: unknown starter cosmetic '%s'"), *Id.ToString()); }
		}
		for (const FName& Id : D.TreasureCosmetics)
		{
			if (!D.CosmeticIdx.Contains(Id)) { UE_LOG(LogFV, Error, TEXT("FV: unknown treasure cosmetic '%s'"), *Id.ToString()); }
		}
		// update 5: every legendary fish is exactly one of the six legends; its bait rule names a real bait and every rule
		// has its checklist line (the line / float ids are items: FVDatabaseHome.cpp validates them)
		bool Seen[int32(EFVLegend::Count)] = {};
		int32 NumSeen = 0;
		for (const FFVFishDef& F : D.Fish)
		{
			if (F.bExpert) { continue; }   // update 6: expert fish / legends have their own rules (FVDBU6, DATA-EXPERT)
			if (F.bLegendary != (F.Legend != EFVLegend::None))
			{
				UE_LOG(LogFV, Error, TEXT("FV: fish '%s': bLegendary and Legend disagree"), *F.Id.ToString());
			}
			if (F.Legend == EFVLegend::None || F.Legend >= EFVLegend::Count) { continue; }
			if (Seen[int32(F.Legend)]) { UE_LOG(LogFV, Error, TEXT("FV: legend %d is assigned twice ('%s')"), int32(F.Legend), *F.Id.ToString()); }
			else { Seen[int32(F.Legend)] = true; ++NumSeen; }
			const FFVLegendPrep& Pr = F.Prep;
			if (!Pr.Bait.IsNone() && !D.BaitIdx.Contains(Pr.Bait))
			{
				UE_LOG(LogFV, Error, TEXT("FV: legend '%s' requires unknown bait '%s'"), *F.Id.ToString(), *Pr.Bait.ToString());
			}
			const int32 Rules = ((!Pr.Bait.IsNone() || Pr.BaitStyle != EFVBaitStyle::Count) ? 1 : 0) + (Pr.Float.IsNone() ? 0 : 1)
				+ (Pr.Lines.Num() > 0 ? 1 : 0) + (Pr.SnapBelowLineTier > 0 ? 1 : 0);
			if (Pr.PrepItems.Num() != Rules)
			{
				UE_LOG(LogFV, Error, TEXT("FV: legend '%s' has %d checklist lines for %d rules"), *F.Id.ToString(), Pr.PrepItems.Num(), Rules);
			}
			if (Pr.PrepText.IsEmpty() || Pr.FightText.IsEmpty()) { UE_LOG(LogFV, Error, TEXT("FV: legend '%s' misses its texts"), *F.Id.ToString()); }
		}
		if (NumSeen != int32(EFVLegend::Count) - 1)
		{
			UE_LOG(LogFV, Error, TEXT("FV: %d of %d legends are assigned to fish"), NumSeen, int32(EFVLegend::Count) - 1);
		}
		for (const FFVBaitDef& B : D.Baits)
		{
			if (B.LegendLure >= EFVLegend::Count || !FMath::IsFinite(B.LegendLureMult) || B.LegendLureMult <= 0.f || !FMath::IsFinite(B.OtherFishMult) || B.OtherFishMult <= 0.f)
			{
				UE_LOG(LogFV, Error, TEXT("FV: bait '%s' has invalid legend lure data"), *B.Id.ToString());
			}
		}
	}

	void Build(FFVDatabaseData& D)
	{
		D.Locations.Reserve(8);
		D.Fish.Reserve(48);
		D.Cosmetics.Reserve(40);
		D.Achievements.Reserve(48);

		BuildLocations(D);
		BuildFish(D);
		BuildJunk(D);
		BuildRods(D);
		BuildBaits(D);
		BuildCosmetics(D);
		BuildAchievements(D);
		BuildKeepnet(D);

		// update 6: new rows are APPENDED here (FVDatabaseU6.h). The appenders must not call FVDB table getters
		// (this table is being built: FVDB::Fish() etc. would recurse).
		{
			FVDBU6::FClassicTables T;
			T.Locations = &D.Locations; T.Fish = &D.Fish; T.Junk = &D.Junk; T.Rods = &D.Rods; T.Baits = &D.Baits;
			T.Cosmetics = &D.Cosmetics; T.Achievements = &D.Achievements;
			FVDBU6::AppendClassicTables(T);
		}
		// update 6 (§3а, §14): «Ихтиолог» stays reachable before the City — the classic species only (not the expert fish,
		// not the ordinary fish of the City / Cave); UFVSaveGame counts CatchSpecies the same way.
		{
			int32 NumClassic = 0;
			for (const FFVFishDef& F : D.Fish)
			{
				if (!F.bExpert && !FVDBU6::IsExpertLocation(F.Location)) { ++NumClassic; }
			}
			for (FFVAchievementDef& A : D.Achievements)
			{
				if (A.Id != FName(TEXT("ach_species_all")) || NumClassic <= 0) { continue; }
				A.Target = NumClassic;
				A.Description = FString::Printf(TEXT("Соберите полную коллекцию долины: все %d видов рыб (кроме Города и Пещеры)."), NumClassic);
			}
		}

		BuildIndex(D.Locations, D.LocationIdx, TEXT("location"));
		BuildIndex(D.Fish, D.FishIdx, TEXT("fish"));
		BuildIndex(D.Junk, D.JunkIdx, TEXT("junk"));
		BuildIndex(D.Rods, D.RodIdx, TEXT("rod"));
		BuildIndex(D.Baits, D.BaitIdx, TEXT("bait"));
		BuildIndex(D.Cosmetics, D.CosmeticIdx, TEXT("cosmetic"));
		BuildIndex(D.Achievements, D.AchievementIdx, TEXT("achievement"));

		Validate(D);
		D.bBuilt = true;

		UE_LOG(LogFV, Log, TEXT("FV: database ready — %d locations, %d fish, %d junk, %d rods, %d baits, %d cosmetics, %d achievements"),
			D.Locations.Num(), D.Fish.Num(), D.Junk.Num(), D.Rods.Num(), D.Baits.Num(), D.Cosmetics.Num(), D.Achievements.Num());
	}

	const FFVDatabaseData& DB()
	{
		static FFVDatabaseData Data;
		if (!Data.bBuilt)
		{
			Build(Data);
		}
		return Data;
	}

	template <typename T>
	const T* FindIn(const TArray<T>& Arr, const TMap<FName, int32>& Index, FName Id)
	{
		if (Id.IsNone()) { return nullptr; }
		const int32* Idx = Index.Find(Id);
		return (Idx && Arr.IsValidIndex(*Idx)) ? &Arr[*Idx] : nullptr;
	}

	/** Picks a junk item; the location theme shifts what tends to get snagged. */
	const FFVJunkDef* PickJunk(const FFVDatabaseData& D, const FFVLocationDef* Loc, FRandomStream& Rng, const FFVBaitDef* Bait = nullptr)
	{
		if (D.Junk.Num() == 0) { return nullptr; }
		const EFVTheme Theme = Loc ? Loc->Theme : EFVTheme::Pond;
		// update 6 (§5.1, Р7): «Мусорная приманка» (bait_trash) — among the junk, scrap metal x3
		const bool bTrashBait = Bait && Bait->Style == EFVBaitStyle::Trash;
		TArray<float, TInlineAllocator<8>> Weights;
		float Total = 0.f;
		for (const FFVJunkDef& J : D.Junk)
		{
			float W = 10.f;
			switch (J.Style)
			{
			case EFVJunkStyle::Weed:      W = 30.f; break;
			case EFVJunkStyle::Boot:      W = 14.f; break;
			case EFVJunkStyle::Can:       W = 18.f; break;
			case EFVJunkStyle::Driftwood: W = 20.f; break;
			case EFVJunkStyle::Bottle:    W = 10.f; break;
			case EFVJunkStyle::Scrap:     W = bTrashBait ? 24.f : 8.f; break;   // update 6: junk_scrap (металлолом)
			default: break;
			}
			switch (Theme)
			{
			case EFVTheme::Ocean:    if (J.Style == EFVJunkStyle::Bottle) { W *= 2.5f; } if (J.Style == EFVJunkStyle::Driftwood) { W *= 1.3f; } break;
			case EFVTheme::Swamp:    if (J.Style == EFVJunkStyle::Weed || J.Style == EFVJunkStyle::Driftwood) { W *= 1.6f; } if (J.Style == EFVJunkStyle::Bottle) { W *= 0.5f; } break;
			case EFVTheme::River:    if (J.Style == EFVJunkStyle::Driftwood) { W *= 1.5f; } break;
			case EFVTheme::Mountain: if (J.Style == EFVJunkStyle::Can) { W *= 0.6f; } if (J.Style == EFVJunkStyle::Driftwood) { W *= 1.3f; } break;
			case EFVTheme::Volcano:  if (J.Style == EFVJunkStyle::Weed) { W *= 0.2f; } if (J.Style == EFVJunkStyle::Can || J.Style == EFVJunkStyle::Boot) { W *= 1.4f; } break;
			case EFVTheme::City:     if (J.Style == EFVJunkStyle::Scrap || J.Style == EFVJunkStyle::Can) { W *= 1.5f; } if (J.Style == EFVJunkStyle::Weed) { W *= 0.5f; } break;
			case EFVTheme::Cave:     if (J.Style == EFVJunkStyle::Scrap) { W *= 1.5f; } if (J.Style == EFVJunkStyle::Weed || J.Style == EFVJunkStyle::Bottle) { W *= 0.3f; } break;
			default: break;
			}
			W = FMath::Max(W, 0.01f);
			Weights.Add(W);
			Total += W;
		}
		float Roll = Rng.FRandRange(0.f, Total);
		for (int32 i = 0; i < D.Junk.Num(); ++i)
		{
			Roll -= Weights[i];
			if (Roll <= 0.f) { return &D.Junk[i]; }
		}
		return &D.Junk.Last();
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
}

// ================================================================================================
// FVDB
// ================================================================================================
namespace FVDB
{
	void Init()
	{
		DB();
		Items();   // update 2 tables (FVDatabaseHome.cpp): built after the base tables, validated there
	}

	const TArray<FFVLocationDef>& Locations() { return DB().Locations; }
	const TArray<FFVFishDef>& Fish() { return DB().Fish; }
	const TArray<FFVJunkDef>& Junk() { return DB().Junk; }
	const TArray<FFVRodDef>& Rods() { return DB().Rods; }
	const TArray<FFVBaitDef>& Baits() { return DB().Baits; }
	const TArray<FFVCosmeticDef>& Cosmetics() { return DB().Cosmetics; }
	const TArray<FFVAchievementDef>& Achievements() { return DB().Achievements; }
	const TArray<FFVKeepnetLevel>& KeepnetLevels() { return DB().Keepnet; }

	const FFVLocationDef* FindLocation(FName Id)
	{
		const FFVDatabaseData& D = DB();
		if (const FFVLocationDef* L = FindIn(D.Locations, D.LocationIdx, Id)) { return L; }
		// update 2: the yard and the house interior are not in Locations() (no fish, no progression)
		if (Id == HomeId()) { return &HomeLocation(); }
		if (Id == HouseId()) { return &HouseLocation(); }
		// update 6: the forest is a big location without water, not in Locations() either
		if (Id == FVDBU6::ForestId()) { return &FVDBU6::ForestLocation(); }
		return nullptr;
	}
	const FFVFishDef* FindFish(FName Id) { const FFVDatabaseData& D = DB(); return FindIn(D.Fish, D.FishIdx, Id); }
	const FFVJunkDef* FindJunk(FName Id) { const FFVDatabaseData& D = DB(); return FindIn(D.Junk, D.JunkIdx, Id); }
	const FFVRodDef* FindRod(FName Id) { const FFVDatabaseData& D = DB(); return FindIn(D.Rods, D.RodIdx, Id); }
	const FFVBaitDef* FindBait(FName Id) { const FFVDatabaseData& D = DB(); return FindIn(D.Baits, D.BaitIdx, Id); }
	const FFVCosmeticDef* FindCosmetic(FName Id) { const FFVDatabaseData& D = DB(); return FindIn(D.Cosmetics, D.CosmeticIdx, Id); }
	const FFVAchievementDef* FindAchievement(FName Id) { const FFVDatabaseData& D = DB(); return FindIn(D.Achievements, D.AchievementIdx, Id); }

	TArray<const FFVFishDef*> FishAt(FName LocationId)
	{
		TArray<const FFVFishDef*> Out;
		for (const FFVFishDef& F : DB().Fish)
		{
			if (F.Location == LocationId) { Out.Add(&F); }
		}
		// by rarity, common first (0.6.1 fix: the update-6 fish were appended after the legend — «Жерех» came last at the
		// river); stable, so the table order stays inside one rarity
		Algo::StableSortBy(Out, [](const FFVFishDef* F) { return uint8(F->Rarity); });
		return Out;
	}

	TArray<const FFVCosmeticDef*> CosmeticsFor(EFVCosmeticSlot Slot)
	{
		TArray<const FFVCosmeticDef*> Out;
		for (const FFVCosmeticDef& C : DB().Cosmetics)
		{
			if (C.Slot == Slot) { Out.Add(&C); }
		}
		return Out;
	}

	int32 LocationIndex(FName LocationId)
	{
		const int32* Idx = DB().LocationIdx.Find(LocationId);
		return Idx ? *Idx : INDEX_NONE;
	}

	bool IsStarterCosmetic(FName CosmeticId)
	{
		return !CosmeticId.IsNone() && DB().StartCosmetics.Contains(CosmeticId);
	}

	bool IsTreasureCosmetic(FName CosmeticId)
	{
		return !CosmeticId.IsNone() && DB().TreasureCosmetics.Contains(CosmeticId);
	}

	FString CosmeticSourceText(FName CosmeticId)
	{
		const FFVCosmeticDef* C = FindCosmetic(CosmeticId);
		if (!C) { return FString(); }
		if (IsStarterCosmetic(CosmeticId)) { return TEXT("Есть с самого начала"); }
		if (C->Price > 0)
		{
			return C->MinLevel > 1
				? FString::Printf(TEXT("В лавке: %s монет, с %d-го уровня"), *FV::Money(C->Price), C->MinLevel)
				: FString::Printf(TEXT("В лавке: %s монет"), *FV::Money(C->Price));
		}
		if (IsTreasureCosmetic(CosmeticId)) { return TEXT("Можно найти в сундуке с сокровищами"); }
		for (const FFVAchievementDef& A : DB().Achievements)
		{
			if (A.RewardCosmetic == CosmeticId)
			{
				return FString::Printf(TEXT("Награда за достижение «%s»"), *A.Name);
			}
		}
		return TEXT("Особая награда");
	}

	// ---------------------------------------------------------------- new game
	int64 StartMoney() { return 150; }
	FName StartLocation() { const FFVDatabaseData& D = DB(); return D.Locations.Num() > 0 ? D.Locations[0].Id : FName(TEXT("pond")); }
	FName StartRod() { const FFVDatabaseData& D = DB(); return D.Rods.Num() > 0 ? D.Rods[0].Id : FName(TEXT("rod_bamboo")); }
	TArray<FName> StartCosmetics() { return DB().StartCosmetics; }

	// ---------------------------------------------------------------- progression
	int32 XpForLevel(int32 Level)
	{
		// Stardew Valley fishing curve: total XP needed to REACH level L. Update 6 (§9, Р15): levels 11-15 take twice the
		// play time of the last classic steps; XP stops at 10 000 until the first arrival in the City (Р1).
		static const int32 Table[] = { 0, 0, 100, 380, 770, 1300, 2150, 3300, 4800, 6900, 10000, 13100, 19300, 26700, 35500, 45900 };
		static_assert(UE_ARRAY_COUNT(Table) == FV::MaxLevel + 1, "one XP threshold per level");
		constexpr int32 TableMax = int32(UE_ARRAY_COUNT(Table)) - 1;
		const int32 L = FMath::Clamp(Level, 1, FMath::Min(FV::MaxLevel, TableMax));
		return Table[L];
	}

	int32 LevelForXp(int32 Xp)
	{
		for (int32 L = FV::MaxLevel; L > 1; --L)
		{
			if (Xp >= XpForLevel(L)) { return L; }
		}
		return 1;
	}

	float LevelBarBonus(int32 Level)
	{
		// update 6: the classic bar stops growing at level 10 (the expert fight has its own stats)
		return 8.f * float(FMath::Clamp(Level, 1, FV::ClassicMaxLevel) - 1);
	}

	TArray<FString> UnlocksAtLevel(int32 Level)
	{
		TArray<FString> Lines;
		if (Level < 2 || Level > FV::MaxLevel) { return Lines; }
		const FFVDatabaseData& D = DB();

		const int32 Bar = FMath::RoundToInt(LevelBarBonus(Level) - LevelBarBonus(Level - 1));
		if (Bar > 0) { Lines.Add(FString::Printf(TEXT("Полоса поимки +%d"), Bar)); }
		// update 6 (§1 «Цепочка прогрессии», §9)
		if (Level == FVDBU6::ForestLocation().MinLevel)
		{
			Lines.Add(FString::Printf(TEXT("Новая локация: %s (тропа от Лесной реки)"), *FVDBU6::ForestLocation().Name));
		}
		if (Level == FV::ClassicMaxLevel)
		{
			Lines.Add(TEXT("Опыт замирает до Города: почините лодку на причале"));
		}
		if (Level == FV::ClassicMaxLevel + 1)
		{
			Lines.Add(TEXT("Экспертная рыбалка: экспертные удочки и элитный улов"));
		}

		for (const FFVLocationDef& L : D.Locations)
		{
			if (L.MinLevel == Level && L.UnlockPrice > 0)
			{
				Lines.Add(FString::Printf(TEXT("Новая локация: %s (%s монет)"), *L.Name, *FV::Money(L.UnlockPrice)));
			}
			else if (L.MinLevel == Level && L.UnlockPrice <= 0)
			{
				// update 6: reached by the boat / the pickaxe / the forest footbridge, not bought — say how
				const TCHAR* How = FVMill::IsMillId(L.Id) ? TEXT("почините мостик через ручей в лесу")
					: (L.Id == FVDBU6::CityId() ? TEXT("почините лодку на Морском причале")
					: (L.Id == FVDBU6::CaveId() ? TEXT("пробейте завалы в Городе киркой") : nullptr));
				Lines.Add(How ? FString::Printf(TEXT("Новая локация: %s — %s"), *L.Name, How)
					: FString::Printf(TEXT("Открывается: %s"), *L.Name));
			}
		}
		for (const FFVRodDef& R : D.Rods)
		{
			if (R.MinLevel != Level) { continue; }
			// 0.6.2 (Р80): forged rods and the rods behind an achievement are not «в лавке» — say where they come from
			if (FVDBU6::RodRecipe(R.Id) || (FVDBU6::IsRodSold(R) && !R.RequiredAchievement.IsNone()))
			{
				Lines.Add(FString::Printf(TEXT("%s (%s): %s"), *R.Name, *FVDBU6::RodTypeLabel(R), *FVDBU6::RodSourceText(R)));
			}
			else if (FVDBU6::IsRodSold(R))
			{
				Lines.Add(FString::Printf(TEXT("В лавке: %s (%s)"), *R.Name, *FVDBU6::RodTypeLabel(R)));
			}
		}
		for (const FFVBaitDef& B : D.Baits)
		{
			if (B.MinLevel == Level && B.PackPrice > 0)
			{
				Lines.Add(FString::Printf(TEXT("Новая наживка: %s"), *B.Name));
			}
		}
		for (const FFVFishDef& F : D.Fish)
		{
			if (F.MinLevel != Level) { continue; }
			const FFVLocationDef* L = FindIn(D.Locations, D.LocationIdx, F.Location);
			// Fish of locations that open at this very level are implied by the location line.
			if (L && L->MinLevel >= Level && !F.bLegendary) { continue; }
			const FString Where = L ? L->Name : FString();
			Lines.Add(F.bLegendary
				? FString::Printf(TEXT("Проснулась легенда: %s"), *Where)
				: FString::Printf(TEXT("Теперь клюёт: %s (%s)"), *F.Name, *Where));
		}
		TArray<FString> Wardrobe;
		for (const FFVCosmeticDef& C : D.Cosmetics)
		{
			if (C.MinLevel == Level && C.Price > 0) { Wardrobe.Add(C.Name); }
		}
		if (Wardrobe.Num() > 0)
		{
			Lines.Add(FString::Printf(TEXT("Обновки в лавке: %s"), *JoinNames(Wardrobe, 3)));
		}
		return Lines;
	}

	// ---------------------------------------------------------------- fishing balance
	bool IsFishAvailable(const FFVFishDef& F, uint8 PhaseMask, EFVWeather Weather, int32 Level)
	{
		const uint8 Phase = (PhaseMask & FVTime::Any) != 0 ? uint8(PhaseMask & FVTime::Any) : FVTime::Any;
		if ((F.TimeMask & Phase) == 0) { return false; }
		if (F.Weather == EFVWeatherReq::Sunny && Weather != EFVWeather::Sunny) { return false; }
		if (F.Weather == EFVWeatherReq::Rain && Weather != EFVWeather::Rain) { return false; }
		return Level >= F.MinLevel;
	}

	void PickCatch(const FFVCatchContext& Ctx, FRandomStream& Rng, const FFVFishDef*& OutFish, const FFVJunkDef*& OutJunk,
		const FFVFishDef** OutGated)
	{
		OutFish = nullptr;
		OutJunk = nullptr;
		if (OutGated) { *OutGated = nullptr; }   // update 5: the first legend LegendGate (NoBite) kept away
		const FFVDatabaseData& D = DB();
		const FFVLocationDef* Loc = FindIn(D.Locations, D.LocationIdx, Ctx.Location);
		const float Depth = FMath::Clamp(Ctx.Depth, 0.f, 1.f);
		// Luck: rod + tackle/food (Ctx.ExtraLuck, a value field so Ctx.Rod can keep pointing at the static rod def)
		const float Luck = FMath::Max(0.f, (Ctx.Rod ? Ctx.Rod->Luck : 0.f) + (FMath::IsFinite(Ctx.ExtraLuck) ? Ctx.ExtraLuck : 0.f));
		const float RareBonus = Ctx.Bait ? FMath::Max(0.f, Ctx.Bait->RareBonus) : 0.f;
		const FFVBaitDef* AffinityBait = (Ctx.Bait && Ctx.Bait->AffinityMult > 0.f) ? Ctx.Bait : nullptr;
		const FFVBaitDef* LegendBait = (Ctx.Bait && Ctx.Bait->LegendLure != EFVLegend::None) ? Ctx.Bait : nullptr;   // update 5

		// ---- junk?
		float JunkChance = Loc ? Loc->JunkChance : 0.2f;
		if (Ctx.Bait) { JunkChance *= FMath::Max(0.f, Ctx.Bait->JunkMult); }
		else if (!Ctx.bNet) { JunkChance *= 1.15f; }                           // an empty hook snags more rubbish (not nets)
		JunkChance *= FMath::IsFinite(Ctx.JunkMultScale) ? FMath::Max(0.f, Ctx.JunkMultScale) : 1.f;   // tackle / food / net
		JunkChance *= 1.f - 0.45f * Depth;                                     // less rubbish in deep water
		JunkChance = FMath::Clamp(JunkChance, 0.02f, 0.75f);

		// ---- fish that bite right now (time / weather / level)
		TArray<const FFVFishDef*, TInlineAllocator<16>> Available;
		if (Loc)
		{
			for (const FFVFishDef& F : D.Fish)
			{
				if (Ctx.bNet && F.bLegendary) { continue; }   // legends never swim into a net
				if (F.bExpert) { continue; }                   // update 6: expert fish only bite in the expert fight (FVDBU6::PickExpertCatch)
				if (F.Location == Loc->Id && IsFishAvailable(F, Ctx.PhaseMask, Ctx.Weather, Ctx.Level))
				{
					if (!FVMill::IsFishAllowed(Ctx, F)) { continue; }   // update 6 (§18): Omut-only fish of the mill
					// update 5: without its hard tackle rule a legend does not bite at all; the first one that would have
					// been there (time, weather, level AND depth) is reported for the «once per visit» toast
					if (F.Legend != EFVLegend::None && LegendGate(F, Ctx).Result == EFVLegendGate::NoBite)
					{
						if (OutGated && !*OutGated && F.MinDepth <= Depth + KINDA_SMALL_NUMBER) { *OutGated = &F; }
						continue;
					}
					Available.Add(&F);
				}
			}
		}

		const bool bJunkRoll = Rng.FRand() < JunkChance;
		if (bJunkRoll || Available.Num() == 0)
		{
			OutJunk = PickJunk(D, Loc, Rng, Ctx.Bait);
			if (OutJunk) { return; }
		}

		// ---- depth filter + weights
		TArray<const FFVFishDef*, TInlineAllocator<16>> Cands;
		TArray<float, TInlineAllocator<16>> Weights;
		float Total = 0.f;
		for (const FFVFishDef* F : Available)
		{
			if (F->MinDepth > Depth + KINDA_SMALL_NUMBER) { continue; }
			float W = FMath::Max(0.f, F->Weight);
			if (F->bLegendary)
			{
				W *= (1.f + 0.5f * (Luck + RareBonus)) * (0.75f + 0.5f * Depth);
			}
			else if (F->Rarity >= EFVRarity::Rare)
			{
				W *= (1.f + Luck + RareBonus) * (0.75f + 0.5f * Depth);
			}
			else if (F->Rarity == EFVRarity::Common)
			{
				W *= 1.15f - 0.3f * Depth;   // small fry thin out in deep water
			}
			if (LegendBait && F->Legend == LegendBait->LegendLure)
			{
				// update 5: a legend's favourite bait — the stronger of the two pulls, not both (black lures already love legends)
				W *= FMath::Max(AffinityBait ? LureAffinity(*AffinityBait, *F) : 1.f, LegendBait->LegendLureMult);
			}
			else
			{
				if (AffinityBait) { W *= LureAffinity(*AffinityBait, *F); }   // lures / dough attract their favourite fish
				if (LegendBait) { W *= LegendBait->OtherFishMult; }          // bait_ember: everyone else bites lazily
			}
			W *= FVMill::CatchWeightMult(Ctx, *F);   // update 6 (§18): mill baits, feeder decks, fish pen (1 elsewhere)
			if (W <= 0.f) { continue; }
			Cands.Add(F);
			Weights.Add(W);
			Total += W;
		}

		if (Cands.Num() > 0 && Total > 0.f)
		{
			float Roll = Rng.FRandRange(0.f, Total);
			for (int32 i = 0; i < Cands.Num(); ++i)
			{
				Roll -= Weights[i];
				if (Roll <= 0.f) { OutFish = Cands[i]; break; }
			}
			if (!OutFish) { OutFish = Cands.Last(); }
			return;
		}

		// ---- the bobber is too shallow for everything that bites now: the easiest available fish takes it
		for (const FFVFishDef* F : Available)
		{
			if (!OutFish || F->Difficulty < OutFish->Difficulty || (F->Difficulty == OutFish->Difficulty && F->MinDepth < OutFish->MinDepth))
			{
				OutFish = F;
			}
		}
		if (OutFish) { return; }

		// ---- last resort: never return nothing
		OutJunk = PickJunk(D, Loc, Rng, Ctx.Bait);
		if (!OutJunk)
		{
			const TArray<const FFVFishDef*> Here = Loc ? FishAt(Loc->Id) : TArray<const FFVFishDef*>();
			for (const FFVFishDef* F : Here) { if (F && !F->bExpert) { OutFish = F; break; } }   // update 6: never an expert fish
			if (!OutFish) { OutFish = D.Fish.Num() > 0 ? &D.Fish[0] : nullptr; }
		}
	}

	float RollBiteDelay(FRandomStream& Rng)
	{
		// Mostly 3..6 s (triangular, peak ~4.5 s); now and then a quick 2.5 s bite or a long 9 s wait.
		if (Rng.FRand() < 0.75f)
		{
			return 3.f + 3.f * 0.5f * (Rng.FRand() + Rng.FRand());
		}
		return Rng.FRandRange(2.5f, 9.f);
	}

	float RollSizeFrac(int32 Level, bool bPerfect, float CatchSeconds, const FFVBaitDef* Bait, FRandomStream& Rng)
	{
		const int32 L = FMath::Clamp(Level, 1, FV::ClassicMaxLevel);   // update 6: levels 11-15 do not grow classic fish
		float Frac = 0.05f + 0.55f * Rng.FRand();              // base roll 0.05..0.60
		Frac += 0.035f * float(L - 1);                          // +0.315 at level 10
		if (bPerfect) { Frac += 0.08f; }
		if (Bait) { Frac += FMath::Clamp(Bait->SizeBonus, 0.f, 0.3f); }
		if (CatchSeconds > 0.f && CatchSeconds < 5.f)
		{
			Frac += 0.06f * (1.f - CatchSeconds / 5.f);         // a quick, confident catch lands a slightly bigger fish
		}
		return FMath::Clamp(Frac, 0.f, 1.f);
	}

	EFVQuality RollQuality(float SizeFrac, bool bPerfect, FRandomStream& /*Rng*/)
	{
		int32 Q = SizeFrac < 0.33f ? 0 : (SizeFrac < 0.66f ? 1 : 2);   // Normal / Silver / Gold
		if (bPerfect) { Q += 1; }                                       // Gold + perfect = Iridium
		return EFVQuality(FMath::Clamp(Q, 0, int32(EFVQuality::Iridium)));
	}

	int32 CatchXp(const FFVFishDef& F, EFVQuality Q, bool bPerfect)
	{
		const int32 QI = FMath::Clamp(int32(Q), 0, int32(EFVQuality::Iridium));
		float Xp = float((QI + 1) * 3) + float(FMath::Max(0, F.Difficulty)) / 3.f;
		if (bPerfect) { Xp *= 1.4f; }
		if (F.bLegendary) { Xp *= 2.f; }
		return FMath::Max(1, FMath::RoundToInt(Xp));
	}

	int32 JunkXp()
	{
		return FMath::RandRange(1, 3);
	}

	FFVTreasure RollTreasure(const FFVLocationDef& Loc, int32 Level, FRandomStream& Rng)
	{
		const FFVDatabaseData& D = DB();
		FFVTreasure T;

		static const float LocMult[] = { 1.f, 2.f, 3.5f, 6.f, 9.f, 14.f };
		const int32 Idx = FMath::Clamp(LocationIndex(Loc.Id), 0, int32(UE_ARRAY_COUNT(LocMult)) - 1);
		const int32 L = FMath::Clamp(Level, 1, FV::ClassicMaxLevel);   // update 6: classic treasure keeps its 0.5 scale
		const float Coins = Rng.FRandRange(40.f, 110.f) * LocMult[Idx] * (1.f + 0.05f * float(L - 1));
		T.Money = FMath::Max<int64>(10, int64(FMath::RoundToInt(Coins / 5.f)) * 5);

		// ~35%: a handful of bait the player can already use (cheaper bait is more common)
		if (Rng.FRand() < 0.35f && D.Baits.Num() > 0)
		{
			TArray<const FFVBaitDef*, TInlineAllocator<8>> Pool;
			TArray<float, TInlineAllocator<8>> W;
			float Total = 0.f;
			// Only baits sold in shops (crafted / dug baits have PackPrice 0); weight = sellable count - rank
			// (earlier = cheaper = more common), the same weights as before update 2.
			int32 NumSellable = 0;
			for (const FFVBaitDef& B : D.Baits) { if (B.PackPrice > 0) { ++NumSellable; } }
			int32 Rank = 0;
			for (int32 i = 0; i < D.Baits.Num(); ++i)
			{
				if (D.Baits[i].PackPrice <= 0) { continue; }
				const float Wi = float(NumSellable - Rank);
				++Rank;
				if (D.Baits[i].MinLevel > L) { continue; }
				Pool.Add(&D.Baits[i]);
				W.Add(Wi);
				Total += Wi;
			}
			if (Pool.Num() > 0)
			{
				float Roll = Rng.FRandRange(0.f, Total);
				const FFVBaitDef* Pick = Pool.Last();
				for (int32 i = 0; i < Pool.Num(); ++i)
				{
					Roll -= W[i];
					if (Roll <= 0.f) { Pick = Pool[i]; break; }
				}
				T.BaitId = Pick->Id;
				T.BaitCount = Rng.RandRange(3, 8);
			}
		}

		// ~6%: a treasure-only cosmetic (UFVGame converts it to coins if already owned)
		if (D.TreasureCosmetics.Num() > 0 && Rng.FRand() < 0.06f)
		{
			T.CosmeticId = D.TreasureCosmetics[Rng.RandRange(0, D.TreasureCosmetics.Num() - 1)];
		}

		// ~12% (update 2): an item for the backpack (glitter, tackle; rarer tackle at higher levels)
		if (Rng.FRand() < 0.12f)
		{
			struct FDrop { const TCHAR* Id; float W; int32 MinLevel; int32 MinCount; int32 MaxCount; };
			static const FDrop Drops[] = {
				{ TEXT("mat_glitter"),   44.f, 1, 1, 2 },
				{ TEXT("float_feather"), 20.f, 1, 1, 1 },
				{ TEXT("line_linen"),    20.f, 1, 1, 1 },
				{ TEXT("line_braided"),  10.f, 3, 1, 1 },
				{ TEXT("float_glow"),     5.f, 5, 1, 1 },
				{ TEXT("float_gold"),     1.f, 9, 1, 1 },
			};
			float DropTotal = 0.f;
			for (const FDrop& Dr : Drops)
			{
				if (Dr.MinLevel <= L && FindItem(FName(Dr.Id))) { DropTotal += Dr.W; }
			}
			if (DropTotal > 0.f)
			{
				float Roll = Rng.FRandRange(0.f, DropTotal);
				for (const FDrop& Dr : Drops)
				{
					if (Dr.MinLevel > L || !FindItem(FName(Dr.Id))) { continue; }
					Roll -= Dr.W;
					if (Roll <= 0.f)
					{
						T.ItemId = FName(Dr.Id);
						T.ItemCount = Rng.RandRange(Dr.MinCount, Dr.MaxCount);
						break;
					}
				}
			}
		}
		return T;
	}

	// ---------------------------------------------------------------- economy
	float QualityPriceMult(EFVQuality Q)
	{
		switch (Q)
		{
		case EFVQuality::Silver:  return 1.25f;
		case EFVQuality::Gold:    return 1.5f;
		case EFVQuality::Iridium: return 2.f;
		default:                  return 1.f;
		}
	}

	int32 FishPrice(const FFVFishDef& F, EFVQuality Q)
	{
		return FMath::Max(1, FMath::RoundToInt(float(F.BasePrice) * QualityPriceMult(Q)));
	}

	int32 ItemPrice(FName ItemId, EFVQuality Q)
	{
		if (const FFVFishDef* F = FindFish(ItemId)) { return FishPrice(*F, Q); }
		if (const FFVJunkDef* J = FindJunk(ItemId)) { return FMath::Max(0, J->Price); }
		if (const FFVItemDef* I = FindItem(ItemId))
		{
			if (I->SellPrice <= 0) { return 0; }
			// dishes are worth more when cooked better; every other item has a flat price
			return I->Kind == EFVItemKind::Food
				? FMath::Max(1, FMath::RoundToInt(float(I->SellPrice) * QualityPriceMult(Q)))
				: I->SellPrice;
		}
		return 0;
	}

	// ---------------------------------------------------------------- presentation
	FString RarityName(EFVRarity R)
	{
		switch (R)
		{
		case EFVRarity::Common:    return TEXT("Обычная");
		case EFVRarity::Uncommon:  return TEXT("Необычная");
		case EFVRarity::Rare:      return TEXT("Редкая");
		case EFVRarity::Epic:      return TEXT("Эпическая");
		case EFVRarity::Legendary: return TEXT("Легендарная");
		default:                   return TEXT("—");
		}
	}

	FLinearColor RarityColor(EFVRarity R)
	{
		switch (R)
		{
		case EFVRarity::Common:    return FV::Col(226, 222, 212);
		case EFVRarity::Uncommon:  return FV::Col(112, 204, 72);
		case EFVRarity::Rare:      return FV::Col(76, 150, 240);
		case EFVRarity::Epic:      return FV::Col(182, 98, 240);
		case EFVRarity::Legendary: return FV::Col(255, 198, 40);
		default:                   return FV::Col(226, 222, 212);
		}
	}

	FString QualityName(EFVQuality Q)
	{
		switch (Q)
		{
		case EFVQuality::Normal:  return TEXT("Обычное");
		case EFVQuality::Silver:  return TEXT("Серебряное");
		case EFVQuality::Gold:    return TEXT("Золотое");
		case EFVQuality::Iridium: return TEXT("Иридиевое");
		default:                  return TEXT("Обычное");
		}
	}

	FLinearColor QualityColor(EFVQuality Q)
	{
		switch (Q)
		{
		case EFVQuality::Silver:  return FV::Col(206, 216, 232);
		case EFVQuality::Gold:    return FV::Col(255, 204, 51);
		case EFVQuality::Iridium: return FV::Col(204, 88, 232);
		default:                  return FV::Col(236, 228, 210);
		}
	}

	FString MotionName(EFVMotion M)
	{
		switch (M)
		{
		case EFVMotion::Mixed:   return TEXT("Непредсказуемая");
		case EFVMotion::Dart:    return TEXT("Резкие рывки");
		case EFVMotion::Smooth:  return TEXT("Плавная");
		case EFVMotion::Sinker:  return TEXT("Уходит на дно");
		case EFVMotion::Floater: return TEXT("Всплывает");
		default:                 return TEXT("—");
		}
	}

	FString TimeMaskText(uint8 Mask)
	{
		const uint8 M = Mask & FVTime::Any;
		if (M == FVTime::Any) { return TEXT("Любое время"); }
		if (M == 0) { return TEXT("Никогда"); }
		FString Out;
		for (uint8 P = 0; P < uint8(EFVDayPhase::Count); ++P)
		{
			if (M & FVTime::Bit(EFVDayPhase(P)))
			{
				if (!Out.IsEmpty()) { Out += TEXT(", "); }
				Out += PhaseName(EFVDayPhase(P));
			}
		}
		return Out;
	}

	FString WeatherReqText(EFVWeatherReq W)
	{
		switch (W)
		{
		case EFVWeatherReq::Sunny: return TEXT("Только в ясную погоду");
		case EFVWeatherReq::Rain:  return TEXT("Только в дождь");
		default:                   return TEXT("Любая погода");
		}
	}

	FString WeatherName(EFVWeather W)
	{
		switch (W)
		{
		case EFVWeather::Rain: return TEXT("Дождь");
		default:               return TEXT("Ясно");
		}
	}

	FString PhaseName(EFVDayPhase P)
	{
		switch (P)
		{
		case EFVDayPhase::Morning: return TEXT("Утро");
		case EFVDayPhase::Day:     return TEXT("День");
		case EFVDayPhase::Evening: return TEXT("Вечер");
		case EFVDayPhase::Night:   return TEXT("Ночь");
		default:                   return TEXT("—");
		}
	}

	FString SlotName(EFVCosmeticSlot S)
	{
		switch (S)
		{
		case EFVCosmeticSlot::Hat:    return TEXT("Шляпа");
		case EFVCosmeticSlot::Outfit: return TEXT("Одежда");
		case EFVCosmeticSlot::Bobber: return TEXT("Поплавок");
		case EFVCosmeticSlot::Pet:    return TEXT("Питомец");
		default:                      return TEXT("—");
		}
	}

	FString ItemName(FName ItemId)
	{
		if (ItemId.IsNone()) { return TEXT("—"); }
		if (const FFVFishDef* F = FindFish(ItemId)) { return F->Name; }
		if (const FFVJunkDef* J = FindJunk(ItemId)) { return J->Name; }
		if (const FFVRodDef* R = FindRod(ItemId)) { return R->Name; }
		if (const FFVBaitDef* B = FindBait(ItemId)) { return B->Name; }
		if (const FFVCosmeticDef* C = FindCosmetic(ItemId)) { return C->Name; }
		if (const FFVLocationDef* L = FindLocation(ItemId)) { return L->Name; }
		if (const FFVAchievementDef* A = FindAchievement(ItemId)) { return A->Name; }
		// update 2
		if (const FFVItemDef* I = FindItem(ItemId)) { return I->Name; }
		if (const FFVDecorDef* Dc = FindDecor(ItemId)) { return Dc->Name; }
		if (const FFVStationDef* S = FindStation(ItemId)) { return S->Name; }
		if (const FFVTraderDef* T = FindTrader(ItemId)) { return T->Name; }
		if (ItemId == AnyFishToken()) { return TEXT("Любая рыба"); }
		// update 5: recipe tokens «a fish of this rarity»
		if (ItemId == AnyFishRareToken()) { return TEXT("Редкая рыба"); }
		if (ItemId == AnyFishEpicToken()) { return TEXT("Эпическая рыба"); }
		if (ItemId == AnyFishLegendToken()) { return TEXT("Легендарная рыба"); }
		if (const FFVRecipeDef* R = FindRecipe(ItemId)) { return ItemName(R->OutputId); }
		return ItemId.ToString();
	}

	FString DifficultyText(int32 Difficulty)
	{
		if (Difficulty < 30) { return TEXT("Легко"); }
		if (Difficulty < 50) { return TEXT("Средне"); }
		if (Difficulty < 70) { return TEXT("Сложно"); }
		if (Difficulty < 95) { return TEXT("Очень сложно"); }
		return TEXT("Легендарно");
	}

	// ============================================================================================
	// UPDATE 5: legends — tackle gate & checklist, rod stat composition, the catch bar
	// ============================================================================================
	FFVLegendGateResult LegendGate(const FFVFishDef& Def, const FFVCatchContext& Ctx)
	{
		FFVLegendGateResult R;
		if (Def.Legend == EFVLegend::None)
		{
			return R;   // ordinary fish: nothing to prepare
		}
		const FFVLegendPrep& Pr = Def.Prep;
		// hard: the bait (a bait id and / or any bait of a style)
		const bool bBaitRule = !Pr.Bait.IsNone() || Pr.BaitStyle != EFVBaitStyle::Count;
		if (bBaitRule)
		{
			const bool bBaitOk = Ctx.Bait && ((!Pr.Bait.IsNone() && Ctx.Bait->Id == Pr.Bait)
				|| (Pr.BaitStyle != EFVBaitStyle::Count && Ctx.Bait->Style == Pr.BaitStyle));
			if (!bBaitOk) { R.Missing |= FVLegendMissing::Bait; }
		}
		// hard: the float and the kind of line
		if (!Pr.Float.IsNone() && Ctx.Float != Pr.Float) { R.Missing |= FVLegendMissing::Float; }
		if (Pr.Lines.Num() > 0 && !Pr.Lines.Contains(Ctx.Line)) { R.Missing |= FVLegendMissing::LineKind; }
		// soft: a weak line bites, then snaps the leader (the default line has tier 0)
		if (Pr.SnapBelowLineTier > 0 && Ctx.LineTier < Pr.SnapBelowLineTier) { R.Missing |= FVLegendMissing::LineTier; }

		constexpr uint8 Hard = FVLegendMissing::Bait | FVLegendMissing::Float | FVLegendMissing::LineKind;
		if ((R.Missing & Hard) != 0) { R.Result = EFVLegendGate::NoBite; }
		else if ((R.Missing & FVLegendMissing::LineTier) != 0) { R.Result = EFVLegendGate::Snap; }
		return R;
	}

	TArray<FFVLegendCheckItem> LegendChecklist(const FFVFishDef& Def, const FFVCatchContext& Ctx)
	{
		TArray<FFVLegendCheckItem> Out;
		if (Def.Legend == EFVLegend::None)
		{
			return Out;
		}
		const FFVLegendPrep& Pr = Def.Prep;
		const FFVLegendGateResult G = LegendGate(Def, Ctx);
		// one row per rule, in the order of FFVLegendPrep (bait, float, line kind, line tier) = the order of PrepItems
		struct FRule { uint8 Bit; bool bHard; FString Fallback; };
		TArray<FRule, TInlineAllocator<4>> Rules;
		if (!Pr.Bait.IsNone() || Pr.BaitStyle != EFVBaitStyle::Count)
		{
			Rules.Add({ FVLegendMissing::Bait, true, FString::Printf(TEXT("наживка: %s"), *ItemName(Pr.Bait)) });
		}
		if (!Pr.Float.IsNone()) { Rules.Add({ FVLegendMissing::Float, true, ItemName(Pr.Float) }); }
		if (Pr.Lines.Num() > 0) { Rules.Add({ FVLegendMissing::LineKind, true, ItemName(Pr.Lines[0]) }); }
		if (Pr.SnapBelowLineTier > 0) { Rules.Add({ FVLegendMissing::LineTier, false, FString::Printf(TEXT("леска: ур. %d+"), int32(Pr.SnapBelowLineTier)) }); }
		for (int32 i = 0; i < Rules.Num(); ++i)
		{
			FFVLegendCheckItem& It = Out.AddDefaulted_GetRef();
			It.Label = Pr.PrepItems.IsValidIndex(i) ? Pr.PrepItems[i] : Rules[i].Fallback;
			It.bOk = (G.Missing & Rules[i].Bit) == 0;
			It.bHard = Rules[i].bHard;
		}
		return Out;
	}

	const FFVFishDef* FindLegend(EFVLegend Legend)
	{
		if (Legend == EFVLegend::None || Legend >= EFVLegend::Count) { return nullptr; }
		for (const FFVFishDef& F : DB().Fish)
		{
			if (F.Legend == Legend) { return &F; }
		}
		return nullptr;
	}

	FFVRodStats ComposeRodStats(const FFVRodDef& Rod, FName Line, FName Float, const FFVStatMods* Food)
	{
		// the same steps as UFVSaveGame::GetTackleMods / GetRodStats + UFVGame::GetEffectiveRodStats
		FFVStatMods Tackle;
		const FFVItemDef* L = FindItem(Line);
		if (L && L->Kind == EFVItemKind::Line) { Tackle.Combine(L->Tackle); }
		const FFVItemDef* Fl = FindItem(Float);
		if (Fl && Fl->Kind == EFVItemKind::Float) { Tackle.Combine(Fl->Tackle); }
		FFVRodStats S = FFVRodStats::FromRod(Rod);
		S.Apply(Tackle);
		if (Food) { S.Apply(*Food); }
		return S;
	}

	float CatchBarHeight(int32 Level, const FFVRodStats& Stats)
	{
		return 96.f + LevelBarBonus(Level) + Stats.BarBonus;
	}
}
