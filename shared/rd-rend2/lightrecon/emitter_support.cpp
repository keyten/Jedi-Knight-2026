// X14 source support uses existing world brush/domain and visual BVH predicates.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "emitter_support.h"
#include "bake_math.h"
#include "emission_geometry.h"
#include "geometry_key.h"
#include <algorithm>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace SourceSupport
{
using namespace Bake::Detail;
namespace
{
void Need(bool b, Code c, const char *s)
{
	if (!b)
		throw Failure(c, s);
}
void Must(Error e)
{
	if (!e)
		throw e;
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (auto c : s)
		k.U(uint8_t(c));
}
bool Free(SpaceClass s)
{
	return s == SpaceClass::Free || s == SpaceClass::Exterior;
}
bool Mounted(const Contract &c)
{
	return c.mountKnown || c.kind == Kind::MountedPoint || c.kind == Kind::MountedSpot ||
		   c.kind == Kind::OriginalSurface || c.kind == Kind::RecessedRectangle;
}
double Round(V p)
{
	return 128 * std::numeric_limits<double>::epsilon() *
		   (1 + std::abs(p.x) + std::abs(p.y) + std::abs(p.z));
}
Bounds BoundsOf(const SpatialSupport &s)
{
	V lo, hi;
	for (unsigned i = 0; i < 3; ++i)
	{
		if (s.kind == SpatialSupport::Kind::Cylinder)
		{
			double r = s.halfLengthWorld * std::abs(V(s.axis)[i]) +
					   s.radiusWorld * std::sqrt(std::max(0., 1 - V(s.axis)[i] * V(s.axis)[i]));
			lo[i] = V(s.centre)[i] - r;
			hi[i] = V(s.centre)[i] + r;
		}
		else
		{
			lo[i] = std::numeric_limits<double>::infinity();
			hi[i] = -lo[i];
			for (auto p : s.vertices)
			{
				lo[i] = std::min(lo[i], V(p)[i]);
				hi[i] = std::max(hi[i], V(p)[i]);
			}
		}
	}
	Bounds b;
	b.known = true;
	b.minWorld = lo.Float();
	b.maxWorld = hi.Float();
	float *low[] = {&b.minWorld.x, &b.minWorld.y, &b.minWorld.z};
	float *high[] = {&b.maxWorld.x, &b.maxWorld.y, &b.maxWorld.z};
	for (unsigned i = 0; i < 3; ++i)
	{
		if (*low[i] > lo[i])
			*low[i] = std::nextafter(*low[i], -std::numeric_limits<float>::infinity());
		if (*high[i] < hi[i])
			*high[i] = std::nextafter(*high[i], std::numeric_limits<float>::infinity());
	}
	return b;
}
bool Intersects(const std::vector<Vec3> &p, const Vec3 triangle[3])
{
	// Separating axes for convex planar polygons, including coplanar edge axes.
	bool separated = false;
	std::vector<V> edges;
	for (size_t i = 0; i < p.size(); ++i)
		edges.push_back(V(p[(i + 1) % p.size()]) - V(p[i]));
	V te[3] = {V(triangle[1]) - V(triangle[0]), V(triangle[2]) - V(triangle[1]),
			   V(triangle[0]) - V(triangle[2])};
	auto axis = [&](V n) {
		if (Length(n) < 1e-18)
			return;
		double a = std::numeric_limits<double>::infinity(), b = -a, c = a, d = -a;
		for (auto v : p)
		{
			double x = Dot(n, V(v));
			a = std::min(a, x);
			b = std::max(b, x);
		}
		for (unsigned i = 0; i < 3; ++i)
		{
			double x = Dot(n, V(triangle[i]));
			c = std::min(c, x);
			d = std::max(d, x);
		}
		double round = 64 * std::numeric_limits<double>::epsilon() *
					   (1 + std::max({std::abs(a), std::abs(b), std::abs(c), std::abs(d)}));
		if (a > d + round || c > b + round)
			separated = true;
	};
	V n = p.size() > 2 ? Cross(edges[0], edges[1]) : V(), tn = Cross(te[0], te[1]);
	axis(n);
	axis(tn);
	for (auto e : edges)
	{
		axis(Cross(n, e));
		for (auto t : te)
			axis(Cross(e, t));
	}
	for (auto t : te)
		axis(Cross(tn, t));
	if (p.size() == 1)
	{
		for (unsigned k = 0; k < 3; ++k)
		{
			V a;
			a[k] = 1;
			axis(a);
		}
	}
	return !separated;
}
std::vector<SpatialSupport> Supports(const PhysicalEmitter &e, const Contract &c)
{
	std::vector<SpatialSupport> out;
	SpatialSupport s;
	if (c.kind == Kind::OriginalSurface)
	{
		for (size_t i = 0; i < c.original.count; ++i)
		{
			s.vertices.assign(c.original.data[i].world, c.original.data[i].world + 3);
			out.push_back(s);
		}
		return out;
	}
	if (e.kind == EmitterKind::Point || e.kind == EmitterKind::Spot)
		s.vertices = {e.position.world};
	else if (e.kind == EmitterKind::Rectangle)
	{
		V p(e.position.world), r = V(e.frame.right) * e.geometry.halfWidthWorld,
							   u = V(e.frame.up) * e.geometry.halfHeightWorld;
		s.vertices = {(p - r - u).Float(), (p + r - u).Float(), (p + r + u).Float(),
					  (p - r + u).Float()};
	}
	else
	{
		s.kind = SpatialSupport::Kind::Cylinder;
		s.centre = e.position.world;
		s.axis = e.frame.right;
		s.radiusWorld = e.geometry.radiusWorld;
		s.halfLengthWorld = e.geometry.halfLengthWorld;
	}
	out.push_back(s);
	return out;
}
} // namespace
Disk::Digest Key(const Contract &c)
{
	GeometryKey k;
	k.U(kVersion);
	k.U(2); // X14 numerical interpretation: declared brush-boundary leaf contact
	k.U(c.version);
	k.U(unsigned(c.kind));
	k.U(c.fixture);
	k.Digest(c.map);
	k.Digest(c.geometry);
	k.V(c.anchorWorld);
	k.V(c.outward);
	k.U(c.mountKnown);
	k.U(c.twoSidedAttested);
	k.F(c.maximumStandOffWorld);
	k.F(c.frontClearanceWorld);
	Text(k, c.provenance);
	auto ids = c.surfaces;
	std::sort(ids.begin(), ids.end());
	k.U(ids.size());
	for (auto id : ids)
		k.U(id);
	k.U(c.original.count);
	k.U(c.triangleSurfaces.size());
	for (auto id : c.triangleSurfaces)
		k.U(id);
	if (c.original.owner && c.original.data && c.original.count <= 64)
		for (size_t i = 0; i < c.original.count; ++i)
			for (unsigned j = 0; j < 3; ++j)
			{
				k.V(c.original.data[i].world[j]);
				k.V(c.original.data[i].normal[j]);
			}
	return k.Finish();
}
Error Validate(const Contract &c)
{
	if (c.version != kVersion || unsigned(c.kind) > unsigned(Kind::UnsupportedAssembly) ||
		c.surfaces.size() > 64 || c.original.count > 64 || c.provenance.size() > 4096 ||
		!Finite(c.anchorWorld) || !Finite(c.outward) || !std::isfinite(c.frontClearanceWorld) ||
		c.frontClearanceWorld < 0 || !std::isfinite(c.maximumStandOffWorld) ||
		c.maximumStandOffWorld < 0)
		return Failure(Code::InvalidInput, "Invalid source support contract");
	if (c.kind == Kind::LegacyVacuum)
		return {};
	if (c.provenance.empty() || c.map == Disk::Digest{} || c.geometry == Disk::Digest{})
		return Failure(Code::InvalidInput, "Source support lacks map/geometry/provenance");
	std::set<Id> ids;
	for (auto id : c.surfaces)
		if (!id || !ids.insert(id).second)
			return Failure(Code::InvalidInput, "Invalid source surface identities");
	if (Mounted(c) && (!c.fixture || c.surfaces.empty() || !Normal(c.outward)))
		return Failure(Code::InvalidInput,
					   "Mount requires original fixture/surface identity and outward normal");
	if (c.kind == Kind::OriginalSurface &&
		(!c.original.count || !c.original.owner || !c.original.data))
		return Failure(Code::InvalidInput, "Original source triangle lease missing");
	if (c.original.count &&
		(c.triangleSurfaces.size() != c.original.count || !c.original.owner || !c.original.data))
		return Failure(Code::InvalidInput, "Original source triangle/surface mapping missing");
	for (auto id : c.triangleSurfaces)
		if (!ids.count(id))
			return Failure(Code::InvalidInput,
						   "Triangle belongs to an undeclared emitting surface");
	for (size_t i = 0; i < c.original.count; ++i)
		for (unsigned j = 0; j < 3; ++j)
			if (!Finite(c.original.data[i].world[j]) || !Normal(c.original.data[i].normal[j]))
				return Failure(Code::InvalidInput, "Original emission geometry invalid");
	return {};
}
Error Check(const PointRefine::SceneInput &scene, const PointRefine::PlacementProvider &placement,
			const PhysicalEmitter &before, const PhysicalEmitter &e, const Contract &c,
			const ReadContext &read, Result &out)
{
	out = {};
	out.key = Key(c);
	out.placement.state = PointRefine::Constraint::UnknownGeometry;
	try
	{
		Must(Validate(c));
		Need(!Cancelled(read), Code::Cancelled, "Source support cancelled");
		Need(scene.domain && scene.geometry && scene.map.sha256 == c.map &&
				 scene.geometry->CertificationKey() == placement.GeometryKey() &&
				 c.map == placement.MapKey() && c.geometry == placement.GeometryKey(),
			 Code::InvalidState, "Stale source support generation");
		auto reject = [&](PointRefine::Constraint state, const char *reason) {
			out.reason = reason;
			out.placement.state = state;
			out.placement.work = out.work;
			out.placement.rays = out.rays;
			out.placement.assumptions = reason;
		};
		if (c.kind == Kind::LegacyVacuum || c.kind == Kind::UnsupportedAssembly)
		{
			reject(PointRefine::Constraint::UnknownGeometry,
				   "Unsupported source assembly or unspecified support contract");
			return {};
		}
		Need(e.position.known && Finite(e.position.world), Code::InvalidInput,
			 "Emitter support position invalid");
		bool point = e.kind == EmitterKind::Point, spot = e.kind == EmitterKind::Spot,
			 rectangle = e.kind == EmitterKind::Rectangle, tube = e.kind == EmitterKind::Tube;
		if ((c.kind == Kind::FreePoint || c.kind == Kind::MountedPoint)
				? !point
				: (c.kind == Kind::FreeSpot || c.kind == Kind::MountedSpot)
					  ? !spot
					  : c.kind == Kind::Cylinder ? !tube : !rectangle)
		{
			reject(PointRefine::Constraint::Mount,
				   "Source kind does not match occupied/emitting support");
			return {};
		}
		if (!point)
			Need(e.frame.known && Normal(e.frame.forward) && Normal(e.frame.right) &&
					 Normal(e.frame.up) &&
					 Length(Cross(V(e.frame.right), V(e.frame.up)) - V(e.frame.forward)) < 1e-4,
				 Code::InvalidInput, "Emitter support frame invalid");
		if (rectangle)
			Need(e.geometry.known && std::isfinite(e.geometry.halfWidthWorld) &&
					 std::isfinite(e.geometry.halfHeightWorld) && e.geometry.halfWidthWorld > 0 &&
					 e.geometry.halfHeightWorld > 0,
				 Code::InvalidInput, "Rectangle support dimensions");
		if (tube)
			Need(e.geometry.known && std::isfinite(e.geometry.radiusWorld) &&
					 std::isfinite(e.geometry.halfLengthWorld) && e.geometry.radiusWorld > 0 &&
					 e.geometry.halfLengthWorld > 0 && !e.angular.twoSided,
				 Code::InvalidInput, "Open cylinder occupied dimensions");
		if (e.angular.twoSided && !c.twoSidedAttested)
		{
			reject(PointRefine::Constraint::Mount,
				   "Two-sided emission requires explicit side evidence");
			return {};
		}
		if (c.kind == Kind::TwoSidedRectangle && !e.angular.twoSided)
		{
			reject(PointRefine::Constraint::Mount, "Two-sided contract and emitter disagree");
			return {};
		}
		if (Length(V(before.position.world) - V(e.position.world)) > 0 ||
			(!point && (Length(V(before.frame.forward) - V(e.frame.forward)) > 0 ||
						Length(V(before.frame.right) - V(e.frame.right)) > 0)))
		{
			reject(PointRefine::Constraint::UnknownPath,
				   "Non-vacuum source motion requires a new swept-support certificate");
			return {};
		}
		auto supports = Supports(e, c);
		std::vector<std::vector<Vec3>> emission;
		double scale = 1;
		for (auto &s : supports)
		{
			if (s.kind == SpatialSupport::Kind::Hull)
			{
				emission.push_back(s.vertices);
				for (auto p : s.vertices)
					scale = std::max(scale, Length(V(p)));
				if (s.vertices.size() >= 3)
					for (size_t i = 1; i + 1 < s.vertices.size(); ++i)
						out.emittingAreaWorldSquared +=
							Length(Cross(V(s.vertices[i]) - V(s.vertices[0]),
										 V(s.vertices[i + 1]) - V(s.vertices[0]))) *
							.5;
			}
			else
			{
				out.emittingAreaWorldSquared = 4 * Pi * s.radiusWorld * s.halfLengthWorld;
				scale = std::max(scale, Length(V(s.centre)) + s.halfLengthWorld + s.radiusWorld);
			}
		}
		double error = 128 * std::numeric_limits<double>::epsilon() * (1 + scale);
		out.contactErrorWorld = error;
		V normal(c.outward);
		double plane = Dot(normal, V(c.anchorWorld));
		if (Mounted(c) && rectangle && Dot(normal, V(e.frame.forward)) < 1 - 1e-6)
		{
			reject(PointRefine::Constraint::Mount,
				   "Emitting face points into or away from the declared mounting side");
			return {};
		}
		if (Mounted(c) && spot && Dot(normal, V(e.frame.forward)) <= 0)
		{
			reject(PointRefine::Constraint::Mount,
				   "Mounted spotlight axis points into its support");
			return {};
		}
		if (Mounted(c) && !tube)
			for (const auto &polygon : emission)
				for (auto p : polygon)
				{
					double signedDistance = Dot(normal, V(p)) - plane;
					if (signedDistance < 0 || signedDistance > c.maximumStandOffWorld + error)
					{
						reject(PointRefine::Constraint::Mount,
							   "Emitting support is behind its mounting side or outside declared "
							   "fixture stand-off");
						return {};
					}
				}

		// Collect only bounded nearby immutable visual geometry, including unknown optics.
		Bounds bounds = BoundsOf(supports[0]);
		for (auto &s : supports)
		{
			auto b = BoundsOf(s);
			bounds.minWorld = Vec3(std::min(bounds.minWorld.x, b.minWorld.x),
								   std::min(bounds.minWorld.y, b.minWorld.y),
								   std::min(bounds.minWorld.z, b.minWorld.z));
			bounds.maxWorld = Vec3(std::max(bounds.maxWorld.x, b.maxWorld.x),
								   std::max(bounds.maxWorld.y, b.maxWorld.y),
								   std::max(bounds.maxWorld.z, b.maxWorld.z));
		}
		if (Mounted(c))
		{
			bounds.minWorld = Vec3(std::min(bounds.minWorld.x, c.anchorWorld.x),
								   std::min(bounds.minWorld.y, c.anchorWorld.y),
								   std::min(bounds.minWorld.z, c.anchorWorld.z));
			bounds.maxWorld = Vec3(std::max(bounds.maxWorld.x, c.anchorWorld.x),
								   std::max(bounds.maxWorld.y, c.anchorWorld.y),
								   std::max(bounds.maxWorld.z, c.anchorWorld.z));
		}
		LocalGeometry local;
		Must(scene.geometry->ExtractBox(bounds, read, 4096, scene.maxWorkPerCheck, local));
		out.work += local.work;
		if (!local.coverageComplete)
		{
			reject(PointRefine::Constraint::UnknownGeometry, "Incomplete local visual support");
			return {};
		}
		std::vector<const VisibilityTriangle *> mount;
		for (const auto &t : local.triangles)
			if (std::find(c.surfaces.begin(), c.surfaces.end(), t.surface) != c.surfaces.end())
			{
				if (t.optical != ObstacleKind::Opaque || t.errorWorld > 0)
				{
					reject(PointRefine::Constraint::UnknownGeometry,
						   "Mount optics/curved geometry are not exact opaque support");
					return {};
				}
				V a(t.geometry.verticesWorld[0]), b(t.geometry.verticesWorld[1]),
					d(t.geometry.verticesWorld[2]);
				V n = Unit(Cross(b - a, d - a));
				if (Mounted(c) && std::abs(Dot(n, normal)) < 1 - 1e-6)
				{
					reject(PointRefine::Constraint::Mount,
						   "Mount contains incompatible surface orientations");
					return {};
				}
				bool onPlane = true;
				for (auto v : t.geometry.verticesWorld)
					onPlane &= std::abs(Dot(normal, V(v)) - plane) <= error;
				if (Mounted(c) && onPlane)
					mount.push_back(&t);
			}
		if (Mounted(c) && mount.empty())
		{
			reject(PointRefine::Constraint::UnknownGeometry,
				   "Named mounting surface absent at declared contact plane");
			return {};
		}
		if (c.kind == Kind::OriginalSurface)
		{
			if (c.original.count != mount.size())
			{
				reject(PointRefine::Constraint::UnknownGeometry,
					   "Original emitting union does not match the pinned source triangles");
				return {};
			}
			std::set<size_t> used;
			for (size_t i = 0; i < c.original.count; ++i)
			{
				bool matched = false;
				for (size_t j = 0; j < mount.size(); ++j)
					if (!used.count(j) && mount[j]->surface == c.triangleSurfaces[i])
					{
						unsigned same = 0;
						for (auto a : c.original.data[i].world)
							for (auto b : mount[j]->geometry.verticesWorld)
								if (Length(V(a) - V(b)) <= error)
									++same;
						if (same == 3)
						{
							used.insert(j);
							matched = true;
							break;
						}
					}
				if (!matched)
				{
					reject(PointRefine::Constraint::UnknownGeometry,
						   "Original emitting triangle is not present in pinned visual geometry");
					return {};
				}
			}
		}

		// Check the projected emission footprint against actual named triangles.
		if (Mounted(c))
		{
			for (size_t i = 0; i < mount.size(); ++i)
				for (size_t j = i + 1; j < mount.size(); ++j)
				{
					Need(++out.work <= scene.maxWorkPerCheck, Code::WorkLimit,
						 "Mount overlap budget");
					std::vector<Vec3> p(mount[i]->geometry.verticesWorld,
										mount[i]->geometry.verticesWorld + 3);
					if (Emission::IntersectionArea(p, mount[j]->geometry.verticesWorld, c.outward) >
						error * (1 + Length(V(mount[i]->geometry.verticesWorld[1]) -
											V(mount[i]->geometry.verticesWorld[0]))))
					{
						reject(PointRefine::Constraint::UnknownGeometry,
							   "Overlapping mounting triangles cannot certify unique support area");
						return {};
					}
				}
			std::vector<std::vector<Vec3>> projected = emission;
			if (tube)
			{
				V centre(e.position.world);
				double low =
					Dot(normal, centre) - plane -
					e.geometry.radiusWorld *
						std::sqrt(std::max(0., 1 - std::pow(Dot(normal, V(e.frame.right)), 2))) -
					e.geometry.halfLengthWorld * std::abs(Dot(normal, V(e.frame.right)));
				if (low < 0)
				{
					reject(PointRefine::Constraint::Solid,
						   "Cylinder occupied body penetrates the mounting plane");
					return {};
				}
				// Tangent mounting line must lie on the named support. Whole cylinder is still
				// checked against world solids.
				V tangent = centre - normal * (Dot(normal, centre) - plane),
				  axis = V(e.frame.right) * e.geometry.halfLengthWorld;
				projected = {{(tangent - axis).Float(), tangent.Float(), (tangent + axis).Float()}};
			}
			for (auto polygon : projected)
			{
				for (auto &v : polygon)
					v = (V(v) - normal * (Dot(normal, V(v)) - plane)).Float();
				double area = 0;
				if (polygon.size() >= 3)
					for (size_t i = 1; i + 1 < polygon.size(); ++i)
						area += Length(Cross(V(polygon[i]) - V(polygon[0]),
											 V(polygon[i + 1]) - V(polygon[0]))) *
								.5;
				if (area > 0)
				{
					double covered = 0;
					for (auto t : mount)
						covered += Emission::IntersectionArea(polygon, t->geometry.verticesWorld,
															  c.outward);
					if (std::abs(covered - area) > error * (1 + std::sqrt(area)))
					{
						reject(PointRefine::Constraint::Mount,
							   "Emitting support extends beyond its named fixture surface");
						return {};
					}
				}
				else
					for (auto p : polygon)
					{
						bool found = false;
						for (auto t : mount)
						{
							V a(t->geometry.verticesWorld[0]), b(t->geometry.verticesWorld[1]),
								d(t->geometry.verticesWorld[2]), v(p);
							V u = b - a, w = d - a;
							double uu = Dot(u, u), ww = Dot(w, w), uw = Dot(u, w),
								   vu = Dot(v - a, u), vw = Dot(v - a, w), det = uu * ww - uw * uw;
							if (det > 0)
							{
								double x = (vu * ww - vw * uw) / det, y = (vw * uu - vu * uw) / det;
								found |= x >= 0 && y >= 0 && x + y <= 1;
							}
						}
						if (!found)
						{
							reject(PointRefine::Constraint::Mount,
								   "Point/tube contact lies outside its named fixture");
							return {};
						}
					}
			}
		}
		auto inspect = [&](SpatialSupport s) -> bool {
			Need(!Cancelled(read), Code::Cancelled, "Source support cancelled");
			Need(out.work < scene.maxWorkPerCheck, Code::WorkLimit,
				 "Source support cumulative work");
			if (Mounted(c))
			{
				s.contactPlaneKnown = true;
				s.contactNormal = c.outward;
				s.contactDistance = plane;
			}
			SpaceClass space;
			bool contact = false;
			uint64_t work = 0;
			Must(scene.domain->ClassifySupport(s, space, contact, work));
			out.work += work;
			out.contact |= contact;
			if (!Free(space))
			{
				reject(space == SpaceClass::Solid ? PointRefine::Constraint::Solid
												  : PointRefine::Constraint::UnknownGeometry,
					   "Occupied source support is not certified outside world solids");
				return false;
			}
			return true;
		};
		for (auto s : supports)
			if (!inspect(s))
				return {};
		if (e.angular.twoSided && Mounted(c))
			for (auto s : supports)
			{
				s.contactPlaneKnown = true;
				s.contactNormal = (normal * -1).Float();
				s.contactDistance = -plane;
				SpaceClass back;
				bool contact = false;
				uint64_t cost = 0;
				Must(scene.domain->ClassifySupport(s, back, contact, cost));
				out.work += cost;
				if (!Free(back))
				{
					reject(PointRefine::Constraint::UnknownGeometry,
						   "Two-sided emitting support has no certified back side outside world "
						   "solids");
					return {};
				}
			}

		for (const auto &t : local.triangles)
		{
			Need(++out.work <= scene.maxWorkPerCheck, Code::WorkLimit,
				 "Source visual support budget");
			bool named =
				std::find(c.surfaces.begin(), c.surfaces.end(), t.surface) != c.surfaces.end();
			bool contact = Mounted(c) && named;
			for (auto v : t.geometry.verticesWorld)
				contact &= std::abs(Dot(normal, V(v)) - plane) <= error;
			if (contact)
			{
				out.contact = true;
				continue;
			}
			bool hit = false;
			for (auto s : supports)
			{
				if (s.kind == SpatialSupport::Kind::Hull)
					hit |= Intersects(s.vertices, t.geometry.verticesWorld);
				else
				{
					auto b = BoundsOf(s);
					bool separated = false;
					for (unsigned k = 0; k < 3; ++k)
					{
						double lo = std::numeric_limits<double>::infinity(), hi = -lo;
						for (auto p : t.geometry.verticesWorld)
						{
							lo = std::min(lo, V(p)[k]);
							hi = std::max(hi, V(p)[k]);
						}
						separated |= lo > V(b.maxWorld)[k] || hi < V(b.minWorld)[k];
					}
					hit |= !separated;
				}
			}
			if (hit)
			{
				reject(t.optical == ObstacleKind::Opaque && !tube
						   ? PointRefine::Constraint::Clearance
						   : PointRefine::Constraint::UnknownGeometry,
					   "Unrelated visual geometry intersects source support (cylinder AABB is "
					   "conservative)");
				return {};
			}
		}
		if (Mounted(c) && c.frontClearanceWorld > 0)
		{
			for (auto polygon : emission)
			{
				SpatialSupport prism;
				prism.vertices = polygon;
				for (auto p : polygon)
					prism.vertices.push_back((V(p) + normal * c.frontClearanceWorld).Float());
				if (!inspect(prism))
					return {};
				LocalGeometry front;
				Must(scene.geometry->ExtractBox(
					BoundsOf(prism), read, 4096,
					std::max<uint64_t>(1, scene.maxWorkPerCheck - out.work), front));
				out.work += front.work;
				if (!front.coverageComplete)
				{
					reject(PointRefine::Constraint::UnknownGeometry,
						   "Source front clearance lacks complete visual geometry");
					return {};
				}
				for (const auto &t : front.triangles)
				{
					bool contact = std::find(c.surfaces.begin(), c.surfaces.end(), t.surface) !=
								   c.surfaces.end();
					for (auto v : t.geometry.verticesWorld)
						contact &= std::abs(Dot(normal, V(v)) - plane) <= error;
					if (!contact)
					{
						reject(PointRefine::Constraint::UnknownGeometry,
							   "Source front clearance has unresolved independent visual geometry");
						return {};
					}
				}
			}
		}
		Must(placement.CheckOccupancy(bounds, read, out.placement));
		out.work += out.placement.work;
		out.rays += out.placement.rays;
		if (!PointRefine::Admissible(out.placement))
		{
			out.reason = "Independent bake occupancy does not admit source support";
			return {};
		}
		Need(out.work <= scene.maxWorkPerCheck, Code::WorkLimit,
			 "Cumulative occupied-support budget");
		out.sourceSideCertified = true;
		out.conditional = c.kind == Kind::RecessedRectangle ||
						  out.placement.state == PointRefine::Constraint::Conditional;
		if (out.conditional)
			out.placement.state = PointRefine::Constraint::Conditional;
		out.reason = Mounted(c)
						 ? "Named emitting-side contact; world solid penetration remains forbidden"
						 : "Source-kind occupied support certified";
		out.placement.assumptions += "; X14: " + c.provenance + "; support=" + Disk::Hex(out.key);
		out.placement.work = out.work;
		out.placement.rays = out.rays;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Source support allocation");
	}
}
Error FromSurfaces(std::shared_ptr<const SurfaceGeometry> g, const StaticVisibility &v,
				   const std::vector<uint32_t> &ordinals, Id fixture, const ReadContext &read,
				   Contract &out)
{
	out = {};
	try
	{
		Need(g && v.BuiltFrom(g) && g->Map().sha256 == v.GeometryIdentity().sha256 && fixture &&
				 !ordinals.empty() && ordinals.size() <= 64,
			 Code::InvalidInput, "Original source geometry identity");
		Contract c;
		c.kind = Kind::OriginalSurface;
		c.fixture = fixture;
		c.map = g->Map().sha256;
		c.geometry = v.CertificationKey();
		c.mountKnown = true;
		c.twoSidedAttested = true;
		c.provenance = "Exact named original BSP planar triangle union; amplitude/profile and bake "
					   "participation remain unknown";
		auto triangles = std::make_shared<std::vector<Bake::FootprintTriangle>>();
		V normal;
		bool first = true;
		std::set<uint32_t> ids;
		auto ordered = ordinals;
		std::sort(ordered.begin(), ordered.end());
		for (auto id : ordered)
		{
			Need(ids.insert(id).second && id < g->Surfaces().size(), Code::InvalidInput,
				 "Original source surface reference");
			const auto &s = g->Surfaces()[id];
			Need(s.ordinal == id && s.motion == SurfaceMotion::StaticWorld &&
					 (s.kind == BakedSurfaceKind::Face || s.kind == BakedSurfaceKind::Triangles),
				 Code::Unavailable, "Source surface is curved/dynamic/unsupported");
			Need(s.indices.size() % 3 == 0, Code::InvalidInput,
				 "Incomplete source triangle index record");
			c.surfaces.push_back(Id(id) + 1);
			c.twoSidedAttested &=
				s.materialFacts.available && s.materialFacts.cull == SurfaceCull::None;
			for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
			{
				Need(!Cancelled(read), Code::Cancelled, "Original source support cancelled");
				Need(triangles->size() < 64 &&
						 (triangles->size() + 1) * sizeof(Bake::FootprintTriangle) <= read.maxBytes,
					 Code::MemoryLimit, "Original source triangle limit");
				Bake::FootprintTriangle t;
				for (unsigned k = 0; k < 3; ++k)
				{
					Need(s.indices[i + k] < s.vertices.size(), Code::InvalidInput,
						 "Original source triangle index");
					t.world[k] = s.vertices[s.indices[i + k]].position;
				}
				V n = Unit(Cross(V(t.world[1]) - V(t.world[0]), V(t.world[2]) - V(t.world[0])));
				Need(Normal(n), Code::Unavailable, "Degenerate source triangle");
				if (first)
				{
					normal = n;
					c.anchorWorld = t.world[0];
					first = false;
				}
				Need(Dot(n, normal) > 1 - 1e-6, Code::Unavailable,
					 "Non-planar or contradictory source assembly");
				for (unsigned k = 0; k < 3; ++k)
				{
					Need(std::abs(Dot(normal, V(t.world[k]) - V(c.anchorWorld))) <=
							 Round(V(t.world[k])),
						 Code::Unavailable, "Non-planar original source support");
					t.normal[k] = normal.Float();
				}
				triangles->push_back(t);
				c.triangleSurfaces.push_back(Id(id) + 1);
			}
		}
		Need(!triangles->empty(), Code::Unavailable,
			 "Original source has no triangles"); // A disconnected collection is an unsupported
												  // assembly, never one bounding panel.
		std::vector<bool> connected(triangles->size(), false);
		connected[0] = true;
		for (size_t pass = 0; pass < triangles->size(); ++pass)
			for (size_t i = 0; i < triangles->size(); ++i)
				if (connected[i])
					for (size_t j = 0; j < triangles->size(); ++j)
						if (!connected[j])
						{
							unsigned shared = 0;
							for (auto a : (*triangles)[i].world)
								for (auto b : (*triangles)[j].world)
									if (Length(V(a) - V(b)) == 0)
										++shared;
							if (shared >= 2)
								connected[j] = true;
						}
		Need(std::all_of(connected.begin(), connected.end(), [](bool x) { return x; }),
			 Code::Unavailable,
			 "Disconnected emitting surfaces require separate source identities");
		c.outward = normal.Float();
		c.original = VectorChunk<Bake::FootprintTriangle>(triangles, 0, triangles->size());
		Must(Validate(c));
		out = std::move(c);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Original source support allocation");
	}
}
} // namespace SourceSupport
} // namespace Core
} // namespace Rend2LightRecon
