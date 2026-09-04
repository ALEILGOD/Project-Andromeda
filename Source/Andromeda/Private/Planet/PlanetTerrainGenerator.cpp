#include "Planet/PlanetTerrainGenerator.h"
#include "Planet/PlanetBiomeGenerator.h"
#include "AndromedaNoiseLibrary.h"

float UPlanetTerrainGenerator::GetTerrainHeight(
    FVector Direction,
    int64 Seed,
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

FVector UPlanetTerrainGenerator::GetTerrainNormal(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
) const
{
    return UAndromedaNoiseLibrary::CalculatePlanetSurfaceNormal(
        Direction,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength,
        TerrainHeight
    );
}

FPlanetSurfaceData UPlanetTerrainGenerator::GetSurfaceData(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
) const
{
    FPlanetSurfaceData SurfaceData = UAndromedaNoiseLibrary::GetPlanetSurfaceData(
        Direction,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength,
        TerrainHeight
    );

    const float NormalizedHeight = (TerrainHeight > 0.0f)
        ? (SurfaceData.Height / TerrainHeight)
        : 0.0f;

    SurfaceData.NormalizedHeight = NormalizedHeight;
    SurfaceData.Slope = UPlanetBiomeGenerator::CalculateSlope(Direction, SurfaceData.Normal);
    SurfaceData.BiomeData = UPlanetBiomeGenerator::CalculateBiome(
        Direction,
        NormalizedHeight,
        SurfaceData.Normal,
        Seed
    );

    return SurfaceData;
}

FPlanetBiomeData UPlanetTerrainGenerator::GetBiomeData(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
) const
{
    return GetSurfaceData(
        Direction,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength,
        TerrainHeight
    ).BiomeData;
}

void UPlanetTerrainGenerator::GenerateTerrainMeshData(
    int32 Resolution,
    float PlanetRadius,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals,
    TArray<FProcMeshTangent>& OutTangents
) const
{
    UAndromedaNoiseLibrary::GeneratePlanetMeshData(
        Resolution,
        PlanetRadius,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength,
        TerrainHeight,
        OutVertices,
        OutTriangles,
        OutNormals,
        OutTangents
    );
}