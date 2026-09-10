#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"


// =========================================================
// ANDROMEDA ATMOSPHERE MANAGER
// =========================================================

class ANDROMEDA_API FAndromedaAtmosphereManager
{


public:

    static FAndromedaAtmosphereManager& Get();


    // =========================================================
    // REGISTRY
    // =========================================================

    FAndromedaAtmosphereHandle RegisterAtmosphere(
        const FAndromedaAtmosphereInstanceDesc& Desc
    );


    bool UnregisterAtmosphere(
        FAndromedaAtmosphereHandle Handle
    );


    bool UpdateAtmosphereParameters(
        FAndromedaAtmosphereHandle Handle,
        const FAndromedaAtmosphereParameters& Parameters
    );


    bool UpdateAtmosphereWorldPosition(
        FAndromedaAtmosphereHandle Handle,
        const FVector& WorldPosition
    );


    // =========================================================
    // QUERIES
    // =========================================================

    bool FindAtmosphere(
        FAndromedaAtmosphereHandle Handle,
        FAndromedaAtmosphereInstance& OutInstance
    ) const;


    void GetAtmosphereSnapshot(
        TArray<FAndromedaAtmosphereInstance>& OutSnapshot
    ) const;


    int32 GetNumAtmospheres() const;


    // Empties the registry and resets the handle id sequence.
    // Returns the number of atmosphere instances that were removed.
    int32 Clear();


private:

    FAndromedaAtmosphereManager() = default;


    mutable FCriticalSection RegistryLock;

    TMap<uint32, FAndromedaAtmosphereInstance> Atmospheres;

    uint32 NextHandleId = 1;
};