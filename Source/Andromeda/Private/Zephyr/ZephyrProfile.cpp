#include "Zephyr/ZephyrProfile.h"
#include "Zephyr/ZephyrTypes.h"

namespace
{
	constexpr uint64 ZephyrHashOffsetBasis = 14695981039346656037ULL;
	constexpr uint64 ZephyrHashPrime = 1099511628211ULL;

	void ZephyrHashBytes(uint64& Rest, const void* Data, int32 SizeInBytes)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (int32 i = 0; i < SizeInBytes; ++i)
		{
			Rest ^= (uint64)Bytes[i];
			Rest *= ZephyrHashPrime;
		}
	}

	void ZephyrHashFloat(uint64& Rest, float Value, float Precision = 1e-4f)
	{
		const int32 Quantized = FMath::RoundToInt(Value / Precision);
		ZephyrHashBytes(Rest, &Quantized, sizeof(Quantized));
	}
}

uint64 FZephyrPlanetProfile::ComputeContentHash() const
{
	uint64 Hash = ZephyrHashOffsetBasis;

	ZephyrHashBytes(Hash, &PlanetId, sizeof(PlanetId));
	ZephyrHashBytes(Hash, &PlanetSeed, sizeof(PlanetSeed));
	ZephyrHashBytes(Hash, &PlanetNumber, sizeof(PlanetNumber));
	ZephyrHashBytes(Hash, &Archetype, sizeof(Archetype));
	ZephyrHashFloat(Hash, OrbitDistanceCm, 1e3f);

	ZephyrHashFloat(Hash, GroundRadiusKm);
	ZephyrHashFloat(Hash, AtmosphereTopRadiusKm);

	const FZephyrSkyAppearance& Sky = SkyAppearance;
	ZephyrHashFloat(Hash, Sky.ZenithColor.R);
	ZephyrHashFloat(Hash, Sky.ZenithColor.G);
	ZephyrHashFloat(Hash, Sky.ZenithColor.B);
	ZephyrHashFloat(Hash, Sky.HorizonColor.R);
	ZephyrHashFloat(Hash, Sky.HorizonColor.G);
	ZephyrHashFloat(Hash, Sky.HorizonColor.B);
	ZephyrHashFloat(Hash, Sky.SunGlowColor.R);
	ZephyrHashFloat(Hash, Sky.SunGlowColor.G);
	ZephyrHashFloat(Hash, Sky.SunGlowColor.B);
	ZephyrHashFloat(Hash, Sky.MieColor.R);
	ZephyrHashFloat(Hash, Sky.MieColor.G);
	ZephyrHashFloat(Hash, Sky.MieColor.B);
	ZephyrHashFloat(Hash, Sky.RayleighScale);
	ZephyrHashFloat(Hash, Sky.MieScale);
	ZephyrHashFloat(Hash, Sky.AerialScale);

	const FZephyrWeatherState& W = BaseWeatherState;
	ZephyrHashBytes(Hash, &W.WeatherType, sizeof(W.WeatherType));
	ZephyrHashFloat(Hash, W.PrecipitationIntensity);
	ZephyrHashFloat(Hash, W.SurfaceWindSpeed);
	ZephyrHashFloat(Hash, W.SurfaceWindDirection);
	ZephyrHashFloat(Hash, W.VisibilityKm);
	ZephyrHashFloat(Hash, W.PressureHpa);
	ZephyrHashFloat(Hash, W.TemperatureC);
	ZephyrHashFloat(Hash, W.Humidity);

	for (const FZephyrCloudLayer& Layer : W.CloudLayers)
	{
		ZephyrHashBytes(Hash, &Layer.CloudType, sizeof(Layer.CloudType));
		ZephyrHashFloat(Hash, Layer.BaseAltitudeKm);
		ZephyrHashFloat(Hash, Layer.TopAltitudeKm);
		ZephyrHashFloat(Hash, Layer.Coverage);
		ZephyrHashFloat(Hash, Layer.Density);
		ZephyrHashFloat(Hash, Layer.CloudColor.R);
		ZephyrHashFloat(Hash, Layer.CloudColor.G);
		ZephyrHashFloat(Hash, Layer.CloudColor.B);
		ZephyrHashFloat(Hash, Layer.WaterContent);
		ZephyrHashFloat(Hash, Layer.IceContent);
	}

	ZephyrHashFloat(Hash, SeasonalVariation);
	ZephyrHashFloat(Hash, WeatherVariationSpeed);

	return Hash;
}

