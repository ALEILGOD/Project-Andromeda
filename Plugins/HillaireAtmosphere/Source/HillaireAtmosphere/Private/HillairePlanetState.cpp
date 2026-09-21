#include "HillairePlanetState.h"

#include "HillaireLimits.h"

bool FHillairePlanetState::IsValid(FString* OutError) const
{
	auto Fail = [&](const TCHAR* Msg) -> bool
	{
		if (OutError)
		{
			*OutError = Msg;
		}
		return false;
	};

	if (PlanetId == INDEX_NONE)
	{
		return Fail(TEXT("PlanetId is not assigned (not registered)."));
	}
	FString ProfileError;
	if (!Profile.IsValid(&ProfileError))
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Invalid profile: %s"), *ProfileError);
		}
		return false;
	}
	if (!FMath::IsNearlyEqual(GroundRadiusKm, Profile.BottomRadiusKm, 1e-3f))
	{
		return Fail(TEXT("GroundRadiusKm diverged from Profile.BottomRadiusKm."));
	}
	if (!FMath::IsNearlyEqual(AtmosphereRadiusKm, Profile.TopRadiusKm, 1e-3f))
	{
		return Fail(TEXT("AtmosphereRadiusKm diverged from Profile.TopRadiusKm."));
	}
	if (TerrainHeightKm < 0.0f || !FMath::IsFinite(TerrainHeightKm))
	{
		return Fail(TEXT("TerrainHeightKm must be finite and non-negative."));
	}
	if (CenterCmWorld.ContainsNaN())
	{
		return Fail(TEXT("CenterCmWorld must be finite."));
	}
	return true;
}

FHillairePlanetState HillaireMakeExternalPlanetState(
	int32 PlanetId,
	const FGuid& PlanetGuid,
	const FName& PlanetName,
	const FVector& CenterCmWorld,
	const FQuat& RotationWorld,
	float GroundRadiusKm,
	float AtmosphereHeightKm,
	float TerrainHeightKm,
	const FHillaireAtmosphereProfile& BaseProfile)
{
	FHillairePlanetState Out;
	Out.PlanetId = PlanetId;
	Out.PlanetGuid = PlanetGuid;
	Out.PlanetName = PlanetName;
	Out.CenterCmWorld = CenterCmWorld;
	Out.RotationWorld = RotationWorld;
	// Single-write radii path via the centralized FASE-2 builder: authoring
	// radii fold into a thickness-normalized profile (authoritative
	// downstream); derived mirrors stay equal by construction. Never assign
	// Profile.Bottom/TopRadiusKm directly here.
	Out.Profile = HillaireBuildNormalizedProfile(BaseProfile, GroundRadiusKm, AtmosphereHeightKm);
	Out.GroundRadiusKm = Out.Profile.BottomRadiusKm;
	Out.AtmosphereRadiusKm = Out.Profile.TopRadiusKm;
	Out.TerrainHeightKm = TerrainHeightKm;
	Out.StarDistanceKm = -1.0f; // directional/infinite (Case A/B scope)
	return Out;
}

FVector3f HillaireRotateVec(const FQuat& Q, const FVector3f& V)
{
	// v' = v + 2*cross(q.xyz, cross(q.xyz,v) + w*v). Identity Q => exact no-op.
	const FVector3f Qv((float)Q.X, (float)Q.Y, (float)Q.Z);
	const float W = (float)Q.W;
	const FVector3f Inner = FVector3f::CrossProduct(Qv, V) + FVector3f(W * V.X, W * V.Y, W * V.Z);
	const FVector3f Outer = FVector3f::CrossProduct(Qv, Inner);
	return V + Outer * 2.0f;
}

FVector3f HillaireWorldToPlanetLocalKm(
	const FVector& CenterCmWorld,
	const FQuat& PlanetRotation,
	const FVector& WorldPosCm)
{
	// Double-precision subtract FIRST (far-field precision), narrow to km,
	// then apply the conjugated planet frame exactly once. The center maps
	// to the origin exactly: rotation can never translate it.
	const double DxKm = (WorldPosCm.X - CenterCmWorld.X) * HillaireLimits::KmPerCm;
	const double DyKm = (WorldPosCm.Y - CenterCmWorld.Y) * HillaireLimits::KmPerCm;
	const double DzKm = (WorldPosCm.Z - CenterCmWorld.Z) * HillaireLimits::KmPerCm;
	const FVector3f DeltaKm((float)DxKm, (float)DyKm, (float)DzKm);
	const FQuat QConj = PlanetRotation.Inverse();
	return HillaireRotateVec(QConj, DeltaKm);
}

