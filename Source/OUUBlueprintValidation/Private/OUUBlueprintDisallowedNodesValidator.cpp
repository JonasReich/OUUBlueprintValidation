// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintDisallowedNodesValidator.h"

#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "Misc/DataValidation.h"
#include "OUUBlueprintValidationSettings.h"
#include "OUUBlueprintValidationUtils.h"

bool UOUUBlueprintDisallowedNodesValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& InContext) const
{
	return IsValid(Cast<UBlueprint>(InAsset));
}

EDataValidationResult UOUUBlueprintDisallowedNodesValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	const auto& Blueprint = *CastChecked<UBlueprint>(InAsset);
	EDataValidationResult Result = EDataValidationResult::Valid;
	ValidateDisallowedNodes(Blueprint, [&](TSharedRef<FTokenizedMessage> Message) {
		Context.AddMessage(Message);
		if (Message->GetSeverity() != EMessageSeverity::Info)
		{
			Result = EDataValidationResult::Invalid;
		}
	});
	return Result;
}

void UOUUBlueprintDisallowedNodesValidator::ValidateDisallowedNodes(
	const UBlueprint& Blueprint,
	TFunctionRef<void(TSharedRef<FTokenizedMessage>)> MessageFunction)
{
	auto& Settings = UOUUBlueprintValidationSettings::Get();

	TArray<UEdGraph*> AllGraphs;
	Blueprint.GetAllGraphs(OUT AllGraphs);
	for (auto* Graph : AllGraphs)
	{
		for (auto Node : Graph->Nodes)
		{
			if (auto* CastNode = Cast<UK2Node_DynamicCast>(Node))
			{
				if (IsValid(CastNode->TargetType)
					&& Settings.CheckBlueprintCasts != EOUUBlueprintValidationSeverity::DoNotValidate)
				{
					const bool IsBlueprintClass = CastNode->TargetType->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
					const bool IsInterface = CastNode->TargetType->HasAnyClassFlags(CLASS_Interface);

					if (IsBlueprintClass && IsInterface == false
						&& Settings.AllowBlueprintCastToChildrenOf.ContainsByPredicate(
							   [CastNode](const TSoftClassPtr<UObject>& Class) -> bool {
								   // No need to load the class. TargetType is already fully resolved, so if the class
								   // is in the inheritance chain this check should pass.
								   return CastNode->TargetType->IsChildOf(Class.Get());
							   })
							== false)
					{
						const auto Message = FTokenizedMessage::Create(ToMessageSeverity(Settings.CheckBlueprintCasts));
						Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(CastNode));
						auto Text =
							INVTEXT("Blueprint casts are disallowed unless explicitly whitelisted to prevent unwanted "
									"asset reference chains.  Cast to an interface or a C++ base class instead.");
						Message->AddText(Text);

						if (CastNode->bHasCompilerMessage == false)
						{
							CastNode->bHasCompilerMessage = true;
							CastNode->ErrorMsg = Text.ToString();
							CastNode->ErrorType = ToMessageSeverity(Settings.CheckBlueprintCasts);
						}
						MessageFunction(Message);
					}
				}
			}

			if (auto* FunctionNode = Cast<UK2Node_CallFunction>(Node))
			{
				const auto* Function = FunctionNode->GetTargetFunction();
				if (Function && Settings.CheckDisallowedFunctions != EOUUBlueprintValidationSeverity::DoNotValidate)
				{
					auto FunctionPath = Function->GetPathName();
					if (auto* Reason = Settings.DisallowedFunctionPaths.Find(FunctionPath))
					{
						const auto Message =
							FTokenizedMessage::Create(ToMessageSeverity(Settings.CheckDisallowedFunctions));
						Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(FunctionNode));
						auto Text = FText::Format(
							INVTEXT("Usage of this function was explicitly disallowed in the project settings. Reason: "
									"{0}"),
							FText::AsCultureInvariant(*Reason));
						Message->AddText(Text);
						if (FunctionNode->bHasCompilerMessage == false)
						{
							FunctionNode->bHasCompilerMessage = true;
							FunctionNode->ErrorMsg = Text.ToString();
							FunctionNode->ErrorType = ToMessageSeverity(Settings.CheckDisallowedFunctions);
						}
						MessageFunction(Message);
					}
				}
			}
		}
	}
}
