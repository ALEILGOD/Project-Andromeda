#include "PlanetaryGravitySystem.h"
#include "PlanetaryMotionMath.h"
#include "Planet/Planet.h"

bool UPlanetaryGravitySystem::IsSurfaceGravityBody(const FPlanetRuntimeData& P)
{
    return P.bValid && P.BodyType == ECelestialBodyType::Planet
        && FMath::IsFinite(P.PlanetRadius) && P.PlanetRadius > 0.0
        && FMath::IsFinite(P.SurfaceGravity) && P.SurfaceGravity > 0.0
        && FMath::IsFinite(P.TerrainHeight) && P.TerrainHeight >= 0.0
        && FMath::IsFinite(P.AtmosphereTopRadius) && P.AtmosphereTopRadius >= P.PlanetRadius
        && FMath::IsFinite(P.MaxInfluenceRadius) && P.MaxInfluenceRadius > P.AtmosphereTopRadius
        && (!P.PlanetActor || (IsValid(P.PlanetActor) && P.PlanetActor->IsA<APlanet>()))
        && PlanetaryMotion::IsFinite(P.WorldPosition)
        && PlanetaryMotion::IsFinite(P.OrbitalVelocity)
        && PlanetaryMotion::IsFinite(P.OrbitalAcceleration)
        && PlanetaryMotion::IsFinite(P.AngularVelocity);
}

double UPlanetaryGravitySystem::GetOuterRadius(const FPlanetRuntimeData& P, const FPlanetaryMotionSettings& Settings)
{
    // 1% surface g would be at 10R. The generated compact system has no
    // authored masses/SOI: use its guaranteed orbital shell clearance instead
    // of inventing a competing central mass/Hill model. Volumes may overlap in
    // empty space but cannot reach another body's surface or the star.
    const double NaturalRadius = P.PlanetRadius / FMath::Sqrt(FMath::Clamp(Settings.OuterGravityFraction, 1.e-4, 0.1));
    return FMath::Min(NaturalRadius, P.MaxInfluenceRadius);
}

double UPlanetaryGravitySystem::GetReferenceOuterRadius(const FPlanetRuntimeData& P, const FPlanetaryMotionSettings& Settings)
{
    // The reference frame hugs the planet's physical scale: the authoritative
    // atmospheric envelope already published in the shared runtime data
    // (sourced from the frozen atmosphere geometry contract, read-only here),
    // lower-bounded by the terrain envelope so real surface is always inside
    // the frame, plus a small margin. The orbital-clearance cap is shared with
    // gravity on purpose: it is the load-bearing guarantee that a reference
    // volume never contains another planet's surface or the star (which would
    // dilute grounded frames via blending). Gravity itself never reads this
    // radius.
    const double PhysicalEnvelope = FMath::Max(
        P.AtmosphereTopRadius, P.PlanetRadius + P.TerrainHeight);
    const double NaturalReferenceRadius =
        PhysicalEnvelope * FMath::Max(Settings.ReferenceAtmosphereMargin, 1.0);
    return FMath::Min(NaturalReferenceRadius, P.MaxInfluenceRadius);
}

double UPlanetaryGravitySystem::GetReferenceWeight(const FPlanetRuntimeData& P, double Distance, const FPlanetaryMotionSettings& Settings)
{
    const double Outer = GetReferenceOuterRadius(P, Settings);
    const double Inner = FMath::Max(P.AtmosphereTopRadius, P.PlanetRadius + P.TerrainHeight);
    if (Outer <= Inner || !FMath::IsFinite(Distance))
    {
        return 0.0;
    }
    return PlanetaryMotion::SmoothStep((Outer - Distance) / (Outer - Inner));
}

double UPlanetaryGravitySystem::GetSurfaceWeight(const FPlanetRuntimeData& P, double Distance)
{
    const double Envelope = P.PlanetRadius + P.TerrainHeight;
    // Full tangential controls by the terrain envelope, starting one radius
    // above it. Includes all actual terrain, without claiming the envelope is
    // a physical collision surface.
    return PlanetaryMotion::SmoothStep((Envelope + P.PlanetRadius - Distance) / P.PlanetRadius);
}

