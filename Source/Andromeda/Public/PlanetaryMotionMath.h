#pragma once

#include "CoreMinimal.h"

/** Shared, double-precision numerical primitives. No engine state or allocations. */
namespace PlanetaryMotion
{
    // Quintic compact support: value, first and second derivative vanish at the edges.
    inline double SmoothStep(double X)
    {
        X = FMath::Clamp(X, 0.0, 1.0);
        return X * X * X * (X * (X * 6.0 - 15.0) + 10.0);
    }

    inline bool IsFinite(const FVector& V)
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
    }

    /** Shortest swing with a caller-owned heading for the otherwise ambiguous antipode. */
    inline FQuat Swing(const FVector& From, const FVector& To, const FVector& Heading)
    {
        const FVector A = From.GetSafeNormal();
        const FVector B = To.GetSafeNormal();
        if (A.IsNearlyZero() || B.IsNearlyZero())
        {
            return FQuat::Identity;
        }
        const double Dot = FMath::Clamp(A | B, -1.0, 1.0);
        if (Dot < -1.0 + 1.e-10)
        {
            FVector Axis = FVector::VectorPlaneProject(Heading, A).GetSafeNormal();
            if (Axis.IsNearlyZero())
            {
                FVector Other;
                A.FindBestAxisVectors(Axis, Other);
            }
            return FQuat(Axis, UE_DOUBLE_PI);
        }
        return FQuat::FindBetweenNormals(A, B).GetNormalized();
    }

    /**
     * Cruise-speed blend between deep space and the planetary region, driven
     * by the existing reference-frame influence weight (0 = deep space,
     * 1 = surface). Geometric (log-domain) interpolation: speeds separated by
     * orders of magnitude blend smoothly without the midpoint collapsing to
     * either endpoint. Returns Space at weight 0 and Base at weight 1.
     * A non-positive Space disables the boost (returns Base); a non-positive
     * Base with a positive Space falls back to a linear fade.
     */
    inline double BlendCruiseSpeed(double Base, double Space, double Weight)
    {
        Weight = FMath::Clamp(Weight, 0.0, 1.0);
        if (Space <= 0.0 || Base >= Space)
        {
            return FMath::Max(Base, 0.0);
        }
        if (Base <= 0.0)
        {
            return Space * (1.0 - Weight);
        }
        return Base * FMath::Pow(Space / Base, 1.0 - Weight);
    }

    /** A velocity carrier, NOT a rebase. Residual momentum is not part of this servo. */
    inline FVector AdvanceCarrier(const FVector& Carrier, const FVector& Target,
        const FVector& MaterialAcceleration, double Weight, double PreviousWeight,
        double ResponseTime, double Dt)
    {
        Weight = FMath::Clamp(Weight, 0.0, 1.0);
        PreviousWeight = FMath::Clamp(PreviousWeight, 0.0, 1.0);
        // Spatial capture solves dC/dw = (Vframe-C)/(1-w). At a constant frame
        // C(w)=C0+w*(Vframe-C0); crossing rapidly does not skip acquisition.
        const double SpatialCapture = Weight > PreviousWeight && PreviousWeight < 1.0
            ? (Weight - PreviousWeight) / (1.0 - PreviousWeight) : 0.0;
        const double TemporalCapture = 1.0 - FMath::Exp(-Weight * Dt / FMath::Max(ResponseTime, 1.e-6));
        const double Alpha = 1.0 - (1.0 - SpatialCapture) * (1.0 - TemporalCapture);
        // A departing carrier coasts; it is never multiplied by a falling weight.
        return Carrier + (Target - Carrier) * Alpha + MaterialAcceleration * (Weight * Dt);
    }
}
