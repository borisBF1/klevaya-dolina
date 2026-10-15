// «Почта» (update 4, Docs/UPDATE4_ONLINE.md §8): letters from the administration, tournament results and system notes.
// Landscape: the list of letters on the left, the open letter on the right. Portrait: the list; tapping a letter slides
// it in (the footer «Назад» slides back). Attachments (coins / XP / level / items) are shown as chips with icons and
// taken with «Забрать» (UFVOnline::ClaimMail, feedback: sound + toast + sparkles, errors inline). Letters without
// attachments are marked read as soon as they are opened (UFVOnline::MarkMailRead). The list is refreshed from the
// server on open (UFVOnline::FetchMail); cached letters stay visible during short connection hiccups.
// Offline mode (update 4b, §10): mail is unavailable — the friendly «Офлайн-режим — онлайн недоступен» card with
// «Подключиться» (UFVOnline::RetryOnline) replaces the letters; they are fetched again as soon as the game reconnects.
// Context: None (HUD) or "profile" («Назад» returns to the own profile).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "FVTextInput.h"
#include "FVOnline.h"
#include "InputCoreTypes.h"

namespace FVMailScreenImpl
{
	FLinearColor InkBad() { return FV::Col(170, 44, 34); }
	FLinearColor InkGood() { return FV::Col(52, 116, 34); }

	FString KindName(const FString& Kind)
	{
		if (Kind == TEXT("season")) { return TEXT("Итоги турнира"); }
		if (Kind == TEXT("admin")) { return TEXT("Администрация"); }
		return TEXT("Клёвая Долина");
	}

	FLinearColor SealColor(const FString& Kind)
	{
		if (Kind == TEXT("season")) { return FV::Col(226, 168, 30); }
		if (Kind == TEXT("admin")) { return FV::Col(196, 48, 44); }
		return FV::Col(60, 120, 200);
	}

