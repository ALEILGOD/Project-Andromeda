#include "PlanetAtmosphereRenderer.h"


UPlanetAtmosphereRenderer::UPlanetAtmosphereRenderer()
{
}


void UPlanetAtmosphereRenderer::Initialize(
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
    int64 InAtmosphereSeed
)
{
    // =========================================================
    // ATMOSPHERE GEOMETRY
    // =========================================================

    GroundRadius =
        InGroundRadius;

    AtmosphereRadius =
        InAtmosphereRadius;


    // =========================================================
    // RAYLEIGH
    // =========================================================

    RayleighScattering =
        InRayleighScattering;

    RayleighScaleHeight =
        InRayleighScaleHeight;


    // =========================================================
    // MIE
    // =========================================================

    MieScattering =
        InMieScattering;

    MieAnisotropy =
        InMieAnisotropy;

    MieScaleHeight =
        InMieScaleHeight;


    // =========================================================
    // ABSORPTION
    // =========================================================

    Absorption =
        InAbsorption;


    // =========================================================
    // LIGHTING
    // =========================================================

    StarWorldPosition =
        InStarWorldPosition;


    // =========================================================
    // QUALITY
    // =========================================================

    Quality =
        InQuality;


    // =========================================================
    // DETERMINISTIC SEED
    // =========================================================

    AtmosphereSeed =
        InAtmosphereSeed;
}