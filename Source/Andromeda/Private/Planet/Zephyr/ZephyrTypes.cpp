#include "Planet/Zephyr/ZephyrTypes.h"

// FNV-1a 64-bit over the raw field bytes. Any profile change
// invalidates the planet-dependent LUT cache (ZEPHYR-01 §18).
uint64 FZephyrPlanetProfile::ComputeProfileHash() const
{
    const uint64 FNVOffset = 14695981039346656037ULL;
    const uint64 FNVPrime = 1099511628211ULL;

    uint64 HashValue = FNVOffset;

    auto AccumulateFloat = [&](float Value)
    {
        uint32 Bits = 0;
        FMemory::Memcpy(&Bits, &Value, sizeof(float));
        for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
        {
            HashValue ^= static_cast<uint64>((Bits >> (ByteIndex * 8)) & 0xFFu);
            HashValue *= FNVPrime;
        }
    };

    auto AccumulateVector = [&](const FVector& Value)
    {
        AccumulateFloat(static_cast<float>(Value.X));
        AccumulateFloat(static_cast<float>(Value.Y));
        AccumulateFloat(static_cast<float>(Value.Z));
    };

    AccumulateFloat(GroundRadius);
    AccumulateFloat(AtmosphereRadius);
    AccumulateVector(RayleighScattering);
    AccumulateFloat(RayleighScaleHeight);
    AccumulateVector(MieScattering);
    AccumulateFloat(MieScaleHeight);
    AccumulateFloat(MieAnisotropy);
    AccumulateVector(AbsorptionCoefficients);
    AccumulateFloat(AbsorptionLayerHeight);
    AccumulateFloat(AbsorptionLayerWidth);
    AccumulateFloat(GroundAlbedo);
    AccumulateFloat(AtmosphericDensityScale);
    AccumulateFloat(MieDensityScale);

    return HashValue;
}
