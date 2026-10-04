#include "Sun.h"

#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureCube.h"
#include "HillaireStarLinkComponent.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "Engine/TextureCube.h"
#include "HAL/IConsoleManager.h"
#include "StarSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Zephyr/ZephyrLog.h"

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

UTextureCube* ASun::BuildNeutralAmbientCube(
    UObject* InOuter
)
{
    if (!InOuter)
    {
        return nullptr;
    }
    uint8 White[6 * 4];
    FMemory::Memset(White, 0xFF, sizeof(White));
    UTextureCube* Cube = NewObject<UTextureCube>(
        InOuter,
        NAME_None,
        RF_Transient
    );
    Cube->Source.Init(1, 1, 6, 1, TSF_BGRA8, White);
    Cube->SRGB = false;
    Cube->UpdateResource();
    return Cube;
}

bool ASun::IsUsableAmbientCube(
    const UTextureCube* Cube
)
{
    return Cube != nullptr
        && Cube->GetSizeX() > 0
        && Cube->GetSizeY() > 0;
}

void ASun::EnsureNeutralAmbientCube()
{    // Provenance check (NOT IsUsableAmbientCube: UTextureCube::GetSizeX
    // reads compiled platform data, unavailable pre-cook, so a valid
    // runtime carrier reports 0x0; non-null here means we built it).
    if (NeutralAmbientCube != nullptr)
    {
        return;
    }
    if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
    {
        return;
    }
    NeutralAmbientCube = BuildNeutralAmbientCube(this);
    ApplyAmbientCube();
}

void ASun::ApplyAmbientCube()
{
    if (!SpaceAmbientLight)
    {
        return;
    }
    UTextureCube* ChosenCube = IsUsableAmbientCube(AmbientCubemap)
        ? AmbientCubemap.Get()
        : NeutralAmbientCube.Get();
    if (ChosenCube)
    {
        SpaceAmbientLight->SourceType =
            ESkyLightSourceType::SLS_SpecifiedCubemap;

        SpaceAmbientLight->SetCubemap(
            ChosenCube
        );
    }
}

void ASun::Tick(
    float DeltaTime
)
{
    Super::Tick(
        DeltaTime
    );

    EnsureNeutralAmbientCube();
    PushSkyAmbient();
}

void ASun::PushSkyAmbient()
{
    if (!SpaceAmbientLight)
    {
        return;
    }

    // Dynamic atmospheric ambient (governing planet, GameThread cache).
    // Falls back to the authored static fill outside any atmosphere.
    FVector3f Transfer = FVector3f::ZeroVector;
    float SunElevCos = -3.0f;
    FVector3f SunIrradiance = FVector3f::ZeroVector;
    bool bHaveAmbient = false;
    if (const UWorld* World = GetWorld())
    {
        if (UHillairePlanetaryAtmosphereSubsystem* Sub =
            World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>())
        {
            bHaveAmbient = Sub->GetGoverningSkyAmbientTransfer(
                Transfer, SunElevCos, SunIrradiance);
        }
    }

    if (bHaveAmbient)
    {
        const HillaireLimits::FSkyAmbientLightState State =
            HillaireLimits::SkyAmbientLightState(
                Transfer, SunElevCos, SunIrradiance, SkyAmbientScale);
        const FLinearColor NewColor(State.Color.X, State.Color.Y, State.Color.Z, 1.0f);
        const float NewIntensity = State.Intensity;
        const bool bColorChanged =
            FMath::Abs(NewColor.R - LastPushedAmbientColor.R) > 1e-4f
            || FMath::Abs(NewColor.G - LastPushedAmbientColor.G) > 1e-4f
            || FMath::Abs(NewColor.B - LastPushedAmbientColor.B) > 1e-4f;
        const bool bIntensityChanged =
            FMath::Abs(NewIntensity - LastPushedAmbientIntensity) > 1e-4f;
        if (!bSkyAmbientActive || bColorChanged || bIntensityChanged)
        {
            SpaceAmbientLight->SetLightColor(NewColor);
            SpaceAmbientLight->SetIntensity(NewIntensity);
            LastPushedAmbientColor = NewColor;
            LastPushedAmbientIntensity = NewIntensity;
            bSkyAmbientActive = true;
            // Push log: first push, solar-state changes (>0.05 elev drift),
            // and a 600-push heartbeat. A handful of lines per session under
            // a static sun; proves per-state delivery without spamming a
            // running day/night cycle.
            static uint64 PushCount = 0;
            static float LastLoggedElev = 99.0f;
            ++PushCount;
            if (PushCount == 1 || (PushCount % 600) == 0
                || FMath::Abs(SunElevCos - LastLoggedElev) > 0.05f)
            {
                LastLoggedElev = SunElevCos;
                UE_LOG(LogZephyr, Log,
                    TEXT("[Sun] SkyAmbient push #%llu: color=(%.4f,%.4f,%.4f) intensity=%.5f sunElev=%.4f sunIrr=(%.3f,%.3f,%.3f)"),
                    PushCount, NewColor.R, NewColor.G, NewColor.B, NewIntensity,
                    SunElevCos, SunIrradiance.X, SunIrradiance.Y, SunIrradiance.Z);
            }
        }
        return;
    }

    if (bSkyAmbientActive)
    {
        SpaceAmbientLight->SetLightColor(AmbientColor);
        SpaceAmbientLight->SetIntensity(AmbientIntensity);
        LastPushedAmbientColor = AmbientColor;
        LastPushedAmbientIntensity = AmbientIntensity;
        bSkyAmbientActive = false;
    }
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
    // NOTE: no EnsureNeutralAmbientCube() here: ConfigureSunLight runs
    // inside the actor constructor (CDO + SpawnActorDeferred), where
    // NewObject is illegal. The carrier is ensured in BeginPlay and lazily
    // in PushSkyAmbient (both strictly post-construction).
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

        // Ambient source selection (Pass-4 fix): the authored cube may be
        // missing or empty (engine DefaultTextureCube loads 0x0: a
        // specified-cubemap skylight on an empty cube contributes nothing at
        // any intensity). Prefer a valid authored cube, else the runtime
        // neutral white carrier (hue always comes from the light color).
        // NOTE: UTextureCube::GetSizeX reads compiled platform data (null
        // until cooked/compiled), so the runtime-built carrier is trusted by
        // provenance (non-null), while the authored asset is size-checked.
        ApplyAmbientCube();

        SpaceAmbientLight->SetCastShadows(false);

        // Nessun RecaptureSky():
        // stiamo usando un cubemap specificato, non una cattura
        // dinamica della scena.
    }
}