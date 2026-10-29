// Offline extended-emitter models and fitting. No renderer publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "amplitude_fit.h"
#include "point_refine.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Shape
{
constexpr uint32_t kVersion = 1;
enum class Law
{
	HistoricalBake,
	DiffuseIntegral
};
enum class Output
{
	RelativeIncident,
	EncodedRgb8,
	PhysicalLux
};
struct Model
{
	Law law = Law::HistoricalBake;
	Output output = Output::EncodedRgb8;
	Bake::ProfileHypothesis profile;
	Units units;
	Calibration calibration; // SI requires PhysicallyAuthored; relative is never cd/nits
	bool allowAssumed = false, allowFacetExpansion = false;
	bool linearRec709PrimariesAttested = false; // never inferred from fitted historical Q
	// Explicit historical subdivision recipe; changing it changes the MODEL.
	unsigned rectangleDivisions = 1, tubeSides = 16, tubeSegments = 4;
	double historicalEmitterPushWorld = 1;
	std::string provenance;
};
struct Emitter
{
	PhysicalEmitter geometry; // canonical centre/frame: rectangle emits forward; tube axis=right
	Vec3 rgbAmplitude;		  // historical Q OR linear on-axis intensity / emitted radiance, per Model
	// Historical angular law has these parameters, NOT a runtime smooth cone.
	double spotTargetDistanceWorld = 64, spotRadiusWorld = 32;
	Id emittingSurface = 0; // optional endpoint-only exclusion, never a general transparent surface
};
struct Sampling
{
	unsigned areaDivisions = 4, tubeSides = 16, tubeSegments = 4;
	unsigned receiverSubdivisions = 0;
	bool compareRefined = true;
	double receiverOffsetWorld = .001, endpointToleranceWorld = .001;
	double minimumDistanceWorld = .0001;
	uint64_t maxBytes = 32ull * 1024 * 1024, maxRays = 1000000, maxWork = 32000000;
};
struct Work
{
	uint64_t blocks = 0, values = 0, pairs = 0, rays = 0, traversal = 0, nodes = 0;
};
struct Prediction
{
	Id observation = 0;
	bool available = false, conditional = true;
	Vec3 value;
	double quadratureDifference = 0; // nested-rule discrepancy, NOT a certified total error
	uint32_t visibilityIssues = 0;
};
class Block
{
  public:
	struct Data;
	const Disk::Digest &Key() const;
	uint64_t Bytes() const;
	const Work &PreparationWork() const;
	// Checks the whole declared RGB box, including FAST and coupled encoding.
	bool Linear(Vec3 lower, Vec3 upper, size_t source) const;
	bool AllLinear(const std::vector<Vec3> &lower, const std::vector<Vec3> &upper) const;
	double Coefficient(size_t receiver, size_t source) const;
	Error Evaluate(const std::vector<Vec3> &rgb, const ReadContext &, std::vector<Prediction> &,
				   Work &) const;

  private:
	std::shared_ptr<const Data> data;
	friend Error Prepare(const Model &, const std::vector<Emitter> &, const Chunk<Bake::Receiver> &,
						 const Bake::EvaluationContext &, const Sampling &, const ReadContext &,
						 std::shared_ptr<const Block> &);
};
// Geometry-only preparation; neither target RGB nor held-out buffers are accepted.
Error Prepare(const Model &, const std::vector<Emitter> &, const Chunk<Bake::Receiver> &,
			  const Bake::EvaluationContext &, const Sampling &, const ReadContext &,
			  std::shared_ptr<const Block> &);

enum class ShapeEvidence
{
	None,
	Inferred,
	VisibleFixture,
	Authored
};
struct Candidate
{
	Emitter initial;
	Bounds centreBounds;
	ShapeEvidence evidence = ShapeEvidence::None;
	std::string provenance;
	bool fitPosition = true, fitFrame = true, fitDimensions = true, fitCone = true;
	bool directionalEvidence = false;
	double maximumTranslationWorld = 16, maximumRotationRadians = .25;
	double minimumDimensionWorld = .5, maximumDimensionWorld = 128;
	double minimumOuterRadians = .08, maximumOuterRadians = 1.55;
	Vec3 lowerRgb, upperRgb = Vec3(1200, 1200, 1200);
	double positionSigmaWorld = 16, angleSigmaRadians = .25, logSizeSigma = .25;
	double geometryPriorWeight = 0; // explicit; evidence does not manufacture a confidence
};
enum class Role
{
	Training,
	Validation,
	Heldout
};
struct Observation
{
	Id id = 0, spatialBlock = 0;
	Bake::Receiver receiver;
	Amplitude::Interval channel[3];
	double area = 1, sigma = 1;
	bool comparable = false;
	Role role = Role::Training;
};
struct Options
{
	size_t batchRows = 64, maxRows = 100000, maxTrials = 1600, maxSweeps = 80, maxAmplitudeIterations = 100;
	uint64_t maxCacheBytes = 8ull * 1024 * 1024, maxBytes = 64ull * 1024 * 1024;
	uint64_t maxRays = 12000000, maxWork = 300000000, maxValues = 500000;
	double positionStepWorld = 4, rotationStepRadians = .08, logSizeStep = .12, coneStep = .08;
	double minimumStepFraction = .01, relativeImprovement = 1e-9, kktTolerance = 1e-6;
	double huber = 4, ridge = 0;
	bool parabolicProposals = true, angularStarts = true;
};
struct Request
{
	Disk::Digest mapKey{}, observationsKey{};
	Model model;
	std::vector<Candidate> candidates; // fixed IDs/count per alternative model
	std::shared_ptr<const ChunkSource<Observation>> training;
	uint64_t count = 0;
	Bake::EvaluationContext geometry;
	PointRefine::SceneInput placement; // immutable BSP + visual geometry; region membership optional
	Sampling searchSampling, strictSampling;
	Options options;
};
enum class Stop
{
	NotStarted,
	PatternStationary,
	SweepLimit,
	Uninformative,
	Cancelled,
	Budget,
	Unavailable
};
struct Iteration
{
	size_t trial = 0, sweep = 0;
	double objective = 0, prior = 0;
	bool accepted = false;
};
struct FitResult
{
	bool fitted = false, converged = false, strictReevaluated = false, amplitudeConverged = false;
	bool linearAmplitudes = false, absoluteScaleKnown = false;
	bool projectedKktAvailable = false;
	Stop stop = Stop::NotStarted;
	Model model;
	std::vector<Emitter> emitters; // Candidate state only, never a resolved runtime asset
	std::vector<Iteration> trace;
	Work work;
	uint64_t trials = 0, rejectedGeometry = 0, rejectedCoverage = 0, accountedBytes = 0;
	size_t parameters = 0;
	double initialObjective = 0, objective = 0, projectedKkt = 0, strictDifference = 0;
	bool preciseAngularClassification =
		false; // remains false without independently identified angular evidence
	Disk::Digest key{}, trainingIdentity{}, mapKey{}, geometryKey{};
	std::vector<std::string> limitations;
};
Error Fit(const Request &, const ReadContext &, FitResult &);
// Caller freezes parameters before using validation/held-out observations.
struct Score
{
	bool available = false, positive = false;
	double intervalMse = 0, blackMse = 0, penalty = 0, penalized = 0, maximumError = 0,
		   quadratureDifference = 0;
	uint64_t requested = 0, comparable = 0, unknown = 0;
	size_t spatialBlocks = 0, parameters = 0;
	Role role = Role::Validation;
	Disk::Digest identity{};
	Work work;
};
Error EvaluateFrozen(const FitResult &, std::shared_ptr<const ChunkSource<Observation>>, uint64_t, Role,
					 const Bake::EvaluationContext &, const Sampling &, double complexityPerParameter,
					 const ReadContext &, Score &);
struct Selection
{
	enum class State
	{
		Unavailable,
		InsufficientSignal,
		Alternatives,
		Discriminated
	} state = State::Unavailable;
	std::vector<size_t> retained;
	size_t preferred = SIZE_MAX; // minimum penalized validation score, not verified source type
	std::string reason;
};
// Trusted-controller validation decision only; scores must share exact IDs/targets/weights.
Error Select(const std::vector<FitResult> &, const std::vector<Score> &validation,
			 double comparableScoreSlack, Selection &);
const char *StopName(Stop);
} // namespace Shape
} // namespace Core
} // namespace Rend2LightRecon
