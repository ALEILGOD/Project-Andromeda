#include "AndromedaNoiseLibrary.h"

float UAndromedaNoiseLibrary::GeneratePlanetHeight(
    FVector Direction,
    int32 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength
)
{
    Direction = Direction.GetSafeNormal();

    const FVector SeedOffset(
        Seed * 12.9898f,
        Seed * 78.233f,
        Seed * 37.719f
    );

    // =========================================================
    // CONTINENTI
    // =========================================================

    const float ContinentalNoise =
        FMath::PerlinNoise3D(
            Direction * ContinentalScale + SeedOffset
        );

    const float Continental01 =
        (ContinentalNoise + 1.0f) * 0.5f;

    // =========================================================
    // MASCHERA MONTAGNE
    // =========================================================

    const float MountainMask =
        FMath::Clamp(
            (Continental01 - 0.50f) / 0.30f,
            0.0f,
            1.0f
        );

    // =========================================================
    // MONTAGNE
    // =========================================================

    const float MountainNoise =
        FMath::PerlinNoise3D(
            Direction * MountainScale + SeedOffset * 1.37f
        );

    const float Ridged =
        1.0f - FMath::Abs(MountainNoise);

    const float MountainShape =
        FMath::Pow(Ridged, 3.0f);

    const float Mountains =
        MountainShape *
        MountainMask *
        MountainStrength;

    // =========================================================
    // DETTAGLI
    // =========================================================

    const float DetailNoise =
        FMath::PerlinNoise3D(
            Direction * DetailScale + SeedOffset * 2.71f
        );

    const float Detail =
        DetailNoise *
        MountainMask *
        DetailStrength;

    // =========================================================
    // PIANURE
    // =========================================================

    const float LandMask =
        FMath::Clamp(
            (Continental01 - 0.42f) / 0.20f,
            0.0f,
            1.0f
        );

    const float Plains =
        FMath::Pow(LandMask, 1.5f) *
        0.25f;

    // =========================================================
    // ALTEZZA FINALE
    // =========================================================

    return Plains +
        Mountains +
        Detail;
}


FVector UAndromedaNoiseLibrary::CalculatePlanetSurfaceNormal(
    FVector Direction,
    int32 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
)
{
    Direction = Direction.GetSafeNormal();

    // =========================================================
    // COSTRUZIONE DI UNA BASE TANGENTE
    // =========================================================

    FVector ReferenceAxis = FVector::UpVector;

    if (FMath::Abs(
        FVector::DotProduct(
            Direction,
            ReferenceAxis
        )
    ) > 0.95f)
    {
        ReferenceAxis = FVector::ForwardVector;
    }

    FVector Tangent =
        FVector::CrossProduct(
            ReferenceAxis,
            Direction
        ).GetSafeNormal();

    FVector Bitangent =
        FVector::CrossProduct(
            Direction,
            Tangent
        ).GetSafeNormal();

    // =========================================================
    // CAMPIONAMENTO DEL TERRENO
    // =========================================================

    const float SampleDistance = 0.0001f;

    const FVector DirectionTangent =
        (Direction + Tangent * SampleDistance).GetSafeNormal();

    const FVector DirectionBitangent =
        (Direction + Bitangent * SampleDistance).GetSafeNormal();

    const float CenterHeight =
        GeneratePlanetHeight(
            Direction,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        ) * TerrainHeight;

    const float TangentHeight =
        GeneratePlanetHeight(
            DirectionTangent,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        ) * TerrainHeight;

    const float BitangentHeight =
        GeneratePlanetHeight(
            DirectionBitangent,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        ) * TerrainHeight;

    // =========================================================
    // POSIZIONI LOCALI
    // =========================================================

    const FVector CenterPosition =
        Direction * CenterHeight;

    const FVector TangentPosition =
        DirectionTangent * (TangentHeight + CenterHeight);

    const FVector BitangentPosition =
        DirectionBitangent * (BitangentHeight + CenterHeight);

    // =========================================================
    // TANGENTI DELLA SUPERFICIE
    // =========================================================

    const FVector SurfaceTangent =
        TangentPosition - CenterPosition;

    const FVector SurfaceBitangent =
        BitangentPosition - CenterPosition;

    // =========================================================
    // NORMALE
    // =========================================================

    FVector Normal =
        FVector::CrossProduct(
            SurfaceTangent,
            SurfaceBitangent
        ).GetSafeNormal();

    // =========================================================
    // CONTROLLO DEL VERSO
    // =========================================================

    if (FVector::DotProduct(
        Normal,
        Direction
    ) < 0.0f)
    {
        Normal *= -1.0f;
    }

    return Normal;
}


