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