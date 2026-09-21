#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HillairePlanetAtmosphereState.h"
#include "HillaireLutManager.h"
#include "HillairePlanetaryViewExtension.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLightSource.h"
#include "HillairePlanetaryAtmosphereSubsystem.generated.h"

class UHillaireAtmosphereComponent;
class UHillaireAtmosphereLightComponent;

/**
 * HILLAIRE PLANETARY ATMOSPHERE SUBSYSTEM (Multiplanetary rebuild).
 *
 * Central authority for ALL planetary atmospheres in a world.
 * - Registers every planet with stable STARMAP-derived PlanetId (FGuid)
 * - Maintains independent atmosphere state per planet
 * - Updates positions/rotations/radii from STARMAP each frame
 * - Associates correct star per planet
 * - Manages per-planet LUT cache keyed by PlanetId + ProfileHash
 * - Builds double-buffered frame snapshots (GT writes, RT reads)
 * - NO global singleton state representing a single planet
 * - NO FSceneView* or transient pointers in persistent state
 */
UCLASS()
class HILLAIREATMOSPHERE_API UHillairePlanetaryAtmosphereSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual ~UHillairePlanetaryAtmosphereSubsystem();

	// ---- Planet Registry (M planets, identified by stable FGuid) ----

	/**
	 * Register a planet component. Returns the assigned PlanetId.
	 * Components use this for manual/test paths; STARMAP uses RegisterExternalPlanet.
	 */
	int32 RegisterPlanetComponent(UHillaireAtmosphereComponent* Component);

	void UnregisterPlanetComponent(UHillaireAtmosphereComponent* Component);

	/**
	 * Register a STARMAP planet. Called once per planet at spawn.
	 * Returns the PlanetId (stable FGuid from APlanet.PlanetID/PlanetSeed).
	 */
	FGuid RegisterExternalPlanet(const FGuid& PlanetId, const FName& PlanetName);

	void UnregisterExternalPlanet(const FGuid& PlanetId);

	/**
	 * Update a STARMAP planet's live state (called every tick by PlanetLink).
	 * Pushes fresh geometry, rotation, profile. Preserves LUT cache across updates.
	 */
	void UpdateExternalPlanet(const FPlanetAtmosphereState& State);

	/** Get current planet count. */
	int32 GetRegisteredPlanetCount() const;

	/** Check if a planet is registered. */
	bool HasPlanet(const FGuid& PlanetId) const;

	/** Get all current planet states (GameThread read). */
	void GetAllPlanetStates(TArray<FPlanetAtmosphereState>& OutStates) const;

	// ---- Star/Light Registry (N sources) ----

	/** Register a star component (manual/test path). */
	void RegisterStarComponent(UHillaireAtmosphereLightComponent* Component);

	void UnregisterStarComponent(UHillaireAtmosphereLightComponent* Component);

	/**
	 * Register/update a STARMAP star for a specific planet.
	 * Each planet knows its star via StarId in FPlanetAtmosphereState.
	 */
	void RegisterExternalStar(const FGuid& StarId, const FHillaireLightSource& Light);

	void UnregisterExternalStar(const FGuid& StarId);

	/**
	 * Per-planet sun override (MULTIPLANETARY CORRECTION).
	 *
	 * The shared star registry holds ONE directional vector per star, but every
	 * planet sees the star along its OWN Planet->Star direction
	 * (StarWorldPos - PlanetWorldPos, normalized). A single global vector is
	 * geometrically wrong for all but one planet: it misplaces the bright
	 * limb / day-night terminator on every other planet.
	 *
	 * The StarLink therefore pushes each planet's own direction (+ effective
	 * intensity, which is also per-planet: illumination x raw intensity) here,
	 * keyed by the planet's stable FGuid. ResolveLightsForAllPlanets prefers
	 * this entry over the shared WorldDirectionToLight/Intensity whenever
	 * present; planets without an entry keep the legacy shared behavior
	 * (manual/test path, single-planet scenes).
	 *
	 * GameThread-only (StarLink ticks + snapshot builds are all GT).
	 */
	struct FPlanetSunOverride
	{
		/** Normalized Planet->Star direction, world space. */
		FVector DirectionToStarWorld = FVector::ForwardVector;
		/** Effective intensity (illumination * raw star intensity), >= 0. */
		float Intensity = 0.0f;
	};

	void SetPlanetSunDirection(
		const FGuid& PlanetId,
		const FVector& DirectionToStarWorld,
		float EffectiveIntensity);

	/** Get all active light sources. */
	void GetAllLightSources(TArray<FHillaireLightSource>& OutSources) const;

	// ---- Frame Snapshot System (Double-Buffered) ----

	/**
	 * Build the next frame snapshot on the GameThread.
	 * Called once per frame from the ViewExtension's SetupViewFamily.
	 * Produces an immutable FHillaireAtmosphereFrameState for the RenderThread.
	 */
	void BuildNextFrameSnapshot(
		const FVector& ViewOriginWS,
		const FMatrix& ViewMatrix,
		const FMatrix& ProjectionMatrix,
		const FIntRect& ViewRect,
		const FVector& ViewDirectionWS);

	/**
	 * Get the current frame snapshot for the RenderThread.
	 * Returns a shared pointer to immutable frame state.
	 */
	TSharedPtr<const FHillaireAtmosphereFrameState> GetCurrentFrameSnapshot() const;

	/**
	 * Get the frame snapshot for a specific view (GT->RT matching via view state).
	 * Called from ViewExtension on RenderThread.
	 */
	TSharedPtr<const FHillaireAtmosphereFrameState> GetSnapshotForView(const FSceneViewStateInterface* ViewState) const;

	// ---- LUT Cache ----

	FHillaireLutManager* GetLutManager() const { return LutManager.Get(); }

	/** Invalidate LUTs for a specific planet (profile change). */
	void InvalidatePlanetLuts(const FGuid& PlanetId);

	// ---- Render Knobs (bake into LUT keys, never hidden globals) ----

	UPROPERTY(EditAnywhere, Category = "Hillaire")
	float MultipleScatteringFactor = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Hillaire")
	bool bFastSkyEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Hillaire")
	bool bAtmosphereEnabled = true;

	static bool IsEnabledByCVar();

	// ---- Debug/Validation ----

	/** Dump current frame state to log. */
	void DumpFrameState() const;

