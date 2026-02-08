// Copyright (c) 2026 Jonas Reich & Contributors

using UnrealBuildTool;

public class OUUBlueprintValidation : ModuleRules
{
	public OUUBlueprintValidation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"DataValidation",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"Engine",
			"BlueprintGraph",
			"UnrealEd",
			"Kismet",
			"UMGEditor",
			"Projects"
		});
	}
}