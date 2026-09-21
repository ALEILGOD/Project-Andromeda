#pragma once

#include "CoreMinimal.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"

/**
 * HILLAIRE ATMOSPHERE - CPU MIRROR OF THE LUT GENERATION MATH (Phase 2A).
 *
 * Pure C++ (no RHI, no UObject) twin of Shaders/HillaireLutCore.ush, used for:
 * - automation tests (generation validity, determinism, cache behaviour run
 *   without a GPU submit);
 * - reference comparison methodology (same profile/resolution/config as the
 *   DX11 sample, baked on CPU, compared statistically);
 * - numeric diagnostics input (AnalyzeLut in HillaireLutDiagnostics.h).
 *
 * Fidelity notes:
 * - Medium sampling, march structure, step counts, stratification, reduction
 *   and series closure mirror the .ush line-for-line (float32 throughout, so
 *   CPU results stay close to GPU results up to compiler reassociation).
 * - Transmittance-LUT sampling inside the MS bake uses a clamp-to-edge
 *   bilinear filter that APPROXIMATES hardware SampleLevel bilinear. It is
 *   not bit-exact vs. the GPU (documented); determinism guarantees apply
 *   CPU-vs-CPU (bit-identical) and the GPU tolerance is validated via
 *   readback stats, not bitwise compare.
 */
namespace HillaireLutCpu
{
	struct FMediumSample
	{
		FVector3f Scattering = FVector3f::ZeroVector;
		FVector3f Absorption = FVector3f::ZeroVector;
		FVector3f Extinction = FVector3f::ZeroVector;
		FVector3f ScatteringMie = FVector3f::ZeroVector;
		FVector3f ScatteringRay = FVector3f::ZeroVector;
	};

	struct FSingleScatteringResult
	{
		FVector3f L = FVector3f::ZeroVector;
		FVector3f OpticalDepth = FVector3f::ZeroVector;
		FVector3f Transmittance = FVector3f::ZeroVector;
		FVector3f MultiScatAs1 = FVector3f::ZeroVector;
	};

	HILLAIREATMOSPHERE_API FMediumSample SampleMedium(
		const FHillaireAtmosphereProfile& Profile, const FVector3f& WorldPosPlanetLocal);

	/** Nearest-positive ray/sphere root with max(0,.) semantics (planet at origin). Returns false on miss. */
	HILLAIREATMOSPHERE_API bool RaySphereNearest(
		const FVector3f& R0, const FVector3f& Rd, float Radius, float& OutT);

	/** Explicit-center variant (earth-shadow lift: center = offset * Up). */
	HILLAIREATMOSPHERE_API bool RaySphereNearestCenter(
		const FVector3f& R0, const FVector3f& Rd,
		const FVector3f& Center, float Radius, float& OutT);

	/** Uv -> (viewHeight, viewZenithCos), mirrors HillaireUvToLutTransmittanceParams. */
	HILLAIREATMOSPHERE_API void UvToTransmittanceParams(
		const FHillaireAtmosphereProfile& Profile, float U, float V,
		float& OutViewHeightKm, float& OutViewZenithCos);

	/**
	 * Forward transmittance mapping (mirrors HillaireLutTransmittanceParamsToUv
	 * in HillaireCommon.ush): (viewHeight, viewZenithCos) -> LUT uv.
	 * Pure, unit-testable.
	 */
	HILLAIREATMOSPHERE_API void TransmittanceParamsToUv(
		float BottomRadiusKm, float TopRadiusKm,
		float ViewHeightKm, float ViewZenithCos,
		float& OutU, float& OutV);

	/** Full 40-step transmittance texel (mirrors HillaireTransmittanceLut.usf MainCS). */
	HILLAIREATMOSPHERE_API FVector3f ComputeTransmittancePixel(
		const FHillaireAtmosphereProfile& Profile, int32 X, int32 Y, int32 W, int32 H);

	HILLAIREATMOSPHERE_API void BakeTransmittanceLut(
		const FHillaireAtmosphereProfile& Profile, int32 W, int32 H, TArray<FLinearColor>& OutLut);

