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
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include <atomic>
#include "Atmosphere/AndromedaAtmosphereViewExtension.h"


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


    // ATMOS-03: enables/disables the atmosphere volume diagnostic overlay.
    // 0 = scene color only (no atmosphere visualization)
    // 1 = volume/intersection diagnostic (ATMOS-03)
    TAutoConsoleVariable<int> CVarAndromedaAtmosDebugVolume(
        TEXT("r.AndromedaAtmos.DebugVolume"),
        1,
        TEXT("ATMOS-03: enable the atmosphere volume/intersection diagnostic overlay. 0 = off, 1 = on (shows atmosphere sphere intersections)."),
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
// =========================================================
// LIFECYCLE
// =========================================================

bool FAndromedaAtmosphereRenderer::bInitialized = false;

FDelegateHandle FAndromedaAtmosphereRenderer::PostEngineInitDelegateHandle;

FDelegateHandle FAndromedaAtmosphereRenderer::WorldCleanupDelegateHandle;


void FAndromedaAtmosphereRenderer::Initialize()
{
    if (bInitialized)
    {
        return;
    }


    check(IsInGameThread());

    RegisterShaderDirectoryMapping();

    // ATMOS-02: the atmosphere pass is hooked into the post-processing chain
    // through the public UE 5.8 scene view extension API once GEngine is
    // available (see HandlePostEngineInit). NewExtension requires a valid
    // GEngine, so registration cannot happen this early in StartupModule.
    PostEngineInitDelegateHandle = FCoreDelegates::GetOnPostEngineInit().AddStatic(&FAndromedaAtmosphereRenderer::HandlePostEngineInit);

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


    FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitDelegateHandle);

    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupDelegateHandle);
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
    const FString RealShaderDirectory = FPaths::Combine(
        FPaths::ProjectDir(),
        TEXT("Shaders"),
        TEXT("Andromeda")
    );


    checkf(
        FPaths::DirectoryExists(RealShaderDirectory),
        TEXT("Andromeda atmosphere shader directory is missing: %s"),
        *RealShaderDirectory
    );


    // Fails with a check if the mapping already exists.
    AddShaderSourceDirectoryMapping(VirtualShaderDirectory, RealShaderDirectory);
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
        FAndromedaAtmosphereGPUData GPUData;


        GPUData.CenterX = Instance.WorldPosition.X;
        GPUData.CenterY = Instance.WorldPosition.Y;
        GPUData.CenterZ = Instance.WorldPosition.Z;
        GPUData.SurfaceRadius = Instance.Parameters.SurfaceRadius;


        GPUData.AtmosphereRadius = Instance.Parameters.AtmosphereRadius;
        GPUData.Pad0 = 0.0f;
        GPUData.Pad1 = 0.0f;
        GPUData.Pad2 = 0.0f;


        OutGPUData.Add(GPUData);
    }
}


// =========================================================
// RENDERING (ATMOS-04: camera-relative stable ray march, 16 steps)
// =========================================================

