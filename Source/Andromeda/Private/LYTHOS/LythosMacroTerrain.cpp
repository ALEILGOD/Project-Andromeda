#include "LYTHOS/LythosMacroTerrain.h"
#include "LYTHOS/LythosCoordinates.h"
#include "AndromedaNoiseLibrary.h"
#include "Planet/PlanetContinentalGenerator.h"
#include "Planet/PlanetLandformGenerator.h"
#include "Planet/PlanetBiomeGenerator.h"

namespace
{
	/** Splitmix64 finalizer for deterministic feature hashing. */
	uint64 FinalizeHash64(uint64 Hash)
	{
		Hash ^= Hash >> 30;
		Hash *= 0xBF58476D1CE4E5B9ULL;
		Hash ^= Hash >> 27;
		Hash *= 0x94D049BB133111EBULL;
		Hash ^= Hash >> 31;
		return Hash;
	}

	/** Quantised spherical sector for feature identity grouping. */
	FIntVector DirectionToSector(const FVector& Direction, int32 Divisions)
	{
		const FVector D = Direction.GetSafeNormal();
		const float Lat = FMath::Clamp(D.Z, -1.f, 1.f);
		const float Lon = FMath::Atan2(D.Y, D.X);

		const float LatNorm = (Lat + 1.f) * 0.5f;       // [0,1]
		const float LonNorm = (Lon + PI) / (2.f * PI);   // [0,1]

		const int32 LatSector = FMath::Clamp(FMath::FloorToInt(LatNorm * Divisions), 0, Divisions - 1);
		const int32 LonSector = FMath::Clamp(FMath::FloorToInt(LonNorm * Divisions), 0, Divisions - 1);

		return FIntVector(LonSector, LatSector, Divisions);
	}
}

int64 FLythosMacroTerrain::DeriveSubSeed(int64 PlanetSeed, uint32 Index)
{
	uint64 Seed = static_cast<uint64>(PlanetSeed) + static_cast<uint64>(Index) * 0x9E3779B97F4A7C15ULL;
	Seed = FinalizeHash64(Seed);
	return static_cast<int64>(Seed & 0x7FFFFFFFFFFFFFFFULL);
}

float FLythosMacroTerrain::ComposeElevation(
	const FLythosPlanetContext& Context,
	const FVector& Direction,
	float* OutContinentalMask,
	float* OutLandformMask,
	float* OutMountainMask,
	float* OutHillMask)
{
	const FVector SafeDir = Direction.GetSafeNormal();

	const float ContinentalMask = UPlanetContinentalGenerator::GetContinentalMask(
		SafeDir, Context.PlanetSeed, Context.ContinentalScale);

	const float LandformMask = UPlanetLandformGenerator::GetLandformMask(SafeDir, Context.PlanetSeed, Context.MountainScale);
	const float MountainMask = UPlanetLandformGenerator::GetMountainMask(SafeDir, Context.PlanetSeed, Context.MountainScale);
	const float HillMask = UPlanetLandformGenerator::GetHillMask(SafeDir, Context.PlanetSeed, Context.MountainScale);

	if (OutContinentalMask) *OutContinentalMask = ContinentalMask;
	if (OutLandformMask)    *OutLandformMask = LandformMask;
	if (OutMountainMask)    *OutMountainMask = MountainMask;
	if (OutHillMask)         *OutHillMask = HillMask;

	const float NormalisedHeight = UAndromedaNoiseLibrary::GeneratePlanetHeight(
		SafeDir,
		Context.PlanetSeed,
		Context.ContinentalScale,
		Context.MountainScale,
		Context.DetailScale,
		Context.MountainStrength,
		Context.DetailStrength);

	const float Height = NormalisedHeight * Context.TerrainHeight;

	return Height;
}