private:
	// Internal planet entry (component or external)
	struct FPlanetEntry
	{
		FGuid PlanetId;
		FName PlanetName;
		TObjectPtr<UHillaireAtmosphereComponent> Component; // null for external
		FPlanetAtmosphereState CurrentState;                // latest GT state
		FPlanetAtmosphereState PendingState;                // being built for next frame
		bool bHasPendingState = false;
	};

	// Internal star entry
	struct FStarEntry
	{
		FGuid StarId;
		FHillaireLightSource Light;
		TObjectPtr<UHillaireAtmosphereLightComponent> Component; // null for external
	};

	// Double-buffered frame snapshots
	struct FFrameSnapshotEntry
	{
		TSharedPtr<const FHillaireAtmosphereFrameState> Snapshot;
		uint64 FrameNumber = 0;
	};

	int32 FindPlanetIndex(const FGuid& PlanetId) const;
	int32 FindStarIndex(const FGuid& StarId) const;

	void UpdatePlanetStatesFromComponents();
	void ResolveLightsForAllPlanets();
	FHillaireAtmosphereFrameState BuildFrameState(
		const FVector& ViewOriginWS,
		const FMatrix& ViewMatrix,
		const FMatrix& ProjectionMatrix,
		const FIntRect& ViewRect,
		const FVector& ViewDirectionWS) const;

	void SwapFrameSnapshots();
	void PruneStaleSnapshots(uint64 CurrentFrame);

	// Planet/star registries (GameThread only)
	TArray<FPlanetEntry> PlanetRegistry;
	TArray<FStarEntry> StarRegistry;

	// Per-planet sun direction/intensity overrides, keyed by stable PlanetId
	// (GameThread only; see SetPlanetSunDirection). Entries are dropped when
	// their planet unregisters.
	TMap<FGuid, FPlanetSunOverride> PlanetSunOverrides;

	const FPlanetSunOverride* FindPlanetSunOverride(const FGuid& PlanetId) const;

	// Double-buffered snapshots: GT writes to NextFrame, RT reads from CurrentFrame
	mutable FCriticalSection SnapshotLock;
	TSharedPtr<const FHillaireAtmosphereFrameState> CurrentFrameSnapshot;
	TSharedPtr<const FHillaireAtmosphereFrameState> NextFrameSnapshot;
	uint64 CurrentFrameNumber = 0;

	// Transition diagnostics (GT-only, log-rate tracking across frames):
	// last published governing identity/slot and its LUT build count, so the
	// per-frame SUCCESS line can flag governing changes vs LUT rebuilds.
	mutable FGuid LastDiagGoverningPlanetId;
	mutable int32 LastDiagGoverningSlot = INDEX_NONE;
	mutable uint32 LastDiagLutBuildCount = 0;

	// Per-view snapshot stash (GT->RT matching via shared view state)
	mutable FCriticalSection StashLock;
	TMap<const FSceneViewStateInterface*, FFrameSnapshotEntry> SnapshotStash;

	// LUT manager (owned)
	TUniquePtr<FHillaireLutManager> LutManager;

	// Global view extension
	TSharedPtr<FHillairePlanetaryViewExtension, ESPMode::ThreadSafe> ViewExtension;
};