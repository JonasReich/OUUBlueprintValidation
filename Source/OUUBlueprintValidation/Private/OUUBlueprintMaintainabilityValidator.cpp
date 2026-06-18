
#include "OUUBlueprintMaintainabilityValidator.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Interfaces/IPluginManager.h"
#include "K2Node.h"
#include "K2Node_Composite.h"
#include "Misc/DataValidation.h"
#include "OUUBlueprintComplexity.h"
#include "OUUBlueprintValidationSettings.h"
#include "OUUBlueprintValidationUtils.h"

namespace OUU::BlueprintValidation::Private
{
	// Organizational nodes group or annotate a graph without participating in its execution or data flow.
	// They are never flagged as disconnected.
	static bool IsOrganizationalNode(const UEdGraphNode& Node) { return Node.IsA<UEdGraphNode_Comment>(); }

	static bool IsPureNode(const UEdGraphNode& Node)
	{
		const auto* K2Node = Cast<UK2Node>(&Node);
		if (K2Node == nullptr || K2Node->IsNodePure() == false)
		{
			return false;
		}
		// An execution reroute (UK2Node_Knot) reports IsNodePure() == true, yet it routes execution flow
		// rather than data and its pins are PC_Exec. Treat any "pure" node that carries an execution pin as
		// an execution-flow node so it is judged by execution reachability instead of the data-only downstream
		// consumer analysis (which would never find a consumer and would wrongly flag it as disconnected).
		for (const auto* Pin : Node.Pins)
		{
			if (Pin != nullptr && UEdGraphSchema_K2::IsExecPin(*Pin))
			{
				return false;
			}
		}
		return true;
	}

	// Forward DFS over execution output pins, collecting every node reachable by execution flow.
	// Pure nodes have no execution pins and are therefore never reached here - the gathered set only ever
	// contains impure (execution-carrying) nodes.
	static void GatherExecReachableNodes(UEdGraphNode& Node, TSet<UEdGraphNode*>& OutReachable)
	{
		for (const auto* Pin : Node.Pins)
		{
			if (Pin == nullptr || Pin->Direction != EGPD_Output
				|| Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				continue;
			}
			for (const auto* LinkedTo : Pin->LinkedTo)
			{
				if (LinkedTo == nullptr)
				{
					continue;
				}
				auto* NextNode = LinkedTo->GetOwningNode();
				if (NextNode == nullptr)
				{
					continue;
				}
				bool AlreadyVisited = false;
				OutReachable.Add(NextNode, &AlreadyVisited);
				if (AlreadyVisited == false)
				{
					GatherExecReachableNodes(*NextNode, OutReachable);
				}
			}
		}
	}

	// Forward DFS over data (non-exec) output pins with per-node memoization, used to resolve which impure
	// consumer nodes a pure node ultimately feeds into. The walk passes through chains of pure passthrough
	// nodes (knots, struct breaks, getters, ...) and terminates at the first impure (or non-K2) node on each
	// path, so a pure node's connectivity can be judged by whether any of these consumers is itself reachable
	// from a graph entry point.
	class FDownstreamImpureConsumerAnalyzer
	{
	public:
		const TSet<UEdGraphNode*>& Analyze(UEdGraphNode& Node)
		{
			if (auto* Existing = Cache.Find(&Node))
			{
				return *Existing;
			}
			// Sentinel: a re-entry while we're still computing this node returns the empty default,
			// breaking any hypothetical cycle deterministically.
			Cache.Add(&Node, TSet<UEdGraphNode*>{});

			TSet<UEdGraphNode*> Result;
			if (IsPureNode(Node) == false)
			{
				// This node is itself an impure consumer - it is the terminal of the data chain.
				Result.Add(&Node);
			}
			else
			{
				for (const auto* OutPin : Node.Pins)
				{
					if (OutPin == nullptr || OutPin->Direction != EGPD_Output
						|| OutPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						continue;
					}
					for (const auto* LinkedTo : OutPin->LinkedTo)
					{
						if (LinkedTo == nullptr)
						{
							continue;
						}
						auto* DownNode = LinkedTo->GetOwningNode();
						if (DownNode == nullptr)
						{
							continue;
						}
						Result.Append(Analyze(*DownNode));
					}
				}
			}

			Cache[&Node] = MoveTemp(Result);
			return Cache[&Node];
		}

