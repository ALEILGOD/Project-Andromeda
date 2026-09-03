#include "PlanetAtmosphereRenderer.h"


// =========================================================
// STATIC DATA
// =========================================================

FVector UPlanetAtmosphereRenderer::ActivePlanetWorldPosition =
FVector::ZeroVector;


float UPlanetAtmosphereRenderer::ActiveGroundRadius =
0.0f;


float UPlanetAtmosphereRenderer::ActiveAtmosphereRadius =
0.0f;


float UPlanetAtmosphereRenderer::ActiveTerrainHeight =
0.0f;


FVector UPlanetAtmosphereRenderer::ActiveStarWorldPosition =
FVector::ZeroVector;


// =========================================================
// CONSTRUCTOR
// =========================================================

UPlanetAtmosphereRenderer::UPlanetAtmosphereRenderer()
{
}


// =========================================================
// INITIALIZE
// =========================================================

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
    FVector InPlanetWorldPosition,
    float InTerrainHeight
)
{
    GroundRadius =
        InGroundRadius;


    AtmosphereRadius =
        InAtmosphereRadius;


    TerrainHeight =
        InTerrainHeight;


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


    // =========================================================
    // ACTIVE DATA
    // =========================================================

    ActivePlanetWorldPosition =
        InPlanetWorldPosition;


    ActiveGroundRadius =
        InGroundRadius;


    ActiveAtmosphereRadius =
        InAtmosphereRadius;


    ActiveTerrainHeight =
        InTerrainHeight;


    ActiveStarWorldPosition =
        InStarWorldPosition;
}


// =========================================================
// ACTIVE PLANET POSITION
// =========================================================

FVector UPlanetAtmosphereRenderer::GetActivePlanetWorldPosition()
{
    return ActivePlanetWorldPosition;
}


// =========================================================
// ACTIVE GROUND RADIUS
// =========================================================

float UPlanetAtmosphereRenderer::GetActiveGroundRadius()
{
    return ActiveGroundRadius;
}


// =========================================================
// ACTIVE ATMOSPHERE RADIUS
// =========================================================

float UPlanetAtmosphereRenderer::GetActiveAtmosphereRadius()
{
    return ActiveAtmosphereRadius;
}


// =========================================================
// ACTIVE TERRAIN HEIGHT
// =========================================================

float UPlanetAtmosphereRenderer::GetActiveTerrainHeight()
{
    return ActiveTerrainHeight;
}


// =========================================================
// ACTIVE STAR POSITION
// =========================================================

FVector UPlanetAtmosphereRenderer::GetActiveStarWorldPosition()
{
    return ActiveStarWorldPosition;
}