#include "Zephyr/ZephyrManager.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillairePlanetState.h"
#include "HillairePlanetAtmosphereState.h"
#include "HillaireLimits.h"
#include "Modules/ModuleManager.h"
#include "Planet/Planet.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Sun.h"
#include "Zephyr/ZephyrLog.h"
#include "Zephyr/ZephyrPresentation.h"

// ZEPHYR presentation owns how the star LOOKS. ATMOS renders the physical sun
// disk (transmittance-attenuated, correct angular size); the decorative star
// MESH is a large opaque sphere from the clean-slate era and would occlude the
// sky. Hide it by default so the ATMOS/ZEPHYR sky presentation is what shows.
static TAutoConsoleVariable<int32> CVarZephyrHideStarMesh(
	TEXT("r.Zephyr.HideStarMesh"),
	1,
	TEXT("Hide the decorative star mesh so the ATMOS-rendered sun disk is the star (0=show mesh, 1=hide)."),
	ECVF_RenderThreadSafe);

bool UZephyrManager::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

void UZephyrManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ProfileLibrary = TStrongObjectPtr<UZephyrProfileLibrary>(
		NewObject<UZephyrProfileLibrary>(GetTransientPackage(), UZephyrProfileLibrary::StaticClass()));

	PlanetStates.Empty();
	GoverningData = FZephyrPlanetGpuData();
	bHasGoverningData = false;
	bCameraInAnyAtmosphere = false;
	TotalUpdateTime = 0.0f;
	FrameNumber = 0;
	CurrentFrameData.Reset();
	NextFrameData.Reset();

	// ZEPHYR PRESENTATION: the single, isolated AfterDOF overlay. It runs after
	// the authoritative ATMOS composite and only touches sky pixels.
	ViewExtension = FSceneViewExtensions::NewExtension<FZephyrPresentationViewExtension>(this);

	UE_LOG(LogZephyr, Log, TEXT("[Zephyr] Manager initialized (world %s)."), *GetWorld()->GetName());
}

void UZephyrManager::Deinitialize()
{
	{
		FScopeLock Lock(&FrameLock);
		CurrentFrameData.Reset();
		NextFrameData.Reset();
	}
	PlanetStates.Empty();
	GoverningData = FZephyrPlanetGpuData();
	bHasGoverningData = false;
	AtmosSubsystem = nullptr;

	ViewExtension.Reset();

	ProfileLibrary.Reset();
	Super::Deinitialize();
}

UZephyrProfileLibrary* UZephyrManager::GetProfileLibrary()
{
	static UZephyrProfileLibrary* StaticLibrary = nullptr;
	if (!StaticLibrary)
	{
		StaticLibrary = NewObject<UZephyrProfileLibrary>(GetTransientPackage(), UZephyrProfileLibrary::StaticClass());
	}
	return StaticLibrary;
}

void UZephyrManager::RegisterPlanet(
	const FGuid& PlanetId,
	const FName& PlanetName,
	int64 PlanetSeed,
	int64 PlanetID,
	int32 Archetype,
	float OrbitDistanceCm)
{
	if (!PlanetId.IsValid())
	{
		return;
	}

	FPlanetZephyrState& State = PlanetStates.FindOrAdd(PlanetId);

	const bool bNewPlanet = !State.bInitialized;
	const bool bIdentityChanged = bNewPlanet
		|| State.Profile.PlanetSeed != PlanetSeed
		|| State.Profile.PlanetNumber != PlanetID
		|| (int32)State.Profile.Archetype != Archetype
		|| !FMath::IsNearlyEqual(State.Profile.OrbitDistanceCm, OrbitDistanceCm);

	if (bIdentityChanged)
	{
		BuildProfileForPlanet(State, PlanetSeed, PlanetID, Archetype, OrbitDistanceCm);
	}

	State.PlanetId = PlanetId;
	State.PlanetName = PlanetName;
	State.bInitialized = true;

	RefreshLinkedActor(State);
}

