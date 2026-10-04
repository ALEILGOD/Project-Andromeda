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

	// ---- Aerial inscatter presentation scale (sky/terrain separation) ----
	// The aerial composite is physically correct, but it shares the sky's
	// SunScale = 30. On thin 1.10x envelopes the normalized density makes
	// short near-surface paths visibly blue at x30. This isolated scale
	// applies ONLY to the aerial inscatter term (never to transmittance,
	// never to the sky, never to LUTs). Must match HILLAIRE_AERIAL_INSCATTER_SCALE.
	// CALIBRATION PASS 2: this is now the NEAR endpoint of the distance ramp
	// (AerialDistanceScale): the validated near-terrain value, unchanged.
	constexpr float AerialInscatterPresentationScale = 0.35f;
	/** Far endpoint of the aerial distance ramp: full physical in-scatter. Must match HILLAIRE_AERIAL_FAR_SCALE. */
	constexpr float AerialFarPresentationScale = 1.0f;
	/**
	 * Beyond-range haze asymptote (w > 1 easing 1.0 -> asymptote over w in
	 * [1, 2]). Must match HILLAIRE_AERIAL_BEYOND_ASYMPTOTE.
	 */
	constexpr float AerialBeyondAsymptote = 0.55f;
	/** Knee of the distance ramp (near value holds at/below, full at w = 1). Must match HILLAIRE_AERIAL_RAMP_KNEE_W. */
	constexpr float AerialRampKneeW = 0.70f;
	/** High-altitude endpoint of the entry scale. Must match HILLAIRE_AERIAL_ENTRY_MIN_SCALE. */
	constexpr float AerialEntryMinScale = 0.25f;

	/** CPU mirror of HillaireAerialDistanceScale in HillaireCommon.ush. */
	inline float AerialDistanceScale(float SliceW)
	{
		const float Knee = FMath::Clamp((SliceW - AerialRampKneeW) / (1.0f - AerialRampKneeW), 0.0f, 1.0f);
		const float S = Knee * Knee * (3.0f - 2.0f * Knee);
		const float NearFar = FMath::Lerp(AerialInscatterPresentationScale, AerialFarPresentationScale, S);
		const float B = FMath::Clamp(SliceW - 1.0f, 0.0f, 1.0f);
		const float BS = B * B * (3.0f - 2.0f * B);
		return NearFar * FMath::Lerp(1.0f, AerialBeyondAsymptote, BS);
	}

	/** CPU mirror of HillaireAerialAltitudeScale in HillaireCommon.ush (0 = surface, 1 = top). */
	inline float AerialAltitudeScale(float Altitude01)
	{
		const float T = FMath::Clamp(Altitude01, 0.0f, 1.0f);
		const float S = T * T * (3.0f - 2.0f * T);
		return FMath::Lerp(1.0f, AerialEntryMinScale, S);
	}

	/** Boundary fade width in envelope fraction (top 1%). Must match HILLAIRE_AERIAL_BOUNDARY_FADE_WIDTH. */
	constexpr float AerialBoundaryFadeWidth = 0.01f;

	/** CPU mirror of HillaireAerialBoundaryFade in HillaireCommon.ush. */
	inline float AerialBoundaryFade(float Altitude01)
	{
		const float T = FMath::Clamp((1.0f - Altitude01) / AerialBoundaryFadeWidth, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** HLSL smoothstep mirror (HillaireSunsetBand needs the exact edge semantics). */
	inline float SunsetSmoothstep(float Edge0, float Edge1, float X)
	{
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** CPU mirror of HillaireSunsetBand in HillaireCommon.ush. */
	inline float SunsetBandWeight(float SunElevCos, float Rise0, float Rise1, float Fall0, float Fall1)
	{
		return SunsetSmoothstep(Rise0, Rise1, SunElevCos) * (1.0f - SunsetSmoothstep(Fall0, Fall1, SunElevCos));
	}

	/** Sunset band windows (rise0, rise1, fall0, fall1) shared by sky + aerial. */
	struct FSunsetBandWindow { float Rise0, Rise1, Fall0, Fall1; };
	constexpr FSunsetBandWindow SunsetBandViolet = { -0.04f, 0.00f, 0.08f, 0.20f };
	constexpr FSunsetBandWindow SunsetBandPink = { -0.11f, -0.05f, 0.02f, 0.10f };
	constexpr FSunsetBandWindow SunsetBandGold = { -0.07f, -0.02f, 0.04f, 0.12f };
	constexpr FSunsetBandWindow SunsetBandOrange = { -0.14f, -0.07f, -0.01f, 0.06f };
	constexpr FSunsetBandWindow SunsetBandRed = { -0.20f, -0.14f, -0.08f, -0.02f };

	/** CPU mirror of HillaireSunsetTint (BaseTint mixed toward SunChroma). */
	inline FVector3f SunsetTint(const FVector3f& BaseTint, const FVector3f& SunChroma)
	{
		const FVector3f Mixed(BaseTint.X * SunChroma.X, BaseTint.Y * SunChroma.Y, BaseTint.Z * SunChroma.Z);
		return FVector3f(
			FMath::Lerp(BaseTint.X, Mixed.X, 0.5f),
			FMath::Lerp(BaseTint.Y, Mixed.Y, 0.5f),
			FMath::Lerp(BaseTint.Z, Mixed.Z, 0.5f));
	}

	/** Sun chromaticity from a slot-0 ColorAttenuation (ratio: intensity cancels). White sun = (1,1,1). */
	inline FVector3f SunsetSunChroma(const FVector3f& SunColorAttenuation)
	{
		const float Luma = 0.2126f * SunColorAttenuation.X + 0.7152f * SunColorAttenuation.Y + 0.0722f * SunColorAttenuation.Z;
		const float Inv = 1.0f / FMath::Max(Luma, 1e-6f);
		return FVector3f(
			FMath::Clamp(SunColorAttenuation.X * Inv, 0.3f, 2.0f),
			FMath::Clamp(SunColorAttenuation.Y * Inv, 0.3f, 2.0f),
			FMath::Clamp(SunColorAttenuation.Z * Inv, 0.3f, 2.0f));
	}

	/** CPU mirror of HillaireSunsetSkyMultiplier in HillaireCommon.ush. */
	inline FVector3f SunsetSkyMultiplier(float SunElevCos, float LightViewCos, float ViewZenithCos, const FVector3f& SunChroma)
	{
		const float SunProx = FMath::Clamp(LightViewCos * 0.5f + 0.5f, 0.0f, 1.0f);
		const float Horizon = FMath::Clamp(1.0f - ViewZenithCos * ViewZenithCos, 0.0f, 1.0f);

		const float WViol = SunsetBandWeight(SunElevCos, SunsetBandViolet.Rise0, SunsetBandViolet.Rise1, SunsetBandViolet.Fall0, SunsetBandViolet.Fall1);
		const float WPink = SunsetBandWeight(SunElevCos, SunsetBandPink.Rise0, SunsetBandPink.Rise1, SunsetBandPink.Fall0, SunsetBandPink.Fall1);
		const float WGold = SunsetBandWeight(SunElevCos, SunsetBandGold.Rise0, SunsetBandGold.Rise1, SunsetBandGold.Fall0, SunsetBandGold.Fall1);
		const float WOran = SunsetBandWeight(SunElevCos, SunsetBandOrange.Rise0, SunsetBandOrange.Rise1, SunsetBandOrange.Fall0, SunsetBandOrange.Fall1);
		const float WRed = SunsetBandWeight(SunElevCos, SunsetBandRed.Rise0, SunsetBandRed.Rise1, SunsetBandRed.Fall0, SunsetBandRed.Fall1);

		const float MV = FMath::Lerp(0.55f, 1.0f, SunProx) * FMath::Lerp(0.45f, 1.0f, Horizon);
		const float MP = FMath::Lerp(0.45f, 1.0f, SunProx) * FMath::Lerp(0.30f, 1.0f, Horizon);
		const float MG = SunProx * FMath::Lerp(0.15f, 1.0f, Horizon);
		const float MO = SunProx * SunProx * FMath::Lerp(0.10f, 1.0f, Horizon);
		const float MR = FMath::Lerp(0.35f, 1.0f, SunProx) * FMath::Lerp(0.05f, 1.0f, Horizon);

		FVector3f Mult(1.0f, 1.0f, 1.0f);
		auto Accumulate = [&](const FVector3f& BaseTint, float W)
		{
			const FVector3f T = SunsetTint(BaseTint, SunChroma);
			Mult.X += (T.X - 1.0f) * W;
			Mult.Y += (T.Y - 1.0f) * W;
			Mult.Z += (T.Z - 1.0f) * W;
		};
		Accumulate(FVector3f(1.10f, 0.85f, 1.30f), WViol * MV * 0.40f);
		Accumulate(FVector3f(1.35f, 0.80f, 1.25f), WPink * MP * 0.52f);
		Accumulate(FVector3f(1.45f, 1.15f, 0.75f), WGold * MG * 0.58f);
		Accumulate(FVector3f(1.60f, 0.95f, 0.55f), WOran * MO * 0.65f);
		Accumulate(FVector3f(1.50f, 0.70f, 0.60f), WRed * MR * 0.58f);
		return Mult;
	}

	/** CPU mirror of HillaireSunsetAerialMultiplier in HillaireCommon.ush (elevation-only, half strength). */
	inline FVector3f SunsetAerialMultiplier(float SunElevCos, const FVector3f& SunChroma)
	{
		const float WViol = SunsetBandWeight(SunElevCos, SunsetBandViolet.Rise0, SunsetBandViolet.Rise1, SunsetBandViolet.Fall0, SunsetBandViolet.Fall1);
		const float WPink = SunsetBandWeight(SunElevCos, SunsetBandPink.Rise0, SunsetBandPink.Rise1, SunsetBandPink.Fall0, SunsetBandPink.Fall1);
		const float WGold = SunsetBandWeight(SunElevCos, SunsetBandGold.Rise0, SunsetBandGold.Rise1, SunsetBandGold.Fall0, SunsetBandGold.Fall1);
		const float WOran = SunsetBandWeight(SunElevCos, SunsetBandOrange.Rise0, SunsetBandOrange.Rise1, SunsetBandOrange.Fall0, SunsetBandOrange.Fall1);
		const float WRed = SunsetBandWeight(SunElevCos, SunsetBandRed.Rise0, SunsetBandRed.Rise1, SunsetBandRed.Fall0, SunsetBandRed.Fall1);

		FVector3f Mult(1.0f, 1.0f, 1.0f);
		auto Accumulate = [&](const FVector3f& BaseTint, float W)
		{
			const FVector3f T = SunsetTint(BaseTint, SunChroma);
			Mult.X += (T.X - 1.0f) * W;
			Mult.Y += (T.Y - 1.0f) * W;
			Mult.Z += (T.Z - 1.0f) * W;
		};
		Accumulate(FVector3f(1.10f, 0.85f, 1.30f), WViol * 0.20f);
		Accumulate(FVector3f(1.35f, 0.80f, 1.25f), WPink * 0.26f);
		Accumulate(FVector3f(1.45f, 1.15f, 0.75f), WGold * 0.29f);
		Accumulate(FVector3f(1.60f, 0.95f, 0.55f), WOran * 0.325f);
		Accumulate(FVector3f(1.50f, 0.70f, 0.60f), WRed * 0.29f);
		return Mult;
	}

	// ---- Terminator / solar-elevation gate (sky composite) ----
	// The normalized volumetric density fills the full 1.10x envelope, so the
	// optically significant atmosphere reaches a large height and the geometric
	// earth shadow keeps the upper limb illuminated far past civil twilight
	// (the top of this envelope is still lit with the sun ~25 deg below the
	// horizon). The LUT content is physically correct for that atmosphere; the
	// terminator is enforced at COMPOSITE time with the same smooth
	// solar-elevation curve the ZEPHYR presentation layer already uses
	// (ZephyrTypes.h ZephyrPresentation::DaylightFactor), evaluated at the
	// ray's atmosphere point (entry point outside, camera up inside). It is a
	// scalar luminance gate on the geometric terminator, not a color overlay:
	// it preserves the LUT's warm sunset/reddened in-scatter and only removes
	// the over-driven (tonemap-desaturating) and night-side energy.
	// VISUAL CALIBRATION: widened from (-0.12, 0.25) which gated the sunset
	// itself (~0.27 at elev 0). Now ~0.74 at horizon (broad warm band),
	// ~0.25 at -6 deg (gradual twilight contraction), 0 at ~-11.5 deg.
	constexpr float TerminatorBeginElevCos = -0.20f; // nautical twilight (~-11.5 deg)
	constexpr float TerminatorFullElevCos = 0.10f;   // full day (~5.7 deg)

	/** CPU mirror of HillaireTerminatorFactor in HillaireCommon.ush. */
	inline float TerminatorFactor(float SunElevationCos)
	{
		const float T = FMath::Clamp(
			(SunElevationCos - TerminatorBeginElevCos)
				/ (TerminatorFullElevCos - TerminatorBeginElevCos),
			0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	// ---- Sky ambient (terrain fill from the atmosphere) ----
	//
	// The rendered terrain is lit by a constant direct light (day/night via
	// N dot L only) plus a STATIC weak skylight fill that knows nothing of
	// the sky: at sunset the sky blazes while terrain collapses to black.
	// The ambient transfer (hemisphere-integrated sky in-scatter,
	// HillaireLutCpu::ComputeSkyAmbientTransfer, same scattering core as the
	// LUTs) drives the EXISTING skylight instead: no probes, no GI, no scene
	// capture. Day: subtle blue fill under dominant direct. Sunset: warm
	// ambient so readable terrain survives the direct collapse. Twilight:
	// faint remnant. Night: exactly zero (earth-shadowed transfer x
	// terminator gate). Symmetric by construction (pure solar geometry).
	/** Transfer -> skylight intensity gain (calibrated: day ambient ~10-15% of typical direct). */
	constexpr float SkyAmbientPresentationScale = 2.5f;

	/** Sky-ambient light state pushed into the existing skylight (color + intensity). */
	struct FSkyAmbientLightState
	{
		FVector3f Color = FVector3f::ZeroVector;
		float Intensity = 0.0f;
	};

	/**
	 * Maps unit-white sky-ambient transfer to a skylight state.
	 * SunIrradiance is the slot-0 delivered color x intensity WITHOUT the
	 * x30 sky SunScale (the ambient lives in the base pass with the direct
	 * light, not in sky buffer space). TerminatorFactor mirrors the sky
	 * extinction curve so no warm residual survives into night.
	 */
	inline FSkyAmbientLightState SkyAmbientLightState(
		const FVector3f& TransferUnitWhite,
		float SunElevCos,
		const FVector3f& SunIrradiance,
		float AmbientScale)
	{
		FSkyAmbientLightState Out;
		const float Gate = TerminatorFactor(SunElevCos);
		const FVector3f Gated(TransferUnitWhite.X * Gate, TransferUnitWhite.Y * Gate, TransferUnitWhite.Z * Gate);
		const float Luma = 0.2126f * Gated.X + 0.7152f * Gated.Y + 0.0722f * Gated.Z;
		Out.Intensity = AmbientScale * Luma;
		const FVector3f AmbChroma(
			FMath::Clamp(Gated.X / FMath::Max(Luma, 1e-9f), 0.2f, 4.0f),
			FMath::Clamp(Gated.Y / FMath::Max(Luma, 1e-9f), 0.2f, 4.0f),
			FMath::Clamp(Gated.Z / FMath::Max(Luma, 1e-9f), 0.2f, 4.0f));
		const FVector3f SunChroma = SunsetSunChroma(SunIrradiance);
		Out.Color = FVector3f(
			FMath::Max(AmbChroma.X * SunChroma.X, 0.0f),
			FMath::Max(AmbChroma.Y * SunChroma.Y, 0.0f),
			FMath::Max(AmbChroma.Z * SunChroma.Z, 0.0f));
		return Out;
	}

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
