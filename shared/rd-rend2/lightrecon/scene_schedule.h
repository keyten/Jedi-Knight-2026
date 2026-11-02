// Bounded offline scene orchestration. No renderer counts, GL or hidden targets.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "producer.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace SceneSchedule
{
constexpr uint32_t kVersion = 1;
enum class Status
{
	Pending,
	Produced,
	NoSupportedEvidence,
	Partial,
	BudgetLimited,
	Unsupported,
	InsufficientValidation,
	Unidentifiable,
	CompleteScopedFit,
	Cancelled,
	Failed
};
struct Limits
{
	size_t maxUnits = 4096, samplesPerUnit = 64, candidatesPerUnit = 12;
	size_t maxCatalogCandidates = 512, newCandidatesPerWave = 4, wavesPerBranch = 8;
	size_t maxInfluenceLinks = 8192, influenceSamples = 3, maxNeighbourLinks = 16384;
	size_t maxGILinks = 65536;
	uint64_t maxBytes = 256ull << 20, maxRays = 16000000, maxWork = 1000000000;
	uint64_t maxForwardCalls = 500000, wallMilliseconds = 0;
	bool publish = false; // final callback is never reachable from local/selection work
};
struct Unit
{
	Id id = 0;
	Disk::Digest map{}, geometry{}, profile{}, generation{};
	Hidden::SearchArea area;
	std::vector<Id> training, neighbours;
	size_t branch = 0, candidateBudget = 0;
	uint64_t rayBudget = 0, workBudget = 0, byteBudget = 0;
	Status status = Status::Pending;
	uint64_t candidates = 0, rays = 0, work = 0, forwardCalls = 0;
	bool searchComplete = false, cached = false;
	std::string reason;
};
struct Influence
{
	Id candidate = 0, unit = 0;
	uint64_t visible = 0, blocked = 0, unknown = 0;
	// Diagnostic centre rays, never a pruning mask or finite-source certificate.
};
struct Work
{
	uint64_t localCalls = 0, cacheHits = 0, jointCalls = 0, waves = 0, rays = 0, work = 0,
			 forwardCalls = 0;
	uint64_t candidatesGenerated = 0, candidateInputsDeferred = 0, candidatesUnsupported = 0;
	uint64_t accountedBytes = 0, neighbourLinks = 0, influenceLinks = 0;
	bool influenceComplete = true, neighboursComplete = true;
	Joint::Work fitting;
};
struct BranchResult
{
	Disk::Digest identity{}, catalogKey{};
	std::string profile, scenario;
	Joint::Result fit;
	Candidates::Report consolidation;
	uint64_t models = 0, offered = 0, deferred = 0, waves = 0;
	uint64_t omittedAttempts = 0;
	std::vector<Joint::Attempt> attempts; // bounded summaries, including rejected wave models
	Status status = Status::Pending;
	std::vector<std::string> diagnostics;
};
// Each branch must be an independently scoped one-profile/one-solar scenario
// produced by Producer::PrepareModelRequests (or explicit caller priors).
// No Hidden/Solar provider invocation is allowed inside a branch publication.
struct Request
{
	std::vector<Producer::Request> branches;
	Hidden::Request discovery; // training-only pool; sharded before Hidden::Generate
	Limits limits;
	bool observationPoolComplete = false;
};
class Validation
{
  public:
	virtual ~Validation() = default;
	virtual Disk::Digest Key() const = 0;
	virtual Error Assess(const Joint::Request &, const Joint::Model &, const ReadContext &,
						 Joint::Score &) const = 0;
	// Public validation receiver geometry only; never final targets or RGB.
	virtual bool BlockBounds(Id, Bounds &) const
	{
		return false;
	}
};
class FinalEvaluation
{
  public:
	virtual ~FinalEvaluation() = default;
	virtual Disk::Digest Key() const = 0;
	virtual Error Assess(const Joint::Request &, const Joint::Model &, const ReadContext &,
						 Producer::FinalReceipt &) const = 0;
};
class Checkpoint; // immutable cooperative continuation, with no held-out target buffers
class LocalCache; // immutable complete searches or completed no-evidence admission decisions
struct Result
{
	Status status = Status::Pending;
	std::string reason;
	Disk::Digest graphKey{};
	std::vector<Unit> units;
	std::vector<BranchResult> branches;
	std::vector<Influence> influence;
	Work work;
	size_t preferred = SIZE_MAX;
	std::vector<size_t> equivalentBranches; // competing full models; never averaged/summed
	bool localCoverageComplete = false, candidateCoverageComplete = false, finalEvaluated = false;
	Producer::Result publication;
	std::shared_ptr<const Checkpoint> checkpoint;
	std::shared_ptr<const LocalCache> cache;
};
// unitsThisCall is a cooperative yield budget, not a scene truncation. Completed
// jobs are not rerun on resume. Optimizer-setting changes invalidate checkpoint
// but can reuse cache when local geometry/training/profile inputs are identical.
Error Run(const Request &, const ReadContext &, Result &, const Validation * = nullptr,
		  const FinalEvaluation * = nullptr, std::shared_ptr<const Checkpoint> resume = {},
		  std::shared_ptr<const LocalCache> cache = {}, size_t unitsThisCall = SIZE_MAX);
const char *StatusName(Status);
std::string ReportJson(const Result &);
} // namespace SceneSchedule
} // namespace Core
} // namespace Rend2LightRecon
