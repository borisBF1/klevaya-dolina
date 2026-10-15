// Online client (update 4): registration, profile sync, save backups, leaderboards, mail, restore codes.
// Contract: Public/FVOnline.h. Protocol: Docs/UPDATE4_ONLINE.md §3 (API), §4 (seasons), §7 (client behaviour).
//
// Design notes
// * Every request is an asynchronous HTTP POST (FHttpModule, JSON body in UTF-8, 15 s timeout). Completion delegates run
//   on the game thread; they hold only a weak pointer to FImpl, so a request finishing after Shutdown() does nothing.
// * Signed requests add pid / ts (server clock via the measured offset) / nonce and the header
//   X-FV-Sig = lowercase hex HMAC-SHA1(key = the 64-char ASCII secret, message = the exact UTF-8 body bytes).
// * Background work (ping, registration, pending rename, sync, save backup) runs one request at a time, coalesced by
//   flags, with exponential back-off after network failures. User calls (boards, profiles, mail, rename, restore) are
//   sent at once; identical in-flight board / profile / mail requests share one HTTP request.

#include "FVOnline.h"
#include "FVGame.h"
#include "FVSaveGame.h"
#include "FVDatabase.h"
#include "FVDraw.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/SecureHash.h"
#include "Misc/Base64.h"
#include "Misc/Crc.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/StringConv.h"

// ================================================================================================
// File-local helpers (unique namespace)
// ================================================================================================
namespace FVOnlineImpl
{
	using FJsonObjPtr = TSharedPtr<FJsonObject>;
	using FJsonArr = TArray<TSharedPtr<FJsonValue>>;

	constexpr float RequestTimeoutSec = 15.f;
	constexpr float SyncIntervalSec = 300.f;      // every 5 min of play
	constexpr float BackupIntervalSec = 900.f;    // every 15 min of play
	constexpr float ImportantDelaySec = 5.f;      // level-up / legendary / achievement / sleep -> sync within 5 s
	constexpr float StartDelaySec = 3.f;          // first sync after entering the world
	constexpr float MinSyncSpacingSec = 8.f;
	constexpr float MinBackupSpacingSec = 60.f;
	constexpr float MaxBackoffSec = 300.f;
	constexpr float SleepJumpMinutes = 90.f;      // a clock jump this large in one frame = the player slept
	constexpr int32 MaxSaveBytes = 700 * 1024;
	constexpr int64 SeasonSeconds = 259200;       // 3 days
	constexpr int32 MaxClaimedIds = 300;
	constexpr int32 MaxNameLen = 16;
	constexpr int32 MaxStatusLen = 40;

	// ---- update 4b: versions / start-up gate (Docs/UPDATE4_ONLINE.md §10)
	constexpr int32 ClientBuildNumber = 9;        // bump with every release (keep DefaultEngine.ini StoreVersion in step)
	const TCHAR* const ClientVersionText = TEXT("0.6.2");
	// Base address of YOUR deployment of Server/Fishing-game (the folder URL, with the trailing slash): the download page
	// is the folder itself, the API is <folder>/api.php. The public source ships a placeholder, so a build from source
	// stays in offline mode until this points at a real server. It can also be changed without rebuilding:
	// [FishingValley.Online] ServerUrl="https://.../api.php" in Config/DefaultGame.ini, or the -fvserver=<url> switch.
#ifndef FV_SERVER_BASE_URL
#define FV_SERVER_BASE_URL "https://example.com/Fishing-game/"
#endif
	const TCHAR* const DefaultDownloadPage = TEXT(FV_SERVER_BASE_URL);
	const TCHAR* const DefaultServerUrl = TEXT(FV_SERVER_BASE_URL) TEXT("api.php");   // UFVOnline::ConfiguredServerUrl
	constexpr double StartupTimeoutSec = 50.0;    // ping + sync + mail, 15 s each at worst
	constexpr double StartupRecheckSec = 20.0;    // StartupCheck() right after a successful check keeps the result
	constexpr double SeasonProbeSpacingSec = 60.0;// a finished tournament: ask for the next one at most once a minute
	// 0.4.1: offline mode lasts only while the server is unreachable — a quiet ping now and then brings the game back
	constexpr double OfflineFirstProbeSec = 20.0;
	constexpr double OfflineProbeSpacingSec = 60.0;

	/** Result of one API call. */
	struct FReply
	{
		bool bReached = false;       // a JSON reply {"ok":...} came back
		bool bOk = false;
		FString Error;               // server error code, or "network" / "server" / "disabled" / "not_registered"
		FString Message;             // Russian text for the UI
		FJsonObjPtr Json;
	};

	int64 LocalUnixNow()
	{
		return FDateTime::UtcNow().ToUnixTimestamp();
	}

	// ---------------------------------------------------------------------------------------- JSON reading
	double ValueNum(const TSharedPtr<FJsonValue>& V, double Def)
	{
		if (!V.IsValid())
		{
			return Def;
		}
		switch (V->Type)
		{
		case EJson::Number:
			return V->AsNumber();
		case EJson::Boolean:
			return V->AsBool() ? 1.0 : 0.0;
		case EJson::String:
		{
			FString S = V->AsString();
			S.TrimStartAndEndInline();
			return (!S.IsEmpty() && S.IsNumeric()) ? FCString::Atod(*S) : Def;
		}
		default:
			return Def;
		}
	}

	TSharedPtr<FJsonValue> FieldOf(const FJsonObjPtr& O, const TCHAR* Key)
	{
		return O.IsValid() ? O->TryGetField(Key) : TSharedPtr<FJsonValue>();
	}

	double NumOf(const FJsonObjPtr& O, const TCHAR* Key, double Def = 0.0)
	{
		return ValueNum(FieldOf(O, Key), Def);
	}

	int64 IntOf(const FJsonObjPtr& O, const TCHAR* Key, int64 Def = 0)
	{
		const double D = NumOf(O, Key, double(Def));
		return int64(FMath::Clamp(D, -9.0e15, 9.0e15));
	}

	int32 Int32Of(const FJsonObjPtr& O, const TCHAR* Key, int32 Def = 0)
	{
		return int32(FMath::Clamp<int64>(IntOf(O, Key, Def), MIN_int32, MAX_int32));
	}

	FString StrOf(const FJsonObjPtr& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> V = FieldOf(O, Key);
		if (!V.IsValid())
		{
			return FString();
		}
		if (V->Type == EJson::String)
		{
			return V->AsString();
		}
		if (V->Type == EJson::Number)
		{
			return FString::Printf(TEXT("%lld"), (long long)V->AsNumber());
		}
		return FString();
	}

	bool BoolOf(const FJsonObjPtr& O, const TCHAR* Key, bool bDef = false)
	{
		return ValueNum(FieldOf(O, Key), bDef ? 1.0 : 0.0) != 0.0;
	}

	FJsonObjPtr ObjOf(const FJsonObjPtr& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> V = FieldOf(O, Key);
		return (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : FJsonObjPtr();
	}

	const FJsonArr* ArrOf(const FJsonObjPtr& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> V = FieldOf(O, Key);
		return (V.IsValid() && V->Type == EJson::Array) ? &V->AsArray() : nullptr;
	}

	FJsonObjPtr AsObj(const TSharedPtr<FJsonValue>& V)
	{
		return (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : FJsonObjPtr();
	}

	FName NameOf(const FJsonObjPtr& O, const TCHAR* Key)
	{
		const FString S = StrOf(O, Key).TrimStartAndEnd();
		return S.IsEmpty() ? NAME_None : FName(*S);
	}

	// ---------------------------------------------------------------------------------------- JSON text
	FString ToJson(const FJsonObjPtr& O)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(O, W);
		return Out;
	}

	FJsonObjPtr ParseJsonObject(FString Text)
	{
		// Tolerate a UTF-8 BOM and stray PHP notices around the JSON object.
		if (Text.Len() > 0 && Text[0] == TCHAR(0xFEFF))
		{
			Text.RightChopInline(1);
		}
		FJsonObjPtr Obj;
		{
			const TSharedRef<TJsonReader<TCHAR>> R = TJsonReaderFactory<TCHAR>::Create(Text);
			if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
			{
				return Obj;
			}
		}
		const int32 First = Text.Find(TEXT("{"));
		const int32 Last = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (First != INDEX_NONE && Last > First)
		{
			const TSharedRef<TJsonReader<TCHAR>> R = TJsonReaderFactory<TCHAR>::Create(Text.Mid(First, Last - First + 1));
			FJsonObjPtr Obj2;
			if (FJsonSerializer::Deserialize(R, Obj2) && Obj2.IsValid())
			{
				return Obj2;
			}
		}
		return FJsonObjPtr();
	}

	// ---------------------------------------------------------------------------------------- crypto / ids
	FString RandomHex(int32 NumChars)
	{
		FString Out;
		while (Out.Len() < NumChars)
		{
			Out += FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
		}
		return Out.Left(NumChars);
	}

	FString HmacHex(const FString& Secret, const TArray<uint8>& Body)
	{
		const FTCHARToUTF8 Key(*Secret);
		uint8 Hash[FSHA1::DigestSize];
		FSHA1::HMACBuffer(Key.Get(), uint32(Key.Length()), Body.GetData(), uint64(Body.Num()), Hash);
		return BytesToHexLower(Hash, FSHA1::DigestSize);
	}

	bool IsHex64(const FString& S)
	{
		if (S.Len() != 64)
		{
			return false;
		}
		for (const TCHAR C : S)
		{
			if (!FChar::IsHexDigit(C))
			{
				return false;
			}
		}
		return true;
	}

	FString PlatformName()
	{
#if PLATFORM_ANDROID
		return TEXT("android");
#else
		return TEXT("win");
#endif
	}

	/** The version the server sees (register "ver", User-Agent): the compiled-in client version, e.g. "0.4". */
	FString GameVersion()
	{
		return ClientVersionText;
	}

	bool IsHttpUrl(const FString& S)
	{
		return S.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) || S.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	}

	// ---------------------------------------------------------------------------------------- text
	const TCHAR* Plural(int64 N, const TCHAR* One, const TCHAR* Few, const TCHAR* Many)
	{
		const int64 A = N < 0 ? -N : N;
		const int64 M100 = A % 100;
		const int64 M10 = A % 10;
		if (M100 >= 11 && M100 <= 14)
		{
			return Many;
		}
		if (M10 == 1)
		{
			return One;
		}
		if (M10 >= 2 && M10 <= 4)
		{
			return Few;
		}
		return Many;
	}

	/** "12 345". */
	FString Grouped(int64 N)
	{
		const bool bNeg = N < 0;
		const uint64 A = bNeg ? uint64(-(N + 1)) + 1ull : uint64(N);
		const FString Digits = FString::Printf(TEXT("%llu"), (unsigned long long)A);
		FString Out;
		const int32 Len = Digits.Len();
		for (int32 i = 0; i < Len; ++i)
		{
			if (i > 0 && (Len - i) % 3 == 0)
			{
				Out.AppendChar(TEXT(' '));
			}
			Out.AppendChar(Digits[i]);
		}
		return bNeg ? (FString(TEXT("-")) + Out) : Out;
	}

	/** Millimetres -> "45,2 см". */
	FString CmText(int64 Mm)
	{
		if (Mm <= 0)
		{
			return TEXT("—");
		}
		return FString::Printf(TEXT("%lld,%lld см"), (long long)(Mm / 10), (long long)(Mm % 10));
	}

	FString NetErrorText()
	{
		return TEXT("Нет связи с сервером. Проверьте интернет и попробуйте ещё раз.");
	}

	FString OfflineText()
	{
		return TEXT("Офлайн-режим: онлайн-функции недоступны.");
	}

	FString ErrorText(const FString& Code, const FString& ServerMessage)
	{
		FString Msg = ServerMessage.TrimStartAndEnd();
		if (!Msg.IsEmpty())
		{
			return Msg;
		}
		if (Code == TEXT("bad_time"))        { return TEXT("Неверное время на устройстве. Проверьте часы."); }
		if (Code == TEXT("bad_sig"))         { return TEXT("Сервер не принял подпись запроса."); }
		if (Code == TEXT("banned"))          { return TEXT("Аккаунт заблокирован."); }
		if (Code == TEXT("rate_limited"))    { return TEXT("Слишком много запросов. Попробуйте через минуту."); }
		if (Code == TEXT("bad_name"))        { return TEXT("Такое имя нельзя использовать."); }
		if (Code == TEXT("bad_status"))      { return TEXT("Такой статус нельзя использовать."); }
		if (Code == TEXT("bad_code"))        { return TEXT("Код неверный, устарел или уже использован."); }
		if (Code == TEXT("already_claimed")) { return TEXT("Подарок уже получен."); }
		if (Code == TEXT("not_found") || Code == TEXT("no_player") || Code == TEXT("unknown_player"))
		{
			return TEXT("Игрок не найден.");
		}
		if (Code == TEXT("network"))         { return NetErrorText(); }
		if (Code == TEXT("offline"))         { return OfflineText(); }
		if (Code == TEXT("server"))          { return TEXT("Сервер временно недоступен. Попробуйте позже."); }
		if (Code == TEXT("disabled"))        { return TEXT("Онлайн-функции отключены."); }
		if (Code == TEXT("not_registered"))  { return TEXT("Профиль ещё не зарегистрирован на сервере."); }
		return FString::Printf(TEXT("Ошибка сервера (%s)."), Code.IsEmpty() ? TEXT("?") : *Code);
	}

	/** Letters of common alphabets (FChar::IsAlpha is locale / platform dependent). */
	bool IsNameLetter(TCHAR C)
	{
		const uint32 U = uint32(C);
		if ((U >= 'a' && U <= 'z') || (U >= 'A' && U <= 'Z'))
		{
			return true;
		}
		if (U == 0xD7 || U == 0xF7)
		{
			return false;                                   // × ÷
		}
		return (U >= 0x00C0 && U <= 0x024F)                // Latin-1 / Latin extended
			|| (U >= 0x0370 && U <= 0x03FF)                // Greek
			|| (U >= 0x0400 && U <= 0x052F)                // Cyrillic (+ supplement)
			|| (U >= 0x0531 && U <= 0x0587)                // Armenian
			|| (U >= 0x05D0 && U <= 0x05EA)                // Hebrew
			|| (U >= 0x0620 && U <= 0x064A)                // Arabic letters
			|| (U >= 0x10A0 && U <= 0x10FF)                // Georgian
			|| (U >= 0x3040 && U <= 0x30FF)                // Hiragana / Katakana
			|| (U >= 0x4E00 && U <= 0x9FFF)                // CJK
			|| (U >= 0xAC00 && U <= 0xD7AF);               // Hangul
	}

	/** Trims, collapses spaces and checks the allowed characters / length (the server re-validates + filters words). */
	bool NormaliseName(const FString& In, FString& Out, FString& OutError)
	{
		FString S;
		S.Reserve(In.Len());
		bool bPrevSpace = true;
		for (const TCHAR C : In)
		{
			if (FChar::IsWhitespace(C))
			{
				if (!bPrevSpace)
				{
					S.AppendChar(TEXT(' '));
					bPrevSpace = true;
				}
				continue;
			}
			const bool bOk = IsNameLetter(C) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_') || C == TEXT('-');
			if (!bOk)
			{
				OutError = TEXT("В имени можно использовать только буквы, цифры, пробел, «_» и «-».");
				return false;
			}
			S.AppendChar(C);
			bPrevSpace = false;
		}
		S.TrimEndInline();
		if (S.IsEmpty())
		{
			OutError = TEXT("Введите имя рыбака.");
			return false;
		}
		if (S.Len() > MaxNameLen)
		{
			OutError = FString::Printf(TEXT("Имя должно быть не длиннее %d символов."), MaxNameLen);
			return false;
		}
		Out = S;
		return true;
	}

