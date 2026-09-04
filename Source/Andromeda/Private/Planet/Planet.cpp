#include "Planet/Planet.h"

#include "Planet/PlanetTerrainGenerator.h"
#include "PlanetAtmosphereComponent.h"
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


    // =========================================================
    // PLANET MATERIAL
    // =========================================================

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


    // =========================================================
    // PLANET ATMOSPHERE COMPONENT
    // =========================================================

    Atmosphere =
        CreateDefaultSubobject<UPlanetAtmosphereComponent>(
            TEXT("Atmosphere")
        );

    Atmosphere->SetupAttachment(Root);


    // =========================================================
    // PLANET ATMOSPHERE MESH
    // =========================================================

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


    // ---------------------------------------------------------
    // UAS OWNS THE ATMOSPHERE VISUALIZATION.
    //
    // The old spherical atmosphere mesh is kept alive for
    // debugging/future use, but is no longer rendered.
    // ---------------------------------------------------------

    AtmosphereMesh->SetVisibility(
        false,
        true
    );

    AtmosphereMesh->SetHiddenInGame(
        true
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
    // =========================================================
    // ATMOSPHERE PARAMETERS
    // =========================================================

    if (Atmosphere)
    {
        Atmosphere->InitializeAtmosphere(
            PlanetRadius,
            TerrainHeight,
            PlanetSeed
        );
    }


    // =========================================================
    // ATMOSPHERE MESH
    // =========================================================

    GenerateAtmosphereMesh();


    // =========================================================
    // TERRAIN
    // =========================================================

    GeneratePlanetMesh();
}


void APlanet::GenerateAtmosphereMesh()
{
    if (!AtmosphereMesh || !Atmosphere)
    {
        return;
    }


    const int32 LatitudeSegments =
        AtmosphereLatitudeSegments;

    const int32 LongitudeSegments =
        AtmosphereLongitudeSegments;


    const int32 VertexCount =
        (LatitudeSegments + 1)
        * (LongitudeSegments + 1);


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


    // =========================================================
    // VERTICES
    // =========================================================

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
                Direction
                * Atmosphere->Parameters.AtmosphereRadius
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


    // =========================================================
    // TRIANGLES
    // =========================================================

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
                Latitude * RowSize
                + Longitude;

            const int32 B =
                A + 1;

            const int32 C =
                A + RowSize;

            const int32 D =
                C + 1;


            // =================================================
            // OUTWARD WINDING
            // =================================================

            Triangles.Add(A);
            Triangles.Add(C);
            Triangles.Add(B);

            Triangles.Add(B);
            Triangles.Add(C);
            Triangles.Add(D);
        }
    }


    // =========================================================
    // CREATE MESH
    // =========================================================

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


    // =========================================================
    // APPLY ATMOSPHERE MATERIAL
    // =========================================================

    if (Atmosphere->AtmosphereMaterial)
    {
        AtmosphereMesh->SetMaterial(
            0,
            Atmosphere->AtmosphereMaterial
        );
    }


    // ---------------------------------------------------------
    // KEEP THE LEGACY MESH DISABLED.
    //
    // UAS is now responsible for the atmospheric rendering.
    // ---------------------------------------------------------

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


    // =========================================================
    // MATERIAL ALREADY ASSIGNED IN CONSTRUCTOR
    // =========================================================
    //
    // M_Planet is assigned once when the component is created.
    // No asset lookup is required every time the procedural
    // terrain is regenerated.
    //
    // This is important because GeneratePlanetMesh() can be
    // called repeatedly during construction/regeneration.
    // =========================================================
}