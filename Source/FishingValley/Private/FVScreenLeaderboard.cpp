// «Таблица лидеров» (update 4, Docs/UPDATE4_ONLINE.md §8).
// Period switch «За всё время» (7 categories) / «Турнир (3 дня)» (tournament page + 4 categories). The tournament page
// (update 4b, §4/§11): «Турнир №N» + countdown, and per category the player's place, projected tier (medal colours) and
// the rewards of all three tiers at his level — every category is rewarded separately. Offline mode (§10): the friendly
// «Офлайн-режим — онлайн недоступен» card with «Подключиться» (UFVOnline::RetryOnline) replaces the pages. Categories are
// swipeable pages (FFVPager + tabs), every list scrolls smoothly (FFVScrollView) and loads 50 rows at a time. Medals for
// places 1-3, the local player's row is highlighted and pinned at the bottom while it is not visible. One search field
// (name or #number) replaces the lists with the results. Tapping a row opens that player's profile
// (OpenScreenWith(Profile, "<pid>")); the profile's «Назад» reopens this screen with the context "back", which restores
// the period, pages, search, scroll positions and the loaded lists (kept in a small file-static memory).
// Context: None (from the HUD), "profile" (opened from the own profile: «Назад» returns there), "back" (see above).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "FVTextInput.h"
#include "FVOnline.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformTime.h"

namespace FVLeaderboardScreenImpl
{
	using ECat = EFVBoardCategory;
	constexpr int32 NumCats = int32(EFVBoardCategory::Count);
	constexpr int32 PageSize = 50;
	constexpr double StaleSeconds = 90.0;
	const ECat GAllCats[] = { ECat::Level, ECat::FishTotal, ECat::Biggest, ECat::Legendary, ECat::Species, ECat::Earned, ECat::Achievements };
	const ECat GSeasonCats[] = { ECat::SeasonFish, ECat::SeasonEarned, ECat::SeasonXp, ECat::SeasonBiggest };
	constexpr int32 NumAllPages = UE_ARRAY_COUNT(GAllCats);
	constexpr int32 NumSeasonPages = UE_ARRAY_COUNT(GSeasonCats) + 1;   // [0] = the tournament info page

	constexpr float RowRightInset = 8.f;        // room for the scroll bar right of the rows
	constexpr float LevelColFromRight = 206.f;  // landscape: the level column, measured from the row's right edge
	FLinearColor InkBad() { return FV::Col(170, 44, 34); }
	FLinearColor ListBg() { return FVPal::Parchment(); }

	struct FCatInfo
	{
		const TCHAR* Tab;       // landscape tab label
		const TCHAR* Short;     // portrait icon-tab label
		EFVIcon Icon;
		const TCHAR* Desc;      // what is measured
	};

	FCatInfo InfoOf(ECat C)
	{
		switch (C)
		{
		case ECat::Level:         return { TEXT("Уровень"), TEXT("Уровень"), EFVIcon::Level, TEXT("Больше всего опыта") };
		case ECat::FishTotal:     return { TEXT("Всего рыбы"), TEXT("Улов"), EFVIcon::Keepnet, TEXT("Всего поймано рыбы") };
		case ECat::Biggest:       return { TEXT("Самая крупная"), TEXT("Крупная"), EFVIcon::Fish, TEXT("Самая крупная пойманная рыба") };
		case ECat::Legendary:     return { TEXT("Легендарные"), TEXT("Легенды"), EFVIcon::StarIridium, TEXT("Поймано легендарных рыб") };
		case ECat::Species:       return { TEXT("Виды"), TEXT("Виды"), EFVIcon::Book, TEXT("Открыто видов рыб") };
		case ECat::Earned:        return { TEXT("Заработано"), TEXT("Доход"), EFVIcon::Coin, TEXT("Монет заработано продажей улова") };
		case ECat::Achievements:  return { TEXT("Достижения"), TEXT("Достиж."), EFVIcon::Trophy, TEXT("Получено достижений") };
		case ECat::SeasonFish:    return { TEXT("Улов"), TEXT("Улов"), EFVIcon::Keepnet, TEXT("Рыбы поймано за турнир") };
		case ECat::SeasonEarned:  return { TEXT("Доход"), TEXT("Доход"), EFVIcon::Coin, TEXT("Монет заработано за турнир") };
		case ECat::SeasonXp:      return { TEXT("Опыт"), TEXT("Опыт"), EFVIcon::Xp, TEXT("Опыта заработано за турнир") };   // update 6: «опыт заработанный» (counts on level 10 too)
		case ECat::SeasonBiggest: return { TEXT("Самая крупная"), TEXT("Крупная"), EFVIcon::Fish, TEXT("Самая крупная рыба турнира") };
		default:                  return { TEXT("?"), TEXT("?"), EFVIcon::Question, TEXT("") };
		}
	}

	bool IsBiggest(ECat C) { return C == ECat::Biggest || C == ECat::SeasonBiggest; }

	FLinearColor MedalColor(int32 Rank)
	{
		return Rank == 1 ? FV::Col(255, 204, 51) : (Rank == 2 ? FV::Col(200, 208, 222) : FV::Col(214, 134, 62));
	}

	FLinearColor TierColor(int32 Tier)
	{
		return Tier == 1 ? FV::Col(255, 204, 51) : (Tier == 2 ? FV::Col(200, 208, 222) : (Tier == 3 ? FV::Col(214, 134, 62) : FV::Col(200, 180, 150)));
	}

	FString TierName(int32 Tier)
	{
		return Tier == 1 ? FString(TEXT("Золото")) : (Tier == 2 ? FString(TEXT("Серебро")) : (Tier == 3 ? FString(TEXT("Бронза")) : FString(TEXT("Без награды"))));
	}

	FString TierTop(int32 Tier)
	{
		return Tier == 1 ? FString(TEXT("топ 25%")) : (Tier == 2 ? FString(TEXT("топ 50%")) : FString(TEXT("топ 75%")));
	}

	FString Countdown(int64 S)
	{
		S = FMath::Max<int64>(0, S);
		const int64 Dd = S / 86400, Hh = (S % 86400) / 3600, Mm = (S % 3600) / 60, Ss = S % 60;
		if (Dd > 0) { return FString::Printf(TEXT("%lld д %02lld:%02lld:%02lld"), (long long)Dd, (long long)Hh, (long long)Mm, (long long)Ss); }
		return FString::Printf(TEXT("%02lld:%02lld:%02lld"), (long long)Hh, (long long)Mm, (long long)Ss);
	}

	void IconAt(FVDraw& D, EFVIcon I, const FFVRect& Box, float MaxUp = 2.f)
	{
		if (const FFVSprite* S = FVUI::Icon(I)) { FVUI::SpriteFit(D, *S, Box, FLinearColor::White, MaxUp); }
	}

	void FishAt(FVDraw& D, FName FishId, const FFVRect& Box, float MaxUp)
	{
		UFVSprites* Sp = UFVSprites::Get();
		const FFVFishDef* Def = FVDB::FindFish(FishId);
		if (!Sp || !Def) { return; }
		const FFVSprite S = Sp->Fish(*Def);
		FVUI::SpriteFit(D, S, Box, FLinearColor::White, MaxUp);
	}

	void Spinner(FVDraw& D, const FVector2D& C, float R, double T)
	{
		const int32 N = 8;
		const float Head = float(FMath::Fmod(T * 10.0, double(N)));
		const float S = R >= 10.f ? 3.f : 2.f;
		for (int32 i = 0; i < N; ++i)
		{
			const float Ang = float(i) / float(N) * 2.f * UE_PI - UE_HALF_PI;
			const float Age = FMath::Fmod(Head - float(i) + float(N), float(N));
			const float A = FMath::Clamp(1.f - Age / float(N), 0.15f, 1.f);
			D.Rect(FFVRect(FMath::RoundToFloat(C.X + FMath::Cos(Ang) * R - S * 0.5f), FMath::RoundToFloat(C.Y + FMath::Sin(Ang) * R - S * 0.5f), S, S),
				FV::Alpha(FV::Col(150, 90, 44), A));
		}
	}