	bool NormaliseStatus(const FString& In, FString& Out, FString& OutError)
	{
		FString S;
		S.Reserve(In.Len());
		bool bPrevSpace = true;
		for (const TCHAR C : In)
		{
			if (FChar::IsWhitespace(C) || C < 32)
			{
				if (!bPrevSpace)
				{
					S.AppendChar(TEXT(' '));
					bPrevSpace = true;
				}
				continue;
			}
			S.AppendChar(C);
			bPrevSpace = false;
		}
		S.TrimEndInline();
		if (S.Len() > MaxStatusLen)
		{
			OutError = FString::Printf(TEXT("Статус должен быть не длиннее %d символов."), MaxStatusLen);
			return false;
		}
		Out = S;
		return true;
	}

	/** Upper case, no spaces / dashes. */
	FString NormaliseCode(const FString& In)
	{
		FString Out;
		for (const TCHAR C : In)
		{
			if (FChar::IsWhitespace(C) || C == TEXT('-'))
			{
				continue;
			}
			Out.AppendChar(FChar::ToUpper(C));
		}
		return Out;
	}

	// ---------------------------------------------------------------------------------------- fish / profile
	EFVQuality QualityOf(int64 Q)
	{
		return EFVQuality(FMath::Clamp<int64>(Q, 0, int64(EFVQuality::Iridium)));
	}

	FFVFishShow FishFromTrophy(const FFVTrophy& T)
	{
		FFVFishShow F;
		F.FishId = T.FishId;
		F.SizeCm = T.SizeCm;
		F.Quality = QualityOf(T.Quality);
		return F;
	}

	/** {fish, size, q} object (or fields of an entry). */
	FFVFishShow FishFromJson(const FJsonObjPtr& O)
	{
		FFVFishShow F;
		if (!O.IsValid())
		{
			return F;
		}
		F.FishId = NameOf(O, TEXT("fish"));
		F.SizeCm = float(FMath::Max(0.0, NumOf(O, TEXT("size"))));
		F.Quality = QualityOf(IntOf(O, TEXT("q")));
		return F;
	}

