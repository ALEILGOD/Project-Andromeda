#include "Atmosphere/AndromedaAtmosphereRegistry.h"

#include "Engine/World.h"


// =========================================================
// LIFECYCLE
// =========================================================

AAndromedaAtmosphereRegistry::AAndromedaAtmosphereRegistry()
{
    PrimaryActorTick.bCanEverTick = true;
}


void AAndromedaAtmosphereRegistry::BeginPlay()
{
    Super::BeginPlay();


    // First attempt to find the StarSystem and register atmospheres.
    SyncAtmospheres();
}


void AAndromedaAtmosphereRegistry::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);


    // Keep trying to find the StarSystem for a limited number of frames
    // (the StarSystem may not exist yet at BeginPlay).
    if (!bStarSystemFound)
    {
        if (FindRetryCount < MaxFindRetries)
        {
            SyncAtmospheres();
        }
        return;
    }


    // StarSystem found: update atmosphere positions every frame so they
    // follow the planets as they orbit.
    if (StarSystem.IsValid())
    {
        TArray<FPlanetRuntimeData> Planets;
        StarSystem->GetAllPlanetRuntimeData(Planets);


        const int32 NumToUpdate = FMath::Min(
            Planets.Num(),
            AtmosphereHandles.Num()
        );


        for (int32 i = 0; i < NumToUpdate; ++i)
        {
            if (AtmosphereHandles[i].IsValid())
            {
                FAndromedaAtmosphereManager::Get().UpdateAtmosphereWorldPosition(
                    AtmosphereHandles[i],
                    Planets[i].WorldPosition
                );
            }
        }
    }
}


// =========================================================
// SYNCHRONIZATION
// =========================================================

void AAndromedaAtmosphereRegistry::SyncAtmospheres()
{
    // --------------------------------------------------------
    // Step 1: find the StarSystem (if not already cached).
    // --------------------------------------------------------
    if (!StarSystem.IsValid())
    {
        for (TActorIterator<AStarSystem> It(GetWorld()); It; ++It)
        {
            StarSystem = *It;
            break;
        }
    }


    if (!StarSystem.IsValid())
    {
        ++FindRetryCount;
        return;
    }


    bStarSystemFound = true;


    // --------------------------------------------------------
    // Step 2: get the current planet list.
    // --------------------------------------------------------
    TArray<FPlanetRuntimeData> Planets;
    StarSystem->GetAllPlanetRuntimeData(Planets);


    if (Planets.Num() <= 0)
    {
        return;
    }


    // --------------------------------------------------------
    // Step 3: register atmospheres for any new planets.
    // --------------------------------------------------------
    while (AtmosphereHandles.Num() < Planets.Num())
    {
        const int32 PlanetIndex = AtmosphereHandles.Num();
        const FPlanetRuntimeData& Planet = Planets[PlanetIndex];


        FAndromedaAtmosphereInstanceDesc Desc;
        Desc.Parameters.SurfaceRadius = Planet.PlanetRadius;
        Desc.Parameters.AtmosphereRadius =
            (Planet.PlanetRadius + Planet.TerrainHeight) * AtmosphereRadiusMultiplier;
        Desc.WorldPosition = Planet.WorldPosition;
        Desc.DebugName = FName(
            *FString::Printf(
                TEXT("Planet_%lld_Atm"),
                Planet.PlanetID
            )
        );


        const FAndromedaAtmosphereHandle Handle =
            FAndromedaAtmosphereManager::Get().RegisterAtmosphere(Desc);


        if (Handle.IsValid())
        {
            AtmosphereHandles.Add(Handle);
        }
        else
        {
            // Registration failed: stop trying to avoid an infinite loop.
            UE_LOG(
                LogAndromedaAtmos,
                Warning,
                TEXT("AAndromedaAtmosphereRegistry: failed to register atmosphere for planet %lld."),
                Planet.PlanetID
            );
            break;
        }
    }
}
