#pragma once

#include "CoreMinimal.h"
#include "HillaireLimits.h"
#include "HillaireLightSource.generated.h"

/**
 * HILLAIRE ATMOSPHERE - LIGHT SOURCE SNAPSHOT (Phase 1).
 *
 * GameThread-side POD describing ONE participating light. There is deliberately
 * NO CurrentSun / PrimarySun / MainLight anywhere: the subsystem collects an
 * ARRAY of these (N x M model) and every planet resolves every light.
 *
 * Conventions (mirror the reference AtmosphereLightSource):
 * - WorldPositionCm: UE world, double precision, centimeters.
 * - WorldDirectionToLight: normalized direction TOWARD the light (same
 *   convention as Andromeda's PlanetaryLightingComponent.CurrentStarDirection).
 * - Directional: position ignored; direction used, no attenuation.
 * - Point: direction resolved from the planet CENTER (Case B approximation),
 *   inverse-square attenuation in KM units; never draws a stellar disk.
 * - Color * Intensity is the transfer weight; the white-solar LUT convention
 *   means this product carries the runtime irradiance.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireLightSource
{
	GENERATED_BODY()

	/** Stable identifier for rendering / cache keys (survives reordering). */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	FGuid LightId;

	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	FName LightName;

	/** Component gate AND owner-light visibility gate (both must pass). */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	bool bEnabled = true;

	/** True: sun-like rig (directional). False: point light. */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	bool bDirectional = false;

	/** UE world position, double, cm. Ignored when directional. */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	FVector WorldPositionCm = FVector::ZeroVector;

	/** Normalized direction TOWARD the light. Used when directional. */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	FVector WorldDirectionToLight = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	FLinearColor Color = FLinearColor::White;

	/**
	 * Lab units. Directional: direct multiplier. Point: divided by d^2 in km.
	 * NOTE: raw UE light intensities are NOT physical here; the normalization
	 * between UE authoring units and atmosphere transfer units is validated in
	 * the rendering phase (sun-facing scenario). Phase 1 stores raw values.
	 */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	float Intensity = 1.0f;

	/** Stellar disk angular radius, radians. >0 draws a disk (governing only). */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	float AngularRadiusRad = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Hillaire|Light")
	bool bDrawDisk = false;

	bool IsActive() const { return bEnabled; }
	uint64 ComputeContentHash() const;
	bool operator==(const FHillaireLightSource& Other) const;
	bool operator!=(const FHillaireLightSource& Other) const { return !(*this == Other); }
};

/**
 * Per-planet RESOLVED light (reference: ResolvedLight). This is what the
 * shader consumes: planet-local direction TO the light plus the attenuated
 * color weight. Produced on the GameThread by HillaireResolveLightForPlanet;
 * the render thread never resolves, it only uploads.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireResolvedLight
{
	GENERATED_BODY()

	/** Planet-local (conj(Q) applied) normalized direction TO the light. */
	UPROPERTY()
	FVector3f LightDirLocal = FVector3f::ZeroVector;

	/** Color * Intensity [* 1/d^2 for points], rgb. Alpha unused downstream. */
	UPROPERTY()
	FVector3f ColorAttenuation = FVector3f::ZeroVector;

	UPROPERTY()
	float AngularRadiusRad = 0.0f;

	UPROPERTY()
	bool bDrawDisk = false;

	/** Identity of the source light (cache keys, telemetry). Zero when empty. */
	UPROPERTY()
	FGuid LightId;

	bool IsEmpty() const
	{
		return ColorAttenuation.IsNearlyZero()
			&& AngularRadiusRad == 0.0f
			&& !bDrawDisk;
	}

	bool operator==(const FHillaireResolvedLight& Other) const;
	bool operator!=(const FHillaireResolvedLight& Other) const { return !(*this == Other); }
};

/**
 * Compacted per-planet light list (reference: updateSkyAtmosphereConstant
 * resolve+compact+flag). Fixed-size array, zero-filled tail, uploaded as-is.
 * bSinglePrimary mirrors UseSinglePrimaryFastPath EXACTLY: valid only when the
 * FIRST registry light is enabled+directional and every other is disabled.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireCompactedLights
{
	GENERATED_BODY()

	UPROPERTY()
	FHillaireResolvedLight Lights[HILLAIRE_MAX_ATMOSPHERE_LIGHTS];

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	bool bSinglePrimary = false;

	FHillaireCompactedLights()
	{
		ZeroFill();
	}

	void ZeroFill()
	{
		for (int32 i = 0; i < HILLAIRE_MAX_ATMOSPHERE_LIGHTS; ++i)
		{
			Lights[i] = FHillaireResolvedLight();
		}
		Count = 0;
		bSinglePrimary = false;
	}
};

/**
 * Resolve ONE light for ONE planet (reference: ResolveLightForPlanet).
 * Positions are camera-relative KM floats; the math is translation-invariant
 * (differences only), so this is MORE precise than the absolute-km reference.
 */
HILLAIREATMOSPHERE_API FHillaireResolvedLight HillaireResolveLightForPlanet(
	const FHillaireLightSource& Light,
	const FVector3f& PlanetCenterCamRelativeKm,
	const FVector3f& LightPosCamRelativeKm,
	const FQuat& PlanetRotation);

/**
 * Compact ALL registry lights for one planet: skip disabled, resolve,
 * zero-fill tail, set Count and the single-primary flag.
 * Registry ORDER matters for bSinglePrimary (reference rule, see above).
 */
HILLAIREATMOSPHERE_API FHillaireCompactedLights HillaireCompactLightsForPlanet(
	const TArray<FHillaireLightSource>& Lights,
	const FVector& ViewOriginCm,
	const FVector3f& PlanetCenterCamRelativeKm,
	const FQuat& PlanetRotation);

/** Number of enabled lights (reference: EffectiveLightCount). */
HILLAIREATMOSPHERE_API int32 HillaireEffectiveLightCount(const TArray<FHillaireLightSource>& Lights);
