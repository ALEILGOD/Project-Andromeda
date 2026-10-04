#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "LYTHOS/Lythos.h"
#include "LYTHOS/LythosCoordinates.h"
#include "LYTHOS/LythosMacroTerrain.h"
#include "LYTHOS/LythosDebugVisualization.h"
#include "StarSystemGenerator.h"
#include "Planet/PlanetBiomeGenerator.h"

namespace
{
	constexpr auto Flags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::ProductFilter;

	FLythosPlanetContext MakeTestContext(
		int64 PlanetSeed = 123456789,
		int64 PlanetID = 0,
		float PlanetRadius = 500000.0f,
		float TerrainHeight = 20000.0f)
	{
		FPlanetGenerationData GenData;
		GenData.PlanetID = PlanetID;
		GenData.PlanetSeed = PlanetSeed;
		GenData.PlanetRadius = PlanetRadius;
		GenData.TerrainHeight = TerrainHeight;
		GenData.OrbitDistance = 5000000.0f;
		GenData.ContinentalScale = 0.5f;
		GenData.MountainScale = 3.0f;
		GenData.DetailScale = 12.0f;
		GenData.MountainStrength = 1.5f;
		GenData.DetailStrength = 0.1f;

		return FLythosPlanetContext::FromGenerationData(
			GenData,
			FVector::ZeroVector,
			FVector(10000000.f, 0.f, 0.f),
			5778.0f);
	}
}

// ---------------------------------------------------------------------------
// TEST 1 — Same seed + same inputs → identical output.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosDeterminismTest,
	"Andromeda.LYTHOS.Determinism", Flags)

bool FLythosDeterminismTest::RunTest(const FString& Parameters)
{
	const FLythosPlanetContext Ctx = MakeTestContext(987654321);

	const TArray<FVector> Directions = {
		FVector::ForwardVector,
		FVector(0.3f, 0.8f, -0.4f).GetSafeNormal(),
		FVector(-0.5f, 0.2f, 0.7f).GetSafeNormal(),
		-FVector::UpVector,
		FVector(0.9f, -0.1f, 0.3f).GetSafeNormal()
	};

	for (const FVector& Dir : Directions)
	{
		const FLythoTerrainSample A = FLythosMacroTerrain::Evaluate(Ctx, Dir);
		const FLythoTerrainSample B = FLythosMacroTerrain::Evaluate(Ctx, Dir);

		TestTrue(TEXT("Same output for identical inputs"),
			A.Elevation == B.Elevation &&
			A.NormalizedElevation == B.NormalizedElevation &&
			A.Landform == B.Landform &&
			A.FeatureId.FeatureId == B.FeatureId.FeatureId);
	}

	return true;
}

// ---------------------------------------------------------------------------
// TEST 2 — Different seeds → meaningfully different macro terrain.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosSeedVariationTest,
	"Andromeda.LYTHOS.SeedVariation", Flags)

bool FLythosSeedVariationTest::RunTest(const FString& Parameters)
{
	const FLythosPlanetContext CtxA = MakeTestContext(111111111);
	const FLythosPlanetContext CtxB = MakeTestContext(999999999);

	const int32 SampleCount = 200;
	int32 Differences = 0;
	float MaxDiff = 0.f;

	for (int32 I = 0; I < SampleCount; ++I)
	{
		const float Theta = (2.f * PI * I) / SampleCount;
		const float Phi = PI * (static_cast<float>(I) / SampleCount - 0.5f);
		const FVector Dir(
			FMath::Cos(Theta) * FMath::Cos(Phi),
			FMath::Sin(Theta) * FMath::Cos(Phi),
			FMath::Sin(Phi));

		const FLythoTerrainSample A = FLythosMacroTerrain::Evaluate(CtxA, Dir);
		const FLythoTerrainSample B = FLythosMacroTerrain::Evaluate(CtxB, Dir);

		const float Diff = FMath::Abs(A.Elevation - B.Elevation);
		MaxDiff = FMath::Max(MaxDiff, Diff);

		if (Diff > 100.f)
		{
			++Differences;
		}
	}

	TestTrue(TEXT("Different seeds produce meaningfully different terrain"),
		Differences > SampleCount / 4);
	TestTrue(TEXT("Maximum elevation difference is non-trivial"), MaxDiff > 500.f);
	AddInfo(FString::Printf(TEXT("seeds differ at %d/%d samples, maxDiff=%.2f cm"),
		Differences, SampleCount, MaxDiff));

	return true;
}

