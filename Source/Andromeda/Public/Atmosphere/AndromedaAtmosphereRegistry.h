#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"
#include "Atmosphere/AndromedaAtmosphereManager.h"
#include "StarSystem.h"
#include "AndromedaAtmosphereRegistry.generated.h"


// =========================================================
// ANDROMEDA ATMOSPHERE REGISTRY
// =========================================================
// Connects StarSystem planets to the AtmosphereManager.
// On BeginPlay it finds the AStarSystem actor and registers one
// atmosphere per planet. Each frame it updates the atmosphere
// world positions to follow the planets.
//
// The StarSystem may not have generated its planets yet when this
// registry starts: instead of a frame-based retry budget, the
// registry retries from Tick within a time window
// (StarSystemSearchTimeoutSeconds) and gives up only if it expires.
// =========================================================

UCLASS(Blueprintable, Category = "Andromeda|Atmosphere")
class ANDROMEDA_API AAndromedaAtmosphereRegistry : public AActor
{
    GENERATED_BODY()


public:

    AAndromedaAtmosphereRegistry();


    virtual void BeginPlay() override;

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    virtual void Tick(float DeltaTime) override;


    // Multiplier for the atmosphere radius convention:
    //     AtmosphereRadius = (PlanetRadius + TerrainHeight) * AtmosphereRadiusMultiplier
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float AtmosphereRadiusMultiplier = 1.1f;


private:

    // Finds the StarSystem (if needed) and synchronizes the
    // registered atmospheres with the current planet list.
    void SyncAtmospheres();


    // Expires the search window if StarSystemSearchTimeoutSeconds have
    // elapsed since the first sync attempt without success (logs once).
    void CheckSearchWindowExpired(UWorld* World);


    // Handles of the atmospheres registered for each planet.
    // Index corresponds to the planet index from GetAllPlanetRuntimeData.
    TArray<FAndromedaAtmosphereHandle> AtmosphereHandles;


    // Cached reference to the StarSystem actor.
    TWeakObjectPtr<AStarSystem> StarSystem;


    // True once the StarSystem has been found AND its planets are available
    // (latched in SyncAtmospheres: no more search retries afterwards).
    bool bStarSystemFound = false;


    // Time window (in world-time seconds) during which the registry keeps
    // searching for the StarSystem and its planets before giving up.
    // Replaces the old frame-based retry budget: the StarSystem spawns its
    // planets over several seconds (~4 s for the first one), so a frame
    // budget could expire before any planet exists.
    static constexpr float StarSystemSearchTimeoutSeconds = 10.0f;


    // World time at which the search window started (< 0 = not started yet).
    double SearchStartWorldSeconds = -1.0;


    // True once the search window has expired without a successful sync:
    // the registry stops retrying (single warning log, no per-frame spam).
    bool bSearchWindowExpired = false;


    // Legacy frame-based retry budget (ATMOS-03). Retained only for
    // compatibility: it is no longer the primary limit for the initial
    // StarSystem sync, which is now time-window based.
    int32 FindRetryCount = 0;


    static constexpr int32 MaxFindRetries = 120;
};
