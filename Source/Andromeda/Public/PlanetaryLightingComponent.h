#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetaryLightingComponent.generated.h"


class AActor;


UCLASS(
    ClassGroup = (Andromeda),
    meta = (BlueprintSpawnableComponent)
)
class ANDROMEDA_API UPlanetaryLightingComponent : public UActorComponent
{
    GENERATED_BODY()


public:

    UPlanetaryLightingComponent();


protected:

    virtual void BeginPlay() override;

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;


public:

    // =========================================================
    // STAR REFERENCE
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|Planetary Lighting"
    )
    TObjectPtr<AActor> StarActor;


    // =========================================================
    // LIGHTING PARAMETERS
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planetary Lighting"
    )
    float MinimumIllumination = 0.15f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planetary Lighting"
    )
    float MaximumIllumination = 1.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planetary Lighting"
    )
    float ReferenceDistance = 10000000.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planetary Lighting"
    )
    float DistanceCompensation = 1.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Planetary Lighting"
    )
    float NightAmbient = 0.08f;


    // =========================================================
    // CURRENT LIGHTING STATE
    // =========================================================

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|Planetary Lighting"
    )
    float CurrentIllumination = 1.0f;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|Planetary Lighting"
    )
    float CurrentStarDistance = 0.0f;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|Planetary Lighting"
    )
    FVector CurrentStarDirection = FVector::ForwardVector;


    // =========================================================
    // STAR CONNECTION
    // =========================================================

    void SetStarActor(
        AActor* InStarActor
    );


protected:

    // =========================================================
    // LIGHTING UPDATE
    // =========================================================

    void UpdatePlanetaryLighting();
};