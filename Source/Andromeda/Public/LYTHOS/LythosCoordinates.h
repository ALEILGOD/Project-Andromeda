#pragma once

#include "CoreMinimal.h"
#include "LYTHOS/Lythos.h"

/**
 * LYTHOS planetary coordinate utilities.
 *
 * Treats planets as spheres. No World-Z as "up", no flat maps.
 * A planetary surface point is defined by:
 *   Direction (unit vector from planet center)
 *   + Elevation (radial offset above reference radius)
 *   = LocalPosition = Direction * (Radius + Elevation)
 *
 * The surface is mapped onto the 6 faces of an enclosing cube
 * (consistent with the existing cube-face mesh generator). This
 * namespace provides deterministic conversions between cube-face
 * coordinates and unit directions, plus latitude/longitude.
 */
namespace LythosCoordinates
{
	/**
	 * Map a cube-face parametric coordinate (U,V in [0,1]) on face
	 * FaceIndex to a unit direction on the sphere.
	 * Same cubemap convention as the existing GetCubeFaceDirection.
	 */
	FVector FaceToDirection(int32 FaceIndex, float U, float V);

	/**
	 * Inverse of FaceToDirection for a given face (best-effort).
	 * Returns true if the direction projects onto the face.
	 */
	bool DirectionToFace(const FVector& Direction, int32 FaceIndex, float& OutU, float& OutV);

	/**
	 * Return the cube-face index and UV for the closest face to Direction.
	 */
	int32 DirectionToNearestFace(const FVector& Direction, float& OutU, float& OutV);

	/**
	 * Spherical latitude in degrees [-90, +90] (Z = +90).
	 */
	float LatitudeDegrees(const FVector& Direction);

	/**
	 * Spherical longitude in degrees [0, 360) (X-axis = 0, CCW).
	 */
	float LongitudeDegrees(const FVector& Direction);

	/**
	 * Normalised latitude [0 = equator, 1 = pole].
	 */
	float NormalisedLatitude(const FVector& Direction);

	/**
	 * Region coordinate from a face + normalized UV.
	 * Divides [0,1]^2 on a face into RegionGridPerSide^RegionLOD cells.
	 */
	FLythosRegionCoord DirectionToRegion(
		const FVector& Direction,
		int32 RegionGridPerSide,
		int32 RegionLOD);

	/**
	 * Center direction of a region (face + integer region coords).
	 * Uses Region.GridResolution for the grid size.
	 */
	FVector RegionCenterDirection(const FLythosRegionCoord& Region);

	/**
	 * All sample directions (unit vectors) for a region, laid out as a
	 * GridResolution x GridResolution regular grid. Output size is
	 * GridResolution^2.
	 */
	void SampleRegionDirections(
		const FLythosRegionCoord& Region,
		TArray<FVector>& OutDirections);

	/**
	 * Convert a planet-local position (Direction * (Radius + Elevation))
	 * to world space using the planet context.
	 */
	FVector LocalToWorld(const FLythosPlanetContext& Context, const FVector& LocalPosition);

	/**
	 * Convert a world-space position to a planet-local surface direction
	 * (unit vector from the planet center). The caller must ensure the
	 * point is outside the planet.
	 */
	FVector WorldToLocalDirection(const FLythosPlanetContext& Context, const FVector& WorldPosition);
}
