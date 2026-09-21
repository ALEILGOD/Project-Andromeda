#include "HillairePlanetaryAtmosphereSubsystem.h"

#include "HillaireAtmosphereComponent.h"
#include "HillaireAtmosphereLightComponent.h"
#include "HillaireAtmosphereLog.h"
#include "HillaireHash.h"
#include "HillaireLimits.h"
#include "HillaireLutManager.h"
#include "HillairePlanetaryViewExtension.h"
#include "HillairePlanetAtmosphereState.h"
#include "SceneViewExtension.h"

static TAutoConsoleVariable<int32> CVarHillaireEnable(
	TEXT("r.Hillaire.Enable"),
	1,
	TEXT("Master switch for the Hillaire planetary atmosphere system (0 disables)."),
	ECVF_Default);

bool UHillairePlanetaryAtmosphereSubsystem::IsEnabledByCVar()
{
	return CVarHillaireEnable.GetValueOnGameThread() != 0;
}

UHillairePlanetaryAtmosphereSubsystem::~UHillairePlanetaryAtmosphereSubsystem() = default;

void UHillairePlanetaryAtmosphereSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LutManager = MakeUnique<FHillaireLutManager>();
	ViewExtension = FSceneViewExtensions::NewExtension<FHillairePlanetaryViewExtension>(this);
	UE_LOG(LogHillaireAtmosphere, Log, TEXT("Planetary Atmosphere Subsystem initialized (world %s)."), *GetWorld()->GetName());
}

void UHillairePlanetaryAtmosphereSubsystem::Deinitialize()
{
	ViewExtension.Reset();
	if (LutManager)
	{
		LutManager->Clear();
		LutManager.Reset();
	}
	PlanetRegistry.Empty();
	StarRegistry.Empty();
	PlanetSunOverrides.Empty();
	CurrentFrameSnapshot.Reset();
	NextFrameSnapshot.Reset();
	{
		FScopeLock Lock(&StashLock);
		SnapshotStash.Empty();
	}
	Super::Deinitialize();
}

