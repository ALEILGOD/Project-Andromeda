#pragma once

#include "CoreMinimal.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLightSource.h"
#include "HillairePlanetState.h"
#include "HillaireLimits.h"
#include "HillairePlanetAtmosphereState.generated.h"

/**
 * HILLAIRE ATMOSPHERE - PER-PLANET ATMOSPHERE STATE (Multiplanetary rebuild).
 *
 * Immutable POD describing ONE planetary atmosphere at a specific frame.
 * Built on the GameThread, consumed read-only on the RenderThread.
 * No UObject references, no transient pointers, no live actor access.
 *
 * All positions in world-space centimeters (double precision) unless noted.
 * Radii and atmosphere heights in kilometers.
 * The Profile owns BottomRadiusKm/TopRadiusKm authoritatively.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FPlanetAtmosphereState
{
	GENERATED_BODY()

	/** Stable planet identifier from STARMAP/APlanet (NOT a transient slot index). */
	UPROPERTY()
	FGuid PlanetId;

	/** Human-readable name for diagnostics. */
	UPROPERTY()
	FName PlanetName;

	/** World-space center of the planet (double precision, cm). */
	UPROPERTY()
	FVector CenterWS = FVector::ZeroVector;

	/** Planet rotation (world frame). Conjugate applied to enter planet-local space. */
	UPROPERTY()
	FQuat RotationWS = FQuat::Identity;

	/** Complete atmosphere profile (authoritative radii + scattering parameters). */
	UPROPERTY()
	FHillaireAtmosphereProfile Profile;

	/** Derived mirror: == Profile.BottomRadiusKm (ground radius in km). */
	UPROPERTY()
	float GroundRadiusKm = 0.0f;

	/** Derived mirror: == Profile.TopRadiusKm (atmosphere top radius in km). */
	UPROPERTY()
	float AtmosphereTopRadiusKm = 0.0f;

	/** Terrain height contribution from planet surface (km). */
	UPROPERTY()
	float TerrainHeightKm = 0.0f;

	/** Star distance for this planet. <0 = directional/infinite (Case A/B). */
	UPROPERTY()
	float StarDistanceKm = -1.0f;

	/** Associated star identifier (for multi-star systems). */
	UPROPERTY()
	FGuid StarId;

	/** Planet-local direction TO the primary star (normalized). */
	UPROPERTY()
	FVector3f StarDirectionLocal = FVector3f::ZeroVector;

	/** World-space direction TO the primary star (normalized). */
	UPROPERTY()
	FVector StarDirectionWorld = FVector::ForwardVector;

	/** Primary star irradiance at this planet (Color * Intensity * attenuation). */
	UPROPERTY()
	FVector3f StarIrradiance = FVector3f::ZeroVector;

	/** Generation version: increments on any state change (detect stale RT snapshots). */
	UPROPERTY()
	uint32 GenerationVersion = 0;

	/** Profile content hash (LUT cache key). */
	UPROPERTY()
	uint64 ProfileHash = 0;

	/** LUT generation version (increments when LUTs regenerate). */
	UPROPERTY()
	uint32 LutGenerationVersion = 0;

	/** Validity flag: false = this state is a placeholder, not ready for rendering. */
	UPROPERTY()
	bool bValid = false;

	/** Last frame this state was updated (GameThread frame counter). */
	UPROPERTY()
	uint64 LastUpdateFrame = 0;

	/** Camera-relative center (km), computed at snapshot time. */
	UPROPERTY()
	FVector3f CenterCamRelativeKm = FVector3f::ZeroVector;

	/** Camera height over planet center (km), computed at snapshot time. */
	UPROPERTY()
	float ViewHeightKm = 0.0f;

	/** Camera distance to planet center (km), computed at snapshot time. */
	UPROPERTY()
	float DistanceKm = 0.0f;

	/** Whether the camera is inside this atmosphere (ViewHeightKm < AtmosphereTopRadiusKm). */
	UPROPERTY()
	bool bCameraInside = false;

	/** Screen rect for non-governing planets (viewport-clipped rendering). */
	UPROPERTY()
	FHillairePlanetScreenRect ScreenRect;

	/** Resolved lights for this planet (N x M expansion, GT-built). */
	UPROPERTY()
	FHillaireCompactedLights ResolvedLights;

	/** Whether this planet is the governing one for the current view. */
	UPROPERTY()
	bool bIsGoverning = false;

	/**
	 * Subsystem registry slot index for LUT cache/pooled targets, recorded at
	 * snapshot time. The frame-array index (position in Frame.Planets) differs
	 * from the registry index whenever the visible set is non-empty, so the
	 * renderer must use THIS slot for EnsurePlanetLuts, never GoverningPlanetIndex.
	 */
	UPROPERTY()
	int32 LutSlotIndex = INDEX_NONE;

	/** Whether the governing planet contains the camera. */
	UPROPERTY()
	bool bGoverningContainsCamera = false;

	FPlanetAtmosphereState() = default;

	/** Create an invalid/placeholder state for a planet slot. */
	static FPlanetAtmosphereState MakeInvalid(const FGuid& InPlanetId, const FName& InPlanetName)
	{
		FPlanetAtmosphereState State;
		State.PlanetId = InPlanetId;
		State.PlanetName = InPlanetName;
		State.bValid = false;
		return State;
	}

	bool IsValid() const
	{
		return bValid && PlanetId.IsValid() && GroundRadiusKm > 0.0f && AtmosphereTopRadiusKm > GroundRadiusKm;
	}
};

