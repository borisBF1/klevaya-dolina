// Thin Unreal framework layer: GameMode (no pawn), PlayerController (input snapshot + game owner), HUD (canvas).

#include "FVFramework.h"
#include "FVGame.h"
#include "FVTextInput.h"

#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/ICursor.h"
#include "Widgets/SWindow.h"

// ============================================================================================
// Game mode
// ============================================================================================
AFVGameMode::AFVGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AFVPlayerController::StaticClass();
	HUDClass = AFVHUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

// ============================================================================================
// Player controller
// ============================================================================================
namespace FVFrameworkLocal
{
	/** 11x16 pixel-art pointer: O = outline, H = highlight, W = cream fill, S = warm shade. */
	const TCHAR* const GCursorArt[] =
	{
		TEXT("O          "),
		TEXT("OO         "),
		TEXT("OHO        "),
		TEXT("OHWO       "),
		TEXT("OHWWO      "),
		TEXT("OHWWWO     "),
		TEXT("OHWWWWO    "),
		TEXT("OHWWWWSO   "),
		TEXT("OHWWWWWSO  "),
		TEXT("OHWWWWWWSO "),
		TEXT("OHWWWSOOOOO"),
		TEXT("OHWOOWSO   "),
		TEXT("OHO OWSO   "),
		TEXT("OO   OWSO  "),
		TEXT("O    OWSO  "),
		TEXT("      OO   "),
	};

	constexpr int32 GCursorScale = 2;   // one art texel = 2x2 screen px (matches the 1280x720 UI scale)
	constexpr int32 GCursorSize = 32;   // bitmap is 32x32

	/** The set of keys whose "just pressed" state is reported in FFVInput::Pressed. */
	const TArray<FKey>& PolledKeys()
	{
		static TArray<FKey> Keys;
		if (Keys.Num() == 0)
		{
			Keys = {
				EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I,
				EKeys::J, EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R,
				EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z,
				EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
				EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine,
				EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
				EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine,
				EKeys::Escape, EKeys::Tab, EKeys::Enter, EKeys::SpaceBar, EKeys::BackSpace, EKeys::Delete,
				EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right,
				EKeys::PageUp, EKeys::PageDown, EKeys::Home, EKeys::End,
				EKeys::Add, EKeys::Subtract, EKeys::Hyphen, EKeys::Equals,
				EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6,
				EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10, EKeys::F11, EKeys::F12,
				EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Right,
				EKeys::Gamepad_FaceButton_Left,
			};
		}
		return Keys;
	}
}

AFVPlayerController::AFVPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableTouchEvents = false;
	bEnableMouseOverEvents = false;
	DefaultMouseCursor = EMouseCursor::Default;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AFVPlayerController::SetupCursor()
{
	bCustomCursor = false;
	if (FV::IsTouchUI() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	TSharedPtr<GenericApplication> App = FSlateApplication::Get().GetPlatformApplication();
	if (!App.IsValid() || !App->Cursor.IsValid() || !App->Cursor->IsCreateCursorFromRGBABufferSupported())
	{
		return;
	}

	const FColor Outline(58, 30, 20, 255);
	const FColor Highlight(255, 255, 255, 255);
	const FColor Fill(255, 238, 200, 255);
	const FColor Shade(236, 150, 70, 255);
	const FColor ShadowC(0, 0, 0, 80);

	TArray<FColor> Px;
	Px.Init(FColor(0, 0, 0, 0), FVFrameworkLocal::GCursorSize * FVFrameworkLocal::GCursorSize);

	auto Put = [&Px](int32 X, int32 Y, const FColor& C)
	{
		for (int32 dy = 0; dy < FVFrameworkLocal::GCursorScale; ++dy)
		{
			for (int32 dx = 0; dx < FVFrameworkLocal::GCursorScale; ++dx)
			{
				const int32 PX = X * FVFrameworkLocal::GCursorScale + dx;
				const int32 PY = Y * FVFrameworkLocal::GCursorScale + dy;
				if (PX >= 0 && PY >= 0 && PX < FVFrameworkLocal::GCursorSize && PY < FVFrameworkLocal::GCursorSize)
				{
					Px[PY * FVFrameworkLocal::GCursorSize + PX] = C;
				}
			}
		}
	};

	const int32 Rows = UE_ARRAY_COUNT(FVFrameworkLocal::GCursorArt);
	// Soft drop shadow first (one texel down-right), then the art on top.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Y = 0; Y < Rows; ++Y)
		{
			const FString Row(FVFrameworkLocal::GCursorArt[Y]);
			for (int32 X = 0; X < Row.Len(); ++X)
			{
				const TCHAR Ch = Row[X];
				if (Ch == TEXT(' ') || Ch == TEXT('.'))
				{
					continue;
				}
				if (Pass == 0)
				{
					Put(X + 1, Y + 1, ShadowC);
					continue;
				}
				const FColor C = Ch == TEXT('O') ? Outline : Ch == TEXT('H') ? Highlight : Ch == TEXT('S') ? Shade : Fill;
				Put(X, Y, C);
			}
		}
	}

	void* Handle = App->Cursor->CreateCursorFromRGBABuffer(Px.GetData(), FVFrameworkLocal::GCursorSize, FVFrameworkLocal::GCursorSize, FVector2D(0.5f / FVFrameworkLocal::GCursorSize, 0.5f / FVFrameworkLocal::GCursorSize));
	if (!Handle)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: custom cursor could not be created, using the system arrow"));
		return;
	}
	App->Cursor->SetTypeShape(EMouseCursor::Custom, Handle);
	DefaultMouseCursor = EMouseCursor::Custom;
	CurrentMouseCursor = EMouseCursor::Custom;
	bCustomCursor = true;
}

void AFVPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);

	SetupCursor();
	// The game draws its own touch controls; never show the engine's default virtual joysticks.
	ActivateTouchInterface(nullptr);

	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* VC = World->GetGameViewport())
		{
			// Everything is drawn on the HUD canvas; the (empty) 3D world does not need to be rendered.
			VC->bDisableWorldRendering = true;
			if (TSharedPtr<SWindow> Window = VC->GetWindow())
			{
				Window->SetTitle(FText::FromString(TEXT("Клёвая Долина")));
			}
		}
	}

	{ void FVIconExportMaybeStart(); FVIconExportMaybeStart(); }   // -fvexportart=<dir>: art export for the site (FVIconExport.cpp)
	Game = NewObject<UFVGame>(this, TEXT("FVGame"));
	if (Game)
	{
		Game->Init(this);
	}
}

void AFVPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Game)
	{
		Game->Shutdown();
		Game = nullptr;
	}

	if (bCustomCursor && FSlateApplication::IsInitialized())
	{
		TSharedPtr<GenericApplication> App = FSlateApplication::Get().GetPlatformApplication();
		if (App.IsValid() && App->Cursor.IsValid())
		{
			App->Cursor->SetTypeShape(EMouseCursor::Custom, nullptr);
		}
		bCustomCursor = false;
	}

	Super::EndPlay(Reason);
}

