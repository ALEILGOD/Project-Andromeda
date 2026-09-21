#include "HillaireViewSnapshot.h"

#include "HillaireHash.h"
#include "HillaireLimits.h"
#include "HillaireUnits.h"

FHillaireViewSnapshot FHillaireViewSnapshotBuilder::Build(
	const TArray<FHillairePlanetState>& Planets,
	const TArray<FHillaireLightSource>& Lights,
	const FVector& ViewOriginCm,
	const FMatrix& ViewMatrix,
	const FMatrix& ProjectionMatrix,
	const FIntRect& ViewRect,
	const FVector& ViewDirectionWorld,
	int32 IncumbentPlanetArrayIndex)
{
	FHillaireViewSnapshot Out;
	Out.ViewOriginCm = ViewOriginCm;
	Out.ViewMatrix = ViewMatrix;
	Out.ProjectionMatrix = ProjectionMatrix;
	Out.ViewRect = ViewRect;
	Out.EffectiveLightCount = HillaireEffectiveLightCount(Lights);

	// Camera in its own relative frame is the origin by construction.
	const FVector3f CameraRelKm = FVector3f::ZeroVector;

	// 1. Raw light list (identity + camera-relative positions for the future
	//    multi-light path; per-planet resolved data is baked below).
	Out.Lights.Reserve(Lights.Num());
	for (const FHillaireLightSource& L : Lights)
	{
		FHillaireSnapshotLight S;
		S.LightId = L.LightId;
		S.LightName = L.LightName;
		S.bEnabled = L.bEnabled;
		S.bDirectional = L.bDirectional;
		S.PositionCamRelativeKm = HillaireUnits::WorldCmToCameraRelativeKm(L.WorldPositionCm, ViewOriginCm);
		S.DirectionToLightWorld = L.WorldDirectionToLight;
		S.Color = L.Color;
		S.Intensity = L.Intensity;
		S.AngularRadiusRad = L.AngularRadiusRad;
		S.bDrawDisk = L.bDrawDisk;
		Out.Lights.Add(S);
	}

	if (Planets.Num() == 0)
	{
		Out.SnapshotHash = ComputeSnapshotHash(Out.Planets, Out.Lights, ViewOriginCm, INDEX_NONE);
		return Out;
	}

	// 2. Camera-relative planet centers (double-subtract, then narrow).
	TArray<FHillaireSelectionInput> SelectionInputs;
	SelectionInputs.Reserve(Planets.Num());
	TArray<FVector3f> CentersRel;
	CentersRel.Reserve(Planets.Num());
	for (const FHillairePlanetState& P : Planets)
	{
		const FVector3f C = HillaireUnits::WorldCmToCameraRelativeKm(P.CenterCmWorld, ViewOriginCm);
		CentersRel.Add(C);
		FHillaireSelectionInput In;
		In.CenterCamRelativeKm = C;
		In.TopRadiusKm = P.Profile.TopRadiusKm;
		SelectionInputs.Add(In);
	}

	// 3. Governing + visible resolution (reference policy + incumbent
	// hysteresis: the subsystem-kept governing planet holds its slot across
	// float32 boundary jitter; without an incumbent this is bit-identical
	// to the reference selection).
	const FHillairePlanetSelection Selection = HillaireSelectPlanetsWithIncumbent(
		SelectionInputs, CameraRelKm, ViewDirectionWorld, IncumbentPlanetArrayIndex);

	const FMatrix ViewProj = ViewMatrix * ProjectionMatrix;
	const int32 ViewW = ViewRect.Width();
	const int32 ViewH = ViewRect.Height();

	auto BakePlanet = [&](int32 PlanetIdx, bool bIsGoverning)
	{
		const FHillairePlanetState& P = Planets[PlanetIdx];
		FHillaireSnapshotPlanet S;
		S.PlanetId = P.PlanetId;
		S.PlanetGuid = P.PlanetGuid;
		S.PlanetName = P.PlanetName;
		S.CenterCamRelativeKm = CentersRel[PlanetIdx];
		S.Rotation = P.RotationWorld;
		S.Profile = P.Profile;
		S.GroundRadiusKm = P.GroundRadiusKm;
		S.AtmosphereRadiusKm = P.AtmosphereRadiusKm;
		S.TerrainHeightKm = P.TerrainHeightKm;
		S.ViewHeightKm = HillaireCameraHeightKm(CentersRel[PlanetIdx], CameraRelKm);
		S.DistanceKm = S.ViewHeightKm;
		S.bIsGoverning = bIsGoverning;
		// N x M expansion, GameThread side: every planet resolves every light.
		S.ResolvedLights = HillaireCompactLightsForPlanet(Lights, ViewOriginCm, S.CenterCamRelativeKm, P.RotationWorld);
		if (!bIsGoverning)
		{
			S.ScreenRect = HillaireComputePlanetScreenRect(
				S.CenterCamRelativeKm, P.Profile.TopRadiusKm, ViewProj, ViewW, ViewH);
		}
		return S;
	};

	// Draw order: visibles far-to-near, governing LAST (reference composite).
	for (int32 i = 0; i < Selection.VisibleCount; ++i)
	{
		const int32 Idx = Selection.VisibleIndices[i];
		if (Planets.IsValidIndex(Idx))
		{
			FHillaireSnapshotPlanet S = BakePlanet(Idx, false);
			if (S.ScreenRect.bValid)
			{
				Out.Planets.Add(S);
			}
		}
	}
	if (Selection.GoverningIndex != INDEX_NONE && Planets.IsValidIndex(Selection.GoverningIndex))
	{
		Out.Planets.Add(BakePlanet(Selection.GoverningIndex, true));
		Out.GoverningPlanetId = Planets[Selection.GoverningIndex].PlanetId;
	}

	Out.SnapshotHash = ComputeSnapshotHash(Out.Planets, Out.Lights, ViewOriginCm, Out.GoverningPlanetId);
	return Out;
}

