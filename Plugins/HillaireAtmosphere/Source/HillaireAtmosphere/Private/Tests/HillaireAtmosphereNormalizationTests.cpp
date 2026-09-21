// HILLAIRE ATMOSPHERE - NORMALIZATION + DEBUG-GATE AUTOMATION TESTS.
//
// FASE 2/10/11: the self-similar planet/atmosphere volume model and the
// r.Hillaire.DebugMode gate.
//
// Background: the reference scattering coefficients are tuned for Earth
// (100 km envelope, 8 km Rayleigh scale height). Production Andromeda planets
// are ~5-9 km with ~0.2-0.8 km authored terrain bounds, so the absolute sigmas
// would give a near-transparent sky while a literal Earth envelope would be
// 12x the planet radius. HillaireBuildNormalizedProfile (the centralized
// BuildAtmosphereParameters) carries the reference atmosphere to any planet by
// NORMALIZING THE DENSITY TO THE ENVELOPE (sigma * H invariant, density shape
// matched in h/T) while the envelope itself is a stable planetary volume
// (self-similar minimum, raised by a stable terrain-containment floor that
// never follows local terrain); these tests lock that contract with RELATIVE
// assertions (no magic absolute bars that could mask a rescale).
//
// All tests are pure CPU (HillaireLutCpu mirror + profile math): no GPU
// submit, no PIE required.

#include "Misc/AutomationTest.h"

#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"
#include "HillaireLutCpu.h"
#include "HillaireLutManager.h"
#include "HillairePlanetState.h"

namespace
{
	float SkyLuminance(const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}

	// Analytic vertical-column optical depth of the PROFILE ITSELF, by fine
	// independent integration (no LUT march, no sampler quadrature): this tests
	// the self-similar MODEL transform exactly. The production marches use
	// bounded adaptive counts (see HillaireEnvelopeAwareSampleCount), so a
	// fixed-step LUT integration is not the right oracle for this invariant.
	float ColumnODChannel(const FHillaireAtmosphereProfile& P, int32 Channel)
	{
		const float T = P.TopRadiusKm - P.BottomRadiusKm;
		auto Pick = [Channel](const FVector& V)
		{
			return Channel == 0 ? (float)V.X : (Channel == 1 ? (float)V.Y : (float)V.Z);
		};
		const float SigmaRay = Pick(P.RayleighScatteringKm);
		const float SigmaMie = Pick(P.MieExtinctionKm);
		const float SigmaOzo = Pick(P.AbsorptionExtinctionKm);

		constexpr int32 N = 20000;
		const double Dh = (double)T / (double)N;
		double OD = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			const double h = ((double)i + 0.5) * Dh;
			const double DRay = FMath::Exp((double)P.RayleighExpScale * h);
			const double DMie = FMath::Exp((double)P.MieExpScale * h);
			const double DOzo = FMath::Clamp(
				h < P.AbsorptionWidthKm
					? P.AbsorptionLinear0 * h + P.AbsorptionConstant0
					: P.AbsorptionLinear1 * h + P.AbsorptionConstant1,
				0.0, 1.0);
			OD += (SigmaRay * DRay + SigmaMie * DMie + SigmaOzo * DOzo) * Dh;
		}
		return (float)OD;
	}

	FVector3f VerticalColumnOD(const FHillaireAtmosphereProfile& P)
	{
		return FVector3f(
			ColumnODChannel(P, 0), ColumnODChannel(P, 1), ColumnODChannel(P, 2));
	}

	float RelativeError(float A, float B)
	{
		return FMath::Abs(A - B) / FMath::Max(1e-6f, FMath::Abs(B));
	}
}

