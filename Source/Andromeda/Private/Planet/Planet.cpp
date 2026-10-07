#include "Planet/Planet.h"

#include "HillairePlanetLinkComponent.h"
#include "LYTHOS2/Lythos2WorldSubsystem.h"
#include "PlanetaryLightingComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
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

    // LYTHOS 2.0 becomes the authoritative terrain system. The planet
    // registers with the world streamer, which seeds the coarse (LOD 0)
    // planet synchronously and streams finer LODs asynchronously based on
    // viewer distance. No legacy terrain generator is invoked.
    if (UWorld* World = GetWorld())
    {
        if (ULythos2WorldSubsystem* Lythos = World->GetSubsystem<ULythos2WorldSubsystem>())
        {
            Lythos->RegisterPlanet(this);
        }
    }
}


void APlanet::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (ULythos2WorldSubsystem* Lythos = World->GetSubsystem<ULythos2WorldSubsystem>())
        {
            Lythos->UnregisterPlanet(this);
        }
    }

    Super::EndPlay(EndPlayReason);
}


void APlanet::OnConstruction(
    const FTransform& Transform
)
{
    Super::OnConstruction(
        Transform
    );

    // Editor preview only computes the deterministic profile. The volumetric
    // terrain mesh is produced by LYTHOS 2.0 at BeginPlay (coarse seed) and by
    // the asynchronous streamer afterwards.
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
}
