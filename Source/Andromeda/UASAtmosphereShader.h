#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"


class FUASAtmosphereShader
    : public FGlobalShader
{
public:

    DECLARE_GLOBAL_SHADER(FUASAtmosphereShader);

    SHADER_USE_PARAMETER_STRUCT(
        FUASAtmosphereShader,
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