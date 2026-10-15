// Procedural pixel art of the six fishing locations (see FVSceneArt.h).
// Everything is painted into FFVImage (sRGB bytes) at 1 texel = 2 virtual px. Light comes from the top-left,
// outlines are dark tinted (never pure black), dithering is used sparingly for gradients.

#include "FVSceneArt.h"

namespace
{
	using namespace FVSceneArt;

	// =========================================================================================
	// Hashing, noise, color helpers
	// =========================================================================================
	FORCEINLINE uint32 HashU(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16;
		return X;
	}
	FORCEINLINE uint32 Hash3(int32 X, int32 Y, uint32 S)
	{
		return HashU((uint32(X) * 0x9E3779B1U) ^ HashU((uint32(Y) * 0x85EBCA77U) ^ (S * 0xC2B2AE3DU)));
	}
	/** Deterministic white noise 0..1. */
	FORCEINLINE float R01(int32 X, int32 Y, uint32 S) { return float(Hash3(X, Y, S) & 0xFFFFFFU) / 16777216.f; }

	float VNoise(float X, float Y, uint32 S)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float FX = X - float(X0), FY = Y - float(Y0);
		const float SX = FX * FX * (3.f - 2.f * FX), SY = FY * FY * (3.f - 2.f * FY);
		const float A = R01(X0, Y0, S), B = R01(X0 + 1, Y0, S), C = R01(X0, Y0 + 1, S), D = R01(X0 + 1, Y0 + 1, S);
		return FMath::Lerp(FMath::Lerp(A, B, SX), FMath::Lerp(C, D, SX), SY);
	}
	float Fbm(float X, float Y, uint32 S)
	{
		return VNoise(X, Y, S) * 0.6f + VNoise(X * 2.1f, Y * 2.1f, S + 17) * 0.3f + VNoise(X * 4.3f, Y * 4.3f, S + 31) * 0.1f;
	}
	FORCEINLINE float Bayer(int32 X, int32 Y)
	{
		static const uint8 M[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
		return (float(M[Y & 3][X & 3]) + 0.5f) / 16.f;
	}

	FORCEINLINE int32 IFloor(double V) { return int32(FMath::FloorToFloat(float(V))); }
	FORCEINLINE int32 ICeil(double V) { return int32(FMath::CeilToFloat(float(V))); }
	FORCEINLINE int32 IRound(double V) { return int32(FMath::RoundToFloat(float(V))); }

	FORCEINLINE FColor Rgb(int32 R, int32 G, int32 B, int32 A = 255)
	{
		return FColor(uint8(FMath::Clamp(R, 0, 255)), uint8(FMath::Clamp(G, 0, 255)), uint8(FMath::Clamp(B, 0, 255)), uint8(FMath::Clamp(A, 0, 255)));
	}
	FColor Mix(const FColor& A, const FColor& B, float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		auto L = [T](uint8 X, uint8 Y) { return FMath::RoundToInt(float(X) + (float(Y) - float(X)) * T); };
		return Rgb(L(A.R, B.R), L(A.G, B.G), L(A.B, B.B), L(A.A, B.A));
	}
	FColor Mul(const FColor& C, float F)
	{
		return Rgb(FMath::RoundToInt(C.R * F), FMath::RoundToInt(C.G * F), FMath::RoundToInt(C.B * F), C.A);
	}
	FORCEINLINE FColor WithA(FColor C, int32 A) { C.A = uint8(FMath::Clamp(A, 0, 255)); return C; }

	const FColor White = Rgb(255, 255, 255);

	struct FPal4 { FColor Hi, Base, Lo, Deep; };
	FPal4 LeafPal(const FColor& Base)
	{
		return { Mix(Base, Rgb(232, 240, 150), 0.32f), Base, Mix(Base, Rgb(20, 52, 44), 0.32f), Mix(Base, Rgb(16, 36, 36), 0.56f) };
	}
	FPal4 RockPal(const FColor& Base)
	{
		return { Mix(Base, White, 0.28f), Base, Mul(Base, 0.76f), Mul(Base, 0.56f) };
	}

	void MulPx(FFVImage& Img, int32 X, int32 Y, float F)
	{
		if (Img.InBounds(X, Y)) { Img.Set(X, Y, Mul(Img.Get(X, Y), F)); }
	}
	void MixPx(FFVImage& Img, int32 X, int32 Y, const FColor& C, float T)
	{
		if (Img.InBounds(X, Y)) { const FColor O = Img.Get(X, Y); Img.Set(X, Y, WithA(Mix(O, C, T), O.A)); }
	}

	/** Darkens an elliptic area (soft dithered rim). */
	void ShadeEllipse(FFVImage& Img, float CX, float CY, float RX, float RY, float F)
	{
		if (RX <= 0.f || RY <= 0.f) { return; }
		for (int32 Y = FMath::FloorToInt(CY - RY); Y <= FMath::CeilToInt(CY + RY); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
			{
				const float DX = (float(X) + 0.5f - CX) / RX, DY = (float(Y) + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY;
				if (D > 1.f) { continue; }
				if (D > 0.6f && Bayer(X, Y) > (1.f - D) / 0.4f) { continue; }
				MulPx(Img, X, Y, F);
			}
		}
	}

	void FillEllipse(FFVImage& Img, float CX, float CY, float RX, float RY, const FColor& C, bool bBlend = false)
	{
		for (int32 Y = FMath::FloorToInt(CY - RY); Y <= FMath::CeilToInt(CY + RY); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX); X <= FMath::CeilToInt(CX + RX); ++X)
			{
				const float DX = (float(X) + 0.5f - CX) / FMath::Max(RX, 0.5f), DY = (float(Y) + 0.5f - CY) / FMath::Max(RY, 0.5f);
				if (DX * DX + DY * DY <= 1.f)
				{
					if (bBlend) { Img.Blend(X, Y, C); } else { Img.Set(X, Y, C); }
				}
			}
		}
	}

	/** Shaded blob lit from the top-left (leaf clumps, rocks, bushes). */
	void PaintBlob(FFVImage& T, float CX, float CY, float RX, float RY, const FPal4& P, uint32 Seed, float Jag = 0.18f, float Noise = 0.35f, int32 HashDX = 0)
	{
		// HashDX (hotfix 0.61): the hashes use X - HashDX (a composition shifted inside a wider image keeps its pixels)
		RX = FMath::Max(RX, 0.6f); RY = FMath::Max(RY, 0.6f);
		for (int32 Y = FMath::FloorToInt(CY - RY - 1.f); Y <= FMath::CeilToInt(CY + RY + 1.f); ++Y)
		{
			for (int32 X = FMath::FloorToInt(CX - RX - 1.f); X <= FMath::CeilToInt(CX + RX + 1.f); ++X)
			{
				const float DX = (float(X) + 0.5f - CX) / RX, DY = (float(Y) + 0.5f - CY) / RY;
				const float D = DX * DX + DY * DY;
				if (D > 1.f - Jag * R01((X - HashDX) >> 1, Y >> 1, Seed)) { continue; }
				const float L = -(DX * 0.55f + DY * 0.83f) + (R01(X - HashDX, Y, Seed + 7) - 0.5f) * Noise;
				T.Set(X, Y, L > 0.45f ? P.Hi : (L > -0.2f ? P.Base : (L > -0.62f ? P.Lo : P.Deep)));
			}
		}
	}

	/** Lit left edge / shaded right edge for every opaque pixel bordering transparency. */
	void EdgeShade(FFVImage& T, float HiAmt, float LoAmt)
	{
		const FFVImage Src = T;
		for (int32 Y = 0; Y < T.H; ++Y)
		{
			for (int32 X = 0; X < T.W; ++X)
			{
				const FColor C = Src.Get(X, Y);
				if (C.A == 0) { continue; }
				if (Src.Get(X - 1, Y).A == 0 || Src.Get(X, Y - 1).A == 0) { T.Set(X, Y, WithA(Mix(C, White, HiAmt), C.A)); }
				else if (Src.Get(X + 1, Y).A == 0 || Src.Get(X, Y + 1).A == 0) { T.Set(X, Y, Mul(C, 1.f - LoAmt)); }
			}
		}
	}

	/** Thick line made of square stamps. */
	void ThickLine(FFVImage& T, FVector2D A, FVector2D B, float W0, float W1, const FColor& C)
	{
		const float Len = FVector2D::Distance(A, B);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len * 2.f));
		for (int32 i = 0; i <= Steps; ++i)
		{
			const float F = float(i) / float(Steps);
			const FVector2D P = FMath::Lerp(A, B, F);
			const float W = FMath::Lerp(W0, W1, F);
			const int32 H = FMath::Max(0, FMath::RoundToInt(W * 0.5f - 0.5f));
			const int32 PX = IFloor(P.X), PY = IFloor(P.Y);
			for (int32 DY = -H; DY <= H; ++DY)
			{
				for (int32 DX = -H; DX <= H; ++DX) { T.Set(PX + DX, PY + DY, C); }
			}
		}
	}

	struct FPalE { TCHAR Ch; FColor Col; };
	FFVImage Ascii(std::initializer_list<const TCHAR*> Rows, std::initializer_list<FPalE> Pal)
	{
		TMap<TCHAR, FColor> P;
		for (const FPalE& E : Pal) { P.Add(E.Ch, E.Col); }
		return FFVImage::FromAscii(Rows, P);
	}

	// =========================================================================================
	// Objects (painted into their own images, outlined, then placed)
	// =========================================================================================
	const FColor BarkHi = Rgb(150, 104, 64), Bark = Rgb(116, 78, 48), BarkLo = Rgb(84, 54, 34);

	FFVImage MakeOak(FRandomStream& R, const FColor& Leaf, int32 Rad, const FColor* Fruit)
	{
		Rad = FMath::Clamp(Rad, 6, 16);
		const int32 W = FMath::RoundToInt(Rad * 3.3f) + 4;
		const int32 H = FMath::RoundToInt(Rad * 2.8f) + 13;
		FFVImage T(W, H);
		const int32 CX = W / 2, Base = H - 1;
		for (int32 Y = Base - Rad - 6; Y <= Base; ++Y)
		{
			const int32 Flare = FMath::Max(0, Y - (Base - 3));
			const int32 X0 = CX - 3 - Flare, X1 = CX + 2 + Flare;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor C = (X <= X0) ? BarkHi : ((X >= X1 - 1) ? BarkLo : Bark);
				if (R01(X, Y, 77) < 0.12f) { C = BarkLo; }
				T.Set(X, Y, C);
			}
		}
		const FPal4 P = LeafPal(Leaf);
		const float CCY = float(Base - 8) - 1.15f * Rad;
		const float Cl[6][3] = { { 0.f, -0.55f, 0.85f }, { -0.78f, -0.1f, 0.72f }, { 0.78f, -0.1f, 0.72f },
			{ -0.42f, 0.45f, 0.7f }, { 0.45f, 0.45f, 0.7f }, { 0.f, 0.15f, 0.72f } };
		const uint32 Seed = R.GetUnsignedInt();
		for (int32 i = 0; i < 6; ++i)
		{
			const float CXi = float(CX) + Cl[i][0] * Rad + R.FRandRange(-1.f, 1.f);
			const float CYi = CCY + Cl[i][1] * Rad + R.FRandRange(-1.f, 1.f);
			PaintBlob(T, CXi, CYi, Cl[i][2] * Rad, Cl[i][2] * Rad * 0.92f, P, Seed + uint32(i) * 13);
		}
		if (Fruit)
		{
			for (int32 i = 0; i < 7; ++i)
			{
				const int32 FX = CX + R.RandRange(-Rad, Rad), FY = FMath::RoundToInt(CCY) + R.RandRange(-Rad / 2, Rad);
				if (T.Get(FX, FY).A > 0 && T.Get(FX + 1, FY + 1).A > 0)
				{
					T.Set(FX, FY, *Fruit); T.Set(FX + 1, FY, Mul(*Fruit, 0.8f)); T.Set(FX, FY + 1, Mul(*Fruit, 0.8f)); T.Set(FX + 1, FY + 1, Mul(*Fruit, 0.65f));
				}
			}
		}
		T.Outline(Rgb(36, 48, 32));
		return T;
	}

	FFVImage MakePine(FRandomStream& R, int32 H, const FColor& Leaf, bool bSnow)
	{
		H = FMath::Clamp(H, 12, 60);
		const int32 HW = FMath::Max(4, FMath::RoundToInt(H * 0.3f));
		const int32 W = HW * 2 + 3;
		FFVImage T(W, H + 1);
		const int32 CX = W / 2, Base = H;
		for (int32 Y = Base - 4; Y <= Base; ++Y)
		{
			T.Set(CX - 1, Y, BarkHi); T.Set(CX, Y, Bark); T.Set(CX + 1, Y, BarkLo);
		}
		const FPal4 P = LeafPal(Leaf);
		const int32 Tiers = H >= 26 ? 4 : 3;
		const int32 Bot = Base - 4;
		const float Step = float(Bot) / (float(Tiers) + 0.6f);
		const uint32 Seed = R.GetUnsignedInt();
		for (int32 i = 0; i < Tiers; ++i)
		{
			const int32 TB = Bot - FMath::RoundToInt(float(i) * Step);
			const int32 TT = FMath::Max(0, FMath::RoundToInt(float(TB) - Step * 1.7f));
			const float TierHW = float(HW) * (1.f - 0.2f * float(i));
			auto HalfAt = [&](int32 Y) { const float F = float(Y - TT) / float(FMath::Max(1, TB - TT)); return FMath::RoundToInt(0.5f + F * TierHW); };
			for (int32 Y = TT; Y <= TB; ++Y)
			{
				const int32 Hw = HalfAt(Y);
				const int32 HwUp = Y > TT ? HalfAt(Y - 1) : -1;
				for (int32 X = CX - Hw; X <= CX + Hw; ++X)
				{
					if (Y == TB && ((X + i) & 1)) { continue; }
					const float S = float(X - CX) / float(FMath::Max(1, Hw));
					const float V = -S * 0.85f - 0.15f + (R01(X, Y, Seed + uint32(i)) - 0.5f) * 0.45f;
					FColor C = V > 0.3f ? P.Hi : (V > -0.3f ? P.Base : (V > -0.75f ? P.Lo : P.Deep));
					if (bSnow && FMath::Abs(X - CX) >= HwUp - 1 && R01(X, Y, Seed + 99) < 0.8f)
					{
						C = S < 0.25f ? Rgb(246, 250, 255) : Rgb(200, 216, 236);
					}
					T.Set(X, Y, C);
				}
			}
		}
		T.Outline(Rgb(24, 40, 38));
		return T;
	}

	FFVImage MakePalm(FRandomStream& R, bool bFlip)
	{
		const int32 W = 48, H = 60;
		FFVImage T(W, H);
		const int32 BaseX = 20, BaseY = H - 1, CrownY = 15;
		const float Lean = 7.f + R.FRandRange(-1.5f, 1.5f);
		const FColor TrHi = Rgb(206, 166, 110), Tr = Rgb(172, 132, 82), TrLo = Rgb(128, 92, 56);
		for (int32 Y = BaseY; Y >= CrownY; --Y)
		{
			const float Tt = float(BaseY - Y) / float(BaseY - CrownY);
			const int32 XC = FMath::RoundToInt(float(BaseX) + Lean * Tt * Tt);
			const int32 Half = Tt < 0.08f ? 3 : 2;
			const bool bRing = ((BaseY - Y) % 4) == 0;
			for (int32 X = XC - Half; X < XC + Half; ++X)
			{
				FColor C = (X == XC - Half) ? TrHi : ((X == XC + Half - 1) ? TrLo : Tr);
				if (bRing) { C = Mul(C, 0.78f); }
				T.Set(X, Y, C);
			}
		}
		const FVector2D Crown(float(BaseX) + Lean, float(CrownY));
		const FPal4 LP = LeafPal(Rgb(70, 150, 60));
		const float Angles[7] = { -172.f, -145.f, -118.f, -90.f, -62.f, -35.f, -8.f };
		for (int32 k = 0; k < 7; ++k)
		{
			const float A = FMath::DegreesToRadians(Angles[k] + R.FRandRange(-8.f, 8.f));
			FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
			FVector2D Pt = Crown;
			const int32 Len = R.RandRange(13, 18);
			for (int32 s = 0; s < Len; ++s)
			{
				Pt += Dir;
				Dir.Y += 0.1f;
				Dir.Normalize();
				const int32 PX = IRound(Pt.X), PY = IRound(Pt.Y);
				T.Set(PX, PY, LP.Base);
				T.Set(PX, PY - 1, s < Len - 2 ? LP.Hi : LP.Base);
				T.Set(PX, PY + 1, LP.Lo);
				if (s > 1 && (s % 2) == 0)
				{
					const int32 LeafLen = FMath::Max(1, 4 - s / 5);
					for (int32 j = 1; j <= LeafLen; ++j) { T.Set(PX + (Dir.X > 0 ? -j / 2 : j / 2), PY + 1 + j, j == LeafLen ? LP.Deep : LP.Lo); }
				}
			}
		}
		const FColor Nut = Rgb(110, 72, 40);
		FillEllipse(T, float(Crown.X) - 1.5f, float(Crown.Y) + 2.5f, 1.6f, 1.6f, Nut);
		FillEllipse(T, float(Crown.X) + 1.5f, float(Crown.Y) + 3.f, 1.6f, 1.6f, Mul(Nut, 0.85f));
		T.Set(IRound(Crown.X - 2.0), IRound(Crown.Y + 2.0), Rgb(150, 104, 60));
		T.Outline(Rgb(40, 50, 28));
		return bFlip ? T.FlippedX() : T;
	}

	void Branch(FFVImage& T, FRandomStream& R, FVector2D P, float Ang, float Len, float W, int32 Depth, const FColor& C, TArray<FVector2D>* Tips)
	{
		const FVector2D E = P + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Len;
		ThickLine(T, P, E, W, FMath::Max(1.f, W * 0.6f), C);
		if (Depth <= 0) { if (Tips) { Tips->Add(E); } return; }
		Branch(T, R, E, Ang - R.FRandRange(0.35f, 0.7f), Len * 0.65f, FMath::Max(1.f, W * 0.6f), Depth - 1, C, Tips);
		Branch(T, R, E, Ang + R.FRandRange(0.35f, 0.7f), Len * 0.6f, FMath::Max(1.f, W * 0.6f), Depth - 1, C, Tips);
	}

	FFVImage MakeDeadTree(FRandomStream& R, bool bMoss)
	{
		const int32 W = 40, H = 50;
		FFVImage T(W, H);
		const FColor Wood = Rgb(110, 98, 88);
		const FVector2D Base(20.f, float(H - 1));
		const FVector2D Top(20.f + R.FRandRange(-3.f, 3.f), 14.f);
		ThickLine(T, Base, Top, 6.f, 3.f, Wood);
		ThickLine(T, Base, Base + FVector2D(-4.f, 0.f), 3.f, 1.f, Wood);
		ThickLine(T, Base, Base + FVector2D(4.f, 0.f), 3.f, 1.f, Wood);
		TArray<FVector2D> Tips;
		Branch(T, R, FMath::Lerp(Base, Top, 0.45f), FMath::DegreesToRadians(-150.f), 9.f, 3.f, 1, Wood, &Tips);
		Branch(T, R, FMath::Lerp(Base, Top, 0.62f), FMath::DegreesToRadians(-35.f), 9.f, 3.f, 1, Wood, &Tips);
		Branch(T, R, Top, FMath::DegreesToRadians(-100.f), 7.f, 2.f, 1, Wood, &Tips);
		EdgeShade(T, 0.25f, 0.3f);
		if (bMoss)
		{
			const FColor M0 = Rgb(104, 128, 64), M1 = Rgb(76, 98, 48);
			for (const FVector2D& Tip : Tips)
			{
				const int32 X = IRound(Tip.X), Y = IRound(Tip.Y);
				const int32 Len = R.RandRange(3, 7);
				for (int32 j = 1; j <= Len; ++j) { T.Set(X, Y + j, j % 2 ? M0 : M1); }
			}
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector2D P = FMath::Lerp(Base, Top, R.FRandRange(0.3f, 0.9f));
				const int32 Len = R.RandRange(2, 5);
				for (int32 j = 0; j < Len; ++j) { T.Set(IRound(P.X) + (i % 2 ? 2 : -2), IRound(P.Y) + j, M1); }
			}
		}
		T.Outline(Rgb(38, 32, 34));
		return T;
	}

	FFVImage MakeRock(FRandomStream& R, int32 RX, int32 RY, const FColor& Base, bool bMoss, bool bSnow)
	{
		FFVImage T(RX * 2 + 3, RY * 2 + 3);
		const FPal4 P = RockPal(Base);
		const uint32 Seed = R.GetUnsignedInt();
		const float CX = float(RX) + 1.5f, CY = float(RY) + 1.5f;
		PaintBlob(T, CX, CY, float(RX), float(RY), P, Seed, 0.12f, 0.25f);
		// a crack
		int32 X = FMath::RoundToInt(CX) + R.RandRange(-RX / 2, RX / 2), Y = FMath::RoundToInt(CY) - RY / 2;
		for (int32 i = 0; i < RY; ++i)
		{
			if (T.Get(X, Y).A > 0 && T.Get(X, Y + 1).A > 0) { T.Set(X, Y, P.Deep); }
			X += R.RandRange(-1, 1); ++Y;
		}
		for (int32 Yy = 0; Yy < T.H; ++Yy)
		{
			for (int32 Xx = 0; Xx < T.W; ++Xx)
			{
				if (T.Get(Xx, Yy).A == 0) { continue; }
				const float DY = (float(Yy) + 0.5f - CY) / float(RY);
				if (bSnow && DY < -0.25f - R01(Xx, 0, Seed) * 0.35f) { T.Set(Xx, Yy, DY < -0.55f || Xx < CX ? Rgb(246, 250, 255) : Rgb(206, 220, 240)); }
				else if (bMoss && DY < -0.1f && R01(Xx >> 1, Yy, Seed + 3) < 0.55f) { T.Set(Xx, Yy, R01(Xx, Yy, Seed) < 0.5f ? Rgb(96, 140, 64) : Rgb(120, 164, 76)); }
			}
		}
		T.Outline(Mix(P.Deep, Rgb(20, 20, 40), 0.5f));
		return T;
	}

	FFVImage MakeBush(FRandomStream& R, const FColor& Leaf, const FColor* Berry, int32 Size = 7)
	{
		FFVImage T(Size * 3 + 2, Size * 2 + 2);
		const FPal4 P = LeafPal(Leaf);
		const uint32 Seed = R.GetUnsignedInt();
		const float CY = float(Size) + 1.f;
		PaintBlob(T, float(Size) + 1.f, CY + 1.f, float(Size) * 0.8f, float(Size) * 0.75f, P, Seed);
		PaintBlob(T, float(Size) * 2.f + 1.f, CY + 1.f, float(Size) * 0.8f, float(Size) * 0.75f, P, Seed + 5);
		PaintBlob(T, float(Size) * 1.5f + 1.f, CY - 1.f, float(Size) * 0.85f, float(Size) * 0.8f, P, Seed + 9);
		if (Berry)
		{
			for (int32 i = 0; i < 6; ++i)
			{
				const int32 X = R.RandRange(2, T.W - 3), Y = R.RandRange(2, T.H - 3);
				if (T.Get(X, Y).A > 0) { T.Set(X, Y, *Berry); T.Set(X + 1, Y, Mul(*Berry, 0.75f)); }
			}
		}
		T.Outline(Rgb(30, 50, 34));
		return T;
	}

	FFVImage MakeMushroom(const FColor& Cap, const FColor& Spot)
	{
		const FColor CapHi = Mix(Cap, White, 0.35f), CapLo = Mul(Cap, 0.7f), Stem = Rgb(236, 224, 200), StemLo = Rgb(196, 180, 156);
		FFVImage M = Ascii({
			TEXT("..hhC.."),
			TEXT(".hCsCCc"),
			TEXT("hCCCCsc"),
			TEXT("ccccccc"),
			TEXT("..SSt.."),
			TEXT("..SSt..") },
			{ { TEXT('h'), CapHi }, { TEXT('C'), Cap }, { TEXT('c'), CapLo }, { TEXT('s'), Spot }, { TEXT('S'), Stem }, { TEXT('t'), StemLo } });
		M.Outline(Rgb(50, 30, 36));
		return M;
	}

	FFVImage MakeCrystal(FRandomStream& R, int32 H, bool bGlowEdge)
	{
		H = FMath::Clamp(H, 8, 30);
		FFVImage T(16, H + 1);
		const FColor F1 = Rgb(88, 64, 128), F2 = Rgb(48, 34, 74), Hi = Rgb(176, 146, 226), Glow = Rgb(214, 120, 255);
		struct FShard { int32 CX; int32 Hh; int32 Hw; };
		const FShard Shards[3] = { { 5, H * 2 / 3, 2 }, { 8, H, 3 }, { 11, H / 2 + 1, 2 } };
		for (const FShard& S : Shards)
		{
			const int32 Top = H - S.Hh;
			for (int32 Y = Top; Y <= H; ++Y)
			{
				const int32 Hw = FMath::Min(S.Hw, (Y - Top) / 2 + 0);
				for (int32 X = S.CX - Hw; X <= S.CX + Hw; ++X)
				{
					FColor C = X < S.CX ? F1 : (X == S.CX ? Hi : F2);
					if (bGlowEdge && X == S.CX + Hw && Y > Top + 1 && R.FRand() < 0.5f) { C = Glow; }
					T.Set(X, Y, C);
				}
			}
		}
		T.Outline(Rgb(26, 16, 36));
		return T;
	}

	FFVImage MakeHut()
	{
		const int32 W = 58, H = 48;
		FFVImage T(W, H);
		const FColor RoofHi = Rgb(210, 104, 70), Roof = Rgb(174, 74, 54), RoofLo = Rgb(130, 52, 42), RoofDeep = Rgb(98, 40, 36);
		const FColor WallHi = Rgb(216, 172, 114), Wall = Rgb(188, 140, 88), WallLo = Rgb(150, 106, 64), Trim = Rgb(110, 70, 42);
		const FColor Stone = Rgb(150, 146, 140), StoneLo = Rgb(112, 106, 104), StoneHi = Rgb(188, 184, 174);
		const FColor Glass = Rgb(66, 92, 132), GlassHi = Rgb(156, 196, 226);
		// walls
		for (int32 Y = 22; Y <= 44; ++Y)
		{
			for (int32 X = 6; X <= 51; ++X)
			{
				FColor C = ((Y - 22) % 4 == 3) ? WallLo : Wall;
				if (X <= 7) { C = ((Y - 22) % 4 == 1) ? WallHi : Mix(C, WallHi, 0.4f); }
				else if (X >= 50) { C = ((Y - 22) % 4 == 1) ? Wall : Trim; }
				else if (R01(X / 4, Y, 3) < 0.08f) { C = WallLo; }
				T.Set(X, Y, C);
			}
		}
		// eave shadow
		for (int32 X = 6; X <= 51; ++X) { T.Set(X, 22, Trim); T.Set(X, 23, WallLo); }
		// foundation
		for (int32 Y = 44; Y <= 47; ++Y)
		{
			for (int32 X = 5; X <= 52; ++X)
			{
				FColor C = ((X + (Y & 1) * 2) % 6 == 0) ? StoneLo : Stone;
				if (Y == 44) { C = StoneHi; }
				if (Y == 47) { C = StoneLo; }
				T.Set(X, Y, C);
			}
		}
		// door
		for (int32 Y = 30; Y <= 45; ++Y)
		{
			for (int32 X = 24; X <= 33; ++X)
			{
				FColor C = Rgb(106, 66, 42);
				if (X == 24 || X == 33 || Y == 30) { C = Trim; }
				else if ((X - 25) % 3 == 2) { C = Rgb(86, 52, 34); }
				else if (X == 25) { C = Rgb(130, 84, 54); }
				T.Set(X, Y, C);
			}
		}
		T.Set(31, 38, Rgb(236, 196, 92)); T.Set(31, 39, Rgb(180, 140, 60));
		for (int32 X = 23; X <= 34; ++X) { T.Set(X, 46, StoneHi); T.Set(X, 47, Stone); }
		// windows
		auto Window = [&](int32 X0, int32 Y0)
		{
			for (int32 Y = Y0; Y <= Y0 + 8; ++Y)
			{
				for (int32 X = X0; X <= X0 + 9; ++X)
				{
					FColor C = Glass;
					if (X == X0 || X == X0 + 9 || Y == Y0 || Y == Y0 + 8) { C = Trim; }
					else if (X == X0 + 5 || Y == Y0 + 4) { C = Rgb(200, 160, 110); }
					else if ((X == X0 + 1 && Y <= Y0 + 2) || (X == X0 + 2 && Y == Y0 + 1)) { C = GlassHi; }
					T.Set(X, Y, C);
				}
			}
		};
		Window(10, 27); Window(38, 27);
		// flower box under the left window
		const FColor Petals[3] = { Rgb(240, 90, 110), Rgb(255, 210, 90), Rgb(250, 150, 200) };
		for (int32 X = 9; X <= 20; ++X)
		{
			T.Set(X, 36, Rgb(136, 88, 52)); T.Set(X, 37, Rgb(104, 64, 40));
			T.Set(X, 35, (X % 2) ? Petals[(X / 2) % 3] : Rgb(76, 140, 60));
		}
		// roof
		for (int32 Y = 3; Y <= 21; ++Y)
		{
			const float Tt = float(Y - 3) / 18.f;
			const int32 X0 = FMath::RoundToInt(10.f - 8.f * Tt), X1 = FMath::RoundToInt(47.f + 8.f * Tt);
			const int32 Row = (Y - 3) / 3, In = (Y - 3) % 3;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor C = In == 0 ? RoofHi : (In == 2 ? RoofLo : Roof);
				if (In != 0 && ((X + (Row % 2) * 3) % 6) == 0) { C = RoofLo; }
				if (X <= X0 + 1) { C = Mix(C, RoofHi, 0.5f); }
				else if (X >= X1 - 1) { C = RoofDeep; }
				T.Set(X, Y, C);
			}
		}
		for (int32 X = 10; X <= 47; ++X) { T.Set(X, 2, RoofDeep); }
		for (int32 X = 2; X <= 55; ++X) { T.Set(X, 21, RoofDeep); }
		// chimney
		for (int32 Y = 0; Y <= 9; ++Y)
		{
			for (int32 X = 40; X <= 45; ++X)
			{
				FColor C = X == 40 ? StoneHi : (X == 45 ? StoneLo : Stone);
				if (Y <= 1) { C = Rgb(70, 64, 64); }
				else if ((Y + (X & 1)) % 3 == 0) { C = StoneLo; }
				T.Set(X, Y, C);
			}
		}
		T.Outline(Rgb(60, 36, 28));
		return T;
	}

	FFVImage MakeFishSign()
	{
		FFVImage T(16, 17);
		const FColor Board = Rgb(196, 146, 88), BoardLo = Rgb(160, 112, 66), BoardHi = Rgb(222, 178, 120);
		for (int32 Y = 0; Y <= 9; ++Y)
		{
			for (int32 X = 0; X <= 15; ++X)
			{
				FColor C = (Y == 4) ? BoardLo : Board;
				if (Y == 0) { C = BoardHi; }
				T.Set(X, Y, C);
			}
		}
		for (int32 Y = 10; Y <= 16; ++Y) { T.Set(7, Y, Bark); T.Set(8, Y, BarkLo); }
		// painted fish
		const FColor F = Rgb(62, 118, 190), FHi = Rgb(120, 180, 236);
		for (int32 X = 4; X <= 10; ++X) { T.Set(X, 4, F); }
		for (int32 X = 5; X <= 9; ++X) { T.Set(X, 3, FHi); T.Set(X, 5, F); }
		T.Set(11, 3, F); T.Set(11, 5, F); T.Set(12, 2, F); T.Set(12, 6, F);
		T.Set(5, 4, Rgb(30, 30, 50));
		T.Outline(Rgb(70, 44, 28));
		return T;
	}

	FFVImage MakeTent()
	{
		const int32 W = 38, H = 27;
		FFVImage T(W, H);
		const FColor Lit = Rgb(226, 150, 76), Mid = Rgb(198, 124, 60), Dark = Rgb(160, 94, 48), Inside = Rgb(62, 40, 34);
		const int32 CX = 19;
		for (int32 Y = 1; Y <= 24; ++Y)
		{
			const int32 Half = (Y - 1) * 17 / 23;
			for (int32 X = CX - Half; X <= CX + Half; ++X)
			{
				FColor C = X < CX - 2 ? Lit : (X <= CX + 1 ? Mid : Dark);
				if (X == CX) { C = Mul(Mid, 0.8f); }
				const int32 OpenHalf = FMath::Max(0, (Y - 10) * 6 / 14);
				if (Y >= 10 && FMath::Abs(X - CX) <= OpenHalf) { C = Inside; }
				if (Y >= 10 && X == CX - OpenHalf - 1) { C = Rgb(240, 196, 120); }
				T.Set(X, Y, C);
			}
		}
		T.Set(CX, 0, Rgb(120, 90, 60));
		ThickLine(T, FVector2D(4.f, 16.f), FVector2D(0.f, 25.f), 1.f, 1.f, Rgb(200, 190, 170));
		ThickLine(T, FVector2D(34.f, 16.f), FVector2D(37.f, 25.f), 1.f, 1.f, Rgb(200, 190, 170));
		T.Outline(Rgb(70, 42, 30));
		return T;
	}

	FFVImage MakeShack()
	{
		const int32 W = 48, H = 54;
		FFVImage T(W, H);
		const FColor Tin = Rgb(150, 94, 62), TinLo = Rgb(118, 70, 50), Rust = Rgb(176, 112, 62);
		const FColor Board = Rgb(112, 118, 96), BoardLo = Rgb(88, 94, 76), Gap = Rgb(58, 62, 52);
		// stilts
		const int32 Stilts[4] = { 5, 17, 30, 42 };
		for (int32 S : Stilts)
		{
			for (int32 Y = 38; Y < H; ++Y) { T.Set(S, Y, Rgb(96, 78, 58)); T.Set(S + 1, Y, Rgb(72, 58, 44)); }
		}
		// porch
		for (int32 Y = 36; Y <= 38; ++Y)
		{
			for (int32 X = 1; X <= 46; ++X) { T.Set(X, Y, Y == 36 ? Rgb(134, 120, 94) : ((X % 5) ? Rgb(108, 96, 76) : Rgb(80, 70, 56))); }
		}
		// walls
		for (int32 Y = 14; Y <= 35; ++Y)
		{
			for (int32 X = 4; X <= 43; ++X)
			{
				FColor C = ((X - 4) % 4 == 3) ? Gap : (((X / 4) % 3 == 1) ? BoardLo : Board);
				if (R01(X, Y / 3, 17) < 0.06f) { C = Mix(C, Rgb(80, 100, 60), 0.5f); }
				T.Set(X, Y, C);
			}
		}
		// window
		for (int32 Y = 19; Y <= 27; ++Y)
		{
			for (int32 X = 9; X <= 18; ++X)
			{
				FColor C = Rgb(40, 58, 62);
				if (X == 9 || X == 18 || Y == 19 || Y == 27) { C = Rgb(84, 70, 54); }
				else if (X == 14) { C = Rgb(84, 70, 54); }
				else if (X == 10 && Y < 22) { C = Rgb(110, 140, 140); }
				T.Set(X, Y, C);
			}
		}
		// door
		for (int32 Y = 20; Y <= 35; ++Y)
		{
			for (int32 X = 26; X <= 34; ++X) { T.Set(X, Y, (X == 26 || X == 34 || Y == 20) ? Rgb(60, 48, 38) : ((X % 3) ? Rgb(84, 66, 50) : Rgb(70, 56, 42))); }
		}
		T.Set(32, 28, Rgb(200, 180, 110));
		// roof (corrugated tin)
		for (int32 Y = 2; Y <= 13; ++Y)
		{
			const int32 X0 = 1 + (13 - Y) / 3, X1 = 46 - (13 - Y) / 3;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor C = (X % 3 == 0) ? TinLo : Tin;
				if (R01(X / 2, Y / 2, 5) < 0.18f) { C = Rust; }
				if (Y == 13) { C = Rgb(90, 56, 42); }
				T.Set(X, Y, C);
			}
		}
		// hanging lantern
		T.Set(38, 14, Rgb(60, 56, 60)); T.Set(38, 15, Rgb(60, 56, 60));
		for (int32 Y = 16; Y <= 19; ++Y)
		{
			for (int32 X = 37; X <= 39; ++X) { T.Set(X, Y, (Y == 16 || Y == 19) ? Rgb(60, 56, 60) : Rgb(236, 240, 150)); }
		}
		T.Outline(Rgb(40, 38, 34));
		return T;
	}

	FFVImage MakeBoat()
	{
		FFVImage T(34, 13);
		const FColor Paint = Rgb(70, 118, 164), PaintHi = Rgb(110, 156, 196), PaintLo = Rgb(50, 86, 124), Stripe = Rgb(236, 232, 220);
		for (int32 Y = 0; Y < 13; ++Y)
		{
			for (int32 X = 0; X < 34; ++X)
			{
				const float DX = (float(X) + 0.5f - 17.f) / 16.5f, DY = (float(Y) + 0.5f - 6.5f) / 6.f;
				const float Shape = DX * DX * (DX > 0.f ? 1.f : 0.7f) + DY * DY;
				if (Shape > 1.f) { continue; }
				FColor C = DY < -0.3f ? PaintHi : (DY > 0.35f ? PaintLo : Paint);
				if (FMath::Abs(DY) < 0.12f) { C = Rgb(92, 70, 50); }           // keel
				if (Shape > 0.72f && Shape < 0.86f) { C = Stripe; }
				T.Set(X, Y, C);
			}
		}
		T.Outline(Rgb(34, 44, 60));
		return T;
	}

	// =========================================================================================
	// Build context and terrain
	// =========================================================================================
	struct FObj
	{
		FFVImage Img;
		int32 X = 0, Y = 0, BaseY = 0;
		float ShadowRX = 0.f, ShadowRY = 0.f;
	};

	struct FGround { FColor Lo, Base, Hi; float Scale = 10.f; };

	struct FCtx
	{
		FFVImage& Img;
		FLayout& L;
		const FFVLocationDef& Loc;
		FRandomStream& R;
		int32 TW = BgW;           // texture size in texels (landscape 320x180, portrait e.g. 180x400)
		int32 TH = BgH;
		int32 Shore = 95;
		int32 WalkTopRow = -1;    // portrait: top row of the walkable land band (-1 = landscape default)
		int32 WaterDeepRow = -1;  // row where the water reaches its deepest color (-1 = bottom of the image)
		int32 VisX0 = 0;          // visible columns [VisX0, VisX1) and first row under the HUD (portrait bleed)
		int32 VisX1 = BgW;
		int32 VisY0 = 0;
		TArray<int32> Edge;
		TArray<uint8> Mask;       // 1 = reserved ground (path, objects), no scatter details
		int32 DockX = 200, DockW = 20, DockY0 = 90, DockY1 = 128;
		TArray<FObj> Objs;
		// ---- hotfix 0.61: a composition wider than the screen (the pond with SideWiden). All 0 = one screen.
		int32 OX = 0;             // side strip in texels: the old one-screen frame starts OX texels right of the content's left edge
		int32 ContentW = 0;       // portrait: content width in texels (old visible width + 2 * OX); 0 = PortVisW
		int32 NX = 0;             // texel shift of the old composition inside this image: hashed / dithered / scattered
		                          // details use X - NX and the base range [NX, BaseX1()), so the old area stays pixel-identical
		int32 NW = 0;             // width of the old composition in texels (0 = TW - 2 * NX)
		/** End (exclusive) of the base range of the random scatter (the whole image for a one-screen layout). */
		int32 BaseX1() const { return NW > 0 ? NX + NW : TW - NX; }

		FCtx(FFVImage& InImg, FLayout& InL, const FFVLocationDef& InLoc, FRandomStream& InR, int32 InW = BgW, int32 InH = BgH)
			: Img(InImg), L(InL), Loc(InLoc), R(InR), TW(InW), TH(InH)
		{
			Mask.Init(0, TW * TH);
			VisX1 = TW;
		}
		bool IsWaterPx(int32 X, int32 Y) const { return X >= 0 && X < TW && Y < TH && Y >= Edge[X]; }
		bool NearDock(int32 X, int32 Margin) const { return X >= DockX - Margin && X < DockX + DockW + Margin; }
		bool Free(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < TW && Y < TH && Mask[Y * TW + X] == 0; }
		void Reserve(int32 X, int32 Y, int32 RW, int32 RH)
		{
			for (int32 Yy = FMath::Max(0, Y); Yy < FMath::Min(TH, Y + RH); ++Yy)
				for (int32 Xx = FMath::Max(0, X); Xx < FMath::Min(TW, X + RW); ++Xx) { Mask[Yy * TW + Xx] = 1; }
		}
		FIntPoint AddObj(FFVImage&& In, int32 CX, int32 BaseY, float SRX = -1.f, float SRY = -1.f)
		{
			FObj& O = Objs.AddDefaulted_GetRef();
			O.X = CX - In.W / 2;
			O.Y = BaseY - (In.H - 1);
			O.BaseY = BaseY;
			O.ShadowRX = SRX < 0.f ? float(In.W) * 0.38f : SRX;
			O.ShadowRY = SRY < 0.f ? FMath::Max(2.f, float(In.W) * 0.1f) : SRY;
			Reserve(O.X, BaseY - 3, In.W, 5);
			O.Img = MoveTemp(In);
			return FIntPoint(O.X, O.Y);
		}
		void PlaceObjs()
		{
			Objs.StableSort([](const FObj& A, const FObj& B) { return A.BaseY < B.BaseY; });
			for (const FObj& O : Objs)
			{
				if (O.ShadowRX > 0.f) { ShadeEllipse(Img, float(O.X) + float(O.Img.W) * 0.5f + 2.f, float(O.BaseY) + 0.5f, O.ShadowRX, O.ShadowRY, 0.74f); }
			}
			for (const FObj& O : Objs) { Img.Blit(O.Img, O.X, O.Y); }
			Objs.Reset();
		}
		static FVector2D V(float X, float Y) { return FVector2D(X * 2.f, Y * 2.f); }   // texel -> virtual
	};

	/** Hotfix 0.61: paints with a random stream of its own for the scope, then restores FCtx::R exactly, so the art
	    of the new side strips of the wide pond never shifts the random draws of the old composition. */
	struct FSideStream
	{
		FCtx& C;
		FRandomStream Saved;
		FSideStream(FCtx& InC, int32 InSeed) : C(InC), Saved(InC.R) { C.R.Initialize(InSeed); }
		~FSideStream() { C.R = Saved; }
	};

	void MakeEdge(FCtx& C, int32 Shore, uint32 Seed)
	{
		C.Shore = Shore;
		C.Edge.SetNum(C.TW);
		for (int32 X = 0; X < C.TW; ++X)
		{
			const float N = VNoise(float(X - C.NX) / 9.f, 3.3f, Seed) * 2.6f - 1.3f;
			C.Edge[X] = Shore + FMath::Clamp(FMath::RoundToInt(N), -1, 1);
		}
	}

	void FillGround(FCtx& C, int32 Y0, const FGround& G, uint32 Seed)
	{
		for (int32 X = 0; X < C.TW; ++X)
		{
			const int32 HX = X - C.NX;   // hotfix 0.61: noise in the coordinates of the one-screen composition
			for (int32 Y = Y0; Y < C.Edge[X]; ++Y)
			{
				const float V = Fbm(float(HX) / G.Scale, float(Y) / (G.Scale * 0.8f), Seed) + (Bayer(HX, Y) - 0.5f) * 0.2f + (R01(HX, Y, Seed + 1) - 0.5f) * 0.1f;
				C.Img.Set(X, Y, V < 0.4f ? G.Lo : (V < 0.62f ? G.Base : G.Hi));
			}
		}
	}

	void FillWater(FCtx& C, uint32 Seed)
	{
		const FColor Sh = C.Loc.WaterShallow, Dp = C.Loc.WaterDeep;
		constexpr int32 Bands = 6;
		const int32 DeepRow = C.WaterDeepRow > C.Shore ? C.WaterDeepRow : C.TH;
		const float Span = float(DeepRow - C.Shore);
		for (int32 X = 0; X < C.TW; ++X)
		{
			const int32 HX = X - C.NX;
			for (int32 Y = C.Edge[X]; Y < C.TH; ++Y)
			{
				const float T = FMath::Clamp(float(Y - C.Shore) / Span, 0.f, 1.f);
				const float V = FMath::Pow(T, 0.8f) + (VNoise(float(HX) / 26.f, float(Y) / 8.f, Seed) - 0.5f) * 0.18f + (Bayer(HX, Y) - 0.5f) / float(Bands);
				const int32 B = FMath::Clamp(FMath::FloorToInt(V * float(Bands)), 0, Bands - 1);
				C.Img.Set(X, Y, Mix(Sh, Dp, float(B) / float(Bands - 1)));
			}
		}
	}

	enum class EBank : uint8 { Grass, Stone, Sand, Mud, Basalt };

	void DrawBank(FCtx& C, EBank S, const FGround& G)
	{
		const FColor Sh = C.Loc.WaterShallow;
		for (int32 X = 0; X < C.TW; ++X)
		{
			const int32 E = C.Edge[X];
			const int32 HX = X - C.NX;   // hotfix 0.61 (the wide pond): hashes / dither of the one-screen composition
			switch (S)
			{
			case EBank::Grass:
				C.Img.Set(X, E - 3, G.Hi);
				C.Img.Set(X, E - 2, R01(HX, 1, 3) < 0.3f ? G.Lo : Rgb(128, 92, 58));
				C.Img.Set(X, E - 1, R01(HX, 2, 3) < 0.3f ? G.Lo : Rgb(94, 66, 42));
				break;
			case EBank::Stone:
			{
				for (int32 Y = E - 3; Y < E; ++Y)
				{
					const int32 In = (X + (Y & 1) * 2) % 5;
					FColor Cc = Rgb(140, 138, 134);
					if (Y == E - 3 || In == 0) { Cc = Rgb(176, 174, 168); }
					if (In == 4 || Y == E - 1) { Cc = Rgb(100, 98, 100); }
					C.Img.Set(X, Y, Cc);
				}
				break;
			}
			case EBank::Sand:
				for (int32 Y = E - 7; Y < E; ++Y) { MixPx(C.Img, X, Y, Rgb(196, 162, 112), float(7 - (E - Y)) / 7.f * 0.75f + (Bayer(X, Y) - 0.5f) * 0.15f); }
				break;
			case EBank::Mud:
				C.Img.Set(X, E - 3, Rgb(92, 76, 52));
				C.Img.Set(X, E - 2, Rgb(74, 58, 40));
				C.Img.Set(X, E - 1, R01(X, 5, 9) < 0.2f ? Rgb(96, 110, 58) : Rgb(58, 44, 32));
				break;
			case EBank::Basalt:
				C.Img.Set(X, E - 3, Rgb(92, 82, 90));
				C.Img.Set(X, E - 2, Rgb(46, 38, 46));
				C.Img.Set(X, E - 1, Mix(Rgb(120, 40, 24), Sh, 0.3f));
				break;
			}
			if (S == EBank::Basalt)
			{
				MixPx(C.Img, X, E, Rgb(255, 236, 140), 0.55f);
				if (Bayer(X, E + 1) < 0.5f) { MixPx(C.Img, X, E + 1, Rgb(255, 220, 110), 0.3f); }
			}
			else if (S == EBank::Sand)
			{
				if (Bayer(X, E) < 0.75f) { MixPx(C.Img, X, E, White, 0.55f); }
				if (Bayer(X, E + 1) < 0.35f) { MixPx(C.Img, X, E + 1, White, 0.3f); }
			}
			else
			{
				MulPx(C.Img, X, E, 0.78f);
				if (Bayer(HX, E + 1) < 0.5f) { MixPx(C.Img, X, E + 1, White, 0.4f); }
				if (Bayer(HX, E + 2) < 0.25f) { MixPx(C.Img, X, E + 2, White, 0.2f); }
			}
		}
	}

	enum class EDock : uint8 { Wood, Dark, Weathered, Pier, Rock };

	void DrawDock(FCtx& C, EDock S)
	{
		const int32 X0 = C.DockX, W = C.DockW, Y0 = C.DockY0, Y1 = C.DockY1;
		// water shadow to the right and below the dock
		for (int32 Y = C.Shore - 1; Y < Y1 + 3; ++Y)
			for (int32 X = X0 + W; X < X0 + W + 4; ++X) { if (C.IsWaterPx(X, Y)) { MulPx(C.Img, X, Y, 0.74f); } }
		for (int32 Y = Y1 + 2; Y < Y1 + 5; ++Y)
			for (int32 X = X0 + 2; X < X0 + W + 4; ++X) { if (C.IsWaterPx(X, Y)) { MulPx(C.Img, X, Y, 0.76f); } }

		if (S == EDock::Rock)
		{
			for (int32 Y = Y0; Y < Y1 + 3; ++Y)
			{
				const int32 Row = (Y - Y0) / 5, In = (Y - Y0) % 5;
				for (int32 X = X0; X < X0 + W; ++X)
				{
					const int32 Off = (Row & 1) ? 3 : 0;
					const int32 SI = (X - X0 + Off) / 6, InX = (X - X0 + Off) % 6;
					FColor Cc;
					if (In == 4 || InX == 5)
					{
						Cc = R01(SI, Row, 5) < 0.22f ? Rgb(226, 96, 34) : Rgb(30, 24, 30);
					}
					else
					{
						Cc = Mix(Rgb(78, 68, 78), Rgb(54, 46, 56), R01(SI, Row, 9));
						if (In == 0 || InX == 0) { Cc = Mix(Cc, Rgb(130, 118, 128), 0.5f); }
						else if (In == 3 || InX == 4) { Cc = Mul(Cc, 0.8f); }
					}
					if (Y >= Y1) { Cc = Mul(Cc, 0.6f); }
					if (X == X0 || X == X0 + W - 1) { Cc = Rgb(28, 22, 28); }
					C.Img.Set(X, Y, Cc);
				}
			}
			return;
		}

		struct FWood { FColor Hi, Base, Lo, Gap, Post, PostHi; };
		FWood Wd{ Rgb(204, 150, 94), Rgb(170, 118, 72), Rgb(128, 84, 50), Rgb(76, 48, 32), Rgb(100, 64, 42), Rgb(146, 100, 64) };
		if (S == EDock::Dark) { Wd = { Rgb(176, 124, 80), Rgb(144, 96, 60), Rgb(106, 68, 42), Rgb(62, 40, 28), Rgb(84, 54, 36), Rgb(128, 86, 56) }; }
		else if (S == EDock::Weathered) { Wd = { Rgb(150, 142, 112), Rgb(122, 114, 88), Rgb(94, 86, 66), Rgb(52, 48, 38), Rgb(78, 70, 54), Rgb(118, 110, 86) }; }
		else if (S == EDock::Pier) { Wd = { Rgb(222, 196, 152), Rgb(192, 162, 122), Rgb(152, 122, 90), Rgb(92, 70, 52), Rgb(112, 86, 62), Rgb(172, 142, 106) }; }

		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			const int32 Row = (Y - Y0) % 4, Board = (Y - Y0) / 4;
			const FColor B = Mix(Wd.Base, Wd.Lo, R01(Board, 7, 11) * 0.45f);
			const int32 Seam = X0 + ((Board & 1) ? 7 : 13);
			for (int32 X = X0; X < X0 + W; ++X)
			{
				FColor Cc;
				if (Row == 3) { Cc = Wd.Gap; }
				else if (X == Seam) { Cc = Mix(Wd.Lo, Wd.Gap, 0.6f); }
				else
				{
					Cc = Row == 0 ? Mix(B, Wd.Hi, 0.55f) : B;
					if (R01((X - C.NX) / 3, Y, 29 + uint32(Board)) < 0.14f) { Cc = Mix(Cc, Wd.Lo, 0.55f); }
				}
				if (X == X0 || X == X0 + W - 1) { Cc = Wd.Gap; }
				else if (X == X0 + 1 && Row != 3) { Cc = Mix(Cc, Wd.Hi, 0.5f); }
				else if (X == X0 + W - 2 && Row != 3) { Cc = Mix(Cc, Wd.Lo, 0.6f); }
				if (Row == 1 && (X == X0 + 2 || X == X0 + W - 3)) { Cc = Rgb(84, 78, 80); }
				C.Img.Set(X, Y, Cc);
			}
		}
		for (int32 X = X0; X < X0 + W; ++X) { C.Img.Set(X, Y1, Wd.Lo); C.Img.Set(X, Y1 + 1, Wd.Gap); }

		auto Post = [&](int32 PX, int32 PY, int32 Len)
		{
			for (int32 Y = PY; Y < PY + Len; ++Y)
			{
				for (int32 X = PX; X < PX + 3; ++X)
				{
					FColor Cc = Wd.Post;
					if (Y == PY) { Cc = Wd.PostHi; }
					else if (X == PX) { Cc = Mix(Wd.Post, Wd.PostHi, 0.5f); }
					else if (X == PX + 2) { Cc = Mix(Wd.Post, Wd.Gap, 0.5f); }
					C.Img.Set(X, Y, Cc);
				}
			}
			for (int32 Y = PY - 1; Y <= PY + Len; ++Y) { C.Img.Set(PX - 1, Y, Wd.Gap); C.Img.Set(PX + 3, Y, Wd.Gap); }
			for (int32 X = PX; X < PX + 3; ++X) { C.Img.Set(X, PY - 1, Wd.Gap); C.Img.Set(X, PY + Len, Wd.Gap); }
		};
		for (int32 Y = C.Shore + 6; Y < Y1 - 6; Y += 12) { Post(X0 - 1, Y, 4); Post(X0 + W - 2, Y, 4); }
		Post(X0 - 1, Y1 - 3, 7);
		Post(X0 + W - 2, Y1 - 3, 7);
		for (int32 X = X0 - 3; X <= X0 + 3; ++X) { if (C.IsWaterPx(X, Y1 + 5)) { MixPx(C.Img, X, Y1 + 5, White, 0.35f); } }
		for (int32 X = X0 + W - 4; X <= X0 + W + 2; ++X) { if (C.IsWaterPx(X, Y1 + 5)) { MixPx(C.Img, X, Y1 + 5, White, 0.35f); } }

		if (S == EDock::Pier)
		{
			// lantern pole on the right end corner
			const int32 PX = X0 + W - 1;
			for (int32 Y = Y1 - 26; Y <= Y1 - 3; ++Y) { C.Img.Set(PX, Y, Rgb(70, 60, 56)); C.Img.Set(PX + 1, Y, Rgb(44, 38, 38)); }
			for (int32 Y = Y1 - 32; Y <= Y1 - 26; ++Y)
			{
				for (int32 X = PX - 2; X <= PX + 3; ++X)
				{
					const bool bFrame = X == PX - 2 || X == PX + 3 || Y == Y1 - 32 || Y == Y1 - 26 || Y == Y1 - 29;
					C.Img.Set(X, Y, bFrame ? Rgb(52, 46, 50) : Rgb(255, 222, 130));
				}
			}
			C.L.Lights.Add({ FCtx::V(float(PX) + 0.5f, float(Y1 - 29)), 54.f, Rgb(255, 200, 110), true, false, FFVRect() });
		}
	}

	/** Static reed clump standing in the shallow water at the shore line. */
	void ReedClump(FCtx& C, int32 X, bool bCattail)
	{
		const FColor S0 = Rgb(86, 138, 58), S1 = Rgb(124, 172, 76), S2 = Rgb(62, 102, 46);
		const int32 N = C.R.RandRange(3, 6);
		for (int32 i = 0; i < N; ++i)
		{
			const int32 SX = X + C.R.RandRange(-4, 4);
			if (SX < 0 || SX >= C.TW || C.NearDock(SX, 3)) { continue; }
			const int32 Base = C.Edge[SX] + C.R.RandRange(1, 4);
			const int32 H = C.R.RandRange(7, 13);
			const int32 Lean = C.R.RandRange(-1, 1);
			for (int32 j = 0; j < H; ++j)
			{
				const int32 PX = SX + (j > H / 2 ? Lean : 0);
				C.Img.Set(PX, Base - j, j < 2 ? S2 : (j > H - 3 ? S1 : S0));
			}
			if (bCattail && (i % 2) == 0)
			{
				const int32 PX = SX + Lean;
				for (int32 j = H - 5; j < H - 1; ++j) { C.Img.Set(PX, Base - j, Rgb(120, 76, 42)); C.Img.Set(PX + 1, Base - j, Rgb(90, 56, 32)); }
			}
			MixPx(C.Img, SX - 1, Base + 1, White, 0.3f); MixPx(C.Img, SX + 1, Base + 1, White, 0.3f);
		}
	}

	/** XMin / XMax < 0 = the base range (the whole image; the old one-screen area of a wide pond, see FCtx::NX). */
	void Tufts(FCtx& C, int32 Count, int32 Y0, int32 Y1, const FGround& G, int32 XMin = -1, int32 XMax = -1)
	{
		if (XMin < 0) { XMin = C.NX + 1; }
		if (XMax < 0) { XMax = C.BaseX1() - 2; }
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 X = C.R.RandRange(XMin, XMax), Y = C.R.RandRange(Y0, Y1);
			if (!C.Free(X, Y) || Y >= C.Edge[X] - 3) { continue; }
			C.Img.Set(X - 1, Y - 2, G.Hi); C.Img.Set(X + 1, Y - 2, G.Hi);
			C.Img.Set(X - 1, Y - 1, G.Base); C.Img.Set(X, Y - 1, G.Hi); C.Img.Set(X + 1, Y - 1, G.Lo);
			C.Img.Set(X, Y, G.Lo);
		}
	}

	void Flowers(FCtx& C, int32 Count, int32 Y0, int32 Y1, const TArray<FColor>& Petals, int32 SpotEvery, int32 XMin = -1, int32 XMax = -1)
	{
		if (Petals.Num() == 0) { return; }
		if (XMin < 0) { XMin = C.NX + 2; }
		if (XMax < 0) { XMax = C.BaseX1() - 3; }
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 X = C.R.RandRange(XMin, XMax), Y = C.R.RandRange(Y0, Y1);
			if (!C.Free(X, Y) || Y >= C.Edge[X] - 4) { continue; }
			const FColor P = Petals[C.R.RandHelper(Petals.Num())];
			if (C.R.FRand() < 0.55f)
			{
				C.Img.Set(X, Y - 1, P); C.Img.Set(X - 1, Y, P); C.Img.Set(X + 1, Y, Mul(P, 0.85f)); C.Img.Set(X, Y + 1, Mul(P, 0.8f));
				C.Img.Set(X, Y, Rgb(250, 220, 90));
				C.Img.Set(X, Y + 2, Rgb(60, 110, 50));
			}
			else
			{
				C.Img.Set(X, Y, P); C.Img.Set(X, Y + 1, Rgb(60, 110, 50));
			}
			// butterfly homes: only where they can be seen (portrait: not in the bleed, not under the info strip)
			const bool bSeen = C.VisX0 == 0 || (X >= C.VisX0 + 4 && X < C.VisX1 - 4 && Y > C.VisY0 + 6);
			if (SpotEvery > 0 && (i % SpotEvery) == 0 && Y > 20 && bSeen) { C.L.FlowerSpots.Add(FCtx::V(float(X), float(Y))); }
		}
	}

	void Pebbles(FCtx& C, int32 Count, int32 DepthRows, const FColor& Tint, float Amount, int32 XMin = -1, int32 XMax = -1)
	{
		if (XMin < 0) { XMin = C.NX; }
		if (XMax < 0) { XMax = C.BaseX1() - 1; }
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 X = C.R.RandRange(XMin, XMax);
			if (C.NearDock(X, 2)) { continue; }
			const int32 Y = C.Edge[X] + C.R.RandRange(3, DepthRows);
			const float RX = C.R.FRandRange(1.f, 2.6f);
			for (int32 Yy = Y - 2; Yy <= Y + 2; ++Yy)
				for (int32 Xx = X - 3; Xx <= X + 3; ++Xx)
				{
					const float DX = (float(Xx - X)) / RX, DY = float(Yy - Y) / (RX * 0.6f);
					if (DX * DX + DY * DY <= 1.f && C.IsWaterPx(Xx, Yy)) { MixPx(C.Img, Xx, Yy, DY < 0.f ? Mix(Tint, White, 0.25f) : Tint, Amount); }
				}
		}
	}

	void LilyPads(FCtx& C, int32 Count, int32 YMin, int32 YMax, int32 XMin = 10, int32 XMax = -1)
	{
		int32 Guard = 0;
		if (XMax < 0) { XMax = C.TW - 10; }
		while (C.L.LilyPads.Num() < Count && Guard++ < 400)
		{
			const int32 X = C.R.RandRange(XMin, XMax), Y = C.R.RandRange(YMin, YMax);
			if (C.NearDock(X, 12) || !C.IsWaterPx(X, Y - 4)) { continue; }
			const FVector2D P = FCtx::V(float(X), float(Y));
			bool bOk = true;
			for (const FVector2D& O : C.L.LilyPads) { if (FVector2D::Distance(O, P) < 34.f) { bOk = false; break; } }
			if (!bOk) { continue; }
			C.L.LilyPads.Add(P);
			C.L.LilyKinds.Add(uint8(C.R.RandHelper(3)));
		}
	}

	void Spawn(FCtx& C, int32 SideOffset)
	{
		C.L.Spawn = FVector2D(FMath::Clamp(float(C.DockX * 2 + C.DockW) + float(SideOffset), 40.f, float(C.TW * 2) - 40.f), float(C.Shore * 2) - 8.f);
	}

	void AddProp(FCtx& C, EDecor Kind, FVector2D Feet, bool bFlip = false)
	{
		FProp P; P.Kind = Kind; P.Feet = Feet; P.bFlip = bFlip;
		C.L.Props.Add(P);
		const FVector2D B = DecorBlocker(Kind);
		if (B.X > 0.f && B.Y > 0.f) { C.L.Blockers.Add(FFVRect(Feet.X - B.X * 0.5f, Feet.Y - B.Y + 2.f, B.X, B.Y)); }
		switch (Kind)
		{
		case EDecor::LampPost: C.L.Lights.Add({ Feet + FVector2D(0.f, -44.f), 50.f, Rgb(255, 196, 110), true, false, FFVRect() }); break;
		case EDecor::Campfire:
			C.L.Fires.Add(Feet + FVector2D(0.f, -6.f));
			C.L.Smokes.Add(Feet + FVector2D(0.f, -22.f));
			C.L.Lights.Add({ Feet + FVector2D(0.f, -10.f), 70.f, Rgb(255, 150, 60), true, false, FFVRect() });
			break;
		case EDecor::Brazier:
			C.L.Fires.Add(Feet + FVector2D(0.f, -26.f));
			C.L.Lights.Add({ Feet + FVector2D(0.f, -30.f), 56.f, Rgb(255, 140, 50), true, true, FFVRect() });
			break;
		case EDecor::GlowShrooms: C.L.Lights.Add({ Feet + FVector2D(0.f, -8.f), 26.f, Rgb(90, 230, 240), true, false, FFVRect() }); break;
		case EDecor::Crystal: C.L.Lights.Add({ Feet + FVector2D(0.f, -18.f), 22.f, Rgb(200, 110, 255), false, false, FFVRect() }); break;
		default: break;
		}
	}

	FFVRect WindowRect(FIntPoint TopLeft, int32 X, int32 Y, int32 W, int32 H)
	{
		return FFVRect(float(TopLeft.X + X) * 2.f, float(TopLeft.Y + Y) * 2.f, float(W) * 2.f, float(H) * 2.f);
	}

	// =========================================================================================
	// Themes
	// =========================================================================================
	void BuildPond(FCtx& C)
	{
		const FGround G{ Rgb(82, 150, 64), Rgb(106, 176, 72), Rgb(136, 200, 88), 11.f };
		// Hotfix 0.61: the pond may be wider than the screen (FFVLocationDef::SideWiden, scroll camera). OX = the side
		// strip in texels: the old 320x180 composition is painted OX texels to the right, pixel-identical (NX = OX:
		// same hashes, same random draws); the strips left of it and right of it (+ the HUD pad beyond the right one)
		// get their own art from streams of their own (FSideStream), so nothing of the old composition moves.
		const int32 OX = C.OX;
		const int32 RS = OX + BgW;                 // first column of the right strip
		const int32 RE = RS + OX;                  // end of the content (the painted HUD pad follows, up to C.TW)
		MakeEdge(C, 95, 11);
		C.DockX = 196 + OX; C.DockW = 20; C.DockY0 = 90; C.DockY1 = 128;
		FillGround(C, 0, G, 21);

		// dirt path from the hut door to the dock (waypoints in the texels of the one-screen composition)
		const FColor PLo = Rgb(152, 114, 74), PBase = Rgb(180, 140, 94), PHi = Rgb(204, 166, 116);
		const FVector2D Way[] = { { 56.f, 58.f }, { 66.f, 67.f }, { 96.f, 75.f }, { 136.f, 80.f }, { 176.f, 85.f }, { 206.f, 91.f } };
		for (int32 i = 0; i + 1 < int32(UE_ARRAY_COUNT(Way)); ++i)
		{
			const float Len = FVector2D::Distance(Way[i], Way[i + 1]);
			for (float s = 0.f; s <= Len; s += 0.5f)
			{
				const FVector2D P = FMath::Lerp(Way[i], Way[i + 1], s / Len);
				const float Rad = 3.2f + VNoise(P.X / 6.f, P.Y / 6.f, 5) * 1.4f;
				for (int32 Y = IFloor(P.Y - Rad - 1.0); Y <= ICeil(P.Y + Rad + 1.0); ++Y)
					for (int32 X0 = IFloor(P.X - Rad - 1.0); X0 <= ICeil(P.X + Rad + 1.0); ++X0)
					{
						const int32 X = X0 + OX;
						const float D = FVector2D::Distance(FVector2D(float(X0) + 0.5f, float(Y) + 0.5f), P);
						if (D > Rad + 0.8f || Y >= C.Edge[FMath::Clamp(X, 0, C.TW - 1)] - 3) { continue; }
						if (D > Rad) { if (Bayer(X0, Y) < 0.5f && C.Free(X, Y)) { C.Img.Set(X, Y, PLo); } continue; }
						const float N = R01(X0, Y, 71);
						C.Img.Set(X, Y, N < 0.2f ? PLo : (N > 0.85f ? PHi : PBase));
						C.Reserve(X, Y, 1, 1);
					}
			}
		}
		for (int32 i = 0; i < 40; ++i)
		{
			const int32 X = C.R.RandRange(50, 210) + OX, Y = C.R.RandRange(56, 92);
			if (!C.Free(X, Y) && Y < C.Edge[X] - 3) { C.Img.Set(X, Y, Rgb(160, 150, 140)); C.Img.Set(X + 1, Y, Rgb(120, 112, 106)); }
		}

		const TArray<FColor> Petals = { Rgb(250, 110, 130), Rgb(255, 220, 90), Rgb(250, 250, 250), Rgb(170, 140, 250), Rgb(255, 160, 80) };
		Tufts(C, 260, 8, 92, G);
		Flowers(C, 90, 10, 90, Petals, 6);
		if (OX > 0)
		{
			// the strips and the pad: the same density of grass tufts (2 texels off the old area); no flowers (butterfly
			// homes) in the corridors of the side exits, where FVSceneU6Objects puts the signs and path decals
			FSideStream Side(C, 0x61D0);
			const int32 PadN = FMath::Max(0, C.TW - RE);
			Tufts(C, FMath::RoundToInt(260.f * float(OX) / float(BgW)), 8, 92, G, 1, OX - 3);
			Tufts(C, FMath::RoundToInt(260.f * float(OX + PadN) / float(BgW)), 8, 92, G, RS + 2, C.TW - 2);
			C.Reserve(0, 40, FMath::Min(OX + 10, 42), 56);
			C.Reserve(RS - 4, 40, C.TW - RS + 4, 56);
			Flowers(C, FMath::RoundToInt(90.f * float(OX) / float(BgW)), 10, 90, Petals, 6, 2, OX - 4);
			Flowers(C, FMath::RoundToInt(90.f * float(OX) / float(BgW)), 10, 90, Petals, 6, RS + 3, RE - 3);
		}

		FillWater(C, 31);
		DrawBank(C, EBank::Grass, G);
		Pebbles(C, 26, 16, Rgb(96, 120, 90), 0.35f);
		auto Weeds = [&C](int32 N, int32 X0, int32 X1)   // underwater weeds
		{
			for (int32 i = 0; i < N; ++i)
			{
				const int32 X = C.R.RandRange(X0, X1);
				if (C.NearDock(X, 4)) { continue; }
				const int32 Y = C.Edge[X] + C.R.RandRange(5, 24), H = C.R.RandRange(2, 5);
				for (int32 j = 0; j < H; ++j) { MixPx(C.Img, X + ((j / 2) % 2), Y - j, Rgb(40, 100, 60), 0.45f); }
			}
		};
		Weeds(40, OX + 2, RS - 3);
		for (int32 X : { 10, 22, 36, 258, 272, 290, 306 }) { ReedClump(C, X + OX, false); }
		if (OX > 0)
		{
			FSideStream Side(C, 0x61D1);
			Pebbles(C, 3, 16, Rgb(96, 120, 90), 0.35f, 0, OX - 4);
			Pebbles(C, 5, 16, Rgb(96, 120, 90), 0.35f, RS + 3, C.TW - 1);
			Weeds(4, 0, OX - 2);
			Weeds(6, RS + 1, C.TW - 3);
			for (int32 X : { 4, 18, RS + 10, RS + 24, RE + 20 }) { ReedClump(C, X, false); }
		}
		DrawDock(C, EDock::Wood);

		// backdrop
		const FIntPoint Hut = C.AddObj(MakeHut(), 56 + OX, 58, 30.f, 4.f);
		C.L.Lights.Add({ FCtx::V(float(Hut.X + 15), float(Hut.Y + 31)), 34.f, Rgb(255, 190, 100), false, false, WindowRect(Hut, 11, 28, 8, 7) });
		C.L.Lights.Add({ FCtx::V(float(Hut.X + 43), float(Hut.Y + 31)), 34.f, Rgb(255, 190, 100), false, false, WindowRect(Hut, 39, 28, 8, 7) });
		C.L.Smokes.Add(FCtx::V(float(Hut.X + 43), float(Hut.Y - 1)));
		C.AddObj(MakeFishSign(), 94 + OX, 58, 5.f, 2.f);
		const FColor Apple = Rgb(222, 60, 50);
		C.AddObj(MakeOak(C.R, Rgb(84, 156, 62), 10, nullptr), 128 + OX, 44);
		C.AddObj(MakeOak(C.R, Rgb(76, 150, 60), 12, &Apple), 176 + OX, 38);
		C.AddObj(MakeOak(C.R, Rgb(88, 160, 66), 12, nullptr), 252 + OX, 46);
		C.AddObj(MakeOak(C.R, Rgb(80, 152, 62), 10, nullptr), 300 + OX, 36);
		C.AddObj(MakeOak(C.R, Rgb(82, 154, 60), 9, nullptr), 6 + OX, 40);
		const FColor Berry = Rgb(230, 70, 90);
		C.AddObj(MakeBush(C.R, Rgb(78, 150, 62), &Berry), 110 + OX, 60);
		C.AddObj(MakeBush(C.R, Rgb(84, 160, 66), nullptr), 226 + OX, 58);
		// (the wide pond: the small bush steps left, clear of the ocean exit sign in the right strip)
		C.AddObj(MakeBush(C.R, Rgb(78, 150, 62), nullptr, 6), (OX > 0 ? 306 : 314) + OX, 60);
		if (OX > 0)
		{
			// strip oaks: above the sign of the side exit (the sign stands in front of the trunk), y-sorted with the rest
			FSideStream Side(C, 0x61D2);
			C.AddObj(MakeOak(C.R, Rgb(80, 152, 62), 9, nullptr), 14, 36);
			C.AddObj(MakeOak(C.R, Rgb(86, 158, 64), 9, &Apple), RS + 18, 40);
		}
		// wooden fence
		{
			const FColor Post = Rgb(150, 104, 62), PostHi = Rgb(196, 148, 96), Rail = Rgb(176, 128, 80);
			for (int32 X = 102 + OX; X <= 150 + OX; ++X)
			{
				C.Img.Set(X, 49, Rail); C.Img.Set(X, 50, Mul(Rail, 0.7f));
				C.Img.Set(X, 53, Rail); C.Img.Set(X, 54, Mul(Rail, 0.7f));
			}
			for (int32 X = 102 + OX; X <= 150 + OX; X += 8)
			{
				for (int32 Y = 46; Y <= 57; ++Y) { C.Img.Set(X, Y, Y == 46 ? PostHi : Post); C.Img.Set(X + 1, Y, Mul(Post, 0.75f)); }
				ShadeEllipse(C.Img, float(X) + 2.f, 57.5f, 2.f, 1.f, 0.75f);
			}
		}
		C.PlaceObjs();

		LilyPads(C, 9, 104, 150, 10 + OX, RS - 10);
		C.L.Reeds = { C.V(float(14 + OX), 97), C.V(float(30 + OX), 98), C.V(float(264 + OX), 97), C.V(float(282 + OX), 98), C.V(float(300 + OX), 97) };
		if (OX > 0)
		{
			// two more lily pads over the wider water (one per strip), animated reeds at the strip clumps
			FSideStream Side(C, 0x61D3);
			LilyPads(C, C.L.LilyPads.Num() + 1, 104, 150, 6, OX - 4);
			LilyPads(C, C.L.LilyPads.Num() + 1, 104, 150, RS + 4, RE - 6);
			C.L.Reeds.Append({ C.V(4.f, 97.f), C.V(18.f, 98.f), C.V(float(RS + 10), 97.f), C.V(float(RS + 24), 98.f) });
		}
		AddProp(C, EDecor::LampPost, C.V(float(C.DockX) - 7.f, 93.f));
		AddProp(C, EDecor::Barrel, FVector2D(476.f + 2.f * float(OX), 156.f));
		AddProp(C, EDecor::Crates, FVector2D(506.f + 2.f * float(OX), 162.f));
		AddProp(C, EDecor::Bush, FVector2D(52.f + 2.f * float(OX), 150.f));
		C.L.bFireflies = true; C.L.bButterflies = true; C.L.bBirds = true;
		C.L.Current = 3.f;
		Spawn(C, -60);
	}

	void BuildRiver(FCtx& C)
	{
		const FGround G{ Rgb(60, 108, 58), Rgb(76, 128, 66), Rgb(98, 150, 76), 9.f };
		MakeEdge(C, 93, 12);
		C.DockX = 122; C.DockW = 20; C.DockY0 = 88; C.DockY1 = 130;
		FillGround(C, 0, G, 22);
		// pine needles & ferns
		for (int32 i = 0; i < 220; ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(4, 90);
			if (Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(120, 96, 60) : Rgb(96, 76, 50)); }
		}
		Tufts(C, 200, 8, 90, G);
		for (int32 i = 0; i < 14; ++i)   // ferns
		{
			const int32 X = C.R.RandRange(6, C.TW - 6), Y = C.R.RandRange(60, 88);
			if (C.NearDock(X, 6) || Y >= C.Edge[X] - 4) { continue; }
			for (int32 k = -3; k <= 3; ++k)
			{
				const int32 Len = 3 - FMath::Abs(k) / 2;
				for (int32 j = 0; j < Len; ++j) { C.Img.Set(X + k + (k < 0 ? -j : j) / 2, Y - j - (3 - FMath::Abs(k)), j == Len - 1 ? Rgb(140, 190, 90) : Rgb(70, 130, 60)); }
			}
		}
		Flowers(C, 30, 50, 90, { Rgb(250, 250, 240), Rgb(240, 200, 90), Rgb(190, 150, 240) }, 4);

		FillWater(C, 32);
		// current streaks
		for (int32 i = 0; i < 140; ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(C.Shore + 4, C.TH - 12);
			const int32 Len = C.R.RandRange(3, 9);
			for (int32 j = 0; j < Len; ++j) { if (C.IsWaterPx(X + j, Y)) { MixPx(C.Img, X + j, Y, White, 0.16f); } }
		}
		DrawBank(C, EBank::Grass, G);
		Pebbles(C, 30, 14, Rgb(120, 116, 100), 0.35f);
		// far bank at the very bottom
		for (int32 X = 0; X < C.TW; ++X)
		{
			const int32 FB = 170 + FMath::RoundToInt(VNoise(float(X) / 7.f, 9.f, 4) * 2.f);
			for (int32 Y = FB; Y < C.TH; ++Y)
			{
				FColor Cc = Y <= FB + 1 ? Rgb(150, 144, 130) : ((R01(X, Y, 6) < 0.5f) ? G.Base : G.Lo);
				if (Y == FB) { Cc = Rgb(186, 180, 166); }
				C.Img.Set(X, Y, Cc);
			}
			MulPx(C.Img, X, FB - 1, 0.8f);
			if (Bayer(X, FB - 2) < 0.5f) { MixPx(C.Img, X, FB - 2, White, 0.35f); }
		}
		// rocks in the stream with foam
		for (const FIntPoint& P : { FIntPoint(40, 142), FIntPoint(262, 124), FIntPoint(300, 156) })
		{
			FFVImage Rock = MakeRock(C.R, 5, 3, Rgb(128, 126, 120), true, false);
			C.Img.Blit(Rock, P.X - Rock.W / 2, P.Y - Rock.H / 2);
			for (int32 X = P.X - 8; X <= P.X + 9; ++X)
			{
				MixPx(C.Img, X, P.Y + Rock.H / 2 + 1, White, 0.55f);
				if ((X & 1) == 0) { MixPx(C.Img, X + 2, P.Y + Rock.H / 2 + 2, White, 0.3f); }
			}
			for (int32 j = 1; j < 14; ++j) { if (Bayer(P.X + 8 + j, P.Y) < 0.5f) { MixPx(C.Img, P.X + 7 + j, P.Y + (j % 3) - 1, White, 0.4f - float(j) * 0.025f); } }
		}
		// shore stones
		for (int32 i = 0; i < 16; ++i)
		{
			const int32 X = C.R.RandRange(4, C.TW - 5);
			if (C.NearDock(X, 6)) { continue; }
			FFVImage Rock = MakeRock(C.R, C.R.RandRange(2, 4), 2, Rgb(140, 136, 128), C.R.FRand() < 0.4f, false);
			C.Img.Blit(Rock, X - Rock.W / 2, C.Edge[X] - Rock.H / 2 - 1);
		}
		for (int32 X : { 14, 30, 290 }) { ReedClump(C, X, false); }
		DrawDock(C, EDock::Dark);

		// backdrop: dense pine forest
		for (int32 i = 0; i < 17; ++i)
		{
			const int32 X = i * 20 + C.R.RandRange(-6, 6);
			const int32 Base = C.R.RandRange(22, 40);
			C.AddObj(MakePine(C.R, C.R.RandRange(30, 44), Mix(Rgb(40, 104, 70), Rgb(58, 124, 74), C.R.FRand()), false), X, Base, 8.f, 2.f);
		}
		for (const FIntPoint& P : { FIntPoint(8, 56), FIntPoint(36, 60), FIntPoint(272, 58), FIntPoint(300, 54), FIntPoint(160, 48) })
		{
			C.AddObj(MakePine(C.R, C.R.RandRange(36, 46), Rgb(48, 112, 72), false), P.X, P.Y, 9.f, 2.5f);
		}
		const FIntPoint Tent = C.AddObj(MakeTent(), 214, 58, 16.f, 3.f);
		C.L.Lights.Add({ FCtx::V(float(Tent.X + 19), float(Tent.Y + 19)), 30.f, Rgb(255, 180, 90), false, false, FFVRect() });
		// fallen log
		{
			FFVImage Log(34, 9);
			for (int32 X = 2; X < 32; ++X)
				for (int32 Y = 1; Y < 8; ++Y) { Log.Set(X, Y, Y <= 2 ? BarkHi : (Y >= 6 ? BarkLo : ((X % 7 == 0) ? BarkLo : Bark))); }
			FillEllipse(Log, 31.5f, 4.5f, 2.6f, 3.6f, Rgb(200, 160, 110));
			FillEllipse(Log, 31.5f, 4.5f, 1.2f, 1.8f, Rgb(170, 130, 86));
			for (int32 X = 4; X < 28; X += 5) { Log.Set(X, 1, Rgb(96, 140, 64)); Log.Set(X + 1, 1, Rgb(120, 164, 76)); }
			Log.Outline(Rgb(50, 34, 24));
			C.AddObj(MoveTemp(Log), 84, 60, 16.f, 2.f);
		}
		C.AddObj(MakeRock(C.R, 9, 6, Rgb(130, 128, 120), true, false), 124, 58);
		C.AddObj(MakeMushroom(Rgb(210, 60, 50), Rgb(250, 240, 230)), 150, 62, 3.f, 1.f);
		C.AddObj(MakeMushroom(Rgb(200, 120, 60), Rgb(250, 230, 200)), 250, 60, 3.f, 1.f);
		C.PlaceObjs();

		C.L.Reeds = { C.V(20, 96), C.V(284, 96), C.V(298, 97) };
		AddProp(C, EDecor::Campfire, FVector2D(430.f, 150.f));
		AddProp(C, EDecor::Stump, FVector2D(80.f, 148.f));
		AddProp(C, EDecor::Boulder, FVector2D(580.f, 150.f));
		AddProp(C, EDecor::Bush, FVector2D(170.f, 136.f));
		C.L.bFireflies = true; C.L.bButterflies = true; C.L.bBirds = true;
		C.L.Current = 26.f;
		Spawn(C, -70);
	}

	/** Mountain ranges / volcano cone silhouettes against the sky. */
	struct FPeak { float X; float Top; float Slope; };
	void PaintRange(FCtx& C, const TArray<FPeak>& Peaks, int32 Base, const FColor& Lit, const FColor& Shade, const FColor* Snow, float SnowDepth, uint32 Seed)
	{
		for (int32 X = 0; X < C.TW; ++X)
		{
			float TopY = 1e9f; int32 Which = 0;
			for (int32 i = 0; i < Peaks.Num(); ++i)
			{
				const float Ty = Peaks[i].Top + FMath::Abs(float(X) - Peaks[i].X) * Peaks[i].Slope;
				if (Ty < TopY) { TopY = Ty; Which = i; }
			}
			TopY += (VNoise(float(X) / 3.f, 0.5f, Seed) - 0.5f) * 2.2f;
			const FPeak& P = Peaks[Which];
			for (int32 Y = FMath::Max(0, FMath::RoundToInt(TopY)); Y < Base; ++Y)
			{
				const bool bLeft = float(X) < P.X + (VNoise(float(Y) / 3.f, float(X) / 30.f, Seed + 1) - 0.5f) * 4.f;
				FColor Cc = bLeft ? Lit : Shade;
				if (R01(X / 2, Y, Seed) < 0.07f) { Cc = Mix(Cc, Shade, 0.6f); }
				if (Snow)
				{
					const float Line = P.Top + SnowDepth + (VNoise(float(X) / 2.5f, float(Y) / 3.f, Seed + 3) - 0.5f) * 5.f;
					const bool bGully = float(Y) < Line + 6.f && R01(X / 2, 0, Seed + 5) < 0.3f;
					if (float(Y) < Line || bGully) { Cc = bLeft ? *Snow : Mix(*Snow, Rgb(150, 170, 210), 0.45f); }
				}
				C.Img.Set(X, Y, Cc);
			}
		}
	}

	/** Banded sky from row Y0 (Top color; rows above keep it) down to row Rows - 1 (Bottom color). */
	void SkyGradient(FCtx& C, int32 Rows, const FColor& Top, const FColor& Bottom, int32 Y0 = 0)
	{
		constexpr int32 Bands = 7;
		for (int32 Y = 0; Y < Rows; ++Y)
			for (int32 X = 0; X < C.TW; ++X)
			{
				const float T = float(Y - Y0) / float(FMath::Max(1, Rows - 1 - Y0)) + (Bayer(X, Y) - 0.5f) / float(Bands);
				const int32 B = FMath::Clamp(FMath::FloorToInt(T * Bands), 0, Bands - 1);
				C.Img.Set(X, Y, Mix(Top, Bottom, float(B) / float(Bands - 1)));
			}
	}

	void BuildMountain(FCtx& C)
	{
		const FGround G{ Rgb(94, 126, 82), Rgb(114, 146, 92), Rgb(138, 166, 106), 10.f };
		MakeEdge(C, 97, 13);
		C.DockX = 178; C.DockW = 20; C.DockY0 = 92; C.DockY1 = 138;
		FillGround(C, 0, G, 23);
		SkyGradient(C, 44, Rgb(118, 176, 230), Rgb(214, 236, 250));
		const FColor Snow = Rgb(246, 250, 255);
		PaintRange(C, { { 40.f, 8.f, 0.9f }, { 118.f, 4.f, 0.8f }, { 210.f, 10.f, 0.85f }, { 290.f, 6.f, 0.9f } }, 44, Rgb(156, 174, 204), Rgb(124, 142, 178), &Snow, 9.f, 41);
		for (int32 Y = 30; Y < 44; ++Y) { for (int32 X = 0; X < C.TW; ++X) { if (Bayer(X, Y) < float(Y - 30) / 20.f) { MixPx(C.Img, X, Y, Rgb(226, 238, 248), 0.45f); } } }
		PaintRange(C, { { 70.f, 16.f, 1.1f }, { 170.f, 12.f, 1.0f }, { 258.f, 18.f, 1.2f } }, 46, Rgb(128, 130, 138), Rgb(96, 98, 110), &Snow, 7.f, 42);
		// tree line
		for (int32 i = 0; i < 70; ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Base = C.R.RandRange(42, 48), H = C.R.RandRange(5, 10);
			for (int32 j = 0; j < H; ++j)
			{
				const int32 Hw = (j * 3) / H;
				for (int32 k = -Hw; k <= Hw; ++k) { C.Img.Set(X + k, Base - H + j, k < 0 ? Rgb(62, 98, 80) : Rgb(46, 78, 66)); }
			}
		}
		// ground blend under the tree line
		for (int32 X = 0; X < C.TW; ++X) { for (int32 Y = 46; Y < 50; ++Y) { if (Bayer(X, Y) < float(50 - Y) / 5.f) { C.Img.Set(X, Y, Rgb(60, 92, 74)); } } }
		// gravel & scree patches
		for (int32 i = 0; i < 12; ++i)
		{
			const int32 CX = C.R.RandRange(0, C.TW - 1), CY = C.R.RandRange(52, 90);
			for (int32 k = 0; k < 40; ++k)
			{
				const int32 X = CX + C.R.RandRange(-8, 8), Y = CY + C.R.RandRange(-3, 3);
				if (X >= 0 && X < C.TW && Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(160, 156, 150) : Rgb(126, 122, 118)); }
			}
		}
		Tufts(C, 160, 50, 92, G);
		Flowers(C, 60, 52, 92, { Rgb(250, 250, 250), Rgb(90, 130, 240), Rgb(190, 120, 230), Rgb(255, 220, 110) }, 5);

		FillWater(C, 33);
		DrawBank(C, EBank::Stone, G);
		Pebbles(C, 50, 26, Rgb(150, 140, 120), 0.4f);
		DrawDock(C, EDock::Dark);

		for (const FIntPoint& P : { FIntPoint(10, 62), FIntPoint(32, 56), FIntPoint(264, 58), FIntPoint(292, 52), FIntPoint(312, 64), FIntPoint(150, 54) })
		{
			C.AddObj(MakePine(C.R, C.R.RandRange(34, 46), Rgb(46, 104, 78), true), P.X, P.Y, 9.f, 2.5f);
		}
		C.AddObj(MakeRock(C.R, 11, 7, Rgb(140, 140, 146), false, true), 76, 60);
		C.AddObj(MakeRock(C.R, 8, 5, Rgb(132, 132, 140), true, false), 118, 58);
		C.AddObj(MakeRock(C.R, 12, 8, Rgb(146, 144, 150), false, true), 222, 58);
		C.AddObj(MakeRock(C.R, 5, 3, Rgb(128, 128, 136), false, false), 244, 62);
		C.PlaceObjs();

		AddProp(C, EDecor::SnowBoulder, FVector2D(96.f, 156.f));
		AddProp(C, EDecor::YoungPine, FVector2D(566.f, 146.f));
		AddProp(C, EDecor::LampPost, C.V(float(C.DockX) - 7.f, 95.f));
		AddProp(C, EDecor::Signpost, FVector2D(300.f, 140.f));
		C.L.bButterflies = true; C.L.bBirds = true;
		C.L.Current = 3.f;
		Spawn(C, -64);
	}

	void BuildOcean(FCtx& C)
	{
		const FGround Sand{ Rgb(218, 188, 130), Rgb(234, 206, 148), Rgb(246, 224, 172), 12.f };
		const FGround Dune{ Rgb(116, 146, 72), Rgb(144, 172, 90), Rgb(174, 196, 110), 8.f };
		MakeEdge(C, 90, 14);
		C.DockX = 150; C.DockW = 24; C.DockY0 = 85; C.DockY1 = 146;
		FillGround(C, 0, Sand, 24);
		// dunes
		for (int32 X = 0; X < C.TW; ++X)
		{
			const int32 DE = 24 + FMath::RoundToInt(VNoise(float(X) / 14.f, 1.f, 8) * 8.f);
			for (int32 Y = 0; Y < DE; ++Y)
			{
				const float V = Fbm(float(X) / Dune.Scale, float(Y) / 6.f, 25) + (Bayer(X, Y) - 0.5f) * 0.2f;
				C.Img.Set(X, Y, V < 0.4f ? Dune.Lo : (V < 0.62f ? Dune.Base : Dune.Hi));
			}
			for (int32 Y = DE; Y < DE + 3; ++Y) { if (Bayer(X, Y) < float(DE + 3 - Y) / 4.f) { C.Img.Set(X, Y, Mix(Sand.Base, Dune.Lo, 0.5f)); } }
			if (C.R.FRand() < 0.3f)
			{
				const int32 H = C.R.RandRange(3, 6);
				for (int32 j = 0; j < H; ++j) { C.Img.Set(X + (j > H / 2 ? 1 : 0), DE + 1 - j, j > H - 2 ? Rgb(200, 206, 120) : Rgb(130, 150, 70)); }
			}
		}
		// sand fence
		for (int32 X = 0; X < C.TW; ++X)
		{
			if ((X / 40) % 3 == 2) { continue; }
			const int32 Y = 36 + FMath::RoundToInt(FMath::Sin(float(X) * 0.05f) * 1.5f);
			C.Img.Set(X, Y - 3, Rgb(120, 110, 100)); C.Img.Set(X, Y, Rgb(120, 110, 100));
			if (X % 3 == 0) { for (int32 j = -5; j <= 1; ++j) { C.Img.Set(X, Y + j, j == -5 ? Rgb(210, 170, 120) : Rgb(170, 124, 84)); } }
			if (X % 3 == 0) { MulPx(C.Img, X + 1, Y + 2, 0.85f); }
		}
		// sand ripples
		for (int32 i = 0; i < 60; ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 8), Y = C.R.RandRange(42, 80);
			for (int32 j = 0; j < 6; ++j) { if (Y < C.Edge[X + j] - 8) { MixPx(C.Img, X + j, Y + (j == 0 || j == 5 ? 1 : 0), Sand.Lo, 0.6f); } }
		}
		// shells & starfish
		for (int32 i = 0; i < 26; ++i)
		{
			const int32 X = C.R.RandRange(4, C.TW - 5), Y = C.R.RandRange(44, 84);
			if (Y >= C.Edge[X] - 4 || C.NearDock(X, 3)) { continue; }
			const int32 Kind = C.R.RandHelper(3);
			if (Kind == 0)
			{
				const FColor S = C.R.FRand() < 0.5f ? Rgb(250, 190, 200) : Rgb(250, 244, 230);
				C.Img.Set(X, Y, S); C.Img.Set(X + 1, Y, S); C.Img.Set(X - 1, Y + 1, Mul(S, 0.85f)); C.Img.Set(X, Y + 1, S); C.Img.Set(X + 1, Y + 1, Mul(S, 0.8f)); C.Img.Set(X + 2, Y + 1, Mul(S, 0.75f));
			}
			else if (Kind == 1)
			{
				const FColor S = Rgb(244, 132, 76), SL = Rgb(206, 96, 56);
				C.Img.Set(X, Y - 2, S); C.Img.Set(X, Y - 1, S); C.Img.Set(X - 2, Y, S); C.Img.Set(X - 1, Y, S); C.Img.Set(X, Y, Rgb(255, 170, 110));
				C.Img.Set(X + 1, Y, SL); C.Img.Set(X + 2, Y, SL); C.Img.Set(X - 1, Y + 1, SL); C.Img.Set(X + 1, Y + 1, SL); C.Img.Set(X - 2, Y + 2, SL); C.Img.Set(X + 2, Y + 2, SL);
			}
			else
			{
				C.Img.Set(X, Y, Rgb(240, 236, 220)); C.Img.Set(X + 1, Y, Rgb(200, 190, 170));
			}
		}

		FillWater(C, 34);
		// sand visible under the shallows
		for (int32 X = 0; X < C.TW; ++X)
		{
			for (int32 Y = C.Edge[X]; Y < C.Edge[X] + 12; ++Y)
			{
				const float T = 1.f - float(Y - C.Edge[X]) / 12.f;
				if (Bayer(X, Y) < T) { MixPx(C.Img, X, Y, Rgb(220, 206, 150), 0.22f); }
			}
		}
		DrawBank(C, EBank::Sand, Sand);
		DrawDock(C, EDock::Pier);
		// bollards at the pier end
		for (int32 BX : { C.DockX + 3, C.DockX + C.DockW - 6 })
		{
			for (int32 Y = C.DockY1 - 7; Y <= C.DockY1 - 3; ++Y)
				for (int32 X = BX; X < BX + 3; ++X) { C.Img.Set(X, Y, Y == C.DockY1 - 7 ? Rgb(120, 116, 116) : (X == BX ? Rgb(96, 90, 92) : Rgb(70, 66, 70))); }
		}

		C.AddObj(MakePalm(C.R, false), 22, 62, 12.f, 3.f);
		C.AddObj(MakePalm(C.R, false), 64, 48, 12.f, 3.f);
		C.AddObj(MakePalm(C.R, true), 262, 54, 12.f, 3.f);
		C.AddObj(MakePalm(C.R, true), 302, 60, 12.f, 3.f);
		C.AddObj(MakeBoat(), 108, 58, 16.f, 3.f);
		C.AddObj(MakeRock(C.R, 6, 4, Rgb(150, 140, 130), false, false), 212, 60);
		C.PlaceObjs();

		AddProp(C, EDecor::BeachUmbrella, FVector2D(470.f, 148.f));
		AddProp(C, EDecor::Crates, FVector2D(248.f, 160.f));
		AddProp(C, EDecor::Lifebuoy, C.V(float(C.DockX) - 7.f, 88.f));
		AddProp(C, EDecor::Barrel, FVector2D(226.f, 170.f));
		C.L.bGulls = true; C.L.bWaves = true;
		C.L.BirdColor = Rgb(250, 250, 250);
		C.L.Current = 6.f;
		Spawn(C, -70);
	}

	void BuildSwamp(FCtx& C)
	{
		const FGround G{ Rgb(58, 68, 42), Rgb(74, 86, 50), Rgb(92, 104, 60), 8.f };
		MakeEdge(C, 94, 15);
		C.DockX = 214; C.DockW = 20; C.DockY0 = 89; C.DockY1 = 130;
		FillGround(C, 0, G, 26);
		// mud puddles & moss
		for (int32 i = 0; i < 9; ++i)
		{
			const float CX = C.R.FRandRange(10.f, 310.f), CY = C.R.FRandRange(50.f, 86.f);
			const float RX = C.R.FRandRange(5.f, 10.f), RY = RX * 0.4f;
			if (CY + RY >= float(C.Edge[FMath::Clamp(int32(CX), 0, C.TW - 1)] - 4)) { continue; }
			FillEllipse(C.Img, CX, CY, RX + 1.f, RY + 1.f, Rgb(92, 74, 50));
			FillEllipse(C.Img, CX, CY, RX, RY, Rgb(52, 60, 44));
			C.Img.Set(int32(CX - RX * 0.4f), int32(CY - RY * 0.3f), Rgb(120, 140, 110));
			C.Img.Set(int32(CX - RX * 0.4f) + 1, int32(CY - RY * 0.3f), Rgb(96, 112, 90));
			C.Reserve(int32(CX - RX), int32(CY - RY), int32(RX * 2.f), int32(RY * 2.f));
		}
		for (int32 i = 0; i < 180; ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(4, 90);
			if (C.Free(X, Y) && Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(104, 124, 56) : Rgb(60, 50, 36)); }
		}
		Tufts(C, 160, 8, 90, G);

		FillWater(C, 35);
		// algae blotches & duckweed
		for (int32 X = 0; X < C.TW; ++X)
			for (int32 Y = C.Edge[X]; Y < C.TH; ++Y)
			{
				const float N = Fbm(float(X) / 14.f, float(Y) / 6.f, 77);
				if (N > 0.64f) { MixPx(C.Img, X, Y, Rgb(84, 110, 46), Bayer(X, Y) < 0.5f ? 0.4f : 0.25f); }
				if (R01(X, Y, 78) < 0.012f) { C.Img.Set(X, Y, Rgb(130, 170, 60)); }
			}
		DrawBank(C, EBank::Mud, G);
		for (int32 X : { 8, 24, 40, 60, 150, 180, 262, 282, 300, 314 }) { ReedClump(C, X, true); }
		DrawDock(C, EDock::Weathered);

		const FIntPoint Shack = C.AddObj(MakeShack(), 48, 58, 22.f, 3.f);
		C.L.Lights.Add({ FCtx::V(float(Shack.X + 38), float(Shack.Y + 18)), 44.f, Rgb(220, 240, 130), true, false, FFVRect() });
		C.L.Lights.Add({ FCtx::V(float(Shack.X + 14), float(Shack.Y + 23)), 26.f, Rgb(255, 196, 110), false, false, WindowRect(Shack, 10, 20, 8, 7) });
		C.AddObj(MakeDeadTree(C.R, true), 112, 50);
		C.AddObj(MakeDeadTree(C.R, true), 170, 42);
		C.AddObj(MakeDeadTree(C.R, true), 272, 52);
		C.AddObj(MakeDeadTree(C.R, false), 310, 40);
		C.AddObj(MakeBush(C.R, Rgb(70, 96, 52), nullptr), 140, 60);
		C.AddObj(MakeBush(C.R, Rgb(64, 90, 50), nullptr, 6), 244, 58);
		for (int32 i = 0; i < 7; ++i)
		{
			const bool bGlow = (i % 2) == 0;
			const int32 X = C.R.RandRange(90, 310), Y = C.R.RandRange(54, 64);
			C.AddObj(MakeMushroom(bGlow ? Rgb(70, 200, 220) : Rgb(170, 60, 120), bGlow ? Rgb(200, 250, 255) : Rgb(240, 200, 230)), X, Y, 3.f, 1.f);
			if (bGlow) { C.L.Lights.Add({ FCtx::V(float(X), float(Y - 4)), 16.f, Rgb(90, 230, 240), false, false, FFVRect() }); }
		}
		C.PlaceObjs();

		LilyPads(C, 7, 104, 156);
		C.L.Reeds = { C.V(16, 96), C.V(52, 97), C.V(160, 96), C.V(276, 97), C.V(306, 96) };
		C.L.bCattails = true;
		AddProp(C, EDecor::DeadStump, FVector2D(150.f, 150.f));
		AddProp(C, EDecor::GlowShrooms, FVector2D(566.f, 142.f));
		AddProp(C, EDecor::GlowShrooms, FVector2D(330.f, 134.f));
		AddProp(C, EDecor::LampPost, C.V(float(C.DockX) - 7.f, 92.f));
		C.L.bFireflies = true; C.L.bFog = true; C.L.bBubbles = true; C.L.bBirds = true;
		C.L.BirdColor = Rgb(40, 36, 44);
		C.L.Glint = Rgb(190, 214, 170);
		C.L.Foam = Rgb(190, 210, 170);
		C.L.FogColor = Rgb(196, 214, 190);
		C.L.Current = 2.f;
		Spawn(C, -60);
	}

	void BuildVolcano(FCtx& C)
	{
		const FGround G{ Rgb(46, 40, 48), Rgb(62, 54, 62), Rgb(82, 72, 80), 9.f };
		MakeEdge(C, 96, 16);
		C.DockX = 166; C.DockW = 22; C.DockY0 = 91; C.DockY1 = 136;
		FillGround(C, 0, G, 27);
		SkyGradient(C, 44, Rgb(36, 18, 34), Rgb(126, 46, 36));
		// smoke clouds in the sky
		for (int32 i = 0; i < 16; ++i)
		{
			const float CX = C.R.FRandRange(0.f, 320.f), CY = C.R.FRandRange(4.f, 26.f);
			FillEllipse(C.Img, CX, CY, C.R.FRandRange(8.f, 16.f), C.R.FRandRange(2.f, 4.f), Rgb(70, 50, 56, 150), true);
		}
		PaintRange(C, { { 60.f, 26.f, 0.8f }, { 280.f, 24.f, 0.7f } }, 46, Rgb(66, 44, 54), Rgb(50, 34, 44), nullptr, 0.f, 51);
		PaintRange(C, { { 184.f, 10.f, 1.0f } }, 46, Rgb(74, 58, 66), Rgb(50, 40, 48), nullptr, 0.f, 52);
		// glowing crater & lava streams running down the cone
		FillEllipse(C.Img, 184.5f, 12.5f, 4.5f, 1.8f, Rgb(236, 96, 34));
		FillEllipse(C.Img, 184.5f, 12.5f, 2.5f, 1.f, Rgb(255, 222, 120));
		for (int32 s = 0; s < 3; ++s)
		{
			float X = 181.f + float(s) * 3.f;
			for (int32 Y = 13; Y < 44; ++Y)
			{
				X += (VNoise(float(Y) / 4.f, float(s) * 3.f, 61) - 0.5f) * 1.6f + (float(s) - 1.f) * 0.55f;
				const int32 IX = FMath::RoundToInt(X);
				if (Y < 12 + FMath::Abs(IX - 184) + 2) { continue; }   // stay on the cone surface
				C.Img.Set(IX, Y, Rgb(255, 120, 40)); C.Img.Set(IX + 1, Y, Rgb(200, 70, 30));
				if (Y % 3 == 0) { C.Img.Set(IX, Y, Rgb(255, 214, 110)); }
			}
		}
		C.L.Lights.Add({ FCtx::V(184.f, 12.f), 60.f, Rgb(255, 110, 40), true, true, FFVRect() });
		C.L.Smokes.Add(FCtx::V(184.f, 8.f));
		for (int32 X = 0; X < C.TW; ++X) { for (int32 Y = 44; Y < 50; ++Y) { if (Bayer(X, Y) < float(50 - Y) / 7.f) { C.Img.Set(X, Y, Rgb(40, 32, 40)); } } }
		// glowing cracks
		for (int32 i = 0; i < 9; ++i)
		{
			int32 X = C.R.RandRange(6, C.TW - 6), Y = C.R.RandRange(52, 86);
			const int32 Len = C.R.RandRange(8, 18);
			const FIntPoint Start(X, Y);
			for (int32 j = 0; j < Len; ++j)
			{
				if (Y >= C.Edge[FMath::Clamp(X, 0, C.TW - 1)] - 4 || C.NearDock(X, 2)) { break; }
				C.Img.Set(X, Y, j % 4 == 1 ? Rgb(255, 214, 100) : Rgb(236, 96, 34));
				MixPx(C.Img, X, Y - 1, Rgb(140, 60, 40), 0.5f);
				X += C.R.RandRange(0, 1) * (i % 2 ? 1 : -1);
				Y += C.R.RandRange(0, 1);
				if (C.R.FRand() < 0.5f) { X += (i % 2 ? 1 : -1); }
			}
			if (i < 5) { C.L.Lights.Add({ FCtx::V(float(Start.X + X) * 0.5f, float(Start.Y + Y) * 0.5f), 18.f, Rgb(255, 110, 40), true, true, FFVRect() }); }
		}
		// ash specks
		for (int32 i = 0; i < 160; ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(48, 92);
			if (Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(100, 92, 98) : Rgb(36, 30, 36)); }
		}

		FillWater(C, 36);
		// crust on the lava
		for (int32 X = 0; X < C.TW; ++X)
			for (int32 Y = C.Edge[X] + 3; Y < C.TH; ++Y)
			{
				const float N = Fbm(float(X) / 12.f, float(Y) / 5.f, 88);
				if (N > 0.66f) { C.Img.Set(X, Y, N > 0.7f ? Rgb(52, 30, 30) : Rgb(96, 40, 26)); }
				else if (N > 0.63f) { MixPx(C.Img, X, Y, Rgb(255, 230, 120), 0.5f); }
			}
		DrawBank(C, EBank::Basalt, G);
		DrawDock(C, EDock::Rock);

		for (const FIntPoint& P : { FIntPoint(40, 58), FIntPoint(128, 62), FIntPoint(250, 56), FIntPoint(302, 62) })
		{
			C.AddObj(MakeCrystal(C.R, C.R.RandRange(12, 20), true), P.X, P.Y, 6.f, 2.f);
			C.L.Lights.Add({ FCtx::V(float(P.X), float(P.Y - 8)), 18.f, Rgb(200, 110, 255), false, false, FFVRect() });
		}
		C.AddObj(MakeRock(C.R, 10, 6, Rgb(70, 62, 72), false, false), 84, 56);
		C.AddObj(MakeRock(C.R, 8, 5, Rgb(66, 58, 68), false, false), 214, 60);
		C.AddObj(MakeRock(C.R, 6, 4, Rgb(74, 64, 74), false, false), 12, 64);
		C.PlaceObjs();

		AddProp(C, EDecor::Brazier, C.V(float(C.DockX) - 8.f, 95.f));
		AddProp(C, EDecor::Brazier, C.V(float(C.DockX + C.DockW) + 8.f, 95.f));
		AddProp(C, EDecor::Crystal, FVector2D(90.f, 156.f));
		AddProp(C, EDecor::BasaltRock, FVector2D(560.f, 150.f));
		AddProp(C, EDecor::Crystal, FVector2D(600.f, 170.f));
		C.L.bRockPier = true;
		C.L.bLava = true; C.L.bEmbers = true;
		C.L.Glint = Rgb(255, 236, 140);
		C.L.Foam = Rgb(255, 214, 110);
		C.L.Current = 2.f;
		Spawn(C, -70);
	}

	// =========================================================================================
	// Portrait compositions (phones held upright). The texture is (180 + 2*PortBleedX) x (Hv + 2*PortBleedY) texels:
	// the visible 360 x VH canvas sits in the middle and the bleed around it is painted too, so letterboxed windows
	// and the space under the UI strips always show the world. The shoreline row is the exact middle of the visible
	// screen (FVScene aligns it to VH / 2). Land above: a far backdrop that starts under the 32 px info strip (and
	// continues up into the bleed), scenery, then the walkable band just above the shore. Water below, down to the
	// bottom edge (the big button dock is drawn over it); the dock reaches about half way to the button dock.
	// Baked objects always stand above the walkable band, so the y-sorted player is drawn in front of them.
	// =========================================================================================
	FFVImage PropImage(EDecor D);   // decor sprite painter (below)

	struct FPFrame
	{
		int32 X0 = PortBleedX;  // first visible column (canvas x = 0)
		int32 VisW = PortVisW;  // visible width in texels
		int32 Top = PortBleedY; // first visible row (canvas y = 0, nominal)
		int32 Hud = 48;         // first row under the top info strip
		int32 WalkTop = 166;    // top row of the walkable land band
		int32 Shore = 232;      // shoreline row = the middle of the visible screen
		int32 WaterEnd = 386;   // deepest water row still above the bottom button dock
		int32 Bottom = 432;     // canvas y = VH (nominal)
		int32 OX = 0;           // hotfix 0.61 (the wide pond): side strip in texels; X0 / VisW = the whole content then
		/** Visible-frame texel X (0 = left screen edge, 180 = right edge) -> texture X. The wide pond: the old frame
		    (its strips are -OX..0 and 180..180+OX). */
		int32 X(float Vx) const { return X0 + OX + FMath::RoundToInt(Vx); }
		/** Visible-frame virtual X (0..360) -> texture-space virtual X. */
		float VX(float Vx) const { return float(X0 + OX) * 2.f + Vx; }
		int32 Right() const { return X0 + VisW; }
		/** Row at fraction T of the visible backdrop (0 = just under the info strip, 1 = top of the walkable band). */
		int32 Vis(float T) const { return Hud + FMath::RoundToInt(T * float(WalkTop - Hud)); }
		/** Row at fraction T of the water (0 = shoreline, 1 = deepest row above the button dock). */
		int32 Depth(float T) const { return Shore + FMath::RoundToInt(T * float(WaterEnd - Shore)); }
	};

	FPFrame PortraitFrame(FCtx& C)
	{
		FPFrame F;
		if (C.ContentW > 0)
		{
			// hotfix 0.61: the wide pond — X0 / VisW / Right() = the content (old frame + a side strip on each side);
			// X() / VX() keep addressing the old 360 px frame (OX texels right of the content's left edge)
			F.X0 = FMath::Clamp((C.TW - C.ContentW) / 2, 0, C.TW / 4);
			F.VisW = C.TW - 2 * F.X0;
			F.OX = C.OX;
		}
		else
		{
			F.X0 = FMath::Clamp((C.TW - PortVisW) / 2, 0, C.TW / 4);
			F.VisW = C.TW - 2 * F.X0;
		}
		F.Top = FMath::Clamp(PortBleedY, 0, C.TH / 6);
		const int32 Hv = C.TH - 2 * F.Top;
		F.Bottom = F.Top + Hv;
		F.Shore = F.Top + Hv / 2;
		F.Hud = F.Top + FMath::CeilToInt(FMath::Max(FV::TopBarH, 24.f) * 0.5f);
		const int32 Band = FMath::Clamp(FMath::RoundToInt(float(Hv / 2) * 0.33f), 56, 68);   // 112..136 virtual px to walk in
		F.WalkTop = F.Shore - Band;
		F.WaterEnd = F.Bottom - FMath::CeilToInt(FMath::Max(FV::BottomBarH, 16.f) * 0.5f) - 4;
		F.WaterEnd = FMath::Max(F.WaterEnd, F.Shore + 40);
		C.WalkTopRow = F.WalkTop;
		C.WaterDeepRow = FMath::Min(C.TH, F.WaterEnd + 12);
		C.VisX0 = F.X0;
		C.VisX1 = F.Right();
		C.VisY0 = F.Hud;
		return F;
	}

	void PortraitDock(FCtx& C, const FPFrame& F, float Reach)
	{
		C.DockY0 = F.Shore - 5;
		C.DockY1 = FMath::Min(F.Depth(Reach), F.WaterEnd - 16);   // never under the bottom button dock
	}

	void PortraitSpawn(FCtx& C, const FPFrame& F, int32 SideOffset)
	{
		Spawn(C, SideOffset);
		C.L.Spawn.X = FMath::Clamp(float(C.L.Spawn.X), F.VX(40.f), F.VX(320.f));
	}

	/** Count tuned for the 320x180 landscape image, scaled to this image's area. */
	int32 Scaled(const FCtx& C, int32 N)
	{
		// (hotfix 0.61: the base range of a wide pond, so the old composition keeps its random draws)
		return FMath::Max(1, FMath::RoundToInt(float(N) * float((C.BaseX1() - C.NX) * C.TH) / float(BgW * BgH)));
	}

	/** Scenery in the side bleeds (seen in letterboxed windows). Make(R, Index) builds each object. Objects standing
	    at the height of the walkable band stay entirely inside the bleed (the player walks in front of baked art).
	    RightPad (hotfix 0.61, texels): the camera of the wide pond over-scrolls right by the HUD pad — band-height
	    objects of the right side stand beyond it (same random draws). */
	template <typename FMake>
	void SideScenery(FCtx& C, const FPFrame& F, int32 PerSide, FMake&& Make, int32 RightPad = 0)
	{
		if (F.X0 < 12) { return; }
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (int32 i = 0; i < PerSide; ++i)
			{
				FFVImage Img = Make(C.R, i + Side * PerSide);
				if (!Img.IsValid()) { continue; }
				const float T = (float(i) + C.R.FRandRange(0.2f, 0.8f)) / float(PerSide);
				const int32 Base = FMath::RoundToInt(FMath::Lerp(float(F.Top + 16), float(F.Shore - 8), T));
				const int32 Half = Img.W / 2 + 1;
				const bool bInBand = Base >= F.WalkTop - 2;
				int32 CX;
				if (Side == 0) { CX = bInBand ? F.X0 - Half - C.R.RandRange(1, 6) : C.R.RandRange(4, F.X0 - 4); }
				else { CX = bInBand ? F.Right() + RightPad + Half + C.R.RandRange(1, 6) : C.R.RandRange(F.Right() + 4, C.TW - 4); }
				C.AddObj(MoveTemp(Img), CX, Base);
			}
		}
	}

	/** Dense tree canopy along the top edge (forest border in the top bleed, peeking out under the info strip). */
	void CanopyBand(FCtx& C, int32 Rows, const FColor& Leaf, uint32 Seed, bool bMoss)
	{
		FFVImage Layer(C.TW, Rows + 16);
		const FPal4 P = LeafPal(Leaf);
		// hotfix 0.61: the wide pond paints the old composition shifted by NX (same draws, same hashes), then the
		// blobs of the new outer margin with a stream of their own
		const int32 NX = C.NX, BaseW = C.BaseX1() - C.NX;
		for (int32 Y = 0; Y < Rows - 4; ++Y)
		{
			for (int32 X = 0; X < C.TW; ++X) { Layer.Set(X, Y, R01((X - NX) >> 1, Y >> 1, Seed) < 0.3f ? P.Lo : P.Deep); }
		}
		auto BlobA = [&](int32 i)
		{
			const float CX = float(NX + i * 9) + C.R.FRandRange(-2.f, 2.f);
			PaintBlob(Layer, CX, float(Rows) * C.R.FRandRange(0.3f, 0.6f), C.R.FRandRange(7.f, 9.f), C.R.FRandRange(6.f, 8.f), P, Seed + uint32(i) * 7, 0.18f, 0.35f, NX);
		};
		auto BlobB = [&](int32 i)
		{
			const float CX = float(NX + i * 12 + 5) + C.R.FRandRange(-3.f, 3.f);
			PaintBlob(Layer, CX, float(Rows) + C.R.FRandRange(-1.f, 3.f), C.R.FRandRange(6.f, 8.5f), C.R.FRandRange(5.f, 7.f), P, Seed + 101 + uint32(i) * 5, 0.18f, 0.35f, NX);
		};
		for (int32 i = 0; i * 9 < BaseW + 10; ++i) { BlobA(i); }
		for (int32 i = 0; i * 12 < BaseW + 12; ++i) { BlobB(i); }
		if (NX > 0)
		{
			FSideStream Side(C, int32(Seed) * 31 + 7);
			for (int32 i = -1; NX + i * 9 > -10; --i) { BlobA(i); }
			for (int32 i = -1; NX + i * 12 + 5 > -10; --i) { BlobB(i); }
			for (int32 i = (BaseW + 10 + 8) / 9; NX + i * 9 < C.TW + 10; ++i) { BlobA(i); }
			for (int32 i = (BaseW + 12 + 11) / 12; NX + i * 12 + 5 < C.TW + 12; ++i) { BlobB(i); }
		}
		TArray<int32> Bottom;
		Bottom.Init(-1, C.TW);
		for (int32 X = 0; X < C.TW; ++X)
		{
			for (int32 Y = Layer.H - 1; Y >= 0; --Y) { if (Layer.Get(X, Y).A > 0) { Bottom[X] = Y; break; } }
		}
		if (bMoss)
		{
			const FColor M0 = Rgb(104, 128, 64), M1 = Rgb(76, 98, 48);
			for (int32 X = NX + 1; X < NX + BaseW - 1; X += C.R.RandRange(3, 6))
			{
				const int32 Len = C.R.RandRange(3, 8);
				for (int32 j = 1; j <= Len; ++j) { Layer.Set(X, Bottom[X] + j, (j % 2) ? M0 : M1); }
			}
		}
		Layer.Outline(Mix(P.Deep, Rgb(10, 20, 24), 0.5f));
		for (int32 X = 0; X < C.TW; ++X)
		{
			for (int32 k = 2; k <= 5; ++k)
			{
				if (k <= 3 || Bayer(X - NX, Bottom[X] + k) < 0.5f) { MulPx(C.Img, X, Bottom[X] + k, 0.8f); }
			}
		}
		C.Img.Blit(Layer, 0, 0);
		C.Reserve(0, 0, C.TW, Rows + 10);
	}

	void PaintPath(FCtx& C, const TArray<FVector2D>& Way, float Width)
	{
		const FColor PLo = Rgb(152, 114, 74), PBase = Rgb(180, 140, 94), PHi = Rgb(204, 166, 116);
		for (int32 i = 0; i + 1 < Way.Num(); ++i)
		{
			const float Len = FMath::Max(0.01f, float(FVector2D::Distance(Way[i], Way[i + 1])));
			for (float s = 0.f; s <= Len; s += 0.5f)
			{
				const FVector2D P = FMath::Lerp(Way[i], Way[i + 1], s / Len);
				const float Rad = Width + VNoise((P.X - float(C.NX)) / 6.f, P.Y / 6.f, 5) * 1.4f;   // (hotfix 0.61: FCtx::NX)
				for (int32 Y = IFloor(P.Y - Rad - 1.0); Y <= ICeil(P.Y + Rad + 1.0); ++Y)
				{
					for (int32 X = IFloor(P.X - Rad - 1.0); X <= ICeil(P.X + Rad + 1.0); ++X)
					{
						const float D = FVector2D::Distance(FVector2D(float(X) + 0.5f, float(Y) + 0.5f), P);
						if (D > Rad + 0.8f || Y >= C.Edge[FMath::Clamp(X, 0, C.TW - 1)] - 3) { continue; }
						if (D > Rad) { if (Bayer(X - C.NX, Y) < 0.5f && C.Free(X, Y)) { C.Img.Set(X, Y, PLo); } continue; }
						const float N = R01(X - C.NX, Y, 71);
						C.Img.Set(X, Y, N < 0.2f ? PLo : (N > 0.85f ? PHi : PBase));
						C.Reserve(X, Y, 1, 1);
					}
				}
			}
		}
	}

	/** Wooden fence along a row (Y = feet row of the posts). */
	void FenceH(FCtx& C, int32 X0, int32 X1, int32 Y)
	{
		const FColor Post = Rgb(150, 104, 62), PostHi = Rgb(196, 148, 96), Rail = Rgb(176, 128, 80);
		for (int32 X = X0; X <= X1; ++X)
		{
			C.Img.Set(X, Y - 8, Rail); C.Img.Set(X, Y - 7, Mul(Rail, 0.7f));
			C.Img.Set(X, Y - 4, Rail); C.Img.Set(X, Y - 3, Mul(Rail, 0.7f));
		}
		for (int32 X = X0; X <= X1; X += 8)
		{
			for (int32 Yy = Y - 11; Yy <= Y; ++Yy) { C.Img.Set(X, Yy, Yy == Y - 11 ? PostHi : Post); C.Img.Set(X + 1, Yy, Mul(Post, 0.75f)); }
			ShadeEllipse(C.Img, float(X) + 2.f, float(Y) + 0.5f, 2.f, 1.f, 0.75f);
		}
		C.Reserve(X0 - 1, Y - 12, X1 - X0 + 3, 14);
	}

	/** Tilled vegetable beds (cabbages, carrots, tomatoes). */
	void Garden(FCtx& C, int32 X0, int32 Y0, int32 GW, int32 GH)
	{
		const FColor Soil = Rgb(122, 84, 54), SoilLo = Rgb(94, 62, 40), SoilHi = Rgb(150, 108, 70);
		for (int32 Y = Y0; Y < Y0 + GH; ++Y)
		{
			for (int32 X = X0; X < X0 + GW; ++X)
			{
				const int32 R5 = (Y - Y0) % 5;
				FColor Cc = R5 == 4 ? SoilLo : (R5 == 0 ? SoilHi : Soil);
				if (R01(X - C.NX, Y, 91) < 0.08f) { Cc = SoilLo; }
				if (X == X0 || X == X0 + GW - 1 || Y == Y0 + GH - 1) { Cc = Mix(Cc, SoilLo, 0.6f); }
				C.Img.Set(X, Y, Cc);
			}
		}
		for (int32 X = X0; X < X0 + GW; ++X) { MulPx(C.Img, X, Y0 + GH, 0.8f); }
		for (int32 Y = Y0 + 2; Y < Y0 + GH - 2; Y += 5)
		{
			const int32 Kind = ((Y - Y0) / 5) % 3;
			for (int32 X = X0 + 3; X < X0 + GW - 3; X += 5)
			{
				if (Kind == 0)
				{
					C.Img.Set(X, Y - 1, Rgb(150, 214, 112));
					C.Img.Set(X - 1, Y, Rgb(120, 190, 90)); C.Img.Set(X, Y, Rgb(164, 224, 124)); C.Img.Set(X + 1, Y, Rgb(96, 160, 76));
					C.Img.Set(X - 1, Y + 1, Rgb(76, 136, 64)); C.Img.Set(X, Y + 1, Rgb(96, 160, 76)); C.Img.Set(X + 1, Y + 1, Rgb(60, 110, 52));
				}
				else if (Kind == 1)
				{
					C.Img.Set(X - 1, Y - 1, Rgb(70, 140, 60)); C.Img.Set(X, Y - 1, Rgb(96, 176, 72)); C.Img.Set(X + 1, Y - 1, Rgb(110, 190, 80));
					C.Img.Set(X, Y, Rgb(236, 130, 50)); C.Img.Set(X, Y + 1, Rgb(196, 98, 40));
				}
				else
				{
					C.Img.Set(X, Y - 2, Rgb(84, 156, 66)); C.Img.Set(X, Y - 1, Rgb(70, 140, 60)); C.Img.Set(X, Y, Rgb(62, 122, 52));
					C.Img.Set(X - 1, Y - 1, Rgb(230, 64, 52)); C.Img.Set(X + 1, Y, Rgb(214, 52, 44)); C.Img.Set(X, Y + 1, Rgb(56, 108, 48));
				}
			}
		}
		C.Reserve(X0 - 1, Y0 - 1, GW + 2, GH + 2);
	}

	FFVImage MakeWell()
	{
		const int32 W = 22, H = 30;
		FFVImage T(W, H);
		const FColor Stone = Rgb(150, 146, 140), StoneLo = Rgb(112, 106, 104), StoneHi = Rgb(188, 184, 174);
		const FColor RoofHi = Rgb(210, 104, 70), Roof = Rgb(174, 74, 54), RoofLo = Rgb(130, 52, 42);
		for (int32 Y = 18; Y <= 29; ++Y)
		{
			for (int32 X = 1; X <= 20; ++X)
			{
				const int32 Row = (Y - 18) / 3;
				const bool bJoint = ((Y - 18) % 3 == 2) || ((X + Row * 3) % 6 == 0);
				FColor Cc = bJoint ? StoneLo : Stone;
				if (X <= 2) { Cc = Mix(Cc, StoneHi, 0.5f); }
				else if (X >= 19) { Cc = Mul(Cc, 0.8f); }
				T.Set(X, Y, Cc);
			}
		}
		FillEllipse(T, 11.f, 18.5f, 10.f, 3.f, StoneHi);
		FillEllipse(T, 11.f, 18.8f, 7.5f, 1.9f, Rgb(40, 60, 84));
		T.Set(8, 18, Rgb(96, 136, 176)); T.Set(9, 18, Rgb(70, 104, 140));
		for (int32 Y = 6; Y <= 19; ++Y) { T.Set(2, Y, BarkHi); T.Set(3, Y, Bark); T.Set(18, Y, Bark); T.Set(19, Y, BarkLo); }
		for (int32 X = 3; X <= 18; ++X) { T.Set(X, 8, BarkLo); }
		for (int32 Y = 9; Y <= 12; ++Y) { T.Set(11, Y, Rgb(206, 186, 136)); }
		for (int32 Y = 13; Y <= 16; ++Y)
		{
			for (int32 X = 9; X <= 13; ++X) { T.Set(X, Y, Y == 13 ? Rgb(150, 150, 160) : (X == 9 ? Rgb(176, 126, 76) : (X == 13 ? Rgb(112, 76, 44) : Rgb(140, 96, 56)))); }
		}
		for (int32 Y = 0; Y <= 6; ++Y)
		{
			const int32 Half = 2 + (Y * 3) / 2;
			for (int32 X = 11 - Half; X <= 10 + Half; ++X)
			{
				FColor Cc = (Y % 2 == 0) ? Roof : RoofHi;
				if (X <= 11 - Half + 1) { Cc = Mix(Cc, RoofHi, 0.5f); }
				else if (X >= 10 + Half - 1 || X > 11) { Cc = (Y % 2 == 0) ? RoofLo : Roof; }
				T.Set(X, Y, Cc);
			}
		}
		T.Outline(Rgb(60, 36, 28));
		return T;
	}

	/** Striped lighthouse; the lamp room sits 8 texels below the top. */
	FFVImage MakeLighthouse(int32 H)
	{
		H = FMath::Clamp(H, 40, 64);
		const int32 W = 22, CX = 11;
		FFVImage T(W, H);
		const int32 TowerTop = 13, Base = H - 1;
		for (int32 Y = TowerTop; Y <= Base; ++Y)
		{
			const float F = float(Y - TowerTop) / float(FMath::Max(1, Base - TowerTop));
			const int32 Half = FMath::RoundToInt(4.f + F * 3.f);
			const bool bRed = ((Y - TowerTop) / 7) % 2 == 0;
			const FColor B0 = bRed ? Rgb(212, 64, 56) : Rgb(244, 240, 230);
			for (int32 X = CX - Half; X < CX + Half; ++X)
			{
				const float S = float(X - (CX - Half)) / float(FMath::Max(1, 2 * Half - 1));
				T.Set(X, Y, S < 0.25f ? Mix(B0, White, 0.3f) : (S > 0.7f ? Mul(B0, 0.72f) : B0));
			}
		}
		for (int32 Y = Base - 6; Y <= Base; ++Y)
		{
			for (int32 X = CX - 2; X <= CX + 1; ++X) { T.Set(X, Y, Y == Base - 6 ? Rgb(70, 50, 40) : (X == CX - 2 ? Rgb(120, 84, 56) : Rgb(96, 66, 44))); }
		}
		for (int32 Y = TowerTop + 11; Y <= TowerTop + 13; ++Y) { T.Set(CX - 1, Y, Rgb(60, 82, 112)); T.Set(CX, Y, Rgb(44, 60, 86)); }
		for (int32 X = CX - 7; X < CX + 7; ++X) { T.Set(X, TowerTop - 1, Rgb(84, 76, 80)); T.Set(X, TowerTop, Rgb(52, 46, 50)); }
		for (int32 X = CX - 7; X < CX + 7; X += 2) { T.Set(X, TowerTop - 2, Rgb(84, 76, 80)); }
		for (int32 Y = 6; Y <= TowerTop - 2; ++Y)
		{
			for (int32 X = CX - 3; X < CX + 3; ++X)
			{
				const bool bFrame = X == CX - 3 || X == CX + 2 || X == CX;
				T.Set(X, Y, bFrame ? Rgb(52, 46, 50) : (Y <= 7 ? Rgb(255, 250, 210) : Rgb(255, 226, 130)));
			}
		}
		for (int32 Y = 1; Y <= 5; ++Y)
		{
			const int32 Half = FMath::Min(4, Y);
			for (int32 X = CX - Half; X < CX + Half; ++X) { T.Set(X, Y, X < CX ? Rgb(222, 80, 64) : Rgb(170, 48, 44)); }
		}
		T.Set(CX - 1, 0, Rgb(70, 64, 70)); T.Set(CX, 0, Rgb(50, 44, 50));
		T.Outline(Rgb(50, 34, 40));
		return T;
	}

	FFVImage MakeLog()
	{
		FFVImage Log(34, 9);
		for (int32 X = 2; X < 32; ++X)
		{
			for (int32 Y = 1; Y < 8; ++Y) { Log.Set(X, Y, Y <= 2 ? BarkHi : (Y >= 6 ? BarkLo : ((X % 7 == 0) ? BarkLo : Bark))); }
		}
		FillEllipse(Log, 31.5f, 4.5f, 2.6f, 3.6f, Rgb(200, 160, 110));
		FillEllipse(Log, 31.5f, 4.5f, 1.2f, 1.8f, Rgb(170, 130, 86));
		for (int32 X = 4; X < 28; X += 5) { Log.Set(X, 1, Rgb(96, 140, 64)); Log.Set(X + 1, 1, Rgb(120, 164, 76)); }
		Log.Outline(Rgb(50, 34, 24));
		return Log;
	}

	/** Puffy pixel cloud painted straight into the sky. */
	void Cloud(FCtx& C, float CX, float CY, float W)
	{
		const FColor Hi = Rgb(255, 255, 255), Md = Rgb(238, 245, 252), Lo = Rgb(204, 222, 242);
		FillEllipse(C.Img, CX, CY + W * 0.06f, W * 0.5f, W * 0.13f, Lo);
		FillEllipse(C.Img, CX - W * 0.22f, CY, W * 0.22f, W * 0.15f, Md);
		FillEllipse(C.Img, CX + W * 0.12f, CY - W * 0.07f, W * 0.26f, W * 0.2f, Md);
		FillEllipse(C.Img, CX + W * 0.07f, CY - W * 0.13f, W * 0.16f, W * 0.12f, Hi);
		FillEllipse(C.Img, CX - W * 0.26f, CY - W * 0.05f, W * 0.12f, W * 0.08f, Hi);
		for (int32 X = IFloor(CX - W * 0.45f); X <= ICeil(CX + W * 0.45f); ++X) { if (Bayer(X, 0) < 0.6f) { MixPx(C.Img, X, IRound(CY + W * 0.19f), Lo, 0.5f); } }
	}

	/** Rock sticking out of the water with a foam collar (optional foam trail to the right = current). */
	void WaterRock(FCtx& C, int32 X, int32 Y, int32 RX, int32 RY, const FColor& Base, bool bMoss, bool bSnow, const FColor& FoamC, bool bTrail)
	{
		FFVImage Rock = MakeRock(C.R, RX, RY, Base, bMoss, bSnow);
		const int32 X0 = X - Rock.W / 2, Y0 = Y - Rock.H / 2;
		ShadeEllipse(C.Img, float(X) + 1.5f, float(Y0 + Rock.H) - 0.5f, float(RX) + 2.f, 1.8f, 0.8f);
		C.Img.Blit(Rock, X0, Y0);
		const int32 FoamY = Y0 + Rock.H;
		for (int32 Xx = X - RX - 3; Xx <= X + RX + 4; ++Xx)
		{
			MixPx(C.Img, Xx, FoamY, FoamC, 0.55f);
			if ((Xx & 1) == 0) { MixPx(C.Img, Xx + 2, FoamY + 1, FoamC, 0.3f); }
		}
		if (bTrail)
		{
			for (int32 j = 1; j < 14; ++j) { if (Bayer(X + RX + 3 + j, Y) < 0.5f) { MixPx(C.Img, X + RX + 2 + j, Y + (j % 3) - 1, FoamC, 0.4f - float(j) * 0.025f); } }
		}
	}

	/** Log floating in the water (swamp). */
	void FloatingLog(FCtx& C, int32 X, int32 Y, bool bFlip)
	{
		const FFVImage Log = MakeLog();
		ShadeEllipse(C.Img, float(X) + 1.f, float(Y) + 3.f, 17.f, 2.5f, 0.78f);
		C.Img.Blit(Log, X - Log.W / 2, Y - Log.H / 2, bFlip);
		for (int32 Xx = X - 19; Xx <= X + 19; ++Xx) { if (Bayer(Xx, Y) < 0.5f) { MixPx(C.Img, Xx, Y + Log.H / 2 + 1, Rgb(200, 220, 180), 0.3f); } }
	}

	/** Weeds under the water surface, fading with depth (down into the bottom bleed). */
	void UnderwaterWeeds(FCtx& C, const FPFrame& F, int32 Count, const FColor& Col, float Amount, int32 XMin = -1, int32 XMax = -1)
	{
		if (XMin < 0) { XMin = C.NX + 2; }   // (hotfix 0.61: the base range, see FCtx::NX)
		if (XMax < 0) { XMax = C.BaseX1() - 3; }
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 X = C.R.RandRange(XMin, XMax);
			if (C.NearDock(X, 4)) { continue; }
			const int32 Y = C.Edge[X] + C.R.RandRange(5, FMath::Max(6, C.TH - F.Shore - 6));
			const int32 Hh = C.R.RandRange(2, 5);
			const float Fade = 1.f - 0.55f * FMath::Clamp(float(Y - F.Shore) / float(FMath::Max(1, F.WaterEnd - F.Shore)), 0.f, 1.f);
			for (int32 j = 0; j < Hh; ++j) { MixPx(C.Img, X + ((j / 2) % 2), Y - j, Col, Amount * Fade); }
		}
	}

	FVector2D PV(float X, float Y) { return FVector2D(X, Y); }

	void BuildPondP(FCtx& C)
	{
		const FGround G{ Rgb(82, 150, 64), Rgb(106, 176, 72), Rgb(136, 200, 88), 11.f };
		// Hotfix 0.61: the wide pond (SideWiden) — F.X / F.VX keep addressing the old 360 px frame, the new strips are
		// vx -OX..0 and 180..180+OX (the old painted bleed, the scatter stays pixel-identical through FCtx::NX); the
		// strip art below is painted from streams of their own (FSideStream).
		const FPFrame F = PortraitFrame(C);
		const bool bWide = C.OX > 0;
		MakeEdge(C, F.Shore, 11);
		C.DockX = F.X(104); C.DockW = 20;
		PortraitDock(C, F, 0.5f);
		FillGround(C, 0, G, 21);

		const int32 HutX = F.X(44), HutBase = F.Vis(0.60f);
		const int32 GardenX = F.X(112), GardenY = F.Vis(0.40f), GardenW = 58, GardenH = FMath::Max(11, F.Vis(0.62f) - GardenY);
		{
			const float Span = float(F.Shore - HutBase);
			TArray<FVector2D> Way = { PV(HutX - 1, HutBase + 2), PV(HutX + 1, HutBase + 9), PV(HutX + 14, HutBase + Span * 0.38f),
				PV(HutX + 36, HutBase + Span * 0.62f), PV(C.DockX + 2, F.Shore - 12), PV(C.DockX + 10, F.Shore - 4) };
			PaintPath(C, Way, 3.2f);
			// a side path leaves the screen to the left (towards the village)
			TArray<FVector2D> Side = { PV(HutX - 2, HutBase + 6), PV(F.X(8), HutBase + 14), PV(F.X(-44), HutBase + 20) };
			PaintPath(C, Side, 2.6f);
			for (int32 i = 0; i < 40; ++i)
			{
				const int32 X = C.R.RandRange(F.X(36), C.DockX + 16), Y = C.R.RandRange(HutBase, F.Shore - 4);
				if (!C.Free(X, Y) && Y < C.Edge[X] - 3) { C.Img.Set(X, Y, Rgb(160, 150, 140)); C.Img.Set(X + 1, Y, Rgb(120, 112, 106)); }
			}
		}
		Garden(C, GardenX, GardenY, GardenW, GardenH);
		Tufts(C, Scaled(C, 260), 8, F.Shore - 2, G);
		Flowers(C, Scaled(C, 90), 10, F.Shore - 2, { Rgb(250, 110, 130), Rgb(255, 220, 90), Rgb(250, 250, 250), Rgb(170, 140, 250), Rgb(255, 160, 80) }, 6);
		if (bWide && C.NX > 3)
		{
			// grass tufts of the new outer bleed columns (the right ones are seen in the HUD pad at the right end)
			FSideStream Side(C, 0x61E2);
			const int32 N = FMath::Max(1, Scaled(C, 260) * C.NX / FMath::Max(1, C.BaseX1() - C.NX));
			Tufts(C, N, 8, F.Shore - 2, G, 1, C.NX - 3);
			Tufts(C, N, 8, F.Shore - 2, G, C.BaseX1() + 2, C.TW - 2);
		}

		FillWater(C, 31);
		DrawBank(C, EBank::Grass, G);
		Pebbles(C, 44, 16, Rgb(96, 120, 90), 0.35f);
		UnderwaterWeeds(C, F, 140, Rgb(40, 100, 60), 0.45f);
		for (int32 X : { -30, -14, 6, 18, 30, 150, 163, 175, 194, 212 }) { ReedClump(C, F.X(X), false); }
		WaterRock(C, F.X(22), F.Depth(0.62f), 5, 3, Rgb(128, 126, 120), true, false, White, false);
		WaterRock(C, F.X(164), F.Depth(0.34f), 4, 3, Rgb(132, 128, 120), true, false, White, false);
		WaterRock(C, F.X(-22), F.Depth(0.45f), 5, 3, Rgb(128, 126, 120), true, false, White, false);
		WaterRock(C, F.X(204), F.Depth(0.8f), 4, 3, Rgb(132, 128, 120), true, false, White, false);
		DrawDock(C, EDock::Wood);

		// backdrop: forest edge, oaks, the hut with its path, a well and the vegetable garden
		CanopyBand(C, F.Hud + 2, Rgb(72, 140, 58), 211, false);
		FenceH(C, GardenX - 3, GardenX + GardenW + 1, GardenY - 1);
		const FColor Apple = Rgb(222, 60, 50);
		const int32 RowA = F.Vis(0.12f);
		C.AddObj(MakeOak(C.R, Rgb(84, 156, 62), 11, nullptr), F.X(12), RowA + 2);
		C.AddObj(MakeOak(C.R, Rgb(76, 150, 60), 12, &Apple), F.X(58), RowA - 2);
		C.AddObj(MakeOak(C.R, Rgb(88, 160, 66), 11, nullptr), F.X(104), RowA + 1);
		C.AddObj(MakeOak(C.R, Rgb(80, 152, 62), 12, nullptr), F.X(150), RowA - 1);
		C.AddObj(MakeOak(C.R, Rgb(82, 154, 60), 10, nullptr), F.X(178), RowA + 6);
		C.AddObj(MakeOak(C.R, Rgb(86, 158, 64), 9, &Apple), F.X(134), F.Vis(0.30f));
		const FIntPoint Hut = C.AddObj(MakeHut(), HutX, HutBase, 30.f, 4.f);
		C.L.Lights.Add({ FCtx::V(float(Hut.X + 15), float(Hut.Y + 31)), 34.f, Rgb(255, 190, 100), false, false, WindowRect(Hut, 11, 28, 8, 7) });
		C.L.Lights.Add({ FCtx::V(float(Hut.X + 43), float(Hut.Y + 31)), 34.f, Rgb(255, 190, 100), false, false, WindowRect(Hut, 39, 28, 8, 7) });
		C.L.Smokes.Add(FCtx::V(float(Hut.X + 43), float(Hut.Y - 1)));
		C.AddObj(MakeWell(), F.X(90), F.Vis(0.36f), 9.f, 2.f);
		C.AddObj(MakeFishSign(), F.X(88), HutBase + 2, 5.f, 2.f);
		const FColor Berry = Rgb(230, 70, 90);
		C.AddObj(MakeBush(C.R, Rgb(78, 150, 62), &Berry), F.X(12), F.Vis(0.78f));
		C.AddObj(MakeBush(C.R, Rgb(84, 160, 66), nullptr), F.X(150), F.Vis(0.86f));
		C.AddObj(MakeBush(C.R, Rgb(78, 150, 62), nullptr, 6), F.X(174), F.Vis(0.72f));
		// (the wide pond: band-height side scenery on the right stands beyond the HUD pad the camera over-scrolls by)
		SideScenery(C, F, 4, [&Apple, &Berry](FRandomStream& R, int32 i)
		{
			if (i % 4 == 3) { return MakeBush(R, Rgb(80, 154, 64), (i & 1) ? &Berry : nullptr); }
			return MakeOak(R, Rgb(78 + (i % 3) * 4, 150 + (i % 2) * 6, 62), R.RandRange(9, 12), (i % 4 == 1) ? &Apple : nullptr);
		}, bWide ? int32(ScrollPadPort * 0.5f) : 0);
		if (bWide)
		{
			// strip scenery, always above the walkable band (the side exits put their signs in the band / the top-left
			// corner): an oak in the left strip, an oak and a bush in the right one
			FSideStream Side(C, 0x61E0);
			C.AddObj(MakeOak(C.R, Rgb(80, 152, 62), 9, nullptr), F.X(-12), F.Vis(0.5f));
			C.AddObj(MakeOak(C.R, Rgb(86, 158, 64), 9, &Apple), F.X(194), F.Vis(0.45f));
			C.AddObj(MakeBush(C.R, Rgb(84, 160, 66), nullptr), F.X(188), F.Vis(0.9f));
		}
		C.PlaceObjs();

		LilyPads(C, 16, F.Shore + 10, F.WaterEnd - 8, F.X(-6), F.X(186));
		C.L.Reeds = { C.V(float(F.X(10)), float(F.Shore + 2)), C.V(float(F.X(24)), float(F.Shore + 3)), C.V(float(F.X(156)), float(F.Shore + 2)), C.V(float(F.X(170)), float(F.Shore + 3)) };
		if (bWide)
		{
			// lily pads over the wider water (one more on the left, two on the right), animated reeds in the strips
			FSideStream Side(C, 0x61E1);
			LilyPads(C, C.L.LilyPads.Num() + 1, F.Shore + 10, F.WaterEnd - 8, F.X(-16), F.X(-2));
			LilyPads(C, C.L.LilyPads.Num() + 2, F.Shore + 10, F.WaterEnd - 8, F.X(182), F.X(196));
			C.L.Reeds.Append({ C.V(float(F.X(-8)), float(F.Shore + 2)), C.V(float(F.X(188)), float(F.Shore + 3)) });
		}
		AddProp(C, EDecor::LampPost, C.V(float(C.DockX) - 7.f, float(F.Shore) - 2.f));
		AddProp(C, EDecor::Barrel, FVector2D(F.VX(292.f), float(F.Shore * 2) - 38.f));
		AddProp(C, EDecor::Crates, FVector2D(F.VX(324.f), float(F.Shore * 2) - 26.f));
		// (the wide pond: the bush leaves the corridor of the river exit at the left edge)
		AddProp(C, EDecor::Bush, bWide ? FVector2D(F.VX(128.f), float(F.Shore * 2) - 34.f) : FVector2D(F.VX(34.f), float(F.Shore * 2) - 52.f));
		C.L.bFireflies = true; C.L.bButterflies = true; C.L.bBirds = true;
		C.L.Current = 3.f;
		PortraitSpawn(C, F, -60);
	}

	void BuildRiverP(FCtx& C)
	{
		const FGround G{ Rgb(60, 108, 58), Rgb(76, 128, 66), Rgb(98, 150, 76), 9.f };
		const FPFrame F = PortraitFrame(C);
		MakeEdge(C, F.Shore, 12);
		C.DockX = F.X(62); C.DockW = 20;
		PortraitDock(C, F, 0.48f);
		FillGround(C, 0, G, 22);
		for (int32 i = 0; i < Scaled(C, 220); ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(4, F.Shore - 4);
			if (Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(120, 96, 60) : Rgb(96, 76, 50)); }
		}
		Tufts(C, Scaled(C, 200), 8, F.Shore - 2, G);
		for (int32 i = 0; i < 34; ++i)   // ferns
		{
			const int32 X = C.R.RandRange(6, C.TW - 6), Y = C.R.RandRange(F.Vis(0.45f), F.Shore - 4);
			if (C.NearDock(X, 6) || Y >= C.Edge[X] - 4) { continue; }
			for (int32 k = -3; k <= 3; ++k)
			{
				const int32 Len = 3 - FMath::Abs(k) / 2;
				for (int32 j = 0; j < Len; ++j) { C.Img.Set(X + k + (k < 0 ? -j : j) / 2, Y - j - (3 - FMath::Abs(k)), j == Len - 1 ? Rgb(140, 190, 90) : Rgb(70, 130, 60)); }
			}
		}
		Flowers(C, Scaled(C, 30), F.Vis(0.45f), F.Shore - 2, { Rgb(250, 250, 240), Rgb(240, 200, 90), Rgb(190, 150, 240) }, 4);

		FillWater(C, 32);
		const int32 Streaks = FMath::RoundToInt(140.f * float(C.TW * (C.TH - F.Shore)) / float(BgW * 85));
		for (int32 i = 0; i < Streaks; ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(F.Shore + 4, C.TH - 4);
			const int32 Len = C.R.RandRange(3, 9);
			for (int32 j = 0; j < Len; ++j) { if (C.IsWaterPx(X + j, Y)) { MixPx(C.Img, X + j, Y, White, 0.16f); } }
		}
		DrawBank(C, EBank::Grass, G);
		Pebbles(C, 52, 14, Rgb(120, 116, 100), 0.35f);
		WaterRock(C, F.X(150), F.Depth(0.17f), 5, 3, Rgb(128, 126, 120), true, false, White, true);
		WaterRock(C, F.X(22), F.Depth(0.32f), 5, 3, Rgb(128, 126, 120), true, false, White, true);
		WaterRock(C, F.X(124), F.Depth(0.54f), 4, 3, Rgb(128, 126, 120), false, false, White, true);
		WaterRock(C, F.X(40), F.Depth(0.76f), 6, 4, Rgb(124, 122, 116), true, false, White, true);
		WaterRock(C, F.X(160), F.Depth(0.88f), 5, 3, Rgb(128, 126, 120), true, false, White, true);
		WaterRock(C, F.X(-24), F.Depth(0.4f), 5, 3, Rgb(128, 126, 120), true, false, White, true);
		WaterRock(C, F.X(200), F.Depth(0.62f), 5, 3, Rgb(124, 122, 116), false, false, White, true);
		for (int32 i = 0; i < 18; ++i)
		{
			const int32 X = C.R.RandRange(4, C.TW - 5);
			if (C.NearDock(X, 6)) { continue; }
			FFVImage Rock = MakeRock(C.R, C.R.RandRange(2, 4), 2, Rgb(140, 136, 128), C.R.FRand() < 0.4f, false);
			C.Img.Blit(Rock, X - Rock.W / 2, C.Edge[X] - Rock.H / 2 - 1);
		}
		for (int32 X : { -20, 8, 22, 150, 170, 196 }) { ReedClump(C, F.X(X), false); }
		DrawDock(C, EDock::Dark);

		// backdrop: rows of pines (from deep in the top bleed) getting lighter towards the clearing with the camp
		const FColor Back = Rgb(30, 78, 64), Mid = Rgb(40, 104, 70), Front = Rgb(58, 124, 74);
		for (int32 Row = -2; Row < 4; ++Row)
		{
			const int32 Base = F.Vis(-0.05f + 0.14f * float(Row));
			constexpr int32 Step = 15;
			for (int32 i = 0; i * Step < C.TW + Step; ++i)
			{
				const int32 X = i * Step + ((Row % 2) ? 7 : 0) + C.R.RandRange(-4, 4);
				if (Row == 3 && X > F.X(44) && X < F.X(150)) { continue; }
				const float Tint = FMath::Clamp(float(Row + 2) / 5.f + C.R.FRandRange(-0.15f, 0.15f), 0.f, 1.f);
				const FColor Leaf = Tint < 0.5f ? Mix(Back, Mid, Tint * 2.f) : Mix(Mid, Front, Tint * 2.f - 1.f);
				C.AddObj(MakePine(C.R, C.R.RandRange(30, 44), Leaf, false), X, Base + C.R.RandRange(-2, 2), 8.f, 2.f);
			}
		}
		C.AddObj(MakePine(C.R, C.R.RandRange(38, 46), Rgb(48, 112, 72), false), F.X(6), F.Vis(0.66f), 9.f, 2.5f);
		C.AddObj(MakePine(C.R, C.R.RandRange(38, 46), Rgb(52, 118, 74), false), F.X(174), F.Vis(0.60f), 9.f, 2.5f);
		const FIntPoint Tent = C.AddObj(MakeTent(), F.X(118), F.Vis(0.72f), 16.f, 3.f);
		C.L.Lights.Add({ FCtx::V(float(Tent.X + 19), float(Tent.Y + 19)), 30.f, Rgb(255, 180, 90), false, false, FFVRect() });
		C.AddObj(MakeLog(), F.X(46), F.Vis(0.82f), 16.f, 2.f);
		C.AddObj(MakeRock(C.R, 9, 6, Rgb(130, 128, 120), true, false), F.X(78), F.Vis(0.62f));
		C.AddObj(MakeMushroom(Rgb(210, 60, 50), Rgb(250, 240, 230)), F.X(152), F.Vis(0.92f), 3.f, 1.f);
		C.AddObj(MakeMushroom(Rgb(200, 120, 60), Rgb(250, 230, 200)), F.X(96), F.Vis(0.96f), 3.f, 1.f);
		C.AddObj(MakeMushroom(Rgb(210, 60, 50), Rgb(250, 240, 230)), F.X(20), F.Vis(0.94f), 3.f, 1.f);
		SideScenery(C, F, 3, [Mid, Front](FRandomStream& R, int32 i)
		{
			if (i % 3 == 2) { return MakeRock(R, R.RandRange(5, 8), 4, Rgb(130, 128, 120), true, false); }
			return MakePine(R, R.RandRange(34, 44), Mix(Mid, Front, R.FRandRange(0.3f, 0.8f)), false);
		});
		C.PlaceObjs();

		C.L.Reeds = { C.V(float(F.X(12)), float(F.Shore + 1)), C.V(float(F.X(154)), float(F.Shore + 1)), C.V(float(F.X(168)), float(F.Shore + 2)) };
		AddProp(C, EDecor::Campfire, FVector2D(F.VX(262.f), float(F.Shore * 2) - 58.f));
		AddProp(C, EDecor::Stump, FVector2D(F.VX(36.f), float(F.Shore * 2) - 36.f));
		AddProp(C, EDecor::Boulder, FVector2D(F.VX(328.f), float(F.Shore * 2) - 30.f));
		AddProp(C, EDecor::Bush, FVector2D(F.VX(186.f), float(F.Shore * 2) - 84.f));
		C.L.bFireflies = true; C.L.bButterflies = true; C.L.bBirds = true;
		C.L.Current = 26.f;
		PortraitSpawn(C, F, -70);
	}

	void BuildMountainP(FCtx& C)
	{
		const FGround G{ Rgb(94, 126, 82), Rgb(114, 146, 92), Rgb(138, 166, 106), 10.f };
		const FPFrame F = PortraitFrame(C);
		MakeEdge(C, F.Shore, 13);
		C.DockX = F.X(96); C.DockW = 20;
		PortraitDock(C, F, 0.5f);
		FillGround(C, 0, G, 23);
		const int32 Sky = F.Vis(0.40f);
		const float Hz = float(Sky - F.Hud);
		auto T0 = [&F, Hz](float Fr) { return float(F.Hud) + Fr * Hz; };
		SkyGradient(C, Sky, Rgb(118, 176, 230), Rgb(214, 236, 250), F.Hud - 10);
		Cloud(C, float(F.X(34)), T0(0.2f), 16.f);
		Cloud(C, float(F.X(128)), T0(0.06f), 20.f);
		Cloud(C, float(F.X(170)), T0(0.3f), 11.f);
		Cloud(C, float(F.X(-22)), T0(-0.25f), 18.f);
		Cloud(C, float(F.X(84)), T0(-0.5f), 22.f);
		Cloud(C, float(F.X(206)), T0(-0.1f), 14.f);
		const FColor Snow = Rgb(246, 250, 255);
		PaintRange(C, { { float(F.X(-40)), T0(0.08f), 0.85f }, { float(F.X(14)), T0(0.18f), 0.9f }, { float(F.X(84)), T0(0.02f), 0.8f },
			{ float(F.X(152)), T0(0.12f), 0.85f }, { float(F.X(204)), T0(0.22f), 0.9f }, { float(F.X(236)), T0(0.06f), 0.85f } },
			Sky, Rgb(156, 174, 204), Rgb(124, 142, 178), &Snow, 9.f, 41);
		for (int32 Y = Sky - 14; Y < Sky; ++Y) { for (int32 X = 0; X < C.TW; ++X) { if (Bayer(X, Y) < float(Y - (Sky - 14)) / 20.f) { MixPx(C.Img, X, Y, Rgb(226, 238, 248), 0.45f); } } }
		PaintRange(C, { { float(F.X(-24)), T0(0.5f), 1.1f }, { float(F.X(44)), T0(0.44f), 1.1f }, { float(F.X(128)), T0(0.36f), 1.0f },
			{ float(F.X(188)), T0(0.5f), 1.2f }, { float(F.X(226)), T0(0.4f), 1.1f } },
			Sky + 2, Rgb(128, 130, 138), Rgb(96, 98, 110), &Snow, 7.f, 42);
		for (int32 i = 0; i < 64; ++i)   // tree line
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Base = C.R.RandRange(Sky - 2, Sky + 4), H = C.R.RandRange(5, 10);
			for (int32 j = 0; j < H; ++j)
			{
				const int32 Hw = (j * 3) / H;
				for (int32 k = -Hw; k <= Hw; ++k) { C.Img.Set(X + k, Base - H + j, k < 0 ? Rgb(62, 98, 80) : Rgb(46, 78, 66)); }
			}
		}
		for (int32 X = 0; X < C.TW; ++X) { for (int32 Y = Sky + 2; Y < Sky + 6; ++Y) { if (Bayer(X, Y) < float(Sky + 6 - Y) / 5.f) { C.Img.Set(X, Y, Rgb(60, 92, 74)); } } }
		for (int32 i = 0; i < 13; ++i)   // gravel & scree
		{
			const int32 CX = C.R.RandRange(0, C.TW - 1), CY = C.R.RandRange(Sky + 8, F.Shore - 6);
			for (int32 k = 0; k < 40; ++k)
			{
				const int32 X = CX + C.R.RandRange(-8, 8), Y = CY + C.R.RandRange(-3, 3);
				if (X >= 0 && X < C.TW && Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(160, 156, 150) : Rgb(126, 122, 118)); }
			}
		}
		Tufts(C, Scaled(C, 160), Sky + 6, F.Shore - 2, G);
		Flowers(C, Scaled(C, 60), Sky + 8, F.Shore - 2, { Rgb(250, 250, 250), Rgb(90, 130, 240), Rgb(190, 120, 230), Rgb(255, 220, 110) }, 5);

		FillWater(C, 33);
		DrawBank(C, EBank::Stone, G);
		Pebbles(C, 84, 26, Rgb(150, 140, 120), 0.4f);
		UnderwaterWeeds(C, F, 44, Rgb(60, 110, 110), 0.3f);
		WaterRock(C, F.X(18), F.Depth(0.46f), 5, 3, Rgb(140, 140, 146), false, true, White, false);
		WaterRock(C, F.X(164), F.Depth(0.7f), 6, 4, Rgb(136, 136, 144), false, true, White, false);
		WaterRock(C, F.X(-22), F.Depth(0.3f), 5, 3, Rgb(140, 140, 146), false, true, White, false);
		WaterRock(C, F.X(204), F.Depth(0.5f), 5, 3, Rgb(136, 136, 144), false, true, White, false);
		DrawDock(C, EDock::Dark);

		const int32 PineRow[6][2] = { { 8, 14 }, { 28, 8 }, { 156, 10 }, { 176, 18 }, { 62, 24 }, { 132, 28 } };
		for (const auto& P : PineRow) { C.AddObj(MakePine(C.R, C.R.RandRange(34, 46), Rgb(46, 104, 78), true), F.X(P[0]), FMath::Min(Sky + P[1], F.WalkTop - 2), 9.f, 2.5f); }
		C.AddObj(MakePine(C.R, C.R.RandRange(34, 42), Rgb(46, 104, 78), true), F.X(170), F.Vis(0.86f), 9.f, 2.5f);
		C.AddObj(MakeRock(C.R, 11, 7, Rgb(140, 140, 146), false, true), F.X(44), F.Vis(0.68f));
		C.AddObj(MakeRock(C.R, 8, 5, Rgb(132, 132, 140), true, false), F.X(106), F.Vis(0.62f));
		C.AddObj(MakeRock(C.R, 5, 3, Rgb(128, 128, 136), false, false), F.X(86), F.Vis(0.9f));
		C.AddObj(MakeRock(C.R, 12, 8, Rgb(146, 144, 150), false, true), F.X(136), F.Vis(0.84f));
		{
			// side bleeds: snowy pines and boulders below the ranges
			for (int32 Side = 0; Side < 2 && F.X0 >= 12; ++Side)
			{
				for (int32 i = 0; i < 3; ++i)
				{
					const int32 Base = FMath::RoundToInt(FMath::Lerp(float(Sky + 8), float(F.Shore - 8), (float(i) + 0.5f) / 3.f));
					const bool bInBand = Base >= F.WalkTop - 2;
					FFVImage Img = (i == 1) ? MakeRock(C.R, C.R.RandRange(6, 9), 5, Rgb(140, 140, 146), false, true) : MakePine(C.R, C.R.RandRange(34, 44), Rgb(46, 104, 78), true);
					const int32 Half = Img.W / 2 + 1;
					const int32 CX = Side == 0 ? (bInBand ? F.X0 - Half - 2 : C.R.RandRange(4, F.X0 - 4)) : (bInBand ? F.Right() + Half + 2 : C.R.RandRange(F.Right() + 4, C.TW - 4));
					C.AddObj(MoveTemp(Img), CX, Base, 9.f, 2.5f);
				}
			}
		}
		C.PlaceObjs();

		AddProp(C, EDecor::SnowBoulder, FVector2D(F.VX(54.f), float(F.Shore * 2) - 40.f));
		AddProp(C, EDecor::YoungPine, FVector2D(F.VX(330.f), float(F.Shore * 2) - 66.f));
		AddProp(C, EDecor::LampPost, C.V(float(C.DockX) - 7.f, float(F.Shore) - 2.f));
		AddProp(C, EDecor::Signpost, FVector2D(F.VX(150.f), float(F.Shore * 2) - 78.f));
		C.L.bButterflies = true; C.L.bBirds = true;
		C.L.Current = 3.f;
		PortraitSpawn(C, F, -64);
	}

	void BuildOceanP(FCtx& C)
	{
		const FGround Sand{ Rgb(218, 188, 130), Rgb(234, 206, 148), Rgb(246, 224, 172), 12.f };
		const FGround Dune{ Rgb(116, 146, 72), Rgb(144, 172, 90), Rgb(174, 196, 110), 8.f };
		const FPFrame F = PortraitFrame(C);
		MakeEdge(C, F.Shore, 14);
		C.DockX = F.X(94); C.DockW = 24;
		PortraitDock(C, F, 0.56f);
		FillGround(C, 0, Sand, 24);
		const int32 DuneRow = F.Vis(0.14f);
		for (int32 X = 0; X < C.TW; ++X)
		{
			const int32 DE = DuneRow + FMath::RoundToInt(VNoise(float(X) / 14.f, 1.f, 8) * 8.f);
			for (int32 Y = 0; Y < DE; ++Y)
			{
				const float V = Fbm(float(X) / Dune.Scale, float(Y) / 6.f, 25) + (Bayer(X, Y) - 0.5f) * 0.2f;
				C.Img.Set(X, Y, V < 0.4f ? Dune.Lo : (V < 0.62f ? Dune.Base : Dune.Hi));
			}
			for (int32 Y = DE; Y < DE + 3; ++Y) { if (Bayer(X, Y) < float(DE + 3 - Y) / 4.f) { C.Img.Set(X, Y, Mix(Sand.Base, Dune.Lo, 0.5f)); } }
			if (C.R.FRand() < 0.3f)
			{
				const int32 H = C.R.RandRange(3, 6);
				for (int32 j = 0; j < H; ++j) { C.Img.Set(X + (j > H / 2 ? 1 : 0), DE + 1 - j, j > H - 2 ? Rgb(200, 206, 120) : Rgb(130, 150, 70)); }
			}
		}
		for (int32 X = 0; X < C.TW; ++X)   // sand fence
		{
			if (((X - F.X0 + 400) / 40) % 3 == 2) { continue; }
			const int32 Y = DuneRow + 14 + FMath::RoundToInt(FMath::Sin(float(X) * 0.05f) * 1.5f);
			C.Img.Set(X, Y - 3, Rgb(120, 110, 100)); C.Img.Set(X, Y, Rgb(120, 110, 100));
			if (X % 3 == 0) { for (int32 j = -5; j <= 1; ++j) { C.Img.Set(X, Y + j, j == -5 ? Rgb(210, 170, 120) : Rgb(170, 124, 84)); } }
			if (X % 3 == 0) { MulPx(C.Img, X + 1, Y + 2, 0.85f); }
		}
		for (int32 i = 0; i < Scaled(C, 60); ++i)   // sand ripples
		{
			const int32 X = C.R.RandRange(0, C.TW - 8), Y = C.R.RandRange(DuneRow + 18, F.Shore - 10);
			for (int32 j = 0; j < 6; ++j) { if (Y < C.Edge[X + j] - 8) { MixPx(C.Img, X + j, Y + (j == 0 || j == 5 ? 1 : 0), Sand.Lo, 0.6f); } }
		}
		for (int32 i = 0; i < 56; ++i)   // shells & starfish
		{
			const int32 X = C.R.RandRange(4, C.TW - 5), Y = C.R.RandRange(DuneRow + 18, F.Shore - 4);
			if (Y >= C.Edge[X] - 4 || C.NearDock(X, 3)) { continue; }
			const int32 Kind = C.R.RandHelper(3);
			if (Kind == 0)
			{
				const FColor S = C.R.FRand() < 0.5f ? Rgb(250, 190, 200) : Rgb(250, 244, 230);
				C.Img.Set(X, Y, S); C.Img.Set(X + 1, Y, S); C.Img.Set(X - 1, Y + 1, Mul(S, 0.85f)); C.Img.Set(X, Y + 1, S); C.Img.Set(X + 1, Y + 1, Mul(S, 0.8f)); C.Img.Set(X + 2, Y + 1, Mul(S, 0.75f));
			}
			else if (Kind == 1)
			{
				const FColor S = Rgb(244, 132, 76), SL = Rgb(206, 96, 56);
				C.Img.Set(X, Y - 2, S); C.Img.Set(X, Y - 1, S); C.Img.Set(X - 2, Y, S); C.Img.Set(X - 1, Y, S); C.Img.Set(X, Y, Rgb(255, 170, 110));
				C.Img.Set(X + 1, Y, SL); C.Img.Set(X + 2, Y, SL); C.Img.Set(X - 1, Y + 1, SL); C.Img.Set(X + 1, Y + 1, SL); C.Img.Set(X - 2, Y + 2, SL); C.Img.Set(X + 2, Y + 2, SL);
			}
			else
			{
				C.Img.Set(X, Y, Rgb(240, 236, 220)); C.Img.Set(X + 1, Y, Rgb(200, 190, 170));
			}
		}

		FillWater(C, 34);
		for (int32 X = 0; X < C.TW; ++X)   // sand under the shallows
		{
			for (int32 Y = C.Edge[X]; Y < C.Edge[X] + 14; ++Y)
			{
				const float T = 1.f - float(Y - C.Edge[X]) / 14.f;
				if (Bayer(X, Y) < T) { MixPx(C.Img, X, Y, Rgb(220, 206, 150), 0.22f); }
			}
		}
		DrawBank(C, EBank::Sand, Sand);
		WaterRock(C, F.X(20), F.Depth(0.3f), 6, 4, Rgb(150, 140, 130), false, false, White, false);
		WaterRock(C, F.X(162), F.Depth(0.62f), 5, 3, Rgb(146, 136, 128), false, false, White, false);
		WaterRock(C, F.X(150), F.Depth(0.66f) + 3, 3, 2, Rgb(146, 136, 128), false, false, White, false);
		WaterRock(C, F.X(-22), F.Depth(0.48f), 6, 4, Rgb(150, 140, 130), false, false, White, false);
		WaterRock(C, F.X(204), F.Depth(0.34f), 5, 3, Rgb(146, 136, 128), false, false, White, false);
		DrawDock(C, EDock::Pier);
		for (int32 BX : { C.DockX + 3, C.DockX + C.DockW - 6 })   // bollards at the pier end
		{
			for (int32 Y = C.DockY1 - 7; Y <= C.DockY1 - 3; ++Y)
				for (int32 X = BX; X < BX + 3; ++X) { C.Img.Set(X, Y, Y == C.DockY1 - 7 ? Rgb(120, 116, 116) : (X == BX ? Rgb(96, 90, 92) : Rgb(70, 66, 70))); }
		}

		// backdrop: dunes with a lighthouse, palms, a boat pulled up on the beach
		const int32 LhBase = F.Vis(0.42f);
		const int32 LhH = FMath::Clamp(LhBase - F.Hud + 2, 40, 58);
		const FIntPoint Lh = C.AddObj(MakeLighthouse(LhH), F.X(156), LhBase, 9.f, 2.f);
		C.L.Lights.Add({ FCtx::V(float(Lh.X) + 11.f, float(Lh.Y) + 8.f), 84.f, Rgb(255, 232, 150), false, false, FFVRect() });
		C.AddObj(MakeRock(C.R, 9, 4, Rgb(150, 140, 130), false, false), F.X(146), LhBase + 3);
		C.AddObj(MakePalm(C.R, false), F.X(16), F.Vis(0.56f), 12.f, 3.f);
		C.AddObj(MakePalm(C.R, false), F.X(48), F.Vis(0.32f), 12.f, 3.f);
		C.AddObj(MakePalm(C.R, true), F.X(128), F.Vis(0.7f), 12.f, 3.f);
		C.AddObj(MakePalm(C.R, true), F.X(176), F.Vis(0.92f), 12.f, 3.f);
		C.AddObj(MakeBoat(), F.X(70), F.Vis(0.74f), 16.f, 3.f);
		C.AddObj(MakeRock(C.R, 6, 4, Rgb(150, 140, 130), false, false), F.X(100), F.Vis(0.5f));
		SideScenery(C, F, 3, [](FRandomStream& R, int32 i)
		{
			if (i % 3 == 1) { return MakeRock(R, R.RandRange(5, 8), 4, Rgb(150, 140, 130), false, false); }
			return MakePalm(R, i >= 3);
		});
		C.PlaceObjs();

		AddProp(C, EDecor::BeachUmbrella, FVector2D(F.VX(300.f), float(F.Shore * 2) - 62.f));
		AddProp(C, EDecor::Crates, FVector2D(F.VX(40.f), float(F.Shore * 2) - 34.f));
		AddProp(C, EDecor::Lifebuoy, C.V(float(C.DockX) - 7.f, float(F.Shore) - 2.f));
		AddProp(C, EDecor::Barrel, FVector2D(F.VX(270.f), float(F.Shore * 2) - 18.f));
		C.L.bGulls = true; C.L.bWaves = true;
		C.L.BirdColor = Rgb(250, 250, 250);
		C.L.Current = 6.f;
		PortraitSpawn(C, F, -70);
	}

	void BuildSwampP(FCtx& C)
	{
		const FGround G{ Rgb(58, 68, 42), Rgb(74, 86, 50), Rgb(92, 104, 60), 8.f };
		const FPFrame F = PortraitFrame(C);
		MakeEdge(C, F.Shore, 15);
		C.DockX = F.X(114); C.DockW = 20;
		PortraitDock(C, F, 0.48f);
		FillGround(C, 0, G, 26);
		for (int32 i = 0; i < 20; ++i)   // mud puddles
		{
			const float CX = C.R.FRandRange(8.f, float(C.TW) - 8.f), CY = C.R.FRandRange(float(F.Vis(0.2f)), float(F.Shore) - 8.f);
			const float RX = C.R.FRandRange(5.f, 10.f), RY = RX * 0.4f;
			if (CY + RY >= float(C.Edge[FMath::Clamp(int32(CX), 0, C.TW - 1)] - 4)) { continue; }
			FillEllipse(C.Img, CX, CY, RX + 1.f, RY + 1.f, Rgb(92, 74, 50));
			FillEllipse(C.Img, CX, CY, RX, RY, Rgb(52, 60, 44));
			C.Img.Set(int32(CX - RX * 0.4f), int32(CY - RY * 0.3f), Rgb(120, 140, 110));
			C.Img.Set(int32(CX - RX * 0.4f) + 1, int32(CY - RY * 0.3f), Rgb(96, 112, 90));
			C.Reserve(int32(CX - RX), int32(CY - RY), int32(RX * 2.f), int32(RY * 2.f));
		}
		for (int32 i = 0; i < Scaled(C, 180); ++i)
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(4, F.Shore - 4);
			if (C.Free(X, Y) && Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(104, 124, 56) : Rgb(60, 50, 36)); }
		}
		Tufts(C, Scaled(C, 160), 8, F.Shore - 2, G);

		FillWater(C, 35);
		for (int32 X = 0; X < C.TW; ++X)   // algae blotches & duckweed
		{
			for (int32 Y = C.Edge[X]; Y < C.TH; ++Y)
			{
				const float N = Fbm(float(X) / 14.f, float(Y) / 6.f, 77);
				if (N > 0.64f) { MixPx(C.Img, X, Y, Rgb(84, 110, 46), Bayer(X, Y) < 0.5f ? 0.4f : 0.25f); }
				if (R01(X, Y, 78) < 0.012f) { C.Img.Set(X, Y, Rgb(130, 170, 60)); }
			}
		}
		DrawBank(C, EBank::Mud, G);
		for (int32 X : { -30, -12, 6, 18, 30, 48, 66, 90, 146, 160, 174, 192, 210 }) { ReedClump(C, F.X(X), true); }
		FloatingLog(C, F.X(34), F.Depth(0.48f), false);
		FloatingLog(C, F.X(152), F.Depth(0.74f), true);
		FloatingLog(C, F.X(-26), F.Depth(0.3f), true);
		{
			const FFVImage Stump = PropImage(EDecor::DeadStump);
			for (const FIntPoint& P : { FIntPoint(F.X(160), F.Depth(0.26f)), FIntPoint(F.X(24), F.Depth(0.84f)), FIntPoint(F.X(204), F.Depth(0.56f)) })
			{
				C.Img.Blit(Stump, P.X - Stump.W / 2, P.Y - Stump.H + 1);
				for (int32 Xx = P.X - 8; Xx <= P.X + 8; ++Xx) { if (Bayer(Xx, P.Y) < 0.6f) { MixPx(C.Img, Xx, P.Y + 1, Rgb(196, 214, 176), 0.35f); } }
			}
		}
		DrawDock(C, EDock::Weathered);

		// backdrop: a murky tree line, dead trees, the shack on stilts, glowing mushrooms
		CanopyBand(C, F.Hud + 2, Rgb(62, 86, 50), 311, true);
		C.AddObj(MakeDeadTree(C.R, true), F.X(40), F.Vis(0.30f));
		C.AddObj(MakeDeadTree(C.R, true), F.X(102), F.Vis(0.22f));
		C.AddObj(MakeDeadTree(C.R, false), F.X(168), F.Vis(0.40f));
		C.AddObj(MakeDeadTree(C.R, true), F.X(138), F.Vis(0.64f));
		C.AddObj(MakeOak(C.R, Rgb(64, 90, 52), 10, nullptr), F.X(12), F.Vis(0.14f));
		C.AddObj(MakeOak(C.R, Rgb(60, 86, 50), 11, nullptr), F.X(150), F.Vis(0.12f));
		const FIntPoint Shack = C.AddObj(MakeShack(), F.X(50), F.Vis(0.80f), 22.f, 3.f);
		C.L.Lights.Add({ FCtx::V(float(Shack.X + 38), float(Shack.Y + 18)), 44.f, Rgb(220, 240, 130), true, false, FFVRect() });
		C.L.Lights.Add({ FCtx::V(float(Shack.X + 14), float(Shack.Y + 23)), 26.f, Rgb(255, 196, 110), false, false, WindowRect(Shack, 10, 20, 8, 7) });
		C.AddObj(MakeBush(C.R, Rgb(70, 96, 52), nullptr), F.X(164), F.Vis(0.88f));
		C.AddObj(MakeBush(C.R, Rgb(64, 90, 50), nullptr, 6), F.X(96), F.Vis(0.52f));
		for (int32 i = 0; i < 8; ++i)
		{
			const bool bGlow = (i % 2) == 0;
			const int32 X = C.R.RandRange(F.X(84), F.X(172)), Y = C.R.RandRange(F.Vis(0.56f), F.Vis(0.98f));
			C.AddObj(MakeMushroom(bGlow ? Rgb(70, 200, 220) : Rgb(170, 60, 120), bGlow ? Rgb(200, 250, 255) : Rgb(240, 200, 230)), X, Y, 3.f, 1.f);
			if (bGlow) { C.L.Lights.Add({ FCtx::V(float(X), float(Y - 4)), 16.f, Rgb(90, 230, 240), false, false, FFVRect() }); }
		}
		SideScenery(C, F, 3, [](FRandomStream& R, int32 i)
		{
			if (i % 3 == 1) { return MakeOak(R, Rgb(62, 88, 50), R.RandRange(9, 11), nullptr); }
			return MakeDeadTree(R, (i & 1) == 0);
		});
		C.PlaceObjs();

		LilyPads(C, 12, F.Shore + 10, F.WaterEnd - 8, F.X(-6), F.X(186));
		C.L.Reeds = { C.V(float(F.X(12)), float(F.Shore + 2)), C.V(float(F.X(40)), float(F.Shore + 2)), C.V(float(F.X(92)), float(F.Shore + 1)),
			C.V(float(F.X(152)), float(F.Shore + 2)), C.V(float(F.X(170)), float(F.Shore + 1)) };
		C.L.bCattails = true;
		AddProp(C, EDecor::DeadStump, FVector2D(F.VX(40.f), float(F.Shore * 2) - 32.f));
		AddProp(C, EDecor::GlowShrooms, FVector2D(F.VX(318.f), float(F.Shore * 2) - 72.f));
		AddProp(C, EDecor::GlowShrooms, FVector2D(F.VX(190.f), float(F.Shore * 2) - 96.f));
		AddProp(C, EDecor::LampPost, C.V(float(C.DockX) - 7.f, float(F.Shore) - 2.f));
		C.L.bFireflies = true; C.L.bFog = true; C.L.bBubbles = true; C.L.bBirds = true;
		C.L.BirdColor = Rgb(40, 36, 44);
		C.L.Glint = Rgb(190, 214, 170);
		C.L.Foam = Rgb(190, 210, 170);
		C.L.FogColor = Rgb(196, 214, 190);
		C.L.Current = 2.f;
		PortraitSpawn(C, F, -60);
	}

	void BuildVolcanoP(FCtx& C)
	{
		const FGround G{ Rgb(46, 40, 48), Rgb(62, 54, 62), Rgb(82, 72, 80), 9.f };
		const FPFrame F = PortraitFrame(C);
		MakeEdge(C, F.Shore, 16);
		C.DockX = F.X(80); C.DockW = 22;
		PortraitDock(C, F, 0.48f);
		FillGround(C, 0, G, 27);
		const int32 Sky = F.Vis(0.40f);
		const float Hz = float(Sky - F.Hud);
		auto T0 = [&F, Hz](float Fr) { return float(F.Hud) + Fr * Hz; };
		SkyGradient(C, Sky, Rgb(36, 18, 34), Rgb(126, 46, 36), F.Hud - 12);
		for (int32 i = 0; i < 18; ++i)   // smoke clouds
		{
			const float CX = C.R.FRandRange(0.f, float(C.TW)), CY = C.R.FRandRange(4.f, T0(0.45f));
			FillEllipse(C.Img, CX, CY, C.R.FRandRange(8.f, 16.f), C.R.FRandRange(2.f, 4.f), Rgb(70, 50, 56, 150), true);
		}
		PaintRange(C, { { float(F.X(-34)), T0(0.30f), 0.8f }, { float(F.X(10)), T0(0.42f), 0.8f }, { float(F.X(176)), T0(0.36f), 0.7f }, { float(F.X(216)), T0(0.26f), 0.8f } },
			Sky + 2, Rgb(66, 44, 54), Rgb(50, 34, 44), nullptr, 0.f, 51);
		const float ConeX = float(F.X(92)), ConeTop = FMath::RoundToFloat(T0(0.08f));
		PaintRange(C, { { ConeX, ConeTop, 1.0f } }, Sky + 2, Rgb(74, 58, 66), Rgb(50, 40, 48), nullptr, 0.f, 52);
		FillEllipse(C.Img, ConeX + 0.5f, ConeTop + 2.5f, 4.5f, 1.8f, Rgb(236, 96, 34));
		FillEllipse(C.Img, ConeX + 0.5f, ConeTop + 2.5f, 2.5f, 1.f, Rgb(255, 222, 120));
		for (int32 s = 0; s < 3; ++s)
		{
			float X = ConeX - 3.f + float(s) * 3.f;
			for (int32 Y = int32(ConeTop) + 3; Y < Sky + 1; ++Y)
			{
				X += (VNoise(float(Y) / 4.f, float(s) * 3.f, 61) - 0.5f) * 1.6f + (float(s) - 1.f) * 0.55f;
				const int32 IX = FMath::RoundToInt(X);
				if (float(Y) < ConeTop + 2.f + FMath::Abs(float(IX) - ConeX) + 2.f) { continue; }
				C.Img.Set(IX, Y, Rgb(255, 120, 40)); C.Img.Set(IX + 1, Y, Rgb(200, 70, 30));
				if (Y % 3 == 0) { C.Img.Set(IX, Y, Rgb(255, 214, 110)); }
			}
		}
		C.L.Lights.Add({ FCtx::V(ConeX, ConeTop + 2.f), 60.f, Rgb(255, 110, 40), true, true, FFVRect() });
		C.L.Smokes.Add(FCtx::V(ConeX, ConeTop - 2.f));
		for (int32 X = 0; X < C.TW; ++X) { for (int32 Y = Sky; Y < Sky + 6; ++Y) { if (Bayer(X, Y) < float(Sky + 6 - Y) / 7.f) { C.Img.Set(X, Y, Rgb(40, 32, 40)); } } }
		int32 CrackLights = 0;
		for (int32 i = 0; i < 20; ++i)   // glowing cracks
		{
			int32 X = C.R.RandRange(6, C.TW - 6), Y = C.R.RandRange(Sky + 8, F.Shore - 6);
			const int32 Len = C.R.RandRange(8, 18);
			const FIntPoint Start(X, Y);
			for (int32 j = 0; j < Len; ++j)
			{
				if (Y >= C.Edge[FMath::Clamp(X, 0, C.TW - 1)] - 4 || C.NearDock(X, 2)) { break; }
				C.Img.Set(X, Y, j % 4 == 1 ? Rgb(255, 214, 100) : Rgb(236, 96, 34));
				MixPx(C.Img, X, Y - 1, Rgb(140, 60, 40), 0.5f);
				X += C.R.RandRange(0, 1) * (i % 2 ? 1 : -1);
				Y += C.R.RandRange(0, 1);
				if (C.R.FRand() < 0.5f) { X += (i % 2 ? 1 : -1); }
			}
			const bool bSeen = Start.X > F.X0 && Start.X < F.Right();
			if (bSeen && CrackLights < 7) { ++CrackLights; C.L.Lights.Add({ FCtx::V(float(Start.X + X) * 0.5f, float(Start.Y + Y) * 0.5f), 18.f, Rgb(255, 110, 40), true, true, FFVRect() }); }
		}
		for (int32 i = 0; i < Scaled(C, 160); ++i)   // ash specks
		{
			const int32 X = C.R.RandRange(0, C.TW - 1), Y = C.R.RandRange(Sky + 4, F.Shore - 2);
			if (Y < C.Edge[X] - 3) { C.Img.Set(X, Y, C.R.FRand() < 0.5f ? Rgb(100, 92, 98) : Rgb(36, 30, 36)); }
		}

		FillWater(C, 36);
		for (int32 X = 0; X < C.TW; ++X)   // crust on the lava
		{
			for (int32 Y = C.Edge[X] + 3; Y < C.TH; ++Y)
			{
				const float N = Fbm(float(X) / 12.f, float(Y) / 5.f, 88);
				if (N > 0.66f) { C.Img.Set(X, Y, N > 0.7f ? Rgb(52, 30, 30) : Rgb(96, 40, 26)); }
				else if (N > 0.63f) { MixPx(C.Img, X, Y, Rgb(255, 230, 120), 0.5f); }
			}
		}
		DrawBank(C, EBank::Basalt, G);
		WaterRock(C, F.X(22), F.Depth(0.42f), 6, 4, Rgb(70, 62, 72), false, false, Rgb(255, 214, 110), false);
		WaterRock(C, F.X(160), F.Depth(0.7f), 5, 3, Rgb(66, 58, 68), false, false, Rgb(255, 214, 110), false);
		WaterRock(C, F.X(-24), F.Depth(0.55f), 6, 4, Rgb(70, 62, 72), false, false, Rgb(255, 214, 110), false);
		WaterRock(C, F.X(206), F.Depth(0.3f), 5, 3, Rgb(66, 58, 68), false, false, Rgb(255, 214, 110), false);
		DrawDock(C, EDock::Rock);

		for (const FIntPoint& P : { FIntPoint(F.X(18), F.Vis(0.6f)), FIntPoint(F.X(56), F.Vis(0.5f)), FIntPoint(F.X(146), F.Vis(0.56f)), FIntPoint(F.X(172), F.Vis(0.86f)), FIntPoint(F.X(116), F.Vis(0.9f)) })
		{
			C.AddObj(MakeCrystal(C.R, C.R.RandRange(12, 20), true), P.X, P.Y, 6.f, 2.f);
			C.L.Lights.Add({ FCtx::V(float(P.X), float(P.Y - 8)), 18.f, Rgb(200, 110, 255), false, false, FFVRect() });
		}
		C.AddObj(MakeRock(C.R, 10, 6, Rgb(70, 62, 72), false, false), F.X(38), F.Vis(0.8f));
		C.AddObj(MakeRock(C.R, 8, 5, Rgb(66, 58, 68), false, false), F.X(128), F.Vis(0.72f));
		C.AddObj(MakeRock(C.R, 6, 4, Rgb(74, 64, 74), false, false), F.X(86), F.Vis(0.94f));
		SideScenery(C, F, 3, [](FRandomStream& R, int32 i)
		{
			if (i % 3 == 1) { return MakeCrystal(R, R.RandRange(12, 18), true); }
			return MakeRock(R, R.RandRange(7, 10), R.RandRange(4, 6), Rgb(70, 62, 72), false, false);
		});
		C.PlaceObjs();

		AddProp(C, EDecor::Brazier, C.V(float(C.DockX) - 8.f, float(F.Shore) - 1.f));
		AddProp(C, EDecor::Brazier, C.V(float(C.DockX + C.DockW) + 8.f, float(F.Shore) - 1.f));
		AddProp(C, EDecor::Crystal, FVector2D(F.VX(40.f), float(F.Shore * 2) - 66.f));
		AddProp(C, EDecor::BasaltRock, FVector2D(F.VX(300.f), float(F.Shore * 2) - 44.f));
		AddProp(C, EDecor::Crystal, FVector2D(F.VX(336.f), float(F.Shore * 2) - 86.f));
		C.L.bRockPier = true;
		C.L.bLava = true; C.L.bEmbers = true;
		C.L.Glint = Rgb(255, 236, 140);
		C.L.Foam = Rgb(255, 214, 110);
		C.L.Current = 2.f;
		PortraitSpawn(C, F, -70);
	}

	// =========================================================================================
	// Decor sprites
	// =========================================================================================
	// =========================================================================================
	// UPDATE 2: the home yard (cottage + garden), the house interior and the world-object anchors
	// (stalls, order board, net spots, resource slots, dig spots) of the fishing locations.
	// Everything state-dependent (pickups, stations, nets, decor) is drawn at runtime by FVSceneHome.cpp.
	// =========================================================================================
	constexpr int32 CottageDoorX = 34, CottageDoorY = 36, CottageDoorW = 10, CottageDoorH = 21;
	const FColor HouseDark = Rgb(52, 36, 30);   // outside of the room (letterbox / under the HUD)

	/** The player's log cottage: shingle roof, a central door under a little porch roof, shuttered windows with
	    flower boxes, a door lantern and a chimney. 78x62 texels, base = bottom row. */
	FFVImage MakeCottage()
	{
		const int32 W = 78, H = 62;
		FFVImage T(W, H);
		const FColor RoofHi = Rgb(214, 100, 68), Roof = Rgb(178, 70, 52), RoofLo = Rgb(134, 50, 42), RoofDeep = Rgb(100, 38, 36);
		const FColor LogHi = Rgb(204, 150, 94), Log = Rgb(170, 116, 68), LogLo = Rgb(128, 84, 48), LogSeam = Rgb(98, 62, 36), LogEnd = Rgb(222, 180, 122);
		const FColor Stone = Rgb(150, 146, 140), StoneLo = Rgb(112, 106, 104), StoneHi = Rgb(188, 184, 174);
		const FColor Trim = Rgb(96, 60, 36);
		const FColor Glass = Rgb(66, 92, 132), GlassHi = Rgb(156, 196, 226);
		// log walls: 3-row logs (lit top, body, seam), round log ends at the corners
		for (int32 Y = 29; Y <= 55; ++Y)
		{
			const int32 R3 = (Y - 29) % 3;
			for (int32 X = 5; X <= 72; ++X)
			{
				FColor Cc = R3 == 0 ? LogHi : (R3 == 1 ? Log : LogSeam);
				if (R3 == 1 && R01(X / 3, Y, 41) < 0.12f) { Cc = LogLo; }
				if (X >= 69) { Cc = Mul(Cc, 0.82f); }
				T.Set(X, Y, Cc);
			}
			if (R3 != 2)
			{
				T.Set(3, Y, R3 == 0 ? LogEnd : Mul(LogEnd, 0.85f)); T.Set(4, Y, LogEnd);
				T.Set(73, Y, Mul(LogEnd, 0.78f)); T.Set(74, Y, Mul(LogEnd, 0.66f));
			}
		}
		for (int32 X = 5; X <= 72; ++X) { T.Set(X, 29, Trim); T.Set(X, 30, LogSeam); }
		// stone foundation
		for (int32 Y = 56; Y <= 61; ++Y)
		{
			for (int32 X = 3; X <= 74; ++X)
			{
				FColor Cc = ((X + (Y & 1) * 3) % 7 == 0) ? StoneLo : Stone;
				if (Y == 56) { Cc = StoneHi; }
				if (Y == 61) { Cc = StoneLo; }
				T.Set(X, Y, Cc);
			}
		}
		// door: planks, frame, a little round window, knob; stone step
		for (int32 Y = CottageDoorY; Y < CottageDoorY + CottageDoorH; ++Y)
		{
			for (int32 X = CottageDoorX; X < CottageDoorX + CottageDoorW; ++X)
			{
				FColor Cc = Rgb(122, 76, 46);
				if (X == CottageDoorX || X == CottageDoorX + CottageDoorW - 1 || Y == CottageDoorY) { Cc = Trim; }
				else if ((X - CottageDoorX) % 3 == 0) { Cc = Rgb(98, 60, 36); }
				else if (X == CottageDoorX + 1) { Cc = Rgb(146, 94, 58); }
				T.Set(X, Y, Cc);
			}
		}
		for (int32 Y = CottageDoorY + 3; Y <= CottageDoorY + 6; ++Y)
		{
			for (int32 X = CottageDoorX + 3; X <= CottageDoorX + 6; ++X)
			{
				const bool bCorner = (X == CottageDoorX + 3 || X == CottageDoorX + 6) && (Y == CottageDoorY + 3 || Y == CottageDoorY + 6);
				T.Set(X, Y, bCorner ? Trim : ((X == CottageDoorX + 4 && Y == CottageDoorY + 4) ? GlassHi : Glass));
			}
		}
		T.Set(CottageDoorX + 7, CottageDoorY + 11, Rgb(236, 196, 92)); T.Set(CottageDoorX + 7, CottageDoorY + 12, Rgb(180, 140, 60));
		for (int32 X = CottageDoorX - 2; X < CottageDoorX + CottageDoorW + 2; ++X) { T.Set(X, 57, StoneHi); T.Set(X, 58, Stone); }
		// porch roof over the door
		for (int32 Y = 31; Y <= 34; ++Y)
		{
			const int32 X0 = CottageDoorX - 4 - (Y - 31), X1 = CottageDoorX + CottageDoorW + 3 + (Y - 31);
			for (int32 X = X0; X <= X1; ++X) { T.Set(X, Y, Y == 31 ? RoofHi : (Y == 34 ? RoofDeep : ((X % 4) == 0 ? RoofLo : Roof))); }
		}
		// windows with green shutters and flower boxes
		auto Window = [&T, Trim, Glass, GlassHi](int32 X0, int32 Y0)
		{
			for (int32 Y = Y0; Y <= Y0 + 10; ++Y)
			{
				for (int32 X = X0; X <= X0 + 11; ++X)
				{
					FColor Cc = Glass;
					if (X == X0 || X == X0 + 11 || Y == Y0 || Y == Y0 + 10) { Cc = Trim; }
					else if (X == X0 + 6 || Y == Y0 + 5) { Cc = Rgb(210, 170, 120); }
					else if ((X == X0 + 1 && Y <= Y0 + 3) || (X == X0 + 2 && Y == Y0 + 1)) { Cc = GlassHi; }
					T.Set(X, Y, Cc);
				}
				T.Set(X0 - 3, Y, Rgb(84, 140, 90)); T.Set(X0 - 2, Y, (Y % 2) ? Rgb(64, 116, 74) : Rgb(84, 140, 90));
				T.Set(X0 + 13, Y, Rgb(64, 116, 74)); T.Set(X0 + 14, Y, (Y % 2) ? Rgb(50, 96, 62) : Rgb(64, 116, 74));
			}
			const FColor Petals[3] = { Rgb(240, 90, 110), Rgb(255, 210, 90), Rgb(250, 150, 200) };
			for (int32 X = X0 - 1; X <= X0 + 12; ++X)
			{
				T.Set(X, Y0 + 11, (X % 2) ? Petals[(X / 2) % 3] : Rgb(76, 140, 60));
				T.Set(X, Y0 + 12, Rgb(136, 88, 52)); T.Set(X, Y0 + 13, Rgb(104, 64, 40));
			}
		};
		Window(12, 38); Window(54, 38);
		// shingle roof
		for (int32 Y = 4; Y <= 28; ++Y)
		{
			const float Tt = float(Y - 4) / 24.f;
			const int32 X0 = FMath::RoundToInt(14.f - 13.f * Tt), X1 = FMath::RoundToInt(63.f + 13.f * Tt);
			const int32 Row = (Y - 4) / 3, In = (Y - 4) % 3;
			for (int32 X = X0; X <= X1; ++X)
			{
				FColor Cc = In == 0 ? RoofHi : (In == 2 ? RoofLo : Roof);
				if (In != 0 && ((X + (Row % 2) * 3) % 6) == 0) { Cc = RoofLo; }
				if (X <= X0 + 1) { Cc = Mix(Cc, RoofHi, 0.5f); }
				else if (X >= X1 - 1) { Cc = RoofDeep; }
				T.Set(X, Y, Cc);
			}
		}
		for (int32 X = 14; X <= 63; ++X) { T.Set(X, 3, RoofDeep); }
		for (int32 X = 1; X <= 76; ++X) { T.Set(X, 28, RoofDeep); }
		// round attic window
		FillEllipse(T, 39.f, 15.f, 3.4f, 3.4f, Trim);
		FillEllipse(T, 39.f, 15.f, 2.4f, 2.4f, Glass);
		T.Set(38, 14, GlassHi);
		// chimney
		for (int32 Y = 0; Y <= 12; ++Y)
		{
			for (int32 X = 56; X <= 62; ++X)
			{
				FColor Cc = X == 56 ? StoneHi : (X == 62 ? StoneLo : Stone);
				if (Y <= 1) { Cc = Rgb(70, 64, 64); }
				else if ((Y + (X & 1)) % 3 == 0) { Cc = StoneLo; }
				T.Set(X, Y, Cc);
			}
		}
		// door lantern
		T.Set(47, 36, Rgb(60, 50, 50));
		T.Set(46, 37, Rgb(60, 50, 50)); T.Set(47, 37, Rgb(255, 236, 170)); T.Set(48, 37, Rgb(60, 50, 50));
		T.Set(46, 38, Rgb(60, 50, 50)); T.Set(47, 38, Rgb(255, 196, 90)); T.Set(48, 38, Rgb(60, 50, 50));
		T.Set(47, 39, Rgb(60, 50, 50));
		T.Outline(Rgb(60, 36, 28));
		return T;
	}

	/** Bare tilled soil in a log border (the home dig bed; the dig spots are drawn on it at runtime). */
	void SoilBed(FCtx& C, int32 X0, int32 Y0, int32 GW, int32 GH)
	{
		const FColor Soil = Rgb(118, 80, 52), SoilLo = Rgb(90, 60, 40), SoilHi = Rgb(146, 104, 68);
		for (int32 Y = Y0; Y < Y0 + GH; ++Y)
		{
			for (int32 X = X0; X < X0 + GW; ++X)
			{
				const int32 R4 = (Y - Y0) % 4;
				FColor Cc = R4 == 3 ? SoilLo : (R4 == 0 ? SoilHi : Soil);
				if (R01(X, Y, 97) < 0.07f) { Cc = SoilLo; }
				else if (R01(X, Y, 98) < 0.035f) { Cc = Rgb(166, 156, 146); }
				C.Img.Set(X, Y, Cc);
			}
		}
		const FColor LogC = Rgb(150, 104, 62), LogHiC = Rgb(192, 144, 92), LogLoC = Rgb(104, 68, 40);
		for (int32 X = X0 - 2; X < X0 + GW + 2; ++X)
		{
			C.Img.Set(X, Y0 - 2, LogHiC); C.Img.Set(X, Y0 - 1, LogC);
			C.Img.Set(X, Y0 + GH, LogC); C.Img.Set(X, Y0 + GH + 1, LogLoC);
			MulPx(C.Img, X, Y0 + GH + 2, 0.8f);
		}
		for (int32 Y = Y0 - 2; Y < Y0 + GH + 2; ++Y)
		{
			C.Img.Set(X0 - 2, Y, LogHiC); C.Img.Set(X0 - 1, Y, LogC);
			C.Img.Set(X0 + GW, Y, LogC); C.Img.Set(X0 + GW + 1, Y, LogLoC);
		}
		// a few sprouts in the corners
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 X = (k % 2) ? X0 + GW - 3 : X0 + 2, Y = (k / 2) ? Y0 + GH - 3 : Y0 + 2;
			C.Img.Set(X, Y, Rgb(110, 190, 84)); C.Img.Set(X - 1, Y - 1, Rgb(140, 210, 100)); C.Img.Set(X + 1, Y - 1, Rgb(96, 170, 76));
		}
		C.Reserve(X0 - 3, Y0 - 3, GW + 6, GH + 6);
	}

	/** No water: the ground covers the whole image (Shore = alignment row only). */
	void NoWaterEdge(FCtx& C, int32 ShoreRow)
	{
		C.Shore = ShoreRow;
		C.Edge.Init(C.TH + 8, C.TW);
		C.DockX = 0; C.DockW = 0; C.DockY0 = 0; C.DockY1 = 0;
	}

	void HomeFlags(FLayout& L)
	{
		L.bNoWater = true;
		L.bFireflies = true; L.bButterflies = true; L.bBirds = true;
		L.Current = 0.f;
	}

	const TArray<FColor>& HomePetals()
	{
		static const TArray<FColor> P = { Rgb(250, 110, 130), Rgb(255, 220, 90), Rgb(250, 250, 250), Rgb(170, 140, 250), Rgb(255, 160, 80) };
		return P;
	}

	/** Cottage lights (windows, door lantern), chimney smoke and the door of a placed cottage (House = top-left). */
	void CottageExtras(FCtx& C, FIntPoint House, int32 HouseBase)
	{
		C.L.Lights.Add({ FCtx::V(float(House.X + 18), float(House.Y + 43)), 34.f, Rgb(255, 190, 100), false, false, WindowRect(House, 13, 39, 10, 9) });
		C.L.Lights.Add({ FCtx::V(float(House.X + 60), float(House.Y + 43)), 34.f, Rgb(255, 190, 100), false, false, WindowRect(House, 55, 39, 10, 9) });
		C.L.Lights.Add({ FCtx::V(float(House.X + 47), float(House.Y + 38)), 24.f, Rgb(255, 204, 120), true, false, FFVRect() });
		C.L.Smokes.Add(FCtx::V(float(House.X + 59), float(House.Y - 1)));
		C.L.bDoor = true;
		C.L.DoorRect = FFVRect(float(House.X + CottageDoorX) * 2.f, float(House.Y + CottageDoorY) * 2.f, float(CottageDoorW) * 2.f, float(CottageDoorH) * 2.f);
		C.L.DoorFeet = FVector2D(C.L.DoorRect.Center().X, float(HouseBase) * 2.f + 14.f);
	}

	/** Flower strip along the cottage front (both sides of the door). */
	void CottageFlowers(FCtx& C, int32 HouseCX, int32 Row)
	{
		const TArray<FColor>& Pet = HomePetals();
		for (int32 X = HouseCX - 36; X <= HouseCX + 36; X += 2)
		{
			if (FMath::Abs(X - HouseCX) < 8) { continue; }
			const FColor P = Pet[(X / 2) % Pet.Num()];
			C.Img.Set(X, Row + 1, Rgb(66, 124, 56)); C.Img.Set(X, Row + 2, Rgb(56, 108, 48));
			C.Img.Set(X, Row, P); C.Img.Set(X + 1, Row + 1, Mul(P, 0.8f));
		}
		C.Reserve(HouseCX - 38, Row - 1, 78, 4);
	}

	/** UPDATE 4b: the practice pond of the yard — a small oval pond sunk into the lawn: a moist grass ring with pebbles
	    and flowers, the brown earth bank of the far (north) side, shallow-to-deep water over a sandy rim, reeds and
	    cattails at both ends and a little plank jetty from the north bank. Centre / radii in texels (layout space x 0.5).
	    Runtime extras: lily pads, swaying reeds, the lamp by the jetty (a y-sorted prop), glints and ripples (FVScene).
	    Hash-based only (no C.R calls): the rest of the yard keeps its random layout. Must match FVSceneRt::PondDist2. */
	void PracticePond(FCtx& C, float TCX, float TCY, float TRX, float TRY)
	{
		const FColor Sh = C.Loc.WaterShallow, Dp = C.Loc.WaterDeep;
		const FColor Moist = Rgb(72, 136, 58), Lip = Rgb(146, 204, 96), Sand = Rgb(206, 186, 134);
		const FColor Soil = Rgb(132, 92, 58), SoilLo = Rgb(98, 66, 42), Root = Rgb(160, 118, 76);
		const FColor Stone = Rgb(158, 152, 142), StoneHi = Rgb(206, 202, 190), StoneLo = Rgb(110, 104, 98);
		const int32 X0 = IFloor(TCX - TRX - 6.f), X1 = ICeil(TCX + TRX + 6.f);
		const int32 Y0 = IFloor(TCY - TRY - 6.f), Y1 = ICeil(TCY + TRY + 6.f);
		auto Dist2 = [TCX, TCY, TRX, TRY](float X, float Y, float Grow)
		{
			const float DX = (X - TCX) / FMath::Max(1.f, TRX + Grow), DY = (Y - TCY) / FMath::Max(1.f, TRY + Grow * 0.7f);
			return DX * DX + DY * DY;
		};
		auto TopAt = [TCX, TCY, TRX, TRY](float X)
		{
			const float DX = (X - TCX) / TRX;
			return DX * DX >= 1.f ? TCY : TCY - TRY * FMath::Sqrt(1.f - DX * DX);
		};
		auto IsWaterTx = [&Dist2, &TopAt](int32 X, int32 Y)
		{
			const float PX = float(X) + 0.5f, PY = float(Y) + 0.5f;
			return Dist2(PX, PY, 0.f) <= 1.f && PY - TopAt(PX) >= 2.6f;
		};
		const int32 PCX = IRound(TCX - 0.45f * TRX);   // the plank's centre column

		// ---- 1) the lawn around the water: a slightly sunk, moist ring; the near (south) edge catches the light
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const float PX = float(X) + 0.5f, PY = float(Y) + 0.5f;
				if (Dist2(PX, PY, 0.f) <= 1.f) { continue; }
				if (Dist2(PX, PY, 1.4f) <= 1.f)
				{
					C.Img.Set(X, Y, PY > TCY + TRY * 0.35f ? (R01(X, Y, 401) < 0.5f ? Lip : Mix(Lip, Moist, 0.4f)) : Moist);
				}
				else if (Dist2(PX, PY, 3.6f) <= 1.f)
				{
					MixPx(C.Img, X, Y, Moist, 0.35f + (Bayer(X, Y) - 0.5f) * 0.2f);
				}
			}
		}

		// ---- 2) the water: earth bank of the far side, shallow -> deep bands, the bank's shadow, a sandy near rim
		constexpr int32 Bands = 5;
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const float PX = float(X) + 0.5f, PY = float(Y) + 0.5f;
				const float E = Dist2(PX, PY, 0.f);
				if (E > 1.f) { continue; }
				const float FromTop = PY - TopAt(PX);
				if (PY < TCY && FromTop < 2.6f)
				{
					// grass overhang, soil, darker soil at the water line (a few roots)
					FColor Cc = FromTop < 0.9f ? Moist : (FromTop < 1.8f ? Soil : SoilLo);
					if (FromTop >= 0.9f && R01(X, Y, 402) < 0.12f) { Cc = Root; }
					C.Img.Set(X, Y, Cc);
					continue;
				}
				const float T = 1.f - FMath::Sqrt(E);   // 0 at the edge .. 1 in the middle
				const float V = FMath::Pow(T, 0.75f) * 1.25f + (VNoise(PX / 7.f, PY / 4.f, 403) - 0.5f) * 0.22f + (Bayer(X, Y) - 0.5f) / float(Bands);
				const int32 B = FMath::Clamp(FMath::FloorToInt(V * float(Bands)), 0, Bands - 1);
				FColor Cc = Mix(Sh, Dp, float(B) / float(Bands - 1));
				if (PY < TCY && FromTop < 4.6f) { Cc = Mul(Cc, 0.84f); }
				else if (PY > TCY && E > 0.8f) { Cc = Mix(Cc, Sand, 0.28f + (E - 0.8f) * 1.2f); }
				C.Img.Set(X, Y, Cc);
				if (PY > TCY && E > 0.93f && Bayer(X, Y) < 0.55f) { MixPx(C.Img, X, Y, White, 0.45f); }   // lapping water
			}
		}
		// pebbles on the sandy bottom, a few dark weed strands
		for (int32 k = 0; k < 9; ++k)
		{
			const float A = UE_PI * (0.08f + 0.84f * R01(k, 7, 404));
			const float Rr = 0.62f + 0.24f * R01(k, 9, 405);
			const int32 PX = IRound(TCX + FMath::Cos(A) * TRX * Rr), PY = IRound(TCY + FMath::Sin(A) * TRY * Rr);
			if (!IsWaterTx(PX, PY)) { continue; }
			MixPx(C.Img, PX, PY, Rgb(186, 170, 130), 0.35f);
			MixPx(C.Img, PX + 1, PY, Rgb(150, 136, 104), 0.3f);
		}
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 PX = IRound(TCX + (R01(k, 3, 406) - 0.5f) * TRX * 1.1f);
			const int32 PY = IRound(TCY + TRY * (0.05f + 0.35f * R01(k, 4, 407)));
			for (int32 j = 0; j < 3; ++j) { if (IsWaterTx(PX, PY - j)) { MixPx(C.Img, PX + (j == 1 ? 1 : 0), PY - j, Rgb(40, 90, 70), 0.4f); } }
		}

		// ---- 3) pebbles and a few bigger stones along the rim (not where the plank meets the bank), some flowers
		auto StoneAt = [&C, Stone, StoneHi, StoneLo](int32 SX, int32 SY, int32 SW, int32 SH)
		{
			for (int32 Y = 0; Y < SH; ++Y)
			{
				for (int32 X = 0; X < SW; ++X)
				{
					if (SW > 2 && (X == 0 || X == SW - 1) && (Y == 0 || Y == SH - 1)) { continue; }   // rounded corners
					FColor Cc = Stone;
					if (Y == 0 || (X == 0 && Y < SH - 1)) { Cc = StoneHi; }
					if (Y == SH - 1) { Cc = StoneLo; }
					C.Img.Set(SX + X, SY + Y, Cc);
				}
			}
			for (int32 X = 0; X < SW; ++X) { MulPx(C.Img, SX + X, SY + SH, 0.8f); }
		};
		const int32 NStones = FMath::Clamp(IRound(TRX * 0.55f), 10, 24);
		for (int32 k = 0; k < NStones; ++k)
		{
			const float A = UE_TWO_PI * (float(k) + 0.3f + 0.4f * R01(k, 11, 408)) / float(NStones);
			const float CA = FMath::Cos(A), SA = FMath::Sin(A);
			const float SX = TCX + CA * (TRX + 1.2f), SY = TCY + SA * (TRY + 0.9f);
			if (SA < -0.2f && FMath::Abs(SX - float(PCX)) < 10.f) { continue; }   // the plank lands here
			if (R01(k, 13, 409) < 0.3f) { continue; }                             // gaps: the grass reaches the water
			const bool bBig = R01(k, 17, 410) < 0.25f;
			StoneAt(IRound(SX) - (bBig ? 2 : 1), IRound(SY) - 1, bBig ? 4 : 3, bBig ? 3 : 2);
		}
		{
			const TArray<FColor>& Pet = HomePetals();
			const float FlowerA[5] = { -2.55f, -0.5f, 0.35f, 2.75f, 1.2f };
			for (int32 k = 0; k < 5 && Pet.Num() > 0; ++k)
			{
				const int32 FX = IRound(TCX + FMath::Cos(FlowerA[k]) * (TRX + 3.4f));
				const int32 FY = IRound(TCY + FMath::Sin(FlowerA[k]) * (TRY + 3.f));
				if (FMath::Abs(FX - PCX) < 10 && float(FY) < TCY) { continue; }
				const FColor P = Pet[k % Pet.Num()];
				C.Img.Set(FX, FY - 1, P); C.Img.Set(FX - 1, FY, P); C.Img.Set(FX + 1, FY, Mul(P, 0.85f)); C.Img.Set(FX, FY + 1, Mul(P, 0.8f));
				C.Img.Set(FX, FY, Rgb(250, 220, 90));
				C.Img.Set(FX, FY + 2, Rgb(60, 110, 50));
				C.L.FlowerSpots.Add(FCtx::V(float(FX), float(FY)));
			}
		}

		// ---- 4) reeds and cattails at both ends
		{
			const FColor S0 = Rgb(86, 138, 58), S1 = Rgb(124, 172, 76), S2 = Rgb(62, 102, 46);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const float Sg = Side == 0 ? -1.f : 1.f;
				for (int32 i = 0; i < 5; ++i)
				{
					const float XX = TCX + Sg * (TRX - 1.5f - float(i) * 1.4f);
					const float Half = TRY * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square((XX - TCX) / TRX)));
					const int32 SX = IRound(XX);
					const int32 Base = IRound(TCY + Half * (0.55f - 0.5f * R01(i, Side, 412)));
					const int32 H = 6 + IRound(5.f * R01(i, Side + 2, 413));
					const int32 Lean = R01(i, Side + 4, 414) < 0.4f ? -1 : (R01(i, Side + 6, 415) < 0.5f ? 0 : 1);
					for (int32 j = 0; j < H; ++j)
					{
						C.Img.Set(SX + (j > H / 2 ? Lean : 0), Base - j, j < 2 ? S2 : (j > H - 3 ? S1 : S0));
					}
					if (i == 1 || i == 3)
					{
						for (int32 j = H - 4; j < H - 1; ++j) { C.Img.Set(SX + Lean, Base - j, Rgb(120, 76, 42)); C.Img.Set(SX + Lean + 1, Base - j, Rgb(90, 56, 32)); }
					}
					MixPx(C.Img, SX - 1, Base + 1, White, 0.3f);
					MixPx(C.Img, SX + 1, Base + 1, White, 0.3f);
				}
			}
		}

		// ---- 5) the plank jetty: boards across it, from the grass of the north bank into the water
		const int32 PW = 12, PX0 = PCX - PW / 2;
		const int32 PT = IFloor(TopAt(float(PCX))) - 5;
		const int32 PB = IRound(TCY - 0.1f * TRY);
		const FColor WHi = Rgb(204, 150, 94), WBase = Rgb(170, 118, 72), WLo = Rgb(128, 84, 50), WGap = Rgb(76, 48, 32);
		const FColor WPost = Rgb(100, 64, 42), WPostHi = Rgb(146, 100, 64);
		for (int32 Y = PT; Y <= PB + 3; ++Y)
		{
			for (int32 X = PX0 + PW; X < PX0 + PW + 3; ++X) { if (IsWaterTx(X, Y)) { MulPx(C.Img, X, Y, 0.74f); } }
		}
		for (int32 Y = PB + 1; Y <= PB + 3; ++Y)
		{
			for (int32 X = PX0 + 1; X < PX0 + PW + 3; ++X) { if (IsWaterTx(X, Y)) { MulPx(C.Img, X, Y, 0.78f); } }
		}
		for (int32 Y = PT; Y < PB; ++Y)
		{
			const int32 Row = (Y - PT) % 4, Board = (Y - PT) / 4;
			const FColor Bc = Mix(WBase, WLo, R01(Board, 7, 416) * 0.45f);
			for (int32 X = PX0; X < PX0 + PW; ++X)
			{
				FColor Cc;
				if (Row == 3) { Cc = WGap; }
				else
				{
					Cc = Row == 0 ? Mix(Bc, WHi, 0.55f) : Bc;
					if (R01(X / 3, Y, 417 + uint32(Board)) < 0.14f) { Cc = Mix(Cc, WLo, 0.55f); }
				}
				if (X == PX0 || X == PX0 + PW - 1) { Cc = WGap; }
				else if (X == PX0 + 1 && Row != 3) { Cc = Mix(Cc, WHi, 0.5f); }
				else if (X == PX0 + PW - 2 && Row != 3) { Cc = Mix(Cc, WLo, 0.6f); }
				if (Row == 1 && (X == PX0 + 2 || X == PX0 + PW - 3)) { Cc = Rgb(84, 78, 80); }   // nails
				C.Img.Set(X, Y, Cc);
			}
		}
		for (int32 X = PX0; X < PX0 + PW; ++X) { C.Img.Set(X, PB, WLo); C.Img.Set(X, PB + 1, WGap); }
		auto Post = [&C, WPost, WPostHi, WGap](int32 PX, int32 PY, int32 Len)
		{
			for (int32 Y = PY; Y < PY + Len; ++Y)
			{
				for (int32 X = PX; X < PX + 3; ++X)
				{
					FColor Cc = WPost;
					if (Y == PY) { Cc = WPostHi; }
					else if (X == PX) { Cc = Mix(WPost, WPostHi, 0.5f); }
					else if (X == PX + 2) { Cc = Mix(WPost, WGap, 0.5f); }
					C.Img.Set(X, Y, Cc);
				}
			}
			for (int32 Y = PY - 1; Y <= PY + Len; ++Y) { C.Img.Set(PX - 1, Y, WGap); C.Img.Set(PX + 3, Y, WGap); }
			for (int32 X = PX; X < PX + 3; ++X) { C.Img.Set(X, PY - 1, WGap); C.Img.Set(X, PY + Len, WGap); }
		};
		const int32 BankRow = IRound(TopAt(float(PCX))) + 1;
		Post(PX0 - 1, BankRow, 3);
		Post(PX0 + PW - 2, BankRow, 3);
		Post(PX0 - 1, PB - 2, 5);
		Post(PX0 + PW - 2, PB - 2, 5);
		for (int32 X = PX0 - 3; X <= PX0 + 3; ++X) { if (IsWaterTx(X, PB + 4)) { MixPx(C.Img, X, PB + 4, White, 0.35f); } }
		for (int32 X = PX0 + PW - 4; X <= PX0 + PW + 2; ++X) { if (IsWaterTx(X, PB + 4)) { MixPx(C.Img, X, PB + 4, White, 0.35f); } }
		C.Reserve(X0, Y0, X1 - X0 + 1, Y1 - Y0 + 1);

		// ---- layout: the water oval, the plank, lily pads, swaying reeds, the lamp by the jetty
		FLayout& L = C.L;
		L.bPond = true;
		L.PondCenter = FCtx::V(TCX, TCY);
		L.PondRadii = FVector2D(TRX * 2.f, TRY * 2.f);
		L.PondPlank = FFVRect(float(PX0) * 2.f, float(PT) * 2.f, float(PW) * 2.f, float(PB - PT) * 2.f);
		auto Pad = [&L, TCX, TCY, TRX, TRY](float FX, float FY, uint8 Kind)
		{
			L.LilyPads.Add(FCtx::V(TCX + FX * TRX, TCY + FY * TRY));
			L.LilyKinds.Add(Kind);
		};
		Pad(0.55f, 0.3f, 1);
		Pad(0.18f, 0.62f, 2);
		Pad(-0.72f, 0.36f, 2);
		L.Reeds.Add(FCtx::V(TCX - TRX + 2.f, TCY + 2.f));
		L.Reeds.Add(FCtx::V(TCX + TRX - 2.5f, TCY + 1.f));
		L.Reeds.Add(FCtx::V(TCX + TRX - 6.f, TCY + TRY * 0.35f));
		const float LampX = float(PX0) - 5.f;
		AddProp(C, EDecor::LampPost, FCtx::V(LampX, TopAt(LampX) - 2.5f));
	}

	/** Home yard, landscape (320x180 texels). Canvas px = texel x 2. */
	void BuildHome(FCtx& C)
	{
		const FGround G{ Rgb(86, 154, 66), Rgb(110, 180, 76), Rgb(140, 204, 92), 11.f };
		NoWaterEdge(C, BgH);
		FillGround(C, 0, G, 23);
		const int32 HouseCX = 128, HouseBase = 74;
		PaintPath(C, { PV(HouseCX, HouseBase + 2), PV(HouseCX + 2, 92), PV(HouseCX + 12, 118), PV(HouseCX + 20, 148), PV(HouseCX + 22, 190) }, 3.4f);
		PaintPath(C, { PV(HouseCX + 12, 116), PV(176, 113), PV(204, 116) }, 2.2f);
		CottageFlowers(C, HouseCX, HouseBase + 2);
		SoilBed(C, 32, 128, 80, 18);
		Garden(C, 206, 104, 92, 34);
		Tufts(C, 240, 8, 176, G);
		Flowers(C, 90, 10, 176, HomePetals(), 5);

		// backdrop: tree line, oaks, the back fence, the cottage
		CanopyBand(C, 14, Rgb(72, 140, 58), 311, false);
		const FColor Apple = Rgb(222, 60, 50);
		C.AddObj(MakeOak(C.R, Rgb(84, 156, 62), 11, nullptr), 22, 50);
		C.AddObj(MakeOak(C.R, Rgb(76, 150, 60), 12, &Apple), 62, 44);
		C.AddObj(MakeOak(C.R, Rgb(88, 160, 66), 11, nullptr), 204, 46);
		C.AddObj(MakeOak(C.R, Rgb(80, 152, 62), 12, nullptr), 250, 40);
		C.AddObj(MakeOak(C.R, Rgb(82, 154, 60), 10, &Apple), 298, 50);
		const FColor Berry = Rgb(230, 70, 90);
		C.AddObj(MakeBush(C.R, Rgb(78, 150, 62), &Berry), 76, 72);
		C.AddObj(MakeBush(C.R, Rgb(84, 160, 66), nullptr, 6), 182, 72);
		FenceH(C, 2, 84, 72);
		FenceH(C, 172, 318, 72);
		const FIntPoint House = C.AddObj(MakeCottage(), HouseCX, HouseBase, 36.f, 4.f);
		C.PlaceObjs();
		CottageExtras(C, House, HouseBase);

		// y-sorted props in the garden
		AddProp(C, EDecor::Well, FVector2D(452.f, 176.f));
		AddProp(C, EDecor::Woodpile, FVector2D(364.f, 160.f));
		AddProp(C, EDecor::Barrel, FVector2D(188.f, 160.f));
		AddProp(C, EDecor::AppleTree, FVector2D(40.f, 220.f));
		AddProp(C, EDecor::AppleTree, FVector2D(608.f, 322.f), true);
		AddProp(C, EDecor::Scarecrow, FVector2D(506.f, 252.f));
		AddProp(C, EDecor::LampPost, FVector2D(318.f, 198.f));
		HomeFlags(C.L);
		C.WalkTopRow = HouseBase + 1;
		C.L.WalkBottom = 334.f;
		C.L.WalkMinX = 14.f;
		C.L.WalkMaxX = 626.f;
		C.L.Spawn = FVector2D(300.f, 298.f);
		C.L.DigSpots = { FVector2D(92.f, 274.f), FVector2D(144.f, 274.f), FVector2D(196.f, 274.f) };
		C.L.ResourceSlots = { FVector2D(44.f, 172.f), FVector2D(222.f, 184.f), FVector2D(380.f, 214.f), FVector2D(424.f, 300.f),
			FVector2D(560.f, 180.f), FVector2D(612.f, 218.f), FVector2D(244.f, 318.f), FVector2D(70.f, 316.f) };
		// update 4b: the practice pond below the vegetable garden (canvas 416..560 x 288..344), away from the door, the
		// dig bed and the spawn; the pickups that were there are refitted around it (WorldAnchors)
		PracticePond(C, 244.f, 158.f, 36.f, 14.f);
	}

	/** Home yard, portrait: the cottage at the top, the garden fills the tall walkable area below. */
	void BuildHomeP(FCtx& C)
	{
		const FGround G{ Rgb(86, 154, 66), Rgb(110, 180, 76), Rgb(140, 204, 92), 11.f };
		const FPFrame F = PortraitFrame(C);
		NoWaterEdge(C, F.Shore);
		FillGround(C, 0, G, 23);
		const int32 HouseCX = F.X(90), HouseBase = F.Hud + 96;
		const int32 Top = HouseBase + 1;
		// the feet stop 10 px above the bottom button dock (canvas VH - BottomBarH - 10)
		const float BotPx = 2.f * float(F.Shore) + FV::VH * 0.5f - FV::BottomBarH - 10.f;
		const int32 Bot = FMath::Clamp(FMath::FloorToInt(BotPx * 0.5f), Top + 60, C.TH - 4);
		auto Row = [Top, Bot](float T) { return Top + FMath::RoundToInt(T * float(Bot - Top)); };
		auto P = [&F, &Row](float Vx, float T) { return FVector2D(F.VX(Vx), float(Row(T)) * 2.f); };

		PaintPath(C, { PV(HouseCX, HouseBase + 2), PV(HouseCX + 4, Row(0.25f)), PV(HouseCX + 14, Row(0.62f)), PV(HouseCX + 8, C.TH + 6) }, 3.2f);
		PaintPath(C, { PV(HouseCX + 6, Row(0.16f)), PV(F.X(114), Row(0.2f)) }, 2.2f);
		CottageFlowers(C, HouseCX, HouseBase + 2);
		const int32 BedRow = Row(0.52f);
		SoilBed(C, F.X(14), BedRow, 62, 18);
		const int32 GardenRow = Row(0.16f);
		Garden(C, F.X(114), GardenRow, 60, 28);
		Tufts(C, Scaled(C, 240), 8, C.TH - 2, G);
		Flowers(C, Scaled(C, 90), 10, C.TH - 2, HomePetals(), 5);

		CanopyBand(C, F.Hud + 2, Rgb(72, 140, 58), 311, false);
		const FColor Apple = Rgb(222, 60, 50);
		const FColor Berry = Rgb(230, 70, 90);
		const int32 RowA = F.Vis(0.14f);
		C.AddObj(MakeOak(C.R, Rgb(84, 156, 62), 11, nullptr), F.X(10), RowA + 30);
		C.AddObj(MakeOak(C.R, Rgb(76, 150, 60), 12, &Apple), F.X(34), RowA + 6);
		C.AddObj(MakeOak(C.R, Rgb(88, 160, 66), 11, nullptr), F.X(150), RowA + 4);
		C.AddObj(MakeOak(C.R, Rgb(80, 152, 62), 10, &Apple), F.X(174), RowA + 34);
		C.AddObj(MakeBush(C.R, Rgb(78, 150, 62), &Berry), F.X(30), HouseBase - 2);
		C.AddObj(MakeBush(C.R, Rgb(84, 160, 66), nullptr, 6), F.X(152), HouseBase - 2);
		FenceH(C, F.X(-40), HouseCX - 42, HouseBase - 1);
		FenceH(C, HouseCX + 41, F.Right() + 40, HouseBase - 1);
		const FIntPoint House = C.AddObj(MakeCottage(), HouseCX, HouseBase, 36.f, 4.f);
		SideScenery(C, F, 4, [&Apple, &Berry](FRandomStream& R, int32 i)
		{
			if (i % 4 == 3) { return MakeBush(R, Rgb(80, 154, 64), (i & 1) ? &Berry : nullptr); }
			return MakeOak(R, Rgb(78 + (i % 3) * 4, 150 + (i % 2) * 6, 62), R.RandRange(9, 12), (i % 4 == 1) ? &Apple : nullptr);
		});
		C.PlaceObjs();
		CottageExtras(C, House, HouseBase);

		AddProp(C, EDecor::Well, P(314.f, 0.05f));
		AddProp(C, EDecor::Woodpile, P(52.f, 0.03f));
		AddProp(C, EDecor::AppleTree, P(34.f, 0.36f));
		AddProp(C, EDecor::AppleTree, P(36.f, 0.97f), true);   // (update 4b: moved off the practice pond's corner)
		AddProp(C, EDecor::Scarecrow, FVector2D(F.VX(286.f), float(GardenRow + 22) * 2.f));
		AddProp(C, EDecor::LampPost, P(164.f, 0.3f));
		HomeFlags(C.L);
		C.WalkTopRow = Top;
		C.L.WalkBottom = float(Bot) * 2.f;
		C.L.Spawn = P(206.f, 0.86f);
		const float DigY = float(BedRow + 9) * 2.f;
		C.L.DigSpots = { FVector2D(F.VX(50.f), DigY), FVector2D(F.VX(90.f), DigY), FVector2D(F.VX(130.f), DigY) };
		C.L.ResourceSlots = { P(64.f, 0.12f), P(256.f, 0.08f), P(326.f, 0.46f), P(150.f, 0.34f),
			P(118.f, 0.8f), P(262.f, 0.64f), P(44.f, 0.93f), P(318.f, 0.95f) };
		// update 4b: the practice pond in the lower right of the garden, right of the path (screen x 230..338); its
		// height follows the tall walkable area (26..40 px half height)
		const float PondTRY = FMath::Clamp(FMath::RoundToFloat(0.1f * float(Bot - Top)), 13.f, 20.f);
		PracticePond(C, float(F.X(142.f)), float(Row(0.74f)), 27.f, PondTRY);
	}

	// ---- house interior pieces (the wall band and the floor stay transparent: runtime decor tiles show through)
	void InteriorCeiling(FFVImage& Img, int32 WallTop)
	{
		const FColor Beam = Rgb(128, 84, 50), BeamHi = Rgb(168, 118, 72), BeamLo = Rgb(90, 58, 36), Ceil = Rgb(70, 48, 36);
		for (int32 Y = 0; Y < WallTop; ++Y)
		{
			for (int32 X = 0; X < Img.W; ++X)
			{
				FColor Cc = Ceil;
				const int32 Dy = WallTop - Y;   // 1 = last row
				if (Dy <= 5) { Cc = Dy == 5 ? BeamHi : (Dy == 1 ? BeamLo : ((X % 24) == 0 ? BeamLo : Beam)); }
				else if ((X + Y * 3) % 17 == 0) { Cc = Mul(Ceil, 0.85f); }
				Img.Set(X, Y, Cc);
			}
		}
		// soft shadow of the beam on the wallpaper
		const int32 A[4] = { 90, 60, 34, 14 };
		for (int32 k = 0; k < 4; ++k)
		{
			for (int32 X = 0; X < Img.W; ++X) { if (Img.Get(X, WallTop + k).A == 0) { Img.Set(X, WallTop + k, Rgb(24, 14, 10, A[k])); } }
		}
	}

	/** Baseboard at WallBot..WallBot+2 and the soft shadow it throws on the floor. */
	void InteriorBaseboard(FFVImage& Img, int32 WallBot)
	{
		for (int32 X = 0; X < Img.W; ++X)
		{
			Img.Set(X, WallBot, Rgb(170, 118, 72));
			Img.Set(X, WallBot + 1, (X % 32) == 0 ? Rgb(96, 62, 38) : Rgb(128, 84, 50));
			Img.Set(X, WallBot + 2, Rgb(88, 56, 34));
			Img.Set(X, WallBot + 3, Rgb(20, 12, 10, 96));
			Img.Set(X, WallBot + 4, Rgb(20, 12, 10, 48));
			Img.Set(X, WallBot + 5, Rgb(20, 12, 10, 18));
			// a faint shade just above the baseboard (the wall meets the floor)
			if (Img.Get(X, WallBot - 1).A == 0) { Img.Set(X, WallBot - 1, Rgb(24, 14, 10, 40)); }
		}
	}

	/** Window frame + sill; the glass (GX, GY, GW, GH) stays transparent: the sky is drawn behind at runtime. */
	void InteriorWindow(FFVImage& Img, int32 GX, int32 GY, int32 GW, int32 GH)
	{
		const FColor Fr = Rgb(150, 100, 60), FrHi = Rgb(198, 148, 96), FrLo = Rgb(100, 64, 40);
		for (int32 Y = GY - 2; Y < GY + GH + 2; ++Y)
		{
			for (int32 X = GX - 2; X < GX + GW + 2; ++X)
			{
				if (X >= GX && X < GX + GW && Y >= GY && Y < GY + GH) { Img.Set(X, Y, FColor(0, 0, 0, 0)); continue; }
				Img.Set(X, Y, (Y == GY - 2 || X == GX - 2) ? FrHi : ((Y == GY + GH + 1 || X == GX + GW + 1) ? FrLo : Fr));
			}
		}
		const int32 MX = GX + GW / 2 - 1, MY = GY + GH / 2;
		for (int32 Y = GY; Y < GY + GH; ++Y) { Img.Set(MX, Y, Fr); Img.Set(MX + 1, Y, FrLo); }
		for (int32 X = GX; X < GX + GW; ++X) { Img.Set(X, MY, Fr); Img.Set(X, MY + 1, FrLo); }
		for (int32 X = GX - 4; X < GX + GW + 4; ++X)
		{
			Img.Set(X, GY + GH + 2, FrHi); Img.Set(X, GY + GH + 3, Fr); Img.Set(X, GY + GH + 4, FrLo);
			Img.Set(X, GY + GH + 5, Rgb(20, 12, 10, 70)); Img.Set(X, GY + GH + 6, Rgb(20, 12, 10, 30));
		}
	}

	/** Plank door on the back wall (the exit) with a doormat on the floor. Y1 = first floor row. */
	void InteriorDoor(FFVImage& Img, int32 X0, int32 Y0, int32 W, int32 Y1)
	{
		const FColor Fr = Rgb(96, 60, 36), FrHi = Rgb(140, 94, 58), Pl = Rgb(150, 100, 60), PlLo = Rgb(122, 80, 48), PlHi = Rgb(176, 124, 78);
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X0 + W; ++X)
			{
				FColor Cc;
				if (X < X0 + 2 || X >= X0 + W - 2 || Y < Y0 + 2) { Cc = (X == X0 || Y == Y0) ? FrHi : Fr; }
				else
				{
					const int32 Bx = (X - X0 - 2) % 4;
					Cc = Bx == 0 ? PlLo : (Bx == 1 ? PlHi : Pl);
				}
				Img.Set(X, Y, Cc);
			}
		}
		// Z brace
		const int32 BY0 = Y0 + 8, BY1 = Y1 - 8;
		for (int32 X = X0 + 2; X < X0 + W - 2; ++X) { Img.Set(X, BY0, PlHi); Img.Set(X, BY0 + 1, PlLo); Img.Set(X, BY1, PlHi); Img.Set(X, BY1 + 1, PlLo); }
		for (int32 Y = BY0 + 2; Y < BY1; ++Y)
		{
			const int32 X = X0 + W - 3 - FMath::RoundToInt(float(Y - BY0 - 2) / float(FMath::Max(1, BY1 - BY0 - 2)) * float(W - 6));
			Img.Set(X, Y, PlHi); Img.Set(X + 1, Y, PlLo);
		}
		// knob
		const int32 KY = (Y0 + Y1) / 2;
		Img.Set(X0 + W - 5, KY, Rgb(240, 200, 96)); Img.Set(X0 + W - 5, KY + 1, Rgb(176, 136, 56));
		// doormat
		for (int32 Y = Y1 + 1; Y < Y1 + 6; ++Y)
		{
			for (int32 X = X0 + 1; X < X0 + W - 1; ++X)
			{
				const bool bEdge = Y == Y1 + 1 || Y == Y1 + 5 || X == X0 + 1 || X == X0 + W - 2;
				Img.Set(X, Y, bEdge ? Rgb(120, 84, 50) : (((X + Y) % 3) == 0 ? Rgb(160, 120, 74) : Rgb(190, 150, 96)));
			}
		}
	}

	/** Small wall shelf with jars (baked, cosy detail). */
	void InteriorShelf(FFVImage& Img, int32 X0, int32 Y)
	{
		for (int32 X = X0; X < X0 + 22; ++X) { Img.Set(X, Y, Rgb(176, 124, 78)); Img.Set(X, Y + 1, Rgb(120, 80, 48)); Img.Set(X, Y + 2, Rgb(20, 12, 10, 60)); }
		Img.Set(X0 + 2, Y + 2, Rgb(96, 62, 38)); Img.Set(X0 + 2, Y + 3, Rgb(96, 62, 38)); Img.Set(X0 + 19, Y + 2, Rgb(96, 62, 38)); Img.Set(X0 + 19, Y + 3, Rgb(96, 62, 38));
		const FColor Jar[3] = { Rgb(120, 170, 200), Rgb(210, 120, 70), Rgb(150, 190, 110) };
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 JX = X0 + 3 + k * 6;
			for (int32 Yy = Y - 5; Yy < Y; ++Yy) { for (int32 X = JX; X < JX + 4; ++X) { Img.Set(X, Yy, Yy == Y - 5 ? Rgb(200, 170, 120) : (X == JX ? Mix(Jar[k], White, 0.35f) : Jar[k])); } }
			Img.Set(JX, Y - 6, Rgb(150, 110, 70)); Img.Set(JX + 3, Y - 6, Rgb(150, 110, 70));
			for (int32 X = JX; X < JX + 4; ++X) { Img.Set(X, Y - 6, Rgb(170, 128, 80)); }
		}
		// hanging herbs
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 HX = X0 + 26 + k * 3;
			Img.Set(HX, Y - 10, Rgb(120, 80, 48));
			for (int32 Yy = Y - 9; Yy < Y - 3; ++Yy) { Img.Set(HX + ((Yy % 2) ? 1 : 0), Yy, (Yy % 3) ? Rgb(96, 150, 70) : Rgb(70, 120, 56)); }
		}
	}

	/** House interior, landscape (320x180 texels; canvas px = texel x 2). */
	void BuildHouse(FCtx& C)
	{
		NoWaterEdge(C, BgH);
		const int32 WallTop = 16, WallBot = 75, FloorTop = WallBot + 3, FrontRow = 173;
		C.Img.FillRect(0, 0, C.TW, C.TH, HouseDark);
		C.Img.FillRect(0, WallTop, C.TW, WallBot - WallTop, FColor(0, 0, 0, 0));
		C.Img.FillRect(0, FloorTop, C.TW, FrontRow - FloorTop, FColor(0, 0, 0, 0));
		InteriorCeiling(C.Img, WallTop);
		InteriorBaseboard(C.Img, WallBot);
		for (int32 X = 0; X < C.TW; ++X)
		{
			C.Img.Set(X, FrontRow - 2, Rgb(20, 12, 10, 50));
			C.Img.Set(X, FrontRow - 1, Rgb(20, 12, 10, 90));
			C.Img.Set(X, FrontRow, Rgb(150, 100, 60));
			C.Img.Set(X, FrontRow + 1, Rgb(110, 72, 44));
		}
		InteriorDoor(C.Img, 16, 34, 22, FloorTop);
		InteriorWindow(C.Img, 86, 26, 28, 24);
		InteriorWindow(C.Img, 206, 26, 28, 24);
		InteriorShelf(C.Img, 44, 38);

		FLayout& L = C.L;
		L.bNoWater = true;
		L.bInterior = true;
		L.Current = 0.f;
		const float BX = float(LandBleedX) * 2.f, BY = float(LandBleedY) * 2.f;
		L.WallRect = FFVRect(-BX, float(WallTop) * 2.f, float(BgW) * 2.f + 2.f * BX, float(WallBot - WallTop) * 2.f);
		L.FloorRect = FFVRect(-BX, float(FloorTop) * 2.f, float(BgW) * 2.f + 2.f * BX, float(BgH - FloorTop) * 2.f + BY);
		L.Windows = { FFVRect(172.f, 52.f, 56.f, 48.f), FFVRect(412.f, 52.f, 56.f, 48.f) };
		L.bDoor = true;
		L.DoorRect = FFVRect(32.f, 68.f, 44.f, float(FloorTop) * 2.f - 68.f);
		L.DoorFeet = FVector2D(54.f, 174.f);
		L.Stations.SetNum(int32(EFVStation::Count));
		L.Stations[int32(EFVStation::Kitchen)] = FVector2D(136.f, 184.f);
		L.Stations[int32(EFVStation::Workshop)] = FVector2D(242.f, 184.f);
		L.Stations[int32(EFVStation::TrophyWall)] = FVector2D(336.f, 92.f);
		L.Stations[int32(EFVStation::Bed)] = FVector2D(568.f, 200.f);
		L.Stations[int32(EFVStation::Smokehouse)] = FVector2D(84.f, 300.f);
		L.Stations[int32(EFVStation::WormFarm)] = FVector2D(192.f, 324.f);
		L.Stations[int32(EFVStation::Radio)] = FVector2D(302.f, 318.f);
		L.Stations[int32(EFVStation::Aquarium)] = FVector2D(440.f, 300.f);
		L.Stations[int32(EFVStation::IceCellar)] = FVector2D(556.f, 322.f);
		L.Stations[int32(EFVStation::Chest)] = FVector2D(598.f, 262.f);
		L.DresserFeet = FVector2D(440.f, 184.f);
		L.LampFeet = FVector2D(506.f, 224.f);
		L.PlantFeet = FVector2D(28.f, 212.f);
		L.RugCenter = FVector2D(330.f, 252.f);
		L.PaintingCenter = FVector2D(596.f, 86.f);
		C.WalkTopRow = FloorTop + 3;
		L.WalkBottom = 334.f;
		L.WalkMinX = 20.f;
		L.WalkMaxX = 620.f;
		L.Spawn = L.DoorFeet + FVector2D(0.f, 10.f);
	}

	/** House interior, portrait: the back wall under the top strip, a tall floor with the stations in rows. */
	void BuildHouseP(FCtx& C)
	{
		const FPFrame F = PortraitFrame(C);
		NoWaterEdge(C, F.Shore);
		const int32 WallTop = F.Hud + 2, WallBot = F.Hud + 58, FloorTop = WallBot + 3;
		C.Img.FillRect(0, 0, C.TW, C.TH, HouseDark);
		C.Img.FillRect(0, WallTop, C.TW, WallBot - WallTop, FColor(0, 0, 0, 0));
		C.Img.FillRect(0, FloorTop, C.TW, C.TH - FloorTop, FColor(0, 0, 0, 0));
		InteriorCeiling(C.Img, WallTop);
		InteriorBaseboard(C.Img, WallBot);
		InteriorDoor(C.Img, F.X(8), WallBot - 42, 20, FloorTop);
		InteriorWindow(C.Img, F.X(38), WallTop + 12, 20, 22);
		InteriorWindow(C.Img, F.X(122), WallTop + 12, 20, 22);

		FLayout& L = C.L;
		L.bNoWater = true;
		L.bInterior = true;
		L.Current = 0.f;
		const float Top = float(FloorTop + 3) * 2.f;
		const float BotPx = 2.f * float(F.Shore) + FV::VH * 0.5f - FV::BottomBarH - 10.f;
		const float Bot = FMath::Clamp(BotPx, Top + 140.f, float(C.TH) * 2.f - 8.f);
		auto P = [&F, Top, Bot](float Vx, float T) { return FVector2D(F.VX(Vx), FMath::Lerp(Top + 20.f, Bot - 4.f, T)); };
		const float Row0 = Top + 20.f;
		L.WallRect = FFVRect(0.f, float(WallTop) * 2.f, float(C.TW) * 2.f, float(WallBot - WallTop) * 2.f);
		L.FloorRect = FFVRect(0.f, float(FloorTop) * 2.f, float(C.TW) * 2.f, float(C.TH - FloorTop) * 2.f);
		L.Windows = { FFVRect(float(F.X(38)) * 2.f, float(WallTop + 12) * 2.f, 40.f, 44.f), FFVRect(float(F.X(122)) * 2.f, float(WallTop + 12) * 2.f, 40.f, 44.f) };
		L.bDoor = true;
		L.DoorRect = FFVRect(float(F.X(8)) * 2.f, float(WallBot - 42) * 2.f, 40.f, float(FloorTop - (WallBot - 42)) * 2.f);
		L.DoorFeet = FVector2D(F.VX(36.f), Top + 12.f);
		L.Stations.SetNum(int32(EFVStation::Count));
		L.Stations[int32(EFVStation::Kitchen)] = FVector2D(F.VX(108.f), Row0);
		L.Stations[int32(EFVStation::TrophyWall)] = FVector2D(F.VX(184.f), float(WallTop + 26) * 2.f);
		L.Stations[int32(EFVStation::Workshop)] = P(64.f, 0.3f);
		L.Stations[int32(EFVStation::Radio)] = P(190.f, 0.3f);
		L.Stations[int32(EFVStation::Bed)] = P(298.f, 0.32f);
		L.Stations[int32(EFVStation::Smokehouse)] = P(64.f, 0.62f);
		L.Stations[int32(EFVStation::Aquarium)] = P(196.f, 0.62f);
		L.Stations[int32(EFVStation::WormFarm)] = P(64.f, 0.94f);
		L.Stations[int32(EFVStation::IceCellar)] = P(190.f, 0.94f);
		L.Stations[int32(EFVStation::Chest)] = P(306.f, 0.94f);
		L.DresserFeet = FVector2D(F.VX(306.f), Row0);
		L.PlantFeet = FVector2D(F.VX(248.f), Row0 - 4.f);
		L.LampFeet = P(334.f, 0.64f);
		L.RugCenter = P(196.f, 0.46f) - FVector2D(0.f, 10.f);
		L.PaintingCenter = FVector2D(F.VX(324.f), float(WallTop + 22) * 2.f);
		C.WalkTopRow = FloorTop + 3;
		L.WalkBottom = Bot;
		L.Spawn = L.DoorFeet + FVector2D(0.f, 10.f);
	}

	/** Keeps new anchors (pickups, dig spots) off blockers, props, the dock, the spawn and each other. */
	struct FAnchorFit
	{
		const FLayout& L;
		TArray<FFVRect> Reserved;
		TArray<FVector2D> Taken;
		float MinY = 0.f, MaxY = 0.f;

		explicit FAnchorFit(const FLayout& InL) : L(InL)
		{
			MinY = L.WalkTop + 8.f;
			MaxY = (L.bNoWater ? L.WalkBottom : L.ShoreY - 2.f) - 8.f;
		}
		static FFVRect Grow(const FFVRect& R, float D) { return FFVRect(R.X - D, R.Y - D, R.W + 2.f * D, R.H + 2.f * D); }
		bool Ok(const FVector2D& P, float Clear) const
		{
			if (P.X < L.WalkMinX + 12.f || P.X > L.WalkMaxX - 12.f || P.Y < MinY || P.Y > MaxY) { return false; }
			if (L.Dock.W > 0.f && P.X > L.Dock.X - 16.f && P.X < L.Dock.Right() + 16.f && P.Y > L.Dock.Y - 16.f) { return false; }
			for (const FFVRect& B : L.Blockers) { if (Grow(B, Clear).Contains(P)) { return false; } }
			for (const FProp& Pr : L.Props) { if (FVector2D::Distance(Pr.Feet, P) < Clear + 10.f) { return false; } }
			for (const FFVRect& B : Reserved) { if (Grow(B, Clear).Contains(P)) { return false; } }
			for (const FVector2D& Tk : Taken) { if (FVector2D::Distance(Tk, P) < 30.f) { return false; } }
			if (FVector2D::Distance(P, L.Spawn) < 30.f) { return false; }
			if (L.bDoor && FVector2D::Distance(P, L.DoorFeet) < 30.f) { return false; }
			return true;
		}
		FVector2D Fit(const FVector2D& Want, float Clear = 10.f)
		{
			FVector2D Best = Want;
			if (!Ok(Want, Clear))
			{
				bool bFound = false;
				for (float Rad = 6.f; Rad <= 110.f && !bFound; Rad += 6.f)
				{
					for (int32 k = 0; k < 16; ++k)
					{
						const float A = float(k) / 16.f * UE_TWO_PI;
						const FVector2D Q = Want + FVector2D(FMath::Cos(A), FMath::Sin(A) * 0.7f) * Rad;
						if (Ok(Q, Clear)) { Best = Q; bFound = true; break; }
					}
				}
			}
			Best = FVector2D(FMath::RoundToFloat(float(Best.X)), FMath::RoundToFloat(float(Best.Y)));
			Taken.Add(Best);
			return Best;
		}
	};

	/** Stall / order board / net spots / resource slots / dig spots of the fishing locations (counts: UPDATE_HOME §6)
	    and the fitted resource slots of the yard. Landscape: picture canvas px (before the bleed translation);
	    portrait: texture px (X = visible-frame px through F.VX, Y = fraction of the walkable land band). */
	void WorldAnchors(FCtx& C, EFVTheme Theme, bool bPortrait)
	{
		FLayout& L = C.L;
		if (Theme == EFVTheme::HomeInterior) { return; }
		if (Theme == EFVTheme::Home)
		{
			FAnchorFit Fit(L);
			for (const FVector2D& D : L.DigSpots) { Fit.Reserved.Add(FFVRect(float(D.X) - 20.f, float(D.Y) - 16.f, 40.f, 32.f)); }
			if (L.bDoor) { Fit.Reserved.Add(FFVRect(L.DoorRect.X - 10.f, L.DoorRect.Y, L.DoorRect.W + 20.f, L.DoorRect.H + 30.f)); }
			if (L.bPond)
			{
				// update 4b: no pickups in / at the practice pond and none within reach of its fishing spots (a click
				// there must cast, not pick something up)
				const FVector2D PC = L.PondCenter, PR = L.PondRadii;
				Fit.Reserved.Add(FFVRect(float(PC.X - PR.X) - 4.f, float(PC.Y - PR.Y), float(PR.X) * 2.f + 8.f, float(PR.Y) * 2.f + 6.f));
				Fit.Reserved.Add(FFVRect(float(PC.X - PR.X) - 26.f, float(PC.Y - PR.Y) - 40.f, float(PR.X) * 2.f + 40.f, 40.f));
			}
			TArray<FVector2D> Want = L.ResourceSlots;
			L.ResourceSlots.Reset();
			for (const FVector2D& W : Want) { L.ResourceSlots.Add(Fit.Fit(W, 10.f)); }
			return;
		}

		FPFrame F;
		if (bPortrait) { F = PortraitFrame(C); }
		const float BandTop = L.WalkTop + 8.f, BandBot = L.ShoreY - 8.f;
		// hotfix 0.61: landscape picture px of canvas x = 0 (the wide pond: its left strip comes first; else 0)
		const float LO = bPortrait ? 0.f : 2.f * float(C.OX);
		// landscape: raw canvas px; portrait: (visible x, band fraction)
		auto Pt = [&](float X, float YOrT) -> FVector2D
		{
			return bPortrait ? FVector2D(F.VX(X), FMath::Lerp(BandTop, BandBot, YOrT)) : FVector2D(X + LO, YOrT);
		};
		auto NX = [&](float X) -> float { return bPortrait ? F.VX(X) : X + LO; };
		FVector2D Stall(-1.f, -1.f), Board(-1.f, -1.f);
		TArray<float> Nets;
		TArray<FVector2D> Res, Dig;
		switch (Theme)
		{
		case EFVTheme::Pond:
			if (bPortrait)
			{
				// update 6: below the HUD button column; hotfix 0.61 (the wide pond): 10 px left, clear of the ocean sign
				Stall = FVector2D(F.VX(C.OX > 0 ? 270.f : 280.f), L.WalkTop + 44.f);
				Board = FVector2D(F.VX(146.f), L.WalkTop + 10.f);
				Nets = { NX(70.f), NX(300.f) };
				Res = { Pt(56.f, 0.2f), Pt(120.f, 0.62f), Pt(236.f, 0.52f), Pt(336.f, 0.48f), Pt(40.f, 0.9f), Pt(206.f, 0.3f) };
				Dig = { Pt(84.f, 0.36f) };
			}
			else
			{
				Stall = FVector2D(492.f + LO, 140.f);   // update 6: left of the HUD button columns (x >= 571)
				Board = FVector2D(372.f + LO, 136.f);
				Nets = { NX(130.f), NX(262.f) };
				Res = { Pt(84.f, 178.f), Pt(130.f, 146.f), Pt(236.f, 176.f), Pt(300.f, 138.f), Pt(470.f, 180.f), Pt(618.f, 184.f) };
				Dig = { Pt(196.f, 152.f) };
			}
			break;
		case EFVTheme::River:
			if (bPortrait)
			{
				Nets = { NX(206.f), NX(300.f) };
				Res = { Pt(40.f, 0.15f), Pt(110.f, 0.55f), Pt(128.f, 0.12f), Pt(220.f, 0.82f), Pt(320.f, 0.35f), Pt(300.f, 0.08f) };
				Dig = { Pt(160.f, 0.55f) };
			}
			else
			{
				Nets = { 130.f, 356.f };
				Res = { Pt(60.f, 176.f), Pt(140.f, 160.f), Pt(226.f, 134.f), Pt(360.f, 140.f), Pt(500.f, 176.f), Pt(620.f, 140.f) };
				Dig = { Pt(318.f, 166.f) };
			}
			break;
		case EFVTheme::Mountain:
			if (bPortrait)
			{
				Nets = { NX(290.f) };
				Res = { Pt(100.f, 0.2f), Pt(110.f, 0.75f), Pt(220.f, 0.45f), Pt(300.f, 0.25f), Pt(260.f, 0.85f) };
			}
			else
			{
				Nets = { 200.f };
				Res = { Pt(60.f, 184.f), Pt(160.f, 140.f), Pt(250.f, 176.f), Pt(460.f, 150.f), Pt(540.f, 182.f) };   // (update 6: off the HUD column)
			}
			break;
		case EFVTheme::Ocean:
			if (bPortrait)
			{
				Stall = FVector2D(F.VX(96.f), L.WalkTop + 14.f);
				Nets = { NX(64.f), NX(318.f) };
				Res = { Pt(170.f, 0.28f), Pt(240.f, 0.12f), Pt(340.f, 0.8f), Pt(230.f, 0.7f), Pt(150.f, 0.62f) };
			}
			else
			{
				Stall = FVector2D(500.f, 138.f);   // update 6: left of the HUD button columns (x >= 571)
				Nets = { 110.f, 420.f };
				Res = { Pt(60.f, 170.f), Pt(150.f, 140.f), Pt(392.f, 136.f), Pt(512.f, 176.f), Pt(300.f, 124.f) };   // (update 6: the last one off the HUD column)
			}
			break;
		case EFVTheme::Swamp:
			if (bPortrait)
			{
				Nets = { NX(110.f), NX(316.f) };
				Res = { Pt(60.f, 0.2f), Pt(130.f, 0.6f), Pt(250.f, 0.3f), Pt(300.f, 0.8f), Pt(150.f, 0.82f), Pt(340.f, 0.35f) };
				Dig = { Pt(90.f, 0.4f) };
			}
			else
			{
				Nets = { 240.f, 522.f };
				Res = { Pt(60.f, 176.f), Pt(100.f, 140.f), Pt(212.f, 172.f), Pt(280.f, 140.f), Pt(372.f, 150.f), Pt(620.f, 176.f) };
				Dig = { Pt(196.f, 140.f) };
			}
			break;
		case EFVTheme::Volcano:
			if (bPortrait) { Res = { Pt(90.f, 0.25f), Pt(120.f, 0.7f), Pt(250.f, 0.2f), Pt(260.f, 0.8f) }; }
			else { Res = { Pt(60.f, 180.f), Pt(180.f, 150.f), Pt(460.f, 172.f), Pt(520.f, 136.f) }; }
			break;
		default:
			break;
		}

		FAnchorFit Fit(L);
		// update 6 (§12.1): no pickup / dig spot under the HUD button column at the top right
		if (L.ScrollRect.W > 0.f)
		{
			// hotfix 0.61: a scrolling layout — the column over the right end of the scroll range (the camera at its
			// right end), in the layout's own space (landscape picture px, portrait texture px: canvas y 0 = TopY)
			const float TopY = bPortrait ? float(L.ShoreY) - FV::VH * 0.5f : 0.f;
			Fit.Reserved.Add(bPortrait ? FFVRect(L.ScrollRect.Right() - 50.f, TopY, 50.f, 218.f) : FFVRect(L.ScrollRect.Right() - 72.f, 0.f, 72.f, 190.f));
		}
		else
		{
			Fit.Reserved.Add(bPortrait ? FFVRect(FV::VW - 50.f, 0.f, 50.f, 218.f) : FFVRect(FV::VW - 72.f, 0.f, 72.f, 190.f));
		}
		if (Theme == EFVTheme::Pond && C.OX > 0)
		{
			// hotfix 0.61: the wide pond keeps pickups / dig spots out of the corridors of its three walk exits (signs,
			// triggers and path decals of FVSceneU6Objects): river on the left, home (up) in the middle / top-left
			// corner, ocean on the right
			if (bPortrait)
			{
				Fit.Reserved.Add(FFVRect(F.VX(-36.f), L.WalkTop - 60.f, 100.f, float(L.ShoreY) - (L.WalkTop - 60.f)));
				Fit.Reserved.Add(FFVRect(F.VX(328.f), L.WalkTop, 68.f, 70.f));
			}
			else
			{
				Fit.Reserved.Add(FFVRect(LO - 64.f, 80.f, 84.f, 110.f));
				Fit.Reserved.Add(FFVRect(LO + 244.f, 60.f, 96.f, 84.f));
				Fit.Reserved.Add(FFVRect(LO + 630.f, 80.f, 74.f, 110.f));
			}
		}
		if (Stall.X >= 0.f)
		{
			L.bStall = true;
			L.StallFeet = Stall;
			Fit.Reserved.Add(FFVRect(float(Stall.X) - 62.f, float(Stall.Y) - 28.f, 124.f, 50.f));
		}
		if (Board.X >= 0.f)
		{
			L.bBoard = true;
			L.BoardFeet = Board;
			Fit.Reserved.Add(FFVRect(float(Board.X) - 36.f, float(Board.Y) - 20.f, 72.f, 40.f));
		}
		for (int32 n = 0; n < Nets.Num(); ++n)
		{
			const float X = Nets[n];
			const FVector2D Post(X, L.ShoreY - 4.f);
			// update 6 (§7, §17.3): the right net spot (index 1) of the ocean pier is the broken boat, in both
			// orientations; the reserved area stays the same, so the resource slots do not move
			if (Theme == EFVTheme::Ocean && n == 1)
			{
				L.bBoat = true;
				L.BoatFeet = Post;
			}
			else
			{
				L.NetSpots.Add(Post);
			}
			Fit.Reserved.Add(FFVRect(X - 20.f, float(Post.Y) - 26.f, 40.f, 30.f));
		}
		for (const FVector2D& D : Dig) { L.DigSpots.Add(Fit.Fit(D, 14.f)); }
		for (const FVector2D& R0 : Res) { L.ResourceSlots.Add(Fit.Fit(R0, 10.f)); }
	}

	FFVImage EllipseImg(int32 W, int32 H, const FColor& C)
	{
		FFVImage T(W, H);
		FillEllipse(T, float(W) * 0.5f, float(H) * 0.5f, float(W) * 0.5f, float(H) * 0.5f, C);
		return T;
	}

	FFVImage ReedFrame(int32 Lean, bool bCattail)
	{
		FFVImage T(9, 15);
		const FColor S0 = Rgb(88, 140, 60), S1 = Rgb(130, 180, 80), S2 = Rgb(60, 100, 46);
		const int32 Bases[4] = { 2, 4, 5, 7 };
		const int32 Heights[4] = { 10, 14, 12, 9 };
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 H = Heights[i];
			for (int32 j = 0; j < H; ++j)
			{
				const float F = float(j) / float(H);
				const int32 X = Bases[i] + FMath::RoundToInt(float(Lean) * F * F * 1.5f);
				T.Set(X, 14 - j, j > H - 3 ? S1 : (j < 3 ? S2 : S0));
			}
			if (bCattail && (i == 1 || i == 3))
			{
				for (int32 j = H - 5; j < H - 1; ++j)
				{
					const float F = float(j) / float(H);
					const int32 X = Bases[i] + FMath::RoundToInt(float(Lean) * F * F * 1.5f);
					T.Set(X, 14 - j, Rgb(124, 78, 44)); T.Set(X + 1, 14 - j, Rgb(92, 58, 34));
				}
			}
		}
		return T;
	}

	FFVImage PropImage(EDecor D)
	{
		FRandomStream R(int32(D) * 7919 + 17);
		switch (D)
		{
		case EDecor::LampPost:
		{
			FFVImage T = Ascii({
				TEXT("..ddd.."),
				TEXT(".ddddd."),
				TEXT(".dyYyd."),
				TEXT(".dyYyd."),
				TEXT(".dyyyd."),
				TEXT(".ddddd."),
				TEXT("...d..."),
				TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."),
				TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."), TEXT("...hd.."),
				TEXT("...hd.."), TEXT("...hd.."),
				TEXT("..hdd.."),
				TEXT(".hdddd."),
				TEXT(".dddddd") },
				{ { TEXT('d'), Rgb(52, 46, 56) }, { TEXT('h'), Rgb(96, 90, 104) }, { TEXT('y'), Rgb(255, 212, 118) }, { TEXT('Y'), Rgb(255, 246, 204) } });
			T.Outline(Rgb(30, 26, 36));
			return T;
		}
		case EDecor::Barrel:
		{
			FFVImage T = Ascii({
				TEXT("..oooooo.."),
				TEXT(".oTTTTTTo."),
				TEXT("oTttttttTo"),
				TEXT(".oTTTTTTo."),
				TEXT("oiiiiiiiio"),
				TEXT("ohBBBBBBlo"),
				TEXT("ohBBBBBBlo"),
				TEXT("oiiiiiiiio"),
				TEXT("ohBBBBBBlo"),
				TEXT("ohBBBBBBlo"),
				TEXT("oiiiiiiiio"),
				TEXT(".oooooooo.") },
				{ { TEXT('o'), Rgb(70, 42, 28) }, { TEXT('T'), Rgb(176, 124, 74) }, { TEXT('t'), Rgb(134, 90, 54) }, { TEXT('i'), Rgb(96, 96, 108) },
				  { TEXT('h'), Rgb(196, 138, 84) }, { TEXT('B'), Rgb(160, 104, 60) }, { TEXT('l'), Rgb(118, 74, 42) } });
			return T;
		}
		case EDecor::Bush: return MakeBush(R, Rgb(84, 158, 66), nullptr, 8);
		case EDecor::Stump:
		{
			FFVImage T = Ascii({
				TEXT("..oooooooo.."),
				TEXT(".oRRRRRRRRo."),
				TEXT("oRrrrRRrrrRo"),
				TEXT("oRrRRrrRRrRo"),
				TEXT(".oRRRRRRRRo."),
				TEXT("obBBBBBBBBlo"),
				TEXT("obBBlBBBBBlo"),
				TEXT("obBBBBBlBBlo"),
				TEXT("oooooooooooo") },
				{ { TEXT('o'), Rgb(62, 40, 26) }, { TEXT('R'), Rgb(200, 162, 112) }, { TEXT('r'), Rgb(170, 132, 88) },
				  { TEXT('b'), Rgb(142, 98, 62) }, { TEXT('B'), Rgb(112, 74, 46) }, { TEXT('l'), Rgb(82, 54, 34) } });
			return T;
		}
		case EDecor::Boulder: return MakeRock(R, 8, 5, Rgb(132, 130, 124), true, false);
		case EDecor::SnowBoulder: return MakeRock(R, 8, 5, Rgb(140, 140, 148), false, true);
		case EDecor::BasaltRock:
		{
			FFVImage T = MakeRock(R, 8, 5, Rgb(72, 62, 74), false, false);
			for (int32 X = 5; X < 12; ++X) { if (T.Get(X, 6 + (X % 2)).A > 0) { T.Set(X, 6 + (X % 2), X % 3 ? Rgb(236, 100, 36) : Rgb(255, 210, 100)); } }
			return T;
		}
		case EDecor::Campfire:
		{
			FFVImage T(18, 10);
			ThickLine(T, FVector2D(3.f, 7.f), FVector2D(14.f, 3.f), 2.f, 2.f, Bark);
			ThickLine(T, FVector2D(3.f, 3.f), FVector2D(14.f, 7.f), 2.f, 2.f, BarkLo);
			FillEllipse(T, 9.f, 5.5f, 2.2f, 1.2f, Rgb(40, 30, 28));
			for (int32 i = 0; i < 8; ++i)
			{
				const float A = float(i) / 8.f * UE_TWO_PI;
				const float SX = 9.f + FMath::Cos(A) * 7.5f, SY = 5.5f + FMath::Sin(A) * 3.3f;
				FillEllipse(T, SX, SY, 1.6f, 1.2f, i < 4 ? Rgb(150, 146, 140) : Rgb(120, 116, 112));
				T.Set(FMath::FloorToInt(SX) - 1, FMath::FloorToInt(SY) - 1, Rgb(190, 186, 178));
			}
			T.Outline(Rgb(46, 36, 34));
			return T;
		}
		case EDecor::Crates:
		{
			FFVImage T(18, 17);
			auto Crate = [&T](int32 X0, int32 Y0, int32 W, int32 H)
			{
				const FColor Wd = Rgb(186, 136, 80), WdHi = Rgb(214, 170, 110), WdLo = Rgb(140, 96, 56), Frame = Rgb(112, 74, 42);
				for (int32 Y = Y0; Y < Y0 + H; ++Y)
					for (int32 X = X0; X < X0 + W; ++X)
					{
						FColor C = (Y - Y0) < 2 ? WdHi : Wd;
						if (X == X0 || X == X0 + W - 1 || Y == Y0 + 2 || Y == Y0 + H - 1) { C = Frame; }
						else if (Y > Y0 + 2 && (X - X0) == (Y - Y0 - 2) * (W - 2) / FMath::Max(1, H - 4)) { C = Frame; }
						else if (Y > Y0 + 2 && ((Y - Y0) % 3 == 0)) { C = WdLo; }
						T.Set(X, Y, C);
					}
			};
			Crate(0, 5, 12, 12);
			Crate(10, 9, 8, 8);
			Crate(2, 0, 8, 6);
			T.Outline(Rgb(62, 38, 24));
			return T;
		}
		case EDecor::Lifebuoy:
		{
			FFVImage T(11, 21);
			for (int32 Y = 0; Y < 21; ++Y) { T.Set(5, Y, Rgb(150, 104, 62)); T.Set(6, Y, Rgb(110, 74, 44)); }
			for (int32 i = 0; i < 48; ++i)
			{
				const float A = float(i) / 48.f * UE_TWO_PI;
				for (float Rr = 2.4f; Rr <= 4.4f; Rr += 0.5f)
				{
					const int32 X = FMath::RoundToInt(5.5f + FMath::Cos(A) * Rr - 0.5f), Y = FMath::RoundToInt(8.f + FMath::Sin(A) * Rr);
					const bool bRed = (i / 6) % 2 == 0;
					T.Set(X, Y, bRed ? (Rr > 3.5f ? Rgb(190, 40, 40) : Rgb(230, 70, 60)) : (Rr > 3.5f ? Rgb(210, 210, 210) : Rgb(250, 250, 250)));
				}
			}
			T.Outline(Rgb(50, 34, 30));
			return T;
		}
		case EDecor::DeadStump:
		{
			FFVImage T = Ascii({
				TEXT("..o.o..o...."),
				TEXT(".oRoRooRo..."),
				TEXT("oRrrrrrrRo.."),
				TEXT(".oRRRRRRo..."),
				TEXT("obBBBBBlo.m."),
				TEXT("obBBlBBloMMm"),
				TEXT("obBBBBBlo.s."),
				TEXT("ooooooooooo.") },
				{ { TEXT('o'), Rgb(40, 34, 32) }, { TEXT('R'), Rgb(150, 136, 118) }, { TEXT('r'), Rgb(118, 106, 92) },
				  { TEXT('b'), Rgb(120, 108, 96) }, { TEXT('B'), Rgb(96, 86, 78) }, { TEXT('l'), Rgb(70, 62, 58) },
				  { TEXT('m'), Rgb(200, 90, 150) }, { TEXT('M'), Rgb(230, 130, 190) }, { TEXT('s'), Rgb(230, 220, 200) } });
			return T;
		}
		case EDecor::GlowShrooms:
		{
			FFVImage T(14, 10);
			const FFVImage A = MakeMushroom(Rgb(70, 206, 226), Rgb(210, 252, 255));
			const FFVImage B = MakeMushroom(Rgb(90, 180, 240), Rgb(220, 246, 255));
			T.Blit(A, 0, 1);
			T.Blit(B, 6, 2);
			T.Blit(A, 4, 0);
			return T;
		}
		case EDecor::Crystal: return MakeCrystal(R, 16, true);
		case EDecor::Brazier:
		{
			FFVImage T = Ascii({
				TEXT("hHHHHHHHHh"),
				TEXT("oEEEEEEEEo"),
				TEXT(".oBBBBBBo."),
				TEXT("..oBBBBo.."),
				TEXT("...oSSo..."),
				TEXT("...oSSo..."),
				TEXT("...oSso..."),
				TEXT("...oSso..."),
				TEXT("...oSso..."),
				TEXT("..oSSsso.."),
				TEXT(".oSSSssso."),
				TEXT(".oooooooo.") },
				{ { TEXT('h'), Rgb(140, 128, 136) }, { TEXT('H'), Rgb(110, 98, 108) }, { TEXT('E'), Rgb(236, 110, 40) }, { TEXT('o'), Rgb(30, 24, 30) },
				  { TEXT('B'), Rgb(84, 74, 84) }, { TEXT('S'), Rgb(76, 66, 76) }, { TEXT('s'), Rgb(52, 44, 54) } });
			return T;
		}
		case EDecor::YoungPine: return MakePine(R, 22, Rgb(46, 110, 78), true);
		case EDecor::BeachUmbrella:
		{
			FFVImage T(26, 30);
			for (int32 Y = 8; Y < 30; ++Y) { T.Set(12, Y, Rgb(220, 214, 200)); T.Set(13, Y, Rgb(170, 160, 150)); }
			for (int32 Y = 0; Y <= 9; ++Y)
			{
				const float Fy = float(Y) / 9.f;
				const int32 Hw = FMath::RoundToInt(3.f + FMath::Sqrt(Fy) * 9.5f);
				for (int32 X = 13 - Hw; X <= 12 + Hw; ++X)
				{
					const int32 Sector = FMath::FloorToInt((float(X) - 12.5f) / FMath::Max(1.f, float(Hw)) * 3.f + 3.f);
					FColor C = (Sector % 2 == 0) ? Rgb(230, 70, 64) : Rgb(250, 246, 236);
					if (Y >= 8) { C = Mul(C, 0.78f); }
					else if (X < 10) { C = Mix(C, White, 0.12f); }
					T.Set(X, Y, C);
				}
			}
			for (int32 X = 1; X < 25; X += 3) { T.Set(X, 10, (X / 3) % 2 ? Rgb(200, 50, 50) : Rgb(220, 216, 206)); }
			T.Outline(Rgb(70, 40, 40));
			return T;
		}
		case EDecor::Signpost:
		{
			FFVImage T = Ascii({
				TEXT("hhhhhhhhhhh..."),
				TEXT("BBBBBBBBBBBB.."),
				TEXT("BBaaaaaaaBBBB."),
				TEXT("BBBBBBBBBBBB.."),
				TEXT("lllllllllll..."),
				TEXT(".....pP......."),
				TEXT(".....pP......."),
				TEXT(".....pP......."),
				TEXT(".....pP......."),
				TEXT(".....pP......."),
				TEXT(".....pP......."),
				TEXT(".....pP......."),
				TEXT("....pPPP......") },
				{ { TEXT('h'), Rgb(214, 166, 108) }, { TEXT('B'), Rgb(182, 132, 80) }, { TEXT('a'), Rgb(110, 70, 40) },
				  { TEXT('l'), Rgb(134, 90, 54) }, { TEXT('p'), Rgb(150, 104, 62) }, { TEXT('P'), Rgb(108, 72, 42) } });
			T.Outline(Rgb(62, 38, 24));
			return T;
		}
		// ---- update 2: home yard props
		case EDecor::Well: return MakeWell();
		case EDecor::Woodpile:
		{
			FFVImage T(28, 15);
			auto LogEnd = [&T](int32 CX, int32 CY)
			{
				FillEllipse(T, float(CX) + 0.5f, float(CY) + 0.5f, 2.7f, 2.7f, Rgb(116, 78, 48));
				FillEllipse(T, float(CX) + 0.5f, float(CY) + 0.5f, 1.9f, 1.9f, Rgb(222, 180, 122));
				T.Set(CX, CY, Rgb(184, 138, 88));
				T.Set(CX - 1, CY - 1, Rgb(242, 208, 152));
			};
			for (int32 i = 0; i < 5; ++i) { LogEnd(3 + i * 5, 11); }
			for (int32 i = 0; i < 4; ++i) { LogEnd(5 + i * 5, 7); }
			for (int32 i = 0; i < 3; ++i) { LogEnd(8 + i * 5, 3); }
			T.Outline(Rgb(56, 36, 24));
			return T;
		}
		case EDecor::AppleTree:
		{
			const FColor Apple = Rgb(222, 60, 50);
			return MakeOak(R, Rgb(80, 154, 62), 11, &Apple);
		}
		case EDecor::Scarecrow:
		{
			FFVImage T = Ascii({
				TEXT(".....hhhh....."),
				TEXT("....hHHHHh...."),
				TEXT("..hhhhhhhhhh.."),
				TEXT(".....ffff....."),
				TEXT(".....fefe....."),
				TEXT(".....ffmf....."),
				TEXT("yssssCCCCssssy"),
				TEXT("y.s..CCCC..s.y"),
				TEXT(".....CcCC....."),
				TEXT(".....CCcC....."),
				TEXT(".....CCCC....."),
				TEXT("......pp......"),
				TEXT("......pp......"),
				TEXT("......pp......"),
				TEXT("......pp......"),
				TEXT("......pp......"),
				TEXT(".....pppp.....") },
				{ { TEXT('h'), Rgb(214, 176, 92) }, { TEXT('H'), Rgb(176, 64, 52) }, { TEXT('f'), Rgb(218, 190, 136) }, { TEXT('e'), Rgb(60, 40, 30) },
				  { TEXT('m'), Rgb(150, 100, 70) }, { TEXT('s'), Rgb(140, 100, 60) }, { TEXT('y'), Rgb(230, 200, 110) }, { TEXT('C'), Rgb(90, 112, 170) },
				  { TEXT('c'), Rgb(206, 124, 64) }, { TEXT('p'), Rgb(122, 86, 52) } });
			T.Outline(Rgb(56, 38, 30));
			return T;
		}
		default: break;
		}
		return FFVImage(2, 2, Rgb(255, 0, 255));
	}
}

