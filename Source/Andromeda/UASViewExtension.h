#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FUASViewExtension
    : public FSceneViewExtensionBase
{
public:

    FUASViewExtension(
        const FAutoRegister& AutoRegister
    );

    virtual bool IsActiveThisFrame_Internal(
        const FSceneViewExtensionContext& Context
    ) const override;

    virtual void PrePostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& InView,
        const FPostProcessingInputs& Inputs
    ) override;
};