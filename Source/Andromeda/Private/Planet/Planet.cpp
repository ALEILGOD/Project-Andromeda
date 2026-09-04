#include "Planet/Planet.h"

#include "Planet/PlanetTerrainGenerator.h"
#include "PlanetAtmosphereComponent.h"
#include "PlanetaryLightingComponent.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"


namespace
{
    constexpr int32 AtmosphereLatitudeSegments = 16;
    constexpr int32 AtmosphereLongitudeSegments = 32;
}


APlanet::APlanet()
{
    PrimaryActorTick.bCanEverTick = false;

    Root =
        CreateDefaultSubobject<USceneComponent>(
            TEXT("Root")
        );

    SetRootComponent(Root);

    PlanetProceduralMesh =
        CreateDefaultSubobject<UProceduralMeshComponent>(
            TEXT("PlanetProceduralMesh")
        );

    PlanetProceduralMesh->SetupAttachment(Root);

    PlanetProceduralMesh->SetCollisionEnabled(
        ECollisionEnabled::QueryAndPhysics
    );

    static ConstructorHelpers::FObjectFinder<UMaterialInterface>
        PlanetMaterialFinder(
            TEXT("/Game/Materials/M_Planet")
        );

    if (PlanetMaterialFinder.Succeeded())
    {
        PlanetProceduralMesh->SetMaterial(
            0,
            PlanetMaterialFinder.Object
        );
    }
    else
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "Planet: impossibile trovare "
                "M_Planet in /Game/Materials/M_Planet."
            )
        );
    }

    Atmosphere =
        CreateDefaultSubobject<UPlanetAtmosphereComponent>(
            TEXT("Atmosphere")
        );

    Atmosphere->SetupAttachment(Root);

    AtmosphereMesh =
        CreateDefaultSubobject<UProceduralMeshComponent>(
            TEXT("AtmosphereMesh")
        );

    AtmosphereMesh->SetupAttachment(Root);

    AtmosphereMesh->SetCollisionEnabled(
        ECollisionEnabled::NoCollision
    );

    AtmosphereMesh->SetGenerateOverlapEvents(
        false
    );

    AtmosphereMesh->SetCastShadow(
        false
    );

    AtmosphereMesh->SetVisibility(
        false,
        true
    );

    AtmosphereMesh->SetHiddenInGame(
        true
    );

    PlanetaryLighting =
        CreateDefaultSubobject<UPlanetaryLightingComponent>(
            TEXT("PlanetaryLighting")
        );
}


void APlanet::BeginPlay()
{
    Super::BeginPlay();

    InitializePlanet();
}


void APlanet::OnConstruction(
    const FTransform& Transform
)
{
    Super::OnConstruction(
        Transform
    );

    InitializePlanet();
}


void APlanet::InitializePlanet()
{
    PlanetProfile =
        UPlanetProfileGenerator::GenerateProfile(
            PlanetSeed,
            PlanetID,
            OrbitDistance
        );

    PlanetArchetype =
        PlanetProfile.Archetype;

    if (Atmosphere)
    {
        Atmosphere->InitializeAtmosphere(
            PlanetRadius,
            TerrainHeight,
            PlanetSeed
        );
    }

    GenerateAtmosphereMesh();
    GeneratePlanetMesh();
}


