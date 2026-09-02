#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sun.generated.h"

UCLASS()
class ANDROMEDA_API ASun : public AActor
{
    GENERATED_BODY()

public:

    ASun();

protected:

    virtual void BeginPlay() override;

public:

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Sun")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Sun")
    TObjectPtr<UStaticMeshComponent> SunMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Andromeda|Sun")
    TObjectPtr<class UPointLightComponent> SunLight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Sun")
    float LightIntensity = 500000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Sun")
    float LightAttenuationRadius = 100000000.0f;

private:

    void ConfigureSunLight();
};