#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PlanetLandformGenerator.generated.h"

UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetLandformGenerator : public UObject
{
    GENERATED_BODY()

public:

    /**
     * Distribuzione generale delle forme del terreno.
     *
     * 0.0 = pianura
     * 1.0 = territorio predisposto a forme più accidentate
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetLandformMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera delle colline.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetHillMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera delle montagne.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetMountainMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera delle catene montuose.
     *
     * Produce strutture allungate e continue
     * invece di montagne distribuite casualmente.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetMountainChainMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );
};