void UZephyrManager::UnregisterPlanet(const FGuid& PlanetId)
{
	PlanetStates.Remove(PlanetId);
	if (bHasGoverningData && GoverningData.PlanetId == PlanetId)
	{
		GoverningData = FZephyrPlanetGpuData();
		bHasGoverningData = false;
	}
}

void UZephyrManager::BuildProfileForPlanet(FPlanetZephyrState& State, int64 PlanetSeed, int64 PlanetID, int32 Archetype, float OrbitDistanceCm)
{
	// Geometric radii are filled from the ATMOS snapshot each frame
	// (UpdatePlanetGpuData). The profile is otherwise deterministic; 100 km
	// placeholder thickness only shapes cloud template altitudes locally and
	// is overwritten by the authoritative snapshot values later.
	State.Profile = UZephyrProfileLibrary::BuildProfile(
		PlanetSeed,
		PlanetID,
		(EPlanetArchetype)Archetype,
		OrbitDistanceCm,
		6371.0f,
		6371.0f * HillaireLimits::PlanetaryAtmosphereRadiusRatio
	);
	State.Profile.PlanetId = State.PlanetId;
	State.CurrentWeather = State.Profile.BaseWeatherState;
	State.LastWeatherUpdateTime = 0.0f;
}

void UZephyrManager::UpdateFromAtmosphere(float DeltaTime)
{
	// ZEPHYR star presentation (independent of ATMOS data availability).
	if (CVarZephyrHideStarMesh.GetValueOnGameThread() != 0)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<ASun> It(World); It; ++It)
			{
				if (ASun* Sun = *It)
				{
					if (Sun->SunMesh && Sun->SunMesh->IsVisible())
					{
						Sun->SunMesh->SetVisibility(false, true);
					}
				}
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (!AtmosSubsystem)
		{
			AtmosSubsystem = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>();
		}
	}

	if (!AtmosSubsystem)
	{
		bHasGoverningData = false;
		return;
	}

	const TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot = AtmosSubsystem->GetCurrentFrameSnapshot();
	if (!Snapshot.IsValid() || !Snapshot->HasAtmosphereContent())
	{
		bHasGoverningData = false;
		return;
	}

	// Auto-discover planets from the authoritative snapshot + live APlanet
	// actors (the ATMOS frame holds the stable FGuid; the world holds the seed).
	SyncPlanetsFromAtmosphere(*Snapshot);

	// Push authoritative geometry + sun into every registered planet.
	for (const FPlanetAtmosphereState& AtmosState : Snapshot->Planets)
	{
		FPlanetZephyrState* State = PlanetStates.Find(AtmosState.PlanetId);
		if (!State || !State->bInitialized)
		{
			continue;
		}
		// Mirror the authoritative ATMOS geometry (Bottom = planetary reference
		// radius, Top = reference + profile-derived envelope). ATMOS owns these
		// values; terrain is never folded in.
		State->Profile.GroundRadiusKm = AtmosState.GroundRadiusKm;
		State->Profile.AtmosphereTopRadiusKm = AtmosState.AtmosphereTopRadiusKm;
		UpdatePlanetGpuData(*State, AtmosState);
	}

	// THE ZEPHYR transition: high-pass smoothstep over the real shell, then
	// weather.
	UpdateTransitionFactors(*Snapshot);
	UpdateWeatherSimulation(DeltaTime);
}

