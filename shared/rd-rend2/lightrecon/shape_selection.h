// Offline modern representation priors. Neither source truth nor an asset writer.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "emitter_shape.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace ShapeSelection
{
constexpr uint32_t kVersion = 1;
enum class Status
{
	Supported,
	Approximation,
	Unknown,
	Rejected
};
enum class Reason
{
	SmallAperture,
	ExtendedArea,
	FlatStrip,
	CylindricalBody,
	HoodDirectional,
	ConeCue,
	NoDirectionalEvidence,
	BodyUnavailable,
	NonRectangular,
	GeometryUnavailable,
	PlacementRejected,
	MaskUnknown,
	PointCollapsesExtent,
	ConeModelAssumed,
	HistoricalShapeAmbiguous,
	NotHistoricalTruth,
	MountOnly
};
struct Policy
{
	double pointExtentWorld = 4, stripAspect = 6, rectangleCoverage = .99999;
	uint64_t maxWork = 1048576;
};
struct Request
{
	Id fixture = 0;
	PointRefine::SceneInput scene;
	std::vector<uint32_t> emittingSurfaces, hoodSurfaces; // exact original ordinals
	bool fullSurfaceEmissionAttested =
		false; // geometry alone does not recover a luminous texture mask
	std::string emissionProvenance;
	bool coneCueKnown = false;
	double coneHalfAngleRadians = 0;
	std::string cueProvenance;
	// Optional measured/author-attested cylinder, with pinned original mounting
	// contract. No planar aspect-ratio heuristic invents its radius or stand-off.
	bool tubeBodyKnown = false;
	Shape::Emitter tubeBody;
	std::string tubeBodyProvenance;
	Policy policy;
};
struct Rival
{
	EmitterKind kind = EmitterKind::Point;
	Status status = Status::Unknown;
	Shape::Emitter emitter;
	unsigned preference = 100; // ordinal geometry prior, NOT a probability/RGB penalty
	std::vector<Reason> reasons;
	std::vector<std::string> uncertainties;
	SourceSupport::Result placement;
};
struct Result
{
	Disk::Digest key{}, map{}, geometry{}, placementPolicy{};
	Id fixture = 0;
	bool primitiveKnown = false, emittingGeometryExact = false;
	std::vector<uint32_t> requestedSurfaces;
	double emittingAreaWorldSquared = 0, widthWorld = 0, heightWorld = 0, aspect = 0, coverage = 0;
	Bounds actualBounds;
	SourceSupport::Contract
		original;			   // retains actual triangle lease even for unsupported primitives
	std::vector<Rival> ranked; // stable prior order; unknown/rejected rows retained
	uint64_t work = 0;
	std::string limitation;
};
Error Rank(const Request &, const ReadContext &, Result &);

enum class TargetDomain
{
	RelativeLinearAttested,
	HistoricalEncodedRgb8
};
struct ComparisonRequest
{
	Result prior;
	Shape::Request fitting; // geometry is fixed by Rank; only RGB amplitudes are fitted
	std::shared_ptr<const ChunkSource<Shape::Observation>> validation;
	uint64_t validationCount = 0;
	Id material = 0;
	bool sameMaterialResponseAttested = false;
	TargetDomain targets = TargetDomain::RelativeLinearAttested;
	double equivalentMseSlack = 1e-5;
};
struct Comparison
{
	Result prior;						// never reranked by held-out RGB
	std::vector<Shape::FitResult> fits; // same order as prior.ranked, including unavailable entries
	std::vector<Shape::Score> validation, heldout;
	std::vector<size_t> photometricAlternatives;
	std::vector<std::string> limitations;
	std::vector<Id> trainingIds, validationIds, trainingBlocks, validationBlocks;
	Bake::EvaluationContext frozenGeometry;
	Shape::Sampling frozenSampling;
	Id material = 0;
	TargetDomain targets = TargetDomain::RelativeLinearAttested;
	bool frozen = false, heldoutAudited = false;
};
// Validation comparison cannot read held-out targets. The separate audit takes
// a frozen comparison and cannot alter the geometry preference or equivalent set.
Error FitAndValidate(const ComparisonRequest &, const ReadContext &, Comparison &);
Error AuditHeldout(const ComparisonRequest &,
				   std::shared_ptr<const ChunkSource<Shape::Observation>>, uint64_t count,
				   const ReadContext &, Comparison &);
const char *StatusName(Status);
const char *ReasonName(Reason);
const char *KindName(EmitterKind);
} // namespace ShapeSelection
} // namespace Core
} // namespace Rend2LightRecon