	TSharedPtr<FJsonValue> FishValue(const FFVFishShow& F)
	{
		if (F.FishId.IsNone())
		{
			return MakeShared<FJsonValueNull>();
		}
		const FJsonObjPtr O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("fish"), F.FishId.ToString());
		O->SetNumberField(TEXT("size"), FMath::RoundToDouble(double(F.SizeCm) * 10.0) / 10.0);
		O->SetNumberField(TEXT("q"), double(uint8(F.Quality)));
		return MakeShared<FJsonValueObject>(O);
	}

	FString NameStr(FName N)
	{
		return N.IsNone() ? FString() : N.ToString();
	}

	const TCHAR* CatKey(EFVBoardCategory C)
	{
		switch (C)
		{
		case EFVBoardCategory::Level:         return TEXT("level");
		case EFVBoardCategory::FishTotal:     return TEXT("fish");
		case EFVBoardCategory::Biggest:       return TEXT("biggest");
		case EFVBoardCategory::Legendary:     return TEXT("legend");
		case EFVBoardCategory::Species:       return TEXT("species");
		case EFVBoardCategory::Earned:        return TEXT("earned");
		case EFVBoardCategory::Achievements:  return TEXT("achievements");
		case EFVBoardCategory::SeasonFish:    return TEXT("s_fish");
		case EFVBoardCategory::SeasonEarned:  return TEXT("s_earned");
		case EFVBoardCategory::SeasonXp:      return TEXT("s_xp");
		case EFVBoardCategory::SeasonBiggest: return TEXT("s_biggest");
		default:                              return TEXT("level");
		}
	}

	bool IsBiggestCat(EFVBoardCategory C)
	{
		return C == EFVBoardCategory::Biggest || C == EFVBoardCategory::SeasonBiggest;
	}

	FFVBoardEntry ParseEntry(const FJsonObjPtr& O, EFVBoardCategory Cat, int64 MyId)
	{
		FFVBoardEntry E;
		E.Rank = FMath::Max(0, Int32Of(O, TEXT("rank")));
		E.PlayerId = IntOf(O, TEXT("pid"));
		E.Name = StrOf(O, TEXT("name"));
		E.Tag = Int32Of(O, TEXT("tag"));
		if (E.Tag <= 0 && E.PlayerId > 0)
		{
			E.Tag = int32(FMath::Min<int64>(1000 + E.PlayerId, MAX_int32));
		}
		E.Level = FMath::Clamp(Int32Of(O, TEXT("level"), 1), 1, FV::ExpertMaxLevel);
		E.Fish = FishFromJson(O);
		if (IsBiggestCat(Cat))
		{
			// Server value = best size in cm (float); the client keeps millimetres.
			const double Cm = FMath::Max(0.0, NumOf(O, TEXT("size"), NumOf(O, TEXT("value"))));
			E.Value = int64(FMath::RoundToDouble(Cm * 10.0));
			E.Fish.SizeCm = float(Cm);
		}
		else
		{
			E.Value = FMath::Max<int64>(0, IntOf(O, TEXT("value")));
		}
		E.bMe = MyId > 0 && E.PlayerId == MyId;
		return E;
	}

	FFVPublicProfile ParseProfile(const FJsonObjPtr& O)
	{
		FFVPublicProfile P;
		P.bOk = true;
		P.PlayerId = IntOf(O, TEXT("pid"));
		P.Name = StrOf(O, TEXT("name"));
		P.Tag = Int32Of(O, TEXT("tag"));
		if (P.Tag <= 0 && P.PlayerId > 0)
		{
			P.Tag = int32(FMath::Min<int64>(1000 + P.PlayerId, MAX_int32));
		}
		P.Status = StrOf(O, TEXT("status"));
		P.Level = FMath::Clamp(Int32Of(O, TEXT("level"), 1), 1, FV::ExpertMaxLevel);
		P.Xp = FMath::Max(0, Int32Of(O, TEXT("xp")));
		P.XpEarned = FMath::Max<int64>(0, IntOf(O, TEXT("xp_earned"), P.Xp));   // update 6 (older servers: the total XP)
		P.XpSeniority = FMath::Max(0, Int32Of(O, TEXT("seniority")));
		P.FishTotal = FMath::Max(0, Int32Of(O, TEXT("fish_total")));
		P.Species = FMath::Max(0, Int32Of(O, TEXT("species")));
		P.Legendary = FMath::Max(0, Int32Of(O, TEXT("legendary")));
		P.Achievements = FMath::Max(0, Int32Of(O, TEXT("achievements")));
		P.CoinsEarned = FMath::Max<int64>(0, IntOf(O, TEXT("coins_earned")));
		P.Perfect = FMath::Max(0, Int32Of(O, TEXT("perfect")));
		P.PlaySeconds = FMath::Max(0, Int32Of(O, TEXT("playtime")));
		P.StationsBuilt = FMath::Max(0, Int32Of(O, TEXT("stations_built")));
		P.FavoriteLocation = NameOf(O, TEXT("favorite_location"));
		P.BestCatch = FishFromJson(ObjOf(O, TEXT("best_catch")));
		if (const FJsonArr* Show = ArrOf(O, TEXT("showcase")))
		{
			for (const TSharedPtr<FJsonValue>& V : *Show)
			{
				const FFVFishShow F = FishFromJson(AsObj(V));
				if (!F.FishId.IsNone() && P.Showcase.Num() < 3)
				{
					P.Showcase.Add(F);
				}
			}
		}
		const FJsonObjPtr Look = ObjOf(O, TEXT("appearance"));
		P.Hat = NameOf(Look, TEXT("hat"));
		P.Outfit = NameOf(Look, TEXT("outfit"));
		P.Bobber = NameOf(Look, TEXT("bobber"));
		P.Pet = NameOf(Look, TEXT("pet"));
		P.StartUnix = IntOf(O, TEXT("start_date"));
		if (P.StartUnix <= 0)
		{
			P.StartUnix = IntOf(O, TEXT("created"));
		}
		P.LastSeenUnix = IntOf(O, TEXT("last_seen"));
		P.bBanned = BoolOf(O, TEXT("banned"));
		if (const FJsonObjPtr Ranks = ObjOf(O, TEXT("ranks")))
		{
			const EFVBoardCategory Cats[] = {
				EFVBoardCategory::Level, EFVBoardCategory::FishTotal, EFVBoardCategory::Biggest, EFVBoardCategory::Legendary,
				EFVBoardCategory::Species, EFVBoardCategory::Earned, EFVBoardCategory::Achievements };
			for (const EFVBoardCategory C : Cats)
			{
				const int32 R = Int32Of(Ranks, CatKey(C));
				if (R > 0)
				{
					P.Ranks.Add(C, R);
				}
			}
		}
		return P;
	}

	int32 StationsBuilt(const UFVSaveGame& S)
	{
		int32 N = 0;
		for (int32 i = 0; i < int32(EFVStation::Count); ++i)
		{
			if (S.GetStationLevel(EFVStation(i)) > 0)
			{
				++N;
			}
		}
		return N;
	}

	/** The local fisherman's profile from the save (no server data). */
	FFVPublicProfile ProfileFromSave(const UFVSaveGame& S)
	{
		FFVPublicProfile P;
		P.bOk = true;
		P.bLocal = true;
		P.PlayerId = S.OnlineId;
		P.Name = S.PlayerName;
		P.Tag = S.OnlineTag;
		P.Status = S.PlayerStatus;
		P.Level = S.GetLevel();
		P.Xp = S.Xp;
		P.XpEarned = FMath::Max<int64>(0, S.U6.XpEarned);
		P.XpSeniority = FMath::Max(0, S.U6.XpSeniority);
		P.FishTotal = S.Stats.FishCaught;
		P.Species = S.SpeciesCaught();
		P.Legendary = S.Stats.Legendary;
		P.Achievements = S.CompletedAchievements.Num();
		P.CoinsEarned = S.Stats.Earned;
		P.Perfect = S.Stats.Perfect;
		P.PlaySeconds = FMath::Max(0, FMath::FloorToInt(S.Stats.PlaySeconds));
		P.StationsBuilt = StationsBuilt(S);

		// Favourite location: most fish caught there; fallback: most species caught there.
		FName Fav;
		int32 FavN = 0;
		for (const TPair<FName, int32>& KV : S.CatchesByLocation)
		{
			if (KV.Value <= 0 || !FVDB::FindLocation(KV.Key))
			{
				continue;
			}
			if (KV.Value > FavN || (KV.Value == FavN && FVDB::LocationIndex(KV.Key) > FVDB::LocationIndex(Fav)))
			{
				Fav = KV.Key;
				FavN = KV.Value;
			}
		}
		if (Fav.IsNone())
		{
			for (const FFVLocationDef& L : FVDB::Locations())
			{
				const int32 N = S.SpeciesCaughtAt(L.Id);
				if (N > FavN)
				{
					Fav = L.Id;
					FavN = N;
				}
			}
		}
		P.FavoriteLocation = Fav;

		// Showcase: the 3 most valuable species (price at the best quality), with their record size.
		struct FCand { FFVFishShow Show; int32 Price = 0; };
		TArray<FCand> Cands;
		for (const TPair<FName, FFVFishRecord>& KV : S.Records)
		{
			if (KV.Value.Caught <= 0)
			{
				continue;
			}
			const FFVFishDef* F = FVDB::FindFish(KV.Key);
			if (!F)
			{
				continue;
			}
			FCand C;
			C.Show.FishId = KV.Key;
			C.Show.SizeCm = KV.Value.MaxSize;
			C.Show.Quality = QualityOf(KV.Value.BestQuality);
			C.Price = FVDB::FishPrice(*F, C.Show.Quality);
			Cands.Add(C);
		}
		Cands.Sort([](const FCand& A, const FCand& B)
		{
			if (A.Price != B.Price)
			{
				return A.Price > B.Price;
			}
			if (A.Show.SizeCm != B.Show.SizeCm)
			{
				return A.Show.SizeCm > B.Show.SizeCm;
			}
			return A.Show.FishId.LexicalLess(B.Show.FishId);
		});
		for (int32 i = 0; i < Cands.Num() && i < 3; ++i)
		{
			P.Showcase.Add(Cands[i].Show);
		}

		if (!S.BestCatch.FishId.IsNone() && FVDB::FindFish(S.BestCatch.FishId))
		{
			P.BestCatch = FishFromTrophy(S.BestCatch);
		}
		else if (P.Showcase.Num() > 0)
		{
			P.BestCatch = P.Showcase[0];
		}

		P.Hat = S.EquippedHat;
		P.Outfit = S.EquippedOutfit;
		P.Bobber = S.EquippedBobber;
		P.Pet = S.EquippedPet;
		P.StartUnix = S.ProfileStartUnix;
		return P;
	}

	/** Can every attachment item be stored right now? */
	bool HasRoomFor(const UFVSaveGame& S, const FFVMail& M, FString& OutError)
	{
		for (const FFVMailItem& It : M.Items)
		{
			if (It.ItemId.IsNone() || It.Count <= 0)
			{
				continue;
			}
			if (FVDB::IsFishId(It.ItemId))
			{
				if (!S.CanAddTo(EFVContainer::Keepnet, It.ItemId, It.Count))
				{
					OutError = TEXT("Не хватает места в садке. Освободите место и попробуйте снова.");
					return false;
				}
			}
			else if (FVDB::IsJunkId(It.ItemId) || FVDB::IsItemId(It.ItemId))
			{
				if (!S.CanAddTo(EFVContainer::Backpack, It.ItemId, It.Count) && !S.CanAddTo(EFVContainer::Chest, It.ItemId, It.Count))
				{
					OutError = TEXT("Не хватает места в рюкзаке. Освободите место и попробуйте снова.");
					return false;
				}
			}
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------- tournament rewards (§4)
	int32 HalfUp(int32 L) { return (L + 1) / 2; }        // ⌈L/2⌉
	int32 QuarterUp(int32 L) { return (L + 3) / 4; }     // ⌈L/4⌉

	/** XP from the start of level L to L+1; at the top level the last step (10: 3100; update 6, level 15: 10 400). */
	int32 LevelSpan(int32 L)
	{
		const int32 Top = FMath::Max(2, FV::MaxLevel);
		const int32 From = FMath::Clamp(L, 1, Top - 1);
		const int32 Span = FVDB::XpForLevel(From + 1) - FVDB::XpForLevel(From);
		return Span > 0 ? Span : 3100;
	}

	// s_fish from level 11 (update 6, lib/season.php FV_EXPERT_BAIT*): the expert bait on top of the classic baits
	constexpr int32 ExpertBaitFromLevel = 11;
	const TCHAR* const ExpertBaitId = TEXT("bait_deepworm");
	int32 ExpertBaitCount(int32 Tier) { return Tier == 1 ? 5 : (Tier == 2 ? 3 : 2); }

	/** The local profile (nullptr outside the game). */
	const UFVSaveGame* LocalProfile()
	{
		const UFVGame* G = UFVGame::Get();
		return G ? G->GetProfile() : nullptr;
	}

	/** s_xp: coins by tier. */
	int32 SeasonXpCoins(int32 Tier, int32 L)
	{
		switch (Tier)
		{
		case 1:  return 300 + 200 * L;
		case 2:  return 150 + 100 * L;
		case 3:  return 50 + 30 * L;
		default: return 0;
		}
	}

	/** s_biggest: XP by tier (a share of the level span, rounded like PHP round()). */
	int32 SeasonBiggestXp(int32 Tier, int32 L)
	{
		const double Span = double(LevelSpan(L));
		switch (Tier)
		{
		case 1:  return FMath::Max(150, FMath::RoundToInt(Span * 0.30));
		case 2:  return FMath::Max(100, FMath::RoundToInt(Span * 0.15));
		case 3:  return FMath::Max(50, FMath::RoundToInt(Span * 0.06));
		default: return 0;
		}
	}

	FString ItemTitle(const TCHAR* Id)
	{
		const FString N = FVDB::ItemName(FName(Id));
		return N.IsEmpty() ? FString(Id) : N;
	}

	/** s_earned: the items are picked by the server (seed = season, player), so only kinds × count are known. */
	FString EarnedRewardText(int32 Kinds, int32 Count, int32 L)
	{
		const bool bTackle = L >= 4;   // the L4+ pools also hold floats / lines / nets
		FString S;
		if (Kinds <= 1)
		{
			S = FString::Printf(TEXT("%s ×%d"), bTackle ? TEXT("Случайный материал или снасть") : TEXT("Случайный материал"), Count);
		}
		else
		{
			S = FString::Printf(TEXT("%d %s %s по %d шт."), Kinds, Plural(Kinds, TEXT("вид"), TEXT("вида"), TEXT("видов")),
				bTackle ? TEXT("материалов и снастей") : TEXT("материалов"), Count);
		}
		if (bTackle && Count > 2)
		{
			S += TEXT(" (снастей — до 2 шт.)");
		}
		return S;
	}

	/** 0 none, 1 gold, 2 silver, 3 bronze — a number or "gold" / "silver" / "bronze". */
	int32 ParseTier(const TSharedPtr<FJsonValue>& V)
	{
		if (V.IsValid() && V->Type == EJson::String)
		{
			const FString T = V->AsString().TrimStartAndEnd().ToLower();
			if (T == TEXT("gold"))   { return 1; }
			if (T == TEXT("silver")) { return 2; }
			if (T == TEXT("bronze")) { return 3; }
		}
		return int32(FMath::Clamp(ValueNum(V, 0.0), 0.0, 3.0));
	}

	/** ping.version {latest_build, latest_name, min_build, download_url, changelog}; false when the server has none. */
	bool ParseVersion(const FJsonObjPtr& Root, FFVVersionInfo& Out)
	{
		FJsonObjPtr V = ObjOf(Root, TEXT("version"));
		if (!V.IsValid() && Root.IsValid() && (Root->HasField(TEXT("latest_build")) || Root->HasField(TEXT("min_build"))))
		{
			V = Root;   // tolerate the fields at the top level
		}
		if (!V.IsValid())
		{
			return false;  // an older server ("version": "1.0"): no version constraints
		}
		FFVVersionInfo I;
		I.bKnown = true;
		I.MinBuild = FMath::Max(0, Int32Of(V, TEXT("min_build")));
		I.LatestBuild = FMath::Max(I.MinBuild, FMath::Max(0, Int32Of(V, TEXT("latest_build"))));
		I.LatestName = StrOf(V, TEXT("latest_name")).TrimStartAndEnd();
		if (I.LatestName.IsEmpty() && I.LatestBuild > 0)
		{
			I.LatestName = FString::Printf(TEXT("сборка %d"), I.LatestBuild);
		}
		I.DownloadUrl = StrOf(V, TEXT("download_url")).TrimStartAndEnd();
		if (!IsHttpUrl(I.DownloadUrl))
		{
			I.DownloadUrl.Empty();
		}
		I.Changelog = StrOf(V, TEXT("changelog")).Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\r"), TEXT("\n")).TrimStartAndEnd();
		Out = MoveTemp(I);
		return true;
	}
}

// ================================================================================================
// FImpl
// ================================================================================================
struct UFVOnline::FImpl : public TSharedFromThis<UFVOnline::FImpl>
{
	using FReply = FVOnlineImpl::FReply;
	using FReplyFn = TFunction<void(const FReply&)>;
	using FResultFn = TFunction<void(bool, const FString&)>;

	TWeakObjectPtr<UFVGame> Game;
	FString Url;
	bool bEnabled = false;
	bool bShutdown = false;
	EFVOnlineStatus Status = EFVOnlineStatus::Disabled;
	FString BanReason;
	int64 TimeOffset = 0;                  // server - local, seconds
	bool bTimeKnown = false;

	FFVSeasonInfo Season;
	TArray<FFVMail> Mail;
	bool bMailFetched = false;
	int32 ServerUnread = 0;                // from sync (used until the mail list is fetched)

	// ---- scheduling (Clock = real seconds since Init)
	double Clock = 0.0;
	double NextBgAt = 0.0;                 // back-off: no background request before this
	int32 FailCount = 0;
	bool bBgBusy = false;
	bool bPingQueued = false;
	bool bSyncQueued = false;
	double SyncDueAt = 0.0;
	double LastSyncAt = -1000.0;
	bool bBackupQueued = false;
	double LastUploadAt = -1000.0;
	uint32 LastUploadCrc = 0;
	float PlaySinceSync = 0.f;
	float PlaySinceBackup = 0.f;

	// ---- session tracking
	TWeakObjectPtr<UFVSaveGame> LastProfile;
	int64 LastProfileId = -1;
	bool bWasInGame = false;
	double LastAbsMinutes = -1.0;

	// ---- registration / names
	bool bRegistering = false;
	FString RegRejectedName;               // never re-register with a name the server refused
	FString RegPrevName;                   // name before SetName (restored when the server refuses the new one)
	TArray<FResultFn> NameWaiters;
	bool bNamePending = false;             // local name not yet accepted by the server (retried in the background)
	bool bNameInFlight = false;
	FString NamePrev;

	// ---- coalescing
	TMap<FString, TArray<TFunction<void(const FFVBoardPage&)>>> BoardWaiters;
	TMap<int64, TArray<TFunction<void(const FFVPublicProfile&)>>> ProfileWaiters;
	TArray<TFunction<void(bool)>> MailWaiters;
	bool bMailInFlight = false;
	TMap<int64, TArray<FResultFn>> ClaimWaiters;
	bool bRestoreInFlight = false;

	// ---- start-up gate & offline mode (update 4b)
	EFVStartupState Startup = EFVStartupState::Idle;
	FFVVersionInfo Version;
	bool bOfflineMode = false;             // chosen by the player: no requests at all until RetryOnline()
	int32 StartupSerial = 0;               // replies of an abandoned check are ignored
	bool bStartupSyncing = false;          // Checking phase: false = version check (ping), true = sync + mail
	double StartupBeganAt = 0.0;
	double StartupDoneAt = -1000.0;
	int32 SkippedBuild = 0;                // «Позже» for this latest_build (not asked again in this session)
	double NextSeasonProbeAt = 0.0;

	// ---- 0.4.1: offline mode only while there is no connection; the failure reason for bug reports
	bool bOfflineProbeBusy = false;        // a quiet ping of the offline mode is in flight
	double NextOfflineProbeAt = 0.0;
	bool bAutoReturn = false;              // left offline mode by itself: tell the player when the check is over
	FString LastNetError;                  // "ping: Other, HTTP 0, 0.3 с" — empty after a reply from the server

	TArray<TFunction<void()>> Deferred;    // callbacks run on the next Tick (never synchronously inside a call)

	// ------------------------------------------------------------------------------------ basics
	UFVGame* GetGame() const { return Game.Get(); }
	UFVSaveGame* Prof() const
	{
		const UFVGame* G = Game.Get();
		return G ? G->GetProfile() : nullptr;
	}
	bool InGame() const
	{
		const UFVGame* G = Game.Get();
		return G && G->IsInGame() && G->GetProfile();
	}
	bool Registered() const
	{
		const UFVSaveGame* P = Prof();
		return P && P->OnlineId > 0 && !P->OnlineSecret.IsEmpty();
	}
	int64 Now() const { return FVOnlineImpl::LocalUnixNow() + TimeOffset; }

	void Defer(TFunction<void()> Fn)
	{
		if (Fn)
		{
			Deferred.Add(MoveTemp(Fn));
		}
	}

	void RunDeferred()
	{
		if (Deferred.Num() == 0)
		{
			return;
		}
		TArray<TFunction<void()>> Run = MoveTemp(Deferred);
		Deferred.Reset();
		for (TFunction<void()>& Fn : Run)
		{
			Fn();
		}
	}

	void DeferFail(const FString& Code, FReplyFn Done)
	{
		if (!Done)
		{
			return;
		}
		Defer([Code, Done = MoveTemp(Done)]()
		{
			FReply R;
			R.Error = Code;
			R.Message = FVOnlineImpl::ErrorText(Code, FString());
			Done(R);
		});
	}

	void QueueSync(float Delay)
	{
		const double Due = Clock + double(FMath::Max(0.f, Delay));
		SyncDueAt = bSyncQueued ? FMath::Min(SyncDueAt, Due) : Due;
		bSyncQueued = true;
	}

	/** Writes the save at once (in the world through UFVGame, from the menus directly). */
	void PersistProfile()
	{
		UFVGame* G = Game.Get();
		UFVSaveGame* P = Prof();
		if (!G || !P)
		{
			return;
		}
		if (G->IsInGame())
		{
			G->NotifyProfileChanged();
			G->SaveNow();
		}
		else if (UFVSaveGame::SlotExists())
		{
			P->Save();
		}
	}

	void Toast(const FString& Text, const FLinearColor& Color, EFVIcon Icon)
	{
		if (UFVGame* G = Game.Get())
		{
			G->Toast(Text, Color, Icon);
		}
	}

	// ------------------------------------------------------------------------------------ HTTP
	/** bEvenOffline: the quiet connection probe of the offline mode (TickOfflineProbe) — nothing else leaves the device. */
	void Send(const FString& Action, FVOnlineImpl::FJsonObjPtr Body, bool bSigned, FReplyFn Done, bool bEvenOffline = false)
	{
		using namespace FVOnlineImpl;
		if (!bEnabled || bShutdown)
		{
			DeferFail(TEXT("disabled"), MoveTemp(Done));
			return;
		}
		if (bOfflineMode && !bEvenOffline)
		{
			DeferFail(TEXT("offline"), MoveTemp(Done));   // offline mode: nothing leaves the device
			return;
		}
		if (!Body.IsValid())
		{
			Body = MakeShared<FJsonObject>();
		}
		FString Secret;
		if (bSigned)
		{
			const UFVSaveGame* P = Prof();
			if (!P || P->OnlineId <= 0 || P->OnlineSecret.IsEmpty())
			{
				DeferFail(TEXT("not_registered"), MoveTemp(Done));
				return;
			}
			Body->SetNumberField(TEXT("pid"), double(P->OnlineId));
			Body->SetNumberField(TEXT("ts"), double(Now()));
			Body->SetStringField(TEXT("nonce"), RandomHex(8));
			Secret = P->OnlineSecret;
		}
		if (IsEngineExitRequested() && !FModuleManager::Get().IsModuleLoaded(TEXT("HTTP")))
		{
			DeferFail(TEXT("network"), MoveTemp(Done));
			return;
		}

		const FString Text = ToJson(Body);
		const FTCHARToUTF8 Utf8(*Text);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

		const FString FullUrl = Url + (Url.Contains(TEXT("?")) ? TEXT("&a=") : TEXT("?a=")) + Action;
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(FullUrl);
		Req->SetVerb(TEXT("POST"));
		Req->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Req->SetHeader(TEXT("User-Agent"), FString::Printf(TEXT("KlevayaDolina/%s (%s; build %d)"), *GameVersion(), *PlatformName(), ClientBuildNumber));
		if (bSigned)
		{
			Req->SetHeader(TEXT("X-FV-Sig"), HmacHex(Secret, Bytes));
		}
		Req->SetTimeout(RequestTimeoutSec);
		Req->SetContent(MoveTemp(Bytes));

		const TWeakPtr<FImpl> Weak = AsShared();
		Req->OnProcessRequestComplete().BindLambda(
			[Weak, Done, bSigned, Action](FHttpRequestPtr HttpReq, FHttpResponsePtr Resp, bool bConnected)
			{
				const TSharedPtr<FImpl> Self = Weak.Pin();
				if (!Self.IsValid() || Self->bShutdown)
				{
					return;   // the game shut down: drop silently
				}
				Self->OnHttpDone(Action, HttpReq, Resp, bConnected, bSigned, Done);
			});
		if (!Req->ProcessRequest())
		{
			Req->OnProcessRequestComplete().Unbind();
			LastNetError = Action + TEXT(": запрос не отправлен");
			NoteFailure();
			DeferFail(TEXT("network"), MoveTemp(Done));
		}
	}

	void OnHttpDone(const FString& Action, FHttpRequestPtr HttpReq, FHttpResponsePtr Resp, bool bConnected, bool bSigned, const FReplyFn& Done)
	{
		using namespace FVOnlineImpl;
		FReply R;
		if (bConnected && Resp.IsValid())
		{
			const TArray<uint8>& Content = Resp->GetContent();
			const FUTF8ToTCHAR Conv(reinterpret_cast<const UTF8CHAR*>(Content.GetData()), Content.Num());
			const FString Text = FString::ConstructFromPtrSize(Conv.Get(), Conv.Length());
			const FJsonObjPtr J = ParseJsonObject(Text);
			if (J.IsValid() && J->HasField(TEXT("ok")))
			{
				R.bReached = true;
				R.Json = J;
				R.bOk = BoolOf(J, TEXT("ok"));
				if (!R.bOk)
				{
					R.Error = StrOf(J, TEXT("error"));
					R.Message = ErrorText(R.Error, StrOf(J, TEXT("message")));
				}
			}
			else
			{
				R.Error = TEXT("server");
				R.Message = ErrorText(R.Error, FString());
				UE_LOG(LogFV, Warning, TEXT("FV online: '%s' bad reply (HTTP %d): %s"), *Action, Resp->GetResponseCode(), *Text.Left(200));
				// e.g. a hosting error page: the code and the start of the page tell where it came from
				FString Head = Text.Left(60).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd();
				LastNetError = FString::Printf(TEXT("%s: HTTP %d%s%s"), *Action, Resp->GetResponseCode(), Head.IsEmpty() ? TEXT("") : TEXT(", "), *Head);
			}
		}
		else
		{
			R.Error = TEXT("network");
			R.Message = NetErrorText();
			// Shipping has no log: keep the transport's reason for the offline cards (Other in ~1 s = TLS / certificate,
			// ConnectionError = DNS / connect, TimedOut = the network path)
			const EHttpFailureReason Why = HttpReq.IsValid() ? HttpReq->GetFailureReason() : EHttpFailureReason::None;
			const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
			const float Secs = HttpReq.IsValid() ? HttpReq->GetElapsedTime() : 0.f;
			LastNetError = FString::Printf(TEXT("%s: %s, HTTP %d, %.1f с"), *Action,
				Why < EHttpFailureReason::Count ? LexToString(Why) : TEXT("?"), Code, Secs);
#if PLATFORM_ANDROID
			LastNetError += FString::Printf(TEXT(", Android %s"), *FPlatformMisc::GetOSVersion());
#endif
			UE_LOG(LogFV, Warning, TEXT("FV online: '%s' failed: %s"), *Action, *LastNetError);
		}

		if (R.bReached)
		{
			LastNetError = R.bOk ? FString() : Action + TEXT(": ") + R.Error;
			const double ServerTime = NumOf(R.Json, TEXT("server_time"));
			if (ServerTime > 1.0e9)
			{
				TimeOffset = int64(ServerTime) - LocalUnixNow();
				bTimeKnown = true;
			}
			NoteReached(R, bSigned);
			if (!R.bOk)
			{
				UE_LOG(LogFV, Log, TEXT("FV online: '%s' -> %s"), *Action, *R.Error);
			}
		}
		else
		{
			NoteFailure();
		}
		if (Done)
		{
			Done(R);
		}
	}

	void NoteFailure()
	{
		++FailCount;
		const float Base = FMath::Min(FVOnlineImpl::MaxBackoffSec, 5.f * float(1 << FMath::Clamp(FailCount - 1, 0, 8)));
		NextBgAt = FMath::Max(NextBgAt, Clock + double(Base * FMath::FRandRange(0.8f, 1.2f)));
		if (Status != EFVOnlineStatus::Banned)
		{
			Status = EFVOnlineStatus::Offline;
		}
	}

	void NoteReached(const FReply& R, bool bSigned)
	{
		FailCount = 0;
		NextBgAt = (R.Error == TEXT("rate_limited")) ? Clock + 120.0 : 0.0;
		if (R.Error == TEXT("banned"))
		{
			Status = EFVOnlineStatus::Banned;
			FString Reason = FVOnlineImpl::StrOf(R.Json, TEXT("reason"));
			if (Reason.IsEmpty())
			{
				Reason = FVOnlineImpl::StrOf(R.Json, TEXT("message"));
				const FString Prefix = TEXT("Аккаунт заблокирован");
				if (Reason.StartsWith(Prefix))
				{
					Reason.RightChopInline(Prefix.Len());
					while (Reason.Len() > 0 && (Reason[0] == TEXT(':') || Reason[0] == TEXT('.') || FChar::IsWhitespace(Reason[0])))
					{
						Reason.RightChopInline(1);
					}
				}
			}
			BanReason = Reason.TrimStartAndEnd();
			return;
		}
		if (bSigned && R.bOk)
		{
			Status = EFVOnlineStatus::Online;
			BanReason.Empty();
		}
		else if (Status != EFVOnlineStatus::Banned)
		{
			Status = EFVOnlineStatus::Online;
		}
	}

	/** Common reactions to errors of signed requests. */
	void HandleSignedError(const FReply& R)
	{
		if (!R.bReached)
		{
			return;
		}
		if (R.Error == TEXT("bad_time"))
		{
			bTimeKnown = false;
			bPingQueued = true;
			QueueSync(1.f);
		}
		else if (R.Error == TEXT("no_player") || R.Error == TEXT("unknown_player"))
		{
			// The server no longer knows this player (database reset): register again with the same secret.
			if (UFVSaveGame* P = Prof())
			{
				UE_LOG(LogFV, Warning, TEXT("FV online: server lost player %lld, registering again"), (long long)P->OnlineId);
				P->OnlineId = 0;
				P->OnlineTag = 0;
				PersistProfile();
			}
		}
		else if (R.Error == TEXT("bad_sig"))
		{
			NextBgAt = FMath::Max(NextBgAt, Clock + 300.0);
		}
		else if (R.Error == TEXT("rate_limited") || R.Error == TEXT("banned"))
		{
			// handled by NoteReached (back-off / status)
		}
		else
		{
			NextBgAt = FMath::Max(NextBgAt, Clock + 60.0);
		}
	}

	// ------------------------------------------------------------------------------------ tick
	void Tick(float Dt)
	{
		using namespace FVOnlineImpl;
		Clock += double(FMath::Max(0.f, Dt));
		RunDeferred();
		if (!bEnabled || bShutdown)
		{
			return;
		}
		CheckSession();
		if (bOfflineMode)
		{
			TickOfflineProbe();       // offline mode: only a quiet ping now and then, it ends by itself when the server answers
			return;
		}
		if (Startup == EFVStartupState::Idle)
		{
			BeginStartup(false);      // the main menu normally starts it; never leave the gate idle
		}
		if (Startup == EFVStartupState::Checking)
		{
			if (Clock - StartupBeganAt < StartupTimeoutSec)
			{
				return;               // the check drives its own requests; background work waits
			}
			UE_LOG(LogFV, Warning, TEXT("FV online: start-up check timed out"));
			++StartupSerial;          // late replies of the abandoned check are ignored
			FinishStartup(EFVStartupState::NoConnection);
		}

		UFVSaveGame* P = Prof();
		const bool bIn = InGame();

		// A finished tournament: fetch the next one (and the results letter) instead of counting below zero.
		if (Season.bKnown && Season.EndsUnix > 0 && Now() >= Season.EndsUnix && Clock >= NextSeasonProbeAt)
		{
			NextSeasonProbeAt = Clock + SeasonProbeSpacingSec;
			bPingQueued = true;
			if (bIn && Registered())
			{
				QueueSync(2.f);
			}
		}
		if (bIn && P)
		{
			const float PlayDt = FMath::Clamp(Dt, 0.f, 1.f);
			PlaySinceSync += PlayDt;
			PlaySinceBackup += PlayDt;
			if (PlaySinceSync >= SyncIntervalSec && !bSyncQueued)
			{
				QueueSync(0.f);
			}
			if (PlaySinceBackup >= BackupIntervalSec)
			{
				bBackupQueued = true;
				PlaySinceBackup = 0.f;
			}
		}

		const bool bNeedRegister = NeedsRegistration();
		const bool bHasWork = bPingQueued || bNeedRegister || (bIn && Registered() && (bNamePending || bSyncQueued || bBackupQueued));
		if (Status == EFVOnlineStatus::Offline && !bHasWork && !bBgBusy)
		{
			bPingQueued = true;       // probe the server so the status recovers
		}
		if (bBgBusy || Clock < NextBgAt)
		{
			return;
		}
		if (bNeedRegister && NameWaiters.Num() > 0)
		{
			StartRegister();
			return;
		}
		if (bPingQueued)
		{
			StartPing();
			return;
		}
		if (bNeedRegister)
		{
			StartRegister();
			return;
		}
		if (!bIn || !Registered())
		{
			return;
		}
		if (bNamePending && !bNameInFlight)
		{
			SendSetName(true, nullptr);
			return;
		}
		if (bSyncQueued && Clock >= SyncDueAt && Clock - LastSyncAt >= MinSyncSpacingSec)
		{
			StartSync(true);
			return;
		}
		if (bBackupQueued && Clock - LastUploadAt >= MinBackupSpacingSec)
		{
			StartUpload(true);
		}
	}

	/** 0.4.1: the offline mode is for «no connection» only (Docs/UPDATE4_ONLINE.md §10) — the first answer of the server
	    brings the game back online by itself, without stopping play. A banned player's offline mode stays as he chose it. */
	void TickOfflineProbe()
	{
		if (bOfflineProbeBusy || Status == EFVOnlineStatus::Banned || Clock < NextOfflineProbeAt)
		{
			return;
		}
		bOfflineProbeBusy = true;
		Send(TEXT("ping"), PingBody(), false, [this](const FReply& R)
		{
			bOfflineProbeBusy = false;
			NextOfflineProbeAt = Clock + FVOnlineImpl::OfflineProbeSpacingSec;
			if (R.bOk && bOfflineMode && Status != EFVOnlineStatus::Banned)
			{
				UE_LOG(LogFV, Log, TEXT("FV online: the server answers again, leaving offline mode"));
				bAutoReturn = true;
				LeaveOfflineMode();
			}
		}, true);
	}

	/** Online again: the start-up check (version, sync, mail) runs in the background; the gate is only in the menu. */
	void LeaveOfflineMode()
	{
		bOfflineMode = false;
		FailCount = 0;
		NextBgAt = 0.0;
		if (Status != EFVOnlineStatus::Banned)
		{
			Status = EFVOnlineStatus::Connecting;
		}
		UE_LOG(LogFV, Log, TEXT("FV online: leaving offline mode"));
		BeginStartup(true);
	}

	bool NeedsRegistration() const
	{
		if (!InGame() || bRegistering || bOfflineMode || Status == EFVOnlineStatus::Banned)
		{
			return false;
		}
		const UFVSaveGame* P = Prof();
		if (!P || P->OnlineId > 0)
		{
			return false;
		}
		const FString Name = P->PlayerName.TrimStartAndEnd();
		return !Name.IsEmpty() && Name != RegRejectedName;
	}

	/** Profile swaps (continue / new game / restore), entering the world, sleeping. */
	void CheckSession()
	{
		using namespace FVOnlineImpl;
		UFVSaveGame* P = Prof();
		const bool bIn = InGame();
		if (P != LastProfile.Get())
		{
			const int64 NewId = P ? P->OnlineId : 0;
			if (NewId != LastProfileId)
			{
				Mail.Reset();
				bMailFetched = false;
				ServerUnread = 0;
				Season.MyRanks.Reset();
				Season.MyTiers.Reset();
				Season.MyBestPercent = 0.f;
				Season.MyTier = 0;
				bNamePending = false;
				RegRejectedName.Empty();
			}
			LastProfile = P;
			LastProfileId = NewId;
			LastUploadCrc = 0;
			PlaySinceSync = 0.f;
			PlaySinceBackup = 0.f;
			LastAbsMinutes = -1.0;
			if (bIn)
			{
				QueueSync(StartDelaySec);
			}
		}
		if (bIn && !bWasInGame)
		{
			QueueSync(StartDelaySec);
			LastAbsMinutes = -1.0;
		}
		bWasInGame = bIn;

		if (bIn && P)
		{
			const double Abs = double(FMath::Max(0, P->Day - 1)) * 1440.0 + double(P->TimeMinutes);
			if (LastAbsMinutes >= 0.0 && Abs - LastAbsMinutes >= double(SleepJumpMinutes))
			{
				bBackupQueued = true;     // slept (or skipped time): back the save up if it changed
			}
			LastAbsMinutes = Abs;
		}
	}

	// ------------------------------------------------------------------------------------ background requests
	static FVOnlineImpl::FJsonObjPtr PingBody()
	{
		const FVOnlineImpl::FJsonObjPtr Body = MakeShared<FJsonObject>();
		Body->SetNumberField(TEXT("build"), double(FVOnlineImpl::ClientBuildNumber));
		Body->SetStringField(TEXT("ver"), FVOnlineImpl::GameVersion());
		Body->SetStringField(TEXT("platform"), FVOnlineImpl::PlatformName());
		return Body;
	}

	void StartPing()
	{
		bPingQueued = false;
		bBgBusy = true;
		Send(TEXT("ping"), PingBody(), false, [this](const FReply& R)
		{
			bBgBusy = false;
			if (R.bReached)
			{
				ApplySeason(FVOnlineImpl::ObjOf(R.Json, TEXT("season")));
				FVOnlineImpl::ParseVersion(R.Json, Version);
				if (R.bOk && Startup == EFVStartupState::NoConnection && !bOfflineMode)
				{
					BeginStartup(true);   // the connection is back while the menu waits: check again by itself
				}
			}
			else
			{
				bPingQueued = true;       // retried after the back-off
			}
		});
	}

	// ------------------------------------------------------------------------------------ start-up gate (§10)
	/** Ping (version) -> if registered: sync + mail -> Ready / UpdateRequired / UpdateAvailable / NoConnection / Banned. */
	void BeginStartup(bool bForce)
	{
		using namespace FVOnlineImpl;
		if (bShutdown)
		{
			return;
		}
		if (!bEnabled)
		{
			Startup = EFVStartupState::Ready;     // no server (autotest / empty URL): the gate is open
			return;
		}
		if (bOfflineMode || Startup == EFVStartupState::Checking)
		{
			return;                               // offline until RetryOnline() / already checking
		}
		if (!bForce && Startup == EFVStartupState::Ready && Clock - StartupDoneAt < StartupRecheckSec)
		{
			return;                               // just checked: keep the result (menu re-opened)
		}
		const int32 Serial = ++StartupSerial;
		Startup = EFVStartupState::Checking;
		bStartupSyncing = false;
		StartupBeganAt = Clock;
		bPingQueued = false;                      // this ping replaces the background one
		FailCount = 0;
		NextBgAt = 0.0;
		if (Status == EFVOnlineStatus::Offline)
		{
			Status = EFVOnlineStatus::Connecting;
		}
		UE_LOG(LogFV, Log, TEXT("FV online: start-up check (client build %d)"), ClientBuildNumber);
		Send(TEXT("ping"), PingBody(), false, [this, Serial](const FReply& R) { OnStartupPing(Serial, R); });
	}

	bool StartupCurrent(int32 Serial) const
	{
		return !bShutdown && Serial == StartupSerial && Startup == EFVStartupState::Checking;
	}

	void OnStartupPing(int32 Serial, const FReply& R)
	{
		using namespace FVOnlineImpl;
		if (!StartupCurrent(Serial))
		{
			return;
		}
		if (!R.bOk)
		{
			// Unreachable, or the server answered with an error (not configured, overloaded): no usable connection.
			FinishStartup(EFVStartupState::NoConnection);
			return;
		}
		ApplySeason(ObjOf(R.Json, TEXT("season")));
		ParseVersion(R.Json, Version);
		if (Version.bKnown && ClientBuildNumber < Version.MinBuild)
		{
			FinishStartup(EFVStartupState::UpdateRequired);
			return;
		}
		if (!Registered())
		{
			FinishStartup(UpdateOrReady());      // a new fisherman registers later, from the world
			return;
		}
		bStartupSyncing = true;
		StartSync(false, [this, Serial](const FReply& SR) { OnStartupSync(Serial, SR); });
	}

	void OnStartupSync(int32 Serial, const FReply& R)
	{
		if (!StartupCurrent(Serial))
		{
			return;
		}
		if (R.Error == TEXT("banned") || (R.bReached && !R.bOk && Status == EFVOnlineStatus::Banned))
		{
			FinishStartup(EFVStartupState::Banned);
			return;
		}
		if (!R.bReached)
		{
			FinishStartup(EFVStartupState::NoConnection);
			return;
		}
		// The server is there (other errors — a lost player, a rate limit — are handled in the background).
		FetchMail([this, Serial](bool)
		{
			if (StartupCurrent(Serial))
			{
				FinishStartup(UpdateOrReady());
			}
		});
	}

	EFVStartupState UpdateOrReady() const
	{
		const bool bNewer = Version.bKnown && FVOnlineImpl::ClientBuildNumber < Version.LatestBuild && SkippedBuild < Version.LatestBuild;
		return bNewer ? EFVStartupState::UpdateAvailable : EFVStartupState::Ready;
	}

	void FinishStartup(EFVStartupState State)
	{
		Startup = State;
		bStartupSyncing = false;
		StartupDoneAt = Clock;
		if (State == EFVStartupState::NoConnection && Status != EFVOnlineStatus::Banned)
		{
			Status = EFVOnlineStatus::Offline;
		}
		static const TCHAR* const Names[] = { TEXT("Idle"), TEXT("Checking"), TEXT("Ready"), TEXT("UpdateRequired"),
			TEXT("UpdateAvailable"), TEXT("NoConnection"), TEXT("Banned") };
		const int32 Ix = int32(State);
		UE_LOG(LogFV, Log, TEXT("FV online: start-up check -> %s (build %d, latest %d, min %d)"),
			Ix >= 0 && Ix < int32(UE_ARRAY_COUNT(Names)) ? Names[Ix] : TEXT("?"),
			FVOnlineImpl::ClientBuildNumber, Version.LatestBuild, Version.MinBuild);

		if (bAutoReturn)
		{
			// the game left offline mode by itself: one short word about it (NoConnection = quietly tried again later)
			bAutoReturn = false;
			switch (State)
			{
			case EFVStartupState::Ready:
			case EFVStartupState::UpdateAvailable:
				Toast(TEXT("Связь с сервером восстановлена"), FVPal::White(), EFVIcon::Radio);
				break;
			case EFVStartupState::UpdateRequired:
				Toast(TEXT("Связь есть, но для онлайна нужна новая версия игры"), FVPal::White(), EFVIcon::Exclaim);
				break;
			default:
				break;
			}
		}
	}

	void StartRegister()
	{
		using namespace FVOnlineImpl;
		UFVSaveGame* P = Prof();
		if (!P)
		{
			return;
		}
		const FString Name = P->PlayerName.TrimStartAndEnd();
		if (!IsHex64(P->OnlineSecret))
		{
			P->OnlineSecret = RandomHex(64);
		}
		if (P->ProfileStartUnix <= 0)
		{
			P->ProfileStartUnix = Now();
		}
		PersistProfile();         // the secret is stored before the server learns it (a retry reuses it)

		const FJsonObjPtr Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("secret"), P->OnlineSecret);
		Body->SetStringField(TEXT("name"), Name);
		Body->SetStringField(TEXT("platform"), PlatformName());
		Body->SetStringField(TEXT("ver"), GameVersion());
		Body->SetNumberField(TEXT("build"), double(ClientBuildNumber));

		bBgBusy = true;
		bRegistering = true;
		if (Status != EFVOnlineStatus::Banned)
		{
			Status = EFVOnlineStatus::Connecting;
		}
		const TWeakObjectPtr<UFVSaveGame> WP = P;
		const FString Secret = P->OnlineSecret;
		Send(TEXT("register"), Body, false, [this, WP, Secret, Name](const FReply& R)
		{
			bBgBusy = false;
			bRegistering = false;
			UFVSaveGame* Cur = Prof();
			const bool bSame = Cur && Cur == WP.Get() && Cur->OnlineSecret == Secret;
			const FString Prev = RegPrevName;
			RegPrevName.Empty();
			if (R.bOk)
			{
				const int64 Pid = IntOf(R.Json, TEXT("pid"));
				const int32 Tag = Int32Of(R.Json, TEXT("tag"));
				const FString SrvName = StrOf(R.Json, TEXT("name")).TrimStartAndEnd();
				if (Pid > 0 && bSame && Cur->OnlineId <= 0)
				{
					Cur->OnlineId = Pid;
					Cur->OnlineTag = Tag > 0 ? Tag : int32(FMath::Min<int64>(1000 + Pid, MAX_int32));
					if (Cur->PlayerName.TrimStartAndEnd() == Name)
					{
						if (!SrvName.IsEmpty())
						{
							Cur->PlayerName = SrvName;
						}
					}
					else
					{
						bNamePending = true;      // renamed while registering: send the new name next
						NamePrev = SrvName.IsEmpty() ? Name : SrvName;
					}
					LastProfileId = Pid;
					PersistProfile();
					QueueSync(1.f);
					bBackupQueued = true;
					UE_LOG(LogFV, Log, TEXT("FV online: registered as pid %lld (#%d)"), (long long)Pid, Cur->OnlineTag);
					Toast(FString::Printf(TEXT("Профиль рыбака создан: #%d"), Cur->OnlineTag), FVPal::Green(), EFVIcon::Check);
				}
				if (bSame && bNamePending && !bNameInFlight && NameWaiters.Num() > 0 && Registered())
				{
					// Renamed while registering: the callers learn the server's verdict on the NEW name.
					TArray<FResultFn> Waiters = MoveTemp(NameWaiters);
					NameWaiters.Reset();
					SendSetName(false, [Waiters = MoveTemp(Waiters)](bool bOk, const FString& Text)
					{
						for (const FResultFn& Fn : Waiters)
						{
							if (Fn)
							{
								Fn(bOk, Text);
							}
						}
					});
				}
				else
				{
					FinishNameWaiters(true, Cur ? Cur->PlayerName : Name);
				}
			}
			else if (R.bReached && R.Error == TEXT("bad_name"))
			{
				RegRejectedName = Name;
				const bool bHadWaiters = NameWaiters.Num() > 0;
				const FString NowName = bSame ? Cur->PlayerName.TrimStartAndEnd() : FString();
				if (bSame && bHadWaiters && InGame() && !NowName.IsEmpty() && NowName != Name)
				{
					// Renamed while the old name was being registered: register the new one; the callers wait for it.
					RegPrevName = Prev;
					NextBgAt = 0.0;
					return;
				}
				if (bSame && Cur->PlayerName.TrimStartAndEnd() == Name)
				{
					Cur->PlayerName = (Prev != Name) ? Prev : FString();
					PersistProfile();
				}
				if (!bHadWaiters)
				{
					Toast(FString::Printf(TEXT("Имя «%s» не подошло. Выберите другое в профиле."), *Name), FVPal::Red(), EFVIcon::Exclaim);
				}
				FinishNameWaiters(false, R.Message);
			}
			else
			{
				// Offline or a temporary server problem: the name stays saved locally, registration is retried.
				if (R.bReached && R.Error != TEXT("rate_limited"))
				{
					NextBgAt = FMath::Max(NextBgAt, Clock + 60.0);
				}
				FinishNameWaiters(true, Cur ? Cur->PlayerName : Name);
			}
		});
	}

	void FinishNameWaiters(bool bOk, const FString& Text)
	{
		if (NameWaiters.Num() == 0)
		{
			return;
		}
		TArray<FResultFn> Run = MoveTemp(NameWaiters);
		NameWaiters.Reset();
		for (FResultFn& Fn : Run)
		{
			if (Fn)
			{
				Fn(bOk, Text);
			}
		}
	}

	/** set_name with the current local name. bBackground: the retry of a pending rename. */
	void SendSetName(bool bBackground, FResultFn Done)
	{
		using namespace FVOnlineImpl;
		UFVSaveGame* P = Prof();
		if (!P)
		{
			return;
		}
		const FString Name = P->PlayerName;
		const FJsonObjPtr Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("name"), Name);
		if (bBackground)
		{
			bBgBusy = true;
		}
		bNameInFlight = true;
		const TWeakObjectPtr<UFVSaveGame> WP = P;
		Send(TEXT("set_name"), Body, true, [this, bBackground, WP, Name, Done](const FReply& R)
		{
			if (bBackground)
			{
				bBgBusy = false;
			}
			bNameInFlight = false;
			UFVSaveGame* Cur = Prof();
			const bool bSame = Cur && Cur == WP.Get() && Cur->PlayerName == Name;
			if (R.bOk)
			{
				const FString SrvName = StrOf(R.Json, TEXT("name")).TrimStartAndEnd();
				if (bSame)
				{
					bNamePending = false;
					if (!SrvName.IsEmpty() && SrvName != Cur->PlayerName)
					{
						Cur->PlayerName = SrvName;
						PersistProfile();
					}
				}
				if (Done)
				{
					Done(true, SrvName.IsEmpty() ? Name : SrvName);
				}
			}
			else if (R.bReached && R.Error == TEXT("bad_name"))
			{
				if (bSame)
				{
					bNamePending = false;
					if (!NamePrev.IsEmpty() && NamePrev != Name)
					{
						Cur->PlayerName = NamePrev;
						PersistProfile();
					}
				}
				if (Done)
				{
					Done(false, R.Message);
				}
				else
				{
					Toast(FString::Printf(TEXT("Имя «%s» не принято: %s"), *Name, *R.Message), FVPal::Red(), EFVIcon::Exclaim);
				}
			}
			else
			{
				HandleSignedError(R);
				if (Done)
				{
					Done(true, Name);      // kept locally, sent again later
				}
			}
		});
	}

	FVOnlineImpl::FJsonObjPtr BuildSyncBody(const UFVSaveGame& P) const
	{
		using namespace FVOnlineImpl;
		const FFVPublicProfile L = ProfileFromSave(P);

		const FJsonObjPtr Stats = MakeShared<FJsonObject>();
		Stats->SetNumberField(TEXT("level"), double(L.Level));
		Stats->SetNumberField(TEXT("xp"), double(L.Xp));
		Stats->SetNumberField(TEXT("fish_total"), double(L.FishTotal));
		Stats->SetNumberField(TEXT("legendary"), double(L.Legendary));
		Stats->SetNumberField(TEXT("species"), double(L.Species));
		Stats->SetNumberField(TEXT("coins_earned"), double(L.CoinsEarned));
		Stats->SetNumberField(TEXT("achievements"), double(L.Achievements));
		Stats->SetNumberField(TEXT("perfect"), double(L.Perfect));
		Stats->SetNumberField(TEXT("playtime"), double(L.PlaySeconds));
		Stats->SetNumberField(TEXT("stations_built"), double(L.StationsBuilt));
		// update 6 (Docs/UPDATE6_FOREST_CITY.md §9): the server checks the level on this scale, the tournament «Опыт»
		// counts xp_earned; xp_migration lets the server accept the one-off trim of XP above 10 000 without a «suspicious» mark
		Stats->SetNumberField(TEXT("max_level"), double(FV::MaxLevel));
		Stats->SetNumberField(TEXT("xp_earned"), double(L.XpEarned));
		Stats->SetNumberField(TEXT("xp_seniority"), double(L.XpSeniority));
		if (L.XpSeniority > 0)
		{
			Stats->SetNumberField(TEXT("xp_migration"), 1.0);
		}

		const FJsonObjPtr Profile = MakeShared<FJsonObject>();
		Profile->SetStringField(TEXT("status"), L.Status);
		Profile->SetStringField(TEXT("favorite_location"), NameStr(L.FavoriteLocation));
		Profile->SetField(TEXT("best_catch"), FishValue(L.BestCatch));
		FJsonArr Show;
		for (const FFVFishShow& F : L.Showcase)
		{
			Show.Add(FishValue(F));
		}
		Profile->SetArrayField(TEXT("showcase"), Show);
		const FJsonObjPtr Look = MakeShared<FJsonObject>();
		Look->SetStringField(TEXT("hat"), NameStr(L.Hat));
		Look->SetStringField(TEXT("outfit"), NameStr(L.Outfit));
		Look->SetStringField(TEXT("bobber"), NameStr(L.Bobber));
		Look->SetStringField(TEXT("pet"), NameStr(L.Pet));
		Profile->SetObjectField(TEXT("appearance"), Look);
		Profile->SetNumberField(TEXT("start_date"), double(L.StartUnix));

		const FJsonObjPtr Body = MakeShared<FJsonObject>();
		Body->SetObjectField(TEXT("stats"), Stats);
		Body->SetObjectField(TEXT("profile"), Profile);
		Body->SetField(TEXT("season_best"), FishValue(FishFromTrophy(P.PendingSeasonBest)));
		return Body;
	}

	/** Then (optional) runs after the reply was applied (the start-up gate waits for it). */
	void StartSync(bool bBackground, FReplyFn Then = nullptr)
	{
		using namespace FVOnlineImpl;
		UFVSaveGame* P = Prof();
		if (!P || !Registered())
		{
			DeferFail(TEXT("not_registered"), MoveTemp(Then));
			return;
		}
		const FJsonObjPtr Body = BuildSyncBody(*P);
		const FFVTrophy SentBest = P->PendingSeasonBest;
		if (bBackground)
		{
			bBgBusy = true;
		}
		bSyncQueued = false;
		LastSyncAt = Clock;
		PlaySinceSync = 0.f;
		const TWeakObjectPtr<UFVSaveGame> WP = P;
		Send(TEXT("sync"), Body, true, [this, bBackground, SentBest, WP, Then](const FReply& R)
		{
			if (bBackground)
			{
				bBgBusy = false;
			}
			if (R.bOk)
			{
				UFVSaveGame* Cur = Prof();
				if (Cur && Cur == WP.Get())
				{
					bool bChanged = false;
					if (!SentBest.FishId.IsNone() && Cur->PendingSeasonBest.FishId == SentBest.FishId
						&& FMath::IsNearlyEqual(Cur->PendingSeasonBest.SizeCm, SentBest.SizeCm))
					{
						Cur->PendingSeasonBest = FFVTrophy();   // reported; the next autosave persists the reset
					}
					const int32 Tag = Int32Of(R.Json, TEXT("tag"));
					if (Tag > 0 && Tag != Cur->OnlineTag)
					{
						Cur->OnlineTag = Tag;
						bChanged = true;
					}
					const FString SrvName = StrOf(R.Json, TEXT("name")).TrimStartAndEnd();
					if (!SrvName.IsEmpty() && !bNamePending && !bNameInFlight && SrvName != Cur->PlayerName)
					{
						Cur->PlayerName = SrvName;             // e.g. the admin reset the name
						bChanged = true;
					}
					if (bChanged)
					{
						PersistProfile();
					}
				}
				ServerUnread = FMath::Max(0, Int32Of(R.Json, TEXT("mail_unread")));
				ApplySeason(ObjOf(R.Json, TEXT("season")));
				if (ServerUnread > 0 || !bMailFetched)
				{
					FetchMail(nullptr);
				}
			}
			else if (!R.bReached)
			{
				QueueSync(0.f);            // retried after the back-off
			}
			else
			{
				HandleSignedError(R);
				if (R.Error == TEXT("rate_limited"))
				{
					QueueSync(0.f);
				}
			}
			if (Then)
			{
				Then(R);
			}
		});
	}

	void StartUpload(bool bBackground)
	{
		using namespace FVOnlineImpl;
		UFVSaveGame* P = Prof();
		bBackupQueued = false;
		if (!P || !Registered())
		{
			return;
		}
		TArray<uint8> Bytes;
		if (!UGameplayStatics::SaveGameToMemory(P, Bytes) || Bytes.Num() == 0)
		{
			UE_LOG(LogFV, Warning, TEXT("FV online: could not serialize the save for the backup"));
			return;
		}
		if (Bytes.Num() > MaxSaveBytes)
		{
			UE_LOG(LogFV, Warning, TEXT("FV online: save too large for a backup (%d bytes)"), Bytes.Num());
			return;
		}
		const uint32 Crc = FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
		PlaySinceBackup = 0.f;
		if (Crc == LastUploadCrc)
		{
			return;                        // unchanged since the last backup
		}
		const FJsonObjPtr Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("save_b64"), FBase64::Encode(Bytes));
		Body->SetNumberField(TEXT("save_version"), double(P->Version > 0 ? P->Version : UFVSaveGame::CurrentVersion));
		if (bBackground)
		{
			bBgBusy = true;
		}
		LastUploadAt = Clock;
		Send(TEXT("save_upload"), Body, true, [this, bBackground, Crc](const FReply& R)
		{
			if (bBackground)
			{
				bBgBusy = false;
			}
			if (R.bOk)
			{
				LastUploadCrc = Crc;
			}
			else
			{
				if (!R.bReached || R.Error == TEXT("rate_limited"))
				{
					bBackupQueued = true;
				}
				HandleSignedError(R);
			}
		});
	}

	// ------------------------------------------------------------------------------------ season / mail
	void ApplySeason(const FVOnlineImpl::FJsonObjPtr& S)
	{
		using namespace FVOnlineImpl;
		if (!S.IsValid())
		{
			return;
		}
		const int32 Id = Int32Of(S, TEXT("id"), -1);
		if (Id < 0)
		{
			return;
		}
		const bool bNewSeason = !Season.bKnown || Id != Season.Id;
		if (bNewSeason)
		{
			Season.MyRanks.Reset();
			Season.MyTiers.Reset();
			Season.MyBestPercent = 0.f;
			Season.MyTier = 0;
			Season.Participants = 0;
			Season.StartsUnix = 0;
		}
		Season.bKnown = true;
		Season.Id = Id;
		const int64 Ends = IntOf(S, TEXT("ends"));
		if (Ends > 0)
		{
			Season.EndsUnix = Ends;
		}
		const int64 Starts = IntOf(S, TEXT("starts"));
		if (Starts > 0)
		{
			Season.StartsUnix = Starts;
		}
		else if (Season.StartsUnix <= 0 && Season.EndsUnix > 0)
		{
			Season.StartsUnix = Season.EndsUnix - SeasonSeconds;
		}
		if (S->HasField(TEXT("participants")))
		{
			Season.Participants = FMath::Max(0, Int32Of(S, TEXT("participants")));
		}
		if (S->HasField(TEXT("my")))
		{
			Season.MyRanks.Reset();
			Season.MyTiers.Reset();
			Season.MyBestPercent = 0.f;
			Season.MyTier = 0;
			if (const FJsonObjPtr My = ObjOf(S, TEXT("my")))
			{
				// Rewards are per category (update 4b): rank_* = places, tier_* = projected tier (0 none, 1 gold, 2 silver, 3 bronze).
				const struct { EFVBoardCategory Cat; const TCHAR* RankKey; const TCHAR* TierKey; } Map[] = {
					{ EFVBoardCategory::SeasonFish, TEXT("rank_fish"), TEXT("tier_fish") },
					{ EFVBoardCategory::SeasonEarned, TEXT("rank_earned"), TEXT("tier_earned") },
					{ EFVBoardCategory::SeasonXp, TEXT("rank_xp"), TEXT("tier_xp") },
					{ EFVBoardCategory::SeasonBiggest, TEXT("rank_biggest"), TEXT("tier_biggest") } };
				bool bPerCategory = false;
				int32 BestTier = 0;
				for (const auto& M : Map)
				{
					const int32 Rank = Int32Of(My, M.RankKey);
					if (Rank > 0)
					{
						Season.MyRanks.Add(M.Cat, Rank);
					}
					if (My->HasField(M.TierKey))
					{
						bPerCategory = true;
						const int32 Tier = ParseTier(My->TryGetField(M.TierKey));
						Season.MyTiers.Add(M.Cat, Tier);
						if (Tier > 0 && (BestTier == 0 || Tier < BestTier))
						{
							BestTier = Tier;
						}
					}
				}
				if (bPerCategory)
				{
					for (const auto& M : Map)
					{
						Season.MyTiers.FindOrAdd(M.Cat, 0);   // every tournament category present (0 = no reward yet)
					}
				}
				double Pct = NumOf(My, TEXT("best_pct"));
				if (Pct > 1.0)
				{
					Pct /= 100.0;          // sent as a percentage
				}
				Season.MyBestPercent = float(FMath::Clamp(Pct, 0.0, 1.0));
				// MyTier (deprecated) = the best tier over the categories; an older server sends only "tier".
				Season.MyTier = bPerCategory ? BestTier : ParseTier(My->TryGetField(TEXT("tier")));
			}
		}
	}

	void ApplyMailList(const FVOnlineImpl::FJsonObjPtr& J)
	{
		using namespace FVOnlineImpl;
		const UFVSaveGame* P = Prof();
		TArray<FFVMail> NewMail;
		TArray<int64> ReadNotSent;             // read here while the server did not hear about it (offline)
		if (const FJsonArr* Arr = ArrOf(J, TEXT("mail")))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const FJsonObjPtr O = AsObj(V);
				if (!O.IsValid())
				{
					continue;
				}
				FFVMail M;
				M.Id = IntOf(O, TEXT("id"));
				if (M.Id <= 0)
				{
					continue;
				}
				M.Kind = StrOf(O, TEXT("kind"));
				M.Title = StrOf(O, TEXT("title"));
				M.Body = StrOf(O, TEXT("body"));
				M.Coins = FMath::Max(0, Int32Of(O, TEXT("coins")));
				M.Xp = FMath::Max(0, Int32Of(O, TEXT("xp")));
				M.SetLevel = FMath::Clamp(Int32Of(O, TEXT("set_level")), 0, FV::ExpertMaxLevel);
				if (const FJsonArr* Items = ArrOf(O, TEXT("items")))
				{
					for (const TSharedPtr<FJsonValue>& IV : *Items)
					{
						const FJsonObjPtr IO = AsObj(IV);
						FFVMailItem It;
						It.ItemId = NameOf(IO, TEXT("id"));
						It.Count = FMath::Clamp(Int32Of(IO, TEXT("n"), 1), 0, 9999);
						if (!It.ItemId.IsNone() && It.Count > 0)
						{
							M.Items.Add(It);
						}
					}
				}
				M.CreatedUnix = IntOf(O, TEXT("created"));
				const bool bServerClaimed = NumOf(O, TEXT("claimed")) > 0.0;
				const bool bLocalClaimed = P && P->ClaimedMail.Contains(M.Id);
				M.bClaimed = bServerClaimed || bLocalClaimed;
				if (bLocalClaimed && !bServerClaimed && !M.HasAttachments() && ReadNotSent.Num() < 5)
				{
					ReadNotSent.Add(M.Id);
				}
				NewMail.Add(MoveTemp(M));
			}
		}
		NewMail.Sort([](const FFVMail& A, const FFVMail& B)
		{
			return A.CreatedUnix != B.CreatedUnix ? A.CreatedUnix > B.CreatedUnix : A.Id > B.Id;
		});
		Mail = MoveTemp(NewMail);
		bMailFetched = true;
		int32 Unread = 0;
		for (const FFVMail& M : Mail)
		{
			Unread += M.bClaimed ? 0 : 1;
		}
		ServerUnread = Unread;
		for (const int64 Id : ReadNotSent)
		{
			SendMailRead(Id);
		}
	}

	/** Tells the server a letter without attachments was read (its unread counter); the result does not matter. */
	void SendMailRead(int64 MailId)
	{
		if (!bEnabled || bOfflineMode || !Registered())
		{
			return;
		}
		const FVOnlineImpl::FJsonObjPtr Body = MakeShared<FJsonObject>();
		Body->SetNumberField(TEXT("id"), double(MailId));
		Send(TEXT("mail_claim"), Body, true, nullptr);
	}

	void FetchMail(TFunction<void(bool)> Done)
	{
		if (Done)
		{
			MailWaiters.Add(MoveTemp(Done));
		}
		if (bMailInFlight)
		{
			return;
		}
		if (!bEnabled || bOfflineMode || !Registered())
		{
			Defer([this]() { FinishMailWaiters(false); });
			return;
		}
		bMailInFlight = true;
		Send(TEXT("mail"), nullptr, true, [this](const FReply& R)
		{
			bMailInFlight = false;
			if (R.bOk)
			{
				ApplyMailList(R.Json);
			}
			else
			{
				HandleSignedError(R);
			}
			FinishMailWaiters(R.bOk);
		});
	}

	void FinishMailWaiters(bool bOk)
	{
		if (MailWaiters.Num() == 0)
		{
			return;
		}
		TArray<TFunction<void(bool)>> Run = MoveTemp(MailWaiters);
		MailWaiters.Reset();
		for (TFunction<void(bool)>& Fn : Run)
		{
			if (Fn)
			{
				Fn(bOk);
			}
		}
	}

	void MarkClaimedLocal(int64 MailId)
	{
		for (FFVMail& M : Mail)
		{
			if (M.Id == MailId)
			{
				M.bClaimed = true;
			}
		}
	}

	static void RememberClaimed(UFVSaveGame& P, int64 MailId)
	{
		P.ClaimedMail.AddUnique(MailId);
		if (P.ClaimedMail.Num() > FVOnlineImpl::MaxClaimedIds)
		{
			P.ClaimedMail.RemoveAt(0, P.ClaimedMail.Num() - FVOnlineImpl::MaxClaimedIds);
		}
	}

	/** Applies the attachments exactly once. Returns the Russian summary. */
	FString ApplyMailRewards(UFVSaveGame& P, const FFVMail& M)
	{
		using namespace FVOnlineImpl;
		if (P.ClaimedMail.Contains(M.Id))
		{
			return TEXT("Подарок уже получен.");
		}
		const int32 LevelBefore = P.GetLevel();
		TArray<FString> Parts;
		TArray<FString> Lost;
		if (M.Coins > 0)
		{
			P.Earn(M.Coins, false);
			Parts.Add(FString::Printf(TEXT("%s %s"), *Grouped(M.Coins), Plural(M.Coins, TEXT("монета"), TEXT("монеты"), TEXT("монет"))));
		}
		int64 XpCoins = 0;      // update 6 (Р10): reward XP paid out as coins while the level is frozen before the City
		if (M.Xp > 0)
		{
			int64 Coins = 0;
			P.AddRewardXp(M.Xp, Coins);
			Coins = FMath::Clamp<int64>(Coins, 0, M.Xp);
			XpCoins += Coins;
			const int64 XpGiven = int64(M.Xp) - Coins;
			if (XpGiven > 0)
			{
				Parts.Add(FString::Printf(TEXT("%s опыта"), *Grouped(XpGiven)));
			}
		}
		if (M.SetLevel > 0)
		{
			// before the first arrival in the City the level stops at 10 (Р1): a higher gift raises it only that far
			const int32 Cap = P.U6.bVisitedCity ? FV::MaxLevel : FMath::Min(FV::MaxLevel, FV::ClassicMaxLevel);
			const int32 Target = FMath::Clamp(M.SetLevel, 1, Cap);
			const int32 Need = FVDB::XpForLevel(Target) - P.Xp;
			if (Need > 0)
			{
				int64 Coins = 0;
				P.AddRewardXp(Need, Coins);
				XpCoins += FMath::Clamp<int64>(Coins, 0, Need);
			}
			Parts.Add(FString::Printf(TEXT("уровень %d"), Target));
		}
		if (XpCoins > 0)
		{
			Parts.Add(FString::Printf(TEXT("%s %s вместо опыта"), *Grouped(XpCoins), Plural(XpCoins, TEXT("монета"), TEXT("монеты"), TEXT("монет"))));
		}
		for (const FFVMailItem& It : M.Items)
		{
			if (It.ItemId.IsNone() || It.Count <= 0)
			{
				continue;
			}
			bool bGiven = P.GiveItem(It.ItemId, It.Count);
			if (!bGiven && (FVDB::IsJunkId(It.ItemId) || FVDB::IsItemId(It.ItemId)))
			{
				bGiven = P.AddTo(EFVContainer::Chest, It.ItemId, It.Count);
			}
			const FString ItemName = FVDB::ItemName(It.ItemId);
			if (bGiven)
			{
				Parts.Add(FString::Printf(TEXT("%s ×%d"), *ItemName, It.Count));
			}
			else
			{
				Lost.Add(ItemName.IsEmpty() ? It.ItemId.ToString() : ItemName);
				UE_LOG(LogFV, Warning, TEXT("FV online: mail %lld item %s x%d could not be stored"), (long long)M.Id, *It.ItemId.ToString(), It.Count);
			}
		}
		RememberClaimed(P, M.Id);
		PersistProfile();

		const int32 LevelAfter = P.GetLevel();
		if (LevelAfter > LevelBefore)
		{
			Toast(FString::Printf(TEXT("Новый уровень рыбалки: %d!"), LevelAfter), FVPal::Gold(), EFVIcon::Level);
		}
		QueueSync(ImportantDelaySec);

		FString Summary = Parts.Num() > 0 ? (FString(TEXT("Получено: ")) + FString::Join(Parts, TEXT(", ")) + TEXT(".")) : FString(TEXT("Письмо прочитано."));
		if (Lost.Num() > 0)
		{
			Summary += FString(TEXT(" Не хватило места: ")) + FString::Join(Lost, TEXT(", ")) + TEXT(".");
		}
		if (XpCoins > 0)
		{
			Summary += TEXT(" Опыт на максимуме — плывите в Город.");
		}
		return Summary;
	}
};

