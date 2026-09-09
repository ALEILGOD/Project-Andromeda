#include "Atmosphere/AndromedaAtmosphereRegistry.h"

#include "Engine/World.h"
#include "EngineUtils.h"


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

    // The placed level instance may have tick disabled in its serialized state:
    // force it on, otherwise the time-window search retry never runs.
    SetActorTickEnabled(true);


    // First attempt to find the StarSystem and register atmospheres.
    SyncAtmospheres();
}


void AAndromedaAtmosphereRegistry::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);


    // Keep trying to find the StarSystem and its planets from the normal
    // Tick (non-blocking) until the sync succeeds or the search time
    // window expires. Once bStarSystemFound is latched, no more retries.
    if (!bStarSystemFound)
    {
        if (!bSearchWindowExpired)
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
    // Guard: without a World there is nothing to synchronize.
    // --------------------------------------------------------
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }


    // --------------------------------------------------------
    // Start the search window on the first attempt (world time).
    // --------------------------------------------------------
    if (SearchStartWorldSeconds < 0.0)
    {
        SearchStartWorldSeconds = World->GetTimeSeconds();
    }


    // --------------------------------------------------------
    // Step 1: find the StarSystem (if not already cached).
    // --------------------------------------------------------
    if (!StarSystem.IsValid())
    {
        for (TActorIterator<AStarSystem> It(World); It; ++It)
        {
            StarSystem = *It;
            break;
        }
    }


    if (!StarSystem.IsValid())
    {
        CheckSearchWindowExpired(World);
        return;
    }


    // NOTE: bStarSystemFound is latched only in Step 2, once the planets are available.


    // --------------------------------------------------------
    // Step 2: get the current planet list.
    // --------------------------------------------------------
    TArray<FPlanetRuntimeData> Planets;
    StarSystem->GetAllPlanetRuntimeData(Planets);


    if (Planets.Num() <= 0)
    {
        // The StarSystem actor exists but has not generated its planets yet
        // (its BeginPlay/SpawnPlanets may run AFTER this registry's BeginPlay).
        // Do NOT latch bStarSystemFound here: keep retrying from Tick until
        // the search time window expires (no blocking waits).
        CheckSearchWindowExpired(World);
        return;
    }


    // StarSystem found AND planets are available: latch and register.
    bStarSystemFound = true;


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


// =========================================================
// SEARCH WINDOW
// =========================================================

void AAndromedaAtmosphereRegistry::CheckSearchWindowExpired(UWorld* World)
{
    if (bSearchWindowExpired)
    {
        return;
    }


    if ((World->GetTimeSeconds() - SearchStartWorldSeconds) >= StarSystemSearchTimeoutSeconds)
    {
        bSearchWindowExpired = true;


        // Logged once: the registry gave up within the time window.
        UE_LOG(
            LogAndromedaAtmos,
            Warning,
            TEXT("AAndromedaAtmosphereRegistry: StarSystem sync timed out after %.1f s (no planets available)."),
            StarSystemSearchTimeoutSeconds
        );
    }
}