bool FZephyrPlanetProfile::IsValid() const
{
	return PlanetId.IsValid()
		&& GroundRadiusKm > 0.0f
		&& AtmosphereTopRadiusKm > GroundRadiusKm;
}

// =========================================================
// ZephyrComputeTransitionFactor (declared in ZephyrTypes.h)
// =========================================================

float ZephyrComputeTransitionFactor(
	float ViewRadiusKm,
	float PlanetAtmosphereRangeKm,
	float AtmosphereTopRadiusKm)
{
	const float RangeKm = FMath::Max(0.0f, PlanetAtmosphereRangeKm);
	const float TopKm = FMath::Max(0.0f, AtmosphereTopRadiusKm);

	// Degenerate / absent volume: step at the reference radius (no div by zero).
	if (TopKm <= RangeKm)
	{
		return (ViewRadiusKm <= RangeKm) ? 1.0f : 0.0f;
	}

	// Move the inner end of the ZEPHYR transition slightly outward using a
	// fraction of the existing atmospheric volume. This prevents the transition
	// from completing right at the planetary reference radius, which looks too
	// close. The offset scales with (Top - Range) so large and small planets
	// behave consistently. No arbitrary kilometer distance.
	const float ShellThickness = TopKm - RangeKm;
	const float TransitionInnerRadius = RangeKm + ShellThickness * 0.10f;

	// Normalized position across the shifted shell [TransitionInnerRadius, top].
	// X = 0 at the inner end, X = 1 at the atmosphere top.
	const float X = FMath::Clamp(
		(ViewRadiusKm - TransitionInnerRadius) / (TopKm - TransitionInnerRadius),
		0.0f,
		1.0f);

	// High-pass smoothstep: 1 inside, 0 at/above the top, smooth in between.
	const float S = X * X * (3.0f - 2.0f * X);
	return 1.0f - S;
}

// =========================================================
// UZephyrProfileLibrary
// =========================================================

uint32 UZephyrProfileLibrary::DeterministicRandom(uint64 Seed, uint32 Salt)
{
	uint64 Hash = ZephyrHashOffsetBasis;
	ZephyrHashBytes(Hash, &Salt, sizeof(Salt));
	ZephyrHashBytes(Hash, &Seed, sizeof(Seed));
	return (uint32)(Hash ^ (Hash >> 32));
}

float UZephyrProfileLibrary::DeterministicUnitRandom(uint64 Seed, uint32 Salt)
{
	return (float)((double)DeterministicRandom(Seed, Salt) / (double)UINT32_MAX);
}

float UZephyrProfileLibrary::DeterministicRangeRandom(uint64 Seed, uint32 Salt, float MinVal, float MaxVal)
{
	return MinVal + (MaxVal - MinVal) * DeterministicUnitRandom(Seed, Salt);
}

