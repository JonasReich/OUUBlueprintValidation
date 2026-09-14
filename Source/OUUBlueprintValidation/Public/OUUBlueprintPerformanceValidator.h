// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "EditorValidatorBase.h"

#include "OUUBlueprintPerformanceValidator.generated.h"

// Validates blueprints for known low-performance node setups
UCLASS()
class OUUBLUEPRINTVALIDATION_API UOUUBlueprintPerformanceValidator : public UEditorValidatorBase
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

	// This implementation is reused for both this asset validator and the BP compiler extension.
	static void ValidatePerformance(
		const UBlueprint& Blueprint,
		const TFunctionRef<void(TSharedRef<FTokenizedMessage>)>& MessageFunction);
};
