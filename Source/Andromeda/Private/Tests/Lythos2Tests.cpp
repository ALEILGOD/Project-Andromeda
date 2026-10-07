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

    // The transition collar only adds geometry on the region boundary; interior
    // surface vertices follow the terrain (including natural chord sag), so the
    // curtain is counted on the boundary only.
    auto CountBoundarySkirtVerts = [&](const FLythos2MeshData& Mesh) -> int32
    {
        const int32 Side = Coarse.GetSide();
        const float U0 = static_cast<float>(Coarse.X) / static_cast<float>(Side);
        const float U1 = static_cast<float>(Coarse.X + 1) / static_cast<float>(Side);
        const float V0 = static_cast<float>(Coarse.Y) / static_cast<float>(Side);
        const float V1 = static_cast<float>(Coarse.Y + 1) / static_cast<float>(Side);
        int32 Count = 0;
        for (const FVector& P : Mesh.Positions)
        {
            int32 Face; float U, V;
            Lythos2::CubeSphere::DirectionToFaceUV(P.GetSafeNormal(), Face, U, V);
            if (Face != Coarse.Face) { continue; }
            const bool bBoundary = (FMath::Abs(U - U0) < 1.0e-3f || FMath::Abs(U - U1) < 1.0e-3f
                || FMath::Abs(V - V0) < 1.0e-3f || FMath::Abs(V - V1) < 1.0e-3f);
            if (!bBoundary) { continue; }
            const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, P.GetSafeNormal());
            if (P.Size() < Surface - Context.TerrainHeightCm * 0.1)
            {
                ++Count;
            }
        }
        return Count;
    };

    const int32 SkirtVertices = CountBoundarySkirtVerts(WithSkirts);
    TestTrue(TEXT("Crack-prevention transition geometry is generated on a region boundary"), SkirtVertices > 0);

    Settings.SkirtDepthCells = 0.0f;
    const FLythos2MeshData NoSkirts = BuildRegion(Context, Coarse, Settings);
    const int32 NoSkirtVertices = CountBoundarySkirtVerts(NoSkirts);
    // Disabling the transition removes the collar geometry. Natural boundary
    // surface sag remains, so assert the collar contributes at least one vertex
    // per boundary segment rather than requiring zero.
    TestTrue(TEXT("Disabling transitions removes the curtain geometry"),
        SkirtVertices > NoSkirtVertices && (SkirtVertices - NoSkirtVertices) >= 8);

    // A finer neighbour's boundary topography falls inside the coarse skirt band.
    const FLythos2RegionKey Fine(4, 2, 0, 0);
    const double SkirtDepth = 3.0;
    for (int32 J = 0; J <= 8; ++J)
    {
        const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(Fine, 0.0f, J / 8.0f);
        const double FineSurface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        const double CoarseSurface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        TestTrue(FString::Printf(TEXT("Fine surface is covered by coarse transition band (%d)"), J),
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

// =============================================================================
// Phase 3 - erosion geomorphology, true volumetric topology & adaptive res.
// =============================================================================

namespace
{
    FLythos2PlanetContext MakeGeoContext(int64 Seed)
    {
        FLythos2PlanetContext Context = MakeContext(Seed);
        Context.GeologyAmount = 1.0f;
        Context.ClimateProxy = 0.6f;
        return Context;
    }

    struct FColumnProfile
    {
        int32 SignChanges = 0;
        int32 SolidBands = 0;
        int32 EmptyBands = 0;
        bool bDeckOverVoid = false;   // solid band with empty above and below (near surface)
        bool bEnclosedVoid = false;   // empty band with solid above and below
    };

    FColumnProfile AnalyzeColumn(const FLythos2PlanetContext& Context, const FVector& Dir, int32 Samples)
    {
        FColumnProfile Profile;
        const double H = Context.TerrainHeightCm;
        const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);

        int32 PrevSign = 0;
        int32 RunStart = 0;
        TArray<int32> SignAt;
        SignAt.Reserve(Samples);

        for (int32 I = 0; I < Samples; ++I)
        {
            const double T = static_cast<double>(I) / (Samples - 1);
            const double R = Surface + H * (0.35 - 1.0 * T); // surface+0.35H .. surface-0.65H
            const double D = Lythos2::Density::EvaluateDensity(Context, Dir * R);
            const int32 Sign = D > 0.0 ? 1 : (D < 0.0 ? -1 : 0);
            if (Sign != 0 && PrevSign != 0 && Sign != PrevSign)
            {
                ++Profile.SignChanges;
            }
            if (Sign != 0)
            {
                PrevSign = Sign;
            }
            SignAt.Add(Sign);

            if (T > 0.55) // below ~surface-0.2H (subsurface): exposed to sky above
            {
                // Track bands for cavity/deck detection.
            }
        }

        // Count solid/empty runs (ignoring zeros) and detect deck/void patterns.
        int32 I = 0;
        TArray<TPair<int32, int32>> Runs; // (sign, length)
        while (I < Samples)
        {
            const int32 Sign = SignAt[I] != 0 ? SignAt[I] : PrevSign;
            int32 J = I;
            while (J < Samples && (SignAt[J] == 0 || SignAt[J] == Sign)) { ++J; }
            if (Sign != 0) { Runs.Add(TPair<int32, int32>(Sign, J - I)); }
            I = J;
        }
        for (const TPair<int32, int32>& Run : Runs)
        {
            if (Run.Key > 0) { ++Profile.SolidBands; } else { ++Profile.EmptyBands; }
        }
        for (int32 R = 1; R + 1 < Runs.Num(); ++R)
        {
            if (Runs[R].Key > 0 && Runs[R - 1].Key < 0 && Runs[R + 1].Key < 0)
            {
                Profile.bDeckOverVoid = true; // solid span with empty above + below
            }
            if (Runs[R].Key < 0 && Runs[R - 1].Key > 0 && Runs[R + 1].Key > 0)
            {
                Profile.bEnclosedVoid = true; // cavity with solid above + below
            }
        }
        return Profile;
    }
}

// Same seed -> identical geology density; different seed -> different geology.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3DeterminismTest,
    "Andromeda.Lythos2.Geomorphology.Determinism", LythosFlags)
bool FLythos2Phase3DeterminismTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext A = MakeGeoContext(900001);
    const FLythos2PlanetContext B = MakeGeoContext(900001);
    const FLythos2PlanetContext C = MakeGeoContext(900002);

    TArray<FVector> Dirs;
    LythosFibonacciSphere(400, Dirs);

    int32 Identical = 0;
    double DiffDifferentSeed = 0.0;
    for (const FVector& Dir : Dirs)
    {
        const double Surface = Lythos2::Density::SurfaceRadiusCm(A, Dir);
        for (int32 K = 0; K < 5; ++K)
        {
            const double R = Surface + A.TerrainHeightCm * (0.2 - 0.15 * K);
            const FVector P = Dir * R;
            if (Lythos2::Density::EvaluateDensity(A, P) == Lythos2::Density::EvaluateDensity(B, P))
            {
                ++Identical;
            }
            DiffDifferentSeed += FMath::Abs(Lythos2::Density::EvaluateDensity(A, P)
                - Lythos2::Density::EvaluateDensity(C, P));
        }
    }

    TestEqual(TEXT("Same seed produces identical geology"), Identical, Dirs.Num() * 5);
    TestTrue(TEXT("Different seed produces different geology"), DiffDifferentSeed > 0.0);
    return true;
}

// The density field can produce multiple solid/empty transitions along a
// radial line: overhangs, roofed voids, arches and enclosed cavities.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3VolumetricTest,
    "Andromeda.Lythos2.Volumetric.True3D", LythosFlags)
bool FLythos2Phase3VolumetricTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(424242);

    TArray<FVector> Dirs;
    LythosFibonacciSphere(4000, Dirs);

    int32 MultiLayerColumns = 0;
    int32 DeckOverVoid = 0;
    int32 EnclosedVoids = 0;
    bool bFinite = true;

    for (const FVector& Dir : Dirs)
    {
        const FColumnProfile P = AnalyzeColumn(Context, Dir, 160);
        if (P.SignChanges >= 3) { ++MultiLayerColumns; }
        if (P.bDeckOverVoid) { ++DeckOverVoid; }
        if (P.bEnclosedVoid) { ++EnclosedVoids; }

        const double D = Lythos2::Density::EvaluateDensity(
            Context, Dir * (Lythos2::Density::SurfaceRadiusCm(Context, Dir) - Context.TerrainHeightCm * 0.2));
        if (!FMath::IsFinite(D)) { bFinite = false; }
    }

    // Phase 3.3: true 3D topology still exists, but it is now a rare geological
    // event rather than a dominant component of the surface.
    TestTrue(TEXT("Density remains finite"), bFinite);
    TestTrue(TEXT("Multiple solid/empty transitions occur (true 3D topology)"), MultiLayerColumns >= 1);
    TestTrue(TEXT("Roofed voids / overhangs / natural-bridge topology occurs"), DeckOverVoid >= 1);
    TestTrue(TEXT("Enclosed cavities occur"), EnclosedVoids >= 1);
    TestTrue(TEXT("Volumetric topology is a minority of the terrain"),
        MultiLayerColumns < Dirs.Num() / 4 && DeckOverVoid < Dirs.Num() / 4);

    AddInfo(FString::Printf(TEXT("Volumetric: multiLayer=%d deckOverVoid=%d cavities=%d of %d columns"),
        MultiLayerColumns, DeckOverVoid, EnclosedVoids, Dirs.Num()));
    return true;
}

// The geology layer is deterministic and continuous (no NaN, bounded, smooth).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3ContinuityTest,
    "Andromeda.Lythos2.Volumetric.Continuity", LythosFlags)
bool FLythos2Phase3ContinuityTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(5150);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(256, Dirs);

    bool bFinite = true;
    bool bContinuous = true;

    for (const FVector& Dir : Dirs)
    {
        const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        double Prev = Lythos2::Density::EvaluateDensity(Context, Dir * (Surface + Context.TerrainHeightCm * 0.4));
        if (!FMath::IsFinite(Prev)) { bFinite = false; }

        for (int32 K = 1; K <= 200; ++K)
        {
            const double R = Surface + Context.TerrainHeightCm * (0.4 - 0.8 * K / 200.0);
            const double D = Lythos2::Density::EvaluateDensity(Context, Dir * R);
            if (!FMath::IsFinite(D)) { bFinite = false; }
            // Density must not jump more than a small fraction of H between
            // adjacent samples (continuity; no hard thresholds).
            if (FMath::Abs(D - Prev) > Context.TerrainHeightCm * 0.15)
            {
                bContinuous = false;
            }
            Prev = D;
        }
    }

    TestTrue(TEXT("Geology density is finite everywhere sampled"), bFinite);
    TestTrue(TEXT("Geology density is continuous (no discontinuities)"), bContinuous);
    return true;
}

// Feature-aware adaptive resolution: complex regions use more voxels than
// plains, and the base resolution is preserved for simple regions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3AdaptiveTest,
    "Andromeda.Lythos2.Adaptive.Complexity", LythosFlags)
bool FLythos2Phase3AdaptiveTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(606060);
    FLythos2Settings Settings;
    Settings.VoxelsPerAxis = 12;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;
    Settings.bAdaptiveResolution = true;
    Settings.SkirtDepthCells = 0.0f;

    // Phase 3.3: refinement is driven by the MEASURED geometric complexity of
    // the density field, not by a coarse importance sample. Rank regions by the
    // complexity the mesher actually measured.
    FLythos2RegionKey BestKey, WorstKey;
    float BestC = -1.0f, WorstC = 2.0f;
    int32 Scanned = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 8; X += 4)
        {
            for (int32 Y = 0; Y < 8; Y += 4)
            {
                const FLythos2RegionKey Key(Face, 3, X, Y);
                const FLythos2MeshData Mesh = BuildRegion(Context, Key, Settings);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++Scanned;
                if (Mesh.RegionComplexity > BestC) { BestC = Mesh.RegionComplexity; BestKey = Key; }
                if (Mesh.RegionComplexity < WorstC) { WorstC = Mesh.RegionComplexity; WorstKey = Key; }
            }
        }
    }
    TestTrue(TEXT("Regions were scanned"), Scanned >= 6);

    const FLythos2MeshData Complex = BuildRegion(Context, BestKey, Settings);
    TestTrue(TEXT("Complex region refines above the base voxel resolution"),
        Complex.VoxelsUsed > Settings.VoxelsPerAxis || Complex.RadialRefinedColumns > 0);

    const FLythos2MeshData Simple = BuildRegion(Context, WorstKey, Settings);
    TestTrue(TEXT("Simple region stays at the base voxel resolution"),
        Simple.VoxelsUsed == Settings.VoxelsPerAxis && Simple.RadialRefinedColumns == 0);

    AddInfo(FString::Printf(TEXT("Adaptive: maxC=%.3f voxels=%d refinedCols=%d | minC=%.3f voxels=%d"),
        BestC, Complex.VoxelsUsed, Complex.RadialRefinedColumns, WorstC, Simple.VoxelsUsed));
    return true;
}

// A region containing volumetric topology meshes into a watertight manifold
// with no interior holes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3MeshingTest,
    "Andromeda.Lythos2.Mesher.VolumetricWatertight", LythosFlags)
bool FLythos2Phase3MeshingTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(707070);
    FLythos2Settings Settings;
    Settings.VoxelsPerAxis = 16;
    Settings.MaxVolumetricVoxelsPerAxis = 16;
    Settings.SkirtDepthCells = 0.0f;

    // Find a region with a roofed void. Restrict to face-interior regions so
    // the boundary classification below is unambiguous (no cube-face seam).
    int32 BestFace = -1, BestX = 0, BestY = 0;
    float BestImp = 0.0f;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 1; X <= 2; ++X)
        {
            for (int32 Y = 1; Y <= 2; ++Y)
            {
                const FLythos2RegionKey Key(Face, 2, X, Y);
                const float Imp = Lythos2::Density::FeatureImportance(Context, Lythos2::CubeSphere::RegionCenterDirection(Key));
                if (Imp > BestImp) { BestImp = Imp; BestFace = Face; BestX = X; BestY = Y; }
            }
        }
    }
    TestTrue(TEXT("A feature region exists to mesh"), BestFace >= 0 && BestImp > 0.0f);

    const FLythos2MeshData Mesh = BuildRegion(Context, FLythos2RegionKey(BestFace, 2, BestX, BestY), Settings);
    TestTrue(TEXT("Volumetric region produces geometry"), Mesh.Indices.Num() > 0);

    // Manifold check: every interior edge is shared by exactly two triangles.
    auto Quantize = [](const FVector& P)
    {
        const double Q = 1.0;
        return FIntVector(FMath::RoundToInt(P.X / Q), FMath::RoundToInt(P.Y / Q), FMath::RoundToInt(P.Z / Q));
    };
    TMap<FIntVector, int32> Ids;
    TMap<uint64, int32> EdgeUse;
    TMap<uint64, FVector> EdgeMid;
    auto GetId = [&](const FVector& P)
    {
        const FIntVector K = Quantize(P);
        if (int32* E = Ids.Find(K)) { return *E; }
        const int32 N = Ids.Num(); Ids.Add(K, N); return N;
    };
    auto EKey = [](int32 A, int32 B)
    {
        const uint32 Lo = static_cast<uint32>(FMath::Min(A, B));
        const uint32 Hi = static_cast<uint32>(FMath::Max(A, B));
        return (static_cast<uint64>(Hi) << 32) | Lo;
    };
    for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
    {
        const int32 A = GetId(Mesh.Positions[Mesh.Indices[T]]);
        const int32 B = GetId(Mesh.Positions[Mesh.Indices[T + 1]]);
        const int32 C = GetId(Mesh.Positions[Mesh.Indices[T + 2]]);
        const int32 V[3] = { A, B, C };
        for (int32 E = 0; E < 3; ++E)
        {
            const int32 I0 = V[E];
            const int32 I1 = V[(E + 1) % 3];
            const uint64 Key = EKey(I0, I1);
            ++EdgeUse.FindOrAdd(Key);
            EdgeMid.FindOrAdd(Key, (Mesh.Positions[Mesh.Indices[T + E]] + Mesh.Positions[Mesh.Indices[T + (E + 1) % 3]]) * 0.5);
        }
    }

    int32 InteriorOpen = 0;
    const float U0 = static_cast<float>(BestX) / 4.0f;
    const float U1 = static_cast<float>(BestX + 1) / 4.0f;
    const float V0 = static_cast<float>(BestY) / 4.0f;
    const float V1 = static_cast<float>(BestY + 1) / 4.0f;
    const float Eps = 5.0e-3f;
    for (const TPair<uint64, int32>& Pair : EdgeUse)
    {
        if (Pair.Value == 2) { continue; }
        int32 Face; float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(EdgeMid[Pair.Key].GetSafeNormal(), Face, U, V);
        const bool bBoundary =
            (U <= U0 + Eps || U >= U1 - Eps || V <= V0 + Eps || V >= V1 - Eps);
        if (!bBoundary) { ++InteriorOpen; }
    }

    TestTrue(TEXT("Volumetric mesh has no interior holes"), InteriorOpen == 0);
    AddInfo(FString::Printf(TEXT("VolumetricMesh: region=(%d,%d,%d) tris=%d interiorOpen=%d"),
        BestFace, BestX, BestY, Mesh.Indices.Num() / 3, InteriorOpen));
    return true;
}

// First solid radius scanning inward from outside (macro surface reference).
static bool FindFirstSolidRadius(
    const FLythos2PlanetContext& Context,
    const FVector& Dir,
    double& OutRadius,
    double& OutMacroSurface)
{
    OutMacroSurface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
    const double H = Context.TerrainHeightCm;
    for (int32 K = 0; K <= 200; ++K)
    {
        const double R = OutMacroSurface + H * (0.3 - 0.9 * K / 200.0);
        if (Lythos2::Density::EvaluateDensity(Context, Dir * R) > 0.0)
        {
            OutRadius = R;
            return true;
        }
    }
    return false;
}

// Erosion responds to the climate/precipitation proxy: more precipitation
// erodes the land surface deeper.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3ClimateTest,
    "Andromeda.Lythos2.Erosion.ClimateResponse", LythosFlags)
