#pragma once

// =============================================================================
// LYTHOS 2.0 - Volumetric mesher.
//
// Extracts a smooth, watertight iso-surface from the density field using
// marching tetrahedra (table-free, deterministic, no topology ambiguity).
//
// Crack prevention across LOD transitions: regions are meshed on a shared
// cube-sphere sample grid, so same-LOD neighbours weld exactly. For differing
// LODs a radial "skirt" curtain is generated along every region boundary,
// which fills the geometric gap without visible holes or severe overlap. The
// mesher interface is deliberately isolated so Transvoxel transition cells can
// replace the skirt strategy later without touching streaming or density.
// =============================================================================

#include "CoreMinimal.h"
#include "LYTHOS2/Lythos2Types.h"

namespace Lythos2
{
    namespace Mesher
    {
        /** Build the derived mesh for one region. Safe to call on any thread. */
        ANDROMEDA_API void BuildRegionMesh(
            const FLythos2PlanetContext& Context,
            const FLythos2RegionKey& Key,
            const FLythos2Settings& Settings,
            FLythos2MeshData& OutMesh);

        /** Lateral sample spacing (cm) used by the region grid. */
        ANDROMEDA_API double RegionSampleSpacingCm(
            const FLythos2RegionKey& Key,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings);
    }
}
