#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HillaireAtmosphereProfile.h"
#include "HillairePlanetState.h"
#include "HillairePlanetAtmosphereState.h"
#include "HillaireAtmosphereComponent.generated.h"

/**
 * HILLAIRE ATMOSPHERE - PLANET COMPONENT (Phase 1).
 *
 * UObject/Actor-facing side of one planetary atmosphere. Owns authoring data
 * (radii, terrain contribution, scattering profile) and registers with the
 * world subsystem, which assigns the stable PlanetId slot.
 *
 * Authoring inputs are ground-centric (GroundRadiusKm + AtmosphereHeightKm),
 * matching how Andromeda authors planets (APlanet.PlanetRadius in cm,
 * converted at registration). On registration the component folds them into
 * Profile.BottomRadiusKm/TopRadiusKm, which are authoritative downstream.
 *
 * FUTURE STARMAP SEAM (not this phase): adapters in Andromeda will drive
 *   STARMAP Planet actor -> FHillairePlanetState { Center, Rotation, Profile }
 * via RegisterExternalPlanet(). This component stays the test/manual path.
 * HillaireAtmosphere never becomes a second astronomy system.
 */
UCLASS(ClassGroup = (Hillaire), meta = (BlueprintSpawnableComponent))
class HILLAIREATMOSPHERE_API UHillaireAtmosphereComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHillaireAtmosphereComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Display/identity name (defaults to owner name at registration). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Planet")
	FName PlanetName;

	/** Center -> ground, km. Authored; folded into Profile on registration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Planet", meta = (ClampMin = "0.001"))
	float GroundRadiusKm = 1000.0f;

	/** Ground -> atmosphere top, km. Authored; folded into Profile on registration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Planet", meta = (ClampMin = "0.001"))
	float AtmosphereHeightKm = 100.0f;

	/** Terrain/height contribution, km (e.g. from APlanet.TerrainHeight in cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Planet", meta = (ClampMin = "0.0"))
	float TerrainHeightKm = 0.0f;

	/** Scattering profile. Radii fields are OVERWRITTEN from the above at registration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Planet")
	FHillaireAtmosphereProfile Profile = FHillaireAtmosphereProfile::MakeReferenceProfile();

	/** <0 = directional/infinite (Case A/B). Finite = Case C/D (future). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Planet")
	float StarDistanceKm = -1.0f;

	/** Assigned slot, INDEX_NONE until registered. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hillaire|Planet")
	int32 PlanetId = INDEX_NONE;

	/** Stable identity for LUT ownership across re-registration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hillaire|Planet")
	FGuid PlanetGuid;

	/** Build the GameThread-side planet state (center/rotation from owner). */
	bool BuildPlanetState(FHillairePlanetState& OutState) const;

	/** Build the new multiplanetary planet atmosphere state. */
	bool BuildPlanetAtmosphereState(FPlanetAtmosphereState& OutState) const;

	/** Push a profile/radii edit into the LUT cache (targeted invalidation). */
	UFUNCTION(BlueprintCallable, Category = "Hillaire|Planet")
	void MarkProfileDirty();

private:
	void RegisterWithSubsystem();
	void UnregisterFromSubsystem();
};
