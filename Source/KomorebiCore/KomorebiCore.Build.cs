// Copyright Komorebi Project. Pure C++20 core simulation & data module (also built standalone via Tests/CMakeLists.txt).
using UnrealBuildTool;

public class KomorebiCore : ModuleRules
{
	public KomorebiCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});
	}
}
