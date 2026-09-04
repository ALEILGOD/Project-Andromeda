#include "Planet/PlanetBiomeGenerator.h"

namespace
{
    uint64 HashSeed64(uint64 Seed, uint64 Salt)
    {
        Seed += Salt;
        Seed = (Seed ^ (Seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        Seed = (Seed ^ (Seed >> 27)) * 0x94D049BB133111EBULL;
        return Seed ^ (Seed >> 31);
    }

    FVector MakeSeedOffset(uint64 Seed, uint64 Salt)
    {
        const uint64 H1 = HashSeed64(Seed, Salt);
        const uint64 H2 = HashSeed64(H1, 0x517CC1B727220A95ULL);
        const uint64 H3 = HashSeed64(H2, 0x9E3779B97F4A7C15ULL);

        const float X = (static_cast<float>(H1 & 0xFFFF) / 65535.0f) * 400.0f - 200.0f;
        const float Y = (static_cast<float>(H2 & 0xFFFF) / 65535.0f) * 400.0f - 200.0f;
        const float Z = (static_cast<float>(H3 & 0xFFFF) / 65535.0f) * 400.0f - 200.0f;

        return FVector(X, Y, Z);
    }

    float SampleMultiscaleField(const FVector& Direction, int64 Seed, uint64 Salt)
    {
        const FVector OffsetMacro = MakeSeedOffset(static_cast<uint64>(Seed), Salt);
        const FVector OffsetRegional = MakeSeedOffset(static_cast<uint64>(Seed), Salt + 0x31415926ULL);
        const FVector OffsetLocal = MakeSeedOffset(static_cast<uint64>(Seed), Salt + 0x27182818ULL);

        // Macro scale: 0.65 (grandi domini biotici e vaste regioni)
        const float Macro = FMath::PerlinNoise3D(Direction * 0.65f + OffsetMacro);
        // Regional scale: 2.20 (variazioni all'interno dei domini e transizioni naturali)
        const float Regional = FMath::PerlinNoise3D(Direction * 2.20f + OffsetRegional);
        // Local scale: 5.50 (micro-ondulazione organica dei confini, debole, senza speckles)
        const float Local = FMath::PerlinNoise3D(Direction * 5.50f + OffsetLocal);

        return Macro * 0.62f + Regional * 0.28f + Local * 0.10f;
    }
    // ========================================================================
    // SUITABILITY AMBIENTALE CORRETTIVA PER I 5 BIOMI REGIONALI
    //
    // Stessa formulazione della pipeline base: viene riusata dal percorso
    // CalculateBiomeWithProfile() con il clima effettivo (bias incluso).
    // ========================================================================

    float ComputeTundraSuitability(float Temperature, float Humidity)
    {
        const float Temp = FMath::Lerp(1.0f, 0.25f, FMath::SmoothStep(0.15f, 0.45f, Temperature));
        const float Hum  = FMath::Lerp(0.40f, 1.0f, 1.0f - FMath::SmoothStep(0.45f, 0.75f, Humidity));

        return Temp * 0.60f + Hum * 0.40f;
    }

    float ComputeDesertSuitability(float Temperature, float Humidity)
    {
        const float Temp = FMath::Lerp(0.35f, 1.0f, FMath::SmoothStep(0.18f, 0.45f, Temperature));
        const float Hum  = FMath::Lerp(1.0f, 0.25f, FMath::SmoothStep(0.20f, 0.55f, Humidity));

        return Temp * 0.45f + Hum * 0.55f;
    }

    float ComputePlainsSuitability(float Temperature, float Humidity)
    {
        const float Temp = FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.15f, 0.30f, Temperature) *
            (1.0f - FMath::SmoothStep(0.75f, 0.90f, Temperature))
        );

        const float Hum = FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.10f, 0.25f, Humidity) *
            (1.0f - FMath::SmoothStep(0.50f, 0.70f, Humidity))
        );

        return Temp * 0.50f + Hum * 0.50f;
    }

    float ComputeGrasslandSuitability(float Temperature, float Humidity)
    {
        const float Temp = FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.20f, 0.35f, Temperature) *
            (1.0f - FMath::SmoothStep(0.80f, 0.95f, Temperature))
        );

        const float Hum = FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.25f, 0.45f, Humidity) *
            (1.0f - FMath::SmoothStep(0.65f, 0.85f, Humidity))
        );

        return Temp * 0.50f + Hum * 0.50f;
    }

    float ComputeForestSuitability(float Temperature, float Humidity)
    {
        const float Temp = FMath::Lerp(
            0.30f,
            1.0f,
            FMath::SmoothStep(0.10f, 0.32f, Temperature)
        );

        const float Hum = FMath::Lerp(
            0.30f,
            1.0f,
            FMath::SmoothStep(0.25f, 0.55f, Humidity)
        );

        return Temp * 0.50f + Hum * 0.50f;
    }

    // ========================================================================
    // MODULAZIONE SOFT DEI PUNTEGGI TRAMITE BIOME BIASES
    //
    // Moltiplicatore: bias 0 -> nessun effetto (1.0); bias positivi favoriscono;
    // bias negativi penalizzano. Interviene SOLO sui punteggi già esistenti e
    // non sostituisce mai il Regional Biome Field.
    // ========================================================================

    float ApplyProfileBias(float BaseScore, float Bias)
    {
        const float Multiplier = FMath::Max(0.05f, 1.0f + Bias * 0.5f);
        return BaseScore * Multiplier;
    }

    // ========================================================================
    // SELEZIONE BIOMA PRIMARIO / SECONDARIO E BLEND FLUIDO
    //
    // Stessa logica della pipeline base, riusata dal percorso con profilo.
    // ========================================================================

    void ResolveBiomeWeights(
        const float (&Weights)[9],
        FPlanetBiomeData& BiomeData
    )
    {
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
        BiomeData.SecondaryBiome = (MaxWeight2 > 0.001f) ? static_cast<EPlanetBiome>(Best2) : BiomeData.PrimaryBiome;

        const float TotalWeight = MaxWeight1 + MaxWeight2;

        if (TotalWeight > 0.001f && MaxWeight2 > 0.0f)
        {
            BiomeData.BiomeBlend = FMath::Clamp(MaxWeight2 / TotalWeight, 0.0f, 0.5f);
        }
        else
        {
            BiomeData.BiomeBlend = 0.0f;
        }
    }
}

