#include "HillaireRdgHelpers.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphDefinitions.h"

namespace HillaireRdg
{
	static FRDGTextureDesc MakeLutDesc2D(int32 W, int32 H)
	{
		// 32F RGBA until a 16F banding A/B passes (reference: "32f is required
		// if you do not want extra visual artefacts"). RenderTargetable for the
		// future raster LUT passes, UAV for the compute MS pass, SRV for sampling.
		return FRDGTextureDesc::Create2D(
			FIntPoint(W, H),
			PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
	}

	FRDGTextureDesc MakeTransmittanceLutDesc()
	{
		return MakeLutDesc2D(HillaireLimits::TransmittanceWidth, HillaireLimits::TransmittanceHeight);
	}

	FRDGTextureDesc MakeMultiScatteringLutDesc()
	{
		return MakeLutDesc2D(HillaireLimits::MultiScatteringSize, HillaireLimits::MultiScatteringSize);
	}

	FRDGTextureDesc MakeSkyViewLutDesc()
	{
		return MakeLutDesc2D(HillaireLimits::SkyViewWidth, HillaireLimits::SkyViewHeight);
	}

	FRDGTextureDesc MakeAerialVolumeDesc()
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(
			FIntVector(HillaireLimits::AerialVolumeSize, HillaireLimits::AerialVolumeSize, HillaireLimits::AerialVolumeSize),
			PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
		return Desc;
	}

	FRDGTextureRef CreatePersistentLutTexture(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureDesc& Desc,
		const TCHAR* Name,
		TRefCountPtr<IPooledRenderTarget>& InOutPooled)
	{
		FRDGTextureRef LutTexture = GraphBuilder.CreateTexture(Desc, Name);
		// Extraction moves the texture into the persistent pool on graph
		// execution; the handle stays valid next frame (reuse path). RDG
		// orders writers before readers automatically via SRV/UAV declarations
		// in the future generation passes.
		GraphBuilder.QueueTextureExtraction(LutTexture, &InOutPooled);
		return LutTexture;
	}
}
