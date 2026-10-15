// Immediate-mode 2D renderer + Stardew-style UI widgets on top of UCanvas. Implemented in FVDraw.cpp.
//
// Usage (every frame, from UFVGame::Draw):
//   D.Begin(Canvas, Input, Time);  ...draw world, screens, widgets...  D.End();
// Widgets return true when clicked THIS frame (LMB went down over them). Everything is in virtual px (640x360).

#pragma once

#include "CoreMinimal.h"
#include "FVTypes.h"

class UCanvas;
class UFont;
class UTexture2D;
struct FFVSprite;

enum class EFVPanel : uint8
{
	Wood,       // dark-brown wooden frame with light parchment interior (main windows)
	Parchment,  // parchment box with thin brown border (sub-panels, cards)
	Dark,       // semi-transparent dark box (HUD strips, hints over the world)
	Tooltip     // light box with brown border, used by tooltips
};

enum class EFVButton : uint8 { Normal, Primary, Danger, Ghost };

/** Stardew-like UI palette (linear colors). */
namespace FVPal
{
	FORCEINLINE FLinearColor Ink()        { return FV::Col(64, 38, 26); }    // main text on parchment
	FORCEINLINE FLinearColor InkSoft()    { return FV::Col(120, 84, 60); }   // secondary text on parchment
	FORCEINLINE FLinearColor Parchment()  { return FV::Col(249, 218, 160); }
	FORCEINLINE FLinearColor ParchDark()  { return FV::Col(226, 176, 110); }
	FORCEINLINE FLinearColor WoodDark()   { return FV::Col(92, 44, 22); }
	FORCEINLINE FLinearColor Wood()       { return FV::Col(160, 84, 36); }
	FORCEINLINE FLinearColor WoodLight()  { return FV::Col(214, 128, 56); }
	FORCEINLINE FLinearColor White()      { return FV::Col(255, 255, 255); }
	FORCEINLINE FLinearColor Shadow()     { return FV::Col(0, 0, 0, 150); }
	FORCEINLINE FLinearColor Gold()       { return FV::Col(255, 204, 51); }
	FORCEINLINE FLinearColor Green()      { return FV::Col(106, 190, 48); }
	FORCEINLINE FLinearColor Red()        { return FV::Col(214, 52, 52); }
	FORCEINLINE FLinearColor Blue()       { return FV::Col(70, 140, 230); }
	FORCEINLINE FLinearColor Purple()     { return FV::Col(170, 90, 230); }
	FORCEINLINE FLinearColor Disabled()   { return FV::Col(150, 130, 110); }
}

class FVDraw
{
public:
	// ------------------------------------------------------------------ frame
	/** Computes Scale/Offset from the canvas size (uniform scale, centered, letterboxed with black bars). */
	void Begin(UCanvas* InCanvas, const FFVInput& InInput, double InTime);
	/** Draws deferred tooltip, clears per-frame state. Landscape: paints black letterbox bars over anything drawn
	    outside the virtual canvas. Portrait: no bars — the world/backgrounds bleed beyond the canvas (see ScreenRect). */
	void End();
	/** Optional callback run by End() right after the letterbox bars are painted (touch controls live in the bars). */
	TFunction<void(FVDraw&)> AfterLetterbox;

	UCanvas* Canvas = nullptr;
	float Scale = 1.f;                   // screen px per virtual px
	FVector2D Offset = FVector2D::ZeroVector; // screen position of virtual (0,0)
	double Time = 0.0;                   // seconds since game start (animations)

	const FFVInput& Input() const { return *InputPtr; }
	/** Pointer in the CURRENT local coordinates (i.e. minus the active PushTranslate offset). */
	FVector2D Mouse() const { return InputPtr->Mouse - Translate; }
	FVector2D ToScreen(const FVector2D& V) const { return Offset + (V + Translate) * Scale; }
	FVector2D ToVirtual(const FVector2D& S) const { return (S - Offset) / Scale - Translate; }

