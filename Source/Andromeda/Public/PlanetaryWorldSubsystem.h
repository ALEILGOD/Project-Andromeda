#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StarSystem.h"
#include "PlanetaryWorldSubsystem.generated.h"

class UAndromedaPawnMovement;

/** Lifecycle registry, not a second simulation. Never searches actors every frame. */
UCLASS()
class ANDROMEDA_API UPlanetaryWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    void RegisterSystem(AStarSystem* System);
    void UnregisterSystem(AStarSystem* System);
    void RegisterMovement(UAndromedaPawnMovement* Movement);
    void UnregisterMovement(UAndromedaPawnMovement* Movement);
    void SamplePlanets(double WorldTimeOffset, TArray<FPlanetRuntimeData>& OutPlanets) const;

private:
    TArray<TWeakObjectPtr<AStarSystem>> Systems;
    TArray<TWeakObjectPtr<UAndromedaPawnMovement>> Movements;
};