FFVInput AFVPlayerController::BuildInput()
{
	FFVInput In;

	// ---- mouse (viewport px -> virtual px)
	float MX = 0.f, MY = 0.f;
	if (GetMousePosition(MX, MY))
	{
		const float S = ViewScale > KINDA_SMALL_NUMBER ? ViewScale : 1.f;
		In.Mouse = (FVector2D(MX, MY) - ViewOffset) / S;
		In.bMouseValid = true;
	}

	In.bLmbDown = IsInputKeyDown(EKeys::LeftMouseButton);
	In.bLmbPressed = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	In.bLmbReleased = WasInputKeyJustReleased(EKeys::LeftMouseButton);
	In.bRmbPressed = WasInputKeyJustPressed(EKeys::RightMouseButton);

	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		In.Wheel += 1;
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		In.Wheel -= 1;
	}

	// Update 4: while a text field is focused the keyboard types (FVTextInput): no movement / action / hotkeys from
	// keyboard keys (keys already held when the field got focus included). Mouse and touch keep working.
	const bool bTyping = FVTextInput::IsActive();

	// ---- movement (WASD, arrows, d-pad, left stick)
	float MoveX = 0.f, MoveY = 0.f;
	if (!bTyping)
	{
		if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left) || IsInputKeyDown(EKeys::Gamepad_DPad_Left)) { MoveX -= 1.f; }
		if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right) || IsInputKeyDown(EKeys::Gamepad_DPad_Right)) { MoveX += 1.f; }
		if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up) || IsInputKeyDown(EKeys::Gamepad_DPad_Up)) { MoveY -= 1.f; }
		if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down) || IsInputKeyDown(EKeys::Gamepad_DPad_Down)) { MoveY += 1.f; }
	}
	const float StickX = GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	const float StickY = GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	if (FMath::Abs(StickX) > 0.25f) { MoveX += StickX; }
	if (FMath::Abs(StickY) > 0.25f) { MoveY -= StickY; }  // stick up is +Y, screen up is -Y
	In.MoveX = FMath::Clamp(MoveX, -1.f, 1.f);
	In.MoveY = FMath::Clamp(MoveY, -1.f, 1.f);

	In.bShiftDown = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);

	// ---- raw action = Space (or the gamepad A button); UFVGame adds LMB when it is not over the UI.
	if (!bTyping)
	{
		In.bActionDown = IsInputKeyDown(EKeys::SpaceBar) || IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom);
		In.bActionPressed = WasInputKeyJustPressed(EKeys::SpaceBar) || WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
		In.bActionReleased = WasInputKeyJustReleased(EKeys::SpaceBar) || WasInputKeyJustReleased(EKeys::Gamepad_FaceButton_Bottom);
	}

	// ---- keys that went down this frame (while typing only F11 = fullscreen)
	for (const FKey& K : FVFrameworkLocal::PolledKeys())
	{
		if (WasInputKeyJustPressed(K) && (!bTyping || K == EKeys::F11))
		{
			In.Pressed.Add(K);
		}
	}
	// ---- interact with the nearest world object in reach: E, right mouse button or the gamepad X button
	In.bInteractPressed = In.WasPressed(EKeys::E) || In.bRmbPressed || In.WasPressed(EKeys::Gamepad_FaceButton_Left);

	// ---- fingers (touch UI): FVTouchControls turns them into movement / action / pointer input
	if (FV::IsTouchUI())
	{
		const float S = ViewScale > KINDA_SMALL_NUMBER ? ViewScale : 1.f;
#if PLATFORM_ANDROID || PLATFORM_IOS
		for (int32 i = 0; i < 10; ++i)
		{
			float TX = 0.f, TY = 0.f;
			bool bPressed = false;
			GetInputTouchState(ETouchIndex::Type(ETouchIndex::Touch1 + i), TX, TY, bPressed);
			// a very short tap (down and up between two frames) is already released when polled: its Began event is still
			// counted this frame, and the last position stays in the touch state — report it for one frame (fast tapping)
			if (!bPressed && i < int32(EKeys::NUM_TOUCH_KEYS) && WasInputKeyJustPressed(EKeys::TouchKeys[i])) { bPressed = true; }
			if (bPressed)
			{
				FFVTouchPoint P;
				P.Id = i;
				P.Pos = (FVector2D(TX, TY) - ViewOffset) / S;
				In.Touches.Add(P);
			}
		}
#else
		// Desktop test mode (-fvtouch): the mouse is finger 0.
		if (In.bMouseValid && In.bLmbDown)
		{
			FFVTouchPoint P;
			P.Id = 0;
			P.Pos = In.Mouse;
			In.Touches.Add(P);
		}
#endif
	}
	// Android "Back" behaves like Escape (while typing it only closes the keyboard: FVTextInput).
	if (!bTyping && WasInputKeyJustPressed(EKeys::Android_Back) && !In.WasPressed(EKeys::Escape))
	{
		In.Pressed.Add(EKeys::Escape);
	}

	// Gamepad conveniences: B / Start behave like Escape.
	if ((In.WasPressed(EKeys::Gamepad_FaceButton_Right) || In.WasPressed(EKeys::Gamepad_Special_Right)) && !In.WasPressed(EKeys::Escape))
	{
		In.Pressed.Add(EKeys::Escape);
	}

	return In;
}

void AFVPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!Game || !IsLocalController())
	{
		return;
	}
	const float Dt = FMath::Clamp(DeltaTime, 0.f, 0.1f);
	const FFVInput In = BuildInput();
	Game->Tick(Dt, In);
}

// ============================================================================================
// HUD
// ============================================================================================
void AFVHUD::DrawHUD()
{
	Super::DrawHUD();

	AFVPlayerController* PC = Cast<AFVPlayerController>(GetOwningPlayerController());
	if (!PC || !Canvas)
	{
		return;
	}
	if (UFVGame* G = PC->GetGame())
	{
		G->Draw(Canvas);
		PC->ViewScale = G->GetViewScale();
		PC->ViewOffset = G->GetViewOffset();
	}
}
