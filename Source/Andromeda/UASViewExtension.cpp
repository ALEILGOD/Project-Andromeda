#include "UASViewExtension.h"

#include "UASTestRenderer.h"

#include "RenderGraphBuilder.h"

FUASViewExtension::FUASViewExtension(
    const FAutoRegister& AutoRegister
)
    : FSceneViewExtensionBase(AutoRegister)
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS TEST] View Extension CREATED")
    );
}

bool FUASViewExtension::IsActiveThisFrame_Internal(
    const FSceneViewExtensionContext& Context
) const
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS TEST] IsActiveThisFrame_Internal")
    );

    return true;
}

void FUASViewExtension::PrePostProcessPass_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& InView,
    const FPostProcessingInputs& Inputs
)
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS TEST] PrePostProcessPass_RenderThread EXECUTED")
    );

    FUASTestRenderer::Render(
        GraphBuilder,
        InView,
        Inputs
    );
}