ELythosLandform FLythosMacroTerrain::ClassifyLandform(
	float NormalizedElevation,
	float ContinentalMask,
	float LandformMask,
	float MountainMask,
	float HillMask,
	float SeaLevel)
{
	const bool bIsOcean = NormalizedElevation <= SeaLevel;

	if (bIsOcean)
	{
		return ELythosLandform::Ocean;
	}

	const float ElevationAboveSea = NormalizedElevation - SeaLevel;
	const float CoastThreshold = SeaLevel + 0.02f;

	if (NormalizedElevation <= CoastThreshold)
	{
		return ELythosLandform::Coast;
	}

	// Mountains dominate where the mountain mask is high.
	if (MountainMask > 0.45f)
	{
		return ELythosLandform::Mountain;
	}

	// Basins: within continents but at relatively low elevation.
	if (NormalizedElevation < SeaLevel + 0.10f)
	{
		return ELythosLandform::Basin;
	}

	// Hills / highlands: landform mask present but not full mountains.
	if (HillMask > 0.30f && MountainMask < 0.30f)
	{
		return ELythosLandform::Hills;
	}

	if (NormalizedElevation >= SeaLevel + 0.15f && ContinentalMask > 0.5f)
	{
		return ELythosLandform::Highlands;
	}

	return ELythosLandform::Plains;
}

FLythosFeatureIdentity FLythosMacroTerrain::ComputeFeatureIdentity(
	const FLythosPlanetContext& Context,
	const FVector& Direction,
	ELythosLandform Landform,
	float ContinentalMask,
	float MountainMask)
{
	FLythosFeatureIdentity Id;
	Id.FeatureType = Landform;
	Id.PlanetSeed = Context.PlanetSeed;
	Id.FeatureSector = DirectionToSector(Direction, 32);

	// Feature-specific seed offset for spatial hashing.
	uint64 Salt = 0;
	switch (Landform)
	{
	case ELythosLandform::Mountain:      Salt = 0x4D6F756E7461696EULL; break;
	case ELythosLandform::Hills:         Salt = 0x48696C6C73000000ULL; break;
	case ELythosLandform::Highlands:     Salt = 0x486967686C616E64ULL; break;
	case ELythosLandform::Basin:         Salt = 0x426173696E000000ULL; break;
	case ELythosLandform::Plains:        Salt = 0x506C61696E730000ULL; break;
	case ELythosLandform::Coast:         Salt = 0x436F617374000000ULL; break;
	case ELythosLandform::Ocean:         Salt = 0x4F6365616E000000ULL; break;
	default:                             Salt = 0x44656661756C7400ULL; break;
	}

	const uint64 SeedHash = FinalizeHash64(
		static_cast<uint64>(Context.PlanetSeed) ^ Salt);

	const uint64 SectorHash = FinalizeHash64(
		SeedHash ^
		static_cast<uint64>(Id.FeatureSector.X) * 0x9E3779B97F4A7C15ULL ^
		static_cast<uint64>(Id.FeatureSector.Y) * 0xBF58476D1CE4E5B9ULL ^
		static_cast<uint64>(Id.FeatureSector.Z) * 0x94D049BB133111EBULL);

	Id.FeatureId = static_cast<uint32>(SectorHash & 0xFFFFFFFFu);
	return Id;
}

float FLythosMacroTerrain::ComputeSeaLevel(const FLythosPlanetContext& Context)
{
	// Water coverage is deterministic from the planet seed.
	const float WaterCoverage = UPlanetBiomeGenerator::CalculatePlanetWaterCoverage(Context.PlanetSeed);

	// Override with profile water coverage if available (profile is authoritative).
	const float EffectiveCoverage = (Context.PlanetProfile.WaterCoverage > 0.0f)
		? Context.PlanetProfile.WaterCoverage
		: WaterCoverage;

	return UPlanetBiomeGenerator::CalculateSeaLevelFromWaterCoverage(EffectiveCoverage);
}

float FLythosMacroTerrain::ComputeOceanSurfaceRadius(const FLythosPlanetContext& Context)
{
	const float SeaLevel = ComputeSeaLevel(Context);
	return Context.PlanetRadius + SeaLevel * Context.TerrainHeight;
}