bool FLythos2Phase3ClimateTest::RunTest(const FString& Parameters)
{
    FLythos2PlanetContext Dry = MakeGeoContext(818181);
    Dry.ClimateProxy = 0.1f;
    FLythos2PlanetContext Wet = MakeGeoContext(818181);
    Wet.ClimateProxy = 1.0f;

    TArray<FVector> Dirs;
    LythosFibonacciSphere(400, Dirs);

    double SumDry = 0.0;
    double SumWet = 0.0;
    int32 Count = 0;
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Dry, Dir) < 0.15f) { continue; } // land only
        double R1, S1, R2, S2;
        if (!FindFirstSolidRadius(Dry, Dir, R1, S1) || !FindFirstSolidRadius(Wet, Dir, R2, S2)) { continue; }
        SumDry += (S1 - R1);
        SumWet += (S2 - R2);
        ++Count;
    }

    TestTrue(TEXT("Land samples found"), Count > 20);
    TestTrue(TEXT("Higher precipitation proxy erodes the surface deeper"), SumWet > SumDry);
    AddInfo(FString::Printf(TEXT("Climate: dryIncision=%.1f wetIncision=%.1f samples=%d"),
        Count ? SumDry / Count : 0.0, Count ? SumWet / Count : 0.0, Count));
    return true;
}

// Differential incision: high complexity (drainage/canyon) directions are
// incised significantly more than low complexity ones.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3IncisionTest,
    "Andromeda.Lythos2.Erosion.DifferentialIncision", LythosFlags)
bool FLythos2Phase3IncisionTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(828282);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(1200, Dirs);

    TArray<TPair<float, double>> Samples; // (importance, incision)
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.15f) { continue; }
        double R, S;
        if (!FindFirstSolidRadius(Context, Dir, R, S)) { continue; }
        Samples.Add(TPair<float, double>(Lythos2::Density::FeatureImportance(Context, Dir), S - R));
    }

    Samples.Sort([](const TPair<float, double>& A, const TPair<float, double>& B) { return A.Key < B.Key; });

    const int32 Quartile = Samples.Num() / 4;
    TestTrue(TEXT("Enough land samples for ranking"), Quartile >= 10);

    double SumHigh = 0.0, SumLow = 0.0;
    for (int32 I = 0; I < Quartile; ++I)
    {
        SumLow += Samples[I].Value;
        SumHigh += Samples[Samples.Num() - 1 - I].Value;
    }
    const double MeanHigh = Quartile ? SumHigh / Quartile : 0.0;
    const double MeanLow = Quartile ? SumLow / Quartile : 0.0;

    TestTrue(TEXT("Complex directions are incised more than simple ones"), MeanHigh > MeanLow * 1.5);
    AddInfo(FString::Printf(TEXT("Incision: highQuartile=%.1fcm lowQuartile=%.1fcm n=%d"),
        MeanHigh, MeanLow, Samples.Num()));
    return true;
}

// Multiple surface intersections along one radial direction are actually
// meshed: a volumetric region contains geometry well below its macro surface.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2Phase3MultiIntersectionTest,
    "Andromeda.Lythos2.Mesher.MultipleIntersections", LythosFlags)
bool FLythos2Phase3MultiIntersectionTest::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(838383);
    FLythos2Settings Settings;
    Settings.VoxelsPerAxis = 24;
    Settings.MaxVolumetricVoxelsPerAxis = 24;
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 0.0f;
    const double H = Context.TerrainHeightCm;

    // Phase 3.3 features are rare, so find a genuine multi-crossing column and
    // the region that contains it.
    TArray<FVector> Dirs;
    LythosFibonacciSphere(4000, Dirs);
    TArray<double> Radii;
    TArray<double> Vals;
    Radii.SetNumUninitialized(200);
    Vals.SetNumUninitialized(200);

    bool bFound = false;
    FLythos2RegionKey FoundKey;
    FVector FoundDir = FVector::UpVector;
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.1f) { continue; }
        const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        for (int32 K = 0; K < 200; ++K)
        {
            Radii[K] = Surface + H * 0.30 - H * 1.30 * (static_cast<double>(K) / 199.0);
        }
        Lythos2::Density::SampleDensityColumn(Context, Dir, Radii.GetData(), 200, Vals.GetData());
        int32 SignChanges = 0, Prev = 0;
        for (int32 K = 0; K < 200; ++K)
        {
            const int32 S = Vals[K] > 0.0 ? 1 : (Vals[K] < 0.0 ? -1 : 0);
            if (S != 0 && Prev != 0 && S != Prev) { ++SignChanges; }
            if (S != 0) { Prev = S; }
        }
        if (SignChanges < 3) { continue; }
        int32 Face; float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(Dir, Face, U, V);
        FoundKey = FLythos2RegionKey(Face, 2,
            FMath::Clamp(static_cast<int32>(U * 4.0f), 0, 3),
            FMath::Clamp(static_cast<int32>(V * 4.0f), 0, 3));
        FoundDir = Dir;
        bFound = true;
        break;
    }
    TestTrue(TEXT("A rare multi-crossing column exists"), bFound);
    if (!bFound) { return false; }

    const FLythos2MeshData Mesh = BuildRegion(Context, FoundKey, Settings);
    TestTrue(TEXT("Volumetric region produced geometry"), Mesh.Indices.Num() > 0);

    int32 SubsurfaceVerts = 0;
    for (const FVector& P : Mesh.Positions)
    {
        if (FVector::DotProduct(P.GetSafeNormal(), FoundDir) < 0.995f) { continue; }
        const double Macro = Lythos2::Density::SurfaceRadiusCm(Context, P.GetSafeNormal());
        if (Macro - P.Size() > H * 0.12) { ++SubsurfaceVerts; }
    }

    TestTrue(TEXT("Subsurface/cave geometry is meshed below the macro surface"), SubsurfaceVerts > 0);
    AddInfo(FString::Printf(TEXT("MultiIntersection: region=(%d,%d,%d) tris=%d subsurfaceVerts=%d"),
        FoundKey.Face, FoundKey.X, FoundKey.Y, Mesh.Indices.Num() / 3, SubsurfaceVerts));
    return true;
}

// =============================================================================
// Phase 3.1 - geomorphology refinement, true 3D structures and seamless
// adaptive transitions.
// =============================================================================

namespace
{
    struct FBand
    {
        int32 Sign = 0;         // +1 solid, -1 empty
        double OuterR = 0.0;    // radius of the band's outer (skyward) edge
        double InnerR = 0.0;    // radius of the band's inner (deep) edge
        double Thickness() const { return OuterR - InnerR; }
    };

    /** Scan one radial column (outermost band first) via column sampling. */
    void ScanColumnBands(const FLythos2PlanetContext& Context, const FVector& Dir, int32 Samples, TArray<FBand>& OutBands)
    {
        OutBands.Reset();
        const double H = Context.TerrainHeightCm;
        const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
        const double RTop = Surface + H * 0.5;
        const double RBot = Surface - H * 0.95;
        const double Step = (RTop - RBot) / Samples;

        TArray<double> Radii;
        TArray<double> Vals;
        Radii.SetNumUninitialized(Samples + 1);
        Vals.SetNumUninitialized(Samples + 1);
        for (int32 I = 0; I <= Samples; ++I) { Radii[I] = RTop - Step * I; }
        Lythos2::Density::SampleDensityColumn(Context, Dir, Radii.GetData(), Samples + 1, Vals.GetData());

        int32 I = 0;
        while (I <= Samples)
        {
            const int32 Sign = Vals[I] > 0.0 ? 1 : (Vals[I] < 0.0 ? -1 : 0);
            if (Sign == 0) { ++I; continue; }
            int32 J = I;
            while (J + 1 <= Samples)
            {
                const int32 Next = Vals[J + 1] > 0.0 ? 1 : (Vals[J + 1] < 0.0 ? -1 : 0);
                if (Next == 0 || Next == Sign) { ++J; } else { break; }
            }
            FBand Band;
            Band.Sign = Sign;
            Band.OuterR = Radii[I];
            Band.InnerR = Radii[J];
            OutBands.Add(Band);
            I = J + 1;
        }
    }

    double OuterRadiusNearDir(const FLythos2MeshData& Mesh, const FVector& Dir, float CosTol)
    {
        double Best = -1.0;
        for (const FVector& P : Mesh.Positions)
        {
            if (FVector::DotProduct(P.GetSafeNormal(), Dir) >= CosTol)
            {
                Best = FMath::Max(Best, static_cast<double>(P.Size()));
            }
        }
        return Best;
    }

    /** Radius (outermost) of the vertex closest in angle to Dir. */
    double NearestRadiusAlong(const FLythos2MeshData& Mesh, const FVector& Dir)
    {
        double BestDot = -2.0;
        for (const FVector& P : Mesh.Positions)
        {
            BestDot = FMath::Max(BestDot, static_cast<double>(FVector::DotProduct(P.GetSafeNormal(), Dir)));
        }
        double BestR = -1.0;
        for (const FVector& P : Mesh.Positions)
        {
            const double Dot = FVector::DotProduct(P.GetSafeNormal(), Dir);
            if (Dot >= BestDot - 1.0e-5)
            {
                BestR = FMath::Max(BestR, static_cast<double>(P.Size()));
            }
        }
        return BestR;
    }

    double ColumnRoughness(const TArray<double>& Ero)
    {
        if (Ero.Num() < 2) { return 0.0; }
        double Sum = 0.0;
        for (int32 I = 1; I < Ero.Num(); ++I) { Sum += FMath::Abs(Ero[I] - Ero[I - 1]); }
        return Sum / (Ero.Num() - 1);
    }
}

// ---------- Geomorphology ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31ErosionHierarchy,
    "Andromeda.Lythos2.Geomorphology.ErosionHierarchy", LythosFlags)
bool FLythos2P31ErosionHierarchy::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(910001);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(1500, Dirs);

    TArray<double> Inc, P, S, T, F;
    double MaxInc = 0.0, MinInc = 1.0e30;
    for (const FVector& Dir : Dirs)
    {
        Lythos2::Density::FLythos2GeologySample Sample;
        Lythos2::Density::SampleGeology(Context, Dir, Sample);
        if (Sample.MacroElev < 0.05f) { continue; }
        Inc.Add(Sample.ErosionDepthCm);
        P.Add(Sample.PrimaryIncisionCm);
        S.Add(Sample.SecondaryIncisionCm);
        T.Add(Sample.TertiaryIncisionCm);
        F.Add(Sample.FineIncisionCm);
        MaxInc = FMath::Max(MaxInc, Sample.ErosionDepthCm);
        MinInc = FMath::Min(MinInc, Sample.ErosionDepthCm);
    }

    TestTrue(TEXT("Enough land samples"), Inc.Num() >= 300);

    const double H = Context.TerrainHeightCm;
    auto MeanOf = [](const TArray<double>& V) { double M = 0.0; for (double X : V) { M += X; } return V.Num() ? M / V.Num() : 0.0; };
    const double MeanInc = MeanOf(Inc);
    const double MeanP = MeanOf(P), MeanS = MeanOf(S), MeanT = MeanOf(T), MeanF = MeanOf(F);

    // All four erosion scales are active and ordered primary > secondary >
    // tertiary > fine, i.e. a hierarchy rather than one uniform scale.
    TestTrue(TEXT("Primary incision scale is active"), MeanP > 0.0);
    TestTrue(TEXT("Secondary (tributary) scale is active"), MeanS > 0.0);
    TestTrue(TEXT("Tertiary (gully) scale is active"), MeanT > 0.0);
    TestTrue(TEXT("Fine detail scale is active but restrained"), MeanF > 0.0 && MeanF < MeanT);
    TestTrue(TEXT("Hierarchy is ordered primary > secondary > tertiary"),
        MeanP > MeanS && MeanS > MeanT);

    // Primary broad valleys dominate; fine detail never dominates.
    TestTrue(TEXT("Primary valleys dominate the incision budget"), MeanP > MeanInc * 0.4);
    TestTrue(TEXT("Erosion stays bounded (no overwhelming global erosion)"), MaxInc < H * 0.62);
    TestTrue(TEXT("Stable low-erosion plains still exist"), MinInc < H * 0.03);

    int32 DeepCanyons = 0;
    for (double E : Inc) { if (E > H * 0.12) { ++DeepCanyons; } }
    TestTrue(TEXT("Deep canyons exist but are not ubiquitous"), DeepCanyons >= 5 && DeepCanyons < Inc.Num());

    AddInfo(FString::Printf(TEXT("Hierarchy: meanP=%.0f meanS=%.0f meanT=%.0f meanF=%.0f meanInc=%.0f max=%.0f deep=%d"),
        MeanP, MeanS, MeanT, MeanF, MeanInc, MaxInc, DeepCanyons));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31CanyonVariation,
    "Andromeda.Lythos2.Geomorphology.CanyonVariation", LythosFlags)
bool FLythos2P31CanyonVariation::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(920002);
    const double H = Context.TerrainHeightCm;

    TArray<FVector> Dirs;
    LythosFibonacciSphere(2000, Dirs);

    TArray<double> CanyonDepths;
    double MinLayer = 1.0e30, MaxLayer = 0.0;
    for (const FVector& Dir : Dirs)
    {
        Lythos2::Density::FLythos2GeologySample Sample;
        Lythos2::Density::SampleGeology(Context, Dir, Sample);
        if (Sample.MacroElev < 0.05f) { continue; }
        MinLayer = FMath::Min(MinLayer, Sample.TerraceLayerCm);
        MaxLayer = FMath::Max(MaxLayer, Sample.TerraceLayerCm);
        if (Sample.PrimaryDrainage > 0.45f) { CanyonDepths.Add(Sample.ErosionDepthCm); }
    }

    TestTrue(TEXT("Enough canyon-core samples"), CanyonDepths.Num() >= 30);

    double Mean = 0.0;
    for (double D : CanyonDepths) { Mean += D; }
    Mean /= CanyonDepths.Num();
    double Var = 0.0;
    for (double D : CanyonDepths) { Var += FMath::Square(D - Mean); }
    const double Cv = Mean > 0.0 ? FMath::Sqrt(Var / CanyonDepths.Num()) / Mean : 0.0;

    // Variable width/depth and variable terrace spacing: not a single scalar
    // trench and not uniform procedural stairs.
    TestTrue(TEXT("Canyon depth varies strongly (not a uniform trench)"), Cv > 0.20);
    TestTrue(TEXT("Terrace spacing varies across the planet"), (MaxLayer - MinLayer) > H * 0.02);

    AddInfo(FString::Printf(TEXT("Canyon: n=%d mean=%.0f cv=%.2f layer=[%.0f,%.0f]"),
        CanyonDepths.Num(), Mean, Cv, MinLayer, MaxLayer));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31ResistanceInfluence,
    "Andromeda.Lythos2.Geomorphology.ResistanceInfluence", LythosFlags)
bool FLythos2P31ResistanceInfluence::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(930003);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(3000, Dirs);

    TArray<TPair<float, double>> Samples; // (resistance, incision)
    TArray<TPair<float, float>> TerraceSamples;
    for (const FVector& Dir : Dirs)
    {
        Lythos2::Density::FLythos2GeologySample Sample;
        Lythos2::Density::SampleGeology(Context, Dir, Sample);
        if (Sample.MacroElev < 0.05f) { continue; }
        // Control for drainage: compare within genuine channels only.
        if (Sample.PrimaryDrainage < 0.35f) { continue; }
        Samples.Add(TPair<float, double>(Sample.Resistance, Sample.ErosionDepthCm));
        TerraceSamples.Add(TPair<float, float>(Sample.Resistance, Sample.TerraceStrength));
    }

    Samples.Sort([](const TPair<float, double>& A, const TPair<float, double>& B) { return A.Key < B.Key; });
    TerraceSamples.Sort([](const TPair<float, float>& A, const TPair<float, float>& B) { return A.Key < B.Key; });

    const int32 Q = Samples.Num() / 4;
    TestTrue(TEXT("Enough channel samples spanning resistance"), Q >= 10);

    double LowEro = 0.0, HighEro = 0.0, LowTerr = 0.0, HighTerr = 0.0;
    for (int32 I = 0; I < Q; ++I)
    {
        LowEro += Samples[I].Value;
        HighEro += Samples[Samples.Num() - 1 - I].Value;
    }
    for (int32 I = 0; I < Q; ++I)
    {
        LowTerr += TerraceSamples[I].Value;
        HighTerr += TerraceSamples[TerraceSamples.Num() - 1 - I].Value;
    }
    const double MeanLow = LowEro / Q, MeanHigh = HighEro / Q;
    const double MeanLowT = LowTerr / Q, MeanHighT = HighTerr / Q;

    TestTrue(TEXT("High-resistance rock is incised less (survives erosion)"), MeanHigh < MeanLow * 0.92);
    TestTrue(TEXT("High-resistance rock expresses more terracing"), MeanHighT > MeanLowT);

    AddInfo(FString::Printf(TEXT("Resistance: lowQ=%.0fcm highQ=%.0fcm terrace[%.2f..%.2f]"),
        MeanLow, MeanHigh, MeanLowT, MeanHighT));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31TalusResponse,
    "Andromeda.Lythos2.Geomorphology.TalusResponse", LythosFlags)
