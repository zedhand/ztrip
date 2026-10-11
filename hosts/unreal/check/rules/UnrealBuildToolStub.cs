// A stand-in for the parts of UnrealBuildTool (UE 5.6) that the plugin's
// module rules use, shaped like the real API: the warning levels live in
// CppCompileWarningSettings, with the older ModuleRules names forwarding to
// them.
using System.Collections.Generic;

namespace UnrealBuildTool
{
	public enum WarningLevel { Default, Off, Warning, Error }
	public enum PCHUsageMode { Default, NoPCHs, NoSharedPCHs, UseSharedPCHs, UseExplicitOrSharedPCHs }
	public enum CppStandardVersion { Default, Cpp20, Latest }

	public class ReadOnlyTargetRules { }

	public class CppCompileWarnings
	{
		public WarningLevel ShadowVariableWarningLevel { get; set; } = WarningLevel.Error;
		public WarningLevel UndefinedIdentifierWarningLevel { get; set; } = WarningLevel.Error;
		public WarningLevel UnsafeTypeCastWarningLevel { get; set; } = WarningLevel.Error;
	}

	public class ModuleRules
	{
		public ModuleRules(ReadOnlyTargetRules Target) { }

		public string ModuleDirectory = "Module";
		public PCHUsageMode PCHUsage = PCHUsageMode.Default;
		public bool bUseUnity = true;
		public bool bEnableExceptions;
		public CppStandardVersion? CppStandard;
		public List<string> PublicIncludePaths = new List<string>();
		public List<string> PrivateIncludePaths = new List<string>();
		public List<string> PublicDefinitions = new List<string>();
		public List<string> PrivateDefinitions = new List<string>();
		public List<string> PublicDependencyModuleNames = new List<string>();
		public List<string> PrivateDependencyModuleNames = new List<string>();
		public CppCompileWarnings CppCompileWarningSettings { get; } = new CppCompileWarnings();
		public WarningLevel ShadowVariableWarningLevel
		{
			get => CppCompileWarningSettings.ShadowVariableWarningLevel;
			set => CppCompileWarningSettings.ShadowVariableWarningLevel = value;
		}
		public bool bEnableUndefinedIdentifierWarnings = true;
	}
}
