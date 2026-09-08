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
// =========================================================

UCLASS(Blueprintable, Category = "Andromeda|Atmosphere")
class ANDROMEDA_API AAndromedaAtmosphereRegistry : public AActor
{
    GENERATED_BODY()


public:

    AAndromedaAtmosphereRegistry();


    virtual void BeginPlay() override;

    virtual void Tick(float DeltaTime) override;


    // Multiplier for the atmosphere radius convention:
    //     AtmosphereRadius = (PlanetRadius + TerrainHeight) * AtmosphereRadiusMultiplier
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float AtmosphereRadiusMultiplier = 1.3f;


private:

    // Finds the StarSystem (if needed) and synchronizes the
    // registered atmospheres with the current planet list.
    void SyncAtmospheres();


    // Handles of the atmospheres registered for each planet.
    // Index corresponds to the planet index from GetAllPlanetRuntimeData.
    TArray<FAndromedaAtmosphereHandle> AtmosphereHandles;


    // Cached reference to the StarSystem actor.
    TWeakObjectPtr<AStarSystem> StarSystem;


    // True once the StarSystem has been found.
    bool bStarSystemFound = false;


    // Retry counter for finding the StarSystem (it may not exist at BeginPlay).
    int32 FindRetryCount = 0;


    static constexpr int32 MaxFindRetries = 120;
};
