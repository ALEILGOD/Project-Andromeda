#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProceduralMeshComponent.h"
#include "PlanetSurfaceData.h"
#include "AndromedaNoiseLibrary.generated.h"

UCLASS()
class ANDROMEDA_API UAndromedaNoiseLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain")
    static float GeneratePlanetHeight(
        FVector Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain")
    static FVector CalculatePlanetSurfaceNormal(
        FVector Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain")
    static FPlanetSurfaceData GetPlanetSurfaceData(
        FVector Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight
    );

    UFUNCTION(BlueprintCallable, Category = "Andromeda|Planet")
    static void GeneratePlanetVertices(
        int32 Resolution,
        float PlanetRadius,
        int32 FaceIndex,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight,
        TArray<FVector>& OutVertices
    );

    UFUNCTION(BlueprintCallable, Category = "Andromeda|Planet")
    static void GeneratePlanetMeshData(
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
    );
};