using UnrealBuildTool;

public class HillaireAtmosphere : ModuleRules
{
	public HillaireAtmosphere(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Phase 2B: ImageWrapper for LUT validation PNG export (debug path).
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RHI",
				"RenderCore",
				"Renderer",
				"Projects",
				"ImageWrapper"
			}
		);
	}
}