void UZephyrManager::SyncPlanetsFromAtmosphere(const FHillaireAtmosphereFrameState& Snapshot)
{
	// Map stable FGuid -> APlanet identity by scanning the world once.
	TMap<FGuid, APlanet*> PlanetsById;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APlanet> It(World); It; ++It)
		{
			APlanet* Planet = *It;
			if (!Planet)
			{
				continue;
			}
			const FGuid StableId = HillaireMakeStablePlanetId(Planet->PlanetID, Planet->PlanetSeed);
			if (StableId.IsValid())
			{
				PlanetsById.Add(StableId, Planet);
			}
		}
	}

	for (const FPlanetAtmosphereState& AtmosState : Snapshot.Planets)
	{
		const FGuid& Id = AtmosState.PlanetId;
		if (!Id.IsValid())
		{
			continue;
		}
		if (PlanetStates.Contains(Id))
		{
			continue;
		}

		if (APlanet** FoundPlanet = PlanetsById.Find(Id))
		{
			APlanet* Planet = *FoundPlanet;
			RegisterPlanet(
				Id,
				FName(*Planet->GetName()),
				Planet->PlanetSeed,
				Planet->PlanetID,
				(int32)Planet->PlanetArchetype,
				Planet->OrbitDistance);
		}
		else
		{
			// Registered externally (no live APlanet actor): derive a stable
			// seed from the GUID so the profile is at least deterministic.
			const int64 FallbackSeed = (int64)((uint64)Id.A | ((uint64)Id.B << 32));
			RegisterPlanet(Id, AtmosState.PlanetName, FallbackSeed, 0, (int32)EPlanetArchetype::Terran, 0.0f);
		}
	}
}

void UZephyrManager::RefreshLinkedActor(FPlanetZephyrState& State)
{
	State.LinkedActor = nullptr;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APlanet> It(World); It; ++It)
		{
			if (HillaireMakeStablePlanetId((*It)->PlanetID, (*It)->PlanetSeed) == State.PlanetId)
			{
				State.LinkedActor = *It;
				return;
			}
		}
	}
}

