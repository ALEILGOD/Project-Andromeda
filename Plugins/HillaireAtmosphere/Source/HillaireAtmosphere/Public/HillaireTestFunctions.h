#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HillaireTestFunctions.generated.h"

/**
 * HILLAIRE ATMOSPHERE - TEST INTERFACES (Phase 1).
 *
 * Data-driven scenario table (lab ApplyScenario analogue) plus the
 * serializable test snapshot (lab MPLN2 field-list analogue). Phase 1
 * provides the STRUCTURES and a pure round-trip; the full 20-scenario
 * automation harness and the test map arrive with the demo milestone.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireScenarioDef
{
	GENERATED_BODY()

	/** Lab scenario name (e.g. Surface, Space, HighOrbit, SunFacing...). */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Test")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Hillaire|Test")
	FString Description;

	/** Focus planet slot this scenario frames. */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Test")
	int32 FocusPlanetIndex = 0;

	/** Camera height above ground, km (lab relative-height table style). */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Test")
	float CameraHeightKm = 0.0f;

	/** Which registered lights are enabled (by registry index). */
	UPROPERTY(EditAnywhere, Category = "Hillaire|Test")
	TArray<int32> EnabledLightIndices;
};

/**
 * Serializable regression-test state. Field list mirrors the lab MPLN2
 * save: camera, sun/light registry summary, planet summary, hashes.
 */
USTRUCT(BlueprintType)
struct HILLAIREATMOSPHERE_API FHillaireTestSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	FName ScenarioName;

	UPROPERTY()
	FVector CameraPositionCm = FVector::ZeroVector;

	UPROPERTY()
	FRotator CameraRotation = FRotator::ZeroRotator;

	UPROPERTY()
	int32 PlanetCount = 0;

	UPROPERTY()
	int32 EnabledLightCount = 0;

	UPROPERTY()
	uint64 SnapshotHash = 0;

	UPROPERTY()
	uint64 ProfileHash = 0;

	/** Deterministic key=value serialization (round-trip tested). */
	FString ToString() const;
	bool FromString(const FString& Str);
	bool operator==(const FHillaireTestSnapshot& Other) const;
};

UCLASS()
class HILLAIREATMOSPHERE_API UHillaireAtmosphereTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Phase-1 scenario table (core coverage; extended to 20 with the demo). */
	UFUNCTION(BlueprintPure, Category = "Hillaire|Test")
	static TArray<FHillaireScenarioDef> GetScenarioTable();

	UFUNCTION(BlueprintPure, Category = "Hillaire|Test")
	static bool FindScenario(const FName& Name, FHillaireScenarioDef& OutDef);
};
