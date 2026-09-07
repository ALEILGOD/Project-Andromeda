#include "AndromedaPawn.h"

#include "Kismet/GameplayStatics.h"
#include "PlanetaryGravitySystem.h"
#include "StarSystem.h"


// =========================================================
// HELPERS
// =========================================================

namespace
{
    /**
     * Check di finitezza completo per un vettore: FVector::ContainsNaN non
     * copre Infinity, quindi ogni componente viene validata con
     * FMath::IsFinite (che rifiuta sia NaN sia +/-Infinity).
     */
    bool IsVectorFinite(const FVector& Vector)
    {
        return FMath::IsFinite(Vector.X) &&
               FMath::IsFinite(Vector.Y) &&
               FMath::IsFinite(Vector.Z);
    }
}


// =========================================================
// ANDROMEDA PAWN MOVEMENT
// =========================================================

UAndromedaPawnMovement::UAndromedaPawnMovement(
    const FObjectInitializer& ObjectInitializer
)
    : Super(ObjectInitializer)
{
}


void UAndromedaPawnMovement::SetPlanetaryInfluence(
    bool bInInfluence,
    FVector InPlanetFrameVelocity,
    FVector InGravityDirection,
    float InGravityAcceleration
)
{
    // =========================================================
    // VALIDAZIONE INPUT (NaN / Infinity)
    //
    // Input non finito: lo stato corrente viene PRESERVATO (no-op)
    // invece di propagare valori invalidi nella velocita' del pawn.
    // =========================================================

    if (
        !IsVectorFinite(InPlanetFrameVelocity) ||
        !IsVectorFinite(InGravityDirection) ||
        !FMath::IsFinite(InGravityAcceleration)
        )
    {
        return;
    }

    if (bInInfluence)
    {
        // =========================================================
        // TRANSIZIONE DI FRAME (continuita' della velocita' mondiale)
        //
        // La velocita' mondiale totale non deve mai scattare: la parte
        // relativa viene ribasata sul nuovo frame compensando il delta:
        //
        //     RelativeVelocity += FramePrecedente - FrameNuovo
        //
        // Questa singola formula copre TUTTI i casi:
        // - ingresso nel volume (il "frame precedente" e' la velocita'
        //   inerziale congelata: spazio libero o uscita precedente);
        // - drift orbitale/rotazionale del pianeta mentre si e' dentro;
        // - cambio pianeta dominante (A -> B).
        // In tutti i casi: WorldVelocity = PlayerVelocity +
        // RelativeVelocity + PlanetFrameVelocity resta CONTINUA.
        // =========================================================

        RelativeVelocity +=
            PlanetFrameVelocity - InPlanetFrameVelocity;

        PlanetFrameVelocity = InPlanetFrameVelocity;
        bInsidePlanetInfluence = true;

        if (
            InGravityAcceleration > 0.0f &&
            !InGravityDirection.IsNearlyZero()
            )
        {
            GravityDirection = InGravityDirection.GetSafeNormal();
            GravityAcceleration = InGravityAcceleration;
        }
        else
        {
            // Dati gravitazionali degradati: gravita' OFF, frame OK.
            GravityDirection = FVector::ZeroVector;
            GravityAcceleration = 0.0f;
        }
    }
    else
    {
        // =========================================================
        // USCITA DAL VOLUME
        //
        // Gravita' = 0. RelativeVelocity e PlanetFrameVelocity restano
        // INVARIATI: frame congelato + velocita' relativa costituiscono la
        // velocita' inerziale risultante, che il pawn conserva senza
        // frenate artificiali, richiami verso il pianeta o azzeramenti.
        // =========================================================

        bInsidePlanetInfluence = false;
        GravityDirection = FVector::ZeroVector;
        GravityAcceleration = 0.0f;
    }
}


bool UAndromedaPawnMovement::IsInsidePlanetGravityInfluence() const
{
    return bInsidePlanetInfluence;
}


