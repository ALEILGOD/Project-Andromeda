#include "PlanetAtmosphereComponent.h"

#include "PlanetAtmosphereRenderer.h"


namespace
{
    constexpr float AtmosphereTerrainClearance =
        10000.0f;
}


// =========================================================
// CONSTRUCTOR
// =========================================================

UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    SetMobility(
        EComponentMobility::Movable
    );
}


// =========================================================
// INITIALIZE ATMOSPHERE
// =========================================================

void UPlanetAtmosphereComponent::InitializeAtmosphere(
    float PlanetRadius,
    float TerrainHeight,
    int64 Seed
)
{
    // =========================================================
    // GROUND RADIUS
    // =========================================================

    Parameters.GroundRadius =
        PlanetRadius;


    // =========================================================
    // ATMOSPHERE HEIGHT
    // =========================================================

    const float RequiredAtmosphereHeight =
        TerrainHeight
        +
        AtmosphereTerrainClearance;


    Parameters.AtmosphereHeight =
        FMath::Max(
            Parameters.AtmosphereHeight,
            RequiredAtmosphereHeight
        );


    // =========================================================
    // ATMOSPHERE RADIUS
    // =========================================================

    Parameters.AtmosphereRadius =
        PlanetRadius
        +
        Parameters.AtmosphereHeight;


    // =========================================================
    // SEED
    // =========================================================

    Parameters.AtmosphereSeed =
        Seed;


    // =========================================================
    // COMPONENT TRANSFORM
    // =========================================================

    SetRelativeLocation(
        FVector::ZeroVector
    );


    SetRelativeRotation(
        FRotator::ZeroRotator
    );


    SetRelativeScale3D(
        FVector::OneVector
    );


    // =========================================================
    // STAR
    // =========================================================

    StarWorldPosition =
        FVector::ZeroVector;


    // =========================================================
    // CREATE RENDERER
    // =========================================================

    if (!Renderer)
    {
        Renderer =
            NewObject<UPlanetAtmosphereRenderer>(
                this,
                UPlanetAtmosphereRenderer::StaticClass()
            );
    }


    if (!Renderer)
    {
        return;
    }


    // =========================================================
    // PLANET WORLD POSITION
    // =========================================================

    const FVector PlanetWorldPosition =
        GetOwner()
        ?
        GetOwner()->GetActorLocation()
        :
        FVector::ZeroVector;


    // =========================================================
    // INITIALIZE RENDERER
    // =========================================================

    Renderer->Initialize(
        Parameters.GroundRadius,
        Parameters.AtmosphereRadius,
        Parameters.RayleighScattering,
        Parameters.MieScattering,
        Parameters.MieAnisotropy,
        Parameters.Absorption,
        Parameters.RayleighScaleHeight,
        Parameters.MieScaleHeight,
        StarWorldPosition,
        static_cast<uint8>(
            Parameters.Quality
            ),
        Parameters.AtmosphereSeed,
        PlanetWorldPosition,
        TerrainHeight
    );
}