void UZephyrManager::PublishFrameForView(const FSceneView& InView)
{
	// Build an immutable GPU-ready snapshot for the RenderThread overlay.
	// Always publish (with factor 0 when no governing planet exists) so the
	// RenderThread stash can never keep a stale "in atmosphere" frame after
	// the camera jumps back to deep space.
	TSharedPtr<FZephyrPresentationFrame> Frame = MakeShared<FZephyrPresentationFrame>();

	if (!bHasGoverningData)
	{
		Frame->ViewRect = InView.UnscaledViewRect;
		Frame->FrameNumber = ++FrameNumber;
		{
			FScopeLock Lock(&FrameLock);
			CurrentFrameData = Frame;
			NextFrameData.Reset();
		}
		return;
	}

	Frame->FrameNumber = ++FrameNumber;
	Frame->GoverningPlanetId = GoverningData.PlanetId;
	Frame->PlanetRadiusKm = GoverningData.PlanetRadiusKm;
	Frame->TerrainHeightKm = GoverningData.TerrainHeightKm;
	Frame->PlanetAtmosphereRangeKm = GoverningData.PlanetAtmosphereRangeKm;
	Frame->AtmosphereTopRadiusKm = GoverningData.AtmosphereTopRadiusKm;
	Frame->ZephyrTransitionFactor = GoverningData.ZephyrTransitionFactor;

	// RT re-anchor inputs (world-space center + rotation).
	Frame->CenterWS = GoverningCenterWS;
	Frame->RotationWS = GoverningRotationWS;

	// Sky appearance.
	const FZephyrSkyAppearance& Sky = GoverningData.SkyAppearance;
	Frame->ZenithColor = FVector3f(Sky.ZenithColor.R, Sky.ZenithColor.G, Sky.ZenithColor.B);
	Frame->HorizonColor = FVector3f(Sky.HorizonColor.R, Sky.HorizonColor.G, Sky.HorizonColor.B);
	Frame->SunGlowColor = FVector3f(Sky.SunGlowColor.R, Sky.SunGlowColor.G, Sky.SunGlowColor.B);
	Frame->MieColor = FVector3f(Sky.MieColor.R, Sky.MieColor.G, Sky.MieColor.B);
	Frame->RayleighScale = Sky.RayleighScale;
	Frame->MieScale = Sky.MieScale;

	// Weather.
	const FZephyrWeatherState& W = GoverningData.WeatherState;
	Frame->WeatherHazeFactor = FMath::Clamp(1.0f - W.VisibilityKm / 50.0f, 0.0f, 1.0f);
	const FLinearColor HazeTint = FLinearColor::LerpUsingHSV(
		Sky.HorizonColor,
		FLinearColor(0.6f, 0.62f, 0.66f, 1.0f),
		FMath::Clamp(W.PrecipitationIntensity * 0.7f + (1.0f - W.VisibilityKm / 50.0f) * 0.3f, 0.0f, 1.0f));
	Frame->WeatherHazeColor = FVector3f(HazeTint.R, HazeTint.G, HazeTint.B);

	// Cloud layers (current weather, packed max = ZephyrLimits::MaxCloudLayers).
	Frame->CloudLayers.Reset();
	for (const FZephyrCloudLayer& L : W.CloudLayers)
	{
		if (Frame->CloudLayers.Num() >= ZephyrLimits::MaxCloudLayers)
		{
			break;
		}
		FZephyrCloudLayerGpu Packed;
		Packed.AltitudeConfigure = FVector4f(L.BaseAltitudeKm, L.TopAltitudeKm, L.Coverage, L.Density);
		Packed.ColorDetail = FVector4f(L.CloudColor.R, L.CloudColor.G, L.CloudColor.B, L.DetailScale);
		Packed.WindWater = FVector4f(L.WindSpeed, FMath::DegreesToRadians(L.WindDirection), L.WaterContent, L.bEnabled ? 1.0f : 0.0f);
		Frame->CloudLayers.Add(Packed);
	}

	// Sun (planet-local).
	Frame->StarDirectionLocal = GoverningData.StarDirectionLocal;
	Frame->StarIrradiance = GoverningData.StarIrradiance;

	// CPU daylight factor from the REAL sun elevation at the camera.
	// ATMOS convention (HillaireCameraUpLocal): up_local =
	// normalize(R^-1 * (viewOrigin - center)). The previous (center -
	// viewOrigin) gave the DOWN vector, which inverted the curve (full
	// cloud/haze at night, nothing at noon).
	{
		const FVector RelCm = InView.ViewMatrices.GetViewOrigin() - GoverningCenterWS;
		const FVector RelKm = RelCm * HillaireLimits::KmPerCm;
		const FVector3f CamLocal = FVector3f(GoverningRotationWS.Inverse().RotateVector(RelKm));
		const FVector3f UpLocal = CamLocal.GetSafeNormal();
		const FVector3f SunDir = GoverningData.StarDirectionLocal.GetSafeNormal();
		const float SunElevCos = FMath::Clamp(FVector3f::DotProduct(SunDir, UpLocal), -1.0f, 1.0f);
		Frame->DaylightFactor = ZephyrPresentation::DaylightFactor(SunElevCos);
	}

	Frame->ViewRect = InView.UnscaledViewRect;
	Frame->TimeSeconds = TotalUpdateTime;

	{
		FScopeLock Lock(&FrameLock);
		CurrentFrameData = Frame;
		NextFrameData.Reset();
	}
}

TSharedPtr<const FZephyrPresentationFrame> UZephyrManager::GetCurrentFrameData() const
{
	FScopeLock Lock(&FrameLock);
	return CurrentFrameData;
}

