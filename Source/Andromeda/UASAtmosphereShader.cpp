#include "UASAtmosphereShader.h"

#include "ShaderCore.h"


IMPLEMENT_GLOBAL_SHADER(
    FUASAtmosphereShader,
    "/Project/Andromeda/UAS/UASAtmosphere.usf",
    "MainVS",
    SF_Vertex
);