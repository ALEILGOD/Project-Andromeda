#pragma once

#include "CoreMinimal.h"
#include "AndromedaZephyrTypes.generated.h"

// =========================================================
// ZEPHYR-01 — PLANETARY SKY FOUNDATION (types)
// =========================================================
//
// ZEPHYR is the planetary-sky authority; ATMOS remains the
// authoritative atmospheric-transport system (Rayleigh,
// transmittance, optical depth, star transport, multiple
// scattering). ZEPHYR consumes ATMOS data and MUST NOT
// duplicate the transport physics.
//
// Planet data source (read-only snapshots, never owned here):
//     FPlanetRuntimeData (StarSystem.h):
//         PlanetID / PlanetSeed ........ identity (deterministic)
//         PlanetRadius / TerrainHeight .. geometry
//         WorldPosition ................ PlanetCenter (follows
//                                        orbit every frame)
//         CurrentRotation / RotationAxis  planet orientation
//                                        (rotation coherent)
// Atmosphere data source:
//     FAndromedaAtmosphereInstance
//     (Atmosphere/AndromedaAtmosphereTypes.h):
//         Parameters.SurfaceRadius ..... = PlanetRadius
//         Parameters.AtmosphereRadius .. = (R + T) * 1.1
//                                        (registry convention,
//                                        MUST NOT be changed)
//         WorldPosition ................ planet center twin
//
// Sky range authority (absolute invariant):
//     ZephyrSkyOuterRadius <= AtmosphereRadius   ALWAYS.
// See FAndromedaZephyrManager::SkyRangeFactor (0.85).
//
// Future chain (prepared, NOT implemented in ZEPHYR-01):
//     PlanetSeed -> Zephyr Planet Context -> Climate/Weather
//     -> Clouds / Rain / Storms
// =========================================================

// =========================================================
// SKY DOMAIN (planet-centered classification)
// =========================================================
// Distances are ALWAYS measured from the real planet center:
//     Altitude = |Position - PlanetCenter| - SurfaceRadius
// Never camera distance, never world Y, never world origin.
UENUM(BlueprintType)
enum class EAndromedaZephyrSkyDomain : uint8
{
    // Observer/point below the physical surface.
    BelowSurface    UMETA(DisplayName = "Below Surface"),

    // Inner shell owned by ZEPHYR:
    // SurfaceRadius < dist <= ZephyrSkyOuterRadius.
    PlanetarySky    UMETA(DisplayName = "Planetary Sky (Zephyr)"),

    // Outer transition band owned by ATMOS:
    // ZephyrSkyOuterRadius < dist <= AtmosphereRadius.
    // Same transport system: NO visible boundary here.
    OuterAtmosphere UMETA(DisplayName = "Outer Atmosphere (ATMOS)"),

    // Beyond ATMOS.
    Space           UMETA(DisplayName = "Space")
};

// =========================================================
// SKY RANGE
// =========================================================
//     SkyHeight            = AtmosphereRadius - SurfaceRadius
//     ZephyrSkyOuterRadius = min(SurfaceRadius
//                                + SkyHeight * SkyRangeFactor,
//                                AtmosphereRadius)
// with 0 < SkyRangeFactor <= 1, hence by construction:
//     ZephyrSkyOuterRadius <= AtmosphereRadius.
USTRUCT(BlueprintType)
struct FAndromedaZephyrSkyRange
{
    GENERATED_BODY()

    // Physical surface radius (ATMOS convention: PlanetRadius).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Zephyr|SkyRange")
    float SurfaceRadius = 0.0f;

    // ATMOS outer radius. NEVER modified by ZEPHYR.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Zephyr|SkyRange")
    float AtmosphereRadius = 0.0f;

    // Atmosphere shell thickness (derived).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|SkyRange")
    float SkyHeight = 0.0f;

    // Relative share of the shell owned by the Zephyr sky
    // domain. Default 0.85 (see manager): strictly inside
    // ATMOS, leaving a continuous outer transition band.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Zephyr|SkyRange")
    float SkyRangeFactor = 0.85f;

    // Zephyr sky domain outer radius (derived, clamped).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|SkyRange")
    float ZephyrSkyOuterRadius = 0.0f;

    bool IsValid() const
    {
        return SurfaceRadius > 0.0f
            && AtmosphereRadius > SurfaceRadius
            && SkyHeight > 0.0f
            && SkyRangeFactor > 0.0f
            && SkyRangeFactor <= 1.0f
            && ZephyrSkyOuterRadius > SurfaceRadius
            && ZephyrSkyOuterRadius <= AtmosphereRadius;
    }
};

