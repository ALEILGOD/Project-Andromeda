#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sun.generated.h"


class UPointLightComponent;
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
    // SUN MESH
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UStaticMeshComponent> SunMesh;


    // =========================================================
    // SUN LIGHT
    // =========================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UPointLightComponent> SunLight;


    // =========================================================
    // LIGHT PARAMETERS
    // =========================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightIntensity = 1000000000000000.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightAttenuationRadius = 500000000.0f;


private:

    void ConfigureSunLight();
};