// Exact source support functions against the retained BSP brush planes.
// No brush removal, inward distance clamp or renderer collision dependency.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bake_math.h"
#include "regions.h"
#include <algorithm>
namespace Rend2LightRecon
{
namespace Core
{
using namespace Bake::Detail;
namespace
{
bool ValidSupport(const SpatialSupport &s)
{
	if (unsigned(s.kind) > 1 || s.vertices.size() > 256)
		return false;
	if (s.kind == SpatialSupport::Kind::Hull)
	{
		if (s.vertices.empty())
			return false;
		for (auto p : s.vertices)
			if (!Finite(p))
				return false;
	}
	else if (!Finite(s.centre) || !Normal(s.axis) || !std::isfinite(s.halfLengthWorld) ||
			 s.halfLengthWorld <= 0 || !std::isfinite(s.radiusWorld) || s.radiusWorld <= 0)
		return false;
	return !s.contactPlaneKnown || (Normal(s.contactNormal) && std::isfinite(s.contactDistance));
}
void Range(const SpatialSupport &s, Vec3 normal, double distance, double &lo, double &hi)
{
	V n(normal);
	if (s.kind == SpatialSupport::Kind::Cylinder)
	{
		double a = Dot(n, V(s.axis));
		double r = s.halfLengthWorld * std::abs(a) +
				   s.radiusWorld * std::sqrt(std::max(0., Dot(n, n) - a * a));
		double c = Dot(n, V(s.centre)) - distance;
		lo = c - r;
		hi = c + r;
		return;
	}
	lo = std::numeric_limits<double>::infinity();
	hi = -lo;
	for (auto p : s.vertices)
	{
		double v = Dot(n, V(p)) - distance;
		lo = std::min(lo, v);
		hi = std::max(hi, v);
	}
}
Bounds SupportBounds(const SpatialSupport &s)
{
	Bounds b;
	b.known = true;
	float *low[] = {&b.minWorld.x, &b.minWorld.y, &b.minWorld.z};
	float *high[] = {&b.maxWorld.x, &b.maxWorld.y, &b.maxWorld.z};
	for (unsigned k = 0; k < 3; ++k)
	{
		V n;
		n[k] = 1;
		double lo, hi;
		Range(s, n.Float(), 0, lo, hi);
		*low[k] = float(lo);
		*high[k] = float(hi);
		if (*low[k] > lo)
			*low[k] = std::nextafter(*low[k], -std::numeric_limits<float>::infinity());
		if (*high[k] < hi)
			*high[k] = std::nextafter(*high[k], std::numeric_limits<float>::infinity());
	}
	return b;
}
bool Contact(const SpatialSupport &s, const SpatialPlane &p, double lo)
{
	// A negative signed distance is NEVER repaired by contact tolerance.
	if (!s.contactPlaneKnown || lo < 0)
		return false;
	double round = 64 * std::numeric_limits<double>::epsilon() *
				   (1 + std::abs(p.distance) + std::abs(s.contactDistance));
	return lo <= round && Length(V(s.contactNormal) - V(p.normal)) <= 1e-7 &&
		   std::abs(s.contactDistance - p.distance) <= round;
}
} // namespace
Error RegionDomain::ClassifySupport(const SpatialSupport &s, SpaceClass &out, bool &contact,
									uint64_t &work) const
{
	out = SpaceClass::Unknown;
	contact = false;
	work = 0;
	if (!ValidSupport(s))
		return Failure(Code::InvalidInput, "Invalid occupied source support");
	auto error = ClassifyBox(SupportBounds(s), out, work);
	// A custom box-only classifier may be unable to classify a zero-volume hull.
	if (!error && error.code == Code::InvalidInput)
	{
		out = SpaceClass::Unknown;
		return {};
	}
	return error;
}
Error BspRegionDomain::ClassifySupport(const SpatialSupport &s, SpaceClass &out, bool &contact,
									   uint64_t &work) const
{
	out = SpaceClass::Unknown;
	contact = false;
	work = 0;
	if (!ValidSupport(s))
		return Failure(Code::InvalidInput, "Invalid BSP source support");
	auto bounds = SupportBounds(s);
	if (bounds.minWorld.x < input_.worldBounds.minWorld.x ||
		bounds.minWorld.y < input_.worldBounds.minWorld.y ||
		bounds.minWorld.z < input_.worldBounds.minWorld.z ||
		bounds.maxWorld.x > input_.worldBounds.maxWorld.x ||
		bounds.maxWorld.y > input_.worldBounds.maxWorld.y ||
		bounds.maxWorld.z > input_.worldBounds.maxWorld.z)
	{
		out = SpaceClass::Outside;
		return {};
	}
	int32_t stack[64];
	size_t size = 1;
	stack[0] = input_.nodes.empty() ? -1 : 0;
	bool unknown = false;
	while (size)
	{
		if (++work > limit_)
			return Failure(Code::WorkLimit, "BSP occupied-support traversal budget");
		int32_t i = stack[--size];
		if (i >= 0)
		{
			const auto &n = input_.nodes[size_t(i)];
			double lo, hi;
			Range(s, input_.planes[n.plane].normal, input_.planes[n.plane].distance, lo, hi);
			if (size + 2 > 64)
				return Failure(Code::WorkLimit, "BSP occupied-support stack");
			if (hi >= 0)
				stack[size++] = n.children[0];
			if (lo < 0)
				stack[size++] = n.children[1];
			continue;
		}
		const auto &leaf = input_.leaves[size_t(-int64_t(i) - 1)];
		bool declaredBrushContact = false; // boundary support may be assigned to the solid BSP leaf
		for (uint32_t j = 0; j < leaf.brushCount; ++j)
		{
			const auto &b = input_.brushes[input_.leafBrushes[leaf.firstBrush + j]];
			if (!b.world || !b.solid)
				continue;
			bool disjoint = false, inside = true;
			std::vector<Vec3> polygon = s.vertices;
			for (uint32_t k = 0; k < b.sideCount; ++k)
			{
				if (++work > limit_)
					return Failure(Code::WorkLimit, "BSP occupied-support brush budget");
				const auto &p = input_.planes[input_.sides[b.firstSide + k]];
				double lo, hi;
				Range(s, p.normal, p.distance, lo, hi);
				double round = 64 * std::numeric_limits<double>::epsilon() *
							   (1 + std::abs(lo) + std::abs(hi) + std::abs(p.distance));
				if (lo > round)
				{
					disjoint = true;
					break;
				}
				if (Contact(s, p, lo))
				{
					contact = true;
					declaredBrushContact = true;
					disjoint = true;
					break;
				}
				inside &= hi < -round;
				if (s.kind == SpatialSupport::Kind::Hull && polygon.size() >= 3)
				{
					std::vector<Vec3> clipped;
					for (size_t v = 0; v < polygon.size(); ++v)
					{
						V a(polygon[v]), c(polygon[(v + 1) % polygon.size()]);
						double da = Dot(V(p.normal), a) - p.distance,
							   dc = Dot(V(p.normal), c) - p.distance;
						if (da <= 0)
							clipped.push_back(a.Float());
						if ((da < 0) != (dc < 0))
							clipped.push_back((a + (c - a) * (da / (da - dc))).Float());
					}
					polygon.swap(clipped);
					if (polygon.size() > 512)
						return Failure(Code::WorkLimit, "BSP support clipping budget");
				}
			}
			if (disjoint)
				continue;
			if (inside)
			{
				out = b.sky ? SpaceClass::Sky : SpaceClass::Solid;
				return {};
			}
			// A clipped polygon centroid strictly inside every brush plane proves
			// actual penetration. Inconclusive cylinder/corner cases stay Unknown.
			if (s.kind == SpatialSupport::Kind::Hull && polygon.size() >= 3)
			{
				V centre;
				for (auto p : polygon)
					centre = centre + V(p);
				centre = centre / double(polygon.size());
				bool deep = true;
				for (uint32_t k = 0; k < b.sideCount; ++k)
				{
					if (++work > limit_)
						return Failure(Code::WorkLimit, "BSP support penetration budget");
					const auto &p = input_.planes[input_.sides[b.firstSide + k]];
					deep &= Dot(V(p.normal), centre) <
							p.distance - 64 * std::numeric_limits<double>::epsilon() *
											 (1 + std::abs(p.distance));
				}
				if (deep)
				{
					out = b.sky ? SpaceClass::Sky : SpaceClass::Solid;
					return {};
				}
			}
			unknown = true;
		}
		// A named contact support on a retained brush boundary is not occupied
		// volume merely because BSP equality chooses its unclustered leaf.
		// No-contact unclustered support and every unresolved brush remain Unknown.
		if (leaf.cluster < 0 && !declaredBrushContact)
			unknown = true;
	}
	out = unknown ? SpaceClass::Unknown : SpaceClass::Free;
	return {};
}
} // namespace Core
} // namespace Rend2LightRecon
