#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sun.generated.h"

class UPointLightComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class USceneComponent;

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

    /** Intensità della luce solare diretta (in lux/candela con falloff normalizzato) */
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

    /** Raggio fisico della stella per il calcolo della penombra sferica */
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

    /** Intensità della luce cosmica ambientale (starlight / radiazione di fondo) */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float AmbientIntensity = 0.015f;

    /** Tinta cromatica dello spazio profondo per il lato notturno */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    FLinearColor AmbientColor = FLinearColor(0.04f, 0.05f, 0.08f, 1.0f);

private:

    void ConfigureSunLight();
};