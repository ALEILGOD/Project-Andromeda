#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarSystemGenerator.h"
#include "StarSystem.generated.h"

UCLASS()
class ANDROMEDA_API AStarSystem : public AActor
{
    GENERATED_BODY()

public:

    AStarSystem();

protected:

    virtual void BeginPlay() override;

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Star System")
    int64 UniverseSeed = 1234567890123456;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Star System")
    FAndromedaInt64Vector SystemCoordinate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Star System")
    TSubclassOf<AActor> PlanetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Star System")
    TSubclassOf<AActor> SunClass;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System")
    FStarSystemData SystemData;

private:

	void SpawnSun();
    void SpawnPlanets();
    bool SetPlanetSeed(AActor* PlanetActor, int64 PlanetSeed);
};