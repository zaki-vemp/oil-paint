// Copyright Komorebi Project. Primary Unreal Engine game module with virtual shader directory mapping.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FKomorebiGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
