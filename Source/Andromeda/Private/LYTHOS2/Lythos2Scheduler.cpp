#include "LYTHOS2/Lythos2Scheduler.h"

void FLythos2StreamScheduler::Reset()
{
    Desired.Reset();
    DesiredOrder.Reset();
    DesiredPriority.Reset();
    PreviousDesired.Reset();
    ActiveRegions.Reset();
    ReadyRegions.Reset();
    BuildingRegions.Reset();
    FailedRegions.Reset();
    RequestQueue.Reset();
}

double FLythos2StreamScheduler::GetPriority(const FLythos2RegionKey& Key) const
{
    const double* Found = DesiredPriority.Find(Key);
    return Found ? *Found : 0.0;
}

void FLythos2StreamScheduler::CapturePreviousDesired()
{
    PreviousDesired = Desired;
}

void FLythos2StreamScheduler::BeginDesiredUpdate()
{
    Desired.Reset();
    DesiredOrder.Reset();
    DesiredPriority.Reset();
}

void FLythos2StreamScheduler::AddDesired(const FLythos2RegionKey& Key, double Priority)
{
    if (!Key.IsValid())
    {
        return;
    }
    bool bAlready = false;
    Desired.Add(Key, &bAlready);
    if (!bAlready)
    {
        DesiredOrder.Add(Key);
        DesiredPriority.Add(Key, Priority);
    }
    else
    {
        // Keep the best (smallest) priority for a duplicate request.
        double& Stored = DesiredPriority.FindOrAdd(Key, Priority);
        Stored = FMath::Min(Stored, Priority);
    }
}

void FLythos2StreamScheduler::CommitDesiredUpdate()
{
    // 1. Drop queued requests that are no longer wanted.
    for (int32 Index = RequestQueue.Num() - 1; Index >= 0; --Index)
    {
        if (!Desired.Contains(RequestQueue[Index]))
        {
            RequestQueue.RemoveAt(Index);
        }
    }

    // 2. Enqueue desired regions that are neither active nor in flight.
    for (const FLythos2RegionKey& Key : DesiredOrder)
    {
        if (ActiveRegions.Contains(Key)
            || ReadyRegions.Contains(Key)
            || BuildingRegions.Contains(Key)
            || FailedRegions.Contains(Key))
        {
            continue;
        }

        if (RequestQueue.Contains(Key))
        {
            continue;
        }

        if (RequestQueue.Num() >= Settings.MaxQueuedBuilds)
        {
            break;
        }

        RequestQueue.Add(Key);
    }

    // 3. Nearest / most relevant first, then finer LOD. This ensures a region
    //    10 m away is never stuck behind a distant planetary region, while
    //    still making progress on everything (no starvation).
    RequestQueue.Sort([this](const FLythos2RegionKey& A, const FLythos2RegionKey& B)
    {
        const double PA = GetPriority(A);
        const double PB = GetPriority(B);
        if (PA != PB) { return PA < PB; }
        if (A.Lod != B.Lod) { return A.Lod > B.Lod; }
        if (A.PlanetID != B.PlanetID) { return A.PlanetID < B.PlanetID; }
        if (A.Face != B.Face) { return A.Face < B.Face; }
        if (A.X != B.X) { return A.X < B.X; }
        return A.Y < B.Y;
    });
}

bool FLythos2StreamScheduler::PopRequest(FLythos2RegionKey& OutKey)
{
    if (RequestQueue.Num() == 0)
    {
        return false;
    }
    OutKey = RequestQueue[0];
    RequestQueue.RemoveAt(0);
    return true;
}

void FLythos2StreamScheduler::MarkBuilding(const FLythos2RegionKey& Key)
{
    RequestQueue.Remove(Key);
    BuildingRegions.Add(Key);
}

void FLythos2StreamScheduler::MarkReady(const FLythos2RegionKey& Key)
{
    BuildingRegions.Remove(Key);
    ReadyRegions.Add(Key);
}

