#include "Atmosphere/AndromedaAtmosphereViewExtension.h"

#include "Atmosphere/AndromedaAtmosphereRenderer.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"
#include "SceneViewExtension.h"

// =========================================================
// SCENE VIEW EXTENSION
// =========================================================

namespace
{
    // Public UE 5.8 rendering hook: subscribes the Andromeda atmosphere
    // pass to the post-processing chain.
    class FAndromedaAtmosphereSceneViewExtension
        : public FSceneViewExtensionBase
    {
    public:
        FAndromedaAtmosphereSceneViewExtension(
            const FAutoRegister& AutoRegister)
            : FSceneViewExtensionBase(AutoRegister)
        {
        }

        virtual void SubscribeToPostProcessingPass(
            ISceneViewExtension::EPostProcessingPass Pass,
            const FSceneView& InView,
            FPostProcessingPassDelegateArray& InOutPassCallbacks,
            bool bIsPassEnabled) override
        {
            // After the tonemapper the scene color is final: ideal hook point
            // for the Andromeda atmosphere pass.
            //
            // UE 5.8 delegate signature:
            // FScreenPassTexture(
            //     FRDGBuilder&,
            //     const FSceneView&,
            //     const FPostProcessMaterialInputs&
            // )

            if (Pass == ISceneViewExtension::EPostProcessingPass::Tonemap)
            {
                InOutPassCallbacks.Add(
                    FPostProcessingPassDelegate::CreateStatic(
                        &FAndromedaAtmosphereRenderer::RenderAtmospheres
                    )
                );
            }
        }
    };

    TSharedPtr<FAndromedaAtmosphereSceneViewExtension, ESPMode::ThreadSafe>
        GViewExtension;
}

// =========================================================
// REGISTRATION
// =========================================================

void FAndromedaAtmosphereViewExtension::Register()
{
    // PHASE 2.1 DEPRECATED: registering this extension would create a
    // SECOND independent Tonemap hook next to the unified one (double
    // subscription). The unified extension owns the single hook and
    // dispatches the aerial stage itself. Refuse loudly, do nothing.
    UE_LOG(
        LogAndromedaAtmos,
        Warning,
        TEXT("[ATMOS-UNIFIED] FAndromedaAtmosphereViewExtension::Register() is deprecated and ignored: single hook owned by FUnifiedAtmosphereViewExtension.")
    );
}

void FAndromedaAtmosphereViewExtension::Unregister()
{
    if (GViewExtension.IsValid())
    {
        GViewExtension.Reset();

        UE_LOG(
            LogAndromedaAtmos,
            Log,
            TEXT("[ATMOS-02] Scene view extension unregistered.")
        );
    }
}