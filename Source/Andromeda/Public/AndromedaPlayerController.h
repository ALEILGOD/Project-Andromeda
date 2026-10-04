#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AndromedaPlayerController.generated.h"

/** Uses Unreal's input scaling, but consumes look deltas in the local quaternion frame. */
UCLASS()
class ANDROMEDA_API AAndromedaPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void UpdateRotation(float DeltaTime) override;
    virtual void SetControlRotation(const FRotator& NewRotation) override;
    /** Publication bypasses the external-request setter; never feeds back into navigation. */
    void PublishPlanetaryView(const FRotator& ViewRotation);
};
