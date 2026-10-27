// Immutable static-world ray queries. No collision engine, renderer or GPU state.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "surfaces.h"
#include <utility>

namespace Rend2LightRecon
{
namespace Core
{

constexpr uint32_t kStaticVisibilityVersion = 2;
enum class VisibilityState
{
	Visible,
	Blocked,
	Unknown
};
enum class RayPurpose
{
	Segment,
	Shadow,
	Sun,
	Sky,
	IrradianceProbe
};
enum class ObstacleKind
{
	None,
	Opaque,
	Sky,
	AlphaTested,
	Translucent,
	Unsupported
};
enum class SkyReach
{
	NotReached,
	Reached,
	Unknown
};
enum class RayReliability
{
	Geometric,
	Approximate,
	Assumed,
	Unknown
};
enum class OpticalPolicy
{
	Unknown,
	AssumeOpaque,
	Ignore
}; // explicit assumptions, never measured transmission
enum class FacingPolicy
{
	Material,
	TwoSided
}; // TwoSided is an explicit occluder policy
enum class PointDomain
{
	Unknown,
	Free,
	Solid,
	Boundary
};

// Optional immutable, thread-safe domain oracle. Collision contents MAY be used
// for origin diagnostics, never as a substitute for optical surface intersection.
class PointClassifier
{
  public:
	virtual ~PointClassifier() = default;
	virtual Error Classify(Vec3 world, double toleranceWorld, PointDomain &out) const = 0;
};

enum RayIssue : uint32_t
{
	OriginUnclassified = 1u << 0,
	OriginInsideSolid = 1u << 1,
	OriginOnBoundary = 1u << 2,
	EndpointInsideSolid = 1u << 3,
	OpticalUnknown = 1u << 4,
	SidednessUnknown = 1u << 5,
	PatchApproximation = 1u << 6,
	UnsupportedGeometry = 1u << 7,
	IncompleteGeometry = 1u << 8,
	SkyBoundaryMissing = 1u << 9,
	CoplanarRay = 1u << 10,
	BiasObstructed = 1u << 11,
	PrecisionLimit = 1u << 12,
	PolicyAssumption = 1u << 13,
	CoincidentSurfaces = 1u << 14
};
struct RayBias
{
	double absoluteWorld = 1e-5, relative = 4.0 * 1.1920928955078125e-7;
	double maximumWorld = .05, footprintWorld = 0; // optional cap: 1% of footprint
	// Offset only with a known source surface + unit geometric normal. Select
	// the normal's side toward end; never move the canonical observation itself.
	bool normalKnown = false;
	Vec3 geometricNormal;
};
struct RayPolicy
{
	FacingPolicy facing = FacingPolicy::Material;
	OpticalPolicy alphaTested = OpticalPolicy::Unknown, translucent = OpticalPolicy::Unknown;
	bool acceptPatchApproximation = false;
	bool requireKnownFreeOrigin = true;
};
struct RayQuery
{
	Vec3 startWorld, endWorld; // finite WORLD positions; direction is receiver -> target/sky
	RayPurpose purpose = RayPurpose::Segment; // Sun/Sky require an actual sky intersection
	RayPolicy policy;
	RayBias bias;
	// IDs are source BSP ordinal + 1, scoped by GeometryIdentity. 0 = unknown.
	Id originSurface = 0, targetSurface = 0;
	double targetToleranceWorld = 0; // suppress ONLY targetSurface hits within this end distance
	bool originKnownFree = false;	 // explicit external evidence, not a guess from lightgrid/PVS
	uint64_t maxWork = 1024 * 1024;	 // per-ray BVH node/primitive tests; bounded local stack
};
struct RayHit
{
	bool present = false, positionKnown = false;
	Vec3 positionWorld, geometricNormal;
	double distanceWorld = 0; // Euclidean distance from ORIGINAL start, when positionKnown
	double parameter = 0;	  // fraction of effective (possibly offset) segment, when positionKnown
	double errorWorld = 0;	  // patch chord bound; bounds-only candidates have positionKnown=false
	double candidateNearWorld = 0, candidateFarWorld = 0; // bounds interval from effective origin
	Id surface = 0, triangle = 0;
	uint32_t material = 0;
	ObstacleKind obstacle = ObstacleKind::None;
	bool frontFace = false; // dot(cross(v1-v0,v2-v0), ray direction) < 0
};
struct RayResult
{
	VisibilityState state = VisibilityState::Unknown;
	SkyReach sky = SkyReach::Unknown;
	RayReliability reliability = RayReliability::Unknown;
	PointDomain originDomain = PointDomain::Unknown;
	uint32_t issues = 0;
	RayHit firstHit, terminalHit; // first geometric event vs later definite opaque/sky terminator
	Vec3 effectiveOriginWorld;
	double appliedBiasWorld = 0;
	uint64_t nodeTests = 0, primitiveTests = 0;
};
struct VisibilityBuildOptions
{
	size_t maxPrimitives = 1024 * 1024, maxSurfaces = 131072;
	uint64_t maxBytes = 256ull * 1024 * 1024, maxWork = 128ull * 1024 * 1024;
	unsigned leafSize = 4,
			 maxPatchSubdivisions = 64; // power of two; fixed independent of runtime LOD
	double patchToleranceWorld = .01;
	bool completeStaticGeometry = true; // caller's coverage assertion, NOT inferred from bounds/PVS
};
struct VisibilityBuildReport
{
	uint64_t triangles = 0, patchTriangles = 0, unsupportedBounds = 0, degenerate = 0;
	uint64_t surfaces = 0, excludedMoving = 0, excludedNoDraw = 0, unboundedUnsupported = 0;
	uint64_t nodes = 0, ownedBytes = 0, peakBuildBytes = 0, buildWork = 0;
	unsigned depth = 0;
	double maximumPatchErrorWorld = 0;
	bool complete = false;
	// Global source-record support vs coverage relevant to a clear local query.
	bool spatialCoverageComplete = false;
	uint64_t excludedFlares = 0, missingUnsupportedBounds = 0, missingKnownSurfaceBounds = 0;
};
struct VisibilitySurface
{
	Id id = 0;
	uint32_t material = 0;
	uint32_t materialSignals = 0; // preserve combined alpha/translucent categories
	ObstacleKind obstacle = ObstacleKind::Unsupported;
	SurfaceCull cull = SurfaceCull::Unknown;
};
struct GeometryBoxResult
{
	bool intersects = false, referencesTruncated = false;
	Id surfaces[8] = {};
	size_t surfaceCount = 0;
	uint64_t work = 0;
	bool coverageComplete = false; // false for unbounded possibly-opaque omissions anywhere
	uint32_t issues =
		0; // OpticalUnknown, UnsupportedGeometry, PatchApproximation, IncompleteGeometry
};

class StaticVisibility final : public VisibilityProvider
{
  public:
	// Explicit bounded synchronous build. Failure clears out; no global cache,
	// automatic map work or half-built publication. Input lease needed only here.
	static Error Build(std::shared_ptr<const SurfaceGeometry>, const VisibilityBuildOptions &,
					   const ReadContext &, std::shared_ptr<const StaticVisibility> &out,
					   std::shared_ptr<const PointClassifier> domain = {});
	Error Query(const RayQuery &, const ReadContext &, RayResult &) const override;
	// Conservative triangle/box SAT, including curved error envelopes. A miss
	// certifies no retained visual geometry in the box, not that it is outside solid.
	Error OverlapBox(const Bounds &, const ReadContext &, GeometryBoxResult &,
					 uint64_t maxWork = 1024 * 1024) const;
	// Inherits VisibilityProvider::Batch: no per-ray heap allocation, explicit prefix.
	// Compatibility adapters only return 0/1 for Geometric outcomes; Unknown,
	// assumed/approximate results return Unavailable, never fake transmission.
	Error Segment(const Vec3 &, const Vec3 &, const ReadContext &, float &) const override;
	Error Sky(const Vec3 &, const Vec3 &, const ReadContext &, float &) const override;
	const VisibilityBuildReport &Report() const;
	const LightmapMapIdentity &GeometryIdentity() const;
	const std::vector<VisibilitySurface> &Surfaces() const;
	const Disk::Digest &CertificationKey() const;
	bool BuiltFrom(const std::shared_ptr<const SurfaceGeometry> &) const;

  private:
	struct Data;
	explicit StaticVisibility(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
	std::shared_ptr<const Data> data_;
};

} // namespace Core
} // namespace Rend2LightRecon
