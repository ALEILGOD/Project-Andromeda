#include "LYTHOS2/Lythos2Mesher.h"

#include "LYTHOS2/Lythos2CubeSphere.h"
#include "LYTHOS2/Lythos2DensityField.h"
#include "HAL/PlatformTime.h"

namespace
{
    // The classic 6-tetrahedron cube decomposition. The main diagonal c0-c6 is
    // shared consistently by adjacent cubes, which makes the decomposition
    // weld across region boundaries with no T-junctions and no table lookup.
    constexpr int32 GTetCorners[6][4] =
    {
        { 0, 5, 1, 6 },
        { 0, 1, 2, 6 },
        { 0, 2, 3, 6 },
        { 0, 3, 7, 6 },
        { 0, 7, 4, 6 },
        { 0, 4, 5, 6 }
    };

    constexpr int32 GTetEdges[6][2] =
    {
        { 0, 1 }, { 0, 2 }, { 0, 3 },
        { 1, 2 }, { 1, 3 }, { 2, 3 }
    };

    // Cube corner offsets (x = U axis, y = V axis, z = radial axis).
    constexpr int32 GCubeCorner[8][3] =
    {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
        { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }
    };

    struct FMeshBuildContext
    {
        const FLythos2PlanetContext* Planet = nullptr;
        FLythos2MeshData* Mesh = nullptr;

        static FColor TerrainColor(const FVector& P, const FVector& Normal, float SurfaceRadius, float TerrainHeight)
        {
            const FVector Dir = P.GetSafeNormal();
            const float NormElev = (static_cast<float>(P.Size()) - SurfaceRadius)
                / FMath::Max(TerrainHeight, 1.0f);
            const float Slope = 1.0f - FMath::Clamp(Dir | Normal, 0.0f, 1.0f);

            if (NormElev < -0.02f) { return FColor(24, 70, 150, 255); }   // ocean
            if (NormElev < 0.05f) { return FColor(196, 182, 130, 255); }  // coast / sand
            if (NormElev > 0.80f || Slope > 0.72f) { return FColor(235, 235, 240, 255); } // peaks
            if (NormElev > 0.45f) { return FColor(120, 118, 110, 255); }  // highland rock
            return FColor(86, 132, 68, 255);                              // plains / forest
        }

        mutable TMap<FIntVector, int32> VertexLookup;

        /**
         * Append (or reuse) one surface vertex. Marching tetra produces the
         * same crossing position, normal and colour for a shared edge/face, so
         * welding them within a region removes ~2/3 of the vertex and upload
         * cost with no visual or determinism change.
         */
        int32 GetOrAddVertex(const FVector& P, const FVector& N, const FColor& Color) const
        {
            const FIntVector Key(
                FMath::RoundToInt(P.X * 2048.0),
                FMath::RoundToInt(P.Y * 2048.0),
                FMath::RoundToInt(P.Z * 2048.0));

            if (const int32* Existing = VertexLookup.Find(Key))
            {
                return *Existing;
            }

            const int32 Index = Mesh->Positions.Num();
            Mesh->Positions.Add(P);
            Mesh->Normals.Add(N);
            Mesh->Colors.Add(Color);

            const FVector Dir = P.GetSafeNormal();
            const float U = 0.5f + FMath::Atan2(Dir.Y, Dir.X) / (2.0f * PI);
            const float V = 0.5f - FMath::Asin(FMath::Clamp(Dir.Z, -1.0f, 1.0f)) / PI;
            Mesh->UVs.Add(FVector2D(U, V));

            VertexLookup.Add(Key, Index);
            return Index;
        }

        /**
         * Append one triangle and orient it for Unreal's front-face convention.
         *
         * Unreal's known-good GenerateBoxMesh stores exterior triangles so the
         * geometric normal cross(B-A, C-A) points OPPOSITE the outward vertex
         * normal (verified against UKismetProceduralMeshLibrary::GenerateBoxMesh,
         * where the +Z face has outward normal +Z but cross(B-A,C-A) = -Z).
         * We therefore flip the winding whenever cross(B-A,C-A) points along the
         * requested OutwardNormal.
         */
        void EmitTriangle(
            const FVector& A, const FVector& B, const FVector& C,
            const FVector& NA, const FVector& NB, const FVector& NC,
            const FVector& OutwardNormal,
            const FColor& CA, const FColor& CB, const FColor& CC) const
        {
            const FVector FaceNormal = FVector::CrossProduct(B - A, C - A);
            // Reject only genuinely zero-area triangles; thin slivers are valid
            // geometry for steep walls and thin roofs and must be kept.
            if (FaceNormal.IsNearlyZero())
            {
                return;
            }

            FVector VB = B;
            FVector VC = C;
            FVector VNB = NB;
            FVector VNC = NC;
            FColor ColorB = CB;
            FColor ColorC = CC;

            if (FVector::DotProduct(FaceNormal, OutwardNormal) > 0.0f)
            {
                Swap(VB, VC);
                Swap(VNB, VNC);
                Swap(ColorB, ColorC);
            }

            const int32 IA = GetOrAddVertex(A, NA, CA);
            const int32 IB = GetOrAddVertex(VB, VNB, ColorB);
            const int32 IC = GetOrAddVertex(VC, VNC, ColorC);

            // Welding can collapse two corners onto one vertex; emitting that
            // triangle would store a duplicate-index (degenerate) face.
            if (IA == IB || IB == IC || IA == IC)
            {
                return;
            }

            Mesh->Indices.Add(IA);
            Mesh->Indices.Add(IB);
            Mesh->Indices.Add(IC);
        }

