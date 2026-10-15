// UFVGame — the game "brain": subsystems, screens, clock & weather, modals & toasts, catch handling, saving,
// world HUD (top bar + portrait button dock; update 3: no hint bar, the guide «?» explains everything) and the
// autotest hook.

#include "FVGame.h"
#include "FVAudio.h"
#include "FVAutoTest.h"
#include "FVDatabase.h"
#include "FVDatabaseU6.h"        // 0.6.2: rods unlocked by achievements
#include "FVFramework.h"
#include "FVGameU6.h"          // update 6 hooks (GAME-FLOW)
#include "FVOnboarding.h"
#include "FVOnline.h"
#include "FVSaveGame.h"
#include "FVSprites.h"
#include "FVTextInput.h"
#include "FVTouch.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace FVTextInput
{
	/** FVTextInput.cpp: phones — a preview of the text being typed above the OS keyboard (not in FVTextInput.h yet). */
	void DrawOverlay(FVDraw& D);
}

namespace FVGameLocal
{
	UFVGame* GFVGame = nullptr;

	constexpr float ToastLife = 3.2f;
	constexpr float ToastIn = 0.25f;
	constexpr float ToastOut = 0.35f;
	constexpr int32 ToastMaxVisible = 5;
	constexpr float AutosaveInterval = 60.f;
	constexpr float ModalInputDelay = 0.2f;     // ignore clicks/keys right after a modal appears (anti mis-click)

	/** Russian plural form: 1 монета, 2 монеты, 5 монет. */
	const TCHAR* RuPlural(int64 N, const TCHAR* One, const TCHAR* Few, const TCHAR* Many)
	{
		const int64 A = (N < 0 ? -N : N) % 100;
		const int64 B = A % 10;
		if (A > 10 && A < 20) { return Many; }
		if (B == 1) { return One; }
		if (B >= 2 && B <= 4) { return Few; }
		return Many;
	}

	FString CoinsText(int64 N)
	{
		return FString::Printf(TEXT("%s %s"), *FV::Money(N), RuPlural(N, TEXT("монета"), TEXT("монеты"), TEXT("монет")));
	}

	float EaseOutCubic(float T) { T = FMath::Clamp(T, 0.f, 1.f); const float U = 1.f - T; return 1.f - U * U * U; }
	float EaseInCubic(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * T; }
	float EaseOutBack(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		const float C1 = 1.70158f, C3 = C1 + 1.f;
		const float U = T - 1.f;
		return 1.f + C3 * U * U * U + C1 * U * U;
	}

	/** Word wrap using the renderer's font metrics. Empty paragraphs produce empty lines. */
	void WrapText(const FVDraw& D, const FString& Text, float MaxW, float Size, bool bBold, TArray<FString>& Out)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		TArray<FString> Paragraphs;
		Text.ParseIntoArray(Paragraphs, TEXT("\n"), false);
		for (const FString& P : Paragraphs)
		{
			TArray<FString> Words;
			P.ParseIntoArrayWS(Words);
			FString Line;
			for (const FString& W : Words)
			{
				const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
				if (!Line.IsEmpty() && D.MeasureText(Try, Size, bBold).X > MaxW)
				{
					Out.Add(Line);
					Line = W;
				}
				else
				{
					Line = Try;
				}
			}
			Out.Add(Line);
		}
	}

	/**
	 * Splits Text into two lines that both fit MaxW: prefers a break at a " · " separator (dropped), otherwise the
	 * word break with the most balanced lines. Returns false when two lines are not enough.
	 */
	bool SplitTwoLines(const FVDraw& D, const FString& Text, float MaxW, float Size, bool bBold, TArray<FString>& Out)
	{
		Out.Reset();
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		if (Words.Num() < 2)
		{
			return false;
		}
		auto Join = [&Words](int32 From, int32 To)
		{
			FString S;
			for (int32 i = From; i < To; ++i)
			{
				if (!S.IsEmpty()) { S += TEXT(" "); }
				S += Words[i];
			}
			return S;
		};
		float BestScore = TNumericLimits<float>::Max();
		for (int32 k = 1; k < Words.Num(); ++k)
		{
			const bool bSepAfter = Words[k] == TEXT("·");
			const bool bSepBefore = Words[k - 1] == TEXT("·");
			const FString A = Join(0, bSepBefore ? k - 1 : k);
			const FString B = Join(bSepAfter ? k + 1 : k, Words.Num());
			if (A.IsEmpty() || B.IsEmpty())
			{
				continue;
			}
			const float WA = D.MeasureText(A, Size, bBold).X;
			const float WB = D.MeasureText(B, Size, bBold).X;
			if (WA > MaxW || WB > MaxW)
			{
				continue;
			}
			const float Score = FMath::Max(WA, WB) - ((bSepAfter || bSepBefore) ? 10000.f : 0.f);
			if (Score < BestScore)
			{
				BestScore = Score;
				Out = { A, B };
			}
		}
		return Out.Num() == 2;
	}

	// ---------------------------------------------------------------- portrait HUD pieces (phones held upright)

	/**
	 * Big dock button (Stardew toolbar style): a raised wooden-parchment key with a big pixel icon and a short label.
	 * Update 3: the key uses the whole dock height; the icon takes the largest pixel-friendly scale (x3 / x2.5 / x2)
	 * that fits above the label. Pressed look while the finger/LMB is down on it; bDim = greyed (still clickable: the
	 * game explains why it cannot open right now). Returns true when clicked (on release).
	 */
	bool DockButton(FVDraw& D, UFVSprites& Sp, const FFVRect& R, EFVIcon Icon, const FString& Label, bool bDim)
	{
		const bool bHover = D.Hover(R);
		const bool bDown = bHover && D.Input().bLmbDown && !D.IsDragGesture();
		const float Lift = bDown ? 1.f : 3.f;                 // visible thickness of the key's front side
		const float Sink = 3.f - Lift;                        // pressed: the key top moves down
		const FFVRect Body(R.X, R.Y + Sink, R.W, R.H - Sink);

		FLinearColor Face = bDim ? FV::Col(206, 180, 150) : FV::Col(246, 204, 138);
		if (bHover && !bDown && !bDim && !FV::IsTouchUI()) { Face = FV::Col(255, 222, 164); }
		if (bDown) { Face = FV::Col(230, 182, 116); }
		const FLinearColor Side = bDim ? FV::Col(150, 118, 92) : FV::Col(178, 104, 48);

		if (!bDown) { D.Rect(FFVRect(R.X + 2.f, R.Bottom(), R.W - 2.f, 2.f), FV::Col(20, 10, 4, 90)); }
		D.Rect(Body, FVPal::WoodDark());
		const FFVRect In = Body.Inset(1.f);
		D.Rect(In, Side);
		const FFVRect F(In.X, In.Y, In.W, In.H - Lift);
		D.Rect(F, Face);
		if (!bDown)
		{
			D.Rect(FFVRect(F.X, F.Y, F.W, 1.f), FV::Col(255, 240, 204));
			D.Rect(FFVRect(F.X, F.Y + 1.f, 1.f, F.H - 1.f), FV::Col(255, 232, 186, 170));
		}
		else
		{
			D.Rect(FFVRect(F.X, F.Y, F.W, 1.f), FV::Col(190, 130, 70));
		}
		// inner engraved frame (Stardew slot look)
		D.Frame(FFVRect(F.X + 3.f, F.Y + 3.f, F.W - 6.f, F.H - 6.f), FV::Col(214, 150, 88, bDim ? 60 : 110), 1.f);

		// label at the bottom, sized to the key (shrinks to fit long words like «Достижения»)
		float TS = FMath::Clamp(FMath::RoundToFloat(F.H * 0.5f) / 4.f, 7.5f, 9.f);
		FVector2D Sz = D.MeasureText(Label, TS, true);
		while (Sz.X > F.W - 6.f && TS > 5.5f)
		{
			TS -= 0.25f;
			Sz = D.MeasureText(Label, TS, true);
		}
		const float LabelY = FMath::RoundToFloat(F.Bottom() - 5.f - Sz.Y);

		// icon: centred in the space above the label
		const FFVSprite& S = Sp.Icon(Icon);
		const FLinearColor Tint = bDim ? FLinearColor(0.75f, 0.72f, 0.7f, 0.55f) : FLinearColor::White;
		if (S.IsValid())
		{
			const float Top = F.Y + 4.f;
			const float Avail = FMath::Max(8.f, LabelY - 1.f - Top);
			const float Base = FMath::Max(1.f, FMath::Max(S.W, S.H));
			float Mul = (Base <= 16.f) ? 2.f : 1.f;
			for (const float M : { 3.f, 2.5f, 2.f })
			{
				if (Base * M <= Avail && Base * M <= F.W - 10.f)
				{
					Mul = M;
					break;
				}
			}
			D.SpriteCentered(S, FVector2D(FMath::RoundToFloat(F.X + F.W * 0.5f), FMath::RoundToFloat(Top + Avail * 0.5f)), Mul, Tint);
		}
		D.Text(Label, FVector2D(FMath::RoundToFloat(F.X + F.W * 0.5f), LabelY), TS, bDim ? FV::Col(128, 106, 90) : FVPal::Ink(), EFVAlign::Center, !bDim, true);

		if (D.Clicked(R))
		{
			if (UFVAudio* A = UFVAudio::Get()) { A->Play(EFVSfx::Click); }
			return true;
		}
		return false;
	}

	/** Width of a Badge() pill. */
	float BadgeWidth(const FVDraw& D, const FString& Text, const FFVSprite* Icon = nullptr)
	{
		const float IconW = (Icon && Icon->IsValid()) ? 11.f : 0.f;
		return FMath::RoundToFloat(FMath::Max(13.f, D.MeasureText(Text, 7.f, true).X + 8.f + IconW));
	}

	/** Small counter pill (e.g. "3/12") with its top-right corner at RightTop; optional sprite before the text. */
	void Badge(FVDraw& D, const FVector2D& RightTop, const FString& Text, const FLinearColor& Fill, const FFVSprite* Icon = nullptr)
	{
		const float TS = 7.f;
		const FVector2D Sz = D.MeasureText(Text, TS, true);
		const float IconW = (Icon && Icon->IsValid()) ? 11.f : 0.f;
		const float H = 13.f;
		const float W = BadgeWidth(D, Text, Icon);
		const FFVRect R(FMath::RoundToFloat(RightTop.X - W), FMath::RoundToFloat(RightTop.Y), W, H);
		D.Rect(R.Offset(1.f, 1.f), FV::Col(20, 10, 4, 110));
		D.Rect(R, FV::Col(52, 26, 14));
		D.Rect(R.Inset(1.f), Fill);
		D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Alpha(FVPal::White(), 0.35f));
		// rounded corners
		for (const FVector2D& C : { R.Pos(), FVector2D(R.Right() - 1.f, R.Y), FVector2D(R.X, R.Bottom() - 1.f), FVector2D(R.Right() - 1.f, R.Bottom() - 1.f) })
		{
			D.Rect(FFVRect(C.X, C.Y, 1.f, 1.f), FV::Col(52, 26, 14, 120));
		}
		float TX = R.X + 4.f;
		if (IconW > 0.f)
		{
			const float M = 10.f / FMath::Max(Icon->W, Icon->H);
			D.SpriteCentered(*Icon, FVector2D(TX + 5.f, R.Y + H * 0.5f), M);
			TX += IconW;
		}
		D.TextOutlined(Text, FVector2D(FMath::RoundToFloat(TX + (R.Right() - 4.f - TX) * 0.5f), FMath::RoundToFloat(R.Y + (H - Sz.Y) * 0.5f)), TS, FVPal::White(), EFVAlign::Center, FV::Col(40, 20, 10));
	}

	/** Round red counter (e.g. unseen guide entries) centred at C; Pop = extra radius (attention pulse). */
	void CountDot(FVDraw& D, const FVector2D& C, int32 N, float Pop = 0.f)
	{
		if (N <= 0)
		{
			return;
		}
		const FString Txt = N > 9 ? FString(TEXT("9+")) : FString::FromInt(N);
		const float TS = FV::IsPortrait() ? 7.f : 6.f;
		const FVector2D Sz = D.MeasureText(Txt, TS, true);
		const float Rad = FMath::Max(FV::IsPortrait() ? 6.f : 5.f, float(Sz.X) * 0.5f + 2.5f) + Pop;
		D.Circle(C + FVector2D(0.5f, 1.f), Rad + 1.f, FV::Col(20, 10, 4, 120));
		D.Circle(C, Rad + 1.f, FV::Col(90, 20, 14));
		D.Circle(C, Rad, FV::Col(214, 52, 40));
		D.Circle(C - FVector2D(0.f, Rad * 0.35f), Rad * 0.45f, FV::Col(255, 150, 130, 80));
		D.Text(Txt, FVector2D(FMath::RoundToFloat(C.X), FMath::RoundToFloat(C.Y - float(Sz.Y) * 0.5f)), TS, FVPal::White(), EFVAlign::Center, false, true);
	}

	/** World HUD buttons (same order in both layouts). Name = dock label, Tip = desktop tooltip with the hotkey,
	    Context = screen context (UFVGame::OpenScreenWith). Update 3: one «Инвентарь» button (backpack or keepnet: where
	    the last received item went) and the guide «?» next to the menu. The portrait dock shows the first HudDockCount;
	    the guide and the menu live in the portrait top strip. No global shop: trading happens at the stalls.
	    Update 4: the fisherman's profile (drawn as his face, with the unread-mail badge) before the menu.
	    Update 6 (§12.1, Р19/Р22): the top bar holds no buttons any more; Меню / Профиль / Лидеры / Учебник form a column
	    at the top-right under the bar (landscape: a second column with the first HudDockCount buttons next to it). */
	struct FHudButton { EFVIcon Icon; EFVScreen Screen; const TCHAR* Context; const TCHAR* Tip; const TCHAR* Name; };
	static const FHudButton HudButtons[] =
	{
		{ EFVIcon::Map, EFVScreen::Map, nullptr, TEXT("Карта [M]"), TEXT("Карта") },
		{ EFVIcon::Backpack, EFVScreen::Inventory, nullptr, TEXT("Инвентарь [I / B]"), TEXT("Инвентарь") },
		{ EFVIcon::Wardrobe, EFVScreen::Wardrobe, nullptr, TEXT("Гардероб [G]"), TEXT("Гардероб") },
		{ EFVIcon::Book, EFVScreen::Collection, nullptr, TEXT("Коллекция рыб [C]"), TEXT("Коллекция") },
		{ EFVIcon::Trophy, EFVScreen::Achievements, nullptr, TEXT("Достижения [J]"), TEXT("Достижения") },
		{ EFVIcon::Question, EFVScreen::Tutorial, nullptr, TEXT("Учебник [F1]"), TEXT("Учебник") },
		{ EFVIcon::Heart, EFVScreen::Profile, nullptr, TEXT("Профиль рыбака [P]"), TEXT("Профиль") },
		{ EFVIcon::Leaders, EFVScreen::Leaderboard, nullptr, TEXT("Лидеры [L]"), TEXT("Лидеры") },   // update 6
		{ EFVIcon::Menu, EFVScreen::Pause, nullptr, TEXT("Меню [Esc]"), TEXT("Меню") },
	};
	constexpr int32 HudDockCount = 5;        // portrait dock: Карта, Инвентарь, Гардероб, Коллекция, Достижения
	constexpr int32 HudInventoryIndex = 1;
	constexpr int32 HudGuideIndex = 5;
	constexpr int32 HudProfileIndex = 6;
	constexpr int32 HudLeadersIndex = 7;     // update 6: the leaderboard button of the column
	constexpr int32 HudPauseIndex = int32(UE_ARRAY_COUNT(HudButtons)) - 1;   // the menu is always the last entry
	/** Update 6 (§12.1): the column at the top-right, top to bottom. */
	static const int32 HudColumnOrder[] = { HudPauseIndex, HudProfileIndex, HudLeadersIndex, HudGuideIndex };
	/** Update 6: HUD column sizes (virtual px): portrait keys are big for the thumb, landscape has two narrow columns. */
	constexpr float HudColBtnPortrait = 40.f, HudColGapPortrait = 4.f;
	constexpr float HudColBtnLandscape = 28.f, HudColGapLandscape = 3.f;
	/** Update 6 (Р1): «Опыт на максимуме — плывите в Город» is toasted once per session after a catch. */
	bool GXpFrozenToastShown = false;

	/** Update 6 (§8.1): short names of the local sea state (fallback when the table has none). */
	FString SeaName(EFVSeaState S)
	{
		FString N = FVDBU6::SeaStateName(S);
		if (!N.IsEmpty())
		{
			return N;
		}
		switch (S)
		{
		case EFVSeaState::Wind: return TEXT("Ветер");
		case EFVSeaState::Storm: return TEXT("Шторм");
		case EFVSeaState::Fog: return TEXT("Туман");
		default: return TEXT("Штиль");
		}
	}

	EFVIcon SeaIcon(EFVSeaState S)
	{
		switch (S)
		{
		case EFVSeaState::Wind: return EFVIcon::SeaWind;
		case EFVSeaState::Storm: return EFVIcon::SeaStorm;
		case EFVSeaState::Fog: return EFVIcon::SeaFog;
		default: return EFVIcon::SeaCalm;
		}
	}

	/** Tooltip lines of the local conditions (empty on classic locations). */
	FString LocalWeatherText(const FFVLocalWeather& W)
	{
		if (!W.bValid)
		{
			return FString();
		}
		FString T = FString::Printf(TEXT("\nМоре: %s"), *SeaName(W.Sea));
		if (W.bTide) { T += TEXT(", вечерний прилив"); }
		if (W.bSpring) { T += TEXT("\nБьёт подводный ключ"); }
		if (W.bDark) { T += TEXT("\nТемно — рыба клюёт иначе"); }
		return T;
	}
	const FName SetupContext(TEXT("setup"));  // EFVScreen::Profile context of the first-run name choice

	// ---- update 4: app lifecycle (FCoreDelegates) — one game instance, so file-level handles
	FDelegateHandle GDeactivateHandle;
	FDelegateHandle GBackgroundHandle;
	FDelegateHandle GReactivateHandle;   // 0.4.1: back in the foreground -> the offline mode checks the connection
	FDelegateHandle GForegroundHandle;

	// ---- 0.5 crash fix: Slate UI screenshots (autotest) only while Slate really draws the game window
	FDelegateHandle GViewportRenderedHandle;
	int32 GShotWindowState = -1;   // last logged window state: 1 = drawn, 0 = not drawn, -1 = not logged yet

	/**
	 * FScreenshotRequest with bShowUI = true ends in FSlateApplication::TakeScreenshot: it hands the Slate renderer a
	 * pointer to a bitmap on the stack of UGameViewportClient::ProcessScreenShots and draws only the game window. When
	 * Slate skips that window (minimized, hidden, zero size, a modal window on top) the renderer never clears the request
	 * (FSlateRHIRenderer::DrawWindows_Private resets it only for a drawn window): the screenshot fails silently and the
	 * next frame that draws the window again reads the back buffer into the dead stack array — UE 5.8.3:
	 * EXCEPTION_ACCESS_VIOLATION writing 0x0 in D3D12RHI (A2B10G10R10 -> FColor) under SlateRHIRenderer,
	 * breadcrumb 'RenderGraphExecute - Slate' (FV_05_port.log: 43 of 49 shots missing, then the crash).
	 * Returns false (with the reason) while such a screenshot must not be taken.
	 */
	bool CanTakeUIShot(FString& OutWhy)
	{
		if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport)
		{
			return true;   // no Slate: the engine reads the viewport itself (no pointer is left behind)
		}
		const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
		if (!Window.IsValid())
		{
			return true;   // no window: ProcessScreenShots takes the viewport path as well
		}
		FSlateApplication& App = FSlateApplication::Get();
		const bool bOffScreen = App.IsRenderingOffScreen();   // -RenderOffScreen: hidden / minimized windows are drawn too
		if (!bOffScreen && !Window->IsVisible())
		{
			OutWhy = TEXT("the game window is hidden");
			return false;
		}
		if (!bOffScreen && Window->IsWindowMinimized())
		{
			OutWhy = TEXT("the game window is minimized");
			return false;
		}
		const FVector2f Size = Window->GetViewportSize();
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			OutWhy = TEXT("the game window has no size");
			return false;
		}
		if (App.GetActiveModalWindow().IsValid())
		{
			OutWhy = TEXT("a modal window is open");
			return false;
		}
		return true;
	}

	/** FViewport::Draw broadcasts this right before UGameViewportClient::ProcessScreenShots — the last moment to drop a
	    UI screenshot request that Slate would not draw (see CanTakeUIShot). Also logs when the window stops / starts
	    being drawn, so a log shows why screenshots are missing. */
	void OnViewportRendered(FViewport* Viewport)
	{
		if (!GEngine || !GEngine->GameViewport || GEngine->GameViewport->Viewport != Viewport)
		{
			return;
		}
		FString Why;
		const bool bDrawn = CanTakeUIShot(Why);
		if (GShotWindowState != (bDrawn ? 1 : 0))
		{
			if (!bDrawn)
			{
				UE_LOG(LogFV, Log, TEXT("FV: %s — Slate does not draw it, UI screenshots are skipped until it is back"), *Why);
			}
			else if (GShotWindowState == 0)
			{
				UE_LOG(LogFV, Log, TEXT("FV: the game window is drawn again, UI screenshots are allowed"));
			}
			GShotWindowState = bDrawn ? 1 : 0;
		}
		if (!bDrawn && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::ShouldShowUI())
		{
			UE_LOG(LogFV, Warning, TEXT("FV: screenshot SKIPPED %s: %s (a Slate UI screenshot of an undrawn window corrupts memory)"),
				*FScreenshotRequest::GetFilename(), *Why);
			FScreenshotRequest::Reset();
		}
	}

	/** Part of a sprite (texel rect X0, Y0, W, H inside S.W x S.H) as a sprite of its own (same texture, narrower UVs). */
	FFVSprite CropSprite(const FFVSprite& S, float X0, float Y0, float W, float H)
	{
		FFVSprite C = S;
		if (!S.IsValid() || S.W <= 0.f || S.H <= 0.f)
		{
			return C;
		}
		const FVector2D Span = S.UV1 - S.UV0;
		C.UV0 = S.UV0 + FVector2D(Span.X * X0 / S.W, Span.Y * Y0 / S.H);
		C.UV1 = S.UV0 + FVector2D(Span.X * (X0 + W) / S.W, Span.Y * (Y0 + H) / S.H);
		C.W = W;
		C.H = H;
		return C;
	}

	/**
	 * HUD profile button: the icon-button frame with a tiny "portrait window" (sky, grass) and the fisherman's face — a
	 * crop of his character sprite (hat included) at x1, so the pixels stay crisp in both layouts. Unread mail: a red
	 * counter at the top-right corner. Returns true when clicked (on release).
	 */
	bool ProfileButton(FVDraw& D, UFVSprites& Sp, const UFVSaveGame* P, const FFVRect& R, int32 Unread, double Time, const FString& Tip)
	{
		const bool bClicked = D.IconButton(R, FFVSprite(), false, true, Tip);
		const bool bDown = D.Hover(R) && D.Input().bLmbDown;
		// IconButton's face area: R.Inset(1) minus the 2 px front lip at the bottom (moves down 1 px while pressed)
		const FFVRect F = R.Inset(1.f);
		const FFVRect Face(F.X, F.Y + (bDown ? 1.f : 0.f), F.W, F.H - 2.f);
		const bool bBig = R.W >= 24.f;
		const FFVRect Win = Face.Inset(bBig ? 2.f : 1.f);
		D.Rect(Win.Inset(-1.f), FV::Col(120, 66, 30));
		D.Rect(Win, FV::Col(150, 204, 238));
		D.Rect(FFVRect(Win.X, Win.Y, Win.W, FMath::RoundToFloat(Win.H * 0.4f)), FV::Col(126, 188, 232));
		D.Rect(FFVRect(Win.X, Win.Bottom() - 3.f, Win.W, 3.f), FV::Col(106, 170, 72));
		if (P)
		{
			const FFVSprite& Body = Sp.Character(P->BuildAppearance(), EFVCharAnim::Idle, EFVFacing::Down, 0);
			if (Body.IsValid())
			{
				// head texels: x 9..22, y 14..26 of the 32x48 canvas (hat above it); the crop fills the window at x1
				const float CW = FMath::Min(Win.W, 30.f), CH = FMath::Min(Win.H, 30.f);
				const float X0 = FMath::RoundToFloat(16.f - CW * 0.5f);
				const float Y0 = bBig ? 7.f : 13.f;
				// update 6: the 40 px column key is wider than the crop — centre it, shoulders on the bottom edge
				const float OX = FMath::RoundToFloat((Win.W - CW) * 0.5f), OY = FMath::Max(0.f, Win.H - CH);
				D.Sprite(CropSprite(Body, X0, Y0, CW, CH), FVector2D(Win.X + OX, Win.Y + OY));
			}
		}
		if (Unread > 0)
		{
			const float Pop = FMath::Max(0.f, FMath::Sin(float(Time) * 3.f)) * (bBig ? 0.8f : 0.6f);
			CountDot(D, bBig ? FVector2D(R.Right() - 3.f, R.Y + 6.f) : FVector2D(R.Right() - 1.f, R.Y + 3.f), Unread, Pop);
		}
		return bClicked;
	}

	/** The inventory opens the container that received the last item (UFVSaveGame::LastInventoryTab: 1 = backpack). */
	bool InventoryOpensBackpack(const UFVSaveGame* P) { return P && P->LastInventoryTab == 1; }
	FName InventoryContext(const UFVSaveGame* P) { return FName(InventoryOpensBackpack(P) ? TEXT("backpack") : TEXT("keepnet")); }

	FName HudContext(const FHudButton& B, const UFVSaveGame* P)
	{
		if (B.Screen == EFVScreen::Inventory)
		{
			return InventoryContext(P);
		}
		return B.Context ? FName(B.Context) : FName(NAME_None);
	}

	/** Screens the HUD / hotkeys open even while fishing (they only pause the world): the menu, the guide and (update 4)
	    the fisherman's profile and the level list. */
	bool OpensWhileBusy(EFVScreen S)
	{
		return S == EFVScreen::Pause || S == EFVScreen::Tutorial || S == EFVScreen::Profile || S == EFVScreen::Levels
			|| S == EFVScreen::Leaderboard;   // update 6: the HUD column's «Лидеры» (information only)
	}

	/** Hotkey -> screen + context. I / Tab / B = the inventory (tab from LastInventoryTab), F1 / H = the guide.
	    No shop key (stalls only). */
	struct FHotkey { EFVScreen Screen = EFVScreen::None; FName Context; };
	FHotkey HotkeyScreen(const FFVInput& In, const UFVSaveGame* P)
	{
		if (In.WasPressed(EKeys::M)) { return { EFVScreen::Map, NAME_None }; }
		if (In.WasPressed(EKeys::I) || In.WasPressed(EKeys::Tab) || In.WasPressed(EKeys::B)) { return { EFVScreen::Inventory, InventoryContext(P) }; }
		if (In.WasPressed(EKeys::G)) { return { EFVScreen::Wardrobe, NAME_None }; }
		if (In.WasPressed(EKeys::C)) { return { EFVScreen::Collection, NAME_None }; }
		if (In.WasPressed(EKeys::J)) { return { EFVScreen::Achievements, NAME_None }; }
		if (In.WasPressed(EKeys::F1) || In.WasPressed(EKeys::H)) { return { EFVScreen::Tutorial, NAME_None }; }
		if (In.WasPressed(EKeys::P)) { return { EFVScreen::Profile, NAME_None }; }
		if (In.WasPressed(EKeys::L)) { return { EFVScreen::Leaderboard, NAME_None }; }   // update 6
		return FHotkey();
	}

	// ---- "where did the last item go" (Profile->LastInventoryTab) + the HUD guide badge. Core-private state that
	// FVGame.h has no members for (there is one game instance).
	struct FInvSnapshot
	{
		TWeakObjectPtr<UFVSaveGame> Profile;
		int32 Keepnet = -1;
		int32 Backpack = -1;
		double GrowTime = -100.0;            // game time of the last growth (HUD badge hop)
	};
	FInvSnapshot GInvSnap;

	int32 StackUnits(const TArray<FFVInvStack>& Stacks)
	{
		int32 N = 0;
		for (const FFVInvStack& S : Stacks)
		{
			N += FMath::Max(0, S.Count);
		}
		return N;
	}

	/** Compares keepnet / backpack units with the previous frame: the container that grew decides the tab (a fish beats
	    a bonus item that arrived in the same frame). Returns true when LastInventoryTab changed. */
	bool TrackInventoryTab(UFVSaveGame* P, double Now)
	{
		if (!P)
		{
			return false;
		}
		const int32 K = StackUnits(P->GetContainer(EFVContainer::Keepnet));
		const int32 B = StackUnits(P->GetContainer(EFVContainer::Backpack));
		bool bChanged = false;
		if (GInvSnap.Profile.Get() == P && GInvSnap.Keepnet >= 0)
		{
			const bool bK = K > GInvSnap.Keepnet;
			const bool bB = B > GInvSnap.Backpack;
			if (bK || bB)
			{
				GInvSnap.GrowTime = Now;
				const uint8 Want = bK ? 0 : 1;
				if (P->LastInventoryTab != Want)
				{
					P->LastInventoryTab = Want;
					bChanged = true;
				}
			}
		}
		else
		{
			GInvSnap.GrowTime = -100.0;      // another profile (new game / load): just take the snapshot
		}
		GInvSnap.Profile = P;
		GInvSnap.Keepnet = K;
		GInvSnap.Backpack = B;
		return bChanged;
	}

	/** Unseen guide entries (HUD badge): re-counted a few times per second and whenever the screen changes. */
	int32 GuideUnseen(UFVGame* G, double Now, EFVScreen Screen)
	{
		static double LastTime = -100.0;
		static EFVScreen LastScreen = EFVScreen::Count;
		static int32 Cached = 0;
		if (Now - LastTime > 0.4 || Now < LastTime || Screen != LastScreen)
		{
			Cached = FMath::Max(0, FVTutorialUnseenCount(G));
			LastTime = Now;
			LastScreen = Screen;
		}
		return Cached;
	}

	const TCHAR* ScreenName(EFVScreen S)
	{
		switch (S)
		{
		case EFVScreen::None: return TEXT("None");
		case EFVScreen::MainMenu: return TEXT("MainMenu");
		case EFVScreen::Map: return TEXT("Map");
		case EFVScreen::Shop: return TEXT("Shop");
		case EFVScreen::Inventory: return TEXT("Inventory");
		case EFVScreen::Wardrobe: return TEXT("Wardrobe");
		case EFVScreen::Collection: return TEXT("Collection");
		case EFVScreen::Achievements: return TEXT("Achievements");
		case EFVScreen::Pause: return TEXT("Pause");
		case EFVScreen::Chest: return TEXT("Chest");
		case EFVScreen::IceCellar: return TEXT("IceCellar");
		case EFVScreen::Station: return TEXT("Station");
		case EFVScreen::Orders: return TEXT("Orders");
		case EFVScreen::NetSpot: return TEXT("NetSpot");
		case EFVScreen::HomeDecor: return TEXT("HomeDecor");
		case EFVScreen::Cooking: return TEXT("Cooking");
		case EFVScreen::LureCraft: return TEXT("LureCraft");
		case EFVScreen::Knots: return TEXT("Knots");
		case EFVScreen::Digging: return TEXT("Digging");
		case EFVScreen::Tutorial: return TEXT("Tutorial");
		case EFVScreen::Profile: return TEXT("Profile");
		case EFVScreen::Leaderboard: return TEXT("Leaderboard");
		case EFVScreen::Mail: return TEXT("Mail");
		case EFVScreen::Levels: return TEXT("Levels");
		case EFVScreen::RestoreSave: return TEXT("RestoreSave");
		case EFVScreen::Chopping: return TEXT("Chopping");
		case EFVScreen::Sawing: return TEXT("Sawing");
		case EFVScreen::Forging: return TEXT("Forging");
		case EFVScreen::Excavation: return TEXT("Excavation");
		case EFVScreen::Builder: return TEXT("Builder");
		case EFVScreen::YardBuilding: return TEXT("YardBuilding");
		case EFVScreen::GardenBed: return TEXT("GardenBed");
		case EFVScreen::Boat: return TEXT("Boat");
		case EFVScreen::Voyage: return TEXT("Voyage");
		case EFVScreen::CityBoard: return TEXT("CityBoard");
		case EFVScreen::Artel: return TEXT("Artel");
		case EFVScreen::ExpertTackle: return TEXT("ExpertTackle");
		case EFVScreen::Mill: return TEXT("Mill");
		default: return TEXT("?");
		}
	}

	/** Compact duration for the HUD food indicator: "2ч30", "45м". */
	FString ShortDuration(float Minutes)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Minutes - 0.001f));
		if (Total >= 60)
		{
			return FString::Printf(TEXT("%dч%02d"), Total / 60, Total % 60);
		}
		return FString::Printf(TEXT("%dм"), Total);
	}

	/** Tooltip body of the active food buff: effect lines + time left. */
	FString FoodTooltipBody(const FFVItemDef& Food, EFVQuality Q, float MinutesLeft)
	{
		TArray<FString> Lines = FVDB::StatModsLines(Food.Buff.Scaled(FVDB::FoodPower(Q)));
		Lines.Add(FString::Printf(TEXT("Осталось: %s"), *UFVGame::DurationText(MinutesLeft)));
		return FString::Join(Lines, TEXT("\n"));
	}
}

