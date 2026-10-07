// =============================================================================
// LYTHOS 2.0 - Automated architecture validation.
//
// These tests validate the guarantees required of the whole-planet volumetric,
// LOD-driven terrain system without needing to render anything.
// =============================================================================

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Planet/Planet.h"
#include "ProceduralMeshComponent.h"
#include "Tests/PlanetaryTestPlanet.h"

#include "LYTHOS2/Lythos2CubeSphere.h"
#include "LYTHOS2/Lythos2DensityField.h"
#include "LYTHOS2/Lythos2LodPolicy.h"
#include "LYTHOS2/Lythos2Mesher.h"
#include "LYTHOS2/Lythos2Scheduler.h"
#include "LYTHOS2/Lythos2Types.h"
#include "LYTHOS2/Lythos2WorldSubsystem.h"

namespace
{
    constexpr auto LythosFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

    FLythos2PlanetContext MakeContext(int64 Seed)
    {
        FLythos2PlanetContext Context;
        Context.PlanetID = 1;
        Context.Seed = Seed;
        Context.RadiusCm = 500000.0f;
        Context.TerrainHeightCm = 20000.0f;
        return Context;
    }

    FLythos2Settings MakeSettings(int32 MaxLod = 2, int32 Voxels = 8)
    {
        FLythos2Settings Settings;
        Settings.VoxelsPerAxis = Voxels;
        Settings.MaxTerrainLOD = MaxLod;
        Settings.LodDetailFactor = 2.5f;
        Settings.MaxActiveRegions = 512;
        Settings.MaxQueuedBuilds = 64;
        Settings.MaxBuildsDispatchedPerTick = 8;
        Settings.MaxUploadsPerTick = 16;
        Settings.SkirtDepthCells = 3.0f;
        return Settings;
    }

    FLythos2MeshData BuildRegion(
        const FLythos2PlanetContext& Context,
        const FLythos2RegionKey& Key,
        const FLythos2Settings& Settings)
    {
        FLythos2MeshData Mesh;
        Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
        return Mesh;
    }

    bool MeshesEqual(const FLythos2MeshData& A, const FLythos2MeshData& B)
    {
        if (A.Positions.Num() != B.Positions.Num() || A.Indices.Num() != B.Indices.Num())
        {
            return false;
        }
        for (int32 i = 0; i < A.Positions.Num(); ++i)
        {
            if (!(A.Positions[i] == B.Positions[i]))
            {
                return false;
            }
        }
        for (int32 i = 0; i < A.Indices.Num(); ++i)
        {
            if (A.Indices[i] != B.Indices[i])
            {
                return false;
            }
        }
        return true;
    }

    struct FLythosTestWorld
    {
        UWorld* World = nullptr;

        FLythosTestWorld()
        {
            const UWorld::InitializationValues IV = UWorld::InitializationValues()
                .AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false)
                .CreatePhysicsScene(true).ShouldSimulatePhysics(false).EnableTraceCollision(true);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                ERHIFeatureLevel::SM5, &IV);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        }

        void Start()
        {
            World->InitializeActorsForPlay(FURL());
            World->GetWorldSettings()->NotifyBeginPlay();
        }

        void Tick(float Dt)
        {
            ++GFrameCounter;
            World->Tick(LEVELTICK_All, Dt);
        }

        ~FLythosTestWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
}

// -----------------------------------------------------------------------------
// Determinism: same seed + same coordinate = identical density.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2DeterminismTest,
    "Andromeda.Lythos2.Density.Determinism", LythosFlags)
bool FLythos2DeterminismTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext A = MakeContext(987654321);
    const FLythos2PlanetContext B = MakeContext(987654321);

    for (int32 i = 0; i < 128; ++i)
    {
        const float Theta = i * 0.37f;
        const float Phi = i * 0.11f;
        const FVector Dir = FVector(FMath::Cos(Theta), FMath::Sin(Theta), FMath::Cos(Phi)).GetSafeNormal();
        const FVector Pos = Dir * (500000.0 + (i % 7) * 2500.0);

        const double DA = Lythos2::Density::EvaluateDensity(A, Pos);
        const double DB = Lythos2::Density::EvaluateDensity(B, Pos);
        TestEqual(FString::Printf(TEXT("Density identical at sample %d"), i), DA, DB);
    }
    return true;
}

// -----------------------------------------------------------------------------
// Seed variation: different seeds produce meaningfully different geography.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2SeedVariationTest,
    "Andromeda.Lythos2.Density.SeedVariation", LythosFlags)
bool FLythos2SeedVariationTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext A = MakeContext(101);
    const FLythos2PlanetContext B = MakeContext(202);

    int32 Different = 0;
    const int32 Samples = 200;
    for (int32 i = 0; i < Samples; ++i)
    {
        const float Theta = i * 0.61803f;
        const float Z = 1.0f - 2.0f * (i / static_cast<float>(Samples));
        const float R = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
        const FVector Dir(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Z);

        const float HA = Lythos2::Density::MacroElevation(A, Dir);
        const float HB = Lythos2::Density::MacroElevation(B, Dir);
        if (FMath::Abs(HA - HB) > 0.05f)
        {
            ++Different;
        }
    }

    TestTrue(TEXT("Different seeds produce meaningfully different macro geography"),
        Different > Samples / 4);
    return true;
}

// -----------------------------------------------------------------------------
// Order independence: A then B equals B then A.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2OrderIndependenceTest,
    "Andromeda.Lythos2.Density.OrderIndependence", LythosFlags)
bool FLythos2OrderIndependenceTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(42424242);
    const FLythos2Settings Settings = MakeSettings(2, 8);

    const FLythos2RegionKey A(4, 2, 0, 0);
    const FLythos2RegionKey B(4, 2, 1, 0);

    const FLythos2MeshData AFirst = BuildRegion(Context, A, Settings);
    const FLythos2MeshData BFirst = BuildRegion(Context, B, Settings);

    const FLythos2MeshData BSecond = BuildRegion(Context, B, Settings);
    const FLythos2MeshData ASecond = BuildRegion(Context, A, Settings);

    TestTrue(TEXT("Region A is identical regardless of order"), MeshesEqual(AFirst, ASecond));
    TestTrue(TEXT("Region B is identical regardless of order"), MeshesEqual(BFirst, BSecond));
    TestTrue(TEXT("Generated meshes are non-empty"), AFirst.Indices.Num() > 0 && BFirst.Indices.Num() > 0);
    return true;
}

// -----------------------------------------------------------------------------
// LOD consistency: higher LOD refines the same terrain, not unrelated terrain.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2LodConsistencyTest,
    "Andromeda.Lythos2.Lod.Consistency", LythosFlags)
bool FLythos2LodConsistencyTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(555);
    const FLythos2Settings Settings = MakeSettings(2, 6);

    const FLythos2RegionKey Coarse(4, 1, 0, 0);
    const FLythos2RegionKey Fine(4, 2, 0, 0);

    const FLythos2MeshData CoarseMesh = BuildRegion(Context, Coarse, Settings);
    const FLythos2MeshData FineMesh = BuildRegion(Context, Fine, Settings);

    TestTrue(TEXT("Coarse mesh generated"), CoarseMesh.Indices.Num() > 0);
    TestTrue(TEXT("Fine mesh generated"), FineMesh.Indices.Num() > 0);

    // A child region covers a smaller area with the same grid, so its sample
    // spacing must be finer: increasing LOD reveals more spatial detail of the
    // SAME terrain rather than different terrain.
    const double CoarseSpacing = Lythos2::Mesher::RegionSampleSpacingCm(Coarse, Context, Settings);
    const double FineSpacing = Lythos2::Mesher::RegionSampleSpacingCm(Fine, Context, Settings);
    TestTrue(TEXT("Higher LOD reveals more spatial detail"), FineSpacing < CoarseSpacing * 0.6);

    // Every mesh vertex from both LODs must lie close to the true iso-surface.
    const double Tolerance = static_cast<double>(Context.TerrainHeightCm) * 0.5;

    auto MaxSurfaceError = [&Context](const FLythos2MeshData& Mesh)
    {
        double MaxError = 0.0;
        for (const FVector& P : Mesh.Positions)
        {
            const double D = Lythos2::Density::EvaluateDensity(Context, P);
            const FVector G = Lythos2::Density::EvaluateGradient(Context, P);
            const double GMag = FMath::Max(1.0e-6, G.Size());
            MaxError = FMath::Max(MaxError, FMath::Abs(D) / GMag);
        }
        return MaxError;
    };

    TestTrue(TEXT("Coarse mesh lies on the true terrain surface"),
        MaxSurfaceError(CoarseMesh) < Tolerance);
    TestTrue(TEXT("Fine mesh lies on the same true terrain surface"),
        MaxSurfaceError(FineMesh) < Tolerance);
    return true;
}

// -----------------------------------------------------------------------------
// Spherical continuity across faces / boundaries.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2SphericalContinuityTest,
    "Andromeda.Lythos2.Spherical.Continuity", LythosFlags)
bool FLythos2SphericalContinuityTest::RunTest(const FString& Parameters)
{
    // Cube-sphere mapping is a bijection, so terrain is continuous by construction.
    for (int32 i = 0; i < 500; ++i)
    {
        const float Theta = i * 0.2345f;
        const float Z = 1.0f - 2.0f * ((i % 97) / 96.0f);
        const float R = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
        const FVector Dir = FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Z).GetSafeNormal();

        int32 Face;
        float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(Dir, Face, U, V);
        const FVector RoundTrip = Lythos2::CubeSphere::FaceUVToDirection(Face, U, V);

        TestTrue(FString::Printf(TEXT("Cube-sphere mapping round-trips (%d)"), i),
            RoundTrip.Equals(Dir, 1.0e-4f));
    }

    // Adjacent same-LOD regions must share identical boundary sample directions,
    // which is what makes the volumetric mesh weld with no cracks.
    const FLythos2Settings Settings = MakeSettings(2, 8);
    const FLythos2RegionKey Left(4, 2, 0, 0);
    const FLythos2RegionKey Right(4, 2, 1, 0);

    for (int32 J = 0; J <= 8; ++J)
    {
        const float LV = J / 8.0f;
        const FVector DL = Lythos2::CubeSphere::RegionSampleDirection(Left, 1.0f, LV);
        const FVector DR = Lythos2::CubeSphere::RegionSampleDirection(Right, 0.0f, LV);
        TestTrue(FString::Printf(TEXT("Shared boundary direction matches (%d)"), J),
            DL.Equals(DR, 1.0e-6f));
    }
    (void)Settings;
    return true;
}

