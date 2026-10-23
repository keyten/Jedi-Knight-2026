// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "visibility.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>

namespace Rend2LightRecon
{
namespace Core
{
namespace
{
struct D3
{
	double a[3];
	D3() : a{0, 0, 0} {}
	D3(Vec3 v) : a{v.x, v.y, v.z} {}
	D3(double x, double y, double z) : a{x, y, z} {}
	D3 operator+(D3 v) const
	{
		return {a[0] + v.a[0], a[1] + v.a[1], a[2] + v.a[2]};
	}
	D3 operator-(D3 v) const
	{
		return {a[0] - v.a[0], a[1] - v.a[1], a[2] - v.a[2]};
	}
	D3 operator*(double v) const
	{
		return {a[0] * v, a[1] * v, a[2] * v};
	}
	Vec3 Float() const
	{
		return Vec3(float(a[0]), float(a[1]), float(a[2]));
	}
};
double Dot(D3 a, D3 b)
{
	return a.a[0] * b.a[0] + a.a[1] * b.a[1] + a.a[2] * b.a[2];
}
D3 Cross(D3 a, D3 b)
{
	return {a.a[1] * b.a[2] - a.a[2] * b.a[1], a.a[2] * b.a[0] - a.a[0] * b.a[2],
			a.a[0] * b.a[1] - a.a[1] * b.a[0]};
}
double Length(D3 a)
{
	return std::sqrt(Dot(a, a));
}
bool Finite(D3 a)
{
	return std::isfinite(a.a[0]) && std::isfinite(a.a[1]) && std::isfinite(a.a[2]);
}
bool Cancelled(const ReadContext &c)
{
	return c.cancellation && c.cancellation->IsCancelled();
}
struct Box
{
	D3 low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
	void Add(D3 p)
	{
		for (unsigned k = 0; k < 3; ++k)
		{
			low.a[k] = std::min(low.a[k], p.a[k]);
			high.a[k] = std::max(high.a[k], p.a[k]);
		}
	}
	void Add(const Box &b)
	{
		Add(b.low);
		Add(b.high);
	}
	void Expand(double e)
	{
		for (unsigned k = 0; k < 3; ++k)
		{
			low.a[k] -= e;
			high.a[k] += e;
		}
	}
};
bool Slab(const Box &b, D3 o, D3 d, double limit, double &near, double &far)
{
	near = 0;
	far = limit;
	for (unsigned k = 0; k < 3; ++k)
	{
		if (d.a[k] == 0)
		{
			if (o.a[k] < b.low.a[k] || o.a[k] > b.high.a[k])
				return false;
			continue;
		}
		double a = (b.low.a[k] - o.a[k]) / d.a[k], z = (b.high.a[k] - o.a[k]) / d.a[k];
		if (a > z)
			std::swap(a, z);
		near = std::max(near, a);
		far = std::min(far, z);
		if (near > far)
			return false;
	}
	return true;
}
struct Primitive
{
	Vec3 p[3];
	Box bounds;
	Id id = 0;
	uint32_t surface = 0;
	double error = 0;
	bool boundsOnly = false;
};
struct Node
{
	Box bounds;
	uint32_t first = 0, count = 0, left = 0, right = 0;
};
ObstacleKind Optics(const MaterialFacts &m)
{
	if (!m.available || (m.signals & MaterialMissing) ||
		(MaterialUncertaintyFlags(m) & MaterialValuesInvalid))
		return ObstacleKind::Unsupported;
	if (m.signals & MaterialSky)
		return ObstacleKind::Sky;
	if (m.signals & MaterialCutout)
		return ObstacleKind::AlphaTested;
	if (m.signals & MaterialTranslucent)
		return ObstacleKind::Translucent;
	return ObstacleKind::Opaque; // emission/unlit do not make an otherwise opaque sheet transparent
}
D3 PatchPoint(const BakedSurface &s, size_t base, double u, double v)
{
	double bu[3] = {(1 - u) * (1 - u), 2 * u * (1 - u), u * u},
		   bv[3] = {(1 - v) * (1 - v), 2 * v * (1 - v), v * v};
	D3 anchor(s.vertices[base].position), p = anchor;
	for (size_t y = 0; y < 3; ++y)
		for (size_t x = 0; x < 3; ++x)
			p = p +
				(D3(s.vertices[base + y * s.patchWidth + x].position) - anchor) * (bu[x] * bv[y]);
	return p;
}
double PatchCurvature(const BakedSurface &s, size_t base)
{
	double uu = 0, vv = 0, uv = 0;
	auto p = [&](size_t x, size_t y) {
		return D3(s.vertices[base + y * s.patchWidth + x].position);
	};
	for (size_t k = 0; k < 3; ++k)
	{
		uu = std::max(uu, 2 * Length(p(2, k) - p(1, k) * 2 + p(0, k)));
		vv = std::max(vv, 2 * Length(p(k, 2) - p(k, 1) * 2 + p(k, 0)));
	}
	for (size_t y = 0; y < 2; ++y)
		for (size_t x = 0; x < 2; ++x)
			uv = std::max(uv, 4 * Length(p(x + 1, y + 1) - p(x + 1, y) - p(x, y + 1) + p(x, y)));
	return .5 * (uu + vv + 2 * uv);
}
template <class Emit>
Error EmitGeometry(const SurfaceGeometry &geometry, const VisibilityBuildOptions &options,
				   const ReadContext &ctx, VisibilityBuildReport &report, Emit emit)
{
	for (const auto &s : geometry.Surfaces())
	{
		if (Cancelled(ctx))
			return Failure(Code::Cancelled, "Visibility build cancelled");
		if (s.ordinal >= geometry.Surfaces().size())
			return Failure(Code::InvalidInput, "Invalid source surface ordinal");
		if (static_cast<unsigned>(s.kind) > static_cast<unsigned>(BakedSurfaceKind::Unsupported) ||
			static_cast<unsigned>(s.motion) > static_cast<unsigned>(SurfaceMotion::Unknown))
			return Failure(Code::InvalidInput, "Invalid static geometry kind/motion");
		++report.surfaces;
		if (s.motion == SurfaceMotion::StaticWorld && s.model != 0)
			return Failure(Code::InvalidInput, "Static surface belongs to a non-world model");
		if (s.motion == SurfaceMotion::BrushModel || s.motion == SurfaceMotion::Deformed ||
			(s.materialFacts.signals & MaterialDeformed))
		{
			++report.excludedMoving;
			continue;
		}
		if (s.materialFacts.signals & MaterialNoDraw)
		{
			++report.excludedNoDraw;
			continue;
		}
		Box bounds;
		for (const auto &v : s.vertices)
		{
			if (!Finite(v.position))
				return Failure(Code::InvalidInput, "Non-finite static vertex");
			bounds.Add(D3(v.position));
		}
		auto unsupported = [&]() -> Error {
			if (s.vertices.empty())
			{
				++report.unboundedUnsupported;
				return Error();
			}
			Primitive p;
			p.bounds = bounds;
			p.boundsOnly = true;
			p.surface = s.ordinal;
			p.id = (Id(s.ordinal + 1) << 32) | 1;
			++report.unsupportedBounds;
			return emit(p);
		};
		if (s.kind == BakedSurfaceKind::Unsupported || s.motion == SurfaceMotion::Unknown)
		{
			auto e = unsupported();
			if (!e)
				return e;
			continue;
		}
		uint32_t local = 0;
		auto triangle = [&](D3 a, D3 b, D3 c, double error) -> Error {
			Primitive p;
			p.p[0] = a.Float();
			p.p[1] = b.Float();
			p.p[2] = c.Float();
			p.surface = s.ordinal;
			p.id = (Id(s.ordinal + 1) << 32) | Id(++local);
			p.error = error;
			for (auto v : p.p)
				p.bounds.Add(D3(v));
			p.bounds.Expand(error);
			if (Length(Cross(b - a, c - a)) == 0)
			{
				p.boundsOnly = true;
				++report.degenerate;
			}
			else
			{
				++report.triangles;
				if (s.kind == BakedSurfaceKind::Patch)
					++report.patchTriangles;
			}
			report.maximumPatchErrorWorld = std::max(report.maximumPatchErrorWorld, error);
			return emit(p);
		};
		if (s.kind == BakedSurfaceKind::Patch)
		{
			if (s.patchWidth < 3 || s.patchHeight < 3 || !(s.patchWidth & 1) ||
				!(s.patchHeight & 1) || uint64_t(s.patchWidth) * s.patchHeight != s.vertices.size())
				return Failure(Code::InvalidInput, "Malformed patch control net");
			for (uint32_t y = 0; y + 2 < s.patchHeight; y += 2)
				for (uint32_t x = 0; x + 2 < s.patchWidth; x += 2)
				{
					const size_t base = size_t(y) * s.patchWidth + x;
					double curve = PatchCurvature(s, base), rounding = 0;
					// Float storage is additional to the analytic chord bound.
					for (size_t j = 0; j < 3; ++j)
						for (size_t i = 0; i < 3; ++i)
							for (unsigned k = 0; k < 3; ++k)
								rounding = std::max(
									rounding,
									std::abs(
										D3(s.vertices[base + j * s.patchWidth + i].position).a[k]) *
										2 * std::numeric_limits<float>::epsilon());
					unsigned n = 1;
					while (curve / (double(n) * n) > options.patchToleranceWorld &&
						   n < options.maxPatchSubdivisions)
						n *= 2;
					if (curve / (double(n) * n) > options.patchToleranceWorld)
						return Failure(Code::WorkLimit,
									   "Patch tolerance requires more subdivisions");
					double error = curve / (double(n) * n) + rounding;
					for (unsigned j = 0; j < n; ++j)
						for (unsigned i = 0; i < n; ++i)
						{
							D3 a = PatchPoint(s, base, double(i) / n, double(j) / n),
							   b = PatchPoint(s, base, double(i + 1) / n, double(j) / n),
							   c = PatchPoint(s, base, double(i + 1) / n, double(j + 1) / n),
							   d = PatchPoint(s, base, double(i) / n, double(j + 1) / n);
							auto e = triangle(a, b, c, error);
							if (!e)
								return e;
							e = triangle(a, c, d, error);
							if (!e)
								return e;
						}
				}
		}
		else
		{
			if (s.indices.empty())
			{
				auto error = unsupported();
				if (!error)
					return error;
				continue;
			}
			if (s.indices.size() % 3)
				return Failure(Code::InvalidInput, "Incomplete static triangle");
			for (size_t i = 0; i < s.indices.size(); i += 3)
			{
				for (size_t k = 0; k < 3; ++k)
					if (s.indices[i + k] >= s.vertices.size())
						return Failure(Code::InvalidInput, "Invalid static vertex index");
				auto e = triangle(D3(s.vertices[s.indices[i]].position),
								  D3(s.vertices[s.indices[i + 1]].position),
								  D3(s.vertices[s.indices[i + 2]].position), 0);
				if (!e)
					return e;
			}
		}
	}
	return Error();
}
// Double-precision watertight shear/edge test: shared edges use the same
// arithmetic instead of a barycentric epsilon that opens cracks in thin walls.
bool Intersect(const Primitive &p, D3 o, D3 d, double limit, double &t, D3 &normal, bool &coplanar)
{
	D3 a = D3(p.p[0]) - o, b = D3(p.p[1]) - o, c = D3(p.p[2]) - o;
	normal = Cross(D3(p.p[1]) - D3(p.p[0]), D3(p.p[2]) - D3(p.p[0]));
	double len = Length(normal), den = Dot(normal, d);
	coplanar = std::abs(den) <= len * 1e-14 && std::abs(Dot(normal, a)) <= len * 1e-10;
	if (coplanar)
		return false;
	unsigned z = 0;
	for (unsigned k = 1; k < 3; ++k)
		if (std::abs(d.a[k]) > std::abs(d.a[z]))
			z = k;
	unsigned x = (z + 1) % 3, y = (x + 1) % 3;
	if (d.a[z] < 0)
		std::swap(x, y);
	double sx = d.a[x] / d.a[z], sy = d.a[y] / d.a[z], sz = 1 / d.a[z];
	double ax = a.a[x] - sx * a.a[z], ay = a.a[y] - sy * a.a[z], bx = b.a[x] - sx * b.a[z],
		   by = b.a[y] - sy * b.a[z], cx = c.a[x] - sx * c.a[z], cy = c.a[y] - sy * c.a[z];
	double u = cx * by - cy * bx, v = ax * cy - ay * cx, w = bx * ay - by * ax;
	if ((u < 0 || v < 0 || w < 0) && (u > 0 || v > 0 || w > 0))
		return false;
	double determinant = u + v + w;
	if (determinant == 0)
		return false;
	t = (u * a.a[z] + v * b.a[z] + w * c.a[z]) * sz / determinant;
	return t >= 0 && t <= limit;
}
struct Event
{
	RayHit hit;
	double t = 1e300;
	uint32_t issues = 0;
	bool assumed = false;
};
void Choose(Event &target, const Event &value)
{
	if (value.t < target.t || (value.t == target.t && value.hit.triangle < target.hit.triangle))
		target = value;
}
bool SkyPurpose(RayPurpose p)
{
	return p == RayPurpose::Sky || p == RayPurpose::Sun;
}
} // namespace

struct StaticVisibility::Data
{
	std::vector<Primitive> primitives;
	std::vector<Node> nodes;
	std::vector<VisibilitySurface> surfaces;
	std::shared_ptr<const PointClassifier> domain;
	LightmapMapIdentity identity;
	VisibilityBuildReport report;
	uint32_t Split(uint32_t first, uint32_t count, unsigned depth, const VisibilityBuildOptions &o,
				   bool &exceeded)
	{
		uint32_t index = static_cast<uint32_t>(nodes.size());
		nodes.emplace_back();
		report.depth = std::max(report.depth, depth);
		Box box, centroids;
		for (uint32_t i = first; i < first + count; ++i)
		{
			box.Add(primitives[i].bounds);
			centroids.Add((primitives[i].bounds.low + primitives[i].bounds.high) * .5);
		}
		report.buildWork += count;
		if (report.buildWork > o.maxWork)
		{
			exceeded = true;
			return index;
		}
		nodes[index].bounds = box;
		if (count <= o.leafSize)
		{
			nodes[index].first = first;
			nodes[index].count = count;
			return index;
		}
		unsigned axis = 0;
		for (unsigned k = 1; k < 3; ++k)
			if (centroids.high.a[k] - centroids.low.a[k] >
				centroids.high.a[axis] - centroids.low.a[axis])
				axis = k;
		std::sort(primitives.begin() + first, primitives.begin() + first + count,
				  [axis](const Primitive &a, const Primitive &b) {
					  double ac = a.bounds.low.a[axis] + a.bounds.high.a[axis],
							 bc = b.bounds.low.a[axis] + b.bounds.high.a[axis];
					  return ac != bc ? ac < bc : a.id < b.id;
				  });
		uint32_t half = count / 2;
		nodes[index].left = Split(first, half, depth + 1, o, exceeded);
		if (exceeded)
			return index;
		nodes[index].right = Split(first + half, count - half, depth + 1, o, exceeded);
		return index;
	}
	Error Trace(const RayQuery &q, D3 origin, D3 direction, double length, const ReadContext &ctx,
				RayResult &result, Event &first, Event &opaque, Event &sky, Event &unknown,
				bool guard = false) const
	{
		if (nodes.empty())
			return Error();
		uint32_t stack[64] = {0};
		size_t size = 1;
		while (size)
		{
			if (Cancelled(ctx))
				return Failure(Code::Cancelled, "Visibility query cancelled");
			if (result.nodeTests + result.primitiveTests >= q.maxWork)
				return Failure(Code::WorkLimit, "Visibility ray work limit");
			const auto &node = nodes[stack[--size]];
			++result.nodeTests;
			double near, far;
			if (!Slab(node.bounds, origin, direction, std::min(length, opaque.t), near, far))
				continue;
			if (!node.count)
			{
				if (size + 2 > 64)
					return Failure(Code::WorkLimit, "Visibility traversal stack limit");
				stack[size++] = node.right;
				stack[size++] = node.left;
				continue;
			}
			for (uint32_t i = node.first; i < node.first + node.count; ++i)
			{
				if (result.nodeTests + result.primitiveTests >= q.maxWork)
					return Failure(Code::WorkLimit, "Visibility ray work limit");
				++result.primitiveTests;
				const auto &p = primitives[i];
				const auto &s = surfaces[p.surface];
				if (!Slab(p.bounds, origin, direction, std::min(length, opaque.t), near, far))
					continue;
				Event e;
				e.hit.present = true;
				e.hit.surface = s.id;
				e.hit.material = s.material;
				e.hit.triangle = p.id;
				e.hit.obstacle = s.obstacle;
				e.hit.errorWorld = p.error;
				e.hit.candidateNearWorld = near;
				e.hit.candidateFarWorld = far;
				if (p.boundsOnly)
					e.hit.obstacle = ObstacleKind::Unsupported;
				double t = near;
				D3 normal;
				bool coplanar = false;
				bool hit =
					!p.boundsOnly && Intersect(p, origin, direction, length, t, normal, coplanar);
				if (!hit)
				{
					if (!p.boundsOnly && !coplanar && p.error == 0)
						continue;
					e.t = near;
					e.issues = p.boundsOnly ? UnsupportedGeometry
											: coplanar ? CoplanarRay : PatchApproximation;
				}
				else
				{
					e.t = t;
					e.hit.positionKnown = true;
					e.hit.positionWorld = (origin + direction * t).Float();
					e.hit.geometricNormal = (normal * (1 / Length(normal))).Float();
					e.hit.frontFace = Dot(normal, direction) < 0;
					// Source-only, zero-distance suppression: never skip an entire surface.
					if (s.id == q.originSurface && t <= 1e-10 && q.bias.normalKnown)
						continue;
					if (!guard && s.id == q.targetSurface && length - t <= q.targetToleranceWorld)
						continue;
					if (!guard && q.policy.facing == FacingPolicy::Material)
					{
						if ((s.cull == SurfaceCull::Front && e.hit.frontFace) ||
							(s.cull == SurfaceCull::Back && !e.hit.frontFace))
							continue;
						if (s.cull == SurfaceCull::Unknown)
							e.issues |= SidednessUnknown;
					}
					if (t <= 1e-10)
						e.issues |= OriginOnBoundary;
					if (p.error > 0 && !q.policy.acceptPatchApproximation)
						e.issues |= PatchApproximation;
				}
				if (guard)
				{
					Choose(unknown, e);
					Choose(first, e);
					continue;
				}
				if (s.obstacle == ObstacleKind::Unsupported)
					e.issues |= OpticalUnknown;
				const bool alpha = (s.materialSignals & MaterialCutout) != 0;
				const bool translucent = (s.materialSignals & MaterialTranslucent) != 0;
				const bool uncertainOptic = alpha || translucent;
				const bool unknownOptic =
					(alpha && q.policy.alphaTested == OpticalPolicy::Unknown) ||
					(translucent && q.policy.translucent == OpticalPolicy::Unknown);
				const bool assumeOpaque =
					(alpha && q.policy.alphaTested == OpticalPolicy::AssumeOpaque) ||
					(translucent && q.policy.translucent == OpticalPolicy::AssumeOpaque);
				if (uncertainOptic && !unknownOptic)
				{
					e.assumed = true;
					e.issues |= PolicyAssumption;
					if (!assumeOpaque)
					{
						result.issues |= PolicyAssumption;
						Choose(first, e);
						continue;
					}
				}
				else if (uncertainOptic)
					e.issues |= OpticalUnknown;
				Choose(first, e);
				if (e.issues & ~uint32_t(PolicyAssumption))
					Choose(unknown, e);
				else if (s.obstacle == ObstacleKind::Sky)
					Choose(sky, e);
				else
					Choose(opaque, e);
			}
		}
		return Error();
	}
};

Error VisibilityProvider::Query(const RayQuery &, const ReadContext &, RayResult &out) const
{
	out = RayResult();
	return Failure(Code::Unavailable, "Structured visibility unavailable");
}
Error StaticVisibility::Build(std::shared_ptr<const SurfaceGeometry> geometry,
							  const VisibilityBuildOptions &options, const ReadContext &ctx,
							  std::shared_ptr<const StaticVisibility> &out,
							  std::shared_ptr<const PointClassifier> domain)
{
	out.reset();
	if (!geometry)
		return Failure(Code::MissingGeometry, "Missing static source geometry");
	if (!options.maxPrimitives || options.maxPrimitives > UINT32_MAX / 2 || !options.maxBytes ||
		!options.maxWork || !options.leafSize || options.leafSize > 32 ||
		!options.maxPatchSubdivisions || options.maxPatchSubdivisions > 256 ||
		(options.maxPatchSubdivisions & (options.maxPatchSubdivisions - 1)) ||
		!std::isfinite(options.patchToleranceWorld) || options.patchToleranceWorld <= 0)
		return Failure(Code::InvalidInput, "Invalid visibility build limits/tolerance");
	if (geometry->Surfaces().size() > options.maxSurfaces)
		return Failure(Code::MemoryLimit, "Too many static surfaces");
	try
	{
		VisibilityBuildReport report;
		uint64_t count = 0;
		auto error = EmitGeometry(*geometry, options, ctx, report, [&](const Primitive &) -> Error {
			if (++count > options.maxPrimitives)
				return Failure(Code::MemoryLimit, "Too many static primitives");
			if ((count & 1023) == 0 && Cancelled(ctx))
				return Failure(Code::Cancelled, "Visibility build cancelled");
			if (count > options.maxWork / 2)
				return Failure(Code::WorkLimit, "Visibility geometry work limit");
			return Error();
		});
		if (!error)
			return error;
		uint64_t bytes = sizeof(Data) + count * sizeof(Primitive) + 2 * count * sizeof(Node) +
						 geometry->Surfaces().size() * sizeof(VisibilitySurface) +
						 geometry->Map().path.size();
		if (bytes > options.maxBytes || bytes > ctx.maxBytes)
			return Failure(Code::MemoryLimit, "Visibility BVH exceeds build budget");
		std::shared_ptr<Data> data(new Data());
		data->identity = geometry->Map();
		data->domain = std::move(domain);
		data->report = report;
		data->report.buildWork = count * 2;
		data->report.peakBuildBytes = bytes;
		data->primitives.reserve(size_t(count));
		data->nodes.reserve(size_t(count * 2));
		data->surfaces.reserve(geometry->Surfaces().size());
		for (const auto &s : geometry->Surfaces())
		{
			VisibilitySurface meta;
			meta.id = Id(s.ordinal) + 1;
			meta.material = s.materialIndex;
			meta.materialSignals = s.materialFacts.signals;
			meta.obstacle = Optics(s.materialFacts);
			meta.cull = s.materialFacts.cull;
			data->surfaces.push_back(meta);
		}
		VisibilityBuildReport second;
		error = EmitGeometry(*geometry, options, ctx, second, [&](const Primitive &p) -> Error {
			data->primitives.push_back(p);
			return Error();
		});
		if (!error)
			return error;
		bool exceeded = false;
		if (count)
			data->Split(0, static_cast<uint32_t>(count), 1, options, exceeded);
		if (exceeded)
			return Failure(Code::WorkLimit, "BVH partition work limit");
		if (Cancelled(ctx))
			return Failure(Code::Cancelled, "Visibility build cancelled");
		data->report.nodes = data->nodes.size();
		data->report.ownedBytes = bytes;
		data->report.complete = options.completeStaticGeometry && !report.unboundedUnsupported;
		out = std::shared_ptr<const StaticVisibility>(new StaticVisibility(data));
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Visibility build allocation failed");
	}
}
const VisibilityBuildReport &StaticVisibility::Report() const
{
	return data_->report;
}
const LightmapMapIdentity &StaticVisibility::GeometryIdentity() const
{
	return data_->identity;
}
const std::vector<VisibilitySurface> &StaticVisibility::Surfaces() const
{
	return data_->surfaces;
}

Error StaticVisibility::Query(const RayQuery &q, const ReadContext &ctx, RayResult &out) const
{
	out = RayResult();
	if (!Finite(q.startWorld) || !Finite(q.endWorld) ||
		static_cast<unsigned>(q.purpose) > static_cast<unsigned>(RayPurpose::IrradianceProbe) ||
		static_cast<unsigned>(q.policy.facing) > static_cast<unsigned>(FacingPolicy::TwoSided) ||
		static_cast<unsigned>(q.policy.alphaTested) >
			static_cast<unsigned>(OpticalPolicy::Ignore) ||
		static_cast<unsigned>(q.policy.translucent) > static_cast<unsigned>(OpticalPolicy::Ignore))
		return Failure(Code::InvalidInput, "Invalid ray coordinates/policy");
	const auto &b = q.bias;
	if (!std::isfinite(b.absoluteWorld) || !std::isfinite(b.relative) ||
		!std::isfinite(b.maximumWorld) || !std::isfinite(b.footprintWorld) || b.absoluteWorld < 0 ||
		b.relative < 0 || b.relative > 1 || b.maximumWorld < 0 || b.footprintWorld < 0 ||
		!std::isfinite(q.targetToleranceWorld) || q.targetToleranceWorld < 0 || !q.maxWork ||
		q.originSurface > data_->surfaces.size() || q.targetSurface > data_->surfaces.size() ||
		(b.normalKnown && (!q.originSurface || !Finite(b.geometricNormal) ||
						   std::abs(Length(D3(b.geometricNormal)) - 1) > 1e-4)))
		return Failure(Code::InvalidInput, "Invalid ray bias/normal");
	D3 start(q.startWorld), end(q.endWorld), delta = end - start;
	double originalLength = Length(delta);
	if (!(originalLength > 1e-12) || !std::isfinite(originalLength) ||
		q.targetToleranceWorld >= originalLength)
		return Failure(Code::InvalidInput, "Degenerate ray or excessive endpoint tolerance");
	if (ctx.maxBytes < 4096)
		return Failure(Code::MemoryLimit, "Ray requires 4096-byte scratch budget");
	if (Cancelled(ctx))
		return Failure(Code::Cancelled, "Visibility query cancelled");
	RayResult result;
	result.effectiveOriginWorld = q.startWorld;
	PointDomain endDomain = PointDomain::Unknown;
	auto classify = [&](Vec3 p, PointDomain &domain) -> Error {
		if (!data_->domain)
			return Error();
		try
		{
			auto error = data_->domain->Classify(p, b.absoluteWorld, domain);
			if (static_cast<unsigned>(error.code) > static_cast<unsigned>(Code::AlgorithmError))
				return Failure(Code::ProviderError, "Invalid domain classifier error");
			if (!error)
				return error;
			if (static_cast<unsigned>(domain) > static_cast<unsigned>(PointDomain::Boundary))
				return Failure(Code::ProviderError, "Invalid domain classifier result");
			return Error();
		}
		catch (const std::exception &)
		{
			return Failure(Code::ProviderError, "Point classifier threw");
		}
		catch (...)
		{
			return Failure(Code::ProviderError, "Point classifier threw");
		}
	};
	auto error = classify(q.startWorld, result.originDomain);
	if (!error)
		return error;
	if (result.originDomain == PointDomain::Solid)
	{
		result.issues |= OriginInsideSolid;
		out = result;
		return Error();
	}
	error = classify(q.endWorld, endDomain);
	if (!error)
		return error;
	if (endDomain == PointDomain::Solid)
		result.issues |= EndpointInsideSolid;
	D3 direction = delta * (1 / originalLength), origin = start;
	if (b.normalKnown)
	{
		double magnitude = 1;
		for (unsigned k = 0; k < 3; ++k)
			magnitude = std::max(magnitude, std::abs(start.a[k]));
		double requested = b.absoluteWorld + b.relative * magnitude, cap = b.maximumWorld;
		if (b.footprintWorld > 0)
			cap = std::min(cap, b.footprintWorld * .01);
		if (requested > cap)
		{
			result.issues |= PrecisionLimit;
			out = result;
			return Error();
		}
		if (requested >= originalLength)
			return Failure(Code::InvalidInput, "Ray bias exceeds segment length");
		D3 normal(b.geometricNormal);
		double sign = Dot(normal, direction);
		if (sign != 0 && requested > 0)
		{
			origin = start + normal * (sign > 0 ? requested : -requested);
			Event first, opaque, sky, unknown;
			error = data_->Trace(q, start, (origin - start) * (1 / requested), requested, ctx,
								 result, first, opaque, sky, unknown, true);
			if (!error)
				return error;
			if (first.hit.present)
			{
				result.issues |= BiasObstructed;
				result.firstHit = first.hit;
				if (first.hit.positionKnown)
				{
					result.firstHit.distanceWorld = first.t;
					result.firstHit.parameter = first.t / requested;
				}
				out = result;
				return Error();
			}
			result.appliedBiasWorld = requested;
			result.effectiveOriginWorld = origin.Float();
			if (result.originDomain == PointDomain::Boundary)
			{
				error = classify(result.effectiveOriginWorld, result.originDomain);
				if (!error)
					return error;
			}
			if (result.originDomain == PointDomain::Solid)
			{
				result.issues |= OriginInsideSolid;
				out = result;
				return Error();
			}
		}
	}
	bool free = result.originDomain == PointDomain::Free ||
				(q.originKnownFree && result.originDomain == PointDomain::Unknown);
	if (!free)
		result.issues |=
			result.originDomain == PointDomain::Boundary ? OriginOnBoundary : OriginUnclassified;
	double length = Length(end - origin);
	direction = (end - origin) * (1 / length);
	Event first, opaque, sky, unknown;
	error = data_->Trace(q, origin, direction, length, ctx, result, first, opaque, sky, unknown);
	if (!error)
		return error;
	if (Cancelled(ctx))
		return Failure(Code::Cancelled, "Visibility query cancelled");
	auto decorate = [&](Event e) -> RayHit {
		if (!e.hit.present)
			return e.hit;
		if (e.hit.positionKnown)
		{
			e.hit.distanceWorld = Length(origin + direction * e.t - start);
			e.hit.parameter = e.t / length;
		}
		if (e.hit.positionKnown && e.hit.errorWorld > 0)
			result.issues |= PatchApproximation;
		return e.hit;
	};
	result.firstHit = decorate(first);
	result.sky = SkyReach::NotReached;
	if (opaque.hit.present && sky.hit.present && std::abs(opaque.t - sky.t) <= 1e-10)
	{
		result.issues |= CoincidentSurfaces;
		result.sky = SkyReach::Unknown;
	}
	else if (opaque.hit.present && opaque.t < sky.t)
	{
		result.state = VisibilityState::Blocked;
		result.terminalHit = decorate(opaque);
		result.issues |= first.issues | opaque.issues;
		result.reliability = opaque.assumed
								 ? RayReliability::Assumed
								 : opaque.hit.errorWorld > 0 ? RayReliability::Approximate
															 : RayReliability::Geometric;
		// Origin-boundary ambiguity is not repaired by a more distant wall.
		if (unknown.hit.present && (unknown.issues & OriginOnBoundary))
			result.state = VisibilityState::Unknown;
	}
	else if (sky.hit.present)
	{
		result.terminalHit = decorate(sky);
		result.issues |= first.issues | sky.issues;
		if (SkyPurpose(q.purpose) && unknown.t > sky.t)
		{
			result.state = VisibilityState::Visible;
			result.sky = SkyReach::Reached;
			result.reliability = RayReliability::Geometric;
		}
		else
		{
			result.state = VisibilityState::Unknown;
			result.sky = SkyReach::Unknown;
		}
	}
	else if (unknown.hit.present)
	{
		result.issues |= unknown.issues;
		result.sky = SkyReach::Unknown;
	}
	else if (SkyPurpose(q.purpose))
	{
		result.issues |= SkyBoundaryMissing;
		result.sky = SkyReach::Unknown;
	}
	else
	{
		result.state = VisibilityState::Visible;
		result.reliability = RayReliability::Geometric;
	}
	if ((q.policy.requireKnownFreeOrigin && !free) || endDomain == PointDomain::Solid)
	{
		if (result.state != VisibilityState::Blocked)
			result.state = VisibilityState::Unknown;
	}
	if (!q.policy.requireKnownFreeOrigin && !free)
		result.issues |= PolicyAssumption;
	if (!data_->report.complete && result.state != VisibilityState::Blocked)
	{
		result.state = VisibilityState::Unknown;
		result.issues |= IncompleteGeometry;
	}
	if (result.issues & PolicyAssumption)
		result.reliability = RayReliability::Assumed;
	if (result.state == VisibilityState::Unknown)
	{
		result.reliability = RayReliability::Unknown;
		if (result.sky == SkyReach::Reached)
			result.sky = SkyReach::Unknown;
	}
	out = result;
	return Error();
}
Error VisibilityProvider::Batch(const RayQuery *queries, size_t count, const ReadContext &ctx,
							  RayResult *out, size_t &completed) const
{
	completed = 0;
	if (!count)
		return Error();
	if (count && (!queries || !out))
		return Failure(Code::InvalidInput, "Missing ray batch buffers");
	if (ctx.maxBytes < 4096 || count > (ctx.maxBytes - 4096) / sizeof(RayResult))
		return Failure(Code::MemoryLimit, "Ray batch result/scratch budget exceeded");
	for (size_t i = 0; i < count; ++i)
	{
		auto error = Query(queries[i], ctx, out[i]);
		if (!error)
			return error;
		++completed;
	}
	return Error();
}
Error StaticVisibility::Segment(const Vec3 &start, const Vec3 &end, const ReadContext &ctx,
								float &transmission) const
{
	RayQuery q;
	q.startWorld = start;
	q.endWorld = end;
	RayResult result;
	auto error = Query(q, ctx, result);
	if (!error)
		return error;
	if (result.state == VisibilityState::Unknown || result.reliability != RayReliability::Geometric)
		return Failure(Code::Unavailable, "No exact geometric transmission result");
	transmission = result.state == VisibilityState::Visible ? 1.0f : 0.0f;
	return Error();
}
Error StaticVisibility::Sky(const Vec3 &start, const Vec3 &direction, const ReadContext &ctx,
							float &transmission) const
{
	if (!Finite(direction) || std::abs(Length(D3(direction)) - 1) > 1e-4)
		return Failure(Code::InvalidInput, "Sky direction must be unit receiver-to-sky");
	// Finite bound guaranteed to cover this scene, never an arbitrary sun distance.
	double reach = 1;
	if (!data_->nodes.empty())
		for (unsigned corner = 0; corner < 8; ++corner)
		{
			D3 p;
			for (unsigned k = 0; k < 3; ++k)
				p.a[k] = (corner & (1u << k)) ? data_->nodes[0].bounds.high.a[k]
											  : data_->nodes[0].bounds.low.a[k];
			reach = std::max(reach, Length(p - D3(start)) + 1);
		}
	D3 end = D3(start) + D3(direction) * reach;
	if (!Finite(start) || !Finite(end) || !Finite(end.Float()))
		return Failure(Code::InvalidInput, "Sky endpoint outside finite world range");
	RayQuery q;
	q.startWorld = start;
	q.endWorld = end.Float();
	q.purpose = RayPurpose::Sky;
	RayResult result;
	auto error = Query(q, ctx, result);
	if (!error)
		return error;
	if (result.state == VisibilityState::Unknown || result.reliability != RayReliability::Geometric)
		return Failure(Code::Unavailable, "No exact geometric sky result");
	transmission = result.sky == SkyReach::Reached ? 1.0f : 0.0f;
	return Error();
}
} // namespace Core
} // namespace Rend2LightRecon
