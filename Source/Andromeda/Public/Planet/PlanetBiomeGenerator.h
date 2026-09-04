#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Planet/PlanetSurfaceData.h"
#include "PlanetBiomeGenerator.generated.h"

/**
 * Generatore matematico del Planet Biome System (PBS) per Beyond The Skies.
 * Classifica in modo deterministico, continuo e senza discontinuità i biomi
 * della superficie planetaria in funzione di latitudine, altitudine, pendenza,
 * temperatura, umidità e prossimità oceanica.
 */
UCLASS(BlueprintType)
class ANDROMEDA_API UPlanetBiomeGenerator : public UObject
{
    GENERATED_BODY()

public:

    /**
     * Calcola la pendenza (slope) tra la direzione radiale e la normale di superficie.
     * 0.0 = piano perfettamente orizzontale
     * 1.0 = parete verticale
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Biome")
    static float CalculateSlope(
        FVector Direction,
        FVector SurfaceNormal
    );

    /**
     * Calcola la temperatura locale [0.0 = polare/gelido, 1.0 = equatoriale/torrido].
     * Combina insolazione latitudinale, gradiente termico verticale (lapse rate)
     * e perturbazioni climatiche deterministiche.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Biome")
    static float CalculateTemperature(
        FVector Direction,
        float NormalizedHeight,
        int64 Seed
    );

    /**
     * Calcola l'umidità / precipitazioni locali [0.0 = arido/deserto, 1.0 = piogge torrenziali].
     * Modella le celle di circolazione planetarie (ITCZ equatoriale, deserti subtropicali,
     * perturbazioni temperate) e variazioni locali deterministiche.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Biome")
    static float CalculateHumidity(
        FVector Direction,
        float NormalizedHeight,
        int64 Seed
    );

    /**
     * Valuta e classifica i biomi per un punto della superficie planetaria.
     * Restituisce i dati climatici completi, il bioma primario, secondario e
     * il fattore di transizione continua (soft blend) per eliminare confini netti.
     */
    UFUNCTION(BlueprintPure, Category = "Andromeda|Biome")
    static FPlanetBiomeData CalculateBiome(
        FVector Direction,
        float NormalizedHeight,
        FVector SurfaceNormal,
        int64 Seed
    );
};