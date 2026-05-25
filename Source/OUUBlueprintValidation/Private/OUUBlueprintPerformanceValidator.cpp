// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintPerformanceValidator.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MapForEach.h"
#include "K2Node_SetForEach.h"
#include "Misc/DataValidation.h"
#include "OUUBlueprintValidationSettings.h"
#include "OUUBlueprintValidationUtils.h"

namespace OUU::BlueprintValidation::Private
{
	// Impure consumer types that re-reference their input pins multiple times inside their body
	// (macros are inlined at compile time; the MapForEach/SetForEach iterator nodes also read the
	// container value once per loop iteration). When a pure node's container output reaches one of
	// these targets, the pure node ends up re-evaluated for every internal reference - typically a
	// perf trap when the pure node actually does work to produce the container.
	static bool IsPotentialExpensivePureContainerTarget(const UEdGraphNode& Node)
	{
		return Node.IsA<UK2Node_MacroInstance>() || Node.IsA<UK2Node_MapForEach>() || Node.IsA<UK2Node_SetForEach>();
	}

	// Only pure nodes that actually run user-authored code are worth flagging as offenders:
	// function calls, macro instances, and collapsed-graph composites. Everything else pure
	// (variable getters, knots, struct breaks, literals, ...) is treated as a cheap passthrough -
	// still walked through during traversal so it cannot hide an upstream offender, but never
	// flagged on its own.
	static bool IsExpensivePureNode(const UK2Node& Node)
	{
		if (Node.IsNodePure() == false)
		{
			return false;
		}
		return Node.IsA<UK2Node_CallFunction>() || Node.IsA<UK2Node_MacroInstance>() || Node.IsA<UK2Node_Composite>();
	}

	// Per-node summary of what happens downstream of a node when its value is consumed by the BP VM.
	struct FPureNodeAnalysis
	{
		// How many times the BP VM will evaluate this node, considering that pure nodes are not cached
		// and get re-run on every pin read. Impure nodes are treated as a single evaluation (one read per
		// exec). For pure nodes this is the sum, over each outgoing pin connection, of the downstream
		// node's own EvalCount - which makes pin connections (not consumer nodes) the unit of counting.
		// That correctly handles e.g. an impure node reading the same upstream pure output via two
		// different input pins (counts as 2).
		int32 EvalCount = 0;

		// Distinct (impure-target-node, container-typed-input-pin) edges reachable downstream of this
		// node through any chain of pure passthrough intermediaries. Used by the container-into-target
		// report. Duplicates may appear if multiple paths converge on the same edge - we dedup when
		// emitting messages.
		TArray<TTuple<UEdGraphNode*, UEdGraphPin*>> ContainerTargetEdges;
	};

	// Forward DFS analyzer with per-graph memoization on the node pointer.
	// EvalCount is intrinsic to a node (depends only on its own downstream subgraph), so caching a
	// single FPureNodeAnalysis per node is sound regardless of how many ancestors reach it.
	class FPureNodeAnalyzer
	{
	public:
		const FPureNodeAnalysis& Analyze(UEdGraphNode& Node)
		{
			if (auto* Existing = Cache.Find(&Node))
			{
				return *Existing;
			}
			// Sentinel: a re-entry while we're still computing this node returns the empty default,
			// breaking any hypothetical cycle deterministically. Pure-only K2 subgraphs cannot cycle,
			// but be defensive.
			Cache.Add(&Node, FPureNodeAnalysis{});

			FPureNodeAnalysis Result;

			auto* K2Node = Cast<UK2Node>(&Node);
			const bool IsImpureOrUnknown = K2Node == nullptr || K2Node->IsNodePure() == false;

			if (IsImpureOrUnknown)
			{
				// Impure nodes are the cache boundary - they evaluate their inputs once when they execute.
				Result.EvalCount = 1;
			}
			else
			{
				for (auto* OutPin : K2Node->Pins)
				{
					if (OutPin == nullptr || OutPin->Direction != EGPD_Output
						|| OutPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						continue;
					}

					for (auto* LinkedPin : OutPin->LinkedTo)
					{
						if (LinkedPin == nullptr)
						{
							continue;
						}
						auto* DownNode = LinkedPin->GetOwningNode();
						if (DownNode == nullptr)
						{
							continue;
						}

						const auto& Sub = Analyze(*DownNode);
						Result.EvalCount += Sub.EvalCount;

						auto* DownK2 = Cast<UK2Node>(DownNode);
						const bool DownIsImpureOrUnknown = DownK2 == nullptr || DownK2->IsNodePure() == false;
						if (DownIsImpureOrUnknown)
						{
							if (IsPotentialExpensivePureContainerTarget(*DownNode) && LinkedPin->PinType.IsContainer())
							{
								Result.ContainerTargetEdges.Emplace(DownNode, LinkedPin);
							}
						}
						else
						{
							// Pure intermediary - inherit the edges it can reach.
							Result.ContainerTargetEdges.Append(Sub.ContainerTargetEdges);
						}
					}
				}
			}

			Cache[&Node] = MoveTemp(Result);
			return Cache[&Node];
		}

