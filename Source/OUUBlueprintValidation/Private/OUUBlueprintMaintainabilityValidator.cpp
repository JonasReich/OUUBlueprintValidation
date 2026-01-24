
#include "OUUBlueprintMaintainabilityValidator.h"

#include "EdGraph/EdGraph.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DataValidation.h"
#include "Misc/UObjectToken.h"
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
	return IsValid(Cast<UBlueprint>(InAsset));
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

		struct Local
		{
			static void OnMessageLogLinkActivated(const class TSharedRef<IMessageToken>& Token)
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
		};

		TokenizedMessage->AddToken(
			FUObjectToken::Create(&Graph, FText::FromString(GetNameSafe(&Graph)))
				->OnMessageTokenActivated(FOnMessageTokenActivated::CreateStatic(&Local::OnMessageLogLinkActivated)));

		TokenizedMessage->AddText(Message);
		return TokenizedMessage;
	};

	auto ConditionallyAddMessage = [&](bool Failed, UEdGraph& Graph, FText&& Message) {
		if (Failed == false)
		{
			return;
		}
		MessageFunction(MakeGraphMessage(
			Settings.WarnOnFailure ? EMessageSeverity::Warning : EMessageSeverity::Error,
			Graph,
			MoveTemp(Message)));
	};

	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(OUT Graphs);

	const int32 NumBlueprintGraphs = Graphs.Num();
	const bool TooManyGraphs = NumBlueprintGraphs > Settings.MaxGraphsPerBlueprint;
	if (TooManyGraphs || LogMetrics)
	{
		MessageFunction(FTokenizedMessage::Create(
			TooManyGraphs ? Settings.WarnOnFailure ? EMessageSeverity::Warning : EMessageSeverity::Error
						  : EMessageSeverity::Info,
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

		const auto GraphComplexity = OUU::BlueprintValidation::ComputeCyclomaticGraphComplexity(*Graph);
		const auto Halstead = OUU::BlueprintValidation::ComputeHalsteadGraphComplexity(*Graph);
		const auto LinesOfCode = OUU::BlueprintValidation::CountBlueprintLinesOfCode(*Graph);

		const double MaintainabilityIndex = OUU::BlueprintValidation::ComputeMicrosoftMaintainabilityIndex(
			Halstead.Volume,
			GraphComplexity,
			LinesOfCode);

		const auto CommentCount = OUU::BlueprintValidation::CountGraphComments(*Graph);
		const auto NodeCount = Graph->Nodes.Num();
		const double CommentPercentage =
			CommentCount > 0 ? (static_cast<double>(CommentCount) / static_cast<double>(NodeCount)) * 100.0 : 0.0;

		if (LogMetrics)
		{
			MetricMessages.Add(MakeGraphMessage(
				EMessageSeverity::Info,
				*Graph,
				FText::Format(
					INVTEXT("Maintainability Index: {0}; Cyclomatic Complexity: {1}; Halstead Volume: {2}; "
							"Node Count: {3}; Comment %: {4}"),
					FText::AsNumber(MaintainabilityIndex),
					FText::AsNumber(GraphComplexity),
					FText::AsNumber(Halstead.Volume),
					FText::AsNumber(NodeCount),
					FText::AsNumber(CommentPercentage))));
		}

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
				FText::AsNumber(Halstead.Volume),
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
	}

	if (LogMetrics)
	{
		for (auto& Message : MetricMessages)
		{
			MessageFunction(Message.ToSharedRef());
		}
	}
}
