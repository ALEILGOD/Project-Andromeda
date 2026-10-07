#pragma once

// =============================================================================
// LYTHOS 2.0 - Planetary cube-sphere addressing.
//
// Terrain is addressed by a robust cube-sphere partition (no latitude /
// longitude singularities). The same mapping is used for density evaluation,
// region selection, meshing and crack prevention, so terrain is continuous
// across every face and region boundary by construction.
// =============================================================================

#include "CoreMinimal.h"
#include "LYTHOS2/Lythos2Types.h"

namespace Lythos2
{
    namespace CubeSphere
    {
        /** Map face-local UV in [0,1]^2 to a unit direction. */
        ANDROMEDA_API FVector FaceUVToDirection(int32 Face, float U, float V);

        /** Map a direction to its cube face and face-local UV in [0,1]^2. */
        ANDROMEDA_API void DirectionToFaceUV(const FVector& Direction, int32& OutFace, float& OutU, float& OutV);

        /** Direction of the centre of a region. */
        ANDROMEDA_API FVector RegionCenterDirection(const FLythos2RegionKey& Key);

        /** Direction of an arbitrary grid sample inside a region (Local in [0,1]^2). */
        ANDROMEDA_API FVector RegionSampleDirection(const FLythos2RegionKey& Key, float LocalU, float LocalV);

        /** Approximate world-space lateral size of a region at its LOD, in cm. */
        ANDROMEDA_API double RegionWorldSizeCm(const FLythos2RegionKey& Key, const FLythos2PlanetContext& Context);

        /** Centre of a region in planet-local space (reference radius). */
        ANDROMEDA_API FVector RegionCenterLocal(const FLythos2RegionKey& Key, const FLythos2PlanetContext& Context);

        /** Number of cube faces. */
        constexpr int32 NumFaces = 6;

        /** Number of LOD-0 root regions. */
        constexpr int32 NumRootRegions = NumFaces;
    }
}