float UPlanetBiomeGenerator::CalculateSlope(
    FVector Direction,
    FVector SurfaceNormal
)
{
    Direction = Direction.GetSafeNormal();
    SurfaceNormal = SurfaceNormal.GetSafeNormal();

    const float Dot =
        FVector::DotProduct(
            Direction,
            SurfaceNormal
        );

    return 1.0f -
        FMath::Clamp(
            Dot,
            0.0f,
            1.0f
        );
}

float UPlanetBiomeGenerator::CalculateTemperature(
    FVector Direction,
    float NormalizedHeight,
    int64 Seed
)
{
    Direction =
        Direction.GetSafeNormal();

    // Latitudine [0.0 = equatore, 1.0 = poli]
    const float Latitude =
        FMath::Abs(Direction.Z);

    // Insolazione solare base:
    // massima all'equatore e minima ai poli.
    const float Insolation =
        1.0f -
        FMath::Pow(
            Latitude,
            1.35f
        );

    // Perturbazione climatica regionale deterministica.
    const uint64 BaseSeed =
        static_cast<uint64>(Seed);

    uint64 TempSeed =
        BaseSeed +
        0x517CC1B727220A95ULL;

    TempSeed =
        (TempSeed ^ (TempSeed >> 30)) *
        0xBF58476D1CE4E5B9ULL;

    TempSeed =
        (TempSeed ^ (TempSeed >> 27)) *
        0x94D049BB133111EBULL;

    TempSeed ^=
        TempSeed >> 31;

    const float OffsetX =
        static_cast<float>(
            TempSeed & 0xFFFF
            )
        / 65535.0f *
        200.0f -
        100.0f;

    const float OffsetY =
        static_cast<float>(
            (TempSeed >> 16) & 0xFFFF
            )
        / 65535.0f *
        200.0f -
        100.0f;

    const float OffsetZ =
        static_cast<float>(
            (TempSeed >> 32) & 0xFFFF
            )
        / 65535.0f *
        200.0f -
        100.0f;

    const float ClimateNoise =
        FMath::PerlinNoise3D(
            Direction * 1.8f +
            FVector(
                OffsetX,
                OffsetY,
                OffsetZ
            )
        ) *
        0.12f;

    // Raffreddamento con l'altitudine.
    const float AltitudeLapse =
        (NormalizedHeight > 0.0f)
        ? (
            NormalizedHeight *
            1.6f
            )
        : 0.0f;

    return FMath::Clamp(
        Insolation +
        ClimateNoise -
        AltitudeLapse,
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
    Direction =
        Direction.GetSafeNormal();

    const float Latitude =
        FMath::Abs(Direction.Z);

    // Modello di circolazione atmosferica
    // a tre celle.
    float ZonalMoisture =
        0.5f;

    if (Latitude < 0.18f)
    {
        // ITCZ equatoriale:
        // convergenza e precipitazioni elevate.
        const float T =
            Latitude /
            0.18f;

        ZonalMoisture =
            FMath::Lerp(
                0.88f,
                0.50f,
                T
            );
    }
    else if (Latitude < 0.42f)
    {
        // Cinture desertiche subtropicali.
        const float T =
            (Latitude - 0.18f) /
            0.24f;

        const float Dip =
            FMath::Sin(
                T * PI
            );

        ZonalMoisture =
            FMath::Lerp(
                0.50f,
                0.14f,
                Dip
            );
    }
    else if (Latitude < 0.72f)
    {
        // Fascia temperata / fronte polare.
        const float T =
            (Latitude - 0.42f) /
            0.30f;

        const float Peak =
            FMath::Sin(
                T * PI
            );

        ZonalMoisture =
            FMath::Lerp(
                0.38f,
                0.68f,
                Peak
            );
    }
    else
    {
        // Deserto polare freddo.
        const float T =
            (Latitude - 0.72f) /
            0.28f;

        ZonalMoisture =
            FMath::Lerp(
                0.38f,
                0.18f,
                T
            );
    }

    // Perturbazione regionale deterministica.
    const uint64 BaseSeed =
        static_cast<uint64>(Seed);

    uint64 HumidSeed =
        BaseSeed +
        0x94D049BB133111EBULL;

    HumidSeed =
        (HumidSeed ^ (HumidSeed >> 30)) *
        0xBF58476D1CE4E5B9ULL;

    HumidSeed =
        (HumidSeed ^ (HumidSeed >> 27)) *
        0x9E3779B97F4A7C15ULL;

    HumidSeed ^=
        HumidSeed >> 31;

    const float OffsetX =
        static_cast<float>(
            HumidSeed & 0xFFFF
            )
        / 65535.0f *
        200.0f -
        100.0f;

    const float OffsetY =
        static_cast<float>(
            (HumidSeed >> 16) & 0xFFFF
            )
        / 65535.0f *
        200.0f -
        100.0f;

    const float OffsetZ =
        static_cast<float>(
            (HumidSeed >> 32) & 0xFFFF
            )
        / 65535.0f *
        200.0f -
        100.0f;

    const float MoistureNoise =
        FMath::PerlinNoise3D(
            Direction * 2.2f +
            FVector(
                OffsetX,
                OffsetY,
                OffsetZ
            )
        ) *
        0.22f;

    return FMath::Clamp(
        ZonalMoisture +
        MoistureNoise,
        0.0f,
        1.0f
    );
}

FRegionalBiomeAffinities UPlanetBiomeGenerator::CalculateRegionalAffinities(
    FVector Direction,
    int64 Seed
)
{
    Direction = Direction.GetSafeNormal();

    // Tre canali multiscala per lo spazio latente biogeografico
    const float U = SampleMultiscaleField(Direction, Seed, 0xA1B2C3D4ULL);
    const float V = SampleMultiscaleField(Direction, Seed, 0xE5F60718ULL);
    const float W = SampleMultiscaleField(Direction, Seed, 0x98765432ULL);

    // Rotazione deterministica dello spazio latente basata sul seed per variare la disposizione planetaria
    const uint64 RotHash = HashSeed64(static_cast<uint64>(Seed), 0xC0FFEEULL);
    const float Angle = (static_cast<float>(RotHash & 0xFFFF) / 65535.0f) * 2.0f * PI;
    const float CosA = FMath::Cos(Angle);
    const float SinA = FMath::Sin(Angle);

    const float RotU = U * CosA - V * SinA;
    const float RotV = U * SinA + V * CosA;

    // 5 domini regionali distribuiti radialmente nello spazio latente (2D + perturbazione W)
    // k = 0: Forest    (theta = 0 deg)
    // k = 1: Grassland (theta = 72 deg)
    // k = 2: Plains    (theta = 144 deg)
    // k = 3: Desert    (theta = 216 deg)
    // k = 4: Tundra    (theta = 288 deg)
    constexpr float InvFive = 2.0f * PI / 5.0f;

    float Projections[5];
    for (int32 k = 0; k < 5; ++k)
    {
        const float ThetaK = static_cast<float>(k) * InvFive;
        const float DirX = FMath::Cos(ThetaK);
        const float DirY = FMath::Sin(ThetaK);

        // Proiezione del punto corrente verso il dominio k
        Projections[k] = RotU * DirX + RotV * DirY + W * 0.12f * FMath::Sin(2.0f * ThetaK);
    }

    // Softmax con sharpness calibrata per produrre province marcate ma transizioni fluide
    float Weights[5];
    float SumWeights = 0.0f;
    for (int32 k = 0; k < 5; ++k)
    {
        Weights[k] = FMath::Exp(FMath::Clamp(Projections[k] * 3.2f, -12.0f, 12.0f));
        SumWeights += Weights[k];
    }

    const float InvSum = (SumWeights > 0.0001f) ? (1.0f / SumWeights) : 0.2f;

    FRegionalBiomeAffinities Result;
    Result.Forest = Weights[0] * InvSum;
    Result.Grassland = Weights[1] * InvSum;
    Result.Plains = Weights[2] * InvSum;
    Result.Desert = Weights[3] * InvSum;
    Result.Tundra = Weights[4] * InvSum;

    return Result;
}

float UPlanetBiomeGenerator::CalculatePlanetWaterCoverage(
    int64 Seed
)
{
    uint64 Hash = static_cast<uint64>(Seed) + 0xA0761D6478BD642FULL;
    Hash = (Hash ^ (Hash >> 30)) * 0xBF58476D1CE4E5B9ULL;
    Hash = (Hash ^ (Hash >> 27)) * 0x94D049BB133111EBULL;
    Hash ^= Hash >> 31;
    const float Unit = static_cast<float>(Hash & 0xFFFF) / 65535.0f;
    return 0.30f + Unit * 0.40f; // Quantità d'acqua deterministica tra 30% e 70%
}

float UPlanetBiomeGenerator::CalculateSeaLevelFromWaterCoverage(
    float WaterCoverage
)
{
    const float ClampedCoverage = FMath::Clamp(WaterCoverage, 0.30f, 0.70f);
    // Interpolazione continua tra i quantili della distribuzione altimetrica del terreno
    // 0.30 -> ~0.065
    // 0.50 -> ~0.151 (quota mediana)
    // 0.70 -> ~0.315
    if (ClampedCoverage <= 0.50f)
    {
        const float Alpha = (ClampedCoverage - 0.30f) / 0.20f;
        return FMath::Lerp(0.065f, 0.151f, Alpha);
    }
    else
    {
        const float Alpha = (ClampedCoverage - 0.50f) / 0.20f;
        return FMath::Lerp(0.151f, 0.315f, Alpha);
    }
}

FPlanetBiomeData UPlanetBiomeGenerator::CalculateBiome(
    FVector Direction,
    float NormalizedHeight,
    FVector SurfaceNormal,
    int64 Seed,
    float SeaLevel
)
{
    FPlanetBiomeData BiomeData;

    Direction =
        Direction.GetSafeNormal();

    const float Latitude =
        FMath::Abs(Direction.Z);

    const float Slope =
        CalculateSlope(
            Direction,
            SurfaceNormal
        );

    const float Temperature =
        CalculateTemperature(
            Direction,
            NormalizedHeight,
            Seed
        );

    const float Humidity =
        CalculateHumidity(
            Direction,
            NormalizedHeight,
            Seed
        );

    BiomeData.Latitude =
        Latitude;

    BiomeData.Slope =
        Slope;

    BiomeData.Temperature =
        Temperature;

    BiomeData.Humidity =
        Humidity;

    // ========================================================================
    // REGIONAL BIOME FIELD
    //
    // Campo continuo multiscala deterministico su sfera per macro domini,
    // transizioni regionali e micro-ondulazione organica dei confini.
    // ========================================================================

    const FRegionalBiomeAffinities Affinities =
        CalculateRegionalAffinities(
            Direction,
            Seed
        );

    BiomeData.RegionalAffinities =
        Affinities;

    // ========================================================================
    // SEPARAZIONE OCEANO / TERRA
    //
    // Il livello del mare è una frontiera netta.
    // Sotto SeaLevel è sempre Ocean e non compete con i biomi terrestri.
    // ========================================================================

    const bool IsOcean =
        NormalizedHeight <= SeaLevel;

    if (IsOcean)
    {
        BiomeData.PrimaryBiome =
            EPlanetBiome::Ocean;

        BiomeData.SecondaryBiome =
            EPlanetBiome::Ocean;

        BiomeData.BiomeBlend =
            0.0f;

        return BiomeData;
    }

    // ========================================================================
    // TERRA EMERSA
    // ========================================================================

    const float ElevationAboveSea =
        NormalizedHeight - SeaLevel;

    const float LandFactor =
        FMath::SmoothStep(
            0.0f,
            0.018f,
            ElevationAboveSea
        );

    // ========================================================================
    // BEACH
    //
    // Fascia costiera stretta e morfologicamente differenziata:
    // le coste piatte favoriscono la spiaggia, mentre scogliere ripide e
    // coste rocciose ne riducono la probabilità.
    // ========================================================================

    const float BeachHeightFactor =
        (1.0f - FMath::SmoothStep(0.008f, 0.022f, ElevationAboveSea));

    const float GentleSlope =
        1.0f -
        FMath::SmoothStep(
            0.10f,
            0.28f,
            Slope
        );

    const float NonFreezing =
        FMath::SmoothStep(
            0.15f,
            0.28f,
            Temperature
        );

    const float BeachWeight =
        BeachHeightFactor *
        GentleSlope *
        NonFreezing *
        0.92f;

    // ========================================================================
    // AREA INTERNA DELLA TERRA
    //
    // Oltre la fascia costiera, la terra emersa è pienamente disponibile
    // per i biomi regionali e le formazioni montuose.
    // ========================================================================

    const float BeachSuppression =
        FMath::SmoothStep(
            0.002f,
            0.022f,
            ElevationAboveSea
        );

    const float InternalLand =
        BeachSuppression;

    // ========================================================================
    // MONTAGNA (TERRAIN-DRIVEN)
    //
    // Guidata primariamente da pendenza ed elevazione geomorfologica.
    // Il campo regionale ha un'influenza debole.
    // Alle temperature di gelo cede la priorità alla neve perenne.
    // ========================================================================

    const float SlopeMountain =
        FMath::SmoothStep(
            0.22f,
            0.45f,
            Slope
        );

    const float AltMountain =
        FMath::SmoothStep(
            0.38f,
            0.54f,
            NormalizedHeight
        );

    const float SnowCoverDampening =
        FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(
                0.12f,
                0.28f,
                Temperature
            )
        );

    const float MountainAltFactor =
        AltMountain *
        FMath::Max(
            SlopeMountain,
            SnowCoverDampening
        );

    const float MountainWeight =
        FMath::Max(
            SlopeMountain,
            MountainAltFactor
        ) *
        LandFactor *
        0.88f;

    // ========================================================================
    // NEVE
    //
    // Favorita da alta quota combinata a temperature rigide o da calotte polari.
    // A bassa quota il freddo produce Tundra o Boreal Forest anziché coprire
    // il terreno di neve.
    // ========================================================================

    const float PolarSnow =
        (1.0f - FMath::SmoothStep(0.06f, 0.18f, Temperature)) *
        FMath::SmoothStep(
            0.78f,
            0.92f,
            Latitude
        );

    const float PeakSnow =
        FMath::SmoothStep(
            0.35f,
            0.50f,
            NormalizedHeight
        ) *
        (1.0f - FMath::SmoothStep(0.12f, 0.26f, Temperature));

    const float SnowWeight =
        FMath::Max(
            PolarSnow,
            PeakSnow
        ) *
        LandFactor *
        0.90f;

    // ========================================================================
    // SUITABILITY AMBIENTALE CORRETTIVA PER I 5 BIOMI REGIONALI
    //
    // Influenza secondaria: corregge/modula l'affinità regionale senza
    // azzerarla bruscamente in presenza di condizioni non ottimali.
    // ========================================================================

    // TUNDRA: ottimale per temperature fredde/fresche, tollera climi temperati freddi
    const float TundraTemp =
        FMath::Lerp(
            1.0f,
            0.25f,
            FMath::SmoothStep(0.15f, 0.45f, Temperature)
        );

    const float TundraHumid =
        FMath::Lerp(
            0.40f,
            1.0f,
            1.0f - FMath::SmoothStep(0.45f, 0.75f, Humidity)
        );

    const float TundraSuitability =
        TundraTemp * 0.60f +
        TundraHumid * 0.40f;

    // DESERTO: ottimale per aridità e caldo, tollera deserti freddi con affinità regionale
    const float DesertTemp =
        FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.18f, 0.45f, Temperature)
        );

    const float DesertHumid =
        FMath::Lerp(
            1.0f,
            0.25f,
            FMath::SmoothStep(0.20f, 0.55f, Humidity)
        );

    const float DesertSuitability =
        DesertTemp * 0.45f +
        DesertHumid * 0.55f;

    // PIANURA: climi temperati/semi-aridi, ampia compatibilità
    const float PlainsTemp =
        FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.15f, 0.30f, Temperature) *
            (1.0f - FMath::SmoothStep(0.75f, 0.90f, Temperature))
        );

    const float PlainsHumid =
        FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.10f, 0.25f, Humidity) *
            (1.0f - FMath::SmoothStep(0.50f, 0.70f, Humidity))
        );

    const float PlainsSuitability =
        PlainsTemp * 0.50f +
        PlainsHumid * 0.50f;

    // PRATERIA (GRASSLAND): clima temperato-caldo, umidità moderata
    const float GrassTemp =
        FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.20f, 0.35f, Temperature) *
            (1.0f - FMath::SmoothStep(0.80f, 0.95f, Temperature))
        );

    const float GrassHumid =
        FMath::Lerp(
            0.35f,
            1.0f,
            FMath::SmoothStep(0.25f, 0.45f, Humidity) *
            (1.0f - FMath::SmoothStep(0.65f, 0.85f, Humidity))
        );

    const float GrassSuitability =
        GrassTemp * 0.50f +
        GrassHumid * 0.50f;

    // FORESTA: vegetazione densa, tollera climi boreali/taiga a basse temperature
    const float ForestTemp =
        FMath::Lerp(
            0.30f,
            1.0f,
            FMath::SmoothStep(0.10f, 0.32f, Temperature)
        );

    const float ForestHumid =
        FMath::Lerp(
            0.30f,
            1.0f,
            FMath::SmoothStep(0.25f, 0.55f, Humidity)
        );

    const float ForestSuitability =
        ForestTemp * 0.50f +
        ForestHumid * 0.50f;

    // ========================================================================
    // COMBINAZIONE SCORE DEI BIOMI REGIONAL-DRIVEN
    //
    // Ponderazione:
    // - Regional Preference: influenza dominante (65%)
    // - Environmental Suitability: influenza secondaria/correttiva (35%)
    // ========================================================================

    constexpr float WReg = 0.65f;
    constexpr float WEnv = 0.35f;

    const float TundraScore =
        (WReg * Affinities.Tundra + WEnv * TundraSuitability) * 0.90f;

    const float DesertScore =
        (WReg * Affinities.Desert + WEnv * DesertSuitability) * 0.90f;

    const float PlainsScore =
        (WReg * Affinities.Plains + WEnv * PlainsSuitability) * 0.90f;

    const float GrasslandScore =
        (WReg * Affinities.Grassland + WEnv * GrassSuitability) * 0.90f;

    const float ForestScore =
        (WReg * Affinities.Forest + WEnv * ForestSuitability) * 0.90f;

    // ========================================================================
    // ASSEGNAZIONE PESI
    // ========================================================================

    float Weights[9] = { 0.0f };

    Weights[0] = 0.0f; // Ocean è gestito sopra con frontiera netta
    Weights[1] = BeachWeight;
    Weights[2] = InternalLand * PlainsScore;
    Weights[3] = InternalLand * GrasslandScore;
    Weights[4] = InternalLand * ForestScore;
    Weights[5] = InternalLand * DesertScore;
    Weights[6] = InternalLand * TundraScore;
    Weights[7] = SnowWeight;
    Weights[8] = MountainWeight;

    // ========================================================================
    // SELEZIONE BIOMA PRIMARIO E SECONDARIO
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
    BiomeData.SecondaryBiome = (MaxWeight2 > 0.001f) ? static_cast<EPlanetBiome>(Best2) : BiomeData.PrimaryBiome;

    // ========================================================================
    // BLEND FLUIDO
    // ========================================================================

    const float TotalWeight = MaxWeight1 + MaxWeight2;

    if (TotalWeight > 0.001f && MaxWeight2 > 0.0f)
    {
        BiomeData.BiomeBlend = FMath::Clamp(MaxWeight2 / TotalWeight, 0.0f, 0.5f);
    }
    else
    {
        BiomeData.BiomeBlend = 0.0f;
    }

    return BiomeData;
}