namespace FVSceneArtLocal
{
	/** Hotfix 0.61 (a classic layout wider than the screen): the landscape picture of the wide pond also paints the
	    HUD pad right of its content for real (ScrollPadLand px), so the view at the right end shows no mirrored
	    twin of the right strip; the usual mirrored bleed follows beyond it. */
	constexpr int32 LandPadT = int32(ScrollPadLand * 0.5f);
	/** Portrait painted bleed of the wide pond: 42 instead of 40 texels, so every detail of the old one-screen
	    composition moves by a multiple of 4 texels (FCtx::NX = 20) and keeps its Bayer dither phase. */
	constexpr int32 PondPortBleedX = 42;

	/** Landscape bleed: the 320x180 picture mirrored outwards (land continues sideways, water downwards). */
	FFVImage MirrorExtend(const FFVImage& Src, int32 BX, int32 BY)
	{
		if (BX <= 0 && BY <= 0) { return Src; }
		FFVImage Out(Src.W + 2 * BX, Src.H + 2 * BY);
		auto Mirror = [](int32 V, int32 N)
		{
			if (V < 0) { V = -V - 1; }
			if (V >= N) { V = 2 * N - V - 1; }
			return FMath::Clamp(V, 0, N - 1);
		};
		for (int32 Y = 0; Y < Out.H; ++Y)
		{
			const int32 SY = Mirror(Y - BY, Src.H);
			for (int32 X = 0; X < Out.W; ++X) { Out.Px[Y * Out.W + X] = Src.Px[SY * Src.W + Mirror(X - BX, Src.W)]; }
		}
		return Out;
	}

