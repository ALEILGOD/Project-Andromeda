#pragma once

#include "CoreMinimal.h"
#include "ZephyrTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAndromedaZephyr, Log, All);

// =========================================================
// ZEPHYR-01 — TRUE PLANETARY SKY RENDERING
// =========================================================
// ZEPHYR produces background sky radiance from the physical
// atmosphere of each planet. It is strictly separated from
// ATMOS:
//
//     CAMERA -> ZEPHYR (background sky radiance)
//            -> ATMOS  (foreground atmospheric volume)
//            -> PLANET
//
// Technologies used (documented at each algorithm site):
//   - Bruneton / Neyret precomputed atmospheric scattering
//     (transmittance, single + multiple scattering, sky radiance,
//     optical depth). "Precomputed Atmospheric Scattering"
//     (Bruneton, Neyret - EGSR 2008).
//   - Rayleigh scattering (molecular, wavelength-dependent,
//     exponential density profile, 1+cos^2 phase).
//   - Mie scattering (aerosols, exponential profile,
//     Henyey-Greenstein phase function).
//   - Wavelength-dependent absorption (ozone-like Chappuis layer).
//   - Hillaire-class LUT architecture ("A Scalable and Production
//     Ready Sky and Atmosphere Rendering Technique", Hillaire 2020):
//     Transmittance LUT + Multi-Scattering LUT + Sky-View LUT.
// =========================================================

// =========================================================
// ZEPHYR DEBUG / DIAGNOSTIC MODES
// =========================================================
// Drives r.AndromedaZephyr.DebugMode. Proves the visible pixel
// comes from ZEPHYR and isolates each physical contributor.
UENUM(BlueprintType)
enum class EZephyrDebugMode : uint8
{
    // Full sky radiance (single + multiple + sun). Default.
    SkyOnly         UMETA(DisplayName = "ZEPHYR_SKY_ONLY"),
    // Raw Transmittance LUT slice (height vs sun zenith).
    Transmittance   UMETA(DisplayName = "ZEPHYR_TRANSMITTANCE"),
    // Single-scattering sky component only.
    SingleScatter   UMETA(DisplayName = "ZEPHYR_SINGLE_SCATTER"),
    // Multiple-scattering sky component only.
    MultiScatter    UMETA(DisplayName = "ZEPHYR_MULTI_SCATTER"),
    // Henyey-Greenstein Mie lobe x transmittance (diagnostic).
    Mie             UMETA(DisplayName = "ZEPHYR_MIE"),
    // Rayleigh phase x transmittance (diagnostic).
    Rayleigh        UMETA(DisplayName = "ZEPHYR_RAYLEIGH"),
    // Absorption loss along the sun path (diagnostic).
    Absorption      UMETA(DisplayName = "ZEPHYR_ABSORPTION"),
    // Raw Sky-View LUT (single scattering) fullscreen.
    SkyViewLUT      UMETA(DisplayName = "ZEPHYR_SKY_VIEW_LUT"),
    // Raw Multi-Scattering LUT slice (height vs sun zenith).
    MultiScatterLUT UMETA(DisplayName = "ZEPHYR_MULTI_SCATTER_LUT")
};

// =========================================================
// ZEPHYR PLANET ATMOSPHERE PROFILE
// =========================================================
// Physical atmosphere description of ONE planet, consumed by
// the ZEPHYR LUT generators. Variation between planets happens
// exclusively through these physical parameters (scattering
// coefficients, scale heights, absorption, albedo, geometry) -
// never through artistic sky/horizon/sunset colors.
//
// UNITS (explicit, never mixed):
//   - All distances/lengths STORED in centimeters (Unreal units).
//   - Scattering/absorption coefficients STORED in km^-1
//     (spectroscopy convention, comparable with published values
//     such as Earth's Rayleigh 5.8/13.5/33.1 e-3 km^-1).
//   - The GPU/shader layer converts march step lengths from cm
//     to km EXPLICITLY (CentimetersToKilometers = 1e-5) before
//     multiplying by coefficients. See ZephyrCommon.ush.
USTRUCT(BlueprintType)
struct FZephyrPlanetProfile
{
    GENERATED_BODY()

    // Ground (surface) radius, cm.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Geometry")
    float GroundRadius = 500000.0f;

    // Outer atmosphere radius, cm. Must be > GroundRadius.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Geometry")
    float AtmosphereRadius = 650000.0f;

    // Rayleigh (molecular) scattering coefficient, km^-1, RGB.
    // Earth reference: (0.0058, 0.0135, 0.0331).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Rayleigh")
    FVector RayleighScattering = FVector(0.0058f, 0.0135f, 0.0331f);

    // Rayleigh density scale height, cm (Earth ~800000 = 8 km).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Rayleigh")
    float RayleighScaleHeight = 800000.0f;

    // Mie (aerosol) scattering coefficient, km^-1, RGB.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Mie")
    FVector MieScattering = FVector(0.003f, 0.003f, 0.003f);