int32 UHillairePlanetaryAtmosphereSubsystem::FindPlanetIndex(const FGuid& PlanetId) const
{
	for (int32 i = 0; i < PlanetRegistry.Num(); ++i)
	{
		if (PlanetRegistry[i].PlanetId == PlanetId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UHillairePlanetaryAtmosphereSubsystem::FindStarIndex(const FGuid& StarId) const
{
	for (int32 i = 0; i < StarRegistry.Num(); ++i)
	{
		if (StarRegistry[i].StarId == StarId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UHillairePlanetaryAtmosphereSubsystem::RegisterPlanetComponent(UHillaireAtmosphereComponent* Component)
{
	if (!Component)
	{
		return INDEX_NONE;
	}

	// Assign stable PlanetId from component's Guid
	FGuid PlanetId = Component->PlanetGuid;
	if (!PlanetId.IsValid())
	{
		PlanetId = FGuid::NewGuid();
		Component->PlanetGuid = PlanetId;
	}

	const int32 ExistingIdx = FindPlanetIndex(PlanetId);
	if (ExistingIdx != INDEX_NONE)
	{
		PlanetRegistry[ExistingIdx].Component = Component;
		return ExistingIdx;
	}

	FPlanetEntry Entry;
	Entry.PlanetId = PlanetId;
	Entry.PlanetName = Component->PlanetName.IsNone() ? FName(*Component->GetOwner()->GetName()) : Component->PlanetName;
	Entry.Component = Component;
	Entry.CurrentState = FPlanetAtmosphereState::MakeInvalid(PlanetId, Entry.PlanetName);
	PlanetRegistry.Add(Entry);

	if (LutManager)
	{
		// Register with LUT manager using array index as slot
		LutManager->RegisterPlanet(PlanetRegistry.Num() - 1);
	}

	return PlanetRegistry.Num() - 1;
}

void UHillairePlanetaryAtmosphereSubsystem::UnregisterPlanetComponent(UHillaireAtmosphereComponent* Component)
{
	if (!Component) return;

	const int32 Idx = FindPlanetIndex(Component->PlanetGuid);
	if (Idx != INDEX_NONE)
	{
		if (LutManager)
		{
			LutManager->UnregisterPlanet(Idx);
		}
		PlanetSunOverrides.Remove(Component->PlanetGuid);
		PlanetRegistry.RemoveAt(Idx);
	}
}

FGuid UHillairePlanetaryAtmosphereSubsystem::RegisterExternalPlanet(const FGuid& PlanetId, const FName& PlanetName)
{
	if (!PlanetId.IsValid())
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[Subsystem] RegisterExternalPlanet: invalid PlanetId"));
		return FGuid();
	}

	const int32 ExistingIdx = FindPlanetIndex(PlanetId);
	if (ExistingIdx != INDEX_NONE)
	{
		PlanetRegistry[ExistingIdx].PlanetName = PlanetName;
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] RegisterExternalPlanet: planet %s already exists at index %d"), *PlanetId.ToString(), ExistingIdx);
		return PlanetId;
	}

	FPlanetEntry Entry;
	Entry.PlanetId = PlanetId;
	Entry.PlanetName = PlanetName;
	Entry.Component = nullptr;
	Entry.CurrentState = FPlanetAtmosphereState::MakeInvalid(PlanetId, PlanetName);
	PlanetRegistry.Add(Entry);

	if (LutManager)
	{
		LutManager->RegisterPlanet(PlanetRegistry.Num() - 1);
	}

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] RegisterExternalPlanet: registered %s (%s) at index %d"), *PlanetName.ToString(), *PlanetId.ToString(), PlanetRegistry.Num() - 1);

	return PlanetId;
}

void UHillairePlanetaryAtmosphereSubsystem::UnregisterExternalPlanet(const FGuid& PlanetId)
{
	const int32 Idx = FindPlanetIndex(PlanetId);
	if (Idx != INDEX_NONE)
	{
		if (LutManager)
		{
			LutManager->UnregisterPlanet(Idx);
		}
		PlanetRegistry.RemoveAt(Idx);
	}
	PlanetSunOverrides.Remove(PlanetId);
}

void UHillairePlanetaryAtmosphereSubsystem::SetPlanetSunDirection(
	const FGuid& PlanetId,
	const FVector& DirectionToStarWorld,
	float EffectiveIntensity)
{
	if (!PlanetId.IsValid())
	{
		return;
	}
	const FVector SafeDir = DirectionToStarWorld.GetSafeNormal();
	if (SafeDir.IsNearlyZero() || !FMath::IsFinite(EffectiveIntensity) || EffectiveIntensity < 0.0f)
	{
		return;
	}
	FPlanetSunOverride Override;
	Override.DirectionToStarWorld = SafeDir;
	Override.Intensity = EffectiveIntensity;
	PlanetSunOverrides.Add(PlanetId, Override);
}

const UHillairePlanetaryAtmosphereSubsystem::FPlanetSunOverride* UHillairePlanetaryAtmosphereSubsystem::FindPlanetSunOverride(const FGuid& PlanetId) const
{
	return PlanetSunOverrides.Find(PlanetId);
}

void UHillairePlanetaryAtmosphereSubsystem::UpdateExternalPlanet(const FPlanetAtmosphereState& State)
{
	if (!State.PlanetId.IsValid()) 
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[Subsystem] UpdateExternalPlanet: invalid PlanetId"));
		return;
	}

	const int32 Idx = FindPlanetIndex(State.PlanetId);
	if (Idx == INDEX_NONE)
	{
		// Auto-register if not yet registered (heals after subsystem restart)
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] UpdateExternalPlanet: auto-registering planet %s (%s)"), *State.PlanetName.ToString(), *State.PlanetId.ToString());
		RegisterExternalPlanet(State.PlanetId, State.PlanetName);
		return;
	}

	// Store as pending state for next frame snapshot. A worldless subsystem
	// (bare NewObject in unit tests) has no frame-snapshot consumer, so the
	// update must also land in CurrentState immediately; the pending copy is
	// still kept because ResolveLightsForAllPlanets resolves into it. With a
	// world, the pending->current handoff in BuildNextFrameSnapshot is the
	// GT/RT-safe path and CurrentState is left untouched here.
	PlanetRegistry[Idx].PendingState = State;
	PlanetRegistry[Idx].bHasPendingState = true;
	if (!GetWorld())
	{
		PlanetRegistry[Idx].CurrentState = State;
	}

	const UWorld* W = GetWorld();
	UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[Subsystem] UpdateExternalPlanet: Sub=%p World=%s GFrame=%llu Idx=%d Planet=%s Star=%s"),
		this, W ? *W->GetName() : TEXT("<null>"), (unsigned long long)GFrameCounter, Idx,
		*State.PlanetId.ToString(), *State.StarId.ToString());
}

void UHillairePlanetaryAtmosphereSubsystem::GetAllPlanetStates(TArray<FPlanetAtmosphereState>& OutStates) const
{
	OutStates.Reset();
	OutStates.Reserve(PlanetRegistry.Num());
	for (const FPlanetEntry& Entry : PlanetRegistry)
	{
		if (Entry.CurrentState.IsValid())
		{
			OutStates.Add(Entry.CurrentState);
		}
	}
}

