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
			bool HasInputData = false;
			for (auto* Pin : Node.Pins)
			{
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					(Pin->Direction == EGPD_Input ? HasInputExecs : HasOutputExecs) = true;
				}
				else if (Pin->Direction == EGPD_Input)
				{
					HasInputData = true;
				}
			}

			// Regular case: the input/gateway tunnel of an execution-driven macro or collapsed graph. It carries
			// execution into the graph via its output exec pins and has no incoming execution of its own.
			if (HasInputExecs == false && HasOutputExecs)
			{
				return true;
			}

			// Purely data-driven case: a collapsed graph built entirely from pure nodes (e.g. a "math expression"
			// node) has no execution pins anywhere. Such a graph is evaluated demand-driven from its result tunnel -
			// the output node pulls the whole upstream pure chain - so we treat that result tunnel (the only tunnel
			// that consumes data) as the graph's entry point. This anchors the downstream-consumer analysis so a
			// connected pure chain is not misreported as disconnected.
			return HasInputExecs == false && HasOutputExecs == false && HasInputData;
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
