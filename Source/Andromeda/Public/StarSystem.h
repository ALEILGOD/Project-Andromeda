#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarSystemGenerator.h"
#include "StarSystem.generated.h"


USTRUCT()
struct FSpawnedPlanetData
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<AActor> PlanetActor = nullptr;

    UPROPERTY()
    FPlanetGenerationData GenerationData;
};


// =========================================================
// PLANET RUNTIME DATA
//
// Snapshot read-only di un pianeta spawnato, utilizzato dal
// PlanetaryGravitySystem e dal futuro sistema di reference
// frame. E' un puro blocco di dati: non contiene logica.
// =========================================================

USTRUCT(BlueprintType)
struct FPlanetRuntimeData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    bool bValid = false;

    /** Riferimento runtime all'actor del pianeta (l'APlanet/BP_Planet spawnato). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    TObjectPtr<AActor> PlanetActor = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    int64 PlanetID = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    int64 PlanetSeed = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    float PlanetRadius = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    float TerrainHeight = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    float OrbitDistance = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    float OrbitalPeriod = 0.0f;

    /** Posizione mondiale corrente del centro del pianeta. */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    FVector WorldPosition = FVector::ZeroVector;

    /**
     * Velocità orbitale EFFETTIVA del pianeta (cm/s, tempo mondiale).
     *
     * Deriva dalla stessa formula e dalla stessa scala temporale usate da
     * UpdatePlanetOrbits (SystemSimulationTime * OrbitTimeScale), quindi
     * rappresenta il movimento che il pianeta ha DAVVERO nel mondo.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    FVector OrbitalVelocity = FVector::ZeroVector;

    /** Rotazione mondiale corrente del pianeta (stesso valore applicato dall'orbital system). */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    FRotator CurrentRotation = FRotator::ZeroRotator;

    /**
     * Velocità angolare di rotazione EFFETTIVA (gradi/s, tempo mondiale).
     *
     * La rotazione usa SystemSimulationTime pieno (vedi UpdatePlanetRotations),
     * quindi il rate include SimulationTimeScale.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    float RotationRateDegreesPerSecond = 0.0f;

    /**
     * Asse di rotazione nel mondo (unitario; il segno codifica la direzione:
     * +1 prograde, -1 retrogrado).
     */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Star System|Planet")
    FVector RotationAxis = FVector::ZeroVector;
};


UCLASS()
class ANDROMEDA_API AStarSystem : public AActor
{
    GENERATED_BODY()


public:

    AStarSystem();


protected:

    virtual void BeginPlay() override;

    virtual void Tick(
        float DeltaTime
    ) override;


public:

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    int64 UniverseSeed = 1234567890123456;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    FAndromedaInt64Vector SystemCoordinate;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    TSubclassOf<AActor> PlanetClass;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    TSubclassOf<AActor> SunClass;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    float SimulationTimeScale = 1.0f;


    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Andromeda|Star System"
    )
    float OrbitTimeScale = 0.05f;


    UPROPERTY(
        BlueprintReadOnly,
        Category = "Andromeda|Star System"
    )
    FStarSystemData SystemData;


    // =========================================================
    // PLANET QUERIES (READ-ONLY)
    //
    // API pubblica pensata per il PlanetaryGravitySystem e per il
    // futuro sistema di reference frame. Leggono lo stato esistente
    // senza modificare in alcun modo la simulazione orbitale.
    // =========================================================

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System|Planet Query")
    int32 GetPlanetCount() const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System|Planet Query")
    FPlanetRuntimeData GetPlanetRuntimeData(
        int64 PlanetID
    ) const;

    UFUNCTION(BlueprintCallable, Category = "Andromeda|Star System|Planet Query")
    int32 GetAllPlanetRuntimeData(
        TArray<FPlanetRuntimeData>& OutPlanets
    ) const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System|Planet Query")
    FVector GetPlanetWorldPosition(
        int64 PlanetID
    ) const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System|Planet Query")
    FVector GetPlanetOrbitalVelocity(
        int64 PlanetID
    ) const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System|Planet Query")
    FRotator GetPlanetRotation(
        int64 PlanetID
    ) const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System|Planet Query")
    float GetPlanetRotationRateDegreesPerSecond(
        int64 PlanetID
    ) const;

    UFUNCTION(BlueprintPure, Category = "Andromeda|Star System|Planet Query")
    FVector GetPlanetRotationAxis(
        int64 PlanetID
    ) const;

    /** Riferimento runtime al pianeta spawnato (API C++; l'actor referenziato e' l'APlanet/BP_Planet). */
    AActor* GetPlanetActor(
        int64 PlanetID
    ) const;


private:

    // =========================================================
    // SPAWN
    // =========================================================

    void SpawnSun();

    void SpawnPlanets();


    // =========================================================
    // SIMULATION
    // =========================================================

    void UpdatePlanetOrbits(
        float DeltaTime
    );

    void UpdatePlanetRotations(
        float DeltaTime
    );


    // =========================================================
    // ORBIT
    // =========================================================

    FVector CalculateOrbitPosition(
        const FPlanetGenerationData& PlanetData,
        float SimulationTime
    ) const;


    // =========================================================
    // ROTATION
    // =========================================================

    FRotator CalculatePlanetRotation(
        const FPlanetGenerationData& PlanetData,
        float SimulationTime
    ) const;

    float CalculateRotationPeriod(
        const FPlanetGenerationData& PlanetData
    ) const;

    float CalculateAxialTilt(
        const FPlanetGenerationData& PlanetData
    ) const;

    float CalculateInitialRotation(
        const FPlanetGenerationData& PlanetData
    ) const;

    float CalculateRotationDirection(
        const FPlanetGenerationData& PlanetData
    ) const;


    // =========================================================
    // PLANET QUERY HELPERS
    // =========================================================

    const FSpawnedPlanetData* FindSpawnedPlanet(
        int64 PlanetID
    ) const;

    FPlanetRuntimeData BuildPlanetRuntimeData(
        const FSpawnedPlanetData& SpawnedPlanet
    ) const;


    // =========================================================
    // PLANET DATA
    // =========================================================

    bool SetPlanetGenerationData(
        AActor* PlanetActor,
        const FPlanetGenerationData& PlanetData
    );


private:

    // =========================================================
    // SPAWNED STAR
    // =========================================================

    UPROPERTY(Transient)
    TObjectPtr<AActor> SpawnedSun = nullptr;


    // =========================================================
    // SPAWNED PLANETS
    // =========================================================

    UPROPERTY(Transient)
    TArray<FSpawnedPlanetData> SpawnedPlanets;


    // =========================================================
    // SIMULATION TIME
    // =========================================================

    float SystemSimulationTime = 0.0f;
};