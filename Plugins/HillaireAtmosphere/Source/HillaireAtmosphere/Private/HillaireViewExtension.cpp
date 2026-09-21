#include "HillaireViewExtension.h"

#include "Engine/World.h"
#include "HillaireAtmosphereLog.h"
#include "HillaireAtmosphereSubsystem.h"
#include "HillaireLutManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneView.h"
#include "ScreenPass.h"

FHillaireViewExtension::FHillaireViewExtension(const FAutoRegister& AutoRegister, UHillaireAtmosphereSubsystem* InSubsystem)
	: FSceneViewExtensionBase(AutoRegister)
	, Subsystem(InSubsystem)
{
}

FHillaireViewExtension::~FHillaireViewExtension() = default;

// Phase 2C: on-demand aerial evaluation gate. Default OFF: the camera volume
// is view-dependent and has no composite consumer yet, so running it every
// frame would burn GPU for nothing (task section 20). Validation commands
// and automation drive FHillaireLutManager::EvaluateAerialPerspective
// directly; this path keeps the production ViewExtension wiring ready for
// the Phase-2D composite with zero cost until then.
static TAutoConsoleVariable<int32> CVarHillaireAerialEval(
	TEXT("r.Hillaire.AerialEval"),
	0,
	TEXT("Evaluate the Hillaire aerial perspective camera volume for the governing planet (0 = off, 1 = on)."),
	ECVF_RenderThreadSafe);

// Production sky background (SkyView sampling) at the same BeforeDOF slot.
// Default ON: the SkyView LUT is already generated in PreRenderView whenever
// the snapshot has content, so the sky is visible in a normal PIE without
// opting into the (default-off, cost-bearing) aerial volume evaluation.
static TAutoConsoleVariable<int32> CVarHillaireSkyEnable(
	TEXT("r.Hillaire.SkyEnable"),
	1,
	TEXT("Composite the Hillaire SkyView sky background over background pixels (0 = off, 1 = on)."),
	ECVF_RenderThreadSafe);

// Throttled end-to-end diagnostics for the production sky path (governing
// planet, heights, sun, LUT availability, gate outcome, dispatch rect).
// Default OFF; enable in PIE/-game to trace the real camera -> LUT -> screen
// path without spamming the log (first execution + every 300 executions).
static TAutoConsoleVariable<int32> CVarHillaireSkyLog(
	TEXT("r.Hillaire.SkyLog"),
	0,
	TEXT("Log the Hillaire sky composite gate inputs/outcome per view (0 = off, 1 = on)."),
	ECVF_RenderThreadSafe);

// FASE-10 debug visualization (diagnostic only, real GPU LUT data):
// 0 = normal rendering, 1 = Transmittance LUT, 2 = MultiScattering LUT,
// 3 = SkyView LUT, 4 = Aerial Perspective volume (needs r.Hillaire.AerialEval 1),
// 5 = atmosphere density/profile. Replaces the normal composites while active.
static TAutoConsoleVariable<int32> CVarHillaireDebugMode(
	TEXT("r.Hillaire.DebugMode"),
	0,
	TEXT("Visualize real Hillaire GPU LUT data instead of the normal composite (0 = off, 1 = Transmittance, 2 = MultiScattering, 3 = SkyView, 4 = Aerial volume, 5 = density/profile)."),
	ECVF_RenderThreadSafe);