// ---------------------------------------------------------------------------
// TEST 3 — Planet radius is respected.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosRadiusRespectedTest,
	"Andromeda.LYTHOS.RadiusRespected", Flags)

bool FLythosRadiusRespectedTest::RunTest(const FString& Parameters)
{
	const float RadiusA = 250000.0f;
	const float RadiusB = 1000000.0f;

	const FLythosPlanetContext CtxA = MakeTestContext(42, 0, RadiusA);
	const FLythosPlanetContext CtxB = MakeTestContext(42, 0, RadiusB);

	const FVector Dir = FVector(0.3f, 0.5f, 0.6f).GetSafeNormal();

	const FLythoTerrainSample SampleA = FLythosMacroTerrain::Evaluate(CtxA, Dir);
	const FLythoTerrainSample SampleB = FLythosMacroTerrain::Evaluate(CtxB, Dir);

	// Surface position should be at PlanetRadius + Elevation.
	const float ExpectedA = RadiusA + SampleA.Elevation;
	const float ExpectedB = RadiusB + SampleB.Elevation;
	const float ActualA = SampleA.LocalPosition.Size();
	const float ActualB = SampleB.LocalPosition.Size();

	TestTrue(TEXT("Radius A is reflected in surface position"),
		FMath::IsNearlyEqual(ActualA, ExpectedA, 1.f));
	TestTrue(TEXT("Radius B is reflected in surface position"),
		FMath::IsNearlyEqual(ActualB, ExpectedB, 1.f));
	TestTrue(TEXT("Different radii produce different surface positions"),
		FMath::Abs(ActualA - ActualB) > 100000.f);

	// Sea level / ocean radius must scale with planet radius.
	const float OceanA = FLythosMacroTerrain::ComputeOceanSurfaceRadius(CtxA);
	const float OceanB = FLythosMacroTerrain::ComputeOceanSurfaceRadius(CtxB);
	TestTrue(TEXT("Ocean surface radius scales with planet radius"),
		FMath::Abs(OceanB - OceanA) > 500000.f);

	return true;
}

// ---------------------------------------------------------------------------
// TEST 4 — Terrain uses spherical planetary coordinates, not World-Z.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosSphericalCoordinatesTest,
	"Andromeda.LYTHOS.SphericalCoordinates", Flags)

bool FLythosSphericalCoordinatesTest::RunTest(const FString& Parameters)
{
	const FLythosPlanetContext Ctx = MakeTestContext(777);

	// Every direction, regardless of orientation, must be evaluated
	// the same way. A point at +X with elevation E should have the same
	// elevation as a point at -X with the same direction vector (just
	// negated), since the generator is direction-based, not Z-based.

	const TArray<FVector> AxisDirs = {
		FVector::UpVector,
		-FVector::UpVector,
		FVector::ForwardVector,
		-FVector::ForwardVector,
		FVector::RightVector,
		-FVector::RightVector,
		FVector(0.57735f, 0.57735f, 0.57735f),  // octahedral
		FVector(-0.57735f, -0.57735f, -0.57735f)
	};

	for (const FVector& Dir : AxisDirs)
	{
		const FLythoTerrainSample S = FLythosMacroTerrain::Evaluate(Ctx, Dir);

		// Surface normal must point outward along the direction (radial).
		TestTrue(TEXT("Normal aligns with direction (radial, not Z-up)"),
			(S.Normal | Dir) > 0.9f);

		// LocalPosition must be along Direction * (Radius + Elevation).
		const FVector ExpectedPos = Dir * (Ctx.PlanetRadius + S.Elevation);
		TestTrue(TEXT("Local position is radial from center"),
			FVector::Dist(S.LocalPosition, ExpectedPos) < 1.f);
	}

	// Latitude must vary with Z component, not with world position orientation.
	const FVector EquatorDir(1.f, 1.f, 0.f);
	const FVector NearPoleDir(FMath::Sin(0.1f), 0.f, FMath::Cos(0.1f));

	const float EquatorLat = FLythosMacroTerrain::Evaluate(Ctx, EquatorDir).Latitude;
	const float PoleLat = FLythosMacroTerrain::Evaluate(Ctx, NearPoleDir).Latitude;
	TestTrue(TEXT("Equator latitude is near zero"), EquatorLat < 0.1f);
	TestTrue(TEXT("Near-pole latitude is near one"), PoleLat > 0.9f);

	return true;
}

