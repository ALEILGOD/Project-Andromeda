#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "SceneUniformBuffer.h"
#include "ShaderParameterStruct.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"

/**
 * HILLAIRE ATMOSPHERE - GLOBAL SHADERS + PASS PARAMETERS (Phase 1).
 *
 * UE5.8 global-shader infrastructure for the future passes (spec section 7).
 * One FGlobalShader subclass per entry point; macros become permutation
 * dimensions only where needed (kept minimal in Phase 1).
 *
 * Per-planet light upload uses uniform arrays at N <= 8 (spec decision 3):
 * lightDirRadius[i] = planet-local dir TO light + disk radius (w),
 * lightColor[i]     = Color*Intensity[*1/d^2], rgb.
 * This mirrors the reference cbuffer layout (lightDirRadius[8]/lightColor[8]).
 */

// Shared LUT-sampling inputs for the Phase-1 stub passes (SkyView/Aerial).
// The two REAL Phase-2A generation passes use FHillaireAtmosphereUniformParams
// below; this struct stays minimal so stub binding keeps compiling.
BEGIN_SHADER_PARAMETER_STRUCT(FHillaireLutGenParameters, )
	SHADER_PARAMETER(float, BottomRadiusKm)
	SHADER_PARAMETER(float, TopRadiusKm)
	SHADER_PARAMETER(float, MultipleScatteringFactor)
	SHADER_PARAMETER(uint32, LutWidth)
	SHADER_PARAMETER(uint32, LutHeight)
END_SHADER_PARAMETER_STRUCT()

/**
 * Participating-medium uniforms shared by the Phase-2A LUT generation passes.
 *
 * Every field binds 1:1 to an HLSL global of the same name in the .usf entry
 * (see HillaireLutCore.ush HillaireMakeAtmosphereParams). Fields are ordered
 * float3+float per row to match HLSL cbuffer packing exactly; do NOT reorder
 * without checking the .usf side. Each pass declares ONLY the uniforms its
 * math consumes (unbound extras would fail shader binding), so pass-specific
 * knobs live in the pass FParameters, not here.
 *
 * Dependency contract (task section 10), enforced by FillAtmosphereUniforms:
 * - Transmittance + MultiScattering bake with UNIT illuminance
 *   (ILLUMINANCE_IS_ONE): SolarIrradiance is NOT an input. Runtime sun
 *   color/intensity multiplies at composite time (Phase 2B+).
 * - MuSMin / MiePhaseG are NOT LUT-generation inputs (phase/disk belong to
 *   the SkyView/final passes). They stay in the profile + cache key only.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FHillaireAtmosphereMediumParams, )
	SHADER_PARAMETER(float, BottomRadiusKm)
	SHADER_PARAMETER(float, TopRadiusKm)
	SHADER_PARAMETER(float, RayleighExpScale)
	SHADER_PARAMETER(float, MieExpScale)
	SHADER_PARAMETER(FVector3f, RayleighScatteringKm)
	SHADER_PARAMETER(float, AbsorptionWidthKm)
	SHADER_PARAMETER(FVector3f, MieScatteringKm)
	SHADER_PARAMETER(float, AbsorptionLinear0)
	SHADER_PARAMETER(FVector3f, MieExtinctionKm)
	SHADER_PARAMETER(float, AbsorptionConstant0)
	SHADER_PARAMETER(FVector3f, MieAbsorptionKm)
	SHADER_PARAMETER(float, AbsorptionLinear1)
	SHADER_PARAMETER(FVector3f, AbsorptionExtinctionKm)
	SHADER_PARAMETER(float, AbsorptionConstant1)
END_SHADER_PARAMETER_STRUCT()

// Full per-planet final-pass parameters (P4). LUT SRVs are bound per pass;
// only the passes that read a texture declare it (RDG culling friendly).
BEGIN_SHADER_PARAMETER_STRUCT(FHillaireAtmospherePassParameters, )
	SHADER_PARAMETER(float, BottomRadiusKm)
	SHADER_PARAMETER(float, TopRadiusKm)
	SHADER_PARAMETER(FVector3f, PlanetCenterCamRelativeKm)
	SHADER_PARAMETER(FVector3f, CameraCamRelativeKm)
	SHADER_PARAMETER(float, MultipleScatteringFactor)
	SHADER_PARAMETER(uint32, LightCount)
	SHADER_PARAMETER(uint32, bSinglePrimary)
	SHADER_PARAMETER_ARRAY(FVector4f, LightDirRadius, [HILLAIRE_MAX_ATMOSPHERE_LIGHTS])
	SHADER_PARAMETER_ARRAY(FVector4f, LightColor, [HILLAIRE_MAX_ATMOSPHERE_LIGHTS])
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, TransmittanceLut)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SkyViewLut)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, MultiScatteringLut)
	SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
END_SHADER_PARAMETER_STRUCT()

/**
 * P0: Transmittance LUT compute (256x64, per-texel).
 *
 * Documented choice (task section 16): the reference bakes this with a
 * fullscreen raster tri, but its math is strictly per-pixel (pixPos -> uv ->
 * 40-step optical-depth march -> exp(-OD): no interpolation, no derivatives,
 * no blending). A per-texel compute invocation is the exact same
 * computation and needs no view/viewport, so it fits the RDG LUT pipeline.
 * Dispatch (W/8, H/8, 1) = (32, 8, 1), [numthreads(8, 8, 1)].
 */
