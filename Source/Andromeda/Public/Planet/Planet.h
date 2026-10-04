#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Planet/PlanetProfile.h"
#include "Planet.generated.h"

class UProceduralMeshComponent;
class UPlanetTerrainGenerator;
class UPlanetaryLightingComponent;
class UHillairePlanetLinkComponent;

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
        Category = "Andromeda|Planet|Lighting"
    )
    TObjectPtr<UPlanetaryLightingComponent> PlanetaryLighting;

    /**
     * Feeds this procedural planet into the Hillaire planet feed
     * (Phase 2F: live center, ground radius, terrain height, atmosphere
     * height). Constructor-owned: every generated APlanet automatically
     * carries exactly one. Plain ActorComponent: never attached to the
     * generated mesh itself.
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Planet"
    )
    TObjectPtr<UHillairePlanetLinkComponent> HillairePlanetLink;

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

    /** Per-body gravity. Zero uses the star system's generated density model. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Planet|Gravity", meta = (ClampMin = "0.0"))
    double SurfaceGravity = 0.0;

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
};
