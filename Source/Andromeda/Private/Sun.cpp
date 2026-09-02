#include "Sun.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"

ASun::ASun()
{
    PrimaryActorTick.bCanEverTick = false;

    Root =
        CreateDefaultSubobject<USceneComponent>(
            TEXT("Root")
        );

    SetRootComponent(Root);

    SunMesh =
        CreateDefaultSubobject<UStaticMeshComponent>(
            TEXT("SunMesh")
        );

    SunMesh->SetupAttachment(Root);

    SunMesh->SetCollisionEnabled(
        ECollisionEnabled::NoCollision
    );

    SunMesh->SetCastShadow(false);

    SunLight =
        CreateDefaultSubobject<UPointLightComponent>(
            TEXT("SunLight")
        );

    SunLight->SetupAttachment(Root);

    SunLight->SetMobility(
        EComponentMobility::Movable
    );

    SunLight->SetCastShadows(true);
}

void ASun::BeginPlay()
{
    Super::BeginPlay();

    ConfigureSunLight();
}

void ASun::ConfigureSunLight()
{
    if (!SunLight)
    {
        return;
    }

    SunLight->SetIntensity(
        LightIntensity
    );

    SunLight->SetAttenuationRadius(
        LightAttenuationRadius
    );
}