	FString WhenText(int64 Unix)
	{
		if (Unix <= 0) { return FString(); }
		const FTimespan Off = FDateTime::Now() - FDateTime::UtcNow();
		const FDateTime T = FDateTime::FromUnixTimestamp(Unix) + Off;
		const FDateTime Today = FDateTime::Now().GetDate();
		const FString Clock = FString::Printf(TEXT("%02d:%02d"), T.GetHour(), T.GetMinute());
		if (T.GetDate() == Today) { return FString::Printf(TEXT("сегодня, %s"), *Clock); }
		if (T.GetDate() == Today - FTimespan::FromDays(1.0)) { return FString::Printf(TEXT("вчера, %s"), *Clock); }
		return FString::Printf(TEXT("%02d.%02d.%04d, %s"), T.GetDay(), T.GetMonth(), T.GetYear(), *Clock);
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

	/** Pixel envelope: closed with a wax seal, or open with the letter peeking out. */
	void Envelope(FVDraw& D, const FFVRect& R, bool bOpen, const FLinearColor& Seal)
	{
		D.Rect(R.Offset(1.f, 1.f), FV::Col(60, 30, 14, 70));
		if (bOpen)
		{
			const FFVRect Paper(R.X + 3.f, FMath::RoundToFloat(R.Y - R.H * 0.3f), R.W - 6.f, FMath::RoundToFloat(R.H * 0.7f));
			D.Rect(Paper, FV::Col(170, 130, 90));
			D.Rect(Paper.Inset(1.f), FV::Col(255, 252, 240));
			for (float LY = Paper.Y + 3.f; LY < R.Y; LY += 2.f) { D.Rect(FFVRect(Paper.X + 3.f, LY, Paper.W - 6.f, 1.f), FV::Col(190, 180, 170)); }
		}
		D.Rect(R, FV::Col(150, 96, 50));
		D.Rect(R.Inset(1.f), bOpen ? FV::Col(246, 226, 188) : FV::Col(252, 234, 198));
		const FVector2D Mid(R.Center().X, FMath::RoundToFloat(R.Y + R.H * 0.56f));
		const FLinearColor Fold = FV::Col(190, 140, 90);
		if (bOpen)
		{
			D.Line(FVector2D(R.X + 1.f, R.Bottom() - 2.f), Mid, Fold, 1.f);
			D.Line(FVector2D(R.Right() - 1.f, R.Bottom() - 2.f), Mid, Fold, 1.f);
		}
		else
		{
			D.Line(FVector2D(R.X + 1.f, R.Y + 1.f), Mid, Fold, 1.f);
			D.Line(FVector2D(R.Right() - 1.f, R.Y + 1.f), Mid, Fold, 1.f);
			const float SR = FMath::Max(2.f, FMath::RoundToFloat(R.H * 0.2f));
			FLinearColor Dark = Seal * 0.7f;
			Dark.A = 1.f;
			D.Circle(Mid, SR + 1.f, Dark);
			D.Circle(Mid, SR, Seal);
		}
	}

	struct FChip
	{
		EFVIcon Icon = EFVIcon::Count;
		FName Item;
		FString Text;
		FLinearColor Color;
	};

	TArray<FChip> ChipsOf(const FFVMail& M)
	{
		TArray<FChip> Out;
		if (M.Coins > 0) { Out.Add({ EFVIcon::Coin, NAME_None, FString::Printf(TEXT("+%s"), *FVUI::Coins(M.Coins)), FV::Col(120, 72, 20) }); }
		// update 6 (§9, Р1 / Р10): before the first arrival in the City the level stops at 10 and reward XP comes as coins 1:1
		const UFVGame* G = UFVGame::Get();
		const UFVSaveGame* P = G ? G->GetProfile() : nullptr;
		const bool bFrozen = P && !M.bClaimed && P->IsXpFrozen();
		const bool bBeforeCity = P && !M.bClaimed && !P->U6.bVisitedCity;
		if (M.Xp > 0)
		{
			if (bFrozen) { Out.Add({ EFVIcon::Coin, NAME_None, FString::Printf(TEXT("+%s вместо опыта"), *FVUI::Coins(M.Xp)), FV::Col(120, 72, 20) }); }
			else { Out.Add({ EFVIcon::Xp, NAME_None, FString::Printf(TEXT("+%s опыта"), *FV::Money(M.Xp)), FV::Col(40, 100, 160) }); }
		}
		if (M.SetLevel > 0)
		{
			const int32 Shown = bBeforeCity ? FMath::Min(M.SetLevel, FV::ClassicMaxLevel) : M.SetLevel;
			Out.Add({ EFVIcon::Level, NAME_None, FString::Printf(TEXT("Уровень %d"), Shown), FV::Col(150, 90, 10) });
		}
		for (const FFVMailItem& It : M.Items)
		{
			if (It.ItemId.IsNone() || It.Count <= 0) { continue; }
			FString N = FVDB::ItemName(It.ItemId);
			if (N.IsEmpty()) { N = It.ItemId.ToString(); }
			Out.Add({ EFVIcon::Count, It.ItemId, It.Count > 1 ? FString::Printf(TEXT("%s ×%d"), *N, It.Count) : N, FV::Col(90, 56, 30) });
		}
		return Out;
	}

	FString Summary(const FFVMail& M)
	{
		TArray<FString> Parts;
		for (const FChip& C : ChipsOf(M)) { Parts.Add(C.Text.StartsWith(TEXT("+")) ? C.Text.RightChop(1) : C.Text); }
		return FString::Join(Parts, TEXT(", "));
	}

	void ChipIcon(FVDraw& D, const FChip& C, const FFVRect& Box)
	{
		if (!C.Item.IsNone())
		{
			if (const FFVSprite* S = FVUI::ItemSprite(C.Item)) { FVUI::SpriteFit(D, *S, Box, FLinearColor::White, 2.f); }
			return;
		}
		IconAt(D, C.Icon, Box, 2.f);
	}

	enum class EAct : uint8 { None, Close, Back, Refresh, Claim, OpenLetter, CloseLetter };

	class FMailScreen final : public FVScreen
	{
	public:
		explicit FMailScreen(UFVGame* InGame) : FVScreen(InGame), Alive(MakeShared<bool>(true)) {}
		virtual ~FMailScreen() override { *Alive = false; }

		virtual bool HandlesEscape() const override { return true; }

		virtual void OnOpen() override
		{
			TickCount = 0;
			*Alive = true;
			bFromProfile = Game && Game->GetScreenContext() == FName(TEXT("profile"));
			SelId = 0;
			bDetail = false;
			ModeT = 0.f;
			ListScroll.Reset();
			BodyScroll.Reset();
			bRetried = false;
			PrevOffline = OfflineOf(Online());
			Refresh();
		}

		virtual void OnClose() override { *Alive = false; }

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal() || FVTextInput::IsActive()) { return; }
			const bool bP = FV::IsPortrait();
			if (In.WasPressed(EKeys::Escape) || (bP && bDetail && In.WasPressed(EKeys::BackSpace)))
			{
				PendingAct = (bP && bDetail) ? EAct::CloseLetter : EAct::Back;
				return;
			}
			if (In.WasPressed(EKeys::Down) || In.WasPressed(EKeys::S)) { PendingMove = 1; }
			if (In.WasPressed(EKeys::Up) || In.WasPressed(EKeys::W)) { PendingMove = -1; }
			if (In.WasPressed(EKeys::Enter) || In.WasPressed(EKeys::SpaceBar))
			{
				PendingAct = (bP && !bDetail) ? EAct::OpenLetter : EAct::Claim;
			}
			if (In.WasPressed(EKeys::F5)) { PendingAct = EAct::Refresh; }
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bP = FV::IsPortrait();
			Act = PendingAct;
			PendingAct = EAct::None;
			const EOffline Why = OfflineOf(Online());
			if (Why == EOffline::None)
			{
				bRetried = false;
				if (PrevOffline != EOffline::None) { Refresh(); }   // back online: fetch the letters
			}
			else { bDetail = false; }
			PrevOffline = Why;
			BuildOrder();
			ApplyMove(bP);
			const FFVRect Win = bP ? FV::WindowRect() : FFVRect(10.f, 30.f, 620.f, 318.f);
			if (D.Window(Win, TEXT("Почта"))) { Act = EAct::Close; }
			if (bP) { DrawPortrait(D, Win); }
			else { DrawLandscape(D, Win); }
			RunAct(bP);
		}

