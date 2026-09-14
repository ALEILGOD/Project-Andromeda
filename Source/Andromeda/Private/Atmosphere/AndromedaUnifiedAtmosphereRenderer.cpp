#include "Atmosphere/AndromedaUnifiedAtmosphereRenderer.h"
#include "Atmosphere/AndromedaUnifiedAtmosphereViewExtension.h"

#include "Atmosphere/AndromedaAtmosphereRenderer.h"
#include "Atmosphere/AndromedaAtmosphereSystem.h"
#include "Planet/Zephyr/ZephyrRenderer.h"

#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Engine/World.h"

#include <atomic>

// =========================================================
// UNIFIED RENDERER STATE (render-thread counters + frame info)
// =========================================================

namespace
{
    std::atomic<uint64> GUnifiedAerialPassCount{ 0 };
    std::atomic<uint64> GUnifiedSkyPassCount{ 0 };

    FCriticalSection GFrameInfoLock;
    TArray<FUnifiedAtmosphereFramePlanetInfo> GFramePlanets;
    int32 GFrameGoverningIndex = -1;
    uint64 GFrameSnapshotVersion = 0;

    // Cached pointer to r.AndromedaAtmosphere.Enable (declared in
    // AndromedaAtmosphereUnifiedCommands.cpp). Resolved lazily so
    // static-init order between translation units cannot matter.
    IConsoleVariable* GUnifiedEnableCVar = nullptr;

    // Single unified view extension: subscribes BOTH ordered stage
    // delegates (Aerial -> Sky) on the same Tonemap hook. UE chains
    // delegates in subscription order, so the sky stage receives the
    // aerial stage output as its SceneColor deterministically.
    class FUnifiedAtmosphereSceneViewExtension : public FSceneViewExtensionBase
    {
    public:
        FUnifiedAtmosphereSceneViewExtension(const FAutoRegister& AutoRegister)
            : FSceneViewExtensionBase(AutoRegister)
        {
        }

        virtual void SubscribeToPostProcessingPass(
            ISceneViewExtension::EPostProcessingPass Pass,
            const FSceneView& InView,
            FPostProcessingPassDelegateArray& InOutPassCallbacks,
            bool bIsPassEnabled) override
        {
            if (Pass == ISceneViewExtension::EPostProcessingPass::Tonemap)
            {
                InOutPassCallbacks.Add(
                    FPostProcessingPassDelegate::CreateStatic(
                        &FUnifiedAtmosphereRenderer::RenderAerialStage
                    )
                );
                InOutPassCallbacks.Add(
                    FPostProcessingPassDelegate::CreateStatic(
                        &FUnifiedAtmosphereRenderer::RenderSkyStage
                    )
                );
            }
        }
    };

    TSharedPtr<FUnifiedAtmosphereSceneViewExtension, ESPMode::ThreadSafe>
        GUnifiedViewExtension;
}

// =========================================================
// LIFECYCLE
// =========================================================

bool FUnifiedAtmosphereRenderer::bInitialized = false;

FDelegateHandle
FUnifiedAtmosphereRenderer::PostEngineInitDelegateHandle;

FDelegateHandle
FUnifiedAtmosphereRenderer::WorldCleanupDelegateHandle;

void FUnifiedAtmosphereRenderer::Initialize()
{
    if (bInitialized)
    {
        return;
    }

    check(IsInGameThread());

    PostEngineInitDelegateHandle =
        FCoreDelegates::GetOnPostEngineInit().AddStatic(
            &FUnifiedAtmosphereRenderer::HandlePostEngineInit
        );

    bInitialized = true;

    UE_LOG(
        LogAndromedaAtmosphere,
        Log,
        TEXT("[ATMOS-UNIFIED] FUnifiedAtmosphereRenderer initialized. Single Tonemap owner (Aerial -> Sky stages).")
    );
}

void FUnifiedAtmosphereRenderer::Shutdown()
{
    if (!bInitialized)
    {
        return;
    }

    FCoreDelegates::GetOnPostEngineInit().Remove(
        PostEngineInitDelegateHandle
    );

    FWorldDelegates::OnWorldCleanup.Remove(
        WorldCleanupDelegateHandle
    );

    WorldCleanupDelegateHandle.Reset();

    FUnifiedAtmosphereViewExtension::Unregister();

    bInitialized = false;
}

bool FUnifiedAtmosphereRenderer::IsInitialized()
{
    return bInitialized;
}

void FUnifiedAtmosphereRenderer::HandlePostEngineInit()
{
    FUnifiedAtmosphereViewExtension::Register();

    WorldCleanupDelegateHandle =
        FWorldDelegates::OnWorldCleanup.AddStatic(
            &FUnifiedAtmosphereRenderer::HandleWorldCleanup
        );

    UE_LOG(
        LogAndromedaAtmosphere,
        Log,
        TEXT("[ATMOS-UNIFIED] Unified view extension registered (single Tonemap hook, Aerial -> Sky).")
    );
}

void FUnifiedAtmosphereRenderer::HandleWorldCleanup(
    UWorld* InWorld,
    bool bSessionEnded,
    bool bCleanupResources
)
{
    // Mailbox lifetime is owned by AAndromedaAtmosphereRegistry::EndPlay;
    // LUT history is owned by the sky stage. Reset only the per-frame
    // diagnostic so no stale governing data outlives its session.
    FScopeLock ScopeLock(&GFrameInfoLock);
    GFramePlanets.Empty();
    GFrameGoverningIndex = -1;
    GFrameSnapshotVersion = 0;
}

