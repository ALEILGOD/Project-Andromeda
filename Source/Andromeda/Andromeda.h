#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUASAtmosphereViewExtension;

class FAndromedaModule
    : public FDefaultGameModuleImpl
{
public:

    virtual void StartupModule() override;

    virtual void ShutdownModule() override;

private:

    TSharedPtr<FUASAtmosphereViewExtension>
        UASViewExtension;
};