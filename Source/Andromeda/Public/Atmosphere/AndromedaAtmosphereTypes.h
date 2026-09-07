#pragma once

#include "CoreMinimal.h"
#include "AndromedaAtmosphereTypes.generated.h"


DECLARE_LOG_CATEGORY_EXTERN(LogAndromedaAtmos, Log, All);


// =========================================================
// ATMOSPHERE HANDLE
// =========================================================

USTRUCT(BlueprintType)
struct FAndromedaAtmosphereHandle
{
    GENERATED_BODY()


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|Atmosphere"
    )
    int32 Id = 0;


    bool IsValid() const
    {
        return Id != 0;
    }


    void Invalidate()
    {
        Id = 0;
    }


    static FAndromedaAtmosphereHandle Invalid()
    {
        return FAndromedaAtmosphereHandle();
    }


    bool operator==(const FAndromedaAtmosphereHandle& Other) const
    {
        return Id == Other.Id;
    }


    bool operator!=(const FAndromedaAtmosphereHandle& Other) const
    {
        return Id != Other.Id;
    }


    friend uint32 GetTypeHash(const FAndromedaAtmosphereHandle& Handle)
    {
        return ::GetTypeHash(Handle.Id);
    }
};


// =========================================================
// ATMOSPHERE PARAMETERS
// =========================================================

USTRUCT(BlueprintType)
struct FAndromedaAtmosphereParameters
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
    float SurfaceRadius = 500000.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    float AtmosphereRadius = 650000.0f;


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


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Rayleigh"
    )
    float RayleighScaleHeight = 8000.0f;


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
    FVector MieAbsorption = FVector::ZeroVector;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Mie"
    )
    float MieAnisotropy = 0.76f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Mie"
    )
    float MieScaleHeight = 1200.0f;


    // =========================================================
    // ABSORPTION
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere|Absorption"
    )
    FVector Absorption = FVector::ZeroVector;


    // =========================================================
    // DETERMINISTIC SEED
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    int64 AtmosphereSeed = 0;


    // =========================================================
    // VALIDATION
    // =========================================================

    bool IsValidConfiguration() const
    {
        return SurfaceRadius > 0.0f
            && AtmosphereRadius > SurfaceRadius
            && RayleighScaleHeight > 0.0f
            && MieScaleHeight > 0.0f
            && FMath::Abs(MieAnisotropy) < 1.0f;
    }
};


// =========================================================
// ATMOSPHERE INSTANCE DESCRIPTOR
// =========================================================

USTRUCT(BlueprintType)
struct FAndromedaAtmosphereInstanceDesc
{
    GENERATED_BODY()


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    FAndromedaAtmosphereParameters Parameters;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    FVector WorldPosition = FVector::ZeroVector;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Atmosphere"
    )
    FName DebugName = NAME_None;
};


// =========================================================
// ATMOSPHERE INSTANCE (registry data - not reflected)
// =========================================================

struct FAndromedaAtmosphereInstance
{
    FAndromedaAtmosphereHandle Handle;

    FName DebugName = NAME_None;

    FAndromedaAtmosphereParameters Parameters;

    FVector WorldPosition = FVector::ZeroVector;
};