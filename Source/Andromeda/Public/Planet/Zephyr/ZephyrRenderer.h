#pragma once

#include "CoreMinimal.h"
#include "Planet/Zephyr/ZephyrTypes.h"

class FRDGBuilder;
class FSceneView;
class UWorld;
struct FScreenPassTexture;
struct FPostProcessMaterialInputs;

// =========================================================
// ZEPHYR RENDERER (ZEPHYR-01)
// =========================================================
// Owns the Hillaire-class LUT pipeline and the background sky
// composite. Per view it:
//
//   1. Snapshots FZephyrManager (planet profiles + star).
//   2. Builds camera-relative GPU planet data (double->float,
//      ATMOS-04 convention; cm radii, km scale heights).
//   3. Maintains the cross-frame LUT history cache with
//      separated invalidation keys (ZEPHYR-01 section 18):
//        planet key = profile hashes + Mie/Absorption scales
//          -> Transmittance + Multi-Scattering LUTs (atlas);
//        view key   = planet key + quantized camera heights +
//                     quantized sun geometry + MultiScatterScale
//          -> Sky-View SS/MS LUTs (atlas).
//   4. Runs the fullscreen sky pass (any FSceneView: game,
//      PIE, editor viewport, photo mode - no PlayerController).
//
// LUT Cache Architecture: Single atlas per LUT type (Transmittance,
// Multi-Scatter, Sky-View SS, Sky-View MS) containing all visible
// planets' slices stacked vertically. Cache stores the last generated
// atlas for each LUT type. On planet-key change (profile/params) the
// planet-dependent atlases are regenerated; on view-key change
// (camera height/sun direction/MS scale) the Sky-View atlases are
// regenerated. This is a single-active-dataset + invalidation model,
// NOT a persistent multi-dataset cache. When the active planet set
// changes, the entire atlas is regenerated.
//
// Separation contract: the sky pass OVERWRITES sky pixels and
// passes geometry pixels through untouched, so the final image
// is order-independent with respect to the ATMOS foreground
// pass (opaque > ATMOS volume > ZEPHYR background).
class ANDROMEDA_API FZephyrRenderer
{
public:
    // LUT atlas geometry (width x slice height). Planet slices
    // stack vertically; all widths/heights are multiples of the
    // 8x8 compute group size.
    static constexpr int32 TransmittanceWidth = 256;
    static constexpr int32 TransmittanceSliceHeight = 64;
    static constexpr int32 MultiScatterWidth = 32;
    static constexpr int32 MultiScatterSliceHeight = 32;
    static constexpr int32 SkyViewWidth = 192;
    static constexpr int32 SkyViewSliceHeight = 112;

    // Maximum planets supported simultaneously. Limited by GPU
    // max 2D texture dimension (typically 16384 or 32768).
    // Dynamic calculation avoids arbitrary hardcoded limits.
    static int32 GetMaxPlanets();

    static void Initialize();
    static void Shutdown();
    static bool IsInitialized();

    // Tonemap-pass delegate (order-independent by construction).
    static FScreenPassTexture RenderSky(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs);

    static uint64 GetDispatchCount();
    static uint64 GetLUTRegenCount();

    static bool ValidateShaderInfrastructure();

    // CPU-side planet data build (exposed for diagnostics).
    static void BuildGPUData(
        const TArray<FZephyrPlanetSnapshotEntry>& Snapshot,
        const FVector& ViewOrigin,
        const FVector& StarWorldPosition,
        TArray<FZephyrPlanetGPUData>& OutGPUData
    );

private:
    static void RegisterShaderDirectoryMapping();
    static void HandlePostEngineInit();
    static void HandleWorldCleanup(
        UWorld* InWorld,
        bool bSessionEnded,
        bool bCleanupResources
    );

    static uint64 ComputePlanetKey(
        const TArray<FZephyrPlanetSnapshotEntry>& Snapshot,
        float MieScale,
        float AbsorptionScale
    );

    static uint64 ComputeViewKey(
        uint64 PlanetKey,
        const TArray<FZephyrPlanetGPUData>& GPUData,
        float MultiScatterScale
    );

    static FDelegateHandle PostEngineInitDelegateHandle;
    static FDelegateHandle WorldCleanupDelegateHandle;
    static bool bInitialized;
};