	/** Interior bleed: the room continues sideways with plain columns (ColL / ColR) instead of mirrored doors and
	    windows; the rows above / below repeat the ceiling / the front edge. */
	FFVImage ClampExtend(const FFVImage& Src, int32 BX, int32 BY, int32 ColL, int32 ColR)
	{
		FFVImage Out(Src.W + 2 * BX, Src.H + 2 * BY);
		for (int32 Y = 0; Y < Out.H; ++Y)
		{
			const int32 SY = FMath::Clamp(Y - BY, 0, Src.H - 1);
			for (int32 X = 0; X < Out.W; ++X)
			{
				int32 SX = X - BX;
				if (SX < 0) { SX = ColL; }
				else if (SX >= Src.W) { SX = ColR; }
				Out.Px[Y * Out.W + X] = Src.Px[SY * Src.W + FMath::Clamp(SX, 0, Src.W - 1)];
			}
		}
		return Out;
	}

	FColor AverageColor(const FFVImage& Img, int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor& Fallback)
	{
		uint64 R = 0, G = 0, B = 0, N = 0;
		for (int32 Y = FMath::Max(0, Y0); Y < FMath::Min(Img.H, Y1); ++Y)
		{
			for (int32 X = FMath::Max(0, X0); X < FMath::Min(Img.W, X1); ++X)
			{
				const FColor& C = Img.Px[Y * Img.W + X];
				R += C.R; G += C.G; B += C.B; ++N;
			}
		}
		return N > 0 ? FColor(uint8(R / N), uint8(G / N), uint8(B / N), 255) : Fallback;
	}
}

