#include "Planet/Zephyr/ZephyrProfileLibrary.h"
#include "Planet/Zephyr/ZephyrSharedAtmosphere.h"

namespace
{
    // SplitMix64-style deterministic hash (same family as the
    // PlanetProfile generator). Maps (Seed, Salt) -> uint64.
    uint64 ZephyrHashSeed64(uint64 Seed, uint64 Salt)
    {
        Seed += Salt;
        Seed = (Seed ^ (Seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        Seed = (Seed ^ (Seed >> 27)) * 0x94D049BB133111EBULL;
        return Seed ^ (Seed >> 31);
    }

    float ZephyrHashToUnitFloat(uint64 HashValue)
    {
        const uint32 Value = static_cast<uint32>(HashValue & 0xFFFFFFULL);
        return static_cast<float>(Value) / 16777216.0f;
    }

    // Deterministic per-planet jitter in [1 - Amount, 1 + Amount].
    float ZephyrJitter(int64 PlanetSeed, uint64 Salt, float Amount)
    {
        const uint64 H = ZephyrHashSeed64(
            static_cast<uint64>(PlanetSeed < 0 ? -PlanetSeed : PlanetSeed) + 0x9E3779B97F4A7C15ULL,
            Salt
        );
        const float Unit = ZephyrHashToUnitFloat(H);
        return 1.0f + (Unit * 2.0f - 1.0f) * Amount;
    }
}

FZephyrPlanetProfile UZephyrProfileLibrary::BuildProfile(
    int64 PlanetSeed,
    EPlanetArchetype Archetype,
    float SurfaceRadiusCm,
    float AtmosphereRadiusCm
)
{
    FZephyrPlanetProfile Profile;

    // Geometry comes from the planet / ATMOS registry (cm).
    Profile.GroundRadius =
        FMath::Max(SurfaceRadiusCm, 1.0f);
    Profile.AtmosphereRadius =
        FMath::Max(AtmosphereRadiusCm, Profile.GroundRadius + 1.0f);

    // --------------------------------------------------------
    // Earth-like physical baseline, built FROM the shared
    // ATMOS/ZEPHYR reference (same cross-sections both systems
    // interpret: chromatic coherence by construction, never by
    // color grading). Rayleigh reference values are sea-level
    // scattering coefficients in km^-1.
    // --------------------------------------------------------
    using namespace AndromedaAtmosphereReference;
    FVector Rayleigh = FVector(EarthRayleighX, EarthRayleighY, EarthRayleighZ);
    float RayleighH = EarthRayleighScaleHeightCm; // 8 km, cm
    FVector Mie = FVector(EarthMie, EarthMie, EarthMie);
    float MieH = EarthMieScaleHeightCm;            // 1.2 km, cm
    float Anisotropy = EarthMieAnisotropy;
    FVector Absorption = FVector(EarthAbsorptionX, EarthAbsorptionY, EarthAbsorptionZ);
    float AbsH = EarthAbsorptionCenterCm;          // 25 km, cm
    float AbsW = EarthAbsorptionWidthCm;           // 15 km, cm
    float Albedo = EarthGroundAlbedo;
    float Density = 1.0f;

    // --------------------------------------------------------
    // Archetype presets: PHYSICAL tendencies, not colors.
    // A denser aerosol load brightens the horizon, strengthens
    // the Mie forward lobe (sun halo) and reddens sunsets;
    // a thinner column darkens the zenith and shortens the
    // transition to space; stronger blue-absorbing species shift
    // the whole spectral balance. All of that emerges from the
    // transfer integral, it is never painted.
    // --------------------------------------------------------
    switch (Archetype)
    {
    case EPlanetArchetype::Terran:
        break;
    case EPlanetArchetype::Oceanic:
        Mie *= 1.6f; Albedo = 0.15f; Density = 1.1f;
        break;
    case EPlanetArchetype::Jungle:
        Mie *= 2.0f; Density = 1.15f; Albedo = 0.2f;
        break;
    case EPlanetArchetype::Arid:
        Mie *= 3.0f; MieH *= 1.4f; Albedo = 0.45f; Density = 1.05f;
        break;
    case EPlanetArchetype::Desert:
        Mie *= 4.5f; MieH *= 1.6f; Albedo = 0.55f; Density = 1.1f;
        Absorption *= 1.4f;
        break;
    case EPlanetArchetype::Frozen:
        Rayleigh *= 0.9f; Mie *= 0.5f; Density = 0.4f;
        RayleighH *= 0.8f; Albedo = 0.6f;
        break;
    case EPlanetArchetype::Tundra:
        Mie *= 0.7f; Density = 0.7f; Albedo = 0.5f;
        break;
    case EPlanetArchetype::Rocky:
        Mie *= 0.6f; Density = 0.55f; Albedo = 0.25f;
        break;
    case EPlanetArchetype::Volcanic:
        // Thick sulfurous haze: heavy neutral Mie load plus
        // strongly enhanced short-wave absorption.
        Mie *= 6.0f; MieH *= 1.8f;
        Absorption *= 3.0f; Albedo = 0.08f; Density = 1.6f;
        break;
    case EPlanetArchetype::Exotic:
        // Alien gas mix: relatively weaker blue molecular
        // scattering, dominant aerosols, strong blue-absorbing
        // species -> unfamiliar zenith/sunset balance.
        Rayleigh = FVector(0.0090f, 0.0105f, 0.0140f);
        Mie *= 2.5f;
        Absorption = FVector(0.0004f, 0.0012f, 0.0045f);
        Albedo = 0.35f; Density = 1.25f;
        Anisotropy = 0.65f;
        break;
    default:
        break;
    }

    // --------------------------------------------------------
    // Deterministic per-planet jitter (±15-20%) from the planet
    // seed. Same seed -> same atmosphere, every run, every
    // machine. Still strictly physical: only cross-sections,
    // scale heights, albedo and density move.
    // --------------------------------------------------------
    Rayleigh *= ZephyrJitter(PlanetSeed, 11, 0.15f);
    Mie *= ZephyrJitter(PlanetSeed, 23, 0.20f);
    RayleighH *= ZephyrJitter(PlanetSeed, 37, 0.15f);
    MieH *= ZephyrJitter(PlanetSeed, 41, 0.15f);
    Absorption *= ZephyrJitter(PlanetSeed, 53, 0.20f);
    Density *= ZephyrJitter(PlanetSeed, 67, 0.15f);
    Anisotropy = FMath::Clamp(
        Anisotropy * ZephyrJitter(PlanetSeed, 71, 0.05f), -0.9f, 0.9f);
    Albedo = FMath::Clamp(
        Albedo * ZephyrJitter(PlanetSeed, 83, 0.20f), 0.02f, 0.9f);

    Profile.RayleighScattering = Rayleigh;
    Profile.RayleighScaleHeight = FMath::Max(RayleighH, 1.0f);
    Profile.MieScattering = Mie;
    Profile.MieScaleHeight = FMath::Max(MieH, 1.0f);
    Profile.MieAnisotropy = Anisotropy;
    Profile.AbsorptionCoefficients = Absorption;
    Profile.AbsorptionLayerHeight = FMath::Max(AbsH, 1.0f);
    Profile.AbsorptionLayerWidth = FMath::Max(AbsW, 1.0f);
    Profile.GroundAlbedo = Albedo;

    // --------------------------------------------------------
    // TOY-PLANET COLUMN COMPENSATION (surface pressure).
    //
    // Physical motivation: the baked sky radiance scales with
    // the vertical optical column (~ sigma x scale height x
    // surface density). Reference Earth-like column uses the
    // 8 km Rayleigh scale height. On toy planets the shell is
    // far thinner than the scale heights, and the shader binds
    // the effective scale to the shell (see
    // ZephyrEffectiveScaleKm), so an uncompensated profile
    // yields columns ~15-60x thinner than Earth - a physically
    // correct but invisible (near-black) sky.
    //
    // Compensation restores an Earth-like column for the
    // baseline (Terran) profile by raising the SURFACE DENSITY
    // (pressure), never by painting colors: all hues still
    // emerge from cross-section ratios + transport. Archetype
    // multipliers and seed jitter applied above are preserved
    // on top, so Frozen stays thinner than Terran, Volcanic
    // stays thicker, etc. For true Earth-scale shells the
    // factor self-disables to 1 (no-op).
    // --------------------------------------------------------
    {
        const float ShellCm =
            Profile.AtmosphereRadius - Profile.GroundRadius;
        const float EffectiveScaleCm = FMath::Min(
            FMath::Max(RayleighH, 1.0f),
            FMath::Max(ShellCm, 1.0f) * 0.35f);
        const float ColumnCompensation = FMath::Clamp(
            EarthRayleighScaleHeightCm / FMath::Max(EffectiveScaleCm, 1.0f),
            1.0f,
            60.0f);

        Density *= ColumnCompensation;
    }

    Profile.AtmosphericDensityScale = FMath::Max(Density, 0.0f);

    // Aerosol loading stays at its archetype/seed value: the
    // column compensation above restores an Earth-like MOLECULAR
    // column (surface pressure) on toy shells. Aerosol surface
    // loading does not scale with pressure (dust injection is
    // independent of it), so Mie keeps factor 1: Desert stays
    // dustier than Terran through its cross-section ratio, but
    // no longer compounds it with the pressure factor (which
    // produced ~28x Earth Mie columns and whited out the day
    // sky through the HG forward lobe). Rayleigh, absorption
    // and Mie keep sharing one consistent density each in
    // scattering and extinction (see ZephyrCommon.ush).
    Profile.MieDensityScale = 1.0f;

    return Profile;
}
