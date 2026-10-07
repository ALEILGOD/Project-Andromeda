#pragma once

// =============================================================================
// LYTHOS 2.0 - Bounded streaming scheduler (pure, testable).
//
// Owns the lifecycle of every region (desired -> building -> ready -> active)
// and guarantees:
//   * the build queue is bounded by MaxQueuedBuilds;
//   * the active set is bounded by MaxActiveRegions;
//   * a region is never removed before a ready replacement covers its area,
//     so LOD transitions cannot open holes;
//   * viewer movement cannot create an unbounded request queue.
// =============================================================================

#include "CoreMinimal.h"
#include "LYTHOS2/Lythos2Types.h"

class ANDROMEDA_API FLythos2StreamScheduler
{
public:
    void Configure(const FLythos2Settings& InSettings) { Settings = InSettings; }

    const FLythos2Settings& GetSettings() const { return Settings; }

    void Reset();

    // --- Desired set update (once per streamer tick) ----------------------
    void BeginDesiredUpdate();
    void AddDesired(const FLythos2RegionKey& Key, double Priority = 0.0);
    void CommitDesiredUpdate();

    /** Capture the previous desired set (call before BeginDesiredUpdate). */
    void CapturePreviousDesired();

    // --- Request queue ----------------------------------------------------
    bool PopRequest(FLythos2RegionKey& OutKey);
    int32 GetQueueCount() const { return RequestQueue.Num(); }
    const TArray<FLythos2RegionKey>& GetRequestQueue() const { return RequestQueue; }

    // --- Lifecycle callbacks ----------------------------------------------
    void MarkBuilding(const FLythos2RegionKey& Key);
    void MarkReady(const FLythos2RegionKey& Key);
    void MarkFailed(const FLythos2RegionKey& Key);

    /** Move ready regions into the active set, bounded by MaxActivations. */
    int32 ActivateReady(int32 MaxActivations);

    /** Activate one ready+desired region (used with budgeted apply-and-swap). */
    bool Activate(const FLythos2RegionKey& Key);

    /** Active regions that are safe to remove (replacements active / none needed). */
    void ComputeRemovals(TArray<FLythos2RegionKey>& OutRemovals) const;
    void RemoveRegion(const FLythos2RegionKey& Key);

    // --- Queries ----------------------------------------------------------
    bool IsActive(const FLythos2RegionKey& Key) const { return ActiveRegions.Contains(Key); }
    bool IsDesired(const FLythos2RegionKey& Key) const { return Desired.Contains(Key); }
    int32 GetActiveCount() const { return ActiveRegions.Num(); }
    int32 GetReadyCount() const { return ReadyRegions.Num(); }
    int32 GetBuildingCount() const { return BuildingRegions.Num(); }
    int32 GetDesiredCount() const { return Desired.Num(); }
    double GetPriority(const FLythos2RegionKey& Key) const;
    const TSet<FLythos2RegionKey>& GetActiveRegions() const { return ActiveRegions; }
    const TSet<FLythos2RegionKey>& GetDesiredRegions() const { return Desired; }
    const TSet<FLythos2RegionKey>& GetPreviousDesiredRegions() const { return PreviousDesired; }

private:
    FLythos2Settings Settings;

    TSet<FLythos2RegionKey> Desired;
    TArray<FLythos2RegionKey> DesiredOrder;
    TMap<FLythos2RegionKey, double> DesiredPriority;

    TSet<FLythos2RegionKey> PreviousDesired;

    TSet<FLythos2RegionKey> ActiveRegions;
    TSet<FLythos2RegionKey> ReadyRegions;
    TSet<FLythos2RegionKey> BuildingRegions;
    TSet<FLythos2RegionKey> FailedRegions;

    TArray<FLythos2RegionKey> RequestQueue;
};
