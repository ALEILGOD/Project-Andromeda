#pragma once

#include "CoreMinimal.h"

struct IPooledRenderTarget;

/**
 * HILLAIRE ATMOSPHERE - LUT DIAGNOSTICS (Phase 2A, task section 21).
 *
 * Proves a LUT contains numerically valid data instead of merely existing:
 * min / max / average / NaN count / Inf count over baked texels. Used by the
 * automation tests on CPU bakes AND by the GPU readback path below, so both
 * sides answer the same question with the same code.
 */
struct FHillaireLutStats
{
	float MinR = FLT_MAX, MinG = FLT_MAX, MinB = FLT_MAX;
	float MaxR = -FLT_MAX, MaxG = -FLT_MAX, MaxB = -FLT_MAX;
	double SumR = 0.0, SumG = 0.0, SumB = 0.0;
	// Rec.709 luminance diagnostics (task section 24): catch explosions and
	// quantify brightness trends (zenith vs horizon, noon vs sunset).
	float MinLum = FLT_MAX;
	float MaxLum = -FLT_MAX;
	double SumLum = 0.0;

	int64 TexelCount = 0;
	int64 NaNCount = 0;
	int64 InfCount = 0;

	double AverageR() const { return TexelCount > 0 ? SumR / (double)TexelCount : 0.0; }
	double AverageG() const { return TexelCount > 0 ? SumG / (double)TexelCount : 0.0; }
	double AverageB() const { return TexelCount > 0 ? SumB / (double)TexelCount : 0.0; }
	double AverageLuminance() const { return TexelCount > 0 ? SumLum / (double)TexelCount : 0.0; }
	bool HasInvalid() const { return NaNCount > 0 || InfCount > 0; }
	bool IsEmpty() const { return TexelCount == 0; }
};

namespace HillaireLutDiagnostics
{
	/** Accumulate stats over a CPU-side LUT buffer (FLinearColor per texel). */
	HILLAIREATMOSPHERE_API FHillaireLutStats AnalyzeLut(const TArray<FLinearColor>& Data);

	/** One-line human-readable summary for logs / test output. */
	HILLAIREATMOSPHERE_API FString DescribeStats(const TCHAR* LutName, const FHillaireLutStats& Stats);

	/**
	 * Blocking GPU readback of a persistent pooled LUT target into CPU memory.
	 * DEBUG/VALIDATION path only (stalls the pipeline by design): issues a
	 * render-thread readback of a PF_FloatRGBA texture and waits on a fence.
	 * Must be called from the GAME thread; returns false outside a valid
	 * rendering context (e.g. commandlet without RHI).
	 */
	HILLAIREATMOSPHERE_API bool ReadbackPooledLut(
		TRefCountPtr<IPooledRenderTarget>& PooledTarget, TArray<FLinearColor>& OutData);

	/**
	 * Blocking GPU readback of a 3D pooled volume target (Phase 2C).
	 * Same DEBUG/VALIDATION contract as ReadbackPooledLut: render-thread
	 * Read3DSurfaceFloatData over all slices, fence-waited, half->float
	 * converted on the GameThread. OutDims receives (W, H, D).
	 */
	HILLAIREATMOSPHERE_API bool ReadbackPooledVolume(
		TRefCountPtr<IPooledRenderTarget>& PooledTarget,
		TArray<FLinearColor>& OutData, FIntVector& OutDims);

	/**
	 * Minimal visual-debug export (task section 20): tonemaps a float LUT
	 * buffer to an 8-bit PNG WITHOUT touching the data (presentation only,
	 * never grading): c = (1 - exp(-L * Exposure))^(1/2.2). Exposure is
	 * logged per image so previews stay comparable. Used by the
	 * Hillaire.BakeSkyViewValidation console command and the GPU dump path.
	 */
	HILLAIREATMOSPHERE_API bool SaveLutPreviewPng(
		const TArray<FLinearColor>& Data, int32 W, int32 H,
		float Exposure, const FString& FilePath);
}