FVector HillairePlanetLocalToWorldCm(
	const FVector& CenterCmWorld,
	const FQuat& PlanetRotation,
	const FVector3f& LocalPosKm)
{
	// Exact inverse: rotate forward, convert to cm in double, add center.
	const FVector3f Rotated = HillaireRotateVec(PlanetRotation, LocalPosKm);
	return FVector(
		CenterCmWorld.X + (double)Rotated.X * HillaireLimits::CmPerKm,
		CenterCmWorld.Y + (double)Rotated.Y * HillaireLimits::CmPerKm,
		CenterCmWorld.Z + (double)Rotated.Z * HillaireLimits::CmPerKm);
}

FVector3f HillaireCameraPlanetLocalKm(
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotation)
{
	// Camera sits at the relative origin: camLocal = conj(Q) * (0 - Center).
	// Implemented via UnrotateVector on doubles: bit-identical to the two
	// historical call sites this helper unifies (bake up-vector + aerial
	// view inputs), so unifying changes no validated output.
	const FVector CamLocal = PlanetRotation.UnrotateVector(-FVector(CenterCamRelativeKm));
	return FVector3f((float)CamLocal.X, (float)CamLocal.Y, (float)CamLocal.Z);
}

FVector3f HillaireCameraUpLocal(
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotation)
{
	const FVector3f CamLocal = HillaireCameraPlanetLocalKm(CenterCamRelativeKm, PlanetRotation);
	const float LenSq = CamLocal.SizeSquared();
	if (LenSq < 1e-12f)
	{
		return FVector3f(0.0f, 0.0f, 1.0f);
	}
	const float InvLen = 1.0f / FMath::Sqrt(LenSq);
	return FVector3f(CamLocal.X * InvLen, CamLocal.Y * InvLen, CamLocal.Z * InvLen);
}

float HillaireSunElevationCos(
	const FVector3f& PrimarySunLocalDir,
	const FVector3f& CameraUpLocal)
{
	const float SunLenSq = PrimarySunLocalDir.SizeSquared();
	const float UpLenSq = CameraUpLocal.SizeSquared();
	if (SunLenSq < 1e-24f || UpLenSq < 1e-24f)
	{
		return -2.0f; // degenerate: outside [-1, 1], never equal to a real key
	}
	const float Inv = 1.0f / (FMath::Sqrt(SunLenSq) * FMath::Sqrt(UpLenSq));
	return FMath::Clamp(
		(PrimarySunLocalDir.X * CameraUpLocal.X
			+ PrimarySunLocalDir.Y * CameraUpLocal.Y
			+ PrimarySunLocalDir.Z * CameraUpLocal.Z) * Inv,
		-1.0f, 1.0f);
}

float HillaireCameraHeightKm(
	const FVector3f& CenterCamRelativeKm,
	const FVector3f& CameraCamRelativeKm)
{
	return (CenterCamRelativeKm - CameraCamRelativeKm).Size();
}

