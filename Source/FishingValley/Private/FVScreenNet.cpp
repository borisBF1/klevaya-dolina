// FVScreenNet.cpp — a net spot at the shore (screen context = "<locationId>:<spot>").
//   no net -> choose a net from the backpack (stats) -> «Поставить»;
//   net    -> durability bar, torn state, catch (icons), «Собрать улов», «Починить (верёвка 1, волокно 2)», «Снять».
// A small animated shore scene shows the posts and the net (its fill / torn look) in both orientations.
// Update 3: the net list and the catch grid scroll smoothly (FFVScrollView); clicks fire on a clean release.

#include "FVScreens.h"
#include "FVScreenHome.h"

namespace FVNetScreenImpl
{
	FV_HOME_UI_USING();

	static const FFVRect WinRect(10.f, 30.f, 620.f, 318.f);
	constexpr float RemoveMinFrac = 0.95f;   // spec §6: a net can be taken down only when durability >= 95%

	enum class EAct : uint8 { None, Place, Collect, Repair, Remove, Destroy };

	/** "верёвка 1, волокно 2" */
	static FString CostShort(const TArray<FFVIngredient>& Cost)
	{
		TArray<FString> Parts;
		for (const FFVIngredient& I : Cost) { Parts.Add(FString::Printf(TEXT("%s ×%d"), *LowerFirst(IngredientName(I.ItemId)), I.Count)); }
		return FString::Join(Parts, TEXT(", "));
	}

	/** Р49: "Ремонт: верёвка ×1 (есть 3), волокно ×2 (есть 0)" — a shortage is red (only while a repair is needed).
	 *  Wraps per ingredient; returns the height used (bDraw = false only measures). */
	static float RepairCostLine(FVDraw& D, const UFVSaveGame& P, const TArray<FFVIngredient>& Cost, float X, float Y, float W, float Size, bool bNeeded, bool bDraw = true)
	{
		const float LH = D.LineHeight(Size, false);
		const FString Head = bNeeded ? FString(TEXT("Ремонт: ")) : FString(TEXT("Ремонт (не нужен): "));
		if (bDraw) { D.Text(Head, FVector2D(X, Y), Size, FVPal::InkSoft(), EFVAlign::Left, false, true); }
		float CX = X + D.MeasureText(Head, Size, true).X;
		float CY = Y;
		for (int32 i = 0; i < Cost.Num(); ++i)
		{
			const int32 Have = P.CountIngredient(Cost[i].ItemId);
			const bool bShort = Have < Cost[i].Count;
			const FString Seg = FString::Printf(TEXT("%s ×%d (есть %d)%s"), *LowerFirst(IngredientName(Cost[i].ItemId)), Cost[i].Count, Have, i + 1 < Cost.Num() ? TEXT(", ") : TEXT(""));
			const float SW = D.MeasureText(Seg, Size, true).X;
			if (CX > X && CX + SW > X + W + 0.5f) { CX = X; CY += LH; }
			const FLinearColor C = (bNeeded && bShort) ? InkBad() : (bNeeded ? FVPal::Ink() : FVPal::InkSoft());
			if (bDraw) { D.Text(Seg, FVector2D(CX, CY), Size, C, EFVAlign::Left, false, true); }
			CX += SW;
		}
		return CY - Y + LH;
	}

	/** Height TextWrappedLimited() takes for S with no line cap (the same word wrap as MeasureWrapped, its own line step). */
	static float WrappedLimitedH(const FVDraw& D, const FString& S, float W, float Size, bool bBold = false)
	{
		if (S.IsEmpty()) { return 0.f; }
		const float One = D.MeasureWrapped(TEXT("Щ"), W, Size, bBold, 1.f);
		const int32 N = One > 0.f ? FMath::Max(1, FMath::RoundToInt(D.MeasureWrapped(S, W, Size, bBold, 1.f) / One)) : 1;
		return float(N) * FMath::RoundToFloat(D.LineHeight(Size, bBold) * 0.95f);
	}

	/** Parts packed «a · b · c» into as many lines as they need (FVUI::PackParts cuts after two lines); a part wider
	    than the line is word-wrapped. Draws when bDraw; returns the height used (line step = LineHeight(Size, false)). */
	static float DrawPartsAll(FVDraw& D, const TArray<FString>& Parts, float X, float Y, float W, float Size, const FLinearColor& C, bool bDraw = true)
	{
		const float LH = D.LineHeight(Size, false);
		float H = 0.f;
		FString Cur;
		auto Flush = [&]()
		{
			if (Cur.IsEmpty()) { return; }
			if (D.MeasureText(Cur, Size, false).X <= W)
			{
				if (bDraw) { D.Text(Cur, FVector2D(X, Y + H), Size, C, EFVAlign::Left, false, false); }
				H += LH;
			}
			else
			{
				H += bDraw ? TextWrappedLimited(D, Cur, FVector2D(X, Y + H), W, Size, C, 40) : WrappedLimitedH(D, Cur, W, Size);
			}
			Cur.Reset();
		};
		for (const FString& Part : Parts)
		{
			const FString Try = Cur.IsEmpty() ? Part : Cur + TEXT(" · ") + Part;
			if (Cur.IsEmpty() || D.MeasureText(Try, Size, false).X <= W) { Cur = Try; }
			else { Flush(); Cur = Part; }
		}
		Flush();
		return H;
	}

	static int32 CatchUnits(const FFVNetState& N)
	{
		int32 U = 0;
		for (const FFVInvStack& S : N.Catch) { U += FMath::Max(0, S.Count); }
		return U;
	}

	class FNetScreen final : public FVScreen
	{
	public:
		explicit FNetScreen(UFVGame* InGame) : FVScreen(InGame) {}

