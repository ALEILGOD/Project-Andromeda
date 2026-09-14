#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "AtmosphereLightReferenceComponent.generated.h"

// =========================================================
// ATMOSPHERE LIGHT DIRECTION CONVENTION
// =========================================================
// The two directions are NOT interchangeable. Exactly one of
// them feeds the atmosphere renderer:
//
//   TowardSun  = Planet -> Sun (direction TOWARD the source).
//                Used by: mu_s, nu, phase functions, sun disk,
//                occultation, SkyView lookup, aerial transport.
//   LightTravel = Sun -> Planet (photon propagation direction).
//                Derived as -TowardSun. Never sampled directly
//                by the atmosphere; provided for completeness so
//                no caller re-derives (and possibly flips) it.
UENUM(BlueprintType)
enum class EAtmosphereLightDirectionConvention : uint8
{
    // Planet -> Sun. THE atmosphere convention (Hillaire Case A).
    TowardSun   UMETA(DisplayName = "Planet To Sun"),
    // Sun -> Planet. Photon travel direction (-TowardSun).
    LightTravel UMETA(DisplayName = "Sun To Planet")
};

// =========================================================
// UAtmosphereLightReferenceComponent (SUN LIGHT REFERENCE)
// =========================================================
// Authoritative light-direction reference owned by ASun.
//
// "The physical direction from which sunlight comes."
//
// This is NOT a second sun, NOT a second light source, NOT a
// STARMAP duplicate: it performs no lighting and owns no
// simulation state. It is the SINGLE choke point that converts
// the Sun's world position into an explicit, convention-tagged
// light direction for the atmosphere system:
//
//   ASun (+ this reference, game thread)
//       -> GetDirectionTowardSunWorld(PlanetCenter)
//       -> FZephyrPlanetSnapshotEntry::SunDirectionWorld
//       -> FAndromedaAtmosphereSystem (mailbox)
//       -> BuildGPUData -> Packed6 -> GPU (render thread)
//
// The render thread NEVER touches this component: the direction
// is baked per planet into the snapshot on the game thread.
//
// Frame contract: everything returned here is WORLD SPACE.
// No planet rotation is applied here or downstream (Hillaire
// Case A directional sun over a rotation-symmetric shell with
// world-frame camera/geometry => sun stays world-frame).
// =========================================================
UCLASS(
    BlueprintType,
    ClassGroup = (Andromeda),
    meta = (BlueprintSpawnableComponent)
)
class ANDROMEDA_API UAtmosphereLightReferenceComponent : public USceneComponent
{
    GENERATED_BODY()

public:

    UAtmosphereLightReferenceComponent();

    // Emission point of the sunlight in world space.
    // By construction this is the owner Sun actor's location.
    UFUNCTION(BlueprintPure, Category = "Andromeda|Sun|AtmosphereReference")
    FVector GetEmissionPointWorld() const;

    // Convention A: Planet -> Sun (toward the source).
    // THIS is what the atmosphere renderer consumes.
    UFUNCTION(BlueprintPure, Category = "Andromeda|Sun|AtmosphereReference")
    FVector GetDirectionTowardSunWorld(
        const FVector& PlanetCenterWorld
    ) const;

    // Convention B: Sun -> Planet (photon travel direction).
    // Exactly -GetDirectionTowardSunWorld(...). Provided so no
    // caller ever hand-flips the vector again.
    UFUNCTION(BlueprintPure, Category = "Andromeda|Sun|AtmosphereReference")
    FVector GetLightTravelDirectionWorld(
        const FVector& PlanetCenterWorld
    ) const;

    // True when attached to a live Sun actor.
    UFUNCTION(BlueprintPure, Category = "Andromeda|Sun|AtmosphereReference")
    bool IsReferenceValid() const;

    // One-line human summary for r.AndromedaAtmosphere.SunReference.
    UFUNCTION(BlueprintPure, Category = "Andromeda|Sun|AtmosphereReference")
    FString GetReferenceSummary() const;

    // Owned choke-point computation (also used as the documented
    // fallback when no Sun actor exists: same function, explicit
    // emission point, no arbitrary reconstruction elsewhere).
    static FVector ComputeDirectionTowardSunWorld(
        const FVector& EmissionPointWorld,
        const FVector& PlanetCenterWorld
    );
};