void UZephyrManager::UpdateTransitionFactors(const FHillaireAtmosphereFrameState& Snapshot)
{
	GoverningData = FZephyrPlanetGpuData();
	bHasGoverningData = false;
	bCameraInAnyAtmosphere = false;

	// The governing planet is chosen AUTHORITATIVELY by ATMOS. ZEPHYR follows
	// the same governing selection so the overlay always matches the physics.
	for (const FPlanetAtmosphereState& P : Snapshot.Planets)
	{
		FPlanetZephyrState* State = PlanetStates.Find(P.PlanetId);
		if (!State || !State->bInitialized)
		{
			continue;
		}

		const float ViewRadiusKm = P.ViewHeightKm; // camera distance to planet center
		// SHARED VOLUME MODEL: ZEPHYR consumes the SAME planetary reference
		// radius ATMOS uses as its atmospheric bottom (base sphere, sea level).
		// The transition spans the atmosphere volume [reference, top] and never
		// re-anchors to terrain: LYTHOS terrain protrudes into the volume and a
		// camera on a peak is simply higher inside the same atmosphere.
		const float RangeKm = State->Profile.GroundRadiusKm;
		const float TopRadiusKm = State->Profile.AtmosphereTopRadiusKm;

		// THE single ZEPHYR transition: high-pass smoothstep over the REAL
		// volume [planetary reference radius, AtmosphereTopRadius].
		const float Factor = ZephyrComputeTransitionFactor(ViewRadiusKm, RangeKm, TopRadiusKm);

		State->GpuData.ZephyrTransitionFactor = Factor;
		State->GpuData.ViewRadiusKm = ViewRadiusKm;
		State->GpuData.bCameraInside = P.bCameraInside;
	}

	// Capture the authoritative governing selection (same as ATMOS).
	const FPlanetAtmosphereState* Gov = Snapshot.GetGoverningPlanet();
	if (!Gov)
	{
		return;
	}

	FPlanetZephyrState* GovState = PlanetStates.Find(Gov->PlanetId);
	if (!GovState || !GovState->bInitialized)
	{
		return;
	}

	GoverningData = GovState->GpuData;
	bHasGoverningData = true;
	bCameraInAnyAtmosphere = Gov->bCameraInside;

	// RT re-anchor inputs (mirror the ATMOS composite re-anchor).
	GoverningCenterWS = Gov->CenterWS;
	GoverningRotationWS = Gov->RotationWS;
}

void UZephyrManager::UpdatePlanetGpuData(FPlanetZephyrState& State, const FPlanetAtmosphereState& AtmosState)
{
	FZephyrPlanetGpuData& G = State.GpuData;
	G.PlanetId = State.PlanetId;

	// Canonical geometry (ATMOS snapshot owns the authoritative values):
	//   PlanetAtmosphereRange = GroundRadius = planetary reference (sea level)
	//   AtmosphereTop         = reference + profile/containment envelope
	// TerrainHeightKm stays metadata (terrain protrudes into the volume; it is
	// never folded into the bottom or into the transition reference).
	G.TerrainHeightKm = AtmosState.TerrainHeightKm;
	G.PlanetAtmosphereRangeKm = AtmosState.GroundRadiusKm;
	G.PlanetRadiusKm = G.PlanetAtmosphereRangeKm;
	G.AtmosphereTopRadiusKm = AtmosState.AtmosphereTopRadiusKm;
	G.AtmosphereMultiplier = (G.PlanetAtmosphereRangeKm > 0.0f)
		? (G.AtmosphereTopRadiusKm / G.PlanetAtmosphereRangeKm)
		: 0.0f;
	G.ViewRadiusKm = AtmosState.ViewHeightKm;

	G.SkyAppearance = State.Profile.SkyAppearance;
	G.WeatherState = State.CurrentWeather;
	G.RotationWS = AtmosState.RotationWS;
	G.StarDirectionLocal = AtmosState.StarDirectionLocal;
	G.StarIrradiance = AtmosState.StarIrradiance;
	G.bCameraInside = AtmosState.bCameraInside;
	G.ProfileHash = State.Profile.ComputeContentHash();
}

