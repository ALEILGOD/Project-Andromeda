#pragma once

#include "CoreMinimal.h"
#include "PlanetSurfaceData.generated.h"

/**
 * Biomi planetari supportati dal Planet Biome System (PBS).
 */
UENUM(BlueprintType)
enum class EPlanetBiome : uint8
{
    Ocean        UMETA(DisplayName = "Ocean"),
    Beach        UMETA(DisplayName = "Beach"),
    Plains       UMETA(DisplayName = "Plains"),
    Grassland    UMETA(DisplayName = "Grassland"),
    Forest       UMETA(DisplayName = "Forest"),
    Desert       UMETA(DisplayName = "Desert"),
    Tundra       UMETA(DisplayName = "Tundra"),
    Snow         UMETA(DisplayName = "Snow"),
    Mountain     UMETA(DisplayName = "Mountain")
};

/**
 * Dati climatici e di classificazione bioma generati dal PBS per un punto della superficie.
 */
USTRUCT(BlueprintType)
struct FPlanetBiomeData
{
    GENERATED_BODY()

    /** Bioma primario predominante */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Biome")
    EPlanetBiome PrimaryBiome = EPlanetBiome::Ocean;

    /** Bioma secondario per transizioni fluide (soft blending) */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Biome")
    EPlanetBiome SecondaryBiome = EPlanetBiome::Ocean;

    /** Fattore di transizione verso il bioma secondario (0.0 = 100% primario, 0.5 = transizione paritaria) */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Biome")
    float BiomeBlend = 0.0f;

    /** Temperatura locale normalizzata [0.0 = polare/gelido, 1.0 = equatoriale/torrido] */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Biome")
    float Temperature = 0.0f;

    /** Umidità locale normalizzata [0.0 = arido/deserto, 1.0 = piogge torrenziali] */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Biome")
    float Humidity = 0.0f;

    /** Pendenza locale della superficie [0.0 = orizzontale/piatto, 1.0 = scogliera verticale] */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Biome")
    float Slope = 0.0f;

    /** Latitudine normalizzata [0.0 = equatore, 1.0 = poli] */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Biome")
    float Latitude = 0.0f;
};

/**
 * Dati geometrici, altimetrici e biologici di un punto sulla superficie planetaria.
 */
USTRUCT(BlueprintType)
struct FPlanetSurfaceData
{
    GENERATED_BODY()

    /** Direzione radiale unitaria dal centro del pianeta */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Surface")
    FVector Direction = FVector::ZeroVector;

    /** Elevazione assoluta in centimetri sul raggio base */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Surface")
    float Height = 0.0f;

    /** Elevazione normalizzata adimensionale (-0.1 = fondali oceanici, 0.0 = costa, >0 = terra emersa) */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Surface")
    float NormalizedHeight = 0.0f;

    /** Normale geometrica della superficie */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Surface")
    FVector Normal = FVector::UpVector;

    /** Pendenza locale (0.0 = piatto, 1.0 = parete verticale) */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Surface")
    float Slope = 0.0f;

    /** Parametri climatici e classificazione del bioma (PBS) */
    UPROPERTY(BlueprintReadOnly, Category = "Andromeda|Surface")
    FPlanetBiomeData BiomeData;
};