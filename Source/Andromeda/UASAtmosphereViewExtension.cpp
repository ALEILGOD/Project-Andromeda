#include "UASAtmosphereViewExtension.h"

#include "UASAtmosphereRenderer.h"

#include "ScreenPass.h"

FUASAtmosphereViewExtension::FUASAtmosphereViewExtension(
    const FAutoRegister& AutoRegister
)
    : FSceneViewExtensionBase(AutoRegister)
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS] View Extension CREATED")
    );
}

bool FUASAtmosphereViewExtension::IsActiveThisFrame_Internal(
    const FSceneViewExtensionContext& Context
) const
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS] IsActiveThisFrame_Internal")
    );

    return true;
}

void FUASAtmosphereViewExtension::BeginRenderViewFamily(
    FSceneViewFamily& InViewFamily
)
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS] BeginRenderViewFamily")
    );
}

void FUASAtmosphereViewExtension::SubscribeToPostProcessingPass(
    EPostProcessingPass Pass,
    const FSceneView& View,
    FAfterPassCallbackDelegateArray& InOutPassCallbacks,
    bool bIsPassEnabled
)
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "[UAS] SubscribeToPostProcessingPass - Pass=%d Enabled=%s"
        ),
        static_cast<int32>(Pass),
        bIsPassEnabled ? TEXT("TRUE") : TEXT("FALSE")
    );

    if (!bIsPassEnabled)
    {
        return;
    }

    if (Pass == EPostProcessingPass::Tonemap)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[UAS] TONEMAP PASS FOUND - Registering callback")
        );

        InOutPassCallbacks.Add(
            FAfterPassCallbackDelegate::CreateRaw(
                this,
                &FUASAtmosphereViewExtension::PostProcessPass_RenderThread
            )
        );
    }
}

FScreenPassTexture
FUASAtmosphereViewExtension::PostProcessPass_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs
)
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS] POST PROCESS CALLBACK EXECUTED")
    );

    return FUASAtmosphereRenderer::Render(
        GraphBuilder,
        View,
        Inputs
    );
}