#pragma once

#include "CoreMinimal.h"
#include "ZephyrTypes.generated.h"

/**
 * ZEPHYR - Planetary Atmospheric/Climate Environment (Andromeda).
 *
 * Architecture:
 *   STARMAP
 *   -> ZEPHYR: defines atmosphere/climate/weather/sky/clouds appearance
 *   -> ATMOS (HillaireAtmosphere): executes physical atmospheric scattering rendering
 *
 * ZEPHYR does NOT implement physical scattering. It provides the planetary
 * climate profile and visual environment that becomes visible rapidly
 * when the camera crosses the atmosphere boundary.
 */

UENUM(BlueprintType)
enum class EZephyrWeatherType : uint8
{
	Clear         UMETA(DisplayName = "Clear"),
	PartlyCloudy  UMETA(DisplayName = "Partly Cloudy"),
	Cloudy        UMETA(DisplayName = "Cloudy"),
	Overcast      UMETA(DisplayName = "Overcast"),
	LightRain     UMETA(DisplayName = "Light Rain"),
	Rain          UMETA(DisplayName = "Rain"),
	HeavyRain     UMETA(DisplayName = "Heavy Rain"),
	Storm         UMETA(DisplayName = "Storm"),
	Thunderstorm  UMETA(DisplayName = "Thunderstorm"),
	Snow          UMETA(DisplayName = "Snow"),
	Blizzard      UMETA(DisplayName = "Blizzard"),
	Fog           UMETA(DisplayName = "Fog"),
	Haze          UMETA(DisplayName = "Haze"),
	DustStorm     UMETA(DisplayName = "Dust Storm"),
	Custom        UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EZephyrCloudType : uint8
{
	None            UMETA(DisplayName = "None"),
	Cirrus          UMETA(DisplayName = "Cirrus"),
	Cirrocumulus    UMETA(DisplayName = "Cirrocumulus"),
	Cirrostratus    UMETA(DisplayName = "Cirrostratus"),
	Altocumulus     UMETA(DisplayName = "Altocumulus"),
	Altostratus     UMETA(DisplayName = "Altostratus"),
	Stratus         UMETA(DisplayName = "Stratus"),
	Stratocumulus   UMETA(DisplayName = "Stratocumulus"),
	Cumulus         UMETA(DisplayName = "Cumulus"),
	Cumulonimbus    UMETA(DisplayName = "Cumulonimbus"),
	Nimbostratus    UMETA(DisplayName = "Nimbostratus"),
	Custom          UMETA(DisplayName = "Custom")
};

/**
 * Cloud layer definition for volumetric cloud rendering.
 */
USTRUCT(BlueprintType)
struct ANDROMEDA_API FZephyrCloudLayer
{
	GENERATED_BODY()

	/** Cloud type for this layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds")
	EZephyrCloudType CloudType = EZephyrCloudType::Cumulus;

	/** Base altitude of cloud layer (km above sea level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0", ClampMax = "50"))
	float BaseAltitudeKm = 2.0f;

	/** Top altitude of cloud layer (km above sea level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0", ClampMax = "50"))
	float TopAltitudeKm = 4.0f;

	/** Cloud coverage [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float Coverage = 0.5f;

	/** Cloud density [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float Density = 0.5f;

	/** Cloud color tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds")
	FLinearColor CloudColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** Water content [0, 1] - affects scattering/absorption. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float WaterContent = 0.5f;

	/** Ice content [0, 1] - affects scattering/absorption. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float IceContent = 0.0f;

	/** Wind speed at this layer (m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0"))
	float WindSpeed = 10.0f;

	/** Wind direction (degrees, meteorological convention: from). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0", ClampMax = "360"))
	float WindDirection = 0.0f;

	/** Cloud detail scale (noise frequency). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds", meta = (ClampMin = "0.1", ClampMax = "100"))
	float DetailScale = 1.0f;

	/** Whether this layer is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Clouds")
	bool bEnabled = true;
};

/**
 * Weather state for a planet.
 */
USTRUCT(BlueprintType)
struct ANDROMEDA_API FZephyrWeatherState
{
	GENERATED_BODY()