void UHillairePlanetaryAtmosphereSubsystem::RegisterStarComponent(UHillaireAtmosphereLightComponent* Component)
{
	if (!Component) return;
	// Components not used in new architecture for stars; external stars are primary
}

void UHillairePlanetaryAtmosphereSubsystem::UnregisterStarComponent(UHillaireAtmosphereLightComponent* Component)
{
	// No-op for new architecture
}

void UHillairePlanetaryAtmosphereSubsystem::RegisterExternalStar(const FGuid& StarId, const FHillaireLightSource& Light)
{
	const int32 Idx = FindStarIndex(StarId);
	if (Idx != INDEX_NONE)
	{
		StarRegistry[Idx].Light = Light;
		UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[Subsystem] RegisterExternalStar: updated existing star %s"), *StarId.ToString());
		return;
	}

	FStarEntry Entry;
	Entry.StarId = StarId;
	Entry.Light = Light;
	Entry.Component = nullptr;
	StarRegistry.Add(Entry);

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] RegisterExternalStar: NEW star registered %s"), *StarId.ToString());
}

void UHillairePlanetaryAtmosphereSubsystem::UnregisterExternalStar(const FGuid& StarId)
{
	const int32 Idx = FindStarIndex(StarId);
	if (Idx != INDEX_NONE)
	{
		StarRegistry.RemoveAt(Idx);
	}
}

void UHillairePlanetaryAtmosphereSubsystem::GetAllLightSources(TArray<FHillaireLightSource>& OutSources) const
{
	OutSources.Reset();
	OutSources.Reserve(StarRegistry.Num());
	for (const FStarEntry& Entry : StarRegistry)
	{
		if (Entry.Light.IsActive())
		{
			OutSources.Add(Entry.Light);
		}
	}
}

void UHillairePlanetaryAtmosphereSubsystem::UpdatePlanetStatesFromComponents()
{
	for (FPlanetEntry& Entry : PlanetRegistry)
	{
		if (Entry.Component)
		{
			FPlanetAtmosphereState State;
			if (Entry.Component->BuildPlanetAtmosphereState(State))
			{
				Entry.PendingState = State;
				Entry.bHasPendingState = true;
			}
		}
	}
}

