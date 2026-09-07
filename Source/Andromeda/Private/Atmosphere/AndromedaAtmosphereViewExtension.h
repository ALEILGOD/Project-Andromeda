#pragma once

#include "CoreMinimal.h"


// =========================================================
// ANDROMEDA ATMOSPHERE VIEW EXTENSION
// =========================================================

// Registration helpers for the Andromeda atmosphere scene view extension.
// Uses the public UE 5.8 ISceneViewExtension hook (no engine source changes):
//     SubscribeToPostProcessingPass(EPostProcessingPass::Tonemap, ...)
// which injects the Andromeda atmosphere RDG pass into the standard
// post-processing chain (SceneColor in -> Andromeda pass -> SceneColor out).
//
// The extension stays registered while the internal shared reference is
// alive; Unregister() releases it (documented lifetime pattern of
// FSceneViewExtensions::NewExtension).

class FAndromedaAtmosphereViewExtension
{


public:

    static void Register();

    static void Unregister();
};