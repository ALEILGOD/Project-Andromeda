#include "HillairePlanetaryViewExtension.h"

#include "Engine/World.h"
#include "HillaireAtmosphereLog.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillaireLutManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "HillaireLimits.h"

FHillairePlanetaryViewExtension::FHillairePlanetaryViewExtension(const FAutoRegister& AutoRegister, UHillairePlanetaryAtmosphereSubsystem* InSubsystem)
	: FSceneViewExtensionBase(AutoRegister)
	, Subsystem(InSubsystem)
{
}

FHillairePlanetaryViewExtension::~FHillairePlanetaryViewExtension() = default;

// CVars
static TAutoConsoleVariable<int32> CVarHillaireAerialEval(
	TEXT("r.Hillaire.AerialEval"),
	1,
	TEXT("Evaluate aerial perspective for governing planet (0=off, 1=on)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarHillaireSkyEnable(
	TEXT("r.Hillaire.SkyEnable"),
	1,
	TEXT("Composite sky background (0=off, 1=on)."),
	ECVF_RenderThreadSafe);

// Outside-atmosphere limb switch (ZEPHYR calibration pass): when 1 (default),
// the sky pass also runs outside the atmosphere wherever the view frustum
// intersects the governing top sphere, rendering the physical limb through
// the existing ray x top-sphere path. When 0, the legacy inside-only policy
// applies (space shows no atmospheric limb). Render knob only: no LUT math,
// no profile, no selection change either way.
static TAutoConsoleVariable<int32> CVarHillaireOutsideLimb(
	TEXT("r.Hillaire.OutsideLimb"),
	1,
	TEXT("Render the physical atmospheric limb from outside the atmosphere (0=off/legacy inside-only, 1=on)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarHillaireDebugMode(
	TEXT("r.Hillaire.DebugMode"),
	0,
	TEXT("Debug visualization: 0=off, 1=Transmittance, 2=MultiScattering, 3=SkyView, 4=Aerial, 5=Density."),
	ECVF_RenderThreadSafe);

// Reference sun-illuminance knob (mirrors the DX11 sample mSunIlluminanceScale
// slider, range 0.1-100). Default 30.0 = the validated end-to-end gain: the
// reference renders unit-sun transfer through a FIXED demo tonemap exposure
// of 10 (Resources/PostProcess.hlsl:64-65), while UE uses the project
// exposure (~1) + ACES. The baked transfer is unit-white by architecture
// (proven: live zenith OD matches Earth 0.99x, GPU==CPU); the game sun
// intensity (~1.0, an authoring value that does not physically light the
// scene) therefore under-delivers the validated daylight level through UE's
// chain (measured: noon zenith transfer lum 0.0055 -> black sky; only the
// limb band survived visibly). The whole chain is linear in sun throughput,
// so this default is radiometrically identical to baking the scale into the
// LUTs: it alters no gradients, limb shape, or contrast. 30 lifts the whole
// sky dome into a properly illuminated blue (the 10x default still left the
// high-elevation dome near black on the thin 1.10x envelopes) and carries the
// warm sunset inscatter onto both the sky and the visible surface.
// Tunable live 0.1-100 like the reference slider; NOT a per-scene gain hack.
static TAutoConsoleVariable<float> CVarHillaireSunScale(
	TEXT("r.Hillaire.SunScale"),
	30.0f,
	TEXT("Linear sun throughput scale at composite (reference mSunIlluminanceScale; default 30 ports the demo exposure calibration into UE's exposure chain)."),
	ECVF_RenderThreadSafe);

bool FHillairePlanetaryViewExtension::ShouldHandleView(const FSceneViewFamily& InViewFamily) const
{
	const UHillairePlanetaryAtmosphereSubsystem* Sub = Subsystem.Get();
	if (!Sub || !UHillairePlanetaryAtmosphereSubsystem::IsEnabledByCVar() || !Sub->bAtmosphereEnabled)
	{
		return false;
	}
	const UWorld* World = Sub->GetWorld();
	if (!World || !World->Scene)
	{
		return false;
	}
	if (InViewFamily.Scene != static_cast<const FSceneInterface*>(World->Scene))
	{
		return false;
	}
	return Sub->GetRegisteredPlanetCount() > 0;
}

void FHillairePlanetaryViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	PruneStaleSnapshots(GFrameCounter);

	if (!ShouldHandleView(InViewFamily))
	{
		UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[ViewExt] SetupViewFamily: ShouldHandleView=false"));
		return;
	}

	UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[ViewExt] SetupViewFamily: ShouldHandleView=true, waiting for views..."));
	// Snapshot will be built in SetupView when the first valid view arrives
}

void FHillairePlanetaryViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	if (!ShouldHandleView(InViewFamily))
	{
		return;
	}

	UHillairePlanetaryAtmosphereSubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] SetupView: RETURN SubsystemGone"));
		return;
	}

	const UWorld* SubWorld = Sub->GetWorld();
	UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[ViewExt] SetupView: Ext=%p Sub=%p World=%s GFrame=%llu LastBuiltGFrame=%llu"),
		this, Sub,
		SubWorld ? *SubWorld->GetName() : TEXT("<null>"),
		(unsigned long long)GFrameCounter,
		(unsigned long long)LastSnapshotBuiltGFrame);

	// Multi-view correctness: build a snapshot for THIS view and stash it
	// immediately. The previous "once per GT frame (first view)" build reused
	// the first view's origin for every view, so any secondary view (probe
	// camera, split screen, editor viewport, VR eye) rendered against the wrong
	// camera height: the sky pass then treated it as outside the atmosphere and
	// composited only the outside-limb ring. The build+stash pair is synchronous
	// within this call, so the shared CurrentFrameSnapshot is read for the view
	// that just built it.
	if (InView.State)
	{
		const FVector ViewOriginWS = InView.ViewMatrices.GetViewOrigin();
		FMatrix RelativeViewMatrix = InView.ViewMatrices.GetWorldToView();
		RelativeViewMatrix.M[3][0] = 0.0f;
		RelativeViewMatrix.M[3][1] = 0.0f;
		RelativeViewMatrix.M[3][2] = 0.0f;
		const FMatrix ProjMatrix = InView.ViewMatrices.GetViewToClip();
		const FIntRect ViewRect = InView.UnscaledViewRect;
		const FVector ViewDir = InView.GetViewDirection();

		Sub->BuildNextFrameSnapshot(ViewOriginWS, RelativeViewMatrix, ProjMatrix, ViewRect, ViewDir);
		LastSnapshotBuiltGFrame = GFrameCounter;

		if (const FSceneViewStateInterface* Key = MakeSnapshotKey(InView))
		{
			const TSharedPtr<const FHillaireAtmosphereFrameState> CurrentSnapshot = Sub->GetCurrentFrameSnapshot();
			if (CurrentSnapshot.IsValid())
			{
				FScopeLock Lock(&StashLock);
				FStashedSnapshot Entry;
				Entry.Snapshot = CurrentSnapshot;
				Entry.Frame = GFrameCounter;
				SnapshotStash.Add(Key, MoveTemp(Entry));
			}
		}
	}
	else
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] SetupView: RETURN NoViewState GFrame=%llu"),
			(unsigned long long)GFrameCounter);
	}
}

