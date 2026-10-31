// Coordinated, bounded offline inverse-lighting search. No asset publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "emitter_shape.h"
#include "indirect.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Joint
{
constexpr uint32_t kVersion = 2; // training identity is separate from validation/checkpoint selection
enum class Channel
{
	Surface,
	GridAmbient,
	GridDirected,
	GridDirection,
	DeluxeDirection,
	Count
};
enum class Role
{
	Training,
	Validation,
	Heldout
};
struct Observation
{
	Id id = 0, patch = 0, region = 0, block = 0;
	Bake::Receiver receiver;
	Channel channel = Channel::Surface;
	Amplitude::Interval rgb[3];
	Vec3 directionToSource;
	double measure = 0, sigma = 1, angularSigmaRadians = 1;
	bool comparable = false, directionKnown = false, required = true;
	Role role = Role::Training;
};
struct Candidate
{
	Id id = 0, family = 0, region = 0; // family alternatives are mutually exclusive
	TransportRootKind root = TransportRootKind::LocalEmitter;
	Shape::Candidate shape;
	Bake::SourceHypothesis environment; // sun/sky only; never renderer defaults
	bool initialActive = false, mayRemove = true, fitAmplitude = true;
	bool fitEnvironmentDirection = false;
	double maximumEnvironmentRotationRadians = .5;
	double clearanceWorld = .001;
	std::string provider, provenance;
	std::vector<Id> parents;
};
struct Profile
{
	Shape::Model model;
	Indirect::Contract contract;
	// A discrete, declared calibration hypothesis. Never jointly free gain and RGB.
	double fixedGain = 1;
	std::string calibrationHypothesis;
};
struct Source
{
	Candidate candidate;
	Shape::Emitter value;
	Bake::SourceHypothesis environment;
};
struct Terms
{
	double surface = 0, gridAmbient = 0, gridDirected = 0, gridDirection = 0, deluxe = 0;
	double shadow = 0, geometryPrior = 0, materialPrior = 0, complexity = 0, worstBlock = 0;
	double Total() const
	{
		return surface + gridAmbient + gridDirected + gridDirection + deluxe + shadow + geometryPrior +
			   materialPrior + complexity + worstBlock;
	}
};
struct Group
{
	Channel channel = Channel::Surface;
	Id region = 0, block = 0;
	uint64_t requested = 0, used = 0, positive = 0;
	double measure = 0, loss = 0, squared = 0, blackSquared = 0;
};
struct Prediction
{
	Id id = 0, patch = 0;
	bool available = false, directionKnown = false;
	Vec3 rgb, directionToSource;
	Vec3 direct, sun, sky, ambient, indirect, unresolved, signedDiscrepancy;
	uint32_t issues = 0;
};
struct Work
{
	uint64_t evaluations = 0, forwardCalls = 0, preparationCalls = 0, cacheHits = 0;
	uint64_t rays = 0, traversal = 0, pairs = 0, links = 0, attemptedModels = 0, rejectedModels = 0;
	uint64_t accountedBytes = 0;
	uint64_t arithmetic = 0, linearColumns = 0;
	uint64_t proposalsDeferred = 0;
};
enum class State
{
	NotStarted,
	Converged,
	Partial,
	Unidentifiable,
	Unvalidated,
	InsufficientSignal,
	Unavailable,
	Cancelled,
	Budget,
	NumericalFailure,
	PhysicallyInvalid
};
struct Score
{
	bool available = false, positive = false, requiredBlocksPass = false;
	uint64_t requested = 0, used = 0, unavailable = 0;
	uint64_t disabled = 0, incomparable = 0;
	// Surface RGB only; independent channel/region metrics remain in groups.
	// Objective combines explicitly sigma-normalized weighted terms, never raw
	// angular squared radians with RGB8 squared bytes.
	double intervalMse = 0, blackMse = 0, objective = 0, worstBlockRmse = 0, measure = 0;
	Disk::Digest partitionKey{}, admissionKey{};
	std::vector<Group> groups;
	Work work;
};
struct RootPower
{
	Id source = 0;
	TransportRootKind kind = TransportRootKind::Unknown;
	double primaryAreaWeighted = 0, firstBounceAreaWeighted = 0;
	// Irradiance-domain RGB sums times WORLD area, not unnamed watts/lumens.
	bool available = true;
};
struct Model
{
	Disk::Digest key{}, trainingKey{}, geometryKey{}, policyKey{}, transferKey{};
	size_t profile = 0;
	std::vector<Source> sources;
	std::vector<Indirect::Reflectance> materials;
	std::vector<RootPower> roots;
	Terms training;
	Score validation;
	bool strictReevaluated = false, stationary = false, absoluteScaleKnown = false;
	bool linearAmplitudeBlock = false;
	bool sourceAllocationIdentifiable = false, physicalExistenceCertified = false;
	Amplitude::Spectrum localAmplitudeSpectrum;
	double reflectedPowerFraction = 0;
	Estimate omittedBouncePower; // area-weighted power bound, never per-luxel error
	std::string proposal;
	std::vector<std::string> assumptions;
};
struct Options
{
	size_t beamWidth = 3, rounds = 4, maxModels = 96, maxSources = 16, maxCandidates = 128;
	size_t amplitudeSweeps = 6, amplitudeLineSteps = 12, geometrySweeps = 2, jointSweeps = 2;
	size_t restarts = 1, batchRows = 64, maxObservations = 100000, maxRetained = 24;
	size_t residualCandidates = 4, residualGridDivisions = 3;
	uint64_t seed = 20261030, maxEvaluations = 200000, maxRays = 12000000, maxWork = 1000000000;
	uint64_t maxBytes = 128ull * 1024 * 1024, cacheBytes = 8ull * 1024 * 1024;
	uint64_t timeLimitMilliseconds = 0; // safety only; work budgets give reproducible stops
	double huber = 4, objectiveTolerance = 1e-9, parameterTolerance = 1e-4;
	double positionStepWorld = 4, angularStepRadians = .1, logDimensionStep = .12;
	double complexityPerSource = .001, complexityPerParameter = .0001;
	double equivalentValidationSlack = .01, worstBlockWeight = .25;
	double maximumRequiredBlockRmse = 0; // 0 = no universal threshold; caller freezes scenario gate
	double minimumValidationImprovement = 0;
	double channelWeight[size_t(Channel::Count)] = {1, 0, 0, 0, 0};
	bool geometry = true, materials = true, residualSearch = true, mergeSplit = true;
	bool conditionalUnknownMaterials = false;
	size_t diagnosticSources = 8, diagnosticRows = 128;
	size_t coarseRows = 0; // 0=all; deterministic block-stratified subset, strict stage always all
};
struct Request
{
	Disk::Digest mapKey{}, trainingKey{}, policyKey{};
	std::vector<Profile> profiles;
	std::vector<Candidate> candidates;
	std::shared_ptr<const ChunkSource<Observation>> training;
	uint64_t count = 0;
	std::shared_ptr<const Indirect::Transfer> transfer;
	std::vector<Indirect::Reflectance> materials;
	std::vector<Indirect::Root> boundaryRoots; // immutable, identified ambient/etc.
	Bake::EvaluationContext geometry;
	PointRefine::SceneInput placement;
	Bounds sourceSearchBounds;
	Shape::Sampling coarseSampling, strictSampling;
	std::shared_ptr<const FitEvidence::Objective> shadow;
	double shadowWeight = 0;
	Options options;
};
// A trusted controller may score validation, never final held-out, while search
// sees only aggregate diagnostics. No target/answer reader is exposed to fitting.
class Validation
{
  public:
	virtual ~Validation() = default;
	virtual Disk::Digest Key() const = 0;
	virtual Error Evaluate(const Model &, const ReadContext &, Score &) const = 0;
};
struct Attempt
{
	Disk::Digest key{};
	std::string proposal, reason;
	State state = State::Unavailable;
	double training = 0, validation = 0;
	size_t sources = 0, profile = 0;
};
struct Checkpoint
{
	uint32_t version = kVersion;
	Disk::Digest inputKey{};
	size_t round = 0;
	std::vector<Model> beam, completed;
	std::vector<Attempt> attempts;
	std::vector<Candidate> catalog;
	Work work;
	// Immutable geometry-only blocks. Cooperative in-process resume; no target RGB.
	std::map<Disk::Digest, std::shared_ptr<const Shape::Block>> geometryCache;
	uint64_t cacheBytes = 0;
};
struct Result
{
	State state = State::NotStarted;
	std::string reason;
	std::vector<Model> alternatives; // complete competing models, NEVER sum them
	size_t preferred = SIZE_MAX;
	std::vector<Attempt> attempts;
	Work work;
	Checkpoint checkpoint; // cooperative round boundary, pinned to input identity
};
Error Solve(const Request &, const ReadContext &, Result &, const Validation * = nullptr,
			const Checkpoint *resume = nullptr, size_t roundsThisCall = SIZE_MAX);
// Refit nuisance parameters of one supplied structural hypothesis. Training
// only; no validation selection, hidden answers, asset publication or new IDs.
Error Refit(const Request&,const Model& seed,const ReadContext&,Model&,Work&);
// Strict parameter-value assessment without optimization. Reads training only;
// useful for nuisance profiling and geometry/RGB sensitivity. No held-out reader.
Error AssessHypothesis(const Request&,const Model&,const ReadContext&,Model&,Score&,Work&,
	std::vector<Prediction>* =nullptr);
// Frozen source-linked transport extraction; no parameter fitting or hidden RGB.
// Reuses exactly the strict direct roots and passive operator used by Solve.
Error DecomposeFrozen(const Request&,const Model&,const ReadContext&,Indirect::Result&,Work&);
// Value-only production evaluation; held-out data are accepted ONLY here after
// freezing. Caller supplies geometry/prior Request, not training target access.
Error EvaluateFrozen(const Request &, const Model &, std::shared_ptr<const ChunkSource<Observation>>,
					 uint64_t, Role, const Disk::Digest &partitionKey, const ReadContext &, Score &,
					 std::vector<Prediction> * = nullptr, Work * = nullptr);
const char *StateName(State);
} // namespace Joint
} // namespace Core
} // namespace Rend2LightRecon
