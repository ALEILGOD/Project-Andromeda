// HILLAIRE ATMOSPHERE - SKY BACKGROUND AUTOMATION TESTS.
//
// Production sky path: the BeforeDOF hook samples the existing Phase-2B
// SkyView LUT for the current camera/view (governing planet, slot-0 primary
// sun, unit-white transfer x ColorAttenuation x PreExposure, composited as
// sky + T * background per the reference FASTSKY branch). Small targeted
// tests for the NEW render hook/sampling only:
//   1. gating truth table (pure, no GPU);
//   2. CPU-mirror response (height / sun / pixel dependence, opaque identity);
//   2b. terminator gate (shared solar-elevation curve, day/night response,
//       transmittance never gated, broad sunset, aerial scale bounds);
//   2c. continuous sunset response (five overlapping elevation bands:
//       exact day/night identity, elevation continuity with no jumps,
//       sun-direction dependence, horizon localization, sunrise/sunset path
//       symmetry, sun-chromaticity planet variation);
//   2d. aerial presentation scales (distance ramp near->far, entry altitude
//       scale, transmittance linearity, warm terrain integration at sunset);
//   3. composite equation (background attenuated by view transmittance);
//   4. view-input rotation order with a spinning (non-identity) planet;
//   5. end-to-end sky with real perspective matrices (nadir vs zenith);
//   6. GPU execution proof (production EnsurePlanetLuts + CompositeSkyBackground).
// Phase 2A-2F suites must keep passing untouched: LUT generation math and the
// aerial volume bake are not modified here (presentation lives at composite).

#include "Misc/AutomationTest.h"

#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"
#include "HillaireLightSource.h"
#include "HillaireLutCpu.h"
#include "HillaireLutDiagnostics.h"
#include "HillaireLutManager.h"
#include "HillairePlanetState.h"
#include "HillaireRdgHelpers.h"
#include "HillaireShaders.h"
#include "HillaireViewSnapshot.h"
#include "Math/PerspectiveMatrix.h"
#include "Math/RotationMatrix.h"
#include "RenderCommandFence.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "SceneUniformBuffer.h"

namespace
{
	// Test projection inverse: uniform scale diag(100) -> view = clip * 100.
	// Fully self-consistent (no engine projection conventions): the sky ray
	// fan derives from it on both CPU and GPU, so agreement is structural.
	FMatrix MakeSkyTestInvProj()
	{
		FMatrix M(EForceInit::ForceInitToZero);
		M.M[0][0] = 100.0; M.M[1][1] = 100.0; M.M[2][2] = 100.0; M.M[3][3] = 1.0;
		return M;
	}

	static TUniformBufferRef<FViewUniformShaderParameters> MakeSkyTestViewUB(float PreExposure)
	{
		FViewUniformShaderParameters ViewUB;
		FMemory::Memset(&ViewUB, 0, sizeof(ViewUB));
		ViewUB.PreExposure = PreExposure;
		return TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(
			ViewUB, UniformBuffer_SingleFrame, EUniformBufferValidation::None);
	}

	struct FSkyFixture
	{
		FHillaireAtmosphereProfile Profile;
		TArray<FLinearColor> TransLut;
		TArray<FLinearColor> MsLut;
		TArray<FLinearColor> SkyLut;
		int32 TW = 0, TH = 0, MSR = 0, SVW = 0, SVH = 0;
		float ViewHeightKm = 0.0f;
		FVector3f SunDir = FVector3f(0.0f, 0.0f, 1.0f);
		FVector3f CamLocal = FVector3f::ZeroVector;
		FMatrix InvProj;
		FMatrix ViewToPlanetRot = FMatrix::Identity;

		void Bake(const FVector3f& InSun, float HeightOverGroundKm)
		{
			Profile = FHillaireAtmosphereProfile::MakeReferenceProfile();
			TW = HillaireLimits::TransmittanceWidth;
			TH = HillaireLimits::TransmittanceHeight;
			MSR = HillaireLimits::MultiScatteringRes;
			SVW = HillaireLimits::SkyViewWidth;
			SVH = HillaireLimits::SkyViewHeight;
			HillaireLutCpu::BakeTransmittanceLut(Profile, TW, TH, TransLut);
			HillaireLutCpu::BakeFullMultiScatteringLut(Profile, TransLut, TW, TH, MSR, 1.0f, MsLut);
			ViewHeightKm = Profile.BottomRadiusKm + HeightOverGroundKm;
			SunDir = InSun.GetSafeNormal();
			CamLocal = FVector3f(0.0f, 0.0f, ViewHeightKm);
			InvProj = MakeSkyTestInvProj();
			const FVector3f Up = CamLocal.GetSafeNormal();
			HillaireLutCpu::BakeFullSkyViewLut(Profile, TransLut, TW, TH, MsLut, MSR,
				ViewHeightKm, SunDir, Up, SkyLut);
		}

		FLinearColor RunSky(const FLinearColor& In, float DeviceZ, float U, float V) const
		{
			return HillaireLutCpu::SampleSkyBackgroundPixel(In, DeviceZ, U, V,
				InvProj, ViewToPlanetRot, CamLocal, SunDir,
				FVector3f(1.0f, 1.0f, 1.0f),
				Profile.BottomRadiusKm, Profile.TopRadiusKm, ViewHeightKm,
				SkyLut, SVW, SVH, TransLut, TW, TH, 1.0f);
		}
	};

	bool SkyAllFinite(const TArray<FLinearColor>& Data)
	{
		for (const FLinearColor& C : Data)
		{
			if (!FMath::IsFinite(C.R) || !FMath::IsFinite(C.G) || !FMath::IsFinite(C.B) || !FMath::IsFinite(C.A))
			{
				return false;
			}
		}
		return true;
	}

	float SkyBgLuminance(const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; }

	// UE row-vector map (out_j = sum_i V_i * M[i][j]), independent of the code
	// under test: mirrors FMatrix::TransformVector4 / VectorTransformVector.
	FVector SkyRowMulPoint(const FMatrix& M, const FVector4& V)
	{
		return FVector(
			V.X * M.M[0][0] + V.Y * M.M[1][0] + V.Z * M.M[2][0] + V.W * M.M[3][0],
			V.X * M.M[0][1] + V.Y * M.M[1][1] + V.Z * M.M[2][1] + V.W * M.M[3][1],
			V.X * M.M[0][2] + V.Y * M.M[1][2] + V.Z * M.M[2][2] + V.W * M.M[3][2]);
	}
	FVector SkyRowMulDir(const FMatrix& M, const FVector& V)
	{
		return FVector(
			V.X * M.M[0][0] + V.Y * M.M[1][0] + V.Z * M.M[2][0],
			V.X * M.M[0][1] + V.Y * M.M[1][1] + V.Z * M.M[2][1],
			V.X * M.M[0][2] + V.Y * M.M[1][2] + V.Z * M.M[2][2]);
	}
}

// ---------------------------------------------------------------------------
// Test 1 - Gating truth table (pure, no GPU).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundGatingTest,
	"Hillaire.SkyBackground.Gating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundGatingTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("CVar off blocks"), FHillaireLutManager::ShouldCompositeSky(false, true, true, true, true, true));
	TestFalse(TEXT("No content blocks"), FHillaireLutManager::ShouldCompositeSky(true, false, true, true, true, true));
	TestFalse(TEXT("No primary blocks"), FHillaireLutManager::ShouldCompositeSky(true, true, false, true, true, true));
	TestFalse(TEXT("No SkyView blocks"), FHillaireLutManager::ShouldCompositeSky(true, true, true, false, true, true));
	TestFalse(TEXT("No transmittance blocks"), FHillaireLutManager::ShouldCompositeSky(true, true, true, true, false, true));
	TestTrue(TEXT("All open runs"), FHillaireLutManager::ShouldCompositeSky(true, true, true, true, true, true));
	// The aerial gate is untouched by the sky work (regression anchor): it keys
	// on the in-graph evaluation inputs (T + MS), not on a pooled volume.
	TestTrue(TEXT("Aerial gate intact"),
		FHillaireLutManager::ShouldCompositeAerial(true, true, true, true, true)
		&& !FHillaireLutManager::ShouldCompositeAerial(true, true, false, true, true)
		&& !FHillaireLutManager::ShouldCompositeAerial(true, true, true, false, true));
	return true;
}

// ---------------------------------------------------------------------------
// Test 2 - CPU-mirror response (height / sun / pixel dependence).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundCpuResponseTest,
	"Hillaire.SkyBackground.CpuResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundCpuResponseTest::RunTest(const FString& Parameters)
{
	FSkyFixture Noon;
	Noon.Bake(FVector3f(0.0f, 0.0f, 1.0f), 1.0f);
	if (!TestEqual(TEXT("Sky LUT texel count"), Noon.SkyLut.Num(), Noon.SVW * Noon.SVH))
	{
		return false;
	}

	const FLinearColor BgIn(0.02f, 0.03f, 0.05f, 1.0f);

	// Opaque pixels are identical (aerial owns them; no double-scatter).
	const FLinearColor Opaque = Noon.RunSky(BgIn, 0.5f, 0.5f, 0.5f);
	TestTrue(TEXT("Opaque exact identity"), Opaque == BgIn);

	// Sky pixels carry live scattered radiance (not identity, not a constant).
	const FLinearColor Zenith = Noon.RunSky(BgIn, 0.0f, 0.5f, 0.5f);
	TestTrue(TEXT("Sky finite + alpha"), SkyAllFinite({ Zenith }) && Zenith.A == 1.0f);
	TestTrue(TEXT("Sky alters background"), Zenith != BgIn);
	TestTrue(TEXT("Sky is lit (positive)"), SkyBgLuminance(Zenith) > 1e-6f);

	// Pixel dependence: different view rays sample different sky.
	const FLinearColor Corner = Noon.RunSky(BgIn, 0.0f, 0.05f, 0.05f);
	TestTrue(TEXT("Pixel-dependent sky"), Corner != Zenith);

	// Camera-height dependence: same ray, LUT baked at 1 km vs 50 km.
	FSkyFixture High;
	High.Bake(FVector3f(0.0f, 0.0f, 1.0f), 50.0f);
	const FLinearColor ZenithHigh = High.RunSky(BgIn, 0.0f, 0.5f, 0.5f);
	TestTrue(TEXT("Height changes sky"), ZenithHigh != Zenith);
	AddInfo(FString::Printf(TEXT("L surface=%.6f high=%.6f corner=%.6f"),
		SkyBgLuminance(Zenith), SkyBgLuminance(ZenithHigh), SkyBgLuminance(Corner)));

	// Sun dependence: same observer, LUT baked under a 2-deg-elevation sun.
	FSkyFixture Sunset;
	{
		const float El = FMath::Cos(88.0f * PI / 180.0f);
		Sunset.Bake(FVector3f(FMath::Sqrt(1.0f - El * El), 0.0f, El), 1.0f);
	}
	const FLinearColor ZenithSunset = Sunset.RunSky(BgIn, 0.0f, 0.5f, 0.5f);
	TestTrue(TEXT("Sun changes sky"), ZenithSunset != Zenith);
	AddInfo(FString::Printf(TEXT("L noon=%.6f sunset=%.6f"),
		SkyBgLuminance(Zenith), SkyBgLuminance(ZenithSunset)));
	return true;
}