// ================================================================================================
// UFVOnline
// ================================================================================================
UFVOnline* UFVOnline::Get()
{
	UFVGame* G = UFVGame::Get();
	return G ? G->GetOnline() : nullptr;
}

void UFVOnline::Init(UFVGame* InGame)
{
	if (Impl.IsValid())
	{
		Shutdown();
	}
	Game = InGame;
	Impl = MakeShared<FImpl>();
	Impl->Game = InGame;

	FString Url = ConfiguredServerUrl();
	FString CmdUrl;
	const bool bCmdUrl = FParse::Value(FCommandLine::Get(), TEXT("fvserver="), CmdUrl) && !CmdUrl.TrimStartAndEnd().IsEmpty();
	if (bCmdUrl)
	{
		Url = CmdUrl;
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("fvautotest")))
	{
		Url.Empty();          // the autotest never talks to the real server unless -fvserver=<url> is given
	}
	Url.TrimStartAndEndInline();
	Url.TrimQuotesInline();
	const bool bHttp = Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) || Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	Impl->Url = bHttp ? Url : FString();
	Impl->bEnabled = bHttp;
	Impl->Status = bHttp ? EFVOnlineStatus::Connecting : EFVOnlineStatus::Disabled;
	Impl->bPingQueued = bHttp;
	UE_LOG(LogFV, Log, TEXT("FV online: %s"), bHttp ? *FString::Printf(TEXT("server %s"), *Impl->Url) : TEXT("disabled (no server URL)"));
}

