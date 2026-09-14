#include "Planet/Zephyr/ZephyrViewExtension.h"

#include "Planet/Zephyr/ZephyrRenderer.h"
#include "Planet/Zephyr/ZephyrTypes.h"
#include "SceneViewExtension.h"

namespace
{
    class FZephyrSceneViewExtension
        : public FSceneViewExtensionBase
    {
    public:
        FZephyrSceneViewExtension(
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
            // Same hook point as the ATMOS foreground pass. The
            // two compose order-independently by construction:
            // ZEPHYR overwrites sky pixels and passes geometry
            // pixels through, so whichever runs first, the final
            // image is opaque > ATMOS volume > ZEPHYR sky.
            if (Pass == ISceneViewExtension::EPostProcessingPass::Tonemap)
            {
                InOutPassCallbacks.Add(
                    FPostProcessingPassDelegate::CreateStatic(
                        &FZephyrRenderer::RenderSky
                    )
                );
            }
        }
    };

    TSharedPtr<FZephyrSceneViewExtension, ESPMode::ThreadSafe>
        GZephyrViewExtension;
}

void FZephyrViewExtension::Register()
{
    // PHASE 2.1 DEPRECATED: registering this extension would create a
    // SECOND independent Tonemap hook next to the unified one (double
    // subscription). The unified extension owns the single hook and
    // dispatches the sky stage itself. Refuse loudly, do nothing.
    UE_LOG(
        LogAndromedaZephyr,
        Warning,
        TEXT("[ATMOS-UNIFIED] FZephyrViewExtension::Register() is deprecated and ignored: single hook owned by FUnifiedAtmosphereViewExtension.")
    );
}

void FZephyrViewExtension::Unregister()
{
    if (GZephyrViewExtension.IsValid())
    {
        GZephyrViewExtension.Reset();

        UE_LOG(
            LogAndromedaZephyr,
            Log,
            TEXT("[ZEPHYR-01] Scene view extension unregistered.")
        );
    }
}
