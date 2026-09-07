#include "Atmosphere/AndromedaAtmosphereRenderer.h"

#include "AndromedaAtmosphereShader.h"
#include "Atmosphere/AndromedaAtmosphereManager.h"
#include "Atmosphere/AndromedaAtmosphereViewExtension.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RHIFeatureLevel.h"
#include "RHI.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include <atomic>


// =========================================================
// GLOBAL SHADER REGISTRATION
// =========================================================

IMPLEMENT_GLOBAL_SHADER(
    FAndromedaAtmospherePS,
    "/Andromeda/AndromedaAtmosphere.usf",
    "AndromedaAtmosphereMainPS",
    SF_Pixel
);


// =========================================================
// CONSOLE VARIABLE & DISPATCH COUNTER
// =========================================================

namespace
{
    // ATMOS-02: enables/disables the atmosphere diagnostic RDG pass.
    // Default: enabled.
    TAutoConsoleVariable<int> CVarAndromedaAtmosEnable(
        TEXT("r.AndromedaAtmos.Enable"),
        1,
        TEXT("Enable the Andromeda atmosphere diagnostic RDG pass (ATMOS-02). 1 = enabled, 0 = disabled."),
        ECVF_RenderThreadSafe
    );


    // Real RDG dispatch counter of FAndromedaAtmospherePS (render thread).
    std::atomic<uint64> GDispatchCounter{ 0 };
}
// =========================================================
// DIAGNOSTIC CONSOLE COMMAND
// =========================================================

namespace
{
    FAutoConsoleCommand GAndromedaAtmosValidateShaderCommand(
        TEXT("r.AndromedaAtmos.ValidateShader"),
        TEXT("Validates the Andromeda atmosphere global shader (mapping /Andromeda -> [Project]/Shaders/Andromeda) and reports the real RDG dispatch count."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            const bool bValid = FAndromedaAtmosphereRenderer::ValidateShaderInfrastructure();
            const uint64 DispatchCount = FAndromedaAtmosphereRenderer::GetDispatchCount();


            if (bValid)
            {
                UE_LOG(
                    LogAndromedaAtmos,
                    Log,
                    TEXT("[ATMOS-02] Global shader infrastructure VALID: FAndromedaAtmospherePS compiled from /Andromeda/AndromedaAtmosphere.usf. RDG dispatches so far: %llu"),
                    DispatchCount
                );
            }
            else
            {
                UE_LOG(
                    LogAndromedaAtmos,
                    Warning,
                    TEXT("[ATMOS-02] Global shader infrastructure NOT validated. The global shader map may still be compiling: retry in a few seconds. RDG dispatches so far: %llu"),
                    DispatchCount
                );
            }
        })
    );
}