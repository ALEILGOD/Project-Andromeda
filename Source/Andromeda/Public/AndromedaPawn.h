#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "StarSystem.h"
#include "AndromedaPawn.generated.h"


// =========================================================
// MOVEMENT COMPONENT (GRAVITY-ENABLED FLOATING MOVEMENT)
//
// Sottoclasse di UFloatingPawnMovement, il componente di movimento
// del Default Pawn di Unreal. Aggiunge la gravita' planetaria
// (UPlanetaryGravitySystem) e il riferimento fisico del pianeta
// dominante SENZA sostituire l'input handling: il giocatore continua
// a muoversi esattamente come con il Default Pawn.
//
// SCOMPOSIZIONE DELLA VELOCITA':
//
//     Velocity (mondiale) =
//         PlayerVelocity + RelativeVelocity + PlanetFrameVelocity
//
// - PlayerVelocity: parte gestita dalla base class (accelerazione,
//   decelerazione, clamp a MaxSpeed dell'input WASD del giocatore).
// - RelativeVelocity: velocita' RELATIVA al frame del pianeta dominante;
//   la gravita' si integra qui (v += a * dt). Fuori da qualsiasi volume
//   di influenza rappresenta la velocita' inerziale del pawn nello spazio
//   (non viene mai azzerata artificialmente).
// - PlanetFrameVelocity: velocita' ereditata dal pianeta dominante
//   (OrbitalVelocity + RotationVelocity di FPlanetaryInfluenceData),
//   attiva SOLO dentro il Gravity Influence Volume. All'uscita resta
//   congelata e diventa parte della velocita' inerziale.
//
// Perche' l'isolamento dalla base class: UFloatingPawnMovement decelera
// e clamp a MaxSpeed SEMPRE l'intera Velocity (anche senza input).
// Sottraendo le componenti non-giocatore prima di
// Super::ApplyControlInputToVelocity e riaddendole dopo, la base class
// agisce solo sulla parte del giocatore mentre gravita' e moto planetario
// integrano correttamente (integrazione semi-implicita: v += a * dt).
//
// TRANSIZIONI (nessun teleport, nessuno scatto di velocita'):
// - Ingresso nel volume: la scomposizione viene ribasata sul frame del
//   pianeta mantenendo IDENTICA la velocita' mondiale totale.
// - Uscita dal volume: gravita' = 0 e frame congelato; il pawn conserva
//   la velocita' inerziale risultante (nessuna frenata/azzeramento).
// - Cambio pianeta dominante (A -> B): stessa compensazione, quindi la
//   velocita' mondiale resta continua e la relativa si ribasa su B.
// =========================================================

UCLASS(
    ClassGroup = (Andromeda),
    meta = (BlueprintSpawnableComponent)
)
class ANDROMEDA_API UAndromedaPawnMovement : public UFloatingPawnMovement
{
    GENERATED_BODY()

public:

    UAndromedaPawnMovement(const FObjectInitializer& ObjectInitializer);

    /**
     * Aggiorna lo stato di influenza planetaria del movimento (aggiornamento
     * ATOMICO di frame pianeta + gravita', chiamato ogni tick da AAndromedaPawn).
     *
     * bInInfluence:
     *   true  -> il pawn e' dentro il Gravity Influence Volume del pianeta
     *            dominante: attiva la velocita' ereditata dal pianeta
     *            (orbita + rotazione) e la gravita'.
     *   false -> gravita' disattivata; frame e velocita' relativa restano
     *            congelati come parte della velocita' inerziale.
     *
     * Input contenenti NaN/Infinity vengono ignorati (lo stato corrente e'
     * preservato) per non contaminare la velocita' del pawn.
     */
    void SetPlanetaryInfluence(
        bool bInInfluence,
        FVector InPlanetFrameVelocity,
        FVector InGravityDirection,
        float InGravityAcceleration
    );

    /** True se il pawn e' attualmente dentro un Gravity Influence Volume. */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    bool IsInsidePlanetGravityInfluence() const;

    /**
     * Velocita' ereditata dal frame del pianeta dominante (cm/s):
     * OrbitalVelocity + RotationVelocity valutata alla posizione del pawn.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    FVector GetPlanetFrameVelocity() const;

    /**
     * Velocita' del pawn relativa al frame del pianeta dominante (cm/s).
     * Fuori da qualsiasi volume coincide con la velocita' mondiale.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Gravity")
    FVector GetPlanetaryRelativeVelocity() const;

protected:

    /**
     * Gestione input INVARIATA del FloatingPawnMovement + integrazione della
     * gravita' sulla velocita' relativa al pianeta e della velocita'
     * ereditata dal frame planetario.
     */
    virtual void ApplyControlInputToVelocity(
        float DeltaTime
    ) override;

private:

    /** Direzione unitaria della gravita' (verso il centro del pianeta dominante). */
    FVector GravityDirection = FVector::ZeroVector;

    /** Accelerazione gravitazionale corrente (cm/s²). */
    float GravityAcceleration = 0.0f;

    /**
     * Velocita' RELATIVA al frame del pianeta dominante (cm/s).
     * Qui si integra la gravita'. Fuori da qualsiasi volume di influenza
     * rappresenta la velocita' inerziale del pawn (mai azzerata).
     */
    FVector RelativeVelocity = FVector::ZeroVector;

    /**
     * Velocita' ereditata dal pianeta dominante (cm/s):
     * OrbitalVelocity + RotationVelocity. Attiva solo dentro il volume;
     * all'uscita resta congelata come parte della velocita' inerziale.
     */
    FVector PlanetFrameVelocity = FVector::ZeroVector;

    /** True mentre il pawn e' dentro un Gravity Influence Volume. */
    bool bInsidePlanetInfluence = false;
};


// =========================================================
// ANDROMEDA PAWN
//
// Sottoclasse del Default Pawn di Unreal. Mantiene intatto il
// comportamento/movimento del Default Pawn e lo estende con la
// gravita' planetaria (UPlanetaryGravitySystem).
//
// Durante Play Unreal verra' spawnato al posto di DefaultPawn
// tramite AAndromedaGameMode::DefaultPawnClass.
// =========================================================

UCLASS(config = Game, Blueprintable, BlueprintType)
class ANDROMEDA_API AAndromedaPawn : public ADefaultPawn
{
    GENERATED_BODY()

public:

    AAndromedaPawn(const FObjectInitializer& ObjectInitializer);

protected:

    virtual void Tick(
        float DeltaTime
    ) override;

private:

    // =========================================================
    // GRAVITY UPDATE
    // =========================================================

    void UpdatePlanetaryGravity(
        float DeltaTime
    );

    AStarSystem* AcquireStarSystem();

    // =========================================================
    // CACHED STATE (performance)
    // =========================================================

    /** StarSystem runtime caché; ricalcolato solo quando necessario. */
    AStarSystem* CachedStarSystem = nullptr;

    /**
     * Cooldown (secondi) prima di ritentare la ricerca dello StarSystem,
     * per evitare ricerche costose ogni frame quando non e' disponibile.
     */
    float StarSystemSearchCooldown = 0.0f;

    /** Snapshot dei pianeti riusato ogni frame (evita allocazioni). */
    TArray<FPlanetRuntimeData> CachedPlanetRuntimeData;

    /** Componente di movimento gravitazionale (risolto una volta). */
    UAndromedaPawnMovement* GravityMovementComponent = nullptr;
};