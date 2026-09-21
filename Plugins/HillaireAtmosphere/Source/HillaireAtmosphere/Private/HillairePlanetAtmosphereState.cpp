#include "HillairePlanetAtmosphereState.h"
#include "HillaireLimits.h"
#include "HillaireHash.h"

FVector3f HillairePlanetMath::WorldToPlanetLocalKm(
	const FVector& PlanetCenterWS,
	const FQuat& PlanetRotationWS,
	const FVector& WorldPosWS)
{
	const double DxKm = (WorldPosWS.X - PlanetCenterWS.X) * HillaireLimits::KmPerCm;
	const double DyKm = (WorldPosWS.Y - PlanetCenterWS.Y) * HillaireLimits::KmPerCm;
	const double DzKm = (WorldPosWS.Z - PlanetCenterWS.Z) * HillaireLimits::KmPerCm;
	const FVector3f DeltaKm((float)DxKm, (float)DyKm, (float)DzKm);
	const FQuat QConj = PlanetRotationWS.Inverse();
	
	const FVector3f Qv((float)QConj.X, (float)QConj.Y, (float)QConj.Z);
	const float W = (float)QConj.W;
	const FVector3f Inner = FVector3f::CrossProduct(Qv, DeltaKm) + FVector3f(W * DeltaKm.X, W * DeltaKm.Y, W * DeltaKm.Z);
	const FVector3f Outer = FVector3f::CrossProduct(Qv, Inner);
	return DeltaKm + Outer * 2.0f;
}

FVector HillairePlanetMath::PlanetLocalToWorldWS(
	const FVector& PlanetCenterWS,
	const FQuat& PlanetRotationWS,
	const FVector3f& LocalPosKm)
{
	const FVector3f Qv((float)PlanetRotationWS.X, (float)PlanetRotationWS.Y, (float)PlanetRotationWS.Z);
	const float W = (float)PlanetRotationWS.W;
	const FVector3f Inner = FVector3f::CrossProduct(Qv, LocalPosKm) + FVector3f(W * LocalPosKm.X, W * LocalPosKm.Y, W * LocalPosKm.Z);
	const FVector3f Outer = FVector3f::CrossProduct(Qv, Inner);
	const FVector3f Rotated = LocalPosKm + Outer * 2.0f;
	
	return FVector(
		PlanetCenterWS.X + (double)Rotated.X * HillaireLimits::CmPerKm,
		PlanetCenterWS.Y + (double)Rotated.Y * HillaireLimits::CmPerKm,
		PlanetCenterWS.Z + (double)Rotated.Z * HillaireLimits::CmPerKm);
}

FVector3f HillairePlanetMath::WorldDirectionToPlanetLocal(
	const FQuat& PlanetRotationWS,
	const FVector& WorldDir)
{
	const FVector3f Dir((float)WorldDir.X, (float)WorldDir.Y, (float)WorldDir.Z);
	const FQuat QConj = PlanetRotationWS.Inverse();
	
	const FVector3f Qv((float)QConj.X, (float)QConj.Y, (float)QConj.Z);
	const float W = (float)QConj.W;
	const FVector3f Inner = FVector3f::CrossProduct(Qv, Dir) + FVector3f(W * Dir.X, W * Dir.Y, W * Dir.Z);
	const FVector3f Outer = FVector3f::CrossProduct(Qv, Inner);
	return Dir + Outer * 2.0f;
}

FVector HillairePlanetMath::PlanetLocalDirectionToWorld(
	const FQuat& PlanetRotationWS,
	const FVector3f& LocalDir)
{
	const FVector3f Qv((float)PlanetRotationWS.X, (float)PlanetRotationWS.Y, (float)PlanetRotationWS.Z);
	const float W = (float)PlanetRotationWS.W;
	const FVector3f Inner = FVector3f::CrossProduct(Qv, LocalDir) + FVector3f(W * LocalDir.X, W * LocalDir.Y, W * LocalDir.Z);
	const FVector3f Outer = FVector3f::CrossProduct(Qv, Inner);
	const FVector3f Rotated = LocalDir + Outer * 2.0f;
	return FVector(Rotated.X, Rotated.Y, Rotated.Z);
}

FVector3f HillairePlanetMath::CameraPlanetLocalKm(
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotationWS)
{
	const FVector CamLocal = PlanetRotationWS.UnrotateVector(-FVector(CenterCamRelativeKm));
	return FVector3f((float)CamLocal.X, (float)CamLocal.Y, (float)CamLocal.Z);
}

FVector3f HillairePlanetMath::CameraUpLocal(
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotationWS)
{
	const FVector3f CamLocal = CameraPlanetLocalKm(CenterCamRelativeKm, PlanetRotationWS);
	const float LenSq = CamLocal.SizeSquared();
	if (LenSq < 1e-12f)
	{
		return FVector3f(0.0f, 0.0f, 1.0f);
	}
	const float InvLen = 1.0f / FMath::Sqrt(LenSq);
	return FVector3f(CamLocal.X * InvLen, CamLocal.Y * InvLen, CamLocal.Z * InvLen);
}

