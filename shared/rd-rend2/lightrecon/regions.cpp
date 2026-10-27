// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "regions.h"
#include "geometry_key.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <tuple>

namespace Rend2LightRecon
{
namespace Core
{
namespace
{
double Axis(Vec3 v, unsigned k)
{
	return k == 0 ? v.x : k == 1 ? v.y : v.z;
}
void Set(Vec3 &v, unsigned k, double x)
{
	if (k == 0)
		v.x = float(x);
	else if (k == 1)
		v.y = float(x);
	else
		v.z = float(x);
}
Vec3 Add(Vec3 a, Vec3 b, double s = 1)
{
	return Vec3(float(a.x + b.x * s), float(a.y + b.y * s), float(a.z + b.z * s));
}
bool Finite(Vec3 v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
double Dot(Vec3 a, Vec3 b)
{
	return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
}
bool Valid(const Bounds &b)
{
	return b.known && Finite(b.minWorld) && Finite(b.maxWorld) && b.minWorld.x < b.maxWorld.x &&
		   b.minWorld.y < b.maxWorld.y && b.minWorld.z < b.maxWorld.z;
}
bool Inside(const Bounds &b, Vec3 p)
{
	for (unsigned k = 0; k < 3; ++k)
		if (Axis(p, k) < Axis(b.minWorld, k) || Axis(p, k) > Axis(b.maxWorld, k))
			return false;
	return true;
}
bool Overlap(const Bounds &a, const Bounds &b)
{
	for (unsigned k = 0; k < 3; ++k)
		if (Axis(a.maxWorld, k) < Axis(b.minWorld, k) || Axis(a.minWorld, k) > Axis(b.maxWorld, k))
			return false;
	return true;
}
void Expand(Bounds &a, const Bounds &b)
{
	if (!a.known)
	{
		a = b;
		return;
	}
	for (unsigned k = 0; k < 3; ++k)
	{
		Set(a.minWorld, k, std::min(Axis(a.minWorld, k), Axis(b.minWorld, k)));
		Set(a.maxWorld, k, std::max(Axis(a.maxWorld, k), Axis(b.maxWorld, k)));
	}
}
Vec3 Centre(const Bounds &b)
{
	return Vec3(float((double(b.minWorld.x) + b.maxWorld.x) * .5),
				float((double(b.minWorld.y) + b.maxWorld.y) * .5),
				float((double(b.minWorld.z) + b.maxWorld.z) * .5));
}
bool Free(SpaceClass s)
{
	return s == SpaceClass::Free || s == SpaceClass::Exterior;
}
bool Cancelled(const ReadContext &c)
{
	return c.cancellation && c.cancellation->IsCancelled();
}
bool Span(uint32_t first, uint32_t count, size_t size)
{
	return first <= size && count <= size - first;
}
void PlaneRange(const SpatialPlane &p, const Bounds &box, double &low, double &high)
{
	low = high = -p.distance;
	for (unsigned k = 0; k < 3; ++k)
	{
		double a = Axis(p.normal, k) * Axis(box.minWorld, k),
			   b = Axis(p.normal, k) * Axis(box.maxWorld, k);
		low += std::min(a, b);
		high += std::max(a, b);
	}
}
struct BuildFailure
{
	Error error;
};
void Require(Error e)
{
	if (static_cast<unsigned>(e.code) > static_cast<unsigned>(Code::AlgorithmError))
		e = Failure(Code::ProviderError, "Invalid region provider error");
	if (!e)
		throw BuildFailure{std::move(e)};
}
struct Dsu
{
	std::vector<uint32_t> parent;
	explicit Dsu(size_t n) : parent(n)
	{
		for (uint32_t i = 0; i < n; ++i)
			parent[i] = i;
	}
	uint32_t Root(uint32_t a)
	{
		while (parent[a] != a)
		{
			parent[a] = parent[parent[a]];
			a = parent[a];
		}
		return a;
	}
	void Join(uint32_t a, uint32_t b)
	{
		a = Root(a);
		b = Root(b);
		if (a != b)
			parent[std::max(a, b)] = std::min(a, b);
	}
};
} // namespace

Error RegionDomain::Classify(Vec3 p, double tolerance, PointDomain &out) const
{
	out = PointDomain::Unknown;
	SpatialLocation l;
	auto e = Locate(p, l, tolerance);
	if (!e)
		return e;
	out = l.boundary ? PointDomain::Boundary
					 : Free(l.space) ? PointDomain::Free
									 : l.space == SpaceClass::Solid || l.space == SpaceClass::Sky
										   ? PointDomain::Solid
										   : PointDomain::Unknown;
	return Error();
}
uint64_t BspRegionDomain::OwnedBytes() const
{
	return sizeof(*this) + input_.identity.path.size() +
		   input_.planes.capacity() * sizeof(SpatialPlane) +
		   input_.nodes.capacity() * sizeof(SpatialNode) +
		   input_.leaves.capacity() * sizeof(SpatialLeaf) +
		   input_.brushes.capacity() * sizeof(SpatialBrush) +
		   (input_.sides.capacity() + input_.leafBrushes.capacity()) * sizeof(uint32_t);
}
Error BspRegionDomain::Create(BspSpaceInput input, const ReadContext &ctx,
							  std::shared_ptr<const BspRegionDomain> &out, uint64_t limit)
{
	out.reset();
	if (!Valid(input.worldBounds) || input.leaves.empty() || !limit || input.identity.path.empty())
		return Failure(Code::InvalidInput, "Invalid BSP spatial identity/bounds/leaves");
	try
	{
		std::shared_ptr<BspRegionDomain> d(new BspRegionDomain(std::move(input), limit));
		const auto &s = d->input_;
		if (d->OwnedBytes() + s.nodes.size() * 2 > ctx.maxBytes)
			return Failure(Code::MemoryLimit, "BSP spatial snapshot budget");
		for (const auto &p : s.planes)
			if (!Finite(p.normal) || !std::isfinite(p.distance) ||
				std::abs(Dot(p.normal, p.normal) - 1) > 1e-4)
				return Failure(Code::InvalidInput, "Invalid BSP spatial plane");
		for (const auto &b : s.brushes)
			if (!Span(b.firstSide, b.sideCount, s.sides.size()) || (b.solid && b.sideCount < 4))
				return Failure(Code::InvalidInput, "Invalid BSP brush span/solid");
		for (auto p : s.sides)
			if (p >= s.planes.size())
				return Failure(Code::InvalidInput, "Invalid BSP brush plane");
		for (auto b : s.leafBrushes)
			if (b >= s.brushes.size())
				return Failure(Code::InvalidInput, "Invalid BSP leaf brush");
		for (const auto &l : s.leaves)
			if (!Span(l.firstBrush, l.brushCount, s.leafBrushes.size()) || l.cluster < -1 ||
				l.area < -1)
				return Failure(Code::InvalidInput, "Invalid BSP leaf span/metadata");
		for (const auto &n : s.nodes)
		{
			if (n.plane >= s.planes.size())
				return Failure(Code::InvalidInput, "Invalid BSP node plane");
			for (auto c : n.children)
				if (c >= 0 ? size_t(c) >= s.nodes.size()
						   : uint64_t(-int64_t(c) - 1) >= s.leaves.size())
					return Failure(Code::InvalidInput, "Invalid BSP child");
		}
		std::vector<uint8_t> colour(s.nodes.size());
		// Validate every component, not just the root, and bound traversal depth.
		struct Frame
		{
			uint32_t index;
			unsigned depth;
			bool exit;
		};
		std::vector<Frame> stack;
		for (uint32_t root = 0; root < s.nodes.size(); ++root)
		{
			if (colour[root])
				continue;
			stack.push_back({root, 1, false});
			while (!stack.empty())
			{
				if (Cancelled(ctx))
					return Failure(Code::Cancelled, "BSP spatial validation cancelled");
				auto f = stack.back();
				stack.pop_back();
				if (f.exit)
				{
					colour[f.index] = 2;
					continue;
				}
				if (colour[f.index] == 1 || f.depth > 60)
					return Failure(Code::InvalidInput, "Cyclic/deep BSP spatial tree");
				if (colour[f.index] == 2)
					continue;
				colour[f.index] = 1;
				stack.push_back({f.index, f.depth, true});
				for (auto child : s.nodes[f.index].children)
					if (child >= 0)
						stack.push_back({uint32_t(child), f.depth + 1, false});
			}
		}
		out = d;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "BSP spatial allocation");
	}
}
Error BspRegionDomain::Locate(Vec3 p, SpatialLocation &out, double tolerance) const
{
	out = SpatialLocation();
	if (!Finite(p) || !std::isfinite(tolerance) || tolerance < 0)
		return Failure(Code::InvalidInput, "Invalid BSP spatial point");
	if (!Inside(input_.worldBounds, p))
	{
		out.space = SpaceClass::Outside;
		return Error();
	}
	int32_t index = input_.nodes.empty() ? -1 : 0;
	while (index >= 0)
	{
		if (++out.work > limit_)
			return Failure(Code::WorkLimit, "BSP point work limit");
		const auto &n = input_.nodes[size_t(index)];
		const auto &plane = input_.planes[n.plane];
		index = n.children[Dot(plane.normal, p) - plane.distance >= 0 ? 0 : 1];
	}
	out.leaf = int32_t(-int64_t(index) - 1);
	const auto &leaf = input_.leaves[size_t(out.leaf)];
	out.cluster = leaf.cluster;
	out.area = leaf.area;
	bool boundary = false;
	for (uint32_t j = 0; j < leaf.brushCount; ++j)
	{
		const auto &b = input_.brushes[input_.leafBrushes[leaf.firstBrush + j]];
		if (!b.world || !b.solid)
			continue;
		bool contains = true, deep = true;
		for (uint32_t k = 0; k < b.sideCount; ++k)
		{
			if (++out.work > limit_)
				return Failure(Code::WorkLimit, "BSP point brush work limit");
			const auto &plane = input_.planes[input_.sides[b.firstSide + k]];
			double d = Dot(plane.normal, p) - plane.distance;
			if (d > tolerance)
			{
				contains = false;
				break;
			}
			if (d >= -tolerance)
				deep = false;
		}
		if (contains && deep)
		{
			out.space = b.sky ? SpaceClass::Sky : SpaceClass::Solid;
			return Error();
		}
		boundary = boundary || contains;
	}
	out.boundary = boundary;
	out.space =
		boundary ? SpaceClass::Unknown : leaf.cluster >= 0 ? SpaceClass::Free : SpaceClass::Unknown;
	return Error();
}
Error BspRegionDomain::ClassifyBox(const Bounds &box, SpaceClass &out, uint64_t &work) const
{
	uint32_t reasons = 0;
	return InspectBox(box, out, work, reasons);
}
Error BspRegionDomain::InspectBox(const Bounds &box, SpaceClass &out, uint64_t &work,
								  uint32_t &reasons) const
{
	reasons = 0;
	out = SpaceClass::Unknown;
	work = 0;
	if (!Valid(box))
		return Failure(Code::InvalidInput, "Invalid BSP spatial box");
	if (!Overlap(box, input_.worldBounds))
	{
		reasons |= BoxOutsideWorld;
		out = SpaceClass::Outside;
		return Error();
	}
	if (!Inside(input_.worldBounds, box.minWorld) || !Inside(input_.worldBounds, box.maxWorld))
	{
		reasons |= BoxOutsideWorld;
		return Error();
	}
	int32_t stack[64];
	size_t size = 1;
	stack[0] = input_.nodes.empty() ? -1 : 0;
	bool uncertain = false;
	while (size)
	{
		if (++work > limit_)
			return Failure(Code::WorkLimit, "BSP box work limit");
		int32_t i = stack[--size];
		if (i >= 0)
		{
			const auto &n = input_.nodes[size_t(i)];
			double low, high;
			PlaneRange(input_.planes[n.plane], box, low, high);
			if (size + 2 > 64)
				return Failure(Code::WorkLimit, "BSP box stack limit");
			if (high >= 0)
				stack[size++] = n.children[0];
			if (low <= 0)
				stack[size++] = n.children[1];
			continue;
		}
		const auto &leaf = input_.leaves[size_t(-int64_t(i) - 1)];
		uncertain = uncertain || leaf.cluster < 0;
		if (leaf.cluster < 0)
			reasons |= BoxUnclusteredLeaf;
		for (uint32_t j = 0; j < leaf.brushCount; ++j)
		{
			const auto &b = input_.brushes[input_.leafBrushes[leaf.firstBrush + j]];
			if (!b.world || !b.solid)
				continue;
			bool candidate = true, inside = true;
			for (uint32_t k = 0; k < b.sideCount; ++k)
			{
				if (++work > limit_)
					return Failure(Code::WorkLimit, "BSP box brush work limit");
				double lo, hi;
				PlaneRange(input_.planes[input_.sides[b.firstSide + k]], box, lo, hi);
				if (lo > 1e-5)
				{
					candidate = false;
					break;
				}
				if (hi >= -1e-5)
					inside = false;
			}
			if (candidate && inside)
			{
				reasons |= BoxInsideSolidBrush;
				out = b.sky ? SpaceClass::Sky : SpaceClass::Solid;
				return Error();
			}
			if (candidate)
			{
				// Necessary interval bounds for the conjunction of brush planes.
				// Unlike independent plane/AABB tests, repeated clipping can prove
				// disjointness at oblique corners. Failure to converge is Unknown.
				double low[3], high[3];
				for (unsigned axis = 0; axis < 3; ++axis)
				{
					low[axis] = Axis(box.minWorld, axis);
					high[axis] = Axis(box.maxWorld, axis);
				}
				for (unsigned pass = 0; pass < 4 && candidate; ++pass)
					for (uint32_t side = 0; side < b.sideCount && candidate; ++side)
					{
						if (++work > limit_)
							return Failure(Code::WorkLimit, "BSP brush interval work limit");
						const auto &plane = input_.planes[input_.sides[b.firstSide + side]];
						for (unsigned axis = 0; axis < 3 && candidate; ++axis)
						{
							double coefficient = Axis(plane.normal, axis), other = 0,
								   magnitude = 1 + std::abs(plane.distance);
							if (std::abs(coefficient) < 1e-15)
								continue;
							for (unsigned k = 0; k < 3; ++k)
								if (k != axis)
								{
									double term = Axis(plane.normal, k) *
												  (Axis(plane.normal, k) >= 0 ? low[k] : high[k]);
									other += term;
									magnitude += std::abs(term);
								}
							double round = 64 * std::numeric_limits<double>::epsilon() * magnitude;
							double bound = (plane.distance + 1e-5 + round - other) / coefficient;
							double outward =
								64 * std::numeric_limits<double>::epsilon() * (1 + std::abs(bound));
							if (coefficient > 0)
								high[axis] = std::min(high[axis], bound + outward);
							else
								low[axis] = std::max(low[axis], bound - outward);
							if (low[axis] > high[axis])
								candidate = false;
						}
					}
				if (!candidate)
					reasons |= BoxBrushDisjointRefined;
			}
			uncertain = uncertain || candidate;
			if (candidate)
				reasons |= BoxPossibleSolidBrush;
		}
	}
	out = uncertain ? SpaceClass::Unknown : SpaceClass::Free;
	return Error();
}

struct LightingRegionGraph::Data
{
	struct Node
	{
		uint32_t origin[3] = {}, units = 1, child = UINT32_MAX, cell = UINT32_MAX;
	};
	RegionBuildInput input;
	RegionOptions options;
	RegionReport report;
	uint32_t rootUnits = 1, dimensions[3] = {};
	std::vector<Node> nodes;
	std::vector<RegionCell> cells;
	std::vector<CellConnection> cellLinks;
	std::vector<LightingRegion> regions;
	std::vector<RegionConnection> connections;
	std::vector<uint32_t> members, neighbours;
	std::vector<RegionBspLink> bspLinks;
	std::vector<RegionBoundary> boundaries;
	uint64_t charged = sizeof(Data), work = 0;
	ReadContext context;
	Data(RegionBuildInput i, RegionOptions o, const ReadContext &c)
		: input(std::move(i)), options(o), context(c)
	{
	}
	void Check()
	{
		if (Cancelled(context))
			throw BuildFailure{Failure(Code::Cancelled, "Region build cancelled")};
	}
	void Charge(uint64_t n)
	{
		uint64_t cap = std::min(context.maxBytes, options.maxBytes);
		if (charged > cap || n > cap - charged)
			throw BuildFailure{Failure(Code::MemoryLimit, "Region graph build memory budget")};
		charged += n;
	}
	void Work(uint64_t n)
	{
		if (n > options.maxWork - work)
			throw BuildFailure{Failure(Code::WorkLimit, "Region graph work budget")};
		work += n;
	}
	Bounds Box(const Node &n) const
	{
		Bounds b;
		b.known = true;
		for (unsigned k = 0; k < 3; ++k)
		{
			Set(b.minWorld, k,
				Axis(options.bounds.minWorld, k) + double(n.origin[k]) * options.minimumCellWorld);
			Set(b.maxWorld, k,
				Axis(options.bounds.minWorld, k) +
					double(n.origin[k] + n.units) * options.minimumCellWorld);
		}
		return b;
	}
	Bounds Box(const RegionCell &c) const
	{
		Node n;
		n.units = c.units;
		std::copy(c.origin, c.origin + 3, n.origin);
		return Box(n);
	}
	uint32_t Root(const uint32_t p[3]) const
	{
		return (p[2] / rootUnits * dimensions[1] + p[1] / rootUnits) * dimensions[0] +
			   p[0] / rootUnits;
	}
	uint32_t FindCell(Vec3 p) const
	{
		uint32_t fine[3];
		for (unsigned k = 0; k < 3; ++k)
		{
			double x = (Axis(p, k) - Axis(options.bounds.minWorld, k)) / options.minimumCellWorld;
			if (x < 0 || x >= double(dimensions[k]) * rootUnits)
				return UINT32_MAX;
			fine[k] = uint32_t(x);
		}
		uint32_t i = Root(fine);
		while (nodes[i].child != UINT32_MAX)
		{
			unsigned child = 0;
			for (unsigned k = 0; k < 3; ++k)
				if (fine[k] >= nodes[i].origin[k] + nodes[i].units / 2)
					child |= 1u << k;
			i = nodes[i].child + child;
		}
		return nodes[i].cell;
	}
	Disk::Digest key{};
	void Subdivide(uint32_t index)
	{
		Check();
		Node n = nodes[index];
		Bounds b = Box(n);
		SpaceClass space;
		uint64_t tests = 0;

		uint32_t bspReasons = 0;
		if (auto bsp = dynamic_cast<const BspRegionDomain *>(input.domain.get()))
			Require(bsp->InspectBox(b, space, tests, bspReasons));
		else
			Require(input.domain->ClassifyBox(b, space, tests));
		Work(tests);
		Work(1);
		report.domainWork += tests;
		if (static_cast<unsigned>(space) > static_cast<unsigned>(SpaceClass::Exterior))
			throw BuildFailure{Failure(Code::ProviderError, "Invalid spatial box classification")};
		const SpaceClass bspSpace = space;
		GeometryBoxResult geometry;
		if (Free(space) || space == SpaceClass::Unknown)
		{
			Require(input.geometry->OverlapBox(b, context, geometry, options.maxRayWork));
			Work(geometry.work);
			report.geometryWork += geometry.work;
		}
		bool moving = false;
		for (const auto &d : input.dynamic)
		{
			Work(1);
			if (Overlap(b, d.bounds))
			{
				moving = true;
				break;
			}
		}
		if (Free(space) && (geometry.intersects || !geometry.coverageComplete))
			space = SpaceClass::Unknown;
		// Refinement cannot resolve an unbounded omission. Refine only for
		// spatially local uncertainty, preserving the global reason on coarse cells.
		if (n.units > 1 && (bspSpace == SpaceClass::Unknown || geometry.intersects || moving))
		{
			if (nodes.size() + 8 > options.maxNodes)
				throw BuildFailure{Failure(Code::MemoryLimit, "Region octree node limit")};
			Charge(8 * sizeof(Node) * 3);
			uint32_t first = uint32_t(nodes.size());
			nodes[index].child = first;
			for (unsigned child = 0; child < 8; ++child)
			{
				Node c = n;
				c.units /= 2;
				for (unsigned k = 0; k < 3; ++k)
					if (child & (1u << k))
						c.origin[k] += c.units;
				nodes.push_back(c);
			}
			for (unsigned child = 0; child < 8; ++child)
				Subdivide(first + child);
			return;
		}
		if (cells.size() >= options.maxCells)
			throw BuildFailure{Failure(Code::MemoryLimit, "Region leaf cell limit")};
		Charge(sizeof(RegionCell) * 3 + 256);
		RegionCell c;
		c.id = cells.size() + 1;
		c.units = n.units;
		std::copy(n.origin, n.origin + 3, c.origin);
		c.representativeWorld = Centre(b);
		c.space = space;
		Require(input.domain->Locate(c.representativeWorld, c.bsp));
		Work(c.bsp.work);
		Work(1);
		report.domainWork += c.bsp.work;
		if (static_cast<unsigned>(c.bsp.space) > static_cast<unsigned>(SpaceClass::Exterior))
			throw BuildFailure{
				Failure(Code::ProviderError, "Invalid spatial point classification")};
		if (Free(space) && !Free(c.bsp.space))
			c.space = SpaceClass::Unknown;

		c.issues = CellBspMembershipSampled;
		report.bspFree += Free(bspSpace);
		report.bspSolid += bspSpace == SpaceClass::Solid;
		report.bspUnknown += bspSpace == SpaceClass::Unknown;
		report.bspUnclustered += bool(bspReasons & BoxUnclusteredLeaf);
		report.bspPossibleBrush += bool(bspReasons & BoxPossibleSolidBrush);
		report.bspOutsideWorld += bool(bspReasons & BoxOutsideWorld);
		report.bspRefinedDisjoint += bool(bspReasons & BoxBrushDisjointRefined);
		if (bspSpace == SpaceClass::Unknown)
			c.issues |= CellBspUncertain;
		if (bspReasons & BoxUnclusteredLeaf)
			c.issues |= CellBspUnclustered;
		if (bspReasons & BoxPossibleSolidBrush)
			c.issues |= CellBspBrushBoundary;
		if (bspReasons & BoxOutsideWorld)
			c.issues |= CellOutsideWorldBounds;
		if (bspSpace == SpaceClass::Solid)
			c.issues |= CellBspSolid;
		if (Free(bspSpace) && geometry.intersects)
			++report.localInvalidated;
		if (Free(bspSpace) && !geometry.intersects && !geometry.coverageComplete)
			++report.globalOnlyInvalidated;
		if ((Free(bspSpace) || bspSpace == SpaceClass::Unknown) && !geometry.coverageComplete)
			c.issues |= CellUnboundedGeometry;
		if (geometry.issues & UnsupportedGeometry)
		{
			c.issues |= CellUnsupportedGeometry;
			++report.unsupportedUncertain;
		}
		if (geometry.issues & OpticalUnknown)
		{
			c.issues |= CellOpticalUncertain;
			++report.opticalUncertain;
		}
		if (geometry.issues & PatchApproximation)
			c.issues |= CellPatchEnvelope;
		if (bspSpace != c.bsp.space && !(Free(bspSpace) && Free(c.bsp.space)))
		{
			c.issues |= CellRepresentativeDisagrees;
			++report.representativeDisagrees;
		}
		if (moving)
			++report.movingUncertain;
		if (c.space == SpaceClass::Unknown && !geometry.intersects && geometry.coverageComplete)
			++report.locallyClearUnassigned;
		if (geometry.intersects)
			c.issues |= CellGeometryBoundary;
		if (geometry.referencesTruncated)
			c.issues |= CellBoundaryRefsTruncated;
		if (moving)
			c.issues |= CellDynamicPossible;
		if (c.space == SpaceClass::Unknown && n.units == 1 &&
			(bspSpace == SpaceClass::Unknown || geometry.intersects))
		{
			c.issues |= CellResolutionLimit;
			++report.minimumLimited;
			report.resolutionLimited = true;
		}
		c.boundaryCount = uint32_t(geometry.surfaceCount);
		std::copy(geometry.surfaces, geometry.surfaces + geometry.surfaceCount, c.boundarySurfaces);
		nodes[index].cell = uint32_t(cells.size());
		cells.push_back(c);
	}
	template <class F>
	void FaceVisit(uint32_t i, const RegionCell &source, unsigned axis, uint32_t coordinate, F &f)
	{
		Work(1);
		const auto &n = nodes[i];
		if (coordinate < n.origin[axis] || coordinate >= n.origin[axis] + n.units)
			return;
		for (unsigned k = 0; k < 3; ++k)
			if (k != axis && (source.origin[k] + source.units <= n.origin[k] ||
							  source.origin[k] >= n.origin[k] + n.units))
				return;
		if (n.child == UINT32_MAX)
		{
			f(n.cell);
			return;
		}
		for (unsigned child = 0; child < 8; ++child)
			FaceVisit(n.child + child, source, axis, coordinate, f);
	}
	template <class F> void Face(uint32_t cell, unsigned axis, bool positive, F f)
	{
		const auto &c = cells[cell];
		if (!positive && c.origin[axis] == 0)
			return;
		uint32_t p[3];
		std::copy(c.origin, c.origin + 3, p);
		p[axis] = positive ? c.origin[axis] + c.units : c.origin[axis] - 1;
		if (p[axis] >= dimensions[axis] * rootUnits)
			return;
		FaceVisit(Root(p), c, axis, p[axis], f);
	}
	void MakeLinks()
	{
		for (uint32_t i = 0; i < cells.size(); ++i)
		{
			Check();
			if (!Free(cells[i].space))
				continue;
			for (unsigned axis = 0; axis < 3; ++axis)
				Face(i, axis, true, [&](uint32_t j) {
					if (!Free(cells[j].space))
						return;
					if (cellLinks.size() >= options.maxLinks)
						throw BuildFailure{Failure(Code::MemoryLimit, "Region adjacency limit")};
					Charge(sizeof(CellConnection) * 3 + 256);
					CellConnection link;
					link.a = i;
					link.b = j;
					link.axis = uint8_t(axis);
					link.openingBounds.known = true;
					double area = 1;
					for (unsigned k = 0; k < 3; ++k)
					{
						double low = std::max(Axis(Box(cells[i]).minWorld, k),
											  Axis(Box(cells[j]).minWorld, k));
						double high = std::min(Axis(Box(cells[i]).maxWorld, k),
											   Axis(Box(cells[j]).maxWorld, k));
						Set(link.openingBounds.minWorld, k, low);
						Set(link.openingBounds.maxWorld, k, high);
						if (k != axis)
							area *= std::max(0., high - low);
					}
					link.faceAreaWorldSquared = area;
					link.openingCentreWorld = Centre(link.openingBounds);
					RayQuery q;
					q.startWorld = cells[i].representativeWorld;
					q.endWorld = cells[j].representativeWorld;
					q.originKnownFree = true;
					q.policy.facing = FacingPolicy::TwoSided;
					q.maxWork = options.maxRayWork;
					RayResult ray;
					Require(input.geometry->Query(q, context, ray));
					++report.rays;
					Work(ray.nodeTests + ray.primitiveTests + 1);
					report.geometryWork += ray.nodeTests + ray.primitiveTests;
					link.direct = ray.reliability == RayReliability::Geometric &&
										  ray.state == VisibilityState::Visible
									  ? DirectConnection::ClearStaticSample
									  : ray.reliability == RayReliability::Geometric &&
												ray.state == VisibilityState::Blocked
											? DirectConnection::BlockedSample
											: DirectConnection::Unknown;
					bool dynamic = ((cells[i].issues | cells[j].issues) & CellDynamicPossible) != 0;
					link.transport = link.direct == DirectConnection::BlockedSample
										 ? TransportConnection::Blocked
										 : dynamic
											   ? TransportConnection::ConditionalDynamic
											   : link.direct == DirectConnection::ClearStaticSample
													 ? TransportConnection::OpenStaticSample
													 : TransportConnection::Unknown;
					cellLinks.push_back(link);
				});
		}
	}
	void Segment()
	{
		const size_t count = cells.size();
		Dsu components(count), wide(count), dynamic(count);
		auto isWide = [&](uint32_t i) {
			return Free(cells[i].space) && !(cells[i].issues & CellDynamicPossible) &&
				   double(cells[i].units) * options.minimumCellWorld >= options.constrictionWorld;
		};
		std::vector<uint32_t> starts(count + 1), adj(cellLinks.size() * 2);
		for (const auto &l : cellLinks)
		{
			++starts[l.a + 1];
			++starts[l.b + 1];
			if (l.transport == TransportConnection::OpenStaticSample)
			{
				components.Join(l.a, l.b);
				if (isWide(l.a) && isWide(l.b))
					wide.Join(l.a, l.b);
			}
			if (l.direct == DirectConnection::ClearStaticSample &&
				(cells[l.a].issues & CellDynamicPossible) &&
				(cells[l.b].issues & CellDynamicPossible))
				dynamic.Join(l.a, l.b);
		}
		for (size_t i = 1; i < starts.size(); ++i)
			starts[i] += starts[i - 1];
		auto cursor = starts;
		for (uint32_t i = 0; i < cellLinks.size(); ++i)
		{
			adj[cursor[cellLinks[i].a]++] = i;
			adj[cursor[cellLinks[i].b]++] = i;
		}
		std::vector<Id> labels(count);
		std::vector<uint32_t> queue;
		queue.reserve(count);
		std::set<uint32_t> seeded;
		for (uint32_t i = 0; i < count; ++i)
			if (isWide(i))
			{
				labels[i] = Id(wide.Root(i)) + 1;
				queue.push_back(i);
				seeded.insert(components.Root(i));
			}
		for (uint32_t i = 0; i < count; ++i)
			if (Free(cells[i].space) && !(cells[i].issues & CellDynamicPossible) &&
				!seeded.count(components.Root(i)))
			{
				labels[i] = Id(components.Root(i)) + 1;
				queue.push_back(i);
				seeded.insert(components.Root(i));
			}
		for (size_t head = 0; head < queue.size(); ++head)
		{
			Check();
			uint32_t a = queue[head];
			for (uint32_t k = starts[a]; k < starts[a + 1]; ++k)
			{
				const auto &l = cellLinks[adj[k]];
				if (l.transport != TransportConnection::OpenStaticSample)
					continue;
				uint32_t b = l.a == a ? l.b : l.a;
				if (!labels[b])
				{
					labels[b] = labels[a];
					queue.push_back(b);
				}
			}
		}
		std::map<Id, uint32_t> regionIndex;
		std::set<Id> componentIds;
		for (uint32_t i = 0; i < count; ++i)
			if (Free(cells[i].space))
			{
				if (cells[i].issues & CellDynamicPossible)
					labels[i] = Id(dynamic.Root(i)) + 1;
				if (!labels[i])
					throw BuildFailure{
						Failure(Code::AlgorithmError, "Unassigned certified free cell")};
				cells[i].region = labels[i];
				regionIndex.emplace(labels[i], 0);
				if (!(cells[i].issues & CellDynamicPossible))
					componentIds.insert(Id(components.Root(i)) + 1);
			}
		report.staticComponents = componentIds.size();
		for (auto &entry : regionIndex)
		{
			entry.second = uint32_t(regions.size());
			LightingRegion r;
			r.id = entry.first;
			regions.push_back(r);
		}
		for (uint32_t i = 0; i < count; ++i)
		{
			const auto &c = cells[i];
			if (!c.region)
				continue;
			auto &r = regions[regionIndex[c.region]];
			if (!r.memberCount)
				r.representativeWorld = c.representativeWorld;
			Expand(r.bounds, Box(c));
			++r.memberCount;
			if (c.issues & CellDynamicPossible)
				r.issues |= RegionDynamicUncertain;
			else
				r.staticComponent = Id(components.Root(i)) + 1;
			if (c.space == SpaceClass::Exterior)
				r.issues |= RegionExteriorKnown;
			bspLinks.push_back({c.region, c.bsp.leaf, c.bsp.cluster, c.bsp.area});
		}
		uint32_t first = 0;
		for (auto &r : regions)
		{
			r.firstMember = first;
			first += r.memberCount;
		}
		members.resize(first);
		std::vector<uint32_t> positions(regions.size());
		for (uint32_t i = 0; i < count; ++i)
			if (cells[i].region)
			{
				uint32_t r = regionIndex[cells[i].region];
				members[regions[r].firstMember + positions[r]++] = i;
			}
		std::sort(bspLinks.begin(), bspLinks.end(),
				  [](const RegionBspLink &a, const RegionBspLink &b) {
					  return std::tie(a.region, a.leaf, a.cluster, a.area) <
							 std::tie(b.region, b.leaf, b.cluster, b.area);
				  });
		bspLinks.erase(std::unique(bspLinks.begin(), bspLinks.end(),
								   [](const RegionBspLink &a, const RegionBspLink &b) {
									   return std::tie(a.region, a.leaf, a.cluster, a.area) ==
											  std::tie(b.region, b.leaf, b.cluster, b.area);
								   }),
					   bspLinks.end());
		std::map<std::pair<Id, Id>, RegionConnection> edges;
		for (const auto &l : cellLinks)
		{
			Id a = cells[l.a].region, b = cells[l.b].region;
			if (a == b)
				continue;
			if (a > b)
				std::swap(a, b);
			auto &e = edges[{a, b}];
			e.a = a;
			e.b = b;
			if (l.direct == DirectConnection::ClearStaticSample)
				++e.clearSamples;
			else if (l.direct == DirectConnection::BlockedSample)
				++e.blockedSamples;
			else
				++e.unknownSamples;
			e.dynamicPossible =
				e.dynamicPossible || l.transport == TransportConnection::ConditionalDynamic;
			Expand(e.sampledOpeningBounds, l.openingBounds);
			double total = e.sampledAreaWorldSquared + l.faceAreaWorldSquared;
			if (total > 0)
				for (unsigned k = 0; k < 3; ++k)
					Set(e.sampledOpeningCentreWorld, k,
						Axis(e.sampledOpeningCentreWorld, k) * (e.sampledAreaWorldSquared / total) +
							Axis(l.openingCentreWorld, k) * (l.faceAreaWorldSquared / total));
			e.sampledAreaWorldSquared = total;
		}
		for (auto &pair : edges)
		{
			auto e = pair.second;
			e.apertureCandidate = e.clearSamples > 0 || e.dynamicPossible;
			e.transport = e.dynamicPossible
							  ? TransportConnection::ConditionalDynamic
							  : e.clearSamples ? TransportConnection::OpenStaticSample
											   : e.unknownSamples ? TransportConnection::Unknown
																  : TransportConnection::Blocked;
			connections.push_back(e);
			++regions[regionIndex[e.a]].neighbourCount;
			++regions[regionIndex[e.b]].neighbourCount;
			regions[regionIndex[e.a]].issues |= RegionRoomCandidate;
			regions[regionIndex[e.b]].issues |= RegionRoomCandidate;
		}
		first = 0;
		for (auto &r : regions)
		{
			r.firstNeighbour = first;
			first += r.neighbourCount;
		}
		neighbours.resize(first);
		std::fill(positions.begin(), positions.end(), 0);
		for (uint32_t i = 0; i < connections.size(); ++i)
			for (Id id : {connections[i].a, connections[i].b})
			{
				uint32_t r = regionIndex[id];
				neighbours[regions[r].firstNeighbour + positions[r]++] = i;
			}
		for (uint32_t i = 0; i < count; ++i)
			if (cells[i].region)
				for (unsigned axis = 0; axis < 3; ++axis)
					for (bool positive : {false, true})
						Face(i, axis, positive, [&](uint32_t j) {
							if (Free(cells[j].space))
								return;
							const auto &c = cells[j];
							for (uint32_t k = 0; k < c.boundaryCount; ++k)
							{
								Charge(sizeof(RegionBoundary) * 3);
								boundaries.push_back({cells[i].region, c.boundarySurfaces[k]});
							}
							if (c.space == SpaceClass::Unknown)
								regions[regionIndex[cells[i].region]].issues |=
									RegionBoundaryIncomplete;
						});
		std::sort(boundaries.begin(), boundaries.end(),
				  [](const RegionBoundary &a, const RegionBoundary &b) {
					  return std::tie(a.region, a.surface) < std::tie(b.region, b.surface);
				  });
		boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
									 [](const RegionBoundary &a, const RegionBoundary &b) {
										 return a.region == b.region && a.surface == b.surface;
									 }),
						 boundaries.end());
		if (options.inspectSky)
			for (auto &r : regions)
			{
				unsigned samples = std::min(options.skySamplesPerRegion, r.memberCount);
				for (unsigned sample = 0; sample < samples; ++sample)
				{
					Check();
					RayQuery q;
					uint32_t member =
						samples == 1
							? 0
							: uint32_t(uint64_t(sample) * (r.memberCount - 1) / (samples - 1));
					q.startWorld = cells[members[r.firstMember + member]].representativeWorld;
					q.endWorld = q.startWorld;
					q.endWorld.z = options.bounds.maxWorld.z + options.coarseCellWorld;
					q.purpose = RayPurpose::Sky;
					q.originKnownFree = true;
					q.policy.facing = FacingPolicy::TwoSided;
					q.maxWork = options.maxRayWork;
					RayResult ray;
					Require(input.geometry->Query(q, context, ray));
					++report.rays;
					Work(ray.nodeTests + ray.primitiveTests + 1);
					report.geometryWork += ray.nodeTests + ray.primitiveTests;
					if (ray.sky == SkyReach::Reached &&
						ray.reliability == RayReliability::Geometric)
					{
						r.issues |= RegionSkyObserved;
						++report.skyRegions;
						break;
					}
				}
			}
	}
};

