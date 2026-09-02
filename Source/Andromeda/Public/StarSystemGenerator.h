#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "StarSystemGenerator.generated.h"

USTRUCT(BlueprintType)
struct FAndromedaInt64Vector
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int64 X = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int64 Y = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int64 Z = 0;
};

USTRUCT(BlueprintType)
struct FPlanetGenerationData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int64 PlanetID = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 PlanetSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 RadiusSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 TerrainSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 BiomeSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 VegetationSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 GeologicalSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 VolumetricSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    float PlanetRadius = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float TerrainHeight = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float ContinentalScale = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float MountainScale = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float DetailScale = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float MountainStrength = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float DetailStrength = 0.0f;
};

USTRUCT(BlueprintType)
struct FStarSystemData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int64 SystemSeed = 0;

    UPROPERTY(BlueprintReadOnly)
    FAndromedaInt64Vector SystemCoordinate;

    UPROPERTY(BlueprintReadOnly)
    int32 PlanetCount = 0;

    UPROPERTY(BlueprintReadOnly)
    TArray<FPlanetGenerationData> Planets;
};

UCLASS(BlueprintType)
class ANDROMEDA_API UStarSystemGenerator : public UObject
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System")
    static FStarSystemData GenerateSystem(
        int64 UniverseSeed,
        FAndromedaInt64Vector SystemCoordinate
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System")
    static bool VerifyDeterminism(
        int64 UniverseSeed,
        FAndromedaInt64Vector SystemCoordinate
    );
};