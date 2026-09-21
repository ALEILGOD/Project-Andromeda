#pragma once

#include "CoreMinimal.h"
#include "HillaireLightSource.h"
#include "HillairePlanetState.h"
#include "HillaireViewSnapshot.generated.h"

/**
 * HILLAIRE ATMOSPHERE - PER-VIEW SNAPSHOT (Phase 1).
 *
 * Immutable logical snapshot of everything the RenderThread needs for ONE
 * view. Built on the GameThread by FHillaireViewSnapshotBuilder from world
 * state (planets, lights, camera); the renderer consumes it read-only and
 * NEVER touches UObjects.
 *
 * Pattern enforced:
 *   GameThread: resolve world state -> build immutable snapshot
 *   RenderThread: consume snapshot -> RDG
 *
 * All positions are camera-relative KM floats (see HillaireUnits.h).
 * Per-planet RESOLVED lights are baked at build time (GT), so the RT upload
 * path is a memcpy-equivalent with zero math and zero branching on light type.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireSnapshotLight
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid LightId;

	UPROPERTY()
	FName LightName;

	UPROPERTY()
	bool bEnabled = false;

	UPROPERTY()
	bool bDirectional = false;

	/** Camera-relative position, km. Directional lights: unused. */
	UPROPERTY()
	FVector3f PositionCamRelativeKm = FVector3f::ZeroVector;

	/** Normalized direction TOWARD the light (world frame). */
	UPROPERTY()
	FVector DirectionToLightWorld = FVector::ForwardVector;

	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	UPROPERTY()
	float Intensity = 1.0f;

	UPROPERTY()
	float AngularRadiusRad = 0.0f;

	UPROPERTY()
	bool bDrawDisk = false;
};

USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireSnapshotPlanet
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PlanetId = INDEX_NONE;

	UPROPERTY()
	FGuid PlanetGuid;

	UPROPERTY()
	FName PlanetName;

	/** Camera-relative center, km. The scattering core recenters from here. */
	UPROPERTY()
	FVector3f CenterCamRelativeKm = FVector3f::ZeroVector;

	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	UPROPERTY()
	FHillaireAtmosphereProfile Profile;

	UPROPERTY()
	float GroundRadiusKm = 0.0f;

	UPROPERTY()
	float AtmosphereRadiusKm = 0.0f;

	UPROPERTY()
	float TerrainHeightKm = 0.0f;

	UPROPERTY()
	float ViewHeightKm = 0.0f;

	UPROPERTY()
	float DistanceKm = 0.0f;

	UPROPERTY()
	bool bIsGoverning = false;

	/** GT-resolved lights for THIS planet (N x M already expanded). */
	FHillaireCompactedLights ResolvedLights;

	/** Valid for visible non-governing planets; governing draws fullscreen. */
	FHillairePlanetScreenRect ScreenRect;
};

USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireViewSnapshot
{
	GENERATED_BODY()

	/** Per-view origin, double, cm (all relative vectors derive from this). */
	UPROPERTY()
	FVector ViewOriginCm = FVector::ZeroVector;

	/**
	 * View matrix in the CAMERA-RELATIVE KM frame (absolute translation
	 * stripped, rotation preserved; built by the ViewExtension). It projects
	 * CenterCamRelativeKm-style inputs, NOT UE world coordinates.
	 */
	UPROPERTY()
	FMatrix ViewMatrix = FMatrix::Identity;

	/** Projection matrix (pass-through; perspective math is scale-invariant). */
	UPROPERTY()
	FMatrix ProjectionMatrix = FMatrix::Identity;

	UPROPERTY()
	FIntRect ViewRect;

	/** Relevant planets only (governing + visible), in draw order. */
	UPROPERTY()
	TArray<FHillaireSnapshotPlanet> Planets;

	/** Active + inactive registry lights (RT needs identity, not just active). */
	UPROPERTY()
	TArray<FHillaireSnapshotLight> Lights;

	UPROPERTY()
	int32 GoverningPlanetId = INDEX_NONE;

	UPROPERTY()
	int32 EffectiveLightCount = 0;

	/**
	 * Render knobs copied from the subsystem on the GameThread (RT-safe: the
	 * render thread must never read UObject properties). MultipleScattering
	 * Factor joins the MS LUT key (baked into MS output); bFastSkyEnabled
	 * joins the SkyView key only. Deliberately EXCLUDED from SnapshotHash:
	 * they are configuration, not world state (determinism tests unaffected).
	 */
	UPROPERTY()
	float MultipleScatteringFactor = 1.0f;

	UPROPERTY()
	bool bFastSkyEnabled = true;

	/** Determinism key: same world input -> same hash (tested). */
	UPROPERTY()
	uint64 SnapshotHash = 0;

	bool HasAtmosphereContent() const
	{
		return Planets.Num() > 0 && GoverningPlanetId != INDEX_NONE;
	}
};

/**
 * Pure builder: world state in, immutable snapshot out. No UObjects, no
 * subsystem access, fully unit-testable. Called on the GameThread.
 */
class HILLAIREATMOSPHERE_API FHillaireViewSnapshotBuilder
{
public:
	static FHillaireViewSnapshot Build(
		const TArray<FHillairePlanetState>& Planets,
		const TArray<FHillaireLightSource>& Lights,
		const FVector& ViewOriginCm,
		const FMatrix& ViewMatrix,
		const FMatrix& ProjectionMatrix,
		const FIntRect& ViewRect,
		const FVector& ViewDirectionWorld,
		int32 IncumbentPlanetArrayIndex = INDEX_NONE);

	static uint64 ComputeSnapshotHash(
		const TArray<FHillaireSnapshotPlanet>& Planets,
		const TArray<FHillaireSnapshotLight>& Lights,
		const FVector& ViewOriginCm,
		int32 GoverningPlanetId);
};
