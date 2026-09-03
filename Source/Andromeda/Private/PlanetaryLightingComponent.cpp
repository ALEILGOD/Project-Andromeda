#include "PlanetaryLightingComponent.h"

#include "GameFramework/Actor.h"


UPlanetaryLightingComponent::UPlanetaryLightingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}


void UPlanetaryLightingComponent::BeginPlay()
{
    Super::BeginPlay();

    UpdatePlanetaryLighting();
}


void UPlanetaryLightingComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(
        DeltaTime,
        TickType,
        ThisTickFunction
    );

    UpdatePlanetaryLighting();
}


void UPlanetaryLightingComponent::SetStarActor(
    AActor* InStarActor
)
{
    StarActor = InStarActor;

    UpdatePlanetaryLighting();
}


void UPlanetaryLightingComponent::UpdatePlanetaryLighting()
{
    const AActor* PlanetActor = GetOwner();

    if (!PlanetActor)
    {
        return;
    }


    if (!StarActor)
    {
        CurrentStarDistance = 0.0f;
        CurrentStarDirection = FVector::ForwardVector;

        CurrentIllumination =
            FMath::Clamp(
                1.0f,
                MinimumIllumination,
                MaximumIllumination
            );

        return;
    }


    const FVector PlanetLocation =
        PlanetActor->GetActorLocation();

    const FVector StarLocation =
        StarActor->GetActorLocation();


    const FVector ToStar =
        StarLocation - PlanetLocation;


    const float DistanceSquared =
        ToStar.SizeSquared();


    if (DistanceSquared <= KINDA_SMALL_NUMBER)
    {
        CurrentStarDistance = 0.0f;
        CurrentStarDirection = FVector::ForwardVector;
        CurrentIllumination = MaximumIllumination;

        return;
    }


    CurrentStarDistance =
        FMath::Sqrt(
            DistanceSquared
        );


    CurrentStarDirection =
        ToStar.GetSafeNormal();


    // =========================================================
    // DISTANCE-BASED LIGHTING
    // =========================================================

    const float SafeReferenceDistance =
        FMath::Max(
            ReferenceDistance,
            1.0f
        );


    const float SafeDistance =
        FMath::Max(
            CurrentStarDistance,
            1.0f
        );


    /*
     * Normalized inverse-square illumination.
     *
     * At ReferenceDistance:
     *
     *     RawIllumination = 1.0
     *
     * Closer planets become brighter.
     * Farther planets become darker.
     */

    const float DistanceRatio =
        SafeReferenceDistance
        / SafeDistance;


    const float RawIllumination =
        DistanceRatio
        * DistanceRatio;


    // =========================================================
    // DISTANCE COMPENSATION
    // =========================================================

    /*
     * DistanceCompensation controls how strongly we
     * counteract the physical inverse-square falloff.
     *
     * 0.0 = completely physical distance response.
     *
     * 1.0 = completely normalized visual response.
     */

    const float Compensation =
        FMath::Clamp(
            DistanceCompensation,
            0.0f,
            1.0f
        );


    const float CompensatedIllumination =
        FMath::Lerp(
            RawIllumination,
            1.0f,
            Compensation
        );


    CurrentIllumination =
        FMath::Clamp(
            CompensatedIllumination,
            MinimumIllumination,
            MaximumIllumination
        );
}