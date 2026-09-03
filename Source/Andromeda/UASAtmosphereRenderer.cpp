#include "UASAtmosphereRenderer.h"

#include "UASAtmosphereShader.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "SceneView.h"

FScreenPassTexture FUASAtmosphereRenderer::Render(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs
)
{
    const FScreenPassTextureSlice SceneColorSlice =
        Inputs.GetInput(
            EPostProcessMaterialInput::SceneColor
        );

    if (!SceneColorSlice.IsValid())
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    const FScreenPassTexture SceneColor =
        FScreenPassTexture::CopyFromSlice(
            GraphBuilder,
            SceneColorSlice,
            FScreenPassTexture(),
            1
        );

    if (!SceneColor.IsValid())
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    FScreenPassRenderTarget Output =
        Inputs.OverrideOutput;

    if (!Output.IsValid())
    {
        Output =
            FScreenPassRenderTarget::CreateFromInput(
                GraphBuilder,
                SceneColor,
                View.GetOverwriteLoadAction(),
                TEXT("UAS.AtmosphereOutput")
            );
    }

    const FScreenPassTextureViewport OutputViewport(
        Output
    );

    const FGlobalShaderMap* ShaderMap =
        GetGlobalShaderMap(
            View.GetFeatureLevel()
        );

    TShaderMapRef<FUASAtmospherePixelShader>
        PixelShader(
            ShaderMap
        );

    FUASAtmospherePixelShader::FParameters*
        PassParameters =
        GraphBuilder.AllocParameters<
        FUASAtmospherePixelShader::FParameters
        >();

    PassParameters->SceneColorTexture =
        SceneColor.Texture;

    PassParameters->SceneColorSampler =
        TStaticSamplerState<
        SF_Bilinear
        >::GetRHI();

    PassParameters->RenderTargets[0] =
        Output.GetRenderTargetBinding();

    FPixelShaderUtils::AddFullscreenPass(
        GraphBuilder,
        ShaderMap,
        RDG_EVENT_NAME(
            "UAS Atmosphere"
        ),
        PixelShader,
        PassParameters,
        OutputViewport.Rect
    );

    return MoveTemp(Output);
}