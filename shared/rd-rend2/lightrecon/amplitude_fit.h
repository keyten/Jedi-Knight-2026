// Fixed-geometry amplitude fitting; no source discovery or renderer publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_forward.h"
#include <map>
namespace Rend2LightRecon
{
namespace Core
{
namespace Amplitude
{
constexpr uint32_t kVersion = 1;
enum class Type
{
	SurfaceEncoded,
	SurfaceRelative,
	GridAmbientRelative,
	GridDirectedRelative,
	Count
};
enum class Loss
{
	Squared,
	Huber,
	Cauchy
};
enum class Method
{
	Automatic,
	LinearOnly,
	NonlinearOnly
};
enum class Stop
{
	NotStarted,
	IntervalFeasible,
	ProjectedKkt,
	ParameterStep,
	Stagnated,
	IterationLimit,
	WorkLimit,
	Cancelled,
	Unsupported,
	NumericalFailure
};
struct Interval
{
	bool present = true;
	double lower = 0, upper = 0;
	bool censored = false;
};
// Truncating compiler RGB8 in OUTPUT space. 255 has no upper constraint; shared
// peak normalization still operates jointly on the complete predicted RGB.
Interval Rgb8(uint8_t);
struct Observation
{
	Id id = 0, region = 0;
	Bake::Receiver receiver;
	Type type = Type::SurfaceEncoded;
	Interval channels[3];
	double measure = 0, reliability = 1, sigma = 1;
	// Surface measure is actual covered world area; grid measure is an explicitly
	// chosen volume/sample weight. They are normalized separately by Type.
	bool eligible = true, encodingAttested = false, primariesComparable = false;
	bool comparisonScaleKnown = false;
	double predictionScale = 1; // explicit observation-specific calibration, never fitted
	Vec3 knownPreencoding;		// fixed surface terms, BEFORE encoding; no arbitrary RGB residual
};
struct Prior
{
	Vec3 softLower, softUpper;
	double intervalWeight = 0;
	Chromaticity colour;
	double colourWeight = 0; // quadratic orthogonal-to-colour penalty, not RGB/I gauge
};
struct Candidate
{
	Bake::SourceHypothesis source; // XYZ/type/orientation immutable; relative-Q fitting only
	Vec3 lowerQ, upperQ = Vec3(1200, 1200, 1200), initialQ = Vec3(64, 64, 64);
	Prior prior;
};
struct Options
{
	Method method = Method::Automatic;
	Loss loss = Loss::Huber;
	double huber = 4, ridge = 0, projectedKktTolerance = 1e-7, objectiveTolerance = 1e-10;
	double parameterTolerance = 1e-4, relativeImprovement = 1e-10, strictTolerance = 1e-3;
	double typeWeight[size_t(Type::Count)] = {1, 1, 0, 0}; // grid is explicit opt-in
	// Optional ONE bounded surface-only nuisance baseline, penalized toward zero.
	Vec3 baselineUpper;
	double baselinePenalty = 0;
	size_t batchRows = 128, maxSources = 64, maxIterations = 200, maxEvaluations = 10000;
	size_t nonlinearPasses = 24, lineSearchSteps = 22, restarts = 2, diagnosticParameters = 24;
	uint64_t maxRows = 1000000, maxBytes = 64ull * 1024 * 1024, maxCacheBytes = 8ull * 1024 * 1024;
	uint64_t maxPairs = 100000000, maxRays = 2000000, maxTraversal = 64000000, maxArithmetic = 200000000;
	bool diagnostics = true;
};
struct Cache
{
	// Geometry only; no target RGB. Sequential caller ownership, never global.
	std::map<Disk::Digest, std::shared_ptr<const Bake::PreparedFactors>> blocks;
	uint64_t bytes = 0;
	void Clear()
	{
		blocks.clear();
		bytes = 0;
	}
};
struct Request
{
	Disk::Digest mapKey{}, observationKey{};
	Bake::ProfileHypothesis profile;
	std::vector<Candidate> candidates;
	std::shared_ptr<const ChunkSource<Observation>> observations;
	uint64_t count = 0;
	Bake::EvaluationContext geometry;
	Bake::Options forward;
	Options options;
	std::shared_ptr<Cache> cache;
};
struct Work
{
	uint64_t objectiveEvaluations = 0, forwardCalls = 0, prepareCalls = 0, passes = 0, rowReads = 0;
	uint64_t pairs = 0, rays = 0, traversal = 0, cacheHits = 0, arithmetic = 0, accountedBytes = 0;
};
struct Coverage
{
	uint64_t requested = 0, used = 0, channels = 0, censored = 0, positive = 0;
	uint64_t missing = 0, encoding = 0, geometry = 0, visibility = 0, calibration = 0, disabled = 0;
	double measure[size_t(Type::Count)] = {};
};
struct Terms
{
	double data = 0, ridge = 0, sourcePrior = 0, colourPrior = 0, baselinePrior = 0;
	double Total() const { return data + ridge + sourcePrior + colourPrior + baselinePrior; }
};
struct GroupTerms
{
	Id region = 0;
	Type type = Type::SurfaceEncoded;
	double objective = 0, weightedSquared = 0, measure = 0;
	uint64_t channels = 0;
};
struct Slice
{
	bool available = false;
	double lower = 0, upper = 0;
	bool upperIsSearchLimit = false; // conditional coordinate feasibility, NEVER joint CI
};
struct SourceResult
{
	Id source = 0;
	Vec3 compilerQ;
	double peakQ = 0;
	Vec3 peakNormalizedColour;
	bool zeroResponse = false, allocationAmbiguous = true, absoluteScaleKnown = false;
	Slice conditional[3];
	double removalObjectiveIncrease = 0; // fixed-other ablation, not existence probability
	bool removalAvailable = false;
	Prior prior; // retained assumptions; original source calibration remains in Request
};
struct Spectrum
{
	bool available = false, conditionKnown = false;
	size_t rank = 0, parameters = 0;
	double condition = 0, relativeThreshold = 1e-6;
	std::vector<double> singular;
	std::string scope;
};
struct AllocationExchange
{
	Id first = 0, second = 0;
	uint32_t channel = 0;
	double secondPerFirst = 0, lowerStep = 0, upperStep = 0;
	// Qfirst += step; Qsecond -= secondPerFirst*step. Numerically verified
	// proportional linear columns; no calibrated joint probability/CI.
};
struct Result
{
	bool fitted = false, converged = false, linear = false, strictReevaluated = false;
	Stop stop = Stop::NotStarted;
	Disk::Digest key{}, profileKey{};
	std::vector<SourceResult> sources;
	Vec3 baseline;
	Coverage coverage;
	Terms objective;
	Spectrum spectrum;
	Work work;
	double initialObjective = 0, maximumIntervalError = 0, projectedKkt = 0, strictDifference = 0;
	std::vector<double> history;
	std::vector<GroupTerms> groups;
	std::vector<AllocationExchange> exchanges;
	bool projectedKktAvailable = false;
	std::vector<std::string> diagnostics;
};
// Synchronous, deterministic streams. Reads TRAINING observations only. No files,
// hidden answers, held-out selection, XYZ update or canonical asset promotion.
// Failed calls return diagnostics/work only; fitted=false, no partial Q publication.
Error Fit(const Request &, const ReadContext &, Result &);
const char *StopName(Stop);
} // namespace Amplitude
} // namespace Core
} // namespace Rend2LightRecon
