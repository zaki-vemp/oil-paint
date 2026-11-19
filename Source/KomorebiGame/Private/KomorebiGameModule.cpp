// Copyright Komorebi Project.
#include "KomorebiGameModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

void FKomorebiGameModule::StartupModule()
{
	const FString ShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
	if (FPaths::DirectoryExists(ShaderDir))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Project"), ShaderDir);
	}
}

void FKomorebiGameModule::ShutdownModule()
{
}

IMPLEMENT_PRIMARY_GAME_MODULE(FKomorebiGameModule, KomorebiGame, "KomorebiGame");