// =========================================================
// PLANET CONTEXT
// =========================================================
// Per-planet sky foundation. Deterministic: the same
// (PlanetSeed, PlanetID) pair always yields the same
// ContextHash / base sky context. Climate placeholders are
// carried for the future Weather chain only.
USTRUCT(BlueprintType)
struct FAndromedaZephyrPlanetContext
{
    GENERATED_BODY()

    // ---- Identity (from FPlanetRuntimeData) ----
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Identity")
    int64 PlanetID = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Identity")
    int64 PlanetSeed = 0;

    // Deterministic hash of (PlanetSeed, PlanetID).
    // Same identity -> same base sky context.
    // NOTE: uint32 is not Blueprint-exposed (by design: this
    // is a C++-only identity key), hence no BlueprintReadOnly.
    UPROPERTY(VisibleAnywhere, Category = "Andromeda|Zephyr|Identity")
    uint32 ContextHash = 0;

    // ---- Planet frame (rotation coherent) ----
    // Real planet center (follows orbit/rotation via the
    // StarSystem snapshot; never assumed at world origin,
    // never assumed axis-aligned with World Z).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Frame")
    FVector PlanetCenter = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Frame")
    FRotator PlanetRotation = FRotator::ZeroRotator;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Frame")
    FVector PlanetRotationAxis = FVector::ZeroVector;

    // ---- Geometry ----
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Geometry")
    float PlanetRadius = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Geometry")
    float TerrainHeight = 0.0f;

    // ---- Sky range (authority: see IsValid) ----
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Sky")
    FAndromedaZephyrSkyRange SkyRange;

    // ---- Star (shared with ATMOS, never duplicated) ----
    // World-space star position snapshot + validity flag.
    // bHasValidStar == false means "no stellar source":
    // night side MUST NOT invent sunlight.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Star")
    FVector StarWorldPosition = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Star")
    bool bHasValidStar = false;

    // ---- Climate placeholders (ZEPHYR-03+, not used yet) ----
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Climate")
    uint8 ClimateArchetype = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Climate")
    float TemperatureBias = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Zephyr|Climate")
    float HumidityBias = 0.0f;

    // ---- Planet-centered helpers (mirror of the .ush) ----
    // Altitude of a world position above the surface.
    float GetAltitudeAtPosition(const FVector& WorldPosition) const
    {
        return static_cast<float>(
            (WorldPosition - PlanetCenter).Size()
            - static_cast<double>(SkyRange.SurfaceRadius)
        );
    }

    float GetDistanceFromCenter(const FVector& WorldPosition) const
    {
        return static_cast<float>((WorldPosition - PlanetCenter).Size());
    }

    // Normalized altitude inside the Zephyr sky domain
    // (0 at surface, 1 at ZephyrSkyOuterRadius, clamped).
    float GetSkyAltitude01(float DistanceFromCenter) const
    {
        const float Denom = SkyRange.ZephyrSkyOuterRadius - SkyRange.SurfaceRadius;
        if (!(Denom > 0.0f))
        {
            return 0.0f;
        }
        return FMath::Clamp(
            (DistanceFromCenter - SkyRange.SurfaceRadius) / Denom,
            0.0f,
            1.0f
        );
    }

    EAndromedaZephyrSkyDomain GetDomainAtDistance(float DistanceFromCenter) const
    {
        if (DistanceFromCenter < SkyRange.SurfaceRadius)
        {
            return EAndromedaZephyrSkyDomain::BelowSurface;
        }
        if (DistanceFromCenter <= SkyRange.ZephyrSkyOuterRadius)
        {
            return EAndromedaZephyrSkyDomain::PlanetarySky;
        }
        if (DistanceFromCenter <= SkyRange.AtmosphereRadius)
        {
            return EAndromedaZephyrSkyDomain::OuterAtmosphere;
        }
        return EAndromedaZephyrSkyDomain::Space;
    }

    bool IsBuriedObserver(float DistanceFromCenter) const
    {
        return DistanceFromCenter
            < SkyRange.SurfaceRadius * (1.0f - 1e-4f);
    }

    bool IsValid() const
    {
        return PlanetID != 0
            && PlanetRadius > 0.0f
            && TerrainHeight >= 0.0f
            && SkyRange.IsValid();
    }

    // Deterministic 64->32 bit mix (splitmix64 finalizer):
    // same (Seed, ID) -> same hash on every run/platform.
    static uint32 ComputeContextHash(int64 PlanetSeed, int64 PlanetID)
    {
        uint64 Z = static_cast<uint64>(PlanetSeed)
            + static_cast<uint64>(PlanetID) * 0x9E3779B97F4A7C15ull;
        Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
        Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
        Z = Z ^ (Z >> 31);
        return static_cast<uint32>(Z ^ (Z >> 32));
    }
};
