#include "Planet/PlanetLandformGenerator.h"

namespace
{
    // ========================================================================
    // DETERMINISTIC HASH & OFFSET GENERATION (SPLITMIX64)
    // ========================================================================

    uint64 HashSeed(uint64 Seed)
    {
        Seed += 0x9E3779B97F4A7C15ULL;

        Seed =
            (Seed ^ (Seed >> 30)) *
            0xBF58476D1CE4E5B9ULL;

        Seed =
            (Seed ^ (Seed >> 27)) *
            0x94D049BB133111EBULL;

        return Seed ^ (Seed >> 31);
    }

    FVector MakeSeedOffset(
        int64 Seed,
        uint64 Salt
    )
    {
        const uint64 Base =
            static_cast<uint64>(Seed) ^ Salt;

        const uint64 HashX =
            HashSeed(Base);

        const uint64 HashY =
            HashSeed(HashX);

        const uint64 HashZ =
            HashSeed(HashY);

        const float X =
            static_cast<float>(
                HashX & 0xFFFFFFULL
                ) /
            16777216.0f *
            200.0f -
            100.0f;

        const float Y =
            static_cast<float>(
                HashY & 0xFFFFFFULL
                ) /
            16777216.0f *
            200.0f -
            100.0f;

        const float Z =
            static_cast<float>(
                HashZ & 0xFFFFFFULL
                ) /
            16777216.0f *
            200.0f -
            100.0f;

        return FVector(X, Y, Z);
    }

    float NormalizeNoise(float Noise)
    {
        return FMath::Clamp(
            (Noise + 1.0f) * 0.5f,
            0.0f,
            1.0f
        );
    }

    float SafeScale(float Scale)
    {
        return FMath::Max(Scale, 0.0001f);
    }

    float SmoothMask(
        float Value,
        float Start,
        float End
    )
    {
        return FMath::SmoothStep(
            Start,
            End,
            FMath::Clamp(Value, 0.0f, 1.0f)
        );
    }


    // ========================================================================
    // CONTINUOUS DOMAIN WARPING
    //
    // Esegue una distorsione a bassa frequenza e derivata continua sul versore
    // sferico, evitando schemi artificiali e garantendo cinture tettoniche
    // curvilinee organiche su scala globale.
    // ========================================================================

    FVector EvaluateOrogenicWarp(
        const FVector& Direction,
        float Scale,
        int64 Seed
    )
    {
        const FVector WarpOffset =
            MakeSeedOffset(
                Seed,
                0x7A93F1C2B54E8D01ULL
            );

        const float WarpFrequency =
            Scale * 0.22f;

        const float WarpA =
            FMath::PerlinNoise3D(
                Direction * WarpFrequency +
                WarpOffset
            );

        const float WarpB =
            FMath::PerlinNoise3D(
                Direction * WarpFrequency +
                WarpOffset * 1.57f
            );

        const float WarpC =
            FMath::PerlinNoise3D(
                Direction * WarpFrequency +
                WarpOffset * 2.11f
            );

        return (
            Direction +
            FVector(WarpA, WarpB, WarpC) * 0.35f
            ).GetSafeNormal();
    }


    // ========================================================================
    // OROGENIC BELT EVALUATOR
    //
    // Costruisce la spina orogenica continua integrando:
    // 1. Corridoio orogenico macro (bassa frequenza globale);
    // 2. Cresta dorsale differenziabile C1 (1 - sqrt(N^2 + eps^2));
    // 3. Modulazione regionale continua (valichi, depressioni, valli trasversali).
    // ========================================================================