float HillairePlanetMath::SunElevationCos(
	const FVector3f& StarDirectionLocal,
	const FVector3f& CameraUpLocal)
{
	const float SunLenSq = StarDirectionLocal.SizeSquared();
	const float UpLenSq = CameraUpLocal.SizeSquared();
	if (SunLenSq < 1e-24f || UpLenSq < 1e-24f)
	{
		return -2.0f;
	}
	const float Inv = 1.0f / (FMath::Sqrt(SunLenSq) * FMath::Sqrt(UpLenSq));
	return FMath::Clamp(
		(StarDirectionLocal.X * CameraUpLocal.X
			+ StarDirectionLocal.Y * CameraUpLocal.Y
			+ StarDirectionLocal.Z * CameraUpLocal.Z) * Inv,
		-1.0f, 1.0f);
}

float HillairePlanetMath::CameraHeightKm(
	const FVector3f& CenterCamRelativeKm,
	const FVector3f& CameraCamRelativeKm)
{
	return (CenterCamRelativeKm - CameraCamRelativeKm).Size();
}

FPlanetSelectionResult HillairePlanetMath::SelectPlanets(
	const TArray<FPlanetSelectionInput>& Planets,
	const FVector3f& CameraCamRelativeKm,
	const FVector& CameraViewDirWS,
	int32 IncumbentGoverningIndex,
	float MinAngularRadiusRad)
{
	FPlanetSelectionResult Out;
	const int32 Count = Planets.Num();
	if (Count <= 0)
	{
		return Out;
	}

	// The camera is a first-class input: planet centers and the camera are both
	// expressed in the same camera-relative km frame, so the camera position
	// must be honored (production passes the origin, but the API is general).
	const FVector3f CameraRelKm = CameraCamRelativeKm;

	// Pass 1: governing - first containing atmosphere wins, else nearest surface
	float BestSurfaceDist = 1e30f;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Dist = CameraHeightKm(Planets[i].CenterCamRelativeKm, CameraRelKm);
		const float Top = Planets[i].TopRadiusKm;
		if (Dist < Top)
		{
			Out.GoverningIndex = i;
			Out.bGoverningContainsCamera = true;
			break;
		}
		const float SurfaceDist = Dist - Top;
		if (SurfaceDist < BestSurfaceDist)
		{
			BestSurfaceDist = SurfaceDist;
			Out.GoverningIndex = i;
		}
	}

	// Hysteresis: if incumbent still contains camera, keep it
	if (IncumbentGoverningIndex != INDEX_NONE && IncumbentGoverningIndex < Count
		&& Out.GoverningIndex != IncumbentGoverningIndex)
	{
		const float DistInc = CameraHeightKm(Planets[IncumbentGoverningIndex].CenterCamRelativeKm, CameraRelKm);
		const float SurfInc = DistInc - Planets[IncumbentGoverningIndex].TopRadiusKm;
		
		if (SurfInc < 0.0f)
		{
			// Incumbent still contains camera - keep it
			Out.GoverningIndex = IncumbentGoverningIndex;
			Out.bGoverningContainsCamera = true;
		}
		else if (Out.GoverningIndex != INDEX_NONE && Out.GoverningIndex < Count)
		{
			// Challenger must beat incumbent by hysteresis margin
			const float DistGov = CameraHeightKm(Planets[Out.GoverningIndex].CenterCamRelativeKm, CameraRelKm);
			const float SurfGov = DistGov - Planets[Out.GoverningIndex].TopRadiusKm;
			const float Margin = FMath::Max(
				HillaireLimits::GoverningHysteresisFloorKm,
				HillaireLimits::GoverningHysteresisRelative * Planets[IncumbentGoverningIndex].TopRadiusKm);
			
			if (SurfInc <= SurfGov + Margin)
			{
				Out.GoverningIndex = IncumbentGoverningIndex;
				Out.bGoverningContainsCamera = false;
			}
		}
	}

	// Pass 2: visible set, far-to-near. Governing never takes a rect.
	FVector View = CameraViewDirWS.GetSafeNormal();

	struct FCandidate { int32 Index; float Dist; };
	TArray<FCandidate> Candidates;
	Candidates.Reserve(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		if (i == Out.GoverningIndex)
		{
			continue;
		}
		const FVector3f Delta = Planets[i].CenterCamRelativeKm - CameraRelKm;
		const float Dist = Delta.Size();
		if (Dist < 1e-4f)
		{
			continue;
		}
		const float AlongView = Delta.X * View.X + Delta.Y * View.Y + Delta.Z * View.Z;
		const float Top = Planets[i].TopRadiusKm;
		if (AlongView + Top < 0.0f)
		{
			continue;
		}
		float Angular = 0.0f;
		if (Dist > Top)
		{
			Angular = FMath::Asin(FMath::Min(1.0f, Top / Dist));
		}
		else
		{
			Angular = PI;
		}
		if (Angular < MinAngularRadiusRad)
		{
			continue;
		}
		Candidates.Add({ i, Dist });
	}

	// Sort far-to-near
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Dist > B.Dist; });

	for (const FCandidate& C : Candidates)
	{
		if (Out.VisibleIndices.Num() >= HILLAIRE_MAX_PLANETS) break;
		Out.VisibleIndices.Add(C.Index);
		Out.VisibleDistancesKm.Add(C.Dist);
	}

	return Out;
}