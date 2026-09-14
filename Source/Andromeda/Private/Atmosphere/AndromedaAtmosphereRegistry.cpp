#include "Atmosphere/AndromedaAtmosphereRegistry.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Planet/Planet.h"
#include "Sun.h"
#include "Atmosphere/AndromedaAtmosphereSystem.h"
#include "Atmosphere/AtmosphereLightReferenceComponent.h"
#include "Planet/Zephyr/ZephyrProfileLibrary.h"
#include "Planet/Zephyr/ZephyrSharedAtmosphere.h"


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


void AAndromedaAtmosphereRegistry::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // ATMOS-LIFETIME: on world teardown (PIE stop, map unload, ...) unregister
    // every atmosphere this registry registered, so no handle/instance outlives
    // its world session. The renderer additionally clears the whole manager on
    // FWorldDelegates::OnWorldCleanup as a world-wide safety net; both paths
    // are idempotent (unregistering a missing handle is a no-op).
    // PHASE 2.1: legacy per-handle unregistration is obsolete;
    // the registry no longer writes the ATMOS manager.
    AtmosphereHandles.Empty();
    StarSystem.Reset();
    bStarSystemFound = false;
    bSearchWindowExpired = false;
    SearchStartWorldSeconds = -1.0;
    FindRetryCount = 0;

    // PHASE 2.1: drop the UNIFIED mailbox (planets + star) so no
    // stale atmosphere outlives its world session (PIE stop, ...).
    FAndromedaAtmosphereSystem::Get().Clear();

    CachedSunActor.Reset();
    CachedLightReference.Reset();
    bSunFallbackLogged = false;

    Super::EndPlay(EndPlayReason);
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


    // StarSystem found: republish the unified snapshot every frame
    // so planet centers/rotations follow orbits + spin and the
    // star stays current. Single writer, single mailbox.
    if (StarSystem.IsValid())
    {
        PublishUnifiedSnapshot();
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


    // PHASE 2.1: no per-handle ATMOS registration anymore. Planet
    // atmospheres exist implicitly as unified instances derived
    // from FPlanetRuntimeData (see PublishUnifiedSnapshot). Just
    // publish from the first successful sync so the atmosphere
    // exists before the next Tick.
    PublishUnifiedSnapshot();
}


// =========================================================
// UNIFIED PUBLISH (game thread, sole writer of the mailbox)
// =========================================================