/**
 * Complete frame snapshot: all planet states + lights + view info for ONE frame.
 * Immutable after construction. GT builds Frame N, RT consumes Frame N.
 * Double-buffered: GT writes to NextFrame, RT reads from CurrentFrame.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireAtmosphereFrameState
{
	GENERATED_BODY()

	/** Frame number this snapshot was built for. */
	UPROPERTY()
	uint64 FrameNumber = 0;

	/** GameThread frame counter at build time. */
	UPROPERTY()
	uint64 GTFrameCounter = 0;

	/** View origin in world-space cm (double precision). */
	UPROPERTY()
	FVector ViewOriginWS = FVector::ZeroVector;

	/** View matrix (camera-relative km frame, translation stripped). */
	UPROPERTY()
	FMatrix ViewMatrix = FMatrix::Identity;

	/** Projection matrix. */
	UPROPERTY()
	FMatrix ProjectionMatrix = FMatrix::Identity;

	/** View rect in pixels. */
	UPROPERTY()
	FIntRect ViewRect;

	/** World-space view direction. */
	UPROPERTY()
	FVector ViewDirectionWS = FVector::ForwardVector;

	/** All registered planet states (valid ones only). */
	UPROPERTY()
	TArray<FPlanetAtmosphereState> Planets;

	/** All active light sources. */
	UPROPERTY()
	TArray<FHillaireLightSource> Lights;

	/** Governing planet index in Planets array (INDEX_NONE if none). */
	UPROPERTY()
	int32 GoverningPlanetIndex = INDEX_NONE;

	/** Indices of visible non-governing planets (far-to-near draw order). */
	UPROPERTY()
	TArray<int32> VisiblePlanetIndices;

	/** Render knobs copied from subsystem. */
	UPROPERTY()
	float MultipleScatteringFactor = 1.0f;

	UPROPERTY()
	bool bFastSkyEnabled = true;

	/** Deterministic hash of all world-state inputs (for change detection). */
	UPROPERTY()
	uint64 WorldStateHash = 0;

	FHillaireAtmosphereFrameState() = default;

	bool HasAtmosphereContent() const
	{
		return Planets.Num() > 0 && GoverningPlanetIndex != INDEX_NONE;
	}

	const FPlanetAtmosphereState* GetGoverningPlanet() const
	{
		if (Planets.IsValidIndex(GoverningPlanetIndex))
		{
			return &Planets[GoverningPlanetIndex];
		}
		return nullptr;
	}
};