FScreenPassTexture FAndromedaAtmosphereRenderer::RenderAtmospheres(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs)
{
    // Gate: the diagnostic pass is enabled by default (r.AndromedaAtmos.Enable).
    if (!bInitialized || CVarAndromedaAtmosEnable.GetValueOnRenderThread() == 0)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
    }


    // --------------------------------------------------------
    // Step 1: get the atmosphere snapshot from the manager.
    // --------------------------------------------------------
    TArray<FAndromedaAtmosphereInstance> AtmosphereSnapshot;
    FAndromedaAtmosphereManager::Get().GetAtmosphereSnapshot(AtmosphereSnapshot);


    const int32 AtmosphereCount = AtmosphereSnapshot.Num();


    // --------------------------------------------------------
    // Step 2: convert CPU snapshot -> packed GPU data, camera-relative.
    // RelCenter = Center - Camera is subtracted in double (FVector) and
    // only the small relative result is converted to float. In exact
    // arithmetic identical to uploading the absolute center, since the
    // shader ray origin is 0 (see .usf): (P - Camera) is translation
    // invariant. Radii are translation-invariant scalars, unchanged.
    // --------------------------------------------------------
    const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();

    TArray<FAndromedaAtmosphereGPUData> GPUData;
    GPUData.Reset(AtmosphereSnapshot.Num());

    for (const FAndromedaAtmosphereInstance& Instance : AtmosphereSnapshot)
    {
        FAndromedaAtmosphereGPUData RelGPUData;

        const FVector RelCenter = Instance.WorldPosition - ViewOrigin;

        RelGPUData.CenterX = (float)RelCenter.X;
        RelGPUData.CenterY = (float)RelCenter.Y;
        RelGPUData.CenterZ = (float)RelCenter.Z;
        RelGPUData.SurfaceRadius = Instance.Parameters.SurfaceRadius;


        RelGPUData.AtmosphereRadius = Instance.Parameters.AtmosphereRadius;
        RelGPUData.Pad0 = 0.0f;
        RelGPUData.Pad1 = 0.0f;
        RelGPUData.Pad2 = 0.0f;


        GPUData.Add(RelGPUData);
    }


    const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);


    if (!SceneColorSlice.TextureSRV)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
    }


    // Resolve the input slice to a real texture (handles texture-array
    // slices and the override-output case) -> SceneColor in.
    FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice, Inputs.OverrideOutput);


    if (!SceneColor.Texture)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
    }


    // Output: the chain-provided override (back buffer) when this is the
    // last pass, otherwise a dedicated RDG texture. This keeps the
    // SceneColor -> Atmosphere pass -> output structure for ATMOS-04+.
    FScreenPassRenderTarget Output;
    ERenderTargetLoadAction OutputLoadAction = ERenderTargetLoadAction::ELoad;


    if (Inputs.OverrideOutput.IsValid())
    {
        Output = Inputs.OverrideOutput;
        OutputLoadAction = ERenderTargetLoadAction::ELoad;
    }
    else
    {
        FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
        OutputDesc.Flags |= ETextureCreateFlags::RenderTargetable;

        Output = FScreenPassRenderTarget(
            GraphBuilder.CreateTexture(OutputDesc, TEXT("AndromedaAtmosphereOutput")),
            SceneColor.ViewRect,
            ERenderTargetLoadAction::ENoAction);

        OutputLoadAction = ERenderTargetLoadAction::ENoAction;
    }


    // Global shaders live in the global shader map (no FViewInfo dependency).
    FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);


    if (!GlobalShaderMap)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
    }


    FAndromedaAtmospherePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAndromedaAtmospherePS::FParameters>();
    PassParameters->ViewportSize = Output.ViewRect.Size();
    PassParameters->SceneColorTexture = SceneColor.Texture;
    PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    PassParameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, OutputLoadAction);


    // --------------------------------------------------------
    // ATMOS-04 DEPTH OCCLUSION: bind the scene-textures uniform buffer
    // (which contains the current frame's depth among all scene textures)
    // and the view uniform buffer exactly like the engine's own global
    // post-process shaders (see PostProcessing.cpp FGBufferPicking).
    // The shader reads SceneTexturesStruct.SceneDepthTexture and converts
    // the stored device-Z with ViewUniformBuffer.InvDeviceZToWorldZTransform
    // (the SAME transform/values as Common.ush ConvertFromDeviceZ()).
    // DepthOcclusionEnabled stays 0 when the scene textures are not
    // available, keeping the exact ATMOS-04 behavior in that case.
    // --------------------------------------------------------
    PassParameters->SceneTexturesStruct = Inputs.SceneTextures.SceneTextures;
    PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
    PassParameters->DepthOcclusionEnabled =
        Inputs.SceneTextures.SceneTextures ? 1 : 0;


    // --------------------------------------------------------
    // Step 3: atmosphere data -> RDG StructuredBuffer.
    // --------------------------------------------------------
    PassParameters->AtmosphereCount = AtmosphereCount;

    // The shader parameter is a required RDG SRV: it must ALWAYS be bound to a
    // valid buffer, even when no atmospheres are registered (ATMOS-03).
    // The shader only reads AtmosphereBuffer when AtmosphereCount > 0, so with
    // 0 atmospheres the SRV is bound to a 1-element dummy buffer (never read).
    const uint32 NumElements = FMath::Max(1u, (uint32)GPUData.Num());

    FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(
        sizeof(FAndromedaAtmosphereGPUData),
        NumElements
    );


    FRDGBufferRef AtmosphereBuffer = GraphBuilder.CreateBuffer(
        BufferDesc,
        TEXT("AndromedaAtmosphereBuffer"),
        ERDGBufferFlags::None
    );


    // Zero-initialized dummy element used when no atmospheres are registered.
    const FAndromedaAtmosphereGPUData DummyAtmosphere = {};
    const FAndromedaAtmosphereGPUData* UploadData =
        (GPUData.Num() > 0) ? GPUData.GetData() : &DummyAtmosphere;
    const uint64 UploadBytes = (GPUData.Num() > 0)
        ? (uint64)(GPUData.Num() * sizeof(FAndromedaAtmosphereGPUData))
        : (uint64)sizeof(FAndromedaAtmosphereGPUData);

    GraphBuilder.QueueBufferUpload(
        AtmosphereBuffer,
        UploadData,
        UploadBytes,
        ERDGInitialDataFlags::None
    );


    // Structured-buffer SRV matching StructuredBuffer<FAndromedaAtmosphereGPUData>
    // declared in AndromedaAtmosphere.usf (element stride = sizeof(FAndromedaAtmosphereGPUData)).
    PassParameters->AtmosphereBuffer = GraphBuilder.CreateSRV(
        FRDGBufferSRVDesc(AtmosphereBuffer)
    );


    // --------------------------------------------------------
    // Step 4: camera data. Numerator precomputation in double precision.
    //
    // Baseline direction: normalize(P - Cam), P = P_h.xyz / W.
    // Identity: P - Cam = (P_h.xyz - Cam*W) / W = Num / W, with
    //   P_h.xyz = clip.x*R0 + clip.y*R1 + clip.z*R2 + clip.w*Row3,
    //   W       = clip.x*W0 + clip.y*W1 + clip.z*W2 + clip.w*W3.
    // Hence Num = clip.x*(R0 - Cam*W0) + clip.y*(R1 - Cam*W1)
    //           + clip.z*(R2 - Cam*W2) + clip.w*(Row3 - Cam*W3).
    //
    // Each bracket is clip-INDEPENDENT: computed here in double from the
    // FMatrix (double) and ViewOrigin (double), then uploaded as float.
    // The GPU combines the small relative rows and divides once by W
    // (see .usf). Translation is folded in exactly, never zeroed; the
    // w-column (W0..W3) is passed through untouched so W is identical
    // to the baseline's P_h.w. CameraWorldPosition stays bound (required
    // RDG parameter) for compatibility; the math no longer subtracts it.
    // --------------------------------------------------------
    const FMatrix& ClipToWorldD = View.ViewMatrices.GetClipToWorld();
    const FVector& ViewOriginD = View.ViewMatrices.GetViewOrigin();

    FMatrix44f RelClipToWorld;
    for (int32 Row = 0; Row < 4; ++Row)
    {
        const double Wd = ClipToWorldD.M[Row][3];
        RelClipToWorld.M[Row][0] = (float)(ClipToWorldD.M[Row][0] - ViewOriginD.X * Wd);
        RelClipToWorld.M[Row][1] = (float)(ClipToWorldD.M[Row][1] - ViewOriginD.Y * Wd);
        RelClipToWorld.M[Row][2] = (float)(ClipToWorldD.M[Row][2] - ViewOriginD.Z * Wd);
        RelClipToWorld.M[Row][3] = (float)Wd;
    }

    PassParameters->CameraWorldPosition = FVector3f(
        View.ViewMatrices.GetViewOrigin()
    );
    PassParameters->InvViewProjection = RelClipToWorld;


    TShaderMapRef<FAndromedaAtmospherePS> PixelShader(GlobalShaderMap);


    // Real fullscreen RDG pass: AddFullscreenPass inserts a raster pass on
    // GraphBuilder (GraphBuilder.AddPass internally) that draws a fullscreen
    // triangle with FAndromedaAtmospherePS bound to the render target above.
    FPixelShaderUtils::AddFullscreenPass<FAndromedaAtmospherePS>(
        GraphBuilder,
        GlobalShaderMap,
        RDG_EVENT_NAME("AndromedaAtmosphereDiagnostic"),
        PixelShader,
        PassParameters,
        Output.ViewRect
    );


    // Dispatch bookkeeping (no per-frame logging: first dispatch only).
    const uint64 DispatchId = GDispatchCounter.fetch_add(1) + 1;


    if (DispatchId == 1)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Log,
            TEXT("[ATMOS-04] First real RDG dispatch of FAndromedaAtmospherePS: atmosphere ray-march diagnostic pass is live on the GPU. Atmospheres: %d"),
            AtmosphereCount
        );
    }


    return FScreenPassTexture(Output.Texture, Output.ViewRect);
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


    FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);


    if (!GlobalShaderMap)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("ValidateShaderInfrastructure: global shader map unavailable.")
        );

        return false;
    }


    const bool bPixelShaderCompiled = GlobalShaderMap->HasShader(
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
    // GEngine is now available: register the scene view extension that hooks
    // the atmosphere pass into the post-processing chain (ATMOS-02). This must
    // happen here (not in Initialize) because NewExtension requires a valid GEngine.
    FAndromedaAtmosphereViewExtension::Register();

    // ATMOS-LIFETIME: register the world teardown hook so the atmosphere
    // manager is cleared whenever a world (including a PIE session) is
    // cleaned up. This must also happen after engine init (FWorldDelegates
    // is fully usable here); the callback itself runs on the game thread
    // inside the world cleanup broadcast and holds the same manager lock
    // used by the render pass, so a PIE stopping mid-frame is safe.
    WorldCleanupDelegateHandle =
        FWorldDelegates::OnWorldCleanup.AddStatic(
            &FAndromedaAtmosphereRenderer::HandleWorldCleanup
        );


    const bool bValid = ValidateShaderInfrastructure();


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
    // Runs on the game thread inside the FWorldDelegates::OnWorldCleanup
    // broadcast (PIE stop, level unload, ...). The manager lock makes the
    // clear safe against a render pass that may still be reading a snapshot
    // on the render thread: GetAtmosphereSnapshot and the per-frame GPU data
    // conversion only ever operate on the locked CPU copy, so no data of the
    // destroyed world can survive into the next session.
    UE_LOG(
        LogAndromedaAtmos,
        Log,
        TEXT("[ATMOS-LIFETIME] Registry cleanup started (world id: %d, session ended: %d, cleanup resources: %d)."),
        InWorld ? (int32)InWorld->GetUniqueID() : -1,
        bSessionEnded ? 1 : 0,
        bCleanupResources ? 1 : 0
    );

    const int32 RemovedCount = FAndromedaAtmosphereManager::Get().Clear();

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