// ---------------------------------------------------------------------------
// Test 2b - Terminator gate (sky composite).
//
// The normalized volumetric density fills the full 1.10x envelope, so the
// optically significant atmosphere reaches a large height and the geometric
// earth shadow keeps the upper limb illuminated far past civil twilight. The
// composite enforces the terminator with the same smooth solar-elevation curve
// the ZEPHYR presentation layer uses (ZephyrTypes.h
// ZephyrPresentation::DaylightFactor), evaluated at the ray's atmosphere point.
// This test locks: (a) the shared curve; (b) day preserved; (c) night
// extinguished; (d) the gate applies ONLY to the sun-scaled inscatter, never to
// the view-ray transmittance (background stars stay attenuated); (e) broad
// NMS-style sunset (horizon stays bright, twilight contracts gradually);
// (f) aerial presentation scale bounds (terrain/sky separation).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundTerminatorGateTest,
	"Hillaire.SkyBackground.TerminatorGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundTerminatorGateTest::RunTest(const FString& Parameters)
{
	// (a) Shared curve: same constants/smoothstep as the ZEPHYR presentation
	// curve (ZEPHYR_TWILIGHT_BEGIN_ELEV_COS / ZEPHYR_FULL_DAY_ELEV_COS).
	TestEqual(TEXT("Zenith sun = full day"),
		HillaireLimits::TerminatorFactor(1.0f), 1.0f);
	TestEqual(TEXT("Deep night extinct"),
		HillaireLimits::TerminatorFactor(-1.0f), 0.0f);
	TestEqual(TEXT("Nautical twilight extinct"),
		HillaireLimits::TerminatorFactor(HillaireLimits::TerminatorBeginElevCos), 0.0f);
	TestEqual(TEXT("Full day at threshold"),
		HillaireLimits::TerminatorFactor(HillaireLimits::TerminatorFullElevCos), 1.0f);
	const float MidTwilight = HillaireLimits::TerminatorFactor(
		0.5f * (HillaireLimits::TerminatorBeginElevCos + HillaireLimits::TerminatorFullElevCos));
	TestTrue(TEXT("Mid twilight strictly between day and night"),
		MidTwilight > 0.0f && MidTwilight < 1.0f);
	TestTrue(TEXT("Curve monotonic"),
		HillaireLimits::TerminatorFactor(0.10f) > HillaireLimits::TerminatorFactor(-0.05f));
	// (e) Broad sunset: horizon stays bright (no thin-line collapse), -6 deg
	// twilight contracts gradually but persists, nautical depth extinct.
	TestTrue(TEXT("Sunset horizon bright (broad band)"),
		HillaireLimits::TerminatorFactor(0.0f) > 0.6f);
	TestTrue(TEXT("Twilight -6deg contracts but persists"),
		HillaireLimits::TerminatorFactor(-0.105f) > 0.1f
		&& HillaireLimits::TerminatorFactor(-0.105f) < 0.5f);
	TestTrue(TEXT("Nautical depth extinct"),
		HillaireLimits::TerminatorFactor(-0.30f) == 0.0f);
	// (f) Aerial presentation scale: isolated, sub-unity, sky untouched.
	TestTrue(TEXT("Aerial scale in (0,1)"),
		HillaireLimits::AerialInscatterPresentationScale > 0.0f
		&& HillaireLimits::AerialInscatterPresentationScale < 1.0f);

	// (b/c) Composite response: day lit, night extinguished, twilight present.
	FSkyFixture Day;
	Day.Bake(FVector3f(0.0f, 0.0f, 1.0f), 1.0f);
	FSkyFixture Night;
	Night.Bake(FVector3f(0.5f, 0.0f, -0.866f), 1.0f); // sun ~ -60 deg
	FSkyFixture Sunset;
	Sunset.Bake(FVector3f(0.99985f, 0.0f, 0.01745f), 1.0f); // sun +1 deg

	const FLinearColor Black(0.0f, 0.0f, 0.0f, 1.0f);
	const FLinearColor DaySky = Day.RunSky(Black, 0.0f, 0.5f, 0.5f);
	const FLinearColor NightSky = Night.RunSky(Black, 0.0f, 0.5f, 0.5f);
	const FLinearColor SunsetSky = Sunset.RunSky(Black, 0.0f, 0.5f, 0.5f);

	TestTrue(TEXT("Day sky lit"), SkyBgLuminance(DaySky) > 1e-4f);
	TestTrue(TEXT("Night sky extinguished"), SkyBgLuminance(NightSky) < 1e-6f);
	TestTrue(TEXT("Day >> night"), SkyBgLuminance(DaySky) > 100.0f * SkyBgLuminance(NightSky));
	TestTrue(TEXT("Twilight present (not instant black)"), SkyBgLuminance(SunsetSky) > 0.0f);

	// (d) Night output equals background * view transmittance: the sun-scaled
	// term is exactly zero, the transmittance is NOT gated.
	float TU = 0.0f, TV = 0.0f;
	HillaireLutCpu::TransmittanceParamsToUv(
		Night.Profile.BottomRadiusKm, Night.Profile.TopRadiusKm,
		Night.ViewHeightKm, 1.0f, TU, TV);
	const FVector3f T = HillaireLutCpu::SampleLutBilinear(
		Night.TransLut, Night.TW, Night.TH, TU, TV);
	const float TMean = (T.X + T.Y + T.Z) / 3.0f;
	const FLinearColor Bg(0.02f, 0.03f, 0.05f, 1.0f);
	const FLinearColor NightBg = Night.RunSky(Bg, 0.0f, 0.5f, 0.5f);
	TestTrue(TEXT("Night = background * view transmittance"),
		FMath::Abs(NightBg.R - Bg.R * TMean) < 1e-5f
		&& FMath::Abs(NightBg.G - Bg.G * TMean) < 1e-5f
		&& FMath::Abs(NightBg.B - Bg.B * TMean) < 1e-5f);

	AddInfo(FString::Printf(TEXT("Terminator: dayL=%.6f sunsetL=%.6f nightL=%.8f Tmean=%.4f"),
		SkyBgLuminance(DaySky), SkyBgLuminance(SunsetSky), SkyBgLuminance(NightSky), TMean));
	return true;
}

// ---------------------------------------------------------------------------
// Test 2c - Continuous sunset response (five overlapping elevation bands).
//
// The single-scalar terminator reads as DAY -> factor -> warm band -> dark
// ("a blocchi"). The pass-2 response re-balances the existing LUT transfer
// with five overlapping smooth elevation bands (violet/pink/gold/orange/red)
// masked by sun-direction and horizon proximity, applied to the atmospheric
// contribution only. This test locks: exact day/night identity (no residual),
// elevation continuity (no discrete jumps), sun-direction dependence (warmest
// toward the sun, cooler anti-sun), horizon localization (red low), orange
// visibility at the horizon, sunrise/sunset family symmetry at composite
// level, and sun-chromaticity planet variation. All pure-function checks run
// on HillaireLimits (the CPU mirror of HillaireCommon.ush); the composite
// checks reuse the baked fixture path.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundSunsetResponseTest,
	"Hillaire.SkyBackground.SunsetResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
	// Hand-built rotation sending the view-center ray to planet-local D
	// (row-vector convention: row 2 = ray dir; same rig as HorizonBandScan).
	FMatrix SunsetCenterRayRot(const FVector3f& D)
	{
		FVector3f R0 = FVector3f::CrossProduct(FVector3f(0.0f, 1.0f, 0.0f), D).GetSafeNormal();
		if (R0.SizeSquared() < 0.5f)
		{
			R0 = FVector3f(1.0f, 0.0f, 0.0f);
		}
		const FVector3f R1 = FVector3f::CrossProduct(D, R0).GetSafeNormal();
		FMatrix Rot(EForceInit::ForceInitToZero);
		Rot.M[0][0] = R0.X; Rot.M[0][1] = R0.Y; Rot.M[0][2] = R0.Z;
		Rot.M[1][0] = R1.X; Rot.M[1][1] = R1.Y; Rot.M[1][2] = R1.Z;
		Rot.M[2][0] = D.X;  Rot.M[2][1] = D.Y;  Rot.M[2][2] = D.Z;
		Rot.M[3][3] = 1.0f;
		return Rot;
	}
}