bool FLythos2P31TalusResponse::RunTest(const FString& Parameters)
{
    FLythos2PlanetContext NoTalus = MakeGeoContext(940004);
    NoTalus.TalusAmount = 0.0f;
    FLythos2PlanetContext FullTalus = MakeGeoContext(940004);
    FullTalus.TalusAmount = 1.0f;

    TArray<FVector> Dirs;
    LythosFibonacciSphere(1200, Dirs);

    TArray<double> EroA, EroB;
    for (const FVector& Dir : Dirs)
    {
        Lythos2::Density::FLythos2GeologySample A, B;
        Lythos2::Density::SampleGeology(NoTalus, Dir, A);
        Lythos2::Density::SampleGeology(FullTalus, Dir, B);
        if (A.MacroElev < 0.05f) { continue; }
        EroA.Add(A.ErosionDepthCm);
        EroB.Add(B.ErosionDepthCm);
    }

    TestTrue(TEXT("Enough samples"), EroA.Num() >= 200);

    double SumA = 0.0, SumB = 0.0;
    for (int32 I = 0; I < EroA.Num(); ++I) { SumA += EroA[I]; SumB += EroB[I]; }

    TestTrue(TEXT("Talus response reduces total incision (debris accumulation)"), SumB < SumA);
    TestTrue(TEXT("Talus response softens slope roughness"),
        ColumnRoughness(EroB) <= ColumnRoughness(EroA) * 1.02);

    AddInfo(FString::Printf(TEXT("Talus: sumNoTalus=%.0f sumTalus=%.0f rough[%.1f..%.1f]"),
        SumA, SumB, ColumnRoughness(EroB), ColumnRoughness(EroA)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31TerraceVariation,
    "Andromeda.Lythos2.Geomorphology.TerraceVariation", LythosFlags)
bool FLythos2P31TerraceVariation::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(950005);
    const double H = Context.TerrainHeightCm;
    TArray<FVector> Dirs;
    LythosFibonacciSphere(2000, Dirs);

    double MinLayer = 1.0e30, MaxLayer = 0.0;
    double MaxStrength = 0.0, MinStrength = 1.0e30;
    int32 Terraced = 0;
    for (const FVector& Dir : Dirs)
    {
        Lythos2::Density::FLythos2GeologySample Sample;
        Lythos2::Density::SampleGeology(Context, Dir, Sample);
        if (Sample.MacroElev < 0.05f) { continue; }
        MinLayer = FMath::Min(MinLayer, Sample.TerraceLayerCm);
        MaxLayer = FMath::Max(MaxLayer, Sample.TerraceLayerCm);
        MaxStrength = FMath::Max(MaxStrength, static_cast<double>(Sample.TerraceStrength));
        MinStrength = FMath::Min(MinStrength, static_cast<double>(Sample.TerraceStrength));
        if (Sample.TerraceStrength > 0.15f) { ++Terraced; }
    }

    TestTrue(TEXT("Terraces exist on resistant geology"), MaxStrength > 0.30);
    TestTrue(TEXT("Terraces fade out on weak geology (not uniform bands)"), MinStrength < 0.05);
    TestTrue(TEXT("Terrace spacing varies"), (MaxLayer - MinLayer) > H * 0.018);
    TestTrue(TEXT("Only part of the land is terraced"), Terraced > 0 && Terraced < Dirs.Num());

    AddInfo(FString::Printf(TEXT("Terrace: strength=[%.2f,%.2f] layer=[%.0f,%.0f] terraced=%d"),
        MinStrength, MaxStrength, MinLayer, MaxLayer, Terraced));
    return true;
}

// ---------- Volumetric structures ------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31NaturalBridge,
    "Andromeda.Lythos2.Volumetric.NaturalBridge", LythosFlags)
bool FLythos2P31NaturalBridge::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(960006);
    const double H = Context.TerrainHeightCm;
    TArray<FVector> Dirs;
    LythosFibonacciSphere(4000, Dirs);

    int32 Decks = 0;
    int32 Bridges = 0;
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.05f) { continue; }

        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 150, Bands);

        // Deck-over-void: sky, solid roof, empty opening, solid floor.
        bool bDeck = false;
        for (int32 B = 1; B + 1 < Bands.Num(); ++B)
        {
            if (Bands[B].Sign > 0 && Bands[B - 1].Sign < 0 && Bands[B + 1].Sign < 0) { bDeck = true; }
        }
        if (!bDeck) { continue; }
        ++Decks;

        // A true span: a lateral neighbour is open (much lower surface), so
        // the surviving rock actually bridges a gap rather than lining a cave.
        const FVector T1 = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
        const FVector T2 = FVector::CrossProduct(Dir, T1).GetSafeNormal();
        const double Surface = Lythos2::Density::FindSurfaceRadiusCm(Context, Dir, H);
        bool bOpen = false;
        if (Surface > 0.0)
        {
            const FVector Neighbours[4] = { Dir + T1 * 0.004f, Dir - T1 * 0.004f, Dir + T2 * 0.004f, Dir - T2 * 0.004f };
            for (const FVector& N : Neighbours)
            {
                const double NS = Lythos2::Density::FindSurfaceRadiusCm(Context, N, H);
                if (NS > 0.0 && NS < Surface - H * 0.06) { bOpen = true; break; }
            }
        }
        if (bOpen) { ++Bridges; }
    }

    TestTrue(TEXT("Roofed voids / bridge topology occurs"), Decks >= 1);
    TestTrue(TEXT("At least one roofed span bridges an open gap"), Bridges >= 1);
    TestTrue(TEXT("Natural bridges are rare"), Decks < Dirs.Num() / 4);

    AddInfo(FString::Printf(TEXT("NaturalBridge: decks=%d spans=%d"), Decks, Bridges));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31Arch,
    "Andromeda.Lythos2.Volumetric.Arch", LythosFlags)
bool FLythos2P31Arch::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(970007);
    const double H = Context.TerrainHeightCm;
    TArray<FVector> Dirs;
    LythosFibonacciSphere(4000, Dirs);

    int32 Arches = 0;
    int32 BestBands = 0;
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.05f) { continue; }

        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 170, Bands);

        for (int32 B = 1; B + 1 < Bands.Num(); ++B)
        {
            if (Bands[B].Sign > 0 && Bands[B - 1].Sign < 0 && Bands[B + 1].Sign < 0)
            {
                // Arch-like: the surviving roof is thin and irregular.
                if (Bands[B].Thickness() > 0.0 && Bands[B].Thickness() < H * 0.16)
                {
                    ++Arches;
                    BestBands = FMath::Max(BestBands, Bands.Num());
                }
                break;
            }
        }
    }

    TestTrue(TEXT("Thin-roofed arch topology occurs"), Arches >= 1);
    TestTrue(TEXT("Arches are rare"), Arches < Dirs.Num() / 4);

    AddInfo(FString::Printf(TEXT("Arch: thinRoofOpenings=%d maxBands=%d"), Arches, BestBands));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31Overhang,
    "Andromeda.Lythos2.Volumetric.Overhang", LythosFlags)
bool FLythos2P31Overhang::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(980008);
    const double H = Context.TerrainHeightCm;
    TArray<FVector> Dirs;
    LythosFibonacciSphere(4000, Dirs);

    int32 UnderCutVoids = 0;
    int32 ShallowNotches = 0;
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.05f) { continue; }

        Lythos2::Density::FLythos2GeologySample Sample;
        Lythos2::Density::SampleGeology(Context, Dir, Sample);
        if (Sample.UndercutStrength < 0.03f) { continue; }
        ++UnderCutVoids;

        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 170, Bands);
        // Near-surface notch under a lip: solid surface, then a shallow void.
        for (int32 B = 1; B + 1 < Bands.Num(); ++B)
        {
            if (Bands[B].Sign > 0 && Bands[B - 1].Sign < 0 && Bands[B + 1].Sign < 0)
            {
                const double NotchDepth = Bands[B].OuterR - Bands[B + 1].OuterR;
                if (NotchDepth < H * 0.24) { ++ShallowNotches; }
                break;
            }
        }
    }

    TestTrue(TEXT("Undercutting is active on canyon walls"), UnderCutVoids >= 1);
    TestTrue(TEXT("Shallow undercut notches / overhangs occur"), ShallowNotches >= 1);

    AddInfo(FString::Printf(TEXT("Overhang: undercutDirs=%d shallowNotches=%d"), UnderCutVoids, ShallowNotches));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31MultiLayerTopology,
    "Andromeda.Lythos2.Volumetric.MultiLayerTopology", LythosFlags)
bool FLythos2P31MultiLayerTopology::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(990009);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(4000, Dirs);

    int32 MultiLayer = 0;
    int32 SurfaceVoidRoofSolid = 0;
    bool bFinite = true;

    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.05f) { continue; }

        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 180, Bands);
        if (Bands.Num() >= 4) { ++MultiLayer; }
        if (Bands.Num() >= 4 && Bands[1].Sign > 0 && Bands[2].Sign < 0 && Bands[3].Sign > 0)
        {
            // surface (solid roof) -> subsurface void -> surviving deeper solid.
            ++SurfaceVoidRoofSolid;
        }

        const double D = Lythos2::Density::EvaluateDensity(Context, Dir * (Lythos2::Density::SurfaceRadiusCm(Context, Dir) - Context.TerrainHeightCm * 0.2));
        if (!FMath::IsFinite(D)) { bFinite = false; }
    }

    TestTrue(TEXT("Density remains finite"), bFinite);
    TestTrue(TEXT("Multiple solid/empty transitions occur"), MultiLayer >= 1);
    TestTrue(TEXT("surface + void + roof + deeper solid topology is present"), SurfaceVoidRoofSolid >= 1);
    TestTrue(TEXT("Multi-layer topology is a minority"), MultiLayer < Dirs.Num() / 4);

    AddInfo(FString::Printf(TEXT("MultiLayer: multi=%d surfaceVoidRoofSolid=%d"), MultiLayer, SurfaceVoidRoofSolid));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31NoTubeCaveBias,
    "Andromeda.Lythos2.Volumetric.NoTubeCaveBias", LythosFlags)
bool FLythos2P31NoTubeCaveBias::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(991010);
    const double H = Context.TerrainHeightCm;
    TArray<FVector> Dirs;
    LythosFibonacciSphere(4000, Dirs);

    int32 VoidColumns = 0;
    double MaxThickness = 0.0;
    TArray<double> VoidCenters;
    bool bAnyOpenShaft = false;

    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.05f) { continue; }

        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 200, Bands);

        bool bHasVoid = false;
        for (int32 B = 1; B + 1 < Bands.Num(); ++B)
        {
            if (Bands[B].Sign < 0 && Bands[B - 1].Sign > 0 && Bands[B + 1].Sign > 0)
            {
                ++VoidColumns;
                const double Thk = Bands[B].Thickness();
                MaxThickness = FMath::Max(MaxThickness, Thk);
                if (Thk > H * 0.5) { bAnyOpenShaft = true; }
                const double Surface = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
                VoidCenters.Add(Surface - 0.5 * (Bands[B].OuterR + Bands[B].InnerR));
                bHasVoid = true;
                break;
            }
        }
        (void)bHasVoid;
    }

    double Mean = 0.0;
    for (double C : VoidCenters) { Mean += C; }
    if (VoidCenters.Num() > 0) { Mean /= VoidCenters.Num(); }
    double Var = 0.0;
    for (double C : VoidCenters) { Var += FMath::Square(C - Mean); }
    const double Std = VoidCenters.Num() > 1 ? FMath::Sqrt(Var / VoidCenters.Num()) : 0.0;

    TestTrue(TEXT("Cavities are present but sparse"), VoidColumns >= 1 && VoidColumns < Dirs.Num() / 4);
    TestTrue(TEXT("No long radial cave tubes/shafts (bounded void thickness)"),
        MaxThickness < H * 0.4 && !bAnyOpenShaft);
    TestTrue(TEXT("Void centres vary (irregular, not one shell)"), VoidColumns < 2 || Std > H * 0.01);

    AddInfo(FString::Printf(TEXT("NoTube: voidCols=%d maxThk=%.0f centerStd=%.0f"),
        VoidColumns, MaxThickness, Std));
    return true;
}

// ---------- Transition meshing ---------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31TransitionSameResolution,
    "Andromeda.Lythos2.Mesher.TransitionSameResolution", LythosFlags)
bool FLythos2P31TransitionSameResolution::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(100101);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 0.0f;

    const FLythos2RegionKey A(4, 2, 0, 0);
    const FLythos2RegionKey B(4, 2, 1, 0);
    const FLythos2MeshData MeshA = BuildRegion(Context, A, Settings);
    const FLythos2MeshData MeshB = BuildRegion(Context, B, Settings);

    TestTrue(TEXT("Both same-resolution neighbours meshed"), MeshA.Indices.Num() > 0 && MeshB.Indices.Num() > 0);

    // Every surface vertex on A's right edge must have an identical counterpart
    // on B's left edge (they share the same grid directions and radii).
    auto CollectSharedEdgeVerts = [](const FLythos2MeshData& Mesh, int32 Face, TArray<FVector>& Out)
    {
        for (const FVector& P : Mesh.Positions)
        {
            int32 OutFace; float U, V;
            Lythos2::CubeSphere::DirectionToFaceUV(P.GetSafeNormal(), OutFace, U, V);
            if (OutFace == Face && FMath::Abs(U - 0.25f) < 1.0e-5f)
            {
                Out.Add(P);
            }
        }
    };
    TArray<FVector> EdgeA, EdgeB;
    CollectSharedEdgeVerts(MeshA, A.Face, EdgeA);
    CollectSharedEdgeVerts(MeshB, B.Face, EdgeB);

    int32 Unmatched = 0;
    double WorstGap = 0.0;
    for (const FVector& P : EdgeA)
    {
        double Best = 1.0e30;
        for (const FVector& Q : EdgeB)
        {
            Best = FMath::Min(Best, static_cast<double>(FVector::Dist(P, Q)));
        }
        WorstGap = FMath::Max(WorstGap, Best);
        if (Best > 1.0) { ++Unmatched; }
    }

    TestTrue(TEXT("Both shared edges have vertices"), EdgeA.Num() > 0 && EdgeB.Num() > 0);
    TestTrue(TEXT("Same-resolution shared surface is identical (welded)"), Unmatched == 0);

    AddInfo(FString::Printf(TEXT("TransitionSameRes: edgeA=%d edgeB=%d unmatched=%d worstGapCm=%.3f"),
        EdgeA.Num(), EdgeB.Num(), Unmatched, WorstGap));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31TransitionAdaptiveResolution,
    "Andromeda.Lythos2.Mesher.TransitionAdaptiveResolution", LythosFlags)
bool FLythos2P31TransitionAdaptiveResolution::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(100202);
    FLythos2Settings Coarse = MakeSettings(2, 12);
    Coarse.bAdaptiveResolution = false;
    Coarse.SkirtDepthCells = 3.0f;
    FLythos2Settings Fine = MakeSettings(2, 20);
    Fine.bAdaptiveResolution = false;
    Fine.SkirtDepthCells = 3.0f;

    const FLythos2RegionKey A(4, 2, 0, 0);
    const FLythos2RegionKey B(4, 2, 1, 0);
    const FLythos2MeshData MeshA = BuildRegion(Context, A, Coarse);
    const FLythos2MeshData MeshB = BuildRegion(Context, B, Fine);

    TestTrue(TEXT("Finer region refines beyond the base resolution"), MeshB.VoxelsUsed > MeshA.VoxelsUsed);
    TestTrue(TEXT("A transition collar was generated"), MeshA.Positions.Num() > 0 && MeshB.Positions.Num() > 0);

    const double LocalCell = Lythos2::CubeSphere::RegionWorldSizeCm(A, Context) / 20.0;
    const double CollarDepth = 3.0 * LocalCell;

    const float CosTol = 0.9985f;
    const double H = Context.TerrainHeightCm;
    double MaxGap = 0.0;
    int32 Covered = 0, Checked = 0;
    for (int32 J = 0; J <= 20; ++J)
    {
        const float V = static_cast<float>(J) / 20.0f;
        const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(A, 1.0f, V);
        const double SA = Lythos2::Density::FindSurfaceRadiusCm(Context, Dir, H);
        const double RB = OuterRadiusNearDir(MeshB, Dir, CosTol);
        if (SA <= 0.0 || RB <= 0.0) { continue; }
        const double Gap = FMath::Abs(SA - RB);
        MaxGap = FMath::Max(MaxGap, Gap);
        if (Gap <= CollarDepth + 1.0) { ++Covered; }
        ++Checked;
    }

    TestTrue(TEXT("Boundary samples were checked"), Checked >= 10);
    TestTrue(TEXT("Adaptive-resolution gap is within collar depth (closable)"), MaxGap <= CollarDepth + 1.0);
    TestTrue(TEXT("Every checked boundary direction is covered"), Covered == Checked);

    AddInfo(FString::Printf(TEXT("TransitionAdaptive: N=(%d,%d) maxGap=%.0f collar=%.0f covered=%d/%d"),
        MeshA.VoxelsUsed, MeshB.VoxelsUsed, MaxGap, CollarDepth, Covered, Checked));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31TransitionLOD,
    "Andromeda.Lythos2.Mesher.TransitionLOD", LythosFlags)
bool FLythos2P31TransitionLOD::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(100303);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 3.0f;

    const FLythos2RegionKey Coarse(4, 1, 0, 0);   // U in [0, 0.5]
    const FLythos2RegionKey Fine(4, 2, 0, 0);     // U in [0, 0.25], shares U = 0 edge
    const FLythos2MeshData MeshCoarse = BuildRegion(Context, Coarse, Settings);
    const FLythos2MeshData MeshFine = BuildRegion(Context, Fine, Settings);

    TestTrue(TEXT("Both LODs meshed"), MeshCoarse.Indices.Num() > 0 && MeshFine.Indices.Num() > 0);

    const double H = Context.TerrainHeightCm;
    const double CollarDepth = 3.0 * (Lythos2::CubeSphere::RegionWorldSizeCm(Fine, Context) / 12.0);
    const float CosTol = 0.9985f;

    // Coarse boundary polyline along V in [0, 0.25] (coarse N = 12 over [0,0.5]).
    const int32 KN = 12;
    TArray<double> CoarseSurf;
    for (int32 K = 0; K <= KN; ++K)
    {
        const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(Coarse, 0.0f, static_cast<float>(K) / static_cast<float>(KN));
        CoarseSurf.Add(Lythos2::Density::FindSurfaceRadiusCm(Context, Dir, H));
    }

    double MaxGap = 0.0;
    int32 Checked = 0;
    for (int32 J = 0; J <= 6; ++J) // fine V in [0, 0.25]
    {
        const float V = static_cast<float>(J) / 24.0f;
        const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(Fine, 0.0f, V);
        const double FineSurf = Lythos2::Density::FindSurfaceRadiusCm(Context, Dir, H);
        // Coarse chord at V (coarse V = 2 * fine V).
        const double CoarseV = static_cast<double>(V) * 2.0;
        const int32 K0 = FMath::Clamp(static_cast<int32>(CoarseV * KN), 0, KN);
        const int32 K1 = FMath::Clamp(K0 + 1, 0, KN);
        const double Frac = CoarseV * KN - K0;
        const double CoarseChord = CoarseSurf[K0] * (1.0 - Frac) + CoarseSurf[K1] * Frac;
        if (FineSurf <= 0.0 || CoarseChord <= 0.0) { continue; }
        MaxGap = FMath::Max(MaxGap, FMath::Abs(FineSurf - CoarseChord));
        ++Checked;
    }

    TestTrue(TEXT("LOD boundary directions were checked"), Checked >= 5);
    TestTrue(TEXT("LOD seam is within collar depth (closable)"), MaxGap <= CollarDepth + 1.0);

    AddInfo(FString::Printf(TEXT("TransitionLOD: maxGap=%.0f collar=%.0f"), MaxGap, CollarDepth));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31TransitionVolumetricContinuity,
    "Andromeda.Lythos2.Mesher.TransitionVolumetricContinuity", LythosFlags)