	// ------------------------------------------------------------------ primitives
	void Rect(const FFVRect& R, const FLinearColor& C);
	void Frame(const FFVRect& R, const FLinearColor& C, float Thickness = 1.f);
	void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& C, float Thickness = 1.f);
	/** Top-left placement; size = sprite size * ScaleMul. */
	void Sprite(const FFVSprite& S, const FVector2D& Pos, float ScaleMul = 1.f, const FLinearColor& Tint = FLinearColor::White, bool bFlipX = false);
	void SpriteCentered(const FFVSprite& S, const FVector2D& Center, float ScaleMul = 1.f, const FLinearColor& Tint = FLinearColor::White, bool bFlipX = false, float RotationDeg = 0.f);
	void SpriteRect(const FFVSprite& S, const FFVRect& R, const FLinearColor& Tint = FLinearColor::White, bool bFlipX = false);
	/** Additive blend (glows, lights). */
	void SpriteAdditive(const FFVSprite& S, const FVector2D& Center, float ScaleMul, const FLinearColor& Tint);
	void Texture(UTexture2D* Tex, const FFVRect& R, const FVector2D& UV0 = FVector2D(0, 0), const FVector2D& UV1 = FVector2D(1, 1), const FLinearColor& Tint = FLinearColor::White);
	/** Soft round glow (procedural radial texture), additive. Radius in virtual px. */
	void Glow(const FVector2D& Center, float Radius, const FLinearColor& C);
	/** Filled circle (procedural texture). */
	void Circle(const FVector2D& Center, float Radius, const FLinearColor& C);
	/** Darkens the whole viewport (including letterbox). */
	void Dim(float Alpha = 0.55f);

	// ------------------------------------------------------------------ text (Roboto, Cyrillic OK)
	/** Size: virtual font size (8 = body text, 6 = small, 11 = header, 16+ = titles). Pos = top-left/top-center/top-right per Align. */
	void Text(const FString& S, const FVector2D& Pos, float Size = 8.f, const FLinearColor& C = FVPal::Ink(), EFVAlign Align = EFVAlign::Left, bool bShadow = false, bool bBold = true);
	/** Text with 1px dark outline — readable over the world. */
	void TextOutlined(const FString& S, const FVector2D& Pos, float Size = 8.f, const FLinearColor& C = FVPal::White(), EFVAlign Align = EFVAlign::Left, const FLinearColor& OutlineC = FV::Col(30, 18, 12));
	FVector2D MeasureText(const FString& S, float Size = 8.f, bool bBold = true) const;
	/** Word-wrapped text; returns height used (virtual px). '\n' forces a line break. */
	float TextWrapped(const FString& S, const FVector2D& Pos, float MaxWidth, float Size = 8.f, const FLinearColor& C = FVPal::Ink(), bool bBold = false, float LineSpacing = 1.1f);

	// ------------------------------------------------------------------ widgets
	void Panel(const FFVRect& R, EFVPanel Style = EFVPanel::Wood);
	/** Standard window: Wood panel + title plate + close [X] button (top-right). Returns true if close clicked. */
	bool Window(const FFVRect& R, const FString& Title);
	bool Button(const FFVRect& R, const FString& Label, EFVButton Style = EFVButton::Normal, bool bEnabled = true, float TextSize = 8.f);
	bool IconButton(const FFVRect& R, const FFVSprite& Icon, bool bSelected = false, bool bEnabled = true, const FString& TooltipText = FString());
	/** The red close [X] of windows on its own (e.g. «give up» of the practice minigame). Returns true when clicked. */
	bool CloseButton(const FFVRect& R);
	/** Folder-tab button (drawn above a panel's top edge). */
	bool Tab(const FFVRect& R, const FString& Label, bool bActive);
	/** Item slot frame (caller draws content inside). Returns click. */
	bool Slot(const FFVRect& R, bool bSelected = false, bool bEnabled = true);
	/** Hover test (false while input is blocked). Marks the UI as hovered. */
	bool Hover(const FFVRect& R);
	/** Update 3: a TAP on R — fires on LMB/finger RELEASE over R when the press also started over R and the pointer
	    did not travel more than TapSlop() (drags/swipes never click). Consumes the click (first widget drawn wins). */
	bool Clicked(const FFVRect& R);
	/** Press semantics (LMB/finger went down over R this frame): for widgets that start a drag (sliders) or need an
	    instant response (mini-games). Consumes the press. */
	bool PressedIn(const FFVRect& R);
	/** Progress bar with optional frame. */
	void Bar(const FFVRect& R, float Frac, const FLinearColor& Fill, const FLinearColor& Back = FV::Col(60, 40, 30), bool bFramed = true);
	/** Quality stars (nothing for Normal). Pos = top-left, 8x8 icon. */
	void Stars(const FVector2D& Pos, EFVQuality Q, float ScaleMul = 1.f);
	/** Coin icon + amount. Returns width used. */
	float MoneyLabel(const FVector2D& Pos, int64 Amount, float Size = 8.f, EFVAlign Align = EFVAlign::Left, const FLinearColor& C = FVPal::Ink());
	/** Deferred tooltip near the mouse, drawn in End(). Last call in a frame wins. */
	void Tooltip(const FString& Title, const FString& Body = FString());

	// ------------------------------------------------------------------ input routing
	/** While blocked, widgets neither hover nor click (used under modal dialogs). */
	void SetInputBlocked(bool bBlocked) { bInputBlocked = bBlocked; }
	bool IsInputBlocked() const { return bInputBlocked; }
	/** True if any widget was hovered/clicked during this frame. */
	bool WasUIHovered() const { return bUIHovered; }
	/** Marks an arbitrary area as UI (e.g., HUD strips) so LMB there doesn't count as a world action. */
	void MarkUI(const FFVRect& R);

	// ------------------------------------------------------------------ extra widgets & helpers (additive)
	/** Tooltip with a colored title (e.g., rarity color). Last call in a frame wins. */
	void Tooltip(const FString& Title, const FString& Body, const FLinearColor& TitleColor);
	/** Horizontal slider 0..1: click or drag with LMB anywhere on the track. Returns true when the value changed this frame. */
	bool Slider(const FFVRect& R, float& Value01);
	/** Checkbox: square box at the left of R (R.H tall, max 12), label to the right. Whole R is clickable.
	    Returns true when toggled this frame (bValue is flipped). */
	bool Checkbox(const FFVRect& R, bool& bValue, const FString& Label = FString(), float TextSize = 8.f);
	/** True while a Slider is being dragged with the LMB. */
	bool IsDragging() const { return ActiveDragKey != 0; }
	/** Height of one text line (font line box) in virtual px. */
	float LineHeight(float Size = 8.f, bool bBold = true) const;
	/** Height TextWrapped() would use for the same arguments, without drawing. */
	float MeasureWrapped(const FString& S, float MaxWidth, float Size = 8.f, bool bBold = false, float LineSpacing = 1.1f) const;
	/** Shortens S with "…" so that it fits into MaxWidth. */
	FString Ellipsize(const FString& S, float MaxWidth, float Size = 8.f, bool bBold = true) const;
	/** Engraved 2px divider line for parchment panels. */
	void Divider(const FVector2D& Pos, float Width);
	/** Pixel ellipse outline (ripples, rings, selection circles). */
	void Ring(const FVector2D& Center, float RadiusX, float RadiusY, const FLinearColor& C, float Thickness = 1.f);
	/** Vertical gradient made of horizontal bands (banded pixel-art look). */
	void GradientV(const FFVRect& R, const FLinearColor& Top, const FLinearColor& Bottom, int32 Bands = 16);

	// ------------------------------------------------------------------ portrait (phone) window layout (additive)
	// Standard portrait overlay window (thumb-friendly):
	//     const FFVRect Win = FV::WindowRect();
	//     if (D.Window(Win, TEXT("Лавка рыбака"))) { close; return; }     // title plate + close X (top-right)
	//     tabs right under the title (Win.Y + 16 ...), content in the middle, ending above ActionBarRect(Win).Y - 10
	//     FFVRect Actions;
	//     if (D.ActionBar(Win, &Actions)) { close; return; }               // divider + "← Назад" at the bottom-left
	//     if (D.Button(FVDraw::SplitH(Actions, 0, 1), TEXT("Купить"), EFVButton::Primary, bCanBuy, FV::Ui::Body() + 1.f)) { ... }
	// Everything is sized with FV::Ui (buttons FV::Ui::ButtonH() tall).

	/** The bottom action-bar row (one row of FV::Ui::ButtonH() buttons) inside window WindowR. Content above it should
	    end at ActionBarRect(WindowR).Y - 10 (ActionBar draws a divider there). */
	static FFVRect ActionBarRect(const FFVRect& WindowR);
	/** Draws the standard bottom action bar of WindowR: a divider and the "← Назад" button at the bottom-left.
	    OutActions (optional) = the free area to the right of "Назад" for the primary action button(s) — split it with
	    SplitH. Returns true when "Назад" was clicked (the caller closes the window / goes back a page). */
	bool ActionBar(const FFVRect& WindowR, FFVRect* OutActions = nullptr, const FString& BackLabel = TEXT("Назад"));
	/** "← Назад" button: Normal style with a pixel arrow before the label. TextSize 0 = FV::Ui::Body() + 1. Returns click. */
	bool BackButton(const FFVRect& R, const FString& Label = TEXT("Назад"), float TextSize = 0.f);
	/** Index-th of Count equal cells of Area, laid out left to right with Gap between them (pixel-rounded). */
	static FFVRect SplitH(const FFVRect& Area, int32 Index, int32 Count, float Gap = 6.f);
	/** The whole viewport in virtual px. Equals (0, 0, VW, VH) unless the screen is letterboxed; in portrait nothing
	    paints the bars, so backgrounds should cover this rect ("bleed") instead of just 0..VW x 0..VH. */
	FFVRect ScreenRect() const;

	// ------------------------------------------------------------------ update 3: clipping, translation, gestures
	// All widgets stay immediate-mode; these make smooth scrolling and sliding pages possible (see Private/FVUIKit.h).
	/** Everything drawn until PopClip is clipped to R (current local coords); nests as an intersection. Hover/Clicked
	    are also limited to the clip. Implemented with the canvas scissor (a flush per push/pop — use per list, not per row). */
	void PushClip(const FFVRect& R);
	void PopClip();
	/** Moves all drawing AND hit-testing by Delta (virtual px) until PopTranslate; nests (adds up). */
	void PushTranslate(const FVector2D& Delta);
	void PopTranslate();
	FVector2D GetTranslate() const { return Translate; }
	/** Is P (current local coords) inside the active clip (true when there is none)? */
	bool InClip(const FVector2D& P) const;

	/** Pointer gesture (mouse button or the pointer finger), tracked across frames by Begin().
	    Widget clicks fire on RELEASE, only when the pointer did not travel more than TapSlop() since the press
	    (so swiping / scrolling never triggers buttons). */
	enum class EDragAxis : uint8 { None, Horizontal, Vertical };
	bool IsPointerDown() const { return bPtrDown; }
	/** Press position in current local coords. */
	FVector2D PressPos() const { return PressPosV - Translate; }
	/** Mouse - PressPos while the pointer is down (zero otherwise). */
	FVector2D DragDelta() const { return bPtrDown ? (InputPtr->Mouse - PressPosV) : FVector2D::ZeroVector; }
	/** True once the current (or just released) gesture moved more than TapSlop(): it is a drag, not a tap.
	    Update 6 (§12.2): also true after CancelTap() (the press will not click, no "pressed" look), but the drag AXIS is
	    still decided as usual, so a list stopped by a tap can be dragged by the same finger. */
	bool IsDragGesture() const { return bDragGesture || bTapCancelled; }
	/** The gesture really moved more than TapSlop() (CancelTap does not count). */
	bool IsMovedGesture() const { return bDragGesture; }
	/** CancelTap() was called for the current (or just released) press. */
	bool IsTapCancelled() const { return bTapCancelled; }
	/** Axis decided when the gesture first exceeded the slop (locked until release). */
	EDragAxis DragAxis() const { return GestureAxis; }
	/** Smoothed pointer velocity, virtual px per second (for flings). */
	FVector2D PointerVelocity() const { return PtrVel; }
	/** The first claimant owns the current drag (pager / scroll view); returns true if Key is (now) the owner. */
	bool ClaimDrag(uint32 Key);
	/** The current press will not click any widget on release (e.g. a tap that stops a fling). Update 6 (§12.2): it no
	    longer marks the gesture as a drag, so the drag axis is still assigned when the finger moves on (repeat swipes). */
	void CancelTap() { bTapCancelled = true; }
	uint32 DragOwner() const { return GestureOwner; }
	/** Pointer went up this frame (the frame widget clicks fire). */
	bool PointerReleased() const { return InputPtr && InputPtr->bLmbReleased; }
	static float TapSlop() { return FV::IsTouchUI() ? 9.f : 5.f; }
	/** Seconds since the previous Begin (clamped 0..0.1) — for animations inside Draw. */
	float FrameDt() const { return DrawDt; }

