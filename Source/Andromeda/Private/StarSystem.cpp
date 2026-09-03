#include "StarSystem.h"

#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "PlanetaryLightingComponent.h"


AStarSystem::AStarSystem()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
}


void AStarSystem::BeginPlay()
{
    Super::BeginPlay();

    SystemSimulationTime = 0.0f;

    SystemData =
        UStarSystemGenerator::GenerateSystem(
            UniverseSeed,
            SystemCoordinate
        );

    SpawnSun();

    SpawnPlanets();
}


void AStarSystem::Tick(
    float DeltaTime
)
{
    Super::Tick(
        DeltaTime
    );

    if (DeltaTime <= 0.0f)
    {
        return;
    }

    const float SafeTimeScale =
        FMath::Max(
            SimulationTimeScale,
            0.0f
        );

    const float SimulationDeltaTime =
        DeltaTime * SafeTimeScale;

    SystemSimulationTime +=
        SimulationDeltaTime;

    UpdatePlanetOrbits(
        SimulationDeltaTime
    );

    UpdatePlanetRotations(
        SimulationDeltaTime
    );
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


    // =========================================================
    // STORE SPAWNED SUN
    // =========================================================

    SpawnedSun = SunActor;


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

    SpawnedPlanets.Empty();

    SpawnedPlanets.Reserve(
        SystemData.Planets.Num()
    );


    for (
        const FPlanetGenerationData& PlanetData :
        SystemData.Planets
        )
    {
        const FVector OrbitPosition =
            CalculateOrbitPosition(
                PlanetData,
                0.0f
            );


        const FVector PlanetWorldPosition =
            GetActorLocation()
            + OrbitPosition;


        const FRotator InitialRotation =
            CalculatePlanetRotation(
                PlanetData,
                0.0f
            );


        const FTransform SpawnTransform(
            InitialRotation,
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


        // =====================================================
        // CONNECT PLANETARY LIGHTING TO THE SPAWNED SUN
        // =====================================================

        UPlanetaryLightingComponent* PlanetaryLighting =
            PlanetActor->FindComponentByClass<
            UPlanetaryLightingComponent
            >();


        if (PlanetaryLighting)
        {
            PlanetaryLighting->SetStarActor(
                SpawnedSun
            );
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "StarSystem: Planet %lld non contiene "
                    "un PlanetaryLightingComponent."
                ),
                PlanetData.PlanetID
            );
        }


        PlanetActor->FinishSpawning(
            SpawnTransform
        );


        FSpawnedPlanetData SpawnedPlanet;

        SpawnedPlanet.PlanetActor =
            PlanetActor;

        SpawnedPlanet.GenerationData =
            PlanetData;


        SpawnedPlanets.Add(
            SpawnedPlanet
        );


        const float RotationPeriod =
            CalculateRotationPeriod(
                PlanetData
            );

        const float AxialTilt =
            CalculateAxialTilt(
                PlanetData
            );

        const float RotationDirection =
            CalculateRotationDirection(
                PlanetData
            );


        UE_LOG(
            LogTemp,
            Log,
            TEXT(
                "StarSystem: Planet %lld spawned | "
                "Seed: %lld | "
                "Radius: %.2f | "
                "TerrainHeight: %.2f | "
                "OrbitDistance: %.2f | "
                "OrbitInclination: %.2f | "
                "OrbitPeriod: %.2f s | "
                "RotationPeriod: %.2f s | "
                "AxialTilt: %.2f deg | "
                "RotationDirection: %.0f | "
                "World Location: %s"
            ),
            PlanetData.PlanetID,
            PlanetData.PlanetSeed,
            PlanetData.PlanetRadius,
            PlanetData.TerrainHeight,
            PlanetData.OrbitDistance,
            PlanetData.OrbitInclination,
            PlanetData.OrbitalPeriod,
            RotationPeriod,
            AxialTilt,
            RotationDirection,
            *PlanetWorldPosition.ToString()
        );
    }
}


void AStarSystem::UpdatePlanetOrbits(
    float DeltaTime
)
{
    if (DeltaTime <= 0.0f)
    {
        return;
    }


    for (
        FSpawnedPlanetData& SpawnedPlanet :
        SpawnedPlanets
        )
    {
        if (!SpawnedPlanet.PlanetActor)
        {
            continue;
        }


        const FVector OrbitPosition =
            CalculateOrbitPosition(
                SpawnedPlanet.GenerationData,
                SystemSimulationTime
            );


        const FVector PlanetWorldPosition =
            GetActorLocation()
            + OrbitPosition;


        SpawnedPlanet.PlanetActor->SetActorLocation(
            PlanetWorldPosition
        );
    }
}


void AStarSystem::UpdatePlanetRotations(
    float DeltaTime
)
{
    if (DeltaTime <= 0.0f)
    {
        return;
    }


    for (
        FSpawnedPlanetData& SpawnedPlanet :
        SpawnedPlanets
        )
    {
        if (!SpawnedPlanet.PlanetActor)
        {
            continue;
        }


        const FRotator PlanetRotation =
            CalculatePlanetRotation(
                SpawnedPlanet.GenerationData,
                SystemSimulationTime
            );


        SpawnedPlanet.PlanetActor->SetActorRotation(
            PlanetRotation
        );
    }
}


