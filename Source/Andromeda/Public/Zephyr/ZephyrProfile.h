#pragma once

#include "CoreMinimal.h"
#include "Planet/PlanetProfile.h"
#include "Zephyr/ZephyrTypes.h"
#include "ZephyrProfile.generated.h"

/**
 * ZEPHYR Planetary Climate Profile.
 *
 * Defines the atmospheric/climate identity of a planet:
 * - Sky appearance (colors, scattering parameters)
 * - Cloud layers and types
 * - Weather patterns and probabilities
 * - Climate zones and seasonal variation
 *
 * This is the authoritative data that ZEPHYR uses for rendering.
 * It is generated deterministically from planet seed/archetype/orbit.
 */
USTRUCT(BlueprintType)
struct ANDROMEDA_API FZephyrPlanetProfile
{
	GENERATED_BODY()

	// =========================================================
	// IDENTITY
	// =========================================================

	/** Planet identifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Identity")
	FGuid PlanetId;

	/** Planet seed (deterministic generation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Identity")
	int64 PlanetSeed = 0;

	/** Planet number within star system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Identity")
	int64 PlanetNumber = 0;

	/** Archetype determining base climate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Identity")
	EPlanetArchetype Archetype = EPlanetArchetype::Terran;

	/** Orbit distance from star (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Identity")
	float OrbitDistanceCm = 0.0f;

	// =========================================================
	// PHYSICAL GEOMETRY (mirrors ATMOS profile - authoritative)
	// =========================================================
	// GroundRadiusKm is the planetary REFERENCE radius (base sphere / sea level,
	// ATMOS Bottom). Terrain is never folded in; mountains protrude into the
	// volume. Overwritten every frame from the authoritative ATMOS snapshot;
	// these defaults only size templates.

	/** Planetary reference radius, km (ATMOS Bottom / GroundRadiusKm, from ATMOS). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Geometry")
	float GroundRadiusKm = 6371.0f;

	/** Atmosphere top radius, km == reference + profile/containment envelope (from ATMOS). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Geometry")
	float AtmosphereTopRadiusKm = 7008.1f;

	// =========================================================
	// SKY APPEARANCE (visual only, does not affect ATMOS physics)
	// =========================================================

	/** Sky appearance parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky")
	FZephyrSkyAppearance SkyAppearance;

	// =========================================================
	// CLIMATE / WEATHER
	// =========================================================

	/** Base weather state for this planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Climate")
	FZephyrWeatherState BaseWeatherState;

	/** Seasonal variation strength [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Climate", meta = (ClampMin = "0", ClampMax = "1"))
	float SeasonalVariation = 0.3f;

	/** Daily weather variation speed (cycles per day). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Climate", meta = (ClampMin = "0", ClampMax = "10"))
	float WeatherVariationSpeed = 0.5f;

	/** Latitude bands for climate zones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Climate")
	TArray<float> ClimateZoneBoundaries;

	/** Weather types per climate zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Climate")
	TArray<EZephyrWeatherType> ClimateZoneWeather;

	/** Cloud layer templates for this planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Climate")
	TArray<FZephyrCloudLayer> CloudLayerTemplates;

	// =========================================================
	// TRANSITION
	// =========================================================
	// No transition knobs live here. The ZEPHYR transition is a pure function
	// of the authoritative geometry: ZephyrComputeTransitionFactor(ViewRadius,
	// PlanetAtmosphereRange, AtmosphereTopRadius). The transition width is the
	// real shell thickness and scales with the planet.

	// =========================================================
	// RUNTIME (not serialized)
	// =========================================================

	/** Content hash for change detection. */
	UPROPERTY(Transient)
	uint64 ContentHash = 0;

	/** Whether profile has been initialized. */
	UPROPERTY(Transient)
	bool bInitialized = false;

	FZephyrPlanetProfile() = default;

	/** Compute content hash for LUT invalidation. */
	uint64 ComputeContentHash() const;

	/** Check if profile is valid for rendering. */
	bool IsValid() const;

	/** Get atmosphere thickness in km. */
	float GetAtmosphereThicknessKm() const { return AtmosphereTopRadiusKm - GroundRadiusKm; }
};

/**
 * ZEPHYR Profile Library - deterministic generation from seed/archetype/orbit.
 */
UCLASS(BlueprintType)
class ANDROMEDA_API UZephyrProfileLibrary : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Build a complete ZEPHYR planet profile from seed, archetype, and geometry.
	 * This is the main entry point for STARMAP -> ZEPHYR profile generation.
	 */
	UFUNCTION(BlueprintPure, Category = "Andromeda|Zephyr|Profile")
	static FZephyrPlanetProfile BuildProfile(
		int64 PlanetSeed,
		int64 PlanetID,
		EPlanetArchetype Archetype,
		float OrbitDistanceCm,
		float GroundRadiusKm,
		float AtmosphereTopRadiusKm
	);

	/**
	 * Get default profile for an archetype (used as base for generation).
	 */
	UFUNCTION(BlueprintPure, Category = "Andromeda|Zephyr|Profile")
	static FZephyrPlanetProfile GetArchetypeDefaultProfile(EPlanetArchetype Archetype);

	/**
	 * Generate sky appearance from archetype and orbit.
	 */
	static FZephyrSkyAppearance GenerateSkyAppearance(
		int64 Seed,
		EPlanetArchetype Archetype,
		float OrbitDistanceCm,
		float GroundRadiusKm
	);

	/**
	 * Archetype-only aesthetic scattering scales (no orbit/seed terms).
	 *
	 * Used by the ATMOS presentation seam to tint the physical atmosphere per
	 * planet without touching the validated scattering SHAPE. Returns neutral
	 * (1,1) for unknown archetypes.
	 */
	static void GetArchetypeScatteringScales(
		EPlanetArchetype Archetype,
		float& OutRayleighScale,
		float& OutMieScale);

	/**
	 * Generate base weather state from archetype and orbit.
	 */
	static FZephyrWeatherState GenerateBaseWeatherState(
		int64 Seed,
		EPlanetArchetype Archetype,
		float OrbitDistanceCm
	);

	/**
	 * Generate cloud layer templates from archetype.
	 */
	static TArray<FZephyrCloudLayer> GenerateCloudLayerTemplates(
		int64 Seed,
		EPlanetArchetype Archetype,
		float AtmosphereThicknessKm
	);

	/**
	 * Generate climate zones from archetype.
	 */
	static void GenerateClimateZones(
		int64 Seed,
		EPlanetArchetype Archetype,
		TArray<float>& OutZoneBoundaries,
		TArray<EZephyrWeatherType>& OutZoneWeather
	);

	/**
	 * Deterministic random from seed + salt (FNV-1a based).
	 */
	static uint32 DeterministicRandom(uint64 Seed, uint32 Salt);

	/**
	 * Deterministic float in [0, 1] from seed + salt.
	 */
	static float DeterministicUnitRandom(uint64 Seed, uint32 Salt);

	/**
	 * Deterministic float in [Min, Max] from seed + salt.
	 */
	static float DeterministicRangeRandom(uint64 Seed, uint32 Salt, float Min, float Max);

	/**
	 * Deterministic choice from array.
	 */
	template<typename T>
	static T DeterministicChoice(uint64 Seed, uint32 Salt, const TArray<T>& Choices)
	{
		if (Choices.Num() == 0) return T();
		uint32 Rand = DeterministicRandom(Seed, Salt);
		return Choices[Rand % Choices.Num()];
	}
};