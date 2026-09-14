#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Planet/PlanetProfile.h"
#include "Planet/Zephyr/ZephyrTypes.h"
#include "ZephyrProfileLibrary.generated.h"

// =========================================================
// ZEPHYR PROFILE LIBRARY (ZEPHYR-01 §6/§7)
// =========================================================
// Builds the physical atmosphere profile of a planet from its
// deterministic identity (seed + archetype + geometry).
//
// PLANET VARIATION RULE: planets differ ONLY through physical
// parameters - Rayleigh/Mie coefficients, scale heights,
// absorption strength, ground albedo, density. There is no
// RandomSkyColor / RandomHorizonColor anywhere in this path:
// zenith, horizon, sunset and night colors EMERGE from the
// radiative transfer through these parameters.
UCLASS(BlueprintType)
class ANDROMEDA_API UZephyrProfileLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Andromeda|Zephyr")
    static FZephyrPlanetProfile BuildProfile(
        int64 PlanetSeed,
        EPlanetArchetype Archetype,
        float SurfaceRadiusCm,
        float AtmosphereRadiusCm
    );
};