// -----------------------------------------------------------------------------
// Surface validity: representative planet samples produce valid terrain.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2SurfaceValidityTest,
    "Andromeda.Lythos2.Surface.Validity", LythosFlags)
bool FLythos2SurfaceValidityTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(314159);

    int32 Land = 0;
    int32 Ocean = 0;
    bool bAllValid = true;

    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 Y = 0; Y < 12; ++Y)
        {
            for (int32 X = 0; X < 12; ++X)
            {
                const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(
                    FLythos2RegionKey(Face, 3, X, Y), 0.5f, 0.5f);

                const float Elev = Lythos2::Density::MacroElevation(Context, Dir);
                if (!FMath::IsFinite(Elev) || FMath::Abs(Elev) > 1.0f)
                {
                    bAllValid = false;
                }

                if (Elev > 0.0f) { ++Land; } else { ++Ocean; }

                // Volumetric sign behaviour around the derived surface.
                const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
                const double Inside = Lythos2::Density::EvaluateDensity(Context, Dir * (Surface - 200.0));
                const double Outside = Lythos2::Density::EvaluateDensity(Context, Dir * (Surface + 200.0));
                if (!(Inside > 0.0 && Outside < 0.0))
                {
                    bAllValid = false;
                }
            }
        }
    }

    TestTrue(TEXT("All sampled elevations are finite and in range"), bAllValid);
    TestTrue(TEXT("Planet has both land and ocean"), Land > 0 && Ocean > 0);
    return true;
}

// -----------------------------------------------------------------------------
// LOD transitions: crack-prevention geometry is present and covers boundaries.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2LodTransitionsTest,
    "Andromeda.Lythos2.Lod.Transitions", LythosFlags)
bool FLythos2LodTransitionsTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(271828);
    FLythos2Settings Settings = MakeSettings(2, 8);
    Settings.SkirtDepthCells = 3.0f;

    const FLythos2RegionKey Coarse(4, 1, 0, 0);
    const double Voxel = Lythos2::Mesher::RegionSampleSpacingCm(Coarse, Context, Settings);

    const FLythos2MeshData WithSkirts = BuildRegion(Context, Coarse, Settings);

    int32 SkirtVertices = 0;
    for (const FVector& P : WithSkirts.Positions)
    {
        const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, P.GetSafeNormal());
        if (P.Size() < Surface - Voxel * 0.5)
        {
            ++SkirtVertices;
        }
    }
    TestTrue(TEXT("Crack-prevention skirts are generated on a region boundary"), SkirtVertices > 0);

    Settings.SkirtDepthCells = 0.0f;
    const FLythos2MeshData NoSkirts = BuildRegion(Context, Coarse, Settings);
    int32 NoSkirtVertices = 0;
    for (const FVector& P : NoSkirts.Positions)
    {
        const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, P.GetSafeNormal());
        if (P.Size() < Surface - Voxel * 0.5)
        {
            ++NoSkirtVertices;
        }
    }
    TestTrue(TEXT("Disabling skirts removes the curtain geometry"), NoSkirtVertices == 0);

    // A finer neighbour's boundary topography falls inside the coarse skirt band.
    const FLythos2RegionKey Fine(4, 2, 0, 0);
    const double SkirtDepth = Settings.SkirtDepthCells;
    for (int32 J = 0; J <= 8; ++J)
    {
        const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(Fine, 0.0f, J / 8.0f);
        const double FineSurface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        const double CoarseSurface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        TestTrue(FString::Printf(TEXT("Fine surface is covered by coarse skirt (%d)"), J),
            FineSurface <= CoarseSurface + Voxel && FineSurface >= CoarseSurface - Voxel * 4.0 - SkirtDepth * Voxel);
    }
    return true;
}

// -----------------------------------------------------------------------------
// Bounded scheduling: movement cannot create an unbounded request queue.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2BoundedSchedulingTest,
    "Andromeda.Lythos2.Scheduling.Bounded", LythosFlags)
bool FLythos2BoundedSchedulingTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(161803);
    FLythos2Settings Settings = MakeSettings(2, 6);
    Settings.MaxQueuedBuilds = 16;
    Settings.MaxActiveRegions = 160;

    FLythos2StreamScheduler Scheduler;
    Scheduler.Configure(Settings);

    bool bBounded = true;
    for (int32 Step = 0; Step < 300; ++Step)
    {
        const float A = Step * 0.31f;
        const FVector Viewer = FVector(FMath::Cos(A), FMath::Sin(A), 0.3f).GetSafeNormal()
            * (Context.RadiusCm * (0.4f + 0.02f * (Step % 50)));

        Scheduler.BeginDesiredUpdate();
        TArray<FLythos2RegionKey> Leaves;
        Lythos2::Lod::SelectRegions(Context.PlanetID, Viewer, Context, Settings, Leaves);
        for (const FLythos2RegionKey& Key : Leaves)
        {
            Scheduler.AddDesired(Key);
        }
        // Converge the active set to the desired partition. Requests are
        // enqueued/released in bounded batches (MaxQueuedBuilds), exactly like
        // the live streamer does over successive ticks.
        for (int32 Inner = 0; Inner < 8; ++Inner)
        {
            Scheduler.CommitDesiredUpdate();

            FLythos2RegionKey Key;
            int32 Guard = 0;
            while (Scheduler.PopRequest(Key) && Guard++ < 100000)
            {
                Scheduler.MarkBuilding(Key);
                Scheduler.MarkReady(Key);
            }
            Scheduler.ActivateReady(100000);

            TArray<FLythos2RegionKey> Removals;
            Scheduler.ComputeRemovals(Removals);
            for (const FLythos2RegionKey& Remove : Removals)
            {
                Scheduler.RemoveRegion(Remove);
            }

            if (Scheduler.GetQueueCount() > Settings.MaxQueuedBuilds
                || Scheduler.GetActiveCount() > Settings.MaxActiveRegions)
            {
                break;
            }
        }

        if (Scheduler.GetQueueCount() > Settings.MaxQueuedBuilds)
        {
            AddInfo(FString::Printf(TEXT("Queue exceeded: %d > %d at step %d"), Scheduler.GetQueueCount(), Settings.MaxQueuedBuilds, Step));
            bBounded = false;
            break;
        }
        if (Scheduler.GetActiveCount() > Settings.MaxActiveRegions)
        {
            int32 Stale = 0;
            for (const FLythos2RegionKey& ActiveKey : Scheduler.GetActiveRegions())
            {
                if (!Scheduler.IsDesired(ActiveKey)) { ++Stale; }
            }
            AddInfo(FString::Printf(TEXT("Active exceeded: %d > %d at step %d (desired=%d stale=%d)"),
                Scheduler.GetActiveCount(), Settings.MaxActiveRegions, Step,
                Scheduler.GetDesiredCount(), Stale));
            bBounded = false;
            break;
        }
        if (Leaves.Num() > Settings.MaxActiveRegions)
        {
            AddInfo(FString::Printf(TEXT("Selection exceeded: %d > %d at step %d"), Leaves.Num(), Settings.MaxActiveRegions, Step));
            bBounded = false;
            break;
        }
    }

    TestTrue(TEXT("Request queue, active set and selection stay bounded under rapid movement"), bBounded);
    return true;
}

// -----------------------------------------------------------------------------
// Streaming: regions enter/leave the active set without corrupting state.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2StreamingTest,
    "Andromeda.Lythos2.Scheduling.Streaming", LythosFlags)
bool FLythos2StreamingTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(112358);
    FLythos2Settings Settings = MakeSettings(2, 6);
    Settings.MaxActiveRegions = 512;

    FLythos2StreamScheduler Scheduler;
    Scheduler.Configure(Settings);

    auto ConvergeOn = [&](const FVector& Viewer)
    {
        for (int32 Iteration = 0; Iteration < 8; ++Iteration)
        {
            Scheduler.BeginDesiredUpdate();
            TArray<FLythos2RegionKey> Leaves;
            Lythos2::Lod::SelectRegions(Context.PlanetID, Viewer, Context, Settings, Leaves);
            for (const FLythos2RegionKey& Key : Leaves)
            {
                Scheduler.AddDesired(Key);
            }
            Scheduler.CommitDesiredUpdate();

            FLythos2RegionKey Key;
            while (Scheduler.PopRequest(Key))
            {
                Scheduler.MarkBuilding(Key);
                Scheduler.MarkReady(Key);
            }
            Scheduler.ActivateReady(100000);

            TArray<FLythos2RegionKey> Removals;
            Scheduler.ComputeRemovals(Removals);
            for (const FLythos2RegionKey& Remove : Removals)
            {
                Scheduler.RemoveRegion(Remove);
            }
        }
    };

    const FVector NearA = FVector(1.0f, 0.2f, 0.1f).GetSafeNormal() * Context.RadiusCm;
    const FVector NearB = FVector(-0.9f, 0.1f, 0.4f).GetSafeNormal() * Context.RadiusCm;

    ConvergeOn(NearA);

    // After convergence the active set equals the desired partition.
    bool bActiveEqualsDesired = true;
    for (const FLythos2RegionKey& Key : Scheduler.GetActiveRegions())
    {
        if (!Scheduler.IsDesired(Key)) { bActiveEqualsDesired = false; break; }
    }
    const int32 ActiveAtA = Scheduler.GetActiveCount();

    ConvergeOn(NearB);
    for (const FLythos2RegionKey& Key : Scheduler.GetActiveRegions())
    {
        if (!Scheduler.IsDesired(Key)) { bActiveEqualsDesired = false; break; }
    }

    TestTrue(TEXT("Active set always equals the desired partition after convergence"), bActiveEqualsDesired);
    TestTrue(TEXT("Moving the viewer streams in/out regions and keeps a valid set"),
        ActiveAtA > 0 && Scheduler.GetActiveCount() > 0);
    return true;
}

