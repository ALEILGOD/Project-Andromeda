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

    // ========================================================================
    // PBS v3 - FASE 1.5: TRANSIZIONI DIRETTE TRA PROVINCE (NO SANDWICH)
    //
    // Alla combinazione finale (Regional Affinity + Climate Suitability) un
    // terzo bioma con alta suitability climatica puo' inserirsi come fascia
    // autonoma tra due province adiacenti (es. Forest -> Grassland -> Desert).
    //
    // Questa sezione espone i PESI DELLE PROVINCE alla risoluzione finale:
    //   Spherical Provinces -> Province Weights -> Dominant Province Pair
    //   -> TransitionFactor -> restrizione morbida dei candidati
    //   -> Final Core Biome
    //
    // In zona di transizione (le due province localmente piu' forti hanno
    // pesi comparabili) i biomi non CANDIDATI delle province dominanti
    // vengono soppressi in modo continuo; il clima continua a modulare la
    // forza relativa dei due candidati, ma non puo' creare fasce di terzi
    // biomi. In piena provincia l'allowance e' 1.0: selezione normale.
    //
    // SINCRONIZZAZIONE OBBLIGATORIA: le costanti e la geometria di
    // SampleProvinceField() replicano ESATTAMENTE lo scheletro province
    // interno a CalculateRegionalAffinities() (stesso hash, stessi centri
    // Fibonacci + jitter + rotazione, stesse famiglie, stesso sigma,
    // stessa FamilySupport). NON modificare uno senza l'altro.
    // CalculateRegionalAffinities() NON viene toccata (requisito Fase 1.5).
    // ========================================================================

    constexpr int32 TransitionProvinceCount = 8;
    constexpr float TransitionSigma = 0.35f;
    constexpr float TransitionInvTwoSigmaSq = 0.5f / (TransitionSigma * TransitionSigma);
    constexpr float TransitionJitterScale = 0.35f;

    // Finestra di transizione sul rapporto di peso W2/W1 tra le due province
    // dominanti: sotto lo start una provincia domina nettamente (selezione
    // normale), sopra l'end la posizione e' pienamente in transizione.
    constexpr float TransitionDominanceStart = 0.40f;
    constexpr float TransitionDominanceEnd = 0.75f;

    // Campione del campo province in una direzione sulla sfera.
    struct FProvinceFieldSample
    {
        float Weights[TransitionProvinceCount]; // somma ~1
        FVector Centers[TransitionProvinceCount]; // centri unitari sulla sfera
    };

    // PBS v4 - SORGETE UNICA della geometria province (pesi + centri).
    // Riusata da nomination (ComputeProvinceExpressions) e affinities
    // (CalculateRegionalAffinities): campo coerente tra i due percorsi.
    void SampleProvinceField(
        const FVector& Direction,
        int64 Seed,
        FProvinceFieldSample& Out
    )
    {
        const FVector Dir = Direction.GetSafeNormal();

        const uint64 ProvinceSeedHash =
            HashSeed64(static_cast<uint64>(Seed), 0x50726F76696E6365ULL);

        // ====================================================================
        // PBS v4.1 - LOW-FREQUENCY DOMAIN WARP (confini organici)
        //
        // Deforma SOLO lo spazio di valutazione della vicinanza alle
        // province: i centri e le famiglie NON cambiano. Due ottave a bassa
        // frequenza: macro (grandi baie e rientranze) + regionale (curvature
        // leggere). Perlin 3D e' continuo sulla sfera: nessun seam ai poli
        // o al punto antipodale. L'ampiezza e' piccola rispetto alla
        // spaziatura delle province (~1 rad): i confini diventano sinuosi
        // senza frammentare le province ne' creare blob.
        // ====================================================================
        auto WarpOffsetComponent = [ProvinceSeedHash](uint64 Salt)
        {
            const uint64 WarpHash = HashSeed64(ProvinceSeedHash, Salt);
            return (static_cast<float>(WarpHash & 0xFFFF) / 65535.0f) * 200.0f - 100.0f;
        };

        constexpr float WarpAmplitudeMacro = 0.12f;
        constexpr float WarpFrequencyMacro = 1.1f;
        constexpr float WarpAmplitudeRegional = 0.05f;
        constexpr float WarpFrequencyRegional = 2.4f;

        const FVector MacroOffsetA(
            WarpOffsetComponent(0x5A11ULL),
            WarpOffsetComponent(0x5A12ULL),
            WarpOffsetComponent(0x5A13ULL));
        const FVector MacroOffsetB(
            WarpOffsetComponent(0x5A14ULL),
            WarpOffsetComponent(0x5A15ULL),
            WarpOffsetComponent(0x5A16ULL));
        const FVector MacroOffsetC(
            WarpOffsetComponent(0x5A17ULL),
            WarpOffsetComponent(0x5A18ULL),
            WarpOffsetComponent(0x5A19ULL));

        const FVector MacroWarp(
            FMath::PerlinNoise3D(Dir * WarpFrequencyMacro + MacroOffsetA),
            FMath::PerlinNoise3D(Dir * WarpFrequencyMacro + MacroOffsetB),
            FMath::PerlinNoise3D(Dir * WarpFrequencyMacro + MacroOffsetC));

        const FVector RegionalOffsetA(
            WarpOffsetComponent(0x5B21ULL),
            WarpOffsetComponent(0x5B22ULL),
            WarpOffsetComponent(0x5B23ULL));
        const FVector RegionalOffsetB(
            WarpOffsetComponent(0x5B24ULL),
            WarpOffsetComponent(0x5B25ULL),
            WarpOffsetComponent(0x5B26ULL));
        const FVector RegionalOffsetC(
            WarpOffsetComponent(0x5B27ULL),
            WarpOffsetComponent(0x5B28ULL),
            WarpOffsetComponent(0x5B29ULL));

        const FVector RegionalWarp(
            FMath::PerlinNoise3D(Dir * WarpFrequencyRegional + RegionalOffsetA),
            FMath::PerlinNoise3D(Dir * WarpFrequencyRegional + RegionalOffsetB),
            FMath::PerlinNoise3D(Dir * WarpFrequencyRegional + RegionalOffsetC));

        // La deflessione totale (~0.17 rad massimo, tipicamente ~0.08) e'
        // molto più piccola di 1: la rinormalizzazione e' stabile ovunque.
        const FVector WarpVector =
            MacroWarp * WarpAmplitudeMacro + RegionalWarp * WarpAmplitudeRegional;
        const FVector WarpedDir = (Dir + WarpVector).GetSafeNormal();

        const uint64 RotHash = HashSeed64(ProvinceSeedHash, 0x5F3759DFULL);
        const float Yaw = (static_cast<float>(RotHash & 0xFFFF) / 65535.0f) * 2.0f * PI;
        const float Pitch = (static_cast<float>((RotHash >> 16) & 0xFFFF) / 65535.0f) * 2.0f * PI;
        const float Roll = (static_cast<float>((RotHash >> 32) & 0xFFFF) / 65535.0f) * 2.0f * PI;

        const float Cy = FMath::Cos(Yaw), Sy = FMath::Sin(Yaw);
        const float Cp = FMath::Cos(Pitch), Sp = FMath::Sin(Pitch);
        const float Cr = FMath::Cos(Roll), Sr = FMath::Sin(Roll);

        const float R00 = Cy * Cp;
        const float R01 = Cy * Sp * Sr - Sy * Cr;
        const float R02 = Cy * Sp * Cr + Sy * Sr;
        const float R10 = Sy * Cp;
        const float R11 = Sy * Sp * Sr + Cy * Cr;
        const float R12 = Sy * Sp * Cr - Cy * Sr;
        const float R20 = -Sp;
        const float R21 = Cp * Sr;
        const float R22 = Cp * Cr;

        const float GoldenAngle = PI * (3.0f - FMath::Sqrt(5.0f));

        float Influences[TransitionProvinceCount];
        float TotalInfluence = 0.0f;

        for (int32 i = 0; i < TransitionProvinceCount; ++i)
        {
            // Centro base: Fibonacci sphere (identico a CalculateRegionalAffinities).
            const float Y = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(TransitionProvinceCount);
            const float R = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Y * Y));
            const float Theta = GoldenAngle * static_cast<float>(i);
            FVector Center(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Y);

            // Perturbazione deterministica per-centro (identica).
            const uint64 CenterHash = HashSeed64(
                ProvinceSeedHash,
                0xD1B54A32D192ED03ULL + static_cast<uint64>(i) * 0x9E3779B97F4A7C15ULL);

            const float JX = ((static_cast<float>(CenterHash & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * TransitionJitterScale;
            const float JY = ((static_cast<float>((CenterHash >> 16) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * TransitionJitterScale;
            const float JZ = ((static_cast<float>((CenterHash >> 32) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * TransitionJitterScale;

            Center.X += JX;
            Center.Y += JY;
            Center.Z += JZ;

            // Rotazione globale per-pianeta (identica).
            const FVector Rotated(
                R00 * Center.X + R01 * Center.Y + R02 * Center.Z,
                R10 * Center.X + R11 * Center.Y + R12 * Center.Z,
                R20 * Center.X + R21 * Center.Y + R22 * Center.Z
            );

            Center = Rotated.GetSafeNormal();

            // Il centro resta disponibile per il Provincial Climate (L4).
            Out.Centers[i] = Center;

            // Influenza gaussiana sulla sfera (senza acos): valutata nella
            // DIREZIONE DEFORMATA (domain warp) -> confini organici. I centri
            // e le famiglie non partecipano al warp.
            const float Dot = FVector::DotProduct(WarpedDir, Center);
            const float ChordSq = FMath::Max(0.0f, 2.0f - 2.0f * Dot);
            Influences[i] = FMath::Exp(-ChordSq * TransitionInvTwoSigmaSq);
            TotalInfluence += Influences[i];
        }

        if (TotalInfluence > 0.0001f)
        {
            const float InvTotal = 1.0f / TotalInfluence;
            for (int32 i = 0; i < TransitionProvinceCount; ++i)
            {
                Out.Weights[i] = Influences[i] * InvTotal;
            }
        }
        else
        {
            const float Uniform = 1.0f / static_cast<float>(TransitionProvinceCount);
            for (int32 i = 0; i < TransitionProvinceCount; ++i)
            {
                Out.Weights[i] = Uniform;
            }
        }
    }

    // ========================================================================
    // PBS v4 - L0: GLOBAL PLANETARY CLIMATE
    //
    // L'orbita influenza direttamente il clima planetario, indipendentemente
    // dall'archetipo estratto dalla roulette (che resta classificazione).
    // Nessun fallback PlanetID: orbita assente/non valida -> regime neutro.
    // ========================================================================
    void ComputePlanetaryClimate(
        int64 Seed,
        const FPlanetProfile& Profile,
        float& OutThermalIndex,
        float& OutAridityIndex,
        float& OutGlobalPlanetTemperature,
        float& OutGlobalPlanetHumidity
    )
    {
        // Normalizzazione orbitale identica a PlanetProfile.cpp.
        const float EffectiveOrbit = (Profile.OrbitDistance > 0.0f) ? Profile.OrbitDistance : 6000000.0f;
        const float NormalizedOrbit = FMath::Clamp((EffectiveOrbit - 3500000.0f) / 12000000.0f, 0.0f, 1.0f);
        const float ThermalIndex = 1.0f - NormalizedOrbit;

        // Variazione planetaria deterministica dal Seed (piccola: il regime
        // climatico resta dominante, ma "vicino = sempre Dry" e' vietato).
        const uint64 ClimateJitterHash = HashSeed64(static_cast<uint64>(Seed), 0x504C414E4554ULL);
        const float ThermalJitter = (static_cast<float>(ClimateJitterHash & 0xFFFF) / 65535.0f - 0.5f) * 0.10f;

        const float EffectiveThermalIndex = FMath::Clamp(ThermalIndex + ThermalJitter, 0.0f, 1.0f);

        OutThermalIndex = EffectiveThermalIndex;

        OutAridityIndex = FMath::Clamp(
            0.5f * EffectiveThermalIndex + 0.5f * (0.5f - Profile.HumidityBias),
            0.0f,
            1.0f
        );

        OutGlobalPlanetTemperature = FMath::Clamp(
            0.25f + 0.50f * EffectiveThermalIndex + 0.50f * Profile.TemperatureBias,
            0.0f,
            1.0f
        );

        OutGlobalPlanetHumidity = FMath::Clamp(
            0.50f - 0.30f * EffectiveThermalIndex + 0.50f * Profile.HumidityBias,
            0.0f,
            1.0f
        );
    }

    // ========================================================================
    // PBS v4 - L3: CLIMATE-WEIGHTED PROVINCE FAMILY ASSIGNMENT
    //
    // Sostituisce l'assegnazione uniforme (& 0x3): la famiglia di ogni
    // provincia e' scelta via CDF deterministica su u_i, con probabilita'
    // pesate dal regime climatico planetario (ThermalIndex) e modulate
    // dall'aridita' globale (AridityIndex). La maggioranza delle province
    // riflette il regime climatico; la varieta' resta seedata.
    // ========================================================================

    uint8 AssignProvinceFamily(float U, float ThermalIndex, float AridityIndex)
    {
        // Tabelle termiche [Temperate, Dry, Cold, Mixed]:
        // Theta ~ 0 (torrido) / Theta ~ 0.5 (temperato) / Theta ~ 1 (gelido).
        static constexpr float HotTable[4]       = { 0.15f, 0.55f, 0.05f, 0.25f };
        static constexpr float TemperateTable[4] = { 0.35f, 0.15f, 0.20f, 0.30f };
        static constexpr float ColdTable[4]      = { 0.10f, 0.15f, 0.55f, 0.20f };

        float Weights[4];

        if (ThermalIndex <= 0.5f)
        {
            const float T = FMath::Clamp(ThermalIndex / 0.5f, 0.0f, 1.0f);

            for (int32 k = 0; k < 4; ++k)
            {
                Weights[k] = FMath::Lerp(HotTable[k], TemperateTable[k], T);
            }
        }
        else
        {
            const float T = FMath::Clamp((ThermalIndex - 0.5f) / 0.5f, 0.0f, 1.0f);

            for (int32 k = 0; k < 4; ++k)
            {
                Weights[k] = FMath::Lerp(TemperateTable[k], ColdTable[k], T);
            }
        }

        // Modulazione aridita' globale: secca -> piu' Dry, meno Temperate;
        // umida -> leggero rafforzamento di Temperate e Cold.
        Weights[1] += 0.25f * AridityIndex;
        Weights[0] = FMath::Max(0.02f, Weights[0] - 0.20f * AridityIndex);
        Weights[2] += 0.08f * (1.0f - AridityIndex);

        const float Total = Weights[0] + Weights[1] + Weights[2] + Weights[3];

        if (Total <= 0.0001f)
        {
            return 3; // fallback deterministico: Mixed
        }

        const float InvTotal = 1.0f / Total;

        float Cumulative = 0.0f;

        for (int32 k = 0; k < 4; ++k)
        {
            Cumulative += Weights[k] * InvTotal;

            if (U <= Cumulative)
            {
                return static_cast<uint8>(k);
            }
        }

        return 3;
    }

    void AssignProvinceFamilies(
        int64 Seed,
        float ThermalIndex,
        float AridityIndex,
        uint8 (&OutFamilies)[TransitionProvinceCount]
    )
    {
        const uint64 ProvinceSeedHash =
            HashSeed64(static_cast<uint64>(Seed), 0x50726F76696E6365ULL);

        for (int32 i = 0; i < TransitionProvinceCount; ++i)
        {
            const uint64 FamilyHash = HashSeed64(
                ProvinceSeedHash,
                0xA0761D6478BD642FULL + static_cast<uint64>(i) * 0x517CC1B727220A95ULL);

            const float U = static_cast<float>(FamilyHash & 0xFFFFFFULL) / 16777216.0f;

            OutFamilies[i] = AssignProvinceFamily(U, ThermalIndex, AridityIndex);
        }
    }

    // ========================================================================
    // PBS v3 - PAES v1: PROVINCE-AUTHORIZED EXPRESSION SELECTION
    //
    //   Province -> Family -> Allowed Expressions -> Climate Expression
    //   Selector -> PrimaryBiome (espressione di p1) / SecondaryBiome
    //   (espressione di p2) -> BiomeBlend (transizione continua)
    //
    // La provincia determina l'identita' territoriale (QUALI biomi puo'
    // esprimere), il clima determina l'espressione locale. NON esiste piu'
    // un argmax globale sui 5 core biomes: un bioma non autorizzato dalla
    // famiglia della provincia non entra mai nel calcolo, qualunque sia la
    // sua suitability o il suo bias.
    //
    // SINCRONIZZAZIONE: usa SampleProvinceField() (stessa geometria Fase 1)
    // e le suitability esistenti. Nessun nuovo noise, nessun PlanetID.
    // ========================================================================

    constexpr float ExpressionGeoWeightPrimary = 1.0f;
    constexpr float ExpressionGeoWeightSecondary = 0.70f;

    // Espressione autorizzata: bioma + peso d'identita'.
    // Ordine biomi: 0 Forest, 1 Grassland, 2 Plains, 3 Desert, 4 Tundra.
    // Le liste sono in ordine crescente di indice bioma: lo scan con '>'
    // stretto applica il tie-break fisso Forest < Grassland < Plains <
    // Desert < Tundra (per Mixed, a parita' esatta Grassland precede Plains).
    struct FFamilyExpression
    {
        int32 Biome;
        float GeoWeight;
    };

    constexpr FFamilyExpression TemperateExpressions[] = { { 0, 1.00f }, { 1, 0.70f } };
    constexpr FFamilyExpression DryExpressions[]       = { { 1, 0.70f }, { 2, 0.70f }, { 3, 1.00f } };
    constexpr FFamilyExpression ColdExpressions[]      = { { 0, 0.70f }, { 4, 1.00f } };
    constexpr FFamilyExpression MixedExpressions[]     = { { 0, 0.70f }, { 1, 1.00f }, { 2, 1.00f } };

    struct FFamilyExpressionSet
    {
        const FFamilyExpression* Expressions;
        int32 Count;
    };

    constexpr FFamilyExpressionSet FamilyExpressionSets[4] =
    {
        { TemperateExpressions, 2 },   // 0 Temperate/Wet
        { DryExpressions, 3 },         // 1 Dry
        { ColdExpressions, 2 },        // 2 Cold
        { MixedExpressions, 3 }        // 3 Mixed
    };

    // Dispatch sulle suitability esistenti (nessuna nuova suitability).
    float ComputeCoreSuitability(int32 Biome, float Temperature, float Humidity)
    {
        switch (Biome)
        {
            case 0: return ComputeForestSuitability(Temperature, Humidity);
            case 1: return ComputeGrasslandSuitability(Temperature, Humidity);
            case 2: return ComputePlainsSuitability(Temperature, Humidity);
            case 3: return ComputeDesertSuitability(Temperature, Humidity);
            default: return ComputeTundraSuitability(Temperature, Humidity);
        }
    }

    // Climate Expression Selector: argmax RISTRETTO alle espressioni
    // autorizzate dalla famiglia. Il clima e i bias possono scegliere solo
    // dentro il set: non possono introdurre biomi esterni.
    int32 SelectProvinceExpression(
        int32 Family,
        float Temperature,
        float Humidity,
        const float (&BiasWeights)[5],
        float& OutExpressionScore
    )
    {
        const FFamilyExpressionSet& Set = FamilyExpressionSets[Family];

        int32 BestBiome = Set.Expressions[0].Biome;
        float BestScore = -1.0f;

        for (int32 i = 0; i < Set.Count; ++i)
        {
            const int32 Biome = Set.Expressions[i].Biome;

            const float Score =
                Set.Expressions[i].GeoWeight *
                ComputeCoreSuitability(Biome, Temperature, Humidity) *
                BiasWeights[Biome];

            if (Score > BestScore)
            {
                BestScore = Score;
                BestBiome = Biome;
            }
        }

        OutExpressionScore = BestScore;
        return BestBiome;
    }

    // Province ranking + nomination delle due espressioni + fattore di
    // transizione territoriale. Le espressioni sono valutate sul PROVINCIAL
    // CLIMATE (campionato al centro della provincia con le funzioni
    // climatiche esistenti + variazione deterministica bassa), NON sul clima
    // puntuale del vertice: il PrimaryBiome non cambia dentro il territorio.
    // La terza provincia non riceve alcun label: entra solo superando
    // effettivamente il ranking territoriale (p2/p1 dinamici).
    void ComputeProvinceExpressions(
        const FVector& Direction,
        int64 Seed,
        float GlobalPlanetTemperature,
        float GlobalPlanetHumidity,
        float ThermalIndex,
        float AridityIndex,
        const float (&BiasWeights)[5],
        int32& OutPrimaryBiome,
        int32& OutSecondaryBiome,
        float& OutPrimaryExpressionScore,
        float& OutTransitionFactor
    )
    {
        FProvinceFieldSample Sample;
        SampleProvinceField(Direction, Seed, Sample);

        uint8 Families[TransitionProvinceCount];
        AssignProvinceFamilies(Seed, ThermalIndex, AridityIndex, Families);

        int32 TopIndex = 0;
        int32 SecondIndex = 0;
        float TopWeight = -1.0f;
        float SecondWeight = -1.0f;

        for (int32 i = 0; i < TransitionProvinceCount; ++i)
        {
            const float W = Sample.Weights[i];

            if (W > TopWeight)
            {
                SecondWeight = TopWeight;
                SecondIndex = TopIndex;
                TopWeight = W;
                TopIndex = i;
            }
            else if (W > SecondWeight)
            {
                SecondWeight = W;
                SecondIndex = i;
            }
        }

        // L4: provincial climate del dominante e della seconda. Il centro
        // provincia fornisce la componente latitudinale/zonale tramite le
        // funzioni climatiche esistenti; la variazione deterministica resta
        // bassa ([-0.12, +0.12]) e non introduce alta frequenza: l'espressione
        // e' stabile dentro il territorio della provincia.
        const uint64 ProvinceSeedHash =
            HashSeed64(static_cast<uint64>(Seed), 0x50726F76696E6365ULL);

        auto ProvinceClimateVariation = [ProvinceSeedHash](int32 ProvinceIndex, uint64 Salt)
        {
            const uint64 VariationHash = HashSeed64(
                ProvinceSeedHash,
                Salt + static_cast<uint64>(ProvinceIndex) * 0x9E3779B97F4A7C15ULL);
            return (static_cast<float>(VariationHash & 0xFFFF) / 65535.0f - 0.5f) * 0.24f;
        };

        const float TopLatitudeTemperature = UPlanetBiomeGenerator::CalculateTemperature(
            Sample.Centers[TopIndex], 0.0f, Seed);
        const float TopLatitudeHumidity = UPlanetBiomeGenerator::CalculateHumidity(
            Sample.Centers[TopIndex], 0.0f, Seed);

        const float TopProvincialTemperature = FMath::Clamp(
            GlobalPlanetTemperature + (TopLatitudeTemperature - 0.5f) * 0.5f + ProvinceClimateVariation(TopIndex, 0x1A2B3C4D5E6F7080ULL),
            0.0f,
            1.0f
        );
        const float TopProvincialHumidity = FMath::Clamp(
            GlobalPlanetHumidity + (TopLatitudeHumidity - 0.5f) * 0.5f + ProvinceClimateVariation(TopIndex, 0x0F1E2D3C4B5A6978ULL),
            0.0f,
            1.0f
        );

        const float SecondLatitudeTemperature = UPlanetBiomeGenerator::CalculateTemperature(
            Sample.Centers[SecondIndex], 0.0f, Seed);
        const float SecondLatitudeHumidity = UPlanetBiomeGenerator::CalculateHumidity(
            Sample.Centers[SecondIndex], 0.0f, Seed);

        const float SecondProvincialTemperature = FMath::Clamp(
            GlobalPlanetTemperature + (SecondLatitudeTemperature - 0.5f) * 0.5f + ProvinceClimateVariation(SecondIndex, 0x1A2B3C4D5E6F7080ULL),
            0.0f,
            1.0f
        );
        const float SecondProvincialHumidity = FMath::Clamp(
            GlobalPlanetHumidity + (SecondLatitudeHumidity - 0.5f) * 0.5f + ProvinceClimateVariation(SecondIndex, 0x0F1E2D3C4B5A6978ULL),
            0.0f,
            1.0f
        );

        float PrimaryExpressionScore = 0.0f;
        float SecondaryExpressionScore = 0.0f;

        OutPrimaryBiome = SelectProvinceExpression(
            Families[TopIndex],
            TopProvincialTemperature,
            TopProvincialHumidity,
            BiasWeights,
            PrimaryExpressionScore
        );

        OutSecondaryBiome = SelectProvinceExpression(
            Families[SecondIndex],
            SecondProvincialTemperature,
            SecondProvincialHumidity,
            BiasWeights,
            SecondaryExpressionScore
        );

        OutPrimaryExpressionScore = PrimaryExpressionScore;

        const float DominanceRatio = (TopWeight > 0.000001f)
            ? FMath::Clamp(SecondWeight / TopWeight, 0.0f, 1.0f)
            : 1.0f;

        OutTransitionFactor = FMath::SmoothStep(
            TransitionDominanceStart,
            TransitionDominanceEnd,
            DominanceRatio
        );
    }

    // Mappatura core biome -> slot dell'array Weights[9] (convenzione
    // esistente: [2]=Plains, [3]=Grassland, [4]=Forest, [5]=Desert,
    // [6]=Tundra).
    constexpr int32 CoreWeightIndex[5] = { 4, 3, 2, 5, 6 };
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
    int64 Seed,
    float ThermalIndex,
    float AridityIndex
)
{
    Direction = Direction.GetSafeNormal();

    // ========================================================================
    // PBS v4 - AUTHORIZED EXPRESSION AFFINITIES
    //
    // Le affinita' regionali rappresentano la quota di autorizzazione
    // territoriale locale: quanto peso provinciale autorizza ciascun core
    // biome tramite i set della propria famiglia (GeoWeight). Campo di
    // carattere: non determina l'identita' primaria, mai competitivo.
    // La composizione delle famiglie e' quella climatica (L3), quindi le
    // affinities riflettono il regime planetario del pianeta.
    // ========================================================================

    FProvinceFieldSample Sample;
    SampleProvinceField(Direction, Seed, Sample);

    uint8 Families[TransitionProvinceCount];
    AssignProvinceFamilies(Seed, ThermalIndex, AridityIndex, Families);

    // ========================================================================
    // PBS v4 - AUTHORIZED EXPRESSION AFFINITIES
    //
    // RegionalAffinities come quota di autorizzazione territoriale: quanto
    // peso provinciale autorizza ciascun core biome (mai competitivo, mai
    // composito). Campo di carattere: non determina l'identita' primaria.
    // ========================================================================

    float Raw[5] = { 0.0f };

    for (int32 i = 0; i < TransitionProvinceCount; ++i)
    {
        const FFamilyExpressionSet& ExpressionSet = FamilyExpressionSets[Families[i]];

        for (int32 e = 0; e < ExpressionSet.Count; ++e)
        {
            Raw[ExpressionSet.Expressions[e].Biome] +=
                Sample.Weights[i] * ExpressionSet.Expressions[e].GeoWeight;
        }
    }

    const float RawTotal = Raw[0] + Raw[1] + Raw[2] + Raw[3] + Raw[4];
    const float InvRaw = (RawTotal > 0.0001f) ? (1.0f / RawTotal) : 1.0f;

    FRegionalBiomeAffinities Result;
    Result.Forest = Raw[0] * InvRaw;
    Result.Grassland = Raw[1] * InvRaw;
    Result.Plains = Raw[2] * InvRaw;
    Result.Desert = Raw[3] * InvRaw;
    Result.Tundra = Raw[4] * InvRaw;

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
    // ========================================================================
    // PBS v4 - PERCORSO LEGACY UNIFICATO
    //
    // Costruisce un profilo neutro (nessun bias climatico, neve non modulata
    // come nel comportamento storico di questo percorso, clima planetario
    // neutro) e delega allo stesso motore condiviso del percorso con profilo.
    // Un'unica fonte di verita' per la biome selection.
    // ========================================================================

    FPlanetProfile NeutralProfile;
    NeutralProfile.Archetype = EPlanetArchetype::Terran;
    NeutralProfile.WaterCoverage = 0.55f;
    NeutralProfile.TemperatureBias = 0.0f;
    NeutralProfile.HumidityBias = 0.0f;
    NeutralProfile.bSnowAllowed = true;
    NeutralProfile.SnowPotential = 1.0f;  // replica la neve legacy (non modulata)
    NeutralProfile.OrbitDistance = 0.0f;  // -> orbita neutra (regime temperato)
    // BiomeBiases: default della struct (tutti 0)

    return CalculateBiomeWithProfile(
        Direction,
        NormalizedHeight,
        SurfaceNormal,
        Seed,
        NeutralProfile,
        SeaLevel
    );
}






FPlanetBiomeData UPlanetBiomeGenerator::CalculateBiomeWithProfile(
    FVector Direction,
    float NormalizedHeight,
    FVector SurfaceNormal,
    int64 Seed,
    const FPlanetProfile& Profile,
    float SeaLevelOverride
)
{
    FPlanetBiomeData BiomeData;

    // ========================================================================
    // LIVELLO DEL MARE
    //
    // Unica fonte di verita: WaterCoverage -> SeaLevel. Il SeaLevelOverride
    // (>= 0) e' riservato al percorso legacy unificato, che passa esplicita-
    // mente la propria frontiera.
    // ========================================================================

    const float SeaLevel = (SeaLevelOverride >= 0.0f)
        ? SeaLevelOverride
        : CalculateSeaLevelFromWaterCoverage(Profile.WaterCoverage);

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
    // PBS v4 - L0: GLOBAL PLANETARY CLIMATE
    //
    // L'orbita e il profilo determinano il regime climatico planetario:
    // composizione delle famiglie (L3) e climate delle espressioni (L4).
    // ========================================================================

    float ThermalIndex = 0.5f;
    float AridityIndex = 0.5f;
    float GlobalPlanetTemperature = 0.5f;
    float GlobalPlanetHumidity = 0.5f;

    ComputePlanetaryClimate(
        Seed,
        Profile,
        ThermalIndex,
        AridityIndex,
        GlobalPlanetTemperature,
        GlobalPlanetHumidity
    );

    // RegionalAffinities: quota di autorizzazione territoriale coerente con
    // la composizione provinciale (campo di carattere, non competitivo).
    BiomeData.RegionalAffinities = CalculateRegionalAffinities(
        Direction,
        Seed,
        ThermalIndex,
        AridityIndex
    );

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
    // PBS v4 - PAES: PROVINCE-AUTHORIZED EXPRESSION SELECTION
    //
    // PrimaryBiome   = espressione della provincia dominante (p1)
    // SecondaryBiome = espressione della seconda provincia (p2)
    //
    // Le espressioni sono scelte sul PROVINCIAL CLIMATE (scala provincia,
    // stabile nel territorio) dentro i set autorizzati dalle famiglie.
    // I BiomeBiases del profilo modulano solo le espressioni autorizzate.
    // Nessun argmax globale sui 5 core biomes.
    // ========================================================================

    const float ExpressionBiasWeights[5] =
    {
        ApplyProfileBias(1.0f, Profile.BiomeBiases.ForestBias),
        ApplyProfileBias(1.0f, Profile.BiomeBiases.GrasslandBias),
        ApplyProfileBias(1.0f, Profile.BiomeBiases.PlainsBias),
        ApplyProfileBias(1.0f, Profile.BiomeBiases.DesertBias),
        ApplyProfileBias(1.0f, Profile.BiomeBiases.TundraBias)
    };

    int32 PrimaryCoreBiome = 0;
    int32 SecondaryCoreBiome = 0;
    float PrimaryExpressionScore = 0.0f;
    float ProvinceTransitionFactor = 0.0f;

    ComputeProvinceExpressions(
        Direction,
        Seed,
        GlobalPlanetTemperature,
        GlobalPlanetHumidity,
        ThermalIndex,
        AridityIndex,
        ExpressionBiasWeights,
        PrimaryCoreBiome,
        SecondaryCoreBiome,
        PrimaryExpressionScore,
        ProvinceTransitionFactor
    );

    // ========================================================================
    // ASSEGNAZIONE PESI
    //
    // Solo i due core biomes nominati ricevono peso. Il rapporto
    // W2 = W1 * t / (2 - t) produce, con la formula esistente
    // BiomeBlend = W2 / (W1 + W2), esattamente BiomeBlend = 0.5 * t
    // (0.5 = transizione paritaria come da contratto BiomeBlend).
    // ========================================================================

    float Weights[9] = { 0.0f };

    const float CoreBlendRatio = ProvinceTransitionFactor / (2.0f - ProvinceTransitionFactor);
    const float PrimaryCoreWeight = InternalLand * PrimaryExpressionScore;
    const float SecondaryCoreWeight = PrimaryCoreWeight * CoreBlendRatio;

    Weights[0] = 0.0f; // Ocean e gestito sopra con frontiera netta
    Weights[1] = BeachWeight;
    Weights[CoreWeightIndex[PrimaryCoreBiome]] = PrimaryCoreWeight;
    Weights[CoreWeightIndex[SecondaryCoreBiome]] =
        (SecondaryCoreBiome == PrimaryCoreBiome) ? 0.0f : SecondaryCoreWeight;
    Weights[7] = SnowWeight;
    Weights[8] = MountainWeight;

    // ========================================================================
    // SELEZIONE BIOMA PRIMARIO E SECONDARIO + BLEND FLUIDO
    // ========================================================================

    ResolveBiomeWeights(Weights, BiomeData);

    return BiomeData;
}