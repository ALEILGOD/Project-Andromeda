#pragma once

#include "CoreMinimal.h"
#include "StarSystemGenerator.h"

/** The star system's circular/inclined orbit solution and its analytic derivatives. */
struct FAndromedaOrbitState
{
    FVector Position = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    FVector Acceleration = FVector::ZeroVector;
};

namespace AndromedaOrbit
{
    inline FAndromedaOrbitState Evaluate(const FPlanetGenerationData& P,
        double SimulationTime, double OrbitTimeScale, double WorldSimulationRate)
    {
        FAndromedaOrbitState Out;
        if (!FMath::IsFinite(P.OrbitalPeriod) || P.OrbitalPeriod <= 0.0)
        {
            return Out;
        }
        const double PhaseRate = 2.0 * UE_DOUBLE_PI / P.OrbitalPeriod;
        const double Angle = FMath::Fmod(FMath::DegreesToRadians(double(P.OrbitAngle))
            + SimulationTime * OrbitTimeScale * PhaseRate, 2.0 * UE_DOUBLE_PI);
        const double Inclination = FMath::DegreesToRadians(double(P.OrbitInclination));
        const double Omega = PhaseRate * OrbitTimeScale * WorldSimulationRate;
        const double C = FMath::Cos(Angle);
        const double S = FMath::Sin(Angle);
        Out.Position = FVector(C, S * FMath::Cos(Inclination), S * FMath::Sin(Inclination)) * P.OrbitDistance;
        Out.Velocity = FVector(-S, C * FMath::Cos(Inclination), C * FMath::Sin(Inclination)) * (P.OrbitDistance * Omega);
        Out.Acceleration = Out.Position * (-Omega * Omega);
        return Out;
    }
}
