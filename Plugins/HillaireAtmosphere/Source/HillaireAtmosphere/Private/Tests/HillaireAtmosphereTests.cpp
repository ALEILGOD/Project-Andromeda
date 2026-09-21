#include "Misc/AutomationTest.h"

#include "HillaireAtmosphereProfile.h"
#include "HillaireLightSource.h"
#include "HillaireLimits.h"
#include "HillaireLutManager.h"
#include "HillairePlanetAtmosphereState.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillairePlanetState.h"
#include "HillaireTestFunctions.h"
#include "HillaireUnits.h"
#include "HillaireViewSnapshot.h"

// ---------------------------------------------------------------------------
// Helpers (pure structs, no world needed)
// ---------------------------------------------------------------------------

static FHillairePlanetState MakeTestPlanet(int32 Slot, const FVector& CenterCm, float GroundKm = 6360.0f, float HeightKm = 100.0f)
{
	FHillairePlanetState P;
	P.PlanetId = Slot;
	P.PlanetGuid = FGuid(1000 + Slot, 0, 0, 0);
	P.PlanetName = FName(*FString::Printf(TEXT("TestPlanet%d"), Slot));
	P.CenterCmWorld = CenterCm;
	P.RotationWorld = FQuat::Identity;
	P.Profile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	P.Profile.BottomRadiusKm = GroundKm;
	P.Profile.TopRadiusKm = GroundKm + HeightKm;
	P.GroundRadiusKm = P.Profile.BottomRadiusKm;
	P.AtmosphereRadiusKm = P.Profile.TopRadiusKm;
	P.TerrainHeightKm = 0.0f;
	return P;
}

static FHillaireLightSource MakeTestLight(int32 Index, bool bEnabled, bool bDirectional, const FVector& PosCm = FVector::ZeroVector)
{
	FHillaireLightSource L;
	L.LightId = FGuid(5000 + Index, 0, 0, 0);
	L.LightName = FName(*FString::Printf(TEXT("TestLight%d"), Index));
	L.bEnabled = bEnabled;
	L.bDirectional = bDirectional;
	L.WorldPositionCm = PosCm;
	L.WorldDirectionToLight = FVector(0.0, 0.0, 1.0);
	L.Color = FLinearColor::White;
	L.Intensity = 2.0f;
	L.AngularRadiusRad = 0.004675f;
	L.bDrawDisk = bDirectional;
	return L;
}

/** Test projection: camera at origin looking +X, focal F. */
static FMatrix MakeTestProjection(float Focal)
{
	return FMatrix(
		FPlane(0.0f, 0.0f, 1.0f, 1.0f),
		FPlane(Focal, 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, Focal, 0.0f, 0.0f),
		FPlane(0.0f, 0.0f, 0.0f, 0.0f));
}

// ---------------------------------------------------------------------------
// 1. Atmosphere state: creation, validation, units, determinism
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePhase1ProfileTest,
	"Hillaire.Phase1.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillairePhase1ProfileTest::RunTest(const FString& Parameters)
{
	FHillaireAtmosphereProfile P = FHillaireAtmosphereProfile::MakeReferenceProfile();
	TestTrue(TEXT("Reference profile validates"), P.IsValid());

	FHillaireAtmosphereProfile Broken = P;
	Broken.TopRadiusKm = Broken.BottomRadiusKm - 1.0f;
	FString Error;
	TestFalse(TEXT("Inverted radii rejected"), Broken.IsValid(&Error));
	TestFalse(TEXT("Error message provided"), Error.IsEmpty());

	FHillaireAtmosphereProfile NegSigma = P;
	NegSigma.RayleighScatteringKm.X = -1.0f;
	TestFalse(TEXT("Negative sigma rejected"), NegSigma.IsValid());

	// Determinism: same input -> same hash; any edit -> different hash.
	TestEqual(TEXT("Profile hash deterministic"), P.ComputeContentHash(), P.ComputeContentHash());
	FHillaireAtmosphereProfile Edited = P;
	Edited.MiePhaseG = 0.7f;
	TestNotEqual(TEXT("Profile edit changes hash"), P.ComputeContentHash(), Edited.ComputeContentHash());

	// Units: 1 km == 100000 cm, exactly at the boundary.
	TestEqual(TEXT("CmToKm"), HillaireUnits::CmToKm(100000.0), 1.0);
	TestEqual(TEXT("KmToCm"), HillaireUnits::KmToCm(1.0), 100000.0);
	const FVector3f Rel = HillaireUnits::WorldCmToCameraRelativeKm(FVector(2e9, 0, 0), FVector(1e9, 0, 0));
	TestTrue(TEXT("Camera-relative conversion"), Rel.Equals(FVector3f(10000.0f, 0.0f, 0.0f)));

	return true;
}

