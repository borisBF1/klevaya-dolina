// UPDATE 6 [DATA-EXPERT] — expert rods, expert fish & legends (conditions, behaviour, facts), local weather, hazards,
// expert catch (Public/FVDatabaseU6.h «DATA-EXPERT»; §8.1, §10). Owner: DATA-EXPERT (also FVSaveGameU6Expert.cpp).
// AppendExpertRods / AppendExpertFish run while FVDB builds its tables: never call FVDB table getters from them.

#include "FVDatabase.h"

namespace FVDBU6ExpertImpl
{
	const TArray<FFVFishFact>& NoFacts() { static const TArray<FFVFishFact> A; return A; }

	// ============================================================================================
	// Expert fish table: one row = the collection def (FFVFishDef) + the expert rules (FFVExpertFishDef) + which
	// conditions each fact reveals (Cover bits, used by the checklist). Pure data: no FVDB calls while building.
	// ============================================================================================
	namespace Cov
	{
		constexpr uint16 Loc = 1 << 0, Time = 1 << 1, Sea = 1 << 2, Tide = 1 << 3, Dark = 1 << 4, Spring = 1 << 5,
			Piles = 1 << 6, Type = 1 << 7, Line = 1 << 8, Float = 1 << 9, Reel = 1 << 10, Bait = 1 << 11, OwnBait = 1 << 12;
		constexpr uint16 PlaceWeather = Loc | Time | Sea | Tide | Dark | Spring | Piles;
	}

	struct FRow
	{
		FFVFishDef Fish;
		FFVExpertFishDef Ex;
		TArray<uint16> Cover;       // parallel to Ex.Facts
	};

	const FName& CaveName() { static const FName N(TEXT("cave")); return N; }

	uint8 TypeBit(EFVExpertRodType T) { return uint8(1u << uint8(T)); }
	uint8 SeaBit(EFVSeaState S) { return uint8(1u << uint8(S)); }

	FRow& XRow(TArray<FRow>& A, const TCHAR* Id, const TCHAR* Name, const TCHAR* Loc, EFVRarity Rarity, int32 Price,
		int32 Difficulty, EFVMotion Motion, float MinSize, float MaxSize, float Weight, int32 MinLevel, const TCHAR* Desc)
	{
		FRow& R = A.AddDefaulted_GetRef();
		FFVFishDef& F = R.Fish;
		F.Id = FName(Id);
		F.Name = Name;
		F.Description = Desc;
		F.Location = FName(Loc);
		F.Rarity = Rarity;
		F.BasePrice = Price;
		F.Difficulty = Difficulty;
		F.Motion = Motion;
		F.MinSize = MinSize;
		F.MaxSize = MaxSize;
		F.Weight = Weight;
		F.MinLevel = MinLevel;
		F.bLegendary = Rarity == EFVRarity::Legendary;
		F.bExpert = true;
		R.Ex.FishId = F.Id;
		return R;
	}

	void XLook(FRow& R, EFVFishShape Shape, EFVFishPattern Pattern, FColor Body, FColor Belly, FColor Fin, FColor Pat)
	{
		R.Fish.Shape = Shape;
		R.Fish.Pattern = Pattern;
		R.Fish.BodyColor = Body;
		R.Fish.BellyColor = Belly;
		R.Fish.FinColor = Fin;
		R.Fish.PatternColor = Pat;
	}

	/** Place / time / local weather (all expert fish). */
	void XWhen(FRow& R, uint8 TimeMask, uint8 SeaMask, bool bTide = false, bool bDark = false, bool bSpring = false, bool bPiles = false)
	{
		R.Ex.TimeMask = TimeMask;
		R.Ex.SeaMask = SeaMask;
		R.Ex.bNeedTide = bTide;
		R.Ex.bNeedDark = bDark;
		R.Ex.bNeedSpring = bSpring;
		R.Ex.bNeedPiles = bPiles;
		R.Fish.TimeMask = TimeMask;
		// the storm only happens on a rainy day (§8.1): mirror it on the classic field (tasks / tools)
		R.Fish.Weather = SeaMask == SeaBit(EFVSeaState::Storm) ? EFVWeatherReq::Rain : EFVWeatherReq::Any;
	}

	/** Rod types + tackle (fish of a type and legends). Universal: TypeMask 0, tackle ignored (§10.4). */
	void XRig(FRow& R, uint8 TypeMask, EFVExpertLine Line = EFVExpertLine::None, EFVExpertFloat Float = EFVExpertFloat::None,
		EFVExpertReel Reel = EFVExpertReel::None, const TCHAR* Bait = nullptr)
	{
		R.Ex.bUniversal = TypeMask == 0;
		R.Ex.TypeMask = TypeMask;
		R.Ex.Line = Line;
		R.Ex.Float = Float;
		R.Ex.Reel = Reel;
		R.Ex.Bait = Bait ? FName(Bait) : NAME_None;
	}

	void XFight(FRow& R, EFVExpertPattern Base, float Speed, float Agility, float Gain, float Drain, float Tension, float Target)
	{
		R.Ex.Base = Base;
		R.Ex.Speed = Speed;
		R.Ex.Agility = Agility;
		R.Ex.Gain = Gain;
		R.Ex.Drain = Drain;
		R.Ex.TensionRate = Tension;
		R.Ex.TargetSeconds = Target;
	}

	FFVExpertMove XMove(EFVExpertPattern P, float Seconds, float Speed, float Amp, float Param, const TCHAR* Name)
	{
		FFVExpertMove M;
		M.Pattern = P;
		M.Seconds = Seconds;
		M.Speed = Speed;
		M.Amp = Amp;
		M.Param = Param;
		M.Name = Name;
		return M;
	}

	void XLegend(FRow& R, EFVExpertLegend L, const TCHAR* OwnBait, float Chance)
	{
		R.Fish.ExpertLegend = L;
		R.Fish.Shape = EFVFishShape::ExpertLegend;
		R.Ex.Legend = L;
		R.Ex.OwnBait = OwnBait ? FName(OwnBait) : NAME_None;
		if (OwnBait) { R.Ex.Bait = R.Ex.OwnBait; }   // Р17: these legends bite ONLY on their own bait
		R.Ex.LegendChance = Chance;
		R.Ex.Tricks.Reset();                          // the score lives in FVExpertLegends::Score
	}

	// ---------------------------------------------------------------- texts of the facts
	/** Bait names (hard-coded: facts are pure data, no FVDB lookups; ids from spec §17.3). */
	FString BaitTitle(FName Id)
	{
		const FString S = Id.ToString();
		if (S == TEXT("bait_deepworm")) { return TEXT("«Глубинный червь»"); }
		if (S == TEXT("bait_marlin")) { return TEXT("«Приманка на марлина»"); }
		if (S == TEXT("bait_harbor")) { return TEXT("«Приманка Хозяина гавани»"); }
		if (S == TEXT("bait_giant")) { return TEXT("«Приманка на великана»"); }
		if (S == TEXT("bait_dragon")) { return TEXT("«Приманка на дракона»"); }
		return TEXT("особая наживка");
	}

