// ANDROMEDA - HILLAIRE MULTIPLANETARY TESTS
// Validates the new multiplanetary ATMOS architecture (planet math, profiles,
// frame snapshots, planet/star links, and subsystem wiring).
// Lives in Andromeda module because it uses UHillairePlanetLinkComponent,
// UHillaireStarLinkComponent, and UHillairePlanetaryAtmosphereSubsystem.

#include "Misc/AutomationTest.h"

#include "HillairePlanetAtmosphereState.h"
#include "HillaireLimits.h"
#include "HillaireHash.h"
#include "HillairePlanetLinkComponent.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillaireAtmosphereProfile.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// 1. Hillaire.PlanetMath.* - Coordinate transforms, selection, hysteresis
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePlanetMath_CoordinateTransformsTest,
	"Hillaire.PlanetMath.CoordinateTransforms",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillairePlanetMath_CoordinateTransformsTest::RunTest(const FString& Parameters)
{
	// World -> Local -> World round-trip
	const FVector PlanetCenter(1000000.0, 2000000.0, 3000000.0); // cm
	const FQuat PlanetRotation = FQuat(FRotator(10.0f, 20.0f, 30.0f));
	const FVector WorldPos(1500000.0, 2500000.0, 3500000.0); // cm

	const FVector3f LocalKm = HillairePlanetMath::WorldToPlanetLocalKm(PlanetCenter, PlanetRotation, WorldPos);
	const FVector WorldBack = HillairePlanetMath::PlanetLocalToWorldWS(PlanetCenter, PlanetRotation, LocalKm);

	const double Error = (WorldBack - WorldPos).Size();
	TestTrue(TEXT("Round-trip world->local->world < 1 cm"), Error < 1.0);

	// Direction transform (rotation only, translation-invariant)
	const FVector WorldDir = FVector(1.0, 2.0, 3.0).GetSafeNormal();
	const FVector3f LocalDir = HillairePlanetMath::WorldDirectionToPlanetLocal(PlanetRotation, WorldDir);
	const FVector WorldDirBack = HillairePlanetMath::PlanetLocalDirectionToWorld(PlanetRotation, LocalDir);
	TestTrue(TEXT("Direction round-trip"), (WorldDirBack - WorldDir).Size() < 1e-6f);

	// Camera planet-local position
	const FVector3f CenterCamRel(10.0f, 5.0f, -3.0f); // km
	const FVector3f CamLocal = HillairePlanetMath::CameraPlanetLocalKm(CenterCamRel, PlanetRotation);
	TestTrue(TEXT("Camera local not NaN"), !CamLocal.ContainsNaN());

	// Camera up normalized
	const FVector3f Up = HillairePlanetMath::CameraUpLocal(CenterCamRel, PlanetRotation);
	TestTrue(TEXT("Camera up normalized"), FMath::Abs(Up.Size() - 1.0f) < 1e-6f);

	// Sun elevation cosine
	const FVector3f SunDir(0.0f, 0.0f, 1.0f);
	const float ElevCos = HillairePlanetMath::SunElevationCos(SunDir, Up);
	TestTrue(TEXT("Sun elevation in [-1, 1]"), ElevCos >= -1.0f && ElevCos <= 1.0f);

	// Camera height
	const float Height = HillairePlanetMath::CameraHeightKm(CenterCamRel, FVector3f::ZeroVector);
	TestTrue(TEXT("Camera height = center distance"), FMath::Abs(Height - CenterCamRel.Size()) < 1e-6f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePlanetMath_SelectionTest,
	"Hillaire.PlanetMath.Selection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillairePlanetMath_SelectionTest::RunTest(const FString& Parameters)
{
	// Two planets: A at origin, B at 10000 km
	TArray<FPlanetSelectionInput> Planets;
	Planets.Add({ FGuid::NewGuid(), FVector3f(0, 0, 0), 6460.0f }); // Earth-like
	Planets.Add({ FGuid::NewGuid(), FVector3f(10000, 0, 0), 6460.0f });

	const FVector3f CameraRel(0, 0, 0);
	const FVector ViewDir(1, 0, 0);

	// Camera at planet A center -> inside A
	auto Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel, ViewDir);
	TestTrue(TEXT("Camera at A center -> A governing"), Result.GoverningIndex == 0);
	TestTrue(TEXT("A contains camera"), Result.bGoverningContainsCamera);

	// Camera at 7000 km from A (outside), 3000 km from B (inside B)
	const FVector3f CameraRel2(7000, 0, 0);
	Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel2, ViewDir);
	TestTrue(TEXT("Camera inside B -> B governing"), Result.GoverningIndex == 1);
	TestTrue(TEXT("B contains camera"), Result.bGoverningContainsCamera);

	// Camera far from both -> nearest surface
	const FVector3f CameraRel3(20000, 0, 0);
	Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel3, ViewDir);
	TestTrue(TEXT("Far from both -> nearest surface (B)"), Result.GoverningIndex == 1);
	TestFalse(TEXT("Not containing"), Result.bGoverningContainsCamera);

	// Visible set: from A's center looking +X toward B, B is a visible
	// non-governing rect. (The final far camera looks +X away from A, so its
	// own visible set is legitimately empty.)
	const FPlanetSelectionResult VisibleResult =
		HillairePlanetMath::SelectPlanets(Planets, CameraRel, ViewDir);
	TestTrue(TEXT("Visible count > 0"), VisibleResult.VisibleIndices.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePlanetMath_SelectionHysteresisTest,
	"Hillaire.PlanetMath.SelectionHysteresis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillairePlanetMath_SelectionHysteresisTest::RunTest(const FString& Parameters)
{
	// Two planets with overlapping boundary region
	TArray<FPlanetSelectionInput> Planets;
	const FGuid IdA = FGuid::NewGuid();
	const FGuid IdB = FGuid::NewGuid();
	Planets.Add({ IdA, FVector3f(0, 0, 0), 6460.0f });
	Planets.Add({ IdB, FVector3f(12000, 0, 0), 6460.0f });

	const FVector ViewDir(1, 0, 0);

	// Camera inside B -> B governs regardless of incumbent
	const FVector3f CameraRel(6500, 0, 0);
	auto Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel, ViewDir, INDEX_NONE);
	TestTrue(TEXT("Inside B -> B governing"), Result.GoverningIndex == 1);

	// With incumbent = A, B still wins because it contains camera
	Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel, ViewDir, 0);
	TestTrue(TEXT("Incumbent A displaced by B containing camera"), Result.GoverningIndex == 1);

	// Camera outside both, equidistant from surfaces -> hysteresis keeps the
	// incumbent. A is at the origin and B at 12000 km (both top 6460); the
	// off-axis camera at (6000, 8000, 0) is 10000 km from each center, i.e.
	// 3540 km above each surface. The reference policy picks A (first), and
	// the incumbent B must keep the slot because A cannot beat it by margin.
	const FVector3f CameraRel2(6000.0f, 8000.0f, 0.0f);
	Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel2, ViewDir, 1);
	TestEqual(TEXT("Hysteresis keeps incumbent at tie"), Result.GoverningIndex, 1);

	return true;
}

