// UPDATE 6 (Р69) — way signs at the walk exits between locations (FVSceneU6Objects.cpp places them): a wooden board on a
// post, shaped as an arrow, with a 16x12 picture of the target place (the river with a forest on its banks, spruces, the
// pond with reeds and a cottage, peaks with a lake, the sea with a sail, reeds in fog, a smoking mountain, a water wheel,
// houses and a lighthouse, a cave mouth, a cottage). A closed place: the picture dimmed + a padlock. Plus the trodden
// path decal that runs off the screen edge at the exits of the one-screen locations.
// Style: as the forest / mill arrow signs (FVArtForest.cpp, FVArtMill.cpp): warm boards, PropOut outline.

#include "FVSprites.h"
#include "FVWorldU6.h"

namespace FVArtSignsImpl
{
	FORCEINLINE FColor Rgb(uint8 R, uint8 G, uint8 B) { return FColor(R, G, B, 255); }

	const FColor Out(58, 36, 30);
	const FColor BoardC(214, 170, 110), BoardHi(236, 196, 140), BoardLo(170, 124, 76), BoardDk(122, 84, 52);
	const FColor PostC(120, 84, 52), PostLo(88, 58, 38);
	const FColor Sky(168, 212, 238), SkyLo(198, 228, 244), Grass(98, 164, 72), GrassDk(70, 128, 58);
	const FColor Water(70, 140, 210), WaterLo(44, 100, 170), Glint(224, 242, 250);
	const FColor Spruce(46, 110, 66), SpruceDk(30, 78, 52), Trunk(110, 72, 44);

	// board (inclusive), picture 16x12 at (PX, PY) — all in the 30x30 layout (copied at +1,+1 into the 32x32 canvas)
	constexpr int32 LW = 30, LH = 30;
	constexpr int32 BX0 = 4, BX1 = 25, BY0 = 3, BY1 = 18;
	constexpr int32 PX = 7, PY = 5;

	/** Drawing inside the 16x12 picture (clipped). */
	struct FPic
	{
		FFVImage& I;
		void P(int32 X, int32 Y, FColor C) const { if (X >= 0 && X < 16 && Y >= 0 && Y < 12) { I.Set(PX + X, PY + Y, C); } }
		void R(int32 X, int32 Y, int32 W, int32 H, FColor C) const { for (int32 y = Y; y < Y + H; ++y) { for (int32 x = X; x < X + W; ++x) { P(x, y, C); } } }
		void Rows(int32 Y0, int32 Y1, FColor C) const { R(0, Y0, 16, Y1 - Y0 + 1, C); }
		/** A spruce: triangle of H rows ending at row BaseY - 1, the trunk at BaseY. */
		void Tree(int32 X, int32 BaseY, int32 H, FColor C, FColor Dk) const
		{
			for (int32 r = 0; r < H; ++r)
			{
				const int32 HW = (r + 1) / 2, Y = BaseY - H + r;
				for (int32 x = X - HW; x <= X + HW; ++x) { P(x, Y, x > X ? Dk : C); }
			}
			P(X, BaseY, Trunk);
		}
		/** Sky gradient (lighter at the horizon row HorizonY). */
		void SkyTo(int32 HorizonY, FColor Top, FColor Low) const
		{
			for (int32 y = 0; y <= HorizonY; ++y) { Rows(y, y, FVArt::Mix(Top, Low, HorizonY > 0 ? float(y) / float(HorizonY) : 1.f)); }
		}
	};

	void PicRiver(const FPic& C)
	{
		C.SkyTo(3, Sky, SkyLo);
		C.Rows(4, 11, Grass);
		for (int32 y = 4; y < 12; ++y)
		{
			const float Mid = 7.5f + FMath::Sin(float(y) * 0.7f) * 1.1f, HW = 0.8f + float(y - 4) * 0.32f;
			for (int32 x = 0; x < 16; ++x)
			{
				const float D = FMath::Abs(float(x) + 0.5f - Mid);
				if (D <= HW) { C.P(x, y, D > HW - 0.9f ? WaterLo : Water); }
				else if (D <= HW + 0.8f) { C.P(x, y, GrassDk); }
			}
		}
		C.P(7, 6, Glint); C.P(8, 9, Glint); C.P(6, 11, Glint);
		// the forest on both banks
		C.Tree(5, 5, 4, Spruce, SpruceDk); C.Tree(10, 5, 4, Spruce, SpruceDk);
		C.Tree(2, 9, 8, Spruce, SpruceDk); C.Tree(13, 9, 8, Spruce, SpruceDk);
		C.Tree(0, 11, 6, SpruceDk, SpruceDk); C.Tree(15, 11, 6, SpruceDk, SpruceDk);
	}

