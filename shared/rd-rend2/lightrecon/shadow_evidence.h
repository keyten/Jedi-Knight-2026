// Automatic baked-gradient / caster / source hypotheses. CPU only; no verified-edge promotion.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_forward.h"
#include "fit_evidence.h"
#include "regions.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Shadow
{
constexpr uint32_t kVersion = 1;
enum Alternative : uint32_t
{
	CastShadow = 1u << 0,
	MaterialEdge = 1u << 1,
	NormalEdge = 1u << 2,
	ChartSeam = 1u << 3,
	SaturationEdge = 1u << 4,
	SpotCone = 1u << 5,
	IndirectTransition = 1u << 6,
	OverlappingSources = 1u << 7,
	BakeFiltering = 1u << 8,
	UnknownTransfer = 1u << 9,
	MissingGeometry = 1u << 10,
	MovingCaster = 1u << 11,
	OpticalAmbiguity = 1u << 12,
	Quantization = 1u << 13,
	SmoothFalloff = 1u << 14
};
struct Sample
{
	enum class Role
	{
		Training,
		Validation,
		Heldout
	};
	Role role = Role::Training;
	Id id = 0, chart = 0, material = 0;
	uint32_t page = 0, x = 0, y = 0;
	Bake::Receiver receiver;
	Vec3 rgb; // original SOURCE-DECODED or explicitly known compiler-linear relative RGB
	double sigmaRgb = 0, mappingErrorWorld = 0, knownFilterRadiusWorld = 0;
	bool sourceLinear = false, primariesComparable = false, filterKnown = false;
	uint32_t issues = 0; // Alternative flags; all original raw data remain elsewhere
};
struct Gradient
{
	Id id = 0, first = 0, second = 0;
	Vec3 world, normal, tangent, deltaRgb;
	double separationWorld = 0, contrast = 0, noise = 0;
	uint32_t alternatives = 0;
	bool geometricallyComparable = false;
};
struct Edge
{
	Id id = 0, surface = 0, chart = 0, material = 0;
	Bake::Receiver receiver; // canonical unchanged receiver plane at observed edge
	Vec3 world, tangent, normal, deltaRgb;
	double extentWorld = 0, locationSigmaWorld = 0, transitionWidthWorld = 0;
	bool widthKnown = false, usable = false;
	uint32_t alternatives = 0;
	Estimate castShadowConfidence; // unknown until calibrated; NOT a product of heuristics
	std::vector<Id> gradients, samples;
	std::string provenance;
};
struct DetectionOptions
{
	double minimumRelativeContrast = .15, minimumSigma = 3, maximumNormalAngle = .05;
	double smoothSlopeFactor = 2;
	size_t maxSamples = 262144, maxGradients = 65536, maxEdges = 4096;
	uint64_t maxBytes = 64ull * 1024 * 1024, maxWork = 4000000;
};
struct Evidence
{
	Disk::Digest map{}, profileAssumption{}, identity{};
	std::vector<Sample> samples;
	std::vector<Gradient> gradients; // observed photometric facts, including excluded interpretations
	std::vector<Edge> edges;		 // POSSIBLE interpretations, never verified shadows
	uint64_t adjacencyTests = 0, smoothRejected = 0, invalidPairs = 0, work = 0;
};
Error Detect(Disk::Digest map, Disk::Digest profileAssumption, const Chunk<Sample> &,
			 const DetectionOptions &, const ReadContext &, std::shared_ptr<const Evidence> &);

enum class Scope
{
	CentreVisibility,
	PhysicalFootprint,
	CompilerScoped,
	OptimizationSurrogate
};
struct Policy
{
	Scope scope = Scope::PhysicalFootprint;
	Bake::ProfileHypothesis profile;
	Units units;
	// Source-dependent geometry is always retraced. These are explicit source/receiver
	// quadrature rules, not a universal baker blur or barycentric tolerance.
	unsigned sourceSamples = 4, receiverSamples = 2;
	double receiverPushWorld = .001, searchRadiusWorld = 128, associationSlack = .25;
	double edgeWeight = 1, tangentWeight = .25, patternWeight = .5, widthWeight = .25, colourWeight = .1;
	double nullCost = 4, huber = 2, minimumPatternChange = .2;
	size_t maxEdges = 4096, maxSources = 16, maxLocalTriangles = 8192, maxAssociationsPerEdge = 8;
	size_t maxAssociationAttemptsPerEdge = 2048;
	uint64_t maxRays = 200000, maxWork = 16000000, maxBytes = 64ull * 1024 * 1024;
	bool requireUsableEvidence = false;
	std::string provenance;
};
struct Caster
{
	Id id = 0, surface = 0, primitive = 0;
	Vec3 a, b, normal;
	ObstacleKind optical = ObstacleKind::Unsupported;
	double geometryErrorWorld = 0;
};
Id CasterId(Id surface, Vec3 a, Vec3 b); // stable geometry-scoped endpoints; no ground-truth IDs
struct OracleBinding
{
	Id edge = 0, source = 0, caster = 0;
}; // reference ablation only
struct Context
{
	std::shared_ptr<const Evidence> observations;
	std::shared_ptr<const StaticVisibility> visibility;
	std::shared_ptr<const Bake::CompilerTrace> compiler;
	std::vector<DynamicBoundary> moving;
	Policy policy;
	std::vector<OracleBinding> oracle; // empty = AUTOMATIC, never implicitly supplied
};
struct Prediction
{
	bool available = false, visibilityKnown = false;
	Vec3 edgeWorld, tangent;
	double signedEdgeDistanceWorld = 0, predictedWidthWorld = 0;
	double visibleLow = 0, visibleHigh = 0, integratedVisibility = 0, unitDirectResponse = 0;
	double numericalDifference = 0; // estimator discrepancy, not a guaranteed bound
	bool exactPointFootprint = false, finiteSourceApproximation = false;
	uint32_t issues = 0;
};
enum class MatchState
{
	Candidate,
	UnknownGeometry,
	WrongOccluder,
	WrongPattern,
	OutsideSupport,
	Unassigned
};
struct Association
{
	Id edge = 0, source = 0, caster = 0, casterSurface = 0;
	MatchState state = MatchState::Unassigned;
	Scope scope = Scope::PhysicalFootprint;
	Prediction prediction;
	double positionLoss = 0, patternLoss = 0, widthLoss = 0, colourLoss = 0, total = 0;
	bool oracle = false, retained = false;
};
struct Family
{
	Id edge = 0, source = 0, caster = 0;
	Vec3 planeNormal, planeAnchor; // source lies in an uncertainty band around this PLANE
	double receiverBandWorld = 0;
	bool finiteOrderingChecked = false;
};
struct Report
{
	FitEvidence::Value value;
	std::vector<Caster> casters;
	std::vector<Association> attempted; // includes wrong/unknown/rejected and competing associations
	std::vector<Family> families;
	uint64_t rawEdges = 0, eligibleEdges = 0, unassigned = 0, unknown = 0, rejected = 0, localTriangles = 0;
	uint64_t associationAttempts = 0, omittedDiagnostics = 0;
	Scope scope = Scope::PhysicalFootprint;
	bool oracle = false, absolutePhotometryKnown = false;
};
class Objective final : public FitEvidence::Objective
{
  public:
	static Error Build(Context, const ReadContext &, std::shared_ptr<const Objective> &);
	Disk::Digest Key() const override;
	Disk::Digest MapKey() const override;
	Disk::Digest GeometryKey() const override;
	Error Evaluate(const std::vector<FitEvidence::Source> &, const ReadContext &,
				   FitEvidence::Value &) const override;
	Error Inspect(const std::vector<FitEvidence::Source> &, const ReadContext &, Report &) const;
	Error EvaluateReference(const std::vector<FitEvidence::Source> &, const ReadContext &,
							FitEvidence::Value &) const override;
	Error Probe(const FitEvidence::Source &, const Bake::Receiver &, const ReadContext &, Prediction &) const;
	Error DebugTsv(const Report &, size_t maxBytes, std::string &) const;
	Error DebugObj(const Report &, size_t maxBytes, std::string &) const;

  private:
	struct Data;
	std::shared_ptr<const Data> data;
};
} // namespace Shadow
} // namespace Core
} // namespace Rend2LightRecon