FZephyrSkyAppearance UZephyrProfileLibrary::GenerateSkyAppearance(
	int64 Seed,
	EPlanetArchetype Archetype,
	float OrbitDistanceCm,
	float GroundRadiusKm)
{
	FZephyrSkyAppearance Sky;
	const uint64 SeedU = (uint64)Seed;

	switch (Archetype)
	{
	case EPlanetArchetype::Terran:
		Sky.ZenithColor = FLinearColor(0.30f, 0.52f, 0.90f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.70f, 0.80f, 0.95f, 1.0f);
		Sky.SunGlowColor = FLinearColor(1.00f, 0.90f, 0.70f, 1.0f);
		Sky.MieColor = FLinearColor(0.90f, 0.80f, 0.60f, 1.0f);
		Sky.RayleighScale = 1.0f;
		Sky.MieScale = 1.0f;
		break;

	case EPlanetArchetype::Arid:
	case EPlanetArchetype::Desert:
		Sky.ZenithColor = FLinearColor(0.35f, 0.48f, 0.72f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.82f, 0.75f, 0.58f, 1.0f);
		Sky.SunGlowColor = FLinearColor(1.00f, 0.92f, 0.75f, 1.0f);
		Sky.MieColor = FLinearColor(0.95f, 0.85f, 0.65f, 1.0f);
		Sky.RayleighScale = 0.85f;
		Sky.MieScale = 1.25f;
		break;

	case EPlanetArchetype::Oceanic:
		Sky.ZenithColor = FLinearColor(0.22f, 0.50f, 0.92f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.60f, 0.78f, 0.98f, 1.0f);
		Sky.SunGlowColor = FLinearColor(0.95f, 0.95f, 0.90f, 1.0f);
		Sky.MieColor = FLinearColor(0.85f, 0.85f, 0.80f, 1.0f);
		Sky.RayleighScale = 1.15f;
		Sky.MieScale = 0.9f;
		break;

	case EPlanetArchetype::Frozen:
	case EPlanetArchetype::Tundra:
		Sky.ZenithColor = FLinearColor(0.40f, 0.58f, 0.90f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.80f, 0.85f, 0.92f, 1.0f);
		Sky.SunGlowColor = FLinearColor(0.95f, 0.95f, 0.90f, 1.0f);
		Sky.MieColor = FLinearColor(0.90f, 0.90f, 0.85f, 1.0f);
		Sky.RayleighScale = 0.75f;
		Sky.MieScale = 0.8f;
		break;

	case EPlanetArchetype::Jungle:
		Sky.ZenithColor = FLinearColor(0.25f, 0.48f, 0.85f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.62f, 0.75f, 0.80f, 1.0f);
		Sky.SunGlowColor = FLinearColor(0.98f, 0.92f, 0.80f, 1.0f);
		Sky.MieColor = FLinearColor(0.85f, 0.80f, 0.70f, 1.0f);
		Sky.RayleighScale = 1.1f;
		Sky.MieScale = 1.05f;
		break;

	case EPlanetArchetype::Volcanic:
		Sky.ZenithColor = FLinearColor(0.28f, 0.34f, 0.55f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.65f, 0.48f, 0.38f, 1.0f);
		Sky.SunGlowColor = FLinearColor(1.00f, 0.75f, 0.45f, 1.0f);
		Sky.MieColor = FLinearColor(0.95f, 0.65f, 0.40f, 1.0f);
		Sky.RayleighScale = 1.0f;
		Sky.MieScale = 1.5f;
		break;

	case EPlanetArchetype::Rocky:
		Sky.ZenithColor = FLinearColor(0.32f, 0.44f, 0.72f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.72f, 0.68f, 0.60f, 1.0f);
		Sky.SunGlowColor = FLinearColor(1.00f, 0.88f, 0.70f, 1.0f);
		Sky.MieColor = FLinearColor(0.92f, 0.82f, 0.62f, 1.0f);
		Sky.RayleighScale = 0.8f;
		Sky.MieScale = 1.1f;
		break;

	case EPlanetArchetype::Exotic:
		Sky.ZenithColor = FLinearColor(0.35f, 0.35f, 0.70f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.60f, 0.55f, 0.80f, 1.0f);
		Sky.SunGlowColor = FLinearColor(0.90f, 0.85f, 1.00f, 1.0f);
		Sky.MieColor = FLinearColor(0.80f, 0.75f, 0.90f, 1.0f);
		Sky.RayleighScale = 1.2f;
		Sky.MieScale = 0.9f;
		break;

	default:
		Sky.ZenithColor = FLinearColor(0.30f, 0.52f, 0.90f, 1.0f);
		Sky.HorizonColor = FLinearColor(0.70f, 0.80f, 0.95f, 1.0f);
		Sky.SunGlowColor = FLinearColor(1.00f, 0.90f, 0.70f, 1.0f);
		Sky.MieColor = FLinearColor(0.90f, 0.80f, 0.60f, 1.0f);
		Sky.RayleighScale = 1.0f;
		Sky.MieScale = 1.0f;
		break;
	}

	// Per-seed tint variation (chromaticity preserved, small amplitude).
	const float TintR = DeterministicRangeRandom(SeedU, 0x11, -0.03f, 0.03f);
	const float TintG = DeterministicRangeRandom(SeedU, 0x22, -0.03f, 0.03f);
	const float TintB = DeterministicRangeRandom(SeedU, 0x33, -0.03f, 0.03f);
	Sky.ZenithColor.R = FMath::Clamp(Sky.ZenithColor.R + TintR, 0.02f, 1.0f);
	Sky.ZenithColor.G = FMath::Clamp(Sky.ZenithColor.G + TintG, 0.02f, 1.0f);
	Sky.ZenithColor.B = FMath::Clamp(Sky.ZenithColor.B + TintB, 0.02f, 1.0f);

	// Orbit-distance influence: very close orbits wash out, far orbits deepen.
	// Ground radius scales the horizon/zenith contrast (thin shell = close tones).
	const float OrbitFactor = FMath::Clamp(OrbitDistanceCm * 1e-10f, 0.6f, 1.4f);
	Sky.RayleighScale *= OrbitFactor;
	Sky.MieScale *= FMath::Lerp(1.4f, 1.0f, FMath::Clamp(OrbitDistanceCm * 1e-11f, 0.0f, 1.0f));

	return Sky;
}

