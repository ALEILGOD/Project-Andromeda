#include "LYTHOS2/Lythos2WorldSubsystem.h"

#include "LYTHOS2/Lythos2CubeSphere.h"
#include "LYTHOS2/Lythos2DensityField.h"
#include "LYTHOS2/Lythos2LodPolicy.h"
#include "LYTHOS2/Lythos2Mesher.h"

#include "Planet/Planet.h"
#include "ProceduralMeshComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Stats/Stats.h"

namespace
{
    bool SetsEqual(const TSet<FLythos2RegionKey>& A, const TSet<FLythos2RegionKey>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (const FLythos2RegionKey& Key : A)
        {
            if (!B.Contains(Key))
            {
                return false;
            }
        }
        return true;
    }
}

void ULythos2WorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    Scheduler.Configure(Settings);
}

void ULythos2WorldSubsystem::Deinitialize()
{
    for (TPair<FLythos2RegionKey, TUniquePtr<FAsyncTask<FLythos2BuildTask>>>& Pair : Tasks)
    {
        if (Pair.Value.IsValid())
        {
            Pair.Value->EnsureCompletion();
        }
    }
    Tasks.Empty();
    ReadyMeshes.Empty();
    Planets.Empty();
    Scheduler.Reset();

    Super::Deinitialize();
}

bool ULythos2WorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULythos2WorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(ULythos2WorldSubsystem, STATGROUP_Tickables);
}

void ULythos2WorldSubsystem::SetViewerWorldOverride(bool bEnabled, const FVector& WorldLocation)
{
    bViewerOverride = bEnabled;
    ViewerOverrideWorld = WorldLocation;
}

ULythos2WorldSubsystem::FPlanetEntry* ULythos2WorldSubsystem::FindEntry(int64 PlanetID)
{
    for (FPlanetEntry& Entry : Planets)
    {
        if (Entry.Context.PlanetID == PlanetID)
        {
            return &Entry;
        }
    }
    return nullptr;
}

void ULythos2WorldSubsystem::RegisterPlanet(APlanet* Planet)
{
    if (!Planet)
    {
        return;
    }

    for (const FPlanetEntry& Entry : Planets)
    {
        if (Entry.Planet.Get() == Planet)
        {
            return;
        }
    }

    FPlanetEntry NewEntry;
    NewEntry.Planet = Planet;
    NewEntry.Context.PlanetID = Planet->PlanetID;
    NewEntry.Context.Seed = Planet->PlanetSeed;
    NewEntry.Context.RadiusCm = Planet->PlanetRadius;
    NewEntry.Context.TerrainHeightCm = Planet->TerrainHeight;
    NewEntry.Context.ContinentalScale = Planet->ContinentalScale;
    NewEntry.Context.MountainScale = Planet->MountainScale;
    NewEntry.Context.DetailScale = Planet->DetailScale;
    NewEntry.Context.MountainStrength = Planet->MountainStrength;
    NewEntry.Context.DetailStrength = Planet->DetailStrength;
    NewEntry.Context.Archetype = static_cast<int32>(Planet->PlanetArchetype);
    NewEntry.Context.VolumetricDetailAmount = Settings.bEnableVolumetricDetail ? 0.12f : 0.0f;
    NewEntry.Context.WorldPosition = Planet->GetActorLocation();

    const int32 Index = Planets.Add(MoveTemp(NewEntry));

    SeedCoarsePlanet(Planet, Planets[Index]);
}

void ULythos2WorldSubsystem::UnregisterPlanet(APlanet* Planet)
{
    if (!Planet)
    {
        return;
    }

    for (int32 Index = Planets.Num() - 1; Index >= 0; --Index)
    {
        if (Planets[Index].Planet.Get() == Planet)
        {
            AppliedMeshCount = FMath::Max(0, AppliedMeshCount - Planets[Index].SectionByRegion.Num());
            for (const TPair<FLythos2RegionKey, FIntPoint>& Size : Planets[Index].MeshSizeByRegion)
            {
                AppliedVertices -= Size.Value.X;
                AppliedTriangles -= Size.Value.Y;
            }
            for (const TPair<FLythos2RegionKey, int32>& Pair : Planets[Index].SectionByRegion)
            {
                Scheduler.RemoveRegion(Pair.Key);
                ReadyMeshes.Remove(Pair.Key);
            }
            Planets.RemoveAt(Index);
        }
    }
}