const FSceneViewStateInterface* FHillairePlanetaryViewExtension::MakeSnapshotKey(const FSceneView& InView)
{
	return InView.State;
}

TSharedPtr<const FHillaireAtmosphereFrameState> FHillairePlanetaryViewExtension::FindSnapshot(const FSceneView& InView) const
{
	const FSceneViewStateInterface* Key = MakeSnapshotKey(InView);
	if (!Key) return nullptr;

	FScopeLock Lock(&StashLock);
	if (const FStashedSnapshot* Found = SnapshotStash.Find(Key))
	{
		return Found->Snapshot;
	}
	return nullptr;
}

void FHillairePlanetaryViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	// No-op: LUT generation happens in BeforeDOF composite graph (same-graph transient handoff)
	(void)GraphBuilder;
	(void)InView;
}

void FHillairePlanetaryViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass, const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (Pass != EPostProcessingPass::BeforeDOF)
	{
		return;
	}
	InOutPassCallbacks.Add(
		FPostProcessingPassDelegate::CreateRaw(this, &FHillairePlanetaryViewExtension::PlanetaryCompositePass));
}

FScreenPassTexture FHillairePlanetaryViewExtension::PlanetaryCompositePass(
	FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTextureSlice InSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	const auto Passthrough = [&]() -> FScreenPassTexture { return FScreenPassTexture(InSlice); };

	// Get snapshot for this view
	const TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot = FindSnapshot(View);
	if (!Snapshot.IsValid() || !Snapshot->HasAtmosphereContent())
	{
		return Passthrough();
	}

	UHillairePlanetaryAtmosphereSubsystem* Sub = Subsystem.Get();
	if (!Sub) return Passthrough();

	FHillaireLutManager* LutManager = Sub->GetLutManager();
	if (!LutManager) return Passthrough();

	// Get governing planet
	const FPlanetAtmosphereState* GoverningPlanet = Snapshot->GetGoverningPlanet();
	if (!GoverningPlanet || GoverningPlanet->ResolvedLights.Count == 0)
	{
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[ViewExt] PlanetaryCompositePass: NO GOVERNING PLANET OR NO LIGHTS (Planet=%s, Lights=%d)"),
			GoverningPlanet ? *GoverningPlanet->PlanetId.ToString() : TEXT("null"), GoverningPlanet ? GoverningPlanet->ResolvedLights.Count : 0);
		return Passthrough();
	}

	// Governing change log
	if (GoverningPlanet->PlanetId != LastLoggedGoverningPlanet)
	{
		LastLoggedGoverningPlanet = GoverningPlanet->PlanetId;
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("GoverningPlanet: %s (%s)"), *GoverningPlanet->PlanetName.ToString(), *GoverningPlanet->PlanetId.ToString());
	}

	// Depth texture
	FRDGTextureRef RdgSceneDepth = nullptr;
	{
		const TRDGUniformBufferBinding<FSceneTextureUniformParameters>& DepthUB = Inputs.SceneTextures.SceneTextures;
		if (DepthUB && DepthUB->GetContents())
		{
			RdgSceneDepth = DepthUB->GetContents()->SceneDepthTexture;
		}
	}
	if (!RdgSceneDepth) 
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] PlanetaryCompositePass: no scene depth texture"));
		return Passthrough();
	}

	// Sun scale
	const float SunScale = CVarHillaireSunScale.GetValueOnRenderThread();
	const FVector3f ScaledSunAttenuation = GoverningPlanet->StarIrradiance * SunScale;

	// ---- Stage 0: Ensure LUTs for governing planet (same-graph transient) ----
	FHillaireLutManager::FHillairePlanetLutGraphOutputs OutLuts;
	// LUT slot = subsystem registry index recorded at snapshot time, NOT the
	// frame-array index: frame order (visibles far-to-near + governing last)
	// differs from registry order, and the LUT cache/pooled targets are keyed
	// by registry slot.
	const int32 PlanetSlotIdx = GoverningPlanet->LutSlotIndex;
	if (PlanetSlotIdx == INDEX_NONE)
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] PlanetaryCompositePass: governing planet has no LUT slot"));
		return Passthrough();
	}

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("[ViewExt] PlanetaryCompositePass: EnsurePlanetLuts for slot %d Planet=%s ViewH=%.3f"),
		PlanetSlotIdx, *GoverningPlanet->PlanetId.ToString(), GoverningPlanet->ViewHeightKm);

	if (!LutManager->EnsurePlanetLuts(
		GraphBuilder,
		PlanetSlotIdx,
		GoverningPlanet->Profile,
		GoverningPlanet->ViewHeightKm,
		GoverningPlanet->CenterCamRelativeKm,
		GoverningPlanet->RotationWS,
		GoverningPlanet->ResolvedLights,
		Snapshot->MultipleScatteringFactor,
		Snapshot->bFastSkyEnabled,
		OutLuts))
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] PlanetaryCompositePass: EnsurePlanetLuts FAILED"));
		return Passthrough();
	}

	const bool bHaveLutT = OutLuts.Transmittance != nullptr;
	const bool bHaveLutMS = OutLuts.MultiScattering != nullptr;
	const bool bHaveLutSky = OutLuts.SkyView != nullptr;

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("[ViewExt] PlanetaryCompositePass: LUTs ready T=%d MS=%d Sky=%d"),
		bHaveLutT ? 1 : 0, bHaveLutMS ? 1 : 0, bHaveLutSky ? 1 : 0);

	// ---- RT-exact composite frame (re-anchor to RT view) ----
	const FVector RTViewOriginWS = View.ViewMatrices.GetViewOrigin();
	const FMatrix RTViewMatrix = View.ViewMatrices.GetWorldToView();
	const FMatrix RTProjMatrix = View.ViewMatrices.GetViewToClip();

	const FHillaireLutManager::FHillaireCompositeViewInputs CompositeFrame = ComputeCompositeViewInputs(
		*GoverningPlanet, *Snapshot, View);

	FHillaireLutManager::FHillaireAerialViewInputs AerialViewInputs;
	FVector3f CameraPlanetLocalKm;

	if (CompositeFrame.bValid)
	{
		AerialViewInputs.CameraPlanetLocalKm = CompositeFrame.CameraPlanetLocalKm;
		AerialViewInputs.InvProjMatrix = CompositeFrame.InvProjMatrix;
		AerialViewInputs.ViewToPlanetLocalRot = CompositeFrame.ViewToPlanetLocalRot;
		CameraPlanetLocalKm = CompositeFrame.CameraPlanetLocalKm;
	}
	else
	{
		// Fallback to snapshot-derived inputs
		AerialViewInputs = FHillaireLutManager::ComputeAerialViewInputs(
			GoverningPlanet->CenterCamRelativeKm,
			GoverningPlanet->RotationWS,
			Snapshot->ViewMatrix,
			Snapshot->ProjectionMatrix);
		CameraPlanetLocalKm = AerialViewInputs.CameraPlanetLocalKm;
	}

	// Actual camera height for rendering (unclamped, used by diagnostics and
	// ray geometry).
	const float ActualViewHeightKm = GoverningPlanet->ViewHeightKm;
	// SkyView/LUT height: clamped into the bake's in-domain range
	// [Bottom + floor, Top - floor] with the SAME formula EnsurePlanetLuts
	// bakes with (cache key stability + valid parameterization). Outside
	// cameras get the top boundary height; the shader measures the ray angles
	// at the top entry point.
	const float HeightFloorKm =
		GoverningPlanet->Profile.BottomRadiusKm + HillaireLimits::ViewHeightEpsFloorKm;
	const float HeightCeilKm = FMath::Max(
		HeightFloorKm,
		GoverningPlanet->Profile.TopRadiusKm - HillaireLimits::ViewHeightEpsFloorKm);
	const float ClampedHeightKm = FMath::Clamp(
		GoverningPlanet->ViewHeightKm, HeightFloorKm, HeightCeilKm);

	// Composite staging
	FRDGTextureSRVRef CurrentColorSrv = InSlice.TextureSRV;
	FRDGTextureRef CurrentColorTex = nullptr;
	bool bTouched = false;
	const FIntRect ViewRect = InSlice.ViewRect;

	// ---- Debug Visualization (FASE 10) ----
	{
		const int32 DebugMode = CVarHillaireDebugMode.GetValueOnRenderThread();
		if (DebugMode >= 1 && DebugMode <= 22)
		{
			// Sun-geometry diagnostic for modes 12-21: log the stored vectors
			// plus an independent CPU recompute of Qc*WorldDir, so any angular
			// offset between stored/expected/transformed sun is measurable.
			// NEVER changes the vectors (diagnostic only).
			if (DebugMode >= 12 && DebugMode <= 21)
			{
				static uint64 SunDiagCount = 0;
				++SunDiagCount;
				if (SunDiagCount == 1 || (SunDiagCount % 300) == 0)
				{
					const FVector SunLocalD(
						GoverningPlanet->StarDirectionLocal.X,
						GoverningPlanet->StarDirectionLocal.Y,
						GoverningPlanet->StarDirectionLocal.Z);
					const FVector RecomputedLocal =
						GoverningPlanet->RotationWS.Inverse().RotateVector(GoverningPlanet->StarDirectionWorld);
					const double Denom = SunLocalD.Size() * RecomputedLocal.Size();
					double AngleDeg = -1.0;
					if (Denom > 1e-12)
					{
						const double CosA = FMath::Clamp(
							SunLocalD.Dot(RecomputedLocal) / Denom, -1.0, 1.0);
						AngleDeg = FMath::RadiansToDegrees(FMath::Acos(CosA));
					}
					UE_LOG(LogHillaireAtmosphere, Log,
						TEXT("[ViewExt][DebugSun] Gov=%s SunLocal=(%.6f,%.6f,%.6f) SunWorld=(%.6f,%.6f,%.6f) CenterWS=(%.1f,%.1f,%.1f) CamWS=(%.1f,%.1f,%.1f) Rot=(%.4f,%.4f,%.4f,%.4f) Recomputed=(%.6f,%.6f,%.6f) AngleDeg=%.4f Mode=%d"),
						*GoverningPlanet->PlanetName.ToString(),
						SunLocalD.X, SunLocalD.Y, SunLocalD.Z,
						GoverningPlanet->StarDirectionWorld.X, GoverningPlanet->StarDirectionWorld.Y, GoverningPlanet->StarDirectionWorld.Z,
						GoverningPlanet->CenterWS.X, GoverningPlanet->CenterWS.Y, GoverningPlanet->CenterWS.Z,
						Snapshot->ViewOriginWS.X, Snapshot->ViewOriginWS.Y, Snapshot->ViewOriginWS.Z,
						GoverningPlanet->RotationWS.X, GoverningPlanet->RotationWS.Y,
						GoverningPlanet->RotationWS.Z, GoverningPlanet->RotationWS.W,
						RecomputedLocal.X, RecomputedLocal.Y, RecomputedLocal.Z,
						AngleDeg, DebugMode);
				}
			}
			FRDGTextureRef RdgT = OutLuts.Transmittance;
			FRDGTextureRef RdgMs = OutLuts.MultiScattering;
			FRDGTextureRef RdgSky = OutLuts.SkyView;
			FRDGTextureRef RdgAerial = nullptr;
			TRefCountPtr<IPooledRenderTarget> AerialScratchKeepAlive;

			if (DebugMode == 4)
			{
				const bool bCameraInsideDbg = GoverningPlanet->ViewHeightKm < GoverningPlanet->Profile.TopRadiusKm;
				if (FHillaireLutManager::ShouldCompositeAerial(
					CVarHillaireAerialEval.GetValueOnRenderThread() != 0,
					Snapshot->HasAtmosphereContent(), bCameraInsideDbg, bHaveLutT, bHaveLutMS)
					&& LutManager->EvaluateAerialPerspective(
						GraphBuilder,
						PlanetSlotIdx,
						GoverningPlanet->Profile,
						AerialViewInputs,
						GoverningPlanet->StarDirectionLocal,
						RdgT, RdgMs, RdgAerial)
					&& RdgAerial)
				{
					// In-graph transient
				}
				else
				{
					RdgAerial = nullptr;
					AerialScratchKeepAlive = LutManager->CopyAerialScratch(PlanetSlotIdx);
					if (AerialScratchKeepAlive.IsValid())
					{
						RdgAerial = GraphBuilder.RegisterExternalTexture(AerialScratchKeepAlive);
					}
				}
			}

			// World-frame ground truth for Mode 20: registry star position in
			// the snapshot camera frame (translation cancels in the normal,
			// so snapshot/RT origin skew cannot flip the side).
			FVector3f StarRelKm = FVector3f::ZeroVector;
			bool bFoundStarPos = false;
			for (const FHillaireLightSource& L : Snapshot->Lights)
			{
				if (L.LightId == GoverningPlanet->StarId && L.bEnabled)
				{
					const double SxKm = (L.WorldPositionCm.X - Snapshot->ViewOriginWS.X) * HillaireLimits::KmPerCm;
					const double SyKm = (L.WorldPositionCm.Y - Snapshot->ViewOriginWS.Y) * HillaireLimits::KmPerCm;
					const double SzKm = (L.WorldPositionCm.Z - Snapshot->ViewOriginWS.Z) * HillaireLimits::KmPerCm;
					StarRelKm = FVector3f((float)SxKm, (float)SyKm, (float)SzKm);
					bFoundStarPos = StarRelKm.SizeSquared() > 1e-12f;
					break;
				}
			}
			if (DebugMode == 20 && !bFoundStarPos)
			{
				UE_LOG(LogHillaireAtmosphere, Warning,
					TEXT("[ViewExt] DebugMode 20: no registry star position for StarId=%s (world/local split unavailable)"),
					*GoverningPlanet->StarId.ToString());
			}
			const FVector4f PlanetQuat(GoverningPlanet->RotationWS.X, GoverningPlanet->RotationWS.Y,
				GoverningPlanet->RotationWS.Z, GoverningPlanet->RotationWS.W);
			// Transition/blackout diagnostics for modes 20-22: every input the
			// ray/world reconstruction depends on, plus validity flags. Lets a
			// BLACK frame be attributed to a concrete missing/invalid input
			// instead of guessed from color alone. Diagnostic only.
			// Basis handedness check: det(view->local 3x3) must be +1 (proper
			// rotation). det -1 would prove a MIRRORED planet-local basis,
			// which flips bright/dark sides while keeping every sun vector
			// correct — the exact observed signature with a clean sun chain.
			{
				const FMatrix& M = AerialViewInputs.ViewToPlanetLocalRot;
				const double Det =
					(double)M.M[0][0] * ((double)M.M[1][1] * (double)M.M[2][2] - (double)M.M[1][2] * (double)M.M[2][1])
					- (double)M.M[0][1] * ((double)M.M[1][0] * (double)M.M[2][2] - (double)M.M[1][2] * (double)M.M[2][0])
					+ (double)M.M[0][2] * ((double)M.M[1][0] * (double)M.M[2][1] - (double)M.M[1][1] * (double)M.M[2][0]);
				static uint64 DetCount = 0;
				++DetCount;
				if (DetCount == 1 || (DetCount % 300) == 0 || FMath::Abs((float)Det - 1.0f) > 1e-3f)
				{
					UE_LOG(LogHillaireAtmosphere, Log,
						TEXT("[ViewExt][DebugBasis] V2PDet=%.6f (expect +1; -1 = MIRRORED basis) Mode=%d"),
						Det, DebugMode);
				}
			}
			if (DebugMode >= 20 && DebugMode <= 22)
			{
				static uint64 RayDbgCount = 0;
				++RayDbgCount;
				if (RayDbgCount == 1 || (RayDbgCount % 300) == 0)
				{
					const bool bSunFin = FMath::IsFinite(StarRelKm.X) && FMath::IsFinite(StarRelKm.Y) && FMath::IsFinite(StarRelKm.Z);
					const bool bCenFin = FMath::IsFinite(GoverningPlanet->CenterCamRelativeKm.X)
						&& FMath::IsFinite(GoverningPlanet->CenterCamRelativeKm.Y)
						&& FMath::IsFinite(GoverningPlanet->CenterCamRelativeKm.Z);
					const bool bQuatFin = FMath::IsFinite(PlanetQuat.X) && FMath::IsFinite(PlanetQuat.Y)
						&& FMath::IsFinite(PlanetQuat.Z) && FMath::IsFinite(PlanetQuat.W);
					UE_LOG(LogHillaireAtmosphere, Log,
						TEXT("[ViewExt][DebugRay] Gov=%s SunLocal=(%.6f,%.6f,%.6f) StarRel=(%.3f,%.3f,%.3f)km CenterRel=(%.3f,%.3f,%.3f)km Quat=(%.4f,%.4f,%.4f,%.4f) TopR=%.3f GrndOff=%.4f ViewH=%.3f FoundStar=%d SunFin=%d CenFin=%d QuatFin=%d Mode=%d"),
						*GoverningPlanet->PlanetName.ToString(),
						GoverningPlanet->StarDirectionLocal.X, GoverningPlanet->StarDirectionLocal.Y, GoverningPlanet->StarDirectionLocal.Z,
						StarRelKm.X, StarRelKm.Y, StarRelKm.Z,
						GoverningPlanet->CenterCamRelativeKm.X, GoverningPlanet->CenterCamRelativeKm.Y, GoverningPlanet->CenterCamRelativeKm.Z,
						PlanetQuat.X, PlanetQuat.Y, PlanetQuat.Z, PlanetQuat.W,
						GoverningPlanet->Profile.TopRadiusKm,
						HillaireLimits::PlanetRadiusOffsetKm,
						ClampedHeightKm,
						bFoundStarPos ? 1 : 0, bSunFin ? 1 : 0, bCenFin ? 1 : 0, bQuatFin ? 1 : 0,
						DebugMode);
				}
			}

			FRDGTextureRef OutTex = nullptr;
			if (LutManager->CompositeDebugVisualization(
				GraphBuilder, CurrentColorSrv, ViewRect, DebugMode,
				RdgT, RdgMs, RdgSky, RdgAerial,
				GoverningPlanet->Profile, RdgSceneDepth,
				AerialViewInputs, GoverningPlanet->StarDirectionLocal,
				ActualViewHeightKm, StarRelKm, GoverningPlanet->CenterCamRelativeKm,
				PlanetQuat, OutTex) && OutTex)
			{
				return FScreenPassTexture(OutTex, ViewRect);
			}
		}
	}

	// ---- Stage 1: Aerial Perspective (camera inside atmosphere) ----
	{
		const bool bCameraInside = GoverningPlanet->ViewHeightKm < GoverningPlanet->Profile.TopRadiusKm;

		const bool bShouldAerial = FHillaireLutManager::ShouldCompositeAerial(
			CVarHillaireAerialEval.GetValueOnRenderThread() != 0,
			Snapshot->HasAtmosphereContent(), bCameraInside, bHaveLutT, bHaveLutMS);

		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[ViewExt] PlanetaryCompositePass: Aerial gate: CVar=%d Content=%d Inside=%d T=%d MS=%d => %d"),
			CVarHillaireAerialEval.GetValueOnRenderThread() ? 1 : 0,
			Snapshot->HasAtmosphereContent() ? 1 : 0,
			bCameraInside ? 1 : 0,
			bHaveLutT ? 1 : 0, bHaveLutMS ? 1 : 0,
			bShouldAerial ? 1 : 0);

		if (bShouldAerial)
		{
			FRDGTextureRef RdgVolume = nullptr;
			if (LutManager->EvaluateAerialPerspective(
				GraphBuilder,
				PlanetSlotIdx,
				GoverningPlanet->Profile,
				AerialViewInputs,
				GoverningPlanet->StarDirectionLocal,
				OutLuts.Transmittance,
				OutLuts.MultiScattering,
				RdgVolume) && RdgVolume)
			{
				FRDGTextureRef OutTex = nullptr;
				if (LutManager->CompositeAerialPerspective(
					GraphBuilder,
					CurrentColorSrv,
					RdgSceneDepth,
					View.ViewUniformBuffer,
					AerialViewInputs.InvProjMatrix,
					ScaledSunAttenuation,
					HillaireLimits::AerialKmPerSliceForEnvelope(
						GoverningPlanet->Profile.TopRadiusKm - GoverningPlanet->Profile.BottomRadiusKm),
					// CALIBRATION PASS 2 presentation inputs: camera altitude
					// fraction in the envelope (entry continuity) + sun
					// elevation at the camera up (terrain sunset response).
					// Transmittance stays physical; only the in-scatter term
					// is presentation-scaled, in the shader and its CPU mirror.
					FMath::Clamp(
						(GoverningPlanet->ViewHeightKm - GoverningPlanet->Profile.BottomRadiusKm)
							/ FMath::Max(GoverningPlanet->Profile.TopRadiusKm - GoverningPlanet->Profile.BottomRadiusKm, 1e-6f),
						0.0f, 1.0f),
					CompositeFrame.bValid ? CompositeFrame.SunElevationCos
						: HillairePlanetMath::SunElevationCos(
							GoverningPlanet->StarDirectionLocal,
							HillairePlanetMath::CameraUpLocal(
								GoverningPlanet->CenterCamRelativeKm, GoverningPlanet->RotationWS)),
					RdgVolume,
					ViewRect,
					OutTex) && OutTex)
				{
					CurrentColorSrv = GraphBuilder.CreateSRV(OutTex);
					CurrentColorTex = OutTex;
					bTouched = true;
					UE_LOG(LogHillaireAtmosphere, Log, TEXT("[ViewExt] PlanetaryCompositePass: Aerial composite EXECUTED"));
				}
				else
				{
					UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] PlanetaryCompositePass: Aerial composite COMPOSITE FAILED"));
				}
			}
			else
			{
				UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] PlanetaryCompositePass: Aerial Evaluate FAILED"));
			}
		}
	}

	// ---- Stage 2: Sky Background (SkyView sampling) ----
	{
		const bool bSkyCVar = CVarHillaireSkyEnable.GetValueOnRenderThread() != 0;
		const bool bCameraInside = GoverningPlanet->ViewHeightKm < GoverningPlanet->Profile.TopRadiusKm;
		// Outside-atmosphere limb path (ZEPHYR correction pass): ATMOS sky is
		// a physical phenomenon, not a ZEPHYR presentation effect, so its
		// visibility is NOT tied to the ZEPHYR transition factor (which stays
		// 0 in deep space). When the camera is outside, the pass still runs
		// iff the RT view frustum intersects the governing top sphere; the
		// per-pixel shader then decides exact rays (ray x top-sphere test:
		// miss -> deep space passthrough, hit -> limb scattering, planet
		// pixels -> opaque passthrough identical). Frustum test is pure
		// geometry (engine FConvexVolume, padded 2% for float narrowing of
		// far-field centers) - no distance fade, no WidthKm, no second gate.
		// Aerial stays inside-only (camera volume is empty outside; the
		// reference falls through to a full march there, which UE ports as
		// this sky-limb path). ShouldCompositeSky itself is untouched.
		bool bFrustumHitsAtmosphere = false;
		if (!bCameraInside && CVarHillaireOutsideLimb.GetValueOnRenderThread() != 0)
		{
			const float TopSphereRadiusCm =
				GoverningPlanet->Profile.TopRadiusKm * 100000.0f * 1.02f;
			bFrustumHitsAtmosphere = View.ViewFrustum.IntersectSphere(
				GoverningPlanet->CenterWS, TopSphereRadiusCm);
		}
		const bool bShouldSky = FHillaireLutManager::ShouldCompositeSky(
			bSkyCVar,
			Snapshot->HasAtmosphereContent(),
			GoverningPlanet->ResolvedLights.Count > 0,
			bHaveLutSky,
			bHaveLutT,
			bCameraInside)
			|| (bSkyCVar
				&& Snapshot->HasAtmosphereContent()
				&& GoverningPlanet->ResolvedLights.Count > 0
				&& bHaveLutSky
				&& bHaveLutT
				&& bFrustumHitsAtmosphere);

		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[ViewExt] PlanetaryCompositePass: Sky gate: CVar=%d Content=%d HasLights=%d SkyLUT=%d T=%d Inside=%d FrustumHit=%d LimbCVar=%d => %d"),
			bSkyCVar ? 1 : 0,
			Snapshot->HasAtmosphereContent() ? 1 : 0,
			GoverningPlanet->ResolvedLights.Count > 0 ? 1 : 0,
			bHaveLutSky ? 1 : 0,
			bHaveLutT ? 1 : 0,
			bCameraInside ? 1 : 0,
			bFrustumHitsAtmosphere ? 1 : 0,
			CVarHillaireOutsideLimb.GetValueOnRenderThread() ? 1 : 0,
			bShouldSky ? 1 : 0);

		if (bShouldSky)
		{
			// Sun disk rendering is DISABLED on the ATMOS composite path: the
			// rendered disk was an artificial emissive contribution (arbitrary
			// luminance authored for a bloom-free demo), drove UE bloom and
			// auto-exposure, and had no physical justification over the surface
			// or the limb. The resolved light still drives scattering,
			// transmittance, the terminator and the aerial perspective; only the
			// drawn disk is removed (angular radius 0 => HillaireGetSunLuminance
			// returns 0 in both the deep-space and sky branches).
			const float SunAngularRadiusRad = 0.0f;

			FRDGTextureRef OutTex = nullptr;
			if (LutManager->CompositeSkyBackground(
				GraphBuilder,
				CurrentColorSrv,
				RdgSceneDepth,
				View.ViewUniformBuffer,
				AerialViewInputs.InvProjMatrix,
				CameraPlanetLocalKm,
				AerialViewInputs.ViewToPlanetLocalRot,
				GoverningPlanet->StarDirectionLocal,
				ScaledSunAttenuation,
				GoverningPlanet->Profile.BottomRadiusKm,
				GoverningPlanet->Profile.TopRadiusKm,
				ClampedHeightKm,
				SunAngularRadiusRad,
				OutLuts.SkyView,
				OutLuts.Transmittance,
				ViewRect,
				OutTex) && OutTex)
			{
				CurrentColorSrv = GraphBuilder.CreateSRV(OutTex);
				CurrentColorTex = OutTex;
				bTouched = true;
				UE_LOG(LogHillaireAtmosphere, Log, TEXT("[ViewExt] PlanetaryCompositePass: Sky composite EXECUTED"));
			}
			else
			{
				UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] PlanetaryCompositePass: Sky composite FAILED"));
			}
		}
	}

	if (!bTouched || !CurrentColorTex)
	{
		return Passthrough();
	}
	return FScreenPassTexture(CurrentColorTex, ViewRect);
}

