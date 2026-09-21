#pragma once

#include "CoreMinimal.h"
#include "Zephyr/ZephyrTypes.h"

/**
 * ZEPHYR - immutable per-frame presentation snapshot (GameThread -> RenderThread).
 *
 * ZEPHYR is a THIN presentation layer ABOVE ATMOS. ATMOS owns the physical
 * atmosphere (scattering, transmittance, multi-scattering, sky-view, limb,
 * aerial perspective) and renders it through its own BeforeDOF composite. The
 * ZEPHYR frame carries ONLY the aesthetic overlay inputs that ATMOS does not
 * own: the per-planet climate profile (cloud/haze) and the gating derived from
 * the authoritative ATMOS geometry.
 *
 * Contract:
 * - GT builds, RT consumes read-only (same double-buffer philosophy as ATMOS).
 * - The overlay runs AFTER the ATMOS composite (AfterDOF) and only ever touches
 *   SKY pixels (device depth == far plane). Opaque geometry passes through
 *   byte-identical: ZEPHYR can never paint the planet surface.
 * - No exposure/bloom ownership: the overlay is a bounded additive tint in the
 *   same pre-exposed linear HDR space ATMOS already writes to.
 */
namespace ZephyrLimits
{
	/** Max cloud layers serialized to the shader (style budget). */
	static constexpr int32 MaxCloudLayers = 4;
}

/** Shader-ready packing of one FZephyrCloudLayer (3x float4). */
struct FZephyrCloudLayerGpu
{
	/** BaseAltitudeKm, TopAltitudeKm, Coverage, Density */
	FVector4f AltitudeConfigure = FVector4f(2.0f, 4.0f, 0.0f, 0.0f);

	/** CloudColor.rgb, DetailScale */
	FVector4f ColorDetail = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);

	/** WindSpeed, WindDirectionRad, WaterContent, bEnabled */
	FVector4f WindWater = FVector4f(10.0f, 0.0f, 0.5f, 0.0f);
};

/**
 * Immutable ZEPHYR presentation frame.
 *
 * The governing planet is the same planet ATMOS selects (authoritative): the
 * overlay can only ever present the atmosphere the physics is already showing.
 */
struct FZephyrPresentationFrame
{
	/** Frame this snapshot was built for. */
	uint64 FrameNumber = 0;

	/** Governing planet for this frame (matches ATMOS governing selection). */
	FGuid GoverningPlanetId;

	/**
	 * Authoritative ZEPHYR transition factor [0,1] of the governing planet
	 * (CPU-only, ZephyrComputeTransitionFactor over the REAL atmospheric
	 * volume). 1 at/below the planetary reference radius, smooth 1->0 across
	 * the volume, 0 at/above the atmosphere top. Deep space => 0 => the
	 * overlay is a strict no-op. The reference is the SAME planetary radius
	 * ATMOS uses as its bottom: terrain never re-anchors the atmosphere.
	 */
	float ZephyrTransitionFactor = 0.0f;

	/** Canonical geometry (km), reused from the authoritative ATMOS snapshot. */
	float PlanetRadiusKm = 0.0f;           // planetary reference radius (sea level)
	float TerrainHeightKm = 0.0f;          // authored terrain bound (metadata)
	float PlanetAtmosphereRangeKm = 0.0f;  // planetary reference radius (ATMOS Bottom)
	float AtmosphereTopRadiusKm = 0.0f;

	/** World-space planet center (cm, double precision) at snapshot time. */
	FVector CenterWS = FVector::ZeroVector;

	/** Planet orientation (world frame) for local-frame re-anchor. */
	FQuat RotationWS = FQuat::Identity;

	/** Sun (planet-local frame). */
	FVector3f StarDirectionLocal = FVector3f::ForwardVector;
	FVector3f StarIrradiance = FVector3f(1.0f, 1.0f, 1.0f);

	/** CPU daylight factor from the real sun elevation (mirror of HLSL). */
	float DaylightFactor = 0.0f;

	/** Sky appearance (presentation tint of the ATMOS sky). */
	FVector3f ZenithColor = FVector3f(0.30f, 0.50f, 0.90f);
	FVector3f HorizonColor = FVector3f(0.70f, 0.80f, 0.95f);
	FVector3f SunGlowColor = FVector3f(1.0f, 0.90f, 0.70f);
	FVector3f MieColor = FVector3f(0.90f, 0.80f, 0.60f);
	float RayleighScale = 1.0f;
	float MieScale = 1.0f;

	/** Weather haze (derived from visibility). */
	float WeatherHazeFactor = 0.0f;
	FVector3f WeatherHazeColor = FVector3f(0.70f, 0.75f, 0.80f);

	/** Cloud layers packed (max ZephyrLimits::MaxCloudLayers). */
	TArray<FZephyrCloudLayerGpu> CloudLayers;

	/** View rect in buffer pixels. */
	FIntRect ViewRect;

	/** Seconds for cloud drift / time-based effects. */
	float TimeSeconds = 0.0f;

	bool HasContent() const
	{
		return GoverningPlanetId.IsValid()
			&& AtmosphereTopRadiusKm > PlanetAtmosphereRangeKm
			&& ViewRect.Width() > 0 && ViewRect.Height() > 0;
	}
};