void ULythos2WorldSubsystem::SeedCoarsePlanet(APlanet* Planet, FPlanetEntry& Entry)
{
    if (!Planet || !Entry.Context.IsValid())
    {
        return;
    }

    // Only the 6 LOD-0 planetary roots are built synchronously: a bounded,
    // coarse collision seed. Collision is cooked synchronously here so the
    // planet is immediately solid; streamed collision uses async cooking.
    UProceduralMeshComponent* PMC = Planet->PlanetProceduralMesh;
    if (PMC)
    {
        PMC->bUseAsyncCooking = false;
    }

    Scheduler.BeginDesiredUpdate();

    TArray<FLythos2RegionKey> Roots;
    for (int32 Face = 0; Face < Lythos2::CubeSphere::NumFaces; ++Face)
    {
        FLythos2RegionKey Root(Face, 0, 0, 0);
        Root.PlanetID = Entry.Context.PlanetID;
        Roots.Add(Root);
        Scheduler.AddDesired(Root, 0.0);
    }

    for (const FLythos2RegionKey& Root : Roots)
    {
        FLythos2MeshData Mesh;
        Lythos2::Mesher::BuildRegionMesh(Entry.Context, Root, Settings, Mesh);
        ApplyMeshToEntry(Entry, Root, MoveTemp(Mesh));
        Scheduler.MarkReady(Root);
        Scheduler.Activate(Root);
    }

    if (PMC && Settings.bEnableCollision)
    {
        for (const TPair<FLythos2RegionKey, int32>& Pair : Entry.SectionByRegion)
        {
            if (FProcMeshSection* Section = PMC->GetProcMeshSection(Pair.Value))
            {
                Section->bEnableCollision = true;
            }
        }
        PMC->ClearCollisionConvexMeshes();
        Entry.CollisionRegions.Reset();
        for (const FLythos2RegionKey& Root : Roots)
        {
            Entry.CollisionRegions.Add(Root);
        }
        Entry.bCollisionDirty = false;
        Entry.TicksSinceCollisionCook = 0;
    }

    if (PMC)
    {
        PMC->bUseAsyncCooking = Settings.bUseAsyncCollisionCooking;
    }
}

