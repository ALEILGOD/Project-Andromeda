#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"


// =========================================================
// ANDROMEDA ATMOSPHERE MANAGER — PHASE 2.1 DEPRECATED
// =========================================================
// Replaced as data owner by FAndromedaAtmosphereSystem
// (Atmosphere/AndromedaAtmosphereSystem.h), the single mailbox of
// the unified atmosphere. The Registry no longer registers handles
// here and no render stage reads from here. This class still
// compiles and works but is off the active path; it will be
// removed after visual validation. Do not add new callers.

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
    // STAR POSITION SNAPSHOT (Game Thread -> Render Thread)
    // =========================================================

    bool SetStarWorldPosition(const FVector& WorldPosition)
    {
        FScopeLock ScopeLock(&RegistryLock);
        StarWorldPosition = WorldPosition;
        return true;
    }
    FVector GetStarWorldPosition() const;


    // =========================================================
    // QUERIES
    // =========================================================

    bool FindAtmosphere(
        FAndromedaAtmosphereHandle Handle,
        FLegacyAndromedaAtmosphereInstance& OutInstance
    ) const;


    void GetAtmosphereSnapshot(
        TArray<FLegacyAndromedaAtmosphereInstance>& OutSnapshot
    ) const;


    int32 GetNumAtmospheres() const;


    // Empties the registry and resets the handle id sequence.
    // Returns the number of atmosphere instances that were removed.
    int32 Clear();


private:

    FAndromedaAtmosphereManager() = default;


    mutable FCriticalSection RegistryLock;

    TMap<uint32, FLegacyAndromedaAtmosphereInstance> Atmospheres;

    uint32 NextHandleId = 1;

    // Star position snapshot written on the Game Thread.
    FVector StarWorldPosition = FVector::ZeroVector;
};