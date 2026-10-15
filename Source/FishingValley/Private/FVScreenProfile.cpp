// «Профиль рыбака» (update 4, Docs/UPDATE4_ONLINE.md §8).
// Context (UFVGame::GetScreenContext):
//   None    — the local player's own profile (built from the save, works offline; server ranks are added when online)
//   "setup" — the friendly first-run page «Как вас зовут, рыбак?» (saves the name locally at once, then UFVOnline::SetName)
//   "<pid>" — another player's profile fetched from the server (loading / error states; «Назад» returns to the rating)
// Own profile: edit name / status (FVKit::TextField + «Сохранить»), «Рейтинг», «Почта» (unread badge), the player number.
// Landscape: identity column on the left (stage with the character + pet, name, #tag, status, level), sections on the
// right in a smooth scroll view. Portrait: one smooth scrolling column, «Назад» + «Рейтинг» / «Почта» in the footer.
// Opens the leaderboard / mail with the context "profile" (their «Назад» comes back here).
// Offline mode (update 4b, §10): the own profile keeps working from the save (a strip offers «Подключиться»); another
// player's profile shows the friendly «Офлайн-режим — онлайн недоступен» card and loads by itself once reconnected.

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "FVTextInput.h"
#include "FVOnline.h"
#include "InputCoreTypes.h"

namespace FVProfileScreenImpl
{
	// =================================================================================================================
	// small helpers
	// =================================================================================================================
	FLinearColor InkGood() { return FV::Col(52, 116, 34); }
	FLinearColor InkBad() { return FV::Col(170, 44, 34); }
	FLinearColor MedalColor(int32 Rank)
	{
		return Rank == 1 ? FV::Col(255, 204, 51) : (Rank == 2 ? FV::Col(196, 204, 218) : FV::Col(214, 134, 62));
	}

	FString DateText(int64 Unix)
	{
		if (Unix <= 0) { return FString(); }
		const FDateTime T = FDateTime::FromUnixTimestamp(Unix) + (FDateTime::Now() - FDateTime::UtcNow());
		return FString::Printf(TEXT("%02d.%02d.%04d"), T.GetDay(), T.GetMonth(), T.GetYear());
	}

	FString PlayTimeText(int64 Seconds)
	{
		const int64 Min = FMath::Max<int64>(0, Seconds) / 60;
		if (Min < 1) { return TEXT("меньше минуты"); }
		const int64 Days = Min / 1440, Hours = (Min % 1440) / 60, Mins = Min % 60;
		if (Days > 0) { return FString::Printf(TEXT("%lld д %lld ч"), (long long)Days, (long long)Hours); }
		if (Hours > 0) { return FString::Printf(TEXT("%lld ч %lld мин"), (long long)Hours, (long long)Mins); }
		return FString::Printf(TEXT("%lld мин"), (long long)Mins);
	}

	FString LastSeenText(int64 Unix, int64 Now)
	{
		if (Unix <= 0) { return FString(); }
		const int64 Ago = FMath::Max<int64>(0, Now - Unix);
		if (Ago < 600) { return TEXT("Сейчас в игре"); }
		if (Ago < 3600) { return FString::Printf(TEXT("Заходил(а) %lld мин назад"), (long long)(Ago / 60)); }
		if (Ago < 86400) { return FString::Printf(TEXT("Заходил(а) %lld ч назад"), (long long)(Ago / 3600)); }
		const int64 Days = Ago / 86400;
		if (Days < 30) { return FString::Printf(TEXT("Заходил(а) %lld %s назад"), (long long)Days, FVUI::Plural(Days, TEXT("день"), TEXT("дня"), TEXT("дней"))); }
		return FString::Printf(TEXT("Заходил(а) %s"), *DateText(Unix));
	}

	bool IsNameLetter(TCHAR C)
	{
		if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z')) { return true; }
		if (C >= 0x00C0 && C < 0x2000 && C != 0x00D7 && C != 0x00F7) { return true; }   // Latin ext., Greek, Cyrillic, ...
		if (C >= 0x3040 && C < 0xA000) { return true; }                                   // kana, CJK
		return C >= 0xAC00 && C < 0xD7A4;                                                 // Hangul
	}

	FString CollapseSpaces(const FString& In)
	{
		FString Out;
		bool bSpace = false;
		for (TCHAR C : In.TrimStartAndEnd())
		{
			if (FChar::IsWhitespace(C))
			{
				if (!bSpace) { Out.AppendChar(TEXT(' ')); }
				bSpace = true;
				continue;
			}
			bSpace = false;
			Out.AppendChar(C);
		}
		return Out;
	}

	/** Device-side name check (the server additionally filters bad words). Returns "" when OK, else a Russian error. */
	FString CheckName(const FString& In, FString& Out)
	{
		Out = CollapseSpaces(In);
		if (Out.IsEmpty()) { return TEXT("Введите имя — хотя бы одну букву"); }
		if (Out.Len() > 16) { return TEXT("Имя не длиннее 16 символов"); }
		for (TCHAR C : Out)
		{
			if (!(IsNameLetter(C) || FChar::IsDigit(C) || C == TEXT(' ') || C == TEXT('_') || C == TEXT('-')))
			{
				return TEXT("Можно буквы, цифры, пробел, «_» и «-»");
			}
		}
		return FString();
	}

	const TCHAR* GRandomNames[] = {
		TEXT("Щукарь"), TEXT("Дед Пескарь"), TEXT("Карасик"), TEXT("Лещ Иваныч"), TEXT("Окунёк"), TEXT("Сом Сомыч"),
		TEXT("Ёршик"), TEXT("Тихий Омут"), TEXT("Клёвый Лёва"), TEXT("Удильщик"), TEXT("Плотвичка"), TEXT("Налим"),
		TEXT("Рыбачка Соня"), TEXT("Капитан Поплавок"), TEXT("Сазанчик"), TEXT("Хозяин Заводи")
	};