// ---------------------------------------------------------------------------
// 2. Light source: enabled/disabled, position, color, intensity, multi
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePhase1LightTest,
	"Hillaire.Phase1.Light",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillairePhase1LightTest::RunTest(const FString& Parameters)
{
	const FVector3f CenterKm(0.0f, 0.0f, 0.0f);

	// Directional: no attenuation, direction rotated to planet-local.
	FHillaireLightSource Dir = MakeTestLight(0, true, true);
	FHillaireResolvedLight RDir = HillaireResolveLightForPlanet(Dir, CenterKm, FVector3f::ZeroVector, FQuat::Identity);
	TestTrue(TEXT("Directional dir preserved (identity Q)"),
		RDir.LightDirLocal.Equals(FVector3f(0.0f, 0.0f, 1.0f)));
	TestTrue(TEXT("Directional no attenuation"),
		RDir.ColorAttenuation.Equals(FVector3f(2.0f, 2.0f, 2.0f)));
	TestTrue(TEXT("Directional keeps disk"), RDir.bDrawDisk && RDir.AngularRadiusRad > 0.0f);

	// Point: Case B from planet center, I/d^2 in km. Light at 1000 km.
	FHillaireLightSource Pt = MakeTestLight(1, true, false, FVector(1000.0 * 100000.0, 0, 0));
	const FVector3f PtRelKm(1000.0f, 0.0f, 0.0f);
	FHillaireResolvedLight RPt = HillaireResolveLightForPlanet(Pt, CenterKm, PtRelKm, FQuat::Identity);
	TestTrue(TEXT("Point dir from center"),
		RPt.LightDirLocal.Equals(FVector3f(1.0f, 0.0f, 0.0f)));
	// 2.0 / 1000^2 = 2e-6.
	TestTrue(TEXT("Point inverse-square in km"),
		FMath::IsNearlyEqual(RPt.ColorAttenuation.X, 2e-6f, 1e-9f));
	TestFalse(TEXT("Point never draws disk"), RPt.bDrawDisk);

	// Disabled: zero contribution regardless of parameters.
	FHillaireLightSource Off = MakeTestLight(2, false, true);
	FHillaireResolvedLight ROff = HillaireResolveLightForPlanet(Off, CenterKm, FVector3f::ZeroVector, FQuat::Identity);
	TestTrue(TEXT("Disabled resolves empty"), ROff.IsEmpty());

	// Degenerate: light at planet center -> empty (no NaN).
	FHillaireLightSource AtCenter = MakeTestLight(3, true, false, FVector::ZeroVector);
	FHillaireResolvedLight RC = HillaireResolveLightForPlanet(AtCenter, CenterKm, CenterKm, FQuat::Identity);
	TestTrue(TEXT("Coincident point light resolves empty"), RC.IsEmpty());

	// Non-identity rotation is applied exactly once (90 deg about Z: +X -> +Y).
	const FQuat Q90(FVector(0, 0, 1), PI / 2.0);
	FHillaireResolvedLight RRot = HillaireResolveLightForPlanet(Dir, CenterKm, FVector3f::ZeroVector, Q90);
	const FVector3f Back = HillaireRotateVec(Q90, RRot.LightDirLocal);
	TestTrue(TEXT("Planet-local transform inverts cleanly"),
		Back.Equals(FVector3f(0.0f, 0.0f, 1.0f), 1e-5f));

	return true;
}