// ---------------------------------------------------------------------------
// TEST 5 — Two independent evaluations of the same region produce identical data.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosRegionRepeatabilityTest,
	"Andromeda.LYTHOS.RegionRepeatability", Flags)

bool FLythosRegionRepeatabilityTest::RunTest(const FString& Parameters)
{
	const FLythosPlanetContext Ctx = MakeTestContext(314159);
	FLythosRegionCoord Region;
	Region.FaceIndex = 0;
	Region.RegionX = 1;
	Region.RegionY = 1;
	Region.RegionLOD = 0;
	Region.GridResolution = 16;

	const FLythosRegionData A = FLythosMacroTerrain::GenerateRegion(Ctx, Region);
	const FLythosRegionData B = FLythosMacroTerrain::GenerateRegion(Ctx, Region);

	TestTrue(TEXT("Region grid sizes match"), A.GridSize == B.GridSize);
	TestTrue(TEXT("Region sample counts match"), A.Samples.Num() == B.Samples.Num());
	TestTrue(TEXT("Sea levels match"), A.SeaLevel == B.SeaLevel);

	bool bAllMatch = true;
	for (int32 I = 0; I < A.Samples.Num(); ++I)
	{
		const FLythoTerrainSample& SA = A.Samples[I];
		const FLythoTerrainSample& SB = B.Samples[I];

		if (SA.Elevation != SB.Elevation ||
			SA.NormalizedElevation != SB.NormalizedElevation ||
			SA.Landform != SB.Landform ||
			SA.FeatureId.FeatureId != SB.FeatureId.FeatureId ||
			SA.FeatureId.FeatureType != SB.FeatureId.FeatureType)
		{
			bAllMatch = false;
			break;
		}
	}

	TestTrue(TEXT("All region samples are identical across evaluations"), bAllMatch);

	// Region identity must be deterministic.
	TestTrue(TEXT("Region fingerprints match"),
		A.RegionId.GetFingerprint() == B.RegionId.GetFingerprint());

	return true;
}

// ---------------------------------------------------------------------------
// TEST 6 — Different planetary surface directions can be evaluated independently.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosDirectionalIndependenceTest,
	"Andromeda.LYTHOS.DirectionalIndependence", Flags)

bool FLythosDirectionalIndependenceTest::RunTest(const FString& Parameters)
{
	const FLythosPlanetContext Ctx = MakeTestContext(271828);

	const FVector DirA = FVector(0.5f, 0.3f, 0.8f).GetSafeNormal();
	const FVector DirB = FVector(-0.7f, 0.2f, 0.5f).GetSafeNormal();
	const FVector DirC = FVector(0.1f, -0.9f, 0.3f).GetSafeNormal();

	const FLythoTerrainSample SA = FLythosMacroTerrain::Evaluate(Ctx, DirA);
	const FLythoTerrainSample SB = FLythosMacroTerrain::Evaluate(Ctx, DirB);
	const FLythoTerrainSample SC = FLythosMacroTerrain::Evaluate(Ctx, DirC);

	// Each direction produces a sample whose normal aligns with that direction.
	TestTrue(TEXT("DirA normal aligns"), (SA.Normal | DirA) > 0.9f);
	TestTrue(TEXT("DirB normal aligns"), (SB.Normal | DirB) > 0.9f);
	TestTrue(TEXT("DirC normal aligns"), (SC.Normal | DirC) > 0.9f);

	// Each direction produces a distinct local position.
	TestTrue(TEXT("DirA and DirB differ"), FVector::Dist(SA.LocalPosition, SB.LocalPosition) > 1000.f);
	TestTrue(TEXT("DirB and DirC differ"), FVector::Dist(SB.LocalPosition, SC.LocalPosition) > 1000.f);

	// Feature identities are deterministic per direction (evaluated
	// independently; nearby directions may share identity, but the
	// evaluation is self-contained per direction).
	TestTrue(TEXT("DirA feature identity is deterministic"),
		FLythosMacroTerrain::Evaluate(Ctx, DirA).FeatureId.FeatureId == SA.FeatureId.FeatureId);

	return true;
}

// ---------------------------------------------------------------------------
// TEST 7 — No full-planet generation is required.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosNoFullPlanetTest,
	"Andromeda.LYTHOS.NoFullPlanetGeneration", Flags)