void APlanet::GenerateAtmosphereMesh()
{
    if (!AtmosphereMesh ||
        !Atmosphere)
    {
        return;
    }

    const int32 LatitudeSegments =
        AtmosphereLatitudeSegments;

    const int32 LongitudeSegments =
        AtmosphereLongitudeSegments;

    const int32 VertexCount =
        (LatitudeSegments + 1) *
        (LongitudeSegments + 1);

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Reserve(VertexCount);
    Normals.Reserve(VertexCount);
    UVs.Reserve(VertexCount);
    VertexColors.Reserve(VertexCount);
    Tangents.Reserve(VertexCount);

    for (int32 Latitude = 0;
        Latitude <= LatitudeSegments;
        ++Latitude)
    {
        const float V =
            static_cast<float>(Latitude)
            / static_cast<float>(LatitudeSegments);

        const float Theta =
            V * PI;

        const float SinTheta =
            FMath::Sin(Theta);

        const float CosTheta =
            FMath::Cos(Theta);

        for (int32 Longitude = 0;
            Longitude <= LongitudeSegments;
            ++Longitude)
        {
            const float U =
                static_cast<float>(Longitude)
                / static_cast<float>(LongitudeSegments);

            const float Phi =
                U * 2.0f * PI;

            const float SinPhi =
                FMath::Sin(Phi);

            const float CosPhi =
                FMath::Cos(Phi);

            const FVector Direction(
                SinTheta * CosPhi,
                SinTheta * SinPhi,
                CosTheta
            );

            Vertices.Add(
                Direction *
                Atmosphere->Parameters.AtmosphereRadius
            );

            Normals.Add(
                Direction
            );

            UVs.Add(
                FVector2D(U, V)
            );

            VertexColors.Add(
                FColor::White
            );

            Tangents.Add(
                FProcMeshTangent(
                    -SinPhi,
                    CosPhi,
                    0.0f
                )
            );
        }
    }

    const int32 RowSize =
        LongitudeSegments + 1;

    for (int32 Latitude = 0;
        Latitude < LatitudeSegments;
        ++Latitude)
    {
        for (int32 Longitude = 0;
            Longitude < LongitudeSegments;
            ++Longitude)
        {
            const int32 A =
                Latitude *
                RowSize +
                Longitude;

            const int32 B =
                A + 1;

            const int32 C =
                A +
                RowSize;

            const int32 D =
                C + 1;

            Triangles.Add(A);
            Triangles.Add(C);
            Triangles.Add(B);

            Triangles.Add(B);
            Triangles.Add(C);
            Triangles.Add(D);
        }
    }

    AtmosphereMesh->ClearAllMeshSections();

    AtmosphereMesh->CreateMeshSection(
        0,
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        false
    );

    if (Atmosphere->AtmosphereMaterial)
    {
        AtmosphereMesh->SetMaterial(
            0,
            Atmosphere->AtmosphereMaterial
        );
    }

    AtmosphereMesh->SetVisibility(
        false,
        true
    );

    AtmosphereMesh->SetHiddenInGame(
        true
    );
}


void APlanet::GeneratePlanetMesh()
{
    if (!PlanetProceduralMesh)
    {
        return;
    }

    if (!TerrainGenerator)
    {
        TerrainGenerator =
            NewObject<UPlanetTerrainGenerator>(
                this,
                UPlanetTerrainGenerator::StaticClass()
            );
    }

    if (!TerrainGenerator)
    {
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FProcMeshTangent> Tangents;
    TArray<FColor> VertexColors;

    TerrainGenerator->GenerateTerrainMeshData(
        Resolution,
        PlanetRadius,
        PlanetSeed,
        ContinentalScale,
        MountainScale,
        DetailScale,
        MountainStrength,
        DetailStrength,
        TerrainHeight,
        PlanetProfile,
        Vertices,
        Triangles,
        Normals,
        Tangents,
        VertexColors
    );

    TArray<FVector2D> UVs;

    UVs.Reserve(
        Vertices.Num()
    );

    for (const FVector& Vertex :
        Vertices)
    {
        const FVector Direction =
            Vertex.GetSafeNormal();

        const float U =
            0.5f +
            FMath::Atan2(
                Direction.Y,
                Direction.X
            ) /
            (2.0f * PI);

        const float VCoord =
            0.5f -
            FMath::Asin(
                FMath::Clamp(
                    Direction.Z,
                    -1.0f,
                    1.0f
                )
            ) /
            PI;

        UVs.Add(
            FVector2D(
                U,
                VCoord
            )
        );
    }

    PlanetProceduralMesh->ClearAllMeshSections();

    PlanetProceduralMesh->CreateMeshSection(
        0,
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        true
    );
}