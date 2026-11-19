// Copyright Komorebi Project. Native C++ Dedicated Server Target (replaces legacy server/room.mjs).
using UnrealBuildTool;
using System.Collections.Generic;

public class KomorebiServerTarget : TargetRules
{
	public KomorebiServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		CppStandard = CppStandardVersion.Cpp20;
		ExtraModuleNames.AddRange(new string[] { "KomorebiCore", "KomorebiGame" });
	}
}
