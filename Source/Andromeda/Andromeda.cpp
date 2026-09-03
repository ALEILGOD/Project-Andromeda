// Fill out your copyright notice in the Description page of Project Settings.

#include "Andromeda.h"

#include "Misc/Paths.h"
#include "ShaderCore.h"


void FAndromedaModule::StartupModule()
{
    // =========================================================
    // UAS SHADER DIRECTORY
    // =========================================================

    const FString ShaderDirectory =
        FPaths::Combine(
            FPaths::ProjectDir(),
            TEXT("Shaders")
        );


    AddShaderSourceDirectoryMapping(
        TEXT("/Project/Andromeda"),
        ShaderDirectory
    );
}


void FAndromedaModule::ShutdownModule()
{
}


IMPLEMENT_PRIMARY_GAME_MODULE(
    FAndromedaModule,
    Andromeda,
    "Andromeda"
);