class HILLAIREATMOSPHERE_API FHillaireTransmittanceLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireTransmittanceLutCS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireTransmittanceLutCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHillaireAtmosphereMediumParams, Atmosphere)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TransmittanceUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};

/**
 * P1: Multi-scattering transfer LUT compute (32x32).
 * Verbatim threading from reference NewMultiScattCS: [numthreads(1, 1, 64)],
 * one thread group per texel, 64 stratified sphere directions reduced via
 * groupshared memory. Dispatch (32, 32, 1). Reads the JUST-BAKED
 * transmittance LUT (T -> MS ordering enforced by RDG barriers).
 * Extra uniforms (all consumed): GroundAlbedo (ground bounce),
 * PlanetRadiusOffsetKm (earth-shadow lift), MultipleScatteringFactor (baked
 * into the output ONLY here), MultiScatteringLutRes (texel -> param mapping).
 */
class HILLAIREATMOSPHERE_API FHillaireMultiScatteringCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireMultiScatteringCS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireMultiScatteringCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHillaireAtmosphereMediumParams, Atmosphere)
		SHADER_PARAMETER(FVector3f, GroundAlbedo)
		SHADER_PARAMETER(float, PlanetRadiusOffsetKm)
		SHADER_PARAMETER(float, MultipleScatteringFactor)
		SHADER_PARAMETER(float, MultiScatteringLutRes)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, TransmittanceLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MultiScatteringUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};

/**
 * Fill the shared medium uniforms from a planet profile (single write path:
 * radii/sigmas/densities always flow from the profile, never Earth
 * literals). SolarIrradiance/MuSMin/MiePhaseG are deliberately NOT uploaded:
 * the LUTs bake unit-illuminance transfer (ILLUMINANCE_IS_ONE) with uniform
 * phase; those profile fields join the cache key but not the upload.
 */
HILLAIREATMOSPHERE_API void HillaireFillAtmosphereUniforms(
	FHillaireAtmosphereMediumParams& Out,
	const FHillaireAtmosphereProfile& Profile);