	/** Current weather type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather")
	EZephyrWeatherType WeatherType = EZephyrWeatherType::Clear;

	/** Precipitation intensity [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float PrecipitationIntensity = 0.0f;

	/** Precipitation type (0=rain, 1=snow, 2=mixed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "0", ClampMax = "2"))
	int32 PrecipitationType = 0;

	/** Wind speed at surface (m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "0"))
	float SurfaceWindSpeed = 5.0f;

	/** Wind direction at surface (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "0", ClampMax = "360"))
	float SurfaceWindDirection = 0.0f;

	/** Visibility range (km). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "0.01", ClampMax = "100"))
	float VisibilityKm = 50.0f;

	/** Atmospheric pressure at sea level (hPa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "300", ClampMax = "1200"))
	float PressureHpa = 1013.25f;

	/** Temperature at sea level (Celsius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "-100", ClampMax = "100"))
	float TemperatureC = 15.0f;

	/** Humidity [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Humidity = 0.5f;

	/** Cloud layers for this weather state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather")
	TArray<FZephyrCloudLayer> CloudLayers;

	/** Time of day this weather was sampled (for transitions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather")
	float TimeOfDay = 0.5f;

	/** Transition progress to next weather [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float TransitionProgress = 0.0f;

	/** Next weather type for transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Weather")
	EZephyrWeatherType NextWeatherType = EZephyrWeatherType::Clear;
};

/**
 * Sky appearance parameters.
 */
USTRUCT(BlueprintType)
struct ANDROMEDA_API FZephyrSkyAppearance
{
	GENERATED_BODY()

	/** Zenith color (overhead sky). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky")
	FLinearColor ZenithColor = FLinearColor(0.3f, 0.5f, 0.9f, 1.0f);

	/** Horizon color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky")
	FLinearColor HorizonColor = FLinearColor(0.7f, 0.8f, 0.95f, 1.0f);

	/** Sun glow color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky")
	FLinearColor SunGlowColor = FLinearColor(1.0f, 0.9f, 0.7f, 1.0f);

	/** Mie scattering color tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky")
	FLinearColor MieColor = FLinearColor(0.9f, 0.8f, 0.6f, 1.0f);

	/** Rayleigh scattering scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky", meta = (ClampMin = "0", ClampMax = "5"))
	float RayleighScale = 1.0f;

	/** Mie scattering scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky", meta = (ClampMin = "0", ClampMax = "5"))
	float MieScale = 1.0f;

	/** Aerial perspective scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky", meta = (ClampMin = "0", ClampMax = "5"))
	float AerialScale = 1.0f;

	/** Sun disk intensity multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky", meta = (ClampMin = "0", ClampMax = "10"))
	float SunDiskIntensity = 1.0f;

	/** Sun disk angular size multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky", meta = (ClampMin = "0.1", ClampMax = "5"))
	float SunDiskSize = 1.0f;

	/** Exposure compensation for sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky", meta = (ClampMin = "-5", ClampMax = "5"))
	float ExposureCompensation = 0.0f;

	/** Gamma correction for sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zephyr|Sky", meta = (ClampMin = "0.5", ClampMax = "3"))
	float Gamma = 1.0f;
};

/**
 * GPU-ready Zephyr planet data (mirror of FZephyrPlanetProfile for shader upload).
 *
 * Canonical geometry (single source of truth, reused from the authoritative
 * ATMOS snapshot):
 *   PlanetAtmosphereRange = planetary REFERENCE radius (base sphere / sea
 *                           level, ATMOS Bottom). Terrain is NOT folded in;
 *                           mountains protrude into the volume.
 *   AtmosphereTopRadius   = reference + profile-derived envelope (self-similar
 *                           optical thickness, raised to contain the authored
 *                           terrain bound plus a few scale heights)
 *   ViewRadius            = distance(camera, planet center)
 *
 * No arbitrary fade distance exists anywhere in ZEPHYR: the transition width
 * is always the real atmospheric volume [reference radius, AtmosphereTopRadius].
 */
USTRUCT(BlueprintType)
struct ANDROMEDA_API FZephyrPlanetGpuData
{
	GENERATED_BODY()

	/** Planet ID for debugging. */
	UPROPERTY()
	FGuid PlanetId;

	/** Base sphere radius of the planet, km (ATMOS Bottom / GroundRadiusKm). */
	UPROPERTY()
	float PlanetRadiusKm = 0.0f;

	/** Authored terrain peak headroom, km (metadata; terrain protrudes into the volume). */
	UPROPERTY()
	float TerrainHeightKm = 0.0f;

	/** Planetary reference radius, km == ATMOS GroundRadiusKm (Bottom). */
	UPROPERTY()
	float PlanetAtmosphereRangeKm = 0.0f;

	/** Envelope ratio == AtmosphereTopRadiusKm / PlanetAtmosphereRangeKm. */
	UPROPERTY()
	float AtmosphereMultiplier = 0.0f;

	/** Atmosphere top radius (km) == reference + profile/containment envelope (ATMOS TopRadiusKm). */
	UPROPERTY()
	float AtmosphereTopRadiusKm = 0.0f;

