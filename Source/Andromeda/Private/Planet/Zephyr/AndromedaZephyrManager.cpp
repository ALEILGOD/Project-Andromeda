#include "Planet/Zephyr/AndromedaZephyrManager.h"

#include "StarSystem.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"

// =========================================================
// SINGLETON
// =========================================================

FAndromedaZephyrManager& FAndromedaZephyrManager::Get()
{
    static FAndromedaZephyrManager Singleton;
    return Singleton;
}

// =========================================================
// PURE HELPERS
// =========================================================

FAndromedaZephyrSkyRange FAndromedaZephyrManager::ComputeSkyRange(
    float SurfaceRadius,
    float AtmosphereRadius,
    float RangeFactor
)
{
    FAndromedaZephyrSkyRange Range;

    // Sanitize: degenerate input yields a degenerate range
    // whose IsValid() is false (never silently "fixed").
    const float SafeSurface = SurfaceRadius;
    const float SafeAtmosphere = AtmosphereRadius;
    const float SafeFactor = FMath::Clamp(RangeFactor, 0.0f, 1.0f);

    Range.SurfaceRadius = SafeSurface;
    Range.AtmosphereRadius = SafeAtmosphere;
    Range.SkyRangeFactor = SafeFactor;

    const float SkyHeight = SafeAtmosphere - SafeSurface;
    Range.SkyHeight = SkyHeight;

    if (SafeSurface > 0.0f && SkyHeight > 0.0f && SafeFactor > 0.0f)
    {
        // Absolute invariant: min() guarantees
        // ZephyrSkyOuterRadius <= AtmosphereRadius.
        Range.ZephyrSkyOuterRadius = FMath::Min(
            SafeSurface + SkyHeight * SafeFactor,
            SafeAtmosphere
        );
    }
    else
    {
        Range.ZephyrSkyOuterRadius = SafeSurface;
    }

    return Range;
}

FAndromedaZephyrPlanetContext FAndromedaZephyrManager::BuildContext(
    const FPlanetRuntimeData& Planet,
    const FAndromedaAtmosphereInstance* AtmosphereOrNull,
    const FVector& StarWorldPosition,
    bool bHasValidStar
)
{
    FAndromedaZephyrPlanetContext Context;

    Context.PlanetID = Planet.PlanetID;
    Context.PlanetSeed = Planet.PlanetSeed;
    Context.ContextHash = FAndromedaZephyrPlanetContext::ComputeContextHash(
        Planet.PlanetSeed,
        Planet.PlanetID
    );

    // Planet frame: center + orientation travel with the
    // StarSystem snapshot, so the sky stays coherent while the
    // planet rotates and orbits. No World-Z alignment assumed.
    Context.PlanetCenter = Planet.WorldPosition;
    Context.PlanetRotation = Planet.CurrentRotation;
    Context.PlanetRotationAxis = Planet.RotationAxis;

    Context.PlanetRadius = Planet.PlanetRadius;
    Context.TerrainHeight = Planet.TerrainHeight;

    float SurfaceRadius = Planet.PlanetRadius;
    float AtmosphereRadius =
        (Planet.PlanetRadius + Planet.TerrainHeight)
        * AtmosphereRadiusMultiplierMirror;

    if (AtmosphereOrNull != nullptr)
    {
        // Authoritative ATMOS radii (registry convention:
        // SurfaceRadius = PlanetRadius,
        // AtmosphereRadius = (R + T) * 1.1).
        SurfaceRadius = AtmosphereOrNull->Parameters.SurfaceRadius;
        AtmosphereRadius = AtmosphereOrNull->Parameters.AtmosphereRadius;
    }

    Context.SkyRange = ComputeSkyRange(SurfaceRadius, AtmosphereRadius);

    // Shared star data (same snapshot ATMOS uses; never a
    // second/fictitious star). Invalid star -> night side MUST
    // stay dark: consumers gate direct light on bHasValidStar.
    Context.StarWorldPosition = StarWorldPosition;
    Context.bHasValidStar = bHasValidStar
        && !StarWorldPosition.IsZero();

    // Climate placeholders for the future Weather chain
    // (ZEPHYR-03+). Deterministic per planet identity so the
    // same seed always yields the same base sky context.
    // Kept neutral in ZEPHYR-01: no weather, no clouds.
    const uint32 H = Context.ContextHash;
    Context.ClimateArchetype = static_cast<uint8>(H & 0xFFu);
    Context.TemperatureBias = 0.0f;
    Context.HumidityBias = 0.0f;

    return Context;
}

