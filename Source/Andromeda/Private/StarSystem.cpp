#include "StarSystem.h"

#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Sun.h"
#include "UObject/UnrealType.h"
#include "PlanetaryLightingComponent.h"
#include "Planet/Planet.h"
#include "HillairePlanetLinkComponent.h"
#include "PlanetaryOrbitMath.h"
#include "PlanetaryWorldSubsystem.h"


AStarSystem::AStarSystem()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SystemOrigin")));
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
}


void AStarSystem::BeginPlay()
{
    Super::BeginPlay();

    SystemSimulationTime = 0.0f;
    OrbitalSimulationTime = 0.0;

    SystemData =
        UStarSystemGenerator::GenerateSystem(
            UniverseSeed,
            SystemCoordinate
        );

    SpawnSun();

    SpawnPlanets();
    UpdateRuntimeSnapshot();

    GetWorld()->GetSubsystem<UPlanetaryWorldSubsystem>()->RegisterSystem(this);
}

void AStarSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UPlanetaryWorldSubsystem* Registry = GetWorld()->GetSubsystem<UPlanetaryWorldSubsystem>())
    {
        Registry->UnregisterSystem(this);
    }
    if (EndPlayReason == EEndPlayReason::Destroyed)
    {
        for (const FSpawnedPlanetData& Planet : SpawnedPlanets)
        {
            if (IsValid(Planet.PlanetActor))
            {
                Planet.PlanetActor->Destroy();
            }
        }
        if (IsValid(SpawnedSun))
        {
            SpawnedSun->Destroy();
        }
    }
    Super::EndPlay(EndPlayReason);
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
    OrbitalSimulationTime += double(SimulationDeltaTime) * FMath::Max(double(OrbitTimeScale), 0.0);

    UpdatePlanetOrbits(
        SimulationDeltaTime
    );

    UpdatePlanetRotations(
        SimulationDeltaTime
    );
    UpdateRuntimeSnapshot();
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
    if (!PlanetClass || !PlanetClass->IsChildOf(APlanet::StaticClass()))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("StarSystem: PlanetClass must derive from APlanet (surface bodies only).")
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

    // Ensure Sun actor ticks AFTER all planet actors so that:
    // 1. StarSystem updates orbits/rotations (in its Tick, TG_PrePhysics)
    // 2. Planet actors tick (TG_PostUpdateWork) -> PlanetaryLightingComponent updates CurrentStarDirection, HillairePlanetLinkComponent pushes planet state
    // 3. Sun actor ticks (TG_PostUpdateWork) -> HillaireStarLinkComponent reads updated planetary lighting and pushes sun direction
    if (SpawnedSun)
    {
        for (const FSpawnedPlanetData& SpawnedPlanet : SpawnedPlanets)
        {
            if (SpawnedPlanet.PlanetActor)
            {
                SpawnedSun->AddTickPrerequisiteActor(SpawnedPlanet.PlanetActor);
            }
        }
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


    const double OrbitSimulationTime = OrbitalSimulationTime;


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
                OrbitSimulationTime
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


int32 AStarSystem::GetPlanetCount() const
{
    return SpawnedPlanets.Num();
}


const FSpawnedPlanetData* AStarSystem::FindSpawnedPlanet(
    int64 PlanetID
) const
{
    for (
        const FSpawnedPlanetData& SpawnedPlanet :
        SpawnedPlanets
        )
    {
        if (
            SpawnedPlanet.GenerationData.PlanetID == PlanetID &&
            SpawnedPlanet.PlanetActor
            )
        {
            return &SpawnedPlanet;
        }
    }

    return nullptr;
}


FPlanetRuntimeData AStarSystem::BuildPlanetRuntimeData(
    const FSpawnedPlanetData& SpawnedPlanet,
    double WorldTimeOffset
) const
{
    FPlanetRuntimeData Out;

    const APlanet* Planet = Cast<APlanet>(SpawnedPlanet.PlanetActor);
    if (!IsValid(Planet))
    {
        return Out;
    }

    Out.bValid = true;
    Out.BodyType = ECelestialBodyType::Planet;

    Out.PlanetActor = SpawnedPlanet.PlanetActor;

    Out.PlanetID =
        SpawnedPlanet.GenerationData.PlanetID;

    Out.PlanetSeed =
        SpawnedPlanet.GenerationData.PlanetSeed;

    const FVector Scale = Planet->GetActorScale3D();
    if (!Scale.AllComponentsEqual(1.e-6) || Scale.X <= 0.0)
    {
        // The surface gravity model is spherical, not an ellipsoid model.
        Out.bValid = false;
        return Out;
    }
    Out.PlanetRadius = float(Planet->PlanetRadius * Scale.X);

    Out.TerrainHeight = float(Planet->TerrainHeight * Scale.X);
    Out.OrbitDistance = SpawnedPlanet.GenerationData.OrbitDistance;
    Out.SurfaceGravity = Planet->SurfaceGravity > 0.0 ? Planet->SurfaceGravity : SpawnedPlanet.GenerationData.SurfaceGravity * Scale.X;
    // Read the frozen atmosphere's geometry contract, never its camera/body selection.
    Out.AtmosphereTopRadius = UHillairePlanetLinkComponent::ComputeAtmosphereTopRadiusCm(Out.PlanetRadius, Out.TerrainHeight);
    const double Envelope = Out.PlanetRadius + Out.TerrainHeight;
    const ASun* Star = GetSunActor();
    Out.MaxInfluenceRadius = Out.OrbitDistance > 0.0 ? Out.OrbitDistance - (Star ? Star->LightSourceRadius : 0.0) : Envelope;
    for (const FSpawnedPlanetData& Other : SpawnedPlanets)
    {
        const APlanet* OtherActor = Cast<APlanet>(Other.PlanetActor);
        if (IsValid(OtherActor) && Other.GenerationData.PlanetID != Out.PlanetID)
        {
            Out.MaxInfluenceRadius = FMath::Min(Out.MaxInfluenceRadius,
                FMath::Abs(double(Other.GenerationData.OrbitDistance) - Out.OrbitDistance) -
                (OtherActor->PlanetRadius + OtherActor->TerrainHeight) * OtherActor->GetActorScale3D().GetAbsMax());
        }
    }

    Out.OrbitalPeriod =
        SpawnedPlanet.GenerationData.OrbitalPeriod;

    Out.RotationRateDegreesPerSecond =
        GetPlanetRotationRateDegreesPerSecond(
            Out.PlanetID
        );

    Out.RotationAxis =
        GetPlanetRotationAxis(
            Out.PlanetID
        );

    Out.AngularVelocity = Out.RotationAxis * FMath::DegreesToRadians(double(Out.RotationRateDegreesPerSecond));
    SampleRuntimeMotion(SpawnedPlanet, WorldTimeOffset, Out);

    return Out;
}

void AStarSystem::SampleRuntimeMotion(const FSpawnedPlanetData& Planet, double WorldTimeOffset, FPlanetRuntimeData& Out) const
{
    const double WorldRate = FMath::Max(double(SimulationTimeScale), 0.0) * CustomTimeDilation;
    const double OrbitWorldRate = WorldRate * FMath::Max(double(OrbitTimeScale), 0.0);
    const FAndromedaOrbitState Orbit = AndromedaOrbit::Evaluate(Planet.GenerationData,
        OrbitalSimulationTime + WorldTimeOffset * OrbitWorldRate, 1.0, OrbitWorldRate);
    // The orbit solution is system-local; the system origin is a rigid world
    // transform, not a camera/planet reference frame inherited a second time.
    Out.WorldPosition = GetActorLocation() + GetActorQuat().RotateVector(Orbit.Position);
    Out.OrbitalVelocity = GetActorQuat().RotateVector(Orbit.Velocity);
    Out.OrbitalAcceleration = GetActorQuat().RotateVector(Orbit.Acceleration);
    Out.CurrentRotation = CalculatePlanetRotation(Planet.GenerationData, SystemSimulationTime + WorldTimeOffset * WorldRate);
}

void AStarSystem::UpdateRuntimeSnapshot()
{
    RuntimeSnapshot.Reset();
    RuntimeSnapshot.Reserve(SpawnedPlanets.Num());
    for (const FSpawnedPlanetData& Planet : SpawnedPlanets)
    {
        RuntimeSnapshot.Add(BuildPlanetRuntimeData(Planet));
    }
}


FPlanetRuntimeData AStarSystem::GetPlanetRuntimeData(
    int64 PlanetID
) const
{
    for (const FPlanetRuntimeData& Planet : RuntimeSnapshot)
    {
        if (Planet.PlanetID == PlanetID && Planet.bValid)
        {
            return Planet;
        }
    }
    return FPlanetRuntimeData();
}


int32 AStarSystem::GetAllPlanetRuntimeData(
    TArray<FPlanetRuntimeData>& OutPlanets
) const
{
    OutPlanets = RuntimeSnapshot;

    return OutPlanets.Num();
}

void AStarSystem::AppendPlanetRuntimeSamples(double WorldTimeOffset, TArray<FPlanetRuntimeData>& OutPlanets) const
{
    for (int32 Index = 0; Index < RuntimeSnapshot.Num(); ++Index)
    {
        FPlanetRuntimeData Sample = RuntimeSnapshot[Index];
        if (Sample.bValid && IsValid(Sample.PlanetActor))
        {
            if (WorldTimeOffset != 0.0)
            {
                SampleRuntimeMotion(SpawnedPlanets[Index], WorldTimeOffset, Sample);
            }
            OutPlanets.Add(Sample);
        }
    }
}


FVector AStarSystem::GetPlanetWorldPosition(
    int64 PlanetID
) const
{
    const FSpawnedPlanetData* SpawnedPlanet =
        FindSpawnedPlanet(
            PlanetID
        );

    if (!SpawnedPlanet)
    {
        return FVector::ZeroVector;
    }

    return SpawnedPlanet->PlanetActor->GetActorLocation();
}


FVector AStarSystem::GetPlanetOrbitalVelocity(
    int64 PlanetID
) const
{
    const FSpawnedPlanetData* SpawnedPlanet =
        FindSpawnedPlanet(
            PlanetID
        );

    if (!SpawnedPlanet)
    {
        return FVector::ZeroVector;
    }

    return GetActorQuat().RotateVector(AndromedaOrbit::Evaluate(SpawnedPlanet->GenerationData, OrbitalSimulationTime, 1.0,
        FMath::Max(double(SimulationTimeScale), 0.0) * CustomTimeDilation * FMath::Max(double(OrbitTimeScale), 0.0)).Velocity);
}


FRotator AStarSystem::GetPlanetRotation(
    int64 PlanetID
) const
{
    const FSpawnedPlanetData* SpawnedPlanet =
        FindSpawnedPlanet(
            PlanetID
        );

    if (!SpawnedPlanet)
    {
        return FRotator::ZeroRotator;
    }

    // Stesso valore applicato da UpdatePlanetRotations.
    return CalculatePlanetRotation(
        SpawnedPlanet->GenerationData,
        SystemSimulationTime
    );
}


float AStarSystem::GetPlanetRotationRateDegreesPerSecond(
    int64 PlanetID
) const
{
    const FSpawnedPlanetData* SpawnedPlanet =
        FindSpawnedPlanet(
            PlanetID
        );

    if (!SpawnedPlanet)
    {
        return 0.0f;
    }

    const float RotationPeriod =
        CalculateRotationPeriod(
            SpawnedPlanet->GenerationData
        );

    if (RotationPeriod <= KINDA_SMALL_NUMBER)
    {
        return 0.0f;
    }

    // La rotazione usa SystemSimulationTime pieno (scalato da
    // SimulationTimeScale), quindi il rate EFFETTIVO nel tempo
    // mondiale include SimulationTimeScale.
    const float SafeTimeScale =
        FMath::Max(
            SimulationTimeScale,
            0.0f
        );

    return (360.0f / RotationPeriod) *
        SafeTimeScale * CustomTimeDilation;
}


FVector AStarSystem::GetPlanetRotationAxis(
    int64 PlanetID
) const
{
    const FSpawnedPlanetData* SpawnedPlanet =
        FindSpawnedPlanet(
            PlanetID
        );

    if (!SpawnedPlanet)
    {
        return FVector::ZeroVector;
    }

    // Stesso asse usato da CalculatePlanetRotation: lo spin avviene attorno
    // all'Up del frame pre-tilt; TiltQuat lo ruota nel mondo.
    const float AxialTilt =
        CalculateAxialTilt(
            SpawnedPlanet->GenerationData
        );

    const float RotationDirection =
        CalculateRotationDirection(
            SpawnedPlanet->GenerationData
        );

    const FQuat TiltQuat(
        FVector::ForwardVector,
        FMath::DegreesToRadians(
            AxialTilt
        )
    );

    return GetActorQuat().RotateVector(TiltQuat.RotateVector(FVector::UpVector)) * RotationDirection;
}


AActor* AStarSystem::GetPlanetActor(
    int64 PlanetID
) const
{
    const FSpawnedPlanetData* SpawnedPlanet =
        FindSpawnedPlanet(
            PlanetID
        );

    if (!SpawnedPlanet)
    {
        return nullptr;
    }

    return SpawnedPlanet->PlanetActor.Get();
}


ASun* AStarSystem::GetSunActor() const
{
    // Read-only accessor for the spawned Sun (generic STARMAP API).
    // SunClass is TSubclassOf<AActor>: a strict cast keeps the API
    // typed without constraining what STARMAP may spawn.
    return Cast<ASun>(SpawnedSun.Get());
}


FGuid AStarSystem::GetStableStarId() const
{
    // Stable star identity derived from the star system's seed and coordinate.
    // Uses FNV-1a 64-bit hash for determinism across frames, spawns, and sessions.
    uint64 Hash = 14695981039346656037ULL;
    const uint64 Seed = static_cast<uint64>(UniverseSeed);
    const uint64 CoordX = static_cast<uint64>(SystemCoordinate.X);
    const uint64 CoordY = static_cast<uint64>(SystemCoordinate.Y);
    const uint64 CoordZ = static_cast<uint64>(SystemCoordinate.Z);
    
    for (int32 i = 0; i < 8; ++i) { Hash ^= (Seed >> (i * 8)) & 0xFFULL; Hash *= 1099511628211ULL; }
    for (int32 i = 0; i < 8; ++i) { Hash ^= (CoordX >> (i * 8)) & 0xFFULL; Hash *= 1099511628211ULL; }
    for (int32 i = 0; i < 8; ++i) { Hash ^= (CoordY >> (i * 8)) & 0xFFULL; Hash *= 1099511628211ULL; }
    for (int32 i = 0; i < 8; ++i) { Hash ^= (CoordZ >> (i * 8)) & 0xFFULL; Hash *= 1099511628211ULL; }
    
    return FGuid(static_cast<uint32>(Hash >> 32), static_cast<uint32>(Hash & 0xFFFFFFFF), 0, 0);
}


FVector AStarSystem::CalculateOrbitPosition(
    const FPlanetGenerationData& PlanetData,
    double SimulationTime
) const
{
    // Same solution as velocity/acceleration; the caller already scales orbit time.
    return GetActorQuat().RotateVector(AndromedaOrbit::Evaluate(PlanetData, SimulationTime, 1.0, 1.0).Position);
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
    double SimulationTime
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


    const double RotationCycles =
        SimulationTime
        / RotationPeriod;


    const double SpinAngle = FMath::Fmod(
        InitialRotation
        + RotationCycles
        * 360.0f
        * RotationDirection, 360.0);


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
        GetActorQuat() * TiltQuat * SpinQuat;


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

    FProperty* PlanetIDProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("PlanetID")
        );
    if (PlanetIDProperty)
    {
        if (FInt64Property* Int64Prop = CastField<FInt64Property>(PlanetIDProperty))
        {
            Int64Prop->SetPropertyValue_InContainer(PlanetActor, PlanetData.PlanetID);
        }
    }

    FProperty* OrbitDistanceProperty =
        PlanetActor->GetClass()->FindPropertyByName(
            TEXT("OrbitDistance")
        );
    if (OrbitDistanceProperty)
    {
        if (FFloatProperty* FloatProp = CastField<FFloatProperty>(OrbitDistanceProperty))
        {
            FloatProp->SetPropertyValue_InContainer(PlanetActor, PlanetData.OrbitDistance);
        }
    }

    return true;
}
