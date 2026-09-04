#include "Sun.h"

#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureCube.h"
#include "UObject/ConstructorHelpers.h"

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
    // La PointLight rimane la sorgente principale.
    // Le ombre della PointLight sono disabilitate perché a scala
    // astronomica producono shadow-map troppo piccole e instabili.
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
    // Lo SkyLight viene usato esclusivamente come fill molto
    // debole per evitare che il lato notturno diventi nero.
    //
    // Non deve sostituire la luce solare.
    // =========================================================

    static ConstructorHelpers::FObjectFinder<UTextureCube> DefaultCubeFinder(
        TEXT("/Engine/EngineResources/DefaultTextureCube")
    );

    if (DefaultCubeFinder.Succeeded())
    {
        AmbientCubemap =
            DefaultCubeFinder.Object;
    }

    SpaceAmbientLight =
        CreateDefaultSubobject<USkyLightComponent>(
            TEXT("SpaceAmbientLight")
        );

    SpaceAmbientLight->SetupAttachment(Root);

    SpaceAmbientLight->SetMobility(
        EComponentMobility::Movable
    );

    SpaceAmbientLight->bLowerHemisphereIsBlack = false;

    SpaceAmbientLight->LowerHemisphereColor =
        AmbientColor;

    SpaceAmbientLight->SetCastShadows(false);

    ConfigureSunLight();
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
    Super::OnConstruction(
        Transform
    );

    ConfigureSunLight();
}

void ASun::ConfigureSunLight()
{
    // =========================================================
    // PRIMARY SOLAR LIGHT
    // =========================================================

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

    // =========================================================
    // COSMIC AMBIENT FILL
    // =========================================================

    if (SpaceAmbientLight)
    {
        SpaceAmbientLight->SetIntensity(
            AmbientIntensity
        );

        SpaceAmbientLight->SetLightColor(
            AmbientColor
        );

        SpaceAmbientLight->bLowerHemisphereIsBlack = false;

        SpaceAmbientLight->LowerHemisphereColor =
            AmbientColor;

        if (AmbientCubemap)
        {
            SpaceAmbientLight->SourceType =
                ESkyLightSourceType::SLS_SpecifiedCubemap;

            SpaceAmbientLight->SetCubemap(
                AmbientCubemap
            );
        }

        SpaceAmbientLight->SetCastShadows(false);

        // Nessun RecaptureSky():
        // stiamo usando un cubemap specificato, non una cattura
        // dinamica della scena.
    }
}