/**
 * View context for rendering: which planets are relevant for this view.
 * Replaces the single "governing planet" concept with a prioritized set.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FAtmosphereViewContext
{
	GENERATED_BODY()

	/** Primary planet: contains camera or is atmospherically dominant. */
	UPROPERTY()
	FGuid PrimaryPlanetId;

	/** Secondary planets: visible, may contribute to sky/background. */
	UPROPERTY()
	TArray<FGuid> SecondaryPlanetIds;

	/** Whether we are in deep space (no primary atmosphere). */
	UPROPERTY()
	bool bIsInSpace = true;

	/** Space background state (starfield, etc.) - future expansion. */
	UPROPERTY()
	bool bHasSpaceBackground = false;

	FAtmosphereViewContext() = default;

	static FAtmosphereViewContext MakeSpaceContext()
	{
		FAtmosphereViewContext Ctx;
		Ctx.bIsInSpace = true;
		return Ctx;
	}

	static FAtmosphereViewContext MakePlanetContext(const FGuid& PlanetId)
	{
		FAtmosphereViewContext Ctx;
		Ctx.PrimaryPlanetId = PlanetId;
		Ctx.bIsInSpace = false;
		return Ctx;
	}
};

/**
 * Selection input for planet visibility/govening resolution.
 */
struct FPlanetSelectionInput
{
	FGuid PlanetId;
	FVector3f CenterCamRelativeKm = FVector3f::ZeroVector;
	float TopRadiusKm = 0.0f;
};

/**
 * Selection result: governing + visible set.
 */
struct FPlanetSelectionResult
{
	int32 GoverningIndex = INDEX_NONE;
	bool bGoverningContainsCamera = false;
	TArray<int32> VisibleIndices;     // far-to-near
	TArray<float> VisibleDistancesKm; // parallel to VisibleIndices

	FPlanetSelectionResult() = default;
};

/**
 * Coordinate conversion helpers - SINGLE canonical implementation.
 * Convention (matching reference Hillaire PlanetWorldToLocal):
 *   PlanetLocal = conjugate(PlanetRotation) * (World - PlanetCenter)
 *   World       = PlanetRotation * PlanetLocal + PlanetCenter
 * Rotation NEVER translates the center. Orbit modifies CenterWS. Rotation modifies RotationWS.
 */
namespace HillairePlanetMath
{
	/** World (double cm) -> Planet-local (float km). Subtract in double first for far-field precision. */
	HILLAIREATMOSPHERE_API FVector3f WorldToPlanetLocalKm(
		const FVector& PlanetCenterWS,
		const FQuat& PlanetRotationWS,
		const FVector& WorldPosWS);

	/** Planet-local (float km) -> World (double cm). Exact inverse. */
	HILLAIREATMOSPHERE_API FVector PlanetLocalToWorldWS(
		const FVector& PlanetCenterWS,
		const FQuat& PlanetRotationWS,
		const FVector3f& LocalPosKm);

	/** World direction -> Planet-local direction (rotation only, translation-invariant). */
	HILLAIREATMOSPHERE_API FVector3f WorldDirectionToPlanetLocal(
		const FQuat& PlanetRotationWS,
		const FVector& WorldDir);

	/** Planet-local direction -> World direction (rotation only). */
	HILLAIREATMOSPHERE_API FVector PlanetLocalDirectionToWorld(
		const FQuat& PlanetRotationWS,
		const FVector3f& LocalDir);

	/** Camera planet-local position (unnormalized) from camera-relative center. */
	HILLAIREATMOSPHERE_API FVector3f CameraPlanetLocalKm(
		const FVector3f& CenterCamRelativeKm,
		const FQuat& PlanetRotationWS);

	/** Normalized planet-local camera up (degenerate -> +Z). */
	HILLAIREATMOSPHERE_API FVector3f CameraUpLocal(
		const FVector3f& CenterCamRelativeKm,
		const FQuat& PlanetRotationWS);

	/** Sun elevation cosine: dot(normalize(CameraUpLocal), normalize(StarDirectionLocal)). */
	HILLAIREATMOSPHERE_API float SunElevationCos(
		const FVector3f& StarDirectionLocal,
		const FVector3f& CameraUpLocal);

	/** Camera height over planet center (km). */
	HILLAIREATMOSPHERE_API float CameraHeightKm(
		const FVector3f& CenterCamRelativeKm,
		const FVector3f& CameraCamRelativeKm);

	/** Select governing + visible planets (reference policy + hysteresis). */
	HILLAIREATMOSPHERE_API FPlanetSelectionResult SelectPlanets(
		const TArray<FPlanetSelectionInput>& Planets,
		const FVector3f& CameraCamRelativeKm,
		const FVector& CameraViewDirWS,
		int32 IncumbentGoverningIndex = INDEX_NONE,
		float MinAngularRadiusRad = HillaireLimits::PlanetRectMinAngularRad);
}