#pragma once

#include "CoreMinimal.h"
#include "HillaireAtmosphereProfile.generated.h"

/**
 * HILLAIRE ATMOSPHERE - PER-PLANET PROFILE (Phase 1).
 *
 * Pure data, serializable, no rendering dependency. Mirrors the reference
 * AtmosphereInfo (SetupEarthAtmosphere) field-for-field in SEMANTICS:
 * radii, Rayleigh/Mie/absorption sigmas, two-layer-equivalent density terms,
 * phase excentricity, twilight cutoff, ground albedo and the white-transfer
 * solar convention (ILLUMINANCE_IS_ONE: solar = white, LUTs act as transfer
 * factors; runtime sun color/irradiance is applied at render time).
 *
 * All length units are KILOMETERS. Coefficients are per-km.
 *
 * The profile OWNS the radii (BottomRadiusKm/TopRadiusKm): it is the single
 * source of truth consumed by LUT keys and the scattering core. Planet state
 * mirrors them as derived convenience values (see FHillairePlanetState).
 * Never hardcode Earth sizes elsewhere: sizes always flow from a profile.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireAtmosphereProfile
{
	GENERATED_BODY()

	/** Center -> ground, km (reference: bottom_radius = 6360). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Radii", meta = (ClampMin = "0.001"))
	float BottomRadiusKm = 6360.0f;

	/** Center -> atmosphere top, km (reference: top_radius = 6460, ~Karman). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Radii", meta = (ClampMin = "0.001"))
	float TopRadiusKm = 6460.0f;

	/** Rayleigh scattering coefficients, 1/km (reference: 0.005802, 0.013558, 0.033100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Rayleigh")
	FVector RayleighScatteringKm = FVector(0.005802, 0.013558, 0.033100);

	/**
	 * Rayleigh exponential density scale (reference: rayleigh_density layer[1].w
	 * = -1/8 with an 8 km scale height). Negative: density falls with height.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Rayleigh")
	float RayleighExpScale = -0.125f;

	/** Mie scattering coefficients, 1/km (reference: 0.003996 uniform). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Mie")
	FVector MieScatteringKm = FVector(0.003996, 0.003996, 0.003996);

	/** Mie extinction coefficients, 1/km (reference: 0.004440 uniform). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Mie")
	FVector MieExtinctionKm = FVector(0.004440, 0.004440, 0.004440);

	/** Mie absorption = extinction - scattering (reference: derived, kept explicit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Mie")
	FVector MieAbsorptionKm = FVector(0.000444, 0.000444, 0.000444);

	/** Mie exponential density scale (reference: -1/1.2, 1.2 km scale height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Mie")
	float MieExpScale = -0.833333f;

	/** Mie phase excentricity (reference: mie_phase_function_g = 0.8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Mie", meta = (ClampMin = "-0.99", ClampMax = "0.99"))
	float MiePhaseG = 0.8f;

	/** Ozone absorption extinction, 1/km (reference: 0.000650, 0.001881, 0.000085). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Absorption")
	FVector AbsorptionExtinctionKm = FVector(0.000650, 0.001881, 0.000085);

	/**
	 * Absorption tent profile (reference: absorption_density layers).
	 * Layer 0 below AbsorptionWidthKm, layer 1 above; density = Linear*h + Constant.
	 * Reference: width 25 km, L0 = h/15 - 2/3, L1 = -h/15 + 8/3.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Absorption")
	float AbsorptionWidthKm = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Absorption")
	float AbsorptionLinear0 = 0.0666667f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Absorption")
	float AbsorptionConstant0 = -0.6666667f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Absorption")
	float AbsorptionLinear1 = -0.0666667f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Absorption")
	float AbsorptionConstant1 = 2.6666667f;

	/** Ground albedo (reference: black 0,0,0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Surface")
	FVector GroundAlbedo = FVector::ZeroVector;

	/** Twilight cutoff (reference: mu_s_min = cos(120 deg) ~= -0.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Surface", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MuSMin = -0.5f;

	/**
	 * Solar irradiance transfer convention (reference: normalized white sun).
	 * LUTs bake transfer factors; the runtime sun color/intensity multiplies later.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Sun")
	FVector SolarIrradiance = FVector(1.0, 1.0, 1.0);

	/** Structural validation: radii ordered, sigmas/extinctions non-negative, scales sane. */
	bool IsValid(FString* OutError = nullptr) const;

	/** Content key for LUT caching (reference: ComputeAtmosphereProfileHash contract). */
	uint64 ComputeContentHash() const;

	/** Validated reference profile (reference: SetupEarthAtmosphere, km units). */
	static FHillaireAtmosphereProfile MakeReferenceProfile();

	bool operator==(const FHillaireAtmosphereProfile& Other) const;
	bool operator!=(const FHillaireAtmosphereProfile& Other) const { return !(*this == Other); }
};