// ATMOS FIX VISIVO DEFINITIVO diagnostics (all default OFF, RenderThread):
// - r.Hillaire.DebugPlanet: throttled governing-planet record (center,
//   radii, camera altitude, sun, LUT build counters/target validity).
// - r.Hillaire.DebugStability: per-frame regen-input record for the
//   flicker hunt (height, sun elevation, profile/light identity, transient
//   availability, gate outcome). Log this with a static camera: any
//   sky-state change must come with a logged input change.
// - r.Hillaire.DebugCoordinates: GT snapshot vs RT view frame record
//   (origins, centers, heights, frame fallback flag).
static TAutoConsoleVariable<int32> CVarHillaireDebugPlanet(
	TEXT("r.Hillaire.DebugPlanet"),
	0,
	TEXT("Log the governing planet record per composite (0 = off, 1 = on, throttled)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarHillaireDebugStability(
	TEXT("r.Hillaire.DebugStability"),
	0,
	TEXT("Log the per-frame LUT regen inputs per composite (0 = off, 1 = on)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarHillaireDebugCoordinates(
	TEXT("r.Hillaire.DebugCoordinates"),
	0,
	TEXT("Log the GT snapshot vs RT view coordinate frames per composite (0 = off, 1 = on, throttled)."),
	ECVF_RenderThreadSafe);

// Reference sun-illuminance knob (mirrors the DX11 sample mSunIlluminanceScale
// slider, range 0.1-100): linear multiplier on the slot-0 sun throughput at
// composite time. Default 1.0 = validated transfer untouched. The reference
// renders unit-sun transfer through a FIXED tonemap exposure of 10; UE uses
// adaptive exposure, which meters mostly-black space frames low and leaves a
// physically-correct unit sky dim. Raising this (8-10 mirrors the reference
// look) is radiometrically identical to baking the scale into the LUTs (the
// whole chain is linear in sun throughput - the N-light exactness proof), so
// it never alters gradients, limb shape, or contrast. This is calibration,
// not a gain hack: LUT math stays transfer-pure.
static TAutoConsoleVariable<float> CVarHillaireSunScale(
	TEXT("r.Hillaire.SunScale"),
	1.0f,
	TEXT("Linear sun throughput scale at composite (reference mSunIlluminanceScale, default 1.0 = validated transfer). 8-10 mirrors the reference fixed-exposure-10 look under UE adaptive exposure."),
	ECVF_RenderThreadSafe);

bool FHillaireViewExtension::ShouldHandleView(const FSceneViewFamily& InViewFamily) const
{
	const UHillaireAtmosphereSubsystem* Sub = Subsystem.Get();
	if (!Sub || !UHillaireAtmosphereSubsystem::IsEnabledByCVar() || !Sub->bAtmosphereEnabled)
	{
		return false;
	}
	const UWorld* World = Sub->GetWorld();
	if (!World || !World->Scene)
	{
		return false;
	}
	// Only views of our own world scene (multi-world/PIE safe).
	if (InViewFamily.Scene != static_cast<const FSceneInterface*>(World->Scene))
	{
		return false;
	}
	return Sub->GetRegisteredPlanetCount() > 0;
}

void FHillaireViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	// Per-family GT hook reserved for family-wide work (mode selection across
	// views, stereo snapshot sharing). Per-view snapshots build in SetupView.
	PruneStaleSnapshots(GFrameCounter);
}

void FHillaireViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	if (!ShouldHandleView(InViewFamily))
	{
		if (CVarHillaireSkyLog.GetValueOnGameThread() != 0)
		{
			LogSkySetupViewState(InViewFamily, InView, nullptr);
		}
		return;
	}
	UHillaireAtmosphereSubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		return;
	}

	// GameThread: resolve world state -> immutable snapshot. ViewOrigin is the
	// per-view camera-relative origin (double, cm); the builder narrows to
	// float km AFTER subtracting, preserving far-field precision.
	//
	// The snapshot matrices live in the CAMERA-RELATIVE KM frame, not the UE
	// world frame: the UE view matrix translates by the absolute origin (cm),
	// which would overflow float32 at planetary scales, so its translation is
	// stripped here (the camera sits at the relative origin by construction;
	// rotation is preserved). The projection matrix is passed through as-is:
	// perspective division is scale-invariant, so it projects view-space km
	// exactly like view-space cm.
	const FVector ViewOriginCm = InView.ViewMatrices.GetViewOrigin();
	FMatrix RelativeViewMatrix = InView.ViewMatrices.GetWorldToView();
	RelativeViewMatrix.M[3][0] = 0.0f;
	RelativeViewMatrix.M[3][1] = 0.0f;
	RelativeViewMatrix.M[3][2] = 0.0f;
	const FMatrix ProjMatrix = InView.ViewMatrices.GetViewToClip();
	const FIntRect ViewRect = InView.UnscaledViewRect;
	const FVector ViewDir = InView.GetViewDirection();

	TSharedPtr<const FHillaireViewSnapshot> Snapshot = MakeShared<const FHillaireViewSnapshot>(
		Sub->BuildSnapshotForView(ViewOriginCm, RelativeViewMatrix, ProjMatrix, ViewRect, ViewDir));

	if (CVarHillaireSkyLog.GetValueOnGameThread() != 0)
	{
		LogSkySetupViewState(InViewFamily, InView, Snapshot.Get());
	}

	// GT -> RT matching by shared view state (never the view address: the
	// renderer copy-constructs every GT view into an RT-local FViewInfo).
	// Null-State views are never stashed (passthrough downstream).
	if (const FSceneViewStateInterface* Key = MakeSnapshotKey(InView))
	{
		FScopeLock Lock(&StashLock);
		FHillaireStashedSnapshot Entry;
		Entry.Snapshot = Snapshot;
		Entry.Frame = GFrameCounter;
		SnapshotStash.Add(Key, MoveTemp(Entry));
	}
}

const FSceneViewStateInterface* FHillaireViewExtension::MakeSnapshotKey(const FSceneView& InView)
{
	return InView.State;
}

TSharedPtr<const FHillaireViewSnapshot> FHillaireViewExtension::FindSnapshot(const FSceneView& InView) const
{
	const FSceneViewStateInterface* Key = MakeSnapshotKey(InView);
	if (!Key)
	{
		return nullptr;
	}
	FScopeLock Lock(&StashLock);
	if (const FHillaireStashedSnapshot* Found = SnapshotStash.Find(Key))
	{
		return Found->Snapshot;
	}
	return nullptr;
}

void FHillaireViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	// RenderThread: intentionally a no-op (ATMOS FIX VISIVO DEFINITIVO).
	//
	// LUT generation NO LONGER happens here. Generating T/MS/SkyView in this
	// graph and consuming the pooled targets in the BeforeDOF composite (a
	// LATER build phase of the SAME frame graph) was the flicker/absence root
	// cause: QueueTextureExtraction leaves the pooled slot unusable until this
	// graph executes, so on every regeneration frame the composite saw null
	// targets and passed the scene through untouched (sky missing exactly when
	// the camera moved, the planet spun/orbited, or the sun moved - i.e.
	// almost always in live PIE). Generation now happens IN the BeforeDOF
	// composite graph (AerialCompositePass -> EnsurePlanetLuts), whose
	// transient outputs feed the aerial evaluation and the sky composite in
	// the same graph: the same same-graph transient handoff already proven by
	// the aerial fix. The pooled extraction queued there still serves
	// debug/dump readers (1-frame latency, GameThread-safe).
	(void)GraphBuilder;
	(void)InView;
}

void FHillaireViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass, const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// BeforeDOF: unconditional pre-tonemap linear-HDR slot (no feature gating
	// on this location, unlike MotionBlur/Tonemap/FXAA which follow the pass
	// sequence). This is where aerial perspective belongs radiometrically.
	if (Pass != EPostProcessingPass::BeforeDOF)
	{
		return;
	}
	InOutPassCallbacks.Add(
		FPostProcessingPassDelegate::CreateRaw(this, &FHillaireViewExtension::AerialCompositePass));
}

FScreenPassTexture FHillaireViewExtension::AerialCompositePass(
	FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTextureSlice InSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	const auto Passthrough = [&]() -> FScreenPassTexture
	{
		return FScreenPassTexture(InSlice);
	};

	// Gate: GT snapshot for this exact view (same stash as PreRenderView,
	// matched by shared view state across the GT -> RT view copy).
	const TSharedPtr<const FHillaireViewSnapshot> Snapshot = FindSnapshot(View);
	if (CVarHillaireSkyLog.GetValueOnRenderThread() != 0)
	{
		LogSkyHookEntry(Snapshot.Get(), View);
	}
	if (!Snapshot.IsValid() || !Snapshot->HasAtmosphereContent())
	{
		return Passthrough();
	}
	// Gate: governing planet with a resolved sun.
	const FHillaireSnapshotPlanet* GoverningPlanet = nullptr;
	for (const FHillaireSnapshotPlanet& P : Snapshot->Planets)
	{
		if (P.bIsGoverning)
		{
			GoverningPlanet = &P;
			break;
		}
	}
	if (!GoverningPlanet || GoverningPlanet->ResolvedLights.Count == 0)
	{
		return Passthrough();
	}
	// Governing change note: flaps between planets would morph the whole sky
	// frame-to-frame. Fires only on change.
	if (GoverningPlanet->PlanetId != LastGoverningLogId)
	{
		LastGoverningLogId = GoverningPlanet->PlanetId;
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("SkyGoverning: now planet id=%d ('%s')"),
			GoverningPlanet->PlanetId, *GoverningPlanet->PlanetName.ToString());
	}

	UHillaireAtmosphereSubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		return Passthrough();
	}
	FHillaireLutManager* LutManager = Sub->GetLutManager();
	if (!LutManager)
	{
		return Passthrough();
	}
	// Slot-0 sun throughput with the reference illuminance scale applied once
	// here (never inside LUTs: they stay transfer-pure). Default 1.0.
	const float SunScale = CVarHillaireSunScale.GetValueOnRenderThread();
	const FVector3f ScaledSunAttenuation =
		GoverningPlanet->ResolvedLights.Lights[0].ColorAttenuation * SunScale;
	// Depth ref out of the post-processing scene-textures UB contents (same
	// values the materials sample; read-only, no copy). Chained operator->
	// resolves TRDGUniformBufferBinding -> contents struct -> member.
	FRDGTextureRef RdgSceneDepth = nullptr;
	{
		const TRDGUniformBufferBinding<FSceneTextureUniformParameters>& DepthUB =
			Inputs.SceneTextures.SceneTextures;
		// Binding -> UB object -> contents struct -> depth member. Each arrow
		// is a public accessor; contents are immutable once the UB exists.
		if (DepthUB && DepthUB->GetContents())
		{
			RdgSceneDepth = DepthUB->GetContents()->SceneDepthTexture;
		}
	}
	if (!RdgSceneDepth)
	{
		return Passthrough();
	}

	// ---- Stage 0: same-graph LUT ensure for the governing planet ----
	// ATMOS FIX VISIVO DEFINITIVO. Transmittance + MultiScattering + SkyView
	// are generated IN THIS RDG GRAPH (EnsurePlanetLuts decides outside graph
	// construction whether passes enqueue: regen -> fresh transient + pooled
	// extraction for debug readers; reuse -> pooled import). The transient
	// OutLuts feed stages 1-2 below IN THE SAME GRAPH. Consuming the pooled
	// targets here instead (as the old PreRenderView path did) showed null on
	// every regeneration frame - the sky vanished exactly when the camera
	// moved, the planet spun/orbited, or the sun moved.
	FHillaireLutManager::FHillairePlanetLutGraphOutputs OutLuts;
	if (!LutManager->EnsurePlanetLuts(
		GraphBuilder,
		GoverningPlanet->PlanetId,
		GoverningPlanet->Profile,
		GoverningPlanet->ViewHeightKm,
		GoverningPlanet->CenterCamRelativeKm,
		GoverningPlanet->Rotation,
		GoverningPlanet->ResolvedLights,
		Snapshot->MultipleScatteringFactor,
		Snapshot->bFastSkyEnabled,
		OutLuts))
	{
		return Passthrough();
	}
	const bool bHaveLutT = OutLuts.Transmittance != nullptr;
	const bool bHaveLutMS = OutLuts.MultiScattering != nullptr;
	const bool bHaveLutSky = OutLuts.SkyView != nullptr;

	// ---- RT-exact composite frame (task C/F) ----
	// The GT snapshot is built a frame AHEAD of the RT view consuming it.
	// Rays derived from snapshot matrices alone would render the shell with
	// a stale camera while the depth buffer was rasterized with the current
	// one (shell offset vs terrain + limb flicker under any motion). The
	// planet is therefore re-anchored to THIS view's own origin/matrices
	// (same center world position/rotation/radii/sun from the snapshot; only
	// the camera-relative presentation is re-based, so LUT content baked
	// from snapshot inputs stays valid). Degenerate RT inputs fall back to
	// the snapshot-derived frame (previous behavior, never worse).
	const FVector RTViewOriginCm = View.ViewMatrices.GetViewOrigin();
	const FMatrix RTViewMatrix = View.ViewMatrices.GetWorldToView();
	const FMatrix RTProjMatrix = View.ViewMatrices.GetViewToClip();
	const FHillaireLutManager::FHillaireCompositeViewInputs CompositeFrame =
		FHillaireLutManager::ComputeCompositeViewInputs(
			GoverningPlanet->CenterCamRelativeKm,
			GoverningPlanet->Rotation,
			Snapshot->ViewOriginCm,
			RTViewOriginCm,
			RTViewMatrix,
			RTProjMatrix,
			GoverningPlanet->ResolvedLights.Lights[0].LightDirLocal);
	FHillaireLutManager::FHillaireAerialViewInputs ViewInputs;
	FVector3f CameraPlanetLocalKm;
	if (CompositeFrame.bValid)
	{
		ViewInputs.CameraPlanetLocalKm = CompositeFrame.CameraPlanetLocalKm;
		ViewInputs.InvProjMatrix = CompositeFrame.InvProjMatrix;
		ViewInputs.ViewToPlanetLocalRot = CompositeFrame.ViewToPlanetLocalRot;
		CameraPlanetLocalKm = CompositeFrame.CameraPlanetLocalKm;
	}
	else
	{
		ViewInputs = FHillaireLutManager::ComputeAerialViewInputs(
			GoverningPlanet->CenterCamRelativeKm,
			GoverningPlanet->Rotation,
			Snapshot->ViewMatrix,
			Snapshot->ProjectionMatrix);
		CameraPlanetLocalKm = ViewInputs.CameraPlanetLocalKm;
	}
	// Bake-height convention (frozen): the SkyView LUT is an inside-height
	// parameterization, so the sampling height is clamped into
	// [Bottom + 20 m floor, Top - 20 m floor] exactly like EnsurePlanetLuts
	// bakes it (outside cameras sample the top boundary radiance).
	const float HeightFloorKm =
		GoverningPlanet->Profile.BottomRadiusKm + HillaireLimits::ViewHeightEpsFloorKm;
	const float HeightCeilKm = FMath::Max(
		HeightFloorKm,
		GoverningPlanet->Profile.TopRadiusKm - HillaireLimits::ViewHeightEpsFloorKm);
	const float ClampedHeightKm = FMath::Clamp(
		GoverningPlanet->ViewHeightKm, HeightFloorKm, HeightCeilKm);

	if (CVarHillaireDebugCoordinates.GetValueOnRenderThread() != 0)
	{
		LogCompositeCoordinates(*Snapshot, *GoverningPlanet, RTViewOriginCm,
			CompositeFrame, ClampedHeightKm);
	}
	if (CVarHillaireDebugPlanet.GetValueOnRenderThread() != 0)
	{
		LogCompositePlanet(GoverningPlanet->PlanetId, *Snapshot, *GoverningPlanet,
			bHaveLutT, bHaveLutMS, bHaveLutSky, LutManager);
	}

	// Two staged composites over the same BeforeDOF slot (both optional,
	// both identity when gated off: baseline rendering provably preserved).
	FRDGTextureSRVRef CurrentColorSrv = InSlice.TextureSRV;
	FRDGTextureRef CurrentColorTex = nullptr;
	bool bTouched = false;
	const FIntRect ViewRect = InSlice.ViewRect;

	// ---- FASE 10: debug visualization (real GPU LUT data, diagnostic only) ----
	// Replaces the normal composites while active (modes 1-5). Falls through
	// to the production path when the mode is 0/out of range or its source
	// is missing (baseline rendering provably preserved). Modes 1-3 consume
	// the Stage-0 SAME-GRAPH transients (always fresher than the pooled
	// copies, valid on regen frames too); mode 4 evaluates its transient
	// in-graph when its gate passes, else the previous frame's pooled
	// scratch (same 1-frame latency as before the fix).
	{
		const int32 DebugMode = CVarHillaireDebugMode.GetValueOnRenderThread();
		if (DebugMode >= 1 && DebugMode <= 22)
		{
			FRDGTextureRef RdgT = OutLuts.Transmittance;
			FRDGTextureRef RdgMs = OutLuts.MultiScattering;
			FRDGTextureRef RdgSky = OutLuts.SkyView;
			FRDGTextureRef RdgAerial = nullptr;
			TRefCountPtr<IPooledRenderTarget> AerialScratchKeepAlive;
			if (DebugMode == 4)
			{
				const bool bCameraInsideDbg =
					GoverningPlanet->ViewHeightKm < GoverningPlanet->Profile.TopRadiusKm;
				if (FHillaireLutManager::ShouldCompositeAerial(
					CVarHillaireAerialEval.GetValueOnRenderThread() != 0,
					Snapshot->HasAtmosphereContent(), bCameraInsideDbg, bHaveLutT, bHaveLutMS)
					&& LutManager->EvaluateAerialPerspective(
						GraphBuilder,
						GoverningPlanet->PlanetId,
						GoverningPlanet->Profile,
						ViewInputs,
						GoverningPlanet->ResolvedLights.Lights[0].LightDirLocal,
						RdgT,
						RdgMs,
						RdgAerial)
					&& RdgAerial)
				{
					// In-graph transient: visualized below, extraction queued
					// inside EvaluateAerialPerspective for dump readers.
				}
				else
				{
					RdgAerial = nullptr;
					AerialScratchKeepAlive = LutManager->CopyAerialScratch(GoverningPlanet->PlanetId);
					if (AerialScratchKeepAlive.IsValid())
					{
						RdgAerial = GraphBuilder.RegisterExternalTexture(AerialScratchKeepAlive);
					}
				}
			}
			// World-frame ground truth for Mode 20 (legacy snapshot carries
			// camera-relative light positions already).
			FVector3f StarRelKm = FVector3f::ZeroVector;
			if (GoverningPlanet->ResolvedLights.Count > 0)
			{
				const FGuid PrimaryId = GoverningPlanet->ResolvedLights.Lights[0].LightId;
				for (const FHillaireSnapshotLight& L : Snapshot->Lights)
				{
					if (L.LightId == PrimaryId && L.bEnabled)
					{
						StarRelKm = L.PositionCamRelativeKm;
						break;
					}
				}
			}
			const FVector4f PlanetQuat(GoverningPlanet->Rotation.X, GoverningPlanet->Rotation.Y,
				GoverningPlanet->Rotation.Z, GoverningPlanet->Rotation.W);
			FRDGTextureRef OutTex = nullptr;
			if (LutManager->CompositeDebugVisualization(
				GraphBuilder, CurrentColorSrv, ViewRect, DebugMode,
				RdgT, RdgMs, RdgSky, RdgAerial,
				GoverningPlanet->Profile, RdgSceneDepth,
				ViewInputs, GoverningPlanet->ResolvedLights.Lights[0].LightDirLocal,
				ClampedHeightKm, StarRelKm, GoverningPlanet->CenterCamRelativeKm,
				PlanetQuat, OutTex) && OutTex)
			{
				return FScreenPassTexture(OutTex, ViewRect);
			}
		}
	}

	// ---- Stage 1: aerial perspective (Phase-2D path, semantics frozen) ----
	// Evaluated IN THIS GRAPH (same-graph transient handoff): the volume is
	// view-dependent scratch, produced and consumed here with zero pool
	// latency, from the Stage-0 same-graph T/MS transients. The evaluation
	// also queues the pooled extraction that serves debug/dump readers
	// (1-frame latency, GameThread-safe).
	{
		const bool bCameraInside =
			GoverningPlanet->ViewHeightKm < GoverningPlanet->Profile.TopRadiusKm;

		// Single source of truth for the gating decision (unit-tested).
		if (FHillaireLutManager::ShouldCompositeAerial(
			CVarHillaireAerialEval.GetValueOnRenderThread() != 0,
			Snapshot->HasAtmosphereContent(), bCameraInside, bHaveLutT, bHaveLutMS))
		{
			FRDGTextureRef RdgVolume = nullptr;
			const bool bEvalOk = LutManager->EvaluateAerialPerspective(
				GraphBuilder,
				GoverningPlanet->PlanetId,
				GoverningPlanet->Profile,
				ViewInputs,
				GoverningPlanet->ResolvedLights.Lights[0].LightDirLocal,
				OutLuts.Transmittance,
				OutLuts.MultiScattering,
				RdgVolume);
			if (bEvalOk && RdgVolume)
			{
				FRDGTextureRef OutTex = nullptr;
				const bool bOk = LutManager->CompositeAerialPerspective(
					GraphBuilder,
					CurrentColorSrv,
					RdgSceneDepth,
					View.ViewUniformBuffer,
					ViewInputs.InvProjMatrix,
					ScaledSunAttenuation,
					HillaireLimits::AerialKmPerSliceForEnvelope(
						GoverningPlanet->Profile.TopRadiusKm - GoverningPlanet->Profile.BottomRadiusKm),
					RdgVolume,
					ViewRect,
					OutTex);
				if (bOk && OutTex)
				{
					CurrentColorSrv = GraphBuilder.CreateSRV(OutTex);
					CurrentColorTex = OutTex;
					bTouched = true;
				}
			}
		}
	}

	// ---- Stage 2: sky background (SkyView sampling, production sky path) ----
	// Composites live scattered sky radiance over background pixels
	// (reversed-Z far): sky + T * background (reference FASTSKY branch).
	// Opaque pixels pass through identical (aerial owns them; no
	// double-scatter). Camera-inside gate: space views must NOT show
	// a full-screen atmospheric sky; the planet limb is handled by
	// ray-marching when a view ray intersects the atmosphere.
	{
		const bool bSkyCVar = CVarHillaireSkyEnable.GetValueOnRenderThread() != 0;
		const bool bCameraInside =
			GoverningPlanet->ViewHeightKm < GoverningPlanet->Profile.TopRadiusKm;
		const bool bShouldSky = FHillaireLutManager::ShouldCompositeSky(
			bSkyCVar,
			Snapshot->HasAtmosphereContent(),
			GoverningPlanet->ResolvedLights.Count > 0,
			bHaveLutSky,
			bHaveLutT,
			bCameraInside);
		if (CVarHillaireSkyLog.GetValueOnRenderThread() != 0)
		{
			LogSkyCompositeState(
				*Snapshot, *GoverningPlanet,
				bSkyCVar, bHaveLutSky, bHaveLutT, bShouldSky, ViewRect);
		}
		if (CVarHillaireDebugStability.GetValueOnRenderThread() != 0)
		{
			LogCompositeStability(*Snapshot, *GoverningPlanet, CompositeFrame,
				ClampedHeightKm, bHaveLutT, bHaveLutMS, bHaveLutSky, bShouldSky);
		}
		if (bShouldSky)
		{
			FRDGTextureRef OutTex;
const float SunAngularRadiusRad = GoverningPlanet->ResolvedLights.Count > 0
			? GoverningPlanet->ResolvedLights.Lights[0].AngularRadiusRad
			: 0.0f;
		const bool bOk = LutManager->CompositeSkyBackground(
			GraphBuilder,
			CurrentColorSrv,
			RdgSceneDepth,
			View.ViewUniformBuffer,
			ViewInputs.InvProjMatrix,
			CameraPlanetLocalKm,
			ViewInputs.ViewToPlanetLocalRot,
			GoverningPlanet->ResolvedLights.Lights[0].LightDirLocal,
			ScaledSunAttenuation,
			GoverningPlanet->Profile.BottomRadiusKm,
			GoverningPlanet->Profile.TopRadiusKm,
			ClampedHeightKm,
			SunAngularRadiusRad,
			OutLuts.SkyView,
			OutLuts.Transmittance,
			ViewRect,
			OutTex);
			if (bOk && OutTex)
			{
				CurrentColorSrv = GraphBuilder.CreateSRV(OutTex);
				CurrentColorTex = OutTex;
				bTouched = true;
			}
		}
	}

	if (!bTouched || !CurrentColorTex)
	{
		return Passthrough();
	}
	return FScreenPassTexture(CurrentColorTex, ViewRect);
}

