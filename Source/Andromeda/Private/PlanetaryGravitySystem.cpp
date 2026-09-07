#include "PlanetaryGravitySystem.h"

#include "StarSystem.h"


FPlanetGravityParameters UPlanetaryGravitySystem::GetDefaultParameters()
{
    // Default della struct: SurfaceGravity=980 cm/s² (circa g terrestre),
    // FalloffExponent=2, bUseInverseSquareFalloff=true.
    // Vedi documentazione di FPlanetGravityParameters in PlanetaryGravitySystem.h.
    return FPlanetGravityParameters();
}


int64 UPlanetaryGravitySystem::GetDominantPlanetID(
    const TArray<FPlanetRuntimeData>& Planets,
    FVector WorldPosition
)
{
    FVector IgnoredCenter = FVector::ZeroVector;

    return SelectDominantPlanet(
        Planets,
        WorldPosition,
        GetDefaultParameters(),
        IgnoredCenter
    );
}


int64 UPlanetaryGravitySystem::SelectDominantPlanet(
    const TArray<FPlanetRuntimeData>& Planets,
    FVector WorldPosition,
    const FPlanetGravityParameters& Parameters,
    FVector& OutPlanetCenter
)
{
    int64 DominantID = -1;
    float BestScore = -1.0f;
    FVector BestCenter = FVector::ZeroVector;

    for (
        const FPlanetRuntimeData& Planet :
        Planets
        )
    {
        if (!Planet.bValid ||
            Planet.PlanetRadius <= 0.0f)
        {
            continue;
        }

        const FVector ToCenter =
            Planet.WorldPosition -
            WorldPosition;

        const float Distance =
            FMath::Max(
                ToCenter.Size(),
                1.0f
            );

        const float DistanceSquared =
            Distance * Distance;

        // GM effettivo derivato dal placeholder:
        //     GM = SurfaceGravity * PlanetRadius^2
        // (vedi documentazione di FPlanetGravityParameters).
        const float EffectiveGM =
            Parameters.SurfaceGravity *
            Planet.PlanetRadius *
            Planet.PlanetRadius;

        // Score = attrazione effettiva nel punto (inverso del quadrato della
        // distanza). Il pianeta dominante e' quello che attrae piu' forte.
        const float Score =
            EffectiveGM /
            DistanceSquared;

        // Tie-break deterministico: a parita' di score vince il PlanetID minore
        // (il confronto `>` non aggiorna il best su valori uguali).
        if (Score > BestScore)
        {
            BestScore = Score;
            DominantID = Planet.PlanetID;
            BestCenter = Planet.WorldPosition;
        }
    }

    OutPlanetCenter = BestCenter;

    return DominantID;
}


FPlanetaryInfluenceData UPlanetaryGravitySystem::CalculatePlanetaryInfluence(
    const TArray<FPlanetRuntimeData>& Planets,
    FVector WorldPosition,
    const FPlanetGravityParameters& Parameters
)
{
    FPlanetaryInfluenceData Result;

    Result.BodyPosition = WorldPosition;

    FVector DominantCenter = FVector::ZeroVector;

    const int64 DominantID =
        SelectDominantPlanet(
            Planets,
            WorldPosition,
            Parameters,
            DominantCenter
        );

    if (DominantID < 0)
    {
        return Result;
    }

    for (
        const FPlanetRuntimeData& Planet :
        Planets
        )
    {
        if (!Planet.bValid ||
            Planet.PlanetID != DominantID)
        {
            continue;
        }

        Result.bValid = true;

        Result.PlanetID = Planet.PlanetID;
        Result.PlanetSeed = Planet.PlanetSeed;

        Result.PlanetCenter = DominantCenter;
        Result.PlanetRadius = Planet.PlanetRadius;
        Result.TerrainHeight = Planet.TerrainHeight;
        Result.OrbitDistance = Planet.OrbitDistance;
        Result.OrbitalPeriod = Planet.OrbitalPeriod;

        Result.OrbitalVelocity = Planet.OrbitalVelocity;
        Result.PlanetRotation = Planet.CurrentRotation;
        Result.RotationRateDegreesPerSecond =
            Planet.RotationRateDegreesPerSecond;
        Result.RotationAxis = Planet.RotationAxis;

        const FVector ToCenter =
            DominantCenter -
            WorldPosition;

        Result.DistanceToCenter = ToCenter.Size();

        Result.GravityDirection =
            ToCenter.GetSafeNormal();

        const float EffectiveFalloffExponent =
            Parameters.bUseInverseSquareFalloff
                ? Parameters.FalloffExponent
                : 0.0f;

        Result.GravityAcceleration =
            CalculateGravityAcceleration(
                Result.DistanceToCenter,
                Parameters.SurfaceGravity,
                Result.PlanetRadius,
                EffectiveFalloffExponent
            );

        Result.HeightAboveSurface =
            FMath::Max(
                Result.DistanceToCenter -
                    Result.PlanetRadius,
                0.0f
            );

        Result.RotationVelocity =
            CalculateRotationVelocity(
                WorldPosition,
                DominantCenter,
                Result.RotationAxis,
                Result.RotationRateDegreesPerSecond
            );

        break;
    }

    return Result;
}


FPlanetaryInfluenceData UPlanetaryGravitySystem::CalculatePlanetaryInfluenceDefault(
    const TArray<FPlanetRuntimeData>& Planets,
    FVector WorldPosition
)
{
    return CalculatePlanetaryInfluence(
        Planets,
        WorldPosition,
        GetDefaultParameters()
    );
}


FVector UPlanetaryGravitySystem::CalculateGravityDirection(
    FVector WorldPosition,
    FVector PlanetCenter
)
{
    return (PlanetCenter - WorldPosition).GetSafeNormal();
}


float UPlanetaryGravitySystem::CalculateGravityAcceleration(
    float DistanceToCenter,
    float SurfaceGravity,
    float PlanetRadius,
    float FalloffExponent
)
{
    if (SurfaceGravity <= 0.0f)
    {
        return 0.0f;
    }

    const float EffectiveDistance =
        FMath::Max(
            DistanceToCenter,
            1.0f
        );

    // Sotto/alla superficie il valore e' bloccato alla SurfaceGravity
    // (placeholder: non modelliamo l'interno del pianeta).
    // Con FalloffExponent == 0 la gravita' e' costante (test/placeholder).
    if (EffectiveDistance <= PlanetRadius ||
        FalloffExponent == 0.0f)
    {
        return SurfaceGravity;
    }

    const float Ratio =
        PlanetRadius /
        EffectiveDistance;

    return SurfaceGravity *
        FMath::Pow(
            Ratio,
            FalloffExponent
        );
}


FVector UPlanetaryGravitySystem::CalculateRotationVelocity(
    FVector WorldPosition,
    FVector PlanetCenter,
    FVector RotationAxis,
    float RotationRateDegreesPerSecond
)
{
    if (RotationAxis.IsNearlyZero())
    {
        return FVector::ZeroVector;
    }

    // Vettore velocita' angolare mondiale (rad/s):
    //     omega = RotationAxis * DegreesToRadians(RotationRateDegreesPerSecond)
    const FVector AngularVelocity =
        RotationAxis *
        FMath::DegreesToRadians(
            RotationRateDegreesPerSecond
        );

    // v = omega x r  (r = posizione del corpo relativa al centro del pianeta)
    return FVector::CrossProduct(
        AngularVelocity,
        WorldPosition - PlanetCenter
    );
}