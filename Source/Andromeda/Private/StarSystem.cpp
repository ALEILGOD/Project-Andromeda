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

    SpawnSun();

    SpawnPlanets();
}


void AStarSystem::SpawnSun()
{
    if (!SunClass)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("StarSystem: SunClass non impostata.")
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

    const FVector SunPosition =
        GetActorLocation();

    const FTransform SpawnTransform(
        FRotator::ZeroRotator,
        SunPosition,
        FVector::OneVector
    );

    AActor* SunActor =
        World->SpawnActorDeferred<AActor>(
            SunClass,
            SpawnTransform,
            this,
            nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn
        );

    if (!SunActor)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("StarSystem: impossibile creare il Sole.")
        );

        return;
    }

    SunActor->FinishSpawning(
        SpawnTransform
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "StarSystem: Sun spawned at center | "
            "World Location: %s"
        ),
        *SunPosition.ToString()
    );
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


        // =====================================================
        // CONVERT ORBIT POSITION TO WORLD SPACE
        // =====================================================

        const FVector PlanetWorldPosition =
            GetActorLocation()
            + OrbitPosition;


        const FTransform SpawnTransform(
            FRotator::ZeroRotator,
            PlanetWorldPosition,
            FVector::OneVector
        );


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
                    "StarSystem: impossibile creare "
                    "il pianeta %lld."
                ),
                PlanetData.PlanetID
            );

            continue;
        }


        if (!SetPlanetGenerationData(
            PlanetActor,
            PlanetData
        ))
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT(
                    "StarSystem: impossibile impostare "
                    "i dati del pianeta %lld."
                ),
                PlanetData.PlanetID
            );

            PlanetActor->Destroy();

            continue;
        }


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
                "TerrainHeight: %.2f | "
                "World Location: %s"
            ),
            PlanetData.PlanetID,
            PlanetData.PlanetSeed,
            PlanetData.PlanetRadius,
            PlanetData.TerrainHeight,
            *PlanetWorldPosition.ToString()
        );
    }
}


bool AStarSystem::SetPlanetGenerationData(
    AActor* PlanetActor,
    const FPlanetGenerationData& PlanetData
)
{
    if (!PlanetActor)
    {
        return false;
    }


    // =========================================================
    // PLANET SEED
    // =========================================================

    FProperty* PlanetSeedProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("PlanetSeed")
        );

    if (!PlanetSeedProperty)
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

    FInt64Property* PlanetSeedInt64 =
        CastField<FInt64Property>(
            PlanetSeedProperty
        );

    if (!PlanetSeedInt64)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "StarSystem: PlanetSeed non e' "
                "di tipo Integer64."
            )
        );

        return false;
    }

    PlanetSeedInt64->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.PlanetSeed
    );


    // =========================================================
    // PLANET RADIUS
    // =========================================================

    FProperty* PlanetRadiusProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("PlanetRadius")
        );

    if (!PlanetRadiusProperty)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "StarSystem: BP_Planet non contiene "
                "la variabile PlanetRadius."
            )
        );

        return false;
    }

    FFloatProperty* PlanetRadiusFloat =
        CastField<FFloatProperty>(
            PlanetRadiusProperty
        );

    if (!PlanetRadiusFloat)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "StarSystem: PlanetRadius non e' "
                "di tipo Float."
            )
        );

        return false;
    }

    PlanetRadiusFloat->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.PlanetRadius
    );


    // =========================================================
    // TERRAIN HEIGHT
    // =========================================================

    FProperty* TerrainHeightProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("TerrainHeight")
        );

    if (!TerrainHeightProperty)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "StarSystem: BP_Planet non contiene "
                "la variabile TerrainHeight."
            )
        );

        return false;
    }

    FFloatProperty* TerrainHeightFloat =
        CastField<FFloatProperty>(
            TerrainHeightProperty
        );

    if (!TerrainHeightFloat)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "StarSystem: TerrainHeight non e' "
                "di tipo Float."
            )
        );

        return false;
    }

    TerrainHeightFloat->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.TerrainHeight
    );


    // =========================================================
    // CONTINENTAL SCALE
    // =========================================================

    FProperty* ContinentalScaleProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("ContinentalScale")
        );

    if (!ContinentalScaleProperty)
    {
        return false;
    }

    FFloatProperty* ContinentalScaleFloat =
        CastField<FFloatProperty>(
            ContinentalScaleProperty
        );

    if (!ContinentalScaleFloat)
    {
        return false;
    }

    ContinentalScaleFloat->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.ContinentalScale
    );


    // =========================================================
    // MOUNTAIN SCALE
    // =========================================================

    FProperty* MountainScaleProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("MountainScale")
        );

    if (!MountainScaleProperty)
    {
        return false;
    }

    FFloatProperty* MountainScaleFloat =
        CastField<FFloatProperty>(
            MountainScaleProperty
        );

    if (!MountainScaleFloat)
    {
        return false;
    }

    MountainScaleFloat->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.MountainScale
    );


    // =========================================================
    // DETAIL SCALE
    // =========================================================

    FProperty* DetailScaleProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("DetailScale")
        );

    if (!DetailScaleProperty)
    {
        return false;
    }

    FFloatProperty* DetailScaleFloat =
        CastField<FFloatProperty>(
            DetailScaleProperty
        );

    if (!DetailScaleFloat)
    {
        return false;
    }

    DetailScaleFloat->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.DetailScale
    );


    // =========================================================
    // MOUNTAIN STRENGTH
    // =========================================================

    FProperty* MountainStrengthProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("MountainStrength")
        );

    if (!MountainStrengthProperty)
    {
        return false;
    }

    FFloatProperty* MountainStrengthFloat =
        CastField<FFloatProperty>(
            MountainStrengthProperty
        );

    if (!MountainStrengthFloat)
    {
        return false;
    }

    MountainStrengthFloat->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.MountainStrength
    );


    // =========================================================
    // DETAIL STRENGTH
    // =========================================================

    FProperty* DetailStrengthProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("DetailStrength")
        );

    if (!DetailStrengthProperty)
    {
        return false;
    }

    FFloatProperty* DetailStrengthFloat =
        CastField<FFloatProperty>(
            DetailStrengthProperty
        );

    if (!DetailStrengthFloat)
    {
        return false;
    }

    DetailStrengthFloat->SetPropertyValue_InContainer(
        PlanetActor,
        PlanetData.DetailStrength
    );


    return true;
}