/**
 * P2: SkyView LUT compute (192x108, per-texel, slot-0 primary bake).
 *
 * Verbatim port of reference SkyViewLutPS (RenderSkyRayMarching.hlsl) with the
 * MULTISCATAPPROX_ENABLED=1 configuration the sample selects whenever
 * MultipleScatteringFactor > 0 (the default): the march reads Transmittance
 * AND MultiScattering. Pipeline note (task section 16): the reference bakes
 * with a fullscreen raster tri, but after the WorldPos/WorldDir reconstruction
 * every texel is independent (no interpolation, no derivatives, no blending),
 * so per-texel compute is the exact same computation. Dispatch (W/8, H/8, 1).
 *
 * v1 single-light rule (task sections 13-14): bakes the SLOT-0 registry light
 * (planet-local dir + unit-white transfer, N x M compatible). No
 * CurrentSun/PrimarySun singleton: the pass input is per-planet resolved data,
 * so the future SkyView(Planet, Light[0..N]) needs no data-model change.
 * Sun COLOR x Intensity applies at composite (Phase 2C+), never in the LUT.
 */
class HILLAIREATMOSPHERE_API FHillaireSkyViewLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireSkyViewLutCS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireSkyViewLutCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHillaireAtmosphereMediumParams, Atmosphere)
		// Slot-0 primary sun, planet-local unit dir TO the sun (GT-resolved).
		SHADER_PARAMETER(FVector3f, PrimarySunDirLocal)
		// Planet-local unit UP at the camera (= normalize(camLocal)).
		SHADER_PARAMETER(FVector3f, CameraUpLocal)
		// Camera height over planet center, km (clamped to the 20 m floor).
		SHADER_PARAMETER(float, ViewHeightKm)
		// Real phase excentricity (MieRayPhase = true on this path).
		SHADER_PARAMETER(float, MiePhaseG)
		// Legacy 4-14 distribution bounds (reference uiViewRayMarchMin/MaxSPP).
		SHADER_PARAMETER(FVector2f, RayMarchMinMaxSPP)
		SHADER_PARAMETER(float, PlanetRadiusOffsetKm)
		SHADER_PARAMETER(float, MultiScatteringLutRes)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, TransmittanceLut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, MultiScatteringLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SkyViewUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};

/**
 * P3: Aerial perspective camera volume (32x32x32 froxels, view-dependent).
 *
 * Verbatim port of reference RenderCameraVolumePS
 * (RenderSkyRayMarching.hlsl, MULTISCATAPPROX_ENABLED=1 configuration):
 * per-froxel clip-space ray march from the planet-local camera, slice-limited
 * tMax (squared distribution, 4 km/slice), under-ground clamp-out,
 * above-top MoveToTop shrink, fixed max(1,(sliceId+1)*2) steps, ground=false,
 * real Mie+Rayleigh phases, MS approximation ON. Output (L, 1-mean(T)).
 *
 * View-dependent by construction: evaluated ON DEMAND into pooled scratch,
 * never cached as planet-global data (task section 9). T/MS inputs stay
 * cached (EnsurePlanetLuts outputs in the same graph). Single-light rule:
 * slot-0 primary, unit-white transfer (Andromeda convention).
 * Threading [numthreads(4,4,4)], dispatch (8,8,8); sliceId == DispatchId.z.
 */
class HILLAIREATMOSPHERE_API FHillaireAerialPerspectiveCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireAerialPerspectiveCS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireAerialPerspectiveCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHillaireAtmosphereMediumParams, Atmosphere)
		// Planet-local camera position, km (snapshot-derived, unnormalized).
		SHADER_PARAMETER(FVector3f, CameraPlanetLocalKm)
		// Inverse projection (snapshot ProjectionMatrix, inverted once on CPU,
		// narrowed to float). Row-vector convention: mul(float4(clip,1), M).
		// NOTE: FMatrix (double) is NOT a valid SHADER_PARAMETER type in UE5;
		// FMatrix44f is. Narrowing happens once at fill time (ToMatrix44f).
		SHADER_PARAMETER(FMatrix44f, InvProjMatrix)
		// View-to-planet-local rotation only, translation zeroed:
		// transpose(snapshot ViewMatrix rotation) * Qconj (row-vector order:
		// view -> cam-relative world -> planet-local). Row-vector
		// convention: mul(dir, (float3x3)ViewToPlanetLocalRot).
		SHADER_PARAMETER(FMatrix44f, ViewToPlanetLocalRot)
		// Slot-0 primary sun, planet-local unit dir TO the sun (GT-resolved).
		SHADER_PARAMETER(FVector3f, PrimarySunDirLocal)
		// Real phase excentricity (MieRayPhase = true on this path).
		SHADER_PARAMETER(float, MiePhaseG)
		SHADER_PARAMETER(float, PlanetRadiusOffsetKm)
		// Reference clamp-out lift (Bottom + offset + 0.001, line 838).
		SHADER_PARAMETER(float, AerialGroundClampLiftKm)
		// Reference slice depth (4 km/slice, line 829).
		SHADER_PARAMETER(float, AerialKmPerSlice)
		SHADER_PARAMETER(float, MultiScatteringLutRes)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, TransmittanceLut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, MultiScatteringLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, AerialVolumeUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};