// ---------------------------------------------------------------------------
// 3. N x M: 1x1, 1x2, 2x1, 2x2 + single-primary flag rules
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePhase1NxMTest,
	"Hillaire.Phase1.NxM",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillairePhase1NxMTest::RunTest(const FString& Parameters)
{
	const FVector OriginCm = FVector::ZeroVector;
	const FVector3f CenterKm(0, 0, 0);
	const FQuat Q = FQuat::Identity;

	auto Compact = [&](const TArray<FHillaireLightSource>& Lights)
	{
		return HillaireCompactLightsForPlanet(Lights, OriginCm, CenterKm, Q);
	};

	// 1x1 single directional -> fast path.
	{
		TArray<FHillaireLightSource> L; L.Add(MakeTestLight(0, true, true));
		FHillaireCompactedLights C = Compact(L);
		TestEqual(TEXT("1x1 count"), C.Count, 1);
		TestTrue(TEXT("1x1 single-primary"), C.bSinglePrimary);
		TestEqual(TEXT("1x1 effective"), HillaireEffectiveLightCount(L), 1);
	}
	// 1x2 mixed -> multi path, both resolved.
	{
		TArray<FHillaireLightSource> L;
		L.Add(MakeTestLight(0, true, true));
		L.Add(MakeTestLight(1, true, false, FVector(5e8, 0, 0)));
		FHillaireCompactedLights C = Compact(L);
		TestEqual(TEXT("1x2 count"), C.Count, 2);
		TestFalse(TEXT("1x2 not single-primary"), C.bSinglePrimary);
		TestTrue(TEXT("1x2 tail zero-filled"), C.Lights[2].IsEmpty() && C.Lights[7].IsEmpty());
	}
	// 2x1: same light resolves per planet (translation-invariant direction).
	{
		TArray<FHillaireLightSource> L; L.Add(MakeTestLight(0, true, true));
		FHillaireCompactedLights A = HillaireCompactLightsForPlanet(L, OriginCm, FVector3f(0, 0, 0), Q);
		FHillaireCompactedLights B = HillaireCompactLightsForPlanet(L, OriginCm, FVector3f(5000, 0, 0), Q);
		TestTrue(TEXT("2x1 directional invariant across planets"),
			A.Lights[0].LightDirLocal.Equals(B.Lights[0].LightDirLocal));
		TestTrue(TEXT("2x1 attenuation invariant across planets"),
			A.Lights[0].ColorAttenuation.Equals(B.Lights[0].ColorAttenuation));
	}
	// 2x2: per-planet point attenuation differs by distance.
	{
		TArray<FHillaireLightSource> L;
		L.Add(MakeTestLight(0, true, false, FVector(1e8, 0, 0))); // 1000 km on X
		L.Add(MakeTestLight(1, true, false, FVector(-1e8, 0, 0)));
		FHillaireCompactedLights A = HillaireCompactLightsForPlanet(L, OriginCm, FVector3f(0, 0, 0), Q);
		TestEqual(TEXT("2x2 count"), A.Count, 2);
		TestFalse(TEXT("2x2 not single-primary"), A.bSinglePrimary);
		TestFalse(TEXT("2x2 contributions differ by side"),
			A.Lights[0].LightDirLocal.Equals(A.Lights[1].LightDirLocal));
	}
	// Registry-order rule: slot-0 disabled + slot-1 enabled is NOT fast path.
	{
		TArray<FHillaireLightSource> L;
		L.Add(MakeTestLight(0, false, true));
		L.Add(MakeTestLight(1, true, true));
		FHillaireCompactedLights C = Compact(L);
		TestEqual(TEXT("Swap count"), C.Count, 1);
		TestFalse(TEXT("Slot swap drops fast path"), C.bSinglePrimary);
	}
	// Single POINT light is not the SkyView fast path either.
	{
		TArray<FHillaireLightSource> L;
		L.Add(MakeTestLight(0, true, false, FVector(1e8, 0, 0)));
		FHillaireCompactedLights C = Compact(L);
		TestFalse(TEXT("Single point is not single-primary"), C.bSinglePrimary);
	}
	// Disabled lights are skipped, tail stays zero.
	{
		TArray<FHillaireLightSource> L;
		L.Add(MakeTestLight(0, false, true));
		L.Add(MakeTestLight(1, false, false));
		FHillaireCompactedLights C = Compact(L);
		TestEqual(TEXT("All-disabled count"), C.Count, 0);
		TestFalse(TEXT("All-disabled not single-primary"), C.bSinglePrimary);
	}

	return true;
}