bool FHillaireSkyBackgroundSunsetResponseTest::RunTest(const FString& Parameters)
{
	const FVector3f WhiteChroma = HillaireLimits::SunsetSunChroma(FVector3f(1.0f, 1.0f, 1.0f));

	// (a) Exact day/night identity: every band weight is exactly 0 at day and
	// below nautical twilight, so the multiplier is
	// exactly (1,1,1): day stays native blue, night stays extinct, no warm
	// residual, no cool wash.
	{
		const float Windows[5][4] = {
			{ -0.04f, 0.00f, 0.08f, 0.20f }, { -0.11f, -0.05f, 0.02f, 0.10f },
			{ -0.07f, -0.02f, 0.04f, 0.12f }, { -0.14f, -0.07f, -0.01f, 0.06f },
			{ -0.20f, -0.14f, -0.08f, -0.02f } };
		bool bDayZero = true, bNightZero = true, bHighDayZero = true;
		for (const auto& W : Windows)
		{
			bDayZero &= HillaireLimits::SunsetBandWeight(1.0f, W[0], W[1], W[2], W[3]) == 0.0f;
			bNightZero &= HillaireLimits::SunsetBandWeight(-0.5f, W[0], W[1], W[2], W[3]) == 0.0f;
			bHighDayZero &= HillaireLimits::SunsetBandWeight(0.2f, W[0], W[1], W[2], W[3]) == 0.0f;
		}
		TestTrue(TEXT("All bands exact 0 at day"), bDayZero);
		TestTrue(TEXT("All bands exact 0 at night"), bNightZero);
		TestTrue(TEXT("All bands exact 0 above +11 deg"), bHighDayZero);
		const FVector3f DayM = HillaireLimits::SunsetSkyMultiplier(1.0f, 1.0f, 0.0f, WhiteChroma);
		const FVector3f NightM = HillaireLimits::SunsetSkyMultiplier(-0.5f, 1.0f, 0.0f, WhiteChroma);
		const FVector3f DayA = HillaireLimits::SunsetAerialMultiplier(1.0f, WhiteChroma);
		const FVector3f NightA = HillaireLimits::SunsetAerialMultiplier(-0.5f, WhiteChroma);
		TestTrue(TEXT("Sky mult exact identity at day"),
			DayM.X == 1.0f && DayM.Y == 1.0f && DayM.Z == 1.0f);
		TestTrue(TEXT("Sky mult exact identity at night"),
			NightM.X == 1.0f && NightM.Y == 1.0f && NightM.Z == 1.0f);
		TestTrue(TEXT("Aerial mult exact identity at day"),
			DayA.X == 1.0f && DayA.Y == 1.0f && DayA.Z == 1.0f);
		TestTrue(TEXT("Aerial mult exact identity at night"),
			NightA.X == 1.0f && NightA.Y == 1.0f && NightA.Z == 1.0f);
	}

	// (b) Orange/gold must be fully present AT the horizon (the missing-orange
	// report): gold plateaus at 1.0, orange > 0.85 at elev 0.
	TestTrue(TEXT("Gold full at horizon"),
		HillaireLimits::SunsetBandWeight(0.0f, -0.07f, -0.02f, 0.04f, 0.12f) == 1.0f);
	TestTrue(TEXT("Orange strong at horizon"),
		HillaireLimits::SunsetBandWeight(0.0f, -0.14f, -0.07f, -0.01f, 0.06f) > 0.85f);

	// (c) Elevation continuity: sweep day -> twilight -> night at fixed
	// toward-sun horizon geometry. A hard-band model would jump ~0.3+ between
	// adjacent samples; the overlapping smoothstep response stays far below.
	// The reverse traversal recomputes the identical elevation list, so it
	// must match bit-exactly (pure function: sunrise traverses the identical
	// progression, no hysteresis, no sign branches).
	{
		TArray<float> Elevs;
		for (int32 K = 0; K <= 275; ++K)
		{
			Elevs.Add(0.25f - (float)K * 0.002f);
		}
		TArray<FVector3f> Forward;
		Forward.Reserve(Elevs.Num());
		float MaxStep = 0.0f;
		FVector3f Prev = HillaireLimits::SunsetSkyMultiplier(Elevs[0], 1.0f, 0.0f, WhiteChroma);
		Forward.Add(Prev);
		for (int32 K = 1; K < Elevs.Num(); ++K)
		{
			const FVector3f M = HillaireLimits::SunsetSkyMultiplier(Elevs[K], 1.0f, 0.0f, WhiteChroma);
			MaxStep = FMath::Max(MaxStep, FMath::Abs(M.X - Prev.X));
			MaxStep = FMath::Max(MaxStep, FMath::Abs(M.Y - Prev.Y));
			MaxStep = FMath::Max(MaxStep, FMath::Abs(M.Z - Prev.Z));
			Forward.Add(M);
			Prev = M;
		}
		TestTrue(TEXT("Sunset elevation response continuous (no blocks)"), MaxStep < 0.15f);
		AddInfo(FString::Printf(TEXT("Sunset sweep max adjacent delta=%.5f"), MaxStep));
		bool bMirror = true;
		for (int32 K = Elevs.Num() - 1; K >= 0; --K)
		{
			const FVector3f M = HillaireLimits::SunsetSkyMultiplier(Elevs[K], 1.0f, 0.0f, WhiteChroma);
			const FVector3f& F = Forward[K];
			bMirror &= (M.X == F.X && M.Y == F.Y && M.Z == F.Z);
		}
		TestTrue(TEXT("Sunrise traverses the identical response (symmetric)"), bMirror);

		// Directional sweep at fixed sunset elevation: also continuous, and
		// the pow() sun lobes stay smooth through anti-sun (derivative 0).
		float MaxDirStep = 0.0f;
		FVector3f PrevD = HillaireLimits::SunsetSkyMultiplier(-0.02f, -1.0f, 0.0f, WhiteChroma);
		for (float L = -1.0f + 0.02f; L <= 1.0f; L += 0.02f)
		{
			const FVector3f M = HillaireLimits::SunsetSkyMultiplier(-0.02f, L, 0.0f, WhiteChroma);
			MaxDirStep = FMath::Max(MaxDirStep, FMath::Abs(M.X - PrevD.X));
			MaxDirStep = FMath::Max(MaxDirStep, FMath::Abs(M.Y - PrevD.Y));
			MaxDirStep = FMath::Max(MaxDirStep, FMath::Abs(M.Z - PrevD.Z));
			PrevD = M;
		}
		TestTrue(TEXT("Sun-direction response continuous"), MaxDirStep < 0.10f);
	}

	// (d) Sun-direction dependence at sunset: warmest toward the sun, cooler
	// (but non-identical) anti-sun twilight. Hand-verified anchors at
	// e=-0.02, horizon, white sun: toward R ~1.85, anti R ~1.09.
	{
		const FVector3f Toward = HillaireLimits::SunsetSkyMultiplier(-0.02f, 1.0f, 0.0f, WhiteChroma);
		const FVector3f Anti = HillaireLimits::SunsetSkyMultiplier(-0.02f, -1.0f, 0.0f, WhiteChroma);
		TestTrue(TEXT("Toward-sun R in warm band"), Toward.X > 1.78f && Toward.X < 1.93f);
		TestTrue(TEXT("Toward much warmer than anti"), Toward.X > 1.5f * Anti.X);
		TestTrue(TEXT("Anti-sun twilight present but cooler"),
			FMath::Abs(Anti.X - 1.0f) > 0.02f && Anti.X < Toward.X);
		AddInfo(FString::Printf(TEXT("Sunset toward R=%.4f anti R=%.4f"), Toward.X, Anti.X));
	}

	// (e) Horizon localization: at twilight red the horizon warms far more
	// than the zenith (red stays low, zenith goes deep blue).
	{
		const FVector3f Hor = HillaireLimits::SunsetSkyMultiplier(-0.10f, 1.0f, 0.0f, WhiteChroma);
		const FVector3f Zen = HillaireLimits::SunsetSkyMultiplier(-0.10f, 1.0f, 1.0f, WhiteChroma);
		TestTrue(TEXT("Red localized to horizon"), Hor.X > Zen.X + 0.15f);
	}

	// (f) Warming progression + aerial anchors (hand-verified: aerial R ~1.43,
	// B ~0.88 at e=-0.02, white sun).
	{
		const FVector3f Early = HillaireLimits::SunsetSkyMultiplier(0.10f, 1.0f, 0.0f, WhiteChroma);
		const FVector3f Late = HillaireLimits::SunsetSkyMultiplier(-0.02f, 1.0f, 0.0f, WhiteChroma);
		TestTrue(TEXT("R strengthens toward sunset"), Late.X > Early.X);
		const float EarlyRB = Early.X / FMath::Max(Early.Z, 1e-6f);
		const float LateRB = Late.X / FMath::Max(Late.Z, 1e-6f);
		TestTrue(TEXT("Reddening progresses (R/B rises)"), LateRB > EarlyRB);
		const FVector3f Aer = HillaireLimits::SunsetAerialMultiplier(-0.02f, WhiteChroma);
		TestTrue(TEXT("Aerial R anchor"), Aer.X > 1.36f && Aer.X < 1.50f);
		TestTrue(TEXT("Aerial B anchor"), Aer.Z > 0.81f && Aer.Z < 0.94f);
	}

	// (g) Planet variation: a red star shifts the response (sun-chromaticity
	// coupling), white sun stays reference (1-ulp tolerance: the luma
	// weights sum to 1.0 within float rounding).
	TestTrue(TEXT("White sun chroma is identity"),
		FMath::Abs(WhiteChroma.X - 1.0f) < 1e-6f
		&& FMath::Abs(WhiteChroma.Y - 1.0f) < 1e-6f
		&& FMath::Abs(WhiteChroma.Z - 1.0f) < 1e-6f);
	{
		const FVector3f RedChroma = HillaireLimits::SunsetSunChroma(FVector3f(1.0f, 0.35f, 0.15f));
		const FVector3f RedM = HillaireLimits::SunsetSkyMultiplier(-0.02f, 1.0f, 0.0f, RedChroma);
		const FVector3f RefM = HillaireLimits::SunsetSkyMultiplier(-0.02f, 1.0f, 0.0f, WhiteChroma);
		TestTrue(TEXT("Red star shifts sunset (profile-driven variation)"),
			FMath::Abs(RedM.X - RefM.X) > 0.02f && SkyAllFinite({ FLinearColor(RedM.X, RedM.Y, RedM.Z, 1.0f) }));
	}

	// (h) Composite level: horizontal-sun fixture, toward vs anti horizon rays
	// (diag-100 rig) plus a mirrored below-horizon (sunrise) fixture. Sunset
	// must be strongly asymmetric and warm; sunrise must belong to the same
	// warm family (not black, not a different effect).
	{
		FSkyFixture Sunset, Sunrise;
		Sunset.Bake(FVector3f(1.0f, 0.0f, 0.0f), 1.0f);
		Sunrise.Bake(FVector3f(0.99985f, 0.0f, -0.01745f).GetSafeNormal(), 1.0f);
		const FLinearColor BgBlack(0.0f, 0.0f, 0.0f, 1.0f);
		const FVector3f WhiteSun(1.0f, 1.0f, 1.0f);
		auto RunDir = [&](const FSkyFixture& F, const FVector3f& D) -> FLinearColor
		{
			return HillaireLutCpu::SampleSkyBackgroundPixel(BgBlack, 0.0f, 0.5f, 0.5f,
				F.InvProj, SunsetCenterRayRot(D), F.CamLocal, F.SunDir, WhiteSun,
				F.Profile.BottomRadiusKm, F.Profile.TopRadiusKm, F.ViewHeightKm,
				F.SkyLut, F.SVW, F.SVH, F.TransLut, F.TW, F.TH, 1.0f);
		};
		const FLinearColor SetToward = RunDir(Sunset, FVector3f(1.0f, 0.0f, 0.0f));
		const FLinearColor SetAnti = RunDir(Sunset, FVector3f(-1.0f, 0.0f, 0.0f));
		const FLinearColor RiseToward = RunDir(Sunrise, FVector3f(1.0f, 0.0f, 0.0f));
		TestTrue(TEXT("Sunset rays finite"),
			SkyAllFinite({ SetToward, SetAnti, RiseToward }));
		const float LumT = SkyBgLuminance(SetToward), LumA = SkyBgLuminance(SetAnti);
		const float LumR = SkyBgLuminance(RiseToward);
		TestTrue(TEXT("Sunset strongly asymmetric toward>>anti"), LumT > 2.0f * LumA);
		const float RBT = SetToward.R / FMath::Max(SetToward.B, 1e-9f);
		const float RBA = SetAnti.R / FMath::Max(SetAnti.B, 1e-9f);
		TestTrue(TEXT("Sunset toward warm absolute (R/B>1)"), RBT > 1.0f);
		TestTrue(TEXT("Sunset toward redder than anti"), RBT > RBA);
		const float RBR = RiseToward.R / FMath::Max(RiseToward.B, 1e-9f);
		TestTrue(TEXT("Sunrise warm too (symmetric family)"), RBR > 1.0f && LumR > 1e-6f);
		TestTrue(TEXT("Sunrise comparable to sunset (same effect, reversed)"),
			LumR > 0.2f * LumT && LumR < 5.0f * LumT);
		AddInfo(FString::Printf(TEXT("Sunset toward L=%.6f R/B=%.3f anti L=%.6f R/B=%.3f sunrise L=%.6f R/B=%.3f"),
			LumT, RBT, LumA, RBA, LumR, RBR));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Test 2d - Aerial presentation scales (distance ramp + entry altitude).
//
// Root causes locked here:
// - Mountain darkness: the flat 0.35 in-scatter scale kept full physical
//   extinction (1-AP.a) while paying only 35% of the compensating in-scatter,
//   so far terrain crushed toward black. The distance ramp (0.35 near, 1.0
//   far, smooth in the slice coordinate) restores the energy balance at
//   distance while the validated near behavior is bit-preserved.
// - Entry blue wash: high-altitude columns rendered full in-scatter over the
//   terrain (surface treated as volume). The altitude scale (1.0 surface,
//   0.35 top, smooth) keeps entry subtle and restores continuously on descent.
// Transmittance is never scaled (linearity check); sunset warms (not blues)
// distant terrain.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireAerialPresentationScalesTest,
	"Hillaire.Aerial.PresentationScales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireAerialPresentationScalesTest::RunTest(const FString& Parameters)
{
	// (a) Pure-function endpoints: near == validated 0.35 (held flat through
	// the mid range by the knee), far == physical 1.0.
	// (1-ulp tolerance: FMath::Lerp endpoint arithmetic rounds, e.g.
	// Lerp(1, 0.35, 1) lands one ulp from 0.35; the composite checks below
	// lock the wired values to 1e-4.)
	TestTrue(TEXT("Near endpoint is the validated 0.35"),
		FMath::Abs(HillaireLimits::AerialDistanceScale(0.125f) - 0.35f) < 1e-6f
		&& FMath::Abs(HillaireLimits::AerialDistanceScale(0.6f) - 0.35f) < 1e-6f);
	TestTrue(TEXT("Far endpoint is physical 1.0"),
		FMath::Abs(HillaireLimits::AerialDistanceScale(1.0f) - 1.0f) < 1e-6f);
	TestTrue(TEXT("Beyond-range eases toward the haze asymptote"),
		FMath::Abs(HillaireLimits::AerialDistanceScale(1.5f) - 0.775f) < 1e-4f
		&& FMath::Abs(HillaireLimits::AerialDistanceScale(2.0f) - 0.55f) < 1e-3f
		&& FMath::Abs(HillaireLimits::AerialDistanceScale(5.0f) - 0.55f) < 1e-6f);
	TestTrue(TEXT("Surface altitude is 1.0"), HillaireLimits::AerialAltitudeScale(0.0f) == 1.0f);
	TestTrue(TEXT("Top altitude is entry-min 0.25"),
		FMath::Abs(HillaireLimits::AerialAltitudeScale(1.0f) - 0.25f) < 1e-6f);
	TestTrue(TEXT("Altitude clamps outside [0,1]"),
		HillaireLimits::AerialAltitudeScale(-1.0f) == 1.0f
		&& FMath::Abs(HillaireLimits::AerialAltitudeScale(2.0f) - 0.25f) < 1e-6f);

	// (b) Shape: non-decreasing out to full coverage (w = 1), then a smooth
	// C1 ease down to the beyond-range haze asymptote (no steps anywhere).
	{
		float PrevD = HillaireLimits::AerialDistanceScale(0.0f);
		float MaxStepD = 0.0f;
		bool bMonoUp = true;
		for (float W = 0.01f; W <= 1.0f; W += 0.01f)
		{
			const float V = HillaireLimits::AerialDistanceScale(W);
			bMonoUp &= V >= PrevD;
			MaxStepD = FMath::Max(MaxStepD, V - PrevD);
			PrevD = V;
		}
		TestTrue(TEXT("Distance ramp monotonic non-decreasing to w=1"), bMonoUp);
		TestTrue(TEXT("Distance ramp continuous to w=1"), MaxStepD < 0.05f);
		TestTrue(TEXT("Distance ramp peaks at full coverage"),
			FMath::Abs(HillaireLimits::AerialDistanceScale(1.0f) - 1.0f) < 1e-6f);
		float PrevB = HillaireLimits::AerialDistanceScale(1.0f);
		float MaxStepB = 0.0f;
		bool bMonoDown = true;
		for (float W = 1.01f; W <= 3.0f; W += 0.01f)
		{
			const float V = HillaireLimits::AerialDistanceScale(W);
			bMonoDown &= V <= PrevB + 1e-6f;
			MaxStepB = FMath::Max(MaxStepB, PrevB - V);
			PrevB = V;
		}
		TestTrue(TEXT("Beyond-range ease monotonic non-increasing"), bMonoDown);
		TestTrue(TEXT("Beyond-range ease continuous"), MaxStepB < 0.05f);
		TestTrue(TEXT("Beyond-range settles on the asymptote"),
			HillaireLimits::AerialDistanceScale(3.0f) >= 0.54f);
		float PrevA = HillaireLimits::AerialAltitudeScale(0.0f);
		float MaxStepA = 0.0f;
		bool bMonoA = true;
		for (float H = 0.01f; H <= 1.0f; H += 0.01f)
		{
			const float V = HillaireLimits::AerialAltitudeScale(H);
			bMonoA &= V <= PrevA;
			MaxStepA = FMath::Max(MaxStepA, PrevA - V);
			PrevA = V;
		}
		TestTrue(TEXT("Altitude ramp monotonic non-increasing"), bMonoA);
		TestTrue(TEXT("Altitude ramp continuous"), MaxStepA < 0.10f);
	}

	// (c) Composite with a synthetic constant volume (32^3, transfer + opacity
	// known exactly): near identity, far physical, entry attenuated, sunset
	// reddened, transmittance linear, sky passthrough.
	const int32 VW = HillaireLimits::AerialVolumeSize;
	TArray<FLinearColor> Volume;
	Volume.Init(FLinearColor(0.02f, 0.03f, 0.05f, 0.4f), VW * VW * VW);
	const FLinearColor Scene(0.4f, 0.3f, 0.2f, 1.0f);
	const FVector3f WhiteSun(1.0f, 1.0f, 1.0f);
	const float KmPerSlice = 0.04f; // 1.0 km envelope / 25

	FMatrix NearInvProj(EForceInit::ForceInitToZero);
	NearInvProj.M[0][0] = 100.0; NearInvProj.M[1][1] = 100.0; NearInvProj.M[2][2] = 100.0; NearInvProj.M[3][3] = 1.0;
	// Far: center-pixel path 1.28 km = Slice 32 on a 0.04 slice (w = 1.0
	// exactly: full-coverage endpoint, scale exactly 1.0).
	FMatrix FarInvProj(EForceInit::ForceInitToZero);
	FarInvProj.M[0][0] = 256000.0; FarInvProj.M[1][1] = 256000.0; FarInvProj.M[2][2] = 256000.0; FarInvProj.M[3][3] = 1.0;
	// Beyond coverage: w = 1.5 (2.88 km, scale 0.775) and w = 2.0 (5.12 km,
	// scale 0.55): the clamped deepest slice must not compound the full x30
	// veil on unrepresentative marches.
	FMatrix Beyond15InvProj(EForceInit::ForceInitToZero);
	Beyond15InvProj.M[0][0] = 576000.0; Beyond15InvProj.M[1][1] = 576000.0; Beyond15InvProj.M[2][2] = 576000.0; Beyond15InvProj.M[3][3] = 1.0;
	FMatrix Beyond20InvProj(EForceInit::ForceInitToZero);
	Beyond20InvProj.M[0][0] = 1024000.0; Beyond20InvProj.M[1][1] = 1024000.0; Beyond20InvProj.M[2][2] = 1024000.0; Beyond20InvProj.M[3][3] = 1.0;

	// Sky pixels pass through identical (no aerial on background).
	{
		const FLinearColor SkyOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.0f, 0.5f, 0.5f, NearInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, 1.0f);
		TestTrue(TEXT("Sky pixel identity"),
			SkyOut.R == Scene.R && SkyOut.G == Scene.G && SkyOut.B == Scene.B && SkyOut.A == Scene.A);
	}

	// Near terrain keeps native identity (validated behavior preserved).
	{
		const FLinearColor NearOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, NearInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, 1.0f);
		TestTrue(TEXT("Near terrain native (<5% deviation)"),
			FMath::Abs(NearOut.R - Scene.R) / Scene.R < 0.05f
			&& FMath::Abs(NearOut.G - Scene.G) / Scene.G < 0.05f
			&& FMath::Abs(NearOut.B - Scene.B) / Scene.B < 0.05f);
		AddInfo(FString::Printf(TEXT("Near out=(%.6f,%.6f,%.6f)"), NearOut.R, NearOut.G, NearOut.B));
	}

	// Far terrain at the surface restores the full physical composite
	// (Out = Scene*(1-A) + Sun*AP): the mountain-darkness fix. w = 1.0
	// exactly here, so the beyond-range ease does not participate.
	{
		const FLinearColor FarOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, 1.0f);
		TestTrue(TEXT("Far terrain full physical composite"),
			FMath::Abs(FarOut.R - (0.4f * 0.6f + 0.02f)) < 1e-4f
			&& FMath::Abs(FarOut.G - (0.3f * 0.6f + 0.03f)) < 1e-4f
			&& FMath::Abs(FarOut.B - (0.2f * 0.6f + 0.05f)) < 1e-4f);
		AddInfo(FString::Printf(TEXT("Far out=(%.6f,%.6f,%.6f)"), FarOut.R, FarOut.G, FarOut.B));
	}

	// Beyond coverage (w = 1.5/2.0): the ease trims the veil on stale
	// clamped samples (0.775/0.55) while transmittance stays untouched.
	{
		const FLinearColor B15 = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, Beyond15InvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, 1.0f);
		TestTrue(TEXT("Beyond-range w=1.5 eased veil"),
			FMath::Abs(B15.R - (0.4f * 0.6f + 0.02f * 0.775f)) < 1e-4f
			&& FMath::Abs(B15.G - (0.3f * 0.6f + 0.03f * 0.775f)) < 1e-4f
			&& FMath::Abs(B15.B - (0.2f * 0.6f + 0.05f * 0.775f)) < 1e-4f);
		const FLinearColor B20 = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, Beyond20InvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, 1.0f);
		TestTrue(TEXT("Beyond-range w=2.0 at asymptote"),
			FMath::Abs(B20.R - (0.4f * 0.6f + 0.02f * 0.55f)) < 1e-4f
			&& FMath::Abs(B20.G - (0.3f * 0.6f + 0.03f * 0.55f)) < 1e-4f
			&& FMath::Abs(B20.B - (0.2f * 0.6f + 0.05f * 0.55f)) < 1e-4f);
		AddInfo(FString::Printf(TEXT("Beyond w=1.5 (%.6f,%.6f,%.6f) w=2.0 (%.6f,%.6f,%.6f)"),
			B15.R, B15.G, B15.B, B20.R, B20.G, B20.B));
	}

	// Entry (just inside the top, alt 0.98): subtle in-scatter over the
	// physical transmittance (surface stays a surface, no blue volume).
	// The boundary fade is exactly 1.0 here (full just inside, 0 at the
	// boundary for continuity with the outside gate).
	{
		TestTrue(TEXT("Boundary fade full just inside"),
			FMath::Abs(HillaireLimits::AerialBoundaryFade(0.98f) - 1.0f) < 1e-6f);
		TestTrue(TEXT("Boundary fade half at mid-blend"),
			FMath::Abs(HillaireLimits::AerialBoundaryFade(0.995f) - 0.5f) < 1e-6f);
		TestTrue(TEXT("Boundary fade zero at the top"),
			HillaireLimits::AerialBoundaryFade(1.0f) == 0.0f);
		const FLinearColor EntryOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.98f, 1.0f);
		TestTrue(TEXT("Entry attenuates in-scatter only"),
			FMath::Abs(EntryOut.R - (0.4f * 0.6f + 0.02f * 0.25f)) < 1e-3f
			&& FMath::Abs(EntryOut.G - (0.3f * 0.6f + 0.03f * 0.25f)) < 1e-3f
			&& FMath::Abs(EntryOut.B - (0.2f * 0.6f + 0.05f * 0.25f)) < 1e-3f);
		const FLinearColor FarOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, 1.0f);
		TestTrue(TEXT("Entry subtler than surface (continuous descent restores)"),
			SkyBgLuminance(EntryOut) < SkyBgLuminance(FarOut));
	}

	// Exact boundary (alt 1.0): inscatter fully faded, transmittance intact
	// (bit-continuous with the outside identity path).
	{
		const FLinearColor EdgeOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 1.0f, 1.0f);
		TestTrue(TEXT("Boundary passes transmittance only"),
			FMath::Abs(EdgeOut.R - 0.4f * 0.6f) < 1e-4f
			&& FMath::Abs(EdgeOut.G - 0.3f * 0.6f) < 1e-4f
			&& FMath::Abs(EdgeOut.B - 0.2f * 0.6f) < 1e-4f);
	}

	// Sunset warms (reddens) distant terrain instead of bluing it.
	{
		const FLinearColor DayOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, 1.0f);
		const FLinearColor SetOut = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, -0.02f);
		const float DayAddR = DayOut.R - Scene.R * 0.6f, DayAddB = DayOut.B - Scene.B * 0.6f;
		const float SetAddR = SetOut.R - Scene.R * 0.6f, SetAddB = SetOut.B - Scene.B * 0.6f;
		TestTrue(TEXT("Sunset adds warm (not blue) in-scatter"),
			SetAddR / FMath::Max(SetAddB, 1e-9f) > 0.02f / 0.05f);
		TestTrue(TEXT("Sunset R in-scatter exceeds day"), SetAddR > DayAddR);
		AddInfo(FString::Printf(TEXT("Aerial added R/B day=%.3f sunset=%.3f"),
			DayAddR / FMath::Max(DayAddB, 1e-9f), SetAddR / FMath::Max(SetAddB, 1e-9f)));
	}

	// Transmittance path is never presentation-scaled: doubling the scene
	// doubles exactly the transmitted part, in-scatter unchanged.
	{
		const FLinearColor O1 = HillaireLutCpu::CompositeAerialPixel(
			Scene, 0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, -0.02f);
		const FLinearColor O2 = HillaireLutCpu::CompositeAerialPixel(
			FLinearColor(Scene.R * 2.0f, Scene.G * 2.0f, Scene.B * 2.0f, 1.0f),
			0.5f, 0.5f, 0.5f, FarInvProj, Volume, VW, VW, VW,
			WhiteSun, 1.0f, KmPerSlice, 0.0f, -0.02f);
		TestTrue(TEXT("Transmittance linear (presentation never touches it)"),
			FMath::Abs((O2.R - O1.R) - Scene.R * 0.6f) < 1e-4f
			&& FMath::Abs((O2.G - O1.G) - Scene.G * 0.6f) < 1e-4f
			&& FMath::Abs((O2.B - O1.B) - Scene.B * 0.6f) < 1e-4f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Test 3 - Composite equation (reference: sky + T * background).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundCompositeEquationTest,
	"Hillaire.SkyBackground.CompositeEquation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundCompositeEquationTest::RunTest(const FString& Parameters)
{
	FSkyFixture Fix;
	Fix.Bake(FVector3f(0.0f, 0.0f, 1.0f), 1.0f);

	// Black background isolates the sky term; gray reveals the transmittance:
	// T = (Out_gray - Out_black) / gray per channel, must lie in [0, 1].
	const FLinearColor Black(0.0f, 0.0f, 0.0f, 1.0f);
	const FLinearColor Gray(0.4f, 0.4f, 0.4f, 1.0f);
	bool bOk = true;
	for (float V : { 0.25f, 0.5f, 0.75f })
	{
		const FLinearColor OutBlack = Fix.RunSky(Black, 0.0f, 0.5f, V);
		const FLinearColor OutGray = Fix.RunSky(Gray, 0.0f, 0.5f, V);
		if (!SkyAllFinite({ OutBlack, OutGray })) { bOk = false; break; }
		const float TR = (OutGray.R - OutBlack.R) / 0.4f;
		const float TG = (OutGray.G - OutBlack.G) / 0.4f;
		const float TB = (OutGray.B - OutBlack.B) / 0.4f;
		AddInfo(FString::Printf(TEXT("V=%.2f T=(%.4f,%.4f,%.4f) skyL=%.6f"),
			V, TR, TG, TB, SkyBgLuminance(OutBlack)));
		if (TR < -1e-4f || TR > 1.0f + 1e-4f
			|| TG < -1e-4f || TG > 1.0f + 1e-4f
			|| TB < -1e-4f || TB > 1.0f + 1e-4f)
		{
			bOk = false;
			break;
		}
		// Sky term present on black (the LUT actually feeds the output).
		if (!(SkyBgLuminance(OutBlack) > 1e-6f)) { bOk = false; break; }
	}
	TestTrue(TEXT("Sky + T*Bg with T in [0,1]"), bOk);
	return true;
}

// ---------------------------------------------------------------------------
// Test 4 - View-input rotation order with a spinning planet (real matrices).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundRotationOrderTest,
	"Hillaire.SkyBackground.RotationOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundRotationOrderTest::RunTest(const FString& Parameters)
{
	// Spinning planet (STARMAP ticks SetActorRotation every frame) under a
	// rotated camera: the composed matrix must apply the view rotation first
	// (view -> world) and the planet conjugation second (world -> local),
	// matching the reference mul(gSkyInvViewMat, HViewPos) + quat-conjugate.
	const FQuat PlanetQ(FVector(0.0f, 0.0f, 1.0f), PI * 0.5f);
	const FMatrix SnapView = FRotationMatrix(FRotator(30.0f, 45.0f, 0.0f));
	const FVector3f CenterRel(100.0f, -50.0f, 25.0f);

	const FHillaireLutManager::FHillaireAerialViewInputs Got =
		FHillaireLutManager::ComputeAerialViewInputs(
			CenterRel, PlanetQ, SnapView, FMatrix::Identity);

	// Independent expectation with UE's own composition (not the code under test).
	FMatrix ViewToWorld(EForceInit::ForceInitToZero);
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 C = 0; C < 3; ++C)
		{
			ViewToWorld.M[R][C] = SnapView.M[C][R];
		}
	}
	ViewToWorld.M[3][3] = 1.0;
	const FMatrix Expected = ViewToWorld * PlanetQ.Inverse().ToMatrix();

	bool bMatch = true;
	for (int32 R = 0; R < 4 && bMatch; ++R)
	{
		for (int32 C = 0; C < 4 && bMatch; ++C)
		{
			if (FMath::Abs(Got.ViewToPlanetLocalRot.M[R][C] - Expected.M[R][C]) > 1e-4f)
			{
				bMatch = false;
			}
		}
	}
	TestTrue(TEXT("Composed rotation order matches UE composition"), bMatch);

	// Direction check: the composed map must send a view-space ray to the
	// planet-local frame (row-vector application, UE convention).
	const FVector DView(0.0f, 0.0f, 1.0f);
	const FVector DGot = SkyRowMulDir(Got.ViewToPlanetLocalRot, DView).GetSafeNormal();
	const FVector DWorld = SkyRowMulDir(ViewToWorld, DView);
	const FVector DExpected = PlanetQ.Inverse().RotateVector(DWorld).GetSafeNormal();
	TestTrue(TEXT("Ray maps view -> world -> planet-local"),
		FVector::Dist(DGot, DExpected) < 1e-4f);
	AddInfo(FString::Printf(TEXT("got=(%.4f,%.4f,%.4f) exp=(%.4f,%.4f,%.4f)"),
		DGot.X, DGot.Y, DGot.Z, DExpected.X, DExpected.Y, DExpected.Z));

	// The old (reversed) order provably differs here, so this test guards it.
	const FMatrix Reversed = PlanetQ.Inverse().ToMatrix() * ViewToWorld;
	const FVector DReversed = SkyRowMulDir(Reversed, DView).GetSafeNormal();
	TestTrue(TEXT("Reversed order would differ (test is sensitive)"),
		FVector::Dist(DReversed, DExpected) > 1e-3f);
	return true;
}

