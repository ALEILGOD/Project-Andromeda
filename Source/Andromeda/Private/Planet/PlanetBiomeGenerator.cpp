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

    // Copia sincronizzata della FamilySupport matrix (stessi valori esatti).
    constexpr float TransitionFamilySupport[4][5] =
    {
        { 1.00f, 0.75f, 0.55f, 0.15f, 0.15f }, // 0 Temperate/Wet
        { 0.12f, 0.55f, 0.60f, 1.00f, 0.08f }, // 1 Dry
        { 0.40f, 0.20f, 0.50f, 0.08f, 1.00f }, // 2 Cold
        { 0.75f, 0.75f, 0.70f, 0.50f, 0.50f }  // 3 Mixed
    };

    // Bioma CANDIDATO primario di ciascuna famiglia: argmax della riga
    // (pareggio -> indice minore). Ordine: 0 Forest, 1 Grassland, 2 Plains,
    // 3 Desert, 4 Tundra.
    constexpr int32 TransitionFamilyCandidate[4] =
    {
        0, // Temperate/Wet -> Forest (1.00)
        3, // Dry           -> Desert (1.00)
        4, // Cold          -> Tundra (1.00)
        0  // Mixed         -> Forest (0.75, tie con Grassland -> indice minore)
    };

    // Finestra di transizione sul rapporto di peso W2/W1 tra le due province
    // dominanti: sotto lo start una provincia domina nettamente (selezione
    // normale), sopra l'end la posizione e' pienamente in transizione.
    constexpr float TransitionDominanceStart = 0.40f;
    constexpr float TransitionDominanceEnd = 0.75f;

    // Campione del campo province in una direzione sulla sfera.
    struct FProvinceFieldSample
    {
        float Weights[TransitionProvinceCount]; // somma ~1
        uint8 Families[TransitionProvinceCount]; // 0..3
    };

    // Replica deterministica del campo province (solo pesi e famiglie).
    // Geometria IDENTICA a CalculateRegionalAffinities().
    void SampleProvinceField(
        const FVector& Direction,
        int64 Seed,
        FProvinceFieldSample& Out
    )
    {
        const FVector Dir = Direction.GetSafeNormal();

        const uint64 ProvinceSeedHash =
            HashSeed64(static_cast<uint64>(Seed), 0x50726F76696E6365ULL);

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

            // Famiglia ecologica deterministica (identica).
            const uint64 FamilyHash = HashSeed64(
                ProvinceSeedHash,
                0xA0761D6478BD642FULL + static_cast<uint64>(i) * 0x517CC1B727220A95ULL);
            Out.Families[i] = static_cast<uint8>(FamilyHash & 0x3ULL);

            // Influenza gaussiana sulla sfera (identica, senza acos).
            const float Dot = FVector::DotProduct(Dir, Center);
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

    // Calcola l'allowance di transizione [~0..1] per i 5 core biomes.
    //
    // Dominant Pair: le due province localmente piu' influenti.
    // TransitionFactor: SmoothStep sul rapporto W2/W1 dei loro pesi
    // (0 = dominanza netta -> selezione normale; 1 = confine pieno).
    // Il supporto primario continuo (peso delle province locali che hanno
    // quel bioma come candidato) rende la restrizione spazialmente continua
    // e morbida, senza soglie nette ne' cambiamenti discreti di candidati.
    void ComputeProvinceTransitionAllowance(
        const FVector& Direction,
        int64 Seed,
        float (&OutAllowance)[5]
    )
    {
        FProvinceFieldSample Sample;
        SampleProvinceField(Direction, Seed, Sample);

        // Dominant Pair: le due province localmente piu' influenti.
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

        // TransitionFactor continuo dal rapporto tra il peso della provincia
        // dominante e quello della seconda.
        const float DominanceRatio = (TopWeight > 0.000001f)
            ? FMath::Clamp(SecondWeight / TopWeight, 0.0f, 1.0f)
            : 1.0f;

        const float TransitionFactor = FMath::SmoothStep(
            TransitionDominanceStart,
            TransitionDominanceEnd,
            DominanceRatio
        );

        // Supporto primario continuo: peso totale delle province locali che
        // hanno quel core biome come candidato primario di famiglia.
        float PrimarySupport[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        for (int32 i = 0; i < TransitionProvinceCount; ++i)
        {
            const int32 Candidate = TransitionFamilyCandidate[Sample.Families[i]];
            PrimarySupport[Candidate] += Sample.Weights[i];
        }

        float MaxSupport = 0.0f;
        for (int32 k = 0; k < 5; ++k)
        {
            MaxSupport = FMath::Max(MaxSupport, PrimarySupport[k]);
        }

        for (int32 k = 0; k < 5; ++k)
        {
            // Normalizzato [0..1]: 1 = bioma candidato della provincia locale
            // dominante; ~0 = bioma non presente come candidato locale.
            const float NormalizedSupport = (MaxSupport > 0.000001f)
                ? FMath::Clamp(PrimarySupport[k] / MaxSupport, 0.0f, 1.0f)
                : 1.0f;

            // Allowance: 1.0 = selezione normale; < 1.0 = soppressione
            // progressiva del terzo bioma in zona di transizione. I due
            // candidati dominanti restano ~1 e il clima continua a decidere
            // la loro forza relativa tramite i punteggi esistenti.
            OutAllowance[k] = 1.0f - TransitionFactor * (1.0f - NormalizedSupport);
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
    // transizione territoriale. La terza provincia non riceve alcun label:
    // entra solo se supera effettivamente il ranking (p2/p1 dinamici),
    // mai per suitability climatica.
    void ComputeProvinceExpressions(
        const FVector& Direction,
        int64 Seed,
        float Temperature,
        float Humidity,
        const float (&BiasWeights)[5],
        int32& OutPrimaryBiome,
        int32& OutSecondaryBiome,
        float& OutPrimaryExpressionScore,
        float& OutTransitionFactor
    )
    {
        FProvinceFieldSample Sample;
        SampleProvinceField(Direction, Seed, Sample);

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

        float PrimaryExpressionScore = 0.0f;
        float SecondaryExpressionScore = 0.0f;

        OutPrimaryBiome = SelectProvinceExpression(
            Sample.Families[TopIndex],
            Temperature,
            Humidity,
            BiasWeights,
            PrimaryExpressionScore
        );

        OutSecondaryBiome = SelectProvinceExpression(
            Sample.Families[SecondIndex],
            Temperature,
            Humidity,
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
    int64 Seed
)
{
    Direction = Direction.GetSafeNormal();

    // ========================================================================
    // SPHERICAL SEEDED BIOGEOGRAPHIC PROVINCES (PBS v3 - Fase 1)
    //
    // 8 province sferiche con centri deterministici (base Fibonacci + perturbazione
    // angolare seedata), rotazione globale per-pianeta, influenza gaussiana continua
    // sulla sfera (overlap tra province, niente bordi netti) e conversione in
    // affinita per i 5 core biomes tramite famiglie ecologiche.
    // NESSUN nuovo noise: solo geometria sferica deterministica.
    //
    // Nota determinismo: l'API del PBS passa a questo livello il seed generazione
    // del pianeta (identita' unica del pianeta a questo livello della pipeline:
    // stesso pianeta -> stesso Seed; pianeti diversi -> Seed diversi). L'hash
    // dedicato sotto decorrela lo scheletro province dagli altri sistemi che
    // consumano lo stesso Seed raw (temperature, humidity, ecc.).
    // ========================================================================

    constexpr int32 ProvinceCount = 8;
    constexpr float Sigma = 0.35f; // raggio di influenza (~radianti effettivi)
    constexpr float InvTwoSigmaSq = 0.5f / (Sigma * Sigma);

    // Seed composito dell'ecosistema province (salt dedicato "Province").
    const uint64 ProvinceSeedHash =
        HashSeed64(static_cast<uint64>(Seed), 0x50726F76696E6365ULL);

    // Rotazione globale deterministica dello scheletro delle province.
    const uint64 RotHash = HashSeed64(ProvinceSeedHash, 0x5F3759DFULL);
    const float Yaw   = (static_cast<float>(RotHash & 0xFFFF) / 65535.0f) * 2.0f * PI;
    const float Pitch = (static_cast<float>((RotHash >> 16) & 0xFFFF) / 65535.0f) * 2.0f * PI;
    const float Roll  = (static_cast<float>((RotHash >> 32) & 0xFFFF) / 65535.0f) * 2.0f * PI;

    const float Cy = FMath::Cos(Yaw),   Sy = FMath::Sin(Yaw);
    const float Cp = FMath::Cos(Pitch), Sp = FMath::Sin(Pitch);
    const float Cr = FMath::Cos(Roll),  Sr = FMath::Sin(Roll);

    // Matrice R = Yaw * Pitch * Roll (row-major)
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

    float Influences[ProvinceCount];
    uint8 Families[ProvinceCount];
    float TotalInfluence = 0.0f;

    for (int32 i = 0; i < ProvinceCount; ++i)
    {
        // ---- Centro base: Fibonacci sphere (copertura uniforme) ----
        const float Y = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(ProvinceCount);
        const float R = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Y * Y));
        const float Theta = GoldenAngle * static_cast<float>(i);
        FVector Center(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Y);

        // ---- Perturbazione angolare deterministica per-centro ----
        // Evita che pianeti diversi condividano identica Fibonacci ruotata:
        // ogni centro e' spostato in modo totalmente deterministico dal seed.
        const uint64 CenterHash = HashSeed64(
            ProvinceSeedHash,
            0xD1B54A32D192ED03ULL + static_cast<uint64>(i) * 0x9E3779B97F4A7C15ULL);

        constexpr float JitterScale = 0.35f; // perturbazione moderata (no cluster patologici)
        const float JX = ((static_cast<float>(CenterHash & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * JitterScale;
        const float JY = ((static_cast<float>((CenterHash >> 16) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * JitterScale;
        const float JZ = ((static_cast<float>((CenterHash >> 32) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * JitterScale;

        Center.X += JX;
        Center.Y += JY;
        Center.Z += JZ;

        // ---- Rotazione globale (orienta l'intero scheletro per pianeta) ----
        const FVector Rotated(
            R00 * Center.X + R01 * Center.Y + R02 * Center.Z,
            R10 * Center.X + R11 * Center.Y + R12 * Center.Z,
            R20 * Center.X + R21 * Center.Y + R22 * Center.Z
        );

        Center = Rotated.GetSafeNormal();

        // ---- Famiglia ecologica deterministica (0..3) ----
        const uint64 FamilyHash = HashSeed64(
            ProvinceSeedHash,
            0xA0761D6478BD642FULL + static_cast<uint64>(i) * 0x517CC1B727220A95ULL);
        Families[i] = static_cast<uint8>(FamilyHash & 0x3ULL);

        // ---- Influenza sferica continua: exp(-d^2 / 2 sigma^2) ----
        // d^2 (corda) = 2 - 2*dot: monotona rispetto alla distanza angolare,
        // evita acos. Le province si sovrappongono dolcemente.
        const float Dot = FVector::DotProduct(Direction, Center);
        const float ChordSq = FMath::Max(0.0f, 2.0f - 2.0f * Dot);
        Influences[i] = FMath::Exp(-ChordSq * InvTwoSigmaSq);
        TotalInfluence += Influences[i];
    }

    // ========================================================================
    // NORMALIZZAZIONE INFLUENCE + CONVERSIONE PROVINCE -> BIOME AFFINITIES
    //
    // ProvinceWeight[i] = Influence[i] / TotalInfluence (fallback deterministico
    // se la somma e' circa zero). Poi:
    // Affinity[k] = Somma_i ProvinceWeight[i] * FamilySupport[Family[i]][k]
    // con FamilySupport nell'ordine [Forest, Grassland, Plains, Desert, Tundra].
    // ========================================================================

    float Affinity[5] = { 0.0f };

    if (TotalInfluence > 0.0001f)
    {
        static constexpr float FamilySupport[4][5] =
        {
            { 1.00f, 0.75f, 0.55f, 0.15f, 0.15f }, // 0 Temperate/Wet -> Forest + Grassland + Plains
            { 0.12f, 0.55f, 0.60f, 1.00f, 0.08f }, // 1 Dry           -> Desert + Plains + Grassland
            { 0.40f, 0.20f, 0.50f, 0.08f, 1.00f }, // 2 Cold          -> Tundra + Plains + Forest fredda
            { 0.75f, 0.75f, 0.70f, 0.50f, 0.50f }  // 3 Mixed         -> combinazioni piu ampie
        };

        for (int32 i = 0; i < ProvinceCount; ++i)
        {
            const float Weight = Influences[i] / TotalInfluence;
            for (int32 k = 0; k < 5; ++k)
            {
                Affinity[k] += Weight * FamilySupport[Families[i]][k];
            }
        }
    }
    else
    {
        // Fallback deterministico sicuro (nessuna divisione per zero).
        for (int32 k = 0; k < 5; ++k)
        {
            Affinity[k] = 0.20f;
        }
    }

    // Normalizzazione finale a somma 1 (le family weights non sono a somma 1).
    const float AffinitySum =
        Affinity[0] + Affinity[1] + Affinity[2] + Affinity[3] + Affinity[4];
    const float InvAffinitySum = (AffinitySum > 0.0001f) ? (1.0f / AffinitySum) : 1.0f;

    FRegionalBiomeAffinities Result;
    Result.Forest = Affinity[0] * InvAffinitySum;
    Result.Grassland = Affinity[1] * InvAffinitySum;
    Result.Plains = Affinity[2] * InvAffinitySum;
    Result.Desert = Affinity[3] * InvAffinitySum;
    Result.Tundra = Affinity[4] * InvAffinitySum;

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
    // PBS v3 - PAES v1: PROVINCE-AUTHORIZED EXPRESSION SELECTION (legacy path)
    //
    // PrimaryBiome   = espressione della provincia dominante (p1)
    // SecondaryBiome = espressione della seconda provincia (p2)
    //
    // Il clima (suitability esistenti) sceglie SOLO tra le espressioni
    // autorizzate dalla famiglia di ciascuna provincia. Nessun argmax
    // globale sui 5 core biomes: un bioma non autorizzato dalle province
    // dominanti non entra mai nel calcolo.
    // ========================================================================

    constexpr float ExpressionBiasWeights[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    int32 PrimaryCoreBiome = 0;
    int32 SecondaryCoreBiome = 0;
    float PrimaryExpressionScore = 0.0f;
    float ProvinceTransitionFactor = 0.0f;

    ComputeProvinceExpressions(
        Direction,
        Seed,
        Temperature,
        Humidity,
        ExpressionBiasWeights,
        PrimaryCoreBiome,
        SecondaryCoreBiome,
        PrimaryExpressionScore,
        ProvinceTransitionFactor
    );

    // Il peso del secondary e' legato a quello del primary scalato dal
    // fattore di transizione: garantisce che l'espressione della provincia
    // dominante resti il massimo tra i core (il fattore 0.999 mantiene
    // l'ordine stretto anche a transizione piena).
    const float CoreBlendFactor = FMath::Min(ProvinceTransitionFactor, 0.999f);
    const float PrimaryCoreWeight = InternalLand * PrimaryExpressionScore;
    const float SecondaryCoreWeight = PrimaryCoreWeight * CoreBlendFactor;

    // ========================================================================
    // ASSEGNAZIONE PESI
    //
    // Solo i due core biomes nominati ricevono peso; gli altri core restano
    // a zero (non competono: nessun argmax globale). Ocean/Beach/Mountain/
    // Snow mantengono i loro canali invariati.
    // ========================================================================

    float Weights[9] = { 0.0f };

    Weights[0] = 0.0f; // Ocean è gestito sopra con frontiera netta
    Weights[1] = BeachWeight;
    Weights[CoreWeightIndex[PrimaryCoreBiome]] = PrimaryCoreWeight;
    Weights[CoreWeightIndex[SecondaryCoreBiome]] =
        (SecondaryCoreBiome == PrimaryCoreBiome) ? 0.0f : SecondaryCoreWeight;
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
    // PBS v3 - PAES v1: PROVINCE-AUTHORIZED EXPRESSION SELECTION (profile path)
    //
    // PrimaryBiome   = espressione della provincia dominante (p1)
    // SecondaryBiome = espressione della seconda provincia (p2)
    //
    // I BiomeBiases del profilo agiscono SOLO come moltiplicatori delle
    // espressioni gia' autorizzate dalle famiglie: non possono introdurre
    // biomi esterni al set (es. un DesertBias enorme su provincia Temperate
    // e' senza effetto: Desert non e' autorizzato da Temperate). Nessun
    // argmax globale sui 5 core biomes.
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
        Temperature,
        Humidity,
        ExpressionBiasWeights,
        PrimaryCoreBiome,
        SecondaryCoreBiome,
        PrimaryExpressionScore,
        ProvinceTransitionFactor
    );

    // Il peso del secondary e' legato a quello del primary scalato dal
    // fattore di transizione territoriale: l'identita' resta stabile anche
    // a transizione piena (fattore 0.999 mantiene l'ordine stretto).
    const float CoreBlendFactor = FMath::Min(ProvinceTransitionFactor, 0.999f);
    const float PrimaryCoreWeight = InternalLand * PrimaryExpressionScore;
    const float SecondaryCoreWeight = PrimaryCoreWeight * CoreBlendFactor;

    // ========================================================================
    // ASSEGNAZIONE PESI
    //
    // Solo i due core biomes nominati ricevono peso; gli altri core restano
    // a zero (non competono: nessun argmax globale). Ocean/Beach/Mountain/
    // Snow mantengono i loro canali invariati.
    // ========================================================================

    float Weights[9] = { 0.0f };

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