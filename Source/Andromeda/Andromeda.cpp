#include "Andromeda.h"
#include "Atmosphere/AndromedaAtmosphereRenderer.h"
#include "Atmosphere/AndromedaUnifiedAtmosphereRenderer.h"
#include "Planet/Zephyr/ZephyrRenderer.h"


void FAndromedaModule::StartupModule()
{
    // PHASE 2.1 — ONE atmosphere system, ONE renderer.
    //
    // ATMOS-01: registers the /Andromeda -> [Project]/Shaders/Andromeda
    // virtual shader directory mapping before any global shader
    // compilation happens. Owns the aerial-stage pixel shader.
    FAndromedaAtmosphereRenderer::Initialize();

    // ZEPHYR LUT pipeline (Transmittance / MultiScatter / SkyView /
    // Sky). No longer owns a Tonemap hook: it runs as the sky stage
    // of the unified renderer over the unified snapshot.
    FZephyrRenderer::Initialize();

    // Single Tonemap owner (Aerial -> Sky), single mailbox consumer,
    // single runtime-proof state. Registered strictly AFTER the two
    // stage initializers above.
    FUnifiedAtmosphereRenderer::Initialize();

    // NOTE: the unified console namespace (r.AndromedaAtmosphere.*)
    // needs no explicit init: AndromedaAtmosphereUnifiedCommands.cpp
    // registers it via static FAutoConsoleCommand/TAutoConsoleVariable.
}


void FAndromedaModule::ShutdownModule()
{
    FUnifiedAtmosphereRenderer::Shutdown();

    FZephyrRenderer::Shutdown();

    FAndromedaAtmosphereRenderer::Shutdown();
}


IMPLEMENT_PRIMARY_GAME_MODULE(
    FAndromedaModule,
    Andromeda,
    "Andromeda"
);
