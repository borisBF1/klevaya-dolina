// Immediate-mode 2D renderer + Stardew-style widgets on top of UCanvas.

#include "FVDraw.h"
#include "FVSprites.h"
#include "FVAudio.h"

#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "TextureResource.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/UObjectGlobals.h"

namespace FVDrawImpl
{
	static UFont* GFont = nullptr;
	static bool bFontLoadFailed = false;
	static TWeakObjectPtr<UTexture2D> GCircleTex;
	static TWeakObjectPtr<UTexture2D> GGlowTex;

	static UFont* GetFont()
	{
		if (!GFont && !bFontLoadFailed)
		{
			GFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
			if (GFont)
			{
				if (!GFont->IsRooted()) { GFont->AddToRoot(); }
			}
			else
			{
				bFontLoadFailed = true;
				UE_LOG(LogFV, Error, TEXT("FV: FVDraw could not load /Engine/EngineFonts/Roboto — text will not render"));
			}
		}
		return GFont;
	}

	static TSharedPtr<FSlateFontMeasure> GetMeasure()
	{
		if (FEngineFontServices::IsInitialized())
		{
			TSharedPtr<FSlateFontMeasure> M = FEngineFontServices::Get().GetFontMeasure();
			if (M.IsValid()) { return M; }
		}
		if (FSlateApplication::IsInitialized())
		{
			if (FSlateRenderer* R = FSlateApplication::Get().GetRenderer())
			{
				return R->GetFontMeasureService();
			}
		}
		return nullptr;
	}

	static FSlateFontInfo MakeFont(UFont* F, float PxSize, bool bBold)
	{
		static const FName BoldName(TEXT("Bold"));
		static const FName RegularName(TEXT("Regular"));
		return FSlateFontInfo(F, FMath::Max(1.f, PxSize), bBold ? BoldName : RegularName);
	}

	static FORCEINLINE FLinearColor Lighten(const FLinearColor& C, float T)
	{
		FLinearColor R = C + (FLinearColor(1.f, 1.f, 1.f, C.A) - C) * FMath::Clamp(T, 0.f, 1.f);
		R.A = C.A;
		return R;
	}
	static FORCEINLINE FLinearColor Darken(const FLinearColor& C, float T)
	{
		const float K = 1.f - FMath::Clamp(T, 0.f, 1.f);
		return FLinearColor(C.R * K, C.G * K, C.B * K, C.A);
	}
	static FORCEINLINE uint32 RectKey(const FFVRect& R, uint32 Salt)
	{
		uint32 H = GetTypeHash(FMath::RoundToInt(R.X * 2.f));
		H = HashCombine(H, GetTypeHash(FMath::RoundToInt(R.Y * 2.f)));
		H = HashCombine(H, GetTypeHash(FMath::RoundToInt(R.W)));
		H = HashCombine(H, GetTypeHash(FMath::RoundToInt(R.H)));
		H = HashCombine(H, Salt);
		return H == 0 ? 1u : H;
	}
	static void PlaySfx(EFVSfx S, float Volume = 1.f, float Pitch = 1.f)
	{
		if (UFVAudio* A = UFVAudio::Get()) { A->Play(S, Volume, Pitch); }
	}

	/** Splits text into lines that fit MaxWidth (word wrap, '\n' = hard break, very long words are split by characters). */
	static void WrapLines(const FVDraw& D, const FString& S, float MaxWidth, float Size, bool bBold, TArray<FString>& Out)
	{
		Out.Reset();
		if (S.IsEmpty()) { return; }
		TArray<FString> Paragraphs;
		S.ParseIntoArray(Paragraphs, TEXT("\n"), false);
		const float MaxW = FMath::Max(8.f, MaxWidth);
		for (const FString& Para : Paragraphs)
		{
			TArray<FString> Words;
			Para.ParseIntoArray(Words, TEXT(" "), true);
			if (Words.Num() == 0) { Out.Add(FString()); continue; }
			FString Line;
			for (const FString& Word : Words)
			{
				const FString Candidate = Line.IsEmpty() ? Word : (Line + TEXT(" ") + Word);
				if (D.MeasureText(Candidate, Size, bBold).X <= MaxW)
				{
					Line = Candidate;
					continue;
				}
				if (!Line.IsEmpty())
				{
					Out.Add(Line);
					Line.Reset();
				}
				// The word alone may still be too wide: split it by characters.
				if (D.MeasureText(Word, Size, bBold).X <= MaxW)
				{
					Line = Word;
				}
				else
				{
					FString Part;
					for (int32 i = 0; i < Word.Len(); ++i)
					{
						const FString Next = Part + Word.Mid(i, 1);
						if (!Part.IsEmpty() && D.MeasureText(Next, Size, bBold).X > MaxW)
						{
							Out.Add(Part);
							Part = Word.Mid(i, 1);
						}
						else
						{
							Part = Next;
						}
					}
					Line = Part;
				}
			}
			Out.Add(Line);
		}
	}

	static float LineStep(float Size, float LineSpacing)
	{
		return Size * 1.4f * FMath::Max(0.5f, LineSpacing);
	}

	struct FButtonLook
	{
		FLinearColor Outline, Face, Light, Dark, Text, TextOutline;
		bool bOutlinedText = false;
	};

	static FButtonLook LookFor(EFVButton Style, bool bEnabled)
	{
		FButtonLook L;
		L.Outline = FVPal::WoodDark();
		if (!bEnabled)
		{
			L.Face = FV::Col(196, 178, 158);
			L.Light = FV::Col(216, 202, 186);
			L.Dark = FV::Col(150, 132, 114);
			L.Text = FV::Col(128, 112, 98);
			L.Outline = FV::Col(120, 96, 80);
			return L;
		}
		switch (Style)
		{
		case EFVButton::Primary:
			L.Face = FV::Col(104, 178, 60);
			L.Light = FV::Col(160, 220, 96);
			L.Dark = FV::Col(58, 118, 38);
			L.Outline = FV::Col(34, 70, 26);
			L.Text = FV::Col(255, 255, 240);
			L.TextOutline = FV::Col(32, 72, 24);
			L.bOutlinedText = true;
			break;
		case EFVButton::Danger:
			L.Face = FV::Col(204, 72, 56);
			L.Light = FV::Col(240, 128, 104);
			L.Dark = FV::Col(140, 40, 32);
			L.Outline = FV::Col(84, 22, 18);
			L.Text = FV::Col(255, 244, 236);
			L.TextOutline = FV::Col(90, 20, 16);
			L.bOutlinedText = true;
			break;
		case EFVButton::Ghost:
			L.Face = FV::Col(0, 0, 0, 0);
			L.Light = FV::Col(0, 0, 0, 0);
			L.Dark = FV::Col(0, 0, 0, 0);
			L.Outline = FV::Col(0, 0, 0, 0);
			L.Text = FVPal::Ink();
			break;
		case EFVButton::Normal:
		default:
			L.Face = FV::Col(240, 190, 122);
			L.Light = FV::Col(255, 228, 170);
			L.Dark = FV::Col(184, 118, 60);
			L.Text = FVPal::Ink();
			break;
		}
		return L;
	}
}

// ============================================================================================
// Frame
// ============================================================================================
void FVDraw::Begin(UCanvas* InCanvas, const FFVInput& InInput, double InTime)
{
	Canvas = InCanvas;
	// Keep a private copy of the frame input: the caller's object may be a temporary.
	if (&InInput != &EmptyInput)
	{
		EmptyInput = InInput;
	}
	InputPtr = &EmptyInput;
	DrawDt = LastBeginTime < 0.0 ? 0.f : FMath::Clamp(float(InTime - LastBeginTime), 0.f, 0.1f);
	LastBeginTime = InTime;
	Time = InTime;
	bUIHovered = false;
	bClickConsumed = false;
	bHasTip = false;
	TipTitle.Reset();
	TipBody.Reset();
	PrevHoverSfxKey = HoverSfxKey;
	HoverSfxKey = 0;
	if (!InInput.bLmbDown) { ActiveDragKey = 0; }
	Translate = FVector2D::ZeroVector;
	TranslateStack.Reset();
	ClipStack.Reset();
	UpdateGesture();

	if (!Canvas)
	{
		return;
	}
	ApplyScissor(FIntRect());   // no clipping at the start of a frame
	const float CW = FMath::Max(1.f, Canvas->ClipX);
	const float CH = FMath::Max(1.f, Canvas->ClipY);
	FV::UpdateCanvasForViewport(CW, CH);
	Scale = FMath::Max(0.1f, FMath::Min(CW / FV::VW, CH / FV::VH));
	Offset = FVector2D(FMath::FloorToFloat((CW - FV::VW * Scale) * 0.5f), FMath::FloorToFloat((CH - FV::VH * Scale) * 0.5f));

	EnsureResources();

	// Clear to black (this also paints the letterbox bars).
	TileScreen(0.f, 0.f, CW, CH, FLinearColor::Black, nullptr, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Opaque, 0.f, false);
}

