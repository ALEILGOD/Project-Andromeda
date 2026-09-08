#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AndromedaAtmosphereTypes.h"


class FRDGBuilder;
class FSceneView;
struct FScreenPassTexture;
struct FPostProcessMaterialInputs;


// =========================================================
// ANDROMEDA ATMOSPHERE RENDERER
// =========================================================

class ANDROMEDA_API FAndromedaAtmosphereRenderer
{


public:

    // =========================================================
    // LIFECYCLE
    // =========================================================

    static void Initialize();

    static void Shutdown();

    static bool IsInitialized();


    // =========================================================
    // RENDERING (ATMOS-03)
    // =========================================================

    static FScreenPassTexture RenderAtmospheres(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs);


    static uint64 GetDispatchCount();


    // =========================================================
    // DIAGNOSTICS
    // =========================================================

    static bool ValidateShaderInfrastructure();


    // =========================================================
    // GPU DATA CONVERSION
    // =========================================================

    // Converts the CPU atmosphere snapshot into the packed GPU
    // representation used by the StructuredBuffer.
    static void BuildGPUData(
        const TArray<FAndromedaAtmosphereInstance>& Snapshot,
        TArray<FAndromedaAtmosphereGPUData>& OutGPUData);


private:

    static void RegisterShaderDirectoryMapping();

    static void HandlePostEngineInit();

    static FDelegateHandle PostEngineInitDelegateHandle;

    static bool bInitialized;
};