bool ULythos2WorldSubsystem::GetViewerWorldLocation(FVector& OutWorld) const
{
    if (bViewerOverride)
    {
        OutWorld = ViewerOverrideWorld;
        return true;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    if (APlayerController* PC = World->GetFirstPlayerController())
    {
        FRotator ViewRotation;
        PC->GetPlayerViewPoint(OutWorld, ViewRotation);
        return true;
    }

    return false;
}

bool ULythos2WorldSubsystem::GetViewerLocalForEntry(const FPlanetEntry& Entry, FVector& OutLocal) const
{
    if (!Entry.Planet.IsValid())
    {
        OutLocal = FVector::ZeroVector;
        return false;
    }

    FVector WorldViewer;
    if (!GetViewerWorldLocation(WorldViewer))
    {
        // No viewer: keep the planet coarse by placing the virtual viewer far away.
        OutLocal = FVector(0.0, 0.0, Entry.Context.RadiusCm * 1000.0f);
        return false;
    }

    OutLocal = Entry.Planet->GetActorTransform().InverseTransformPosition(WorldViewer);
    return true;
}

void ULythos2WorldSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    Scheduler.Configure(Settings);

    Planets.RemoveAll([](const FPlanetEntry& Entry) { return !Entry.Planet.IsValid(); });

    TickDensityMs = 0.0;
    TickMeshingMs = 0.0;
    TickUploadMs = 0.0;
    TickCollisionMs = 0.0;
    TickRemovalMs = 0.0;
    UploadsThisTick = 0;
    UploadVerticesThisTick = 0;
    StaleRejectedThisTick = 0;
    RemovalsThisTick = 0;

    // 1-3. Desired partition per planet (bounded, deterministic, hysteresis).
    Scheduler.CapturePreviousDesired();
    Scheduler.BeginDesiredUpdate();

    const double SelectStart = FPlatformTime::Seconds();
    LastSelectedCount = 0;

    for (FPlanetEntry& Entry : Planets)
    {
        if (!Entry.Context.IsValid())
        {
            continue;
        }

        FVector ViewerLocal = FVector::ZeroVector;
        Entry.bHasViewer = GetViewerLocalForEntry(Entry, ViewerLocal);
        Entry.LastViewerLocal = ViewerLocal;

        TArray<FLythos2RegionKey> Leaves;
        Lythos2::Lod::SelectRegions(
            Entry.Context.PlanetID,
            ViewerLocal,
            Entry.Context,
            Settings,
            Leaves,
            &Entry.PreviouslyRefinedNodes);

        LastSelectedCount += Leaves.Num();

        for (const FLythos2RegionKey& Leaf : Leaves)
        {
            const double Priority = Lythos2::Lod::RegionPriorityCm(ViewerLocal, Leaf, Entry.Context);
            Scheduler.AddDesired(Leaf, Priority);
        }

        // Rebuild hysteresis state for the next tick: every strict ancestor of
        // a leaf had children this tick.
        TSet<FLythos2RegionKey> RefinedNodes;
        RefinedNodes.Reserve(Leaves.Num() * 2);
        for (const FLythos2RegionKey& Leaf : Leaves)
        {
            FLythos2RegionKey Parent = Leaf.GetParent();
            while (Parent.IsValid())
            {
                RefinedNodes.Add(Parent);
                Parent = Parent.GetParent();
            }
        }
        Entry.PreviouslyRefinedNodes = MoveTemp(RefinedNodes);
    }

    const double SelectMs = (FPlatformTime::Seconds() - SelectStart) * 1000.0;

    const double SchedulerStart = FPlatformTime::Seconds();
    Scheduler.CommitDesiredUpdate();
    const double SchedulerMs = (FPlatformTime::Seconds() - SchedulerStart) * 1000.0;

    // 4-7. Bounded async generation, upload, swap and unload.
    DispatchBuilds();
    PollBuilds();

    // Collision is coalesced into a single, viewer-bounded cook per tick so the
    // per-section Create/Clear calls never trigger a full-planet recook.
    BeginCollisionBatch();
    ApplyActivations();
    ApplyRemovals();
    EndCollisionBatch();

    PublishStats(SelectMs, SchedulerMs);
}

void ULythos2WorldSubsystem::DispatchBuilds()
{
    int32 Budget = FMath::Max(0, Settings.MaxBuildsDispatchedPerTick);

    while (Budget-- > 0)
    {
        // Bound concurrency and the completed backlog so work can never pile up.
        if (Tasks.Num() >= FMath::Max(1, Settings.MaxConcurrentMeshTasks))
        {
            break;
        }
        if (ReadyMeshes.Num() >= FMath::Max(1, Settings.MaxCompletedMeshBacklog))
        {
            break;
        }

        FLythos2RegionKey Key;
        if (!Scheduler.PopRequest(Key))
        {
            break;
        }

        FPlanetEntry* Entry = FindEntry(Key.PlanetID);
        if (!Entry || !Entry->Context.IsValid())
        {
            Scheduler.MarkFailed(Key);
            continue;
        }

        Scheduler.MarkBuilding(Key);

        TUniquePtr<FAsyncTask<FLythos2BuildTask>> Task =
            MakeUnique<FAsyncTask<FLythos2BuildTask>>(Entry->Context, Key, Settings);
        Task->StartBackgroundTask();
        Tasks.Add(Key, MoveTemp(Task));
    }
}

void ULythos2WorldSubsystem::PollBuilds()
{
    TArray<FLythos2RegionKey> Finished;
    for (const TPair<FLythos2RegionKey, TUniquePtr<FAsyncTask<FLythos2BuildTask>>>& Pair : Tasks)
    {
        if (Pair.Value.IsValid() && Pair.Value->IsDone())
        {
            Finished.Add(Pair.Key);
        }
    }

    const int32 BacklogCap = FMath::Max(1, Settings.MaxCompletedMeshBacklog);

    for (const FLythos2RegionKey& Key : Finished)
    {
        TUniquePtr<FAsyncTask<FLythos2BuildTask>> Task;
        Tasks.RemoveAndCopyValue(Key, Task);
        if (!Task.IsValid())
        {
            continue;
        }

        FLythos2BuildTask& Built = Task->GetTask();
        TickDensityMs += Built.Mesh.DensityMs;
        TickMeshingMs += Built.Mesh.MeshingMs;

        if (!Built.bBuilt || Built.Mesh.IsEmpty())
        {
            Scheduler.MarkFailed(Key);
            continue;
        }

        // Backlog budget: keep a finished result only if the backlog has room
        // or the region is still wanted. Stale results are rejected safely.
        if (ReadyMeshes.Num() < BacklogCap || Scheduler.IsDesired(Key))
        {
            ++Settings.TotalRegionsGenerated;
            ReadyMeshes.Add(Key, MoveTemp(Built.Mesh));
            Scheduler.MarkReady(Key);
        }
        else
        {
            Scheduler.RemoveRegion(Key);
            ++StaleRejectedThisTick;
            ++Settings.TotalStaleRejected;
        }
    }
}

