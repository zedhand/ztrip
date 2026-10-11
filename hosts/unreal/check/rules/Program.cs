// Runs each module's rules and checks the settings the ztrip core needs.
using System;
using UnrealBuildTool;

static class Program
{
	static int Failures;

	static void Expect(bool Ok, string What)
	{
		Console.WriteLine((Ok ? "ok   " : "FAIL ") + What);
		if (!Ok) Failures++;
	}

	static int Main()
	{
		ReadOnlyTargetRules Target = new ReadOnlyTargetRules();

		ModuleRules Runtime = new ZtripRuntime(Target);
		Expect(Runtime.PCHUsage == PCHUsageMode.NoPCHs, "ZtripRuntime: no PCH");
		Expect(!Runtime.bUseUnity, "ZtripRuntime: no unity files");
		Expect(Runtime.bEnableExceptions, "ZtripRuntime: exceptions on");
		Expect(Runtime.CppStandard == CppStandardVersion.Cpp20, "ZtripRuntime: C++20");
		Expect(Runtime.PrivateDefinitions.Exists(D => D.StartsWith("ZTRIP_VERSION_STRING=")), "ZtripRuntime: version define");
		Expect(Runtime.PrivateDefinitions.Contains("pffft_transform=ztrip_pffft_transform"), "ZtripRuntime: pffft renamed");
		Expect(Runtime.PrivateIncludePaths.Count == 2, "ZtripRuntime: third-party include paths");
		Expect(Runtime.PrivateDependencyModuleNames.Contains("Niagara"), "ZtripRuntime: Niagara dependency");
		CppCompileWarnings W = Runtime.CppCompileWarningSettings;
		Expect(W.ShadowVariableWarningLevel == WarningLevel.Off
			&& W.UndefinedIdentifierWarningLevel == WarningLevel.Off
			&& W.UnsafeTypeCastWarningLevel == WarningLevel.Off, "ZtripRuntime: third-party warnings relaxed");
		Expect(!Runtime.bEnableUndefinedIdentifierWarnings, "ZtripRuntime: undefined-identifier flag off");

		ModuleRules Editor = new ZtripEditor(Target);
		Expect(Editor.bEnableExceptions, "ZtripEditor: exceptions on");
		Expect(Editor.PrivateDependencyModuleNames.Contains("ZtripRuntime"), "ZtripEditor: depends on ZtripRuntime");

		Console.WriteLine(Failures == 0 ? "module rules ok" : Failures + " check(s) failed");
		return Failures == 0 ? 0 : 1;
	}
}
