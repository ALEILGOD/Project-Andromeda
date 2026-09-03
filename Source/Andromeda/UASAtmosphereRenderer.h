#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

class FUASAtmosphereRenderer
{
public:

    static FScreenPassTexture Render(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs
    );
};