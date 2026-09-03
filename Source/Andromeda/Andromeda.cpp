// Fill out your copyright notice in the Description page of Project Settings.

#include "Andromeda.h"

#include "UASAtmosphereViewExtension.h"

#include "Misc/Paths.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"

void FAndromedaModule::StartupModule()
{
    // ========================================================
    // UAS SHADER DIRECTORY
    // ========================================================

    const FString ShaderDirectory =
        FPaths::Combine(
            FPaths::ProjectDir(),
            TEXT("Shaders")
        );

    AddShaderSourceDirectoryMapping(
        TEXT("/Project/Andromeda"),
        ShaderDirectory
    );

    // ========================================================
    // UAS VIEW EXTENSION
    // ========================================================

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS] Creating View Extension")
    );

    UASViewExtension =
        FSceneViewExtensions::NewExtension<
        FUASAtmosphereViewExtension
        >();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[UAS] View Extension IsValid = %s"),
        UASViewExtension.IsValid()
        ? TEXT("TRUE")
        : TEXT("FALSE")
    );
}

void FAndromedaModule::ShutdownModule()
{
    UASViewExtension.Reset();
}

IMPLEMENT_PRIMARY_GAME_MODULE(
    FAndromedaModule,
    Andromeda,
    "Andromeda"
);