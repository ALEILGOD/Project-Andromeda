#include "Planet/Planet.h"

#include "HillairePlanetLinkComponent.h"
#include "Planet/PlanetTerrainGenerator.h"
#include "PlanetaryLightingComponent.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"


APlanet::APlanet()
{
    // Tick enabled so HillairePlanetLinkComponent can push the live
    // planet position/rotation to the atmosphere feed. Use TG_PostUpdateWork
    // to run after the StarSystem has updated orbits/rotations.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

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

    PlanetaryLighting =
        CreateDefaultSubobject<UPlanetaryLightingComponent>(
            TEXT("PlanetaryLighting")
        );

    // HILLAIRE PLANET LINK (ATMOS WIRING): plain ActorComponent owned by
    // the planet Actor itself, never by PlanetProceduralMesh. Every
    // generated planet is ATMOS-ready with exactly one link.
    HillairePlanetLink =
        CreateDefaultSubobject<UHillairePlanetLinkComponent>(
            TEXT("HillairePlanetLink")
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

    GeneratePlanetMesh();
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