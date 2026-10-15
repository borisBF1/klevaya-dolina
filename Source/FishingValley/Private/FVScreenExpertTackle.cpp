// UPDATE 6 [SCREEN-TACKLE] — expert rod configuration and saved configurations (Docs/UPDATE6_FOREST_CITY.md §10.4, §10.15). Owner: SCREEN-TACKLE.
// Context = expert rod id (None = the equipped expert rod, else the first owned one). Rod chips, the three slots
// Леска / Поплавок / Катушка (InstallExpertTackle / RemoveExpertTackle), the characteristics Сила / Размер зоны /
// Прочность / Устойчивость (GetExpertStats) with the difference of the chosen candidate, the bait, and saved
// configurations applied in one tap (SaveConfig / ApplyConfig / DeleteConfig). Fish conditions are never hinted (Р33).
// Classic tackle never goes on an expert rod and vice versa: only expert kinds are listed.
// Р77: a tap on a saved configuration whose text does not fit opens it (both lines wrapped in full, one open at a time);
// the context "fv_expand" (screenshot test) opens the first such row.

#include "FVScreens.h"
#include "FVScreenHome.h"
#include "FVGame.h"
#include "FVDraw.h"
#include "FVDatabase.h"
#include "FVSaveGame.h"

namespace FVScreenExpertTackleImpl
{
	FV_HOME_UI_USING();

	float SmallS() { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }
	float BodyS() { return FV::IsPortrait() ? FV::Ui::Body() : 8.f; }
	float HeadS() { return FV::IsPortrait() ? 11.f : 9.f; }

	constexpr int32 NumSlots = 3;
	const TCHAR* SlotName(int32 S) { return S == 0 ? TEXT("Леска") : (S == 1 ? TEXT("Поплавок") : TEXT("Катушка")); }
	EFVItemKind SlotKind(int32 S) { return S == 0 ? EFVItemKind::Line : (S == 1 ? EFVItemKind::Float : EFVItemKind::Reel); }
	EFVIcon SlotIcon(int32 S) { return S == 0 ? EFVIcon::Line : (S == 1 ? EFVIcon::Float : EFVIcon::Rod); }

	/** Which expert slot an item fits (-1 = not expert tackle: classic line / float never go here). */
	int32 SlotOfItem(FName ItemId)
	{
		if (FVDBU6::LineKindOf(ItemId) != EFVExpertLine::None) { return 0; }
		if (FVDBU6::FloatKindOf(ItemId) != EFVExpertFloat::None) { return 1; }
		if (FVDBU6::ReelKindOf(ItemId) != EFVExpertReel::None) { return 2; }
		const FFVItemDef* It = FVDB::FindItem(ItemId);
		return (It && It->Kind == EFVItemKind::Reel) ? 2 : -1;
	}

	FName SlotItem(const FFVExpertLoadout& L, int32 S) { return S == 0 ? L.Line : (S == 1 ? L.Float : L.Reel); }

	FString ItemTitle(FName Id)
	{
		if (Id.IsNone()) { return TEXT("— пусто —"); }
		const FFVItemDef* It = FVDB::FindItem(Id);
		return It ? It->Name : FVDB::ItemName(Id);
	}

	float StatOf(const FFVExpertStats& St, int32 i)
	{
		switch (i)
		{
		case 0: return St.Force;
		case 1: return St.ZoneSize;
		case 2: return St.LineStrength;
		default: return St.Stability;
		}
	}
	const TCHAR* StatName(int32 i)
	{
		static const TCHAR* N[] = { TEXT("Сила"), TEXT("Размер зоны"), TEXT("Прочность"), TEXT("Устойчивость") };
		return N[FMath::Clamp(i, 0, 3)];
	}

	/** Tiny pixel chevron at the bottom-right of a row whose text can be opened (pointing down) / is open (up). */
	void ExpandHint(FVDraw& D, float Right, float Bottom, bool bOpen)
	{
		const int32 N = FV::IsPortrait() ? 4 : 3;   // pixel rows; the widest one is 2N-1 px
		const float Left = Right - float(2 * N - 1);
		const float Top = Bottom - float(N);
		for (int32 r = 0; r < N; ++r)
		{
			const int32 Half = bOpen ? r : N - 1 - r;
			D.Rect(FFVRect(Left + float(N - 1 - Half), Top + float(r), float(2 * Half + 1), 1.f), FVPal::InkSoft());
		}
	}