// Update 5 (§3): the «a hard tackle rule kept the legend away» toast lives in FVFishing (FImpl::ShowGatedToast): once
// per location visit and legend (FVFishing::Reset runs on every EnterWorld), never after that legend was caught.

// ============================================================================================
// Lifecycle
// ============================================================================================
UFVGame::UFVGame()
{
}

UFVGame::~UFVGame()
{
	if (FVGameLocal::GFVGame == this)
	{
		FVGameLocal::GFVGame = nullptr;
	}
}

UFVGame* UFVGame::Get()
{
	return FVGameLocal::GFVGame;
}

UWorld* UFVGame::GetWorld() const
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}
	if (APlayerController* P = PC.Get())
	{
		return P->GetWorld();
	}
	if (UObject* Outer = GetOuter())
	{
		return Outer->GetWorld();
	}
	return nullptr;
}

void UFVGame::Init(APlayerController* InPC)
{
	FVGameLocal::GFVGame = this;
	PC = InPC;
	Time = 0.0;

	const bool bAutoTest = FParse::Param(FCommandLine::Get(), TEXT("fvautotest"));
	if (bAutoTest)
	{
		UFVSaveGame::SetAutoTestSlot(true);
		UE_LOG(LogFV, Log, TEXT("FV: autotest mode (-fvautotest), using the autotest save slot"));
	}

	FVDB::Init();

	Sprites = NewObject<UFVSprites>(this, TEXT("FVSprites"));
	Sprites->Init();

	// Profile: the existing save (for Continue) or a fresh one; only saved once a game is started.
	Profile = UFVSaveGame::Load();
	if (!Profile)
	{
		Profile = NewObject<UFVSaveGame>(this, TEXT("FVProfile"));
		Profile->ResetToNewGame();
	}

	Audio = NewObject<UFVAudio>(this, TEXT("FVAudio"));
	Audio->Init(InPC);
	Audio->SetSfxVolume(Profile->SfxVolume);
	Audio->SetMusicVolume(Profile->MusicVolume);

	// ---- update 4: typing (names, search, codes) and the online services (they never block the game)
	FVTextInput::Startup();
	Online = NewObject<UFVOnline>(this, TEXT("FVOnline"));
	Online->Init(this);
	{
		// Phones: the app may be killed in the background — save, then the last sync / backup (best effort).
		TWeakObjectPtr<UFVGame> WeakThis(this);
		auto OnAppPause = [WeakThis]()
		{
			auto Run = [WeakThis]()
			{
				UFVGame* G = WeakThis.Get();
				if (!G)
				{
					return;
				}
				if (G->IsInGame())
				{
					G->SaveNow();
				}
				if (UFVOnline* O = G->GetOnline())
				{
					O->NotifyAppPause();
				}
			};
			if (IsInGameThread())
			{
				Run();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(Run));
			}
		};
		FVGameLocal::GDeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddLambda(OnAppPause);
		FVGameLocal::GBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddLambda(OnAppPause);

		// 0.4.1: back from the background: an offline game asks the server at once instead of waiting for its next ping.
		auto OnAppResume = [WeakThis]()
		{
			auto Run = [WeakThis]()
			{
				UFVGame* G = WeakThis.Get();
				if (UFVOnline* O = G ? G->GetOnline() : nullptr)
				{
					O->NotifyAppResume();
				}
			};
			if (IsInGameThread())
			{
				Run();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(Run));
			}
		};
		FVGameLocal::GReactivateHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddLambda(OnAppResume);
		FVGameLocal::GForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddLambda(OnAppResume);
	}

	// 0.5 crash fix: a UI screenshot request is dropped when Slate would not draw the game window (FVGameLocal::CanTakeUIShot).
	UGameViewportClient::OnViewportRendered().Remove(FVGameLocal::GViewportRenderedHandle);
	FVGameLocal::GShotWindowState = -1;
	FVGameLocal::GViewportRenderedHandle = UGameViewportClient::OnViewportRendered().AddStatic(&FVGameLocal::OnViewportRendered);

	Scene = MakeUnique<FVScene>(this);
	Fishing = MakeUnique<FVFishing>(this);
	if (FV::IsTouchUI())
	{
		Touch = MakeShared<FVTouchControls>();
		// A finger resting on a world object (stall, resource, station...) is a tap on it, never a cast wind-up.
		// (update 6: the finger is in screen px; world objects are hit-tested in world px — big locations scroll)
		Touch->SetCastBlocker([this](const FVector2D& P) { return Scene && Scene->ObjectAt(Scene->ScreenToWorld(P)) != nullptr; });
		// 0.4.1: the practice minigame's «give up» [X] is a button, not the reel; update 6: the HUD button column is UI
		Touch->SetUiTest([this](const FVector2D& P)
		{
			return (Fishing && Fishing->IsUiAt(P)) || (HudColumnRect.W > 0.f && HudColumnRect.Contains(P));
		});
		UE_LOG(LogFV, Log, TEXT("FV: touch controls enabled"));
	}

	// Keep a valid scene loaded at all times (screens may draw it as a backdrop).
	FName StartLoc = Profile->CurrentLocation;
	if (StartLoc.IsNone() || !FVDB::FindLocation(StartLoc))
	{
		StartLoc = FVDB::StartLocation();
	}
	Scene->LoadLocation(StartLoc);

	bInGame = false;
	OpenScreen(EFVScreen::MainMenu);
	Audio->SetMusicTheme(EFVTheme::Count);

	if (bAutoTest)
	{
		AutoTest = MakeShared<FVAutoTest>(this);
	}

	UE_LOG(LogFV, Log, TEXT("FV: game initialized (save %s)"), HasSaveGame() ? TEXT("found") : TEXT("not found"));
}

