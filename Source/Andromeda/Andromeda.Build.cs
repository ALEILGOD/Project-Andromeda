using UnrealBuildTool;
using System.IO;

public class Andromeda : ModuleRules
{
    public Andromeda(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "ProceduralMeshComponent"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "RenderCore",
                "RHI",
                "Renderer"
            }
        );

        PrivateIncludePaths.Add(
    Path.Combine(
        EngineDirectory,
        "Source/Runtime/Renderer/Public"
    )
);

        PrivateIncludePaths.Add(
            Path.Combine(
                EngineDirectory,
                "Source/Runtime/Renderer/Internal"
            )
        );


    }
}