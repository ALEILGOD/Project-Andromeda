#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

class FUASAtmosphereViewExtension
    : public FSceneViewExtensionBase
{
public:

    FUASAtmosphereViewExtension(
        const FAutoRegister& AutoRegister
    );

    virtual bool IsActiveThisFrame_Internal(
        const FSceneViewExtensionContext& Context
    ) const override;

    virtual void BeginRenderViewFamily(
        FSceneViewFamily& InViewFamily
    ) override;

    virtual void SubscribeToPostProcessingPass(
        EPostProcessingPass Pass,
        const FSceneView& View,
        FAfterPassCallbackDelegateArray& InOutPassCallbacks,
        bool bIsPassEnabled
    ) override;

private:

    FScreenPassTexture PostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs
    );
};