void UFVGame::Shutdown()
{
	if (bInGame)
	{
		SaveNow();
	}
	AutoTest.Reset();

	// ---- update 4
	FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(FVGameLocal::GDeactivateHandle);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(FVGameLocal::GBackgroundHandle);
	FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(FVGameLocal::GReactivateHandle);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(FVGameLocal::GForegroundHandle);
	FVGameLocal::GDeactivateHandle.Reset();
	FVGameLocal::GBackgroundHandle.Reset();
	FVGameLocal::GReactivateHandle.Reset();
	FVGameLocal::GForegroundHandle.Reset();
	UGameViewportClient::OnViewportRendered().Remove(FVGameLocal::GViewportRenderedHandle);
	FVGameLocal::GViewportRenderedHandle.Reset();
	if (Online)
	{
		Online->Shutdown();
		Online = nullptr;
	}
	FVTextInput::Shutdown();

	if (ActiveScreen)
	{
		ActiveScreen->OnClose();
		ActiveScreen.Reset();
	}
	CurrentScreen = EFVScreen::None;
	RetiredScreens.Reset();
	Modals.Reset();
	Toasts.Reset();

	Fishing.Reset();
	Scene.Reset();

	if (Audio)
	{
		Audio->Shutdown();
	}
	if (Sprites)
	{
		Sprites->Shutdown();
	}
	bInGame = false;
	if (FVGameLocal::GFVGame == this)
	{
		FVGameLocal::GFVGame = nullptr;
	}
	UE_LOG(LogFV, Log, TEXT("FV: game shut down"));
}

// ============================================================================================
// Tick
// ============================================================================================
void UFVGame::Tick(float Dt, const FFVInput& RawInput)
{
	Dt = FMath::Clamp(Dt, 0.f, 0.1f);
	Time += Dt;

	// Screens closed during the previous frame are no longer referenced by anyone.
	RetiredScreens.Reset();

	FFVInput In = RawInput;
	if (AutoTest)
	{
		AutoTest->Tick(Dt, In);
	}

	// ---- update 4: while a text field is focused the keyboard types — no hotkeys, no walking, no Esc-to-pause
	// (the Slate pre-processor already swallows the keys; this also covers keys held since before the field got focus)
	FVTextInput::Tick(Dt);
	if (FVTextInput::IsActive())
	{
		const bool bF11 = In.WasPressed(EKeys::F11);
		In.Pressed.Reset();
		if (bF11)
		{
			In.Pressed.Add(EKeys::F11);
		}
		In.MoveX = 0.f;
		In.MoveY = 0.f;
		In.bActionDown = In.bActionPressed = In.bActionReleased = false;
		In.bInteractPressed = false;
	}

	if (!Scene || !Fishing || !Profile)
	{
		FrameInput = In;
		return;
	}

	// ---- touch: fingers -> joystick / action button / pointer
	const bool bWorldInteractive = bInGame && CurrentScreen == EFVScreen::None && !HasModal();
	if (Touch)
	{
		const bool bIdle = Fishing->GetState() == EFVFishState::Idle;
		Touch->Process(In, Dt, bWorldInteractive, bIdle, bIdle && Scene->CanFishFromHere());
	}

	// ---- world objects: hover highlight, click / tap / E, walk-to and interaction (consumes the click it uses)
	TickWorldInteraction(In);

	// ---- resolve the world "action": Space, or LMB that went down over the world (not over UI).
	// On touch screens the action button is the only action source (a tap on a HUD icon must never cast).
	bool bLmbNewPress = false;
	if (!bWorldInteractive || Touch)
	{
		bLmbActionLatched = false;
	}
	else if (In.bLmbPressed && In.bMouseValid && !bUIHoveredLastFrame)
	{
		bLmbActionLatched = true;
		bLmbNewPress = true;
	}
	const bool bDown = In.bActionDown || (bLmbActionLatched && In.bLmbDown);
	const bool bTapped = (In.bActionPressed || bLmbNewPress) && !bPrevActionDown;
	In.bActionDown = bDown;
	In.bActionPressed = bTapped || (bDown && !bPrevActionDown);
	In.bActionReleased = (bPrevActionDown && !bDown) || (bTapped && !bDown);
	bPrevActionDown = bDown;
	if (!In.bLmbDown)
	{
		bLmbActionLatched = false;
	}
	FrameInput = In;

	// ---- global keys
	if (In.WasPressed(EKeys::F11))
	{
		ToggleFullscreen();
	}

	// ---- modal / screen / world input
	if (HasModal())
	{
		if (ModalAge >= FVGameLocal::ModalInputDelay)
		{
			if (In.WasPressed(EKeys::Enter))
			{
				ChooseModal(0);
			}
			else if (In.WasPressed(EKeys::Escape))
			{
				ChooseModal(-1);
			}
		}
	}
	else if (ActiveScreen)
	{
		FVScreen* Screen = ActiveScreen.Get();
		const EFVScreen Before = CurrentScreen;
		Screen->Tick(Dt, In);
		const bool bStillSame = ActiveScreen.Get() == Screen && CurrentScreen == Before && !HasModal();
		if (bStillSame && !Screen->HandlesEscape())
		{
			if (In.WasPressed(EKeys::Escape))
			{
				if (CurrentScreen != EFVScreen::MainMenu)
				{
					CloseScreen();
				}
			}
			else if (bInGame && CurrentScreen != EFVScreen::MainMenu && !(CurrentScreen == EFVScreen::Profile && ScreenContext == FVGameLocal::SetupContext))
			{
				// (the first-run name choice cannot be left with a hotkey)
				const FVGameLocal::FHotkey Hot = FVGameLocal::HotkeyScreen(In, Profile);
				if (Hot.Screen != EFVScreen::None)
				{
					// The same key closes its screen (the inventory keys close the inventory whatever tab is shown).
					if (Hot.Screen == CurrentScreen && (Hot.Context.IsNone() || Hot.Context == ScreenContext || Hot.Screen == EFVScreen::Inventory))
					{
						CloseScreen();
					}
					else
					{
						OpenScreenWith(Hot.Screen, Hot.Context);
					}
				}
			}
		}
	}
	else if (bInGame)
	{
		if (In.WasPressed(EKeys::Escape))
		{
			const EFVFishState FS = Fishing->GetState();
			// While charging or waiting the fishing module uses ESC to cancel the cast.
			if (FS != EFVFishState::Charging && FS != EFVFishState::Waiting)
			{
				OpenScreen(EFVScreen::Pause);
			}
		}
		else
		{
			const FVGameLocal::FHotkey Hot = FVGameLocal::HotkeyScreen(In, Profile);
			if (Hot.Screen != EFVScreen::None)
			{
				if (FVGameLocal::OpensWhileBusy(Hot.Screen))
				{
					OpenScreenWith(Hot.Screen, Hot.Context);
				}
				else
				{
					TryOpenFromWorldWith(Hot.Screen, Hot.Context);
				}
			}
		}
	}

	// ---- simulation
	const bool bWorldRuns = bInGame && CurrentScreen == EFVScreen::None && !HasModal();
	if (bWorldRuns)
	{
		TickClock(Dt);
		Scene->Tick(Dt, In, !Fishing->IsBusy());
		Fishing->Tick(Dt, In);
	}
	else if (!Scene->GetLocationId().IsNone())
	{
		// Menus pause the game, but the water and critters beneath the dimmed overlay keep living.
		static const FFVInput IdleInput;
		Scene->Tick(Dt, IdleInput, false);
	}

	// ---- update 4b: the first-fishing coach marks (they only watch; Esc, the pause menu and hotkeys stay untouched)
	FVOnboarding::Tick(this, Dt);

	if (Audio)
	{
		const bool bReeling = bWorldRuns && Fishing->GetState() == EFVFishState::Minigame;
		if (bReeling)
		{
			const FFVMinigame& Mini = Fishing->GetMinigame();
			Audio->SetReeling(true, Mini.bFishInBar ? 0.9f : 0.35f);
		}
		else
		{
			Audio->SetReeling(false);
		}
	}

	// ---- bookkeeping
	if (bInGame)
	{
		// «Инвентарь» opens where the last received item went (catch, pickup, purchase, transfer...).
		if (FVGameLocal::TrackInventoryTab(Profile, Time))
		{
			bProfileDirty = true;
		}
		Profile->Stats.PlaySeconds += Dt;
		FVGameU6::Tick(*this, Dt, bWorldRuns);   // update 6: played minutes, exits, first arrival...
		AutosaveTimer += Dt;
		if (AutosaveTimer >= FVGameLocal::AutosaveInterval)
		{
			if (!bProfileDirty)
			{
				AutosaveTimer = 0.f;
			}
			else if (!Fishing->IsBusy() && !HasModal())
			{
				SaveNow();
				UE_LOG(LogFV, Verbose, TEXT("FV: autosave"));
			}
		}
	}

	if (Online)
	{
		Online->Tick(Dt);
	}
	if (Audio)
	{
		Audio->Tick(Dt);
	}
}

void UFVGame::TickClock(float Dt)
{
	if (!Profile)
	{
		return;
	}
	// Stardew-style: time stands still while the fish is on the line.
	if (Fishing && Fishing->GetState() == EFVFishState::Minigame)
	{
		return;
	}

	// Day rollovers, nets, smokehouse, food and order deadlines all advance through the same path as sleeping.
	AdvanceClock(Dt / FV::MinuteSeconds, false);
}

// ============================================================================================
// Draw
// ============================================================================================
void UFVGame::Draw(UCanvas* Canvas)
{
	if (!Canvas)
	{
		return;
	}
	const float Dt = LastDrawTime < 0.0 ? 0.f : FMath::Clamp(float(Time - LastDrawTime), 0.f, 0.1f);
	LastDrawTime = Time;

	FVDraw& D = DrawCtx;
	D.Begin(Canvas, FrameInput, Time);

	if (AFVPlayerController* FPC = Cast<AFVPlayerController>(PC.Get()))
	{
		FPC->ViewScale = D.Scale;
		FPC->ViewOffset = D.Offset;
	}

	const bool bModal = HasModal();
	FVScreen* Screen = ActiveScreen.Get();
	const bool bWorldBelow = !Screen || Screen->DrawsWorldBelow();

	// ---- world (+ HUD)
	HudColumnRect = FFVRect();   // update 6: re-registered by DrawWorldHud when the column is drawn
	if (bInGame && bWorldBelow && Scene && Fishing)
	{
		D.SetInputBlocked(bModal || Screen != nullptr);
		Scene->BeginWorldView(D);     // update 6: camera of big worlds (no-op on classic locations)
		Scene->DrawScene(D);
		Fishing->DrawWorld(D);
		Scene->DrawLighting(D);
		Scene->DrawOverlay(D);
		Scene->EndWorldView(D);
		Fishing->DrawUI(D);
		DrawWorldHud(D);
		FVGameU6::DrawOverlay(*this, D);   // update 6: banners above the HUD
		// update 4b: the onboarding's speech bubbles / arrows sit above the HUD, below any overlay screen
		FVOnboarding::Draw(this, D);
	}
	else if (bWorldBelow)
	{
		// No running game under an overlay: plain backdrop.
		D.Rect(FFVRect(0.f, 0.f, FV::VW, FV::VH), FV::Col(28, 36, 52));
	}

	// ---- active screen
	if (Screen)
	{
		D.SetInputBlocked(bModal);
		if (Screen->DrawsWorldBelow())
		{
			D.Dim(0.45f);
		}
		Screen->Draw(D);   // may open/close screens; the old one stays alive in RetiredScreens
	}

	// ---- modal, toasts
	D.SetInputBlocked(false);
	if (HasModal())
	{
		ModalAge += Dt;
		DrawModal(D);
	}
	else
	{
		ModalAge = 0.f;
	}
	DrawToasts(D, Dt);
	// phones: what is being typed, above the OS keyboard (only when the keyboard covers the focused field)
	FVTextInput::DrawOverlay(D);

	// The mouse pointer is a hardware cursor (see AFVPlayerController::SetupCursor), so it is never hidden
	// behind a slow frame and is always visible.

	if (Touch)
	{
		const bool bWorldControls = bInGame && CurrentScreen == EFVScreen::None && !HasModal();
		TWeakObjectPtr<UFVGame> WeakThis(this);
		D.AfterLetterbox = [WeakThis, bWorldControls](FVDraw& DD)
		{
			if (UFVGame* G = WeakThis.Get())
			{
				if (G->Touch)
				{
					G->Touch->Draw(DD, bWorldControls);
				}
			}
		};
	}

	bUIHoveredLastFrame = D.WasUIHovered();
	D.End();
}

