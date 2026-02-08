// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintValidationCompilerExtension.h"

#include "Editor/UMGEditor/Public/Settings/WidgetDesignerSettings.h"
#include "Editor/UMGEditor/Public/WidgetBlueprint.h"
#include "KismetCompiler.h"
#include "OUUBlueprintDisallowedNodesValidator.h"
#include "OUUBlueprintMaintainabilityValidator.h"
#include "OUUBlueprintValidationSettings.h"

void UOUUBlueprintValidationCompilerExtension::ProcessBlueprintCompiled(
	const FKismetCompilerContext& CompilationContext,
	const FBlueprintCompiledData& Data)
{
	// Do not fail or prolong blueprint compilation during cook
	if (IsRunningCookCommandlet())
	{
		return;
	}

	bool ShouldLogMetrics = UOUUBlueprintValidationSettings::Get().LogMetricsOnBlueprintCompile;

	CompilationContext.MessageLog.BeginEvent(TEXT("ValidateMaintainability"));
	UOUUBlueprintMaintainabilityValidator::ValidateMaintainability(
		*CompilationContext.Blueprint,
		[&](TSharedRef<FTokenizedMessage> Message) { CompilationContext.MessageLog.AddTokenizedMessage(Message); },
		ShouldLogMetrics);
	CompilationContext.MessageLog.EndEvent();

	CompilationContext.MessageLog.BeginEvent(TEXT("ValidateDisallowedNodes"));
	UOUUBlueprintDisallowedNodesValidator::ValidateDisallowedNodes(
		*CompilationContext.Blueprint,
		[&](TSharedRef<FTokenizedMessage> Message) { CompilationContext.MessageLog.AddTokenizedMessage(Message); });
	CompilationContext.MessageLog.EndEvent();

	if (ShouldLogMetrics && CompilationContext.Blueprint->IsA<UWidgetBlueprint>()
		&& (CompilationContext.MessageLog.NumErrors + CompilationContext.MessageLog.NumWarnings) == 0)
	{
		// Make sure the messages actually get seen by the user if this plugin is enabled and configured to display
		// messages on compile (which is NOT the default, i.e. an active choice). By default the widget designer would
		// dismiss/close the compiler results tab.
		GetMutableDefault<UWidgetDesignerSettings>()->DismissOnCompile = DoC_Never;
	}
}
