#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HillaireLimits.h"
#include "Sun.generated.h"

class UPointLightComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class USceneComponent;
class UTextureCube;
class UHillaireStarLinkComponent;

UCLASS()
class ANDROMEDA_API ASun : public AActor
{
    GENERATED_BODY()

public:

    ASun();

protected:

    virtual void BeginPlay() override;

    virtual void Tick(
        float DeltaTime
    ) override;

    virtual void OnConstruction(
        const FTransform& Transform
    ) override;

public:

    // =========================================================
    // ROOT
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<USceneComponent> Root;

    // =========================================================
    // VISUAL MESH
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UStaticMeshComponent> SunMesh;

    // =========================================================
    // PRIMARY SOLAR LIGHT
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UPointLightComponent> SunLight;

    // =========================================================
    // COSMIC AMBIENT / STARLIGHT FILL
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<USkyLightComponent> SpaceAmbientLight;

    // =========================================================
    // LIGHTING PARAMETERS
    // =========================================================

    /** Intensità della luce solare diretta */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightIntensity = 1.0f;

    /** Raggio di attenuazione su scala astronomica */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightAttenuationRadius = 50000000000.0f;

    /** Raggio fisico della stella */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightSourceRadius = 1000000.0f;

    /** Raggio di sfumatura morbida della sorgente */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightSoftSourceRadius = 2500000.0f;

    /**
     * Intensità del fill ambientale.
     *
     * Deve rimanere molto più debole della luce solare:
     * serve solamente a mantenere leggibile il lato notturno
     * senza trasformarlo in una superficie uniformemente illuminata.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun",
        meta = (
            ClampMin = "0.0",
            UIMin = "0.0",
            UIMax = "5.0"
            )
    )
    float AmbientIntensity = 0.35f;

    /** Tinta fredda e molto scura dello starlight */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    FLinearColor AmbientColor =
        FLinearColor(
            0.08f,
            0.11f,
            0.18f,
            1.0f
        );

    /** Cubemap ambientale per il fill a 360 gradi */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UTextureCube> AmbientCubemap;

    /**
     * Builds the neutral ambient carrier: a 1x1 white cube used when the
     * authored AmbientCubemap is missing or empty (the referenced engine
     * DefaultTextureCube loads 0x0: a specified-cubemap skylight sampling an
     * empty cube contributes nothing at any intensity, which killed the fill
     * entirely). The cube is intentionally uniform: all hue comes from the
     * pushed light color (hemisphere sky transfer x sun chromaticity).
     */
    static UTextureCube* BuildNeutralAmbientCube(
        UObject* InOuter
    );

    /** True when a cubemap can actually feed a specified-cubemap skylight. */
    static bool IsUsableAmbientCube(
        const UTextureCube* Cube
    );

    /**
     * Transfer -> skylight intensity gain for the dynamic atmospheric
     * ambient (see HillaireLimits::SkyAmbientLightState). The ambient color
     * itself comes from the hemisphere-integrated sky transfer (same hue
     * family as the visible sky, symmetric sunrise/sunset, zero at night);
     * this scale only sets how strongly it fills relative to direct light.
     * Calibrated so day ambient sits ~10-15% under direct illumination.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun",
        meta = (
            ClampMin = "0.0",
            UIMin = "0.0",
            UIMax = "10.0"
            )
    )
    float SkyAmbientScale = HillaireLimits::SkyAmbientPresentationScale;

    /**
     * Stable star identity for this sun, derived from its star system.
     * Set once at spawn time by the owning AStarSystem.
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    FGuid StableStarId;

    // =========================================================
    // HILLAIRE STAR LINK (ATMOS WIRING)
    // =========================================================

    /**
     * Feeds this primary star into the Hillaire light feed (Phase 2E).
     * Constructor-owned: every generated ASun (the actor returned by
     * AStarSystem::GetSunActor) automatically carries exactly one.
     * Plain ActorComponent: never attached to any mesh.
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UHillaireStarLinkComponent> HillaireStarLink;

private:

    void ConfigureSunLight();

    void ConfigureSunMesh();

    /** Runtime neutral carrier (transient, instance-only, never on templates). */
    UPROPERTY(
        Transient
    )
    TObjectPtr<UTextureCube> NeutralAmbientCube;

    /** Builds NeutralAmbientCube on demand (idempotent, instance-only). */
    void EnsureNeutralAmbientCube();

    /** Binds the selected ambient cube to SpaceAmbientLight (see selection rule above). */
    void ApplyAmbientCube();

    /**
     * Dynamic atmospheric ambient: pushes the governing planet's
     * hemisphere-integrated sky transfer into the existing
     * SpaceAmbientLight (color + intensity, cubemap kept, no recapture).
     * Falls back to the authored static fill when no atmospheric ambient
     * is available (deep space / no governing planet). Change-thresholded
     * so render state is only dirtied on real updates.
     */
    void PushSkyAmbient();

    bool bSkyAmbientActive = false;
    FLinearColor LastPushedAmbientColor = FLinearColor::Black;
    float LastPushedAmbientIntensity = -1.0f;
};