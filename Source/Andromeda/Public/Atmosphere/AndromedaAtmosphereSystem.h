#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Planet/Zephyr/ZephyrTypes.h"

// =========================================================
// ANDROMEDA ATMOSPHERE SYSTEM (PHASE 2.1 — UNIFIED)
// =========================================================
// The ONE atmosphere system of Project Andromeda.
//
// Phase 2.0 left two independent systems sharing the same planets:
//   - FAndromedaAtmosphereManager (ATMOS, handle-based, Rayleigh-only data)
//   - FZephyrManager              (ZEPHYR, mailbox, full-physics data)
//
// Both are now DEPRECATED as data owners. This class is the single
// game-thread -> render-thread mailbox for the whole atmosphere:
//
//   AAndromedaAtmosphereRegistry (game thread, auto-registration)
//       -> FAndromedaAtmosphereSystem::SetSnapshot()
//       -> FUnifiedAtmosphereRenderer stages (render thread)
//
// SNAPSHOT CONTENT (per planet, full physics):
//   FAndromedaAtmosphereInstance carries everything any stage needs:
//     PlanetID, PlanetCenter, PlanetRotation, GroundRadius,
//     AtmosphereRadius, AtmosphereProfile, TerrainHeightCm,
//     SkyTransitionRadiusCm. The star lives once at system level
//     (StarWorldPosition). No Actor, no Component, no Material,
//     no Blueprint, no manual LUT is ever required: the Registry
//     derives every instance from FPlanetRuntimeData (STARMAP).
//
// TRANSITIONAL ALIASES:
//   The unified instance/profile types are promoted ZEPHYR types.
//   A physical rename (FZephyr* -> FAndromedaAtmosphere*) is planned
//   after visual validation; until then these aliases ARE the
//   canonical unified names used by all new code.
// =========================================================

DECLARE_LOG_CATEGORY_EXTERN(LogAndromedaAtmosphere, Log, All);

// Full-physics atmosphere profile of ONE planet (§7).
// (Promoted unified name; implementation = FZephyrPlanetProfile.)
using FAndromedaAtmosphereProfile = FZephyrPlanetProfile;

// Runtime atmosphere state of ONE planet (§6).
// (Promoted unified name; implementation = FZephyrPlanetSnapshotEntry.)
using FAndromedaAtmosphereInstance = FZephyrPlanetSnapshotEntry;

// =========================================================
// UNIFIED DEBUG MODES (§22)
// =========================================================
// Driven by r.AndromedaAtmosphere.DebugMode (unified console).
// Modes 0/5/6/7/8 drive real sky-pass visuals (forwarded to the
// LUT pipeline); modes 1-4 report real per-planet diagnostics
// through r.AndromedaAtmosphere.Status / the DebugMode log itself.
// No mode is documented without an implementation behind it.
enum class EAndromedaAtmosphereDebugMode : uint8
{
    // Full unified composite (aerial stage + LUT sky). Default.
    Normal = 0,
    // Atmosphere regions: inside / transition / outside per planet
    // (text diagnostics from the last rendered frame + normal sky).
    Regions = 1,
    // Planet selection: governing planet + camera distances
    // (text diagnostics from the last rendered frame + normal sky).
    Selection = 2,
    // SunDirPlanet per planet in the planet-local frame
    // (text diagnostics; rotation-verification aid).
    SunDirection = 3,
    // Active profile per planet: hashes + coefficients
    // (text diagnostics; isolation-verification aid).
    Profile = 4,
    // Raw Transmittance LUT slice (visual).
    Transmittance = 5,
    // Raw Sky-View LUT fullscreen (visual).
    SkyView = 6,
    // Raw Multi-Scattering LUT slice (visual).
    AtlasDataset = 7,
    // Multiple-scattering sky component only (visual).
    MultiScatter = 8
};

// =========================================================
// ANDROMEDA ATMOSPHERE SYSTEM — SINGLE MAILBOX
// =========================================================
class ANDROMEDA_API FAndromedaAtmosphereSystem
{
public:
    static FAndromedaAtmosphereSystem& Get();

    // Game thread: replace the whole snapshot (planets + star).
    // Called ONLY by AAndromedaAtmosphereRegistry.
    void SetSnapshot(
        const TArray<FAndromedaAtmosphereInstance>& InPlanets,
        const FVector& InStarWorldPosition
    );

    // Game thread: drop everything (world teardown).
    void Clear();

    // Render thread (or any thread): copy out the current snapshot.
    void GetSnapshot(
        TArray<FAndromedaAtmosphereInstance>& OutPlanets,
        FVector& OutStarWorldPosition,
        uint64& OutVersion
    ) const;

    // Any thread: current planet count (cheap validation gate).
    int32 GetPlanetCount() const;

    // Any thread: current snapshot version (cache/proof aid).
    uint64 GetVersion() const;

private:
    FAndromedaAtmosphereSystem() = default;

    mutable FCriticalSection SnapshotLock;

    TArray<FAndromedaAtmosphereInstance> Planets;

    FVector StarWorldPosition = FVector::ZeroVector;

    uint64 SnapshotVersion = 0;
};