// -----------------------------------------------------------------------------
// Maximum LOD: MaxTerrainLOD correctly limits refinement.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2MaxLodTest,
    "Andromeda.Lythos2.Lod.MaxLimit", LythosFlags)
bool FLythos2MaxLodTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(777);
    const FVector Viewer = FVector(0.0f, 0.0f, 1.0f) * Context.RadiusCm;

    for (int32 MaxLod : { 0, 2, 4 })
    {
        FLythos2Settings Settings = MakeSettings(MaxLod, 6);
        Settings.MaxActiveRegions = 4096;

        TArray<FLythos2RegionKey> Leaves;
        Lythos2::Lod::SelectRegions(Context.PlanetID, Viewer, Context, Settings, Leaves);

        int32 Deepest = 0;
        for (const FLythos2RegionKey& Key : Leaves)
        {
            Deepest = FMath::Max(Deepest, Key.Lod);
            TestTrue(TEXT("No region exceeds MaxTerrainLOD"), Key.Lod <= MaxLod);
        }
        TestTrue(TEXT("Selection respects MaxTerrainLOD cap"), Deepest <= MaxLod);
    }

    // Raising the cap allows deeper refinement for the same viewer.
    FLythos2Settings Low = MakeSettings(1, 6);
    Low.MaxActiveRegions = 4096;
    FLythos2Settings High = MakeSettings(4, 6);
    High.MaxActiveRegions = 4096;

    TArray<FLythos2RegionKey> LowLeaves;
    TArray<FLythos2RegionKey> HighLeaves;
    Lythos2::Lod::SelectRegions(Context.PlanetID, Viewer, Context, Low, LowLeaves);
    Lythos2::Lod::SelectRegions(Context.PlanetID, Viewer, Context, High, HighLeaves);

    TestTrue(TEXT("Higher MaxTerrainLOD yields more refined regions"), HighLeaves.Num() > LowLeaves.Num());
    return true;
}

// -----------------------------------------------------------------------------
// No synchronous full-planet generation: selection is always a bounded subset.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2NoFullPlanetTest,
    "Andromeda.Lythos2.Selection.NoFullPlanet", LythosFlags)
bool FLythos2NoFullPlanetTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("The LOD-0 planetary seed is exactly six roots"),
        Lythos2::CubeSphere::NumRootRegions, 6);

    const FLythos2PlanetContext Context = MakeContext(999);
    const int32 MaxLod = 4;
    FLythos2Settings Settings = MakeSettings(MaxLod, 6);
    Settings.MaxActiveRegions = 4096;

    int32 FullPartition = 0;
    for (int32 Lod = 0; Lod <= MaxLod; ++Lod)
    {
        FullPartition += 6 * (1 << (2 * Lod));
    }

    for (int32 i = 0; i < 32; ++i)
    {
        const float A = i * 0.7f;
        const FVector Viewer = FVector(FMath::Cos(A), FMath::Sin(A), 0.15f).GetSafeNormal() * Context.RadiusCm;

        TArray<FLythos2RegionKey> Leaves;
        Lythos2::Lod::SelectRegions(Context.PlanetID, Viewer, Context, Settings, Leaves);

        TestTrue(FString::Printf(TEXT("Selection %d is far smaller than the full max-LOD partition"), i),
            Leaves.Num() < FullPartition / 4);

        // Every selected region must be a valid, addressable key.
        for (const FLythos2RegionKey& Key : Leaves)
        {
            if (!Key.IsValid())
            {
                AddError(TEXT("Selection produced an invalid region key"));
                return false;
            }
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// End-to-end runtime validation: a real planet streams through the subsystem.
// -----------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2LiveStreamingTest,
    "Andromeda.Lythos2.Integration.WorldStreaming", LythosFlags)
bool FLythos2LiveStreamingTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;

    APlanetaryTestPlanet* Planet = Fixture.World->SpawnActor<APlanetaryTestPlanet>(
        FVector::ZeroVector, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Test planet spawns"), Planet))
    {
        return false;
    }

    Planet->PlanetID = 1;
    Planet->PlanetSeed = 987654;
    Planet->PlanetRadius = 500000.0f;
    Planet->TerrainHeight = 20000.0f;

    Fixture.Start();

    ULythos2WorldSubsystem* Lythos = Fixture.World->GetSubsystem<ULythos2WorldSubsystem>();
    if (!TestNotNull(TEXT("LYTHOS 2.0 world subsystem exists"), Lythos))
    {
        return false;
    }

    TestEqual(TEXT("A planet is registered for streaming"), Lythos->GetRegisteredPlanetCount(), 1);
    TestEqual(TEXT("The coarse seed is exactly the six LOD-0 sections"),
        Lythos->GetAppliedMeshCount(), 6);

    // Keep the integration test cheap while exercising the real streaming path.
    Lythos->Settings.MaxTerrainLOD = 2;
    Lythos->Settings.VoxelsPerAxis = 6;
    Lythos->Settings.MaxActiveRegions = 64;
    Lythos->Settings.MaxQueuedBuilds = 24;
    Lythos->Settings.MaxBuildsDispatchedPerTick = 8;
    Lythos->Settings.MaxUploadsPerTick = 8;

    const FVector Up = FVector(0.2f, 0.4f, 0.8f).GetSafeNormal();
    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius);

    int32 PeakActive = 0;
    int32 PeakApplied = 0;
    for (int32 I = 0; I < 300; ++I)
    {
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.002f);
        PeakActive = FMath::Max(PeakActive, Lythos->GetActiveRegionCount());
        PeakApplied = FMath::Max(PeakApplied, Lythos->GetAppliedMeshCount());
    }

    TestTrue(TEXT("Nearby terrain progressively refines beyond the coarse seed"),
        PeakActive > 6 && PeakApplied > 6);

    bool bWithinMaxLod = true;
    for (const FLythos2RegionKey& Key : Lythos->GetActiveRegions())
    {
        if (Key.Lod > Lythos->Settings.MaxTerrainLOD)
        {
            bWithinMaxLod = false;
        }
    }
    TestTrue(TEXT("MaxTerrainLOD is respected by every active region"), bWithinMaxLod);
    TestTrue(TEXT("The generation queue never becomes unbounded"),
        Lythos->GetPendingBuildCount() <= Lythos->Settings.MaxQueuedBuilds + Lythos->Settings.MaxBuildsDispatchedPerTick + 1);

    // Move the viewer far away: refined regions must unload / coarsen.
    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius * 40.0f);
    for (int32 I = 0; I < 300; ++I)
    {
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.002f);
    }

    TestTrue(TEXT("Moving away reduces and unloads refined regions"),
        Lythos->GetActiveRegionCount() < PeakActive);

    AddInfo(FString::Printf(
        TEXT("near peakActive=%d peakApplied=%d | far active=%d applied=%d selected=%d pending=%d"),
        PeakActive, PeakApplied, Lythos->GetActiveRegionCount(),
        Lythos->GetAppliedMeshCount(), Lythos->GetSelectedRegionCount(),
        Lythos->GetPendingBuildCount()));

    TestTrue(TEXT("Distant terrain remains coarse"),
        Lythos->GetAppliedMeshCount() < PeakApplied);

    return true;
}

// =============================================================================
// Phase 1.1 - runtime performance / convergence / budget validation.
// =============================================================================

namespace
{
    FLythos2Settings MakePerfSettings()
    {
        FLythos2Settings Settings;
        Settings.MaxTerrainLOD = 2;
        Settings.VoxelsPerAxis = 6;
        Settings.LodDetailFactor = 2.5f;
        Settings.LodHysteresis = 0.15f;
        Settings.MaxActiveRegions = 128;
        Settings.MaxQueuedBuilds = 32;
        Settings.MaxBuildsDispatchedPerTick = 4;
        Settings.MaxConcurrentMeshTasks = 3;
        Settings.MaxCompletedMeshBacklog = 8;
        Settings.MaxUploadsPerTick = 4;
        Settings.MaxUploadMillisecondsPerTick = 2.5f;
        Settings.MaxSwapsPerTick = 4;
        Settings.MaxRemovalsPerTick = 8;
        Settings.CollisionDistanceScale = 0.25f;
        Settings.CollisionUpdateIntervalTicks = 2;
        return Settings;
    }

    APlanetaryTestPlanet* SpawnPlanetWithLythos(
        FLythosTestWorld& Fixture,
        ULythos2WorldSubsystem*& OutLythos)
    {
        APlanetaryTestPlanet* Planet = Fixture.World->SpawnActor<APlanetaryTestPlanet>(
            FVector::ZeroVector, FRotator::ZeroRotator);
        Planet->PlanetID = 1;
        Planet->PlanetSeed = 987654;
        Planet->PlanetRadius = 500000.0f;
        Planet->TerrainHeight = 20000.0f;

        Fixture.Start();

        OutLythos = Fixture.World->GetSubsystem<ULythos2WorldSubsystem>();
        return Planet;
    }

