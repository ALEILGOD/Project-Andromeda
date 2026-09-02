#include "PlanetAtmosphereComponent.h"

#include "PlanetAtmosphereRenderer.h"


namespace
{
    constexpr float AtmosphereTerrainClearance = 10000.0f;
}


UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
    PrimaryComponentTick.bCanEverTick = false;


    // =========================================================
    // DEFAULT TRANSFORM
    // =========================================================

    SetMobility(
        EComponentMobility::Movable
    );
}


void UPlanetAtmosphereComponent::InitializeAtmosphere(
    float PlanetRadius,
    float TerrainHeight,
    int64 Seed
)
{
    // =========================================================
    // PLANET GEOMETRY
    // =========================================================

    Parameters.GroundRadius =
        PlanetRadius;


    // =========================================================
    // ATMOSPHERE HEIGHT
    // =========================================================

    const float RequiredAtmosphereHeight =
        TerrainHeight
        + AtmosphereTerrainClearance;


    Parameters.AtmosphereHeight =
        FMath::Max(
            Parameters.AtmosphereHeight,
            RequiredAtmosphereHeight
        );


    Parameters.AtmosphereRadius =
        PlanetRadius
        + Parameters.AtmosphereHeight;


    // =========================================================
    // DETERMINISTIC SEED
    // =========================================================

    Parameters.AtmosphereSeed =
        Seed;


    // =========================================================
    // ATMOSPHERE TRANSFORM
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
    // STAR POSITION
    // =========================================================

    StarWorldPosition =
        FVector::ZeroVector;


    // =========================================================
    // CREATE UAS RENDERER
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
    // INITIALIZE UAS RENDERER
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
        Parameters.AtmosphereSeed
    );
}