// ANDROMEDA - SKY TWILIGHT RESPONSE TESTS (CPU diagnostic + regression lock).
//
// Problem: GPU PIE showed day-blue collapsing almost immediately to black on
// the night side, with no believable twilight band or sunrise/sunset.
// ZEPHYR never multiplies the physical sky (out = scene + presentation), so a
// missing twilight must originate in the ATMOS SkyView content itself or in
// how it is sampled - never in a ZEPHYR gradient (there is none).
//
// Method: reuse the exact CPU mirror of the GPU march (HillaireLutCpu - same
// math as HillaireSkyViewLut.usf / HillaireLutCore.ush) on a representative
// small normalized planet, sweeping the primary-sun elevation cosine while
// probing fixed LUT texels:
//   toward-sun horizon (u~0, just above horizon), anti-sun horizon (u~1),
//   zenith, toward-sun mid-sky.
// Physical expectations (no palettes, ratios only):
//   - sunset asymmetry: toward-sun horizon >> anti-sun horizon;
//   - low-angle reddening: toward-sun R/B rises as the sun drops;
//   - twilight persistence: -6 deg band >> deep-night floor (no instant black);
//   - night dark: deep-night transfer ~0 everywhere sampled;
//   - day bright: zenith transfer clearly nonzero.
// The test logs the full sweep table (AddInfo) so a failure prints physics,
// not just a boolean.

