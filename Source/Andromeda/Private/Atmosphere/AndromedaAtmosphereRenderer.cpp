#include "Atmosphere/AndromedaAtmosphereRenderer.h"

#include "AndromedaAtmosphereShader.h"

#include "Atmosphere/AndromedaAtmosphereManager.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIFeatureLevel.h"
#include "RHI.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"

#include "Atmosphere/AndromedaAtmosphereViewExtension.h"

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
    TAutoConsoleVariable<int> CVarAndromedaAtmosEnable(
        TEXT("r.AndromedaAtmos.Enable"),
        1,
        TEXT("Enable the Andromeda atmosphere diagnostic RDG pass (ATMOS-02). 1 = enabled, 0 = disabled."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<int> CVarAndromedaAtmosDebugVolume(
        TEXT("r.AndromedaAtmos.DebugVolume"),
        1,
        TEXT("ATMOS-03: enable the atmosphere volume/intersection diagnostic overlay. 0 = off, 1 = on (shows atmosphere sphere intersections)."),
        ECVF_RenderThreadSafe
    );


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
            const bool bValid =
                FAndromedaAtmosphereRenderer::ValidateShaderInfrastructure();

            const uint64 DispatchCount =
                FAndromedaAtmosphereRenderer::GetDispatchCount();

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

// =========================================================
// LIFECYCLE
// =========================================================

bool FAndromedaAtmosphereRenderer::bInitialized = false;

FDelegateHandle
FAndromedaAtmosphereRenderer::PostEngineInitDelegateHandle;

FDelegateHandle
FAndromedaAtmosphereRenderer::WorldCleanupDelegateHandle;

void FAndromedaAtmosphereRenderer::Initialize()
{
    if (bInitialized)
    {
        return;
    }

    check(IsInGameThread());

    RegisterShaderDirectoryMapping();

    PostEngineInitDelegateHandle =
        FCoreDelegates::GetOnPostEngineInit().AddStatic(
            &FAndromedaAtmosphereRenderer::HandlePostEngineInit
        );

    bInitialized = true;

    UE_LOG(
        LogAndromedaAtmos,
        Log,
        TEXT("[ATMOS-02] FAndromedaAtmosphereRenderer initialized. Virtual shader directory '/Andromeda' mapped.")
    );
}

void FAndromedaAtmosphereRenderer::Shutdown()
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

    FAndromedaAtmosphereViewExtension::Unregister();

    FAndromedaAtmosphereManager::Get().Clear();

    bInitialized = false;
}

bool FAndromedaAtmosphereRenderer::IsInitialized()
{
    return bInitialized;
}

void FAndromedaAtmosphereRenderer::RegisterShaderDirectoryMapping()
{
    const FString VirtualShaderDirectory = TEXT("/Andromeda");

    const FString RealShaderDirectory =
        FPaths::Combine(
            FPaths::ProjectDir(),
            TEXT("Shaders"),
            TEXT("Andromeda")
        );

    checkf(
        FPaths::DirectoryExists(RealShaderDirectory),
        TEXT("Andromeda atmosphere shader directory is missing: %s"),
        *RealShaderDirectory
    );

    AddShaderSourceDirectoryMapping(
        VirtualShaderDirectory,
        RealShaderDirectory
    );
}

// =========================================================
// GPU DATA CONVERSION
// =========================================================

void FAndromedaAtmosphereRenderer::BuildGPUData(
    const TArray<FAndromedaAtmosphereInstance>& Snapshot,
    TArray<FAndromedaAtmosphereGPUData>& OutGPUData)
{
    OutGPUData.Reset(Snapshot.Num());

    for (const FAndromedaAtmosphereInstance& Instance : Snapshot)
    {
        FAndromedaAtmosphereGPUData GPUData = {};

        GPUData.CenterX =
            static_cast<float>(Instance.WorldPosition.X);

        GPUData.CenterY =
            static_cast<float>(Instance.WorldPosition.Y);

        GPUData.CenterZ =
            static_cast<float>(Instance.WorldPosition.Z);

        GPUData.SurfaceRadius =
            Instance.Parameters.SurfaceRadius;

        GPUData.AtmosphereRadius =
            Instance.Parameters.AtmosphereRadius;

        GPUData.Pad0 = 0.0f;
        GPUData.Pad1 = 0.0f;
        GPUData.Pad2 = 0.0f;

        // ATMOS-06: Rayleigh scattering parameters
        GPUData.RayleighScatteringX =
            Instance.Parameters.RayleighScattering.X;
        GPUData.RayleighScatteringY =
            Instance.Parameters.RayleighScattering.Y;
        GPUData.RayleighScatteringZ =
            Instance.Parameters.RayleighScattering.Z;
        GPUData.RayleighScaleHeight =
            Instance.Parameters.RayleighScaleHeight;

        GPUData.Pad3 = 0.0f;
        GPUData.Pad4 = 0.0f;
        GPUData.Pad5 = 0.0f;
    }
}

// =========================================================
// RENDERING
// ATMOS-04: camera-relative stable ray march, 16 steps
// ATMOS-05: finite-position point-star transport
// ATMOS-06: Rayleigh scattering with exponential density and phase function
// =========================================================

FScreenPassTexture FAndromedaAtmosphereRenderer::RenderAtmospheres(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs)
{
    if (
        !bInitialized ||
        CVarAndromedaAtmosEnable.GetValueOnRenderThread() == 0
    )
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    // --------------------------------------------------------
    // Step 1: get the atmosphere snapshot from the manager.
    // --------------------------------------------------------

    TArray<FAndromedaAtmosphereInstance> AtmosphereSnapshot;

    FAndromedaAtmosphereManager::Get().GetAtmosphereSnapshot(
        AtmosphereSnapshot
    );

    const int32 AtmosphereCount =
        AtmosphereSnapshot.Num();

    // --------------------------------------------------------
    // Step 2: camera position.
    //
    // ATMOS-04 NUMERICAL STABILIZATION:
    // The following values remain in FVector/double precision
    // until the final small camera-relative result is converted
    // to float.
    // --------------------------------------------------------

    const FVector ViewOrigin =
        View.ViewMatrices.GetViewOrigin();

    // --------------------------------------------------------
    // ATMOS-05 STAR SOURCE
    //
    // The star position is read from the Game Thread snapshot
    // written into FAndromedaAtmosphereManager. The renderer
    // must never touch UWorld / AStarSystem from the render
    // thread, so any TActorIterator access has been removed.
    //
    // A valid star position is available when the Game Thread
    // has written one since the last clear. The renderer only
    // uses the copied value here.
    // --------------------------------------------------------

    const FVector StarWorldPosition =
        FAndromedaAtmosphereManager::Get().GetStarWorldPosition();

    const bool bHasStarPosition =
        StarWorldPosition != FVector::ZeroVector;

    const FVector StarRelativeToCamera =
        bHasStarPosition
            ? (StarWorldPosition - ViewOrigin)
            : FVector::ZeroVector;

    // --------------------------------------------------------
    // Step 3: CPU snapshot -> packed GPU data.
    //
    // Both atmosphere center and star position are calculated
    // camera-relative in FVector precision BEFORE conversion
    // to float.
    // --------------------------------------------------------

    TArray<FAndromedaAtmosphereGPUData> GPUData;

    GPUData.Reset(
        AtmosphereSnapshot.Num()
    );

    for (
        const FAndromedaAtmosphereInstance& Instance :
        AtmosphereSnapshot
    )
    {
        FAndromedaAtmosphereGPUData RelGPUData = {};

        // ATMOS-04:
        // center - camera in CPU double precision.

        const FVector RelCenter =
            Instance.WorldPosition - ViewOrigin;

        RelGPUData.CenterX =
            static_cast<float>(RelCenter.X);

        RelGPUData.CenterY =
            static_cast<float>(RelCenter.Y);

        RelGPUData.CenterZ =
            static_cast<float>(RelCenter.Z);

        RelGPUData.SurfaceRadius =
            Instance.Parameters.SurfaceRadius;

        RelGPUData.AtmosphereRadius =
            Instance.Parameters.AtmosphereRadius;

        RelGPUData.Pad0 = 0.0f;
        RelGPUData.Pad1 = 0.0f;
        RelGPUData.Pad2 = 0.0f;

        // ATMOS-05:
        // StarWorldPosition - CameraWorldPosition,
        // calculated in FVector/double before conversion
        // to float.
        //
        // The shader's RayOrigin is exactly zero in the
        // camera-relative frame, so this becomes the star's
        // position relative to the camera.

        RelGPUData.StarPositionX =
            static_cast<float>(StarRelativeToCamera.X);

        RelGPUData.StarPositionY =
            static_cast<float>(StarRelativeToCamera.Y);

        RelGPUData.StarPositionZ =
            static_cast<float>(StarRelativeToCamera.Z);

        // ATMOS-06: Rayleigh scattering parameters
        RelGPUData.RayleighScatteringX =
            Instance.Parameters.RayleighScattering.X;
        RelGPUData.RayleighScatteringY =
            Instance.Parameters.RayleighScattering.Y;
        RelGPUData.RayleighScatteringZ =
            Instance.Parameters.RayleighScattering.Z;
        RelGPUData.RayleighScaleHeight =
            Instance.Parameters.RayleighScaleHeight;

        RelGPUData.Pad3 = 0.0f;
        RelGPUData.Pad4 = 0.0f;
        RelGPUData.Pad5 = 0.0f;

        GPUData.Add(RelGPUData);
    }

    const FScreenPassTextureSlice SceneColorSlice =
        Inputs.GetInput(
            EPostProcessMaterialInput::SceneColor
        );

    if (!SceneColorSlice.TextureSRV)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    FScreenPassTexture SceneColor =
        FScreenPassTexture::CopyFromSlice(
            GraphBuilder,
            SceneColorSlice,
            Inputs.OverrideOutput
        );

    if (!SceneColor.Texture)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    // --------------------------------------------------------
    // Output
    // --------------------------------------------------------

    FScreenPassRenderTarget Output;

    ERenderTargetLoadAction OutputLoadAction =
        ERenderTargetLoadAction::ELoad;

    if (Inputs.OverrideOutput.IsValid())
    {
        Output = Inputs.OverrideOutput;

        OutputLoadAction =
            ERenderTargetLoadAction::ELoad;
    }
    else
    {
        FRDGTextureDesc OutputDesc =
            SceneColor.Texture->Desc;

        OutputDesc.Flags |=
            ETextureCreateFlags::RenderTargetable;

        Output =
            FScreenPassRenderTarget(
                GraphBuilder.CreateTexture(
                    OutputDesc,
                    TEXT("AndromedaAtmosphereOutput")
                ),
                SceneColor.ViewRect,
                ERenderTargetLoadAction::ENoAction
            );

        OutputLoadAction =
            ERenderTargetLoadAction::ENoAction;
    }

    FGlobalShaderMap* GlobalShaderMap =
        GetGlobalShaderMap(GMaxRHIFeatureLevel);

    if (!GlobalShaderMap)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    FAndromedaAtmospherePS::FParameters* PassParameters =
        GraphBuilder.AllocParameters<
            FAndromedaAtmospherePS::FParameters
        >();

    PassParameters->ViewportSize =
        Output.ViewRect.Size();

    PassParameters->SceneColorTexture =
        SceneColor.Texture;

    PassParameters->SceneColorSampler =
        TStaticSamplerState<
            SF_Bilinear,
            AM_Clamp,
            AM_Clamp,
            AM_Clamp
        >::GetRHI();

    PassParameters->RenderTargets[0] =
        FRenderTargetBinding(
            Output.Texture,
            OutputLoadAction
        );

    // --------------------------------------------------------
    // ATMOS-04 DEPTH OCCLUSION
    //
    // UNCHANGED.
    // --------------------------------------------------------

    PassParameters->SceneTexturesStruct =
        Inputs.SceneTextures.SceneTextures;

    PassParameters->ViewUniformBuffer =
        View.ViewUniformBuffer;

    PassParameters->DepthOcclusionEnabled =
        Inputs.SceneTextures.SceneTextures
            ? 1
            : 0;

    // --------------------------------------------------------
    // Step 4: atmosphere data -> RDG StructuredBuffer.
    // --------------------------------------------------------

    PassParameters->AtmosphereCount =
        AtmosphereCount;

    const uint32 NumElements =
        FMath::Max(
            1u,
            static_cast<uint32>(GPUData.Num())
        );

    FRDGBufferDesc BufferDesc =
        FRDGBufferDesc::CreateStructuredDesc(
            sizeof(FAndromedaAtmosphereGPUData),
            NumElements
        );

    FRDGBufferRef AtmosphereBuffer =
        GraphBuilder.CreateBuffer(
            BufferDesc,
            TEXT("AndromedaAtmosphereBuffer"),
            ERDGBufferFlags::None
        );

    const FAndromedaAtmosphereGPUData DummyAtmosphere = {};

    const FAndromedaAtmosphereGPUData* UploadData =
        (GPUData.Num() > 0)
            ? GPUData.GetData()
            : &DummyAtmosphere;

    const uint64 UploadBytes =
        (GPUData.Num() > 0)
            ? static_cast<uint64>(
                GPUData.Num() *
                sizeof(FAndromedaAtmosphereGPUData)
            )
            : static_cast<uint64>(
                sizeof(FAndromedaAtmosphereGPUData)
            );

    GraphBuilder.QueueBufferUpload(
        AtmosphereBuffer,
        UploadData,
        UploadBytes,
        ERDGInitialDataFlags::None
    );

    PassParameters->AtmosphereBuffer =
        GraphBuilder.CreateSRV(
            FRDGBufferSRVDesc(AtmosphereBuffer)
        );

    // --------------------------------------------------------
    // Step 5: camera data.
    //
    // ATMOS-04 EXACT NUMERICAL STABILIZATION.
    //
    // DO NOT replace this with an ordinary float inverse
    // view-projection reconstruction.
    // --------------------------------------------------------

    const FMatrix& ClipToWorldD =
        View.ViewMatrices.GetClipToWorld();

    const FVector& ViewOriginD =
        View.ViewMatrices.GetViewOrigin();

    FMatrix44f RelClipToWorld;

    for (int32 Row = 0; Row < 4; ++Row)
    {
        const double Wd =
            ClipToWorldD.M[Row][3];

        RelClipToWorld.M[Row][0] =
            static_cast<float>(
                ClipToWorldD.M[Row][0] -
                ViewOriginD.X * Wd
            );

        RelClipToWorld.M[Row][1] =
            static_cast<float>(
                ClipToWorldD.M[Row][1] -
                ViewOriginD.Y * Wd
            );

        RelClipToWorld.M[Row][2] =
            static_cast<float>(
                ClipToWorldD.M[Row][2] -
                ViewOriginD.Z * Wd
            );

        RelClipToWorld.M[Row][3] =
            static_cast<float>(Wd);
    }

    PassParameters->CameraWorldPosition =
        FVector3f(
            View.ViewMatrices.GetViewOrigin()
        );

    PassParameters->InvViewProjection =
        RelClipToWorld;

    TShaderMapRef<FAndromedaAtmospherePS> PixelShader(
        GlobalShaderMap
    );

    FPixelShaderUtils::AddFullscreenPass<FAndromedaAtmospherePS>(
        GraphBuilder,
        GlobalShaderMap,
        RDG_EVENT_NAME("AndromedaAtmosphereDiagnostic"),
        PixelShader,
        PassParameters,
        Output.ViewRect
    );

    const uint64 DispatchId =
        GDispatchCounter.fetch_add(1) + 1;

    if (DispatchId == 1)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Log,
            TEXT("[ATMOS-05] First real RDG dispatch with finite point-star transport: atmosphere ray-march pass is live on the GPU. Atmospheres: %d | Star position available: %d"),
            AtmosphereCount,
            bHasStarPosition ? 1 : 0
        );
    }

    return FScreenPassTexture(
        Output.Texture,
        Output.ViewRect
    );
}

