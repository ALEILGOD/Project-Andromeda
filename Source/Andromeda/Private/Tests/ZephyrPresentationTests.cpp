// ANDROMEDA - ZEPHYR PRESENTATION TESTS
//
// Locks the ZEPHYR sky-presentation contract (CPU mirror of ZephyrCommon.ush):
//   - DaylightFactor(sunElevCos) is a pure function of REAL sun elevation
//     (dot(sunDir, planetUp), planet-local). No palettes, no screen terms.
//   - Day (sun high) -> 1, horizon -> partial, twilight depth -> 0, night -> 0.
//   - Monotonic non-decreasing over the whole elevation domain.
//   - Presentation HDR budgets are bounded (bloom-headroom argument:
//     clouds + haze can never exceed 1.0 combined, and the shader
//     additionally fades additions as the ATMOS base approaches 1.0, so only
//     the ATMOS sun disk can cross the bloom threshold).
//
// The tested function mirrors HLSL ZephyrDaylightFactor exactly (same
// smoothstep); any divergence breaks sunrise/sunset/night response.

#include "Misc/AutomationTest.h"
#include "Zephyr/ZephyrTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrPresentation_DaylightCurveTest,
	"Andromeda.Zephyr.Presentation.Daylight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrPresentation_DaylightCurveTest::RunTest(const FString& Parameters)
{
	using namespace ZephyrPresentation;

	// Day: sun high -> full presentation.
	TestEqual(TEXT("Noon == 1"), DaylightFactor(1.0f), 1.0f);
	TestEqual(TEXT("FullDay edge == 1"), DaylightFactor(FullDayElevCos), 1.0f);
	TestTrue(TEXT("High sun saturates at 1"), DaylightFactor(0.5f) == 1.0f);

	// Horizon: partial (sunset dimming underway, ATMOS provides warm colors).
	const float AtHorizon = DaylightFactor(0.0f);
	TestTrue(TEXT("Horizon partial (0,1)"), AtHorizon > 0.0f && AtHorizon < 1.0f);

	// Twilight depth: extinct. Night stays extinct (stars + ATMOS dark sky).
	TestEqual(TEXT("TwilightBegin == 0"), DaylightFactor(TwilightBeginElevCos), 0.0f);
	TestEqual(TEXT("Below twilight == 0"), DaylightFactor(-0.5f), 0.0f);
	TestEqual(TEXT("Nadir == 0"), DaylightFactor(-1.0f), 0.0f);

	// Twilight interior: strictly between day and night (smooth, no steps).
	const float MidTwilight = DaylightFactor(0.5f * (TwilightBeginElevCos + FullDayElevCos));
	TestTrue(TEXT("Mid twilight in (0,1)"), MidTwilight > 0.0f && MidTwilight < 1.0f);

	// Monotonic non-decreasing over [-1, 1]: sunrise/sunset sweep smoothly.
	float Prev = DaylightFactor(-1.0f);
	bool bMonotonic = true;
	for (int32 i = 1; i <= 128; ++i)
	{
		const float Elev = FMath::Lerp(-1.0f, 1.0f, (float)i / 128.0f);
		const float F = DaylightFactor(Elev);
		TestTrue(TEXT("Daylight in [0,1]"), F >= 0.0f && F <= 1.0f);
		TestTrue(TEXT("Daylight finite"), FMath::IsFinite(F));
		if (F < Prev - 1e-6f)
		{
			bMonotonic = false;
		}
		Prev = F;
	}
	TestTrue(TEXT("Daylight monotonic non-decreasing"), bMonotonic);

	// Pure function of elevation: same input -> same output (no hidden state).
	TestEqual(TEXT("Deterministic"), DaylightFactor(0.1f), DaylightFactor(0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZephyrPresentation_BoundsTest,
	"Andromeda.Zephyr.Presentation.Bounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FZephyrPresentation_BoundsTest::RunTest(const FString& Parameters)
{
	using namespace ZephyrPresentation;

	// Budgets are positive, sub-bloom individually...
	TestTrue(TEXT("Cloud budget in (0,1)"), MaxCloudAdd > 0.0f && MaxCloudAdd < 1.0f);
	TestTrue(TEXT("Haze budget in (0,1)"), MaxHazeAdd > 0.0f && MaxHazeAdd < 1.0f);
	// ...and combined (worst case both caps hit at once).
	TestTrue(TEXT("Combined budget below bloom threshold"),
		(MaxCloudAdd + MaxHazeAdd) < 1.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
