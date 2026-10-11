using UnrealBuildTool;

public class ZtripEditor : ModuleRules
{
	public ZtripEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true; // for the ztrip core headers

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ZtripRuntime",
		});
	}
}
