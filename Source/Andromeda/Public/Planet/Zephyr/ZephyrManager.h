#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Planet/Zephyr/ZephyrTypes.h"

// =========================================================
// ZEPHYR MANAGER (ZEPHYR-01) — PHASE 2.1 DEPRECATED
// =========================================================
// Replaced as data owner by FAndromedaAtmosphereSystem
// (Atmosphere/AndromedaAtmosphereSystem.h), the single mailbox of
// the unified atmosphere. This class still compiles and works but
// NOTHING in the active path writes or reads it anymore; it will
// be removed after visual validation. Do not add new callers.
//
// Thread-safe game-thread -> render-thread mailbox for the
// per-planet sky data.
//
// The atmosphere registry (game thread) publishes one snapshot
// entry per planet (physical profile + world center) plus the
// star world position every frame. The ZEPHYR renderer (render
// thread) takes a copy and builds LUTs / sky radiance from it.
//
// ZEPHYR never touches UWorld / AActor / PlayerController:
// this mailbox is its only input, which is why the sky works
// identically for the game camera, PIE, the editor viewport
// camera and the photo-mode camera (all provide FSceneView).
class ANDROMEDA_API FZephyrManager
{
public:
    static FZephyrManager& Get();

    // Game thread: replace the whole snapshot (planets + star).
    void SetSnapshot(
        const TArray<FZephyrPlanetSnapshotEntry>& InPlanets,
        const FVector& InStarWorldPosition
    );

    // Game thread: drop everything (world teardown).
    void Clear();

    // Render thread: copy out the current snapshot.
    void GetSnapshot(
        TArray<FZephyrPlanetSnapshotEntry>& OutPlanets,
        FVector& OutStarWorldPosition,
        uint64& OutVersion
    ) const;

    // Any thread: current planet count (cheap validation gate).
    int32 GetPlanetCount() const
    {
        FScopeLock ScopeLock(&SnapshotLock);
        return Planets.Num();
    }

private:
    FZephyrManager() = default;

    mutable FCriticalSection SnapshotLock;

    TArray<FZephyrPlanetSnapshotEntry> Planets;

    FVector StarWorldPosition = FVector::ZeroVector;

    uint64 SnapshotVersion = 0;
};
