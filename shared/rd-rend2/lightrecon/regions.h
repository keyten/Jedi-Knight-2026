// Structural lighting topology, not irradiance, PVS, or a light source model.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "visibility.h"
#include "lightgrid.h"

namespace Rend2LightRecon
{
namespace Core
{
constexpr uint32_t kLightingRegionGraphVersion = 2;
constexpr const char *kLightingRegionGraphSchema = "org.openjk.lighting-region-graph";
enum class SpaceClass
{
	Free,
	Solid,
	Outside,
	Sky,
	Unknown,
	Exterior
};
struct SpatialLocation
{
	SpaceClass space = SpaceClass::Unknown;
	int32_t leaf = -1, cluster = -1, area = -1;
	bool boundary = false;
	uint64_t work = 0;
};
enum SpatialBoxIssue : uint32_t
{
	BoxOutsideWorld = 1u << 0,
	BoxUnclusteredLeaf = 1u << 1,
	BoxPossibleSolidBrush = 1u << 2,
	BoxInsideSolidBrush = 1u << 3,
	BoxBrushDisjointRefined = 1u << 4
};
class RegionDomain : public PointClassifier
{
  public:
	virtual Error Locate(Vec3, SpatialLocation &, double toleranceWorld = 1e-5) const = 0;
	// Uniform-volume certificate, not a classification inferred from a few samples.
	virtual Error ClassifyBox(const Bounds &, SpaceClass &, uint64_t &work) const = 0;
	virtual const LightmapMapIdentity *SourceIdentity() const
	{
		return nullptr;
	}
	Error Classify(Vec3, double, PointDomain &) const override;
};
struct SpatialPlane
{
	Vec3 normal;
	float distance = 0;
};
struct SpatialNode
{
	uint32_t plane = 0;
	int32_t children[2] = {-1, -1};
};
struct SpatialLeaf
{
	int32_t cluster = -1, area = -1;
	uint32_t firstBrush = 0, brushCount = 0;
};
struct SpatialBrush
{
	uint32_t firstSide = 0, sideCount = 0;
	bool solid = false, sky = false, world = true;
};
struct BspSpaceInput
{
	LightmapMapIdentity identity;
	Bounds worldBounds;
	std::vector<SpatialPlane> planes;
	std::vector<SpatialNode> nodes;
	std::vector<SpatialLeaf> leaves;
	std::vector<SpatialBrush> brushes;
	std::vector<uint32_t> sides, leafBrushes; // plane indices / brush indices
};
class BspRegionDomain final : public RegionDomain
{
  public:
	static Error Create(BspSpaceInput, const ReadContext &,
						std::shared_ptr<const BspRegionDomain> &,
						uint64_t maxTestsPerQuery = 1024 * 1024);
	Error Locate(Vec3, SpatialLocation &, double toleranceWorld = 1e-5) const override;
	Error ClassifyBox(const Bounds &, SpaceClass &, uint64_t &) const override;
	Error InspectBox(const Bounds &, SpaceClass &, uint64_t &, uint32_t &reasons) const;
	const BspSpaceInput &Source() const
	{
		return input_;
	}
	const LightmapMapIdentity *SourceIdentity() const override
	{
		return &input_.identity;
	}
	uint64_t OwnedBytes() const;

