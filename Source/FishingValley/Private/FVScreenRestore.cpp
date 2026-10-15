// «Восстановление сохранения» (update 4, Docs/UPDATE4_ONLINE.md §8): restore a save by a one-time code that the admin
// creates in the admin panel. Explanation, the player's own number (to tell the developer), a code field
// (letters / digits, 8 characters), a confirm dialog (the current progress will be replaced), progress and the result.
// UFVOnline::RestoreByCode downloads the save, replaces the local one and re-enters the world (which may close this
// screen: the result is then reported with a toast).
// Context: None, or "settings" (opened from the pause menu settings: «Назад» returns to the pause menu).
// Offline mode (update 4b, §10): restoring needs the server — the friendly «Офлайн-режим — онлайн недоступен» card
// with «Подключиться» (UFVOnline::RetryOnline) replaces the form until the game reconnects.

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "FVTextInput.h"
#include "FVOnline.h"
#include "InputCoreTypes.h"

namespace FVRestoreScreenImpl
{
	constexpr int32 CodeLen = 8;

	FLinearColor InkBad() { return FV::Col(170, 44, 34); }
	FLinearColor InkGood() { return FV::Col(52, 116, 34); }

	/** Upper-case letters and digits only ("ab-cd 12ef" -> "ABCD12EF"). */
	FString CleanCode(const FString& In)
	{
		FString Out;
		for (TCHAR C : In)
		{
			if (FChar::IsAlnum(C) && C < 128) { Out.AppendChar(FChar::ToUpper(C)); }
		}
		return Out;
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

	void Card(FVDraw& D, const FFVRect& R, const FLinearColor& Border, const FLinearColor& Fill)
	{
		D.Rect(R.Offset(0.f, 1.f), FV::Col(120, 70, 36, 80));
		D.Rect(R, Border);
		D.Rect(R.Inset(1.f), Fill);
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Alpha(FVPal::White(), 0.5f));
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

	enum class EState : uint8 { Idle, Working, Success, Failed };
	enum class EAct : uint8 { None, Close, Back, Submit };

	class FRestoreScreen final : public FVScreen
	{
	public:
		explicit FRestoreScreen(UFVGame* InGame) : FVScreen(InGame), Alive(MakeShared<bool>(true)) {}
		virtual ~FRestoreScreen() override { *Alive = false; }

		virtual bool HandlesEscape() const override { return true; }

		virtual void OnOpen() override
		{
			TickCount = 0;
			*Alive = true;
			bFromSettings = Game && Game->GetScreenContext() == FName(TEXT("settings"));
			Field = FVKit::FFVTextField();
			Field.MaxLen = CodeLen;
			Field.bLettersDigitsOnly = true;
			State = EState::Idle;
			Message.Reset();
			StateTime = 0.0;
			bRetried = false;
		}

		virtual void OnClose() override
		{
			*Alive = false;
			FVKit::EndTextField(Field);
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (In.WasPressed(EKeys::Escape))
			{
				if (Field.bFocused) { FVKit::EndTextField(Field); return; }
				if (State == EState::Working) { return; }   // the download is running: stay
				PendingAct = EAct::Back;
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!Game) { return; }
			const bool bP = FV::IsPortrait();
			Act = PendingAct;
			PendingAct = EAct::None;
			if (ServerUsable()) { bRetried = false; }
			const FFVRect Win = bP ? FV::WindowRect() : FFVRect(110.f, 34.f, 420.f, 306.f);
			if (D.Window(Win, TEXT("Восстановление")) && State != EState::Working) { Act = EAct::Close; }
			DrawBody(D, Win, bP);
			if (bP)
			{
				FFVRect Actions;
				if (D.ActionBar(Win, &Actions) && State != EState::Working) { Act = EAct::Back; }
				DrawMainButton(D, Actions, true);
			}
			RunAct();
		}

	private:
		TSharedRef<bool> Alive;
		bool bFromSettings = false;
		FVKit::FFVTextField Field;
		EState State = EState::Idle;
		FString Message;
		double StateTime = 0.0;
		int32 TickCount = 0;
		EAct Act = EAct::None;
		EAct PendingAct = EAct::None;
		bool bRetried = false;       // «Подключиться» was pressed (a failed attempt shows a hint)

		UFVOnline* Online() const { return Game ? Game->GetOnline() : nullptr; }
		/** The server can be used: not the offline mode, no connection trouble, no required update (§10). */
		bool ServerUsable() const { return OfflineOf(Online()) == EOffline::None; }
		bool CodeReady() const { return CleanCode(Field.Text).Len() == CodeLen; }

		void SetState(EState S, const FString& Msg = FString())
		{
			State = S;
			Message = Msg;
			StateTime = Game ? Game->GetTime() : 0.0;
		}

		void RunAct()
		{
			const EAct A = Act;
			Act = EAct::None;
			if (A == EAct::None || !Game) { return; }
			switch (A)
			{
			case EAct::Close:
			{
				FVKit::EndTextField(Field);
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
			case EAct::Back:
			{
				FVKit::EndTextField(Field);
				UFVGame* G = Game;
				if (bFromSettings && G->IsInGame()) { G->OpenScreen(EFVScreen::Pause); }
				else { G->CloseScreen(); }
				return;
			}
			case EAct::Submit: AskConfirm(); return;
			default: return;
			}
		}

		void AskConfirm()
		{
			if (State == EState::Working) { return; }
			if (State == EState::Success) { Act = EAct::None; UFVGame* G = Game; G->CloseScreen(); return; }
			const FString Code = CleanCode(Field.Text);
			if (Code.Len() != CodeLen)
			{
				SetState(EState::Failed, FString::Printf(TEXT("В коде должно быть %d символов (буквы и цифры) — сейчас %d."), CodeLen, Code.Len()));
				FVUI::Sfx(EFVSfx::Error);
				return;
			}
			if (!ServerUsable())
			{
				SetState(EState::Failed, TEXT("Восстановление работает только при связи с сервером."));
				FVUI::Sfx(EFVSfx::Error);
				return;
			}
			FVKit::EndTextField(Field);
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			TSharedRef<bool> AliveFlag = Alive;
			FRestoreScreen* Self = this;
			Game->Confirm(TEXT("Заменить прогресс?"),
				FString::Printf(TEXT("Текущий прогресс на этом устройстве (уровень, улов, монеты, дом) будет заменён сохранением с сервера по коду %s. Отменить это нельзя."), *Code),
				[WeakGame, AliveFlag, Self, Code]()
				{
					if (!WeakGame.IsValid() || !*AliveFlag) { return; }
					Self->StartRestore(Code);
				},
				TEXT("Заменить"), TEXT("Отмена"));
		}

		void StartRestore(const FString& Code)
		{
			UFVOnline* On = Online();
			if (!On) { return; }
			SetState(EState::Working);
			FVUI::Sfx(EFVSfx::Click);
			TWeakObjectPtr<UFVGame> WeakGame(Game);
			TSharedRef<bool> AliveFlag = Alive;
			FRestoreScreen* Self = this;
			On->RestoreByCode(Code, [WeakGame, AliveFlag, Self](bool bOk, const FString& Msg)
			{
				UFVGame* G = WeakGame.Get();
				if (!G) { return; }
				const bool bOpen = *AliveFlag;
				if (bOk)
				{
					FVUI::Sfx(EFVSfx::LevelUp, 0.8f);
					const UFVSaveGame* P = G->GetProfile();
					const FString Name = P ? P->PlayerName : FString();
					G->Toast(Name.IsEmpty() ? FString(TEXT("Сохранение восстановлено!")) : FString::Printf(TEXT("Сохранение восстановлено. С возвращением, %s!"), *Name), FVPal::Gold(), EFVIcon::Check);
					if (bOpen) { Self->SetState(EState::Success, TEXT("Готово! Прогресс восстановлен — можно продолжать рыбалку.")); }
				}
				else
				{
					FVUI::Sfx(EFVSfx::Error);
					const FString Text = Msg.IsEmpty() ? FString(TEXT("Код не подошёл: он неверный, уже использован или устарел.")) : Msg;
					if (bOpen) { Self->SetState(EState::Failed, Text); }
					else { G->Toast(Text, FV::Col(255, 150, 130), EFVIcon::Exclaim); }
				}
			});
		}

		void DrawMainButton(FVDraw& D, const FFVRect& R, bool bP)
		{
			const float TS = bP ? FV::Ui::Body() + 1.f : 9.f;
			if (State == EState::Success)
			{
				if (D.Button(R, TEXT("Продолжить"), EFVButton::Primary, true, TS)) { Act = EAct::Close; }
				return;
			}
			const bool bBusy = State == EState::Working;
			if (D.Button(R, bBusy ? FString(TEXT("Загружаем…")) : FString(TEXT("Восстановить")), EFVButton::Primary, !bBusy && CodeReady() && ServerUsable(), TS))
			{
				Act = EAct::Submit;
			}
		}

		void DrawBody(FVDraw& D, const FFVRect& Win, bool bP)
		{
			const double T = Game->GetTime();
			const float Pad = bP ? 14.f : 14.f;
			const float X = Win.X + Pad;
			const float W = Win.W - Pad * 2.f;
			const float HS = bP ? FV::Ui::Header() : 10.f;
			const float BS = bP ? FV::Ui::Body() : 7.5f;
			const float SS = bP ? FV::Ui::Small() : 6.5f;
			float Y = Win.Y + (bP ? 20.f : 12.f);

			// ---- heading with a chest
			const float IS = bP ? 36.f : 24.f;
			IconAt(D, EFVIcon::Chest, FFVRect(X, Y, IS, IS), 2.f);
			const float TX = X + IS + 10.f;
			D.Text(TEXT("Вернуть прогресс по коду"), FVector2D(TX, Y + (bP ? 2.f : 0.f)), HS, FVPal::Ink(), EFVAlign::Left, true, true);
			const UFVSaveGame* P = Game->GetProfile();
			const FString Num = (P && P->OnlineTag > 0) ? FString::Printf(TEXT("Ваш номер игрока: #%d"), P->OnlineTag) : FString(TEXT("Номер игрока появится после связи с сервером"));
			D.Text(D.Ellipsize(Num, X + W - TX, SS, true), FVector2D(TX, Y + (bP ? 2.f : 0.f) + D.LineHeight(HS)), SS, FV::Col(120, 72, 20), EFVAlign::Left, false, true);
			Y += IS + (bP ? 12.f : 8.f);

			// ---- offline: the friendly card instead of the form (a running / finished restore keeps its progress view)
			if (!ServerUsable() && (State == EState::Idle || State == EState::Failed))
			{
				const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 10.f : Win.Bottom() - 10.f;
				UFVOnline* On = Online();
				OfflineCard(D, FFVRect(Win.X + 6.f, Y, Win.W - 12.f, FMath::Max(60.f, Bottom - Y)), bP, On, OfflineOf(On),
					TEXT("Вернуть прогресс по коду можно, как только игра подключится к серверу."), bRetried);
				return;
			}

			// ---- explanation
			const FString Expl = TEXT("Сменили телефон или потеряли прогресс? Напишите разработчику игры и назовите свой номер игрока — он пришлёт одноразовый код восстановления.");
			Y += D.TextWrapped(Expl, FVector2D(X, Y), W, BS, FVPal::Ink(), false, 1.12f) + (bP ? 8.f : 5.f);
			{
				const FString Warn = TEXT("Внимание: сохранение с сервера заменит весь текущий прогресс на этом устройстве.");
				const float WH = D.MeasureWrapped(Warn, W - 30.f, SS, true);
				const FFVRect WR(X, Y, W, FMath::CeilToFloat(WH + (bP ? 12.f : 8.f)));
				Card(D, WR, FV::Col(206, 140, 30), FV::Col(255, 238, 180));
				IconAt(D, EFVIcon::Exclaim, FFVRect(WR.X + 5.f, FMath::RoundToFloat(WR.Center().Y - 7.f), 14.f, 14.f), 1.f);
				D.TextWrapped(Warn, FVector2D(WR.X + 24.f, WR.Y + (bP ? 6.f : 4.f)), W - 30.f, SS, FV::Col(120, 72, 20), true);
				Y += WR.H + (bP ? 14.f : 8.f);
			}

			// ---- the code field
			D.Text(TEXT("Код восстановления"), FVector2D(X, Y), SS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			const FString Clean = CleanCode(Field.Text);
			D.Text(FString::Printf(TEXT("%d / %d"), Clean.Len(), CodeLen), FVector2D(X + W, Y), SS, Clean.Len() == CodeLen ? InkGood() : FVPal::InkSoft(), EFVAlign::Right, false, true);
			Y += D.LineHeight(SS) + 2.f;
			const float FH = bP ? 40.f : 22.f;
			const bool bLocked = State == EState::Working || State == EState::Success;
			if (bLocked)
			{
				// read-only look while working / after success
				const FFVRect FR(X, Y, W, FH);
				Card(D, FR, FV::Col(186, 124, 66), FV::Col(240, 226, 200));
				D.Text(Clean, FVector2D(FR.X + 8.f, FMath::RoundToFloat(FR.Center().Y - D.LineHeight(bP ? FV::Ui::Header() : 10.f) * 0.5f)), bP ? FV::Ui::Header() : 10.f, FVPal::InkSoft(), EFVAlign::Left, false, true);
			}
			else if (FVKit::TextField(D, FFVRect(X, Y, W, FH), Field, TEXT("Например: K7PX2M9Q")))
			{
				if (CodeReady()) { Act = EAct::Submit; }
			}
			bool bNonLatin = false;
			for (TCHAR C : Field.Text) { bNonLatin |= C >= 128; }
			if (!bLocked && bNonLatin)
			{
				D.Text(D.Ellipsize(TEXT("Переключите клавиатуру на латиницу: в коде только латинские буквы и цифры"), W, SS, true), FVector2D(X, Y + FH + 2.f), SS, InkBad(), EFVAlign::Left, false, true);
			}
			Y += FH + D.LineHeight(SS) + (bP ? 10.f : 5.f);

			// ---- landscape button (portrait: in the footer)
			if (!bP)
			{
				const float BW = 150.f;
				DrawMainButton(D, FFVRect(X + W - BW, Y, BW, 22.f), false);
				Y += 22.f + 8.f;
			}

			// ---- progress / result
			const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 10.f : Win.Bottom() - 10.f;
			if (State == EState::Working)
			{
				const float H = bP ? 64.f : 40.f;
				const FFVRect R(X, FMath::Min(Y, Bottom - H), W, H);
				Card(D, R, FV::Col(186, 124, 66), FV::Col(255, 244, 222));
				Spinner(D, FVector2D(R.X + (bP ? 26.f : 18.f), R.Center().Y), bP ? 10.f : 7.f, T);
				D.Text(TEXT("Загружаем сохранение с сервера…"), FVector2D(R.X + (bP ? 48.f : 34.f), FMath::RoundToFloat(R.Center().Y - D.LineHeight(BS) - 1.f)), BS, FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(TEXT("Не закрывайте игру"), FVector2D(R.X + (bP ? 48.f : 34.f), FMath::RoundToFloat(R.Center().Y + 1.f)), SS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				// an indeterminate progress stripe
				const float BarW = R.W - 16.f;
				const float K = float(FMath::Fmod(T - StateTime, 1.4) / 1.4);
				D.Rect(FFVRect(R.X + 8.f, R.Bottom() - 6.f, BarW, 2.f), FV::Col(214, 170, 120));
				const float SegW = BarW * 0.3f;
				const float SX = R.X + 8.f + (BarW + SegW) * K - SegW;
				const float L = FMath::Max(R.X + 8.f, SX), Rr = FMath::Min(R.X + 8.f + BarW, SX + SegW);
				if (Rr > L) { D.Rect(FFVRect(FMath::RoundToFloat(L), R.Bottom() - 6.f, FMath::RoundToFloat(Rr - L), 2.f), FVPal::Gold()); }
			}
			else if ((State == EState::Failed || State == EState::Success) && !Message.IsEmpty())
			{
				const bool bOk = State == EState::Success;
				const float MH = D.MeasureWrapped(Message, W - 34.f, BS, true);
				const float H = FMath::CeilToFloat(MH + (bP ? 18.f : 12.f));
				const FFVRect R(X, FMath::Min(Y, Bottom - H), W, H);
				// a short pop when the result arrives
				const float Age = float(T - StateTime);
				const float Pop = Age < 0.2f ? 1.f + (0.2f - Age) * 0.3f : 1.f;
				const FFVRect RR(FMath::RoundToFloat(R.Center().X - R.W * Pop * 0.5f), FMath::RoundToFloat(R.Center().Y - R.H * Pop * 0.5f), FMath::RoundToFloat(R.W * Pop), FMath::RoundToFloat(R.H * Pop));
				Card(D, RR, bOk ? FV::Col(96, 150, 56) : FV::Col(170, 70, 50), bOk ? FV::Col(214, 238, 160) : FV::Col(250, 214, 190));
				IconAt(D, bOk ? EFVIcon::Check : EFVIcon::Exclaim, FFVRect(R.X + 7.f, FMath::RoundToFloat(R.Center().Y - 8.f), 16.f, 16.f), 1.f);
				D.TextWrapped(Message, FVector2D(R.X + 28.f, R.Y + (bP ? 9.f : 6.f)), W - 34.f, BS, bOk ? InkGood() : InkBad(), true);
			}
			else if (State == EState::Idle)
			{
				D.TextWrapped(TEXT("Код состоит из 8 латинских букв и цифр. Он одноразовый и действует 7 дней."), FVector2D(X, Y), W, SS, FVPal::InkSoft(), false);
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeRestoreScreen(UFVGame* Game)
{
	return MakeUnique<FVRestoreScreenImpl::FRestoreScreen>(Game);
}
