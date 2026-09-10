#include "Atmosphere/AndromedaAtmosphereManager.h"


DEFINE_LOG_CATEGORY(LogAndromedaAtmos);


// =========================================================
// SINGLETON
// =========================================================

FAndromedaAtmosphereManager& FAndromedaAtmosphereManager::Get()
{
    static FAndromedaAtmosphereManager Singleton;
    return Singleton;
}


// =========================================================
// REGISTRY
// =========================================================

FAndromedaAtmosphereHandle FAndromedaAtmosphereManager::RegisterAtmosphere(
    const FAndromedaAtmosphereInstanceDesc& Desc
)
{
    FScopeLock ScopeLock(&RegistryLock);


    if (!Desc.Parameters.IsValidConfiguration())
    {
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("RegisterAtmosphere: rejected invalid parameters ('%s'). SurfaceRadius must be > 0, AtmosphereRadius > SurfaceRadius and scale heights > 0."),
            *Desc.DebugName.ToString()
        );

        return FAndromedaAtmosphereHandle::Invalid();
    }


    FAndromedaAtmosphereInstance Instance;

    Instance.Handle.Id = NextHandleId++;
    Instance.DebugName = Desc.DebugName;
    Instance.Parameters = Desc.Parameters;
    Instance.WorldPosition = Desc.WorldPosition;


    const uint32 HandleId = Instance.Handle.Id;
    Atmospheres.Add(HandleId, Instance);


    UE_LOG(
        LogAndromedaAtmos,
        Log,
        TEXT("RegisterAtmosphere: '%s' registered (Handle %u). Total atmospheres: %d."),
        *Instance.DebugName.ToString(),
        HandleId,
        Atmospheres.Num()
    );


    return Instance.Handle;
}


bool FAndromedaAtmosphereManager::UnregisterAtmosphere(
    FAndromedaAtmosphereHandle Handle
)
{
    if (!Handle.IsValid())
    {
        return false;
    }


    FScopeLock ScopeLock(&RegistryLock);

    FAndromedaAtmosphereInstance RemovedInstance;
    const bool bRemoved = Atmospheres.RemoveAndCopyValue(Handle.Id, RemovedInstance);


    if (bRemoved)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Log,
            TEXT("UnregisterAtmosphere: '%s' unregistered (Handle %u). Total atmospheres: %d."),
            *RemovedInstance.DebugName.ToString(),
            Handle.Id,
            Atmospheres.Num()
        );
    }


    return bRemoved;
}


bool FAndromedaAtmosphereManager::UpdateAtmosphereParameters(
    FAndromedaAtmosphereHandle Handle,
    const FAndromedaAtmosphereParameters& Parameters
)
{
    if (!Handle.IsValid())
    {
        return false;
    }


    if (!Parameters.IsValidConfiguration())
    {
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("UpdateAtmosphereParameters: rejected invalid parameters (Handle %u)."),
            Handle.Id
        );

        return false;
    }


    FScopeLock ScopeLock(&RegistryLock);

    FAndromedaAtmosphereInstance* Instance = Atmospheres.Find(Handle.Id);


    if (!Instance)
    {
        return false;
    }


    Instance->Parameters = Parameters;

    return true;
}


bool FAndromedaAtmosphereManager::UpdateAtmosphereWorldPosition(
    FAndromedaAtmosphereHandle Handle,
    const FVector& WorldPosition
)
{
    if (!Handle.IsValid())
    {
        return false;
    }


    FScopeLock ScopeLock(&RegistryLock);

    FAndromedaAtmosphereInstance* Instance = Atmospheres.Find(Handle.Id);


    if (!Instance)
    {
        return false;
    }


    Instance->WorldPosition = WorldPosition;

    return true;
}


// =========================================================
// QUERIES
// =========================================================

bool FAndromedaAtmosphereManager::FindAtmosphere(
    FAndromedaAtmosphereHandle Handle,
    FAndromedaAtmosphereInstance& OutInstance
) const
{
    if (!Handle.IsValid())
    {
        return false;
    }


    FScopeLock ScopeLock(&RegistryLock);

    const FAndromedaAtmosphereInstance* Instance = Atmospheres.Find(Handle.Id);


    if (!Instance)
    {
        return false;
    }


    OutInstance = *Instance;

    return true;
}


void FAndromedaAtmosphereManager::GetAtmosphereSnapshot(
    TArray<FAndromedaAtmosphereInstance>& OutSnapshot
) const
{
    FScopeLock ScopeLock(&RegistryLock);

    Atmospheres.GenerateValueArray(OutSnapshot);
}


int32 FAndromedaAtmosphereManager::GetNumAtmospheres() const
{
    FScopeLock ScopeLock(&RegistryLock);

    return Atmospheres.Num();
}


int32 FAndromedaAtmosphereManager::Clear()
{
    FScopeLock ScopeLock(&RegistryLock);

    const int32 RemovedCount = Atmospheres.Num();

    Atmospheres.Empty();
    NextHandleId = 1;

    return RemovedCount;
}


FVector FAndromedaAtmosphereManager::GetStarWorldPosition() const
{
    FScopeLock ScopeLock(&RegistryLock);
    return StarWorldPosition;
}