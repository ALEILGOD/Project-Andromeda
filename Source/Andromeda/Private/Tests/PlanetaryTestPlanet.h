#pragma once

#include "Planet/Planet.h"
#include "PlanetaryTestPlanet.generated.h"

/** Small real collision meshes for headless integration tests. No alternate terrain logic. */
UCLASS(Transient, NotBlueprintable)
class APlanetaryTestPlanet : public APlanet
{
    GENERATED_BODY()
public:
    APlanetaryTestPlanet() { Resolution = 12; }
};
