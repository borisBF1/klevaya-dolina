// Pixel-art core: FFVImage CPU raster ops, transient texture creation, sprite registry/cache, UI icons,
// and the debug contact sheet. Item art lives in FVArtItems.cpp, character/cosmetics/pets in FVArtCharacter.cpp.

#include "FVSprites.h"
#include "FVDatabase.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	UFVSprites* GFVSpritesInstance = nullptr;

	/** How many cache entries we pre-reserve so that references returned by the getters stay valid
	    (TMap element storage does not move while Num() stays below the reserved capacity). */
	constexpr int32 CacheReserve = 8192;
	constexpr int32 NamedReserve = 1024;

	uint8 ToByte(float V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V), 0, 255); }
}

// ============================================================================================
// FVArt color helpers
// ============================================================================================
FColor FVArt::Mix(FColor A, FColor B, float T)
{
	T = FMath::Clamp(T, 0.f, 1.f);
	return FColor(
		ToByte(A.R + (B.R - A.R) * T),
		ToByte(A.G + (B.G - A.G) * T),
		ToByte(A.B + (B.B - A.B) * T),
		ToByte(A.A + (B.A - A.A) * T));
}

FColor FVArt::Shade(FColor C, float Amount)
{
	const uint8 Alpha = C.A;
	FColor Out = Mix(C, FColor(40, 24, 62, Alpha), FMath::Clamp(Amount, 0.f, 1.f) * 0.72f);
	Out.A = Alpha;
	return Out;
}

FColor FVArt::Light(FColor C, float Amount)
{
	const uint8 Alpha = C.A;
	FColor Out = Mix(C, FColor(255, 250, 226, Alpha), FMath::Clamp(Amount, 0.f, 1.f) * 0.85f);
	Out.A = Alpha;
	return Out;
}

FColor FVArt::OutlineOf(FColor C)
{
	FColor Out = Mix(C, FColor(34, 20, 38), 0.74f);
	Out.A = 255;
	return Out;
}

float FVArt::Noise(int32 X, int32 Y, uint32 Seed)
{
	uint32 H = uint32(X) * 374761393u + uint32(Y) * 668265263u + Seed * 2246822519u;
	H = (H ^ (H >> 13)) * 1274126177u;
	H ^= (H >> 16);
	return float(H & 0xFFFFFF) / float(0x1000000);
}

// ============================================================================================
// FFVImage
// ============================================================================================
void FFVImage::Init(int32 InW, int32 InH, FColor Fill)
{
	W = FMath::Max(0, InW);
	H = FMath::Max(0, InH);
	Px.Init(Fill, W * H);
}

void FFVImage::Blend(int32 X, int32 Y, FColor C)
{
	if (!InBounds(X, Y) || C.A == 0)
	{
		return;
	}
	FColor& D = Px[Y * W + X];
	if (C.A == 255 || D.A == 0)
	{
		D = C;
		return;
	}
	const float SA = C.A / 255.f;
	const float DA = D.A / 255.f;
	const float OA = SA + DA * (1.f - SA);
	if (OA <= 0.f)
	{
		D = FColor(0, 0, 0, 0);
		return;
	}
	const float K = DA * (1.f - SA);
	D = FColor(
		ToByte((C.R * SA + D.R * K) / OA),
		ToByte((C.G * SA + D.G * K) / OA),
		ToByte((C.B * SA + D.B * K) / OA),
		ToByte(OA * 255.f));
}

void FFVImage::FillRect(int32 X, int32 Y, int32 RW, int32 RH, FColor C, bool bBlend)
{
	const int32 X0 = FMath::Max(0, X), Y0 = FMath::Max(0, Y);
	const int32 X1 = FMath::Min(W, X + RW), Y1 = FMath::Min(H, Y + RH);
	for (int32 YY = Y0; YY < Y1; ++YY)
	{
		for (int32 XX = X0; XX < X1; ++XX)
		{
			if (bBlend) { Blend(XX, YY, C); }
			else { Px[YY * W + XX] = C; }
		}
	}
}

void FFVImage::FillCircle(int32 CX, int32 CY, int32 Radius, FColor C, bool bBlend)
{
	if (Radius < 0)
	{
		return;
	}
	const int32 R2 = Radius * Radius + Radius;   // (r + 0.5)^2 rounded: nicer pixel circles
	for (int32 DY = -Radius; DY <= Radius; ++DY)
	{
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			if (DX * DX + DY * DY <= R2)
			{
				if (bBlend) { Blend(CX + DX, CY + DY, C); }
				else { Set(CX + DX, CY + DY, C); }
			}
		}
	}
}

void FFVImage::Line(int32 X0, int32 Y0, int32 X1, int32 Y1, FColor C)
{
	const int32 DX = FMath::Abs(X1 - X0), SX = X0 < X1 ? 1 : -1;
	const int32 DY = -FMath::Abs(Y1 - Y0), SY = Y0 < Y1 ? 1 : -1;
	int32 Err = DX + DY;
	int32 Guard = 0;
	while (Guard++ < 4096)
	{
		Blend(X0, Y0, C);
		if (X0 == X1 && Y0 == Y1)
		{
			break;
		}
		const int32 E2 = 2 * Err;
		if (E2 >= DY) { Err += DY; X0 += SX; }
		if (E2 <= DX) { Err += DX; Y0 += SY; }
	}
}

void FFVImage::Blit(const FFVImage& Src, int32 DX, int32 DY, bool bFlipX)
{
	if (!Src.IsValid())
	{
		return;
	}
	for (int32 Y = 0; Y < Src.H; ++Y)
	{
		const int32 TY = DY + Y;
		if (TY < 0 || TY >= H)
		{
			continue;
		}
		for (int32 X = 0; X < Src.W; ++X)
		{
			const FColor C = Src.Px[Y * Src.W + (bFlipX ? (Src.W - 1 - X) : X)];
			if (C.A != 0)
			{
				Blend(DX + X, TY, C);
			}
		}
	}
}

void FFVImage::Outline(FColor C)
{
	if (!IsValid())
	{
		return;
	}
	const TArray<FColor> Src = Px;
	auto Opaque = [&](int32 X, int32 Y)
	{
		return X >= 0 && Y >= 0 && X < W && Y < H && Src[Y * W + X].A > 0;
	};
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			if (Src[Y * W + X].A != 0)
			{
				continue;
			}
			if (Opaque(X - 1, Y) || Opaque(X + 1, Y) || Opaque(X, Y - 1) || Opaque(X, Y + 1))
			{
				Px[Y * W + X] = C;
			}
		}
	}
}

void FFVImage::Tint(FColor Mul)
{
	for (FColor& P : Px)
	{
		P.R = uint8((uint32(P.R) * Mul.R + 127) / 255);
		P.G = uint8((uint32(P.G) * Mul.G + 127) / 255);
		P.B = uint8((uint32(P.B) * Mul.B + 127) / 255);
	}
}

FFVImage FFVImage::FlippedX() const
{
	FFVImage Out(W, H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			Out.Px[Y * W + X] = Px[Y * W + (W - 1 - X)];
		}
	}
	return Out;
}

FFVImage FFVImage::Cropped(int32 X, int32 Y, int32 CW, int32 CH) const
{
	FFVImage Out(FMath::Max(0, CW), FMath::Max(0, CH));
	for (int32 YY = 0; YY < Out.H; ++YY)
	{
		for (int32 XX = 0; XX < Out.W; ++XX)
		{
			Out.Px[YY * Out.W + XX] = Get(X + XX, Y + YY);
		}
	}
	return Out;
}

FFVImage FFVImage::FromAscii(const TArray<FString>& Rows, const TMap<TCHAR, FColor>& Palette)
{
	int32 MaxW = 0;
	for (const FString& R : Rows)
	{
		MaxW = FMath::Max(MaxW, R.Len());
	}
	FFVImage Img(MaxW, Rows.Num());
	for (int32 Y = 0; Y < Rows.Num(); ++Y)
	{
		const FString& R = Rows[Y];
		for (int32 X = 0; X < R.Len(); ++X)
		{
			const TCHAR Ch = R[X];
			if (Ch == TEXT('.') || Ch == TEXT(' '))
			{
				continue;
			}
			if (const FColor* C = Palette.Find(Ch))
			{
				Img.Px[Y * Img.W + X] = *C;
			}
		}
	}
	return Img;
}

FFVImage FFVImage::FromAscii(std::initializer_list<const TCHAR*> Rows, const TMap<TCHAR, FColor>& Palette)
{
	TArray<FString> Arr;
	Arr.Reserve((int32)Rows.size());
	for (const TCHAR* R : Rows)
	{
		Arr.Add(FString(R ? R : TEXT("")));
	}
	return FromAscii(Arr, Palette);
}

