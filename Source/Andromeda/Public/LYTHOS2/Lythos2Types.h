#pragma once

// =============================================================================
// LYTHOS 2.0 - Core types.
//
// LYTHOS 2.0 is a whole-planet, fully volumetric (density based) terrain
// system. The authoritative terrain is a deterministic scalar density field
// evaluated in planetary coordinates. Meshes are DERIVED products.
//
// The architecture is intentionally edit-ready:
//
//      Procedural Base Density  +  Future Persistent Voxel Modifications
//
// No editing is implemented in this phase, but the density evaluator is the
// single seam where future voxel edits will be injected.
// =============================================================================

#include "CoreMinimal.h"
#include "Lythos2Types.generated.h"

/**
 * Stable identity of a terrain region on the planetary cube-sphere.
 *
 * At LOD L a cube face is divided into 2^L x 2^L cells. LOD 0 is a single
 * cell per face (planetary scale). Each increase in LOD subdivides every cell
 * into 2x2 children, so a child region refines the SAME underlying terrain as
 * its parent. Region identity is purely integer + deterministic.
 */
struct FLythos2RegionKey
{
    int64 PlanetID = 0;
    int32 Face = 0;
    int32 Lod = 0;
    int32 X = 0;
    int32 Y = 0;

    FLythos2RegionKey() = default;
    FLythos2RegionKey(int32 InFace, int32 InLod, int32 InX, int32 InY)
        : Face(InFace), Lod(InLod), X(InX), Y(InY)
    {
    }

    bool operator==(const FLythos2RegionKey& Other) const
    {
        return PlanetID == Other.PlanetID
            && Face == Other.Face && Lod == Other.Lod
            && X == Other.X && Y == Other.Y;
    }

    bool operator!=(const FLythos2RegionKey& Other) const
    {
        return !(*this == Other);
    }

    bool IsValid() const
    {
        if (Face < 0 || Face > 5 || Lod < 0)
        {
            return false;
        }
        const int32 Side = 1 << Lod;
        return X >= 0 && X < Side && Y >= 0 && Y < Side;
    }

    /** Number of cells per face axis at this LOD. */
    int32 GetSide() const { return 1 << Lod; }

    /** The parent region one LOD coarser, or an invalid key at LOD 0. */
    FLythos2RegionKey GetParent() const
    {
        FLythos2RegionKey Parent(Face, Lod <= 0 ? -1 : Lod - 1, 0, 0);
        Parent.PlanetID = PlanetID;
        if (Lod > 0)
        {
            Parent.X = X >> 1;
            Parent.Y = Y >> 1;
        }
        return Parent;
    }

    bool IsAncestorOf(const FLythos2RegionKey& Other) const
    {
        if (PlanetID != Other.PlanetID || Face != Other.Face || Lod >= Other.Lod)
        {
            return false;
        }
        const int32 Shift = Other.Lod - Lod;
        return (Other.X >> Shift) == X && (Other.Y >> Shift) == Y;
    }

    friend uint32 GetTypeHash(const FLythos2RegionKey& Key)
    {
        uint32 Hash = ::GetTypeHash(Key.PlanetID);
        Hash = HashCombine(Hash, ::GetTypeHash(Key.Face));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.Lod));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.X));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.Y));
        return Hash;
    }
};

/**
 * Fully deterministic, stateless description of a planet's terrain.
 * The same context always produces the same terrain regardless of thread,
 * order, region, LOD or cache state.
 */
struct FLythos2PlanetContext
{
    int64 PlanetID = 0;
    int64 Seed = 0;

    /** Reference (sea level) radius in cm. */
    float RadiusCm = 500000.0f;

    /** Maximum terrain relief above the reference radius in cm. */
    float TerrainHeightCm = 20000.0f;

    /** Authoring character knobs (kept from the planet profile). */
    float ContinentalScale = 0.5f;
    float MountainScale = 3.0f;
    float DetailScale = 12.0f;
    float MountainStrength = 1.5f;
    float DetailStrength = 0.1f;

