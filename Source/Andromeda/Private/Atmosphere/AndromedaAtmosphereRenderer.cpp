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
    // Step 2: convert CPU snapshot -> packed GPU data.
    // --------------------------------------------------------
    TArray<FAndromedaAtmosphereGPUData> GPUData;
    BuildGPUData(AtmosphereSnapshot, GPUData);


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
    // Step 4: camera data for view-ray reconstruction.
    // --------------------------------------------------------
    PassParameters->CameraWorldPosition = FVector3f(
        View.ViewMatrices.GetViewOrigin()
    );


    PassParameters->InvViewProjection = FMatrix44f(
        View.ViewMatrices.GetClipToWorld()
    );


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