void UHillairePlanetaryAtmosphereSubsystem::ResolveLightsForAllPlanets()
{
	// Collect all active lights
	TArray<FHillaireLightSource> ActiveLights;
	GetAllLightSources(ActiveLights);

	{
		const UWorld* W = GetWorld();
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] ResolveLights: Sub=%p World=%s GFrame=%llu StarRegistry=%d ActiveLights=%d Planets=%d"),
			this, W ? *W->GetName() : TEXT("<null>"), (unsigned long long)GFrameCounter,
			StarRegistry.Num(), ActiveLights.Num(), PlanetRegistry.Num());
	}

	// For each planet, resolve its star and other lights
	for (FPlanetEntry& Entry : PlanetRegistry)
	{
		if (!Entry.PendingState.IsValid())
		{
			UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[Subsystem] ResolveLights: skip planet %s (PendingState invalid)"),
				*Entry.PlanetId.ToString());
			continue;
		}

		FPlanetAtmosphereState& State = Entry.PendingState;

		// Find this planet's primary star
		FHillaireLightSource PrimaryStar;
		bool bFoundPrimary = false;
		for (const FStarEntry& StarEntry : StarRegistry)
		{
			if (StarEntry.StarId == State.StarId && StarEntry.Light.IsActive())
			{
				PrimaryStar = StarEntry.Light;
				bFoundPrimary = true;
				break;
			}
		}
		if (!bFoundPrimary)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[Subsystem] ResolveLights: planet %s StarId=%s has NO matching registered star (registry=%d)"),
				*Entry.PlanetId.ToString(), *State.StarId.ToString(), StarRegistry.Num());
		}

		// Build resolved lights: primary star first, then other active lights
		TArray<FHillaireLightSource> PlanetLights;
		if (bFoundPrimary)
		{
			PlanetLights.Add(PrimaryStar);
		}
		for (const FHillaireLightSource& Light : ActiveLights)
		{
			if (bFoundPrimary && Light.LightId == PrimaryStar.LightId) continue;
			if (Light.IsActive())
			{
				PlanetLights.Add(Light);
			}
		}

		// Resolve for this planet (planet-local directions, attenuation)
		State.ResolvedLights = HillaireCompactLightsForPlanet(
			PlanetLights,
			FVector::ZeroVector, // ViewOrigin not needed for direction resolution
			FVector3f::ZeroVector, // CenterCamRelative - will be computed at snapshot
			State.RotationWS);
		
		// Override first light with primary star's resolved data if we found it
		if (bFoundPrimary && State.ResolvedLights.Count > 0)
		{
			// Per-planet sun (multiplanetary correction): the shared registry
			// vector is ONE direction for the whole system, but this planet's
			// true Planet->Star direction (and effective intensity) is its own.
			// The StarLink pushes both per planet via SetPlanetSunDirection;
			// prefer them whenever present. Planets without an entry keep the
			// legacy shared values (manual/test path).
			FVector SunWorld = PrimaryStar.WorldDirectionToLight;
			float SunIntensity = PrimaryStar.Intensity;
			if (const FPlanetSunOverride* Override = FindPlanetSunOverride(State.PlanetId))
			{
				SunWorld = Override->DirectionToStarWorld;
				SunIntensity = Override->Intensity;
			}
			// Convert world-space star direction to planet-local
			FVector3f StarDirLocal = HillairePlanetMath::WorldDirectionToPlanetLocal(State.RotationWS, SunWorld);
			State.ResolvedLights.Lights[0].LightDirLocal = StarDirLocal;
			State.ResolvedLights.Lights[0].ColorAttenuation = FVector3f(PrimaryStar.Color.R, PrimaryStar.Color.G, PrimaryStar.Color.B) * SunIntensity;
			State.ResolvedLights.Lights[0].AngularRadiusRad = PrimaryStar.AngularRadiusRad;
			State.ResolvedLights.Lights[0].bDrawDisk = PrimaryStar.bDrawDisk;
			State.ResolvedLights.Lights[0].LightId = PrimaryStar.LightId;

			// Store for snapshot
			State.StarDirectionLocal = StarDirLocal;
			State.StarDirectionWorld = SunWorld.GetSafeNormal();
			State.StarIrradiance = State.ResolvedLights.Lights[0].ColorAttenuation;
		}

		// Mark generation version
		State.GenerationVersion++;
		State.LastUpdateFrame = GFrameCounter;

		if (State.ResolvedLights.Count == 0)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[Subsystem] ResolveLights: planet %s resolved Lights=0 (registry=%d active=%d foundPrimary=%d)"),
				*Entry.PlanetId.ToString(), StarRegistry.Num(), ActiveLights.Num(), bFoundPrimary ? 1 : 0);
		}
	}
}

