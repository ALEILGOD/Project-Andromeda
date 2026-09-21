#include "Zephyr/ZephyrPresentation.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "Zephyr/ZephyrManager.h"
#include "Zephyr/ZephyrShaders.h"

#include "HillaireLimits.h"
#include "HillairePlanetAtmosphereState.h"

static TAutoConsoleVariable<int32> CVarZephyrPresentationEnable(
	TEXT("r.Zephyr.PresentationEnable"),
	1,
	TEXT("ZEPHYR presentation overlay (per-planet cloud/haze on sky pixels). 0=off, 1=on."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarZephyrPresentationIntensity(
	TEXT("r.Zephyr.PresentationIntensity"),
	1.0f,
	TEXT("ZEPHYR presentation overlay master intensity (bounded; 0=off, 1=nominal)."),
	ECVF_RenderThreadSafe);

FZephyrPresentationViewExtension::FZephyrPresentationViewExtension(const FAutoRegister& AutoRegister, UZephyrManager* InManager)
	: FSceneViewExtensionBase(AutoRegister)
	, Manager(InManager)
{
}

FZephyrPresentationViewExtension::~FZephyrPresentationViewExtension() = default;

bool FZephyrPresentationViewExtension::ShouldHandleView(const FSceneViewFamily& InViewFamily) const
{
	const UZephyrManager* M = Manager.Get();
	if (!M)
	{
		return false;
	}
	const UWorld* World = M->GetWorld();
	if (!World || !World->Scene)
	{
		return false;
	}
	if (InViewFamily.Scene != static_cast<const FSceneInterface*>(World->Scene))
	{
		return false;
	}
	return true;
}

void FZephyrPresentationViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	PruneStaleSnapshots(GFrameCounter);
}

void FZephyrPresentationViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	if (!ShouldHandleView(InViewFamily))
	{
		return;
	}

	UZephyrManager* M = Manager.Get();
	if (!M)
	{
		return;
	}

	// Update the ATMOS-derived state once per GT frame; publish per view so
	// multi-view families each get their own view rect + daylight.
	if (GFrameCounter != LastUpdatedGFrame)
	{
		M->UpdateFromAtmosphere((float)FApp::GetDeltaTime());
		LastUpdatedGFrame = GFrameCounter;
	}
	M->PublishFrameForView(InView);

	if (const FSceneViewStateInterface* Key = MakeSnapshotKey(InView))
	{
		const TSharedPtr<const FZephyrPresentationFrame> Frame = M->GetCurrentFrameData();
		if (Frame.IsValid())
		{
			FScopeLock Lock(&StashLock);
			FStashedSnapshot Entry;
			Entry.Frame = Frame;
			Entry.FrameBuilt = GFrameCounter;
			SnapshotStash.Add(Key, MoveTemp(Entry));
		}
	}
}

const FSceneViewStateInterface* FZephyrPresentationViewExtension::MakeSnapshotKey(const FSceneView& InView)
{
	return InView.State;
}

TSharedPtr<const FZephyrPresentationFrame> FZephyrPresentationViewExtension::FindSnapshot(const FSceneView& InView) const
{
	const FSceneViewStateInterface* Key = MakeSnapshotKey(InView);
	if (!Key)
	{
		return nullptr;
	}
	FScopeLock Lock(&StashLock);
	if (const FStashedSnapshot* Found = SnapshotStash.Find(Key))
	{
		return Found->Frame;
	}
	return nullptr;
}

void FZephyrPresentationViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass, const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// AfterDOF: strictly after the authoritative ATMOS BeforeDOF composite.
	if (Pass != EPostProcessingPass::AfterDOF)
	{
		return;
	}
	if (CVarZephyrPresentationEnable.GetValueOnGameThread() == 0)
	{
		return;
	}
	InOutPassCallbacks.Add(
		FPostProcessingPassDelegate::CreateRaw(this, &FZephyrPresentationViewExtension::ZephyrPresentationPass));
}