/**
 * P4-COMP: Aerial perspective composite (Phase 2D, fullscreen post pass).
 *
 * Consumes the Phase-2C camera volume at BeforeDOF (linear HDR, pre-tonemap):
 * Out = In * (1-AP.a) + SunColor * AP.rgb * PreExposure, alpha preserved.
 * Sky/background pixels (reversed-Z device depth <= CompositeSkyDepthEpsilon)
 * pass through IDENTICAL (no aerial, no double-scatter: background belongs to
 * the future SkyView path). Beyond-range w clamps to the deepest slice
 * (reference-identical, finite). No sun disk (final/sky scope, not 2D).
 *
 * Bindings: scene color via exact-texel Load (no resampling blur, mirrors the
 * reference per-pixel march); scene depth via the FSceneTextureShaderParameters
 * struct include (engine SceneTexturesStruct pattern); exposure via the view
 * uniform buffer (the same PreExposure the base pass used - task section 17).
 */
class HILLAIREATMOSPHERE_API FHillaireAerialCompositeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireAerialCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireAerialCompositeCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// STRUCT_REF (not INCLUDE): binds the view's existing immediate UB
		// (View.ViewUniformBuffer), engine pattern per PostProcessBokehDOF /
		// ScreenSpaceShadows. HLSL accessor is still View.*.
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		// Explicit depth SRV (NOT the scene-textures UB): the UB contents
		// cannot be fabricated in tests (RHI UB creation validates every
		// resource slot non-null), and production extracts the raw ref from
		// the post-processing UB (see ViewExtension). Same depth values,
		// testable binding.
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, SceneDepthInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SceneColorInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, AerialVolume)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
		// Inverse projection (snapshot ProjectionMatrix, inverted once on CPU,
		// narrowed to float). Row-vector convention: mul(float4(clip,1), M).
		SHADER_PARAMETER(FMatrix44f, InvProjMatrix)
		// Slot-0 sun Color*Intensity (linear), unit-white transfer convention.
		SHADER_PARAMETER(FVector3f, SunColorAttenuation)
		// View-rect origin/size in buffer pixels (exact-texel Load mapping).
		SHADER_PARAMETER(FVector2f, ViewRectMin)
		SHADER_PARAMETER(FVector2f, ViewRectSize)
// Slice depth: tDepth / AerialKmPerSlice (atmosphere-relative: the
	// per-planet envelope / 25; see HillaireLimits).
	SHADER_PARAMETER(float, AerialKmPerSlice)
		// Sky/background device-Z threshold (reversed-Z: 0 = far).
		SHADER_PARAMETER(float, SkyDepthEpsilon)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutputUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};

/**
 * P4-SKY: Sky background composite (production sky path, BeforeDOF).
 *
 * Samples the Phase-2B SkyView LUT for the governing planet's slot-0 primary
 * sun at the current camera/view: per-pixel planet-local ray (InvProj +
 * ViewToPlanetLocalRot, same matrices as the aerial bake), viewZenith +
 * sun-azimuth uv mapping (HillaireSampling.ush, verbatim), unit-white
 * transfer scaled by slot-0 ColorAttenuation and the view PreExposure (same
 * buffer-space convention as the Phase-2D aerial composite). Opaque pixels
 * pass through identical (aerial owns them); background pixels are composited
 * as sky + T * background (reference FASTSKY branch). No sun disk, no hardcoded gradient.
 */
