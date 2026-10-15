// Text entry for canvas UIs (update 4): names, status, search, restore codes. See FVTextInput.h.
//
// Desktop: a Slate input pre-processor sees every key first while a field is focused: Enter submits, Esc ends,
// BackSpace deletes (the OS key repeat repeats it; Ctrl+BackSpace deletes a word), Ctrl+V / Shift+Insert paste, and
// every other key-down is swallowed so letters never reach the game as hotkeys or movement. UE 5.8's IInputProcessor
// has no character hook, so the typed characters (with the player's keyboard layout, Cyrillic included) are read by a
// tiny invisible focusable leaf widget on the game viewport that holds the keyboard focus while typing.
// Phones: the native virtual keyboard is shown through Slate with our own IVirtualKeyboardEntry (text updates while
// typing, "Done" = submit, dismissing the keyboard ends the edit).

#include "FVTextInput.h"
#include "FVAudio.h"
#include "FVDraw.h"
#include "FVUIKit.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Input/Events.h"
#include "Layout/Visibility.h"
#include "CoreGlobals.h"
#include "Widgets/Input/IVirtualKeyboardEntry.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"

namespace FVTextInputImpl
{
	class FProcessor;
	class SCharCatcher;
	class FKeyboardEntry;

	struct FState
	{
		bool bStarted = false;
		bool bActive = false;
		FString Text;
		int32 MaxLen = 16;
		bool bCodeMode = false;              // letters & digits only (restore codes): upper-case Latin
		bool bSubmitPending = false;         // Enter / "Done" pressed (ConsumeSubmit / the field that owned it)
		uint32 OwnerKey = 0;                 // FFVTextField::Key of the focused field (0 = Begin() called directly)
		uint32 SubmitKey = 0;                // the owner when Enter / "Done" was pressed
		double ChangeTime = 0.0;             // caret stays solid right after typing
		uint64 OwnerDrawFrame = 0;           // GFrameCounter when the owner field was drawn last (watchdog)
		bool bUseVirtualKeyboard = false;    // phones: the OS keyboard delivers the text
		bool bKeyboardShown = false;
		bool bWantFocus = false;             // desktop: move the keyboard focus to the catcher (applied in Tick)
		// the focused field, for the phone "typing preview" overlay (the keyboard may cover the field)
		float FieldBottom = 0.f;             // untranslated virtual y of the field's bottom edge
		FString Placeholder;

		TSharedPtr<FProcessor> Processor;
		TSharedPtr<SCharCatcher> Catcher;
		TSharedPtr<SWidget> CatcherHost;
		TWeakObjectPtr<UGameViewportClient> CatcherViewport;
		TSharedPtr<FKeyboardEntry> Entry;    // kept alive here: the platform code only holds a weak pointer
	};

	FState& S()
	{
		static FState State;
		return State;
	}

	double Now() { return FPlatformTime::Seconds(); }

	void Sfx(EFVSfx Sound, float Volume, float Pitch = 1.f)
	{
		if (UFVAudio* A = UFVAudio::Get())
		{
			A->Play(Sound, Volume, Pitch);
		}
	}

	/** One typed / pasted character after filtering (0 = rejected). */
	TCHAR FilterChar(TCHAR C, bool bCode)
	{
		if (C == TCHAR('\t') || C == TCHAR('\n') || C == TCHAR('\r'))
		{
			C = TCHAR(' ');
		}
		const uint32 U = uint32(C);
		if (U < 32u || U == 127u || (U >= 0x80u && U < 0xA0u))
		{
			return 0;   // control characters (Ctrl+letter chords arrive as these)
		}
		if ((U >= 0xD800u && U <= 0xDFFFu) || (U >= 0xE000u && U <= 0xF8FFu))
		{
			return 0;   // emoji halves / private use: the font cannot draw them
		}
		if ((U >= 0x200Bu && U <= 0x200Fu) || (U >= 0x2028u && U <= 0x202Eu) || U == 0xFEFFu)
		{
			return 0;   // invisible formatting marks
		}
		if (!bCode)
		{
			return C;
		}
		if (C >= TCHAR('a') && C <= TCHAR('z'))
		{
			return TCHAR(C - TCHAR('a') + TCHAR('A'));
		}
		if ((C >= TCHAR('A') && C <= TCHAR('Z')) || (C >= TCHAR('0') && C <= TCHAR('9')))
		{
			return C;
		}
		// Codes are Latin: with a Russian layout the same key gives the Latin letter (ЙЦУКЕН -> QWERTY).
		static const TCHAR* const Ru = TEXT("ЙЦУКЕНГШЩЗФЫВАПРОЛДЯЧСМИТЬйцукенгшщзфывапролдячсмить");
		static const TCHAR* const En = TEXT("QWERTYUIOPASDFGHJKLZXCVBNMQWERTYUIOPASDFGHJKLZXCVBNM");
		for (int32 i = 0; Ru[i] != 0; ++i)
		{
			if (Ru[i] == C)
			{
				return En[i];
			}
		}
		return 0;
	}

