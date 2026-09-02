#include "StarSystemGenerator.h"
#include "AndromedaSeedLibrary.h"

namespace
{
    constexpr int64 RadiusSubsystemID = 1;
    constexpr int64 TerrainSubsystemID = 2;
    constexpr int64 BiomeSubsystemID = 3;
    constexpr int64 VegetationSubsystemID = 4;
    constexpr int64 GeologicalSubsystemID = 5;
    constexpr int64 VolumetricSubsystemID = 6;
}

FStarSystemData UStarSystemGenerator::GenerateSystem(
    int64 UniverseSeed,
    FAndromedaInt64Vector SystemCoordinate
)
{
    FStarSystemData SystemData;

    // Salva le coordinate del sistema
    SystemData.SystemCoordinate = SystemCoordinate;

    // Genera il seed deterministico del sistema
    SystemData.SystemSeed =
        UAndromedaSeedLibrary::GenerateSystemSeed(
            UniverseSeed,
            SystemCoordinate.X,
            SystemCoordinate.Y,
            SystemCoordinate.Z
        );

    // Random deterministico del sistema.
    // Viene usato solo per determinare il numero di pianeti.
    FRandomStream RandomStream(
        static_cast<int32>(
            SystemData.SystemSeed & 0x7FFFFFFF
            )
    );

    // Numero di pianeti
    SystemData.PlanetCount =
        RandomStream.RandRange(1, 8);

    SystemData.Planets.Reserve(
        SystemData.PlanetCount
    );

    // Generazione dei dati di ogni pianeta
    for (int32 PlanetIndex = 0;
        PlanetIndex < SystemData.PlanetCount;
        ++PlanetIndex)
    {
        FPlanetGenerationData PlanetData;

        // ID del pianeta all'interno del sistema
        PlanetData.PlanetID = PlanetIndex;

        // Seed principale del pianeta
        PlanetData.PlanetSeed =
            UAndromedaSeedLibrary::GeneratePlanetSeed(
                SystemData.SystemSeed,
                PlanetData.PlanetID
            );

        // Seed indipendenti dei sottosistemi
        PlanetData.RadiusSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                RadiusSubsystemID
            );

        PlanetData.TerrainSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                TerrainSubsystemID
            );

        PlanetData.BiomeSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                BiomeSubsystemID
            );

        PlanetData.VegetationSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                VegetationSubsystemID
            );

        PlanetData.GeologicalSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                GeologicalSubsystemID
            );

        PlanetData.VolumetricSeed =
            UAndromedaSeedLibrary::GenerateSubsystemSeed(
                PlanetData.PlanetSeed,
                VolumetricSubsystemID
            );

        // Random indipendente per il raggio
        FRandomStream RadiusRandom(
            static_cast<int32>(
                PlanetData.RadiusSeed & 0x7FFFFFFF
                )
        );

        PlanetData.PlanetRadius =
            RadiusRandom.FRandRange(
                300000.0f,
                700000.0f
            );

        // Random indipendente per i parametri del terreno
        FRandomStream TerrainRandom(
            static_cast<int32>(
                PlanetData.TerrainSeed & 0x7FFFFFFF
                )
        );

        PlanetData.TerrainHeight =
            TerrainRandom.FRandRange(
                10000.0f,
                30000.0f
            );

        PlanetData.ContinentalScale =
            TerrainRandom.FRandRange(
                0.3f,
                0.8f
            );

        PlanetData.MountainScale =
            TerrainRandom.FRandRange(
                2.0f,
                5.0f
            );

        PlanetData.DetailScale =
            TerrainRandom.FRandRange(
                8.0f,
                16.0f
            );

        PlanetData.MountainStrength =
            TerrainRandom.FRandRange(
                0.8f,
                2.0f
            );

        PlanetData.DetailStrength =
            TerrainRandom.FRandRange(
                0.05f,
                0.2f
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
    // Genera due volte lo stesso sistema
    const FStarSystemData FirstSystem =
        GenerateSystem(
            UniverseSeed,
            SystemCoordinate
        );

    const FStarSystemData SecondSystem =
        GenerateSystem(
            UniverseSeed,
            SystemCoordinate
        );

    // Il numero di pianeti deve essere identico
    if (FirstSystem.PlanetCount != SecondSystem.PlanetCount)
    {
        return false;
    }

    // Il seed del sistema deve essere identico
    if (FirstSystem.SystemSeed != SecondSystem.SystemSeed)
    {
        return false;
    }

    // Controlla ogni pianeta
    for (int32 PlanetIndex = 0;
        PlanetIndex < FirstSystem.Planets.Num();
        ++PlanetIndex)
    {
        const FPlanetGenerationData& FirstPlanet =
            FirstSystem.Planets[PlanetIndex];

        const FPlanetGenerationData& SecondPlanet =
            SecondSystem.Planets[PlanetIndex];

        if (FirstPlanet.PlanetID != SecondPlanet.PlanetID)
        {
            return false;
        }

        if (FirstPlanet.PlanetSeed != SecondPlanet.PlanetSeed)
        {
            return false;
        }

        if (FirstPlanet.RadiusSeed != SecondPlanet.RadiusSeed)
        {
            return false;
        }

        if (FirstPlanet.TerrainSeed != SecondPlanet.TerrainSeed)
        {
            return false;
        }

        if (FirstPlanet.BiomeSeed != SecondPlanet.BiomeSeed)
        {
            return false;
        }

        if (FirstPlanet.VegetationSeed != SecondPlanet.VegetationSeed)
        {
            return false;
        }

        if (FirstPlanet.GeologicalSeed != SecondPlanet.GeologicalSeed)
        {
            return false;
        }

        if (FirstPlanet.VolumetricSeed != SecondPlanet.VolumetricSeed)
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            FirstPlanet.PlanetRadius,
            SecondPlanet.PlanetRadius))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            FirstPlanet.TerrainHeight,
            SecondPlanet.TerrainHeight))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            FirstPlanet.ContinentalScale,
            SecondPlanet.ContinentalScale))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            FirstPlanet.MountainScale,
            SecondPlanet.MountainScale))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            FirstPlanet.DetailScale,
            SecondPlanet.DetailScale))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            FirstPlanet.MountainStrength,
            SecondPlanet.MountainStrength))
        {
            return false;
        }

        if (!FMath::IsNearlyEqual(
            FirstPlanet.DetailStrength,
            SecondPlanet.DetailStrength))
        {
            return false;
        }
    }

    return true;
}