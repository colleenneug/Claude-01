using UnrealBuildTool;
using System.Collections.Generic;

public class ErebusCradleEditorTarget : TargetRules
{
	public ErebusCradleEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("ErebusCradle");
	}
}
