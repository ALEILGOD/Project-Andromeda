#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Planet/PlanetProfile.h"
#include "Planet.generated.h"

class UProceduralMeshComponent;
class UPlanetTerrainGenerator;
class UPlanetAtmosphereComponent;
class UPlanetaryLightingComponent;

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

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet"
    )
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet"
    )
    TObjectPtr<UProceduralMeshComponent> PlanetProceduralMesh;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet|Atmosphere"
    )
    TObjectPtr<UPlanetAtmosphereComponent> Atmosphere;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet|Atmosphere"
    )
    TObjectPtr<UProceduralMeshComponent> AtmosphereMesh;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet|Lighting"
    )
    TObjectPtr<UPlanetaryLightingComponent> PlanetaryLighting;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet",
        meta = (ExposeOnSpawn = "true")
    )
    int64 PlanetID = 0;

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
    float OrbitDistance = 0.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planet|Profile"
    )
    EPlanetArchetype PlanetArchetype = EPlanetArchetype::Terran;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet|Profile"
    )
    FPlanetProfile PlanetProfile;

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

    UPROPERTY(
        Transient,
        BlueprintReadOnly,
        Category = "Andromeda|Planet"
    )
    TObjectPtr<UPlanetTerrainGenerator> TerrainGenerator;

protected:

    void InitializePlanet();

    void GeneratePlanetMesh();

    void GenerateAtmosphereMesh();
};