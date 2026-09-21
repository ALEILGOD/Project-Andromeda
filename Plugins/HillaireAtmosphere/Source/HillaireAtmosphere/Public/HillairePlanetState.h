#pragma once

#include "CoreMinimal.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"
#include "HillairePlanetState.generated.h"

/**
 * HILLAIRE ATMOSPHERE - PER-PLANET LUT STATE (Phase 1).
 *
 * Plain data (no UObject): content hashes, the SkyView sun cache, validity
 * flags, pooled-target handles and telemetry counters for ONE atmosphere.
 * Owned by FHillaireLutManager, keyed by PlanetId. There is deliberately NO
 * global atmosphere state and NO CurrentAtmosphere singleton.
 */
struct FHillairePlanetLutState
{
	uint64 TransmittanceHash = 0;
	uint64 MultiScatteringHash = 0;
	uint64 SkyViewHash = 0;
	uint64 LastProfileHash = 0;

	/** Planet-local primary-sun ELEVATION cosine at SkyView bake time (sun cache). */
	float SkyViewCachedSunElevationCos = -2.0f;
	float SkyViewCachedHeightKm = -1.0f;
	bool bSkyViewSunInit = false;

	/**
	 * Full normalized planet-local sun direction at SkyView bake time.
	 * Diagnostic/audit only: the bake content is elevation-determined, so this
	 * NEVER participates in the regen decision (see QueryRegen). It lets logs
	 * prove whether the sun's AZIMUTH drifted while the elevation key held.
	 */
	FVector3f SkyViewCachedSunDirLocal = FVector3f::ZeroVector;
	bool bSkyViewSunDirInit = false;

	bool bTransmittanceValid = false;
	bool bMultiScatteringValid = false;
	bool bSkyViewValid = false;

	/** Pooled RDG target handles (created on first ensure, never per-frame). */
	FGuid LutHandleTransmittance;
	FGuid LutHandleMultiScattering;
	FGuid LutHandleSkyView;

	/** Primary slot-0 light identity at SkyView bake time (swap detection). */
	FGuid LastPrimaryLightId;
	bool bLastPrimaryDirectional = false;

	uint32 LutBuildCount = 0;
	uint32 LutReuseCount = 0;
	float LastLutBuildMs = 0.0f;

	void Reset()
	{
		*this = FHillairePlanetLutState();
	}

	void InvalidateAll()
	{
		bTransmittanceValid = false;
		bMultiScatteringValid = false;
		bSkyViewValid = false;
	}
};

/**
 * HILLAIRE ATMOSPHERE - PER-PLANET STATE (Phase 1).
 *
 * Autonomous description of ONE planetary atmosphere. No dependency on any
 * "Sun" concept: lights arrive separately and are resolved per planet.
 *
 * Radii policy: Profile.BottomRadiusKm/TopRadiusKm are AUTHORITATIVE.
 * GroundRadiusKm/AtmosphereRadiusKm/TerrainHeightKm are derived mirrors kept
 * for cheap selection/rect math without re-reading the profile. They are
 * written by exactly one path (component registration) and verified equal.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillairePlanetState
{
	GENERATED_BODY()

	/** Subsystem slot index (identity for LUT ownership). */
	UPROPERTY()
	int32 PlanetId = INDEX_NONE;

	UPROPERTY()
	FGuid PlanetGuid;

	UPROPERTY()
	FName PlanetName;

	/** UE world center, double, cm (GameThread truth; converted at snapshot). */
	UPROPERTY()
	FVector CenterCmWorld = FVector::ZeroVector;

	/** UE world rotation (planet frame Q). */
	UPROPERTY()
	FQuat RotationWorld = FQuat::Identity;

	UPROPERTY(EditAnywhere, Category = "Hillaire|Planet")
	FHillaireAtmosphereProfile Profile;

	/** Derived: == Profile.BottomRadiusKm. */
	UPROPERTY()
	float GroundRadiusKm = 0.0f;

	/** Derived: == Profile.TopRadiusKm. */
	UPROPERTY()
	float AtmosphereRadiusKm = 0.0f;

	/** Terrain/height contribution from the planet (e.g. APlanet.TerrainHeight). */
	UPROPERTY()
	float TerrainHeightKm = 0.0f;

	/**
	 * Star distance, km. <0 = directional/infinite (Case A/B, current scope).
	 * Finite = positional Case C/D (future insertion point, NOT v1).
	 */
	UPROPERTY()
	float StarDistanceKm = -1.0f;

	/** LUT cache state (persisted by the manager across snapshot rebuilds). */
	FHillairePlanetLutState Lut;

	bool IsValid(FString* OutError = nullptr) const;
	float GetTopRadiusKm() const { return Profile.TopRadiusKm; }
	float GetGroundRadiusKm() const { return Profile.BottomRadiusKm; }
};