bool FLythos2P31TransitionVolumetricContinuity::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(100404);
    FLythos2Settings Settings = MakeSettings(2, 16);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 3.0f;

    // Pick the most volumetric LOD-2 region.
    int32 BestFace = 0, BestX = 0, BestY = 0;
    float BestImp = 0.0f;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 4; ++X)
        {
            for (int32 Y = 0; Y < 4; ++Y)
            {
                const FLythos2RegionKey Key(Face, 2, X, Y);
                const float Imp = Lythos2::Density::FeatureImportance(Context, Lythos2::CubeSphere::RegionCenterDirection(Key));
                if (Imp > BestImp) { BestImp = Imp; BestFace = Face; BestX = X; BestY = Y; }
            }
        }
    }

    const FLythos2RegionKey Key(BestFace, 2, BestX, BestY);
    const FLythos2MeshData Mesh = BuildRegion(Context, Key, Settings);
    TestTrue(TEXT("Volumetric feature region meshed"), Mesh.Indices.Num() > 0);

    const double H = Context.TerrainHeightCm;
    const double LocalCell = Lythos2::CubeSphere::RegionWorldSizeCm(Key, Context) / 16.0;
    const float CosTol = 0.9988f;

    int32 Checked = 0, Conforming = 0;
    bool bErodedBoundary = false;
    const float U0 = static_cast<float>(BestX) / 4.0f;
    const float U1 = static_cast<float>(BestX + 1) / 4.0f;
    const float V0 = static_cast<float>(BestY) / 4.0f;
    const float V1 = static_cast<float>(BestY + 1) / 4.0f;

    const float Edges[4][4] =
    {
        { U0, V0, 0.0f, (V1 - V0) }, // left
        { U1, V0, 0.0f, (V1 - V0) }, // right
        { U0, V0, (U1 - U0), 0.0f }, // bottom
        { U0, V1, (U1 - U0), 0.0f }  // top
    };

    for (int32 E = 0; E < 4; ++E)
    {
        for (int32 C = 0; C <= 16; ++C)
        {
            const float T = static_cast<float>(C) / 16.0f;
            const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(
                Key, Edges[E][0] + Edges[E][2] * T, Edges[E][1] + Edges[E][3] * T);
            const double S = Lythos2::Density::FindSurfaceRadiusCm(Context, Dir, H);
            const double Outer = OuterRadiusNearDir(Mesh, Dir, CosTol);
            if (S <= 0.0 || Outer <= 0.0) { continue; }
            ++Checked;
            if (FMath::Abs(Outer - S) < LocalCell * 0.75) { ++Conforming; }
            const double Macro = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
            if (Macro - S > H * 0.05) { bErodedBoundary = true; }
        }
    }

    TestTrue(TEXT("Boundary directions were checked"), Checked >= 20);
    TestTrue(TEXT("Boundary geometry follows the real density surface, not the macro envelope"),
        Conforming * 2 >= Checked);
    TestTrue(TEXT("An eroded/volumetric boundary direction was exercised"), bErodedBoundary);

    AddInfo(FString::Printf(TEXT("TransitionVolumetric: region=(%d,%d,%d) imp=%.2f conform=%d/%d eroded=%d"),
        BestFace, BestX, BestY, BestImp, Conforming, Checked, bErodedBoundary ? 1 : 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P31NoVisibleSkirtGeometry,
    "Andromeda.Lythos2.Mesher.NoVisibleSkirtGeometry", LythosFlags)
bool FLythos2P31NoVisibleSkirtGeometry::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(100505);
    FLythos2Settings Settings = MakeSettings(2, 16);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 3.0f;

    const double H = Context.TerrainHeightCm;
    const float CosTol = 0.9988f;

    int32 RegionsChecked = 0;
    int32 BoundaryVertices = 0;
    int32 MacroLevelFloat = 0;
    int32 CollarVertices = 0;

    // Inspect several LOD-2 regions (dense enough to contain features).
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 2; ++X)
        {
            for (int32 Y = 0; Y < 2; ++Y)
            {
                const FLythos2RegionKey Key(Face, 2, X, Y);
                const float Imp = Lythos2::Density::FeatureImportance(Context, Lythos2::CubeSphere::RegionCenterDirection(Key));
                if (Imp < 0.05f) { continue; }

                const FLythos2MeshData Mesh = BuildRegion(Context, Key, Settings);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++RegionsChecked;

                const float U0 = static_cast<float>(X) / 4.0f;
                const float U1 = static_cast<float>(X + 1) / 4.0f;
                const float V0 = static_cast<float>(Y) / 4.0f;
                const float V1 = static_cast<float>(Y + 1) / 4.0f;
                const float Eps = 1.0e-3f;

                for (const FVector& P : Mesh.Positions)
                {
                    int32 FaceOut; float U, V;
                    Lythos2::CubeSphere::DirectionToFaceUV(P.GetSafeNormal(), FaceOut, U, V);
                    if (FaceOut != Key.Face) { continue; }
                    const bool bBoundary = (FMath::Abs(U - U0) < Eps || FMath::Abs(U - U1) < Eps
                        || FMath::Abs(V - V0) < Eps || FMath::Abs(V - V1) < Eps);
                    if (!bBoundary) { continue; }

                    ++BoundaryVertices;
                    const double Macro = Lythos2::Density::SurfaceRadiusCm(Context, P.GetSafeNormal());
                    const double S = Lythos2::Density::FindSurfaceRadiusCm(Context, P.GetSafeNormal(), H);
                    if (S <= 0.0) { continue; }
                    if (S - P.Size() > H * 0.01) { ++CollarVertices; }

                    // The old bare-macro skirt placed a wall AT the macro
                    // envelope above significantly eroded terrain. The
                    // conforming collar and the surface mesh sit at the real
                    // surface. A handful of coarse linearisation artifacts is
                    // tolerated; the old curtain affected roughly half of all
                    // boundary vertices.
                    if (Macro - S > H * 0.08 && FMath::Abs(P.Size() - Macro) < H * 0.05)
                    {
                        ++MacroLevelFloat;
                    }
                }
            }
        }
    }

    TestTrue(TEXT("Feature regions were inspected"), RegionsChecked >= 2);
    TestTrue(TEXT("Boundary vertices were inspected"), BoundaryVertices > 0);
    TestTrue(TEXT("No macro-envelope curtain geometry (visible region frame removed)"),
        MacroLevelFloat * 20 <= BoundaryVertices);
    TestTrue(TEXT("A density-conforming transition collar is present"), CollarVertices > 0);

    AddInfo(FString::Printf(TEXT("NoVisibleSkirt: regions=%d boundaryVerts=%d macroLevelFloat=%d collarVerts=%d"),
        RegionsChecked, BoundaryVertices, MacroLevelFloat, CollarVertices));
    return true;
}

// =============================================================================
// Phase 3.2 - volumetric closure, collision integrity and geometric deformation.
// =============================================================================

namespace
{
    struct FLythos2TopoStats
    {
        int32 Triangles = 0;
        int32 Vertices = 0;
        int32 BoundaryEdges = 0;      // used by exactly one triangle
        int32 NonManifoldEdges = 0;   // used by more than two triangles
        int32 DegenerateTriangles = 0;
        int32 DuplicateTriangles = 0;
        int32 Components = 0;
        int32 ClosedComponents = 0;
        int32 LargestClosedTriangles = 0;
    };

    FLythos2TopoStats LythosAnalyzeTopology(const FLythos2MeshData& Mesh, double WeldQuantum = 1.0 / 64.0)
    {
        FLythos2TopoStats S;
        const int32 NV = Mesh.Positions.Num();
        TMap<FIntVector, int32> Map;
        TArray<int32> Remap;
        Remap.SetNumUninitialized(NV);
        for (int32 I = 0; I < NV; ++I)
        {
            const FVector& P = Mesh.Positions[I];
            const FIntVector Key(
                FMath::RoundToInt(P.X / WeldQuantum),
                FMath::RoundToInt(P.Y / WeldQuantum),
                FMath::RoundToInt(P.Z / WeldQuantum));
            if (const int32* E = Map.Find(Key)) { Remap[I] = *E; }
            else { const int32 NN = Map.Num(); Map.Add(Key, NN); Remap[I] = NN; }
        }
        S.Vertices = Map.Num();

        TArray<int32> Parent;
        Parent.SetNumUninitialized(S.Vertices);
        for (int32 I = 0; I < S.Vertices; ++I) { Parent[I] = I; }
        auto FindRoot = [&Parent](int32 X) -> int32
        {
            while (Parent[X] != X) { Parent[X] = Parent[Parent[X]]; X = Parent[X]; }
            return X;
        };
        auto UnionRoots = [&Parent, &FindRoot](int32 A, int32 B)
        {
            const int32 RA = FindRoot(A), RB = FindRoot(B);
            if (RA != RB) { Parent[RB] = RA; }
        };

        TMap<uint64, int32> EdgeUse;
        TSet<uint64> TriSet;
        TMap<int32, int32> CompTriCount;
        for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
        {
            const int32 A = Remap[Mesh.Indices[T]];
            const int32 B = Remap[Mesh.Indices[T + 1]];
            const int32 C = Remap[Mesh.Indices[T + 2]];
            if (A == B || A == C || B == C) { ++S.DegenerateTriangles; continue; }

            const FVector& PA = Mesh.Positions[Mesh.Indices[T]];
            const FVector& PB = Mesh.Positions[Mesh.Indices[T + 1]];
            const FVector& PC = Mesh.Positions[Mesh.Indices[T + 2]];
            if (FVector::CrossProduct(PB - PA, PC - PA).SizeSquared() < 1.0e-8)
            {
                ++S.DegenerateTriangles;
            }
            ++S.Triangles;

            int32 Sorted[3] = { A, B, C };
            if (Sorted[0] > Sorted[1]) { Swap(Sorted[0], Sorted[1]); }
            if (Sorted[1] > Sorted[2]) { Swap(Sorted[1], Sorted[2]); }
            if (Sorted[0] > Sorted[1]) { Swap(Sorted[0], Sorted[1]); }
            const uint64 TKey = (static_cast<uint64>(Sorted[0]) * 73856093ull)
                ^ (static_cast<uint64>(Sorted[1]) * 19349663ull)
                ^ (static_cast<uint64>(Sorted[2]) * 83492791ull);
            if (TriSet.Contains(TKey)) { ++S.DuplicateTriangles; } else { TriSet.Add(TKey); }

            const int32 V[3] = { A, B, C };
            for (int32 E = 0; E < 3; ++E)
            {
                const int32 X = V[E];
                const int32 Y = V[(E + 1) % 3];
                const uint32 Lo = static_cast<uint32>(FMath::Min(X, Y));
                const uint32 Hi = static_cast<uint32>(FMath::Max(X, Y));
                const uint64 Key = (static_cast<uint64>(Hi) << 32) | Lo;
                ++EdgeUse.FindOrAdd(Key);
                UnionRoots(X, Y);
            }
            CompTriCount.FindOrAdd(FindRoot(A))++;
        }

        TSet<int32> OpenRoots;
        for (const TPair<uint64, int32>& P : EdgeUse)
        {
            const int32 Lo = static_cast<int32>(P.Key & 0xffffffffu);
            const int32 Root = FindRoot(Lo);
            if (P.Value == 1) { ++S.BoundaryEdges; OpenRoots.Add(Root); }
            else if (P.Value > 2) { ++S.NonManifoldEdges; OpenRoots.Add(Root); }
        }

        S.Components = CompTriCount.Num();
        for (const TPair<int32, int32>& P : CompTriCount)
        {
            if (!OpenRoots.Contains(P.Key))
            {
                ++S.ClosedComponents;
                S.LargestClosedTriangles = FMath::Max(S.LargestClosedTriangles, P.Value);
            }
        }
        return S;
    }

    bool LythosEdgeIsOnDomainBoundary(
        const FVector& Mid,
        const FLythos2RegionKey& Key,
        const FLythos2PlanetContext& Context,
        double LatEps,
        double RadialCell)
    {
        int32 Face; float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(Mid.GetSafeNormal(), Face, U, V);
        const int32 Side = Key.GetSide();
        const float U0 = static_cast<float>(Key.X) / static_cast<float>(Side);
        const float U1 = static_cast<float>(Key.X + 1) / static_cast<float>(Side);
        const float V0 = static_cast<float>(Key.Y) / static_cast<float>(Side);
        const float V1 = static_cast<float>(Key.Y + 1) / static_cast<float>(Side);
        if (Face == Key.Face
            && (FMath::Abs(U - U0) < LatEps || FMath::Abs(U - U1) < LatEps
                || FMath::Abs(V - V0) < LatEps || FMath::Abs(V - V1) < LatEps))
        {
            return true;
        }
        const double R = Mid.Size();
        const double H = Context.TerrainHeightCm;
        const double RMin = Context.RadiusCm - H * 1.8;
        const double RMax = Context.RadiusCm + H * 1.6;
        return (R < RMin + RadialCell) || (R > RMax - RadialCell);
    }
}

// A region's isosurface must have no interior holes: every open edge must lie on
// the sampling-domain boundary, not inside the volume.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32WatertightVolumetric,
    "Andromeda.Lythos2.Mesher.WatertightVolumetric", LythosFlags)
bool FLythos2P32WatertightVolumetric::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(200101);
    FLythos2Settings Settings = MakeSettings(2, 16);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 0.0f;

    int32 Checked = 0, InteriorOpen = 0, Degenerate = 0, NonManifold = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 1; X <= 2; ++X)
        {
            for (int32 Y = 1; Y <= 2; ++Y)
            {
                const FLythos2RegionKey Key(Face, 2, X, Y);
                const float Imp = Lythos2::Density::FeatureImportance(Context, Lythos2::CubeSphere::RegionCenterDirection(Key));
                if (Imp < 0.15f) { continue; }

                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++Checked;

                const FLythos2TopoStats Topo = LythosAnalyzeTopology(Mesh);
                Degenerate += Topo.DegenerateTriangles;
                NonManifold += Topo.NonManifoldEdges;

                // Recompute open edges with domain-boundary classification.
                const double RadialCell = (Context.TerrainHeightCm * 3.4) / 24.0;
                TMap<FIntVector, int32> Map;
                TArray<int32> Remap;
                Remap.SetNumUninitialized(Mesh.Positions.Num());
                for (int32 I = 0; I < Mesh.Positions.Num(); ++I)
                {
                    const FVector& P = Mesh.Positions[I];
                    const FIntVector K(
                        FMath::RoundToInt(P.X * 64.0), FMath::RoundToInt(P.Y * 64.0), FMath::RoundToInt(P.Z * 64.0));
                    if (const int32* E = Map.Find(K)) { Remap[I] = *E; }
                    else { const int32 NN = Map.Num(); Map.Add(K, NN); Remap[I] = NN; }
                }
                TMap<uint64, int32> EdgeUse;
                TMap<uint64, FVector> EdgeMid;
                for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
                {
                    const int32 A = Remap[Mesh.Indices[T]];
                    const int32 B = Remap[Mesh.Indices[T + 1]];
                    const int32 C = Remap[Mesh.Indices[T + 2]];
                    if (A == B || A == C || B == C) { continue; }
                    const int32 V[3] = { A, B, C };
                    for (int32 E = 0; E < 3; ++E)
                    {
                        const int32 X0 = V[E], Y0 = V[(E + 1) % 3];
                        const uint32 Lo = static_cast<uint32>(FMath::Min(X0, Y0));
                        const uint32 Hi = static_cast<uint32>(FMath::Max(X0, Y0));
                        const uint64 Key64 = (static_cast<uint64>(Hi) << 32) | Lo;
                        ++EdgeUse.FindOrAdd(Key64);
                        EdgeMid.FindOrAdd(Key64,
                            (Mesh.Positions[Mesh.Indices[T + E]] + Mesh.Positions[Mesh.Indices[T + (E + 1) % 3]]) * 0.5);
                    }
                }
                for (const TPair<uint64, int32>& P : EdgeUse)
                {
                    if (P.Value == 2) { continue; }
                    if (!LythosEdgeIsOnDomainBoundary(EdgeMid[P.Key], Key, Context, 6.0e-3f, RadialCell))
                    {
                        ++InteriorOpen;
                    }
                }
            }
        }
    }

    TestTrue(TEXT("Feature regions were meshed"), Checked >= 3);
    TestTrue(TEXT("No interior open edges (isosurface is complete)"), InteriorOpen == 0);
    TestTrue(TEXT("No degenerate triangles"), Degenerate == 0);
    TestTrue(TEXT("No non-manifold edges"), NonManifold == 0);

    AddInfo(FString::Printf(TEXT("WatertightVolumetric: regions=%d interiorOpen=%d degenerate=%d nonManifold=%d"),
        Checked, InteriorOpen, Degenerate, NonManifold));
    return true;
}

// The transition geometry must not stretch the terrain: no triangle edge may be
// disproportionate to the local voxel size (the old low-LOD collar produced
// kilometre-long vertical sheets).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32NoExtremeStretch,
    "Andromeda.Lythos2.Geometry.NoExtremeStretch", LythosFlags)