private:
	FFVInput EmptyInput;
	const FFVInput* InputPtr = &EmptyInput;
	bool bInputBlocked = false;
	bool bUIHovered = false;
	bool bClickConsumed = false;
	FString TipTitle, TipBody;
	bool bHasTip = false;
	UTexture2D* CircleTex = nullptr;     // procedural; owned by UFVSprites
	UTexture2D* GlowTex = nullptr;
	UFont* Font = nullptr;               // /Engine/EngineFonts/Roboto (rooted)

	void EnsureResources();

	// ---- additive private state ----
	FLinearColor TipColor = FLinearColor::White;
	uint32 ActiveDragKey = 0;            // slider being dragged (0 = none)
	uint32 HoverSfxKey = 0;              // interactive widget hovered this frame (hover sfx plays on change)
	uint32 PrevHoverSfxKey = 0;

	// ---- update 3 private state
	FVector2D Translate = FVector2D::ZeroVector;
	TArray<FVector2D> TranslateStack;
	struct FClip { FFVRect V; FIntRect Px; };   // V = clip in untranslated virtual coords
	TArray<FClip> ClipStack;
	bool bPtrDown = false;
	bool bDragGesture = false;
	bool bTapCancelled = false;          // update 6 (§12.2): CancelTap() of the current press (separate from the drag)
	EDragAxis GestureAxis = EDragAxis::None;
	uint32 GestureOwner = 0;
	FVector2D PressPosV = FVector2D::ZeroVector;
	FVector2D LastPtrPos = FVector2D::ZeroVector;
	FVector2D PtrVel = FVector2D::ZeroVector;
	double LastBeginTime = -1.0;
	float DrawDt = 0.f;
	void ApplyScissor(const FIntRect& Px);
	void UpdateGesture();

	/** Screen-space tile (screen px). Tex == nullptr -> white. BlendMode = ESimpleElementBlendMode. */
	void TileScreen(float SX, float SY, float SW, float SH, const FLinearColor& C, UTexture2D* Tex = nullptr,
		const FVector2D& UV0 = FVector2D(0, 0), const FVector2D& UV1 = FVector2D(1, 1), int32 BlendMode = 2, float RotationDeg = 0.f, bool bSnap = true);
	/** Draws a single text run at a virtual top-left position. */
	void DrawTextRaw(const FString& S, const FVector2D& VirtualPos, float Size, const FLinearColor& C, bool bBold, bool bShadow, const FLinearColor& ShadowC, float ShadowOffsetPx);
	void DrawTooltipNow();
	void NoteHover(uint32 Key) { HoverSfxKey = Key; }
};
