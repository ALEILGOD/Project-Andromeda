#include "AndromedaNoiseLibrary.h"

#include "Planet/PlanetContinentalGenerator.h"
#include "Planet/PlanetLandformGenerator.h"

namespace
{
    // ============================================================
    // PLANET HEIGHT
    // ============================================================

    float GeneratePlanetHeightInternal(
        FVector Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength
    )
    {
        Direction =
            Direction.GetSafeNormal();

        // ========================================================
        // CONTINENTS
        // ========================================================

        const float ContinentalMask =
            UPlanetContinentalGenerator::GetContinentalMask(
                Direction,
                Seed,
                ContinentalScale
            );

        // ========================================================
        // LANDFORM DISTRIBUTION
        // ========================================================

        const float LandformMask =
            UPlanetLandformGenerator::GetLandformMask(
                Direction,
                Seed,
                MountainScale
            );

        const float HillMask =
            UPlanetLandformGenerator::GetHillMask(
                Direction,
                Seed,
                MountainScale
            );

        const float MountainMask =
            UPlanetLandformGenerator::GetMountainMask(
                Direction,
                Seed,
                MountainScale
            );

        const float MountainChainMask =
            UPlanetLandformGenerator::GetMountainChainMask(
                Direction,
                Seed,
                MountainScale
            );

        // ========================================================
        // TERRAIN CLASSIFICATION
        //
        // Le forme sono correlate.
        //
        // Pianura -> collina -> montagna
        //
        // Non vogliamo quattro noise indipendenti.
        // ========================================================

        const float PlainsMask =
            FMath::Clamp(
                1.0f -
                LandformMask *
                1.15f,
                0.0f,
                1.0f
            );

        const float FinalHillMask =
            HillMask *
            LandformMask *
            (1.0f - MountainMask);

        const float FinalMountainMask =
            MountainMask *
            LandformMask;

        const float FinalMountainChainMask =
            MountainChainMask *
            FinalMountainMask;

        // ========================================================
        // CONTINENTAL BASE
        //
        // Il continente ha una superficie leggermente ondulata.
        // L'oceano non viene lasciato completamente piatto.
        // ========================================================

        const float OceanBase =
            -0.105f;

        const float LandBase =
            0.055f;

        const float ContinentalBase =
            FMath::Lerp(
                OceanBase,
                LandBase,
                ContinentalMask
            );

        // ========================================================
        // MACRO LAND VARIATION
        //
        // Movimento molto lento del terreno.
        // Questo crea grandi variazioni senza rumore fine.
        // ========================================================

        const float MacroNoise =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    ContinentalScale *
                    0.55f,
                    0.0001f
                ) +
                FVector(
                    41.7f,
                    -23.4f,
                    17.9f
                )
            );

        const float MacroHeight =
            MacroNoise *
            0.055f *
            ContinentalMask;

        // ========================================================
        // PLAINS
        // ========================================================

        const float PlainsNoise =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    MountainScale *
                    0.70f,
                    0.0001f
                ) +
                FVector(
                    -17.3f,
                    36.8f,
                    12.4f
                )
            );

        const float PlainsHeight =
            PlainsNoise *
            0.018f *
            PlainsMask *
            ContinentalMask;

        // ========================================================
        // HILLS
        // ========================================================

        const float HillNoise =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    MountainScale *
                    1.05f,
                    0.0001f
                ) +
                FVector(
                    -51.2f,
                    18.7f,
                    43.6f
                )
            );

        const float HillShape =
            FMath::Pow(
                FMath::Abs(
                    HillNoise
                ),
                1.35f
            );

        const float HillHeight =
            HillShape *
            0.075f *
            FinalHillMask *
            ContinentalMask;

        // ========================================================
        // MOUNTAINS
        //
        // Il noise non decide più dove sono le montagne.
        // La MountainMask decide la posizione.
        //
        // Il noise determina la forma delle montagne.
        // ========================================================

        const uint64 BaseSeed =
            static_cast<uint64>(Seed);

        uint64 MountainSeed =
            BaseSeed +
            0x9E3779B97F4A7C15ULL;

        MountainSeed =
            (MountainSeed ^
                (MountainSeed >> 30)) *
            0xBF58476D1CE4E5B9ULL;

        MountainSeed =
            (MountainSeed ^
                (MountainSeed >> 27)) *
            0x94D049BB133111EBULL;

        MountainSeed ^=
            MountainSeed >> 31;

        const float MountainOffsetX =
            static_cast<float>(
                MountainSeed & 0xFFFF
                ) /
            65535.0f *
            200.0f -
            100.0f;

        const float MountainOffsetY =
            static_cast<float>(
                (MountainSeed >> 16) & 0xFFFF
                ) /
            65535.0f *
            200.0f -
            100.0f;

        const float MountainOffsetZ =
            static_cast<float>(
                (MountainSeed >> 32) & 0xFFFF
                ) /
            65535.0f *
            200.0f -
            100.0f;

        const FVector MountainOffset(
            MountainOffsetX,
            MountainOffsetY,
            MountainOffsetZ
        );

        const float MountainNoise =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    MountainScale *
                    1.15f,
                    0.0001f
                ) +
                MountainOffset
            );

        const float MountainShape =
            FMath::Pow(
                FMath::Max(
                    MountainNoise,
                    0.0f
                ),
                0.72f
            );

        const float MountainHeight =
            MountainShape *
            MountainStrength *
            0.75f *
            FinalMountainMask *
            ContinentalMask;

        // ========================================================
        // MOUNTAIN CHAINS
        // ========================================================

        const float ChainNoise =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    MountainScale *
                    0.82f,
                    0.0001f
                ) +
                MountainOffset *
                1.61f
            );

        const float ChainShape =
            FMath::Pow(
                FMath::Max(
                    ChainNoise,
                    0.0f
                ),
                0.58f
            );

        const float MountainChainHeight =
            ChainShape *
            MountainStrength *
            0.65f *
            FinalMountainChainMask *
            ContinentalMask;

        // ========================================================
        // MICRO DETAIL
        //
        // Molto più debole.
        // Il dettaglio non deve definire la morfologia principale.
        // ========================================================

        uint64 DetailSeed =
            BaseSeed +
            0xD1B54A32D192ED03ULL;

        DetailSeed =
            (DetailSeed ^
                (DetailSeed >> 30)) *
            0xBF58476D1CE4E5B9ULL;

        DetailSeed =
            (DetailSeed ^
                (DetailSeed >> 27)) *
            0x94D049BB133111EBULL;

        DetailSeed ^=
            DetailSeed >> 31;

        const float DetailOffsetX =
            static_cast<float>(
                DetailSeed & 0xFFFF
                ) /
            65535.0f *
            200.0f -
            100.0f;

        const float DetailOffsetY =
            static_cast<float>(
                (DetailSeed >> 16) & 0xFFFF
                ) /
            65535.0f *
            200.0f -
            100.0f;

        const float DetailOffsetZ =
            static_cast<float>(
                (DetailSeed >> 32) & 0xFFFF
                ) /
            65535.0f *
            200.0f -
            100.0f;

        const FVector DetailOffset(
            DetailOffsetX,
            DetailOffsetY,
            DetailOffsetZ
        );

        const float DetailNoise =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    DetailScale,
                    0.0001f
                ) +
                DetailOffset
            );

        const float DetailMask =
            ContinentalMask *
            (
                0.035f +
                FinalMountainMask *
                0.20f
                );

        const float DetailHeight =
            DetailNoise *
            DetailStrength *
            DetailMask;

        // ========================================================
        // FINAL HEIGHT
        // ========================================================

        return
            ContinentalBase
            +
            MacroHeight
            +
            PlainsHeight
            +
            HillHeight
            +
            MountainHeight
            +
            MountainChainHeight
            +
            DetailHeight;
    }


    // ============================================================
    // CUBE FACE DIRECTION
    // ============================================================

    FVector GetCubeFaceDirection(
        int32 FaceIndex,
        float U,
        float V
    )
    {
        const float X =
            U * 2.0f - 1.0f;

        const float Y =
            V * 2.0f - 1.0f;

        switch (FaceIndex)
        {
        case 0:
            return FVector(
                1.0f,
                Y,
                -X
            );

        case 1:
            return FVector(
                -1.0f,
                Y,
                X
            );

        case 2:
            return FVector(
                X,
                1.0f,
                -Y
            );

        case 3:
            return FVector(
                X,
                -1.0f,
                Y
            );

        case 4:
            return FVector(
                X,
                Y,
                1.0f
            );

        case 5:
            return FVector(
                X,
                -Y,
                -1.0f
            );

        default:
            return FVector::UpVector;
        }
    }
}


