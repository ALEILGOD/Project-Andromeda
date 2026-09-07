#include "CoreMinimal.h"
#include "AndromedaNoiseLibrary.h"
#include "Planet/PlanetBiomeGenerator.h"
#include "Planet/PlanetContinentalGenerator.h"
#include "Planet/PlanetLandformGenerator.h"
#include <atomic>

namespace
{
    float GeneratePlanetHeightInternal(
        const FVector& Direction,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength)
    {
        const FVector SafeDirection = Direction.GetSafeNormal();

        const float ContinentalMask =
            UPlanetContinentalGenerator::GetContinentalMask(
                SafeDirection,
                Seed,
                ContinentalScale);

        const float LandformMask =
            UPlanetLandformGenerator::GetLandformMask(
                SafeDirection,
                Seed,
                MountainScale);

        const float HillMask =
            UPlanetLandformGenerator::GetHillMask(
                SafeDirection,
                Seed,
                MountainScale);

        const float MountainMask =
            UPlanetLandformGenerator::GetMountainMask(
                SafeDirection,
                Seed,
                MountainScale);

        const float MountainChainMask =
            UPlanetLandformGenerator::GetMountainChainMask(
                SafeDirection,
                Seed,
                MountainScale);

        const float PlainsMask =
            FMath::Clamp(
                1.0f - LandformMask * 1.15f,
                0.0f,
                1.0f);

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

        constexpr float OceanBase = -0.105f;
        constexpr float LandBase = 0.055f;

        const float ContinentalBase =
            FMath::Lerp(
                OceanBase,
                LandBase,
                ContinentalMask);

        const float MacroNoise =
            FMath::PerlinNoise3D(
                SafeDirection * (ContinentalScale * 0.55f) +
                FVector(41.7f, -23.4f, 17.9f));

        const float MacroHeight =
            MacroNoise *
            0.045f *
            ContinentalMask;

        const float UndulationNoiseA =
            FMath::PerlinNoise3D(
                SafeDirection * (ContinentalScale * 1.65f) +
                FVector(-28.4f, 63.1f, -19.7f));

        const float UndulationNoiseB =
            FMath::PerlinNoise3D(
                SafeDirection * (ContinentalScale * 3.15f) +
                FVector(47.2f, -15.8f, 32.9f));

        const float UndulationNoise =
            UndulationNoiseA * 0.68f +
            UndulationNoiseB * 0.32f;

        const float UndulationHeight =
            UndulationNoise *
            0.020f *
            ContinentalMask;

        const float PlainsNoise =
            FMath::PerlinNoise3D(
                SafeDirection * (MountainScale * 0.55f) +
                FVector(-17.3f, 36.8f, 12.4f));

        const float PlainsHeight =
            PlainsNoise *
            0.006f *
            PlainsMask *
            ContinentalMask;

        const float HillsNoise =
            FMath::PerlinNoise3D(
                SafeDirection * (MountainScale * 1.05f) +
                FVector(-51.2f, 18.7f, 43.6f));

        const float HillsShape =
            FMath::Square(
                FMath::SmoothStep(
                    -0.25f,
                    0.65f,
                    HillsNoise));

        const float HillsHeight =
            HillsShape *
            0.045f *
            FinalHillMask *
            ContinentalMask;

        const uint64 MountainSeed =
            static_cast<uint64>(Seed) ^
            0x9E3779B97F4A7C15ULL;

        uint64 MountainHash = MountainSeed;

        MountainHash ^= MountainHash >> 30;
        MountainHash *= 0xBF58476D1CE4E5B9ULL;
        MountainHash ^= MountainHash >> 27;
        MountainHash *= 0x94D049BB133111EBULL;
        MountainHash ^= MountainHash >> 31;

        const FVector MountainOffset(
            static_cast<float>(MountainHash & 0xFFFF) / 65535.0f * 200.0f - 100.0f,
            static_cast<float>((MountainHash >> 16) & 0xFFFF) / 65535.0f * 200.0f - 100.0f,
            static_cast<float>((MountainHash >> 32) & 0xFFFF) / 65535.0f * 200.0f - 100.0f);

        const float MountainNoise =
            FMath::PerlinNoise3D(
                SafeDirection * (MountainScale * 1.15f) +
                MountainOffset);

        const float MountainRidge =
            1.0f -
            FMath::Sqrt(
                MountainNoise * MountainNoise +
                0.04f);

        const float MountainShape =
            FMath::Square(
                FMath::SmoothStep(
                    0.20f,
                    0.85f,
                    MountainRidge));

        const float MountainHeight =
            MountainShape *
            0.22f *
            MountainStrength *
            FinalMountainMask *
            ContinentalMask;

        const float ChainNoise =
            FMath::PerlinNoise3D(
                SafeDirection * (MountainScale * 0.82f) +
                MountainOffset * 1.61f);

        const float ChainRidge =
            1.0f -
            FMath::Sqrt(
                ChainNoise * ChainNoise +
                0.04f);

        const float ChainShape =
            FMath::Square(
                FMath::SmoothStep(
                    0.25f,
                    0.80f,
                    ChainRidge));

        const float MountainChainHeight =
            ChainShape *
            0.16f *
            MountainStrength *
            FinalMountainChainMask *
            ContinentalMask;

        const uint64 DetailSeed =
            static_cast<uint64>(Seed) ^
            0xD1B54A32D192ED03ULL;

        uint64 DetailHash = DetailSeed;

        DetailHash ^= DetailHash >> 30;
        DetailHash *= 0xBF58476D1CE4E5B9ULL;
        DetailHash ^= DetailHash >> 27;
        DetailHash *= 0x94D049BB133111EBULL;
        DetailHash ^= DetailHash >> 31;

        const FVector DetailOffset(
            static_cast<float>(DetailHash & 0xFFFF) / 65535.0f * 200.0f - 100.0f,
            static_cast<float>((DetailHash >> 16) & 0xFFFF) / 65535.0f * 200.0f - 100.0f,
            static_cast<float>((DetailHash >> 32) & 0xFFFF) / 65535.0f * 200.0f - 100.0f);

        const float DetailNoise =
            FMath::PerlinNoise3D(
                SafeDirection * (DetailScale * 0.75f) +
                DetailOffset);

        const float DetailMask =
            ContinentalMask *
            (0.015f + FinalMountainMask * 0.12f);

        const float DetailHeight =
            DetailNoise *
            (DetailStrength * 0.012f) *
            DetailMask;

        return
            ContinentalBase +
            MacroHeight +
            UndulationHeight +
            PlainsHeight +
            HillsHeight +
            MountainHeight +
            MountainChainHeight +
            DetailHeight;
    }