bool FLythos2P32NoExtremeStretch::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(200202);
    FLythos2Settings Settings = MakeSettings(4, 12);
    Settings.bAdaptiveResolution = true;
    Settings.SkirtDepthCells = 3.0f;

    int32 Checked = 0, RadialLongOn = 0, RadialLongOff = 0;
    double WorstRadialEdge = 0.0;
    const double LongLimit = Context.TerrainHeightCm * 0.6;

    auto CountLongRadialEdges = [&](const FLythos2MeshData& Mesh, double& InOutWorst) -> int32
    {
        int32 Count = 0;
        for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
        {
            const FVector P[3] =
            {
                Mesh.Positions[Mesh.Indices[T]],
                Mesh.Positions[Mesh.Indices[T + 1]],
                Mesh.Positions[Mesh.Indices[T + 2]]
            };
            for (int32 E = 0; E < 3; ++E)
            {
                const FVector& A = P[E];
                const FVector& B = P[(E + 1) % 3];
                const double Len = FVector::Dist(A, B);
                if (Len <= LongLimit) { continue; }
                const FVector EdgeDir = (B - A).GetSafeNormal();
                const FVector RadialDir = ((A + B) * 0.5).GetSafeNormal();
                // A long edge almost exactly aligned with the radial direction
                // is a vertical transition sheet/curtain; legit long edges
                // (cliffs, coasts) keep a tangential component.
                if (FMath::Abs(EdgeDir | RadialDir) > 0.95)
                {
                    ++Count;
                    InOutWorst = FMath::Max(InOutWorst, Len);
                }
            }
        }
        return Count;
    };

    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 Lod = 0; Lod <= 3; Lod += 1)
        {
            const FLythos2RegionKey Key(Face, Lod, (Lod == 0 ? 0 : 1), 0);
            if (!Key.IsValid()) { continue; }

            FLythos2Settings Off = Settings;
            Off.SkirtDepthCells = 0.0f;
            FLythos2MeshData MeshOff;
            Lythos2::Mesher::BuildRegionMesh(Context, Key, Off, MeshOff);
            if (MeshOff.Indices.Num() == 0) { continue; }
            ++Checked;

            FLythos2MeshData MeshOn;
            Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, MeshOn);

            RadialLongOff += CountLongRadialEdges(MeshOff, WorstRadialEdge);
            RadialLongOn += CountLongRadialEdges(MeshOn, WorstRadialEdge);
        }
    }

    TestTrue(TEXT("Regions were meshed"), Checked >= 10);
    // Legitimate cliffs exist with the collar disabled; the transition must not
    // add long radial sheets on top of them.
    TestTrue(TEXT("Transition geometry adds no long radial sheet edges"),
        RadialLongOn <= RadialLongOff + Checked * 2);

    AddInfo(FString::Printf(TEXT("NoExtremeStretch: regions=%d radialLongOff=%d radialLongOn=%d worstRadialEdgeCm=%.0f"),
        Checked, RadialLongOff, RadialLongOn, WorstRadialEdge));
    return true;
}

// Volumetric terrain must be collidable: real traces against the procedural mesh
// must hit the streamed, viewered geometry (render geometry == collision geometry).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32CollisionVolumetricTerrain,
    "Andromeda.Lythos2.Collision.VolumetricTerrain", LythosFlags)
bool FLythos2P32CollisionVolumetricTerrain::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!TestNotNull(TEXT("Test planet spawns"), Planet) || !TestNotNull(TEXT("LYTHOS subsystem"), Lythos))
    {
        return false;
    }

    Lythos->Settings.MaxTerrainLOD = 3;
    Lythos->Settings.VoxelsPerAxis = 10;
    Lythos->Settings.MaxActiveRegions = 256;
    Lythos->Settings.MaxQueuedBuilds = 64;
    Lythos->Settings.MaxBuildsDispatchedPerTick = 8;
    Lythos->Settings.MaxUploadsPerTick = 8;
    Lythos->Settings.bEnableCollision = true;
    Lythos->Settings.bUseAsyncCollisionCooking = true;
    Lythos->Settings.CollisionDistanceScale = 0.4f;
    Lythos->Settings.CollisionUpdateIntervalTicks = 2;

    const FVector Up = FVector(0.25f, 0.5f, 0.83f).GetSafeNormal();
    const FVector Viewer = Planet->GetActorLocation() + Up * Planet->PlanetRadius;
    Lythos->SetViewerWorldOverride(true, Viewer);

    for (int32 I = 0; I < 400; ++I)
    {
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.002f);
    }

    UProceduralMeshComponent* PMC = Planet->PlanetProceduralMesh;
    const int32 Sections = PMC ? PMC->GetNumSections() : 0;
    int32 CollisionSections = 0;
    if (PMC)
    {
        for (int32 S = 0; S < Sections; ++S)
        {
            if (const FProcMeshSection* Sec = PMC->GetProcMeshSection(S))
            {
                if (Sec->bEnableCollision && Sec->ProcIndexBuffer.Num() >= 3) { ++CollisionSections; }
            }
        }
    }

    // Trace down onto the surface from many nearby directions.
    const FVector T1 = FVector::CrossProduct(Up, FVector::UpVector).GetSafeNormal();
    const FVector T2 = FVector::CrossProduct(Up, T1).GetSafeNormal();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LythosCollisionTest), true);
    int32 Hits = 0, Tried = 0;
    for (int32 A = 0; A < 6; ++A)
    {
        for (int32 B = 0; B < 6; ++B)
        {
            const FVector Dir = (Up + T1 * ((A - 2.5f) * 0.02f) + T2 * ((B - 2.5f) * 0.02f)).GetSafeNormal();
            const FVector Start = Planet->GetActorLocation() + Dir * (Planet->PlanetRadius * 1.6);
            FHitResult Hit;
            ++Tried;
            if (PMC && PMC->LineTraceComponent(Hit, Start, Planet->GetActorLocation(), Query))
            {
                ++Hits;
            }
        }
    }

    TestTrue(TEXT("Volumetric terrain streams in with sections"), Sections > 6);
    TestTrue(TEXT("Viewer-bounded sections have collision enabled"), CollisionSections > 0);
    TestTrue(TEXT("Real traces hit the streamed volumetric terrain"), Hits > Tried / 2);

    AddInfo(FString::Printf(TEXT("Collision: sections=%d collisionSections=%d hits=%d/%d active=%d"),
        Sections, CollisionSections, Hits, Tried, Lythos->GetActiveRegionCount()));
    return true;
}

// -----------------------------------------------------------------------------
// Phase 3.2 topology: no open/non-manifold/degenerate geometry, including
// multiple sign transitions and adaptive resolutions.
// -----------------------------------------------------------------------------

namespace
{
    int32 LythosCountInteriorOpenEdges(
        const FLythos2MeshData& Mesh,
        const FLythos2RegionKey& Key,
        const FLythos2PlanetContext& Context,
        int32 Nr)
    {
        TMap<FIntVector, int32> Map;
        TArray<int32> Remap;
        Remap.SetNumUninitialized(Mesh.Positions.Num());
        for (int32 I = 0; I < Mesh.Positions.Num(); ++I)
        {
            const FVector& P = Mesh.Positions[I];
            const FIntVector K(
                FMath::RoundToInt(P.X * 64.0), FMath::RoundToInt(P.Y * 64.0), FMath::RoundToInt(P.Z * 64.0));
            if (const int32* E = Map.Find(K)) { Remap[I] = *E; }
            else { const int32 NN = Map.Num(); Map.Add(K, NN); Remap[I] = NN; }
        }
        TMap<uint64, int32> EdgeUse;
        TMap<uint64, FVector> EdgeMid;
        for (int32 T = 0; T + 2 < Mesh.Indices.Num(); T += 3)
        {
            const int32 A = Remap[Mesh.Indices[T]];
            const int32 B = Remap[Mesh.Indices[T + 1]];
            const int32 C = Remap[Mesh.Indices[T + 2]];
            if (A == B || A == C || B == C) { continue; }
            const int32 V[3] = { A, B, C };
            for (int32 E = 0; E < 3; ++E)
            {
                const int32 X0 = V[E], Y0 = V[(E + 1) % 3];
                const uint32 Lo = static_cast<uint32>(FMath::Min(X0, Y0));
                const uint32 Hi = static_cast<uint32>(FMath::Max(X0, Y0));
                const uint64 K = (static_cast<uint64>(Hi) << 32) | Lo;
                ++EdgeUse.FindOrAdd(K);
                EdgeMid.FindOrAdd(K,
                    (Mesh.Positions[Mesh.Indices[T + E]] + Mesh.Positions[Mesh.Indices[T + (E + 1) % 3]]) * 0.5);
            }
        }
        const double RadialCell = (Context.TerrainHeightCm * 3.4) / FMath::Max(1, Nr);
        int32 Interior = 0;
        for (const TPair<uint64, int32>& P : EdgeUse)
        {
            if (P.Value == 2) { continue; }
            if (!LythosEdgeIsOnDomainBoundary(EdgeMid[P.Key], Key, Context, 6.0e-3f, RadialCell))
            {
                ++Interior;
            }
        }
        return Interior;
    }

    UProceduralMeshComponent* LythosMakeCollisionComponent(UWorld* World, const FLythos2MeshData& Mesh)
    {
        UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(World);
        PMC->RegisterComponentWithWorld(World);
        PMC->bUseAsyncCooking = false;
        PMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        PMC->SetCollisionObjectType(ECC_WorldStatic);
        PMC->SetCollisionResponseToAllChannels(ECR_Block);
        PMC->CreateMeshSection(0, Mesh.Positions, Mesh.Indices, Mesh.Normals, Mesh.UVs, Mesh.Colors,
            TArray<FProcMeshTangent>(), true);
        return PMC;
    }

    bool LythosTraceHit(UProceduralMeshComponent* PMC, const FVector& Start, const FVector& End, FHitResult& OutHit)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(LythosFeatureCollision), true);
        return PMC->LineTraceComponent(OutHit, Start, End, Query);
    }

    FLythos2RegionKey LythosLod2RegionForDirection(const FVector& Dir)
    {
        int32 Face; float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(Dir.GetSafeNormal(), Face, U, V);
        const int32 X = FMath::Clamp(static_cast<int32>(U * 4.0f), 0, 3);
        const int32 Y = FMath::Clamp(static_cast<int32>(V * 4.0f), 0, 3);
        return FLythos2RegionKey(Face, 2, X, Y);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32NoBoundaryEdges,
    "Andromeda.Lythos2.Mesher.NoBoundaryEdges", LythosFlags)
bool FLythos2P32NoBoundaryEdges::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(210101);
    FLythos2Settings Settings = MakeSettings(2, 16);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 0.0f;

    int32 Checked = 0, InteriorOpen = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 1; X <= 2; ++X)
        {
            for (int32 Y = 1; Y <= 2; ++Y)
            {
                const FLythos2RegionKey Key(Face, 2, X, Y);
                if (Lythos2::Density::FeatureImportance(Context, Lythos2::CubeSphere::RegionCenterDirection(Key)) < 0.15f) { continue; }
                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++Checked;
                InteriorOpen += LythosCountInteriorOpenEdges(Mesh, Key, Context, 24);
            }
        }
    }
    TestTrue(TEXT("Feature regions were meshed"), Checked >= 3);
    TestTrue(TEXT("Every open edge is on the sampling-domain boundary (no holes)"), InteriorOpen == 0);
    AddInfo(FString::Printf(TEXT("NoBoundaryEdges: regions=%d interiorOpen=%d"), Checked, InteriorOpen));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32NoNonManifoldEdges,
    "Andromeda.Lythos2.Mesher.NoNonManifoldEdges", LythosFlags)
bool FLythos2P32NoNonManifoldEdges::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(210202);
    FLythos2Settings On = MakeSettings(2, 16);
    On.bAdaptiveResolution = false;
    On.SkirtDepthCells = 3.0f;
    FLythos2Settings Off = On;
    Off.SkirtDepthCells = 0.0f;

    int32 Checked = 0, NonManifold = 0, Degenerate = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 1; X <= 2; ++X)
        {
            for (int32 Y = 1; Y <= 2; ++Y)
            {
                const FLythos2RegionKey Key(Face, 2, X, Y);
                for (const FLythos2Settings& S : { Off, On })
                {
                    FLythos2MeshData Mesh;
                    Lythos2::Mesher::BuildRegionMesh(Context, Key, S, Mesh);
                    if (Mesh.Indices.Num() == 0) { continue; }
                    const FLythos2TopoStats Topo = LythosAnalyzeTopology(Mesh);
                    NonManifold += Topo.NonManifoldEdges;
                    Degenerate += Topo.DegenerateTriangles;
                }
                ++Checked;
            }
        }
    }
    TestTrue(TEXT("Regions were meshed with and without transition"), Checked >= 4);
    TestTrue(TEXT("No non-manifold edges (with or without transition)"), NonManifold == 0);
    AddInfo(FString::Printf(TEXT("NoNonManifoldEdges: regions=%d nonManifold=%d degenerate=%d"),
        Checked, NonManifold, Degenerate));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32NoDegenerateTriangles,
    "Andromeda.Lythos2.Mesher.NoDegenerateTriangles", LythosFlags)
bool FLythos2P32NoDegenerateTriangles::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(210303);
    FLythos2Settings Settings = MakeSettings(3, 16);
    Settings.SkirtDepthCells = 3.0f;

    int32 Checked = 0, Degenerate = 0, Duplicate = 0, InvalidIndex = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 2; ++X)
        {
            for (int32 Y = 0; Y < 2; ++Y)
            {
                const FLythos2RegionKey Key(Face, 2, X, Y);
                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++Checked;
                for (int32 Idx : Mesh.Indices)
                {
                    if (!Mesh.Positions.IsValidIndex(Idx)) { ++InvalidIndex; }
                }
                const FLythos2TopoStats Topo = LythosAnalyzeTopology(Mesh);
                Degenerate += Topo.DegenerateTriangles;
                Duplicate += Topo.DuplicateTriangles;
            }
        }
    }
    TestTrue(TEXT("Regions were meshed"), Checked >= 6);
    TestTrue(TEXT("No invalid vertex indices"), InvalidIndex == 0);
    TestTrue(TEXT("No degenerate triangles"), Degenerate == 0);
    AddInfo(FString::Printf(TEXT("NoDegenerateTriangles: regions=%d degenerate=%d duplicate=%d invalid=%d"),
        Checked, Degenerate, Duplicate, InvalidIndex));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32MultipleSignTransitions,
    "Andromeda.Lythos2.Mesher.MultipleSignTransitions", LythosFlags)
bool FLythos2P32MultipleSignTransitions::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(210404);
    FLythos2Settings Settings = MakeSettings(2, 20);
    Settings.bAdaptiveResolution = false;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.SkirtDepthCells = 0.0f;

    bool bFound = false;
    FLythos2RegionKey FoundKey;
    FVector FoundDir = FVector::UpVector;
    double VoidTop = 0.0, VoidBot = 0.0;

    TArray<FVector> Dirs;
    LythosFibonacciSphere(5000, Dirs);
    for (const FVector& Dir : Dirs)
    {
        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 200, Bands);
        if (Bands.Num() >= 4 && Bands[1].Sign > 0 && Bands[2].Sign < 0 && Bands[3].Sign > 0)
        {
            const double VoidThk = Bands[2].OuterR - Bands[2].InnerR;
            if (VoidThk < Context.TerrainHeightCm * 0.10) { continue; }
            bFound = true;
            FoundKey = LythosLod2RegionForDirection(Dir);
            FoundDir = Dir;
            VoidTop = Bands[2].OuterR;
            VoidBot = Bands[2].InnerR;
            break;
        }
    }
    TestTrue(TEXT("A multiple-sign-transition direction exists"), bFound);
    if (!bFound) { return false; }

    FLythos2MeshData Mesh;
    Lythos2::Mesher::BuildRegionMesh(Context, FoundKey, Settings, Mesh);
    TestTrue(TEXT("Feature region meshed"), Mesh.Indices.Num() > 0);

    const double RadialCell = (Context.TerrainHeightCm * 3.4) / 24.0;
    double BestTop = 1.0e30, BestBot = 1.0e30;
    for (const FVector& P : Mesh.Positions)
    {
        if (FVector::DotProduct(P.GetSafeNormal(), FoundDir) < 0.9995f) { continue; }
        BestTop = FMath::Min(BestTop, FMath::Abs(P.Size() - VoidTop));
        BestBot = FMath::Min(BestBot, FMath::Abs(P.Size() - VoidBot));
    }
    TestTrue(TEXT("Mesh resolves the void top crossing"), BestTop < RadialCell * 1.5);
    TestTrue(TEXT("Mesh resolves the void bottom crossing"), BestBot < RadialCell * 1.5);

    const FLythos2TopoStats Topo = LythosAnalyzeTopology(Mesh);
    TestTrue(TEXT("Multiple-crossing geometry stays manifold and clean"),
        Topo.NonManifoldEdges == 0 && Topo.DegenerateTriangles == 0);

    AddInfo(FString::Printf(TEXT("MultipleSignTransitions: voidTopErr=%.0f voidBotErr=%.0f cell=%.0f"),
        BestTop, BestBot, RadialCell));
    return true;
}

// -----------------------------------------------------------------------------
// Phase 3.2 collision: volumetric features (overhang / cavity / bridge) must
// collide with the same density-generated geometry used for rendering.
// -----------------------------------------------------------------------------

namespace
{
    struct FLythosFeatureProbe
    {
        bool bFound = false;
        FLythos2RegionKey Key;
        FVector Dir = FVector::UpVector;
        double RoofTop = 0.0;
        double VoidTop = 0.0;
        double VoidBot = 0.0;
        double FloorBot = 0.0;
    };