	/** Clamp-to-edge bilinear sample of a baked CPU LUT (approximates HW SampleLevel). */
	HILLAIREATMOSPHERE_API FVector3f SampleLutBilinear(
		const TArray<FLinearColor>& Lut, int32 W, int32 H, float U, float V);

	/**
	 * Full integrator for one march (mirrors HillaireIntegrateScatteredLuminance).
	 * Trailing options default to the Phase-2A MS-bake configuration, so older
	 * call sites (MS bake, regression tests) stay bit-stable. SkyView callers
	 * pass variable-count + real phases + MS approximation (reference
	 * SkyViewLutPS configuration).
	 */
	HILLAIREATMOSPHERE_API FSingleScatteringResult IntegrateScatteredLuminance(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const FVector3f& WorldPos, const FVector3f& WorldDir, const FVector3f& SunDir,
		bool bGround, int32 SampleCount,
		bool bVariableSampleCount = false,
		float MinSamples = 4.0f, float MaxSamples = 14.0f,
		bool bMieRayPhase = false, float MiePhaseG = 0.8f,
		bool bUseMultiScatteringApprox = false,
		const TArray<FLinearColor>* MultiScatteringLut = nullptr,
		int32 MsRes = 32,
		float TMaxMax = 9000000.0f);

	HILLAIREATMOSPHERE_API float RayleighPhase(float CosTheta);
	HILLAIREATMOSPHERE_API float CornetteShanksMiePhase(float G, float CosTheta);

	/** Move ray start to the atmosphere top (mirrors HillaireMoveToTopAtmosphere). */
	HILLAIREATMOSPHERE_API bool MoveToTopAtmosphere(
		FVector3f& WorldPos, const FVector3f& WorldDir, float TopRadiusKm);

	/** Uv -> (viewZenithCos, lightViewCos), mirrors HillaireUvToSkyViewLutParams. */
	HILLAIREATMOSPHERE_API void UvToSkyViewParams(
		float BottomRadiusKm, float ViewHeightKm, float U, float V,
		float& OutViewZenithCos, float& OutLightViewCos);

	/** One SkyView texel at true resolution (mirrors HillaireSkyViewLut.usf MainCS). */
	HILLAIREATMOSPHERE_API FVector3f ComputeSkyViewTexel(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		float ViewHeightKm, const FVector3f& SunDirLocal, const FVector3f& CameraUpLocal,
		int32 X, int32 Y, int32 W, int32 H);

	/** Bake an arbitrary SkyView texel subset at true resolution. */
	HILLAIREATMOSPHERE_API void BakeSkyViewTexels(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		float ViewHeightKm, const FVector3f& SunDirLocal, const FVector3f& CameraUpLocal,
		int32 W, int32 H,
		const TArray<FIntPoint>& Coords, TArray<FLinearColor>& OutValues);

	/** Full 192x108 SkyView bake (validation/debug path; spots for tests). */
	HILLAIREATMOSPHERE_API void BakeFullSkyViewLut(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		float ViewHeightKm, const FVector3f& SunDirLocal, const FVector3f& CameraUpLocal,
		TArray<FLinearColor>& OutLut);

	/** Full 32x32 MS bake (validation/debug path). */
	HILLAIREATMOSPHERE_API void BakeFullMultiScatteringLut(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		int32 Res, float MultipleScatteringFactor, TArray<FLinearColor>& OutLut);

	/** One MS texel at true resolution (mirrors HillaireMultiScattering.usf MainCS incl. 64-dir reduction). */
	HILLAIREATMOSPHERE_API FVector3f ComputeMultiScatteringTexel(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		int32 X, int32 Y, int32 Res, float MultipleScatteringFactor);

	/** Bake an arbitrary texel subset at true resolution (tests stay fast; pass all coords for a full bake). */
	HILLAIREATMOSPHERE_API void BakeMultiScatteringTexels(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		int32 Res, float MultipleScatteringFactor,
		const TArray<FIntPoint>& Coords, TArray<FLinearColor>& OutValues);