	FString FilterString(const FString& In, bool bCode, int32 MaxLen)
	{
		FString Out;
		Out.Reserve(FMath::Min(In.Len(), MaxLen));
		for (int32 i = 0; i < In.Len() && Out.Len() < MaxLen; ++i)
		{
			const TCHAR F = FilterChar(In[i], bCode);
			if (F != 0)
			{
				Out.AppendChar(F);
			}
		}
		return Out;
	}

	/** Appends filtered text; a soft "full" sound when nothing fits any more. */
	void AppendText(const FString& Add)
	{
		FState& St = S();
		if (!St.bActive || Add.IsEmpty())
		{
			return;
		}
		const FString F = FilterString(Add, St.bCodeMode, 4096);
		if (F.IsEmpty())
		{
			return;
		}
		const int32 Room = St.MaxLen - St.Text.Len();
		if (Room <= 0)
		{
			Sfx(EFVSfx::Error, 0.3f, 1.2f);
			return;
		}
		St.Text += F.Left(Room);
		St.ChangeTime = Now();
		Sfx(EFVSfx::Click, 0.2f, 1.3f + FMath::FRand() * 0.25f);
	}

	void DeleteBack(bool bWord)
	{
		FState& St = S();
		if (St.Text.IsEmpty())
		{
			return;
		}
		if (bWord)
		{
			// Ctrl+BackSpace: trailing spaces, then the last word
			int32 N = St.Text.Len();
			while (N > 0 && FChar::IsWhitespace(St.Text[N - 1])) { --N; }
			while (N > 0 && !FChar::IsWhitespace(St.Text[N - 1])) { --N; }
			St.Text.LeftInline(N);
		}
		else
		{
			St.Text.LeftChopInline(1);
		}
		St.ChangeTime = Now();
		Sfx(EFVSfx::Click, 0.16f, 0.9f);
	}

	void Paste()
	{
		FString Clip;
		FPlatformApplicationMisc::ClipboardPaste(Clip);
		AppendText(Clip);
	}

	void EndEdit();

	void Submit()
	{
		FState& St = S();
		if (!St.bActive)
		{
			return;
		}
		St.bSubmitPending = true;
		St.SubmitKey = St.OwnerKey;
		EndEdit();
	}