// ============================================================================================
// UFVSprites core
// ============================================================================================
UFVSprites* UFVSprites::Get()
{
	return GFVSpritesInstance;
}

void UFVSprites::Init()
{
	GFVSpritesInstance = this;
	Cache.Reserve(CacheReserve);
	Named.Reserve(NamedReserve);

	if (!Missing.IsValid())
	{
		Missing = MakeSprite(BuildMissing(), TEXT("Missing"));
	}
	for (int32 I = 0; I < int32(EFVIcon::Count); ++I)
	{
		Icon(EFVIcon(I));
	}
	UE_LOG(LogFV, Log, TEXT("FV: sprites initialized (%d icons, %d textures)"), int32(EFVIcon::Count), Textures.Num());
}

void UFVSprites::Shutdown()
{
	Named.Empty();
	Cache.Empty();
	WarnedMissing.Empty();
	Missing = FFVSprite();
	Textures.Empty();
	if (GFVSpritesInstance == this)
	{
		GFVSpritesInstance = nullptr;
	}
}

void UFVSprites::BeginDestroy()
{
	if (GFVSpritesInstance == this)
	{
		GFVSpritesInstance = nullptr;
	}
	UObject::BeginDestroy();
}

UTexture2D* UFVSprites::MakeTexture(const FFVImage& Img, const FString& DebugName)
{
	if (!Img.IsValid())
	{
		UE_LOG(LogFV, Warning, TEXT("FV: MakeTexture('%s') got an empty image"), *DebugName);
		return nullptr;
	}
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(Img.W * Img.H * 4);
	uint8* Dst = Bytes.GetData();
	for (const FColor& C : Img.Px)
	{
		*Dst++ = C.B;
		*Dst++ = C.G;
		*Dst++ = C.R;
		*Dst++ = C.A;
	}
	UTexture2D* Tex = UTexture2D::CreateTransient(Img.W, Img.H, PF_B8G8R8A8, NAME_None,
		TConstArrayView64<uint8>(Bytes.GetData(), Bytes.Num()));
	if (!Tex)
	{
		UE_LOG(LogFV, Warning, TEXT("FV: CreateTransient failed for '%s' (%dx%d)"), *DebugName, Img.W, Img.H);
		return nullptr;
	}
	Tex->Filter = TF_Nearest;
	Tex->SRGB = true;
	Tex->LODGroup = TEXTUREGROUP_Pixels2D;
	Tex->NeverStream = true;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	Tex->UpdateResource();
	Textures.Add(Tex);
	return Tex;
}

FFVSprite UFVSprites::MakeSprite(const FFVImage& Img, const FString& DebugName)
{
	FFVSprite S;
	S.Tex = MakeTexture(Img, DebugName);
	if (!S.Tex)
	{
		// Fall back to the placeholder so callers always get something drawable.
		if (Missing.IsValid())
		{
			return Missing;
		}
		return S;
	}
	S.W = float(Img.W);
	S.H = float(Img.H);
	S.UV0 = FVector2D(0, 0);
	S.UV1 = FVector2D(1, 1);
	return S;
}

void UFVSprites::Register(FName Key, const FFVSprite& Sprite)
{
	Named.Add(Key, Sprite);
}

const FFVSprite& UFVSprites::Get(FName Key)
{
	if (const FFVSprite* S = Named.Find(Key))
	{
		return *S;
	}
	if (!WarnedMissing.Contains(Key))
	{
		WarnedMissing.Add(Key);
		UE_LOG(LogFV, Warning, TEXT("FV: sprite '%s' is not registered, using placeholder"), *Key.ToString());
	}
	if (!Missing.IsValid())
	{
		Missing = MakeSprite(BuildMissing(), TEXT("Missing"));
	}
	return Missing;
}

const FFVSprite& UFVSprites::AddCached(uint64 Key, const FFVImage& Img, const FString& DebugName)
{
	if (const FFVSprite* Existing = Cache.Find(Key))
	{
		return *Existing;
	}
	if (!Missing.IsValid() && Img.IsValid())
	{
		Missing = MakeSprite(BuildMissing(), TEXT("Missing"));
	}
	return Cache.Add(Key, MakeSprite(Img, DebugName));
}

const FFVSprite& UFVSprites::Icon(EFVIcon I)
{
	const uint64 Key = MakeKey(FVSpriteCat::Icon, uint32(I));
	if (const FFVSprite* S = Cache.Find(Key))
	{
		return *S;
	}
	return AddCached(Key, BuildIcon(I), FString::Printf(TEXT("Icon_%d"), int32(I)));
}

FFVImage UFVSprites::BuildMissing()
{
	FFVImage Img(16, 16);
	for (int32 Y = 0; Y < 16; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			const bool bA = ((X / 4) + (Y / 4)) % 2 == 0;
			Img.Set(X, Y, bA ? FColor(255, 0, 255) : FColor(24, 0, 24));
		}
	}
	return Img;
}

// ============================================================================================
// UI icons
// ============================================================================================
namespace
{
	const TMap<TCHAR, FColor>& IconPalette()
	{
		// Magic static: initialized once, thread-safe.
		static const TMap<TCHAR, FColor> Pal = []()
		{
			TMap<TCHAR, FColor> P;
			P.Add(TEXT('o'), FVArt::InkOutline());
			P.Add(TEXT('w'), FColor(255, 255, 255));
			P.Add(TEXT('W'), FColor(244, 236, 214));
			P.Add(TEXT('y'), FColor(255, 204, 51));
			P.Add(TEXT('Y'), FColor(255, 242, 150));
			P.Add(TEXT('d'), FColor(222, 146, 34));
			P.Add(TEXT('D'), FColor(164, 96, 30));
			P.Add(TEXT('r'), FColor(220, 58, 58));
			P.Add(TEXT('R'), FColor(248, 128, 112));
			P.Add(TEXT('q'), FColor(152, 34, 48));
			P.Add(TEXT('g'), FColor(106, 190, 48));
			P.Add(TEXT('G'), FColor(178, 234, 112));
			P.Add(TEXT('h'), FColor(50, 124, 56));
			P.Add(TEXT('b'), FColor(72, 142, 230));
			P.Add(TEXT('B'), FColor(156, 208, 250));
			P.Add(TEXT('n'), FColor(44, 68, 146));
			P.Add(TEXT('p'), FColor(178, 94, 232));
			P.Add(TEXT('P'), FColor(228, 172, 255));
			P.Add(TEXT('v'), FColor(106, 46, 158));
			P.Add(TEXT('m'), FColor(166, 88, 38));
			P.Add(TEXT('M'), FColor(218, 134, 60));
			P.Add(TEXT('e'), FColor(106, 54, 30));
			P.Add(TEXT('s'), FColor(198, 204, 218));
			P.Add(TEXT('S'), FColor(242, 245, 252));
			P.Add(TEXT('x'), FColor(136, 140, 160));
			P.Add(TEXT('X'), FColor(90, 92, 116));
			P.Add(TEXT('c'), FColor(249, 218, 160));
			P.Add(TEXT('C'), FColor(224, 174, 108));
			P.Add(TEXT('t'), FColor(58, 172, 170));
			P.Add(TEXT('T'), FColor(140, 224, 210));
			P.Add(TEXT('u'), FColor(30, 108, 122));
			P.Add(TEXT('O'), FColor(244, 142, 44));
			P.Add(TEXT('i'), FColor(242, 122, 162));
			P.Add(TEXT('I'), FColor(255, 192, 212));
			P.Add(TEXT('l'), FColor(194, 240, 255));
			return P;
		}();
		return Pal;
	}

	FFVImage Ascii(std::initializer_list<const TCHAR*> Rows)
	{
		return FFVImage::FromAscii(Rows, IconPalette());
	}

	FFVImage IconStar(FColor Light, FColor Mid, FColor Dark)
	{
		TMap<TCHAR, FColor> Pal;
		Pal.Add(TEXT('o'), FVArt::OutlineOf(Dark));
		Pal.Add(TEXT('L'), Light);
		Pal.Add(TEXT('M'), Mid);
		Pal.Add(TEXT('N'), Dark);
		Pal.Add(TEXT('w'), FColor(255, 255, 255));
		return FFVImage::FromAscii({
			TEXT("...oo..."),
			TEXT("..oLMo.."),
			TEXT("oooLMooo"),
			TEXT("oLLwMMNo"),
			TEXT(".oLMMNo."),
			TEXT(".oMMMNo."),
			TEXT("oMNooNNo"),
			TEXT("ooo..ooo"),
		}, Pal);
	}