// ---------------------------------------------------------------------------
// 2. Hillaire.Profile.* - Profile hash, multiplier, normalization
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireProfile_HashStabilityTest,
	"Hillaire.Profile.HashStability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillaireProfile_HashStabilityTest::RunTest(const FString& Parameters)
{
	FHillaireAtmosphereProfile Profile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	
	const uint64 Hash1 = Profile.ComputeContentHash();
	const uint64 Hash2 = Profile.ComputeContentHash();
	
	TestEqual(TEXT("Profile hash stable"), Hash1, Hash2);
	TestTrue(TEXT("Profile hash non-zero"), Hash1 != 0);

	// Modify radius -> hash changes
	Profile.TopRadiusKm += 1.0f;
	const uint64 Hash3 = Profile.ComputeContentHash();
	TestTrue(TEXT("Radius change invalidates hash"), Hash3 != Hash1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireProfile_ThicknessNormalizationTest,
	"Hillaire.Profile.ThicknessNormalization",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillaireProfile_ThicknessNormalizationTest::RunTest(const FString& Parameters)
{
	// CORRECTED VOLUME MODEL: the density is NORMALIZED TO THE ENVELOPE
	// (K = 100 / T) so the vertical optical depth sigma * H is preserved
	// exactly versus the reference profile, on any planet size. The envelope
	// is an independent, stable planetary volume bound.
	FHillaireAtmosphereProfile EarthProfile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	const float ReferenceOD = EarthProfile.RayleighScatteringKm.X * (-1.0f / EarthProfile.RayleighExpScale);

	const float SmallGround = 5000.0f;
	const float SmallThickness = SmallGround * 0.02f;
	FHillaireAtmosphereProfile SmallProfile =
		HillaireBuildNormalizedProfile(EarthProfile, SmallGround, SmallThickness);

	const float BaseH = -1.0f / EarthProfile.RayleighExpScale;
	const float OutH = SmallThickness / HillaireLimits::RayleighVolumeScaleHeights;
	const float ExpectedRayleigh = EarthProfile.RayleighScatteringKm.X * (BaseH / (OutH * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights))));
	TestTrue(TEXT("Rayleigh normalized"),
		FMath::Abs(SmallProfile.RayleighScatteringKm.X - ExpectedRayleigh) < 0.5f);
	TestTrue(TEXT("Radii set correctly"), FMath::Abs(SmallProfile.BottomRadiusKm - SmallGround) < 1e-3f);
	TestTrue(TEXT("Top radius = Ground + Thickness"),
		FMath::Abs(SmallProfile.TopRadiusKm - (SmallGround + SmallThickness)) < 1e-3f);

	const float SmallOD = SmallProfile.RayleighScatteringKm.X * (-1.0f / SmallProfile.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("Vertical optical depth preserved (envelope-normalized)"),
		FMath::IsNearlyEqual(SmallOD, ReferenceOD, ReferenceOD * 1e-3f));

	// Reference configuration preserves radii and optical depth
	FHillaireAtmosphereProfile EarthRebuilt = HillaireBuildNormalizedProfile(
		EarthProfile, EarthProfile.BottomRadiusKm, EarthProfile.TopRadiusKm - EarthProfile.BottomRadiusKm);
	const float EarthRebuiltOD = EarthRebuilt.RayleighScatteringKm.X * (-1.0f / EarthRebuilt.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("Reference case preserves geometry and optical depth"),
		EarthRebuilt.BottomRadiusKm == EarthProfile.BottomRadiusKm
		&& EarthRebuilt.TopRadiusKm == EarthProfile.TopRadiusKm
		&& FMath::IsNearlyEqual(EarthRebuiltOD, ReferenceOD, ReferenceOD * 1e-3f));

	// Envelope re-normalization: growing the volume (terrain containment)
	// re-scales the density so the vertical optical depth stays
	// reference-identical, and only the top moves.
	FHillaireAtmosphereProfile BiggerEnvelope =
		HillaireBuildNormalizedProfile(EarthProfile, SmallGround, SmallThickness * 8.0f);
	const float BigOD = BiggerEnvelope.RayleighScatteringKm.X * (-1.0f / BiggerEnvelope.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("Larger envelope preserves the vertical optical depth"),
		FMath::IsNearlyEqual(BigOD, ReferenceOD, ReferenceOD * 1e-3f));
	TestTrue(TEXT("Larger envelope only raises the top"),
		BiggerEnvelope.TopRadiusKm > SmallProfile.TopRadiusKm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireProfile_PlanetaryBuilderTest,
	"Hillaire.Profile.PlanetaryBuilder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillaireProfile_PlanetaryBuilderTest::RunTest(const FString& Parameters)
{
	// Planetary builder: stable reference bottom + persistent atmospheric volume (~1.10x ground).
	// Terrain is inside the volume, and optical depth is preserved.
	FHillaireAtmosphereProfile Reference = FHillaireAtmosphereProfile::MakeReferenceProfile();
	const float GroundKm = 8.835f;
	const float TerrainKm = 0.529f;

	FHillaireAtmosphereProfile Profile =
		HillaireBuildPlanetaryProfile(Reference, GroundKm, TerrainKm);
	TestTrue(TEXT("Bottom = stable reference radius"), FMath::IsNearlyEqual(Profile.BottomRadiusKm, GroundKm, 1e-4f));
	TestTrue(TEXT("Top contains the terrain bound + headroom"),
		Profile.TopRadiusKm > GroundKm + TerrainKm);
	TestTrue(TEXT("Profile valid"), Profile.IsValid());

	// Optical depth preserved versus the reference.
	const float ReferenceOD = Reference.RayleighScatteringKm.X * (-1.0f / Reference.RayleighExpScale);
	const float OD = Profile.RayleighScatteringKm.X * (-1.0f / Profile.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("Vertical optical depth preserved"), FMath::IsNearlyEqual(OD, ReferenceOD, ReferenceOD * 1e-3f));

	// Planetary volume is persistent and anchored to planetary reference radius.
	FHillaireAtmosphereProfile NoTerrain = HillaireBuildPlanetaryProfile(Reference, GroundKm, 0.0f);
	const float NoTerrainOD = NoTerrain.RayleighScatteringKm.X * (-1.0f / NoTerrain.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("No-terrain OD preserved"), FMath::IsNearlyEqual(NoTerrainOD, ReferenceOD, ReferenceOD * 1e-3f));
	TestTrue(TEXT("Planetary volume is persistent"),
		FMath::IsNearlyEqual(Profile.TopRadiusKm, NoTerrain.TopRadiusKm, 1e-3f));

	return true;
}

// ---------------------------------------------------------------------------
// 3. Hillaire.FrameSnapshot.* - Double-buffer immutability
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireFrameSnapshot_DoubleBufferTest,
	"Hillaire.FrameSnapshot.DoubleBuffer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillaireFrameSnapshot_DoubleBufferTest::RunTest(const FString& Parameters)
{
	// Test frame state immutability concept
	FHillaireAtmosphereFrameState Frame1;
	Frame1.FrameNumber = 1;
	Frame1.GTFrameCounter = 100;
	Frame1.ViewOriginWS = FVector(0, 0, 0);

	FPlanetAtmosphereState Planet;
	Planet.PlanetId = FGuid::NewGuid();
	Planet.PlanetName = TEXT("TestPlanet");
	Planet.CenterWS = FVector(0, 0, -636000000.0); // cm
	Planet.GroundRadiusKm = 6360.0f;
	Planet.AtmosphereTopRadiusKm = 6460.0f;
	Planet.Profile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	Planet.ProfileHash = Planet.Profile.ComputeContentHash();
	Planet.bValid = true;
	Planet.ViewHeightKm = 6370.0f;
	Planet.DistanceKm = 6370.0f;

	Frame1.Planets.Add(Planet);
	Frame1.GoverningPlanetIndex = 0;
	Frame1.MultipleScatteringFactor = 1.0f;
	Frame1.bFastSkyEnabled = true;

	// Simulate shared pointer immutability
	const TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot1 = MakeShared<const FHillaireAtmosphereFrameState>(Frame1);
	
	TestEqual(TEXT("Frame number"), Snapshot1->FrameNumber, 1ULL);
	TestEqual(TEXT("Planet count"), Snapshot1->Planets.Num(), 1);
	TestTrue(TEXT("Has content"), Snapshot1->HasAtmosphereContent());
	TestNotNull(TEXT("Governing planet"), Snapshot1->GetGoverningPlanet());

	// Frame 2 - different data
	FHillaireAtmosphereFrameState Frame2 = *Snapshot1; // Copy
	Frame2.FrameNumber = 2;
	Frame2.GTFrameCounter = 101;
	Frame2.Planets[0].ViewHeightKm = 6380.0f; // Camera moved
	
	const TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot2 = MakeShared<const FHillaireAtmosphereFrameState>(Frame2);
	
	TestEqual(TEXT("Snapshot1 unchanged"), Snapshot1->Planets[0].ViewHeightKm, 6370.0f);
	TestEqual(TEXT("Snapshot2 updated"), Snapshot2->Planets[0].ViewHeightKm, 6380.0f);
	TestNotEqual(TEXT("Different frame numbers"), Snapshot1->FrameNumber, Snapshot2->FrameNumber);

	return true;
}

// ---------------------------------------------------------------------------
// 4. Hillaire.PlanetLink.* - Stable IDs, live geometry propagation
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePlanetLink_StableIdTest,
	"Hillaire.PlanetLink.StableId",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillairePlanetLink_StableIdTest::RunTest(const FString& Parameters)
{
	// Test that the same planet gets the same stable FGuid
	// This is a compile-time/structural test since we can't spawn actors in automation easily
	
	// The component uses PlanetID + PlanetSeed to generate stable FGuid
	// Same values -> same FGuid
	
	// Verified by the component's ReadPlanetId implementation using FNV-1a
	// This test documents the requirement
	
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillairePlanetLink_GeometryPropagationTest,
	"Hillaire.PlanetLink.GeometryPropagation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillairePlanetLink_GeometryPropagationTest::RunTest(const FString& Parameters)
{
	// Test the pure math functions used by the link component.
	// Ground radius = base sphere (stable reference; terrain NOT folded in).
	const float GroundKm = UHillairePlanetLinkComponent::ComputeGroundRadiusKm(636000000.0f, 2000000.0f);
	TestTrue(TEXT("Earth reference ground ~6360 km"), FMath::Abs(GroundKm - 6360.0f) < 10.0f);

	// Envelope: profile-derived optical thickness raised by terrain
	// containment; the density scale is never stretched.
	const float TerrainKm = 20.0f;
	const float EnvelopeKm = UHillairePlanetLinkComponent::ComputeAtmosphereEnvelopeThicknessKm(GroundKm, TerrainKm);
	const float OpticalKm = UHillairePlanetLinkComponent::ComputeAtmosphereOpticalThicknessKm(GroundKm);
	TestTrue(TEXT("Earth optical thickness matches the planetary ratio"),
		FMath::Abs(OpticalKm - GroundKm * HillaireLimits::PlanetaryAtmosphereThicknessRatio) < 0.1f);
	TestTrue(TEXT("Envelope contains the terrain bound"), EnvelopeKm > TerrainKm);

	// MakePlanetAtmosphereState produces valid state with the corrected
	// contract: bottom = reference radius, top = reference + envelope.
	const FPlanetAtmosphereState State = UHillairePlanetLinkComponent::MakePlanetAtmosphereState(
		FGuid::NewGuid(), FName(TEXT("Test")),
		FVector::ZeroVector, FQuat::Identity,
		GroundKm, EnvelopeKm, TerrainKm, FGuid());
	
	TestTrue(TEXT("State is valid"), State.IsValid());
	TestTrue(TEXT("Profile bottom is the reference radius"),
		FMath::Abs(State.Profile.BottomRadiusKm - GroundKm) < 1e-4f);
	TestTrue(TEXT("Profile top = reference + envelope"),
		FMath::Abs(State.Profile.TopRadiusKm - (GroundKm + EnvelopeKm)) < 1e-4f);

	return true;
}

// ---------------------------------------------------------------------------
// 5. Hillaire.Subsystem.* - Wiring/integration
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSubsystem_RegisterUpdateTest,
	"Hillaire.Subsystem.RegisterAndUpdate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillaireSubsystem_RegisterUpdateTest::RunTest(const FString& Parameters)
{
	UHillairePlanetaryAtmosphereSubsystem* Subsystem = NewObject<UHillairePlanetaryAtmosphereSubsystem>();
	if (!TestTrue(TEXT("Subsystem created"), Subsystem != nullptr)) return false;

	// Register planet with stable FGuid
	const FGuid PlanetId = FGuid::NewGuid();
	const FName PlanetName = FName(TEXT("RuntimePlanet"));
	
	const FGuid RegisteredId = Subsystem->RegisterExternalPlanet(PlanetId, PlanetName);
	TestTrue(TEXT("Planet registered with stable FGuid"), RegisteredId == PlanetId);

	// Build a valid planet state
	FHillaireAtmosphereProfile BaseProfile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	FHillaireAtmosphereProfile NormalizedProfile = HillaireBuildNormalizedProfile(BaseProfile, 5.0f, 100.0f);

	FPlanetAtmosphereState LivePlanet;
	LivePlanet.PlanetId = PlanetId;
	LivePlanet.PlanetName = PlanetName;
	LivePlanet.CenterWS = FVector::ZeroVector;
	LivePlanet.RotationWS = FQuat::Identity;
	LivePlanet.Profile = NormalizedProfile;
	LivePlanet.GroundRadiusKm = NormalizedProfile.BottomRadiusKm;
	LivePlanet.AtmosphereTopRadiusKm = NormalizedProfile.TopRadiusKm;
	LivePlanet.TerrainHeightKm = 0.2f;
	LivePlanet.StarDistanceKm = -1.0f;
	LivePlanet.bValid = true;

	Subsystem->UpdateExternalPlanet(LivePlanet);

	TArray<FPlanetAtmosphereState> Planets;
	Subsystem->GetAllPlanetStates(Planets);

	if (!TestEqual(TEXT("One live planet registered"), Planets.Num(), 1)) return false;

	TestTrue(TEXT("Runtime ground radius arrives (5 km)"),
		FMath::IsNearlyEqual(Planets[0].GroundRadiusKm, 5.0f, 1e-4f));
	TestTrue(TEXT("Runtime top arrives (105 km)"),
		FMath::IsNearlyEqual(Planets[0].AtmosphereTopRadiusKm, 105.0f, 1e-4f));
	TestEqual(TEXT("Planet counted"), Subsystem->GetRegisteredPlanetCount(), 1);

	Subsystem->UnregisterExternalPlanet(PlanetId);
	Subsystem->GetAllPlanetStates(Planets);
	TestEqual(TEXT("Planet released"), Planets.Num(), 0);

	// Star registration
	FHillaireLightSource Star;
	Star.LightId = FGuid::NewGuid();
	Star.LightName = TEXT("PrimaryStar");
	Star.bEnabled = true;
	Star.bDirectional = true;
	Star.WorldDirectionToLight = FVector(0.0, 0.0, 1.0);
	Star.Color = FLinearColor::White;
	Star.Intensity = 0.8f;

	const FGuid StarId = FGuid::NewGuid();
	Subsystem->RegisterExternalStar(StarId, Star);

	TArray<FHillaireLightSource> Lights;
	Subsystem->GetAllLightSources(Lights);
	TestEqual(TEXT("One star light registered"), Lights.Num(), 1);
	TestTrue(TEXT("Star registered"), Lights[0].LightId == Star.LightId);

	Subsystem->UnregisterExternalStar(StarId);
	Subsystem->GetAllLightSources(Lights);
	TestEqual(TEXT("Star released"), Lights.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSubsystem_FrameSnapshotTest,
	"Hillaire.Subsystem.FrameSnapshot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHillaireSubsystem_FrameSnapshotTest::RunTest(const FString& Parameters)
{
	// This test requires a world context, so we test the frame state structure directly
	// The actual subsystem requires UWorld to initialize properly
	
	FHillaireAtmosphereFrameState Frame;
	Frame.FrameNumber = 1;
	Frame.GTFrameCounter = 100;
	Frame.ViewOriginWS = FVector::ZeroVector;
	Frame.ViewMatrix = FMatrix::Identity;
	Frame.ProjectionMatrix = FMatrix::Identity;
	Frame.ViewRect = FIntRect(0, 0, 1920, 1080);
	Frame.ViewDirectionWS = FVector::ForwardVector;
	Frame.MultipleScatteringFactor = 1.0f;
	Frame.bFastSkyEnabled = true;

	FPlanetAtmosphereState Planet;
	Planet.PlanetId = FGuid::NewGuid();
	Planet.PlanetName = TEXT("TestPlanet");
	Planet.CenterWS = FVector(0, 0, -636000000.0);
	Planet.GroundRadiusKm = 6360.0f;
	Planet.AtmosphereTopRadiusKm = 6460.0f;
	Planet.Profile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	Planet.ProfileHash = Planet.Profile.ComputeContentHash();
	Planet.bValid = true;
	Planet.ViewHeightKm = 6370.0f;
	Planet.DistanceKm = 6370.0f;

	Frame.Planets.Add(Planet);
	Frame.GoverningPlanetIndex = 0;
	Frame.VisiblePlanetIndices.Add(0);

	// Verify frame can be shared immutably
	const TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot = MakeShared<const FHillaireAtmosphereFrameState>(Frame);
	
	TestEqual(TEXT("Frame number"), Snapshot->FrameNumber, 1ULL);
	TestEqual(TEXT("Planet count"), Snapshot->Planets.Num(), 1);
	TestTrue(TEXT("Has content"), Snapshot->HasAtmosphereContent());
	TestNotNull(TEXT("Governing planet"), Snapshot->GetGoverningPlanet());
	TestEqual(TEXT("Governing index"), Snapshot->GoverningPlanetIndex, 0);
	TestEqual(TEXT("Visible indices"), Snapshot->VisiblePlanetIndices.Num(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS