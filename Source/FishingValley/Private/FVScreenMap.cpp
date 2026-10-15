// World map: illustrated procedural map, location pins, travel & unlocking.
// Update 3: the details panel is a swipeable pager over the places (swipe / ←→ / pin tap slide it to the neighbour).
// Update 6 (§3) [SCREEN-MISC]: the map is wider than its window and scrolls left-right (finger / mouse drag with
// inertia, wheel), centred on the current place. The forest pin lies left of the river; the City lies right, beyond
// the sea (a dashed route from the ocean pier, «Нужна лодка»), the Cave is a mark next to the City. Closed places show
// their conditions; travel goes through TravelToEntry (the City by the voyage scene).

#include "FVScreens.h"
#include "FVScreenCommon.h"
#include "FVUIKit.h"
#include "InputCoreTypes.h"
#include "Algo/StableSort.h"

namespace FVMapScreenImpl
{
	constexpr float MapScale = 2.f;
	/** FFVLocationDef::MapPos is authored in the classic 640x360 map space (independent of the runtime canvas). */
	constexpr float MapPosRefW = 640.f;
	constexpr float MapPosRefH = 360.f;
	/** Update 6: the map space goes on to the right beyond the sea (the City and the Cave have MapPos.X > 640). */
	constexpr float MapPosMaxX = 900.f;
	static bool IsFarPin(const FFVLocationDef& L) { return L.MapPos.X > MapPosRefW; }
	/** Update 6: places of this update (the forest, the City, the Cave): conditions instead of a coin price. */
	static bool IsU6Place(FName Id) { return FVDBU6::IsForestId(Id) || Id == FVDBU6::CityId() || Id == FVDBU6::CaveId() || FVMill::IsMillId(Id); }

	/** Size of the procedural map image in texels. Landscape: the classic 200x148; portrait: tall & narrow. */
	struct FMapDims
	{
		int32 W = 200;
		int32 H = 148;
		bool operator==(const FMapDims& O) const { return W == O.W && H == O.H; }
		bool operator!=(const FMapDims& O) const { return !(*this == O); }
	};

	// Landscape layout (desktop) — unchanged classic layout.
	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	static const FVector2D MapOrigin(19.f, 40.f);
	static const FFVRect SideRect(425.f, 40.f, 194.f, 296.f);
	static const FMapDims LandscapeDims{ 200, 148 };

	/** Per-frame layout of the map screen (landscape: constants above; portrait: follows FV::VH). */
	struct FMapLayout
	{
		bool bPortrait = false;
		FFVRect Win;
		FMapDims Dims;
		FVector2D Origin;
		FFVRect MapRect;
		FFVRect Info;                 // details panel (landscape: the side panel)
		FFVRect Bar;                  // portrait: bottom action bar row (FVDraw::ActionBarRect: "← Назад" + Отправиться / Открыть за N)
		float PinK = 1.f;             // pin size multiplier (portrait: bigger, finger-sized pins)
		float LabelSize = 6.5f;       // pin name labels
		float PinHitHalf = 10.f;      // half size of a pin's tap/click target
	};

	static FMapLayout ComputeLayout()
	{
		FMapLayout L;
		L.bPortrait = FV::IsPortrait();
		if (!L.bPortrait)
		{
			L.Win = WinRect;
			L.Dims = LandscapeDims;
			L.Origin = MapOrigin;
			L.Info = SideRect;
			L.PinHitHalf = FV::IsTouchUI() ? 15.f : 10.f;
		}
		else
		{
			// Illustrated map on top at full width, the selected place's details below it, the action bar at the
			// bottom (thumb zone). The details get a fixed base height plus a share of the extra height of tall
			// phones; the map takes the rest (its image is rebuilt for that aspect, so it never stretches).
			// Only VH decides the split, so the map never changes size when another place is selected.
			L.Win = FV::WindowRect();
			L.Bar = FVDraw::ActionBarRect(L.Win);
			const float X0 = L.Win.X + 9.f;
			const float CW = L.Win.W - 18.f;
			const float Top = L.Win.Y + 18.f;
			const float InfoBottom = L.Bar.Y - 11.f;   // FVDraw::ActionBar draws its divider above the bar
			const float InfoH = FMath::RoundToFloat(FMath::Clamp(206.f + 0.2f * (FV::VH - 640.f), 196.f, 270.f));
			L.Dims.W = FMath::Max(60, FMath::FloorToInt(CW / MapScale));
			L.Dims.H = FMath::Clamp(FMath::FloorToInt((InfoBottom - InfoH - 8.f - Top) / MapScale), 80, 400);
			L.Origin = FVector2D(FMath::RoundToFloat(X0 + (CW - L.Dims.W * MapScale) * 0.5f), Top);
			const float MapBottom = Top + L.Dims.H * MapScale;
			L.Info = FFVRect(X0, MapBottom + 8.f, CW, FMath::Max(60.f, InfoBottom - MapBottom - 8.f - 7.f));   // 7 px: page dots
			L.PinK = 1.3f;
			L.LabelSize = FV::Ui::Small();
			L.PinHitHalf = 17.f;      // 34 px targets
		}
		L.MapRect = FFVRect(L.Origin.X, L.Origin.Y, L.Dims.W * MapScale, L.Dims.H * MapScale);
		return L;
	}

	namespace Terr
	{
		constexpr uint8 Grass = 0, Sea = 1, Sand = 2, River = 3, Lake = 4, Mountain = 5, Swamp = 6, Volcano = 7, Village = 8, Road = 9, Forest = 10;
	}

