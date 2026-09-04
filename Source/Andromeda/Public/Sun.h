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

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UStaticMeshComponent> SunMesh;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Andromeda|Sun"
    )
    TObjectPtr<UPointLightComponent> SunLight;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightIntensity = 5000000000000.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Sun"
    )
    float LightAttenuationRadius = 50000000000.0f;
private:

    void ConfigureSunLight();
};