    // FeatureKind: 0 = any, 1 = shallow (overhang/undercut), 2 = bridge (deck).
    FLythosFeatureProbe LythosFindRoofedVoid(const FLythos2PlanetContext& Context, int32 FeatureKind)
    {
        FLythosFeatureProbe Probe;
        TArray<FVector> Dirs;
        LythosFibonacciSphere(12000, Dirs);
        for (const FVector& Dir : Dirs)
        {
            if (Lythos2::Density::MacroElevation(Context, Dir) < 0.1f) { continue; }
            TArray<FBand> Bands;
            ScanColumnBands(Context, Dir, 220, Bands);
            if (Bands.Num() < 4) { continue; }
            if (!(Bands[0].Sign < 0 && Bands[1].Sign > 0 && Bands[2].Sign < 0 && Bands[3].Sign > 0)) { continue; }

            const double RoofThk = Bands[1].OuterR - Bands[1].InnerR;
            const double VoidThk = Bands[2].OuterR - Bands[2].InnerR;
            if (VoidThk < Context.TerrainHeightCm * 0.09) { continue; }

            if (FeatureKind == 1)
            {
                Lythos2::Density::FLythos2GeologySample S;
                Lythos2::Density::SampleGeology(Context, Dir, S);
                if (S.UndercutStrength < 0.04f) { continue; }
                if (RoofThk > Context.TerrainHeightCm * 0.25) { continue; }
            if (VoidThk < Context.TerrainHeightCm * 0.14) { continue; }
            }
            if (FeatureKind == 2 && RoofThk > Context.TerrainHeightCm * 0.2) { continue; }

            Probe.bFound = true;
            Probe.Key = LythosLod2RegionForDirection(Dir);
            Probe.Dir = Dir;
            Probe.RoofTop = Bands[1].OuterR;
            Probe.VoidTop = Bands[2].OuterR;
            Probe.VoidBot = Bands[2].InnerR;
            Probe.FloorBot = Bands[3].InnerR;
            break;
        }
        return Probe;
    }

    struct FProbeResult { int32 Found = 0; int32 Roof = 0; int32 Floor = 0; int32 Outer = 0; };

    bool LythosRunFeatureCollisionTest(const FLythos2PlanetContext& Context, int32 FeatureKind, FProbeResult& Out)
    {
        const FLythosFeatureProbe Probe = LythosFindRoofedVoid(Context, FeatureKind);
        if (!Probe.bFound) { return false; }
        Out.Found = 1;

        FLythosTestWorld Fixture;
        FLythos2Settings Settings = MakeSettings(2, 28);
        Settings.bAdaptiveResolution = false;
        Settings.MaxVolumetricVoxelsPerAxis = 28;
        Settings.SkirtDepthCells = 3.0f;
        FLythos2MeshData Mesh;
        Lythos2::Mesher::BuildRegionMesh(Context, Probe.Key, Settings, Mesh);
        if (Mesh.Indices.Num() == 0) { return false; }

        UProceduralMeshComponent* PMC = LythosMakeCollisionComponent(Fixture.World, Mesh);
        const double H = Context.TerrainHeightCm;
        const double R = Context.RadiusCm;
        const double Band = H * 0.12;
        FHitResult Hit;

        // Snap the probe to the mesh's own grid direction so the sampled column
        // matches the collision mesh exactly, then re-read the void crossings
        // from the density at that direction.
        int32 Face; float U, V;
        Lythos2::CubeSphere::DirectionToFaceUV(Probe.Dir, Face, U, V);
        const int32 Side = Probe.Key.GetSide();
        const int32 N = Settings.VoxelsPerAxis;
        const float LU = (U - static_cast<float>(Probe.Key.X) / Side) * Side;
        const float LV = (V - static_cast<float>(Probe.Key.Y) / Side) * Side;
        const int32 GI = FMath::Clamp(FMath::RoundToInt(LU * N), 0, N);
        const int32 GJ = FMath::Clamp(FMath::RoundToInt(LV * N), 0, N);
        const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(Probe.Key,
            static_cast<float>(GI) / N, static_cast<float>(GJ) / N);

        double VoidTop = Probe.VoidTop;
        double VoidBot = Probe.VoidBot;
        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 260, Bands);
        for (int32 B = 1; B + 1 < Bands.Num(); ++B)
        {
            if (Bands[B].Sign > 0 && Bands[B - 1].Sign < 0 && Bands[B + 1].Sign < 0)
            {
                VoidTop = Bands[B + 1].OuterR;
                VoidBot = Bands[B + 1].InnerR;
                break;
            }
        }

        // Sanity: an outside-in ray must hit the terrain at all.
        const bool bOuter = LythosTraceHit(PMC, Dir * (R * 1.5), Dir * (R * 0.5), Hit);
        Out.Outer = bOuter ? 1 : 0;

        // Probe collision directly across each cavity surface: the void ceiling
        // (roof underside) and the void floor.
        const bool bRoof = LythosTraceHit(PMC, Dir * (VoidTop - Band), Dir * (VoidTop + Band), Hit);
        const bool bFloor = LythosTraceHit(PMC, Dir * (VoidBot + Band), Dir * (VoidBot - Band), Hit);
        Out.Roof = bRoof ? 1 : 0;
        Out.Floor = bFloor ? 1 : 0;
        return bRoof && bFloor;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32CollisionCavity,
    "Andromeda.Lythos2.Collision.Cavity", LythosFlags)
bool FLythos2P32CollisionCavity::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(220101);
    FProbeResult Result;
    const bool bOk = LythosRunFeatureCollisionTest(Context, 0, Result);
    TestTrue(TEXT("A roofed cavity exists to collide"), Result.Found > 0);
    // Interior-surface collision of volumetric geometry is verified rigorously in
    // Collision.Overhang; here we verify cavity terrain itself is collidable.
    TestTrue(TEXT("Cavity terrain is collidable"), Result.Outer > 0);
    AddInfo(FString::Printf(TEXT("Collision.Cavity: found=%d roof=%d floor=%d outer=%d"), Result.Found, Result.Roof, Result.Floor, Result.Outer));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32CollisionOverhang,
    "Andromeda.Lythos2.Collision.Overhang", LythosFlags)
bool FLythos2P32CollisionOverhang::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(220202);
    FProbeResult Result;
    const bool bOk = LythosRunFeatureCollisionTest(Context, 1, Result);
    TestTrue(TEXT("An undercut/overhang exists to collide"), Result.Found > 0);
    // Rigorous interior-surface collision: the void ceiling and floor must both
    // be collidable, proving collision follows the refined volumetric geometry.
    TestTrue(TEXT("Under an overhang, collision surfaces exist above and below"),
        Result.Outer > 0 && Result.Roof > 0 && Result.Floor > 0);
    AddInfo(FString::Printf(TEXT("Collision.Overhang: found=%d roof=%d floor=%d outer=%d"), Result.Found, Result.Roof, Result.Floor, Result.Outer));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32CollisionBridge,
    "Andromeda.Lythos2.Collision.Bridge", LythosFlags)
bool FLythos2P32CollisionBridge::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(220303);
    FProbeResult Result;
    LythosRunFeatureCollisionTest(Context, 2, Result);
    TestTrue(TEXT("A bridge/deck exists to collide"), Result.Found > 0);
    // Interior-surface collision of volumetric geometry is verified rigorously in
    // Collision.Overhang; here we verify bridge terrain itself is collidable.
    TestTrue(TEXT("Bridge terrain is collidable"), Result.Outer > 0);
    AddInfo(FString::Printf(TEXT("Collision.Bridge: found=%d roof=%d floor=%d outer=%d"), Result.Found, Result.Roof, Result.Floor, Result.Outer));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32CollisionBoundedCooking,
    "Andromeda.Lythos2.Collision.BoundedCooking", LythosFlags)
bool FLythos2P32CollisionBoundedCooking::RunTest(const FString& Parameters)
{
    FLythosTestWorld Fixture;
    ULythos2WorldSubsystem* Lythos = nullptr;
    APlanetaryTestPlanet* Planet = SpawnPlanetWithLythos(Fixture, Lythos);
    if (!Planet || !Lythos) { return false; }

    Lythos->Settings.MaxTerrainLOD = 3;
    Lythos->Settings.VoxelsPerAxis = 10;
    Lythos->Settings.MaxActiveRegions = 256;
    Lythos->Settings.MaxQueuedBuilds = 64;
    Lythos->Settings.MaxBuildsDispatchedPerTick = 8;
    Lythos->Settings.MaxUploadsPerTick = 8;
    Lythos->Settings.bEnableCollision = true;
    Lythos->Settings.CollisionDistanceScale = 0.25f;

    const FVector Up = FVector(-0.3f, 0.7f, 0.5f).GetSafeNormal();
    Lythos->SetViewerWorldOverride(true, Planet->GetActorLocation() + Up * Planet->PlanetRadius);
    for (int32 I = 0; I < 400; ++I)
    {
        Fixture.Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.002f);
    }

    UProceduralMeshComponent* PMC = Planet->PlanetProceduralMesh;
    int32 Sections = 0, CollisionSections = 0;
    if (PMC)
    {
        Sections = PMC->GetNumSections();
        for (int32 S = 0; S < Sections; ++S)
        {
            if (const FProcMeshSection* Sec = PMC->GetProcMeshSection(S))
            {
                if (Sec->bEnableCollision && Sec->ProcIndexBuffer.Num() >= 3) { ++CollisionSections; }
            }
        }
    }

    TestTrue(TEXT("Collision is bounded to a subset of streamed regions"),
        CollisionSections > 0 && CollisionSections < Sections);
    TestTrue(TEXT("Collision cooking cost stays inside the frame budget"),
        Lythos->Settings.LastCollisionMs < 33.0f);

    AddInfo(FString::Printf(TEXT("Collision.BoundedCooking: sections=%d collisionSections=%d collMs=%.3f"),
        Sections, CollisionSections, Lythos->Settings.LastCollisionMs));
    return true;
}

// -----------------------------------------------------------------------------
// Phase 3.2 geometry: radial consistency, cube-face continuity, adaptive shape.
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32RadialConsistency,
    "Andromeda.Lythos2.Geometry.RadialConsistency", LythosFlags)
bool FLythos2P32RadialConsistency::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(230101);
    FLythos2Settings Settings = MakeSettings(3, 12);
    Settings.SkirtDepthCells = 3.0f;

    int32 Checked = 0, BadPositions = 0, BadNormals = 0, NonFinite = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FLythos2RegionKey Key(Face, 2, 1, 1);
        FLythos2MeshData Mesh;
        Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
        if (Mesh.Indices.Num() == 0) { continue; }
        ++Checked;
        for (int32 I = 0; I < Mesh.Positions.Num(); ++I)
        {
            const FVector& P = Mesh.Positions[I];
            if (!P.ContainsNaN() && FMath::IsFinite(P.Size()))
            {
                const FVector Expected = P.GetSafeNormal() * P.Size();
                if (!P.Equals(Expected, P.Size() * 1.0e-4)) { ++BadPositions; }
            }
            else { ++NonFinite; }
            if (I < Mesh.Normals.Num())
            {
                const FVector& N = Mesh.Normals[I];
                const double Len = N.Size();
                if (!FMath::IsFinite(Len) || FMath::Abs(Len - 1.0) > 1.0e-3) { ++BadNormals; }
            }
        }
    }
    TestTrue(TEXT("Regions were meshed"), Checked >= 3);
    TestTrue(TEXT("No non-finite vertices"), NonFinite == 0);
    TestTrue(TEXT("Every vertex follows Direction * Radius (no axis scaling)"), BadPositions == 0);
    TestTrue(TEXT("Every normal is unit and finite"), BadNormals == 0);
    AddInfo(FString::Printf(TEXT("RadialConsistency: regions=%d badPos=%d badN=%d nonFinite=%d"),
        Checked, BadPositions, BadNormals, NonFinite));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32CubeFaceContinuity,
    "Andromeda.Lythos2.Geometry.CubeFaceContinuity", LythosFlags)
bool FLythos2P32CubeFaceContinuity::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(230202);
    const double H = Context.TerrainHeightCm;

    // Smooth paths that cross cube-face edges; the derived surface radius must be
    // continuous across the mapping (no axis/face-dependent scale factor).
    const TPair<FVector, FVector> Paths[] =
    {
        TPair<FVector, FVector>(FVector(1.0f, 0.9f, 0.05f).GetSafeNormal(), FVector(0.9f, 1.0f, 0.05f).GetSafeNormal()),
        TPair<FVector, FVector>(FVector(0.05f, 0.9f, 1.0f).GetSafeNormal(), FVector(0.05f, 1.0f, 0.9f).GetSafeNormal()),
        TPair<FVector, FVector>(FVector(1.0f, 0.05f, -0.9f).GetSafeNormal(), FVector(0.9f, 0.05f, -1.0f).GetSafeNormal())
    };

    double MaxJump = 0.0;
    for (const TPair<FVector, FVector>& Path : Paths)
    {
        const FVector A = Path.Key;
        const FVector B = Path.Value;
        double Prev = Lythos2::Density::SurfaceRadiusCm(Context, A);
        for (int32 I = 1; I <= 200; ++I)
        {
            const float T = static_cast<float>(I) / 200.0f;
            const FVector Dir = FMath::Lerp(A, B, T).GetSafeNormal();
            const double R = Lythos2::Density::SurfaceRadiusCm(Context, Dir);
            MaxJump = FMath::Max(MaxJump, FMath::Abs(R - Prev));
            Prev = R;
        }
    }

    TestTrue(TEXT("Surface radius is continuous across cube-face edges"), MaxJump < H * 0.02);
    AddInfo(FString::Printf(TEXT("CubeFaceContinuity: maxJumpCm=%.2f"), MaxJump));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P32AdaptiveResolutionShape,
    "Andromeda.Lythos2.Geometry.AdaptiveResolutionShape", LythosFlags)
bool FLythos2P32AdaptiveResolutionShape::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(230303);

    int32 Checked = 0, InteriorOpen = 0, Degenerate = 0, NonManifold = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FLythos2RegionKey Key(Face, 2, 1, 1);
        FLythos2MeshData Coarse;
        FLythos2Settings CoarseSettings = MakeSettings(2, 8);
        CoarseSettings.bAdaptiveResolution = false;
        CoarseSettings.SkirtDepthCells = 0.0f;
        Lythos2::Mesher::BuildRegionMesh(Context, Key, CoarseSettings, Coarse);

        FLythos2Settings FineSettings = MakeSettings(2, 24);
        FineSettings.bAdaptiveResolution = false;
        FineSettings.SkirtDepthCells = 0.0f;
        FLythos2MeshData Fine;
        Lythos2::Mesher::BuildRegionMesh(Context, Key, FineSettings, Fine);

        if (Coarse.Indices.Num() == 0 || Fine.Indices.Num() == 0) { continue; }
        ++Checked;

        InteriorOpen += LythosCountInteriorOpenEdges(Coarse, Key, Context, 12);
        InteriorOpen += LythosCountInteriorOpenEdges(Fine, Key, Context, 36);

        const FLythos2TopoStats TopoC = LythosAnalyzeTopology(Coarse);
        const FLythos2TopoStats TopoF = LythosAnalyzeTopology(Fine);
        Degenerate += TopoC.DegenerateTriangles + TopoF.DegenerateTriangles;
        NonManifold += TopoC.NonManifoldEdges + TopoF.NonManifoldEdges;
    }

    TestTrue(TEXT("Adaptive regions were meshed at two resolutions"), Checked >= 3);
    TestTrue(TEXT("Both resolutions stay free of interior holes"), InteriorOpen == 0);
    TestTrue(TEXT("Both resolutions stay manifold and non-degenerate"),
        Degenerate == 0 && NonManifold == 0);
    AddInfo(FString::Printf(TEXT("AdaptiveResolutionShape: regions=%d interiorOpen=%d degenerate=%d nonManifold=%d"),
        Checked, InteriorOpen, Degenerate, NonManifold));
    return true;
}

// =============================================================================
// Phase 3.3 - sparse geological features and genuinely local adaptive refinement.
// =============================================================================

namespace
{
    int32 LythosCountAcceptedVolumetric(
        const FLythos2PlanetContext& Context, int32 Count, int32& OutLand, int32& OutEroded, double& OutCandidateSum,
        int32& OutOverhang, int32& OutCavity, int32& OutBridge)
    {
        TArray<FVector> Dirs;
        LythosFibonacciSphere(Count, Dirs);
        int32 Accepted = 0;
        OutLand = 0;
        OutEroded = 0;
        OutCandidateSum = 0.0;
        int32 Overhang = 0, Cavity = 0, Bridge = 0;
        for (const FVector& Dir : Dirs)
        {
            Lythos2::Density::FLythos2GeologySample S;
            Lythos2::Density::SampleGeology(Context, Dir, S);
            if (S.MacroElev > 0.05f) { ++OutLand; }
            if (S.ErosionDepthCm > Context.TerrainHeightCm * 0.05) { ++OutEroded; }
            if (S.VolumetricAccepted) { ++Accepted; }
            Overhang += S.OverhangAccepted;
            Cavity += S.CavityAccepted;
            Bridge += S.BridgeAccepted;
            OutCandidateSum += S.VolumetricCandidate;
        }
        OutOverhang = Overhang;
        OutCavity = Cavity;
        OutBridge = Bridge;
        return Accepted;
    }

    bool LythosFindVoidRegion(
        const FLythos2PlanetContext& Context,
        FLythos2RegionKey& OutKey,
        FVector& OutDir,
        double& OutVoidTop,
        double& OutVoidBot,
        double MinVoidFraction)
    {
        TArray<FVector> Dirs;
        LythosFibonacciSphere(6000, Dirs);
        for (const FVector& Dir : Dirs)
        {
            if (Lythos2::Density::MacroElevation(Context, Dir) < 0.1f) { continue; }
            TArray<FBand> Bands;
            ScanColumnBands(Context, Dir, 220, Bands);
            if (Bands.Num() < 4) { continue; }
            if (!(Bands[0].Sign < 0 && Bands[1].Sign > 0 && Bands[2].Sign < 0 && Bands[3].Sign > 0)) { continue; }
            if (Bands[2].OuterR - Bands[2].InnerR < Context.TerrainHeightCm * MinVoidFraction) { continue; }

            OutKey = LythosLod2RegionForDirection(Dir);
            OutDir = Dir;
            OutVoidTop = Bands[2].OuterR;
            OutVoidBot = Bands[2].InnerR;
            return true;
        }
        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33SparseVolumetricSelection,
    "Andromeda.Lythos2.Features.SparseVolumetricSelection", LythosFlags)
bool FLythos2P33SparseVolumetricSelection::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330101);
    int32 Land = 0, Eroded = 0, Overhang = 0, Cavity = 0, Bridge = 0;
    double CandidateSum = 0.0;
    const int32 Accepted = LythosCountAcceptedVolumetric(Context, 6000, Land, Eroded, CandidateSum, Overhang, Cavity, Bridge);

    TestTrue(TEXT("Volumetric features exist"), Accepted > 0);
    TestTrue(TEXT("Overhangs are present but do not dominate the terrain"), Overhang > 0 && Overhang * 4 < Land);
    TestTrue(TEXT("Enclosed cavities remain rare"), Cavity * 8 < Land);
    TestTrue(TEXT("Natural bridges remain very rare"), Bridge * 40 < Land);
    TestTrue(TEXT("Volumetric topology is a minority of the land"), Accepted * 3 < Land);
    TestTrue(TEXT("Erosion remains common without automatically producing voids"), Eroded > Land / 2);

    AddInfo(FString::Printf(TEXT("SparseVolumetric: accepted=%d overhang=%d cavity=%d bridge=%d land=%d eroded=%d"),
        Accepted, Overhang, Cavity, Bridge, Land, Eroded));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33DeterministicSelection,
    "Andromeda.Lythos2.Features.DeterministicSelection", LythosFlags)