namespace FVSceneArt
{
	FIntPoint CanvasBgSize()
	{
		if (!FV::IsPortrait()) { return FIntPoint(BgW + 2 * LandBleedX, BgH + 2 * LandBleedY); }
		const int32 W = FMath::Max(32, FMath::CeilToInt(FV::VW / FV::WorldScale - 0.01f));
		const int32 H = FMath::Max(64, FMath::CeilToInt(FV::VH / FV::WorldScale / 8.f - 0.001f) * 8);
		return FIntPoint(W + 2 * PortBleedX, H + 2 * PortBleedY);
	}

	int32 SideStrip(const FFVLocationDef& Loc, bool bPortrait)
	{
		if (Loc.SideWiden <= 0.f) { return 0; }
		if (Loc.Theme != EFVTheme::Pond)
		{
			// only the pond builders know the strip offset (FCtx::OX): any other theme stays one screen
			static bool bWarned = false;
			if (!bWarned)
			{
				bWarned = true;
				UE_LOG(LogFV, Warning, TEXT("FV: location '%s' has SideWiden %.2f, only the pond supports it - ignored"), *Loc.Id.ToString(), Loc.SideWiden);
			}
			return 0;
		}
		const float Base = float(bPortrait ? PortVisW : BgW);
		return FMath::Clamp(FMath::RoundToInt(Base * FMath::Min(Loc.SideWiden, 0.5f)), 0, 160);
	}

