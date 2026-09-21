// ANDROMEDA - HILLAIRE RUNTIME WIRING TESTS (Multiplanetary rebuild).
// Every generated star/planet is ATMOS-ready by construction:
//   ASun    -> UHillaireStarLinkComponent   -> Hillaire light feed
//   APlanet -> UHillairePlanetLinkComponent -> Hillaire planet feed
// Minimal by design: ownership on the class defaults (constructor
// subobjects, so ALL generated actors qualify), feed register/update on a
// bare subsystem, and the pure source-planet selection. Scattering math,
// feeds and GPU paths are owned by the Hillaire suites (re-run intact).

#include "Misc/AutomationTest.h"

#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillaireLightSource.h"
#include "HillaireLimits.h"
#include "HillairePlanetLinkComponent.h"
#include "HillairePlanetAtmosphereState.h"
#include "HillaireStarLinkComponent.h"
#include "Planet/Planet.h"
#include "ProceduralMeshComponent.h"
#include "Sun.h"

// ---------------------------------------------------------------------------
// Test 1 - A generated ASun owns exactly one star link (constructor).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringSunOwnsStarLinkTest,
	"Andromeda.Wiring.SunOwnsStarLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringSunOwnsStarLinkTest::RunTest(const FString& Parameters)
{
	const ASun* SunCDO =
		ASun::StaticClass()->GetDefaultObject<ASun>();

	if (!TestTrue(TEXT("ASun CDO exists"), SunCDO != nullptr))
	{
		return false;
	}

	TArray<UHillaireStarLinkComponent*> Links;
	SunCDO->GetComponents<UHillaireStarLinkComponent>(Links);

	TestEqual(TEXT("ASun owns exactly one star link"), Links.Num(), 1);

	if (Links.Num() == 1)
	{
		TestTrue(TEXT("Link owner is the star actor"),
			Links[0]->GetOwner() == SunCDO);
		TestFalse(TEXT("Link is a plain actor component (never mesh-attached)"),
			Links[0]->IsA<USceneComponent>());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Test 2 - A generated APlanet owns exactly one planet link (constructor).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringPlanetOwnsPlanetLinkTest,
	"Andromeda.Wiring.PlanetOwnsPlanetLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringPlanetOwnsPlanetLinkTest::RunTest(const FString& Parameters)
{
	const APlanet* PlanetCDO =
		APlanet::StaticClass()->GetDefaultObject<APlanet>();

	if (!TestTrue(TEXT("APlanet CDO exists"), PlanetCDO != nullptr))
	{
		return false;
	}

	TArray<UHillairePlanetLinkComponent*> Links;
	PlanetCDO->GetComponents<UHillairePlanetLinkComponent>(Links);

	TestEqual(TEXT("APlanet owns exactly one planet link"), Links.Num(), 1);

	if (Links.Num() == 1)
	{
		TestTrue(TEXT("Link owner is the planet actor"),
			Links[0]->GetOwner() == PlanetCDO);
		TestFalse(TEXT("Link is a plain actor component (never on the mesh)"),
			Links[0]->IsA<USceneComponent>());
	}

	TestTrue(TEXT("Generated mesh carries no planet link"),
		PlanetCDO->PlanetProceduralMesh
		&& PlanetCDO->PlanetProceduralMesh->GetAttachChildren().Num() == 0);

	return true;
}

// ---------------------------------------------------------------------------
// Test 3 - Both feeds register and update at runtime (bare subsystem).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringFeedsRegisterAndUpdateTest,
	"Andromeda.Wiring.FeedsRegisterAndUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringFeedsRegisterAndUpdateTest::RunTest(const FString& Parameters)
{
	UHillairePlanetaryAtmosphereSubsystem* Subsystem =
		NewObject<UHillairePlanetaryAtmosphereSubsystem>();

	if (!TestTrue(TEXT("Subsystem created"), Subsystem != nullptr))
	{
		return false;
	}

	// Planet feed: register with stable FGuid -> push live-size geometry -> visible -> release.
	const FGuid PlanetId = FGuid::NewGuid();
	const FName PlanetName = FName(TEXT("RuntimePlanet"));
	
	const FGuid RegisteredId = Subsystem->RegisterExternalPlanet(PlanetId, PlanetName);
	TestTrue(TEXT("Planet registered with stable FGuid"), RegisteredId == PlanetId);

	const FHillaireAtmosphereProfile BaseProfile = FHillaireAtmosphereProfile::MakeReferenceProfile();
	const FHillaireAtmosphereProfile NormalizedProfile = HillaireBuildNormalizedProfile(BaseProfile, 5.0f, 100.0f);

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

	if (!TestEqual(TEXT("One live planet registered"), Planets.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("Runtime ground radius arrives (5 km)"),
		FMath::IsNearlyEqual(Planets[0].GroundRadiusKm, 5.0f, 1e-4f));
	TestTrue(TEXT("Runtime top arrives (105 km)"),
		FMath::IsNearlyEqual(Planets[0].AtmosphereTopRadiusKm, 105.0f, 1e-4f));
	TestEqual(TEXT("Planet counted"), Subsystem->GetRegisteredPlanetCount(), 1);

	Subsystem->UnregisterExternalPlanet(PlanetId);
	Subsystem->GetAllPlanetStates(Planets);
	TestEqual(TEXT("Planet released"), Planets.Num(), 0);

	// Light feed: push -> star registry -> release.
	FHillaireLightSource Star;
	Star.LightId = UHillaireStarLinkComponent::GetPrimaryStarSlotId();
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

	if (!TestEqual(TEXT("One star light registered"), Lights.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("Star registered"),
		Lights[0].LightId == UHillaireStarLinkComponent::GetPrimaryStarSlotId());

	Subsystem->UnregisterExternalStar(StarId);
	Subsystem->GetAllLightSources(Lights);
	TestEqual(TEXT("Star released"), Lights.Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Test 4 - Source-planet selection: camera-nearest wins (pure).
// Uses HillairePlanetMath::SelectPlanets (new architecture).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringSourcePlanetSelectionTest,
	"Andromeda.Wiring.SourcePlanetSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringSourcePlanetSelectionTest::RunTest(const FString& Parameters)
{
	// Two planets: A at origin, B at 10000 km
	TArray<FPlanetSelectionInput> Planets;
	Planets.Add({ FGuid::NewGuid(), FVector3f(0, 0, 0), 6460.0f });
	Planets.Add({ FGuid::NewGuid(), FVector3f(10000, 0, 0), 6460.0f });

	const FVector3f CameraRel(0, 0, 0);
	const FVector ViewDir(1, 0, 0);

	// Camera at planet A center -> inside A
	auto Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel, ViewDir);
	TestTrue("Camera at A center -> A governing", Result.GoverningIndex == 0);
	TestTrue("A contains camera", Result.bGoverningContainsCamera);

	// Camera at 7000 km from A (outside), 3000 km from B (inside B)
	const FVector3f CameraRel2(7000, 0, 0);
	Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel2, ViewDir);
	TestTrue("Camera inside B -> B governing", Result.GoverningIndex == 1);
	TestTrue("B contains camera", Result.bGoverningContainsCamera);

	// Camera far from both -> nearest surface
	const FVector3f CameraRel3(20000, 0, 0);
	Result = HillairePlanetMath::SelectPlanets(Planets, CameraRel3, ViewDir);
	TestTrue("Far from both -> nearest surface (B)", Result.GoverningIndex == 1);
	TestFalse("Not containing", Result.bGoverningContainsCamera);

	return true;
}

// ---------------------------------------------------------------------------
// Test 5 - Atmosphere scale: profile-derived, self-similar, terrain-independent.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringAtmosphereScaleTest,
	"Andromeda.Wiring.AtmosphereScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringAtmosphereScaleTest::RunTest(const FString& Parameters)
{
	const float TargetRatio = HillaireLimits::PlanetaryAtmosphereThicknessRatio;

	// Atmospheric thickness is the planetary volume ratio (~0.10x ground radius)
	// on any planet, with NO terrain input (pure planetary sphere).
	const float Radii[] = { 3.9f, 5.0f, 8.8f, 9.4f, 6360.0f };
	for (float R : Radii)
	{
		const float Optical1 = UHillairePlanetLinkComponent::ComputeAtmosphereOpticalThicknessKm(R);
		const float Optical2 = UHillairePlanetLinkComponent::ComputeAtmosphereOpticalThicknessKm(R);
		TestTrue(TEXT("Optical thickness deterministic"), Optical1 == Optical2);
		TestTrue(TEXT("Optical thickness matches target ratio"),
			FMath::IsNearlyEqual(Optical1, R * TargetRatio, FMath::Max(1e-4f, R * 1e-5f)));
		TestTrue(TEXT("Optical thickness positive"), Optical1 > 0.0f);
	}

	const float GroundKm = 8.835f;
	const float TerrainKm = 0.529f;
	const float Envelope = UHillairePlanetLinkComponent::ComputeAtmosphereEnvelopeThicknessKm(GroundKm, TerrainKm);
	const float Optical = UHillairePlanetLinkComponent::ComputeAtmosphereOpticalThicknessKm(GroundKm);
	TestTrue(TEXT("Envelope >= optical thickness"), Envelope >= Optical - 1e-6f);
	TestTrue(TEXT("Envelope deterministic"),
		Envelope == UHillairePlanetLinkComponent::ComputeAtmosphereEnvelopeThicknessKm(GroundKm, TerrainKm));
	TestTrue(TEXT("Envelope matches optical volume"),
		FMath::IsNearlyEqual(Envelope, Optical, 1e-6f));
	return true;
}

// ---------------------------------------------------------------------------
// Test 6 - Vertical scale: Bottom = reference radius (terrain NOT folded in),
// Top = reference + profile/containment envelope, camera on the terrain bound
// stays inside the volume, and the density scale is envelope-independent.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringAtmosphereVerticalScaleTest,
	"Andromeda.Wiring.AtmosphereVerticalScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringAtmosphereVerticalScaleTest::RunTest(const FString& Parameters)
{
	struct FSizeCase { float RadiusCm; float TerrainCm; };
	const FSizeCase Cases[] = {
		{ 250000.0f, 10000.0f },
		{ 500000.0f, 20000.0f },
		{ 1200000.0f, 30000.0f },
	};
	uint64 PrevHash = 0;
	bool bFirst = true;
	for (const FSizeCase& C : Cases)
	{
		const float GroundKm = UHillairePlanetLinkComponent::ComputeGroundRadiusKm(C.RadiusCm, C.TerrainCm);
		TestTrue(TEXT("Ground is the stable reference radius (terrain NOT folded in)"),
			FMath::IsNearlyEqual(GroundKm, C.RadiusCm * 1e-5f, 1e-6f));

		const float TerrainKm = C.TerrainCm * 1e-5f;
		const float EnvelopeKm =
			UHillairePlanetLinkComponent::ComputeAtmosphereEnvelopeThicknessKm(GroundKm, TerrainKm);
		const float OpticalKm = UHillairePlanetLinkComponent::ComputeAtmosphereOpticalThicknessKm(GroundKm);

		const FPlanetAtmosphereState State = UHillairePlanetLinkComponent::MakePlanetAtmosphereState(
			FGuid::NewGuid(), FName(TEXT("ScaleProbe")),
			FVector::ZeroVector, FQuat::Identity,
			GroundKm, EnvelopeKm, TerrainKm, FGuid());
		if (!TestTrue(TEXT("Scaled planet valid"), State.IsValid()))
		{
			return false;
		}
		TestTrue(TEXT("Profile bottom is the reference radius"),
			FMath::IsNearlyEqual(State.Profile.BottomRadiusKm, GroundKm, 1e-4f));
		TestTrue(TEXT("Profile top = reference + envelope"),
			FMath::IsNearlyEqual(State.Profile.TopRadiusKm, GroundKm + EnvelopeKm, 1e-4f));
		TestTrue(TEXT("Camera on the authored terrain bound is inside the volume"),
			(GroundKm + TerrainKm) < State.Profile.TopRadiusKm);

		// Density is normalized to the envelope: sigma * scale_height (the vertical
		// optical depth) is preserved versus the reference profile.
		const FHillaireAtmosphereProfile Reference = FHillaireAtmosphereProfile::MakeReferenceProfile();
		const float ReferenceOD = Reference.RayleighScatteringKm.X * (-1.0f / Reference.RayleighExpScale);
		// Volumetric distribution (H_R = T / RayleighVolumeScaleHeights): the
		// preserved quantity is the finite column sigma * H * (1 - exp(-T/H)).
		const float RayleighIntegralFraction =
			1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights);
		const float StateOD = State.Profile.RayleighScatteringKm.X
			* (-1.0f / State.Profile.RayleighExpScale) * RayleighIntegralFraction;
		TestTrue(TEXT("Vertical Rayleigh optical depth preserved (envelope-normalized density)"),
			FMath::IsNearlyEqual(StateOD, ReferenceOD, ReferenceOD * 1e-3f));

		// Envelope re-normalization: the same ground with a larger envelope
		// re-scales the density so the vertical optical depth stays
		// reference-identical; only the top moves.
		const FPlanetAtmosphereState BiggerVolume = UHillairePlanetLinkComponent::MakePlanetAtmosphereState(
			FGuid::NewGuid(), FName(TEXT("ScaleProbeBig")),
			FVector::ZeroVector, FQuat::Identity,
			GroundKm, EnvelopeKm * 4.0f, TerrainKm, FGuid());
		const float BigOD =
			BiggerVolume.Profile.RayleighScatteringKm.X
			* (-1.0f / BiggerVolume.Profile.RayleighExpScale) * RayleighIntegralFraction;
		TestTrue(TEXT("Larger envelope preserves the vertical optical depth"),
			FMath::IsNearlyEqual(BigOD, ReferenceOD, ReferenceOD * 1e-3f));
		TestTrue(TEXT("Larger envelope only raises the top"),
			BiggerVolume.Profile.TopRadiusKm > State.Profile.TopRadiusKm);

		AddInfo(FString::Printf(TEXT("R=%.0fcm ground=%.3fkm optical=%.4fkm envelope=%.4fkm top=%.3fkm"),
			(double)C.RadiusCm, (double)GroundKm, (double)OpticalKm, (double)EnvelopeKm,
			(double)State.Profile.TopRadiusKm));

		// Distinct sizes hash distinctly: separate LUT keys per planet, the
		// propagation path the renderer keys regen on.
		const uint64 H = State.Profile.ComputeContentHash();
		if (!bFirst)
		{
			TestTrue(TEXT("Sizes hash distinctly (LUT separation)"), H != PrevHash);
		}
		PrevHash = H;
		bFirst = false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Test 7 - Source-planet containment preference: the atmosphere owning the
// camera wins over a nearer center; without radii the legacy nearest rule
// holds bit-identically.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringSourcePlanetContainmentTest,
	"Andromeda.Wiring.SourcePlanetContainment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringSourcePlanetContainmentTest::RunTest(const FString& Parameters)
{
	// A: top 100 km at the origin. B: top 100 km at 250 km (no overlap:
	// 250 > 100 + 100). Camera at 120 km: outside both shells, nearer to A.
	// Growing B's shell to 200 km contains the camera: B wins despite A's
	// nearer center.
	const TArray<FVector> Positions = {
		FVector::ZeroVector,
		FVector(25000000.0, 0.0, 0.0), // 250 km
	};
	const TArray<float> TopsCm = { 10000000.0f, 10000000.0f }; // 100 km each

	const FVector Cam(12000000.0, 0.0, 0.0); // 120 km
	
	// Convert to new selection input format
	TArray<FPlanetSelectionInput> SelectionInputs;
	SelectionInputs.Add({ FGuid::NewGuid(), FVector3f(0, 0, 0), 100.0f });
	SelectionInputs.Add({ FGuid::NewGuid(), FVector3f(250.0f, 0.0f, 0.0f), 100.0f });

	FPlanetSelectionResult Result = HillairePlanetMath::SelectPlanets(
		SelectionInputs, FVector3f(120.0f, 0.0f, 0.0f), FVector(1, 0, 0));

	TestEqual(TEXT("Nobody contains -> nearest (A)"), Result.GoverningIndex, 0);

	// Grow B's shell to 200 km - contains camera
	SelectionInputs[1].TopRadiusKm = 200.0f;
	Result = HillairePlanetMath::SelectPlanets(
		SelectionInputs, FVector3f(120.0f, 0.0f, 0.0f), FVector(1, 0, 0));

	TestEqual(TEXT("B contains -> B despite A nearer"), Result.GoverningIndex, 1);

	// Missing radii degrade to legacy nearest - test with only one having radius
	SelectionInputs[1].TopRadiusKm = 0.0f;
	Result = HillairePlanetMath::SelectPlanets(
		SelectionInputs, FVector3f(120.0f, 0.0f, 0.0f), FVector(1, 0, 0));
	TestEqual(TEXT("No radii on B -> legacy nearest (A)"), Result.GoverningIndex, 0);

	// Non-positive radii are ignored
	SelectionInputs[0].TopRadiusKm = 0.0f;
	SelectionInputs[1].TopRadiusKm = -5.0f;
	Result = HillairePlanetMath::SelectPlanets(
		SelectionInputs, FVector3f(120.0f, 0.0f, 0.0f), FVector(1, 0, 0));
	TestEqual(TEXT("Degenerate radii -> legacy nearest"), Result.GoverningIndex, 0);

	return true;
}

// ---------------------------------------------------------------------------
// Test 8 - Atmosphere-top radius in cm: Top = reference ground + profile/
// containment envelope (no seed multiplier).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndromedaWiringAtmosphereTopRadiusCmTest,
	"Andromeda.Wiring.AtmosphereTopRadiusCm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAndromedaWiringAtmosphereTopRadiusCmTest::RunTest(const FString& Parameters)
{
	const float RadiusCm = 500000.0f;
	const float TerrainCm = 20000.0f;
	const float TopCm = UHillairePlanetLinkComponent::ComputeAtmosphereTopRadiusCm(
		RadiusCm, TerrainCm);

	const float GroundKm = UHillairePlanetLinkComponent::ComputeGroundRadiusKm(RadiusCm, TerrainCm);
	const float TerrainKm = TerrainCm * 1e-5f;
	const float EnvelopeKm = UHillairePlanetLinkComponent::ComputeAtmosphereEnvelopeThicknessKm(
		GroundKm, TerrainKm);
	TestTrue(TEXT("TopCm = (reference ground + envelope) / KmPerCm"),
		FMath::IsNearlyEqual(TopCm, (GroundKm + EnvelopeKm) * 100000.0f, 1.0f));
	TestTrue(TEXT("TopCm exceeds the authored terrain bound"),
		TopCm > RadiusCm + TerrainCm);
	// Negative terrain clamps like the pushed state (envelope floor is the
	// self-similar optical atmosphere).
	const float TopNeg = UHillairePlanetLinkComponent::ComputeAtmosphereTopRadiusCm(
		RadiusCm, -5000.0f);
	const float OpticalOnlyCm =
		(GroundKm + UHillairePlanetLinkComponent::ComputeAtmosphereOpticalThicknessKm(GroundKm)) * 100000.0f;
	TestTrue(TEXT("Negative terrain clamps to the optical envelope"),
		FMath::IsNearlyEqual(TopNeg, OpticalOnlyCm, 1.0f));
	return true;
}