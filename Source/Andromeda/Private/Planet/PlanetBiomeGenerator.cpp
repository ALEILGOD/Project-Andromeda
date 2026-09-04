#include "Planet/PlanetBiomeGenerator.h"

float UPlanetBiomeGenerator::CalculateSlope(
    FVector Direction,
    FVector SurfaceNormal
)
{
    Direction = Direction.GetSafeNormal();
    SurfaceNormal = SurfaceNormal.GetSafeNormal();

    const float Dot = FVector::DotProduct(Direction, SurfaceNormal);
    return 1.0f - FMath::Clamp(Dot, 0.0f, 1.0f);
}

float UPlanetBiomeGenerator::CalculateTemperature(
    FVector Direction,
    float NormalizedHeight,
    int64 Seed
)
{
    Direction = Direction.GetSafeNormal();

    // Latitudine [0.0 = equatore, 1.0 = poli]
    const float Latitude = FMath::Abs(Direction.Z);

    // Insolazione solare base: massima all'equatore, calo continuo verso i poli
    const float Insolation = 1.0f - FMath::Pow(Latitude, 1.35f);

    // Perturbazione climatica regionale deterministica
    const uint64 BaseSeed = static_cast<uint64>(Seed);
    uint64 TempSeed = BaseSeed + 0x517CC1B727220A95ULL;
    TempSeed = (TempSeed ^ (TempSeed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    TempSeed = (TempSeed ^ (TempSeed >> 27)) * 0x94D049BB133111EBULL;
    TempSeed ^= TempSeed >> 31;

    const float OffsetX = static_cast<float>(TempSeed & 0xFFFF) / 65535.0f * 200.0f - 100.0f;
    const float OffsetY = static_cast<float>((TempSeed >> 16) & 0xFFFF) / 65535.0f * 200.0f - 100.0f;
    const float OffsetZ = static_cast<float>((TempSeed >> 32) & 0xFFFF) / 65535.0f * 200.0f - 100.0f;

    const float ClimateNoise = FMath::PerlinNoise3D(
        Direction * 1.8f + FVector(OffsetX, OffsetY, OffsetZ)
    ) * 0.12f;

    // Gradiente termico verticale (adiabatic lapse rate): le vette si raffreddano
    const float AltitudeLapse = (NormalizedHeight > 0.0f)
        ? (NormalizedHeight * 1.6f)
        : 0.0f;

    return FMath::Clamp(
        Insolation + ClimateNoise - AltitudeLapse,
        0.0f,
        1.0f
    );
}

float UPlanetBiomeGenerator::CalculateHumidity(
    FVector Direction,
    float NormalizedHeight,
    int64 Seed
)
{
    Direction = Direction.GetSafeNormal();
    const float Latitude = FMath::Abs(Direction.Z);

    // Modello di circolazione a 3 celle (Hadley, Ferrel, Polare)
    float ZonalMoisture = 0.5f;
    if (Latitude < 0.18f)
    {
        // ITCZ equatoriale: convergenza e precipitazioni elevate
        const float T = Latitude / 0.18f;
        ZonalMoisture = FMath::Lerp(0.88f, 0.50f, T);
    }
    else if (Latitude < 0.42f)
    {
        // Cinture desertiche subtropicali anticicloniche
        const float T = (Latitude - 0.18f) / 0.24f;
        const float Dip = FMath::Sin(T * PI);
        ZonalMoisture = FMath::Lerp(0.50f, 0.14f, Dip);
    }
    else if (Latitude < 0.72f)
    {
        // Fronte polare / fascia temperata piovosa
        const float T = (Latitude - 0.42f) / 0.30f;
        const float Peak = FMath::Sin(T * PI);
        ZonalMoisture = FMath::Lerp(0.38f, 0.68f, Peak);
    }
    else
    {
        // Deserto polare freddo
        const float T = (Latitude - 0.72f) / 0.28f;
        ZonalMoisture = FMath::Lerp(0.38f, 0.18f, T);
    }

    // Perturbazione di umidità regionale deterministica
    const uint64 BaseSeed = static_cast<uint64>(Seed);
    uint64 HumidSeed = BaseSeed + 0x94D049BB133111EBULL;
    HumidSeed = (HumidSeed ^ (HumidSeed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    HumidSeed = (HumidSeed ^ (HumidSeed >> 27)) * 0x9E3779B97F4A7C15ULL;
    HumidSeed ^= HumidSeed >> 31;

    const float OffsetX = static_cast<float>(HumidSeed & 0xFFFF) / 65535.0f * 200.0f - 100.0f;
    const float OffsetY = static_cast<float>((HumidSeed >> 16) & 0xFFFF) / 65535.0f * 200.0f - 100.0f;
    const float OffsetZ = static_cast<float>((HumidSeed >> 32) & 0xFFFF) / 65535.0f * 200.0f - 100.0f;

    const float MoistureNoise = FMath::PerlinNoise3D(
        Direction * 2.2f + FVector(OffsetX, OffsetY, OffsetZ)
    ) * 0.22f;

    return FMath::Clamp(
        ZonalMoisture + MoistureNoise,
        0.0f,
        1.0f
    );
}

FPlanetBiomeData UPlanetBiomeGenerator::CalculateBiome(
    FVector Direction,
    float NormalizedHeight,
    FVector SurfaceNormal,
    int64 Seed
)
{
    FPlanetBiomeData BiomeData;

    Direction = Direction.GetSafeNormal();

    const float Latitude = FMath::Abs(Direction.Z);
    const float Slope = CalculateSlope(Direction, SurfaceNormal);
    const float Temperature = CalculateTemperature(Direction, NormalizedHeight, Seed);
    const float Humidity = CalculateHumidity(Direction, NormalizedHeight, Seed);

    BiomeData.Latitude = Latitude;
    BiomeData.Slope = Slope;
    BiomeData.Temperature = Temperature;
    BiomeData.Humidity = Humidity;

    // ========================================================================
    // VALUTAZIONE DEI PESI DEI 9 BIOMI FONDAMENTALI
    // 0 = Ocean, 1 = Beach, 2 = Plains, 3 = Grassland, 4 = Forest,
    // 5 = Desert, 6 = Tundra, 7 = Snow, 8 = Mountain
    // ========================================================================

    float Weights[9] = { 0.0f };

    // 1. OCEANO: fondali e aree sommerse
    const float OceanWeight =
        FMath::SmoothStep(0.012f, -0.015f, NormalizedHeight) * 3.0f;
    Weights[0] = OceanWeight;

    // 2. SPIAGGIA (BEACH): nastro litoraneo a debole pendenza vicino al livello del mare
    const float EmergeFactor =
        FMath::SmoothStep(-0.015f, 0.003f, NormalizedHeight) *
        FMath::SmoothStep(0.028f, 0.012f, NormalizedHeight);
    const float GentleSlope =
        1.0f - FMath::SmoothStep(0.12f, 0.35f, Slope);
    const float NonFreezing =
        FMath::SmoothStep(0.18f, 0.32f, Temperature);
    const float BeachWeight =
        EmergeFactor * GentleSlope * NonFreezing * 2.8f;
    Weights[1] = BeachWeight;

    // Presenza di terra emersa
    const float LandFactor =
        FMath::SmoothStep(-0.010f, 0.018f, NormalizedHeight);

    // 8. MONTAGNA (MOUNTAIN): pareti scoscese o quote elevate
    const float SlopeMountain =
        FMath::SmoothStep(0.25f, 0.50f, Slope);
    const float AltMountain =
        FMath::SmoothStep(0.12f, 0.28f, NormalizedHeight);
    const float MountainWeight =
        FMath::Max(SlopeMountain, AltMountain * 0.85f) * LandFactor * 2.2f;
    Weights[8] = MountainWeight;

    // 7. NEVE / GHIACCIAIO (SNOW): calotte polari o vette d'alta quota congelate
    const float PolarSnow =
        FMath::SmoothStep(0.24f, 0.10f, Temperature);
    const float PeakSnow =
        FMath::SmoothStep(0.15f, 0.26f, NormalizedHeight) *
        FMath::SmoothStep(0.42f, 0.25f, Temperature);
    const float SnowWeight =
        FMath::Max(PolarSnow, PeakSnow) * LandFactor * 2.5f;
    Weights[7] = SnowWeight;

    // Fattore residuo per le pianure/colline vegetate della terraferma
    const float SubMountain =
        FMath::Min(1.0f, FMath::Max(MountainWeight * 0.70f, SnowWeight * 0.80f));
    const float SubBeach =
        FMath::Min(1.0f, BeachWeight * 0.90f);
    const float VegetatedLand =
        LandFactor * (1.0f - SubMountain) * (1.0f - SubBeach);

    // 6. TUNDRA: fredda e subpolare
    const float TundraWeight =
        VegetatedLand *
        FMath::SmoothStep(0.08f, 0.20f, Temperature) *
        FMath::SmoothStep(0.40f, 0.28f, Temperature) *
        2.0f;
    Weights[6] = TundraWeight;

    // 5. DESERTO: caldo/arido con precipitazioni minime
    const float DesertWeight =
        VegetatedLand *
        FMath::SmoothStep(0.40f, 0.65f, Temperature) *
        FMath::SmoothStep(0.38f, 0.16f, Humidity) *
        2.2f;
    Weights[5] = DesertWeight;

    // 2. PIANURA (PLAINS): temperata o semi-arida
    const float TempPlains =
        FMath::SmoothStep(0.25f, 0.40f, Temperature) *
        FMath::SmoothStep(0.82f, 0.68f, Temperature);
    const float HumidPlains =
        FMath::SmoothStep(0.14f, 0.26f, Humidity) *
        FMath::SmoothStep(0.50f, 0.38f, Humidity);
    const float PlainsWeight =
        VegetatedLand * TempPlains * HumidPlains * 1.8f;
    Weights[2] = PlainsWeight;

    // 3. PRATERIA (GRASSLAND): clima temperato a umidità moderata
    const float TempGrass =
        FMath::SmoothStep(0.28f, 0.42f, Temperature) *
        FMath::SmoothStep(0.85f, 0.72f, Temperature);
    const float HumidGrass =
        FMath::SmoothStep(0.32f, 0.46f, Humidity) *
        FMath::SmoothStep(0.70f, 0.58f, Humidity);
    const float GrasslandWeight =
        VegetatedLand * TempGrass * HumidGrass * 2.0f;
    Weights[3] = GrasslandWeight;

    // 4. FORESTA (FOREST): da temperata umida a pluviale/equatoriale
    const float TempForest =
        FMath::SmoothStep(0.25f, 0.38f, Temperature);
    const float HumidForest =
        FMath::SmoothStep(0.46f, 0.68f, Humidity);
    const float ForestWeight =
        VegetatedLand * TempForest * HumidForest * 2.2f;
    Weights[4] = ForestWeight;

    // ========================================================================
    // SELEZIONE BIOMA PRIMARIO, SECONDARIO E SOFT BLEND
    // ========================================================================

    int32 Best1 = 0;
    int32 Best2 = 0;
    float MaxWeight1 = -1.0f;
    float MaxWeight2 = -1.0f;

    for (int32 i = 0; i < 9; ++i)
    {
        const float W = Weights[i];
        if (W > MaxWeight1)
        {
            MaxWeight2 = MaxWeight1;
            Best2 = Best1;
            MaxWeight1 = W;
            Best1 = i;
        }
        else if (W > MaxWeight2)
        {
            MaxWeight2 = W;
            Best2 = i;
        }
    }

    BiomeData.PrimaryBiome = static_cast<EPlanetBiome>(Best1);
    BiomeData.SecondaryBiome = (MaxWeight2 > 0.001f)
        ? static_cast<EPlanetBiome>(Best2)
        : BiomeData.PrimaryBiome;

    const float TotalWeight = MaxWeight1 + MaxWeight2;
    if (TotalWeight > 0.001f && MaxWeight2 > 0.0f)
    {
        // 0.0 = 100% primario, 0.5 = 50% primario e 50% secondario
        BiomeData.BiomeBlend = FMath::Clamp(MaxWeight2 / TotalWeight, 0.0f, 0.5f);
    }
    else
    {
        BiomeData.BiomeBlend = 0.0f;
    }

    return BiomeData;
}