	/** Words split into at most MaxLines lines of MaxW (the last one ellipsized). */
	TArray<FString> WrapLines(const FVDraw& D, const FString& Text, float MaxW, float Size, int32 MaxLines, bool bBold = false)
	{
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		TArray<FString> Lines;
		FString Cur;
		for (int32 i = 0; i < Words.Num(); ++i)
		{
			const FString Try = Cur.IsEmpty() ? Words[i] : Cur + TEXT(" ") + Words[i];
			if (Cur.IsEmpty() || D.MeasureText(Try, Size, bBold).X <= MaxW)
			{
				Cur = Try;
				continue;
			}
			if (Lines.Num() >= MaxLines - 1)
			{
				FString Rest = Cur;
				for (int32 j = i; j < Words.Num(); ++j) { Rest += TEXT(" ") + Words[j]; }
				Lines.Add(D.Ellipsize(Rest, MaxW, Size, bBold));
				return Lines;
			}
			Lines.Add(Cur);
			Cur = Words[i];
		}
		if (!Cur.IsEmpty()) { Lines.Add(D.Ellipsize(Cur, MaxW, Size, bBold)); }
		return Lines;
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

	void Card(FVDraw& D, const FFVRect& R, bool bHighlight = false)
	{
		D.Rect(R.Offset(0.f, 1.f), FV::Col(120, 70, 36, 80));
		D.Rect(R, bHighlight ? FV::Col(200, 146, 40) : FV::Col(186, 124, 66));
		D.Rect(R.Inset(1.f), bHighlight ? FV::Col(255, 238, 170) : FV::Col(255, 238, 206));
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Alpha(FVPal::White(), 0.55f));
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

	FString SizeText(float Cm)
	{
		return FString::Printf(TEXT("%s см"), *FVUI::Num(Cm, 1));
	}

	/** Width of the quality stars D.Stars draws (0 for Normal). */
	float StarsWidth(EFVQuality Q, float Scale)
	{
		return Q == EFVQuality::Normal ? 0.f : 9.f * Scale;
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

	FFVAppearance LookFromIds(FName HatId, FName OutfitId)
	{
		FFVAppearance A;
		const FFVCosmeticDef* Hat = FVDB::FindCosmetic(HatId);
		if (Hat && Hat->Slot == EFVCosmeticSlot::Hat)
		{
			A.Hat = EFVHatStyle(FMath::Min<uint8>(Hat->Style, uint8(EFVHatStyle::Count) - 1));
			A.HatPrimary = Hat->Primary;
			A.HatSecondary = Hat->Secondary;
		}
		const FFVCosmeticDef* Outfit = FVDB::FindCosmetic(OutfitId);
		if (Outfit && Outfit->Slot == EFVCosmeticSlot::Outfit)
		{
			A.Outfit = EFVOutfitStyle(FMath::Min<uint8>(Outfit->Style, uint8(EFVOutfitStyle::Count) - 1));
			A.OutfitPrimary = Outfit->Primary;
			A.OutfitSecondary = Outfit->Secondary;
		}
		return A;
	}

	EFVPetStyle PetFromId(FName PetId)
	{
		const FFVCosmeticDef* C = FVDB::FindCosmetic(PetId);
		if (C && C->Slot == EFVCosmeticSlot::Pet) { return EFVPetStyle(FMath::Min<uint8>(C->Style, uint8(EFVPetStyle::Count) - 1)); }
		return EFVPetStyle::None;
	}

	/** Own profile when UFVOnline is not available (never expected in the game, but keeps the screen robust). */
	FFVPublicProfile LocalFallback(const UFVSaveGame& P)
	{
		FFVPublicProfile R;
		R.bOk = true;
		R.bLocal = true;
		R.PlayerId = P.OnlineId;
		R.Name = P.PlayerName;
		R.Tag = P.OnlineTag;
		R.Status = P.PlayerStatus;
		R.Level = P.GetLevel();
		R.Xp = P.Xp;
		R.FishTotal = P.Stats.FishCaught;
		R.Species = P.SpeciesCaught();
		R.Legendary = P.Stats.Legendary;
		R.Achievements = P.CompletedAchievements.Num();
		R.CoinsEarned = P.Stats.Earned;
		R.Perfect = P.Stats.Perfect;
		R.PlaySeconds = int32(P.Stats.PlaySeconds);
		for (const TPair<FName, int32>& S : P.StationLevels) { if (S.Value > 0) { ++R.StationsBuilt; } }
		int32 Best = 0;
		for (const TPair<FName, int32>& L : P.CatchesByLocation)
		{
			if (L.Value > Best) { Best = L.Value; R.FavoriteLocation = L.Key; }
		}
		R.BestCatch.FishId = P.BestCatch.FishId;
		R.BestCatch.SizeCm = P.BestCatch.SizeCm;
		R.BestCatch.Quality = FVUI::QualityOf(P.BestCatch.Quality);
		for (int32 i = 0; i < P.Trophies.Num() && R.Showcase.Num() < 3; ++i)
		{
			FFVFishShow& S = R.Showcase.AddDefaulted_GetRef();
			S.FishId = P.Trophies[i].FishId;
			S.SizeCm = P.Trophies[i].SizeCm;
			S.Quality = FVUI::QualityOf(P.Trophies[i].Quality);
		}
		R.Hat = P.EquippedHat;
		R.Outfit = P.EquippedOutfit;
		R.Bobber = P.EquippedBobber;
		R.Pet = P.EquippedPet;
		R.StartUnix = P.ProfileStartUnix;
		return R;
	}

	// =================================================================================================================
	// the stage: a little pier scene with the character and the pet
	// =================================================================================================================
	void Cloud(FVDraw& D, float X, float Y, float K)
	{
		const FLinearColor C = FV::Col(255, 255, 255, 220);
		D.Rect(FFVRect(FMath::RoundToFloat(X), FMath::RoundToFloat(Y + 3.f * K), FMath::RoundToFloat(26.f * K), FMath::RoundToFloat(5.f * K)), C);
		D.Rect(FFVRect(FMath::RoundToFloat(X + 5.f * K), FMath::RoundToFloat(Y), FMath::RoundToFloat(12.f * K), FMath::RoundToFloat(4.f * K)), C);
		D.Rect(FFVRect(FMath::RoundToFloat(X + 13.f * K), FMath::RoundToFloat(Y - 2.f * K), FMath::RoundToFloat(8.f * K), FMath::RoundToFloat(4.f * K)), C);
	}

	void DrawStage(FVDraw& D, const FFVRect& R, const FFVPublicProfile* Prof, float CharScale, double T, bool bLoading)
	{
		D.PushClip(R);
		const float K = CharScale / 2.f;
		const float DeckY = FMath::RoundToFloat(R.Bottom() - 20.f * K);    // the character's feet stand on the pier here
		const float WaterY = DeckY - FMath::RoundToFloat(3.f * K);
		D.GradientV(FFVRect(R.X, R.Y, R.W, WaterY - R.Y), FV::Col(118, 190, 238), FV::Col(214, 238, 250), 12);
		const FVector2D Sun(R.Right() - 26.f * K, R.Y + 20.f * K);
		D.Glow(Sun, 24.f * K, FV::Col(255, 236, 170, 110));
		D.Circle(Sun, 8.f * K, FV::Col(255, 238, 150));
		for (int32 c = 0; c < 3; ++c)
		{
			const float Span = R.W + 70.f * K;
			const float CX = R.X - 35.f * K + FMath::Fmod(float(c) * 97.f * K + float(T) * (3.f + c), Span);
			Cloud(D, CX, R.Y + (10.f + 12.f * c) * K, K * (c == 1 ? 0.8f : 1.f));
		}
		// far hills + near bushes
		for (int32 i = 0; i < 7; ++i)
		{
			D.Circle(FVector2D(R.X + (float(i) * 52.f - 10.f) * K, WaterY + 10.f * K), (30.f + float(i % 3) * 8.f) * K, FV::Col(134, 182, 104));
		}
		for (int32 i = 0; i < 9; ++i)
		{
			D.Circle(FVector2D(R.X + (float(i) * 38.f + 14.f) * K, WaterY + 4.f * K), (12.f + float((i * 7) % 4) * 3.f) * K, FV::Col(92, 150, 74));
		}
		// water
		D.GradientV(FFVRect(R.X, WaterY, R.W, R.Bottom() - WaterY), FV::Col(88, 168, 216), FV::Col(40, 98, 168), 8);
		D.Rect(FFVRect(R.X, WaterY, R.W, 1.f), FV::Col(200, 236, 250, 180));
		for (int32 i = 0; i < 6; ++i)
		{
			const float RY = WaterY + (6.f + float(i % 3) * 7.f) * K;
			const float RX = R.X + FMath::Fmod(float(i) * 61.f * K + float(T) * 6.f, R.W + 20.f) - 10.f;
			D.Rect(FFVRect(FMath::RoundToFloat(RX), FMath::RoundToFloat(RY), FMath::RoundToFloat(10.f * K), 1.f), FV::Col(210, 240, 255, 140));
		}
		// pier
		const float DeckR = FMath::RoundToFloat(R.X + R.W * 0.64f);
		for (float PX = R.X + 8.f * K; PX < DeckR - 2.f; PX += 26.f * K)
		{
			D.Rect(FFVRect(FMath::RoundToFloat(PX), DeckY, FMath::RoundToFloat(4.f * K), R.Bottom() - DeckY), FV::Col(104, 60, 30));
		}
		D.Rect(FFVRect(R.X, DeckY, DeckR - R.X, FMath::RoundToFloat(6.f * K)), FV::Col(176, 110, 56));
		D.Rect(FFVRect(R.X, DeckY, DeckR - R.X, 1.f), FV::Col(222, 160, 96));
		D.Rect(FFVRect(R.X, DeckY + FMath::RoundToFloat(6.f * K), DeckR - R.X, FMath::RoundToFloat(2.f * K)), FV::Col(110, 62, 30));
		for (float PX = R.X + 10.f * K; PX < DeckR; PX += 18.f * K)
		{
			D.Rect(FFVRect(FMath::RoundToFloat(PX), DeckY + 1.f, 1.f, FMath::RoundToFloat(5.f * K)), FV::Col(140, 84, 40));
		}

		UFVSprites* Sp = UFVSprites::Get();
		const FVector2D Feet(FMath::RoundToFloat(R.X + R.W * 0.34f), DeckY + 1.f);
		if (bLoading || !Prof)
		{
			D.Rect(FFVRect(Feet.X - 14.f * K, Feet.Y - 3.f, 28.f * K, 3.f), FV::Col(40, 20, 10, 60));
			if (bLoading) { Spinner(D, FVector2D(Feet.X, Feet.Y - 40.f * K), 9.f * K, T); }
		}
		else if (Sp)
		{
			D.Rect(FFVRect(FMath::RoundToFloat(Feet.X - 12.f * K), Feet.Y - 2.f, FMath::RoundToFloat(24.f * K), 3.f), FV::Col(50, 24, 10, 70));
			const FFVAppearance Look = LookFromIds(Prof->Hat, Prof->Outfit);
			const int32 Frames = FMath::Max(1, UFVSprites::CharacterFrameCount(EFVCharAnim::Idle));
			const FFVSprite Spr = Sp->Character(Look, EFVCharAnim::Idle, EFVFacing::Down, int32(T * 2.0) % Frames);
			D.Sprite(Spr, Feet - UFVSprites::CharacterAnchor() * CharScale, CharScale);
			const EFVPetStyle Pet = PetFromId(Prof->Pet);
			if (Pet != EFVPetStyle::None && Pet < EFVPetStyle::Count)
			{
				const EFVPetAnim Anim = FMath::Fmod(T, 7.0) > 2.5 ? EFVPetAnim::Sit : EFVPetAnim::Idle;
				const int32 PF = FMath::Max(1, UFVSprites::PetFrameCount(Anim));
				const FFVSprite PetSpr = Sp->Pet(Pet, Anim, int32(T * 2.5) % PF);
				const FVector2D PetFeet(FMath::RoundToFloat(Feet.X + 22.f * CharScale), Feet.Y);
				D.Rect(FFVRect(FMath::RoundToFloat(PetFeet.X - 8.f * K), PetFeet.Y - 2.f, FMath::RoundToFloat(16.f * K), 2.f), FV::Col(50, 24, 10, 60));
				D.Sprite(PetSpr, FVector2D(FMath::RoundToFloat(PetFeet.X - PetSpr.W * CharScale * 0.5f), PetFeet.Y - PetSpr.H * CharScale), CharScale, FLinearColor::White, true);
			}
		}
		D.PopClip();
		D.Frame(R, FVPal::WoodDark(), 2.f);
		D.Frame(R.Inset(2.f), FV::Col(214, 150, 88, 120), 1.f);
	}

	void LevelBadge(FVDraw& D, const FVector2D& C, float R, int32 Level, float Size)
	{
		D.Circle(C + FVector2D(1.f, 1.f), R, FV::Col(60, 30, 10, 90));
		D.Circle(C, R, FV::Col(122, 62, 22));
		D.Circle(C, R - 1.5f, FV::Col(255, 210, 80));
		D.Circle(C, R - 3.f, FV::Col(232, 150, 40));
		const FString L = FString::FromInt(Level);
		D.TextOutlined(L, FVector2D(C.X, FMath::RoundToFloat(C.Y - D.LineHeight(Size) * 0.5f)), Size, FVPal::White(), EFVAlign::Center, FV::Col(90, 40, 10));
	}

	// =================================================================================================================
	// the screen
	// =================================================================================================================
	enum class EMode : uint8 { Own, Other, Setup };
	enum class ELoad : uint8 { Loading, Ready, Failed };
	enum class EAct : uint8 { None, Close, Back, Board, Mail, Edit, Save, Cancel, Retry, Start, RandomName };

	class FProfileScreen final : public FVScreen
	{
	public:
		explicit FProfileScreen(UFVGame* InGame) : FVScreen(InGame), Alive(MakeShared<bool>(true)) {}
		virtual ~FProfileScreen() override { *Alive = false; }

		virtual bool HandlesEscape() const override { return true; }

		virtual void OnOpen() override
		{
			TickCount = 0;
			*Alive = true;
			const FName Ctx = Game ? Game->GetScreenContext() : NAME_None;
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Mode = EMode::Own;
			bFromBoard = false;
			OtherId = 0;
			if (Ctx == FName(TEXT("setup")))
			{
				Mode = EMode::Setup;
				SetupField = FVKit::FFVTextField();
				SetupField.MaxLen = 16;
				SetupField.Text = P ? P->PlayerName : FString();
			}
			else if (!Ctx.IsNone())
			{
				OtherId = FCString::Atoi64(*Ctx.ToString());
				bFromBoard = true;
				if (OtherId > 0 && !(P && P->OnlineId == OtherId)) { Mode = EMode::Other; }
			}
			Scroll.Reset();
			ContentH = 0.f;
			bEditing = false;
			NameField = FVKit::FFVTextField();
			NameField.MaxLen = 16;
			StatusField = FVKit::FFVTextField();
			StatusField.MaxLen = 40;
			bRetried = false;
			PrevOffline = OfflineOf(Online());
			if (Mode == EMode::Other) { StartFetch(); }
			else
			{
				RefreshLocal();
				FetchOwnRanks();
			}
		}

		virtual void OnClose() override
		{
			*Alive = false;
			EndFields();
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (In.WasPressed(EKeys::Escape))
			{
				if (AnyFieldFocused()) { EndFields(); return; }
				if (Mode == EMode::Setup) { return; }   // the name is required: stay on the page
				if (bEditing) { bEditing = false; FVUI::Sfx(EFVSfx::Close, 0.6f); return; }
				PendingAct = bFromBoard ? EAct::Back : EAct::Close;
				return;
			}
			if (FVTextInput::IsActive() || Mode == EMode::Setup || bEditing) { return; }
			if (In.WasPressed(EKeys::PageDown)) { Scroll.ScrollTo(Scroll.Target + Scroll.View * 0.85f); }
			if (In.WasPressed(EKeys::PageUp)) { Scroll.ScrollTo(Scroll.Target - Scroll.View * 0.85f); }
			if (In.WasPressed(EKeys::Down)) { Scroll.ScrollTo(Scroll.Target + 40.f); }
			if (In.WasPressed(EKeys::Up)) { Scroll.ScrollTo(Scroll.Target - 40.f); }
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bP = FV::IsPortrait();
			Act = PendingAct;
			PendingAct = EAct::None;
			if (Mode == EMode::Setup)
			{
				DrawSetup(D, bP);
				RunAct();
				return;
			}
			if (Mode == EMode::Own && Game->GetTime() >= NextLocalRefresh) { RefreshLocal(); }
			const EOffline Why = OfflineOf(Online());
			if (Why == EOffline::None)
			{
				bRetried = false;
				if (PrevOffline != EOffline::None)
				{
					// back online (e.g. after «Подключиться»): load what was missing
					if (Mode == EMode::Other && Load != ELoad::Ready) { StartFetch(); }
					else if (Mode == EMode::Own) { FetchOwnRanks(); }
				}
			}
			PrevOffline = Why;

			const FFVRect Win = bP ? FV::WindowRect() : FFVRect(10.f, 30.f, 620.f, 318.f);
			const bool bPrevBlocked = D.IsInputBlocked();
			if (bEditing) { D.SetInputBlocked(true); }
			if (D.Window(Win, Mode == EMode::Own ? FString(TEXT("Мой профиль")) : FString(TEXT("Профиль рыбака")))) { Act = EAct::Close; }
			if (bP) { DrawPortrait(D, Win); }
			else { DrawLandscape(D, Win); }
			D.SetInputBlocked(bPrevBlocked);
			if (bEditing) { DrawEditSheet(D, Win, bP); }
			RunAct();
		}

	private:
		EMode Mode = EMode::Own;
		ELoad Load = ELoad::Ready;
		int64 OtherId = 0;
		bool bFromBoard = false;
		FFVPublicProfile Prof;
		FString LoadError;
		int32 FetchGen = 0;
		TMap<EFVBoardCategory, int32> OwnRanks;
		double NextLocalRefresh = 0.0;
		FFVScrollView Scroll;
		float ContentH = 0.f;
		bool bEditing = false;
		FVKit::FFVTextField NameField;
		FVKit::FFVTextField StatusField;
		FString EditError;
		FVKit::FFVTextField SetupField;
		FString SetupError;
		int32 RandomIdx = -1;
		TSharedRef<bool> Alive;
		int32 TickCount = 0;
		EAct Act = EAct::None;
		EAct PendingAct = EAct::None;
		bool bRetried = false;                   // «Подключиться» was pressed (a failed attempt shows a hint)
		EOffline PrevOffline = EOffline::None;

		// ------------------------------------------------------------------ data
		bool AnyFieldFocused() const { return NameField.bFocused || StatusField.bFocused || SetupField.bFocused; }

		void EndFields()
		{
			FVKit::EndTextField(NameField);
			FVKit::EndTextField(StatusField);
			FVKit::EndTextField(SetupField);
		}

		UFVOnline* Online() const { return Game ? Game->GetOnline() : nullptr; }

		void RefreshLocal()
		{
			NextLocalRefresh = (Game ? Game->GetTime() : 0.0) + 1.0;
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { Prof = FFVPublicProfile(); Load = ELoad::Failed; LoadError = TEXT("Профиль не загружен"); return; }
			UFVOnline* On = Online();
			Prof = On ? On->BuildLocalProfile() : LocalFallback(*P);
			Prof.bOk = true;
			if (Prof.Name.IsEmpty()) { Prof.Name = P->PlayerName; }
			if (Prof.Status.IsEmpty()) { Prof.Status = P->PlayerStatus; }
			if (Prof.Tag <= 0) { Prof.Tag = P->OnlineTag; }
			if (Prof.Ranks.Num() == 0) { Prof.Ranks = OwnRanks; }
			Load = ELoad::Ready;
		}

		/** Own profile: the all-time places come from the server profile (added silently when available). */
		void FetchOwnRanks()
		{
			UFVOnline* On = Online();
			const UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!On || !P || OfflineOf(On) != EOffline::None || !On->IsRegistered() || P->OnlineId <= 0) { return; }
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			TSharedRef<bool> AliveFlag = Alive;
			FProfileScreen* Self = this;
			On->FetchProfile(P->OnlineId, [WeakGame, AliveFlag, Self](const FFVPublicProfile& R)
			{
				if (!WeakGame.IsValid() || !*AliveFlag || !R.bOk) { return; }
				Self->OwnRanks = R.Ranks;
				Self->Prof.Ranks = R.Ranks;
			});
		}

		void StartFetch()
		{
			UFVOnline* On = Online();
			if (!On || OfflineOf(On) != EOffline::None)
			{
				Load = ELoad::Failed;   // the offline card is shown; the profile loads by itself after reconnecting
				LoadError = TEXT("Профили других рыбаков доступны только при связи с сервером.");
				return;
			}
			Load = ELoad::Loading;
			const int32 Gen = ++FetchGen;
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			TSharedRef<bool> AliveFlag = Alive;
			FProfileScreen* Self = this;
			On->FetchProfile(OtherId, [WeakGame, AliveFlag, Self, Gen](const FFVPublicProfile& R)
			{
				if (!WeakGame.IsValid() || !*AliveFlag || Gen != Self->FetchGen) { return; }
				if (R.bOk)
				{
					Self->Prof = R;
					Self->Load = ELoad::Ready;
					Self->Scroll.Reset();
				}
				else
				{
					Self->Load = ELoad::Failed;
					Self->LoadError = R.Error.IsEmpty() ? FString(TEXT("Не удалось загрузить профиль. Проверьте связь и попробуйте ещё раз.")) : R.Error;
				}
			});
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
				EndFields();
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
			case EAct::Back:
			{
				EndFields();
				UFVGame* G = Game;
				if (bFromBoard) { G->OpenScreenWith(EFVScreen::Leaderboard, FName(TEXT("back"))); }
				else { G->CloseScreen(); }
				return;
			}
			case EAct::Board:
			{
				EndFields();
				UFVGame* G = Game;
				G->OpenScreenWith(EFVScreen::Leaderboard, FName(TEXT("profile")));
				return;
			}
			case EAct::Mail:
			{
				EndFields();
				UFVGame* G = Game;
				G->OpenScreenWith(EFVScreen::Mail, FName(TEXT("profile")));
				return;
			}
			case EAct::Edit:
			{
				const UFVSaveGame* P = Game->GetProfile();
				NameField.Text = P ? P->PlayerName : Prof.Name;
				StatusField.Text = P ? P->PlayerStatus : Prof.Status;
				EditError.Reset();
				bEditing = true;
				FVUI::Sfx(EFVSfx::Open, 0.6f);
				return;
			}
			case EAct::Cancel:
				EndFields();
				bEditing = false;
				FVUI::Sfx(EFVSfx::Close, 0.6f);
				return;
			case EAct::Save: DoSave(); return;
			case EAct::Retry: StartFetch(); return;
			case EAct::Start: DoStart(); return;
			case EAct::RandomName:
			{
				const int32 N = UE_ARRAY_COUNT(GRandomNames);
				int32 I = FMath::RandRange(0, N - 1);
				if (I == RandomIdx) { I = (I + 1) % N; }
				RandomIdx = I;
				SetupField.Text = GRandomNames[I];
				if (SetupField.bFocused) { FVTextInput::SetText(SetupField.Text); }
				SetupError.Reset();
				FVUI::Sfx(EFVSfx::Click, 0.8f, 1.1f);
				return;
			}
			default: return;
			}
		}

		/** Toast for a name / status reply that the server rejected (offline failures stay silent: saved locally). */
		static void ReportReply(TWeakObjectPtr<UFVGame> WeakGame, bool bOk, const FString& Msg, const TCHAR* What)
		{
			UFVGame* G = WeakGame.Get();
			if (!G || bOk) { return; }
			UFVOnline* On = G->GetOnline();
			const bool bReached = On && (On->IsOnline() || On->GetStatus() == EFVOnlineStatus::Banned);
			if (!bReached) { return; }
			FVUI::Sfx(EFVSfx::Error);
			G->Toast(FString::Printf(TEXT("%s не принят сервером: %s"), What, Msg.IsEmpty() ? TEXT("попробуйте другой") : *Msg), FV::Col(255, 150, 130), EFVIcon::Exclaim);
		}

		void DoSave()
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			if (!P) { return; }
			FString Name;
			const FString Err = CheckName(NameField.Text, Name);
			if (!Err.IsEmpty())
			{
				EditError = Err;
				FVUI::Sfx(EFVSfx::Error);
				return;
			}
			FString Status = CollapseSpaces(StatusField.Text);
			if (Status.Len() > 40) { Status = Status.Left(40).TrimEnd(); }
			const bool bNameChanged = Name != P->PlayerName;
			const bool bStatusChanged = Status != P->PlayerStatus;
			EndFields();
			bEditing = false;
			if (!bNameChanged && !bStatusChanged)
			{
				FVUI::Sfx(EFVSfx::Close, 0.6f);
				return;
			}
			UFVOnline* On = Online();
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			if (bNameChanged)
			{
				if (On) { On->SetName(Name, [WeakGame](bool bOk, const FString& Msg) { ReportReply(WeakGame, bOk, Msg, TEXT("Имя")); }); }
				if (P->PlayerName != Name) { P->PlayerName = Name; }
			}
			if (bStatusChanged)
			{
				if (On) { On->SetStatus(Status, [WeakGame](bool bOk, const FString& Msg) { ReportReply(WeakGame, bOk, Msg, TEXT("Статус")); }); }
				if (P->PlayerStatus != Status) { P->PlayerStatus = Status; }
			}
			Game->NotifyProfileChanged();
			FVUI::Sfx(EFVSfx::Equip);
			Game->Toast(TEXT("Профиль обновлён"), FVPal::Gold(), EFVIcon::Check);
			RefreshLocal();
		}

