#pragma once

// =============================================================================
// LYTHOS 2.0 - World streaming subsystem.
//
// Global, LOD-driven terrain streamer. Each tick it:
//   1. finds the viewer,
//   2. computes the desired region partition (per planet, bounded),
//   3. prioritises near/high-relevance regions,
//   4. dispatches bounded asynchronous density + mesh builds,
//   5. uploads finished geometry on a bounded amount of game-thread work,
//   6. keeps the old valid LOD visible until the replacement is ready,
//   7. unloads regions that are no longer relevant.
//
// No synchronous full-planet generation ever happens: only the 6 LOD-0
// planetary roots are built synchronously (a bounded, coarse collision seed).
// =============================================================================

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Async/AsyncWork.h"
#include "LYTHOS2/Lythos2Types.h"
#include "LYTHOS2/Lythos2Mesher.h"
#include "LYTHOS2/Lythos2Scheduler.h"
#include "Lythos2WorldSubsystem.generated.h"

class APlanet;

/**
 * Background terrain build task: evaluates the deterministic density field and
 * extracts the derived mesh. Touches no shared mutable state, so it is safe on
 * the thread pool and order independent.
 */
class FLythos2BuildTask : public FNonAbandonableTask
{
    friend class FAsyncTask<FLythos2BuildTask>;

public:
    FLythos2BuildTask(
        const FLythos2PlanetContext& InContext,
        const FLythos2RegionKey& InKey,
        const FLythos2Settings& InSettings)
        : Context(InContext)
        , Key(InKey)
        , Settings(InSettings)
    {
    }

    void DoWork()
    {
        Lythos2::Mesher::BuildRegionMesh(Context, Key, Settings, Mesh);
        bBuilt = true;
    }

    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FLythos2BuildTask, STATGROUP_ThreadPoolAsyncTasks);
    }

    FLythos2PlanetContext Context;
    FLythos2RegionKey Key;
    FLythos2Settings Settings;
    FLythos2MeshData Mesh;
    bool bBuilt = false;
};

UCLASS()
class ANDROMEDA_API ULythos2WorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    // --- USubsystem / UTickableWorldSubsystem -----------------------------
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    // --- Planet registration (called by APlanet) --------------------------
    void RegisterPlanet(APlanet* Planet);
    void UnregisterPlanet(APlanet* Planet);

    /** Configurable runtime budgets / limits (editable in the editor). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LYTHOS2")
    FLythos2Settings Settings;

    /** Test / debug viewer override. When enabled it replaces the player view. */
    void SetViewerWorldOverride(bool bEnabled, const FVector& WorldLocation);
    bool IsViewerOverrideEnabled() const { return bViewerOverride; }

    int32 GetActiveRegionCount() const { return Scheduler.GetActiveCount(); }
    int32 GetPendingBuildCount() const { return Scheduler.GetBuildingCount() + Scheduler.GetQueueCount(); }
    int32 GetSelectedRegionCount() const { return LastSelectedCount; }
    int32 GetRegisteredPlanetCount() const { return Planets.Num(); }

    /** Number of currently applied (non-empty) terrain mesh sections across all planets. */
    int32 GetAppliedMeshCount() const { return AppliedMeshCount; }

    /** Completed-but-not-yet-uploaded meshes waiting in the backlog. */
    int32 GetReadyBacklogCount() const { return ReadyMeshes.Num(); }

    /** Async mesh-generation tasks currently in flight. */
    int32 GetInFlightTaskCount() const { return Tasks.Num(); }

    int64 GetAppliedVertexCount() const { return AppliedVertices; }
    int64 GetAppliedTriangleCount() const { return AppliedTriangles; }

    const TSet<FLythos2RegionKey>& GetActiveRegions() const { return Scheduler.GetActiveRegions(); }

private:
    struct FPlanetEntry
    {
        TWeakObjectPtr<APlanet> Planet;
        FLythos2PlanetContext Context;
        TMap<FLythos2RegionKey, int32> SectionByRegion;
        TSet<int32> UsedSections;

        /** Viewer position in this planet's local space (streaming priority). */
        FVector LastViewerLocal = FVector::ZeroVector;
        bool bHasViewer = false;

        /** Nodes that had children last tick (LOD hysteresis). */
        TSet<FLythos2RegionKey> PreviouslyRefinedNodes;

        /** Sections currently flagged for collision (bounded to the viewer). */
        TSet<FLythos2RegionKey> CollisionRegions;
        bool bCollisionDirty = false;
        int32 TicksSinceCollisionCook = 0;

        /** Applied mesh size per region (X=vertices, Y=triangles). */
        TMap<FLythos2RegionKey, FIntPoint> MeshSizeByRegion;
    };

    FPlanetEntry* FindEntry(int64 PlanetID);

    bool GetViewerWorldLocation(FVector& OutWorld) const;
    bool GetViewerLocalForEntry(const FPlanetEntry& Entry, FVector& OutLocal) const;

    void SeedCoarsePlanet(APlanet* Planet, FPlanetEntry& Entry);
    void DispatchBuilds();
    void PollBuilds();
    void ApplyActivations();
    void ApplyRemovals();
    void BeginCollisionBatch();
    void EndCollisionBatch();
    void ApplyMeshToEntry(FPlanetEntry& Entry, const FLythos2RegionKey& Key, FLythos2MeshData&& Mesh);
    void RemoveMeshFromEntry(FPlanetEntry& Entry, const FLythos2RegionKey& Key);
    bool ShouldRegionHaveCollision(const FLythos2RegionKey& Key, const FPlanetEntry& Entry) const;
    void PublishStats(double SelectMs, double SchedulerMs);

    TArray<FPlanetEntry> Planets;
    TMap<FLythos2RegionKey, FLythos2MeshData> ReadyMeshes;
    TMap<FLythos2RegionKey, TUniquePtr<FAsyncTask<FLythos2BuildTask>>> Tasks;

    FLythos2StreamScheduler Scheduler;

    bool bViewerOverride = false;
    FVector ViewerOverrideWorld = FVector::ZeroVector;

    int32 AppliedMeshCount = 0;
    int64 AppliedVertices = 0;
    int64 AppliedTriangles = 0;
    int32 LastSelectedCount = 0;

    // Per-tick telemetry accumulators.
    double TickDensityMs = 0.0;
    double TickMeshingMs = 0.0;
    double TickUploadMs = 0.0;
    double TickCollisionMs = 0.0;
    double TickRemovalMs = 0.0;
    int32 UploadsThisTick = 0;
    int32 UploadVerticesThisTick = 0;
    int32 StaleRejectedThisTick = 0;
    int32 RemovalsThisTick = 0;
};