void UZephyrManager::UpdateWeatherSimulation(float DeltaTime)
{
	TotalUpdateTime += DeltaTime;
	for (auto& Pair : PlanetStates)
	{
		FPlanetZephyrState& State = Pair.Value;
		if (!State.bInitialized)
		{
			continue;
		}

		State.LastWeatherUpdateTime += DeltaTime;
		const float Period = 24.0f * 3600.0f / FMath::Max(0.1f, State.Profile.WeatherVariationSpeed);
		const float Phase = FMath::Fmod(State.LastWeatherUpdateTime / Period, 1.0f);

		FZephyrWeatherState Target = State.Profile.BaseWeatherState;
		for (FZephyrCloudLayer& Layer : Target.CloudLayers)
		{
			if (Layer.bEnabled)
			{
				const float Variation = FMath::Sin(Phase * 2.0f * PI + (float)(State.PlanetId.A ^ State.PlanetId.B)) * 0.15f;
				Layer.Coverage = FMath::Clamp(Layer.Coverage + Variation, 0.0f, 1.0f);
			}
		}

		// Very slow blend toward the (varying) target weather.
		State.CurrentWeather = InterpolateWeather(State.CurrentWeather, Target, 0.0001f * DeltaTime);
	}
}

FZephyrWeatherState UZephyrManager::InterpolateWeather(const FZephyrWeatherState& A, const FZephyrWeatherState& B, float T)
{
	FZephyrWeatherState Result = A;
	T = FMath::Clamp(T, 0.0f, 1.0f);

	Result.PrecipitationIntensity = FMath::Lerp(A.PrecipitationIntensity, B.PrecipitationIntensity, T);
	Result.SurfaceWindSpeed = FMath::Lerp(A.SurfaceWindSpeed, B.SurfaceWindSpeed, T);
	Result.SurfaceWindDirection = FMath::Lerp(A.SurfaceWindDirection, B.SurfaceWindDirection, T);
	Result.VisibilityKm = FMath::Lerp(A.VisibilityKm, B.VisibilityKm, T);
	Result.PressureHpa = FMath::Lerp(A.PressureHpa, B.PressureHpa, T);
	Result.TemperatureC = FMath::Lerp(A.TemperatureC, B.TemperatureC, T);
	Result.Humidity = FMath::Lerp(A.Humidity, B.Humidity, T);

	const int32 MaxLayers = FMath::Max(A.CloudLayers.Num(), B.CloudLayers.Num());
	Result.CloudLayers.SetNum(MaxLayers);
	for (int32 i = 0; i < MaxLayers; ++i)
	{
		const FZephyrCloudLayer* LA = A.CloudLayers.IsValidIndex(i) ? &A.CloudLayers[i] : nullptr;
		const FZephyrCloudLayer* LB = B.CloudLayers.IsValidIndex(i) ? &B.CloudLayers[i] : nullptr;
		if (LA && LB)
		{
			Result.CloudLayers[i] = *LA;
			FZephyrCloudLayer& R = Result.CloudLayers[i];
			R.Coverage = FMath::Lerp(LA->Coverage, LB->Coverage, T);
			R.Density = FMath::Lerp(LA->Density, LB->Density, T);
			R.CloudColor = FMath::Lerp(LA->CloudColor, LB->CloudColor, T);
		}
		else if (LB)
		{
			Result.CloudLayers[i] = *LB;
		}
	}

	if (T > 0.5f && B.WeatherType != A.WeatherType)
	{
		Result.WeatherType = B.WeatherType;
		Result.PrecipitationType = B.PrecipitationType;
	}

	return Result;
}

bool UZephyrManager::IsCameraInAnyAtmosphere() const
{
	return bCameraInAnyAtmosphere;
}

bool UZephyrManager::GetPlanetProfile(const FGuid& PlanetId, FZephyrPlanetProfile& OutProfile) const
{
	if (const FPlanetZephyrState* State = PlanetStates.Find(PlanetId))
	{
		if (State->bInitialized)
		{
			OutProfile = State->Profile;
			return true;
		}
	}
	return false;
}