		void DoStart()
		{
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			FString Name;
			const FString Err = CheckName(SetupField.Text, Name);
			if (!Err.IsEmpty())
			{
				SetupError = Err;
				FVUI::Sfx(EFVSfx::Error);
				return;
			}
			EndFields();
			UFVOnline* On = Online();
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			if (On) { On->SetName(Name, [WeakGame](bool bOk, const FString& Msg) { ReportReply(WeakGame, bOk, Msg, TEXT("Имя")); }); }
			if (P && P->PlayerName != Name) { P->PlayerName = Name; }
			Game->NotifyProfileChanged();
			if (Game->IsInGame()) { Game->SaveNow(); }
			FVUI::Sfx(EFVSfx::Achievement, 0.8f);
			Game->Toast(FString::Printf(TEXT("Добро пожаловать в долину, %s!"), *Name), FVPal::Gold(), EFVIcon::Fish);
			UFVGame* G = Game;
			G->CloseScreen();
		}

		// ------------------------------------------------------------------ setup page («Как вас зовут, рыбак?»)
		void DrawSetup(FVDraw& D, bool bP)
		{
			const double T = Game->GetTime();
			const FFVRect Win = bP ? FV::WindowRect() : FFVRect(140.f, 26.f, 360.f, 318.f);
			D.Panel(Win, EFVPanel::Wood);
			D.MarkUI(Win);
			const float X = Win.X + (bP ? 14.f : 14.f);
			const float W = Win.W - (bP ? 28.f : 28.f);
			float Y = Win.Y + (bP ? 16.f : 12.f);
			const float HS = bP ? FV::Ui::Header() : 11.f;
			D.Text(TEXT("Добро пожаловать в Клёвую Долину!"), FVector2D(Win.Center().X, Y), HS, FVPal::Ink(), EFVAlign::Center, true, true);
			Y += D.LineHeight(HS) + (bP ? 8.f : 5.f);

			const float StageH = bP ? FMath::Clamp(FMath::RoundToFloat(FV::VH * 0.22f), 124.f, 190.f) : 116.f;
			const UFVSaveGame* P = Game->GetProfile();
			FFVPublicProfile Look;
			if (P) { Look.Hat = P->EquippedHat; Look.Outfit = P->EquippedOutfit; Look.Pet = P->EquippedPet; }
			DrawStage(D, FFVRect(X, Y, W, StageH), &Look, StageH >= 180.f ? 3.f : 2.f, T, false);
			Y += StageH + (bP ? 12.f : 8.f);

			const float QS = bP ? FV::Ui::Title() : 12.f;
			D.Text(TEXT("Как вас зовут, рыбак?"), FVector2D(Win.Center().X, Y), QS, FVPal::Ink(), EFVAlign::Center, false, true);
			Y += D.LineHeight(QS) + (bP ? 8.f : 5.f);

			const float FH = bP ? 40.f : 22.f;
			const FFVRect FieldR(X, Y, W, FH);
			const bool bSubmit = FVKit::TextField(D, FieldR, SetupField, TEXT("Ваше имя"));
			if (bSubmit) { Act = EAct::Start; }
			Y += FH + 4.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			FString Check;
			const FString LiveErr = SetupField.Text.IsEmpty() ? FString() : CheckName(SetupField.Text, Check);
			const FString Line = !LiveErr.IsEmpty() ? LiveErr : (SetupField.Text.TrimStartAndEnd().IsEmpty() ? SetupError : FString());
			if (!Line.IsEmpty()) { D.Text(D.Ellipsize(Line, W, SS, true), FVector2D(X + 2.f, Y), SS, InkBad(), EFVAlign::Left, false, true); }
			else
			{
				D.Text(FString::Printf(TEXT("%d / 16"), CollapseSpaces(SetupField.Text).Len()), FVector2D(X + W - 2.f, Y), SS, FVPal::InkSoft(), EFVAlign::Right, false, false);
				D.Text(TEXT("Буквы, цифры, пробел, «_» и «-»"), FVector2D(X + 2.f, Y), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			Y += D.LineHeight(SS) + (bP ? 8.f : 5.f);

			const float BH = bP ? 40.f : 22.f;
			const FFVRect Row(X, Y, W, BH);
			const FFVRect RandR = FVDraw::SplitH(Row, 0, 3, 6.f);
			const FFVRect StartR(RandR.Right() + 6.f, Y, Row.Right() - RandR.Right() - 6.f, BH);
			if (D.Button(RandR, TEXT("Придумать"), EFVButton::Normal, true, bP ? FV::Ui::Body() : 8.f)) { Act = EAct::RandomName; }
			if (D.Button(StartR, TEXT("Начать рыбалку"), EFVButton::Primary, LiveErr.IsEmpty() && !SetupField.Text.TrimStartAndEnd().IsEmpty(), bP ? FV::Ui::Body() + 1.f : 9.f))
			{
				Act = EAct::Start;
			}
			Y += BH + (bP ? 12.f : 7.f);
			const FString Note = TEXT("Имя увидят другие рыбаки в таблице лидеров. Сменить его можно в любой момент в профиле. Одинаковые имена не страшны — у каждого есть свой номер.");
			D.TextWrapped(Note, FVector2D(X + 2.f, Y), W - 4.f, SS, FVPal::InkSoft(), false, 1.1f);
		}

		// ------------------------------------------------------------------ identity (stage, name, tag, status, level)
		FString DisplayName() const
		{
			return Prof.Name.IsEmpty() ? FString(TEXT("Безымянный рыбак")) : Prof.Name;
		}

		FString TagText() const
		{
			return Prof.Tag > 0 ? FString::Printf(TEXT("#%d"), Prof.Tag) : FString();
		}

		bool IsBanned() const
		{
			if (Mode == EMode::Other) { return Prof.bBanned; }
			const UFVOnline* On = Online();
			return On && On->GetStatus() == EFVOnlineStatus::Banned;
		}

		/** Level badge + XP bar + numbers. Returns the height used. */
		float DrawLevel(FVDraw& D, float X, float Y, float W, bool bP)
		{
			const int32 L = FMath::Clamp(Prof.Level, 1, FV::MaxLevel);
			const float R = bP ? 14.f : 10.f;
			LevelBadge(D, FVector2D(X + R, Y + R), R, L, bP ? FV::Ui::Body() + 1.f : 8.f);
			const float TX = X + R * 2.f + (bP ? 8.f : 6.f);
			const float TW = X + W - TX;
			const float BS = bP ? FV::Ui::Body() : 8.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			D.Text(FString::Printf(TEXT("Уровень %d"), L), FVector2D(TX, Y), BS, FVPal::Ink(), EFVAlign::Left, false, true);
			const int32 Cur = FVDB::XpForLevel(L);
			const bool bMax = L >= FV::MaxLevel;
			const int32 Next = bMax ? Cur : FVDB::XpForLevel(L + 1);
			const float Frac = bMax ? 1.f : FMath::Clamp(float(Prof.Xp - Cur) / float(FMath::Max(1, Next - Cur)), 0.f, 1.f);
			const float BarY = Y + D.LineHeight(BS) + 1.f;
			const float BarH = bP ? 8.f : 6.f;
			D.Bar(FFVRect(TX, FMath::RoundToFloat(BarY), TW, BarH), Frac, FV::Col(96, 180, 230));
			FString XpT = bMax ? FString(TEXT("Максимальный уровень!")) : FString::Printf(TEXT("%s / %s XP"), *FV::Money(Prof.Xp), *FV::Money(Next));
			// update 6 (§9): own profile — «стаж» (XP trimmed by the 0.5 -> 6 migration) and the freeze before the City
			if (Mode != EMode::Other && Game)
			{
				if (const UFVSaveGame* SP = Game->GetProfile())
				{
					if (SP->IsXpFrozen() && !bMax) { XpT = TEXT("Опыт замер до Города"); }
					if (SP->U6.XpSeniority > 0) { XpT += FString::Printf(TEXT(" · стаж: %s"), *FV::Money(SP->U6.XpSeniority)); }
				}
			}
			D.Text(D.Ellipsize(XpT, TW, SS, false), FVector2D(TX, BarY + BarH + 1.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			return FMath::Max(R * 2.f, BarY + BarH + 1.f + D.LineHeight(SS, false) - Y);
		}

		/** Centered name / #tag / status / start date block. Returns the height used. */
		float DrawNameBlock(FVDraw& D, float CX, float Y, float W, bool bP)
		{
			const float Y0 = Y;
			const float NS = bP ? FV::Ui::Title() + 2.f : 13.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			D.Text(D.Ellipsize(DisplayName(), W, NS, true), FVector2D(CX, Y), NS, FVPal::Ink(), EFVAlign::Center, true, true);
			Y += D.LineHeight(NS) - (bP ? 1.f : 2.f);
			const FString Tag = TagText();
			if (!Tag.IsEmpty())
			{
				D.Text(Tag, FVector2D(CX, Y), SS, FVPal::InkSoft(), EFVAlign::Center, false, true);
				Y += D.LineHeight(SS) + 2.f;
			}
			else { Y += 3.f; }
			const float StS = bP ? FV::Ui::Body() - 0.5f : 7.f;
			if (!Prof.Status.IsEmpty())
			{
				const TArray<FString> Lines = WrapLines(D, FString::Printf(TEXT("«%s»"), *Prof.Status), W, StS, 2);
				for (const FString& L : Lines)
				{
					D.Text(L, FVector2D(CX, Y), StS, FV::Col(110, 70, 40), EFVAlign::Center, false, false);
					Y += D.LineHeight(StS, false);
				}
			}
			else if (Mode == EMode::Own)
			{
				D.Text(TEXT("Статус не задан"), FVector2D(CX, Y), StS, FV::Col(150, 120, 96), EFVAlign::Center, false, false);
				Y += D.LineHeight(StS, false);
			}
			const FString Since = DateText(Prof.StartUnix);
			if (!Since.IsEmpty())
			{
				Y += 2.f;
				D.Text(FString::Printf(TEXT("Рыбачит с %s"), *Since), FVector2D(CX, Y), SS, FVPal::InkSoft(), EFVAlign::Center, false, false);
				Y += D.LineHeight(SS, false);
			}
			return Y - Y0;
		}

		/** Red banner for blocked accounts. Returns the height used (0 when not banned). */
		float DrawBanBanner(FVDraw& D, float X, float Y, float W, bool bP)
		{
			if (!IsBanned()) { return 0.f; }
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			FString Reason;
			if (Mode != EMode::Other)
			{
				if (const UFVOnline* On = Online()) { Reason = On->GetBanReason(); }
			}
			const FString Text = Reason.IsEmpty() ? FString(TEXT("Аккаунт заблокирован")) : FString::Printf(TEXT("Аккаунт заблокирован: %s"), *Reason);
			const float H = FMath::CeilToFloat(D.MeasureWrapped(Text, W - 12.f, SS, true) + 8.f);
			const FFVRect R(X, Y, W, H);
			D.Rect(R, FV::Col(170, 60, 44));
			D.Rect(R.Inset(1.f), FV::Col(250, 206, 180));
			D.TextWrapped(Text, FVector2D(X + 6.f, Y + 4.f), W - 12.f, SS, InkBad(), true);
			return H + 6.f;
		}

		/** Own: the player number + connection status. Returns the height used. */
		float DrawNumberCard(FVDraw& D, float X, float Y, float W, bool bP)
		{
			const UFVOnline* On = Online();
			const UFVSaveGame* P = Game->GetProfile();
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float BS = bP ? FV::Ui::Body() : 8.f;
			const float Pad = bP ? 8.f : 6.f;
			const FString Hint = (P && P->OnlineTag > 0)
				? FString(TEXT("Назовите номер друзьям — по нему вас легко найти в поиске рейтинга."))
				: FString(TEXT("Номер появится после первой связи с сервером. Играть можно и без интернета."));
			const float TW = W - Pad * 2.f;
			const float H = FMath::CeilToFloat(Pad * 2.f + D.LineHeight(BS) + 2.f + D.LineHeight(SS) + 3.f + D.MeasureWrapped(Hint, TW, SS, false));
			Card(D, FFVRect(X, Y, W, H));
			float TY = Y + Pad;
			D.Text(TEXT("Номер игрока"), FVector2D(X + Pad, TY), BS, FVPal::Ink(), EFVAlign::Left, false, true);
			if (P && P->OnlineTag > 0)
			{
				D.Text(FString::Printf(TEXT("#%d"), P->OnlineTag), FVector2D(X + W - Pad, TY - 1.f), BS + 2.f, FV::Col(120, 72, 20), EFVAlign::Right, false, true);
			}
			TY += D.LineHeight(BS) + 2.f;
			// connection status: a coloured dot + the text
			FLinearColor Dot = FV::Col(150, 130, 110);
			FString St = TEXT("Без сервера");
			if (On)
			{
				St = On->GetStatusText();
				switch (On->GetStatus())
				{
				case EFVOnlineStatus::Online: Dot = FVPal::Green(); break;
				case EFVOnlineStatus::Connecting: Dot = FVPal::Gold(); break;
				case EFVOnlineStatus::Banned: Dot = FVPal::Red(); break;
				case EFVOnlineStatus::Offline: Dot = FV::Col(236, 140, 60); break;
				default: break;
				}
			}
			const float DotS = bP ? 6.f : 5.f;
			D.Rect(FFVRect(X + Pad, FMath::RoundToFloat(TY + D.LineHeight(SS) * 0.5f - DotS * 0.5f), DotS, DotS), Dot);
			D.Text(D.Ellipsize(St, TW - DotS - 5.f, SS, true), FVector2D(X + Pad + DotS + 5.f, TY), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			TY += D.LineHeight(SS) + 3.f;
			D.TextWrapped(Hint, FVector2D(X + Pad, TY), TW, SS, FVPal::InkSoft(), false);
			return H;
		}

		/** Own profile while the online features are off: «Офлайн-режим — онлайн недоступен» + «Подключиться».
		    Returns the height used (0 when online, or when the game runs without a server — the number card says so). */
		float DrawOfflineStrip(FVDraw& D, float X, float Y, float W, bool bP)
		{
			UFVOnline* On = Online();
			const EOffline Why = OfflineOf(On);
			if (!On || Why == EOffline::None || Why == EOffline::NoServer) { return 0.f; }
			const bool bBusy = Why == EOffline::Connecting;
			const bool bUpdate = Why == EOffline::UpdateRequired;
			const bool bFail = bRetried && !bBusy && !bUpdate;
			const FString Title = bBusy ? FString(TEXT("Подключаемся к серверу…"))
				: (bUpdate ? FString(TEXT("Нужна новая версия игры")) : FString(TEXT("Офлайн-режим — онлайн недоступен")));
			FString Text = TEXT("Профиль хранится на устройстве. Рейтинг, почта и турниры вернутся после подключения.");
			if (bBusy) { Text = TEXT("Проверяем связь — это займёт несколько секунд."); }
			else if (bUpdate) { Text = TEXT("Установите обновление, чтобы вернуть рейтинг, почту и турниры."); }
			else if (bFail) { Text = On->GetRetryFailedText(); }
			const float BS = bP ? FV::Ui::Body() : 8.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float Pad = bP ? 10.f : 6.f;
			const float BR = bP ? 14.f : 10.f;
			const float BW = bP ? 160.f : 96.f;
			const float BH = bP ? 34.f : 20.f;
			const float TX = X + Pad + BR * 2.f + (bP ? 10.f : 7.f);
			const float TW = bP ? X + W - Pad - TX : X + W - Pad - BW - 8.f - TX;
			const float TextH = D.LineHeight(BS) + 1.f + D.MeasureWrapped(Text, TW, SS, bFail);
			const float H = FMath::CeilToFloat(bP ? Pad + FMath::Max(BR * 2.f, TextH) + 8.f + BH + Pad : Pad + FMath::Max(FMath::Max(BR * 2.f, TextH), BH) + Pad);
			const FFVRect R(X, Y, W, H);
			Card(D, R);
			D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 3.f, R.H - 2.f), FV::Col(236, 140, 60));
			SignalBadge(D, FVector2D(X + Pad + BR, Y + Pad + BR), BR, Why, D.Time);
			D.Text(D.Ellipsize(Title, TW, BS, true), FVector2D(TX, Y + Pad), BS, FVPal::Ink(), EFVAlign::Left, false, true);
			D.TextWrapped(Text, FVector2D(TX, Y + Pad + D.LineHeight(BS) + 1.f), TW, SS, bFail ? InkBad() : FVPal::InkSoft(), bFail);
			const FFVRect BtnR = bP ? FFVRect(X + W - Pad - BW, Y + H - Pad - BH, BW, BH)
				: FFVRect(X + W - Pad - BW, FMath::RoundToFloat(Y + (H - BH) * 0.5f), BW, BH);
			const float TS = bP ? FV::Ui::Body() : 8.f;
			if (bUpdate)
			{
				if (D.Button(BtnR, TEXT("Скачать"), EFVButton::Primary, true, TS)) { FVUI::Sfx(EFVSfx::Click); On->OpenDownloadPage(); }
			}
			else if (D.Button(BtnR, bBusy ? FString(TEXT("Подключаемся…")) : FString(TEXT("Подключиться")), EFVButton::Primary, !bBusy, TS))
			{
				FVUI::Sfx(EFVSfx::Click);
				bRetried = true;
				On->RetryOnline();
			}
			return H;
		}

		// ------------------------------------------------------------------ sections
		float DrawFavAndBest(FVDraw& D, float X, float Y, float W, bool bP)
		{
			const float Gap = bP ? 8.f : 6.f;
			const float CW = FMath::FloorToFloat((W - Gap) * 0.5f);
			const float H = bP ? 80.f : 58.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float BS = bP ? FV::Ui::Body() : 8.f;
			const float Pad = bP ? 8.f : 6.f;
			// favourite location
			{
				const FFVRect R(X, Y, CW, H);
				Card(D, R);
				const FFVLocationDef* Loc = FVDB::FindLocation(Prof.FavoriteLocation);
				D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 3.f, R.H - 2.f), Loc ? FVUI::ThemeColor(Loc->Theme) : FV::Col(200, 170, 130));
				const float IS = bP ? 24.f : 16.f;
				IconAt(D, EFVIcon::Map, FFVRect(R.X + Pad + 2.f, R.Y + Pad, IS, IS), bP ? 1.5f : 1.f);
				const float TX = R.X + Pad + IS + 8.f;
				D.Text(TEXT("Любимое место"), FVector2D(TX, R.Y + Pad), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
				const float TW = R.Right() - Pad - TX;
				if (Loc)
				{
					const TArray<FString> Lines = WrapLines(D, Loc->Name, TW, BS, 2, true);
					float LY = R.Y + Pad + D.LineHeight(SS) + 1.f;
					for (const FString& L : Lines) { D.Text(L, FVector2D(TX, LY), BS, FVPal::Ink(), EFVAlign::Left, false, true); LY += D.LineHeight(BS); }
				}
				else
				{
					D.TextWrapped(TEXT("Пока не выбрано — порыбачьте подольше!"), FVector2D(TX, R.Y + Pad + D.LineHeight(SS) + 1.f), TW, SS, FVPal::InkSoft());
				}
			}
			// best catch
			{
				const FFVRect R(X + CW + Gap, Y, W - CW - Gap, H);
				Card(D, R);
				D.Text(TEXT("Лучший улов"), FVector2D(R.X + Pad, R.Y + Pad), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
				const FFVFishDef* F = FVDB::FindFish(Prof.BestCatch.FishId);
				const float TopY = R.Y + Pad + D.LineHeight(SS) + 1.f;
				if (F)
				{
					const float FW = bP ? 58.f : 40.f;
					const FFVRect FishBox(R.X + Pad - 2.f, TopY, FW, R.Bottom() - Pad - TopY);
					FishAt(D, F->Id, FishBox, bP ? 2.f : 1.f);
					const float TX = FishBox.Right() + 4.f;
					const float TW = R.Right() - Pad - TX;
					D.Text(D.Ellipsize(F->Name, TW, BS, true), FVector2D(TX, TopY), BS, FVUI::RarityInk(F->Rarity), EFVAlign::Left, false, true);
					const float LY = TopY + D.LineHeight(BS) + 1.f;
					const FString Sz = SizeText(Prof.BestCatch.SizeCm);
					D.Text(Sz, FVector2D(TX, LY), SS, FVPal::Ink(), EFVAlign::Left, false, true);
					const float StarS = bP ? 1.f : 1.f;
					D.Stars(FVector2D(TX, LY + D.LineHeight(SS) + 1.f), Prof.BestCatch.Quality, StarS);
				}
				else
				{
					D.TextWrapped(TEXT("Ещё впереди — удачи на рыбалке!"), FVector2D(R.X + Pad, TopY), R.W - Pad * 2.f, SS, FVPal::InkSoft());
				}
			}
			return H;
		}

		void Plaque(FVDraw& D, const FFVRect& R, const FFVFishShow* F, bool bP)
		{
			D.Rect(R.Offset(1.f, 2.f), FV::Col(60, 30, 14, 70));
			D.Rect(R, FVPal::WoodDark());
			D.Rect(R.Inset(1.f), FV::Col(138, 74, 34));
			D.Rect(R.Inset(3.f), FV::Col(156, 88, 42));
			for (float GY = R.Y + 7.f; GY < R.Bottom() - 4.f; GY += 9.f)
			{
				D.Rect(FFVRect(R.X + 4.f, GY, R.W - 8.f, 1.f), FV::Col(140, 78, 36, 150));
			}
			D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(196, 124, 64));
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float LabH = FMath::RoundToFloat(D.LineHeight(SS) * 2.f + 4.f);
			const FFVRect Lab(R.X + 4.f, R.Bottom() - LabH - 4.f, R.W - 8.f, LabH);
			const FFVFishDef* Def = F ? FVDB::FindFish(F->FishId) : nullptr;
			if (!Def)
			{
				const FFVRect Hole(R.X + 8.f, R.Y + 7.f, R.W - 16.f, Lab.Y - R.Y - 12.f);
				D.Frame(Hole, FV::Col(110, 60, 28), 1.f);
				D.Text(TEXT("пусто"), FVector2D(Hole.Center().X, FMath::RoundToFloat(Hole.Center().Y - D.LineHeight(SS) * 0.5f)), SS, FV::Col(216, 170, 120), EFVAlign::Center, false, false);
				return;
			}
			// two brass pins
			D.Rect(FFVRect(R.X + 5.f, R.Y + 5.f, 2.f, 2.f), FV::Col(236, 196, 96));
			D.Rect(FFVRect(R.Right() - 7.f, R.Y + 5.f, 2.f, 2.f), FV::Col(236, 196, 96));
			FishAt(D, Def->Id, FFVRect(R.X + 8.f, R.Y + 8.f, R.W - 16.f, Lab.Y - R.Y - 12.f), bP ? 3.f : 2.f);
			D.Rect(Lab, FV::Col(140, 98, 34));
			D.Rect(Lab.Inset(1.f), FV::Col(234, 196, 100));
			D.Rect(FFVRect(Lab.X + 1.f, Lab.Y + 1.f, Lab.W - 2.f, 1.f), FV::Col(255, 236, 170));
			D.Text(D.Ellipsize(Def->Name, Lab.W - 6.f, SS, true), FVector2D(Lab.Center().X, Lab.Y + 2.f), SS, FVPal::WoodDark(), EFVAlign::Center, false, true);
			const FString Sz = SizeText(F->SizeCm);
			const float SW = D.MeasureText(Sz, SS, false).X;
			const float StW = StarsWidth(F->Quality, 1.f);
			const float SX = FMath::RoundToFloat(Lab.Center().X - (SW + StW) * 0.5f);
			const float SY = Lab.Y + 2.f + D.LineHeight(SS);
			D.Text(Sz, FVector2D(SX, SY), SS, FV::Col(90, 56, 20), EFVAlign::Left, false, false);
			if (StW > 0.f) { D.Stars(FVector2D(SX + SW + 2.f, SY + 1.f), F->Quality, 1.f); }
		}

		float DrawShowcase(FVDraw& D, float X, float Y, float W, bool bP)
		{
			const float Y0 = Y;
			Y = FVUI::SectionHeader(D, TEXT("Витрина трофеев"), FVector2D(X, Y), W, bP ? 11.5f : 9.f);
			const float Gap = bP ? 8.f : 6.f;
			const float PW = FMath::FloorToFloat((W - Gap * 2.f) / 3.f);
			const float PH = bP ? 104.f : 70.f;
			for (int32 i = 0; i < 3; ++i)
			{
				const FFVRect R(FMath::RoundToFloat(X + (PW + Gap) * i), Y, i == 2 ? W - (PW + Gap) * 2.f : PW, PH);
				Plaque(D, R, Prof.Showcase.IsValidIndex(i) ? &Prof.Showcase[i] : nullptr, bP);
			}
			return Y + PH - Y0;
		}

		void StatTile(FVDraw& D, const FFVRect& R, EFVIcon Icon, const FString& Value, const FString& Label, bool bP)
		{
			D.Rect(R, FV::Col(214, 160, 100, 90));
			D.Rect(R.Inset(1.f), FV::Col(255, 240, 210, 170));
			const float IS = bP ? 22.f : 16.f;
			const float IX = R.X + (bP ? 7.f : 5.f);
			IconAt(D, Icon, FFVRect(IX, FMath::RoundToFloat(R.Center().Y - IS * 0.5f), IS, IS), bP ? 2.f : 1.f);
			const float TX = IX + IS + (bP ? 8.f : 5.f);
			const float TW = R.Right() - 4.f - TX;
			const float VS = bP ? FV::Ui::Body() + 1.f : 8.f;
			const float LS = bP ? FV::Ui::Small() : 6.5f;
			const float TotalH = D.LineHeight(VS) + D.LineHeight(LS, false) - 1.f;
			const float TY = FMath::RoundToFloat(R.Center().Y - TotalH * 0.5f);
			D.Text(D.Ellipsize(Value, TW, VS, true), FVector2D(TX, TY), VS, FVPal::Ink(), EFVAlign::Left, false, true);
			D.Text(D.Ellipsize(Label, TW, LS, false), FVector2D(TX, TY + D.LineHeight(VS) - 1.f), LS, FVPal::InkSoft(), EFVAlign::Left, false, false);
		}

		float DrawStats(FVDraw& D, float X, float Y, float W, bool bP)
		{
			const float Y0 = Y;
			Y = FVUI::SectionHeader(D, TEXT("Статистика"), FVector2D(X, Y), W, bP ? 11.5f : 9.f);
			struct FStat { EFVIcon Icon; FString Value; FString Label; };
			const int32 SpeciesTotal = FMath::Max(1, FVDB::Fish().Num());
			const int32 AchTotal = FMath::Max(1, FVDB::Achievements().Num());
			TArray<FStat> Stats;
			Stats.Add({ EFVIcon::Keepnet, FV::Money(Prof.FishTotal), FVUI::Plural(Prof.FishTotal, TEXT("рыба поймана"), TEXT("рыбы поймано"), TEXT("рыб поймано")) });
			Stats.Add({ EFVIcon::Book, FString::Printf(TEXT("%d из %d"), Prof.Species, SpeciesTotal), TEXT("видов рыб") });
			Stats.Add({ EFVIcon::StarIridium, FV::Money(Prof.Legendary), FVUI::Plural(Prof.Legendary, TEXT("легендарная"), TEXT("легендарные"), TEXT("легендарных")) });
			Stats.Add({ EFVIcon::Trophy, FString::Printf(TEXT("%d из %d"), Prof.Achievements, AchTotal), TEXT("достижений") });
			Stats.Add({ EFVIcon::Check, FV::Money(Prof.Perfect), FVUI::Plural(Prof.Perfect, TEXT("идеальная подсечка"), TEXT("идеальные подсечки"), TEXT("идеальных подсечек")) });
			Stats.Add({ EFVIcon::Coin, FV::Money(Prof.CoinsEarned), TEXT("монет заработано") });
			Stats.Add({ EFVIcon::Hammer, FV::Money(Prof.StationsBuilt), FVUI::Plural(Prof.StationsBuilt, TEXT("постройка в доме"), TEXT("постройки в доме"), TEXT("построек в доме")) });
			Stats.Add({ EFVIcon::Clock, PlayTimeText(Prof.PlaySeconds), TEXT("в игре") });
			const int32 Cols = 2;
			const float Gap = bP ? 6.f : 4.f;
			const float TH = bP ? 42.f : 26.f;
			const float TW = FMath::FloorToFloat((W - Gap) / Cols);
			for (int32 i = 0; i < Stats.Num(); ++i)
			{
				const int32 C = i % Cols, Rw = i / Cols;
				const FFVRect R(X + (TW + Gap) * C, Y + (TH + Gap) * Rw, C == Cols - 1 ? W - (TW + Gap) * C : TW, TH);
				StatTile(D, R, Stats[i].Icon, Stats[i].Value, Stats[i].Label, bP);
			}
			const int32 Rows = (Stats.Num() + Cols - 1) / Cols;
			return Y + Rows * (TH + Gap) - Gap - Y0;
		}

		float DrawRanks(FVDraw& D, float X, float Y, float W, bool bP)
		{
			static const EFVBoardCategory Order[] = { EFVBoardCategory::Level, EFVBoardCategory::FishTotal, EFVBoardCategory::Biggest,
				EFVBoardCategory::Legendary, EFVBoardCategory::Species, EFVBoardCategory::Earned, EFVBoardCategory::Achievements };
			TArray<TPair<EFVBoardCategory, int32>> List;
			for (EFVBoardCategory C : Order)
			{
				const int32* R = Prof.Ranks.Find(C);
				if (R && *R > 0) { List.Emplace(C, *R); }
			}
			if (List.Num() == 0) { return -1.f; }
			const float Y0 = Y;
			Y = FVUI::SectionHeader(D, TEXT("Места в рейтинге"), FVector2D(X, Y), W, bP ? 11.5f : 9.f);
			const float SS = bP ? FV::Ui::Small() + 0.5f : 6.5f;
			const float CH = FMath::RoundToFloat(D.LineHeight(SS) + (bP ? 10.f : 6.f));
			const float Gap = bP ? 6.f : 4.f;
			float CX = X, CY = Y;
			for (const TPair<EFVBoardCategory, int32>& E : List)
			{
				const FString Name = UFVOnline::CategoryName(E.Key);
				const FString Place = FString::Printf(TEXT("#%d"), E.Value);
				const float NW = D.MeasureText(Name, SS, false).X;
				const float PW = D.MeasureText(Place, SS, true).X;
				const float ChipW = FMath::RoundToFloat(NW + PW + (bP ? 20.f : 14.f));
				if (CX > X && CX + ChipW > X + W) { CX = X; CY += CH + Gap; }
				const FFVRect R(CX, CY, FMath::Min(ChipW, W), CH);
				const bool bMedal = E.Value <= 3;
				FLinearColor Border = MedalColor(E.Value) * 0.75f;
				Border.A = 1.f;
				D.Rect(R, bMedal ? Border : FV::Col(186, 124, 66));
				D.Rect(R.Inset(1.f), bMedal ? FV::Col(255, 244, 200) : FV::Col(255, 238, 206));
				const float TY = FMath::RoundToFloat(R.Y + (CH - D.LineHeight(SS)) * 0.5f);
				D.Text(Place, FVector2D(R.X + (bP ? 7.f : 5.f), TY), SS, bMedal ? FV::Col(150, 96, 10) : FV::Col(120, 72, 20), EFVAlign::Left, false, true);
				D.Text(Name, FVector2D(R.X + (bP ? 7.f : 5.f) + PW + 4.f, TY), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				CX += ChipW + Gap;
			}
			return CY + CH - Y0;
		}

		/** Right column (landscape) / the rest of the portrait column. Returns the height used. */
		float DrawSections(FVDraw& D, float X, float Y, float W, bool bP)
		{
			const float Y0 = Y;
			const float Gap = bP ? 14.f : 10.f;
			if (Mode == EMode::Own)
			{
				const float OH = DrawOfflineStrip(D, X, Y, W, bP);
				if (OH > 0.f) { Y += OH + Gap; }
			}
			Y += DrawFavAndBest(D, X, Y, W, bP) + Gap;
			Y += DrawShowcase(D, X, Y, W, bP) + Gap;
			Y += DrawStats(D, X, Y, W, bP) + Gap;
			const float RH = DrawRanks(D, X, Y, W, bP);
			if (RH >= 0.f) { Y += RH + Gap; }
			if (Mode == EMode::Other)
			{
				const FString Seen = LastSeenText(Prof.LastSeenUnix, Online() ? Online()->GetServerNow() : FDateTime::UtcNow().ToUnixTimestamp());
				if (!Seen.IsEmpty())
				{
					const float SS = bP ? FV::Ui::Small() : 6.5f;
					IconAt(D, EFVIcon::Clock, FFVRect(X, Y - 1.f, bP ? 14.f : 10.f, bP ? 14.f : 10.f), 1.f);
					D.Text(Seen, FVector2D(X + (bP ? 18.f : 13.f), Y), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
					Y += D.LineHeight(SS) + Gap;
				}
			}
			else if (!bP)
			{
				Y += DrawNumberCard(D, X, Y, W, bP) + Gap;
			}
			return Y - Gap - Y0;
		}

		void DrawStateCard(FVDraw& D, const FFVRect& Area, bool bP)
		{
			UFVOnline* On = Online();
			const EOffline Why = OfflineOf(On);
			if (Why != EOffline::None)
			{
				OfflineCard(D, Area, bP, On, Why, TEXT("Профили других рыбаков откроются, как только игра подключится к серверу."), bRetried);
				return;
			}
			const float SS = bP ? FV::Ui::Body() : 8.f;
			const FVector2D C = Area.Center();
			if (Load == ELoad::Loading)
			{
				Spinner(D, FVector2D(C.X, C.Y - 14.f), bP ? 12.f : 9.f, Game->GetTime());
				D.Text(TEXT("Загружаем профиль…"), FVector2D(C.X, C.Y + 4.f), SS, FVPal::InkSoft(), EFVAlign::Center, false, true);
				return;
			}
			const float W = FMath::Min(Area.W - 20.f, bP ? 300.f : 300.f);
			const FString Msg = LoadError.IsEmpty() ? FString(TEXT("Профиль недоступен.")) : LoadError;
			const float TH = D.MeasureWrapped(Msg, W - 20.f, SS, false);
			const float BH = bP ? 36.f : 20.f;
			const float H = FMath::CeilToFloat(22.f + TH + 10.f + BH + 12.f + (bP ? 8.f : 0.f));
			const FFVRect R(FMath::RoundToFloat(C.X - W * 0.5f), FMath::RoundToFloat(C.Y - H * 0.5f), W, H);
			Card(D, R);
			IconAt(D, EFVIcon::Exclaim, FFVRect(C.X - 8.f, R.Y + 5.f, 16.f, 16.f), 1.f);
			D.TextWrapped(Msg, FVector2D(R.X + 10.f, R.Y + 24.f), W - 20.f, SS, FVPal::Ink(), false);
			const float BW = bP ? 160.f : 110.f;
			if (D.Button(FFVRect(FMath::RoundToFloat(C.X - BW * 0.5f), R.Bottom() - BH - 10.f, BW, BH), TEXT("Повторить"), EFVButton::Primary, true, bP ? FV::Ui::Body() : 8.f))
			{
				Act = EAct::Retry;
			}
		}

		// ------------------------------------------------------------------ layouts
		void DrawLandscape(FVDraw& D, const FFVRect& Win)
		{
			const double T = Game->GetTime();
			const FFVRect Left(Win.X + 12.f, Win.Y + 14.f, 200.f, Win.H - 26.f);
			const FFVRect Right(Left.Right() + 12.f, Win.Y + 14.f, Win.Right() - 12.f - Left.Right() - 12.f, Win.H - 26.f);
			D.Rect(FFVRect(Left.Right() + 5.f, Left.Y + 4.f, 1.f, Left.H - 8.f), FV::Col(206, 150, 92));
			D.Rect(FFVRect(Left.Right() + 6.f, Left.Y + 4.f, 1.f, Left.H - 8.f), FV::Col(255, 236, 196));

			const bool bReady = Mode != EMode::Other || Load == ELoad::Ready;
			const bool bSpin = !bReady && Load == ELoad::Loading;
			const FFVRect Stage(Left.X, Left.Y, Left.W, 124.f);
			DrawStage(D, Stage, bReady ? &Prof : nullptr, 2.f, T, bSpin);
			float Y = Stage.Bottom() + 5.f;
			if (bReady)
			{
				Y += DrawNameBlock(D, Left.Center().X, Y, Left.W - 4.f, false) + 5.f;
				Y += DrawLevel(D, Left.X + 2.f, Y, Left.W - 4.f, false) + 6.f;
				Y += DrawBanBanner(D, Left.X, Y, Left.W, false);
			}
			// buttons at the bottom of the left column
			const float BH = 20.f;
			if (Mode == EMode::Own)
			{
				const FFVRect Row1(Left.X, Left.Bottom() - BH * 2.f - 4.f, Left.W, BH);
				const FFVRect Row2(Left.X, Left.Bottom() - BH, Left.W, BH);
				if (D.Button(FVDraw::SplitH(Row1, 0, 2, 4.f), TEXT("Рейтинг"), EFVButton::Normal, true, 8.f)) { Act = EAct::Board; }
				const FFVRect MailR = FVDraw::SplitH(Row1, 1, 2, 4.f);
				if (D.Button(MailR, TEXT("Почта"), EFVButton::Normal, true, 8.f)) { Act = EAct::Mail; }
				const int32 Unread = Online() ? Online()->UnreadMailCount() : 0;
				if (Unread > 0) { FVUI::CountBubble(D, FVector2D(MailR.Right() - 4.f, MailR.Y + 1.f), Unread); }
				if (D.Button(Row2, TEXT("Изменить имя и статус"), EFVButton::Primary, true, 8.f)) { Act = EAct::Edit; }
			}
			else if (bFromBoard)
			{
				if (D.BackButton(FFVRect(Left.X, Left.Bottom() - BH, Left.W, BH), TEXT("К рейтингу"), 8.f)) { Act = EAct::Back; }
			}

			// right: sections in a smooth scroll view
			if (!bReady)
			{
				DrawStateCard(D, Right, false);
				return;
			}
			Scroll.Update(D, Right, ContentH);
			D.PushClip(Right);
			const float Y0 = Right.Y + 2.f - Scroll.Offset();
			const float H = DrawSections(D, Right.X + 2.f, Y0, Right.W - 12.f, false);
			ContentH = H + 8.f;
			D.PopClip();
			Scroll.DrawFades(D, Right, FVPal::Parchment());
			Scroll.DrawBar(D, Right);
		}

		void DrawPortrait(FVDraw& D, const FFVRect& Win)
		{
			const double T = Game->GetTime();
			const float Top = Win.Y + 16.f;
			const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;
			const FFVRect Area(Win.X + 4.f, Top, Win.W - 8.f, Bottom - Top);
			const float X = Win.X + 12.f;
			const float W = Win.W - 24.f;
			const bool bReady = Mode != EMode::Other || Load == ELoad::Ready;
			const bool bSpin = !bReady && Load == ELoad::Loading;

			Scroll.Update(D, Area, bReady ? ContentH : Area.H);
			D.PushClip(Area);
			float Y = Area.Y + 4.f - Scroll.Offset();
			const float Y0 = Y;
			const float StageH = 190.f;
			DrawStage(D, FFVRect(X, Y, W, StageH), bReady ? &Prof : nullptr, 3.f, T, bSpin);
			Y += StageH + 8.f;
			if (!bReady)
			{
				DrawStateCard(D, FFVRect(X, Y, W, FMath::Max(140.f, Area.Bottom() - Y)), true);
			}
			else
			{
				Y += DrawNameBlock(D, X + W * 0.5f, Y, W - 8.f, true) + 10.f;
				Y += DrawLevel(D, X + 4.f, Y, W - 8.f, true) + 10.f;
				Y += DrawBanBanner(D, X, Y, W, true);
				if (Mode == EMode::Own)
				{
					const float BW = 230.f;
					if (D.Button(FFVRect(FMath::RoundToFloat(X + (W - BW) * 0.5f), Y, BW, 34.f), TEXT("Изменить имя и статус"), EFVButton::Primary, true, FV::Ui::Body()))
					{
						Act = EAct::Edit;
					}
					Y += 34.f + 14.f;
				}
				Y += DrawSections(D, X, Y, W, true) + 14.f;
				if (Mode == EMode::Own) { Y += DrawNumberCard(D, X, Y, W, true) + 10.f; }
				ContentH = Y - Y0 + 4.f;
			}
			D.PopClip();
			Scroll.DrawFades(D, Area, FVPal::Parchment());
			Scroll.DrawBar(D, Area);

			// footer
			FFVRect Actions;
			if (D.ActionBar(Win, &Actions, bFromBoard ? FString(TEXT("К рейтингу")) : FString(TEXT("Назад")))) { Act = bFromBoard ? EAct::Back : EAct::Close; }
			const float BS = FV::Ui::Body() + 0.5f;
			if (Mode == EMode::Own)
			{
				if (D.Button(FVDraw::SplitH(Actions, 0, 2), TEXT("Рейтинг"), EFVButton::Normal, true, BS)) { Act = EAct::Board; }
				const FFVRect MailR = FVDraw::SplitH(Actions, 1, 2);
				if (D.Button(MailR, TEXT("Почта"), EFVButton::Normal, true, BS)) { Act = EAct::Mail; }
				const int32 Unread = Online() ? Online()->UnreadMailCount() : 0;
				if (Unread > 0) { FVUI::CountBubble(D, FVector2D(MailR.Right() - 6.f, MailR.Y + 2.f), Unread); }
			}
			else
			{
				const bool bCan = Load != ELoad::Loading && OfflineOf(Online()) == EOffline::None;
				if (D.Button(Actions, Load == ELoad::Loading ? FString(TEXT("Загрузка…")) : FString(TEXT("Обновить")), EFVButton::Normal, bCan, BS))
				{
					Act = EAct::Retry;
				}
			}
		}

		// ------------------------------------------------------------------ edit sheet (name + status)
		void DrawEditSheet(FVDraw& D, const FFVRect& Win, bool bP)
		{
			D.Rect(Win.Inset(4.f), FV::Col(40, 20, 10, 120));
			const float W = bP ? Win.W - 20.f : 340.f;
			const float FH = bP ? 38.f : 20.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float HS = bP ? FV::Ui::Header() : 10.f;
			const float Pad = bP ? 16.f : 14.f;
			const float LabH = D.LineHeight(SS) + 2.f;
			const float BH = bP ? 38.f : 20.f;
			const float H = FMath::CeilToFloat(Pad + D.LineHeight(HS) + 8.f + (LabH + FH + 8.f) * 2.f + D.LineHeight(SS) * 2.f + 8.f + BH + Pad);
			const FFVRect R = bP ? FFVRect(Win.X + 10.f, Win.Y + 30.f, W, H) : FFVRect(FMath::RoundToFloat(Win.Center().X - W * 0.5f), Win.Y + 36.f, W, H);
			D.Panel(R, EFVPanel::Wood);
			D.MarkUI(R);
			const float X = R.X + Pad;
			const float CW = R.W - Pad * 2.f;
			float Y = R.Y + Pad - 2.f;
			D.Text(TEXT("Изменить профиль"), FVector2D(R.Center().X, Y), HS, FVPal::Ink(), EFVAlign::Center, true, true);
			Y += D.LineHeight(HS) + 8.f;

			D.Text(TEXT("Имя"), FVector2D(X, Y), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			D.Text(FString::Printf(TEXT("%d / 16"), NameField.Text.Len()), FVector2D(X + CW, Y), SS, FVPal::InkSoft(), EFVAlign::Right, false, false);
			Y += LabH;
			bool bSubmit = FVKit::TextField(D, FFVRect(X, Y, CW, FH), NameField, TEXT("Ваше имя"));
			Y += FH + 8.f;
			D.Text(TEXT("Статус — пара слов о себе"), FVector2D(X, Y), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			D.Text(FString::Printf(TEXT("%d / 40"), StatusField.Text.Len()), FVector2D(X + CW, Y), SS, FVPal::InkSoft(), EFVAlign::Right, false, false);
			Y += LabH;
			bSubmit |= FVKit::TextField(D, FFVRect(X, Y, CW, FH), StatusField, TEXT("Например: «Ловлю только на рассвете»"));
			Y += FH + 6.f;

			FString Clean;
			const FString LiveErr = CheckName(NameField.Text, Clean);
			const FString Line = !LiveErr.IsEmpty() ? LiveErr : EditError;
			if (!Line.IsEmpty()) { D.TextWrapped(Line, FVector2D(X, Y), CW, SS, InkBad(), true); }
			else
			{
				const FString Tag = TagText();
				const FString Hint = Tag.IsEmpty() ? FString(TEXT("Имя видят все рыбаки. Одинаковые имена разрешены."))
					: FString::Printf(TEXT("Имя видят все рыбаки. Ваш номер %s останется прежним."), *Tag);
				D.TextWrapped(Hint, FVector2D(X, Y), CW, SS, FVPal::InkSoft(), false);
			}
			Y += D.LineHeight(SS) * 2.f + 8.f;
			const FFVRect Row(X, Y, CW, BH);
			if (D.Button(FVDraw::SplitH(Row, 0, 2), TEXT("Отмена"), EFVButton::Normal, true, bP ? FV::Ui::Body() : 8.f)) { Act = EAct::Cancel; }
			if (D.Button(FVDraw::SplitH(Row, 1, 2), TEXT("Сохранить"), EFVButton::Primary, LiveErr.IsEmpty(), bP ? FV::Ui::Body() + 0.5f : 8.f)) { Act = EAct::Save; }
			if (bSubmit && LiveErr.IsEmpty() && Act == EAct::None) { Act = EAct::Save; }
		}
	};
}

TUniquePtr<FVScreen> FVMakeProfileScreen(UFVGame* Game)
{
	return MakeUnique<FVProfileScreenImpl::FProfileScreen>(Game);
}