	void PicForest(const FPic& C)
	{
		C.SkyTo(9, Sky, SkyLo);
		C.Rows(10, 11, GrassDk);
		C.Tree(3, 10, 8, Spruce, SpruceDk);
		C.Tree(13, 10, 7, Spruce, SpruceDk);
		C.Tree(8, 10, 10, Rgb(56, 124, 72), SpruceDk);
		C.P(1, 11, Grass); C.P(6, 11, Grass); C.P(11, 11, Grass); C.P(15, 11, Grass);
	}

	void PicPond(const FPic& C)
	{
		C.SkyTo(4, Sky, SkyLo);
		C.Rows(5, 11, Grass);
		for (int32 y = 6; y < 12; ++y)
		{
			for (int32 x = 0; x < 16; ++x)
			{
				const float DX = (float(x) + 0.5f - 7.5f) / 5.2f, DY = (float(y) + 0.5f - 8.8f) / 2.6f;
				const float D = DX * DX + DY * DY;
				if (D < 1.f) { C.P(x, y, D > 0.62f ? WaterLo : Water); }
			}
		}
		C.P(6, 8, Glint); C.P(9, 9, Glint);
		// the cottage on the bank (right)
		const FColor Wall(232, 212, 170), Roof(190, 70, 50), RoofDk(150, 50, 40);
		C.R(11, 3, 5, 4, Wall);
		for (int32 r = 0; r < 3; ++r) { for (int32 x = 12 - r; x <= 14 + r; ++x) { C.P(x, r, x > 13 ? RoofDk : Roof); } }
		C.P(13, 5, Rgb(110, 70, 44)); C.P(13, 6, Rgb(110, 70, 44)); C.P(12, 4, Rgb(120, 180, 220)); C.P(15, 4, Rgb(120, 180, 220));
		// reeds (left)
		const FColor Stem(84, 130, 60), Head(116, 72, 42);
		for (int32 X : { 1, 3, 4 })
		{
			for (int32 y = (X == 3 ? 4 : 6); y <= 10; ++y) { C.P(X, y, Stem); }
			C.P(X, X == 3 ? 3 : 5, Head); C.P(X, X == 3 ? 4 : 6, Head);
		}
	}

	void PicMountain(const FPic& C)
	{
		C.SkyTo(8, Sky, SkyLo);
		const FColor Rock(132, 136, 150), RockDk(100, 104, 122), Snow(244, 248, 252);
		for (int32 y = 3; y <= 8; ++y)   // the back peak (right)
		{
			const int32 HW = y - 3;
			for (int32 x = 11 - HW; x <= 11 + HW; ++x) { C.P(x, y, y <= 4 ? Snow : (x > 11 ? RockDk : Rock)); }
		}
		for (int32 y = 1; y <= 8; ++y)   // the front peak (left)
		{
			const int32 HW = y - 1;
			for (int32 x = 5 - HW; x <= 5 + HW; ++x) { C.P(x, y, y <= 2 ? Snow : (x > 5 ? RockDk : Rock)); }
		}
		C.P(4, 3, Snow); C.P(6, 3, Snow);
		C.Rows(9, 9, Grass);
		C.Rows(10, 11, Water);
		C.R(1, 10, 3, 1, Glint); C.R(9, 11, 3, 1, Glint);
	}

	void PicOcean(const FPic& C)
	{
		C.SkyTo(6, Sky, SkyLo);
		C.Rows(7, 11, Water);
		for (int32 x = 0; x < 16; x += 3) { C.P(x, 9, Glint); C.P(x + 1, 11, WaterLo); }
		const FColor Hull(140, 90, 52), Sail(250, 248, 236), SailDk(214, 212, 204);
		for (int32 y = 1; y <= 6; ++y) { C.P(8, y, Trunk); }
		for (int32 y = 1; y <= 6; ++y) { for (int32 x = 9; x <= 8 + y && x <= 13; ++x) { C.P(x, y, x == 8 + y ? SailDk : Sail); } }
		for (int32 y = 3; y <= 6; ++y) { for (int32 x = 7 - (y - 3); x <= 7; ++x) { C.P(x, y, Sail); } }
		C.P(8, 0, Rgb(210, 60, 50)); C.P(9, 0, Rgb(210, 60, 50));
		C.R(4, 7, 9, 1, Hull); C.R(5, 8, 7, 1, Rgb(110, 70, 40));
	}

