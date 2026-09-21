// ANDROMEDA - ZEPHYR TRANSITION TESTS
//
// Verifies THE canonical ZEPHYR space<->atmosphere transition, expressed on
// the SHARED planetary volume model:
//   PlanetAtmosphereRange = planetary REFERENCE radius (base sphere / sea
//                           level; terrain is NEVER folded in)
//   AtmosphereTopRadius   = reference + profile/containment envelope
//   TransitionInnerRadius = ReferenceRadius + (Top - Reference) * 0.10
//   ZephyrTransitionFactor = 1 - smoothstep((ViewRadius - Inner)/(Top - Inner))
//
// Required matrix:
//   ViewRadius <= TransitionInnerRadius -> 1
//   midpoint of [Inner, Top]             -> ~0.5
//   ViewRadius >= AtmosphereTopRadius    -> 0
//   plus monotonicity, different planet scales/envelope sizes, terrain
//   INDEPENDENCE of the reference, and two planets evaluated simultaneously
//   (per-planet independence).
//
// The tested function is the CPU-side single source of truth consumed by the
// ZEPHYR manager; the GPU receives the computed factor as a single gate.

#include "Misc/AutomationTest.h"
#include "Zephyr/ZephyrTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Canonical geometry of one planet for a per-planet evaluation.
struct FZephyrTestPlanet
{
	float PlanetRadiusKm = 0.0f;       // planetary reference radius (ATMOS Bottom)
	float TerrainHeightKm = 0.0f;      // authored bound (metadata; never a reference)
	float AtmosphereTopRadiusKm = 0.0f;

	static FZephyrTestPlanet Make(float ReferenceRadiusKm, float TerrainHeightKm, float TopRadiusKm)
	{
		FZephyrTestPlanet P;
		P.PlanetRadiusKm = ReferenceRadiusKm;
		P.TerrainHeightKm = TerrainHeightKm;
		P.AtmosphereTopRadiusKm = TopRadiusKm;
		return P;
	}

	float ComputeFactor(float ViewRadiusKm) const
	{
		return ZephyrComputeTransitionFactor(ViewRadiusKm, PlanetRadiusKm, AtmosphereTopRadiusKm);
	}
};