// ---------------------------------------------------------------------------
// 4. Snapshot: determinism, camera-relative, light/planet resolution
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePhase1SnapshotTest,
	"Hillaire.Phase1.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillairePhase1SnapshotTest::RunTest(const FString& Parameters)
{
	const FIntRect Rect(0, 0, 1280, 720);
	const FMatrix View = FMatrix::Identity;
	const FMatrix Proj = MakeTestProjection(1.0f);

	TArray<FHillairePlanetState> Planets;
	// Camera inside planet 0 (center == origin => governing, contains).
	Planets.Add(MakeTestPlanet(0, FVector::ZeroVector));
	// Planet 1: +X at 20000 km, top 6460 -> angular ~0.33 rad, front-facing.
	Planets.Add(MakeTestPlanet(1, FVector(20000.0 * 100000.0, 0, 0)));

	TArray<FHillaireLightSource> Lights;
	Lights.Add(MakeTestLight(0, true, true));

	const FVector OriginCm = FVector::ZeroVector;
	const FVector ViewDir(1, 0, 0);

	FHillaireViewSnapshot A = FHillaireViewSnapshotBuilder::Build(Planets, Lights, OriginCm, View, Proj, Rect, ViewDir);
	FHillaireViewSnapshot B = FHillaireViewSnapshotBuilder::Build(Planets, Lights, OriginCm, View, Proj, Rect, ViewDir);

	TestTrue(TEXT("Snapshot has content"), A.HasAtmosphereContent());
	TestEqual(TEXT("Snapshot deterministic hash"), A.SnapshotHash, B.SnapshotHash);
	TestEqual(TEXT("Governing resolved"), A.GoverningPlanetId, 0);
	// Visible planet 1 + governing planet 0 (governing last).
	TestEqual(TEXT("Planet resolution count"), A.Planets.Num(), 2);
	if (A.Planets.Num() == 2)
	{
		TestTrue(TEXT("Governing drawn last"), A.Planets[1].bIsGoverning);
		TestTrue(TEXT("Visible rect valid"), A.Planets[0].ScreenRect.bValid);
		TestEqual(TEXT("Per-planet light count"), A.Planets[0].ResolvedLights.Count, 1);
		TestTrue(TEXT("Governing fast path"), A.Planets[1].ResolvedLights.bSinglePrimary);
	}
	TestEqual(TEXT("Light resolution count"), A.Lights.Num(), 1);

	// Camera-relative invariance: shifting world AND origin equally keeps every
	// relative vector and resolved light bit-identical. (The snapshot HASH
	// covers the absolute origin by design, so it must differ across
	// translations; determinism of the hash is asserted per-configuration.)
	const FVector Shift(5e9, -3e9, 1e9);
	TArray<FHillairePlanetState> PlanetsShifted = Planets;
	for (FHillairePlanetState& P : PlanetsShifted)
	{
		P.CenterCmWorld += Shift;
	}
	TArray<FHillaireLightSource> LightsShifted = Lights;
	for (FHillaireLightSource& L : LightsShifted)
	{
		L.WorldPositionCm += Shift;
	}
	FHillaireViewSnapshot C = FHillaireViewSnapshotBuilder::Build(
		PlanetsShifted, LightsShifted, OriginCm + Shift, View, Proj, Rect, ViewDir);
	FHillaireViewSnapshot C2 = FHillaireViewSnapshotBuilder::Build(
		PlanetsShifted, LightsShifted, OriginCm + Shift, View, Proj, Rect, ViewDir);
	TestEqual(TEXT("Shifted config deterministic"), C.SnapshotHash, C2.SnapshotHash);
	TestEqual(TEXT("Translation preserves planet count"), A.Planets.Num(), C.Planets.Num());
	TestEqual(TEXT("Translation preserves light count"), A.Lights.Num(), C.Lights.Num());
	TestEqual(TEXT("Translation preserves governing"), A.GoverningPlanetId, C.GoverningPlanetId);
	if (A.Planets.Num() == C.Planets.Num())
	{
		for (int32 i = 0; i < A.Planets.Num(); ++i)
		{
			const FHillaireSnapshotPlanet& PA = A.Planets[i];
			const FHillaireSnapshotPlanet& PC = C.Planets[i];
			TestTrue(TEXT("Camera-relative centers stable"),
				PA.CenterCamRelativeKm.Equals(PC.CenterCamRelativeKm));
			TestEqual(TEXT("View heights stable"), PA.ViewHeightKm, PC.ViewHeightKm);
			TestEqual(TEXT("Resolved counts stable"), PA.ResolvedLights.Count, PC.ResolvedLights.Count);
			for (int32 j = 0; j < PA.ResolvedLights.Count; ++j)
			{
				TestTrue(TEXT("Resolved lights stable"),
					PA.ResolvedLights.Lights[j] == PC.ResolvedLights.Lights[j]);
			}
		}
	}
	if (A.Lights.Num() == C.Lights.Num())
	{
		for (int32 i = 0; i < A.Lights.Num(); ++i)
		{
			TestTrue(TEXT("Camera-relative light positions stable"),
				A.Lights[i].PositionCamRelativeKm.Equals(C.Lights[i].PositionCamRelativeKm));
		}
	}

	// Empty world -> empty snapshot, stable hash.
	TArray<FHillairePlanetState> NoPlanets;
	TArray<FHillaireLightSource> NoLights;
	FHillaireViewSnapshot E = FHillaireViewSnapshotBuilder::Build(NoPlanets, NoLights, OriginCm, View, Proj, Rect, ViewDir);
	TestFalse(TEXT("Empty snapshot has no content"), E.HasAtmosphereContent());

	return true;
}

