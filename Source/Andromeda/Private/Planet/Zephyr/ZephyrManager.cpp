#include "Planet/Zephyr/ZephyrManager.h"

DEFINE_LOG_CATEGORY(LogAndromedaZephyr);

FZephyrManager& FZephyrManager::Get()
{
    static FZephyrManager Singleton;
    return Singleton;
}

void FZephyrManager::SetSnapshot(
    const TArray<FZephyrPlanetSnapshotEntry>& InPlanets,
    const FVector& InStarWorldPosition
)
{
    FScopeLock ScopeLock(&SnapshotLock);
    Planets = InPlanets;
    StarWorldPosition = InStarWorldPosition;
    ++SnapshotVersion;
}

void FZephyrManager::Clear()
{
    FScopeLock ScopeLock(&SnapshotLock);
    Planets.Empty();
    StarWorldPosition = FVector::ZeroVector;
    ++SnapshotVersion;
}

void FZephyrManager::GetSnapshot(
    TArray<FZephyrPlanetSnapshotEntry>& OutPlanets,
    FVector& OutStarWorldPosition,
    uint64& OutVersion
) const
{
    FScopeLock ScopeLock(&SnapshotLock);
    OutPlanets = Planets;
    OutStarWorldPosition = StarWorldPosition;
    OutVersion = SnapshotVersion;
}
