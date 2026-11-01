// Hierarchical observation selection. Geometry and training-only signal are separate inputs.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "disk.h"
#include "input.h"
#include <array>
#include <vector>
namespace Rend2LightRecon
{
namespace Core
{
namespace Sampling
{
constexpr uint32_t kVersion = 1;
enum class Role
{
	Training,
	Validation,
	Heldout,
	Gap
};
enum class Purpose
{
	Census,
	CoarseCoverage,
	Discovery,
	PositionRefinement,
	ShadowRefinement,
	ModelValidation,
	FinalHeldout,
	Count
};
enum Reason : uint32_t
{
	NoPositiveSignal = 1u << 0,
	TooFewSameMaterial = 1u << 1,
	InsufficientNormals = 1u << 2,
	MissingIndependentFolds = 1u << 3,
	MissingGeometry = 1u << 4,
	UnknownEncoding = 1u << 5,
	WorkLimited = 1u << 6,
	CorrelatedSupport = 1u << 7,
	SaturatedOnly = 1u << 8
};
struct Geometry
{
	Id id = 0, surface = 0, material = 0, block = 0, correlation = 0;
	Vec3 position, normal;
	double receiverArea = 0, footprintRadius = 0, representedArea = 0;
	Role role = Role::Gap;
	bool supported = true, encodingKnown = false;
};
struct Signal
{
	Id id = 0;
	double rgb[3] = {};
	bool censored[3] = {};
	// Unknown stock bytes may be used only under the declared conditional policy.
	bool encodingKnown = false;
};
enum class PriorityCause
{
	TrainingResidual,
	SourceInfluence,
	SupportedShadow
};
struct Priority
{
	Id id = 0; // must already have a training Signal; hidden target IDs are rejected
	PriorityCause cause = PriorityCause::TrainingResidual;
	double value = 0; // bounded [0,1] selection rank, never a confidence/objective weight
};
struct Policy
{
	size_t maxPool = 65536, maxJobs = 8, coarseRows = 96, discoveryRows = 48, positionRows = 64,
		   shadowRows = 24, validationRows = 24, heldoutRows = 24, maxTotalRows = 1024,
		   maxRounds = 2;
	size_t maxSurfacesPerJob = 24, maxBlocksPerJob = 12;
	uint64_t maxBytes = 64ull << 20, maxWork = 16000000;
	double partitionBlockWorld = 256, minimumContrast = .03;
	bool allowConditionalEncodedSignal = false;
	bool sourcePoolComplete = true;
};
struct Selection
{
	Id id = 0, correlation = 0;
	double receiverArea = 0, representedArea = 0, statisticalWeight = 0;
};
struct Quality
{
	uint64_t samples = 0, effectiveGroups = 0, surfaces = 0, materials = 0, normalBins = 0,
			 blocks = 0;
	uint64_t positive = 0, censored = 0, maxSameMaterial = 0, informativeMaterials = 0;
	double receiverArea = 0, representedArea = 0, baselineWorld = 0, normalDiversity = 0,
		   contrast = 0;
	uint32_t reasons = 0;
	bool directionalSupport = false, discoveryAdmitted = false, validationGeometryAvailable = false;
	bool signalEvaluated = false;
};
struct Job
{
	Id id = 0;
	Bounds receivers, sourceContext;
	std::array<std::vector<Selection>, size_t(Purpose::Count)> selected;
	Quality training, validation, heldout;
	uint32_t reasons = 0;
};
struct Report
{
	uint64_t input = 0, duplicates = 0, gap = 0, unsupported = 0, work = 0, bytes = 0;
	uint64_t eligibleNeighbourhoods = 0, deferredNeighbourhoods = 0;
	double medianFootprintDiameter = 0, correlationWorld = 0, localCellWorld = 0,
		   coarseCellWorld = 0;
	bool complete = true;
};
struct Plan
{
	Disk::Digest mapKey{}, partitionKey{}, geometryKey{}, trainingSignalKey{}, key{};
	Policy policy;
	std::vector<Geometry> geometry; // bounded geometry only, no target backing buffer
	std::vector<Job> jobs;
	std::array<std::vector<Selection>, size_t(Purpose::Count)> global;
	std::array<std::vector<Selection>, 2> evaluationCoverage; // map-wide geometry-only reserves
	Report report;
	size_t rounds = 0;
};
// Caller assigns immutable roles using its frozen block/footprint-gap manifest.
// This module never reassigns folds or shrinks a filtering gap.
Error Build(Disk::Digest map, Disk::Digest partition, std::vector<Geometry>, const Policy &,
			const ReadContext &, Plan &);
// Every signal ID must belong to Training. Validation/held-out attempts fail.
// Only training selections may change; evaluation geometry is frozen by Build.
Error Refine(const Plan &, const std::vector<Signal> &, const ReadContext &, Plan &,
			 const std::vector<Priority> & = {});
// Weights sum to one per (spatial block, receiver surface). Correlation duplicates
// have one vote; represented stratum area is never a transport/reflector area.
void Normalize(const std::vector<Geometry> &, std::vector<Selection> &);
const char *PurposeName(Purpose);
} // namespace Sampling
} // namespace Core
} // namespace Rend2LightRecon