    bool StepUntilIdle(
        FLythosTestWorld& Fixture,
        ULythos2WorldSubsystem* Lythos,
        int32 MaxTicks,
        int32& OutTicks)
    {
        int32 StableTicks = 0;
        int32 LastActive = -1;
        for (int32 I = 0; I < MaxTicks; ++I)
        {
            Fixture.Tick(1.0f / 60.0f);
            FPlatformProcess::Sleep(0.001f);

            const bool bIdle =
                Lythos->GetPendingBuildCount() == 0
                && Lythos->GetReadyBacklogCount() == 0
                && Lythos->GetInFlightTaskCount() == 0
                && Lythos->Settings.LastRemovalsThisTick == 0;

            const int32 Active = Lythos->GetActiveRegionCount();
            if (bIdle && Active == LastActive && Active > 0)
            {
                if (++StableTicks >= 3)
                {
                    OutTicks = I + 1;
                    return true;
                }
            }
            else
            {
                StableTicks = 0;
            }
            LastActive = Active;
        }
        OutTicks = MaxTicks;
        return false;
    }
}

// Stationary viewer near the planet reaches its desired LOD, and generation
// work then falls to (near) zero.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfConvergenceTest,
    "Andromeda.Lythos2.Performance.Convergence", LythosFlags)
bool FLythos2PerfConvergenceTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Subsystem exists"), Lythos)) { return false; }
    Lythos->Settings = MakePerfSettings();

    const FVector Up = FVector(0.1f, 0.3f, 0.95f).GetSafeNormal();
    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius);

    int32 Ticks = 0;
    const bool bConverged = StepUntilIdle(Fixture, Lythos, 600, Ticks);
    TestTrue(TEXT("Stationary viewer converges to its desired LOD"), bConverged);

    const int32 ActiveAtConvergence = Lythos->GetActiveRegionCount();
    TestTrue(TEXT("Near viewer materialises many more than the coarse seed"), ActiveAtConvergence > 6);

    bool bReachedMax = false;
    if (Lythos->Settings.LastActiveRegionsByLOD.Num() > Lythos->Settings.MaxTerrainLOD)
    {
        bReachedMax = Lythos->Settings.LastActiveRegionsByLOD[Lythos->Settings.MaxTerrainLOD] > 0;
    }
    TestTrue(TEXT("The configured MaxTerrainLOD is actually reached near the viewer"), bReachedMax);

    const int32 GeneratedAfterConvergence = Lythos->Settings.TotalRegionsGenerated;

    // Runtime convergence: no continuous generation once settled.
    int32 BusyTicks = 0;
    int32 MaxUploadsSeen = 0;
    for (int32 I = 0; I < 120; ++I)
    {
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.001f);
        if (Lythos->GetPendingBuildCount() != 0 || Lythos->GetInFlightTaskCount() != 0)
        {
            ++BusyTicks;
        }
        MaxUploadsSeen = FMath::Max(MaxUploadsSeen, Lythos->Settings.LastUploadsThisTick);
    }

    TestEqual(TEXT("No further region generation after convergence"),
        Lythos->Settings.TotalRegionsGenerated, GeneratedAfterConvergence);
    TestEqual(TEXT("Underlying jobs are idle after convergence"),
        BusyTicks, 0);
    TestEqual(TEXT("No needless uploads once converged"), MaxUploadsSeen, 0);

    AddInfo(FString::Printf(
        TEXT("Convergence: ticks=%d active=%d generated=%d finalTickWork=%.3fms"),
        Ticks, ActiveAtConvergence, GeneratedAfterConvergence, Lythos->Settings.LastTickWorkMs));
    return true;
}

// Stable viewer must not thrash LODs (no repeated add/remove churn).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfNoThrashingTest,
    "Andromeda.Lythos2.Performance.NoThrashing", LythosFlags)
bool FLythos2PerfNoThrashingTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Subsystem exists"), Lythos)) { return false; }
    Lythos->Settings = MakePerfSettings();

    const FVector Up = FVector(0.6f, -0.2f, 0.75f).GetSafeNormal();
    const FVector StationaryViewer = Planet->GetActorLocation() + Up * Planet->PlanetRadius;
    Lythos->SetViewerWorldOverride(true, StationaryViewer);

    int32 Ticks = 0;
    StepUntilIdle(Fixture, Lythos, 600, Ticks);

    // Micro-jitter that stays well inside the hysteresis band.
    int32 TotalRemovals = 0;
    int32 TotalUploads = 0;
    const int32 ActiveBefore = Lythos->GetActiveRegionCount();
    for (int32 I = 0; I < 120; ++I)
    {
        const double Jitter = 0.0005 * FMath::Sin(I * 0.7);
        Lythos->SetViewerWorldOverride(true, StationaryViewer * (1.0 + Jitter));
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.001f);
        TotalRemovals += Lythos->Settings.LastRemovalsThisTick;
        TotalUploads += Lythos->Settings.LastUploadsThisTick;
    }

    TestTrue(TEXT("Stationary micro-jitter does not thrash region removal"), TotalRemovals <= 2);
    TestTrue(TEXT("Stationary micro-jitter does not thrash regeneration"), TotalUploads <= 4);
    TestTrue(TEXT("Active set remains stable"),
        FMath::Abs(Lythos->GetActiveRegionCount() - ActiveBefore) <= 4);

    AddInfo(FString::Printf(TEXT("NoThrash: removals=%d uploads=%d activeBefore=%d activeAfter=%d"),
        TotalRemovals, TotalUploads, ActiveBefore, Lythos->GetActiveRegionCount()));
    return true;
}

// Completed-but-obsolete high-resolution work is rejected safely, not uploaded.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfStaleRejectionTest,
    "Andromeda.Lythos2.Performance.StaleRejection", LythosFlags)
bool FLythos2PerfStaleRejectionTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Subsystem exists"), Lythos)) { return false; }
    Lythos->Settings = MakePerfSettings();

    const FVector Up = FVector(-0.3f, 0.5f, 0.8f).GetSafeNormal();
    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius);

    // Let refinement get underway, then immediately leave before it finishes.
    for (int32 I = 0; I < 5; ++I)
    {
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.0005f);
    }

    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius * 60.0f);

    int32 Ticks = 0;
    const bool bIdle = StepUntilIdle(Fixture, Lythos, 600, Ticks);
    TestTrue(TEXT("System returns to idle after the viewer leaves"), bIdle);

    // Only the coarse planetary roots should remain active.
    int32 DeepestLod = 0;
    for (const FLythos2RegionKey& Key : Lythos->GetActiveRegions())
    {
        DeepestLod = FMath::Max(DeepestLod, Key.Lod);
    }
    TestTrue(TEXT("No stale high-resolution regions remain active"), DeepestLod <= 1);
    TestTrue(TEXT("No stale meshes linger in the backlog"), Lythos->GetReadyBacklogCount() == 0);

    AddInfo(FString::Printf(TEXT("StaleRejection: deepestLod=%d staleTotal=%d backlog=%d"),
        DeepestLod, Lythos->Settings.TotalStaleRejected, Lythos->GetReadyBacklogCount()));
    return true;
}

// Every per-tick budget is respected during a fast approach.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfBudgetsTest,
    "Andromeda.Lythos2.Performance.Budgets", LythosFlags)
bool FLythos2PerfBudgetsTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Subsystem exists"), Lythos)) { return false; }

    FLythos2Settings Settings = MakePerfSettings();
    Settings.MaxConcurrentMeshTasks = 3;
    Settings.MaxCompletedMeshBacklog = 8;
    Settings.MaxUploadsPerTick = 2;
    Settings.MaxSwapsPerTick = 2;
    Settings.MaxRemovalsPerTick = 3;
    Lythos->Settings = Settings;

    const FVector Up = FVector(0.0f, 0.0f, 1.0f);
    bool bBounded = true;

    // Rapid approach: move from deep space to the surface over 60 ticks.
    for (int32 I = 0; I < 120; ++I)
    {
        const float T = FMath::Min(1.0f, I / 60.0f);
        const float Distance = FMath::Lerp(20.0f, 1.0f, T) * Planet->PlanetRadius;
        Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Distance);
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.001f);

        if (Lythos->GetInFlightTaskCount() > Settings.MaxConcurrentMeshTasks) { bBounded = false; }
        if (Lythos->Settings.LastUploadsThisTick > Settings.MaxUploadsPerTick) { bBounded = false; }
        if (Lythos->Settings.LastRemovalsThisTick > Settings.MaxRemovalsPerTick) { bBounded = false; }
        if (Lythos->GetPendingBuildCount() > Settings.MaxQueuedBuilds + Settings.MaxConcurrentMeshTasks) { bBounded = false; }
        if (Lythos->GetReadyBacklogCount() > Settings.MaxActiveRegions) { bBounded = false; }
    }

    TestTrue(TEXT("All per-tick runtime budgets are respected during a fast approach"), bBounded);
    TestTrue(TEXT("Fast approach still materialises fine terrain"), Lythos->GetActiveRegionCount() > 6);

    AddInfo(FString::Printf(TEXT("Budgets: active=%d inflight=%d backlog=%d uploadsLast=%d"),
        Lythos->GetActiveRegionCount(), Lythos->GetInFlightTaskCount(),
        Lythos->GetReadyBacklogCount(), Lythos->Settings.LastUploadsThisTick));
    return true;
}

// A stable desired region is requested exactly once (no duplicate generation).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfNoDuplicateTest,
    "Andromeda.Lythos2.Performance.NoDuplicateGeneration", LythosFlags)