	FIntPoint CanvasBgSize(const FFVLocationDef& Loc)
	{
		const bool bPortrait = FV::IsPortrait();
		const int32 Side = SideStrip(Loc, bPortrait);
		if (Side <= 0) { return CanvasBgSize(); }
		const FIntPoint One = CanvasBgSize();
		// landscape: the wider picture + the painted HUD pad + the usual mirrored bleed; portrait: the wider content +
		// a slightly wider painted bleed (FVSceneArtLocal::PondPortBleedX, it holds the HUD pad)
		return bPortrait ? FIntPoint(One.X - 2 * PortBleedX + 2 * Side + 2 * FVSceneArtLocal::PondPortBleedX, One.Y)
			: FIntPoint(BgW + 2 * Side + FVSceneArtLocal::LandPadT + 2 * LandBleedX, One.Y);
	}

	FVector2D CanvasOrigin(const FLayout& T)
	{
		if (T.bPortrait)
		{
			// Centered horizontally; the shoreline lands exactly on the middle of the screen.
			return FVector2D(FMath::RoundToFloat(FV::VW * 0.5f - float(T.TexW)), FV::VH * 0.5f - T.ShoreY);
		}
		return FVector2D(-2.f * float(T.BleedX), -2.f * float(T.BleedY));
	}