FHillaireAtmosphereFrameState UHillairePlanetaryAtmosphereSubsystem::BuildFrameState(
	const FVector& ViewOriginWS,
	const FMatrix& ViewMatrix,
	const FMatrix& ProjectionMatrix,
	const FIntRect& ViewRect,
	const FVector& ViewDirectionWS) const
{
	FHillaireAtmosphereFrameState Frame;
	Frame.FrameNumber = CurrentFrameNumber + 1;
	Frame.GTFrameCounter = GFrameCounter;
	Frame.ViewOriginWS = ViewOriginWS;
	Frame.ViewMatrix = ViewMatrix;
	Frame.ProjectionMatrix = ProjectionMatrix;
	Frame.ViewRect = ViewRect;
	Frame.ViewDirectionWS = ViewDirectionWS;
	Frame.MultipleScatteringFactor = MultipleScatteringFactor;
	Frame.bFastSkyEnabled = bFastSkyEnabled;

	// Collect valid planet states with camera-relative data
	TArray<FPlanetAtmosphereState> ValidPlanets;
	ValidPlanets.Reserve(PlanetRegistry.Num());

	// Camera in its own relative frame is origin
	const FVector3f CameraRelKm = FVector3f::ZeroVector;

	UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[Subsystem] BuildFrameState: ViewOriginWS=%s RegistryCount=%d"), *ViewOriginWS.ToString(), PlanetRegistry.Num());

	for (int32 RegIdx = 0; RegIdx < PlanetRegistry.Num(); ++RegIdx)
	{
		const FPlanetEntry& Entry = PlanetRegistry[RegIdx];
		if (!Entry.CurrentState.IsValid())
		{
			UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[Subsystem] BuildFrameState: skipping invalid planet %s"), *Entry.PlanetId.ToString());
			continue;
		}

		FPlanetAtmosphereState State = Entry.CurrentState;
		State.LutSlotIndex = RegIdx;

		// Compute camera-relative center (double subtract, then narrow to km)
		const double DxKm = (State.CenterWS.X - ViewOriginWS.X) * HillaireLimits::KmPerCm;
		const double DyKm = (State.CenterWS.Y - ViewOriginWS.Y) * HillaireLimits::KmPerCm;
		const double DzKm = (State.CenterWS.Z - ViewOriginWS.Z) * HillaireLimits::KmPerCm;
		State.CenterCamRelativeKm = FVector3f((float)DxKm, (float)DyKm, (float)DzKm);

		// View height and distance
		State.ViewHeightKm = State.CenterCamRelativeKm.Size();
		State.DistanceKm = State.ViewHeightKm;
		State.bCameraInside = State.ViewHeightKm < State.AtmosphereTopRadiusKm;

		// Profile hash for LUT key
		State.ProfileHash = State.Profile.ComputeContentHash();

		UE_LOG(LogHillaireAtmosphere, Verbose, TEXT("[Subsystem] BuildFrameState: planet %s CenterRel=(%.3f,%.3f,%.3f) ViewH=%.3f Inside=%d"),
			*State.PlanetId.ToString(), State.CenterCamRelativeKm.X, State.CenterCamRelativeKm.Y, State.CenterCamRelativeKm.Z,
			State.ViewHeightKm, State.bCameraInside ? 1 : 0);

		ValidPlanets.Add(State);
	}

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] BuildFrameState: ValidPlanets=%d"), ValidPlanets.Num());

	// Selection inputs
	TArray<FPlanetSelectionInput> SelectionInputs;
	SelectionInputs.Reserve(ValidPlanets.Num());
	for (const FPlanetAtmosphereState& P : ValidPlanets)
	{
		FPlanetSelectionInput In;
		In.PlanetId = P.PlanetId;
		In.CenterCamRelativeKm = P.CenterCamRelativeKm;
		In.TopRadiusKm = P.AtmosphereTopRadiusKm;
		SelectionInputs.Add(In);
	}

	// Find incumbent governing index for hysteresis
	int32 IncumbentIndex = INDEX_NONE;
	if (CurrentFrameSnapshot.IsValid() && CurrentFrameSnapshot->GoverningPlanetIndex != INDEX_NONE)
	{
		const FGuid IncumbentId = CurrentFrameSnapshot->Planets[CurrentFrameSnapshot->GoverningPlanetIndex].PlanetId;
		for (int32 i = 0; i < ValidPlanets.Num(); ++i)
		{
			if (ValidPlanets[i].PlanetId == IncumbentId)
			{
				IncumbentIndex = i;
				break;
			}
		}
	}

	// Select governing + visible
	const FPlanetSelectionResult Selection = HillairePlanetMath::SelectPlanets(
		SelectionInputs, CameraRelKm, ViewDirectionWS, IncumbentIndex);

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] BuildFrameState: Selection GoverningIndex=%d ContainsCamera=%d VisibleCount=%d"),
		Selection.GoverningIndex, Selection.bGoverningContainsCamera ? 1 : 0, Selection.VisibleIndices.Num());

	// Build final planet array in draw order: visible far-to-near, governing last
	Frame.Planets.Reserve(Selection.VisibleIndices.Num() + 1);
	
	for (int32 VisIdx : Selection.VisibleIndices)
	{
		if (ValidPlanets.IsValidIndex(VisIdx))
		{
			FPlanetAtmosphereState P = ValidPlanets[VisIdx];
			
			// Compute screen rect
			const FMatrix ViewProj = ViewMatrix * ProjectionMatrix;
			P.ScreenRect = HillaireComputePlanetScreenRect(
				P.CenterCamRelativeKm, P.AtmosphereTopRadiusKm, ViewProj, ViewRect.Width(), ViewRect.Height());
			
			P.bIsGoverning = false;
			Frame.Planets.Add(P);
		}
	}

	if (Selection.GoverningIndex != INDEX_NONE && ValidPlanets.IsValidIndex(Selection.GoverningIndex))
	{
		FPlanetAtmosphereState P = ValidPlanets[Selection.GoverningIndex];
		P.bIsGoverning = true;
		P.bGoverningContainsCamera = Selection.bGoverningContainsCamera;
		Frame.GoverningPlanetIndex = Frame.Planets.Num();
		Frame.Planets.Add(P);
		
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] BuildFrameState: Governing planet = %s (ViewH=%.3f, Inside=%d)"),
			*P.PlanetId.ToString(), P.ViewHeightKm, P.bCameraInside ? 1 : 0);
	}
	else
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[Subsystem] BuildFrameState: NO GOVERNING PLANET SELECTED! ValidPlanets=%d"), ValidPlanets.Num());
	}

	// Visible indices in final array
	for (int32 i = 0; i < Frame.Planets.Num(); ++i)
	{
		if (!Frame.Planets[i].bIsGoverning)
		{
			Frame.VisiblePlanetIndices.Add(i);
		}
	}

	// Lights
	GetAllLightSources(Frame.Lights);

	// Compute world state hash
	uint64 Hash = HillaireHash::OffsetBasis;
	Hash = HillaireHash::HashVector(ViewOriginWS, HillaireLimits::CmPerKm * 1e-3, Hash);
	Hash = HillaireHash::HashFloat((float)Frame.GoverningPlanetIndex, 1.0f, Hash);
	for (const FPlanetAtmosphereState& P : Frame.Planets)
	{
		Hash = HillaireHash::HashBytes(&P.PlanetId, sizeof(P.PlanetId), Hash);
		Hash = HillaireHash::HashFloat(P.CenterCamRelativeKm.X, 1e-4f, Hash);
		Hash = HillaireHash::HashFloat(P.CenterCamRelativeKm.Y, 1e-4f, Hash);
		Hash = HillaireHash::HashFloat(P.CenterCamRelativeKm.Z, 1e-4f, Hash);
		Hash = HillaireHash::HashBytes(&P.ProfileHash, sizeof(P.ProfileHash), Hash);
		Hash = HillaireHash::HashFloat(P.ViewHeightKm, 1e-3f, Hash);
		Hash = HillaireHash::HashFloat(P.bIsGoverning ? 1.0f : 0.0f, 1.0f, Hash);
	}
	Frame.WorldStateHash = Hash;

	return Frame;
}

