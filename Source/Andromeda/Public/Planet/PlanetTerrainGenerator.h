#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ProceduralMeshComponent.h"
#include "PlanetSurfaceData.h"
#include "PlanetTerrainGenerator.generated.h"

UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetTerrainGenerator : public UObject
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain")
    float GetTerrainHeight(
        FVector Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight
    ) const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain")
    FVector GetTerrainNormal(
        FVector Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight
    ) const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain")
    FPlanetSurfaceData GetSurfaceData(
        FVector Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight
    ) const;

    UFUNCTION(BlueprintCallable, Category = "Andromeda|Terrain")
    void GenerateTerrainMeshData(
        int32 Resolution,
        float PlanetRadius,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight,
        TArray<FVector>& OutVertices,
        TArray<int32>& OutTriangles,
        TArray<FVector>& OutNormals,
        TArray<FProcMeshTangent>& OutTangents
    ) const;
};
