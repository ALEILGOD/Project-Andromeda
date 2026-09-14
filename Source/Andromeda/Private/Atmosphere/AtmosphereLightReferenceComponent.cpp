#include "Atmosphere/AtmosphereLightReferenceComponent.h"

#include "Atmosphere/AndromedaAtmosphereSystem.h"

UAtmosphereLightReferenceComponent::UAtmosphereLightReferenceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    // Pure reference transform: identity relative to the Sun.
    // The DIRECTION is per-planet and computed on demand, so the
    // component itself carries no offset or orientation state.
    //
    // MOBILITY FIX (surgical): Movable, NOT Static. BP_Sun's Root is
    // non-static, and a Static child cannot attach to a non-static
    // parent (AttachTo runtime error in PIE). A Movable reference is
    // compatible with any parent mobility and changes nothing about
    // this component (pure query API, no physics, no rendering).
    // The Sun actor itself is never touched for this.
    SetMobility(EComponentMobility::Movable);
}

FVector UAtmosphereLightReferenceComponent::GetEmissionPointWorld() const
{
    const AActor* Owner = GetOwner();

    if (Owner == nullptr)
    {
        return FVector::ZeroVector;
    }

    return Owner->GetActorLocation();
}

FVector UAtmosphereLightReferenceComponent::ComputeDirectionTowardSunWorld(
    const FVector& EmissionPointWorld,
    const FVector& PlanetCenterWorld
)
{
    // Convention A (Planet -> Sun), world frame, double precision
    // until the returned value. Single owned implementation: every
    // atmospheric sun direction in the project comes from here.
    const FVector TowardSun = EmissionPointWorld - PlanetCenterWorld;

    if (TowardSun.SizeSquared() < 1e-12)
    {
        // Degenerate (planet coincident with the emission point:
        // physically impossible, numerically must never yield NaN
        // downstream). Deterministic fallback, same as the legacy
        // renderer fallback it replaces.
        return FVector(0.0f, 0.0f, 1.0f);
    }

    return TowardSun.GetSafeNormal();
}

FVector UAtmosphereLightReferenceComponent::GetDirectionTowardSunWorld(
    const FVector& PlanetCenterWorld
) const
{
    return ComputeDirectionTowardSunWorld(
        GetEmissionPointWorld(),
        PlanetCenterWorld
    );
}

FVector UAtmosphereLightReferenceComponent::GetLightTravelDirectionWorld(
    const FVector& PlanetCenterWorld
) const
{
    // Convention B is DERIVED here, once, from convention A.
    // No caller may re-derive or re-flip it elsewhere.
    return -GetDirectionTowardSunWorld(PlanetCenterWorld);
}

bool UAtmosphereLightReferenceComponent::IsReferenceValid() const
{
    return GetOwner() != nullptr;
}

FString UAtmosphereLightReferenceComponent::GetReferenceSummary() const
{
    const AActor* Owner = GetOwner();

    return FString::Printf(
        TEXT("AtmosphereLightReference owner=%s emission=%s convention=TowardSun(Planet->Sun, world)"),
        Owner != nullptr ? *Owner->GetName() : TEXT("<none>"),
        *GetEmissionPointWorld().ToString()
    );
}
