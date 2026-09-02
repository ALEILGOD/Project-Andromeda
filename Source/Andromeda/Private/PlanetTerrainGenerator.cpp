#include "PlanetTerrainGenerator.h"
#include "AndromedaNoiseLibrary.h"

float UPlanetTerrainGenerator::GetTerrainHeight(
    FVector Direction,
    int32 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
) const
{
    return UAndromedaNoiseLibrary::GeneratePlanetHeight(
        Direction,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength
    ) * TerrainHeight;
}

FPlanetSurfaceData UPlanetTerrainGenerator::GetSurfaceData(
    FVector Direction,
    int32 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
) const
{
    FPlanetSurfaceData SurfaceData;

    Direction = Direction.GetSafeNormal();

    SurfaceData.Direction = Direction;

    SurfaceData.Height = GetTerrainHeight(
        Direction,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength,
        TerrainHeight
    );

    SurfaceData.NormalizedHeight =
        TerrainHeight != 0.0f
        ? SurfaceData.Height / TerrainHeight
        : 0.0f;

    SurfaceData.Normal =
        Direction;

    return SurfaceData;
}