FVector AStarSystem::CalculateOrbitPosition(
    const FPlanetGenerationData& PlanetData,
    float SimulationTime
) const
{
    if (PlanetData.OrbitalPeriod <= KINDA_SMALL_NUMBER)
    {
        return FVector::ZeroVector;
    }


    const float OrbitalCycles =
        SimulationTime
        / PlanetData.OrbitalPeriod;


    const float CurrentOrbitAngle =
        PlanetData.OrbitAngle
        + OrbitalCycles * 360.0f;


    const float OrbitAngleRadians =
        FMath::DegreesToRadians(
            CurrentOrbitAngle
        );


    const float InclinationRadians =
        FMath::DegreesToRadians(
            PlanetData.OrbitInclination
        );


    FVector OrbitPosition;


    OrbitPosition.X =
        FMath::Cos(
            OrbitAngleRadians
        )
        * PlanetData.OrbitDistance;


    OrbitPosition.Y =
        FMath::Sin(
            OrbitAngleRadians
        )
        * PlanetData.OrbitDistance
        * FMath::Cos(
            InclinationRadians
        );


    OrbitPosition.Z =
        FMath::Sin(
            OrbitAngleRadians
        )
        * PlanetData.OrbitDistance
        * FMath::Sin(
            InclinationRadians
        );


    return OrbitPosition;
}


float AStarSystem::CalculateRotationPeriod(
    const FPlanetGenerationData& PlanetData
) const
{
    uint64 Seed =
        static_cast<uint64>(
            PlanetData.PlanetSeed
            );

    Seed ^= Seed >> 30;
    Seed *= 0xBF58476D1CE4E5B9ULL;
    Seed ^= Seed >> 27;
    Seed *= 0x94D049BB133111EBULL;
    Seed ^= Seed >> 31;

    const double Normalized =
        static_cast<double>(
            Seed & 0xFFFFFFFFULL
            )
        / 4294967295.0;


    const float MinimumRotationPeriod =
        1800.0f;

    const float MaximumRotationPeriod =
        259200.0f;


    return FMath::Lerp(
        MinimumRotationPeriod,
        MaximumRotationPeriod,
        static_cast<float>(Normalized)
    );
}


float AStarSystem::CalculateAxialTilt(
    const FPlanetGenerationData& PlanetData
) const
{
    uint64 Seed =
        static_cast<uint64>(
            PlanetData.PlanetSeed
            );

    Seed ^= 0x9E3779B97F4A7C15ULL;
    Seed ^= Seed >> 30;
    Seed *= 0xBF58476D1CE4E5B9ULL;
    Seed ^= Seed >> 27;
    Seed *= 0x94D049BB133111EBULL;
    Seed ^= Seed >> 31;


    const double Normalized =
        static_cast<double>(
            Seed & 0xFFFFFFFFULL
            )
        / 4294967295.0;


    return FMath::Lerp(
        0.0f,
        45.0f,
        static_cast<float>(Normalized)
    );
}


float AStarSystem::CalculateInitialRotation(
    const FPlanetGenerationData& PlanetData
) const
{
    uint64 Seed =
        static_cast<uint64>(
            PlanetData.PlanetSeed
            );

    Seed ^= 0xD1B54A32D192ED03ULL;
    Seed ^= Seed >> 30;
    Seed *= 0xBF58476D1CE4E5B9ULL;
    Seed ^= Seed >> 27;
    Seed *= 0x94D049BB133111EBULL;
    Seed ^= Seed >> 31;


    const double Normalized =
        static_cast<double>(
            Seed & 0xFFFFFFFFULL
            )
        / 4294967295.0;


    return FMath::Lerp(
        0.0f,
        360.0f,
        static_cast<float>(Normalized)
    );
}


float AStarSystem::CalculateRotationDirection(
    const FPlanetGenerationData& PlanetData
) const
{
    uint64 Seed =
        static_cast<uint64>(
            PlanetData.PlanetSeed
            );

    Seed ^= 0xA24BAED4963EE407ULL;
    Seed ^= Seed >> 30;
    Seed *= 0xBF58476D1CE4E5B9ULL;
    Seed ^= Seed >> 27;
    Seed *= 0x94D049BB133111EBULL;
    Seed ^= Seed >> 31;


    const double Normalized =
        static_cast<double>(
            Seed & 0xFFFFFFFFULL
            )
        / 4294967295.0;


    if (Normalized < 0.10)
    {
        return -1.0f;
    }


    return 1.0f;
}


FRotator AStarSystem::CalculatePlanetRotation(
    const FPlanetGenerationData& PlanetData,
    float SimulationTime
) const
{
    const float RotationPeriod =
        CalculateRotationPeriod(
            PlanetData
        );


    if (RotationPeriod <= KINDA_SMALL_NUMBER)
    {
        return FRotator::ZeroRotator;
    }


    const float AxialTilt =
        CalculateAxialTilt(
            PlanetData
        );


    const float InitialRotation =
        CalculateInitialRotation(
            PlanetData
        );


    const float RotationDirection =
        CalculateRotationDirection(
            PlanetData
        );


    const float RotationCycles =
        SimulationTime
        / RotationPeriod;


    const float SpinAngle =
        InitialRotation
        + RotationCycles
        * 360.0f
        * RotationDirection;


    const FQuat TiltQuat =
        FQuat(
            FVector::ForwardVector,
            FMath::DegreesToRadians(
                AxialTilt
            )
        );


    const FQuat SpinQuat =
        FQuat(
            FVector::UpVector,
            FMath::DegreesToRadians(
                SpinAngle
            )
        );


    const FQuat FinalQuat =
        TiltQuat * SpinQuat;


    return FinalQuat.Rotator();
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