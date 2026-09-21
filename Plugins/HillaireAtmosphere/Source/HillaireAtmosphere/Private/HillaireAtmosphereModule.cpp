#include "Modules/ModuleManager.h"

#include "HillaireAtmosphereLog.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogHillaireAtmosphere);

/**
 * HILLAIRE ATMOSPHERE - MODULE (Phase 1).
 * Registers the /Plugin/HillaireAtmosphere shader directory mapping so the
 * .ush/.usf sources resolve through the UE5.8 global-shader pipeline.
 */
class FHillaireAtmosphereModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HillaireAtmosphere"));
		if (Plugin.IsValid())
		{
			const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/HillaireAtmosphere"), ShaderDir);
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("Shader directory mapped: %s"), *ShaderDir);
		}
		else
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Plugin handle not found; shader mapping skipped."));
		}
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FHillaireAtmosphereModule, HillaireAtmosphere)
