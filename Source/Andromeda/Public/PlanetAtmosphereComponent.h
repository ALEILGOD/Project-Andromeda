#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetAtmosphereComponent.generated.h"


class UMaterialInterface;
class UPlanetAtmosphereRenderer;


// =========================================================
// ATMOSPHERE QUALITY
// =========================================================

UENUM(BlueprintType)
enum class EPlanetAtmosphereQuality : uint8
{
    Disabled UMETA(DisplayName = "Disabled"),
    Far UMETA(DisplayName = "Far"),
    Medium UMETA(DisplayName = "Medium"),
    Near UMETA(DisplayName = "Near"),
    Ultra UMETA(DisplayName = "Ultra")
};


// =========================================================
// ATMOSPHERE PARAMETERS
// =========================================================

USTRUCT(BlueprintType)
struct FPlanetAtmosphereParameters
{
    GENERATED_BODY()


    // =========================================================
    // GEOMETRY
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float GroundRadius = 500000.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float AtmosphereRadius = 550000.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float AtmosphereHeight = 50000.0f;


    // =========================================================
    // RAYLEIGH
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Rayleigh"
    )
    FVector RayleighScattering = FVector(
        0.0058f,
        0.0135f,
        0.0331f
    );


    // =========================================================
    // MIE
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Mie"
    )
    FVector MieScattering = FVector(
        0.003f,
        0.003f,
        0.003f
    );


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Mie"
    )
    float MieAnisotropy = 0.76f;


    // =========================================================
    // ABSORPTION
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Absorption"
    )
    FVector Absorption = FVector(
        0.0f,
        0.0f,
        0.0f
    );


    // =========================================================
    // DENSITY
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float RayleighScaleHeight = 8000.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float MieScaleHeight = 1200.0f;


    // =========================================================
    // QUALITY
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    EPlanetAtmosphereQuality Quality =
        EPlanetAtmosphereQuality::Near;


    // =========================================================
    // DETERMINISTIC SEED
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    int64 AtmosphereSeed = 0;
};


// =========================================================
// PLANET ATMOSPHERE COMPONENT
// =========================================================

UCLASS(
    ClassGroup = (Andromeda),
    BlueprintType,
    meta = (BlueprintSpawnableComponent)
)
class ANDROMEDA_API UPlanetAtmosphereComponent
    : public USceneComponent
{
    GENERATED_BODY()


public:

    UPlanetAtmosphereComponent();


    // =========================================================
    // PARAMETERS
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    FPlanetAtmosphereParameters Parameters;


    // =========================================================
    // ATMOSPHERE MATERIAL
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    TObjectPtr<UMaterialInterface> AtmosphereMaterial;


    // =========================================================
    // STAR
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Lighting"
    )
    FVector StarWorldPosition =
        FVector::ZeroVector;


    // =========================================================
    // UAS RENDERER
    // =========================================================

    UPROPERTY(
        Transient,
        BlueprintReadOnly,
        Category = "Andromeda|UAS"
    )
    TObjectPtr<UPlanetAtmosphereRenderer> Renderer;


    // =========================================================
    // INITIALIZATION
    // =========================================================

    UFUNCTION(
        BlueprintCallable,
        Category = "Andromeda|Atmosphere"
    )
    void InitializeAtmosphere(
        float PlanetRadius,
        float TerrainHeight,
        int64 Seed
    );
};