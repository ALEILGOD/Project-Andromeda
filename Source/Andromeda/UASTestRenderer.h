#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessingInputs;

class FUASTestRenderer
{
public:

    static void Render(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessingInputs& Inputs
    );
};