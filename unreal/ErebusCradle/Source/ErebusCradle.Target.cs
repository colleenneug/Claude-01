using UnrealBuildTool;
using System.Collections.Generic;

public class ErebusCradleTarget : TargetRules
{
	public ErebusCradleTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("ErebusCradle");
	}
}
