#pragma once

#include "CoreMinimal.h"

/**
 * HILLAIRE ATMOSPHERE - CENTRAL LIMITS (Phase 1).
 *
 * Single point of configuration. Do NOT scatter these numbers through the
 * codebase: include this header and use HillaireLimits::*, never literals.
 *
 * The N <= 8 bound mirrors the golden DX11 reference (MAX_LIGHTS == 8,
 * cbuffer lightDirRadius[8]/lightColor[8]) and the UE5.8 decision to use
 * uniform arrays instead of structured buffers at this scale (spec section 11).
 */

// Preprocessor mirrors, required where a compile-time array bound is needed
// (e.g. SHADER_PARAMETER_ARRAY). They are static_asserted equal to the
// typed constants below: there is exactly ONE value, spelled twice.
#define HILLAIRE_MAX_ATMOSPHERE_LIGHTS 8
#define HILLAIRE_MAX_PLANETS 8

namespace HillaireLimits
{
	/** Max atmosphere light sources (uniform arrays, NOT structured buffers). */
	constexpr int32 MaxAtmosphereLights = 8;
	/** Max planetary atmospheres tracked by one world subsystem. */
	constexpr int32 MaxPlanets = 8;

	static_assert(MaxAtmosphereLights == HILLAIRE_MAX_ATMOSPHERE_LIGHTS,
		"Hillaire light bound spelled inconsistently; fix HillaireLimits.h only.");
	static_assert(MaxPlanets == HILLAIRE_MAX_PLANETS,
		"Hillaire planet bound spelled inconsistently; fix HillaireLimits.h only.");

	/**
	 * SkyView sun-cache threshold (ATMOS FIX VISIVO DEFINITIVO).
	 *
	 * The SkyView bake consumes the primary sun ONLY through its elevation
	 * above the planet-local camera up (dot(Up, sunDir); the bake rebuilds
	 * SunDir as (sqrt(1-c^2), 0, c), i.e. azimuth is reconstructed as zero
	 * by construction). The generation key therefore compares the cached
	 * sun ELEVATION cosine, not the raw 3D sun vector: a rigid planet spin
	 * rotates both the local sun and the local up identically (elevation
	 * invariant, baked content bit-identical), so spin alone must NOT
	 * regenerate. The old vector-angle threshold (cosAng < 0.9999995,
	 * ~0.001 rad) over-invalidated on every spin frame; combined with the
	 * pooled-target handoff that nulled the slot during regeneration, a
	 * spinning planet lost its sky every frame.
	 *
	 * Sensitivity: |dElevCos| > 1e-3 regenerates. At the horizon
	 * d(cos)/d(elev) = 1, so this matches the old ~0.001 rad worst case
	 * exactly where SkyView content changes fastest (sunrise colors); near
	 * the zenith the content is azimuth-symmetric and varies slowly, so the
	 * looser effective angle there is content-correct (sub-texel shifts).
	 */
	constexpr float SkyViewSunElevationCosDelta = 1e-3f;

	/**
	 * Planet selection angular threshold (reference: SelectPlanets call site,
	 * 0.0008 rad ~= ~1px at 720p). Planets below this are culled from rect passes.
	 */
	constexpr float PlanetRectMinAngularRad = 0.0008f;

	/** SkyView height-eps floor (reference: max(0.02, 1% * h), km). */
	constexpr float ViewHeightEpsFloorKm = 0.02f;
	constexpr float ViewHeightEpsRelative = 0.01f;

	/** Point-light resolve guard (reference: dist < 1e-4 km -> zero contribution). */
	constexpr float PointLightMinDistanceKm = 1e-4f;

	/** Limb + forward-scattering rect margin (reference: pad 1.05 in PlanetScreenRect). */
	constexpr float ScreenRectPad = 1.05f;

