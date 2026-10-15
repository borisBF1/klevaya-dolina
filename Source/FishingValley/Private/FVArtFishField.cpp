// UPDATE 6 [ART-FISH-FIELD] — top-down fish of the expert field (every fish; expert fish and legends with their own look) (Docs/UPDATE6_FOREST_CITY.md §10.2, §10.10, §10.12). Owner: ART-FISH-FIELD.
// Contract: Public/FVSprites.h «UPDATE 6».
// Style: pixel art, Stardew-like shading via FVArt::Shade/Light, tinted 1px outline, deterministic, cached through
// UFVSprites::AddCached(MakeKey(FVSpriteCat::FieldFish, ...)).
//
// The fish is seen from above, HEAD TO THE RIGHT (FVExpertDraw rotates it by the heading). Frame 0..3 = tail swing
// (eels wave with the whole body). Built procedurally from a spine: a width profile per shape, tail fan, pectoral fins,
// eyes on both sides, a dark dorsal line, a highlight on the back and a soft baked shadow (+1,+2) for readability on
// the water of any theme. Body length >= 12 texels (classic small fish) .. 30 (expert legends); image adds tail, bill,
// fins, outline and shadow. Classic fish are drawn by form and colours (Р18); expert fish and legends get details.

#include "FVSprites.h"
#include "FVDatabase.h"

namespace FVArtFishFieldImpl
{
	enum class EKind : uint8 { Fish, Eel, Ray, Round, Flat, Squid, Craw };

	struct FProf
	{
		EKind Kind = EKind::Fish;
		float L = 14.f;        // body length (texels)
		float W = 2.5f;        // max half width
		float Peak = 0.6f;     // where the body is widest (0 tail .. 1 head)
		float TailL = 4.f;     // tail fan length
		float TailW = 3.f;     // tail fan half width at its end
		bool bFork = false;    // forked tail
		float Bill = 0.f;      // sword / spear length
		float Pect = 1.f;      // pectoral fin size multiplier (0 = none)
		bool bDorsal = false;  // a long dorsal fin drawn along the spine
		bool bWhiskers = false;
		bool bLeafy = false;   // sea dragon appendages
		bool bLegs = false;    // proteus
		bool bCrown = false;   // gold crest along the spine
		bool bLantern = false; // angler
		bool bSparkle = false;
		bool bPlates = false;  // stone plates (seams across)
		bool bBlind = false;
		uint8 Alpha = 255;
	};