// =========================================================
// SYNC
// =========================================================

int32 FAndromedaZephyrManager::SyncFromSnapshots(
    const TArray<FPlanetRuntimeData>& Planets,
    const TArray<FAndromedaAtmosphereInstance>& Atmospheres,
    const FVector& StarWorldPosition,
    bool bHasValidStar
)
{
    TMap<int64, FAndromedaZephyrPlanetContext> Rebuilt;
    Rebuilt.Reserve(Planets.Num());

    // The registry places each atmosphere exactly at its
    // planet's WorldPosition, so center proximity is the
    // robust match key (independent of registration order or
    // debug-name parsing). Tolerance is generous in absolute
    // terms (1 cm) yet far below any planet radius.
    constexpr double MatchToleranceCm = 1.0;
    constexpr double MatchToleranceSq = MatchToleranceCm * MatchToleranceCm;

    for (const FPlanetRuntimeData& Planet : Planets)
    {
        if (!Planet.bValid || Planet.PlanetID == 0 || Planet.PlanetRadius <= 0.0f)
        {
            continue;
        }

        const FAndromedaAtmosphereInstance* Matched = nullptr;
        double BestDistSq = MatchToleranceSq;

        for (const FAndromedaAtmosphereInstance& Atmosphere : Atmospheres)
        {
            const double DistSq =
                (Atmosphere.WorldPosition - Planet.WorldPosition).SizeSquared();
            if (DistSq <= BestDistSq)
            {
                BestDistSq = DistSq;
                Matched = &Atmosphere;
            }
        }

        FAndromedaZephyrPlanetContext Context = BuildContext(
            Planet,
            Matched,
            StarWorldPosition,
            bHasValidStar
        );

        if (!Context.IsValid())
        {
            UE_LOG(
                LogAndromedaAtmos,
                Warning,
                TEXT("FAndromedaZephyrManager: rejected invalid context for planet %lld (SurfaceRadius=%.1f AtmosphereRadius=%.1f)."),
                Planet.PlanetID,
                Context.SkyRange.SurfaceRadius,
                Context.SkyRange.AtmosphereRadius
            );
            continue;
        }

        Rebuilt.Add(Planet.PlanetID, Context);
    }

    FScopeLock ScopeLock(&ContextLock);
    Contexts = MoveTemp(Rebuilt);
    return Contexts.Num();
}

// =========================================================
// QUERIES
// =========================================================

bool FAndromedaZephyrManager::FindContext(
    int64 PlanetID,
    FAndromedaZephyrPlanetContext& OutContext
) const
{
    FScopeLock ScopeLock(&ContextLock);

    const FAndromedaZephyrPlanetContext* Found = Contexts.Find(PlanetID);
    if (Found == nullptr)
    {
        return false;
    }

    OutContext = *Found;
    return true;
}

void FAndromedaZephyrManager::GetContextSnapshot(
    TArray<FAndromedaZephyrPlanetContext>& OutSnapshot
) const
{
    FScopeLock ScopeLock(&ContextLock);
    Contexts.GenerateValueArray(OutSnapshot);
}

int32 FAndromedaZephyrManager::GetNumContexts() const
{
    FScopeLock ScopeLock(&ContextLock);
    return Contexts.Num();
}

int32 FAndromedaZephyrManager::Clear()
{
    FScopeLock ScopeLock(&ContextLock);

    const int32 RemovedCount = Contexts.Num();
    Contexts.Empty();
    return RemovedCount;
}