void FVDraw::End()
{
	if (Canvas && ClipStack.Num() > 0)
	{
		// Unbalanced PushClip: flush what was clipped and draw the rest of the frame unclipped.
		ApplyScissor(FIntRect());
		ClipStack.Reset();
	}
	Translate = FVector2D::ZeroVector;
	TranslateStack.Reset();
	if (Canvas)
	{
		// Letterbox bars (landscape only): cover anything that was drawn outside the virtual canvas.
		// Portrait (phones): no bars — the world and the backgrounds bleed beyond the canvas, so a phone whose aspect
		// is outside the 600..900 canvas range still shows the scene edge to edge instead of black stripes.
		if (!FV::IsPortrait())
		{
			const float CW = Canvas->ClipX;
			const float CH = Canvas->ClipY;
			const float L = FMath::RoundToFloat(Offset.X);
			const float T = FMath::RoundToFloat(Offset.Y);
			const float R = FMath::RoundToFloat(Offset.X + FV::VW * Scale);
			const float B = FMath::RoundToFloat(Offset.Y + FV::VH * Scale);
			if (L > 0.f) { TileScreen(0.f, 0.f, L, CH, FLinearColor::Black, nullptr, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Opaque, 0.f, false); }
			if (R < CW) { TileScreen(R, 0.f, CW - R, CH, FLinearColor::Black, nullptr, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Opaque, 0.f, false); }
			if (T > 0.f) { TileScreen(0.f, 0.f, CW, T, FLinearColor::Black, nullptr, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Opaque, 0.f, false); }
			if (B < CH) { TileScreen(0.f, B, CW, CH - B, FLinearColor::Black, nullptr, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Opaque, 0.f, false); }
		}

		if (AfterLetterbox)
		{
			AfterLetterbox(*this);
		}

		if (bHasTip)
		{
			DrawTooltipNow();
		}
	}

	AfterLetterbox = nullptr;
	// No hover sounds on touch screens: every tap would "hover" a new widget.
	if (HoverSfxKey != 0 && HoverSfxKey != PrevHoverSfxKey && !FV::IsTouchUI())
	{
		FVDrawImpl::PlaySfx(EFVSfx::Hover, 0.45f);
	}

	bHasTip = false;
	TipTitle.Reset();
	TipBody.Reset();
	Canvas = nullptr;   // the UCanvas is only valid during DrawHUD
}

void FVDraw::EnsureResources()
{
	Font = FVDrawImpl::GetFont();

	CircleTex = FVDrawImpl::GCircleTex.Get();
	GlowTex = FVDrawImpl::GGlowTex.Get();
	if (CircleTex && GlowTex)
	{
		return;
	}
	UFVSprites* Sprites = UFVSprites::Get();
	if (!Sprites)
	{
		return;
	}

	if (!CircleTex)
	{
		const int32 N = 64;
		FFVImage Img(N, N, FColor(255, 255, 255, 0));
		const float Rad = N * 0.5f;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const float DX = X + 0.5f - Rad;
				const float DY = Y + 0.5f - Rad;
				const float D = FMath::Sqrt(DX * DX + DY * DY);
				const float A = FMath::Clamp(Rad - D, 0.f, 1.f);
				Img.Set(X, Y, FColor(255, 255, 255, uint8(FMath::RoundToInt(A * 255.f))));
			}
		}
		UTexture2D* Tex = Sprites->MakeTexture(Img, TEXT("FV_UI_Circle"));
		FVDrawImpl::GCircleTex = Tex;
		CircleTex = Tex;
	}
	if (!GlowTex)
	{
		const int32 N = 64;
		FFVImage Img(N, N, FColor(0, 0, 0, 0));
		const float Rad = N * 0.5f;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const float DX = X + 0.5f - Rad;
				const float DY = Y + 0.5f - Rad;
				const float D = FMath::Clamp(FMath::Sqrt(DX * DX + DY * DY) / Rad, 0.f, 1.f);
				float F = 1.f - D;
				F = F * F * (3.f - 2.f * F);
				F = F * F;
				const uint8 V = uint8(FMath::Clamp(FMath::RoundToInt(F * 255.f), 0, 255));
				Img.Set(X, Y, FColor(V, V, V, V));   // premultiplied (additive)
			}
		}
		UTexture2D* Tex = Sprites->MakeTexture(Img, TEXT("FV_UI_Glow"));
		if (Tex)
		{
			Tex->SRGB = false;
			Tex->Filter = TF_Bilinear;
			Tex->UpdateResource();
		}
		FVDrawImpl::GGlowTex = Tex;
		GlowTex = Tex;
	}
}

// ============================================================================================
// Primitives
// ============================================================================================
void FVDraw::TileScreen(float SX, float SY, float SW, float SH, const FLinearColor& C, UTexture2D* Tex,
	const FVector2D& UV0, const FVector2D& UV1, int32 BlendMode, float RotationDeg, bool bSnap)
{
	if (!Canvas || C.A <= 0.f || SW <= 0.f || SH <= 0.f)
	{
		return;
	}
	float X0 = SX, Y0 = SY, X1 = SX + SW, Y1 = SY + SH;
	if (bSnap && RotationDeg == 0.f)
	{
		X0 = FMath::RoundToFloat(X0); Y0 = FMath::RoundToFloat(Y0);
		X1 = FMath::RoundToFloat(X1); Y1 = FMath::RoundToFloat(Y1);
		if (X1 <= X0) { X1 = X0 + 1.f; }
		if (Y1 <= Y0) { Y1 = Y0 + 1.f; }
	}
	const FVector2D Pos(X0, Y0);
	const FVector2D Size(X1 - X0, Y1 - Y0);

	if (Tex)
	{
		FTextureResource* Res = Tex->GetResource();
		if (!Res)
		{
			return;
		}
		FCanvasTileItem Item(Pos, Res, Size, UV0, UV1, C);
		Item.BlendMode = ESimpleElementBlendMode(BlendMode);
		if (RotationDeg != 0.f)
		{
			Item.Rotation = FRotator(0.f, RotationDeg, 0.f);
			Item.PivotPoint = FVector2D(0.5f, 0.5f);
		}
		Canvas->DrawItem(Item);
	}
	else
	{
		FCanvasTileItem Item(Pos, Size, C);
		Item.BlendMode = ESimpleElementBlendMode(BlendMode);
		if (RotationDeg != 0.f)
		{
			Item.Rotation = FRotator(0.f, RotationDeg, 0.f);
			Item.PivotPoint = FVector2D(0.5f, 0.5f);
		}
		Canvas->DrawItem(Item);
	}
}

void FVDraw::Rect(const FFVRect& R, const FLinearColor& C)
{
	if (R.W <= 0.f || R.H <= 0.f) { return; }
	const FVector2D P = ToScreen(R.Pos());
	TileScreen(P.X, P.Y, R.W * Scale, R.H * Scale, C);
}

void FVDraw::Frame(const FFVRect& R, const FLinearColor& C, float Thickness)
{
	const float T = FMath::Max(0.5f, FMath::Min(Thickness, FMath::Min(R.W, R.H) * 0.5f));
	Rect(FFVRect(R.X, R.Y, R.W, T), C);
	Rect(FFVRect(R.X, R.Bottom() - T, R.W, T), C);
	Rect(FFVRect(R.X, R.Y + T, T, R.H - 2.f * T), C);
	Rect(FFVRect(R.Right() - T, R.Y + T, T, R.H - 2.f * T), C);
}

void FVDraw::Line(const FVector2D& A, const FVector2D& B, const FLinearColor& C, float Thickness)
{
	const FVector2D Delta = B - A;
	const float Len = Delta.Size();
	const float T = FMath::Max(0.25f, Thickness);
	if (Len < 0.01f)
	{
		Rect(FFVRect(A.X - T * 0.5f, A.Y - T * 0.5f, T, T), C);
		return;
	}
	// Axis-aligned lines: crisp rects.
	if (FMath::Abs(Delta.Y) < 0.001f)
	{
		Rect(FFVRect(FMath::Min(A.X, B.X), A.Y - T * 0.5f, Len, T), C);
		return;
	}
	if (FMath::Abs(Delta.X) < 0.001f)
	{
		Rect(FFVRect(A.X - T * 0.5f, FMath::Min(A.Y, B.Y), T, Len), C);
		return;
	}
	// Rotated quad around the segment midpoint (alpha-correct, unlike thick canvas lines).
	const FVector2D Mid = ToScreen((A + B) * 0.5f);
	const float SL = Len * Scale;
	const float ST = FMath::Max(1.f, T * Scale);
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	TileScreen(Mid.X - SL * 0.5f, Mid.Y - ST * 0.5f, SL, ST, C, nullptr, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Translucent, Deg, false);
}

void FVDraw::Sprite(const FFVSprite& S, const FVector2D& Pos, float ScaleMul, const FLinearColor& Tint, bool bFlipX)
{
	SpriteRect(S, FFVRect(Pos.X, Pos.Y, S.W * ScaleMul, S.H * ScaleMul), Tint, bFlipX);
}

void FVDraw::SpriteCentered(const FFVSprite& S, const FVector2D& Center, float ScaleMul, const FLinearColor& Tint, bool bFlipX, float RotationDeg)
{
	const float W = S.W * ScaleMul;
	const float H = S.H * ScaleMul;
	if (RotationDeg == 0.f)
	{
		SpriteRect(S, FFVRect(Center.X - W * 0.5f, Center.Y - H * 0.5f, W, H), Tint, bFlipX);
		return;
	}
	if (!S.Tex) { return; }
	FVector2D UV0 = S.UV0, UV1 = S.UV1;
	if (bFlipX) { Swap(UV0.X, UV1.X); }
	const FVector2D P = ToScreen(FVector2D(Center.X - W * 0.5f, Center.Y - H * 0.5f));
	TileScreen(P.X, P.Y, W * Scale, H * Scale, Tint, S.Tex, UV0, UV1, SE_BLEND_Translucent, RotationDeg, false);
}