	FString TimePhrase(uint8 Mask)
	{
		if ((Mask & FVTime::Any) == FVTime::Any || Mask == 0) { return TEXT("в любое время"); }
		static const TCHAR* Words[] = { TEXT("утром"), TEXT("днём"), TEXT("вечером"), TEXT("ночью") };
		TArray<FString> Parts;
		for (int32 i = 0; i < 4; ++i)
		{
			if (Mask & (1 << i)) { Parts.Add(Words[i]); }
		}
		FString Out;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			if (i > 0) { Out += i == Parts.Num() - 1 ? TEXT(" и ") : TEXT(", "); }
			Out += Parts[i];
		}
		return Out;
	}

	/** «Город, у старых свай» (rows). */
	FString PlaceRow(const FRow& R)
	{
		FString S = R.Fish.Location == CaveName() ? TEXT("Пещера") : TEXT("Город");
		if (R.Ex.bNeedPiles) { S += TEXT(", у старых свай"); }
		if (R.Ex.bNeedSpring) { S += TEXT(", у подводного ключа"); }
		return S;
	}

	/** «в Городе, у старых свай,» (tells). */
	FString PlaceTell(const FRow& R)
	{
		FString S = R.Fish.Location == CaveName() ? TEXT("в Пещере") : TEXT("в Городе");
		if (R.Ex.bNeedPiles) { S += TEXT(", у старых свай,"); }
		if (R.Ex.bNeedSpring) { S += TEXT(", у подводного ключа,"); }
		return S;
	}

	bool HasWeather(const FFVExpertFishDef& E) { return E.SeaMask != 0 || E.bNeedTide || E.bNeedDark; }

	/** «шторм», «штиль или ветер, прилив» (row) / «в шторм», «в штиль или при ветре» (tell). */
	FString WeatherText(const FFVExpertFishDef& E, bool bTell)
	{
		static const TCHAR* RowW[] = { TEXT("штиль"), TEXT("ветер"), TEXT("шторм"), TEXT("туман") };
		static const TCHAR* TellW[] = { TEXT("в штиль"), TEXT("при ветре"), TEXT("в шторм"), TEXT("в туман") };
		FString Out;
		if (E.SeaMask != 0)
		{
			bool bFirst = true;
			for (int32 i = 0; i < int32(EFVSeaState::Count); ++i)
			{
				if (!(E.SeaMask & (1 << i))) { continue; }
				if (!bFirst) { Out += TEXT(" или "); }
				Out += bTell ? TellW[i] : RowW[i];
				bFirst = false;
			}
		}
		auto Add = [&Out](const TCHAR* W) { if (!Out.IsEmpty()) { Out += TEXT(", "); } Out += W; };
		if (E.bNeedTide) { Add(bTell ? TEXT("в прилив") : TEXT("прилив")); }
		if (E.bNeedDark) { Add(bTell ? TEXT("в полной темноте") : TEXT("темнота")); }
		if (Out.IsEmpty()) { Out = bTell ? TEXT("в любую погоду") : TEXT("любая"); }
		return Out;
	}

	FString TypesText(uint8 Mask, bool bQuoted, const TCHAR* Sep)
	{
		FString Out;
		for (int32 i = 0; i < int32(EFVExpertRodType::Count); ++i)
		{
			if (!(Mask & (1 << i))) { continue; }
			if (!Out.IsEmpty()) { Out += Sep; }
			const FString N = FVDBU6::RodTypeName(EFVExpertRodType(i));
			Out += bQuoted ? FString::Printf(TEXT("«%s»"), *N) : N;
		}
		return Out;
	}

	FString LineAcc(EFVExpertLine L) { return L == EFVExpertLine::Thin ? TEXT("тонкую") : TEXT("тяжёлую"); }

	void AddFact(FRow& R, EFVFactKind Kind, uint16 Cover, const FString& Row, const FString& Tell)
	{
		FFVFishFact& F = R.Ex.Facts.AddDefaulted_GetRef();
		F.Kind = Kind;
		F.Row = Row;
		F.Tell = Tell;
		R.Cover.Add(Cover);
	}

	/** 3–5 facts in the reveal order «place/time -> weather -> type -> tackle -> own bait» (§10.16). */
	void MakeFacts(FRow& R)
	{
		const FFVExpertFishDef& E = R.Ex;
		const TCHAR* N = *R.Fish.Name;
		R.Ex.Facts.Reset();
		R.Cover.Reset();

		int32 Count = 3;   // place/time, weather (universal: always, «любая»), types
		if (!E.bUniversal && !HasWeather(E)) { --Count; }
		const int32 Tackle = E.bUniversal ? 0 : int32(E.Line != EFVExpertLine::None) + int32(E.Float != EFVExpertFloat::None)
			+ int32(E.Reel != EFVExpertReel::None) + int32(!E.Bait.IsNone() || !E.OwnBait.IsNone());
		const bool bMergeWeather = Count + Tackle > 5 && HasWeather(E);
		if (bMergeWeather) { --Count; }
		const bool bMergeFloatReel = Count + Tackle > 5 && E.Float != EFVExpertFloat::None && E.Reel != EFVExpertReel::None;

		// ---- place and time (+ weather when merged)
		{
			uint16 C = Cov::Loc | Cov::Time | (E.bNeedPiles ? Cov::Piles : 0) | (E.bNeedSpring ? Cov::Spring : 0);
			FString Row = FString::Printf(TEXT("Где и когда: %s, %s"), *PlaceRow(R), *TimePhrase(E.TimeMask));
			FString Tell = FString::Printf(TEXT("«%s» держится %s и клюёт %s"), N, *PlaceTell(R), *TimePhrase(E.TimeMask));
			if (bMergeWeather)
			{
				C |= Cov::Sea | Cov::Tide | Cov::Dark;
				Row += FString::Printf(TEXT(", погода: %s"), *WeatherText(E, false));
				Tell += FString::Printf(TEXT(", но только %s"), *WeatherText(E, true));
			}
			AddFact(R, EFVFactKind::PlaceTime, C, Row, Tell + TEXT("."));
		}
		// ---- local weather
		if (!bMergeWeather && (E.bUniversal || HasWeather(E)))
		{
			FString Tell;
			if (HasWeather(E))
			{
				Tell = FString::Printf(TEXT("«%s» выходит только %s — в другую погоду и не жди."), N, *WeatherText(E, true));
			}
			else if (R.Fish.Location == CaveName())
			{
				Tell = FString::Printf(TEXT("«%s» погоды не знает: под землёй всегда одно и то же."), N);
			}
			else
			{
				Tell = FString::Printf(TEXT("«%s» клюёт в любую погоду — хоть в штиль, хоть в шторм."), N);
			}
			AddFact(R, EFVFactKind::Weather, Cov::Sea | Cov::Tide | Cov::Dark,
				FString::Printf(TEXT("Погода: %s"), *WeatherText(E, false)), Tell);
		}
		// ---- rod types
		if (E.bUniversal)
		{
			AddFact(R, EFVFactKind::RodTypes, Cov::Type, TEXT("Клюёт на: любую экспертную удочку"),
				FString::Printf(TEXT("«%s» не привереда: берёт на любую экспертную удочку, оснастка не важна."), N));
			return;
		}
		AddFact(R, EFVFactKind::RodTypes, Cov::Type,
			FString::Printf(TEXT("Клюёт на: %s"), *TypesText(E.TypeMask, false, TEXT(", "))),
			FString::Printf(TEXT("«%s» садится только на удочки типа %s."), N, *TypesText(E.TypeMask, true, TEXT(" или "))));
		// ---- tackle
		if (E.Line != EFVExpertLine::None)
		{
			AddFact(R, EFVFactKind::Line, Cov::Line, FString::Printf(TEXT("Леска: %s"), *FVDBU6::ExpertLineName(E.Line)),
				FString::Printf(TEXT("Для «%s» леску ставь %s, иначе и не подойдёт."), N, *LineAcc(E.Line)));
		}
		if (bMergeFloatReel)
		{
			AddFact(R, EFVFactKind::Float, Cov::Float | Cov::Reel,
				FString::Printf(TEXT("Поплавок: %s, катушка: %s"), *FVDBU6::ExpertFloatName(E.Float), *FVDBU6::ExpertReelName(E.Reel)),
				FString::Printf(TEXT("Под «%s» нужны %s поплавок и %s катушка."), N, *FVDBU6::ExpertFloatName(E.Float), *FVDBU6::ExpertReelName(E.Reel)));
		}
		else
		{
			if (E.Float != EFVExpertFloat::None)
			{
				AddFact(R, EFVFactKind::Float, Cov::Float, FString::Printf(TEXT("Поплавок: %s"), *FVDBU6::ExpertFloatName(E.Float)),
					FString::Printf(TEXT("Под «%s» нужен %s поплавок."), N, *FVDBU6::ExpertFloatName(E.Float)));
			}
			if (E.Reel != EFVExpertReel::None)
			{
				AddFact(R, EFVFactKind::Reel, Cov::Reel, FString::Printf(TEXT("Катушка: %s"), *FVDBU6::ExpertReelName(E.Reel)),
					FString::Printf(TEXT("На «%s» нужна %s катушка."), N, *FVDBU6::ExpertReelName(E.Reel)));
			}
		}
		if (!E.OwnBait.IsNone())
		{
			AddFact(R, EFVFactKind::OwnBait, Cov::Bait | Cov::OwnBait,
				FString::Printf(TEXT("Своя наживка: %s"), *BaitTitle(E.OwnBait)),
				FString::Printf(TEXT("«%s» клюёт лишь на свою приманку — %s. Рецепт подскажут в Мастерской мастера."), N, *BaitTitle(E.OwnBait)));
		}
		else if (!E.Bait.IsNone())
		{
			AddFact(R, EFVFactKind::Bait, Cov::Bait, FString::Printf(TEXT("Наживка: %s"), *BaitTitle(E.Bait)),
				FString::Printf(TEXT("«%s» берёт только на %s."), N, *BaitTitle(E.Bait)));
		}
	}

	TArray<FRow> BuildRows()
	{
		using R = EFVRarity;
		using M = EFVMotion;
		using S = EFVFishShape;
		using P = EFVFishPattern;
		using X = EFVExpertPattern;
		using T = EFVExpertRodType;
		using L = EFVExpertLine;
		using F = EFVExpertFloat;
		using K = EFVExpertReel;
		constexpr uint8 Mor = FVTime::Morning, Day = FVTime::Day, Eve = FVTime::Evening, Ngt = FVTime::Night, Any = FVTime::Any;
		const uint8 Calm = SeaBit(EFVSeaState::Calm), Wind = SeaBit(EFVSeaState::Wind), Storm = SeaBit(EFVSeaState::Storm), Fog = SeaBit(EFVSeaState::Fog);
		const uint8 Pend = TypeBit(T::Pendulum), Twin = TypeBit(T::Twin), Comp = TypeBit(T::Compass), Magn = TypeBit(T::Magnet), Helm = TypeBit(T::Helm);
		const TCHAR* City = TEXT("city");
		const TCHAR* Cave = TEXT("cave");

		TArray<FRow> A;
		// ======================================================================= City: universal (any expert rod)
		{
			FRow& Rw = XRow(A, TEXT("efish_barracuda"), TEXT("Барракуда"), City, R::Rare, 900, 86, M::Dart, 60, 150, 10.f, 11,
				TEXT("Морская щука с зубами-иглами. Бросается на всё, что блестит."));
			XLook(Rw, S::Barracuda, P::Stripes, FColor(120, 136, 150), FColor(222, 230, 234), FColor(90, 104, 120), FColor(60, 72, 88));
			XWhen(Rw, Any, 0);
			XRig(Rw, 0);
			XFight(Rw, X::Snake, 64.f, 1.2f, 0.0923f, 0.072f, 0.32f, 22.f);
			Rw.Ex.Tricks = { XMove(X::Dash, 2.5f, 1.4f, 1.f, 2.f, TEXT("Два броска")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_king_mackerel"), TEXT("Королевская скумбрия"), City, R::Rare, 820, 84, M::Smooth, 50, 120, 10.f, 11,
				TEXT("Стремительная и полосатая, ходит стаями у самой поверхности."));
			XLook(Rw, S::Mackerel, P::Stripes, FColor(70, 120, 150), FColor(226, 232, 226), FColor(72, 96, 120), FColor(36, 60, 84));
			XWhen(Rw, Mor | Day, Calm | Wind);
			XRig(Rw, 0);
			XFight(Rw, X::Snake, 44.f, 1.1f, 0.1142f, 0.0365f, 0.30f, 22.f);
			Rw.Ex.Tricks = { XMove(X::Shuttle, 3.5f, 1.3f, 1.f, 0.4f, TEXT("Челнок")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_giant_bass"), TEXT("Морской окунь-великан"), City, R::Epic, 1800, 95, M::Sinker, 80, 200, 6.f, 11,
				TEXT("Огромный пятнистый окунь. Прячется в тени пирса и глотает добычу целиком."));
			XLook(Rw, S::Grouper, P::Spots, FColor(110, 96, 70), FColor(214, 196, 150), FColor(96, 78, 56), FColor(62, 50, 36));
			XWhen(Rw, Eve | Ngt, 0);
			XRig(Rw, 0);
			XFight(Rw, X::Snake, 52.f, 0.8f, 0.0706f, 0.0924f, 0.40f, 28.f);
			Rw.Ex.Tricks = { XMove(X::Hide, 3.f, 0.8f, 1.f, 0.f, TEXT("Уходит в тень")), XMove(X::Dive, 2.f, 1.f, 1.f, 0.f, TEXT("На дно")) };
		}
		// ======================================================================= City: fish of a type
		{
			FRow& Rw = XRow(A, TEXT("efish_swordfish"), TEXT("Меч-рыба"), City, R::Epic, 2600, 100, M::Dart, 120, 300, 7.f, 11,
				TEXT("Её меч рассекает волну. Выходит, когда море неспокойно."));
			XLook(Rw, S::Swordfish, P::Plain, FColor(64, 84, 128), FColor(206, 214, 226), FColor(52, 66, 104), FColor(40, 50, 80));
			XWhen(Rw, Day | Eve, Wind | Storm);
			XRig(Rw, Pend | Helm, L::Heavy);
			XFight(Rw, X::Snake, 43.f, 1.1f, 0.1f, 0.06f, 0.42f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Dash, 3.f, 1.5f, 1.1f, 3.f, TEXT("Три удара мечом")), XMove(X::Jump, 2.5f, 1.1f, 1.f, 0.f, TEXT("Свеча над волной")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_stingray"), TEXT("Скат-хвостокол"), City, R::Epic, 2400, 98, M::Sinker, 60, 160, 7.f, 11,
				TEXT("Плоский, как блюдо, с ядовитым шипом на хвосте. Зарывается в песок."));
			XLook(Rw, S::Ray, P::Spots, FColor(128, 110, 88), FColor(226, 214, 196), FColor(104, 88, 70), FColor(70, 58, 44));
			XWhen(Rw, Any, Calm | Fog);
			XRig(Rw, Twin | Magn, L::None, F::Deep);
			XFight(Rw, X::Snake, 50.f, 0.9f, 0.0649f, 0.072f, 0.36f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Hide, 3.f, 0.7f, 1.f, 0.f, TEXT("Зарывается в песок")), XMove(X::Dive, 2.f, 1.f, 1.1f, 0.f, TEXT("Прыжок со дна")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_sunfish"), TEXT("Рыба-луна"), City, R::Epic, 2800, 96, M::Floater, 100, 250, 6.f, 11,
				TEXT("Круглая, как луна, греется у поверхности в тихую погоду. Пуглива."));
			XLook(Rw, S::Sunfish, P::Plain, FColor(170, 176, 186), FColor(232, 234, 238), FColor(140, 146, 158), FColor(110, 116, 128));
			XWhen(Rw, Mor | Day, Calm);
			XRig(Rw, Comp, L::None, F::Surface);
			XFight(Rw, X::Circle, 46.f, 0.8f, 0.048f, 0.055f, 0.30f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Skittish, 4.f, 1.2f, 1.f, 0.f, TEXT("Пугается")), XMove(X::Circle, 4.f, 1.1f, 1.2f, 2.f, TEXT("Размах")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_sailfish"), TEXT("Парусник"), City, R::Epic, 3400, 104, M::Dart, 150, 320, 5.f, 11,
				TEXT("Самая быстрая рыба моря. Поднимает спинной парус, когда идёт на рывок."));
			XLook(Rw, S::Sailfish, P::Spots, FColor(40, 80, 150), FColor(214, 222, 236), FColor(64, 110, 190), FColor(120, 170, 230));
			XWhen(Rw, Day, Wind);
			XRig(Rw, Comp | Helm, L::Thin, F::None, K::Light);
			XFight(Rw, X::Snake, 67.f, 1.3f, 0.102f, 0.084f, 0.44f, 32.f);
			Rw.Ex.Tricks = { XMove(X::Dash, 3.f, 1.6f, 1.2f, 2.f, TEXT("Парус вверх — рывок")), XMove(X::Jump, 2.5f, 1.2f, 1.f, 0.f, TEXT("Свеча с парусом")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_moray"), TEXT("Мурена"), City, R::Epic, 3000, 102, M::Mixed, 90, 200, 6.f, 11,
				TEXT("Живёт в щелях между старыми сваями и выходит на охоту по ночам."));
			XLook(Rw, S::Moray, P::Spots, FColor(96, 110, 52), FColor(170, 176, 96), FColor(80, 92, 44), FColor(210, 200, 90));
			XWhen(Rw, Ngt, 0, false, false, false, true);
			XRig(Rw, Magn, L::Heavy);
			XFight(Rw, X::Snake, 58.f, 1.2f, 0.0556f, 0.153f, 0.46f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Hide, 3.f, 0.8f, 1.f, 0.f, TEXT("В щель")), XMove(X::Dash, 2.5f, 1.5f, 1.f, 1.f, TEXT("Бросок из норы")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_seadragon"), TEXT("Морской дракон"), City, R::Epic, 3600, 106, M::Floater, 25, 45, 5.f, 11,
				TEXT("Похож на обрывок водоросли. Приплывает с вечерним приливом."));
			XLook(Rw, S::SeaDragon, P::Stripes, FColor(196, 150, 60), FColor(236, 206, 120), FColor(120, 170, 80), FColor(150, 90, 40));
			XWhen(Rw, Eve, 0, true);
			XRig(Rw, Twin, L::Thin, F::Surface);
			XFight(Rw, X::Snake, 50.f, 1.4f, 0.045f, 0.036f, 0.30f, 32.f);
			Rw.Ex.Tricks = { XMove(X::Decoy, 3.f, 1.f, 1.f, 2.5f, TEXT("Обманка")), XMove(X::Eight, 4.f, 1.1f, 1.f, 0.f, TEXT("Качели")) };
		}
		// ======================================================================= Cave: universal
		{
			FRow& Rw = XRow(A, TEXT("efish_blind_catfish"), TEXT("Слепой сом"), Cave, R::Rare, 1000, 86, M::Sinker, 60, 160, 10.f, 12,
				TEXT("Глаз у него нет — охотится на ощупь длинными усами."));
			XLook(Rw, S::Catfish, P::Plain, FColor(186, 176, 176), FColor(232, 226, 222), FColor(160, 150, 152), FColor(130, 120, 124));
			XWhen(Rw, Any, 0);
			XRig(Rw, 0);
			XFight(Rw, X::Snake, 52.f, 0.9f, 0.0962f, 0.04f, 0.34f, 22.f);
			Rw.Ex.Tricks = { XMove(X::Dive, 2.f, 1.f, 1.f, 0.f, TEXT("На дно")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_cave_grayling"), TEXT("Пещерный хариус"), Cave, R::Rare, 950, 84, M::Dart, 25, 55, 10.f, 12,
				TEXT("Бледный хариус с огромным спинным плавником. Чуток к каждому шороху."));
			XLook(Rw, S::Grayling, P::Spots, FColor(150, 160, 176), FColor(222, 226, 234), FColor(170, 120, 190), FColor(110, 90, 140));
			XWhen(Rw, Mor | Day | Eve, 0);
			XRig(Rw, 0);
			XFight(Rw, X::Snake, 41.f, 1.2f, 0.0938f, 0.035f, 0.30f, 22.f);
			Rw.Ex.Tricks = { XMove(X::Skittish, 3.f, 1.2f, 1.f, 0.f, TEXT("Пугается")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_stone_char"), TEXT("Каменный голец"), Cave, R::Epic, 2000, 96, M::Mixed, 40, 90, 6.f, 12,
				TEXT("Серый, как пещерный камень, — пока не шевельнётся, не заметишь."));
			XLook(Rw, S::Pike, P::Spots, FColor(110, 110, 112), FColor(196, 190, 180), FColor(140, 90, 70), FColor(70, 70, 74));
			XWhen(Rw, Eve | Ngt, 0);
			XRig(Rw, 0);
			XFight(Rw, X::Snake, 60.f, 1.f, 0.0504f, 0.17f, 0.38f, 28.f);
			Rw.Ex.Tricks = { XMove(X::Hide, 3.f, 0.8f, 1.f, 0.f, TEXT("Среди камней")), XMove(X::Dash, 2.5f, 1.4f, 1.f, 2.f, TEXT("Два броска")) };
		}
		// ======================================================================= Cave: fish of a type
		{
			FRow& Rw = XRow(A, TEXT("efish_crystal_eel"), TEXT("Кристальный угорь"), Cave, R::Epic, 3000, 102, M::Mixed, 60, 140, 6.f, 12,
				TEXT("Прозрачный, как горный хрусталь. Ночью в свете поплавка переливается радугой."));
			XLook(Rw, S::Eel, P::Shimmer, FColor(170, 210, 230), FColor(226, 244, 250), FColor(140, 190, 220), FColor(250, 250, 255));
			XWhen(Rw, Ngt, 0);
			XRig(Rw, Pend | Comp, L::None, F::Glow);
			XFight(Rw, X::Snake, 62.f, 1.3f, 0.0659f, 0.0736f, 0.34f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Decoy, 3.f, 1.f, 1.f, 2.5f, TEXT("Двойник")), XMove(X::Eight, 4.f, 1.1f, 1.f, 0.f, TEXT("Качели")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_proteus"), TEXT("Бледный протей"), Cave, R::Epic, 2900, 100, M::Smooth, 20, 35, 6.f, 12,
				TEXT("«Человеческая рыбка»: розовая, с пушистыми жабрами. Живёт сто лет и пугается грубой снасти."));
			XLook(Rw, S::Proteus, P::Plain, FColor(236, 196, 196), FColor(250, 230, 226), FColor(226, 150, 160), FColor(210, 120, 130));
			XWhen(Rw, Any, 0);
			XRig(Rw, Twin, L::Thin, F::Deep);
			XFight(Rw, X::Skittish, 48.f, 1.f, 0.0841f, 0.042f, 0.28f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Hide, 3.f, 0.8f, 1.f, 0.f, TEXT("Прячется в иле")), XMove(X::Skittish, 3.f, 1.3f, 1.f, 0.f, TEXT("Бегство")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_glow_perch"), TEXT("Светящийся окунь"), Cave, R::Epic, 2700, 98, M::Mixed, 25, 50, 7.f, 12,
				TEXT("Полосы светятся голубым. Плывёт на свет, как мотылёк."));
			XLook(Rw, S::Perch, P::Glow, FColor(40, 90, 110), FColor(150, 220, 230), FColor(60, 170, 200), FColor(120, 240, 255));
			XWhen(Rw, Any, 0);
			XRig(Rw, Magn | Twin, L::None, F::Glow);
			XFight(Rw, X::Circle, 43.f, 1.1f, 0.07f, 0.059f, 0.34f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Circle, 4.f, 1.2f, 1.1f, 2.f, TEXT("Хоровод")), XMove(X::Dash, 2.5f, 1.4f, 1.f, 1.f, TEXT("Бросок")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_mica_carp"), TEXT("Слюдяной карп"), Cave, R::Epic, 3200, 103, M::Sinker, 40, 90, 6.f, 12,
				TEXT("Чешуя из слюдяных пластин. Кормится у подводного ключа, где вода несёт крошку."));
			XLook(Rw, S::Carp, P::Shimmer, FColor(176, 160, 120), FColor(234, 226, 196), FColor(150, 130, 90), FColor(250, 244, 210));
			XWhen(Rw, Mor | Day, 0, false, false, true);
			XRig(Rw, Pend | Helm, L::None, F::None, K::Heavy);
			XFight(Rw, X::Snake, 54.f, 0.9f, 0.12f, 0.08f, 0.40f, 30.f);
			Rw.Ex.Tricks = { XMove(X::Dive, 2.f, 1.f, 1.1f, 0.f, TEXT("На дно")), XMove(X::Shuttle, 3.f, 1.2f, 1.f, 0.4f, TEXT("Челнок")) };
		}
		{
			FRow& Rw = XRow(A, TEXT("efish_cave_sturgeon"), TEXT("Пещерный осётр"), Cave, R::Epic, 3800, 108, M::Smooth, 100, 240, 5.f, 12,
				TEXT("Древний, как сама пещера. Поднимается из глубины только за глубинным червём."));
			XLook(Rw, S::Sturgeon, P::Scales, FColor(90, 96, 104), FColor(200, 200, 196), FColor(70, 76, 84), FColor(210, 214, 220));
			XWhen(Rw, Eve | Ngt, 0);
			XRig(Rw, Helm, L::None, F::None, K::Heavy, TEXT("bait_deepworm"));
			XFight(Rw, X::Snake, 37.f, 0.8f, 0.042f, 0.11f, 0.44f, 34.f);
			Rw.Ex.Tricks = { XMove(X::Dive, 2.5f, 1.f, 1.2f, 0.f, TEXT("В глубину")), XMove(X::Dash, 3.f, 1.3f, 1.1f, 2.f, TEXT("Два удара хвостом")) };
		}
		// ======================================================================= Expert legends (§10.8): one type + tackle
		{
			FRow& Rw = XRow(A, TEXT("elegend_marlin"), TEXT("Царь-марлин"), City, R::Legendary, 25000, 125, M::Dart, 250, 450, 1.f, 11,
				TEXT("Властелин штормового моря. Говорят, он сам выбирает рыбака."));
			XLook(Rw, S::ExpertLegend, P::Stripes, FColor(30, 60, 130), FColor(220, 230, 244), FColor(60, 120, 210), FColor(140, 200, 255));
			XWhen(Rw, Any, Storm);
			XRig(Rw, Helm, L::Heavy, F::None, K::Heavy);
			XLegend(Rw, EFVExpertLegend::KingMarlin, TEXT("bait_marlin"), 0.f);
			XFight(Rw, X::Snake, 88.f, 1.2f, 0.0161f, 0.05f, 0.40f, 95.f);
		}
		{
			FRow& Rw = XRow(A, TEXT("elegend_harbor"), TEXT("Хозяин гавани"), City, R::Legendary, 20000, 120, M::Sinker, 180, 320, 1.f, 11,
				TEXT("Огромный старый групер. Живёт под сваями дольше, чем стоит сам Город."));
			XLook(Rw, S::ExpertLegend, P::Spots, FColor(70, 64, 50), FColor(170, 160, 130), FColor(90, 80, 60), FColor(40, 36, 28));
			XWhen(Rw, Ngt, 0, false, false, false, true);
			XRig(Rw, Magn, L::Heavy, F::Deep);
			XLegend(Rw, EFVExpertLegend::HarborMaster, TEXT("bait_harbor"), 0.f);
			XFight(Rw, X::Snake, 70.f, 0.8f, 0.0349f, 0.0112f, 0.46f, 90.f);
		}
		{
			FRow& Rw = XRow(A, TEXT("elegend_ghostray"), TEXT("Призрачный скат"), City, R::Legendary, 18000, 118, M::Floater, 150, 300, 1.f, 11,
				TEXT("Белый, почти прозрачный скат. Скользит под водой только в утреннем тумане."));
			XLook(Rw, S::ExpertLegend, P::Shimmer, FColor(214, 224, 232), FColor(244, 248, 252), FColor(190, 204, 220), FColor(255, 255, 255));
			XWhen(Rw, Mor, Fog);
			XRig(Rw, Twin, L::Thin, F::Surface);
			XLegend(Rw, EFVExpertLegend::GhostRay, nullptr, 0.04f);
			XFight(Rw, X::Snake, 67.f, 1.1f, 0.0264f, 0.0312f, 0.34f, 80.f);
		}
		{
			FRow& Rw = XRow(A, TEXT("elegend_patriarch"), TEXT("Слепой патриарх"), Cave, R::Legendary, 22000, 120, M::Sinker, 200, 380, 1.f, 12,
				TEXT("Сом-исполин, старший в подземном озере. Слышит, как падает каждая капля."));
			XLook(Rw, S::ExpertLegend, P::Plain, FColor(200, 190, 188), FColor(240, 234, 230), FColor(170, 160, 160), FColor(120, 110, 112));
			XWhen(Rw, Any, 0, false, true);
			XRig(Rw, Pend, L::Heavy, F::Glow);
			XLegend(Rw, EFVExpertLegend::BlindPatriarch, nullptr, 0.04f);
			XFight(Rw, X::Snake, 66.f, 0.9f, 0.0354f, 0.024f, 0.42f, 85.f);
		}
		{
			FRow& Rw = XRow(A, TEXT("elegend_giant"), TEXT("Каменный великан"), Cave, R::Legendary, 30000, 126, M::Sinker, 220, 420, 1.f, 12,
				TEXT("Осётр, обросший каменной бронёй. Стоит в струе подводного ключа, как скала."));
			XLook(Rw, S::ExpertLegend, P::Scales, FColor(100, 96, 90), FColor(180, 172, 160), FColor(80, 76, 70), FColor(150, 140, 120));
			XWhen(Rw, Any, 0, false, false, true);
			XRig(Rw, Helm, L::None, F::Deep, K::Heavy);
			XLegend(Rw, EFVExpertLegend::StoneGiant, TEXT("bait_giant"), 0.f);
			XFight(Rw, X::Snake, 62.f, 0.7f, 0.0208f, 0.0225f, 0.48f, 100.f);
		}
		{
			FRow& Rw = XRow(A, TEXT("elegend_dragon"), TEXT("Хрустальный дракон"), Cave, R::Legendary, 40000, 130, M::Dart, 120, 240, 1.f, 12,
				TEXT("Живой хрусталь с плавниками-крыльями. Кто его видел, говорят, видел всё."));
			XLook(Rw, S::ExpertLegend, P::Shimmer, FColor(150, 210, 240), FColor(230, 246, 255), FColor(190, 150, 240), FColor(255, 255, 255));
			XWhen(Rw, Any, 0);
			XRig(Rw, Comp, L::Thin, F::Glow, K::Light);
			XLegend(Rw, EFVExpertLegend::CrystalDragon, TEXT("bait_dragon"), 0.f);
			XFight(Rw, X::Snake, 80.f, 1.4f, 0.0453f, 0.036f, 0.50f, 110.f);
		}

		for (FRow& Rw : A) { MakeFacts(Rw); }
		return A;
	}

	const TArray<FRow>& Rows()
	{
		static const TArray<FRow> A = BuildRows();
		return A;
	}

	const FRow* FindRow(FName FishId)
	{
		for (const FRow& R : Rows())
		{
			if (R.Ex.FishId == FishId) { return &R; }
		}
		return nullptr;
	}

	// ============================================================================================
	// Expert rods (§10.3): FFVRodDef (shop / wardrobe colours / theme) + the control characteristics («характер»)
	// ============================================================================================
	struct FRodRow
	{
		FFVRodDef Rod;
		FFVExpertRodDef Ex;
	};

	FRodRow& XRod(TArray<FRodRow>& A, const TCHAR* Id, const TCHAR* Name, EFVExpertRodType Type, EFVExpertTheme Theme,
		int32 Tier, int32 Price, int32 MinLevel, uint8 TraderMask, FColor RodColor, FColor Accent, const TCHAR* Desc)
	{
		FRodRow& R = A.AddDefaulted_GetRef();
		FFVRodDef& D = R.Rod;
		D.Id = FName(Id);
		D.Name = Name;
		D.Description = Desc;
		D.Tier = Tier;
		D.Price = Price;
		D.MinLevel = MinLevel;
		// classic fields: only used when an expert rod fishes a classic lake (Р18: bite speed, cast, treasure)
		D.BarBonus = 0.f;
		D.ReelMult = 1.f;
		D.DrainMult = 1.f;
		D.BiteSpeed = 0.7f;
		D.CastRange = 1.f;
		D.Luck = 0.3f;
		D.TreasureBonus = 0.06f;
		D.RodColor = RodColor;
		D.AccentColor = Accent;
		D.TraderMask = TraderMask;   // 0 = forged (0.6.2: FVDBU6::AppendRecipes, «Закалочная кузня»)
		D.bExpert = true;
		D.ExpertType = Type;
		D.Theme = Theme;
		R.Ex.RodId = D.Id;
		R.Ex.Type = Type;
		return R;
	}

	// §19 / Р60 (one vertical track): these numbers are the rod's CHARACTER only. FFVExpertFight::Start turns them into
	// the 1D bar relative to the reference rod of its type (Бриз, Близнецы, Румб, Якорь, Рулевой): ZoneH (Маятник /
	// Двойчатка) or ZoneW (Импульс / Магнит / Ползунок) -> bar height, MaxSpeed -> speed cap, Gravity -> the bar's
	// acceleration, Push -> the Impulse push / the Slider spring, Inertia -> the Impulse brake / the Slider inertia,
	// Pull -> the Magnet pull, Overlap -> the Twin overlap, TensionReserve -> the line reserve. Unused 2D leftovers
	// (Leash, ArrowPeriod) are ignored.
	TArray<FRodRow> BuildRods()
	{
		using T = EFVExpertRodType;
		using Th = EFVExpertTheme;
		constexpr uint8 CityTackle = 16;   // tr_city_tackle (§17.3)
		TArray<FRodRow> A;
		{
			FFVExpertRodDef& E = XRod(A, TEXT("rod_breeze"), TEXT("Удочка «Бриз»"), T::Pendulum, Th::Breeze, 6, 30000, 11, CityTackle,
				FColor(80, 200, 200), FColor(236, 246, 246),
				TEXT("Маятник. Скромная и послушная: широкая полоса, мягкий разгон. С неё начинают все эксперты.")).Ex;
			E.ZoneW = 140.f; E.ZoneH = 112.f; E.Gravity = 220.f; E.MaxSpeed = 150.f; E.Drag = 2.2f; E.Leash = 2.2f; E.Bounce = 0.30f;
		}
		{
			FFVExpertRodDef& E = XRod(A, TEXT("rod_twins"), TEXT("Удочка «Близнецы»"), T::Twin, Th::Twins, 6, 45000, 12, CityTackle,
				FColor(170, 186, 210), FColor(40, 70, 150),
				TEXT("Двойчатка. Две полосы на одной шкале, но медленные: рыбу приходится передавать заранее.")).Ex;
			E.ZoneW = 140.f; E.ZoneH = 83.f; E.Gravity = 200.f; E.MaxSpeed = 120.f; E.Drag = 2.4f; E.Bounce = 0.30f; E.Overlap = 44.f;
		}
		{
			FFVExpertRodDef& E = XRod(A, TEXT("rod_surf"), TEXT("Удочка «Прибой»"), T::Pendulum, Th::Surf, 7, 70000, 13, 0,
				FColor(30, 120, 124), FColor(230, 250, 250),
				TEXT("Маятник. Полоса уже, разгон сильнее: быстрее и труднее «Бриза».")).Ex;
			E.ZoneW = 118.f; E.ZoneH = 68.f; E.Gravity = 330.f; E.MaxSpeed = 195.f; E.Drag = 1.9f; E.Leash = 3.0f; E.Bounce = 0.45f;
		}
		{
			// 0.6.2 (Р81): sold by Кузьма only after «Друг Города»
			FRodRow& Rw = XRod(A, TEXT("rod_rhumb"), TEXT("Удочка «Румб»"), T::Compass, Th::Rhumb, 7, 150000, 13, CityTackle,
				FColor(200, 160, 70), FColor(150, 90, 50),
				TEXT("Импульс. Мощные толчки вверх и тяжёлое падение: думай на шаг вперёд."));
			Rw.Rod.RequiredAchievement = FName(TEXT("ach_city_tasks"));
			FFVExpertRodDef& E = Rw.Ex;
			E.ZoneW = 125.f; E.ZoneH = 125.f; E.MaxSpeed = 200.f; E.Drag = 2.8f; E.Push = 125.f; E.ArrowPeriod = 0.2f; E.Inertia = 0.6f; E.Bounce = 0.4f;
		}
		{
			FFVExpertRodDef& E = XRod(A, TEXT("rod_mirror"), TEXT("Удочка «Зеркало»"), T::Twin, Th::Mirror, 7, 90000, 14, 0,
				FColor(214, 220, 228), FColor(120, 130, 146),
				TEXT("Двойчатка. Узкие быстрые полосы и малое перекрытие — для точной руки.")).Ex;
			E.ZoneW = 128.f; E.ZoneH = 47.f; E.Gravity = 310.f; E.MaxSpeed = 205.f; E.Drag = 1.9f; E.Bounce = 0.40f; E.Overlap = 20.f;
		}
		{
			FFVExpertRodDef& E = XRod(A, TEXT("rod_anchor"), TEXT("Удочка «Якорь»"), T::Magnet, Th::Anchor, 7, 95000, 14, 0,
				FColor(70, 72, 80), FColor(170, 60, 50),
				TEXT("Магнит. Сильная тяга, но леска держит мало: не тяни, когда рыба рвётся.")).Ex;
			E.ZoneW = 80.f; E.ZoneH = 80.f; E.MaxSpeed = 40.f; E.Drag = 3.f; E.Pull = 130.f; E.TensionReserve = 0.7f;
		}
		{
			FFVExpertRodDef& E = XRod(A, TEXT("rod_helmsman"), TEXT("Удочка «Рулевой»"), T::Helm, Th::Helmsman, 8, 130000, 15, 0,
				FColor(130, 50, 36), FColor(236, 190, 70),
				TEXT("Ползунок. Полоса идёт за пальцем, но она самая узкая; резкие рывки с рыбой натягивают леску.")).Ex;
			E.ZoneW = 86.f; E.ZoneH = 86.f; E.MaxSpeed = 180.f; E.Drag = 1.2f; E.Push = 260.f; E.Inertia = 0.7f; E.TurnTension = 0.1f; E.Bounce = 0.35f;
		}
		{
			// 0.6.2 (Р81): sold by Кузьма only after «Знаток повадок»
			FRodRow& Rw = XRod(A, TEXT("rod_pole"), TEXT("Удочка «Полюс»"), T::Magnet, Th::Pole, 8, 250000, 15, CityTackle,
				FColor(180, 230, 240), FColor(150, 90, 210),
				TEXT("Магнит. Слабая тяга, но широкая полоса и огромный запас лески — терпение вместо силы."));
			Rw.Rod.RequiredAchievement = FName(TEXT("ach_facts"));
			FFVExpertRodDef& E = Rw.Ex;
			E.ZoneW = 104.f; E.ZoneH = 104.f; E.MaxSpeed = 36.f; E.Drag = 3.f; E.Pull = 85.f; E.TensionReserve = 1.8f;
		}
		return A;
	}

	const TArray<FRodRow>& RodRows()
	{
		static const TArray<FRodRow> A = BuildRods();
		return A;
	}

	// ---------------------------------------------------------------- misc
	uint32 Mix(uint32 A, uint32 B)
	{
		uint32 H = (A * 0x9E3779B1u) ^ (B + 0x7F4A7C15u + (A << 6) + (A >> 2));
		H ^= H >> 16; H *= 0x85EBCA6Bu; H ^= H >> 13; H *= 0xC2B2AE35u; H ^= H >> 16;
		return H;
	}

	bool IsInvisibility(EFVHazard H) { return H == EFVHazard::Darkness || H == EFVHazard::Weeds; }

	float Sanitize(float V) { return FMath::IsFinite(V) ? FMath::Clamp(V, 0.2f, 5.f) : 1.f; }

	/** Are the conditions of a group (Cov bits) satisfied right now? */
	bool CoverOk(uint16 C, const FRow& Rw, const FFVExpertCatchContext& Ctx)
	{
		const FFVExpertFishDef& E = Rw.Ex;
		if ((C & Cov::Loc) && Ctx.Location != Rw.Fish.Location) { return false; }
		if ((C & Cov::Time) && !(E.TimeMask & Ctx.PhaseMask)) { return false; }
		if ((C & Cov::Sea) && E.SeaMask != 0 && !(Ctx.Local.bValid && (E.SeaMask & SeaBit(Ctx.Local.Sea)))) { return false; }
		if ((C & Cov::Tide) && E.bNeedTide && !Ctx.Local.bTide) { return false; }
		if ((C & Cov::Dark) && E.bNeedDark && !Ctx.Local.bDark) { return false; }
		if ((C & Cov::Spring) && E.bNeedSpring && !Ctx.Local.bSpring) { return false; }
		if ((C & Cov::Piles) && E.bNeedPiles && !Ctx.bNearPiles) { return false; }
		if ((C & Cov::Type) && !E.bUniversal && !(Ctx.Type < EFVExpertRodType::Count && (E.TypeMask & TypeBit(Ctx.Type)))) { return false; }
		if ((C & Cov::Line) && E.Line != EFVExpertLine::None && E.Line != Ctx.Line) { return false; }
		if ((C & Cov::Float) && E.Float != EFVExpertFloat::None && E.Float != Ctx.Float) { return false; }
		if ((C & Cov::Reel) && E.Reel != EFVExpertReel::None && E.Reel != Ctx.Reel) { return false; }
		if ((C & Cov::Bait) && !E.Bait.IsNone() && E.Bait != Ctx.Bait) { return false; }
		return true;
	}
}

// ================================================================================================
// Tables
// ================================================================================================
void FVDBU6::AppendExpertRods(TArray<FFVRodDef>& Rods)
{
	for (const FVDBU6ExpertImpl::FRodRow& R : FVDBU6ExpertImpl::RodRows())
	{
		if (!Rods.ContainsByPredicate([&R](const FFVRodDef& D) { return D.Id == R.Rod.Id; })) { Rods.Add(R.Rod); }
	}
}

void FVDBU6::AppendExpertFish(TArray<FFVFishDef>& Fish)
{
	for (const FVDBU6ExpertImpl::FRow& R : FVDBU6ExpertImpl::Rows())
	{
		if (!Fish.ContainsByPredicate([&R](const FFVFishDef& D) { return D.Id == R.Fish.Id; })) { Fish.Add(R.Fish); }
	}
}

const TArray<FFVExpertRodDef>& FVDBU6::ExpertRods()
{
	static const TArray<FFVExpertRodDef> A = []()
	{
		TArray<FFVExpertRodDef> Out;
		for (const FVDBU6ExpertImpl::FRodRow& R : FVDBU6ExpertImpl::RodRows()) { Out.Add(R.Ex); }
		return Out;
	}();
	return A;
}

const TArray<FFVExpertFishDef>& FVDBU6::ExpertFish()
{
	static const TArray<FFVExpertFishDef> A = []()
	{
		TArray<FFVExpertFishDef> Out;
		for (const FVDBU6ExpertImpl::FRow& R : FVDBU6ExpertImpl::Rows()) { Out.Add(R.Ex); }
		return Out;
	}();
	return A;
}

const FFVExpertRodDef* FVDBU6::FindExpertRod(FName RodId)
{
	for (const FFVExpertRodDef& R : ExpertRods())
	{
		if (R.RodId == RodId) { return &R; }
	}
	return nullptr;
}

const FFVExpertFishDef* FVDBU6::FindExpertFish(FName FishId)
{
	for (const FFVExpertFishDef& F : ExpertFish())
	{
		if (F.FishId == FishId) { return &F; }
	}
	return nullptr;
}

const FFVFishDef* FVDBU6::FindExpertLegend(EFVExpertLegend Legend)
{
	if (Legend == EFVExpertLegend::None) { return nullptr; }
	for (const FFVExpertFishDef& F : ExpertFish())
	{
		if (F.Legend == Legend) { return FVDB::FindFish(F.FishId); }
	}
	return nullptr;
}

const TArray<FFVFishFact>& FVDBU6::Facts(FName FishId)
{
	const FFVExpertFishDef* F = FindExpertFish(FishId);
	return F ? F->Facts : FVDBU6ExpertImpl::NoFacts();
}

FString FVDBU6::RodTypeName(EFVExpertRodType T)
{
	switch (T)
	{
	case EFVExpertRodType::Pendulum: return TEXT("Маятник");
	case EFVExpertRodType::Twin: return TEXT("Двойчатка");
	case EFVExpertRodType::Compass: return TEXT("Импульс");
	case EFVExpertRodType::Magnet: return TEXT("Магнит");
	case EFVExpertRodType::Helm: return TEXT("Ползунок");
	default: return FString();
	}
}

FString FVDBU6::RodTypeLabel(const FFVRodDef& Rod)
{
	return Rod.bExpert && Rod.ExpertType < EFVExpertRodType::Count ? RodTypeName(Rod.ExpertType) : FString(TEXT("Обычная"));
}

bool FVDBU6::IsRodSold(const FFVRodDef& Rod)
{
	return Rod.Price > 0 && Rod.TraderMask != 0;
}

const FFVRecipeDef* FVDBU6::RodRecipe(FName RodId)
{
	if (RodId.IsNone()) { return nullptr; }
	for (const FFVRecipeDef& R : FVDB::Recipes())
	{
		if (R.OutputId == RodId) { return &R; }
	}
	return nullptr;
}

FName FVDBU6::RodRequiredAchievement(const FFVRodDef& Rod)
{
	if (const FFVRecipeDef* R = RodRecipe(Rod.Id)) { return R->RequiredAchievement; }
	return IsRodSold(Rod) ? Rod.RequiredAchievement : NAME_None;
}

FString FVDBU6::RodSourceText(const FFVRodDef& Rod)
{
	auto AchName = [](FName Id) -> FString
	{
		const FFVAchievementDef* A = FVDB::FindAchievement(Id);
		return A ? A->Name : Id.ToString();
	};
	if (const FFVRecipeDef* R = RodRecipe(Rod.Id))
	{
		// «Удочка «Бриз»» -> «удочки «Бриз»» (the rod names all start with «Удочка »)
		FString From;
		for (const FFVIngredient& I : R->Inputs)
		{
			if (const FFVRodDef* In = FVDB::FindRod(I.ItemId))
			{
				FString Short = In->Name;
				Short.RemoveFromStart(TEXT("Удочка "));
				From = FString::Printf(TEXT("удочки %s"), *Short);
				break;
			}
		}
		const TCHAR* Where = R->Yard == EFVYardBuilding::Forge ? (R->StationLevel >= 2 ? TEXT("в Закалочной кузне") : TEXT("в кузне"))
			: TEXT("в мастерской");
		FString S = From.IsEmpty() ? FString::Printf(TEXT("Ковка %s"), Where) : FString::Printf(TEXT("Перековка %s %s"), *From, Where);
		if (!R->RequiredAchievement.IsNone()) { S += FString::Printf(TEXT(" · нужно достижение «%s»"), *AchName(R->RequiredAchievement)); }
		return S;
	}
	if (IsRodSold(Rod))
	{
		FString Who;
		for (const FFVTraderDef& T : FVDB::Traders())
		{
			if (T.Mask & Rod.TraderMask) { Who = T.Name; break; }
		}
		FString S = Who.IsEmpty() ? FString(TEXT("Продаётся в лавке")) : FString::Printf(TEXT("Продаёт %s"), *Who);
		if (!Rod.RequiredAchievement.IsNone()) { S += FString::Printf(TEXT(" — после достижения «%s»"), *AchName(Rod.RequiredAchievement)); }
		return S;
	}
	return Rod.Price <= 0 ? FString(TEXT("Есть с самого начала")) : FString(TEXT("Пока не получить"));
}

FString FVDBU6::RodTypeHowTo(EFVExpertRodType T)
{
	// §19: one vertical track; the fish counts while its icon is inside the green bar
	switch (T)
	{
	case EFVExpertRodType::Pendulum:
		return TEXT("Нажатие (Пробел, ЛКМ, касание) переворачивает полосу: летит вверх или вниз, от краёв отскакивает.");
	case EFVExpertRodType::Twin:
		return TEXT("Две полосы ходят по всей шкале: держите — одна идёт вверх, другая вниз; отпустите — наоборот. Проходят сквозь друг друга; рыба в обеих сразу — в 1,5 раза быстрее.");
	case EFVExpertRodType::Compass:
		return TEXT("Каждое нажатие — толчок полосы вверх, между толчками она падает. Держать бесполезно — жмите чаще.");
	case EFVExpertRodType::Magnet:
		return TEXT("Полоса почти стоит. Держите — рыбу тянет к полосе. Рыба рвётся — отпустите, иначе лопнет леска.");
	case EFVExpertRodType::Helm:
		return TEXT("Ведите полосу пальцем или мышью вверх-вниз (ПК: ещё W/S). Полоса узкая и идёт с инерцией.");
	default:
		return FString();
	}
}

FString FVDBU6::SeaStateName(EFVSeaState S)
{
	switch (S)
	{
	case EFVSeaState::Calm: return TEXT("Штиль");
	case EFVSeaState::Wind: return TEXT("Ветер");
	case EFVSeaState::Storm: return TEXT("Шторм");
	case EFVSeaState::Fog: return TEXT("Туман");
	default: return FString();
	}
}

FString FVDBU6::HazardName(EFVHazard H)
{
	switch (H)
	{
	case EFVHazard::Current: return TEXT("Течение");
	case EFVHazard::Whirlpool: return TEXT("Водоворот");
	case EFVHazard::Wave: return TEXT("Волна");
	case EFVHazard::Piles: return TEXT("Сваи и коряги");
	case EFVHazard::Weeds: return TEXT("Водоросли");
	case EFVHazard::Darkness: return TEXT("Темнота");
	case EFVHazard::Drip: return TEXT("Капель");
	case EFVHazard::Spring: return TEXT("Подводный ключ");
	default: return FString();
	}
}

FString FVDBU6::ExpertLineName(EFVExpertLine L)
{
	return L == EFVExpertLine::Thin ? TEXT("тонкая") : L == EFVExpertLine::Heavy ? TEXT("тяжёлая") : TEXT("любая");
}

FString FVDBU6::ExpertFloatName(EFVExpertFloat F)
{
	switch (F)
	{
	case EFVExpertFloat::Surface: return TEXT("поверхностный");
	case EFVExpertFloat::Deep: return TEXT("глубинный");
	case EFVExpertFloat::Glow: return TEXT("светящийся");
	default: return TEXT("любой");
	}
}

FString FVDBU6::ExpertReelName(EFVExpertReel R)
{
	return R == EFVExpertReel::Light ? TEXT("лёгкая") : R == EFVExpertReel::Heavy ? TEXT("тяжёлая") : TEXT("любая");
}

EFVExpertLine FVDBU6::LineKindOf(FName ItemId)
{
	const FFVItemDef* D = FVDB::FindItem(ItemId);
	return (D && D->bExpert && D->Kind == EFVItemKind::Line && D->ExpertKind < uint8(EFVExpertLine::Count)) ? EFVExpertLine(D->ExpertKind) : EFVExpertLine::None;
}

EFVExpertFloat FVDBU6::FloatKindOf(FName ItemId)
{
	const FFVItemDef* D = FVDB::FindItem(ItemId);
	return (D && D->bExpert && D->Kind == EFVItemKind::Float && D->ExpertKind < uint8(EFVExpertFloat::Count)) ? EFVExpertFloat(D->ExpertKind) : EFVExpertFloat::None;
}

EFVExpertReel FVDBU6::ReelKindOf(FName ItemId)
{
	const FFVItemDef* D = FVDB::FindItem(ItemId);
	return (D && D->bExpert && D->Kind == EFVItemKind::Reel && D->ExpertKind < uint8(EFVExpertReel::Count)) ? EFVExpertReel(D->ExpertKind) : EFVExpertReel::None;
}

// ================================================================================================
// Local conditions (§8.1): deterministic from day + clock, nothing saved
// ================================================================================================
FFVLocalWeather FVDBU6::LocalWeather(FName LocationId, int32 Day, float ClockMinutes, EFVWeather Global)
{
	FFVLocalWeather W;
	if (!IsExpertLocation(LocationId)) { return W; }
	W.bValid = true;

	float Clock = FMath::IsFinite(ClockMinutes) ? FMath::Fmod(ClockMinutes, 1440.f) : 0.f;
	if (Clock < 0.f) { Clock += 1440.f; }
	const int32 Hour = FMath::Clamp(FMath::FloorToInt(Clock / 60.f), 0, 23);
	const float Minute = Clock - float(Hour) * 60.f;

	if (LocationId == CaveId())
	{
		// no weather under ground: always dark; the underwater spring beats 20 minutes of every hour (:20–:40)
		W.bDark = true;
		W.Sea = EFVSeaState::Calm;
		W.bSpring = Minute >= 20.f && Minute < 40.f;
		return W;
	}

	// City: evening tide 18–22, night 22–06; the sea changes every two hours (deterministic from the day)
	W.bTide = Hour >= 18 && Hour < 22;
	W.bDark = Hour >= 22 || Hour < 6;
	const uint32 D = uint32(FMath::Max(0, Day));
	const bool bRain = Global == EFVWeather::Rain;
	const uint32 Roll = FVDBU6ExpertImpl::Mix(D, uint32(Hour / 2) + 101u) % 100u;
	if (bRain)
	{
		W.Sea = Roll < 40u ? EFVSeaState::Storm : Roll < 75u ? EFVSeaState::Wind : EFVSeaState::Calm;   // storm: rain only
	}
	else
	{
		W.Sea = Roll < 35u ? EFVSeaState::Wind : EFVSeaState::Calm;
	}
	// fog only in the morning (06–10); a foggy morning is decided once per day
	if (Hour >= 6 && Hour < 10)
	{
		const uint32 FogRoll = FVDBU6ExpertImpl::Mix(D, 977u) % 100u;
		if (FogRoll < (bRain ? 25u : 45u)) { W.Sea = EFVSeaState::Fog; }
	}
	return W;
}

TArray<EFVHazard> FVDBU6::ExpertHazards(FName LocationId, const FFVLocalWeather& W, EFVDayPhase Phase, int32 Level, bool bNearPiles)
{
	TArray<EFVHazard> Out;
	if (!W.bValid || !IsExpertLocation(LocationId)) { return Out; }

	TArray<EFVHazard, TInlineAllocator<8>> Cands;   // in priority order (§10.7 «где и когда»)
	if (LocationId == CaveId())
	{
		Cands.Add(EFVHazard::Darkness);
		Cands.Add(W.bSpring ? EFVHazard::Spring : EFVHazard::Drip);
	}
	else
	{
		if (bNearPiles) { Cands.Add(EFVHazard::Piles); }
		if (W.Sea == EFVSeaState::Storm) { Cands.Add(EFVHazard::Wave); }
		if (W.bTide) { Cands.Add(EFVHazard::Whirlpool); }
		if (W.bDark) { Cands.Add(EFVHazard::Darkness); }
		if (Phase == EFVDayPhase::Day && W.Sea == EFVSeaState::Wind) { Cands.Add(EFVHazard::Current); }
		if (Phase == EFVDayPhase::Day && W.Sea == EFVSeaState::Calm) { Cands.Add(EFVHazard::Weeds); }
	}

	const int32 MaxCount = Level < 13 ? 1 : 2;   // before level 13 one hazard at a time; never two «invisibilities»
	bool bInvisible = false;
	for (EFVHazard H : Cands)
	{
		if (Out.Num() >= MaxCount) { break; }
		if (FVDBU6ExpertImpl::IsInvisibility(H))
		{
			if (bInvisible) { continue; }
			bInvisible = true;
		}
		Out.AddUnique(H);
	}
	return Out;
}

FFVExpertStats FVDBU6::ComposeExpertStats(FName RodId, FName Line, FName Float, FName Reel, const FFVExpertStats* Food)
{
	(void)RodId;   // a rod's «характер» lives in its FFVExpertRodDef numbers; its stats base is neutral
	FFVExpertStats S;
	const FName Ids[] = { Line, Float, Reel };
	const EFVItemKind Kinds[] = { EFVItemKind::Line, EFVItemKind::Float, EFVItemKind::Reel };
	for (int32 i = 0; i < 3; ++i)
	{
		if (Ids[i].IsNone()) { continue; }
		const FFVItemDef* D = FVDB::FindItem(Ids[i]);
		if (D && D->bExpert && D->Kind == Kinds[i]) { S.Combine(D->ExpertTackle); }
	}
	if (Food) { S.Combine(*Food); }
	S.Force = FVDBU6ExpertImpl::Sanitize(S.Force);
	S.ZoneSize = FVDBU6ExpertImpl::Sanitize(S.ZoneSize);
	S.LineStrength = FVDBU6ExpertImpl::Sanitize(S.LineStrength);
	S.Stability = FVDBU6ExpertImpl::Sanitize(S.Stability);
	return S;
}

// ================================================================================================
// What bites (§10.4, §10.6, §10.8, §10.9)
// ================================================================================================
bool FVDBU6::ExpertConditionsMet(const FFVExpertFishDef& Def, const FFVExpertCatchContext& Ctx)
{
	const FVDBU6ExpertImpl::FRow* Rw = FVDBU6ExpertImpl::FindRow(Def.FishId);
	if (!Rw || Ctx.Level < Rw->Fish.MinLevel) { return false; }
	// universal fish: place, time and local weather only — the tackle never matters (§10.4)
	const uint16 Need = Def.bUniversal ? FVDBU6ExpertImpl::Cov::PlaceWeather : uint16(0xFFFF);
	return FVDBU6ExpertImpl::CoverOk(Need, *Rw, Ctx);
}

const FFVFishDef* FVDBU6::PickExpertCatch(const FFVExpertCatchContext& Ctx, FRandomStream& Rng)
{
	if (!IsExpertLocation(Ctx.Location)) { return nullptr; }
	const float Luck = FMath::IsFinite(Ctx.ExtraLuck) ? FMath::Clamp(Ctx.ExtraLuck, 0.f, 3.f) : 0.f;

	// ---- legends first: own bait + all conditions = 100 % (Р17); the others a small chance per bite
	for (const FVDBU6ExpertImpl::FRow& Rw : FVDBU6ExpertImpl::Rows())
	{
		const FFVExpertFishDef& E = Rw.Ex;
		if (E.Legend == EFVExpertLegend::None || Rw.Fish.Location != Ctx.Location) { continue; }
		if (!ExpertConditionsMet(E, Ctx)) { continue; }
		const FFVFishDef* F = FVDB::FindFish(E.FishId);
		if (!F) { continue; }
		if (!E.OwnBait.IsNone())
		{
			if (Ctx.Bait == E.OwnBait) { return F; }
			continue;
		}
		const float Chance = FMath::Clamp(E.LegendChance * (1.f + 0.5f * Luck), 0.f, 0.5f);
		if (Rng.FRand() < Chance) { return F; }
	}

	// ---- ordinary expert fish whose conditions hold now (weights like the classic PickCatch)
	TArray<const FFVFishDef*, TInlineAllocator<16>> Cands;
	TArray<float, TInlineAllocator<16>> Weights;
	float Total = 0.f;
	for (const FVDBU6ExpertImpl::FRow& Rw : FVDBU6ExpertImpl::Rows())
	{
		const FFVExpertFishDef& E = Rw.Ex;
		if (E.Legend != EFVExpertLegend::None || Rw.Fish.Location != Ctx.Location) { continue; }
		if (!ExpertConditionsMet(E, Ctx)) { continue; }
		const FFVFishDef* F = FVDB::FindFish(E.FishId);
		if (!F) { continue; }
		float W = FMath::Max(0.f, F->Weight);
		if (!E.bUniversal) { W *= 1.25f; }                          // a matched configuration is rewarded
		if (F->Rarity >= EFVRarity::Epic) { W *= 1.f + Luck; }
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
			if (Roll <= 0.f) { return Cands[i]; }
		}
		return Cands.Last();
	}

	// ---- last resort: a universal fish of the place (never nothing on an expert rod)
	TArray<const FFVFishDef*, TInlineAllocator<4>> Uni;
	for (const FVDBU6ExpertImpl::FRow& Rw : FVDBU6ExpertImpl::Rows())
	{
		if (Rw.Ex.bUniversal && Rw.Ex.Legend == EFVExpertLegend::None && Rw.Fish.Location == Ctx.Location)
		{
			if (const FFVFishDef* F = FVDB::FindFish(Rw.Ex.FishId)) { Uni.Add(F); }
		}
	}
	return Uni.Num() > 0 ? Uni[Rng.RandRange(0, Uni.Num() - 1)] : nullptr;
}

TArray<FFVExpertCheckItem> FVDBU6::ExpertChecklist(const FFVExpertFishDef& Def, const FFVExpertCatchContext& Ctx,
	TFunctionRef<bool(int32 FactIndex)> IsKnown)
{
	TArray<FFVExpertCheckItem> Out;
	const FVDBU6ExpertImpl::FRow* Rw = FVDBU6ExpertImpl::FindRow(Def.FishId);
	if (!Rw) { return Out; }
	for (int32 i = 0; i < Rw->Ex.Facts.Num(); ++i)
	{
		FFVExpertCheckItem& It = Out.AddDefaulted_GetRef();
		It.Kind = Rw->Ex.Facts[i].Kind;
		It.bKnown = IsKnown(i);
		It.Label = It.bKnown ? Rw->Ex.Facts[i].Row : FString(TEXT("???"));
		It.bOk = It.bKnown && Rw->Cover.IsValidIndex(i) && FVDBU6ExpertImpl::CoverOk(Rw->Cover[i], *Rw, Ctx);
	}
	return Out;
}

FFVTreasure FVDBU6::RollExpertTreasure(FName LocationId, int32 Level, FRandomStream& Rng, bool& bOutJournalPage)
{
	FFVTreasure T;
	const bool bCave = LocationId == CaveId();
	const int32 L = FMath::Clamp(Level, FV::ClassicMaxLevel + 1, FV::ExpertMaxLevel);
	const float Coins = Rng.FRandRange(0.8f, 1.6f) * (bCave ? 2400.f : 1600.f) * (1.f + 0.08f * float(L - (FV::ClassicMaxLevel + 1)));
	T.Money = FMath::Max<int64>(100, int64(FMath::RoundToInt(Coins / 10.f)) * 10);

	// ~35 %: a page of the fisherman's journal (the caller picks the fact: UFVSaveGame::PickNextFact, §10.16)
	bOutJournalPage = Rng.FRand() < 0.35f;

	// ~30 %: deep worms
	if (Rng.FRand() < 0.30f)
	{
		const FName Worm(TEXT("bait_deepworm"));
		if (FVDB::FindBait(Worm))
		{
			T.BaitId = Worm;
			T.BaitCount = Rng.RandRange(2, 5);
		}
	}

	// ~30 %: an item (expert tackle of the City shop; hard rock — plenty of it in the Cave)
	if (Rng.FRand() < 0.30f)
	{
		struct FDrop { const TCHAR* Id; float W; int32 MinLevel; int32 MinCount; int32 MaxCount; };
		static const FDrop CityDrops[] = {
			{ TEXT("eline_thin"),     24.f, 11, 1, 1 },
			{ TEXT("eline_heavy"),    24.f, 11, 1, 1 },
			{ TEXT("efloat_surface"), 20.f, 11, 1, 1 },
			{ TEXT("efloat_deep"),    20.f, 11, 1, 1 },
			{ TEXT("mat_hardrock"),   12.f, 12, 1, 1 },
		};
		static const FDrop CaveDrops[] = {
			{ TEXT("mat_hardrock"),   50.f, 12, 1, 3 },
			{ TEXT("efloat_glow"),    18.f, 12, 1, 1 },
			{ TEXT("eline_thin"),     16.f, 12, 1, 1 },
			{ TEXT("eline_heavy"),    16.f, 12, 1, 1 },
		};
		const FDrop* Drops = bCave ? &CaveDrops[0] : &CityDrops[0];
		const int32 Num = bCave ? int32(UE_ARRAY_COUNT(CaveDrops)) : int32(UE_ARRAY_COUNT(CityDrops));
		float Total = 0.f;
		for (int32 i = 0; i < Num; ++i)
		{
			if (Drops[i].MinLevel <= L && FVDB::IsItemId(FName(Drops[i].Id))) { Total += Drops[i].W; }
		}
		if (Total > 0.f)
		{
			float Roll = Rng.FRandRange(0.f, Total);
			int32 Pick = INDEX_NONE;
			for (int32 i = 0; i < Num; ++i)
			{
				if (Drops[i].MinLevel > L || !FVDB::IsItemId(FName(Drops[i].Id))) { continue; }
				Pick = i;
				Roll -= Drops[i].W;
				if (Roll <= 0.f) { break; }
			}
			if (Pick != INDEX_NONE)
			{
				T.ItemId = FName(Drops[Pick].Id);
				T.ItemCount = Rng.RandRange(Drops[Pick].MinCount, Drops[Pick].MaxCount);
			}
		}
	}
	return T;
}

int32 FVDBU6::ExpertCatchXp(const FFVFishDef& F, EFVQuality Q, bool bPerfect)
{
	const int32 Classic = FVDB::CatchXp(F, Q, bPerfect);
	if (!F.bExpert) { return Classic; }   // Р18: a classic fish on an expert rod gives the classic reward
	// §9 / Р15: the fight is ~2x longer and a level needs ~1.5x more XP; x1.4 keeps an expert level at ~2x the time.
	// Expert legends (already x2 as bLegendary) fight 60–120 s: another x1.25.
	const float Mult = F.ExpertLegend != EFVExpertLegend::None ? 1.4f * 1.25f : 1.4f;
	return FMath::Max(1, FMath::RoundToInt(float(Classic) * Mult));
}
