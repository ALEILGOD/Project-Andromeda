#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PlanetAtmosphereRenderer.generated.h"


// =========================================================
// PLANET ATMOSPHERE RENDERER — PHASE 2.1 DEPRECATED
// =========================================================
// Single-planet global-static renderer predating multi-planet
// support. No active path reads it (unified rendering owns
// FUnifiedAtmosphereRenderer). Kept compiling only; removal
// follows visual validation together with UPlanetAtmosphereComponent.

UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetAtmosphereRenderer
    : public UObject
{
    GENERATED_BODY()


public:

    UPlanetAtmosphereRenderer();


    // =========================================================
    // INITIALIZATION
    // =========================================================

    void Initialize(
        float InGroundRadius,
        float InAtmosphereRadius,
        FVector InRayleighScattering,
        FVector InMieScattering,
        float InMieAnisotropy,
        FVector InAbsorption,
        float InRayleighScaleHeight,
        float InMieScaleHeight,
        FVector InStarWorldPosition,
        uint8 InQuality,
        int64 InAtmosphereSeed,
        FVector InPlanetWorldPosition,
        float InTerrainHeight
    );


    // =========================================================
    // ACTIVE PLANET DATA
    // =========================================================

    static FVector GetActivePlanetWorldPosition();

    static float GetActiveGroundRadius();

    static float GetActiveAtmosphereRadius();

    static float GetActiveTerrainHeight();

    static FVector GetActiveStarWorldPosition();


    // =========================================================
    // GEOMETRY
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS"
    )
    float GroundRadius = 0.0f;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS"
    )
    float AtmosphereRadius = 0.0f;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS"
    )
    float TerrainHeight = 0.0f;


    // =========================================================
    // RAYLEIGH
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS|Rayleigh"
    )
    FVector RayleighScattering =
        FVector::ZeroVector;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS|Rayleigh"
    )
    float RayleighScaleHeight = 0.0f;


    // =========================================================
    // MIE
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS|Mie"
    )
    FVector MieScattering =
        FVector::ZeroVector;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS|Mie"
    )
    float MieAnisotropy = 0.0f;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS|Mie"
    )
    float MieScaleHeight = 0.0f;


    // =========================================================
    // ABSORPTION
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS"
    )
    FVector Absorption =
        FVector::ZeroVector;


    // =========================================================
    // LIGHTING
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS|Lighting"
    )
    FVector StarWorldPosition =
        FVector::ZeroVector;


    // =========================================================
    // QUALITY
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS"
    )
    uint8 Quality = 0;


    // =========================================================
    // SEED
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|UAS"
    )
    int64 AtmosphereSeed = 0;


private:

    static FVector ActivePlanetWorldPosition;

    static float ActiveGroundRadius;

    static float ActiveAtmosphereRadius;

    static float ActiveTerrainHeight;

    static FVector ActiveStarWorldPosition;
};