    int32 Archetype = 0;

    /**
     * Strength of the volumetric (3D position dependent) density term.
     * 0 = smooth radial surface (phase 1 default). Positive values allow
     * overhangs and caves; the density field, mesher and streaming all
     * support this without any architectural change.
     */
    float VolumetricDetailAmount = 0.0f;

    /**
     * Phase 3 geomorphology weight. 0 = pure Phase 2 macro envelope (used by
     * the macro/orientation tests); 1 = full erosion/drainage/volumetric
     * geology. Does not change the LargeScale macro shape.
     */
    float GeologyAmount = 0.0f;

    /** Deterministic precipitation/erosion proxy in [0,1] (future climate seam). */
    float ClimateProxy = 0.5f;

    /**
     * Phase 3.1 slope-response weight in [0,1]. Deterministic geomorphological
     * approximation of talus / debris accumulation at cliff and gully bases.
     * 0 = no slope response, 1 = full restrained infill.
     */
    float TalusAmount = 0.6f;

    /** World pose of the planet (used only for streaming bookkeeping). */
    FVector WorldPosition = FVector::ZeroVector;

    bool IsValid() const
    {
        return RadiusCm > 1.0f && TerrainHeightCm > 0.0f && FMath::IsFinite(RadiusCm);
    }
};

/** Derived triangle mesh for a single terrain region. Positions are planet-local (cm). */
struct FLythos2MeshData
{
    TArray<FVector> Positions;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FColor> Colors;
    TArray<FVector2D> UVs;

    /** Profiling: milliseconds spent sampling density / extracting the mesh. */
    double DensityMs = 0.0;
    double MeshingMs = 0.0;

    /** Adaptive resolution actually used to build this region. */
    int32 VoxelsUsed = 0;

    // Phase 3.3 adaptive-refinement telemetry.
    int32 BaseVoxelsUsed = 0;         // base resolution before refinement
    int32 RefinementLevels = 0;       // 0 = not refined, 1/2 = local refinement steps
    int32 TotalCellCount = 0;         // candidate surface-adjacent cells
    int32 RefinedCellCount = 0;       // cells that were locally refined
    int32 RadialRefinedColumns = 0;   // columns that received local radial refinement
    float RegionComplexity = 0.0f;    // measured geometric complexity in [0,1]

    // Phase 3.4 deep-depression telemetry.
    float DepressionComplexity = 0.0f;      // normalized elevation-drop complexity
    double MaxSurfaceDisplacementCm = 0.0;  // largest lateral surface drop in the region
    int32 DeepRefinedColumns = 0;           // columns refined because of a deep drop
    double RadialRangeUsedCm = 0.0;         // (RMax - RMin) actually sampled

    void Reset()
    {
        Positions.Reset();
        Indices.Reset();
        Normals.Reset();
        Colors.Reset();
        UVs.Reset();
        DensityMs = 0.0;
        MeshingMs = 0.0;
        VoxelsUsed = 0;
        BaseVoxelsUsed = 0;
        RefinementLevels = 0;
        TotalCellCount = 0;
        RefinedCellCount = 0;
        RadialRefinedColumns = 0;
        RegionComplexity = 0.0f;
        DepressionComplexity = 0.0f;
        MaxSurfaceDisplacementCm = 0.0;
        DeepRefinedColumns = 0;
        RadialRangeUsedCm = 0.0;
    }

    bool IsEmpty() const { return Indices.Num() == 0; }
};

/** Build / lifecycle state of a region as tracked by the streamer. */
enum class ELythos2RegionState : uint8
{
    Desired,
    Building,
    Ready,
    Active,
    Failed
};

/**
 * Runtime tuning for LYTHOS 2.0. Everything that bounds work lives here so
 * that increasing MaxTerrainLOD later cannot introduce unbounded work.
 */
USTRUCT(BlueprintType)
struct ANDROMEDA_API FLythos2Settings
{
    GENERATED_BODY()

