// HILLAIRE ATMOSPHERE - SKY BACKGROUND AUTOMATION TESTS.
//
// Production sky path: the BeforeDOF hook samples the existing Phase-2B
// SkyView LUT for the current camera/view (governing planet, slot-0 primary
// sun, unit-white transfer x ColorAttenuation x PreExposure, composited as
// sky + T * background per the reference FASTSKY branch). Small targeted
// tests for the NEW render hook/sampling only:
//   1. gating truth table (pure, no GPU);
//   2. CPU-mirror response (height / sun / pixel dependence, opaque identity);
//   3. composite equation (background attenuated by view transmittance);
//   4. view-input rotation order with a spinning (non-identity) planet;
//   5. end-to-end sky with real perspective matrices (nadir vs zenith);
//   6. GPU execution proof (production EnsurePlanetLuts + CompositeSkyBackground).
// Phase 2A-2F suites must keep passing untouched: the aerial path, LUT
// generation math and the CPU aerial mirror are not modified here.

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

	float SkyLuminance(const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; }

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
	TestTrue(TEXT("Sky is lit (positive)"), SkyLuminance(Zenith) > 1e-6f);

	// Pixel dependence: different view rays sample different sky.
	const FLinearColor Corner = Noon.RunSky(BgIn, 0.0f, 0.05f, 0.05f);
	TestTrue(TEXT("Pixel-dependent sky"), Corner != Zenith);

	// Camera-height dependence: same ray, LUT baked at 1 km vs 50 km.
	FSkyFixture High;
	High.Bake(FVector3f(0.0f, 0.0f, 1.0f), 50.0f);
	const FLinearColor ZenithHigh = High.RunSky(BgIn, 0.0f, 0.5f, 0.5f);
	TestTrue(TEXT("Height changes sky"), ZenithHigh != Zenith);
	AddInfo(FString::Printf(TEXT("L surface=%.6f high=%.6f corner=%.6f"),
		SkyLuminance(Zenith), SkyLuminance(ZenithHigh), SkyLuminance(Corner)));

	// Sun dependence: same observer, LUT baked under a 2-deg-elevation sun.
	FSkyFixture Sunset;
	{
		const float El = FMath::Cos(88.0f * PI / 180.0f);
		Sunset.Bake(FVector3f(FMath::Sqrt(1.0f - El * El), 0.0f, El), 1.0f);
	}
	const FLinearColor ZenithSunset = Sunset.RunSky(BgIn, 0.0f, 0.5f, 0.5f);
	TestTrue(TEXT("Sun changes sky"), ZenithSunset != Zenith);
	AddInfo(FString::Printf(TEXT("L noon=%.6f sunset=%.6f"),
		SkyLuminance(Zenith), SkyLuminance(ZenithSunset)));
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
			V, TR, TG, TB, SkyLuminance(OutBlack)));
		if (TR < -1e-4f || TR > 1.0f + 1e-4f
			|| TG < -1e-4f || TG > 1.0f + 1e-4f
			|| TB < -1e-4f || TB > 1.0f + 1e-4f)
		{
			bOk = false;
			break;
		}
		// Sky term present on black (the LUT actually feeds the output).
		if (!(SkyLuminance(OutBlack) > 1e-6f)) { bOk = false; break; }
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
	AddInfo(FString::Printf(TEXT("L nadir=%.6f zenith=%.6f"), SkyLuminance(Nadir), SkyLuminance(Zenith)));
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
	const float ThickKm = 100.0f;
	const float ViewHeightKm = GroundKm + 2.0f;
	FHillairePlanetState Planet = HillaireMakeExternalPlanetState(
		0, FGuid(0xBA9D001, 0, 0, 0), FName(TEXT("BandScanPlanet")),
		FVector::ZeroVector, FQuat::Identity,
		GroundKm, ThickKm, 0.2f,
		FHillaireAtmosphereProfile::MakeReferenceProfile());
	const FHillaireAtmosphereProfile& Profile = Planet.Profile;

	const FVector3f SunLocal(0.0f, 0.0f, 1.0f);
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
		const float Lum = SkyLuminance(Out);
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
			const float Lum = SkyLuminance(Out);
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
	const float ThickKm = 100.0f;
	const float CamOverKm = 2.0f;
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
				const float Lum = SkyLuminance(Out);
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