// ---------------------------------------------------------------------------
// Test 1 - Envelope normalization factor values (pure, deterministic).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireNormalizationFactorTest,
	"Hillaire.Normalization.FactorValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireNormalizationFactorTest::RunTest(const FString& Parameters)
{
	// K = ReferenceThickness / Envelope: the density is normalized to the
	// atmosphere envelope so sigma * H is invariant on any size.
	TestTrue(TEXT("Reference envelope needs no scaling"),
		FMath::IsNearlyEqual(HillaireThicknessNormalizationFactor(100.0f), 1.0f, 1e-6f));
	TestTrue(TEXT("1 km envelope scales x100"),
		FMath::IsNearlyEqual(HillaireThicknessNormalizationFactor(1.0f), 100.0f, 1e-4f));
	TestTrue(TEXT("0.5 km envelope scales x200"),
		FMath::IsNearlyEqual(HillaireThicknessNormalizationFactor(0.5f), 200.0f, 1e-3f));
	TestTrue(TEXT("150 km envelope scales down"),
		FMath::IsNearlyEqual(HillaireThicknessNormalizationFactor(150.0f), 100.0f / 150.0f, 1e-6f));
	const float Degenerate = HillaireThicknessNormalizationFactor(0.0f);
	TestTrue(TEXT("Degenerate envelope is guarded, finite and positive"),
		FMath::IsFinite(Degenerate) && Degenerate > 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// Test 2 - Earth invariance: the reference shell is untouched by the builder.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireNormalizationEarthInvariantTest,
	"Hillaire.Normalization.EarthInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireNormalizationEarthInvariantTest::RunTest(const FString& Parameters)
{
	const FHillaireAtmosphereProfile Ref = FHillaireAtmosphereProfile::MakeReferenceProfile();
	const FHillaireAtmosphereProfile Built = HillaireBuildNormalizedProfile(Ref, 6360.0f, 100.0f);
	TestTrue(TEXT("Earth normalized bottom radius matches"), FMath::IsNearlyEqual(Built.BottomRadiusKm, Ref.BottomRadiusKm, 1e-4f));
	TestTrue(TEXT("Earth normalized top radius matches"), FMath::IsNearlyEqual(Built.TopRadiusKm, Ref.TopRadiusKm, 1e-4f));
	const float RefOD = Ref.RayleighScatteringKm.X * (-1.0f / Ref.RayleighExpScale);
	const float BuiltOD = Built.RayleighScatteringKm.X * (-1.0f / Built.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("Earth normalized profile preserves optical depth"), FMath::IsNearlyEqual(BuiltOD, RefOD, RefOD * 0.02f));
	FString Error;
	TestTrue(TEXT("Earth normalized profile valid: ") + Error, Built.IsValid(&Error));
	return true;
}

// ---------------------------------------------------------------------------
// Test 3 - Small-planet scaling values (volumetric distribution)
// and envelope re-normalization (a larger envelope preserves sigma * H).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireNormalizationSmallPlanetTest,
	"Hillaire.Normalization.SmallPlanetScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireNormalizationSmallPlanetTest::RunTest(const FString& Parameters)
{
	const FHillaireAtmosphereProfile Ref = FHillaireAtmosphereProfile::MakeReferenceProfile();
	const FHillaireAtmosphereProfile P = HillaireBuildNormalizedProfile(Ref, 5.0f, 0.5f);
	const float K = 200.0f; // 100 / envelope

	TestTrue(TEXT("Bottom is the stable reference radius"),
		FMath::IsNearlyEqual(P.BottomRadiusKm, 5.0f, 1e-6f));
	TestTrue(TEXT("Top is bottom + envelope"),
		FMath::IsNearlyEqual(P.TopRadiusKm, 5.5f, 1e-6f));

	const float ExpectedRayleighScale = -HillaireLimits::RayleighVolumeScaleHeights / 0.5f;
	const float BaseH = -1.0f / Ref.RayleighExpScale;
	const float OutH = 0.5f / HillaireLimits::RayleighVolumeScaleHeights;
	const float ExpectedRayleighSigma = Ref.RayleighScatteringKm.X * (BaseH / (OutH * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights))));
	TestTrue(TEXT("Rayleigh sigma scales for volumetric distribution"),
		FMath::IsNearlyEqual(P.RayleighScatteringKm.X, ExpectedRayleighSigma, 1e-3f));
	TestTrue(TEXT("Rayleigh falloff set for volumetric distribution"),
		FMath::IsNearlyEqual(P.RayleighExpScale, ExpectedRayleighScale, 1e-4f));

	const float BaseHMie = -1.0f / Ref.MieExpScale;
	const float OutHMie = 0.5f / HillaireLimits::MieVolumeScaleHeights;
	const float ExpectedMieSigma = Ref.MieExtinctionKm.X * (BaseHMie / (OutHMie * (1.0f - FMath::Exp(-HillaireLimits::MieVolumeScaleHeights))));
	const float ExpectedMieScale = -HillaireLimits::MieVolumeScaleHeights / 0.5f;
	TestTrue(TEXT("Mie extinction scales for volumetric distribution"),
		FMath::IsNearlyEqual(P.MieExtinctionKm.X, ExpectedMieSigma, 1e-4f));
	TestTrue(TEXT("Mie falloff set for volumetric distribution"),
		FMath::IsNearlyEqual(P.MieExpScale, ExpectedMieScale, 1e-3f));

	TestTrue(TEXT("Ozone sigma scales by K"),
		FMath::IsNearlyEqual(P.AbsorptionExtinctionKm.Y, Ref.AbsorptionExtinctionKm.Y * K, 1e-4f));
	TestTrue(TEXT("Ozone tent width set for volume"),
		FMath::IsNearlyEqual(P.AbsorptionWidthKm, 0.25f * 0.5f, 1e-6f));
	TestTrue(TEXT("Ozone tent slope set for volume"),
		FMath::IsNearlyEqual(P.AbsorptionLinear0, 1.0f / (0.15f * 0.5f), 1e-3f));
	TestTrue(TEXT("Ozone tent offset is scale-invariant"),
		FMath::IsNearlyEqual(P.AbsorptionConstant0, Ref.AbsorptionConstant0, 1e-6f)
		&& FMath::IsNearlyEqual(P.AbsorptionConstant1, Ref.AbsorptionConstant1, 1e-6f));
	TestTrue(TEXT("Albedo/phase/cutoff/solar untouched"),
		P.GroundAlbedo.Equals(Ref.GroundAlbedo)
		&& P.MiePhaseG == Ref.MiePhaseG
		&& P.MuSMin == Ref.MuSMin
		&& P.SolarIrradiance.Equals(Ref.SolarIrradiance));
	FString Error;
	TestTrue(TEXT("Scaled profile valid: ") + Error, P.IsValid(&Error));

	// Envelope re-normalization: a larger envelope re-scales the density so
	// the vertical optical depth sigma * H stays reference-identical, and the
	// top moves.
	const FHillaireAtmosphereProfile Big = HillaireBuildNormalizedProfile(Ref, 5.0f, 4.0f);
	const float ReferenceOD = Ref.RayleighScatteringKm.X * (-1.0f / Ref.RayleighExpScale);
	const float SmallOD = P.RayleighScatteringKm.X * (-1.0f / P.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	const float BigOD = Big.RayleighScatteringKm.X * (-1.0f / Big.RayleighExpScale) * (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("Small envelope preserves vertical OD"),
		FMath::IsNearlyEqual(SmallOD, ReferenceOD, ReferenceOD * 1e-3f));
	TestTrue(TEXT("Larger envelope preserves vertical OD"),
		FMath::IsNearlyEqual(BigOD, ReferenceOD, ReferenceOD * 1e-3f));
	TestTrue(TEXT("Larger envelope raises the top"),
		FMath::IsNearlyEqual(Big.TopRadiusKm, 9.0f, 1e-6f));
	return true;
}

// ---------------------------------------------------------------------------
// Test 4 - Optical-depth preservation: a 1 km normalized shell carries the
// same zenith extinction column as the 100 km Earth reference (< 2 %).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireNormalizationOpticalDepthTest,
	"Hillaire.Normalization.OpticalDepthPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireNormalizationOpticalDepthTest::RunTest(const FString& Parameters)
{
	const FHillaireAtmosphereProfile Ref = FHillaireAtmosphereProfile::MakeReferenceProfile();
	const FHillairePlanetState Small = HillaireMakeExternalPlanetState(
		0, FGuid(0x0D0001, 0, 0, 0), FName(TEXT("ODSmall")),
		FVector::ZeroVector, FQuat::Identity,
		5.0f, 1.0f, 0.0f, Ref);

	const FVector3f ODEarth = VerticalColumnOD(Ref);
	const FVector3f ODSmall = VerticalColumnOD(Small.Profile);
	AddInfo(FString::Printf(TEXT("OD earth=(%.5f,%.5f,%.5f) small=(%.5f,%.5f,%.5f)"),
		ODEarth.X, ODEarth.Y, ODEarth.Z, ODSmall.X, ODSmall.Y, ODSmall.Z));

	TestTrue(TEXT("Red channel OD preserved"),
		RelativeError(ODSmall.X, ODEarth.X) < 0.02f);
	TestTrue(TEXT("Green channel OD preserved"),
		RelativeError(ODSmall.Y, ODEarth.Y) < 0.02f);
	TestTrue(TEXT("Blue channel OD preserved"),
		RelativeError(ODSmall.Z, ODEarth.Z) < 0.02f);
	TestTrue(TEXT("Column is optically meaningful (not transparent)"),
		ODSmall.X > 0.01f && ODSmall.Y > 0.05f && ODSmall.Z > 0.15f);
	return true;
}

// ---------------------------------------------------------------------------
// Test 5 - Sky brightness lift: the same small planet renders brighter with
// the self-similar profile than with raw Earth sigmas (the old in-game
// behavior). Evaluated at a realistic EYE height (2 m above the reference
// ground): the corrected density scale is much shorter than Earth's, so a
// fixed 20 m probe would sit several scale heights up and under-report the
// surface sky. Relative assertion: normalized mean > 3x raw mean.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireNormalizationSkyBrightnessTest,
	"Hillaire.Normalization.SkyBrightnessLift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireNormalizationSkyBrightnessTest::RunTest(const FString& Parameters)
{
	const FHillaireAtmosphereProfile Ref = FHillaireAtmosphereProfile::MakeReferenceProfile();

	// Old behavior: Earth sigmas on a 1 km shell (radii overridden only).
	FHillaireAtmosphereProfile RawSmall = Ref;
	RawSmall.BottomRadiusKm = 5.2f;
	RawSmall.TopRadiusKm = 6.2f;

	// New behavior: centralized builder through the STARMAP seam.
	const FHillairePlanetState NormState = HillaireMakeExternalPlanetState(
		1, FGuid(0x0D0002, 0, 0, 0), FName(TEXT("NormSmall")),
		FVector::ZeroVector, FQuat::Identity,
		5.2f, 1.0f, 0.0f, Ref);
	const FHillaireAtmosphereProfile& NormSmall = NormState.Profile;

	const int32 TW = HillaireLimits::TransmittanceWidth;
	const int32 TH = HillaireLimits::TransmittanceHeight;
	const int32 MSR = HillaireLimits::MultiScatteringRes;
	const int32 SVW = HillaireLimits::SkyViewWidth;
	const int32 SVH = HillaireLimits::SkyViewHeight;

	auto BakeSkyMean = [&](const FHillaireAtmosphereProfile& P, float& OutMean) -> bool
	{
		TArray<FLinearColor> TransLut, MsLut;
		HillaireLutCpu::BakeTransmittanceLut(P, TW, TH, TransLut);
		HillaireLutCpu::BakeFullMultiScatteringLut(P, TransLut, TW, TH, MSR, 1.0f, MsLut);
		const float ViewHeightKm = P.BottomRadiusKm + 0.002f; // 2 m eye height
		const FVector3f Noon(0.0f, 0.0f, 1.0f);
		// Representative MID-SKY rows (zenith ~26-60 deg): near-horizon rays
		// saturate optically for BOTH profiles, which would compress the
		// measured ratio and hide the density lift.
		TArray<FIntPoint> Coords;
		for (int32 Y : { 8, 12, 16, 20, 24 })
		{
			for (int32 X : { 2, 6, 12, 24, 96, 190 })
			{
				Coords.Add(FIntPoint(X, Y));
			}
		}
		TArray<FLinearColor> Values;
		HillaireLutCpu::BakeSkyViewTexels(
			P, TransLut, TW, TH, MsLut, MSR,
			ViewHeightKm, Noon, Noon, SVW, SVH, Coords, Values);
		if (Values.Num() != Coords.Num())
		{
			return false;
		}
		double Sum = 0.0;
		for (const FLinearColor& C : Values)
		{
			if (!FMath::IsFinite(C.R) || !FMath::IsFinite(C.G) || !FMath::IsFinite(C.B))
			{
				return false;
			}
			Sum += SkyLuminance(C);
		}
		OutMean = (float)(Sum / (double)Values.Num());
		return true;
	};

	float RawMean = 0.0f, NormMean = 0.0f;
	if (!TestTrue(TEXT("Raw small-planet sky bakes finite"), BakeSkyMean(RawSmall, RawMean))
		|| !TestTrue(TEXT("Normalized small-planet sky bakes finite"), BakeSkyMean(NormSmall, NormMean)))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Noon-surface SkyView mean: raw=%.6f normalized=%.6f (x%.1f)"),
		RawMean, NormMean, NormMean / FMath::Max(1e-9f, RawMean)));
	TestTrue(TEXT("Normalized sky is actually visible"), NormMean > 0.001f);
	TestTrue(TEXT("Normalized sky is several times brighter than raw"),
		NormMean > 3.0f * RawMean);
	return true;
}