FPlanetBiomeData UPlanetBiomeGenerator::CalculateBiomeWithProfile(
    FVector Direction,
    float NormalizedHeight,
    FVector SurfaceNormal,
    int64 Seed,
    const FPlanetProfile& Profile
)
{
    FPlanetBiomeData BiomeData;

    // ========================================================================
    // LIVELLO DEL MARE
    //
    // Unica fonte di verita: WaterCoverage -> SeaLevel.
    // L'oceano resta una classificazione hard sotto questo livello.
    // ========================================================================

    const float SeaLevel = CalculateSeaLevelFromWaterCoverage(Profile.WaterCoverage);

    Direction = Direction.GetSafeNormal();

    const float Latitude = FMath::Abs(Direction.Z);

    const float Slope = CalculateSlope(Direction, SurfaceNormal);

    // ========================================================================
    // TEMPERATURA E UMIDITA LOCALI + MODULAZIONE SOFT DEL PROFILO
    //
    // La temperatura/umidita locale del sistema esistente viene calcolata
    // integralmente e poi spostata leggermente dai bias del profilo.
    // Il risultato viene clampato nell'intervallo valido [0.0, 1.0].
    // Il profilo NON sostituisce il clima locale: lo modula soltanto.
    // ========================================================================

    const float BaseTemperature = CalculateTemperature(Direction, NormalizedHeight, Seed);
    const float Temperature = FMath::Clamp(BaseTemperature + Profile.TemperatureBias, 0.0f, 1.0f);

    const float BaseHumidity = CalculateHumidity(Direction, NormalizedHeight, Seed);
    const float Humidity = FMath::Clamp(BaseHumidity + Profile.HumidityBias, 0.0f, 1.0f);

    BiomeData.Latitude = Latitude;
    BiomeData.Slope = Slope;
    BiomeData.Temperature = Temperature;
    BiomeData.Humidity = Humidity;

    // ========================================================================
    // REGIONAL BIOME FIELD
    //
    // Rimane il principale driver spaziale della distribuzione dei biomi.
    // I bias del profilo agiscono solo come modulazione addizionale dei
    // punteggi finali, senza sostituire le province regionali.
    // ========================================================================

    const FRegionalBiomeAffinities Affinities = CalculateRegionalAffinities(Direction, Seed);

    BiomeData.RegionalAffinities = Affinities;

    // ========================================================================
    // SEPARAZIONE OCEANO / TERRA (frontiera netta, invariata)
    // ========================================================================

    if (NormalizedHeight <= SeaLevel)
    {
        BiomeData.PrimaryBiome = EPlanetBiome::Ocean;
        BiomeData.SecondaryBiome = EPlanetBiome::Ocean;
        BiomeData.BiomeBlend = 0.0f;

        return BiomeData;
    }

    const float ElevationAboveSea = NormalizedHeight - SeaLevel;

    const float LandFactor = FMath::SmoothStep(0.0f, 0.018f, ElevationAboveSea);

    // ========================================================================
    // BEACH (stessa formulazione della pipeline base)
    // ========================================================================

    const float BeachHeightFactor = (1.0f - FMath::SmoothStep(0.008f, 0.022f, ElevationAboveSea));

    const float GentleSlope = 1.0f - FMath::SmoothStep(0.10f, 0.28f, Slope);

    const float NonFreezing = FMath::SmoothStep(0.15f, 0.28f, Temperature);

    const float BeachWeight = BeachHeightFactor * GentleSlope * NonFreezing * 0.92f;

    const float BeachSuppression = FMath::SmoothStep(0.002f, 0.022f, ElevationAboveSea);

    const float InternalLand = BeachSuppression;

    // ========================================================================
    // MONTAGNA (TERRAIN-DRIVEN)
    //
    // Slope, altitudine e land factor restano alla base della determinazione.
    // MountainBias e solo una modulazione soft del punteggio: l'archetipo
    // non trasforma direttamente il pianeta in Mountain.
    // ========================================================================

    const float SlopeMountain = FMath::SmoothStep(0.22f, 0.45f, Slope);

    const float AltMountain = FMath::SmoothStep(0.38f, 0.54f, NormalizedHeight);

    const float SnowCoverDampening = FMath::Lerp(
        0.35f,
        1.0f,
        FMath::SmoothStep(0.12f, 0.28f, Temperature)
    );

    const float MountainAltFactor = AltMountain * FMath::Max(SlopeMountain, SnowCoverDampening);

    const float MountainBaseWeight = FMath::Max(SlopeMountain, MountainAltFactor) * LandFactor * 0.88f;

    const float MountainWeight = ApplyProfileBias(MountainBaseWeight, Profile.BiomeBiases.MountainBias);

    // ========================================================================
    // NEVE
    //
    // Influenze locali: temperatura, latitudine e altitudine continuano a
    // contribuire insieme (nessuna di esse e l'unico driver, in particolare
    // non esiste una regola "alta quota = neve").
    //
    // Modulazione planetaria del profilo:
    // - bSnowAllowed == false: punteggio fortemente soppresso (virtualmente
    //   disabilitato, ma determinismo preservato).
    // - bSnowAllowed == true : SnowPotential modula l'intensita globale;
    //   SnowBias regola ulteriormente la probabilita del fenomeno.
    // ========================================================================

    const float PolarSnow =
        (1.0f - FMath::SmoothStep(0.06f, 0.18f, Temperature)) *
        FMath::SmoothStep(0.78f, 0.92f, Latitude);

    const float PeakSnow =
        FMath::SmoothStep(0.35f, 0.50f, NormalizedHeight) *
        (1.0f - FMath::SmoothStep(0.12f, 0.26f, Temperature));

    const float SnowLocalWeight = FMath::Max(PolarSnow, PeakSnow) * LandFactor * 0.90f;

    float SnowWeight = 0.0f;
    if (Profile.bSnowAllowed)
    {
        float SnowModulation = FMath::Clamp(Profile.SnowPotential, 0.0f, 1.0f);
        SnowModulation *= (1.0f + Profile.BiomeBiases.SnowBias * 0.5f);
        SnowModulation = FMath::Clamp(SnowModulation, 0.0f, 1.5f);

        SnowWeight = SnowLocalWeight * SnowModulation;
    }
    else
    {
        // Neve praticamente disabilitata ma percorso deterministico invariato.
        SnowWeight = SnowLocalWeight * 0.03f;
    }

    // ========================================================================
    // SUITABILITY AMBIENTALE CORRETTIVA PER I 5 BIOMI REGIONALI
    //
    // Calcolata sul clima effettivo del pianeta (temperature/humidity locali
    // già modulate dai bias del profilo).
    // ========================================================================

    const float TundraSuitability = ComputeTundraSuitability(Temperature, Humidity);
    const float DesertSuitability = ComputeDesertSuitability(Temperature, Humidity);
    const float PlainsSuitability = ComputePlainsSuitability(Temperature, Humidity);
    const float GrassSuitability = ComputeGrasslandSuitability(Temperature, Humidity);
    const float ForestSuitability = ComputeForestSuitability(Temperature, Humidity);

    // ========================================================================
    // COMBINAZIONE SCORE DEI BIOMI REGIONAL-DRIVEN
    //
    // Ponderazione invariata:
    // - Regional Preference: influenza dominante (65%)
    // - Environmental Suitability: influenza secondaria/correttiva (35%)
    //
    // I BiomeBiases del profilo intervengono come modulazione soft finale
    // dei punteggi già esistenti (moltiplicatore, non sostituzione).
    // ========================================================================

    constexpr float WReg = 0.65f;
    constexpr float WEnv = 0.35f;

    const float TundraScore = ApplyProfileBias(
        (WReg * Affinities.Tundra + WEnv * TundraSuitability) * 0.90f,
        Profile.BiomeBiases.TundraBias
    );

    const float DesertScore = ApplyProfileBias(
        (WReg * Affinities.Desert + WEnv * DesertSuitability) * 0.90f,
        Profile.BiomeBiases.DesertBias
    );

    const float PlainsScore = ApplyProfileBias(
        (WReg * Affinities.Plains + WEnv * PlainsSuitability) * 0.90f,
        Profile.BiomeBiases.PlainsBias
    );

    const float GrasslandScore = ApplyProfileBias(
        (WReg * Affinities.Grassland + WEnv * GrassSuitability) * 0.90f,
        Profile.BiomeBiases.GrasslandBias
    );

    const float ForestScore = ApplyProfileBias(
        (WReg * Affinities.Forest + WEnv * ForestSuitability) * 0.90f,
        Profile.BiomeBiases.ForestBias
    );

    // ========================================================================
    // ASSEGNAZIONE PESI
    // ========================================================================

    float Weights[9] = { 0.0f };

    Weights[0] = 0.0f; // Ocean e gestito sopra con frontiera netta
    Weights[1] = BeachWeight;
    Weights[2] = InternalLand * PlainsScore;
    Weights[3] = InternalLand * GrasslandScore;
    Weights[4] = InternalLand * ForestScore;
    Weights[5] = InternalLand * DesertScore;
    Weights[6] = InternalLand * TundraScore;
    Weights[7] = SnowWeight;
    Weights[8] = MountainWeight;

    // ========================================================================
    // SELEZIONE BIOMA PRIMARIO E SECONDARIO + BLEND FLUIDO
    // ========================================================================

    ResolveBiomeWeights(Weights, BiomeData);

    return BiomeData;
}