FHillairePlanetSelection HillaireSelectPlanets(
	const TArray<FHillaireSelectionInput>& Planets,
	const FVector3f& CameraCamRelativeKm,
	const FVector& CameraViewDirWorld,
	float MinAngularRadiusRad)
{
	FHillairePlanetSelection Out;

	const int32 Count = Planets.Num();
	if (Count <= 0)
	{
		return Out;
	}

	// Pass 1: governing. First containing atmosphere wins (planets must not
	// overlap); otherwise nearest surface.
	float BestSurfaceDist = 1e30f;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Dist = HillaireCameraHeightKm(Planets[i].CenterCamRelativeKm, CameraCamRelativeKm);
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

	// Pass 2: visible set, far-to-near. Governing never takes a rect.
	FVector View = CameraViewDirWorld.GetSafeNormal();

	struct FCandidate { int32 Index; float Dist; };
	FCandidate Candidates[HILLAIRE_MAX_PLANETS];
	int32 CandidateCount = 0;

	for (int32 i = 0; i < Count && CandidateCount < HILLAIRE_MAX_PLANETS; ++i)
	{
		if (i == Out.GoverningIndex)
		{
			continue;
		}
		const FVector3f Delta = Planets[i].CenterCamRelativeKm - CameraCamRelativeKm;
		const float Dist = Delta.Size();
		if (Dist < 1e-4f)
		{
			continue;
		}
		const float AlongView = (float)(Delta.X * View.X + Delta.Y * View.Y + Delta.Z * View.Z);
		const float Top = Planets[i].TopRadiusKm;
		// Generous behind-camera rule: huge nearby spheres still cover the screen.
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
			Angular = PI; // inside: only reachable when not governing
		}
		if (Angular < MinAngularRadiusRad)
		{
			continue;
		}
		Candidates[CandidateCount].Index = i;
		Candidates[CandidateCount].Dist = Dist;
		++CandidateCount;
	}

	// Insertion sort far-to-near (N <= 8, trivial cost).
	for (int32 a = 1; a < CandidateCount; ++a)
	{
		const FCandidate Key = Candidates[a];
		int32 b = a - 1;
		while (b >= 0 && Candidates[b].Dist < Key.Dist)
		{
			Candidates[b + 1] = Candidates[b];
			--b;
		}
		Candidates[b + 1] = Key;
	}

	for (int32 i = 0; i < CandidateCount && Out.VisibleCount < HILLAIRE_MAX_PLANETS; ++i)
	{
		Out.VisibleIndices[Out.VisibleCount] = Candidates[i].Index;
		Out.VisibleDistancesKm[Out.VisibleCount] = Candidates[i].Dist;
		++Out.VisibleCount;
	}
	return Out;
}

namespace
{
	/**
	 * File-local visible-set rebuild for the hysteresis keeper below: the
	 * kept incumbent never takes a rect; every other candidate (base
	 * visibles + the displaced governing planet) passes the same
	 * front-facing/angular tests as the reference pass 2, far-to-near.
	 */
	void HillaireRebuildVisibleExcluding(
		const TArray<FHillaireSelectionInput>& Planets,
		const FVector3f& CameraCamRelativeKm,
		const FVector& CameraViewDirWorld,
		float MinAngularRadiusRad,
		int32 ExcludeIndex,
		const FHillairePlanetSelection& Base,
		FHillairePlanetSelection& Out)
	{
		const FVector View = CameraViewDirWorld.GetSafeNormal();
		struct FCandidate { int32 Index; float Dist; };
		FCandidate Candidates[HILLAIRE_MAX_PLANETS];
		int32 CandidateCount = 0;
		auto TryAdmit = [&](int32 Idx)
		{
			if (Idx == ExcludeIndex || Idx < 0 || Idx >= Planets.Num()
				|| CandidateCount >= HILLAIRE_MAX_PLANETS)
			{
				return;
			}
			const FVector3f Delta = Planets[Idx].CenterCamRelativeKm - CameraCamRelativeKm;
			const float Dist = Delta.Size();
			if (Dist < 1e-4f)
			{
				return;
			}
			const float AlongView = (float)(Delta.X * View.X + Delta.Y * View.Y + Delta.Z * View.Z);
			const float Top = Planets[Idx].TopRadiusKm;
			if (AlongView + Top < 0.0f)
			{
				return;
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
				return;
			}
			Candidates[CandidateCount].Index = Idx;
			Candidates[CandidateCount].Dist = Dist;
			++CandidateCount;
		};
		for (int32 i = 0; i < Base.VisibleCount; ++i)
		{
			TryAdmit(Base.VisibleIndices[i]);
		}
		TryAdmit(Base.GoverningIndex);
		for (int32 a = 1; a < CandidateCount; ++a)
		{
			const FCandidate Key = Candidates[a];
			int32 b = a - 1;
			while (b >= 0 && Candidates[b].Dist < Key.Dist)
			{
				Candidates[b + 1] = Candidates[b];
				--b;
			}
			Candidates[b + 1] = Key;
		}
		for (int32 i = 0; i < CandidateCount && Out.VisibleCount < HILLAIRE_MAX_PLANETS; ++i)
		{
			Out.VisibleIndices[Out.VisibleCount] = Candidates[i].Index;
			Out.VisibleDistancesKm[Out.VisibleCount] = Candidates[i].Dist;
			++Out.VisibleCount;
		}
	}
} // namespace

