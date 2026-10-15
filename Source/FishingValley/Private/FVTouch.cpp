#include "FVTouch.h"
#include "FVDraw.h"

namespace FVTouchLocal
{
	constexpr float JoyRadius = 30.f;      // knob travel (virtual px)
	constexpr float JoyDeadZone = 0.15f;
	constexpr float MoveThreshold = 9.f;   // finger travel that turns a touch into a swipe (walk)
	constexpr float HoldDelay = 0.22f;     // a still finger held this long becomes the action (cast wind-up)
	constexpr float TapMaxAge = 0.35f;     // a still finger lifted sooner than this is a world tap (objects)
}

bool FVTouchControls::IsUiArea(const FVector2D& P)
{
	return P.Y < FV::TopBarH || (FV::IsPortrait() && P.Y > FV::VH - FV::BottomBarH);
}

bool FVTouchControls::IsWorldFingerDown() const
{
	return Tracks.ContainsByPredicate([](const FTrack& T) { return T.Role != ERole::Pointer; });
}

void FVTouchControls::Process(FFVInput& In, float Dt, bool bWorldControls, bool bFishingIdle, bool bCanCast)
{
	using namespace FVTouchLocal;

	// The pointer is rebuilt from the fingers: forget whatever the mouse said.
	In.bLmbDown = false;
	In.bLmbPressed = false;
	In.bLmbReleased = false;
	In.bRmbPressed = false;
	In.bMouseValid = false;
	In.Mouse = FVector2D(-1000.f, -1000.f);

	bool bActionPressed = false, bActionReleased = false;

	// ---- lifted fingers
	for (int32 i = Tracks.Num() - 1; i >= 0; --i)
	{
		const FTrack& T = Tracks[i];
		if (In.Touches.ContainsByPredicate([&T](const FFVTouchPoint& P) { return P.Id == T.Id; }))
		{
			continue;
		}
		switch (T.Role)
		{
		case ERole::Pointer:
			// Release at the last known position so widgets see where the finger left.
			In.bLmbReleased = true;
			In.Mouse = T.Pos;
			In.bMouseValid = true;
			break;
		case ERole::Action:
			bActionReleased = true;
			break;
		case ERole::Joystick:
			bJoyActive = false;
			break;
		case ERole::Pending:
			// A short still tap on the world: UFVGame uses it for world objects (pickups, stalls, stations...).
			if (T.Age < TapMaxAge && FVector2D::Distance(T.Pos, T.Start) <= MoveThreshold)
			{
				In.bWorldTap = true;
				In.WorldTapPos = T.Pos;
			}
			break;
		}
		Tracks.RemoveAt(i);
	}

	// ---- current fingers
	for (const FFVTouchPoint& P : In.Touches)
	{
		FTrack* T = Tracks.FindByPredicate([&P](const FTrack& X) { return X.Id == P.Id; });
		if (!T)
		{
			FTrack NewT;
			NewT.Id = P.Id;
			NewT.Start = P.Pos;
			NewT.Pos = P.Pos;
			const bool bHasWorldFinger = IsWorldFingerDown();
			const bool bHasPointer = Tracks.ContainsByPredicate([](const FTrack& X) { return X.Role == ERole::Pointer; });
			if (bWorldControls && !bHasWorldFinger && !IsUiAt(P.Pos))
			{
				if (bFishingIdle)
				{
					NewT.Role = ERole::Pending;          // swipe or hold? decided below
				}
				else
				{
					NewT.Role = ERole::Action;           // hook, reel, continue...
					bActionPressed = true;
				}
			}
			else if (!bHasPointer && (!bWorldControls || IsUiAt(P.Pos)))
			{
				NewT.Role = ERole::Pointer;
				In.bLmbPressed = true;
			}
			else
			{
				continue;   // an extra finger on the world while another one walks/fishes: ignore it
			}
			T = &Tracks.Add_GetRef(NewT);
		}
		T->Pos = P.Pos;
		T->Age += Dt;

		// Pending: swipe -> joystick, still hold by the water -> action.
		if (T->Role == ERole::Pending)
		{
			if (!bWorldControls)
			{
				T->Role = ERole::Pointer;   // an overlay opened under the finger
			}
			else if (FVector2D::Distance(T->Pos, T->Start) > MoveThreshold)
			{
				T->Role = ERole::Joystick;
				bJoyActive = true;
				JoyOrigin = T->Start;
				JoyKnob = T->Pos;
			}
			else if (T->Age >= HoldDelay && bCanCast && !(CastBlocker && CastBlocker(T->Pos)))
			{
				T->Role = ERole::Action;
				bActionPressed = true;
			}
		}

		switch (T->Role)
		{
		case ERole::Pointer:
			// Drags are read by the widgets themselves (kinetic scroll views, swipe pagers): no wheel emulation.
			In.Mouse = T->Pos;
			In.bMouseValid = true;
			In.bLmbDown = true;
			break;
		case ERole::Joystick:
		{
			FVector2D Delta = T->Pos - JoyOrigin;
			const float Len = Delta.Size();
			if (Len > JoyRadius * 1.6f)
			{
				// Keep the anchor within reach so turning around stays responsive.
				JoyOrigin = T->Pos - Delta / Len * JoyRadius * 1.6f;
				Delta = T->Pos - JoyOrigin;
			}
			JoyKnob = JoyOrigin + Delta.GetClampedToMaxSize(JoyRadius);
			FVector2D V = Delta / JoyRadius;
			if (V.Size() > 1.f) { V = V.GetSafeNormal(); }
			if (V.Size() < JoyDeadZone) { V = FVector2D::ZeroVector; }
			if (bWorldControls && bFishingIdle)
			{
				if (FMath::Abs(V.X) > FMath::Abs(In.MoveX)) { In.MoveX = float(V.X); }
				if (FMath::Abs(V.Y) > FMath::Abs(In.MoveY)) { In.MoveY = float(V.Y); }
			}
			break;
		}
		case ERole::Pending:
		case ERole::Action:
			break;
		}
	}

	const bool bActionHeld = Tracks.ContainsByPredicate([](const FTrack& X) { return X.Role == ERole::Action; });
	In.bActionDown = In.bActionDown || bActionHeld;
	In.bActionPressed = In.bActionPressed || bActionPressed;
	In.bActionReleased = In.bActionReleased || (bActionReleased && !bActionHeld);

	JoyFade = bJoyActive ? 1.f : FMath::Max(0.f, JoyFade - Dt * 5.f);
}

void FVTouchControls::Draw(FVDraw& D, bool bWorldControls)
{
	using namespace FVTouchLocal;
	if (!bWorldControls || JoyFade <= 0.01f)
	{
		return;
	}
	const float A = JoyFade;
	D.Circle(JoyOrigin, JoyRadius + 6.f, FV::Col(20, 14, 10, uint8(A * 90.f)));
	D.Ring(JoyOrigin, JoyRadius + 6.f, JoyRadius + 6.f, FV::Col(255, 244, 220, uint8(A * 170.f)), 1.5f);
	D.Circle(JoyKnob, 13.f, FV::Col(249, 218, 160, uint8(A * 220.f)));
	D.Ring(JoyKnob, 13.f, 13.f, FV::Col(92, 44, 22, uint8(A * 230.f)), 1.5f);
}
