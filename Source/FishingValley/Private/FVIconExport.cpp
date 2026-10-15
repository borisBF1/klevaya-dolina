// Art export mode (-fvexportart=<dir>): see FVIconExport.h.

#include "FVIconExport.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"
#include "FVFishing.h"
#include "FVGame.h"
#include "FVSaveGame.h"
#include "FVScene.h"
#include "FVSprites.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace FVIconExportImpl
{
	constexpr int32 Scale = 4;              // one art texel = 4x4 PNG px (nearest neighbour)
	constexpr float ShotEvery = 0.1f;       // seconds between gameplay frames
	constexpr float ForceBiteAfter = 1.6f;  // seconds of waiting before the bite is forced
	constexpr float ShowcaseHold = 2.6f;    // seconds of the catch card in the clip
	constexpr float TotalTimeout = 120.f;

	enum class EStage : uint8 { WaitGame, EnterWorld, Setup, FindSpot, Settle, Fish, Quit };

	struct FState
	{
		FString Dir;
		bool bGif = false;
		EStage Stage = EStage::WaitGame;
		int32 Frames = 0;
		double StageStart = 0.0;
		double Start = 0.0;
		double LastShot = -1.0;
		double WaitStart = -1.0;
		double ShowcaseStart = -1.0;
		int32 ShotIndex = 0;
		int32 Casts = 0;
		EFVFishState LastFishState = EFVFishState::Idle;
		FString FrameLog;
	};

	FState& S()
	{
		static FState State;
		return State;
	}

	/** RGBA pixels scaled by Scale, alpha kept, written as PNG through IImageWrapper. */
	bool SavePng(const FFVImage& Img, const FString& Path, int32 Up = Scale)
	{
		if (!Img.IsValid())
		{
			return false;
		}
		const int32 W = Img.W * Up, H = Img.H * Up;
		TArray<FColor> Big;
		Big.SetNumUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				FColor C = Img.Get(X / Up, Y / Up);
				if (C.A == 0)
				{
					C = FColor(0, 0, 0, 0);
				}
				Big[Y * W + X] = C;
			}
		}
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Png = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Big.GetData(), int64(Big.Num()) * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		const TArray64<uint8>& Data = Png->GetCompressed();
		return Data.Num() > 0 && FFileHelper::SaveArrayToFile(Data, *Path);
	}

	/** Opaque pixels present (hat None / pet None give an empty canvas). */
	bool HasPixels(const FFVImage& Img)
	{
		for (const FColor& C : Img.Px)
		{
			if (C.A > 0)
			{
				return true;
			}
		}
		return false;
	}

	const TCHAR* RarityName(EFVRarity R)
	{
		switch (R)
		{
		case EFVRarity::Common: return TEXT("common");
		case EFVRarity::Uncommon: return TEXT("uncommon");
		case EFVRarity::Rare: return TEXT("rare");
		case EFVRarity::Epic: return TEXT("epic");
		case EFVRarity::Legendary: return TEXT("legendary");
		default: return TEXT("common");
		}
	}

	struct FExporter
	{
		FString IconDir, SpriteDir;
		TArray<TSharedPtr<FJsonValue>> Items;
		TSet<FString> Done;
		int32 Written = 0, Failed = 0;

		TSharedPtr<FJsonObject> Add(const FString& Id, const FString& Name, const TCHAR* Kind, const FFVImage& Img)
		{
			if (Id.IsEmpty() || Done.Contains(Id) || !Img.IsValid() || !HasPixels(Img))
			{
				return nullptr;
			}
			Done.Add(Id);
			if (!SavePng(Img, IconDir / (Id + TEXT(".png"))))
			{
				++Failed;
				UE_LOG(LogFV, Warning, TEXT("FV: EXPORTART failed to write %s"), *Id);
				return nullptr;
			}
			++Written;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("id"), Id);
			O->SetStringField(TEXT("name"), Name);
			O->SetStringField(TEXT("kind"), Kind);
			O->SetNumberField(TEXT("w"), Img.W);
			O->SetNumberField(TEXT("h"), Img.H);
			Items.Add(MakeShared<FJsonValueObject>(O));
			return O;
		}

		void Sprite(const FString& Name, const FFVImage& Img, int32 Up = Scale)
		{
			if (Img.IsValid() && SavePng(Img, SpriteDir / (Name + TEXT(".png")), Up))
			{
				++Written;
			}
		}
	};

	void AddFish(FExporter& E, const FFVFishDef& F)
	{
		if (TSharedPtr<FJsonObject> O = E.Add(F.Id.ToString(), F.Name, TEXT("fish"), UFVSprites::BuildFish(F, false)))
		{
			O->SetStringField(TEXT("loc"), F.Location.ToString());
			O->SetStringField(TEXT("rarity"), RarityName(F.Rarity));
			O->SetBoolField(TEXT("legendary"), F.Rarity == EFVRarity::Legendary);
			O->SetBoolField(TEXT("expert"), F.bExpert);
			O->SetNumberField(TEXT("max_size"), F.MaxSize);
			O->SetStringField(TEXT("desc"), F.Description);
		}
	}

	void ExportIcons(const FString& Dir)
	{
		FExporter E;
		E.IconDir = Dir / TEXT("icons");
		E.SpriteDir = Dir / TEXT("sprites");
		IFileManager::Get().MakeDirectory(*E.IconDir, true);
		IFileManager::Get().MakeDirectory(*E.SpriteDir, true);

		// ---- fish (classic, update 6, expert fish and expert legends)
		for (const FFVFishDef& F : FVDB::Fish())
		{
			AddFish(E, F);
		}
		for (const FFVExpertFishDef& X : FVDBU6::ExpertFish())
		{
			if (const FFVFishDef* F = FVDB::FindFish(X.FishId))
			{
				AddFish(E, *F);
			}
		}
		// ---- junk, baits (crafted lures are drawn from their parts), rods, items, decor
		for (const FFVJunkDef& J : FVDB::Junk())
		{
			E.Add(J.Id.ToString(), J.Name, TEXT("junk"), UFVSprites::BuildJunk(J.Style));
		}
		for (const FFVBaitDef& B : FVDB::Baits())
		{
			E.Add(B.Id.ToString(), B.Name, TEXT("bait"),
				B.bLure ? UFVSprites::BuildLureIcon(B.LureBase, B.LureColor, B.LureGlitter) : UFVSprites::BuildBait(B.Style));
		}
		for (const FFVRodDef& R : FVDB::Rods())
		{
			if (TSharedPtr<FJsonObject> O = E.Add(R.Id.ToString(), R.Name, TEXT("rod"), UFVSprites::BuildRodIcon(R)))
			{
				O->SetBoolField(TEXT("expert"), R.bExpert);
			}
		}
		for (const FFVItemDef& It : FVDB::Items())
		{
			E.Add(It.Id.ToString(), It.Name, TEXT("item"), UFVSprites::BuildItemArt(It.Art, It.Tint, It.Tier));
		}
		for (const FFVDecorDef& D : FVDB::Decor())
		{
			E.Add(D.Id.ToString(), D.Name, TEXT("decor"), UFVSprites::BuildDecorIcon(D));
		}
		// ---- tools (pseudo ids of UFVSprites::ItemIcon)
		for (int32 L = 1; L <= 4; ++L)
		{
			E.Add(FString::Printf(TEXT("shovel_%d"), L), FString::Printf(TEXT("Лопата %d"), L), TEXT("tool"), UFVSprites::BuildShovelIcon(L));
			E.Add(FString::Printf(TEXT("axe_%d"), L), FString::Printf(TEXT("Топор %d"), L), TEXT("tool"), UFVSprites::BuildAxeIcon(L));
		}
		for (int32 L = 1; L <= 2; ++L)
		{
			E.Add(FString::Printf(TEXT("pickaxe_%d"), L), FString::Printf(TEXT("Кирка %d"), L), TEXT("tool"), UFVSprites::BuildPickaxeIcon(L));
		}
		// ---- cosmetics, as the wardrobe draws them (FVScreenWardrobe.cpp DrawCosmeticIcon)
		for (const FFVCosmeticDef& C : FVDB::Cosmetics())
		{
			FFVImage Img;
			const TCHAR* Kind = TEXT("cosmetic");
			switch (C.Slot)
			{
			case EFVCosmeticSlot::Hat:
				Img = UFVSprites::BuildHatIcon(EFVHatStyle(FMath::Min<int32>(C.Style, int32(EFVHatStyle::Count) - 1)), C.Primary, C.Secondary);
				Kind = TEXT("hat");
				break;
			case EFVCosmeticSlot::Outfit:
				Img = UFVSprites::BuildOutfitIcon(EFVOutfitStyle(FMath::Min<int32>(C.Style, int32(EFVOutfitStyle::Count) - 1)), C.Primary, C.Secondary);
				Kind = TEXT("outfit");
				break;
			case EFVCosmeticSlot::Bobber:
				Img = UFVSprites::BuildBobber(EFVBobberStyle(FMath::Min<int32>(C.Style, int32(EFVBobberStyle::Count) - 1)), 0);
				Kind = TEXT("bobber");
				break;
			case EFVCosmeticSlot::Pet:
				Img = UFVSprites::BuildPet(EFVPetStyle(FMath::Min<int32>(C.Style, int32(EFVPetStyle::Count) - 1)), EFVPetAnim::Idle, 0);
				Kind = TEXT("pet");
				break;
			default:
				break;
			}
			if (TSharedPtr<FJsonObject> O = E.Add(C.Id.ToString(), C.Name, Kind, Img))
			{
				O->SetBoolField(TEXT("elite"), C.bElite);
			}
		}
		// ---- UI icons used by the site / admin
		const TPair<EFVIcon, const TCHAR*> Ui[] = {
			{ EFVIcon::Coin, TEXT("coin") }, { EFVIcon::Xp, TEXT("xp") }, { EFVIcon::Level, TEXT("level") }, { EFVIcon::Fish, TEXT("fish") },
			{ EFVIcon::Trophy, TEXT("trophy") }, { EFVIcon::Map, TEXT("map") }, { EFVIcon::Shop, TEXT("shop") }, { EFVIcon::Book, TEXT("book") },
			{ EFVIcon::StarGold, TEXT("star_gold") }, { EFVIcon::StarIridium, TEXT("star_iridium") }, { EFVIcon::Heart, TEXT("heart") },
			{ EFVIcon::House, TEXT("house") }, { EFVIcon::Boat, TEXT("boat") }, { EFVIcon::Axe, TEXT("axe") }, { EFVIcon::Pickaxe, TEXT("pickaxe") },
			{ EFVIcon::Seed, TEXT("seed") }, { EFVIcon::Chest, TEXT("chest") }, { EFVIcon::Clock, TEXT("clock") }, { EFVIcon::Sun, TEXT("sun") },
			{ EFVIcon::Moon, TEXT("moon") }, { EFVIcon::Rain, TEXT("rain") }, { EFVIcon::Leaders, TEXT("leaders") }, { EFVIcon::ExpertRod, TEXT("expert_rod") },
			{ EFVIcon::Rod, TEXT("rod") }, { EFVIcon::Bait, TEXT("bait") }, { EFVIcon::Wardrobe, TEXT("wardrobe") }, { EFVIcon::Treasure, TEXT("treasure") },
		};
		for (const TPair<EFVIcon, const TCHAR*>& U : Ui)
		{
			if (U.Key != EFVIcon::Count)
			{
				E.Add(FString(TEXT("ui_")) + U.Value, U.Value, TEXT("ui"), UFVSprites::BuildIcon(U.Key));
			}
		}
		// ---- character sprites for the site header (default look and a straw hat), chest frames
		FFVAppearance Look;
		Look.Hat = EFVHatStyle::Straw;
		Look.HatPrimary = FColor(236, 200, 110);
		Look.HatSecondary = FColor(170, 60, 50);
		const TPair<EFVCharAnim, const TCHAR*> Anims[] = {
			{ EFVCharAnim::Idle, TEXT("idle") }, { EFVCharAnim::Walk, TEXT("walk") }, { EFVCharAnim::CastWindup, TEXT("cast_windup") },
			{ EFVCharAnim::CastRelease, TEXT("cast_release") }, { EFVCharAnim::FishWait, TEXT("fish_wait") }, { EFVCharAnim::Reel, TEXT("reel") },
			{ EFVCharAnim::HoldFish, TEXT("hold_fish") },
		};
		for (const TPair<EFVCharAnim, const TCHAR*>& A : Anims)
		{
			for (int32 F = 0; F < UFVSprites::CharacterFrameCount(A.Key); ++F)
			{
				if (A.Key == EFVCharAnim::Idle || A.Key == EFVCharAnim::Walk)   // fishing animations exist facing down only
				{
					E.Sprite(FString::Printf(TEXT("char_right_%s_%d"), A.Value, F), UFVSprites::BuildCharacter(Look, A.Key, EFVFacing::Right, F));
				}
				E.Sprite(FString::Printf(TEXT("char_down_%s_%d"), A.Value, F), UFVSprites::BuildCharacter(Look, A.Key, EFVFacing::Down, F));
			}
		}
		for (int32 F = 0; F < 2; ++F)
		{
			E.Sprite(FString::Printf(TEXT("bobber_%d"), F), UFVSprites::BuildBobber(EFVBobberStyle::Classic, F));
			E.Sprite(FString::Printf(TEXT("chest_%d"), F), UFVSprites::BuildChest(F));
		}
		for (int32 F = 0; F < UFVSprites::PetFrameCount(EFVPetAnim::Walk); ++F)
		{
			E.Sprite(FString::Printf(TEXT("pet_cat_walk_%d"), F), UFVSprites::BuildPet(EFVPetStyle::Cat, EFVPetAnim::Walk, F));
		}

		// ---- manifest: icons + locations (the site's location cards)
		TArray<TSharedPtr<FJsonValue>> Locs;
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("id"), L.Id.ToString());
			O->SetStringField(TEXT("name"), L.Name);
			O->SetStringField(TEXT("desc"), L.Description);
			O->SetNumberField(TEXT("min_level"), L.MinLevel);
			O->SetNumberField(TEXT("price"), L.UnlockPrice);
			Locs.Add(MakeShared<FJsonValueObject>(O));
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("scale"), Scale);
		Root->SetArrayField(TEXT("icons"), E.Items);
		Root->SetArrayField(TEXT("locations"), Locs);
		FString Json;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
		FFileHelper::SaveStringToFile(Json, *(Dir / TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogFV, Log, TEXT("FV: EXPORTART icons: %d files written, %d failed, %d icons in the manifest -> %s"), E.Written, E.Failed, E.Items.Num(), *Dir);
	}

	void SetStage(EStage St)
	{
		S().Stage = St;
		S().StageStart = FPlatformTime::Seconds();
		UE_LOG(LogFV, Log, TEXT("FV: EXPORTART stage %d"), int32(St));
	}

	void DismissAll(UFVGame* G)
	{
		for (int32 Guard = 0; G->HasModal() && Guard < 32; ++Guard)
		{
			G->DismissModal(-1);
		}
		if (G->GetScreen() != EFVScreen::None)
		{
			G->CloseScreen();
		}
	}

	/** A classic rod (the expert rods fight in the 2D field): the best owned one, else the starter rod. */
	void EquipClassicRod(UFVGame* G)
	{
		UFVSaveGame* P = G->GetProfile();
		if (!P)
		{
			return;
		}
		const FFVRodDef* Cur = FVDB::FindRod(P->EquippedRod);
		if (Cur && !Cur->bExpert)
		{
			return;
		}
		FName Best = FVDB::Rods().Num() > 0 ? FVDB::Rods()[0].Id : NAME_None;
		for (const FFVRodDef& R : FVDB::Rods())
		{
			if (!R.bExpert && P->OwnsRod(R.Id))
			{
				Best = R.Id;
			}
		}
		P->EquippedRod = Best;
		G->NotifyProfileChanged();
	}

	const TCHAR* StateName(EFVFishState St)
	{
		switch (St)
		{
		case EFVFishState::Idle: return TEXT("idle");
		case EFVFishState::Charging: return TEXT("charging");
		case EFVFishState::Casting: return TEXT("casting");
		case EFVFishState::Waiting: return TEXT("waiting");
		case EFVFishState::Bite: return TEXT("bite");
		case EFVFishState::Minigame: return TEXT("minigame");
		case EFVFishState::Showcase: return TEXT("showcase");
		case EFVFishState::Escaped: return TEXT("escaped");
		case EFVFishState::Retrieving: return TEXT("retrieving");
		default: return TEXT("?");
		}
	}

	void Finish()
	{
		FState& St = S();
		if (!St.FrameLog.IsEmpty())
		{
			FFileHelper::SaveStringToFile(St.FrameLog, *(St.Dir / TEXT("gif_frames.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		UE_LOG(LogFV, Log, TEXT("FV: EXPORTART DONE (%d gameplay frames)"), St.ShotIndex);
		SetStage(EStage::Quit);
		FPlatformMisc::RequestExit(false, TEXT("FVIconExport"));
	}

	bool Tick(float Dt)
	{
		FState& St = S();
		++St.Frames;
		const double Now = FPlatformTime::Seconds();
		const double InStage = Now - St.StageStart;
		if (St.Stage == EStage::Quit)
		{
			return false;
		}
		UFVGame* G = UFVGame::Get();
		if (St.Stage != EStage::WaitGame && (!G || Now - St.Start > TotalTimeout))
		{
			UE_LOG(LogFV, Warning, TEXT("FV: EXPORTART WARNING timeout / no game (stage %d)"), int32(St.Stage));
			Finish();
			return false;
		}
		switch (St.Stage)
		{
		case EStage::WaitGame:
			if (G && G->GetSprites() && St.Frames > 20)
			{
				St.Start = Now;
				ExportIcons(St.Dir);
				if (!St.bGif)
				{
					Finish();
					return false;
				}
				SetStage(EStage::EnterWorld);
			}
			break;
		case EStage::EnterWorld:
			if (InStage > 1.0)
			{
				DismissAll(G);
				if (!G->IsInGame() && !(G->HasSaveGame() && G->ContinueGame()))
				{
					G->NewGame();
				}
				SetStage(EStage::Setup);
			}
			break;
		case EStage::Setup:
			DismissAll(G);
			if (InStage > 1.0 && G->IsInGame())
			{
				G->SetWeather(EFVWeather::Sunny);
				if (G->GetScene().GetLocationId() != FName(TEXT("pond")) && !G->TravelTo(FName(TEXT("pond"))))
				{
					UE_LOG(LogFV, Warning, TEXT("FV: EXPORTART WARNING travel to the pond failed"));
				}
				G->SetClockMinutes(9.f * 60.f + 40.f);
				EquipClassicRod(G);
				SetStage(EStage::FindSpot);
			}
			break;
		case EStage::FindSpot:
			DismissAll(G);
			if (InStage > 1.0)
			{
				FVScene& Scene = G->GetScene();
				const FVector2D From = Scene.GetPlayerPos();
				bool bFound = Scene.CanFishFromHere();
				for (int32 Dy = 0; Dy <= 480 && !bFound; Dy += 4)
				{
					for (const float Dx : { 0.f, -40.f, 40.f, -90.f, 90.f, -150.f, 150.f })
					{
						Scene.DebugTeleport(FVector2D(From.X + Dx, Scene.GetShoreY() - 6.f - 240.f + Dy));
						if (Scene.CanFishFromHere())
						{
							bFound = true;
							break;
						}
					}
				}
				UE_LOG(LogFV, Log, TEXT("FV: EXPORTART fishing spot %s at (%.0f, %.0f)"), bFound ? TEXT("found") : TEXT("NOT found"),
					Scene.GetPlayerPos().X, Scene.GetPlayerPos().Y);
				SetStage(EStage::Settle);
			}
			break;
		case EStage::Settle:
			DismissAll(G);
			if (InStage > 1.5)
			{
				G->GetFishing().SetAutoPlay(true);
				SetStage(EStage::Fish);
			}
			break;
		case EStage::Fish:
		{
			FVFishing& F = G->GetFishing();
			const EFVFishState FS = F.GetState();
			if (FS != EFVFishState::Showcase && G->HasModal())
			{
				DismissAll(G);
			}
			if (FS == EFVFishState::Charging && St.LastFishState != EFVFishState::Charging)
			{
				++St.Casts;
			}
			if (FS == EFVFishState::Waiting)
			{
				if (St.WaitStart < 0.0)
				{
					St.WaitStart = Now;
				}
				else if (Now - St.WaitStart > ForceBiteAfter)
				{
					F.DebugForceBite(FName(TEXT("fish_perch")), false);
					St.WaitStart = -1.0;
				}
			}
			else
			{
				St.WaitStart = -1.0;
			}
			if (FS == EFVFishState::Showcase)
			{
				if (St.ShowcaseStart < 0.0)
				{
					St.ShowcaseStart = Now;
					F.SetAutoPlay(false);   // keep the catch card on screen
				}
				else if (Now - St.ShowcaseStart > ShowcaseHold)
				{
					Finish();
					return false;
				}
			}
			St.LastFishState = FS;
			if (Now - St.LastShot >= ShotEvery && !FScreenshotRequest::IsScreenshotRequested())
			{
				St.LastShot = Now;
				++St.ShotIndex;
				const FString Name = FString::Printf(TEXT("gif_%04d"), St.ShotIndex);
				G->RequestShot(Name);
				St.FrameLog += FString::Printf(TEXT("%s %s %d %.2f\n"), *Name, StateName(FS), St.Casts, Now - St.StageStart);
			}
			break;
		}
		default:
			break;
		}
		return true;
	}
}

void FVIconExportMaybeStart()
{
	using namespace FVIconExportImpl;
	static bool bStarted = false;
	FString Dir;
	if (bStarted || !FParse::Value(FCommandLine::Get(), TEXT("fvexportart="), Dir) || Dir.IsEmpty())
	{
		return;
	}
	bStarted = true;
	Dir.TrimQuotesInline();
	S().Dir = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Dir) ? FPaths::ProjectSavedDir() / Dir : Dir);
	S().bGif = FParse::Param(FCommandLine::Get(), TEXT("fvexportgif"));
	IFileManager::Get().MakeDirectory(*S().Dir, true);
	// never the player's save, never the real server
	UFVSaveGame::SetAutoTestSlot(true);
	if (!FString(FCommandLine::Get()).Contains(TEXT("fvserver=")))
	{
		FCommandLine::Append(TEXT(" -fvserver=off"));
	}
	UE_LOG(LogFV, Log, TEXT("FV: EXPORTART mode -> %s%s"), *S().Dir, S().bGif ? TEXT(" (+ gameplay frames)") : TEXT(""));
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FVIconExportImpl::Tick), 0.f);
}