// ---------------------------------------------------------------------------
// Test 6 - Planet/atmosphere model (CORRECTED VOLUME MODEL): Bottom = stable
// planetary reference radius (terrain NOT folded in); the envelope contains
// the authored terrain bound; the camera on the terrain bound is inside; the
// density scale is self-similar and envelope-independent.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireNormalizationRadiiModelTest,
	"Hillaire.Normalization.RadiiModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireNormalizationRadiiModelTest::RunTest(const FString& Parameters)
{
	// 5.2 km reference planet, 0.2 km authored terrain bound.
	const float GroundKm = 5.2f;
	const float TerrainKm = 0.2f;
	const FHillaireAtmosphereProfile Ref = FHillaireAtmosphereProfile::MakeReferenceProfile();

	// Planetary builder: envelope derived from the profile scale + terrain.
	const FHillaireAtmosphereProfile P =
		HillaireBuildPlanetaryProfile(Ref, GroundKm, TerrainKm);
	TestTrue(TEXT("Bottom is the planetary reference radius"),
		FMath::IsNearlyEqual(P.BottomRadiusKm, GroundKm, 1e-6f));
	TestTrue(TEXT("Top contains the authored terrain bound plus headroom"),
		P.TopRadiusKm > GroundKm + TerrainKm);

	// Seam state (explicit envelope floor (1) must not beat the physical one).
	const FHillairePlanetState S = HillaireMakeExternalPlanetState(
		2, FGuid(0x0D0003, 0, 0, 0), FName(TEXT("RadiiPlanet")),
		FVector::ZeroVector, FQuat::Identity,
		GroundKm, 1.0f, TerrainKm, Ref);
	TestTrue(TEXT("Mirrors equal the authoritative profile"),
		FMath::IsNearlyEqual(S.GroundRadiusKm, S.Profile.BottomRadiusKm, 1e-6f)
		&& FMath::IsNearlyEqual(S.AtmosphereRadiusKm, S.Profile.TopRadiusKm, 1e-6f));
	TestTrue(TEXT("Terrain contribution preserved (metadata)"),
		FMath::IsNearlyEqual(S.TerrainHeightKm, TerrainKm, 1e-6f));
	TestTrue(TEXT("Terrain never enters the bottom"),
		FMath::IsNearlyEqual(S.Profile.BottomRadiusKm, GroundKm, 1e-6f));

	const float ReferenceOD = Ref.RayleighScatteringKm.X * (-1.0f / Ref.RayleighExpScale);
	// The volumetric builder distributes the density over the whole envelope
	// (H_R = T / RayleighVolumeScaleHeights), so the preserved quantity is the
	// finite column integral sigma * H * (1 - exp(-T/H)), not sigma * H.
	const float OD = S.Profile.RayleighScatteringKm.X * (-1.0f / S.Profile.RayleighExpScale)
		* (1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights));
	TestTrue(TEXT("Density self-similar (vertical OD preserved)"),
		FMath::IsNearlyEqual(OD, ReferenceOD, ReferenceOD * 5e-3f));
	FString Error;
	TestTrue(TEXT("Planet valid: ") + Error, S.IsValid(&Error));
	return true;
}