void UFVGame::DrawWorldHud(FVDraw& D)
{
	HudColumnRect = FFVRect();
	if (!Profile || !Sprites || !Scene || !Fishing)
	{
		return;
	}
	// Shared by both layouts
	const int32 Level = Profile->GetLevel();
	const bool bMax = Level >= FV::ExpertMaxLevel;
	// update 6 (Р1, §9): before the first arrival in the City the XP stops at the threshold of level 10 — the bar is
	// full and shows a boat («Опыт на максимуме — плывите в Город»)
	const bool bXpFrozen = !bMax && Profile->IsXpFrozen();
	const float XpFill = (bMax || bXpFrozen) ? 1.f : FMath::Clamp(Profile->GetLevelProgress(), 0.f, 1.f);
	const FLinearColor XpColor = bMax ? FVPal::Gold() : (bXpFrozen ? FV::Col(96, 184, 236) : FVPal::Green());
	FString XpText;
	if (bMax)
	{
		XpText = FString::Printf(TEXT("Опыт: %s (максимальный уровень)"), *FV::Money(Profile->Xp));
	}
	else if (bXpFrozen)
	{
		XpText = FString::Printf(TEXT("Опыт на максимуме — плывите в Город.\nОпыт: %s — он не копится до первого прибытия в Город."), *FV::Money(Profile->Xp));
	}
	else
	{
		XpText = FString::Printf(TEXT("Опыт: %s / %s"), *FV::Money(Profile->Xp), *FV::Money(FVDB::XpForLevel(Level + 1)));
	}
	const EFVDayPhase Phase = GetPhase();
	const EFVWeather Weather = GetWeather();
	const FFVLocalWeather LocalW = GetLocalWeather();          // update 6 (§8.1): City / Cave conditions
	const FString LocalWText = FVGameLocal::LocalWeatherText(LocalW);
	const FFVLocationDef* Loc = FVDB::FindLocation(Scene->GetLocationId());
	const FString LocName = Loc ? Loc->Name : FString(TEXT("Клёвая Долина"));
	EFVIcon ClockIcon = EFVIcon::Sun;
	FLinearColor ClockTint = FLinearColor::White;
	if (Weather == EFVWeather::Rain) { ClockIcon = EFVIcon::Rain; }
	else if (Phase == EFVDayPhase::Night) { ClockIcon = EFVIcon::Moon; }
	else if (Phase == EFVDayPhase::Evening) { ClockTint = FV::Col(255, 190, 140); }
	auto PressHudButton = [this](int32 Index)
	{
		if (Index < 0 || Index >= int32(UE_ARRAY_COUNT(FVGameLocal::HudButtons)))
		{
			return;
		}
		const FVGameLocal::FHudButton& B = FVGameLocal::HudButtons[Index];
		const FName Context = FVGameLocal::HudContext(B, Profile);
		if (FVGameLocal::OpensWhileBusy(B.Screen))
		{
			OpenScreenWith(B.Screen, Context);   // the menu, the guide, the profile, the leaders only pause the world
		}
		else
		{
			TryOpenFromWorldWith(B.Screen, Context);
		}
	};
	// Rects of this frame only (the autotest taps them): buttons differ between the layouts.
	HudButtonRects.Reset();
	auto RegisterHudRect = [this](int32 Index, const FFVRect& R)
	{
		HudButtonRects.Add(FVGameLocal::HudButtons[Index].Screen, R);
		if (Index == FVGameLocal::HudInventoryIndex)
		{
			HudBackpackRect = R;   // (update 2 field) the single «Инвентарь» button now
		}
	};
	const int32 GuideIndex = FVGameLocal::HudGuideIndex;
	const int32 GuideNew = FVGameLocal::GuideUnseen(this, Time, CurrentScreen);
	// ---- update 4: the profile button (the fisherman's face + unread mail) and the level badge (-> Levels)
	const int32 ProfileIndex = FVGameLocal::HudProfileIndex;
	const int32 MailUnread = Online ? FMath::Max(0, Online->UnreadMailCount()) : 0;
	auto ProfileTooltip = [&](FVDraw& DD)
	{
		FString Who = Profile->PlayerName.IsEmpty() ? FString(TEXT("Имя ещё не выбрано")) : Profile->PlayerName;
		const FString Tag = Online ? Online->GetTagText() : FString();
		if (!Tag.IsEmpty())
		{
			Who += TEXT(" ") + Tag;
		}
		FString Body = Who + TEXT("\nТрофеи, рейтинг рыбаков и почта");
		if (MailUnread > 0)
		{
			Body += FString::Printf(TEXT("\nНовых писем: %d"), MailUnread);
		}
		DD.Tooltip(FVGameLocal::HudButtons[ProfileIndex].Tip, Body);
	};
	/** Level badge + XP bar as a button: the pressed / hover look under the badge (call before drawing the badge). */
	auto LevelButtonBack = [](FVDraw& DD, const FFVRect& R, bool bTouchUI)
	{
		const bool bHover = DD.Hover(R);
		if (bHover && DD.Input().bLmbDown && !DD.IsDragGesture())
		{
			DD.Rect(R, FV::Col(255, 236, 190, 46));
		}
		else if (bHover && !bTouchUI)
		{
			DD.Rect(R, FV::Col(255, 236, 190, 22));
			DD.Frame(R, FV::Col(255, 214, 140, 90), 1.f);
		}
	};
	const FString LevelTipTail = TEXT("\nНажмите — что открывает каждый уровень");
	// update 6: the XP bar (longer now) and, while the XP is frozen, a small boat at its right end (in BoatR)
	auto DrawXpBar = [&](FVDraw& DD, const FFVRect& Bar, const FFVRect& BoatR)
	{
		DD.Bar(Bar, XpFill, XpColor);
		if (bXpFrozen && BoatR.W > 0.f)
		{
			const FFVSprite& BoatS = Sprites->Icon(EFVIcon::Boat);
			if (BoatS.IsValid())
			{
				const float Bob = FMath::RoundToFloat(FMath::Sin(float(Time) * 2.5f));
				DD.SpriteCentered(BoatS, BoatR.Center() + FVector2D(0.f, Bob), 1.f);
			}
		}
	};
	// Inventory button: the icon shows where it opens (keepnet / backpack); a short hop when something arrived.
	const bool bInvBackpack = FVGameLocal::InventoryOpensBackpack(Profile);
	const EFVIcon InvIcon = bInvBackpack ? EFVIcon::Backpack : EFVIcon::Keepnet;
	const float InvAge = float(Time - FVGameLocal::GInvSnap.GrowTime);
	const float InvHop = (InvAge >= 0.f && InvAge < 0.45f) ? FMath::Sin(InvAge / 0.45f * UE_PI) * 4.f : 0.f;
	const int32 NetUsed = Profile->KeepnetUsed();
	const int32 NetCap = Profile->KeepnetCapacity();
	const FFVBaitDef* Bait = Profile->GetBait();
	const int32 BaitN = Bait ? Profile->BaitCount(Bait->Id) : 0;
	auto InventoryTooltip = [&](FVDraw& DD)
	{
		const int32 PackUsed = Profile->ContainerUsed(EFVContainer::Backpack);
		const int32 PackCap = Profile->ContainerCapacity(EFVContainer::Backpack);
		FString Body = FString::Printf(TEXT("Садок: %d / %d\nРюкзак: %d / %d ячеек"), NetUsed, NetCap, PackUsed, PackCap);
		Body += Bait ? FString::Printf(TEXT("\nНаживка: %s — %d шт."), *Bait->Name, BaitN) : FString(TEXT("\nБез наживки"));
		Body += bInvBackpack ? TEXT("\nОткроется рюкзак: туда попала последняя находка.") : TEXT("\nОткроется садок: туда попал последний улов.");
		DD.Tooltip(FVGameLocal::HudButtons[FVGameLocal::HudInventoryIndex].Tip, Body);
	};
	const FString ClockTipBody = FString::Printf(TEXT("%s · %s%s\nНекоторые рыбы клюют только в определённое время или в дождь."),
		*FVDB::PhaseName(Phase), *FVDB::WeatherName(Weather), *LocalWText);

	// ---- update 6 (§12.1, Р19/Р22): the button column(s) at the top-right, under the full-width info bar. Hidden under
	// overlay screens, in the expert fight (IsHudColumnHidden, §10.12) and in the classic reeling minigame (its panel may
	// sit at the right edge); the pause stays on Esc / Android «Назад». Every key registers its HudButtonRects entry.
	const bool bShowColumn = !ActiveScreen && !IsHudColumnHidden() && Fishing->GetState() != EFVFishState::Minigame;
	auto ColumnButton = [&](FVDraw& DD, int32 Index, const FFVRect& R, bool bTouchUI) -> bool
	{
		RegisterHudRect(Index, R);
		const FVGameLocal::FHudButton& B = FVGameLocal::HudButtons[Index];
		const bool bBigKey = R.W >= 36.f;
		if (Index == ProfileIndex)
		{
			const bool bClicked = FVGameLocal::ProfileButton(DD, *Sprites, Profile, R, MailUnread, Time, FString());
			if (!bTouchUI && DD.Hover(R))
			{
				ProfileTooltip(DD);
			}
			return bClicked;
		}
		const bool bInv = Index == FVGameLocal::HudInventoryIndex;
		const FFVRect DrawR = bInv ? R.Offset(0.f, -FMath::RoundToFloat(InvHop * 0.5f)) : R;
		const bool bClicked = DD.IconButton(DrawR, Sprites->Icon(bInv ? InvIcon : B.Icon), false, true, FString());
		const bool bTip = !bTouchUI && DD.Hover(R);
		if (bInv)
		{
			if (bTip)
			{
				InventoryTooltip(DD);
			}
			// the keepnet is full / the bait is almost gone: a small warning dot
			if (NetUsed >= NetCap || (Bait && BaitN <= 3))
			{
				const FVector2D C(R.Right() - 3.f, R.Bottom() - 3.f);
				DD.Circle(C, 3.5f, FV::Col(60, 16, 10));
				DD.Circle(C, 2.5f, NetUsed >= NetCap ? FV::Col(230, 60, 44) : FV::Col(236, 150, 40));
			}
		}
		else if (Index == GuideIndex)
		{
			if (GuideNew > 0)
			{
				const float Pop = FMath::Max(0.f, FMath::Sin(float(Time) * 3.f)) * (bBigKey ? 0.8f : 0.6f);
				FVGameLocal::CountDot(DD, FVector2D(R.Right() - 3.f, R.Y + 5.f), GuideNew, Pop);
			}
			if (bTip)
			{
				DD.Tooltip(B.Tip, GuideNew > 0
					? FString::Printf(TEXT("Как устроены рыбалка, предметы и дом.\nНовых страниц: %d"), GuideNew)
					: FString(TEXT("Как устроены рыбалка, предметы и дом.")));
			}
		}
		else if (Index == FVGameLocal::HudLeadersIndex)
		{
			if (bTip)
			{
				DD.Tooltip(B.Tip, TEXT("Лучшие рыбаки: уровень, улов, легенды и турнир."));
			}
		}
		else if (Index == FVGameLocal::HudPauseIndex)
		{
			if (bTip)
			{
				DD.Tooltip(B.Tip, TEXT("Настройки, сохранение и выход."));
			}
		}
		else if (bTip)
		{
			DD.Tooltip(B.Tip);
		}
		return bClicked;
	};
	/** One column of keys (Indices top to bottom) with its translucent backing; returns the pressed index. */
	auto DrawColumn = [&](FVDraw& DD, const int32* Indices, int32 N, float X, float Y0, float BtnS, float Gap, bool bTouchUI) -> int32
	{
		if (N <= 0)
		{
			return INDEX_NONE;
		}
		const float ColH = float(N) * BtnS + float(N - 1) * Gap;
		const FFVRect Back(X - 3.f, Y0 - 3.f, BtnS + 6.f, ColH + 6.f);
		// rounded translucent backing (1 px cut corners)
		DD.Rect(FFVRect(Back.X + 1.f, Back.Y, Back.W - 2.f, Back.H), FV::Col(26, 18, 14, 92));
		DD.Rect(FFVRect(Back.X, Back.Y + 1.f, 1.f, Back.H - 2.f), FV::Col(26, 18, 14, 92));
		DD.Rect(FFVRect(Back.Right() - 1.f, Back.Y + 1.f, 1.f, Back.H - 2.f), FV::Col(26, 18, 14, 92));
		DD.MarkUI(Back);
		if (HudColumnRect.W <= 0.f)
		{
			HudColumnRect = Back;
		}
		else
		{
			const float L = FMath::Min(HudColumnRect.X, Back.X), T = FMath::Min(HudColumnRect.Y, Back.Y);
			const float R = FMath::Max(HudColumnRect.Right(), Back.Right()), B = FMath::Max(HudColumnRect.Bottom(), Back.Bottom());
			HudColumnRect = FFVRect(L, T, R - L, B - T);
		}
		int32 Pressed = INDEX_NONE;
		for (int32 k = 0; k < N; ++k)
		{
			const FFVRect R(X, FMath::RoundToFloat(Y0 + float(k) * (BtnS + Gap)), BtnS, BtnS);
			if (ColumnButton(DD, Indices[k], R, bTouchUI))
			{
				Pressed = Indices[k];
			}
		}
		return Pressed;
	};

	// Active food buff (shared by both layouts)
	const double NowAbs = GetAbsMinutes();
	const FFVItemDef* ActiveFood = Profile->GetActiveFood(NowAbs);
	const float FoodLeft = ActiveFood ? Profile->FoodMinutesLeft(NowAbs) : 0.f;
	const EFVQuality FoodQ = EFVQuality(FMath::Min<uint8>(Profile->ActiveFoodQuality, uint8(EFVQuality::Iridium)));
	const float FoodAlpha = (ActiveFood && FoodLeft < 20.f) ? 0.55f + 0.45f * FMath::Abs(FMath::Sin(float(Time) * 4.f)) : 1.f;

	// ================= portrait (phones held upright) =================
	// Top: a full-width info strip (coins · level + XP · clock/phase/weather + location + food). Under it at the right:
	// the column Меню / Профиль / Лидеры / Учебник (update 6). Bottom: the thumb dock — five big keys for the main screens.
	if (FV::IsPortrait())
	{
		const bool bTouchUI = Touch.IsValid() || FV::IsTouchUI();
		const float W = FV::VW;
		const FFVRect Screen = D.ScreenRect();
		const float BarH = FV::TopBarH;

		// ---------------------------------------------------------------- top info strip (full width, no buttons)
		// (bleeds above the canvas when the phone is taller than the canvas)
		const FFVRect Top(Screen.X, Screen.Y, Screen.W, BarH - Screen.Y);
		D.Rect(Top, FV::Col(26, 18, 14, 206));
		D.Rect(FFVRect(Screen.X, BarH - 1.f, Screen.W, 1.f), FV::Col(0, 0, 0, 110));
		D.Rect(FFVRect(Screen.X, BarH, Screen.W, 1.f), FV::Col(255, 255, 255, 22));
		D.MarkUI(FFVRect(0.f, 0.f, W, BarH));

		// coins: 2x coin + amount
		const float MidY = BarH * 0.5f;
		const float MoneySize = 10.5f;
		const FString MoneyTxt = FV::Money(Profile->Money);
		const FVector2D MoneySz = D.MeasureText(MoneyTxt, MoneySize, true);
		const FFVSprite& CoinS = Sprites->Icon(EFVIcon::Coin);
		float X = 6.f;
		if (CoinS.IsValid())
		{
			D.Sprite(CoinS, FVector2D(X, FMath::RoundToFloat(MidY - CoinS.H)), 2.f);
			X += CoinS.W * 2.f + 3.f;
		}
		D.Text(MoneyTxt, FVector2D(X, FMath::RoundToFloat(MidY - MoneySz.Y * 0.5f)), MoneySize, FVPal::Gold(), EFVAlign::Left, true);
		X += MoneySz.X;
		if (!bTouchUI && D.Hover(FFVRect(2.f, 0.f, X, BarH)))
		{
			D.Tooltip(TEXT("Монеты"), FString::Printf(TEXT("У вас %s.\nПродавайте улов в лавке Митрича у пруда или на причале."), *FVGameLocal::CoinsText(Profile->Money)));
		}

		// level badge + XP bar (update 4: a button — what every level unlocks; update 6: a longer bar, the boat while
		// the XP is frozen)
		const float BlockR = W - 6.f;
		const float LX = FMath::RoundToFloat(X + 10.f);
		const float XpX = LX + 19.f;
		const float BoatW = bXpFrozen ? 17.f : 0.f;
		const float ClockMinX = FMath::Max(170.f, BlockR - 150.f);   // the clock block keeps ~150 px for time + location
		const float XpW = FMath::RoundToFloat(FMath::Clamp(FMath::Max(ClockMinX, XpX + 40.f) - 10.f - BoatW - XpX, 30.f, 120.f));
		const FFVRect LevelR(LX - 3.f, 1.f, XpX + XpW + BoatW - LX + 6.f, BarH - 2.f);
		HudButtonRects.Add(EFVScreen::Levels, LevelR);
		LevelButtonBack(D, LevelR, bTouchUI);
		D.Sprite(Sprites->Icon(EFVIcon::Level), FVector2D(LX, FMath::RoundToFloat(MidY - 8.f)));
		D.TextOutlined(FString::FromInt(Level), FVector2D(LX + 8.f, FMath::RoundToFloat(MidY - 5.f)), 8.f, FVPal::White(), EFVAlign::Center);
		DrawXpBar(D, FFVRect(XpX, FMath::RoundToFloat(MidY - 3.f), XpW, 7.f), FFVRect(XpX + XpW + 1.f, MidY - 8.f, BoatW, 16.f));
		if (!bTouchUI && D.Hover(LevelR))
		{
			D.Tooltip(FString::Printf(TEXT("Уровень рыбалки %d"), Level), XpText + LevelTipTail);
		}
		const bool bLevelClicked = D.Clicked(LevelR);

		// clock + phase/weather icon (row 1: + the local sea state at its right end) and the location name (row 2)
		const float ClockX = FMath::RoundToFloat(XpX + XpW + BoatW + 10.f);
		D.Sprite(Sprites->Icon(ClockIcon), FVector2D(ClockX, FMath::RoundToFloat(MidY - 8.f)), 1.f, ClockTint);
		const float TX = ClockX + 19.f;
		const float TW = FMath::Max(20.f, BlockR - TX);
		const FString ClockTxt = GetClockText();
		const float ClockSize = 10.f;
		const FVector2D ClockSz = D.MeasureText(ClockTxt, ClockSize, true);
		D.Text(ClockTxt, FVector2D(TX, 1.f), ClockSize, FVPal::White(), EFVAlign::Left, true);
		float Row1R = BlockR;
		if (LocalW.bValid)
		{
			// update 6 (§8.1): the local sea state (+ the evening tide) of the City / the Cave
			const float IY = FMath::RoundToFloat(1.f + ClockSz.Y * 0.5f);
			const FFVSprite& SeaS = Sprites->Icon(FVGameLocal::SeaIcon(LocalW.Sea));
			if (SeaS.IsValid())
			{
				D.SpriteCentered(SeaS, FVector2D(Row1R - 7.f, IY), 1.f);
				Row1R -= 17.f;
			}
			if (LocalW.bTide)
			{
				const FFVSprite& TideS = Sprites->Icon(EFVIcon::Tide);
				if (TideS.IsValid())
				{
					D.SpriteCentered(TideS, FVector2D(Row1R - 7.f, IY), 1.f);
					Row1R -= 17.f;
				}
			}
		}
		{
			// "День 3 · Утро", or just the phase when space is short
			const float SmallSize = 7.5f;
			const float SX = TX + ClockSz.X + 5.f;
			FString DayTxt = FString::Printf(TEXT("День %d · %s"), GetDay(), *FVDB::PhaseName(Phase));
			if (D.MeasureText(DayTxt, SmallSize, false).X > Row1R - SX)
			{
				DayTxt = FVDB::PhaseName(Phase);
			}
			const FVector2D DaySz = D.MeasureText(DayTxt, SmallSize, false);
			if (DaySz.X <= Row1R - SX)
			{
				D.Text(DayTxt, FVector2D(SX, FMath::RoundToFloat(1.f + ClockSz.Y - DaySz.Y - 1.f)), SmallSize, FV::Col(220, 205, 180), EFVAlign::Left, true, false);
			}
		}
		bool bFoodHovered = false;
		{
			// row 2: the location name, and the active food buff (icon + time left) at its right end
			const float NameSize = FV::Ui::Small();
			float NameW = TW;
			if (ActiveFood)
			{
				const FString FoodTxt = FVGameLocal::ShortDuration(FoodLeft);
				const float FoodTS = 7.f;
				const FVector2D FoodSz = D.MeasureText(FoodTxt, FoodTS, true);
				// the name comes first: when it would be cut, the food shows only its (pulsing) icon, time in the tooltip
				const float FullNameW = D.MeasureText(LocName, NameSize, true).X;
				const bool bFoodText = FullNameW <= BlockR - (12.f + 2.f + FoodSz.X) - 6.f - TX;
				const float FoodW = bFoodText ? 12.f + 2.f + FoodSz.X : 12.f;
				const float FX = FMath::RoundToFloat(BlockR - FoodW);
				const float RowY = FMath::RoundToFloat(BarH - 3.f - 12.f);
				const FFVSprite& FoodIcon = Sprites->ItemIcon(ActiveFood->Id);
				if (FoodIcon.IsValid())
				{
					D.SpriteCentered(FoodIcon, FVector2D(FX + 6.f, RowY + 6.f), 12.f / FMath::Max(1.f, FMath::Max(FoodIcon.W, FoodIcon.H)), FLinearColor(1.f, 1.f, 1.f, FoodAlpha));
				}
				if (bFoodText)
				{
					D.Text(FoodTxt, FVector2D(FX + 14.f, FMath::RoundToFloat(RowY + 6.f - FoodSz.Y * 0.5f)), FoodTS, FV::Alpha(FV::Col(190, 240, 150), FoodAlpha), EFVAlign::Left, true);
				}
				if (!bTouchUI && D.Hover(FFVRect(FX - 2.f, RowY - 1.f, FoodW + 4.f, 14.f)))
				{
					bFoodHovered = true;
					D.Tooltip(FString::Printf(TEXT("%s (%s)"), *ActiveFood->Name, *FVDB::QualityName(FoodQ)), FVGameLocal::FoodTooltipBody(*ActiveFood, FoodQ, FoodLeft));
				}
				NameW = FMath::Max(20.f, FX - 6.f - TX);
			}
			const FString NameShown = D.Ellipsize(LocName, NameW, NameSize, true);
			D.Text(NameShown, FVector2D(TX, FMath::RoundToFloat(BarH - 3.f - D.MeasureText(NameShown, NameSize, true).Y)), NameSize, FV::Col(255, 236, 196), EFVAlign::Left, true);
		}
		if (!bTouchUI && !bFoodHovered && D.Hover(FFVRect(ClockX - 2.f, 0.f, BlockR - ClockX + 2.f, BarH)))
		{
			D.Tooltip(FString::Printf(TEXT("%s · день %d, %s"), *LocName, GetDay(), *ClockTxt), ClockTipBody);
		}

		// ---------------------------------------------------------------- the button column (top-right, update 6)
		int32 ColumnPressed = INDEX_NONE;
		if (bShowColumn)
		{
			const float BtnS = FVGameLocal::HudColBtnPortrait, Gap = FVGameLocal::HudColGapPortrait;
			ColumnPressed = DrawColumn(D, FVGameLocal::HudColumnOrder, int32(UE_ARRAY_COUNT(FVGameLocal::HudColumnOrder)),
				FMath::RoundToFloat(W - 4.f - BtnS), BarH + 6.f, BtnS, Gap, bTouchUI);
		}

		// ---------------------------------------------------------------- bottom dock (hidden under overlay screens)
		if (ActiveScreen)
		{
			if (bLevelClicked) { OpenScreenWith(EFVScreen::Levels, NAME_None); }
			return;
		}
		// Update 3: no hint line any more — the whole dock is the wooden toolbar with five big keys.
		const float DockY = FV::VH - FV::BottomBarH;
		const float DockBottom = Screen.Bottom();
		// soft shadow over the world just above the board
		D.GradientV(FFVRect(Screen.X, DockY - 6.f, Screen.W, 6.f), FV::Col(20, 10, 4, 0), FV::Col(20, 10, 4, 70), 3);
		// toolbar: wooden board (bleeds to the bottom edge of the screen)
		D.Rect(FFVRect(Screen.X, DockY, Screen.W, DockBottom - DockY), FVPal::WoodDark());
		D.Rect(FFVRect(Screen.X, DockY + 1.f, Screen.W, DockBottom - DockY - 1.f), FVPal::Wood());
		D.Rect(FFVRect(Screen.X, DockY + 1.f, Screen.W, 1.f), FVPal::WoodLight());
		for (int32 g = 0; g < 7; ++g)
		{
			const float GY = DockY + 9.f + g * 12.f;
			if (GY > DockBottom - 2.f) { break; }
			const float GX = FMath::Fmod(37.f + g * 71.f, 120.f);
			for (float SX = GX; SX < W; SX += 120.f + g * 7.f)
			{
				D.Rect(FFVRect(SX, GY, 22.f + g * 5.f, 1.f), FV::Col(184, 100, 44));
			}
		}
		D.MarkUI(FFVRect(0.f, DockY, W, FV::VH - DockY));

		// five big keys (HudButtons[0..4]); same 3 px gap as before, the keys fill the dock
		const int32 NumDock = FVGameLocal::HudDockCount;
		const float Margin = 4.f, Gap = 3.f;
		const float BtnW = FMath::FloorToFloat((W - 2.f * Margin - Gap * (NumDock - 1)) / NumDock);
		const float RowW = BtnW * NumDock + Gap * (NumDock - 1);
		const float BtnY = DockY + 6.f;
		const float BtnH = FMath::Max(44.f, FV::VH - 4.f - BtnY);
		const bool bBusy = IsWorldBusy();
		int32 Pressed = INDEX_NONE;
		float BX = FMath::RoundToFloat((W - RowW) * 0.5f);
		for (int32 i = 0; i < NumDock; ++i)
		{
			const FVGameLocal::FHudButton& B = FVGameLocal::HudButtons[i];
			const FFVRect R(BX, BtnY, BtnW, BtnH);
			RegisterHudRect(i, R);
			const EFVIcon Icon = (i == FVGameLocal::HudInventoryIndex) ? InvIcon : B.Icon;
			if (FVGameLocal::DockButton(D, *Sprites, R, Icon, B.Name, bBusy))
			{
				Pressed = i;
			}
			if (!bTouchUI && D.Hover(R))
			{
				if (i == FVGameLocal::HudInventoryIndex)
				{
					InventoryTooltip(D);
				}
				else
				{
					D.Tooltip(B.Tip);
				}
			}
			BX += BtnW + Gap;
		}

		// badges on «Инвентарь»: keepnet fill (top-right, hops when something arrives) and a low-bait warning (top-left)
		{
			const FFVRect InvR = HudButtonRects.FindRef(EFVScreen::Inventory);
			const FLinearColor NetFill = NetUsed >= NetCap ? FV::Col(200, 60, 44) : (NetUsed >= NetCap * 3 / 4 ? FV::Col(206, 140, 30) : FV::Col(70, 110, 150));
			FVGameLocal::Badge(D, FVector2D(InvR.Right() + 1.f, FMath::RoundToFloat(InvR.Y - 5.f - InvHop)), FString::Printf(TEXT("%d/%d"), NetUsed, NetCap), NetFill,
				&Sprites->Icon(EFVIcon::Keepnet));
			if (Bait && BaitN <= 5)
			{
				const FString BaitTxt = FString::FromInt(BaitN);
				const FFVSprite* BaitS = &Sprites->Bait(Bait->Style);
				const float BW = FVGameLocal::BadgeWidth(D, BaitTxt, BaitS);
				FVGameLocal::Badge(D, FVector2D(InvR.X - 1.f + BW, InvR.Y - 5.f), BaitTxt, BaitN <= 3 ? FV::Col(200, 70, 50) : FV::Col(206, 140, 30), BaitS);
			}
		}

		if (ColumnPressed != INDEX_NONE)
		{
			PressHudButton(ColumnPressed);
		}
		else if (bLevelClicked)
		{
			OpenScreenWith(EFVScreen::Levels, NAME_None);   // info only: opens even while fishing (pauses the world)
		}
		else if (Pressed != INDEX_NONE)
		{
			PressHudButton(Pressed);
		}
		return;
	}

	// ================= landscape: top bar (full width, no buttons since update 6) =================
	const bool bTouchLand = Touch.IsValid() || FV::IsTouchUI();
	const float VW = FV::VW;
	const FFVRect Top(0.f, 0.f, VW, FV::TopBarH);
	D.Panel(Top, EFVPanel::Dark);
	D.MarkUI(Top);

	// Coins
	D.MoneyLabel(FVector2D(8.f, 8.f), Profile->Money, 8.f, EFVAlign::Left, FVPal::Gold());
	if (D.Hover(FFVRect(4.f, 2.f, 78.f, 20.f)))
	{
		D.Tooltip(TEXT("Монеты"), FString::Printf(TEXT("У вас %s.\nПродавайте улов в лавке Митрича у пруда или на причале."), *FVGameLocal::CoinsText(Profile->Money)));
	}

	// Level badge + XP bar (update 4: a button — what every level unlocks; update 6: a longer bar, the boat while frozen)
	const float XpBarW = 110.f;
	const float LandBoatW = bXpFrozen ? 17.f : 0.f;
	const FFVRect LevelR(85.f, 2.f, 23.f + XpBarW + LandBoatW + 4.f, 20.f);
	HudButtonRects.Add(EFVScreen::Levels, LevelR);
	LevelButtonBack(D, LevelR, bTouchLand);
	const FFVRect Badge(88.f, 4.f, 16.f, 16.f);
	D.Sprite(Sprites->Icon(EFVIcon::Level), Badge.Pos());
	D.TextOutlined(FString::FromInt(Level), FVector2D(Badge.X + 8.f, Badge.Y + 3.f), 8.f, FVPal::White(), EFVAlign::Center);
	DrawXpBar(D, FFVRect(108.f, 9.f, XpBarW, 6.f), FFVRect(108.f + XpBarW + 1.f, 4.f, LandBoatW, 16.f));
	const bool bLevelClicked = D.Clicked(LevelR);
	if (D.Hover(LevelR))
	{
		D.Tooltip(FString::Printf(TEXT("Уровень рыбалки %d"), Level), XpText + LevelTipTail);
	}

	// Right block, from the right edge: local sea state (update 6), clock + phase / weather, food buff
	float RX = VW - 6.f;
	if (LocalW.bValid)
	{
		const FFVSprite& SeaS = Sprites->Icon(FVGameLocal::SeaIcon(LocalW.Sea));
		if (SeaS.IsValid())
		{
			D.SpriteCentered(SeaS, FVector2D(RX - 8.f, 12.f), 1.f);
			RX -= 18.f;
		}
		if (LocalW.bTide)
		{
			const FFVSprite& TideS = Sprites->Icon(EFVIcon::Tide);
			if (TideS.IsValid())
			{
				D.SpriteCentered(TideS, FVector2D(RX - 8.f, 12.f), 1.f);
				RX -= 18.f;
			}
		}
		if (D.Hover(FFVRect(RX, 2.f, VW - 6.f - RX, 20.f)))
		{
			D.Tooltip(FString::Printf(TEXT("Море: %s"), *FVGameLocal::SeaName(LocalW.Sea)), LocalWText.TrimStartAndEnd());
		}
	}
	const float ClockX = FMath::RoundToFloat(RX - 80.f);
	D.Sprite(Sprites->Icon(ClockIcon), FVector2D(ClockX, 4.f), 1.f, ClockTint);
	D.Text(GetClockText(), FVector2D(ClockX + 19.f, 2.f), 8.f, FVPal::White(), EFVAlign::Left, true);
	D.Text(FString::Printf(TEXT("День %d · %s"), GetDay(), *FVDB::PhaseName(Phase)), FVector2D(ClockX + 19.f, 13.f), 6.f, FV::Col(220, 205, 180), EFVAlign::Left, true, false);
	if (D.Hover(FFVRect(ClockX - 2.f, 2.f, 80.f, 20.f)))
	{
		D.Tooltip(FString::Printf(TEXT("День %d, %s"), GetDay(), *GetClockText()), ClockTipBody);
	}

	// Active food buff: dish icon + time left, just left of the clock
	float NameRight = ClockX - 8.f;
	if (ActiveFood)
	{
		const FString FoodTxt = FVGameLocal::ShortDuration(FoodLeft);
		const float FoodTS = 6.5f;
		const FVector2D FoodSz = D.MeasureText(FoodTxt, FoodTS, true);
		const float FoodW = 16.f + 2.f + FoodSz.X;
		const float FX = FMath::RoundToFloat(ClockX - 6.f - FoodW);
		const FFVSprite& FoodIcon = Sprites->ItemIcon(ActiveFood->Id);
		if (FoodIcon.IsValid())
		{
			D.SpriteCentered(FoodIcon, FVector2D(FX + 8.f, 12.f), 16.f / FMath::Max(1.f, FMath::Max(FoodIcon.W, FoodIcon.H)), FLinearColor(1.f, 1.f, 1.f, FoodAlpha));
		}
		D.Text(FoodTxt, FVector2D(FX + 18.f, FMath::RoundToFloat(12.f - FoodSz.Y * 0.5f)), FoodTS, FV::Alpha(FV::Col(190, 240, 150), FoodAlpha), EFVAlign::Left, true);
		if (D.Hover(FFVRect(FX - 2.f, 2.f, FoodW + 4.f, 20.f)))
		{
			D.Tooltip(FString::Printf(TEXT("%s (%s)"), *ActiveFood->Name, *FVDB::QualityName(FoodQ)), FVGameLocal::FoodTooltipBody(*ActiveFood, FoodQ, FoodLeft));
		}
		NameRight = FX - 8.f;
	}

	// Location name: centred in the free space between the level bar and the right block
	{
		const float NameLeft = LevelR.Right() + 8.f;
		const float NameW = FMath::Max(20.f, NameRight - NameLeft);
		const FString NameShown = D.Ellipsize(LocName, NameW, 8.f, true);
		D.Text(NameShown, FVector2D(FMath::RoundToFloat(NameLeft + NameW * 0.5f), 7.f), 8.f, FVPal::White(), EFVAlign::Center, true);
	}

	// ---- update 6 (§12.1, Р22): two narrow columns at the right edge under the bar — the outer one like the portrait
	// column (Меню / Профиль / Лидеры / Учебник), the inner one with the main screens (the portrait dock keys).
	int32 Pressed = INDEX_NONE;
	if (bShowColumn)
	{
		const float BtnS = FVGameLocal::HudColBtnLandscape, Gap = FVGameLocal::HudColGapLandscape;
		const float Y0 = FV::TopBarH + 5.f;
		const float OuterX = FMath::RoundToFloat(VW - 4.f - BtnS);
		const float InnerX = OuterX - 6.f - BtnS;   // 6 px apart: the two backings touch without overlapping
		int32 MainKeys[FVGameLocal::HudDockCount];
		for (int32 i = 0; i < FVGameLocal::HudDockCount; ++i)
		{
			MainKeys[i] = i;
		}
		const int32 OuterPressed = DrawColumn(D, FVGameLocal::HudColumnOrder, int32(UE_ARRAY_COUNT(FVGameLocal::HudColumnOrder)), OuterX, Y0, BtnS, Gap, bTouchLand);
		const int32 InnerPressed = DrawColumn(D, MainKeys, FVGameLocal::HudDockCount, InnerX, Y0, BtnS, Gap, bTouchLand);
		Pressed = OuterPressed != INDEX_NONE ? OuterPressed : InnerPressed;
	}
	if (Pressed != INDEX_NONE)
	{
		PressHudButton(Pressed);
	}
	else if (bLevelClicked)
	{
		OpenScreenWith(EFVScreen::Levels, NAME_None);   // info only: opens even while fishing (pauses the world)
	}
	// Update 3: no bottom hint bar — the world shows through (FV::BottomBarH stays as a layout margin). The interaction
	// prompt is drawn under the highlighted object's name plate by the scene; the guide «?» explains the controls.
}

