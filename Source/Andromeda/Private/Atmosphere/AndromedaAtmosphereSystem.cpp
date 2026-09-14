#include "Atmosphere/AndromedaAtmosphereSystem.h"

DEFINE_LOG_CATEGORY(LogAndromedaAtmosphere);

FAndromedaAtmosphereSystem& FAndromedaAtmosphereSystem::Get()
{
    static FAndromedaAtmosphereSystem Singleton;
    return Singleton;
}

void FAndromedaAtmosphereSystem::SetSnapshot(
    const TArray<FAndromedaAtmosphereInstance>& InPlanets,
    const FVector& InStarWorldPosition
)
{
    FScopeLock ScopeLock(&SnapshotLock);
    Planets = InPlanets;
    StarWorldPosition = InStarWorldPosition;
    ++SnapshotVersion;
}

void FAndromedaAtmosphereSystem::Clear()
{
    FScopeLock ScopeLock(&SnapshotLock);
    Planets.Empty();
    StarWorldPosition = FVector::ZeroVector;
    ++SnapshotVersion;
}

void FAndromedaAtmosphereSystem::GetSnapshot(
    TArray<FAndromedaAtmosphereInstance>& OutPlanets,
    FVector& OutStarWorldPosition,
    uint64& OutVersion
) const
{
    FScopeLock ScopeLock(&SnapshotLock);
    OutPlanets = Planets;
    OutStarWorldPosition = StarWorldPosition;
    OutVersion = SnapshotVersion;
}

int32 FAndromedaAtmosphereSystem::GetPlanetCount() const
{
    FScopeLock ScopeLock(&SnapshotLock);
    return Planets.Num();
}

uint64 FAndromedaAtmosphereSystem::GetVersion() const
{
    FScopeLock ScopeLock(&SnapshotLock);
    return SnapshotVersion;
}