        /** Surface triangle with terrain palette colours; OutwardNormal points to empty space. */
        void AppendTriangle(
            const FVector& A, const FVector& B, const FVector& C,
            const FVector& NA, const FVector& NB, const FVector& NC,
            const FVector& OutwardNormal) const
        {
            const FColor CA = TerrainColor(A, NA, Planet->RadiusCm, Planet->TerrainHeightCm);
            const FColor CB = TerrainColor(B, NB, Planet->RadiusCm, Planet->TerrainHeightCm);
            const FColor CC = TerrainColor(C, NC, Planet->RadiusCm, Planet->TerrainHeightCm);
            EmitTriangle(A, B, C, NA, NB, NC, OutwardNormal, CA, CB, CC);
        }
    };

    /**
     * Phase 3.3 geometry-aware complexity of a region, measured from the
     * authoritative density field (never from viewer/order state). Combines the
     * fraction of columns with multiple radial sign transitions (narrow/roofed
     * voids), a radial curvature proxy, and LATERAL surface curvature (canyon
     * walls, cliffs, narrow openings).
     */
    float MeasureRegionComplexity(
        const FLythos2PlanetContext& Context,
        const FLythos2RegionKey& Key,
        int32 ProbeN,
        double RMin,
        double RMax,
        float* OutDepression = nullptr)
    {
        ProbeN = FMath::Clamp(ProbeN, 4, 16);
        const int32 PSu = ProbeN + 1;
        // The radial probe must be fine enough to SEE a narrow (<0.1H) void,
        // otherwise a thin feature is invisible and never triggers refinement.
        const int32 PSr = FMath::Clamp(ProbeN * 8, 24, 80);
        const double H = FMath::Max(1.0f, Context.TerrainHeightCm);

        TArray<double> Radii;
        TArray<double> Vals;
        TArray<double> SurfR;
        Radii.SetNumUninitialized(PSr);
        Vals.SetNumUninitialized(PSr);
        SurfR.Init(-1.0, PSu * PSu);

        int32 Columns = 0;
        double MultiSum = 0.0;
        double CurveSum = 0.0;

        for (int32 I = 0; I < PSu; ++I)
        {
            const float LU = static_cast<float>(I) / static_cast<float>(ProbeN);
            for (int32 J = 0; J < PSu; ++J)
            {
                const float LV = static_cast<float>(J) / static_cast<float>(ProbeN);
                const FVector Dir = Lythos2::CubeSphere::RegionSampleDirection(Key, LU, LV);
                for (int32 K = 0; K < PSr; ++K)
                {
                    Radii[K] = RMin + (RMax - RMin) * (static_cast<double>(K) / (PSr - 1));
                }
                Lythos2::Density::SampleDensityColumn(Context, Dir, Radii.GetData(), PSr, Vals.GetData());

                int32 SignChanges = 0;
                int32 PrevSign = 0;
                double MaxCurv = 0.0;
                double OuterSurface = -1.0;
                for (int32 K = 0; K < PSr; ++K)
                {
                    const int32 S = Vals[K] > 0.0 ? 1 : (Vals[K] < 0.0 ? -1 : 0);
                    if (S != 0 && PrevSign != 0 && S != PrevSign) { ++SignChanges; }
                    if (S != 0) { PrevSign = S; }
                }
                for (int32 K = 1; K + 1 < PSr; ++K)
                {
                    MaxCurv = FMath::Max(MaxCurv, FMath::Abs(Vals[K + 1] - 2.0 * Vals[K] + Vals[K - 1]));
                }
                // Outermost solid crossing (first solid from the outside).
                for (int32 K = PSr - 1; K >= 0; --K)
                {
                    if (Vals[K] > 0.0)
                    {
                        if (K == PSr - 1) { OuterSurface = Radii[K]; }
                        else
                        {
                            const double Denom = Vals[K] - Vals[K + 1];
                            const double T = FMath::Abs(Denom) > 1.0e-12 ? (-Vals[K + 1]) / Denom : 0.0;
                            OuterSurface = FMath::Lerp(Radii[K + 1], Radii[K], T);
                        }
                        break;
                    }
                }
                SurfR[I * PSu + J] = OuterSurface;

                ++Columns;
                MultiSum += FMath::Clamp(static_cast<double>(SignChanges) / 4.0, 0.0, 1.0);
                CurveSum += FMath::Clamp(MaxCurv / (H * 0.25), 0.0, 1.0);
            }
        }

        if (Columns == 0)
        {
            return 0.0f;
        }

        // Lateral second-difference of the surface radius -> cliff / canyon wall
        // curvature, and first-difference -> large elevation drops / depressions.
        double LatSum = 0.0;
        int32 LatCount = 0;
        double MaxDrop = 0.0;
        for (int32 I = 0; I < PSu; ++I)
        {
            for (int32 J = 0; J < PSu; ++J)
            {
                if (I + 1 >= PSu || J + 1 >= PSu) { continue; }
                const double A = SurfR[I * PSu + J];
                const double B = SurfR[(I + 1) * PSu + J];
                const double C = SurfR[I * PSu + (J + 1)];
                const double D = SurfR[(I + 1) * PSu + (J + 1)];
                if (A < 0.0 || B < 0.0 || C < 0.0 || D < 0.0) { continue; }
                const double D2 = FMath::Abs((A + D) - (B + C));
                LatSum += FMath::Clamp(D2 / (H * 0.20), 0.0, 1.0);
                MaxDrop = FMath::Max(MaxDrop, FMath::Abs(A - B));
                MaxDrop = FMath::Max(MaxDrop, FMath::Abs(A - C));
                ++LatCount;
            }
        }
        const double Lateral = LatCount > 0 ? (LatSum / LatCount) : 0.0;
        // Phase 3.4: a large elevation drop / deep depression is a first-class
        // geometric complexity, normalised by the local geological height scale.
        const double Depression = FMath::Clamp(MaxDrop / (H * 0.55), 0.0, 1.0);
        if (OutDepression)
        {
            *OutDepression = static_cast<float>(Depression);
        }

        return FMath::Clamp(
            static_cast<float>(0.28 * (MultiSum / Columns) + 0.14 * (CurveSum / Columns)
                + 0.28 * Lateral + 0.30 * Depression),
            0.0f, 1.0f);
    }
}

