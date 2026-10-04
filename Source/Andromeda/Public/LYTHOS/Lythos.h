#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Planet/PlanetProfile.h"
#include "StarSystemGenerator.h"
#include "StarSystem.h"
#include "Lythos.generated.h"

// =========================================================
// LYTHOS - Phase 1: Deterministic planetary terrain foundation.
//
// Additive system. Does NOT replace or modify:
//   - APlanet
//   - AStarSystem
//   - existing orbital/reframe/gravity math
//   - existing planet generation code (UPlanetContinentalGenerator,
//     UPlanetLandformGenerator, UAndromedaNoiseLibrary, UPlanetBiomeGenerator)
//   - ATMOS / ZEPHYR / Hillaire
//
// Reuses authoritative values from FPlanetGenerationData /
// FPlanetRuntimeData / FPlanetProfile.  Same seed + same inputs
// always yields the same terrain.
// =========================================================

/**
 * Landform categories produced by the Phase 1 macro-terrain generator.
 * These are coarse geographic classifiers used for debug visualization
 * and for future LYTHOS validation (anti-repetition). They are NOT
 * biomes: biomes are computed later by the existing PBS pipeline.
 */
UENUM(BlueprintType)
enum class ELythosLandform : uint8
{
	Unknown		UMETA(DisplayName = "Unknown"),

	Ocean		UMETA(DisplayName = "Ocean"),
	Coast		UMETA(DisplayName = "Coast"),
	Lowland		UMETA(DisplayName = "Lowland"),
	Plains		UMETA(DisplayName = "Plains"),
	Hills		UMETA(DisplayName = "Hills"),
	Highlands	UMETA(DisplayName = "Highlands"),
	Mountain	UMETA(DisplayName = "Mountain"),
	Basin		UMETA(DisplayName = "Basin"),
};

/**
 * Generation schema version. Bumped whenever the macro-terrain
 * algorithm changes. Baked into feature identities so that future
 * persistence can detect when a region must be regenerated.
 */
static constexpr int32 LYTHOS_GENERATION_VERSION = 1;

/**
 * Deterministic identity for a generated planetary region.
 * Derived only from stable inputs (planet identity, region coords,
 * generation version). Future persistence will key on this.
 */
USTRUCT(BlueprintType)
struct FLythosRegionId
{
	GENERATED_BODY()

	UPROPERTY()
	int64 PlanetID = 0;

	UPROPERTY()
	int64 PlanetSeed = 0;

	UPROPERTY()
	int32 FaceIndex = 0;

	UPROPERTY()
	int32 RegionX = 0;

	UPROPERTY()
	int32 RegionY = 0;

	UPROPERTY()
	int32 RegionLOD = 0;

	/** Schema version baked into the identity. */
	UPROPERTY()
	int32 GenerationVersion = LYTHOS_GENERATION_VERSION;

	/** Stable 64-bit fingerprint. */
	uint64 GetFingerprint() const;
};

/**
 * Coordinate of a localized region on a planetary cube-face sphere.
 *
 * The planet surface is projected onto the six faces of a cube
 * (the same cube-map projection used by the existing mesh generator).
 * Each face is subdivided into a regular grid of regions.
 */
USTRUCT(BlueprintType)
struct FLythosRegionCoord
{
	GENERATED_BODY()

	/** Cube face index [0..5]. */
	UPROPERTY()
	int32 FaceIndex = 0;

	/** Region column within the face. */
	UPROPERTY()
	int32 RegionX = 0;

	/** Region row within the face. */
	UPROPERTY()
	int32 RegionY = 0;

	/** LOD level of this region (0 = coarsest, higher = finer). */
	UPROPERTY()
	int32 RegionLOD = 0;

	/** Grid resolution per region axis (vertices per side). */
	UPROPERTY()
	int32 GridResolution = 16;

	/** Derived full region id. */
	FLythosRegionId ToRegionId(int64 PlanetID, int64 PlanetSeed) const;
};

/**
 * Deterministic feature identity for a macro geographic feature
 * (e.g. a mountain range or basin). Carries the feature type so that
 * future anti-repetition validation can reason about regional variety.
 */
USTRUCT(BlueprintType)
struct FLythosFeatureIdentity
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 FeatureId = 0;

	UPROPERTY()
	ELythosLandform FeatureType = ELythosLandform::Unknown;

	UPROPERTY()
	FIntVector FeatureSector = FIntVector::ZeroValue;

	/** Planet-level stable id. */
	UPROPERTY()
	int64 PlanetSeed = 0;
};

/**
 * Forward-declarations for the existing validated generators that
 * LYTHOS wraps. These are UObjects and are NOT owned by LYTHOS;
 * LYTHOS holds raw C++ pointers only during generation.
 */
class UPlanetContinentalGenerator;
class UPlanetLandformGenerator;
class UPlanetProfileGenerator;

/**
 * LYTHOS runtime input contract.
 *
 * Aggregates the deterministic, stable, read-only values needed by
 * the macro-terrain generator. All authority for these values lives
 * in the existing AStarSystem / FPlanetGenerationData / APlanet systems;
 * this struct is a flat snapshot for stateless evaluation.
 */
USTRUCT(BlueprintType)
struct FLythosPlanetContext
{
	GENERATED_BODY()

	/** Stable planet identity (assigned by AStarSystem). */
	UPROPERTY()
	int64 PlanetID = 0;

	/** Authoritative deterministic planet seed (PlanetGenerationData.PlanetSeed). */
	UPROPERTY()
	int64 PlanetSeed = 0;

	/** Reference radius of the planet, in cm (Planet::PlanetRadius / RuntimeData.PlanetRadius). */
	UPROPERTY()
	float PlanetRadius = 1.0f;

	/** Maximum terrain height above the reference radius, in cm. */
	UPROPERTY()
	float TerrainHeight = 1.0f;

	/** World-space position of the planet center (RuntimeData.WorldPosition). */
	UPROPERTY()
	FVector WorldPosition = FVector::ZeroVector;

	/** World-space position of the star / Sun (RuntimeData.SunActor->GetActorLocation()). */
	UPROPERTY()
	FVector StarPosition = FVector::ZeroVector;

	/** Star effective temperature in Kelvin (for future climate coupling). */
	UPROPERTY()
	float StarTemperatureKelvin = 5778.0f;

	/** Orbital distance from the star, in cm. */
	UPROPERTY()
	float OrbitDistance = 0.0f;

	/** Authoritative planet profile (archetype + climate biases). */
	UPROPERTY()
	FPlanetProfile PlanetProfile;

	// =========================================================
	// Deterministic generation parameters.
	// Sourced from FPlanetGenerationData (set by UStarSystemGenerator).
	// =========================================================

	UPROPERTY()
	float ContinentalScale = 0.5f;

	UPROPERTY()
	float MountainScale = 3.0f;

	UPROPERTY()
	float DetailScale = 12.0f;

	UPROPERTY()
	float MountainStrength = 1.5f;

	UPROPERTY()
	float DetailStrength = 0.1f;

	/** Convenience: is this context usable for generation? */
	bool IsValid() const;

	/** Build directly from FPlanetGenerationData + runtime position. */
	static FLythosPlanetContext FromGenerationData(
		const FPlanetGenerationData& GenData,
		const FVector& InWorldPosition,
		const FVector& InStarPosition,
		float InStarTemperatureKelvin = 5778.0f);

	/** Build from FPlanetRuntimeData + star actor position. */
	static FLythosPlanetContext FromRuntimeData(
		const FPlanetRuntimeData& RuntimeData,
		const FVector& InStarPosition,
		float InStarTemperatureKelvin = 5778.0f);
};

/**
 * Terrain sample at a single planetary surface direction.
 *
 * Plain data-oriented struct — no UObject. Lightweight enough for
 * per-sample evaluation during regional generation.
 */
USTRUCT(BlueprintType)
struct FLythoTerrainSample
{
	GENERATED_BODY()

	/** Unit surface direction (radial "up"). */
	UPROPERTY()
	FVector Direction = FVector::ForwardVector;

	/** Elevation above the reference radius, in cm. Can be negative (ocean). */
	UPROPERTY()
	float Elevation = 0.0f;

	/** Elevation normalised to [-1, +1] over TerrainHeight. */
	UPROPERTY()
	float NormalizedElevation = 0.0f;

	/** Surface normal (pointing outward). */
	UPROPERTY()
	FVector Normal = FVector::ForwardVector;

	/** Slope [0 = flat, 1 = vertical wall]. */
	UPROPERTY()
	float Slope = 0.0f;

	/** Absolute surface position in planet-local space. */
	UPROPERTY()
	FVector LocalPosition = FVector::ForwardVector;

	/** Coarse landform classification for this point. */
	UPROPERTY()
	ELythosLandform Landform = ELythosLandform::Unknown;

	/** Deterministic feature identity of the dominant feature at this point. */
	UPROPERTY()
	FLythosFeatureIdentity FeatureId;

	/** Latitude [0 = equator, 1 = pole] (pre-Tilt). */
	UPROPERTY()
	float Latitude = 0.0f;

	// =========================================================
	// FUTURE EXTENSION HOOKS (Phase 2+ — zero-initialized here)
	// =========================================================

	/** Reserved for future hydrology metadata. */
	UPROPERTY()
	uint32 HydrologyReserved = 0;

	/** Reserved for future biome metadata identity. */
	UPROPERTY()
	uint32 BiomeReserved = 0;
};

/**
 * Region-level terrain metadata. Aggregated from all samples within
 * a region, carrying deterministic identity for streaming / persistence.
 */
USTRUCT(BlueprintType)
struct FLythosRegionData
{
	GENERATED_BODY()

	UPROPERTY()
	FLythosRegionId RegionId;

	/** All terrain samples for this region (flattened grid). */
	UPROPERTY()
	TArray<FLythoTerrainSample> Samples;

	/** Number of samples per side (GridResolution^2 total). */
	UPROPERTY()
	int32 GridSize = 0;

	/** Planet radius in cm at evaluation time. */
	UPROPERTY()
	float PlanetRadius = 0.0f;

	/** Terrain height in cm at evaluation time. */
	UPROPERTY()
	float TerrainHeight = 0.0f;

	/** Approximate global sea level (normalised elevation). 0 = dry. */
	UPROPERTY()
	float SeaLevel = 0.0f;

	/** Deterministic. */
	bool IsValid() const { return GridSize > 1 && PlanetRadius > 0.0f; }
};
