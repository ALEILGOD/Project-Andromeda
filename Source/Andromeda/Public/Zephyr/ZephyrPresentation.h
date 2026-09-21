#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "Zephyr/ZephyrRenderData.h"

class FSceneViewStateInterface;
class UZephyrManager;

/**
 * ZEPHYR PRESENTATION VIEW EXTENSION - bounded AfterDOF sky overlay.
 *
 * Architecture:
 *   GameThread (SetupView): Manager->UpdateFromAtmosphere(dt) + PublishFrameForView
 *   RenderThread (AfterDOF): consume the published frame -> add the per-planet
 *   cloud/haze presentation tint to SKY pixels only.
 *
 * AfterDOF (not BeforeDOF) guarantees the ZEPHYR overlay runs AFTER the
 * authoritative ATMOS sky/aerial composite (which subscribes to BeforeDOF):
 * the overlay never fights the physical sky underneath, and it never replaces
 * it. The shader passes opaque pixels and deep-space/night pixels through
 * byte-identical.
 */
class ANDROMEDA_API FZephyrPresentationViewExtension : public FSceneViewExtensionBase
{
public:
	FZephyrPresentationViewExtension(const FAutoRegister& AutoRegister, UZephyrManager* InManager);
	virtual ~FZephyrPresentationViewExtension();

	// FSceneViewExtensionBase
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	/** AfterDOF overlay: bounded ZEPHYR cloud/haze presentation on sky pixels. */
	FScreenPassTexture ZephyrPresentationPass(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	UZephyrManager* GetManager() const { return Manager.Get(); }

private:
	bool ShouldHandleView(const FSceneViewFamily& InViewFamily) const;
	void PruneStaleSnapshots(uint64 CurrentFrame);
	TSharedPtr<const FZephyrPresentationFrame> FindSnapshot(const FSceneView& InView) const;

	/** GT -> RT snapshot key: shared view state (survives GT->RT view copy). */
	static const FSceneViewStateInterface* MakeSnapshotKey(const FSceneView& InView);

	struct FStashedSnapshot
	{
		TSharedPtr<const FZephyrPresentationFrame> Frame;
		uint64 FrameBuilt = 0;
	};

	TWeakObjectPtr<UZephyrManager> Manager;

	mutable FCriticalSection StashLock;
	TMap<const FSceneViewStateInterface*, FStashedSnapshot> SnapshotStash;

	// Diagnostics / build guard
	uint64 LastUpdatedGFrame = MAX_uint64;
};
