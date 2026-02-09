// Copyright (c) 2026 Jonas Reich & Contributors

#include "OUUBlueprintValidationSettings.h"

const FString* UOUUBlueprintValidationSettings::FindDisallowedFunctionReason(const FString& FunctionPath) const
{
	auto* DisallowedFunctionPtr = DisallowedFunctionPaths.FindByPredicate(
		[&FunctionPath](const FOUUBV_KeyValuePair& Pair) { return Pair.Key.Equals(FunctionPath); });
	if (DisallowedFunctionPtr)
	{
		return &DisallowedFunctionPtr->Value;
	}
	return nullptr;
}