void FHillaireViewExtension::LogSkyCompositeState(
	const FHillaireViewSnapshot& Snapshot,
	const FHillaireSnapshotPlanet& GoverningPlanet,
	bool bSkyCVar, bool bHaveSkyView, bool bHaveTransmittance,
	bool bShouldSky, const FIntRect& ViewRect)
{
	// RenderThread-only counter (the hook runs on the RT by contract).
	static uint64 CallCount = 0;
	++CallCount;
	if (CallCount != 1 && (CallCount % 300) != 0)
	{
		return;
	}
	const FHillaireResolvedLight& Sun = GoverningPlanet.ResolvedLights.Count > 0
		? GoverningPlanet.ResolvedLights.Lights[0]
		: FHillaireResolvedLight();
	UE_LOG(LogHillaireAtmosphere, Log,
		TEXT("SkyComposite #%llu: planet='%s' id=%d h=%.3fkm top=%.3fkm bottom=%.3fkm ")
		TEXT("sun=(%.4f,%.4f,%.4f) atten=(%.4f,%.4f,%.4f) sunid=%s cvar=%d sky=%d trans=%d run=%d rect=%dx%d"),
		CallCount,
		*GoverningPlanet.PlanetName.ToString(), GoverningPlanet.PlanetId,
		GoverningPlanet.ViewHeightKm,
		GoverningPlanet.Profile.TopRadiusKm, GoverningPlanet.Profile.BottomRadiusKm,
		Sun.LightDirLocal.X, Sun.LightDirLocal.Y, Sun.LightDirLocal.Z,
		Sun.ColorAttenuation.X, Sun.ColorAttenuation.Y, Sun.ColorAttenuation.Z,
		*Sun.LightId.ToString(),
		bSkyCVar ? 1 : 0, bHaveSkyView ? 1 : 0, bHaveTransmittance ? 1 : 0,
		bShouldSky ? 1 : 0, ViewRect.Width(), ViewRect.Height());
	(void)Snapshot;
}

