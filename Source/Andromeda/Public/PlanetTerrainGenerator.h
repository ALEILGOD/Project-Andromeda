#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
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
        int32 Seed,
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
        int32 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight
    ) const;
};