// Copyright Komorebi Project. Cross-platform Game Target (Linux, Mac, Windows).
using UnrealBuildTool;
using System.Collections.Generic;

public class KomorebiTarget : TargetRules
{
	public KomorebiTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		CppStandard = CppStandardVersion.Cpp20;
		ExtraModuleNames.AddRange(new string[] { "KomorebiCore", "KomorebiGame" });
	}
}