FHillaireLutManager::FHillaireCompositeViewInputs FHillairePlanetaryViewExtension::ComputeCompositeViewInputs(
	const FPlanetAtmosphereState& Planet,
	const FHillaireAtmosphereFrameState& Snapshot,
	const FSceneView& View) const
{
	FHillaireLutManager::FHillaireCompositeViewInputs Out;

	// Re-anchor snapshot center to RT view origin: double-subtract in cm, then narrow
	const FVector OriginDeltaCm = Snapshot.ViewOriginWS - View.ViewMatrices.GetViewOrigin();
	if (!FMath::IsFinite(OriginDeltaCm.X) || !FMath::IsFinite(OriginDeltaCm.Y) || !FMath::IsFinite(OriginDeltaCm.Z))
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] ComputeCompositeViewInputs: OriginDeltaCm not finite"));
		return Out;
	}

	const FVector3f OriginDeltaKm(
		(float)(OriginDeltaCm.X * HillaireLimits::KmPerCm),
		(float)(OriginDeltaCm.Y * HillaireLimits::KmPerCm),
		(float)(OriginDeltaCm.Z * HillaireLimits::KmPerCm));

	const FVector3f CenterRelRT = Planet.CenterCamRelativeKm + OriginDeltaKm;
	if (CenterRelRT.ContainsNaN()) 
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] ComputeCompositeViewInputs: CenterRelRT contains NaN"));
		return Out;
	}

	Out.ViewHeightKm = CenterRelRT.Size();

	// Planet-local camera + up
	Out.CameraPlanetLocalKm = HillairePlanetMath::CameraPlanetLocalKm(CenterRelRT, Planet.RotationWS);
	if (Out.CameraPlanetLocalKm.ContainsNaN()) 
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] ComputeCompositeViewInputs: CameraPlanetLocalKm contains NaN"));
		return Out;
	}
	Out.CameraUpLocal = HillairePlanetMath::CameraUpLocal(CenterRelRT, Planet.RotationWS);
	Out.SunElevationCos = HillairePlanetMath::SunElevationCos(Planet.StarDirectionLocal, Out.CameraUpLocal);

	// View-to-planet-local rotation from RT view
	FMatrix ViewToWorld(EForceInit::ForceInitToZero);
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 C = 0; C < 3; ++C)
		{
			const double V = View.ViewMatrices.GetWorldToView().M[C][R];
			if (!FMath::IsFinite(V)) 
			{
				UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] ComputeCompositeViewInputs: View matrix not finite"));
				return Out;
			}
			ViewToWorld.M[R][C] = V;
		}
	}
	ViewToWorld.M[3][3] = 1.0;
	Out.ViewToPlanetLocalRot = ViewToWorld * Planet.RotationWS.Inverse().ToMatrix();

	// Inverse RT projection
	const FMatrix InvProj = View.ViewMatrices.GetViewToClip().Inverse();
	if (!FMath::IsFinite(InvProj.M[0][0]) || !FMath::IsFinite(InvProj.M[3][3])) 
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] ComputeCompositeViewInputs: InvProj not finite"));
		return Out;
	}
	for (int32 R = 0; R < 4; ++R)
	{
		for (int32 C = 0; C < 4; ++C)
		{
			if (!FMath::IsFinite(InvProj.M[R][C])) 
			{
				UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[ViewExt] ComputeCompositeViewInputs: InvProj[%d][%d] not finite"), R, C);
				return Out;
			}
		}
	}
	Out.InvProjMatrix = InvProj;
	Out.bValid = true;

	UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[ViewExt] ComputeCompositeViewInputs: OK ViewH=%.3f SunElev=%.3f bValid=%d"), Out.ViewHeightKm, Out.SunElevationCos, Out.bValid ? 1 : 0);

	return Out;
}