	void PicSwamp(const FPic& C)
	{
		C.SkyTo(6, Rgb(150, 172, 150), Rgb(190, 204, 186));
		C.Rows(7, 11, Rgb(64, 96, 80));
		C.R(2, 9, 5, 1, Rgb(90, 128, 96)); C.R(9, 11, 5, 1, Rgb(90, 128, 96));
		const FColor Stem(84, 124, 60), Head(112, 70, 40), Fog(226, 232, 222);
		for (int32 X : { 1, 3, 5, 11, 13, 14 })
		{
			const int32 Top = 3 + (X * 7) % 3;
			for (int32 y = Top + 2; y <= 10; ++y) { C.P(X, y, Stem); }
			C.P(X, Top, Head); C.P(X, Top + 1, Head);
		}
		for (int32 x = 0; x < 16; ++x)   // two fog bands (dithered)
		{
			if (x % 2 == 0) { C.P(x, 5, Fog); }
			if (x % 2 == 1 || x % 4 == 0) { C.P(x, 8, Fog); }
		}
	}

	void PicVolcano(const FPic& C)
	{
		C.SkyTo(11, Rgb(240, 186, 140), Rgb(250, 214, 170));
		const FColor Rock(86, 64, 64), RockDk(62, 46, 50), Lava(236, 86, 40), LavaHi(255, 196, 80), Smoke(168, 160, 160);
		for (int32 y = 4; y < 12; ++y)
		{
			const int32 HW = 2 + (y - 4);
			for (int32 x = 8 - HW; x <= 7 + HW; ++x) { C.P(x, y, x >= 8 ? RockDk : Rock); }
		}
		C.R(6, 4, 4, 1, Lava); C.P(7, 5, LavaHi); C.P(8, 5, Lava); C.P(8, 6, Lava); C.P(9, 7, Lava); C.P(9, 8, LavaHi); C.P(10, 9, Lava);
		// smoke puffs
		C.R(6, 2, 3, 1, Smoke); C.R(7, 1, 3, 1, Smoke); C.R(9, 0, 3, 1, Smoke); C.P(5, 3, Smoke); C.P(8, 3, Smoke);
	}

	void PicMill(const FPic& C)
	{
		C.SkyTo(7, Sky, SkyLo);
		C.Rows(8, 9, Grass);
		C.Rows(10, 11, Water);
		const FColor Wall(156, 104, 62), WallDk(122, 80, 48), Roof(96, 66, 50);
		C.R(2, 4, 7, 6, Wall); C.R(7, 4, 2, 6, WallDk);
		for (int32 r = 0; r < 3; ++r) { for (int32 x = 5 - r - 1; x <= 5 + r + 1; ++x) { C.P(x, 1 + r, Roof); } }
		C.P(4, 6, Rgb(255, 214, 120)); C.P(5, 8, Rgb(80, 50, 30)); C.P(5, 9, Rgb(80, 50, 30));
		// the water wheel (right), in the water
		const FColor Rim(110, 72, 40), Spoke(150, 104, 60);
		for (int32 k = 0; k < 16; ++k)
		{
			const float A = float(k) * UE_PI / 8.f;
			C.P(FMath::RoundToInt(11.5f + FMath::Cos(A) * 3.2f), FMath::RoundToInt(7.f + FMath::Sin(A) * 3.2f), Rim);
		}
		for (int32 d = -2; d <= 2; ++d) { C.P(12 + d, 7, Spoke); C.P(12, 7 + d, Spoke); }
		C.P(12, 7, Rim);
		C.P(9, 11, Glint); C.P(14, 11, Glint);
	}

