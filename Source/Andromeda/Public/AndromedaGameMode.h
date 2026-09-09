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


protected:

    // Automatically spawns an AAndromedaAtmosphereRegistry if the world does
    // not contain one yet (ATMOS-03). The registry connects the StarSystem
    // planets to the atmosphere manager and keeps atmosphere positions in
    // sync every frame.
    virtual void BeginPlay() override;
};