	/** Camera distance to the planet center (km). */
	UPROPERTY()
	float ViewRadiusKm = 0.0f;

	/** Sky appearance. */
	UPROPERTY()
	FZephyrSkyAppearance SkyAppearance;

	/** Current weather state. */
	UPROPERTY()
	FZephyrWeatherState WeatherState;

	/** Planetary rotation (world frame). */
	UPROPERTY()
	FQuat RotationWS = FQuat::Identity;

	/** Star direction in planet-local space. */
	UPROPERTY()
	FVector3f StarDirectionLocal = FVector3f::ZeroVector;

	/** Star irradiance at planet. */
	UPROPERTY()
	FVector3f StarIrradiance = FVector3f::ZeroVector;

	/** Whether camera is inside the atmosphere shell (diagnostic). */
	UPROPERTY()
	bool bCameraInside = false;

	/**
	 * Authoritative ZEPHYR transition: 0 = deep space (ZEPHYR sky hidden),
	 * 1 = at/inside the surface reference range (ZEPHYR sky 100%). Computed
	 * ONLY on the CPU by ZephyrComputeTransitionFactor over the real shell
	 * [PlanetAtmosphereRange, AtmosphereTopRadius]. The GPU consumes it as a
	 * single gate; there is no second fade anywhere.
	 */
	UPROPERTY()
	float ZephyrTransitionFactor = 0.0f;

	/** Profile hash for change detection. */
	UPROPERTY()
	uint64 ProfileHash = 0;

	FZephyrPlanetGpuData() = default;
};

/**
 * THE single ZEPHYR space<->atmosphere transition (canonical, CPU-authoritative).
 *
 * Geometry (arguments are the SHARED planetary reference used by ATMOS):
 *   PlanetAtmosphereRange = planetary reference radius (base sphere, sea
 *                           level; terrain is NOT folded in)
 *   AtmosphereTopRadius   = reference + profile-derived envelope
 *   ViewRadius            = distance(camera, governing planet center)
 *
 * High-pass smoothstep over the volume:
 *   TransitionInnerRadius = PlanetAtmosphereRange + (Top - Range) * 0.10
 *   X = clamp((ViewRadius - TransitionInnerRadius) / (AtmosphereTopRadius - TransitionInnerRadius), 0, 1);
 *   S = X * X * (3 - 2 * X);
 *   result = 1 - S;
 *
 * Result:
 *   ViewRadius <= TransitionInnerRadius    -> 1 (full ZEPHYR, no fade)
 *   TransitionInnerRadius < ViewRadius < AtmosphereTopRadius -> smooth 1 -> 0
 *   ViewRadius >= AtmosphereTopRadius      -> 0 (deep space, ZEPHYR hidden)
 *
 * The transition width is ALWAYS the real atmospheric volume thickness
 * (AtmosphereTopRadius - PlanetAtmosphereRange); no arbitrary fade distance.
 * The inner offset (10%) scales with the volume so large and small planets
 * behave consistently. Degenerate AtmosphereTopRadius <= PlanetAtmosphereRange
 * is handled as a step at the reference radius (no division by zero).
 */
ANDROMEDA_API float ZephyrComputeTransitionFactor(
	float ViewRadiusKm,
	float PlanetAtmosphereRangeKm,
	float AtmosphereTopRadiusKm);

/**
 * ZEPHYR presentation contract (CPU mirror of ZephyrCommon.ush).
 *
 * Sky radiance is authoritative ATMOS output; the ZEPHYR AfterDOF composite
 * adds ONLY bounded presentation (clouds + haze) on sky pixels, scaled by the
 * daylight factor below. The HLSL ZephyrDaylightFactor MUST evaluate
 * identically to DaylightFactor here (same smoothstep); the presentation
 * tests lock the shared curve (day/twilight/night response with no palettes).
 */
namespace ZephyrPresentation
{
	/** Sun elevation where presentation reaches full strength. */
	constexpr float FullDayElevCos = 0.25f;
	/** Sun elevation (civil-twilight depth) where presentation is extinct. */
	constexpr float TwilightBeginElevCos = -0.12f;
	/** Cap on total cloud HDR additions (bloom-headroom budget). */
	constexpr float MaxCloudAdd = 0.45f;
	/** Cap on total haze HDR additions (bloom-headroom budget). */
	constexpr float MaxHazeAdd = 0.20f;

	/** CPU mirror of HLSL ZephyrDaylightFactor (identical smoothstep). */
	inline float DaylightFactor(float SunElevCos)
	{
		const float T = FMath::Clamp(
			(SunElevCos - TwilightBeginElevCos) / (FullDayElevCos - TwilightBeginElevCos),
			0.0f,
			1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}