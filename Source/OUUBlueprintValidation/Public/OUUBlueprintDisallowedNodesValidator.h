// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "EditorValidatorBase.h"

#include "OUUBlueprintDisallowedNodesValidator.generated.h"

// Validates blueprints for usage of disallowed nodes
UCLASS()
class OUUBLUEPRINTVALIDATION_API UOUUBlueprintDisallowedNodesValidator : public UEditorValidatorBase
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
	static void ValidateDisallowedNodes(
		const UBlueprint& Blueprint,
		TFunctionRef<void(TSharedRef<FTokenizedMessage>)> MessageFunction);
};
