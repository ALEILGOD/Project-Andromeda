#include "Planet/PlanetContinentalGenerator.h"

namespace
{
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

    float HashToUnitFloat(uint64 Hash)
    {
        const uint32 Value =
            static_cast<uint32>(
                Hash & 0xFFFFFFULL
                );

        return
            static_cast<float>(Value) /
            16777216.0f;
    }

    FVector MakeSeedOffset(int64 Seed)
    {
        const uint64 BaseSeed =
            static_cast<uint64>(Seed);

        const uint64 HashX =
            HashSeed(BaseSeed);

        const uint64 HashY =
            HashSeed(HashX);

        const uint64 HashZ =
            HashSeed(HashY);

        const float X =
            HashToUnitFloat(HashX) * 200.0f - 100.0f;

        const float Y =
            HashToUnitFloat(HashY) * 200.0f - 100.0f;

        const float Z =
            HashToUnitFloat(HashZ) * 200.0f - 100.0f;

        return FVector(X, Y, Z);
    }
}

float UPlanetContinentalGenerator::GetContinentalMask(
    FVector Direction,
    int64 Seed,
    float Scale
)
{
    Direction =
        Direction.GetSafeNormal();

    const float SafeScale =
        FMath::Max(
            Scale,
            0.0001f
        );

    const FVector SeedOffset =
        MakeSeedOffset(Seed);

    const float LargeNoise =
        FMath::PerlinNoise3D(
            Direction * SafeScale +
            SeedOffset
        );

    const float RegionalNoise =
        FMath::PerlinNoise3D(
            Direction * SafeScale * 2.15f +
            SeedOffset * 1.73f
        );

    const float CombinedNoise =
        LargeNoise * 0.78f +
        RegionalNoise * 0.22f;

    const float NormalizedNoise =
        (CombinedNoise + 1.0f) * 0.5f;

    const float ContinentalMask =
        FMath::SmoothStep(
            0.38f,
            0.62f,
            NormalizedNoise
        );

    return FMath::Clamp(
        ContinentalMask,
        0.0f,
        1.0f
    );
}