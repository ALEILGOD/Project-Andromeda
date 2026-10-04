#include "PlanetaryWorldSubsystem.h"
#include "AndromedaPawnMovement.h"

void UPlanetaryWorldSubsystem::RegisterSystem(AStarSystem* System)
{
    Systems.AddUnique(System);
    for (const auto& Entry : Movements)
    {
        if (UAndromedaPawnMovement* Movement = Entry.Get())
        {
            Movement->AddTickPrerequisiteActor(System);
        }
    }
}

void UPlanetaryWorldSubsystem::UnregisterSystem(AStarSystem* System)
{
    for (const auto& Entry : Movements)
    {
        if (UAndromedaPawnMovement* Movement = Entry.Get())
        {
            Movement->RemoveTickPrerequisiteActor(System);
        }
    }
    Systems.Remove(System);
}

void UPlanetaryWorldSubsystem::RegisterMovement(UAndromedaPawnMovement* Movement)
{
    Movements.AddUnique(Movement);
    for (const auto& Entry : Systems)
    {
        if (AStarSystem* System = Entry.Get())
        {
            Movement->AddTickPrerequisiteActor(System);
        }
    }
}

void UPlanetaryWorldSubsystem::UnregisterMovement(UAndromedaPawnMovement* Movement)
{
    Movements.Remove(Movement);
}

void UPlanetaryWorldSubsystem::SamplePlanets(double WorldTimeOffset, TArray<FPlanetRuntimeData>& OutPlanets) const
{
    OutPlanets.Reset();
    for (const auto& Entry : Systems)
    {
        if (const AStarSystem* System = Entry.Get())
        {
            System->AppendPlanetRuntimeSamples(WorldTimeOffset, OutPlanets);
        }
    }
}