	FFVImage IconClock()
	{
		FFVImage Img(16, 16);
		const FColor Out = FVArt::InkOutline();
		Img.FillCircle(8, 8, 7, Out);
		Img.FillCircle(8, 8, 6, FColor(218, 134, 60));
		Img.FillCircle(8, 8, 5, FColor(166, 88, 38));
		Img.FillCircle(8, 8, 4, FColor(250, 240, 214));
		// rim highlight (top-left) and face shade (bottom-right)
		Img.Set(4, 3, FColor(244, 180, 100)); Img.Set(3, 4, FColor(244, 180, 100)); Img.Set(5, 2, FColor(244, 180, 100));
		Img.Set(10, 11, FColor(226, 210, 176)); Img.Set(11, 10, FColor(226, 210, 176)); Img.Set(11, 9, FColor(226, 210, 176));
		// hour marks
		const FColor Mark(150, 110, 80);
		Img.Set(8, 4, Mark); Img.Set(12, 8, Mark); Img.Set(8, 12, Mark); Img.Set(4, 8, Mark);
		// hands: minute up, hour right
		Img.Set(8, 5, Out); Img.Set(8, 6, Out); Img.Set(8, 7, Out);
		Img.Set(9, 8, FColor(200, 50, 50)); Img.Set(10, 8, FColor(200, 50, 50));
		Img.Set(8, 8, Out);
		return Img;
	}

	FFVImage IconMoon()
	{
		FFVImage Img(16, 16);
		const FColor L(255, 246, 190), M(250, 222, 120), D(214, 170, 70);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const float AX = X + 0.5f - 7.5f, AY = Y + 0.5f - 8.0f;
				const float BX = X + 0.5f - 11.0f, BY = Y + 0.5f - 5.5f;
				const bool bA = AX * AX + AY * AY <= 6.3f * 6.3f;
				const bool bB = BX * BX + BY * BY <= 5.6f * 5.6f;
				if (bA && !bB)
				{
					// shade by distance to the inner (cut) edge: inner edge darker, outer-left lighter
					const float Edge = FMath::Sqrt(BX * BX + BY * BY) - 5.6f;
					Img.Set(X, Y, Edge < 1.2f ? D : (AX + AY < -3.f ? L : M));
				}
			}
		}
		// craters
		Img.Set(4, 9, FColor(232, 196, 100)); Img.Set(5, 12, FColor(232, 196, 100)); Img.Set(3, 6, FColor(255, 252, 220));
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	FFVImage IconClose()
	{
		FFVImage Img(16, 16);
		for (int32 Y = 2; Y <= 13; ++Y)
		{
			for (int32 X = 2; X <= 13; ++X)
			{
				const bool bD1 = FMath::Abs(X - Y) <= 1;
				const bool bD2 = FMath::Abs(X + Y - 15) <= 1;
				if (bD1 || bD2)
				{
					// light edge on the upper-left side of each stroke, dark on the lower-right
					const bool bLight = (bD1 && X - Y == -1) || (bD2 && X + Y - 15 == -1);
					const bool bDark = (bD1 && X - Y == 1) || (bD2 && X + Y - 15 == 1);
					Img.Set(X, Y, bLight ? FColor(248, 128, 112) : (bDark ? FColor(160, 36, 48) : FColor(220, 58, 58)));
				}
			}
		}
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	FFVImage IconSun()
	{
		FFVImage Img(16, 16);
		const FColor Ray(255, 170, 40), RayL(255, 214, 90);
		// rays (8 directions)
		for (int32 I = 0; I < 2; ++I)
		{
			Img.Set(7 + I, 0, RayL); Img.Set(7 + I, 1, Ray);
			Img.Set(7 + I, 14, Ray); Img.Set(7 + I, 15, Ray);
			Img.Set(0, 7 + I, RayL); Img.Set(1, 7 + I, Ray);
			Img.Set(14, 7 + I, Ray); Img.Set(15, 7 + I, Ray);
		}
		Img.Set(2, 2, RayL); Img.Set(3, 3, Ray);
		Img.Set(13, 2, Ray); Img.Set(12, 3, Ray);
		Img.Set(2, 13, Ray); Img.Set(3, 12, Ray);
		Img.Set(13, 13, Ray); Img.Set(12, 12, Ray);
		// disc
		FFVImage Disc(16, 16);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				const float DX = X + 0.5f - 8.f, DY = Y + 0.5f - 8.f;
				const float D2 = DX * DX + DY * DY;
				if (D2 <= 4.6f * 4.6f)
				{
					FColor C(255, 208, 58);
					if (DX + DY < -3.5f) { C = FColor(255, 244, 160); }
					else if (DX + DY > 3.8f) { C = FColor(240, 150, 40); }
					Disc.Set(X, Y, C);
				}
			}
		}
		Disc.Set(6, 6, FColor(255, 255, 230));
		Disc.Outline(FColor(170, 84, 30));
		Img.Blit(Disc, 0, 0);
		return Img;
	}
}

// Update 2 procedural icons (named namespace: unity-build safe).
namespace FVSpritesIconImpl
{
	FFVImage IconSnowflake()
	{
		FFVImage Img(16, 16);
		const FColor Ice(194, 240, 255), IceD(120, 190, 240), Core(255, 255, 255);
		const int32 C = 7;
		// axis arms (length 6) with a V branch, diagonal arms (length 4)
		for (int32 I = 1; I <= 6; ++I)
		{
			const FColor Col = I >= 5 ? IceD : Ice;
			Img.Set(C, C - I, Col); Img.Set(C, C + I, Col); Img.Set(C - I, C, Col); Img.Set(C + I, C, Col);
		}
		for (int32 I = 1; I <= 4; ++I)
		{
			const FColor Col = I >= 4 ? IceD : Ice;
			Img.Set(C - I, C - I, Col); Img.Set(C + I, C - I, Col); Img.Set(C - I, C + I, Col); Img.Set(C + I, C + I, Col);
		}
		// V branches near the tips of the axis arms
		Img.Set(C - 1, C - 5, Ice); Img.Set(C + 1, C - 5, Ice);
		Img.Set(C - 1, C + 5, Ice); Img.Set(C + 1, C + 5, Ice);
		Img.Set(C - 5, C - 1, Ice); Img.Set(C - 5, C + 1, Ice);
		Img.Set(C + 5, C - 1, Ice); Img.Set(C + 5, C + 1, Ice);
		Img.Set(C, C, Core);
		Img.Set(C - 1, C, Core); Img.Set(C + 1, C, Core); Img.Set(C, C - 1, Core); Img.Set(C, C + 1, Core);
		Img.Outline(FColor(44, 68, 146));
		return Img;
	}

