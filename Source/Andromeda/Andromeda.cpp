#include "Andromeda.h"
#include "Atmosphere/AndromedaAtmosphereRenderer.h"


void FAndromedaModule::StartupModule()
{
    // ATMOS-01: registers the /Andromeda -> [Project]/Shaders/Andromeda
    // virtual shader directory mapping before any global shader
    // compilation happens.
    FAndromedaAtmosphereRenderer::Initialize();
}


void FAndromedaModule::ShutdownModule()
{
    FAndromedaAtmosphereRenderer::Shutdown();
}


IMPLEMENT_PRIMARY_GAME_MODULE(
    FAndromedaModule,
    Andromeda,
    "Andromeda"
);