#include "StarSystem.h"

#include "Engine/World.h"
#include "UObject/UnrealType.h"

AStarSystem::AStarSystem()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AStarSystem::BeginPlay()
{
    Super::BeginPlay();

    SystemData =
        UStarSystemGenerator::GenerateSystem(
            UniverseSeed,
            SystemCoordinate
        );

    SpawnPlanets();
}

void AStarSystem::SpawnPlanets()
{
    if (!PlanetClass)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("StarSystem: PlanetClass non impostata.")
        );

        return;
    }

    UWorld* World = GetWorld();

    if (!World)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("StarSystem: World non valido.")
        );

        return;
    }

    for (const FPlanetGenerationData& PlanetData : SystemData.Planets)
    {
        const float OrbitAngleRadians =
            FMath::DegreesToRadians(
                PlanetData.OrbitAngle
            );

        const float InclinationRadians =
            FMath::DegreesToRadians(
                PlanetData.OrbitInclination
            );

        FVector OrbitPosition;

        OrbitPosition.X =
            FMath::Cos(OrbitAngleRadians)
            * PlanetData.OrbitDistance;

        OrbitPosition.Y =
            FMath::Sin(OrbitAngleRadians)
            * PlanetData.OrbitDistance
            * FMath::Cos(InclinationRadians);

        OrbitPosition.Z =
            FMath::Sin(OrbitAngleRadians)
            * PlanetData.OrbitDistance
            * FMath::Sin(InclinationRadians);

        const FTransform SpawnTransform(
            FRotator::ZeroRotator,
            OrbitPosition,
            FVector::OneVector
        );

        /*
         * IMPORTANT:
         *
         * SpawnActorDeferred crea il pianeta ma NON esegue
         * ancora il Construction Script del Blueprint.
         *
         * Questo ci permette di impostare PlanetSeed prima
         * che BP_Planet generi la mesh procedurale.
         */
        AActor* PlanetActor =
            World->SpawnActorDeferred<AActor>(
                PlanetClass,
                SpawnTransform,
                this,
                nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn
            );

        if (!PlanetActor)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT(
                    "StarSystem: impossibile creare il pianeta %lld."
                ),
                PlanetData.PlanetID
            );

            continue;
        }

        /*
         * Imposta PlanetSeed PRIMA del Construction Script.
         */
        if (!SetPlanetSeed(
            PlanetActor,
            PlanetData.PlanetSeed
        ))
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT(
                    "StarSystem: impossibile impostare PlanetSeed "
                    "sul pianeta %lld."
                ),
                PlanetData.PlanetID
            );

            PlanetActor->Destroy();

            continue;
        }

        /*
         * Ora eseguiamo definitivamente il processo di spawn.
         *
         * Da questo momento BP_Planet può eseguire il suo
         * Construction Script vedendo già il PlanetSeed corretto.
         */
        PlanetActor->FinishSpawning(
            SpawnTransform
        );

        UE_LOG(
            LogTemp,
            Log,
            TEXT(
                "StarSystem: Planet %lld spawned | "
                "Seed: %lld | "
                "Radius: %.2f | "
                "Location: %s"
            ),
            PlanetData.PlanetID,
            PlanetData.PlanetSeed,
            PlanetData.PlanetRadius,
            *OrbitPosition.ToString()
        );
    }
}

bool AStarSystem::SetPlanetSeed(
    AActor* PlanetActor,
    int64 PlanetSeed
)
{
    if (!PlanetActor)
    {
        return false;
    }

    FProperty* Property =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("PlanetSeed")
        );

    if (!Property)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "StarSystem: BP_Planet non contiene "
                "la variabile PlanetSeed."
            )
        );

        return false;
    }

    FInt64Property* Int64Property =
        CastField<FInt64Property>(Property);

    if (!Int64Property)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "StarSystem: PlanetSeed non e' di tipo Integer64."
            )
        );

        return false;
    }

    Int64Property->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetSeed
    );

    return true;
}