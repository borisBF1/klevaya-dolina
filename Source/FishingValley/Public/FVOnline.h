// Online services (update 4): registration, profile sync, save backups, leaderboards, mail, restore codes.
// Talks to the PHP server on the owner's hosting (Server/Fishing-game/api.php, base address FV_SERVER_BASE_URL in FVOnline.cpp; protocol: Docs/UPDATE4_ONLINE.md §3).
// Implemented in FVOnline.cpp. The game never blocks on the network: every call is asynchronous, failures are silent
// (status Offline) and retried with back-off. Callbacks run on the game thread.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FVTypes.h"
#include "FVOnline.generated.h"

class UFVGame;

enum class EFVOnlineStatus : uint8
{
	Disabled,     // no server URL (autotest without -fvserver)
	Offline,      // not reachable right now (retrying)
	Connecting,   // first contact / registration in progress
	Online,       // last request succeeded
	Banned        // the server blocked this player (reason in GetBanReason)
};

/** Start-up gate (update 4b): the main menu lets the player in only in Ready / offline mode / Disabled. */
enum class EFVStartupState : uint8
{
	Idle,             // StartupCheck not started yet
	Checking,         // ping + version + first sync in progress
	Ready,            // up to date (or the optional update was postponed) and synced
	UpdateRequired,   // build < min_build: play is blocked until the player installs the new version
	UpdateAvailable,  // build < latest_build: «Обновить» / «Позже»
	NoConnection,     // server unreachable: «Повторить» / «Играть офлайн»
	Banned            // blocked by the admin: reason + «Играть офлайн»
};

struct FFVVersionInfo
{
	bool bKnown = false;
	int32 LatestBuild = 0;
	FString LatestName;          // "0.4"
	int32 MinBuild = 0;
	FString DownloadUrl;
	FString Changelog;
};

/** Leaderboard categories. All-time: Level..Achievements; tournament (3 days): SeasonFish..SeasonBiggest. */
enum class EFVBoardCategory : uint8
{
	Level, FishTotal, Biggest, Legendary, Species, Earned, Achievements,
	SeasonFish, SeasonEarned, SeasonXp, SeasonBiggest,
	Count
};

/** A caught fish shown in profiles (best catch, showcase, biggest-fish boards). FishId None = nothing. */
struct FFVFishShow
{
	FName FishId;
	float SizeCm = 0.f;
	EFVQuality Quality = EFVQuality::Normal;
};

struct FFVBoardEntry
{
	int32 Rank = 0;
	int64 PlayerId = 0;
	FString Name;
	int32 Tag = 0;               // shown as "#1042"
	int32 Level = 1;
	int64 Value = 0;             // the category value (for Biggest: size in mm)
	FFVFishShow Fish;            // Biggest / SeasonBiggest: the fish
	bool bMe = false;
};

struct FFVBoardPage
{
	bool bOk = false;
	FString Error;               // Russian, for the UI when !bOk
	EFVBoardCategory Category = EFVBoardCategory::Level;
	FString Query;
	int32 Offset = 0;
	int32 Total = 0;             // total ranked players (after the search filter)
	TArray<FFVBoardEntry> Entries;
	bool bHasMe = false;
	FFVBoardEntry Me;            // the local player's row (if ranked)
};

struct FFVPublicProfile
{
	bool bOk = false;
	FString Error;
	bool bLocal = false;         // built from the local save (no server data needed)
	int64 PlayerId = 0;
	FString Name;
	int32 Tag = 0;
	FString Status;
	int32 Level = 1;
	int32 Xp = 0;
	int32 FishTotal = 0;
	int32 Species = 0;
	int32 Legendary = 0;
	int32 Achievements = 0;
	int64 CoinsEarned = 0;
	int32 Perfect = 0;
	int32 PlaySeconds = 0;
	int32 StationsBuilt = 0;
	FName FavoriteLocation;
	FFVFishShow BestCatch;
	TArray<FFVFishShow> Showcase;                 // up to 3
	FName Hat, Outfit, Bobber, Pet;               // cosmetic ids (render with UFVSprites like the wardrobe does)
	int64 StartUnix = 0;
	int64 LastSeenUnix = 0;
	TMap<EFVBoardCategory, int32> Ranks;          // all-time ranks (server profiles only)
	bool bBanned = false;
	// ---- update 6 (Docs/UPDATE6_FOREST_CITY.md §9; server: profile.xp_earned / profile.seniority, 0 from older servers)
	int64 XpEarned = 0;          // «опыт заработанный»: always grows (the tournament «Опыт» counts it)
	int32 XpSeniority = 0;       // «стаж»: XP above 10 000 trimmed by the update-6 migration (shown in the profile)
};