    /** Number of voxel cells per region axis. Samples = Cells + 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "4", ClampMax = "32"))
    int32 VoxelsPerAxis = 12;

    /**
     * TEMPORARY development/performance limit. The hierarchy already supports
     * more levels; raising this simply allows deeper refinement. Never hardcoded
     * elsewhere.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "0", ClampMax = "12"))
    int32 MaxTerrainLOD = 4;

    /** A region subdivides while the viewer is within ChunkWorldSize * this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "0.5"))
    float LodDetailFactor = 2.5f;

    /** Fractional band that must be crossed before an LOD switch, to stop thrashing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float LodHysteresis = 0.15f;

    /** Maximum active/meshed regions per planet. Hard bound on memory + work. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "6"))
    int32 MaxActiveRegions = 320;

    /** Maximum region builds queued at once (global). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "1"))
    int32 MaxQueuedBuilds = 64;

    /** Maximum region builds dispatched per tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "1"))
    int32 MaxBuildsDispatchedPerTick = 8;

    /** Maximum finished regions uploaded to render/sections per tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "1"))
    int32 MaxUploadsPerTick = 6;

    /** Regions beyond this distance from the viewer (relative to radius) unload. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "1.0"))
    float MaxRelevantRadiusScale = 12.0f;

    /**
     * Phase 3.1 transition-collar depth in local voxel sizes. The collar is a
     * density-conforming boundary strip generated by the finer region; it
     * bridges same-LOD, adaptive-resolution and LOD seams. 0 disables it.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "0.0"))
    float SkirtDepthCells = 3.0f;

    /** Fraction of TerrainHeight sampled below the reference radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "0.0"))
    float RadialBelowScale = 1.0f;

    /** Fraction of TerrainHeight sampled above the reference radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "0.05"))
    float RadialAboveScale = 1.3f;

    /** Extra radial margin as a fraction of TerrainHeight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2", meta = (ClampMin = "0.0"))
    float RadialMarginScale = 0.2f;

    /**
     * Enable volumetric overhang/cave contribution of the 3D density field.
     * Off in phase 1 (no editing), but the density evaluator and mesher fully
     * support it.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2")
    bool bEnableVolumetricDetail = false;

    // =====================================================================
    // Phase 3 geomorphology: erosion, drainage, differential resistance and
    // true volumetric topology (overhangs, arches, natural bridges, caves).
    // =====================================================================

    /** Master weight for the Phase 3 geology layer (0 disables it). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float GeologyAmount = 1.0f;

    /** Deterministic precipitation/erosion proxy in [0,1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ClimateProxy = 0.5f;

    /**
     * Phase 3.1 slope response (talus / debris accumulation) weight in [0,1].
     * Softens over-steep walls and partially infills narrow gullies.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TalusAmount = 0.6f;

    /** Enable per-region feature-aware resolution (adaptive volumentric LOD). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology")
    bool bAdaptiveResolution = true;

    /** Maximum local voxel resolution allowed for complex/volumetric regions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "4", ClampMax = "32"))
    int32 MaxVolumetricVoxelsPerAxis = 20;

    /**
     * Phase 3.1 radial sample oversampling for geomorphology. The radial band
     * is much larger than the lateral cell, so overhangs/arches/bridges need
     * extra radial samples to be visible. Uniform across all regions at the
     * same N, so same-resolution neighbours still weld exactly. 1 = off.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "1.0", ClampMax = "3.0"))
    float GeologyRadialOversample = 2.0f;

    /** Number of adaptive resolution steps between base and maximum. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "0", ClampMax = "4"))
    int32 VolumetricResolutionLevels = 2;

    /**
     * Phase 3.3: locally redistribute a column's radial samples onto its own
     * surface / narrow layers when that column is geometrically complex. Keeps
     * ordinary terrain at base resolution while filling narrow voids and steep
     * curvature with small triangles only where needed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology")
    bool bLocalRadialRefinement = true;

    // =====================================================================
    // Phase 3.6 constrained, topology-aware smoothing of the extracted mesh.
    // Interior vertices are smoothed with a bilateral (normal-similarity)
    // filter; domain-boundary vertices are frozen so cross-region welds and the
    // density-conforming collar stay exact. Connectivity is unchanged, so
    // overhangs/alcoves/cavity roofs and floors are preserved.
    // =====================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology")
    bool bSmoothExtractedMesh = true;

    /** Per-iteration blend of a vertex toward its filtered position [0,1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MeshSmoothingStrength = 0.40f;

    /** Number of smoothing iterations. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Geology", meta = (ClampMin = "0", ClampMax = "8"))
    int32 MeshSmoothingIterations = 2;

    // =====================================================================
    // Phase 1.1 runtime budgets. These are what keep close-range streaming
    // responsive: work is bounded independently for generation, meshing,
    // completed backlog, uploads, collision, swaps and unloads.
    // =====================================================================

    /** Maximum async mesh-generation tasks allowed in flight at once. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Budgets", meta = (ClampMin = "1"))
    int32 MaxConcurrentMeshTasks = 6;

    /** Maximum completed-but-not-uploaded meshes held in the backlog. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Budgets", meta = (ClampMin = "1"))
    int32 MaxCompletedMeshBacklog = 24;

    /**
     * Soft wall-clock budget (ms) for Game Thread mesh uploads per tick.
     * Uploads are skipped once exceeded; 0 disables the time budget.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Budgets", meta = (ClampMin = "0.0"))
    float MaxUploadMillisecondsPerTick = 2.5f;

    /** Maximum regions removed/unloaded per tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Budgets", meta = (ClampMin = "1"))
    int32 MaxRemovalsPerTick = 16;

    /** Maximum number of per-tick LOD swaps (region activations). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Budgets", meta = (ClampMin = "1"))
    int32 MaxSwapsPerTick = 8;

    /** Enable terrain collision. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Collision")
    bool bEnableCollision = true;

    /**
     * Only regions within (Radius * this) of the viewer receive collision.
     * This keeps the per-cook collision bounded to the player's neighbourhood
     * instead of the whole planet (including the huge LOD-0 roots).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Collision", meta = (ClampMin = "0.01"))
    float CollisionDistanceScale = 0.05f;

    /** Minimum ticks between collision recooks while streaming (throttle). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Collision", meta = (ClampMin = "1"))
    int32 CollisionUpdateIntervalTicks = 8;

    /**
     * Phase 3.6: streamed collision is cooked synchronously for determinism
     * (async cooks could apply stale/empty geometry after a newer mesh was
     * installed). Kept configurable for future backends.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2|Collision")
    bool bUseAsyncCollisionCooking = false;

    // =====================================================================
    // Telemetry (written by the streamer; read by tests/debug tooling).
    // =====================================================================

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastActiveRegionCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastSelectedRegionCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastPendingBuildCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastGeneratingRegionCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastReadyRegionCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastStaleRejectedCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastUploadsThisTick = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastRemovalsThisTick = 0;

    /** Active regions per LOD index [0..MaxTerrainLOD]. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    TArray<int32> LastActiveRegionsByLOD;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastSelectMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastSchedulerMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastDensityMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastMeshingMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastUploadMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastCollisionMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastRemovalMs = 0.0f;

    /** Game-Thread LYTHOS cost for the last tick (selection + scheduler + upload + unload). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastTickWorkMs = 0.0f;

    /** Off-thread worker cost of builds completed this tick (density + meshing). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    float LastWorkerMs = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 TotalRegionsGenerated = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 TotalStaleRejected = 0;

    /** Render workload: applied sections / vertices / triangles across all planets. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastActiveSectionCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int64 LastActiveVertexCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int64 LastActiveTriangleCount = 0;

    /** Vertices uploaded to the render mesh during the last tick. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastUploadVertexCount = 0;

    /** Phase 3: highest local voxel resolution used by any applied region. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastMaxVoxelsUsed = 0;

    /** Phase 3: number of active regions that used adaptive high resolution. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LYTHOS2|Stats")
    int32 LastVolumetricRegionCount = 0;
};