// ---------------------------------------------------------------------------
// 5. LUT cache: reuse, invalidation, per-planet independence, light targeting
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePhase1LutCacheTest,
	"Hillaire.Phase1.LutCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillairePhase1LutCacheTest::RunTest(const FString& Parameters)
{
	FHillaireLutManager Mgr;
	Mgr.RegisterPlanet(0);
	Mgr.RegisterPlanet(1);
	FHillairePlanetLutState* S0 = Mgr.FindLutState(0);
	FHillairePlanetLutState* S1 = Mgr.FindLutState(1);
	if (!TestTrue(TEXT("LUT slots exist"), S0 && S1))
	{
		return false;
	}

	const uint64 ProfileHash = 0x12345678abcdef01ull;
	const float MsFactor = 1.0f;
	const FVector3f SunLocal(0, 0, 1);
	const FGuid PrimaryId(11, 0, 0, 0);

	// Cold: everything needs generation.
	FHillaireLutRegenQuery Cold = Mgr.QueryRegen(*S0, ProfileHash, MsFactor, SunLocal, true, PrimaryId, 100.0f, true);
	TestTrue(TEXT("Cold transmittance"), Cold.bTransmittance);
	TestTrue(TEXT("Cold multi-scattering"), Cold.bMultiScattering);
	TestTrue(TEXT("Cold skyview"), Cold.bSkyView);

	// Build, then query again: full reuse (the rest-state requirement).
	Mgr.MarkTransmittanceBuilt(*S0, ProfileHash);
	Mgr.MarkMultiScatteringBuilt(*S0, Mgr.MakeMultiScatteringKey(ProfileHash, MsFactor));
	Mgr.MarkSkyViewBuilt(*S0, ProfileHash, 100.0f, FVector3f::ZeroVector);
	FHillaireLutRegenQuery Warm = Mgr.QueryRegen(*S0, ProfileHash, MsFactor, SunLocal, true, PrimaryId, 100.0f, true);
	TestFalse(TEXT("Warm reuse: no regen"), Warm.NeedsAnything());
	Mgr.NoteReuse(*S0);
	TestEqual(TEXT("Reuse counter"), S0->LutReuseCount, 1u);

	// Sub-threshold sun jitter reuses; supra-threshold invalidates SkyView ONLY.
	// NOTE (ATMOS FIX VISIVO DEFINITIVO): the SkyView key is the sun ELEVATION
	// cosine above the planet-local camera up (default up +Z here), not the
	// raw sun vector. Near-zenith moves change elevation slowly (dcos ~ t^2),
	// so the supra-threshold probe uses a 0.05 rad tilt (elevation delta
	// ~1.25e-3 > 1e-3 key delta); the jitter stays far below it.
	const FVector3f Jitter(0.0001f, 0.0f, 1.0f /* ~1e-4 rad, normalized below */);
	const FVector3f JitterN = Jitter.GetSafeNormal();
	FHillaireLutRegenQuery QJitter = Mgr.QueryRegen(*S0, ProfileHash, MsFactor, JitterN, true, PrimaryId, 100.0f, true);
	TestFalse(TEXT("Sub-threshold sun move reuses SkyView"), QJitter.bSkyView);

	const FVector3f Moved(0.05f, 0.0f, 1.0f /* ~0.05 rad, elevation delta ~1.25e-3 */);
	const FVector3f MovedN = Moved.GetSafeNormal();
	FHillaireLutRegenQuery QMoved = Mgr.QueryRegen(*S0, ProfileHash, MsFactor, MovedN, true, PrimaryId, 100.0f, true);
	TestTrue(TEXT("Sun move invalidates SkyView"), QMoved.bSkyView);
	TestFalse(TEXT("Sun move keeps Transmittance"), QMoved.bTransmittance);
	TestFalse(TEXT("Sun move keeps MultiScattering"), QMoved.bMultiScattering);

	// MS factor joins ONLY the MS key (no over-invalidation).
	Mgr.MarkSkyViewBuilt(*S0, ProfileHash, 100.0f, FVector3f::ZeroVector);
	FHillaireLutRegenQuery QMs = Mgr.QueryRegen(*S0, ProfileHash, 2.0f, MovedN, true, PrimaryId, 100.0f, true);
	TestTrue(TEXT("MS factor regenerates MS"), QMs.bMultiScattering);
	TestFalse(TEXT("MS factor keeps SkyView"), QMs.bSkyView);
	TestFalse(TEXT("MS factor keeps Transmittance"), QMs.bTransmittance);

	// Profile edit invalidates planet 0 only (per-planet independence).
	Mgr.MarkTransmittanceBuilt(*S0, ProfileHash);
	Mgr.MarkMultiScatteringBuilt(*S0, Mgr.MakeMultiScatteringKey(ProfileHash, MsFactor));
	Mgr.MarkSkyViewBuilt(*S0, ProfileHash, 100.0f, FVector3f::ZeroVector);
	Mgr.MarkTransmittanceBuilt(*S1, ProfileHash);
	Mgr.MarkMultiScatteringBuilt(*S1, Mgr.MakeMultiScatteringKey(ProfileHash, MsFactor));
	Mgr.MarkSkyViewBuilt(*S1, ProfileHash, 100.0f, FVector3f::ZeroVector);
	Mgr.InvalidatePlanet(*S0);
	TestFalse(TEXT("Planet 0 invalidated"), S0->bTransmittanceValid);
	TestTrue(TEXT("Planet 1 independent"), S1->bTransmittanceValid && S1->bSkyViewValid);

	// Targeted light invalidation: slot swap hits only subscribed planets.
	FHillairePlanetLutState Sub;
	Sub.bSkyViewValid = true;
	const FGuid IdA(21, 0, 0, 0), IdB(22, 0, 0, 0);
	Mgr.NotifyPrimarySlotChanged(Sub, IdA, true, true); // first set records identity
	Sub.bSkyViewValid = true;
	TestFalse(TEXT("Same primary twice: clean"),
		Mgr.NotifyPrimarySlotChanged(Sub, IdA, true, true));
	TestTrue(TEXT("Slot swap invalidates"),
		Mgr.NotifyPrimarySlotChanged(Sub, IdB, true, true));
	TestFalse(TEXT("Swap consumed: valid flag cleared"), Sub.bSkyViewValid);
	Sub.bSkyViewValid = true;
	TestFalse(TEXT("Multi mode: secondary churn ignored"),
		Mgr.NotifyPrimarySlotChanged(Sub, IdA, true, false));

	Mgr.UnregisterPlanet(0);
	TestTrue(TEXT("Unregister drops slot"), Mgr.FindLutState(0) == nullptr);

	return true;
}

