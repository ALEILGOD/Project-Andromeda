#include "Sun.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"


ASun::ASun()
{
    PrimaryActorTick.bCanEverTick = false;


    // =========================================================
    // ROOT
    // =========================================================

    Root =
        CreateDefaultSubobject<USceneComponent>(
            TEXT("Root")
        );

    SetRootComponent(Root);


    // =========================================================
    // SUN MESH
    // =========================================================

    SunMesh =
        CreateDefaultSubobject<UStaticMeshComponent>(
            TEXT("SunMesh")
        );

    SunMesh->SetupAttachment(Root);

    SunMesh->SetCollisionEnabled(
        ECollisionEnabled::NoCollision
    );

    SunMesh->SetCastShadow(false);


    // =========================================================
    // SUN LIGHT
    // =========================================================

    SunLight =
        CreateDefaultSubobject<UPointLightComponent>(
            TEXT("SunLight")
        );

    SunLight->SetupAttachment(Root);

    SunLight->SetMobility(
        EComponentMobility::Movable
    );

    SunLight->SetCastShadows(true);


    // =========================================================
    // INITIAL POINT LIGHT SETTINGS
    // =========================================================

    SunLight->SetIntensity(
        LightIntensity
    );

    SunLight->SetAttenuationRadius(
        LightAttenuationRadius
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


    // =========================================================
    // POINT LIGHT
    // =========================================================

    SunLight->SetIntensity(
        LightIntensity
    );


    SunLight->SetAttenuationRadius(
        LightAttenuationRadius
    );


    SunLight->SetCastShadows(
        true
    );
}