	class FExpertTackleScreen final : public FVScreen
	{
	public:
		explicit FExpertTackleScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			RodId = Game ? Game->GetScreenContext() : NAME_None;
			bAutoExpand = RodId == FName(TEXT("fv_expand"));   // screenshot test: open the first saved set that does not fit
			if (bAutoExpand) { RodId = NAME_None; }
			ExpandedCfg = -1;
			ExpandedName.Empty();
			ShowCfgFrames = 0;
			ColL.Reset();
			ColR.Reset();
			Slot = 0;
			Cand = NAME_None;
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = FV::WindowRect();
			bool bClose = D.Window(Win, TEXT("Оснастка эксперта"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Act = EAct::None;
			if (P)
			{
				ResolveRod(*P);
				const float Top = Win.Y + (bP ? 16.f : 10.f);
				const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 8.f : Win.Bottom() - 10.f;
				const FFVRect Area(Win.X + 10.f, Top, Win.W - 20.f, FMath::Max(40.f, Bottom - Top));
				if (RodId.IsNone())
				{
					D.Panel(Area, EFVPanel::Parchment);
					EmptyState(D, Area, EFVIcon::ExpertRod, TEXT("У вас пока нет экспертной удочки. Их продаёт снастёвщик Кузьма в Городе, а самые сильные куют в «Закалочной кузне»."), BodyS());
				}
				else if (bP)
				{
					D.Panel(Area, EFVPanel::Parchment);
					const FFVRect In = Area.Inset(5.f);
					const float W = In.W - ColL.BarRoom() - 4.f;
					float Y = ColL.Begin(D, In, 4.f);
					Y = DrawRodPart(D, *P, In.X + 2.f, Y, W);
					Y = DrawStatsPart(D, *P, In.X + 2.f, Y + 4.f, W);
					Y = DrawBaitPart(D, *P, In.X + 2.f, Y + 4.f, W);
					Y = DrawConfigsPart(D, *P, In.X + 2.f, Y + 4.f, W);
					ColL.End(D, Y, 6.f);
				}
				else
				{
					const FFVRect Left(Area.X, Area.Y, 318.f, Area.H);
					const FFVRect Right(Left.Right() + 6.f, Area.Y, Area.Right() - Left.Right() - 6.f, Area.H);
					D.Panel(Left, EFVPanel::Parchment);
					D.Panel(Right, EFVPanel::Parchment);
					{
						const FFVRect In = Left.Inset(5.f);
						float Y = ColL.Begin(D, In, 3.f);
						Y = DrawRodPart(D, *P, In.X + 2.f, Y, In.W - ColL.BarRoom() - 4.f);
						ColL.End(D, Y, 6.f);
					}
					{
						const FFVRect In = Right.Inset(5.f);
						const float W = In.W - ColR.BarRoom() - 4.f;
						float Y = ColR.Begin(D, In, 3.f);
						Y = DrawStatsPart(D, *P, In.X + 2.f, Y, W);
						Y = DrawBaitPart(D, *P, In.X + 2.f, Y + 4.f, W);
						Y = DrawConfigsPart(D, *P, In.X + 2.f, Y + 4.f, W);
						ColR.End(D, Y, 6.f);
					}
				}
			}
			if (bP && D.ActionBar(Win)) { bClose = true; }
			if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); return; }
			if (P) { RunAct(*P); }
		}

	private:
		enum class EAct : uint8 { None, SelectRod, EquipRod, Install, Remove, Bait, Save, Apply, Delete };
		FName RodId;
		int32 Slot = 0;          // the slot whose candidates are listed
		FName Cand;              // highlighted candidate (stat difference preview)
		FScrollColumn ColL, ColR;
		EAct Act = EAct::None;
		FName ActId;
		int32 ActIndex = -1;
		int32 ExpandedCfg = -1;      // the saved configuration opened by a tap (-1 = none)
		FString ExpandedName;        // its name (a deletion shifts the indices: another row must not open by itself)
		bool bShowInstant = false;   // scroll to the opened row at once (the screenshot test) instead of gliding
		int32 ShowCfgFrames = 0;     // frames left to keep the opened row scrolled into view (the scroll range follows a frame late)
		bool bAutoExpand = false;    // context "fv_expand": open the first row that does not fit

