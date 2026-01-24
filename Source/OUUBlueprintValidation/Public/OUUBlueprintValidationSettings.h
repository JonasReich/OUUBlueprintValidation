// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "OUUBlueprintValidationSettings.generated.h"

UCLASS(Config = Editor, DefaultConfig)
class UOUUBlueprintValidationSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	FORCEINLINE static const UOUUBlueprintValidationSettings& Get()
	{
		return *GetDefault<UOUUBlueprintValidationSettings>();
	}

	// Should messages for BP maintainability failures be sent only as warning? Otherwise they are sent as error.
	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Overall")
	bool WarnOnFailure = false;

	// Should info messages for BP maintainability be sent to the compiler output independent of validation result?
	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Overall")
	bool LogMetricsOnBlueprintCompile = false;

	// Should info messages for BP maintainability be sent to the message log independent of validation result?
	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Overall")
	bool LogMetricsOnAssetValidate = false;

	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Overall", meta = (UIMin = 0, UIMax = 200))
	int32 MaxGraphsPerBlueprint = 100;

	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 100))
	double MinGraphMaintainabilityIndex = 30;

	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 500))
	int32 MaxNodeCountPerGraph = 120;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Bluperint Maintainability - Per Graph",
		meta = (UIMin = 0, UIMax = 50000))
	int32 MaxHalsteadVolumePerGraph = 3500;

	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 100))
	int32 MaxCyclomaticComplexityPerGraph = 40;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Bluperint Maintainability - Per Graph",
		meta = (UIMin = 0, ClampMin = 0, UIMax = 100, ClampMax = 100, Units = "Percent"))
	float MinCommentPercentagePerGraph = 30;

	// Only evaluate comment percentage if this number of nodes in the graph is reached or surpassed
	UPROPERTY(Config, EditAnywhere, Category = "Bluperint Maintainability - Per Graph", meta = (UIMin = 0, UIMax = 100))
	int32 MinNumberOfNodesToConsiderComments = 20;
};