	// ------------------------------------------------------------------ desktop: keys (pre-processor)
	class FProcessor : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			FState& St = S();
			if (!St.bActive)
			{
				return false;
			}
			const FKey Key = InKeyEvent.GetKey();
			if (Key == EKeys::F11)
			{
				return false;   // fullscreen still works while typing
			}
			if (Key == EKeys::Escape || Key == EKeys::Android_Back || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
			{
				if (!InKeyEvent.IsRepeat())
				{
					EndEdit();
				}
				return true;
			}
			if (Key == EKeys::Enter)
			{
				if (!InKeyEvent.IsRepeat())
				{
					Submit();
				}
				return true;
			}
			if (Key.IsMouseButton() || Key.IsTouch() || Key.IsGamepadKey())
			{
				return false;
			}
			if (!St.bUseVirtualKeyboard)
			{
				// the OS key repeat delivers the held BackSpace again and again
				if (Key == EKeys::BackSpace)
				{
					DeleteBack(InKeyEvent.IsControlDown());
					return true;
				}
				if ((Key == EKeys::V && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown())) || (Key == EKeys::Insert && InKeyEvent.IsShiftDown()))
				{
					if (!InKeyEvent.IsRepeat())
					{
						Paste();
					}
					return true;
				}
			}
			return true;   // no hotkeys, no walking while typing (the characters still arrive as char events)
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("FVTextInput"); }
	};

	// ------------------------------------------------------------------ desktop: characters (focus widget)
	class SCharCatcher : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCharCatcher) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) {}

		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(1.f, 1.f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			return LayerId;   // invisible
		}

		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override
		{
			FState& St = S();
			if (!St.bActive || St.bUseVirtualKeyboard)
			{
				return FReply::Unhandled();
			}
			AppendText(FString::Chr(InCharacterEvent.GetCharacter()));
			return FReply::Handled();
		}
	};

	// ------------------------------------------------------------------ phones: the OS keyboard
	class FKeyboardEntry : public IVirtualKeyboardEntry
	{
	public:
		virtual void SetTextFromVirtualKeyboard(const FText& InNewText, ETextEntryType TextEntryType) override
		{
			FState& St = S();
			if (!St.bActive || St.Entry.Get() != this)
			{
				return;   // a late callback of a keyboard that belonged to an earlier edit
			}
			const FString NewText = FilterString(InNewText.ToString(), St.bCodeMode, St.MaxLen);
			if (NewText != St.Text)
			{
				St.Text = NewText;
				St.ChangeTime = Now();
			}
			if (TextEntryType == ETextEntryType::TextEntryAccepted)
			{
				Submit();
			}
			else if (TextEntryType == ETextEntryType::TextEntryCanceled)
			{
				EndEdit();   // the keyboard was dismissed: keep what was typed, no submit
			}
		}

		virtual void SetSelectionFromVirtualKeyboard(int InSelStart, int SelEnd) override {}

		virtual FText GetText() const override { return FText::FromString(S().Text); }

		virtual bool GetSelection(int& OutSelStart, int& OutSelEnd) override
		{
			OutSelStart = OutSelEnd = S().Text.Len();   // the caret always sits at the end
			return true;
		}

		virtual FText GetHintText() const override { return FText::FromString(S().Placeholder); }

		virtual EKeyboardType GetVirtualKeyboardType() const override
		{
			return S().bCodeMode ? EKeyboardType::Keyboard_AlphaNumeric : EKeyboardType::Keyboard_Default;
		}

		virtual FVirtualKeyboardOptions GetVirtualKeyboardOptions() const override { return FVirtualKeyboardOptions(); }

		virtual bool IsMultilineEntry() const override { return false; }
	};

	void ShowKeyboard()
	{
		FState& St = S();
		if (!St.bUseVirtualKeyboard || !FSlateApplication::IsInitialized())
		{
			return;
		}
		// A new entry object every time: showing the same entry again would toggle the keyboard off on Android.
		St.Entry = MakeShared<FKeyboardEntry>();
		FSlateApplication::Get().ShowVirtualKeyboard(true, 0, St.Entry);
		St.bKeyboardShown = true;
	}

	void HideKeyboard()
	{
		FState& St = S();
		if (St.bKeyboardShown && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ShowVirtualKeyboard(false, 0, St.Entry);
		}
		St.bKeyboardShown = false;
		St.Entry.Reset();
	}

	void EndEdit()
	{
		FState& St = S();
		if (!St.bActive)
		{
			return;
		}
		St.bActive = false;
		St.bWantFocus = false;
		St.OwnerKey = 0;
		HideKeyboard();
		// the keyboard focus goes back to the game viewport in Tick (never in the middle of a mouse gesture)
	}

	/** Desktop focus management: the catcher holds the keyboard focus while typing, the game viewport otherwise.
	    Never switched while a mouse button is held: the viewport flushes its pressed keys when it loses the focus. */
	void UpdateFocus()
	{
		FState& St = S();
		if (!St.Catcher.IsValid() || !FSlateApplication::IsInitialized())
		{
			return;
		}
		FSlateApplication& App = FSlateApplication::Get();
		if (App.GetPressedMouseButtons().Num() > 0)
		{
			return;
		}
		const bool bHasFocus = St.Catcher->HasKeyboardFocus();
		if (St.bActive)
		{
			if (!bHasFocus || St.bWantFocus)
			{
				App.SetKeyboardFocus(St.Catcher, EFocusCause::SetDirectly);
			}
			St.bWantFocus = false;
		}
		else if (bHasFocus)
		{
			App.SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
		}
	}

	// ------------------------------------------------------------------ drawing
	float CodeSpacing() { return FV::IsPortrait() ? 3.f : 2.f; }

	float MeasureField(const FVDraw& D, const FString& Text, float Size, bool bCode)
	{
		if (!bCode)
		{
			return D.MeasureText(Text, Size, true).X;
		}
		float W = 0.f;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			W += D.MeasureText(Text.Mid(i, 1), Size, true).X + (i > 0 ? CodeSpacing() : 0.f);
		}
		return W;
	}

	/** Draws Text at Pos (top-left); code mode spaces the characters out. Returns the width used. */
	float DrawFieldText(FVDraw& D, const FString& Text, const FVector2D& Pos, float Size, bool bCode, const FLinearColor& C)
	{
		if (!bCode)
		{
			D.Text(Text, Pos, Size, C, EFVAlign::Left, false, true);
			return D.MeasureText(Text, Size, true).X;
		}
		float X = Pos.X;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const FString Ch = Text.Mid(i, 1);
			D.Text(Ch, FVector2D(FMath::RoundToFloat(X), Pos.Y), Size, C, EFVAlign::Left, false, true);
			X += D.MeasureText(Ch, Size, true).X + CodeSpacing();
		}
		return FMath::Max(0.f, X - Pos.X - (Text.Len() > 0 ? CodeSpacing() : 0.f));
	}

	/**
	 * The text box (Stardew style): dark wooden outline, warm cream interior with an inset shadow, the text (the tail
	 * while typing, "…" at the end when idle), a blinking caret, the placeholder when empty and, while focused, a soft
	 * golden focus frame and a small "5/16" counter.
	 */
	void PaintField(FVDraw& D, const FFVRect& R, const FString& Text, const FString& Placeholder, bool bFocused, bool bHover, bool bCode, int32 MaxLen)
	{
		const bool bPortrait = FV::IsPortrait();
		if (bFocused)
		{
			const float Pulse = 0.6f + 0.4f * FMath::Abs(FMath::Sin(float(D.Time) * 2.6f));
			D.Frame(R.Inset(-2.f), FV::Alpha(FVPal::Gold(), 0.75f * Pulse), 1.f);
			D.Frame(R.Inset(-1.f), FV::Col(255, 236, 170, 110), 1.f);
		}
		D.Rect(FFVRect(R.X + 1.f, R.Bottom(), R.W - 1.f, 1.f), FV::Col(0, 0, 0, 55));
		D.Rect(R, bFocused ? FV::Col(150, 78, 26) : (bHover ? FVPal::Wood() : FVPal::WoodDark()));
		const FFVRect In = R.Inset(1.f);
		D.Rect(In, bFocused ? FV::Col(255, 247, 226) : (bHover ? FV::Col(255, 240, 208) : FV::Col(252, 233, 194)));
		D.Rect(FFVRect(In.X, In.Y, In.W, 1.f), FV::Col(212, 164, 104));          // inset shadow (top, left)
		D.Rect(FFVRect(In.X, In.Y + 1.f, 1.f, In.H - 1.f), FV::Col(230, 192, 138));
		D.Rect(FFVRect(In.X + 1.f, In.Y + 1.f, In.W - 1.f, 1.f), FV::Col(236, 206, 156, 120));

		const float TS = FMath::Clamp(FMath::RoundToFloat(R.H * 0.55f * 2.f) * 0.5f, 7.f, FV::Ui::Body() + (bPortrait ? 3.f : 2.f));
		const float LH = D.MeasureText(TEXT("Йy"), TS, true).Y;
		const float Pad = FMath::Clamp(FMath::RoundToFloat(R.H * 0.28f), 4.f, 9.f);
		const float TY = FMath::RoundToFloat(R.Y + (R.H - LH) * 0.5f);
		float TextX = R.X + Pad;
		float Right = R.Right() - Pad;

		// counter "5/16" at the right while typing (wide fields only)
		if (bFocused && MaxLen > 0 && MaxLen < 100 && R.W >= 100.f)
		{
			const float CS = FV::Ui::Small() - 0.5f;
			const FString Cnt = FString::Printf(TEXT("%d/%d"), Text.Len(), MaxLen);
			const FVector2D CSz = D.MeasureText(Cnt, CS, false);
			const FLinearColor CC = Text.Len() >= MaxLen ? FV::Col(196, 70, 48) : FV::Col(168, 130, 96);
			D.Text(Cnt, FVector2D(Right, FMath::RoundToFloat(R.Y + (R.H - CSz.Y) * 0.5f)), CS, CC, EFVAlign::Right, false, false);
			Right -= CSz.X + 5.f;
		}
		const float CaretW = 1.f;
		const float AvailW = FMath::Max(8.f, Right - TextX - (bFocused ? CaretW + 2.f : 0.f));

		float TextW = 0.f;
		if (Text.IsEmpty())
		{
			if (!Placeholder.IsEmpty())
			{
				const float PS = FMath::Min(TS, FV::Ui::Body() + (bPortrait ? 1.f : 0.f));
				const FString P = D.Ellipsize(Placeholder, FMath::Max(8.f, Right - TextX - (bFocused ? 4.f : 0.f)), PS, false);
				const float PY = FMath::RoundToFloat(R.Y + (R.H - D.MeasureText(TEXT("Йy"), PS, false).Y) * 0.5f);
				D.Text(P, FVector2D(TextX + (bFocused ? 3.f : 0.f), PY), PS, FV::Alpha(FV::Col(150, 112, 80), bFocused ? 0.55f : 0.85f), EFVAlign::Left, false, false);
			}
		}
		else if (MeasureField(D, Text, TS, bCode) <= AvailW)
		{
			TextW = DrawFieldText(D, Text, FVector2D(TextX, TY), TS, bCode, FVPal::Ink());
		}
		else if (bFocused)
		{
			// typing past the edge: show the tail after "…"
			const FString Ell = TEXT("…");
			const float EllW = D.MeasureText(Ell, TS, true).X;
			FString Tail = Text;
			while (Tail.Len() > 1 && MeasureField(D, Tail, TS, bCode) > AvailW - EllW)
			{
				Tail.RightChopInline(1);
			}
			D.Text(Ell, FVector2D(TextX, TY), TS, FVPal::InkSoft(), EFVAlign::Left, false, true);
			TextW = EllW + DrawFieldText(D, Tail, FVector2D(TextX + EllW, TY), TS, bCode, FVPal::Ink());
		}
		else
		{
			const FString Shown = D.Ellipsize(Text, AvailW, TS, true);
			TextW = DrawFieldText(D, Shown, FVector2D(TextX, TY), TS, false, FVPal::Ink());
		}

		// blinking caret (solid for a moment after every change)
		if (bFocused)
		{
			const double Since = Now() - S().ChangeTime;
			const bool bOn = Since < 0.5 || FMath::Fmod(float(Since - 0.5), 1.06f) < 0.53f;
			if (bOn)
			{
				const float CX = FMath::RoundToFloat(TextX + TextW + (Text.IsEmpty() ? 0.f : 1.f));
				const float CH = FMath::Max(6.f, FMath::RoundToFloat(LH * 0.9f));
				D.Rect(FFVRect(CX, FMath::RoundToFloat(R.Y + (R.H - CH) * 0.5f), CaretW, CH), FVPal::Ink());
			}
		}
	}
}

