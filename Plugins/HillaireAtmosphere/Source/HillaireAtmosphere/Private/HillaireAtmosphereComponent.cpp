#include "HillaireAtmosphereComponent.h"

#include "GameFramework/Actor.h"
#include "HillaireAtmosphereLog.h"
#include "HillaireAtmosphereSubsystem.h"

UHillaireAtmosphereComponent::UHillaireAtmosphereComponent()
{
	// No per-frame tick: registration is event-driven (BeginPlay/EndPlay +
	// explicit MarkProfileDirty). The renderer pulls snapshots on demand.
	PrimaryComponentTick.bCanEverTick = false;
}

void UHillaireAtmosphereComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterWithSubsystem();
}

void UHillaireAtmosphereComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSubsystem();
	Super::EndPlay(EndPlayReason);
}

void UHillaireAtmosphereComponent::RegisterWithSubsystem()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (PlanetName.IsNone())
	{
		if (AActor* Owner = GetOwner())
		{
			PlanetName = FName(*Owner->GetName());
		}
	}
	if (!PlanetGuid.IsValid())
	{
		PlanetGuid = FGuid::NewGuid();
	}
	if (UHillaireAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillaireAtmosphereSubsystem>())
	{
		PlanetId = Subsystem->RegisterPlanetComponent(this);
	}
}

void UHillaireAtmosphereComponent::UnregisterFromSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UHillaireAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillaireAtmosphereSubsystem>())
		{
			Subsystem->UnregisterPlanetComponent(this);
		}
	}
	PlanetId = INDEX_NONE;
}

bool UHillaireAtmosphereComponent::BuildPlanetState(FHillairePlanetState& OutState) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	OutState = FHillairePlanetState();
	OutState.PlanetId = PlanetId;
	OutState.PlanetGuid = PlanetGuid;
	OutState.PlanetName = PlanetName;
	OutState.CenterCmWorld = Owner->GetActorLocation();
	OutState.RotationWorld = Owner->GetActorQuat();
	// Single write path for radii via the centralized FASE-2 builder: the
	// authored ground + height fold into a thickness-normalized profile,
	// which is authoritative downstream. Derived mirrors stay equal by
	// construction. Never assign Profile.Bottom/TopRadiusKm directly here.
	OutState.Profile = HillaireBuildNormalizedProfile(Profile, GroundRadiusKm, AtmosphereHeightKm);
	OutState.GroundRadiusKm = OutState.Profile.BottomRadiusKm;
	OutState.AtmosphereRadiusKm = OutState.Profile.TopRadiusKm;
	OutState.TerrainHeightKm = TerrainHeightKm;
	OutState.StarDistanceKm = StarDistanceKm;
	return true;
}

bool UHillaireAtmosphereComponent::BuildPlanetAtmosphereState(FPlanetAtmosphereState& OutState) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	OutState = FPlanetAtmosphereState();
	OutState.PlanetId = PlanetGuid;
	OutState.PlanetName = PlanetName;
	OutState.CenterWS = Owner->GetActorLocation();
	OutState.RotationWS = Owner->GetActorQuat();
	// Single write path for radii via the centralized FASE-2 builder
	OutState.Profile = HillaireBuildNormalizedProfile(Profile, GroundRadiusKm, AtmosphereHeightKm);
	OutState.GroundRadiusKm = OutState.Profile.BottomRadiusKm;
	OutState.AtmosphereTopRadiusKm = OutState.Profile.TopRadiusKm;
	OutState.TerrainHeightKm = TerrainHeightKm;
	OutState.StarDistanceKm = StarDistanceKm;
	OutState.bValid = true;
	return true;
}

void UHillaireAtmosphereComponent::MarkProfileDirty()
{
	if (UWorld* World = GetWorld())
	{
		if (UHillaireAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillaireAtmosphereSubsystem>())
		{
			Subsystem->InvalidatePlanetLuts(PlanetId);
		}
	}
}
