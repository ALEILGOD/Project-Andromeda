#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"


class FUASTestVertexShader
    : public FGlobalShader
{
public:

    DECLARE_GLOBAL_SHADER(
        FUASTestVertexShader
    );

    SHADER_USE_PARAMETER_STRUCT(
        FUASTestVertexShader,
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


class FUASTestPixelShader
    : public FGlobalShader
{
public:

    DECLARE_GLOBAL_SHADER(
        FUASTestPixelShader
    );

    SHADER_USE_PARAMETER_STRUCT(
        FUASTestPixelShader,
        FGlobalShader
    );


    BEGIN_SHADER_PARAMETER_STRUCT(
        FParameters,
        )

        SHADER_PARAMETER_STRUCT_REF(
            FViewUniformShaderParameters,
            View
        )

        SHADER_PARAMETER_STRUCT_INCLUDE(
            FSceneTextureShaderParameters,
            SceneTextures
        )

        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D,
            SceneColorTexture
        )

        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            SceneColorSampler
        )

        SHADER_PARAMETER(
            FVector2f,
            ViewportMin
        )

        SHADER_PARAMETER(
            FVector2f,
            ViewportSize
        )

        SHADER_PARAMETER(
            FVector3f,
            PlanetCenter
        )

        SHADER_PARAMETER(
            float,
            AtmosphereRadius
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