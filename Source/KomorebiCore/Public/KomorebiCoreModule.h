// Copyright Komorebi Project. UE module entrypoint (compiled only under Unreal Engine; skipped in standalone CMake builds).
#pragma once

#if defined(__UNREAL__)
#include "Modules/ModuleManager.h"

class FKomorebiCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
#endif