	void PicCity(const FPic& C)
	{
		C.SkyTo(11, Rgb(156, 198, 232), Rgb(200, 226, 244));
		const FColor Win(255, 222, 120);
		C.R(0, 5, 4, 7, Rgb(200, 120, 90)); C.R(0, 4, 4, 1, Rgb(120, 60, 50));
		C.R(4, 3, 4, 9, Rgb(232, 212, 162)); C.R(4, 2, 4, 1, Rgb(150, 70, 50));
		C.R(8, 6, 3, 6, Rgb(150, 170, 204)); C.R(8, 5, 3, 1, Rgb(80, 90, 120));
		C.P(1, 7, Win); C.P(2, 9, Win); C.P(5, 5, Win); C.P(6, 7, Win); C.P(5, 9, Win); C.P(9, 8, Win);
		// the lighthouse (right): white with red bands, the light on top
		for (int32 y = 2; y < 12; ++y) { C.R(12, y, 3, 1, (y == 5 || y == 8) ? Rgb(210, 60, 50) : Rgb(246, 244, 236)); }
		C.P(14, 3, Rgb(210, 208, 200)); C.P(14, 6, Rgb(210, 208, 200)); C.P(14, 9, Rgb(210, 208, 200));
		C.R(12, 1, 3, 1, Win); C.P(13, 0, Rgb(80, 60, 50)); C.P(11, 1, Rgb(255, 244, 190)); C.P(15, 1, Rgb(255, 244, 190));
	}

	void PicCave(const FPic& C)
	{
		C.SkyTo(2, Sky, SkyLo);
		const FColor Rock(124, 116, 106), RockHi(156, 148, 136), RockDk(92, 86, 80), Dark(22, 18, 24), Path(170, 146, 110);
		for (int32 y = 1; y < 12; ++y)
		{
			const int32 Inset = y == 1 ? 5 : (y == 2 ? 2 : 0);
			for (int32 x = Inset; x < 16 - Inset; ++x)
			{
				const float N = FVArt::Noise(x / 2, y / 2, 77u);
				C.P(x, y, N < 0.25f ? RockDk : (N > 0.8f ? RockHi : Rock));
			}
		}
		for (int32 y = 5; y < 12; ++y)   // the arched mouth
		{
			const float HW = y == 5 ? 1.5f : (y == 6 ? 2.5f : 3.2f);
			for (int32 x = 0; x < 16; ++x) { if (FMath::Abs(float(x) + 0.5f - 8.f) <= HW) { C.P(x, y, Dark); } }
		}
		C.P(5, 7, RockHi); C.P(4, 9, RockHi);   // the lit edge of the mouth
		C.R(6, 11, 4, 1, Path); C.R(5, 10, 1, 1, RockDk);
	}

	void PicHome(const FPic& C)
	{
		C.SkyTo(7, Sky, SkyLo);
		C.Rows(8, 11, Grass);
		const FColor Wall(232, 204, 154), WallDk(204, 176, 128), Roof(190, 70, 50), RoofDk(150, 50, 40), Fence(236, 214, 170);
		C.R(4, 5, 8, 6, Wall); C.R(10, 5, 2, 6, WallDk);
		for (int32 r = 0; r < 4; ++r) { for (int32 x = 6 - r - 1; x <= 9 + r + 1; ++x) { C.P(x, 1 + r, x >= 8 ? RoofDk : Roof); } }
		C.R(10, 0, 1, 2, Rgb(120, 90, 80));
		C.R(7, 8, 2, 3, Rgb(118, 76, 46)); C.P(5, 6, Rgb(255, 222, 120)); C.P(10, 6, Rgb(255, 222, 120));
		for (int32 x : { 0, 2, 13, 15 }) { C.P(x, 9, Fence); C.P(x, 10, Fence); }
		C.R(0, 9, 3, 1, Fence); C.R(13, 9, 3, 1, Fence);
	}

	void Picture(FFVImage& I, FVWorldU6::EWayPicto Picto)
	{
		using FVWorldU6::EWayPicto;
		const FPic C{ I };
		switch (Picto)
		{
		case EWayPicto::River: PicRiver(C); break;
		case EWayPicto::Forest: PicForest(C); break;
		case EWayPicto::Pond: PicPond(C); break;
		case EWayPicto::Mountain: PicMountain(C); break;
		case EWayPicto::Ocean: PicOcean(C); break;
		case EWayPicto::Swamp: PicSwamp(C); break;
		case EWayPicto::Volcano: PicVolcano(C); break;
		case EWayPicto::Mill: PicMill(C); break;
		case EWayPicto::City: PicCity(C); break;
		case EWayPicto::Cave: PicCave(C); break;
		case EWayPicto::Home:
		default: PicHome(C); break;
		}
	}