uint64 FHillaireViewSnapshotBuilder::ComputeSnapshotHash(
	const TArray<FHillaireSnapshotPlanet>& Planets,
	const TArray<FHillaireSnapshotLight>& Lights,
	const FVector& ViewOriginCm,
	int32 GoverningPlanetId)
{
	uint64 H = HillaireHash::OffsetBasis;
	H = HillaireHash::HashVector(ViewOriginCm, HillaireLimits::CmPerKm * 1e-3, H);
	H = HillaireHash::HashFloat((float)GoverningPlanetId, 1.0f, H);
	for (const FHillaireSnapshotPlanet& P : Planets)
	{
		H = HillaireHash::HashBytes(&P.PlanetGuid, sizeof(P.PlanetGuid), H);
		H = HillaireHash::HashFloat(P.CenterCamRelativeKm.X, 1e-4f, H);
		H = HillaireHash::HashFloat(P.CenterCamRelativeKm.Y, 1e-4f, H);
		H = HillaireHash::HashFloat(P.CenterCamRelativeKm.Z, 1e-4f, H);
		const uint64 ProfileHash = P.Profile.ComputeContentHash();
		H = HillaireHash::HashBytes(&ProfileHash, sizeof(ProfileHash), H);
		H = HillaireHash::HashFloat(P.ViewHeightKm, 1e-3f, H);
		H = HillaireHash::HashFloat(P.bIsGoverning ? 1.0f : 0.0f, 1.0f, H);
		for (int32 i = 0; i < P.ResolvedLights.Count; ++i)
		{
			const FHillaireResolvedLight& R = P.ResolvedLights.Lights[i];
			H = HillaireHash::HashBytes(&R.LightId, sizeof(R.LightId), H);
			H = HillaireHash::HashFloat(R.LightDirLocal.X, 1e-6f, H);
			H = HillaireHash::HashFloat(R.LightDirLocal.Y, 1e-6f, H);
			H = HillaireHash::HashFloat(R.LightDirLocal.Z, 1e-6f, H);
			H = HillaireHash::HashFloat(R.ColorAttenuation.X, 1e-6f, H);
			H = HillaireHash::HashFloat(R.ColorAttenuation.Y, 1e-6f, H);
			H = HillaireHash::HashFloat(R.ColorAttenuation.Z, 1e-6f, H);
		}
	}
	for (const FHillaireSnapshotLight& L : Lights)
	{
		H = HillaireHash::HashBytes(&L.LightId, sizeof(L.LightId), H);
		H = HillaireHash::HashFloat(L.bEnabled ? 1.0f : 0.0f, 1.0f, H);
		H = HillaireHash::HashFloat(L.PositionCamRelativeKm.X, 1e-4f, H);
		H = HillaireHash::HashFloat(L.PositionCamRelativeKm.Y, 1e-4f, H);
		H = HillaireHash::HashFloat(L.PositionCamRelativeKm.Z, 1e-4f, H);
	}
	return H;
}
