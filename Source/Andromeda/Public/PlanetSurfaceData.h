#pragma once

#include "CoreMinimal.h"
#include "PlanetSurfaceData.generated.h"

USTRUCT(BlueprintType)
struct FPlanetSurfaceData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FVector Direction = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly)
    float Height = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float NormalizedHeight = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    FVector Normal = FVector::UpVector;
};