    float EvaluateOrogenicBelt(
        FVector Direction,
        int64 Seed,
        float Scale
    )
    {
        Direction =
            Direction.GetSafeNormal();

        const float S =
            SafeScale(Scale);

        const FVector WarpedDirection =
            EvaluateOrogenicWarp(Direction, S, Seed);

        const FVector OffsetMacro =
            MakeSeedOffset(
                Seed,
                0xA17C39D482E651F0ULL
            );

        const FVector OffsetSpine =
            MakeSeedOffset(
                Seed,
                0x5D8E2A1F4C73B902ULL
            );

        const FVector OffsetRegional =
            MakeSeedOffset(
                Seed,
                0xE36B91F8402CA7D5ULL
            );

        // Corridoio orogenico macro su scala continentale
        const float MacroBelt =
            NormalizeNoise(
                FMath::PerlinNoise3D(
                    WarpedDirection * (S * 0.34f) +
                    OffsetMacro
                )
            );

        // Spina dorsale tettonica continua (regolarizzata con C1 continuo)
        const float RidgeNoise =
            FMath::PerlinNoise3D(
                WarpedDirection * (S * 0.58f) +
                OffsetSpine
            );

        const float SmoothSpine =
            1.0f -
            FMath::Sqrt(
                RidgeNoise * RidgeNoise +
                0.04f
            );

        const float NormalizedSpine =
            FMath::Clamp(
                (SmoothSpine - 0.15f) / 0.75f,
                0.0f,
                1.0f
            );

        // Modulazione strutturale regionale lungo l'arco della cintura
        const float RegionalBelt =
            NormalizeNoise(
                FMath::PerlinNoise3D(
                    WarpedDirection * (S * 0.88f) +
                    OffsetRegional
                )
            );

        // Campo orogenico continuo composito
        const float RawBelt =
            MacroBelt * 0.42f +
            NormalizedSpine * 0.44f +
            RegionalBelt * 0.14f;

        // Transizione Hermite ampia: definisce grandi sistemi e sfumature progressive
        return SmoothMask(
            RawBelt,
            0.34f,
            0.72f
        );
    }
}


// ============================================================================
// OROGENIC BELT MASK
// ============================================================================

float UPlanetLandformGenerator::GetOrogenicBeltMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    return EvaluateOrogenicBelt(
        Direction,
        Seed,
        Scale
    );
}


// ============================================================================
// CORRELATED MOUNTAIN MASK (MOUNTAIN CORE)
//
// Occupa il nucleo della cintura orogenica. Non e' una bolla isolata ma il cuore
// strutturale delle catene, con variazioni regionali di estensione dei massicci.
// ============================================================================

float UPlanetLandformGenerator::GetCorrelatedMountainMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float S =
        SafeScale(Scale);

    const float OrogenicBelt =
        EvaluateOrogenicBelt(
            Direction,
            Seed,
            Scale
        );

    if (OrogenicBelt <= 0.001f)
    {
        return 0.0f;
    }

    const FVector WarpedDirection =
        EvaluateOrogenicWarp(Direction, S, Seed);

    // Variazione regionale dei massicci: differenzia larghezza e intensita' dei gruppi montuosi
    const FVector MassifOffset =
        MakeSeedOffset(
            Seed,
            0x4D2E8B91C3F7A055ULL
        );

    const float MassifNoise =
        NormalizeNoise(
            FMath::PerlinNoise3D(
                WarpedDirection * (S * 0.72f) +
                MassifOffset
            )
        );

    // Modulazione morbida (+-0.08) della larghezza del nucleo lungo la cintura
    const float ShiftedBelt =
        OrogenicBelt +
        (MassifNoise - 0.5f) * 0.16f;

    return SmoothMask(
        ShiftedBelt,
        0.44f,
        0.78f
    );
}


// ============================================================================
// CORRELATED FOOTHILL MASK
//
// Circonda naturalmente il nucleo montuoso all'interno dell'orogenesi.
// Non usa esclusioni distruttive ma forma la base di transizione verso le pianure.
// ============================================================================

float UPlanetLandformGenerator::GetCorrelatedFoothillMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float S =
        SafeScale(Scale);

    const float OrogenicBelt =
        EvaluateOrogenicBelt(
            Direction,
            Seed,
            Scale
        );

    if (OrogenicBelt <= 0.001f)
    {
        return 0.0f;
    }

    // Salita graduale dai bordi esterni della cintura orogenica
    const float FoothillRise =
        SmoothMask(
            OrogenicBelt,
            0.12f,
            0.42f
        );

    // Transizione verso il nucleo: sostiene la base dei massicci senza azzerarsi bruscamente
    const float FoothillFall =
        1.0f -
        SmoothMask(
            OrogenicBelt,
            0.56f,
            0.88f
        ) * 0.65f;

    return FMath::Clamp(
        FoothillRise * FoothillFall,
        0.0f,
        1.0f
    );
}


// ============================================================================
// REGIONAL HILL MASK (INTERNAL CONTINENTAL HILLS & PLATEAUS)
//
// Genera province collinari e altopiani all'interno delle masse continentali,
// impedendo che l'entroterra rimanga una gigantesca pianura uniforme e piatta.
// ============================================================================

