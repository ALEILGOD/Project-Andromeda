#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"


// =========================================================
// ANDROMEDA ATMOSPHERE GLOBAL SHADER
// =========================================================

// ATMOS-03: fullscreen diagnostic pixel shader that reconstructs the view ray,
// intersects it with each registered atmosphere sphere (ray/sphere intersection),
// and produces a diagnostic visualization of the intersection result.
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
        FRDGBufferSRVRef AtmosphereBuffer;

        // Scene color
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)

        // Output
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};
