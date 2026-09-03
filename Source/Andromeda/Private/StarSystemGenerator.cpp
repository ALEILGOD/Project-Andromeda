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


    // =========================================================
    // ORBIT
    // =========================================================

    /*
     * Distanza minima dell'orbita più interna dal centro
     * del sistema.
     */
    constexpr float MinimumOrbitDistance = 3500000.0f;


    /*
     * Spazio aggiuntivo tra due pianeti.
     *
     * Questo NON è la distanza totale tra le orbite:
     * viene aggiunto oltre ai raggi dei due pianeti.
     */
    constexpr float MinimumOrbitSafetyGap = 900000.0f;


    /*
     * Variazione deterministica applicata alla distanza
     * orbitale dopo aver calcolato la distanza minima sicura.
     *
     * La jitter viene limitata in modo da non poter
     * compromettere la separazione minima.
     */
    constexpr float OrbitJitter = 250000.0f;


    /*
     * Distribuzione angolare iniziale.
     */
    constexpr float GoldenAngle = 137.507764f;
    constexpr float AngleVariation = 25.0f;
    constexpr float MaxInclination = 25.0f;


    /*
     * Periodo orbitale di riferimento.
     *
     * Il valore aumenta con la distanza dal Sole.
     *
     * 30 secondi = riferimento per l'orbita più interna.
     */
    constexpr float MinimumOrbitalPeriod = 30.0f;

    constexpr float OrbitalPeriodExponent = 1.5f;
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


    // =========================================================
    // ORBIT STATE
    // =========================================================
    //
    // Manteniamo la distanza dell'ultima orbita generata.
    //
    // Questo permette di costruire le orbite una dopo
    // l'altra garantendo una distanza minima sufficiente.
    //
    float PreviousOrbitDistance = 0.0f;
    float PreviousPlanetRadius = 0.0f;


    for (
        int32 PlanetIndex = 0;
        PlanetIndex < SystemData.PlanetCount;
        ++PlanetIndex
        )
    {
        FPlanetGenerationData PlanetData;

        PlanetData.PlanetID = PlanetIndex;


        // =====================================================
        // PLANET SEEDS
        // =====================================================

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


        // =====================================================
        // PLANET RADIUS
        // =====================================================

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


        // =====================================================
        // TERRAIN HEIGHT
        // =====================================================

        const float TerrainHeightRatio =
            RadiusRandom.FRandRange(
                MinTerrainHeightRatio,
                MaxTerrainHeightRatio
            );


        PlanetData.TerrainHeight =
            PlanetData.PlanetRadius
            * TerrainHeightRatio;


        // =====================================================
        // TERRAIN PARAMETERS
        // =====================================================

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


        // =====================================================
        // ORBIT RANDOM
        // =====================================================

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


        // =====================================================
        // ORBIT DISTANCE
        // =====================================================
        //
        // La distanza viene costruita in modo cumulativo.
        //
        // Per il primo pianeta:
        //
        //     MinimumOrbitDistance
        //
        // Per i successivi:
        //
        //     precedente orbita
        //     + raggio pianeta precedente
        //     + raggio pianeta corrente
        //     + safety gap
        //
        // In questo modo le superfici dei due pianeti non
        // possono intersecarsi radialmente.
        //
        // IMPORTANTE:
        // il gap è volutamente abbondante perché le orbite
        // possono avere inclinazioni diverse.
        //

        if (PlanetIndex == 0)
        {
            PlanetData.OrbitDistance =
                MinimumOrbitDistance;
        }
        else
        {
            const float MinimumSafeOrbitDistance =
                PreviousOrbitDistance
                + PreviousPlanetRadius
                + PlanetData.PlanetRadius
                + MinimumOrbitSafetyGap;


            const float MaximumAllowedJitter =
                FMath::Min(
                    OrbitJitter,
                    MinimumOrbitSafetyGap * 0.45f
                );


            const float OrbitJitterValue =
                OrbitRandom.FRandRange(
                    -MaximumAllowedJitter,
                    MaximumAllowedJitter
                );


            PlanetData.OrbitDistance =
                MinimumSafeOrbitDistance
                + OrbitJitterValue;


            /*
             * La jitter non deve mai riportare l'orbita
             * sotto la distanza minima sicura.
             */
            PlanetData.OrbitDistance =
                FMath::Max(
                    PlanetData.OrbitDistance,
                    MinimumSafeOrbitDistance
                );
        }


        // =====================================================
        // ORBIT ANGLE
        // =====================================================

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


        // =====================================================
        // ORBIT INCLINATION
        // =====================================================

        PlanetData.OrbitInclination =
            OrbitRandom.FRandRange(
                -MaxInclination,
                MaxInclination
            );


        // =====================================================
        // ORBITAL PERIOD
        // =====================================================
        //
        // Il periodo cresce con la distanza orbitale.
        //
        // Non usiamo PlanetRadius come velocità orbitale:
        // il raggio è una proprietà fisica del pianeta,
        // mentre il periodo dipende principalmente dall'orbita
        // attorno alla stella.
        //

        const float DistanceRatio =
            PlanetData.OrbitDistance
            / MinimumOrbitDistance;


        PlanetData.OrbitalPeriod =
            MinimumOrbitalPeriod
            * FMath::Pow(
                DistanceRatio,
                OrbitalPeriodExponent
            );


        // =====================================================
        // SAVE ORBIT STATE
        // =====================================================

        PreviousOrbitDistance =
            PlanetData.OrbitDistance;

        PreviousPlanetRadius =
            PlanetData.PlanetRadius;


        // =====================================================
        // ADD PLANET
        // =====================================================

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


    for (
        int32 Index = 0;
        Index < First.Planets.Num();
        ++Index
        )
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

        if (!FMath::IsNearlyEqual(
            A.OrbitalPeriod,
            B.OrbitalPeriod
        ))
        {
            return false;
        }
    }


    return true;
}