void FVDraw::SpriteRect(const FFVSprite& S, const FFVRect& R, const FLinearColor& Tint, bool bFlipX)
{
	if (!S.Tex || R.W <= 0.f || R.H <= 0.f) { return; }
	FVector2D UV0 = S.UV0, UV1 = S.UV1;
	if (bFlipX) { Swap(UV0.X, UV1.X); }
	const FVector2D P = ToScreen(R.Pos());
	TileScreen(P.X, P.Y, R.W * Scale, R.H * Scale, Tint, S.Tex, UV0, UV1, SE_BLEND_Translucent);
}

void FVDraw::SpriteAdditive(const FFVSprite& S, const FVector2D& Center, float ScaleMul, const FLinearColor& Tint)
{
	if (!S.Tex) { return; }
	const float W = S.W * ScaleMul;
	const float H = S.H * ScaleMul;
	const FLinearColor Pre(Tint.R * Tint.A, Tint.G * Tint.A, Tint.B * Tint.A, 1.f);
	const FVector2D P = ToScreen(FVector2D(Center.X - W * 0.5f, Center.Y - H * 0.5f));
	TileScreen(P.X, P.Y, W * Scale, H * Scale, Pre, S.Tex, S.UV0, S.UV1, SE_BLEND_Additive);
}

void FVDraw::Texture(UTexture2D* Tex, const FFVRect& R, const FVector2D& UV0, const FVector2D& UV1, const FLinearColor& Tint)
{
	if (!Tex || R.W <= 0.f || R.H <= 0.f) { return; }
	const FVector2D P = ToScreen(R.Pos());
	TileScreen(P.X, P.Y, R.W * Scale, R.H * Scale, Tint, Tex, UV0, UV1, SE_BLEND_Translucent);
}

void FVDraw::Glow(const FVector2D& Center, float Radius, const FLinearColor& C)
{
	if (!GlowTex || Radius <= 0.f || C.A <= 0.f) { return; }
	const FLinearColor Pre(C.R * C.A, C.G * C.A, C.B * C.A, 1.f);
	const FVector2D P = ToScreen(Center - FVector2D(Radius, Radius));
	TileScreen(P.X, P.Y, Radius * 2.f * Scale, Radius * 2.f * Scale, Pre, GlowTex, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Additive, 0.f, false);
}

void FVDraw::Circle(const FVector2D& Center, float Radius, const FLinearColor& C)
{
	if (Radius <= 0.f || C.A <= 0.f) { return; }
	if (!CircleTex)
	{
		// Fallback: a small plus-shaped blob made of rects.
		Rect(FFVRect(Center.X - Radius, Center.Y - Radius * 0.6f, Radius * 2.f, Radius * 1.2f), C);
		Rect(FFVRect(Center.X - Radius * 0.6f, Center.Y - Radius, Radius * 1.2f, Radius * 2.f), C);
		return;
	}
	const FVector2D P = ToScreen(Center - FVector2D(Radius, Radius));
	TileScreen(P.X, P.Y, Radius * 2.f * Scale, Radius * 2.f * Scale, C, CircleTex, FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Translucent);
}

void FVDraw::Dim(float Alpha)
{
	if (!Canvas) { return; }
	TileScreen(0.f, 0.f, Canvas->ClipX, Canvas->ClipY, FLinearColor(0.f, 0.f, 0.f, FMath::Clamp(Alpha, 0.f, 1.f)), nullptr,
		FVector2D(0, 0), FVector2D(1, 1), SE_BLEND_Translucent, 0.f, false);
}

void FVDraw::Ring(const FVector2D& Center, float RadiusX, float RadiusY, const FLinearColor& C, float Thickness)
{
	const float RMax = FMath::Max(RadiusX, RadiusY);
	if (RMax <= 0.f || C.A <= 0.f) { return; }
	const float T = FMath::Max(0.5f, Thickness);
	const int32 N = FMath::Clamp(FMath::RoundToInt(2.f * PI * RMax / FMath::Max(1.f, T * 1.2f)), 8, 160);
	for (int32 i = 0; i < N; ++i)
	{
		const float A = 2.f * PI * float(i) / float(N);
		const float X = Center.X + FMath::Cos(A) * RadiusX;
		const float Y = Center.Y + FMath::Sin(A) * RadiusY;
		Rect(FFVRect(X - T * 0.5f, Y - T * 0.5f, T, T), C);
	}
}

void FVDraw::GradientV(const FFVRect& R, const FLinearColor& Top, const FLinearColor& Bottom, int32 Bands)
{
	const int32 N = FMath::Clamp(Bands, 1, 256);
	for (int32 i = 0; i < N; ++i)
	{
		const float Y0 = R.Y + R.H * float(i) / float(N);
		const float Y1 = R.Y + R.H * float(i + 1) / float(N);
		const float T = N > 1 ? float(i) / float(N - 1) : 0.f;
		Rect(FFVRect(R.X, Y0, R.W, Y1 - Y0), Top + (Bottom - Top) * T);
	}
}

// ============================================================================================
// Text
// ============================================================================================
void FVDraw::DrawTextRaw(const FString& S, const FVector2D& VirtualPos, float Size, const FLinearColor& C, bool bBold, bool bShadow, const FLinearColor& ShadowC, float ShadowOffsetPx)
{
	if (!Canvas || S.IsEmpty() || C.A <= 0.f) { return; }
	UFont* F = FVDrawImpl::GetFont();
	if (!F) { return; }
	FVector2D SP = ToScreen(VirtualPos);
	SP.X = FMath::RoundToFloat(SP.X);
	SP.Y = FMath::RoundToFloat(SP.Y);
	FCanvasTextItem Item(SP, FText::FromString(S), FVDrawImpl::MakeFont(F, Size * Scale, bBold), C);
	Item.BlendMode = SE_BLEND_Translucent;
	if (bShadow)
	{
		Item.EnableShadow(ShadowC, FVector2D(ShadowOffsetPx, ShadowOffsetPx));
	}
	Canvas->DrawItem(Item);
}

void FVDraw::Text(const FString& S, const FVector2D& Pos, float Size, const FLinearColor& C, EFVAlign Align, bool bShadow, bool bBold)
{
	if (!Canvas || S.IsEmpty()) { return; }
	FVector2D P = Pos;
	if (Align != EFVAlign::Left)
	{
		const float W = MeasureText(S, Size, bBold).X;
		P.X -= (Align == EFVAlign::Center) ? W * 0.5f : W;
	}
	FLinearColor ShadowC = FV::Col(40, 24, 16, 170);
	if (C.GetLuminance() < 0.12f)
	{
		// Dark ink on parchment: warm, light-brown drop shadow (Stardew look).
		ShadowC = FV::Col(222, 156, 92, 200);
	}
	ShadowC.A *= C.A;
	const float Off = FMath::Max(1.f, FMath::RoundToFloat(Scale * 0.6f));
	DrawTextRaw(S, P, Size, C, bBold, bShadow, ShadowC, Off);
}

void FVDraw::TextOutlined(const FString& S, const FVector2D& Pos, float Size, const FLinearColor& C, EFVAlign Align, const FLinearColor& OutlineC)
{
	if (!Canvas || S.IsEmpty()) { return; }
	FVector2D P = Pos;
	if (Align != EFVAlign::Left)
	{
		const float W = MeasureText(S, Size, true).X;
		P.X -= (Align == EFVAlign::Center) ? W * 0.5f : W;
	}
	const float O = FMath::Max(1.f, FMath::RoundToFloat(Scale * 0.75f)) / Scale;   // virtual units
	static const FVector2D Dirs[8] = {
		FVector2D(-1, 0), FVector2D(1, 0), FVector2D(0, -1), FVector2D(0, 1),
		FVector2D(-1, -1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(1, 1) };
	FLinearColor OC = OutlineC;
	OC.A *= C.A;
	for (const FVector2D& D : Dirs)
	{
		DrawTextRaw(S, P + D * O, Size, OC, true, false, FLinearColor::Transparent, 0.f);
	}
	DrawTextRaw(S, P, Size, C, true, false, FLinearColor::Transparent, 0.f);
}

FVector2D FVDraw::MeasureText(const FString& S, float Size, bool bBold) const
{
	const float Sc = FMath::Max(Scale, 0.25f);
	if (S.IsEmpty())
	{
		return FVector2D(0.f, Size * 1.56f);
	}
	if (UFont* F = FVDrawImpl::GetFont())
	{
		TSharedPtr<FSlateFontMeasure> M = FVDrawImpl::GetMeasure();
		if (M.IsValid())
		{
			const FVector2D V = M->Measure(S, FVDrawImpl::MakeFont(F, Size * Sc, bBold), 1.f);
			return V / Sc;
		}
	}
	// Rough fallback (no font services yet).
	return FVector2D(S.Len() * Size * (bBold ? 0.64f : 0.6f), Size * 1.56f);
}

float FVDraw::LineHeight(float Size, bool bBold) const
{
	return MeasureText(TEXT("Щy"), Size, bBold).Y;
}

float FVDraw::TextWrapped(const FString& S, const FVector2D& Pos, float MaxWidth, float Size, const FLinearColor& C, bool bBold, float LineSpacing)
{
	TArray<FString> Lines;
	FVDrawImpl::WrapLines(*this, S, MaxWidth, Size, bBold, Lines);
	const float Step = FVDrawImpl::LineStep(Size, LineSpacing);
	float Y = Pos.Y;
	for (const FString& L : Lines)
	{
		if (!L.IsEmpty())
		{
			Text(L, FVector2D(Pos.X, Y), Size, C, EFVAlign::Left, false, bBold);
		}
		Y += Step;
	}
	return Lines.Num() * Step;
}

float FVDraw::MeasureWrapped(const FString& S, float MaxWidth, float Size, bool bBold, float LineSpacing) const
{
	TArray<FString> Lines;
	FVDrawImpl::WrapLines(*this, S, MaxWidth, Size, bBold, Lines);
	return Lines.Num() * FVDrawImpl::LineStep(Size, LineSpacing);
}

FString FVDraw::Ellipsize(const FString& S, float MaxWidth, float Size, bool bBold) const
{
	if (S.IsEmpty() || MeasureText(S, Size, bBold).X <= MaxWidth)
	{
		return S;
	}
	const FString Dots = TEXT("…");
	int32 Lo = 0, Hi = S.Len();
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi + 1) / 2;
		if (MeasureText(S.Left(Mid) + Dots, Size, bBold).X <= MaxWidth) { Lo = Mid; }
		else { Hi = Mid - 1; }
	}
	return S.Left(Lo).TrimEnd() + Dots;
}