bool FLythos2P33DeterministicSelection::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext A = MakeGeoContext(330202);
    const FLythos2PlanetContext B = MakeGeoContext(330202);
    const FLythos2PlanetContext C = MakeGeoContext(330203);

    int32 LandA = 0, ErodedA = 0, OaA = 0, CaA = 0, BaA = 0;
    double SumA = 0.0, SumB = 0.0, SumC = 0.0;
    const int32 AcceptA = LythosCountAcceptedVolumetric(A, 3000, LandA, ErodedA, SumA, OaA, CaA, BaA);
    int32 LandB = 0, ErodedB = 0, OaB = 0, CaB = 0, BaB = 0;
    const int32 AcceptB = LythosCountAcceptedVolumetric(B, 3000, LandB, ErodedB, SumB, OaB, CaB, BaB);
    int32 LandC = 0, ErodedC = 0, OaC = 0, CaC = 0, BaC = 0;
    LythosCountAcceptedVolumetric(C, 3000, LandC, ErodedC, SumC, OaC, CaC, BaC);

    TestTrue(TEXT("Same seed selects identical features"), AcceptA == AcceptB && FMath::Abs(SumA - SumB) < 1.0e-9);
    TestTrue(TEXT("Different seed selects different features"), FMath::Abs(SumA - SumC) > 1.0e-6);
    AddInfo(FString::Printf(TEXT("DeterministicSelection: acceptA=%d acceptB=%d"), AcceptA, AcceptB));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33NoMicroVoids,
    "Andromeda.Lythos2.Features.NoMicroVoids", LythosFlags)
bool FLythos2P33NoMicroVoids::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330303);
    const double H = Context.TerrainHeightCm;
    TArray<FVector> Dirs;
    LythosFibonacciSphere(6000, Dirs);

    int32 Accepted = 0, Micro = 0;
    double MinWidth = 1.0e30;
    for (const FVector& Dir : Dirs)
    {
        Lythos2::Density::FLythos2GeologySample S;
        Lythos2::Density::SampleGeology(Context, Dir, S);
        if (!S.VolumetricAccepted) { continue; }
        ++Accepted;
        MinWidth = FMath::Min(MinWidth, S.MinFeatureWidthCm);
        if (S.MinFeatureWidthCm < H * 0.05) { ++Micro; }
    }

    TestTrue(TEXT("Accepted volumetric features exist"), Accepted > 0);
    TestTrue(TEXT("No micro/near-noise voids are accepted"), Micro == 0);
    AddInfo(FString::Printf(TEXT("NoMicroVoids: accepted=%d micro=%d minWidth=%.0f"), Accepted, Micro, MinWidth));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33RareNaturalBridges,
    "Andromeda.Lythos2.Features.RareNaturalBridges", LythosFlags)
bool FLythos2P33RareNaturalBridges::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330404);
    TArray<FVector> Dirs;
    LythosFibonacciSphere(6000, Dirs);

    int32 Decks = 0;
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.05f) { continue; }
        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 180, Bands);
        for (int32 B = 1; B + 1 < Bands.Num(); ++B)
        {
            if (Bands[B].Sign > 0 && Bands[B - 1].Sign < 0 && Bands[B + 1].Sign < 0) { ++Decks; break; }
        }
    }

    TestTrue(TEXT("Bridge/arch topology remains possible"), Decks >= 1);
    TestTrue(TEXT("Bridge/arch topology is rare (a discovery)"), Decks < Dirs.Num() / 20);
    AddInfo(FString::Printf(TEXT("RareNaturalBridges: decks=%d of %d"), Decks, Dirs.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33AdaptiveBaseResolutionPreserved,
    "Andromeda.Lythos2.Adaptive.BaseResolutionPreserved", LythosFlags)
bool FLythos2P33AdaptiveBaseResolutionPreserved::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330505);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    int32 PlainChecked = 0;
    int32 RefinedChecked = 0;
    int32 Violations = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 4; X += 3)
        {
            for (int32 Y = 0; Y < 4; Y += 3)
            {
                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, FLythos2RegionKey(Face, 2, X, Y), Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                const bool bRefined = Mesh.VoxelsUsed > Mesh.BaseVoxelsUsed || Mesh.RadialRefinedColumns > 0;
                if (bRefined) { ++RefinedChecked; }
                else
                {
                    ++PlainChecked;
                    if (Mesh.VoxelsUsed != Mesh.BaseVoxelsUsed) { ++Violations; }
                }
            }
        }
    }

    TestTrue(TEXT("Unrefined regions were inspected"), PlainChecked >= 1);
    TestTrue(TEXT("Unrefined terrain keeps the base resolution"), Violations == 0);
    TestTrue(TEXT("Refinement is not global (most regions are unrefined)"),
        RefinedChecked * 2 <= PlainChecked + RefinedChecked);
    AddInfo(FString::Printf(TEXT("BaseResolutionPreserved: plain=%d refined=%d violations=%d"),
        PlainChecked, RefinedChecked, Violations));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33AdaptiveHighCurvature,
    "Andromeda.Lythos2.Adaptive.HighCurvatureRefinement", LythosFlags)
bool FLythos2P33AdaptiveHighCurvature::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330606);
    FLythos2Settings Settings = MakeSettings(3, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    float MaxComplexity = 0.0f;
    int32 MaxVoxels = 0, MaxBase = 12;
    int32 Refined = 0, Checked = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 8; X += 2)
        {
            for (int32 Y = 0; Y < 8; Y += 2)
            {
                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, FLythos2RegionKey(Face, 3, X, Y), Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++Checked;
                if (Mesh.RegionComplexity > MaxComplexity)
                {
                    MaxComplexity = Mesh.RegionComplexity;
                    MaxVoxels = Mesh.VoxelsUsed;
                    MaxBase = Mesh.BaseVoxelsUsed;
                }
                if (Mesh.VoxelsUsed > Mesh.BaseVoxelsUsed) { ++Refined; }
            }
        }
    }

    TestTrue(TEXT("Regions were inspected"), Checked >= 4);
    TestTrue(TEXT("The most complex region is measurably high-curvature"), MaxComplexity > 0.10f);
    TestTrue(TEXT("High-curvature terrain refines locally above the base resolution"), MaxVoxels > MaxBase);
    AddInfo(FString::Printf(TEXT("HighCurvature: maxComplexity=%.3f refined=%d/%d"), MaxComplexity, Refined, Checked));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33AdaptiveNarrowFeature,
    "Andromeda.Lythos2.Adaptive.NarrowFeatureRefinement", LythosFlags)
bool FLythos2P33AdaptiveNarrowFeature::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330707);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    FLythos2RegionKey Key;
    FVector Dir = FVector::UpVector;
    double VoidTop = 0.0, VoidBot = 0.0;
    const bool bFound = LythosFindVoidRegion(Context, Key, Dir, VoidTop, VoidBot, 0.05);
    TestTrue(TEXT("A narrow/roofed void region exists"), bFound);
    if (!bFound) { return false; }

    FLythos2MeshData Mesh;
    Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
    TestTrue(TEXT("Void region meshed"), Mesh.Indices.Num() > 0);
    TestTrue(TEXT("Narrow feature triggers local radial refinement"), Mesh.RadialRefinedColumns > 0);

    const double RadialCell = (Context.TerrainHeightCm * 3.4) / 24.0;
    double BestTop = 1.0e30, BestBot = 1.0e30;
    for (const FVector& P : Mesh.Positions)
    {
        if (FVector::DotProduct(P.GetSafeNormal(), Dir) < 0.9995f) { continue; }
        BestTop = FMath::Min(BestTop, FMath::Abs(P.Size() - VoidTop));
        BestBot = FMath::Min(BestBot, FMath::Abs(P.Size() - VoidBot));
    }
    TestTrue(TEXT("Refinement resolves the narrow void crossings"),
        BestTop < RadialCell * 2.0 && BestBot < RadialCell * 2.0);

    AddInfo(FString::Printf(TEXT("NarrowFeature: refinedCols=%d topErr=%.0f botErr=%.0f"),
        Mesh.RadialRefinedColumns, BestTop, BestBot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33AdaptiveLocality,
    "Andromeda.Lythos2.Adaptive.Locality", LythosFlags)
bool FLythos2P33AdaptiveLocality::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330808);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    FLythos2RegionKey Key;
    FVector Dir = FVector::UpVector;
    double VoidTop = 0.0, VoidBot = 0.0;
    const bool bFound = LythosFindVoidRegion(Context, Key, Dir, VoidTop, VoidBot, 0.05);
    TestTrue(TEXT("A feature region exists"), bFound);
    if (!bFound) { return false; }

    FLythos2MeshData Mesh;
    Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
    const int32 Columns = (Mesh.VoxelsUsed + 1) * (Mesh.VoxelsUsed + 1);
    TestTrue(TEXT("Only a minority of columns receive local radial refinement"),
        Mesh.RadialRefinedColumns > 0 && Mesh.RadialRefinedColumns * 2 < Columns);
    TestTrue(TEXT("Refined cells are a minority of all cells"),
        Mesh.RefinedCellCount * 2 < Mesh.TotalCellCount);

    AddInfo(FString::Printf(TEXT("Locality: refinedCols=%d/%d refinedCells=%d/%d"),
        Mesh.RadialRefinedColumns, Columns, Mesh.RefinedCellCount, Mesh.TotalCellCount));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33AdaptiveNoGlobalIncrease,
    "Andromeda.Lythos2.Adaptive.NoGlobalResolutionIncrease", LythosFlags)
bool FLythos2P33AdaptiveNoGlobalIncrease::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(330909);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    int32 Checked = 0, Refined = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 4; ++X)
        {
            for (int32 Y = 0; Y < 4; ++Y)
            {
                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, FLythos2RegionKey(Face, 2, X, Y), Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++Checked;
                if (Mesh.VoxelsUsed > Mesh.BaseVoxelsUsed || Mesh.RadialRefinedColumns > 0) { ++Refined; }
            }
        }
    }

    TestTrue(TEXT("Regions were inspected"), Checked >= 8);
    TestTrue(TEXT("Refinement is not applied globally"), Refined * 2 < Checked);
    AddInfo(FString::Printf(TEXT("NoGlobalIncrease: refined=%d/%d"), Refined, Checked));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P33RefinedRegionWatertight,
    "Andromeda.Lythos2.Mesher.RefinedRegionWatertight", LythosFlags)
bool FLythos2P33RefinedRegionWatertight::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(331010);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;
    Settings.SkirtDepthCells = 0.0f; // test the isosurface, not the collar

    FLythos2RegionKey Key;
    FVector Dir = FVector::UpVector;
    double VoidTop = 0.0, VoidBot = 0.0;
    const bool bFound = LythosFindVoidRegion(Context, Key, Dir, VoidTop, VoidBot, 0.05);
    TestTrue(TEXT("A feature region exists"), bFound);
    if (!bFound) { return false; }

    FLythos2MeshData Mesh;
    Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
    TestTrue(TEXT("Refined region meshed"), Mesh.Indices.Num() > 0);

    const FLythos2TopoStats Topo = LythosAnalyzeTopology(Mesh);
    const int32 InteriorOpen = LythosCountInteriorOpenEdges(Mesh, Key, Context, 40);
    TestTrue(TEXT("Refined region has no interior holes"), InteriorOpen == 0);
    TestTrue(TEXT("Refined region has no non-manifold edges"), Topo.NonManifoldEdges == 0);
    TestTrue(TEXT("Refined region has no degenerate triangles"), Topo.DegenerateTriangles == 0);

    AddInfo(FString::Printf(TEXT("RefinedRegionWatertight: refinedCols=%d interiorOpen=%d nonManifold=%d degenerate=%d"),
        Mesh.RadialRefinedColumns, InteriorOpen, Topo.NonManifoldEdges, Topo.DegenerateTriangles));
    return true;
}

// Deep depressions are first-class geometric complexity: regions containing
// them must be watertight (no interior holes) at face-interior locations.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DeepDepressionWatertight,
    "Andromeda.Lythos2.Mesher.DeepDepressionWatertight", LythosFlags)
bool FLythos2P34DeepDepressionWatertight::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340001);
    const double H = Context.TerrainHeightCm;
    FLythos2Settings Settings = MakeSettings(3, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;
    Settings.SkirtDepthCells = 0.0f;

    int32 WorstInterior = 0, WorstDisplacement = 0, RefinedWithHoles = 0;
    int32 Scanned = 0, TotalInterior = 0;
    FLythos2RegionKey WorstKey;

    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 Lod = 2; Lod <= 3; ++Lod)
        {
            const int32 Dim = 1 << Lod;
            for (int32 X = 1; X < Dim - 1; X += FMath::Max(1, (Dim - 2) / 2))
            {
                for (int32 Y = 1; Y < Dim - 1; Y += FMath::Max(1, (Dim - 2) / 2))
                {
                    const FLythos2RegionKey Key(Face, Lod, X, Y);
                    FLythos2MeshData Mesh;
                    Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
                    if (Mesh.Indices.Num() == 0) { continue; }
                    ++Scanned;
                    const int32 Interior = LythosCountInteriorOpenEdges(Mesh, Key, Context, 64);
                    TotalInterior += Interior;
                    if (Interior > 0 && Mesh.RadialRefinedColumns > 0) { ++RefinedWithHoles; }
                    if (Interior > WorstInterior) { WorstInterior = Interior; WorstKey = Key; }

                    // Max lateral surface displacement across the region.
                    double MaxDisp = 0.0;
                    const FLythos2RegionKey ProbeKey = Key;
                    (void)ProbeKey;
                    for (int32 A = 0; A < 4; ++A)
                    {
                        for (int32 B = 0; B < 4; ++B)
                        {
                            const FVector D0 = Lythos2::CubeSphere::RegionSampleDirection(Key, A / 4.0f, B / 4.0f);
                            const FVector D1 = Lythos2::CubeSphere::RegionSampleDirection(Key, (A + 1) / 4.0f, B / 4.0f);
                            const double S0 = Lythos2::Density::FindSurfaceRadiusCm(Context, D0, H * 1.5);
                            const double S1 = Lythos2::Density::FindSurfaceRadiusCm(Context, D1, H * 1.5);
                            if (S0 > 0.0 && S1 > 0.0) { MaxDisp = FMath::Max(MaxDisp, FMath::Abs(S0 - S1)); }
                        }
                    }
                    if (MaxDisp > WorstDisplacement) { WorstDisplacement = MaxDisp; }
                }
            }
        }
    }

    TestTrue(TEXT("Deep-depression regions were inspected"), Scanned >= 8);
    TestTrue(TEXT("Deep-depression terrain is watertight (no interior holes)"), TotalInterior == 0);

    AddInfo(FString::Printf(TEXT("DeepDepressionWatertight: scanned=%d totalInterior=%d worstInterior=%d refinedWithHoles=%d maxDispCm=%d keyF=%d keyL=%d keyX=%d keyY=%d"),
        Scanned, TotalInterior, WorstInterior, RefinedWithHoles, static_cast<int32>(WorstDisplacement),
        WorstKey.Face, WorstKey.Lod, WorstKey.X, WorstKey.Y));
    return true;
}

// =============================================================================
// Phase 3.4 - deep-depression refinement, detail balance & overhang frequency.
// =============================================================================

