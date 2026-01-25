// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintValidationUtils.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_EventNodeInterface.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Tunnel.h"

namespace OUU::BlueprintValidation
{
	bool IsBlueprintGraph(const UEdGraph& Graph) { return IsValid(Cast<UEdGraphSchema_K2>(Graph.GetSchema())); }

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

} // namespace OUU::BlueprintValidation