	FFVImage BuildSign(uint8 PictoIn, uint8 Dir, bool bLocked)
	{
		FFVImage L(LW, LH);
		auto BoardPx = [&L](int32 X, int32 Y) { L.Set(X, Y, BoardC); };
		// the post (behind the board) with a grass tuft at its foot
		for (int32 Y = BY1 + 1; Y < LH - 1; ++Y) { L.Set(14, Y, PostC); L.Set(15, Y, PostLo); }
		for (int32 X = 12; X <= 17; ++X) { L.Set(X, LH - 1, (X % 2) ? Rgb(90, 150, 70) : Rgb(70, 128, 58)); }
		L.Set(12, LH - 2, Rgb(90, 150, 70)); L.Set(17, LH - 2, Rgb(90, 150, 70));
		// the board: planks, the light top edge, the dark bottom edge
		for (int32 Y = BY0; Y <= BY1; ++Y)
		{
			for (int32 X = BX0; X <= BX1; ++X)
			{
				FColor C = Y == BY0 ? BoardHi : (Y == BY1 ? BoardLo : BoardC);
				if (Y == 10 && X % 5 != 0) { C = FVArt::Mix(C, BoardLo, 0.5f); }   // the joint of the two planks
				L.Set(X, Y, C);
			}
		}
		// the arrow tip
		switch (Dir)
		{
		case 0:   // left
			for (int32 k = 0; k < 4; ++k) { for (int32 Y = 10 - 2 * k; Y <= 11 + 2 * k; ++Y) { BoardPx(k, Y); } }
			L.Set(2, 10, BoardDk); L.Set(2, 11, BoardDk); L.Set(3, 9, BoardDk); L.Set(3, 12, BoardDk);
			break;
		case 1:   // right
			for (int32 k = 0; k < 4; ++k) { for (int32 Y = 10 - 2 * k; Y <= 11 + 2 * k; ++Y) { BoardPx(LW - 1 - k, Y); } }
			L.Set(LW - 3, 10, BoardDk); L.Set(LW - 3, 11, BoardDk); L.Set(LW - 4, 9, BoardDk); L.Set(LW - 4, 12, BoardDk);
			break;
		case 2:   // up: a roof-like tip above the board
			for (int32 X = 9; X <= 20; ++X) { L.Set(X, 2, BoardHi); }
			for (int32 X = 12; X <= 17; ++X) { L.Set(X, 1, BoardHi); }
			L.Set(14, 0, BoardHi); L.Set(15, 0, BoardHi);
			L.Set(14, 1, BoardDk); L.Set(15, 1, BoardDk);
			break;
		default:  // down: a tip below the board, over the post
			for (int32 X = 9; X <= 20; ++X) { L.Set(X, BY1 + 1, BoardLo); }
			for (int32 X = 12; X <= 17; ++X) { L.Set(X, BY1 + 2, BoardLo); }
			L.Set(14, BY1 + 3, BoardLo); L.Set(15, BY1 + 3, BoardLo);
			L.Set(14, BY1 + 1, BoardDk); L.Set(15, BY1 + 1, BoardDk);
			break;
		}
		// the recessed frame of the picture + the picture
		for (int32 X = PX - 1; X <= PX + 16; ++X) { L.Set(X, PY - 1, BoardDk); L.Set(X, PY + 12, BoardHi); }
		for (int32 Y = PY - 1; Y <= PY + 12; ++Y) { L.Set(PX - 1, Y, BoardDk); L.Set(PX + 16, Y, BoardHi); }
		const uint8 NP = uint8(FVWorldU6::EWayPicto::Count);
		Picture(L, FVWorldU6::EWayPicto(PictoIn < NP ? PictoIn : uint8(FVWorldU6::EWayPicto::Home)));
		// nails
		L.Set(BX0 + 1, BY0 + 1, Rgb(90, 80, 80)); L.Set(BX1 - 1, BY0 + 1, Rgb(90, 80, 80));
		L.Set(BX0 + 1, BY1 - 1, Rgb(90, 80, 80)); L.Set(BX1 - 1, BY1 - 1, Rgb(90, 80, 80));
		if (bLocked)
		{
			// a closed place: the picture greyed out, a padlock over its lower right corner
			for (int32 Y = PY; Y < PY + 12; ++Y)
			{
				for (int32 X = PX; X < PX + 16; ++X) { L.Set(X, Y, FVArt::Mix(L.Get(X, Y), Rgb(118, 112, 108), 0.6f)); }
			}
			const FColor Body(226, 184, 70), BodyDk(170, 126, 44), Shackle(176, 178, 188), Ink(70, 44, 28);
			for (int32 Y = 9; Y <= 11; ++Y) { L.Set(20, Y, Shackle); L.Set(24, Y, Shackle); }
			for (int32 X = 21; X <= 23; ++X) { L.Set(X, 8, Shackle); }
			for (int32 Y = 12; Y <= 17; ++Y)
			{
				for (int32 X = 19; X <= 25; ++X)
				{
					const bool bEdge = X == 19 || X == 25 || Y == 12 || Y == 17;
					L.Set(X, Y, bEdge ? Ink : (X >= 23 ? BodyDk : Body));
				}
			}
			L.Set(22, 14, Ink); L.Set(22, 15, Ink);
		}
		// into the 32x32 canvas (room for the outline) + the outline
		FFVImage I(LW + 2, LH + 2);
		I.Blit(L, 1, 1);
		I.Outline(Out);
		return I;
	}

