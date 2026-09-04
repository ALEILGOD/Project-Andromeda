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
        // Pianura -> collina -> montagna
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
            (1.0f - MountainMask * 0.70f);

        const float FinalMountainMask =
            MountainMask *
            LandformMask;

        const float FinalMountainChainMask =
            MountainChainMask *
            FinalMountainMask;

        // ========================================================
        // CONTINENTAL BASE
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
            0.045f *
            ContinentalMask;

        // ========================================================
        // CONTINENTAL UNDULATIONS (ONDULAZIONI DEL TERRENO)
        // ========================================================

        const float UndulationNoiseA =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    ContinentalScale *
                    1.65f,
                    0.0001f
                ) +
                FVector(
                    -28.4f,
                    63.1f,
                    -19.7f
                )
            );

        const float UndulationNoiseB =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    ContinentalScale *
                    3.15f,
                    0.0001f
                ) +
                FVector(
                    47.2f,
                    -15.8f,
                    32.9f
                )
            );

        const float UndulationField =
            UndulationNoiseA * 0.68f +
            UndulationNoiseB * 0.32f;

        const float UndulationHeight =
            UndulationField *
            0.020f *
            ContinentalMask;

        // ========================================================
        // PLAINS (CALME E POCO RUMOROSE)
        // ========================================================

        const float PlainsNoise =
            FMath::PerlinNoise3D(
                Direction *
                FMath::Max(
                    MountainScale *
                    0.55f,
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
            0.006f *
            PlainsMask *
            ContinentalMask;

        // ========================================================
        // HILLS (RILIEVI MORBIDI A CUPOLA, DERIVATA CONTINUA C1)
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

        const float HillBlend =
            FMath::SmoothStep(
                -0.25f,
                0.65f,
                HillNoise
            );

        const float HillShape =
            HillBlend *
            HillBlend;

        const float HillHeight =
            HillShape *
            0.045f *
            FinalHillMask *
            ContinentalMask;

        // ========================================================
        // MOUNTAINS (CRESTE RACCORDATE, NESSUNA DERIVATA INFINITA)
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

        const float MountainRidge =
            1.0f -
            FMath::Sqrt(
                MountainNoise * MountainNoise +
                0.04f
            );

        const float MountainBlend =
            FMath::SmoothStep(
                0.20f,
                0.85f,
                MountainRidge
            );

        const float MountainShape =
            MountainBlend *
            MountainBlend;

        const float MountainHeight =
            MountainShape *
            (0.22f * MountainStrength) *
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

        const float ChainRidge =
            1.0f -
            FMath::Sqrt(
                ChainNoise * ChainNoise +
                0.04f
            );

        const float ChainBlend =
            FMath::SmoothStep(
                0.25f,
                0.80f,
                ChainRidge
            );

        const float ChainShape =
            ChainBlend *
            ChainBlend;

        const float MountainChainHeight =
            ChainShape *
            (0.16f * MountainStrength) *
            FinalMountainChainMask *
            ContinentalMask;

        // ========================================================
        // MICRO DETAIL (DELICATO E LOCALIZZATO)
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
                    DetailScale * 0.75f,
                    0.0001f
                ) +
                DetailOffset
            );

        const float DetailMask =
            ContinentalMask *
            (
                0.015f +
                FinalMountainMask *
                0.12f
                );

        const float DetailHeight =
            DetailNoise *
            (DetailStrength * 0.012f) *
            DetailMask;

        // ========================================================
        // FINAL HEIGHT
        // ========================================================

        return
            ContinentalBase
            +
            MacroHeight
            +
            UndulationHeight
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
        0.005f;

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

    const float ReferencePlanetRadius =
        (TerrainHeight > 0.0f)
        ? FMath::Max(TerrainHeight * 25.0f, 250000.0f)
        : 500000.0f;

    const FVector PointCenter =
        Direction *
        (ReferencePlanetRadius + HeightCenter);

    const FVector PointA =
        DirectionA *
        (ReferencePlanetRadius + HeightA);

    const FVector PointB =
        DirectionB *
        (ReferencePlanetRadius + HeightB);

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
    }

    // ========================================================================
    // CALCOLO ACCURATO DEI NORMALI TRAMITE ACCUMULAZIONE SUI TRIANGOLI
    // ========================================================================

    const int32 TotalVertexCount =
        OutVertices.Num();

    OutNormals.Init(
        FVector::ZeroVector,
        TotalVertexCount
    );

    OutTangents.SetNumUninitialized(
        TotalVertexCount
    );

    const int32 TriangleIndexCount =
        OutTriangles.Num();

    for (int32 TriIdx = 0;
        TriIdx < TriangleIndexCount;
        TriIdx += 3)
    {
        const int32 I0 = OutTriangles[TriIdx];
        const int32 I1 = OutTriangles[TriIdx + 1];
        const int32 I2 = OutTriangles[TriIdx + 2];

        const FVector& V0 = OutVertices[I0];
        const FVector& V1 = OutVertices[I1];
        const FVector& V2 = OutVertices[I2];

        FVector TriNormal =
            FVector::CrossProduct(
                V1 - V0,
                V2 - V0
            );

        const FVector Centroid =
            (V0 + V1 + V2) * 0.3333333f;

        if (FVector::DotProduct(TriNormal, Centroid) < 0.0f)
        {
            TriNormal = -TriNormal;
        }

        OutNormals[I0] += TriNormal;
        OutNormals[I1] += TriNormal;
        OutNormals[I2] += TriNormal;
    }

    // ========================================================================
    // SEAM WELDING SUI BORDI CONDIVISI DEL CUBO-SFERA
    // ========================================================================

    const int32 RowSize =
        Resolution + 1;

    TMap<FIntVector, FVector> BoundaryNormals;
    BoundaryNormals.Reserve(TotalFaces * Resolution * 4);

    for (int32 VertIdx = 0;
        VertIdx < TotalVertexCount;
        ++VertIdx)
    {
        const int32 FaceVertIdx =
            VertIdx % VerticesPerFace;
        const int32 X =
            FaceVertIdx % RowSize;
        const int32 Y =
            FaceVertIdx / RowSize;

        if (X == 0 || X == Resolution || Y == 0 || Y == Resolution)
        {
            const FVector& Pos =
                OutVertices[VertIdx];

            const FIntVector Key(
                FMath::RoundToInt(Pos.X * 0.1f),
                FMath::RoundToInt(Pos.Y * 0.1f),
                FMath::RoundToInt(Pos.Z * 0.1f)
            );

            FVector& Sum =
                BoundaryNormals.FindOrAdd(
                    Key,
                    FVector::ZeroVector
                );

            Sum += OutNormals[VertIdx];
        }
    }

    for (int32 VertIdx = 0;
        VertIdx < TotalVertexCount;
        ++VertIdx)
    {
        const int32 FaceVertIdx =
            VertIdx % VerticesPerFace;
        const int32 X =
            FaceVertIdx % RowSize;
        const int32 Y =
            FaceVertIdx / RowSize;

        if (X == 0 || X == Resolution || Y == 0 || Y == Resolution)
        {
            const FVector& Pos =
                OutVertices[VertIdx];

            const FIntVector Key(
                FMath::RoundToInt(Pos.X * 0.1f),
                FMath::RoundToInt(Pos.Y * 0.1f),
                FMath::RoundToInt(Pos.Z * 0.1f)
            );

            if (const FVector* SharedNormal = BoundaryNormals.Find(Key))
            {
                OutNormals[VertIdx] = *SharedNormal;
            }
        }

        const FVector& Pos =
            OutVertices[VertIdx];

        const FVector RadialDir =
            Pos.GetSafeNormal();

        FVector Normal =
            OutNormals[VertIdx];

        if (Normal.IsNearlyZero())
        {
            Normal = RadialDir;
        }
        else
        {
            Normal.Normalize();

            if (FVector::DotProduct(Normal, RadialDir) < 0.0f)
            {
                Normal = -Normal;
            }
        }

        OutNormals[VertIdx] = Normal;

        // ====================================================================
        // TANGENTE SFERICA GLOBALE CONTINUA (EST PLANETARIO / INCREASING U)
        // ====================================================================

        FVector Tangent =
            FVector::CrossProduct(
                FVector::UpVector,
                Normal
            );

        if (Tangent.SizeSquared() < 0.001f)
        {
            Tangent =
                FVector::CrossProduct(
                    FVector::RightVector,
                    Normal
                );
        }

        Tangent.Normalize();

        OutTangents[VertIdx] =
            FProcMeshTangent(
                Tangent,
                true
            );
    }
}