float UPlanetLandformGenerator::GetRegionalHillMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float S =
        SafeScale(Scale);

    const FVector HillWarpOffset =
        MakeSeedOffset(
            Seed,
            0x3B9F1C74E2A85D06ULL
        );

    const float WarpA =
        FMath::PerlinNoise3D(
            Direction * (S * 0.28f) +
            HillWarpOffset
        );

    const float WarpB =
        FMath::PerlinNoise3D(
            Direction * (S * 0.24f) +
            HillWarpOffset * 1.63f
        );

    const FVector WarpedDir =
        (
            Direction +
            FVector(WarpA, WarpB, -WarpA) * 0.26f
            ).GetSafeNormal();

    const FVector HillOffsetMacro =
        MakeSeedOffset(
            Seed,
            0x7E14C9A250B83DF1ULL
        );

    const FVector HillOffsetRegional =
        MakeSeedOffset(
            Seed,
            0xD4A82F109C5E76B3ULL
        );

    // Campo macro che definisce la distribuzione delle province collinari
    const float MacroHills =
        NormalizeNoise(
            FMath::PerlinNoise3D(
                WarpedDir * (S * 0.46f) +
                HillOffsetMacro
            )
        );

    // Rilievo regionale interno
    const float RegionalHills =
        NormalizeNoise(
            FMath::PerlinNoise3D(
                WarpedDir * (S * 0.95f) +
                HillOffsetRegional
            )
        );

    const float CombinedField =
        MacroHills * 0.65f +
        RegionalHills * 0.35f;

    // Transizione graduale Hermite: le colline si sviluppano dolcemente sui territori interni
    return SmoothMask(
        CombinedField,
        0.38f,
        0.72f
    );
}


// ============================================================================
// COMPATIBILITY IMPLEMENTATIONS
// ============================================================================

float UPlanetLandformGenerator::GetLandformMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    const float OrogenicBelt =
        GetOrogenicBeltMask(
            Direction,
            Seed,
            Scale
        );

    const float RegionalHills =
        GetRegionalHillMask(
            Direction,
            Seed,
            Scale
        );

    // Copertura delle forme strutturate del terreno (cinture montuose + colline interne)
    // Permette alle pianure di esistere nei bacini, senza invadere l'intero continente
    const float CombinedLandforms =
        1.0f -
        (1.0f - OrogenicBelt) *
        (1.0f - RegionalHills * 0.72f);

    return FMath::Clamp(
        CombinedLandforms,
        0.0f,
        1.0f
    );
}


float UPlanetLandformGenerator::GetHillMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    const float Foothills =
        GetCorrelatedFoothillMask(
            Direction,
            Seed,
            Scale
        );

    const float RegionalHills =
        GetRegionalHillMask(
            Direction,
            Seed,
            Scale
        );

    // Unione non-distruttiva: combina i contrafforti orogenici con le province collinari interne
    const float CombinedHills =
        1.0f -
        (1.0f - Foothills) *
        (1.0f - RegionalHills);

    return FMath::Clamp(
        CombinedHills,
        0.0f,
        1.0f
    );
}


float UPlanetLandformGenerator::GetMountainMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    return GetCorrelatedMountainMask(
        Direction,
        Seed,
        Scale
    );
}


float UPlanetLandformGenerator::GetMountainChainMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float S =
        SafeScale(Scale);

    const FVector WarpedDirection =
        EvaluateOrogenicWarp(Direction, S, Seed);

    const FVector ChainOffsetA =
        MakeSeedOffset(
            Seed,
            0x8C15E394A2D7B0F1ULL
        );

    const FVector ChainOffsetB =
        MakeSeedOffset(
            Seed,
            0x3E7B20A195C84DF6ULL
        );

    // Spina dorsale continua principale
    const float RidgeNoiseA =
        FMath::PerlinNoise3D(
            WarpedDirection * (S * 0.76f) +
            ChainOffsetA
        );

    const float RidgeA =
        1.0f -
        FMath::Sqrt(
            RidgeNoiseA * RidgeNoiseA +
            0.035f
        );

    // Cresta secondaria di raccordo
    const float RidgeNoiseB =
        FMath::PerlinNoise3D(
            WarpedDirection * (S * 1.28f) +
            ChainOffsetB
        );

    const float RidgeB =
        1.0f -
        FMath::Sqrt(
            RidgeNoiseB * RidgeNoiseB +
            0.035f
        );

    const float CombinedRidge =
        RidgeA * 0.72f +
        RidgeB * 0.28f;

    // Transizione continua e morbida per evitare creste spezzate
    return SmoothMask(
        CombinedRidge,
        0.32f,
        0.74f
    );
}