// ---------------------------------------------------------------------------
// Test 7 - External seam goes through the builder (no direct radii writes).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireNormalizationSeamTest,
	"Hillaire.Normalization.ExternalSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireNormalizationSeamTest::RunTest(const FString& Parameters)
{
	const FHillaireAtmosphereProfile Ref = FHillaireAtmosphereProfile::MakeReferenceProfile();
	const FHillairePlanetState P = HillaireMakeExternalPlanetState(
		3, FGuid(0x0D0004, 0, 0, 0), FName(TEXT("SeamPlanet")),
		FVector::ZeroVector, FQuat::Identity,
		5.0f, 0.5f, 0.0f, Ref);
	const FHillaireAtmosphereProfile Expected = HillaireBuildNormalizedProfile(Ref, 5.0f, 0.5f);
	TestTrue(TEXT("Seam profile equals the centralized builder output"), P.Profile == Expected);
	// The volumetric model redistributes even the reference profile over its
	// envelope (H_R = T/4), so Earth is no longer byte-identical to the base
	// profile: the seam must match the centralized builder instead.
	TestTrue(TEXT("Earth seam matches the centralized builder"),
		HillaireMakeExternalPlanetState(
			4, FGuid(0x0D0005, 0, 0, 0), FName(TEXT("SeamEarth")),
			FVector::ZeroVector, FQuat::Identity,
			6360.0f, 100.0f, 0.0f, Ref).Profile
		== HillaireBuildNormalizedProfile(Ref, 6360.0f, 100.0f));
	return true;
}

