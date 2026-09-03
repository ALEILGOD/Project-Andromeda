#include "Andromeda.h"

#include "UASViewExtension.h"

#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"

void FAndromedaModule::StartupModule()
{
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS TEST] StartupModule")
    );

    const FString ShaderDirectory =
        FPaths::Combine(
            FPaths::ProjectDir(),
            TEXT("Shaders")
        );

    AddShaderSourceDirectoryMapping(
        TEXT("/Andromeda"),
        ShaderDirectory
    );

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS TEST] Shader directory mapped: %s"),
        *ShaderDirectory
    );

    FCoreDelegates::OnPostEngineInit.AddLambda(
        [this]()
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[UAS TEST] Creating View Extension AFTER ENGINE INIT")
            );

            UASViewExtension =
                FSceneViewExtensions::NewExtension<
                FUASViewExtension
                >();

            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "[UAS TEST] View Extension IsValid = %s"
                ),
                UASViewExtension.IsValid()
                ? TEXT("TRUE")
                : TEXT("FALSE")
            );
        }
    );
}

void FAndromedaModule::ShutdownModule()
{
    UASViewExtension.Reset();

    ResetAllShaderSourceDirectoryMappings();
}

IMPLEMENT_PRIMARY_GAME_MODULE(
    FAndromedaModule,
    Andromeda,
    "Andromeda"
);