// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "OUUBlueprintValidationSettings.generated.h"

UENUM()
enum class EOUUBlueprintValidationSeverity
{
	DoNotValidate,
	Info,
	Warning,
	Error
};

FORCEINLINE EMessageSeverity::Type ToMessageSeverity(EOUUBlueprintValidationSeverity Severity)
{
	switch (Severity)
	{
	case EOUUBlueprintValidationSeverity::Error: return EMessageSeverity::Error;
	case EOUUBlueprintValidationSeverity::Warning: return EMessageSeverity::Warning;
	default: return EMessageSeverity::Info;
	}
}

UCLASS(Config = Editor, DefaultConfig)
class UOUUBlueprintValidationSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	FORCEINLINE static const UOUUBlueprintValidationSettings& Get()
	{
		return *GetDefault<UOUUBlueprintValidationSettings>();
	}

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Validation")
	EOUUBlueprintValidationSeverity CheckBlueprintCasts = EOUUBlueprintValidationSeverity::Warning;

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Validation")
	TArray<TSoftClassPtr<UObject>> AllowBlueprintCastToChildrenOf;

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Validation")
	EOUUBlueprintValidationSeverity CheckDisallowedFunctions = EOUUBlueprintValidationSeverity::Warning;

	// Function paths, and reasons why they are disallowed
	UPROPERTY(Config, EditAnywhere, CategorY = "Blueprint Validation")
	TMap<FString, FString> DisallowedFunctionPaths;

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Overall")
	EOUUBlueprintValidationSeverity CheckMaintainabilityMetrics = EOUUBlueprintValidationSeverity::Warning;

	// Should info messages for BP maintainability be sent to the compiler output independent of validation result?
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Overall")
	bool LogMetricsOnBlueprintCompile = false;

	// Should info messages for BP maintainability be sent to the message log independent of validation result?
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Overall")
	bool LogMetricsOnAssetValidate = false;

	// Custom documentation for maintainability.
	// If left empty, the maintainability validator will link to the local plugin README.md instead.
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Overall")
	FString MaintainabilityDocumentationURL;

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Overall", meta = (UIMin = 0, UIMax = 200))
	int32 MaxGraphsPerBlueprint = 100;

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 100))
	double MinGraphMaintainabilityIndex = 30;

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 500))
	int32 MaxNodeCountPerGraph = 120;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Blueprint Maintainability - Per Graph",
		meta = (UIMin = 0, UIMax = 50000))
	int32 MaxHalsteadVolumePerGraph = 3500;

	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 100))
	int32 MaxCyclomaticComplexityPerGraph = 40;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Blueprint Maintainability - Per Graph",
		meta = (UIMin = 0, ClampMin = 0, UIMax = 100, ClampMax = 100, Units = "Percent"))
	float MinCommentPercentagePerGraph = 30;

	// Only evaluate comment percentage if this number of nodes in the graph is reached or surpassed
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 100))
	int32 MinNumberOfNodesToConsiderComments = 20;
};