	private:
		TMap<const UEdGraphNode*, TSet<UEdGraphNode*>> Cache;
	};

	// Finds all disconnected nodes in a single blueprint graph.
	// An impure node is disconnected when it is not reachable by execution flow from any graph entry node.
	// A pure node is disconnected when none of the impure consumers it feeds (directly or through pure
	// passthrough chains) is itself a connected impure node.
	static TArray<UEdGraphNode*> FindDisconnectedNodes(UEdGraph& Graph)
	{
		// 1. Collect all impure nodes reachable by execution flow from the graph's entry points.
		TSet<UEdGraphNode*> ConnectedImpureNodes;
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (IsValid(Node) && IsBlueprintEntryNode(*Node))
			{
				bool AlreadyVisited = false;
				ConnectedImpureNodes.Add(Node, &AlreadyVisited);
				if (AlreadyVisited == false)
				{
					GatherExecReachableNodes(*Node, ConnectedImpureNodes);
				}
			}
		}

		// 2. Classify every (non-organizational, non-entry) node against the connected set.
		FDownstreamImpureConsumerAnalyzer Analyzer;
		TArray<UEdGraphNode*> Result;
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (IsValid(Node) == false || IsOrganizationalNode(*Node)
				|| IsBlueprintEntryNode(*Node)
				// Tunnels may be left disconnected, because macro/collapse graph setups always get the "outputs" node,
				// except composites / collapsed graphs.
				|| (Node->IsA<UK2Node_Tunnel>() && Node->IsA<UK2Node_Composite>() == false))
			{
				continue;
			}

			bool IsDisconnected = false;
			if (IsPureNode(*Node))
			{
				// A pure node is connected only if it ultimately feeds a connected impure node.
				IsDisconnected = true;
				for (auto* Consumer : Analyzer.Analyze(*Node))
				{
					if (ConnectedImpureNodes.Contains(Consumer))
					{
						IsDisconnected = false;
						break;
					}
				}
			}
			else
			{
				IsDisconnected = ConnectedImpureNodes.Contains(Node) == false;
			}

			if (IsDisconnected)
			{
				Result.Add(Node);
			}
		}

		// For the search we needed nodes to be correctly flagged, but now we can bend the rules:
		// Any nodes that have comments to them are assumed to be "commented out" and will be stripped from the results.
		// We only apply this rule for single individually commented nodes, not node clusters with "comment node"
		// markup.
		for (auto It = Result.CreateIterator(); It; ++It)
		{
			auto* Node = *It;
			if (Node->NodeComment.IsEmpty() == false)
			{
				It.RemoveCurrentSwap();
			}
		}

		return Result;
	}
} // namespace OUU::BlueprintValidation::Private

bool UOUUBlueprintMaintainabilityValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& InContext) const
{
	// Theoretically we could also limit this to only some context (e.g. skip when saving because it likely happens
	// together with compilation, but this is not that much duplicate work if save on compile is enabled and keeps it
	// more consistent).
	const auto& Settings = UOUUBlueprintValidationSettings::Get();
	return IsValid(Cast<UBlueprint>(InAsset))
		&& (Settings.CheckMaintainabilityMetrics != EOUUBlueprintValidationSeverity::DoNotValidate
			|| Settings.CheckDisconnectedNodes != EOUUBlueprintValidationSeverity::DoNotValidate);
}

EDataValidationResult UOUUBlueprintMaintainabilityValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	UBlueprint& Blueprint = *CastChecked<UBlueprint>(InAsset);
	EDataValidationResult Result = EDataValidationResult::Valid;
	ValidateMaintainability(
		Blueprint,
		[&](TSharedRef<FTokenizedMessage> Message) {
			Context.AddMessage(Message);
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Result = EDataValidationResult::Invalid;
			}
		},
		UOUUBlueprintValidationSettings::Get().LogMetricsOnAssetValidate);
	return Result;
}

