// Generated spatial field support. Historical observations are deliberately not inputs.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../tr_lightrecon_manager.h"
#include "field_basis.h"
#include "regions.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Probes
{
constexpr uint32_t kVersion = 1;
constexpr unsigned kNeighbours = 16;
enum class Classification : uint32_t
{
	Valid,
	Relocated,
	Solid,
	Unknown,
	NoClearance
};
enum Issue : uint32_t
{
	GeometryUnknown = 1,
	OpticalUnknown = 2,
	DynamicConditional = 4,
	RegionUnknown = 8,
	ResolutionLimited = 16,
	BudgetLimited = 32,
	RelocationRejected = 64,
	MomentApproximation = 128,
	MissingValues = 256,
	NegativeAngularValue = 512
};
enum class RefinementReason
{
	Geometry,
	SourceProximity,
	AngularError,
	FieldUncertainty,
	Coverage
};
struct Refinement
{
	Id evidence = 0;
	Bounds bounds;
	float spacingWorld = 0;
	RefinementReason reason = RefinementReason::Geometry;
};
struct Moment
{
	bool present = false;
	Vec3 directionToQuery = Vec3(0, 0, 1);
	double minimumCosine = -1, mean = 0, second = 0, maximumDistance = 0;
	uint32_t samples = 0;
};
// A conditional interpolation heuristic; NEVER a visibility/free-space certificate.
Error MomentWeight(const Moment &, Vec3 directionToQuery, double distance, double &weight);
struct Probe
{
	Id id = 0, source = 0, cell = 0, region = 0;
	Vec3 originalWorld, positionWorld;
	Bounds support; // uniform static free-space certificate, not a claimed whole room
	Classification classification = Classification::Unknown;
	uint32_t issues = 0, level = 0;
	int32_t leaf = -1, area = -1;
	double displacementWorld = 0, clearanceWorld = 0;
	Moment moment;
};
struct Link
{
	uint32_t probe = 0;
	Bounds certificate;
	uint32_t issues = 0;
};
struct Cell
{
	Id id = 0, parent = 0;
	Bounds bounds, support;
	uint32_t level = 0, issues = 0;
	SpaceClass space = SpaceClass::Unknown;
	uint32_t probe = UINT32_MAX, firstLink = 0, linkCount = 0;
};
struct Options
{
	Bounds bounds;
	float coarseSpacingWorld = 128, minimumSpacingWorld = 16, clearanceWorld = 1;
	float maximumRelocationFraction = .25f;
	unsigned neighbours = 8, maxDepth = 24;
	size_t maxNodes = 100000, maxProbes = 20000, maxLinks = 160000;
	uint64_t maxBytes = 64ull << 20, maxWork = 32000000, maxRays = 20000, rayWork = 100000;
};
struct Input
{
	Disk::Digest map{}, geometry{}, domainKey{};
	std::shared_ptr<const RegionDomain> domain;
	std::shared_ptr<const StaticVisibility> visibility;
	std::shared_ptr<const LightingRegionGraph> regions; // optional, never replaces local geometry
	std::vector<DynamicBoundary> dynamic;
	std::vector<Refinement> refinement;
};
struct Report
{
	uint64_t nodes = 0, valid = 0, relocated = 0, solid = 0, unknown = 0, noClearance = 0;
	uint64_t links = 0, rejectedLinks = 0, work = 0, rays = 0, bytes = 0, peakBuildBytes = 0;
	uint64_t conditional = 0, unassignedRegion = 0;
	double analysisVolume = 0, supportedStaticVolume = 0, unknownVolume = 0, solidVolume = 0,
		   maximumDisplacement = 0;
	bool complete = true, unboundedMotion = false;
};
struct Support
{
	Status status = Status::Unavailable;
	Id cell = 0, region = 0;
	uint32_t issues = 0, count = 0;
	Id probes[kNeighbours] = {};
	double weights[kNeighbours] = {}, rawWeight = 0, supportedWeight = 0;
	bool conditional = false;
};
struct QueryPolicy
{
	bool allowConditionalStatic = false, useMoments = false;
	double minimumWeightFraction = .01; // reject near-zero surviving support, never floor it
};
class Topology
{
  public:
	struct Data;
	explicit Topology(std::shared_ptr<const Data>);
	const std::vector<Probe> &Probes() const;
	const std::vector<Cell> &Cells() const;
	const std::vector<Link> &Links() const;
	const Report &Diagnostics() const;
	const Disk::Digest &Key() const;
	const Disk::Digest &GeometryKey() const;
	const Disk::Digest &MapKey() const;
	// O(log cells + fixed stencil), no allocation, I/O or ray queries.
	Support Lookup(Vec3, const QueryPolicy & = QueryPolicy()) const;
	const std::shared_ptr<const Data> &Storage() const
	{
		return data_;
	}

  private:
	std::shared_ptr<const Data> data_;
};
Error Build(const Input &, const Options &, const ReadContext &, std::shared_ptr<const Topology> &);
// Separate portable CPU chunks; no photometry in either chunk. Generic Disk container
// supplies chunk directory/streaming. Missing lighting datasets remain unavailable.
Error Encode(const Topology &, Id geometryDataset, Id topologyDataset, const ReadContext &,
			 std::vector<Disk::Chunk> &);
Error Decode(const std::vector<Disk::Chunk> &, const Disk::Digest &expectedGeometry,
			 const ReadContext &, std::shared_ptr<const Topology> &);
struct Value
{
	Id probe = 0;
	Vec3 evaluatedPositionWorld;
	Angular::Field angular;
};
struct Values
{
	Id field = 0;
	Disk::Digest topology{};
	std::vector<Value> records; // sparse, explicit zero uses present zero coefficients
};
// Spatial topology is representation neutral; this first prepared value adapter
// reuses P03 SH/SG codecs in a fixed WORLD basis. SG diffuse quadrature is offline.
class Prepared final : public CpuEvaluator
{
  public:
	struct Data;
	explicit Prepared(std::shared_ptr<const Data>);
	Status AcceptsAsset(const ReconstructedLightingAsset &) const override;
	Status Supports(Capability) const override;
	Status Indirect(const ReconstructedLightingAsset &, const IrradianceQuery &,
					LightingSample &) const override;
	Status IncomingRadiance(const ReconstructedLightingAsset &, const RadianceQuery &,
							LightingSample &) const override;
	Support GeometrySupport(Vec3, const QueryPolicy & = QueryPolicy()) const;

  private:
	std::shared_ptr<const Data> data_;
};
Error Prepare(std::shared_ptr<const Topology>, const Values &, const ReconstructedLightingAsset &,
			  const ReadContext &, std::shared_ptr<const Prepared> &);
} // namespace Probes
} // namespace Core
} // namespace Rend2LightRecon