	private:
		TSharedRef<bool> Alive;
		bool bFromProfile = false;
		bool bFetching = false;
		bool bFetchFailed = false;
		int64 SelId = 0;
		int64 ActId = 0;
		bool bDetail = false;              // portrait: a letter is open
		float ModeT = 0.f;                 // portrait: 0 = list, 1 = letter (slide)
		FFVScrollView ListScroll;
		FFVScrollView BodyScroll;
		float BodyContentH = 0.f;
		int64 BodyFor = 0;
		TSet<int64> Pending;               // claims in flight
		TMap<int64, double> ClaimedAt;     // successful claims (sparkles)
		TMap<int64, FString> ClaimErrors;
		TSet<int64> MarkedRead;            // letters without attachments already marked read by this screen
		TArray<int32> Order;               // indices into GetMail(), newest first
		int32 PendingMove = 0;
		int32 TickCount = 0;
		EAct Act = EAct::None;
		EAct PendingAct = EAct::None;
		bool bRetried = false;             // «Подключиться» was pressed (a failed attempt shows a hint)
		EOffline PrevOffline = EOffline::None;

		UFVOnline* Online() const { return Game ? Game->GetOnline() : nullptr; }
		bool ServerUsable() const { return OfflineOf(Online()) == EOffline::None; }

		/** The offline card (mail needs the server). */
		void DrawOffline(FVDraw& D, const FFVRect& Area, bool bP)
		{
			UFVOnline* On = Online();
			OfflineCard(D, Area, bP, On, OfflineOf(On), TEXT("Письма и награды за турниры никуда не денутся — они придут, как только игра подключится к серверу."), bRetried);
		}

		const TArray<FFVMail>* MailList() const
		{
			const UFVOnline* On = Online();
			return On ? &On->GetMail() : nullptr;
		}

		const FFVMail* FindMail(int64 Id) const
		{
			const TArray<FFVMail>* L = MailList();
			if (!L || Id == 0) { return nullptr; }
			return L->FindByPredicate([Id](const FFVMail& M) { return M.Id == Id; });
		}

		void BuildOrder()
		{
			Order.Reset();
			const TArray<FFVMail>* L = MailList();
			if (!L) { return; }
			for (int32 i = 0; i < L->Num(); ++i) { Order.Add(i); }
			Order.StableSort([L](int32 A, int32 B) { return (*L)[A].CreatedUnix > (*L)[B].CreatedUnix; });
			if (!FindMail(SelId) && !FV::IsPortrait())
			{
				// landscape always shows a letter: the first unread one, else the newest
				SelId = 0;
				for (int32 Idx : Order) { if (!(*L)[Idx].bClaimed) { SelId = (*L)[Idx].Id; break; } }
				if (SelId == 0 && Order.Num() > 0) { SelId = (*L)[Order[0]].Id; }
			}
		}

		int32 SelPos() const
		{
			const TArray<FFVMail>* L = MailList();
			if (!L) { return INDEX_NONE; }
			return Order.IndexOfByPredicate([this, L](int32 Idx) { return (*L)[Idx].Id == SelId; });
		}

		void ApplyMove(bool bP)
		{
			const int32 Move = PendingMove;
			PendingMove = 0;
			const TArray<FFVMail>* L = MailList();
			if (Move == 0 || !L || Order.Num() == 0 || (bP && bDetail)) { return; }
			const int32 Cur = SelPos();
			const int32 Next = FMath::Clamp(Cur == INDEX_NONE ? 0 : Cur + Move, 0, Order.Num() - 1);
			SelId = (*L)[Order[Next]].Id;
			const float Pitch = bP ? 70.f : 40.f;
			ListScroll.ScrollToShow(float(Next) * Pitch, Pitch);
			FVUI::Sfx(EFVSfx::Click, 0.6f);
		}

		void Refresh()
		{
			UFVOnline* On = Online();
			if (!On || !ServerUsable()) { return; }
			bFetching = true;
			bFetchFailed = false;
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			TSharedRef<bool> AliveFlag = Alive;
			FMailScreen* Self = this;
			On->FetchMail([WeakGame, AliveFlag, Self](bool bOk)
			{
				if (!WeakGame.IsValid() || !*AliveFlag) { return; }
				Self->bFetching = false;
				Self->bFetchFailed = !bOk;
			});
		}

