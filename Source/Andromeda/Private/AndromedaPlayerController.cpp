#include "AndromedaPlayerController.h"
#include "AndromedaPawn.h"

void AAndromedaPlayerController::SetControlRotation(const FRotator& NewRotation)
{
    if (!NewRotation.ContainsNaN())
    {
        if (AAndromedaPawn* PlanetaryPawn = Cast<AAndromedaPawn>(GetPawn()))
        {
            PlanetaryPawn->SetExternalView(NewRotation.Quaternion());
        }
        PublishPlanetaryView(NewRotation);
    }
}

void AAndromedaPlayerController::PublishPlanetaryView(const FRotator& ViewRotation)
{
    Super::SetControlRotation(ViewRotation);
    // Base controller suppresses changes below 0.001 degrees. Control is an
    // exact output here, including tiny curvature changes near poles.
    ControlRotation = ViewRotation;
}

void AAndromedaPlayerController::UpdateRotation(float DeltaTime)
{
    if (AAndromedaPawn* PlanetaryPawn = Cast<AAndromedaPawn>(GetPawn()))
    {
        PlanetaryPawn->ApplyLocalLook(RotationInput);
        // PlayerController resets RotationInput at the end of PlayerTick.
        // ProcessViewRotation's world-Euler clamps must not reinterpret this view.
    }
    else
    {
        Super::UpdateRotation(DeltaTime);
    }
}