void UFVGame::DrawModal(FVDraw& D)
{
	if (Modals.Num() == 0)
	{
		return;
	}
	const FFVModal& M = Modals[0];

	D.Dim(0.5f);

	const bool bIcon = M.Icon != EFVIcon::Count && Sprites != nullptr;
	TArray<FString> Labels = M.Buttons;
	if (Labels.Num() == 0)
	{
		Labels.Add(TEXT("OK"));
	}

	// ================= portrait (phones): a wide card with finger-sized buttons at its bottom =================
	if (FV::IsPortrait())
	{
		const float W = FMath::Min(FV::VW - 20.f, 340.f);
		const float Pad = 16.f;
		const float TextW = W - 2.f * Pad - 4.f;
		const float BtnH = FV::Ui::ButtonH();
		const float BtnGap = 8.f;
		const float BtnText = FV::Ui::Body() + 1.5f;
		const int32 NumBtn = Labels.Num();
		const float Avail = W - 2.f * Pad;

		// One row when the buttons fit side by side (the last one = cancel/back on the LEFT, the first one = the main
		// action on the RIGHT, like the window action bars); otherwise stacked with the main action at the bottom.
		float Need = BtnGap * (NumBtn - 1);
		for (const FString& L : Labels)
		{
			Need += FMath::Max(96.f, D.MeasureText(L, BtnText).X + 28.f);
		}
		const bool bStack = NumBtn > 1 && Need > Avail;
		const float ButtonsH = bStack ? NumBtn * BtnH + (NumBtn - 1) * BtnGap : BtnH;

		// Text: the FV::Ui sizes, shrunk a little (then trimmed) only if a long dialog would not fit the screen.
		const float IconH = bIcon ? 40.f : 0.f;
		const float MaxH = FV::VH - 24.f;
		float TitleSize = FV::Ui::Title();
		float BodySize = FV::Ui::Body();
		TArray<FString> TitleLines, BodyLines, BulletLines;
		TArray<bool> BulletStarts;
		float TitleLH = 0.f, BodyLH = 0.f;
		const float BulletGap = 4.f;
		auto Layout = [&]()
		{
			TitleLines.Reset(); BodyLines.Reset(); BulletLines.Reset(); BulletStarts.Reset();
			FVGameLocal::WrapText(D, M.Title, TextW, TitleSize, true, TitleLines);
			FVGameLocal::WrapText(D, M.Body, TextW, BodySize, false, BodyLines);
			for (const FString& Line : M.Lines)
			{
				TArray<FString> Wrapped;
				FVGameLocal::WrapText(D, Line, TextW - 12.f, BodySize, false, Wrapped);
				for (int32 i = 0; i < Wrapped.Num(); ++i)
				{
					BulletLines.Add(Wrapped[i]);
					BulletStarts.Add(i == 0);
				}
			}
			TitleLH = D.MeasureText(TEXT("Йy"), TitleSize, true).Y + 1.f;
			BodyLH = D.MeasureText(TEXT("Йy"), BodySize, false).Y + 2.f;
		};
		auto ContentH = [&]()
		{
			int32 Items = 0;
			for (bool bStart : BulletStarts) { Items += bStart ? 1 : 0; }
			return Pad + IconH + TitleLines.Num() * TitleLH + (BodyLines.Num() > 0 ? 6.f + BodyLines.Num() * BodyLH : 0.f)
				+ (BulletLines.Num() > 0 ? 10.f + BulletLines.Num() * BodyLH + FMath::Max(0, Items - 1) * BulletGap : 0.f)
				+ 16.f + ButtonsH + Pad;
		};
		Layout();
		while (ContentH() > MaxH && BodySize > 8.f)
		{
			BodySize -= 0.5f;
			TitleSize = FMath::Max(13.f, TitleSize - 0.5f);
			Layout();
		}
		while (ContentH() > MaxH && BulletLines.Num() > 0) { BulletLines.Pop(); BulletStarts.Pop(); }
		while (ContentH() > MaxH && BodyLines.Num() > 1) { BodyLines.Pop(); }
		const float H = FMath::RoundToFloat(ContentH());

		const float Pop = FVGameLocal::EaseOutBack(ModalAge / 0.22f);
		const float X = FMath::RoundToFloat((FV::VW - W) * 0.5f);
		const float Y = FMath::RoundToFloat(FMath::Max(8.f, (FV::VH - H) * 0.5f) + (1.f - Pop) * 16.f);
		const FFVRect R(X, Y, W, H);
		D.Panel(R, EFVPanel::Wood);
		D.MarkUI(R);

		float CY = Y + Pad;
		const float CX = X + W * 0.5f;
		if (bIcon)
		{
			const float Bob = FMath::Sin(float(Time) * 3.f) * 1.5f;
			D.SpriteCentered(Sprites->Icon(M.Icon), FVector2D(CX, CY + 16.f + Bob), 2.f);
			CY += IconH;
		}
		for (const FString& L : TitleLines)
		{
			D.Text(L, FVector2D(CX, CY), TitleSize, FVPal::Ink(), EFVAlign::Center, true);
			CY += TitleLH;
		}
		if (BodyLines.Num() > 0)
		{
			CY += 6.f;
			for (const FString& L : BodyLines)
			{
				D.Text(L, FVector2D(CX, CY), BodySize, FVPal::InkSoft(), EFVAlign::Center, false, false);
				CY += BodyLH;
			}
		}
		if (BulletLines.Num() > 0)
		{
			CY += 4.f;
			D.Divider(FVector2D(X + Pad, CY), W - 2.f * Pad);
			CY += 6.f;
			const float LX = X + Pad + 2.f;
			for (int32 i = 0; i < BulletLines.Num(); ++i)
			{
				if (BulletStarts[i])
				{
					if (i > 0) { CY += BulletGap; }
					D.Text(TEXT("•"), FVector2D(LX, CY), BodySize, FVPal::Wood());
				}
				D.Text(BulletLines[i], FVector2D(LX + 12.f, CY), BodySize, FVPal::Ink(), EFVAlign::Left, false, false);
				CY += BodyLH;
			}
		}

		// buttons (Enter = first, ESC = last)
		int32 Chosen = INDEX_NONE;
		const float BY0 = Y + H - Pad - ButtonsH;
		for (int32 i = 0; i < NumBtn; ++i)
		{
			FFVRect BR;
			if (bStack)
			{
				// main action (index 0) at the bottom, closest to the thumb
				BR = FFVRect(X + Pad, BY0 + (NumBtn - 1 - i) * (BtnH + BtnGap), Avail, BtnH);
			}
			else if (NumBtn == 1)
			{
				const float BW = FMath::Min(Avail, FMath::Max(180.f, D.MeasureText(Labels[0], BtnText).X + 40.f));
				BR = FFVRect(FMath::RoundToFloat(CX - BW * 0.5f), BY0, BW, BtnH);
			}
			else
			{
				// right to left: index 0 is the right-most cell
				BR = FVDraw::SplitH(FFVRect(X + Pad, BY0, Avail, BtnH), NumBtn - 1 - i, NumBtn, BtnGap);
			}
			const EFVButton Style = (i == 0) ? EFVButton::Primary : EFVButton::Normal;
			if (D.Button(BR, Labels[i], Style, true, BtnText) && ModalAge >= FVGameLocal::ModalInputDelay && Chosen == INDEX_NONE)
			{
				Chosen = i;
			}
		}
		if (Chosen != INDEX_NONE)
		{
			ChooseModal(Chosen);
		}
		return;
	}

	// ================= landscape =================
	const float Pad = 14.f;
	const float BtnH = 20.f;
	const float BtnGap = 8.f;

	// Buttons decide the minimum width.
	TArray<float> BtnW;
	float BtnTotal = 0.f;
	for (const FString& L : Labels)
	{
		const float BW = FMath::Max(72.f, D.MeasureText(L, 8.f).X + 24.f);
		BtnW.Add(BW);
		BtnTotal += BW;
	}
	BtnTotal += BtnGap * (Labels.Num() - 1);
	const float W = FMath::Clamp(FMath::Max(300.f, BtnTotal + 2.f * Pad), 200.f, FV::VW - 20.f);
	const float TextW = W - 2.f * Pad - 6.f;

	const float TitleSize = 12.f, BodySize = 8.f;
	TArray<FString> TitleLines, BodyLines, BulletLines;
	TArray<bool> BulletStarts;
	FVGameLocal::WrapText(D, M.Title, TextW, TitleSize, true, TitleLines);
	FVGameLocal::WrapText(D, M.Body, TextW, BodySize, false, BodyLines);
	for (const FString& Line : M.Lines)
	{
		TArray<FString> Wrapped;
		FVGameLocal::WrapText(D, Line, TextW - 10.f, BodySize, false, Wrapped);
		for (int32 i = 0; i < Wrapped.Num(); ++i)
		{
			BulletLines.Add(Wrapped[i]);
			BulletStarts.Add(i == 0);
		}
	}

	const float TitleLH = D.MeasureText(TEXT("Йy"), TitleSize, true).Y + 1.f;
	const float BodyLH = D.MeasureText(TEXT("Йy"), BodySize, false).Y + 1.f;
	const float IconH = bIcon ? 36.f : 0.f;

	// Fit on screen: drop overflowing bullet/body lines if the content is very long.
	auto ContentH = [&]() {
		return Pad + IconH + TitleLines.Num() * TitleLH + 6.f + BodyLines.Num() * BodyLH
			+ (BulletLines.Num() > 0 ? 6.f + BulletLines.Num() * BodyLH : 0.f) + 12.f + BtnH + Pad;
	};
	while (ContentH() > FV::VH - 20.f && BulletLines.Num() > 0) { BulletLines.Pop(); BulletStarts.Pop(); }
	while (ContentH() > FV::VH - 20.f && BodyLines.Num() > 1) { BodyLines.Pop(); }
	const float H = ContentH();

	const float Pop = FVGameLocal::EaseOutBack(ModalAge / 0.22f);
	const float X = FMath::RoundToFloat((FV::VW - W) * 0.5f);
	const float Y = FMath::RoundToFloat((FV::VH - H) * 0.5f + (1.f - Pop) * 14.f);
	const FFVRect R(X, Y, W, H);
	D.Panel(R, EFVPanel::Wood);
	D.MarkUI(R);

	float CY = Y + Pad;
	const float CX = X + W * 0.5f;
	if (bIcon)
	{
		const float Bob = FMath::Sin(float(Time) * 3.f) * 1.5f;
		D.SpriteCentered(Sprites->Icon(M.Icon), FVector2D(CX, CY + 15.f + Bob), 2.f);
		CY += IconH;
	}
	for (const FString& L : TitleLines)
	{
		D.Text(L, FVector2D(CX, CY), TitleSize, FVPal::Ink(), EFVAlign::Center);
		CY += TitleLH;
	}
	CY += 6.f;
	for (const FString& L : BodyLines)
	{
		D.Text(L, FVector2D(CX, CY), BodySize, FVPal::InkSoft(), EFVAlign::Center, false, false);
		CY += BodyLH;
	}
	if (BulletLines.Num() > 0)
	{
		CY += 6.f;
		const float LX = X + Pad + 6.f;
		for (int32 i = 0; i < BulletLines.Num(); ++i)
		{
			if (BulletStarts[i])
			{
				D.Text(TEXT("•"), FVector2D(LX, CY), BodySize, FVPal::Wood());
			}
			D.Text(BulletLines[i], FVector2D(LX + 10.f, CY), BodySize, FVPal::Ink(), EFVAlign::Left, false, false);
			CY += BodyLH;
		}
	}

	// Buttons (ESC = last, Enter = first)
	float BX = FMath::RoundToFloat(CX - BtnTotal * 0.5f);
	const float BY = Y + H - Pad - BtnH;
	int32 Chosen = INDEX_NONE;
	for (int32 i = 0; i < Labels.Num(); ++i)
	{
		const FFVRect BR(BX, BY, BtnW[i], BtnH);
		const EFVButton Style = (i == 0) ? EFVButton::Primary : EFVButton::Normal;
		if (D.Button(BR, Labels[i], Style, true, 8.f) && ModalAge >= FVGameLocal::ModalInputDelay && Chosen == INDEX_NONE)
		{
			Chosen = i;
		}
		BX += BtnW[i] + BtnGap;
	}
	if (Chosen != INDEX_NONE)
	{
		ChooseModal(Chosen);
	}
}

