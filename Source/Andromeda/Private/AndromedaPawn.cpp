#include "AndromedaPawn.h"
#include "AndromedaPlayerController.h"

#include "Camera/CameraTypes.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "PlanetaryMotionMath.h"

AAndromedaPawn::AAndromedaPawn(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UAndromedaPawnMovement>(Super::MovementComponentName))
{
    bUseControllerRotationYaw = false;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;
    // Only the root collision shape participates in physics.
    GetMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AAndromedaPawn::BeginPlay()
{
    Super::BeginPlay();
    InitializeNavigation();
}

void AAndromedaPawn::InitializeNavigation()
{
    if (!bNavigationInitialized)
    {
        // Spawn orientation is the inertial frame. Never restore world Z on exit.
        NavigationQuat = GetActorQuat().GetNormalized();
        bNavigationInitialized = true;
    }
}

void AAndromedaPawn::PawnClientRestart()
{
    Super::PawnClientRestart();
    InitializeNavigation();
    PublishView();
    if (Controller)
    {
        GetMovementComponent()->AddTickPrerequisiteActor(Controller);
    }
}

void AAndromedaPawn::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    GetMovementComponent()->AddTickPrerequisiteActor(NewController);
}

void AAndromedaPawn::UnPossessed()
{
    if (Controller)
    {
        GetMovementComponent()->RemoveTickPrerequisiteActor(Controller);
    }
    LocalInput = FVector::ZeroVector;
    Super::UnPossessed();
}

void AAndromedaPawn::MoveForward(float Value) { LocalInput.X = FMath::IsFinite(Value) ? Value : 0.f; }
void AAndromedaPawn::MoveRight(float Value) { LocalInput.Y = FMath::IsFinite(Value) ? Value : 0.f; }
void AAndromedaPawn::MoveUp_World(float Value) { LocalInput.Z = FMath::IsFinite(Value) ? Value : 0.f; }

void AAndromedaPawn::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
    // ControlRotation is an output of PublishView, never an actor-rotation input.
}

void AAndromedaPawn::ApplyLocalLook(const FRotator& InputDelta)
{
    InitializeNavigation();
    if (InputDelta.ContainsNaN())
    {
        return;
    }
    NavigationQuat = (NavigationQuat * FQuat(FVector::UpVector, FMath::DegreesToRadians(double(InputDelta.Yaw)))
        * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(double(InputDelta.Roll)))).GetNormalized();
    // A local ergonomic pitch limit (independent of world latitude or influence).
    // No world-Euler pitch/roll normalization and no changing input sensitivity.
    LookPitchDegrees = FMath::Clamp(LookPitchDegrees + InputDelta.Pitch, -89.5, 89.5);
    PublishView();
}

void AAndromedaPawn::TransportNavigation(const FPlanetaryFieldSample& Field, double DeltaTime)
{
    InitializeNavigation();
    const double Weight = Field.OrientationInfluence;
    const FVector Heading = NavigationQuat.GetAxisX();
    FQuat Delta = FQuat::Identity;
    const double SpinRate = Field.AngularVelocity.Size();
    if (SpinRate > UE_DOUBLE_SMALL_NUMBER)
    {
        Delta = FQuat(Field.AngularVelocity / SpinRate, SpinRate * DeltaTime);
    }
    if (!Field.LocalUp.IsNearlyZero() && Weight > 0.0)
    {
        if (!PreviousFieldUp.IsNearlyZero())
        {
            // Parallel transport after spin: spin and curvature do not apply
            // the same swing twice. No geographic yaw/north/pole singularity.
            const FQuat Curvature = PlanetaryMotion::Swing(Delta.RotateVector(PreviousFieldUp), Field.LocalUp, Heading);
            Delta = (FQuat::Slerp(FQuat::Identity, Curvature, FMath::Min(Weight, PreviousOrientationInfluence)) * Delta).GetNormalized();
        }
        const FVector TransportedUp = Delta.RotateVector(NavigationQuat.GetAxisZ());
        FQuat Alignment = PlanetaryMotion::Swing(TransportedUp, Field.LocalUp, Heading);
        const double Angle = Alignment.GetAngle();
        if (Angle > UE_DOUBLE_SMALL_NUMBER)
        {
            const auto* Movement = CastChecked<UAndromedaPawnMovement>(GetMovementComponent());
            const double MaxRate = FMath::DegreesToRadians(Movement->Settings.AlignmentDegreesPerSecond);
            // Acquisition rate vanishes with influence. Curvature transport is
            // not clamped, so aligned high-speed surface travel does not lag.
            const double Fraction = FMath::Min(1.0, MaxRate * Weight * DeltaTime / Angle);
            Delta = (FQuat::Slerp(FQuat::Identity, Alignment, Fraction) * Delta).GetNormalized();
        }
    }
    NavigationQuat = (Delta * NavigationQuat).GetNormalized();
    PreviousFieldUp = Field.LocalUp;
    PreviousOrientationInfluence = Weight;
    SetActorRotation(NavigationQuat);
}