	private:
		TMap<const UEdGraphNode*, FPureNodeAnalysis> Cache;
	};
} // namespace OUU::BlueprintValidation::Private

bool UOUUBlueprintPerformanceValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& InContext) const
{
	const auto& Settings = UOUUBlueprintValidationSettings::Get();
	const bool AnyCheckActive =
		Settings.CheckPureContainerIntoMacroInput != EOUUBlueprintValidationSeverity::DoNotValidate
		|| Settings.CheckMultiplyEvaluatedPureNodes != EOUUBlueprintValidationSeverity::DoNotValidate;
	return IsValid(Cast<UBlueprint>(InAsset)) && AnyCheckActive;
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
	using namespace OUU::BlueprintValidation::Private;

	auto& Settings = UOUUBlueprintValidationSettings::Get();
	const bool CheckContainer =
		Settings.CheckPureContainerIntoMacroInput != EOUUBlueprintValidationSeverity::DoNotValidate;
	const bool CheckMultiEval =
		Settings.CheckMultiplyEvaluatedPureNodes != EOUUBlueprintValidationSeverity::DoNotValidate;
	if (CheckContainer == false && CheckMultiEval == false)
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

		FPureNodeAnalyzer Analyzer;

		for (auto Node : Graph->Nodes)
		{
			auto* PureNode = Cast<UK2Node>(Node);
			if (PureNode == nullptr || IsExpensivePureNode(*PureNode) == false)
			{
				continue;
			}

			const auto& Analysis = Analyzer.Analyze(*PureNode);

			// Container-into-target check: one message per (offender, target, container input pin) tuple.
			if (CheckContainer && Analysis.ContainerTargetEdges.Num() > 0)
			{
				TSet<TTuple<UEdGraphNode*, UEdGraphPin*>> Seen;
				const auto Severity = ToMessageSeverity(Settings.CheckPureContainerIntoMacroInput);
				for (const auto& Edge : Analysis.ContainerTargetEdges)
				{
					bool AlreadySeen = false;
					Seen.Add(Edge, &AlreadySeen);
					if (AlreadySeen)
					{
						continue;
					}

					const auto Message = FTokenizedMessage::Create(Severity);
					Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(PureNode));
					Message->AddText(INVTEXT("(pure node) feeds a container value (directly or via passthrough pure "
											 "nodes) into the input pin of "));
					Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(Edge.Get<0>()));
					const auto Text =
						INVTEXT(". Macro instances and Map/SetForEach nodes re-reference their container input pin "
								"once per element/iteration, so the pure node will be re-evaluated and the container "
								"re-built for every reference. Cache the container into a local variable (or convert "
								"the macro into a function) instead.");
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

			// Multi-evaluation check: a pure node whose effective evaluation count exceeds the threshold.
			if (CheckMultiEval && Analysis.EvalCount > Settings.MaxAllowedPureNodeEvaluations)
			{
				const auto Severity = ToMessageSeverity(Settings.CheckMultiplyEvaluatedPureNodes);
				const auto Message = FTokenizedMessage::Create(Severity);
				Message->AddToken(OUU::BlueprintValidation::CreateGraphOrNodeToken(PureNode));
				const auto Text = FText::Format(
					INVTEXT(" (pure node) will be evaluated {0} times - its output (directly or via passthrough pure "
							"nodes) is read by {0} downstream pin connections. Pure nodes are not cached, so each pin "
							"read re-runs the node. Cache the value into a local variable assigned from an impure "
							"step and read the variable instead."),
					FText::AsNumber(Analysis.EvalCount));
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