void UHillairePlanetaryAtmosphereSubsystem::BuildNextFrameSnapshot(
	const FVector& ViewOriginWS,
	const FMatrix& ViewMatrix,
	const FMatrix& ProjectionMatrix,
	const FIntRect& ViewRect,
	const FVector& ViewDirectionWS)
{
	const bool bCVar = IsEnabledByCVar();
	int32 PendingCount = 0;
	int32 CurrentValidCount = 0;
	for (const FPlanetEntry& Entry : PlanetRegistry)
	{
		if (Entry.bHasPendingState && Entry.PendingState.IsValid()) { PendingCount++; }
		if (Entry.CurrentState.IsValid()) { CurrentValidCount++; }
	}
	{
		const UWorld* W = GetWorld();
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] BuildNextFrameSnapshot ENTER Sub=%p World=%s GFrame=%llu CurFrameNum=%llu Registered=%d Pending=%d CurrentValid=%d NextValid=%d"),
			this, W ? *W->GetName() : TEXT("<null>"),
			(unsigned long long)GFrameCounter, (unsigned long long)CurrentFrameNumber,
			PlanetRegistry.Num(), PendingCount, CurrentValidCount, NextFrameSnapshot.IsValid() ? 1 : 0);
	}
	if (!bCVar || !bAtmosphereEnabled)
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("[Subsystem] BuildNextFrameSnapshot RETURN: Disabled CVar=%d Enabled=%d"), bCVar ? 1 : 0, bAtmosphereEnabled ? 1 : 0);
		return;
	}

	// Resolve lights FIRST, into PendingState, then apply to CurrentState.
	// Previous order (apply -> resolve -> build-from-Current) left the frame
	// one snapshot behind on lights (empty on frame 1): resolve wrote into
	// PendingState AFTER CurrentState had already been snapshotted.
	ResolveLightsForAllPlanets();

	// Apply pending states (now carrying fresh ResolvedLights) to CurrentState
	int32 AppliedCount = 0;
	for (FPlanetEntry& Entry : PlanetRegistry)
	{
		if (Entry.bHasPendingState)
		{
			Entry.CurrentState = Entry.PendingState;
			Entry.bHasPendingState = false;
			AppliedCount++;
		}
	}
	if (AppliedCount > 0)
	{
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] BuildNextFrameSnapshot: applied %d pending planet states"), AppliedCount);
	}

	// Build the frame state
	FHillaireAtmosphereFrameState FrameState = BuildFrameState(
		ViewOriginWS, ViewMatrix, ProjectionMatrix, ViewRect, ViewDirectionWS);

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] BuildNextFrameSnapshot: Frame=%llu Planets=%d Governing=%d Visible=%d"),
		FrameState.FrameNumber, FrameState.Planets.Num(), FrameState.GoverningPlanetIndex, FrameState.VisiblePlanetIndices.Num());

	// Store as next frame snapshot (immutable shared pointer), then publish
	// to Current so the SetupView stashing below in the same call reads the
	// just-built snapshot. SwapFrameSnapshots was previously dead code (no
	// callers), which left CurrentFrameSnapshot null forever.
	NextFrameSnapshot = MakeShared<const FHillaireAtmosphereFrameState>(MoveTemp(FrameState));
	SwapFrameSnapshots();

	// Transition diagnostics: governing identity/slot, profile hash, view
	// height, sun elevation, and LUT build count for THIS published frame,
	// plus change flags vs the previously published frame. Lets a moving
	// camera session attribute any visual snap to (a) governing change,
	// (b) LUT rebuild, or (c) neither (parameterization/sampling).
	{
		const FPlanetAtmosphereState* Gov = CurrentFrameSnapshot.IsValid()
			? CurrentFrameSnapshot->GetGoverningPlanet() : nullptr;
		const FGuid GovId = Gov ? Gov->PlanetId : FGuid();
		const int32 GovSlot = Gov ? Gov->LutSlotIndex : INDEX_NONE;
		const uint64 ProfHash = Gov ? Gov->ProfileHash : 0ULL;
		const float ViewH = Gov ? Gov->ViewHeightKm : -1.0f;
		float SunElev = -3.0f;
		uint32 LutBuilds = 0;
		float SkyCacheH = -1.0f;
		float SkyCacheElev = -3.0f;
		float SunAngDeltaDeg = -1.0f;
		if (Gov)
		{
			const FVector3f UpLocal = HillairePlanetMath::CameraUpLocal(Gov->CenterCamRelativeKm, Gov->RotationWS);
			SunElev = HillairePlanetMath::SunElevationCos(Gov->StarDirectionLocal, UpLocal);
			if (LutManager)
			{
				FHillairePlanetLutState LutCopy;
				if (LutManager->CopyLutState(GovSlot, LutCopy))
				{
					LutBuilds = LutCopy.LutBuildCount;
					SkyCacheH = LutCopy.SkyViewCachedHeightKm;
					SkyCacheElev = LutCopy.SkyViewCachedSunElevationCos;
					// Full-direction audit: angle between the CURRENT local sun
					// and the sun actually baked into the cached SkyView. The
					// regen key is elevation-only by design (bake content is
					// elevation-determined); this delta exposes azimuth drift
					// while the key holds. -1 = no baked direction recorded.
					if (LutCopy.bSkyViewSunDirInit)
					{
						const float CLenSq = Gov->StarDirectionLocal.SizeSquared();
						const float BLensq = LutCopy.SkyViewCachedSunDirLocal.SizeSquared();
						if (CLenSq > 1e-12f && BLensq > 1e-12f)
						{
							const float Dot = FMath::Clamp(
								(Gov->StarDirectionLocal.X * LutCopy.SkyViewCachedSunDirLocal.X
									+ Gov->StarDirectionLocal.Y * LutCopy.SkyViewCachedSunDirLocal.Y
									+ Gov->StarDirectionLocal.Z * LutCopy.SkyViewCachedSunDirLocal.Z)
								/ (FMath::Sqrt(CLenSq) * FMath::Sqrt(BLensq)),
								-1.0f, 1.0f);
							SunAngDeltaDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
						}
					}
				}
			}
		}
		const bool bGovChanged = (GovId != LastDiagGoverningPlanetId);
		const bool bLutChanged = (LutBuilds != LastDiagLutBuildCount) || bGovChanged;
		LastDiagGoverningPlanetId = GovId;
		LastDiagGoverningSlot = GovSlot;
		LastDiagLutBuildCount = LutBuilds;
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("[Subsystem] BuildNextFrameSnapshot SUCCESS Frame=%llu Planets=%d Governing=%d GovId=%s Slot=%d ProfHash=%llu ViewH=%.3f SunElev=%.5f SunAngDeltaDeg=%.3f LutBuilds=%u SkyCacheH=%.3f SkyCacheElev=%.5f GovChanged=%d LutChanged=%d PendingApplied=%d Published=true Sub=%p"),
			CurrentFrameSnapshot.IsValid() ? CurrentFrameSnapshot->FrameNumber : 0ULL,
			CurrentFrameSnapshot.IsValid() ? CurrentFrameSnapshot->Planets.Num() : 0,
			CurrentFrameSnapshot.IsValid() ? CurrentFrameSnapshot->GoverningPlanetIndex : -1,
			*GovId.ToString(), GovSlot, ProfHash, ViewH, SunElev, SunAngDeltaDeg,
			LutBuilds, SkyCacheH, SkyCacheElev,
			bGovChanged ? 1 : 0, bLutChanged ? 1 : 0,
			AppliedCount,
			this);
	}
}

