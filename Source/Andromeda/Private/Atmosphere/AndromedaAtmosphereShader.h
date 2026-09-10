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

// ATMOS-04: fullscreen diagnostic pixel shader that reconstructs the camera
// world-space ray per pixel, intersects every atmosphere sphere,
// marches fixed samples entry->exit and accumulates diagnostic density.
//
// Source file : /Andromeda/AndromedaAtmosphere.usf
//               ([Project]/Shaders/Andromeda/AndromedaAtmosphere.usf)
// Entry point : AndromedaAtmosphereMainPS
// Registered in AndromedaAtmosphereRenderer.cpp via IMPLEMENT_GLOBAL_SHADER.

class FAndromedaAtmospherePS
    : public FGlobalShader
{


public:

    DECLARE_GLOBAL_SHADER(FAndromedaAtmospherePS);

    SHADER_USE_PARAMETER_STRUCT(FAndromedaAtmospherePS, FGlobalShader);


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
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FAndromedaAtmosphereGPUData>, AtmosphereBuffer)

        // Scene color
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)

        // Scene textures (ATMOS-04 depth occlusion)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
        SHADER_PARAMETER(int32, DepthOcclusionEnabled)

        // Output
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};
