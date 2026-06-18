// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintValidationUtils.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_EventNodeInterface.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/UObjectToken.h"

namespace OUU::BlueprintValidation
{
	bool IsBlueprintGraph(const UEdGraph& Graph)
	{
		auto* pSchema = Cast<UEdGraphSchema_K2>(Graph.GetSchema());
		// Many ed graph types like ability graphs are "full" BP graphs, but the checks in this lib don't make sense for
		// anim graphs, because all the blend graphs, etc. come with their own set of connection rules that were not
		// considered.
		return IsValid(pSchema) && pSchema->GetName().Contains(TEXT("Anim")) == false;
	}

	bool IsBlueprintEntryNode(UEdGraphNode& Node)
	{
		// for functions and event graphs
		if (Node.IsA<UK2Node_FunctionEntry>() || Node.Implements<UK2Node_EventNodeInterface>())
		{
			return true;
		}

		// for macros and collapsed graphs
		if (Node.IsA<UK2Node_Tunnel>())
		{
			bool HasInputExecs = false, HasOutputExecs = false;
			for (auto* Pin : Node.Pins)
			{
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					(Pin->Direction == EGPD_Input ? HasInputExecs : HasOutputExecs) = true;
				}
			}
			return HasInputExecs == false && HasOutputExecs;
		}
		return false;
	}

	TArray<UEdGraphPin*> GetInputParameterPins(UEdGraphNode& Node)
	{
		// interpret all non exec inputs of a node as a "parameter"
		TArray<UEdGraphPin*> ParameterPins;
		for (auto& Pin : Node.Pins)
		{
			if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				ParameterPins.Add(Pin);
			}
		}
		return ParameterPins;
	}

	void OnMessageLogLinkActivated(const TSharedRef<IMessageToken>& Token)
	{
		if (Token->GetType() == EMessageToken::Object)
		{
			const TSharedRef<FUObjectToken> UObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
			if (UObjectToken->GetObject().IsValid())
			{
				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(UObjectToken->GetObject().Get());
			}
		}
	}

	TSharedRef<IMessageToken> CreateGraphOrNodeToken(const UObject* InObject)
	{
		auto DisplayName = FText::FromString(GetNameSafe(InObject));
		if (auto* Node = Cast<UEdGraphNode>(InObject))
		{
			DisplayName = Node->GetNodeTitle(ENodeTitleType::Type::ListView);
		}

		return FUObjectToken::Create(InObject, DisplayName)
			->OnMessageTokenActivated(FOnMessageTokenActivated::CreateStatic(&OnMessageLogLinkActivated));
	}

} // namespace OUU::BlueprintValidation