FHillairePlanetSelection HillaireSelectPlanetsWithIncumbent(
	const TArray<FHillaireSelectionInput>& Planets,
	const FVector3f& CameraCamRelativeKm,
	const FVector& CameraViewDirWorld,
	int32 IncumbentIndex,
	float MinAngularRadiusRad)
{
	FHillairePlanetSelection Base = HillaireSelectPlanets(
		Planets, CameraCamRelativeKm, CameraViewDirWorld, MinAngularRadiusRad);

	const int32 Count = Planets.Num();
	if (IncumbentIndex < 0 || IncumbentIndex >= Count
		|| Base.GoverningIndex == IncumbentIndex)
	{
		return Base; // no incumbent, or incumbent already wins: reference policy
	}

	// Signed surface distances (negative = camera inside that atmosphere).
	const float DistInc = HillaireCameraHeightKm(
		Planets[IncumbentIndex].CenterCamRelativeKm, CameraCamRelativeKm);
	const float SurfInc = DistInc - Planets[IncumbentIndex].TopRadiusKm;

	// An incumbent that still contains the camera keeps the slot outright:
	// containment is absolute (atmospheres must not overlap, so at most one
	// planet contains the camera and the reference pass-1 would agree).
	if (SurfInc < 0.0f)
	{
		if (Base.GoverningIndex == INDEX_NONE)
		{
			return Base;
		}
		FHillairePlanetSelection Out;
		Out.GoverningIndex = IncumbentIndex;
		Out.bGoverningContainsCamera = true;
		// Rebuild the visible set: drop the incumbent, re-admit the
		// displaced governing planet through the standard visibility tests.
		HillaireRebuildVisibleExcluding(Planets, CameraCamRelativeKm,
			CameraViewDirWorld, MinAngularRadiusRad, IncumbentIndex, Base, Out);
		return Out;
	}

	// Outside: the challenger must beat the incumbent's surface distance by
	// more than the hysteresis margin, else the incumbent stays.
	if (Base.GoverningIndex == INDEX_NONE || !Planets.IsValidIndex(Base.GoverningIndex))
	{
		return Base;
	}
	const float DistGov = HillaireCameraHeightKm(
		Planets[Base.GoverningIndex].CenterCamRelativeKm, CameraCamRelativeKm);
	const float SurfGov = DistGov - Planets[Base.GoverningIndex].TopRadiusKm;
	const float Margin = FMath::Max(
		HillaireLimits::GoverningHysteresisFloorKm,
		HillaireLimits::GoverningHysteresisRelative * Planets[IncumbentIndex].TopRadiusKm);
	if (SurfInc <= SurfGov + Margin)
	{
		// Keep the incumbent as governing (the challenger re-enters the rect
		// set only through the standard visibility tests, same as above).
		FHillairePlanetSelection Out;
		Out.GoverningIndex = IncumbentIndex;
		Out.bGoverningContainsCamera = false;
		HillaireRebuildVisibleExcluding(Planets, CameraCamRelativeKm,
			CameraViewDirWorld, MinAngularRadiusRad, IncumbentIndex, Base, Out);
		return Out;
	}
	return Base;
}

FGuid HillaireMakeStablePlanetId(int64 PlanetID, int64 PlanetSeed)
{
	// FNV-1a 64 over the ID bytes then the seed bytes (shift-extracted:
	// endian-independent), folded into A/B. Bit-identical to the historical
	// PlanetLink formula it unifies: existing content keys are preserved.
	uint64 Hash = 14695981039346656037ULL;
	const uint64 ID = (uint64)PlanetID;
	const uint64 Seed = (uint64)PlanetSeed;
	for (int32 i = 0; i < 8; ++i) { Hash ^= (ID >> (i * 8)) & 0xFFULL; Hash *= 1099511628211ULL; }
	for (int32 i = 0; i < 8; ++i) { Hash ^= (Seed >> (i * 8)) & 0xFFULL; Hash *= 1099511628211ULL; }
	return FGuid((uint32)(Hash >> 32), (uint32)(Hash & 0xFFFFFFFF), 0, 0);
}