void FHillaireViewExtension::LogSkyHookEntry(const FHillaireViewSnapshot* Snapshot, const FSceneView& InView)
{
	// RenderThread-only counter (the hook runs on the RT by contract).
	static uint64 EntryCount = 0;
	++EntryCount;
	if (EntryCount != 1 && (EntryCount % 300) != 0)
	{
		return;
	}
	const int32 PlanetCount = Snapshot ? Snapshot->Planets.Num() : -1;
	const int32 GoverningId = (Snapshot && Snapshot->HasAtmosphereContent())
		? Snapshot->GoverningPlanetId : INDEX_NONE;
	UE_LOG(LogHillaireAtmosphere, Log,
		TEXT("SkyHook #%llu: view=%p state=%p snapshot=%d planets=%d governing=%d"),
		EntryCount, &InView, InView.State, Snapshot ? 1 : 0, PlanetCount, GoverningId);
}

void FHillaireViewExtension::LogSkySetupViewState(const FSceneViewFamily& InViewFamily, const FSceneView& InView,
	const FHillaireViewSnapshot* Snapshot)
{
	// Called on the GameThread (BeginRenderingViewFamily path).
	static uint64 SetupCount = 0;
	++SetupCount;
	if (SetupCount != 1 && (SetupCount % 300) != 0)
	{
		return;
	}
	const UHillaireAtmosphereSubsystem* Sub = Subsystem.Get();
	const bool bCVar = UHillaireAtmosphereSubsystem::IsEnabledByCVar();
	const bool bFlag = Sub ? Sub->bAtmosphereEnabled : false;
	const UWorld* World = Sub ? Sub->GetWorld() : nullptr;
	const bool bScene = (World && World->Scene)
		? (InViewFamily.Scene == static_cast<const FSceneInterface*>(World->Scene)) : false;
	const int32 PlanetCount = Sub ? Sub->GetRegisteredPlanetCount() : -1;
	const FVector Origin = InView.ViewMatrices.GetViewOrigin();
	int32 GovId = INDEX_NONE;
	float GovH = -1.0f;
	int32 SnapPlanets = -1;
	if (Snapshot != nullptr)
	{
		SnapPlanets = Snapshot->Planets.Num();
		if (Snapshot->HasAtmosphereContent())
		{
			GovId = Snapshot->GoverningPlanetId;
			for (const FHillaireSnapshotPlanet& P : Snapshot->Planets)
			{
				if (P.PlanetId == GovId)
				{
					GovH = P.ViewHeightKm;
					break;
				}
			}
		}
	}
	UE_LOG(LogHillaireAtmosphere, Log,
		TEXT("SkySetup #%llu: view=%p state=%p cvar=%d flag=%d scene=%d planets=%d handle=%d origin=(%.0f,%.0f,%.0f) snapplanets=%d gov=%d govh=%.2f"),
		SetupCount, &InView, InView.State,
		bCVar ? 1 : 0, bFlag ? 1 : 0, bScene ? 1 : 0, PlanetCount,
		ShouldHandleView(InViewFamily) ? 1 : 0,
		Origin.X, Origin.Y, Origin.Z, SnapPlanets, GovId, (double)GovH);
}