uint64 FAndromedaAtmosphereRenderer::GetDispatchCount()
{
    return GDispatchCounter.load();
}

// =========================================================
// DIAGNOSTICS
// =========================================================

bool FAndromedaAtmosphereRenderer::ValidateShaderInfrastructure()
{
    if (!bInitialized)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("ValidateShaderInfrastructure: renderer not initialized.")
        );

        return false;
    }

    if (IsRunningCommandlet())
    {
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("ValidateShaderInfrastructure: not available in commandlets.")
        );

        return false;
    }

    FGlobalShaderMap* GlobalShaderMap =
        GetGlobalShaderMap(GMaxRHIFeatureLevel);

    if (!GlobalShaderMap)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("ValidateShaderInfrastructure: global shader map unavailable.")
        );

        return false;
    }

    const bool bPixelShaderCompiled =
        GlobalShaderMap->HasShader(
            &FAndromedaAtmospherePS::GetStaticType(),
            0
        );

    UE_CLOG(
        !bPixelShaderCompiled,
        LogAndromedaAtmos,
        Log,
        TEXT("ValidateShaderInfrastructure: FAndromedaAtmospherePS not in the global shader map yet.")
    );

    return bPixelShaderCompiled;
}

void FAndromedaAtmosphereRenderer::HandlePostEngineInit()
{
    FAndromedaAtmosphereViewExtension::Register();

    WorldCleanupDelegateHandle =
        FWorldDelegates::OnWorldCleanup.AddStatic(
            &FAndromedaAtmosphereRenderer::HandleWorldCleanup
        );

    const bool bValid =
        ValidateShaderInfrastructure();

    if (bValid)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Log,
            TEXT("[ATMOS-02] Global shader infrastructure VALID (checked after engine init).")
        );
    }
    else
    {
        UE_LOG(
            LogAndromedaAtmos,
            Log,
            TEXT("[ATMOS-02] Global shader infrastructure not validated at engine init. Use 'r.AndromedaAtmos.ValidateShader' once the global shader map finished compiling.")
        );
    }
}

// =========================================================
// ATMOS-LIFETIME: WORLD TEARDOWN CLEANUP
// =========================================================

void FAndromedaAtmosphereRenderer::HandleWorldCleanup(
    UWorld* InWorld,
    bool bSessionEnded,
    bool bCleanupResources)
{
    UE_LOG(
        LogAndromedaAtmos,
        Log,
        TEXT("[ATMOS-LIFETIME] Registry cleanup started (world id: %d, session ended: %d, cleanup resources: %d)."),
        InWorld
            ? static_cast<int32>(InWorld->GetUniqueID())
            : -1,
        bSessionEnded ? 1 : 0,
        bCleanupResources ? 1 : 0
    );

    const int32 RemovedCount =
        FAndromedaAtmosphereManager::Get().Clear();

    UE_LOG(
        LogAndromedaAtmos,
        Log,
        TEXT("[ATMOS-LIFETIME] Removed %d atmosphere instances."),
        RemovedCount
    );

    UE_LOG(
        LogAndromedaAtmos,
        Log,
        TEXT("[ATMOS-LIFETIME] Registry cleanup completed.")
    );
}