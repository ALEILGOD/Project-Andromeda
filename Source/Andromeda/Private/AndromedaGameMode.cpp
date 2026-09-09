#include "AndromedaGameMode.h"

#include "AndromedaPawn.h"
#include "Atmosphere/AndromedaAtmosphereRegistry.h"
#include "Engine/World.h"
#include "EngineUtils.h"


AAndromedaGameMode::AAndromedaGameMode(
    const FObjectInitializer& ObjectInitializer
)
    : Super(ObjectInitializer)
{
    // Il Default Pawn del progetto e' AAndromedaPawn: mantiene il movimento
    // del DefaultPawn di Unreal e aggiunge la gravita' planetaria.
    DefaultPawnClass = AAndromedaPawn::StaticClass();
}


void AAndromedaGameMode::BeginPlay()
{
    Super::BeginPlay();


    // =========================================================
    // ATMOS-03: ensure a single AAndromedaAtmosphereRegistry exists.
    // =========================================================
    // The registry finds the AStarSystem in the world and registers one
    // atmosphere per planet, then keeps their world positions in sync every
    // frame (see AndromedaAtmosphereRegistry.cpp). Auto-spawn it here only
    // if the level does not already contain one, to avoid duplicates.
    UWorld* World = GetWorld();

    if (!World)
    {
        return;
    }


    bool bRegistryAlreadyPresent = false;

    for (TActorIterator<AAndromedaAtmosphereRegistry> It(World); It; ++It)
    {
        bRegistryAlreadyPresent = true;

        break;
    }


    if (bRegistryAlreadyPresent)
    {
        return;
    }


    const FTransform SpawnTransform(
        FRotator::ZeroRotator,
        FVector::ZeroVector,
        FVector::OneVector
    );

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;


    AAndromedaAtmosphereRegistry* Registry =
        World->SpawnActor<AAndromedaAtmosphereRegistry>(
            AAndromedaAtmosphereRegistry::StaticClass(),
            SpawnTransform,
            SpawnParameters
        );


    if (Registry)
    {
        UE_LOG(
            LogAndromedaAtmos,
            Log,
            TEXT("AAndromedaGameMode: auto-spawned an AAndromedaAtmosphereRegistry (none present in the level).")
        );
    }
    else
    {
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("AAndromedaGameMode: failed to spawn AAndromedaAtmosphereRegistry.")
        );
    }
}