	FFVImage IconSmoke()
	{
		// Three rising cloud puffs, each outlined on its own so they read as separate clouds.
		FFVImage Img(16, 16);
		auto Puff = [&Img](float CX, float CY, float R, FColor Base)
		{
			FFVImage P(16, 16);
			const FColor Lt = FVArt::Light(Base, 0.5f), Dk = FVArt::Shade(Base, 0.3f);
			const float Bx[3] = { CX, CX - R * 0.62f, CX + R * 0.62f };
			const float By[3] = { CY, CY + R * 0.28f, CY + R * 0.32f };
			const float Br[3] = { R, R * 0.68f, R * 0.62f };
			for (int32 Y = 0; Y < 16; ++Y)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					bool bIn = false;
					for (int32 B = 0; B < 3; ++B)
					{
						const float DX = X + 0.5f - Bx[B], DY = Y + 0.5f - By[B];
						bIn |= DX * DX + DY * DY <= Br[B] * Br[B];
					}
					if (!bIn)
					{
						continue;
					}
					const float S = ((X + 0.5f - CX) + (Y + 0.5f - CY)) / R;
					P.Set(X, Y, S < -0.55f ? Lt : (S > 0.55f ? Dk : Base));
				}
			}
			P.Outline(FVArt::InkOutline());
			Img.Blit(P, 0, 0);
		};
		Puff(10.8f, 3.4f, 2.2f, FColor(226, 226, 236));
		Puff(8.6f, 7.4f, 2.8f, FColor(202, 202, 214));
		Puff(6.2f, 11.4f, 3.2f, FColor(178, 178, 194));
		// embers under the smoke
		Img.Set(3, 15, FColor(255, 170, 50)); Img.Set(10, 15, FColor(255, 120, 40)); Img.Set(6, 15, FColor(255, 220, 120));
		return Img;
	}

	FFVImage IconKnot()
	{
		// Figure-eight knot: two twisted rope rings crossing, with two loose ends.
		FFVImage Img(16, 16);
		const FColor RopeL(246, 222, 166), RopeM(214, 170, 108), RopeD(160, 112, 66);
		auto RopePx = [&](int32 X, int32 Y) -> FColor
		{
			const int32 Ph = ((X + 2 * Y) % 3 + 3) % 3;
			return Ph == 0 ? RopeD : (Ph == 1 ? RopeL : RopeM);
		};
		auto Ring = [&](float CX, float CY, float R, int32 BX0, int32 BY0, int32 BX1, int32 BY1)
		{
			for (int32 Y = BY0; Y <= BY1; ++Y)
			{
				for (int32 X = BX0; X <= BX1; ++X)
				{
					const float DX = X + 0.5f - CX, DY = Y + 0.5f - CY;
					if (FMath::Abs(FMath::Sqrt(DX * DX + DY * DY) - R) <= 0.85f)
					{
						Img.Set(X, Y, RopePx(X, Y));
					}
				}
			}
		};
		Ring(5.6f, 5.6f, 3.4f, 0, 0, 15, 15);
		Ring(10.4f, 10.4f, 3.4f, 0, 0, 15, 15);
		// over-under: the first ring passes over the second at the upper-right crossing
		Ring(5.6f, 5.6f, 3.4f, 7, 4, 10, 8);
		// loose ends
		Img.Set(2, 12, RopeM); Img.Set(1, 13, RopeL); Img.Set(1, 14, RopeD); Img.Set(3, 11, RopeD);
		Img.Set(12, 3, RopeM); Img.Set(13, 2, RopeL); Img.Set(14, 1, RopeD); Img.Set(11, 4, RopeD);
		Img.Outline(FVArt::InkOutline());
		return Img;
	}

	FFVImage IconFood()
	{
		// Crossed fork and knife.
		FFVImage Img(16, 16);
		const FColor MetL(242, 245, 252), MetM(198, 204, 218), MetD(136, 140, 160);
		const FColor WoodL(218, 134, 60), WoodD(166, 88, 38);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				// knife: "/" axis (S = x - y grows to the top-right), across T = x + y (15/16)
				const int32 S = X - Y, T = X + Y;
				if ((T == 15 || T == 16) && S >= -12 && S <= 12)
				{
					FColor C;
					if (S >= 2) { C = (T == 15) ? MetL : MetM; }               // blade
					else if (S >= 0) { C = FColor(255, 204, 51); }              // bolster
					else { C = (T == 15) ? WoodL : WoodD; }                     // handle
					if (S == 12 && T == 16) { continue; }                       // pointed tip
					Img.Set(X, Y, C);
				}
				if (T == 14 && S >= 3 && S <= 11) { Img.Set(X, Y, MetM); }       // blade width (spine side)
			}
		}
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				// fork: "\" axis (A = x + y grows to the bottom-right), across C = x - y
				const int32 A = X + Y, C = X - Y;
				if (A >= 14 && A <= 27 && (C == 0 || C == 1))                   // handle + neck
				{
					Img.Set(X, Y, A >= 16 ? (C == 0 ? WoodL : WoodD) : (C == 0 ? MetL : MetM));
				}
				else if (A >= 10 && A <= 13 && C >= -2 && C <= 3)                // head
				{
					Img.Set(X, Y, C <= 0 ? MetL : MetD);
				}
				else if (A >= 3 && A <= 9 && (C == -2 || C == 0 || C == 2))      // tines
				{
					Img.Set(X, Y, C == -2 ? MetL : MetM);
				}
			}
		}
		Img.Outline(FVArt::InkOutline());
		return Img;
	}
}

// update 6 [ART-ITEMS]: the new icons are drawn in FVArtItemsU6.cpp
namespace FVArtItemsU6
{
	FFVImage BuildIcon(EFVIcon I);
}