/**
 * Build a planet state from explicit external inputs (Phase 2F STARMAP seam).
 *
 * Pure helper with no UObject access: game-module adapters (which own the
 * procedural planet actors) read live values there and fold them here through
 * HillaireBuildNormalizedProfile (the centralized FASE-2 builder: profile
 * authoritative, envelope-normalized density, derived mirrors equal by
 * construction). GroundRadiusKm is the STABLE planetary reference radius
 * (atmosphere Bottom, sea level; terrain is never folded in) and
 * AtmosphereHeightKm is the persistent volume envelope (>= the self-similar
 * minimum; terrain containment may raise it). TerrainHeightKm is metadata.
 * Units are km throughout; callers convert from cm
 * (HillaireLimits::KmPerCm).
 */
HILLAIREATMOSPHERE_API FHillairePlanetState HillaireMakeExternalPlanetState(
	int32 PlanetId,
	const FGuid& PlanetGuid,
	const FName& PlanetName,
	const FVector& CenterCmWorld,
	const FQuat& RotationWorld,
	float GroundRadiusKm,
	float AtmosphereHeightKm,
	float TerrainHeightKm,
	const FHillaireAtmosphereProfile& BaseProfile);

/** Selection input: camera-relative flat data, no UObjects. */
struct FHillaireSelectionInput
{
	FVector3f CenterCamRelativeKm = FVector3f::ZeroVector;
	float TopRadiusKm = 0.0f;
};

/** Selection result: governing + visible set, far-to-near (reference: PlanetSelection). */
struct FHillairePlanetSelection
{
	int32 GoverningIndex = INDEX_NONE;
	bool bGoverningContainsCamera = false;
	int32 VisibleIndices[HILLAIRE_MAX_PLANETS];
	float VisibleDistancesKm[HILLAIRE_MAX_PLANETS];
	int32 VisibleCount = 0;

	FHillairePlanetSelection()
	{
		for (int32 i = 0; i < HILLAIRE_MAX_PLANETS; ++i)
		{
			VisibleIndices[i] = INDEX_NONE;
			VisibleDistancesKm[i] = 0.0f;
		}
	}
};

/**
 * Planet selection (reference: SelectPlanets, verbatim policy).
 * Governing: first atmosphere CONTAINING the camera wins; otherwise the
 * nearest surface. Visible: front-facing, above the angular threshold,
 * far-to-near; the governing planet is NEVER in the rect set (it draws
 * fullscreen, avoiding double-draw/double-blend).
 */
HILLAIREATMOSPHERE_API FHillairePlanetSelection HillaireSelectPlanets(
	const TArray<FHillaireSelectionInput>& Planets,
	const FVector3f& CameraCamRelativeKm,
	const FVector& CameraViewDirWorld,
	float MinAngularRadiusRad = HillaireLimits::PlanetRectMinAngularRad);

/**
 * Governing selection with incumbent hysteresis (ATMOS FIX VISIVO DEFINITIVO,
 * task G). When IncumbentIndex is a valid planet-array index, the incumbent
 * keeps the governing slot while it still contains the camera, or while no
 * challenger beats its signed surface distance by more than
 * max(HysteresisFloorKm, HysteresisRelative * IncumbentTop). Otherwise the
 * reference policy (HillaireSelectPlanets) decides. With an invalid
 * incumbent the result is bit-identical to HillaireSelectPlanets.
 *
 * Signed surface distance = center distance - TopRadiusKm (negative inside).
 * The visible set is rebuilt consistently: the kept incumbent never takes a
 * rect, a displaced former governing re-enters the rect set only when it
 * passes the same front-facing/angular tests as any visible planet.
 */
HILLAIREATMOSPHERE_API FHillairePlanetSelection HillaireSelectPlanetsWithIncumbent(
	const TArray<FHillaireSelectionInput>& Planets,
	const FVector3f& CameraCamRelativeKm,
	const FVector& CameraViewDirWorld,
	int32 IncumbentIndex,
	float MinAngularRadiusRad = HillaireLimits::PlanetRectMinAngularRad);