// ============================================================================
// PLANET HEIGHT
// ============================================================================

float UAndromedaNoiseLibrary::GeneratePlanetHeight(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength
)
{
    return GeneratePlanetHeightInternal(
        Direction,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength
    );
}


// ============================================================================
// PLANET SURFACE NORMAL
// ============================================================================

FVector UAndromedaNoiseLibrary::CalculatePlanetSurfaceNormal(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
)
{
    Direction =
        Direction.GetSafeNormal();

    const float SampleDistance =
        0.001f;

    FVector TangentA =
        FVector::CrossProduct(
            Direction,
            FVector::UpVector
        );

    if (TangentA.IsNearlyZero())
    {
        TangentA =
            FVector::CrossProduct(
                Direction,
                FVector::RightVector
            );
    }

    TangentA.Normalize();

    FVector TangentB =
        FVector::CrossProduct(
            Direction,
            TangentA
        );

    TangentB.Normalize();

    const FVector DirectionA =
        (
            Direction +
            TangentA *
            SampleDistance
            ).GetSafeNormal();

    const FVector DirectionB =
        (
            Direction +
            TangentB *
            SampleDistance
            ).GetSafeNormal();

    const float HeightCenter =
        GeneratePlanetHeightInternal(
            Direction,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        ) *
        TerrainHeight;

    const float HeightA =
        GeneratePlanetHeightInternal(
            DirectionA,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        ) *
        TerrainHeight;

    const float HeightB =
        GeneratePlanetHeightInternal(
            DirectionB,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        ) *
        TerrainHeight;

    const FVector PointCenter =
        Direction *
        (1.0f + HeightCenter);

    const FVector PointA =
        DirectionA *
        (1.0f + HeightA);

    const FVector PointB =
        DirectionB *
        (1.0f + HeightB);

    const FVector EdgeA =
        PointA -
        PointCenter;

    const FVector EdgeB =
        PointB -
        PointCenter;

    FVector Normal =
        FVector::CrossProduct(
            EdgeA,
            EdgeB
        );

    Normal.Normalize();

    if (FVector::DotProduct(
        Normal,
        Direction
    ) < 0.0f)
    {
        Normal *= -1.0f;
    }

    return Normal;
}