// ---------------------------------------------------------------------------
// Test 5 - End-to-end sky with real perspective matrices (nadir vs zenith).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundRealPerspectiveTest,
	"Hillaire.SkyBackground.RealPerspective",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundRealPerspectiveTest::RunTest(const FString& Parameters)
{
	FSkyFixture Fix;
	Fix.Bake(FVector3f(0.0f, 0.0f, 1.0f), 1.0f);
	const float B = Fix.Profile.BottomRadiusKm;
	const float T = Fix.Profile.TopRadiusKm;

	// Real perspective projection (km units, reversed-Z like production views).
	const FMatrix Persp = FReversedZPerspectiveMatrix(PI * 0.25f, 16.0f, 9.0f, 0.001f, 100000.0f);
	const FMatrix InvPersp = Persp.Inverse();

	// Camera 1 km over the +Z pole: camLocal = +Z*(B+1), center rel = -camLocal.
	const FVector3f CamLocal(0.0f, 0.0f, B + 1.0f);
	const FVector3f CenterRel = -CamLocal;

	auto RunForView = [&](const FMatrix& SnapView) -> FLinearColor
	{
		const FHillaireLutManager::FHillaireAerialViewInputs Inputs =
			FHillaireLutManager::ComputeAerialViewInputs(
				CenterRel, FQuat::Identity, SnapView, Persp);
		return HillaireLutCpu::SampleSkyBackgroundPixel(
			FLinearColor(0.02f, 0.03f, 0.05f, 1.0f), 0.0f, 0.5f, 0.5f,
			Inputs.InvProjMatrix, Inputs.ViewToPlanetLocalRot,
			Inputs.CameraPlanetLocalKm, Fix.SunDir,
			FVector3f(1.0f, 1.0f, 1.0f),
			B, T, B + 1.0f,
			Fix.SkyLut, Fix.SVW, Fix.SVH, Fix.TransLut, Fix.TW, Fix.TH, 1.0f);
	};

	// Nadir view (forward = -up): UE view space is Z-forward (clip.w comes
	// from view.Z; the optical axis is view +Z), so the camera forward is
	// matrix ROW 2. Rows X=(1,0,0), Y=(0,-1,0), Z=(0,0,-1) (right-handed).
	FMatrix NadirView(EForceInit::ForceInitToZero);
	NadirView.M[0][0] = 1.0f; NadirView.M[0][1] = 0.0f; NadirView.M[0][2] = 0.0f;
	NadirView.M[1][0] = 0.0f; NadirView.M[1][1] = -1.0f; NadirView.M[1][2] = 0.0f;
	NadirView.M[2][0] = 0.0f; NadirView.M[2][1] = 0.0f; NadirView.M[2][2] = -1.0f;
	NadirView.M[3][3] = 1.0f;
	// Zenith view (forward = +up): identity (view axes = world axes).
	FMatrix ZenithView = FMatrix::Identity;

	// Independent center-ray check: clip (0,0,0.5) must map to view forward.
	{
		const FVector Clip(0.0f, 0.0f, 0.5f);
		const FVector4 H = FVector4(
			Clip.X * InvPersp.M[0][0] + Clip.Y * InvPersp.M[1][0] + Clip.Z * InvPersp.M[2][0] + InvPersp.M[3][0],
			Clip.X * InvPersp.M[0][1] + Clip.Y * InvPersp.M[1][1] + Clip.Z * InvPersp.M[2][1] + InvPersp.M[3][1],
			Clip.X * InvPersp.M[0][2] + Clip.Y * InvPersp.M[1][2] + Clip.Z * InvPersp.M[2][2] + InvPersp.M[3][2],
			Clip.X * InvPersp.M[0][3] + Clip.Y * InvPersp.M[1][3] + Clip.Z * InvPersp.M[2][3] + InvPersp.M[3][3]);
		const FVector DView = (FVector(H.X, H.Y, H.Z) / H.W).GetSafeNormal();
		const FVector DWorld = SkyRowMulDir(NadirView, DView).GetSafeNormal();
		// Nadir rig looks along -Z world: transpose-map the optical axis there.
		AddInfo(FString::Printf(TEXT("center ray world=(%.4f,%.4f,%.4f)"), DWorld.X, DWorld.Y, DWorld.Z));
		TestTrue(TEXT("Center ray aims nadir"), FVector::Dist(DWorld, FVector(0.0f, 0.0f, -1.0f)) < 1e-3f);
	}

	const FLinearColor Nadir = RunForView(NadirView);
	const FLinearColor Zenith = RunForView(ZenithView);
	TestTrue(TEXT("Both finite + alpha"), SkyAllFinite({ Nadir, Zenith }) && Nadir.A == 1.0f && Zenith.A == 1.0f);
	TestTrue(TEXT("Nadir (ground branch) differs from zenith"), Nadir != Zenith);
	AddInfo(FString::Printf(TEXT("L nadir=%.6f zenith=%.6f"), SkyBgLuminance(Nadir), SkyBgLuminance(Zenith)));
	return true;
}