	/** Where the «Дом» pin goes (640x360 map space): the home def's MapPos when the data module placed it clear of
	    the other pins, otherwise a fixed spot south-east of the village pond. */
	static FVector2D HomeMapPos()
	{
		const FVector2D Fallback(378.f, 292.f);
		const FVector2D P = FVDB::HomeLocation().MapPos;
		if (P.Equals(FVector2D(320.f, 180.f), 0.5f)) { return Fallback; }   // FFVLocationDef default: not placed
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			if (FVector2D::Distance(L.MapPos, P) < 36.f) { return Fallback; }
		}
		return P;
	}

	/** Pin position in map texels, from FFVLocationDef::MapPos (640x360 map space, normalized into the map image). */
	static FVector2D PinTexel(const FFVLocationDef& L, const FMapDims& M)
	{
		const FVector2D Pos = FVDB::IsHomeId(L.Id) ? HomeMapPos() : L.MapPos;
		// update 6: far pins (the City, the Cave) lie on the extension right of the classic image
		// §18: the Old mill (MapPos.X < 0) sits at the very left edge, left of and below the forest pin
		const float U = IsFarPin(L) ? FMath::Clamp(Pos.X / MapPosRefW, 1.03f, MapPosMaxX / MapPosRefW - 0.04f)
			: (Pos.X < 0.f ? 0.035f : FMath::Clamp(Pos.X / MapPosRefW, 0.07f, 0.93f));
		const float V = FMath::Clamp(Pos.Y / MapPosRefH, 0.09f, 0.91f);
		return FVector2D(FMath::RoundToFloat(U * M.W), FMath::RoundToFloat(V * M.H));
	}

	/** Every pin of the map: the fishing locations (progression order) and «Дом» last (always open). */
	static TArray<const FFVLocationDef*> MapPins()
	{
		TArray<const FFVLocationDef*> Out;
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			Out.Add(&L);
			if (L.Id == FName(TEXT("river"))) { Out.Add(&FVDBU6::ForestLocation()); }   // update 6: the forest, a path from the river
		}
		if (!Out.Contains(&FVDBU6::ForestLocation())) { Out.Add(&FVDBU6::ForestLocation()); }
		// hotfix 0.6.1: the list follows the unlock level (the Old mill, level 10, right after the volcano; the City,
		// level 10 + the boat, after the mill)
		auto Rank = [](const FFVLocationDef* L) { return L->MinLevel * 4 + (L->Id == FVDBU6::CityId() ? 1 : 0); };
		Algo::StableSortBy(Out, Rank);
		Out.Add(&FVDB::HomeLocation());
		return Out;
	}

	/** The player is at this place (the house interior counts as «Дом»). */
	static bool IsCurrentPlace(const UFVSaveGame* P, FName PlaceId)
	{
		if (!P) { return false; }
		if (FVDB::IsHomeId(PlaceId)) { return FVDB::IsHomeId(P->CurrentLocation); }
		return P->CurrentLocation == PlaceId;
	}

	static FVector2D PinVirtual(const FFVLocationDef& L, const FMapLayout& Lay)
	{
		return Lay.Origin + PinTexel(L, Lay.Dims) * MapScale;
	}

	/** Layout facts shared by the image builder and the animated overlay. */
	struct FMapInfo
	{
		FMapDims Dims;
		bool bHas[int32(EFVTheme::Count)] = {};
		FVector2D Pin[int32(EFVTheme::Count)];
		float SeaX = 200 * 0.84f;
	};

	static FMapInfo ComputeInfo(const FMapDims& M)
	{
		FMapInfo I;
		I.Dims = M;
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			if (IsFarPin(L)) { continue; }   // update 6: drawn on the extension, not in the classic image
			const int32 T = FMath::Clamp(int32(L.Theme), 0, int32(EFVTheme::Count) - 1);
			if (!I.bHas[T])
			{
				I.bHas[T] = true;
				I.Pin[T] = PinTexel(L, M);
			}
		}
		const int32 Ocean = int32(EFVTheme::Ocean);
		I.SeaX = I.bHas[Ocean] ? FMath::Clamp(I.Pin[Ocean].X + 7.f, M.W * 0.62f, M.W * 0.95f) : M.W * 0.84f;
		return I;
	}

	static float CoastX(const FMapInfo& I, float Y)
	{
		return I.SeaX + 5.f * FMath::Sin(Y * 0.085f + 1.3f) + 2.5f * FMath::Sin(Y * 0.21f + 0.4f)
			+ (FVUI::ValueNoise(0.5f, Y / 9.f, 21) - 0.5f) * 5.f;
	}

	/** Update 6: width (texels) of the extension right of the classic image — the open sea and the far coast with
	    the City and the Cave. It is a part of the same procedural image (same palette, same pixel style). */
	static int32 ExtTexels(const FMapDims& M)
	{
		return FMath::RoundToInt((MapPosMaxX - MapPosRefW) / MapPosRefW * float(M.W));
	}

	/** Update 6: the far coast of the extension (texel X of the first land column at row Y; land lies to the right). */
	static float FarCoastX(const FMapDims& M, float Y)
	{
		return float(M.W) + float(ExtTexels(M)) * 0.36f + 3.f * FMath::Sin(Y * 0.075f + 0.8f) + 2.f * FMath::Sin(Y * 0.2f + 2.1f)
			+ (FVUI::ValueNoise(3.5f, Y / 8.f, 23) - 0.5f) * 4.f;
	}

	/** Update 6: the lighthouse on the far coast, south of the City (texel of the tower's lamp). */
	static FVector2D LighthouseTexel(const FMapDims& M)
	{
		const float Y = FMath::RoundToFloat(M.H * 0.84f);
		return FVector2D(FMath::RoundToFloat(FarCoastX(M, Y)) - 1.f, Y - 8.f);
	}

	// ----------------------------------------------------------------------------------------
	// Map image builder
	// ----------------------------------------------------------------------------------------
	struct FMapBuilder
	{
		int32 MapW = 200;             // full image width: the classic part + the update 6 extension
		int32 MapH = 148;
		int32 ClassicW = 200;         // the classic part (the pins' 640x360 space maps onto it)
		int32 ExtW = 0;               // update 6: the extension (open sea, the far coast, the City, the Cave)
		float FeatureK = 1.f;         // horizontal feature scale (narrow portrait maps shrink wide features a bit)
		float AreaK = 1.f;            // map area relative to the classic 200x148 (scales scatter counts)
		FFVImage Img;
		TArray<uint8> T;
		FMapInfo I;
		FRandomStream Rng;
		FRandomStream Rng2;           // update 6 art (a separate stream: the classic part stays exactly as it was)
		TArray<FVector2D> AllPins;
		FMapDims Dims;                // classic dims (PinTexel space)

		explicit FMapBuilder(const FMapDims& M)
			: MapW(M.W + ExtTexels(M)), MapH(M.H), ClassicW(M.W), ExtW(ExtTexels(M)), Img(M.W + ExtTexels(M), M.H, FColor(118, 180, 86, 255)),
			  Rng(20240917), Rng2(20260926), Dims(M)
		{
			FeatureK = FMath::Clamp(float(ClassicW) / 200.f, 0.75f, 1.f);
			AreaK = float(ClassicW * MapH) / float(200 * 148);
			T.Init(Terr::Grass, MapW * MapH);
			I = ComputeInfo(M);
			for (const FFVLocationDef& L : FVDB::Locations()) { AllPins.Add(PinTexel(L, M)); }
			AllPins.Add(PinTexel(FVDB::HomeLocation(), M));
			AllPins.Add(PinTexel(FVDBU6::ForestLocation(), M));
		}
		FVector2D PinById(FName Id, bool& bOk) const
		{
			const FFVLocationDef* L = FVDBU6::IsForestId(Id) ? &FVDBU6::ForestLocation() : FVDB::FindLocation(Id);
			bOk = L != nullptr;
			return L ? PinTexel(*L, Dims) : FVector2D::ZeroVector;
		}
		int32 Scaled(int32 Count) const { return FMath::Max(1, FMath::RoundToInt(float(Count) * AreaK)); }

		bool In(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < MapW && Y < MapH; }
		uint8 TAt(int32 X, int32 Y) const { return In(X, Y) ? T[Y * MapW + X] : Terr::Sea; }
		void SetT(int32 X, int32 Y, uint8 V) { if (In(X, Y)) { T[Y * MapW + X] = V; } }
		void Px(int32 X, int32 Y, FColor C, uint8 Type) { if (In(X, Y)) { Img.Set(X, Y, C); T[Y * MapW + X] = Type; } }
		bool Has(EFVTheme Th) const { return I.bHas[int32(Th)]; }
		FVector2D PinOf(EFVTheme Th) const { return I.Pin[int32(Th)]; }
		float MinPinDist(const FVector2D& P) const
		{
			float D = 1e9f;
			for (const FVector2D& Q : AllPins) { D = FMath::Min(D, FVector2D::Distance(P, Q)); }
			return D;
		}

		void Grass()
		{
			for (int32 Y = 0; Y < MapH; ++Y)
			{
				for (int32 X = 0; X < MapW; ++X)
				{
					const float N = FVUI::ValueNoise(X / 14.f, Y / 14.f, 11) * 0.7f + FVUI::ValueNoise(X / 5.f, Y / 5.f, 12) * 0.3f;
					FColor C = N < 0.36f ? FColor(106, 168, 78) : (N < 0.64f ? FColor(118, 180, 86) : FColor(132, 192, 94));
					const float H = FVUI::Hash01(X, Y, 13);
					if (H > 0.97f) { C = FColor(94, 150, 70); }
					else if (H > 0.955f) { C = FColor(150, 204, 106); }
					Img.Set(X, Y, C);
				}
			}
		}

		void Sea()
		{
			for (int32 Y = 0; Y < MapH; ++Y)
			{
				const float CX = CoastX(I, float(Y));
				const float FX = FarCoastX(Dims, float(Y));   // update 6: the far coast of the extension (mirrors the classic one)
				for (int32 X = 0; X < MapW; ++X)
				{
					const float D = X - CX;
					const float F = FX - X;
					if (D >= 0.f && F > 0.f)
					{
						const float DD = FMath::Min(D, F - 1.f);
						FColor C = DD < 2.f ? FColor(124, 202, 228) : FVUI::LerpColor(FColor(90, 170, 220), FColor(44, 100, 176), (DD - 2.f) / 16.f);
						Px(X, Y, C, Terr::Sea);
					}
					else if (D >= -2.f && D < 0.f)
					{
						Px(X, Y, D >= -1.f ? FColor(240, 222, 164) : FColor(222, 198, 140), Terr::Sand);
					}
					else if (F <= 0.f && F > -2.f)
					{
						Px(X, Y, F > -1.f ? FColor(240, 222, 164) : FColor(222, 198, 140), Terr::Sand);
					}
				}
			}
			// wave marks (update 6: the extension's open sea gets its own, from the second stream)
			for (int32 k = 0, NW = Scaled(40) * ExtW / FMath::Max(1, ClassicW); k < NW; ++k)
			{
				const int32 X = Rng2.RandRange(ClassicW, MapW - 3);
				const int32 Y = Rng2.RandRange(1, MapH - 2);
				if (TAt(X, Y) == Terr::Sea && TAt(X + 2, Y) == Terr::Sea && X > CoastX(I, float(Y)) + 4.f && X + 6.f < FarCoastX(Dims, float(Y)))
				{
					const FColor W(170, 214, 240);
					Img.Set(X, Y, W); Img.Set(X + 1, Y - 1, W); Img.Set(X + 2, Y, W);
				}
			}
			for (int32 k = 0, NW = Scaled(70); k < NW; ++k)
			{
				const int32 X = Rng.RandRange(0, ClassicW - 3);
				const int32 Y = Rng.RandRange(1, MapH - 2);
				if (TAt(X, Y) == Terr::Sea && TAt(X + 2, Y) == Terr::Sea && X > CoastX(I, float(Y)) + 4.f)
				{
					const FColor W(170, 214, 240);
					Img.Set(X, Y, W); Img.Set(X + 1, Y - 1, W); Img.Set(X + 2, Y, W);
				}
			}
			// pier at the ocean pin
			if (Has(EFVTheme::Ocean))
			{
				const FVector2D P = PinOf(EFVTheme::Ocean);
				const int32 PY = FMath::RoundToInt(P.Y) + 3;
				// the pier runs from the pin out into the sea
				const int32 X0 = FMath::RoundToInt(P.X) - 2;
				const int32 X1 = FMath::Max(X0 + 6, FMath::CeilToInt(CoastX(I, float(PY))) + 9);
				for (int32 X = FMath::Min(X0, X1); X <= FMath::Max(X0, X1); ++X)
				{
					Img.Set(X, PY, FColor(170, 116, 66));
					Img.Set(X, PY + 1, FColor(112, 72, 42));
					if ((X - X0) % 4 == 0) { Img.Set(X, PY + 2, FColor(90, 58, 36)); }
				}
				// little boat
				const int32 BX = FMath::Min(ClassicW - 8, X1 + 6), BY = PY + 6;
				for (int32 i = 0; i < 6; ++i) { Img.Set(BX + i, BY, FColor(150, 90, 50)); }
				for (int32 i = 1; i < 5; ++i) { Img.Set(BX + i, BY + 1, FColor(110, 64, 36)); }
				Img.Set(BX + 3, BY - 1, FColor(240, 240, 230)); Img.Set(BX + 3, BY - 2, FColor(240, 240, 230)); Img.Set(BX + 2, BY - 1, FColor(220, 220, 210));
			}
		}

		void Swamp()
		{
			if (!Has(EFVTheme::Swamp)) { return; }
			const FVector2D C = PinOf(EFVTheme::Swamp);
			const float R = 25.f * FeatureK;
			for (int32 Y = FMath::FloorToInt(C.Y - R - 4.f); Y <= FMath::CeilToInt(C.Y + R + 4.f); ++Y)
			{
				for (int32 X = FMath::FloorToInt(C.X - R - 4.f); X <= FMath::CeilToInt(C.X + R + 4.f); ++X)
				{
					if (!In(X, Y) || TAt(X, Y) != Terr::Grass) { continue; }
					const float D = FVector2D::Distance(FVector2D(X, Y), C) / (R * (0.8f + 0.4f * FVUI::ValueNoise(X / 6.f, Y / 6.f, 41)));
					if (D > 1.f) { continue; }
					const float N = FVUI::ValueNoise(X / 4.f, Y / 4.f, 42);
					const FColor Col = N < 0.35f ? FColor(86, 108, 66) : (N < 0.7f ? FColor(98, 122, 74) : FColor(112, 128, 80));
					Px(X, Y, Col, Terr::Swamp);
				}
			}
			// murky pools
			for (int32 k = 0; k < 8; ++k)
			{
				const float A = Rng.FRandRange(0.f, 2.f * PI);
				const float Rad = Rng.FRandRange(7.f, R * 0.75f);
				const FVector2D P = C + FVector2D(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad * 0.8f);
				const float RX = Rng.FRandRange(2.f, 4.f), RY = RX * 0.6f;
				if (TAt(FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y)) != Terr::Swamp) { continue; }
				FVUI::EllipsePx(Img, P.X, P.Y + 0.5f, RX + 0.8f, RY + 0.8f, FColor(70, 92, 60));
				FVUI::EllipsePx(Img, P.X, P.Y, RX, RY, FColor(62, 98, 92));
			}
			// main pool at the pin
			FVUI::EllipsePx(Img, C.X, C.Y + 0.5f, 8.f, 4.8f, FColor(66, 88, 58));
			FVUI::EllipsePx(Img, C.X, C.Y, 7.f, 4.f, FColor(58, 100, 98));
			for (int32 i = -3; i <= 1; ++i) { Img.Set(FMath::RoundToInt(C.X) + i, FMath::RoundToInt(C.Y) - 2, FColor(110, 150, 140)); }
			for (int32 Y = -5; Y <= 5; ++Y) { for (int32 X = -8; X <= 8; ++X) { const float E = (X / 7.f) * (X / 7.f) + (Y / 4.f) * (Y / 4.f); if (E <= 1.f) { SetT(FMath::RoundToInt(C.X) + X, FMath::RoundToInt(C.Y) + Y, Terr::Lake); } } }
			// reeds
			for (int32 k = 0; k < 60; ++k)
			{
				const int32 X = FMath::RoundToInt(C.X + Rng.FRandRange(-R, R));
				const int32 Y = FMath::RoundToInt(C.Y + Rng.FRandRange(-R, R));
				if (TAt(X, Y) != Terr::Swamp || TAt(X, Y - 2) != Terr::Swamp) { continue; }
				const int32 H = Rng.RandRange(2, 3);
				for (int32 j = 0; j < H; ++j) { Img.Set(X, Y - j, FColor(54, 86, 44)); }
				Img.Set(X, Y - H, FColor(128, 92, 52));
			}
			// dead trees
			for (int32 k = 0; k < 5; ++k)
			{
				const int32 X = FMath::RoundToInt(C.X + Rng.FRandRange(-R * 0.8f, R * 0.8f));
				const int32 Y = FMath::RoundToInt(C.Y + Rng.FRandRange(-R * 0.7f, R * 0.7f));
				if (TAt(X, Y) != Terr::Swamp || FVector2D::Distance(FVector2D(X, Y), C) < 9.f) { continue; }
				for (int32 j = 0; j < 5; ++j) { Img.Set(X, Y - j, FColor(104, 90, 80)); }
				Img.Set(X + 1, Y - 3, FColor(104, 90, 80)); Img.Set(X + 2, Y - 4, FColor(104, 90, 80));
				Img.Set(X - 1, Y - 2, FColor(84, 72, 66));
				Img.Set(X + 1, Y + 1, FColor(70, 90, 56));
			}
		}

		void House(int32 X, int32 Y, FColor Roof)
		{
			if (!In(X - 1, Y) || !In(X + 6, Y + 6)) { return; }
			for (int32 j = 0; j < 7; ++j) { for (int32 i = -1; i < 7; ++i) { const uint8 Ty = TAt(X + i, Y + j); if (Ty == Terr::Sea || Ty == Terr::Lake || Ty == Terr::River) { return; } } }
			const FColor RoofDark(FMath::Max(0, Roof.R - 50), FMath::Max(0, Roof.G - 30), FMath::Max(0, Roof.B - 20));
			for (int32 i = 1; i <= 4; ++i) { Px(X + i, Y, Roof, Terr::Village); }
			for (int32 i = 0; i <= 5; ++i) { Px(X + i, Y + 1, Roof, Terr::Village); }
			for (int32 i = -1; i <= 6; ++i) { Px(X + i, Y + 2, RoofDark, Terr::Village); }
			for (int32 j = 3; j <= 5; ++j)
			{
				for (int32 i = 0; i <= 5; ++i) { Px(X + i, Y + j, i == 5 ? FColor(206, 188, 150) : FColor(238, 224, 188), Terr::Village); }
			}
			Img.Set(X + 1, Y, FVUI::LerpColor(Roof, FColor(255, 255, 255), 0.3f));
			Img.Set(X + 2, Y + 4, FColor(120, 76, 44)); Img.Set(X + 2, Y + 5, FColor(120, 76, 44));
			Img.Set(X + 4, Y + 4, FColor(110, 160, 206));
			for (int32 i = 0; i <= 6; ++i) { FVUI::BlendPx(Img, X + i, Y + 6, FColor(40, 70, 30, 90)); }
		}

		/** «Дом»: a little farmstead (house, fence, garden beds) just above the home pin and a path to the village. */
		void Homestead()
		{
			const FVector2D H = PinTexel(FVDB::HomeLocation(), Dims);
			const int32 HX = FMath::RoundToInt(H.X), HY = FMath::RoundToInt(H.Y);
			// path to the village pond (only over grass)
			if (Has(EFVTheme::Pond))
			{
				const FVector2D C = PinOf(EFVTheme::Pond);
				const int32 Steps = FMath::Max(1, FMath::RoundToInt(FVector2D::Distance(C, H)));
				for (int32 k = 0; k <= Steps; ++k)
				{
					const float U = float(k) / float(Steps);
					const int32 X = FMath::RoundToInt(FMath::Lerp(C.X, H.X, U));
					const int32 Y = FMath::RoundToInt(FMath::Lerp(C.Y, H.Y, U) + FMath::Sin(U * PI) * 3.f);
					if (TAt(X, Y) == Terr::Grass) { Px(X, Y, FColor(208, 180, 124), Terr::Road); }
				}
			}
			// garden beds right of the house and a fence along the yard
			for (int32 j = 0; j < 4; ++j)
			{
				for (int32 i = 0; i < 5; ++i)
				{
					const int32 X = HX + 5 + i, Y = HY - 7 + j;
					if (TAt(X, Y) != Terr::Grass) { continue; }
					Px(X, Y, (j % 2 == 0) ? FColor(132, 92, 56) : FColor(96, 164, 70), Terr::Village);
				}
			}
			for (int32 i = -6; i <= 11; ++i)
			{
				if ((i & 1) == 0 && TAt(HX + i, HY - 1) == Terr::Grass) { Px(HX + i, HY - 1, FColor(150, 104, 64), Terr::Village); }
			}
			House(HX - 4, HY - 9, FColor(196, 72, 56));
		}

		void Village()
		{
			if (!Has(EFVTheme::Pond)) { return; }
			const FVector2D C = PinOf(EFVTheme::Pond);
			const int32 CX = FMath::RoundToInt(C.X), CY = FMath::RoundToInt(C.Y);
			// fields
			auto Field = [&](int32 FX, int32 FY, int32 W, int32 H)
			{
				for (int32 j = 0; j < H; ++j)
				{
					for (int32 i = 0; i < W; ++i)
					{
						if (TAt(FX + i, FY + j) != Terr::Grass) { continue; }
						Px(FX + i, FY + j, (j % 2 == 0) ? FColor(180, 150, 86) : FColor(124, 170, 66), Terr::Village);
					}
				}
			};
			Field(CX - 26, CY - 16, 11, 6);
			Field(CX + 13, CY + 9, 10, 6);
			// dirt paths
			for (int32 X = CX - 22; X <= CX + 22; ++X) { if (TAt(X, CY + 7) == Terr::Grass) { Px(X, CY + 7, FColor(208, 180, 124), Terr::Road); } }
			for (int32 Y = CY - 16; Y <= CY + 16; ++Y) { if (TAt(CX - 10, Y) == Terr::Grass) { Px(CX - 10, Y, FColor(208, 180, 124), Terr::Road); } }
			// pond at the pin
			FVUI::EllipsePx(Img, C.X, C.Y + 0.5f, 8.f, 5.5f, FColor(84, 150, 84));
			FVUI::EllipsePx(Img, C.X, C.Y, 7.f, 4.6f, FColor(100, 180, 228));
			FVUI::EllipsePx(Img, C.X - 1.f, C.Y - 1.f, 4.f, 2.f, FColor(134, 204, 238));
			for (int32 Y = -5; Y <= 5; ++Y) { for (int32 X = -8; X <= 8; ++X) { const float E = (X / 7.5f) * (X / 7.5f) + (Y / 5.f) * (Y / 5.f); if (E <= 1.f) { SetT(CX + X, CY + Y, Terr::Lake); } } }
			// lily pads
			Img.Set(CX + 3, CY + 1, FColor(70, 140, 60)); Img.Set(CX + 4, CY + 1, FColor(70, 140, 60)); Img.Set(CX - 4, CY + 2, FColor(70, 140, 60));
			Img.Set(CX + 4, CY, FColor(240, 150, 190));
			// houses
			House(CX - 19, CY - 6, FColor(196, 76, 56));
			House(CX - 9, CY - 16, FColor(70, 110, 180));
			House(CX + 9, CY - 13, FColor(196, 76, 56));
			House(CX + 15, CY - 1, FColor(170, 110, 60));
			House(CX - 20, CY + 9, FColor(120, 150, 70));
		}

		void Peak(int32 BX, int32 BY, int32 H)
		{
			for (int32 R = 0; R <= H; ++R)
			{
				const int32 Y = BY - H + R;
				const int32 HW = FMath::RoundToInt(R * 0.8f);
				const int32 SnowRows = FMath::RoundToInt(H * 0.34f) + ((FVUI::Hash01(BX, R, 51) > 0.5f) ? 1 : 0);
				for (int32 X = BX - HW; X <= BX + HW; ++X)
				{
					if (!In(X, Y)) { continue; }
					const uint8 Ty = TAt(X, Y);
					if (Ty == Terr::Sea || Ty == Terr::Lake) { continue; }
					const bool bLeft = X <= BX;
					FColor C = bLeft ? FColor(172, 160, 150) : FColor(126, 116, 112);
					if (R < SnowRows) { C = bLeft ? FColor(246, 248, 252) : FColor(204, 214, 232); }
					if (X == BX - HW || X == BX + HW) { C = FColor(88, 76, 74); }
					Px(X, Y, C, Terr::Mountain);
				}
			}
			// base shadow
			for (int32 X = BX - FMath::RoundToInt(H * 0.8f); X <= BX + FMath::RoundToInt(H * 0.8f); ++X)
			{
				if (TAt(X, BY + 1) == Terr::Grass) { FVUI::BlendPx(Img, X, BY + 1, FColor(40, 70, 40, 80)); }
			}
		}

		void Mountains()
		{
			struct FPeak { int32 X, Y, H; };
			TArray<FPeak> Peaks;
			const float VolcX = Has(EFVTheme::Volcano) ? PinOf(EFVTheme::Volcano).X : MapW * 0.8f;
			// a range along the top edge
			for (int32 X = 6; X < VolcX - 26.f && X < CoastX(I, 10.f) - 10.f; X += 9)
			{
				const int32 PX = X + Rng.RandRange(-2, 2);
				const int32 PY = Rng.RandRange(11, 16);
				if (MinPinDist(FVector2D(PX, PY - 4)) < 12.f) { continue; }
				Peaks.Add({ PX, PY, Rng.RandRange(8, 13) });
			}
			if (Has(EFVTheme::Mountain))
			{
				const FVector2D C = PinOf(EFVTheme::Mountain);
				for (int32 k = 0; k < 9; ++k)
				{
					const float A = (float(k) / 9.f) * 2.f * PI + Rng.FRandRange(-0.2f, 0.2f);
					const float Rad = Rng.FRandRange(12.f, 22.f);
					const int32 PX = FMath::RoundToInt(C.X + FMath::Cos(A) * Rad);
					const int32 PY = FMath::RoundToInt(C.Y + FMath::Sin(A) * Rad * 0.6f) + 6;
					bool bNearOther = false;
					for (const FVector2D& Q : AllPins)
					{
						if (FVector2D::Distance(Q, C) > 1.f && FVector2D::Distance(Q, FVector2D(PX, PY - 4)) < 12.f) { bNearOther = true; }
					}
					if (bNearOther || (FMath::Abs(PX - C.X) < 9.f && PY - C.Y < 9.f && PY > C.Y - 2.f)) { continue; }
					Peaks.Add({ PX, PY, Rng.RandRange(9, 15) });
				}
			}
			Peaks.Sort([](const FPeak& A, const FPeak& B) { return A.Y < B.Y; });
			for (const FPeak& P : Peaks) { Peak(P.X, P.Y, P.H); }

			if (Has(EFVTheme::Mountain))
			{
				const FVector2D C = PinOf(EFVTheme::Mountain);
				FVUI::EllipsePx(Img, C.X, C.Y + 0.5f, 7.5f, 4.5f, FColor(150, 146, 140));
				FVUI::EllipsePx(Img, C.X, C.Y, 6.5f, 3.6f, FColor(108, 186, 232));
				FVUI::EllipsePx(Img, C.X - 1.5f, C.Y - 1.f, 3.f, 1.4f, FColor(170, 220, 246));
				for (int32 Y = -4; Y <= 4; ++Y) { for (int32 X = -7; X <= 7; ++X) { const float E = (X / 7.f) * (X / 7.f) + (Y / 4.f) * (Y / 4.f); if (E <= 1.f) { SetT(FMath::RoundToInt(C.X) + X, FMath::RoundToInt(C.Y) + Y, Terr::Lake); } } }
			}
		}

		void Volcano()
		{
			if (!Has(EFVTheme::Volcano)) { return; }
			const FVector2D C = PinOf(EFVTheme::Volcano);
			// scorched ground
			const float SR = 30.f * FeatureK;
			for (int32 Y = FMath::FloorToInt(C.Y - SR); Y <= FMath::CeilToInt(C.Y + SR); ++Y)
			{
				for (int32 X = FMath::FloorToInt(C.X - SR); X <= FMath::CeilToInt(C.X + SR); ++X)
				{
					if (!In(X, Y)) { continue; }
					const uint8 Ty = TAt(X, Y);
					if (Ty != Terr::Grass && Ty != Terr::Mountain) { continue; }
					const float D = FVector2D::Distance(FVector2D(X, Y), C) / SR * (0.85f + 0.3f * FVUI::ValueNoise(X / 5.f, Y / 5.f, 61));
					if (D >= 1.f) { continue; }
					const float K = FMath::Pow(1.f - D, 0.6f) * 0.9f;
					FColor Base = Img.Get(X, Y);
					FColor Ash = FVUI::Hash01(X, Y, 62) > 0.9f ? FColor(84, 74, 72) : FColor(122, 102, 86);
					Img.Set(X, Y, FVUI::LerpColor(Base, Ash, K));
					if (D < 0.72f && Ty == Terr::Grass) { SetT(X, Y, Terr::Volcano); }
				}
			}
			// cone
			const float TopY = C.Y - 2.f, BaseY = C.Y + 16.f;
			for (int32 Y = FMath::FloorToInt(TopY); Y <= FMath::CeilToInt(BaseY); ++Y)
			{
				const float K = (Y - TopY) / (BaseY - TopY);
				const float HW = FMath::Lerp(7.f, 24.f * FeatureK, FMath::Pow(FMath::Clamp(K, 0.f, 1.f), 0.8f));
				for (int32 X = FMath::FloorToInt(C.X - HW); X <= FMath::CeilToInt(C.X + HW); ++X)
				{
					if (!In(X, Y)) { continue; }
					const bool bLeft = X <= C.X;
					FColor Col = bLeft ? FColor(116, 92, 86) : FColor(80, 64, 66);
					if (FVUI::Hash01(X, Y, 63) > 0.86f) { Col = bLeft ? FColor(132, 108, 98) : FColor(70, 56, 58); }
					if (FMath::Abs(X - C.X) >= HW - 1.f) { Col = FColor(52, 40, 42); }
					Px(X, Y, Col, Terr::Volcano);
				}
			}
			// lava streams
			const int32 Starts[3] = { -4, 1, 5 };
			for (int32 s = 0; s < 3; ++s)
			{
				float X = C.X + Starts[s];
				const int32 Len = Rng.RandRange(8, 15);
				for (int32 j = 0; j < Len; ++j)
				{
					const int32 Y = FMath::RoundToInt(C.Y + 2.f + j);
					X += (Starts[s] < 0 ? -0.35f : (Starts[s] > 2 ? 0.35f : 0.f)) + Rng.FRandRange(-0.4f, 0.4f);
					const int32 IX = FMath::RoundToInt(X);
					if (TAt(IX, Y) != Terr::Volcano) { break; }
					Img.Set(IX, Y, j < Len - 3 ? FColor(255, 150, 50) : FColor(210, 80, 30));
					if (j % 3 == 0) { Img.Set(IX + 1, Y, FColor(236, 96, 36)); }
				}
			}
			// crater lava lake (at the pin)
			FVUI::EllipsePx(Img, C.X, C.Y + 0.5f, 7.f, 3.2f, FColor(60, 44, 44));
			FVUI::EllipsePx(Img, C.X, C.Y, 6.f, 2.6f, FColor(255, 110, 36));
			FVUI::EllipsePx(Img, C.X, C.Y - 0.3f, 3.5f, 1.3f, FColor(255, 206, 90));
		}

		void River()
		{
			if (!Has(EFVTheme::River)) { return; }
			const FVector2D Mid = PinOf(EFVTheme::River);
			const FVector2D Start = Has(EFVTheme::Mountain) ? PinOf(EFVTheme::Mountain) + FVector2D(5.f, 3.f) : FVector2D(Mid.X - 24.f, 0.f);
			const float EndY = FMath::Clamp(Mid.Y + 14.f, 4.f, MapH - 4.f);
			const FVector2D End(CoastX(I, EndY) + 2.f, EndY);
			const FVector2D Pts[3] = { Start, Mid, End };
			const FVector2D Tan[3] = { (Mid - Start) * 0.9f, (End - Start) * 0.55f, (End - Mid) * 0.9f };

			TArray<FVector2D> Samples;
			TArray<float> Widths;
			const float TotalLen = FVector2D::Distance(Start, Mid) + FVector2D::Distance(Mid, End);
			float Acc = 0.f;
			for (int32 Seg = 0; Seg < 2; ++Seg)
			{
				const FVector2D P0 = Pts[Seg], P1 = Pts[Seg + 1];
				const FVector2D M0 = Tan[Seg], M1 = Tan[Seg + 1];
				const float SegLen = FMath::Max(1.f, FVector2D::Distance(P0, P1));
				const int32 N = FMath::Max(8, FMath::RoundToInt(SegLen * 3.f));
				for (int32 i = 0; i <= N; ++i)
				{
					const float U = float(i) / float(N);
					const float U2 = U * U, U3 = U2 * U;
					FVector2D P = P0 * (2 * U3 - 3 * U2 + 1) + M0 * (U3 - 2 * U2 + U) + P1 * (-2 * U3 + 3 * U2) + M1 * (U3 - U2);
					const FVector2D Dir = (P1 - P0).GetSafeNormal();
					const FVector2D Perp(-Dir.Y, Dir.X);
					const float S = Acc + U * SegLen;
					P += Perp * FMath::Sin(S * 0.33f) * 1.4f;
					Samples.Add(P);
					Widths.Add(FMath::Lerp(1.1f, 2.3f, FMath::Clamp(S / FMath::Max(1.f, TotalLen), 0.f, 1.f)));
				}
				Acc += SegLen;
			}
			// banks, then water, then sparkles
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				const FVector2D P = Samples[i];
				const float W = Widths[i] + 1.f;
				for (int32 Y = FMath::FloorToInt(P.Y - W); Y <= FMath::CeilToInt(P.Y + W); ++Y)
				{
					for (int32 X = FMath::FloorToInt(P.X - W); X <= FMath::CeilToInt(P.X + W); ++X)
					{
						const uint8 Ty = TAt(X, Y);
						if (Ty == Terr::Sea || Ty == Terr::Lake || Ty == Terr::River) { continue; }
						if (FVector2D::DistSquared(FVector2D(X + 0.5f, Y + 0.5f), P) <= W * W) { Img.Set(X, Y, FColor(84, 140, 72)); }
					}
				}
			}
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				const FVector2D P = Samples[i];
				const float W = Widths[i];
				for (int32 Y = FMath::FloorToInt(P.Y - W); Y <= FMath::CeilToInt(P.Y + W); ++Y)
				{
					for (int32 X = FMath::FloorToInt(P.X - W); X <= FMath::CeilToInt(P.X + W); ++X)
					{
						const uint8 Ty = TAt(X, Y);
						if (Ty == Terr::Sea || Ty == Terr::Lake) { continue; }
						if (FVector2D::DistSquared(FVector2D(X + 0.5f, Y + 0.5f), P) <= W * W) { Px(X, Y, FColor(84, 162, 222), Terr::River); }
					}
				}
			}
			for (int32 i = 0; i < Samples.Num(); i += 5)
			{
				const int32 X = FMath::RoundToInt(Samples[i].X), Y = FMath::RoundToInt(Samples[i].Y);
				if (TAt(X, Y) == Terr::River && FVUI::Hash01(i, 3, 71) > 0.4f) { Img.Set(X, Y, FColor(160, 214, 244)); }
			}
		}

		void Roads()
		{
			const TArray<FFVLocationDef>& Locs = FVDB::Locations();
			for (int32 i = 1; i < Locs.Num(); ++i)
			{
				if (IsFarPin(Locs[i])) { continue; }   // update 6: the City / Cave are reached by sea (drawn on the extension)
				// connect each place to the nearest one that comes before it in progression order (a road network, not a zigzag)
				const FVector2D B = PinTexel(Locs[i], I.Dims);
				FVector2D A = PinTexel(Locs[0], I.Dims);
				for (int32 k = 1; k < i; ++k)
				{
					if (IsFarPin(Locs[k])) { continue; }
					const FVector2D Q = PinTexel(Locs[k], I.Dims);
					if (FVector2D::DistSquared(Q, B) < FVector2D::DistSquared(A, B)) { A = Q; }
				}
				const FVector2D Dir = (B - A).GetSafeNormal();
				const FVector2D Perp(-Dir.Y, Dir.X);
				const FVector2D Ctrl = (A + B) * 0.5f + Perp * ((i % 2 == 0) ? 6.f : -6.f);
				const float Len = FVector2D::Distance(A, B);
				const int32 N = FMath::Max(6, FMath::RoundToInt(Len * 1.5f));
				for (int32 k = 0; k <= N; ++k)
				{
					const float U = float(k) / float(N);
					const FVector2D P = A * (1 - U) * (1 - U) + Ctrl * 2.f * U * (1 - U) + B * U * U;
					if (FVector2D::Distance(P, A) < 7.f || FVector2D::Distance(P, B) < 7.f) { continue; }
					const int32 X = FMath::RoundToInt(P.X), Y = FMath::RoundToInt(P.Y);
					const uint8 Ty = TAt(X, Y);
					if (Ty == Terr::Sea || Ty == Terr::Lake || Ty == Terr::Village) { continue; }
					if (Ty == Terr::River)
					{
						Img.Set(X, Y, FColor(160, 110, 64));
						Img.Set(X, Y + 1, FColor(110, 70, 40));
						continue;
					}
					if (k % 3 == 0)
					{
						Img.Set(X, Y, FColor(236, 214, 160));
						FVUI::BlendPx(Img, X, Y + 1, FColor(90, 60, 30, 70));
						if (Ty == Terr::Grass) { SetT(X, Y, Terr::Road); }
					}
				}
			}
		}

		void Tree(int32 X, int32 Y, bool bPine)
		{
			if (bPine)
			{
				const FColor D(36, 90, 66), M(50, 116, 80), L(84, 150, 96);
				Img.Set(X, Y - 3, M);
				Img.Set(X - 1, Y - 2, L); Img.Set(X, Y - 2, M); Img.Set(X + 1, Y - 2, D);
				Img.Set(X - 1, Y - 1, L); Img.Set(X, Y - 1, M); Img.Set(X + 1, Y - 1, D);
				Img.Set(X - 2, Y, M); Img.Set(X - 1, Y, M); Img.Set(X, Y, M); Img.Set(X + 1, Y, D); Img.Set(X + 2, Y, D);
				Img.Set(X, Y + 1, FColor(100, 70, 42));
				FVUI::BlendPx(Img, X + 1, Y + 1, FColor(30, 60, 30, 90));
			}
			else
			{
				const FColor D(34, 88, 48), M(48, 114, 60), L(88, 156, 78);
				for (int32 i = -1; i <= 1; ++i) { Img.Set(X + i, Y - 2, M); }
				for (int32 i = -2; i <= 2; ++i) { Img.Set(X + i, Y - 1, M); Img.Set(X + i, Y, M); }
				for (int32 i = -1; i <= 1; ++i) { Img.Set(X + i, Y + 1, D); }
				Img.Set(X - 1, Y - 2, L); Img.Set(X - 2, Y - 1, L); Img.Set(X - 1, Y - 1, L);
				Img.Set(X + 2, Y, D); Img.Set(X + 2, Y - 1, D); Img.Set(X + 1, Y + 1, D);
				Img.Set(X, Y + 2, FColor(104, 72, 42));
				FVUI::BlendPx(Img, X + 1, Y + 2, FColor(30, 60, 30, 90));
				FVUI::BlendPx(Img, X + 2, Y + 2, FColor(30, 60, 30, 70));
			}
			for (int32 j = -3; j <= 2; ++j) { for (int32 i = -2; i <= 2; ++i) { if (TAt(X + i, Y + j) == Terr::Grass) { SetT(X + i, Y + j, Terr::Forest); } } }
		}

		bool TreeFits(int32 X, int32 Y) const
		{
			for (int32 j = -3; j <= 3; ++j)
			{
				for (int32 i = -2; i <= 2; ++i)
				{
					if (!In(X + i, Y + j) || TAt(X + i, Y + j) != Terr::Grass) { return false; }
				}
			}
			return MinPinDist(FVector2D(X, Y)) > 9.f;
		}

		void Forest()
		{
			struct FTree { int32 X, Y; bool bPine; };
			TArray<FTree> Trees;
			const bool bMountain = Has(EFVTheme::Mountain);
			const FVector2D MP = bMountain ? PinOf(EFVTheme::Mountain) : FVector2D(-1000.f, -1000.f);
			auto TryAdd = [&](int32 X, int32 Y, int32 Pine = -1)
			{
				if (!TreeFits(X, Y)) { return; }
				for (const FTree& Tr : Trees) { if (FMath::Abs(Tr.X - X) < 4 && FMath::Abs(Tr.Y - Y) < 3) { return; } }
				Trees.Add({ X, Y, Pine >= 0 ? Pine > 0 : (FVector2D::Distance(FVector2D(X, Y), MP) < 34.f || Y < 22) });
			};
			// update 6: the dense forest of «Лес» (mixed pines and leafy trees, a clearing at the pin) — first, so the
			// scattered classic trees fill around it
			{
				const FVector2D FP = PinTexel(FVDBU6::ForestLocation(), Dims);
				const float RX = 26.f * FeatureK, RY = 21.f;
				int32 Row = 0;
				for (int32 Y = FMath::RoundToInt(FP.Y - RY); Y <= FMath::RoundToInt(FP.Y + RY); Y += 3, ++Row)
				{
					for (int32 X = FMath::RoundToInt(FP.X - RX) + (Row % 2) * 2; X <= FMath::RoundToInt(FP.X + RX); X += 5)
					{
						// rows exactly 3 apart and trees ≥ 4 apart in a row: TryAdd's spacing never drops a grid tree
						const int32 JX = X + Rng2.RandRange(0, 1), JY = Y;
						const float E = FMath::Square((JX - FP.X) / RX) + FMath::Square((JY - FP.Y) / RY);
						const bool bPine = Rng2.FRand() < 0.55f;
						if (E > 0.7f + 0.4f * FVUI::ValueNoise(JX / 6.f, JY / 6.f, 91)) { continue; }
						TryAdd(JX, JY, bPine ? 1 : 0);
					}
				}
			}
			// update 6: the far coast (the City's outskirts, pines on the Cave hill)
			{
				bool bCave = false;
				const FVector2D CP = PinById(FVDBU6::CaveId(), bCave);
				for (int32 k = 0, NT = Scaled(500) * ExtW / FMath::Max(1, ClassicW); k < NT; ++k)
				{
					const int32 X = Rng2.RandRange(ClassicW + 3, MapW - 4);
					const int32 Y = Rng2.RandRange(4, MapH - 4);
					const bool bLucky = Rng2.FRand() < 0.05f;
					if (FVUI::ValueNoise(X / 13.f, Y / 13.f, 32) > 0.55f || bLucky)
					{
						TryAdd(X, Y, (Y < 24 || (bCave && FVector2D::Distance(FVector2D(X, Y), CP) < 28.f)) ? 1 : 0);
					}
				}
			}
			if (Has(EFVTheme::River))
			{
				const FVector2D C = PinOf(EFVTheme::River);
				for (int32 k = 0; k < 160; ++k)
				{
					const float A = Rng.FRandRange(0.f, 2.f * PI);
					const float Rad = Rng.FRandRange(8.f, 32.f);
					TryAdd(FMath::RoundToInt(C.X + FMath::Cos(A) * Rad), FMath::RoundToInt(C.Y + FMath::Sin(A) * Rad * 0.75f));
				}
			}
			for (int32 k = 0, NT = Scaled(500); k < NT; ++k)
			{
				const int32 X = Rng.RandRange(3, ClassicW - 4);
				const int32 Y = Rng.RandRange(4, MapH - 4);
				if (FVUI::ValueNoise(X / 13.f, Y / 13.f, 31) > 0.6f || Rng.FRand() < 0.04f) { TryAdd(X, Y); }
			}
			Trees.Sort([](const FTree& A, const FTree& B) { return A.Y < B.Y; });
			for (const FTree& Tr : Trees) { Tree(Tr.X, Tr.Y, Tr.bPine); }
		}

		void Compass()
		{
			const FVector2D Cands[4] = { FVector2D(ClassicW - 13, MapH - 13), FVector2D(12, MapH - 13), FVector2D(ClassicW - 13, 12), FVector2D(12, 12) };
			FVector2D C = Cands[0];
			for (const FVector2D& Cand : Cands)
			{
				bool bClear = MinPinDist(Cand) > 16.f;
				for (int32 j = -7; j <= 7 && bClear; ++j)
				{
					for (int32 i = -7; i <= 7; ++i)
					{
						const uint8 Ty = TAt(FMath::RoundToInt(Cand.X) + i, FMath::RoundToInt(Cand.Y) + j);
						if (Ty != Terr::Sea && Ty != Terr::Grass) { bClear = false; break; }
					}
				}
				if (bClear) { C = Cand; break; }
			}
			const int32 CX = FMath::RoundToInt(C.X), CY = FMath::RoundToInt(C.Y);
			const FColor Ol(88, 60, 40), Pa(246, 232, 196), Pd(214, 190, 150), Red(206, 60, 50), RedD(150, 40, 36);
			FVUI::DiskPx(Img, CX + 0.5f, CY + 0.5f, 4.5f, FColor(88, 60, 40, 90));
			FVUI::DiskPx(Img, CX + 0.5f, CY + 0.5f, 3.5f, FColor(246, 232, 196, 150));
			for (int32 i = 1; i <= 6; ++i)
			{
				const int32 W = i < 3 ? 1 : 0;
				for (int32 w = -W; w <= W; ++w)
				{
					Img.Set(CX + w, CY - i, w <= 0 ? Red : RedD);            // north
					Img.Set(CX + w, CY + i, w <= 0 ? Pa : Pd);               // south
					Img.Set(CX + i, CY + w, w <= 0 ? Pa : Pd);               // east
					Img.Set(CX - i, CY + w, w <= 0 ? Pa : Pd);               // west
				}
			}
			Img.Set(CX, CY - 7, Ol); Img.Set(CX, CY + 7, Ol); Img.Set(CX + 7, CY, Ol); Img.Set(CX - 7, CY, Ol);
			Img.Set(CX, CY, Ol);
			for (int32 i = 1; i <= 2; ++i)
			{
				Img.Set(CX + i, CY - i, Pd); Img.Set(CX - i, CY - i, Pd); Img.Set(CX + i, CY + i, Pd); Img.Set(CX - i, CY + i, Pd);
			}
		}

		void Finish()
		{
			const FColor Paper(238, 220, 170);
			for (int32 Y = 0; Y < MapH; ++Y)
			{
				for (int32 X = 0; X < MapW; ++X)
				{
					FColor C = FVUI::LerpColor(Img.Get(X, Y), Paper, 0.1f);
					// (update 6: the frame goes around the whole image, the extension included)
					const int32 E = FMath::Min(FMath::Min(FMath::Min(X, Y), MapH - 1 - Y), MapW - 1 - X);
					if (E < 5)
					{
						C = FVUI::LerpColor(C, FColor(150, 110, 70), (5 - E) * 0.06f);
					}
					if (E == 0) { C = FColor(96, 62, 36); }
					C.A = 255;
					Img.Set(X, Y, C);
				}
			}
		}

		// ---- update 6: the new places drawn in the classic style (same palette, outlines, dotted roads, House-like
		// buildings, Peak mountains, Tree forests) — the forest and the Old mill on the left, the far coast on the right

		static FColor Darker(FColor C, int32 R, int32 G, int32 B) { return FColor(uint8(FMath::Max(0, C.R - R)), uint8(FMath::Max(0, C.G - G)), uint8(FMath::Max(0, C.B - B))); }

		bool IsLandFree(int32 X, int32 Y) const { const uint8 Ty = TAt(X, Y); return In(X, Y) && (Ty == Terr::Grass || Ty == Terr::Road); }

		/** A House-style building of any width / height (roof 3 rows with the eave, walls with windows, a door, a shadow). */
		bool Building(int32 X, int32 Y, int32 Wd, int32 WallH, FColor Roof, FColor Wall = FColor(238, 224, 188), bool bCheck = true)
		{
			if (bCheck)
			{
				for (int32 j = 0; j < 3 + WallH + 1; ++j) { for (int32 i = -1; i <= Wd; ++i) { if (!IsLandFree(X + i, Y + j)) { return false; } } }
			}
			const FColor RoofDark = Darker(Roof, 50, 30, 20);
			const FColor WallDark = Darker(Wall, 32, 36, 38);
			for (int32 i = 1; i <= Wd - 2; ++i) { Px(X + i, Y, Roof, Terr::Village); }
			for (int32 i = 0; i <= Wd - 1; ++i) { Px(X + i, Y + 1, Roof, Terr::Village); }
			for (int32 i = -1; i <= Wd; ++i) { Px(X + i, Y + 2, RoofDark, Terr::Village); }
			for (int32 j = 3; j < 3 + WallH; ++j)
			{
				for (int32 i = 0; i <= Wd - 1; ++i) { Px(X + i, Y + j, i == Wd - 1 ? WallDark : Wall, Terr::Village); }
			}
			Img.Set(X + 1, Y, FVUI::LerpColor(Roof, FColor(255, 255, 255), 0.3f));
			// windows (one per 2 columns on every other wall row) and the door on the ground floor
			for (int32 j = 4; j < 3 + WallH; j += 2)
			{
				for (int32 i = 1; i < Wd - 1; i += 2) { Img.Set(X + i, Y + j, FColor(110, 160, 206)); }
			}
			const int32 DoorX = X + Wd / 2 - (Wd % 2 == 0 ? 1 : 0);
			Img.Set(DoorX, Y + 2 + WallH, FColor(120, 76, 44));
			if (WallH >= 3) { Img.Set(DoorX, Y + 1 + WallH, FColor(120, 76, 44)); }
			for (int32 i = 0; i <= Wd; ++i) { FVUI::BlendPx(Img, X + i, Y + 3 + WallH, FColor(40, 70, 30, 90)); }
			return true;
		}

		/** A dotted road like the classic ones (dots every 3rd texel, shadow under them, planks over rivers). */
		void Trail(const FVector2D& A, const FVector2D& B, float Bend, float SkipA = 7.f, float SkipB = 7.f)
		{
			const FVector2D Dir = (B - A).GetSafeNormal();
			const FVector2D Perp(-Dir.Y, Dir.X);
			const FVector2D Ctrl = (A + B) * 0.5f + Perp * Bend;
			const float Len = FVector2D::Distance(A, B);
			const int32 N = FMath::Max(6, FMath::RoundToInt(Len * 1.5f));
			int32 LastBridge = -100;
			for (int32 k = 0; k <= N; ++k)
			{
				const float U = float(k) / float(N);
				const FVector2D P = A * (1 - U) * (1 - U) + Ctrl * 2.f * U * (1 - U) + B * U * U;
				if (FVector2D::Distance(P, A) < SkipA || FVector2D::Distance(P, B) < SkipB) { continue; }
				const int32 X = FMath::RoundToInt(P.X), Y = FMath::RoundToInt(P.Y);
				const uint8 Ty = TAt(X, Y);
				if (Ty == Terr::Sea || Ty == Terr::Lake || Ty == Terr::Village || Ty == Terr::Mountain) { continue; }
				if (Ty == Terr::River)
				{
					// a little footbridge: planks across the stream with a rail on both sides
					if (k - LastBridge > 3)
					{
						for (int32 d = -2; d <= 2; ++d)
						{
							Img.Set(X + d, Y, FColor(170, 116, 66));
							Img.Set(X + d, Y + 1, FColor(112, 72, 42));
							if (d == -2 || d == 2) { Img.Set(X + d, Y - 1, FColor(90, 58, 36)); }
						}
						LastBridge = k;
					}
					continue;
				}
				if (k % 3 == 0)
				{
					Img.Set(X, Y, FColor(236, 214, 160));
					FVUI::BlendPx(Img, X, Y + 1, FColor(90, 60, 30, 70));
					if (Ty == Terr::Grass) { SetT(X, Y, Terr::Road); }
				}
			}
		}

		/** A narrow stream (the River pixels & banks of the classic river) through the points (Catmull-Rom). */
		void Stream(const TArray<FVector2D>& Pts, float W0, float W1)
		{
			if (Pts.Num() < 2) { return; }
			TArray<FVector2D> S;
			TArray<float> Wd;
			const int32 NSeg = Pts.Num() - 1;
			for (int32 Seg = 0; Seg < NSeg; ++Seg)
			{
				const FVector2D P0 = Pts[FMath::Max(0, Seg - 1)], P1 = Pts[Seg], P2 = Pts[Seg + 1], P3 = Pts[FMath::Min(NSeg, Seg + 2)];
				const int32 N = FMath::Max(6, FMath::RoundToInt(FVector2D::Distance(P1, P2) * 3.f));
				for (int32 i = 0; i < N; ++i)
				{
					const float U = float(i) / float(N), U2 = U * U, U3 = U2 * U;
					const FVector2D P = ((P1 * 2.f) + (P2 - P0) * U + (P0 * 2.f - P1 * 5.f + P2 * 4.f - P3) * U2 + (P1 * 3.f - P0 - P2 * 3.f + P3) * U3) * 0.5f;
					S.Add(P);
					Wd.Add(FMath::Lerp(W0, W1, (float(Seg) + U) / float(NSeg)));
				}
			}
			S.Add(Pts.Last());
			Wd.Add(W1);
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (int32 i = 0; i < S.Num(); ++i)
				{
					const float W = Wd[i] + (Pass == 0 ? 1.f : 0.f);
					for (int32 Y = FMath::FloorToInt(S[i].Y - W); Y <= FMath::CeilToInt(S[i].Y + W); ++Y)
					{
						for (int32 X = FMath::FloorToInt(S[i].X - W); X <= FMath::CeilToInt(S[i].X + W); ++X)
						{
							const uint8 Ty = TAt(X, Y);
							if (Ty == Terr::Sea || Ty == Terr::Lake || Ty == Terr::Mountain || (Pass == 0 && Ty == Terr::River)) { continue; }
							if (FVector2D::DistSquared(FVector2D(X + 0.5f, Y + 0.5f), S[i]) > W * W) { continue; }
							if (Pass == 0) { Img.Set(X, Y, FColor(84, 140, 72)); }
							else { Px(X, Y, FColor(84, 162, 222), Terr::River); }
						}
					}
				}
			}
			for (int32 i = 0; i < S.Num(); i += 5)
			{
				const int32 X = FMath::RoundToInt(S[i].X), Y = FMath::RoundToInt(S[i].Y);
				if (TAt(X, Y) == Terr::River && FVUI::Hash01(i, 5, 72) > 0.45f) { Img.Set(X, Y, FColor(160, 214, 244)); }
			}
		}

		/** Update 6: «Лес» (a darker forest floor, the forester's cabin, trails) and «Старая мельница» (the millpond fed
		    by a forest stream with a footbridge, the timber mill with its water wheel, the beaver dam). The trees
		    themselves come in Forest(). */
		void ForestAndMill()
		{
			const FVector2D FP = PinTexel(FVDBU6::ForestLocation(), Dims);
			const bool bMill = Has(EFVTheme::Mill);
			const FVector2D MP = bMill ? PinOf(EFVTheme::Mill) : FVector2D::ZeroVector;
			// forest floor: a darker, mossy green under the trees (only over the grass)
			const float RX = 29.f * FeatureK, RY = 24.f;
			for (int32 Y = FMath::FloorToInt(FP.Y - RY); Y <= FMath::CeilToInt(FP.Y + RY); ++Y)
			{
				for (int32 X = FMath::FloorToInt(FP.X - RX); X <= FMath::CeilToInt(FP.X + RX); ++X)
				{
					if (!In(X, Y) || TAt(X, Y) != Terr::Grass) { continue; }
					const float E = FMath::Square((X - FP.X) / RX) + FMath::Square((Y - FP.Y) / RY);
					const float Lim = 0.75f + 0.35f * FVUI::ValueNoise(X / 6.f, Y / 6.f, 92);
					if (E > Lim) { continue; }
					const float K = FMath::Clamp((Lim - E) / Lim * 2.f, 0.f, 1.f) * 0.42f;
					const FColor Moss = FVUI::Hash01(X, Y, 93) > 0.93f ? FColor(70, 118, 56) : FColor(86, 140, 64);
					Img.Set(X, Y, FVUI::LerpColor(Img.Get(X, Y), Moss, K));
				}
			}
			if (bMill)
			{
				// the forest stream: from a spring north of the forest, past the pin (west), into the millpond; then out
				// over the beaver dam down towards the swamp
				const FVector2D Spring(FP.X - 2.f, FMath::Max(20.f, FP.Y - 24.f));
				TArray<FVector2D> In1 = { Spring, FVector2D(FP.X - 4.f, FP.Y - 12.f), FVector2D(FP.X - 7.f, FP.Y - 3.f),
					FVector2D(MP.X + 8.f, MP.Y - 6.f), FVector2D(MP.X + 5.f, MP.Y - 3.f) };
				Stream(In1, 0.6f, 1.1f);
				const float OutY = FMath::Min(MP.Y + 26.f, float(MapH) - 3.f);
				TArray<FVector2D> Out1 = { FVector2D(MP.X + 3.f, MP.Y + 4.f), FVector2D(MP.X + 4.f, MP.Y + 11.f), FVector2D(MP.X + 9.f, MP.Y + 18.f), FVector2D(MP.X + 14.f, OutY) };
				Stream(Out1, 1.1f, 1.4f);
				// the spring: a few wet stones
				Img.Set(FMath::RoundToInt(Spring.X) - 1, FMath::RoundToInt(Spring.Y) - 1, FColor(150, 146, 140));
				Img.Set(FMath::RoundToInt(Spring.X) + 1, FMath::RoundToInt(Spring.Y) - 1, FColor(126, 116, 112));
				// the millpond at the pin (like the classic pond: a grassy rim, water, a highlight)
				// (the pond lies a little east of the pin: the pin sits at the very left edge of the map)
				const float PX = MP.X + 2.f;
				FVUI::EllipsePx(Img, PX, MP.Y + 0.5f, 8.f, 5.5f, FColor(84, 150, 84));
				FVUI::EllipsePx(Img, PX, MP.Y, 7.f, 4.6f, FColor(88, 166, 206));
				FVUI::EllipsePx(Img, PX - 1.f, MP.Y - 1.f, 4.f, 2.f, FColor(130, 198, 226));
				const int32 CX = FMath::RoundToInt(PX), CY = FMath::RoundToInt(MP.Y);
				for (int32 Y = -5; Y <= 5; ++Y) { for (int32 X = -8; X <= 8; ++X) { const float E = (X / 7.5f) * (X / 7.5f) + (Y / 5.f) * (Y / 5.f); if (E <= 1.f) { SetT(CX + X, CY + Y, Terr::Lake); } } }
				// the beaver dam (logs) at the outlet, a lodge in the water
				for (int32 i = -3; i <= 4; ++i)
				{
					Px(CX + i, CY + 5, (i & 1) ? FColor(132, 92, 56) : FColor(150, 106, 64), Terr::Village);
					Px(CX + i, CY + 6, FColor(96, 64, 40), Terr::Village);
				}
				Img.Set(CX - 2, CY + 4, FColor(170, 124, 76)); Img.Set(CX + 2, CY + 4, FColor(170, 124, 76));
				FVUI::DiskPx(Img, CX - 4.5f, CY + 2.5f, 1.8f, FColor(120, 84, 52));
				Img.Set(CX - 5, CY + 1, FColor(150, 110, 70));
				// reeds on the shore
				for (int32 k = 0; k < 14; ++k)
				{
					const float A = Rng2.FRandRange(0.f, 2.f * PI);
					const int32 X = FMath::RoundToInt(MP.X + FMath::Cos(A) * 8.5f), Y = FMath::RoundToInt(MP.Y + FMath::Sin(A) * 5.8f);
					if (TAt(X, Y) != Terr::Grass || TAt(X, Y - 2) != Terr::Grass) { continue; }
					Img.Set(X, Y, FColor(54, 86, 44)); Img.Set(X, Y - 1, FColor(54, 86, 44)); Img.Set(X, Y - 2, FColor(128, 92, 52));
				}
				// the timber mill on the north shore with its water wheel in the pond
				const int32 MX = CX - 3, MY = CY - 13;
				Building(MX, MY, 7, 4, FColor(150, 84, 54), FColor(196, 150, 104), false);
				// stone footing
				for (int32 i = 0; i < 7; ++i) { Px(MX + i, MY + 7, (i & 1) ? FColor(150, 146, 140) : FColor(126, 116, 112), Terr::Village); }
				// the wheel: a spoked ring on the pond side
				const FVector2D WC(float(MX) + 7.5f, float(MY) + 7.5f);
				for (int32 s = 0; s < 12; ++s)
				{
					const float A = s * PI / 6.f;
					Img.Set(FMath::RoundToInt(WC.X + FMath::Cos(A) * 2.6f), FMath::RoundToInt(WC.Y + FMath::Sin(A) * 2.6f), FColor(110, 72, 42));
				}
				for (int32 d = -1; d <= 1; ++d)
				{
					Img.Set(FMath::RoundToInt(WC.X) + d, FMath::RoundToInt(WC.Y), FColor(150, 106, 64));
					Img.Set(FMath::RoundToInt(WC.X), FMath::RoundToInt(WC.Y) + d, FColor(150, 106, 64));
				}
				Img.Set(FMath::RoundToInt(WC.X), FMath::RoundToInt(WC.Y), FColor(70, 44, 26));
				// the trail from the forest to the mill (a footbridge over the stream)
				Trail(FP, MP, -4.f, 7.f, 9.f);
			}
			// the forester's cabin (a log hut with a dark roof) just north-east of the forest pin
			{
				const int32 HX = FMath::RoundToInt(FP.X) + 4, HY = FMath::RoundToInt(FP.Y) - 14;
				if (Building(HX, HY, 6, 3, FColor(112, 78, 52), FColor(176, 128, 84)))
				{
					for (int32 j = 3; j < 6; ++j) { Img.Set(HX, HY + j, FColor(140, 96, 60)); }   // log corners
					Img.Set(HX + 4, HY - 1, FColor(120, 110, 104));                               // chimney
					Img.Set(HX + 4, HY - 2, FColor(96, 88, 84));
				}
			}
			// the trail from the Forest river to the forest (§1: «тропа от реки»)
			if (Has(EFVTheme::River)) { Trail(PinOf(EFVTheme::River), FP, 5.f); }
		}

		/** Update 6: the far coast — mountains along the top, the Cave hill with its mouth, the port City (houses,
		    streets, the town hall with a clock tower, the harbour), fields and the lighthouse on a cape. */
		void FarCoast()
		{
			// the mountain range continues along the top edge
			{
				struct FPk { int32 X, Y, H; };
				TArray<FPk> Pk;
				for (int32 X = FMath::RoundToInt(FarCoastX(Dims, 12.f)) + 11; X < MapW - 8; X += 9)
				{
					const int32 PX = X + Rng2.RandRange(-2, 2), PY = Rng2.RandRange(11, 16), PH = Rng2.RandRange(8, 13);
					if (MinPinDist(FVector2D(PX, PY - 4)) < 12.f) { continue; }
					Pk.Add({ PX, PY, PH });
				}
				Pk.Sort([](const FPk& A, const FPk& B) { return A.Y < B.Y; });
				for (const FPk& P : Pk) { Peak(P.X, P.Y, P.H); }
			}
			// the Cave hill
			bool bCave = false;
			const FVector2D CV = PinById(FVDBU6::CaveId(), bCave);
			if (bCave)
			{
				const float SR = 17.f * FeatureK;
				for (int32 Y = FMath::FloorToInt(CV.Y - SR); Y <= FMath::CeilToInt(CV.Y + SR); ++Y)
				{
					for (int32 X = FMath::FloorToInt(CV.X - SR); X <= FMath::CeilToInt(CV.X + SR); ++X)
					{
						if (!In(X, Y) || TAt(X, Y) != Terr::Grass) { continue; }
						const float D = FVector2D::Distance(FVector2D(X, Y), CV) / SR * (0.85f + 0.3f * FVUI::ValueNoise(X / 5.f, Y / 5.f, 64));
						if (D >= 1.f) { continue; }
						const float K = FMath::Pow(1.f - D, 0.6f) * 0.75f;
						const FColor Rock = FVUI::Hash01(X, Y, 65) > 0.9f ? FColor(126, 116, 112) : FColor(160, 150, 132);
						Img.Set(X, Y, FVUI::LerpColor(Img.Get(X, Y), Rock, K));
						if (D < 0.6f) { SetT(X, Y, Terr::Mountain); }
					}
				}
				struct FPk { int32 X, Y, H; };
				const int32 CX = FMath::RoundToInt(CV.X), CY = FMath::RoundToInt(CV.Y);
				TArray<FPk> Pk = { { CX - 11, CY - 3, 10 }, { CX + 11, CY - 2, 11 }, { CX - 2, CY - 7, 13 }, { CX + 5, CY - 9, 9 } };
				Pk.Sort([](const FPk& A, const FPk& B) { return A.Y < B.Y; });
				for (const FPk& P : Pk) { if (In(P.X, P.Y)) { Peak(P.X, P.Y, P.H); } }
				// the cave mouth at the pin: a dark arch in a rocky face, boulders of the rubble around
				FVUI::EllipsePx(Img, CV.X, CV.Y + 0.5f, 6.f, 4.2f, FColor(88, 76, 74));
				FVUI::EllipsePx(Img, CV.X, CV.Y + 1.f, 4.4f, 3.2f, FColor(40, 32, 34));
				FVUI::EllipsePx(Img, CV.X - 0.5f, CV.Y + 2.f, 3.f, 1.6f, FColor(58, 110, 128));   // the underground lake glints inside
				for (int32 Y = -4; Y <= 4; ++Y) { for (int32 X = -6; X <= 6; ++X) { SetT(CX + X, CY + Y, Terr::Mountain); } }
				for (int32 k = 0; k < 7; ++k)
				{
					const int32 BX = CX + Rng2.RandRange(-9, 9), BY = CY + Rng2.RandRange(3, 7);
					if (FMath::Abs(BX - CX) < 5 && BY - CY < 5) { continue; }
					Img.Set(BX, BY, FColor(150, 146, 140)); Img.Set(BX + 1, BY, FColor(126, 116, 112)); Img.Set(BX, BY - 1, FColor(172, 160, 150));
					FVUI::BlendPx(Img, BX + 1, BY + 1, FColor(40, 70, 40, 80));
				}
			}
			// the City
			bool bCity = false;
			const FVector2D CP = PinById(FVDBU6::CityId(), bCity);
			if (bCity)
			{
				const int32 CX = FMath::RoundToInt(CP.X), CY = FMath::RoundToInt(CP.Y);
				const FColor Cob(196, 186, 166), CobD(174, 164, 146);
				auto Pave = [&](int32 X, int32 Y)
				{
					if (!In(X, Y)) { return; }
					const uint8 Ty = TAt(X, Y);
					if (Ty != Terr::Grass && Ty != Terr::Road) { return; }
					Px(X, Y, FVUI::Hash01(X, Y, 66) > 0.7f ? CobD : Cob, Terr::Road);
				};
				// the harbour: a stone quay along the shore and a wooden pier with boats
				for (int32 Y = CY - 8; Y <= CY + 10; ++Y)
				{
					const int32 QX = FMath::RoundToInt(FarCoastX(Dims, float(Y)));
					for (int32 X = QX; X <= QX + 1; ++X) { if (In(X, Y) && TAt(X, Y) != Terr::Sea) { Px(X, Y, (Y & 1) ? FColor(170, 162, 150) : FColor(150, 142, 132), Terr::Road); } }
				}
				{
					const int32 PY = CY + 3;
					const int32 X1 = FMath::RoundToInt(FarCoastX(Dims, float(PY)));
					const int32 X0 = X1 - 9;
					for (int32 X = X0; X < X1; ++X)
					{
						Img.Set(X, PY, FColor(170, 116, 66));
						Img.Set(X, PY + 1, FColor(112, 72, 42));
						if ((X - X0) % 4 == 0) { Img.Set(X, PY + 2, FColor(90, 58, 36)); }
					}
					for (int32 b = 0; b < 2; ++b)
					{
						const int32 BX = X0 + 1 + b * 4, BY = PY + (b == 0 ? 5 : -3);
						for (int32 i = 0; i < 5; ++i) { Img.Set(BX + i, BY, FColor(150, 90, 50)); }
						for (int32 i = 1; i < 4; ++i) { Img.Set(BX + i, BY + 1, FColor(110, 64, 36)); }
						if (b == 0) { Img.Set(BX + 2, BY - 1, FColor(240, 240, 230)); Img.Set(BX + 2, BY - 2, FColor(240, 240, 230)); Img.Set(BX + 1, BY - 1, FColor(220, 220, 210)); }
					}
				}
				// the market square at the pin and two streets across the town
				for (int32 Y = -4; Y <= 4; ++Y) { for (int32 X = -6; X <= 6; ++X) { if (FMath::Square(X / 6.f) + FMath::Square(Y / 4.f) <= 1.f) { Pave(CX + X, CY + Y); } } }
				for (int32 X = FMath::RoundToInt(FarCoastX(Dims, float(CY + 1))) + 2; X < MapW - 3; ++X) { Pave(X, CY + 1); }
				for (int32 Y = CY - 24; Y <= CY + 24; ++Y) { Pave(CX + 1, Y); }
				Img.Set(CX, CY, FColor(150, 142, 132));   // the fountain
				// the town hall with the clock tower north of the square
				{
					const int32 TX = CX - 4, TY = CY - 14;
					if (Building(TX, TY, 9, 3, FColor(150, 70, 62), FColor(232, 220, 196), false))
					{
						for (int32 j = -6; j < 3; ++j) { for (int32 i = 3; i <= 5; ++i) { Px(TX + i, TY + j, i == 5 ? FColor(200, 186, 160) : FColor(232, 220, 196), Terr::Village); } }
						Px(TX + 4, TY - 9, FColor(70, 110, 180), Terr::Village);
						for (int32 i = 3; i <= 5; ++i) { Px(TX + i, TY - 8, FColor(70, 110, 180), Terr::Village); Px(TX + i, TY - 7, FColor(50, 84, 150), Terr::Village); }
						Img.Set(TX + 4, TY - 4, FColor(255, 214, 90));   // the clock
						Img.Set(TX + 4, TY - 2, FColor(110, 160, 206));
					}
				}
				// houses: blocks packed along the two streets around the square (roofs of the classic village palette + town ones)
				const FColor Roofs[6] = { FColor(196, 76, 56), FColor(70, 110, 180), FColor(170, 110, 60), FColor(196, 76, 56), FColor(60, 138, 128), FColor(150, 70, 62) };
				for (int32 KY = -4; KY <= 3; ++KY)
				{
					for (int32 KX = -5; KX <= 4; ++KX)
					{
						const bool bTall = Rng2.FRand() < 0.3f;
						const FColor Roof = Roofs[Rng2.RandRange(0, 5)];
						const bool bSkip = Rng2.FRand() < 0.1f;
						const int32 WallH = bTall ? 4 : 3;
						// 8x8 slots: above the east-west street (CY + 1) the houses end right over it, below they start under it;
						// west of the north-south street (CX + 1) they end right before it, east of it they start after it
						const int32 HY = KY < 0 ? CY + 1 + 8 * KY + (8 - (3 + WallH + 1)) : CY + 2 + 8 * KY;
						const int32 HX = KX < 0 ? CX - 6 + 8 * (KX + 1) : CX + 3 + 8 * KX;
						const FVector2D HC(HX + 3, HY + 4);
						if (bSkip || FVector2D::Distance(HC, CP) > 25.f + 6.f * FVUI::ValueNoise(HX / 7.f, HY / 7.f, 67)) { continue; }
						if (MinPinDist(HC) < 7.f) { continue; }
						if (HY >= CY && HY <= CY + 9 && FMath::Abs(HC.X - CP.X) < 11.f) { continue; }   // the pin's name label
						if (HX - 1 <= FMath::Max(FarCoastX(Dims, float(HY)), FarCoastX(Dims, float(HY + 7))) + 2.f) { continue; }   // keep the quay
						Building(HX, HY, 6, WallH, Roof, bTall ? FColor(226, 214, 190) : FColor(238, 224, 188));
					}
				}
				// fields and gardens on the outskirts south-east of the town
				auto Field = [&](int32 FX, int32 FY, int32 W, int32 H)
				{
					for (int32 j = 0; j < H; ++j) { for (int32 i = 0; i < W; ++i) { if (TAt(FX + i, FY + j) == Terr::Grass) { Px(FX + i, FY + j, (j % 2 == 0) ? FColor(180, 150, 86) : FColor(124, 170, 66), Terr::Village); } } }
				};
				Field(CX + 8, CY + 27, 11, 6);
				Field(CX - 6, CY + 33, 9, 5);
				// the road to the Cave
				if (bCave) { Trail(CP, CV, 5.f, 9.f, 8.f); }
			}
			// the lighthouse on a rocky cape south of the City
			{
				const FVector2D L = LighthouseTexel(Dims);
				const int32 LX = FMath::RoundToInt(L.X), LY = FMath::RoundToInt(L.Y);
				FVUI::EllipsePx(Img, LX + 0.5f, LY + 9.5f, 4.5f, 2.5f, FColor(222, 198, 140));
				FVUI::EllipsePx(Img, LX + 0.5f, LY + 9.f, 3.5f, 1.8f, FColor(150, 142, 132));
				Img.Set(LX - 2, LY + 9, FColor(126, 116, 112)); Img.Set(LX + 3, LY + 10, FColor(126, 116, 112));
				for (int32 j = 1; j <= 8; ++j)
				{
					const bool bRed = (j == 3 || j == 4 || j == 7);
					Px(LX, LY + j, bRed ? FColor(196, 60, 50) : FColor(246, 244, 236), Terr::Village);
					Px(LX + 1, LY + j, bRed ? FColor(150, 40, 36) : FColor(206, 204, 200), Terr::Village);
					Img.Set(LX - 1, LY + j, FColor(88, 76, 74));
					Img.Set(LX + 2, LY + j, FColor(88, 76, 74));
				}
				Img.Set(LX, LY, FColor(255, 226, 120)); Img.Set(LX + 1, LY, FColor(255, 200, 90));
				Img.Set(LX - 1, LY, FColor(88, 76, 74)); Img.Set(LX + 2, LY, FColor(88, 76, 74));
				for (int32 i = -1; i <= 2; ++i) { Img.Set(LX + i, LY - 1, FColor(196, 60, 50)); }
				Img.Set(LX, LY - 2, FColor(150, 40, 36)); Img.Set(LX + 1, LY - 2, FColor(150, 40, 36));
				// the path from the town to the lighthouse along the shore
				if (bCity) { Trail(CP, FVector2D(LX + 3.f, LY + 9.f), -4.f, 9.f, 3.f); }
			}
		}

		FFVImage Build()
		{
			Grass();
			Sea();
			Swamp();
			Mountains();
			Volcano();
			River();
			ForestAndMill();   // update 6
			Village();
			Homestead();
			FarCoast();        // update 6
			Roads();
			Forest();
			Compass();
			Finish();
			return Img;
		}
	};

	static const FFVSprite* GetMapSprite(const FMapDims& M)
	{
		UFVSprites* Sp = UFVSprites::Get();
		if (!Sp || FVDB::Locations().Num() == 0 || M.W <= 0 || M.H <= 0) { return nullptr; }
		uint32 H = 0x5EA;
		for (const FFVLocationDef& L : FVDB::Locations())
		{
			H = HashCombine(H, GetTypeHash(L.Id));
			H = HashCombine(H, GetTypeHash(PinTexel(L, M)));
			H = HashCombine(H, uint32(L.Theme));
		}
		H = HashCombine(H, GetTypeHash(PinTexel(FVDB::HomeLocation(), M)));
		H = HashCombine(H, GetTypeHash(PinTexel(FVDBU6::ForestLocation(), M)));
		H = HashCombine(H, uint32(M.W) * 1024u + uint32(M.H));
		const uint64 Key = UFVSprites::MakeKey(FVSpriteCat::Misc, 0xA11A9, H & 0xFFFFFF, 0x4E);   // 0x4E: update 6 extension in the image
		if (const FFVSprite* S = Sp->FindCached(Key)) { return S; }
		FMapBuilder B(M);
		const FFVImage Img = B.Build();
		return &Sp->AddCached(Key, Img, FString::Printf(TEXT("FV_WorldMap_%dx%d"), M.W, M.H));
	}

	// ----------------------------------------------------------------------------------------
	// Screen
	// ----------------------------------------------------------------------------------------

	/** What the main button does for a place (shared by the landscape side panel and the portrait action bar). */
	enum class EMapAction : uint8 { None, Here, Travel, Unlock, NeedLevel };

	struct FMapAction
	{
		EMapAction Kind = EMapAction::None;
		FString Label;
		bool bEnabled = false;
	};

	static FMapAction ActionFor(const UFVSaveGame* P, const FFVLocationDef& L)
	{
		FMapAction A;
		if (!P)
		{
			A.Label = TEXT("Отправиться");
			return A;
		}
		if (FVDB::IsHomeId(L.Id))
		{
			// «Дом» is always open
			if (IsCurrentPlace(P, L.Id))
			{
				A.Kind = EMapAction::Here;
				A.Label = TEXT("Вы дома");
			}
			else
			{
				A.Kind = EMapAction::Travel;
				A.Label = TEXT("Домой");
				A.bEnabled = true;
			}
			return A;
		}
		// update 6: the forest (free, level 4), the City (the boat), the Cave (the rubble): conditions, no coin price
		if (IsU6Place(L.Id))
		{
			if (P->CurrentLocation == L.Id)
			{
				A.Kind = EMapAction::Here;
				A.Label = TEXT("Вы здесь");
			}
			else if (P->GetLevel() < L.MinLevel)
			{
				A.Kind = EMapAction::NeedLevel;
				A.Label = FString::Printf(TEXT("Нужен уровень %d"), L.MinLevel);
			}
			else if (L.Id == FVDBU6::CityId() && !P->IsBoatRepaired())
			{
				A.Kind = EMapAction::NeedLevel;
				A.Label = TEXT("Нужна лодка");
			}
			else if (L.Id == FVDBU6::CaveId() && !P->IsCaveOpen())
			{
				A.Kind = EMapAction::NeedLevel;
				A.Label = TEXT("Вход завален");
			}
			else if (FVMill::IsMillId(L.Id) && !P->IsLocationUnlocked(L.Id) && !P->CanOpenMill())
			{
				A.Kind = EMapAction::NeedLevel;
				A.Label = TEXT("Почините мостик");
			}
			else
			{
				A.Kind = EMapAction::Travel;
				A.Label = L.Id == FVDBU6::CityId() ? FString(TEXT("Отплыть")) : FString(TEXT("Отправиться"));
				A.bEnabled = true;
			}
			return A;
		}
		if (P->IsLocationUnlocked(L.Id))
		{
			if (P->CurrentLocation == L.Id)
			{
				A.Kind = EMapAction::Here;
				A.Label = TEXT("Вы здесь");
			}
			else
			{
				A.Kind = EMapAction::Travel;
				A.Label = TEXT("Отправиться");
				A.bEnabled = true;
			}
		}
		else if (P->GetLevel() < L.MinLevel)
		{
			A.Kind = EMapAction::NeedLevel;
			A.Label = FString::Printf(TEXT("Нужен уровень %d"), L.MinLevel);
		}
		else
		{
			A.Kind = EMapAction::Unlock;
			A.Label = L.UnlockPrice > 0 ? FString::Printf(TEXT("Открыть за %s"), *FV::Money(L.UnlockPrice)) : FString(TEXT("Открыть"));
			A.bEnabled = true;
		}
		return A;
	}

	/** Update 6: the condition line of a closed forest / City / Cave (instead of the coin price). */
	static FString U6ConditionText(const UFVSaveGame* P, const FFVLocationDef& L, bool& bOk)
	{
		bOk = false;
		if (!P) { return FString(); }
		if (FVDBU6::IsForestId(L.Id)) { bOk = true; return TEXT("Бесплатно — тропа от реки"); }
		if (L.Id == FVDBU6::CityId())
		{
			bOk = P->IsBoatRepaired();
			const int32 N = FVDBU6::BoatStages().Num();
			return bOk ? FString(TEXT("Лодка готова")) : FString::Printf(TEXT("Нужна лодка: ремонт %d/%d"), FMath::Clamp(P->U6.BoatStage, 0, N), N);
		}
		if (L.Id == FVDBU6::CaveId())
		{
			bOk = P->IsCaveOpen();
			return bOk ? FString(TEXT("Вход разобран")) : FString(TEXT("Разобрать завал киркой (в Городе)"));
		}
		if (FVMill::IsMillId(L.Id))
		{
			bOk = P->IsBridgeRepaired();
			return bOk ? FString(TEXT("Мостик через ручей починен")) : FString(TEXT("Почините мостик через ручей (в лесу, у домика Лесника)"));
		}
		return FString();
	}

	/** The place is open for the player as the map shows it: unlocked, or (update 6 places) its conditions are met —
	    the pin, the tooltip and the details panel all agree («Открыто» + «Отправиться»). */
	static bool IsOpenOnMap(const UFVSaveGame* P, const FFVLocationDef& L)
	{
		if (FVDB::IsHomeId(L.Id)) { return true; }
		if (!P) { return false; }
		if (P->IsLocationUnlocked(L.Id)) { return true; }
		if (!IsU6Place(L.Id)) { return false; }
		bool bCondOk = false;
		U6ConditionText(P, L, bCondOk);
		return bCondOk && P->GetLevel() >= L.MinLevel;
	}

	/** Update 6: the details of a place without fish (the forest). */
	static const TCHAR* NoFishText()
	{
		return TEXT("Рыбы здесь нет: в лесу собирают ветки и камни, а деревья с зарубкой рубят топором.");
	}

	// ---- update 5: a legend's live preparation checklist (the equipped rod with its tackle + the current bait)

	/** Checklist label with what happens without it: «не клюёт» (hard rule) / «обрыв» (the leader snaps). */
	FString CheckLabel(const FFVLegendCheckItem& It)
	{
		return It.bOk ? It.Label : It.Label + (It.bHard ? TEXT(" — не клюёт") : TEXT(" — обрыв"));
	}

	/** Icon rows: Check (green) / Close (red) + the label. Stops before MaxY; returns the height used. */
	float DrawLegendChecklist(FVDraw& D, const TArray<FFVLegendCheckItem>& Items, float X, float Y, float W, float Size, float MaxY)
	{
		const float RowH = FMath::RoundToFloat(D.LineHeight(Size, true) + 1.f);
		const float IconS = FMath::Clamp(FMath::FloorToFloat(RowH - 1.f), 8.f, 16.f);
		float H = 0.f;
		for (const FFVLegendCheckItem& It : Items)
		{
			if (Y + H + RowH > MaxY) { break; }
			if (const FFVSprite* S = FVUI::Icon(It.bOk ? EFVIcon::Check : EFVIcon::Close))
			{
				D.SpriteCentered(*S, FVector2D(X + IconS * 0.5f, Y + H + RowH * 0.5f), IconS / FMath::Max(1.f, S->H));
			}
			const FLinearColor C = It.bOk ? FVPal::Ink() : (It.bHard ? FV::Col(190, 50, 40) : FV::Col(196, 112, 20));
			D.Text(D.Ellipsize(CheckLabel(It), W - IconS - 3.f, Size), FVector2D(X + IconS + 3.f, Y + H), Size, C, EFVAlign::Left, false, true);
			H += RowH;
		}
		return H;
	}

	class FVMapScreen final : public FVScreen
	{
	public:
		explicit FVMapScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			InitState();
		}

		void InitState()
		{
			bInit = true;
			TickCount = 0;
			Lay = ComputeLayout();
			Info = ComputeInfo(Lay.Dims);
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			SelId = P ? P->CurrentLocation : NAME_None;
			if (FVDB::IsHomeId(SelId)) { SelId = FVDB::HomeId(); }   // yard or house: the «Дом» pin
			else if (!FVDB::FindLocation(SelId) && !FVDBU6::IsForestId(SelId) && FVDB::Locations().Num() > 0) { SelId = FVDB::Locations()[0].Id; }
			bScrollInit = false;   // update 6: centre the wide map on the current place
			SelFish = NAME_None;
			FishLoc = NAME_None;
			FishDetailScroll.Reset();
			FishDetailFor = NAME_None;
			// autotest hook (context «fv_expand», the map uses no other context): portrait opens with a fish of the current
			// place picked (its legend if there is one: the longest facts), so the full fish details are on the photo
			if (Game && Lay.bPortrait && Game->GetScreenContext() == FName(TEXT("fv_expand")))
			{
				for (const FFVFishDef* F : FVDB::FishAt(SelId))
				{
					if (!F) { continue; }
					if (SelFish.IsNone() || F->Legend != EFVLegend::None) { SelFish = F->Id; }
					if (F->Legend != EFVLegend::None) { break; }
				}
				FishLoc = SelFish.IsNone() ? NAME_None : SelId;
			}
			// the details pager starts on the selected place (no slide on open)
			const TArray<const FFVLocationDef*> Pins = MapPins();
			InfoPager = FFVPager();
			InfoPager.NumPagesCached = FMath::Max(1, Pins.Num());
			InfoPager.SetPage(FMath::Max(0, PinIndex(Pins, SelId)));
		}

		static int32 PinIndex(const TArray<const FFVLocationDef*>& Pins, FName Id)
		{
			return Pins.IndexOfByPredicate([Id](const FFVLocationDef* L) { return L && L->Id == Id; });
		}

		/** Selects pin Idx: the details pager slides to it. A far jump starts one page away, so it reads as a single
		    slide from the side the place lies on instead of flicking through every place in between. */
		void SelectPin(int32 Idx, int32 NumPins)
		{
			if (NumPins <= 0) { return; }
			Idx = FMath::Clamp(Idx, 0, NumPins - 1);
			InfoPager.NumPagesCached = NumPins;
			if (Idx == InfoPager.Page) { return; }
			const float Dist = float(Idx) - InfoPager.Visual;
			if (!InfoPager.bDrag && FMath::Abs(Dist) > 1.f) { InfoPager.Visual = float(Idx) - FMath::Sign(Dist); }
			InfoPager.GoTo(Idx);
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			if (!bInit) { InitState(); }
			++TickCount;
			UnlockFx = FMath::Max(0.f, UnlockFx - Dt);
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (In.Wheel != 0 && Lay.MapRect.Contains(In.Mouse)) { PendingWheel += In.Wheel; }   // update 6: scroll the wide map
			const TArray<const FFVLocationDef*> Pins = MapPins();
			if (Pins.Num() > 0 && (In.WasPressed(EKeys::Left) || In.WasPressed(EKeys::A) || In.WasPressed(EKeys::Right) || In.WasPressed(EKeys::D)))
			{
				// the pager plays the page sound; wrapping around jumps with a one-page slide
				const int32 Idx = InfoPager.Page;
				const int32 Dir = (In.WasPressed(EKeys::Left) || In.WasPressed(EKeys::A)) ? -1 : 1;
				SelectPin((Idx + Dir + Pins.Num()) % Pins.Num(), Pins.Num());
			}
			if (In.WasPressed(EKeys::M))
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
		}

		virtual void Draw(FVDraw& D) override
		{
			if (!bInit) { InitState(); }
			Lay = ComputeLayout();
			if (Info.Dims != Lay.Dims) { Info = ComputeInfo(Lay.Dims); }
			UpdateScroll(D);
			Lay.Origin.X -= FMath::RoundToFloat(ScrollX);   // update 6: the map content scrolls under the fixed map window
			const bool bClose = D.Window(Lay.Win, TEXT("Карта долины"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			FName TravelId = NAME_None;
			bool bBack = false;

			DrawMap(D, P);

			// ---- the details panel: one page per place, swipe left/right (or tap a pin / ←→) to slide to another
			const TArray<const FFVLocationDef*> Pins = MapPins();
			const FFVRect Panel = Lay.bPortrait ? Lay.Info : SideRect;
			const FFVRect PagerArea(Panel.X - 4.f, Panel.Y - 1.f, Panel.W + 8.f, Panel.H + 5.f);
			InfoPager.Update(D, PagerArea, Pins.Num());
			if (Pins.IsValidIndex(InfoPager.Page) && Pins[InfoPager.Page] && Pins[InfoPager.Page]->Id != SelId)
			{
				SelId = Pins[InfoPager.Page]->Id;
				SelFish = NAME_None;
				FishLoc = SelId;
			}
			InfoPager.DrawPages(D, PagerArea, [&](int32 PageIdx)
			{
				const FFVLocationDef* PL = Pins.IsValidIndex(PageIdx) ? Pins[PageIdx] : nullptr;
				if (!PL) { return; }
				if (Lay.bPortrait) { DrawInfoPortrait(D, P, *PL, Panel); }
				else { DrawSide(D, P, *PL, Panel, TravelId); }
			});
			DrawPageDots(D, Panel, Pins.Num());

			if (Lay.bPortrait)
			{
				DrawActionBarPortrait(D, P, TravelId, bBack);
			}
			else
			{
				if (!FV::IsTouchUI())
				{
					(void)0; /* update 3: no key hints at the bottom (see the guide) */
				}
			}

			if (!Game) { return; }
			if (!TravelId.IsNone())
			{
				UFVGame* G = Game;          // the screen may be destroyed by the calls below
				if (TravelId == FVDBU6::CityId())
				{
					G->OpenScreenWith(EFVScreen::Voyage, TravelId);   // update 6: the short sailing scene travels
					return;
				}
				const bool bOk = G->TravelToEntry(TravelId, FName(TEXT("from_map")));
				if (!bOk)
				{
					FVUI::Sfx(EFVSfx::Error);
					G->Toast(TEXT("Сейчас туда не попасть"), FV::Col(255, 150, 130), EFVIcon::Exclaim);
					return;
				}
				if (G->GetScreen() == EFVScreen::Map) { G->CloseScreen(); }
				return;
			}
			if (bClose || bBack)
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
		}

	private:
		FMapLayout Lay;
		FMapInfo Info;
		FName SelId;
		FName SelFish;               // portrait: fish whose details are shown under the grid
		FName FishLoc;               // location SelFish belongs to (reset when another place is selected)
		FFVScrollView FishDetailScroll;   // portrait: the fish facts when they do not fit under the grid
		FName FishDetailFor;              // fish FishDetailScroll was scrolled for (a new fish starts at the top)
		FFVScrollView HomeScroll;         // «Дом» details: the station list when it is taller than the panel
		bool bInit = false;
		int32 TickCount = 0;
		float UnlockFx = 0.f;
		FName UnlockFxId;
		FFVPager InfoPager;          // details panel: one page per map pin (MapPins order)
		// ---- update 6 (§3): horizontal scrolling of the wide map
		float ScrollX = 0.f;         // px of the content left of the map window
		float ScrollVel = 0.f;       // px/s (fling)
		float ScrollTarget = -1.f;   // animated scroll (a pin chosen from the details / keys), < 0 = none
		bool bScrollInit = false;
		bool bMapDrag = false;
		float DragStartScroll = 0.f;
		FName ScrolledFor;           // SelId the scroll last followed
		int32 PendingWheel = 0;

		/** Width of the extension right of the classic image (px). */
		float ExtraW() const { return float(ExtTexels(Lay.Dims)) * MapScale; }
		float MaxScroll() const { return FMath::Max(0.f, float(Lay.Dims.W) * MapScale + ExtraW() - Lay.MapRect.W); }
		/** Content X (px from the left of the classic image) of a pin. */
		float PinContentX(const FFVLocationDef& L) const { return PinTexel(L, Lay.Dims).X * MapScale; }

		void CenterOn(FName Id, bool bInstant)
		{
			const FFVLocationDef* L = FVDB::IsHomeId(Id) ? &FVDB::HomeLocation() : (FVDBU6::IsForestId(Id) ? &FVDBU6::ForestLocation() : FVDB::FindLocation(Id));
			if (!L) { return; }
			const float T = FMath::Clamp(PinContentX(*L) - Lay.MapRect.W * 0.5f, 0.f, MaxScroll());
			if (bInstant) { ScrollX = T; ScrollVel = 0.f; ScrollTarget = -1.f; }
			else
			{
				// only when the pin is not comfortably visible
				const float X = PinContentX(*L) - ScrollX;
				if (X < 40.f || X > Lay.MapRect.W - 40.f) { ScrollTarget = T; ScrollVel = 0.f; }
			}
		}

		void UpdateScroll(FVDraw& D)
		{
			const float Dt = D.FrameDt();
			const float Max = MaxScroll();
			if (!bScrollInit)
			{
				bScrollInit = true;
				CenterOn(SelId, true);
				ScrolledFor = SelId;
			}
			else if (SelId != ScrolledFor)
			{
				ScrolledFor = SelId;
				if (!bMapDrag) { CenterOn(SelId, false); }
			}
			const FFVRect& View = Lay.MapRect;
			const bool bDragNow = D.IsPointerDown() && D.IsMovedGesture() && D.DragAxis() == FVDraw::EDragAxis::Horizontal
				&& View.Contains(D.PressPos()) && Max > 0.f && D.ClaimDrag(0x4D4150u);
			if (bDragNow)
			{
				if (!bMapDrag) { bMapDrag = true; DragStartScroll = ScrollX; ScrollTarget = -1.f; }
				float X = DragStartScroll - float(D.DragDelta().X);
				if (X < 0.f) { X *= 0.35f; }                          // rubber band at the edges
				else if (X > Max) { X = Max + (X - Max) * 0.35f; }
				ScrollX = X;
				ScrollVel = -float(D.PointerVelocity().X);
			}
			else
			{
				if (bMapDrag) { bMapDrag = false; }
				if (PendingWheel != 0 && Max > 0.f)
				{
					ScrollTarget = FMath::Clamp((ScrollTarget >= 0.f ? ScrollTarget : ScrollX) - float(PendingWheel) * 60.f, 0.f, Max);
					ScrollVel = 0.f;
				}
				if (ScrollTarget >= 0.f)
				{
					ScrollX = FMath::Lerp(ScrollX, ScrollTarget, 1.f - FMath::Exp(-10.f * Dt));
					if (FMath::Abs(ScrollX - ScrollTarget) < 0.5f) { ScrollX = ScrollTarget; ScrollTarget = -1.f; }
				}
				else
				{
					ScrollX += ScrollVel * Dt;
					ScrollVel *= FMath::Exp(-4.f * Dt);
					if (FMath::Abs(ScrollVel) < 5.f) { ScrollVel = 0.f; }
					if (ScrollX < 0.f) { ScrollX = FMath::Lerp(ScrollX, 0.f, 1.f - FMath::Exp(-14.f * Dt)); ScrollVel = 0.f; if (ScrollX > -0.5f) { ScrollX = 0.f; } }
					else if (ScrollX > Max) { ScrollX = FMath::Lerp(ScrollX, Max, 1.f - FMath::Exp(-14.f * Dt)); ScrollVel = 0.f; if (ScrollX < Max + 0.5f) { ScrollX = Max; } }
				}
			}
			PendingWheel = 0;
		}

		/** The extension right of the classic image is a part of the map image (FMapBuilder::FarCoast); here only its
		    animation: twinkles on the open sea and the lighthouse's blinking light. */
		void DrawExtension(FVDraw& D, const UFVSaveGame* P)
		{
			const float X0 = Lay.Origin.X + float(Lay.Dims.W) * MapScale;
			const float W = ExtraW();
			const FFVRect& View = Lay.MapRect;
			if (X0 > View.Right() || X0 + W < View.X) { return; }
			const float T = float(D.Time);
			const int32 MapH = Lay.Dims.H;
			// sea twinkles between the classic image's edge and the far coast (the same look as the classic ones)
			const int32 N = FMath::Max(1, FMath::RoundToInt(10.f * float(MapH) / 148.f));
			for (int32 i = 0; i < N; ++i)
			{
				const float TY = 4.f + FVUI::Hash01(i, 1, 82) * (MapH - 8.f);
				const float SeaW = FarCoastX(Lay.Dims, TY) - float(Lay.Dims.W) - 6.f;
				if (SeaW < 2.f) { continue; }
				const float TX = float(Lay.Dims.W) + FVUI::Hash01(i, 2, 82) * SeaW;
				const float A = FMath::Max(0.f, FMath::Sin(T * 1.7f + i * 2.3f + 0.7f));
				if (A < 0.05f) { continue; }
				const FVector2D Q = Lay.Origin + FVector2D(TX, TY) * MapScale;
				D.Rect(FFVRect(Q.X, Q.Y, 4.f, 1.f), FV::Col(230, 246, 255, uint8(200 * A)));
				D.Rect(FFVRect(Q.X + 1.f, Q.Y - 1.f, 2.f, 1.f), FV::Col(230, 246, 255, uint8(120 * A)));
			}
			// the lighthouse's light
			{
				const FVector2D L = Lay.Origin + (LighthouseTexel(Lay.Dims) + FVector2D(1.f, 0.5f)) * MapScale;
				const float Blink = 0.5f + 0.5f * FMath::Sin(T * 3.f);
				D.Glow(L, 8.f + 4.f * Blink, FLinearColor(1.f, 0.9f, 0.5f, 0.3f + 0.3f * Blink));
			}
			(void)P;
		}

		/** Dashed sea route from the ocean pier to the City (+ «Нужна лодка» until the boat is repaired). */
		void DrawSeaRoute(FVDraw& D, const UFVSaveGame* P)
		{
			const FFVLocationDef* Ocean = FVDB::FindLocation(FName(TEXT("ocean")));
			const FFVLocationDef* City = FVDB::FindLocation(FVDBU6::CityId());
			if (!Ocean || !City) { return; }
			const FVector2D A = PinVirtual(*Ocean, Lay), B = PinVirtual(*City, Lay);
			const float Len = FVector2D::Distance(A, B);
			if (Len < 4.f) { return; }
			const FVector2D Dir = (B - A) / Len;
			const FVector2D Perp(-Dir.Y, Dir.X);
			const float T = float(D.Time);
			const float Shift = FMath::Fmod(T * 10.f, 10.f);
			const bool bBoat = P && P->IsBoatRepaired();
			for (float d = 10.f + Shift; d < Len - 10.f; d += 10.f)
			{
				const float K = d / Len;
				const FVector2D Q = A + Dir * d + Perp * (FMath::Sin(K * PI) * -18.f);
				D.Rect(FFVRect(FMath::RoundToFloat(Q.X) - 2.f, FMath::RoundToFloat(Q.Y) - 1.f, 4.f, 2.f), bBoat ? FV::Col(255, 244, 200, 230) : FV::Col(255, 255, 255, 150));
			}
			if (!bBoat)
			{
				// the label sits over the middle of the route; while the route is only partly in the map window (the wide
				// map scrolls), it slides along the route to stay whole inside the window instead of being cut at its edge
				const TCHAR* const NeedBoat = TEXT("Нужна лодка");
				const FFVRect& V = Lay.MapRect;
				const float HalfW = D.MeasureText(NeedBoat, Lay.LabelSize, true).X * 0.5f + 4.f;
				float Dm = Len * 0.5f;
				const float RMin = FMath::Min(A.X, B.X), RMax = FMath::Max(A.X, B.X);
				if (FMath::Abs(Dir.X) > 0.2f && RMax > V.X && RMin < V.Right())
				{
					const float Lo = FMath::Max(V.X + HalfW, RMin + 4.f), Hi = FMath::Min(V.Right() - HalfW, RMax - 4.f);
					const float Mx = A.X + Dir.X * Dm;
					const float X = Lo <= Hi ? FMath::Clamp(Mx, Lo, Hi) : (Lo + Hi) * 0.5f;
					Dm = FMath::Clamp((X - A.X) / Dir.X, 0.f, Len);
				}
				const FVector2D M = A + Dir * Dm + Perp * (FMath::Sin(Dm / Len * PI) * -18.f);
				D.TextOutlined(NeedBoat, FVector2D(FMath::RoundToFloat(M.X), FMath::RoundToFloat(M.Y - 12.f)), Lay.LabelSize, FV::Col(255, 236, 170), EFVAlign::Center, FV::Col(30, 30, 60));
			}
		}

		/** Little arrows at the map edges while more of it lies beyond (tap = scroll that way). */
		void DrawScrollHints(FVDraw& D)
		{
			const float Max = MaxScroll();
			if (Max <= 0.f) { return; }
			const FFVRect& V = Lay.MapRect;
			const float Pulse = 0.6f + 0.4f * FMath::Sin(float(D.Time) * 3.f);
			const float S = Lay.bPortrait ? 22.f : 16.f;
			auto Arrow = [&](bool bRight)
			{
				const FFVRect R(bRight ? V.Right() - S - 2.f : V.X + 2.f, V.Center().Y - S * 0.5f, S, S);
				D.Rect(R, FV::Col(40, 24, 14, uint8(110 * Pulse)));
				const float CX = R.Center().X, CY = R.Center().Y;
				for (int32 r = 0; r < 5; ++r)
				{
					const float HW = float(5 - r);
					const float X = bRight ? CX - 2.f + float(r) : CX + 2.f - float(r) - 1.f;
					D.Rect(FFVRect(X, CY - HW, 1.f, HW * 2.f), FV::Col(255, 244, 214, uint8(230 * Pulse)));
				}
				if (D.Clicked(R))
				{
					ScrollTarget = FMath::Clamp(ScrollX + (bRight ? 1.f : -1.f) * V.W * 0.6f, 0.f, Max);
					ScrollVel = 0.f;
				}
			};
			if (ScrollX > 1.f) { Arrow(false); }
			if (ScrollX < Max - 1.f) { Arrow(true); }
		}

		/** Little page dots under the details panel (which place of how many; the active one follows the slide). */
		void DrawPageDots(FVDraw& D, const FFVRect& Panel, int32 N)
		{
			if (N <= 1) { return; }
			const bool bP = Lay.bPortrait;
			const float Dot = bP ? 4.f : 3.f, Gap = bP ? 5.f : 4.f;
			const float W = float(N) * Dot + float(N - 1) * Gap;
			const float X0 = FMath::RoundToFloat(Panel.Center().X - W * 0.5f);
			const float Y = FMath::RoundToFloat(Panel.Bottom() + (bP ? 2.f : 1.f));
			const float V = FMath::Clamp(InfoPager.VisualPos(), 0.f, float(N - 1));
			for (int32 i = 0; i < N; ++i)
			{
				const float K = FMath::Clamp(1.f - FMath::Abs(V - float(i)), 0.f, 1.f);
				D.Rect(FFVRect(X0 + float(i) * (Dot + Gap), Y, Dot, Dot), FVUI::LerpLinear(FV::Col(150, 104, 62, 150), FVPal::Gold(), K));
			}
		}

		/** Unlock (shared by both layouts): pays, plays the unlock effect on the pin, reports the result. */
		void TryUnlock(UFVSaveGame* P, const FFVLocationDef& L)
		{
			if (!P) { return; }
			const EFVBuyResult R = P->UnlockLocation(L.Id);
			if (FVUI::ReportBuy(Game, R, FString::Printf(TEXT("Открыто: %s!"), *L.Name)))
			{
				UnlockFx = 1.6f;
				UnlockFxId = L.Id;
				if (Game) { Game->NotifyProfileChanged(); }
			}
		}

		/** Runs the main action of a place (shared by both layouts). */
		void RunAction(UFVSaveGame* P, const FFVLocationDef& L, const FMapAction& A, FName& OutTravel)
		{
			if (A.Kind == EMapAction::Travel)
			{
				// update 6: the forest / City / Cave open by their conditions (checked by ActionFor), not by coins
				if (P && IsU6Place(L.Id) && !P->IsLocationUnlocked(L.Id)) { P->UnlockLocation(L.Id); }
				OutTravel = L.Id;
			}
			else if (A.Kind == EMapAction::Unlock) { TryUnlock(P, L); }
		}

		void DrawAnimatedOverlay(FVDraw& D)
		{
			const float T = float(D.Time);
			const int32 MapW = Lay.Dims.W;
			const int32 MapH = Lay.Dims.H;
			const FFVRect& MapRect = Lay.MapRect;
			// sea twinkles
			const int32 NumTwinkles = FMath::Max(1, FMath::RoundToInt(18.f * float(MapH) / 148.f));
			for (int32 i = 0; i < NumTwinkles; ++i)
			{
				const float TY = 4.f + FVUI::Hash01(i, 1, 81) * (MapH - 8.f);
				const float Coast = CoastX(Info, TY);
				if (Coast > MapW - 6.f) { continue; }
				const float TX = Coast + 4.f + FVUI::Hash01(i, 2, 81) * FMath::Max(1.f, MapW - Coast - 8.f);
				const float A = FMath::Max(0.f, FMath::Sin(T * 1.7f + i * 2.1f));
				if (A < 0.05f) { continue; }
				const FVector2D P = Lay.Origin + FVector2D(TX, TY) * MapScale;
				D.Rect(FFVRect(P.X, P.Y, 4.f, 1.f), FV::Col(230, 246, 255, uint8(200 * A)));
				D.Rect(FFVRect(P.X + 1.f, P.Y - 1.f, 2.f, 1.f), FV::Col(230, 246, 255, uint8(120 * A)));
			}
			// volcano smoke & glow
			if (Info.bHas[int32(EFVTheme::Volcano)])
			{
				const FVector2D C = Lay.Origin + Info.Pin[int32(EFVTheme::Volcano)] * MapScale;
				D.Glow(C, 18.f + 3.f * FMath::Sin(T * 2.3f), FLinearColor(1.f, 0.35f, 0.05f, 0.35f));
				for (int32 i = 0; i < 5; ++i)
				{
					const float Ph = FMath::Frac(T * 0.16f + i / 5.f);
					const FVector2D SP = C + FVector2D(FMath::Sin(Ph * 6.f + i) * 3.f + Ph * 14.f, -6.f - Ph * 40.f);
					if (SP.Y < MapRect.Y + 6.f) { continue; }
					D.Circle(SP, 2.5f + Ph * 6.f, FV::Col(96, 90, 96, uint8(110 * (1.f - Ph))));
				}
			}
			// drifting cloud shadows (spread over the map height: 40 / 128 / 216 px on the classic 296 px map)
			for (int32 c = 0; c < 3; ++c)
			{
				const float Span = MapRect.W + 60.f;
				const float X = MapRect.X - 30.f + FMath::Fmod(T * (4.f + c) + c * 157.f, Span);
				const float Y = MapRect.Y + MapRect.H * ((40.f + c * 88.f) / 296.f) + 6.f * FMath::Sin(T * 0.2f + c);
				const float Edge = FMath::Clamp(FMath::Min(X - MapRect.X, MapRect.Right() - X) / 30.f, 0.f, 1.f);
				if (Edge <= 0.f) { continue; }
				const FLinearColor CC = FV::Col(255, 255, 255, uint8(46 * Edge));
				D.Circle(FVector2D(X, Y), 9.f, CC);
				D.Circle(FVector2D(X + 10.f, Y + 2.f), 7.f, CC);
				D.Circle(FVector2D(X - 10.f, Y + 3.f), 6.f, CC);
				D.Circle(FVector2D(X + 3.f, Y - 5.f), 6.f, CC);
			}
		}

		void DrawMap(FVDraw& D, UFVSaveGame* P)
		{
			const FFVRect& MapRect = Lay.MapRect;
			D.Rect(MapRect.Inset(-2.f), FVPal::WoodDark());
			D.PushClip(MapRect);   // update 6: the wide map scrolls under this window
			if (const FFVSprite* Map = GetMapSprite(Lay.Dims))
			{
				D.Sprite(*Map, Lay.Origin, MapScale);
			}
			else
			{
				D.Rect(FFVRect(Lay.Origin.X, Lay.Origin.Y, Lay.Dims.W * MapScale + ExtraW(), Lay.Dims.H * MapScale), FV::Col(118, 180, 86));
			}
			DrawExtension(D, P);
			DrawAnimatedOverlay(D);
			DrawSeaRoute(D, P);

			const float T = float(D.Time);
			const float K = Lay.PinK;
			const float LS = Lay.LabelSize;
			const TArray<const FFVLocationDef*> Pins = MapPins();
			const FFVLocationDef* HoverLoc = nullptr;
			// Fingers: bigger pin targets, and the name label under a pin selects it too.
			const bool bBigHits = Lay.bPortrait || FV::IsTouchUI();
			for (const FFVLocationDef* LP : Pins)
			{
				if (!LP) { continue; }
				const FFVLocationDef& L = *LP;
				const bool bHome = FVDB::IsHomeId(L.Id);
				const FVector2D C = PinVirtual(L, Lay);
				if (C.X < MapRect.X - 4.f || C.X > MapRect.Right() + 4.f) { continue; }   // update 6: scrolled out of the window
				const bool bUnl = IsOpenOnMap(P, L);
				const bool bCur = IsCurrentPlace(P, L.Id);
				const bool bSel = L.Id == SelId;
				const float HitHalf = Lay.PinHitHalf;
				const FFVRect Hit(C.X - HitHalf, C.Y - HitHalf, HitHalf * 2.f, HitHalf * 2.f);
				// name label under the pin, kept inside the map
				const FVector2D LSz = D.MeasureText(L.Name, LS, true);
				const float LabelCX = FMath::Clamp(C.X, MapRect.X + LSz.X * 0.5f + 3.f, MapRect.Right() - LSz.X * 0.5f - 3.f);
				const float LabelY = C.Y + 9.f * K;
				FFVRect LabelHit;
				if (bBigHits)
				{
					LabelHit = FFVRect(LabelCX - LSz.X * 0.5f - 3.f, LabelY - 1.f, LSz.X + 6.f, LSz.Y + 4.f);
				}
				const bool bHover = D.Hover(Hit) || (bBigHits && D.Hover(LabelHit));

				if (bSel)
				{
					const float R = 11.f * K + 1.2f * FMath::Sin(T * 4.f);
					if (Lay.bPortrait) { D.Circle(C, R, FV::Col(255, 236, 150, 70)); }
					D.Ring(C, R, R, FV::Alpha(FVPal::Gold(), 0.9f), Lay.bPortrait ? 2.f : 1.5f);
				}
				if (bCur)
				{
					const float Ph = FMath::Frac(T * 0.8f);
					const float RR = (8.f + Ph * 10.f) * K;
					D.Ring(C, RR, RR, FV::Col(255, 255, 255, uint8(200 * (1.f - Ph))), 1.f);
				}
				if (UnlockFx > 0.f && L.Id == UnlockFxId)
				{
					const float FK = 1.f - UnlockFx / 1.6f;
					D.Glow(C, 20.f + FK * 20.f, FLinearColor(1.f, 0.85f, 0.4f, 0.6f * (1.f - FK)));
					D.Ring(C, 6.f + FK * 26.f, 6.f + FK * 26.f, FV::Alpha(FVPal::Gold(), 1.f - FK), 1.5f);
					for (int32 s = 0; s < 8; ++s)
					{
						const float A = s * PI / 4.f + FK * 1.5f;
						const FVector2D SP = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * (8.f + FK * 22.f);
						D.Rect(FFVRect(SP.X - 1.f, SP.Y - 1.f, 2.f, 2.f), FV::Alpha(FV::Col(255, 250, 200), 1.f - FK));
					}
				}

				D.Circle(C + FVector2D(1.f, 2.f), 7.5f * K, FV::Col(0, 0, 0, 80));
				if (bHome)
				{
					// «Дом»: a warm red pin with a little house
					D.Circle(C, 7.5f * K, FV::Col(70, 30, 18));
					D.Circle(C, 6.3f * K, bHover ? FV::Col(236, 128, 96) : FV::Col(206, 86, 60));
					D.Circle(C + FVector2D(-2.f, -2.f) * K, 1.6f * K, FV::Col(255, 255, 255, 150));
					if (const FFVSprite* HouseIcon = FVUI::Icon(EFVIcon::House))
					{
						D.SpriteCentered(*HouseIcon, C, FMath::Min(1.f, 10.f * K / FMath::Max(1.f, FMath::Max(HouseIcon->W, HouseIcon->H))));
					}
				}
				else if (bUnl)
				{
					const FLinearColor TC = FVUI::ThemeColor(L.Theme);
					D.Circle(C, 7.f * K, FV::Col(56, 30, 18));
					D.Circle(C, 5.8f * K, bHover ? FVUI::LerpLinear(TC, FLinearColor::White, 0.3f) : TC);
					D.Circle(C + FVector2D(-1.8f, -1.8f) * K, 1.8f * K, FV::Col(255, 255, 255, 170));
					D.Circle(C, 1.6f * K, FV::Col(255, 250, 230));
				}
				else
				{
					D.Circle(C, 8.f * K, FV::Col(56, 48, 46));
					D.Circle(C, 6.8f * K, bHover ? FV::Col(176, 166, 158) : FV::Col(146, 138, 132));
					if (const FFVSprite* Lock = FVUI::Icon(EFVIcon::Lock))
					{
						D.SpriteCentered(*Lock, C, FMath::Min(1.f, 12.f * K / FMath::Max(1.f, FMath::Max(Lock->W, Lock->H))));
					}
				}
				if (bCur)
				{
					const float B = FMath::Abs(FMath::Sin(T * 4.f)) * 3.f;
					const float AY = C.Y - (Lay.bPortrait ? 23.f : 19.f) - B;
					for (int32 r = 0; r < 5; ++r)
					{
						const float HW = 4.f - r;
						D.Rect(FFVRect(C.X - HW - 1.f, AY + r - 1.f, HW * 2.f + 3.f, 1.f), FV::Col(70, 34, 14));
					}
					for (int32 r = 0; r < 4; ++r)
					{
						const float HW = 3.f - r;
						D.Rect(FFVRect(C.X - HW, AY + r, HW * 2.f + 1.f, 1.f), r == 0 ? FV::Col(255, 236, 140) : FVPal::Gold());
					}
				}
				const FLinearColor LabelC = bSel && Lay.bPortrait ? FV::Col(255, 236, 150) : (bUnl ? FVPal::White() : FV::Col(214, 204, 196));
				D.TextOutlined(L.Name, FVector2D(LabelCX, LabelY), LS, LabelC, EFVAlign::Center, FV::Col(40, 24, 14));

				if (bHover && MapRect.Contains(D.Mouse()))
				{
					HoverLoc = &L;
					if (D.Clicked(Hit) || (bBigHits && D.Clicked(LabelHit)))
					{
						SelectPin(PinIndex(Pins, L.Id), Pins.Num());
					}
				}
			}

			D.PopClip();
			DrawScrollHints(D);
			D.Frame(MapRect.Inset(-1.f), FV::Col(150, 96, 50), 1.f);

			// Portrait: the details panel right below the map already says everything (and phones have no hover).
			if (HoverLoc && !Lay.bPortrait && !FV::IsTouchUI() && FVDB::IsHomeId(HoverLoc->Id))
			{
				D.Tooltip(HoverLoc->Name, IsCurrentPlace(P, HoverLoc->Id) ? FString(TEXT("Вы дома"))
					: FString(TEXT("Всегда открыто — двор с участками под постройки и дом со станками, сундуком и кроватью")));
			}
			else if (HoverLoc && !Lay.bPortrait && !FV::IsTouchUI())
			{
				const bool bUnl = IsOpenOnMap(P, *HoverLoc);
				const bool bCur = P ? P->CurrentLocation == HoverLoc->Id : false;
				FString Body;
				if (bCur) { Body = TEXT("Вы здесь"); }
				else if (bUnl) { Body = TEXT("Открыто — можно отправиться"); }
				else if (IsU6Place(HoverLoc->Id))
				{
					bool bOk = false;
					Body = FString::Printf(TEXT("Уровень %d · %s"), HoverLoc->MinLevel, *U6ConditionText(P, *HoverLoc, bOk));
				}
				else
				{
					Body = FString::Printf(TEXT("Закрыто · уровень %d · %s"), HoverLoc->MinLevel, *FVUI::Coins(HoverLoc->UnlockPrice));
				}
				if (P)
				{
					const int32 Total = FVDB::FishAt(HoverLoc->Id).Num();
					Body += FString::Printf(TEXT("\nПоймано видов: %d / %d"), P->SpeciesCaughtAt(HoverLoc->Id), Total);
				}
				D.Tooltip(HoverLoc->Name, Body);
			}
		}

		// ------------------------------------------------------------------------------------
		// Portrait: details panel (under the map) and the bottom action bar
		// ------------------------------------------------------------------------------------
		/** One page of the portrait details pager: place Loc drawn into panel R (local coords of the page). */
		void DrawInfoPortrait(FVDraw& D, UFVSaveGame* P, const FFVLocationDef& Loc, const FFVRect& R)
		{
			D.Panel(R, EFVPanel::Parchment);
			const float BodyS = FV::Ui::Body();
			const float SmallS = FV::Ui::Small();
			const float HeadS = FV::Ui::Header();
			if (FVDB::IsHomeId(Loc.Id))
			{
				DrawHomeInfo(D, P, R, true);
				return;
			}
			const FFVLocationDef* L = &Loc;
			// the fish selection belongs to one place (a neighbour page sliding in shows its own grid unselected)
			const FName PageSelFish = FishLoc == L->Id ? SelFish : FName(NAME_None);
			const float X = R.X + 8.f;
			const float W = R.W - 16.f;
			float Y = R.Y + 7.f;
			const bool bUnl = IsOpenOnMap(P, *L);
			const bool bCur = P ? P->CurrentLocation == L->Id : false;
			const int32 Level = P ? P->GetLevel() : 1;
			const FLinearColor OkC = FV::Col(60, 130, 40);
			const FLinearColor BadC = FV::Col(186, 50, 40);

			D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 3.f), FVUI::ThemeColor(L->Theme));

			// ---- name + status chip
			const FString Status = bCur ? FString(TEXT("Вы здесь")) : (bUnl ? FString(TEXT("Открыто")) : FString(TEXT("Закрыто")));
			const float ChipW = FVUI::StatusChip(D, Status, FVector2D(X + W, Y + 2.f), bUnl ? FVUI::EChip::Good : FVUI::EChip::Bad, !bUnl, SmallS);
			D.Text(D.Ellipsize(L->Name, W - ChipW - 8.f, HeadS), FVector2D(X, Y), HeadS, FVPal::Ink(), EFVAlign::Left, true);
			Y += FMath::RoundToFloat(D.LineHeight(HeadS) + 3.f);

			// ---- requirements of a closed place: reserved at the bottom of the panel
			float Bottom = R.Bottom() - 6.f;
			if (!bUnl)
			{
				const float ReqH = FMath::RoundToFloat(D.LineHeight(BodyS) + 1.f);
				const FString LvlLabel = TEXT("Нужен уровень:");
				// update 6: the unlock condition sits right of the level; a long one («Почините мостик…») gets its own
				// wrapped lines under it instead of being cut
				bool bCondOk = false;
				const FString Cond = IsU6Place(L->Id) ? U6ConditionText(P, *L, bCondOk) : FString();
				const float LvlW = D.MeasureText(LvlLabel, SmallS, false).X + D.MeasureText(FString::FromInt(L->MinLevel), BodyS, true).X + 14.f;
				const bool bCondWrap = !Cond.IsEmpty() && D.MeasureText(Cond, SmallS, true).X > W - LvlW;
				const float CondH = bCondWrap ? FMath::RoundToFloat(D.MeasureWrapped(Cond, W, SmallS, true, 1.f) + 1.f) : 0.f;
				const float ReqY = Bottom - ReqH - CondH;
				D.Divider(FVector2D(X, ReqY - 5.f), W);
				const bool bLevelOk = Level >= L->MinLevel;
				const bool bMoneyOk = P ? P->CanAfford(L->UnlockPrice) : false;
				const float LabelDY = FMath::RoundToFloat((D.LineHeight(BodyS) - D.LineHeight(SmallS)) * 0.5f);
				D.Text(LvlLabel, FVector2D(X, ReqY + LabelDY), SmallS, FVPal::InkSoft(), EFVAlign::Left, false, false);
				D.Text(FString::FromInt(L->MinLevel), FVector2D(X + D.MeasureText(LvlLabel, SmallS, false).X + 4.f, ReqY), BodyS, bLevelOk ? OkC : BadC, EFVAlign::Left, false, true);
				if (IsU6Place(L->Id))
				{
					if (bCondWrap) { D.TextWrapped(Cond, FVector2D(X, ReqY + ReqH), W, SmallS, bCondOk ? OkC : BadC, true, 1.f); }
					else { D.Text(Cond, FVector2D(X + W, ReqY + LabelDY), SmallS, bCondOk ? OkC : BadC, EFVAlign::Right, false, true); }
				}
				else
				{
					const float MW = D.MoneyLabel(FVector2D(X + W, ReqY), L->UnlockPrice, BodyS, EFVAlign::Right, bMoneyOk ? OkC : BadC);
					D.Text(TEXT("Стоимость:"), FVector2D(X + W - MW - 4.f, ReqY + LabelDY), SmallS, FVPal::InkSoft(), EFVAlign::Right, false, false);
				}
				Bottom = ReqY - 9.f;
			}

			// ---- description (body text; one step smaller if it would take more than 3 lines)
			float DescS = BodyS;
			float DescH = D.MeasureWrapped(L->Description, W, DescS, false, 1.f);
			if (DescH > DescS * 1.4f * 3.f + 1.f)
			{
				DescS = BodyS - 1.f;
				DescH = D.MeasureWrapped(L->Description, W, DescS, false, 1.f);
			}
			D.TextWrapped(L->Description, FVector2D(X, Y), W, DescS, FVPal::InkSoft(), false, 1.f);
			Y += DescH + 3.f;
			D.Divider(FVector2D(X, Y), W);
			Y += 6.f;

			// ---- species progress (text + bar in one row)
			const TArray<const FFVFishDef*> Fish = FVDB::FishAt(L->Id);
			if (Fish.Num() == 0)
			{
				// update 6: a place without water (the forest) — no empty «0 / 0» grid
				D.TextWrapped(NoFishText(), FVector2D(X, Y), W, SmallS, FVPal::InkSoft(), false, 1.f);
				return;
			}
			const int32 Caught = P ? P->SpeciesCaughtAt(L->Id) : 0;
			const FString SpeciesText = FString::Printf(TEXT("Поймано видов: %d / %d"), Caught, Fish.Num());
			D.Text(SpeciesText, FVector2D(X, Y), BodyS, FVPal::Ink(), EFVAlign::Left, false, true);
			const float RowH = D.LineHeight(BodyS, true);
			const float BarX = X + D.MeasureText(SpeciesText, BodyS, true).X + 8.f;
			float BarRight = X + W;
			if (P && Fish.Num() > 0 && P->IsLocationComplete(L->Id))
			{
				if (const FFVSprite* Tr = FVUI::Icon(EFVIcon::Trophy))
				{
					D.SpriteCentered(*Tr, FVector2D(X + W - 8.f, Y + RowH * 0.5f), FMath::Min(1.f, 16.f / FMath::Max(1.f, Tr->H)));
					BarRight -= 20.f;
				}
			}
			D.Bar(FFVRect(BarX, FMath::RoundToFloat(Y + RowH * 0.5f - 4.f), FMath::Max(10.f, BarRight - BarX), 8.f),
				Fish.Num() > 0 ? float(Caught) / float(Fish.Num()) : 0.f, FVUI::ThemeColor(L->Theme));
			Y += FMath::RoundToFloat(RowH + 5.f);

			// ---- fish grid (tap a fish for its details); cells grow on tall phones, the details get at least ~2 lines
			const int32 Cols = 7;
			const int32 NumFish = FMath::Min(Fish.Num(), Cols * 2);
			const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(NumFish, Cols));
			const float Gap = 4.f;
			const float CellW = FMath::FloorToFloat((W - Gap * (Cols - 1)) / Cols);
			const float DetailMin = D.LineHeight(BodyS) + 2.f * SmallS * 1.4f + 6.f;
			const float CellH = FMath::Clamp(FMath::FloorToFloat((Bottom - Y - DetailMin - 6.f) / Rows) - Gap, 30.f, 46.f);
			UFVSprites* Sp = UFVSprites::Get();
			for (int32 i = 0; i < NumFish; ++i)
			{
				const FFVFishDef* F = Fish[i];
				if (!F) { continue; }
				const int32 Row = i / Cols;
				const int32 RowN = Row == 0 ? FMath::Min(NumFish, Cols) : NumFish - Cols;
				const float RowW = RowN * (CellW + Gap) - Gap;
				const float GX = FMath::RoundToFloat(X + (W - RowW) * 0.5f);
				const FFVRect Cell(GX + (i % Cols) * (CellW + Gap), Y + Row * (CellH + Gap), CellW, CellH);
				const FFVFishRecord* Rec = P ? P->GetRecord(F->Id) : nullptr;
				const bool bKnown = Rec && Rec->Caught > 0;
				const bool bSelFish = F->Id == PageSelFish;
				D.Rect(Cell, F->bLegendary ? FV::Col(214, 160, 40) : FV::Col(214, 156, 94));
				D.Rect(Cell.Inset(1.f), bSelFish ? FV::Col(255, 244, 214) : (bKnown ? FV::Col(255, 236, 196) : FV::Col(236, 206, 156)));
				if (Sp)
				{
					const FFVSprite& S = bKnown ? Sp->Fish(*F) : Sp->FishSilhouette(*F);
					FVUI::SpriteFit(D, S, Cell.Inset(3.f), bKnown ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.75f), 2.f);
				}
				if (bSelFish)
				{
					D.Frame(Cell.Inset(-1.f), FV::Col(214, 60, 40), 2.f);
				}
				else if (D.Hover(Cell))
				{
					D.Frame(Cell, FV::Col(236, 150, 60), 1.f);
				}
				if (D.Clicked(Cell))
				{
					FVUI::Sfx(EFVSfx::Click, 0.7f);
					FishLoc = L->Id;
					SelFish = bSelFish ? NAME_None : F->Id;
				}
			}
			Y += Rows * (CellH + Gap) + 2.f;

			// ---- selected fish details / tips (fills the space between the grid and the bottom block)
			DrawFishDetailPortrait(D, P, Fish, PageSelFish, X, Y, W, Bottom);
		}

		void DrawFishDetailPortrait(FVDraw& D, const UFVSaveGame* P, const TArray<const FFVFishDef*>& Fish, FName PageSelFish, float X, float Y, float W, float MaxY)
		{
			const float BodyS = FV::Ui::Body();
			const float SmallS = FV::Ui::Small();
			if (MaxY - Y < SmallS * 1.4f) { return; }
			const FFVFishDef* F = nullptr;
			for (const FFVFishDef* Q : Fish)
			{
				if (Q && Q->Id == PageSelFish) { F = Q; break; }
			}
			const FLinearColor Soft = FV::Alpha(FVPal::InkSoft(), 0.9f);
			if (!F)
			{
				const FString Tip = FV::IsTouchUI() ? FString(TEXT("Коснитесь рыбы — покажу, когда и где она клюёт."))
					: FString(TEXT("Нажмите на рыбу — покажу, когда и где она клюёт."));
				const FString Tip2 = TEXT("Клёв зависит от времени суток и погоды. Редкая рыба держится глубже — забрасывайте дальше.");
				const FString Both = Tip + TEXT("\n") + Tip2;
				if (D.MeasureWrapped(Both, W, SmallS, false, 1.1f) <= MaxY - Y)
				{
					D.TextWrapped(Both, FVector2D(X, Y), W, SmallS, Soft, false, 1.1f);
				}
				else if (D.MeasureWrapped(Tip, W, SmallS, false, 1.1f) <= MaxY - Y)
				{
					D.TextWrapped(Tip, FVector2D(X, Y), W, SmallS, Soft, false, 1.1f);
				}
				return;
			}

			const FFVFishRecord* Rec = P ? P->GetRecord(F->Id) : nullptr;
			const bool bKnown = Rec && Rec->Caught > 0;
			const FString Right = bKnown ? FString::Printf(TEXT("Поймано: %d · рекорд %s см"), Rec->Caught, *FVUI::Num(Rec->MaxSize, 1))
				: FString(TEXT("Ещё не поймана"));
			const float NameH = D.LineHeight(BodyS, true);
			const float RW = D.MeasureText(Right, SmallS, false).X;
			D.Text(Right, FVector2D(X + W, Y + FMath::RoundToFloat((NameH - D.LineHeight(SmallS, false)) * 0.5f)), SmallS, FVPal::InkSoft(), EFVAlign::Right, false, false);
			D.Text(D.Ellipsize(bKnown ? F->Name : FString(TEXT("???")), W - RW - 8.f, BodyS), FVector2D(X, Y), BodyS,
				bKnown ? FVUI::RarityInk(F->Rarity) : FVPal::InkSoft(), EFVAlign::Left, false, true);
			Y += FMath::RoundToFloat(NameH + 1.f);
			if (MaxY - Y < SmallS * 1.3f) { return; }

			FString Body = FString::Printf(TEXT("%s · %s · Клюёт: %s · %s"), *FVDB::RarityName(F->Rarity), *FVDB::DifficultyText(F->Difficulty),
				*FVDB::TimeMaskText(F->TimeMask), *FVDB::WeatherReqText(F->Weather));
			if (F->MinDepth >= 0.45f) { Body += TEXT(" · держится на глубине"); }
			if (P && F->MinLevel > P->GetLevel()) { Body += FString::Printf(TEXT(" · клюёт с уровня %d"), F->MinLevel); }
			// update 5: a legend also shows its preparation checklist under the facts
			const TArray<FFVLegendCheckItem> Check = (F->Legend != EFVLegend::None && Game)
				? FVDB::LegendChecklist(*F, Game->MakeCatchContext(0.f)) : TArray<FFVLegendCheckItem>();
			const float CheckH = float(Check.Num()) * FMath::RoundToFloat(D.LineHeight(SmallS, true) + 1.f);
			const float BodyH = D.MeasureWrapped(Body, W, SmallS, false, 1.1f);
			if (BodyH + (CheckH > 0.f ? CheckH + 2.f : 0.f) <= MaxY - Y)
			{
				D.TextWrapped(Body, FVector2D(X, Y), W, SmallS, FVPal::Ink(), false, 1.1f);
				Y += BodyH;
				if (Check.Num() > 0)
				{
					DrawLegendChecklist(D, Check, X, Y + 2.f, W, SmallS, MaxY);
				}
				return;
			}
			// does not fit: the facts + the checklist scroll in the free space (nothing is cut or dropped)
			if (FishDetailFor != F->Id)
			{
				FishDetailScroll.Reset();
				FishDetailFor = F->Id;
			}
			const FFVRect Area(X, Y, W, FMath::Max(1.f, MaxY - Y));
			const float TW = W - 7.f;   // room for the scrollbar
			const float BodyH2 = D.MeasureWrapped(Body, TW, SmallS, false, 1.1f);
			FishDetailScroll.Update(D, Area, BodyH2 + (CheckH > 0.f ? CheckH + 2.f : 0.f) + 2.f);
			D.PushClip(Area);
			const float CY = Area.Y - FishDetailScroll.Offset();
			D.TextWrapped(Body, FVector2D(X, CY), TW, SmallS, FVPal::Ink(), false, 1.1f);
			if (Check.Num() > 0)
			{
				DrawLegendChecklist(D, Check, X, CY + BodyH2 + 2.f, TW, SmallS, 1.0e6f);
			}
			D.PopClip();
			FishDetailScroll.DrawFades(D, Area, FVPal::Parchment());
			FishDetailScroll.DrawBar(D, Area);
		}

		/** Bottom bar: "← Назад" on the left, the big main action (Отправиться / Открыть за N) on the right. */
		void DrawActionBarPortrait(FVDraw& D, UFVSaveGame* P, FName& OutTravel, bool& bOutBack)
		{
			FFVRect MainR;
			if (D.ActionBar(Lay.Win, &MainR))
			{
				bOutBack = true;
			}
			const FFVLocationDef* L = FVDB::IsHomeId(SelId) ? &FVDB::HomeLocation()
				: (FVDBU6::IsForestId(SelId) ? &FVDBU6::ForestLocation() : FVDB::FindLocation(SelId));
			if (!L)
			{
				D.Button(MainR, TEXT("Выберите место"), EFVButton::Normal, false, FV::Ui::Body() + 1.f);
				return;
			}
			const FMapAction A = ActionFor(P, *L);
			if (D.Button(MainR, A.Label, A.bEnabled ? EFVButton::Primary : EFVButton::Normal, A.bEnabled, FV::Ui::Body() + 1.5f) && A.bEnabled)
			{
				RunAction(P, *L, A, OutTravel);
			}
		}

		/** Details of the «Дом» pin (both layouts): what is at home and which stations are built. */
		void DrawHomeInfo(FVDraw& D, const UFVSaveGame* P, const FFVRect& R, bool bPortrait)
		{
			const FFVLocationDef& Home = FVDB::HomeLocation();
			const float HeadS = bPortrait ? FV::Ui::Header() : 11.f;
			const float BodyS = bPortrait ? FV::Ui::Body() : 7.f;
			const float SmallS = bPortrait ? FV::Ui::Small() : 6.5f;
			const float X = R.X + (bPortrait ? 8.f : 7.f);
			const float W = R.W - (bPortrait ? 16.f : 14.f);
			float Y = R.Y + (bPortrait ? 7.f : 5.f);
			const float Bottom = R.Bottom() - 6.f;
			D.Rect(FFVRect(R.X + 1.f, R.Y + 1.f, R.W - 2.f, 3.f), FV::Col(206, 86, 60));

			// ---- name + state
			const bool bHere = IsCurrentPlace(P, Home.Id);
			const FString Name = Home.Name.IsEmpty() ? FString(TEXT("Дом")) : Home.Name;
			const float ChipW = FVUI::StatusChip(D, bHere ? TEXT("Вы дома") : TEXT("Всегда открыто"), FVector2D(X + W, Y + 2.f), FVUI::EChip::Good, false, SmallS);
			D.Text(D.Ellipsize(Name, W - ChipW - 8.f, HeadS), FVector2D(X, Y), HeadS, FVPal::Ink(), EFVAlign::Left, true);
			Y += FMath::RoundToFloat(D.LineHeight(HeadS) + 3.f);

			// ---- description
			const FString Desc = Home.Description.IsEmpty()
				? FString(TEXT("Свой двор с участками под постройки и домик у околицы. Внутри — кухня, мастерская, сундук и кровать: здесь можно мастерить, готовить и отдыхать до утра."))
				: Home.Description;
			const float DescH = D.MeasureWrapped(Desc, W, BodyS, false, 1.05f);
			if (Y + DescH <= Bottom)
			{
				D.TextWrapped(Desc, FVector2D(X, Y), W, BodyS, FVPal::InkSoft(), false, 1.05f);
				Y += DescH + 3.f;
			}
			if (!P || Y + 20.f > Bottom) { return; }
			D.Divider(FVector2D(X, Y), W);
			Y += 6.f;

			// ---- stations: how many are built, then every station with its level name
			int32 Built = 0;
			int32 Total = 0;
			for (int32 s = 0; s < int32(EFVStation::Count); ++s)
			{
				const EFVStation St = EFVStation(s);
				if (St == EFVStation::Bed || St == EFVStation::Chest) { continue; }
				++Total;
				if (P->IsStationBuilt(St)) { ++Built; }
			}
			const FString Count = FString::Printf(TEXT("Построено: %d из %d"), Built, Total);
			D.Text(Count, FVector2D(X, Y), BodyS, FVPal::Ink(), EFVAlign::Left, false, true);
			const float RowH = FMath::RoundToFloat(D.LineHeight(BodyS, true));
			const float BarX = X + D.MeasureText(Count, BodyS, true).X + 8.f;
			if (BarX < X + W - 16.f)
			{
				D.Bar(FFVRect(BarX, FMath::RoundToFloat(Y + RowH * 0.5f - 3.f), X + W - BarX, 6.f), Total > 0 ? float(Built) / float(Total) : 0.f, FV::Col(206, 86, 60));
			}
			Y += RowH + 3.f;
			// one entry per station: its name and its level name, both in full. An entry whose name and level name do not
			// fit side by side in its column takes two lines (the name above, the level name below it, right-aligned like
			// the rest); a row with such an entry is two lines tall for both columns. One full-width column when the list
			// fits so, else two columns; when even that is taller than the panel (short phones), the list scrolls.
			struct FEntry
			{
				FString Name;
				FString Level;
				bool bBuilt = false;
				float NameW = 0.f;
				float LevelW = 0.f;
			};
			TArray<FEntry> Entries;
			for (int32 s = 0; s < int32(EFVStation::Count); ++s)
			{
				const EFVStation St = EFVStation(s);
				const int32 Lvl = P->GetStationLevel(St);
				FEntry& E = Entries.AddDefaulted_GetRef();
				E.Name = FVDB::Station(St).Name;
				E.Level = Lvl > 0 ? FVDB::StationLevelName(St, Lvl) : FString(TEXT("—"));
				E.bBuilt = Lvl > 0;
				E.NameW = D.MeasureText(E.Name, SmallS, false).X;
				E.LevelW = D.MeasureText(E.Level, SmallS, E.bBuilt).X;
			}
			const float LineH = FMath::RoundToFloat(D.LineHeight(SmallS, false) + (bPortrait ? 2.f : 1.f));
			const float Line2 = FMath::RoundToFloat(D.LineHeight(SmallS, false));   // the level name line of a two-line row
			const float ColGap = 8.f;
			auto ColWidth = [&](int32 Cols, float ListW)
			{
				return Cols <= 1 ? ListW : FMath::FloorToFloat((ListW - ColGap * float(Cols - 1)) / float(Cols));
			};
			auto RowTwoLines = [&](int32 Row, int32 Cols, float ColW)
			{
				for (int32 c = 0; c < Cols; ++c)
				{
					const int32 i = Row * Cols + c;
					if (Entries.IsValidIndex(i) && Entries[i].NameW + 6.f + Entries[i].LevelW > ColW) { return true; }
				}
				return false;
			};
			auto ListHeight = [&](int32 Cols, float ListW)
			{
				const float ColW = ColWidth(Cols, ListW);
				float H = 0.f;
				for (int32 Row = 0; Row * Cols < Entries.Num(); ++Row) { H += RowTwoLines(Row, Cols, ColW) ? LineH + Line2 : LineH; }
				return H;
			};
			// Clip: rows outside it are skipped (scrolling list)
			auto DrawList = [&](int32 Cols, float ListW, float Top, const FFVRect* Clip)
			{
				const float ColW = ColWidth(Cols, ListW);
				float RowY = Top;
				for (int32 Row = 0; Row * Cols < Entries.Num(); ++Row)
				{
					const bool bTwo = RowTwoLines(Row, Cols, ColW);
					const float ThisH = bTwo ? LineH + Line2 : LineH;
					if (!Clip || (RowY + ThisH >= Clip->Y && RowY <= Clip->Bottom()))
					{
						for (int32 c = 0; c < Cols; ++c)
						{
							const int32 i = Row * Cols + c;
							if (!Entries.IsValidIndex(i)) { break; }
							const FEntry& E = Entries[i];
							const float CX = X + float(c) * (ColW + ColGap);
							// Ellipsize only guards a single word wider than the whole column (no station has one)
							D.Text(D.Ellipsize(E.Name, bTwo ? ColW : ColW - E.LevelW - 6.f, SmallS, false), FVector2D(CX, RowY), SmallS,
								E.bBuilt ? FVPal::Ink() : FVPal::InkSoft(), EFVAlign::Left, false, false);
							D.Text(D.Ellipsize(E.Level, ColW, SmallS, E.bBuilt), FVector2D(CX + ColW, bTwo ? RowY + Line2 : RowY), SmallS,
								E.bBuilt ? FV::Col(60, 130, 40) : FVPal::InkSoft(), EFVAlign::Right, false, E.bBuilt);
						}
					}
					RowY += ThisH;
				}
			};
			const float Avail = Bottom - Y;
			if (Avail < LineH) { return; }
			const int32 Cols = ListHeight(1, W) <= Avail ? 1 : 2;
			if (ListHeight(Cols, W) <= Avail)
			{
				DrawList(Cols, W, Y, nullptr);
				return;
			}
			// taller than the panel: the two columns scroll in the free space (the bar keeps a few px at the right)
			const FFVRect Area(X, Y, W, Avail);
			const float ListW = W - 7.f;
			HomeScroll.Update(D, Area, ListHeight(2, ListW) + 1.f);
			D.PushClip(Area);
			DrawList(2, ListW, Area.Y - HomeScroll.Offset(), &Area);
			D.PopClip();
			HomeScroll.DrawFades(D, Area, FVPal::Parchment());
			HomeScroll.DrawBar(D, Area);
		}

		void FishTooltip(FVDraw& D, const FFVFishDef& F, bool bKnown, const UFVSaveGame* P)
		{
			FString Body = FString::Printf(TEXT("%s · %s\n"), *FVDB::RarityName(F.Rarity), *FVDB::DifficultyText(F.Difficulty));
			Body += FString::Printf(TEXT("Клюёт: %s\n%s"), *FVDB::TimeMaskText(F.TimeMask), *FVDB::WeatherReqText(F.Weather));
			if (F.MinDepth >= 0.45f) { Body += TEXT("\nДержится на глубине — забрасывайте дальше"); }
			if (P && F.MinLevel > P->GetLevel()) { Body += FString::Printf(TEXT("\nКлюёт с уровня %d"), F.MinLevel); }
			if (bKnown && P)
			{
				if (const FFVFishRecord* R = P->GetRecord(F.Id))
				{
					Body += FString::Printf(TEXT("\nПоймано: %d · рекорд %s см"), R->Caught, *FVUI::Num(R->MaxSize, 1));
				}
			}
			if (F.Legend != EFVLegend::None && Game)
			{
				// update 5: the preparation with the current tackle (the tooltip is text only; the panel shows the icons)
				const TArray<FFVLegendCheckItem> Check = FVDB::LegendChecklist(F, Game->MakeCatchContext(0.f));
				if (Check.Num() == 0) { Body += TEXT("\nПодготовка: не нужна"); }
				for (const FFVLegendCheckItem& It : Check)
				{
					Body += FString::Printf(TEXT("\n%s %s"), It.bOk ? TEXT("Есть:") : TEXT("Нет:"), *CheckLabel(It));
				}
			}
			D.Tooltip(bKnown ? F.Name : FString(TEXT("???")), Body, bKnown ? FVDB::RarityColor(F.Rarity) : FVPal::InkSoft());
		}

		// ------------------------------------------------------------------------------------
		// Landscape (desktop): the classic side panel
		// ------------------------------------------------------------------------------------
		/** One page of the landscape details pager: place Loc drawn into panel SideR (local coords of the page). */
		void DrawSide(FVDraw& D, UFVSaveGame* P, const FFVLocationDef& Loc, const FFVRect& SideR, FName& OutTravel)
		{
			D.Panel(SideR, EFVPanel::Parchment);
			if (FVDB::IsHomeId(Loc.Id))
			{
				const FFVLocationDef& Home = FVDB::HomeLocation();
				DrawHomeInfo(D, P, FFVRect(SideR.X, SideR.Y, SideR.W, SideR.H - 32.f), false);
				const FMapAction A = ActionFor(P, Home);
				const FFVRect Btn(SideR.X + 7.f, SideR.Bottom() - 26.f, SideR.W - 14.f, 21.f);
				if (D.Button(Btn, A.Label, A.Kind == EMapAction::Travel ? EFVButton::Primary : EFVButton::Normal, A.bEnabled, 9.f) && A.bEnabled)
				{
					RunAction(P, Home, A, OutTravel);
				}
				return;
			}
			const FFVLocationDef* L = &Loc;
			const float X = SideR.X + 7.f;
			const float W = SideR.W - 14.f;
			float Y = SideR.Y + 5.f;
			const bool bUnl = IsOpenOnMap(P, *L);
			const bool bCur = P ? P->CurrentLocation == L->Id : false;
			const int32 Level = P ? P->GetLevel() : 1;

			D.Rect(FFVRect(SideR.X + 1.f, SideR.Y + 1.f, SideR.W - 2.f, 3.f), FVUI::ThemeColor(L->Theme));
			D.Text(D.Ellipsize(L->Name, W, 11.f), FVector2D(X, Y), 11.f, FVPal::Ink(), EFVAlign::Left, true);
			Y += 17.f;
			if (bCur) { D.Text(TEXT("Вы здесь"), FVector2D(X, Y), 7.f, FV::Col(60, 130, 40)); }
			else if (bUnl) { D.Text(TEXT("Открыто"), FVector2D(X, Y), 7.f, FV::Col(60, 130, 40)); }
			else { D.Text(TEXT("Закрыто"), FVector2D(X, Y), 7.f, FV::Col(180, 50, 40)); }
			Y += 12.f;
			Y += D.TextWrapped(L->Description, FVector2D(X, Y), W, 7.f, FVPal::InkSoft(), false, 1.05f) + 3.f;
			D.Divider(FVector2D(X, Y), W);
			Y += 5.f;

			// species progress
			const TArray<const FFVFishDef*> Fish = FVDB::FishAt(L->Id);
			const int32 Caught = P ? P->SpeciesCaughtAt(L->Id) : 0;
			if (Fish.Num() == 0)
			{
				// update 6: a place without water (the forest) — no empty «0 / 0» grid
				Y += D.TextWrapped(NoFishText(), FVector2D(X, Y), W, 7.f, FVPal::InkSoft(), false, 1.05f) + 4.f;
			}
			else
			{
			D.Text(FString::Printf(TEXT("Поймано видов: %d / %d"), Caught, Fish.Num()), FVector2D(X, Y), 8.f, FVPal::Ink(), EFVAlign::Left, false, true);
			if (P && Fish.Num() > 0 && P->IsLocationComplete(L->Id))
			{
				if (const FFVSprite* Tr = FVUI::Icon(EFVIcon::Trophy))
				{
					D.SpriteCentered(*Tr, FVector2D(X + W - 8.f, Y + 5.f), FMath::Min(1.f, 12.f / FMath::Max(1.f, Tr->H)));
				}
			}
			Y += 13.f;
			D.Bar(FFVRect(X, Y, W, 5.f), Fish.Num() > 0 ? float(Caught) / float(Fish.Num()) : 0.f, FVUI::ThemeColor(L->Theme));
			Y += 9.f;

			// fish grid
			const int32 Cols = 4;
			const float CellW = FMath::FloorToFloat((W + 2.f) / Cols) - 2.f;
			const float CellH = 22.f;
			UFVSprites* Sp = UFVSprites::Get();
			const FFVFishDef* HoverFish = nullptr;
			bool bHoverKnown = false;
			for (int32 i = 0; i < Fish.Num() && i < 12; ++i)
			{
				const FFVFishDef* F = Fish[i];
				if (!F) { continue; }
				const FFVRect Cell(X + (i % Cols) * (CellW + 2.f), Y + (i / Cols) * (CellH + 2.f), CellW, CellH);
				const FFVFishRecord* Rec = P ? P->GetRecord(F->Id) : nullptr;
				const bool bKnown = Rec && Rec->Caught > 0;
				D.Rect(Cell, F->bLegendary ? FV::Col(214, 160, 40) : FV::Col(214, 156, 94));
				D.Rect(Cell.Inset(1.f), bKnown ? FV::Col(255, 236, 196) : FV::Col(236, 206, 156));
				if (Sp)
				{
					const FFVSprite& S = bKnown ? Sp->Fish(*F) : Sp->FishSilhouette(*F);
					FVUI::SpriteFit(D, S, Cell.Inset(2.f), bKnown ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.75f), 1.f);
				}
				if (D.Hover(Cell))
				{
					D.Frame(Cell, FV::Col(236, 150, 60), 1.f);
					HoverFish = F;
					bHoverKnown = bKnown;
				}
			}
			if (HoverFish) { FishTooltip(D, *HoverFish, bHoverKnown, P); }
			const int32 Rows = FMath::DivideAndRoundUp(FMath::Min(Fish.Num(), 12), Cols);
			Y += Rows * (CellH + 2.f) + 3.f;
			}

			// requirements / info
			const float BtnY = SideR.Bottom() - 26.f;
			const FFVRect Btn(X, BtnY, W, 21.f);
			const FMapAction A = ActionFor(P, *L);
			if (!bUnl)
			{
				D.Divider(FVector2D(X, Y), W);
				Y += 5.f;
				const bool bLevelOk = Level >= L->MinLevel;
				const bool bMoneyOk = P ? P->CanAfford(L->UnlockPrice) : false;
				D.Text(TEXT("Нужен уровень:"), FVector2D(X, Y), 7.5f, FVPal::InkSoft(), EFVAlign::Left, false, false);
				D.Text(FString::Printf(TEXT("%d"), L->MinLevel), FVector2D(X + W, Y), 8.f, bLevelOk ? FV::Col(60, 130, 40) : FV::Col(190, 50, 40), EFVAlign::Right);
				Y += 12.f;
				if (IsU6Place(L->Id))
				{
					bool bCondOk = false;
					const FString Cond = U6ConditionText(P, *L, bCondOk);
					Y += D.TextWrapped(Cond, FVector2D(X, Y), W, 7.5f, bCondOk ? FV::Col(60, 130, 40) : FV::Col(190, 50, 40), true) + 2.f;
				}
				else
				{
					D.Text(TEXT("Стоимость:"), FVector2D(X, Y), 7.5f, FVPal::InkSoft(), EFVAlign::Left, false, false);
					D.MoneyLabel(FVector2D(X + W, Y), L->UnlockPrice, 8.f, EFVAlign::Right, bMoneyOk ? FV::Col(60, 130, 40) : FV::Col(190, 50, 40));
					Y += 12.f;
				}
			}
			else if (Fish.Num() > 0 && Y < BtnY - 30.f)
			{
				D.Divider(FVector2D(X, Y), W);
				Y += 5.f;
				// update 5: the legend of this place (not caught yet) — its preparation with the current tackle, as icons
				const FFVFishDef* Leg = nullptr;
				for (const FFVFishDef* F : Fish) { if (F && F->Legend != EFVLegend::None) { Leg = F; } }
				const FFVFishRecord* LegRec = (Leg && P) ? P->GetRecord(Leg->Id) : nullptr;
				const TArray<FFVLegendCheckItem> Check = (Leg && Game && !(LegRec && LegRec->Caught > 0))
					? FVDB::LegendChecklist(*Leg, Game->MakeCatchContext(0.f)) : TArray<FFVLegendCheckItem>();
				if (Check.Num() > 0)
				{
					D.Text(TEXT("Легенда места — подготовка:"), FVector2D(X, Y), 7.f, FVPal::InkSoft(), EFVAlign::Left, false, false);
					Y += 10.f;
					DrawLegendChecklist(D, Check, X, Y, W, 7.f, BtnY - 3.f);
				}
				else
				{
					D.TextWrapped(TEXT("Клёв зависит от времени суток и погоды. Редкая рыба держится глубже — забрасывайте дальше."),
						FVector2D(X, Y), W, 6.5f, FV::Alpha(FVPal::InkSoft(), 0.85f), false, 1.05f);
				}
			}
			const bool bPrimary = A.Kind == EMapAction::Travel || A.Kind == EMapAction::Unlock || A.Kind == EMapAction::NeedLevel;
			if (D.Button(Btn, A.Label, bPrimary ? EFVButton::Primary : EFVButton::Normal, A.bEnabled, 9.f) && A.bEnabled)
			{
				RunAction(P, *L, A, OutTravel);
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeMapScreen(UFVGame* Game)
{
	return MakeUnique<FVMapScreenImpl::FVMapScreen>(Game);
}
