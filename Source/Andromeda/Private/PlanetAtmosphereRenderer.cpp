#include "PlanetAtmosphereRenderer.h"


FVector UPlanetAtmosphereRenderer::ActivePlanetWorldPosition =
FVector::ZeroVector;

float UPlanetAtmosphereRenderer::ActiveAtmosphereRadius =
0.0f;


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
    int64 InAtmosphereSeed,
    FVector InPlanetWorldPosition
)
{
    GroundRadius =
        InGroundRadius;

    AtmosphereRadius =
        InAtmosphereRadius;

    RayleighScattering =
        InRayleighScattering;

    RayleighScaleHeight =
        InRayleighScaleHeight;

    MieScattering =
        InMieScattering;

    MieAnisotropy =
        InMieAnisotropy;

    MieScaleHeight =
        InMieScaleHeight;

    Absorption =
        InAbsorption;

    StarWorldPosition =
        InStarWorldPosition;

    Quality =
        InQuality;

    AtmosphereSeed =
        InAtmosphereSeed;

    ActivePlanetWorldPosition =
        InPlanetWorldPosition;

    ActiveAtmosphereRadius =
        InAtmosphereRadius;
}


FVector UPlanetAtmosphereRenderer::GetActivePlanetWorldPosition()
{
    return ActivePlanetWorldPosition;
}


float UPlanetAtmosphereRenderer::GetActiveAtmosphereRadius()
{
    return ActiveAtmosphereRadius;
}