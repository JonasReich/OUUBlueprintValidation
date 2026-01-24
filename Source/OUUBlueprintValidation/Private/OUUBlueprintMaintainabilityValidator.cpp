
#include "OUUBlueprintMaintainabilityValidator.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DataValidation.h"
#include "Misc/UObjectToken.h"
#include "OUUBlueprintComplexity.h"
#include "OUUBlueprintValidationSettings.h"
#include "OUUBlueprintValidationUtils.h"

UE_DISABLE_OPTIMIZATION

bool UOUUBlueprintMaintainabilityValidator::CanValidateAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& InContext) const
{
	if (IsValid(InAsset) == false)
	{
		return false;
	}

	return InAsset->IsA<UBlueprint>();
}

EDataValidationResult UOUUBlueprintMaintainabilityValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& InAssetData,
	UObject* InAsset,
	FDataValidationContext& Context)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(InAsset);

	if (IsValid(Blueprint) == false)
	{
		Context.AddError(INVTEXT("Asset is not a blueprint"));
		return EDataValidationResult::Invalid;
	}

	EDataValidationResult Result = EDataValidationResult::Valid;
	Result = CombineDataValidationResults(Result, CheckCyclomaticComplexity(*Blueprint, Context));
	return Result;
}

EDataValidationResult UOUUBlueprintMaintainabilityValidator::CheckCyclomaticComplexity(
	const UBlueprint& Blueprint,
	FDataValidationContext& Context)
{
	auto& Settings = UOUUBlueprintValidationSettings::Get();

	auto ConditionallyAddMessage = [&](bool Failed, UEdGraph* OptionalGraph, FText&& Message) {
		if (Failed || Settings.InfoOnSuccess)
		{
			auto TokenizedMessage = FTokenizedMessage::Create(
				Failed ? Settings.WarnOnFailure ? EMessageSeverity::Warning : EMessageSeverity::Error
					   : EMessageSeverity::Info);
			if (OptionalGraph)
			{
				struct Local
				{
					static void OnMessageLogLinkActivated(const class TSharedRef<IMessageToken>& Token)
					{
						if (Token->GetType() == EMessageToken::Object)
						{
							const TSharedRef<FUObjectToken> UObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
							if (UObjectToken->GetObject().IsValid())
							{
								FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(
									UObjectToken->GetObject().Get());
							}
						}
					}
				};

				TokenizedMessage->AddToken(
					FUObjectToken::Create(OptionalGraph, FText::FromString(GetNameSafe(OptionalGraph)))
						->OnMessageTokenActivated(
							FOnMessageTokenActivated::CreateStatic(&Local::OnMessageLogLinkActivated)));
			}
			TokenizedMessage->AddText(Message);
			Context.AddMessage(TokenizedMessage);
		}
	};

	TArray<UEdGraph*> Graphs;
	Blueprint.GetAllGraphs(OUT Graphs);

	int32 NumBlueprintGraphs = 0;

	for (auto* Graph : Graphs)
	{
		if (Graph == nullptr || OUU::BlueprintValidation::IsBlueprintGraph(*Graph) == false)
		{
			continue;
		}
		NumBlueprintGraphs++;
		const auto GraphComplexity = OUU::BlueprintValidation::ComputeCyclomaticGraphComplexity(*Graph);
		const auto Halstead = OUU::BlueprintValidation::ComputeHalsteadGraphComplexity(*Graph);
		const auto LinesOfCode = OUU::BlueprintValidation::CountBlueprintLinesOfCode(*Graph);
		const auto MaintainabilityIndex =
			OUU::BlueprintValidation::ComputeMaintainabilityIndex(Halstead.Volume, GraphComplexity, LinesOfCode);
		const auto CommentCount = OUU::BlueprintValidation::CountGraphComments(*Graph);
		const auto NodeCount = Graph->Nodes.Num();

		ConditionallyAddMessage(
			NodeCount > Settings.MaxNodeCountPerGraph,
			Graph,
			FText::Format(
				INVTEXT("Node count: {0} (max per graph: {1})"),
				FText::AsNumber(NodeCount),
				FText::AsNumber(Settings.MaxNodeCountPerGraph)));

		const double CommentPercentage =
			CommentCount > 0 ? (static_cast<double>(CommentCount) / static_cast<double>(NodeCount)) * 100.0 : 0.0;
		ConditionallyAddMessage(
			NodeCount > Settings.MinNumberOfNodesToConsiderComments
				&& CommentPercentage < Settings.MinCommentPercentagePerGraph,
			Graph,
			FText::Format(
				INVTEXT("Comment percentage: {0} (min per graph: {1}; only if more than {2} nodes)"),
				FText::AsNumber(CommentPercentage),
				FText::AsNumber(Settings.MinCommentPercentagePerGraph),
				FText::AsNumber(Settings.MinNumberOfNodesToConsiderComments)));

		ConditionallyAddMessage(
			GraphComplexity > Settings.MaxCyclomaticComplexityPerGraph,
			Graph,
			FText::Format(
				INVTEXT("Cyclomatic Complexity: {0} (max per graph: {1})"),
				FText::AsNumber(GraphComplexity),
				FText::AsNumber(Settings.MaxCyclomaticComplexityPerGraph)));

		ConditionallyAddMessage(
			Halstead.Volume > Settings.MaxHalsteadVolumePerGraph,
			Graph,
			FText::Format(
				INVTEXT("Halstead volume: {0} (max per graph: {1})"),
				FText::AsNumber(Halstead.Volume),
				FText::AsNumber(Settings.MaxHalsteadVolumePerGraph)));

		ConditionallyAddMessage(
			MaintainabilityIndex < Settings.MinGraphMaintainabilityIndex,
			Graph,
			FText::Format(
				INVTEXT("Maintainability index: {0} (min per graph: {1})"),
				FText::AsNumber(MaintainabilityIndex),
				FText::AsNumber(Settings.MinGraphMaintainabilityIndex)));
	}

	ConditionallyAddMessage(
		NumBlueprintGraphs > Settings.MaxGraphsPerBlueprint,
		nullptr,
		FText::Format(
			INVTEXT("Number of graphs: {0} (max per BP: {1})"),
			FText::AsNumber(NumBlueprintGraphs),
			FText::AsNumber(Settings.MaxGraphsPerBlueprint)));

	return (Context.GetNumErrors() > 0 || Context.GetNumWarnings() > 0) ? EDataValidationResult::Invalid
																		: EDataValidationResult::Valid;
}