void ULythos2WorldSubsystem::ApplyActivations()
{
    // Apply a ready region AND activate it together, so a region only counts as
    // active/mesh-backed once it is actually on screen. This is what makes the
    // hole-free transition guarantee hold even when uploads are budgeted.
    TArray<FLythos2RegionKey> ToApply;
    ToApply.Reserve(ReadyMeshes.Num());
    for (const TPair<FLythos2RegionKey, FLythos2MeshData>& Pair : ReadyMeshes)
    {
        if (Scheduler.IsDesired(Pair.Key))
        {
            ToApply.Add(Pair.Key);
        }
    }

    ToApply.Sort([this](const FLythos2RegionKey& A, const FLythos2RegionKey& B)
    {
        const double PA = Scheduler.GetPriority(A);
        const double PB = Scheduler.GetPriority(B);
        if (PA != PB) { return PA < PB; }
        return A.Lod > B.Lod;
    });

    const int32 MaxUploads = FMath::Max(0, Settings.MaxUploadsPerTick);
    const double UploadBudgetMs = Settings.MaxUploadMillisecondsPerTick;

    for (const FLythos2RegionKey& Key : ToApply)
    {
        if (UploadsThisTick >= MaxUploads)
        {
            break;
        }
        if (UploadBudgetMs > 0.0 && TickUploadMs >= UploadBudgetMs)
        {
            break;
        }

        FPlanetEntry* Entry = FindEntry(Key.PlanetID);
        if (!Entry)
        {
            ReadyMeshes.Remove(Key);
            Scheduler.RemoveRegion(Key);
            continue;
        }

        FLythos2MeshData Mesh;
        ReadyMeshes.RemoveAndCopyValue(Key, Mesh);

        const double UploadStart = FPlatformTime::Seconds();
        ApplyMeshToEntry(*Entry, Key, MoveTemp(Mesh));
        TickUploadMs += (FPlatformTime::Seconds() - UploadStart) * 1000.0;

        Scheduler.Activate(Key);
        ++UploadsThisTick;
    }

    // Reject completed meshes that are no longer wanted before ever uploading.
    for (auto It = ReadyMeshes.CreateIterator(); It; ++It)
    {
        if (!Scheduler.IsActive(It.Key()) && !Scheduler.IsDesired(It.Key()))
        {
            Scheduler.RemoveRegion(It.Key());
            It.RemoveCurrent();
            ++StaleRejectedThisTick;
            ++Settings.TotalStaleRejected;
        }
    }
}

void ULythos2WorldSubsystem::ApplyRemovals()
{
    TArray<FLythos2RegionKey> Removals;
    Scheduler.ComputeRemovals(Removals);

    const int32 RemovalBudget = FMath::Max(1, Settings.MaxRemovalsPerTick);
    int32 Removed = 0;

    for (const FLythos2RegionKey& Key : Removals)
    {
        if (Removed >= RemovalBudget)
        {
            break;
        }

        const double RemovalStart = FPlatformTime::Seconds();
        if (FPlanetEntry* Entry = FindEntry(Key.PlanetID))
        {
            RemoveMeshFromEntry(*Entry, Key);
        }
        TickRemovalMs += (FPlatformTime::Seconds() - RemovalStart) * 1000.0;

        ReadyMeshes.Remove(Key);
        Scheduler.RemoveRegion(Key);
        ++Removed;
        ++RemovalsThisTick;
    }
}

