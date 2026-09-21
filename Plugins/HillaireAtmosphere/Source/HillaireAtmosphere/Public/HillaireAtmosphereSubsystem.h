#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HillaireLightSource.h"
#include "HillaireLutManager.h"
#include "HillairePlanetState.h"
#include "HillaireViewExtension.h"
#include "HillaireViewSnapshot.h"
#include "HillaireAtmosphereSubsystem.generated.h"
class UHillaireAtmosphereComponent;
class UHillaireAtmosphereLightComponent;

/**
 * HILLAIRE ATMOSPHERE - WORLD SUBSYSTEM (Phase 1).
 *
 * Owns the planet + light registries, LUT manager, render knobs and per-view
 * snapshot builds for ONE world. This is the ONLY owner of atmosphere state;
 * there are no CurrentSun / PrimarySun / CurrentAtmosphere singletons.
 *
 * STARMAP remains the source of truth for stars/planets/orbits/seeds.
 * Future Andromeda adapters bind STARMAP data into RegisterExternalPlanet /
 * the light collector WITHOUT touching this class's model.
 *
 * No per-frame tick: snapshots are built on demand by the ViewExtension
 * (GameThread side) and consumed on the RenderThread. Components push
 * changes via Register/Unregister/Invalidate calls only.
 */
UCLASS()
class HILLAIREATMOSPHERE_API UHillaireAtmosphereSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual ~UHillaireAtmosphereSubsystem();

	// ---- Planet registry (M atmospheres) ----
	int32 RegisterPlanetComponent(UHillaireAtmosphereComponent* Component);
	void UnregisterPlanetComponent(UHillaireAtmosphereComponent* Component);
	int32 GetRegisteredPlanetCount() const;

	// ---- External planets (Phase 2F: STARMAP planet feed) ----
	//
	// GameThread-only POD states pushed by game-module adapters that own the
	// real procedural planet actors. Same lifecycle shape as components:
	// assign a slot once (LUT targets registered, cache preserved across
	// updates), push fresh live values per tick, release on teardown.
	// A re-pushed planet keeps its slot and LUT cache; geometry changes
	// propagate through the profile hash (LUT regen) and every snapshot.
	int32 RegisterExternalPlanetSlot();
	void UpdateExternalPlanet(const FHillairePlanetState& State);
	void UnregisterExternalPlanet(int32 PlanetId);

	// ---- Light registry (N sources) ----
	void RegisterLightComponent(UHillaireAtmosphereLightComponent* Component);
	void UnregisterLightComponent(UHillaireAtmosphereLightComponent* Component);
	int32 GetRegisteredLightCount() const;

	// ---- World-state reads (GameThread) ----
	void GetPlanetStates(TArray<FHillairePlanetState>& OutStates) const;
	void GetLightSources(TArray<FHillaireLightSource>& OutSources) const;

	// ---- External light sources (Phase 2E: STARMAP primary-star feed) ----
	//
	// GameThread-only. External providers (e.g. the STARMAP star link living
	// in the Andromeda game module, which the plugin must never depend on)
	// push plain POD light structs here. They are PREPENDED to the component
	// registry in GetLightSources, so the primary star owns slot 0: the same
	// slot the SkyView bake, the aerial evaluation and the 2D composite all
	// consume as their shared sun source. No scattering math is touched.
	void RegisterExternalLight(const FHillaireLightSource& Light);
	void UnregisterExternalLight(const FGuid& LightId);

	/**
	 * Externals-first merge (pure, unit-tested): external entries keep their
	 * order at the head, component entries follow untouched (including
	 * disabled ones: slot identity is part of the fast-path key).
	 */
	static TArray<FHillaireLightSource> MergeExternalAndComponentLights(
		const TArray<FHillaireLightSource>& ExternalLights,
		const TArray<FHillaireLightSource>& ComponentLights);

	/**
	 * Build the immutable per-view snapshot (GameThread only).
	 * Resolves relevant planets + active lights, converts to camera-relative
	 * km, compacts per-planet lights (N x M expansion).
	 */
	FHillaireViewSnapshot BuildSnapshotForView(
		const FVector& ViewOriginCm,
		const FMatrix& ViewMatrix,
		const FMatrix& ProjectionMatrix,
		const FIntRect& ViewRect,
		const FVector& ViewDirectionWorld) const;

	// ---- LUT cache ----
	FHillaireLutManager* GetLutManager() const { return LutManager.Get(); }
	void InvalidatePlanetLuts(int32 PlanetId);

	// ---- Render knobs (bake into LUT keys, never hidden globals) ----
	UPROPERTY(EditAnywhere, Category = "Hillaire")
	float MultipleScatteringFactor = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Hillaire")
	bool bFastSkyEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Hillaire")
	bool bAtmosphereEnabled = true;

	static bool IsEnabledByCVar();

private:
	int32 AssignPlanetSlot() const;
	bool IsPlanetSlotUsed(int32 Slot) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHillaireAtmosphereComponent>> PlanetComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHillaireAtmosphereLightComponent>> LightComponents;

	/** Phase 2E external star feed (GameThread-only POD, no UObjects). */
	TArray<FHillaireLightSource> ExternalLights;

	/** Phase 2F external planet feed (GameThread-only POD, no UObjects). */
	TArray<FHillairePlanetState> ExternalPlanets;

	/**
	 * Governing-planet hysteresis memory (ATMOS FIX VISIVO DEFINITIVO).
	 * GameThread-only: written by BuildSnapshotForView (the only GT writer),
	 * never touched on the render thread. Identifies the last governing
	 * planet by (PlanetId, PlanetGuid) so the next snapshot can resolve it
	 * to an array index (order may shift on spawn churn; the Guid guards
	 * slot reuse) and keep it stable across selection-boundary jitter.
	 */
	mutable int32 LastGoverningPlanetId = INDEX_NONE;
	mutable FGuid LastGoverningPlanetGuid;

	TUniquePtr<FHillaireLutManager> LutManager;

	/** Global (per-module-lifetime) view extension; filters by world scene. */
	TSharedPtr<FHillaireViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