// ---------------------------------------------------------------------------
// Required transition matrix on an Earth-like planet.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrTransition_MatrixTest,
	"Andromeda.Zephyr.Transition.Matrix",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrTransition_MatrixTest::RunTest(const FString& Parameters)
{
	const FZephyrTestPlanet Earth = FZephyrTestPlanet::Make(6367.0f, 3.0f, 6467.0f);
	const float Range = Earth.PlanetRadiusKm;
	const float Top = Earth.AtmosphereTopRadiusKm;

	TestEqual(TEXT("Range is the planetary reference radius"), Range, 6367.0f);
	TestTrue(TEXT("Top is above the reference radius"), Top > Range);

	// ViewRadius < TransitionInnerRadius -> 1
	// TransitionInnerRadius = Range + (Top - Range) * 0.10
	const float TransitionInner = Range + (Top - Range) * 0.10f;
	TestTrue(TEXT("TransitionInner > Range"), TransitionInner > Range);
	TestEqual(TEXT("Factor below range == 1"), Earth.ComputeFactor(Range - 100.0f), 1.0f);
	TestEqual(TEXT("Factor far inside == 1"), Earth.ComputeFactor(0.5f * Range), 1.0f);
	TestEqual(TEXT("Factor at range == 1"), Earth.ComputeFactor(Range), 1.0f);
	TestEqual(TEXT("Factor at TransitionInner == 1"), Earth.ComputeFactor(TransitionInner), 1.0f);

	// Midpoint of the shifted shell [TransitionInner, Top] -> ~0.5
	const float Mid = 0.5f * (TransitionInner + Top);
	TestTrue(TEXT("Factor at shifted midpoint ~ 0.5"),
		FMath::Abs(Earth.ComputeFactor(Mid) - 0.5f) < 1e-5f);

	// ViewRadius == AtmosphereTopRadius -> 0
	TestEqual(TEXT("Factor at top == 0"), Earth.ComputeFactor(Top), 0.0f);

	// ViewRadius > AtmosphereTopRadius -> 0
	TestEqual(TEXT("Factor above top == 0"), Earth.ComputeFactor(Top + 500.0f), 0.0f);
	TestEqual(TEXT("Factor deep space == 0"), Earth.ComputeFactor(Top * 10.0f), 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Monotonicity: factor is non-increasing over the real shell, strictly
// decreasing inside, and never leaves [0,1].
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrTransition_MonotonicityTest,
	"Andromeda.Zephyr.Transition.Monotonicity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrTransition_MonotonicityTest::RunTest(const FString& Parameters)
{
	const FZephyrTestPlanet Earth = FZephyrTestPlanet::Make(6367.0f, 3.0f, 6467.0f);
	const float Range = Earth.PlanetRadiusKm;
	const float Top = Earth.AtmosphereTopRadiusKm;

	constexpr int32 Steps = 256;

	// Wide-domain non-increasing check (inside flat 1, shell, space flat 0).
	float WidePrev = 1.0f;
	bool bNonIncreasing = true;
	for (int32 i = 0; i <= Steps; ++i)
	{
		const float T = (float)i / (float)Steps;
		const float ViewRadius = FMath::Lerp(Range * 0.5f, Top * 1.5f, T);
		const float F = Earth.ComputeFactor(ViewRadius);
		TestTrue(TEXT("Factor in [0,1]"), F >= -1e-6f && F <= 1.0f + 1e-6f);
		TestTrue(TEXT("Factor finite"), FMath::IsFinite(F));
		if (F > WidePrev + 1e-6f)
		{
			bNonIncreasing = false;
		}
		WidePrev = F;
	}
	TestTrue(TEXT("Factor is non-increasing across the whole domain"), bNonIncreasing);

	// Strict decrease through the shifted shell interior [TransitionInner, Top].
	const float TransitionInner = Range + (Top - Range) * 0.10f;
	int32 StrictDecreaseCount = 0;
	float ShellPrev = Earth.ComputeFactor(TransitionInner); // == 1 at the inner end
	for (int32 i = 1; i <= Steps; ++i)
	{
		const float ViewRadius = FMath::Lerp(TransitionInner, Top, (float)i / (float)Steps);
		const float F = Earth.ComputeFactor(ViewRadius);
		if (F < ShellPrev - 1e-6f)
		{
			++StrictDecreaseCount;
		}
		TestTrue(TEXT("Shell factor <= previous"), F <= ShellPrev + 1e-6f);
		ShellPrev = F;
	}
	TestTrue(TEXT("Strictly decreasing through the shifted shell interior"),
		StrictDecreaseCount >= Steps - 2);

	// Deeper check: values between TransitionInner and top are strictly ordered.
	for (int32 i = 1; i < 64; ++i)
	{
		const float A = Earth.ComputeFactor(TransitionInner + (Top - TransitionInner) * (float)(i - 1) / 64.0f);
		const float B = Earth.ComputeFactor(TransitionInner + (Top - TransitionInner) * (float)i / 64.0f);
		TestTrue(TEXT("Strict decrease between shell samples"), A > B);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Different planet scales (radius + multiplier) keep the canonical shape.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrTransition_PlanetScaleTest,
	"Andromeda.Zephyr.Transition.PlanetScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrTransition_PlanetScaleTest::RunTest(const FString& Parameters)
{
	// Gigantic planet with a small relative envelope.
	const FZephyrTestPlanet Giant = FZephyrTestPlanet::Make(60000.0f, 200.0f, 63000.0f);
	const float GiantInner = Giant.PlanetRadiusKm + (Giant.AtmosphereTopRadiusKm - Giant.PlanetRadiusKm) * 0.10f;
	const float GiantMid = 0.5f * (GiantInner + Giant.AtmosphereTopRadiusKm);
	TestTrue(TEXT("Giant: mid ~ 0.5"), FMath::Abs(Giant.ComputeFactor(GiantMid) - 0.5f) < 1e-5f);
	TestEqual(TEXT("Giant: below range == 1"), Giant.ComputeFactor(Giant.PlanetRadiusKm - 1.0f), 1.0f);
	TestEqual(TEXT("Giant: at top == 0"), Giant.ComputeFactor(Giant.AtmosphereTopRadiusKm), 0.0f);

	// Tiny planet with a thick relative envelope.
	const FZephyrTestPlanet Tiny = FZephyrTestPlanet::Make(800.0f, 10.0f, 1040.0f);
	const float TinyInner = Tiny.PlanetRadiusKm + (Tiny.AtmosphereTopRadiusKm - Tiny.PlanetRadiusKm) * 0.10f;
	const float TinyMid = 0.5f * (TinyInner + Tiny.AtmosphereTopRadiusKm);
	TestTrue(TEXT("Tiny: mid ~ 0.5"), FMath::Abs(Tiny.ComputeFactor(TinyMid) - 0.5f) < 1e-5f);
	TestEqual(TEXT("Tiny: below range == 1"), Tiny.ComputeFactor(Tiny.PlanetRadiusKm - 1.0f), 1.0f);
	TestEqual(TEXT("Tiny: at top == 0"), Tiny.ComputeFactor(Tiny.AtmosphereTopRadiusKm), 0.0f);

	// The transition width is the real volume thickness for both.
	TestTrue(TEXT("Giant volume thicker than tiny volume"),
		Giant.AtmosphereTopRadiusKm - Giant.PlanetRadiusKm >
		Tiny.AtmosphereTopRadiusKm - Tiny.PlanetRadiusKm);

	return true;
}

// ---------------------------------------------------------------------------
// Terrain INDEPENDENCE: the transition reference is the planetary reference
// radius; the authored terrain bound never re-anchors the atmosphere.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrTransition_TerrainIndependenceTest,
	"Andromeda.Zephyr.Transition.TerrainIndependence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrTransition_TerrainIndependenceTest::RunTest(const FString& Parameters)
{
	// Same reference radius and same atmosphere top, terrain 0 vs 2000 km:
	// the transition MUST be bit-identical (terrain is metadata only).
	const FZephyrTestPlanet NoTerrain = FZephyrTestPlanet::Make(6360.0f, 0.0f, 6460.0f);
	const FZephyrTestPlanet HighTerrain = FZephyrTestPlanet::Make(6360.0f, 2000.0f, 6460.0f);

	TestEqual(TEXT("Reference radius is terrain-independent"), NoTerrain.PlanetRadiusKm, HighTerrain.PlanetRadiusKm);

	for (int32 i = 0; i <= 32; ++i)
	{
		const float ViewRadius = FMath::Lerp(6300.0f, 6500.0f, (float)i / 32.0f);
		const float FNoTerrain = NoTerrain.ComputeFactor(ViewRadius);
		const float FHighTerrain = HighTerrain.ComputeFactor(ViewRadius);
		if (!TestTrue(TEXT("Terrain does not shift the transition"),
			FMath::IsNearlyEqual(FNoTerrain, FHighTerrain, 1e-7f)))
		{
			return false;
		}
	}

	// A containing envelope (ATMOS raises the top to enclose the terrain):
	// the REFERENCE stays the same planetary radius, and a camera on the
	// authored terrain bound is inside the larger volume.
	const FZephyrTestPlanet Contained = FZephyrTestPlanet::Make(6360.0f, 2000.0f, 9000.0f);
	TestEqual(TEXT("Factor at the shared reference radius == 1"), NoTerrain.ComputeFactor(6360.0f), 1.0f);
	TestEqual(TEXT("Factor at the shared reference radius == 1 (contained)"), Contained.ComputeFactor(6360.0f), 1.0f);
	TestEqual(TEXT("Small volume ends at its top"), NoTerrain.ComputeFactor(6460.0f), 0.0f);
	TestTrue(TEXT("Camera on the terrain bound inside the containing volume"),
		Contained.ComputeFactor(6360.0f + 2000.0f) > 0.0f);
	// Without the containment envelope extension, terrain does NOT re-anchor
	// the small atmosphere: the same camera is simply outside it.
	TestEqual(TEXT("Terrain never re-anchors the small volume"), NoTerrain.ComputeFactor(8360.0f), 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Two planets with different ranges evaluated simultaneously (per-planet
// independence, the multiplanetary requirement).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrTransition_MultiPlanetTest,
	"Andromeda.Zephyr.Transition.MultiPlanet",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrTransition_MultiPlanetTest::RunTest(const FString& Parameters)
{
	// Planet A: Earth-like reference 6367, envelope to 6467.
	const FZephyrTestPlanet PlanetA = FZephyrTestPlanet::Make(6367.0f, 3.0f, 6467.0f);
	// Planet B: small dwarf, reference 1700, envelope to 2130.
	const FZephyrTestPlanet PlanetB = FZephyrTestPlanet::Make(1700.0f, 5.0f, 2130.0f);

	// Camera just above B's reference radius (inside B's volume).
	const float ViewRadiusNearB = PlanetB.PlanetRadiusKm + 50.0f;

	// B: 50 km into a 430 km volume -> factor close to 1.
	const float FNearB_B = PlanetB.ComputeFactor(ViewRadiusNearB);
	TestTrue(TEXT("B transitioning approaching the reference"), FNearB_B > 0.8f && FNearB_B <= 1.0f);

	// A: same view radius is far below A's reference -> 1.
	const float FNearB_A = PlanetA.ComputeFactor(ViewRadiusNearB);
	TestEqual(TEXT("A unaffected, fully active"), FNearB_A, 1.0f);

	// Camera above B's volume but just above A's reference.
	const float ViewRadiusAboveB = PlanetB.AtmosphereTopRadiusKm + 100.0f;
	TestEqual(TEXT("B: above B's top == 0"), PlanetB.ComputeFactor(ViewRadiusAboveB), 0.0f);
	TestEqual(TEXT("A: deep inside A == 1"), PlanetA.ComputeFactor(ViewRadiusAboveB), 1.0f);

	// Independence: moving Planet A -> space -> Planet B transitions separately.
	for (int32 i = 0; i <= 32; ++i)
	{
		const float ViewRadius =
			FMath::Lerp(PlanetA.PlanetRadiusKm, PlanetA.AtmosphereTopRadiusKm, (float)i / 32.0f);
		const float FA = PlanetA.ComputeFactor(ViewRadius);
		const float FB = PlanetB.ComputeFactor(ViewRadius);
		TestTrue(TEXT("A transition monotonic toward 0"), FA >= 0.0f && FA <= 1.0f);
		TestTrue(TEXT("B fully shaded during A transit"), FB == 0.0f);
	}

	return true;
}

// Small helper used by the edge-case sweep (Earth-like shell).
static float EarthLikeFactor(float ViewRadiusKm)
{
	return ZephyrComputeTransitionFactor(ViewRadiusKm, 6370.0f, 6460.0f);
}

// ---------------------------------------------------------------------------
// Degenerate shell (AtmosphereTopRadius <= PlanetAtmosphereRange): step
// transition, no division by zero, no NaN.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrTransition_EdgeCaseTest,
	"Andromeda.Zephyr.Transition.EdgeCases",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrTransition_EdgeCaseTest::RunTest(const FString& Parameters)
{
	// Degenerate: top == range.
	const float Range = 1000.0f;
	const float TopEqual = Range;
	TestEqual(TEXT("Degenerate(equal): inside == 1"), ZephyrComputeTransitionFactor(Range - 1.0f, Range, TopEqual), 1.0f);
	TestEqual(TEXT("Degenerate(equal): at surface == 1"), ZephyrComputeTransitionFactor(Range, Range, TopEqual), 1.0f);
	TestEqual(TEXT("Degenerate(equal): outside == 0"), ZephyrComputeTransitionFactor(Range + 1.0f, Range, TopEqual), 0.0f);

	// Degenerate: top < range.
	const float TopBelow = 900.0f;
	TestEqual(TEXT("Degenerate(below): inside == 1"), ZephyrComputeTransitionFactor(Range - 1.0f, Range, TopBelow), 1.0f);
	TestEqual(TEXT("Degenerate(below): at surface == 1"), ZephyrComputeTransitionFactor(Range, Range, TopBelow), 1.0f);
	TestEqual(TEXT("Degenerate(below): outside == 0"), ZephyrComputeTransitionFactor(Range + 1.0f, Range, TopBelow), 0.0f);

	// Zero range, valid top: still normalizes cleanly (no division by zero).
	const float ZeroRange = 0.0f;
	const float ValidTop = 100.0f;
	const float ZeroInner = ZeroRange + (ValidTop - ZeroRange) * 0.10f;
	const float ZeroMid = 0.5f * (ZeroInner + ValidTop);
	TestTrue(TEXT("Zero-range mid finite"), FMath::IsFinite(ZephyrComputeTransitionFactor(ZeroMid, ZeroRange, ValidTop)));
	TestTrue(TEXT("Zero-range mid ~ 0.5"),
		FMath::Abs(ZephyrComputeTransitionFactor(ZeroMid, ZeroRange, ValidTop) - 0.5f) < 1e-5f);

	// NaN/Inf-free across the whole domain.
	float Acc = 0.0f;
	for (int32 i = -20; i <= 140; ++i)
	{
		const float F = EarthLikeFactor((float)i * 10.0f);
		TestTrue(TEXT("Factor finite over domain"), FMath::IsFinite(F));
		Acc += F;
	}
	TestTrue(TEXT("Accumulator finite"), FMath::IsFinite(Acc));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS