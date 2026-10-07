#pragma once

// =============================================================================
// LYTHOS 2.0 - LOD policy and region selection.
//
// Resolution is LOD driven: the closer the viewer to a region, the finer the
// LOD, capped by the configurable MaxTerrainLOD. Distant regions stay coarse
// (LOD 0) and are never materialised at high resolution. Region selection is a
// deterministic, bounded quadtree walk over the cube-sphere.
// =============================================================================

#include "CoreMinimal.h"
#include "LYTHOS2/Lythos2Types.h"

namespace Lythos2
{
    namespace Lod
    {
        /** Approximate lateral size of a region (cm). */
        ANDROMEDA_API double RegionSizeCm(const FLythos2RegionKey& Key, const FLythos2PlanetContext& Context);

        /** View distance from a viewer to a region, discounting the region's own extent. */
        ANDROMEDA_API double DistanceToRegionCm(
            const FVector& ViewerLocalCm,
            const FLythos2RegionKey& Key,
            const FLythos2PlanetContext& Context);

        /** Ideal LOD for a region at a given distance, clamped to [0, MaxTerrainLOD]. */
        ANDROMEDA_API int32 IdealLodForDistanceCm(
            double DistanceCm,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings);

        /** Apply LOD hysteresis so small viewer movements do not thrash LODs. */
        ANDROMEDA_API int32 ApplyHysteresis(
            int32 CurrentLod,
            int32 DesiredLod,
            double DistanceCm,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings);

        /**
         * Select the set of leaf regions that partition the planet for a viewer.
         * Deterministic and bounded by Settings.MaxActiveRegions. Coverage is
         * always preserved: a region is only replaced by its four children.
         *
         * PreviouslyRefinedNodes, when supplied, contains every node that had
         * children in the previous partition. It applies LOD hysteresis so a
         * stationary viewer near a boundary cannot thrash N <-> N+1.
         */
        ANDROMEDA_API void SelectRegions(
            int64 PlanetID,
            const FVector& ViewerLocalCm,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings,
            TArray<FLythos2RegionKey>& OutLeaves,
            const TSet<FLythos2RegionKey>* PreviouslyRefinedNodes = nullptr);

        /** Convenience: distance used for priority ordering (smaller = sooner). */
        ANDROMEDA_API double RegionPriorityCm(
            const FVector& ViewerLocalCm,
            const FLythos2RegionKey& Key,
            const FLythos2PlanetContext& Context);
    }
}
