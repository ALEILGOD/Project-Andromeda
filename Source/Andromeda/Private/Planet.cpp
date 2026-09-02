#include "Planet.h"

#include "PlanetTerrainGenerator.h"


APlanet::APlanet()
{
    PrimaryActorTick.bCanEverTick = false;


    // =========================================================
    // ROOT
    // =========================================================

    Root =
        CreateDefaultSubobject<USceneComponent>(
            TEXT("Root")
        );

    SetRootComponent(Root);


    // =========================================================
    // PLANET PROCEDURAL MESH
    // =========================================================

    PlanetProceduralMesh =
        CreateDefaultSubobject<UProceduralMeshComponent>(
            TEXT("PlanetProceduralMesh")
        );

    PlanetProceduralMesh->SetupAttachment(Root);

    PlanetProceduralMesh->SetCollisionEnabled(
        ECollisionEnabled::QueryAndPhysics
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
    Super::OnConstruction(Transform);

    InitializePlanet();
}


void APlanet::InitializePlanet()
{
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


    // =========================================================
    // GENERATED DATA
    // =========================================================

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FProcMeshTangent> Tangents;


    // =========================================================
    // GENERATE TERRAIN
    // =========================================================

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
        Vertices,
        Triangles,
        Normals,
        Tangents
    );


    // =========================================================
    // CREATE MESH
    // =========================================================

    PlanetProceduralMesh->ClearAllMeshSections();

    PlanetProceduralMesh->CreateMeshSection(
        0,
        Vertices,
        Triangles,
        Normals,
        TArray<FVector2D>(),
        TArray<FColor>(),
        Tangents,
        true
    );
}