FFVImage UFVSprites::BuildIcon(EFVIcon I)
{
	switch (I)
	{
	case EFVIcon::Coin:
		return Ascii({
			TEXT("..oooo.."),
			TEXT(".oYYYyo."),
			TEXT("oYYyyydo"),
			TEXT("oYydDydo"),
			TEXT("oyydDydo"),
			TEXT("oyyyyddo"),
			TEXT(".oyddDo."),
			TEXT("..oooo.."),
		});
	case EFVIcon::StarSilver:
		return IconStar(FColor(250, 252, 255), FColor(200, 208, 224), FColor(130, 138, 164));
	case EFVIcon::StarGold:
		return IconStar(FColor(255, 244, 150), FColor(255, 204, 51), FColor(214, 130, 30));
	case EFVIcon::StarIridium:
		return IconStar(FColor(250, 180, 255), FColor(206, 92, 226), FColor(128, 40, 170));
	case EFVIcon::Lock:
		return Ascii({
			TEXT("................"),
			TEXT(".....oooooo....."),
			TEXT("....oSSSSSso...."),
			TEXT("...oSxooooxso..."),
			TEXT("...oso....oxo..."),
			TEXT("...oso....oxo..."),
			TEXT("..oooooooooooo.."),
			TEXT("..oYYYYYYYYyyo.."),
			TEXT("..oYyyyyyyyydo.."),
			TEXT("..oYyyyooyyydo.."),
			TEXT("..oyyyyooyyydo.."),
			TEXT("..oyyyyyoyyydo.."),
			TEXT("..oyyyyyoyyydo.."),
			TEXT("..oydddddddDDo.."),
			TEXT("..oooooooooooo.."),
			TEXT("................"),
		});
	case EFVIcon::Map:
		return Ascii({
			TEXT("................"),
			TEXT(".oooooooooooooo."),
			TEXT(".oBBBBcCccccCco."),
			TEXT(".oBBBccCcggcCco."),
			TEXT(".oBBcccgggggGco."),
			TEXT(".oBccgGggggghco."),
			TEXT(".occcggggghhCco."),
			TEXT(".orcccghhhCcCco."),
			TEXT(".occrccCcccCcco."),
			TEXT(".occcrcCcccrcro."),
			TEXT(".occcccrrcccrco."),
			TEXT(".occcccCcrcrcro."),
			TEXT(".occcccCcccCcco."),
			TEXT(".oCCCCCCCCCCCCo."),
			TEXT(".oooooooooooooo."),
			TEXT("................"),
		});
	case EFVIcon::Shop:
		return Ascii({
			TEXT("................"),
			TEXT("..oooooooooooo.."),
			TEXT(".orrWWrrWWrrWWo."),
			TEXT("orrWWrrWWrrWWrro"),
			TEXT("oqqsxqqsxqqsxqqo"),
			TEXT(".oqo.oqo.oqo.oo."),
			TEXT("..oMo......oMo.."),
			TEXT("..oMo.oooo.oMo.."),
			TEXT("..oMooyYyyooMo.."),
			TEXT("..oMoyYyyddoMo.."),
			TEXT(".ooooooooooooooo"),
			TEXT(".oMMMMMMMMMMMMmo"),
			TEXT(".ommmmmmmmmmmmeo"),
			TEXT(".omeoooooooooemo"),
			TEXT(".oeo.......oeeo."),
			TEXT(".ooo.......oooo."),
		});
	case EFVIcon::Keepnet:
		return Ascii({
			TEXT("................"),
			TEXT("..oooooooooooo.."),
			TEXT(".oMMMMMMMMMMMmo."),
			TEXT(".omeeeeeeeeeemo."),
			TEXT("..oooooooooooo.."),
			TEXT("..oWxWxWxWxWxo.."),
			TEXT("..oxWxWxWxWxWo.."),
			TEXT("..oWxWTtxWxWxo.."),
			TEXT("..oxWTttuxWxWo.."),
			TEXT("..oWxWtuxWxWxo.."),
			TEXT("...oWxWxWxWxo..."),
			TEXT("...oxWxWxWxWo..."),
			TEXT("....oWxWxWxo...."),
			TEXT(".....oxWxWo....."),
			TEXT("......oooo......"),
			TEXT("................"),
		});
	case EFVIcon::Book:
		return Ascii({
			TEXT("................"),
			TEXT("..ooooooooooooo."),
			TEXT(".ohgGGGGGGGGGgo."),
			TEXT(".ohgggggggggggo."),
			TEXT(".ohggYYYYYYgggo."),
			TEXT(".ohggyyyyyygggo."),
			TEXT(".ohgggggggggggo."),
			TEXT(".ohggggoygggggo."),
			TEXT(".ohgyyyyyyoyggo."),
			TEXT(".ohgyyoyyyyyggo."),
			TEXT(".ohggyyyyoyhggo."),
			TEXT(".ohggggggggggho."),
			TEXT(".ohoooooooooooo."),
			TEXT(".ohWWWWWWWWWWWo."),
			TEXT("..ooooooooooooo."),
			TEXT("................"),
		});
	case EFVIcon::Trophy:
		return Ascii({
			TEXT("................"),
			TEXT("..oooooooooooo.."),
			TEXT("..oYYYYYyyyyyo.."),
			TEXT("oooYyyyyyyyydooo"),
			TEXT("oyoYyyyyyyyydoyo"),
			TEXT("oyoYyyyyyyyydodo"),
			TEXT(".oyoYyyyyyyddod."),
			TEXT("..ooYyyyyyydoo.."),
			TEXT("....oyyyyydo...."),
			TEXT(".....oyyddo....."),
			TEXT("......oydo......"),
			TEXT("......oydo......"),
			TEXT("....ooyyddoo...."),
			TEXT("...oMMMMMMmmo..."),
			TEXT("...oeeeeeeeeo..."),
			TEXT("...oooooooooo..."),
		});
	case EFVIcon::Wardrobe:
		return Ascii({
			TEXT("................"),
			TEXT(".......oo......."),
			TEXT("......o..o......"),
			TEXT(".....oooooo....."),
			TEXT("...ooBBooBBoo..."),
			TEXT("..oBBBBBBBBbbo.."),
			TEXT(".oBBbbBBBBbbbbo."),
			TEXT("oBbbbbbbbbbbbnno"),
			TEXT("ooobbbbbbbbbnooo"),
			TEXT("...obbbbbbbbno.."),
			TEXT("...obbbbbbbbno.."),
			TEXT("...obbbbbbbbno.."),
			TEXT("...obbbbbbbbno.."),
			TEXT("...onnnnnnnnno.."),
			TEXT("...oooooooooooo."),
			TEXT("................"),
		});
	case EFVIcon::Menu:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT(".oooooooooooooo."),
			TEXT(".occcccccccccCo."),
			TEXT(".oCCCCCCCCCCCCo."),
			TEXT(".oooooooooooooo."),
			TEXT("................"),
			TEXT(".oooooooooooooo."),
			TEXT(".occcccccccccCo."),
			TEXT(".oCCCCCCCCCCCCo."),
			TEXT(".oooooooooooooo."),
			TEXT("................"),
			TEXT(".oooooooooooooo."),
			TEXT(".occcccccccccCo."),
			TEXT(".oCCCCCCCCCCCCo."),
			TEXT(".oooooooooooooo."),
		});
	case EFVIcon::Clock:
		return IconClock();
	case EFVIcon::Sun:
		return IconSun();
	case EFVIcon::Rain:
		return Ascii({
			TEXT("................"),
			TEXT("......oooo......"),
			TEXT("....ooSSSSoo...."),
			TEXT("...oSSSSSSSSo..."),
			TEXT(".ooSSSSSSSSSSoo."),
			TEXT("oSSSSSSSSSSSSSso"),
			TEXT("oSSsSSSSSSSSsssx"),
			TEXT("osssssssssssssxo"),
			TEXT(".oxxxxxxxxxxxxo."),
			TEXT("..oooooooooooo.."),
			TEXT("...ob...ob...ob."),
			TEXT("..ob...ob...ob.."),
			TEXT("..o...oo...oo..."),
			TEXT(".ob...ob...o...."),
			TEXT("ob...ob...ob...."),
			TEXT("o....o....o....."),
		});
	case EFVIcon::Moon:
		return IconMoon();
	case EFVIcon::Xp:
		return Ascii({
			TEXT("................"),
			TEXT(".......oo......."),
			TEXT(".......oo......."),
			TEXT("......oGgo......"),
			TEXT("......oGgo......"),
			TEXT(".....oGGggo....."),
			TEXT("..oooGGwggooo..."),
			TEXT("ooGGGGwwggggghoo"),
			TEXT("oohggggggggghhoo"),
			TEXT("..oooggggghooo.."),
			TEXT(".....oggghho...."),
			TEXT("......oghho....."),
			TEXT("......oghho....."),
			TEXT(".......oho......"),
			TEXT(".......oo......."),
			TEXT("................"),
		});
	case EFVIcon::Treasure:
		return BuildChest(0);
	case EFVIcon::Check:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("............ooo."),
			TEXT("...........oGgo."),
			TEXT("..........oGgho."),
			TEXT(".........oGgho.."),
			TEXT(".ooo....oGgho..."),
			TEXT("oGGgo..oGgho...."),
			TEXT("ohggGooGgho....."),
			TEXT(".ohggGGgho......"),
			TEXT("..ohggggho......"),
			TEXT("...ohggho......."),
			TEXT("....ohho........"),
			TEXT(".....oo........."),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVIcon::Exclaim:
		return Ascii({
			TEXT("......oooo......"),
			TEXT(".....oYYYyo....."),
			TEXT(".....oYwyyo....."),
			TEXT(".....oYyydo....."),
			TEXT(".....oYyydo....."),
			TEXT("......oyydo....."),
			TEXT("......oyydo....."),
			TEXT("......oyydo....."),
			TEXT("......oyddo....."),
			TEXT(".......odo......"),
			TEXT(".......oo......."),
			TEXT("......oooo......"),
			TEXT(".....oYyydo....."),
			TEXT(".....oyyddo....."),
			TEXT("......oooo......"),
			TEXT("................"),
		});
	case EFVIcon::Question:
		return Ascii({
			TEXT("....oooooo......"),
			TEXT("...oYYYyyyo....."),
			TEXT("..oYwyoooyydo..."),
			TEXT("..oYyo...oyydo.."),
			TEXT("..oooo...oyydo.."),
			TEXT("........oyydo..."),
			TEXT(".......oyydo...."),
			TEXT("......oyydo....."),
			TEXT("......oyydo....."),
			TEXT("......oyddo....."),
			TEXT(".......oooo....."),
			TEXT("......oooo......"),
			TEXT(".....oYyydo....."),
			TEXT(".....oyyddo....."),
			TEXT("......oooo......"),
			TEXT("................"),
		});
	case EFVIcon::ArrowRight:
	case EFVIcon::ArrowLeft:
	{
		FFVImage Img = Ascii({
			TEXT("................"),
			TEXT("........o......."),
			TEXT("........oo......"),
			TEXT("........oYo....."),
			TEXT("........oYMo...."),
			TEXT(".ooooooooYMMo..."),
			TEXT(".oYYYYYYYMMMMo.."),
			TEXT(".oYMMMMMMMMMMMo."),
			TEXT(".oMmmmmmmmmmmmo."),
			TEXT(".ommmmmmmmmmmo.."),
			TEXT(".oooooooommmo..."),
			TEXT("........omeo...."),
			TEXT("........oeo....."),
			TEXT("........oo......"),
			TEXT("........o......."),
			TEXT("................"),
		});
		return I == EFVIcon::ArrowLeft ? Img.FlippedX() : Img;
	}
	case EFVIcon::Heart:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("..oooo....oooo.."),
			TEXT(".oRRrro..oRrrro."),
			TEXT("oRwRrrrooRrrrrqo"),
			TEXT("oRRrrrrrrrrrrrqo"),
			TEXT("oRrrrrrrrrrrrrqo"),
			TEXT("orrrrrrrrrrrrqqo"),
			TEXT(".orrrrrrrrrrqqo."),
			TEXT("..orrrrrrrrqqo.."),
			TEXT("...orrrrrrqqo..."),
			TEXT("....orrrrqqo...."),
			TEXT(".....orrqqo....."),
			TEXT("......orqo......"),
			TEXT(".......oo......."),
			TEXT("................"),
		});
	case EFVIcon::Close:
		return IconClose();
	case EFVIcon::Cursor:
		return Ascii({
			TEXT("oo.........."),
			TEXT("oWo........."),
			TEXT("oWWo........"),
			TEXT(".oWWo......."),
			TEXT("..oWWoooo..."),
			TEXT("...oWWxWxo.."),
			TEXT("..ooWWWxWxo."),
			TEXT(".oWWoWWWWWxo"),
			TEXT(".oWWWWWWWWso"),
			TEXT("..oWWWWWWsbo"),
			TEXT("...osWWssbbo"),
			TEXT("....ooooooo."),
		});
	case EFVIcon::Level:
		return Ascii({
			TEXT("................"),
			TEXT(".....oooooo....."),
			TEXT("...ooBBBBbboo..."),
			TEXT("..oBBBBoobbbbo.."),
			TEXT(".oBBBBoYyobbbno."),
			TEXT(".oBBBoYYyyobbno."),
			TEXT("oBBBoYyyyyyobbno"),
			TEXT("oBbooooyyooooono"),
			TEXT("obbbbboyyobbbnno"),
			TEXT("obbbbboyyobbbnno"),
			TEXT(".obbbboyyobbnno."),
			TEXT(".obbbboddobnnno."),
			TEXT("..onbboooobnno.."),
			TEXT("...oonnnnnnoo..."),
			TEXT(".....oooooo....."),
			TEXT("................"),
		});
	case EFVIcon::Fish:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("......ooooo....."),
			TEXT(".....oOOOOOo...."),
			TEXT("oo..ooTTTTTToo.."),
			TEXT("oOooTTTTTTTwoTo."),
			TEXT("oOOTTTTTTTTooTTo"),
			TEXT(".oOttttttttttttu"),
			TEXT(".oOttWWWWWWWWtuo"),
			TEXT("oOOoWWWWWWWWuuo."),
			TEXT("oOo.ooWWWWuoo..."),
			TEXT("oo....oOOoo....."),
			TEXT("......ooo......."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVIcon::Bait:
		return BuildBait(EFVBaitStyle::Worm);
	case EFVIcon::Rod:
	{
		FFVRodDef Def;   // default (bamboo) colors
		return BuildRodIcon(Def);
	}
	// ---------------------------------------------------------------- update 2 (16x16)
	case EFVIcon::Backpack:
		return Ascii({
			TEXT("................"),
			TEXT(".....oooooo....."),
			TEXT(".....oMooMo....."),
			TEXT("...oooooooooo..."),
			TEXT("..oMMMMMMMMMMo.."),
			TEXT(".oMMMMMMMMMMmmo."),
			TEXT(".oMmmmmmmmmmmeo."),
			TEXT(".oeeeeoyyoeeeeo."),
			TEXT(".oGgggoydogggho."),
			TEXT(".oGggggggggggho."),
			TEXT(".oGgoooooooogho."),
			TEXT(".oGgoGgggggogho."),
			TEXT(".oGgoggggghogho."),
			TEXT(".ohhhhhhhhhhhho."),
			TEXT("..oooooooooooo.."),
			TEXT("................"),
		});
	case EFVIcon::House:
		return Ascii({
			TEXT("................"),
			TEXT(".......oo......."),
			TEXT("......orro.ooo.."),
			TEXT(".....orRrrooxo.."),
			TEXT("....orRrrrroxo.."),
			TEXT("...orRrrrrrro..."),
			TEXT("..orRrrrrrrrro.."),
			TEXT(".oqqqqqqqqqqqqo."),
			TEXT("..occcccccccco.."),
			TEXT("..ocoooocooooo.."),
			TEXT("..ocoBBocoMmoo.."),
			TEXT("..ocoBbocoMmoo.."),
			TEXT("..ocoooocoMyoo.."),
			TEXT("..occccccoMmoo.."),
			TEXT("..oooooooooooo.."),
			TEXT("................"),
		});
	case EFVIcon::Hammer:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("..oooooooo......"),
			TEXT(".oSSSSSSsxoooo.."),
			TEXT(".oSsssssssxxxXo."),
			TEXT(".oxxxxxxxxXooXo."),
			TEXT("..oooooMmooo.oo."),
			TEXT("......oMmo......"),
			TEXT("......oMmo......"),
			TEXT("......oMmo......"),
			TEXT("......oMmo......"),
			TEXT("......oMmo......"),
			TEXT("......oMmo......"),
			TEXT("......oMeo......"),
			TEXT("......oooo......"),
			TEXT("................"),
		});
	case EFVIcon::Pot:
		return Ascii({
			TEXT(".....W.....W...."),
			TEXT("....W.......W..."),
			TEXT("......oooo......"),
			TEXT("......oYDo......"),
			TEXT("..oooooooooooo.."),
			TEXT("..oYOOOOOOOODo.."),
			TEXT("..oDDDDDDDDDDo.."),
			TEXT("oooooooooooooooo"),
			TEXT("oDoYOOOOOOOODoDo"),
			TEXT("oooYOOOOOOOODooo"),
			TEXT("..oYOOOOOOODDo.."),
			TEXT("..oOOOOOOOODDo.."),
			TEXT("...oDDDDDDDDo..."),
			TEXT("....oooooooo...."),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVIcon::Net:
		return BuildItemArt(EFVItemArt::Net, FColor(200, 180, 130), 2);
	case EFVIcon::Shovel:
		return BuildShovelIcon(2);
	case EFVIcon::Order:
		return Ascii({
			TEXT("................"),
			TEXT("......ooo......."),
			TEXT(".....oRrqo......"),
			TEXT("..oooooqoooooo.."),
			TEXT("..ocWWWWWWWWCo.."),
			TEXT("..ocxxxxxWWWCo.."),
			TEXT("..ocWWWWWWWWCo.."),
			TEXT("..ocxxxWxxxWCo.."),
			TEXT("..ocWWWWWWWWCo.."),
			TEXT("..ocxxxxWWWgCo.."),
			TEXT("..ocWgWWWWgWCo.."),
			TEXT("..ocWWgWWgWWCo.."),
			TEXT("..ocWWWggWWWCo.."),
			TEXT("..oCCCCCCCCCCo.."),
			TEXT("..oooooooooooo.."),
			TEXT("................"),
		});
	case EFVIcon::Snowflake:
		return FVSpritesIconImpl::IconSnowflake();
	case EFVIcon::Radio:
		return Ascii({
			TEXT("................"),
			TEXT("...........o...."),
			TEXT("..........o....."),
			TEXT(".........o......"),
			TEXT("..oooooooooooo.."),
			TEXT(".oMMMMMMMMMMMmo."),
			TEXT(".oMCCCCCCMWWWmo."),
			TEXT(".oMDDDDDDMWrWmo."),
			TEXT(".oMCCCCCCMWWWmo."),
			TEXT(".oMDDDDDDMMMMmo."),
			TEXT(".oMCCCCCCMyMymo."),
			TEXT(".oMDDDDDDMdMdmo."),
			TEXT(".oMMMMMMMMMMMmo."),
			TEXT(".ommmmmmmmmmmeo."),
			TEXT(".oooooooooooooo."),
			TEXT("..oo........oo.."),
		});
	case EFVIcon::Bed:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT("oooo............"),
			TEXT("oMmo............"),
			TEXT("oMmo.........ooo"),
			TEXT("oMmoooooooooooMo"),
			TEXT("oMmoWWWWoRRRRoMo"),
			TEXT("oMmoWWrRRRRRRoMo"),
			TEXT("oMmorrrRwRRRwoMo"),
			TEXT("oMmorrrrrrrrroMo"),
			TEXT("oMmoMMMMMMMMMoMo"),
			TEXT("oMmommmmmmmmmoMo"),
			TEXT("oooooooooooooooo"),
			TEXT("oeo..........oeo"),
			TEXT("ooo..........ooo"),
		});
	case EFVIcon::Smoke:
		return FVSpritesIconImpl::IconSmoke();
	case EFVIcon::Aquarium:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT(".oooooooooooooo."),
			TEXT(".oxxxxxxxxxxxxo."),
			TEXT(".oTTTTTTTTTTTTo."),
			TEXT(".otttttttttwtto."),
			TEXT(".otgtttttttwtto."),
			TEXT(".otgtttOOOttOto."),
			TEXT(".oggttOoOOOOtto."),
			TEXT(".otghttOOOttOto."),
			TEXT(".otghtttttttuto."),
			TEXT(".oCChCCcCCCcCCo."),
			TEXT(".oCCCCCCCCCCCCo."),
			TEXT(".oooooooooooooo."),
			TEXT("..oxo......oxo.."),
			TEXT("..ooo......ooo.."),
		});
	case EFVIcon::Hand:
		return Ascii({
			TEXT(".....oo........."),
			TEXT("..W.ocCo.W......"),
			TEXT(".W..ocCo..W....."),
			TEXT("....ocCo........"),
			TEXT("....ocCo........"),
			TEXT("....ocCoooooo..."),
			TEXT("....ocCocCocCo.."),
			TEXT("..oooccocCocCCo."),
			TEXT(".occoccccccccCo."),
			TEXT(".occcccccccccCo."),
			TEXT("..occccccccccCo."),
			TEXT("...occcccccccCo."),
			TEXT("....occccccCCo.."),
			TEXT(".....oooooooo..."),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVIcon::Door:
		return Ascii({
			TEXT("................"),
			TEXT("...oooooooooo..."),
			TEXT("...oeeeeeeeeo..."),
			TEXT("...oeMmMMmMeo..."),
			TEXT("...oeMooooMeo..."),
			TEXT("...oeMoBBoMeo..."),
			TEXT("...oeMooooMeo..."),
			TEXT("...oeMmMMmMeo..."),
			TEXT("...oeMmMMmMeo..."),
			TEXT("...oeMmMMmyeo..."),
			TEXT("...oeMmMMmMeo..."),
			TEXT("...oeMmMMmMeo..."),
			TEXT("...oeMmMMmMeo..."),
			TEXT("..oxxxxxxxxxxo.."),
			TEXT("..oooooooooooo.."),
			TEXT("................"),
		});
	case EFVIcon::Knot:
		return FVSpritesIconImpl::IconKnot();
	case EFVIcon::Food:
		return FVSpritesIconImpl::IconFood();
	case EFVIcon::Line:
		return BuildItemArt(EFVItemArt::Line, FColor(214, 196, 150), 1);
	case EFVIcon::Float:
		return BuildItemArt(EFVItemArt::FloatFeather, FColor::White, 1);
	// ---------------------------------------------------------------- update 5: legend fight warnings (16x16, read at 1x)
	case EFVIcon::WarnCurrent:   // ▼▼ the current (Sturgeon): two stacked blue chevrons
		return Ascii({
			TEXT("................"),
			TEXT(".ooooooooooooo.."),
			TEXT(".oBBBBBBBBBBBo.."),
			TEXT("..obbbbbbbbbo..."),
			TEXT("...obbbbbbbo...."),
			TEXT("....obbbbbo....."),
			TEXT(".....obbbo......"),
			TEXT("......obo......."),
			TEXT(".ooooooooooooo.."),
			TEXT(".oBBBBBBBBBBBo.."),
			TEXT("..obbbbbbbbbo..."),
			TEXT("...obbbbbbbo...."),
			TEXT("....obbnbbo....."),
			TEXT(".....obnbo......"),
			TEXT("......ono......."),
			TEXT(".......o........"),
		});
	case EFVIcon::WarnEye:   // turning transparent (Crystal trout): an open eye
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
			TEXT(".....oooooo....."),
			TEXT("...ooWWWWWWoo..."),
			TEXT("..oWWWobbboWWo.."),
			TEXT(".oWWWobBBbboWWo."),
			TEXT("oWWWWobBoobbWWWo"),
			TEXT("oWWWWobboobbWWWo"),
			TEXT(".oWWWobbbbboWWo."),
			TEXT("..oWWWobbboWWo.."),
			TEXT("...ooWWWWWWoo..."),
			TEXT(".....oooooo....."),
			TEXT("................"),
			TEXT("................"),
			TEXT("................"),
		});
	case EFVIcon::WarnLantern:   // the lantern (Angler): a glowing ball on the angler's rod, with rays
		return Ascii({
			TEXT(".ooooo.........."),
			TEXT("oxsssxoo........"),
			TEXT("oso..oxxo......."),
			TEXT("oo....oxo......."),
			TEXT(".......oxo......"),
			TEXT("...Y...oxo...Y.."),
			TEXT("......ooooo....."),
			TEXT(".Y...oYYYYyo..Y."),
			TEXT("....oYwwYYyyo..."),
			TEXT("....oYwYYYyyo..."),
			TEXT("....oYYYYyyyo..."),
			TEXT(".Y..oyYyyyydo.Y."),
			TEXT(".....oyyyddo...."),
			TEXT("......ooooo....."),
			TEXT("...Y.......Y...."),
			TEXT("................"),
		});
	case EFVIcon::WarnCloud:   // the fog (Fog serpent): a cloud over drifting fog bands
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("......oooo......"),
			TEXT(".....oSSSSo....."),
			TEXT("..ooosSSSSsoo..."),
			TEXT(".oSSSsSSSssSSo.."),
			TEXT("oSSSSSSSsssSSSo."),
			TEXT("osSSSSSsssssSsso"),
			TEXT("oxssssssssssssxo"),
			TEXT(".oxxxxxxxxxxxxo."),
			TEXT("..oooooooooooo.."),
			TEXT("................"),
			TEXT(".oTTTTTo.oTTTo.."),
			TEXT("..ooooo...ooo..."),
			TEXT("....oTTTTTTTo..."),
			TEXT(".....ooooooo...."),
		});
	case EFVIcon::WarnFlame:   // the eruption (Zhar-ryba): a flame
		return Ascii({
			TEXT("......o........."),
			TEXT(".....oro........"),
			TEXT(".....oRro......."),
			TEXT("....oRrro...o..."),
			TEXT("...oRrOro..oro.."),
			TEXT("...oRrOrro.oRo.."),
			TEXT("..oRrOOOrrooRro."),
			TEXT("..oRrOyOOrrRrro."),
			TEXT(".oRrOyyyOOrrrqo."),
			TEXT(".oRrOyYyyOOrrqo."),
			TEXT(".oRrOyYYyyOrrqo."),
			TEXT(".oRrOyYwYyOrqqo."),
			TEXT("..orOyYYYyOrqo.."),
			TEXT("..oqrOyyyOrqqo.."),
			TEXT("...ooqqqqqqoo..."),
			TEXT(".....oooooo....."),
		});
	case EFVIcon::WarnSpark:   // the blink (Zhar-ryba): a star inside a closing ring
		return Ascii({
			TEXT(".....ooooo......"),
			TEXT("....oyyyyyo....."),
			TEXT("...oyoooooOo...."),
			TEXT("..o.o..o..o.o..."),
			TEXT(".oyo..oyo..oOo.."),
			TEXT("oyo...oYo...oOo."),
			TEXT("oyo.ooYwyoo.oOo."),
			TEXT("oyooyYwwYYdooOo."),
			TEXT("oyo.ooyYyoo.oOo."),
			TEXT("oyo...oYo...oOo."),
			TEXT(".oOo..odo..oOo.."),
			TEXT("..o.o..o..o.o..."),
			TEXT("...oOoooooOo...."),
			TEXT("....oOOOOOo....."),
			TEXT(".....ooooo......"),
			TEXT("................"),
		});
	case EFVIcon::Chest:
		return Ascii({
			TEXT("................"),
			TEXT("................"),
			TEXT("...oooooooooo..."),
			TEXT("..oMMMMMMMMMMo.."),
			TEXT(".oMmmmmmmmmmmmo."),
			TEXT(".oxmmmmmmmmmmxo."),
			TEXT(".oxeeeeeeeeeexo."),
			TEXT(".ooooooyyoooooo."),
			TEXT(".oxMMMoydoMMMxo."),
			TEXT(".oxmmmmoommmmxo."),
			TEXT(".oxmmmmmmmmmmxo."),
			TEXT(".oxmmmmmmmmmmxo."),
			TEXT(".oxeeeeeeeeeexo."),
			TEXT(".oooooooooooooo."),
			TEXT("..oo........oo.."),
			TEXT("................"),
		});
	default:
	{
		FFVImage U6 = FVArtItemsU6::BuildIcon(I);   // update 6: Leaders .. Tensionmeter (FVArtItemsU6.cpp)
		if (U6.IsValid()) { return U6; }
		break;
	}
	}
	return BuildMissing();
}