FString UFVOnline::ConfiguredServerUrl()
{
	// The address is built in: 0.4 / 0.4.1 read it only from DefaultGame.ini, and the engine's ini parser cut the unquoted
	// value at "//" (an inline comment for it), so they got "https:" and stayed «Онлайн отключён» without ever sending a
	// request. [FishingValley.Online] ServerUrl (quoted) may still override the address; ServerUrl=off turns the online
	// features off; anything that is not a whole http(s) address is ignored (0.5 autotest: «a normal game uses https://…»).
	FString Url = FVOnlineImpl::DefaultServerUrl;
	FString Cfg;
	if (GConfig && GConfig->GetString(TEXT("FishingValley.Online"), TEXT("ServerUrl"), Cfg, GGameIni))
	{
		Cfg.TrimStartAndEndInline();
		Cfg.TrimQuotesInline();
		const bool bHttp = (Cfg.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) || Cfg.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase)) && Cfg.Len() > 10;
		if (Cfg.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Url.Empty();
		}
		else if (bHttp)
		{
			Url = Cfg;
		}
		else if (!Cfg.IsEmpty())
		{
			UE_LOG(LogFV, Warning, TEXT("FV online: ignoring ServerUrl '%s' from the ini (not an http(s) address; quote it) — using %s"), *Cfg, *Url);
		}
	}
	return Url;
}

