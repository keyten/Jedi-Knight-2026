// Geometry-constrained offline point XYZ refinement. No runtime light publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "amplitude_fit.h"
#include "fit_evidence.h"
#include "regions.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace PointRefine
{
constexpr uint32_t kVersion = 1;
enum class Constraint
{
	Free,
	Solid,
	Outside,
	UnknownGeometry,
	Clearance,
	OpaquePath,
	UnknownPath,
	Dynamic,
	Mount,
	SearchBounds,
	Count
};
struct PlacementResult
{
	Constraint state = Constraint::UnknownGeometry;
	Id region = 0;
	bool regionKnown = false;
	uint64_t work = 0, rays = 0;
};
class PlacementProvider
{
  public:
	virtual ~PlacementProvider() = default;
	virtual Disk::Digest MapKey() const = 0;
	virtual Disk::Digest GeometryKey() const = 0;
	virtual Disk::Digest PolicyKey() const = 0;
	// Endpoint AND complete swept path for the enclosing radius cube. Initial
	// certification uses from==to. Free is a certificate, not a few point samples.
	virtual Error Check(Vec3 from, Vec3 to, double radius, const ReadContext &, PlacementResult &) const = 0;
};
struct SceneInput
{
	LightmapMapIdentity map;
	std::shared_ptr<const RegionDomain> domain;
	Disk::Digest domainKey{};
	std::shared_ptr<const StaticVisibility> geometry;
	std::shared_ptr<const LightingRegionGraph> regions;
	std::vector<DynamicBoundary> dynamic;
	uint64_t maxWorkPerCheck = 1048576;
	size_t maxSegments = 256;
	double minimumSegmentWorld = .125;
};
// Reuses BSP/visual-box certification and real CPU rays, not PVS/cluster IDs.
Error MakePlacement(SceneInput, const ReadContext &, std::shared_ptr<const PlacementProvider> &);
enum class PriorKind
{
	Inferred,
	AuthoredFixture,
	Explicit
};
struct PositionPrior
{
	bool known = false;
	Vec3 meanWorld, sigmaWorld = Vec3(32, 32, 32);
	double weight = 0;
	PriorKind kind = PriorKind::Inferred;
	std::string provenance;
};
struct Mount
{
	bool known = false;
	Vec3 anchorWorld, normalWorld;
	double minimumOffsetWorld = 0, maximumOffsetWorld = 1;
};
struct Point
{
	Id source = 0;
	Bounds bounds; // hard maximum search domain; fixed world coordinates
	double initialRadiusWorld = 64, maximumRadiusWorld = 64;
	double sourceSupportRadiusWorld = 0,
		   clearanceWorld = .001; // placement only, NOT baker finite-radius expansion
	PositionPrior prior;
	Mount mount;
};
struct DirectionEvidence
{
	Id id = 0, source = 0, surface = 0;
	Vec3 originWorld, toSource;
	double halfAngleRadians = 0, weight = 0;
	bool isolatedSourceAttested = false, profileValidated = false, originKnownFree = false;
	std::string provenance;
};
struct Options
{
	double initialStepWorld = 16, minimumStepWorld = .05, relativeImprovement = 1e-9;
	double diagnosticStepWorld = 1, feasibleObjectiveSlack = .01;
	size_t maxSweeps = 160, maxFits = 1200, multiStarts = 1, maxPoints = 8;
	size_t diagnosticRows = 128, maxDiagnosticFits = 24;
	uint64_t maxBytes = 96ull * 1024 * 1024, maxAmplitudeEvaluations = 200000;
	uint64_t maxPairs = 200000000, maxRays = 4000000, maxWork = 128000000;
	bool expand = false, parabolicAcceleration = true, diagnostics = true, balanceRegions = true;
	uint64_t seed = 20261029;
};
struct Request
{
	Amplitude::Request lighting; // TRAINING stream only; S06 proof/nonlinear policy retained
	std::vector<Point> points;
	std::shared_ptr<const PlacementProvider> placement;
	std::vector<DirectionEvidence> directions; // isolated/profile-validated point cones only
	Options options;
	std::shared_ptr<const FitEvidence::Objective> shadow;
	double shadowWeight = 0;
};
enum class Stop
{
	NotStarted,
	PatternStationary,
	IterationLimit,
	Uninformative,
	Unavailable,
	Cancelled,
	WorkLimit,
	NumericalFailure
};
struct Work
{
	uint64_t fits = 0, assessments = 0, amplitudeEvaluations = 0, pairs = 0, rays = 0, traversal = 0;
	uint64_t placementChecks = 0, cacheHits = 0, cacheInvalidations = 0, accepted = 0, expansions = 0;
	uint64_t visibilityChanges = 0, coverageRejected = 0, rejected[size_t(Constraint::Count)] = {};
	uint64_t accountedBytes = 0;
};
struct Iteration
{
	size_t evaluation = 0, sweep = 0;
	Id source = 0;
	Vec3 position;
	double stepWorld = 0, data = 0, prior = 0, directional = 0, shadow = 0, total = 0;
	bool accepted = false, visibilityChanged = false, expansion = false;
};
struct AxisRange
{
	bool sampled = false, limitedByGeometry = false, limitedBySearch = false;
	double minimum = 0, maximum = 0; // envelope of tested acceptable coordinates, NOT a CI
};
struct PointResult
{
	Id source = 0, region = 0;
	Vec3 initialWorld, world;
	bool regionKnown = false, locallyIdentifiable = false, boundaryConstrained = false;
	AxisRange sampledRange[3];
	std::string uncertainty;
};
struct Information
{
	bool available = false, conditionKnown = false, stableVisibility = false, stepStable = false;
	size_t rank = 0, parameters = 0, rows = 0;
	size_t amplitudeRank = 0, amplitudeParameters = 0;
	std::vector<double> amplitudeSingular;
	double condition = 0, relativeRankThreshold = 1e-4, stepWorld = 0;
	double maximumDerivativeRelativeChange = 0; // h vs h/2; diagnostic, not a gradient guarantee
	std::vector<double> singular;
	std::string scope;
};
struct Result
{
	bool refined = false, converged = false, strictReevaluated = false;
	Stop stop = Stop::NotStarted;
	Disk::Digest key{}, admissionKey{};
	std::vector<PointResult> points;
	Amplitude::Result amplitude;
	double initialObjective = 0, objective = 0, positionPrior = 0, directionalObjective = 0;
	double shadowObjective = 0, shadowWeight = 0;
	FitEvidence::Value shadowDiagnostics;
	FitEvidence::Value shadowReference;
	Work work;
	Information information;
	std::vector<Iteration> trace;
	std::vector<std::string> diagnostics;
};
Error Refine(const Request &, const ReadContext &, Result &);
const char *StopName(Stop);
const char *ConstraintName(Constraint);
} // namespace PointRefine
} // namespace Core
} // namespace Rend2LightRecon