	/** A trodden path, the outer end at the image edge (left for Dir 0 / up for Dir 2), the inner end dithered out. */
	FFVImage BuildPath(uint8 Dir)
	{
		const bool bVert = Dir >= 2;
		const int32 Len = 40, Wd = 14;
		FFVImage I(bVert ? Wd : Len, bVert ? Len : Wd);
		const FColor Dirt(176, 146, 104), DirtLo(152, 122, 86), DirtHi(204, 180, 138), Edge(134, 106, 74);
		for (int32 t = 0; t < Len; ++t)
		{
			const float Mid = float(Wd) * 0.5f + FMath::Sin(float(t) * 0.22f) * 0.9f;
			const float HW = 3.6f + FVArt::Noise(t / 3, 1, 91u) * 1.2f;
			const float Keep = FMath::Clamp(float(Len - 1 - t) / 12.f, 0.f, 1.f);   // the inner end fades out
			for (int32 s = 0; s < Wd; ++s)
			{
				const float D = FMath::Abs(float(s) + 0.5f - Mid);
				if (D > HW + 0.6f) { continue; }
				if (FVArt::Noise(t, s, 93u) > Keep) { continue; }
				const float N = FVArt::Noise(t, s, 95u);
				FColor C = D > HW - 0.6f ? Edge : (N < 0.2f ? DirtLo : (N > 0.9f ? DirtHi : Dirt));
				if (bVert) { I.Set(s, t, C); } else { I.Set(t, s, C); }
			}
		}
		return Dir == 1 ? I.FlippedX() : I;
	}
}

FFVImage UFVSprites::BuildWaySign(uint8 Picto, uint8 Dir, bool bLocked)
{
	return FVArtSignsImpl::BuildSign(Picto, Dir, bLocked);
}

const FFVSprite& UFVSprites::WaySignSprite(uint8 Picto, uint8 Dir, bool bLocked)
{
	const uint64 CacheKey = MakeKey(FVSpriteCat::WaySign, uint32(Picto), uint32(Dir & 3u) | (bLocked ? 4u : 0u), 0);
	if (const FFVSprite* Found = FindCached(CacheKey))
	{
		return *Found;
	}
	return AddCached(CacheKey, FVArtSignsImpl::BuildSign(Picto, Dir & 3u, bLocked), FString::Printf(TEXT("WaySign_%d_%d%s"), int32(Picto), int32(Dir), bLocked ? TEXT("_Locked") : TEXT("")));
}

const FFVSprite& UFVSprites::WayPathSprite(uint8 Dir)
{
	const uint64 CacheKey = MakeKey(FVSpriteCat::WaySign, 0xFFFFu, uint32(Dir & 3u), 1);
	if (const FFVSprite* Found = FindCached(CacheKey))
	{
		return *Found;
	}
	return AddCached(CacheKey, FVArtSignsImpl::BuildPath(Dir & 3u), FString::Printf(TEXT("WayPath_%d"), int32(Dir)));
}