// ============================================================================================
// FVTextInput
// ============================================================================================
void FVTextInput::Startup()
{
	using namespace FVTextInputImpl;
	FState& St = S();
	if (St.bStarted)
	{
		return;
	}
	St.bStarted = true;
	St.bUseVirtualKeyboard = FPlatformApplicationMisc::RequiresVirtualKeyboard();
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogFV, Warning, TEXT("FV: text input: Slate is not initialized, typing is unavailable"));
		return;
	}
	St.Processor = MakeShared<FProcessor>();
	FSlateApplication::Get().RegisterInputPreProcessor(St.Processor, 0);

	if (!St.bUseVirtualKeyboard && GEngine && GEngine->GameViewport)
	{
		// 1x1 invisible focus target in the top-left corner of the game viewport (mouse input passes through it)
		St.CatcherHost = SNew(SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			[
				SAssignNew(St.Catcher, SCharCatcher)
			];
		GEngine->GameViewport->AddViewportWidgetContent(St.CatcherHost.ToSharedRef(), 10000);
		St.CatcherViewport = GEngine->GameViewport;
	}
	UE_LOG(LogFV, Log, TEXT("FV: text input ready (%s)"), St.bUseVirtualKeyboard ? TEXT("virtual keyboard") : TEXT("hardware keyboard"));
}