void FHillairePlanetaryViewExtension::LogCompositeState(const FHillaireAtmosphereFrameState& Snapshot,
	const FPlanetAtmosphereState& Planet,
	bool bHaveLutT, bool bHaveLutMS, bool bHaveLutSky,
	const FIntRect& ViewRect) const
{
	static uint64 CallCount = 0;
	++CallCount;
	if (CallCount != 1 && (CallCount % 300) != 0) return;

	UE_LOG(LogHillaireAtmosphere, Log,
		TEXT("Composite: planet=%s id=%s h=%.3f top=%.3f bot=%.3f sunLocal=(%.4f,%.4f,%.4f) ")
		TEXT("T=%d MS=%d Sky=%d rect=%dx%d"),
		*Planet.PlanetName.ToString(), *Planet.PlanetId.ToString(),
		Planet.ViewHeightKm, Planet.Profile.TopRadiusKm, Planet.Profile.BottomRadiusKm,
		Planet.StarDirectionLocal.X, Planet.StarDirectionLocal.Y, Planet.StarDirectionLocal.Z,
		bHaveLutT ? 1 : 0, bHaveLutMS ? 1 : 0, bHaveLutSky ? 1 : 0,
		ViewRect.Width(), ViewRect.Height());
}

void FHillairePlanetaryViewExtension::PruneStaleSnapshots(uint64 CurrentFrame)
{
	static constexpr uint64 KeepFrames = 30;
	FScopeLock Lock(&StashLock);
	TArray<const FSceneViewStateInterface*> Stale;
	for (const auto& Pair : SnapshotStash)
	{
		if (Pair.Value.Frame + KeepFrames < CurrentFrame)
		{
			Stale.Add(Pair.Key);
		}
	}
	for (const FSceneViewStateInterface* Key : Stale)
	{
		SnapshotStash.Remove(Key);
	}
}