void UFVOnline::Shutdown()
{
	if (Impl.IsValid())
	{
		// In-flight requests keep running (a last sync on quit is best effort) but their callbacks become no-ops.
		Impl->bShutdown = true;
		Impl->Deferred.Reset();
		Impl->BoardWaiters.Reset();
		Impl->ProfileWaiters.Reset();
		Impl->MailWaiters.Reset();
		Impl->ClaimWaiters.Reset();
		Impl->NameWaiters.Reset();
		Impl.Reset();
	}
	Game = nullptr;
}

void UFVOnline::Tick(float Dt)
{
	if (Impl.IsValid())
	{
		const TSharedPtr<FImpl> Keep = Impl;   // callbacks may call Shutdown()
		Keep->Tick(Dt);
	}
}

// ---------------------------------------------------------------------------------------- state
EFVOnlineStatus UFVOnline::GetStatus() const
{
	if (!Impl.IsValid())
	{
		return EFVOnlineStatus::Disabled;
	}
	if (Impl->bOfflineMode && Impl->Status != EFVOnlineStatus::Banned)
	{
		return EFVOnlineStatus::Offline;        // a late reply of a request sent before offline mode changes nothing
	}
	return Impl->Status;
}

bool UFVOnline::IsEnabled() const
{
	return Impl.IsValid() && Impl->bEnabled;
}

FString UFVOnline::GetStatusText() const
{
	if (Impl.IsValid() && Impl->bEnabled)
	{
		if (Impl->bOfflineMode)
		{
			return TEXT("Офлайн-режим");
		}
		if (Impl->Startup == EFVStartupState::Checking)
		{
			return Impl->bStartupSyncing ? TEXT("Синхронизация…") : TEXT("Проверка обновлений…");
		}
	}
	switch (GetStatus())
	{
	case EFVOnlineStatus::Disabled:   return TEXT("Онлайн отключён");
	case EFVOnlineStatus::Offline:    return TEXT("Нет связи с сервером");
	case EFVOnlineStatus::Connecting: return TEXT("Подключение...");
	case EFVOnlineStatus::Online:     return TEXT("В сети");
	case EFVOnlineStatus::Banned:
	{
		const FString Reason = GetBanReason();
		return Reason.IsEmpty() ? FString(TEXT("Аккаунт заблокирован")) : FString::Printf(TEXT("Аккаунт заблокирован: %s"), *Reason);
	}
	default:                          return FString();
	}
}

FString UFVOnline::GetBanReason() const
{
	return Impl.IsValid() ? Impl->BanReason : FString();
}

int64 UFVOnline::GetServerNow() const
{
	return FVOnlineImpl::LocalUnixNow() + (Impl.IsValid() ? Impl->TimeOffset : 0);
}

// ---------------------------------------------------------------------------------------- identity
bool UFVOnline::IsRegistered() const
{
	return Impl.IsValid() && Impl->Registered();
}

FString UFVOnline::GetTagText() const
{
	const UFVSaveGame* P = Impl.IsValid() ? Impl->Prof() : nullptr;
	return (P && P->OnlineTag > 0 && P->OnlineId > 0) ? FString::Printf(TEXT("#%d"), P->OnlineTag) : FString();
}

