#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Planet/PlanetSurfaceData.h"
#include "Planet/PlanetProfile.h"
#include "PlanetBiomeGenerator.generated.h"

UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetBiomeGenerator : public UObject
{
    GENERATED_BODY()

public:

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Biome"
    )
    static float CalculateSlope(
        FVector Direction,
        FVector SurfaceNormal
    );

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Biome"
    )
    static float CalculateTemperature(
        FVector Direction,
        float NormalizedHeight,
        int64 Seed
    );

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Biome"
    )
    static float CalculateHumidity(
        FVector Direction,
        float NormalizedHeight,
        int64 Seed
    );

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Biome"
    )
    static FRegionalBiomeAffinities CalculateRegionalAffinities(
        FVector Direction,
        int64 Seed
    );

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Planet"
    )
    static float CalculatePlanetWaterCoverage(
        int64 Seed
    );

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Planet"
    )
    static float CalculateSeaLevelFromWaterCoverage(
        float WaterCoverage
    );

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Biome"
    )
    static FPlanetBiomeData CalculateBiome(
        FVector Direction,
        float NormalizedHeight,
        FVector SurfaceNormal,
        int64 Seed,
        float SeaLevel = 0.0f
    );

    UFUNCTION(
        BlueprintPure,
        Category = "Andromeda|Biome"
    )
    static FPlanetBiomeData CalculateBiomeWithProfile(
        FVector Direction,
        float NormalizedHeight,
        FVector SurfaceNormal,
        int64 Seed,
        const FPlanetProfile& Profile
    );
};