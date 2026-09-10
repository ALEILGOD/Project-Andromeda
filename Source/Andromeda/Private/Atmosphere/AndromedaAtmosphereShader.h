#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RenderGraphFwd.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"

// =========================================================
// ANDROMEDA ATMOSPHERE GLOBAL SHADER
// =========================================================
//
// ATMOS-04:
//     Fullscreen diagnostic pixel shader that reconstructs the
//     camera-relative ray, intersects atmosphere spheres and
//     marches fixed samples.
//
// ATMOS-05:
//     Receives the finite point-star position in the same
//     camera-relative coordinate frame. The USF calculates
//     Sample -> Star for every march sample.
//
// Source:
//     /Andromeda/AndromedaAtmosphere.usf
//
// Entry:
//     AndromedaAtmosphereMainPS
// =========================================================
class FAndromedaAtmospherePS
    : public FGlobalShader
{
public:

    DECLARE_GLOBAL_SHADER(FAndromedaAtmospherePS);

    SHADER_USE_PARAMETER_STRUCT(
        FAndromedaAtmospherePS,
        FGlobalShader
    );

    // =========================================================
    // SHADER PARAMETERS
    // =========================================================
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )

        // Viewport
        SHADER_PARAMETER(FIntPoint, ViewportSize)

        // Camera
        SHADER_PARAMETER(FVector3f, CameraWorldPosition)
        SHADER_PARAMETER(FMatrix44f, InvViewProjection)

        // Atmosphere data
        SHADER_PARAMETER(int32, AtmosphereCount)

        SHADER_PARAMETER_RDG_BUFFER_SRV(
            StructuredBuffer<FAndromedaAtmosphereGPUData>,
            AtmosphereBuffer
        )

        // Scene color
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            SceneColorTexture
        )

        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            SceneColorSampler
        )

        // Scene textures (ATMOS-04 depth occlusion)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(
            FSceneTextureUniformParameters,
            SceneTexturesStruct
        )

        SHADER_PARAMETER_STRUCT_REF(
            FViewUniformShaderParameters,
            ViewUniformBuffer
        )

        SHADER_PARAMETER(
            int32,
            DepthOcclusionEnabled
        )

        // Output
        RENDER_TARGET_BINDING_SLOTS()

    END_SHADER_PARAMETER_STRUCT()
};