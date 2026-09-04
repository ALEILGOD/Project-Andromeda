#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PlanetContinentalGenerator.generated.h"

UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetContinentalGenerator : public UObject
{
    GENERATED_BODY()

public:

    /**
     * Genera una maschera continentale deterministica.
     *
     * 0.0 = oceano
     * 1.0 = continente
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Continents")
    static float GetContinentalMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );
};