// ============================================================================================
// Widgets
// ============================================================================================
void FVDraw::Panel(const FFVRect& R, EFVPanel Style)
{
	if (R.W <= 2.f || R.H <= 2.f) { return; }
	switch (Style)
	{
	case EFVPanel::Wood:
	{
		const FLinearColor Shade = FV::Col(122, 58, 26);
		const FLinearColor Grain = FV::Col(184, 100, 44);
		Rect(R.Offset(2.f, 3.f), FV::Col(0, 0, 0, 80));
		Rect(R, FVPal::WoodDark());
		Rect(R.Inset(1.f), FVPal::Wood());
		// bevel: light from the top-left
		Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FVPal::WoodLight());
		Rect(FFVRect(R.X + 1.f, R.Y + 1.f, 1.f, R.H - 2.f), FVPal::WoodLight());
		Rect(FFVRect(R.X + 1.f, R.Bottom() - 2.f, R.W - 2.f, 1.f), Shade);
		Rect(FFVRect(R.Right() - 2.f, R.Y + 2.f, 1.f, R.H - 3.f), Shade);
		// wood grain streaks on the frame
		if (R.W > 40.f)
		{
			Rect(FFVRect(R.X + 8.f, R.Y + 2.5f, R.W * 0.22f, 0.5f), Grain);
			Rect(FFVRect(R.X + R.W * 0.55f, R.Y + 2.5f, R.W * 0.3f, 0.5f), Grain);
			Rect(FFVRect(R.X + R.W * 0.18f, R.Bottom() - 3.f, R.W * 0.35f, 0.5f), Grain);
			Rect(FFVRect(R.X + R.W * 0.7f, R.Bottom() - 3.f, R.W * 0.2f, 0.5f), Grain);
		}
		const FFVRect In = R.Inset(4.f);
		Rect(In, FVPal::WoodDark());
		Rect(In.Inset(1.f), FVPal::Parchment());
		// inset shadow on the parchment (top/left) and a soft highlight (bottom/right)
		Rect(FFVRect(In.X + 1.f, In.Y + 1.f, In.W - 2.f, 2.f), FV::Alpha(FVPal::ParchDark(), 0.7f));
		Rect(FFVRect(In.X + 1.f, In.Y + 3.f, 1.f, In.H - 4.f), FV::Alpha(FVPal::ParchDark(), 0.5f));
		Rect(FFVRect(In.X + 1.f, In.Bottom() - 2.f, In.W - 2.f, 1.f), FV::Col(255, 236, 196));
		// corner studs
		const FLinearColor Stud = FV::Col(236, 170, 90);
		const FLinearColor StudDark = FV::Col(90, 42, 20);
		const FVector2D Studs[4] = {
			FVector2D(R.X + 1.5f, R.Y + 1.5f), FVector2D(R.Right() - 3.5f, R.Y + 1.5f),
			FVector2D(R.X + 1.5f, R.Bottom() - 3.5f), FVector2D(R.Right() - 3.5f, R.Bottom() - 3.5f) };
		for (const FVector2D& P : Studs)
		{
			Rect(FFVRect(P.X, P.Y, 2.f, 2.f), StudDark);
			Rect(FFVRect(P.X, P.Y, 1.f, 1.f), Stud);
		}
		break;
	}
	case EFVPanel::Parchment:
	{
		Rect(R.Offset(1.f, 1.f), FV::Col(120, 70, 36, 60));
		Rect(R, FV::Col(176, 110, 56));
		Rect(R.Inset(1.f), FVPal::Parchment());
		Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 238, 200));
		Rect(FFVRect(R.X + 1.f, R.Bottom() - 2.f, R.W - 2.f, 1.f), FVPal::ParchDark());
		break;
	}
	case EFVPanel::Dark:
	{
		Rect(R, FV::Col(26, 18, 14, 196));
		Frame(R, FV::Col(0, 0, 0, 90), 1.f);
		Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 255, 255, 26));
		break;
	}
	case EFVPanel::Tooltip:
	default:
	{
		Rect(R.Offset(2.f, 2.f), FV::Col(0, 0, 0, 90));
		Rect(R, FVPal::WoodDark());
		Rect(R.Inset(1.f), FV::Col(255, 243, 214));
		Frame(R.Inset(2.f), FV::Col(226, 176, 110, 150), 1.f);
		break;
	}
	}
}

bool FVDraw::Window(const FFVRect& R, const FString& Title)
{
	Panel(R, EFVPanel::Wood);
	MarkUI(R);

	// Portrait (phones): bigger title (FV::Ui::Title) on a taller plate; plate + ribbon tails + the close button all
	// stay inside the 360 px canvas (the plate never reaches the close button at the top-right).
	const bool bPortraitWin = FV::IsPortrait();
	const float CloseSize = bPortraitWin ? 24.f : 16.f;
	if (!Title.IsEmpty())
	{
		float TS = bPortraitWin ? FV::Ui::Title() : 11.f;
		// the plate (+ its 8 px tails) must leave room for the close button on both sides (it is centered)
		const float MaxTextW = bPortraitWin ? R.W - 2.f * (CloseSize + 22.f) - 30.f : R.W - 70.f;
		FVector2D TSz = MeasureText(Title, TS, true);
		while (bPortraitWin && TSz.X > MaxTextW && TS > 11.f)
		{
			TS -= 0.5f;
			TSz = MeasureText(Title, TS, true);
		}
		const FString Shown = TSz.X > MaxTextW ? Ellipsize(Title, MaxTextW, TS, true) : Title;
		if (Shown != Title) { TSz = MeasureText(Shown, TS, true); }
		const float PW = bPortraitWin ? FMath::RoundToFloat(TSz.X + 34.f) : FMath::Min(TSz.X + 30.f, R.W - 40.f);
		const float PH = bPortraitWin ? FMath::RoundToFloat(FMath::Max(22.f, TSz.Y + 3.f)) : 20.f;
		const float PX = FMath::RoundToFloat(R.X + (R.W - PW) * 0.5f);
		const float PY = bPortraitWin ? FMath::RoundToFloat(FMath::Max(1.f, R.Y - PH * 0.62f)) : R.Y - 11.f;
		const FLinearColor Shade = FV::Col(122, 58, 26);
		// ribbon tails
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float TX = Side == 0 ? PX - 8.f : PX + PW - 2.f;
			Rect(FFVRect(TX, PY + 6.f, 10.f, PH - 4.f), FVPal::WoodDark());
			Rect(FFVRect(TX + 1.f, PY + 7.f, 8.f, PH - 6.f), Shade);
			// notch
			Rect(FFVRect(Side == 0 ? TX : TX + 7.f, PY + 6.f + (PH - 4.f) * 0.5f - 1.5f, 3.f, 3.f), FVPal::Wood());
		}
		MarkUI(FFVRect(PX - 8.f, PY, PW + 16.f, PH));
		Rect(FFVRect(PX + 1.f, PY + 2.f, PW, PH), FV::Col(0, 0, 0, 70));
		Rect(FFVRect(PX, PY, PW, PH), FVPal::WoodDark());
		Rect(FFVRect(PX + 1.f, PY + 1.f, PW - 2.f, PH - 2.f), FVPal::Wood());
		Rect(FFVRect(PX + 1.f, PY + 1.f, PW - 2.f, 1.f), FVPal::WoodLight());
		Rect(FFVRect(PX + 1.f, PY + PH - 2.f, PW - 2.f, 1.f), Shade);
		Frame(FFVRect(PX + 3.f, PY + 3.f, PW - 6.f, PH - 6.f), FV::Col(120, 56, 24, 140), 0.5f);
		TextOutlined(Shown, FVector2D(PX + PW * 0.5f, FMath::RoundToFloat(PY + (PH - TSz.Y) * 0.5f)), TS, FV::Col(255, 238, 200), EFVAlign::Center, FV::Col(70, 32, 14));
	}

	// Close [X] (portrait / phones: a bigger button that is easy to hit with a thumb, still inside the canvas;
	// its bottom edge stays at R.Y + 12 so content starting at R.Y + 16 is never covered)
	const FFVRect CR = bPortraitWin
		? FFVRect(FMath::Min(R.Right() - 21.f, FV::VW - 3.f - CloseSize), R.Y + 12.f - CloseSize, CloseSize, CloseSize)
		: FFVRect(R.Right() - 13.f, R.Y - 5.f, 16.f, 16.f);
	return CloseButton(CR);
}

