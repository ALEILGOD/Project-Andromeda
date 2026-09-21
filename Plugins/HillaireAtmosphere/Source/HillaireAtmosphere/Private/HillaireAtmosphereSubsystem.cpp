#include "HillaireAtmosphereSubsystem.h"

#include "HillaireAtmosphereComponent.h"
#include "HillaireAtmosphereLightComponent.h"
#include "HillaireAtmosphereLog.h"
#include "HillaireLimits.h"
#include "HillaireLutManager.h"
#include "HillaireViewExtension.h"
#include "SceneViewExtension.h"

static TAutoConsoleVariable<int32> CVarHillaireEnable(
	TEXT("r.Hillaire.Enable"),
	1,
	TEXT("Master switch for the Hillaire atmosphere view extension (0 disables snapshot builds and RDG work)."),
	ECVF_Default);

bool UHillaireAtmosphereSubsystem::IsEnabledByCVar()
{
	return CVarHillaireEnable.GetValueOnGameThread() != 0;
}

UHillaireAtmosphereSubsystem::~UHillaireAtmosphereSubsystem() = default;

void UHillaireAtmosphereSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LutManager = MakeUnique<FHillaireLutManager>();
	// Old subsystem: do NOT register view extension (legacy path).
	// The new multiplanetary system uses UHillairePlanetaryAtmosphereSubsystem
	// with FHillairePlanetaryViewExtension instead.
	// ViewExtension = FSceneViewExtensions::NewExtension<FHillaireViewExtension>(this);
	UE_LOG(LogHillaireAtmosphere, Log, TEXT("Legacy Subsystem initialized (world %s) - view extension DISABLED."), *GetWorld()->GetName());
}

void UHillaireAtmosphereSubsystem::Deinitialize()
{
	// ViewExtension.Reset(); // Not created in Initialize
	if (LutManager)
	{
		LutManager->Clear();
		LutManager.Reset();
	}
	PlanetComponents.Empty();
	LightComponents.Empty();
	ExternalLights.Empty();
	ExternalPlanets.Empty();
	Super::Deinitialize();
}

bool UHillaireAtmosphereSubsystem::IsPlanetSlotUsed(int32 Slot) const
{
	if (Slot < 0 || Slot >= HILLAIRE_MAX_PLANETS)
	{
		return true;
	}
	for (const TObjectPtr<UHillaireAtmosphereComponent>& C : PlanetComponents)
	{
		if (C && C->PlanetId == Slot)
		{
			return true;
		}
	}
	for (const FHillairePlanetState& S : ExternalPlanets)
	{
		if (S.PlanetId == Slot)
		{
			return true;
		}
	}
	return false;
}

