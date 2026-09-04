#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PlanetLandformGenerator.generated.h"

UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetLandformGenerator : public UObject
{
    GENERATED_BODY()

public:

    // ========================================================================
    // OROGENIC BELT & MASK ARCHITECTURE
    // ========================================================================

    /**
     * Maschera della cintura orogenica primaria.
     * Definisce le grandi zone tettoniche e di sollevamento su scala planetaria.
     *
     * 0.0 = pianura / scudo continentale stabile
     * 1.0 = asse di sollevamento orogenico massimo
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Orogeny")
    static float GetOrogenicBeltMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera del nucleo montuoso (Mountain Core).
     * Derivata direttamente dalla cintura orogenica; rappresenta le vette e i massicci principali.
     *
     * 0.0 = assenza di massicci montuosi
     * 1.0 = cuore del massiccio montuoso
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Orogeny")
    static float GetCorrelatedMountainMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera dei contrafforti e fasce pedemontane (Foothills).
     * Si estende naturalmente attorno al nucleo montuoso all'interno della cintura orogenica.
     *
     * 0.0 = pianura aperta
     * 1.0 = contrafforte montuoso / collina pedemontana
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Orogeny")
    static float GetCorrelatedFoothillMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera delle colline regionali interne (Regional Hills).
     * Distribuisce province collinari e altopiani all'interno della massa continentale,
     * impedendo la formazione di pianure vuote ed eccessivamente estese.
     *
     * 0.0 = pianura o bacino
     * 1.0 = provincia collinare / altopiano interno
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetRegionalHillMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );


    // ========================================================================
    // COMPATIBILITY API
    // ========================================================================

    /**
     * Distribuzione generale delle forme del terreno.
     * Copre sia le cinture orogeniche montuose sia le province collinari interne,
     * lasciando alle pianure le sole aree di bacino e pianure costiere.
     *
     * 0.0 = pianura / bacino
     * 1.0 = territorio montuoso o collinare
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetLandformMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera complessiva delle colline.
     * Unisce i contrafforti orogenici (foothills) e le colline regionali interne.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetHillMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera delle montagne.
     * Mappa sul nucleo montuoso correlato (mountain core).
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetMountainMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );

    /**
     * Maschera delle catene montuose.
     * Produce strutture allungate e continue lungo la spina orogenica.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Terrain|Landforms")
    static float GetMountainChainMask(
        FVector Direction,
        int64 Seed,
        float Scale
    );
};