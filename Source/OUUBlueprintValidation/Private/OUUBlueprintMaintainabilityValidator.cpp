
#include "OUUBlueprintMaintainabilityValidator.h"

#include "EdGraph/EdGraph.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DataValidation.h"
#include "OUUBlueprintComplexity.h"
#include "OUUBlueprintValidationSettings.h"
#include "OUUBlueprintValidationUtils.h"

bool UOUUBlueprintMaintainabilityValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& InContext) const
{
	// Theoretically we could also limit this to only some context (e.g. skip when saving because it likely happens
	// together with compilation, but this is not that much duplicate work if save on compile is enabled and keeps it
	// more consistent).
	return IsValid(Cast<UBlueprint>(InAsset))
		&& UOUUBlueprintValidationSettings::Get().CheckMaintainabilityMetrics
		!= EOUUBlueprintValidationSeverity::DoNotValidate;
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
			if (Message->GetSeverity() != EMessageSeverity::Info)
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
	const bool TooManyGraphs = NumBlueprintGraphs > Settings.MaxGraphsPerBlueprint;
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