TSharedPtr<const FHillaireAtmosphereFrameState> UHillairePlanetaryAtmosphereSubsystem::GetCurrentFrameSnapshot() const
{
	return CurrentFrameSnapshot;
}

TSharedPtr<const FHillaireAtmosphereFrameState> UHillairePlanetaryAtmosphereSubsystem::GetSnapshotForView(const FSceneViewStateInterface* ViewState) const
{
	if (!ViewState) return nullptr;

	FScopeLock Lock(&StashLock);
	if (const FFrameSnapshotEntry* Entry = SnapshotStash.Find(ViewState))
	{
		return Entry->Snapshot;
	}
	return nullptr;
}

void UHillairePlanetaryAtmosphereSubsystem::SwapFrameSnapshots()
{
	CurrentFrameSnapshot = NextFrameSnapshot;
	NextFrameSnapshot.Reset();
	CurrentFrameNumber++;
	PruneStaleSnapshots(GFrameCounter);
}

void UHillairePlanetaryAtmosphereSubsystem::PruneStaleSnapshots(uint64 CurrentFrame)
{
	FScopeLock Lock(&StashLock);
	static constexpr uint64 KeepFrames = 30;
	TArray<const FSceneViewStateInterface*> Stale;
	for (const auto& Pair : SnapshotStash)
	{
		if (Pair.Value.FrameNumber + KeepFrames < CurrentFrame)
		{
			Stale.Add(Pair.Key);
		}
	}
	for (const FSceneViewStateInterface* Key : Stale)
	{
		SnapshotStash.Remove(Key);
	}
}