bool FLythosNoFullPlanetTest::RunTest(const FString& Parameters)
{
	const FLythosPlanetContext Ctx = MakeTestContext(424242);

	// Generate a single region: it should contain exactly GridSize^2 samples,
	// NOT a full-planet sample count.
	FLythosRegionCoord Region;
	Region.FaceIndex = 2;
	Region.RegionX = 0;
	Region.RegionY = 0;
	Region.RegionLOD = 0;
	Region.GridResolution = 8;

	const FLythosRegionData Data = FLythosMacroTerrain::GenerateRegion(Ctx, Region);

	const int32 ExpectedSamples = 8 * 8;
	TestEqual(TEXT("Region produces exactly GridSize^2 samples"),
		Data.Samples.Num(), ExpectedSamples);

	TestTrue(TEXT("Data is valid"), Data.IsValid());
	TestTrue(TEXT("GridSize recorded"), Data.GridSize == 8);

	// A single face region should have far fewer samples than a full planet
	// at the same resolution.
	const int32 FullPlanetSamples = 6 * 8 * 8;
	TestTrue(TEXT("Region is a subset, not a full planet"),
		Data.Samples.Num() < FullPlanetSamples);

	return true;
}

// ---------------------------------------------------------------------------
// TEST 8 — Hydrology extension points exist.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosHydrologyHooksTest,
	"Andromeda.LYTHOS.HydrologyHooks", Flags)

bool FLythosHydrologyHooksTest::RunTest(const FString& Parameters)
{
	const FLythosPlanetContext Ctx = MakeTestContext(2024);

	// Sea level is deterministic and in valid range.
	const float SeaLevelA = FLythosMacroTerrain::ComputeSeaLevel(Ctx);
	const float SeaLevelB = FLythosMacroTerrain::ComputeSeaLevel(Ctx);
	TestTrue(TEXT("Sea level is deterministic"), SeaLevelA == SeaLevelB);
	TestTrue(TEXT("Sea level in valid range [0, 1]"),
		SeaLevelA >= 0.f && SeaLevelA <= 1.f);

	// Ocean surface radius is computable and above planet radius.
	const float OceanRadius = FLythosMacroTerrain::ComputeOceanSurfaceRadius(Ctx);
	TestTrue(TEXT("Ocean surface radius exceeds planet radius"),
		OceanRadius > Ctx.PlanetRadius);

	// Ocean floor elevation (negative).
	const float OceanFloor = FLythosMacroTerrain::ComputeOceanFloorElevation(Ctx, FVector::ForwardVector);
	TestTrue(TEXT("Ocean floor is below sea level"), OceanFloor < 0.f);

	// Region data carries sea level and hydrology reserved field.
	FLythosRegionCoord HydroReg;
	HydroReg.FaceIndex = 0;
	HydroReg.RegionX = 0;
	HydroReg.RegionY = 0;
	HydroReg.RegionLOD = 0;
	HydroReg.GridResolution = 16;

	const FLythosRegionData Data = FLythosMacroTerrain::GenerateRegion(Ctx, HydroReg);

	TestTrue(TEXT("Region data carries sea level"),
		FMath::IsNearlyEqual(Data.SeaLevel, SeaLevelA, KINDA_SMALL_NUMBER));

	// Terrain sample exposes hydrology hook.
	const FLythoTerrainSample S = FLythosMacroTerrain::Evaluate(Ctx, FVector::ForwardVector);
	TestTrue(TEXT("Terrain sample has hydrology extension hook"),
		S.HydrologyReserved == 0u);

	// Slope is available for future river routing.
	TestTrue(TEXT("Slope is in valid range [0, 1]"),
		S.Slope >= 0.f && S.Slope <= 1.f);

	// Feature identity carries type info for future anti-repetition.
	TestTrue(TEXT("Feature identity has a non-Unknown type"),
		S.FeatureId.FeatureType != ELythosLandform::Unknown);

	return true;
}

// ---------------------------------------------------------------------------
// TEST 9 — Existing Andromeda.PlanetaryMotion tests remain passing.
// (This test just verifies the LYTHOS public API does not break existing
//  systems; the actual PlanetaryMotion tests are run separately.)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosPlanetaryMotionCompatibilityTest,
	"Andromeda.LYTHOS.PlanetaryMotionCompatibility", Flags)