class HILLAIREATMOSPHERE_API FHillaireSkyBackgroundCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireSkyBackgroundCS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireSkyBackgroundCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// STRUCT_REF (not INCLUDE): binds the view's existing immediate UB
		// (View.ViewUniformBuffer), engine pattern per PostProcessBokehDOF /
		// ScreenSpaceShadows. HLSL accessor is still View.*.
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SceneColorInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, SceneDepthInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SkyViewLut)
		// View-ray transmittance (reference composite: sky + T * background).
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, TransmittanceLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
		// Inverse projection (snapshot ProjectionMatrix, inverted once on CPU,
		// narrowed to float). Row-vector convention: mul(float4(clip,1), M).
		SHADER_PARAMETER(FMatrix44f, InvProjMatrix)
		// View-to-planet-local rotation only, translation zeroed:
		// transpose(snapshot ViewMatrix rotation) * Qconj (row-vector order:
		// view -> cam-relative world -> planet-local). Row-vector
		// convention: mul(dir, (float3x3)ViewToPlanetLocalRot).
		SHADER_PARAMETER(FMatrix44f, ViewToPlanetLocalRot)
		// Planet-local camera position, km (snapshot-derived, unnormalized).
		SHADER_PARAMETER(FVector3f, CameraPlanetLocalKm)
		// Slot-0 primary sun, planet-local unit dir TO the sun (GT-resolved).
		SHADER_PARAMETER(FVector3f, PrimarySunDirLocal)
		// Slot-0 sun Color*Intensity (linear), unit-white transfer convention.
		SHADER_PARAMETER(FVector3f, SunColorAttenuation)
		// Authoritative planet boundaries, km (profile mirrors).
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		// Camera height over planet center, km (CPU-clamped to the 20 m floor,
		// the same convention as the SkyView bake).
		SHADER_PARAMETER(float, ViewHeightKm)
		// Sun disk angular radius, radians (0 = no disk).
		SHADER_PARAMETER(float, SunAngularRadiusRad)
		// View-rect origin/size in buffer pixels (exact-texel Load mapping).
		SHADER_PARAMETER(FVector2f, ViewRectMin)
		SHADER_PARAMETER(FVector2f, ViewRectSize)
		// Sky/background device-Z threshold (reversed-Z: 0 = far).
		SHADER_PARAMETER(float, SkyDepthEpsilon)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutputUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};

/**
 * FASE-10 debug visualization (diagnostic only, never shipping behaviour).
 *
 * Fullscreen compute over the view rect driven by r.Hillaire.DebugMode:
 * 1 = Transmittance LUT, 2 = MultiScattering LUT, 3 = SkyView LUT,
 * 4 = Aerial Perspective volume, 5 = density/profile,
 * 6 = per-pixel planet-local view ray (ViewDirLocal*0.5+0.5; flat screen here
 *     proves the ray matrices are broken, NOT the scattering),
 * 7 = view zenith cosine grayscale ((cos+1)/2; proves horizon mapping varies),
 * 8 = SkyView LUT UV (u,v,0; proves per-pixel UV mapping),
 * 9 = ray/depth classification mask (red = opaque/depth passthrough,
 *     green = sky ray hitting ground sphere, blue = space ray missing ground;
 *     proves sphere intersection + depth gating per pixel),
 * 10 = view-ray transmittance mean grayscale (isolates transmittance energy),
 * 11 = raw SkyView transfer sample (isolates LUT energy from sun/exposure scale),
 * 12 = +PrimarySunDirLocal as RGB, 13 = -PrimarySunDirLocal as RGB,
 * 14 = saturate(dot(ViewDir, SunDir)), 15 = saturate(dot(ViewDir, -SunDir)),
 * 16 = surface NdotL mask (saturate(dot(n, sun)) on opaque, black on sky),
 * 17 = SkyViewU.x grayscale, 18 = SkyViewU.y grayscale,
 * 19 = sun-vector validity traffic light, 20 = world-vs-local NdotL split
 * (R = world NdotL, G = local NdotL, B = |difference|),
 * 21 = cached-vs-direct SkyView split (LEFT = cached LUT as in production,
 * RIGHT = direct per-pixel march with the CURRENT sun).
 * Modes 1-4 sample the REAL pooled GPU textures produced by the generation
 * passes (same-frame graph); mode 5 evaluates the live governing profile;
 * modes 6-18 re-derive the production sky-pass ray inputs per pixel from the
 * SAME matrices the composite consumes. Output is raw linear HDR (alpha 1);
 * every bound texture is produced by a real pass or import (debug-only
 * fallbacks included), so no mode can trip an RDG unwritten-resource error.
 */