// ============================================================================
// PLANET SURFACE DATA
// ============================================================================

FPlanetSurfaceData UAndromedaNoiseLibrary::GetPlanetSurfaceData(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight
)
{
    FPlanetSurfaceData SurfaceData;

    Direction =
        Direction.GetSafeNormal();

    SurfaceData.Height =
        GeneratePlanetHeightInternal(
            Direction,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength
        ) *
        TerrainHeight;

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

    SurfaceData.Direction =
        Direction;

    return SurfaceData;
}


// ============================================================================
// PLANET VERTICES
// ============================================================================

void UAndromedaNoiseLibrary::GeneratePlanetVertices(
    int32 Resolution,
    float PlanetRadius,
    int32 FaceIndex,
    int64 Seed,
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

    if (FaceIndex < 0 ||
        FaceIndex > 5)
    {
        return;
    }

    const int32 VertexCount =
        (Resolution + 1) *
        (Resolution + 1);

    OutVertices.Reserve(
        VertexCount
    );

    const float Step =
        1.0f /
        static_cast<float>(Resolution);

    for (int32 Y = 0;
        Y <= Resolution;
        ++Y)
    {
        const float V =
            static_cast<float>(Y) *
            Step;

        for (int32 X = 0;
            X <= Resolution;
            ++X)
        {
            const float U =
                static_cast<float>(X) *
                Step;

            FVector Direction =
                GetCubeFaceDirection(
                    FaceIndex,
                    U,
                    V
                );

            Direction =
                Direction.GetSafeNormal();

            const float Height =
                GeneratePlanetHeightInternal(
                    Direction,
                    Seed,
                    ContinentalScale,
                    MountainScale,
                    DetailScale,
                    MountainStrength,
                    DetailStrength
                ) *
                TerrainHeight;

            OutVertices.Add(
                Direction *
                (PlanetRadius + Height)
            );
        }
    }
}