FHillairePlanetScreenRect HillaireComputePlanetScreenRect(
	const FVector3f& CenterCamRelativeKm,
	float TopRadiusKm,
	const FMatrix& ViewProjectionMatrix,
	int32 ViewWidth,
	int32 ViewHeight)
{
	FHillairePlanetScreenRect Out;
	if (ViewWidth <= 0 || ViewHeight <= 0 || TopRadiusKm <= 0.0f)
	{
		return Out;
	}

	// UE FMatrix uses row-vector convention (v*M), matching the reference
	// row-major row-vector math: clip = M * center expanded explicitly.
	const float Cx = CenterCamRelativeKm.X;
	const float Cy = CenterCamRelativeKm.Y;
	const float Cz = CenterCamRelativeKm.Z;
	const float ClipX = Cx * ViewProjectionMatrix.M[0][0] + Cy * ViewProjectionMatrix.M[1][0] + Cz * ViewProjectionMatrix.M[2][0] + ViewProjectionMatrix.M[3][0];
	const float ClipY = Cx * ViewProjectionMatrix.M[0][1] + Cy * ViewProjectionMatrix.M[1][1] + Cz * ViewProjectionMatrix.M[2][1] + ViewProjectionMatrix.M[3][1];
	const float ClipW = Cx * ViewProjectionMatrix.M[0][3] + Cy * ViewProjectionMatrix.M[1][3] + Cz * ViewProjectionMatrix.M[2][3] + ViewProjectionMatrix.M[3][3];
	if (ClipW <= 0.0f)
	{
		return Out; // behind camera
	}

	const float NdcX = ClipX / ClipW;
	const float NdcY = ClipY / ClipW;
	const float ViewZ = ClipW; // perspective: w == view z

	// True focals are the norms of projection columns 0/1 (rotation columns
	// have unit norm), ALWAYS positive. Using raw M[0][0]/M[1][1] breaks under
	// axis flips (reference Phase 1.1 distant-planet bug); keep the fix.
	const float Fx = FMath::Sqrt(
		ViewProjectionMatrix.M[0][0] * ViewProjectionMatrix.M[0][0] +
		ViewProjectionMatrix.M[1][0] * ViewProjectionMatrix.M[1][0] +
		ViewProjectionMatrix.M[2][0] * ViewProjectionMatrix.M[2][0]);
	const float Fy = FMath::Sqrt(
		ViewProjectionMatrix.M[0][1] * ViewProjectionMatrix.M[0][1] +
		ViewProjectionMatrix.M[1][1] * ViewProjectionMatrix.M[1][1] +
		ViewProjectionMatrix.M[2][1] * ViewProjectionMatrix.M[2][1]);

	constexpr float FarNdc = 2.0f; // camera inside: cover everything
	const float RNdcX = (ViewZ > TopRadiusKm) ? (TopRadiusKm * Fx / ViewZ) : FarNdc;
	const float RNdcY = (ViewZ > TopRadiusKm) ? (TopRadiusKm * Fy / ViewZ) : FarNdc;
	const float Pad = HillaireLimits::ScreenRectPad;

	const float MinX = (NdcX - RNdcX * Pad) * 0.5f * (float)ViewWidth + 0.5f * (float)ViewWidth;
	const float MaxX = (NdcX + RNdcX * Pad) * 0.5f * (float)ViewWidth + 0.5f * (float)ViewWidth;
	// NDC Y up vs pixel Y down.
	const float MinY = (1.0f - (NdcY + RNdcY * Pad)) * 0.5f * (float)ViewHeight;
	const float MaxY = (1.0f - (NdcY - RNdcY * Pad)) * 0.5f * (float)ViewHeight;

	if (MaxX < 0.0f || MinX >= (float)ViewWidth || MaxY < 0.0f || MinY >= (float)ViewHeight)
	{
		return Out; // fully off-screen
	}

	Out.bValid = true;
	Out.Rect = FIntRect(
		FMath::Clamp(FMath::FloorToInt(MinX), 0, ViewWidth),
		FMath::Clamp(FMath::FloorToInt(MinY), 0, ViewHeight),
		FMath::Clamp(FMath::CeilToInt(MaxX), 0, ViewWidth),
		FMath::Clamp(FMath::CeilToInt(MaxY), 0, ViewHeight));
	const float Dist = CenterCamRelativeKm.Size();
	Out.DistanceKm = Dist;
	Out.AngularRadiusRad = (Dist > TopRadiusKm) ? FMath::Asin(FMath::Min(1.0f, TopRadiusKm / Dist)) : PI;
	return Out;
}
