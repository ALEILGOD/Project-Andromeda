#include "Planet/PlanetLandformGenerator.h"

namespace
{
    // ============================================================
    // HASH DETERMINISTICO
    // ============================================================

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
        uint64 Base =
            static_cast<uint64>(Seed);

        Base ^=
            Salt;

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

        return FVector(
            X,
            Y,
            Z
        );
    }


    float NormalizeNoise(
        float Noise
    )
    {
        return FMath::Clamp(
            (Noise + 1.0f) * 0.5f,
            0.0f,
            1.0f
        );
    }


    float SafeScale(
        float Scale
    )
    {
        return FMath::Max(
            Scale,
            0.0001f
        );
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
            FMath::Clamp(
                Value,
                0.0f,
                1.0f
            )
        );
    }


    // ============================================================
    // CONTINUOUS REGION
    //
    // Crea una regione ampia e morbida.
    //
    // Non esiste un bordo netto.
    // La regione entra lentamente nel terreno.
    // ============================================================

    float GenerateRegion(
        FVector Direction,
        float Scale,
        const FVector& Offset,
        float Threshold,
        float Softness
    )
    {
        const float Noise =
            NormalizeNoise(
                FMath::PerlinNoise3D(
                    Direction *
                    Scale +
                    Offset
                )
            );

        return SmoothMask(
            Noise,
            Threshold - Softness,
            Threshold + Softness
        );
    }


    // ============================================================
    // MOUNTAIN REGION
    //
    // Ogni regione usa:
    //
    // 1. noise macro
    // 2. noise regionale
    // 3. domain warp
    //
    // Questo evita che le montagne sembrino distribuite
    // uniformemente su tutto il pianeta.
    // ============================================================

    float GenerateMountainRegion(
        FVector Direction,
        float Scale,
        int64 Seed,
        uint64 Salt,
        float Threshold,
        float Softness
    )
    {
        const FVector WarpOffset =
            MakeSeedOffset(
                Seed,
                Salt ^ 0x19A7D43C82F651B0ULL
            );

        const float WarpA =
            FMath::PerlinNoise3D(
                Direction *
                Scale *
                0.22f +
                WarpOffset
            );

        const float WarpB =
            FMath::PerlinNoise3D(
                Direction *
                Scale *
                0.19f +
                WarpOffset *
                1.71f
            );

        const float WarpC =
            FMath::PerlinNoise3D(
                Direction *
                Scale *
                0.25f +
                WarpOffset *
                2.13f
            );

        const FVector WarpedDirection =
            (
                Direction +
                FVector(
                    WarpA,
                    WarpB,
                    WarpC
                ) *
                0.28f
                ).GetSafeNormal();

        const FVector RegionOffset =
            MakeSeedOffset(
                Seed,
                Salt
            );

        const float MacroNoise =
            NormalizeNoise(
                FMath::PerlinNoise3D(
                    WarpedDirection *
                    Scale *
                    0.48f +
                    RegionOffset
                )
            );

        const float RegionalNoise =
            NormalizeNoise(
                FMath::PerlinNoise3D(
                    WarpedDirection *
                    Scale *
                    0.92f +
                    RegionOffset *
                    1.43f
                )
            );

        const float RegionField =
            MacroNoise *
            0.70f +
            RegionalNoise *
            0.30f;

        return SmoothMask(
            RegionField,
            Threshold - Softness,
            Threshold + Softness
        );
    }


    // ============================================================
    // HILL REGION
    // ============================================================

    float GenerateHillRegion(
        FVector Direction,
        float Scale,
        int64 Seed,
        uint64 Salt,
        float Threshold,
        float Softness
    )
    {
        const FVector Offset =
            MakeSeedOffset(
                Seed,
                Salt
            );

        const float LargeNoise =
            NormalizeNoise(
                FMath::PerlinNoise3D(
                    Direction *
                    Scale *
                    0.50f +
                    Offset
                )
            );

        const float RegionalNoise =
            NormalizeNoise(
                FMath::PerlinNoise3D(
                    Direction *
                    Scale *
                    1.05f +
                    Offset *
                    1.51f
                )
            );

        const float Field =
            LargeNoise *
            0.68f +
            RegionalNoise *
            0.32f;

        return SmoothMask(
            Field,
            Threshold - Softness,
            Threshold + Softness
        );
    }


    // ============================================================
    // COMBINE REGIONS
    //
    // Le regioni vengono sommate in modo controllato.
    //
    // Non vogliamo che 4 montagne sovrapposte producano
    // automaticamente un'altezza assurda.
    // ============================================================

    float CombineRegions(
        float A,
        float B,
        float C,
        float D
    )
    {
        const float Combined =
            1.0f -
            (
                (1.0f - A) *
                (1.0f - B) *
                (1.0f - C) *
                (1.0f - D)
                );

        return FMath::Clamp(
            Combined,
            0.0f,
            1.0f
        );
    }
}


// ============================================================================
// LANDFORM MASK
// ============================================================================

float UPlanetLandformGenerator::GetLandformMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float S =
        SafeScale(Scale);

    const FVector Offset =
        MakeSeedOffset(
            Seed,
            0xA17C39D482E651F0ULL
        );

    const float MacroNoise =
        NormalizeNoise(
            FMath::PerlinNoise3D(
                Direction *
                S *
                0.34f +
                Offset
            )
        );

    const float RegionalNoise =
        NormalizeNoise(
            FMath::PerlinNoise3D(
                Direction *
                S *
                0.67f +
                Offset *
                1.37f
            )
        );

    const float LocalNoise =
        NormalizeNoise(
            FMath::PerlinNoise3D(
                Direction *
                S *
                1.15f +
                Offset *
                1.83f
            )
        );

    const float Field =
        MacroNoise *
        0.58f +
        RegionalNoise *
        0.30f +
        LocalNoise *
        0.12f;

    return SmoothMask(
        Field,
        0.28f,
        0.78f
    );
}


