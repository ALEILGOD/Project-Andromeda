#pragma once

// =============================================================================
// LYTHOS 2.0 - Deterministic volumetric density field.
//
// The authoritative terrain is the scalar field Density(P) evaluated in
// planet-local coordinates. Positive = solid, negative = empty, 0 = surface.
//
// It is fully deterministic: same context + same position always yields the
// same value regardless of thread, order, LOD or cache state. It is genuinely
// volumetric (3D position dependent), so it inherently supports caves,
// overhangs and future voxel edits. A radial height is available only as a
// DERIVED convenience, never as the source of truth.
// =============================================================================

#include "CoreMinimal.h"
#include "LYTHOS2/Lythos2Types.h"

namespace Lythos2
{
    namespace Density
    {
        /** Normalized macro elevation in [-1,1] for a unit direction. Derived. */
        ANDROMEDA_API float MacroElevation(const FLythos2PlanetContext& Context, const FVector& Direction);

        /** Derived surface radius (cm from planet centre) for a unit direction. */
        ANDROMEDA_API double SurfaceRadiusCm(const FLythos2PlanetContext& Context, const FVector& Direction);

        /** Authoritative scalar density at a planet-local position (cm). */
        ANDROMEDA_API double EvaluateDensity(const FLythos2PlanetContext& Context, const FVector& LocalPosition);

        /** Analytic-ish gradient (central differences) used for smooth normals. */
        ANDROMEDA_API FVector EvaluateGradient(const FLythos2PlanetContext& Context, const FVector& LocalPosition);
    }
}