bool FVDraw::CloseButton(const FFVRect& CR)
{
	MarkUI(CR);   // a click here is never the world action (windows mark their whole rect anyway)
	const bool bHover = Hover(CR);
	const bool bDown = bHover && Input().bLmbDown;
	const float DY = bDown ? 1.f : 0.f;
	Rect(CR.Offset(1.f, 1.f), FV::Col(0, 0, 0, 70));
	Rect(CR, FV::Col(84, 22, 18));
	const FLinearColor Face = bHover ? FV::Col(232, 96, 72) : FV::Col(204, 66, 52);
	Rect(FFVRect(CR.X + 1.f, CR.Y + 1.f, CR.W - 2.f, CR.H - 2.f), FV::Col(140, 40, 32));
	Rect(FFVRect(CR.X + 1.f, CR.Y + 1.f + DY, CR.W - 2.f, CR.H - 3.f - DY), Face);
	if (!bDown) { Rect(FFVRect(CR.X + 1.f, CR.Y + 1.f, CR.W - 2.f, 1.f), FV::Col(250, 150, 120)); }
	// pixel "X" (designed for 16 px, scaled with the button)
	const float XS = CR.W / 16.f;
	const float XX = CR.X + 4.5f * XS;
	const float XY = CR.Y + 4.f * XS + DY;
	for (int32 i = 0; i < 7; ++i)
	{
		Rect(FFVRect(XX + i * XS, XY + (i + 1.f) * XS, 1.5f * XS, XS), FV::Col(90, 20, 16));
		Rect(FFVRect(XX + (6 - i) * XS, XY + (i + 1.f) * XS, 1.5f * XS, XS), FV::Col(90, 20, 16));
		Rect(FFVRect(XX + i * XS, XY + i * XS, 1.5f * XS, XS), FV::Col(255, 244, 236));
		Rect(FFVRect(XX + (6 - i) * XS, XY + i * XS, 1.5f * XS, XS), FV::Col(255, 244, 236));
	}
	if (bHover)
	{
		NoteHover(FVDrawImpl::RectKey(CR, 0xC105E));
	}
	if (Clicked(CR))
	{
		FVDrawImpl::PlaySfx(EFVSfx::Click);
		return true;
	}
	return false;
}

bool FVDraw::Button(const FFVRect& R, const FString& Label, EFVButton Style, bool bEnabled, float TextSize)
{
	const bool bHover = Hover(R);
	const bool bDown = bHover && bEnabled && Input().bLmbDown;
	FVDrawImpl::FButtonLook L = FVDrawImpl::LookFor(Style, bEnabled);
	const float DY = bDown ? 1.f : 0.f;

	if (Style == EFVButton::Ghost && bEnabled)
	{
		if (bHover)
		{
			Rect(R, FV::Col(214, 150, 90, bDown ? 120 : 80));
			Frame(R, FV::Col(176, 110, 56, 160), 1.f);
		}
	}
	else
	{
		FLinearColor Face = L.Face;
		if (bEnabled && bHover) { Face = FVDrawImpl::Lighten(Face, bDown ? 0.05f : 0.2f); }
		if (bDown) { Face = FVDrawImpl::Darken(Face, 0.06f); }
		if (!bDown) { Rect(FFVRect(R.X + 1.f, R.Bottom(), R.W - 1.f, 1.f), FV::Col(0, 0, 0, 60)); }
		Rect(R, L.Outline);
		const FFVRect F = R.Inset(1.f);
		Rect(F, L.Dark);
		Rect(FFVRect(F.X, F.Y + DY, F.W, bDown ? F.H - 1.f : F.H - 2.f), Face);
		if (!bDown)
		{
			Rect(FFVRect(F.X, F.Y, F.W, 1.f), L.Light);
			Rect(FFVRect(F.X, F.Y + 1.f, 1.f, F.H - 3.f), FV::Alpha(L.Light, 0.6f));
		}
		else
		{
			Rect(FFVRect(F.X, F.Y, F.W, 1.f), FVDrawImpl::Darken(L.Dark, 0.1f));
		}
	}

	if (!Label.IsEmpty())
	{
		float S = TextSize;
		FVector2D Sz = MeasureText(Label, S, true);
		while (Sz.X > R.W - 6.f && S > 5.5f)
		{
			S -= 0.5f;
			Sz = MeasureText(Label, S, true);
		}
		const FVector2D TP(R.X + R.W * 0.5f, FMath::RoundToFloat(R.Y + (R.H - Sz.Y) * 0.5f - 0.5f + DY));
		if (L.bOutlinedText && bEnabled)
		{
			TextOutlined(Label, TP, S, L.Text, EFVAlign::Center, L.TextOutline);
		}
		else
		{
			Text(Label, TP, S, L.Text, EFVAlign::Center, bEnabled && Style != EFVButton::Ghost, true);
		}
	}

	if (bHover && bEnabled)
	{
		NoteHover(FVDrawImpl::RectKey(R, GetTypeHash(Label)));
	}
	if (Clicked(R) && bEnabled)
	{
		FVDrawImpl::PlaySfx(EFVSfx::Click);
		return true;
	}
	return false;
}

bool FVDraw::IconButton(const FFVRect& R, const FFVSprite& Icon, bool bSelected, bool bEnabled, const FString& TooltipText)
{
	const bool bHover = Hover(R);
	const bool bDown = bHover && bEnabled && Input().bLmbDown;
	const float DY = bDown ? 1.f : 0.f;

	FLinearColor Face = bSelected ? FV::Col(255, 214, 120) : FV::Col(236, 184, 116);
	if (bHover && bEnabled) { Face = FVDrawImpl::Lighten(Face, 0.22f); }
	if (!bEnabled) { Face = FV::Col(190, 172, 152); }

	if (!bDown) { Rect(FFVRect(R.X + 1.f, R.Bottom(), R.W - 1.f, 1.f), FV::Col(0, 0, 0, 60)); }
	Rect(R, bSelected ? FV::Col(120, 60, 16) : FVPal::WoodDark());
	const FFVRect F = R.Inset(1.f);
	Rect(F, FV::Col(176, 110, 56));
	Rect(FFVRect(F.X, F.Y + DY, F.W, F.H - 2.f), Face);
	if (!bDown) { Rect(FFVRect(F.X, F.Y, F.W, 1.f), FV::Col(255, 236, 190)); }

	if (Icon.IsValid() && Icon.W > 0.f && Icon.H > 0.f)
	{
		const float Avail = FMath::Min(R.W, R.H) - 4.f;
		float Mul = 1.f;
		const float MaxDim = FMath::Max(Icon.W, Icon.H);
		if (MaxDim > Avail) { Mul = Avail / MaxDim; }
		else if (MaxDim * 2.f <= Avail) { Mul = FMath::FloorToFloat(Avail / MaxDim); }
		const FLinearColor Tint = bEnabled ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.6f, 0.5f);
		SpriteCentered(Icon, FVector2D(R.X + R.W * 0.5f, R.Y + R.H * 0.5f - 0.5f + DY), Mul, Tint);
	}
	if (bSelected)
	{
		Frame(R.Inset(-1.f), FV::Col(255, 200, 60), 1.f);
	}

	if (bHover)
	{
		if (!TooltipText.IsEmpty()) { Tooltip(TooltipText); }
		if (bEnabled) { NoteHover(FVDrawImpl::RectKey(R, 0x1C0B)); }
	}
	if (Clicked(R) && bEnabled)
	{
		FVDrawImpl::PlaySfx(EFVSfx::Click);
		return true;
	}
	return false;
}

bool FVDraw::Tab(const FFVRect& R, const FString& Label, bool bActive)
{
	const bool bHover = Hover(R);
	if (bActive)
	{
		Rect(FFVRect(R.X, R.Y, R.W, R.H + 2.f), FVPal::WoodDark());
		Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, R.H + 2.f), FVPal::Parchment());
		Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 1.f), FV::Col(255, 238, 200));
	}
	else
	{
		const FFVRect T(R.X, R.Y + 2.f, R.W, R.H - 2.f);
		Rect(T, FVPal::WoodDark());
		Rect(T.Inset(1.f), bHover ? FV::Col(240, 198, 134) : FV::Col(214, 158, 96));
		Rect(FFVRect(T.X + 1.f, T.Y + 1.f, T.W - 2.f, 1.f), FV::Col(250, 214, 160));
	}
	if (!Label.IsEmpty())
	{
		// finger-sized portrait tabs get the bigger body text (shrinks to fit)
		float S = (FV::IsPortrait() && R.H >= 22.f) ? FV::Ui::Body() : 8.f;
		FVector2D Sz = MeasureText(Label, S, true);
		while (Sz.X > R.W - 4.f && S > 5.5f)
		{
			S -= 0.5f;
			Sz = MeasureText(Label, S, true);
		}
		const float TY = FMath::RoundToFloat(R.Y + (bActive ? 0.f : 2.f) + ((R.H - (bActive ? 0.f : 2.f)) - Sz.Y) * 0.5f);
		Text(Label, FVector2D(R.X + R.W * 0.5f, TY), S, bActive ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Center, bActive, true);
	}
	if (bHover && !bActive)
	{
		NoteHover(FVDrawImpl::RectKey(R, GetTypeHash(Label) ^ 0x7AB));
	}
	if (Clicked(R))
	{
		if (!bActive) { FVDrawImpl::PlaySfx(EFVSfx::Click); }
		return true;
	}
	return false;
}