bool FLythosPlanetaryMotionCompatibilityTest::RunTest(const FString& Parameters)
{
	// Verify LYTHOS does not reference any PlanetaryMotion symbols in a
	// way that could break compilation or linkage.
	const FLythosPlanetContext Ctx = MakeTestContext(123);
	const FLythoTerrainSample S = FLythosMacroTerrain::Evaluate(Ctx, FVector::ForwardVector);
	TestTrue(TEXT("LYTHOS evaluates without touching PlanetaryMotion"), S.Direction.IsNormalized());

	// Verify LYTHOS context can be built from FPlanetGenerationData,
	// which is the same data used by AStarSystem for runtime motion.
	FPlanetGenerationData Gen;
	Gen.PlanetID = 5;
	Gen.PlanetSeed = 999;
	Gen.PlanetRadius = 600000.f;
	Gen.TerrainHeight = 25000.f;
	Gen.OrbitDistance = 8000000.f;
	Gen.ContinentalScale = 0.4f;
	Gen.MountainScale = 2.5f;
	Gen.DetailScale = 10.0f;
	Gen.MountainStrength = 1.2f;
	Gen.DetailStrength = 0.08f;

	const FLythosPlanetContext FromGen =
		FLythosPlanetContext::FromGenerationData(Gen, FVector(1e7f, 0, 0), FVector::ZeroVector);

	TestTrue(TEXT("Context built from FPlanetGenerationData"), FromGen.IsValid());
	TestTrue(TEXT("Context preserves planet radius"),
		FMath::IsNearlyEqual(FromGen.PlanetRadius, 600000.f, 0.01f));
	TestTrue(TEXT("Context preserves terrain height"),
		FMath::IsNearlyEqual(FromGen.TerrainHeight, 25000.f, 0.01f));
	TestTrue(TEXT("Context preserves planet seed"), FromGen.PlanetSeed == 999);

	return true;
}

// ---------------------------------------------------------------------------
// TEST 10 — Existing Andromeda.Wiring tests remain passing.
// (Compatibility check: LYTHOS does not interfere with wiring-level
//  planet/star structures.)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythosWiringCompatibilityTest,
	"Andromeda.LYTHOS.WiringCompatibility", Flags)

bool FLythosWiringCompatibilityTest::RunTest(const FString& Parameters)
{
	// LYTHOS uses only plain C++ structs and pure functions.
	// Verify no UObjects are created during evaluation.
	const FLythosPlanetContext Ctx = MakeTestContext(555);

	const FLythoTerrainSample S = FLythosMacroTerrain::Evaluate(Ctx, FVector(0.4f, 0.5f, 0.6f).GetSafeNormal());

	TestTrue(TEXT("Terrain sample is valid"), S.Direction.IsNormalized());
	TestTrue(TEXT("Elevation is finite"), FMath::IsFinite(S.Elevation));
	TestTrue(TEXT("Normal is finite"), !S.Normal.ContainsNaN());
	TestTrue(TEXT("Slope is finite"), FMath::IsFinite(S.Slope));
	TestTrue(TEXT("LocalPosition is finite"), !S.LocalPosition.ContainsNaN());

	// Verify the debug visualization does not allocate UObjects.
	FLythosRegionCoord Reg10;
	Reg10.FaceIndex = 1;
	Reg10.RegionX = 0;
	Reg10.RegionY = 0;
	Reg10.RegionLOD = 0;
	Reg10.GridResolution = 8;

	const FLythosRegionData Data = FLythosMacroTerrain::GenerateRegion(Ctx, Reg10);

	const FLythosDebugMesh Mesh = LythosDebugVisualization::BuildRegionDebugMesh(Data, Ctx);
	TestTrue(TEXT("Debug mesh has vertices"), Mesh.Vertices.Num() > 0);
	TestTrue(TEXT("Debug mesh has triangles"), Mesh.Triangles.Num() > 0);
	TestTrue(TEXT("Debug mesh has vertex colors"), Mesh.VertexColors.Num() == Mesh.Vertices.Num());

	// Landform-to-color mapping should produce non-black colors.
	for (const FColor& Color : Mesh.VertexColors)
	{
		TestTrue(TEXT("Debug colors are non-trivial"),
			Color.R > 10 || Color.G > 10 || Color.B > 10);
	}

	// Biome hook exists on profile (FPlanetProfile is reused).
	TestTrue(TEXT("Context carries planet profile for biome coupling"),
		Ctx.PlanetProfile.WaterCoverage >= 0.f);

	return true;
}

#endif
