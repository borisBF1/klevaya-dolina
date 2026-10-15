// Text entry for canvas UIs (update 4): names, status, search, restore codes.
// Desktop: typed characters (Cyrillic too) are read by an invisible focusable widget on the game viewport that holds the
// keyboard focus while a field is edited (in UE 5.8 IInputProcessor has no character hook); a Slate input pre-processor
// handles Enter / Esc / BackSpace / Ctrl+V and blocks hotkeys (UFVGame must not treat letters as shortcuts while
// FVTextInput::IsActive()). Android / touch: the native virtual keyboard through Slate's IVirtualKeyboardEntry.
// Implemented in FVTextInput.cpp (Core module).

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class FVDraw;

namespace FVTextInput
{
	/** Registers the input pre-processor (call once from UFVGame::Init) / unregisters it (Shutdown). */
	void Startup();
	void Shutdown();

	/** Starts editing Initial (focus): shows the virtual keyboard on phones, captures typed characters on desktop. */
	void Begin(const FString& Initial, int32 MaxLen, bool bDigitsAndLettersOnly = false);
	/** Stops editing and hides the keyboard. */
	void End();
	bool IsActive();
	/** Current text of the focused field. */
	const FString& GetText();
	void SetText(const FString& Text);
	/** True once after Enter / the keyboard's "Done" was pressed. */
	bool ConsumeSubmit();
	/** Called by UFVGame every frame (keyboard timeouts, cursor blink, backspace repeat). */
	void Tick(float Dt);
	/** Phones: a preview strip of the typed text when the OS keyboard covers the field (UFVGame::Draw, last). */
	void DrawOverlay(FVDraw& D);
}

namespace FVKit
{
	/** State of one text field (keep it as a screen member). */
	struct FFVTextField
	{
		FString Text;
		int32 MaxLen = 16;
		bool bFocused = false;
		bool bLettersDigitsOnly = false;   // restore codes
		uint32 Key = 0;                    // assigned on first use
	};

	/** Tap to focus (shows the keyboard), tap outside / Enter to finish. Draws a parchment box with the text, a blinking
	    caret and the placeholder when empty. Returns true on the frame the text was submitted (Enter / "Done"). While
	    focused, Field.Text follows the typing. */
	bool TextField(FVDraw& D, const FFVRect& R, FFVTextField& Field, const FString& Placeholder);
	/** Unfocus programmatically (e.g. when the screen closes). */
	void EndTextField(FFVTextField& Field);
	/** Focus programmatically (e.g. the first-run name field): starts editing Field.Text (shows the keyboard on phones). */
	void FocusTextField(FFVTextField& Field);
}