	FProf ProfileOf(const FFVFishDef& Def)
	{
		FProf P;
		auto Set = [&P](EKind K, float L, float W, float TailL, float TailW, bool bFork)
		{
			P.Kind = K; P.L = L; P.W = W; P.TailL = TailL; P.TailW = TailW; P.bFork = bFork;
		};
		switch (Def.Shape)
		{
		case EFVFishShape::Minnow:   Set(EKind::Fish, 12, 2.2f, 3, 2.5f, true); break;
		case EFVFishShape::Perch:    Set(EKind::Fish, 14, 3.0f, 4, 3.f, false); P.bDorsal = true; break;
		case EFVFishShape::Carp:     Set(EKind::Fish, 16, 3.8f, 4, 3.5f, true); break;
		case EFVFishShape::Pike:     Set(EKind::Fish, 20, 2.6f, 4, 3.f, false); P.Peak = 0.45f; break;
		case EFVFishShape::Eel:      Set(EKind::Eel, 22, 1.6f, 2, 1.5f, false); P.Pect = 0.5f; break;
		case EFVFishShape::Flounder: Set(EKind::Flat, 16, 5.5f, 3, 3.f, false); P.Pect = 0.f; break;
		case EFVFishShape::Catfish:  Set(EKind::Fish, 18, 3.4f, 4, 3.f, false); P.Peak = 0.72f; P.bWhiskers = true; break;
		case EFVFishShape::Puffer:   Set(EKind::Round, 12, 4.8f, 3, 2.5f, false); break;
		case EFVFishShape::Tuna:     Set(EKind::Fish, 20, 4.0f, 5, 5.f, true); P.Peak = 0.55f; break;
		case EFVFishShape::Squid:    Set(EKind::Squid, 14, 2.8f, 5, 2.f, false); P.Pect = 0.f; break;
		case EFVFishShape::Crayfish: Set(EKind::Craw, 12, 2.6f, 3, 3.f, false); P.Pect = 0.f; break;
		case EFVFishShape::Legend:   Set(EKind::Fish, 24, 4.6f, 6, 5.f, true); break;
		case EFVFishShape::FlyingFish: Set(EKind::Fish, 16, 2.4f, 4, 3.5f, true); P.Pect = 2.4f; break;
		case EFVFishShape::Barracuda:  Set(EKind::Fish, 22, 2.4f, 5, 4.f, true); P.Peak = 0.5f; break;
		case EFVFishShape::Mackerel:   Set(EKind::Fish, 18, 2.8f, 5, 4.5f, true); P.Peak = 0.55f; break;
		case EFVFishShape::Grouper:    Set(EKind::Fish, 20, 5.0f, 4, 4.f, false); P.Peak = 0.62f; P.bDorsal = true; break;
		case EFVFishShape::Swordfish:  Set(EKind::Fish, 22, 3.4f, 5, 5.f, true); P.Bill = 7.f; break;
		case EFVFishShape::Ray:        Set(EKind::Ray, 16, 9.0f, 9, 0.5f, false); P.Pect = 0.f; break;
		case EFVFishShape::Sunfish:    Set(EKind::Round, 16, 4.0f, 2, 5.f, false); P.bDorsal = true; break;
		case EFVFishShape::Sailfish:   Set(EKind::Fish, 22, 3.0f, 5, 5.f, true); P.Bill = 6.f; P.bDorsal = true; break;
		case EFVFishShape::Moray:      Set(EKind::Eel, 24, 2.0f, 2, 1.5f, false); P.Pect = 0.f; break;
		case EFVFishShape::SeaDragon:  Set(EKind::Eel, 20, 1.8f, 3, 1.f, false); P.Pect = 0.f; P.bLeafy = true; P.Bill = 3.f; break;
		case EFVFishShape::Grayling:   Set(EKind::Fish, 16, 2.6f, 4, 3.f, true); P.bDorsal = true; break;
		case EFVFishShape::Proteus:    Set(EKind::Eel, 18, 1.5f, 3, 1.f, false); P.Pect = 0.f; P.bLegs = true; break;
		case EFVFishShape::Sturgeon:   Set(EKind::Fish, 24, 3.0f, 5, 4.f, true); P.Peak = 0.5f; P.bPlates = true; P.bWhiskers = true; break;
		case EFVFishShape::ExpertLegend: Set(EKind::Fish, 28, 4.6f, 6, 6.f, true); break;
		default: break;
		}
		switch (Def.ExpertLegend)
		{
		case EFVExpertLegend::KingMarlin:     Set(EKind::Fish, 28, 4.2f, 7, 7.f, true); P.Bill = 9.f; P.bCrown = true; P.bDorsal = true; break;
		case EFVExpertLegend::HarborMaster:   Set(EKind::Fish, 28, 7.0f, 6, 6.f, false); P.Peak = 0.62f; P.bDorsal = true; break;
		case EFVExpertLegend::GhostRay:       Set(EKind::Ray, 22, 13.f, 13, 0.5f, false); P.Pect = 0.f; P.Alpha = 200; break;
		case EFVExpertLegend::BlindPatriarch: Set(EKind::Fish, 30, 5.2f, 6, 5.f, false); P.Peak = 0.72f; P.bWhiskers = true; break;
		case EFVExpertLegend::StoneGiant:     Set(EKind::Fish, 28, 6.4f, 6, 6.f, false); P.Peak = 0.6f; P.bPlates = true; break;
		case EFVExpertLegend::CrystalDragon:  Set(EKind::Eel, 32, 3.2f, 5, 4.f, false); P.Pect = 1.4f; P.bCrown = true; P.bSparkle = true; P.Bill = 2.f; break;
		default: break;
		}
		switch (Def.Legend)
		{
		case EFVLegend::FogSerpent:   Set(EKind::Eel, 30, 2.6f, 4, 2.f, false); P.Pect = 0.f; P.Alpha = 225; break;
		case EFVLegend::Sturgeon:     Set(EKind::Fish, 30, 3.6f, 6, 5.f, true); P.Peak = 0.5f; P.bPlates = true; P.bWhiskers = true; break;
		case EFVLegend::Angler:       Set(EKind::Round, 22, 6.5f, 4, 4.f, false); P.bLantern = true; break;
		case EFVLegend::TsarCarp:     Set(EKind::Fish, 26, 6.0f, 5, 5.f, true); P.bCrown = true; break;
		case EFVLegend::CrystalTrout: P.bSparkle = true; break;
		case EFVLegend::ZharRyba:     P.bSparkle = true; P.bDorsal = true; break;
		case EFVLegend::MillMaster:   Set(EKind::Fish, 30, 5.8f, 5, 4.f, false); P.Peak = 0.75f; P.bWhiskers = true; P.bDorsal = true; break;   // §18 сом-водяной
		default: break;
		}
		if (Def.Pattern == EFVFishPattern::Shimmer) { P.bSparkle = true; }
		P.bBlind = Def.ExpertLegend == EFVExpertLegend::BlindPatriarch || Def.Shape == EFVFishShape::Proteus || Def.Id.ToString().Contains(TEXT("blind"));
		// expert fish are a bit bigger than the classic ones (§10.12: >= 24 px on screen anyway)
		if (Def.bExpert && Def.ExpertLegend == EFVExpertLegend::None) { P.L += 2.f; P.W += 0.3f; }
		return P;
	}