void UFVOnline::SetName(const FString& NewName, TFunction<void(bool, const FString&)> Done)
{
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	UFVSaveGame* P = I.Prof();
	FString Norm, Err;
	if (!P)
	{
		I.Defer([Done]() { if (Done) { Done(false, TEXT("Профиль не загружен.")); } });
		return;
	}
	if (!FVOnlineImpl::NormaliseName(NewName, Norm, Err))
	{
		I.Defer([Done, Err]() { if (Done) { Done(false, Err); } });
		return;
	}
	const FString Prev = P->PlayerName;
	const bool bUnchanged = (Norm == Prev);
	if (!bUnchanged)
	{
		P->PlayerName = Norm;
		I.PersistProfile();
	}
	if (!I.bEnabled)
	{
		I.Defer([Done, Norm]() { if (Done) { Done(true, Norm); } });
		return;
	}
	if (!I.Registered())
	{
		if (I.RegRejectedName == Norm)
		{
			const FString Msg = FVOnlineImpl::ErrorText(TEXT("bad_name"), FString());
			if (!bUnchanged)
			{
				P->PlayerName = Prev;
				I.PersistProfile();
			}
			I.Defer([Done, Msg]() { if (Done) { Done(false, Msg); } });
			return;
		}
		if (!I.InGame() || I.bOfflineMode || I.Status == EFVOnlineStatus::Banned)
		{
			I.Defer([Done, Norm]() { if (Done) { Done(true, Norm); } });   // registers later (from the world, online)
			return;
		}
		if (!I.bRegistering)
		{
			I.RegPrevName = Prev;
		}
		if (Done)
		{
			I.NameWaiters.Add(MoveTemp(Done));
		}
		I.NextBgAt = 0.0;                          // try right now, even during a back-off
		if (!I.bBgBusy && I.NeedsRegistration())
		{
			I.StartRegister();
		}
		return;
	}
	if (bUnchanged && !I.bNamePending)
	{
		I.Defer([Done, Norm]() { if (Done) { Done(true, Norm); } });
		return;
	}
	if (!I.bNamePending)
	{
		I.NamePrev = Prev;
	}
	I.bNamePending = true;
	I.SendSetName(false, MoveTemp(Done));
}

void UFVOnline::SetStatus(const FString& NewStatus, TFunction<void(bool, const FString&)> Done)
{
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	UFVSaveGame* P = I.Prof();
	FString Norm, Err;
	if (!P)
	{
		I.Defer([Done]() { if (Done) { Done(false, TEXT("Профиль не загружен.")); } });
		return;
	}
	if (!FVOnlineImpl::NormaliseStatus(NewStatus, Norm, Err))
	{
		I.Defer([Done, Err]() { if (Done) { Done(false, Err); } });
		return;
	}
	const FString Prev = P->PlayerStatus;
	if (Norm != Prev)
	{
		P->PlayerStatus = Norm;
		I.PersistProfile();
	}
	if (!I.bEnabled || !I.Registered())
	{
		I.Defer([Done, Norm]() { if (Done) { Done(true, Norm); } });   // sent with the next sync (profile.status)
		return;
	}
	const FVOnlineImpl::FJsonObjPtr Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("status"), Norm);
	const TWeakObjectPtr<UFVSaveGame> WP = P;
	FImpl* Self = &I;
	I.Send(TEXT("set_status"), Body, true, [Self, WP, Norm, Prev, Done](const FImpl::FReply& R)
	{
		UFVSaveGame* Cur = Self->Prof();
		const bool bSame = Cur && Cur == WP.Get() && Cur->PlayerStatus == Norm;
		if (R.bOk)
		{
			const FString Srv = FVOnlineImpl::StrOf(R.Json, TEXT("status"));
			if (bSame && R.Json.IsValid() && R.Json->HasField(TEXT("status")) && Srv != Norm)
			{
				Cur->PlayerStatus = Srv;
				Self->PersistProfile();
			}
			if (Done)
			{
				Done(true, bSame ? Cur->PlayerStatus : Norm);
			}
		}
		else if (R.bReached && R.Error == TEXT("bad_status"))
		{
			if (bSame)
			{
				Cur->PlayerStatus = Prev;
				Self->PersistProfile();
			}
			if (Done)
			{
				Done(false, R.Message);
			}
		}
		else
		{
			Self->HandleSignedError(R);
			if (Done)
			{
				Done(true, Norm);                  // kept locally; the next sync carries it
			}
		}
	});
}

void UFVOnline::RequestSync(bool bUploadSave)
{
	if (!Impl.IsValid() || !Impl->bEnabled)
	{
		return;
	}
	Impl->QueueSync(1.f);
	if (bUploadSave)
	{
		Impl->bBackupQueued = true;
	}
}

// ---------------------------------------------------------------------------------------- boards & profiles
void UFVOnline::FetchBoard(EFVBoardCategory Cat, const FString& Query, int32 Offset, int32 Limit, TFunction<void(const FFVBoardPage&)> Done)
{
	using namespace FVOnlineImpl;
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	if (Cat >= EFVBoardCategory::Count)
	{
		Cat = EFVBoardCategory::Level;
	}
	FString Q = Query.TrimStartAndEnd().Left(32);
	Offset = FMath::Max(0, Offset);          // FFVBoardPage::Offset always echoes the requested offset
	Limit = FMath::Clamp(Limit, 1, 50);
	if (!I.bEnabled || I.bOfflineMode)
	{
		const FString Err = I.bOfflineMode ? OfflineText() : FString(TEXT("Онлайн-функции отключены."));
		I.Defer([Done, Cat, Q, Offset, Err]()
		{
			FFVBoardPage Page;
			Page.Category = Cat;
			Page.Query = Q;
			Page.Offset = Offset;
			Page.Error = Err;
			if (Done)
			{
				Done(Page);
			}
		});
		return;
	}
	const FString Key = FString::Printf(TEXT("%d|%d|%d|%s"), int32(Cat), Offset, Limit, *Q.ToLower());
	if (TArray<TFunction<void(const FFVBoardPage&)>>* Waiting = I.BoardWaiters.Find(Key))
	{
		Waiting->Add(MoveTemp(Done));
		return;
	}
	I.BoardWaiters.Add(Key).Add(MoveTemp(Done));

	const FJsonObjPtr Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("cat"), CatKey(Cat));
	Body->SetStringField(TEXT("period"), IsSeasonCategory(Cat) ? TEXT("season") : TEXT("all"));
	Body->SetStringField(TEXT("q"), Q);
	Body->SetNumberField(TEXT("offset"), double(Offset));
	Body->SetNumberField(TEXT("limit"), double(Limit));
	const UFVSaveGame* P = I.Prof();
	const int64 MyId = (P && P->OnlineId > 0) ? P->OnlineId : 0;
	if (MyId > 0)
	{
		Body->SetNumberField(TEXT("pid"), double(MyId));
	}
	FImpl* Self = &I;
	I.Send(TEXT("board"), Body, false, [Self, Key, Cat, Q, Offset, MyId](const FImpl::FReply& R)
	{
		FFVBoardPage Page;
		Page.Category = Cat;
		Page.Query = Q;
		Page.Offset = Offset;
		if (R.bOk)
		{
			Page.bOk = true;
			Page.Total = FMath::Max(0, Int32Of(R.Json, TEXT("total")));
			if (const FJsonArr* Arr = ArrOf(R.Json, TEXT("entries")))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					const FJsonObjPtr O = AsObj(V);
					if (O.IsValid())
					{
						Page.Entries.Add(ParseEntry(O, Cat, MyId));
					}
				}
			}
			if (const FJsonObjPtr Me = ObjOf(R.Json, TEXT("me")))
			{
				Page.Me = ParseEntry(Me, Cat, MyId);
				Page.Me.bMe = true;
				Page.bHasMe = Page.Me.Rank > 0;
			}
			Self->ApplySeason(ObjOf(R.Json, TEXT("season")));
		}
		else
		{
			Page.Error = R.Message;
		}
		TArray<TFunction<void(const FFVBoardPage&)>> Run;
		Self->BoardWaiters.RemoveAndCopyValue(Key, Run);
		for (TFunction<void(const FFVBoardPage&)>& Fn : Run)
		{
			if (Fn)
			{
				Fn(Page);
			}
		}
	});
}

void UFVOnline::FetchProfile(int64 PlayerId, TFunction<void(const FFVPublicProfile&)> Done)
{
	using namespace FVOnlineImpl;
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	if (!I.bEnabled || I.bOfflineMode || PlayerId <= 0)
	{
		const FString Err = !I.bEnabled ? FString(TEXT("Онлайн-функции отключены."))
			: I.bOfflineMode ? OfflineText() : FString(TEXT("Игрок не найден."));
		I.Defer([Done, Err, PlayerId]()
		{
			FFVPublicProfile P;
			P.PlayerId = PlayerId;
			P.Error = Err;
			if (Done)
			{
				Done(P);
			}
		});
		return;
	}
	if (TArray<TFunction<void(const FFVPublicProfile&)>>* Waiting = I.ProfileWaiters.Find(PlayerId))
	{
		Waiting->Add(MoveTemp(Done));
		return;
	}
	I.ProfileWaiters.Add(PlayerId).Add(MoveTemp(Done));

	const FJsonObjPtr Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("id"), double(PlayerId));
	FImpl* Self = &I;
	I.Send(TEXT("profile"), Body, false, [Self, PlayerId](const FImpl::FReply& R)
	{
		FFVPublicProfile Out;
		const FJsonObjPtr PO = ObjOf(R.Json, TEXT("profile"));
		if (R.bOk && PO.IsValid())
		{
			Out = ParseProfile(PO);
			if (Out.PlayerId <= 0)
			{
				Out.PlayerId = PlayerId;
			}
		}
		else
		{
			Out.PlayerId = PlayerId;
			Out.Error = R.bOk ? ErrorText(TEXT("not_found"), FString()) : R.Message;
		}
		TArray<TFunction<void(const FFVPublicProfile&)>> Run;
		Self->ProfileWaiters.RemoveAndCopyValue(PlayerId, Run);
		for (TFunction<void(const FFVPublicProfile&)>& Fn : Run)
		{
			if (Fn)
			{
				Fn(Out);
			}
		}
	});
}

FFVPublicProfile UFVOnline::BuildLocalProfile() const
{
	const UFVSaveGame* P = Impl.IsValid() ? Impl->Prof() : nullptr;
	if (!P)
	{
		if (const UFVGame* G = Game)
		{
			P = G->GetProfile();
		}
	}
	if (!P)
	{
		FFVPublicProfile Empty;
		Empty.bLocal = true;
		Empty.Error = TEXT("Профиль не загружен.");
		return Empty;
	}
	FFVPublicProfile Out = FVOnlineImpl::ProfileFromSave(*P);
	Out.LastSeenUnix = GetServerNow();
	Out.bBanned = GetStatus() == EFVOnlineStatus::Banned;
	return Out;
}

bool UFVOnline::IsSeasonCategory(EFVBoardCategory Cat)
{
	return Cat >= EFVBoardCategory::SeasonFish && Cat < EFVBoardCategory::Count;
}

FString UFVOnline::CategoryName(EFVBoardCategory Cat)
{
	switch (Cat)
	{
	case EFVBoardCategory::Level:         return TEXT("Уровень");
	case EFVBoardCategory::FishTotal:     return TEXT("Всего рыбы");
	case EFVBoardCategory::Biggest:       return TEXT("Самая крупная");
	case EFVBoardCategory::Legendary:     return TEXT("Легендарные");
	case EFVBoardCategory::Species:       return TEXT("Виды рыб");
	case EFVBoardCategory::Earned:        return TEXT("Заработано");
	case EFVBoardCategory::Achievements:  return TEXT("Достижения");
	// Tournament pages sit under the «Турнир (3 дня)» switch, so their names stay short.
	case EFVBoardCategory::SeasonFish:    return TEXT("Улов");
	case EFVBoardCategory::SeasonEarned:  return TEXT("Заработок");
	case EFVBoardCategory::SeasonXp:      return TEXT("Опыт");
	case EFVBoardCategory::SeasonBiggest: return TEXT("Крупнейшая");
	default:                              return FString();
	}
}

FString UFVOnline::FormatBoardValue(EFVBoardCategory Cat, int64 Value)
{
	using namespace FVOnlineImpl;
	Value = FMath::Max<int64>(0, Value);
	switch (Cat)
	{
	case EFVBoardCategory::Level:
		return FString::Printf(TEXT("Ур. %d · %s опыта"), FVDB::LevelForXp(int32(FMath::Min<int64>(Value, MAX_int32))), *Grouped(Value));
	case EFVBoardCategory::FishTotal:
	case EFVBoardCategory::SeasonFish:
		return FString::Printf(TEXT("%s %s"), *Grouped(Value), Plural(Value, TEXT("рыба"), TEXT("рыбы"), TEXT("рыб")));
	case EFVBoardCategory::Biggest:
	case EFVBoardCategory::SeasonBiggest:
		return CmText(Value);
	case EFVBoardCategory::Legendary:
		return FString::Printf(TEXT("%s %s"), *Grouped(Value), Plural(Value, TEXT("легенда"), TEXT("легенды"), TEXT("легенд")));
	case EFVBoardCategory::Species:
	{
		const int32 Total = FMath::Max(1, FVDB::Fish().Num());
		return FString::Printf(TEXT("%lld из %d"), (long long)Value, Total);
	}
	case EFVBoardCategory::Earned:
	case EFVBoardCategory::SeasonEarned:
		return FString::Printf(TEXT("%s %s"), *Grouped(Value), Plural(Value, TEXT("монета"), TEXT("монеты"), TEXT("монет")));
	case EFVBoardCategory::Achievements:
	{
		const int32 Total = FMath::Max(1, FVDB::Achievements().Num());
		return FString::Printf(TEXT("%lld из %d"), (long long)Value, Total);
	}
	case EFVBoardCategory::SeasonXp:
		return FString::Printf(TEXT("%s опыта"), *Grouped(Value));
	default:
		return Grouped(Value);
	}
}

// ---------------------------------------------------------------------------------------- tournament
const FFVSeasonInfo& UFVOnline::GetSeason() const
{
	static const FFVSeasonInfo Unknown;
	return Impl.IsValid() ? Impl->Season : Unknown;
}

void UFVOnline::SeasonReward(int32 Tier, int32 Level, int32& OutCoins, int32& OutXp)
{
	// DEPRECATED: rewards are per category now. Kept meaningful for old callers: the coins of the «Опыт» category and
	// the XP of the «Крупнейшая» category for this tier.
	const int32 L = FMath::Clamp(Level, 1, FV::ExpertMaxLevel);
	OutCoins = FVOnlineImpl::SeasonXpCoins(Tier, L);
	OutXp = FVOnlineImpl::SeasonBiggestXp(Tier, L);
}

