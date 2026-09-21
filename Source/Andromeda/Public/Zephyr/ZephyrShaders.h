#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "SceneUniformBuffer.h"
#include "ShaderParameterStruct.h"

/**
 * ZEPHYR PRESENTATION - GLOBAL SHADER (bounded sky-only aesthetic overlay).
 *
 * Runs at AfterDOF, AFTER the authoritative ATMOS BeforeDOF composite. It is
 * NOT a sky renderer: ATMOS owns the physical sky. This pass adds ONLY a
 * bounded, depth-gated presentation tint (per-planet cloud/haze) to SKY
 * pixels, identified exactly like the ATMOS composite does:
 *
 *   DeviceZ > SkyDepthEpsilon  -> opaque geometry -> pass through byte-identical
 *   DeviceZ <= SkyDepthEpsilon -> background/sky  -> overlay applies
 *
 * Hard safety properties:
 * - Opaque (planet/terrain/mesh) pixels are copied unchanged: the surface can
 *   never be painted by ZEPHYR (no white blocks).
 * - Deep space (transition factor 0) and rays that miss the atmosphere are
 *   copied unchanged: no giant halo, no duplicated atmosphere.
 * - Night (daylight factor 0) is copied unchanged: no artificial night glow.
 * - The additive term is hard-clamped to ZephyrPresentation budgets, so the
 *   overlay can never cross the bloom threshold on its own.
 *
 * Row-vector conventions match HillaireSkyBackground.usf exactly (clip-z 0.5
 * ray fan, mul(float4(clip,1), InvProjMatrix), then
 * mul(dir, (float3x3)ViewToPlanetLocalRot)).
 */
class ANDROMEDA_API FZephyrPresentationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FZephyrPresentationCS);
	SHADER_USE_PARAMETER_STRUCT(FZephyrPresentationCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Immediate view UB (PreExposure etc.), engine pattern.
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, SceneColorInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, SceneDepthInput)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)

		// View geometry (camera-relative, same matrices as the ATMOS sky fan).
		SHADER_PARAMETER(FMatrix44f, InvProjMatrix)
		SHADER_PARAMETER(FMatrix44f, ViewToPlanetLocalRot)
		SHADER_PARAMETER(FVector3f, CameraPlanetLocalKm)

		// Sun (planet-local frame).
		SHADER_PARAMETER(FVector3f, PrimarySunDirLocal)
		SHADER_PARAMETER(FVector3f, SunColorAttenuation)

		// Canonical geometry (km, reused from the authoritative ATMOS snapshot).
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		SHADER_PARAMETER(float, ViewHeightKm)

		// Authoritative ZEPHYR gating: 0 at/above the real atmosphere top,
		// smooth 0->1 across the shell, 1 at/below the surface reference.
		SHADER_PARAMETER(float, ZephyrTransitionFactor)
		// CPU daylight factor from the real sun elevation (mirror of HLSL).
		SHADER_PARAMETER(float, DaylightFactor)

		// Sky appearance.
		SHADER_PARAMETER(FVector3f, ZenithColor)
		SHADER_PARAMETER(FVector3f, HorizonColor)
		SHADER_PARAMETER(FVector3f, SunGlowColor)
		SHADER_PARAMETER(FVector3f, MieColor)
		SHADER_PARAMETER(float, RayleighScale)
		SHADER_PARAMETER(float, MieScale)

		// Weather haze.
		SHADER_PARAMETER(float, WeatherHazeFactor)
		SHADER_PARAMETER(FVector3f, WeatherHazeColor)

		// Cloud layers (packed; up to ZephyrLimits::MaxCloudLayers arrays).
		SHADER_PARAMETER(uint32, CloudLayerCount)
		SHADER_PARAMETER_ARRAY(FVector4f, CloudLayerAltitudeConfigure, [4])
		SHADER_PARAMETER_ARRAY(FVector4f, CloudLayerColorDetail, [4])
		SHADER_PARAMETER_ARRAY(FVector4f, CloudLayerWindWater, [4])

		// Time for cloud drift.
		SHADER_PARAMETER(float, TimeSeconds)
		// Master intensity (bounded; scales the additive term before clamping).
		SHADER_PARAMETER(float, PresentationIntensity)

		// View rect + sky depth threshold (exact-texel Load mapping).
		SHADER_PARAMETER(FVector2f, ViewRectMin)
		SHADER_PARAMETER(FVector2f, ViewRectSize)
		SHADER_PARAMETER(float, SkyDepthEpsilon)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutputUav)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
	static bool ShouldCache(EShaderPlatform Platform) { return true; }
};