    FVector GetCubeFaceDirection(
        int32 FaceIndex,
        float U,
        float V)
    {
        const float X = U * 2.0f - 1.0f;
        const float Y = V * 2.0f - 1.0f;

        switch (FaceIndex)
        {
        case 0:
            return FVector(1.0f, Y, -X);

        case 1:
            return FVector(-1.0f, Y, X);

        case 2:
            return FVector(X, 1.0f, -Y);

        case 3:
            return FVector(X, -1.0f, Y);

        case 4:
            return FVector(X, Y, 1.0f);

        case 5:
            return FVector(X, -Y, -1.0f);

        default:
            return FVector::UpVector;
        }
    }

    FVector3f GetBiomeCode(
        EPlanetBiome Biome)
    {
        switch (Biome)
        {
        case EPlanetBiome::Ocean:
            return FVector3f(0.93f, 0.07f, 0.07f);

        case EPlanetBiome::Beach:
            return FVector3f(0.07f, 0.93f, 0.07f);

        case EPlanetBiome::Plains:
            return FVector3f(0.07f, 0.07f, 0.93f);

        case EPlanetBiome::Grassland:
            return FVector3f(0.93f, 0.93f, 0.07f);

        case EPlanetBiome::Forest:
            return FVector3f(0.93f, 0.07f, 0.93f);

        case EPlanetBiome::Desert:
            return FVector3f(0.07f, 0.93f, 0.93f);

        case EPlanetBiome::Tundra:
            return FVector3f(0.55f, 0.25f, 0.85f);

        case EPlanetBiome::Snow:
            return FVector3f(0.25f, 0.85f, 0.55f);

        case EPlanetBiome::Mountain:
            return FVector3f(0.85f, 0.55f, 0.25f);

        default:
            return FVector3f(0.93f, 0.07f, 0.07f);
        }
    }

