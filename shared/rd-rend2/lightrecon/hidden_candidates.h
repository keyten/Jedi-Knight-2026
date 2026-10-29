// Bounded surface-driven proposals, never reconstructed physical-light output.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_forward.h"
#include "evidence.h"
#include "regions.h"

namespace Rend2LightRecon
{
namespace Core
{
namespace Hidden
{
constexpr uint32_t kVersion = 1;
enum class Kind
{
	PhysicalEmitterCandidate,
	AmbiguousSourceOrTransport,
	TransportEvidence
};
enum class Feasibility
{
	ProvenFree,
	PlausibleUnverified,
	Boundary,
	RegionVisibilityUnknown
};
enum class Context
{
	CertifiedRegion,
	BspContext,
	LocalVisibility,
	Neighbourhood
};
enum class Relation
{
	Visible,
	Blocked,
	Unknown,
	Assumed,
	BackFacing
};
enum class Method
{
	SurfaceResponseSearch,
	DirectionIntersection,
	Refinement
};
struct Sample
{
	Bake::Receiver receiver;
	uint8_t rgb[3] = {};
	double areaWorldSquared = 0;
	bool eligible = false, encodingAttested = false, transportSuspected = false;
	uint32_t materialUncertainty = 0;
};
Error FromObservation(const Bake::Receiver &, const Decode::Observation &, double area, bool eligible,
					  bool encodingAttested, uint32_t materialUncertainty, bool transportSuspected, Sample &);
struct DirectionConstraint
{
	Id id = 0;
	Vec3 originWorld, toSource;
	double halfAngleRadians = 0;
	bool usable = false;
	std::string provenance;
};
// Additional mixture uncertainty is REQUIRED; quantization is not an accurate
// sun/lamp likelihood. These constraints only seed/tie-break surface proposals.
DirectionConstraint GridConstraint(const GridCell &, Id, double mixtureRadians);
DirectionConstraint DeluxeConstraint(const Sample &, const DeluxeDirection &, Id, double mixtureRadians);
struct SearchArea
{
	Id id = 0;
	Bounds positions, receivers;
	std::string reason; // includes later controller-requested additional search areas
};
struct Options
{
	double localCellWorld = 512, reachWorld = 256;
	uint32_t coarseDivisions = 4, refinementRounds = 2, refineSeeds = 4;
	size_t maxSamples = 512, samplesPerArea = 64, maxAreas = 32, maxProfiles = 4;
	size_t maxNodesPerArea = 768, retainedPerChannelProfile = 4;
	uint64_t maxRays = 2000000, maxWork = 64000000, maxForwardCalls = 100000;
	uint64_t maxBytes = 64ull * 1024 * 1024;
	double probeAmplitudes[2] = {128, 512};
	double minimumContrast = .03, alternativeScoreDelta = .03;
	bool spots = true, allowAssumedProfiles = false, allowAssumedEncoding = false;
	// Only an independently known direct-only compiler experiment may attest this.
	// Stock/unknown mixtures default to ambiguous source-versus-transport.
	bool directOnlyAttested = false;
};
struct Request
{
	LightmapMapIdentity map;
	Units units;
	Chunk<Sample> training; // training-side observations ONLY, no hidden answers
	Bounds analysisBounds;
	std::vector<SearchArea> areas; // empty => deterministic local spatial bins
	std::vector<DirectionConstraint> directions;
	std::vector<Bake::ProfileHypothesis> profiles; // UnknownProfile is not a numeric law
	Bake::EvaluationContext geometry;
	std::shared_ptr<const PointClassifier> domain;
	Disk::Digest domainKey{}; // explicit immutable classifier generation, when domain is supplied
	std::shared_ptr<const LightingRegionGraph> regions; // optional; never a global admission gate
	Options options;
};
struct SupportingObservation
{
	Id observation = 0, surface = 0;
	Relation relation = Relation::Unknown;
	uint32_t issues = 0;
};
struct Hypothesis
{
	Id candidate = 0, area = 0, region = 0;
	Kind kind = Kind::AmbiguousSourceOrTransport;
	Feasibility feasibility = Feasibility::RegionVisibilityUnknown;
	Context context = Context::Neighbourhood;
	SpatialLocation bsp; // separate leaf/cluster/area context; never an optical certificate
	Method method = Method::SurfaceResponseSearch;
	std::string profile, profileScope;
	Bake::SourceKind sourceKind = Bake::SourceKind::Point;
	uint32_t channel = 0;
	uint32_t evidenceChannels =
		0;					   // deduplicated same geometry; anonymous RGB-code channels if primaries unknown
	Estimate channelScores[3]; // shape distances, not probabilities
	Bounds searchedCell;	   // searched resolution cell, NOT a confidence interval
	std::vector<SupportingObservation> observations;
	size_t distinctSurfaces = 0, distinctMaterials = 0, visible = 0, blocked = 0, unknown = 0;
	double score = 0, directionalDisagreementRadians = 0, receiverBaselineWorld = 0;
	double normalDiversity = 0;
	// A false support-warning flag does NOT attest unique XYZ: no parameter
	// Jacobian/covariance or exhaustive feasible set is computed by this provider.
	bool scoreAvailable = false, positionUnderconstrained = true, colourAmbiguous = true;
	Estimate existenceConfidence; // always unknown: this is NOT source verification
	std::vector<std::string> reasons;
};
struct AreaReport
{
	Id id = 0;
	uint64_t requested = 0, selected = 0, saturated = 0, excluded = 0;
	uint64_t unsupportedReceivers = 0; // profile/receiver combination, not an unknown blocker
	uint64_t nodes = 0, rejectedSolid = 0, rejectedOutside = 0, retained = 0;
	uint64_t unknownRegion = 0, unknownVisibility = 0, profileUnsupported = 0;
	bool searchLimited = false, sampleLimited = false;
};
struct Report
{
	std::vector<AreaReport> areas;
	uint64_t rays = 0, traversalWork = 0, forwardCalls = 0, sourceReceiverPairs = 0;
	uint64_t accountedPeakBytes = 0;
	bool complete = true; // bounded proposal SEARCH coverage, never completed reconstruction
	std::vector<std::string> diagnostics;
};
struct Result
{
	Disk::Digest key{};
	// Same records used by the emissive evidence provider. Indices/aliases use
	// this result's evidence vector. No canonical resolved-emitter list is mutated.
	std::vector<EmitterEvidence> evidence;
	std::vector<EmitterCandidate> candidates;
	std::vector<Hypothesis> hypotheses;
	Report report;
};
// Synchronous, deterministic; borrowed immutable leases only for this call.
// Output owns its records, no world/renderer pointer, I/O, global cache or threads.
// Errors clear proposals; capped local searches return explicit incomplete reports.
Error Generate(const Request &, const ReadContext &, Result &);
} // namespace Hidden
} // namespace Core
} // namespace Rend2LightRecon
