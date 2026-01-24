// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "BlueprintCompilerExtension.h"

#include "OUUBlueprintValidationCompilerExtension.generated.h"

UCLASS()
class UOUUBlueprintValidationCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()
protected:
	void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;

private:
	void ValidateMaintainability(const FKismetCompilerContext& CompilationContext);
};