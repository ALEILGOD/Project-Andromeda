using UnrealBuildTool;

public class Andromeda : ModuleRules
{
    public Andromeda(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "ProceduralMeshComponent",
                "RHI",
                "RenderCore",
                "Renderer"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Projects",
                "HillaireAtmosphere"
            }
        );
    }
}