void UZephyrProfileLibrary::GetArchetypeScatteringScales(
	EPlanetArchetype Archetype,
	float& OutRayleighScale,
	float& OutMieScale)
{
	// Archetype-only scales, extracted from GenerateSkyAppearance (no orbit or
	// seed terms): the presentation tint must be deterministic per archetype.
	OutRayleighScale = 1.0f;
	OutMieScale = 1.0f;

	switch (Archetype)
	{
	case EPlanetArchetype::Terran:
		OutRayleighScale = 1.0f;
		OutMieScale = 1.0f;
		break;
	case EPlanetArchetype::Arid:
	case EPlanetArchetype::Desert:
		OutRayleighScale = 0.85f;
		OutMieScale = 1.25f;
		break;
	case EPlanetArchetype::Oceanic:
		OutRayleighScale = 1.15f;
		OutMieScale = 0.9f;
		break;
	case EPlanetArchetype::Frozen:
	case EPlanetArchetype::Tundra:
		OutRayleighScale = 0.75f;
		OutMieScale = 0.8f;
		break;
	case EPlanetArchetype::Jungle:
		OutRayleighScale = 1.1f;
		OutMieScale = 1.05f;
		break;
	case EPlanetArchetype::Volcanic:
		OutRayleighScale = 1.0f;
		OutMieScale = 1.5f;
		break;
	case EPlanetArchetype::Rocky:
		OutRayleighScale = 0.8f;
		OutMieScale = 1.1f;
		break;
	case EPlanetArchetype::Exotic:
		OutRayleighScale = 1.2f;
		OutMieScale = 0.9f;
		break;
	default:
		OutRayleighScale = 1.0f;
		OutMieScale = 1.0f;
		break;
	}
}

