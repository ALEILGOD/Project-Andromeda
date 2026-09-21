#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HillaireLightSource.h"
#include "HillaireStarLinkComponent.generated.h"


class AStarSystem;
class ASun;
class UPlanetaryLightingComponent;


/**
 * STARMAP -> HILLAIRE primary-star link (Multiplanetary rebuild).
 *
 * Owned by ASun itself (constructor subobject): every generated primary
 * star carries exactly one. Every tick it pushes ONE directional light POD
 * into the Hillaire planetary atmosphere subsystem for its associated planet(s).
 *
 * The relationship is: Star -> PlanetarySystem -> PlanetAtmosphereState
 * Each planet knows its StarId and resolves its star direction locally.
 *
 * Validated Hillaire scattering math is untouched; the subsystem prepends
 * the external star feed so SkyView, aerial, and composite all consume it.
 */
UCLASS(
    ClassGroup = (Andromeda),
    meta = (BlueprintSpawnableComponent)
)
class ANDROMEDA_API UHillaireStarLinkComponent : public UActorComponent
{
    GENERATED_BODY()


public:

    UHillaireStarLinkComponent();


protected:

    virtual void BeginPlay() override;

    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason
    ) override;


public:

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;


    // =========================================================
    // STAR REFERENCE
    // =========================================================

    /** Explicit star system. When null and bAutoFindStarSystem is set, the first AStarSystem in the world is used. */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Hillaire Link"
    )
    TObjectPtr<AStarSystem> StarSystemActor = nullptr;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Hillaire Link"
    )
    bool bAutoFindStarSystem = true;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Hillaire Link"
    )
    FName StarLightName = TEXT("PrimaryStar");


    // =========================================================
    // STAR LIGHT BUILD (PURE)
    // =========================================================

    /**
     * Build the directional primary-star POD from explicit inputs.
     * No world access: the star world position is kept for traceability.
     */
    static FHillaireLightSource MakeStarLightSource(
        const FVector& StarWorldPosCm,
        const FVector& DirectionToStarWorld,
        const FLinearColor& StarColor,
        float EffectiveIntensity,
        const FGuid& LightId,
        const FName& LightName
    );


    /** Well-known shared slot id for the primary star (same on every link). */
    static FGuid GetPrimaryStarSlotId();


    /**
     * Resolve the current primary star and push it into the Hillaire
     * planetary atmosphere subsystem. Returns false when no star is available.
     */
    bool PushStarLight();


private:

    AStarSystem* ResolveStarSystem() const;

    /** Sun-owned path: owner ASun + its star system drive the star feed. */
    bool PushStarLightFromSun(
        ASun* OwnerSun,
        UWorld* World
    );

    /** Legacy planet-attached path (for backward compat). */
    bool PushStarLightFromPlanetActor(
        const AActor* PlanetActor,
        ASun* Sun,
        UWorld* World
    );
};