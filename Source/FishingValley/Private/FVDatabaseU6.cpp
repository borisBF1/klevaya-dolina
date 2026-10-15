// UPDATE 6 [DATA-CORE] — table hooks and special ids (Public/FVDatabaseU6.h). Owner: DATA-CORE.
// The order of the appenders is part of the contract (rows are appended, never inserted).

#include "FVDatabaseU6.h"
#include "FVDatabase.h"

void FVDBU6::AppendClassicTables(const FClassicTables& T)
{
	if (T.Locations) { AppendLocations(*T.Locations); }         // DATA-CONTENT
	if (T.Fish) { AppendFish(*T.Fish); }                        // DATA-CONTENT (ordinary fish)
	if (T.Fish) { AppendExpertFish(*T.Fish); }                  // DATA-EXPERT (expert fish + legends)
	if (T.Rods) { AppendExpertRods(*T.Rods); }                  // DATA-EXPERT
	if (T.Junk) { AppendJunk(*T.Junk); }                        // DATA-ITEMS
	if (T.Baits) { AppendBaits(*T.Baits); }                     // DATA-ITEMS
	if (T.Cosmetics) { AppendCosmetics(*T.Cosmetics); }         // DATA-CONTENT
	if (T.Achievements) { AppendAchievements(*T.Achievements); }// DATA-CONTENT
	// §18 «Старая мельница» [MILL] (FVDatabaseU6Mill.cpp) — always last
	if (T.Locations) { FVMill::AppendLocations(*T.Locations); }
	if (T.Fish) { FVMill::AppendFish(*T.Fish); }
	if (T.Baits) { FVMill::AppendBaits(*T.Baits); }
	if (T.Cosmetics) { FVMill::AppendCosmetics(*T.Cosmetics); }
	if (T.Achievements) { FVMill::AppendAchievements(*T.Achievements); }
}

void FVDBU6::AppendHomeTables(const FHomeTables& T)
{
	if (T.Items) { AppendItems(*T.Items); }                     // DATA-ITEMS
	if (T.Recipes) { AppendRecipes(*T.Recipes); }               // DATA-ITEMS
	if (T.Stations) { AppendStationLevels(*T.Stations); }       // DATA-ITEMS
	if (T.Traders) { AppendTraders(*T.Traders); }               // DATA-ITEMS
	if (T.Decor) { AppendDecor(*T.Decor); }                     // DATA-CONTENT
	// §18 «Старая мельница» [MILL] (FVDatabaseU6Mill.cpp) — always last (items also take §18 «Магазины» off sale)
	if (T.Items) { FVMill::AppendItems(*T.Items); }
	if (T.Recipes) { FVMill::AppendRecipes(*T.Recipes); }
	if (T.Traders) { FVMill::AppendTraders(*T.Traders); }
}

FName FVDBU6::ForestId() { static const FName N(TEXT("forest")); return N; }
FName FVDBU6::CityId() { static const FName N(TEXT("city")); return N; }
FName FVDBU6::CaveId() { static const FName N(TEXT("cave")); return N; }

FName FVDBU6::YardBuildingId(EFVYardBuilding B)
{
	static const FName Ids[] = { FName(TEXT("yb_sawmill")), FName(TEXT("yb_garden")), FName(TEXT("yb_forge")) };
	const int32 I = int32(B);
	return (I >= 0 && I < UE_ARRAY_COUNT(Ids)) ? Ids[I] : NAME_None;
}

bool FVDBU6::IsForestId(FName LocationId)
{
	return LocationId == ForestId();
}

bool FVDBU6::IsExpertLocation(FName LocationId)
{
	return LocationId == CityId() || LocationId == CaveId();
}

const FFVLocationDef& FVDBU6::ForestLocation()
{
	static FFVLocationDef Def;
	static bool bInit = false;
	if (!bInit)
	{
		bInit = true;
		Def.Id = ForestId();
		Def.Name = TEXT("Лес");
		Def.Description = TEXT("Опушка за Лесной рекой: ветки, камни, берёзы и сосны. За завалом — Чаща, где живут "
			"Лесник Потапыч и Прораб Гаврила. Рубить можно только деревья с зарубкой — нужен топор.");
		Def.Theme = EFVTheme::Forest;
		Def.MinLevel = 4;            // §1: the forest opens at level 4 (free, a path from the river)
		Def.UnlockPrice = 0;
		// §3: left of the Forest river (168,188) on the scrolling map (640x360 map space; the map may go below x = 0)
		Def.MapPos = FVector2D(64.f, 176.f);
		Def.JunkChance = 0.f;        // no water: nothing is fished here
		Def.TreasureChance = 0.f;
		Def.bExpertZone = false;
		Def.bBigWorld = true;
	}
	return Def;
}