// ---------------------------------------------------------------------------
// Test 8 - Debug gate truth table (r.Hillaire.DebugMode 0-5).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireDebugGateTest,
	"Hillaire.Debug.Gate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireDebugGateTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Mode 0 never visualizes"),
		FHillaireLutManager::ShouldVisualizeDebug(0, true, true, true, true, true));
	TestTrue(TEXT("Mode 1 needs Transmittance"),
		FHillaireLutManager::ShouldVisualizeDebug(1, true, false, false, false, true));
	TestFalse(TEXT("Mode 1 without Transmittance"),
		FHillaireLutManager::ShouldVisualizeDebug(1, false, true, true, true, true));
	TestTrue(TEXT("Mode 2 needs MultiScattering"),
		FHillaireLutManager::ShouldVisualizeDebug(2, false, true, false, false, true));
	TestFalse(TEXT("Mode 2 without MultiScattering"),
		FHillaireLutManager::ShouldVisualizeDebug(2, true, false, true, true, true));
	TestTrue(TEXT("Mode 3 needs SkyView"),
		FHillaireLutManager::ShouldVisualizeDebug(3, false, false, true, false, true));
	TestFalse(TEXT("Mode 3 without SkyView"),
		FHillaireLutManager::ShouldVisualizeDebug(3, true, true, false, true, true));
	TestTrue(TEXT("Mode 4 needs the aerial volume"),
		FHillaireLutManager::ShouldVisualizeDebug(4, true, true, true, true, true));
	TestFalse(TEXT("Mode 4 without the aerial volume"),
		FHillaireLutManager::ShouldVisualizeDebug(4, true, true, true, false, true));
	TestTrue(TEXT("Mode 5 needs the profile alone"),
		FHillaireLutManager::ShouldVisualizeDebug(5, false, false, false, false, false));
	TestTrue(TEXT("Mode 6 needs the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(6, true, true, true, true, true));
	TestFalse(TEXT("Mode 6 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(6, true, true, true, true, false));
	TestTrue(TEXT("Mode 9 needs the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(9, true, true, true, true, true));
	TestFalse(TEXT("Mode 9 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(9, true, true, true, true, false));
	TestTrue(TEXT("Mode 10 needs ray frame + Transmittance"),
		FHillaireLutManager::ShouldVisualizeDebug(10, true, false, false, false, true));
	TestFalse(TEXT("Mode 10 without Transmittance"),
		FHillaireLutManager::ShouldVisualizeDebug(10, false, false, false, false, true));
	TestTrue(TEXT("Mode 11 needs ray frame + SkyView"),
		FHillaireLutManager::ShouldVisualizeDebug(11, false, false, true, false, true));
	TestFalse(TEXT("Mode 11 without SkyView"),
		FHillaireLutManager::ShouldVisualizeDebug(11, false, false, false, false, true));
	TestTrue(TEXT("Mode 12 needs the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(12, true, true, true, true, true));
	TestFalse(TEXT("Mode 12 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(12, true, true, true, true, false));
	TestTrue(TEXT("Mode 16 needs the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(16, true, true, true, true, true));
	TestFalse(TEXT("Mode 16 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(16, true, true, true, true, false));
	TestTrue(TEXT("Mode 18 needs the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(18, true, true, true, true, true));
	TestFalse(TEXT("Mode 18 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(18, true, true, true, true, false));
	TestTrue(TEXT("Mode 19 needs the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(19, true, true, true, true, true));
	TestFalse(TEXT("Mode 19 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(19, true, true, true, true, false));
	TestTrue(TEXT("Mode 20 needs the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(20, true, true, true, true, true));
	TestFalse(TEXT("Mode 20 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(20, true, true, true, true, false));
	TestTrue(TEXT("Mode 21 needs ray frame + T + MS"),
		FHillaireLutManager::ShouldVisualizeDebug(21, true, true, false, false, true));
	TestFalse(TEXT("Mode 21 without MultiScattering"),
		FHillaireLutManager::ShouldVisualizeDebug(21, true, false, false, false, true));
	TestFalse(TEXT("Mode 21 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(21, true, true, false, false, false));
	TestTrue(TEXT("Mode 22 needs ray frame + T + MS"),
		FHillaireLutManager::ShouldVisualizeDebug(22, true, true, false, false, true));
	TestFalse(TEXT("Mode 22 without the ray frame"),
		FHillaireLutManager::ShouldVisualizeDebug(22, true, true, false, false, false));
	TestFalse(TEXT("Mode 23 is out of range"),
		FHillaireLutManager::ShouldVisualizeDebug(23, true, true, true, true, true));
	TestFalse(TEXT("Negative mode is out of range"),
		FHillaireLutManager::ShouldVisualizeDebug(-1, true, true, true, true, true));
	return true;
}