int32 UHillaireAtmosphereSubsystem::AssignPlanetSlot() const
{
	for (int32 i = 0; i < HILLAIRE_MAX_PLANETS; ++i)
	{
		if (!IsPlanetSlotUsed(i))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UHillaireAtmosphereSubsystem::RegisterExternalPlanetSlot()
{
	const int32 Slot = AssignPlanetSlot();
	if (Slot == INDEX_NONE)
	{
		UE_LOG(LogHillaireAtmosphere, Error,
			TEXT("Planet registry full (%d atmospheres); external planet NOT registered."),
			HillaireLimits::MaxPlanets);
		return INDEX_NONE;
	}
	// Idempotent in the manager (adds only when missing): the slot's LUT
	// cache is created once here and survives every UpdateExternalPlanet.
	if (LutManager)
	{
		LutManager->RegisterPlanet(Slot);
	}
	FHillairePlanetState Placeholder;
	Placeholder.PlanetId = Slot;
	ExternalPlanets.Add(Placeholder);
	return Slot;
}

void UHillaireAtmosphereSubsystem::UpdateExternalPlanet(const FHillairePlanetState& State)
{
	if (State.PlanetId == INDEX_NONE)
	{
		return;
	}
	for (FHillairePlanetState& S : ExternalPlanets)
	{
		if (S.PlanetId == State.PlanetId)
		{
			S = State;
			return;
		}
	}
	// Unknown slot (e.g. subsystem restarted): adopt it if free so live
	// planets heal without re-registration.
	if (!IsPlanetSlotUsed(State.PlanetId))
	{
		if (LutManager)
		{
			LutManager->RegisterPlanet(State.PlanetId);
		}
		ExternalPlanets.Add(State);
	}
}

void UHillaireAtmosphereSubsystem::UnregisterExternalPlanet(int32 PlanetId)
{
	ExternalPlanets.RemoveAll(
		[&](const FHillairePlanetState& S) { return S.PlanetId == PlanetId; });
	// Release the LUT targets with the slot (same policy as components).
	if (LutManager && PlanetId != INDEX_NONE)
	{
		LutManager->UnregisterPlanet(PlanetId);
	}
}

int32 UHillaireAtmosphereSubsystem::RegisterPlanetComponent(UHillaireAtmosphereComponent* Component)
{
	if (!Component)
	{
		return INDEX_NONE;
	}
	PlanetComponents.AddUnique(Component);
	const int32 Slot = AssignPlanetSlot();
	if (Slot == INDEX_NONE)
	{
		UE_LOG(LogHillaireAtmosphere, Error,
			TEXT("Planet registry full (%d atmospheres); '%s' NOT registered."),
			HillaireLimits::MaxPlanets, *Component->GetName());
		PlanetComponents.Remove(Component);
		return INDEX_NONE;
	}
	if (LutManager)
	{
		LutManager->RegisterPlanet(Slot);
	}
	return Slot;
}

void UHillaireAtmosphereSubsystem::UnregisterPlanetComponent(UHillaireAtmosphereComponent* Component)
{
	if (!Component)
	{
		return;
	}
	const int32 Slot = Component->PlanetId;
	PlanetComponents.Remove(Component);
	// Release the LUT targets with the slot (no cross-planet reuse in v1:
	// two identical profiles keep separate handles; sharing is an optimization).
	if (LutManager && Slot != INDEX_NONE)
	{
		LutManager->UnregisterPlanet(Slot);
	}
}

int32 UHillaireAtmosphereSubsystem::GetRegisteredPlanetCount() const
{
	// External slots count only once they carry a valid planet (a bare slot
	// reservation must not enable the view extension by itself).
	int32 ValidExternals = 0;
	for (const FHillairePlanetState& S : ExternalPlanets)
	{
		if (S.IsValid())
		{
			++ValidExternals;
		}
	}
	return PlanetComponents.Num() + ValidExternals;
}

void UHillaireAtmosphereSubsystem::RegisterLightComponent(UHillaireAtmosphereLightComponent* Component)
{
	if (!Component)
	{
		return;
	}
	LightComponents.AddUnique(Component);
}

void UHillaireAtmosphereSubsystem::UnregisterLightComponent(UHillaireAtmosphereLightComponent* Component)
{
	LightComponents.Remove(Component);
}

int32 UHillaireAtmosphereSubsystem::GetRegisteredLightCount() const
{
	return LightComponents.Num();
}

void UHillaireAtmosphereSubsystem::GetPlanetStates(TArray<FHillairePlanetState>& OutStates) const
{
	OutStates.Reset();
	for (const TObjectPtr<UHillaireAtmosphereComponent>& C : PlanetComponents)
	{
		if (!C)
		{
			continue;
		}
		FHillairePlanetState S;
		if (!C->BuildPlanetState(S))
		{
			continue;
		}
		FString Error;
		if (!S.IsValid(&Error))
		{
			UE_LOG(LogHillaireAtmosphere, Warning,
				TEXT("Skipping invalid planet '%s': %s"), *C->GetName(), *Error);
			continue;
		}
		// Reattach the persisted LUT cache (snapshot rebuilds are stateless;
		// validity flags live in the manager, keyed by slot). Locked copy:
		// the slot cannot vanish mid-copy on the render thread's behalf.
		if (LutManager)
		{
			LutManager->CopyLutState(S.PlanetId, S.Lut);
		}
		OutStates.Add(S);
	}
	// Phase 2F: live procedural planets (same validation + cache reattach as
	// components; placeholders from slot assignment never validate, so they
	// are skipped until the first real push arrives).
	for (const FHillairePlanetState& E : ExternalPlanets)
	{
		FString Error;
		if (!E.IsValid(&Error))
		{
			continue;
		}
		FHillairePlanetState S = E;
		if (LutManager)
		{
			LutManager->CopyLutState(S.PlanetId, S.Lut);
		}
		OutStates.Add(S);
	}
}

void UHillaireAtmosphereSubsystem::GetLightSources(TArray<FHillaireLightSource>& OutSources) const
{
	TArray<FHillaireLightSource> ComponentLights;
	for (const TObjectPtr<UHillaireAtmosphereLightComponent>& C : LightComponents)
	{
		if (!C)
		{
			continue;
		}
		FHillaireLightSource L;
		if (C->BuildLightSource(L))
		{
			ComponentLights.Add(L);
		}
	}
	// Phase 2E: the STARMAP primary star (external feed) owns slot 0, ahead
	// of every component light. With no external feed this is the identity
	// (empty head), so all pre-2E behavior is bit-stable.
	OutSources = MergeExternalAndComponentLights(ExternalLights, ComponentLights);
}

void UHillaireAtmosphereSubsystem::RegisterExternalLight(const FHillaireLightSource& Light)
{
	for (FHillaireLightSource& E : ExternalLights)
	{
		if (E.LightId == Light.LightId)
		{
			E = Light;
			return;
		}
	}
	ExternalLights.Add(Light);
}

void UHillaireAtmosphereSubsystem::UnregisterExternalLight(const FGuid& LightId)
{
	ExternalLights.RemoveAll(
		[&](const FHillaireLightSource& E) { return E.LightId == LightId; });
}

TArray<FHillaireLightSource> UHillaireAtmosphereSubsystem::MergeExternalAndComponentLights(
	const TArray<FHillaireLightSource>& InExternalLights,
	const TArray<FHillaireLightSource>& ComponentLights)
{
	TArray<FHillaireLightSource> Out;
	Out.Reserve(InExternalLights.Num() + ComponentLights.Num());
	for (const FHillaireLightSource& L : InExternalLights)
	{
		Out.Add(L);
	}
	for (const FHillaireLightSource& L : ComponentLights)
	{
		Out.Add(L);
	}
	return Out;
}

FHillaireViewSnapshot UHillaireAtmosphereSubsystem::BuildSnapshotForView(
	const FVector& ViewOriginCm,
	const FMatrix& ViewMatrix,
	const FMatrix& ProjectionMatrix,
	const FIntRect& ViewRect,
	const FVector& ViewDirectionWorld) const
{
	TArray<FHillairePlanetState> Planets;
	TArray<FHillaireLightSource> Lights;
	GetPlanetStates(Planets);
	GetLightSources(Lights);
	// Resolve the hysteresis incumbent to a live array index (order may
	// shift on spawn churn; the Guid pins the identity across slot reuse).
	// Stale incumbent (planet gone) resolves to INDEX_NONE = pure reference
	// policy, so teardown can never wedge the selection.
	int32 IncumbentIndex = INDEX_NONE;
	if (LastGoverningPlanetId != INDEX_NONE)
	{
		for (int32 i = 0; i < Planets.Num(); ++i)
		{
			if (Planets[i].PlanetId == LastGoverningPlanetId
				&& Planets[i].PlanetGuid == LastGoverningPlanetGuid)
			{
				IncumbentIndex = i;
				break;
			}
		}
	}
	FHillaireViewSnapshot Snapshot = FHillaireViewSnapshotBuilder::Build(
		Planets, Lights, ViewOriginCm, ViewMatrix, ProjectionMatrix, ViewRect, ViewDirectionWorld,
		IncumbentIndex);
	// Stamp render knobs for RT consumption (GameThread read of own members).
	Snapshot.MultipleScatteringFactor = MultipleScatteringFactor;
	Snapshot.bFastSkyEnabled = bFastSkyEnabled;
	// Remember the governing planet for the next frame's hysteresis. A view
	// with no content clears the memory (no stale incumbent across level
	// transitions or full unregisters).
	if (Snapshot.HasAtmosphereContent())
	{
		for (const FHillaireSnapshotPlanet& P : Snapshot.Planets)
		{
			if (P.bIsGoverning)
			{
				LastGoverningPlanetId = P.PlanetId;
				LastGoverningPlanetGuid = P.PlanetGuid;
				break;
			}
		}
	}
	else
	{
		LastGoverningPlanetId = INDEX_NONE;
		LastGoverningPlanetGuid = FGuid();
	}
	return Snapshot;
}

void UHillaireAtmosphereSubsystem::InvalidatePlanetLuts(int32 PlanetId)
{
	if (LutManager)
	{
		LutManager->InvalidatePlanetById(PlanetId);
	}
}