  private:
	BspRegionDomain(BspSpaceInput input, uint64_t limit) : input_(std::move(input)), limit_(limit)
	{
	}
	BspSpaceInput input_;
	uint64_t limit_;
};
struct DynamicBoundary
{
	Id id = 0;
	uint32_t model = 0;
	Bounds bounds;
	bool sweptExtentKnown = false; // authored model bounds rarely describe its travel
};
enum RegionCellIssue : uint32_t
{
	CellGeometryBoundary = 1u << 0,
	CellDynamicPossible = 1u << 1,
	CellBspMembershipSampled = 1u << 2,
	CellBoundaryRefsTruncated = 1u << 3,
	CellResolutionLimit = 1u << 4,
	CellBspUncertain = 1u << 5,
	CellUnboundedGeometry = 1u << 6,
	CellUnsupportedGeometry = 1u << 7,
	CellOpticalUncertain = 1u << 8,
	CellRepresentativeDisagrees = 1u << 9,
	CellPatchEnvelope = 1u << 10,
	CellBspSolid = 1u << 11,
	CellBspUnclustered = 1u << 12,
	CellBspBrushBoundary = 1u << 13,
	CellOutsideWorldBounds = 1u << 14
};
struct RegionCell
{
	Id id = 0, region = 0;
	uint32_t origin[3] = {}, units = 1;
	Vec3 representativeWorld;
	SpaceClass space = SpaceClass::Unknown;
	SpatialLocation bsp;
	uint32_t issues = 0;
	Id boundarySurfaces[8] = {};
	uint32_t boundaryCount = 0;
};
enum class DirectConnection
{
	ClearStaticSample,
	BlockedSample,
	Unknown
};
enum class TransportConnection
{
	OpenStaticSample,
	Blocked,
	ConditionalDynamic,
	Unknown
};
struct CellConnection
{
	uint32_t a = 0, b = 0; // cell vector indices, NOT lightgrid coordinates
	uint8_t axis = 0;
	double faceAreaWorldSquared = 0;
	Vec3 openingCentreWorld;
	Bounds openingBounds;
	DirectConnection direct = DirectConnection::Unknown;
	TransportConnection transport = TransportConnection::Unknown;
};
enum RegionIssue : uint32_t
{
	RegionSampledTopology = 1u << 0,
	RegionRoomCandidate = 1u << 1,
	RegionSkyObserved = 1u << 2,
	RegionExteriorKnown = 1u << 3,
	RegionDynamicUncertain = 1u << 4,
	RegionBoundaryIncomplete = 1u << 5
};
struct LightingRegion
{
	Id id = 0, staticComponent = 0; // 0 component = conditional/unresolved
	Bounds bounds;
	Vec3 representativeWorld; // actual certified free centre, never AABB midpoint
	uint32_t issues = RegionSampledTopology;
	uint32_t firstMember = 0, memberCount = 0, firstNeighbour = 0, neighbourCount = 0;
};
struct RegionBspLink
{
	Id region = 0;
	int32_t leaf = -1, cluster = -1, area = -1;
	RegionBspLink() : region(0), leaf(-1), cluster(-1), area(-1) {}
	RegionBspLink(Id r, int32_t l, int32_t c, int32_t a) : region(r), leaf(l), cluster(c), area(a)
	{
	}
};
struct RegionBoundary
{
	Id region = 0, surface = 0;
	RegionBoundary() : region(0), surface(0) {}
	RegionBoundary(Id r, Id s) : region(r), surface(s) {}
};
struct RegionConnection
{
	Id a = 0, b = 0; // distinct sorted region IDs; shared-face GEOMETRIC adjacency only
	uint64_t clearSamples = 0, blockedSamples = 0, unknownSamples = 0;
	TransportConnection transport = TransportConnection::Unknown;
	Bounds sampledOpeningBounds;
	Vec3 sampledOpeningCentreWorld;
	double sampledAreaWorldSquared = 0;
	bool apertureCandidate = true, roomBoundaryCandidate = true, dynamicPossible = false;
	// Sampled aperture geometry, NOT an exact portal mesh, permanent LOS or emitter.
};
struct RegionOptions
{
	Bounds bounds; // explicit bounded analysis domain, not inferred from a lightgrid
	float coarseCellWorld = 128, minimumCellWorld = 16, constrictionWorld = 64;
	size_t maxNodes = 400000, maxCells = 350000, maxLinks = 2000000, maxDynamicBounds = 4096;
	uint64_t maxBytes = 128ull * 1024 * 1024, maxWork = 128ull * 1024 * 1024,
			 maxRayWork = 1024 * 1024;
	bool inspectSky = true;
	unsigned skySamplesPerRegion = 16; // bounded actual sky rays from member centres
};
struct RegionBuildInput
{
	LightmapMapIdentity identity;
	std::shared_ptr<const RegionDomain> domain;
	std::shared_ptr<const StaticVisibility> geometry;
	std::vector<DynamicBoundary> dynamic;
};
struct RegionReport
{
	uint64_t freeCells = 0, solidCells = 0, outsideCells = 0, skyCells = 0, unknownCells = 0,
			 dynamicCells = 0;
	uint64_t regions = 0, staticComponents = 0, connections = 0, apertures = 0, skyRegions = 0;
	uint64_t domainWork = 0, geometryWork = 0, rays = 0, ownedBytes = 0, peakBuildBytes = 0;
	bool resolutionLimited = false, dynamicExtentUnknown = false, staticOnly = true;
	// Final adaptive leaves, before visual geometry; counters may overlap.
	uint64_t bspFree = 0, bspSolid = 0, bspUnknown = 0;
	uint64_t localInvalidated = 0, globalOnlyInvalidated = 0, minimumLimited = 0;
	uint64_t movingUncertain = 0, opticalUncertain = 0, unsupportedUncertain = 0;
	// Box vs point differences include expected mixed-boundary boxes, not just invalid
	// certificates.
	uint64_t representativeDisagrees = 0, locallyClearUnassigned = 0;
	uint64_t bspUnclustered = 0, bspPossibleBrush = 0, bspOutsideWorld = 0;
	uint64_t bspRefinedDisjoint = 0;
};
enum class RegionAssignmentState
{
	Assigned,
	Solid,
	Outside,
	Sky,
	Unknown,
	MissingObservation
};
struct RegionAssignment
{
	RegionAssignmentState state = RegionAssignmentState::Unknown;
	Id cell = 0, region = 0, staticComponent = 0;
	SpatialLocation bsp;
	uint32_t issues = 0;				// cell reasons, valid also when region == 0
	bool outsideAnalysisBounds = false; // distinct from physically Outside
};
struct SurfaceRegionSides
{
	RegionAssignment positive, negative;
	double offsetWorld = 0; // search cap, not a modification to the canonical position
	double positiveOffsetWorld = 0, negativeOffsetWorld = 0;
};
enum class SampleGraphRelation
{
	SameRegion,
	SameStaticComponent,
	DisconnectedSampleGraph,
	Unknown
};
struct RegionPairFilter
{
	RegionAssignment source, observation;
	SampleGraphRelation relation = SampleGraphRelation::Unknown;
	RayResult direct; // Only actual ray blockage rejects direct light, never graph/PVS alone.
	bool dynamicPossible = false, dynamicExtentUnknown = false; // distinct from static ray result
};
class LightingRegionGraph
{
  public:
	static Error Build(RegionBuildInput, const RegionOptions &, const ReadContext &,
					   std::shared_ptr<const LightingRegionGraph> &);
	Error Lookup(Vec3, const ReadContext &, RegionAssignment &) const;
	Error AssignGrid(const GridCell &, const ReadContext &, RegionAssignment &) const;
	Error AssignGrid(const LightgridObservations &, uint64_t ordinal, const ReadContext &,
					 RegionAssignment &) const;
	Error AssignSurface(const LuxelTile &, size_t sample, const ReadContext &, SurfaceRegionSides &,
						double maximumOffsetWorld = 0) const;
	Error AssignSurfaceSides(Vec3 position, Vec3 geometricNormal, Id sourceSurface,
							 const ReadContext &, SurfaceRegionSides &,
							 double maximumOffsetWorld = 0) const;
	Error FilterPair(Vec3 source, Vec3 observation, const ReadContext &, RegionPairFilter &) const;
	Error DebugObj(size_t maxCells, size_t maxBytes, std::string &) const;
	const LightingRegion *Find(Id) const;
	const std::vector<RegionCell> &Cells() const;
	const std::vector<LightingRegion> &Regions() const;
	const std::vector<RegionConnection> &Connections() const;
	const std::vector<CellConnection> &CellConnections() const;
	const std::vector<uint32_t> &Members() const;	 // CSR cell indices
	const std::vector<uint32_t> &Neighbours() const; // CSR connection indices
	const std::vector<RegionBspLink> &BspLinks() const;
	const std::vector<RegionBoundary> &Boundaries() const;
	const RegionReport &Report() const;
	const RegionOptions &Options() const;
	const LightmapMapIdentity &Identity() const;
	// Scope ordinal IDs by this content/options/version key. Never reuse old certification.
	const Disk::Digest &CertificationKey() const;
	bool PortableCertification() const; // false for custom domains without canonical BSP content
	// Cheap export guard; identical replacement leases still require explicit replay.
	bool MatchesSources(const std::shared_ptr<const SurfaceGeometry> &,
						const std::shared_ptr<const RegionDomain> &,
						const std::vector<DynamicBoundary> &) const;

  private:
	struct Data;
	explicit LightingRegionGraph(std::shared_ptr<const Data> d) : data_(std::move(d)) {}
	std::shared_ptr<const Data> data_;
};
} // namespace Core
} // namespace Rend2LightRecon
