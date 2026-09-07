#pragma once

#include "CoreMinimal.h"


class FRDGBuilder;
class FSceneView;
struct FScreenPassTexture;
struct FPostProcessMaterialInputs;


// =========================================================
// ANDROMEDA ATMOSPHERE RENDERER
// =========================================================

class ANDROMEDA_API FAndromedaAtmosphereRenderer
{


public:

    // =========================================================
    // LIFECYCLE
    // =========================================================

    // Registers the virtual shader directory mapping:
    //     /Andromeda -> [Project]/Shaders/Andromeda
    // and hooks the atmosphere pass into the post-processing chain
    // through the public scene view extension API.
    static void Initialize();


    static void Shutdown();


    static bool IsInitialized();


    // =========================================================
    // RENDERING (ATMOS-02)
    // =========================================================

    // Post-processing delegate (SceneColor in -> Andromeda pass -> output).
    // Dispatches FAndromedaAtmospherePS as a real fullscreen RDG pass
    // through FPixelShaderUtils::AddFullscreenPass.
    static FScreenPassTexture RenderAtmospheres(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs);


    // Number of real RDG dispatches of FAndromedaAtmospherePS since startup.
    static uint64 GetDispatchCount();


    // =========================================================
    // DIAGNOSTICS
    // =========================================================

    // Diagnostic: verifies
    //     AndromedaAtmosphere.usf -> registration -> compilation
    // Also exposed via console command: r.AndromedaAtmos.ValidateShader
    static bool ValidateShaderInfrastructure();


private:

    static void RegisterShaderDirectoryMapping();

    // Runs ValidateShaderInfrastructure once engine init completed
    // (RHI available, global shader map building). Logs the result.
    static void HandlePostEngineInit();

    static FDelegateHandle PostEngineInitDelegateHandle;


    static bool bInitialized;
};