// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraph.h"

namespace OUU::BlueprintValidation
{
	bool IsBlueprintGraph(const UEdGraph& Graph);

	// should return true for macro entry, function entry and event graph event nodes.
	// For collapsed/macro graphs made up entirely of pure nodes (no execution pins anywhere) the result tunnel is
	// treated as the entry point, because such graphs are evaluated demand-driven from their output.
	bool IsBlueprintEntryNode(UEdGraphNode& Node);

	TArray<UEdGraphPin*> GetInputParameterPins(UEdGraphNode& Node);

	void OnMessageLogLinkActivated(const TSharedRef<IMessageToken>& Token);

	TSharedRef<IMessageToken> CreateGraphOrNodeToken(const UObject* InObject);

} // namespace OUU::BlueprintValidation