FZephyrWeatherState UZephyrProfileLibrary::GenerateBaseWeatherState(
	int64 Seed,
	EPlanetArchetype Archetype,
	float OrbitDistanceCm)
{
	FZephyrWeatherState Weather;
	const uint64 SeedU = (uint64)Seed;

	switch (Archetype)
	{
	case EPlanetArchetype::Terran:
		Weather.WeatherType = EZephyrWeatherType::PartlyCloudy;
		Weather.PrecipitationIntensity = 0.05f;
		Weather.SurfaceWindSpeed = 5.0f;
		Weather.VisibilityKm = 50.0f;
		Weather.PressureHpa = 1013.25f;
		Weather.TemperatureC = 15.0f;
		Weather.Humidity = 0.55f;
		break;

	case EPlanetArchetype::Arid:
	case EPlanetArchetype::Desert:
		Weather.WeatherType = EZephyrWeatherType::Clear;
		Weather.PrecipitationIntensity = 0.0f;
		Weather.SurfaceWindSpeed = 12.0f;
		Weather.VisibilityKm = 80.0f;
		Weather.PressureHpa = 1008.0f;
		Weather.TemperatureC = 32.0f;
		Weather.Humidity = 0.15f;
		break;

	case EPlanetArchetype::Oceanic:
		Weather.WeatherType = EZephyrWeatherType::Cloudy;
		Weather.PrecipitationIntensity = 0.15f;
		Weather.SurfaceWindSpeed = 18.0f;
		Weather.VisibilityKm = 30.0f;
		Weather.PressureHpa = 1015.0f;
		Weather.TemperatureC = 22.0f;
		Weather.Humidity = 0.80f;
		break;

	case EPlanetArchetype::Frozen:
	case EPlanetArchetype::Tundra:
		Weather.WeatherType = EZephyrWeatherType::Snow;
		Weather.PrecipitationIntensity = 0.15f;
		Weather.PrecipitationType = 1;
		Weather.SurfaceWindSpeed = 15.0f;
		Weather.VisibilityKm = 20.0f;
		Weather.PressureHpa = 980.0f;
		Weather.TemperatureC = -25.0f;
		Weather.Humidity = 0.70f;
		break;

	case EPlanetArchetype::Jungle:
		Weather.WeatherType = EZephyrWeatherType::Rain;
		Weather.PrecipitationIntensity = 0.35f;
		Weather.SurfaceWindSpeed = 8.0f;
		Weather.VisibilityKm = 25.0f;
		Weather.PressureHpa = 1010.0f;
		Weather.TemperatureC = 28.0f;
		Weather.Humidity = 0.90f;
		break;

	case EPlanetArchetype::Volcanic:
		Weather.WeatherType = EZephyrWeatherType::Haze;
		Weather.PrecipitationIntensity = 0.0f;
		Weather.SurfaceWindSpeed = 20.0f;
		Weather.VisibilityKm = 10.0f;
		Weather.PressureHpa = 990.0f;
		Weather.TemperatureC = 45.0f;
		Weather.Humidity = 0.30f;
		break;

	case EPlanetArchetype::Rocky:
		Weather.WeatherType = EZephyrWeatherType::Clear;
		Weather.PrecipitationIntensity = 0.0f;
		Weather.SurfaceWindSpeed = 7.0f;
		Weather.VisibilityKm = 60.0f;
		Weather.PressureHpa = 602.0f;
		Weather.TemperatureC = 5.0f;
		Weather.Humidity = 0.10f;
		break;

	case EPlanetArchetype::Exotic:
		Weather.WeatherType = EZephyrWeatherType::PartlyCloudy;
		Weather.PrecipitationIntensity = 0.05f;
		Weather.SurfaceWindSpeed = 6.0f;
		Weather.VisibilityKm = 45.0f;
		Weather.PressureHpa = 950.0f;
		Weather.TemperatureC = 10.0f;
		Weather.Humidity = 0.40f;
		break;

	default:
		Weather.WeatherType = EZephyrWeatherType::PartlyCloudy;
		Weather.PrecipitationIntensity = 0.05f;
		Weather.SurfaceWindSpeed = 5.0f;
		Weather.VisibilityKm = 50.0f;
		Weather.PressureHpa = 1013.25f;
		Weather.TemperatureC = 15.0f;
		Weather.Humidity = 0.55f;
		break;
	}

	// Orby distance scales temperature (insolation proxy).
	const float TemperatureScale = FMath::Clamp(1.0f - OrbitDistanceCm * 2e-11f, 0.4f, 1.4f);
	Weather.TemperatureC *= TemperatureScale;

	// Per-seed variation in humidity/wind.
	Weather.Humidity = FMath::Clamp(Weather.Humidity + DeterministicRangeRandom(SeedU, 0x44, -0.1f, 0.1f), 0.0f, 1.0f);
	Weather.SurfaceWindSpeed = FMath::Max(1.0f, Weather.SurfaceWindSpeed + DeterministicRangeRandom(SeedU, 0x55, -3.0f, 3.0f));

	return Weather;
}

