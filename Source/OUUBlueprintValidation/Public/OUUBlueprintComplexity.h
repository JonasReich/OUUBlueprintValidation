// Copyright (c) 2026 Jonas Reich & Contributors

#pragma once

#include "CoreMinimal.h"

class UEdGraph;

namespace OUU::BlueprintValidation
{
	// This is the formula that pieces all the other metrics below together into a maintainability index.
	// Uses the Microsoft formula from here:
	// For blueprint, we might get better results with other formulas.
	// https://learn.microsoft.com/en-us/visualstudio/code-quality/code-metrics-maintainability-index-range-and-meaning
	FORCEINLINE double ComputeMicrosoftMaintainabilityIndex(
		double HalsteadVolume,
		double Cyclomatic_Complexity,
		double LinesOfCode)
	{
		return FMath::Max(
			0.0,
			(171.0 - 5.2 * FMath::Loge(HalsteadVolume) - 0.23 * (Cyclomatic_Complexity)-16.2 * FMath::Loge(LinesOfCode))
				* 100.0 / 171.0);
	}

	// Get Cyclomatic Complexity of a single blueprint graph to estimate the number of different execution paths.
	// This function does NOT recurse into subgraphs.
	//
	// NOTE: This is not the true cyclomatic complexity as described by Thomas J. McCabe.
	// see https://en.wikipedia.org/wiki/Cyclomatic_complexity
	// The McCabe formula considers value selection (ternary expressions) and compound predicates (boolean expressions
	// combined with AND or OR operators) as separate decision points in the program. Instead, we use the more lenient
	// evaluation of execution paths suggested by Hangar 13's Valentin Galea in his Unreal Fest 2024 talk:
	// https://dev.epicgames.com/community/learning/talks-and-demos/z0WW/unreal-engine-de-spaghetti-your-blueprints-the-scientific-way-unreal-fest-2024
	OUUBLUEPRINTVALIDATION_API double ComputeCyclomaticGraphComplexity(UEdGraph& Graph);

	struct FHalsteadComplexity
	{
		uint32 Vocabulary = 0;
		uint32 Length = 0;
		double Effort = 0.0;
		double Difficulty = 0.0;
		double Volume = 0.0;
	};

	// Calculate the halstead complexity for a single Blueprint graph.
	// see https://en.wikipedia.org/wiki/Halstead_complexity_measures
	// This implementation assumes:
	// - Operators are: Non-pure nodes and pure nodes taking inputs
	// - Operands are:  Literal pin values and pure nodes without inputs
	OUUBLUEPRINTVALIDATION_API FHalsteadComplexity ComputeHalsteadGraphComplexity(UEdGraph& Graph);

	// @returns the "number of lines" in a blueprint graph:
	// number of impure nodes + (number of pure nodes / 5)
	// conservatively assuming every 5 impure nodes is equivalent to a new line of code
	OUUBLUEPRINTVALIDATION_API double CountBlueprintLinesOfCode(UEdGraph& Graph);

	// Count all the comments in the graph:
	// Number of comment nodes and node comments
	OUUBLUEPRINTVALIDATION_API uint32 CountGraphComments(UEdGraph& Graph);

} // namespace OUU::BlueprintValidation
