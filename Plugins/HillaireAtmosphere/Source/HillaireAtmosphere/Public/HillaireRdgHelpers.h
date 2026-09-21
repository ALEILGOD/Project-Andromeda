#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "HillaireLimits.h"

/**
 * HILLAIRE ATMOSPHERE - RDG INFRASTRUCTURE (Phase 1).
 *
 * Helpers for the future LUT/final passes (spec section 6). Phase 1 provides:
 * resource descriptors (sizes/formats), persistent-texture creation with
 * RDG import/export lifetime (QueueTextureExtraction into pooled targets that
 * survive frames - this is what makes LUT reuse possible under RDG), and the
 * pass-boundary entry point the ViewExtension calls per view.
 *
 * The actual LUT GENERATION passes (Transmittance raster, MultiScattering
 * compute, SkyView raster, Aerial volume, Final composite) land in the ray
 * marching milestone; they plug into FHillaireLutManager::BuildLutPasses
 * without changing this layer.
 */
namespace HillaireRdg
{
	HILLAIREATMOSPHERE_API FRDGTextureDesc MakeTransmittanceLutDesc();
	HILLAIREATMOSPHERE_API FRDGTextureDesc MakeMultiScatteringLutDesc();
	HILLAIREATMOSPHERE_API FRDGTextureDesc MakeSkyViewLutDesc();
	HILLAIREATMOSPHERE_API FRDGTextureDesc MakeAerialVolumeDesc();

	/**
	 * Create-or-reuse a persistent LUT texture: registers a transient RDG
	 * texture and extracts it into the pooled target that outlives the graph.
	 * Pool handle survives across frames; content is preserved (reuse path).
	 */
	HILLAIREATMOSPHERE_API FRDGTextureRef CreatePersistentLutTexture(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureDesc& Desc,
		const TCHAR* Name,
		TRefCountPtr<IPooledRenderTarget>& InOutPooled);
}