		/** Opening a letter: letters without attachments count as read now. */
		void MarkSeen(const FFVMail& M)
		{
			if (M.bClaimed || M.HasAttachments() || MarkedRead.Contains(M.Id)) { return; }
			MarkedRead.Add(M.Id);
			if (UFVOnline* On = Online()) { On->MarkMailRead(M.Id); }
		}

		void Claim(int64 Id)
		{
			UFVOnline* On = Online();
			const FFVMail* M = FindMail(Id);
			if (!On || !M || M->bClaimed || !M->HasAttachments() || Pending.Contains(Id) || !ServerUsable()) { return; }
			Pending.Add(Id);
			ClaimErrors.Remove(Id);
			const FString What = Summary(*M);
			FVUI::Sfx(EFVSfx::Click, 0.7f);
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			TSharedRef<bool> AliveFlag = Alive;
			FMailScreen* Self = this;
			On->ClaimMail(Id, [WeakGame, AliveFlag, Self, Id, What](bool bOk, const FString& Err)
			{
				UFVGame* G = WeakGame.Get();
				if (!G) { return; }
				if (bOk)
				{
					FVUI::Sfx(EFVSfx::Coin);
					FVUI::Sfx(EFVSfx::Treasure, 0.6f);
					G->Toast(FString::Printf(TEXT("Получено: %s"), *What), FVPal::Gold(), EFVIcon::Treasure);
				}
				else
				{
					FVUI::Sfx(EFVSfx::Error);
					G->Toast(Err.IsEmpty() ? FString(TEXT("Не удалось забрать вложение")) : Err, FV::Col(255, 150, 130), EFVIcon::Exclaim);
				}
				if (!*AliveFlag) { return; }
				Self->Pending.Remove(Id);
				if (bOk) { Self->ClaimedAt.Add(Id, G->GetTime()); }
				else { Self->ClaimErrors.Add(Id, Err.IsEmpty() ? FString(TEXT("Не получилось. Проверьте связь и попробуйте ещё раз.")) : Err); }
			});
		}

