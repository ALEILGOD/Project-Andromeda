#include "Sun.h"

#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureCube.h"
#include "HillaireStarLinkComponent.h"
#include "StarSystem.h"
#include "UObject/ConstructorHelpers.h"

ASun::ASun()
{
    // Tick enabled so HillaireStarLinkComponent can push the live
    // sun direction to the atmosphere feed. Use TG_PostUpdateWork to
    // run after planetary lighting components have updated their
    // CurrentStarDirection.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

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

    ConfigureSunMesh();

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

    // =========================================================
    // HILLAIRE STAR LINK (ATMOS WIRING)
    //
    // Plain ActorComponent owned by the star Actor itself (never by a
    // mesh): every spawned Sun feeds the Hillaire light feed with its
    // own live direction/intensity/color. No second lighting system.
    // =========================================================

    HillaireStarLink =
        CreateDefaultSubobject<UHillaireStarLinkComponent>(
            TEXT("HillaireStarLink")
        );
}

void ASun::BeginPlay()
{
    Super::BeginPlay();

    // Set stable star ID from owning star system
    if (AStarSystem* StarSystem = Cast<AStarSystem>(GetOwner()))
    {
        StableStarId = StarSystem->GetStableStarId();
    }

    ConfigureSunLight();
    ConfigureSunMesh();
}

void ASun::OnConstruction(
    const FTransform& Transform
)
{
    Super::OnConstruction(
        Transform
    );

    ConfigureSunLight();
    ConfigureSunMesh();
}

void ASun::ConfigureSunMesh()
{
    if (!SunMesh)
    {
        return;
    }

    // CLEAN SLATE: no sky renderer replaces the decorative mesh,
    // so it stays visible as authored.
    SunMesh->SetVisibility(
        true,
        true
    );

    SunMesh->SetHiddenInGame(
        false
    );
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