    // Mie density scale height, cm (Earth ~120000 = 1.2 km).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Mie")
    float MieScaleHeight = 120000.0f;

    // Mie phase anisotropy g in (-1, 1). Forward lobe ~0.76.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Mie")
    float MieAnisotropy = 0.76f;

    // Ozone-like absorption coefficient (peak of layer), km^-1.
    // Representative Chappuis-band values (Hillaire 2020 class):
    // (0.00065, 0.001881, 0.000085).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Absorption")
    FVector AbsorptionCoefficients = FVector(0.00065f, 0.001881f, 0.000085f);

    // Center altitude of the absorption layer above ground, cm.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Absorption")
    float AbsorptionLayerHeight = 2500000.0f;

    // 1-sigma half-width of the absorption layer, cm.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Absorption")
    float AbsorptionLayerWidth = 1500000.0f;

    // Lambertian ground albedo (single-bounce contribution to the
    // multi-scattering gather). 0 = black rock, 1 = white.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Ground")
    float GroundAlbedo = 0.3f;

    // Global density multiplier (pressure-like). Scales the
    // MOLECULAR density profiles (Rayleigh + absorption)
    // equally, preserving spectral ratios.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Density")
    float AtmosphericDensityScale = 1.0f;

    // Aerosol (Mie) density multiplier. Kept SEPARATE from the
    // pressure-like scale above: aerosol surface loading is set
    // by dust injection (archetype + seed jitter, baked into the
    // Mie cross-section), not by surface pressure. Folding the
    // toy-planet column compensation into Mie as well would
    // multiply the archetype dust load by the pressure factor
    // (e.g. Desert 4.5x x ~15x pressure = ~68x baseline dust),
    // whiting out the day sky through the forward lobe and
    // washing every sunset. The same factor feeds scattering
    // AND extinction, so energy consistency is preserved.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Density")
    float MieDensityScale = 1.0f;

    bool IsValidConfiguration() const
    {
        return GroundRadius > 0.0f
            && AtmosphereRadius > GroundRadius
            && RayleighScaleHeight > 0.0f
            && MieScaleHeight > 0.0f
            && FMath::Abs(MieAnisotropy) < 1.0f
            && AtmosphericDensityScale >= 0.0f
            && MieDensityScale >= 0.0f
            && AbsorptionLayerWidth > 0.0f
            && GroundAlbedo >= 0.0f
            && GroundAlbedo <= 1.0f;
    }

    // FNV-1a hash of every field that changes the LUT contents.
    // Used for planet-dependent cache invalidation (ZEPHYR-01 §18).
    uint64 ComputeProfileHash() const;
};

// =========================================================
// ZEPHYR PLANET SNAPSHOT ENTRY (game thread -> render thread)
// =========================================================
// One entry per planet: its physical profile plus its current
// world-space center. Published by the atmosphere registry on
// the game thread, consumed by the ZEPHYR renderer on the
// render thread. No UObjects cross the thread boundary.
USTRUCT(BlueprintType)
struct FZephyrPlanetSnapshotEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    FZephyrPlanetProfile Profile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    FVector PlanetCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    int64 PlanetID = 0;

    // Maximum terrain relief above the planet datum (cm). Feeds
    // the sky visual-transition radius below; published by the
    // registry from the star-system runtime data (same source
    // that sizes the ATMOS volumes).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    float TerrainHeightCm = 0.0f;

    // Sky visual-transition radius (cm, world length):
    //   PlanetRadius + MaximumTerrainHeight + ~2 km (200000 cm).
    // See AndromedaAtmosphereReference::ComputeSkyTransitionRadiusCm.
    // Deliberately DISTINCT from Profile.AtmosphereRadius (the
    // physical shell from AtmosphereRadiusMultiplier): the sky may
    // become visually fully atmospheric by Rs without truncating
    // the physical atmosphere at Rs. Rs is the OUTER blend start;
    // completion is TransitionCompleteRadiusCm below (never deleted:
    // other diagnostics may still read Rs).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    float SkyTransitionRadiusCm = 0.0f;

    // Transition COMPLETION radius (cm, world length, per planet):
    //   (PlanetRadius + TerrainHeight) * 1.1
    // See AndromedaAtmosphereReference::ComputeTransitionCompleteRadiusCm.
    // At/below this camera distance the planetary atmospheric regime
    // is 100% active; outside it blends monotonely toward space up to
    // the outer edge. Separate semantics from the physical shell Rt
    // (tunable multiplier, ray-march bound, untouched) and from Rs
    // (outer blend start). Live runtime data, same cm unit as the
    // camera distances it is compared with.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    float TransitionCompleteRadiusCm = 0.0f;

    // Planet rotation (world frame). Updated every frame from
    // the StarSystem runtime data (FPlanetRuntimeData.CurrentRotation).
    // NOTE (SUN LIGHT REFERENCE): the rotation is NOT applied to the
    // sun direction. The atmosphere renders a Hillaire Case A
    // directional sun over world-frame camera/geometry, so SunDir
    // stays world-frame end to end. Rotation still drives the terrain
    // mesh and is reported for diagnostics. See SunDirectionWorld.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    FRotator PlanetRotation = FRotator::ZeroRotator;

    // SUN LIGHT REFERENCE (source of truth, per planet, world frame).
    // Convention A: Planet -> Sun (direction TOWARD the source).
    // Baked on the game thread by AAndromedaAtmosphereRegistry from
    // ASun::AtmosphereLightReference
    // (UAtmosphereLightReferenceComponent::GetDirectionTowardSunWorld).
    // The render thread NEVER recomputes this and NEVER rotates it:
    // BuildGPUData copies it verbatim into Packed6. Normalized and
    // validated at bake time; per-planet (parallax preserved).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr")
    FVector SunDirectionWorld = FVector::ZeroVector;
};