#include "Misc/AutomationTest.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"
#include "HillaireLutCpu.h"
#include "HillairePlanetState.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FTwilightProbe
	{
		FIntPoint Coord;
		const TCHAR* Name;
	};

	float ProbeLuminance(const FLinearColor& C)
	{
		return 0.299f * C.R + 0.587f * C.G + 0.114f * C.B;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkyTwilightProgressionTest,
	"Andromeda.Sky.TwilightProgression",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSkyTwilightProgressionTest::RunTest(const FString& Parameters)
{
	// Representative small Andromeda planet (thickness-normalized profile:
	// zenith optical depth matches the Earth reference by construction).
	const float GroundKm = 5.0f;
	const float ThickKm = 1.0f;
	const FHillairePlanetState PlanetState = HillaireMakeExternalPlanetState(
		0, FGuid(0x7ED001, 0, 0, 0), FName(TEXT("TwilightProbe")),
		FVector::ZeroVector, FQuat::Identity,
		GroundKm, ThickKm, 0.05f,
		FHillaireAtmosphereProfile::MakeReferenceProfile());
	const FHillaireAtmosphereProfile& Profile = PlanetState.Profile;
	if (!TestTrue(TEXT("Probe planet valid"), PlanetState.IsValid()))
	{
		return false;
	}

	const float ViewHeightKm = GroundKm + 0.05f + 0.002f; // 2 m over terrain
	const FVector3f UpLocal(0.0f, 0.0f, 1.0f);

	// Height/sun-independent LUTs baked once.
	TArray<FLinearColor> TransLut, MsLut;
	HillaireLutCpu::BakeTransmittanceLut(Profile,
		HillaireLimits::TransmittanceWidth, HillaireLimits::TransmittanceHeight, TransLut);
	HillaireLutCpu::BakeFullMultiScatteringLut(Profile, TransLut,
		HillaireLimits::TransmittanceWidth, HillaireLimits::TransmittanceHeight,
		HillaireLimits::MultiScatteringRes, 1.0f, MsLut);
	if (!TestTrue(TEXT("T/MS LUTs baked"), TransLut.Num() > 0 && MsLut.Num() > 0))
	{
		return false;
	}

	// Fixed texel probes (192x108 SkyView parameterization):
	// u = sqrt(-lightView*0.5+0.5): u~0 = toward sun, u~1 = anti-sun.
	// v < 0.5 = above horizon, v -> 0.5- = horizon grazing.
	const FTwilightProbe Probes[] = {
		{ FIntPoint(2, 50), TEXT("SunHorizon") },
		{ FIntPoint(189, 50), TEXT("AntiHorizon") },
		{ FIntPoint(96, 2), TEXT("Zenith") },
		{ FIntPoint(2, 30), TEXT("SunMidSky") },
	};
	constexpr int32 NumProbes = UE_ARRAY_COUNT(Probes);

	// Sun elevation sweep (cosine from planet-local up; bake consumes the
	// elevation through its SunDir reconstruction, azimuth-symmetric).
	const float ElevCosSweep[] = { 0.5f, 0.17f, 0.0f, -0.10f, -0.21f, -0.50f };
	constexpr int32 NumElev = UE_ARRAY_COUNT(ElevCosSweep);

	// [elev][probe] baked transfer values.
	TArray<FLinearColor> Values;
	Values.SetNum(NumElev * NumProbes);
	for (int32 E = 0; E < NumElev; ++E)
	{
		const float C = ElevCosSweep[E];
		const FVector3f SunLocal(
			FMath::Sqrt(FMath::Max(0.0f, 1.0f - C * C)), 0.0f, C);
		TArray<FIntPoint> Coords;
		for (int32 P = 0; P < NumProbes; ++P)
		{
			Coords.Add(Probes[P].Coord);
		}
		TArray<FLinearColor> Out;
		HillaireLutCpu::BakeSkyViewTexels(Profile, TransLut,
			HillaireLimits::TransmittanceWidth, HillaireLimits::TransmittanceHeight,
			MsLut, HillaireLimits::MultiScatteringRes,
			ViewHeightKm, SunLocal, UpLocal,
			HillaireLimits::SkyViewWidth, HillaireLimits::SkyViewHeight,
			Coords, Out);
		if (!TestEqual(TEXT("Probe count"), Out.Num(), NumProbes))
		{
			return false;
		}
		for (int32 P = 0; P < NumProbes; ++P)
		{
			Values[E * NumProbes + P] = Out[P];
		}
	}

	// Sweep table for the log (luminance + R/B per probe per elevation).
	for (int32 E = 0; E < NumElev; ++E)
	{
		FString Row = FString::Printf(TEXT("elevCos=%+.2f:"), ElevCosSweep[E]);
		for (int32 P = 0; P < NumProbes; ++P)
		{
			const FLinearColor& C = Values[E * NumProbes + P];
			const float RB = (C.B > 1e-9f) ? (C.R / C.B) : -1.0f;
			Row += FString::Printf(TEXT(" %s L=%.5f R/B=%.3f"), Probes[P].Name, ProbeLuminance(C), RB);
		}
		AddInfo(Row);
	}

	auto Lum = [&](int32 E, int32 P) { return ProbeLuminance(Values[E * NumProbes + P]); };
	auto RB = [&](int32 E, int32 P)
	{
		const FLinearColor& C = Values[E * NumProbes + P];
		return (C.B > 1e-9f) ? (C.R / C.B) : 0.0f;
	};

	// Indices: probes 0=SunHorizon 1=AntiHorizon 2=Zenith 3=SunMidSky;
	// elevations 0=day(0.5) 1=low(0.17) 2=sunset(0) 3=twilight(-0.10) 5=night(-0.50).
	// NOTE: the LUT holds unit-white TRANSFER (dim by design); composite gain
	// (SunScale x PreExposure) applies later. Thresholds are transfer-scale.
	TestTrue(TEXT("Day zenith nonzero transfer"), Lum(0, 2) > 0.001f);
	TestTrue(TEXT("Day/night zenith ordering"), Lum(0, 2) > 50.0f * Lum(5, 2));
	TestTrue(TEXT("Sunset asymmetry toward>>anti"),
		Lum(2, 0) > 3.0f * Lum(2, 1));
	TestTrue(TEXT("Low-angle reddening emerges at sunset"),
		RB(2, 0) > 1.5f * RB(0, 0));
	TestTrue(TEXT("Twilight persists (no instant black)"),
		Lum(3, 0) > 5.0f * Lum(5, 0));
	TestTrue(TEXT("Night dark"), Lum(5, 0) < 0.02f && Lum(5, 2) < 0.02f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
