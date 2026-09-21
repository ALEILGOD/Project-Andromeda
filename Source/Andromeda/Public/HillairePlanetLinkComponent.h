#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HillairePlanetAtmosphereState.h"
#include "HillairePlanetLinkComponent.generated.h"


/**
 * STARMAP Planet -> Planetary Atmosphere link (Multiplanetary rebuild).
 *
 * Attach to a planet actor (APlanet or compatible Blueprint). Every tick it
 * reads the ACTUAL procedural planet values and pushes them into the
 * Hillaire planetary atmosphere subsystem:
 *
 * - World-space center: owner actor location (live; follows orbits).
 * - Reference ground radius (atmosphere Bottom): APlanet::PlanetRadius (the
 *   mesh BASE sphere / sea level) in cm, converted to km. Terrain is NOT
 *   folded in: the bottom is a STABLE planetary reference.
 * - Atmosphere top: reference ground + a profile-derived envelope
 *   (envelope-normalized density, floored by stable terrain containment +
 *   headroom). The envelope is a persistent planetary volume; the density
 *   keeps the reference shape over it, so the sky exists at terrain level and
 *   the visible limb is an optical fraction of the envelope. No seed
 *   multiplier, no terrain-driven shell.
 * - Surface height: APlanet::TerrainHeight in cm, converted to km (metadata:
 *   the authored peak headroom used for stable volume containment).
 * - Planet rotation: owner actor quaternion.
 * - Associated star: resolved from the planet's PlanetaryLightingComponent.
 *
 * Uses stable FGuid PlanetId from APlanet.PlanetID/PlanetSeed (NOT transient slot).
 */
UCLASS(
    ClassGroup = (Andromeda),
    meta = (BlueprintSpawnableComponent)
)
class ANDROMEDA_API UHillairePlanetLinkComponent : public UActorComponent
{
    GENERATED_BODY()


public:

    UHillairePlanetLinkComponent();


protected:

    virtual void BeginPlay() override;

    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason
    ) override;


public:

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;


    // =========================================================
    // PLANET GEOMETRY INPUTS (PURE)
    // =========================================================

    /**
     * Planetary atmospheric volume thickness in km:
     *   T = GroundRadiusKm * HillaireLimits::PlanetaryAtmosphereThicknessRatio (~0.10x Ground)
     * Target geometry: AtmosphereTop = PlanetReferenceRadius * 1.10.
     * Pure, persistent planetary sphere, terrain-independent.
     */
    static float ComputeAtmosphereOpticalThicknessKm(
        float GroundRadiusKm
    );


    /**
     * Stable atmosphere ENVELOPE thickness in km (bottom -> top):
     * Anchored to the planet reference radius, extends meaningfully beyond the planet (~1.10x).
     * Terrain exists inside the volume without terrain conforming.
     */
    static float ComputeAtmosphereEnvelopeThicknessKm(
        float GroundRadiusKm,
        float TerrainHeightKm
    );


    /**
     * Atmosphere bottom radius in km = the planet's STABLE REFERENCE RADIUS
     * (base sphere, sea level). Pure. Terrain is intentionally NOT folded in:
     * terrain protrudes upward into the atmospheric volume.
     */
    static float ComputeGroundRadiusKm(
        float RadiusCm,
        float TerrainCm
    );


    /**
     * Atmosphere-top radius in cm from live cm inputs:
     *   Top = Ground(base sphere) + Envelope(profile scale, terrain bound)
     * Pure; the previous visual [1.02, 1.05] multiplier is gone.
     */
    static float ComputeAtmosphereTopRadiusCm(
        float RadiusCm,
        float TerrainCm
    );


    /**
     * Read the live geometry from the owner and push it into the Hillaire
     * planetary atmosphere subsystem. Returns false when no valid geometry
     * is available (the registry keeps the last valid planet).
     */
    bool PushPlanetState();


    /**
     * Build the planet atmosphere state from explicit inputs (no world access).
     * Radii in km; folds them with the centralized thickness-normalized profile builder.
     *
     * ZEPHYR presentation seam: PlanetSeed/Archetype/OrbitDistanceCm select the
     * deterministic ZEPHYR climate identity, which tints ONLY the scattering
     * magnitudes of the reference profile (conservatively clamped). The
     * validated reference SHAPE (density profiles, phase, absorption tent) is
     * preserved and ATMOS physics is untouched.
     */
    static FPlanetAtmosphereState MakePlanetAtmosphereState(
        const FGuid& InPlanetId,
        const FName& InPlanetName,
        const FVector& CenterWS,
        const FQuat& RotationWS,
        float GroundRadiusKm,
        float AtmosphereHeightKm,
        float TerrainHeightKm,
        const FGuid& StarId,
        int64 PlanetSeed = 0,
        int32 Archetype = 0,
        float OrbitDistanceCm = 0.0f
    );


private:

    /**
     * Live ground radius in cm: APlanet::PlanetRadius first, then a
     * PlanetRadius float property via reflection. <= 0 when unavailable.
     */
    float ReadPlanetRadiusCm(
        const AActor* PlanetActor
    ) const;

    /**
     * Live terrain height in cm: APlanet::TerrainHeight first, then a
     * TerrainHeight float property via reflection. 0 when unavailable.
     */
    float ReadTerrainHeightCm(
        const AActor* PlanetActor
    ) const;

    /**
     * Live procedural seed: APlanet::PlanetSeed first, then a PlanetSeed
     * int64 property via reflection. 0 when unavailable (still yields a
     * deterministic in-range multiplier).
     */
    int64 ReadPlanetSeed(
        const AActor* PlanetActor
    ) const;

    /**
     * Live planet ID: APlanet::PlanetID first, then PlanetID int64 property.
     * Falls back to a deterministic hash of the seed if unavailable.
     */
    FGuid ReadPlanetId(
        const AActor* PlanetActor
    ) const;

    /**
     * Associated star ID: from PlanetaryLightingComponent's StarActor.
     */
    FGuid ReadStarId(
        const AActor* PlanetActor
    ) const;

    static float ReadFloatProperty(
        const AActor* Actor,
        const TCHAR* PropertyName
    );

    static int64 ReadInt64Property(
        const AActor* Actor,
        const TCHAR* PropertyName
    );
};