bool FVDraw::Slot(const FFVRect& R, bool bSelected, bool bEnabled)
{
	const bool bHover = Hover(R);
	Rect(R, FV::Col(176, 110, 56));
	const FFVRect In = R.Inset(1.f);
	Rect(In, bEnabled ? FV::Col(255, 222, 158) : FV::Col(222, 204, 178));
	// inset: dark top/left, light bottom/right
	Rect(FFVRect(In.X, In.Y, In.W, 1.f), FV::Col(214, 150, 88));
	Rect(FFVRect(In.X, In.Y + 1.f, 1.f, In.H - 1.f), FV::Col(214, 150, 88));
	Rect(FFVRect(In.X + 1.f, In.Bottom() - 1.f, In.W - 1.f, 1.f), FV::Col(255, 240, 206));
	Rect(FFVRect(In.Right() - 1.f, In.Y + 1.f, 1.f, In.H - 2.f), FV::Col(255, 240, 206));
	if (bHover && bEnabled)
	{
		Rect(In.Inset(1.f), FV::Col(255, 255, 255, 60));
	}
	if (bSelected)
	{
		Frame(R.Inset(-1.f), FV::Col(214, 60, 40), 2.f);
		Frame(R.Inset(1.f), FV::Col(255, 200, 80, 200), 1.f);
	}
	else if (bHover && bEnabled)
	{
		Frame(R, FV::Col(236, 150, 60), 1.f);
	}
	if (Clicked(R) && bEnabled)
	{
		FVDrawImpl::PlaySfx(EFVSfx::Click, 0.7f);
		return true;
	}
	return false;
}

bool FVDraw::Hover(const FFVRect& R)
{
	if (bInputBlocked || !InputPtr || !InputPtr->bMouseValid)
	{
		return false;
	}
	// Fingers are less precise than a mouse: widgets accept taps slightly outside their frame.
	const FFVRect HitR = FV::IsTouchUI() ? R.Inset(-3.f) : R;
	const FVector2D M = Mouse();
	if (HitR.Contains(M) && InClip(M))
	{
		bUIHovered = true;
		return true;
	}
	return false;
}

bool FVDraw::Clicked(const FFVRect& R)
{
	if (!Hover(R))
	{
		return false;
	}
	if (!InputPtr->bLmbReleased || bClickConsumed || bDragGesture || bTapCancelled)
	{
		return false;
	}
	// The press must have started on this widget too (in the same local coordinates).
	const FFVRect HitR = FV::IsTouchUI() ? R.Inset(-3.f) : R;
	if (!HitR.Contains(PressPos()))
	{
		return false;
	}
	bClickConsumed = true;
	return true;
}

bool FVDraw::PressedIn(const FFVRect& R)
{
	if (!Hover(R))
	{
		return false;
	}
	if (InputPtr->bLmbPressed && !bClickConsumed)
	{
		bClickConsumed = true;
		return true;
	}
	return false;
}

// ============================================================================================
// Update 3: gestures, translation, clipping
// ============================================================================================
void FVDraw::UpdateGesture()
{
	const FFVInput& In = *InputPtr;
	const FVector2D M = In.Mouse;
	if (In.bLmbPressed)
	{
		bPtrDown = true;
		bDragGesture = false;
		bTapCancelled = false;
		GestureAxis = EDragAxis::None;
		GestureOwner = 0;
		PressPosV = M;
		LastPtrPos = M;
		PtrVel = FVector2D::ZeroVector;
	}
	else if (!In.bLmbDown && !In.bLmbReleased)
	{
		// Idle: forget the finished gesture (a release frame keeps its drag state so Clicked can reject it).
		bPtrDown = false;
		if (!In.bLmbDown) { GestureOwner = 0; }
	}
	if ((bPtrDown && In.bLmbDown) || In.bLmbReleased)
	{
		const FVector2D D = M - PressPosV;
		if (!bDragGesture && D.Size() > TapSlop())
		{
			bDragGesture = true;
			GestureAxis = FMath::Abs(D.X) > FMath::Abs(D.Y) ? EDragAxis::Horizontal : EDragAxis::Vertical;
		}
		// The release frame repeats the last position: keep the velocity measured while moving (flings).
		if (DrawDt > 0.f && !In.bLmbReleased)
		{
			const FVector2D V = (M - LastPtrPos) / DrawDt;
			PtrVel = FMath::Lerp(PtrVel, V, 0.5f);
		}
		LastPtrPos = M;
	}
	if (In.bLmbReleased)
	{
		bPtrDown = false;
	}
}

bool FVDraw::ClaimDrag(uint32 Key)
{
	if (Key == 0) { return false; }
	if (GestureOwner == 0) { GestureOwner = Key; }
	return GestureOwner == Key;
}

void FVDraw::PushTranslate(const FVector2D& Delta)
{
	TranslateStack.Add(Translate);
	Translate += Delta;
}

void FVDraw::PopTranslate()
{
	if (TranslateStack.Num() > 0)
	{
		Translate = TranslateStack.Pop(EAllowShrinking::No);
	}
	else
	{
		Translate = FVector2D::ZeroVector;
	}
}

bool FVDraw::InClip(const FVector2D& P) const
{
	if (ClipStack.Num() == 0) { return true; }
	return ClipStack.Last().V.Contains(P + Translate);
}

void FVDraw::ApplyScissor(const FIntRect& Px)
{
	if (!Canvas || !Canvas->Canvas) { return; }
	// Items batched so far are rendered with the previous scissor, then the new one applies to what follows.
	Canvas->Canvas->Flush_GameThread(true);
	Canvas->Canvas->SetRenderTargetScissorRect(Px);
}

void FVDraw::PushClip(const FFVRect& R)
{
	// Clip rect in untranslated virtual coords, intersected with the parent clip.
	FFVRect V(R.X + Translate.X, R.Y + Translate.Y, FMath::Max(0.f, R.W), FMath::Max(0.f, R.H));
	if (ClipStack.Num() > 0)
	{
		const FFVRect& P = ClipStack.Last().V;
		const float X0 = FMath::Max(V.X, P.X), Y0 = FMath::Max(V.Y, P.Y);
		const float X1 = FMath::Min(V.Right(), P.Right()), Y1 = FMath::Min(V.Bottom(), P.Bottom());
		V = FFVRect(X0, Y0, FMath::Max(0.f, X1 - X0), FMath::Max(0.f, Y1 - Y0));
	}
	const FVector2D S0 = Offset + V.Pos() * Scale;
	const FVector2D S1 = Offset + (V.Pos() + V.Size()) * Scale;
	FIntRect Px(FMath::RoundToInt(S0.X), FMath::RoundToInt(S0.Y), FMath::RoundToInt(S1.X), FMath::RoundToInt(S1.Y));
	if (Px.Width() <= 0 || Px.Height() <= 0)
	{
		Px = FIntRect(FMath::RoundToInt(S0.X), FMath::RoundToInt(S0.Y), FMath::RoundToInt(S0.X) + 1, FMath::RoundToInt(S0.Y) + 1);
	}
	if (Canvas)
	{
		const int32 CW = FMath::Max(1, FMath::RoundToInt(Canvas->ClipX));
		const int32 CH = FMath::Max(1, FMath::RoundToInt(Canvas->ClipY));
		Px.Min.X = FMath::Clamp(Px.Min.X, 0, CW); Px.Max.X = FMath::Clamp(Px.Max.X, Px.Min.X + 1, CW + 1);
		Px.Min.Y = FMath::Clamp(Px.Min.Y, 0, CH); Px.Max.Y = FMath::Clamp(Px.Max.Y, Px.Min.Y + 1, CH + 1);
	}
	ClipStack.Add(FClip{ V, Px });
	ApplyScissor(Px);
}

void FVDraw::PopClip()
{
	if (ClipStack.Num() == 0) { return; }
	ClipStack.Pop(EAllowShrinking::No);
	ApplyScissor(ClipStack.Num() > 0 ? ClipStack.Last().Px : FIntRect());
}

void FVDraw::Bar(const FFVRect& R, float Frac, const FLinearColor& Fill, const FLinearColor& Back, bool bFramed)
{
	FFVRect In = R;
	if (bFramed)
	{
		Rect(R, FVPal::WoodDark());
		In = R.Inset(1.f);
	}
	Rect(In, Back);
	const float F = FMath::Clamp(Frac, 0.f, 1.f);
	if (F > 0.f && In.W > 0.f)
	{
		const FFVRect FR(In.X, In.Y, FMath::Max(0.5f, In.W * F), In.H);
		Rect(FR, Fill);
		if (In.H >= 3.f) { Rect(FFVRect(FR.X, FR.Y, FR.W, 1.f), FVDrawImpl::Lighten(Fill, 0.35f)); }
		if (In.H >= 5.f) { Rect(FFVRect(FR.X, FR.Bottom() - 1.f, FR.W, 1.f), FVDrawImpl::Darken(Fill, 0.25f)); }
	}
}