void UOUUBlueprintMaintainabilityValidator::ValidateMaintainability(
	const UBlueprint& Blueprint,
	TFunctionRef<void(TSharedRef<FTokenizedMessage>)> MessageFunction,
	bool LogMetrics)
{
	auto& Settings = UOUUBlueprintValidationSettings::Get();
	const bool CheckMaintainability =
		Settings.CheckMaintainabilityMetrics != EOUUBlueprintValidationSeverity::DoNotValidate;
	const bool CheckDisconnectedNodes =
		Settings.CheckDisconnectedNodes != EOUUBlueprintValidationSeverity::DoNotValidate;
	const auto DisconnectedNodesSeverity = ToMessageSeverity(Settings.CheckDisconnectedNodes);

	auto MakeGraphMessage =
		[&](EMessageSeverity::Type Severity, UEdGraph& Graph, FText&& Message) -> TSharedRef<FTokenizedMessage> {
		auto TokenizedMessage = FTokenizedMessage::Create(Severity);
		TokenizedMessage->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(&Graph));
		TokenizedMessage->AddText(Message);
		return TokenizedMessage;
	};

	bool AnyGraphRuleFailed = false;
	auto ConditionallyAddMessage = [&](bool Failed, UEdGraph& Graph, FText&& Message) {
		if (Failed == false)
		{
			return;
		}
		AnyGraphRuleFailed = true;
		MessageFunction(
			MakeGraphMessage(ToMessageSeverity(Settings.CheckMaintainabilityMetrics), Graph, MoveTemp(Message)));
	};

	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(OUT Graphs);

	const int32 NumBlueprintGraphs = Graphs.Num();
	const bool TooManyGraphs = CheckMaintainability && NumBlueprintGraphs > Settings.MaxGraphsPerBlueprint;
	if (TooManyGraphs || LogMetrics)
	{
		MessageFunction(FTokenizedMessage::Create(
			TooManyGraphs ? ToMessageSeverity(Settings.CheckMaintainabilityMetrics) : EMessageSeverity::Info,
			FText::Format(
				INVTEXT("Number of graphs: {0} (max per BP: {1})"),
				FText::AsNumber(NumBlueprintGraphs),
				FText::AsNumber(Settings.MaxGraphsPerBlueprint))));
	}

	TArray<TSharedPtr<FTokenizedMessage>> MetricMessages;

	for (auto* Graph : Graphs)
	{
		if (Graph == nullptr || OUU::BlueprintValidation::IsBlueprintGraph(*Graph) == false)
		{
			continue;
		}

		AnyGraphRuleFailed = false;

		if (CheckDisconnectedNodes)
		{
			const auto DisconnectedNodes = OUU::BlueprintValidation::Private::FindDisconnectedNodes(*Graph);
			if (DisconnectedNodes.Num() > 0)
			{
				for (auto* Node : DisconnectedNodes)
				{
					if (Node->bHasCompilerMessage == false)
					{
						Node->bHasCompilerMessage = true;
						Node->ErrorMsg =
							TEXT("disconnected node (not reachable from any graph entry node, and - if pure - not "
								 "feeding any connected impure node)");
						Node->ErrorType = DisconnectedNodesSeverity;
					}
				}
				auto Message = FTokenizedMessage::Create(DisconnectedNodesSeverity);
				Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(DisconnectedNodes[0]));
				Message->AddText(FText::Format(
					INVTEXT(" is a disconnected node (not reachable from any graph entry node, and - if pure - not "
							"feeding any connected impure node). {0} disconnected node(s) found in this graph."),
					FText::AsNumber(DisconnectedNodes.Num())));
				MessageFunction(Message);
			}
		}

		if (CheckMaintainability == false)
		{
			continue;
		}

		// For display purposes: round all values to integers

		const double GraphComplexityDouble = OUU::BlueprintValidation::ComputeCyclomaticGraphComplexity(*Graph);
		const int32 GraphComplexity = FMath::RoundToInt(GraphComplexityDouble);
		const auto Halstead = OUU::BlueprintValidation::ComputeHalsteadGraphComplexity(*Graph);
		const int32 HalsteadVolume = FMath::RoundToInt(Halstead.Volume);
		const int32 LinesOfCode = OUU::BlueprintValidation::CountBlueprintLinesOfCode(*Graph);

		const int32 MaintainabilityIndex =
			FMath::RoundToInt(OUU::BlueprintValidation::ComputeMicrosoftMaintainabilityIndex(
				HalsteadVolume,
				GraphComplexityDouble,
				LinesOfCode));

		const int32 CommentCount = OUU::BlueprintValidation::CountGraphComments(*Graph);
		const int32 NodeCount = Graph->Nodes.Num();
		const int32 CommentPercentage = FMath::RoundToInt(
			CommentCount > 0 ? (static_cast<double>(CommentCount) / static_cast<double>(NodeCount)) * 100.0 : 0.0);

		ConditionallyAddMessage(
			MaintainabilityIndex < Settings.MinGraphMaintainabilityIndex,
			*Graph,
			FText::Format(
				INVTEXT("Maintainability index: {0} (min per graph: {1})"),
				FText::AsNumber(MaintainabilityIndex),
				FText::AsNumber(Settings.MinGraphMaintainabilityIndex)));

		ConditionallyAddMessage(
			GraphComplexity > Settings.MaxCyclomaticComplexityPerGraph,
			*Graph,
			FText::Format(
				INVTEXT("Cyclomatic Complexity: {0} (max per graph: {1})"),
				FText::AsNumber(GraphComplexity),
				FText::AsNumber(Settings.MaxCyclomaticComplexityPerGraph)));

		ConditionallyAddMessage(
			Halstead.Volume > Settings.MaxHalsteadVolumePerGraph,
			*Graph,
			FText::Format(
				INVTEXT("Halstead volume: {0} (max per graph: {1})"),
				FText::AsNumber(HalsteadVolume),
				FText::AsNumber(Settings.MaxHalsteadVolumePerGraph)));

		ConditionallyAddMessage(
			NodeCount > Settings.MaxNodeCountPerGraph,
			*Graph,
			FText::Format(
				INVTEXT("Node count: {0} (max per graph: {1})"),
				FText::AsNumber(NodeCount),
				FText::AsNumber(Settings.MaxNodeCountPerGraph)));

		ConditionallyAddMessage(
			NodeCount > Settings.MinNumberOfNodesToConsiderComments
				&& CommentPercentage < Settings.MinCommentPercentagePerGraph,
			*Graph,
			FText::Format(
				INVTEXT("Comment percentage: {0} (min per graph: {1}; only if more than {2} nodes)"),
				FText::AsNumber(CommentPercentage),
				FText::AsNumber(Settings.MinCommentPercentagePerGraph),
				FText::AsNumber(Settings.MinNumberOfNodesToConsiderComments)));

		if (LogMetrics || AnyGraphRuleFailed)
		{
			auto MetricMessage = MakeGraphMessage(
				EMessageSeverity::Info,
				*Graph,
				FText::Format(
					INVTEXT("Maintainability Index: {0}; Cyclomatic Complexity: {1}; Halstead Volume: {2}; "
							"Node Count: {3}; Comment %: {4}"),
					FText::AsNumber(MaintainabilityIndex),
					FText::AsNumber(GraphComplexity),
					FText::AsNumber(HalsteadVolume),
					FText::AsNumber(NodeCount),
					FText::AsNumber(CommentPercentage)));

			FString MessageURL = Settings.MaintainabilityDocumentationURL;
			if (MessageURL.IsEmpty())
			{
				auto& PluginManager = IPluginManager::Get();
				if (auto PluginPtr = PluginManager.FindPlugin(TEXT("OUUBlueprintValidation")))
				{
					MessageURL = FString::Printf(
						TEXT("file:///%s/README.md"),
						*FPaths::ConvertRelativePathToFull(PluginPtr->GetBaseDir()));
				}
			}
			if (MessageURL.IsEmpty() == false)
			{
				MetricMessage->AddToken(FURLToken::Create(MessageURL, INVTEXT("HELP")));
			}
			if (AnyGraphRuleFailed)
			{
				MessageFunction(MetricMessage);
			}
			else
			{
				MetricMessages.Add(MetricMessage);
			}
		}
	}

	if (LogMetrics)
	{
		for (auto& Message : MetricMessages)
		{
			MessageFunction(Message.ToSharedRef());
		}
	}
}