void UFVGame::DrawToasts(FVDraw& D, float Dt)
{
	if (Toasts.Num() == 0)
	{
		return;
	}
	// Too many at once: hurry the oldest ones out.
	const int32 Excess = Toasts.Num() - FVGameLocal::ToastMaxVisible;
	for (int32 i = 0; i < Excess; ++i)
	{
		Toasts[i].Age = FMath::Max(Toasts[i].Age, FVGameLocal::ToastLife - FVGameLocal::ToastOut);
	}

	// ================= portrait: centered cards, bigger text (FV::Ui), wrapped to two lines =================
	// World / pause: stacked downwards just below the top info strip. Overlay windows: stacked upwards just above the
	// window's bottom action bar (next to the button that was pressed). Title screen: near the top.
	if (FV::IsPortrait())
	{
		const bool bWorldOrPause = bInGame && (CurrentScreen == EFVScreen::None || CurrentScreen == EFVScreen::Pause);
		const bool bOverWindow = bInGame && !bWorldOrPause && CurrentScreen != EFVScreen::MainMenu;
		const float Size = FV::Ui::Body();
		const float MaxW = FV::VW - 20.f;
		// update 6: over windows the toasts stack under the window header, never over the controls above the action bar
		float NextTop = bOverWindow ? FV::WindowRect().Y + 42.f : (bInGame ? FV::TopBarH + 6.f : 8.f);
		for (FToast& T : Toasts)
		{
			T.Age += Dt;
			const bool bIcon = T.Icon != EFVIcon::Count && Sprites != nullptr;
			const float IconW = bIcon ? 20.f : 0.f;
			const float MaxTextW = MaxW - 16.f - IconW;
			float TS = Size;
			TArray<FString> Lines;
			if (D.MeasureText(T.Text, TS, true).X <= MaxTextW)
			{
				Lines.Add(T.Text);
			}
			else
			{
				bool bSplit = false;
				for (const float Try : { Size, Size - 1.f })
				{
					if (FVGameLocal::SplitTwoLines(D, T.Text, MaxTextW, Try, true, Lines))
					{
						TS = Try;
						bSplit = true;
						break;
					}
				}
				if (!bSplit)
				{
					TS = Size - 1.f;
					Lines.Reset();
					FVGameLocal::WrapText(D, T.Text, MaxTextW, TS, true, Lines);
					if (Lines.Num() > 2)
					{
						TArray<FString> Rest(Lines.GetData() + 1, Lines.Num() - 1);
						Lines.SetNum(1);
						Lines.Add(FString::Join(Rest, TEXT(" ")));
					}
					for (FString& L : Lines) { L = D.Ellipsize(L, MaxTextW, TS, true); }
				}
			}
			const float LineBox = D.MeasureText(TEXT("Йy"), TS, true).Y;
			const float LineStep = FMath::RoundToFloat(LineBox * 0.9f);
			float MaxLineW = 0.f;
			for (const FString& L : Lines) { MaxLineW = FMath::Max(MaxLineW, D.MeasureText(L, TS, true).X); }
			const float H = FMath::RoundToFloat(FMath::Max(26.f, LineBox + LineStep * (Lines.Num() - 1) + 8.f));
			const float W = FMath::RoundToFloat(FMath::Min(MaxLineW + 16.f + IconW, MaxW));

			const float WantY = NextTop;
			T.Y = (T.Y < 0.f) ? WantY : FMath::FInterpTo(T.Y, WantY, Dt, 12.f);

			// fade + a short slide towards the anchor edge
			float Slide = 0.f;
			if (T.Age < FVGameLocal::ToastIn)
			{
				Slide = 1.f - FVGameLocal::EaseOutCubic(T.Age / FVGameLocal::ToastIn);
			}
			else if (T.Age > FVGameLocal::ToastLife - FVGameLocal::ToastOut)
			{
				Slide = FVGameLocal::EaseInCubic((T.Age - (FVGameLocal::ToastLife - FVGameLocal::ToastOut)) / FVGameLocal::ToastOut);
			}
			const float A = FMath::Clamp(1.f - Slide, 0.f, 1.f);
			const float DY = -10.f * Slide;
			const FFVRect R(FMath::RoundToFloat((FV::VW - W) * 0.5f), FMath::RoundToFloat(T.Y + DY), W, H);
			D.Rect(R.Offset(1.f, 2.f), FV::Col(0, 0, 0, uint8(70 * A)));
			D.Rect(R, FV::Col(26, 18, 14, uint8(224 * A)));
			D.Frame(R, FV::Col(0, 0, 0, uint8(100 * A)), 1.f);
			D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 255, 255, uint8(30 * A)));
			D.Rect(FFVRect(R.X + 1.f, R.Bottom() - 3.f, R.W - 2.f, 2.f), FV::Alpha(T.Color, 0.55f * A));   // accent line

			float TX = R.X + 8.f;
			if (bIcon)
			{
				D.Sprite(Sprites->Icon(T.Icon), FVector2D(R.X + 6.f, FMath::RoundToFloat(R.Y + (H - 2.f - 16.f) * 0.5f)), 1.f, FLinearColor(1.f, 1.f, 1.f, A));
				TX = R.X + 6.f + IconW;
			}
			float TY = FMath::RoundToFloat(R.Y + (H - 2.f - (LineBox + LineStep * (Lines.Num() - 1))) * 0.5f);
			for (const FString& L : Lines)
			{
				D.Text(L, FVector2D(TX, TY), TS, FV::Alpha(T.Color, T.Color.A * A), EFVAlign::Left, true);
				TY += LineStep;
			}
			NextTop += H + 4.f;
		}
		Toasts.RemoveAll([](const FToast& T) { return T.Age >= FVGameLocal::ToastLife; });
		return;
	}

	// ================= landscape: top-right under the HUD bar =================
	// (under overlay windows: above the window frame, left of its close button)
	const bool bOverWindow = !(bInGame && CurrentScreen == EFVScreen::None);
	const float TopY = bOverWindow ? 2.f : FV::TopBarH + 4.f;
	// update 6: in the world the toasts stand left of the HUD button column, not over it
	const float RightMargin = bOverWindow ? 30.f
		: (HudColumnRect.W > 0.f && HudColumnRect.Y < TopY + 60.f ? FMath::Max(4.f, FV::VW - HudColumnRect.X + 4.f) : 4.f);
	float TargetY = TopY;
	for (FToast& T : Toasts)
	{
		T.Age += Dt;
		const bool bIcon = T.Icon != EFVIcon::Count && Sprites != nullptr;
		const FVector2D TS = D.MeasureText(T.Text, 8.f);
		const float H = 18.f;
		const float W = FMath::Min(TS.X + 14.f + (bIcon ? 17.f : 0.f), FMath::Min(FV::VW - 20.f, FV::VW - RightMargin - 8.f));
		T.Y = (T.Y < 0.f) ? TargetY : FMath::FInterpTo(T.Y, TargetY, Dt, 12.f);

		float Slide = 0.f;
		if (T.Age < FVGameLocal::ToastIn)
		{
			Slide = 1.f - FVGameLocal::EaseOutCubic(T.Age / FVGameLocal::ToastIn);
		}
		else if (T.Age > FVGameLocal::ToastLife - FVGameLocal::ToastOut)
		{
			Slide = FVGameLocal::EaseInCubic((T.Age - (FVGameLocal::ToastLife - FVGameLocal::ToastOut)) / FVGameLocal::ToastOut);
		}
		const float X = FMath::RoundToFloat(FV::VW - RightMargin - W + Slide * (W + RightMargin + 6.f));
		const FFVRect R(X, FMath::RoundToFloat(T.Y), W, H);
		D.Panel(R, EFVPanel::Dark);

		float TX = R.X + 7.f;
		if (bIcon)
		{
			D.Sprite(Sprites->Icon(T.Icon), FVector2D(R.X + 4.f, R.Y + 1.f));
			TX = R.X + 22.f;
		}
		D.Text(D.Ellipsize(T.Text, R.Right() - TX - 4.f, 8.f, true), FVector2D(TX, R.Y + FMath::Max(0.f, (H - TS.Y) * 0.5f)), 8.f, T.Color, EFVAlign::Left, true);
		TargetY += H + 3.f;
	}
	Toasts.RemoveAll([](const FToast& T) { return T.Age >= FVGameLocal::ToastLife; });
}

// ============================================================================================
// Screens
// ============================================================================================
void UFVGame::RetireActiveScreen()
{
	if (ActiveScreen)
	{
		TUniquePtr<FVScreen> Old = MoveTemp(ActiveScreen);
		Old->OnClose();
		RetiredScreens.Add(MoveTemp(Old));
	}
}

void UFVGame::OpenScreen(EFVScreen Screen)
{
	OpenScreenWith(Screen, NAME_None);
}

