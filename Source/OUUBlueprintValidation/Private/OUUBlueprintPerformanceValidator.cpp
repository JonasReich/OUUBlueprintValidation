// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintPerformanceValidator.h"

#include "EdGraphSchema_K2.h"
#include "IClassVariableCreator.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MapForEach.h"
#include "K2Node_SetForEach.h"
#include "K2Node_VariableGet.h"
#include "Misc/DataValidation.h"
#include "OUUBlueprintValidationSettings.h"
#include "OUUBlueprintValidationUtils.h"

namespace OUU::BlueprintValidation::Private
{
	// Walk back through reroute knots to find the original upstream node.
	// Returns the first non-knot pin in the chain, or nullptr if the chain dead-ends in an unconnected knot.
	// This could theoretically stall if a user manages to connect knots in a circular path.
	static UEdGraphPin* TraceThroughKnots(UEdGraphPin* Pin)
	{
		while (Pin != nullptr)
		{
			auto* Knot = Cast<UK2Node_Knot>(Pin->GetOwningNode());
			if (Knot == nullptr)
			{
				return Pin;
			}
			auto* KnotInput = Knot->GetInputPin();
			if (KnotInput == nullptr || KnotInput->LinkedTo.IsEmpty())
			{
				return nullptr;
			}
			Pin = KnotInput->LinkedTo[0];
		}
		return nullptr;
	}

	// Is the given node a potential target for slow pure value copies.
	// This is most likely the target for nodes that iterate container elements without assigning the container value
	// to a variable first.
	static bool IsPotentialExpensivePureContainerTarget(const UEdGraphNode& Node)
	{
		return Node.IsA<UK2Node_MacroInstance>() || Node.IsA<UK2Node_MapForEach>() || Node.IsA<UK2Node_SetForEach>();
	}

	// Pure nodes that just read stored state (variable getters / property access, reroute knots) are cheap
	// even when their output is an array - re-evaluating them inside an inlined macro body is not a meaningful
	// perf concern. We only want to flag pure nodes that perform real work to produce the array.
	static bool IsExpensivePureArraySource(const UK2Node& Node)
	{
		if (Node.IsNodePure() == false)
		{
			return false;
		}
		// IClassVariableCreator is the only way for us to check for property access nodes
		if (Node.IsA<UK2Node_VariableGet>() || Node.IsA<UK2Node_Knot>() || Node.IsA<UK2Node_BreakStruct>()
			|| Node.IsA<UK2Node_StructMemberGet>() || Node.Implements<UClassVariableCreator>())
		{
			return false;
		}
		return true;
	}
} // namespace OUU::BlueprintValidation::Private

bool UOUUBlueprintPerformanceValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& InContext) const
{
	return IsValid(Cast<UBlueprint>(InAsset))
		&& UOUUBlueprintValidationSettings::Get().CheckPureContainerIntoMacroInput
		!= EOUUBlueprintValidationSeverity::DoNotValidate;
}

EDataValidationResult UOUUBlueprintPerformanceValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	const auto& Blueprint = *CastChecked<UBlueprint>(InAsset);
	EDataValidationResult Result = EDataValidationResult::Valid;
	ValidatePerformance(Blueprint, [&](TSharedRef<FTokenizedMessage> Message) {
		Context.AddMessage(Message);
		if (Message->GetSeverity() == EMessageSeverity::Error)
		{
			Result = EDataValidationResult::Invalid;
		}
	});
	return Result;
}

void UOUUBlueprintPerformanceValidator::ValidatePerformance(
	const UBlueprint& Blueprint,
	TFunctionRef<void(TSharedRef<FTokenizedMessage>)> MessageFunction)
{
	auto& Settings = UOUUBlueprintValidationSettings::Get();
	if (Settings.CheckPureContainerIntoMacroInput == EOUUBlueprintValidationSeverity::DoNotValidate)
	{
		return;
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint.GetAllGraphs(OUT AllGraphs);
	for (auto* Graph : AllGraphs)
	{
		if (Graph == nullptr || OUU::BlueprintValidation::IsBlueprintGraph(*Graph) == false)
		{
			continue;
		}

		for (auto Node : Graph->Nodes)
		{
			if (Node == nullptr
				|| OUU::BlueprintValidation::Private::IsPotentialExpensivePureContainerTarget(*Node.Get()) == false)
			{
				continue;
			}

			for (auto* InputPin : OUU::BlueprintValidation::GetInputParameterPins(*Node))
			{
				// We only care about container-typed inputs. Pure nodes that produce them get re-evaluated
				// for every reference inside the inlined macro body, rebuilding the entire container each time.
				if (InputPin == nullptr || InputPin->PinType.IsContainer() == false)
				{
					continue;
				}

				for (auto* LinkedToPin : InputPin->LinkedTo)
				{
					auto* SourcePin = OUU::BlueprintValidation::Private::TraceThroughKnots(LinkedToPin);
					if (SourcePin == nullptr)
					{
						continue;
					}
					auto* PureNode = Cast<UK2Node>(SourcePin->GetOwningNode());
					if (PureNode == nullptr
						|| OUU::BlueprintValidation::Private::IsExpensivePureArraySource(*PureNode) == false)
					{
						continue;
					}

					const auto Severity = ToMessageSeverity(Settings.CheckPureContainerIntoMacroInput);
					const auto Message = FTokenizedMessage::Create(Severity);
					Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(PureNode));
					Message->AddText(INVTEXT("(pure node) feeds an array output into the input pin "));
					Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(Node));
					const auto Text = INVTEXT(
						"(blueprint macro). Macro inputs are inlined at every reference inside the macro body, "
						"so the pure node will be re-evaluated and the container re-built for every reference. "
						"Cache the container into a local variable (or convert the macro into a function) instead.");
					Message->AddText(Text);

					if (PureNode->bHasCompilerMessage == false)
					{
						PureNode->bHasCompilerMessage = true;
						PureNode->ErrorMsg = Text.ToString();
						PureNode->ErrorType = Severity;
					}
					MessageFunction(Message);
				}
			}
		}
	}
}
