#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "StarSystem.h"
#include "PlanetaryGravitySystem.generated.h"

/** Gameplay/numerical policy, independent of atmosphere rendering. Units: cm, seconds. */
USTRUCT(BlueprintType)
struct FPlanetaryMotionSettings
{
    GENERATED_BODY()

    /** Outer inverse-square acceleration fraction before orbital separation caps the volume. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planetary Motion", meta=(ClampMin="0.0001", ClampMax="0.1"))
    double OuterGravityFraction = 0.01;

    /**
     * Small margin above the planet's physical envelope at which the
     * REFERENCE frame fades to zero. The reference outer radius is derived
     * from the authoritative atmospheric envelope (AtmosphereTopRadius from
     * the shared planet runtime data, lower-bounded by PlanetRadius +
     * TerrainHeight so real terrain is always inside the frame):
     *     ReferenceOuterRadius = max(AtmosphereTopRadius, PlanetRadius +
     *         TerrainHeight) * ReferenceAtmosphereMargin
     * still capped by MaxInfluenceRadius (orbital shell clearance), so the
     * reference field can never include another planet's surface or the
     * star. Gravity uses only OuterGravityFraction and is unaffected by
     * this parameter.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planetary Motion", meta=(ClampMin="1.0"))
    double ReferenceAtmosphereMargin = 1.10;

    /** Half-second response for stationary capture / a changing frame; traversal also captures spatially. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planetary Motion", meta=(ClampMin="0.01"))
    double CaptureResponseTime = 0.5;

    /** Horizon acquisition, not curvature transport. A quarter turn takes at least one second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planetary Motion", meta=(ClampMin="1.0"))
    double AlignmentDegreesPerSecond = 90.0;

    /** Baseline integration at 120 Hz, refined spatially at high speed. No velocity clamp. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planetary Motion", meta=(ClampMin="0.0001"))
    double MaxSimulationStep = 1.0 / 120.0;

    /** At most 1/32 of a radius or transition width per near-body step. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planetary Motion", meta=(ClampMin="0.001", ClampMax="0.1"))
    double SpatialStepFraction = 1.0 / 32.0;
};

/** A continuous field, not a binary mode or a dominant-body transform. */
USTRUCT(BlueprintType)
struct FPlanetaryFieldSample
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    FVector GravityAcceleration = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    double ReferenceInfluence = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    double OrientationInfluence = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    double SurfaceInfluence = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    FVector LocalUp = FVector::ZeroVector;

    /** Unweighted, smoothly blended point velocity of the contributing frames. */
    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    FVector FrameVelocity = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    FVector FrameMaterialAcceleration = FVector::ZeroVector;

    /** Spin is acquired only inside the planetary near-field, not at orbital distances. */
    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    FVector AngularVelocity = FVector::ZeroVector;

    /** Diagnostic identity only. Never used to hard-switch velocity or orientation. */
    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    TObjectPtr<AActor> DominantPlanet = nullptr;

    UPROPERTY(BlueprintReadOnly, Category="Planetary Motion")
    int64 DominantPlanetID = -1;
};

UCLASS()
class ANDROMEDA_API UPlanetaryGravitySystem : public UObject
{
    GENERATED_BODY()
public:
    static bool IsSurfaceGravityBody(const FPlanetRuntimeData& Planet);
    static double GetOuterRadius(const FPlanetRuntimeData& Planet, const FPlanetaryMotionSettings& Settings);
    /** Reference-frame outer radius: atmospheric envelope plus margin, capped by orbital clearance. Gravity never uses this. */
    static double GetReferenceOuterRadius(const FPlanetRuntimeData& Planet, const FPlanetaryMotionSettings& Settings);
    static double GetReferenceWeight(const FPlanetRuntimeData& Planet, double Distance, const FPlanetaryMotionSettings& Settings);
    static double GetSurfaceWeight(const FPlanetRuntimeData& Planet, double Distance);
    static FPlanetaryFieldSample Sample(TConstArrayView<FPlanetRuntimeData> Planets,
        const FVector& Position, const FVector& WorldVelocity, const FPlanetaryMotionSettings& Settings);
    static double ChooseSimulationStep(TConstArrayView<FPlanetRuntimeData> Planets,
        const FVector& Position, const FVector& Velocity, double RemainingTime, const FPlanetaryMotionSettings& Settings);
};