TArray<FZephyrCloudLayer> UZephyrProfileLibrary::GenerateCloudLayerTemplates(
	int64 Seed,
	EPlanetArchetype Archetype,
	float AtmosphereThicknessKm)
{
	TArray<FZephyrCloudLayer> Layers;
	const uint64 SeedU = (uint64)Seed;
	const float CloudCeilingKm = FMath::Max(4.0f, AtmosphereThicknessKm * 0.35f);

	auto AddLayer = [&](int32 Salt, EZephyrCloudType Type, float BaseFrac, float TopFrac, float Coverage, float Density)
	{
		FZephyrCloudLayer Layer;
		Layer.CloudType = Type;
		Layer.BaseAltitudeKm = FMath::Max(0.2f, CloudCeilingKm * BaseFrac);
		Layer.TopAltitudeKm = FMath::Max(Layer.BaseAltitudeKm + 0.3f, CloudCeilingKm * TopFrac);
		Layer.Coverage = FMath::Clamp(Coverage + DeterministicRangeRandom(SeedU, Salt, -0.15f, 0.15f), 0.0f, 1.0f);
		Layer.Density = FMath::Clamp(Density + DeterministicRangeRandom(SeedU, Salt + 1, -0.15f, 0.15f), 0.05f, 1.0f);
		Layer.WaterContent = DeterministicRangeRandom(SeedU, Salt + 2, 0.3f, 0.9f);
		Layer.IceContent = DeterministicRangeRandom(SeedU, Salt + 3, 0.0f, 0.4f);
		Layer.WindSpeed = FMath::Max(3.0f, 15.0f + DeterministicRangeRandom(SeedU, Salt + 4, -10.0f, 20.0f));
		Layer.WindDirection = DeterministicRangeRandom(SeedU, Salt + 5, 0.0f, 360.0f);
		Layer.DetailScale = FMath::Max(0.1f, 1.0f + DeterministicRangeRandom(SeedU, Salt + 6, -0.3f, 0.5f));
		Layer.CloudColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
		Layer.bEnabled = true;
		Layers.Add(Layer);
	};

	switch (Archetype)
	{
	case EPlanetArchetype::Desert:
	case EPlanetArchetype::Arid:
		AddLayer(0x60, EZephyrCloudType::Cirrus, 0.55f, 0.70f, 0.15f, 0.25f);
		AddLayer(0x61, EZephyrCloudType::Stratocumulus, 0.10f, 0.20f, 0.20f, 0.30f);
		break;

	case EPlanetArchetype::Oceanic:
		AddLayer(0x62, EZephyrCloudType::Stratus, 0.08f, 0.15f, 0.60f, 0.55f);
		AddLayer(0x63, EZephyrCloudType::Cumulus, 0.15f, 0.30f, 0.45f, 0.60f);
		AddLayer(0x64, EZephyrCloudType::Nimbostratus, 0.25f, 0.45f, 0.30f, 0.50f);
		break;

	case EPlanetArchetype::Frozen:
	case EPlanetArchetype::Tundra:
		AddLayer(0x65, EZephyrCloudType::Stratus, 0.10f, 0.20f, 0.55f, 0.50f);
		AddLayer(0x66, EZephyrCloudType::Nimbostratus, 0.20f, 0.40f, 0.45f, 0.60f);
		AddLayer(0x67, EZephyrCloudType::Cirrostratus, 0.45f, 0.60f, 0.35f, 0.35f);
		break;

	case EPlanetArchetype::Jungle:
		AddLayer(0x68, EZephyrCloudType::Cumulus, 0.12f, 0.30f, 0.55f, 0.60f);
		AddLayer(0x69, EZephyrCloudType::Cumulonimbus, 0.15f, 0.50f, 0.40f, 0.70f);
		AddLayer(0x6A, EZephyrCloudType::Cirrostratus, 0.50f, 0.65f, 0.30f, 0.30f);
		break;

	case EPlanetArchetype::Volcanic:
		AddLayer(0x6B, EZephyrCloudType::Stratus, 0.10f, 0.25f, 0.40f, 0.55f);
		AddLayer(0x6C, EZephyrCloudType::Cirrus, 0.50f, 0.65f, 0.25f, 0.30f);
		break;

	case EPlanetArchetype::Terran:
	default:
		AddLayer(0x6D, EZephyrCloudType::Cirrus, 0.55f, 0.70f, 0.25f, 0.30f);
		AddLayer(0x6E, EZephyrCloudType::Cumulus, 0.12f, 0.28f, 0.40f, 0.50f);
		AddLayer(0x6F, EZephyrCloudType::Stratocumulus, 0.08f, 0.20f, 0.30f, 0.40f);
		break;
	}

	return Layers;
}

