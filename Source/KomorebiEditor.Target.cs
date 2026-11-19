// Copyright Komorebi Project. Cross-platform Editor Target (Linux, Mac, Windows).
using UnrealBuildTool;
using System.Collections.Generic;

public class KomorebiEditorTarget : TargetRules
{
	public KomorebiEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		CppStandard = CppStandardVersion.Cpp20;
		ExtraModuleNames.AddRange(new string[] { "KomorebiCore", "KomorebiGame" });
	}
}