	float Profile(const FProf& P, float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		switch (P.Kind)
		{
		case EKind::Eel:   return T > 0.9f ? FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square((T - 0.9f) / 0.1f))) * 0.9f + 0.1f : 0.35f + 0.65f * FMath::Min(1.f, T / 0.35f);
		case EKind::Ray:   return T < 0.5f ? 0.08f + 0.92f * (T / 0.5f) : FMath::Max(0.1f, (1.f - T) / 0.5f);
		case EKind::Round:
		case EKind::Flat:  return FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square(2.f * T - 1.f)));
		case EKind::Squid: return T < 0.3f ? 0.5f + 0.5f * T / 0.3f : (T < 0.85f ? 1.f : FMath::Max(0.3f, (1.f - T) / 0.15f));
		default: break;
		}
		const float Pk = P.Peak;
		if (T < Pk) { return FMath::Max(0.2f, FMath::Pow(T / Pk, 0.8f)); }
		return FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square((T - Pk) / (1.f - Pk)))) * 0.9f + 0.1f;
	}

	FColor A(FColor C, int32 Al) { C.A = uint8(FMath::Clamp(Al, 0, 255)); return C; }

	uint32 Hash(const FFVFishDef& Def)
	{
		const FString S = Def.Id.ToString();
		uint32 H = 2166136261u;
		for (int32 I = 0; I < S.Len(); ++I) { H = (H ^ uint32(S[I])) * 16777619u; }
		return H;
	}

	void Tri(FFVImage& I, FVector2D P0, FVector2D P1, FVector2D P2, FColor C)
	{
		const int32 X0 = FMath::FloorToInt(FMath::Min3(P0.X, P1.X, P2.X)), X1 = FMath::CeilToInt(FMath::Max3(P0.X, P1.X, P2.X));
		const int32 Y0 = FMath::FloorToInt(FMath::Min3(P0.Y, P1.Y, P2.Y)), Y1 = FMath::CeilToInt(FMath::Max3(P0.Y, P1.Y, P2.Y));
		auto Edge = [](const FVector2D& A, const FVector2D& B, const FVector2D& Q) { return (B.X - A.X) * (Q.Y - A.Y) - (B.Y - A.Y) * (Q.X - A.X); };
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector2D Q(X + 0.5f, Y + 0.5f);
				const double E0 = Edge(P0, P1, Q), E1 = Edge(P1, P2, Q), E2 = Edge(P2, P0, Q);
				if ((E0 >= 0 && E1 >= 0 && E2 >= 0) || (E0 <= 0 && E1 <= 0 && E2 <= 0)) { I.Set(X, Y, C); }
			}
		}
	}

	FFVImage Build(const FFVFishDef& Def, int32 Frame)
	{
		const FProf P = ProfileOf(Def);
		const float Ph = float(Frame & 3) * (PI * 0.5f);
		const float Amp = P.Kind == EKind::Eel ? FMath::Max(1.2f, P.W * 0.9f) : (P.Kind == EKind::Ray ? 0.8f : FMath::Max(1.f, P.W * 0.45f));
		const float Side = FMath::Max3(P.W, P.TailW, P.W * (0.6f + 0.55f * P.Pect)) + Amp + (P.bWhiskers ? 2.f : 0.f) + (P.bLeafy ? 2.f : 0.f) + (P.bLegs ? 1.5f : 0.f);
		const int32 Mx = 3;   // outline + shadow margin
		const int32 W = FMath::CeilToInt(P.TailL + P.L + P.Bill + (P.bLantern ? 4.f : 0.f) + (P.Kind == EKind::Craw ? 4.f : 0.f)) + Mx * 2;
		const int32 H = FMath::CeilToInt(Side * 2.f) + Mx * 2 + 1;
		const float X0 = Mx + P.TailL;             // tail end of the body
		const float CY = H * 0.5f - 0.5f;
		FFVImage I(W, H);

		auto Spine = [&](float T) -> float
		{
			if (P.Kind == EKind::Eel) { return Amp * FMath::Sin(Ph + T * 2.f * PI * 1.1f) * (1.f - 0.75f * FMath::Clamp(T, 0.f, 1.f)); }
			const float K = FMath::Max(0.f, (0.55f - T) / 0.55f);
			return Amp * K * K * FMath::Sin(Ph);
		};

		const FColor Body = Def.BodyColor, Belly = Def.BellyColor, Fin = Def.FinColor, Pat = Def.PatternColor;
		const FColor FinD = FVArt::Shade(Fin, 0.3f);
		const uint32 Seed = Hash(Def);

		// ---- fins under the body
		// tail fan
		if (P.TailL > 0.f && P.Kind != EKind::Ray)
		{
			const float TB = -P.TailL / P.L;
			const float Sy0 = CY + Spine(0.f), Sy1 = CY + Spine(TB) + (P.Kind == EKind::Eel ? 0.f : Amp * 0.4f * FMath::Sin(Ph));
			const FVector2D Root(X0 + 0.5f, Sy0), E0(X0 - P.TailL, Sy1 - P.TailW), E1(X0 - P.TailL, Sy1 + P.TailW);
			Tri(I, Root + FVector2D(1.f, 0.f), E0, E1, Fin);
			if (P.bFork) { Tri(I, FVector2D(X0 - P.TailL * 0.45f, Sy1), E0 + FVector2D(-0.5f, P.TailW * 0.55f), E1 + FVector2D(-0.5f, -P.TailW * 0.55f), FColor(0, 0, 0, 0)); }
			// rays of the fan
			for (float Fy = -P.TailW + 1.f; Fy < P.TailW; Fy += 2.f) { I.Set(int32(X0 - P.TailL + 1.f), int32(Sy1 + Fy), FinD); }
		}
		// pectoral fins
		if (P.Pect > 0.f)
		{
			const float T = P.Kind == EKind::Round ? 0.55f : 0.66f;
			const float X = X0 + T * P.L, Cy = CY + Spine(T);
			const float Hw = P.W * Profile(P, T);
			const float Out = FMath::Max(1.5f, P.W * 0.55f * P.Pect), Back = FMath::Max(2.f, P.L * 0.12f * FMath::Sqrt(P.Pect));
			const float Sw = 0.6f * FMath::Sin(Ph + 1.f);
			Tri(I, FVector2D(X + 1.f, Cy - Hw + 0.5f), FVector2D(X - 1.f, Cy - Hw + 0.5f), FVector2D(X - Back, Cy - Hw - Out + Sw), Fin);
			Tri(I, FVector2D(X + 1.f, Cy + Hw - 0.5f), FVector2D(X - 1.f, Cy + Hw - 0.5f), FVector2D(X - Back, Cy + Hw + Out - Sw), Fin);
		}
		// sunfish / grouper side fins, sea-dragon leaves
		if (P.Kind == EKind::Round && Def.Shape == EFVFishShape::Sunfish)
		{
			const float X = X0 + 0.35f * P.L;
			Tri(I, FVector2D(X + 2.f, CY - P.W * 0.8f), FVector2D(X - 2.f, CY - P.W * 0.8f), FVector2D(X - 3.f, CY - P.W - 3.f), FinD);
			Tri(I, FVector2D(X + 2.f, CY + P.W * 0.8f), FVector2D(X - 2.f, CY + P.W * 0.8f), FVector2D(X - 3.f, CY + P.W + 3.f), FinD);
		}
		if (P.bLeafy)
		{
			for (float T = 0.15f; T < 0.9f; T += 0.18f)
			{
				const float X = X0 + T * P.L, Cy = CY + Spine(T), Hw = P.W * Profile(P, T);
				const float Sw = FMath::Sin(Ph + T * 6.f);
				Tri(I, FVector2D(X - 1.f, Cy - Hw), FVector2D(X + 1.f, Cy - Hw), FVector2D(X + Sw, Cy - Hw - 3.f), Fin);
				Tri(I, FVector2D(X - 1.f, Cy + Hw), FVector2D(X + 1.f, Cy + Hw), FVector2D(X - Sw, Cy + Hw + 3.f), Fin);
			}
		}

		// ---- body
		for (int32 X = 0; X < W; ++X)
		{
			const float T = (X + 0.5f - X0) / P.L;
			if (T < 0.f || T > 1.f) { continue; }
			const float Cy = CY + Spine(T);
			const float Hw = FMath::Max(0.6f, P.W * Profile(P, T));
			for (int32 Y = FMath::FloorToInt(Cy - Hw - 1.f); Y <= FMath::CeilToInt(Cy + Hw + 1.f); ++Y)
			{
				const float N = (Y + 0.5f - Cy) / Hw;
				if (FMath::Abs(N) > 1.f) { continue; }
				FColor C;
				const float AN = FMath::Abs(N);
				if (AN < 0.28f) { C = FVArt::Shade(Body, 0.22f); }                 // the back seen from above
				else if (AN < 0.72f) { C = Body; }
				else { C = FVArt::Mix(Body, Belly, 0.4f); }                        // flanks show the paler sides
				if (N < -0.55f) { C = FVArt::Light(C, 0.12f); }                     // light from the top-left
				else if (N > 0.6f) { C = FVArt::Shade(C, 0.12f); }
				const int32 BX = X - int32(X0);
				switch (Def.Pattern)
				{
				case EFVFishPattern::Stripes:
					if (T > 0.12f && T < 0.8f && (BX % 4) < (P.L >= 20.f ? 2 : 1) && AN < 0.9f) { C = FVArt::Mix(C, Pat, 0.8f); }
					break;
				case EFVFishPattern::Spots:
					if (T > 0.1f && T < 0.85f && FVArt::Noise(X, Y, Seed) < 0.16f) { C = FVArt::Mix(C, Pat, 0.85f); }
					break;
				case EFVFishPattern::Glow:
					if (AN > 0.7f) { C = FVArt::Mix(C, FVArt::Light(Pat, 0.3f), 0.7f); }
					else if (FVArt::Noise(X, Y, Seed) < 0.1f) { C = FVArt::Light(Pat, 0.5f); }
					break;
				case EFVFishPattern::Scales:
					if (T > 0.1f && T < 0.85f && ((BX + (Y % 2) * 2) % 4) == 0) { C = FVArt::Shade(FVArt::Mix(C, Pat, 0.25f), 0.15f); }
					break;
				case EFVFishPattern::Shimmer:
					if (FVArt::Noise(X, Y, Seed ^ 0x9e37u) < 0.14f) { C = FVArt::Mix(C, Pat, 0.75f); }
					break;
				default: break;
				}
				if (P.bPlates && T > 0.1f && T < 0.9f && (BX % 5) == 0) { C = FVArt::Shade(C, 0.25f); }
				I.Set(X, Y, C);
			}
		}
		// dorsal fin / crest along the spine
		if (P.bDorsal || P.bCrown)
		{
			for (float T = 0.25f; T < 0.7f; T += 0.5f / P.L)
			{
				const int32 X = int32(X0 + T * P.L), Y = int32(CY + Spine(T));
				I.Set(X, Y, P.bCrown ? FColor(255, 216, 84) : FVArt::Mix(FinD, Body, 0.3f));
			}
		}
		// highlight on the back near the head
		for (float T = 0.5f; T < 0.82f; T += 0.5f / P.L)
		{
			const float Hw = P.W * Profile(P, T);
			if (Hw < 1.5f) { continue; }
			I.Set(int32(X0 + T * P.L), int32(CY + Spine(T) - Hw * 0.45f), FVArt::Light(Body, 0.45f));
		}
		// squid tentacles / crayfish claws (under the outline, they are part of the silhouette)
		if (P.Kind == EKind::Squid)
		{
			for (int32 k = -2; k <= 2; ++k)
			{
				for (int32 d = 1; d <= int32(P.TailL); ++d)
				{
					I.Set(int32(X0) - d, int32(CY + k * 1.2f + FMath::Sin(Ph + d * 0.9f + k) * 0.7f), FVArt::Shade(Body, 0.15f));
				}
			}
		}
		if (P.Kind == EKind::Craw)
		{
			const float HX = X0 + P.L;
			for (int32 s = -1; s <= 1; s += 2)
			{
				Tri(I, FVector2D(HX - 2.f, CY + s * 1.5f), FVector2D(HX + 3.f, CY + s * (P.W + 1.5f)), FVector2D(HX + 1.f, CY + s * 0.8f), Fin);
			}
		}

		// ---- outline
		FColor OutC = FVArt::OutlineOf(Body);
		if (Def.Pattern == EFVFishPattern::Glow || Def.Pattern == EFVFishPattern::Shimmer) { OutC = FVArt::Mix(OutC, Pat, 0.3f); }
		I.Outline(OutC);

		// ---- details after the outline
		const float HeadT = P.Kind == EKind::Ray ? 0.8f : (P.Kind == EKind::Round ? 0.8f : 0.86f);
		{
			const float Ex = X0 + HeadT * P.L, Cy = CY + Spine(HeadT), Hw = FMath::Max(1.f, P.W * Profile(P, HeadT));
			const FColor Eye = P.bBlind ? FColor(206, 198, 206) : FColor(30, 22, 44);
			if (P.Kind == EKind::Flat)
			{
				I.Set(int32(Ex), int32(Cy - Hw * 0.35f), Eye); I.Set(int32(Ex) - 1, int32(Cy + Hw * 0.1f), Eye);
			}
			else
			{
				I.Set(int32(Ex), int32(FMath::Floor(Cy - Hw * 0.7f)), Eye);
				I.Set(int32(Ex), int32(FMath::Floor(Cy + Hw * 0.7f)), Eye);
			}
		}
		const float HeadX = X0 + P.L;
		const float HeadY = CY + Spine(1.f);
		if (P.Bill > 0.f)
		{
			const FColor BillC = FVArt::Light(Body, 0.35f);
			for (int32 d = 0; d < int32(P.Bill); ++d) { I.Set(int32(HeadX) + d, int32(HeadY), d == int32(P.Bill) - 1 ? FVArt::OutlineOf(Body) : BillC); }
		}
		if (P.bWhiskers)
		{
			const FColor Wc = FVArt::Mix(OutC, Body, 0.3f);
			const int32 Len = Def.ExpertLegend == EFVExpertLegend::BlindPatriarch ? 7 : 3;
			for (int32 s = -1; s <= 1; s += 2)
			{
				for (int32 d = 0; d < Len; ++d) { I.Set(int32(HeadX) - d / 2, int32(HeadY + s * (1.5f + d * 0.7f + 0.3f * FMath::Sin(Ph + d))), Wc); }
			}
		}
		if (P.bLegs)
		{
			for (float T : { 0.3f, 0.7f })
			{
				const float X = X0 + T * P.L, Cy = CY + Spine(T), Hw = P.W * Profile(P, T);
				I.Set(int32(X), int32(Cy - Hw - 1.f), FVArt::Shade(Body, 0.2f)); I.Set(int32(X) - 1, int32(Cy - Hw - 2.f), FVArt::Shade(Body, 0.2f));
				I.Set(int32(X), int32(Cy + Hw + 1.f), FVArt::Shade(Body, 0.2f)); I.Set(int32(X) - 1, int32(Cy + Hw + 2.f), FVArt::Shade(Body, 0.2f));
			}
			// red feathery gills behind the head
			const float Gx = X0 + 0.82f * P.L, Gy = CY + Spine(0.82f), Gw = P.W * Profile(P, 0.82f);
			for (int32 d = 0; d < 2; ++d) { I.Set(int32(Gx) - d, int32(Gy - Gw - 1.f - d), Fin); I.Set(int32(Gx) - d, int32(Gy + Gw + 1.f + d), Fin); }
		}
		if (P.bLantern)
		{
			const int32 LX = int32(HeadX) + 2, LY = int32(HeadY) - 1;
			I.Set(int32(HeadX), LY, OutC); I.Set(int32(HeadX) + 1, LY - 1, OutC);
			I.Set(LX, LY - 1, FColor(255, 250, 200)); I.Set(LX + 1, LY - 1, FVArt::Light(Pat, 0.5f));
			I.Set(LX, LY, FVArt::Light(Pat, 0.5f)); I.Set(LX + 1, LY, Pat);
		}
		if (Def.ExpertLegend == EFVExpertLegend::CrystalDragon)
		{
			// horns
			I.Set(int32(HeadX) - 2, int32(HeadY - P.W - 1.f), FColor(240, 250, 255)); I.Set(int32(HeadX) - 3, int32(HeadY - P.W - 2.f), FColor(240, 250, 255));
			I.Set(int32(HeadX) - 2, int32(HeadY + P.W + 1.f), FColor(240, 250, 255)); I.Set(int32(HeadX) - 3, int32(HeadY + P.W + 2.f), FColor(240, 250, 255));
		}
		if (Def.ExpertLegend == EFVExpertLegend::HarborMaster)
		{
			// shells on the back and the rusty hook in the lip
			FRandomStream R(int32(Seed & 0xffff));
			for (int32 k = 0; k < 5; ++k)
			{
				const float T = R.FRandRange(0.2f, 0.75f);
				I.Set(int32(X0 + T * P.L), int32(CY + Spine(T) + R.FRandRange(-2.f, 2.f)), FColor(226, 216, 192));
			}
			I.Set(int32(HeadX) + 1, int32(HeadY) + 1, FColor(178, 90, 50)); I.Set(int32(HeadX) + 2, int32(HeadY) + 1, FColor(178, 90, 50));
			I.Set(int32(HeadX) + 2, int32(HeadY), FColor(214, 130, 70));
		}
		if (Def.ExpertLegend == EFVExpertLegend::StoneGiant)
		{
			for (float T = 0.2f; T < 0.8f; T += 0.13f)
			{
				I.Set(int32(X0 + T * P.L), int32(CY + Spine(T) + ((int32(T * 40.f) & 1) ? 1 : -1)), FColor(255, 170, 60));   // amber cracks
			}
		}
		if (P.bSparkle)
		{
			FRandomStream R(int32(Seed & 0xffff) + Frame * 7);
			for (int32 k = 0; k < FMath::Max(2, int32(P.L / 6.f)); ++k)
			{
				const float T = R.FRandRange(0.15f, 0.85f);
				const int32 X = int32(X0 + T * P.L), Y = int32(CY + Spine(T) + R.FRandRange(-P.W * 0.6f, P.W * 0.6f));
				I.Set(X, Y, FColor(255, 255, 250));
			}
		}

		// ---- alpha (ghosts) and the baked shadow
		if (P.Alpha < 255) { for (FColor& C : I.Px) { if (C.A > 0) { C.A = FMath::Min(C.A, P.Alpha); } } }
		FFVImage Out(W, H);
		const FColor Sh(8, 24, 44, 70);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X) { if (I.Get(X, Y).A > 0) { Out.Set(X + 1, Y + 2, Sh); } }
		}
		Out.Blit(I, 0, 0);
		return Out;
	}
}

const FFVSprite& UFVSprites::FieldFish(const FFVFishDef& Def, int32 Frame)
{
	const int32 Fr = Frame & 3;
	const uint32 H = FVArtFishFieldImpl::Hash(Def);
	const uint64 Key = MakeKey(FVSpriteCat::FieldFish, H & 0xFFFFFF, uint32(Fr) | (uint32(Def.Shape) << 4) | ((H >> 24) << 12));
	if (const FFVSprite* S = FindCached(Key)) { return *S; }
	return AddCached(Key, BuildFieldFish(Def, Fr), FString::Printf(TEXT("FieldFish_%s_%d"), *Def.Id.ToString(), Fr));
}
FFVImage UFVSprites::BuildFieldFish(const FFVFishDef& Def, int32 Frame)
{
	return FVArtFishFieldImpl::Build(Def, Frame);
}
