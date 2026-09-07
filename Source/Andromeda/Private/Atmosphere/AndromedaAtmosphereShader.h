#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"


// =========================================================
// ANDROMEDA ATMOSPHERE GLOBAL SHADER
// =========================================================

// ATMOS-02: fullscreen diagnostic pixel shader dispatched through a real
// RDG pass (FPixelShaderUtils::AddFullscreenPass) in the post-processing
// chain via the public scene view extension hook.
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

    // Legacy name binding: every uniform member must match a global
    // variable declared in AndromedaAtmosphere.usf.
    // ATMOS-04+ will extend this struct with atmosphere volumes,
    // scattering coefficients and optical depth data.
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, ViewportSize)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};