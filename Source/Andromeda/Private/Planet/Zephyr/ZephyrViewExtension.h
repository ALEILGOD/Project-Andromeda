#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

// =========================================================
// ZEPHYR VIEW EXTENSION (ZEPHYR-01 section 11)
// =========================================================
// Hooks the ZEPHYR sky pass into the standard post-processing
// chain via the public UE 5.8 ISceneViewExtension interface
// (SubscribeToPostProcessingPass, Tonemap).
//
// Because the hook lives at the FSceneView level - never in a
// PlayerController, Character or CameraManager, and never
// requiring a special camera - EVERY view receives the sky:
// game camera, PIE, editor viewport camera and photo-mode
// camera. The extension stays alive while its shared reference
// is held; Unregister() releases it.
class FZephyrViewExtension
{
public:
    static void Register();
    static void Unregister();
};