// ============================================================================
// PLANET MESH DATA
// ============================================================================

void UAndromedaNoiseLibrary::GeneratePlanetMeshData(
    int32 Resolution,
    float PlanetRadius,
    int64 Seed,
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
        (Resolution + 1) *
        (Resolution + 1);

    constexpr int32 TotalFaces = 6;

    OutVertices.Reserve(
        VerticesPerFace *
        TotalFaces
    );

    OutNormals.Reserve(
        VerticesPerFace *
        TotalFaces
    );

    OutTangents.Reserve(
        VerticesPerFace *
        TotalFaces
    );

    OutTriangles.Reserve(
        Resolution *
        Resolution *
        6 *
        TotalFaces
    );

    for (int32 FaceIndex = 0;
        FaceIndex < TotalFaces;
        ++FaceIndex)
    {
        const int32 FaceVertexStart =
            OutVertices.Num();

        TArray<FVector> FaceVertices;

        GeneratePlanetVertices(
            Resolution,
            PlanetRadius,
            FaceIndex,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength,
            TerrainHeight,
            FaceVertices
        );

        for (const FVector& Vertex :
            FaceVertices)
        {
            OutVertices.Add(Vertex);
        }

        const int32 RowSize =
            Resolution + 1;

        for (int32 Y = 0;
            Y < Resolution;
            ++Y)
        {
            for (int32 X = 0;
                X < Resolution;
                ++X)
            {
                const int32 A =
                    FaceVertexStart +
                    Y * RowSize +
                    X;

                const int32 B =
                    A + 1;

                const int32 C =
                    A + RowSize;

                const int32 D =
                    C + 1;

                OutTriangles.Add(A);
                OutTriangles.Add(C);
                OutTriangles.Add(B);

                OutTriangles.Add(B);
                OutTriangles.Add(C);
                OutTriangles.Add(D);
            }
        }

        for (int32 Y = 0;
            Y <= Resolution;
            ++Y)
        {
            const float V =
                static_cast<float>(Y) /
                static_cast<float>(Resolution);

            for (int32 X = 0;
                X <= Resolution;
                ++X)
            {
                const float U =
                    static_cast<float>(X) /
                    static_cast<float>(Resolution);

                const FVector Direction =
                    GetCubeFaceDirection(
                        FaceIndex,
                        U,
                        V
                    ).GetSafeNormal();

                OutNormals.Add(
                    CalculatePlanetSurfaceNormal(
                        Direction,
                        Seed,
                        ContinentalScale,
                        MountainScale,
                        DetailScale,
                        MountainStrength,
                        DetailStrength,
                        TerrainHeight
                    )
                );
            }
        }

        for (int32 Y = 0;
            Y <= Resolution;
            ++Y)
        {
            const float V =
                static_cast<float>(Y) /
                static_cast<float>(Resolution);

            for (int32 X = 0;
                X <= Resolution;
                ++X)
            {
                const float U =
                    static_cast<float>(X) /
                    static_cast<float>(Resolution);

                const float Delta =
                    0.001f;

                const FVector Direction =
                    GetCubeFaceDirection(
                        FaceIndex,
                        U,
                        V
                    ).GetSafeNormal();

                FVector Neighbor =
                    GetCubeFaceDirection(
                        FaceIndex,
                        FMath::Clamp(
                            U + Delta,
                            0.0f,
                            1.0f
                        ),
                        V
                    ).GetSafeNormal();

                FVector Tangent =
                    Neighbor -
                    Direction *
                    FVector::DotProduct(
                        Neighbor,
                        Direction
                    );

                if (Tangent.IsNearlyZero())
                {
                    Tangent =
                        FVector::CrossProduct(
                            FVector::UpVector,
                            Direction
                        );

                    if (Tangent.IsNearlyZero())
                    {
                        Tangent =
                            FVector::CrossProduct(
                                FVector::RightVector,
                                Direction
                            );
                    }
                }

                Tangent.Normalize();

                OutTangents.Add(
                    FProcMeshTangent(
                        Tangent,
                        false
                    )
                );
            }
        }
    }
}