FVector UAndromedaPawnMovement::GetPlanetFrameVelocity() const
{
    return PlanetFrameVelocity;
}


FVector UAndromedaPawnMovement::GetPlanetaryRelativeVelocity() const
{
    // Dentro l'influenza: velocita' del pawn rispetto al frame del pianeta
    // dominante. Fuori: la velocita' mondiale (nessun frame attivo).
    if (bInsidePlanetInfluence)
    {
        return Velocity - PlanetFrameVelocity;
    }

    return Velocity;
}


void UAndromedaPawnMovement::ApplyControlInputToVelocity(
    float DeltaTime
)
{
    // =========================================================
    // 0) Difensivo: accumulatori non finiti (NaN / Infinity) vengono
    //    azzerati. La logica di gravita'/frame non deve mai contaminare
    //    la velocita' del pawn.
    // =========================================================

    if (!IsVectorFinite(RelativeVelocity))
    {
        RelativeVelocity = FVector::ZeroVector;
    }

    if (!IsVectorFinite(PlanetFrameVelocity))
    {
        PlanetFrameVelocity = FVector::ZeroVector;
    }

    // =========================================================
    // 1) Integra la gravita' sulla velocita' RELATIVA al frame del
    //    pianeta: v_rel += dir * accel * dt.
    //    Fuori da qualsiasi volume la velocita' resta inerziale (nessun
    //    azzeramento artificiale). DeltaTime non valido: nessuna
    //    integrazione.
    // =========================================================

    if (
        DeltaTime > 0.0f &&
        GravityAcceleration > 0.0f &&
        !GravityDirection.IsNearlyZero()
        )
    {
        RelativeVelocity +=
            GravityDirection * (GravityAcceleration * DeltaTime);
    }

    // =========================================================
    // 2) Isola la velocita' NON gestita dal giocatore (velocita' relativa
    //    + velocita' ereditata dal pianeta): l'input handling del
    //    FloatingPawnMovement (accelerazione, decelerazione a riposo,
    //    clamp a MaxSpeed) deve agire SOLO sulla parte del giocatore,
    //    altrimenti decelererebbe o clamperebbe caduta libera e moto
    //    planetario.
    // =========================================================

    const FVector InheritedVelocity =
        RelativeVelocity + PlanetFrameVelocity;

    Velocity -= InheritedVelocity;

    // =========================================================
    // 3) Gestione input del giocatore INVARIATA rispetto al Default Pawn.
    // =========================================================

    Super::ApplyControlInputToVelocity(DeltaTime);

    // =========================================================
    // 4) Ricomponi: la velocita' totale usata come Delta di posizione da
    //    TickComponent (via SafeMoveUpdatedComponent, con le collisioni
    //    del Default Pawn intatte) e':
    //        input del giocatore + velocita' relativa + frame pianeta.
    //    WorldVelocity = PlanetVelocity + RelativeVelocity + Input.
    // =========================================================

    Velocity += InheritedVelocity;
}


// =========================================================
// ANDROMEDA PAWN
// =========================================================

AAndromedaPawn::AAndromedaPawn(
    const FObjectInitializer& ObjectInitializer
)
    : Super(
        ObjectInitializer.SetDefaultSubobjectClass<UAndromedaPawnMovement>(
            Super::MovementComponentName
        )
    )
{
}


void AAndromedaPawn::Tick(
    float DeltaTime
)
{
    Super::Tick(
        DeltaTime
    );

    UpdatePlanetaryGravity(
        DeltaTime
    );
}