		void RunAct(bool bP)
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
			case EAct::Refresh:
				FVUI::Sfx(EFVSfx::Click, 0.7f);
				Refresh();
				return;
			case EAct::Claim:
				Claim(ActId != 0 ? ActId : SelId);
				ActId = 0;
				return;
			case EAct::OpenLetter:
			{
				const int64 Id = ActId != 0 ? ActId : SelId;
				ActId = 0;
				if (const FFVMail* M = FindMail(Id))
				{
					SelId = Id;
					bDetail = true;
					BodyScroll.Reset();
					MarkSeen(*M);
					FVUI::Sfx(EFVSfx::Open, 0.6f);
				}
				return;
			}
			case EAct::CloseLetter:
				bDetail = false;
				FVUI::Sfx(EFVSfx::Close, 0.6f);
				return;
			default: return;
			}
		}

		// ------------------------------------------------------------------ list
		bool DrawRow(FVDraw& D, const FFVRect& R, const FFVMail& M, bool bSel, bool bP)
		{
			const bool bUnread = !M.bClaimed;
			const bool bPress = FVUI::IsPressingOn(D, R);
			const bool bHover = !bP && !FV::IsTouchUI() && D.Hover(R);
			FLinearColor Bg = bUnread ? FV::Col(255, 244, 214) : FV::Col(240, 214, 170, 150);
			if (bSel) { Bg = FV::Col(255, 226, 150); }
			else if (bHover) { Bg = FV::Col(255, 248, 228); }
			if (bPress) { Bg = FV::Col(230, 186, 120); }
			D.Rect(R, FV::Col(186, 124, 66, bSel ? 255 : 140));
			D.Rect(R.Inset(1.f), Bg);
			if (bSel) { D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 3.f, R.H - 2.f), FV::Col(206, 140, 30)); }
			const float EH = bP ? 22.f : 14.f;
			const float EW = FMath::RoundToFloat(EH * 1.45f);
			const float Pad = bP ? 10.f : 6.f;
			const FFVRect Env(R.X + Pad + 2.f, FMath::RoundToFloat(R.Center().Y - EH * 0.5f + (bP ? 0.f : 1.f)), EW, EH);
			Envelope(D, Env, !bUnread, SealColor(M.Kind));
			if (bUnread) { D.Circle(FVector2D(R.X + 5.f, R.Center().Y), bP ? 3.f : 2.f, FV::Col(214, 52, 52)); }
			const float TX = Env.Right() + (bP ? 10.f : 7.f);
			const float TW = R.Right() - Pad - TX;
			const float NS = bP ? FV::Ui::Body() + 0.5f : 7.5f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float TY = FMath::RoundToFloat(R.Y + (bP ? 9.f : 4.f));
			D.Text(D.Ellipsize(M.Title.IsEmpty() ? FString(TEXT("Письмо")) : M.Title, TW, NS, bUnread), FVector2D(TX, TY), NS, FVPal::Ink(), EFVAlign::Left, false, bUnread);
			// attachment mini icons on the right of the second line
			const TArray<FChip> Chips = ChipsOf(M);
			const float IS = bP ? 14.f : 10.f;
			float IX = R.Right() - Pad;
			const float SY = TY + D.LineHeight(NS, bUnread) + (bP ? 2.f : 0.f);
			if (Chips.Num() > 0)
			{
				if (M.bClaimed)
				{
					IX -= IS;
					IconAt(D, EFVIcon::Check, FFVRect(IX, SY, IS, IS), 1.f);
				}
				else
				{
					for (int32 i = FMath::Min(Chips.Num(), 4) - 1; i >= 0; --i)
					{
						IX -= IS + 1.f;
						ChipIcon(D, Chips[i], FFVRect(IX, SY, IS, IS));
					}
				}
				IX -= 4.f;
			}
			const FString Sub = FString::Printf(TEXT("%s · %s"), *KindName(M.Kind), *WhenText(M.CreatedUnix));
			D.Text(D.Ellipsize(Sub, IX - TX, SS, false), FVector2D(TX, SY), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			return D.Clicked(R);
		}

		void DrawList(FVDraw& D, const FFVRect& Area, bool bP)
		{
			const TArray<FFVMail>* L = MailList();
			const double T = Game->GetTime();
			if (!L || Order.Num() == 0)
			{
				DrawEmpty(D, Area, bP);
				return;
			}
			const float RowH = bP ? 64.f : 36.f;
			const float Pitch = bP ? 70.f : 40.f;
			ListScroll.Update(D, Area, float(Order.Num()) * Pitch + 4.f);
			const float RW = Area.W - 10.f;
			D.PushClip(Area);
			for (int32 k = 0; k < Order.Num(); ++k)
			{
				const float Y = Area.Y + 2.f - ListScroll.Offset() + float(k) * Pitch;
				if (Y + RowH < Area.Y || Y > Area.Bottom()) { continue; }
				const FFVMail& M = (*L)[Order[k]];
				const bool bSel = M.Id == SelId;
				if (DrawRow(D, FFVRect(Area.X + 2.f, Y, RW, RowH), M, bSel, bP))
				{
					if (bP) { Act = EAct::OpenLetter; ActId = M.Id; }
					else if (M.Id != SelId)
					{
						SelId = M.Id;
						BodyScroll.Reset();
						FVUI::Sfx(EFVSfx::Click, 0.7f);
					}
				}
			}
			D.PopClip();
			ListScroll.DrawFades(D, Area, FVPal::Parchment());
			ListScroll.DrawBar(D, Area);
			if (bFetching)
			{
				Spinner(D, FVector2D(Area.Right() - 12.f, Area.Bottom() - 10.f), bP ? 6.f : 4.f, T);
			}
		}

		void DrawEmpty(FVDraw& D, const FFVRect& Area, bool bP)
		{
			const UFVOnline* On = Online();
			const double T = Game->GetTime();
			const float SS = bP ? FV::Ui::Body() - 0.5f : 7.5f;
			const FVector2D C = Area.Center();
			if (bFetching)
			{
				Spinner(D, FVector2D(C.X, C.Y - 12.f), bP ? 12.f : 9.f, T);
				D.Text(TEXT("Проверяем почтовый ящик…"), FVector2D(C.X, C.Y + 6.f), bP ? FV::Ui::Body() : 8.f, FVPal::InkSoft(), EFVAlign::Center, false, true);
				return;
			}
			FString Msg = TEXT("Писем пока нет. Сюда приходят награды за турниры и весточки из Клёвой Долины.");
			bool bRetry = false;
			if (On && bFetchFailed)
			{
				Msg = TEXT("Нет связи с сервером. Письма появятся, как только связь вернётся.");
				bRetry = true;
			}
			const float W = FMath::Min(Area.W - 20.f, bP ? 290.f : 210.f);
			const float TH = D.MeasureWrapped(Msg, W - 16.f, SS, false);
			const float EH = bP ? 30.f : 20.f;
			const float BH = bRetry ? (bP ? 36.f : 20.f) : 0.f;
			const float H = FMath::CeilToFloat(12.f + EH + 12.f + TH + (bRetry ? 10.f + BH : 0.f) + 12.f);
			const FFVRect R(FMath::RoundToFloat(C.X - W * 0.5f), FMath::RoundToFloat(FMath::Max(Area.Y + 4.f, C.Y - H * 0.5f)), W, H);
			Card(D, R);
			const float EW = FMath::RoundToFloat(EH * 1.45f);
			Envelope(D, FFVRect(FMath::RoundToFloat(C.X - EW * 0.5f), R.Y + 14.f, EW, EH), false, FV::Col(196, 48, 44));
			D.TextWrapped(Msg, FVector2D(R.X + 8.f, R.Y + 14.f + EH + 10.f), W - 16.f, SS, FVPal::Ink(), false);
			if (bRetry)
			{
				const float BW = bP ? 160.f : 100.f;
				if (D.Button(FFVRect(FMath::RoundToFloat(C.X - BW * 0.5f), R.Bottom() - BH - 10.f, BW, BH), TEXT("Повторить"), EFVButton::Primary, true, bP ? FV::Ui::Body() : 8.f))
				{
					Act = EAct::Refresh;
				}
			}
		}

		// ------------------------------------------------------------------ the open letter
		/** Attachment box (chips + «Забрать» / «Получено»). Returns its height; draws only when bDraw. */
		float AttachBox(FVDraw& D, const FFVMail& M, float X, float Y, float W, bool bP, bool bDraw, bool bButton)
		{
			const TArray<FChip> Chips = ChipsOf(M);
			if (Chips.Num() == 0) { return 0.f; }
			const float CS = bP ? FV::Ui::Body() : 7.5f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float IS = bP ? 20.f : 14.f;
			const float ChipH = bP ? 28.f : 18.f;
			const float Gap = bP ? 6.f : 4.f;
			const float Pad = bP ? 8.f : 6.f;
			// flow layout of the chips
			TArray<FFVRect> Rects;
			float CX = X + Pad, CY = Y + Pad + D.LineHeight(SS) + 3.f;
			for (const FChip& C : Chips)
			{
				const float CW = FMath::Min(W - Pad * 2.f, FMath::RoundToFloat(IS + 10.f + D.MeasureText(C.Text, CS, true).X));
				if (CX > X + Pad && CX + CW > X + W - Pad) { CX = X + Pad; CY += ChipH + Gap; }
				Rects.Add(FFVRect(CX, CY, CW, ChipH));
				CX += CW + Gap;
			}
			const float BH = bButton ? (bP ? 36.f : 20.f) : 0.f;
			const FString* Err = ClaimErrors.Find(M.Id);
			const float ErrH = (Err && !M.bClaimed) ? D.MeasureWrapped(*Err, W - Pad * 2.f, SS, true) + 4.f : 0.f;
			const float StatusH = (M.bClaimed || !bButton) ? D.LineHeight(CS) + 4.f : 0.f;
			const float H = FMath::CeilToFloat((CY + ChipH - Y) + 6.f + ErrH + (bButton && !M.bClaimed ? BH + 4.f : StatusH) + Pad);
			if (!bDraw) { return H; }
			const FFVRect Box(X, Y, W, H);
			Card(D, Box, !M.bClaimed);
			D.Text(TEXT("Вложения"), FVector2D(X + Pad, Y + Pad), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			for (int32 i = 0; i < Chips.Num(); ++i)
			{
				const FFVRect& R = Rects[i];
				D.Rect(R, FV::Col(206, 150, 92, 150));
				D.Rect(R.Inset(1.f), M.bClaimed ? FV::Col(240, 226, 200) : FV::Col(255, 250, 236));
				ChipIcon(D, Chips[i], FFVRect(R.X + 4.f, FMath::RoundToFloat(R.Center().Y - IS * 0.5f), IS, IS));
				D.Text(D.Ellipsize(Chips[i].Text, R.W - IS - 10.f, CS, true), FVector2D(R.X + IS + 7.f, FMath::RoundToFloat(R.Center().Y - D.LineHeight(CS) * 0.5f)),
					CS, M.bClaimed ? FVPal::InkSoft() : Chips[i].Color, EFVAlign::Left, false, true);
			}
			float BY = CY + ChipH + 6.f;
			if (ErrH > 0.f)
			{
				D.TextWrapped(*Err, FVector2D(X + Pad, BY), W - Pad * 2.f, SS, InkBad(), true);
				BY += ErrH;
			}
			if (M.bClaimed)
			{
				IconAt(D, EFVIcon::Check, FFVRect(X + Pad, BY, IS * 0.8f, IS * 0.8f), 1.f);
				D.Text(TEXT("Вложения получены"), FVector2D(X + Pad + IS * 0.8f + 4.f, BY), CS, InkGood(), EFVAlign::Left, false, true);
			}
			else if (bButton)
			{
				const bool bBusy = Pending.Contains(M.Id);
				const float BW = FMath::Min(W - Pad * 2.f, bP ? 200.f : 130.f);
				if (D.Button(FFVRect(X + W - Pad - BW, BY, BW, BH), bBusy ? FString(TEXT("Забираем…")) : FString(TEXT("Забрать")), EFVButton::Primary, !bBusy, bP ? FV::Ui::Body() + 1.f : 8.f))
				{
					Act = EAct::Claim;
					ActId = M.Id;
				}
			}
			else
			{
				const bool bBusy = Pending.Contains(M.Id);
				D.Text(bBusy ? FString(TEXT("Забираем…")) : FString(TEXT("Нажмите «Забрать» внизу")), FVector2D(X + Pad, BY), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			}
			// sparkles right after a successful claim
			if (const double* At = ClaimedAt.Find(M.Id))
			{
				const float K = float(Game->GetTime() - *At) / 0.9f;
				if (K >= 0.f && K < 1.f)
				{
					const FVector2D C = Box.Center();
					for (int32 s = 0; s < 12; ++s)
					{
						const float Ang = float(s) / 12.f * 2.f * UE_PI + K * 1.5f;
						const float Rad = 10.f + K * (bP ? 90.f : 60.f);
						const FVector2D P = C + FVector2D(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad * 0.6f);
						const float Sz = s % 2 ? 2.f : 3.f;
						D.Rect(FFVRect(FMath::RoundToFloat(P.X), FMath::RoundToFloat(P.Y), Sz, Sz), FV::Col(255, 222, 90, uint8(255.f * (1.f - K))));
					}
				}
			}
			return H;
		}

		void DrawLetter(FVDraw& D, const FFVRect& Area, const FFVMail& M, bool bP, bool bButton)
		{
			const float Pad = bP ? 10.f : 8.f;
			const float X = Area.X + Pad;
			const float W = Area.W - Pad * 2.f;
			const float HS = bP ? FV::Ui::Header() : 10.f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			const float BS = bP ? FV::Ui::Body() : 8.f;
			float Y = Area.Y + (bP ? 8.f : 6.f);
			// header: envelope + title + from/date
			const float EH = bP ? 24.f : 16.f;
			const float EW = FMath::RoundToFloat(EH * 1.45f);
			Envelope(D, FFVRect(X, Y + (bP ? 6.f : 4.f), EW, EH), true, SealColor(M.Kind));
			const float TX = X + EW + (bP ? 10.f : 8.f);
			const float TW = X + W - TX;
			float TY = Y;
			const FString Title = M.Title.IsEmpty() ? FString(TEXT("Письмо")) : M.Title;
			const float TitleH = D.TextWrapped(Title, FVector2D(TX, TY), TW, HS, FVPal::Ink(), true, 1.0f);
			TY += TitleH;
			D.Text(D.Ellipsize(FString::Printf(TEXT("От: %s · %s"), *KindName(M.Kind), *WhenText(M.CreatedUnix)), TW, SS, false), FVector2D(TX, TY + 1.f), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
			TY += D.LineHeight(SS) + 3.f;
			Y = FMath::Max(TY, Y + EH + 10.f);
			D.Divider(FVector2D(X, Y), W);
			Y += 5.f;
			// attachments at the bottom, the body scrolls above them
			const float AH = AttachBox(D, M, X, 0.f, W, bP, false, bButton);
			const float BodyBottom = Area.Bottom() - (AH > 0.f ? AH + 6.f : 2.f);
			const FFVRect BodyR(Area.X + 2.f, Y, Area.W - 4.f, FMath::Max(20.f, BodyBottom - Y));
			if (BodyFor != M.Id)
			{
				BodyFor = M.Id;
				BodyScroll.Reset();
			}
			BodyScroll.Update(D, BodyR, BodyContentH);
			D.PushClip(BodyR);
			const FString Body = M.Body.IsEmpty() ? FString(TEXT("(пустое письмо)")) : M.Body;
			const float BH = D.TextWrapped(Body, FVector2D(X, BodyR.Y + 2.f - BodyScroll.Offset()), W - 6.f, BS, FVPal::Ink(), false, 1.15f);
			BodyContentH = BH + 8.f;
			D.PopClip();
			BodyScroll.DrawFades(D, BodyR, FVPal::Parchment());
			BodyScroll.DrawBar(D, BodyR);
			if (AH > 0.f) { AttachBox(D, M, X, Area.Bottom() - AH, W, bP, true, bButton); }
		}

		// ------------------------------------------------------------------ layouts
		void DrawHeaderLine(FVDraw& D, float X, float Y, float W, bool bP)
		{
			const UFVOnline* On = Online();
			const float SS = bP ? FV::Ui::Small() + 0.5f : 6.5f;
			const int32 Unread = On ? On->UnreadMailCount() : 0;
			const int32 Total = Order.Num();
			FString L;
			if (Total == 0) { L = TEXT("Почтовый ящик"); }
			else if (Unread > 0) { L = FString::Printf(TEXT("Новых: %d · всего писем: %d"), Unread, Total); }
			else { L = FString::Printf(TEXT("Всего писем: %d · всё прочитано"), Total); }
			D.Text(D.Ellipsize(L, W, SS, true), FVector2D(X, Y), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
		}

		void DrawLandscape(FVDraw& D, const FFVRect& Win)
		{
			if (!ServerUsable())
			{
				DrawOffline(D, FFVRect(Win.X + 12.f, Win.Y + 14.f, Win.W - 24.f, Win.H - 26.f), false);
				return;
			}
			const FFVRect ListPanel(Win.X + 12.f, Win.Y + 26.f, 234.f, Win.H - 38.f);
			const FFVRect Detail(ListPanel.Right() + 8.f, Win.Y + 14.f, Win.Right() - 12.f - ListPanel.Right() - 8.f, Win.H - 26.f);
			DrawHeaderLine(D, ListPanel.X + 2.f, Win.Y + 13.f, ListPanel.W - 24.f, false);
			// refresh button next to the header
			const FFVRect RB(ListPanel.Right() - 18.f, Win.Y + 10.f, 18.f, 14.f);
			if (D.Button(RB, FString(), EFVButton::Ghost, !bFetching && ServerUsable()))
			{
				Act = EAct::Refresh;
			}
			{
				const FVector2D C = RB.Center();
				for (int32 i = 0; i < 16; ++i)
				{
					if (i == 3 || i == 11) { continue; }
					const float A = float(i) / 16.f * 2.f * UE_PI;
					D.Rect(FFVRect(FMath::RoundToFloat(C.X + FMath::Cos(A) * 4.f), FMath::RoundToFloat(C.Y + FMath::Sin(A) * 4.f), 1.f, 1.f), FVPal::InkSoft());
				}
				if (D.Hover(RB)) { D.Tooltip(TEXT("Обновить"), TEXT("Проверить новые письма (F5)")); }
			}
			D.Panel(ListPanel, EFVPanel::Parchment);
			DrawList(D, ListPanel.Inset(3.f), false);

			D.Rect(FFVRect(Detail.X - 4.f, Detail.Y + 4.f, 1.f, Detail.H - 8.f), FV::Col(206, 150, 92));
			const FFVMail* M = FindMail(SelId);
			if (!M)
			{
				if (Order.Num() > 0 || bFetching) { return; }
				const FVector2D C = Detail.Center();
				D.Text(TEXT("Выберите письмо слева"), FVector2D(C.X, C.Y), 8.f, FVPal::InkSoft(), EFVAlign::Center, false, false);
				return;
			}
			MarkSeen(*M);
			DrawLetter(D, Detail, *M, false, true);
		}

		void DrawPortrait(FVDraw& D, const FFVRect& Win)
		{
			const float Top = Win.Y + 16.f;
			const float Bottom = FVDraw::ActionBarRect(Win).Y - 10.f;
			const FFVMail* M = FindMail(SelId);
			if (bDetail && !M) { bDetail = false; }
			ModeT = FVKit::Approach(ModeT, bDetail ? 1.f : 0.f, D.FrameDt(), 0.07f);
			if (FMath::Abs(ModeT - (bDetail ? 1.f : 0.f)) < 0.002f) { ModeT = bDetail ? 1.f : 0.f; }
			const FFVRect Content(Win.X + 4.f, Top, Win.W - 8.f, Bottom - Top);
			if (!ServerUsable())
			{
				DrawOffline(D, Content, true);
				FFVRect OffActions;
				if (D.ActionBar(Win, &OffActions)) { Act = EAct::Back; }
				D.Button(OffActions, TEXT("Обновить"), EFVButton::Normal, false, FV::Ui::Body() + 0.5f);
				return;
			}
			const float Slide = FMath::RoundToFloat(ModeT * Content.W);
			D.PushClip(Content);
			if (ModeT < 1.f)
			{
				D.PushTranslate(FVector2D(-Slide, 0.f));
				DrawHeaderLine(D, Content.X + 8.f, Content.Y, Content.W - 16.f, true);
				const float HH = D.LineHeight(FV::Ui::Small() + 0.5f) + 6.f;
				DrawList(D, FFVRect(Content.X, Content.Y + HH, Content.W, Content.H - HH), true);
				D.PopTranslate();
			}
			if (ModeT > 0.f && M)
			{
				D.PushTranslate(FVector2D(Content.W - Slide, 0.f));
				DrawLetter(D, Content, *M, true, false);
				D.PopTranslate();
			}
			D.PopClip();

			FFVRect Actions;
			if (D.ActionBar(Win, &Actions, bDetail ? FString(TEXT("К письмам")) : FString(TEXT("Назад"))))
			{
				Act = bDetail ? EAct::CloseLetter : EAct::Back;
			}
			const float BS = FV::Ui::Body() + 0.5f;
			if (bDetail && M)
			{
				if (M->HasAttachments() && !M->bClaimed)
				{
					const bool bBusy = Pending.Contains(M->Id);
					if (D.Button(Actions, bBusy ? FString(TEXT("Забираем…")) : FString(TEXT("Забрать")), EFVButton::Primary, !bBusy, BS))
					{
						Act = EAct::Claim;
						ActId = M->Id;
					}
				}
				else
				{
					D.Button(Actions, M->HasAttachments() ? FString(TEXT("Получено")) : FString(TEXT("Прочитано")), EFVButton::Normal, false, BS);
				}
			}
			else
			{
				const bool bCan = !bFetching && ServerUsable();
				if (D.Button(Actions, bFetching ? FString(TEXT("Проверяем…")) : FString(TEXT("Обновить")), EFVButton::Normal, bCan, BS)) { Act = EAct::Refresh; }
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeMailScreen(UFVGame* Game)
{
	return MakeUnique<FVMailScreenImpl::FMailScreen>(Game);
}
