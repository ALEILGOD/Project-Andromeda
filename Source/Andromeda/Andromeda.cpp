#include "Andromeda.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Zephyr/ZephyrLog.h"

DEFINE_LOG_CATEGORY(LogZephyr);

void FAndromedaModule::StartupModule()
{
	// ZEPHYR presentation shaders: /Andromeda/Zephyr/ZephyrPresentation.usf.
	const FString ZephyrShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders/Andromeda"));
	AddShaderSourceDirectoryMapping(TEXT("/Andromeda"), ZephyrShaderDir);
	UE_LOG(LogZephyr, Log, TEXT("[Zephyr] Shader directory mapped: %s"), *ZephyrShaderDir);
}


void FAndromedaModule::ShutdownModule()
{
}


IMPLEMENT_PRIMARY_GAME_MODULE(
    FAndromedaModule,
    Andromeda,
    "Andromeda"
);
