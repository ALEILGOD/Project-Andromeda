#include "UASTestRenderer.h"

#include "UASTestShader.h"
#include "PlanetAtmosphereRenderer.h"

#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "SceneRenderTargetParameters.h"

#include "Runtime/Renderer/Internal/PostProcess/PostProcessInputs.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"


void FUASTestRenderer::Render(
    FRDGBuilder& GraphBuilder,
    const FSceneView& InView,
    const FPostProcessingInputs& Inputs
)
{
    Inputs.Validate();

    const FViewInfo& View =
        static_cast<const FViewInfo&>(
            InView
            );

    const FIntRect Viewport =
        View.ViewRect;

    if (!Inputs.SceneTextures)
    {
        return;
    }

    const FScreenPassTexture SceneColor(
        (*Inputs.SceneTextures)
        ->SceneColorTexture,
        Viewport
    );

    if (!SceneColor.IsValid())
    {
        return;
    }


    // =========================================================
    // OUTPUT
    // =========================================================

    FRDGTextureDesc OutputDesc =
        SceneColor.Texture->Desc;

    OutputDesc.Flags |=
        TexCreate_RenderTargetable;

    FRDGTextureRef OutputTexture =
        GraphBuilder.CreateTexture(
            OutputDesc,
            TEXT("UAS.Test.SceneColorOutput")
        );

    if (!OutputTexture)
    {
        return;
    }


    // =========================================================
    // SHADER
    // =========================================================

    const FGlobalShaderMap* GlobalShaderMap =
        GetGlobalShaderMap(
            View.GetFeatureLevel()
        );

    TShaderMapRef<FUASTestPixelShader> PixelShader(
        GlobalShaderMap
    );


    FUASTestPixelShader::FParameters*
        PassParameters =
        GraphBuilder.AllocParameters<
        FUASTestPixelShader::FParameters
        >();


    // =========================================================
    // VIEW
    // =========================================================

    PassParameters->View =
        View.ViewUniformBuffer;


    // =========================================================
    // SCENE TEXTURES
    // =========================================================

    PassParameters->SceneTextures =
        CreateSceneTextureShaderParameters(
            GraphBuilder,
            View,
            ESceneTextureSetupMode::All
        );


    // =========================================================
    // SCENE COLOR
    // =========================================================

    PassParameters->SceneColorTexture =
        SceneColor.Texture;

    PassParameters->SceneColorSampler =
        TStaticSamplerState<
        SF_Bilinear,
        AM_Clamp,
        AM_Clamp,
        AM_Clamp
        >::GetRHI();


    // =========================================================
    // VIEWPORT
    // =========================================================

    PassParameters->ViewportMin =
        FVector2f(
            static_cast<float>(
                Viewport.Min.X
                ),
            static_cast<float>(
                Viewport.Min.Y
                )
        );

    PassParameters->ViewportSize =
        FVector2f(
            static_cast<float>(
                Viewport.Width()
                ),
            static_cast<float>(
                Viewport.Height()
                )
        );


    // =========================================================
    // PLANET
    // =========================================================

    const FVector PlanetWorldPosition =
        UPlanetAtmosphereRenderer::
        GetActivePlanetWorldPosition();


    const FVector PlanetTranslatedWorldPosition =
        PlanetWorldPosition
        + View.ViewMatrices.GetPreViewTranslation();


    PassParameters->PlanetCenter =
        FVector3f(
            static_cast<float>(
                PlanetTranslatedWorldPosition.X
                ),
            static_cast<float>(
                PlanetTranslatedWorldPosition.Y
                ),
            static_cast<float>(
                PlanetTranslatedWorldPosition.Z
                )
        );


    PassParameters->AtmosphereRadius =
        UPlanetAtmosphereRenderer::
        GetActiveAtmosphereRadius();


    // =========================================================
    // OUTPUT
    // =========================================================

    PassParameters->RenderTargets[0] =
        FRenderTargetBinding(
            OutputTexture,
            ERenderTargetLoadAction::EClear
        );


    // =========================================================
    // FULLSCREEN PASS
    // =========================================================

    FPixelShaderUtils::AddFullscreenPass(
        GraphBuilder,
        GlobalShaderMap,
        RDG_EVENT_NAME(
            "UAS Test Atmosphere Depth"
        ),
        PixelShader,
        PassParameters,
        Viewport
    );


    // =========================================================
    // COPY BACK
    // =========================================================

    AddCopyTexturePass(
        GraphBuilder,
        OutputTexture,
        SceneColor.Texture
    );
}