	/**
	 * Governing-planet hysteresis (ATMOS FIX VISIVO DEFINITIVO).
	 *
	 * The reference selection ("first containing atmosphere wins, else
	 * nearest surface") has no memory: with two atmospheres near the
	 * boundary, float32 center quantization flips the governing planet
	 * A -> B -> A across frames, and every flip swaps the whole sky state
	 * for a frame (visible shell jump). The incumbent keeps its slot while
	 * it still contains the camera, or while no challenger beats its signed
	 * surface distance by more than
	 * max(GoverningHysteresisFloorKm,
	 *     GoverningHysteresisRelative * IncumbentTopRadiusKm).
	 */
	constexpr float GoverningHysteresisFloorKm = 0.1f;
	constexpr float GoverningHysteresisRelative = 0.02f;

	// LUT dimensions (reference: LookUpTablesInfo + renderSky sizes).
	// Phase 2A decision (spec section 6.2 + task section 20): every LUT
	// resolution lives HERE. Shaders receive sizes as uniforms or derive
	// dispatch dims from the RDG descriptor; no magic numbers in .usf.
	constexpr int32 TransmittanceWidth = 256;
	constexpr int32 TransmittanceHeight = 64;
	constexpr int32 MultiScatteringSize = 32;
	constexpr int32 MultiScatteringRes = 32;
	constexpr int32 SkyViewWidth = 192;
	constexpr int32 SkyViewHeight = 108;
	constexpr int32 AerialVolumeSize = 32;

	// ---- Phase 2A: LUT generation march configuration ----
	// Verbatim from Resources/RenderSkyRayMarching.hlsl:
	// - RenderTransmittanceLutPS: SampleCountIni = 40, fixed uniform steps.
	// - NewMultiScattCS: SampleCountIni = 20, fixed steps, ground = true,
	//   MieRayPhase = false, 8x8 = 64 stratified sphere directions per texel.
	constexpr int32 TransmittanceMarchSamples = 40;
	constexpr int32 MultiScatteringMarchSamples = 20;
	constexpr int32 MultiScatteringSphereSamples = 64;
	constexpr int32 MultiScatteringSphereSqrt = 8;

	// Compute threading (documented choice, task section 16):
	// - Transmittance: per-texel [8x8x1], dispatch (W/8, H/8, 1) = (32, 8, 1).
	//   The reference is a fullscreen raster tri, but its math is strictly
	//   per-pixel (pixPos -> uv -> 40-step march, no interpolation, no
	//   derivatives, no blending): a per-texel compute invocation is the
	//   exact same computation, RDG-friendly (no view/viewport needed).
	// - MultiScattering: [1x1x64] verbatim (one thread group per texel, 64
	//   stratified directions reduced via groupshared memory), dispatch
	//   (32, 32, 1). Groupshared reduction maps 1:1 to SM5.
	constexpr int32 TransmittanceThreadGroupX = 8;
	constexpr int32 TransmittanceThreadGroupY = 8;

	// Planet radius offset (reference: PLANET_RADIUS_OFFSET in
	// RenderSkyCommon.hlsl, 0.01 km = 10 m). Keeps LUT bake queries off the
	// exact ground/top singularities.
	constexpr float PlanetRadiusOffsetKm = 0.01f;

	// ---- Phase 2B: SkyView march configuration ----
	// Reference SkyViewLutPS calls the integrator with SampleCountIni = 30
	// (dead: overwritten by the variable-count path), VariableSampleCount =
	// true and MieRayPhase = true. Defaults from Game.h:
	// uiViewRayMarchMinSPP = 4, uiViewRayMarchMaxSPP = 14.
	// Below 60 km rays: lerp(4, 14, saturate(tMax * 0.01)); above: the
	// shell-aware DistantMarch (16-256 steps) from the reference.
	constexpr float SkyViewMarchMinSamples = 4.0f;
	constexpr float SkyViewMarchMaxSamples = 14.0f;
	constexpr float SkyViewMarchDeadSampleCountIni = 30.0f; // reference-passed, overwritten; kept for traceability
	constexpr float VariableMarchDistantThresholdKm = 60.0f;

	// SkyView compute threading (same per-texel rationale as Transmittance:
	// the reference raster math is strictly per-pixel after the WorldDir
	// reconstruction, so compute is the exact same computation).
	constexpr int32 SkyViewThreadGroupX = 8;
	constexpr int32 SkyViewThreadGroupY = 8;