void FHillaireViewExtension::PruneStaleSnapshots(uint64 CurrentFrame)
{
	// Age-based prune (GameThread): entries not rewritten for
	// SnapshotStashKeepFrames frames belong to dead views (the RT only ever
	// reads entries for live views, which SetupView rewrites every frame).
	// Generous window: a lagging RT still finds real data during hitches
	// instead of flickering to passthrough.
	static constexpr uint64 SnapshotStashKeepFrames = 30;
	FScopeLock Lock(&StashLock);
	TArray<const FSceneViewStateInterface*> Stale;
	for (const auto& Pair : SnapshotStash)
	{
		if (Pair.Value.Frame + SnapshotStashKeepFrames < CurrentFrame)
		{
			Stale.Add(Pair.Key);
		}
	}
	for (const FSceneViewStateInterface* Key : Stale)
	{
		SnapshotStash.Remove(Key);
	}
}

void FHillaireViewExtension::LogCompositePlanet(
	int32 GoverningPlanetId,
	const FHillaireViewSnapshot& Snapshot,
	const FHillaireSnapshotPlanet& GoverningPlanet,
	bool bHaveLutT, bool bHaveLutMS, bool bHaveLutSky,
	FHillaireLutManager* LutManager)
{
	// RenderThread-only counter (the hook runs on the RT by contract).
	static uint64 CallCount = 0;
	++CallCount;
	if (CallCount != 1 && (CallCount % 300) != 0)
	{
		return;
	}
	const FHillaireResolvedLight& Sun = GoverningPlanet.ResolvedLights.Count > 0
		? GoverningPlanet.ResolvedLights.Lights[0]
		: FHillaireResolvedLight();
	uint32 Builds = 0;
	uint32 Reuses = 0;
	if (LutManager)
	{
		FHillairePlanetLutState LutCopy;
		if (LutManager->CopyLutState(GoverningPlanetId, LutCopy))
		{
			Builds = LutCopy.LutBuildCount;
			Reuses = LutCopy.LutReuseCount;
		}
	}
	const float AltitudeKm = GoverningPlanet.ViewHeightKm - GoverningPlanet.Profile.BottomRadiusKm;
	UE_LOG(LogHillaireAtmosphere, Log,
		TEXT("HillairePlanet #%llu: id=%d ('%s') centerRel=(%.3f,%.3f,%.3f)km ground=%.4fkm top=%.4fkm alt=%.4fkm ")
		TEXT("sunLocal=(%.4f,%.4f,%.4f) sunid=%s builds=%u reuses=%u transients=T%d/MS%d/Sky%d snaps=%d"),
		CallCount, GoverningPlanetId, *GoverningPlanet.PlanetName.ToString(),
		GoverningPlanet.CenterCamRelativeKm.X, GoverningPlanet.CenterCamRelativeKm.Y, GoverningPlanet.CenterCamRelativeKm.Z,
		GoverningPlanet.Profile.BottomRadiusKm, GoverningPlanet.Profile.TopRadiusKm, AltitudeKm,
		Sun.LightDirLocal.X, Sun.LightDirLocal.Y, Sun.LightDirLocal.Z,
		*Sun.LightId.ToString(), Builds, Reuses,
		bHaveLutT ? 1 : 0, bHaveLutMS ? 1 : 0, bHaveLutSky ? 1 : 0,
		Snapshot.Planets.Num());
}

