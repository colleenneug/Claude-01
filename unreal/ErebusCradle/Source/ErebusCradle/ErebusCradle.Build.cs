using UnrealBuildTool;

public class ErebusCradle : ModuleRules
{
	public ErebusCradle(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"GameplayTasks"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Slate",
			"SlateCore"
		});
	}
}