FVector AAndromedaPawn::GetMovementIntent(double SurfaceInfluence) const
{
    if (IsMoveInputIgnored())
    {
        return FVector::ZeroVector;
    }
    // A quaternion pitch interpolation cannot collapse or invert an axis like
    // lerping/projection of nearly antipodal world directions can.
    const FQuat MovementPitch(FVector::RightVector,
        -FMath::DegreesToRadians(LookPitchDegrees) * (1.0 - SurfaceInfluence));
    const FQuat MovementBasis = NavigationQuat * MovementPitch;
    return (MovementBasis.GetAxisX() * LocalInput.X + MovementBasis.GetAxisY() * LocalInput.Y
        + MovementBasis.GetAxisZ() * LocalInput.Z).GetClampedToMaxSize(1.0);
}

FRotator AAndromedaPawn::GetViewRotation() const
{
    return (NavigationQuat * FQuat(FVector::RightVector, -FMath::DegreesToRadians(LookPitchDegrees))).Rotator();
}

void AAndromedaPawn::PublishView()
{
    if (auto* PlanetaryController = Cast<AAndromedaPlayerController>(Controller))
    {
        PlanetaryController->PublishPlanetaryView(GetViewRotation());
    }
    else if (Controller)
    {
        Controller->SetControlRotation(GetViewRotation());
    }
}

void AAndromedaPawn::SetExternalView(const FQuat& WorldView)
{
    if (!WorldView.ContainsNaN() && WorldView.SizeSquared() > UE_DOUBLE_SMALL_NUMBER)
    {
        NavigationQuat = WorldView.GetNormalized();
        LookPitchDegrees = 0.0;
        PreviousFieldUp = FVector::ZeroVector;
        PreviousOrientationInfluence = 0.0;
        bNavigationInitialized = true;
        SetActorRotation(NavigationQuat);
    }
}

void AAndromedaPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    OutResult.Location = GetActorLocation();
    OutResult.Rotation = GetViewRotation();
    // PlayerCameraManager remains responsible for FOV, postprocess and modifiers.
}

FVector AAndromedaPawn::GetPlanetaryUp() const
{
    return NavigationQuat.GetAxisZ();
}

float AAndromedaPawn::GetPlanetaryInfluence() const
{
    const auto* Movement = Cast<UAndromedaPawnMovement>(GetMovementComponent());
    return Movement ? float(Movement->GetField().ReferenceInfluence) : 0.f;
}

float AAndromedaPawn::GetBaseVelocity() const
{
    const auto* Movement = Cast<UAndromedaPawnMovement>(GetMovementComponent());
    return Movement ? Movement->BaseVelocity : 0.f;
}

void AAndromedaPawn::SetBaseVelocity(float NewBaseVelocity)
{
    if (auto* Movement = Cast<UAndromedaPawnMovement>(GetMovementComponent()))
    {
        Movement->BaseVelocity = FMath::Max(NewBaseVelocity, 0.f);
    }
}

float AAndromedaPawn::GetSpaceBaseVelocity() const
{
    const auto* Movement = Cast<UAndromedaPawnMovement>(GetMovementComponent());
    return Movement ? Movement->SpaceBaseVelocity : 0.f;
}

void AAndromedaPawn::SetSpaceBaseVelocity(float NewSpaceBaseVelocity)
{
    if (auto* Movement = Cast<UAndromedaPawnMovement>(GetMovementComponent()))
    {
        Movement->SpaceBaseVelocity = FMath::Max(NewSpaceBaseVelocity, 0.f);
    }
}