// ============================================================================
// HILL MASK
// ============================================================================

float UPlanetLandformGenerator::GetHillMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float S =
        SafeScale(Scale);

    const float HillA =
        GenerateHillRegion(
            Direction,
            S,
            Seed,
            0x1234ABCD5678EF01ULL,
            0.50f,
            0.18f
        );

    const float HillB =
        GenerateHillRegion(
            Direction,
            S * 0.82f,
            Seed,
            0x7B29D4E68153ACF0ULL,
            0.56f,
            0.20f
        );

    const float HillC =
        GenerateHillRegion(
            Direction,
            S * 1.18f,
            Seed,
            0xD8316FA249C75BE0ULL,
            0.53f,
            0.17f
        );

    return CombineRegions(
        HillA,
        HillB,
        HillC,
        0.0f
    );
}


// ============================================================================
// MOUNTAIN MASK
// ============================================================================

float UPlanetLandformGenerator::GetMountainMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float S =
        SafeScale(Scale);

    // ============================================================
    // QUATTRO REGIONI MONTUOSE INDIPENDENTI
    //
    // Hanno scale, seed e soglie diverse.
    // ============================================================

    const float MountainA =
        GenerateMountainRegion(
            Direction,
            S * 0.72f,
            Seed,
            0x1A73C59E42B8D601ULL,
            0.67f,
            0.20f
        );

    const float MountainB =
        GenerateMountainRegion(
            Direction,
            S * 0.91f,
            Seed,
            0xB46D218F7395CA02ULL,
            0.72f,
            0.18f
        );

    const float MountainC =
        GenerateMountainRegion(
            Direction,
            S * 1.13f,
            Seed,
            0x58E2A941C637BD03ULL,
            0.70f,
            0.21f
        );

    const float MountainD =
        GenerateMountainRegion(
            Direction,
            S * 0.57f,
            Seed,
            0xC9137F6A42DE8504ULL,
            0.75f,
            0.19f
        );

    return CombineRegions(
        MountainA,
        MountainB,
        MountainC,
        MountainD
    );
}


// ============================================================================
// MOUNTAIN CHAIN MASK
// ============================================================================

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

    // ============================================================
    // CHAIN A
    // ============================================================

    const FVector OffsetA =
        MakeSeedOffset(
            Seed,
            0xA1B2C3D4E5F60718ULL
        );

    const float WarpA =
        FMath::PerlinNoise3D(
            Direction *
            S *
            0.23f +
            OffsetA
        );

    const float WarpB =
        FMath::PerlinNoise3D(
            Direction *
            S *
            0.19f +
            OffsetA *
            1.73f
        );

    FVector ChainDirectionA =
        (
            Direction +
            FVector(
                WarpA,
                WarpB,
                -WarpA
            ) *
            0.30f
            ).GetSafeNormal();

    const float RidgeA =
        1.0f -
        FMath::Abs(
            FMath::PerlinNoise3D(
                ChainDirectionA *
                S *
                0.47f +
                OffsetA *
                1.31f
            )
        );

    const float ChainA =
        SmoothMask(
            RidgeA,
            0.64f,
            0.84f
        );


    // ============================================================
    // CHAIN B
    // ============================================================

    const FVector OffsetB =
        MakeSeedOffset(
            Seed,
            0x9182736455AABBCCULL
        );

    const float WarpC =
        FMath::PerlinNoise3D(
            Direction *
            S *
            0.17f +
            OffsetB
        );

    const float WarpD =
        FMath::PerlinNoise3D(
            Direction *
            S *
            0.27f +
            OffsetB *
            1.61f
        );

    FVector ChainDirectionB =
        (
            Direction +
            FVector(
                WarpC,
                -WarpD,
                WarpD
            ) *
            0.26f
            ).GetSafeNormal();

    const float RidgeB =
        1.0f -
        FMath::Abs(
            FMath::PerlinNoise3D(
                ChainDirectionB *
                S *
                0.61f +
                OffsetB *
                1.47f
            )
        );

    const float ChainB =
        SmoothMask(
            RidgeB,
            0.67f,
            0.86f
        );


    // ============================================================
    // CHAIN C
    // ============================================================

    const FVector OffsetC =
        MakeSeedOffset(
            Seed,
            0xD6E5F4A3B2918077ULL
        );

    const float WarpE =
        FMath::PerlinNoise3D(
            Direction *
            S *
            0.21f +
            OffsetC
        );

    FVector ChainDirectionC =
        (
            Direction +
            FVector(
                WarpE,
                WarpE * 0.65f,
                -WarpE * 0.85f
            ) *
            0.24f
            ).GetSafeNormal();

    const float RidgeC =
        1.0f -
        FMath::Abs(
            FMath::PerlinNoise3D(
                ChainDirectionC *
                S *
                0.54f +
                OffsetC *
                1.29f
            )
        );

    const float ChainC =
        SmoothMask(
            RidgeC,
            0.69f,
            0.87f
        );


    // ============================================================
    // COMBINAZIONE
    // ============================================================

    const float Combined =
        1.0f -
        (
            (1.0f - ChainA) *
            (1.0f - ChainB) *
            (1.0f - ChainC)
            );

    return FMath::Clamp(
        Combined,
        0.0f,
        1.0f
    );
}