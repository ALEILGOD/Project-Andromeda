#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

// ============================================================
// UAS - Atmosphere Vertex Shader
// ============================================================

class FUASAtmosphereVertexShader
    : public FGlobalShader
{
public:

    DECLARE_GLOBAL_SHADER(
        FUASAtmosphereVertexShader
    );

    SHADER_USE_PARAMETER_STRUCT(
        FUASAtmosphereVertexShader,
        FGlobalShader
    );

    BEGIN_SHADER_PARAMETER_STRUCT(
        FParameters,
        )
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    )
    {
        return IsFeatureLevelSupported(
            Parameters.Platform,
            ERHIFeatureLevel::SM6
        );
    }
};

// ============================================================
// UAS - Atmosphere Pixel Shader
// ============================================================

class FUASAtmospherePixelShader
    : public FGlobalShader
{
public:

    DECLARE_GLOBAL_SHADER(
        FUASAtmospherePixelShader
    );

    SHADER_USE_PARAMETER_STRUCT(
        FUASAtmospherePixelShader,
        FGlobalShader
    );

    BEGIN_SHADER_PARAMETER_STRUCT(
        FParameters,
        )
        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            SceneColorTexture
        )

        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            SceneColorSampler
        )

        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(
        const FGlobalShaderPermutationParameters& Parameters
    )
    {
        return IsFeatureLevelSupported(
            Parameters.Platform,
            ERHIFeatureLevel::SM6
        );
    }
};