#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AndromedaAtmosphereSystem.h"

class FRDGBuilder;
class FSceneView;
struct FScreenPassTexture;
struct FPostProcessMaterialInputs;

// =========================================================
// UNIFIED ANDROMEDA ATMOSPHERE RENDERER (PHASE 2.1)
// =========================================================
// The ONE atmosphere renderer of Project Andromeda (§13).
//
// There is exactly ONE Tonemap subscription, owned by
// FUnifiedAtmosphereViewExtension, dispatching two ordered
// internal STAGES of a single render pass over a SINGLE
// unified snapshot (FAndromedaAtmosphereSystem):
//
//   Stage A — Aerial / limb (legacy ATMOS raymarch):
//             aerial perspective on geometry + limb radiance.
//             Reads the unified snapshot through a derived
//             Rayleigh-only view (documented limitation).
//   Stage B — Sky / LUT (Hillaire Case A pipeline):
//             Transmittance -> MultiScatter -> SkyView -> Sky PS,
//             governing planet, sun disk, debug visuals.
//
// Both stages share the same planets, the same star, the same
// camera-relative convention. No double snapshot, no second
// mailbox, no independent hook. Order A->B is deterministic.
//
// FOLLOW-UP (explicit, not in this phase): absorb Stage A into
// the LUT pass (transmittance-based aerial + depth), then remove
// the AtmosEnabled cross-gate in ZephyrSky.usf.
// =========================================================

// Per-planet camera relation of the last rendered frame.
// Recorded on the render thread by the stage wrappers; consumed
// by r.AndromedaAtmosphere.Status / DebugMode 1-4. Distances are
// computed in FVector/double precision on the CPU.
struct FUnifiedAtmosphereFramePlanetInfo
{
    int64 PlanetID = 0;
    // Camera distance to planet center (cm).
    double DistanceCm = 0.0;
    // Camera height above ground, negative when buried (cm).
    double HeightAboveGroundCm = 0.0;
    // True when the camera sits inside the atmosphere shell.
    bool bInsideAtmosphere = false;
};

class ANDROMEDA_API FUnifiedAtmosphereRenderer
{
public:
    // =========================================================
    // LIFECYCLE (called from FAndromedaModule, game thread)
    // =========================================================
    static void Initialize();
    static void Shutdown();
    static bool IsInitialized();

    // =========================================================
    // SINGLE TONEMAP ENTRY — ordered internal stages
    // =========================================================
    static FScreenPassTexture RenderAerialStage(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs);

    static FScreenPassTexture RenderSkyStage(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs);

    // =========================================================
    // RUNTIME PROOF (§23) — real counters, no estimates
    // =========================================================
    static uint64 GetAerialPassCount();
    static uint64 GetSkyPassCount();

    // Last-frame camera relation (render thread writers,
    // thread-safe copy out for diagnostics).
    static void GetLastFrameInfo(
        TArray<FUnifiedAtmosphereFramePlanetInfo>& OutPlanets,
        int32& OutGoverningIndex,
        uint64& OutSnapshotVersion
    );

private:
    static void HandlePostEngineInit();

    static void HandleWorldCleanup(
        UWorld* InWorld,
        bool bSessionEnded,
        bool bCleanupResources
    );

    // True when r.AndromedaAtmosphere.Enable != 0 (single gate).
    static bool UnifiedPassEnabled();

    static void UpdateLastFrameInfo(const FSceneView& View);

    static FDelegateHandle PostEngineInitDelegateHandle;

    static FDelegateHandle WorldCleanupDelegateHandle;

    static bool bInitialized;
};