namespace Lythos2
{
    namespace Mesher
    {
        double RegionSampleSpacingCm(
            const FLythos2RegionKey& Key,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings)
        {
            const double Size = CubeSphere::RegionWorldSizeCm(Key, Context);
            return Size / FMath::Max(1, Settings.VoxelsPerAxis);
        }

        void BuildRegionMesh(
            const FLythos2PlanetContext& Context,
            const FLythos2RegionKey& Key,
            const FLythos2Settings& Settings,
            FLythos2MeshData& OutMesh)
        {
            OutMesh.Reset();

            if (!Context.IsValid() || !Key.IsValid())
            {
                return;
            }

            // -----------------------------------------------------------------
            // Phase 3.3 dynamic, geometry-aware local resolution.
            //
            // Ordinary terrain keeps the base resolution. Only when the density
            // field genuinely needs detail does the region refine: the region
            // resolution is selected from the MEASURED geometric complexity
            // (multiple radial sign transitions + radial curvature), then each
            // individual column may locally redistribute its radial samples onto
            // its own surface and narrow layers. Every decision comes from the
            // density field alone, so it is deterministic and independent of the
            // viewer, generation order or neighbouring regions.
            // -----------------------------------------------------------------
            const double R = Context.RadiusCm;
            const double H = Context.TerrainHeightCm;
            // The deepest roofed void / cavity can sit over a TerrainHeight below
            // the macro envelope; the radial domain must contain the topology.
            const double GeologyDepth = 0.55 * FMath::Clamp(Context.GeologyAmount, 0.0f, 1.0f);
            const double RMin = FMath::Max(R * 0.05, R - H * (Settings.RadialBelowScale + Settings.RadialMarginScale + GeologyDepth));
            const double RMax = R + H * Settings.RadialAboveScale + H * Settings.RadialMarginScale;

            const int32 BaseN = FMath::Clamp(Settings.VoxelsPerAxis, 4, 32);
            int32 N = BaseN;
            float RegionComplexity = 0.0f;
            float DepressionComplexity = 0.0f;
            if (Context.GeologyAmount > 0.0f && Settings.bAdaptiveResolution && Settings.VolumetricResolutionLevels > 0)
            {
                const int32 MaxN = FMath::Clamp(Settings.MaxVolumetricVoxelsPerAxis, BaseN, 32);
                if (MaxN > BaseN)
                {
                    // Cheap pre-check: only pay for the measured probe where a
                    // feature/canyon/depression could plausibly be present.
                    const float Importance = Density::FeatureImportance(Context, CubeSphere::RegionCenterDirection(Key));
                    if (Importance > 0.02f)
                    {
                        RegionComplexity = MeasureRegionComplexity(Context, Key, FMath::Min(BaseN, 8), RMin, RMax, &DepressionComplexity);
                    }
                    // Phase 3.4: a smooth, curved mapping keeps ordinary terrain
                    // at base resolution while giving moderate/steep terrain a
                    // modest increase in detail (not a global density increase).
                    const float T = FMath::SmoothStep(0.25f, 0.90f, RegionComplexity);
                    N = FMath::Clamp(FMath::RoundToInt(FMath::Lerp(static_cast<float>(BaseN), static_cast<float>(MaxN), T)), BaseN, 32);
                }
            }
            OutMesh.BaseVoxelsUsed = BaseN;
            OutMesh.VoxelsUsed = N;
            OutMesh.RegionComplexity = RegionComplexity;
            OutMesh.DepressionComplexity = DepressionComplexity;
            OutMesh.MaxSurfaceDisplacementCm = static_cast<double>(DepressionComplexity) * H * 0.55;
            OutMesh.RadialRangeUsedCm = RMax - RMin;
            OutMesh.RefinementLevels = (N > BaseN) ? 1 : 0;

            int32 Nr = N;
            if (Context.GeologyAmount > 0.0f)
            {
                float Oversample = FMath::Clamp(Settings.GeologyRadialOversample, 1.0f, 3.0f);
                Oversample += 0.75f * RegionComplexity;
                Nr = FMath::Clamp(FMath::RoundToInt(static_cast<float>(N) * Oversample), N, 64);
            }
            const int32 Su = N + 1;
            const int32 Sr = Nr + 1;
            const double RadialStep = (RMax - RMin) / static_cast<double>(FMath::Max(1, Nr));
            OutMesh.TotalCellCount = N * N * Nr;

            const int32 SampleCount = Su * Su * Sr;

            TArray<FVector> Positions;
            TArray<double> Values;
            TArray<FVector> Gradients;
            Positions.SetNumUninitialized(SampleCount);
            Values.SetNumUninitialized(SampleCount);
            Gradients.SetNumUninitialized(SampleCount);

            auto Index = [Su, Sr](int32 I, int32 J, int32 K) { return (I * Su + J) * Sr + K; };

            const double DensityStart = FPlatformTime::Seconds();

            // 1. Sample the authoritative density field once per grid point. Each
            //    radial column shares one direction, so the direction-only
            //    geomorphology is evaluated once per column and reused for every
            //    radius (SampleDensityColumn).
            TArray<double> UniformRadii;
            TArray<double> UniformVals;
            TArray<double> WarpedRadii;
            TArray<double> WarpedVals;
            UniformRadii.SetNumUninitialized(Sr);
            UniformVals.SetNumUninitialized(Sr);
            WarpedRadii.SetNumUninitialized(Sr);
            WarpedVals.SetNumUninitialized(Sr);

            const bool bLocalRadialRefine = Context.GeologyAmount > 0.0f
                && Settings.bLocalRadialRefinement && Settings.bAdaptiveResolution;

            for (int32 I = 0; I < Su; ++I)
            {
                const float LU = static_cast<float>(I) / static_cast<float>(N);
                for (int32 J = 0; J < Su; ++J)
                {
                    const float LV = static_cast<float>(J) / static_cast<float>(N);
                    const FVector Dir = CubeSphere::RegionSampleDirection(Key, LU, LV);

                    for (int32 K = 0; K < Sr; ++K)
                    {
                        UniformRadii[K] = RMin + RadialStep * K;
                    }
                    Density::SampleDensityColumn(Context, Dir, UniformRadii.GetData(), Sr, UniformVals.GetData());

                    const double* UseVals = UniformVals.GetData();
                    const double* UseRadii = UniformRadii.GetData();

                    // Local radial refinement for geometrically complex columns.
                    if (bLocalRadialRefine)
                    {
                        int32 SignChanges = 0;
                        int32 PrevSign = 0;
                        double MaxCurv = 0.0;
                        for (int32 K = 0; K < Sr; ++K)
                        {
                            const int32 S = UniformVals[K] > 0.0 ? 1 : (UniformVals[K] < 0.0 ? -1 : 0);
                            if (S != 0 && PrevSign != 0 && S != PrevSign) { ++SignChanges; }
                            if (S != 0) { PrevSign = S; }
                        }
                        for (int32 K = 1; K + 1 < Sr; ++K)
                        {
                            MaxCurv = FMath::Max(MaxCurv,
                                FMath::Abs(UniformVals[K + 1] - 2.0 * UniformVals[K] + UniformVals[K - 1]));
                        }
                        // Local radial refinement is triggered only by genuine
                        // topological complexity (a real void: multiple sign
                        // changes) or a truly steep layer, never by the broad
                        // low-amplitude density curvature of ordinary terrain.
                        const float ColComplexity = FMath::Clamp(
                            (SignChanges >= 3 ? 0.7f : 0.0f)
                            + (MaxCurv > H * 0.28 ? 0.5f : 0.0f),
                            0.0f, 1.0f);

                        if (ColComplexity > 0.4f)
                        {
                            TArray<double> Weight;
                            Weight.SetNumUninitialized(Sr);
                            const double SurfaceScale = H * 0.05;
                            for (int32 K = 0; K < Sr; ++K)
                            {
                                const double D = FMath::Abs(UniformVals[K]) / SurfaceScale;
                                const double NearSurface = FMath::Exp(-D * D);
                                double Curv = 0.0;
                                if (K > 0 && K + 1 < Sr)
                                {
                                    Curv = FMath::Min(1.0,
                                        FMath::Abs(UniformVals[K + 1] - 2.0 * UniformVals[K] + UniformVals[K - 1]) / (H * 0.15));
                                }
                                Weight[K] = 1.0 + 6.0 * NearSurface + 4.0 * Curv;
                            }

                            double Total = 0.0;
                            for (int32 K = 0; K + 1 < Sr; ++K)
                            {
                                Total += 0.5 * (Weight[K] + Weight[K + 1]);
                            }

                            if (Total > 0.0)
                            {
                                WarpedRadii[0] = RMin;
                                WarpedRadii[Nr] = RMax;
                                int32 Cursor = 0;
                                double Acc = 0.0;
                                for (int32 M = 1; M < Nr; ++M)
                                {
                                    const double Target = Total * (static_cast<double>(M) / Nr);
                                    while (Cursor + 1 < Sr - 1 && Acc + 0.5 * (Weight[Cursor] + Weight[Cursor + 1]) < Target)
                                    {
                                        Acc += 0.5 * (Weight[Cursor] + Weight[Cursor + 1]);
                                        ++Cursor;
                                    }
                                    const double Seg = FMath::Max(1.0e-12, 0.5 * (Weight[Cursor] + Weight[Cursor + 1]));
                                    const double Frac = FMath::Clamp((Target - Acc) / Seg, 0.0, 1.0);
                                    WarpedRadii[M] = UniformRadii[Cursor] + (UniformRadii[Cursor + 1] - UniformRadii[Cursor]) * Frac;
                                }
                                // Strict monotonicity with a minimum cell size so
                                // refinement never creates degenerate (hole-
                                // producing) slivers.
                                const double MinGap = RadialStep * 0.5;
                                WarpedRadii[1] = FMath::Max(WarpedRadii[1], RMin + MinGap);
                                for (int32 M = 2; M < Nr; ++M)
                                {
                                    WarpedRadii[M] = FMath::Max(WarpedRadii[M], WarpedRadii[M - 1] + MinGap);
                                }
                                WarpedRadii[Nr - 1] = FMath::Min(WarpedRadii[Nr - 1], RMax - MinGap);
                                for (int32 M = Nr - 2; M >= 1; --M)
                                {
                                    WarpedRadii[M] = FMath::Min(WarpedRadii[M], WarpedRadii[M + 1] - MinGap);
                                }

                                Density::SampleDensityColumn(Context, Dir, WarpedRadii.GetData(), Sr, WarpedVals.GetData());
                                UseRadii = WarpedRadii.GetData();
                                UseVals = WarpedVals.GetData();
                                ++OutMesh.RadialRefinedColumns;
                            }
                        }
                    }

                    for (int32 K = 0; K < Sr; ++K)
                    {
                        const int32 Idx = Index(I, J, K);
                        Positions[Idx] = Dir * UseRadii[K];
                        Values[Idx] = UseVals[K];
                    }
                }
            }
            OutMesh.RefinedCellCount = OutMesh.RadialRefinedColumns * Nr;
            OutMesh.DeepRefinedColumns = (DepressionComplexity > 0.25f) ? OutMesh.RadialRefinedColumns : 0;

            // 2. Derive the gradient from the sampled grid with finite
            //    differences instead of re-evaluating the density field six
            //    more times per sample. The gradient is only used for surface
            //    normals and triangle orientation, so this is both exact to the
            //    sampled field and ~7x cheaper. Fully deterministic.
            for (int32 I = 0; I < Su; ++I)
            {
                for (int32 J = 0; J < Su; ++J)
                {
                    for (int32 K = 0; K < Sr; ++K)
                    {
                        const int32 Idx = Index(I, J, K);

                        int32 I0 = FMath::Max(I - 1, 0);
                        int32 I1 = FMath::Min(I + 1, N);
                        int32 J0 = FMath::Max(J - 1, 0);
                        int32 J1 = FMath::Min(J + 1, N);
                        int32 K0 = FMath::Max(K - 1, 0);
                        int32 K1 = FMath::Min(K + 1, Nr);

                        FVector Gradient = FVector::ZeroVector;

                        const FVector DU = Positions[Index(I1, J, K)] - Positions[Index(I0, J, K)];
                        const double DU2 = DU.SizeSquared();
                        if (DU2 > 1.0e-6)
                        {
                            Gradient += DU * ((Values[Index(I1, J, K)] - Values[Index(I0, J, K)]) / DU2);
                        }

                        const FVector DV = Positions[Index(I, J1, K)] - Positions[Index(I, J0, K)];
                        const double DV2 = DV.SizeSquared();
                        if (DV2 > 1.0e-6)
                        {
                            Gradient += DV * ((Values[Index(I, J1, K)] - Values[Index(I, J0, K)]) / DV2);
                        }

                        const FVector DR = Positions[Index(I, J, K1)] - Positions[Index(I, J, K0)];
                        const double DR2 = DR.SizeSquared();
                        if (DR2 > 1.0e-6)
                        {
                            Gradient += DR * ((Values[Index(I, J, K1)] - Values[Index(I, J, K0)]) / DR2);
                        }

                        Gradients[Idx] = Gradient;
                    }
                }
            }

            const double DensityEnd = FPlatformTime::Seconds();
            const double MeshStart = DensityEnd;

            FMeshBuildContext Build;
            Build.Planet = &Context;
            Build.Mesh = &OutMesh;


            for (int32 I = 0; I < N; ++I)
            {
                for (int32 J = 0; J < N; ++J)
                {
                    for (int32 K = 0; K < Nr; ++K)
                    {
                        int32 CornerIdx[8];
                        FVector CornerPos[8];
                        double CornerVal[8];
                        FVector CornerGrad[8];

                        for (int32 C = 0; C < 8; ++C)
                        {
                            const int32 Idx = Index(I + GCubeCorner[C][0], J + GCubeCorner[C][1], K + GCubeCorner[C][2]);
                            CornerIdx[C] = Idx;
                            CornerPos[C] = Positions[Idx];
                            CornerVal[C] = Values[Idx];
                            CornerGrad[C] = Gradients[Idx];
                        }

                        for (int32 T = 0; T < 6; ++T)
                        {
                            const int32* Tet = GTetCorners[T];

                            bool bInside[4];
                            int32 InsideCount = 0;
                            for (int32 V = 0; V < 4; ++V)
                            {
                                bInside[V] = CornerVal[Tet[V]] > 0.0;
                                InsideCount += bInside[V] ? 1 : 0;
                            }

                            if (InsideCount == 0 || InsideCount == 4)
                            {
                                continue;
                            }

                            FVector CrossPos[4];
                            FVector CrossGrad[4];
                            int32 CrossCount = 0;

                            for (int32 E = 0; E < 6; ++E)
                            {
                                const int32 A = GTetEdges[E][0];
                                const int32 B = GTetEdges[E][1];
                                if (bInside[A] == bInside[B])
                                {
                                    continue;
                                }

                                const int32 CA = Tet[A];
                                const int32 CB = Tet[B];
                                const double VA = CornerVal[CA];
                                const double VB = CornerVal[CB];
                                const double Denom = VA - VB;
                                const double TAlpha = FMath::Abs(Denom) > 1.0e-12 ? VA / Denom : 0.5;

                                if (CrossCount < 4)
                                {
                                    CrossPos[CrossCount] = FMath::Lerp(CornerPos[CA], CornerPos[CB], TAlpha);
                                    CrossGrad[CrossCount] = FMath::Lerp(CornerGrad[CA], CornerGrad[CB], TAlpha);
                                }
                                ++CrossCount;
                            }

                            auto MakeNormal = [](const FVector& G)
                            {
                                FVector Nrm = -G;
                                if (Nrm.IsNearlyZero())
                                {
                                    return FVector::UpVector;
                                }
                                return Nrm.GetSafeNormal();
                            };

                            if (CrossCount == 3)
                            {
                                const FVector AvgGrad = (CrossGrad[0] + CrossGrad[1] + CrossGrad[2]) / 3.0;
                                Build.AppendTriangle(
                                    CrossPos[0], CrossPos[1], CrossPos[2],
                                    MakeNormal(CrossGrad[0]), MakeNormal(CrossGrad[1]), MakeNormal(CrossGrad[2]),
                                    -AvgGrad);
                            }
                            else if (CrossCount == 4)
                            {
                                // Order the quad around the tetra's in/out sets.
                                int32 InVerts[2];
                                int32 OutVerts[2];
                                int32 InN = 0, OutN = 0;
                                for (int32 V = 0; V < 4; ++V)
                                {
                                    if (bInside[V]) { if (InN < 2) InVerts[InN] = V; ++InN; }
                                    else { if (OutN < 2) OutVerts[OutN] = V; ++OutN; }
                                }

                                if (InN == 2 && OutN == 2)
                                {
                                    const int32 I0 = InVerts[0], I1 = InVerts[1];
                                    const int32 O0 = OutVerts[0], O1 = OutVerts[1];

                                    const int32 CA0 = Tet[I0], CA1 = Tet[I1];
                                    const int32 CB0 = Tet[O0], CB1 = Tet[O1];

                                    auto Interp = [&](int32 InCorner, int32 OutCorner, FVector& OutP, FVector& OutG)
                                    {
                                        const double VA = CornerVal[InCorner];
                                        const double VB = CornerVal[OutCorner];
                                        const double Denom = VA - VB;
                                        const double T = FMath::Abs(Denom) > 1.0e-12 ? VA / Denom : 0.5;
                                        OutP = FMath::Lerp(CornerPos[InCorner], CornerPos[OutCorner], T);
                                        OutG = FMath::Lerp(CornerGrad[InCorner], CornerGrad[OutCorner], T);
                                    };

                                    FVector P00, G00, P01, G01, P10, G10, P11, G11;
                                    Interp(CA0, CB0, P00, G00);
                                    Interp(CA0, CB1, P01, G01);
                                    Interp(CA1, CB1, P10, G10);
                                    Interp(CA1, CB0, P11, G11);

                                    const FVector Avg0 = (G00 + G01 + G10) / 3.0;
                                    const FVector Avg1 = (G00 + G10 + G11) / 3.0;

                                    Build.AppendTriangle(
                                        P00, P01, P10,
                                        MakeNormal(G00), MakeNormal(G01), MakeNormal(G10), -Avg0);

                                    Build.AppendTriangle(
                                        P00, P10, P11,
                                        MakeNormal(G00), MakeNormal(G10), MakeNormal(G11), -Avg1);
                                }
                            }
                        }
                    }
                }
            }

            // Radial cell size (used by smoothing fix-tolerance and the collar).
            const double RadialCell = (RMax - RMin) / FMath::Max(1, Nr);

            // -----------------------------------------------------------------
            // Phase 3.6 constrained, topology-aware smoothing.
            //
            // Reduces high-frequency mesh jitter that makes the terrain look
            // procedurally generated, while preserving real geology:
            //   * only INTERIOR vertices move - domain-boundary vertices are
            //     frozen, so cross-region welds and the density-conforming collar
            //     stay exact;
            //   * neighbour weights are bilateral (normal similarity), so the two
            //     sides of a sharp crease or thin wall are not averaged together;
            //   * displacement is bounded per iteration (no runaway shrinkage);
            //   * connectivity is unchanged, so overhangs, alcoves and cavity
            //     roofs/floors are preserved.
            // -----------------------------------------------------------------
            if (Settings.bSmoothExtractedMesh && Settings.MeshSmoothingIterations > 0
                && OutMesh.Positions.Num() > 0)
            {
                const int32 VCount = OutMesh.Positions.Num();

                // Neighbours + incident triangles + topology-based boundary set.
                TArray<TArray<int32>> Neighbors;
                TArray<TArray<int32>> IncidentTris;
                Neighbors.SetNum(VCount);
                IncidentTris.SetNum(VCount);
                auto AddN = [&Neighbors](int32 A, int32 B)
                {
                    if (A != B && !Neighbors[A].Contains(B)) { Neighbors[A].Add(B); }
                };
                TMap<uint64, int32> EdgeUse;
                auto EdgeKey = [](int32 A, int32 B)
                {
                    const uint32 Lo = static_cast<uint32>(FMath::Min(A, B));
                    const uint32 Hi = static_cast<uint32>(FMath::Max(A, B));
                    return (static_cast<uint64>(Hi) << 32) | Lo;
                };
                for (int32 T = 0; T + 2 < OutMesh.Indices.Num(); T += 3)
                {
                    const int32 A = OutMesh.Indices[T], B = OutMesh.Indices[T + 1], C = OutMesh.Indices[T + 2];
                    AddN(A, B); AddN(A, C); AddN(B, C);
                    IncidentTris[A].Add(T); IncidentTris[B].Add(T); IncidentTris[C].Add(T);
                    ++EdgeUse.FindOrAdd(EdgeKey(A, B));
                    ++EdgeUse.FindOrAdd(EdgeKey(B, C));
                    ++EdgeUse.FindOrAdd(EdgeKey(C, A));
                }
                // Domain-boundary vertices = incident to an open (used-once)
                // edge. The isosurface is watertight internally, so these are
                // exactly the region sampling-domain boundary; freezing them
                // preserves cross-region welds and the collar, and works even at
                // ambiguous cube-face edges where DirectionToFaceUV is unreliable.
                TArray<uint8> Fixed;
                Fixed.Init(0, VCount);
                for (const TPair<uint64, int32>& E : EdgeUse)
                {
                    if (E.Value == 1)
                    {
                        Fixed[static_cast<int32>(E.Key & 0xffffffffu)] = 1;
                        Fixed[static_cast<int32>(E.Key >> 32)] = 1;
                    }
                }

                const float Strength = FMath::Clamp(Settings.MeshSmoothingStrength, 0.0f, 1.0f);
                TArray<FVector> Curr = OutMesh.Positions;
                TArray<FVector> Next = Curr;
                for (int32 Iter = 0; Iter < Settings.MeshSmoothingIterations; ++Iter)
                {
                    for (int32 V = 0; V < VCount; ++V)
                    {
                        if (Fixed[V] || Neighbors[V].Num() == 0)
                        {
                            Next[V] = Curr[V];
                            continue;
                        }
                        const FVector Ni = OutMesh.Normals[V];
                        FVector Sum = FVector::ZeroVector;
                        float WSum = 0.0f;
                        float AvgEdge = 0.0f;
                        for (int32 J : Neighbors[V])
                        {
                            const float W = FMath::Max(0.0f, static_cast<float>(FVector::DotProduct(Ni, OutMesh.Normals[J])));
                            Sum += Curr[J] * W;
                            WSum += W;
                            AvgEdge += static_cast<float>(FVector::Dist(Curr[V], Curr[J]));
                        }
                        AvgEdge /= static_cast<float>(Neighbors[V].Num());
                        if (WSum > 1.0e-6f)
                        {
                            FVector Delta = (Sum / WSum) - Curr[V];
                            const float MaxD = 0.30f * AvgEdge;
                            if (Delta.SizeSquared() > MaxD * MaxD)
                            {
                                Delta = Delta.GetSafeNormal() * MaxD;
                            }
                            FVector Candidate = Curr[V] + Delta * Strength;

                            // Anti-fold guard: never move a vertex if it would
                            // invert any incident triangle's winding.
                            for (int32 Guard = 0; Guard < 4; ++Guard)
                            {
                                bool bFlip = false;
                                for (int32 T : IncidentTris[V])
                                {
                                    const int32 A = OutMesh.Indices[T], B = OutMesh.Indices[T + 1], C = OutMesh.Indices[T + 2];
                                    const FVector PA = (A == V) ? Candidate : Curr[A];
                                    const FVector PB = (B == V) ? Candidate : Curr[B];
                                    const FVector PC = (C == V) ? Candidate : Curr[C];
                                    const FVector N0 = FVector::CrossProduct(Curr[B] - Curr[A], Curr[C] - Curr[A]);
                                    const FVector N1 = FVector::CrossProduct(PB - PA, PC - PA);
                                    if (FVector::DotProduct(N0, N1) < 0.0f) { bFlip = true; break; }
                                }
                                if (!bFlip) { break; }
                                Candidate = FMath::Lerp(Curr[V], Candidate, 0.5f);
                            }
                            Next[V] = Candidate;
                        }
                        else
                        {
                            Next[V] = Curr[V];
                        }
                    }
                    Swap(Curr, Next);
                }
                OutMesh.Positions = MoveTemp(Curr);

                // Recompute vertex normals from the smoothed geometry (area
                // weighted). Outward convention = -cross(B-A, C-A), matching
                // EmitTriangle (cross points into solid, -cross into empty space).
                for (FVector& Nrm : OutMesh.Normals) { Nrm = FVector::ZeroVector; }
                for (int32 T = 0; T + 2 < OutMesh.Indices.Num(); T += 3)
                {
                    const int32 A = OutMesh.Indices[T], B = OutMesh.Indices[T + 1], C = OutMesh.Indices[T + 2];
                    const FVector Fn = -FVector::CrossProduct(
                        OutMesh.Positions[B] - OutMesh.Positions[A],
                        OutMesh.Positions[C] - OutMesh.Positions[A]);
                    OutMesh.Normals[A] += Fn;
                    OutMesh.Normals[B] += Fn;
                    OutMesh.Normals[C] += Fn;
                }
                for (FVector& Nrm : OutMesh.Normals)
                {
                    if (!Nrm.Normalize()) { Nrm = FVector::UpVector; }
                }
            }

            // -----------------------------------------------------------------
            // Phase 3.5 multi-band transition collar.
            //
            // The collar is a bounded, density-conforming crack-filler. It now
            // seals EVERY solid band along the boundary (the terrain band AND the
            // roof/floor bands of any cavity, overhang or bridge), not just the
            // outermost surface, so volumetric features crossing a mixed-
            // resolution boundary no longer leave open seams. Each curtain is
            // sized from the actual seam magnitude and clamped to that band's
            // thickness, so it never crosses empty space, creates a visible wall
            // in the open, or stretches.
            // -----------------------------------------------------------------

            if (Settings.SkirtDepthCells > 0.0)
            {
                struct FSolidBand { double Top; double Bot; };

                auto ColumnSolidBands = [&](int32 CI, int32 CJ, TArray<FSolidBand>& OutBands)
                {
                    OutBands.Reset();
                    double PrevR = Positions[Index(CI, CJ, Sr - 1)].Size();
                    double PrevV = Values[Index(CI, CJ, Sr - 1)];
                    int32 PrevSign = PrevV > 0.0 ? 1 : (PrevV < 0.0 ? -1 : 0);
                    bool bOpen = PrevSign > 0;
                    double OpenTop = bOpen ? PrevR : 0.0;
                    for (int32 K = Sr - 2; K >= 0; --K)
                    {
                        const int32 Idx = Index(CI, CJ, K);
                        const double V = Values[Idx];
                        const double Rr = Positions[Idx].Size();
                        const int32 Sign = V > 0.0 ? 1 : (V < 0.0 ? -1 : 0);
                        if (PrevSign <= 0 && Sign > 0)
                        {
                            const double Den = V - PrevV;
                            const double T = FMath::Abs(Den) > 1.0e-12 ? (-PrevV) / Den : 0.5;
                            OpenTop = FMath::Lerp(PrevR, Rr, T);
                            bOpen = true;
                        }
                        else if (PrevSign > 0 && Sign <= 0)
                        {
                            const double Den = PrevV - V;
                            const double T = FMath::Abs(Den) > 1.0e-12 ? PrevV / Den : 0.5;
                            FSolidBand Band;
                            Band.Top = OpenTop;
                            Band.Bot = FMath::Lerp(PrevR, Rr, T);
                            OutBands.Add(Band);
                            bOpen = false;
                        }
                        PrevR = Rr;
                        PrevV = V;
                        PrevSign = Sign;
                    }
                    if (bOpen)
                    {
                        FSolidBand Band;
                        Band.Top = OpenTop;
                        Band.Bot = RMin;
                        OutBands.Add(Band);
                    }
                };

                struct FBoundaryDef { int32 FixedAxis; int32 FixedIndex; };
                const FBoundaryDef Boundaries[4] =
                {
                    { 0, 0 }, { 0, N }, { 1, 0 }, { 1, N }
                };

                TArray<TArray<FSolidBand>> ColumnBands;
                TArray<double> Surf;
                ColumnBands.SetNum(N + 1);
                Surf.SetNumUninitialized(N + 1);

                for (const FBoundaryDef& B : Boundaries)
                {
                    for (int32 C = 0; C <= N; ++C)
                    {
                        const int32 CI = (B.FixedAxis == 0) ? B.FixedIndex : C;
                        const int32 CJ = (B.FixedAxis == 0) ? C : B.FixedIndex;
                        ColumnSolidBands(CI, CJ, ColumnBands[C]);
                        Surf[C] = ColumnBands[C].Num() > 0 ? ColumnBands[C][0].Top : -1.0;
                    }

                    // Seam magnitude against a one-step-coarser neighbour:
                    // curvature (second difference) AND elevation drop (first).
                    double Crack = 0.0;
                    double MaxDropC = 0.0;
                    for (int32 C = 1; C < N; ++C)
                    {
                        if (Surf[C] > 0.0 && Surf[C - 1] > 0.0)
                        {
                            MaxDropC = FMath::Max(MaxDropC, FMath::Abs(Surf[C] - Surf[C - 1]));
                        }
                        if (Surf[C] > 0.0 && Surf[C - 1] > 0.0 && Surf[C + 1] > 0.0)
                        {
                            Crack = FMath::Max(Crack,
                                FMath::Abs(Surf[C] - 0.5 * (Surf[C - 1] + Surf[C + 1])));
                        }
                    }
                    const double BoundaryDepth = FMath::Min(
                        Crack * 1.25 + MaxDropC * 1.5 + 2.0 * RadialCell, H * 0.40);

                    for (int32 C = 0; C < N; ++C)
                    {
                        const int32 BandCount = FMath::Min(ColumnBands[C].Num(), ColumnBands[C + 1].Num());
                        if (BandCount <= 0)
                        {
                            continue;
                        }

                        int32 I0 = 0, J0 = 0, I1 = 0, J1 = 0;
                        float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
                        if (B.FixedAxis == 0)
                        {
                            I0 = I1 = B.FixedIndex;
                            J0 = C; J1 = C + 1;
                            U0 = U1 = static_cast<float>(B.FixedIndex) / static_cast<float>(N);
                            V0 = static_cast<float>(C) / static_cast<float>(N);
                            V1 = static_cast<float>(C + 1) / static_cast<float>(N);
                        }
                        else
                        {
                            J0 = J1 = B.FixedIndex;
                            I0 = C; I1 = C + 1;
                            V0 = V1 = static_cast<float>(B.FixedIndex) / static_cast<float>(N);
                            U0 = static_cast<float>(C) / static_cast<float>(N);
                            U1 = static_cast<float>(C + 1) / static_cast<float>(N);
                        }

                        const FVector D0 = CubeSphere::RegionSampleDirection(Key, U0, V0);
                        const FVector D1 = CubeSphere::RegionSampleDirection(Key, U1, V1);

                        for (int32 BandIdx = 0; BandIdx < BandCount; ++BandIdx)
                        {
                            const FSolidBand& Band0 = ColumnBands[C][BandIdx];
                            const FSolidBand& Band1 = ColumnBands[C + 1][BandIdx];
                            const double Thick0 = Band0.Top - Band0.Bot;
                            const double Thick1 = Band1.Top - Band1.Bot;
                            const double SegDepth = FMath::Min(BoundaryDepth, FMath::Min(Thick0, Thick1));
                            if (SegDepth <= 1.0)
                            {
                                continue;
                            }

                            // A hair inward keeps the collar from z-fighting; it
                            // never rises above the real surface.
                            const double Top0R = FMath::Max(Band0.Top - 1.0, RMin);
                            const double Top1R = FMath::Max(Band1.Top - 1.0, RMin);

                            const FVector Top0 = D0 * Top0R;
                            const FVector Top1 = D1 * Top1R;
                            const FVector Bot0 = D0 * FMath::Max(Top0R - SegDepth, RMin);
                            const FVector Bot1 = D1 * FMath::Max(Top1R - SegDepth, RMin);

                            const FVector QuadCenter = (Top0 + Top1 + Bot1 + Bot0) * 0.25;
                            const FVector CenterPoint = CubeSphere::RegionCenterDirection(Key) * QuadCenter.Size();
                            FVector OutwardRef = (QuadCenter - CenterPoint).GetSafeNormal();
                            if (OutwardRef.IsNearlyZero())
                            {
                                OutwardRef = QuadCenter.GetSafeNormal();
                            }

                            Build.AppendTriangle(Top0, Top1, Bot1, D0, D1, D1, OutwardRef);
                            Build.AppendTriangle(Top0, Bot1, Bot0, D0, D1, D0, OutwardRef);
                        }
                    }
                }
            }

            OutMesh.DensityMs = (DensityEnd - DensityStart) * 1000.0;
            OutMesh.MeshingMs = (FPlatformTime::Seconds() - MeshStart) * 1000.0;
        }
    }
}