Error LightingRegionGraph::Build(RegionBuildInput input, const RegionOptions &o,
								 const ReadContext &ctx,
								 std::shared_ptr<const LightingRegionGraph> &out)
{
	out.reset();
	if (!input.domain || !input.geometry)
		return Failure(Code::MissingGeometry, "Region graph requires domain and static geometry");
	if (!Valid(o.bounds) || !std::isfinite(o.coarseCellWorld) ||
		!std::isfinite(o.minimumCellWorld) || !std::isfinite(o.constrictionWorld) ||
		o.minimumCellWorld < 1e-4 || o.coarseCellWorld < o.minimumCellWorld ||
		o.constrictionWorld <= 0 || !o.maxNodes || !o.maxCells || !o.maxLinks ||
		o.maxNodes > UINT32_MAX / 8 || o.maxCells > UINT32_MAX / 4 || o.maxLinks > UINT32_MAX / 4 ||
		!o.maxBytes || !o.maxWork || !o.maxRayWork || !o.skySamplesPerRegion ||
		o.skySamplesPerRegion > 256)
		return Failure(Code::InvalidInput, "Invalid region bounds/resolution/budget");
	if (input.identity.path != input.geometry->GeometryIdentity().path ||
		input.identity.sha256 != input.geometry->GeometryIdentity().sha256)
		return Failure(Code::InvalidInput, "Region/visibility map identities differ");
	const auto *domainIdentity = input.domain->SourceIdentity();
	if (domainIdentity && (domainIdentity->path != input.identity.path ||
						   domainIdentity->sha256 != input.identity.sha256))
		return Failure(Code::InvalidInput, "Region/domain map identities differ");
	if (input.dynamic.size() > o.maxDynamicBounds)
		return Failure(Code::MemoryLimit, "Region dynamic boundary limit");
	double ratio = double(o.coarseCellWorld) / o.minimumCellWorld;
	unsigned units = 1;
	while (units < ratio && units < 1024)
		units *= 2;
	if (units != ratio)
		return Failure(Code::InvalidInput, "Region resolution ratio must be a power of two <=1024");
	for (const auto &d : input.dynamic)
		if (!d.id || !Valid(d.bounds))
			return Failure(Code::InvalidInput, "Invalid dynamic boundary");
	try
	{
		std::shared_ptr<Data> d(new Data(std::move(input), o, ctx));
		d->Charge(d->input.dynamic.size() * sizeof(DynamicBoundary) * 3 +
				  d->input.identity.path.size());
		d->rootUnits = units;
		d->Check();
		uint64_t roots = 1;
		for (unsigned k = 0; k < 3; ++k)
		{
			double magnitude = std::max(std::abs(Axis(o.bounds.minWorld, k)),
										std::abs(Axis(o.bounds.maxWorld, k)));
			if (o.minimumCellWorld <
				4 * std::numeric_limits<float>::epsilon() * std::max(1., magnitude))
				return Failure(Code::InvalidInput,
							   "Region resolution below float coordinate precision");
			double count = std::ceil((Axis(o.bounds.maxWorld, k) - Axis(o.bounds.minWorld, k)) /
									 o.coarseCellWorld);
			if (count < 1 || count > UINT32_MAX / units || count > double(o.maxNodes) ||
				roots > o.maxNodes / uint64_t(count))
				return Failure(Code::MemoryLimit, "Region root grid budget");
			d->dimensions[k] = uint32_t(count);
			roots *= uint64_t(count);
		}
		d->Charge(roots * sizeof(Data::Node) * 3);
		d->nodes.resize(size_t(roots));
		for (uint32_t z = 0; z < d->dimensions[2]; ++z)
			for (uint32_t y = 0; y < d->dimensions[1]; ++y)
				for (uint32_t x = 0; x < d->dimensions[0]; ++x)
				{
					auto &n = d->nodes[(z * d->dimensions[1] + y) * d->dimensions[0] + x];
					n.units = units;
					n.origin[0] = x * units;
					n.origin[1] = y * units;
					n.origin[2] = z * units;
				}
		for (uint32_t i = 0; i < roots; ++i)
			d->Subdivide(i);
		d->MakeLinks();
		d->Segment();
		for (const auto &c : d->cells)
		{
			if (Free(c.space))
			{
				++d->report.freeCells;
				if (c.issues & CellDynamicPossible)
					++d->report.dynamicCells;
			}
			else if (c.space == SpaceClass::Solid)
				++d->report.solidCells;
			else if (c.space == SpaceClass::Outside)
				++d->report.outsideCells;
			else if (c.space == SpaceClass::Sky)
				++d->report.skyCells;
			else
				++d->report.unknownCells;
		}
		for (const auto &v : d->input.dynamic)
			d->report.dynamicExtentUnknown = d->report.dynamicExtentUnknown || !v.sweptExtentKnown;
		d->report.regions = d->regions.size();
		d->report.connections = d->connections.size();
		for (const auto &e : d->connections)
			if (e.apertureCandidate)
				++d->report.apertures;
		d->report.peakBuildBytes = d->charged;
		d->report.ownedBytes =
			sizeof(Data) + d->nodes.capacity() * sizeof(Data::Node) +
			d->cells.capacity() * sizeof(RegionCell) +
			d->cellLinks.capacity() * sizeof(CellConnection) +
			d->regions.capacity() * sizeof(LightingRegion) +
			d->connections.capacity() * sizeof(RegionConnection) +
			(d->members.capacity() + d->neighbours.capacity()) * sizeof(uint32_t) +
			d->bspLinks.capacity() * sizeof(RegionBspLink) +
			d->boundaries.capacity() * sizeof(RegionBoundary) +
			d->input.dynamic.capacity() * sizeof(DynamicBoundary) + d->input.identity.path.size();

		GeometryKey key;
		const auto &options = d->options;
		key.U(kLightingRegionGraphVersion);
		key.Digest(d->input.identity.sha256);
		key.Digest(d->input.geometry->CertificationKey());
		key.B(options.bounds);
		key.F(options.coarseCellWorld);
		key.F(options.minimumCellWorld);
		key.F(options.constrictionWorld);
		key.U(options.inspectSky);
		key.U(options.skySamplesPerRegion);
		key.U(options.maxNodes);
		key.U(options.maxCells);
		key.U(options.maxLinks);
		key.U(options.maxDynamicBounds);
		key.U(options.maxBytes);
		key.U(options.maxWork);
		key.U(options.maxRayWork);
		// Custom domains have no persistent certification unless they supply a
		// canonical BSP snapshot. Their IDs remain lease-local.
		if (auto bsp = dynamic_cast<const BspRegionDomain *>(d->input.domain.get()))
		{
			auto &s = bsp->Source();
			key.B(s.worldBounds);
			key.U(s.planes.size());
			for (auto &p : s.planes)
			{
				key.V(p.normal);
				key.F(p.distance);
			}
			key.U(s.nodes.size());
			for (auto &n : s.nodes)
			{
				key.U(n.plane);
				key.U(n.children[0]);
				key.U(n.children[1]);
			}
			key.U(s.leaves.size());
			for (auto &l : s.leaves)
			{
				key.U(l.cluster);
				key.U(l.area);
				key.U(l.firstBrush);
				key.U(l.brushCount);
			}
			key.U(s.brushes.size());
			for (auto &b : s.brushes)
			{
				key.U(b.firstSide);
				key.U(b.sideCount);
				key.U(b.solid);
				key.U(b.sky);
				key.U(b.world);
			}
			key.U(s.sides.size());
			for (auto i : s.sides)
				key.U(i);
			key.U(s.leafBrushes.size());
			for (auto i : s.leafBrushes)
				key.U(i);
		}
		key.U(d->input.dynamic.size());
		for (auto &b : d->input.dynamic)
		{
			key.U(b.id);
			key.U(b.model);
			key.B(b.bounds);
			key.U(b.sweptExtentKnown);
		}
		d->key = key.Finish();
		d->context = ReadContext();
		out = std::shared_ptr<const LightingRegionGraph>(new LightingRegionGraph(d));
		return Error();
	}
	catch (const BuildFailure &f)
	{
		return f.error;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Region graph allocation");
	}
	catch (const std::exception &)
	{
		return Failure(Code::ProviderError, "Region provider threw");
	}
}
const LightingRegion *LightingRegionGraph::Find(Id id) const
{
	auto i = std::lower_bound(data_->regions.begin(), data_->regions.end(), id,
							  [](const LightingRegion &r, Id v) { return r.id < v; });
	return i != data_->regions.end() && i->id == id ? &*i : nullptr;
}
const std::vector<RegionCell> &LightingRegionGraph::Cells() const
{
	return data_->cells;
}
const std::vector<LightingRegion> &LightingRegionGraph::Regions() const
{
	return data_->regions;
}
const std::vector<RegionConnection> &LightingRegionGraph::Connections() const
{
	return data_->connections;
}
const std::vector<CellConnection> &LightingRegionGraph::CellConnections() const
{
	return data_->cellLinks;
}
const std::vector<uint32_t> &LightingRegionGraph::Members() const
{
	return data_->members;
}
const std::vector<uint32_t> &LightingRegionGraph::Neighbours() const
{
	return data_->neighbours;
}
const std::vector<RegionBspLink> &LightingRegionGraph::BspLinks() const
{
	return data_->bspLinks;
}
const std::vector<RegionBoundary> &LightingRegionGraph::Boundaries() const
{
	return data_->boundaries;
}
const RegionReport &LightingRegionGraph::Report() const
{
	return data_->report;
}
const RegionOptions &LightingRegionGraph::Options() const
{
	return data_->options;
}
const Disk::Digest &LightingRegionGraph::CertificationKey() const
{
	return data_->key;
}
bool LightingRegionGraph::PortableCertification() const
{
	return dynamic_cast<const BspRegionDomain *>(data_->input.domain.get()) != nullptr;
}
bool LightingRegionGraph::MatchesSources(const std::shared_ptr<const SurfaceGeometry> &geometry,
										 const std::shared_ptr<const RegionDomain> &domain,
										 const std::vector<DynamicBoundary> &dynamic) const
{
	if (!data_->input.geometry->BuiltFrom(geometry) || data_->input.domain != domain ||
		data_->input.dynamic.size() != dynamic.size())
		return false;
	for (size_t i = 0; i < dynamic.size(); ++i)
	{
		auto &a = data_->input.dynamic[i];
		auto &b = dynamic[i];
		if (a.id != b.id || a.model != b.model || a.sweptExtentKnown != b.sweptExtentKnown ||
			a.bounds.known != b.bounds.known)
			return false;
		for (unsigned k = 0; k < 3; ++k)
			if (Axis(a.bounds.minWorld, k) != Axis(b.bounds.minWorld, k) ||
				Axis(a.bounds.maxWorld, k) != Axis(b.bounds.maxWorld, k))
				return false;
	}
	return true;
}
const LightmapMapIdentity &LightingRegionGraph::Identity() const
{
	return data_->input.identity;
}
Error LightingRegionGraph::Lookup(Vec3 p, const ReadContext &ctx, RegionAssignment &out) const
{
	out = RegionAssignment();
	if (!Finite(p))
		return Failure(Code::InvalidInput, "Invalid region lookup point");
	if (Cancelled(ctx))
		return Failure(Code::Cancelled, "Region lookup cancelled");
	Error error;
	try
	{
		error = data_->input.domain->Locate(p, out.bsp);
	}
	catch (...)
	{
		out = RegionAssignment();
		return Failure(Code::ProviderError, "Region lookup provider threw");
	}
	if (static_cast<unsigned>(error.code) > static_cast<unsigned>(Code::AlgorithmError))
	{
		out = RegionAssignment();
		return Failure(Code::ProviderError, "Invalid region provider error");
	}
	if (!error)
	{
		out = RegionAssignment();
		return error;
	}
	if (static_cast<unsigned>(out.bsp.space) > static_cast<unsigned>(SpaceClass::Exterior) ||
		static_cast<unsigned>(error.code) > static_cast<unsigned>(Code::AlgorithmError))
	{
		out = RegionAssignment();
		return Failure(Code::ProviderError, "Invalid region lookup provider result");
	}
	if (Cancelled(ctx))
	{
		out = RegionAssignment();
		return Failure(Code::Cancelled, "Region lookup cancelled");
	}
	out.outsideAnalysisBounds = !Inside(data_->options.bounds, p);
	if (out.bsp.space == SpaceClass::Solid)
	{
		out.state = RegionAssignmentState::Solid;
		return Error();
	}
	if (out.bsp.space == SpaceClass::Sky)
	{
		out.state = RegionAssignmentState::Sky;
		return Error();
	}
	if (out.bsp.space == SpaceClass::Outside)
	{
		out.state = RegionAssignmentState::Outside;
		return Error();
	}
	if (out.outsideAnalysisBounds)
		return Error();
	uint32_t cell = data_->FindCell(p);
	if (cell == UINT32_MAX)
		return Error();
	const auto &c = data_->cells[cell];
	out.cell = c.id;
	out.issues = c.issues;
	if (Free(c.space) && Free(out.bsp.space) && c.region)
	{
		out.state = RegionAssignmentState::Assigned;
		out.region = c.region;
		out.staticComponent = Find(c.region)->staticComponent;
	}
	return Error();
}
Error LightingRegionGraph::AssignGrid(const GridCell &cell, const ReadContext &ctx,
									  RegionAssignment &out) const
{
	out = RegionAssignment();
	if (cell.presence != GridPresence::Present)
	{
		out.state = RegionAssignmentState::MissingObservation;
		return Error();
	}
	return Lookup(cell.centreWorld, ctx, out);
}
Error LightingRegionGraph::AssignSurfaceSides(Vec3 p, Vec3 n, Id surface, const ReadContext &ctx,
											  SurfaceRegionSides &out, double maximum) const
{
	out = SurfaceRegionSides();
	if (!Finite(p) || !Finite(n) || std::abs(Dot(n, n) - 1) > 1e-4 || !surface ||
		surface > data_->input.geometry->Surfaces().size() || !std::isfinite(maximum) ||
		maximum < 0)
		return Failure(Code::InvalidInput, "Invalid surface region-side query");
	if (maximum == 0)
		maximum = double(data_->options.minimumCellWorld) * 4;
	out.offsetWorld = maximum;
	for (int side : {1, -1})
	{
		RegionAssignment &assignment = side > 0 ? out.positive : out.negative;
		for (unsigned step = 0; step < 8; ++step)
		{
			double distance = maximum * double(step + 1) / 8;
			Vec3 probe = Add(p, n, side * distance);
			RegionAssignment candidate;
			auto error = Lookup(probe, ctx, candidate);
			if (!error)
				return error;
			if (candidate.state != RegionAssignmentState::Assigned)
			{
				if (step == 0)
					assignment = candidate;
				continue;
			}
			RayQuery ray;
			ray.startWorld = p;
			ray.endWorld = probe;
			ray.originSurface = surface;
			ray.bias.normalKnown = true;
			ray.bias.geometricNormal = n;
			ray.originKnownFree = true;
			ray.policy.facing = FacingPolicy::TwoSided;
			ray.maxWork = data_->options.maxRayWork;
			RayResult hit;
			error = data_->input.geometry->Query(ray, ctx, hit);
			if (!error)
				return error;
			if (hit.state == VisibilityState::Visible &&
				hit.reliability == RayReliability::Geometric)
			{
				assignment = candidate;
				(side > 0 ? out.positiveOffsetWorld : out.negativeOffsetWorld) = distance;
				break;
			}
			if (hit.state == VisibilityState::Blocked)
				break;
		}
	}
	return Error();
}
Error LightingRegionGraph::AssignGrid(const LightgridObservations &grid, uint64_t ordinal,
									  const ReadContext &ctx, RegionAssignment &out) const
{
	out = RegionAssignment();
	if (grid.Map().path != Identity().path || grid.Map().sha256 != Identity().sha256)
		return Failure(Code::InvalidInput, "Lightgrid/region map identities differ");
	GridCell cell;
	auto error = grid.Cell(ordinal, cell);
	if (!error)
		return error;
	return AssignGrid(cell, ctx, out);
}
Error LightingRegionGraph::AssignSurface(const LuxelTile &tile, size_t sample,
										 const ReadContext &ctx, SurfaceRegionSides &out,
										 double maximum) const
{
	out = SurfaceRegionSides();
	if (!tile.geometry)
		return Failure(Code::Unavailable, "Missing luxel geometry lease");
	if (tile.geometry->Map().path != Identity().path ||
		tile.geometry->Map().sha256 != Identity().sha256)
		return Failure(Code::InvalidInput, "Luxel/region map identities differ");
	if (sample >= tile.samples.size())
		return Failure(Code::InvalidInput, "Invalid luxel region sample");
	const auto &s = tile.samples[sample];
	if (s.coverage != LuxelCoverage::Mapped || !s.positionKnown)
		return Failure(Code::Unavailable, "Luxel has no unambiguous surface position");
	return AssignSurfaceSides(s.positionWorld, s.geometricNormal, Id(s.surface) + 1, ctx, out,
							  maximum);
}
Error LightingRegionGraph::FilterPair(Vec3 source, Vec3 observation, const ReadContext &ctx,
									  RegionPairFilter &out) const
{
	out = RegionPairFilter();
	auto error = Lookup(source, ctx, out.source);
	if (!error)
		return error;
	error = Lookup(observation, ctx, out.observation);
	if (!error)
		return error;
	if (out.source.region && out.observation.region)
		out.relation = out.source.region == out.observation.region
						   ? SampleGraphRelation::SameRegion
						   : out.source.staticComponent &&
									 out.source.staticComponent == out.observation.staticComponent
								 ? SampleGraphRelation::SameStaticComponent
								 : out.source.staticComponent && out.observation.staticComponent
									   ? SampleGraphRelation::DisconnectedSampleGraph
									   : SampleGraphRelation::Unknown;
	RayQuery q;
	q.startWorld = source;
	q.endWorld = observation;
	q.originKnownFree = Free(out.source.bsp.space);
	q.policy.facing = FacingPolicy::TwoSided;
	q.maxWork = data_->options.maxRayWork;
	out.dynamicExtentUnknown = data_->report.dynamicExtentUnknown;
	for (const auto &boundary : data_->input.dynamic)
	{
		double lo = 0, hi = 1;
		for (unsigned k = 0; k < 3; ++k)
		{
			double a = Axis(source, k), d = Axis(observation, k) - a;
			if (d == 0)
			{
				if (a < Axis(boundary.bounds.minWorld, k) || a > Axis(boundary.bounds.maxWorld, k))
					hi = -1;
			}
			else
			{
				double near = (Axis(boundary.bounds.minWorld, k) - a) / d,
					   far = (Axis(boundary.bounds.maxWorld, k) - a) / d;
				if (near > far)
					std::swap(near, far);
				lo = std::max(lo, near);
				hi = std::min(hi, far);
			}
		}
		if (lo <= hi)
			out.dynamicPossible = true;
	}
	return data_->input.geometry->Query(q, ctx, out.direct);
}
Error LightingRegionGraph::DebugObj(size_t maximum, size_t bytes, std::string &out) const
{
	out.clear();
	try
	{
		std::ostringstream text;
		text.imbue(std::locale::classic());
		text << std::setprecision(9)
			 << "# lighting region graph v1; points are certified cell centres; lines are "
				"topology, not LOS\n";
		size_t vertices = 0, written = 0;
		for (const auto &c : data_->cells)
			if (c.region && written++ < maximum)
			{
				text << "o region_" << c.region << "_cell_" << c.id << "\nv "
					 << c.representativeWorld.x << ' ' << c.representativeWorld.y << ' '
					 << c.representativeWorld.z << "\np " << ++vertices << '\n';
				if (uint64_t(text.tellp()) > bytes)
					return Failure(Code::MemoryLimit, "Region debug export budget");
			}
		for (const auto &e : data_->connections)
		{
			const auto *a = Find(e.a);
			const auto *b = Find(e.b);
			text << "o aperture_candidate_" << e.a << '_' << e.b << "\n# sampled-area "
				 << e.sampledAreaWorldSquared << " dynamic " << e.dynamicPossible << "\nv "
				 << a->representativeWorld.x << ' ' << a->representativeWorld.y << ' '
				 << a->representativeWorld.z << "\nv " << e.sampledOpeningCentreWorld.x << ' '
				 << e.sampledOpeningCentreWorld.y << ' ' << e.sampledOpeningCentreWorld.z << "\nv "
				 << b->representativeWorld.x << ' ' << b->representativeWorld.y << ' '
				 << b->representativeWorld.z << "\nl " << vertices + 1 << ' ' << vertices + 2 << ' '
				 << vertices + 3 << '\n';
			vertices += 3;
			if (uint64_t(text.tellp()) > bytes)
				return Failure(Code::MemoryLimit, "Region debug export budget");
		}
		if (uint64_t(text.tellp()) > bytes)
			return Failure(Code::MemoryLimit, "Region debug export budget");
		out = text.str();
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Region debug allocation");
	}
}
} // namespace Core
} // namespace Rend2LightRecon
