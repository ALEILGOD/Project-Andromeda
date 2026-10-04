#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "CollisionQueryParams.h"
#include "PlanetaryGravitySystem.h"
#include "AndromedaPawnMovement.generated.h"

class UPlanetaryWorldSubsystem;

/** Custom inertial integrator. FloatingPawnMovement's speed clamp, world KillZ
 * and collision-derived velocity overwrite are intentionally not inherited. */
UCLASS(ClassGroup=(Andromeda), meta=(BlueprintSpawnableComponent))
class ANDROMEDA_API UAndromedaPawnMovement : public UPawnMovementComponent
{
    GENERATED_BODY()
public:
    UAndromedaPawnMovement(const FObjectInitializer& ObjectInitializer);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual float GetMaxSpeed() const override { return BaseVelocity; }

    /**
     * Base movement velocity of the player (cm/s).
     * This is the cruise speed of the controllable propulsion channel:
     *     TargetControlVelocity = MovementIntent * BaseVelocity
     * which the existing inertial integrator then blends into the actual
     * Velocity (X/Y/Z) state alongside gravity, the planetary frame carrier
     * and collision impulses. It never limits inherited orbital/free-fall
     * momentum directly. Future space-speed modes will build on this value.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Andromeda|Movement", meta=(ClampMin="0.0"))
    float BaseVelocity = 1200.f;
    /**
     * Deep-space cruise velocity (cm/s). Fully available when the planetary
     * reference influence is 0 and blended geometrically down to BaseVelocity
     * as reference influence rises to 1 at the planet. Feeds only the
     * controlled-propulsion channel (same integrator, same collision, same
     * carrier); actual Velocity X/Y/Z stays continuous and inertial momentum
     * is never overwritten. Zero disables the boost (BaseVelocity everywhere).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Andromeda|Movement", meta=(ClampMin="0.0"))
    float SpaceBaseVelocity = 1000000.f;
    /** Legacy cruise-speed control, superseded by BaseVelocity (kept for serialized content; migrated in BeginPlay). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Andromeda|Movement", meta=(ClampMin="0.0", DeprecatedProperty, DeprecationMessage="Use BaseVelocity instead; MaxSpeed is migrated to BaseVelocity on BeginPlay."))
    float MaxSpeed = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Andromeda|Movement", meta=(ClampMin="0.0"))
    float Acceleration = 4000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Andromeda|Movement", meta=(ClampMin="0.0"))
    float Deceleration = 8000.f;
    /** Coulomb support/friction is derived from this maximum walkable incline. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Andromeda|Movement", meta=(ClampMin="0.0", ClampMax="80.0"))
    double WalkableSlopeDegrees = 50.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Andromeda|Movement")
    FPlanetaryMotionSettings Settings;

    UFUNCTION(BlueprintPure, Category="Andromeda|ReferenceFrame")
    float GetReferenceFrameInfluence() const { return float(Field.ReferenceInfluence); }
    UFUNCTION(BlueprintPure, Category="Andromeda|ReferenceFrame")
    FVector GetPlanetFrameVelocity() const { return CarrierVelocity; }
    UFUNCTION(BlueprintPure, Category="Andromeda|ReferenceFrame")
    FVector GetPlanetaryRelativeVelocity() const { return Velocity - Field.FrameVelocity; }
    UFUNCTION(BlueprintPure, Category="Andromeda|Movement")
    bool IsGrounded() const { return bGrounded; }
    const FPlanetaryFieldSample& GetField() const { return Field; }
    UFUNCTION(BlueprintPure, Category="Andromeda|ReferenceFrame")
    FPlanetaryFieldSample GetPlanetaryField() const { return Field; }

    /** Initial conditions for a spawner already in a moving frame. Accepted
     * only before the first integration tick, never used for frame transitions. */
    void InitializeMotion(const FVector& InitialWorldVelocity, const FVector& InitialCarrierVelocity);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void MoveWithMovingBodyCollision(double Dt, TConstArrayView<FPlanetRuntimeData> StartPlanets,
        TConstArrayView<FPlanetRuntimeData> EndPlanets, FVector& EndVelocity);
    bool SweepMotion(const FVector& Start, const FVector& End,
        TConstArrayView<FPlanetRuntimeData> StartPlanets, TConstArrayView<FPlanetRuntimeData> EndPlanets,
        double StartFraction, FHitResult& OutHit, FVector& OutContactVelocity, double& OutContactSkin) const;

    UPROPERTY(Transient)
    TObjectPtr<UPlanetaryWorldSubsystem> Registry;
    UPROPERTY(Transient)
    FPlanetaryFieldSample Field;
    FVector CarrierVelocity = FVector::ZeroVector;
    FVector ControlledVelocity = FVector::ZeroVector;
    double PreviousReferenceWeight = 0.0;
    bool bGrounded = false;
    bool bSimulationInitialized = false;
    uint64 PreviousBodySignature = 0;
    // Reserved and reused; each body is sampled from AStarSystem's own clock.
    TArray<FPlanetRuntimeData> StartSamples;
    TArray<FPlanetRuntimeData> MidSamples;
    TArray<FPlanetRuntimeData> EndSamples;
    FCollisionQueryParams SceneQueryParams;
};
