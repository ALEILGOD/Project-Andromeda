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
                FMath::RoundToInt(P.X * 64.0),
                FMath::RoundToInt(P.Y * 64.0),
                FMath::RoundToInt(P.Z * 64.0));

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

            const int32 N = FMath::Clamp(Settings.VoxelsPerAxis, 4, 32);
            const int32 S = N + 1;

            const double R = Context.RadiusCm;
            const double H = Context.TerrainHeightCm;
            const double RMin = FMath::Max(R * 0.05, R - H * Settings.RadialBelowScale - H * Settings.RadialMarginScale);
            const double RMax = R + H * Settings.RadialAboveScale + H * Settings.RadialMarginScale;

            const int32 SampleCount = S * S * S;

            TArray<FVector> Positions;
            TArray<double> Values;
            TArray<FVector> Gradients;
            Positions.SetNumUninitialized(SampleCount);
            Values.SetNumUninitialized(SampleCount);
            Gradients.SetNumUninitialized(SampleCount);

            auto Index = [S](int32 I, int32 J, int32 K) { return (I * S + J) * S + K; };

            const double DensityStart = FPlatformTime::Seconds();

            // 1. Sample the authoritative density field exactly once per grid
            //    point. This is the only place density is evaluated.
            for (int32 I = 0; I < S; ++I)
            {
                const float LU = static_cast<float>(I) / static_cast<float>(N);
                for (int32 J = 0; J < S; ++J)
                {
                    const float LV = static_cast<float>(J) / static_cast<float>(N);
                    const FVector Dir = CubeSphere::RegionSampleDirection(Key, LU, LV);
                    for (int32 K = 0; K < S; ++K)
                    {
                        const double Radius = RMin + (RMax - RMin) * (static_cast<double>(K) / N);
                        const FVector P = Dir * Radius;
                        const int32 Idx = Index(I, J, K);
                        Positions[Idx] = P;
                        Values[Idx] = Density::EvaluateDensity(Context, P);
                    }
                }
            }

            // 2. Derive the gradient from the sampled grid with finite
            //    differences instead of re-evaluating the density field six
            //    more times per sample. The gradient is only used for surface
            //    normals and triangle orientation, so this is both exact to the
            //    sampled field and ~7x cheaper. Fully deterministic.
            for (int32 I = 0; I < S; ++I)
            {
                for (int32 J = 0; J < S; ++J)
                {
                    for (int32 K = 0; K < S; ++K)
                    {
                        const int32 Idx = Index(I, J, K);

                        int32 I0 = FMath::Max(I - 1, 0);
                        int32 I1 = FMath::Min(I + 1, N);
                        int32 J0 = FMath::Max(J - 1, 0);
                        int32 J1 = FMath::Min(J + 1, N);
                        int32 K0 = FMath::Max(K - 1, 0);
                        int32 K1 = FMath::Min(K + 1, N);

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
                    for (int32 K = 0; K < N; ++K)
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

            // -----------------------------------------------------------------
            // Crack-prevention skirts: a hidden radial curtain hanging inward
            // along every region boundary. It fills the geometric seam between
            // neighbouring regions at different LODs.
            // -----------------------------------------------------------------
            const double SkirtDepth = Settings.SkirtDepthCells
                * RegionSampleSpacingCm(Key, Context, Settings);
            if (SkirtDepth > 0.0)
            {
                struct FBoundaryDef { float StartU, StartV, StepU, StepV; };

                const FBoundaryDef Boundaries[4] =
                {
                    { 0.0f, 0.0f, 0.0f, 1.0f }, // U = 0
                    { 1.0f, 0.0f, 0.0f, 1.0f }, // U = 1
                    { 0.0f, 0.0f, 1.0f, 0.0f }, // V = 0
                    { 0.0f, 1.0f, 1.0f, 0.0f }  // V = 1
                };

                for (const FBoundaryDef& B : Boundaries)
                {
                    for (int32 C = 0; C < N; ++C)
                    {
                        const float T0 = static_cast<float>(C) / static_cast<float>(N);
                        const float T1 = static_cast<float>(C + 1) / static_cast<float>(N);

                        const float U0 = B.StartU + B.StepU * T0;
                        const float V0 = B.StartV + B.StepV * T0;
                        const float U1 = B.StartU + B.StepU * T1;
                        const float V1 = B.StartV + B.StepV * T1;

                        const FVector D0 = CubeSphere::RegionSampleDirection(Key, U0, V0);
                        const FVector D1 = CubeSphere::RegionSampleDirection(Key, U1, V1);

                        const double RS0 = Density::SurfaceRadiusCm(Context, D0);
                        const double RS1 = Density::SurfaceRadiusCm(Context, D1);

                        if (RS0 < RMin || RS0 > RMax || RS1 < RMin || RS1 > RMax)
                        {
                            continue;
                        }

                        const FVector Top0 = D0 * RS0;
                        const FVector Top1 = D1 * RS1;
                        const FVector Bot0 = D0 * FMath::Max(RS0 - SkirtDepth, RMin);
                        const FVector Bot1 = D1 * FMath::Max(RS1 - SkirtDepth, RMin);

                        // The curtain's outward face points away from the chunk
                        // centre, tangentially. EmitTriangle applies the same
                        // Unreal winding convention as the surface.
                        const FVector QuadCenter = (Top0 + Top1 + Bot1 + Bot0) * 0.25;
                        const FVector CenterPoint = CubeSphere::RegionCenterDirection(Key) * QuadCenter.Size();
                        FVector OutwardRef = (QuadCenter - CenterPoint).GetSafeNormal();
                        if (OutwardRef.IsNearlyZero())
                        {
                            OutwardRef = QuadCenter.GetSafeNormal();
                        }

                        const FColor SkirtColor(90, 110, 90, 255);
                        Build.EmitTriangle(Top0, Top1, Bot1, D0, D1, D1, OutwardRef, SkirtColor, SkirtColor, SkirtColor);
                        Build.EmitTriangle(Top0, Bot1, Bot0, D0, D1, D0, OutwardRef, SkirtColor, SkirtColor, SkirtColor);
                    }
                }
            }

            OutMesh.DensityMs = (DensityEnd - DensityStart) * 1000.0;
            OutMesh.MeshingMs = (FPlatformTime::Seconds() - MeshStart) * 1000.0;
        }
    }
}
