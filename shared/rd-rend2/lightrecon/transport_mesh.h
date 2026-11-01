// Geometry-only, disjoint diffuse support. Never reads observation targets or folds.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "indirect.h"
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Indirect
{
namespace Topology
{
constexpr uint32_t kVersion = 1;
enum Issue : uint32_t
{
	UnknownSide = 1u << 0,
	UnsupportedSurface = 1u << 1,
	ConflictingMaterial = 1u << 2,
	CrossingGeometry = 1u << 3,
	NumericalGeometry = 1u << 4,
	OutsideSupport = 1u << 5,
	PartialCoverage = 1u << 6
};
struct Support
{
	Id surface = 0, triangle = 0, material = 0;
	uint8_t style = 0;
	Vec3 world[3], normal;
	bool sideKnown = false;
};
struct Options
{
	// Fixed geometry-only scope. Unknown means all supplied geometry, never all observations.
	Bounds bounds;
	bool completeSourceSet = false; // explicit external coverage assertion, not inferred
	double maximumEdgeWorld = 128;
	size_t maxInputTriangles = 262144, maxPieces = 8192;
	uint64_t maxWork = 16000000, maxBytes = 128ull << 20;
};
struct Provenance
{
	Id surface = 0, triangle = 0;
	Provenance(Id s = 0, Id t = 0) : surface(s), triangle(t)
	{
	}
};
struct Element
{
	Patch patch;
	std::vector<Provenance> sources;
};
struct Report
{
	uint64_t inputTriangles = 0, clippedTriangles = 0, splitPieces = 0, work = 0, bytes = 0;
	uint64_t duplicateOverlaps = 0, conflictingOverlaps = 0, crossingPairs = 0, opposingPairs = 0;
	uint64_t unsupportedRecords = 0;
	uint64_t precisionExcludedPieces = 0;
	double inputArea = 0, scopedArea = 0, deduplicatedArea = 0, unsupportedArea = 0, outputArea = 0;
	double areaAccountingResidual = 0;
	bool complete = false;
	uint32_t issues = 0;
};
enum class Relation
{
	Disjoint,
	Coplanar,
	SameSideOverlap,
	OppositeSideOverlap,
	Crossing
};
struct Contact
{
	Relation relation = Relation::Disjoint;
	double sharedArea = 0;
};
// Geometric contact, with a roundoff-only tolerance; no optical/visibility assertion.
Contact Inspect(const Bake::FootprintTriangle &, Vec3, const Bake::FootprintTriangle &, Vec3);
enum class Availability
{
	Available,
	Partial,
	Unavailable
};
struct Weight
{
	Id patch = 0;
	double areaWorldSquared = 0;
	Weight(Id p = 0, double a = 0) : patch(p), areaWorldSquared(a)
	{
	}
};
class Mapping
{
  public:
	Id Observation() const
	{
		return observation;
	}
	Disk::Digest MeshKey() const
	{
		return meshKey;
	}
	Disk::Digest ReceiverKey() const
	{
		return receiverKey;
	}
	Availability State() const
	{
		return state;
	}
	double Area() const
	{
		return area;
	}
	double CoveredArea() const
	{
		return coveredArea;
	}
	uint32_t Issues() const
	{
		return issues;
	}
	const std::vector<Weight> &Weights() const
	{
		return weights;
	}

  private:
	friend class Mesh;
	Id observation = 0;
	Disk::Digest meshKey{}, receiverKey{};
	Availability state = Availability::Unavailable;
	double area = 0, coveredArea = 0;
	uint32_t issues = 0;
	std::vector<Weight> weights;
};
Disk::Digest ReceiverKey(const Bake::Receiver &);
class Mesh
{
  public:
	static Error Build(Disk::Digest map, Disk::Digest geometry, std::vector<Support>,
					   const Options &, const ReadContext &, std::shared_ptr<const Mesh> &);
	// Captured source triangles, independent of lightmap charts/sampling and RGB.
	static Error FromSurfaces(const SurfaceGeometry &, Disk::Digest geometry, const Options &,
							  const ReadContext &, std::shared_ptr<const Mesh> &);
	Error Map(const Bake::Receiver &, const ReadContext &, std::shared_ptr<const Mapping> &) const;
	const std::vector<Element> &Elements() const
	{
		return elements;
	}
	const Report &Diagnostics() const
	{
		return report;
	}
	Disk::Digest Key() const
	{
		return key;
	}
	Disk::Digest MapKey() const
	{
		return mapKey;
	}
	Disk::Digest GeometryKey() const
	{
		return geometryKey;
	}
	const Options &Policy() const
	{
		return options;
	}

  private:
	std::vector<Element> elements;
	Report report;
	Disk::Digest key{}, mapKey{}, geometryKey{};
	Options options;
	// Fixed 3-D spatial buckets, reused for receiver-footprint mapping.
	std::map<std::tuple<int64_t, int64_t, int64_t>, std::vector<size_t>> index;
};
} // namespace Topology
} // namespace Indirect
} // namespace Core
} // namespace Rend2LightRecon
