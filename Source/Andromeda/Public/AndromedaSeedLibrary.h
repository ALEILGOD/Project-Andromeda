#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AndromedaSeedLibrary.generated.h"

UCLASS()
class ANDROMEDA_API UAndromedaSeedLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintPure, Category = "Andromeda|Seed")
    static int64 HashSeed(
        int64 Seed
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Seed")
    static int64 CombineSeeds(
        int64 ParentSeed,
        int64 ChildID
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Seed")
    static int64 GenerateGalaxySeed(
        int64 UniverseSeed,
        int64 GalaxyX,
        int64 GalaxyY,
        int64 GalaxyZ
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Seed")
    static int64 GenerateSystemSeed(
        int64 GalaxySeed,
        int64 SystemX,
        int64 SystemY,
        int64 SystemZ
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Seed")
    static int64 GeneratePlanetSeed(
        int64 SystemSeed,
        int64 PlanetID
    );

    UFUNCTION(BlueprintPure, Category = "Andromeda|Seed")
    static int64 GenerateSubsystemSeed(
        int64 PlanetSeed,
        int64 SubsystemID
    );
};