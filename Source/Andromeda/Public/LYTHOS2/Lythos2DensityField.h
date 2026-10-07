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
        /**
         * Phase 3.1 deterministic geology sample at a direction. Pure debug/test
         * seam into the geomorphology so automation can validate erosion
         * hierarchy, differential resistance, terraces, undercutting and the
         * roofed-void (bridge/arch/cave) system without inspecting meshes.
         */
        struct FLythos2GeologySample
        {
            float MacroElev = 0.0f;
            float Resistance = 0.0f;
            float PrimaryDrainage = 0.0f;
            float SecondaryDrainage = 0.0f;
            float TertiaryDrainage = 0.0f;
            double ErosionDepthCm = 0.0;
            double PrimaryIncisionCm = 0.0;
            double SecondaryIncisionCm = 0.0;
            double TertiaryIncisionCm = 0.0;
            double FineIncisionCm = 0.0;
            float TerraceStrength = 0.0f;
            double TerraceLayerCm = 0.0;
            float UndercutStrength = 0.0f;
            float VoidMask = 0.0f;
            float DeepVoidMask = 0.0f;
            float FeatureImportance = 0.0f;

            // Phase 3.3 sparse-feature selection + scale (diagnostics/tests).
            float SparseField = 0.0f;
            float VolumetricCandidate = 0.0f;
            double MinFeatureWidthCm = 0.0;
            int32 VolumetricAccepted = 0;

            // Phase 3.4 per-class acceptance (diagnostics/tests).
            int32 OverhangAccepted = 0;
            int32 CavityAccepted = 0;
            int32 BridgeAccepted = 0;
        };

        ANDROMEDA_API void SampleGeology(
            const FLythos2PlanetContext& Context,
            const FVector& Direction,
            FLythos2GeologySample& OutSample);

        /** Normalized macro elevation in [-1,1] for a unit direction. Derived. */
        ANDROMEDA_API float MacroElevation(const FLythos2PlanetContext& Context, const FVector& Direction);

        /** Derived surface radius (cm from planet centre) for a unit direction. */
        ANDROMEDA_API double SurfaceRadiusCm(const FLythos2PlanetContext& Context, const FVector& Direction);

        /** Authoritative scalar density at a planet-local position (cm). */
        ANDROMEDA_API double EvaluateDensity(const FLythos2PlanetContext& Context, const FVector& LocalPosition);

        /** Analytic-ish gradient (central differences) used for smooth normals. */
        ANDROMEDA_API FVector EvaluateGradient(const FLythos2PlanetContext& Context, const FVector& LocalPosition);

        /**
         * Evaluate the density field for a whole radial column in one call.
         * The direction-only geomorphology is computed once per column, which
         * makes the per-region mesher dramatically cheaper (the mesher walks
         * radial columns). Fully deterministic; identical results to calling
         * EvaluateDensity per sample.
         */
        ANDROMEDA_API void SampleDensityColumn(
            const FLythos2PlanetContext& Context,
            const FVector& Direction,
            const double* Radii,
            int32 Count,
            double* OutValues);

        /**
         * Outermost solid crossing radius scanning inward from above (the
         * visible terrain surface along this direction, including any surviving
         * roof). Returns a negative value when no solid is found within
         * SearchDepthCm below the macro surface. Used by the transition mesher
         * so boundary collars follow the real density surface (never the bare
         * macro envelope, which caused the visible region frame).
         */
        ANDROMEDA_API double FindSurfaceRadiusCm(
            const FLythos2PlanetContext& Context,
            const FVector& Direction,
            double SearchDepthCm);

        /**
         * Deterministic local geomorphology/volumetric complexity in [0,1] at a
         * direction. Used by the streamer/mesher to pick adaptive local
         * resolution (canyons, arches, natural bridges, caves refine more).
         */
        ANDROMEDA_API float FeatureImportance(const FLythos2PlanetContext& Context, const FVector& Direction);
    }
}
