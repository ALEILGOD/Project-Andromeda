#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sun.generated.h"

class UPointLightComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class USceneComponent;
class UTextureCube;

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

private:

    void ConfigureSunLight();
};