	void TranslateLayout(FLayout& L, const FVector2D& D)
	{
		const float DX = float(D.X), DY = float(D.Y);
		L.ShoreY += DY;
		L.Dock = L.Dock.Offset(DX, DY);
		L.WalkTop += DY;
		L.WalkMinX += DX;
		L.WalkMaxX += DX;
		for (FFVRect& B : L.Blockers) { B = B.Offset(DX, DY); }
		for (FLight& Lt : L.Lights)
		{
			Lt.Pos += D;
			if (Lt.Window.W > 0.f) { Lt.Window = Lt.Window.Offset(DX, DY); }
		}
		for (FProp& P : L.Props) { P.Feet += D; }
		for (FVector2D& P : L.LilyPads) { P += D; }
		for (FVector2D& P : L.Reeds) { P += D; }
		for (FVector2D& P : L.FlowerSpots) { P += D; }
		for (FVector2D& P : L.Fires) { P += D; }
		for (FVector2D& P : L.Smokes) { P += D; }
		L.Spawn += D;
		L.Origin += D;
		// hotfix 0.61: the scroll range of a wide classic layout (W == 0 = one screen: stays zero)
		if (L.ScrollRect.W > 0.f) { L.ScrollRect = L.ScrollRect.Offset(DX, DY); }
		// ---- UPDATE 2 anchors
		L.WalkBottom += DY;
		for (FVector2D& P : L.ResourceSlots) { P += D; }
		for (FVector2D& P : L.DigSpots) { P += D; }
		for (FVector2D& P : L.NetSpots) { P += D; }
		L.StallFeet += D;
		L.BoardFeet += D;
		L.DoorFeet += D;
		if (L.DoorRect.W > 0.f) { L.DoorRect = L.DoorRect.Offset(DX, DY); }
		if (L.WallRect.W > 0.f) { L.WallRect = L.WallRect.Offset(DX, DY); }
		if (L.FloorRect.W > 0.f) { L.FloorRect = L.FloorRect.Offset(DX, DY); }
		for (FFVRect& W : L.Windows) { W = W.Offset(DX, DY); }
		for (FVector2D& C : L.CurtainSpots) { C += D; }
		for (FVector2D& P : L.Stations) { P += D; }
		L.DresserFeet += D;
		L.RugCenter += D;
		L.LampFeet += D;
		L.PlantFeet += D;
		L.PaintingCenter += D;
		// ---- UPDATE 4b: practice pond
		L.PondCenter += D;
		if (L.PondPlank.W > 0.f) { L.PondPlank = L.PondPlank.Offset(DX, DY); }
		// ---- UPDATE 6: the boat of the ocean pier (big worlds are never translated: world px)
		L.BoatFeet += D;
	}

