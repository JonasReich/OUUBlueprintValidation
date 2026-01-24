// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "EditorValidatorBase.h"

#include "OUUBlueprintMaintainabilityValidator.generated.h"

// Validates blueprints for complexity metrics configured in OUUBlueprintValidationSettings
UCLASS()
class UOUUBlueprintMaintainabilityValidator : public UEditorValidatorBase
{
	GENERATED_BODY()
public:
	// - UEditorValidatorBase
	bool CanValidateAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InObject,
		FDataValidationContext& InContext) const override;
	EDataValidationResult ValidateLoadedAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InAsset,
		FDataValidationContext& Context) override;
	// --

private:
	EDataValidationResult CheckCyclomaticComplexity(const UBlueprint& Blueprint, FDataValidationContext& Context);
};