FScreenPassTexture FZephyrPresentationViewExtension::ZephyrPresentationPass(
	FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTextureSlice InSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	const auto Passthrough = [&]() -> FScreenPassTexture { return FScreenPassTexture(InSlice); };

	if (CVarZephyrPresentationEnable.GetValueOnRenderThread() == 0)
	{
		return Passthrough();
	}

	const TSharedPtr<const FZephyrPresentationFrame> Frame = FindSnapshot(View);
	if (!Frame.IsValid() || !Frame->HasContent() || Frame->ZephyrTransitionFactor <= 0.0f)
	{
		return Passthrough();
	}

	FRDGTextureRef SceneDepth = nullptr;
	{
		const TRDGUniformBufferBinding<FSceneTextureUniformParameters>& DepthUB = Inputs.SceneTextures.SceneTextures;
		if (DepthUB && DepthUB->GetContents())
		{
			SceneDepth = DepthUB->GetContents()->SceneDepthTexture;
		}
	}
	if (!SceneDepth)
	{
		return Passthrough();
	}

	// RT re-anchor: planet-local camera + view->planet-local rotation, exactly
	// the ATMOS composite convention (double-subtract, narrow once).
	const FVector RTViewOrigin = View.ViewMatrices.GetViewOrigin();
	const FVector RelKm = (Frame->CenterWS - RTViewOrigin) * HillaireLimits::KmPerCm;
	const FVector3f CenterRelKm((float)RelKm.X, (float)RelKm.Y, (float)RelKm.Z);
	if (CenterRelKm.ContainsNaN())
	{
		return Passthrough();
	}
	const FVector3f CameraPlanetLocalKm = FVector3f(Frame->RotationWS.Inverse().RotateVector(RelKm));

	FMatrix ViewToWorld(EForceInit::ForceInitToZero);
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 C = 0; C < 3; ++C)
		{
			ViewToWorld.M[R][C] = View.ViewMatrices.GetWorldToView().M[C][R];
		}
	}
	ViewToWorld.M[3][3] = 1.0;
	const FMatrix ViewToPlanetLocal = ViewToWorld * Frame->RotationWS.Inverse().ToMatrix();
	const FMatrix InvProj = View.ViewMatrices.GetViewToClip().Inverse();

	const FIntRect ViewRect = InSlice.ViewRect;
	if (ViewRect.IsEmpty())
	{
		return Passthrough();
	}

	FRDGTextureDesc OutDesc = InSlice.TextureSRV->Desc.Texture->Desc;
	OutDesc.Flags |= TexCreate_UAV;
	OutDesc.ClearValue = FClearValueBinding::None;
	FRDGTextureRef OutTex = GraphBuilder.CreateTexture(OutDesc, TEXT("Zephyr.Presentation"));

	FZephyrPresentationCS::FParameters* Params = GraphBuilder.AllocParameters<FZephyrPresentationCS::FParameters>();
	Params->View = View.ViewUniformBuffer;
	Params->SceneColorInput = InSlice.TextureSRV;
	Params->SceneDepthInput = GraphBuilder.CreateSRV(SceneDepth);
	Params->LinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	Params->InvProjMatrix = FMatrix44f(InvProj);
	Params->ViewToPlanetLocalRot = FMatrix44f(ViewToPlanetLocal);
	Params->CameraPlanetLocalKm = CameraPlanetLocalKm;

	Params->PrimarySunDirLocal = Frame->StarDirectionLocal;
	Params->SunColorAttenuation = Frame->StarIrradiance.GetSafeNormal();

	Params->BottomRadiusKm = Frame->PlanetRadiusKm;
	Params->TopRadiusKm = Frame->AtmosphereTopRadiusKm;
	Params->ViewHeightKm = CenterRelKm.Size();
	Params->ZephyrTransitionFactor = Frame->ZephyrTransitionFactor;
	Params->DaylightFactor = Frame->DaylightFactor;

	Params->ZenithColor = Frame->ZenithColor;
	Params->HorizonColor = Frame->HorizonColor;
	Params->SunGlowColor = Frame->SunGlowColor;
	Params->MieColor = Frame->MieColor;
	Params->RayleighScale = Frame->RayleighScale;
	Params->MieScale = Frame->MieScale;

	Params->WeatherHazeFactor = Frame->WeatherHazeFactor;
	Params->WeatherHazeColor = Frame->WeatherHazeColor;

	const int32 NumClouds = FMath::Min(Frame->CloudLayers.Num(), ZephyrLimits::MaxCloudLayers);
	Params->CloudLayerCount = (uint32)NumClouds;
	for (int32 i = 0; i < NumClouds; ++i)
	{
		Params->CloudLayerAltitudeConfigure[i] = Frame->CloudLayers[i].AltitudeConfigure;
		Params->CloudLayerColorDetail[i] = Frame->CloudLayers[i].ColorDetail;
		Params->CloudLayerWindWater[i] = Frame->CloudLayers[i].WindWater;
	}
	Params->TimeSeconds = Frame->TimeSeconds;

	Params->ViewRectMin = FVector2f((float)ViewRect.Min.X, (float)ViewRect.Min.Y);
	Params->ViewRectSize = FVector2f((float)ViewRect.Width(), (float)ViewRect.Height());
	Params->SkyDepthEpsilon = HillaireLimits::CompositeSkyDepthEpsilon;
	Params->PresentationIntensity = CVarZephyrPresentationIntensity.GetValueOnRenderThread();
	Params->CompositeOutputUav = GraphBuilder.CreateUAV(OutTex);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FZephyrPresentationCS> Shader(ShaderMap);
	const FIntVector GroupCount(
		(ViewRect.Width() + HillaireLimits::CompositeThreadGroupX - 1) / HillaireLimits::CompositeThreadGroupX,
		(ViewRect.Height() + HillaireLimits::CompositeThreadGroupY - 1) / HillaireLimits::CompositeThreadGroupY,
		1);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Zephyr.Presentation"),
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		Shader,
		Params,
		GroupCount);

	return FScreenPassTexture(OutTex, ViewRect);
}

void FZephyrPresentationViewExtension::PruneStaleSnapshots(uint64 CurrentFrame)
{
	static constexpr uint64 KeepFrames = 30;
	FScopeLock Lock(&StashLock);
	TArray<const FSceneViewStateInterface*> Stale;
	for (const auto& Pair : SnapshotStash)
	{
		if (Pair.Value.FrameBuilt + KeepFrames < CurrentFrame)
		{
			Stale.Add(Pair.Key);
		}
	}
	for (const FSceneViewStateInterface* Key : Stale)
	{
		SnapshotStash.Remove(Key);
	}
}