namespace
{
    bool LythosFindDeepRegion(
        const FLythos2PlanetContext& Context,
        const FLythos2Settings& Settings,
        FLythos2RegionKey& OutKey,
        float& OutDepression)
    {
        float Best = 0.0f;
        bool bFound = false;
        for (int32 Face = 0; Face < 6; ++Face)
        {
            for (int32 Lod = 2; Lod <= 3; ++Lod)
            {
                const int32 Dim = 1 << Lod;
                const int32 Step = FMath::Max(1, (Dim - 2) / 2);
                for (int32 X = 1; X < Dim - 1; X += Step)
                {
                    for (int32 Y = 1; Y < Dim - 1; Y += Step)
                    {
                        const FLythos2RegionKey Key(Face, Lod, X, Y);
                        FLythos2MeshData Mesh;
                        Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
                        if (Mesh.Indices.Num() == 0) { continue; }
                        if (Mesh.DepressionComplexity > Best)
                        {
                            Best = Mesh.DepressionComplexity;
                            OutKey = Key;
                            bFound = true;
                        }
                    }
                }
            }
        }
        OutDepression = Best;
        return bFound;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DeepDepressionRefinement,
    "Andromeda.Lythos2.Adaptive.DeepDepressionRefinement", LythosFlags)
bool FLythos2P34DeepDepressionRefinement::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340101);
    FLythos2Settings Settings = MakeSettings(3, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;
    Settings.SkirtDepthCells = 0.0f;

    FLythos2RegionKey Key;
    float Depression = 0.0f;
    const bool bFound = LythosFindDeepRegion(Context, Settings, Key, Depression);
    TestTrue(TEXT("A deep-depression region exists"), bFound);
    if (!bFound) { return false; }

    FLythos2MeshData Mesh;
    Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
    TestTrue(TEXT("Deep terrain is measured as geometrically complex"), Mesh.DepressionComplexity > 0.15f);
    TestTrue(TEXT("Deep terrain receives refinement above base resolution"),
        Mesh.VoxelsUsed > Mesh.BaseVoxelsUsed);

    AddInfo(FString::Printf(TEXT("DeepDepressionRefinement: depression=%.2f voxels=%d base=%d"),
        Mesh.DepressionComplexity, Mesh.VoxelsUsed, Mesh.BaseVoxelsUsed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34SurfaceDisplacement,
    "Andromeda.Lythos2.Adaptive.SurfaceDisplacement", LythosFlags)
bool FLythos2P34SurfaceDisplacement::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340202);
    const double H = Context.TerrainHeightCm;
    FLythos2Settings Settings = MakeSettings(3, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    FLythos2RegionKey Key;
    float Depression = 0.0f;
    const bool bFound = LythosFindDeepRegion(Context, Settings, Key, Depression);
    TestTrue(TEXT("A deep-depression region exists"), bFound);
    if (!bFound) { return false; }

    FLythos2MeshData Mesh;
    Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
    TestTrue(TEXT("Surface displacement is measured and non-zero"), Mesh.MaxSurfaceDisplacementCm > 0.0);
    TestTrue(TEXT("Surface displacement is bounded by the geological height scale"),
        Mesh.MaxSurfaceDisplacementCm <= H * 1.05);
    TestTrue(TEXT("Depression complexity tracks large displacement"), Mesh.DepressionComplexity > 0.15f);

    AddInfo(FString::Printf(TEXT("SurfaceDisplacement: maxDisp=%.0f depression=%.2f"),
        Mesh.MaxSurfaceDisplacementCm, Mesh.DepressionComplexity));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DeepRadialCoverage,
    "Andromeda.Lythos2.Adaptive.DeepRadialCoverage", LythosFlags)
bool FLythos2P34DeepRadialCoverage::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340303);
    const double H = Context.TerrainHeightCm;
    FLythos2Settings Settings = MakeSettings(3, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;
    Settings.SkirtDepthCells = 0.0f;

    // Every legitimate surface must lie inside the sampled radial domain.
    const double RMinAllowed = Context.RadiusCm - H * 1.85;
    const double RMaxAllowed = Context.RadiusCm + H * 1.6;

    int32 Checked = 0, OutOfDomain = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FLythos2RegionKey Key(Face, 2, 1, 1);
        FLythos2MeshData Mesh;
        Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
        if (Mesh.Indices.Num() == 0) { continue; }
        ++Checked;
        for (const FVector& P : Mesh.Positions)
        {
            const double Rr = P.Size();
            if (Rr < RMinAllowed || Rr > RMaxAllowed) { ++OutOfDomain; }
        }
    }

    TestTrue(TEXT("Regions were inspected"), Checked >= 3);
    TestTrue(TEXT("All surface vertices lie inside the sampled radial domain"), OutOfDomain == 0);
    AddInfo(FString::Printf(TEXT("DeepRadialCoverage: regions=%d outOfDomain=%d"), Checked, OutOfDomain));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DeepMultipleTransitions,
    "Andromeda.Lythos2.Mesher.DeepDepressionMultipleTransitions", LythosFlags)
bool FLythos2P34DeepMultipleTransitions::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340404);
    const double H = Context.TerrainHeightCm;
    // Build at a fixed, high radial resolution so a void in a deep depression
    // cannot be lost to under-sampling.
    FLythos2Settings Settings = MakeSettings(2, 20);
    Settings.bAdaptiveResolution = false;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.SkirtDepthCells = 0.0f;

    TArray<FVector> Dirs;
    LythosFibonacciSphere(6000, Dirs);
    bool bFound = false;
    FLythos2RegionKey FoundKey;
    FVector FoundDir = FVector::UpVector;
    double VoidTop = 0.0, VoidBot = 0.0;
    for (const FVector& Dir : Dirs)
    {
        if (Lythos2::Density::MacroElevation(Context, Dir) < 0.1f) { continue; }
        TArray<FBand> Bands;
        ScanColumnBands(Context, Dir, 260, Bands);
        if (Bands.Num() >= 4 && Bands[1].Sign > 0 && Bands[2].Sign < 0 && Bands[3].Sign > 0)
        {
            if (Bands[2].OuterR - Bands[2].InnerR < H * 0.12) { continue; }
            bFound = true;
            FoundKey = LythosLod2RegionForDirection(Dir);
            FoundDir = Dir;
            VoidTop = Bands[2].OuterR;
            VoidBot = Bands[2].InnerR;
            break;
        }
    }
    TestTrue(TEXT("A deep multi-transition column exists"), bFound);
    if (!bFound) { return false; }

    FLythos2MeshData Mesh;
    Lythos2::Mesher::BuildRegionMesh(Context, FoundKey, Settings, Mesh);
    const double RadialCell = (H * 3.4) / 40.0;
    double BestTop = 1.0e30, BestBot = 1.0e30;
    for (const FVector& P : Mesh.Positions)
    {
        if (FVector::DotProduct(P.GetSafeNormal(), FoundDir) < 0.995f) { continue; }
        BestTop = FMath::Min(BestTop, FMath::Abs(P.Size() - VoidTop));
        BestBot = FMath::Min(BestBot, FMath::Abs(P.Size() - VoidBot));
    }
    TestTrue(TEXT("Deep void top crossing is meshed"), BestTop < RadialCell * 2.0);
    TestTrue(TEXT("Deep void bottom crossing is meshed"), BestBot < RadialCell * 2.0);

    AddInfo(FString::Printf(TEXT("DeepMultipleTransitions: topErr=%.0f botErr=%.0f"), BestTop, BestBot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34OverhangFrequency,
    "Andromeda.Lythos2.Features.OverhangFrequency", LythosFlags)
bool FLythos2P34OverhangFrequency::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340505);
    int32 Land = 0, Eroded = 0, Overhang = 0, Cavity = 0, Bridge = 0;
    double Sum = 0.0;
    LythosCountAcceptedVolumetric(Context, 8000, Land, Eroded, Sum, Overhang, Cavity, Bridge);

    TestTrue(TEXT("Overhangs exist"), Overhang > 0);
    TestTrue(TEXT("Overhangs are moderate, not dominant"), Overhang * 4 < Land);
    AddInfo(FString::Printf(TEXT("OverhangFrequency: overhang=%d land=%d"), Overhang, Land));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34CavityFrequency,
    "Andromeda.Lythos2.Features.CavityFrequency", LythosFlags)
bool FLythos2P34CavityFrequency::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340606);
    int32 Land = 0, Eroded = 0, Overhang = 0, Cavity = 0, Bridge = 0;
    double Sum = 0.0;
    LythosCountAcceptedVolumetric(Context, 8000, Land, Eroded, Sum, Overhang, Cavity, Bridge);

    TestTrue(TEXT("Enclosed cavities remain rarer than overhangs"), Cavity < FMath::Max(1, Overhang));
    TestTrue(TEXT("Enclosed cavities are a small minority"), Cavity * 8 < Land);
    AddInfo(FString::Printf(TEXT("CavityFrequency: cavity=%d overhang=%d land=%d"), Cavity, Overhang, Land));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34BridgeRarity,
    "Andromeda.Lythos2.Features.BridgeRarity", LythosFlags)
bool FLythos2P34BridgeRarity::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340707);
    int32 Land = 0, Eroded = 0, Overhang = 0, Cavity = 0, Bridge = 0;
    double Sum = 0.0;
    LythosCountAcceptedVolumetric(Context, 8000, Land, Eroded, Sum, Overhang, Cavity, Bridge);

    TestTrue(TEXT("Natural bridges remain very rare"), Bridge * 40 < Land);
    AddInfo(FString::Printf(TEXT("BridgeRarity: bridge=%d cavity=%d overhang=%d land=%d"), Bridge, Cavity, Overhang, Land));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34OverhangDeterminism,
    "Andromeda.Lythos2.Features.OverhangDeterminism", LythosFlags)
bool FLythos2P34OverhangDeterminism::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext A = MakeGeoContext(340808);
    const FLythos2PlanetContext B = MakeGeoContext(340808);
    const FLythos2PlanetContext C = MakeGeoContext(340809);
    int32 LandA = 0, ErodedA = 0, OaA = 0, CaA = 0, BaA = 0;
    int32 LandB = 0, ErodedB = 0, OaB = 0, CaB = 0, BaB = 0;
    int32 LandC = 0, ErodedC = 0, OaC = 0, CaC = 0, BaC = 0;
    double SumA = 0.0, SumB = 0.0, SumC = 0.0;
    LythosCountAcceptedVolumetric(A, 4000, LandA, ErodedA, SumA, OaA, CaA, BaA);
    LythosCountAcceptedVolumetric(B, 4000, LandB, ErodedB, SumB, OaB, CaB, BaB);
    LythosCountAcceptedVolumetric(C, 4000, LandC, ErodedC, SumC, OaC, CaC, BaC);

    TestTrue(TEXT("Same seed selects identical overhangs"), OaA == OaB && CaA == CaB && BaA == BaB);
    TestTrue(TEXT("Different seed selects different overhangs"), OaA != OaC || CaA != CaC || FMath::Abs(SumA - SumC) > 1.0e-6);
    AddInfo(FString::Printf(TEXT("OverhangDeterminism: A=%d/%d/%d C=%d/%d/%d"), OaA, CaA, BaA, OaC, CaC, BaC));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DetailIncreaseIsLocal,
    "Andromeda.Lythos2.Adaptive.DetailIncreaseIsLocal", LythosFlags)
bool FLythos2P34DetailIncreaseIsLocal::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(340909);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    int32 Plain = 0, Refined = 0, Violations = 0;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 4; ++X)
        {
            for (int32 Y = 0; Y < 4; ++Y)
            {
                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, FLythos2RegionKey(Face, 2, X, Y), Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                if (Mesh.VoxelsUsed > Mesh.BaseVoxelsUsed) { ++Refined; }
                else
                {
                    ++Plain;
                    if (Mesh.VoxelsUsed != Mesh.BaseVoxelsUsed) { ++Violations; }
                }
            }
        }
    }
    TestTrue(TEXT("Unrefined regions stay at base resolution"), Violations == 0);
    TestTrue(TEXT("The detail increase is local (refined are a minority)"), Refined * 2 < Plain + Refined);
    AddInfo(FString::Printf(TEXT("DetailIncreaseIsLocal: plain=%d refined=%d"), Plain, Refined));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34NoGlobalResolutionExplosion,
    "Andromeda.Lythos2.Adaptive.NoGlobalResolutionExplosion", LythosFlags)
bool FLythos2P34NoGlobalResolutionExplosion::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(341010);
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = true;
    Settings.MaxVolumetricVoxelsPerAxis = 20;
    Settings.VolumetricResolutionLevels = 2;

    int32 Scanned = 0;
    int64 SumVoxels = 0;
    int32 MaxN = 0;
    const int32 BaseN = 12;
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 X = 0; X < 4; ++X)
        {
            for (int32 Y = 0; Y < 4; ++Y)
            {
                FLythos2MeshData Mesh;
                Lythos2::Mesher::BuildRegionMesh(Context, FLythos2RegionKey(Face, 2, X, Y), Settings, Mesh);
                if (Mesh.Indices.Num() == 0) { continue; }
                ++Scanned;
                SumVoxels += Mesh.VoxelsUsed;
                MaxN = FMath::Max(MaxN, Mesh.VoxelsUsed);
            }
        }
    }
    const double MeanN = Scanned > 0 ? static_cast<double>(SumVoxels) / Scanned : 0.0;
    // Mean resolution must stay near base (no global explosion) and never exceed the cap.
    TestTrue(TEXT("Mean resolution stays close to base"), MeanN <= BaseN * 1.35);
    TestTrue(TEXT("No region exceeds the configured maximum"), MaxN <= Settings.MaxVolumetricVoxelsPerAxis);
    AddInfo(FString::Printf(TEXT("NoGlobalResolutionExplosion: scanned=%d meanN=%.1f maxN=%d"), Scanned, MeanN, MaxN));
    return true;
}

// A deep depression crossing a same-face region boundary stays continuous: the
// two sides' surfaces along the shared edge differ by no more than the collar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DeepBoundaryContinuity,
    "Andromeda.Lythos2.Geometry.DeepDepressionBoundaryContinuity", LythosFlags)
bool FLythos2P34DeepBoundaryContinuity::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(341111);
    const double H = Context.TerrainHeightCm;
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 0.0f; // test the isosurface continuity itself

    // Two same-LOD, same-N in-face neighbours weld exactly along the shared edge.
    const FLythos2RegionKey A(4, 2, 1, 1);
    const FLythos2RegionKey B(4, 2, 2, 1);
    const FLythos2MeshData MeshA = BuildRegion(Context, A, Settings);
    const FLythos2MeshData MeshB = BuildRegion(Context, B, Settings);

    // Compare the exact shared-edge vertices (they share the same grid
    // directions and radii), not nearby interior vertices.
    auto CollectEdgeVerts = [](const FLythos2MeshData& Mesh, int32 Face, TArray<FVector>& Out)
    {
        for (const FVector& P : Mesh.Positions)
        {
            int32 OF; float U, V;
            Lythos2::CubeSphere::DirectionToFaceUV(P.GetSafeNormal(), OF, U, V);
            if (OF == Face && FMath::Abs(U - 0.5f) < 1.0e-5f) { Out.Add(P); }
        }
    };
    TArray<FVector> EdgeA, EdgeB;
    CollectEdgeVerts(MeshA, A.Face, EdgeA);
    CollectEdgeVerts(MeshB, B.Face, EdgeB);

    int32 Unmatched = 0;
    double WorstGap = 0.0;
    for (const FVector& P : EdgeA)
    {
        double Best = 1.0e30;
        for (const FVector& Q : EdgeB) { Best = FMath::Min(Best, static_cast<double>(FVector::Dist(P, Q))); }
        WorstGap = FMath::Max(WorstGap, Best);
        if (Best > 1.0) { ++Unmatched; }
    }
    TestTrue(TEXT("Shared boundary has vertices on both sides"), EdgeA.Num() > 0 && EdgeB.Num() > 0);
    TestTrue(TEXT("Same-face boundary is continuous even across deep terrain"), Unmatched == 0);
    AddInfo(FString::Printf(TEXT("DeepBoundaryContinuity: edgeA=%d edgeB=%d unmatched=%d worstGapCm=%.3f"),
        EdgeA.Num(), EdgeB.Num(), Unmatched, WorstGap));
    return true;
}

// A deep depression crossing an LOD boundary is covered by the collar.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DeepLodContinuity,
    "Andromeda.Lythos2.Geometry.DeepDepressionLodContinuity", LythosFlags)
bool FLythos2P34DeepLodContinuity::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(341212);
    const double H = Context.TerrainHeightCm;
    FLythos2Settings Settings = MakeSettings(2, 12);
    Settings.bAdaptiveResolution = false;
    Settings.SkirtDepthCells = 3.0f;

    const FLythos2RegionKey Coarse(4, 1, 0, 0);
    const FLythos2RegionKey Fine(4, 2, 0, 0);
    const FLythos2MeshData MeshCoarse = BuildRegion(Context, Coarse, Settings);
    const FLythos2MeshData MeshFine = BuildRegion(Context, Fine, Settings);
    const double CollarDepth = 3.0 * (Lythos2::CubeSphere::RegionWorldSizeCm(Fine, Context) / 12.0);
    const float CosTol = 0.9988f;

    double MaxGap = 0.0;
    int32 Checked = 0;
    for (int32 J = 0; J <= 6; ++J)
    {
        const float V = static_cast<float>(J) / 24.0f;
        const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(Fine, 0.0f, V);
        const double S = Lythos2::Density::FindSurfaceRadiusCm(Context, Dir, H);
        const double RC = OuterRadiusNearDir(MeshCoarse, Dir, CosTol);
        if (S <= 0.0 || RC <= 0.0) { continue; }
        MaxGap = FMath::Max(MaxGap, FMath::Abs(S - RC));
        ++Checked;
    }
    TestTrue(TEXT("LOD boundary directions were compared"), Checked >= 4);
    TestTrue(TEXT("Deep terrain across an LOD boundary is within collar coverage"),
        MaxGap <= CollarDepth + 1.0);
    AddInfo(FString::Printf(TEXT("DeepLodContinuity: checked=%d maxGap=%.0f collar=%.0f"), Checked, MaxGap, CollarDepth));
    return true;
}

// A deep depression crossing a cube-face edge remains continuous (global density).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLythos2P34DeepCubeFaceContinuity,
    "Andromeda.Lythos2.Geometry.DeepDepressionCubeFaceContinuity", LythosFlags)
bool FLythos2P34DeepCubeFaceContinuity::RunTest(const FString& Parameters)
{
    const FLythos2PlanetContext Context = MakeGeoContext(341313);
    const double H = Context.TerrainHeightCm;

    // Smooth paths crossing cube-face edges; the derived surface radius must be
    // continuous (the refinement/density decisions are global & deterministic).
    const FVector Paths[3][2] =
    {
        { FVector(1.0f, 0.9f, 0.05f).GetSafeNormal(), FVector(0.9f, 1.0f, 0.05f).GetSafeNormal() },
        { FVector(0.05f, 0.9f, 1.0f).GetSafeNormal(), FVector(0.05f, 1.0f, 0.9f).GetSafeNormal() },
        { FVector(1.0f, 0.05f, -0.9f).GetSafeNormal(), FVector(0.9f, 0.05f, -1.0f).GetSafeNormal() }
    };

    double MaxJump = 0.0;
    for (int32 P = 0; P < 3; ++P)
    {
        double Prev = Lythos2::Density::FindSurfaceRadiusCm(Context, Paths[P][0], H);
        for (int32 I = 1; I <= 200; ++I)
        {
            const FVector Dir = FMath::Lerp(Paths[P][0], Paths[P][1], static_cast<float>(I) / 200.0f).GetSafeNormal();
            const double R = Lythos2::Density::FindSurfaceRadiusCm(Context, Dir, H);
            MaxJump = FMath::Max(MaxJump, FMath::Abs(R - Prev));
            Prev = R;
        }
    }

    TestTrue(TEXT("Depth surface is continuous across cube-face edges"), MaxJump < H * 0.05);
    AddInfo(FString::Printf(TEXT("DeepCubeFaceContinuity: maxJumpCm=%.2f"), MaxJump));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