FPlanetSurfaceData UAndromedaNoiseLibrary::GetPlanetSurfaceData(
    FVector Direction,
    int32 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
)
{
    FPlanetSurfaceData SurfaceData;

    // =========================================================
    // DIREZIONE NORMALIZZATA
    // =========================================================

    Direction = Direction.GetSafeNormal();

    SurfaceData.Direction = Direction;

    // =========================================================
    // ALTEZZA NORMALIZZATA
    // =========================================================

    const float NormalizedHeight =
        GeneratePlanetHeight(
            Direction,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        );

    SurfaceData.NormalizedHeight =
        NormalizedHeight;

    // =========================================================
    // ALTEZZA FISICA
    // =========================================================

    SurfaceData.Height =
        NormalizedHeight * TerrainHeight;

    // =========================================================
    // NORMALE DELLA SUPERFICIE
    // =========================================================

    SurfaceData.Normal =
        CalculatePlanetSurfaceNormal(
            Direction,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength,
            TerrainHeight
        );

    return SurfaceData;
}


void UAndromedaNoiseLibrary::GeneratePlanetVertices(
    int32 Resolution,
    float PlanetRadius,
    int32 FaceIndex,
    int32 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight,
    TArray<FVector>& OutVertices
)
{
    OutVertices.Reset();

    if (Resolution < 2)
    {
        return;
    }

    const int32 VertexCount =
        Resolution * Resolution;

    OutVertices.Reserve(VertexCount);

    for (int32 Row = 0;
        Row < Resolution;
        Row++)
    {
        for (int32 Col = 0;
            Col < Resolution;
            Col++)
        {
            const float U =
                static_cast<float>(Col) /
                static_cast<float>(Resolution - 1);

            const float V =
                static_cast<float>(Row) /
                static_cast<float>(Resolution - 1);

            const float X =
                U * 2.0f - 1.0f;

            const float Y =
                V * 2.0f - 1.0f;

            FVector CubePosition;

            switch (FaceIndex)
            {
            case 0:
                CubePosition =
                    FVector(1.0f, Y, X);
                break;

            case 1:
                CubePosition =
                    FVector(-1.0f, Y, -X);
                break;

            case 2:
                CubePosition =
                    FVector(X, 1.0f, Y);
                break;

            case 3:
                CubePosition =
                    FVector(X, -1.0f, -Y);
                break;

            case 4:
                CubePosition =
                    FVector(X, Y, 1.0f);
                break;

            case 5:
                CubePosition =
                    FVector(X, -Y, -1.0f);
                break;

            default:
                CubePosition =
                    FVector(X, Y, 1.0f);
                break;
            }

            const FVector Direction =
                CubePosition.GetSafeNormal();

            const float Height =
                GeneratePlanetHeight(
                    Direction,
                    Seed,
                    ContinentalScale,
                    MountainScale,
                    DetailScale,
                    MountainStrength,
                    DetailStrength
                ) * TerrainHeight;

            const float FinalRadius =
                PlanetRadius + Height;

            OutVertices.Add(
                Direction * FinalRadius
            );
        }
    }
}


