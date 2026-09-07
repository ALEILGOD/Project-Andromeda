#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AndromedaGameMode.generated.h"


// =========================================================
// ANDROMEDA GAME MODE
//
// GameMode del progetto Andromeda. Deriva da AGameModeBase
// (la GameMode di default di Unreal) e imposta AAndromedaPawn
// come pawn di default per i giocatori.
//
// La selezione avviene tramite Project Settings -> Maps & Modes ->
// Default Game Mode (GlobalDefaultGameMode in DefaultEngine.ini),
// quindi durante Play Unreal verra' spawnato AAndromedaPawn
// al posto del DefaultPawn standard.
// =========================================================

UCLASS(notplaceable, Blueprintable, BlueprintType)
class ANDROMEDA_API AAndromedaGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:

    AAndromedaGameMode(const FObjectInitializer& ObjectInitializer);
};