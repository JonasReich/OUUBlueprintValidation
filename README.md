# OUUBlueprintValidation

This Unreal Engine plugin adds blueprint validators and a blueprint compiler extensions to check for
common best practices of node usage and blueprint complexity to ensure they remain maintainable.

## Maintainability Validator

This validator checks each blueprint graph (event graph, function graph, etc)
for the following complexity metrics, which are described in more detail below:

- Maintainability Index
- Cyclomatic Complexity
- Halstead Volume
- Graph/Node Count Limits
- Comment Percentage

Your project can define custom thresholds for each of these values,
and specify if crossing those thresholds yields an error, warning or merely a text message.

### Maintainability Index

> TLDR: Improve other metrics to improve this

This is a combined factor of all the other metrics. Our formula is adapted from the [Visual Studio maintainability index](https://learn.microsoft.com/en-us/visualstudio/code-quality/code-metrics-maintainability-index-range-and-meaning):
```
MaintainabilityIndex = max(0.0, (171.0 - 5.2 * ln(max(1.0, HalsteadVolume)) - 0.23 * (CyclomaticComplexity) - 16.2 * ln(max(1.0, LinesOfCode))) * 100.0 / 171.0)
```

Essentially, this is a score, where an empty graph that can be considered perfectly maintainable
gets a score of 100 that is reduced by each increase of the other metrics (Cyclomatic Complexity, Halstead Volume, Lines of Code).

Falling below a minimum threshold of around 30 indicates problems with the overall maintainability even if the other
metrics by themselves do not cross their respective thresholds.

Since Blueprints don't have any text lines that could be counted, we consider each non-pure node a new line as well as
every 5th pure node. Like many of the other values in these formulas this is a somewhat arbitrary "magic number"
value that we just stick with as long as we feel it results in validation results that match our expectations.

### Cyclomatic Complexity

> TLDR: You can decrease cyclomatic complexity by reducing the number of branches / alternate exec paths in a graph.

This is a measure of how many different code paths lead through a piece of code. This means the number of entry points
of a graph and branching points increase the cyclomatic complexity.

NOTE: This is not the "true" cyclomatic complexity as described by
[Thomas J. McCabe](https://en.wikipedia.org/wiki/Cyclomatic_complexity).

The McCabe formula considers value selection (ternary expressions) and compound predicates
(boolean expressions combined with AND or OR operators) as separate decision points in the program.
Instead, we use the more lenient evaluation of execution paths suggested by Hangar 13's Valentin Galea
in his Unreal Fest 2024 talk ["De-spaghetti Your Blueprints, the Scientific Way"](https://dev.epicgames.com/community/learning/talks-and-demos/z0WW/unreal-engine-de-spaghetti-your-blueprints-the-scientific-way-unreal-fest-2024).

Our implementation increases cyclomatic complexity for:
- each unique entry point of a graph (e.g. multiple event nodes)
- each time an EXEC path splits after a node (e.g. after each IF), including disconnected exec pins that could result in early termination of a code path

Our implementation does NOT increase cyclomatic complexity for:
- value selection of pure nodes
- multiple exec output from one node that are all connected to the same input pin on another node

Usually, this value will range between 1-20 for ordinary blueprint graphs.

### Halstead Volume

> TLDR: You can reduce Halstead Volume by removing nodes and using nodes with fewer parameters

The Halstead Volume is one of several [complexity measures introduced by M. H. Halstead](https://en.wikipedia.org/wiki/Halstead_complexity_measures).
His paper was considering text-based source code, so some liberties were taken to re-interpret them for use in Blueprint code.

Out implementation assumes:
- Operators are: All non-pure nodes and pure nodes taking inputs
- Operands are:  All literal pin values and pure nodes without inputs

The algorithm then tallies up the total usages and number of unique variants of these operators and operands to derive
several metrics. The volume that is used for the Maintainability Index is caclulated as follows:

```
Volume = (TotalOperatos + TotalOperands) * ln(UniqueOperators + UniqueOperands)
```

Usually, this value will range between 100-500 for ordinary blueprint graphs, but might as well be bigger.
The bigger this is, the more important it is to keep other metrics like Cyclomatic Complexity low.

### Comment Percentage

> TLDR: You can improve this metric by adding comments

Comment percentage is the relation of comments (comment nodes or on-node-comments) to the number of total nodes in the graph.
I would recommend keeping this value fairly low, lower than you initally think to ensure the validator permits graphs
that are node-heavy but still structured well and "self explanatory", but still finding outlier graphs.
The worst outcome of introducing a validator like this is making everyone add redundant comments that are
written badly only to please the machine.

## Disallowed Node Validator

This validator checks each blueprint node against a list of disallowed nodes from the project settings.
I recommend adding any functions that are likely to misuse and cause runtime hitches, like GetAllActors, LoadAssetBlocking, etc.

The functions are referenced by their function path in the project settings, so you'll have to look up
the function library or class and its module for the full path, e.g. `/Script/Engine.KismetSystemLibrary:LoadAsset_Blocking`.