void ULythos2WorldSubsystem::BeginCollisionBatch()
{
    // Disable collision on every section for the duration of the mutation
    // batch, so the UpdateCollision() calls made internally by
    // Create/ClearMeshSection snapshot no geometry (they become ~free).
    for (FPlanetEntry& Entry : Planets)
    {
        APlanet* Planet = Entry.Planet.Get();
        if (!Planet || !Planet->PlanetProceduralMesh)
        {
            continue;
        }

        UProceduralMeshComponent* PMC = Planet->PlanetProceduralMesh;
        for (const TPair<FLythos2RegionKey, int32>& Pair : Entry.SectionByRegion)
        {
            if (FProcMeshSection* Section = PMC->GetProcMeshSection(Pair.Value))
            {
                Section->bEnableCollision = false;
            }
        }
    }
}

void ULythos2WorldSubsystem::EndCollisionBatch()
{
    if (!Settings.bEnableCollision)
    {
        return;
    }

    const int32 Interval = FMath::Max(1, Settings.CollisionUpdateIntervalTicks);

    for (FPlanetEntry& Entry : Planets)
    {
        APlanet* Planet = Entry.Planet.Get();
        if (!Planet || !Planet->PlanetProceduralMesh)
        {
            continue;
        }

        ++Entry.TicksSinceCollisionCook;

        TSet<FLythos2RegionKey> DesiredCollision;
        for (const FLythos2RegionKey& Key : Scheduler.GetActiveRegions())
        {
            if (Key.PlanetID == Entry.Context.PlanetID && ShouldRegionHaveCollision(Key, Entry))
            {
                DesiredCollision.Add(Key);
            }
        }

        const bool bChanged = Entry.bCollisionDirty
            || !SetsEqual(DesiredCollision, Entry.CollisionRegions);

        if (!bChanged || Entry.TicksSinceCollisionCook < Interval)
        {
            // Keep whatever collision representation is currently active.
            continue;
        }

        UProceduralMeshComponent* PMC = Planet->PlanetProceduralMesh;

        // Re-enable collision only on the viewer-bounded sections, then do a
        // single coalesced cook for the whole component.
        for (const TPair<FLythos2RegionKey, int32>& Pair : Entry.SectionByRegion)
        {
            if (FProcMeshSection* Section = PMC->GetProcMeshSection(Pair.Value))
            {
                Section->bEnableCollision = DesiredCollision.Contains(Pair.Key);
            }
        }

        const double CookStart = FPlatformTime::Seconds();
        PMC->ClearCollisionConvexMeshes();
        TickCollisionMs += (FPlatformTime::Seconds() - CookStart) * 1000.0;

        Entry.CollisionRegions = MoveTemp(DesiredCollision);
        Entry.bCollisionDirty = false;
        Entry.TicksSinceCollisionCook = 0;
    }
}

bool ULythos2WorldSubsystem::ShouldRegionHaveCollision(const FLythos2RegionKey& Key, const FPlanetEntry& Entry) const
{
    if (!Settings.bEnableCollision)
    {
        return false;
    }

    // Coarse roots always collide so the initial planet is solid.
    if (Key.Lod == 0)
    {
        return true;
    }

    if (!Entry.bHasViewer)
    {
        return Key.Lod <= 1;
    }

    const double Distance = Lythos2::Lod::DistanceToRegionCm(Entry.LastViewerLocal, Key, Entry.Context);
    return Distance < static_cast<double>(Entry.Context.RadiusCm) * Settings.CollisionDistanceScale;
}