void FLythos2StreamScheduler::MarkFailed(const FLythos2RegionKey& Key)
{
    BuildingRegions.Remove(Key);
    ReadyRegions.Remove(Key);
    FailedRegions.Add(Key);
}

bool FLythos2StreamScheduler::Activate(const FLythos2RegionKey& Key)
{
    if (!ReadyRegions.Contains(Key) || !Desired.Contains(Key))
    {
        return false;
    }
    ReadyRegions.Remove(Key);
    ActiveRegions.Add(Key);
    return true;
}

int32 FLythos2StreamScheduler::ActivateReady(int32 MaxActivations)
{
    if (MaxActivations <= 0)
    {
        return 0;
    }

    TArray<FLythos2RegionKey> Sorted = ReadyRegions.Array();
    Sorted.Sort([this](const FLythos2RegionKey& A, const FLythos2RegionKey& B)
    {
        const double PA = GetPriority(A);
        const double PB = GetPriority(B);
        if (PA != PB) { return PA < PB; }
        if (A.Lod != B.Lod) { return A.Lod > B.Lod; }
        if (A.PlanetID != B.PlanetID) { return A.PlanetID < B.PlanetID; }
        if (A.Face != B.Face) { return A.Face < B.Face; }
        if (A.X != B.X) { return A.X < B.X; }
        return A.Y < B.Y;
    });

    int32 Activated = 0;
    for (const FLythos2RegionKey& Key : Sorted)
    {
        if (Activated >= MaxActivations)
        {
            break;
        }
        if (!Desired.Contains(Key))
        {
            // Became irrelevant before it finished; discard. Bounded stale
            // rejection: never uploaded, never rendered.
            ReadyRegions.Remove(Key);
            continue;
        }

        // The active set is bounded because Desired is itself bounded by
        // MaxActiveRegions (selection cap). Do NOT hard-stop at the cap here:
        // doing so deadlocks LOD transitions, since a coarse replacement can
        // only become active while the finer regions it replaces are still
        // active. Removals free those slots in the same streamer tick.
        ReadyRegions.Remove(Key);
        ActiveRegions.Add(Key);
        ++Activated;
    }

    return Activated;
}

void FLythos2StreamScheduler::ComputeRemovals(TArray<FLythos2RegionKey>& OutRemovals) const
{
    OutRemovals.Reset();

    for (const FLythos2RegionKey& Active : ActiveRegions)
    {
        if (Desired.Contains(Active))
        {
            continue;
        }

        bool bHasDesiredDescendant = false;
        bool bHasDesiredAncestor = false;
        bool bAllDescendantsActive = true;
        bool bAncestorActive = false;

        for (const FLythos2RegionKey& Wanted : Desired)
        {
            if (Active.IsAncestorOf(Wanted))
            {
                bHasDesiredDescendant = true;
                if (!ActiveRegions.Contains(Wanted))
                {
                    bAllDescendantsActive = false;
                }
            }
            else if (Wanted.IsAncestorOf(Active))
            {
                bHasDesiredAncestor = true;
                if (ActiveRegions.Contains(Wanted))
                {
                    bAncestorActive = true;
                }
            }
        }

        if (bHasDesiredDescendant)
        {
            // Refining: wait until the whole desired partition below is active.
            if (bAllDescendantsActive)
            {
                OutRemovals.Add(Active);
            }
        }
        else if (bHasDesiredAncestor)
        {
            // Coarsening: wait until the covering ancestor is active.
            if (bAncestorActive)
            {
                OutRemovals.Add(Active);
            }
        }
        else
        {
            // No replacement needed: unload.
            OutRemovals.Add(Active);
        }
    }
}

void FLythos2StreamScheduler::RemoveRegion(const FLythos2RegionKey& Key)
{
    ActiveRegions.Remove(Key);
    ReadyRegions.Remove(Key);
    BuildingRegions.Remove(Key);
    RequestQueue.Remove(Key);
}
