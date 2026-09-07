#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "StarSystem.h"
#include "PlanetaryGravitySystem.generated.h"


// =================================================
// CONFIGURAZIONE GRAVITAZIONALE PARAMETRICA
//
// PLACEHOLDER FISICO (documentato):
// FPlanetGenerationData non contiene ancora massa / densita' / composizione.
// Fino all'introduzione di quei dati, TUTTI i pianeti condividono la stessa
// SurfaceGravity. La massa effettiva e' derivata come:
//
//     GM = SurfaceGravity * PlanetRadius^2
//
// (scaling coerente: a parita' di surface gravity, i pianeti piu' grandi
// attraggono piu' forte e hanno una sfera di influenza piu' ampia).
// Quando esisteranno masse reali, questo parametro verra' sostituito
// senza cambiare la struttura dell'API.
// =================================================

USTRUCT(BlueprintType)
struct FPlanetGravityParameters
{
    GENERATED_BODY()

    /** Accelerazione di gravita' alla superficie del pianeta (cm/s²). Default ~g terrestre (980). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Gravity")
    float SurfaceGravity = 980.0f;

    /** Esponente del decadimento radiale dell'accelerazione (2.0 = legge inversa del quadrato). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Gravity")
    float FalloffExponent = 2.0f;

    /**
     * Se true:  g(r) = SurfaceGravity * Pow(PlanetRadius / r, FalloffExponent).
     * Se false: g(r) = SurfaceGravity costante (solo test/placeholder).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Gravity")
    bool bUseInverseSquareFalloff = true;
};


// =================================================
// RISULTATO DELLA QUERY DI INFLUENZA
//
// Contiene il pianeta dominante + tutti i dati di
// reference frame necessari alle fasi successive
// (movimento ereditato, gravita', superficie locale).
// =================================================

USTRUCT(BlueprintType)
struct FPlanetaryInfluenceData
{
    GENERATED_BODY()

    /** True se e' stato trovato un pianeta dominante valido. */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    bool bValid = false;

    /** PlanetID del pianeta dominante (=-1 se nessuno). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    int64 PlanetID = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    int64 PlanetSeed = 0;

    /** Centro del pianeta dominante nel mondo. */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    FVector PlanetCenter = FVector::ZeroVector;

    /** Posizione del corpo (riportata dall'input). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    FVector BodyPosition = FVector::ZeroVector;

    /** Distanza corpo -> centro del pianeta dominante (cm). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float DistanceToCenter = 0.0f;

    /** Direzione unitaria verso il centro del pianeta (direzione della gravita'). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    FVector GravityDirection = FVector::ZeroVector;

    /** Accelerazione gravitazionale (cm/s²) diretta verso il centro. */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float GravityAcceleration = 0.0f;

    /** Altezza del corpo sopra la superficie (cm) = max(DistanceToCenter - PlanetRadius, 0). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float HeightAboveSurface = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float PlanetRadius = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float TerrainHeight = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float OrbitDistance = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float OrbitalPeriod = 0.0f;

    /** Velocita' orbitale EFFETTIVA del pianeta (cm/s, tempo mondiale). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    FVector OrbitalVelocity = FVector::ZeroVector;

    /** Rotazione mondiale corrente del pianeta dominante. */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    FRotator PlanetRotation = FRotator::ZeroRotator;

    /** Velocita' angolare di rotazione EFFETTIVA (gradi/s mondiali). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    float RotationRateDegreesPerSecond = 0.0f;

    /** Asse di rotazione mondiale (unitario; segno = direzione di rotazione). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    FVector RotationAxis = FVector::ZeroVector;

    /**
     * Velocita' (cm/s) dovuta alla sola rotazione planetaria, valutata alla
     * BodyPosition. Per il futuro reference frame:
     *     moto totale ereditato = OrbitalVelocity + RotationVelocity.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Gravity")
    FVector RotationVelocity = FVector::ZeroVector;
};


UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetaryGravitySystem : public UObject
{
    GENERATED_BODY()

public:

    // =================================================
    // PIANETA DOMINANTE
    // =================================================

    /**
     * Determina il PlanetID del pianeta dominante rispetto a WorldPosition.
     *
     * Criterio (deterministico, estendibile a una futura Sphere of Influence):
     *   score = (SurfaceGravity * PlanetRadius^2) / distance^2
     * Il pianeta dominante e' quello con lo score massimo nel punto dato,
     * cioe' quello che attrae piu' forte. A parita' di score vince il
     * PlanetID minore.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    static int64 GetDominantPlanetID(
        const TArray<FPlanetRuntimeData>& Planets,
        FVector WorldPosition
    );

    // =================================================
    // QUERY COMPLETA DI INFLUENZA
    // =================================================

    /**
     * Calcola influenza + dati di reference frame per il pianeta dominante.
     * Planets deve essere lo snapshot corrente preso da
     * AStarSystem::GetAllPlanetRuntimeData.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    static FPlanetaryInfluenceData CalculatePlanetaryInfluence(
        const TArray<FPlanetRuntimeData>& Planets,
        FVector WorldPosition,
        const FPlanetGravityParameters& Parameters
    );

    /** Come CalculatePlanetaryInfluence ma con la configurazione di default. */
    static FPlanetaryInfluenceData CalculatePlanetaryInfluenceDefault(
        const TArray<FPlanetRuntimeData>& Planets,
        FVector WorldPosition
    );

    // =================================================
    // COMPONENTI DEL CALCOLO
    // =================================================

    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    static FVector CalculateGravityDirection(
        FVector WorldPosition,
        FVector PlanetCenter
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    static float CalculateGravityAcceleration(
        float DistanceToCenter,
        float SurfaceGravity,
        float PlanetRadius,
        float FalloffExponent
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    static FVector CalculateRotationVelocity(
        FVector WorldPosition,
        FVector PlanetCenter,
        FVector RotationAxis,
        float RotationRateDegreesPerSecond
    );

private:

    static int64 SelectDominantPlanet(
        const TArray<FPlanetRuntimeData>& Planets,
        FVector WorldPosition,
        const FPlanetGravityParameters& Parameters,
        FVector& OutPlanetCenter
    );

    static FPlanetGravityParameters GetDefaultParameters();
};