	void Card(FVDraw& D, const FFVRect& R, bool bHighlight = false, const FLinearColor* Accent = nullptr)
	{
		D.Rect(R.Offset(0.f, 1.f), FV::Col(120, 70, 36, 80));
		D.Rect(R, Accent ? *Accent : (bHighlight ? FV::Col(200, 146, 40) : FV::Col(186, 124, 66)));
		D.Rect(R.Inset(1.f), bHighlight ? FV::Col(255, 238, 170) : FV::Col(255, 238, 206));
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Alpha(FVPal::White(), 0.55f));
	}

	/** Round medal with a ribbon and the place number (places 1-3). */
	void Medal(FVDraw& D, const FVector2D& C, float R, int32 Rank, float Size)
	{
		const FLinearColor Rib = Rank == 1 ? FV::Col(206, 56, 50) : (Rank == 2 ? FV::Col(64, 128, 214) : FV::Col(84, 160, 60));
		const float RW = FMath::RoundToFloat(R * 0.55f), RH = FMath::RoundToFloat(R * 0.9f);
		D.Rect(FFVRect(FMath::RoundToFloat(C.X - RW - 1.f), FMath::RoundToFloat(C.Y - R - RH * 0.5f), RW, RH), Rib);
		D.Rect(FFVRect(FMath::RoundToFloat(C.X + 1.f), FMath::RoundToFloat(C.Y - R - RH * 0.5f), RW, RH), Rib * 0.8f + FLinearColor(0.f, 0.f, 0.f, 0.2f));
		D.Circle(C + FVector2D(0.f, 1.f), R, FV::Col(60, 30, 10, 90));
		FLinearColor Rim = MedalColor(Rank) * 0.6f;
		Rim.A = 1.f;
		D.Circle(C, R, Rim);
		D.Circle(C, R - 1.f, MedalColor(Rank));
		D.Circle(C + FVector2D(-R * 0.3f, -R * 0.3f), R * 0.3f, FV::Col(255, 255, 255, 110));
		D.Text(FString::FromInt(Rank), FVector2D(C.X, FMath::RoundToFloat(C.Y - D.LineHeight(Size) * 0.5f)), Size, FV::Col(90, 52, 10), EFVAlign::Center, false, true);
	}

	/** Two-arrow "refresh" glyph drawn with pixels. */
	void RefreshGlyph(FVDraw& D, const FVector2D& C, float R, const FLinearColor& Col)
	{
		for (int32 i = 0; i < 20; ++i)
		{
			const float A = (float(i) / 20.f) * 2.f * UE_PI;
			if ((i >= 3 && i <= 4) || (i >= 13 && i <= 14)) { continue; }
			D.Rect(FFVRect(FMath::RoundToFloat(C.X + FMath::Cos(A) * R - 0.5f), FMath::RoundToFloat(C.Y + FMath::Sin(A) * R - 0.5f), 1.5f, 1.5f), Col);
		}
		D.Rect(FFVRect(FMath::RoundToFloat(C.X + R - 2.f), FMath::RoundToFloat(C.Y - 1.f), 4.f, 1.5f), Col);
		D.Rect(FFVRect(FMath::RoundToFloat(C.X - R - 2.f), FMath::RoundToFloat(C.Y), 4.f, 1.5f), Col);
	}

	/** "28.09, 00:00" in the device's local time. */
	FString LocalStamp(int64 Unix)
	{
		if (Unix <= 0) { return FString(); }
		const FDateTime T = FDateTime::FromUnixTimestamp(Unix) + (FDateTime::Now() - FDateTime::UtcNow());
		return FString::Printf(TEXT("%02d.%02d, %02d:%02d"), T.GetDay(), T.GetMonth(), T.GetHour(), T.GetMinute());
	}

	/** Pill with the tier name on the medal colour, right-aligned at RightTop. Returns its width. */
	float TierChip(FVDraw& D, int32 Tier, const FVector2D& RightTop, float Size)
	{
		const FString L = TierName(Tier);
		const float W = FMath::RoundToFloat(D.MeasureText(L, Size, true).X + 10.f);
		const float H = FMath::RoundToFloat(D.LineHeight(Size) + 2.f);
		const FFVRect R(FMath::RoundToFloat(RightTop.X - W), FMath::RoundToFloat(RightTop.Y), W, H);
		FLinearColor Edge = TierColor(Tier) * 0.62f;
		Edge.A = 1.f;
		D.Rect(R, Tier > 0 ? Edge : FV::Col(196, 170, 140));
		D.Rect(R.Inset(1.f), Tier > 0 ? TierColor(Tier) : FV::Col(242, 228, 206));
		if (Tier > 0) { D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 255, 255, 130)); }
		D.Text(L, FVector2D(R.Center().X, FMath::RoundToFloat(R.Y + (H - D.LineHeight(Size)) * 0.5f)), Size, Tier > 0 ? FV::Col(84, 48, 10) : FVPal::InkSoft(), EFVAlign::Center, false, true);
		return W;
	}

	// =================================================================================================================
	// offline mode (update 4b, Docs/UPDATE4_ONLINE.md §10): «Офлайн-режим — онлайн недоступен» + «Подключиться»
	// =================================================================================================================
	enum class EOffline : uint8 { None, NoServer, OfflineMode, NoConnection, Connecting, UpdateRequired };

	/** Why the online features are unavailable right now (None = the server can be used). */
	EOffline OfflineOf(const UFVOnline* On)
	{
		if (!On || !On->IsEnabled()) { return EOffline::NoServer; }
		if (On->IsOfflineMode()) { return EOffline::OfflineMode; }
		const EFVStartupState St = On->GetStartupState();
		if (St == EFVStartupState::Checking) { return EOffline::Connecting; }
		if (St == EFVStartupState::UpdateRequired) { return EOffline::UpdateRequired; }
		switch (On->GetStatus())
		{
		case EFVOnlineStatus::Disabled: return EOffline::NoServer;
		case EFVOnlineStatus::Offline:  return EOffline::NoConnection;
		default:                        return EOffline::None;
		}
	}

	/** Word-wrapped lines (for centred paragraphs). */
	TArray<FString> WrapWords(const FVDraw& D, const FString& Text, float MaxW, float Size, bool bBold)
	{
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		TArray<FString> Lines;
		FString Cur;
		for (const FString& Wd : Words)
		{
			const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
			if (Cur.IsEmpty() || D.MeasureText(Try, Size, bBold).X <= MaxW) { Cur = Try; continue; }
			Lines.Add(Cur);
			Cur = Wd;
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
		return Lines;
	}

	float LineStep(const FVDraw& D, float Size, bool bBold) { return FMath::RoundToFloat(D.LineHeight(Size, bBold) * 1.08f); }

	/** Draws Lines centred on CX; returns the height used. */
	float CenteredLines(FVDraw& D, const TArray<FString>& Lines, float CX, float Y, float Size, const FLinearColor& Col, bool bBold)
	{
		const float Step = LineStep(D, Size, bBold);
		for (int32 i = 0; i < Lines.Num(); ++i) { D.Text(Lines[i], FVector2D(CX, Y + Step * float(i)), Size, Col, EFVAlign::Center, false, bBold); }
		return Step * float(Lines.Num());
	}

	/** Round medallion with signal arcs: crossed out while offline, lighting up one by one while connecting. */
	void SignalBadge(FVDraw& D, const FVector2D& C, float R, EOffline Why, double T)
	{
		D.Circle(C + FVector2D(0.f, 1.5f), R, FV::Col(120, 70, 36, 80));
		D.Circle(C, R, FV::Col(186, 124, 66));
		D.Circle(C, R - 1.5f, FV::Col(255, 248, 232));
		const float K = R / 16.f;
		if (Why == EOffline::UpdateRequired)
		{
			const float S = FMath::RoundToFloat(16.f * K);
			IconAt(D, EFVIcon::Exclaim, FFVRect(FMath::RoundToFloat(C.X - S * 0.5f), FMath::RoundToFloat(C.Y - S * 0.5f), S, S), 2.f);
			return;
		}
		const bool bBusy = Why == EOffline::Connecting;
		const int32 Lit = bBusy ? int32(FMath::Fmod(T * 2.5, 4.0)) : -1;
		const FLinearColor OnC = FV::Col(214, 150, 30);
		const FLinearColor OffC = bBusy ? FV::Col(226, 206, 176) : FV::Col(168, 140, 112);
		const FVector2D O(FMath::RoundToFloat(C.X), FMath::RoundToFloat(C.Y + 5.f * K));
		const float Dot = FMath::Max(2.f, FMath::RoundToFloat(2.5f * K));
		D.Rect(FFVRect(O.X - FMath::RoundToFloat(Dot * 0.5f), O.Y - FMath::RoundToFloat(Dot * 0.5f), Dot, Dot), bBusy ? OnC : OffC);
		const float PS = K >= 1.2f ? 2.f : 1.5f;
		for (int32 a = 1; a <= 3; ++a)
		{
			const float Rad = (1.f + 4.f * float(a)) * K;
			const int32 N = 4 + a * 3;
			const FLinearColor Col = (bBusy && a <= Lit) ? OnC : OffC;
			for (int32 i = 0; i <= N; ++i)
			{
				const float Ang = FMath::DegreesToRadians(-135.f + 90.f * float(i) / float(N));
				D.Rect(FFVRect(FMath::RoundToFloat(O.X + FMath::Cos(Ang) * Rad - PS * 0.5f), FMath::RoundToFloat(O.Y + FMath::Sin(Ang) * Rad - PS * 0.5f), PS, PS), Col);
			}
		}
		if (!bBusy)
		{
			const FVector2D A(C.X - 9.f * K, C.Y + 9.f * K), B(C.X + 9.f * K, C.Y - 9.f * K);
			D.Line(A, B, FV::Col(255, 248, 232), 4.f * K);
			D.Line(A, B, FV::Col(206, 64, 50), 2.f * K);
		}
	}

	/** The friendly offline state of an online feature, centred in Area. Feature = what comes back after reconnecting
	    (one sentence). «Подключиться» calls UFVOnline::RetryOnline (bRetried remembers it: a failed attempt shows a
	    hint); «Скачать обновление» opens the download page when the server needs a newer build. */
	void OfflineCard(FVDraw& D, const FFVRect& Area, bool bP, UFVOnline* On, EOffline Why, const FString& Feature, bool& bRetried)
	{
		FString Title = TEXT("Офлайн-режим");
		FString Sub = TEXT("онлайн недоступен");
		FString Body;
		switch (Why)
		{
		case EOffline::Connecting:
			Title = TEXT("Подключаемся…");
			Sub = TEXT("проверяем связь с сервером");
			Body = TEXT("Это займёт всего несколько секунд.");
			break;
		case EOffline::UpdateRequired:
			Title = TEXT("Нужна новая версия");
			Body = TEXT("Для онлайна нужна свежая версия игры. Установите обновление — и всё снова заработает. Рыбачить можно и сейчас.");
			break;
		case EOffline::NoServer:
			Body = TEXT("Эта копия игры запущена без сервера, поэтому онлайн-функции выключены. Рыбачить, строить и копить улов можно и так!");
			break;
		case EOffline::NoConnection:
			Body = FString(TEXT("Сервер сейчас не отвечает — проверьте интернет. ")) + Feature;
			break;
		default:
			Body = FString(TEXT("Вы играете без интернета. ")) + Feature + TEXT(" Рыбалка, дом и весь прогресс работают как обычно.");
			break;
		}
		const bool bButton = On && Why != EOffline::NoServer;
		const bool bFailed = bRetried && (Why == EOffline::NoConnection || Why == EOffline::OfflineMode);
		const float HS = bP ? FV::Ui::Header() : 10.f;
		const float BS = bP ? FV::Ui::Body() - 0.5f : 7.5f;
		const float SS = bP ? FV::Ui::Small() : 6.5f;
		const float W = FMath::Min(Area.W - (bP ? 16.f : 20.f), bP ? 312.f : 300.f);
		const float Pad = bP ? 14.f : 10.f;
		const float TW = W - Pad * 2.f;
		const float BadgeR = bP ? 20.f : 14.f;
		const float BH = bP ? 38.f : 20.f;
		const TArray<FString> BodyL = WrapWords(D, Body, TW, BS, false);
		const TArray<FString> FailL = bFailed ? WrapWords(D, On ? On->GetRetryFailedText() : FString(TEXT("Не получилось подключиться. Попробуйте ещё раз чуть позже.")), TW, SS, true) : TArray<FString>();
		const float H = FMath::CeilToFloat(Pad + BadgeR * 2.f + (bP ? 10.f : 6.f) + D.LineHeight(HS) + D.LineHeight(SS) + (bP ? 8.f : 5.f)
			+ LineStep(D, BS, false) * float(BodyL.Num()) + (bFailed ? 6.f + LineStep(D, SS, true) * float(FailL.Num()) : 0.f)
			+ (bButton ? (bP ? 14.f : 9.f) + BH : 0.f) + Pad);
		const FVector2D AC = Area.Center();
		const FFVRect R(FMath::RoundToFloat(AC.X - W * 0.5f), FMath::RoundToFloat(FMath::Max(Area.Y + 4.f, AC.Y - H * 0.5f)), W, H);
		D.Rect(R.Offset(0.f, 2.f), FV::Col(120, 70, 36, 70));
		D.Rect(R, FV::Col(186, 124, 66));
		D.Rect(R.Inset(1.f), FV::Col(255, 242, 214));
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Alpha(FVPal::White(), 0.6f));
		const float CX = R.Center().X;
		float Y = R.Y + Pad;
		SignalBadge(D, FVector2D(CX, Y + BadgeR), BadgeR, Why, D.Time);
		Y += BadgeR * 2.f + (bP ? 10.f : 6.f);
		D.Text(Title, FVector2D(CX, Y), HS, FVPal::Ink(), EFVAlign::Center, false, true);
		Y += D.LineHeight(HS);
		D.Text(Sub, FVector2D(CX, Y), SS, FV::Col(160, 100, 40), EFVAlign::Center, false, true);
		Y += D.LineHeight(SS) + (bP ? 8.f : 5.f);
		Y += CenteredLines(D, BodyL, CX, Y, BS, FVPal::Ink(), false);
		if (bFailed) { Y += 6.f + CenteredLines(D, FailL, CX, Y + 6.f, SS, InkBad(), true); }
		if (!bButton) { return; }
		Y += bP ? 14.f : 9.f;
		const float BW = FMath::Min(TW, bP ? 210.f : 140.f);
		const FFVRect BR(FMath::RoundToFloat(CX - BW * 0.5f), FMath::RoundToFloat(Y), BW, BH);
		const float TS = bP ? FV::Ui::Body() + 1.f : 8.f;
		if (Why == EOffline::UpdateRequired)
		{
			if (D.Button(BR, TEXT("Скачать обновление"), EFVButton::Primary, true, TS)) { FVUI::Sfx(EFVSfx::Click); On->OpenDownloadPage(); }
			return;
		}
		const bool bBusy = Why == EOffline::Connecting;
		if (D.Button(BR, bBusy ? FString(TEXT("Подключаемся…")) : FString(TEXT("Подключиться")), EFVButton::Primary, !bBusy, TS))
		{
			FVUI::Sfx(EFVSfx::Click);
			bRetried = true;
			On->RetryOnline();
		}
	}

	// =================================================================================================================
	// state
	// =================================================================================================================
	struct FCatState
	{
		FFVBoardPage Page;           // accumulated rows (Page.Entries grows with "load more")
		bool bHasData = false;
		bool bLoading = false;       // first page in flight
		bool bLoadingMore = false;
		bool bError = false;
		FString Error;
		FString DataQuery;           // the query the data / the request in flight belongs to
		double FetchedAt = -1000.0;  // FPlatformTime::Seconds()
		double RetryAt = 0.0;        // no automatic retry before this
		int32 Gen = 0;
	};

	/** What the screen remembers between openings (restored with the context "back"). */
	struct FMemory
	{
		bool bValid = false;
		bool bSeason = false;
		int32 AllPage = 0;
		int32 SeasonPage = 0;
		FString Query;
		bool bFromProfile = false;
		TArray<FCatState> States;
		TArray<FFVScrollView> Scrolls;   // whole scroll states (ScrollTo would clamp before the first Update)
	};

	FMemory& Memory()
	{
		static FMemory M;
		return M;
	}

	enum class EAct : uint8 { None, Close, Back, OpenProfile, Refresh, ClearSearch };

	class FLeaderboardScreen final : public FVScreen
	{
	public:
		explicit FLeaderboardScreen(UFVGame* InGame) : FVScreen(InGame), Alive(MakeShared<bool>(true)) {}
		virtual ~FLeaderboardScreen() override { *Alive = false; }

		virtual bool HandlesEscape() const override { return true; }

		virtual void OnOpen() override
		{
			TickCount = 0;
			*Alive = true;
			States.SetNum(NumCats);
			Scrolls.SetNum(NumCats);
			Search = FVKit::FFVTextField();
			Search.MaxLen = 16;
			const FName Ctx = Game ? Game->GetScreenContext() : NAME_None;
			const FMemory& M = Memory();
			const double Now = FPlatformTime::Seconds();
			bSeason = false;
			bFromProfile = Ctx == FName(TEXT("profile"));
			AllPager = FFVPager();
			SeasonPager = FFVPager();
			AllPager.NumPagesCached = NumAllPages;
			SeasonPager.NumPagesCached = NumSeasonPages;
			if (Ctx == FName(TEXT("back")) && M.bValid)
			{
				bSeason = M.bSeason;
				bFromProfile = M.bFromProfile;
				AllPager.SetPage(FMath::Clamp(M.AllPage, 0, NumAllPages - 1));
				SeasonPager.SetPage(FMath::Clamp(M.SeasonPage, 0, NumSeasonPages - 1));
				ActiveQuery = M.Query;
				Search.Text = M.Query;
				for (int32 i = 0; i < NumCats && i < M.States.Num(); ++i)
				{
					States[i] = M.States[i];
					if (M.Scrolls.IsValidIndex(i))
					{
						Scrolls[i] = M.Scrolls[i];
						Scrolls[i].Pos = Scrolls[i].Target = FMath::Clamp(Scrolls[i].Pos, 0.f, Scrolls[i].MaxOffset());
						Scrolls[i].Vel = 0.f;
						Scrolls[i].bDrag = false;
					}
				}
			}
			else
			{
				ActiveQuery.Reset();
				// reuse fresh unfiltered lists from the previous visit
				for (int32 i = 0; i < NumCats && i < M.States.Num(); ++i)
				{
					const FCatState& Old = M.States[i];
					if (Old.bHasData && Old.DataQuery.IsEmpty() && Now - Old.FetchedAt < StaleSeconds) { States[i] = Old; }
				}
			}
			for (FCatState& S : States)
			{
				S.bLoading = false;
				S.bLoadingMore = false;
				++S.Gen;
				if (S.DataQuery != ActiveQuery) { S = FCatState(); }
			}
			LastSearchText = Search.Text;
			InfoScroll.Reset();
			InfoContentH = 0.f;
			bRetried = false;
			bWasUsable = ServerUsable();
			if (bWasUsable) { Online()->RequestSync(false); }   // fresh tournament places
		}

		virtual void OnClose() override
		{
			*Alive = false;
			FVKit::EndTextField(Search);
			FMemory& M = Memory();
			M.bValid = true;
			M.bSeason = bSeason;
			M.AllPage = AllPager.Page;
			M.SeasonPage = SeasonPager.Page;
			M.Query = ActiveQuery;
			M.bFromProfile = bFromProfile;
			M.States = States;
			M.Scrolls = Scrolls;
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (In.WasPressed(EKeys::Escape))
			{
				if (Search.bFocused) { FVKit::EndTextField(Search); return; }
				PendingAct = EAct::Back;
				return;
			}
			if (FVTextInput::IsActive()) { return; }
			FFVPager& Pager = CurPager();
			const int32 N = bSeason ? NumSeasonPages : NumAllPages;
			if (In.WasPressed(EKeys::Left) || In.WasPressed(EKeys::A)) { Pager.GoTo(FMath::Max(0, Pager.Page - 1)); }
			if (In.WasPressed(EKeys::Right) || In.WasPressed(EKeys::D)) { Pager.GoTo(FMath::Min(N - 1, Pager.Page + 1)); }
			if (In.WasPressed(EKeys::F5)) { PendingAct = EAct::Refresh; }
			if (FFVScrollView* S = CurScroll())
			{
				if (In.WasPressed(EKeys::PageDown)) { S->ScrollTo(S->Target + S->View * 0.85f); }
				if (In.WasPressed(EKeys::PageUp)) { S->ScrollTo(S->Target - S->View * 0.85f); }
				if (In.WasPressed(EKeys::Home)) { S->ScrollTo(0.f); }
				if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { S->ScrollTo(S->Target + 40.f); }
				if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { S->ScrollTo(S->Target - 40.f); }
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bP = FV::IsPortrait();
			Act = PendingAct;
			PendingAct = EAct::None;
			const bool bUsable = ServerUsable();
			if (bUsable)
			{
				bRetried = false;
				if (!bWasUsable)
				{
					// back online (e.g. after «Подключиться»): retry failed pages at once
					for (FCatState& S : States) { S.RetryAt = 0.0; }
				}
			}
			bWasUsable = bUsable;
			UpdateSearch();
			const FFVRect Win = bP ? FV::WindowRect() : FFVRect(10.f, 30.f, 620.f, 318.f);
			if (D.Window(Win, TEXT("Таблица лидеров"))) { Act = EAct::Close; }
			if (bP) { DrawPortrait(D, Win); }
			else { DrawLandscape(D, Win); }
			RunAct();
		}

	private:
		TArray<FCatState> States;
		TArray<FFVScrollView> Scrolls;
		FFVScrollView InfoScroll;
		float InfoContentH = 0.f;
		FFVPager AllPager;
		FFVPager SeasonPager;
		bool bSeason = false;
		bool bFromProfile = false;
		FVKit::FFVTextField Search;
		FString ActiveQuery;
		FString LastSearchText;
		double LastSearchEdit = 0.0;
		TSharedRef<bool> Alive;
		int32 TickCount = 0;
		EAct Act = EAct::None;
		EAct PendingAct = EAct::None;
		int64 ActPid = 0;
		bool bRetried = false;       // «Подключиться» was pressed (a failed attempt shows a hint)
		bool bWasUsable = true;

		// ------------------------------------------------------------------ helpers
		UFVOnline* Online() const { return Game ? Game->GetOnline() : nullptr; }
		/** The server can be used: not the offline mode, no connection trouble, no required update (§10). */
		bool ServerUsable() const { return OfflineOf(Online()) == EOffline::None; }
		FFVPager& CurPager() { return bSeason ? SeasonPager : AllPager; }

		bool PageCat(bool bInSeason, int32 Page, ECat& Out) const
		{
			if (!bInSeason)
			{
				Out = GAllCats[FMath::Clamp(Page, 0, NumAllPages - 1)];
				return true;
			}
			if (Page <= 0) { return false; }
			Out = GSeasonCats[FMath::Clamp(Page - 1, 0, NumSeasonPages - 2)];
			return true;
		}

		FFVScrollView* CurScroll()
		{
			ECat C;
			if (!PageCat(bSeason, CurPager().Page, C)) { return &InfoScroll; }
			return Scrolls.IsValidIndex(int32(C)) ? &Scrolls[int32(C)] : nullptr;
		}

		void SetSeason(bool bNew)
		{
			if (bNew == bSeason) { return; }
			bSeason = bNew;
			FVUI::Sfx(EFVSfx::Click, 0.8f, bNew ? 1.1f : 1.f);
		}

		// ------------------------------------------------------------------ search
		void UpdateSearch()
		{
			const double T = Game->GetTime();
			if (Search.Text != LastSearchText)
			{
				LastSearchText = Search.Text;
				LastSearchEdit = T;
			}
			const FString Q = Search.Text.TrimStartAndEnd();
			if (Q != ActiveQuery && T - LastSearchEdit > 0.8) { ApplyQuery(Q); }
		}

		void ApplyQuery(const FString& Q)
		{
			const FString Clean = Q.TrimStartAndEnd();
			if (Clean == ActiveQuery) { return; }
			ActiveQuery = Clean;
			for (int32 i = 0; i < States.Num(); ++i)
			{
				const int32 Gen = States[i].Gen + 1;
				States[i] = FCatState();
				States[i].Gen = Gen;
				Scrolls[i].Reset();
			}
		}

		// ------------------------------------------------------------------ fetching
		void Fetch(ECat Cat, bool bMore)
		{
			UFVOnline* On = Online();
			const int32 CI = int32(Cat);
			if (!On || !ServerUsable() || !States.IsValidIndex(CI)) { return; }
			FCatState& S = States[CI];
			int32 Offset = 0;
			if (bMore)
			{
				if (S.bLoadingMore || S.bLoading) { return; }
				S.bLoadingMore = true;
				Offset = S.Page.Offset + S.Page.Entries.Num();
			}
			else
			{
				++S.Gen;
				S.bLoading = true;
				S.bLoadingMore = false;
				S.DataQuery = ActiveQuery;
			}
			const int32 Gen = S.Gen;
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			TSharedRef<bool> AliveFlag = Alive;
			FLeaderboardScreen* Self = this;
			On->FetchBoard(Cat, ActiveQuery, Offset, PageSize, [WeakGame, AliveFlag, Self, CI, Gen, bMore](const FFVBoardPage& R)
			{
				if (!WeakGame.IsValid() || !*AliveFlag) { return; }
				Self->OnBoard(CI, Gen, bMore, R);
			});
		}

		void OnBoard(int32 CI, int32 Gen, bool bMore, const FFVBoardPage& R)
		{
			if (!States.IsValidIndex(CI)) { return; }
			FCatState& S = States[CI];
			if (S.Gen != Gen) { return; }
			const double Now = FPlatformTime::Seconds();
			if (bMore)
			{
				S.bLoadingMore = false;
				if (!R.bOk) { S.RetryAt = Now + 6.0; return; }
				TSet<int64> Have;
				for (const FFVBoardEntry& E : S.Page.Entries) { Have.Add(E.PlayerId); }
				for (const FFVBoardEntry& E : R.Entries) { if (!Have.Contains(E.PlayerId)) { S.Page.Entries.Add(E); } }
				S.Page.Total = R.Total;
				if (R.bHasMe) { S.Page.bHasMe = true; S.Page.Me = R.Me; }
				if (R.Entries.Num() == 0) { S.Page.Total = S.Page.Offset + S.Page.Entries.Num(); }   // nothing more to load
				return;
			}
			S.bLoading = false;
			if (R.bOk)
			{
				S.Page = R;
				S.bHasData = true;
				S.bError = false;
				S.Error.Reset();
				S.FetchedAt = Now;
			}
			else
			{
				S.bError = true;
				S.Error = R.Error.IsEmpty() ? FString(TEXT("Нет связи с сервером. Проверьте интернет и попробуйте ещё раз.")) : R.Error;
				S.RetryAt = Now + 15.0;
			}
		}

		/** Called for every visible category page: first load, stale refresh (current page only), automatic retries. */
		void EnsureData(ECat Cat, bool bCurrent)
		{
			const int32 CI = int32(Cat);
			if (!States.IsValidIndex(CI) || !ServerUsable()) { return; }
			FCatState& S = States[CI];
			if (S.bLoading) { return; }
			const double Now = FPlatformTime::Seconds();
			if (S.bError && Now < S.RetryAt) { return; }
			const bool bWrongQuery = S.DataQuery != ActiveQuery;
			const bool bStale = bCurrent && S.bHasData && Now - S.FetchedAt > StaleSeconds && !S.bLoadingMore;
			if (!S.bHasData || bWrongQuery || bStale) { Fetch(Cat, false); }
		}

		// ------------------------------------------------------------------ actions
		void RunAct()
		{
			const EAct A = Act;
			Act = EAct::None;
			if (A == EAct::None || !Game) { return; }
			switch (A)
			{
			case EAct::Close:
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
			case EAct::Back:
			{
				UFVGame* G = Game;
				if (bFromProfile) { G->OpenScreen(EFVScreen::Profile); }
				else { G->CloseScreen(); }
				return;
			}
			case EAct::OpenProfile:
			{
				if (ActPid <= 0) { return; }
				FVUI::Sfx(EFVSfx::Click);
				UFVGame* G = Game;
				G->OpenScreenWith(EFVScreen::Profile, FName(*FString::Printf(TEXT("%lld"), (long long)ActPid)));
				return;
			}
			case EAct::Refresh:
			{
				FVUI::Sfx(EFVSfx::Click, 0.7f);
				UFVOnline* On = Online();
				const EOffline Why = OfflineOf(On);
				if (Why == EOffline::OfflineMode || Why == EOffline::NoConnection)
				{
					bRetried = true;
					On->RetryOnline();   // offline: the refresh button reconnects
					return;
				}
				if (Why != EOffline::None) { return; }
				On->RequestSync(false);
				ECat C;
				if (PageCat(bSeason, CurPager().Page, C)) { Fetch(C, false); }
				return;
			}
			case EAct::ClearSearch:
			{
				FVKit::EndTextField(Search);
				Search.Text.Reset();
				LastSearchText.Reset();
				ApplyQuery(FString());
				FVUI::Sfx(EFVSfx::Click, 0.7f);
				return;
			}
			default: return;
			}
		}

		// ------------------------------------------------------------------ header pieces
		/** «За всё время» / «Турнир (3 дня)» segmented switch. */
		void DrawPeriodSwitch(FVDraw& D, const FFVRect& R, bool bP)
		{
			const float TS = bP ? FV::Ui::Body() : 8.f;
			if (D.Button(FVDraw::SplitH(R, 0, 2, 4.f), TEXT("За всё время"), bSeason ? EFVButton::Normal : EFVButton::Primary, true, TS)) { SetSeason(false); }
			if (D.Button(FVDraw::SplitH(R, 1, 2, 4.f), TEXT("Турнир (3 дня)"), bSeason ? EFVButton::Primary : EFVButton::Normal, true, TS)) { SetSeason(true); }
		}

		/** Search field + a small button: × (clear) while there is a query, refresh otherwise. */
		void DrawSearchRow(FVDraw& D, const FFVRect& R, bool bP)
		{
			const float BW = R.H;
			const FFVRect FieldR(R.X, R.Y, R.W - BW - 4.f, R.H);
			const FFVRect BtnR(R.Right() - BW, R.Y, BW, R.H);
			if (FVKit::TextField(D, FieldR, Search, TEXT("Поиск: имя или #номер"))) { ApplyQuery(Search.Text); }
			const bool bClear = !Search.Text.IsEmpty() || !ActiveQuery.IsEmpty();
			const bool bHit = D.Button(BtnR, bClear ? FString(TEXT("×")) : FString(), EFVButton::Normal, true, bP ? FV::Ui::Header() : 10.f);
			if (!bClear)
			{
				const bool bDown = FVUI::IsPressingOn(D, BtnR);
				RefreshGlyph(D, BtnR.Center() + FVector2D(0.f, bDown ? 1.f : 0.f), bP ? 7.f : 4.5f, FVPal::Ink());
				if (!bP && D.Hover(BtnR)) { D.Tooltip(TEXT("Обновить"), TEXT("Загрузить свежие места (F5)")); }
			}
			if (bHit) { Act = bClear ? EAct::ClearSearch : EAct::Refresh; }
		}

		/** Tournament number + countdown (season), or the number of ranked fishermen (all time). */
		FString InfoLine() const
		{
			const UFVOnline* On = Online();
			if (!On || !ServerUsable()) { return FString(); }
			if (bSeason)
			{
				const FFVSeasonInfo& S = On->GetSeason();
				if (!S.bKnown) { return TEXT("Турнир: ждём связи с сервером…"); }
				const int64 Left = S.EndsUnix - On->GetServerNow();
				if (Left <= 0) { return FString::Printf(TEXT("Турнир №%d завершён — подводим итоги"), S.Id); }
				return FString::Printf(TEXT("Турнир №%d · до конца %s"), S.Id, *Countdown(Left));
			}
			ECat C;
			if (PageCat(false, AllPager.Page, C) && States.IsValidIndex(int32(C)) && States[int32(C)].bHasData && ActiveQuery.IsEmpty())
			{
				const int32 N = States[int32(C)].Page.Total;
				return FString::Printf(TEXT("В рейтинге %s %s"), *FV::Money(N), FVUI::Plural(N, TEXT("рыбак"), TEXT("рыбака"), TEXT("рыбаков")));
			}
			return FString();
		}

		/** Portrait tabs: icon + short label cells following the pager (7 categories do not fit as text tabs). */
		void IconTabs(FVDraw& D, const FFVRect& Row, FFVPager& Pager)
		{
			const int32 N = bSeason ? NumSeasonPages : NumAllPages;
			const float Gap = 3.f;
			for (int32 i = 0; i < N; ++i)
			{
				const FFVRect Cell = FVDraw::SplitH(Row, i, N, Gap);
				const bool bActive = i == Pager.Page;
				ECat C = ECat::Level;
				const bool bCat = PageCat(bSeason, i, C);
				const EFVIcon Icon = bCat ? InfoOf(C).Icon : EFVIcon::Trophy;
				const FString Label = bCat ? FString(InfoOf(C).Short) : FString(TEXT("Турнир"));
				if (D.Button(Cell, FString(), bActive ? EFVButton::Primary : EFVButton::Normal) && !bActive)
				{
					Pager.GoTo(i);
				}
				const float DY = FVUI::IsPressingOn(D, Cell) ? 1.f : 0.f;
				IconAt(D, Icon, FFVRect(FMath::RoundToFloat(Cell.Center().X - 8.f), Cell.Y + 5.f + DY, 16.f, 16.f), 2.f);
				const float LS = FV::Ui::Small();
				const FString L = D.Ellipsize(Label, Cell.W - 4.f, LS, true);
				const FVector2D P(Cell.Center().X, Cell.Y + 23.f + DY);
				if (bActive) { D.TextOutlined(L, P, LS, FVPal::White(), EFVAlign::Center, FV::Col(32, 72, 24)); }
				else { D.Text(L, P, LS, FVPal::Ink(), EFVAlign::Center, false, true); }
			}
			if (N > 1)
			{
				const float V = FMath::Clamp(Pager.VisualPos(), 0.f, float(N - 1));
				const int32 I0 = FMath::FloorToInt(V);
				const int32 I1 = FMath::Min(N - 1, I0 + 1);
				const float T = V - float(I0);
				const FFVRect C0 = FVDraw::SplitH(Row, I0, N, Gap);
				const FFVRect C1 = FVDraw::SplitH(Row, I1, N, Gap);
				const float X = FMath::Lerp(C0.X, C1.X, T) + 5.f;
				const float W = FMath::Lerp(C0.W, C1.W, T) - 10.f;
				D.Rect(FFVRect(FMath::RoundToFloat(X), Row.Bottom() + 2.f, FMath::RoundToFloat(W), 3.f), FVPal::Gold());
			}
		}

		// ------------------------------------------------------------------ pages
		void DrawPagesArea(FVDraw& D, const FFVRect& Area, bool bP)
		{
			if (!ServerUsable())
			{
				UFVOnline* On = Online();
				OfflineCard(D, Area, bP, On, OfflineOf(On), TEXT("Таблица лидеров и турниры откроются, как только игра подключится к серверу."), bRetried);
				return;
			}
			FFVPager& Pager = CurPager();
			const int32 N = bSeason ? NumSeasonPages : NumAllPages;
			Pager.Update(D, Area, N);
			if (Pager.PageChangedThisFrame())
			{
				FVKit::EndTextField(Search);
			}
			const int32 Cur = Pager.Page;
			Pager.DrawPages(D, Area, [this, &D, &Area, bP, Cur](int32 Page)
			{
				ECat C;
				if (!PageCat(bSeason, Page, C)) { DrawSeasonInfo(D, Area, bP); }
				else { DrawCatPage(D, C, Area, bP, Page == Cur); }
			});
			FVKit::SwipeHint(D, Area, Pager);
		}

		/** Centered friendly message (+ optional «Повторить»). */
		void DrawMessage(FVDraw& D, const FFVRect& Area, bool bP, EFVIcon Icon, const FString& Msg, bool bRetry, ECat RetryCat = ECat::Level)
		{
			const float SS = bP ? FV::Ui::Body() - 0.5f : 7.5f;
			const float W = FMath::Min(Area.W - 24.f, bP ? 300.f : 320.f);
			const float TH = D.MeasureWrapped(Msg, W - 20.f, SS, false);
			const float BH = bRetry ? (bP ? 36.f : 20.f) : 0.f;
			const float H = FMath::CeilToFloat(28.f + TH + (bRetry ? BH + 12.f : 0.f) + 10.f);
			const FVector2D C = Area.Center();
			const FFVRect R(FMath::RoundToFloat(C.X - W * 0.5f), FMath::RoundToFloat(FMath::Max(Area.Y + 6.f, C.Y - H * 0.5f)), W, H);
			Card(D, R);
			IconAt(D, Icon, FFVRect(FMath::RoundToFloat(C.X - 8.f), R.Y + 6.f, 16.f, 16.f), 1.f);
			D.TextWrapped(Msg, FVector2D(R.X + 10.f, R.Y + 26.f), W - 20.f, SS, FVPal::Ink(), false);
			if (bRetry)
			{
				const float BW = bP ? 160.f : 110.f;
				if (D.Button(FFVRect(FMath::RoundToFloat(C.X - BW * 0.5f), R.Bottom() - BH - 10.f, BW, BH), TEXT("Повторить"), EFVButton::Primary, true, bP ? FV::Ui::Body() : 8.f))
				{
					Fetch(RetryCat, false);
				}
			}
		}

		void DrawCatPage(FVDraw& D, ECat Cat, const FFVRect& Area, bool bP, bool bCurrent)
		{
			const int32 CI = int32(Cat);
			if (!States.IsValidIndex(CI)) { return; }
			EnsureData(Cat, bCurrent);
			FCatState& S = States[CI];
			const FCatInfo Info = InfoOf(Cat);
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const double T = Game->GetTime();

			// ---- page header
			const float HeadH = bP ? 22.f : 14.f;
			const float X = Area.X + (bP ? 8.f : 6.f);
			const float RightX = Area.Right() - (bP ? 10.f : RowRightInset + 6.f);
			const bool bSearch = !ActiveQuery.IsEmpty();
			if (bP)
			{
				const FString Left = bSearch ? FString::Printf(TEXT("Поиск «%s»"), *ActiveQuery) : FString(Info.Desc);
				FString Right;
				if (S.bHasData) { Right = bSearch ? FString::Printf(TEXT("найдено: %d"), S.Page.Total) : FString(); }
				const float RW = Right.IsEmpty() ? 0.f : D.MeasureText(Right, SS, true).X + 8.f;
				D.Text(D.Ellipsize(Left, RightX - X - RW, SS, true), FVector2D(X, Area.Y + 4.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
				if (!Right.IsEmpty()) { D.Text(Right, FVector2D(RightX, Area.Y + 4.f), SS, FVPal::InkSoft(), EFVAlign::Right, false, true); }
			}
			else
			{
				const float HY = Area.Y + 3.f;
				D.Text(TEXT("Место"), FVector2D(Area.X + 4.f, HY), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
				const FString Who = bSearch ? FString::Printf(TEXT("Рыбак · поиск «%s»%s"), *ActiveQuery, S.bHasData ? *FString::Printf(TEXT(": %d"), S.Page.Total) : TEXT(""))
					: FString(TEXT("Рыбак"));
				D.Text(D.Ellipsize(Who, Area.W * 0.5f, SS, true), FVector2D(Area.X + 40.f, HY), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
				D.Text(TEXT("Ур."), FVector2D(Area.Right() - RowRightInset - LevelColFromRight + 6.f, HY), SS, FVPal::InkSoft(), EFVAlign::Center, false, true);
				D.Text(Info.Desc, FVector2D(RightX, HY), SS, FVPal::InkSoft(), EFVAlign::Right, false, true);
			}
			if (S.bLoading && S.bHasData) { Spinner(D, FVector2D(RightX - (bP ? 4.f : 0.f), Area.Y + HeadH * 0.5f), bP ? 6.f : 4.f, T); }
			D.Rect(FFVRect(Area.X + 2.f, Area.Y + HeadH - 2.f, Area.W - 4.f, 1.f), FV::Col(206, 150, 92));
			const FFVRect List(Area.X, Area.Y + HeadH, Area.W, Area.H - HeadH);

			// ---- states
			if (!S.bHasData)
			{
				if (S.bError && !S.bLoading)
				{
					DrawMessage(D, List, bP, EFVIcon::Exclaim, S.Error, true, Cat);
				}
				else
				{
					Spinner(D, FVector2D(List.Center().X, List.Center().Y - 12.f), bP ? 12.f : 9.f, T);
					D.Text(TEXT("Загружаем таблицу…"), FVector2D(List.Center().X, List.Center().Y + 6.f), bP ? FV::Ui::Body() : 8.f, FVPal::InkSoft(), EFVAlign::Center, false, true);
				}
				return;
			}
			if (S.Page.Entries.Num() == 0)
			{
				FString Msg;
				if (bSearch) { Msg = FString::Printf(TEXT("Никого не нашли по запросу «%s». Проверьте имя или номер (например, #1042)."), *ActiveQuery); }
				else if (UFVOnline::IsSeasonCategory(Cat)) { Msg = TEXT("В этом турнире пока никого. Поймайте рыбу — и вы окажетесь на первом месте!"); }
				else { Msg = TEXT("Здесь пока никого нет. Станьте первым!"); }
				DrawMessage(D, List, bP, bSearch ? EFVIcon::Question : EFVIcon::Fish, Msg, false);
				if (S.Page.bHasMe) { DrawPinned(D, List, S, Cat, bP, INDEX_NONE); }
				return;
			}

			// ---- the list
			const float RowH = bP ? 44.f : 20.f;
			const float Pitch = bP ? 46.f : 21.f;
			const int32 Num = S.Page.Entries.Num();
			const bool bMore = S.Page.Offset + Num < S.Page.Total;
			const float Top = bP ? 4.f : 2.f;
			const float ContentH = Top + float(Num) * Pitch + (bMore ? Pitch : 0.f) + (S.Page.bHasMe ? Pitch + 4.f : 0.f) + 4.f;
			FFVScrollView& Sc = Scrolls[CI];
			Sc.Update(D, List, ContentH);
			const float RW = List.W - 2.f - RowRightInset;
			int32 MeIdx = INDEX_NONE;
			bool bMeVisible = false;
			D.PushClip(List);
			for (int32 i = 0; i < Num; ++i)
			{
				const FFVBoardEntry& E = S.Page.Entries[i];
				const float Y = List.Y + Top - Sc.Offset() + float(i) * Pitch;
				const bool bMine = E.bMe || (S.Page.bHasMe && E.PlayerId == S.Page.Me.PlayerId);
				if (bMine)
				{
					MeIdx = i;
					bMeVisible = Y >= List.Y - 1.f && Y + RowH <= List.Bottom() + 1.f;
				}
				if (Y + RowH < List.Y || Y > List.Bottom()) { continue; }
				DrawRow(D, FFVRect(List.X + 2.f, Y, RW, RowH), E, bMine, Cat, bP, i);
			}
			if (bMore)
			{
				const float Y = List.Y + Top - Sc.Offset() + float(Num) * Pitch;
				if (Y < List.Bottom() + Pitch * 3.f && !S.bLoadingMore && FPlatformTime::Seconds() >= S.RetryAt) { Fetch(Cat, true); }
				if (Y < List.Bottom())
				{
					const float CY = Y + RowH * 0.5f;
					Spinner(D, FVector2D(List.Center().X - 50.f, CY), bP ? 6.f : 4.f, T);
					D.Text(TEXT("Загружаем ещё…"), FVector2D(List.Center().X - 40.f, FMath::RoundToFloat(CY - D.LineHeight(SS) * 0.5f)), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
				}
			}
			D.PopClip();
			Sc.DrawFades(D, List, ListBg());
			Sc.DrawBar(D, List);
			if (S.bError && S.bHasData)
			{
				const FString E = TEXT("Не удалось обновить — показаны прошлые данные");
				D.Text(D.Ellipsize(E, List.W - 12.f, SS, true), FVector2D(List.Center().X, List.Y + 2.f), SS, InkBad(), EFVAlign::Center, false, true);
			}
			if (S.Page.bHasMe && !bMeVisible) { DrawPinned(D, List, S, Cat, bP, MeIdx); }
		}

		/** The local player's row pinned to the bottom of the list (tap = scroll to it, or open the own profile). */
		void DrawPinned(FVDraw& D, const FFVRect& List, const FCatState& S, ECat Cat, bool bP, int32 MeIdx)
		{
			const float RowH = bP ? 44.f : 20.f;
			const FFVRect R(List.X + 2.f, List.Bottom() - RowH - 2.f, List.W - 2.f - RowRightInset, RowH);
			D.Rect(FFVRect(List.X, R.Y - 5.f, List.W, R.H + 7.f), FV::Alpha(ListBg(), 0.92f));
			D.Rect(FFVRect(List.X + 2.f, R.Y - 3.f, List.W - 4.f, 1.f), FV::Col(120, 70, 36, 90));
			FFVBoardEntry Me = S.Page.Me;
			Me.bMe = true;
			const bool bClicked = DrawRow(D, R, Me, true, Cat, bP, -1, false);
			if (bClicked)
			{
				const float Pitch = bP ? 46.f : 21.f;
				if (MeIdx != INDEX_NONE && Scrolls.IsValidIndex(int32(Cat)))
				{
					Scrolls[int32(Cat)].ScrollToShow(float(MeIdx) * Pitch, RowH + 8.f + RowH);
					FVUI::Sfx(EFVSfx::Click, 0.7f);
				}
				else
				{
					Act = EAct::OpenProfile;
					ActPid = Me.PlayerId;
				}
			}
		}

		/** One ranked row. Returns true when tapped (and, unless bOpenOnTap is false, requests the player's profile). */
		bool DrawRow(FVDraw& D, const FFVRect& R, const FFVBoardEntry& E, bool bMine, ECat Cat, bool bP, int32 Index, bool bOpenOnTap = true)
		{
			const bool bPress = FVUI::IsPressingOn(D, R);
			const bool bHover = !bP && !FV::IsTouchUI() && D.Hover(R);
			FLinearColor Bg = (Index >= 0 && Index % 2 == 1) ? FV::Col(236, 190, 126, 70) : FV::Col(255, 244, 222, 90);
			if (bMine) { Bg = FV::Col(255, 222, 128); }
			if (bHover) { Bg = bMine ? FV::Col(255, 232, 156) : FV::Col(255, 248, 226, 200); }
			if (bPress) { Bg = FV::Col(230, 186, 120); }
			D.Rect(R, Bg);
			if (bMine)
			{
				D.Frame(R, FV::Col(206, 140, 30), 1.f);
				D.Rect(FFVRect(R.X, R.Y, 3.f, R.H), FV::Col(206, 140, 30));
			}
			const float CY = R.Center().Y;
			const float NS = bP ? FV::Ui::Body() + 0.5f : 8.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;

			// place
			const float RankW = bP ? 40.f : 34.f;
			if (E.Rank >= 1 && E.Rank <= 3)
			{
				Medal(D, FVector2D(FMath::RoundToFloat(R.X + RankW * 0.5f + 1.f), FMath::RoundToFloat(CY + (bP ? 2.f : 1.f))), bP ? 11.f : 7.f, E.Rank, bP ? FV::Ui::Small() + 0.5f : 6.5f);
			}
			else
			{
				const FString Rk = E.Rank > 0 ? FString::FromInt(E.Rank) : FString(TEXT("—"));
				const float RS = bP ? FV::Ui::Body() : 7.5f;
				D.Text(Rk, FVector2D(R.X + RankW * 0.5f + 1.f, FMath::RoundToFloat(CY - D.LineHeight(RS) * 0.5f)), RS, bMine ? FV::Col(120, 72, 20) : FVPal::InkSoft(), EFVAlign::Center, false, true);
			}

			// value (+ the fish for the biggest-fish boards)
			// «Уровень»: the level the server stores (update 6: 0.5 clients stay at 10 above 10 000 XP, so it is not
			// derived from the XP value on this client's 15-level table)
			const FString Val = Cat == ECat::Level
				? FString::Printf(TEXT("Ур. %d · %s опыта"), E.Level, *FV::Money(FMath::Max<int64>(0, E.Value)))
				: UFVOnline::FormatBoardValue(Cat, E.Value);
			const float VS = bP ? FV::Ui::Body() + 0.5f : 8.f;
			const float VRight = R.Right() - (bP ? 8.f : 6.f);
			const float VW = D.MeasureText(Val, VS, true).X;
			const FString Name = E.Name.IsEmpty() ? FString(TEXT("Рыбак")) : E.Name;
			const FString Tag = E.Tag > 0 ? FString::Printf(TEXT("#%d"), E.Tag) : FString();
			const float NX = R.X + RankW + 4.f;
			if (bP)
			{
				const float TopY = FMath::RoundToFloat(R.Y + 5.f);
				float ValueLeft = VRight - VW;
				if (IsBiggest(Cat) && !E.Fish.FishId.IsNone())
				{
					D.Text(Val, FVector2D(VRight, TopY), VS, FVPal::Ink(), EFVAlign::Right, false, true);
					const FFVFishDef* F = FVDB::FindFish(E.Fish.FishId);
					const FString FN = F ? F->Name : FString();
					const float FW = FMath::Min(D.MeasureText(FN, SS, false).X, 110.f);
					D.Text(D.Ellipsize(FN, 110.f, SS, false), FVector2D(VRight, TopY + D.LineHeight(VS)), SS, FVPal::InkSoft(), EFVAlign::Right, false, false);
					const float FishW = 34.f;
					const float FX = VRight - FMath::Max(VW, FW) - FishW - 4.f;
					FishAt(D, E.Fish.FishId, FFVRect(FX, R.Y + 6.f, FishW, R.H - 12.f), 2.f);
					ValueLeft = FX;
				}
				else
				{
					D.Text(Val, FVector2D(VRight, FMath::RoundToFloat(CY - D.LineHeight(VS) * 0.5f)), VS, FVPal::Ink(), EFVAlign::Right, false, true);
				}
				const float NW = ValueLeft - 8.f - NX;
				D.Text(D.Ellipsize(Name, NW, NS, true), FVector2D(NX, TopY), NS, FVPal::Ink(), EFVAlign::Left, false, true);
				FString Sub = Tag;
				Sub += (Sub.IsEmpty() ? TEXT("") : TEXT(" · ")) + FString::Printf(TEXT("ур. %d"), E.Level);
				if (bMine) { Sub += TEXT(" · это вы"); }
				D.Text(D.Ellipsize(Sub, NW, SS, false), FVector2D(NX, TopY + D.LineHeight(NS)), SS, bMine ? FV::Col(140, 84, 20) : FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			else
			{
				const float TY = FMath::RoundToFloat(CY - D.LineHeight(NS) * 0.5f);
				D.Text(Val, FVector2D(VRight, TY), VS, FVPal::Ink(), EFVAlign::Right, false, true);
				const float LevelX = R.Right() - LevelColFromRight;
				if (IsBiggest(Cat) && !E.Fish.FishId.IsNone())
				{
					const FFVFishDef* F = FVDB::FindFish(E.Fish.FishId);
					const float FX = LevelX + 34.f;
					FishAt(D, E.Fish.FishId, FFVRect(FX, R.Y + 2.f, 24.f, R.H - 4.f), 1.f);
					if (F)
					{
						const float MaxW = VRight - VW - 8.f - (FX + 28.f);
						D.Text(D.Ellipsize(F->Name, MaxW, SS, false), FVector2D(FX + 28.f, FMath::RoundToFloat(CY - D.LineHeight(SS) * 0.5f)), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
					}
				}
				const FString LevelT = FString::FromInt(E.Level);
				D.Text(LevelT, FVector2D(LevelX + 6.f, TY), NS, FV::Col(120, 72, 20), EFVAlign::Center, false, true);
				const float TagW = Tag.IsEmpty() ? 0.f : D.MeasureText(Tag, SS, false).X + 5.f;
				const float MeW = bMine ? D.MeasureText(TEXT("это вы"), SS, true).X + 12.f : 0.f;
				const float NameMax = FMath::Max(30.f, LevelX - 10.f - NX - TagW - MeW);
				const FString NameE = D.Ellipsize(Name, NameMax, NS, true);
				D.Text(NameE, FVector2D(NX, TY), NS, FVPal::Ink(), EFVAlign::Left, false, true);
				float TX = NX + D.MeasureText(NameE, NS, true).X + 5.f;
				const float SY = FMath::RoundToFloat(CY - D.LineHeight(SS) * 0.5f + 1.f);
				if (!Tag.IsEmpty())
				{
					D.Text(Tag, FVector2D(TX, SY), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
					TX += TagW;
				}
				if (bMine)
				{
					const FString MeT = TEXT("это вы");
					const float W = D.MeasureText(MeT, SS, true).X + 6.f;
					const FFVRect Chip(TX, FMath::RoundToFloat(CY - 5.f), W, 10.f);
					D.Rect(Chip, FV::Col(206, 140, 30));
					D.Text(MeT, FVector2D(Chip.X + 3.f, Chip.Y), SS, FVPal::White(), EFVAlign::Left, false, true);
				}
			}

			const bool bClicked = D.Clicked(R);
			if (bClicked && bOpenOnTap)
			{
				Act = EAct::OpenProfile;
				ActPid = E.PlayerId;
			}
			return bClicked;
		}

		// ------------------------------------------------------------------ tournament info page
		// «Турнир №N»: countdown card, a strip explaining that every category is rewarded separately (+ the projection),
		// then one card per season category: the player's place, his projected tier (MyTiers) and the rewards of all
		// three tiers at his level (UFVOnline::SeasonRewardText), the projected tier highlighted. Landscape: 2x2 grid.

		/** Ranked players of a season category: the loaded board's total when known, else the tournament participants. */
		int32 CatTotal(ECat C, const FFVSeasonInfo& S, int32 Rank) const
		{
			int32 N = S.Participants;
			const int32 CI = int32(C);
			if (States.IsValidIndex(CI) && States[CI].bHasData && States[CI].DataQuery.IsEmpty() && States[CI].Page.Total > 0) { N = States[CI].Page.Total; }
			return FMath::Max(N, Rank);
		}

		static void MyPlace(const FFVSeasonInfo& S, ECat C, int32& OutRank, int32& OutTier)
		{
			const int32* RankP = S.MyRanks.Find(C);
			OutRank = RankP ? FMath::Max(0, *RankP) : 0;
			const int32* TierP = S.MyTiers.Find(C);
			OutTier = (OutRank > 0 && TierP) ? FMath::Clamp(*TierP, 0, 3) : 0;
		}

		/** One season category card. Returns the height; draws only when bDraw (MinH stretches the card). */
		float SeasonCatCard(FVDraw& D, ECat C, const FFVSeasonInfo& S, int32 Level, float X, float Y, float W, bool bP, bool bDraw, float MinH = 0.f)
		{
			const FCatInfo Info = InfoOf(C);
			int32 Rank = 0, Tier = 0;
			MyPlace(S, C, Rank, Tier);
			const float TS = bP ? FV::Ui::Body() + 0.5f : 8.f;       // category title
			const float BS = bP ? FV::Ui::Body() - 0.5f : 7.f;       // reward text
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float Pad = bP ? 8.f : 6.f;
			const float IS = bP ? 20.f : 14.f;
			const float MedR = bP ? 9.f : 6.f;
			const float LabelW = bP ? 66.f : 62.f;
			const float RowX = X + Pad;
			const float RowW = W - Pad * 2.f;
			const float TextX = RowX + 4.f + MedR * 2.f + 6.f + LabelW;
			const float TagS = 6.5f;
			const FString Tag = bP ? FString(TEXT("ваш прогноз")) : FString(TEXT("прогноз"));
			const float TagW = FMath::RoundToFloat(D.MeasureText(Tag, TagS, true).X + 8.f);
			const float HeadH = FMath::CeilToFloat(FMath::Max(IS, D.LineHeight(TS) + D.LineHeight(SS, false)));
			const float HeadGap = bP ? 9.f : 6.f;
			const float RowGap = bP ? 3.f : 2.f;
			const float LabelH = bP ? D.LineHeight(SS + 1.f) + D.LineHeight(SS, false) : D.LineHeight(SS);
			FString Texts[3];
			float TextWs[3];
			float RowHs[3];
			float RowsH = 0.f;
			for (int32 i = 0; i < 3; ++i)
			{
				Texts[i] = UFVOnline::SeasonRewardText(C, i + 1, Level);
				if (Texts[i].IsEmpty()) { Texts[i] = TEXT("—"); }
				// landscape: the «прогноз» tag sits inside the projected row, on the right
				TextWs[i] = FMath::Max(40.f, RowX + RowW - 4.f - TextX - ((!bP && Tier == i + 1) ? TagW + 6.f : 0.f));
				const float TH = D.MeasureWrapped(Texts[i], TextWs[i], BS, true, 1.05f);
				RowHs[i] = FMath::CeilToFloat(FMath::Max(FMath::Max(LabelH, TH), MedR * 2.f + (bP ? 4.f : 2.f)) + (bP ? 10.f : 5.f));
				RowsH += RowHs[i];
			}
			const float H = FMath::Max(MinH, FMath::CeilToFloat(Pad + HeadH + HeadGap + RowsH + RowGap * 2.f + Pad - 2.f));
			if (!bDraw) { return H; }

			const FFVRect R(X, Y, W, H);
			const FLinearColor Acc = TierColor(Tier);
			Card(D, R, false, Tier > 0 ? &Acc : nullptr);
			if (Tier > 0) { D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 3.f, R.H - 2.f), Acc); }

			// ---- header: icon, title, what is measured | tier chip, place
			const float HY = Y + Pad;
			IconAt(D, Info.Icon, FFVRect(X + Pad, FMath::RoundToFloat(HY + (HeadH - IS) * 0.5f), IS, IS), bP ? 2.f : 1.f);
			const float HX = X + Pad + IS + (bP ? 8.f : 5.f);
			const float HRight = X + W - Pad;
			float ChipW = 0.f;
			if (Rank > 0) { ChipW = TierChip(D, Tier, FVector2D(HRight, HY - 1.f), SS) + 6.f; }
			D.Text(D.Ellipsize(Info.Tab, HRight - ChipW - HX, TS, true), FVector2D(HX, HY), TS, FVPal::Ink(), EFVAlign::Left, false, true);
			const float SY = HY + D.LineHeight(TS);
			FString Place;
			if (Rank > 0)
			{
				const int32 N = CatTotal(C, S, Rank);
				const int32 Pct = FMath::Clamp(FMath::CeilToInt(float(Rank) * 100.f / float(FMath::Max(1, N))), 1, 100);
				Place = FString::Printf(TEXT("место %d из %d · топ %d%%"), Rank, N, Pct);
			}
			else { Place = TEXT("пока без результата"); }
			const float PW = D.MeasureText(Place, SS, true).X;
			D.Text(Place, FVector2D(HRight, SY), SS, Rank > 0 ? FV::Col(120, 72, 20) : FVPal::InkSoft(), EFVAlign::Right, false, true);
			D.Text(D.Ellipsize(Info.Desc, HRight - PW - 8.f - HX, SS, false), FVector2D(HX, SY), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			D.Rect(FFVRect(X + Pad, FMath::RoundToFloat(HY + HeadH + HeadGap * 0.5f - 1.f), W - Pad * 2.f, 1.f), FV::Col(214, 166, 110));

			// ---- the three tiers
			float RY = HY + HeadH + HeadGap;
			for (int32 i = 0; i < 3; ++i)
			{
				const int32 T = i + 1;
				const bool bMine = T == Tier;
				const FFVRect Row(RowX, RY, RowW, RowHs[i]);
				FLinearColor Edge = TierColor(T) * 0.62f;
				Edge.A = 1.f;
				if (bMine)
				{
					FLinearColor Fill = TierColor(T);
					Fill.A = 0.34f;
					D.Rect(Row, Fill);
					D.Frame(Row, Edge, 1.f);
				}
				const float MCX = FMath::RoundToFloat(RowX + 4.f + MedR);
				const float MCY = FMath::RoundToFloat(Row.Center().Y + (bP ? 2.f : 1.f));
				Medal(D, FVector2D(MCX, MCY), MedR, T, bP ? FV::Ui::Small() : 6.5f);
				const float LX = MCX + MedR + 6.f;
				if (bP)
				{
					const float LY = FMath::RoundToFloat(Row.Center().Y - LabelH * 0.5f);
					D.Text(TierName(T), FVector2D(LX, LY), SS + 1.f, FVPal::Ink(), EFVAlign::Left, false, true);
					D.Text(TierTop(T), FVector2D(LX, LY + D.LineHeight(SS + 1.f)), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
				else
				{
					const float LY = FMath::RoundToFloat(Row.Center().Y - D.LineHeight(SS) * 0.5f);
					const FString Nm = TierName(T);
					D.Text(Nm, FVector2D(LX, LY), SS, FVPal::Ink(), EFVAlign::Left, false, true);
					D.Text(TierTop(T), FVector2D(LX + D.MeasureText(Nm, SS, true).X + 3.f, LY), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				}
				const float TH = D.MeasureWrapped(Texts[i], TextWs[i], BS, true, 1.05f);
				const FLinearColor TextC = bMine ? FV::Col(96, 52, 8) : (Tier > 0 ? FV::Col(140, 100, 66) : FV::Col(120, 72, 20));
				D.TextWrapped(Texts[i], FVector2D(TextX, FMath::RoundToFloat(Row.Center().Y - TH * 0.5f)), TextWs[i], BS, TextC, true, 1.05f);
				if (bMine)
				{
					// «ваш прогноз» tag: sticks out of the row's top edge (portrait) / sits inside on the right (landscape)
					const float TagH = FMath::RoundToFloat(D.LineHeight(TagS) + 2.f);
					const FFVRect TagR = bP ? FFVRect(FMath::RoundToFloat(Row.Right() - TagW - 6.f), FMath::RoundToFloat(Row.Y - TagH * 0.5f), TagW, TagH)
						: FFVRect(FMath::RoundToFloat(Row.Right() - TagW - 3.f), FMath::RoundToFloat(Row.Center().Y - TagH * 0.5f), TagW, TagH);
					D.Rect(TagR, Edge);
					D.Text(Tag, FVector2D(TagR.Center().X, TagR.Y + 1.f), TagS, FVPal::White(), EFVAlign::Center, false, true);
				}
				RY += RowHs[i] + RowGap;
			}
			return H;
		}

		void DrawSeasonInfo(FVDraw& D, const FFVRect& Area, bool bP)
		{
			const UFVOnline* On = Online();
			if (!On) { return; }
			const FFVSeasonInfo& S = On->GetSeason();
			const double T = Game->GetTime();
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float BS = bP ? FV::Ui::Body() : 8.f;
			const float HS = bP ? FV::Ui::Header() : 10.f;
			if (!S.bKnown)
			{
				if (On->GetStatus() == EFVOnlineStatus::Connecting)
				{
					Spinner(D, FVector2D(Area.Center().X, Area.Center().Y - 12.f), bP ? 12.f : 9.f, T);
					D.Text(TEXT("Узнаём, как идёт турнир…"), FVector2D(Area.Center().X, Area.Center().Y + 6.f), BS, FVPal::InkSoft(), EFVAlign::Center, false, true);
				}
				else
				{
					DrawMessage(D, Area, bP, EFVIcon::Clock, TEXT("Сведения о турнире появятся, как только игра свяжется с сервером."), false);
				}
				return;
			}
			const UFVSaveGame* P = Game->GetProfile();
			const int32 Level = FMath::Clamp(P ? P->GetLevel() : 1, 1, FV::MaxLevel);
			const int64 Now = On->GetServerNow();
			const int64 Left = S.EndsUnix - Now;

			InfoScroll.Update(D, Area, InfoContentH);
			D.PushClip(Area);
			const float Pad = bP ? 10.f : 8.f;
			const float X = Area.X + Pad;
			const float W = Area.W - Pad * 2.f - 6.f;
			float Y = Area.Y + (bP ? 8.f : 6.f) - InfoScroll.Offset();
			const float Y0 = Y;

			// ---- «Турнир №N» + countdown
			{
				const float H = bP ? 100.f : 58.f;
				const FFVRect R(X, Y, W, H);
				Card(D, R, true);
				const float IS = bP ? 32.f : 22.f;
				IconAt(D, EFVIcon::Trophy, FFVRect(R.X + 8.f, R.Y + 8.f, IS, IS), 2.f);
				const float TX = R.X + 8.f + IS + 8.f;
				D.Text(FString::Printf(TEXT("Турнир №%d"), S.Id), FVector2D(TX, R.Y + 6.f), HS, FVPal::Ink(), EFVAlign::Left, true, true);
				const FString Sub = Left > 0 ? FString(TEXT("Новый турнир — каждые 3 дня")) : FString(TEXT("Турнир завершён — подводим итоги"));
				const FString Part = FString::Printf(TEXT("%s %s"), *FV::Money(S.Participants), FVUI::Plural(S.Participants, TEXT("участник"), TEXT("участника"), TEXT("участников")));
				const FString CT = Left > 0 ? FString::Printf(TEXT("до конца %s"), *Countdown(Left)) : FString(TEXT("время вышло"));
				const FString Finish = LocalStamp(S.EndsUnix);
				const int64 Dur = FMath::Max<int64>(1, S.EndsUnix - S.StartsUnix);
				const float Frac = FMath::Clamp(float(Now - S.StartsUnix) / float(Dur), 0.f, 1.f);
				if (bP)
				{
					D.Text(Part, FVector2D(R.Right() - 8.f, R.Y + 9.f), SS, FVPal::InkSoft(), EFVAlign::Right, false, true);
					D.Text(D.Ellipsize(Sub, R.Right() - 8.f - TX, SS, false), FVector2D(TX, R.Y + 6.f + D.LineHeight(HS)), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
					const float CYt = R.Y + 6.f + D.LineHeight(HS) + D.LineHeight(SS) + 6.f;
					const float CS = FV::Ui::Title();
					IconAt(D, EFVIcon::Clock, FFVRect(TX, CYt + 2.f, 16.f, 16.f), 1.f);
					D.Text(CT, FVector2D(TX + 20.f, CYt), CS, FV::Col(120, 72, 20), EFVAlign::Left, false, true);
					const float BarY = R.Bottom() - 24.f;
					D.Bar(FFVRect(R.X + 8.f, BarY, R.W - 16.f, 6.f), Frac, FVPal::Gold(), FV::Col(120, 84, 50), false);
					const FString StartT = LocalStamp(S.StartsUnix);
					if (!StartT.IsEmpty()) { D.Text(FString::Printf(TEXT("старт %s"), *StartT), FVector2D(R.X + 8.f, BarY + 9.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false); }
					if (!Finish.IsEmpty()) { D.Text(FString::Printf(TEXT("финиш %s"), *Finish), FVector2D(R.Right() - 8.f, BarY + 9.f), SS, FVPal::InkSoft(), EFVAlign::Right, false, true); }
				}
				else
				{
					const float CS = 11.f;
					const float CW = D.MeasureText(CT, CS, true).X;
					D.Text(CT, FVector2D(R.Right() - 8.f, R.Y + 6.f), CS, FV::Col(120, 72, 20), EFVAlign::Right, false, true);
					IconAt(D, EFVIcon::Clock, FFVRect(R.Right() - 8.f - CW - 15.f, R.Y + 7.f, 12.f, 12.f), 1.f);
					const FString Info2 = Finish.IsEmpty() ? Part : FString::Printf(TEXT("%s · финиш %s"), *Part, *Finish);
					const float I2W = D.MeasureText(Info2, SS, false).X;
					D.Text(Info2, FVector2D(R.Right() - 8.f, R.Y + 8.f + D.LineHeight(CS)), SS, FVPal::InkSoft(), EFVAlign::Right, false, false);
					const float SubW = FMath::Max(40.f, R.Right() - 8.f - FMath::Max(CW + 20.f, I2W) - 10.f - TX);
					D.Text(D.Ellipsize(Sub, SubW, SS, false), FVector2D(TX, R.Y + 6.f + D.LineHeight(HS)), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
					D.Bar(FFVRect(R.X + 8.f, R.Bottom() - 10.f, R.W - 16.f, 4.f), Frac, FVPal::Gold(), FV::Col(120, 84, 50), false);
				}
				Y += H + (bP ? 10.f : 7.f);
			}

			// ---- «each category is rewarded separately» + the projection
			{
				int32 NumTiers = 0;
				bool bAny = false;
				for (ECat C : GSeasonCats)
				{
					int32 Rank = 0, Tier = 0;
					MyPlace(S, C, Rank, Tier);
					bAny |= Rank > 0;
					NumTiers += Tier > 0 ? 1 : 0;
				}
				const FString L1 = TEXT("Каждая категория награждается отдельно");
				FString L2;
				if (Left <= 0) { L2 = TEXT("Итоги подводятся сами — все награды придут одним письмом в «Почту»."); }
				else if (NumTiers > 0)
				{
					L2 = FString::Printf(TEXT("За турнир можно получить до 4 наград. Если финиш будет сейчас, вы получите %d %s из 4."),
						NumTiers, FVUI::Plural(NumTiers, TEXT("награду"), TEXT("награды"), TEXT("наград")));
				}
				else if (bAny) { L2 = TEXT("За турнир можно получить до 4 наград. Пока вы вне призовых мест — войдите в топ 75% хотя бы в одной категории!"); }
				else { L2 = TEXT("За турнир можно получить до 4 наград. Поймайте рыбу во время турнира — и вы появитесь в рейтинге."); }
				const float IS = bP ? 24.f : 16.f;
				const float TX = X + 8.f + IS + 8.f;
				const float TW = X + W - 8.f - TX;
				const float L2H = D.MeasureWrapped(L2, TW, SS, false);
				const float H = FMath::CeilToFloat(FMath::Max(IS + 12.f, (bP ? 14.f : 9.f) + D.LineHeight(BS) + 1.f + L2H));
				const FFVRect R(X, Y, W, H);
				const FLinearColor Acc = NumTiers > 0 ? TierColor(1) : FV::Col(186, 124, 66);
				Card(D, R, NumTiers > 0, &Acc);
				IconAt(D, EFVIcon::Treasure, FFVRect(X + 8.f, FMath::RoundToFloat(R.Center().Y - IS * 0.5f), IS, IS), 2.f);
				const float TY = R.Y + (bP ? 7.f : 4.f);
				D.Text(D.Ellipsize(L1, TW, BS, true), FVector2D(TX, TY), BS, FVPal::Ink(), EFVAlign::Left, false, true);
				D.TextWrapped(L2, FVector2D(TX, TY + D.LineHeight(BS) + 1.f), TW, SS, NumTiers > 0 ? FV::Col(110, 64, 10) : FVPal::InkSoft(), false);
				Y += H + (bP ? 14.f : 9.f);
			}

			// ---- one card per category
			{
				Y = FVUI::SectionHeader(D, FString::Printf(TEXT("Места и награды · ваш уровень %d"), Level), FVector2D(X, Y), W, bP ? 11.5f : 9.f);
				const float Gap = bP ? 10.f : 6.f;
				if (bP)
				{
					for (ECat C : GSeasonCats)
					{
						Y += SeasonCatCard(D, C, S, Level, X, Y, W, true, true) + Gap;
					}
				}
				else
				{
					const float CW = FMath::FloorToFloat((W - Gap) * 0.5f);
					for (int32 Row = 0; Row < 2; ++Row)
					{
						const ECat A = GSeasonCats[Row * 2], B = GSeasonCats[Row * 2 + 1];
						const float RH = FMath::Max(SeasonCatCard(D, A, S, Level, X, Y, CW, false, false), SeasonCatCard(D, B, S, Level, X + CW + Gap, Y, W - CW - Gap, false, false));
						SeasonCatCard(D, A, S, Level, X, Y, CW, false, true, RH);
						SeasonCatCard(D, B, S, Level, X + CW + Gap, Y, W - CW - Gap, false, true, RH);
						Y += RH + Gap;
					}
				}
				Y += bP ? 2.f : 2.f;
			}

			// ---- how it works
			{
				const FString Note = TEXT("Турниры идут без перерыва: каждый длится 3 дня, а следующий начинается сразу после финиша. "
					"В каждой категории места считаются среди рыбаков с результатом: топ 25% — золото, топ 50% — серебро, топ 75% — бронза. "
					"Итоги подводятся автоматически, а награды приходят письмом в «Почту». Чем выше ваш уровень, тем щедрее награды.");
				const float H = D.TextWrapped(Note, FVector2D(X, Y), W, SS, FVPal::InkSoft(), false);
				Y += H + 8.f;
			}
			InfoContentH = Y - Y0 + (bP ? 12.f : 8.f);
			D.PopClip();
			InfoScroll.DrawFades(D, Area, ListBg());
			InfoScroll.DrawBar(D, Area);
		}

		// ------------------------------------------------------------------ layouts
		void DrawBanLine(FVDraw& D, float X, float Y, float W, float Size)
		{
			const UFVOnline* On = Online();
			if (!On || On->GetStatus() != EFVOnlineStatus::Banned) { return; }
			const FString R = On->GetBanReason();
			const FString T = R.IsEmpty() ? FString(TEXT("Ваш аккаунт заблокирован — вас нет в таблице")) : FString::Printf(TEXT("Аккаунт заблокирован: %s"), *R);
			D.Text(D.Ellipsize(T, W, Size, true), FVector2D(X, Y), Size, InkBad(), EFVAlign::Left, false, true);
		}

		void DrawLandscape(FVDraw& D, const FFVRect& Win)
		{
			const float X = Win.X + 12.f;
			const float W = Win.W - 24.f;
			const float Y = Win.Y + 14.f;
			DrawPeriodSwitch(D, FFVRect(X, Y, 224.f, 18.f), false);
			const float SearchW = 170.f;
			DrawSearchRow(D, FFVRect(X + W - SearchW, Y, SearchW, 18.f), false);
			const FString Line = InfoLine();
			const float MidX = X + 232.f;
			const float MidW = (X + W - SearchW - 8.f) - MidX;
			if (!Line.IsEmpty())
			{
				D.Text(D.Ellipsize(Line, MidW, 7.f, true), FVector2D(MidX + MidW * 0.5f, Y + 4.f), 7.f, bSeason ? FV::Col(120, 72, 20) : FVPal::InkSoft(), EFVAlign::Center, false, true);
			}
			DrawBanLine(D, MidX, Y + 12.f, MidW, 6.5f);

			const FFVRect Panel(Win.X + 10.f, Y + 40.f, Win.W - 20.f, Win.Bottom() - 10.f - (Y + 40.f));
			D.Panel(Panel, EFVPanel::Parchment);
			if (ServerUsable())
			{
				TArray<FString> Labels;
				const int32 N = bSeason ? NumSeasonPages : NumAllPages;
				for (int32 i = 0; i < N; ++i)
				{
					ECat C;
					Labels.Add(PageCat(bSeason, i, C) ? FString(InfoOf(C).Tab) : FString(TEXT("Турнир")));
				}
				FVKit::TabStrip(D, FFVRect(Panel.X + 4.f, Panel.Y - 15.f, Panel.W - 8.f, 16.f), Labels, CurPager());
			}
			DrawPagesArea(D, Panel.Inset(4.f), false);
		}

		void DrawPortrait(FVDraw& D, const FFVRect& Win)
		{
			const float X = Win.X + 10.f;
			const float W = Win.W - 20.f;
			float Y = Win.Y + 16.f;
			DrawPeriodSwitch(D, FFVRect(X, Y, W, 32.f), true);
			Y += 38.f;
			DrawSearchRow(D, FFVRect(X, Y, W, 36.f), true);
			Y += 40.f;
			const FString Line = InfoLine();
			const float SS = FV::Ui::Small();
			if (!Line.IsEmpty()) { D.Text(D.Ellipsize(Line, W, SS + 0.5f, true), FVector2D(X + W * 0.5f, Y), SS + 0.5f, bSeason ? FV::Col(120, 72, 20) : FVPal::InkSoft(), EFVAlign::Center, false, true); }
			Y += D.LineHeight(SS + 0.5f) + 2.f;
			const UFVOnline* On = Online();
			if (On && On->GetStatus() == EFVOnlineStatus::Banned)
			{
				DrawBanLine(D, X, Y, W, SS);
				Y += D.LineHeight(SS) + 2.f;
			}
			if (ServerUsable())
			{
				IconTabs(D, FFVRect(X, Y, W, 38.f), CurPager());
				Y += 38.f + 8.f;
			}
			const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;
			const FFVRect Area(Win.X + 4.f, Y, Win.W - 8.f, FMath::Max(60.f, Bottom - Y));
			DrawPagesArea(D, Area, true);

			FFVRect Actions;
			if (D.ActionBar(Win, &Actions)) { Act = EAct::Back; }
			ECat C;
			const bool bCatPage = PageCat(bSeason, CurPager().Page, C);
			const bool bBusy = bCatPage && States.IsValidIndex(int32(C)) && States[int32(C)].bLoading;
			if (D.Button(Actions, bBusy ? FString(TEXT("Обновляем…")) : FString(TEXT("Обновить")), EFVButton::Normal, ServerUsable() && !bBusy, FV::Ui::Body() + 0.5f))
			{
				Act = EAct::Refresh;
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeLeaderboardScreen(UFVGame* Game)
{
	return MakeUnique<FVLeaderboardScreenImpl::FLeaderboardScreen>(Game);
}