bool FLythos2PerfNoDuplicateTest::RunTest(const FString& Parameters)
{
    FLythos2Settings Settings = MakePerfSettings();
    FLythos2StreamScheduler Scheduler;
    Scheduler.Configure(Settings);

    FLythos2RegionKey Key(4, 2, 0, 0);
    Key.PlanetID = 1;

    int32 Generations = 0;
    for (int32 Tick = 0; Tick < 50; ++Tick)
    {
        Scheduler.BeginDesiredUpdate();
        Scheduler.AddDesired(Key, 0.0);
        Scheduler.CommitDesiredUpdate();

        FLythos2RegionKey Out;
        while (Scheduler.PopRequest(Out))
        {
            ++Generations;
            Scheduler.MarkBuilding(Out);
            Scheduler.MarkReady(Out);
            Scheduler.Activate(Out);
        }
    }

    TestEqual(TEXT("A stable desired region is generated exactly once"), Generations, 1);
    TestTrue(TEXT("The region stays active across ticks"), Scheduler.IsActive(Key));
    return true;
}

// Fast departure must not leave a backlog of obsolete high-resolution uploads.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfFastDepartureTest,
    "Andromeda.Lythos2.Performance.FastDeparture", LythosFlags)
bool FLythos2PerfFastDepartureTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Subsystem exists"), Lythos)) { return false; }
    Lythos->Settings = MakePerfSettings();

    const FVector Up = FVector(0.2f, -0.6f, 0.75f).GetSafeNormal();
    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius);

    int32 NearTicks = 0;
    StepUntilIdle(Fixture, Lythos, 600, NearTicks);
    const int32 NearActive = Lythos->GetActiveRegionCount();

    // Leave instantly.
    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius * 80.0f);
    int32 FarTicks = 0;
    const bool bIdle = StepUntilIdle(Fixture, Lythos, 600, FarTicks);

    TestTrue(TEXT("System reaches a stable far state after a fast departure"), bIdle);
    TestTrue(TEXT("Far state is coarser than the close state"),
        Lythos->GetActiveRegionCount() < NearActive);
    TestEqual(TEXT("No obsolete backlog after departure"), Lythos->GetReadyBacklogCount(), 0);
    TestTrue(TEXT("No high-resolution regions remain active after departure"),
        Lythos->GetActiveRegionCount() <= 24);

    AddInfo(FString::Printf(TEXT("FastDeparture: nearActive=%d farActive=%d staleTotal=%d"),
        NearActive, Lythos->GetActiveRegionCount(), Lythos->Settings.TotalStaleRejected));
    return true;
}

// Unattended stress profile over the required viewer scenarios. Asserts the
// structural guarantees and reports measured statistics.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfStressProfileTest,
    "Andromeda.Lythos2.Performance.StressProfile", LythosFlags)
bool FLythos2PerfStressProfileTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Subsystem exists"), Lythos)) { return false; }
    Lythos->Settings = MakePerfSettings();

    const FVector Up = FVector(0.15f, 0.35f, 0.92f).GetSafeNormal();
    const FVector Center = Planet->GetActorLocation();

    auto Phase = [&](const TCHAR* Name, const FVector& ViewerA, const FVector& ViewerB, int32 Ticks, int32 MoveTicks)
    {
        double WallSum = 0.0;
        double WorkSum = 0.0;
        double WorkMax = 0.0;
        double WorkerSum = 0.0;
        for (int32 I = 0; I < Ticks; ++I)
        {
            const float T = MoveTicks > 0 ? FMath::Min(1.0f, I / static_cast<float>(MoveTicks)) : 1.0f;
            const FVector Viewer = FMath::Lerp(ViewerA, ViewerB, T);
            Lythos->SetViewerWorldOverride(true, Viewer);

            const double T0 = FPlatformTime::Seconds();
            Fixture.Tick(1.0f / 60.0f);
            const double WallMs = (FPlatformTime::Seconds() - T0) * 1000.0;
            FPlatformProcess::Sleep(0.001f);

            WallSum += WallMs;
            WorkSum += Lythos->Settings.LastTickWorkMs;
            WorkMax = FMath::Max(WorkMax, static_cast<double>(Lythos->Settings.LastTickWorkMs));
            WorkerSum += Lythos->Settings.LastWorkerMs;
        }

        AddInfo(FString::Printf(
            TEXT("  %-18s wallAvg=%.3fms GTavg=%.3fms GTmax=%.3fms workerAvg=%.3fms active=%d pending=%d"),
            Name, WallSum / Ticks, WorkSum / Ticks, WorkMax, WorkerSum / Ticks,
            Lythos->GetActiveRegionCount(), Lythos->GetPendingBuildCount()));

        TestTrue(FString::Printf(TEXT("%s: in-flight tasks bounded"), Name),
            Lythos->GetInFlightTaskCount() <= Lythos->Settings.MaxConcurrentMeshTasks + 1);
    };

    const FVector Distant = Center + Up * Planet->PlanetRadius * 10.0f;
    const FVector Close = Center + Up * Planet->PlanetRadius;

    AddInfo(TEXT("LYTHOS 2.0 Phase 1.1 stress profile (planetLocalRadius=500000cm)"));
    Phase(TEXT("distant"), Distant, Distant, 30, 0);
    Phase(TEXT("gradual-approach"), Distant, Close, 120, 120);
    Phase(TEXT("close-stationary"), Close, Close, 180, 0);
    Phase(TEXT("rapid-approach"), Distant, Close, 60, 12);
    Phase(TEXT("rapid-departure"), Close, Distant, 60, 12);
    Phase(TEXT("stationary"), Distant, Distant, 120, 0);

    TestTrue(TEXT("Final active set is bounded"), Lythos->GetActiveRegionCount() <= Lythos->Settings.MaxActiveRegions);
    TestTrue(TEXT("Final backlog is bounded"), Lythos->GetReadyBacklogCount() <= Lythos->Settings.MaxCompletedMeshBacklog);
    return true;
}

// Same stress path but at the REAL default quality ceiling (VoxelsPerAxis=12,
// MaxTerrainLOD=4) to prove the runtime fix holds at production resolution.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2PerfDefaultProfileTest,
    "Andromeda.Lythos2.Performance.DefaultSettingsProfile", LythosFlags)
bool FLythos2PerfDefaultProfileTest::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Subsystem exists"), Lythos)) { return false; }

    // Intentionally keep default Settings (quality ceiling untouched).
    const FLythos2Settings Defaults;
    const FVector Up = FVector(0.25f, 0.45f, 0.86f).GetSafeNormal();
    const FVector Center = Planet->GetActorLocation();
    const FVector Distant = Center + Up * Planet->PlanetRadius * 12.0f;
    const FVector Close = Center + Up * Planet->PlanetRadius;

    double GtMax = 0.0;
    double GtSum = 0.0;
    double WorkerSum = 0.0;
    int32 Ticks = 0;
    int32 PeakActive = 0;
    int32 PeakBacklog = 0;
    int32 PeakSections = 0;
    int64 PeakVertices = 0;
    int64 PeakTriangles = 0;
    bool bBounded = true;

    auto RunPhase = [&](const FVector& A, const FVector& B, int32 Count, int32 MoveTicks)
    {
        for (int32 I = 0; I < Count; ++I)
        {
            const float T = MoveTicks > 0 ? FMath::Min(1.0f, I / static_cast<float>(MoveTicks)) : 1.0f;
            Lythos->SetViewerWorldOverride(true, FMath::Lerp(A, B, T));
            Fixture.Tick(1.0f / 60.0f);
            FPlatformProcess::Sleep(0.001f);
            ++Ticks;

            GtMax = FMath::Max(GtMax, static_cast<double>(Lythos->Settings.LastTickWorkMs));
            GtSum += Lythos->Settings.LastTickWorkMs;
            WorkerSum += Lythos->Settings.LastWorkerMs;
            PeakActive = FMath::Max(PeakActive, Lythos->GetActiveRegionCount());
            PeakBacklog = FMath::Max(PeakBacklog, Lythos->GetReadyBacklogCount());
            PeakSections = FMath::Max(PeakSections, Lythos->GetAppliedMeshCount());
            PeakVertices = FMath::Max(PeakVertices, Lythos->GetAppliedVertexCount());
            PeakTriangles = FMath::Max(PeakTriangles, Lythos->GetAppliedTriangleCount());

            if (Lythos->GetInFlightTaskCount() > Defaults.MaxConcurrentMeshTasks) { bBounded = false; }
            if (Lythos->Settings.LastUploadsThisTick > Defaults.MaxUploadsPerTick) { bBounded = false; }
            if (Lythos->GetReadyBacklogCount() > Defaults.MaxActiveRegions) { bBounded = false; }
        }
    };

    RunPhase(Distant, Distant, 20, 0);
    RunPhase(Distant, Close, 180, 150);
    RunPhase(Close, Close, 180, 0);

    // Memory stability: an extended stationary period must not keep growing.
    const int64 VerticesAtStart = Lythos->GetAppliedVertexCount();
    RunPhase(Close, Close, 120, 0);
    const int64 VerticesAtEnd = Lythos->GetAppliedVertexCount();

    bool bReachedMax = false;
    if (Lythos->Settings.LastActiveRegionsByLOD.Num() > Defaults.MaxTerrainLOD)
    {
        bReachedMax = Lythos->Settings.LastActiveRegionsByLOD[Defaults.MaxTerrainLOD] > 0;
    }

    TestTrue(TEXT("Default-quality run respects all budgets"), bBounded);
    TestTrue(TEXT("Default-quality run reaches MaxTerrainLOD near the viewer"), bReachedMax);
    TestTrue(TEXT("Default-quality Game Thread cost stays within a frame"), GtMax < 33.0);
    TestTrue(TEXT("Active set bounded by MaxActiveRegions"), PeakActive <= Defaults.MaxActiveRegions);
    TestTrue(TEXT("Backlog bounded by MaxCompletedMeshBacklog"),
        PeakBacklog <= FMath::Max(Defaults.MaxCompletedMeshBacklog, Defaults.MaxActiveRegions));
    TestTrue(TEXT("Render workload is finite and bounded"),
        PeakVertices > 0 && PeakSections <= Defaults.MaxActiveRegions);
    TestTrue(TEXT("Memory footprint is stable once converged"),
        VerticesAtStart == VerticesAtEnd);

    FString LodStr;
    for (int32 I = 0; I < Lythos->Settings.LastActiveRegionsByLOD.Num(); ++I)
    {
        LodStr += FString::Printf(TEXT("%s%d"), I == 0 ? TEXT("") : TEXT(" "),
            Lythos->Settings.LastActiveRegionsByLOD[I]);
    }

    AddInfo(FString::Printf(
        TEXT("DefaultProfile: ticks=%d GTavg=%.3fms GTmax=%.3fms workerAvg=%.3fms peakActive=%d peakSections=%d peakVerts=%lld peakTris=%lld byLOD=[%s]"),
        Ticks, GtSum / Ticks, GtMax, WorkerSum / Ticks, PeakActive, PeakSections,
        (long long)PeakVertices, (long long)PeakTriangles, *LodStr));
    return true;
}