void FVTextInput::Shutdown()
{
	using namespace FVTextInputImpl;
	FState& St = S();
	if (St.bActive)
	{
		EndEdit();
	}
	HideKeyboard();
	if (St.CatcherHost.IsValid())
	{
		if (UGameViewportClient* VC = St.CatcherViewport.Get())
		{
			VC->RemoveViewportWidgetContent(St.CatcherHost.ToSharedRef());
		}
	}
	if (St.Processor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(St.Processor);
	}
	St.Processor.Reset();
	St.Catcher.Reset();
	St.CatcherHost.Reset();
	St.CatcherViewport.Reset();
	St.bSubmitPending = false;
	St.bStarted = false;
}

void FVTextInput::Begin(const FString& Initial, int32 MaxLen, bool bDigitsAndLettersOnly)
{
	using namespace FVTextInputImpl;
	FState& St = S();
	if (St.bActive)
	{
		EndEdit();   // another field was focused
	}
	St.MaxLen = FMath::Clamp(MaxLen, 1, 256);
	St.bCodeMode = bDigitsAndLettersOnly;
	St.Text = FilterString(Initial, St.bCodeMode, St.MaxLen);
	St.bActive = true;
	St.bSubmitPending = false;
	St.SubmitKey = 0;
	St.OwnerKey = 0;
	St.ChangeTime = Now();
	St.OwnerDrawFrame = GFrameCounter;
	if (St.bUseVirtualKeyboard)
	{
		ShowKeyboard();
	}
	else
	{
		St.bWantFocus = true;   // the focus moves in Tick (fields are focused while drawing: no Slate focus changes there)
	}
}