// ---------------------------------------------------------------------------
// Test 6 - GPU execution proof (production path, real LUTs, readback).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundGpuExecutionTest,
	"Hillaire.SkyBackground.GpuExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundGpuExecutionTest::RunTest(const FString& Parameters)
{
	// NullRHI/commandlet: no render device, RDG cannot run (same guard as
	// RealPlanetFrame: skip the GPU half explicitly instead of crashing).
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("GPU execution skipped (no render device / NullRHI)."));
		return true;
	}
	// ---- GT: planet + overhead sun -> snapshot (mirrors Phase-2D GPU test) ----
	FHillaireAtmosphereProfile Profile = FHillaireAtmosphereProfile::MakeReferenceProfile();

	FHillairePlanetState Planet;
	Planet.PlanetId = 0;
	Planet.PlanetGuid = FGuid(7000, 0, 0, 0);
	Planet.PlanetName = FName(TEXT("SkyGpuTestPlanet"));
	Planet.CenterCmWorld = FVector::ZeroVector;
	Planet.RotationWorld = FQuat::Identity;
	Planet.Profile = Profile;
	Planet.GroundRadiusKm = Profile.BottomRadiusKm;
	Planet.AtmosphereRadiusKm = Profile.TopRadiusKm;
	Planet.TerrainHeightKm = 0.0f;
	Planet.StarDistanceKm = -1.0f;

	FHillaireLightSource Sun;
	Sun.LightId = FGuid(7001, 0, 0, 0);
	Sun.LightName = FName(TEXT("SkyGpuTestSun"));
	Sun.bEnabled = true;
	Sun.bDirectional = true;
	Sun.WorldPositionCm = FVector::ZeroVector;
	Sun.WorldDirectionToLight = FVector(0.0, 0.0, 1.0);
	Sun.Color = FLinearColor::White;
	Sun.Intensity = 1.0f;

	const double CamHeightKm = (double)Profile.BottomRadiusKm + 1.0;
	const FVector OriginCm(0.0, 0.0, CamHeightKm * 100000.0);
	TArray<FHillairePlanetState> Planets; Planets.Add(Planet);
	TArray<FHillaireLightSource> Lights; Lights.Add(Sun);

	const FHillaireViewSnapshot Snap = FHillaireViewSnapshotBuilder::Build(
		Planets, Lights, OriginCm, FMatrix::Identity, FMatrix::Identity,
		FIntRect(0, 0, 64, 64), FVector(1.0, 0.0, 0.0));
	if (!TestTrue(TEXT("Snapshot has atmosphere content"), Snap.HasAtmosphereContent()))
	{
		return false;
	}
	int32 SnapIdx = 0;
	for (int32 i = 0; i < Snap.Planets.Num(); ++i)
	{
		if (Snap.Planets[i].bIsGoverning) { SnapIdx = i; break; }
	}
	const FHillaireSnapshotPlanet& SnapPlanet = Snap.Planets[SnapIdx];
	if (!TestTrue(TEXT("Snapshot planet has a resolved light"), SnapPlanet.ResolvedLights.Count > 0))
	{
		return false;
	}

	// ---- RT: production EnsurePlanetLuts -> CompositeSkyBackground ----
	struct FGpuSkyResult
	{
		bool bGraphOk = false;
		bool bOutputValid = false;
		TRefCountPtr<IPooledRenderTarget> Pooled;
	};
	TSharedPtr<FHillaireLutManager> Mgr = MakeShared<FHillaireLutManager>();
	TSharedPtr<FGpuSkyResult> RtResult = MakeShared<FGpuSkyResult>();

	const int32 RtPlanetId = SnapPlanet.PlanetId;
	const FHillaireAtmosphereProfile RtProfile = SnapPlanet.Profile;
	const float RtHeightKm = SnapPlanet.ViewHeightKm;
	const FVector3f RtCenter = SnapPlanet.CenterCamRelativeKm;
	const FQuat RtRotation = SnapPlanet.Rotation;
	const FHillaireCompactedLights RtLights = SnapPlanet.ResolvedLights;
	const float RtMsFactor = Snap.MultipleScatteringFactor;
	const FMatrix RtView = Snap.ViewMatrix;
	const FMatrix RtProj = Snap.ProjectionMatrix;

	ENQUEUE_RENDER_COMMAND(HillaireSkyBackgroundGpu)(
		[Mgr, RtResult, RtPlanetId, RtProfile, RtHeightKm, RtCenter, RtRotation, RtLights,
			RtMsFactor, RtView, RtProj](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			Mgr->RegisterPlanet(RtPlanetId);
			FHillaireLutManager::FHillairePlanetLutGraphOutputs Outputs;
			const bool bLutsOk = Mgr->EnsurePlanetLuts(
				GraphBuilder, RtPlanetId, RtProfile, RtHeightKm,
				RtCenter, RtRotation, RtLights, RtMsFactor, true, Outputs);
			const FHillaireLutManager::FHillaireAerialViewInputs ViewInputs =
				FHillaireLutManager::ComputeAerialViewInputs(
					RtCenter, RtRotation, RtView, RtProj);
			// In-domain SkyView sampling height (mirror of EnsurePlanetLuts).
			const float HeightFloorKm = RtProfile.BottomRadiusKm + HillaireLimits::ViewHeightEpsFloorKm;
			const float HeightCeilKm = FMath::Max(
				HeightFloorKm, RtProfile.TopRadiusKm - HillaireLimits::ViewHeightEpsFloorKm);
			const float ClampedHeightKm = FMath::Clamp(RtHeightKm, HeightFloorKm, HeightCeilKm);

			// Synthetic scene: uniform dark color + SKY depth (reversed-Z far).
			FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(
				FIntPoint(64, 64), PF_FloatRGBA, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef ColorTex = GraphBuilder.CreateTexture(ColorDesc, TEXT("Hillaire.SkyGpuColor"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ColorTex), FLinearColor(0.02f, 0.03f, 0.05f, 1.0f));
			FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(
				FIntPoint(64, 64), PF_R32_FLOAT, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef DepthTex = GraphBuilder.CreateTexture(DepthDesc, TEXT("Hillaire.SkyGpuDepth"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DepthTex), 0.0f);

			FRDGTextureRef OutTex = nullptr;
			const bool bSkyOk = bLutsOk && Outputs.SkyView != nullptr && Outputs.Transmittance != nullptr
&& Mgr->CompositeSkyBackground(
			GraphBuilder,
			GraphBuilder.CreateSRV(ColorTex),
			DepthTex,
			MakeSkyTestViewUB(1.0f),
			ViewInputs.InvProjMatrix,
			ViewInputs.CameraPlanetLocalKm,
			ViewInputs.ViewToPlanetLocalRot,
			RtLights.Lights[0].LightDirLocal,
			RtLights.Lights[0].ColorAttenuation,
			RtProfile.BottomRadiusKm,
			RtProfile.TopRadiusKm,
			ClampedHeightKm,
			RtLights.Lights[0].AngularRadiusRad,
			Outputs.SkyView,
			Outputs.Transmittance,
			FIntRect(0, 0, 64, 64),
			OutTex);
			if (OutTex)
			{
				GraphBuilder.QueueTextureExtraction(OutTex, &RtResult->Pooled);
			}
			GraphBuilder.Execute();
			RtResult->bGraphOk = bSkyOk && OutTex != nullptr;
			RtResult->bOutputValid = RtResult->bGraphOk && RtResult->Pooled.IsValid();
		});
	{
		FRenderCommandFence Fence;
		Fence.BeginFence();
		Fence.Wait();
	}
	if (!TestTrue(TEXT("GPU graph executed with sky output"), RtResult->bGraphOk))
	{
		return false;
	}
	if (!TestTrue(TEXT("GPU sky pooled target valid"), RtResult->bOutputValid))
	{
		return false;
	}

	// ---- GT: readback + gates ----
	TRefCountPtr<IPooledRenderTarget> Pooled = RtResult->Pooled;
	TArray<FLinearColor> GpuOut;
	if (!TestTrue(TEXT("GPU sky readback"),
		HillaireLutDiagnostics::ReadbackPooledLut(Pooled, GpuOut)))
	{
		return false;
	}
	TestEqual(TEXT("GPU output texel count"), GpuOut.Num(), 64 * 64);
	const FHillaireLutStats GpuStats = HillaireLutDiagnostics::AnalyzeLut(GpuOut);
	AddInfo(*HillaireLutDiagnostics::DescribeStats(TEXT("GpuSky"), GpuStats));
	TestEqual(TEXT("GPU sky NaN"), GpuStats.NaNCount, (int64)0);
	TestEqual(TEXT("GPU sky Inf"), GpuStats.InfCount, (int64)0);
	TestTrue(TEXT("GPU sky finite"), SkyAllFinite(GpuOut));

	// Sky depth everywhere: every pixel must carry sampled sky (output != input).
	bool bAnySky = false;
	for (const FLinearColor& C : GpuOut)
	{
		if (C != FLinearColor(0.02f, 0.03f, 0.05f, 1.0f)) { bAnySky = true; break; }
	}
	TestTrue(TEXT("Sky alters background"), bAnySky);

	// CPU agreement on the identical configuration (bilinear HW vs CPU approx).
	FSkyFixture Fix;
	Fix.Bake(FVector3f(0.0f, 0.0f, 1.0f), 1.0f);
	const FLinearColor CpuOut = Fix.RunSky(
		FLinearColor(0.02f, 0.03f, 0.05f, 1.0f), 0.0f, 0.5f, 0.5f);
	const FLinearColor& GpuMid = GpuOut[32 * 64 + 32];
	const float Num = FMath::Abs(GpuMid.R - CpuOut.R) + FMath::Abs(GpuMid.G - CpuOut.G) + FMath::Abs(GpuMid.B - CpuOut.B);
	const float Den = FMath::Abs(CpuOut.R) + FMath::Abs(CpuOut.G) + FMath::Abs(CpuOut.B);
	const double RelErr = Den > 1e-6 ? (double)Num / (double)Den : 1.0;
	AddInfo(FString::Printf(TEXT("GPU-vs-CPU mid rel err=%.4f"), RelErr));
	TestTrue(TEXT("GPU-vs-CPU agreement < 15%"), RelErr < 0.15);
	return true;
}

// ---------------------------------------------------------------------------
// Test 7b - Horizon band scan: WHERE is the bright limb, in LUT and on screen?
//
// Diagnostic for "uniform blue haze" reports. Logs (a) the baked SkyView LUT
// row profile (mean luminance per row: above-horizon y<54, below y>=54) and
// (b) a center-pixel viewZenith sweep 0..180 deg through the production CPU
// sampler with hand-built view rotations (row 2 of a row-vector rotation =
// the planet-local ray for the center clip texel; convention locked by the
// RotationOrder test). A correct port shows a bright limb band near the
// horizon (viewZenith ~= 115-125 deg at 2 km over an 8.8 km planet).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundHorizonBandScanTest,
	"Hillaire.SkyBackground.HorizonBandScan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundHorizonBandScanTest::RunTest(const FString& Parameters)
{
	const float GroundKm = 8.835f;
	// Volumetric planetary model: T = Ground * PlanetaryAtmosphereThicknessRatio
	// (AtmosphereTop = 1.10x Ground). The old 100 km Earth envelope is gone.
	const float ThickKm = GroundKm * HillaireLimits::PlanetaryAtmosphereThicknessRatio;
	const float ViewHeightKm = GroundKm + 0.002f;
	FHillairePlanetState Planet = HillaireMakeExternalPlanetState(
		0, FGuid(0xBA9D001, 0, 0, 0), FName(TEXT("BandScanPlanet")),
		FVector::ZeroVector, FQuat::Identity,
		GroundKm, ThickKm, 0.2f,
		FHillaireAtmosphereProfile::MakeReferenceProfile());
	const FHillaireAtmosphereProfile& Profile = Planet.Profile;

	// Low sun (2 deg above the horizon) so the bright limb band forms at the
	// geometric horizon dip, which is what this scan asserts. A zenith sun
	// would (correctly) put the peak at the zenith instead.
	const FVector3f SunLocal(0.99939f, 0.0f, 0.03490f);
	const FVector3f CamLocal(0.0f, 0.0f, ViewHeightKm);
	const FVector3f UpLocal(0.0f, 0.0f, 1.0f);

	const int32 TW = HillaireLimits::TransmittanceWidth;
	const int32 TH = HillaireLimits::TransmittanceHeight;
	const int32 MSR = HillaireLimits::MultiScatteringRes;
	const int32 SVW = HillaireLimits::SkyViewWidth;
	const int32 SVH = HillaireLimits::SkyViewHeight;
	TArray<FLinearColor> TransLut, MsLut, SkyLut;
	HillaireLutCpu::BakeTransmittanceLut(Profile, TW, TH, TransLut);
	HillaireLutCpu::BakeFullMultiScatteringLut(Profile, TransLut, TW, TH, MSR, 1.0f, MsLut);
	HillaireLutCpu::BakeFullSkyViewLut(Profile, TransLut, TW, TH, MsLut, MSR,
		ViewHeightKm, SunLocal, UpLocal, SkyLut);

	// (a) LUT row profile.
	float RowMax = 0.0f;
	int32 RowMaxIdx = -1;
	for (int32 Y = 0; Y < SVH; ++Y)
	{
		double Sum = 0.0;
		for (int32 X = 0; X < SVW; ++X)
		{
			const FLinearColor& C = SkyLut[Y * SVW + X];
			Sum += 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B;
		}
		const float Mean = (float)(Sum / (double)SVW);
		if ((Y % 12) == 0 || Y == SVH - 1)
		{
			AddInfo(FString::Printf(TEXT("LUT row y=%d meanL=%.6f"), Y, Mean));
		}
		if (Mean > RowMax) { RowMax = Mean; RowMaxIdx = Y; }
	}
	AddInfo(FString::Printf(TEXT("LUT brightest row y=%d meanL=%.6f"), RowMaxIdx, RowMax));

	// (b) Center-pixel sweep over view zenith (diag-100 InvProj: center clip
	// maps exactly to view +Z, which the hand-built rotation sends to the
	// target planet-local direction).
	FMatrix InvProj(EForceInit::ForceInitToZero);
	InvProj.M[0][0] = 100.0; InvProj.M[1][1] = 100.0; InvProj.M[2][2] = 100.0; InvProj.M[3][3] = 1.0;
	const FLinearColor BgBlack(0.0f, 0.0f, 0.0f, 1.0f);
	const FVector3f SunAtten(1.0f, 1.0f, 1.0f);
	float SweepMax = 0.0f;
	float SweepMaxAng = -1.0f;
	bool bFin = true;
	for (float Deg : { 0.0f, 30.0f, 60.0f, 80.0f, 90.0f, 100.0f, 110.0f, 115.0f, 120.0f, 125.0f, 130.0f, 140.0f, 160.0f, 180.0f })
	{
		const float Th = Deg * PI / 180.0f;
		const FVector3f D(FMath::Sin(Th), 0.0f, FMath::Cos(Th));
		// Orthonormal rows, row 2 = ray dir (row-vector convention).
		FVector3f R0 = FVector3f::CrossProduct(FVector3f(0.0f, 1.0f, 0.0f), D).GetSafeNormal();
		if (R0.SizeSquared() < 0.5f)
		{
			R0 = FVector3f(1.0f, 0.0f, 0.0f);
		}
		const FVector3f R1 = FVector3f::CrossProduct(D, R0).GetSafeNormal();
		FMatrix Rot(EForceInit::ForceInitToZero);
		Rot.M[0][0] = R0.X; Rot.M[0][1] = R0.Y; Rot.M[0][2] = R0.Z;
		Rot.M[1][0] = R1.X; Rot.M[1][1] = R1.Y; Rot.M[1][2] = R1.Z;
		Rot.M[2][0] = D.X;  Rot.M[2][1] = D.Y;  Rot.M[2][2] = D.Z;
		Rot.M[3][3] = 1.0f;

		const FLinearColor Out = HillaireLutCpu::SampleSkyBackgroundPixel(
			BgBlack, 0.0f, 0.5f, 0.5f,
			InvProj, Rot, CamLocal, SunLocal, SunAtten,
			Profile.BottomRadiusKm, Profile.TopRadiusKm, ViewHeightKm,
			SkyLut, SVW, SVH, TransLut, TW, TH, 1.0f);
		if (!FMath::IsFinite(Out.R) || !FMath::IsFinite(Out.G) || !FMath::IsFinite(Out.B))
		{
			bFin = false;
		}
		const float Lum = SkyBgLuminance(Out);
		AddInfo(FString::Printf(TEXT("viewZenith %5.1f deg -> L=%.6f (%.4f,%.4f,%.4f)"),
			Deg, Lum, Out.R, Out.G, Out.B));
		if (Lum > SweepMax) { SweepMax = Lum; SweepMaxAng = Deg; }
	}
	AddInfo(FString::Printf(TEXT("Sweep brightest at %.1f deg L=%.6f"), SweepMaxAng, SweepMax));
	TestTrue(TEXT("Sweep finite"), bFin);
	// Shape invariant (no magic absolute bar): the limb band peaks within
	// 15 deg of the geometric horizon-dip angle for this height.
	{
		const float Vh = FMath::Sqrt(ViewHeightKm * ViewHeightKm - GroundKm * GroundKm);
		const float Beta = FMath::Acos(FMath::Clamp(Vh / ViewHeightKm, -1.0f, 1.0f));
		const float HorizonDipDeg = (PI - Beta) * 180.0f / PI;
		AddInfo(FString::Printf(TEXT("Geometric horizon dip %.1f deg"), HorizonDipDeg));
		TestTrue(TEXT("Peak sits at the horizon dip"),
			FMath::Abs(SweepMaxAng - HorizonDipDeg) < 15.0f);
	}

	// Absolute-scale anchor: the SAME sweep on the unmodified Earth reference
	// profile (6360/6460, +2 km). If Earth peaks bright while the tiny planet
	// peaks two orders lower, the dimness is genuine small-planet geometry
	// (short limb paths), not a systematic port under-scale.
	{
		FHillairePlanetState Earth = HillaireMakeExternalPlanetState(
			1, FGuid(0xEA27001, 0, 0, 0), FName(TEXT("EarthAnchor")),
			FVector::ZeroVector, FQuat::Identity,
			6360.0f, 100.0f, 0.0f,
			FHillaireAtmosphereProfile::MakeReferenceProfile());
		const FHillaireAtmosphereProfile& EP = Earth.Profile;
		const float EH = EP.BottomRadiusKm + 2.0f;
		const FVector3f ECam(0.0f, 0.0f, EH);
		TArray<FLinearColor> ET, EM, ES;
		HillaireLutCpu::BakeTransmittanceLut(EP, TW, TH, ET);
		HillaireLutCpu::BakeFullMultiScatteringLut(EP, ET, TW, TH, MSR, 1.0f, EM);
		HillaireLutCpu::BakeFullSkyViewLut(EP, ET, TW, TH, EM, MSR,
			EH, SunLocal, FVector3f(0.0f, 0.0f, 1.0f), ES);
		float EMax = 0.0f;
		float EMaxAng = -1.0f;
		for (float Deg : { 0.0f, 60.0f, 80.0f, 85.0f, 88.0f, 89.0f, 89.5f, 90.0f, 91.0f, 95.0f, 100.0f, 120.0f, 180.0f })
		{
			const float Th = Deg * PI / 180.0f;
			const FVector3f D(FMath::Sin(Th), 0.0f, FMath::Cos(Th));
			FVector3f R0 = FVector3f::CrossProduct(FVector3f(0.0f, 1.0f, 0.0f), D).GetSafeNormal();
			if (R0.SizeSquared() < 0.5f)
			{
				R0 = FVector3f(1.0f, 0.0f, 0.0f);
			}
			const FVector3f R1 = FVector3f::CrossProduct(D, R0).GetSafeNormal();
			FMatrix Rot(EForceInit::ForceInitToZero);
			Rot.M[0][0] = R0.X; Rot.M[0][1] = R0.Y; Rot.M[0][2] = R0.Z;
			Rot.M[1][0] = R1.X; Rot.M[1][1] = R1.Y; Rot.M[1][2] = R1.Z;
			Rot.M[2][0] = D.X;  Rot.M[2][1] = D.Y;  Rot.M[2][2] = D.Z;
			Rot.M[3][3] = 1.0f;
			const FLinearColor Out = HillaireLutCpu::SampleSkyBackgroundPixel(
				BgBlack, 0.0f, 0.5f, 0.5f,
				InvProj, Rot, ECam, SunLocal, SunAtten,
				EP.BottomRadiusKm, EP.TopRadiusKm, EH,
				ES, SVW, SVH, ET, TW, TH, 1.0f);
			const float Lum = SkyBgLuminance(Out);
			AddInfo(FString::Printf(TEXT("Earth viewZenith %5.1f deg -> L=%.6f"), Deg, Lum));
			if (Lum > EMax) { EMax = Lum; EMaxAng = Deg; }
		}
		AddInfo(FString::Printf(TEXT("Earth sweep brightest at %.1f deg L=%.6f (tiny-planet peak was %.6f)"),
			EMaxAng, EMax, SweepMax));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Test 7 - Real-planet full frame: CPU distribution vs GPU distribution.
//
// Discriminating test for "uniform blue haze" reports: bakes and samples a
// full frame with REAL PIE geometry (link-built 8.835/108.835 km planet,
// horizontal PIE-like sun, spinning planet, rotated perspective view) on BOTH
// the CPU mirror and the production GPU path, then compares structure.
// A flat/uniform output on either side fails here (earlier tests only probed
// single pixels, which a constant field passes).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireSkyBackgroundRealPlanetFrameTest,
	"Hillaire.SkyBackground.RealPlanetFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireSkyBackgroundRealPlanetFrameTest::RunTest(const FString& Parameters)
{
	// ---- GT: link-style planet + PIE-like horizontal sun + spinning planet ----
	const float GroundKm = 8.835f;
	// Volumetric planetary model: T = Ground * PlanetaryAtmosphereThicknessRatio
	// (AtmosphereTop = 1.10x Ground); surface camera 2 m over the ground.
	const float ThickKm = GroundKm * HillaireLimits::PlanetaryAtmosphereThicknessRatio;
	const float CamOverKm = 0.002f;
	const float ViewHeightKm = GroundKm + CamOverKm;
	FHillairePlanetState Planet = HillaireMakeExternalPlanetState(
		0, FGuid(0xA11CE001, 0, 0, 0), FName(TEXT("RealPlanetFrame")),
		FVector::ZeroVector, FQuat::Identity,
		GroundKm, ThickKm, 0.2f,
		FHillaireAtmosphereProfile::MakeReferenceProfile());
	FString PlanetErr;
	if (!TestTrue(TEXT("Link planet valid: ") + PlanetErr, Planet.IsValid(&PlanetErr)))
	{
		return false;
	}
	const FHillaireAtmosphereProfile& Profile = Planet.Profile;

	FHillaireLightSource Sun;
	Sun.LightId = FGuid(0x5EED001, 0, 0, 0);
	Sun.LightName = FName(TEXT("RealSun"));
	Sun.bEnabled = true;
	Sun.bDirectional = true;
	Sun.WorldPositionCm = FVector::ZeroVector;
	Sun.WorldDirectionToLight = FVector(-0.976, 0.216, 0.010).GetSafeNormal();
	Sun.Color = FLinearColor::White;
	Sun.Intensity = 1.0f;

	// Camera 2 km over the +Z pole; rotated view (yaw 45 deg); planet spun 30
	// deg about Z (STARMAP ticks rotation every frame); real reversed-Z
	// perspective projection in km units.
	const FVector OriginCm(0.0, 0.0, (double)ViewHeightKm * 100000.0);
	const FVector3f CenterRel(0.0f, 0.0f, -ViewHeightKm);
	const FQuat PlanetQ(FVector(0.0f, 0.0f, 1.0f), PI / 6.0f);
	const FMatrix SnapView = FRotationMatrix(FRotator(0.0f, 45.0f, 0.0f));
	const FMatrix SnapProj = FReversedZPerspectiveMatrix(PI * 0.25f, 16.0f, 9.0f, 0.001f, 100000.0f);

	TArray<FHillaireLightSource> Lights;
	Lights.Add(Sun);
	const FHillaireCompactedLights Resolved =
		HillaireCompactLightsForPlanet(Lights, OriginCm, CenterRel, PlanetQ);
	if (!TestEqual(TEXT("One resolved light"), Resolved.Count, 1))
	{
		return false;
	}
	const FVector3f SunLocal = Resolved.Lights[0].LightDirLocal;
	const FVector3f SunAtten = Resolved.Lights[0].ColorAttenuation;

	const FHillaireLutManager::FHillaireAerialViewInputs ViewInputs =
		FHillaireLutManager::ComputeAerialViewInputs(CenterRel, PlanetQ, SnapView, SnapProj);
	const FVector3f CamUpLocal = FHillaireLutManager::ComputeCameraUpLocal(CenterRel, PlanetQ);

	// ---- CPU: full LUT chain + 32x18 frame grid ----
	const int32 TW = HillaireLimits::TransmittanceWidth;
	const int32 TH = HillaireLimits::TransmittanceHeight;
	const int32 MSR = HillaireLimits::MultiScatteringRes;
	const int32 SVW = HillaireLimits::SkyViewWidth;
	const int32 SVH = HillaireLimits::SkyViewHeight;
	TArray<FLinearColor> TransLut, MsLut, SkyLut;
	HillaireLutCpu::BakeTransmittanceLut(Profile, TW, TH, TransLut);
	HillaireLutCpu::BakeFullMultiScatteringLut(Profile, TransLut, TW, TH, MSR, 1.0f, MsLut);
	HillaireLutCpu::BakeFullSkyViewLut(Profile, TransLut, TW, TH, MsLut, MSR,
		ViewHeightKm, SunLocal, CamUpLocal, SkyLut);

	AddInfo(TEXT("--- CPU LUT content (real planet) ---"));
	{
		float Mn = 1e30f, Mx = -1e30f;
		double Sum = 0.0;
		int64 Nn = 0;
		for (const FLinearColor& C : SkyLut)
		{
			const float Lum = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
			Mn = FMath::Min(Mn, Lum); Mx = FMath::Max(Mx, Lum); Sum += Lum; ++Nn;
		}
		AddInfo(FString::Printf(TEXT("CPU SkyView lum min=%.6f max=%.6f mean=%.6f ratio=%.2f"),
			Mn, Mx, Sum / (double)Nn, Mx / FMath::Max(Mn, 1e-9f)));
		if (!TestTrue(TEXT("CPU SkyView LUT non-degenerate"), Mx > 1e-6f && Mx / FMath::Max(Mn, 1e-9f) > 2.0f))
		{
			return false;
		}
	}

	const FLinearColor BgBlack(0.0f, 0.0f, 0.0f, 1.0f);
	const int32 GW = 32, GH = 18;
	TArray<FLinearColor> CpuFrame;
	CpuFrame.Reserve(GW * GH);
	float CpuMn = 1e30f, CpuMx = -1e30f;
	bool bCpuFinite = true;
	auto SampleGrid = [&](const FMatrix& InSnapView, const FQuat& InQ, TArray<FLinearColor>& OutFrame,
		float& Mn, float& Mx, bool& Fin)
	{
		const FHillaireLutManager::FHillaireAerialViewInputs In =
			FHillaireLutManager::ComputeAerialViewInputs(CenterRel, InQ, InSnapView, SnapProj);
		OutFrame.Reset();
		OutFrame.Reserve(GW * GH);
		Mn = 1e30f; Mx = -1e30f; Fin = true;
		for (int32 Y = 0; Y < GH; ++Y)
		{
			for (int32 X = 0; X < GW; ++X)
			{
				const float U = ((float)X + 0.5f) / (float)GW;
				const float V = ((float)Y + 0.5f) / (float)GH;
				const FLinearColor Out = HillaireLutCpu::SampleSkyBackgroundPixel(
					BgBlack, 0.0f, U, V,
					In.InvProjMatrix, In.ViewToPlanetLocalRot,
					In.CameraPlanetLocalKm, SunLocal, SunAtten,
					Profile.BottomRadiusKm, Profile.TopRadiusKm, ViewHeightKm,
					SkyLut, SVW, SVH, TransLut, TW, TH, 1.0f);
				if (!FMath::IsFinite(Out.R) || !FMath::IsFinite(Out.G) || !FMath::IsFinite(Out.B))
				{
					Fin = false;
				}
				const float Lum = SkyBgLuminance(Out);
				Mn = FMath::Min(Mn, Lum);
				Mx = FMath::Max(Mx, Lum);
				OutFrame.Add(Out);
			}
		}
	};
	SampleGrid(SnapView, PlanetQ, CpuFrame, CpuMn, CpuMx, bCpuFinite);
	AddInfo(FString::Printf(TEXT("CPU frame yawed lum min=%.6f max=%.6f ratio=%.2f"),
		CpuMn, CpuMx, CpuMx / FMath::Max(CpuMn, 1e-9f)));
	if (!TestTrue(TEXT("CPU frame finite"), bCpuFinite && SkyAllFinite(CpuFrame)))
	{
		return false;
	}
	// Pitched fans (centers 30 deg off-zenith) sweep the upper sky: genuine
	// contrast here is modest (measured 2.5..3.6x; the bright limb band at
	// 115..125 deg stays outside these fans). The >2.0 bar is an
	// anti-collapse guard; band structure is proven by HorizonBandScan.
	for (float PitchDeg : { -30.0f, 30.0f })
	{
		TArray<FLinearColor> PitchedFrame;
		float PMn, PMx;
		bool PFin = false;
		SampleGrid(FRotationMatrix(FRotator(PitchDeg, 45.0f, 0.0f)), PlanetQ,
			PitchedFrame, PMn, PMx, PFin);
		AddInfo(FString::Printf(TEXT("CPU frame pitch %+.0f lum min=%.6f max=%.6f ratio=%.2f"),
			PitchDeg, PMn, PMx, PMx / FMath::Max(PMn, 1e-9f)));
		if (!TestTrue(TEXT("Pitched CPU frame finite"), PFin && SkyAllFinite(PitchedFrame)))
		{
			return false;
		}
		if (!TestTrue(TEXT("Pitched CPU frame non-constant"), PMx / FMath::Max(PMn, 1e-9f) > 2.0f))
		{
			return false;
		}
	}

	// ---- GPU: production LUT gen + full-frame composite + readbacks ----
	// NullRHI/commandlet: no render device, RDG cannot run. The CPU mirror
	// above already validated the sky math; skip the GPU half explicitly
	// instead of crashing the suite.
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("GPU section skipped (no render device / NullRHI)."));
		return true;
	}
	TSharedPtr<FHillaireLutManager> Mgr = MakeShared<FHillaireLutManager>();
	struct FGpuFrameResult
	{
		bool bGraphOk = false;
		TArray<FLinearColor> Out;
		TArray<FLinearColor> GpuTrans, GpuSky;
		TRefCountPtr<IPooledRenderTarget> PooledOut, PooledTrans, PooledSky;
	};
	TSharedPtr<FGpuFrameResult> RtResult = MakeShared<FGpuFrameResult>();

	const int32 FW = 96, FH = 54;
	const FHillaireAtmosphereProfile RtProfile = Profile;
	const FVector3f RtCenter = CenterRel;
	const FHillaireCompactedLights RtLights = Resolved;
	const FHillaireLutManager::FHillaireAerialViewInputs RtInputs = ViewInputs;

	ENQUEUE_RENDER_COMMAND(HillaireRealPlanetFrame)(
		[Mgr, RtResult, RtProfile, ViewHeightKm, RtCenter, PlanetQ, RtLights, RtInputs, FW, FH](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			Mgr->RegisterPlanet(0);
			FHillaireLutManager::FHillairePlanetLutGraphOutputs Outputs;
			const bool bLutsOk = Mgr->EnsurePlanetLuts(
				GraphBuilder, 0, RtProfile, ViewHeightKm,
				RtCenter, PlanetQ, RtLights, 1.0f, true, Outputs);

			FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(
				FIntPoint(FW, FH), PF_FloatRGBA, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef ColorTex = GraphBuilder.CreateTexture(ColorDesc, TEXT("Hillaire.RealFrameColor"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ColorTex), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
			FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(
				FIntPoint(FW, FH), PF_R32_FLOAT, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef DepthTex = GraphBuilder.CreateTexture(DepthDesc, TEXT("Hillaire.RealFrameDepth"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DepthTex), 0.0f);

			FRDGTextureRef OutTex = nullptr;
			// In-domain SkyView sampling height (mirror of EnsurePlanetLuts).
			const float HeightFloorKm = RtProfile.BottomRadiusKm + HillaireLimits::ViewHeightEpsFloorKm;
			const float HeightCeilKm = FMath::Max(
				HeightFloorKm, RtProfile.TopRadiusKm - HillaireLimits::ViewHeightEpsFloorKm);
			const float ClampedH = FMath::Clamp(ViewHeightKm, HeightFloorKm, HeightCeilKm);
			const bool bSkyOk = bLutsOk && Outputs.SkyView != nullptr && Outputs.Transmittance != nullptr
&& Mgr->CompositeSkyBackground(
			GraphBuilder,
			GraphBuilder.CreateSRV(ColorTex),
			DepthTex,
			MakeSkyTestViewUB(1.0f),
			RtInputs.InvProjMatrix,
			RtInputs.CameraPlanetLocalKm,
			RtInputs.ViewToPlanetLocalRot,
			RtLights.Lights[0].LightDirLocal,
			RtLights.Lights[0].ColorAttenuation,
			RtProfile.BottomRadiusKm,
			RtProfile.TopRadiusKm,
			ClampedH,
			RtLights.Lights[0].AngularRadiusRad,
			Outputs.SkyView,
			Outputs.Transmittance,
			FIntRect(0, 0, FW, FH),
			OutTex);
			TRefCountPtr<IPooledRenderTarget> PooledOut;
			if (OutTex)
			{
				GraphBuilder.QueueTextureExtraction(OutTex, &PooledOut);
			}
			GraphBuilder.Execute();
			RtResult->bGraphOk = bSkyOk && OutTex != nullptr && PooledOut.IsValid();
			if (RtResult->bGraphOk)
			{
				RtResult->PooledOut = PooledOut;
				FHillaireLutManager::FHillairePlanetLutTargets Targets;
				if (Mgr->CopyTargets(0, Targets))
				{
					RtResult->PooledTrans = Targets.Transmittance;
					RtResult->PooledSky = Targets.SkyView;
				}
			}
		});
	{
		FRenderCommandFence Fence;
		Fence.BeginFence();
		Fence.Wait();
	}
	if (!TestTrue(TEXT("GPU graph executed"), RtResult->bGraphOk))
	{
		return false;
	}
	// Readbacks must run on the game thread (see ReadbackPooledLut).
	if (!TestTrue(TEXT("GPU frame readback"),
		HillaireLutDiagnostics::ReadbackPooledLut(RtResult->PooledOut, RtResult->Out)))
	{
		return false;
	}
	HillaireLutDiagnostics::ReadbackPooledLut(RtResult->PooledTrans, RtResult->GpuTrans);
	HillaireLutDiagnostics::ReadbackPooledLut(RtResult->PooledSky, RtResult->GpuSky);
	if (!TestEqual(TEXT("GPU frame texel count"), RtResult->Out.Num(), FW * FH))
	{
		return false;
	}

	auto FrameStats = [](const TArray<FLinearColor>& F, float& Mn, float& Mx, bool& Fin)
	{
		Mn = 1e30f; Mx = -1e30f; Fin = true;
		for (const FLinearColor& C : F)
		{
			if (!FMath::IsFinite(C.R) || !FMath::IsFinite(C.G) || !FMath::IsFinite(C.B)) { Fin = false; }
			const float Lum = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
			Mn = FMath::Min(Mn, Lum); Mx = FMath::Max(Mx, Lum);
		}
	};
	float GpuMn, GpuMx;
	bool bGpuFin = false;
	FrameStats(RtResult->Out, GpuMn, GpuMx, bGpuFin);
	AddInfo(FString::Printf(TEXT("GPU frame lum min=%.6f max=%.6f ratio=%.2f"),
		GpuMn, GpuMx, GpuMx / FMath::Max(GpuMn, 1e-9f)));
	TestTrue(TEXT("GPU frame finite"), bGpuFin);
	// Anti-collapse guard (same genuine-physics note as the CPU yawed fan:
	// a zenith-centered fan over this geometry spans lum ~0.0046..0.0084).
	TestTrue(TEXT("GPU frame non-constant"), GpuMx / FMath::Max(GpuMn, 1e-9f) > 1.5f);

	// GPU LUT content (real profile): degenerate bake would explain flat sky.
	{
		float TMn, TMx, SMn, SMx;
		bool TFin = false, SFin = false;
		FrameStats(RtResult->GpuTrans, TMn, TMx, TFin);
		FrameStats(RtResult->GpuSky, SMn, SMx, SFin);
		AddInfo(FString::Printf(TEXT("GPU Transmittance lum min=%.6f max=%.6f"), TMn, TMx));
		AddInfo(FString::Printf(TEXT("GPU SkyView lum min=%.6f max=%.6f ratio=%.2f"), SMn, SMx, SMx / FMath::Max(SMn, 1e-9f)));
		TestTrue(TEXT("GPU LUTs finite"), TFin && SFin);
		TestTrue(TEXT("GPU SkyView non-degenerate"), SMx > 1e-6f && SMx / FMath::Max(SMn, 1e-9f) > 2.0f);
	}

	// Per-pixel GPU-vs-CPU agreement on the identical configuration.
	{
		double Num = 0.0, Den = 0.0;
		int32 Compared = 0;
		for (int32 Y = 0; Y < FH; Y += 3)
		{
			for (int32 X = 0; X < FW; X += 3)
			{
				const float U = ((float)X + 0.5f) / (float)FW;
				const float V = ((float)Y + 0.5f) / (float)FH;
				const FLinearColor Cpu = HillaireLutCpu::SampleSkyBackgroundPixel(
					BgBlack, 0.0f, U, V,
					ViewInputs.InvProjMatrix, ViewInputs.ViewToPlanetLocalRot,
					ViewInputs.CameraPlanetLocalKm, SunLocal, SunAtten,
					Profile.BottomRadiusKm, Profile.TopRadiusKm, ViewHeightKm,
					SkyLut, SVW, SVH, TransLut, TW, TH, 1.0f);
				const FLinearColor& Gpu = RtResult->Out[Y * FW + X];
				Num += FMath::Abs(Gpu.R - Cpu.R) + FMath::Abs(Gpu.G - Cpu.G) + FMath::Abs(Gpu.B - Cpu.B);
				Den += FMath::Abs(Cpu.R) + FMath::Abs(Cpu.G) + FMath::Abs(Cpu.B);
				++Compared;
			}
		}
		const double RelErr = Den > 1e-6 ? Num / Den : 1.0;
		AddInfo(FString::Printf(TEXT("GPU-vs-CPU frame rel err=%.4f over %d texels"), RelErr, Compared));
		TestTrue(TEXT("GPU-vs-CPU frame agreement < 15%"), RelErr < 0.15);
	}
	return true;
}
