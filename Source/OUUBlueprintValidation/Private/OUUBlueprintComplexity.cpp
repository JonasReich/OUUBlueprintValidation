// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintComplexity.h"

#include "EdGraphNode_Comment.h"
#include "K2Node_ExecutionSequence.h"
#include "OUUBlueprintValidationUtils.h"

namespace OUU::BlueprintValidation
{
	int32 CountBranches(UEdGraphNode& Node, TSet<uint32>& VisitedNodes)
	{
		if (VisitedNodes.Contains(Node.GetUniqueID()))
		{
			return 0;
		}
		VisitedNodes.Add(Node.GetUniqueID());

		// We only have to consider outgoing exec connections.
		// Theoretically blueprint Pure inputs also contribute to executions, but they are always executed on all
		// branches, so there's only a matter of value selection, not execution flow / order changes on the BP graph
		// level.
		TArray<UEdGraphPin*> ConnectedExecOutPins = Node.Pins.FilterByPredicate([](const UEdGraphPin* Pin) {
			return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
				&& Pin->Direction == EEdGraphPinDirection::EGPD_Output;
		});

		TArray<UEdGraphPin*> ConnectedPins;
		TArray<UEdGraphNode*> ConnectedNodes;
		bool HasDisconnectedOutputs = false;
		bool HasConnectedOutputs = false;
		for (const auto ExecOutPin : ConnectedExecOutPins)
		{
			if (ExecOutPin->LinkedTo.IsEmpty())
			{
				HasDisconnectedOutputs = true;
			}
			else
			{
				// exec OUT pins are always just connected to a single other pin and node
				HasConnectedOutputs = true;
				ConnectedPins.AddUnique(ExecOutPin->LinkedTo[0]);
				ConnectedNodes.AddUnique(ExecOutPin->LinkedTo[0]->GetOwningNode());
			}
		}

		if (HasConnectedOutputs == false)
		{
			return 0;
		}

		uint32 BranchCounter = 0;

		// The sequence node is the only node I could think of that can be connected to arbitrarily many other nodes or
		// have disconnected output pins without creating new possible execution paths / early returns.
		if (Node.IsA<UK2Node_ExecutionSequence>() == false)
		{
			if (HasDisconnectedOutputs)
			{
				// If we know there's both disconnected and connected output pins those are early returns or
				// continuations, i.e. 1 additional branch.
				BranchCounter++;
			}

			// Number of alternative exec pins on this same node.
			// We consider every secondary output pin on a node a "branch" except for those on sequence nodes.
			// Since nodes that have multiple inputs are usually flow control nodes like gates, async tasks, etc.
			// those pins need to be counted individually, not just per node.
			BranchCounter += FMath::Max(0, ConnectedPins.Num() - 1);
		}

		for (const auto& ConnectedNode : ConnectedNodes)
		{
			if (IsValid(ConnectedNode))
			{
				BranchCounter += CountBranches(*ConnectedNode, VisitedNodes);
			}
		}

		return BranchCounter;
	}

	double ComputeCyclomaticGraphComplexity(UEdGraph& Graph)
	{
		if (!ensure(OUU::BlueprintValidation::IsBlueprintGraph(Graph)))
		{
			return 0.0;
		}
		TSet<uint32> VisitedNodes;
		int32 GraphComplexity = 0;
		VisitedNodes.Empty();
		for (auto& Node : Graph.Nodes)
		{
			if (IsValid(Node) && IsBlueprintEntryNode(*Node))
			{
				// Always start with 1 for each entry point of the graph
				int32 NumberOfBranches = 1 + CountBranches(*Node, IN OUT VisitedNodes);
				GraphComplexity += NumberOfBranches;
			}
		}
		return static_cast<double>(GraphComplexity);
	}