	/**
	 * One aerial froxel (mirrors HillaireAerialPerspective.usf MainCS).
	 * View rays derive from clip coords through InvProjMatrix +
	 * ViewToPlanetLocalRot exactly like the shader (row-vector convention
	 * == FMatrix column-convention maps, so CPU and GPU agree up to float
	 * reassociation). Returns (L, 1 - mean(T)) like the shader output.
	 */
	HILLAIREATMOSPHERE_API FLinearColor ComputeAerialFroxel(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		const FVector3f& CameraPlanetLocalKm,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& SunDirLocal,
		int32 X, int32 Y, int32 Z, int32 W, int32 H, int32 D);

	/** Clamp-to-edge trilinear sample of a baked CPU volume (approximates HW SampleLevel). */
	HILLAIREATMOSPHERE_API FVector3f SampleVolumeTrilinear(
		const TArray<FLinearColor>& Volume, int32 W, int32 H, int32 D,
		float U, float V, float Wgt);

	/**
	 * One composite pixel (mirrors HillaireAerialComposite.usf MainCS).
	 * DeviceZ uses the reversed-Z convention (0 = sky/far -> identity).
	 * SunColor is slot-0 ColorAttenuation (linear); PreExposure matches the
	 * buffer convention (1.0 in tests, view value in production).
	 */
	HILLAIREATMOSPHERE_API FLinearColor CompositeAerialPixel(
		const FLinearColor& SceneColor,
		float DeviceZ,
		float ViewU, float ViewV,
		const FMatrix& InvProjMatrix,
		const TArray<FLinearColor>& Volume, int32 VW, int32 VH, int32 VD,
		const FVector3f& SunColor,
		float PreExposure,
		float AerialKmPerSlice);

	/**
	 * Forward SkyView LUT mapping (mirrors HillaireSkyViewLutParamsToUv in
	 * HillaireSampling.ush): planet-local view geometry -> LUT uv (sub-uv
	 * convention, directly sampleable). Pure, unit-testable.
	 */
	HILLAIREATMOSPHERE_API void SkyViewLutParamsToUv(
		float BottomRadiusKm,
		bool bIntersectGround,
		float ViewZenithCos,
		float LightViewCos,
		float ViewHeightKm,
		float& OutU, float& OutV);

	/**
	 * One sky-background pixel (mirrors HillaireSkyBackground.usf MainCS,
	 * reference FASTSKY branch: sky + T * background).
	 * DeviceZ uses the reversed-Z convention (0 = sky/far -> SkyView sample;
	 * anything above CompositeSkyDepthEpsilon is opaque -> identity, aerial
	 * owns it). SunColor is slot-0 ColorAttenuation (linear); PreExposure
	 * matches the buffer convention (1.0 in tests, view value in production).
	 */
	HILLAIREATMOSPHERE_API FLinearColor SampleSkyBackgroundPixel(
		const FLinearColor& SceneColor,
		float DeviceZ,
		float ViewU, float ViewV,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& CameraPlanetLocalKm,
		const FVector3f& SunDirLocal,
		const FVector3f& SunColor,
		float BottomRadiusKm,
		float TopRadiusKm,
		float ViewHeightKm,
		const TArray<FLinearColor>& SkyViewLut, int32 SVW, int32 SVH,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		float PreExposure);

	/** Bake an arbitrary froxel subset at true resolution (tests stay fast). */
	HILLAIREATMOSPHERE_API void BakeAerialFroxels(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		const FVector3f& CameraPlanetLocalKm,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& SunDirLocal,
		int32 W, int32 H, int32 D,
		const TArray<FIntVector>& Coords, TArray<FLinearColor>& OutValues);

	/** Full 32x32x32 aerial bake (validation/debug path). */
	HILLAIREATMOSPHERE_API void BakeFullAerialVolume(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		const FVector3f& CameraPlanetLocalKm,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& SunDirLocal,
		TArray<FLinearColor>& OutVolume);
}