/**
 * Thickness normalization factor K = ReferenceAtmosphereThicknessKm / T.
 *
 * The density profile is normalized to the ATMOSPHERE ENVELOPE: with sigma' =
 * K * sigma and H' = H / K the vertical optical depth sigma * H is preserved
 * and the density shape matches the validated Earth reference in normalized
 * (h/T) coordinates on every planet size. T is floored at
 * MinAtmosphereThicknessKm (division guard only).
 */
HILLAIREATMOSPHERE_API float HillaireThicknessNormalizationFactor(
	float AtmosphereHeightKm);

/**
 * Centralized atmosphere builder (FASE 2 planet/atmosphere model).
 *
 * Conceptually this is the required BuildAtmosphereParameters(Planet, Star):
 * FHillaireAtmosphereProfile IS FPlanetAtmosphereParameters (radii +
 * Rayleigh/Mie/absorption sigmas + density scales + albedo + sun terms) and
 * FHillaireResolvedLight IS FAtmosphereLight (direction + radiance +
 * angular radius). Both LUT generation and final rendering consume the
 * Profile stored in FHillairePlanetState, so every caller of this function
 * shares one consistent atmosphere by construction.
 *
 * GEOMETRY CONTRACT (corrected atmospheric volume model):
 * - BottomRadiusKm = GroundRadiusKm, the planet's STABLE REFERENCE RADIUS
 *   (base sphere / sea level). It NEVER includes terrain: terrain protrudes
 *   upward into the volume exactly like the reference sample's heightmap
 *   protrudes into its 6360-6460 domain.
 * - TopRadiusKm = Bottom + max(AtmosphereHeightKm, self-similar optical
 *   thickness): the ENVELOPE is the complete mathematical volume in which
 *   density/scattering is defined, and it is a stable planetary sphere (not
 *   terrain-shaped). The density profile is normalized to it (K =
 *   ReferenceThickness / Envelope): the optically significant layer is a
 *   fixed fraction of the envelope, so the visible limb is an optical result
 *   of the profile, never of the terrain.
 * - The envelope may be raised by a stable containment floor (terrain bound
 *   + headroom, see HillaireBuildPlanetaryProfile) so the mountains and a
 *   camera standing on them are inside the volume; that extra volume is
 *   still part of the same profile scale (the density fills the envelope
 *   with the reference shape, which is what makes the sky present at terrain
 *   level).
 *
 * The reference case is the identity: Base = MakeReferenceProfile(),
 * GroundRadiusKm = 6360, AtmosphereHeightKm = 100 leaves the profile
 * unchanged (K = 1).
 *
 * This is the ONLY sanctioned path from authoring radii to a renderable
 * profile. Both single-write call sites (HillaireMakeExternalPlanetState for
 * the STARMAP seam, UHillaireAtmosphereComponent::BuildPlanetState for the
 * test/manual path) must go through here; never assign Bottom/TopRadiusKm
 * directly.
 */
HILLAIREATMOSPHERE_API FHillaireAtmosphereProfile HillaireBuildNormalizedProfile(
	const FHillaireAtmosphereProfile& BaseProfile,
	float GroundRadiusKm,
	float AtmosphereHeightKm);

/**
 * Planetary-volume builder (STARMAP seam).
 *
 * Same envelope-normalized density as HillaireBuildNormalizedProfile, with
 * the envelope derived from the profile itself plus a STABLE containment
 * margin:
 *
 *   SelfSimilarThickness = (Base.Top - Base.Bottom) * Ground / Base.Bottom
 *   SelfSimilarScale     = -1 / Base.RayleighExpScale * Ground / Base.Bottom
 *   ContainmentThick     = max(0, TerrainHeadroomKm)
 *                        + AtmosphereHeadroomScaleHeights * SelfSimilarScale
 *   TopRadiusKm          = Ground + max(SelfSimilarThickness, ContainmentThick)
 *
 * TerrainHeadroomKm is the planet's AUTHORED terrain bound (constant), not a
 * local/sampled terrain height: the volume is stable and persistent, it never
 * follows LYTHOS terrain, and it always encloses the mountains plus a few
 * (self-similar) scale heights so a camera standing on a peak is inside the
 * mathematical domain with sky above it. The bottom stays the reference
 * radius. The self-similar quantities only size the MINIMUM volume and the
 * containment margin; the density is then normalized to the final envelope,
 * so the profile shape and the optical depth stay the validated reference.
 */
HILLAIREATMOSPHERE_API FHillaireAtmosphereProfile HillaireBuildPlanetaryProfile(
	const FHillaireAtmosphereProfile& BaseProfile,
	float GroundRadiusKm,
	float TerrainHeadroomKm);