		virtual void OnOpen() override
		{
			TickCount = 0;
			const FString Ctx = Game ? Game->GetScreenContext().ToString() : FString();
			FString L, R;
			if (Ctx.Split(TEXT(":"), &L, &R))
			{
				LocId = FName(*L);
				Spot = FMath::Max(0, FCString::Atoi(*R));
			}
			else
			{
				LocId = FName(*Ctx);
				Spot = 0;
			}
		}

		virtual void Tick(float Dt, const FFVInput& In) override
		{
			++TickCount;
			if (TickCount <= 1 || !Game || Game->HasModal()) { return; }
			if (In.WasPressed(EKeys::Up)) { PendingMove = -1; }
			else if (In.WasPressed(EKeys::Down)) { PendingMove = 1; }
		}

		virtual void Draw(FVDraw& D) override
		{
			const bool bP = FV::IsPortrait();
			const FFVRect Win = bP ? FV::WindowRect() : WinRect;
			const FFVLocationDef* Loc = FVDB::FindLocation(LocId);
			bool bClose = D.Window(Win, TEXT("Рыболовная сеть"));
			UFVSaveGame* P = Game ? Game->GetProfile() : nullptr;
			Act = EAct::None;
			if (!P || !Loc)
			{
				D.Text(TEXT("Место для сети недоступно"), Win.Center() - FVector2D(0.f, 6.f), FV::Ui::Body(), FVPal::InkSoft(), EFVAlign::Center);
				if (bP && D.ActionBar(Win)) { bClose = true; }
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}
			// update 6 (Р35, Р36): no nets in the expert zones; a spot the artel uses is shown read-only
			const FFVNetState* ArtelNet = P->U6.ArtelNets.FindByPredicate([this](const FFVNetState& N) { return N.Location == LocId && N.Spot == Spot; });
			if (FVDBU6::IsExpertLocation(LocId) || (ArtelNet && !P->FindNet(LocId, Spot)))
			{
				DrawBlocked(D, *Loc, ArtelNet, Win, bClose);
				if (bClose && Game) { UFVGame* G = Game; G->CloseScreen(); }
				return;
			}
			const FFVNetState* Net = P->FindNet(LocId, Spot);
			if (Net) { DrawNet(D, *P, *Loc, *Net, Win, bClose); }
			else { DrawEmpty(D, *P, *Loc, Win, bClose); }
			if (bClose && Game)
			{
				UFVGame* G = Game;
				G->CloseScreen();
				return;
			}
			RunAct(*P);
		}

	private:
		FName LocId;
		int32 Spot = 0;
		int32 TickCount = 0;
		FFVScrollView NetList;       // nets in the backpack (smooth scrolling)
		FFVScrollView CatchScroll;   // the catch grid
		bool bEnsureSel = false;
		FName SelNet;
		int32 PendingMove = 0;
		EAct Act = EAct::None;
		FName ActId;

		float SmallS() const { return FV::IsPortrait() ? FV::Ui::Small() : 6.5f; }
		float BodyS() const { return FV::IsPortrait() ? FV::Ui::Body() : 7.5f; }

		void Want(EAct A, FName Id = NAME_None)
		{
			if (Act == EAct::None) { Act = A; ActId = Id; }
		}

		/** Update 6: «В экспертной зоне сетей нет» / «Занято артелью» (the artel net's state, managed in the City). */
		void DrawBlocked(FVDraw& D, const FFVLocationDef& Loc, const FFVNetState* ArtelNet, const FFVRect& Win, bool& bClose)
		{
			const bool bP = FV::IsPortrait();
			float Y = DrawHeader(D, Loc, Win) + 6.f;
			const float X = Win.X + 16.f;
			const float W = Win.W - 32.f;
			const float TS = BodyS() + (bP ? 1.f : 1.5f);
			if (!ArtelNet)
			{
				DrawIcon(D, EFVIcon::Artel, FFVRect(Win.Center().X - 16.f, Y + 6.f, 32.f, 32.f), 2.f);
				Y += 46.f;
				D.Text(TEXT("В экспертной зоне сетей нет"), FVector2D(Win.Center().X, Y), TS, FVPal::Ink(), EFVAlign::Center, false, true);
				Y += D.LineHeight(TS) + 4.f;
				D.TextWrapped(TEXT("В Городе и Пещере сети ставить нельзя. Отдайте сети Артельщику Никифору — он расставит их на пруду, реке, в горах, на причале и болоте."),
					FVector2D(X, Y), W, SmallS() + 0.5f, FVPal::InkSoft());
			}
			else
			{
				const FFVItemDef* Def = FVDB::FindItem(ArtelNet->NetItem);
				const float MaxDur = (Def && Def->NetDurability > 0.f) ? Def->NetDurability : 1.f;
				const float Frac = FMath::Clamp(ArtelNet->Durability / MaxDur, 0.f, 1.f);
				int32 Units = 0;
				for (const FFVInvStack& S : ArtelNet->Catch) { Units += FMath::Max(0, S.Count); }
				const FFVRect Ic(X, Y, 36.f, 36.f);
				IconBox(D, Ic);
				DrawItemIcon(D, ArtelNet->NetItem, Ic.Inset(2.f), 2.f);
				const float TX = Ic.Right() + 8.f;
				D.Text(TEXT("Занято артелью"), FVector2D(TX, Y), TS, FVPal::Ink(), EFVAlign::Left, false, true);
				D.Text(FString::Printf(TEXT("%s · прочность %d%%"), *FVDB::ItemName(ArtelNet->NetItem), FMath::RoundToInt(Frac * 100.f)),
					FVector2D(TX, Y + D.LineHeight(TS) + 2.f), SmallS() + 0.5f, ArtelNet->bTorn ? InkBad() : FVPal::InkSoft(), EFVAlign::Left, false, false);
				Y += 44.f;
				FString Info = ArtelNet->bTorn ? FString(TEXT("Сеть порвана. "))
					: FString::Printf(TEXT("В сети: %s. "), *FishCount(Units));
				Info += TEXT("Эту сеть поставил Артельщик Никифор: он каждое утро собирает и продаёт улов. Починить, заменить или забрать сеть можно у него в Городе.");
				D.TextWrapped(Info, FVector2D(X, Y), W, SmallS() + 0.5f, FVPal::InkSoft());
			}
			if (bP)
			{
				if (D.ActionBar(Win)) { bClose = true; }
			}
			else
			{
				const FFVRect Btn(Win.Center().X - 60.f, Win.Bottom() - 34.f, 120.f, 22.f);
				if (D.Button(Btn, TEXT("Понятно"), EFVButton::Normal, true, 8.f)) { bClose = true; }
			}
		}

