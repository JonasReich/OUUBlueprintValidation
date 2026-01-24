// Copyright (c) 2026 Jonas Reich & Contributors

using UnrealBuildTool;

public class OUUBlueprintValidation : ModuleRules
{
	public OUUBlueprintValidation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"DataValidation",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"BlueprintGraph",
			"UnrealEd"
		});
	}
}