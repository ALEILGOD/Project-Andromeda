#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RenderGraphFwd.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"
#include "Planet/Zephyr/ZephyrTypes.h"

// =========================================================
// ZEPHYR GLOBAL SHADERS (ZEPHYR-01)
// =========================================================
// Four shaders form the Hillaire-class LUT pipeline:
//
//   FZephyrTransmittanceCS  -> Transmittance LUT atlas
//     T(height, sunZenith) per planet slice (Bruneton §4.1).
//
//   FZephyrMultiScatterCS   -> Multi-Scattering LUT atlas
//     MS(height, sunZenith) per slice. A REAL second-order
//     gather (fibonacci-sphere directions x short segments,
//     first-order sources with LUT transmittance, one-bounce
//     Lambertian ground). Never singleScattering * constant.
//
//   FZephyrSkyViewCS        -> Sky-View SS + MS atlas pair
//     Lsky(viewZenith, sunAzimuth) for the CURRENT camera
//     height and sun direction, by marching the view ray and
//     sampling the T/MS LUTs (Hillaire §5; integral of
//     T_view * (S_Rayleigh + S_Mie + S_Multiple)).
//
//   FZephyrSkyPS            -> final background composite
//     Selects the governing planet per pixel, samples the
//     Sky-View LUTs along the true FSceneView camera ray,
//     adds the transmittance-attenuated sun disk, and
//     OVERWRITES sky pixels (order-independent vs ATMOS:
//     geometry pixels pass through untouched).
//
// Planet data travels as an RDG structured buffer of
// FZephyrPlanetGPUData (layout MUST match ZephyrCommon.ush).
// LUT atlases stack planet slices vertically:
//     slice s occupies rows [s*SliceH, (s+1)*SliceH).
// =========================================================

class FZephyrTransmittanceCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FZephyrTransmittanceCS);
    SHADER_USE_PARAMETER_STRUCT(FZephyrTransmittanceCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(
            StructuredBuffer<FZephyrPlanetGPUData>,
            PlanetBuffer
        )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float4>,
            OutTransmittance
        )
        SHADER_PARAMETER(int32, PlanetCount)
        SHADER_PARAMETER(int32, SliceHeight)
        SHADER_PARAMETER(float, MieScale)
        SHADER_PARAMETER(float, AbsorptionScale)
    END_SHADER_PARAMETER_STRUCT()
};

class FZephyrMultiScatterCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FZephyrMultiScatterCS);
    SHADER_USE_PARAMETER_STRUCT(FZephyrMultiScatterCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(
            StructuredBuffer<FZephyrPlanetGPUData>,
            PlanetBuffer
        )
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            InTransmittance
        )
        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            LinearClampSampler
        )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float4>,
            OutMultiScatter
        )
        SHADER_PARAMETER(int32, PlanetCount)
        SHADER_PARAMETER(int32, TransSliceHeight)
        SHADER_PARAMETER(int32, SliceHeight)
        SHADER_PARAMETER(float, MieScale)
        SHADER_PARAMETER(float, AbsorptionScale)
    END_SHADER_PARAMETER_STRUCT()
};

class FZephyrSkyViewCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FZephyrSkyViewCS);
    SHADER_USE_PARAMETER_STRUCT(FZephyrSkyViewCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(
            StructuredBuffer<FZephyrPlanetGPUData>,
            PlanetBuffer
        )
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            InTransmittance
        )
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            InMultiScatter
        )
        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            LinearClampSampler
        )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float4>,
            OutSkySingle
        )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float4>,
            OutSkyMulti
        )
        // Per-planet sun direction now stored in PlanetBuffer.Packed6.xyz
        // No single SunDirectionCam uniform needed.
        SHADER_PARAMETER(int32, PlanetCount)
        SHADER_PARAMETER(int32, TransSliceHeight)
        SHADER_PARAMETER(int32, MSSliceHeight)
        SHADER_PARAMETER(int32, SliceHeight)
        SHADER_PARAMETER(float, MieScale)
        SHADER_PARAMETER(float, AbsorptionScale)
        SHADER_PARAMETER(float, MultiScatterScale)
    END_SHADER_PARAMETER_STRUCT()
};

class FZephyrSkyPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FZephyrSkyPS);
    SHADER_USE_PARAMETER_STRUCT(FZephyrSkyPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, ViewportSize)
        SHADER_PARAMETER(FVector3f, CameraWorldPosition)
        SHADER_PARAMETER(FMatrix44f, InvViewProjection)

        SHADER_PARAMETER(int32, PlanetCount)

        SHADER_PARAMETER_RDG_BUFFER_SRV(
            StructuredBuffer<FZephyrPlanetGPUData>,
            PlanetBuffer
        )

        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            SceneColorTexture
        )
        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            SceneColorSampler
        )

        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            SkySingleLUT
        )
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            SkyMultiLUT
        )
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            TransmittanceLUT
        )
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            MultiScatterLUT
        )
        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            LutSampler
        )

        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(
            FSceneTextureUniformParameters,
            SceneTexturesStruct
        )
        SHADER_PARAMETER_STRUCT_REF(
            FViewUniformShaderParameters,
            ViewUniformBuffer
        )
        SHADER_PARAMETER(int32, DepthOcclusionEnabled)

        // READ-ONLY ATMOS coupling (no ATMOS modification): 1 when
        // the ATMOS foreground pass is enabled. ATMOS integrates
        // every atmosphere chord (including limb chords) in its
        // own pass, so ZEPHYR must not add a second limb
        // integral on top ("double atmosphere"). When ATMOS is
        // off, ZEPHYR owns the limb.
        SHADER_PARAMETER(int32, AtmosEnabled)

        // Per-planet sun direction now stored in PlanetBuffer.Packed6.xyz
        // No single SunDirectionCam uniform needed.
        SHADER_PARAMETER(int32, StarValid)
        SHADER_PARAMETER(int32, DebugMode)
        SHADER_PARAMETER(int32, TransSliceHeight)
        SHADER_PARAMETER(int32, MSSliceHeight)
        SHADER_PARAMETER(int32, SkySliceHeight)
        SHADER_PARAMETER(float, SunCosAngularRadius)
        SHADER_PARAMETER(float, SunLuminance)
        SHADER_PARAMETER(float, Exposure)
        SHADER_PARAMETER(float, MieScale)
        SHADER_PARAMETER(float, AbsorptionScale)
        SHADER_PARAMETER(float, MultiScatterScale)

        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};
