#include "PlanetAtmosphereComponent.h"

#include "PlanetAtmosphereRenderer.h"


namespace
{
    constexpr float AtmosphereTerrainClearance =
        10000.0f;
}


UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

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
    Parameters.GroundRadius =
        PlanetRadius;

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

    Parameters.AtmosphereSeed =
        Seed;

    SetRelativeLocation(
        FVector::ZeroVector
    );

    SetRelativeRotation(
        FRotator::ZeroRotator
    );

    SetRelativeScale3D(
        FVector::OneVector
    );

    StarWorldPosition =
        FVector::ZeroVector;

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

    const FVector PlanetWorldPosition =
        GetOwner()
        ? GetOwner()->GetActorLocation()
        : FVector::ZeroVector;

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
        PlanetWorldPosition
    );
}