		/** Header line: location + spot. Returns the Y below it. */
		float DrawHeader(FVDraw& D, const FFVLocationDef& Loc, const FFVRect& Win)
		{
			const bool bP = FV::IsPortrait();
			const float X = Win.X + (bP ? 10.f : 12.f);
			const float Y = Win.Y + (bP ? 16.f : 10.f);
			const float HS = bP ? FV::Ui::Body() + 1.f : 9.f;
			D.Rect(FFVRect(X, Y + 2.f, 4.f, D.LineHeight(HS) - 3.f), FVUI::ThemeColor(Loc.Theme));
			D.Text(FString::Printf(TEXT("%s · место №%d"), *Loc.Name, Spot + 1), FVector2D(X + 8.f, Y), HS, FVPal::Ink(), EFVAlign::Left, false, true);
			return Y + D.LineHeight(HS) + (bP ? 6.f : 4.f);
		}

		// ---------------------------------------------------------------------------------------- shore scene
		void DrawScene(FVDraw& D, const FFVRect& R, const FFVLocationDef& Loc, const FFVNetState* Net, const FFVItemDef* NetDef)
		{
			IconBox(D, R);
			const FFVRect In = R.Inset(2.f);
			const float T = float(D.Time);
			const float LandH = FMath::RoundToFloat(In.H * 0.34f);
			const float ShoreY = In.Y + LandH;
			// land (theme tinted) + sand edge
			const FLinearColor Land = FVUI::LerpLinear(FV::Col(110, 170, 80), FVUI::ThemeColor(Loc.Theme), 0.35f);
			D.Rect(FFVRect(In.X, In.Y, In.W, LandH), Land);
			for (int32 i = 0; i < 26; ++i)
			{
				const float GX = In.X + FVUI::Hash01(i, 1, 71) * (In.W - 2.f);
				const float GY = In.Y + 2.f + FVUI::Hash01(i, 2, 71) * (LandH - 8.f);
				D.Rect(FFVRect(FMath::RoundToFloat(GX), FMath::RoundToFloat(GY), 1.f, 3.f), FV::Col(70, 130, 50));
			}
			D.Rect(FFVRect(In.X, ShoreY - 4.f, In.W, 5.f), FV::Col(226, 200, 140));
			// water
			const FFVRect Water(In.X, ShoreY + 1.f, In.W, In.Bottom() - ShoreY - 1.f);
			D.GradientV(Water, FV::Col(Loc.WaterShallow), FV::Col(Loc.WaterDeep), 10);
			for (int32 i = 0; i < 12; ++i)
			{
				const float WY = Water.Y + 6.f + FVUI::Hash01(i, 5, 72) * (Water.H - 10.f);
				const float WX = Water.X + FMath::Fmod(FVUI::Hash01(i, 6, 72) * Water.W + T * (6.f + 4.f * FVUI::Hash01(i, 7, 72)), Water.W - 10.f);
				D.Rect(FFVRect(FMath::RoundToFloat(WX), FMath::RoundToFloat(WY), 8.f, 1.f), FLinearColor(1.f, 1.f, 1.f, 0.25f));
			}
			UFVSprites* Sp = UFVSprites::Get();
			const FVector2D Mid(In.Center().X, ShoreY);
			bool bDrawnPosts = false;
			bool bDrawnNet = false;
			if (Net && NetDef && Sp)
			{
				const int32 Cap = FMath::Max(1, NetDef->NetCapacity);
				const int32 Fill = FMath::Clamp(FMath::CeilToInt(3.f * float(CatchUnits(*Net)) / float(Cap)), 0, 3);
				const FFVSprite NS = Sp->NetWaterSprite(FMath::Clamp<int32>(NetDef->Tier, 1, 3), Fill, Net->bTorn);
				if (NS.IsValid())
				{
					const float Sc = FVUI::FitScale(NS, Water.W * 0.8f, Water.H * 0.8f, 3.f);
					const float Bob = FMath::Sin(T * 1.6f) * 1.5f;
					D.Sprite(NS, FVector2D(FMath::RoundToFloat(Mid.X - NS.W * Sc * 0.5f), FMath::RoundToFloat(ShoreY + 3.f + Bob)), Sc);
					bDrawnNet = true;
				}
			}
			if (Sp)
			{
				const FFVSprite PS = Sp->NetPostSprite(Net != nullptr);
				if (PS.IsValid())
				{
					const float Sc = FVUI::FitScale(PS, In.W * 0.7f, LandH + 6.f, 3.f);
					D.Sprite(PS, FVector2D(FMath::RoundToFloat(Mid.X - PS.W * Sc * 0.5f), FMath::RoundToFloat(ShoreY + 2.f - PS.H * Sc)), Sc);
					bDrawnPosts = true;
				}
			}
			const float PostDX = FMath::RoundToFloat(In.W * 0.28f);
			if (!bDrawnPosts)
			{
				for (int32 s = -1; s <= 1; s += 2)
				{
					const FFVRect Post(Mid.X + s * PostDX - 2.f, ShoreY - 18.f, 4.f, 22.f);
					D.Rect(Post, FV::Col(96, 60, 30));
					D.Rect(FFVRect(Post.X, Post.Y, 1.f, Post.H), FV::Col(140, 96, 54));
				}
			}
			if (Net && !bDrawnNet)
			{
				// fallback net: rope + mesh between the posts
				const float Top = ShoreY + 4.f;
				const float H = FMath::Min(Water.H * 0.6f, 44.f);
				const FLinearColor Rope = NetDef ? FV::Col(NetDef->Tint) : FV::Col(200, 180, 130);
				D.Line(FVector2D(Mid.X - PostDX, ShoreY - 12.f), FVector2D(Mid.X - PostDX + 6.f, Top), Rope, 1.f);
				D.Line(FVector2D(Mid.X + PostDX, ShoreY - 12.f), FVector2D(Mid.X + PostDX - 6.f, Top), Rope, 1.f);
				for (int32 k = 0; k <= 8; ++k)
				{
					const float X0 = Mid.X - PostDX + 6.f + (2.f * PostDX - 12.f) * float(k) / 8.f;
					if (Net->bTorn && k == 5) { continue; }
					D.Line(FVector2D(X0, Top), FVector2D(X0, Top + H), FV::Alpha(Rope, 0.8f), 1.f);
				}
				for (int32 k = 0; k <= 4; ++k)
				{
					const float Y0 = Top + H * float(k) / 4.f;
					D.Line(FVector2D(Mid.X - PostDX + 6.f, Y0), FVector2D(Mid.X + PostDX - 6.f, Y0), FV::Alpha(Rope, 0.8f), 1.f);
				}
				for (int32 k = 0; k < 5; ++k)
				{
					D.Circle(FVector2D(Mid.X - PostDX + 6.f + (2.f * PostDX - 12.f) * float(k) / 4.f, Top + FMath::Sin(T * 2.f + k) * 1.f), 2.f, FV::Col(236, 80, 60));
				}
			}
			if (!Net)
			{
				D.TextOutlined(TEXT("Здесь можно поставить сеть"), FVector2D(Water.Center().X, Water.Center().Y - 4.f), SmallS(), FV::Col(236, 248, 255), EFVAlign::Center, FV::Col(20, 50, 80));
			}
		}

