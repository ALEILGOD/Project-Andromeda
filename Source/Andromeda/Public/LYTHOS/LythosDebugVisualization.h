#pragma once

#include "CoreMinimal.h"
#include "LYTHOS/Lythos.h"
#include "LythosDebugVisualization.generated.h"

/**
 * LYTHOS debug visualization utilities.
 *
 * Produces a simple colored vertex mesh for a planetary region so that
 * macro terrain can be visually inspected without modifying existing
 * planet materials. Each landform type maps to a distinct debug color.
 */
USTRUCT(BlueprintType)
struct FLythosDebugMesh
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector> Vertices;

	UPROPERTY()
	TArray<int32> Triangles;

	UPROPERTY()
	TArray<FVector> Normals;

	UPROPERTY()
	TArray<FVector2D> UVs;

	UPROPERTY()
	TArray<FColor> VertexColors;
};

/**
 * Map an ELythosLandform to a debug vertex color for visual inspection.
 */
UENUM(BlueprintType)
enum class ELythosDebugColor : uint8
{
	Ocean     UMETA(DisplayName = "Ocean"),
	Coast     UMETA(DisplayName = "Coast"),
	Lowland   UMETA(DisplayName = "Lowland"),
	Plains    UMETA(DisplayName = "Plains"),
	Hills     UMETA(DisplayName = "Hills"),
	Highlands UMETA(DisplayName = "Highlands"),
	Mountain  UMETA(DisplayName = "Mountain"),
	Basin     UMETA(DisplayName = "Basin"),
	Unknown   UMETA(DisplayName = "Unknown")
};

namespace LythosDebugVisualization
{
	/**
	 * Convert a landform enum to a debug color.
	 */
	FColor LandformToDebugColor(ELythosLandform Landform);

	/**
	 * Build a debug mesh for a region: vertices colored by landform,
	 * with elevation applied for geometric inspection.
	 * Does NOT modify any existing materials or meshes.
	 */
	FLythosDebugMesh BuildRegionDebugMesh(
		const FLythosRegionData& Region,
		const FLythosPlanetContext& Context);

	/**
	 * Build a debug mesh for an arbitrary set of directions.
	 */
	FLythosDebugMesh BuildDirectionDebugMesh(
		const FLythosPlanetContext& Context,
		const TArray<FVector>& Directions);
}
