#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PlanetProfile.generated.h"

/**
 * Archetipi planetari globali per PBS v2 - UNKNOWN WORLDS.
 * Definisce l'identita macroscopica e le tendenze ambientali del pianeta.
 */
UENUM(BlueprintType)
enum class EPlanetArchetype : uint8
{
    Terran      UMETA(DisplayName = "Terran"),
    Arid        UMETA(DisplayName = "Arid"),
    Desert      UMETA(DisplayName = "Desert"),
    Oceanic     UMETA(DisplayName = "Oceanic"),
    Frozen      UMETA(DisplayName = "Frozen"),
    Tundra      UMETA(DisplayName = "Tundra"),
    Jungle      UMETA(DisplayName = "Jungle"),
    Volcanic    UMETA(DisplayName = "Volcanic"),
    Rocky       UMETA(DisplayName = "Rocky"),
    Exotic      UMETA(DisplayName = "Exotic")
};

/**
 * Bias e affinita specifiche per i singoli biomi all'interno di un profilo planetario.
 */
USTRUCT(BlueprintType)
struct FPlanetBiomeBiases
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float PlainsBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float GrasslandBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float ForestBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float DesertBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float TundraBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float MountainBias = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float SnowBias = 0.0f;
};

/**
 * Profilo generato di un singolo pianeta.
 * Contiene i parametri e i bias bioclimatici derivati dall'archetipo e dai parametri orbitali.
 */
USTRUCT(BlueprintType)
struct FPlanetProfile
{
    GENERATED_BODY()

    /** Archetipo planetario assegnato */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile")
    EPlanetArchetype Archetype = EPlanetArchetype::Terran;

    /** Nome descrittivo dell'archetipo */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile")
    FString ArchetypeName = TEXT("Terran");

    /** Percentuale globale di copertura dell'acqua [0.30 - 0.70] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Water")
    float WaterCoverage = 0.55f;

    /** Limite minimo di copertura dell'acqua per l'archetipo */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Water")
    float WaterCoverageMin = 0.30f;

    /** Limite massimo di copertura dell'acqua per l'archetipo */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Water")
    float WaterCoverageMax = 0.70f;

    /** Bias globale di temperatura [-0.5 = gelido, +0.5 = torrido] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Climate")
    float TemperatureBias = 0.0f;

    /** Bias globale di umidita [-0.5 = arido, +0.5 = piovoso] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Climate")
    float HumidityBias = 0.0f;

    /** Indica se la neve e ammessa sul pianeta */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Snow")
    bool bSnowAllowed = true;

    /** Potenziale di innevamento e persistenza dei ghiacci [0.0 - 1.0] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Snow")
    float SnowPotential = 0.40f;

    /** Grado di biodiversita biologica [0.0 = sterile/desolato, 1.0 = lussureggiante] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Ecology")
    float Biodiversity = 0.85f;

    /** Varieta e ricchezza di biomi differenti sul pianeta [0.0 - 1.0] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Ecology")
    float BiomeDiversity = 0.80f;

    /** Bias e affinita per singoli biomi */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    FPlanetBiomeBiases BiomeBiases;

    /** Probabilita di generazione di biomi speciali o anomalie esotiche [0.0 - 1.0] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Biome")
    float SpecialBiomeAvailability = 0.10f;

    /** ID planetario all'interno del sistema stellare */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Identity")
    int64 PlanetID = 0;

    /** Seed univoco del pianeta */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Identity")
    int64 PlanetSeed = 0;

    /** Distanza orbitale dal centro della stella in cm */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Andromeda|Profile|Orbit")
    float OrbitDistance = 0.0f;
};

/**
 * Generatore deterministico per Archetipi e Profili Planetari (PBS v2).
 */
UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetProfileGenerator : public UObject
{
    GENERATED_BODY()

public:

    /**
     * Determina deterministicamente l'archetipo planetario a partire da seed, ID e distanza orbitale.
     * La distanza orbitale agisce come bias probabilistico (non come vincolo rigido).
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Planet|Profile")
    static EPlanetArchetype DetermineArchetype(
        int64 PlanetSeed,
        int64 PlanetID,
        float OrbitDistance
    );

    /**
     * Genera un profilo planetario completo, deterministico e coerente per il pianeta specificato.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Planet|Profile")
    static FPlanetProfile GenerateProfile(
        int64 PlanetSeed,
        int64 PlanetID,
        float OrbitDistance
    );

    /**
     * Restituisce la configurazione base di default per un determinato archetipo planetario.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Planet|Profile")
    static FPlanetProfile GetArchetypeDefaultProfile(
        EPlanetArchetype Archetype
    );

    /**
     * Restituisce la stringa del nome dell'archetipo.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Planet|Profile")
    static FString GetArchetypeName(
        EPlanetArchetype Archetype
    );
};
