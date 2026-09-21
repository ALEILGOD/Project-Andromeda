#include "AndromedaGameMode.h"

#include "AndromedaPawn.h"


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

    // CLEAN SLATE (ATMOS/ZEPHYR removal): no atmosphere registry is
    // auto-spawned. STARMAP (AStarSystem, APlanet, ASun) runs untouched;
    // the future Hillaire rebuild will add its own data path.
}