		// ---------------------------------------------------------------------------------------- no net: choose one
		struct FNetChoice
		{
			FName Id;
			int32 Count = 0;
		};

		void DrawEmpty(FVDraw& D, UFVSaveGame& P, const FFVLocationDef& Loc, const FFVRect& Win, bool& bClose)
		{
			const bool bP = FV::IsPortrait();
			const float Top = DrawHeader(D, Loc, Win);
			// nets in the backpack (one row per id)
			TArray<FNetChoice> Nets;
			for (const FFVInvStack& S : P.Backpack)
			{
				const FFVItemDef* It = FVDB::FindItem(S.ItemId);
				if (!It || It->Kind != EFVItemKind::Net || S.Count <= 0) { continue; }
				FNetChoice* Found = Nets.FindByPredicate([&S](const FNetChoice& C) { return C.Id == S.ItemId; });
				if (Found) { Found->Count += S.Count; }
				else { FNetChoice C; C.Id = S.ItemId; C.Count = S.Count; Nets.Add(C); }
			}
			int32 Sel = Nets.IndexOfByPredicate([this](const FNetChoice& C) { return C.Id == SelNet; });
			if (Sel == INDEX_NONE && Nets.Num() > 0) { Sel = 0; SelNet = Nets[0].Id; }
			if (PendingMove != 0 && Nets.Num() > 0)
			{
				Sel = FMath::Clamp(Sel + PendingMove, 0, Nets.Num() - 1);
				SelNet = Nets[Sel].Id;
				bEnsureSel = true;
				FVUI::Sfx(EFVSfx::Click);
			}
			PendingMove = 0;
			const FFVItemDef* SelDef = Nets.IsValidIndex(Sel) ? FVDB::FindItem(Nets[Sel].Id) : nullptr;

			const float X = Win.X + 10.f;
			const float W = Win.W - 20.f;
			const float Bottom = bP ? FVDraw::ActionBarRect(Win).Y - 10.f : Win.Bottom() - 10.f;
			const FFVRect SceneR = bP ? FFVRect(X, Top, W, 104.f) : FFVRect(X, Top, 250.f, 150.f);
			DrawScene(D, SceneR, Loc, nullptr, nullptr);
			// how nets work (landscape: under the scene)
			const FString Help = TEXT("Сеть ловит рыбу сама, даже пока вы спите. Чем больше сеть, тем больше улова; ценная рыба быстрее её изнашивает.");
			if (!bP)
			{
				const FFVRect HelpR(X, SceneR.Bottom() + 6.f, SceneR.W, Bottom - SceneR.Bottom() - 6.f);
				D.Panel(HelpR, EFVPanel::Parchment);
				TextWrappedLimited(D, Help, FVector2D(HelpR.X + 7.f, HelpR.Y + 6.f), HelpR.W - 14.f, 7.f, FVPal::InkSoft(), 8);
			}
			const FFVRect ListP = bP ? FFVRect(X, SceneR.Bottom() + 6.f, W, Bottom - SceneR.Bottom() - 6.f) : FFVRect(SceneR.Right() + 6.f, Top, Win.Right() - 10.f - SceneR.Right() - 6.f, Bottom - Top);
			D.Panel(ListP, EFVPanel::Parchment);
			const FFVRect In = ListP.Inset(7.f);
			const float HS = bP ? FV::Ui::Body() : 8.f;
			D.Text(TEXT("Сети в рюкзаке"), FVector2D(In.X, In.Y), HS, FVPal::Ink(), EFVAlign::Left, false, true);
			const float LTop = In.Y + D.LineHeight(HS) + 3.f;
			// details of the selected net: the classic height, or taller when its description + effects need more lines
			TArray<FString> SelFx;
			if (SelDef)
			{
				SelFx = ItemEffectLines(*SelDef);
				SelFx.Add(TEXT("в дождь и ночью ловит чаще"));
			}
			const float DetNeed = SelDef ? 9.f + WrappedLimitedH(D, SelDef->Description, In.W, SmallS()) + 1.f + DrawPartsAll(D, SelFx, In.X, 0.f, In.W, SmallS(), InkInfo(), false) + 2.f : 0.f;
			const float DetailH = FMath::Min(FMath::Max(bP ? 64.f : 70.f, DetNeed), FMath::Max(bP ? 64.f : 70.f, In.Bottom() - LTop - (bP ? 0.f : 26.f) - 42.f));
			const FFVRect ListA(In.X, LTop, In.W, In.Bottom() - LTop - DetailH - (bP ? 0.f : 26.f));
			if (Nets.Num() == 0)
			{
				EmptyState(D, FFVRect(In.X, LTop, In.W, In.Bottom() - LTop - (bP ? 0.f : 26.f)), EFVIcon::Net,
					TEXT("В рюкзаке нет сетей. Сплетите сеть в мастерской (нужен верстак) или купите в лавке."), SmallS());
			}
			else
			{
				const float RowH = bP ? 38.f : 26.f;
				const float Pitch = RowH + 2.f;
				NetList.Update(D, ListA, float(Nets.Num()) * Pitch - 2.f);
				if (bEnsureSel && Sel >= 0) { NetList.ScrollToShow(float(Sel) * Pitch, RowH); }
				bEnsureSel = false;
				const float RW = ListA.W - (NetList.CanScroll() ? 7.f : 0.f);
				D.PushClip(ListA);
				for (int32 i = 0; i < Nets.Num(); ++i)
				{
					const FFVRect Row(ListA.X, ListA.Y - NetList.Offset() + float(i) * Pitch, RW, RowH);
					if (!RowVisible(ListA, Row.Y, RowH)) { continue; }
					const FFVItemDef* It = FVDB::FindItem(Nets[i].Id);
					RowBack(D, Row, i == Sel, !bP && D.Hover(Row), PressingIn(D, Row));
					if (D.Clicked(Row) && i != Sel) { SelNet = Nets[i].Id; Sel = i; SelDef = It; FVUI::Sfx(EFVSfx::Click); }
					const float IS = RowH - 6.f;
					DrawItemIcon(D, Nets[i].Id, FFVRect(Row.X + 3.f, Row.Y + 3.f, IS, IS), 2.f);
					const float TX = Row.X + IS + 9.f;
					const float NS = bP ? FV::Ui::Body() : 7.5f;
					float CW = 0.f;
					if (Nets[i].Count > 1) { CW = FVUI::StatusChip(D, FString::Printf(TEXT("×%d"), Nets[i].Count), FVector2D(Row.Right() - 3.f, Row.Y + 3.f), FVUI::EChip::Neutral, false, bP ? 7.f : 6.f); }
					D.Text(D.Ellipsize(FVDB::ItemName(Nets[i].Id), Row.Right() - 8.f - CW - TX, NS), FVector2D(TX, Row.Y + (bP ? 3.f : 2.f)), NS, FVPal::Ink(), EFVAlign::Left, false, true);
					if (It)
					{
						const FString Sub = FString::Printf(TEXT("%d рыб · прочность %d · 1 рыба ≈ %s"), It->NetCapacity, FMath::RoundToInt(It->NetDurability), *UFVGame::DurationText(It->NetMinutesPerFish));
						D.Text(D.Ellipsize(Sub, Row.Right() - 5.f - TX, SmallS(), false), FVector2D(TX, Row.Y + (bP ? 21.f : 14.f)), SmallS(), FVPal::InkSoft(), EFVAlign::Left, false, false);
					}
				}
				EndList(D, NetList, ListA);
				// details of the selected net
				if (SelDef)
				{
					float Y = ListA.Bottom() + 4.f;
					D.Divider(FVector2D(In.X, Y), In.W);
					Y += 5.f;
					Y += TextWrappedLimited(D, SelDef->Description, FVector2D(In.X, Y), In.W, SmallS(), FVPal::Ink(), 40) + 1.f;
					TArray<FString> Fx = ItemEffectLines(*SelDef);
					Fx.Add(TEXT("в дождь и ночью ловит чаще"));
					DrawPartsAll(D, Fx, In.X, Y, In.W, SmallS(), InkInfo());   // every effect (the panel grew to fit them)
				}
			}
			const FString Label = SelDef ? FString::Printf(TEXT("Поставить: %s"), *SelDef->Name) : FString(TEXT("Нет сетей"));
			if (bP)
			{
				FFVRect ActR;
				if (D.ActionBar(Win, &ActR)) { bClose = true; }
				if (D.Button(ActR, SelDef ? FString(TEXT("Поставить сеть")) : Label, EFVButton::Primary, SelDef != nullptr, FV::Ui::Body() + 0.5f)) { Want(EAct::Place, SelNet); }
				return;
			}
			if (D.Button(FFVRect(In.X, In.Bottom() - 20.f, In.W, 20.f), Label, EFVButton::Primary, SelDef != nullptr, 8.f)) { Want(EAct::Place, SelNet); }
		}