void UAndromedaNoiseLibrary::GeneratePlanetMeshData(
    int32 Resolution,
    float PlanetRadius,
    int32 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals,
    TArray<FProcMeshTangent>& OutTangents
)
{
    OutVertices.Reset();
    OutTriangles.Reset();
    OutNormals.Reset();
    OutTangents.Reset();

    if (Resolution < 2)
    {
        return;
    }

    const int32 VerticesPerFace =
        Resolution * Resolution;

    const int32 CellsPerFace =
        (Resolution - 1) *
        (Resolution - 1);

    const int32 TotalVertices =
        VerticesPerFace * 6;

    const int32 TotalTriangles =
        CellsPerFace * 2 * 6;

    OutVertices.Reserve(TotalVertices);

    OutTriangles.Reserve(
        TotalTriangles * 3
    );

    OutNormals.SetNumZeroed(
        TotalVertices
    );

    OutTangents.SetNum(
        TotalVertices
    );

    // =========================================================
    // VERTICI
    // =========================================================

    for (int32 FaceIndex = 0;
        FaceIndex < 6;
        FaceIndex++)
    {
        const int32 FaceOffset =
            FaceIndex * VerticesPerFace;

        for (int32 Row = 0;
            Row < Resolution;
            Row++)
        {
            for (int32 Col = 0;
                Col < Resolution;
                Col++)
            {
                const float U =
                    static_cast<float>(Col) /
                    static_cast<float>(Resolution - 1);

                const float V =
                    static_cast<float>(Row) /
                    static_cast<float>(Resolution - 1);

                const float X =
                    U * 2.0f - 1.0f;

                const float Y =
                    V * 2.0f - 1.0f;

                FVector CubePosition;

                switch (FaceIndex)
                {
                case 0:
                    CubePosition =
                        FVector(1.0f, Y, X);
                    break;

                case 1:
                    CubePosition =
                        FVector(-1.0f, Y, -X);
                    break;

                case 2:
                    CubePosition =
                        FVector(X, 1.0f, Y);
                    break;

                case 3:
                    CubePosition =
                        FVector(X, -1.0f, -Y);
                    break;

                case 4:
                    CubePosition =
                        FVector(X, Y, 1.0f);
                    break;

                case 5:
                    CubePosition =
                        FVector(X, -Y, -1.0f);
                    break;

                default:
                    CubePosition =
                        FVector(X, Y, 1.0f);
                    break;
                }

                const FVector Direction =
                    CubePosition.GetSafeNormal();

                const float Height =
                    GeneratePlanetHeight(
                        Direction,
                        Seed,
                        ContinentalScale,
                        MountainScale,
                        DetailScale,
                        MountainStrength,
                        DetailStrength
                    ) * TerrainHeight;

                const float FinalRadius =
                    PlanetRadius + Height;

                OutVertices.Add(
                    Direction * FinalRadius
                );
            }
        }

        // =====================================================
        // TRIANGOLI
        // =====================================================

        for (int32 Row = 0;
            Row < Resolution - 1;
            Row++)
        {
            for (int32 Col = 0;
                Col < Resolution - 1;
                Col++)
            {
                const int32 A =
                    FaceOffset +
                    Row * Resolution +
                    Col;

                const int32 B =
                    A + 1;

                const int32 C =
                    A + Resolution;

                const int32 D =
                    B + Resolution;

                const bool ReverseWinding =
                    FaceIndex == 0 ||
                    FaceIndex == 1 ||
                    FaceIndex == 2 ||
                    FaceIndex == 3;

                if (ReverseWinding)
                {
                    // A-B-C
                    OutTriangles.Add(A);
                    OutTriangles.Add(B);
                    OutTriangles.Add(C);

                    // C-B-D
                    OutTriangles.Add(C);
                    OutTriangles.Add(B);
                    OutTriangles.Add(D);
                }
                else
                {
                    // A-C-B
                    OutTriangles.Add(A);
                    OutTriangles.Add(C);
                    OutTriangles.Add(B);

                    // B-C-D
                    OutTriangles.Add(B);
                    OutTriangles.Add(C);
                    OutTriangles.Add(D);
                }
            }
        }
    }

    // =========================================================
    // NORMALI GEOMETRICHE
    // =========================================================

    for (int32 TriangleIndex = 0;
        TriangleIndex < OutTriangles.Num();
        TriangleIndex += 3)
    {
        const int32 IndexA =
            OutTriangles[TriangleIndex];

        const int32 IndexB =
            OutTriangles[TriangleIndex + 1];

        const int32 IndexC =
            OutTriangles[TriangleIndex + 2];

        const FVector& A =
            OutVertices[IndexA];

        const FVector& B =
            OutVertices[IndexB];

        const FVector& C =
            OutVertices[IndexC];

        const FVector Edge1 =
            B - A;

        const FVector Edge2 =
            C - A;

        FVector FaceNormal =
            FVector::CrossProduct(
                Edge1,
                Edge2
            );

        if (FaceNormal.IsNearlyZero())
        {
            continue;
        }

        // =====================================================
        // CONTROLLO DEL VERSO
        // =====================================================

        const FVector TriangleCenter =
            (A + B + C) / 3.0f;

        const FVector OutwardDirection =
            TriangleCenter.GetSafeNormal();

        if (FVector::DotProduct(
            FaceNormal,
            OutwardDirection
        ) < 0.0f)
        {
            FaceNormal *= -1.0f;
        }

        // =====================================================
        // ACCUMULO
        // =====================================================

        OutNormals[IndexA] += FaceNormal;
        OutNormals[IndexB] += FaceNormal;
        OutNormals[IndexC] += FaceNormal;
    }

    // =========================================================
    // RAGGRUPPAMENTO DEI VERTICI CONDIVISI
    // =========================================================

    TMap<FIntVector, TArray<int32>> VertexGroups;

    VertexGroups.Reserve(
        TotalVertices
    );

    const double PositionScale =
        100000.0;

    for (int32 Index = 0;
        Index < TotalVertices;
        Index++)
    {
        const FVector& Position =
            OutVertices[Index];

        const FIntVector Key(
            FMath::RoundToInt(
                Position.X * PositionScale
            ),
            FMath::RoundToInt(
                Position.Y * PositionScale
            ),
            FMath::RoundToInt(
                Position.Z * PositionScale
            )
        );

        VertexGroups.FindOrAdd(Key).Add(Index);
    }

    // =========================================================
    // SMOOTHING
    // =========================================================

    for (const TPair<FIntVector, TArray<int32>>& Pair
        : VertexGroups)
    {
        const TArray<int32>& Indices =
            Pair.Value;

        if (Indices.Num() <= 1)
        {
            continue;
        }

        FVector CombinedNormal =
            FVector::ZeroVector;

        for (const int32 Index : Indices)
        {
            CombinedNormal +=
                OutNormals[Index];
        }

        if (!CombinedNormal.IsNearlyZero())
        {
            CombinedNormal.Normalize();
        }

        for (const int32 Index : Indices)
        {
            OutNormals[Index] =
                CombinedNormal;
        }
    }

    // =========================================================
    // NORMALIZZAZIONE FINALE
    // =========================================================

    for (int32 Index = 0;
        Index < TotalVertices;
        Index++)
    {
        if (OutNormals[Index].IsNearlyZero())
        {
            OutNormals[Index] =
                OutVertices[Index].GetSafeNormal();
        }
        else
        {
            OutNormals[Index].Normalize();
        }

        // Ultima garanzia:
        // la normale deve puntare verso l'esterno.
        const FVector OutwardDirection =
            OutVertices[Index].GetSafeNormal();

        if (FVector::DotProduct(
            OutNormals[Index],
            OutwardDirection
        ) < 0.0f)
        {
            OutNormals[Index] *= -1.0f;
        }
    }

    // =========================================================
    // TANGENTS
    // =========================================================

    for (int32 Index = 0;
        Index < TotalVertices;
        Index++)
    {
        const FVector Normal =
            OutNormals[Index];

        FVector ReferenceAxis =
            FVector::UpVector;

        if (FMath::Abs(
            FVector::DotProduct(
                Normal,
                ReferenceAxis
            )
        ) > 0.95f)
        {
            ReferenceAxis =
                FVector::ForwardVector;
        }

        const FVector Tangent =
            FVector::CrossProduct(
                ReferenceAxis,
                Normal
            ).GetSafeNormal();

        OutTangents[Index] =
            FProcMeshTangent(
                Tangent,
                false
            );
    }
}