void FVDraw::Stars(const FVector2D& Pos, EFVQuality Q, float ScaleMul)
{
	UFVSprites* Sp = UFVSprites::Get();
	if (!Sp) { return; }
	EFVIcon Icon;
	switch (Q)
	{
	case EFVQuality::Silver: Icon = EFVIcon::StarSilver; break;
	case EFVQuality::Gold: Icon = EFVIcon::StarGold; break;
	case EFVQuality::Iridium: Icon = EFVIcon::StarIridium; break;
	default: return;
	}
	const FFVSprite& S = Sp->Icon(Icon);
	Sprite(S, Pos, ScaleMul);
	if (Q == EFVQuality::Iridium)
	{
		// soft twinkle for the best quality
		const float Tw = 0.5f + 0.5f * FMath::Sin(float(Time) * 4.f + Pos.X * 0.3f);
		Glow(Pos + FVector2D(S.W, S.H) * ScaleMul * 0.5f, 5.f * ScaleMul, FLinearColor(0.8f, 0.3f, 1.f, 0.25f * Tw));
	}
}

float FVDraw::MoneyLabel(const FVector2D& Pos, int64 Amount, float Size, EFVAlign Align, const FLinearColor& C)
{
	const FString T = FV::Money(Amount);
	const FVector2D TS = MeasureText(T, Size, true);
	UFVSprites* Sp = UFVSprites::Get();
	const FFVSprite* Coin = Sp ? &Sp->Icon(EFVIcon::Coin) : nullptr;
	const float IconMul = FMath::Max(1.f, FMath::FloorToFloat(Size / 8.f + 0.25f));
	const float IW = (Coin && Coin->IsValid()) ? Coin->W * IconMul : 0.f;
	const float IH = (Coin && Coin->IsValid()) ? Coin->H * IconMul : 0.f;
	const float Gap = IW > 0.f ? 2.f : 0.f;
	const float Total = IW + Gap + TS.X;
	float X = Pos.X;
	if (Align == EFVAlign::Center) { X -= Total * 0.5f; }
	else if (Align == EFVAlign::Right) { X -= Total; }
	if (IW > 0.f)
	{
		const float CY = FMath::RoundToFloat(Pos.Y + TS.Y * 0.5f - IH * 0.5f);
		Sprite(*Coin, FVector2D(FMath::RoundToFloat(X), CY), IconMul);
	}
	const bool bLight = C.GetLuminance() > 0.3f;
	Text(T, FVector2D(X + IW + Gap, Pos.Y), Size, C, EFVAlign::Left, bLight, true);
	return Total;
}

void FVDraw::Tooltip(const FString& Title, const FString& Body)
{
	Tooltip(Title, Body, FVPal::Ink());
}

void FVDraw::Tooltip(const FString& Title, const FString& Body, const FLinearColor& TitleColor)
{
	TipTitle = Title;
	TipBody = Body;
	TipColor = TitleColor;
	bHasTip = !Title.IsEmpty() || !Body.IsEmpty();
}

void FVDraw::DrawTooltipNow()
{
	const bool bPortrait = FV::IsPortrait();
	const float TSize = bPortrait ? FV::Ui::Body() : 8.f;
	const float BSize = bPortrait ? FV::Ui::Small() + 0.5f : 7.f;
	const float Pad = bPortrait ? 6.f : 5.f;
	const float MaxBodyW = FMath::Min(bPortrait ? 240.f : 180.f, FV::VW - 8.f - 2.f * Pad);

	const FVector2D TSz = TipTitle.IsEmpty() ? FVector2D::ZeroVector : MeasureText(TipTitle, TSize, true);
	TArray<FString> Lines;
	FVDrawImpl::WrapLines(*this, TipBody, MaxBodyW, BSize, false, Lines);
	float BodyW = 0.f;
	for (const FString& L : Lines)
	{
		BodyW = FMath::Max(BodyW, MeasureText(L, BSize, false).X);
	}
	const float Step = FVDrawImpl::LineStep(BSize, 1.05f);
	const float TitleH = TipTitle.IsEmpty() ? 0.f : TSz.Y;
	const float BodyH = Lines.Num() * Step;
	const float W = FMath::Min(FV::VW - 8.f, FMath::Max(TSz.X, BodyW) + Pad * 2.f);
	const float H = Pad * 2.f + TitleH + ((TitleH > 0.f && BodyH > 0.f) ? 3.f : 0.f) + BodyH - 1.f;

	const FVector2D M = Mouse();
	float X = M.X + 10.f;
	float Y = M.Y + 12.f;
	if (FV::IsTouchUI())
	{
		// A finger covers what is under it: show the tip centered above the touch point.
		X = M.X - W * 0.5f;
		Y = M.Y - H - 26.f;
		if (Y < 3.f) { Y = M.Y + 30.f; }
	}
	if (X + W > FV::VW - 3.f) { X = M.X - W - 6.f; }
	if (Y + H > FV::VH - 3.f) { Y = M.Y - H - 6.f; }
	X = FMath::Clamp(X, 3.f, FMath::Max(3.f, FV::VW - 3.f - W));
	Y = FMath::Clamp(Y, 3.f, FMath::Max(3.f, FV::VH - 3.f - H));
	X = FMath::RoundToFloat(X);
	Y = FMath::RoundToFloat(Y);

	Panel(FFVRect(X, Y, W, H), EFVPanel::Tooltip);
	float CY = Y + Pad - 1.f;
	if (!TipTitle.IsEmpty())
	{
		Text(TipTitle, FVector2D(X + Pad, CY), TSize, TipColor, EFVAlign::Left, TipColor.GetLuminance() < 0.12f, true);
		CY += TitleH;
		if (!Lines.IsEmpty())
		{
			Rect(FFVRect(X + Pad, CY + 0.5f, W - Pad * 2.f, 1.f), FV::Col(226, 176, 110, 180));
			CY += 3.f;
		}
	}
	for (const FString& L : Lines)
	{
		if (!L.IsEmpty())
		{
			Text(L, FVector2D(X + Pad, CY), BSize, FVPal::InkSoft(), EFVAlign::Left, false, false);
		}
		CY += Step;
	}
}

void FVDraw::MarkUI(const FFVRect& R)
{
	if (bInputBlocked || !InputPtr || !InputPtr->bMouseValid)
	{
		return;
	}
	if (R.Contains(Mouse()))
	{
		bUIHovered = true;
	}
}

// ============================================================================================
// Extra widgets
// ============================================================================================
bool FVDraw::Slider(const FFVRect& R, float& Value01)
{
	const uint32 Key = FVDrawImpl::RectKey(R, 0x511DE);
	// Portrait / finger-sized sliders (R.H > 12): the hit area is at least FV::Ui::ButtonH() (36 px) tall.
	const float HitH = R.H > 12.f ? FMath::Max(R.H + 6.f, FV::Ui::ButtonH()) : R.H + 6.f;
	const float HitPadX = R.H > 12.f ? 10.f : 5.f;
	const FFVRect Hit(R.X - HitPadX, FMath::RoundToFloat(R.Y + R.H * 0.5f - HitH * 0.5f), R.W + 2.f * HitPadX, HitH);
	const float Old = Value01;

	if (PressedIn(Hit))
	{
		ActiveDragKey = Key;
		ClaimDrag(Key);
		FVDrawImpl::PlaySfx(EFVSfx::Click, 0.6f);
	}
	const bool bDragging = ActiveDragKey == Key;
	if (bDragging)
	{
		if (!bInputBlocked && InputPtr && InputPtr->bLmbDown && R.W > 0.f)
		{
			Value01 = FMath::Clamp((Mouse().X - R.X) / R.W, 0.f, 1.f);
			bUIHovered = true;
		}
		else
		{
			ActiveDragKey = 0;
		}
	}
	Value01 = FMath::Clamp(Value01, 0.f, 1.f);
	const bool bHover = Hover(Hit);

	// groove. A taller slider (finger-sized, portrait) gets a thicker groove and a bigger knob;
	// R.H <= 12 keeps the classic 5 px groove and the 8x12 knob.
	const float CY = R.Y + R.H * 0.5f;
	const bool bBig = R.H > 12.f;
	const float GH = bBig ? 7.f : 5.f;
	Rect(FFVRect(R.X, CY - GH * 0.5f, R.W, GH), FVPal::WoodDark());
	Rect(FFVRect(R.X + 1.f, CY - GH * 0.5f + 1.f, R.W - 2.f, GH - 2.f), FV::Col(120, 70, 40));
	Rect(FFVRect(R.X + 1.f, CY - GH * 0.5f + 1.f, (R.W - 2.f) * Value01, GH - 2.f), FVPal::Green());
	Rect(FFVRect(R.X + 1.f, CY - GH * 0.5f + 1.f, (R.W - 2.f) * Value01, 1.f), FV::Col(170, 226, 110));
	// tick marks
	for (int32 i = 1; i < 4; ++i)
	{
		Rect(FFVRect(R.X + R.W * i / 4.f, CY + GH * 0.5f + 0.5f, 1.f, 2.f), FV::Alpha(FVPal::InkSoft(), 0.6f));
	}
	// knob
	const float KH = bBig ? FMath::RoundToFloat(R.H) : 12.f;
	const float KW = bBig ? FMath::RoundToFloat(KH * 0.6f) : 8.f;
	const float KX = FMath::RoundToFloat(R.X + R.W * Value01 - KW * 0.5f);
	const FFVRect K(KX, bBig ? FMath::RoundToFloat(CY - KH * 0.5f) : CY - 6.f, KW, KH);
	Rect(K.Offset(1.f, 1.f), FV::Col(0, 0, 0, 60));
	Rect(K, FVPal::WoodDark());
	Rect(K.Inset(1.f), (bHover || bDragging) ? FV::Col(250, 170, 90) : FVPal::WoodLight());
	Rect(FFVRect(K.X + 1.f, K.Y + 1.f, K.W - 2.f, 1.f), FV::Col(255, 214, 150));
	if (bBig)
	{
		// grip lines
		const float GX = K.X + FMath::RoundToFloat(KW * 0.5f) - 2.f;
		for (int32 g = 0; g < 3; ++g)
		{
			Rect(FFVRect(GX, K.Y + FMath::RoundToFloat(KH * 0.3f) + g * 3.f, 4.f, 1.f), FV::Col(140, 70, 30));
		}
	}
	else
	{
		Rect(FFVRect(K.X + 3.f, K.Y + 4.f, 2.f, 4.f), FV::Col(140, 70, 30));
	}

	if (bHover && !bDragging)
	{
		NoteHover(Key);
	}
	return !FMath::IsNearlyEqual(Old, Value01);
}