class HILLAIREATMOSPHERE_API FHillaireAtmosphereDebugCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireAtmosphereDebugCS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireAtmosphereDebugCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, DebugMode)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, TransmittanceLut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, MultiScatteringLut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SkyViewLut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, AerialVolume)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, SceneDepthInput)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		SHADER_PARAMETER(float, RayleighExpScale)
		SHADER_PARAMETER(float, MieExpScale)
		SHADER_PARAMETER(float, AbsorptionWidthKm)
		SHADER_PARAMETER(float, AbsorptionLinear0)
		SHADER_PARAMETER(float, AbsorptionConstant0)
		SHADER_PARAMETER(float, AbsorptionLinear1)
		SHADER_PARAMETER(float, AbsorptionConstant1)
		// Ray-debug frame (modes 6-11): the exact matrices/positions the
		// production sky composite consumes this frame.
		SHADER_PARAMETER(FMatrix44f, InvProjMatrix)
		SHADER_PARAMETER(FMatrix44f, ViewToPlanetLocalRot)
		SHADER_PARAMETER(FVector3f, CameraPlanetLocalKm)
		SHADER_PARAMETER(FVector3f, PrimarySunDirLocal)
		SHADER_PARAMETER(float, ViewHeightKm)
		SHADER_PARAMETER(float, SkyDepthEpsilon)
		// World-frame ground truth (mode 20): camera-relative star/planet
		// geometry plus planet orientation. Translation cancels in the normal.
		SHADER_PARAMETER(FVector3f, StarCamRelativeKm)
		SHADER_PARAMETER(FVector3f, CenterCamRelativeKmDbg)
		SHADER_PARAMETER(FVector4f, PlanetQuatWS)
		// Direct-march medium (mode 21): full participating medium, same
		// fields the production LUT bakes consume. Filled from the live
		// governing profile; march config mirrors the SkyView bake.
		SHADER_PARAMETER(FVector3f, RayleighScatteringKm)
		SHADER_PARAMETER(FVector3f, MieScatteringKm)
		SHADER_PARAMETER(FVector3f, MieExtinctionKm)
		SHADER_PARAMETER(FVector3f, MieAbsorptionKm)
		SHADER_PARAMETER(FVector3f, AbsorptionExtinctionKm)
		SHADER_PARAMETER(float, MiePhaseG)
		SHADER_PARAMETER(FVector2f, RayMarchMinMaxSPP)
		SHADER_PARAMETER(float, PlanetRadiusOffsetKm)
		SHADER_PARAMETER(float, MultiScatteringLutRes)
		SHADER_PARAMETER(FVector2f, ViewRectMin)
		SHADER_PARAMETER(FVector2f, ViewRectSize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutputUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};

/** P5 (test-only): LUT debug viewer. Excluded from shipping behaviour. */
class HILLAIREATMOSPHERE_API FHillaireDebugLutPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHillaireDebugLutPS);
	SHADER_USE_PARAMETER_STRUCT(FHillaireDebugLutPS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, DebugLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DebugLutSampler)
		SHADER_PARAMETER(uint32, LutKind)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};
