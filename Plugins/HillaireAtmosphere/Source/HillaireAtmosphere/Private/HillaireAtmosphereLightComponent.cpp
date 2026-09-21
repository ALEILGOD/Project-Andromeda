#include "HillaireAtmosphereLightComponent.h"

#include "Components/LightComponent.h"
#include "GameFramework/Actor.h"
#include "HillaireAtmosphereSubsystem.h"

UHillaireAtmosphereLightComponent::UHillaireAtmosphereLightComponent()
{
	// No per-frame tick: collection is pull-driven by snapshot builds.
	PrimaryComponentTick.bCanEverTick = false;
}

void UHillaireAtmosphereLightComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterWithSubsystem();
}

void UHillaireAtmosphereLightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSubsystem();
	Super::EndPlay(EndPlayReason);
}

void UHillaireAtmosphereLightComponent::RegisterWithSubsystem()
{
	if (!LightGuid.IsValid())
	{
		LightGuid = FGuid::NewGuid();
	}
	if (LightName.IsNone())
	{
		if (AActor* Owner = GetOwner())
		{
			LightName = FName(*Owner->GetName());
		}
	}
	if (UWorld* World = GetWorld())
	{
		if (UHillaireAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillaireAtmosphereSubsystem>())
		{
			Subsystem->RegisterLightComponent(this);
		}
	}
}

void UHillaireAtmosphereLightComponent::UnregisterFromSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UHillaireAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillaireAtmosphereSubsystem>())
		{
			Subsystem->UnregisterLightComponent(this);
		}
	}
}

bool UHillaireAtmosphereLightComponent::BuildLightSource(FHillaireLightSource& OutSource) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	OutSource = FHillaireLightSource();
	OutSource.LightId = LightGuid;
	OutSource.LightName = LightName;
	OutSource.bDirectional = bDirectional;
	OutSource.AngularRadiusRad = AngularRadiusRad;
	OutSource.bDrawDisk = bDrawDisk;

	const ULightComponent* OwnerLight = Owner->FindComponentByClass<ULightComponent>();
	if (OwnerLight)
	{
		// Live read: the ULightComponent stays the source of truth and keeps
		// working normally for the UE renderer (direct lighting, shadows);
		// this component only ADDS atmosphere participation.
		const bool bOwnerActive = OwnerLight->IsVisible();
		OutSource.bEnabled = bEnabled && bOwnerActive;
		OutSource.WorldPositionCm = OwnerLight->GetComponentLocation();
		OutSource.Color = OwnerLight->GetLightColor();
		OutSource.Intensity = OwnerLight->Intensity;
		if (bDirectional)
		{
			// Direction TOWARD the light. Travel direction is the owner -Z
			// (directional-light proxy convention); PROVISIONAL: verified by
			// the sun-facing scenario in the rendering phase.
			const FQuat OwnerQ = OwnerLight->GetComponentQuat();
			OutSource.WorldDirectionToLight = OwnerQ.GetAxisZ().GetSafeNormal();
		}
	}
	else
	{
		// No owner light (test sun rig): own properties are the source.
		OutSource.bEnabled = bEnabled;
		OutSource.WorldPositionCm = Owner->GetActorLocation();
		OutSource.Color = Color;
		OutSource.Intensity = Intensity;
		if (bDirectional)
		{
			OutSource.WorldDirectionToLight = Owner->GetActorQuat().GetAxisZ().GetSafeNormal();
		}
	}
	return true;
}