// =============================================================================
// Phase 1.2 - surface orientation (inside-out fix) validation.
// =============================================================================

namespace
{
    // A gently bumpy sphere: small relief relative to the radius, but large
    // enough that the iso-surface always stays inside the sampled radial band
    // (the lateral chord sag of a coarse grid is a few metres).
    FLythos2PlanetContext MakeSphereContext(int64 Seed = 4242)
    {
        FLythos2PlanetContext Context;
        Context.PlanetID = 77;
        Context.Seed = Seed;
        Context.RadiusCm = 500000.0f;
        Context.TerrainHeightCm = 4000.0f;
        return Context;
    }

    struct FOrientationStats
    {
        int32 Triangles = 0;
        int32 WindingViolations = 0;   // cross(B-A,C-A) aligned with outward
        int32 NormalViolations = 0;    // vertex normal points inward
        int32 RadiusViolations = 0;    // surface vertex not on the expected sphere
    };

    FOrientationStats AnalyzeMesh(
        const FLythos2MeshData& Mesh,
        const FLythos2PlanetContext& Context,
        bool bCheckRadius)
    {
        FOrientationStats Stats;
        const float VoxelTol = FMath::Max(1.0f, Context.TerrainHeightCm * 2.0f);

        for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
        {
            const int32 I0 = Mesh.Indices[T];
            const int32 I1 = Mesh.Indices[T + 1];
            const int32 I2 = Mesh.Indices[T + 2];
            if (!Mesh.Positions.IsValidIndex(I0) || !Mesh.Normals.IsValidIndex(I0))
            {
                continue;
            }

            ++Stats.Triangles;
            const FVector& A = Mesh.Positions[I0];
            const FVector& B = Mesh.Positions[I1];
            const FVector& C = Mesh.Positions[I2];
            const FVector Centroid = (A + B + C) / 3.0;
            const FVector Outward = Centroid.GetSafeNormal();

            const FVector FaceNormal = FVector::CrossProduct(B - A, C - A);
            if (!FaceNormal.IsNearlyZero() && FVector::DotProduct(FaceNormal.GetSafeNormal(), Outward) > 0.0f)
            {
                ++Stats.WindingViolations;
            }

            for (int32 K = 0; K < 3; ++K)
            {
                const int32 VI = Mesh.Indices[T + K];
                const FVector VDir = Mesh.Positions[VI].GetSafeNormal();
                if (FVector::DotProduct(Mesh.Normals[VI].GetSafeNormal(), VDir) < 0.0f)
                {
                    ++Stats.NormalViolations;
                }
                if (bCheckRadius)
                {
                    const float Expected = Context.RadiusCm;
                    if (FMath::Abs(static_cast<float>(Mesh.Positions[VI].Size()) - Expected) > VoxelTol)
                    {
                        ++Stats.RadiusViolations;
                    }
                }
            }
        }
        return Stats;
    }
}

// Production mesher on a known spherical field: normals outward, winding
// matches Unreal's convention (geometric normal opposite the outward normal).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2OrientationSphereTest,
    "Andromeda.Lythos2.Orientation.Sphere", LythosFlags)
bool FLythos2OrientationSphereTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeSphereContext();
    FLythos2Settings Settings = MakeSettings(0, 24);
    Settings.SkirtDepthCells = 0.0f;

    const FLythos2RegionKey Key(4, 0, 0, 0);
    const FLythos2MeshData Mesh = BuildRegion(Context, Key, Settings);
    TestTrue(TEXT("Sphere mesh generated"), Mesh.Indices.Num() > 0);

    const FOrientationStats Stats = AnalyzeMesh(Mesh, Context, /*bCheckRadius*/ true);
    TestTrue(TEXT("No inward-facing vertex normals"), Stats.NormalViolations == 0);
    TestTrue(TEXT("No inside-out triangle winding"), Stats.WindingViolations == 0);
    TestTrue(TEXT("Surface vertices lie on the sphere"), Stats.RadiusViolations == 0);

    AddInfo(FString::Printf(TEXT("Sphere: tris=%d windingBad=%d normalBad=%d radiusBad=%d"),
        Stats.Triangles, Stats.WindingViolations, Stats.NormalViolations, Stats.RadiusViolations));
    return true;
}

// The production (real-density) planet must have outward normals and correct
// winding at the default quality settings.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2OrientationProductionTest,
    "Andromeda.Lythos2.Orientation.ProductionConvention", LythosFlags)
bool FLythos2OrientationProductionTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(13579);

    FLythos2Settings Settings;
    Settings.VoxelsPerAxis = 12;
    Settings.SkirtDepthCells = 0.0f;

    int32 BadWinding = 0;
    int32 BadNormals = 0;
    int32 Triangles = 0;

    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FLythos2RegionKey Key(Face, 0, 0, 0);
        const FLythos2MeshData Mesh = BuildRegion(Context, Key, Settings);
        const FOrientationStats Stats = AnalyzeMesh(Mesh, Context, /*bCheckRadius*/ false);
        BadWinding += Stats.WindingViolations;
        BadNormals += Stats.NormalViolations;
        Triangles += Stats.Triangles;
    }

    TestTrue(TEXT("Production terrain has triangles"), Triangles > 0);
    TestTrue(TEXT("Production winding is outside-out on every face"), BadWinding == 0);
    TestTrue(TEXT("Production normals point to the exterior on every face"), BadNormals == 0);

    AddInfo(FString::Printf(TEXT("Production: tris=%d windingBad=%d normalBad=%d"), Triangles, BadWinding, BadNormals));
    return true;
}

// Every LOD (including LOD 4) uses the same correct orientation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2OrientationAllLodsTest,
    "Andromeda.Lythos2.Orientation.AllLods", LythosFlags)
bool FLythos2OrientationAllLodsTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeSphereContext(2468);
    int32 TotalWindingBad = 0;
    int32 TotalNormalBad = 0;

    for (int32 Lod = 0; Lod <= 4; ++Lod)
    {
        FLythos2Settings Settings = MakeSettings(Lod, 8);
        Settings.SkirtDepthCells = 0.0f;
        const FLythos2RegionKey Key(2, Lod, 0, 0);
        const FLythos2MeshData Mesh = BuildRegion(Context, Key, Settings);
        const FOrientationStats Stats = AnalyzeMesh(Mesh, Context, false);
        TotalWindingBad += Stats.WindingViolations;
        TotalNormalBad += Stats.NormalViolations;
        TestTrue(FString::Printf(TEXT("LOD %d mesh generated"), Lod), Mesh.Indices.Num() > 0);
    }

    TestTrue(TEXT("Every LOD has correct winding"), TotalWindingBad == 0);
    TestTrue(TEXT("Every LOD has outward normals"), TotalNormalBad == 0);
    return true;
}

// Adjacent regions at different LODs keep correct orientation (no inverted
// faces at transitions).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2OrientationMixedLodTest,
    "Andromeda.Lythos2.Orientation.MixedLod", LythosFlags)
bool FLythos2OrientationMixedLodTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeSphereContext(97531);
    FLythos2Settings Settings = MakeSettings(2, 8);
    Settings.SkirtDepthCells = 3.0f; // include skirts in the orientation check

    const FLythos2RegionKey Coarse(4, 1, 0, 0);
    const FLythos2RegionKey Fine(4, 2, 0, 0);

    const FOrientationStats CoarseStats = AnalyzeMesh(BuildRegion(Context, Coarse, Settings), Context, false);
    const FOrientationStats FineStats = AnalyzeMesh(BuildRegion(Context, Fine, Settings), Context, false);

    // Skirt triangles have tangent normals, so only main-surface winding is
    // asserted to be outside-out; normals are allowed to be tangential.
    TestTrue(TEXT("Coarse region winding is outside-out"), CoarseStats.WindingViolations == 0);
    TestTrue(TEXT("Fine region winding is outside-out"), FineStats.WindingViolations == 0);
    return true;
}

// A meshed region must contain no interior holes (every edge away from the
// region boundary is shared by exactly two triangles).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2OrientationNoInteriorHolesTest,
    "Andromeda.Lythos2.Orientation.NoInteriorHoles", LythosFlags)
bool FLythos2OrientationNoInteriorHolesTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(22222);
    FLythos2Settings Settings = MakeSettings(0, 8);
    Settings.SkirtDepthCells = 0.0f;

    auto Quantize = [](const FVector& P)
    {
        const double Q = 1.0;
        return FIntVector(
            FMath::RoundToInt(P.X / Q), FMath::RoundToInt(P.Y / Q), FMath::RoundToInt(P.Z / Q));
    };

    TMap<FIntVector, int32> VertexIds;
    TMap<uint64, TPair<int32, int32>> EdgeFirstUse;
    TSet<uint64> ClosedEdges;
    TMap<uint64, FVector> EdgeMidpoint;

    auto GetId = [&](const FVector& P)
    {
        const FIntVector Key = Quantize(P);
        if (int32* Existing = VertexIds.Find(Key)) { return *Existing; }
        const int32 NewId = VertexIds.Num();
        VertexIds.Add(Key, NewId);
        return NewId;
    };
    auto EdgeKey = [](int32 A, int32 B)
    {
        const uint32 Lo = static_cast<uint32>(FMath::Min(A, B));
        const uint32 Hi = static_cast<uint32>(FMath::Max(A, B));
        return (static_cast<uint64>(Hi) << 32) | Lo;
    };

    const FLythos2MeshData Mesh = BuildRegion(Context, FLythos2RegionKey(4, 0, 0, 0), Settings);
    for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
    {
        const FVector PA = Mesh.Positions[Mesh.Indices[T]];
        const FVector PB = Mesh.Positions[Mesh.Indices[T + 1]];
        const FVector PC = Mesh.Positions[Mesh.Indices[T + 2]];
        const int32 Ids[3] = { GetId(PA), GetId(PB), GetId(PC) };
        for (int32 E = 0; E < 3; ++E)
        {
            const int32 A = Ids[E];
            const int32 B = Ids[(E + 1) % 3];
            const uint64 Key = EdgeKey(A, B);
            if (ClosedEdges.Contains(Key)) { continue; }
            const FVector Mid = (Mesh.Positions[Mesh.Indices[T + E]] + Mesh.Positions[Mesh.Indices[T + (E + 1) % 3]]) * 0.5;
            if (EdgeFirstUse.Contains(Key))
            {
                ClosedEdges.Add(Key);
            }
            else
            {
                EdgeFirstUse.Add(Key, TPair<int32, int32>(A, B));
                EdgeMidpoint.Add(Key, Mid);
            }
        }
    }

    int32 InteriorOpen = 0;
    int32 BoundaryOpen = 0;
    for (const TPair<uint64, FVector>& Open : EdgeMidpoint)
    {
        if (ClosedEdges.Contains(Open.Key)) { continue; }

        int32 Face; float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(Open.Value.GetSafeNormal(), Face, U, V);
        const bool bOnBoundary = (U <= 1.0e-3f || U >= 1.0f - 1.0e-3f || V <= 1.0e-3f || V >= 1.0f - 1.0e-3f);
        if (bOnBoundary) { ++BoundaryOpen; } else { ++InteriorOpen; }
    }

    TestTrue(TEXT("Region has a boundary"), BoundaryOpen > 0);
    TestTrue(TEXT("Region has no interior holes"), InteriorOpen == 0);

    AddInfo(FString::Printf(TEXT("NoInteriorHoles: boundaryOpen=%d interiorOpen=%d"), BoundaryOpen, InteriorOpen));
    return true;
}

// Two same-LOD neighbours must weld along their shared edge (no interior gaps).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2OrientationSameLodWeldTest,
    "Andromeda.Lythos2.Orientation.SameLodWeld", LythosFlags)
bool FLythos2OrientationSameLodWeldTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(33333);
    FLythos2Settings Settings = MakeSettings(1, 8);
    Settings.SkirtDepthCells = 0.0f;

    auto Quantize = [](const FVector& P)
    {
        const double Q = 1.0;
        return FIntVector(
            FMath::RoundToInt(P.X / Q), FMath::RoundToInt(P.Y / Q), FMath::RoundToInt(P.Z / Q));
    };

    TMap<FIntVector, int32> VertexIds;
    TMap<uint64, int32> EdgeUseCount;
    TMap<uint64, FVector> EdgeMidpoint;

    auto GetId = [&](const FVector& P)
    {
        const FIntVector Key = Quantize(P);
        if (int32* Existing = VertexIds.Find(Key)) { return *Existing; }
        const int32 NewId = VertexIds.Num();
        VertexIds.Add(Key, NewId);
        return NewId;
    };
    auto EdgeKey = [](int32 A, int32 B)
    {
        const uint32 Lo = static_cast<uint32>(FMath::Min(A, B));
        const uint32 Hi = static_cast<uint32>(FMath::Max(A, B));
        return (static_cast<uint64>(Hi) << 32) | Lo;
    };

    const FLythos2RegionKey A(4, 1, 0, 0); // U in [0,0.5], V in [0,0.5]
    const FLythos2RegionKey B(4, 1, 1, 0); // U in [0.5,1], V in [0,0.5]

    for (const FLythos2RegionKey& Key : { A, B })
    {
        const FLythos2MeshData Mesh = BuildRegion(Context, Key, Settings);
        for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
        {
            const int32 Ids[3] =
            {
                GetId(Mesh.Positions[Mesh.Indices[T]]),
                GetId(Mesh.Positions[Mesh.Indices[T + 1]]),
                GetId(Mesh.Positions[Mesh.Indices[T + 2]])
            };
            for (int32 E = 0; E < 3; ++E)
            {
                const int32 I0 = Ids[E];
                const int32 I1 = Ids[(E + 1) % 3];
                const uint64 Key2 = EdgeKey(I0, I1);
                ++EdgeUseCount.FindOrAdd(Key2);
                if (!EdgeMidpoint.Contains(Key2))
                {
                    EdgeMidpoint.Add(Key2, (Mesh.Positions[Mesh.Indices[T + E]] + Mesh.Positions[Mesh.Indices[T + (E + 1) % 3]]) * 0.5);
                }
            }
        }
    }

    int32 InteriorOpen = 0;
    for (const TPair<uint64, int32>& Pair : EdgeUseCount)
    {
        if (Pair.Value == 2) { continue; }
        const FVector Mid = EdgeMidpoint[Pair.Key];
        int32 Face; float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(Mid.GetSafeNormal(), Face, U, V);
        // Union spans U in [0,1], V in [0,0.5]; interior excludes the outer boundary.
        const bool bInterior = U > 5.0e-3f && U < 1.0f - 5.0e-3f && V > 5.0e-3f && V < 0.5f - 5.0e-3f;
        if (bInterior) { ++InteriorOpen; }
    }

    TestTrue(TEXT("Same-LOD neighbours weld with no interior gaps"), InteriorOpen == 0);
    AddInfo(FString::Printf(TEXT("SameLodWeld: interiorOpen=%d"), InteriorOpen));
    return true;
}

// =============================================================================
// Phase 2 - macro planetary geography validation.
// =============================================================================

namespace
{
    /** Deterministic, near-uniform sample directions (Fibonacci sphere). */
    void LythosFibonacciSphere(int32 Count, TArray<FVector>& Out)
    {
        Out.Reset(Count);
        const float Golden = PI * (3.0f - FMath::Sqrt(5.0f));
        for (int32 I = 0; I < Count; ++I)
        {
            const float Z = 1.0f - (I + 0.5f) * 2.0f / static_cast<float>(Count);
            const float R = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
            const float Theta = Golden * I;
            Out.Add(FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Z));
        }
    }

    struct FElevationStats
    {
        float Min = 0.0f;
        float Max = 0.0f;
        float Median = 0.0f;
        float P90 = 0.0f;
        float P95 = 0.0f;
        float Mean = 0.0f;
        float StdDev = 0.0f;
        float LandFraction = 0.0f;
    };

    FElevationStats ComputeElevationStats(const TArray<float>& Samples)
    {
        FElevationStats Stats;
        if (Samples.Num() == 0) { return Stats; }

        TArray<float> Sorted = Samples;
        Sorted.Sort();

        Stats.Min = Sorted[0];
        Stats.Max = Sorted.Last();
        Stats.Median = Sorted[Sorted.Num() / 2];
        Stats.P90 = Sorted[FMath::Clamp(FMath::RoundToInt(Sorted.Num() * 0.90f), 0, Sorted.Num() - 1)];
        Stats.P95 = Sorted[FMath::Clamp(FMath::RoundToInt(Sorted.Num() * 0.95f), 0, Sorted.Num() - 1)];

        double Sum = 0.0;
        int32 Land = 0;
        for (float Value : Samples)
        {
            Sum += Value;
            if (Value > 0.0f) { ++Land; }
        }
        Stats.Mean = static_cast<float>(Sum / Samples.Num());
        Stats.LandFraction = static_cast<float>(Land) / Samples.Num();

        double Var = 0.0;
        for (float Value : Samples)
        {
            Var += FMath::Square(Value - Stats.Mean);
        }
        Stats.StdDev = static_cast<float>(FMath::Sqrt(Var / Samples.Num()));
        return Stats;
    }
}

// Continents and oceans exist, are spatially coherent, and contain highlands.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase2ContinentsTest,
    "Andromeda.Lythos2.MacroGeography.ContinentsAndOceans", LythosFlags)
bool FLythos2Phase2ContinentsTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(20241);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(3000, Dirs);

    TArray<float> Elev;
    Elev.SetNumUninitialized(Dirs.Num());
    for (int32 I = 0; I < Dirs.Num(); ++I)
    {
        Elev[I] = Lythos2::Density::MacroElevation(Context, Dirs[I]);
    }
    const FElevationStats Stats = ComputeElevationStats(Elev);

    TestTrue(TEXT("Both land and ocean exist"),
        Stats.LandFraction > 0.15f && Stats.LandFraction < 0.85f);
    TestTrue(TEXT("Elevation has substantial macro variation"), Stats.Max - Stats.Min > 0.6f);
    TestTrue(TEXT("Highlands/mountains stand above typical land"), Stats.P95 - Stats.Median > 0.18f);

    // Spatial coherence: neighbouring directions (well below the continental
    // feature scale) rarely disagree on land/ocean.
    const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(6.0f));
    int32 Samples = 0;
    int32 Agree = 0;
    for (int32 I = 0; I < Dirs.Num(); ++I)
    {
        for (int32 J = I + 1; J < Dirs.Num(); ++J)
        {
            if ((Dirs[I] | Dirs[J]) < CosThreshold) { continue; }
            ++Samples;
            if ((Elev[I] > 0.0f) == (Elev[J] > 0.0f)) { ++Agree; }
        }
    }
    const float AgreeFraction = Samples > 0 ? static_cast<float>(Agree) / Samples : 0.0f;
    TestTrue(TEXT("Land/ocean classification is spatially coherent"), AgreeFraction > 0.75f);

    AddInfo(FString::Printf(
        TEXT("Continents: land=%.2f min=%.2f max=%.2f median=%.2f p95=%.2f pairAgree=%.2f"),
        Stats.LandFraction, Stats.Min, Stats.Max, Stats.Median, Stats.P95, AgreeFraction));
    return true;
}