	FHalsteadComplexity ComputeHalsteadGraphComplexity(UEdGraph& Graph)
	{
		if (!ensure(OUU::BlueprintValidation::IsBlueprintGraph(Graph)))
		{
			return {};
		}

		uint32 NumberOfTotalOperators = 0, NumberOfTotalOperands = 0;

		TSet<uint32> VisitedNodes;

		TSet<FString> UniqueParameterValues;
		TSet<uint32> UniqueOperatorNodes;
		// Nodes that are not functions but variables / property access / etc that have no parameter pins
		TSet<uint32> UniqueOperandNodes;

		for (auto& Node : Graph.Nodes)
		{
			auto* K2Node = Cast<UK2Node>(Node);
			if (K2Node == nullptr || K2Node->IsNodePure() || VisitedNodes.Contains(K2Node->GetUniqueID()))
			{
				continue;
			}
			VisitedNodes.Add(K2Node->GetUniqueID());
			UniqueOperatorNodes.Add(K2Node->GetUniqueID());

			TArray<UEdGraphPin*> ParameterPins = GetInputParameterPins(*Node);
			for (auto ParamPinIt = ParameterPins.CreateIterator(); ParamPinIt; ++ParamPinIt)
			{
				auto* ParamPin = *ParamPinIt;
				if (ParamPin->LinkedTo.IsEmpty())
				{
					// we consider values in their clipboard/copy format
					FString PinValue;
					ParamPin->ExportTextItem(OUT PinValue, PPF_Copy);
					UniqueParameterValues.Add(PinValue);
					NumberOfTotalOperands++;
				}
				else
				{
					for (auto* LinkedTo : ParamPin->LinkedTo)
					{
						auto* LinkedToK2Node = Cast<UK2Node>(LinkedTo->GetOwningNode());
						// ignore if the connection in itself is not a pure node (it will be processed at top level)
						if (LinkedToK2Node == nullptr || LinkedToK2Node->IsNodePure() == false
							|| VisitedNodes.Contains(LinkedToK2Node->GetUniqueID()))
						{
							continue;
						}
						VisitedNodes.Add(LinkedToK2Node->GetUniqueID());

						auto RecursivePins = GetInputParameterPins(*LinkedToK2Node);
						if (RecursivePins.IsEmpty())
						{
							NumberOfTotalOperators++;
							UniqueOperandNodes.Add(LinkedToK2Node->GetUniqueID());
						}
						else
						{
							// This is an operator node
							NumberOfTotalOperators++;
							UniqueOperatorNodes.Add(LinkedToK2Node->GetUniqueID());
						}
						ParameterPins.Append(RecursivePins);
					}
				}
			}
		}

		uint32 NumberOfUniqueOperators = UniqueOperatorNodes.Num();
		uint32 NumberOfUniqueOperands = UniqueOperandNodes.Num() + UniqueParameterValues.Num();

		FHalsteadComplexity Result;
		Result.Vocabulary = NumberOfUniqueOperators + NumberOfUniqueOperands;
		Result.Length = NumberOfTotalOperators + NumberOfTotalOperands;
		Result.Volume = Result.Vocabulary == 0
			? 0
			: static_cast<double>(Result.Length) * FMath::Log2(static_cast<double>(Result.Vocabulary));
		Result.Difficulty = NumberOfUniqueOperands == 0 ? 0
														: (static_cast<double>(NumberOfUniqueOperators) / 2.0)
				* (static_cast<double>(NumberOfTotalOperators) / static_cast<double>(NumberOfUniqueOperands));
		Result.Effort = Result.Difficulty * Result.Volume;

		return Result;
	}

	double CountBlueprintLinesOfCode(UEdGraph& Graph)
	{
		if (!ensure(OUU::BlueprintValidation::IsBlueprintGraph(Graph)))
		{
			return 0.0;
		}

		uint32 NumberOfPureNodes = 0, NumberOfImpureNodes = 0;
		for (auto& Node : Graph.Nodes)
		{
			if (auto* K2Node = Cast<UK2Node>(Node))
			{
				if (K2Node->IsNodePure())
				{
					NumberOfPureNodes++;
				}
				else
				{
					NumberOfImpureNodes++;
				}
			}
		}

		return NumberOfImpureNodes + (NumberOfPureNodes / 5.0);
	}

	uint32 CountGraphComments(UEdGraph& Graph)
	{
		if (!ensure(OUU::BlueprintValidation::IsBlueprintGraph(Graph)))
		{
			return 0;
		}

		uint32 NumComments = 0;
		for (auto& Node : Graph.Nodes)
		{
			if (Node->IsA<UEdGraphNode_Comment>() || Node->NodeComment.Len() > 0)
			{
				NumComments++;
			}
		}
		return NumComments;
	}
} // namespace OUU::BlueprintValidation