	// ---- Phase 2C: Aerial perspective (camera volume) configuration ----
	// Verbatim from Resources/RenderSkyRayMarching.hlsl RenderCameraVolumePS:
	// 32x32x32 froxel volume (AerialVolumeSize), squared slice distribution,
	// per-slice fixed march max(1, (sliceId+1)*2) = 2..64 steps, ground =
	// false, MieRayPhase = true, MS approximation ON (factor>0 permutation,
	// same rule as SkyView). The volume is VIEW-DEPENDENT (camera + view rays
	// enter per froxel): it is evaluated on demand into pooled scratch, never
	// cached as planet data.
	//
	// SCALE (corrected): the reference slice depth is 4 km over a 100 km
	// Earth envelope = 25 slices per envelope, with the composite near fade at
	// half a slice (2 km = 2% of the envelope). Those are EARTH-ABSOLUTE
	// distances; on Andromeda's sub-km envelopes they suppressed the
	// near-ground aerial almost entirely. The production slice depth is
	// therefore derived per planet from the profile envelope
	// (AerialKmPerSliceForEnvelope), which makes both the volume resolution
	// and the composite near fade reference-relative. Scattering, density,
	// LUTs and the atmosphere volume are untouched.
	constexpr float AerialSlicesPerEnvelope = 25.0f;
	/** Reference absolute slice (documentation only; production scales per planet). */
	constexpr float ReferenceAerialKmPerSlice = 4.0f;

	/** Per-planet aerial slice depth (km) from the atmosphere envelope. */
	inline float AerialKmPerSliceForEnvelope(float EnvelopeKm)
	{
		// 1 m guard (MinAtmosphereThicknessKm, declared below).
		return FMath::Max(EnvelopeKm, 1e-3f) / AerialSlicesPerEnvelope;
	}

	constexpr int32 AerialThreadGroupX = 4;
	constexpr int32 AerialThreadGroupY = 4;
	constexpr int32 AerialThreadGroupZ = 4;
	// Reference ground clamp-out lift: BottomRadius + PLANET_RADIUS_OFFSET +
	// 0.001f (RenderCameraVolumePS line 838). Named here, never a literal.
	constexpr float AerialGroundClampLiftKm = 0.001f;
	// Reference IntegrateScatteredLuminance default (line 27): upper bound for
	// tMaxMax. Passed explicitly by every caller; frozen for T/MS/SkyView so
	// their outputs stay bit-stable versus Phase 2A/2B.
	constexpr float IntegratorTMaxMaxKm = 9000000.0f;

	// ---- Phase 2D: Aerial composite ----
	// Sky/background detection on reversed-Z device depth. The far plane is
	// cleared to EXACTLY 0.0; any rasterized geometry has device depth
	// Near/distance > 0. The previous 1e-6 threshold classified everything
	// farther than ~100 km (Near 10 cm) as sky, so at planetary/orbital
	// distances the ATMOS sky (and the ZEPHYR overlay) composited over opaque
	// terrain and planets: glowing meshes, sky-painted disks, bloom. 1e-9
	// keeps only true background pixels as sky (geometry up to ~100,000 km is
	// opaque), which is the physically correct classification for this scale.
	// Shared by the GPU shaders and the CPU mirror (never a literal).
	constexpr float CompositeSkyDepthEpsilon = 1e-9f;
	// Composite threading: fullscreen tiles over the view rect.
	constexpr int32 CompositeThreadGroupX = 8;
	constexpr int32 CompositeThreadGroupY = 8;

	// Unit scale.
	constexpr double CmPerKm = 100000.0;
	constexpr double KmPerCm = 1e-5;

