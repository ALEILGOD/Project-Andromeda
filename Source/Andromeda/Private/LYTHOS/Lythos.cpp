#include "LYTHOS/Lythos.h"
#include "LYTHOS/LythosCoordinates.h"
#include "AndromedaSeedLibrary.h"

uint64 FLythosRegionId::GetFingerprint() const
{
	uint64 Hash = static_cast<uint64>(PlanetSeed);

	Hash = static_cast<uint64>(PlanetID) ^ (Hash << 32);
	Hash ^= static_cast<uint32>(FaceIndex) + 0x9E3779B9u;
	Hash ^= static_cast<uint32>(RegionX) + 0x9E3779B9u;
	Hash ^= static_cast<uint32>(RegionY) + 0x9E3779B9u;
	Hash ^= static_cast<uint32>(RegionLOD) + 0x9E3779B9u;
	Hash ^= static_cast<uint32>(GenerationVersion) + 0x9E3779B9u;

	// Splitmix64 finalizer.
	Hash ^= Hash >> 30;
	Hash *= 0xBF58476D1CE4E5B9ULL;
	Hash ^= Hash >> 27;
	Hash *= 0x94D049BB133111EBULL;
	Hash ^= Hash >> 31;

	return Hash;
}

FLythosRegionId FLythosRegionCoord::ToRegionId(int64 PlanetID_, int64 PlanetSeed_) const
{
	FLythosRegionId Id;
	Id.PlanetID = PlanetID_;
	Id.PlanetSeed = PlanetSeed_;
	Id.FaceIndex = FaceIndex;
	Id.RegionX = RegionX;
	Id.RegionY = RegionY;
	Id.RegionLOD = RegionLOD;
	Id.GenerationVersion = LYTHOS_GENERATION_VERSION;
	return Id;
}

bool FLythosPlanetContext::IsValid() const
{
	return PlanetRadius > 0.0f && PlanetSeed != 0;
}

FLythosPlanetContext FLythosPlanetContext::FromGenerationData(
	const FPlanetGenerationData& GenData,
	const FVector& InWorldPosition,
	const FVector& InStarPosition,
	float InStarTemperatureKelvin)
{
	FLythosPlanetContext Context;
	Context.PlanetID = GenData.PlanetID;
	Context.PlanetSeed = GenData.PlanetSeed;
	Context.PlanetRadius = GenData.PlanetRadius;
	Context.TerrainHeight = GenData.TerrainHeight;
	Context.WorldPosition = InWorldPosition;
	Context.StarPosition = InStarPosition;
	Context.StarTemperatureKelvin = InStarTemperatureKelvin;
	Context.OrbitDistance = GenData.OrbitDistance;
	Context.PlanetProfile = GenData.PlanetProfile;
	Context.ContinentalScale = GenData.ContinentalScale;
	Context.MountainScale = GenData.MountainScale;
	Context.DetailScale = GenData.DetailScale;
	Context.MountainStrength = GenData.MountainStrength;
	Context.DetailStrength = GenData.DetailStrength;
	return Context;
}

FLythosPlanetContext FLythosPlanetContext::FromRuntimeData(
	const FPlanetRuntimeData& RuntimeData,
	const FVector& InStarPosition,
	float InStarTemperatureKelvin)
{
	FLythosPlanetContext Context;
	Context.PlanetID = RuntimeData.PlanetID;
	Context.PlanetSeed = RuntimeData.PlanetSeed;
	Context.PlanetRadius = RuntimeData.PlanetRadius;
	Context.TerrainHeight = RuntimeData.TerrainHeight;
	Context.WorldPosition = RuntimeData.WorldPosition;
	Context.StarPosition = InStarPosition;
	Context.StarTemperatureKelvin = InStarTemperatureKelvin;
	Context.OrbitDistance = RuntimeData.OrbitDistance;
	// Note: FPlanetRuntimeData does not carry a full Profile; we leave defaults.
	// The profile is authoritative only on the APlanet / generation data.
	Context.ContinentalScale = 0.5f;
	Context.MountainScale = 3.0f;
	Context.DetailScale = 12.0f;
	Context.MountainStrength = 1.5f;
	Context.DetailStrength = 0.1f;
	return Context;
}