// =========================================================
// ZEPHYR GPU PLANET DATA
// =========================================================
// Packed per-planet data for the ZEPHYR LUT compute shaders and
// the sky pixel shader.
//
// LAYOUT CONTRACT: must match FZephyrPlanetGPUData in
// ZephyrCommon.ush EXACTLY (7 x float4 = 112 bytes).
//
// Positions are camera-relative (PlanetCenter - CameraPosition,
// computed in FVector/double precision on the CPU then narrowed
// to float), following the ATMOS-04 numerical stabilization
// convention. Radii stay in cm; scale heights are pre-converted
// to km on the CPU so the shader never mixes units.
//
// Packed0: RelCenter.xyz (cm, camera-relative), GroundRadius (cm)
// Packed1: AtmoRadius (cm), RayleighScaleH (km), MieScaleH (km), MieAnisotropy
// Packed2: Rayleigh sigma rgb (km^-1), DensityScale (molecular: Rayleigh + absorption)
// Packed3: Mie sigma rgb (km^-1), GroundAlbedo
// Packed4: Absorption peak sigma rgb (km^-1), AbsLayerCenter (km above ground)
// Packed5: AbsLayerWidth (km), ViewHeightKm (per-view), MieDensityScale, SkyTransitionRadiusCm (outer blend start)
// Packed6: SunDirWorld.xyz (unit vector, Planet->Sun, world frame), TransitionCompleteRadiusCm (inner blend edge, 100% regime)
struct FZephyrPlanetGPUData
{
    // Camera-relative planet center (cm) + ground radius (cm).
    float CenterX = 0.0f;
    float CenterY = 0.0f;
    float CenterZ = 0.0f;
    float GroundRadiusCm = 0.0f;

    // Outer atmosphere radius (cm), scale heights (km), Mie g.
    float AtmosphereRadiusCm = 0.0f;
    float RayleighScaleKm = 8.0f;
    float MieScaleKm = 1.2f;
    float MieAnisotropy = 0.76f;

    // Rayleigh sigma (km^-1) + global density scale.
    float RayleighX = 0.0058f;
    float RayleighY = 0.0135f;
    float RayleighZ = 0.0331f;
    float DensityScale = 1.0f;

    // Mie sigma (km^-1) + ground albedo.
    float MieX = 0.003f;
    float MieY = 0.003f;
    float MieZ = 0.003f;
    float GroundAlbedo = 0.3f;

    // Absorption peak sigma (km^-1) + layer center (km above ground).
    float AbsorbX = 0.00065f;
    float AbsorbY = 0.001881f;
    float AbsorbZ = 0.000085f;
    float AbsorbCenterKm = 25.0f;

    // Absorption layer width (km) + camera height above ground (km,
    // per-view, may exceed the shell when in space) + aerosol
    // density scale (decoupled from pressure, see profile) +
    // sky visual-transition radius (cm, world length:
    // PlanetRadius + MaxTerrainHeight + ~2 km, shared reference).
    float AbsorbWidthKm = 15.0f;
    float ViewHeightKm = 0.0f;
    float MieDensityScale = 1.0f;
    float SkyTransitionRadiusCm = 0.0f;

    // SUN LIGHT REFERENCE (world frame, convention A: Planet -> Sun).
    // Carries FZephyrPlanetSnapshotEntry::SunDirectionWorld verbatim
    // (baked from ASun::AtmosphereLightReference, never rotated:
    // Up/RayDir in the shaders are world-frame, so SunDir must be
    // world-frame too). Stored per-planet because each planet has a
    // different planet-center-relative sun vector (parallax preserved
    // for multi-planet). Legacy field name retained to avoid touching
    // the Hillaire shader math that reads Packed6.
    float SunDirPlanetX = 0.0f;
    float SunDirPlanetY = 0.0f;
    float SunDirPlanetZ = 1.0f;
    // Transition completion radius (cm, world length, per planet):
    // (PlanetRadius + TerrainHeight) * 1.1, from the snapshot field.
    // Reuses the previously unread alignment slot: total size stays
    // 7 x float4 = 112 bytes, layout contract with ZephyrCommon.ush
    // unchanged (Packed6.w). Inner edge of the transition fade.
    float TransitionCompleteRadiusCm = 0.0f;
};