		void Want(EAct A, FName Id = NAME_None, int32 Index = -1) { if (Act == EAct::None) { Act = A; ActId = Id; ActIndex = Index; } }

		static bool IsExpertRod(FName Id)
		{
			const FFVRodDef* R = FVDB::FindRod(Id);
			return R && R->bExpert;
		}

		TArray<FName> OwnedExpertRods(const UFVSaveGame& P) const
		{
			TArray<FName> Out;
			for (const FName& Id : P.OwnedRods) { if (IsExpertRod(Id)) { Out.Add(Id); } }
			return Out;
		}

		void ResolveRod(const UFVSaveGame& P)
		{
			const TArray<FName> Rods = OwnedExpertRods(P);
			if (Rods.Contains(RodId)) { return; }
			RodId = Rods.Contains(P.EquippedRod) ? P.EquippedRod : (Rods.Num() > 0 ? Rods[0] : NAME_None);
		}

		/** Candidate items for a slot from the backpack (unique ids). */
		TArray<FName> Candidates(const UFVSaveGame& P, int32 S) const
		{
			TArray<FName> Out;
			for (const FFVInvStack& St : P.Backpack)
			{
				if (St.Count > 0 && SlotOfItem(St.ItemId) == S) { Out.AddUnique(St.ItemId); }
			}
			return Out;
		}

