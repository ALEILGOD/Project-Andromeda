#include "StarSystemGenerator.h"

#include "AndromedaSeedLibrary.h"

namespace
{
    constexpr int32 MinPlanets = 3;
    constexpr int32 MaxPlanets = 8;

    constexpr float MinPlanetRadius = 250000.0f;
    constexpr float MaxPlanetRadius = 1200000.0f;

    constexpr float MinTerrainHeightRatio = 0.035f;
    constexpr float MaxTerrainHeightRatio = 0.090f;

    constexpr float MinimumOrbitDistance = 2000000.0f;
    constexpr float OrbitSpacing = 1100000.0f;
    constexpr float OrbitJitter = 350000.0f;

    constexpr float GoldenAngle = 137.507764f;
    constexpr float AngleVariation = 25.0f;
    constexpr float MaxInclination = 25.0f;
}

FStarSystemData UStarSystemGenerator::GenerateSystem(
    int64 UniverseSeed,
    FAndromedaInt64Vector SystemCoordinate
)
{
    FStarSystemData SystemData;

    SystemData.SystemCoordinate = SystemCoordinate;

    SystemData.SystemSeed =
        UAndromedaSeedLibrary::GenerateSystemSeed(
            UniverseSeed,
            SystemCoordinate.X,
            SystemCoordinate.Y,
            SystemCoordinate.Z
        );

    FRandomStream SystemRandom(
        static_cast<int32>(
            UAndromedaSeedLibrary::HashSeed(
                SystemData.SystemSeed
            )
            )
    );

    SystemData.PlanetCount =
        SystemRandom.RandRange(
            MinPlanets,
            MaxPlanets
        );

    SystemData.Planets.Reserve(
        SystemData.PlanetCount
    );

    for (int32 PlanetIndex = 0;
        PlanetIndex < SystemData.PlanetCount;
        ++PlanetIndex)
    {
        FPlanetGenerationData PlanetData;

        PlanetData.PlanetID = PlanetIndex;

        PlanetData.PlanetSeed =
            UAndromedaSeedLibrary::GeneratePlanetSeed(
                SystemData.SystemSeed,
                PlanetData.PlanetID
            );

        PlanetData.RadiusSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                0
            );

        PlanetData.TerrainSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                1
            );

        PlanetData.BiomeSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                2
            );

        PlanetData.VegetationSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                3
            );

        PlanetData.GeologicalSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                4
            );

        PlanetData.VolumetricSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                5
            );

        /*
         * ------------------------------------------------------------
         * PLANET SIZE
         * ------------------------------------------------------------
         *
         * Ogni pianeta riceve una dimensione indipendente e
         * deterministica dal proprio PlanetSeed.
         *
         * Range:
         * 250000 cm = 2.5 km
         * 1200000 cm = 12 km
         */
        FRandomStream RadiusRandom(
            static_cast<int32>(
                UAndromedaSeedLibrary::HashSeed(
                    PlanetData.RadiusSeed
                )
                )
        );

        PlanetData.PlanetRadius =
            RadiusRandom.FRandRange(
                MinPlanetRadius,
                MaxPlanetRadius
            );

        /*
         * ------------------------------------------------------------
         * TERRAIN HEIGHT
         * ------------------------------------------------------------
         *
         * L'altezza del terreno dipende dalla dimensione del pianeta,
         * ma mantiene una variazione indipendente.
         *
         * Un pianeta più grande può quindi avere montagne
         * proporzionalmente più alte.
         */
        const float TerrainHeightRatio =
            RadiusRandom.FRandRange(
                MinTerrainHeightRatio,
                MaxTerrainHeightRatio
            );

        PlanetData.TerrainHeight =
            PlanetData.PlanetRadius
            * TerrainHeightRatio;

        /*
         * ------------------------------------------------------------
         * TERRAIN PARAMETERS
         * ------------------------------------------------------------
         */

        FRandomStream TerrainRandom(
            static_cast<int32>(
                UAndromedaSeedLibrary::HashSeed(
                    PlanetData.TerrainSeed
                )
                )
        );

        PlanetData.ContinentalScale =
            TerrainRandom.FRandRange(
                0.35f,
                0.70f
            );

        PlanetData.MountainScale =
            TerrainRandom.FRandRange(
                2.0f,
                4.5f
            );

        PlanetData.DetailScale =
            TerrainRandom.FRandRange(
                8.0f,
                16.0f
            );

        PlanetData.MountainStrength =
            TerrainRandom.FRandRange(
                1.0f,
                2.0f
            );

        PlanetData.DetailStrength =
            TerrainRandom.FRandRange(
                0.05f,
                0.15f
            );

        /*
         * ------------------------------------------------------------
         * ORBIT
         * ------------------------------------------------------------
         *
         * Le orbite rimangono ordinate per distanza dal sistema,
         * ma con jitter deterministico.
         */

        FRandomStream OrbitRandom(
            static_cast<int32>(
                UAndromedaSeedLibrary::HashSeed(
                    UAndromedaSeedLibrary::GenerateSubsystemSeed(
                        PlanetData.PlanetSeed,
                        10
                    )
                )
                )
        );

        const float BaseOrbitDistance =
            MinimumOrbitDistance
            + PlanetIndex * OrbitSpacing;

        const float OrbitJitterValue =
            OrbitRandom.FRandRange(
                -OrbitJitter,
                OrbitJitter
            );

        PlanetData.OrbitDistance =
            BaseOrbitDistance
            + OrbitJitterValue;

        /*
         * Ogni pianeta viene distribuito usando il Golden Angle,
         * con una variazione deterministica.
         */
        const float AngleOffset =
            OrbitRandom.FRandRange(
                -AngleVariation,
                AngleVariation
            );

        PlanetData.OrbitAngle =
            PlanetIndex * GoldenAngle
            + AngleOffset;

        PlanetData.OrbitAngle =
            FMath::Fmod(
                PlanetData.OrbitAngle,
                360.0f
            );

        if (PlanetData.OrbitAngle < 0.0f)
        {
            PlanetData.OrbitAngle += 360.0f;
        }

        /*
         * Inclinazione orbitale deterministica.
         */
        PlanetData.OrbitInclination =
            OrbitRandom.FRandRange(
                -MaxInclination,
                MaxInclination
            );

        SystemData.Planets.Add(
            PlanetData
        );
    }

    return SystemData;
}

bool UStarSystemGenerator::VerifyDeterminism(
    int64 UniverseSeed,
    FAndromedaInt64Vector SystemCoordinate
)
{
    const FStarSystemData First =
        GenerateSystem(
            UniverseSeed,
            SystemCoordinate
        );

    const FStarSystemData Second =
        GenerateSystem(
            UniverseSeed,
            SystemCoordinate
        );

    if (First.SystemSeed != Second.SystemSeed)
    {
        return false;
    }

    if (First.PlanetCount != Second.PlanetCount)
    {
        return false;
    }

    if (First.Planets.Num() != Second.Planets.Num())
    {
        return false;
    }

    for (int32 Index = 0;
        Index < First.Planets.Num();
        ++Index)
    {
        const FPlanetGenerationData& A =
            First.Planets[Index];

        const FPlanetGenerationData& B =
            Second.Planets[Index];

        if (A.PlanetID != B.PlanetID)
        {
            return false;
        }

        if (A.PlanetSeed != B.PlanetSeed)
        {
            return false;
        }

        if (A.RadiusSeed != B.RadiusSeed)
        {
            return false;
        }

        if (A.TerrainSeed != B.TerrainSeed)
        {
            return false;
        }

        if (A.BiomeSeed != B.BiomeSeed)
        {
            return false;
        }

        if (A.VegetationSeed != B.VegetationSeed)
        {
            return false;
        }

        if (A.GeologicalSeed != B.GeologicalSeed)
        {
            return false;
        }

        if (A.VolumetricSeed != B.VolumetricSeed)
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.PlanetRadius,
            B.PlanetRadius
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.TerrainHeight,
            B.TerrainHeight
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.ContinentalScale,
            B.ContinentalScale
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.MountainScale,
            B.MountainScale
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.DetailScale,
            B.DetailScale
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.MountainStrength,
            B.MountainStrength
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.DetailStrength,
            B.DetailStrength
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.OrbitDistance,
            B.OrbitDistance
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.OrbitAngle,
            B.OrbitAngle
        ))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            A.OrbitInclination,
            B.OrbitInclination
        ))
        {
            return false;
        }
    }

    return true;
}