void FHillaireViewExtension::LogCompositeStability(
	const FHillaireViewSnapshot& Snapshot,
	const FHillaireSnapshotPlanet& GoverningPlanet,
	const FHillaireLutManager::FHillaireCompositeViewInputs& CompositeFrame,
	float ClampedHeightKm,
	bool bHaveLutT, bool bHaveLutMS, bool bHaveLutSky,
	bool bShouldSky)
{
	// Unthrottled by design (opt-in flicker hunt): one line per composite
	// execution. With a static camera and a static sun, consecutive lines
	// must be identical; any sky change without an input change here points
	// at resource lifetime or the view path, not the predicates.
	static uint64 CallCount = 0;
	++CallCount;
	const FHillaireResolvedLight& Sun = GoverningPlanet.ResolvedLights.Count > 0
		? GoverningPlanet.ResolvedLights.Lights[0]
		: FHillaireResolvedLight();
	const uint64 ProfileHash = GoverningPlanet.Profile.ComputeContentHash();
	UE_LOG(LogHillaireAtmosphere, Log,
		TEXT("HillaireStability #%llu: gov=%d hSnap=%.6f hBake=%.6f hRT=%.6f elev=%.6f rtfb=%d ")
		TEXT("profHash=%llu sunid=%s ms=%.3f fast=%d transients=T%d/MS%d/Sky%d run=%d"),
		CallCount, GoverningPlanet.PlanetId,
		(double)GoverningPlanet.ViewHeightKm, (double)ClampedHeightKm,
		CompositeFrame.bValid ? (double)CompositeFrame.ViewHeightKm : -1.0,
		CompositeFrame.bValid ? (double)CompositeFrame.SunElevationCos : -3.0,
		CompositeFrame.bValid ? 0 : 1,
		ProfileHash, *Sun.LightId.ToString(),
		(double)Snapshot.MultipleScatteringFactor, Snapshot.bFastSkyEnabled ? 1 : 0,
		bHaveLutT ? 1 : 0, bHaveLutMS ? 1 : 0, bHaveLutSky ? 1 : 0,
		bShouldSky ? 1 : 0);
}