struct FFVMailItem
{
	FName ItemId;
	int32 Count = 1;
};

struct FFVMail
{
	int64 Id = 0;
	FString Kind;                // "admin" / "season" / "system"
	FString Title;
	FString Body;
	int32 Coins = 0;
	int32 Xp = 0;
	int32 SetLevel = 0;          // raise the fishing level to at least this (0 = none)
	TArray<FFVMailItem> Items;
	int64 CreatedUnix = 0;
	bool bClaimed = false;       // attachments already taken (letters without attachments count as claimed when read)
	bool HasAttachments() const { return Coins > 0 || Xp > 0 || SetLevel > 0 || Items.Num() > 0; }
};

struct FFVSeasonInfo
{
	bool bKnown = false;
	int32 Id = 0;
	int64 StartsUnix = 0;
	int64 EndsUnix = 0;
	int32 Participants = 0;
	TMap<EFVBoardCategory, int32> MyRanks;        // Season* categories (0 = not ranked)
	float MyBestPercent = 0.f;                    // 0..1 (lower = better), 0 when unknown
	int32 MyTier = 0;                             // DEPRECATED: best projected tier over the categories
	TMap<EFVBoardCategory, int32> MyTiers;        // projected tier per Season* category: 0 none, 1 gold, 2 silver, 3 bronze
};

UCLASS()
class UFVOnline : public UObject
{
	GENERATED_BODY()

public:
	static UFVOnline* Get();

	void Init(UFVGame* InGame);
	void Shutdown();
	/** The server address of a normal game: built in (FV_SERVER_BASE_URL + "api.php", FVOnline.cpp), overridable by
	    DefaultGame.ini [FishingValley.Online] ServerUrl (off = no online). Init applies -fvserver / -fvautotest on top. */
	static FString ConfiguredServerUrl();
	/** Timers (sync / backup / retries) and the request queue. */
	void Tick(float Dt);

	// ---- state
	EFVOnlineStatus GetStatus() const;
	bool IsEnabled() const;                       // a server URL is configured
	bool IsOnline() const { return GetStatus() == EFVOnlineStatus::Online; }
	FString GetStatusText() const;                // Russian: "В сети", "Нет связи с сервером", "Аккаунт заблокирован: …"
	FString GetBanReason() const;
	/** Server time now (unix seconds): local clock + the offset measured from the server. */
	int64 GetServerNow() const;

	// ---- identity (the name / tag live in the save: UFVSaveGame::PlayerName / OnlineTag)
	bool IsRegistered() const;
	/** "#1042" (empty until registered). */
	FString GetTagText() const;
	/** Changes the name locally at once and on the server (validated there). Done(bOk, ErrorOrNormalisedName). */
	void SetName(const FString& NewName, TFunction<void(bool, const FString&)> Done);
	void SetStatus(const FString& NewStatus, TFunction<void(bool, const FString&)> Done);
	/** Sync as soon as possible (bUploadSave: also back the save up). Safe to call often (coalesced). */
	void RequestSync(bool bUploadSave = false);

