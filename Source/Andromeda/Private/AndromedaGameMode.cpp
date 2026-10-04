#include "AndromedaGameMode.h"

#include "AndromedaPawn.h"
#include "AndromedaPlayerController.h"


AAndromedaGameMode::AAndromedaGameMode(
    const FObjectInitializer& ObjectInitializer
)
    : Super(ObjectInitializer)
{
    DefaultPawnClass = AAndromedaPawn::StaticClass();
    PlayerControllerClass = AAndromedaPlayerController::StaticClass();
}


void AAndromedaGameMode::BeginPlay()
{
    Super::BeginPlay();

    // CLEAN SLATE (ATMOS/ZEPHYR removal): no atmosphere registry is
    // auto-spawned. STARMAP (AStarSystem, APlanet, ASun) runs untouched;
    // the future Hillaire rebuild will add its own data path.
}
