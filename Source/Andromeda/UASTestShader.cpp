#include "UASTestShader.h"

IMPLEMENT_GLOBAL_SHADER(
    FUASTestVertexShader,
    "/Andromeda/UAS/UAS_Test.usf",
    "MainVS",
    SF_Vertex
);

IMPLEMENT_GLOBAL_SHADER(
    FUASTestPixelShader,
    "/Andromeda/UAS/UAS_Test.usf",
    "MainPS",
    SF_Pixel
);