// ============================================================================================
// Debug contact sheet
// ============================================================================================
namespace
{
	struct FSheetBuilder
	{
		struct FItem { FFVImage Img; int32 X = 0; int32 Y = 0; };
		TArray<FItem> Items;
		int32 MaxW = 1200;
		int32 Gap = 2;
		int32 CurX = 0;
		int32 CurY = 0;
		int32 RowH = 0;
		int32 TotalW = 0;

		void Add(const FFVImage& Img)
		{
			if (!Img.IsValid())
			{
				return;
			}
			if (CurX > 0 && CurX + Img.W > MaxW)
			{
				NewLine(Gap);
			}
			FItem& It = Items.AddDefaulted_GetRef();
			It.Img = Img;
			It.X = CurX + Gap;
			It.Y = CurY + Gap;
			CurX += Img.W + Gap;
			RowH = FMath::Max(RowH, Img.H);
			TotalW = FMath::Max(TotalW, CurX + Gap);
		}
		void NewLine(int32 Extra)
		{
			if (CurX == 0)
			{
				return;
			}
			CurY += RowH + Gap + Extra;
			CurX = 0;
			RowH = 0;
		}
		void Section() { NewLine(6); }
		FFVImage Render(int32 Scale) const
		{
			const int32 TH = CurY + RowH + Gap * 2;
			FFVImage Sheet(FMath::Max(1, TotalW + Gap), FMath::Max(1, TH), FColor(128, 128, 128));
			for (const FItem& It : Items)
			{
				Sheet.Blit(It.Img, It.X, It.Y);
			}
			if (Scale <= 1)
			{
				return Sheet;
			}
			FFVImage Big(Sheet.W * Scale, Sheet.H * Scale);
			for (int32 Y = 0; Y < Big.H; ++Y)
			{
				for (int32 X = 0; X < Big.W; ++X)
				{
					Big.Px[Y * Big.W + X] = Sheet.Px[(Y / Scale) * Sheet.W + (X / Scale)];
				}
			}
			return Big;
		}
	};