float FLythosMacroTerrain::ComputeOceanFloorElevation(const FLythosPlanetContext& Context, const FVector& Direction)
{
	const float Normalized = UAndromedaNoiseLibrary::GeneratePlanetHeight(
		Direction.GetSafeNormal(),
		Context.PlanetSeed,
		Context.ContinentalScale,
		Context.MountainScale,
		Context.DetailScale,
		Context.MountainStrength,
		Context.DetailStrength);

	// Oceans are slightly deeper than the lowest land basins.
	const float OceanDepth = FMath::Clamp(
		-0.08f + 0.5f * Normalized,
		-0.12f, -0.04f);

	return OceanDepth * Context.TerrainHeight;
}

float FLythosMacroTerrain::ComputeSlope(const FVector& Direction, const FVector& SurfaceNormal)
{
	const FVector D = Direction.GetSafeNormal();
	const FVector N = SurfaceNormal.GetSafeNormal();
	const float Dot = FVector::DotProduct(D, N);
	return 1.0f - FMath::Clamp(Dot, 0.f, 1.f);
}

FLythoTerrainSample FLythosMacroTerrain::Evaluate(
	const FLythosPlanetContext& Context,
	const FVector& Direction)
{
	FLythoTerrainSample Sample;

	const FVector Dir = Direction.GetSafeNormal();
	Sample.Direction = Dir;
	Sample.Latitude = LythosCoordinates::NormalisedLatitude(Dir);

	float ContinentalMask = 0.f, LandformMask = 0.f, MountainMask = 0.f, HillMask = 0.f;

	const float Elevation = ComposeElevation(
		Context, Dir, &ContinentalMask, &LandformMask, &MountainMask, &HillMask);

	const float TerrainHeight = FMath::Max(Context.TerrainHeight, KINDA_SMALL_NUMBER);

	Sample.Elevation = Elevation;
	Sample.NormalizedElevation = FMath::Clamp(Elevation / TerrainHeight, -1.f, 1.f);

	// Surface normal via the existing validated normal computation.
	Sample.Normal = UAndromedaNoiseLibrary::CalculatePlanetSurfaceNormal(
		Dir,
		Context.PlanetSeed,
		Context.ContinentalScale,
		Context.MountainScale,
		Context.DetailScale,
		Context.MountainStrength,
		Context.DetailStrength,
		Context.TerrainHeight);

	Sample.Slope = ComputeSlope(Dir, Sample.Normal);

	// World-local surface position = direction * (radius + elevation).
	Sample.LocalPosition = Dir * (Context.PlanetRadius + Elevation);

	const float SeaLevel = ComputeSeaLevel(Context);

	Sample.Landform = ClassifyLandform(
		Sample.NormalizedElevation,
		ContinentalMask,
		LandformMask,
		MountainMask,
		HillMask,
		SeaLevel);

	Sample.FeatureId = ComputeFeatureIdentity(
		Context, Dir, Sample.Landform, ContinentalMask, MountainMask);

	return Sample;
}

FLythosRegionData FLythosMacroTerrain::GenerateRegion(
	const FLythosPlanetContext& Context,
	const FLythosRegionCoord& Region)
{
	FLythosRegionData Data;
	Data.RegionId = Region.ToRegionId(Context.PlanetID, Context.PlanetSeed);
	Data.PlanetRadius = Context.PlanetRadius;
	Data.TerrainHeight = Context.TerrainHeight;
	Data.SeaLevel = ComputeSeaLevel(Context);

	TArray<FVector> Directions;
	LythosCoordinates::SampleRegionDirections(Region, Directions);

	const int32 GridSize = FMath::Max(2, Region.GridResolution);
	Data.GridSize = GridSize;

	Data.Samples.Empty(Directions.Num());
	Data.Samples.Reserve(Directions.Num());

	for (const FVector& Dir : Directions)
	{
		Data.Samples.Add(Evaluate(Context, Dir));
	}

	return Data;
}
