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
// ATMOS-06:
//     Implements first-order Rayleigh scattering with exponential
//     density profile and standard (1 + cos^2) phase function.
//     Uses RayleighScattering coefficient and RayleighScaleHeight
//     from atmosphere parameters.
//
// ATMOS-10:
//     Adds real second-order Rayleigh multiple scattering: the
//     diffuse already-scattered field is gathered per view sample
//     (tetrahedral quadrature + segment transmittance) and scattered
//     once more toward the camera. Tuned by MultipleScatteringScale.
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

        // ATMOS-10: global exposure of the second-order (multiple
        // scattering) term. 0 disables L2. Must match the USF.
        SHADER_PARAMETER(
            float,
            MultipleScatteringScale
        )

        // ATMOS-11: star validity flag. 1 when the Game Thread has
        // provided a real star position, 0 when StarPosition is still
        // the zero vector ("star not available"). The shader must skip
        // atmosphere integration when 0: a zero vector is the camera
        // position, not a star, and would generate nonphysical glow.
        // Must match the USF.
        SHADER_PARAMETER(
            int32,
            StarValid
        )

        // Output
        RENDER_TARGET_BINDING_SLOTS()

    END_SHADER_PARAMETER_STRUCT()
};