		// ---------------------------------------------------------------------------------------- net placed
		void DrawNet(FVDraw& D, UFVSaveGame& P, const FFVLocationDef& Loc, const FFVNetState& Net, const FFVRect& Win, bool& bClose)
		{
			const bool bP = FV::IsPortrait();
			const FFVItemDef* Def = FVDB::FindItem(Net.NetItem);
			const float Top = DrawHeader(D, Loc, Win);
			const float MaxDur = Def ? FMath::Max(1.f, Def->NetDurability) : FMath::Max(1.f, Net.Durability);
			const float Frac = FMath::Clamp(Net.Durability / MaxDur, 0.f, 1.f);
			const int32 Cap = Def ? FMath::Max(1, Def->NetCapacity) : 1;
			const int32 Units = CatchUnits(Net);
			const TArray<FFVIngredient> Cost = P.NetRepairCost();
			const bool bNeedsRepair = Net.bTorn || Net.Durability < MaxDur - 0.5f;
			const bool bCanRepair = bNeedsRepair && P.HasIngredients(Cost, 1);
			const bool bCanRemove = !Net.bTorn && Frac >= RemoveMinFrac - 0.0001f && Units == 0;
			// A torn / worn-out net can always be thrown away (its catch is collected first as far as it fits).
			const bool bCanDestroy = Net.bTorn || Frac < RemoveMinFrac - 0.0001f;

			const float X = Win.X + 10.f;
			const float W = Win.W - 20.f;
			const float Bottom = bP ? FVUI::ActionBarExtraRowRect(Win).Y - 10.f : Win.Bottom() - 10.f;
			const FFVRect SceneR = bP ? FFVRect(X, Top, W, 100.f) : FFVRect(X, Top, 260.f, 120.f);
			DrawScene(D, SceneR, Loc, &Net, Def);

			// info card: name, durability, state, rate
			FString State;
			FLinearColor StateC = FVPal::InkSoft();
			if (Net.bTorn) { State = TEXT("Сеть порвана и ничего не ловит — почините её."); StateC = InkBad(); }
			else if (Units >= Cap) { State = TEXT("Сеть полна — соберите улов, иначе она простаивает."); StateC = InkWarn(); }
			else if (Def && Def->NetMinutesPerFish > 0.f)
			{
				const float Next = FMath::Max(1.f, (1.f - FMath::Clamp(Net.Progress, 0.f, 1.f)) * Def->NetMinutesPerFish);
				State = FString::Printf(TEXT("1 рыба ≈ за %s (в дождь и ночью чаще). Следующая ≈ через %s."), *UFVGame::DurationText(Def->NetMinutesPerFish), *UFVGame::DurationText(Next));
			}
			if (Frac < 0.25f && !Net.bTorn) { State += TEXT(" Сеть изношена: может порваться!"); StateC = InkWarn(); }
			const float NS = bP ? FV::Ui::Body() + 0.5f : 8.5f;
			const float BarH = bP ? 9.f : 7.f;
			float InfoH = Bottom - SceneR.Bottom() - 6.f;   // landscape: the whole column under the scene
			if (bP)
			{
				// portrait: the classic height, taller when the state / repair lines need more room (the catch grid below scrolls)
				const float InW = W - 14.f;
				const float Need = 14.f + D.LineHeight(NS) + 2.f + D.LineHeight(SmallS()) + 1.f + BarH + 4.f
					+ WrappedLimitedH(D, State, InW, SmallS()) + 3.f + RepairCostLine(D, P, Cost, 0.f, 0.f, InW, SmallS(), bNeedsRepair, false) + 2.f;
				const float BaseH = 96.f + 2.f * D.LineHeight(SmallS(), false);
				InfoH = FMath::Max(BaseH, FMath::Min(FMath::CeilToFloat(Need), Bottom - SceneR.Bottom() - 6.f - 6.f - 70.f));
			}
			const FFVRect InfoR(X, SceneR.Bottom() + 6.f, bP ? W : SceneR.W, InfoH);
			D.Panel(InfoR, EFVPanel::Parchment);
			{
				const FFVRect In = InfoR.Inset(7.f);
				float Y = In.Y;
				const FString Name = Def ? Def->Name : FVDB::ItemName(Net.NetItem);
				float ChipW = 0.f;
				if (Net.bTorn) { ChipW = FVUI::StatusChip(D, TEXT("Порвана!"), FVector2D(In.Right(), Y - 1.f), FVUI::EChip::Bad, false, bP ? 7.5f : 6.5f); }
				else if (Units >= Cap) { ChipW = FVUI::StatusChip(D, TEXT("Полна"), FVector2D(In.Right(), Y - 1.f), FVUI::EChip::Bad, false, bP ? 7.5f : 6.5f); }
				else { ChipW = FVUI::StatusChip(D, TEXT("Ловит"), FVector2D(In.Right(), Y - 1.f), FVUI::EChip::Good, false, bP ? 7.5f : 6.5f); }
				D.Text(D.Ellipsize(Name, In.W - ChipW - 6.f, NS), FVector2D(In.X, Y), NS, FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(NS) + 2.f;
				D.Text(FString::Printf(TEXT("Прочность: %d / %d (%s)"), FMath::RoundToInt(Net.Durability), FMath::RoundToInt(MaxDur), *FVUI::Percent(Frac)),
					FVector2D(In.X, Y), SmallS(), FVPal::Ink(), EFVAlign::Left, false, true);
				Y += D.LineHeight(SmallS()) + 1.f;
				const FLinearColor BarC = Net.bTorn ? FVPal::Red() : (Frac < 0.25f ? FVPal::Red() : (Frac < 0.5f ? FV::Col(236, 170, 40) : FVPal::Green()));
				D.Bar(FFVRect(In.X, Y, In.W, BarH), Frac, BarC);
				Y += BarH + 4.f;
				Y += TextWrappedLimited(D, State, FVector2D(In.X, Y), In.W, SmallS(), StateC, 40) + 3.f;   // the whole state (no line cap)
				// Р49: repair cost with what the player has (both orientations)
				Y += RepairCostLine(D, P, Cost, In.X, Y, In.W, SmallS(), bNeedsRepair) + 2.f;
				if (!bP && !bCanRemove && Y + D.LineHeight(SmallS(), false) <= In.Bottom() + 1.f)
				{
					// as many lines as the card has left (two at the current sizes)
					const float NoteStep = FMath::Max(1.f, FMath::RoundToFloat(D.LineHeight(SmallS(), false) * 0.95f));
					TextWrappedLimited(D, bCanDestroy ? FString(TEXT("Снять можно только целую сеть (от 95%). Не хотите чинить — сеть можно выбросить.")) : FString(TEXT("Снять можно только целую сеть (от 95%) с пустым уловом.")), FVector2D(In.X, Y), In.W, SmallS(), FVPal::InkSoft(),
						FMath::Max(2, FMath::FloorToInt((In.Bottom() + 1.f - Y) / NoteStep)));
				}
			}

			// catch
			const FFVRect CatchR = bP ? FFVRect(X, InfoR.Bottom() + 6.f, W, Bottom - InfoR.Bottom() - 6.f) : FFVRect(SceneR.Right() + 6.f, Top, Win.Right() - 10.f - SceneR.Right() - 6.f, Bottom - Top);
			D.Panel(CatchR, EFVPanel::Parchment);
			{
				const FFVRect In = CatchR.Inset(7.f);
				const float HS = bP ? FV::Ui::Body() : 8.f;
				D.Text(FString::Printf(TEXT("Улов: %d / %d"), Units, Cap), FVector2D(In.X, In.Y), HS, FVPal::Ink(), EFVAlign::Left, false, true);
				D.Bar(FFVRect(In.Right() - 90.f, In.Y + 2.f, 90.f, bP ? 8.f : 6.f), float(Units) / float(Cap), Units >= Cap ? FVPal::Red() : FV::Col(70, 150, 220));
				const float GTop = In.Y + D.LineHeight(HS) + 4.f;
				const float GBottom = bP ? In.Bottom() : In.Bottom() - 50.f;
				const FFVRect GridA(In.X, GTop, In.W, GBottom - GTop);
				if (Net.Catch.Num() == 0)
				{
					EmptyState(D, GridA, EFVIcon::Fish, Net.bTorn ? TEXT("Порванная сеть ничего не ловит.") : TEXT("Пока пусто. Рыба попадётся со временем — загляните позже."), SmallS());
				}
				else
				{
					// every stack of the catch, smoothly scrolling when it does not fit
					const float Cell = bP ? 44.f : 30.f;
					const float Pitch = Cell + 3.f;
					const int32 Cols = FMath::Max(1, FMath::FloorToInt((GridA.W - 6.f + 3.f) / Pitch));
					const int32 Rows = FMath::DivideAndRoundUp(Net.Catch.Num(), Cols);
					CatchScroll.Update(D, GridA, float(Rows) * Pitch - 3.f);
					D.PushClip(GridA);
					for (int32 i = 0; i < Net.Catch.Num(); ++i)
					{
						const FFVInvStack& S = Net.Catch[i];
						const FFVRect C(GridA.X + float(i % Cols) * Pitch, GridA.Y - CatchScroll.Offset() + float(i / Cols) * Pitch, Cell, Cell);
						if (!RowVisible(GridA, C.Y, Cell)) { continue; }
						D.Rect(C, BoxEdge());
						D.Rect(C.Inset(1.f), BoxFill());
						StackCell(D, C, S.ItemId, S.Quality, S.Flags, S.Count);
						if (!bP && D.Hover(C))
						{
							const EFVQuality Q = QualityOf(S.Quality);
							D.Tooltip(FVDB::StackName(S.ItemId, S.Flags), Q != EFVQuality::Normal ? FString::Printf(TEXT("%s · ×%d"), *FVDB::QualityName(Q), S.Count) : FString::Printf(TEXT("×%d"), S.Count));
						}
					}
					EndList(D, CatchScroll, GridA);
				}
				if (!bP)
				{
					const float BW = FMath::FloorToFloat((In.W - 6.f) * 0.5f);
					if (D.Button(FFVRect(In.X, In.Bottom() - 44.f, In.W, 20.f), Units > 0 ? FString::Printf(TEXT("Собрать улов (%d)"), Units) : FString(TEXT("Собрать улов")), EFVButton::Primary, Units > 0, 8.f))
					{
						Want(EAct::Collect);
					}
					const FString RepairLabel = !bNeedsRepair ? FString(TEXT("Сеть целая")) : FString::Printf(TEXT("Починить (%s)"), *CostShort(Cost));
					if (D.Button(FFVRect(In.X, In.Bottom() - 20.f, BW, 20.f), RepairLabel, EFVButton::Normal, bCanRepair, 7.f)) { Want(EAct::Repair); }
					if (bCanDestroy)
					{
						if (D.Button(FFVRect(In.X + BW + 6.f, In.Bottom() - 20.f, In.W - BW - 6.f, 20.f), TEXT("Выбросить сеть"), EFVButton::Danger, true, 7.5f)) { Want(EAct::Destroy); }
					}
					else if (D.Button(FFVRect(In.X + BW + 6.f, In.Bottom() - 20.f, In.W - BW - 6.f, 20.f), TEXT("Снять сеть"), EFVButton::Normal, bCanRemove, 7.5f)) { Want(EAct::Remove); }
				}
			}
			if (!bP) { return; }
			const FFVRect Row = FVUI::ActionBarExtraRow(D, Win);
			if (D.Button(FVDraw::SplitH(Row, 0, 2, 8.f), bNeedsRepair ? FString(TEXT("Починить")) : FString(TEXT("Сеть целая")), EFVButton::Normal, bCanRepair, FV::Ui::Body())) { Want(EAct::Repair); }
			if (bCanDestroy)
			{
				if (D.Button(FVDraw::SplitH(Row, 1, 2, 8.f), TEXT("Выбросить сеть"), EFVButton::Danger, true, FV::Ui::Body())) { Want(EAct::Destroy); }
			}
			else if (D.Button(FVDraw::SplitH(Row, 1, 2, 8.f), bCanRemove ? FString(TEXT("Снять сеть")) : FString(TEXT("Снять нельзя")), EFVButton::Normal, bCanRemove, FV::Ui::Body())) { Want(EAct::Remove); }
			FFVRect ActR;
			if (D.ActionBar(Win, &ActR)) { bClose = true; }
			if (D.Button(ActR, Units > 0 ? FString::Printf(TEXT("Собрать улов (%d)"), Units) : FString(TEXT("Улова нет")), EFVButton::Primary, Units > 0, FV::Ui::Body() + 0.5f)) { Want(EAct::Collect); }
		}

		// ---------------------------------------------------------------------------------------- actions
		void RunAct(UFVSaveGame& P)
		{
			const EAct A = Act;
			const FName Id = ActId;
			Act = EAct::None;
			UFVGame* G = Game;
			if (A == EAct::None || !G) { return; }
			switch (A)
			{
			case EAct::Place:
			{
				const EFVBuyResult R = P.PlaceNet(LocId, Spot, Id, G->GetAbsMinutes());
				if (R != EFVBuyResult::Ok)
				{
					Fail(G, R == EFVBuyResult::LevelTooLow ? FString(TEXT("Слишком низкий уровень для этой сети")) : ResultText(R));
					return;
				}
				Success(G, FString::Printf(TEXT("Сеть поставлена: %s"), *FVDB::ItemName(Id)), EFVSfx::Splash, EFVIcon::Net);
				Refresh(G);
				return;
			}
			case EAct::Collect:
			{
				const FFVNetState* Net = P.FindNet(LocId, Spot);
				const int32 Before = Net ? CatchUnits(*Net) : 0;
				const int32 Moved = P.CollectNet(LocId, Spot);
				if (Moved <= 0)
				{
					Fail(G, Before > 0 ? FString(TEXT("Нет места: садок или рюкзак полон")) : FString(TEXT("В сети пусто")));
					return;
				}
				if (Moved < Before) { Success(G, FString::Printf(TEXT("Собрано %d, остальное не поместилось"), Moved), EFVSfx::Catch, EFVIcon::Keepnet); }
				else { Success(G, FString::Printf(TEXT("Улов собран: +%d"), Moved), EFVSfx::Catch, EFVIcon::Keepnet); }
				Refresh(G);
				return;
			}
			case EAct::Repair:
			{
				const EFVBuyResult R = P.RepairNet(LocId, Spot);
				if (R != EFVBuyResult::Ok) { Fail(G, R); return; }
				Success(G, TEXT("Сеть починена"), EFVSfx::Knot, EFVIcon::Knot);
				Refresh(G);
				return;
			}
			case EAct::Remove:
			{
				const FFVNetState* Net = P.FindNet(LocId, Spot);
				const FName Item = Net ? Net->NetItem : NAME_None;
				const EFVBuyResult R = P.RemoveNet(LocId, Spot);
				if (R != EFVBuyResult::Ok)
				{
					Fail(G, R == EFVBuyResult::NoRoom ? FString(TEXT("В рюкзаке нет места для сети")) : FString(TEXT("Снять можно только целую сеть с пустым уловом")));
					return;
				}
				Success(G, FString::Printf(TEXT("Сеть снята: %s — в рюкзак"), *FVDB::ItemName(Item)), EFVSfx::Equip, EFVIcon::Backpack);
				SelNet = Item;
				Refresh(G);
				return;
			}
			case EAct::Destroy:
			{
				const FFVNetState* Net = P.FindNet(LocId, Spot);
				if (!Net) { return; }
				const FName Item = Net->NetItem;
				const FFVItemDef* Def = FVDB::FindItem(Item);
				const float MaxDur = (Def && Def->NetDurability > 0.f) ? Def->NetDurability : 1.f;
				const int32 Pct = FMath::RoundToInt(100.f * FMath::Clamp(Net->Durability / MaxDur, 0.f, 1.f));
				const int32 CatchN = CatchUnits(*Net);
				const FString State = Net->bTorn ? FString(TEXT("порвана")) : FString::Printf(TEXT("прочность %d%%"), Pct);
				FString Body = FString::Printf(TEXT("«%s» (%s) пропадёт навсегда — в рюкзак она не вернётся."), *FVDB::ItemName(Item), *State);
				if (CatchN > 0)
				{
					Body += FString::Printf(TEXT(" Улов (%d) сначала заберём в садок и рюкзак; что не поместится — пропадёт."), CatchN);
				}
				TWeakObjectPtr<UFVGame> WeakG(G);
				const FName Loc = LocId;
				const int32 SpotIdx = Spot;
				G->Confirm(TEXT("Выбросить сеть?"), Body, [WeakG, Loc, SpotIdx, Item]()
				{
					UFVGame* GG = WeakG.Get();
					UFVSaveGame* PP = GG ? GG->GetProfile() : nullptr;
					if (!PP) { return; }
					int32 Collected = 0, Lost = 0;
					if (PP->DestroyNet(Loc, SpotIdx, Collected, Lost) != EFVBuyResult::Ok) { return; }
					FString Msg = FString::Printf(TEXT("Сеть выброшена: %s"), *FVDB::ItemName(Item));
					if (Collected > 0) { Msg += FString::Printf(TEXT(" · улов +%d"), Collected); }
					if (Lost > 0) { Msg += FString::Printf(TEXT(" · не поместилось: %d"), Lost); }
					GG->Toast(Msg, FVPal::Gold(), EFVIcon::Net);
					if (UFVAudio* A = UFVAudio::Get()) { A->Play(EFVSfx::NetTear, 0.8f); }
					GG->NotifyProfileChanged();
					GG->GetScene().RefreshWorldObjects();
				}, TEXT("Выбросить"), TEXT("Отмена"));
				return;
			}
			default:
				return;
			}
		}
	};
}

TUniquePtr<FVScreen> FVMakeNetSpotScreen(UFVGame* Game)
{
	return MakeUnique<FVNetScreenImpl::FNetScreen>(Game);
}