/** Camera height over the planet center, km (reference: PlanetCameraHeightKm). */
HILLAIREATMOSPHERE_API float HillaireCameraHeightKm(
	const FVector3f& CenterCamRelativeKm,
	const FVector3f& CameraCamRelativeKm);

/** Planet-frame rotation helper (reference: PlanetRotateVec / HLSL QuatRotate). */
HILLAIREATMOSPHERE_API FVector3f HillaireRotateVec(const FQuat& Q, const FVector3f& V);

/**
 * UNIFIED PLANETARY FRAME HELPERS (ATMOS FIX VISIVO DEFINITIVO, task A/H).
 *
 * Single conversion path, used by the snapshot builder, the LUT manager
 * (bake + composite view inputs) and the automated centering probe instead
 * of duplicated inline formulas. Convention everywhere (reference
 * PlanetWorldToLocal / PlanetLocalToWorld):
 *
 *   planet-local = conjugate(PlanetRotation) * (world - PlanetCenter)
 *   world        = PlanetRotation * planet-local + PlanetCenter
 *
 * The rotation NEVER translates the center: rotating the planet only spins
 * the local axes around the same PlanetCenterWS. All helpers assert this by
 * construction (center maps to origin exactly).
 */

/** World (double, cm) -> planet-local (float, km). Subtract in double first. */
HILLAIREATMOSPHERE_API FVector3f HillaireWorldToPlanetLocalKm(
	const FVector& CenterCmWorld,
	const FQuat& PlanetRotation,
	const FVector& WorldPosCm);

/** Planet-local -> world (double, cm). Exact inverse of the above. */
HILLAIREATMOSPHERE_API FVector HillairePlanetLocalToWorldCm(
	const FVector& CenterCmWorld,
	const FQuat& PlanetRotation,
	const FVector3f& LocalPosKm);

/**
 * Planet-local camera position, km, from a camera-relative center
 * (snapshot frame: camera at the relative origin).
 * camLocal = conjugate(Q) * (0 - CenterCamRelativeKm). Unnormalized: the
 * SkyView bake normalizes its own copy, the aerial march needs the true
 * position. ONE implementation (no per-callsite duplicates).
 */
HILLAIREATMOSPHERE_API FVector3f HillaireCameraPlanetLocalKm(
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotation);

/** Normalized planet-local camera up (degenerate center -> +Z). */
HILLAIREATMOSPHERE_API FVector3f HillaireCameraUpLocal(
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotation);

/**
 * Sun elevation cosine the SkyView bake consumes:
 * dot(normalize(CameraUpLocal), normalize(PrimarySunLocalDir)).
 * This scalar (not the raw sun vector) is the exact SkyView generation key:
 * the bake rebuilds SunDir from this cosine alone.
 */
HILLAIREATMOSPHERE_API float HillaireSunElevationCos(
	const FVector3f& PrimarySunLocalDir,
	const FVector3f& CameraUpLocal);

/**
 * Stable planet FGuid from the procedural identity (APlanet::PlanetID + PlanetSeed).
 * FNV-1a 64 over both int64 (shift-extracted bytes, endian-independent), folded
 * into an FGuid. THE single construction site: PlanetLink, StarLink and tests
 * must all use this (a duplicate formula silently splits one planet into two
 * LUT/sun identities).
 */
HILLAIREATMOSPHERE_API FGuid HillaireMakeStablePlanetId(int64 PlanetID, int64 PlanetSeed);

/** Screen rect of the Top-sphere (reference: PlanetScreenRect, incl. focal fix + pad). */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillairePlanetScreenRect
{
	GENERATED_BODY()

	UPROPERTY()
	bool bValid = false;

	UPROPERTY()
	FIntRect Rect = FIntRect(0, 0, 0, 0);

	UPROPERTY()
	float AngularRadiusRad = 0.0f;

	UPROPERTY()
	float DistanceKm = 0.0f;
};

HILLAIREATMOSPHERE_API FHillairePlanetScreenRect HillaireComputePlanetScreenRect(
	const FVector3f& CenterCamRelativeKm,
	float TopRadiusKm,
	const FMatrix& ViewProjectionMatrix,
	int32 ViewWidth,
	int32 ViewHeight);