void AAndromedaPawn::UpdatePlanetaryGravity(
    float DeltaTime
)
{
    if (DeltaTime <= 0.0f)
    {
        return;
    }

    // =========================================================
    // MOVEMENT COMPONENT
    // =========================================================

    if (!GravityMovementComponent)
    {
        GravityMovementComponent =
            Cast<UAndromedaPawnMovement>(
                GetMovementComponent()
            );

        // Senza movement component valido il movimento normale del
        // Default Pawn continua a funzionare, semplicemente senza gravita'.
        if (!GravityMovementComponent)
        {
            return;
        }
    }

    // =========================================================
    // STAR SYSTEM (caché: nessuna ricerca costosa ogni frame)
    // =========================================================

    if (!CachedStarSystem && StarSystemSearchCooldown > 0.0f)
    {
        StarSystemSearchCooldown -= DeltaTime;
    }

    AStarSystem* StarSystem = AcquireStarSystem();

    if (!StarSystem)
    {
        // Spazio libero (nessuno StarSystem): gravita' OFF, velocita'
        // inerziale preservata.
        GravityMovementComponent->SetPlanetaryInfluence(
            false,
            FVector::ZeroVector,
            FVector::ZeroVector,
            0.0f
        );

        return;
    }

    // =========================================================
    // SNAPSHOT DEI PIANETI (array riusato ogni frame)
    // =========================================================

    StarSystem->GetAllPlanetRuntimeData(
        CachedPlanetRuntimeData
    );

    if (CachedPlanetRuntimeData.Num() == 0)
    {
        // Nessun pianeta: gravita' OFF, velocita' inerziale preservata.
        GravityMovementComponent->SetPlanetaryInfluence(
            false,
            FVector::ZeroVector,
            FVector::ZeroVector,
            0.0f
        );

        return;
    }

    // =========================================================
    // INFLUENZA GRAVITAZIONALE (pianeta dominante, direzione, falloff)
    // =========================================================

    const FPlanetaryInfluenceData Influence =
        UPlanetaryGravitySystem::CalculatePlanetaryInfluenceDefault(
            CachedPlanetRuntimeData,
            GetActorLocation()
        );

    if (
        Influence.bValid &&
        Influence.bIsInsideInfluenceRadius
        )
    {
        // =========================================================
        // VELOCITA' DEL FRAME PLANETARIO
        //
        // Velocita' del pianeta dominante valutata alla posizione del pawn:
        // moto orbitale + moto di rotazione (omega x r). E' la velocita'
        // che il pawn eredita mentre e' dentro il Gravity Influence Volume.
        // =========================================================

        const FVector PlanetFrameVelocity =
            Influence.OrbitalVelocity + Influence.RotationVelocity;

        GravityMovementComponent->SetPlanetaryInfluence(
            true,
            PlanetFrameVelocity,
            Influence.GravityDirection,
            Influence.GravityAcceleration
        );
    }
    else
    {
        // Spazio libero: nessun volume contiene il pawn. Gravita' = 0 e il
        // movimento torna a essere quello standard del Default Pawn,
        // conservando la velocita' inerziale risultante.
        GravityMovementComponent->SetPlanetaryInfluence(
            false,
            FVector::ZeroVector,
            FVector::ZeroVector,
            0.0f
        );
    }
}


AStarSystem* AAndromedaPawn::AcquireStarSystem()
{
    if (CachedStarSystem)
    {
        return CachedStarSystem;
    }

    // Ritenta la ricerca solo a intervalli (cooldown) invece che ogni frame,
    // per il caso in cui lo StarSystem non esista o sia ancora in spawn.
    if (StarSystemSearchCooldown > 0.0f)
    {
        return nullptr;
    }

    // Intervallo di retry (secondi) quando lo StarSystem non e' trovato.
    const float StarSystemSearchRetryInterval = 1.0f;
    StarSystemSearchCooldown = StarSystemSearchRetryInterval;

    TArray<AActor*> StarSystemActors;

    UGameplayStatics::GetAllActorsOfClass(
        this,
        AStarSystem::StaticClass(),
        StarSystemActors
    );

    for (
        AActor* CandidateActor :
        StarSystemActors
        )
    {
        if (AStarSystem* StarSystem = Cast<AStarSystem>(CandidateActor))
        {
            CachedStarSystem = StarSystem;
            return StarSystem;
        }
    }

    return nullptr;
}