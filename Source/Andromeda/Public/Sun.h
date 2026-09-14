#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sun.generated.h"

class UPointLightComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class USceneComponent;
class UTextureCube;
class UAtmosphereLightReferenceComponent;

UCLASS()
class ANDROMEDA_API ASun : public AActor
{
    GENERATED_BODY()

public:

    ASun();

protected:

    virtual void BeginPlay() override;

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
    // ATMOSPHERE LIGHT REFERENCE (SUN LIGHT REFERENCE)
    // =========================================================
    // Authoritative light-direction reference for the unified
    // atmosphere (UAtmosphereLightReferenceComponent).
    // "The physical direction from which sunlight comes"
    // (Planet -> Sun, world frame). The atmosphere NEVER
    // reconstructs the sun direction on its own: the Registry
    // queries this reference per planet on the game thread and
    // bakes the result into the snapshot. Not a second sun, not
    // a second light source: no lighting, no simulation state.
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UAtmosphereLightReferenceComponent> AtmosphereLightReference;

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
     * Nasconde la mesh visiva della stella (default: nascosta).
     *
     * MOTIVAZIONE (ZEPHYR-01 white-sphere fix): la SunMesh e' una
     * sfera opaca di raggio fisso nel mondo (asset Sphere_6510340D,
     * ~50 cm). Un raggio fisso non puo' mai rappresentare
     * correttamente una stella: da vicino riempie la vista come
     * una "sfera bianca" priva di senso fisico (il PlayerStart di
     * Andromeda_Main si trova a ~52 cm dal centro del sole!),
     * da lontano diventa un puntino arbitrario. Il visuale
     * corretto della stella (disco angolare da 0.53 gradi +
     * alone Mie, attenuato dalla transmittance atmosferica) e'
     * prodotto da ZEPHYR per ogni camera e distanza.
     * La posizione della stella, le luci (SunLight, SkyLight) e
     * il trasporto radiativo restano invariati: cambia solo la
     * mesh decorativa. Precedente: AtmosphereMesh di APlanet,
     * nascosta per lo stesso motivo (i sistemi volumetrici
     * possiedono il look). Disabilitare qui per ripristinare
     * la mesh legacy (sconsigliato).
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    bool bHideSunMeshForZephyrSky = true;

private:

    void ConfigureSunLight();

    void ConfigureSunMesh();
};