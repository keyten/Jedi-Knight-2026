// Tool-only geometry export and independent plane/barycentric ray witness.
#pragma once
#include <array>
namespace RegionAudit
{
inline Disk::Digest DomainKey(const BspRegionDomain &domain,
							  const std::vector<DynamicBoundary> &dynamic)
{
	GeometryKey k;
	const auto &s = domain.Source();
	k.B(s.worldBounds);
	k.U(s.planes.size());
	for (auto &p : s.planes)
	{
		k.V(p.normal);
		k.F(p.distance);
	}
	k.U(s.nodes.size());
	for (auto &n : s.nodes)
	{
		k.U(n.plane);
		k.U(n.children[0]);
		k.U(n.children[1]);
	}
	k.U(s.leaves.size());
	for (auto &l : s.leaves)
	{
		k.U(l.cluster);
		k.U(l.area);
		k.U(l.firstBrush);
		k.U(l.brushCount);
	}
	k.U(s.brushes.size());
	for (auto &b : s.brushes)
	{
		k.U(b.firstSide);
		k.U(b.sideCount);
		k.U(b.solid);
		k.U(b.sky);
		k.U(b.world);
	}
	k.U(s.sides.size());
	for (auto v : s.sides)
		k.U(v);
	k.U(s.leafBrushes.size());
	for (auto v : s.leafBrushes)
		k.U(v);
	k.U(dynamic.size());
	for (auto &d : dynamic)
	{
		k.U(d.id);
		k.U(d.model);
		k.B(d.bounds);
		k.U(d.sweptExtentKnown);
	}
	return k.Finish();
}
struct Triangle
{
	Vec3 p[3];
	Id surface;
	bool opaque;
	bool patch;
};
inline Vec3 Blend(Vec3 a, Vec3 b, double t)
{
	return Vec3(float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t),
				float(a.z + (b.z - a.z) * t));
}
inline Vec3 Bezier(const BakedSurface &s, unsigned x, unsigned y, double u, double v)
{
	Vec3 rows[3];
	for (unsigned j = 0; j < 3; ++j)
	{
		auto a = s.vertices[(y + j) * s.patchWidth + x].position,
			 b = s.vertices[(y + j) * s.patchWidth + x + 1].position,
			 c = s.vertices[(y + j) * s.patchWidth + x + 2].position;
		rows[j] = Blend(Blend(a, b, u), Blend(b, c, u), u);
	}
	return Blend(Blend(rows[0], rows[1], v), Blend(rows[1], rows[2], v), v);
}
inline std::vector<Triangle> Geometry(const O::Sources &sources, const Bounds &roi,
									  const std::string &prefix)
{
	std::vector<Triangle> triangles;
	std::ofstream out(prefix + "-geometry.obj");
	uint64_t vertex = 0;
	for (auto &s : sources.geometry->Surfaces())
	{
		std::string kind = s.motion != SurfaceMotion::StaticWorld
							   ? "moving"
							   : !s.materialFacts.available
									 ? "unresolved"
									 : s.materialFacts.signals & MaterialSky
										   ? "sky"
										   : s.materialFacts.signals & MaterialCutout
												 ? "alpha"
												 : s.materialFacts.signals & MaterialTranslucent
													   ? "translucent"
													   : "opaque";
		out << "g " << kind << "_surface_" << s.ordinal + 1 << '\n';
		auto emit = [&](Vec3 a, Vec3 b, Vec3 c, bool patch) {
			Vec3 p[3] = {a, b, c};
			for (unsigned k = 0; k < 3; ++k)
			{
				double lo = std::min({Axis(a, k), Axis(b, k), Axis(c, k)}),
					   hi = std::max({Axis(a, k), Axis(b, k), Axis(c, k)});
				if (hi < Axis(roi.minWorld, k) || lo > Axis(roi.maxWorld, k))
					return;
			}
			for (auto v : p)
				out << std::setprecision(9) << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
			out << "f " << vertex + 1 << ' ' << vertex + 2 << ' ' << vertex + 3 << '\n';
			vertex += 3;
			if (s.motion == SurfaceMotion::StaticWorld &&
				!(s.materialFacts.signals & MaterialNoDraw))
			{
				Triangle t;
				std::copy(p, p + 3, t.p);
				t.surface = Id(s.ordinal) + 1;
				t.patch = patch;
				t.opaque = kind == "opaque";
				triangles.push_back(t);
			}
		};
		if (s.kind == BakedSurfaceKind::Face || s.kind == BakedSurfaceKind::Triangles)
			for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
				emit(s.vertices[s.indices[i]].position, s.vertices[s.indices[i + 1]].position,
					 s.vertices[s.indices[i + 2]].position, false);
		if (s.kind == BakedSurfaceKind::Patch)
			for (unsigned y = 0; y + 2 < s.patchHeight; y += 2)
				for (unsigned x = 0; x + 2 < s.patchWidth; x += 2)
					for (unsigned v = 0; v < 8; ++v)
						for (unsigned u = 0; u < 8; ++u)
						{
							auto a = Bezier(s, x, y, u / 8., v / 8.),
								 b = Bezier(s, x, y, (u + 1) / 8., v / 8.),
								 c = Bezier(s, x, y, (u + 1) / 8., (v + 1) / 8.),
								 d = Bezier(s, x, y, u / 8., (v + 1) / 8.);
							emit(a, b, c, true);
							emit(a, c, d, true);
						}
	}
	if (!out)
		throw std::runtime_error("Geometry OBJ write failed");
	return triangles;
}
using D = std::array<double, 3>;
inline D Sub(Vec3 a, Vec3 b)
{
	return {{double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z}};
}
inline D Cross(D a, D b)
{
	return {{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}};
}
inline double Dot(D a, D b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
// Plane intersection followed by barycentric containment; independent from core watertight
// shearing.
inline double Reference(const Triangle &t, Vec3 start, Vec3 end)
{
	D a = Sub(t.p[1], t.p[0]), b = Sub(t.p[2], t.p[0]), n = Cross(a, b), ray = Sub(end, start);
	double denom = Dot(ray, n);
	if (std::abs(denom) < 1e-14)
		return 2;
	double f = Dot(Sub(t.p[0], start), n) / denom;
	if (f <= 1e-8 || f >= 1 - 1e-8)
		return 2;
	D w = Sub(start, t.p[0]);
	for (unsigned k = 0; k < 3; ++k)
		w[k] += f * ray[k];
	double aa = Dot(a, a), ab = Dot(a, b), bb = Dot(b, b), wa = Dot(w, a), wb = Dot(w, b),
		   det = aa * bb - ab * ab;
	if (det <= 0)
		return 2;
	double u = (wa * bb - wb * ab) / det, v = (wb * aa - wa * ab) / det;
	return u >= 1e-9 && v >= 1e-9 && u + v <= 1 - 1e-9
			   ? f
			   : 2; // strict interior, boundary ambiguity reported separately
}
struct RayStats
{
	uint64_t states[3] = {}, referenceOpaqueHits = 0, falseClear = 0, exactContacts = 0;
	double maxContactError = 0;
};
inline RayStats Rays(const O::Sources &s, const LightingRegionGraph &graph,
					 const StaticVisibility &scene, const ReadContext &ctx,
					 const std::vector<Triangle> &triangles, const std::string &prefix)
{
	std::vector<Vec3> free;
	for (auto &c : graph.Cells())
		if (c.bsp.space == SpaceClass::Free || c.bsp.space == SpaceClass::Exterior)
			free.push_back(c.representativeWorld);
	RayStats result;
	std::ofstream out(prefix + "-rays.tsv");
	out << "sx\tsy\tsz\tex\tey\tez\tstate\tissues\tterminal_surface\treference_face_t\n";
	for (size_t i = 0; i < 256 && free.size() > 1; ++i)
	{
		RayQuery q;
		q.startWorld = free[(i * 7919 + 3) % free.size()];
		q.endWorld = free[(i * 1543 + 17) % free.size()];
		if (q.startWorld.x == q.endWorld.x && q.startWorld.y == q.endWorld.y &&
			q.startWorld.z == q.endWorld.z)
			continue;
		q.policy.facing = FacingPolicy::TwoSided;
		RayResult r;
		Check(scene.Query(q, ctx, r));
		++result.states[unsigned(r.state)];
		double first = 2;
		Id firstId = 0;
		for (auto &t : triangles)
			if (t.opaque && !t.patch)
			{
				double f = Reference(t, q.startWorld, q.endWorld);
				if (f < first)
				{
					first = f;
					firstId = t.surface;
				}
			}
		if (first < 1)
		{
			++result.referenceOpaqueHits;
			if (r.state == VisibilityState::Visible)
				++result.falseClear;
		}
		if (firstId && r.terminalHit.surface == firstId && r.terminalHit.positionKnown)
		{
			double length =
				std::sqrt(Dot(Sub(q.endWorld, q.startWorld), Sub(q.endWorld, q.startWorld)));
			result.maxContactError = std::max(
				result.maxContactError, std::abs(r.terminalHit.distanceWorld - first * length));
			++result.exactContacts;
		}
		out << std::setprecision(9) << q.startWorld.x << '\t' << q.startWorld.y << '\t'
			<< q.startWorld.z << '\t' << q.endWorld.x << '\t' << q.endWorld.y << '\t'
			<< q.endWorld.z << '\t' << unsigned(r.state) << '\t' << r.issues << '\t'
			<< r.terminalHit.surface << '\t' << first << '\n';
	}
	if (result.falseClear || result.maxContactError > 1e-4)
		throw std::runtime_error("Independent opaque-face ray reference mismatch");
	(void)s;
	return result;
}
} // namespace RegionAudit