void FVTextInput::End()
{
	FVTextInputImpl::EndEdit();
}

bool FVTextInput::IsActive()
{
	return FVTextInputImpl::S().bActive;
}

const FString& FVTextInput::GetText()
{
	return FVTextInputImpl::S().Text;
}

void FVTextInput::SetText(const FString& Text)
{
	using namespace FVTextInputImpl;
	FState& St = S();
	St.Text = FilterString(Text, St.bCodeMode, St.MaxLen);
	St.ChangeTime = Now();
}

bool FVTextInput::ConsumeSubmit()
{
	FVTextInputImpl::FState& St = FVTextInputImpl::S();
	const bool bWas = St.bSubmitPending;
	St.bSubmitPending = false;
	return bWas;
}

void FVTextInput::Tick(float Dt)
{
	using namespace FVTextInputImpl;
	FState& St = S();
	// A field that is no longer drawn (its screen closed without EndTextField) cannot keep the keyboard.
	if (St.bActive && St.OwnerKey != 0 && GFrameCounter > St.OwnerDrawFrame + 3)
	{
		EndEdit();
	}
	UpdateFocus();
}

void FVTextInput::DrawOverlay(FVDraw& D)
{
	using namespace FVTextInputImpl;
	FState& St = S();
	if (!St.bActive || !St.bKeyboardShown || St.OwnerKey == 0 || St.FieldBottom < FV::VH * 0.42f)
	{
		return;
	}
	const float H = FV::Ui::ButtonH();
	const float Y = FV::IsPortrait() ? FV::TopBarH + 8.f : 30.f;
	const FFVRect Strip(D.ScreenRect().X, Y - 8.f, D.ScreenRect().W, H + 16.f);
	D.Rect(Strip, FV::Col(26, 18, 14, 200));
	D.Rect(FFVRect(Strip.X, Strip.Bottom(), Strip.W, 1.f), FV::Col(0, 0, 0, 90));
	PaintField(D, FFVRect(12.f, Y, FV::VW - 24.f, H), St.Text, St.Placeholder, true, false, St.bCodeMode, St.MaxLen);
}