FString UFVOnline::SeasonRewardText(EFVBoardCategory SeasonCat, int32 Tier, int32 Level)
{
	using namespace FVOnlineImpl;
	if (Tier < 1 || Tier > 3)
	{
		return FString();
	}
	const int32 L = FMath::Clamp(Level, 1, FV::ExpertMaxLevel);
	switch (SeasonCat)
	{
	case EFVBoardCategory::SeasonFish:           // good baits (+ the expert bait from level 11)
	{
		FString S;
		if (Tier == 1)
		{
			S = FString::Printf(TEXT("%s ×%d, %s ×10"), *ItemTitle(TEXT("bait_magic")), 3 + HalfUp(L), *ItemTitle(TEXT("bait_lure")));
		}
		else if (Tier == 2)
		{
			S = FString::Printf(TEXT("%s ×%d, %s ×10"), *ItemTitle(TEXT("bait_lure")), 4 + HalfUp(L), *ItemTitle(TEXT("bait_nightcrawler")));
		}
		else
		{
			S = FString::Printf(TEXT("%s ×10"), *ItemTitle(TEXT("bait_grub")));
		}
		if (L >= ExpertBaitFromLevel)
		{
			S += FString::Printf(TEXT(", %s ×%d"), *ItemTitle(ExpertBaitId), ExpertBaitCount(Tier));
		}
		return S;
	}
	case EFVBoardCategory::SeasonEarned:         // random materials of the level band (the server picks them)
		if (Tier == 1)
		{
			return EarnedRewardText(3, 4 + L, L);
		}
		if (Tier == 2)
		{
			return EarnedRewardText(2, 2 + HalfUp(L), L);
		}
		return EarnedRewardText(1, 1 + QuarterUp(L), L);
	case EFVBoardCategory::SeasonXp:             // coins
	{
		const int32 Coins = SeasonXpCoins(Tier, L);
		return FString::Printf(TEXT("%s %s"), *Grouped(Coins), Plural(Coins, TEXT("монета"), TEXT("монеты"), TEXT("монет")));
	}
	case EFVBoardCategory::SeasonBiggest:        // XP (Р10: coins 1:1 while the local level is frozen before the City)
	{
		const int32 Xp = SeasonBiggestXp(Tier, L);
		const UFVSaveGame* Me = LocalProfile();
		if (Me && L >= FV::ClassicMaxLevel && Me->IsXpFrozen())
		{
			return FString::Printf(TEXT("%s %s вместо опыта"), *Grouped(Xp), Plural(Xp, TEXT("монета"), TEXT("монеты"), TEXT("монет")));
		}
		return FString::Printf(TEXT("%s опыта"), *Grouped(Xp));
	}
	default:
		return FString();
	}
}

// ---------------------------------------------------------------------------------------- mail
const TArray<FFVMail>& UFVOnline::GetMail() const
{
	static const TArray<FFVMail> None;
	return Impl.IsValid() ? Impl->Mail : None;
}

int32 UFVOnline::UnreadMailCount() const
{
	if (!Impl.IsValid() || !Impl->bEnabled)
	{
		return 0;
	}
	if (!Impl->bMailFetched)
	{
		return Impl->ServerUnread;
	}
	int32 N = 0;
	for (const FFVMail& M : Impl->Mail)
	{
		N += M.bClaimed ? 0 : 1;
	}
	return N;
}

void UFVOnline::FetchMail(TFunction<void(bool)> Done)
{
	if (Impl.IsValid())
	{
		Impl->FetchMail(MoveTemp(Done));
	}
}

void UFVOnline::ClaimMail(int64 MailId, TFunction<void(bool, const FString&)> Done)
{
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	UFVSaveGame* P = I.Prof();
	const FFVMail* Found = I.Mail.FindByPredicate([MailId](const FFVMail& M) { return M.Id == MailId; });
	if (!P || !Found)
	{
		I.Defer([Done]() { if (Done) { Done(false, TEXT("Письмо не найдено.")); } });
		return;
	}
	if (P->ClaimedMail.Contains(MailId))
	{
		I.MarkClaimedLocal(MailId);
		I.Defer([Done]() { if (Done) { Done(true, TEXT("Подарок уже получен.")); } });
		return;
	}
	if (!Found->HasAttachments())
	{
		MarkMailRead(MailId);
		I.Defer([Done]() { if (Done) { Done(true, TEXT("Письмо прочитано.")); } });
		return;
	}
	if (Found->bClaimed)
	{
		I.Defer([Done]() { if (Done) { Done(false, TEXT("Подарок уже получен.")); } });
		return;
	}
	if (I.bOfflineMode)
	{
		I.Defer([Done]() { if (Done) { Done(false, FVOnlineImpl::OfflineText()); } });
		return;
	}
	FString RoomError;
	if (!FVOnlineImpl::HasRoomFor(*P, *Found, RoomError))
	{
		I.Defer([Done, RoomError]() { if (Done) { Done(false, RoomError); } });
		return;
	}
	if (TArray<FImpl::FResultFn>* Waiting = I.ClaimWaiters.Find(MailId))
	{
		Waiting->Add(MoveTemp(Done));
		return;
	}
	I.ClaimWaiters.Add(MailId).Add(MoveTemp(Done));

	const FFVMail Copy = *Found;
	const TWeakObjectPtr<UFVSaveGame> WP = P;
	const FVOnlineImpl::FJsonObjPtr Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("id"), double(MailId));
	FImpl* Self = &I;
	I.Send(TEXT("mail_claim"), Body, true, [Self, Copy, WP](const FImpl::FReply& R)
	{
		bool bOk = false;
		FString Msg;
		UFVSaveGame* Cur = Self->Prof();
		const bool bSame = Cur && Cur == WP.Get();
		if (R.bOk)
		{
			if (bSame)
			{
				Msg = Self->ApplyMailRewards(*Cur, Copy);
				bOk = true;
			}
			else
			{
				Msg = TEXT("Профиль сменился, подарок не применён.");
			}
			Self->MarkClaimedLocal(Copy.Id);
		}
		else if (R.bReached && R.Error == TEXT("already_claimed"))
		{
			if (bSame)
			{
				FImpl::RememberClaimed(*Cur, Copy.Id);
				Self->PersistProfile();
			}
			Self->MarkClaimedLocal(Copy.Id);
			Msg = R.Message;
		}
		else
		{
			Self->HandleSignedError(R);
			Msg = R.Message;
		}
		TArray<FImpl::FResultFn> Run;
		Self->ClaimWaiters.RemoveAndCopyValue(Copy.Id, Run);
		for (FImpl::FResultFn& Fn : Run)
		{
			if (Fn)
			{
				Fn(bOk, Msg);
			}
		}
	});
}

void UFVOnline::MarkMailRead(int64 MailId)
{
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	FFVMail* M = I.Mail.FindByPredicate([MailId](const FFVMail& X) { return X.Id == MailId; });
	if (!M || M->HasAttachments())
	{
		return;                                   // letters with attachments stay unread until «Забрать»
	}
	// UnreadMailCount drops at once (the list, and the server-side counter used before the list arrives).
	const bool bWasUnread = !M->bClaimed;
	M->bClaimed = true;
	if (bWasUnread)
	{
		I.ServerUnread = FMath::Max(0, I.ServerUnread - 1);
	}
	UFVSaveGame* P = I.Prof();
	if (P && !P->ClaimedMail.Contains(MailId))
	{
		// Remembered in the save, so a later mail list (or a restart) keeps it read even if the server did not hear.
		FImpl::RememberClaimed(*P, MailId);
		UFVGame* G = I.GetGame();
		if (G && G->IsInGame())
		{
			G->NotifyProfileChanged();
		}
		else if (UFVSaveGame::SlotExists())
		{
			P->Save();
		}
	}
	if (bWasUnread)
	{
		I.SendMailRead(MailId);                   // the server's unread counter (skipped offline; re-sent on the next list)
	}
}

// ---------------------------------------------------------------------------------------- restore
void UFVOnline::RestoreByCode(const FString& Code, TFunction<void(bool, const FString&)> Done)
{
	using namespace FVOnlineImpl;
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	const FString Norm = NormaliseCode(Code);
	bool bValid = Norm.Len() >= 6 && Norm.Len() <= 12;
	for (const TCHAR C : Norm)
	{
		bValid = bValid && ((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('0') && C <= TEXT('9')));
	}
	if (!I.bEnabled)
	{
		I.Defer([Done]() { if (Done) { Done(false, TEXT("Онлайн-функции отключены.")); } });
		return;
	}
	if (I.bOfflineMode)
	{
		I.Defer([Done]() { if (Done) { Done(false, OfflineText()); } });
		return;
	}
	if (!bValid)
	{
		I.Defer([Done]() { if (Done) { Done(false, TEXT("Код состоит из 8 латинских букв и цифр.")); } });
		return;
	}
	if (I.bRestoreInFlight)
	{
		I.Defer([Done]() { if (Done) { Done(false, TEXT("Восстановление уже идёт, подождите.")); } });
		return;
	}
	I.bRestoreInFlight = true;
	const FJsonObjPtr Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("code"), Norm);
	FImpl* Self = &I;
	I.Send(TEXT("restore"), Body, false, [Self, Done](const FImpl::FReply& R)
	{
		Self->bRestoreInFlight = false;
		auto Finish = [&Done](bool bOk, const FString& Msg)
		{
			if (Done)
			{
				Done(bOk, Msg);
			}
		};
		if (!R.bOk)
		{
			Finish(false, R.Message);
			return;
		}
		TArray<uint8> Bytes;
		const FString B64 = StrOf(R.Json, TEXT("save_b64"));
		if (B64.IsEmpty() || !FBase64::Decode(B64, Bytes) || Bytes.Num() == 0)
		{
			Finish(false, TEXT("Сервер прислал повреждённое сохранение."));
			return;
		}
		UFVSaveGame* Loaded = Cast<UFVSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (!Loaded)
		{
			Finish(false, TEXT("Не удалось прочитать сохранение."));
			return;
		}
		Loaded->FixupAfterLoad();
		const int64 Pid = IntOf(R.Json, TEXT("pid"));
		if (Loaded->OnlineId <= 0 && Pid > 0 && IsHex64(Loaded->OnlineSecret))
		{
			Loaded->OnlineId = Pid;
			Loaded->OnlineTag = int32(FMath::Min<int64>(1000 + Pid, MAX_int32));
		}
		if (Loaded->PlayerName.TrimStartAndEnd().IsEmpty())
		{
			Loaded->PlayerName = StrOf(R.Json, TEXT("name"));
		}
		if (!UGameplayStatics::SaveGameToSlot(Loaded, UFVSaveGame::GetSlotName(), 0))
		{
			Finish(false, TEXT("Не удалось записать сохранение на устройство."));
			return;
		}
		UE_LOG(LogFV, Log, TEXT("FV online: save restored by code (pid %lld)"), (long long)Pid);

		// Fresh session for the restored fisherman.
		Self->Mail.Reset();
		Self->bMailFetched = false;
		Self->ServerUnread = 0;
		Self->bNamePending = false;
		Self->LastUploadCrc = 0;
		Self->QueueSync(FVOnlineImpl::StartDelaySec);

		UFVGame* G = Self->GetGame();
		if (!G || !G->ContinueGame())
		{
			Finish(false, TEXT("Сохранение записано, но загрузить его не удалось. Нажмите «Продолжить» в меню."));
			return;
		}
		G->Toast(TEXT("Сохранение восстановлено!"), FVPal::Green(), EFVIcon::Check);
		Finish(true, TEXT("Сохранение восстановлено!"));
	});
}

// ---------------------------------------------------------------------------------------- events
void UFVOnline::NotifyImportantEvent()
{
	if (Impl.IsValid() && Impl->bEnabled)
	{
		Impl->QueueSync(FVOnlineImpl::ImportantDelaySec);
	}
}

void UFVOnline::NotifyAppPause()
{
	if (!Impl.IsValid() || !Impl->bEnabled || Impl->bOfflineMode || !Impl->Registered())
	{
		return;
	}
	// Best effort: the app may be suspended / killed before these finish.
	Impl->StartSync(false);
	if (Impl->InGame())
	{
		Impl->StartUpload(false);
	}
}

// ---------------------------------------------------------------------------------------- start-up gate & offline mode (§10)
int32 UFVOnline::ClientBuild()
{
	return FVOnlineImpl::ClientBuildNumber;
}

FString UFVOnline::ClientVersionName()
{
	return FVOnlineImpl::ClientVersionText;
}

void UFVOnline::StartupCheck()
{
	if (Impl.IsValid())
	{
		Impl->BeginStartup(false);
	}
}

EFVStartupState UFVOnline::GetStartupState() const
{
	if (!Impl.IsValid() || !Impl->bEnabled)
	{
		return EFVStartupState::Ready;          // no server configured (autotest / empty URL): the gate is open
	}
	return Impl->Startup;
}

const FFVVersionInfo& UFVOnline::GetVersionInfo() const
{
	static const FFVVersionInfo Unknown;
	return Impl.IsValid() ? Impl->Version : Unknown;
}

void UFVOnline::OpenDownloadPage() const
{
	FString Url = Impl.IsValid() ? Impl->Version.DownloadUrl : FString();
	if (!FVOnlineImpl::IsHttpUrl(Url))
	{
		Url = FVOnlineImpl::DefaultDownloadPage;
	}
	FString Error;
	FPlatformProcess::LaunchURL(*Url, nullptr, &Error);
	if (Error.IsEmpty())
	{
		UE_LOG(LogFV, Log, TEXT("FV online: opened the download page %s"), *Url);
	}
	else
	{
		UE_LOG(LogFV, Warning, TEXT("FV online: could not open %s: %s"), *Url, *Error);
	}
}

void UFVOnline::EnterOfflineMode()
{
	if (!Impl.IsValid() || !Impl->bEnabled || Impl->bOfflineMode)
	{
		return;
	}
	FImpl& I = *Impl;
	if (I.Startup != EFVStartupState::NoConnection && I.Startup != EFVStartupState::Banned)
	{
		UE_LOG(LogFV, Warning, TEXT("FV online: offline mode is offered only without a connection or when banned"));
		return;
	}
	I.bOfflineMode = true;
	I.bPingQueued = false;
	I.bAutoReturn = false;
	I.NextOfflineProbeAt = I.Clock + FVOnlineImpl::OfflineFirstProbeSec;
	if (I.Status != EFVOnlineStatus::Banned)
	{
		I.Status = EFVOnlineStatus::Offline;
	}
	UE_LOG(LogFV, Log, TEXT("FV online: offline mode (a quiet ping every %.0f s brings the game back online)"), FVOnlineImpl::OfflineProbeSpacingSec);
}

bool UFVOnline::IsOfflineMode() const
{
	return Impl.IsValid() && Impl->bEnabled && Impl->bOfflineMode;
}

void UFVOnline::RetryOnline()
{
	if (!Impl.IsValid())
	{
		return;
	}
	FImpl& I = *Impl;
	if (!I.bEnabled)
	{
		I.Startup = EFVStartupState::Ready;
		return;
	}
	I.bAutoReturn = false;                 // the player asked: the cards show the result themselves
	I.LeaveOfflineMode();
}

void UFVOnline::NotifyAppResume()
{
	if (!Impl.IsValid() || !Impl->bEnabled)
	{
		return;
	}
	FImpl& I = *Impl;
	if (I.bOfflineMode)
	{
		I.NextOfflineProbeAt = FMath::Min(I.NextOfflineProbeAt, I.Clock);   // back from the background: is the server there?
	}
	else if (I.Status == EFVOnlineStatus::Offline)
	{
		I.NextBgAt = FMath::Min(I.NextBgAt, I.Clock);                       // skip the back-off: ping now
		I.bPingQueued = true;
	}
}

FString UFVOnline::GetLastNetError() const
{
	return Impl.IsValid() ? Impl->LastNetError : FString();
}

void UFVOnline::DebugEnterOfflineMode()
{
	if (!Impl.IsValid() || !Impl->bEnabled || Impl->bOfflineMode)
	{
		return;
	}
	++Impl->StartupSerial;                          // a running check must not overwrite the pretended result
	Impl->Startup = EFVStartupState::NoConnection;
	EnterOfflineMode();
}

FString UFVOnline::GetRetryFailedText() const
{
	const FString Why = GetLastNetError();
	const FString Text = TEXT("Не получилось подключиться. Попробуйте ещё раз чуть позже.");
	return Why.IsEmpty() ? Text : FString::Printf(TEXT("%s\n(%s)"), *Text, *Why);
}

void UFVOnline::SkipOptionalUpdate()
{
	if (!Impl.IsValid() || Impl->Startup != EFVStartupState::UpdateAvailable)
	{
		return;
	}
	Impl->SkippedBuild = FMath::Max(Impl->SkippedBuild, Impl->Version.LatestBuild);
	Impl->Startup = EFVStartupState::Ready;
	Impl->StartupDoneAt = Impl->Clock;
}
