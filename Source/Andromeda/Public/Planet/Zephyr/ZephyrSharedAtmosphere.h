#pragma once

#include "CoreMinimal.h"

// =========================================================
// ZEPHYR SHARED PHYSICAL ATMOSPHERE REFERENCE
// (ZEPHYR rebuild — ATMOS/ZEPHYR consistency layer)
// =========================================================
// Single deterministic source for the physical constants that
// BOTH renderers interpret:
//
//   ATMOS  = foreground atmospheric volume renderer
//   ZEPHYR = background planetary sky renderer
//
// The two systems stay SEPARATE (no shared rendering code, no
// merged pipeline). What they share is ONLY this reference:
// Earth-baseline cross-sections, scale heights, unit
// conventions and the sky-transition definition. Every value
// below is traceable to published references:
//
//   - Rayleigh sea-level coefficients (km^-1):
//     (0.0058, 0.0135, 0.0331) — Bruneton/Neyret and the
//     Hillaire 2020 family; community path-tracer cross-checks
//     use (5.802, 13.558, 33.100)e-6 m^-1, identical after the
//     m^-1 -> km^-1 conversion (x1000).
//   - Mie (aerosol) baseline 0.004 km^-1 ~= 3.996e-6 m^-1,
//     Henyey-Greenstein g = 0.76, Mie scale height 1.2 km.
//   - Ozone-like (Chappuis-band) absorption peak
//     (0.00065, 0.001881, 0.000085) km^-1 ~=
//     (0.650, 1.881, 0.085)e-6 m^-1, layer center 25 km,
//     width 15 km (Hillaire 2020 class values).
//   - Rayleigh scale height 8 km.
//
// COHERENCE TABLE (explicit, deterministic):
//   ATMOS defaults (AndromedaAtmosphereTypes.h):
//     Rayleigh (0.0058, 0.0135, 0.0331) == shared reference.
//     NOTE: ATMOS evaluates Rayleigh transport only (no Mie /
//     absorption terms exist in AndromedaAtmosphere.usf); its
//     phase convention folds the 3/(16*pi) normalization into
//     the display exposure instead of the phase function, while
//     ZEPHYR uses physically normalized phases with
//     ZEPHYR_RADIANCE_SCALE. Same cross-sections, documented
//     convention difference — never a second set of colors.
//   ZEPHYR baseline (ZephyrProfileLibrary.cpp):
//     built FROM these constants (see ZephyrEarth* helpers), so
//     a Terran planet at density 1.0 carries the same molecular
//     column in both systems by construction.
//   UE 5.8 SkyAtmosphere (reference only, never instantiated):
//     same physical quantities (ground radius, atmosphere
//     height, Rayleigh/Mie exponential distributions,
//     mu/mu_s/nu geometry). USkyAtmosphereComponent is NOT used.
//
// UNITS: centimeters for all world lengths (Unreal units:
// 1 km = 100000 cm), km^-1 for cross-sections. Conversion is
// always explicit (CmToKm = 1e-5), never mixed.
//
// DETERMINISM: everything here is constexpr / pure functions of
// explicit inputs (seed, profile, geometry). No RNG, no frame
// state, no execution-order dependence.
// =========================================================

namespace AndromedaAtmosphereReference
{
    // Unreal length convention: 1 km = 100000 cm.
    constexpr float CmPerKm = 100000.0f;
    constexpr double CmPerKmDouble = 100000.0;

    // Explicit cm -> km conversion (single convention).
    constexpr float CmToKm = 1e-5f;

    // Sky visual-transition height above the maximum terrain:
    // ~2 km = 200000 cm (NOT 2000 cm, which would be 20 m).
    constexpr float SkyTransitionAboveTerrainCm = 200000.0f;

    // ---- Earth molecular baseline (km^-1, RGB) ----
    constexpr float EarthRayleighX = 0.0058f;
    constexpr float EarthRayleighY = 0.0135f;
    constexpr float EarthRayleighZ = 0.0331f;

    // ---- Earth scale heights (cm) ----
    constexpr float EarthRayleighScaleHeightCm = 800000.0f; // 8 km
    constexpr float EarthMieScaleHeightCm = 120000.0f;       // 1.2 km

    // ---- Earth aerosol baseline (km^-1, neutral) ----
    constexpr float EarthMie = 0.004f;
    constexpr float EarthMieAnisotropy = 0.76f;

    // ---- Ozone-like absorption peak (km^-1, RGB) ----
    constexpr float EarthAbsorptionX = 0.00065f;
    constexpr float EarthAbsorptionY = 0.001881f;
    constexpr float EarthAbsorptionZ = 0.000085f;
    constexpr float EarthAbsorptionCenterCm = 2500000.0f; // 25 km
    constexpr float EarthAbsorptionWidthCm = 1500000.0f;  // 15 km

    constexpr float EarthGroundAlbedo = 0.3f;

    // Sky visual-transition radius (cm, world length):
    //   PlanetRadius + MaximumTerrainHeight + ~2 km.
    // Kept SEPARATE from the physical AtmosphereRadius =
    // (PlanetRadius + TerrainHeight) * AtmosphereRadiusMultiplier
    // (read from the registry at runtime, never hardcoded here).
    // The sky may become visually fully atmospheric by Rs without
    // truncating the physical shell at Rs.
    inline float ComputeSkyTransitionRadiusCm(
        float PlanetRadiusCm,
        float MaxTerrainHeightCm)
    {
        return FMath::Max(PlanetRadiusCm, 1.0f)
            + FMath::Max(MaxTerrainHeightCm, 0.0f)
            + SkyTransitionAboveTerrainCm;
    }

    // Transition COMPLETION factor (mandated rule, single definition):
    // the camera distance at/below which the planetary atmospheric
    // regime is 100% active. Deliberately SEPARATE from both the
    // physical shell Rt = (Rg + terrain) * AtmosphereRadiusMultiplier
    // (tunable UPROPERTY, drives ray-march/bake bounds) and the Rs
    // blend-start above: pinning completion to live runtime data
    // keeps the visual regime correct even if the multiplier is tuned.
    constexpr float TransitionCompletionFactor = 1.1f;

    // Transition completion radius (cm, world length, per planet):
    //   (PlanetRadius + TerrainHeight) * 1.1
    // Same unit (cm) as every camera distance it is compared with.
    // TerrainHeight is the live FPlanetRuntimeData value (never a
    // second source, never hardcoded, planet geometry untouched).
    inline float ComputeTransitionCompleteRadiusCm(
        float PlanetRadiusCm,
        float TerrainHeightCm)
    {
        return (FMath::Max(PlanetRadiusCm, 1.0f)
            + FMath::Max(TerrainHeightCm, 0.0f))
            * TransitionCompletionFactor;
    }
} // namespace AndromedaAtmosphereReference