void AAndromedaAtmosphereRegistry::PublishUnifiedSnapshot()
{
    if (!StarSystem.IsValid())
    {
        return;
    }

    // Game thread: resolve the Sun's light reference (cached;
    // re-resolved only when missing). Render thread never touches it.
    ResolveSunLightReference();

    TArray<FPlanetRuntimeData> Planets;
    StarSystem->GetAllPlanetRuntimeData(Planets);

    TArray<FAndromedaAtmosphereInstance> Entries;
    Entries.Reserve(Planets.Num());

    for (const FPlanetRuntimeData& Planet : Planets)
    {
        // Physical identity: seed + archetype from the planet
        // actor when available, runtime data otherwise.
        EPlanetArchetype Archetype = EPlanetArchetype::Terran;
        int64 Seed = Planet.PlanetSeed;

        if (const APlanet* PlanetActor = Cast<APlanet>(Planet.PlanetActor))
        {
            Archetype = PlanetActor->PlanetArchetype;
            Seed = PlanetActor->PlanetSeed;
        }

        // Geometry convention shared with the ATMOS volumes.
        // AtmosphereRadius reads the live multiplier (never
        // hardcoded); SkyTransitionRadius is PlanetRadius +
        // MaxTerrainHeight + ~2 km (200000 cm, shared reference)
        // and stays DISTINCT from the physical shell.
        const float SurfaceRadius = Planet.PlanetRadius;
        const float AtmosphereRadius =
            (Planet.PlanetRadius + Planet.TerrainHeight)
            * AtmosphereRadiusMultiplier;

        FAndromedaAtmosphereInstance Entry;
        Entry.Profile =
            UZephyrProfileLibrary::BuildProfile(
                Seed,
                Archetype,
                SurfaceRadius,
                AtmosphereRadius
            );

        if (!Entry.Profile.IsValidConfiguration())
        {
            continue;
        }

        Entry.PlanetCenter = Planet.WorldPosition;
        Entry.PlanetID = Planet.PlanetID;
        Entry.TerrainHeightCm = FMath::Max(Planet.TerrainHeight, 0.0f);
        Entry.SkyTransitionRadiusCm =
            AndromedaAtmosphereReference::ComputeSkyTransitionRadiusCm(
                Planet.PlanetRadius,
                Planet.TerrainHeight);
        // Transition COMPLETION radius (mandated rule, per planet):
        // (PlanetRadius + TerrainHeight) * 1.1, live runtime data,
        // cm like every distance it is compared with. Separate from
        // the physical shell (multiplier-driven, untouched) and from
        // Rs above (outer blend start, retained).
        Entry.TransitionCompleteRadiusCm =
            AndromedaAtmosphereReference::ComputeTransitionCompleteRadiusCm(
                Planet.PlanetRadius,
                Planet.TerrainHeight);
        // Planet rotation (world frame, TiltQuat * SpinQuat). Reported
        // for diagnostics and terrain coherence. It is NOT applied to
        // the sun direction: the Case A directional sun stays
        // world-frame end to end (see SunDirectionWorld).
        Entry.PlanetRotation = Planet.CurrentRotation;

        // SUN LIGHT REFERENCE (source of truth, convention A:
        // Planet -> Sun, world frame). The Registry never computes
        // (Star - Center) itself: the Sun's AtmosphereLightReference
        // owns that computation. Per planet (parallax preserved).
        if (CachedLightReference.IsValid())
        {
            Entry.SunDirectionWorld =
                CachedLightReference->GetDirectionTowardSunWorld(
                    Planet.WorldPosition
                );
        }
        else
        {
            // Documented fallback (no Sun actor/reference): same owned
            // helper, explicit emission point (StarSystem location).
            // Logged once in ResolveSunLightReference().
            Entry.SunDirectionWorld =
                UAtmosphereLightReferenceComponent::ComputeDirectionTowardSunWorld(
                    StarSystem->GetActorLocation(),
                    Planet.WorldPosition
                );
        }

        Entries.Add(Entry);
    }

    // Single star source, single mailbox: the emission point comes
    // from the Sun reference when available (authoritative), else
    // the StarSystem location (documented fallback). Both render
    // stages read this snapshot; neither reconstructs the sun.
    const FVector EmissionPoint =
        CachedLightReference.IsValid()
            ? CachedLightReference->GetEmissionPointWorld()
            : StarSystem->GetActorLocation();

    FAndromedaAtmosphereSystem::Get().SetSnapshot(
        Entries,
        EmissionPoint
    );
}


// =========================================================
// SUN LIGHT REFERENCE
// =========================================================

void AAndromedaAtmosphereRegistry::ResolveSunLightReference()
{
    if (!StarSystem.IsValid())
    {
        return;
    }

    // Re-resolve only when the cached actor died or was never found
    // (Sun spawns once with the system; no per-frame discovery).
    if (!CachedSunActor.IsValid() || !CachedLightReference.IsValid())
    {
        CachedSunActor.Reset();
        CachedLightReference.Reset();

        if (AActor* SunActor = StarSystem->GetSunActor())
        {
            CachedSunActor = SunActor;
            CachedLightReference =
                SunActor->FindComponentByClass<UAtmosphereLightReferenceComponent>();
        }

        if (!CachedLightReference.IsValid() && !bSunFallbackLogged)
        {
            bSunFallbackLogged = true;

            UE_LOG(
                LogAndromedaAtmos,
                Warning,
                TEXT("AAndromedaAtmosphereRegistry: no Sun AtmosphereLightReference found; using StarSystem location as emission point (documented fallback).")
            );
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