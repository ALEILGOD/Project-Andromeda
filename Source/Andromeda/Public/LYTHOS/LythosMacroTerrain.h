#pragma once

#include "CoreMinimal.h"
#include "LYTHOS/Lythos.h"

/**
 * LYTHOS macro terrain generator.
 *
 * Stateless, deterministic, region-evaluable. Produces structured
 * planetary geography at multiple geographic scales:
 *
 *   Planet seed
 *     -> continental-scale variation       (ocean vs land)
 *     -> major elevation structure         (highlands vs lowlands)
 *     -> mountain regions / ranges          (orogenic belts)
 *     -> basins and valleys                (depressions)
 *     -> plains / lowlands                 (gently rolling)
 *     -> coastal regions                   (sea-level transition)
 *
 * The generator delegates the raw noise evaluation to the existing
 * validated UPlanetContinentalGenerator and UPlanetLandformGenerator
 * subsystems (it does NOT reimplement or replace them). It adds:
 *
 *   - A multi-scale elevation composition with structured geographic masks.
 *   - Deterministic feature identity for every macro-scale landform.
 *   - A region-evaluation API (GenerateRegion) that produces only the
 *     requested cube-face region, never the full planet.
 *   - Sea-level computation for future hydrology coupling.
 *   - Extension hooks for future biome / hydrology consumers.
 *
 * All public functions are pure: same (Context, Direction) = same output.
 */
class FLythosMacroTerrain
{
public:
	/**
	 * Evaluate the full macro-terrain sample at a single planetary
	 * surface direction. Pure function.
	 */
	static FLythoTerrainSample Evaluate(
		const FLythosPlanetContext& Context,
		const FVector& Direction);

	/**
	 * Evaluate terrain for a localized planetary region.
	 * Produces exactly Region.GridResolution^2 samples in row-major order
	 * (V outer, U inner). GridResolution is read from the RegionCoord.
	 *
	 * Pure: same (Context, Region) = same output every time.
	 */
	static FLythosRegionData GenerateRegion(
		const FLythosPlanetContext& Context,
		const FLythosRegionCoord& Region);

	/**
	 * Deterministic approximate global sea level (normalised elevation).
	 * Derived from the planet water-coverage profile. The ocean surface
	 * itself is planar (global radius), as required by the Phase 1 hydrology
	 * contract.
	 */
	static float ComputeSeaLevel(const FLythosPlanetContext& Context);

	/**
	 * Ocean surface radius in cm (planet radius + sea level * terrain height).
	 * This is the future global ocean level — planar, independent of terrain noise.
	 */
	static float ComputeOceanSurfaceRadius(const FLythosPlanetContext& Context);

	/**
	 * Elevation of the seabed at a direction (negative elevation, below sea).
	 * For future ocean system consumption.
	 */
	static float ComputeOceanFloorElevation(const FLythosPlanetContext& Context, const FVector& Direction);

	/**
	 * Slope from neighbouring samples, used by future hydrology and biome systems.
	 * Returns a normalised slope [0, 1].
	 */
	static float ComputeSlope(const FVector& Direction, const FVector& SurfaceNormal);

private:
	/**
	 * Deterministic seed derivation for macro-terrain sub-domains.
	 * Each scale gets its own derived seed so features do not alias.
	 */
	static int64 DeriveSubSeed(int64 PlanetSeed, uint32 Index);

	/**
	 * Multi-scale elevation composition.
	 * Returns normalised height in [-1, +1].
	 */
	static float ComposeElevation(
		const FLythosPlanetContext& Context,
		const FVector& Direction,
		float* OutContinentalMask = nullptr,
		float* OutLandformMask = nullptr,
		float* OutMountainMask = nullptr,
		float* OutHillMask = nullptr);

	/**
	 * Classify a point into a coarse landform category.
	 */
	static ELythosLandform ClassifyLandform(
		float NormalizedElevation,
		float ContinentalMask,
		float LandformMask,
		float MountainMask,
		float HillMask,
		float SeaLevel);

	/**
	 * Deterministic feature identity for the dominant macro feature
	 * at a direction. Used by future anti-repetition validation.
	 */
	static FLythosFeatureIdentity ComputeFeatureIdentity(
		const FLythosPlanetContext& Context,
		const FVector& Direction,
		ELythosLandform Landform,
		float ContinentalMask,
		float MountainMask);
};