bool FUnifiedAtmosphereRenderer::UnifiedPassEnabled()
{
    if (GUnifiedEnableCVar == nullptr)
    {
        GUnifiedEnableCVar = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.AndromedaAtmosphere.Enable")
        );
        if (GUnifiedEnableCVar == nullptr)
        {
            // Unified console not linked yet (should never happen after
            // module startup): fail open so the stages' own gates decide.
            return true;
        }
    }
    return GUnifiedEnableCVar->GetInt() != 0;
}

// =========================================================
// ORDERED STAGES (single snapshot, single owner)
// =========================================================

FScreenPassTexture FUnifiedAtmosphereRenderer::RenderAerialStage(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs)
{
    UpdateLastFrameInfo(View);

    if (!bInitialized || !UnifiedPassEnabled())
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    ++GUnifiedAerialPassCount;

    // Legacy ATMOS raymarch reused as the aerial/limb stage under
    // unified ownership: it reads the unified snapshot (Rayleigh
    // view derived from the full-physics profile, see
    // FAndromedaAtmosphereRenderer::RenderAtmospheres).
    return FAndromedaAtmosphereRenderer::RenderAtmospheres(
        GraphBuilder,
        View,
        Inputs
    );
}

FScreenPassTexture FUnifiedAtmosphereRenderer::RenderSkyStage(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs)
{
    if (!bInitialized || !UnifiedPassEnabled())
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    ++GUnifiedSkyPassCount;

    // Hillaire Case A LUT pipeline (Transmittance -> MultiScatter ->
    // SkyView -> Sky PS) reading the same unified snapshot.
    return FZephyrRenderer::RenderSky(
        GraphBuilder,
        View,
        Inputs
    );
}

// =========================================================
// RUNTIME PROOF STATE
// =========================================================

uint64 FUnifiedAtmosphereRenderer::GetAerialPassCount()
{
    return GUnifiedAerialPassCount.load();
}

uint64 FUnifiedAtmosphereRenderer::GetSkyPassCount()
{
    return GUnifiedSkyPassCount.load();
}

void FUnifiedAtmosphereRenderer::UpdateLastFrameInfo(const FSceneView& View)
{
    TArray<FAndromedaAtmosphereInstance> Snapshot;
    FVector StarWorldPosition = FVector::ZeroVector;
    uint64 SnapshotVersion = 0;

    FAndromedaAtmosphereSystem::Get().GetSnapshot(
        Snapshot,
        StarWorldPosition,
        SnapshotVersion
    );

    const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();

    TArray<FUnifiedAtmosphereFramePlanetInfo> FramePlanets;
    FramePlanets.Reserve(Snapshot.Num());

    int32 GoverningIndex = -1;
    double BestSurfaceDistance = TNumericLimits<double>::Max();

    for (int32 i = 0; i < Snapshot.Num(); ++i)
    {
        const FAndromedaAtmosphereInstance& Entry = Snapshot[i];

        FUnifiedAtmosphereFramePlanetInfo Info;
        Info.PlanetID = Entry.PlanetID;

        // Double precision until the stored values (CPU only).
        const FVector RelCenter = Entry.PlanetCenter - ViewOrigin;
        const double DistanceCm = RelCenter.Length();
        Info.DistanceCm = DistanceCm;
        Info.HeightAboveGroundCm =
            DistanceCm - static_cast<double>(Entry.Profile.GroundRadius);
        Info.bInsideAtmosphere =
            DistanceCm < static_cast<double>(Entry.Profile.AtmosphereRadius);

        FramePlanets.Add(Info);

        // Approximate governing planet: smallest surface distance
        // (camera closest above the outer shell). The sky shader
        // applies the exact zone/fade selection per pixel; this is
        // the render-thread diagnostic approximation.
        const double SurfaceDistance =
            DistanceCm - static_cast<double>(Entry.Profile.AtmosphereRadius);
        if (SurfaceDistance < BestSurfaceDistance)
        {
            BestSurfaceDistance = SurfaceDistance;
            GoverningIndex = i;
        }
    }

    FScopeLock ScopeLock(&GFrameInfoLock);
    GFramePlanets = MoveTemp(FramePlanets);
    GFrameGoverningIndex = GoverningIndex;
    GFrameSnapshotVersion = SnapshotVersion;
}

void FUnifiedAtmosphereRenderer::GetLastFrameInfo(
    TArray<FUnifiedAtmosphereFramePlanetInfo>& OutPlanets,
    int32& OutGoverningIndex,
    uint64& OutSnapshotVersion
)
{
    FScopeLock ScopeLock(&GFrameInfoLock);
    OutPlanets = GFramePlanets;
    OutGoverningIndex = GFrameGoverningIndex;
    OutSnapshotVersion = GFrameSnapshotVersion;
}

// =========================================================
// UNIFIED VIEW EXTENSION REGISTRATION
// =========================================================

void FUnifiedAtmosphereViewExtension::Register()
{
    if (GUnifiedViewExtension.IsValid())
    {
        return;
    }

    GUnifiedViewExtension =
        FSceneViewExtensions::NewExtension<
            FUnifiedAtmosphereSceneViewExtension>();

    UE_LOG(
        LogAndromedaAtmosphere,
        Log,
        TEXT("[ATMOS-UNIFIED] Scene view extension registered: single Tonemap hook (Aerial -> Sky).")
    );
}

void FUnifiedAtmosphereViewExtension::Unregister()
{
    if (GUnifiedViewExtension.IsValid())
    {
        GUnifiedViewExtension.Reset();

        UE_LOG(
            LogAndromedaAtmosphere,
            Log,
            TEXT("[ATMOS-UNIFIED] Scene view extension unregistered.")
        );
    }
}

bool FUnifiedAtmosphereViewExtension::IsRegistered()
{
    return GUnifiedViewExtension.IsValid();
}
