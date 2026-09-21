#pragma once

#include "CoreMinimal.h"
#include "HillaireLimits.h"

/**
 * HILLAIRE ATMOSPHERE - UNIT CONVERSIONS (Phase 1).
 *
 * The scattering core works EXCLUSIVELY in kilometers (float), exactly like the
 * golden DX11 reference ("All units in kilometers"). Unreal world space is
 * centimeters (double). Conversion happens ONCE at the GameThread -> snapshot
 * boundary; nothing inside the core ever sees centimeter values.
 *
 * Coordinate chain (documented once, enforced by types):
 *
 *   World Space (FVector, double, cm)        -- UE truth: actors, STARMAP, Sun
 *     -> Kilometers (double)                 -- divide by CmPerKm
 *     -> Camera-relative (FVector3f, float)  -- subtract per-view origin, in km
 *     -> Shader coordinates (float km)       -- uploaded as-is; the planet-local
 *                                               transform (conj(Q)*(P-C)) is then
 *                                               applied EXACTLY ONCE in-shader,
 *                                               mirroring the reference.
 *
 * Camera-relative rendering is load-bearing for Andromeda: planet radii of
 * ~6360 km at UE centimeter scale overflow float32 precision. The reference
 * sidesteps this because its demo world IS kilometers around the origin; we
 * recenter per view instead (spec section 8.2).
 */
namespace HillaireUnits
{
	FORCEINLINE double CmToKm(double Cm)
	{
		return Cm * HillaireLimits::KmPerCm;
	}

	FORCEINLINE double KmToCm(double Km)
	{
		return Km * HillaireLimits::CmPerKm;
	}

	/**
	 * World (double, cm) -> camera-relative (float, km).
	 * Subtract in double precision FIRST, then narrow: this preserves precision
	 * far from the world origin, where narrowing first would quantize to meters.
	 */
	FORCEINLINE FVector3f WorldCmToCameraRelativeKm(const FVector& WorldCm, const FVector& OriginCm)
	{
		const double DxKm = (WorldCm.X - OriginCm.X) * HillaireLimits::KmPerCm;
		const double DyKm = (WorldCm.Y - OriginCm.Y) * HillaireLimits::KmPerCm;
		const double DzKm = (WorldCm.Z - OriginCm.Z) * HillaireLimits::KmPerCm;
		return FVector3f((float)DxKm, (float)DyKm, (float)DzKm);
	}

	FORCEINLINE double WorldCmToKmDouble(double Cm)
	{
		return Cm * HillaireLimits::KmPerCm;
	}
}