void ULythos2WorldSubsystem::ApplyMeshToEntry(FPlanetEntry& Entry, const FLythos2RegionKey& Key, FLythos2MeshData&& Mesh)
{
    APlanet* Planet = Entry.Planet.Get();
    if (!Planet || !Planet->PlanetProceduralMesh)
    {
        return;
    }

    if (Mesh.IsEmpty())
    {
        return;
    }

    int32 SectionIndex = INDEX_NONE;
    if (const int32* Existing = Entry.SectionByRegion.Find(Key))
    {
        SectionIndex = *Existing;
    }
    else
    {
        SectionIndex = 0;
        while (Entry.UsedSections.Contains(SectionIndex))
        {
            ++SectionIndex;
        }
        Entry.UsedSections.Add(SectionIndex);
        Entry.SectionByRegion.Add(Key, SectionIndex);
        ++AppliedMeshCount;
    }

    // Always create without collision; collision is applied once per tick in
    // EndCollisionBatch so per-section calls never trigger a full recook.
    Planet->PlanetProceduralMesh->CreateMeshSection(
        SectionIndex,
        Mesh.Positions,
        Mesh.Indices,
        Mesh.Normals,
        Mesh.UVs,
        Mesh.Colors,
        TArray<FProcMeshTangent>(),
        false);

    FIntPoint& Size = Entry.MeshSizeByRegion.FindOrAdd(Key);
    AppliedVertices -= Size.X;
    AppliedTriangles -= Size.Y;
    Size = FIntPoint(Mesh.Positions.Num(), Mesh.Indices.Num() / 3);
    AppliedVertices += Size.X;
    AppliedTriangles += Size.Y;
    UploadVerticesThisTick += Mesh.Positions.Num();

    Entry.bCollisionDirty = true;
}

void ULythos2WorldSubsystem::RemoveMeshFromEntry(FPlanetEntry& Entry, const FLythos2RegionKey& Key)
{
    const int32* SectionIndex = Entry.SectionByRegion.Find(Key);
    if (!SectionIndex)
    {
        return;
    }

    if (APlanet* Planet = Entry.Planet.Get())
    {
        if (Planet->PlanetProceduralMesh)
        {
            Planet->PlanetProceduralMesh->ClearMeshSection(*SectionIndex);
        }
    }

    Entry.UsedSections.Remove(*SectionIndex);
    Entry.SectionByRegion.Remove(Key);
    Entry.CollisionRegions.Remove(Key);
    if (FIntPoint* Size = Entry.MeshSizeByRegion.Find(Key))
    {
        AppliedVertices -= Size->X;
        AppliedTriangles -= Size->Y;
        Entry.MeshSizeByRegion.Remove(Key);
    }
    Entry.bCollisionDirty = true;
    AppliedMeshCount = FMath::Max(0, AppliedMeshCount - 1);
}

void ULythos2WorldSubsystem::PublishStats(double SelectMs, double SchedulerMs)
{
    Settings.LastActiveRegionCount = Scheduler.GetActiveCount();
    Settings.LastSelectedRegionCount = LastSelectedCount;
    Settings.LastPendingBuildCount = Scheduler.GetBuildingCount() + Scheduler.GetQueueCount();
    Settings.LastGeneratingRegionCount = Scheduler.GetBuildingCount();
    Settings.LastReadyRegionCount = Scheduler.GetReadyCount();
    Settings.LastStaleRejectedCount = StaleRejectedThisTick;
    Settings.LastUploadsThisTick = UploadsThisTick;
    Settings.LastRemovalsThisTick = RemovalsThisTick;
    Settings.LastActiveSectionCount = AppliedMeshCount;
    Settings.LastActiveVertexCount = AppliedVertices;
    Settings.LastActiveTriangleCount = AppliedTriangles;
    Settings.LastUploadVertexCount = UploadVerticesThisTick;

    Settings.LastSelectMs = static_cast<float>(SelectMs);
    Settings.LastSchedulerMs = static_cast<float>(SchedulerMs);
    Settings.LastDensityMs = static_cast<float>(TickDensityMs);
    Settings.LastMeshingMs = static_cast<float>(TickMeshingMs);
    Settings.LastUploadMs = static_cast<float>(TickUploadMs);
    Settings.LastCollisionMs = static_cast<float>(TickCollisionMs);
    Settings.LastRemovalMs = static_cast<float>(TickRemovalMs);
    Settings.LastWorkerMs = static_cast<float>(TickDensityMs + TickMeshingMs);
    Settings.LastTickWorkMs = static_cast<float>(
        SelectMs + SchedulerMs + TickUploadMs + TickCollisionMs + TickRemovalMs);

    const int32 LodCount = FMath::Max(1, Settings.MaxTerrainLOD + 1);
    Settings.LastActiveRegionsByLOD.Init(0, LodCount);
    for (const FLythos2RegionKey& Key : Scheduler.GetActiveRegions())
    {
        if (Key.Lod >= 0 && Key.Lod < LodCount)
        {
            ++Settings.LastActiveRegionsByLOD[Key.Lod];
        }
    }
}

