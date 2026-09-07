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
    // pass to the post-processing chain. Runs on the render thread with
    // the frame FRDGBuilder; no UObject access involved.
    class FAndromedaAtmosphereSceneViewExtension
        : public FSceneViewExtensionBase
    {


    public:

        FAndromedaAtmosphereSceneViewExtension(const FAutoRegister& AutoRegister)
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
            // for the ATMOS-02 diagnostic pass. The delegate signature is
            // FScreenPassTexture(FRDGBuilder&, const FSceneView&, const FPostProcessMaterialInputs&).
            if (Pass == ISceneViewExtension::EPostProcessingPass::Tonemap && bIsPassEnabled)
            {
                InOutPassCallbacks.Add(
                    FPostProcessingPassDelegate::CreateStatic(
                        &FAndromedaAtmosphereRenderer::RenderAtmospheres
                    )
                );
            }
        }
    };


    TSharedPtr<FAndromedaAtmosphereSceneViewExtension, ESPMode::ThreadSafe> GViewExtension;
}


// =========================================================
// REGISTRATION
// =========================================================

void FAndromedaAtmosphereViewExtension::Register()
{
    if (GViewExtension.IsValid())
    {
        return;
    }


    GViewExtension = FSceneViewExtensions::NewExtension<FAndromedaAtmosphereSceneViewExtension>();


    UE_LOG(
        LogAndromedaAtmos,
        Log,
        TEXT("[ATMOS-02] Scene view extension registered: atmosphere pass hooked to the post-processing chain (after tonemap).")
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