	// ---- leaderboards & profiles (callbacks may arrive after the calling screen closed: capture weak pointers)
	void FetchBoard(EFVBoardCategory Cat, const FString& Query, int32 Offset, int32 Limit, TFunction<void(const FFVBoardPage&)> Done);
	void FetchProfile(int64 PlayerId, TFunction<void(const FFVPublicProfile&)> Done);
	/** The local player's profile from the save (works offline). */
	FFVPublicProfile BuildLocalProfile() const;
	static bool IsSeasonCategory(EFVBoardCategory Cat);
	static FString CategoryName(EFVBoardCategory Cat);     // "Уровень", "Всего рыбы", "Самая крупная", ...
	/** Formats a board value for display: "12 345 монет", "45,2 см", "7 из 40"... */
	static FString FormatBoardValue(EFVBoardCategory Cat, int64 Value);

	// ---- tournament
	const FFVSeasonInfo& GetSeason() const;
	/** DEPRECATED (update 4): overall reward preview. Rewards are per category now — use SeasonRewardText. */
	static void SeasonReward(int32 Tier, int32 Level, int32& OutCoins, int32& OutXp);
	/** Russian description of the reward of a season category for a tier (1 gold, 2 silver, 3 bronze) at Level,
	    e.g. "Волшебная наживка ×7, Блесна ×10" (Docs/UPDATE4_ONLINE.md §4 table). */
	static FString SeasonRewardText(EFVBoardCategory SeasonCat, int32 Tier, int32 Level);

	// ---- mail
	const TArray<FFVMail>& GetMail() const;
	int32 UnreadMailCount() const;                 // unclaimed letters (with attachments or unread)
	void FetchMail(TFunction<void(bool)> Done = nullptr);
	/** Claims attachments on the server, then applies them to the profile exactly once (and saves). */
	void ClaimMail(int64 MailId, TFunction<void(bool, const FString&)> Done);
	/** Marks a letter without attachments as read (local). */
	void MarkMailRead(int64 MailId);

	// ---- restore
	/** Downloads the save for a one-time code, replaces the local save and re-enters the world. */
	void RestoreByCode(const FString& Code, TFunction<void(bool, const FString&)> Done);

	// ---- start-up gate & offline mode (update 4b; Docs/UPDATE4_ONLINE.md §10)
	static int32 ClientBuild();                    // build number compiled into this game (5 = version 0.4.1)
	static FString ClientVersionName();            // "0.4.1"
	/** Ping + version check + (if registered) sync & mail. Progress/result via GetStartupState(). */
	void StartupCheck();
	EFVStartupState GetStartupState() const;
	const FFVVersionInfo& GetVersionInfo() const;
	/** Opens the download page / APK URL in the system browser. */
	void OpenDownloadPage() const;
	/** Allowed only in NoConnection / Banned: online features off until RetryOnline() or the server answers again. */
	void EnterOfflineMode();
	bool IsOfflineMode() const;
	/** Leaves offline mode and runs StartupCheck again. (0.4.1: offline mode also ends by itself — a quiet ping every
	    minute; the first answer runs the check in the background and shows «Связь с сервером восстановлена».) */
	void RetryOnline();
	/** Called by UFVGame when the app comes back to the foreground: offline -> check the connection at once. */
	void NotifyAppResume();
	/** The last network failure for bug reports: "ping: Other, HTTP 0, 0.3 с, Android 14" (empty after a reply). */
	FString GetLastNetError() const;
	/** «Не получилось подключиться…» plus the reason on its own line (the offline cards after a failed attempt). */
	FString GetRetryFailedText() const;
	/** Autotest: offline mode as if the start-up check had failed (the local server is up, so it must end by itself). */
	void DebugEnterOfflineMode();
	/** UpdateAvailable -> Ready (the player chose «Позже»). */
	void SkipOptionalUpdate();

	/** Called by UFVGame on important events (level-up, legendary, achievement, sleep): sync soon. */
	void NotifyImportantEvent();
	/** Called by UFVGame when the app goes to the background / quits: last best-effort sync + backup. */
	void NotifyAppPause();

private:
	UFVGame* Game = nullptr;
	struct FImpl;
	TSharedPtr<FImpl> Impl;
};