	void BuildLocation(const FFVLocationDef& Loc, FFVImage& OutBg, FLayout& OutLayout)
	{
		// (hotfix 0.61: the landscape size of this location — the wide pond has its strips and the HUD pad)
		const int32 Side = SideStrip(Loc, false);
		BuildLocation(Loc, BgW + 2 * Side + (Side > 0 ? FVSceneArtLocal::LandPadT : 0) + 2 * LandBleedX, BgH + 2 * LandBleedY, OutBg, OutLayout);
	}

	void BuildLocation(const FFVLocationDef& Loc, int32 TexW, int32 TexH, FFVImage& OutBg, FLayout& OutLayout)
	{
		TexW = FMath::Clamp(TexW, 32, 1024);
		TexH = FMath::Clamp(TexH, 64, 1024);
		const bool bPortrait = TexH > TexW;
		OutLayout = FLayout();
		FRandomStream R(int32(GetTypeHash(Loc.Id) & 0x7FFFFFFF) ^ (int32(Loc.Theme) * 7919 + 1));
		// hotfix 0.61: side strips of a classic layout wider than the screen (the pond with SideWiden; 0 = one screen)
		const int32 Side = SideStrip(Loc, bPortrait);
		if (bPortrait)
		{
			OutBg.Init(TexW, TexH, Rgb(0, 0, 0));
			FCtx C(OutBg, OutLayout, Loc, R, TexW, TexH);
			if (Side > 0)
			{
				// content = the old 180-texel frame + a strip on each side, centred (CanvasOrigin centres the texture);
				// NX = where the old one-screen texture (180 + 2 * PortBleedX) sits inside this one
				const int32 OneW = FMath::Max(PortVisW, TexW - 2 * Side - 2 * (FVSceneArtLocal::PondPortBleedX - PortBleedX));
				C.OX = Side;
				C.ContentW = PortVisW + 2 * Side;
				C.NX = FMath::Max(0, (TexW - OneW) / 2);
			}
			switch (Loc.Theme)
			{
			case EFVTheme::River: BuildRiverP(C); break;
			case EFVTheme::Mountain: BuildMountainP(C); break;
			case EFVTheme::Ocean: BuildOceanP(C); break;
			case EFVTheme::Swamp: BuildSwampP(C); break;
			case EFVTheme::Volcano: BuildVolcanoP(C); break;
			case EFVTheme::Home: BuildHomeP(C); break;
			case EFVTheme::HomeInterior: BuildHouseP(C); break;
			case EFVTheme::Pond:
			default: BuildPondP(C); break;
			}
			OutLayout.ShoreY = float(C.Shore) * 2.f;
			OutLayout.Dock = FFVRect(float(C.DockX) * 2.f, float(C.DockY0) * 2.f, float(C.DockW) * 2.f, float(C.DockY1 - C.DockY0) * 2.f);
			OutLayout.WalkTop = C.WalkTopRow >= 0 ? float(C.WalkTopRow) * 2.f : OutLayout.ShoreY - 120.f;
			// the player (and the pet on his trail) stays fully on screen
			OutLayout.WalkMinX = float(C.VisX0) * 2.f + 18.f;
			OutLayout.WalkMaxX = float(C.VisX1) * 2.f - 18.f;
			OutLayout.BleedX = C.VisX0;
			OutLayout.BleedY = FMath::Clamp(PortBleedY, 0, TexH / 6);
			if (Side > 0)
			{
				// the camera shows the content + the HUD pad right of it (texture px; only X / W matter)
				OutLayout.ScrollRect = FFVRect(float(C.VisX0) * 2.f, 0.f, float(C.VisX1 - C.VisX0) * 2.f + ScrollPadPort, float(TexH) * 2.f);
			}
			WorldAnchors(C, Loc.Theme, true);
		}
		else
		{
			// The landscape picture is always 320x180 (exactly as before); the rest of the texture is mirrored bleed.
			// Hotfix 0.61: the wide pond paints (Side + 320 + Side) x 180 + the HUD pad right of it (LandPadT); the
			// old composition sits Side texels to the right (FCtx::OX / NX), picture texel Side = canvas x 0.
			const int32 PicW = BgW + (Side > 0 ? 2 * Side + FVSceneArtLocal::LandPadT : 0);
			const int32 BX = FMath::Max(0, (TexW - PicW) / 2), BY = FMath::Max(0, (TexH - BgH) / 2);
			FFVImage Pic(PicW, BgH, Rgb(0, 0, 0));
			FCtx C(Pic, OutLayout, Loc, R, PicW, BgH);
			if (Side > 0)
			{
				C.OX = Side;
				C.ContentW = BgW + 2 * Side;
				C.NX = Side;
				C.NW = BgW;
			}
			switch (Loc.Theme)
			{
			case EFVTheme::River: BuildRiver(C); break;
			case EFVTheme::Mountain: BuildMountain(C); break;
			case EFVTheme::Ocean: BuildOcean(C); break;
			case EFVTheme::Swamp: BuildSwamp(C); break;
			case EFVTheme::Volcano: BuildVolcano(C); break;
			case EFVTheme::Home: BuildHome(C); break;
			case EFVTheme::HomeInterior: BuildHouse(C); break;
			case EFVTheme::Pond:
			default: BuildPond(C); break;
			}
			OutLayout.ShoreY = float(C.Shore) * 2.f;
			OutLayout.Dock = FFVRect(float(C.DockX) * 2.f, float(C.DockY0) * 2.f, float(C.DockW) * 2.f, float(C.DockY1 - C.DockY0) * 2.f);
			OutLayout.WalkTop = C.WalkTopRow >= 0 ? float(C.WalkTopRow) * 2.f : FMath::Min(126.f, OutLayout.ShoreY - 56.f);
			const int32 ContentW = BgW + 2 * Side;   // (texels; = 320 for a one-screen layout)
			if (!OutLayout.bNoWater) { OutLayout.WalkMaxX = float(ContentW) * 2.f - 12.f; }
			if (Side > 0)
			{
				// the camera shows the content + the HUD pad right of it (picture px here, translated below)
				OutLayout.ScrollRect = FFVRect(0.f, 0.f, float(ContentW) * 2.f + ScrollPadLand, float(BgH) * 2.f);
			}
			WorldAnchors(C, Loc.Theme, false);
			OutBg = OutLayout.bInterior ? FVSceneArtLocal::ClampExtend(Pic, BX, BY, 2, PicW - 3) : FVSceneArtLocal::MirrorExtend(Pic, BX, BY);
			TranslateLayout(OutLayout, FVector2D(2.f * float(BX), 2.f * float(BY)));   // into texture space
			OutLayout.Origin = FVector2D::ZeroVector;
			OutLayout.BleedX = BX + Side;   // canvas x = 0 is picture texel Side (CanvasOrigin: -2 * BleedX)
			OutLayout.BleedY = BY;
			TexW = OutBg.W;
			TexH = OutBg.H;
		}
		OutLayout.TexW = TexW;
		OutLayout.TexH = TexH;
		OutLayout.bPortrait = bPortrait;
		if (OutLayout.Glint == FColor(235, 250, 255) && !OutLayout.bLava)
		{
			OutLayout.Glint = Mix(Loc.WaterShallow, White, 0.75f);
		}
		// Fallback fills for the (rare) area beyond the bleed.
		const int32 ShoreRow = FMath::Clamp(FMath::RoundToInt(OutLayout.ShoreY * 0.5f), 1, TexH - 1);
		OutLayout.FarTop = FVSceneArtLocal::AverageColor(OutBg, 0, 0, TexW, 2, OutLayout.FarTop);
		const FColor LandL = FVSceneArtLocal::AverageColor(OutBg, 0, 0, 2, ShoreRow - 2, OutLayout.FarLand);
		const FColor LandR = FVSceneArtLocal::AverageColor(OutBg, TexW - 2, 0, TexW, ShoreRow - 2, OutLayout.FarLand);
		OutLayout.FarLand = Mix(LandL, LandR, 0.5f);
		OutLayout.FarWater = FVSceneArtLocal::AverageColor(OutBg, 0, TexH - 2, TexW, TexH, FColor(Loc.WaterDeep));
		if (OutLayout.bInterior)
		{
			// the background has holes (wallpaper / floor / windows): the far fills are the dark outside of the room
			OutLayout.FarTop = HouseDark;
			OutLayout.FarLand = HouseDark;
			OutLayout.FarWater = HouseDark;
		}
		else if (OutLayout.bNoWater)
		{
			OutLayout.FarWater = OutLayout.FarLand;
		}
	}