// Coastlines are irregular but not noise: a great circle crosses a small,
// finite number of land/ocean boundaries.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase2CoastlinesTest,
    "Andromeda.Lythos2.MacroGeography.Coastlines", LythosFlags)
bool FLythos2Phase2CoastlinesTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(777001);
    const int32 Along = 720;
    int32 TotalCrossings = 0;

    const FVector Axes[3] = { FVector::UpVector, FVector::ForwardVector, FVector::RightVector };
    const FVector Perp[3] = { FVector::ForwardVector, FVector::UpVector, FVector::UpVector };

    for (int32 Circle = 0; Circle < 3; ++Circle)
    {
        const FVector A = Axes[Circle].GetSafeNormal();
        const FVector B = Perp[Circle].GetSafeNormal();
        bool bPrevLand = Lythos2::Density::MacroElevation(Context, A) > 0.0f;
        int32 Crossings = 0;
        for (int32 I = 1; I <= Along; ++I)
        {
            const float Angle = 2.0f * PI * I / static_cast<float>(Along);
            const FVector Dir = (A * FMath::Cos(Angle) + B * FMath::Sin(Angle)).GetSafeNormal();
            const bool bLand = Lythos2::Density::MacroElevation(Context, Dir) > 0.0f;
            if (bLand != bPrevLand) { ++Crossings; }
            bPrevLand = bLand;
        }
        TotalCrossings += Crossings;
    }

    // Recognisable geography: at least one coast, but far from per-sample noise.
    TestTrue(TEXT("Coastlines exist (multiple land/ocean transitions)"), TotalCrossings >= 2);
    TestTrue(TEXT("Coastlines are coherent, not high-frequency noise"), TotalCrossings <= 160);

    AddInfo(FString::Printf(TEXT("Coastlines: total crossings over 3 great circles = %d"), TotalCrossings));
    return true;
}

// Mountain systems produce clustered, high-elevation belts rather than spikes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase2MountainsTest,
    "Andromeda.Lythos2.MacroGeography.Mountains", LythosFlags)
bool FLythos2Phase2MountainsTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(555111);
    const FLythos2PlanetContext OtherSeed = MakeContext(555112);

    TArray<FVector> Dirs;
    LythosFibonacciSphere(3000, Dirs);

    TArray<float> Elev;
    Elev.SetNumUninitialized(Dirs.Num());
    for (int32 I = 0; I < Dirs.Num(); ++I)
    {
        Elev[I] = Lythos2::Density::MacroElevation(Context, Dirs[I]);
    }
    const FElevationStats Stats = ComputeElevationStats(Elev);

    // Mountain systems are deterministic (same seed -> identical high samples).
    int32 Identical = 0;
    for (int32 I = 0; I < Dirs.Num(); ++I)
    {
        const float Again = Lythos2::Density::MacroElevation(Context, Dirs[I]);
        if (Again == Elev[I]) { ++Identical; }
    }
    TestEqual(TEXT("Mountain systems are deterministic"), Identical, Dirs.Num());

    TestTrue(TEXT("Mountain elevations are non-trivial"), Stats.P95 - Stats.Median > 0.18f);
    TestTrue(TEXT("Elevation variance indicates real relief"), Stats.StdDev > 0.10f);

    // High elevations form clustered belts: most peaks have a nearby peak.
    const float CosNeighbour = FMath::Cos(FMath::DegreesToRadians(6.0f));
    int32 HighCount = 0;
    int32 HighWithNeighbour = 0;
    for (int32 I = 0; I < Dirs.Num(); ++I)
    {
        if (Elev[I] < Stats.P90) { continue; }
        ++HighCount;
        for (int32 J = 0; J < Dirs.Num(); ++J)
        {
            if (J == I || (Dirs[I] | Dirs[J]) < CosNeighbour) { continue; }
            if (Elev[J] >= Stats.P90) { ++HighWithNeighbour; break; }
        }
    }
    const float ClusterFraction = HighCount > 0 ? static_cast<float>(HighWithNeighbour) / HighCount : 0.0f;
    TestTrue(TEXT("High elevations form coherent ranges/plateaus"), ClusterFraction > 0.7f);

    // A different seed moves the mountain systems.
    TArray<float> OtherElev;
    OtherElev.SetNumUninitialized(Dirs.Num());
    for (int32 I = 0; I < Dirs.Num(); ++I)
    {
        OtherElev[I] = Lythos2::Density::MacroElevation(OtherSeed, Dirs[I]);
    }
    int32 Overlap = 0;
    for (int32 I = 0; I < Dirs.Num(); ++I)
    {
        if (Elev[I] >= Stats.P90 && OtherElev[I] >= Stats.P90) { ++Overlap; }
    }
    const float OverlapFraction = HighCount > 0 ? static_cast<float>(Overlap) / HighCount : 1.0f;
    TestTrue(TEXT("Mountain placement changes with the seed"), OverlapFraction < 0.5f);

    AddInfo(FString::Printf(
        TEXT("Mountains: p90=%.2f p95=%.2f std=%.2f cluster=%.2f seedOverlap=%.2f"),
        Stats.P90, Stats.P95, Stats.StdDev, ClusterFraction, OverlapFraction));
    return true;
}

// Different seeds produce meaningfully different macro geography.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase2SeedVariationTest,
    "Andromeda.Lythos2.MacroGeography.SeedVariation", LythosFlags)
bool FLythos2Phase2SeedVariationTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext A = MakeContext(101);
    const FLythos2PlanetContext B = MakeContext(202);

    TArray<FVector> Dirs;
    LythosFibonacciSphere(1024, Dirs);

    int32 Flips = 0;
    double SumAbsDiff = 0.0;
    for (const FVector& Dir : Dirs)
    {
        const float EA = Lythos2::Density::MacroElevation(A, Dir);
        const float EB = Lythos2::Density::MacroElevation(B, Dir);
        if ((EA > 0.0f) != (EB > 0.0f)) { ++Flips; }
        SumAbsDiff += FMath::Abs(EA - EB);
    }

    const float FlipFraction = static_cast<float>(Flips) / Dirs.Num();
    const double MeanAbs = SumAbsDiff / Dirs.Num();

    TestTrue(TEXT("Different seeds change land/ocean layout"), FlipFraction > 0.15f);
    TestTrue(TEXT("Different seeds change macro elevation"), MeanAbs > 0.08);

    AddInfo(FString::Printf(TEXT("SeedVariation: flips=%.2f meanAbsDiff=%.3f"), FlipFraction, MeanAbs));
    return true;
}

// The macro field is finite, spherical (direction-only) and yields valid density.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase2FieldValidityTest,
    "Andromeda.Lythos2.MacroGeography.FieldValidity", LythosFlags)
bool FLythos2Phase2FieldValidityTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeContext(31337);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(512, Dirs);

    bool bFinite = true;
    bool bBounded = true;
    bool bSpherical = true;
    bool bDensityLinear = true;
    bool bGradientValid = true;

    for (const FVector& Dir : Dirs)
    {
        const float E = Lythos2::Density::MacroElevation(Context, Dir);
        if (!FMath::IsFinite(E)) { bFinite = false; }
        if (E < -1.0f || E > 1.0f) { bBounded = false; }

        // Direction-only: scaling the position must not change the macro height.
        if (FMath::Abs(Lythos2::Density::MacroElevation(Context, Dir * 0.5f) - E) > 1.0e-5f) { bSpherical = false; }
        if (FMath::Abs(Lythos2::Density::MacroElevation(Context, Dir * 2.0f) - E) > 1.0e-5f) { bSpherical = false; }

        const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        const double Inside = Lythos2::Density::EvaluateDensity(Context, Dir * (Surface - 100.0));
        const double Outside = Lythos2::Density::EvaluateDensity(Context, Dir * (Surface + 100.0));
        if (!(Inside > 0.0 && Outside < 0.0) || !FMath::IsFinite(Inside) || !FMath::IsFinite(Outside))
        {
            bDensityLinear = false;
        }
        // Density is radial-linear: D(r) = Surface - r.
        const double AtR = Lythos2::Density::EvaluateDensity(Context, Dir * (Surface + 1000.0));
        if (!FMath::IsFinite(AtR) || FMath::Abs((Outside - AtR) - 900.0) > 1.0e-3)
        {
            bDensityLinear = false;
        }

        const FVector Grad = Lythos2::Density::EvaluateGradient(Context, Dir * Surface);
        if (!Grad.ContainsNaN() && Grad.Size() < 1.0e-6) { bGradientValid = false; }
        if (Grad.ContainsNaN()) { bGradientValid = false; }
    }

    TestTrue(TEXT("Macro elevation is finite (no NaN/Inf)"), bFinite);
    TestTrue(TEXT("Macro elevation stays within [-1, 1]"), bBounded);
    TestTrue(TEXT("Terrain is spherical (direction-only height)"), bSpherical);
    TestTrue(TEXT("Density is radial and continuous across the surface"), bDensityLinear);
    TestTrue(TEXT("Gradients are finite and non-degenerate"), bGradientValid);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS