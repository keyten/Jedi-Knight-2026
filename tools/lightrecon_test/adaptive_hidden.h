// ASR16 tool-only adaptive local search. The historical Hidden path is unchanged.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/hidden_candidates.h"

namespace AdaptiveHidden
{
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
struct Options
{
	size_t maxFeatures = 6, maxBatches = 128;
	double maximumAreaExtentWorld = 2048, minimumCellWorld = 4;
	// Conditional exclusion by the caller's visible-fixture association stage.
	// Absence from this list does not certify that a region has no physical lamp.
	std::vector<Id> explainedAreas;
};
struct Batch
{
	Id area = 0;
	Bounds positions;
	std::string reason;
	std::vector<Id> observations;
	uint64_t nodes = 0, retained = 0, darkReceivers = 0, blockedBrightRelations = 0;
};
struct Audit
{
	std::vector<Batch> batches;
	uint64_t featureWork = 0, explainedSkipped = 0, unsupportedFeatures = 0;
	bool budgetLimited = false;
};
Error Generate(const Hidden::Request &, const Options &, const ReadContext &, Hidden::Result &, Audit &);
} // namespace AdaptiveHidden
