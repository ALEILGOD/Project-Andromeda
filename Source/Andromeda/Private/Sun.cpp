#include "Sun.h"

#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"

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
    // VISUAL MESH
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
    // PRIMARY SOLAR LIGHT
    //
    // A scala astronomica (centinaia di migliaia di km):
    // 1. NON usiamo shadow map dalla PointLight: proietterebbero
    //    solo ~37 pixel sull'intero pianeta, causando aliasing a
    //    gradini da 270 metri e pop-in binario quando la camera si avvicina.
    // 2. La transizione giorno/notte della sfera e' naturale e continua
    //    grazie ai normali di superficie (N . L).
    // 3. Disattiviamo il falloff quadratico puro (1/d^2 in cm) per evitare
    //    l'azzeramento dei pianeti esterni e numeri a 12 cifre.
    // =========================================================

    SunLight =
        CreateDefaultSubobject<UPointLightComponent>(
            TEXT("SunLight")
        );

    SunLight->SetupAttachment(Root);

    SunLight->SetMobility(
        EComponentMobility::Movable
    );

    SunLight->SetCastShadows(false);

    SunLight->bUseInverseSquaredFalloff = false;
    SunLight->LightFalloffExponent = 1.0f;

    SunLight->SetIntensity(
        LightIntensity
    );

    SunLight->SetAttenuationRadius(
        LightAttenuationRadius
    );

    SunLight->SetSourceRadius(
        LightSourceRadius
    );

    SunLight->SetSoftSourceRadius(
        LightSoftSourceRadius
    );

    // =========================================================
    // COSMIC AMBIENT / STARLIGHT FILL
    //
    // Nello spazio profondo l'assenza totale di luce ambiente
    // rende il lato notturno un vuoto nero assoluto (0.0), tagliando
    // il terminatore a rasoio. Lo SpaceAmbientLight fornisce
    // la luce diffusa delle stelle dell'universo, ammorbidendo
    // la transizione verso la notte ed evidenziando la sagoma 3D.
    // =========================================================

    SpaceAmbientLight =
        CreateDefaultSubobject<USkyLightComponent>(
            TEXT("SpaceAmbientLight")
        );

    SpaceAmbientLight->SetupAttachment(Root);

    SpaceAmbientLight->SetMobility(
        EComponentMobility::Movable
    );

    SpaceAmbientLight->SetIntensity(
        AmbientIntensity
    );

    SpaceAmbientLight->SetLightColor(
        AmbientColor
    );

    SpaceAmbientLight->SetCastShadows(false);
}

void ASun::BeginPlay()
{
    Super::BeginPlay();

    ConfigureSunLight();
}

void ASun::OnConstruction(
    const FTransform& Transform
)
{
    Super::OnConstruction(Transform);

    ConfigureSunLight();
}

void ASun::ConfigureSunLight()
{
    if (SunLight)
    {
        SunLight->SetIntensity(
            LightIntensity
        );

        SunLight->SetAttenuationRadius(
            LightAttenuationRadius
        );

        SunLight->SetCastShadows(false);

        SunLight->bUseInverseSquaredFalloff = false;
        SunLight->LightFalloffExponent = 1.0f;

        SunLight->SetSourceRadius(
            LightSourceRadius
        );

        SunLight->SetSoftSourceRadius(
            LightSoftSourceRadius
        );
    }

    if (SpaceAmbientLight)
    {
        SpaceAmbientLight->SetIntensity(
            AmbientIntensity
        );

        SpaceAmbientLight->SetLightColor(
            AmbientColor
        );

        SpaceAmbientLight->SetCastShadows(false);
    }
}