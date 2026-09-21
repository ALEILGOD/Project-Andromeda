#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Zephyr/ZephyrTypes.h"
#include "Zephyr/ZephyrProfile.h"
#include "Zephyr/ZephyrRenderData.h"
#include "HillairePlanetAtmosphereState.h"
#include "ZephyrManager.generated.h"

class UWorld;
class UHillairePlanetaryAtmosphereSubsystem;
class APlanet;
class FZephyrPresentationViewExtension;
class FSceneView;

/**
 * ZEPHYR Manager - GameThread subsystem for planetary climate/presentation.
 *
 * Architecture (ATMOS owns physics, ZEPHYR owns presentation):
 *   STARMAP (APlanet identity)
 *     -> ZEPHYR CORE: deterministic per-planet climate/aesthetic profile
 *     -> ATMOS: physical scattering (untouched)
 *     -> ZEPHYR PRESENTATION: bounded, depth-gated cloud/haze overlay on sky
 *
 * Responsibilities:
 * - Maintain per-planet ZEPHYR profiles (climate, weather, sky appearance).
 * - Consume the authoritative ATMOS planetary snapshot (geometry + lighting).
 * - Compute THE ZEPHYR transition factor from the camera distance to the
 *   governing planet center vs the real shell
 *   [PlanetAtmosphereRange, AtmosphereTopRadius].
 * - Update the (slow) weather simulation per planet.
 * - Publish an immutable per-frame presentation snapshot for the RenderThread.
 *
 * Does NOT:
 * - Implement physical scattering (ATMOS owns it).
 * - Modify ATMOS LUTs / profile / intensity / sky.
 * - Touch exposure or bloom.
 * - Modify LYTHOS terrain / materials / LODs / Nanite.
 */
UCLASS()
class ANDROMEDA_API UZephyrManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * Per-frame GameThread update. Reads the authoritative ATMOS snapshot,
	 * discovers planets (APlanet in the world, stable FGuid), recomputes the
	 * ZEPHYR transition factor of the governing planet and publishes a new
	 * immutable frame snapshot consumed by the RenderThread.
	 */
	void UpdateFromAtmosphere(float DeltaTime);

	/** Publish a fresh frame snapshot (view rect + RT re-anchor inputs). */
	void PublishFrameForView(const FSceneView& InView);

	/** Register/refresh a planet identity (explicit STARMAP feed, optional). */
	void RegisterPlanet(
		const FGuid& PlanetId,
		const FName& PlanetName,
		int64 PlanetSeed,
		int64 PlanetID,
		int32 Archetype,
		float OrbitDistanceCm);

	/** Unregister a planet. */
	void UnregisterPlanet(const FGuid& PlanetId);

	/** Get the current immutable frame snapshot (GT or RT read-safe). */
	TSharedPtr<const FZephyrPresentationFrame> GetCurrentFrameData() const;

	/** Access to the ZEPHYR profile generation library (stateless, static). */
	static UZephyrProfileLibrary* GetProfileLibrary();

	/** Whether the camera is inside any registered atmosphere. */
	bool IsCameraInAnyAtmosphere() const;

	/** Get the ZEPHYR profile for a planet (presentation identity). */
	bool GetPlanetProfile(const FGuid& PlanetId, FZephyrPlanetProfile& OutProfile) const;

private:
	struct FPlanetZephyrState
	{
		FGuid PlanetId;
		FName PlanetName;
		FZephyrPlanetProfile Profile;
		FZephyrWeatherState CurrentWeather;
		FZephyrPlanetGpuData GpuData;
		TObjectPtr<APlanet> LinkedActor;
		float LastWeatherUpdateTime = 0.0f;
		bool bInitialized = false;
	};

	void BuildProfileForPlanet(FPlanetZephyrState& State, int64 PlanetSeed, int64 PlanetID, int32 Archetype, float OrbitDistanceCm);
	void RefreshLinkedActor(FPlanetZephyrState& State);
	void SyncPlanetsFromAtmosphere(const FHillaireAtmosphereFrameState& Snapshot);
	void UpdateTransitionFactors(const FHillaireAtmosphereFrameState& Snapshot);
	void UpdatePlanetGpuData(FPlanetZephyrState& State, const FPlanetAtmosphereState& AtmosState);
	void UpdateWeatherSimulation(float DeltaTime);
	FZephyrWeatherState InterpolateWeather(const FZephyrWeatherState& A, const FZephyrWeatherState& B, float T);

	TObjectPtr<UHillairePlanetaryAtmosphereSubsystem> AtmosSubsystem = nullptr;
	TSharedPtr<FZephyrPresentationViewExtension, ESPMode::ThreadSafe> ViewExtension;
	TMap<FGuid, FPlanetZephyrState> PlanetStates;
	FZephyrPlanetGpuData GoverningData;
	bool bHasGoverningData = false;
	bool bCameraInAnyAtmosphere = false;
	float TotalUpdateTime = 0.0f;
	uint64 FrameNumber = 0;
	TStrongObjectPtr<UZephyrProfileLibrary> ProfileLibrary;

	// RT re-anchor inputs captured from the governing ATMOS state at update time.
	FVector GoverningCenterWS = FVector::ZeroVector;
	FQuat GoverningRotationWS = FQuat::Identity;

	// Double-buffered frame data: GT writes Next, publishes to Current once
	// per frame. RT reads via GetCurrentFrameData.
	mutable FCriticalSection FrameLock;
	TSharedPtr<const FZephyrPresentationFrame> CurrentFrameData;
	TSharedPtr<FZephyrPresentationFrame> NextFrameData;
};
