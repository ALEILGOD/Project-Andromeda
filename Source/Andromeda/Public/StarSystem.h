#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarSystemGenerator.h"
#include "StarSystem.generated.h"


USTRUCT()
struct FSpawnedPlanetData
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<AActor> PlanetActor = nullptr;

    UPROPERTY()
    FPlanetGenerationData GenerationData;
};


UCLASS()
class ANDROMEDA_API AStarSystem : public AActor
{
    GENERATED_BODY()


public:

    AStarSystem();


protected:

    virtual void BeginPlay() override;

    virtual void Tick(
        float DeltaTime
    ) override;


public:

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    int64 UniverseSeed = 1234567890123456;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    FAndromedaInt64Vector SystemCoordinate;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    TSubclassOf<AActor> PlanetClass;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    TSubclassOf<AActor> SunClass;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    float SimulationTimeScale = 1.0f;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|Star System"
    )
    FStarSystemData SystemData;


private:

    // =========================================================
    // SPAWN
    // =========================================================

    void SpawnSun();

    void SpawnPlanets();


    // =========================================================
    // SIMULATION
    // =========================================================

    void UpdatePlanetOrbits(
        float DeltaTime
    );

    void UpdatePlanetRotations(
        float DeltaTime
    );


    // =========================================================
    // ORBIT
    // =========================================================

    FVector CalculateOrbitPosition(
        const FPlanetGenerationData& PlanetData,
        float SimulationTime
    ) const;


    // =========================================================
    // ROTATION
    // =========================================================

    FRotator CalculatePlanetRotation(
        const FPlanetGenerationData& PlanetData,
        float SimulationTime
    ) const;

    float CalculateRotationPeriod(
        const FPlanetGenerationData& PlanetData
    ) const;

    float CalculateAxialTilt(
        const FPlanetGenerationData& PlanetData
    ) const;

    float CalculateInitialRotation(
        const FPlanetGenerationData& PlanetData
    ) const;

    float CalculateRotationDirection(
        const FPlanetGenerationData& PlanetData
    ) const;


    // =========================================================
    // PLANET DATA
    // =========================================================

    bool SetPlanetGenerationData(
        AActor* PlanetActor,
        const FPlanetGenerationData& PlanetData
    );


private:

    // =========================================================
    // SPAWNED STAR
    // =========================================================

    UPROPERTY(Transient)
    TObjectPtr<AActor> SpawnedSun = nullptr;


    // =========================================================
    // SPAWNED PLANETS
    // =========================================================

    UPROPERTY(Transient)
    TArray<FSpawnedPlanetData> SpawnedPlanets;


    // =========================================================
    // SIMULATION TIME
    // =========================================================

    float SystemSimulationTime = 0.0f;
};