bool FVDraw::Checkbox(const FFVRect& R, bool& bValue, const FString& Label, float TextSize)
{
	const bool bHover = Hover(R);
	const float B = FMath::Min(R.H, FV::IsPortrait() ? 20.f : 12.f);
	const FFVRect Box(R.X, FMath::RoundToFloat(R.Y + (R.H - B) * 0.5f), B, B);
	Rect(Box.Offset(1.f, 1.f), FV::Col(0, 0, 0, 50));
	Rect(Box, FVPal::WoodDark());
	Rect(Box.Inset(1.f), bHover ? FV::Col(255, 236, 196) : FV::Col(255, 222, 158));
	Rect(FFVRect(Box.X + 1.f, Box.Y + 1.f, B - 2.f, 1.f), FV::Col(214, 150, 88));
	if (bValue)
	{
		// pixel check mark
		const float S = B / 12.f;
		const FLinearColor CK = FV::Col(60, 140, 40);
		const float X0 = Box.X + 2.5f * S, Y0 = Box.Y + 5.5f * S;
		for (int32 i = 0; i < 3; ++i) { Rect(FFVRect(X0 + i * S, Y0 + i * S, 1.6f * S, 1.6f * S), CK); }
		for (int32 i = 0; i < 5; ++i) { Rect(FFVRect(X0 + (3 + i) * S, Y0 + (2 - i) * S, 1.6f * S, 1.6f * S), CK); }
	}
	if (!Label.IsEmpty())
	{
		const FVector2D Sz = MeasureText(Label, TextSize, true);
		Text(Label, FVector2D(Box.Right() + 5.f, FMath::RoundToFloat(R.Y + (R.H - Sz.Y) * 0.5f)), TextSize, FVPal::Ink(), EFVAlign::Left, false, true);
	}
	if (bHover)
	{
		NoteHover(FVDrawImpl::RectKey(R, 0xC4EC));
	}
	if (Clicked(R))
	{
		bValue = !bValue;
		FVDrawImpl::PlaySfx(EFVSfx::Click);
		return true;
	}
	return false;
}

void FVDraw::Divider(const FVector2D& Pos, float Width)
{
	Rect(FFVRect(Pos.X, Pos.Y, Width, 1.f), FV::Col(214, 156, 94));
	Rect(FFVRect(Pos.X, Pos.Y + 1.f, Width, 1.f), FV::Col(255, 236, 196));
}

// ============================================================================================
// Portrait window layout helpers
// ============================================================================================
FFVRect FVDraw::ActionBarRect(const FFVRect& WindowR)
{
	const float BH = FV::Ui::ButtonH();
	const float Pad = 10.f;   // 5 px wooden frame + 5 px parchment margin
	return FFVRect(WindowR.X + Pad, FMath::RoundToFloat(WindowR.Bottom() - Pad - BH), WindowR.W - 2.f * Pad, BH);
}

bool FVDraw::ActionBar(const FFVRect& WindowR, FFVRect* OutActions, const FString& BackLabel)
{
	const FFVRect Bar = ActionBarRect(WindowR);
	// Footer band: a slightly darker parchment strip behind the buttons with an engraved divider above it.
	const float BandY = Bar.Y - 7.f;
	Rect(FFVRect(WindowR.X + 5.f, BandY, WindowR.W - 10.f, WindowR.Bottom() - 5.f - BandY), FV::Alpha(FVPal::ParchDark(), 0.35f));
	Divider(FVector2D(WindowR.X + 6.f, BandY - 1.f), WindowR.W - 12.f);
	MarkUI(FFVRect(Bar.X, BandY, Bar.W, Bar.Bottom() - BandY));
	const float BackW = FMath::RoundToFloat(FMath::Clamp(Bar.W * 0.34f, 90.f, 120.f));
	if (OutActions)
	{
		*OutActions = FFVRect(Bar.X + BackW + 8.f, Bar.Y, FMath::Max(0.f, Bar.W - BackW - 8.f), Bar.H);
	}
	return BackButton(FFVRect(Bar.X, Bar.Y, BackW, Bar.H), BackLabel);
}

bool FVDraw::BackButton(const FFVRect& R, const FString& Label, float TextSize)
{
	float TS = TextSize > 0.f ? TextSize : FV::Ui::Body() + 1.f;
	const bool bDown = Hover(R) && Input().bLmbDown;
	const bool bClicked = Button(R, FString(), EFVButton::Normal, true, TS);
	const float DY = bDown ? 1.f : 0.f;

	// "← Label" centered as a group; the arrow is pixel art (crisp at any scale, no font glyph needed)
	const float HeadL = FMath::Max(3.f, FMath::RoundToFloat(TS * 0.45f));
	const float ArrowW = FMath::RoundToFloat(HeadL * 2.4f);
	const float Gap = FMath::RoundToFloat(FMath::Max(3.f, TS * 0.45f));
	FVector2D Sz = MeasureText(Label, TS, true);
	while (!Label.IsEmpty() && ArrowW + Gap + Sz.X > R.W - 10.f && TS > 6.f)
	{
		TS -= 0.5f;
		Sz = MeasureText(Label, TS, true);
	}
	const float Total = Label.IsEmpty() ? ArrowW : ArrowW + Gap + Sz.X;
	const float X0 = FMath::RoundToFloat(R.X + (R.W - Total) * 0.5f);
	const float CY = FMath::RoundToFloat(R.Y + R.H * 0.5f - 0.5f + DY);
	const float Thick = HeadL >= 5.f ? 3.f : 2.f;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		// pass 0: warm light-brown drop shadow (same as the ink text shadow), pass 1: ink
		const FLinearColor C = Pass == 0 ? FV::Col(222, 156, 92, 200) : FVPal::Ink();
		const float O = Pass == 0 ? 1.f : 0.f;
		for (int32 i = 0; i < int32(HeadL); ++i)
		{
			const float ColH = 2.f * i + (Thick >= 3.f ? 3.f : 2.f);
			Rect(FFVRect(X0 + i + O, CY - FMath::FloorToFloat(ColH * 0.5f) + O, 1.f, ColH), C);
		}
		Rect(FFVRect(X0 + HeadL + O, CY - FMath::FloorToFloat(Thick * 0.5f) + O, ArrowW - HeadL, Thick), C);
	}
	if (!Label.IsEmpty())
	{
		Text(Label, FVector2D(X0 + ArrowW + Gap, FMath::RoundToFloat(R.Y + (R.H - Sz.Y) * 0.5f - 0.5f + DY)), TS, FVPal::Ink(), EFVAlign::Left, true, true);
	}
	return bClicked;
}

FFVRect FVDraw::SplitH(const FFVRect& Area, int32 Index, int32 Count, float Gap)
{
	Count = FMath::Max(1, Count);
	Index = FMath::Clamp(Index, 0, Count - 1);
	const float CellW = (Area.W - Gap * float(Count - 1)) / float(Count);
	const float X0 = FMath::RoundToFloat(Area.X + Index * (CellW + Gap));
	const float X1 = (Index == Count - 1) ? Area.Right() : FMath::RoundToFloat(Area.X + Index * (CellW + Gap) + CellW);
	return FFVRect(X0, Area.Y, FMath::Max(0.f, X1 - X0), Area.H);
}

FFVRect FVDraw::ScreenRect() const
{
	if (!Canvas || Scale <= 0.f)
	{
		return FFVRect(0.f, 0.f, FV::VW, FV::VH);
	}
	const FVector2D A = ToVirtual(FVector2D(0.f, 0.f));
	const FVector2D B = ToVirtual(FVector2D(Canvas->ClipX, Canvas->ClipY));
	const float X0 = FMath::Min(0.f, FMath::FloorToFloat(A.X));
	const float Y0 = FMath::Min(0.f, FMath::FloorToFloat(A.Y));
	const float X1 = FMath::Max(FV::VW, FMath::CeilToFloat(B.X));
	const float Y1 = FMath::Max(FV::VH, FMath::CeilToFloat(B.Y));
	return FFVRect(X0, Y0, X1 - X0, Y1 - Y0);
}
