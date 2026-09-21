#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "HillaireLutManager.h"
#include "HillaireViewSnapshot.h"

class FSceneViewStateInterface;

/**
 * HILLAIRE ATMOSPHERE - SCENE VIEW EXTENSION (Phase 1 skeleton).
 *
 * v1 architecture: Scene ViewExtension + Global Shaders + RDG, over-opaque
 * final (spec section 14, frozen decision). This phase builds the structure:
 *
 *   GameThread: SetupView -> subsystem BuildSnapshotForView -> stash per view
 *   RenderThread: BeforeDOF composite -> EnsurePlanetLuts (same-graph
 *     transient T/MS/SkyView) -> aerial evaluation -> sky/aerial composites.
 *     PreRenderView stays a no-op: generating LUTs there and consuming the
 *     pooled targets in the later-built BeforeDOF graph showed null on every
 *     regen frame (QueueTextureExtraction fills pooled only at graph
 *     execute), i.e. the sky flicker/absence root cause.
 *
 * Explicitly NOT in this phase: the final composite, LUT generation math,
 * renderer engine changes (no fork, no legacy hooks, no singletons).
 *
 * GT -> RT view matching: the renderer COPY-CONSTRUCTS every GameThread
 * FSceneView into an RT-local FViewInfo (SceneRendering.cpp: the RT family
 * holds the copies, with Family repointed at the RT-local family). Raw
 * FSceneView* addresses therefore NEVER match across the boundary. Snapshots
 * are keyed by FSceneViewStateInterface* (FSceneView::State), which the copy
 * shares with its GT original and which the engine guarantees unique per
 * view in a family (occlusion-state check). Views with null State are never
 * stashed (passthrough, no regression vs. the old address-keyed behavior).
 */
class HILLAIREATMOSPHERE_API FHillaireViewExtension : public FSceneViewExtensionBase
{
public:
	FHillaireViewExtension(const FAutoRegister& AutoRegister, class UHillaireAtmosphereSubsystem* InSubsystem);
	virtual ~FHillaireViewExtension();

	// FSceneViewExtensionBase
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	/**
	 * Phase-2D BeforeDOF composite delegate: scene color + depth + pooled
	 * aerial volume -> composited scene color. Returns the input unchanged
	 * whenever gating fails (CVar off, no content, outside atmosphere, no
	 * volume): baseline rendering provably preserved.
	 */
	FScreenPassTexture AerialCompositePass(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	UHillaireAtmosphereSubsystem* GetSubsystem() const { return Subsystem.Get(); }

	/**
	 * GT -> RT snapshot key: the shared per-view occlusion/temporal state.
	 * Null State views are never stashed (passthrough). Pure and
	 * unit-testable; the shared-pointer identity is what survives the
	 * GT-view -> RT-FViewInfo copy, never the view address.
	 */
	static const FSceneViewStateInterface* MakeSnapshotKey(const FSceneView& InView);

private:
	bool ShouldHandleView(const FSceneViewFamily& InViewFamily) const;
	void PruneStaleSnapshots(uint64 CurrentFrame);
	TSharedPtr<const FHillaireViewSnapshot> FindSnapshot(const FSceneView& InView) const;

	/**
	 * Throttled sky-path diagnostics (RenderThread, r.Hillaire.SkyLog):
	 * governing planet, camera heights, sun, LUT availability, gate outcome
	 * and dispatch rect for the real view. First call + every 300 calls.
	 */
	static void LogSkyCompositeState(
		const FHillaireViewSnapshot& Snapshot,
		const FHillaireSnapshotPlanet& GoverningPlanet,
		bool bSkyCVar, bool bHaveSkyView, bool bHaveTransmittance,
		bool bShouldSky, const FIntRect& ViewRect);

	/**
	 * Governing-planet record (RenderThread, r.Hillaire.DebugPlanet):
	 * planet identity, snapshot center, radii, camera altitude, slot-0 sun,
	 * pooled LUT build counters and transient availability for the frame.
	 * First call + every 300 calls.
	 */
	static void LogCompositePlanet(
		int32 GoverningPlanetId,
		const FHillaireViewSnapshot& Snapshot,
		const FHillaireSnapshotPlanet& GoverningPlanet,
		bool bHaveLutT, bool bHaveLutMS, bool bHaveLutSky,
		FHillaireLutManager* LutManager);

	/**
	 * Regen-input record (RenderThread, r.Hillaire.DebugStability): every
	 * composite execution logs the exact SkyView generation inputs (bake
	 * height, sun elevation, profile hash, primary identity, MS factor) plus
	 * transient availability and the sky gate outcome. With a static camera
	 * and a static sun, a sky change without a logged input change points at
	 * the resource-lifetime/view path instead of the predicates.
	 */
	static void LogCompositeStability(
		const FHillaireViewSnapshot& Snapshot,
		const FHillaireSnapshotPlanet& GoverningPlanet,
		const FHillaireLutManager::FHillaireCompositeViewInputs& CompositeFrame,
		float ClampedHeightKm,
		bool bHaveLutT, bool bHaveLutMS, bool bHaveLutSky,
		bool bShouldSky);

	/**
	 * Coordinate-frame record (RenderThread, r.Hillaire.DebugCoordinates):
	 * GT snapshot origin vs RT view origin, snapshot vs RT-relative center,
	 * heights, and whether the snapshot fallback frame was used. First call
	 * + every 300 calls.
	 */
	static void LogCompositeCoordinates(
		const FHillaireViewSnapshot& Snapshot,
		const FHillaireSnapshotPlanet& GoverningPlanet,
		const FVector& RTViewOriginCm,
		const FHillaireLutManager::FHillaireCompositeViewInputs& CompositeFrame,
		float ClampedHeightKm);

	/**
	 * Throttled hook-entry diagnostics (RenderThread, r.Hillaire.SkyLog):
	 * proves the BeforeDOF hook executes per view and reports the RT view's
	 * shared state pointer (must equal the GT SetupView state for the same
	 * view), snapshot presence, planet count and governing id.
	 * First call + every 300 calls.
	 */
	static void LogSkyHookEntry(const FHillaireViewSnapshot* Snapshot, const FSceneView& InView);

	/**
	 * Throttled SetupView diagnostics (r.Hillaire.SkyLog): proves per-view
	 * snapshots are built on the GameThread and reports the gate outcome
	 * (CVar, atmosphere flag, scene match, registered planets) plus, when a
	 * snapshot was built, the GT view origin and governing planet/height.
	 * Runs on the calling thread; rate-limited to first call + every 300.
	 * A null Snapshot means the gate rejected the view (no stash write).
	 */
	void LogSkySetupViewState(const FSceneViewFamily& InViewFamily, const FSceneView& InView,
		const FHillaireViewSnapshot* Snapshot);

	struct FHillaireStashedSnapshot
	{
		TSharedPtr<const FHillaireViewSnapshot> Snapshot;
		uint64 Frame = 0;
	};

	TWeakObjectPtr<UHillaireAtmosphereSubsystem> Subsystem;

	mutable FCriticalSection StashLock;
	TMap<const FSceneViewStateInterface*, FHillaireStashedSnapshot> SnapshotStash;

	// TEMP-DIAG (flicker hunt): last governing planet seen by the composite.
	// Only written on the render thread by AerialCompositePass.
	int32 LastGoverningLogId = INDEX_NONE;
};
