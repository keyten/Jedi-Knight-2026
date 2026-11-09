// Physical occupied/emitting support, distinct from optical receiver rays.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "point_refine.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace SourceSupport
{
constexpr uint32_t kVersion = 1;
enum class Kind
{
	LegacyVacuum,
	FreePoint,
	FreeSpot,
	MountedPoint,
	MountedSpot,
	Rectangle,
	TwoSidedRectangle,
	Cylinder,
	OriginalSurface,
	RecessedRectangle,
	UnsupportedAssembly
};
struct Contract
{
	uint32_t version = kVersion;
	Kind kind = Kind::LegacyVacuum;
	Id fixture = 0;
	Disk::Digest map{}, geometry{};
	std::vector<Id> surfaces; // original BSP ordinal+1; no proximity-based fixture identity
	Vec3 anchorWorld, outward;
	bool mountKnown = false, twoSidedAttested = false;
	double maximumStandOffWorld = 0; // fixture-evidenced face distance (tube: body gap); default contact
	double frontClearanceWorld = 0;	 // empty outward extrusion (tube: conservative body enclosure), not an offset
	std::string provenance;
	Chunk<Bake::FootprintTriangle> original; // immutable, disjoint source triangles
	std::vector<Id> triangleSurfaces;
};
struct Result
{
	PointRefine::PlacementResult placement;
	bool contact = false, sourceSideCertified = false, conditional = false;
	double emittingAreaWorldSquared = 0, contactErrorWorld = 0;
	uint64_t work = 0, rays = 0;
	std::string reason;
	Disk::Digest key{};
};
Disk::Digest Key(const Contract &);
Error Validate(const Contract &);
// No RGB/answers/observation folds. Static world brushes are NEVER excluded.
Error Check(const PointRefine::SceneInput &, const PointRefine::PlacementProvider &,
			const PhysicalEmitter &before, const PhysicalEmitter &after, const Contract &,
			const ReadContext &, Result &);
// Derive exact planar support from named original emitting surfaces. No fitted
// bounding rectangle replaces the triangle union; non-planar/multi-part stays unsupported.
Error FromSurfaces(std::shared_ptr<const SurfaceGeometry>, const StaticVisibility &,
				   const std::vector<uint32_t> &sourceOrdinals, Id fixture, const ReadContext &,
				   Contract &);
} // namespace SourceSupport
} // namespace Core
} // namespace Rend2LightRecon