void UFVGame::OpenScreenWith(EFVScreen Screen, FName Context)
{
	if (Screen == EFVScreen::None)
	{
		CloseScreen();
		return;
	}
	// The same screen with the same context is already open: nothing to do. Another context (another trader,
	// station, recipe...) rebuilds the screen so it reads the new context in OnOpen.
	if (Screen == CurrentScreen && ActiveScreen && Context == ScreenContext)
	{
		return;
	}

	TUniquePtr<FVScreen> NewScreen;
	switch (Screen)
	{
	case EFVScreen::MainMenu: NewScreen = FVMakeMainMenuScreen(this); break;
	case EFVScreen::Map: NewScreen = FVMakeMapScreen(this); break;
	case EFVScreen::Shop: NewScreen = FVMakeShopScreen(this); break;
	case EFVScreen::Inventory: NewScreen = FVMakeInventoryScreen(this); break;
	case EFVScreen::Wardrobe: NewScreen = FVMakeWardrobeScreen(this); break;
	case EFVScreen::Collection: NewScreen = FVMakeCollectionScreen(this); break;
	case EFVScreen::Achievements: NewScreen = FVMakeAchievementsScreen(this); break;
	case EFVScreen::Pause: NewScreen = FVMakePauseScreen(this); break;
	case EFVScreen::Chest: NewScreen = FVMakeChestScreen(this); break;
	case EFVScreen::IceCellar: NewScreen = FVMakeIceCellarScreen(this); break;
	case EFVScreen::Station: NewScreen = FVMakeStationScreen(this); break;
	case EFVScreen::Orders: NewScreen = FVMakeOrdersScreen(this); break;
	case EFVScreen::NetSpot: NewScreen = FVMakeNetSpotScreen(this); break;
	case EFVScreen::HomeDecor: NewScreen = FVMakeHomeDecorScreen(this); break;
	case EFVScreen::Cooking: NewScreen = FVMakeCookingScreen(this); break;
	case EFVScreen::LureCraft: NewScreen = FVMakeLureCraftScreen(this); break;
	case EFVScreen::Knots: NewScreen = FVMakeKnotsScreen(this); break;
	case EFVScreen::Digging: NewScreen = FVMakeDiggingScreen(this); break;
	case EFVScreen::Tutorial: NewScreen = FVMakeTutorialScreen(this); break;
	case EFVScreen::Profile: NewScreen = FVMakeProfileScreen(this); break;
	case EFVScreen::Leaderboard: NewScreen = FVMakeLeaderboardScreen(this); break;
	case EFVScreen::Mail: NewScreen = FVMakeMailScreen(this); break;
	case EFVScreen::Levels: NewScreen = FVMakeLevelsScreen(this); break;
	case EFVScreen::RestoreSave: NewScreen = FVMakeRestoreScreen(this); break;
	// ---- update 6
	case EFVScreen::Chopping: NewScreen = FVMakeChoppingScreen(this); break;
	case EFVScreen::Sawing: NewScreen = FVMakeSawingScreen(this); break;
	case EFVScreen::Forging: NewScreen = FVMakeForgingScreen(this); break;
	case EFVScreen::Excavation: NewScreen = FVMakeExcavationScreen(this); break;
	case EFVScreen::Builder: NewScreen = FVMakeBuilderScreen(this); break;
	case EFVScreen::YardBuilding: NewScreen = FVMakeYardBuildingScreen(this); break;
	case EFVScreen::GardenBed: NewScreen = FVMakeGardenBedScreen(this); break;
	case EFVScreen::Boat: NewScreen = FVMakeBoatScreen(this); break;
	case EFVScreen::Voyage: NewScreen = FVMakeVoyageScreen(this); break;
	case EFVScreen::CityBoard: NewScreen = FVMakeCityBoardScreen(this); break;
	case EFVScreen::Artel: NewScreen = FVMakeArtelScreen(this); break;
	case EFVScreen::ExpertTackle: NewScreen = FVMakeExpertTackleScreen(this); break;
	case EFVScreen::Mill: NewScreen = FVMakeMillScreen(this); break;   // update 6 (§18)
	default: break;
	}
	if (!NewScreen)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: screen %s could not be created"), FVGameLocal::ScreenName(Screen));
		return;
	}

	RetireActiveScreen();
	ActiveScreen = MoveTemp(NewScreen);
	CurrentScreen = Screen;
	ScreenContext = Context;       // before OnOpen: the screen reads it there
	bLmbActionLatched = false;
	if (Audio && Screen != EFVScreen::MainMenu)
	{
		Audio->Play(EFVSfx::Open);
	}
	ActiveScreen->OnOpen();
	UE_LOG(LogFV, Verbose, TEXT("FV: open screen %s (%s)"), FVGameLocal::ScreenName(Screen), *Context.ToString());
}

void UFVGame::CloseScreen()
{
	if (!ActiveScreen && CurrentScreen == EFVScreen::None)
	{
		return;
	}
	const bool bWasMainMenu = CurrentScreen == EFVScreen::MainMenu;
	RetireActiveScreen();
	CurrentScreen = EFVScreen::None;
	ScreenContext = NAME_None;
	if (!bInGame)
	{
		// No running game: the title screen is the only place to go.
		OpenScreen(EFVScreen::MainMenu);
		return;
	}
	if (Audio && !bWasMainMenu)
	{
		Audio->Play(EFVSfx::Close);
	}
	// update 4b: after the first-run name choice the practice-pond onboarding takes over (no "how to play" dialog)
}

bool UFVGame::TryOpenFromWorld(EFVScreen Screen)
{
	return TryOpenFromWorldWith(Screen, NAME_None);
}

bool UFVGame::TryOpenFromWorldWith(EFVScreen Screen, FName Context)
{
	if (!bInGame || !Fishing)
	{
		return false;
	}
	if (IsWorldBusy())
	{
		Toast(TEXT("Сначала закончите рыбалку"), FV::Col(255, 150, 130), EFVIcon::Exclaim);
		if (Audio)
		{
			Audio->Play(EFVSfx::Error, 0.7f);
		}
		return false;
	}
	OpenScreenWith(Screen, Context);
	return CurrentScreen == Screen;
}

// ============================================================================================
// Modals & toasts
// ============================================================================================
void UFVGame::ShowModal(FFVModal Modal)
{
	if (Modal.Buttons.Num() == 0)
	{
		Modal.Buttons.Add(TEXT("OK"));
	}
	if (Modals.Num() == 0)
	{
		ModalAge = 0.f;
	}
	Modals.Add(MoveTemp(Modal));
	bLmbActionLatched = false;
}

void UFVGame::Confirm(const FString& Title, const FString& Body, TFunction<void()> OnYes, const FString& YesLabel, const FString& NoLabel)
{
	FFVModal M;
	M.Title = Title;
	M.Body = Body;
	M.Icon = EFVIcon::Question;
	M.Buttons = { YesLabel, NoLabel };
	M.OnChoice = [OnYes = MoveTemp(OnYes)](int32 Index)
	{
		if (Index == 0 && OnYes)
		{
			OnYes();
		}
	};
	ShowModal(MoveTemp(M));
}

void UFVGame::ChooseModal(int32 ButtonIndex)
{
	if (Modals.Num() == 0)
	{
		return;
	}
	FFVModal M = MoveTemp(Modals[0]);
	Modals.RemoveAt(0);
	ModalAge = 0.f;

	const int32 NumButtons = FMath::Max(1, M.Buttons.Num());
	const int32 Index = (ButtonIndex < 0 || ButtonIndex >= NumButtons) ? NumButtons - 1 : ButtonIndex;
	if (Audio)
	{
		Audio->Play(EFVSfx::Click);
	}
	if (M.OnChoice)
	{
		M.OnChoice(Index);
	}
}

void UFVGame::DismissModal(int32 ButtonIndex)
{
	ChooseModal(ButtonIndex);
}

void UFVGame::Toast(const FString& Text, const FLinearColor& Color, EFVIcon Icon)
{
	if (Text.IsEmpty())
	{
		return;
	}
	// Same message still on screen: refresh it instead of stacking duplicates.
	for (FToast& T : Toasts)
	{
		if (T.Text == Text && T.Age < FVGameLocal::ToastLife - FVGameLocal::ToastOut)
		{
			T.Age = FMath::Min(T.Age, FVGameLocal::ToastIn);
			return;
		}
	}
	FToast T;
	T.Text = Text;
	T.Color = Color;
	T.Icon = Icon;
	T.Age = 0.f;
	T.Y = -1.f;
	Toasts.Add(MoveTemp(T));
	while (Toasts.Num() > 12)
	{
		Toasts.RemoveAt(0);
	}
}

void UFVGame::ShowHelp()
{
	// Update 3: short on purpose: everything else (items, stations, nets, food...) is in the guide «?».
	FFVModal M;
	M.Title = TEXT("Как играть");
	M.Icon = EFVIcon::Question;
	M.Body = TEXT("Добро пожаловать в Клёвую Долину! Ловите рыбу, продавайте улов в лавках и обустраивайте свой дом.");
	if (Touch)
	{
		M.Lines = {
			TEXT("Проведите пальцем — рыбак пойдёт (джойстик появится под пальцем)"),
			TEXT("У воды зажмите палец — замах, отпустите — заброс. «!» — коснитесь, чтобы подсечь"),
			TEXT("Рядом с предметом, лавкой или дверью просто коснитесь экрана — рыбак воспользуется ими"),
			TEXT("Кнопка «?» вверху — учебник: всё о рыбалке, предметах и доме"),
		};
		M.Buttons = { TEXT("Поехали!") };
		ShowModal(MoveTemp(M));
		return;
	}
	M.Lines = {
		TEXT("WASD / стрелки — ходить; Пробел (или ЛКМ) у воды — заброс, держите для силы"),
		TEXT("«!» — нажмите, чтобы подсечь; держите кнопку, чтобы вести рыбу в зелёной полосе"),
		TEXT("Рядом с предметом, лавкой или дверью — щелчок в любом месте или E"),
		TEXT("Кнопка «?» вверху или F1 — учебник: всё о рыбалке, предметах и доме"),
		TEXT("Esc — меню и пауза, F11 — полный экран"),
	};
	M.Buttons = { TEXT("Поехали!") };
	ShowModal(MoveTemp(M));
}

// ============================================================================================
// Game flow
// ============================================================================================
bool UFVGame::HasSaveGame() const
{
	return UFVSaveGame::SlotExists();
}

void UFVGame::EnterWorld(FName LocationId)
{
	if (!Profile || !Scene || !Fishing)
	{
		return;
	}
	const FFVLocationDef* Loc = FVDB::FindLocation(LocationId);
	if (!Loc || !Profile->IsLocationUnlocked(LocationId))
	{
		Loc = FVDB::FindLocation(FVDB::StartLocation());
	}
	if (!Loc)
	{
		const TArray<FFVLocationDef>& All = FVDB::Locations();
		Loc = All.Num() > 0 ? &All[0] : nullptr;
	}

	bInGame = true;
	RetireActiveScreen();
	CurrentScreen = EFVScreen::None;
	ScreenContext = NAME_None;
	bLmbActionLatched = false;
	bPrevActionDown = false;
	AutosaveTimer = 0.f;

	Fishing->Reset();
	if (Loc)
	{
		Scene->LoadLocation(Loc->Id);
		Profile->CurrentLocation = Loc->Id;
	}
	Scene->RefreshAppearance();

	PendingInteractKey = NAME_None;
	if (Audio)
	{
		Audio->SetReeling(false);
		Audio->SetMusicTheme(Loc ? MusicThemeFor(Loc->Id) : EFVTheme::Pond);
		Audio->SetRain(GetWeather() == EFVWeather::Rain);
	}
	if (Loc)
	{
		Toast(Loc->Name, FVPal::White(), FVDB::IsHomeId(Loc->Id) ? EFVIcon::House : EFVIcon::Map);
	}
	UE_LOG(LogFV, Log, TEXT("FV: entered the world at %s (day %d, %s)"), Loc ? *Loc->Id.ToString() : TEXT("?"), GetDay(), *GetClockText());

	// update 4: a fisherman without a name (first run, or a save from before the online update) chooses one first
	FVGameU6::OnEnterWorld(*this);   // update 6
	if (!IsAutoTest() && Profile->PlayerName.TrimStartAndEnd().IsEmpty())
	{
		OpenScreenWith(EFVScreen::Profile, FVGameLocal::SetupContext);
	}
	if (Online)
	{
		Online->RequestSync(false);
	}
}

void UFVGame::NewGame()
{
	UFVSaveGame* Fresh = NewObject<UFVSaveGame>(this);
	Fresh->ResetToNewGame();
	if (Profile)
	{
		// Audio preferences belong to the player, not to the save.
		Fresh->SfxVolume = Profile->SfxVolume;
		Fresh->MusicVolume = Profile->MusicVolume;
		// Update 4: so do the chosen name and motto. The online identity does NOT carry over: the server never lets
		// counters go down, so a fresh start registers as a new fisherman (a new «#номер»).
		Fresh->PlayerName = Profile->PlayerName;
		Fresh->PlayerStatus = Profile->PlayerStatus;
	}
	Profile = Fresh;
	Profile->Save();

	Modals.Reset();
	Toasts.Reset();
	// Update 4b: a new fisherman starts in his yard next to the practice pond (ResetToNewGame: CurrentLocation = home,
	// OnboardingStep = 0). A nameless one first chooses his name (EnterWorld opens the setup profile), then the
	// onboarding coach marks lead him from the practice pond to the village pond. The "how to play" dialog is gone:
	// the onboarding replaces it (the guide «?» stays in the HUD).
	if (Scene && Scene->GetLocationId() == FVDB::HouseId())
	{
		// the backdrop was the old save's house: leave it first, or the yard would put the new fisherman at the door
		Scene->LoadLocation(FVDB::HomeId());
	}
	EnterWorld(Profile->CurrentLocation.IsNone() ? FVDB::HomeId() : Profile->CurrentLocation);
	UE_LOG(LogFV, Log, TEXT("FV: new game started at %s (onboarding step %d)"), *Profile->CurrentLocation.ToString(), Profile->OnboardingStep);
}

bool UFVGame::ContinueGame()
{
	UFVSaveGame* Loaded = UFVSaveGame::Load();
	if (!Loaded)
	{
		Toast(TEXT("Сохранение не найдено"), FV::Col(255, 150, 130), EFVIcon::Exclaim);
		if (Audio)
		{
			Audio->Play(EFVSfx::Error);
		}
		return false;
	}
	Profile = Loaded;
	if (Audio)
	{
		Audio->SetSfxVolume(Profile->SfxVolume);
		Audio->SetMusicVolume(Profile->MusicVolume);
	}
	Modals.Reset();
	Toasts.Reset();
	EnterWorld(Profile->CurrentLocation);
	// update 5: achievements added by an update (the legend of every location) complete at once for an old save
	AnnounceAchievements(Profile->CheckAchievements());
	UE_LOG(LogFV, Log, TEXT("FV: save loaded (day %d, level %d, %lld coins)"), Profile->Day, Profile->GetLevel(), (long long)Profile->Money);
	return true;
}

void UFVGame::ReturnToMainMenu()
{
	SaveNow();
	if (Fishing)
	{
		Fishing->Reset();
	}
	Modals.Reset();
	bInGame = false;
	bLmbActionLatched = false;
	if (Audio)
	{
		Audio->SetReeling(false);
		Audio->SetRain(false);
		Audio->SetMusicTheme(EFVTheme::Count);
	}
	OpenScreen(EFVScreen::MainMenu);
}

void UFVGame::SaveNow()
{
	AutosaveTimer = 0.f;
	if (!Profile || !bInGame)
	{
		return;
	}
	if (Profile->Save())
	{
		bProfileDirty = false;
	}
	else
	{
		UE_LOG(LogFV, Warning, TEXT("FV: saving failed"));
	}
}

void UFVGame::QuitGame()
{
	SaveNow();
	if (Online)
	{
		Online->NotifyAppPause();   // the last sync / backup (best effort: the app may close before the answer)
	}
	UE_LOG(LogFV, Log, TEXT("FV: quit requested"));
	UWorld* World = GetWorld();
	APlayerController* P = PC.Get();
	if (World && P)
	{
		UKismetSystemLibrary::QuitGame(World, P, EQuitPreference::Quit, false);
	}
	else
	{
		FPlatformMisc::RequestExit(false);
	}
}

bool UFVGame::TravelTo(FName LocationId)
{
	const FFVLocationDef* Loc = FVDB::FindLocation(LocationId);
	if (!Loc || !Profile || !Scene || !Fishing)
	{
		return false;
	}
	if (!Profile->IsLocationUnlocked(LocationId) && (FVDBU6::IsForestId(LocationId) || FVDBU6::IsExpertLocation(LocationId)))
	{
		Profile->UnlockLocation(LocationId);   // update 6: forest (level 4), City (boat), Cave (rubble) — free, by condition
	}
	if (!Profile->IsLocationUnlocked(LocationId))
	{
		Toast(TEXT("Это место ещё закрыто"), FV::Col(255, 150, 130), EFVIcon::Lock);
		if (Audio)
		{
			Audio->Play(EFVSfx::Error);
		}
		return false;
	}

	Fishing->Reset();
	PendingInteractKey = NAME_None;
	const FName FromLocation = Scene->GetLocationId();
	Scene->LoadLocation(Loc->Id);
	Scene->RefreshAppearance();
	Profile->CurrentLocation = Loc->Id;
	if (CurrentScreen == EFVScreen::Map)
	{
		RetireActiveScreen();
		CurrentScreen = EFVScreen::None;
		ScreenContext = NAME_None;
	}
	if (Audio)
	{
		Audio->SetReeling(false);
		Audio->SetMusicTheme(MusicThemeFor(Loc->Id));
		Audio->SetRain(GetWeather() == EFVWeather::Rain);
	}
	Toast(Loc->Name, FVPal::White(), FVDB::IsHomeId(Loc->Id) ? EFVIcon::House : EFVIcon::Map);
	FVGameU6::OnTravel(*this, FromLocation, Loc->Id);   // update 6: first arrival in the City...
	bProfileDirty = true;
	SaveNow();
	UE_LOG(LogFV, Log, TEXT("FV: travelled to %s"), *Loc->Id.ToString());
	return true;
}

