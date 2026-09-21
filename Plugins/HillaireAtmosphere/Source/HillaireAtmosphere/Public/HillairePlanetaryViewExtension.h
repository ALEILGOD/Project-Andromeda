#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "HillaireLutManager.h"
#include "HillairePlanetAtmosphereState.h"

class FSceneViewStateInterface;
class UHillairePlanetaryAtmosphereSubsystem;

/**
 * HILLAIRE PLANETARY VIEW EXTENSION (Multiplanetary rebuild).
 *
 * Architecture:
 *   GameThread (SetupViewFamily): BuildNextFrameSnapshot -> SwapFrameSnapshots
 *   GameThread (SetupView): Stash snapshot for this view (keyed by ViewState)
 *   RenderThread (BeforeDOF): Get snapshot -> EnsurePlanetLuts -> Composite
 *
 * Double-buffered frame snapshots eliminate GT/RT races:
 *   Frame N: GT builds -> RT consumes
 *   Frame N+1: GT builds -> RT consumes
 *   Never mixing data from two frames.
 */
class HILLAIREATMOSPHERE_API FHillairePlanetaryViewExtension : public FSceneViewExtensionBase
{
public:
	FHillairePlanetaryViewExtension(const FAutoRegister& AutoRegister, UHillairePlanetaryAtmosphereSubsystem* InSubsystem);
	virtual ~FHillairePlanetaryViewExtension();

	// FSceneViewExtensionBase
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	/** BeforeDOF composite: sky + aerial perspective for relevant planets. */
	FScreenPassTexture PlanetaryCompositePass(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	UHillairePlanetaryAtmosphereSubsystem* GetSubsystem() const { return Subsystem.Get(); }

	/** GT -> RT snapshot key: shared view state (survives GT->RT view copy). */
	static const FSceneViewStateInterface* MakeSnapshotKey(const FSceneView& InView);

private:
	bool ShouldHandleView(const FSceneViewFamily& InViewFamily) const;
	void PruneStaleSnapshots(uint64 CurrentFrame);
	TSharedPtr<const FHillaireAtmosphereFrameState> FindSnapshot(const FSceneView& InView) const;

	/** Compute composite view inputs for a planet (RT-exact frame re-anchoring). */
	FHillaireLutManager::FHillaireCompositeViewInputs ComputeCompositeViewInputs(
		const FPlanetAtmosphereState& Planet,
		const FHillaireAtmosphereFrameState& Snapshot,
		const FSceneView& View) const;

	/** Log throttled diagnostics. */
	void LogCompositeState(const FHillaireAtmosphereFrameState& Snapshot,
		const FPlanetAtmosphereState& Planet,
		bool bHaveLutT, bool bHaveLutMS, bool bHaveLutSky,
		const FIntRect& ViewRect) const;

	struct FStashedSnapshot
	{
		TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot;
		uint64 Frame = 0;
	};

	TWeakObjectPtr<UHillairePlanetaryAtmosphereSubsystem> Subsystem;

	mutable FCriticalSection StashLock;
	TMap<const FSceneViewStateInterface*, FStashedSnapshot> SnapshotStash;

	// Diagnostics
	mutable FGuid LastLoggedGoverningPlanet;

	// Per-instance, per-frame guard: GT frame (GFrameCounter) for which this
	// extension last built a snapshot. Replaces the old process-global static
	// bool which latched true forever (and across PIE sessions), preventing
	// any rebuild after the first SetupView. MAX_uint64 = "never built".
	uint64 LastSnapshotBuiltGFrame = MAX_uint64;
};