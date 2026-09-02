#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Planet.generated.h"

class UProceduralMeshComponent;
class UPlanetTerrainGenerator;

UCLASS()
class ANDROMEDA_API APlanet : public AActor
{
    GENERATED_BODY()

public:

    APlanet();

protected:

    virtual void BeginPlay() override;

    virtual void OnConstruction(
        const FTransform& Transform
    ) override;

public:

    // =========================================================
    // ROOT
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet"
    )
    TObjectPtr<USceneComponent> Root;


    // =========================================================
    // PLANET SURFACE
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet"
    )
    TObjectPtr<UProceduralMeshComponent> PlanetProceduralMesh;


    // =========================================================
    // PLANET PARAMETERS
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet",
        meta = (ExposeOnSpawn = "true")
    )
    int64 PlanetSeed = 0;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet",
        meta = (ExposeOnSpawn = "true")
    )
    float PlanetRadius = 500000.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet",
        meta = (ExposeOnSpawn = "true")
    )
    float TerrainHeight = 20000.0f;


    // =========================================================
    // TERRAIN
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet|Terrain"
    )
    int32 Resolution = 150;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet|Terrain"
    )
    float ContinentalScale = 0.5f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet|Terrain"
    )
    float MountainScale = 3.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet|Terrain"
    )
    float DetailScale = 12.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet|Terrain"
    )
    float MountainStrength = 1.5f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet|Terrain"
    )
    float DetailStrength = 0.1f;


    // =========================================================
    // TERRAIN GENERATOR
    // =========================================================

    UPROPERTY(
        Transient,
        BlueprintReadOnly,
        Category = "Andromeda|Planet"
    )
    TObjectPtr<UPlanetTerrainGenerator> TerrainGenerator;


protected:

    // =========================================================
    // INITIALIZATION
    // =========================================================

    void InitializePlanet();

    void GeneratePlanetMesh();
};