void UFVGame::NotifyProfileChanged()
{
	if (!Profile)
	{
		return;
	}
	AnnounceAchievements(Profile->CheckAchievements());
	if (Scene)
	{
		Scene->RefreshAppearance();
	}
	bProfileDirty = true;
}

void UFVGame::ToggleFullscreen()
{
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		return;
	}
	// Decide from the real window state (the command line may have overridden the saved settings).
	bool bFullscreenNow = Settings->GetFullscreenMode() != EWindowMode::Windowed;
	if (GEngine->GameViewport)
	{
		if (TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			bFullscreenNow = Window->GetWindowMode() != EWindowMode::Windowed;
		}
	}
	if (bFullscreenNow)
	{
		Settings->SetFullscreenMode(EWindowMode::Windowed);
		Settings->SetScreenResolution(FIntPoint(1280, 720));
	}
	else
	{
		Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		Settings->SetScreenResolution(Settings->GetDesktopResolution());
	}
	Settings->ApplySettings(false);
	UE_LOG(LogFV, Log, TEXT("FV: fullscreen %s"), bFullscreenNow ? TEXT("off") : TEXT("on"));
}

void UFVGame::SetSfxVolume(float V)
{
	V = FMath::Clamp(V, 0.f, 1.f);
	if (Audio)
	{
		Audio->SetSfxVolume(V);
	}
	if (Profile)
	{
		Profile->SfxVolume = V;
		bProfileDirty = true;
	}
}

void UFVGame::SetMusicVolume(float V)
{
	V = FMath::Clamp(V, 0.f, 1.f);
	if (Audio)
	{
		Audio->SetMusicVolume(V);
	}
	if (Profile)
	{
		Profile->MusicVolume = V;
		bProfileDirty = true;
	}
}

// ============================================================================================
// Fishing callbacks
// ============================================================================================
void UFVGame::ApplyTreasure(FFVCatchResult& Result)
{
	if (!Result.bTreasure || !Profile)
	{
		return;
	}
	FFVTreasure& T = Result.Treasure;
	Profile->Stats.Treasures++;

	TArray<FString> Parts;
	if (!T.CosmeticId.IsNone())
	{
		const FFVCosmeticDef* Cos = FVDB::FindCosmetic(T.CosmeticId);
		if (Cos && !Profile->OwnsCosmetic(T.CosmeticId))
		{
			Profile->GrantCosmetic(T.CosmeticId);
			Parts.Add(Cos->Name);
		}
		else
		{
			// Already owned (or unknown): pay out its worth instead.
			const int64 Worth = FMath::Max<int64>(200, Cos ? Cos->Price / 2 : 0);
			T.Money += Worth;
			T.CosmeticId = NAME_None;
		}
	}
	if (!T.ItemId.IsNone() && T.ItemCount > 0)
	{
		// Items (tackle, glitter, materials) go to the backpack; a full backpack pays their worth instead.
		const int32 Count = FMath::Clamp(T.ItemCount, 1, 99);
		if (FVDB::FindItem(T.ItemId) && Profile->AddTo(EFVContainer::Backpack, T.ItemId, Count))
		{
			Parts.Add(Count > 1 ? FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(T.ItemId), Count) : FVDB::ItemName(T.ItemId));
		}
		else
		{
			const int64 Worth = FMath::Max<int64>(20, int64(FMath::Max(1, FVDB::ItemPriceEx(T.ItemId, EFVQuality::Normal, 0))) * Count);
			if (FVDB::FindItem(T.ItemId))
			{
				Toast(TEXT("Рюкзак полон — находку из сокровища пришлось продать"), FV::Col(255, 210, 110), EFVIcon::Backpack);
			}
			T.Money += Worth;
			T.ItemId = NAME_None;
			T.ItemCount = 0;
		}
	}
	if (T.BaitCount > 0 && FVDB::FindBait(T.BaitId))
	{
		Profile->AddBait(T.BaitId, T.BaitCount);
		Parts.Insert(FString::Printf(TEXT("%s ×%d"), *FVDB::ItemName(T.BaitId), T.BaitCount), 0);
	}
	if (T.Money > 0)
	{
		Profile->Earn(T.Money, false);
		Parts.Insert(FString::Printf(TEXT("+%s"), *FVGameLocal::CoinsText(T.Money)), 0);
	}
	if (Parts.Num() > 0)
	{
		Toast(FString::Printf(TEXT("Сокровище: %s"), *FString::Join(Parts, TEXT(", "))), FVPal::Gold(), EFVIcon::Treasure);
	}
}

void UFVGame::HandleCatch(FFVCatchResult& Result)
{
	if (!Profile)
	{
		return;
	}
	FVGameU6::BeforeCatch(*this, Result);   // update 6: scrap junk -> item, expert catch data

	if (Result.Fish)
	{
		const UFVSaveGame::FRecordResult Rec = Profile->RecordCatch(*Result.Fish, Result.SizeCm, Result.Quality, Result.bPerfect);
		Result.bNewSpecies = Rec.bNewSpecies;
		Result.bNewRecord = Rec.bNewRecord;
		Result.bStored = Profile->AddToKeepnet(Result.Fish->Id, Result.Quality, 1);

		// ---- update 4: profile stats (favourite location, best catch) and the tournament's biggest fish
		const FName CatchLoc = Scene ? Scene->GetLocationId() : Profile->CurrentLocation;
		if (!CatchLoc.IsNone())
		{
			Profile->CatchesByLocation.FindOrAdd(CatchLoc) += 1;
		}
		const float Size = FMath::IsFinite(Result.SizeCm) ? FMath::Max(0.f, Result.SizeCm) : 0.f;
		const int32 UnitPrice = FVDB::FishPrice(*Result.Fish, Result.Quality);
		const bool bBest = Profile->BestCatch.FishId.IsNone() || UnitPrice > Profile->BestCatchPrice
			|| (UnitPrice == Profile->BestCatchPrice && Size > Profile->BestCatch.SizeCm);
		if (bBest)
		{
			Profile->BestCatch.FishId = Result.Fish->Id;
			Profile->BestCatch.SizeCm = Size;
			Profile->BestCatch.Quality = uint8(Result.Quality);
			Profile->BestCatchPrice = UnitPrice;
		}
		if (Profile->PendingSeasonBest.FishId.IsNone() || Size > Profile->PendingSeasonBest.SizeCm)
		{
			Profile->PendingSeasonBest.FishId = Result.Fish->Id;
			Profile->PendingSeasonBest.SizeCm = Size;
			Profile->PendingSeasonBest.Quality = uint8(Result.Quality);
		}
	}
	else if (Result.Junk)
	{
		// Junk is not fish: it goes to the backpack (the workshop recycles it into materials).
		Profile->RecordJunk();
		Result.bStored = Profile->AddTo(EFVContainer::Backpack, Result.Junk->Id, 1);
	}

	if (Result.Fish)
	{
		if (!Result.bStored)
		{
			Toast(TEXT("Садок полон — улов пришлось отпустить"), FV::Col(255, 150, 130), EFVIcon::Keepnet);
		}
		else if (Profile->KeepnetFull())
		{
			Toast(TEXT("Садок полон — продайте улов в лавке или уберите рыбу в ледник"), FV::Col(255, 210, 110), EFVIcon::Keepnet);
		}
	}
	else if (Result.Junk && !Result.bStored)
	{
		Toast(TEXT("Рюкзак полон — мусор выброшен"), FV::Col(255, 150, 130), EFVIcon::Backpack);
	}

	ApplyTreasure(Result);

	// Experience & level-ups
	const int32 OldLevel = Profile->GetLevel();
	Result.LevelsGained = FMath::Max(0, Profile->AddXp(FMath::Max(0, Result.Xp)));
	if (Result.LevelsGained > 0)
	{
		const int32 NewLevel = Profile->GetLevel();
		if (Audio)
		{
			Audio->Play(EFVSfx::LevelUp);
		}
		FFVModal M;
		M.Title = FString::Printf(TEXT("Новый уровень рыбалки: %d!"), NewLevel);
		M.Icon = EFVIcon::Level;
		// update 6 (§9): the bar bonus ends at level 10; levels 11-15 are the expert zone's
		if (NewLevel > FV::ClassicMaxLevel)
		{
			M.Body = TEXT("Экспертный уровень: в Городе и Пещере открываются новые снасти, задания и места.");
		}
		else
		{
			M.Body = TEXT("Полоса улова стала длиннее — рыбе будет труднее вырваться.");
		}
		if (Profile->IsXpFrozen())
		{
			M.Body += TEXT("\nОпыт на максимуме — плывите в Город: до первого прибытия туда он больше не копится.");
			FVGameLocal::GXpFrozenToastShown = true;   // said here already
		}
		for (int32 L = OldLevel + 1; L <= NewLevel; ++L)
		{
			M.Lines.Append(FVDB::UnlocksAtLevel(L));
			M.Lines.Append(FVDB::HomeUnlocksAtLevel(L));
		}
		M.Buttons = { TEXT("Отлично!") };
		ShowModal(MoveTemp(M));
		UE_LOG(LogFV, Log, TEXT("FV: level up %d -> %d"), OldLevel, NewLevel);
	}
	else if (Result.Xp > 0 && Profile->IsXpFrozen() && !FVGameLocal::GXpFrozenToastShown)
	{
		// update 6 (Р1): the XP stands still at level 10 until the first arrival in the City — say it once per session
		FVGameLocal::GXpFrozenToastShown = true;
		Toast(TEXT("Опыт на максимуме — плывите в Город"), FV::Col(150, 210, 250), EFVIcon::Boat);
	}

	AnnounceAchievements(Profile->CheckAchievements());
	FVGameU6::AfterCatch(*this, Result);    // update 6: journal page facts, expert records

	bProfileDirty = true;
	SaveNow();

	// update 4: the leaderboards learn about a new level or a legendary fish soon (the sync is coalesced)
	if (Online && (Result.LevelsGained > 0 || (Result.Fish && Result.Fish->bLegendary)))
	{
		Online->NotifyImportantEvent();
	}
}

void UFVGame::AnnounceAchievements(const TArray<FName>& Ids)
{
	int32 Shown = 0;
	for (const FName& Id : Ids)
	{
		const FFVAchievementDef* A = FVDB::FindAchievement(Id);
		if (!A)
		{
			continue;
		}
		FString Reward;
		if (A->RewardMoney > 0)
		{
			Reward += FString::Printf(TEXT(" · +%s"), *FVGameLocal::CoinsText(A->RewardMoney));
		}
		if (!A->RewardCosmetic.IsNone())
		{
			// update 5: the legend skins have adjective names («Царский»): say what they are
			const FFVCosmeticDef* C = FVDB::FindCosmetic(A->RewardCosmetic);
			Reward += (C && C->Slot == EFVCosmeticSlot::Bobber)
				? FString::Printf(TEXT(" · поплавок «%s»"), *C->Name)
				: FString::Printf(TEXT(" · %s"), *FVDB::ItemName(A->RewardCosmetic));
		}
		if (!A->RewardDecor.IsNone())
		{
			Reward += FString::Printf(TEXT(" · %s"), *FVDB::ItemName(A->RewardDecor));
		}
		Toast(FString::Printf(TEXT("Достижение «%s»%s"), *A->Name, *Reward), FVPal::Gold(), EFVIcon::Trophy);
		// 0.6.2 (Р81–Р83): the achievement unlocks a rod — say where to get it now
		for (const FFVRodDef& Rod : FVDB::Rods())
		{
			if (FVDBU6::RodRequiredAchievement(Rod) != Id) { continue; }
			FString Short = Rod.Name;
			Short.RemoveFromStart(TEXT("Удочка "));
			Toast(FVDBU6::RodRecipe(Rod.Id) ? FString::Printf(TEXT("В Закалочной кузне можно выковать %s"), *Short)
				: FString::Printf(TEXT("Кузьма теперь продаёт удочку %s"), *Short), FVPal::Gold(),
				FVDBU6::RodRecipe(Rod.Id) ? EFVIcon::Anvil : (Rod.bExpert ? EFVIcon::ExpertRod : EFVIcon::Rod));
		}
		UE_LOG(LogFV, Log, TEXT("FV: achievement %s"), *Id.ToString());
		++Shown;
	}
	if (Shown > 0)
	{
		if (Audio)
		{
			Audio->Play(EFVSfx::Achievement);
		}
		if (Scene)
		{
			Scene->RefreshAppearance();   // rewards may include cosmetics
		}
		bProfileDirty = true;
		if (Online)
		{
			Online->NotifyImportantEvent();
		}
	}
}

void UFVGame::HandleEscape(const FFVFishDef* Fish)
{
	if (Profile)
	{
		Profile->Stats.Escaped++;
		bProfileDirty = true;
	}
}

FFVCatchContext UFVGame::MakeCatchContext(float Depth) const
{
	FFVCatchContext Ctx;
	Ctx.Location = Scene ? Scene->GetLocationId() : (Profile ? Profile->CurrentLocation : FVDB::StartLocation());
	Ctx.Depth = FMath::Clamp(Depth, 0.f, 1.f);
	Ctx.PhaseMask = GetPhaseMask();
	Ctx.Weather = GetWeather();
	Ctx.Level = Profile ? Profile->GetLevel() : 1;
	Ctx.Rod = Profile ? &Profile->GetRod() : (FVDB::Rods().Num() > 0 ? &FVDB::Rods()[0] : nullptr);
	Ctx.Bait = Profile ? Profile->GetBait() : nullptr;
	if (Profile && Ctx.Rod)
	{
		// Tackle + food: value fields (never point Ctx.Rod at a temporary).
		const FFVRodStats Eff = GetEffectiveRodStats();
		Ctx.ExtraLuck = Eff.Luck - Ctx.Rod->Luck;
		Ctx.JunkMultScale = FMath::Max(0.f, Eff.JunkMult);
		// update 5: the installed tackle (legend gates / checklist)
		const FFVRodLoadout L = Profile->GetLoadout(Ctx.Rod->Id);
		const FFVItemDef* LineDef = FVDB::FindItem(L.Line);
		const FFVItemDef* FloatDef = FVDB::FindItem(L.Float);
		if (LineDef && LineDef->Kind == EFVItemKind::Line) { Ctx.Line = L.Line; Ctx.LineTier = LineDef->Tier; }
		if (FloatDef && FloatDef->Kind == EFVItemKind::Float) { Ctx.Float = L.Float; }
	}
	FVGameU6::FillCatchContext(*this, Ctx);   // update 6
	return Ctx;
}

// ============================================================================================
// Clock & weather
// ============================================================================================
float UFVGame::GetClockMinutes() const
{
	return Profile ? FMath::Clamp(Profile->TimeMinutes, 0.f, 1439.99f) : FV::DayStartMinutes;
}

EFVDayPhase UFVGame::GetPhase() const
{
	const float H = GetClockMinutes() / 60.f;
	if (H >= 6.f && H < 12.f) { return EFVDayPhase::Morning; }
	if (H >= 12.f && H < 18.f) { return EFVDayPhase::Day; }
	if (H >= 18.f && H < 22.f) { return EFVDayPhase::Evening; }
	return EFVDayPhase::Night;
}

EFVWeather UFVGame::GetWeather() const
{
	if (!Profile || Profile->Weather >= uint8(EFVWeather::Count))
	{
		return EFVWeather::Sunny;
	}
	return EFVWeather(Profile->Weather);
}

int32 UFVGame::GetDay() const
{
	return Profile ? FMath::Max(1, Profile->Day) : 1;
}

FString UFVGame::GetClockText() const
{
	// Stardew shows the time in 10-minute steps.
	const int32 Total = FMath::FloorToInt(GetClockMinutes());
	const int32 Hours = (Total / 60) % 24;
	const int32 Minutes = ((Total % 60) / 10) * 10;
	return FString::Printf(TEXT("%02d:%02d"), Hours, Minutes);
}

float UFVGame::GetNightFactor() const
{
	const float H = GetClockMinutes() / 60.f;
	if (H >= 8.f && H <= 17.f)
	{
		return 0.f;
	}
	if (H > 17.f && H < 22.f)
	{
		return FMath::SmoothStep(0.f, 1.f, (H - 17.f) / 5.f);   // dusk
	}
	if (H >= 4.f && H < 8.f)
	{
		return 1.f - FMath::SmoothStep(0.f, 1.f, (H - 4.f) / 4.f);   // dawn
	}
	return 1.f;   // 22:00 .. 04:00
}

void UFVGame::SetClockMinutes(float Minutes)
{
	if (Profile)
	{
		Profile->TimeMinutes = FMath::Fmod(FMath::Fmod(Minutes, 1440.f) + 1440.f, 1440.f);
		bProfileDirty = true;
	}
}

void UFVGame::SetWeather(EFVWeather NewWeather)
{
	if (!Profile || NewWeather >= EFVWeather::Count)
	{
		return;
	}
	Profile->Weather = uint8(NewWeather);
	bProfileDirty = true;
	if (Audio && bInGame)
	{
		Audio->SetRain(NewWeather == EFVWeather::Rain);
	}
}

// ============================================================================================
// Autotest helpers
// ============================================================================================
void UFVGame::RequestShot(const FString& Name)
{
	// -fvshotdir=<name> writes into Saved/<name> (parallel test runs keep their screenshots apart).
	FString SubDir = TEXT("AutoTest");
	FString Custom;
	if (FParse::Value(FCommandLine::Get(), TEXT("fvshotdir="), Custom) && !Custom.IsEmpty())
	{
		SubDir = Custom;
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / SubDir);
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = Dir / (Name + TEXT(".png"));
	// 0.5 crash fix: never ask for a UI screenshot while Slate does not draw the window (it would fail anyway and leave
	// the renderer a dangling bitmap pointer — see FVGameLocal::CanTakeUIShot). The autotest simply goes on without it.
	FString Why;
	if (!FVGameLocal::CanTakeUIShot(Why))
	{
		UE_LOG(LogFV, Warning, TEXT("FV: screenshot SKIPPED %s: %s"), *File, *Why);
		return;
	}
	FScreenshotRequest::RequestScreenshot(File, true, false);
	UE_LOG(LogFV, Log, TEXT("FV: screenshot requested %s"), *File);
}
