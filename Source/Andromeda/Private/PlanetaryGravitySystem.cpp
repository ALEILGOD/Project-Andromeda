#include "PlanetaryGravitySystem.h"

#include "StarSystem.h"


FPlanetGravityParameters UPlanetaryGravitySystem::GetDefaultParameters()
{
    // Default della struct: SurfaceGravity=980 cm/s² (circa g terrestre),
    // FalloffExponent=2, bUseInverseSquareFalloff=true,
    // GravityInfluenceMultiplier=1.3.
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
    if (WorldPosition.ContainsNaN())
    {
        OutPlanetCenter = FVector::ZeroVector;
        return -1;
    }

    const float SafeInfluenceMultiplier =
        FMath::Max(
            Parameters.GravityInfluenceMultiplier,
            0.0f
        );

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

        if (ToCenter.ContainsNaN())
        {
            continue;
        }

        const float Distance =
            FMath::Max(
                ToCenter.Size(),
                1.0f
            );

        // Volume di influenza gravitazionale DINAMICO, derivato dai dati
        // reali del pianeta:
        //     GravityInfluenceRadius = (PlanetRadius + TerrainHeight) * 1.3
        // Fuori da questo volume il pianeta NON compete (gravita' = 0).
        const float InfluenceRadius =
            CalculateInfluenceRadius(
                Planet.PlanetRadius,
                Planet.TerrainHeight,
                SafeInfluenceMultiplier
            );

        if (Distance > InfluenceRadius)
        {
            continue;
        }

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
        // distanza). Tra i pianeti che contengono il corpo, il dominante e'
        // quello che attrae piu' forte.
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


float UPlanetaryGravitySystem::CalculateInfluenceRadius(
    float PlanetRadius,
    float TerrainHeight,
    float InfluenceMultiplier
)
{
    if (!FMath::IsFinite(PlanetRadius) ||
        !FMath::IsFinite(TerrainHeight) ||
        !FMath::IsFinite(InfluenceMultiplier))
    {
        return 0.0f;
    }

    const float SafeMultiplier =
        FMath::Max(
            InfluenceMultiplier,
            0.0f
        );

    return FMath::Max(
        (PlanetRadius + TerrainHeight) *
            SafeMultiplier,
        0.0f
    );
}


FPlanetaryInfluenceData UPlanetaryGravitySystem::CalculatePlanetaryInfluence(
    const TArray<FPlanetRuntimeData>& Planets,
    FVector WorldPosition,
    const FPlanetGravityParameters& Parameters
)
{
    FPlanetaryInfluenceData Result;

    if (WorldPosition.ContainsNaN())
    {
        return Result;
    }

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

        // =========================================================
        // IDENTITA' DEL PIANETA DOMINANTE
        // =========================================================

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

        // =========================================================
        // SUPERFICIE FISICA E VOLUME DI INFLUENZA
        //
        // Il limite della superficie non e' PlanetRadius ma
        // PlanetRadius + TerrainHeight. Il volume di influenza e'
        // (PlanetRadius + TerrainHeight) * GravityInfluenceMultiplier.
        // =========================================================

        const float SurfaceRadius =
            FMath::Max(
                Planet.PlanetRadius + Planet.TerrainHeight,
                1.0f
            );

        Result.GravityInfluenceRadius =
            CalculateInfluenceRadius(
                Planet.PlanetRadius,
                Planet.TerrainHeight,
                Parameters.GravityInfluenceMultiplier
            );

        Result.SurfaceRadius = SurfaceRadius;

        // =========================================================
        // GEOMETRIA CORPO -> PIANETA
        // =========================================================

        const FVector ToCenter =
            DominantCenter -
            WorldPosition;

        Result.DistanceToCenter = ToCenter.Size();

        Result.GravityDirection =
            ToCenter.GetSafeNormal();

        // Difensivo: il dominante e' selezionato solo dentro il volume, ma il
        // flag viene comunque calcolato esplicitamente.
        Result.bIsInsideInfluenceRadius =
            Result.DistanceToCenter <=
            Result.GravityInfluenceRadius;

        // =========================================================
        // GRAVITA (falloff esistente, clamp alla superficie fisica)
        // =========================================================

        const float EffectiveFalloffExponent =
            Parameters.bUseInverseSquareFalloff
                ? Parameters.FalloffExponent
                : 0.0f;

        Result.GravityAcceleration =
            CalculateGravityAcceleration(
                Result.DistanceToCenter,
                Parameters.SurfaceGravity,
                Planet.PlanetRadius,
                SurfaceRadius,
                EffectiveFalloffExponent
            );

        Result.HeightAboveSurface =
            FMath::Max(
                Result.DistanceToCenter -
                    SurfaceRadius,
                0.0f
            );

        // =========================================================
        // ROTAZIONE DEL PIANETA (futuro reference frame; preparata)
        // =========================================================

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
    float SurfaceRadius,
    float FalloffExponent
)
{
    if (!FMath::IsFinite(DistanceToCenter) ||
        !FMath::IsFinite(SurfaceGravity) ||
        !FMath::IsFinite(PlanetRadius) ||
        !FMath::IsFinite(SurfaceRadius))
    {
        return 0.0f;
    }

    if (SurfaceGravity <= 0.0f ||
        PlanetRadius <= 0.0f)
    {
        return 0.0f;
    }

    const float EffectiveDistance =
        FMath::Max(
            DistanceToCenter,
            1.0f
        );

    // Superficie fisica: PlanetRadius + TerrainHeight. Difensivo: il
    // raggio del falloff non puo' scendere sotto il core del pianeta.
    const float SafeSurfaceRadius =
        FMath::Max(
            SurfaceRadius,
            PlanetRadius
        );

    // Sotto/alla superficie il valore e' bloccato alla SurfaceGravity
    // (placeholder: non modelliamo l'interno del pianeta).
    // Con FalloffExponent == 0 la gravita' e' costante (test/placeholder).
    if (EffectiveDistance <= SafeSurfaceRadius ||
        FalloffExponent == 0.0f)
    {
        return SurfaceGravity;
    }

    const float Ratio =
        SafeSurfaceRadius /
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
    if (RotationAxis.IsNearlyZero() ||
        !FMath::IsFinite(RotationRateDegreesPerSecond))
    {
        return FVector::ZeroVector;
    }

    if (WorldPosition.ContainsNaN() ||
        PlanetCenter.ContainsNaN())
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