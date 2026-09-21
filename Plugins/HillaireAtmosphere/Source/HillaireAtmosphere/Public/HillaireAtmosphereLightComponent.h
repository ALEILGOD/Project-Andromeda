#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HillaireLightSource.h"
#include "HillaireAtmosphereLightComponent.generated.h"

/**
 * HILLAIRE ATMOSPHERE - ATMOSPHERE LIGHT SOURCE (Phase 1).
 *
 * Attach to ANY compatible light source. Reference architecture:
 *   Point Light
 *     +-- Atmosphere Light Source
 *              +-- atmospheric contribution
 * The interface is light-type agnostic: at render time the subsystem sees
 * only normalized FHillaireLightSource structs discriminated by bDirectional.
 * Adding spot/rect lights later = new GameThread normalization, zero
 * shader-struct change.
 *
 * The component NEVER duplicates position/color/intensity as render truth:
 * it READS the owning ULightComponent every collection (position, color,
 * intensity, visibility) and ANDs it with its own gates. Its own Color /
 * Intensity are offsets for owners WITHOUT a light component (test sun rig).
 * Disabling this component != disabling the light (and vice versa).
 */
UCLASS(ClassGroup = (Hillaire), meta = (BlueprintSpawnableComponent))
class HILLAIREATMOSPHERE_API UHillaireAtmosphereLightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHillaireAtmosphereLightComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Atmosphere participation gate (ANDed with owner-light visibility). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Light")
	bool bEnabled = true;

	/**
	 * True: sun-like rig, direction from owner orientation. False (default):
	 * point-light-attached, position from owner location.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Light")
	bool bDirectional = false;

	/** Fallback color when the owner has no ULightComponent (test rigs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Light")
	FLinearColor Color = FLinearColor::White;

	/** Fallback intensity when the owner has no ULightComponent. Lab units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Light")
	float Intensity = 1.0f;

	/** Stellar disk angular radius, radians. >0 draws a disk (governing only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Light")
	float AngularRadiusRad = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Light")
	bool bDrawDisk = false;

	/** Stable identifier for rendering/cache keys. Generated if invalid. */
	UPROPERTY(VisibleAnywhere, Category = "Hillaire|Light")
	FGuid LightGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hillaire|Light")
	FName LightName;

	/** Build the GameThread-side light snapshot (reads owner light live). */
	bool BuildLightSource(FHillaireLightSource& OutSource) const;

private:
	void RegisterWithSubsystem();
	void UnregisterFromSubsystem();
};
