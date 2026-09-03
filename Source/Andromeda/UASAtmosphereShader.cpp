#include "UASAtmosphereShader.h"

#include "ShaderCore.h"

IMPLEMENT_GLOBAL_SHADER(
    FUASAtmosphereVertexShader,
    "/Project/Andromeda/UAS/UASAtmosphere.usf",
    "MainVS",
    SF_Vertex
);

IMPLEMENT_GLOBAL_SHADER(
    FUASAtmospherePixelShader,
    "/Project/Andromeda/UAS/UASAtmosphere.usf",
    "MainPS",
    SF_Pixel
);