// ---------------------------------------------------------------------------
// 6. Resolve-path equality (precursor of the MultiA == Surface blocking gate)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePhase1ResolveParityTest,
	"Hillaire.Phase1.ResolveParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillairePhase1ResolveParityTest::RunTest(const FString& Parameters)
{
	// The future pixel-level gate (multi-light path with one enabled light ==
	// single-primary path) rests on resolve parity: compacting a 1-light
	// registry must equal resolving that light directly. Any deviation here
	// would be a port bug, no exceptions.
	TArray<FHillaireLightSource> Lights;
	Lights.Add(MakeTestLight(0, true, true));

	const FVector OriginCm = FVector::ZeroVector;
	const FVector3f CenterKm(1234.0f, -567.0f, 890.0f);
	const FQuat Q(FVector(0.3, 0.5, 0.2).GetSafeNormal(), 0.7);

	const FHillaireCompactedLights C = HillaireCompactLightsForPlanet(Lights, OriginCm, CenterKm, Q);
	const FHillaireResolvedLight Direct = HillaireResolveLightForPlanet(
		Lights[0], CenterKm, HillaireUnits::WorldCmToCameraRelativeKm(Lights[0].WorldPositionCm, OriginCm), Q);

	TestTrue(TEXT("Compact == direct resolve"), C.Lights[0] == Direct);

	// Scenario table + test-snapshot round trip (MPLN2 analogue).
	TestTrue(TEXT("Scenario table non-empty"),
		UHillaireAtmosphereTestLibrary::GetScenarioTable().Num() >= 6);
	FHillaireScenarioDef Def;
	TestTrue(TEXT("Surface scenario found"),
		UHillaireAtmosphereTestLibrary::FindScenario(FName(TEXT("Surface")), Def));

	FHillaireTestSnapshot Snap;
	Snap.ScenarioName = FName(TEXT("Surface"));
	Snap.CameraPositionCm = FVector(1e8, 2e8, 3e8);
	Snap.CameraRotation = FRotator(10.0, 20.0, 30.0);
	Snap.PlanetCount = 2;
	Snap.EnabledLightCount = 1;
	Snap.SnapshotHash = 0xdeadbeef12345678ull;
	Snap.ProfileHash = 0x0102030405060708ull;
	FHillaireTestSnapshot Back;
	TestTrue(TEXT("Snapshot serializes"), Back.FromString(Snap.ToString()));
	TestTrue(TEXT("Snapshot round-trips"), Back == Snap);
	TestFalse(TEXT("Snapshot rejects garbage"), Back.FromString(TEXT("garbage;;;")));

	return true;
}