	FFVImage BuildDecor(EDecor D)
	{
		switch (D)
		{
		case EDecor::Shadow: return EllipseImg(14, 5, Rgb(20, 16, 34, 80));
		case EDecor::ShadowSmall: return EllipseImg(9, 3, Rgb(20, 16, 34, 80));
		case EDecor::LilyPad:
		case EDecor::LilyPadFlower:
		{
			FFVImage T = Ascii({
				TEXT("..ggggg...."),
				TEXT(".gGhhGGgg.."),
				TEXT("gGhGGGG...."),
				TEXT("gGGGGGGGgg."),
				TEXT(".gGGGGGGGg."),
				TEXT("..ggggggg..") },
				{ { TEXT('g'), Rgb(44, 100, 52) }, { TEXT('G'), Rgb(86, 162, 72) }, { TEXT('h'), Rgb(134, 200, 100) } });
			if (D == EDecor::LilyPadFlower)
			{
				const FColor P = Rgb(244, 150, 196), PH = Rgb(255, 214, 234);
				T.Set(3, 1, PH); T.Set(4, 0, P); T.Set(5, 1, P); T.Set(4, 2, P); T.Set(4, 1, Rgb(250, 220, 90)); T.Set(3, 2, P); T.Set(5, 2, Mul(P, 0.8f));
			}
			return T;
		}
		case EDecor::LilyPadSmall:
			return Ascii({
				TEXT(".gggg."),
				TEXT("gGhG.."),
				TEXT("gGGGGg"),
				TEXT(".gggg.") },
				{ { TEXT('g'), Rgb(44, 100, 52) }, { TEXT('G'), Rgb(86, 162, 72) }, { TEXT('h'), Rgb(134, 200, 100) } });
		case EDecor::Reed0: return ReedFrame(-2, false);
		case EDecor::Reed1: return ReedFrame(0, false);
		case EDecor::Reed2: return ReedFrame(2, false);
		case EDecor::Cattail0: return ReedFrame(-2, true);
		case EDecor::Cattail1: return ReedFrame(0, true);
		case EDecor::Cattail2: return ReedFrame(2, true);
		case EDecor::Bird0:
			return Ascii({ TEXT("w.....w"), TEXT(".w...w."), TEXT("..www.."), TEXT("......."), TEXT(".......") }, { { TEXT('w'), White } });
		case EDecor::Bird1:
			return Ascii({ TEXT("......."), TEXT("......."), TEXT("..www.."), TEXT(".w...w."), TEXT("w.....w") }, { { TEXT('w'), White } });
		case EDecor::Gull0:
		{
			FFVImage T = Ascii({ TEXT("d.......d"), TEXT("ww.....ww"), TEXT(".ww...ww."), TEXT("..wwwwwy."), TEXT("........."), TEXT(".........") },
				{ { TEXT('w'), Rgb(250, 250, 250) }, { TEXT('d'), Rgb(60, 60, 70) }, { TEXT('y'), Rgb(250, 190, 60) } });
			T.Outline(Rgb(150, 156, 172));
			return T;
		}
		case EDecor::Gull1:
		{
			FFVImage T = Ascii({ TEXT("........."), TEXT("........."), TEXT("..wwwwwy."), TEXT(".ww...ww."), TEXT("ww.....ww"), TEXT("d.......d") },
				{ { TEXT('w'), Rgb(250, 250, 250) }, { TEXT('d'), Rgb(60, 60, 70) }, { TEXT('y'), Rgb(250, 190, 60) } });
			T.Outline(Rgb(150, 156, 172));
			return T;
		}
		case EDecor::Butterfly0:
			return Ascii({ TEXT("ww.ww"), TEXT("wwbww"), TEXT(".wbw."), TEXT("..b..") }, { { TEXT('w'), White }, { TEXT('b'), Rgb(50, 40, 40) } });
		case EDecor::Butterfly1:
			return Ascii({ TEXT("....."), TEXT(".wbw."), TEXT(".wbw."), TEXT("..b..") }, { { TEXT('w'), White }, { TEXT('b'), Rgb(50, 40, 40) } });
		case EDecor::JumpFish:
			return Ascii({
				TEXT("w...wwww.."),
				TEXT("ww.wwwwwww"),
				TEXT("wwwwwwwwdw"),
				TEXT("ww.wwwwww."),
				TEXT("w...www...") },
				{ { TEXT('w'), White }, { TEXT('d'), Rgb(40, 40, 50) } });
		case EDecor::Ripple0: case EDecor::Ripple1: case EDecor::Ripple2:
		case EDecor::Ripple3: case EDecor::Ripple4: case EDecor::Ripple5:
		{
			const int32 i = int32(D) - int32(EDecor::Ripple0);
			const int32 RX = 2 + i * 2;
			const int32 RY = FMath::Max(1, FMath::RoundToInt(float(RX) * 0.4f));
			FFVImage T(RX * 2 + 1, RY * 2 + 1);
			const int32 Steps = 24 + RX * 8;
			for (int32 k = 0; k < Steps; ++k)
			{
				const float A = float(k) / float(Steps) * UE_TWO_PI;
				const float S = FMath::Sin(A);
				const int32 X = FMath::RoundToInt(float(RX) + FMath::Cos(A) * float(RX));
				const int32 Y = FMath::RoundToInt(float(RY) + S * float(RY));
				T.Set(X, Y, Rgb(255, 255, 255, S > 0.f ? 255 : 150));
			}
			return T;
		}
		case EDecor::Fog:
		{
			// Soft wisp (drawn as a 96x24-texel box, see FVScene.cpp): elliptical smooth falloff with a wavy
			// noise-warped outline instead of a separable sine "pillow" (whose clamped plateau read as a
			// rectangle); an extra guard fades the texture border to exactly 0 so no straight edge survives the
			// ~6x bilinear stretch. The texture is 128x32 (same 4:1 aspect) for smoother gradients.
			const int32 W = 128, H = 32;
			FFVImage T(W, H);
			auto Smooth = [](float E0, float E1, float V) { const float S = FMath::Clamp((V - E0) / (E1 - E0), 0.f, 1.f); return S * S * (3.f - 2.f * S); };
			for (int32 Y = 0; Y < H; ++Y)
				for (int32 X = 0; X < W; ++X)
				{
					const float NX = (float(X) + 0.5f) / float(W) * 2.f - 1.f, NY = (float(Y) + 0.5f) / float(H) * 2.f - 1.f;
					// Wavy top / bottom contour and a slightly lumpy side outline.
					const float WarpY = (Fbm(float(X) / 16.f, 3.f, 405) - 0.5f) * 0.55f;
					const float WarpX = (Fbm(1.f, float(Y) / 6.f, 406) - 0.5f) * 0.25f;
					const float DX = NX * (1.f + WarpX), DY = NY + WarpY;
					const float Dist = FMath::Sqrt(DX * DX + DY * DY);
					float Env = 1.f - Smooth(0.15f, 1.f, Dist);
					Env *= Env;
					Env *= Smooth(0.f, 0.3f, 1.f - FMath::Abs(NY)) * Smooth(0.f, 0.18f, 1.f - FMath::Abs(NX));
					const float N = Fbm(float(X) / 14.f, float(Y) / 5.f, 404);
					const float A = FMath::Clamp(Env * (0.45f + 0.9f * N), 0.f, 1.f);
					T.Set(X, Y, Rgb(255, 255, 255, FMath::RoundToInt(A * 210.f)));
				}
			return T;
		}
		default: break;
		}
		return PropImage(D);
	}

	FVector2D DecorPivot(EDecor D)
	{
		switch (D)
		{
		case EDecor::Reed0: case EDecor::Reed1: case EDecor::Reed2:
		case EDecor::Cattail0: case EDecor::Cattail1: case EDecor::Cattail2:
			return FVector2D(0.5f, 1.f);
		case EDecor::Shadow: case EDecor::ShadowSmall:
		case EDecor::LilyPad: case EDecor::LilyPadFlower: case EDecor::LilyPadSmall:
		case EDecor::Bird0: case EDecor::Bird1: case EDecor::Gull0: case EDecor::Gull1:
		case EDecor::Butterfly0: case EDecor::Butterfly1: case EDecor::JumpFish:
		case EDecor::Ripple0: case EDecor::Ripple1: case EDecor::Ripple2:
		case EDecor::Ripple3: case EDecor::Ripple4: case EDecor::Ripple5:
		case EDecor::Fog:
			return FVector2D(0.5f, 0.5f);
		default:
			return FVector2D(0.5f, 1.f);
		}
	}

	FVector2D DecorBlocker(EDecor D)
	{
		switch (D)
		{
		case EDecor::LampPost: return FVector2D(10.f, 6.f);
		case EDecor::Barrel: return FVector2D(18.f, 8.f);
		case EDecor::Bush: return FVector2D(34.f, 10.f);
		case EDecor::Stump: return FVector2D(20.f, 8.f);
		case EDecor::Boulder: return FVector2D(30.f, 10.f);
		case EDecor::SnowBoulder: return FVector2D(30.f, 10.f);
		case EDecor::BasaltRock: return FVector2D(30.f, 10.f);
		case EDecor::Campfire: return FVector2D(30.f, 12.f);
		case EDecor::Crates: return FVector2D(34.f, 10.f);
		case EDecor::Lifebuoy: return FVector2D(8.f, 6.f);
		case EDecor::DeadStump: return FVector2D(18.f, 8.f);
		case EDecor::Crystal: return FVector2D(20.f, 8.f);
		case EDecor::Brazier: return FVector2D(14.f, 8.f);
		case EDecor::YoungPine: return FVector2D(10.f, 6.f);
		case EDecor::BeachUmbrella: return FVector2D(6.f, 4.f);
		case EDecor::Signpost: return FVector2D(6.f, 4.f);
		case EDecor::Well: return FVector2D(38.f, 10.f);
		case EDecor::Woodpile: return FVector2D(48.f, 8.f);
		case EDecor::AppleTree: return FVector2D(14.f, 6.f);
		case EDecor::Scarecrow: return FVector2D(8.f, 4.f);
		default: return FVector2D::ZeroVector;
		}
	}
}