// ============================================================================================
// FVKit::TextField
// ============================================================================================
bool FVKit::TextField(FVDraw& D, const FFVRect& R, FFVTextField& Field, const FString& Placeholder)
{
	using namespace FVTextInputImpl;
	FState& St = S();
	if (Field.Key == 0)
	{
		Field.Key = FVKit::NewKey();
	}
	Field.MaxLen = FMath::Clamp(Field.MaxLen, 1, 256);
	bool bSubmitted = false;

	// Focus ended elsewhere: Enter / "Done" (-> submitted), Esc, the keyboard closed, another field, End().
	if (Field.bFocused && !(St.bActive && St.OwnerKey == Field.Key))
	{
		Field.bFocused = false;
		if (St.bSubmitPending && St.SubmitKey == Field.Key)
		{
			St.bSubmitPending = false;
			bSubmitted = true;
		}
	}
	if (Field.bFocused)
	{
		Field.Text = St.Text;
		St.OwnerDrawFrame = GFrameCounter;
		St.Placeholder = Placeholder;
		St.FieldBottom = R.Bottom() + float(D.GetTranslate().Y);
		if (D.IsInputBlocked())
		{
			EndTextField(Field);   // a dialog came up over the field
		}
	}

	const bool bHover = D.Hover(R) && !FV::IsTouchUI();
	if (D.Clicked(R))
	{
		if (!Field.bFocused)
		{
			FVTextInput::Begin(Field.Text, Field.MaxLen, Field.bLettersDigitsOnly);
			St.OwnerKey = Field.Key;
			St.OwnerDrawFrame = GFrameCounter;
			St.Placeholder = Placeholder;
			St.FieldBottom = R.Bottom() + float(D.GetTranslate().Y);
			Field.bFocused = true;
			Field.Text = St.Text;
			Sfx(EFVSfx::Click, 0.6f);
		}
		else if (St.bUseVirtualKeyboard)
		{
			ShowKeyboard();   // the player hid the keyboard with the system button: a tap brings it back
		}
	}
	else if (Field.bFocused && D.PointerReleased() && !D.IsDragGesture() && !D.IsInputBlocked() && !R.Contains(D.Mouse()))
	{
		EndTextField(Field);   // a tap elsewhere finishes the edit (the text stays)
	}

	PaintField(D, R, Field.Text, Placeholder, Field.bFocused, bHover, Field.bLettersDigitsOnly, Field.MaxLen);
	return bSubmitted;
}

void FVKit::EndTextField(FFVTextField& Field)
{
	using namespace FVTextInputImpl;
	FState& St = S();
	if (Field.bFocused && St.bActive && St.OwnerKey == Field.Key)
	{
		Field.Text = St.Text;
		EndEdit();
	}
	Field.bFocused = false;
}

void FVKit::FocusTextField(FFVTextField& Field)
{
	using namespace FVTextInputImpl;
	FState& St = S();
	if (Field.Key == 0)
	{
		Field.Key = FVKit::NewKey();
	}
	if (Field.bFocused && St.bActive && St.OwnerKey == Field.Key)
	{
		return;
	}
	Field.MaxLen = FMath::Clamp(Field.MaxLen, 1, 256);
	FVTextInput::Begin(Field.Text, Field.MaxLen, Field.bLettersDigitsOnly);
	St.OwnerKey = Field.Key;
	St.OwnerDrawFrame = GFrameCounter;
	Field.bFocused = true;
	Field.Text = St.Text;
}