void UHillairePlanetaryAtmosphereSubsystem::InvalidatePlanetLuts(const FGuid& PlanetId)
{
	const int32 Idx = FindPlanetIndex(PlanetId);
	if (Idx != INDEX_NONE && LutManager)
	{
		LutManager->InvalidatePlanetById(Idx);
		PlanetRegistry[Idx].CurrentState.LutGenerationVersion++;
	}
}

void UHillairePlanetaryAtmosphereSubsystem::DumpFrameState() const
{
	if (!CurrentFrameSnapshot.IsValid()) return;

	UE_LOG(LogHillaireAtmosphere, Log, TEXT("=== HILLAIRE FRAME STATE (Frame %llu) ==="), CurrentFrameSnapshot->FrameNumber);
	UE_LOG(LogHillaireAtmosphere, Log, TEXT("ViewOrigin: %s"), *CurrentFrameSnapshot->ViewOriginWS.ToString());
	UE_LOG(LogHillaireAtmosphere, Log, TEXT("Planets: %d, Governing: %d, Visible: %d"),
		CurrentFrameSnapshot->Planets.Num(),
		CurrentFrameSnapshot->GoverningPlanetIndex,
		CurrentFrameSnapshot->VisiblePlanetIndices.Num());
	UE_LOG(LogHillaireAtmosphere, Log, TEXT("Lights: %d, MSFactor: %.3f, FastSky: %d"),
		CurrentFrameSnapshot->Lights.Num(),
		CurrentFrameSnapshot->MultipleScatteringFactor,
		CurrentFrameSnapshot->bFastSkyEnabled ? 1 : 0);
	UE_LOG(LogHillaireAtmosphere, Log, TEXT("WorldStateHash: %llu"), CurrentFrameSnapshot->WorldStateHash);

	for (int32 i = 0; i < CurrentFrameSnapshot->Planets.Num(); ++i)
	{
		const FPlanetAtmosphereState& P = CurrentFrameSnapshot->Planets[i];
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("  [%d] %s (Id: %s) CenterRel: %s Ground: %.3f Top: %.3f ViewH: %.3f Inside: %d Gov: %d"),
			i, *P.PlanetName.ToString(), *P.PlanetId.ToString(),
			*P.CenterCamRelativeKm.ToString(),
			P.GroundRadiusKm, P.AtmosphereTopRadiusKm, P.ViewHeightKm,
			P.bCameraInside ? 1 : 0, P.bIsGoverning ? 1 : 0);
	}
}

int32 UHillairePlanetaryAtmosphereSubsystem::GetRegisteredPlanetCount() const
{
	// Count planets that have been registered (have pending or current state),
	// not just those with valid CurrentState. This avoids a chicken-and-egg
	// deadlock where ShouldHandleView checks planet count before the first
	// frame snapshot applies pending states to CurrentState.
	int32 Count = 0;
	for (const FPlanetEntry& Entry : PlanetRegistry)
	{
		if (Entry.bHasPendingState || Entry.CurrentState.IsValid())
		{
			Count++;
		}
	}
	return Count;
}

bool UHillairePlanetaryAtmosphereSubsystem::HasPlanet(const FGuid& PlanetId) const
{
	return FindPlanetIndex(PlanetId) != INDEX_NONE;
}