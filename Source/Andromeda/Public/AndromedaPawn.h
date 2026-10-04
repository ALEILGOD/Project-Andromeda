#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "AndromedaPawnMovement.h"
#include "AndromedaPawn.generated.h"

/** DefaultPawn supplies collision/input bindings only. Its floating physics and
 * world-Euler view pipeline are replaced by the planetary movement/controller. */
UCLASS(config=Game, Blueprintable, BlueprintType)
class ANDROMEDA_API AAndromedaPawn : public ADefaultPawn
{
    GENERATED_BODY()
public:
    AAndromedaPawn(const FObjectInitializer& ObjectInitializer);
    virtual void MoveForward(float Value) override;
    virtual void MoveRight(float Value) override;
    virtual void MoveUp_World(float Value) override;
    virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    virtual FRotator GetViewRotation() const override;
    virtual void PawnClientRestart() override;

    void ApplyLocalLook(const FRotator& InputDelta);
    void TransportNavigation(const FPlanetaryFieldSample& Field, double DeltaTime);
    FVector GetMovementIntent(double SurfaceInfluence) const;
    double GetAscentInput() const { return IsMoveInputIgnored() ? 0.0 : FMath::Clamp(LocalInput.Z, 0.0, 1.0); }
    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;
    void PublishView();
    /** Explicit cinematic/debug/spawn look request, separate from published control output. */
    void SetExternalView(const FQuat& WorldView);
    const FQuat& GetNavigationQuat() const { return NavigationQuat; }

    UFUNCTION(BlueprintPure, Category="Andromeda|ReferenceFrame")
    FVector GetPlanetaryUp() const;
    UFUNCTION(BlueprintPure, Category="Andromeda|ReferenceFrame")
    float GetPlanetaryInfluence() const;

    /** Player base movement velocity (cm/s), stored on the movement component. See UAndromedaPawnMovement::BaseVelocity. */
    UFUNCTION(BlueprintPure, Category="Andromeda|Movement")
    float GetBaseVelocity() const;
    UFUNCTION(BlueprintCallable, Category="Andromeda|Movement")
    void SetBaseVelocity(float NewBaseVelocity);
    /** Deep-space cruise velocity (cm/s). See UAndromedaPawnMovement::SpaceBaseVelocity. */
    UFUNCTION(BlueprintPure, Category="Andromeda|Movement")
    float GetSpaceBaseVelocity() const;
    UFUNCTION(BlueprintCallable, Category="Andromeda|Movement")
    void SetSpaceBaseVelocity(float NewSpaceBaseVelocity);

protected:
    virtual void BeginPlay() override;

private:
    void InitializeNavigation();
    FQuat NavigationQuat = FQuat::Identity;
    double LookPitchDegrees = 0.0;
    FVector LocalInput = FVector::ZeroVector;
    FVector PreviousFieldUp = FVector::ZeroVector;
    double PreviousOrientationInfluence = 0.0;
    bool bNavigationInitialized = false;
};