	/** Looks up a cosmetic of a slot/style in the database for realistic colors (falls back to defaults). */
	bool FindCosmeticColors(EFVCosmeticSlot Slot, uint8 Style, FColor& OutP, FColor& OutS)
	{
		for (const FFVCosmeticDef& C : FVDB::Cosmetics())
		{
			if (C.Slot == Slot && C.Style == Style)
			{
				OutP = C.Primary;
				OutS = C.Secondary;
				return true;
			}
		}
		return false;
	}
}

void UFVSprites::DebugWriteContactSheet(const FString& FilePath)
{
	FVDB::Init();
	FSheetBuilder SB;

	// --- icons
	for (int32 I = 0; I < int32(EFVIcon::Count); ++I)
	{
		SB.Add(BuildIcon(EFVIcon(I)));
	}
	SB.Add(BuildMissing());
	SB.Section();

	// --- fish, then silhouettes
	for (const FFVFishDef& F : FVDB::Fish())
	{
		SB.Add(BuildFish(F, false));
	}
	SB.NewLine(2);
	for (const FFVFishDef& F : FVDB::Fish())
	{
		SB.Add(BuildFish(F, true));
	}
	SB.Section();

	// --- junk, bait, rods, chest
	for (int32 I = 0; I < int32(EFVJunkStyle::Count); ++I) { SB.Add(BuildJunk(EFVJunkStyle(I))); }
	for (int32 I = 0; I < int32(EFVBaitStyle::Count); ++I) { SB.Add(BuildBait(EFVBaitStyle(I))); }
	for (const FFVRodDef& R : FVDB::Rods()) { SB.Add(BuildRodIcon(R)); }
	SB.Add(BuildChest(0));
	SB.Add(BuildChest(1));
	SB.Section();

	// --- character: 3 appearances, every anim / facing / frame
	TArray<FFVAppearance> Looks;
	{
		FFVAppearance A;
		A.Hat = EFVHatStyle::Straw; A.HatPrimary = FColor(236, 196, 96); A.HatSecondary = FColor(200, 60, 50);
		A.Outfit = EFVOutfitStyle::Overalls; A.OutfitPrimary = FColor(70, 110, 180); A.OutfitSecondary = FColor(236, 226, 206);
		Looks.Add(A);
		FFVAppearance B;
		B.Hat = EFVHatStyle::Cap; B.HatPrimary = FColor(210, 60, 60); B.HatSecondary = FColor(245, 245, 240);
		B.Outfit = EFVOutfitStyle::Hoodie; B.OutfitPrimary = FColor(96, 168, 96); B.OutfitSecondary = FColor(240, 240, 230);
		B.RodColor = FColor(60, 60, 70); B.RodAccent = FColor(200, 60, 50);
		Looks.Add(B);
		FFVAppearance C;
		C.Hat = EFVHatStyle::Wizard; C.HatPrimary = FColor(90, 70, 170); C.HatSecondary = FColor(250, 210, 80);
		C.Outfit = EFVOutfitStyle::Raincoat; C.OutfitPrimary = FColor(242, 196, 58); C.OutfitSecondary = FColor(60, 70, 90);
		C.RodColor = FColor(120, 200, 230); C.RodAccent = FColor(70, 60, 120);
		Looks.Add(C);
	}
	const EFVFacing Facings[4] = { EFVFacing::Down, EFVFacing::Right, EFVFacing::Up, EFVFacing::Left };
	for (const FFVAppearance& L : Looks)
	{
		for (int32 A = 0; A < int32(EFVCharAnim::Count); ++A)
		{
			const EFVCharAnim Anim = EFVCharAnim(A);
			const bool bFishing = Anim != EFVCharAnim::Idle && Anim != EFVCharAnim::Walk;
			for (int32 FI = 0; FI < (bFishing ? 1 : 4); ++FI)
			{
				for (int32 Fr = 0; Fr < CharacterFrameCount(Anim); ++Fr)
				{
					SB.Add(BuildCharacter(L, Anim, Facings[FI], Fr));
				}
			}
		}
		SB.NewLine(2);
	}
	SB.Section();

	// --- every hat and outfit on the character (idle, 4 facings), with database colors when available
	for (int32 H = 0; H < int32(EFVHatStyle::Count); ++H)
	{
		FFVAppearance L;
		L.Hat = EFVHatStyle(H);
		FindCosmeticColors(EFVCosmeticSlot::Hat, uint8(H), L.HatPrimary, L.HatSecondary);
		for (EFVFacing F : Facings) { SB.Add(BuildCharacter(L, EFVCharAnim::Idle, F, 0)); }
	}
	SB.NewLine(2);
	for (int32 O = 0; O < int32(EFVOutfitStyle::Count); ++O)
	{
		FFVAppearance L;
		L.Outfit = EFVOutfitStyle(O);
		FindCosmeticColors(EFVCosmeticSlot::Outfit, uint8(O), L.OutfitPrimary, L.OutfitSecondary);
		for (EFVFacing F : Facings) { SB.Add(BuildCharacter(L, EFVCharAnim::Idle, F, 0)); }
	}
	SB.Section();

	// --- hat & outfit icons: database cosmetics first, then every style with default colors
	for (const FFVCosmeticDef& C : FVDB::Cosmetics())
	{
		if (C.Slot == EFVCosmeticSlot::Hat && C.Style < uint8(EFVHatStyle::Count))
		{
			SB.Add(BuildHatIcon(EFVHatStyle(C.Style), C.Primary, C.Secondary));
		}
	}
	for (const FFVCosmeticDef& C : FVDB::Cosmetics())
	{
		if (C.Slot == EFVCosmeticSlot::Outfit && C.Style < uint8(EFVOutfitStyle::Count))
		{
			SB.Add(BuildOutfitIcon(EFVOutfitStyle(C.Style), C.Primary, C.Secondary));
		}
	}
	SB.NewLine(2);
	for (int32 H = 1; H < int32(EFVHatStyle::Count); ++H)
	{
		SB.Add(BuildHatIcon(EFVHatStyle(H), FColor(200, 80, 60), FColor(250, 220, 120)));
	}
	for (int32 O = 0; O < int32(EFVOutfitStyle::Count); ++O)
	{
		SB.Add(BuildOutfitIcon(EFVOutfitStyle(O), FColor(70, 110, 180), FColor(236, 226, 206)));
	}
	SB.Section();

	// --- bobbers (both frames)
	for (int32 B = 0; B < int32(EFVBobberStyle::Count); ++B)
	{
		SB.Add(BuildBobber(EFVBobberStyle(B), 0));
		SB.Add(BuildBobber(EFVBobberStyle(B), 1));
	}
	SB.Section();

	// --- pets: every anim & frame
	for (int32 P = 1; P < int32(EFVPetStyle::Count); ++P)
	{
		for (int32 A = 0; A < int32(EFVPetAnim::Count); ++A)
		{
			for (int32 Fr = 0; Fr < PetFrameCount(EFVPetAnim(A)); ++Fr)
			{
				SB.Add(BuildPet(EFVPetStyle(P), EFVPetAnim(A), Fr));
			}
		}
		SB.NewLine(2);
	}

	const FFVImage Sheet = SB.Render(2);
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Sheet.W, Sheet.H, TArrayView64<const FColor>(Sheet.Px.GetData(), Sheet.Px.Num()), Png);
	const FString Dir = FPaths::GetPath(FilePath);
	if (!Dir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Dir, true);
	}
	if (Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *FilePath))
	{
		UE_LOG(LogFV, Log, TEXT("FV: contact sheet written: %s (%dx%d)"), *FilePath, Sheet.W, Sheet.H);
	}
	else
	{
		UE_LOG(LogFV, Warning, TEXT("FV: failed to write contact sheet %s"), *FilePath);
	}

	// Update 2 art (item art, lures, shovels, decor, stations, stalls, nets...) goes to a second, labeled sheet.
	DebugAddHomeSheet(FPaths::Combine(Dir, FPaths::GetBaseFilename(FilePath) + TEXT("_Home.png")));
}
