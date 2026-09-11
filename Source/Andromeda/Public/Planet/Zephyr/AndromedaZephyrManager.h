#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Planet/Zephyr/AndromedaZephyrTypes.h"

// Forward declarations (decoupling):
// ZEPHYR reads planet/atmosphere snapshots but owns neither
// system, so the full definitions stay in the .cpp.
struct FPlanetRuntimeData;
struct FAndromedaAtmosphereInstance;

// =========================================================
// ANDROMEDA ZEPHYR MANAGER (ZEPHYR-01 foundation)
// =========================================================
//
// Thread-safe singleton mirroring the FAndromedaAtmosphereManager
// pattern. Owns one FAndromedaZephyrPlanetContext per planet.
//
// Data flow (shared planet data, no duplication):
//     StarSystem (FPlanetRuntimeData snapshots)
//         + ATMOS registry (FAndromedaAtmosphereInstance)
//         + star world position (ATMOS manager snapshot)
//              --> SyncFromSnapshots (Game Thread)
//              --> per-planet contexts (Render/Game queries)
//
// Future ZEPHYR <-> ATMOS communication docks here; no
// complex messaging is built in ZEPHYR-01.
//
// Sky-range authority:
//     ZephyrSkyOuterRadius =
//         min(SurfaceRadius + SkyHeight * SkyRangeFactor,
//             AtmosphereRadius)
// The min() clamp plus 0 < Factor <= 1 makes
//     ZephyrSkyOuterRadius <= AtmosphereRadius
// hold for EVERY planet configuration, including very thin
// atmospheres (the range is relative, so it adapts).
// =========================================================
class ANDROMEDA_API FAndromedaZephyrManager
{
public:
    // SkyRangeFactor is NOT arbitrary. Real project parameters:
    //   PlanetRadius in [250k, 1200k] cm,
    //   TerrainHeight = 3.5%..9% of PlanetRadius,
    //   AtmosphereRadius = (R + T) * 1.1 (registry convention),
    // so the ATMOS shell is SkyHeight = 0.1*R + 1.1*T, i.e.
    // 13.85%..19.9% of the planet radius, and the effective
    // Rayleigh scale used by the shader is shell * 0.35
    // (default RayleighScaleHeight 800000 cm dwarfs any shell).
    // At 85% shell altitude the density is exp(-0.85/0.35) ~=
    // 0.088: still optically meaningful, while the outer 15%
    // band holds only the <= ~9% density tail. The band stays
    // thick enough in absolute terms (15% of a 35k..240k cm
    // shell = ~5k..36k cm) for ZEPHYR-02 to blend surface->space
    // continuity, and thin enough that no seam can form: both
    // sides run the SAME ATMOS transport.
    static constexpr float SkyRangeFactor = 0.85f;

    // Mirror of the ATMOS registry convention, used ONLY as a
    // fallback when no atmosphere instance is matched for a
    // planet. The authoritative radii always come from the
    // matched FAndromedaAtmosphereInstance. This constant MUST
    // track AAndromedaAtmosphereRegistry::AtmosphereRadiusMultiplier
    // (1.1); it never overrides it.
    static constexpr float AtmosphereRadiusMultiplierMirror = 1.1f;

    static FAndromedaZephyrManager& Get();

    // =========================================================
    // SYNC (Game Thread)
    // =========================================================
    // Rebuilds the context table from live snapshots.
    // Planets are matched to atmospheres by center proximity
    // (the registry places each atmosphere at its planet's
    // WorldPosition). Unmatched planets fall back to derived
    // radii; unmatched atmospheres are ignored.
    // Returns the number of planet contexts after the sync.
    int32 SyncFromSnapshots(
        const TArray<FPlanetRuntimeData>& Planets,
        const TArray<FAndromedaAtmosphereInstance>& Atmospheres,
        const FVector& StarWorldPosition,
        bool bHasValidStar
    );

    // =========================================================
    // QUERIES (any thread; snapshot copies)
    // =========================================================
    bool FindContext(
        int64 PlanetID,
        FAndromedaZephyrPlanetContext& OutContext
    ) const;

    void GetContextSnapshot(
        TArray<FAndromedaZephyrPlanetContext>& OutSnapshot
    ) const;

    int32 GetNumContexts() const;

    // Empties the context table. Returns removed count.
    int32 Clear();

    // =========================================================
    // PURE HELPERS (no state; testable; also used by Sync)
    // =========================================================
    static FAndromedaZephyrSkyRange ComputeSkyRange(
        float SurfaceRadius,
        float AtmosphereRadius,
        float RangeFactor = SkyRangeFactor
    );

    static FAndromedaZephyrPlanetContext BuildContext(
        const FPlanetRuntimeData& Planet,
        const FAndromedaAtmosphereInstance* AtmosphereOrNull,
        const FVector& StarWorldPosition,
        bool bHasValidStar
    );

private:
    FAndromedaZephyrManager() = default;

    mutable FCriticalSection ContextLock;

    TMap<int64, FAndromedaZephyrPlanetContext> Contexts;
};