    FColor BiomeToVertexColor(
        EPlanetBiome Biome)
    {
        const FVector3f Code = GetBiomeCode(Biome);

        return FColor(
            static_cast<uint8>(FMath::RoundToInt(Code.X * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(Code.Y * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(Code.Z * 255.0f)),
            255);
    }

    EPlanetBiome CalculateVertexBiome(
        const FVector& Direction,
        float NormalizedHeight,
        const FVector& SurfaceNormal,
        int64 Seed,
        const FPlanetProfile& PlanetProfile)
    {
        const FPlanetBiomeData BiomeData =
            UPlanetBiomeGenerator::CalculateBiomeWithProfile(
                Direction,
                NormalizedHeight,
                SurfaceNormal,
                Seed,
                PlanetProfile);

        return BiomeData.PrimaryBiome;
    }

    EPlanetBiome CalculateTriangleBiome(
        const FVector& PositionA,
        const FVector& PositionB,
        const FVector& PositionC,
        float PlanetRadius,
        int64 Seed,
        float ContinentalScale,
        float MountainScale,
        float DetailScale,
        float MountainStrength,
        float DetailStrength,
        float TerrainHeight,
        const FPlanetProfile& PlanetProfile)
    {
        // Centro geometrico reale del triangolo sulla mesh effettiva.
        // (Usando le POSIZIONI dei vertici della mesh, non una ri-evaluazione
        //  del rumore con scale forzate.) Il bioma segue cosi' esattamente
        //  la superficie che l'utente vede: niente mismatch con la geometria.
        const FVector CenterPoint =
            (PositionA + PositionB + PositionC) / 3.0f;

        const FVector TriangleDirection =
            CenterPoint.GetSafeNormal();

        // Altezza normalizzata reale del centro del triangolo, derivata
        // direttamente dalla geometria della mesh (raggio effettivo).
        const float NormalizedHeight =
            TerrainHeight > KINDA_SMALL_NUMBER
                ? FMath::Clamp(
                    (CenterPoint.Size() - PlanetRadius) / TerrainHeight,
                    -1.0f,
                    1.0f)
                : 0.0f;

        // Normale della superficie calcolata con gli STESSI parametri reali
        // usati dalla mesh (Continental/Mountain/DetailScale/Strength e
        // TerrainHeight). In questo modo la pendenza (e quindi la classificazione
        // Mountain) corrisponde al terreno mostrato.
        const FVector SurfaceNormal =
            UAndromedaNoiseLibrary::CalculatePlanetSurfaceNormal(
                TriangleDirection,
                Seed,
                ContinentalScale,
                MountainScale,
                DetailScale,
                MountainStrength,
                DetailStrength,
                TerrainHeight);

        return CalculateVertexBiome(
            TriangleDirection,
            NormalizedHeight,
            SurfaceNormal,
            Seed,
            PlanetProfile);
    }
}

float UAndromedaNoiseLibrary::GeneratePlanetHeight(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength)
{
    return GeneratePlanetHeightInternal(
        Direction,
        Seed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength);
}

FVector UAndromedaNoiseLibrary::CalculatePlanetSurfaceNormal(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight)
{
    Direction = Direction.GetSafeNormal();

    const float SampleDistance = 0.005f;

    FVector TangentA =
        FVector::CrossProduct(
            Direction,
            FVector::UpVector);

    if (TangentA.IsNearlyZero())
    {
        TangentA =
            FVector::CrossProduct(
                Direction,
                FVector::RightVector);
    }

    TangentA.Normalize();

    FVector TangentB =
        FVector::CrossProduct(
            Direction,
            TangentA);

    TangentB.Normalize();

    const FVector DirectionA =
        (Direction + TangentA * SampleDistance).GetSafeNormal();

    const FVector DirectionB =
        (Direction + TangentB * SampleDistance).GetSafeNormal();

    const float HeightCenter =
        GeneratePlanetHeightInternal(
            Direction,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength) *
        TerrainHeight;

    const float HeightA =
        GeneratePlanetHeightInternal(
            DirectionA,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength) *
        TerrainHeight;

    const float HeightB =
        GeneratePlanetHeightInternal(
            DirectionB,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength) *
        TerrainHeight;

    const float ReferencePlanetRadius =
        TerrainHeight > 0.0f
            ? FMath::Max(
                TerrainHeight * 25.0f,
                250000.0f)
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
        PointA - PointCenter;

    const FVector EdgeB =
        PointB - PointCenter;

    FVector Normal =
        FVector::CrossProduct(
            EdgeA,
            EdgeB);

    Normal.Normalize();

    if (FVector::DotProduct(Normal, Direction) < 0.0f)
    {
        Normal *= -1.0f;
    }

    return Normal;
}

FPlanetSurfaceData UAndromedaNoiseLibrary::GetPlanetSurfaceData(
    FVector Direction,
    int64 Seed,
    float ContinentalScale,
    float MountainScale,
    float DetailScale,
    float MountainStrength,
    float DetailStrength,
    float TerrainHeight)
{
    Direction = Direction.GetSafeNormal();

    FPlanetSurfaceData SurfaceData;

    SurfaceData.Height =
        GeneratePlanetHeightInternal(
            Direction,
            Seed,
            ContinentalScale,
            MountainScale,
            DetailScale,
            MountainStrength,
            DetailStrength) *
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
            TerrainHeight);

    SurfaceData.Direction = Direction;

    return SurfaceData;
}

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
    TArray<FVector>& OutVertices)
{
    OutVertices.Reset();

    if (Resolution < 2)
    {
        return;
    }

    if (FaceIndex < 0 || FaceIndex > 5)
    {
        return;
    }

    const int32 VertexCount =
        (Resolution + 1) *
        (Resolution + 1);

    OutVertices.Reserve(VertexCount);

    for (int32 Y = 0; Y <= Resolution; ++Y)
    {
        const float V =
            static_cast<float>(Y) /
            static_cast<float>(Resolution);

        for (int32 X = 0; X <= Resolution; ++X)
        {
            const float U =
                static_cast<float>(X) /
                static_cast<float>(Resolution);

            const FVector Direction =
                GetCubeFaceDirection(
                    FaceIndex,
                    U,
                    V).GetSafeNormal();

            const float Height =
                GeneratePlanetHeightInternal(
                    Direction,
                    Seed,
                    ContinentalScale,
                    MountainScale,
                    DetailScale,
                    MountainStrength,
                    DetailStrength) *
                TerrainHeight;

            OutVertices.Add(
                Direction *
                (PlanetRadius + Height));
        }
    }
}

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
    const FPlanetProfile& PlanetProfile,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals,
    TArray<FProcMeshTangent>& OutTangents,
    TArray<FColor>& OutVertexColors)
{
    OutVertices.Reset();
    OutTriangles.Reset();
    OutNormals.Reset();
    OutTangents.Reset();
    OutVertexColors.Reset();

    if (Resolution < 2)
    {
        return;
    }

    const int32 VerticesPerFace =
        (Resolution + 1) *
        (Resolution + 1);

    const int32 TotalFaces = 6;

    TArray<FVector> BaseVertices;
    TArray<int32> BaseTriangles;
    TArray<FVector> BaseNormals;
    TArray<EPlanetBiome> BaseBiomes;

    BaseVertices.Reserve(
        VerticesPerFace *
        TotalFaces);

    BaseTriangles.Reserve(
        Resolution *
        Resolution *
        TotalFaces *
        6);

    BaseNormals.SetNumZeroed(
        VerticesPerFace *
        TotalFaces);

    BaseBiomes.Reserve(
        VerticesPerFace *
        TotalFaces);

    // ============================================================
    // 1. GENERATE THE ORIGINAL GRID VERTICES
    // ============================================================

    for (int32 FaceIndex = 0;
         FaceIndex < TotalFaces;
         ++FaceIndex)
    {
        const int32 FaceVertexStart =
            BaseVertices.Num();

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
            FaceVertices);

        for (const FVector& Vertex : FaceVertices)
        {
            BaseVertices.Add(Vertex);

            const FVector Direction =
                Vertex.GetSafeNormal();

            const float RawHeight =
                Direction.IsNearlyZero()
                    ? 0.0f
                    : Vertex.Size() - PlanetRadius;

            const float NormalizedHeight =
                TerrainHeight > KINDA_SMALL_NUMBER
                    ? FMath::Clamp(
                        RawHeight / TerrainHeight,
                        -1.0f,
                        1.0f)
                    : 0.0f;

            const FVector SurfaceNormal =
                CalculatePlanetSurfaceNormal(
                    Direction,
                    Seed,
                    ContinentalScale,
                    MountainScale,
                    DetailScale,
                    MountainStrength,
                    DetailStrength,
                    TerrainHeight);

            const EPlanetBiome Biome =
                CalculateVertexBiome(
                    Direction,
                    NormalizedHeight,
                    SurfaceNormal,
                    Seed,
                    PlanetProfile);

            BaseBiomes.Add(Biome);
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

                BaseTriangles.Add(A);
                BaseTriangles.Add(C);
                BaseTriangles.Add(B);

                BaseTriangles.Add(B);
                BaseTriangles.Add(C);
                BaseTriangles.Add(D);
            }
        }
    }

    // ============================================================
    // 2. CALCULATE SMOOTH BASE NORMALS
    // ============================================================

    for (int32 TriangleIndex = 0;
         TriangleIndex < BaseTriangles.Num();
         TriangleIndex += 3)
    {
        const int32 I0 =
            BaseTriangles[TriangleIndex];

        const int32 I1 =
            BaseTriangles[TriangleIndex + 1];

        const int32 I2 =
            BaseTriangles[TriangleIndex + 2];

        const FVector& V0 =
            BaseVertices[I0];

        const FVector& V1 =
            BaseVertices[I1];

        const FVector& V2 =
            BaseVertices[I2];

        FVector TriangleNormal =
            FVector::CrossProduct(
                V1 - V0,
                V2 - V0);

        const FVector Centroid =
            (V0 + V1 + V2) / 3.0f;

        if (FVector::DotProduct(
                TriangleNormal,
                Centroid) < 0.0f)
        {
            TriangleNormal *= -1.0f;
        }

        BaseNormals[I0] += TriangleNormal;
        BaseNormals[I1] += TriangleNormal;
        BaseNormals[I2] += TriangleNormal;
    }

    // ============================================================
    // 3. WELD BOUNDARY NORMALS
    // ============================================================

    const int32 RowSize =
        Resolution + 1;

    TMap<FIntVector, FVector> BoundaryNormals;

    auto MakeBoundaryKey =
        [](const FVector& Position)
        {
            return FIntVector(
                FMath::RoundToInt(Position.X * 0.1f),
                FMath::RoundToInt(Position.Y * 0.1f),
                FMath::RoundToInt(Position.Z * 0.1f));
        };

    for (int32 FaceIndex = 0;
         FaceIndex < TotalFaces;
         ++FaceIndex)
    {
        const int32 FaceStart =
            FaceIndex *
            VerticesPerFace;

        for (int32 Y = 0;
             Y <= Resolution;
             ++Y)
        {
            for (int32 X = 0;
                 X <= Resolution;
                 ++X)
            {
                if (X != 0 &&
                    X != Resolution &&
                    Y != 0 &&
                    Y != Resolution)
                {
                    continue;
                }

                const int32 Index =
                    FaceStart +
                    Y * RowSize +
                    X;

                const FIntVector Key =
                    MakeBoundaryKey(
                        BaseVertices[Index]);

                BoundaryNormals.FindOrAdd(Key) +=
                    BaseNormals[Index];
            }
        }
    }

    for (int32 FaceIndex = 0;
         FaceIndex < TotalFaces;
         ++FaceIndex)
    {
        const int32 FaceStart =
            FaceIndex *
            VerticesPerFace;

        for (int32 Y = 0;
             Y <= Resolution;
             ++Y)
        {
            for (int32 X = 0;
                 X <= Resolution;
                 ++X)
            {
                if (X != 0 &&
                    X != Resolution &&
                    Y != 0 &&
                    Y != Resolution)
                {
                    continue;
                }

                const int32 Index =
                    FaceStart +
                    Y * RowSize +
                    X;

                const FIntVector Key =
                    MakeBoundaryKey(
                        BaseVertices[Index]);

                if (const FVector* SharedNormal =
                        BoundaryNormals.Find(Key))
                {
                    BaseNormals[Index] =
                        *SharedNormal;
                }
            }
        }
    }

    for (FVector& Normal : BaseNormals)
    {
        Normal.Normalize();

        if (Normal.IsNearlyZero())
        {
            Normal =
                BaseVertices[
                    &Normal -
                    BaseNormals.GetData()
                ].GetSafeNormal();
        }

        const int32 NormalIndex =
            static_cast<int32>(
                &Normal -
                BaseNormals.GetData());

        const FVector Radial =
            BaseVertices[NormalIndex]
                .GetSafeNormal();

        if (FVector::DotProduct(
                Normal,
                Radial) < 0.0f)
        {
            Normal *= -1.0f;
        }
    }

    // ============================================================
    // 4. EXPAND TRIANGLES
    //
    // IMPORTANT:
    // Every triangle gets ONE categorical biome code on all
    // three of its vertices.
    //
    // Therefore Vertex Color interpolation can never generate
    // fake intermediate biome IDs inside a triangle.
    // ============================================================

    const int32 TriangleCount =
        BaseTriangles.Num() / 3;

    OutVertices.Reserve(
        TriangleCount * 3);

    OutTriangles.Reserve(
        TriangleCount * 3);

    OutNormals.Reserve(
        TriangleCount * 3);

    OutTangents.Reserve(
        TriangleCount * 3);

    OutVertexColors.Reserve(
        TriangleCount * 3);

    for (int32 TriangleIndex = 0;
         TriangleIndex < BaseTriangles.Num();
         TriangleIndex += 3)
    {
        const int32 I0 =
            BaseTriangles[TriangleIndex];

        const int32 I1 =
            BaseTriangles[TriangleIndex + 1];

        const int32 I2 =
            BaseTriangles[TriangleIndex + 2];

        const FVector& V0 =
            BaseVertices[I0];

        const FVector& V1 =
            BaseVertices[I1];

        const FVector& V2 =
            BaseVertices[I2];

        // Evaluate the biome at the real triangle center (geometry coherent).
        const EPlanetBiome TriangleBiome =
            CalculateTriangleBiome(
                V0,
                V1,
                V2,
                PlanetRadius,
                Seed,
                ContinentalScale,
                MountainScale,
                DetailScale,
                MountainStrength,
                DetailStrength,
                TerrainHeight,
                PlanetProfile);

        const FColor TriangleColor =
            BiomeToVertexColor(
                TriangleBiome);

        const int32 NewIndex =
            OutVertices.Num();

        OutVertices.Add(V0);
        OutVertices.Add(V1);
        OutVertices.Add(V2);

        OutNormals.Add(
            BaseNormals[I0]);

        OutNormals.Add(
            BaseNormals[I1]);

        OutNormals.Add(
            BaseNormals[I2]);

        OutVertexColors.Add(
            TriangleColor);

        OutVertexColors.Add(
            TriangleColor);

        OutVertexColors.Add(
            TriangleColor);

        OutTriangles.Add(
            NewIndex);

        OutTriangles.Add(
            NewIndex + 1);

        OutTriangles.Add(
            NewIndex + 2);
    }

    // ============================================================
    // 5. GENERATE TANGENTS
    // ============================================================

    for (int32 Index = 0;
         Index < OutNormals.Num();
         ++Index)
    {
        FVector Normal =
            OutNormals[Index].GetSafeNormal();

        FVector Tangent =
            FVector::CrossProduct(
                FVector::UpVector,
                Normal);

        if (Tangent.IsNearlyZero())
        {
            Tangent =
                FVector::CrossProduct(
                    FVector::RightVector,
                    Normal);
        }

        Tangent.Normalize();

        OutTangents.Add(
            FProcMeshTangent(
                Tangent,
                true));
    }
}