void UZephyrProfileLibrary::GenerateClimateZones(
	int64 Seed,
	EPlanetArchetype Archetype,
	TArray<float>& OutZoneBoundaries,
	TArray<EZephyrWeatherType>& OutZoneWeather)
{
	OutZoneBoundaries.Reset();
	OutZoneWeather.Reset();

	// Zone boundaries in [-1, 1] latitude: 0 = equator, +/-1 = poles.
	OutZoneBoundaries.Add(0.0f);
	OutZoneBoundaries.Add(0.35f);
	OutZoneBoundaries.Add(0.70f);
	OutZoneBoundaries.Add(1.0f);

	switch (Archetype)
	{
	case EPlanetArchetype::Frozen:
	case EPlanetArchetype::Tundra:
		OutZoneWeather = { EZephyrWeatherType::Snow, EZephyrWeatherType::Blizzard, EZephyrWeatherType::Snow };
		break;

	case EPlanetArchetype::Desert:
	case EPlanetArchetype::Arid:
		OutZoneWeather = { EZephyrWeatherType::Clear, EZephyrWeatherType::DustStorm, EZephyrWeatherType::Clear };
		break;

	case EPlanetArchetype::Jungle:
		OutZoneWeather = { EZephyrWeatherType::Rain, EZephyrWeatherType::Thunderstorm, EZephyrWeatherType::Rain };
		break;

	case EPlanetArchetype::Volcanic:
		OutZoneWeather = { EZephyrWeatherType::Haze, EZephyrWeatherType::Storm, EZephyrWeatherType::Haze };
		break;

	case EPlanetArchetype::Oceanic:
		OutZoneWeather = { EZephyrWeatherType::Cloudy, EZephyrWeatherType::Storm, EZephyrWeatherType::Cloudy };
		break;

	case EPlanetArchetype::Terran:
	default:
		OutZoneWeather = { EZephyrWeatherType::PartlyCloudy, EZephyrWeatherType::LightRain, EZephyrWeatherType::Snow };
		break;
	}
}

FZephyrPlanetProfile UZephyrProfileLibrary::GetArchetypeDefaultProfile(EPlanetArchetype Archetype)
{
	FZephyrPlanetProfile Profile;
	Profile.Archetype = Archetype;
	Profile.SkyAppearance = GenerateSkyAppearance(0, Archetype, 0.0f, 6371.0f);
	Profile.BaseWeatherState = GenerateBaseWeatherState(0, Archetype, 0.0f);
	Profile.CloudLayerTemplates = GenerateCloudLayerTemplates(0, Archetype, 100.0f);
	GenerateClimateZones(0, Archetype, Profile.ClimateZoneBoundaries, Profile.ClimateZoneWeather);
	Profile.bInitialized = true;
	return Profile;
}

FZephyrPlanetProfile UZephyrProfileLibrary::BuildProfile(
	int64 PlanetSeed,
	int64 PlanetID,
	EPlanetArchetype Archetype,
	float OrbitDistanceCm,
	float GroundRadiusKm,
	float AtmosphereTopRadiusKm)
{
	FZephyrPlanetProfile Profile;

	Profile.PlanetId = FGuid::NewGuid();
	Profile.PlanetSeed = PlanetSeed;
	Profile.PlanetNumber = PlanetID;
	Profile.Archetype = Archetype;
	Profile.OrbitDistanceCm = OrbitDistanceCm;

	Profile.GroundRadiusKm = GroundRadiusKm;
	Profile.AtmosphereTopRadiusKm = FMath::Max(GroundRadiusKm + 1.0f, AtmosphereTopRadiusKm);

	Profile.SkyAppearance = GenerateSkyAppearance(PlanetSeed, Archetype, OrbitDistanceCm, GroundRadiusKm);
	Profile.BaseWeatherState = GenerateBaseWeatherState(PlanetSeed, Archetype, OrbitDistanceCm);
	Profile.CloudLayerTemplates = GenerateCloudLayerTemplates(PlanetSeed, Archetype, Profile.GetAtmosphereThicknessKm());
	GenerateClimateZones(PlanetSeed, Archetype, Profile.ClimateZoneBoundaries, Profile.ClimateZoneWeather);
	Profile.BaseWeatherState.CloudLayers = Profile.CloudLayerTemplates;

	const uint64 SeedU = (uint64)PlanetSeed;
	Profile.SeasonalVariation = DeterministicRangeRandom(SeedU, 0x70, 0.1f, 0.5f);
	Profile.WeatherVariationSpeed = DeterministicRangeRandom(SeedU, 0x71, 0.2f, 1.5f);

	Profile.ContentHash = Profile.ComputeContentHash();
	Profile.bInitialized = true;

	return Profile;
}