	/** MultipleScatteringFactor default (reference render knob, baked into MS LUT). */
	constexpr float DefaultMultipleScatteringFactor = 1.0f;

/**
	 * Reference atmosphere scale (documentation anchor, NOT a geometry knob).
	 *
	 * The validated reference profile (SetupEarthAtmosphere) is a 100 km
	 * envelope around a 6360 km ground sphere with an 8 km Rayleigh scale
	 * height (12.5 scale heights). The builder carries that model to any
	 * planet by normalizing the density to the planet's envelope
	 * (K = 100 / T): the reference density shape and the zenith optical depth
	 * are preserved on every size. The envelope itself is a stable planetary
	 * volume (self-similar minimum, raised by a stable terrain-containment
	 * floor); see HillaireBuildNormalizedProfile / HillaireBuildPlanetaryProfile.
	 */
	constexpr float ReferenceAtmosphereThicknessKm = 100.0f;
	/** Division guard for the density factor (1 m envelope floor). */
	constexpr float MinAtmosphereThicknessKm = 1e-3f;

	/**
	 * Planetary volumetric atmosphere geometry (ATMOS volumetric atmosphere).
	 * AtmosphereBottom = PlanetReferenceRadius
	 * AtmosphereTop = PlanetReferenceRadius * PlanetaryAtmosphereRadiusRatio (~1.10)
	 * AtmosphericThickness = PlanetReferenceRadius * PlanetaryAtmosphereThicknessRatio (0.10)
	 */
	constexpr float PlanetaryAtmosphereRadiusRatio = 1.10f;
	constexpr float PlanetaryAtmosphereThicknessRatio = 0.10f;

	/**
	 * Volumetric atmosphere scale-height distribution across the thickness T:
	 * H_Rayleigh = T / RayleighVolumeScaleHeights (~4.0).
	 * Rayleigh density decays smoothly to exp(-4.0) ~= 1.8% at top of atmosphere.
	 */
	constexpr float RayleighVolumeScaleHeights = 4.0f;

	/**
	 * Mie scale height H_Mie = T / MieVolumeScaleHeights (~12.0).
	 * Mie occupies lower ~25-30% of atmosphere (planetary haze/weather boundary layer).
	 */
	constexpr float MieVolumeScaleHeights = 12.0f;

	/**
	 * Stable volume headroom above the authored terrain bound, in SELF-SIMILAR
	 * Rayleigh scale heights (used only to size the containment floor).
	 */
	constexpr float AtmosphereHeadroomScaleHeights = 3.0f;

	/**
	 * Reference-derived sampler scale adaptation (CPU mirror of the HLSL
	 * constants in HillaireLutCore.ush).
	 *
	 * The reference sampler counts are Earth-calibrated in absolute km. With
	 * the envelope-normalized density (K = 100 / T) the fixed-bake ratio is
	 * reference-identical by construction (factor 1) for production planets;
	 * these bounds remain as a safety net for authoring with oversized
	 * envelopes, re-expressing the fixed bakes and the variable-march
	 * DistantMarch threshold in units of the SHORTER density scale height
	 * (Mie, 1.2 km on Earth). For the reference profile these equal the
	 * reference absolute values (50 * 1.2 = 60 km threshold; 83.33 * 1.2 =
	 * 100 km envelope), so Earth output is bit-identical.
	 */
	constexpr float DistantMarchScaleUnits = 50.0f;
	constexpr float ReferenceEnvelopeScaleUnits = 83.333333f;
	/**
	 * Fixed-bake envelope scaling bound: the T/MS/aerial bakes are profile-
	 * static (regen on profile change only), so they can afford a larger
	 * bound than the per-view SkyView bake. 16 covers the maximum practical
	 * containment ratio (terrain bound + headroom on the smallest planets)
	 * with a reference-relative step (~2 min density scales).
	 */
	constexpr float MaxEnvelopeSampleScale = 16.0f;
	/** Hard bound for the fixed bakes (T/MS/aerial): 512 steps max. */
	constexpr int32 IntegratorMaxSamples = 512;
	/**
	 * Variable-march (SkyView) hard bound. The long-ray count uses the same
	 * scale-relative formula but stays at the reference 256 cap: the per-view
	 * bake regens on camera-height/sun drift, so its cost must stay bounded.
	 * The near-loaded (quadratic) distribution used for rays that START
	 * inside the atmosphere resolves the dense ground layer within this cap.
	 */
	constexpr int32 VariableMarchMaxSamples = 256;
}