void FHillaireViewExtension::LogCompositeCoordinates(
	const FHillaireViewSnapshot& Snapshot,
	const FHillaireSnapshotPlanet& GoverningPlanet,
	const FVector& RTViewOriginCm,
	const FHillaireLutManager::FHillaireCompositeViewInputs& CompositeFrame,
	float ClampedHeightKm)
{
	// RenderThread-only counter (the hook runs on the RT by contract).
	static uint64 CallCount = 0;
	++CallCount;
	if (CallCount != 1 && (CallCount % 300) != 0)
	{
		return;
	}
	const FVector OriginDeltaCm = Snapshot.ViewOriginCm - RTViewOriginCm;
	const double OriginDeltaM = OriginDeltaCm.Size() * 0.01;
	const FVector3f CenterRT = CompositeFrame.bValid
		? (GoverningPlanet.CenterCamRelativeKm + FVector3f(
			(float)(OriginDeltaCm.X * HillaireLimits::KmPerCm),
			(float)(OriginDeltaCm.Y * HillaireLimits::KmPerCm),
			(float)(OriginDeltaCm.Z * HillaireLimits::KmPerCm)))
		: GoverningPlanet.CenterCamRelativeKm;
	UE_LOG(LogHillaireAtmosphere, Log,
		TEXT("HillaireCoords #%llu: gov=%d snapOrigin=(%.0f,%.0f,%.0f) rtOrigin=(%.0f,%.0f,%.0f) dOrigin=%.3fm ")
		TEXT("centerSnap=(%.4f,%.4f,%.4f)km centerRT=(%.4f,%.4f,%.4f)km hSnap=%.5f hRT=%.5f hBake=%.5f rtfb=%d rot=(%.4f,%.4f,%.4f,%.4f)"),
		CallCount, GoverningPlanet.PlanetId,
		Snapshot.ViewOriginCm.X, Snapshot.ViewOriginCm.Y, Snapshot.ViewOriginCm.Z,
		RTViewOriginCm.X, RTViewOriginCm.Y, RTViewOriginCm.Z, OriginDeltaM,
		GoverningPlanet.CenterCamRelativeKm.X, GoverningPlanet.CenterCamRelativeKm.Y, GoverningPlanet.CenterCamRelativeKm.Z,
		CenterRT.X, CenterRT.Y, CenterRT.Z,
		(double)GoverningPlanet.ViewHeightKm,
		CompositeFrame.bValid ? (double)CompositeFrame.ViewHeightKm : -1.0,
		(double)ClampedHeightKm,
		CompositeFrame.bValid ? 0 : 1,
		GoverningPlanet.Rotation.X, GoverningPlanet.Rotation.Y,
		GoverningPlanet.Rotation.Z, GoverningPlanet.Rotation.W);
}