FPlanetaryFieldSample UPlanetaryGravitySystem::Sample(TConstArrayView<FPlanetRuntimeData> Planets,
    const FVector& Position, const FVector& WorldVelocity, const FPlanetaryMotionSettings& Settings)
{
    FPlanetaryFieldSample Out;
    if (!PlanetaryMotion::IsFinite(Position) || !PlanetaryMotion::IsFinite(WorldVelocity))
    {
        return Out;
    }
    double Sum = 0.0;
    double SpaceWeight = 1.0;
    double Best = 0.0;
    FVector UpSum = FVector::ZeroVector;
    for (const FPlanetRuntimeData& P : Planets)
    {
        if (!IsSurfaceGravityBody(P))
        {
            continue;
        }
        const FVector R = Position - P.WorldPosition;
        const double Distance = R.Size();
        const double Weight = GetReferenceWeight(P, Distance, Settings);
        // Gravity support intentionally exceeds reference support (gravity
        // keeps its own outer radius), so a body contributes gravity even
        // where its frame weight is zero. Zero-weight frame terms below are
        // algebraic no-ops; the Sum > 0 guard still excludes them.
        if (Weight <= 0.0 && Distance >= GetOuterRadius(P, Settings))
        {
            continue;
        }
        const FVector Up = Distance > UE_DOUBLE_SMALL_NUMBER ? R / Distance : FVector::ZeroVector;
        const double Surface = GetSurfaceWeight(P, Distance);
        const FVector Omega = P.AngularVelocity * Surface;
        const FVector FrameVelocity = P.OrbitalVelocity + (Omega ^ R);
        // Derivative at the moving player's point, from the SAME orbital truth.
        const FVector MaterialAcceleration = P.OrbitalAcceleration
            + (Omega ^ (WorldVelocity - P.OrbitalVelocity));
        Sum += Weight;
        SpaceWeight *= 1.0 - Weight;
        Out.FrameVelocity += FrameVelocity * Weight;
        Out.FrameMaterialAcceleration += MaterialAcceleration * Weight;
        Out.AngularVelocity += Omega * Weight;
        Out.SurfaceInfluence += Surface * Weight;
        UpSum += Up * (Weight * FMath::Min(Distance / P.PlanetRadius, 1.0));

        // Full inverse square in the inner half of the gravity shell; C2
        // taper in its outer half. Uniform-density interior is nonsingular.
        // Intentionally uses the GRAVITY outer radius: the reference-frame
        // extension above must not alter the gravity field.
        const double Ratio = P.PlanetRadius / FMath::Max(Distance, P.PlanetRadius);
        const double Interior = FMath::Min(Distance / P.PlanetRadius, 1.0);
        const double GravityWindow = PlanetaryMotion::SmoothStep(2.0 *
            (GetOuterRadius(P, Settings) - Distance) /
            (GetOuterRadius(P, Settings) - FMath::Max(P.AtmosphereTopRadius, P.PlanetRadius + P.TerrainHeight)));
        Out.GravityAcceleration -= Up * (P.SurfaceGravity * Ratio * Ratio * Interior * GravityWindow);
        if (Weight > Best || (Weight == Best && P.PlanetID < Out.DominantPlanetID))
        {
            Best = Weight;
            Out.DominantPlanet = P.PlanetActor;
            Out.DominantPlanetID = P.PlanetID;
        }
    }
    if (Sum > 0.0)
    {
        Out.ReferenceInfluence = 1.0 - SpaceWeight;
        Out.FrameVelocity /= Sum;
        Out.FrameMaterialAcceleration /= Sum;
        Out.AngularVelocity *= Out.ReferenceInfluence / Sum;
        Out.SurfaceInfluence *= Out.ReferenceInfluence / Sum;
        // Opposing sources extinguish alignment rather than selecting an
        // arbitrary hemisphere. Forces and frame velocity still blend.
        Out.OrientationInfluence = Out.ReferenceInfluence * FMath::Clamp(UpSum.Size() / Sum, 0.0, 1.0);
        Out.LocalUp = UpSum.GetSafeNormal();
    }
    return Out;
}

double UPlanetaryGravitySystem::ChooseSimulationStep(TConstArrayView<FPlanetRuntimeData> Planets,
    const FVector& Position, const FVector& Velocity, double RemainingTime, const FPlanetaryMotionSettings& Settings)
{
    double Step = FMath::Min(RemainingTime, FMath::Max(Settings.MaxSimulationStep, 1.e-6));
    for (const FPlanetRuntimeData& P : Planets)
    {
        if (!IsSurfaceGravityBody(P))
        {
            continue;
        }
        // Gap is measured against the GRAVITY outer radius (the larger of the
        // two fields) so a distant step cannot jump over it; Feature resolves
        // the thinner reference shell, so high-speed substeps cannot skip
        // frame acquisition either.
        const double GravityOuter = GetOuterRadius(P, Settings);
        const double ReferenceOuter = GetReferenceOuterRadius(P, Settings);
        const double Inner = FMath::Max(P.AtmosphereTopRadius, P.PlanetRadius + P.TerrainHeight);
        const double Feature = FMath::Max(FMath::Min(P.PlanetRadius, ReferenceOuter - Inner), 1.0);
        const double Speed = (Velocity - P.OrbitalVelocity).Size() + P.AngularVelocity.Size() * GravityOuter;
        if (Speed > UE_DOUBLE_SMALL_NUMBER)
        {
            const double Gap = FMath::Max((Position - P.WorldPosition).Size() - GravityOuter, 0.0);
            // A distant step cannot jump over a whole field. Inside it, bound
            // spatial/angular error, not the player's velocity or elapsed time.
            Step = FMath::Min(Step, (Gap + Feature * Settings.SpatialStepFraction) / Speed);
        }
    }
    return FMath::Max(FMath::Min(Step, RemainingTime), FMath::Min(RemainingTime, 1.e-9));
}