		// ---------------------------------------------------------------------------------------- rod + slots + candidates
		float DrawRodPart(FVDraw& D, UFVSaveGame& P, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			const FFVRodDef* Rod = FVDB::FindRod(RodId);
			// rod chips
			const TArray<FName> Rods = OwnedExpertRods(P);
			{
				const float ChipH = bP ? 34.f : 20.f;
				const float Gap = 4.f;
				float CX = X;
				for (const FName& Id : Rods)
				{
					const FFVRodDef* R = FVDB::FindRod(Id);
					if (!R) { continue; }
					// 0.6.2 (Р84): «Бриз» · Маятник — the type on every rod chip (all of them are «Удочка «…»»)
					FString Short = R->Name;
					Short.RemoveFromStart(TEXT("Удочка "));
					const FString Label = FString::Printf(TEXT("%s · %s"), *Short, *FVDBU6::RodTypeLabel(*R));
					const float CW = FMath::Min(W, D.MeasureText(Label, BodyS()).X + 22.f);
					if (CX + CW > X + W + 0.5f) { CX = X; Y += ChipH + Gap; }
					if (D.Button(FFVRect(CX, Y, CW, ChipH), D.Ellipsize(Label, CW - 12.f, BodyS()), Id == RodId ? EFVButton::Primary : EFVButton::Normal, true, BodyS()) && Id != RodId)
					{
						Want(EAct::SelectRod, Id);
					}
					CX += CW + Gap;
				}
				Y += ChipH + 6.f;
			}
			if (!Rod) { return Y; }
			// rod line: name + type plate (0.6.2 Р84) + equip
			{
				const bool bEquipped = P.EquippedRod == RodId;
				const float BW = bP ? 110.f : 86.f, BH = bP ? 32.f : 18.f;
				const float ChipS = SmallS();
				const float ChipW = FMath::RoundToFloat(D.MeasureText(FVDBU6::RodTypeLabel(*Rod), ChipS, true).X + 8.f);
				const FString Name = D.Ellipsize(Rod->Name, FMath::Max(20.f, W - BW - 6.f - ChipW - 5.f), BodyS());
				const float NameW = D.MeasureText(Name, BodyS()).X;
				D.Text(Name, FVector2D(X, Y + (BH - D.LineHeight(BodyS())) * 0.5f), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
				const float ChipH = FMath::RoundToFloat(D.MeasureText(FVDBU6::RodTypeLabel(*Rod), ChipS, true).Y + 3.f);
				FVUI::RodTypeChip(D, *Rod, FVector2D(X + NameW + 5.f, Y + FMath::RoundToFloat((BH - ChipH) * 0.5f)), ChipS);
				if (D.Button(FFVRect(X + W - BW, Y, BW, BH), bEquipped ? TEXT("В руках") : TEXT("Взять в руки"), bEquipped ? EFVButton::Normal : EFVButton::Primary, !bEquipped, SmallS() + 0.5f))
				{
					Want(EAct::EquipRod, RodId);
				}
				Y += BH + 2.f;
				if (Rod->ExpertType < EFVExpertRodType::Count)
				{
					Y += D.TextWrapped(FVDBU6::RodTypeHowTo(Rod->ExpertType), FVector2D(X, Y), W, SmallS(), FVPal::InkSoft()) + 4.f;   // in full: the column scrolls
				}
			}
			// the three slots
			const FFVExpertLoadout L = P.GetExpertLoadout(RodId);
			Y = FVUI::SectionHeader(D, TEXT("Снасти"), FVector2D(X, Y), W, HeadS()) + 2.f;
			const float RowH = bP ? 44.f : 26.f;
			for (int32 s = 0; s < NumSlots; ++s)
			{
				const FFVRect Row(X, Y, W, RowH);
				const FName Cur = SlotItem(L, s);
				const bool bSel = s == Slot;
				const bool bHover = !bP && D.Hover(Row);
				RowBack(D, Row, bSel, bHover, PressingIn(D, Row));
				const float IconS = bP ? 30.f : 18.f;
				const FFVRect IconR(Row.X + 5.f, FMath::RoundToFloat(Row.Center().Y - IconS * 0.5f), IconS, IconS);
				if (Cur.IsNone()) { DrawIcon(D, SlotIcon(s), IconR, 1.f, FLinearColor(1.f, 1.f, 1.f, 0.35f)); }
				else { DrawItemIcon(D, Cur, IconR, 2.f); }
				const float BW = bP ? 76.f : 52.f, BH = bP ? 30.f : 18.f;
				const FFVRect BR(Row.Right() - BW - 4.f, FMath::RoundToFloat(Row.Center().Y - BH * 0.5f), BW, BH);
				const float TX = IconR.Right() + 6.f;
				const FString T = FString::Printf(TEXT("%s: %s"), SlotName(s), *ItemTitle(Cur));
				D.Text(D.Ellipsize(T, BR.X - 6.f - TX, BodyS()), FVector2D(TX, FMath::RoundToFloat(Row.Center().Y - D.LineHeight(BodyS()) * 0.5f)), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
				bool bButton = false;
				if (!Cur.IsNone())
				{
					if (D.Button(BR, TEXT("Снять"), EFVButton::Normal, true, SmallS() + 0.5f)) { Want(EAct::Remove, NAME_None, s); bButton = true; }
				}
				if (!bButton && !BR.Contains(D.Mouse()) && D.Clicked(Row) && s != Slot) { Slot = s; Cand = NAME_None; FVUI::Sfx(EFVSfx::Click); }
				Y += RowH + 3.f;
			}
			// candidates of the selected slot
			Y += 3.f;
			Y = FVUI::SectionHeader(D, FString::Printf(TEXT("%s: что есть в рюкзаке"), SlotName(Slot)), FVector2D(X, Y), W, HeadS()) + 2.f;
			const TArray<FName> Cands = Candidates(P, Slot);
			if (Cands.Num() == 0)
			{
				Y += D.TextWrapped(Slot == 2 ? TEXT("Катушек нет. Их куют в кузне во дворе.")
					: TEXT("Нет подходящих снастей. Экспертные снасти продаёт снастёвщик Кузьма в Городе."), FVector2D(X, Y), W, SmallS(), FVPal::InkSoft()) + 6.f;
				return Y;
			}
			for (const FName& Id : Cands)
			{
				const FFVRect Row(X, Y, W, RowH);
				const bool bSel = Id == Cand;
				RowBack(D, Row, bSel, !bP && D.Hover(Row), PressingIn(D, Row));
				const float IconS = bP ? 30.f : 18.f;
				const FFVRect IconR(Row.X + 5.f, FMath::RoundToFloat(Row.Center().Y - IconS * 0.5f), IconS, IconS);
				DrawItemIcon(D, Id, IconR, 2.f);
				const float BW = bP ? 96.f : 64.f, BH = bP ? 30.f : 18.f;
				const FFVRect BR(Row.Right() - BW - 4.f, FMath::RoundToFloat(Row.Center().Y - BH * 0.5f), BW, BH);
				const float TX = IconR.Right() + 6.f;
				const FString T = FString::Printf(TEXT("%s ×%d"), *ItemTitle(Id), P.CountItem(Id));
				D.Text(D.Ellipsize(T, BR.X - 6.f - TX, BodyS()), FVector2D(TX, FMath::RoundToFloat(Row.Center().Y - D.LineHeight(BodyS()) * 0.5f)), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true);
				bool bButton = false;
				if (D.Button(BR, TEXT("Поставить"), EFVButton::Primary, true, SmallS() + 0.5f)) { Want(EAct::Install, Id); bButton = true; }
				if (!bButton && !BR.Contains(D.Mouse()))
				{
					if (!bP && D.Hover(Row)) { Cand = Id; }
					if (D.Clicked(Row)) { Cand = bSel ? NAME_None : Id; FVUI::Sfx(EFVSfx::Click); }
				}
				Y += RowH + 3.f;
			}
			return Y + 4.f;
		}

		// ---------------------------------------------------------------------------------------- stats with the preview
		float DrawStatsPart(FVDraw& D, UFVSaveGame& P, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			Y = FVUI::SectionHeader(D, TEXT("Характеристики"), FVector2D(X, Y), W, HeadS()) + 2.f;
			const double Now = Game ? Game->GetAbsMinutes() : 0.0;
			const FFVExpertStats Cur = P.GetExpertStats(RodId, Now);
			const FFVExpertLoadout L = P.GetExpertLoadout(RodId);
			bool bPreview = false;
			FFVExpertStats Prev = Cur;
			if (!Cand.IsNone())
			{
				const int32 S = SlotOfItem(Cand);
				if (S >= 0)
				{
					const FFVExpertStats Base = FVDBU6::ComposeExpertStats(RodId, L.Line, L.Float, L.Reel);
					const FFVExpertStats With = FVDBU6::ComposeExpertStats(RodId, S == 0 ? Cand : L.Line, S == 1 ? Cand : L.Float, S == 2 ? Cand : L.Reel);
					auto Ratio = [](float A, float B) { return B > 0.0001f ? A / B : 1.f; };
					Prev.Force = Cur.Force * Ratio(With.Force, Base.Force);
					Prev.ZoneSize = Cur.ZoneSize * Ratio(With.ZoneSize, Base.ZoneSize);
					Prev.LineStrength = Cur.LineStrength * Ratio(With.LineStrength, Base.LineStrength);
					Prev.Stability = Cur.Stability * Ratio(With.Stability, Base.Stability);
					bPreview = true;
				}
			}
			const float NameW = bP ? 104.f : 74.f;
			const float ValW = bP ? 84.f : 62.f;
			const float BarH = bP ? 10.f : 7.f;
			const float LH = FMath::Max(D.LineHeight(SmallS() + 0.5f), BarH) + (bP ? 6.f : 3.f);
			for (int32 i = 0; i < 4; ++i)
			{
				const float V = StatOf(Cur, i);
				const float NV = StatOf(Prev, i);
				D.Text(StatName(i), FVector2D(X, Y), SmallS() + 0.5f, FVPal::Ink(), EFVAlign::Left, false, true);
				const FFVRect Bar(X + NameW, FMath::RoundToFloat(Y + (D.LineHeight(SmallS() + 0.5f) - BarH) * 0.5f), FMath::Max(20.f, W - NameW - ValW - 6.f), BarH);
				D.Bar(Bar, FMath::Clamp(V / 2.f, 0.f, 1.f), FV::Col(90, 160, 220));
				FString Val = FString::Printf(TEXT("×%s"), *FVUI::Num(V));
				FLinearColor C = FVPal::Ink();
				if (bPreview && !FMath::IsNearlyEqual(NV, V, 0.005f))
				{
					const float Dx = NV - V;
					Val += FString::Printf(TEXT(" %s%s"), Dx > 0.f ? TEXT("+") : TEXT("−"), *FVUI::Num(FMath::Abs(Dx)));
					C = Dx > 0.f ? InkGood() : InkBad();
					D.Frame(FFVRect(Bar.X + Bar.W * FMath::Clamp(NV / 2.f, 0.f, 1.f) - 1.f, Bar.Y - 1.f, 2.f, Bar.H + 2.f), C, 1.f);
				}
				D.Text(Val, FVector2D(X + W, Y), SmallS() + 0.5f, C, EFVAlign::Right, false, true);
				Y += LH;
			}
			if (bPreview)
			{
				Y += D.TextWrapped(FString::Printf(TEXT("Если поставить: %s"), *ItemTitle(Cand)), FVector2D(X, Y), W, SmallS(), InkInfo()) + 2.f;
			}
			else if (!bP)
			{
				D.Text(TEXT("Наведите на снасть — покажем разницу."), FVector2D(X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += D.LineHeight(SmallS(), false) + 2.f;
			}
			else
			{
				D.Text(TEXT("Коснитесь снасти в списке — покажем разницу."), FVector2D(X, Y), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += D.LineHeight(SmallS(), false) + 2.f;
			}
			return Y + 4.f;
		}

		// ---------------------------------------------------------------------------------------- bait
		float DrawBaitPart(FVDraw& D, UFVSaveGame& P, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			Y = FVUI::SectionHeader(D, TEXT("Наживка"), FVector2D(X, Y), W, HeadS()) + 2.f;
			TArray<FName> Baits;
			for (const TPair<FName, int32>& B : P.Baits) { if (B.Value > 0 && FVDB::FindBait(B.Key)) { Baits.Add(B.Key); } }
			Baits.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
			const float ChipH = bP ? 32.f : 18.f;
			const float Gap = 4.f;
			float CX = X;
			auto Chip = [&](FName Id, const FString& Label)
			{
				const float CW = FMath::Min(W, D.MeasureText(Label, SmallS() + 0.5f).X + 18.f);
				if (CX + CW > X + W + 0.5f) { CX = X; Y += ChipH + Gap; }
				const bool bOn = P.EquippedBait == Id;
				if (D.Button(FFVRect(CX, Y, CW, ChipH), Label, bOn ? EFVButton::Primary : EFVButton::Normal, true, SmallS() + 0.5f) && !bOn) { Want(EAct::Bait, Id); }
				CX += CW + Gap;
			};
			Chip(NAME_None, TEXT("Без наживки"));
			for (const FName& Id : Baits)
			{
				const FFVBaitDef* B = FVDB::FindBait(Id);
				const int32* N = P.Baits.Find(Id);
				Chip(Id, FString::Printf(TEXT("%s ×%d"), B ? *B->Name : *Id.ToString(), N ? *N : 0));
			}
			return Y + ChipH + 6.f;
		}

		// ---------------------------------------------------------------------------------------- saved configurations
		float DrawConfigsPart(FVDraw& D, UFVSaveGame& P, float X, float Y, float W)
		{
			const bool bP = FV::IsPortrait();
			Y = FVUI::SectionHeader(D, TEXT("Сохранённые наборы"), FVector2D(X, Y), W, HeadS()) + 2.f;
			const bool bEquippedHere = P.EquippedRod == RodId;
			const float BH = bP ? 34.f : 20.f;
			if (D.Button(FFVRect(X, Y, W, BH), bEquippedHere ? TEXT("Сохранить текущий набор") : TEXT("Сначала возьмите удочку в руки"), EFVButton::Primary, bEquippedHere, BodyS()))
			{
				Want(EAct::Save);
			}
			Y += BH + 4.f;
			const TArray<FFVSavedConfig>& Cfgs = P.U6.Configs;
			if (Cfgs.Num() == 0)
			{
				Y += D.TextWrapped(TEXT("Соберите удочку, снасти и наживку и сохраните набор — потом он ставится в одно касание прямо у воды."),
					FVector2D(X, Y), W, SmallS(), FVPal::InkSoft()) + 4.f;
				bAutoExpand = false;
				return Y;
			}
			if (!Cfgs.IsValidIndex(ExpandedCfg) || Cfgs[ExpandedCfg].Name != ExpandedName) { ExpandedCfg = -1; }
			FScrollColumn& Col = bP ? ColL : ColR;
			const float RowH = bP ? 52.f : 32.f;
			const float PadT = bP ? 5.f : 3.f;
			const float HintRoom = bP ? 7.f : 5.f;   // below the text of an open row: the chevron
			for (int32 i = 0; i < Cfgs.Num(); ++i)
			{
				const FFVSavedConfig& C = Cfgs[i];
				const float AW = bP ? 92.f : 64.f, DW = bP ? 40.f : 20.f, BtnH = bP ? 32.f : 18.f;
				const float TX = X + 6.f;
				const float TW = X + W - DW - 4.f - AW - 4.f - 6.f - TX;   // left of [Применить][×]
				const FFVRodDef* R = FVDB::FindRod(C.Rod);
				const FFVBaitDef* B = FVDB::FindBait(C.Bait);
				const FString Title = FString::Printf(TEXT("%s — %s"), *C.Name, R ? *R->Name : *C.Rod.ToString());
				const FString Sub = FString::Printf(TEXT("%s · %s · %s · %s"), *ItemTitle(C.Line), *ItemTitle(C.Float), *ItemTitle(C.Reel), B ? *B->Name : TEXT("без наживки"));
				const bool bTitleCut = D.MeasureText(Title, BodyS()).X > TW;
				const bool bSubCut = D.MeasureText(Sub, SmallS(), false).X > TW;
				const bool bCut = bTitleCut || bSubCut;
				if (bAutoExpand && bCut && ExpandedCfg < 0) { ExpandedCfg = i; ExpandedName = C.Name; ShowCfgFrames = 3; bShowInstant = true; }
				const bool bOpen = bCut && i == ExpandedCfg;
				// an open row grows to fit both lines wrapped in full; a closed one looks as before
				const float TitleH = bOpen && bTitleCut ? D.MeasureWrapped(Title, TW, BodyS(), true) : D.LineHeight(BodyS());
				const float SubH = bOpen && bSubCut ? D.MeasureWrapped(Sub, TW, SmallS()) : D.LineHeight(SmallS(), false);
				const float H = bOpen ? FMath::Max(RowH, FMath::CeilToFloat(PadT + TitleH + (bP ? 2.f : 0.f) + SubH + HintRoom + 2.f)) : RowH;
				const FFVRect Row(X, Y, W, H);
				D.Rect(Row, FV::Col(214, 162, 104));
				D.Rect(Row.Inset(1.f), RowFill());
				// the buttons keep their place when the row opens
				const FFVRect DelR(Row.Right() - DW - 4.f, FMath::RoundToFloat(Row.Y + (RowH - BtnH) * 0.5f), DW, BtnH);
				const FFVRect ApR(DelR.X - AW - 4.f, DelR.Y, AW, BtnH);
				float TY = Row.Y + PadT;
				if (bOpen && bTitleCut) { D.TextWrapped(Title, FVector2D(TX, TY), TW, BodyS(), FVPal::Ink(), true); }
				else { D.Text(D.Ellipsize(Title, TW, BodyS()), FVector2D(TX, TY), BodyS(), FVPal::Ink(), EFVAlign::Left, false, true); }
				TY += TitleH + (bP ? 2.f : 0.f);
				if (bOpen && bSubCut) { D.TextWrapped(Sub, FVector2D(TX, TY), TW, SmallS(), FVPal::InkSoft()); }
				else { D.Text(D.Ellipsize(Sub, TW, SmallS(), false), FVector2D(TX, TY), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false); }
				if (bCut) { ExpandHint(D, TX + TW, Row.Bottom() - (bP ? 4.f : 3.f), bOpen); }
				bool bButton = false;
				if (D.Button(ApR, TEXT("Применить"), EFVButton::Primary, true, SmallS() + 0.5f)) { Want(EAct::Apply, NAME_None, i); bButton = true; }
				if (D.Button(DelR, TEXT("×"), EFVButton::Danger, true, SmallS() + 0.5f)) { Want(EAct::Delete, NAME_None, i); bButton = true; }
				// a tap on the row itself (not on its buttons; a swipe never clicks) opens / closes the full text
				if (bCut && !bButton && !ApR.Contains(D.Mouse()) && !DelR.Contains(D.Mouse()) && D.Clicked(Row))
				{
					ExpandedCfg = bOpen ? -1 : i;
					ExpandedName = bOpen ? FString() : C.Name;
					ShowCfgFrames = bOpen ? 0 : 3;
					bShowInstant = false;
					FVUI::Sfx(EFVSfx::Click);
				}
				else if (bOpen && ShowCfgFrames > 0)
				{
					// keep the opened row in view (content coordinates of the column)
					--ShowCfgFrames;
					Col.View.ScrollToShow(Y - Col.Area.Y + Col.View.Offset(), FMath::Min(H, Col.Area.H), bShowInstant);
				}
				Y += H + 3.f;
			}
			bAutoExpand = false;
			return Y + 4.f;
		}

		// ---------------------------------------------------------------------------------------- actions
		void RunAct(UFVSaveGame& P)
		{
			const EAct A = Act;
			const FName Id = ActId;
			const int32 Index = ActIndex;
			Act = EAct::None;
			UFVGame* G = Game;
			if (A == EAct::None || !G) { return; }
			switch (A)
			{
			case EAct::SelectRod:
				RodId = Id;
				Cand = NAME_None;
				FVUI::Sfx(EFVSfx::Click);
				return;
			case EAct::EquipRod:
				if (!P.EquipRod(Id)) { Fail(G, EFVBuyResult::Invalid); return; }
				Success(G, FString::Printf(TEXT("В руках: %s"), *FVDB::ItemName(Id)), EFVSfx::Equip, EFVIcon::ExpertRod);
				Refresh(G);
				return;
			case EAct::Install:
			{
				const EFVBuyResult R = P.InstallExpertTackle(RodId, Id);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				Cand = NAME_None;
				Success(G, FString::Printf(TEXT("Поставлено: %s"), *ItemTitle(Id)), EFVSfx::Equip, SlotIcon(FMath::Max(0, SlotOfItem(Id))));
				Refresh(G);
				return;
			}
			case EAct::Remove:
			{
				const EFVBuyResult R = P.RemoveExpertTackle(RodId, SlotKind(FMath::Clamp(Index, 0, NumSlots - 1)));
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				Success(G, TEXT("Снасть убрана в рюкзак"), EFVSfx::Equip, EFVIcon::Backpack);
				Refresh(G);
				return;
			}
			case EAct::Bait:
				if (!P.EquipBait(Id)) { Fail(G, EFVBuyResult::Invalid); return; }
				FVUI::Sfx(EFVSfx::Equip);
				Refresh(G);
				return;
			case EAct::Save:
			{
				const int32 N = P.SaveConfig(FString::Printf(TEXT("Набор %d"), P.U6.Configs.Num() + 1));
				if (N < 0) { Fail(G, EFVBuyResult::Invalid); return; }
				Success(G, TEXT("Набор сохранён"), EFVSfx::Equip, EFVIcon::Check);
				Refresh(G);
				return;
			}
			case EAct::Apply:
			{
				if (!P.U6.Configs.IsValidIndex(Index)) { return; }
				const FName Rod = P.U6.Configs[Index].Rod;
				const EFVBuyResult R = P.ApplyConfig(Index);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				if (IsExpertRod(Rod)) { RodId = Rod; }
				Success(G, FString::Printf(TEXT("Набор «%s» готов"), *P.U6.Configs[Index].Name), EFVSfx::Equip, EFVIcon::ExpertRod);
				Refresh(G);
				return;
			}
			case EAct::Delete:
			{
				if (!P.U6.Configs.IsValidIndex(Index)) { return; }
				const FString Name = P.U6.Configs[Index].Name;
				TWeakObjectPtr<UFVGame> WeakG(G);
				G->Confirm(TEXT("Удалить набор?"), FString::Printf(TEXT("Набор «%s» будет удалён. Снасти останутся у вас."), *Name),
					[WeakG, Index, Name]()
					{
						UFVGame* GG = WeakG.Get();
						UFVSaveGame* PP = GG ? GG->GetProfile() : nullptr;
						if (!PP || !PP->U6.Configs.IsValidIndex(Index) || PP->U6.Configs[Index].Name != Name) { return; }
						PP->DeleteConfig(Index);
						FVHomeUI::Refresh(GG);
					}, TEXT("Удалить"), TEXT("Отмена"));
				return;
			}
			default:
				return;
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeExpertTackleScreen(UFVGame* Game)
{
	return MakeUnique<FVScreenExpertTackleImpl::FExpertTackleScreen>(Game);
}
