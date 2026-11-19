// Copyright Komorebi Project. Main Unreal Engine 5.4/5.5 runtime game module (Linux Vulkan, macOS Metal, Windows DX12/Vulkan).
using UnrealBuildTool;

public class KomorebiGame : ModuleRules
{
	public KomorebiGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ProceduralMeshComponent",
			"UMG",
			"Slate",
			"SlateCore",
			"AudioMixer",
			"SignalProcessing",
			"NetCore",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"AIModule",
			"NavigationSystem",
			"RenderCore",
			"RHI",
			"KomorebiCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore"
		});
	}
}