// ---------------------------------------------------------------------------
// Hillaire.Phase1.PerPlanetSun - one star, N planets: every planet keeps its
// OWN Planet->Star direction/intensity (no last-writer-wins through the shared
// directional registry entry). Regression test for the inverted-hemisphere
// production bug: the StarLink used to overwrite the single shared star vector
// once per planet, so all but one planet baked/sampled the WRONG sun.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePhase1PerPlanetSunTest,
	"Hillaire.Phase1.PerPlanetSun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillairePhase1PerPlanetSunTest::RunTest(const FString& Parameters)
{
	UHillairePlanetaryAtmosphereSubsystem* Subsystem = NewObject<UHillairePlanetaryAtmosphereSubsystem>();
	if (!TestTrue(TEXT("Subsystem created"), Subsystem != nullptr)) return false;

	// Stable planet-id helper: deterministic, seed-sensitive.
	const FGuid IdCheckA1 = HillaireMakeStablePlanetId(7, 12345);
	const FGuid IdCheckA2 = HillaireMakeStablePlanetId(7, 12345);
	const FGuid IdCheckB = HillaireMakeStablePlanetId(7, 12346);
	TestTrue(TEXT("Stable planet id deterministic"), IdCheckA1 == IdCheckA2);
	TestTrue(TEXT("Stable planet id seed-sensitive"), IdCheckA1 != IdCheckB);

	const FGuid PlanetIdA = FGuid::NewGuid();
	const FGuid PlanetIdB = FGuid::NewGuid();
	const FGuid StarId = FGuid::NewGuid();

	Subsystem->RegisterExternalPlanet(PlanetIdA, FName(TEXT("PlanetA")));
	Subsystem->RegisterExternalPlanet(PlanetIdB, FName(TEXT("PlanetB")));

	FHillaireAtmosphereProfile BaseProfile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	FHillaireAtmosphereProfile NormalizedProfile = HillaireBuildNormalizedProfile(BaseProfile, 500.0f, 100.0f);

	auto MakePlanet = [&](const FGuid& Id, const FName& Name, const FVector& Center, const FQuat& Rot)
	{
		FPlanetAtmosphereState S;
		S.PlanetId = Id;
		S.PlanetName = Name;
		S.CenterWS = Center;
		S.RotationWS = Rot;
		S.Profile = NormalizedProfile;
		S.GroundRadiusKm = NormalizedProfile.BottomRadiusKm;
		S.AtmosphereTopRadiusKm = NormalizedProfile.TopRadiusKm;
		S.TerrainHeightKm = 0.0f;
		S.StarDistanceKm = -1.0f;
		S.StarId = StarId;
		S.bValid = true;
		return S;
	};

	const FQuat RotA = FQuat::Identity;
	const FQuat RotB = FQuat(FVector::UpVector, HALF_PI); // active rotation maps +X -> +Y
	Subsystem->UpdateExternalPlanet(MakePlanet(PlanetIdA, FName(TEXT("PlanetA")), FVector::ZeroVector, RotA));
	Subsystem->UpdateExternalPlanet(MakePlanet(PlanetIdB, FName(TEXT("PlanetB")), FVector(5.0e8, 0.0, 0.0), RotB));

	// ONE shared directional star pointing +X for the whole system.
	FHillaireLightSource Star;
	Star.LightId = StarId;
	Star.LightName = TEXT("SharedStar");
	Star.bEnabled = true;
	Star.bDirectional = true;
	Star.WorldPositionCm = FVector(1.0e10, 0.0, 0.0);
	Star.WorldDirectionToLight = FVector(1.0, 0.0, 0.0);
	Star.Color = FLinearColor::White;
	Star.Intensity = 2.0f;
	Subsystem->RegisterExternalStar(StarId, Star);

	// Per-planet truth: A sees the star along +X, B along +Y (finite distance).
	Subsystem->SetPlanetSunDirection(PlanetIdA, FVector(1.0, 0.0, 0.0), 2.0f);
	Subsystem->SetPlanetSunDirection(PlanetIdB, FVector(0.0, 1.0, 0.0), 3.0f);

	Subsystem->BuildNextFrameSnapshot(
		FVector(0.0, 0.0, 200000.0), // 2 km above A center: inside A
		FMatrix::Identity,
		FMatrix::Identity,
		FIntRect(0, 0, 1920, 1080),
		FVector(1.0, 0.0, 0.0));

	const TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot = Subsystem->GetCurrentFrameSnapshot();
	if (!TestTrue(TEXT("Snapshot published"), Snapshot.IsValid())) return false;

	const FPlanetAtmosphereState* SnapA = nullptr;
	const FPlanetAtmosphereState* SnapB = nullptr;
	for (const FPlanetAtmosphereState& P : Snapshot->Planets)
	{
		if (P.PlanetId == PlanetIdA) SnapA = &P;
		if (P.PlanetId == PlanetIdB) SnapB = &P;
	}
	if (!TestNotNull(TEXT("Planet A in snapshot"), SnapA)) return false;
	if (!TestNotNull(TEXT("Planet B in snapshot"), SnapB)) return false;

	// A: override matches shared values (control).
	TestTrue(TEXT("A world sun = +X"),
		(SnapA->StarDirectionWorld - FVector(1.0, 0.0, 0.0)).Size() < 1e-4);
	TestTrue(TEXT("A local sun = +X (identity rotation)"),
		(SnapA->StarDirectionLocal - FVector3f(1.0f, 0.0f, 0.0f)).Size() < 1e-4f);
	TestTrue(TEXT("A irradiance = 2"),
		FMath::IsNearlyEqual(SnapA->StarIrradiance.X, 2.0f, 1e-4f));

	// B: MUST carry its own +Y world sun, not the shared +X (regression core).
	// Pre-fix this resolves to +X world with irradiance 2.
	TestTrue(TEXT("B world sun = +Y (own Planet->Star, not shared +X)"),
		(SnapB->StarDirectionWorld - FVector(0.0, 1.0, 0.0)).Size() < 1e-4);
	const FVector3f ExpectedLocalB = HillairePlanetMath::WorldDirectionToPlanetLocal(RotB, FVector(0.0, 1.0, 0.0));
	TestTrue(TEXT("B local sun = conj(Qb) * +Y"),
		(SnapB->StarDirectionLocal - ExpectedLocalB).Size() < 1e-4f);
	TestTrue(TEXT("B irradiance = 3 (own intensity)"),
		FMath::IsNearlyEqual(SnapB->StarIrradiance.X, 3.0f, 1e-4f));

	// Override cleanup on unregister (no stale sun for a recycled slot).
	Subsystem->UnregisterExternalPlanet(PlanetIdB);
	Subsystem->RegisterExternalPlanet(PlanetIdB, FName(TEXT("PlanetB")));
	Subsystem->UpdateExternalPlanet(MakePlanet(PlanetIdB, FName(TEXT("PlanetB")), FVector(5.0e8, 0.0, 0.0), RotB));
	Subsystem->BuildNextFrameSnapshot(
		FVector(0.0, 0.0, 200000.0),
		FMatrix::Identity,
		FMatrix::Identity,
		FIntRect(0, 0, 1920, 1080),
		FVector(1.0, 0.0, 0.0));
	const TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot2 = Subsystem->GetCurrentFrameSnapshot();
	const FPlanetAtmosphereState* SnapB2 = nullptr;
	for (const FPlanetAtmosphereState& P : Snapshot2->Planets)
	{
		if (P.PlanetId == PlanetIdB) SnapB2 = &P;
	}
	if (TestNotNull(TEXT("Planet B in second snapshot"), SnapB2))
	{
		TestTrue(TEXT("B falls back to shared +X after re-register (override cleared)"),
			(SnapB2->StarDirectionWorld - FVector(1.0, 0.0, 0.0)).Size() < 1e-4);
	}

	return true;
}
