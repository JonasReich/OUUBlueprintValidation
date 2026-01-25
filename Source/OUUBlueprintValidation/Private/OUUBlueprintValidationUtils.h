// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraph.h"

namespace OUU::BlueprintValidation
{
	bool IsBlueprintGraph(const UEdGraph& Graph);

	// should return true for macro entry, function entry and event graph event nodes
	bool IsBlueprintEntryNode(UEdGraphNode& Node);

	TArray<UEdGraphPin*> GetInputParameterPins(UEdGraphNode& Node);

} // namespace OUU::BlueprintValidation
