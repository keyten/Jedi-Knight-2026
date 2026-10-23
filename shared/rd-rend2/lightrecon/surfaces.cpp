// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "surfaces.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <locale>
#include <iomanip>

namespace Rend2LightRecon
{
namespace Core
{
namespace
{
struct D3
{
	double x = 0, y = 0, z = 0;
	D3() = default;
	D3(double a, double b, double c) : x(a), y(b), z(c) {}
	D3(Vec3 v) : x(v.x), y(v.y), z(v.z) {}
	D3 operator+(D3 b) const
	{
		return {x + b.x, y + b.y, z + b.z};
	}
	D3 operator-(D3 b) const
	{
		return {x - b.x, y - b.y, z - b.z};
	}
	D3 operator*(double b) const
	{
		return {x * b, y * b, z * b};
	}
	Vec3 value() const
	{
		return Vec3(float(x), float(y), float(z));
	}
};
double dot(D3 a, D3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
D3 cross(D3 a, D3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(D3 a)
{
	return std::sqrt(dot(a, a));
}
D3 unit(D3 a)
{
	double l = length(a);
	return std::isfinite(l) && l > 1e-15 ? a * (1 / l) : D3();
}
bool finite(D3 a)
{
	return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
LightmapUv sub(LightmapUv a, LightmapUv b)
{
	return {a.u - b.u, a.v - b.v};
}
LightmapUv add(LightmapUv a, LightmapUv b)
{
	return {a.u + b.u, a.v + b.v};
}
LightmapUv mul(LightmapUv a, double b)
{
	return {a.u * b, a.v * b};
}
double wedge(LightmapUv a, LightmapUv b)
{
	return a.u * b.v - a.v * b.u;
}
using Polygon = std::vector<LightmapUv>;
double area(const Polygon &p)
{
	double sum = 0;
	for (size_t i = 0; i < p.size(); ++i)
		sum += wedge(p[i], p[(i + 1) % p.size()]);
	return std::abs(sum) * .5;
}
Polygon clip(const Polygon &p, LightmapUv a, LightmapUv b)
{
	Polygon out;
	if (p.empty())
		return out;
	auto edge = sub(b, a);
	auto prev = p.back();
	double dp = wedge(edge, sub(prev, a));
	for (auto current : p)
	{
		double dc = wedge(edge, sub(current, a));
		bool pin = dp >= 0, cin = dc >= 0;
		if (pin != cin)
		{
			double t = dp / (dp - dc);
			out.push_back(add(prev, mul(sub(current, prev), t)));
		}
		if (cin)
			out.push_back(current);
		prev = current;
		dp = dc;
	}
	return out;
}
Polygon intersect(Polygon p, const Polygon &q)
{
	for (size_t i = 0; i < q.size() && !p.empty(); ++i)
		p = clip(p, q[i], q[(i + 1) % q.size()]);
	return p;
}
struct MapTriangle
{
	const BakedSurface *surface = nullptr;
	uint32_t slot = 0;
	LightmapUv uv[3];
	D3 p[3], n[3];
	bool patch = false;
	uint32_t patchX = 0, patchY = 0;
	double determinant = 0;
	double basisError = 0, basisOffset = 0;
	bool basisKnown = false;
	bool duplicate = false;
	bool multipleReferences = false;
};
struct Evaluation
{
	D3 p, du, dv, normal, shading;
};
void weights(double t, double *b)
{
	b[0] = (1 - t) * (1 - t);
	b[1] = 2 * t * (1 - t);
	b[2] = t * t;
}
Evaluation evaluate(const MapTriangle &t, LightmapUv uv)
{
	Evaluation e;
	const auto a = sub(t.uv[1], t.uv[0]), b = sub(t.uv[2], t.uv[0]), q = sub(uv, t.uv[0]);
	double s = wedge(q, b) / t.determinant, r = wedge(a, q) / t.determinant;
	if (!t.patch)
	{
		e.p = t.p[0] + (t.p[1] - t.p[0]) * s + (t.p[2] - t.p[0]) * r;
		e.du = ((t.p[1] - t.p[0]) * b.v - (t.p[2] - t.p[0]) * a.v) * (1 / t.determinant);
		e.dv = ((t.p[2] - t.p[0]) * a.u - (t.p[1] - t.p[0]) * b.u) * (1 / t.determinant);
		e.shading = unit(t.n[0] * (1 - s - r) + t.n[1] * s + t.n[2] * r);
	}
	else
	{
		const auto &surf = *t.surface;
		const auto &v = surf.vertices;
		const size_t base = size_t(t.patchY) * surf.patchWidth + t.patchX;
		const auto start = v[base].lightmap[t.slot];
		const auto u = sub(v[base + 2].lightmap[t.slot], start),
				   w = sub(v[base + 2 * surf.patchWidth].lightmap[t.slot], start);
		const auto d = sub(uv, start);
		double det = wedge(u, w);
		s = wedge(d, w) / det;
		r = wedge(u, d) / det;
		double bu[3], bv[3];
		weights(s, bu);
		weights(r, bv);
		D3 ps, pr, shade;
		const D3 anchor(v[base].position);
		e.p = anchor;
		for (size_t y = 0; y < 3; ++y)
			for (size_t x = 0; x < 3; ++x)
			{
				const auto &cp = v[base + y * surf.patchWidth + x];
				e.p = e.p + (D3(cp.position) - anchor) * (bu[x] * bv[y]);
				shade = shade + D3(cp.shadingNormal) * (bu[x] * bv[y]);
			}
		// First differences avoid cancellation of large absolute world coordinates.
		// A collapsed control edge then has EXACTLY zero transverse derivative.
		for (size_t y = 0; y < 3; ++y)
			for (size_t x = 0; x < 2; ++x)
				ps = ps + (D3(v[base + y * surf.patchWidth + x + 1].position) -
						   D3(v[base + y * surf.patchWidth + x].position)) *
							  (2 * (x ? s : 1 - s) * bv[y]);
		for (size_t y = 0; y < 2; ++y)
			for (size_t x = 0; x < 3; ++x)
				pr = pr + (D3(v[base + (y + 1) * surf.patchWidth + x].position) -
						   D3(v[base + y * surf.patchWidth + x].position)) *
							  (2 * (y ? r : 1 - r) * bu[x]);
		e.du = (ps * w.v - pr * u.v) * (1 / det);
		e.dv = (pr * u.u - ps * w.u) * (1 / det);
		e.shading = unit(shade);
	}
	e.normal = unit(cross(e.du, e.dv));
	return e;
}
bool contains(const MapTriangle &t, LightmapUv p, double eps)
{
	auto a = sub(t.uv[1], t.uv[0]), b = sub(t.uv[2], t.uv[0]), q = sub(p, t.uv[0]);
	double u = wedge(q, b) / t.determinant, v = wedge(a, q) / t.determinant;
	return u >= -eps && v >= -eps && u + v <= 1 + eps;
}
double jacobian(const MapTriangle &t, LightmapUv p)
{
	auto e = evaluate(t, p);
	return length(cross(e.du, e.dv));
}
double quadrature(const MapTriangle &t, LightmapUv a, LightmapUv b, LightmapUv c)
{
	// Degree-5 Dunavant quadrature. Positive weights, exact affine area.
	const double ar = std::abs(wedge(sub(b, a), sub(c, a))) * .5;
	double sum = .225 * jacobian(t, mul(add(add(a, b), c), 1. / 3));
	for (int set = 0; set < 2; ++set)
	{
		double x = set ? .797426985353087 : .059715871789770,
			   y = set ? .101286507323456 : .470142064105115,
			   w = set ? .125939180544827 : .132394152788506;
		sum += w * (jacobian(t, add(add(mul(a, x), mul(b, y)), mul(c, y))) +
					jacobian(t, add(add(mul(a, y), mul(b, x)), mul(c, y))) +
					jacobian(t, add(add(mul(a, y), mul(b, y)), mul(c, x))));
	}
	return ar * sum;
}
struct Budget
{
	const MappingOptions &options;
	const ReadContext &context;
	uint64_t work = 0;
	bool exceeded = false;
	Budget(const MappingOptions &o, const ReadContext &c) : options(o), context(c) {}
	bool step(uint64_t n = 1)
	{
		if (n > options.maxWork - work)
		{
			exceeded = true;
			return false;
		}
		work += n;
		return !(context.cancellation && context.cancellation->IsCancelled());
	}
};
double integrate(const MapTriangle &t, LightmapUv a, LightmapUv b, LightmapUv c,
				 const MappingOptions &o, unsigned depth, double &error, Budget &budget)
{
	if (!budget.step(5))
		return 0;
	double coarse = quadrature(t, a, b, c);
	auto ab = mul(add(a, b), .5), bc = mul(add(b, c), .5), ca = mul(add(c, a), .5);
	double fine = quadrature(t, a, ab, ca) + quadrature(t, ab, b, bc) + quadrature(t, ca, bc, c) +
				  quadrature(t, ab, bc, ca);
	double delta = std::abs(fine - coarse);
	if (depth == o.areaMaxDepth || delta <= o.areaRelativeTolerance * std::max(fine, 1e-20))
	{
		error += delta;
		return fine;
	}
	return integrate(t, a, ab, ca, o, depth + 1, error, budget) +
		   integrate(t, ab, b, bc, o, depth + 1, error, budget) +
		   integrate(t, ca, bc, c, o, depth + 1, error, budget) +
		   integrate(t, ab, bc, ca, o, depth + 1, error, budget);
}
bool patchAffine(const BakedSurface &s, uint32_t slot, uint32_t px, uint32_t py, double tolerance)
{
	size_t base = size_t(py) * s.patchWidth + px;
	const auto a = s.vertices[base].lightmap[slot];
	auto u = sub(s.vertices[base + 2].lightmap[slot], a),
		 v = sub(s.vertices[base + 2 * s.patchWidth].lightmap[slot], a);
	if (std::abs(wedge(u, v)) < 1e-14)
		return false;
	for (size_t y = 0; y < 3; ++y)
		for (size_t x = 0; x < 3; ++x)
		{
			auto expected = add(a, add(mul(u, double(x) / 2), mul(v, double(y) / 2))),
				 actual = s.vertices[base + y * s.patchWidth + x].lightmap[slot];
			if (std::abs(expected.u - actual.u) > tolerance ||
				std::abs(expected.v - actual.v) > tolerance)
				return false;
		}
	return true;
}
std::vector<double> key(const MapTriangle &t)
{ // canonical geometry/UV ordering, not vertex/triangle indices
	std::vector<std::vector<double>> v;
	for (size_t i = 0; i < 3; ++i)
		v.push_back({t.uv[i].u, t.uv[i].v, t.p[i].x, t.p[i].y, t.p[i].z});
	std::sort(v.begin(), v.end());
	std::vector<double> out;
	for (auto &a : v)
		out.insert(out.end(), a.begin(), a.end());
	// Analytic patch pieces with equal corners may have DIFFERENT interiors.
	if (t.patch)
	{
		out.push_back(-999);
		const auto &s = *t.surface;
		for (size_t y = 0; y < 3; ++y)
			for (size_t x = 0; x < 3; ++x)
			{
				auto p = s.vertices[(t.patchY + y) * s.patchWidth + t.patchX + x].position;
				out.push_back(p.x);
				out.push_back(p.y);
				out.push_back(p.z);
			}
	}
	return out;
}
struct Piece
{
	Polygon polygon;
	size_t triangle = 0;
	Piece(Polygon p, size_t t) : polygon(std::move(p)), triangle(t) {}
};
bool validSurface(const BakedSurface &s)
{
	if (s.kind == BakedSurfaceKind::Unsupported)
		return true;
	if (s.vertices.empty())
		return false;
	for (const auto &v : s.vertices)
	{
		if (!finite(v.position))
			return false;
		for (size_t slot = 0; slot < 4; ++slot)
		{
			if (s.lightmap[slot] < 0 || s.styles[slot] == 255)
				continue;
			auto uv = v.lightmap[slot];
			if (!std::isfinite(uv.u) || !std::isfinite(uv.v) || std::abs(uv.u) > 1e6 ||
				std::abs(uv.v) > 1e6)
				return false;
		}
	}
	for (auto i : s.indices)
		if (i >= s.vertices.size())
			return false;
	if (s.kind == BakedSurfaceKind::Patch)
		return s.patchWidth >= 3 && s.patchHeight >= 3 && s.patchWidth % 2 && s.patchHeight % 2 &&
			   uint64_t(s.patchWidth) * s.patchHeight == s.vertices.size();
	return s.indices.size() % 3 == 0;
}
} // namespace
void SurfaceCapture::Reset() noexcept
{
	staged_.reset();
	std::atomic_store(&published_, std::shared_ptr<const SurfaceGeometry>());
	report_ = SurfaceReport();
}
void SurfaceCapture::Begin(BuildPolicy p, bool hit)
{
	Reset();
	report_.state = NeedLightmapObservations(p, hit)
						? CaptureState::Collecting
						: hit && p == BuildPolicy::CacheOrBuild ? CaptureState::CacheHit
																: CaptureState::NotRequested;
}
Error SurfaceCapture::Fail(Code c, const std::string &message)
{
	staged_.reset();
	std::atomic_store(&published_, std::shared_ptr<const SurfaceGeometry>());
	report_.state = CaptureState::Failed;
	report_.bytes = 0;
	report_.message = message;
	return Failure(c, message, "surfaces");
}
Error SurfaceCapture::SetMap(const LightmapMapIdentity &m)
{
	if (!Needed() || staged_)
		return Failure(Code::InvalidState, "Surface map not writable");
	if (m.path.empty() || m.path.size() > limits_.maxPathBytes || !m.bytes)
		return Fail(Code::InvalidInput, "Invalid surface map identity");
	try
	{
		staged_.reset(new SurfaceGeometry());
		staged_->map_ = m;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "Surface map allocation failed");
	}
}
Error SurfaceCapture::Add(BakedSurface s)
{
	if (!Needed() || !staged_)
		return Failure(Code::InvalidState, "Surface geometry not writable");
	if (s.ordinal != staged_->surfaces_.size())
		return Fail(Code::InvalidInput, "Surface ordinals must be complete BSP order");
	s.materialFacts.uncertainty = MaterialUncertaintyFlags(s.materialFacts);
	uint64_t bytes = sizeof(s) + s.material.size() + s.materialFacts.baseTexture.size() +
					 s.materialFacts.emissionTexture.size() +
					 uint64_t(s.vertices.size()) * sizeof(BakedVertex) +
					 uint64_t(s.indices.size()) * 4;
	if (staged_->surfaces_.size() >= limits_.maxSurfaces ||
		s.material.size() > limits_.maxPathBytes ||
		s.vertices.size() > limits_.maxVertices - report_.vertices ||
		s.indices.size() > limits_.maxIndices - report_.indices ||
		bytes > limits_.maxBytes - report_.bytes)
		return Fail(Code::MemoryLimit, "Surface geometry exceeds capture limits");
	for (size_t slot = 0; slot < 4; ++slot)
	{
		bool valid = !s.vertices.empty();
		LightmapUv low(1e10, 1e10), high(-1e10, -1e10);
		for (const auto &v : s.vertices)
		{
			auto uv = v.lightmap[slot];
			if (!std::isfinite(uv.u) || !std::isfinite(uv.v) || std::abs(uv.u) > 1e6 ||
				std::abs(uv.v) > 1e6)
			{
				valid = false;
				break;
			}
			low.u = std::min(low.u, uv.u);
			low.v = std::min(low.v, uv.v);
			high.u = std::max(high.u, uv.u);
			high.v = std::max(high.v, uv.v);
		}
		s.uvBoundsKnown[slot] = valid;
		if (valid)
		{
			s.uvMin[slot] = low;
			s.uvMax[slot] = high;
		}
	}
	if (!validSurface(s))
	{
		s.kind = BakedSurfaceKind::Unsupported;
		++report_.malformed;
		std::vector<BakedVertex>().swap(s.vertices);
		std::vector<uint32_t>().swap(s.indices);
	}
	report_.vertices += s.vertices.size();
	report_.indices += s.indices.size();
	report_.bytes += bytes;
	if (s.kind == BakedSurfaceKind::Face)
		++report_.faces;
	if (s.kind == BakedSurfaceKind::Patch)
		++report_.patches;
	if (s.kind == BakedSurfaceKind::Triangles)
		++report_.triangles;
	if (s.lightmap[0] < 0)
		++report_.vertexLit;
	try
	{
		staged_->surfaces_.push_back(std::move(s));
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "Surface capture allocation failed");
	}
}
Error SurfaceCapture::Material(uint32_t ordinal, bool deform, bool excluded)
{
	if (!Needed() || !staged_ || ordinal >= staged_->surfaces_.size())
		return Failure(Code::InvalidState, "Invalid surface material update");
	auto &s = staged_->surfaces_[ordinal];
	if (deform)
		s.motion = SurfaceMotion::Deformed;
	s.excludedMaterial = excluded;
	return Error();
}
Error SurfaceCapture::Material(uint32_t ordinal, const MaterialFacts &facts)
{
	if (!Needed() || !staged_ || ordinal >= staged_->surfaces_.size())
		return Failure(Code::InvalidState, "Invalid surface material update");
	auto &s = staged_->surfaces_[ordinal];
	const uint64_t oldBytes =
		s.materialFacts.baseTexture.size() + s.materialFacts.emissionTexture.size();
	const uint64_t newBytes = facts.baseTexture.size() + facts.emissionTexture.size();
	if (facts.baseTexture.size() > limits_.maxPathBytes ||
		facts.emissionTexture.size() > limits_.maxPathBytes ||
		newBytes > limits_.maxBytes - (report_.bytes - oldBytes))
		return Fail(Code::MemoryLimit, "Material evidence exceeds capture limits");
	try
	{
		s.materialFacts = facts;
		s.materialFacts.uncertainty = MaterialUncertaintyFlags(facts);
		if (facts.signals & MaterialDeformed)
			s.motion = SurfaceMotion::Deformed;
		s.excludedMaterial =
			(facts.signals & (MaterialMissing | MaterialSky | MaterialNoDraw)) != 0;
		report_.bytes = report_.bytes - oldBytes + newBytes;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "Material evidence allocation failed");
	}
}
Error SurfaceCapture::VertexHdr(VertexHdrState state, const std::string &source, const void *data,
								size_t bytes, uint64_t sourceVertices)
{
	if (!Needed() || !staged_ || staged_->hdrState_ != VertexHdrState::NotRequested)
		return Failure(Code::InvalidState, "Vertex HDR capture not writable");
	if (source.size() > limits_.maxPathBytes || source.size() > limits_.maxBytes - report_.bytes)
		return Fail(Code::MemoryLimit, "Vertex HDR source exceeds capture budget");
	try
	{
		staged_->hdrSource_ = source;
		staged_->hdrState_ = state;
		report_.bytes += source.size();
		if (state != VertexHdrState::Present)
			return Error();
		if (!data || sourceVertices > limits_.maxVertices ||
			sourceVertices > SIZE_MAX / (3 * sizeof(float)) ||
			bytes != sourceVertices * 3 * sizeof(float))
		{
			staged_->hdrState_ = VertexHdrState::Invalid;
			return Failure(Code::InvalidInput, "Vertex HDR size does not match original drawVerts");
		}
		if (bytes > limits_.maxBytes - report_.bytes)
			return Fail(Code::MemoryLimit, "Vertex HDR payload exceeds capture budget");
		staged_->vertexHdr_.resize(bytes / sizeof(float));
		if (bytes)
			std::memcpy(staged_->vertexHdr_.data(), data, bytes);
		staged_->hdrHash_ = Disk::Hash(data, bytes);
		report_.bytes += bytes;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "Vertex HDR allocation failed");
	}
}
Error SurfaceCapture::Freeze()
{
	if (!Needed())
		return Failure(Code::InvalidState, "Surface capture is not writable");
	if (!staged_)
		return Fail(Code::InvalidState, "Surface source unavailable");
	for (const auto &s : staged_->surfaces_)
		if (s.motion != SurfaceMotion::StaticWorld || s.excludedMaterial)
			++report_.excluded;
	report_.state = CaptureState::Captured;
	staged_->report_ = report_;
	try
	{
		std::shared_ptr<const SurfaceGeometry> p(std::move(staged_));
		std::atomic_store(&published_, p);
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "Surface publication failed");
	}
}
Error MapLuxels(std::shared_ptr<const SurfaceGeometry> geometry,
				std::shared_ptr<const LightmapObservations> images, size_t image, uint32_t x,
				uint32_t y, uint32_t width, uint32_t height, const MappingOptions &o,
				const ReadContext &context, LuxelTile &out)
{
	out = LuxelTile();
	if (!geometry || !images)
		return Failure(Code::Unavailable, "Missing original geometry/images");
	if (geometry->Map().sha256 != images->Map().sha256 ||
		geometry->Map().path != images->Map().path)
		return Failure(Code::InvalidInput, "Geometry/image map identities differ");
	if (image >= images->Images().size())
		return Failure(Code::InvalidInput, "Image ordinal outside observations");
	const auto &im = images->Images()[image];
	if (im.missing || im.role != LightmapRole::Lighting)
		return Failure(Code::Unavailable, "Missing image or direction-only deluxe image");
	if (!width || !height || x > im.width || width > im.width - x || y > im.height ||
		height > im.height - y)
		return Failure(Code::InvalidInput, "Invalid source image tile");
	if (!std::isfinite(o.positionToleranceWorld) || o.positionToleranceWorld <= 0 ||
		!std::isfinite(o.uvTolerance) || o.uvTolerance <= 0 || o.uvTolerance > 1e-3 ||
		!std::isfinite(o.areaRelativeTolerance) || o.areaRelativeTolerance <= 0 ||
		o.areaMaxDepth > 8)
		return Failure(Code::InvalidInput, "Invalid mapping tolerances");
	uint64_t count = uint64_t(width) * height;
	if (count > o.maxTexels || count > SIZE_MAX / sizeof(SurfaceLightingObservation) ||
		count * sizeof(SurfaceLightingObservation) > context.maxBytes)
		return Failure(Code::MemoryLimit, "Luxel tile exceeds output budget");
	if (count > o.maxScratchBytes / 64)
		return Failure(Code::MemoryLimit, "Luxel tile scratch budget exceeded");
	if (context.cancellation && context.cancellation->IsCancelled())
		return Failure(Code::Cancelled, "Luxel mapping cancelled");
	try
	{
		LuxelTile tile;
		tile.geometry = geometry;
		tile.images = images;
		tile.image = image;
		tile.x = x;
		tile.y = y;
		tile.width = width;
		tile.height = height;
		tile.samples.resize(size_t(count));
		Budget budget{o, context};
		uint64_t scratch = count * 64;
		bool scratchExceeded = false;
		std::vector<MapTriangle> triangles;
		std::map<std::vector<double>, size_t> unique;
		std::map<std::pair<uint32_t, uint32_t>, std::pair<double, double>> bases;
		std::vector<bool> unsupported(static_cast<size_t>(count), false);
		auto unsupportedBox = [&](double u0, double v0, double u1, double v1) {
			double a = std::max(double(x), std::floor(u0 * im.width)),
				   b = std::min(double(x) + width, std::ceil(u1 * im.width));
			double c = std::max(double(y), std::floor(v0 * im.height)),
				   d = std::min(double(y) + height, std::ceil(v1 * im.height));
			if (a >= b || c >= d)
				return;
			for (uint32_t yy = uint32_t(c); yy < uint32_t(d); ++yy)
				for (uint32_t xx = uint32_t(a); xx < uint32_t(b); ++xx)
				{
					if (!budget.step())
						return;
					unsupported[size_t(yy - y) * width + xx - x] = true;
				}
		};
		auto append = [&](MapTriangle t) {
			if (!budget.step())
				return;
			if (o.maxScratchBytes - scratch < 2048)
			{
				scratchExceeded = true;
				return;
			}
			t.determinant = wedge(sub(t.uv[1], t.uv[0]), sub(t.uv[2], t.uv[0]));
			if (std::abs(t.determinant) < 1e-14 ||
				(!t.patch && length(cross(t.p[1] - t.p[0], t.p[2] - t.p[0])) < 1e-12))
			{
				++tile.report.degenerate;
				return;
			}
			auto canonical = key(t);
			auto inserted = unique.emplace(std::move(canonical), triangles.size());
			if (!inserted.second)
			{
				auto &original = triangles[inserted.first->second];
				original.duplicate = true;
				original.multipleReferences = original.multipleReferences ||
											  original.surface->ordinal != t.surface->ordinal ||
											  original.slot != t.slot;
				++tile.report.duplicates;
				return;
			}
			if (triangles.size() >= o.maxTriangles)
			{
				budget.exceeded = true;
				return;
			}
			if (t.surface->kind == BakedSurfaceKind::Face)
			{
				auto id = std::make_pair(t.surface->ordinal, t.slot);
				auto found = bases.find(id);
				if (found == bases.end())
				{
					const auto &s = *t.surface;
					double best = std::numeric_limits<double>::infinity(), chosen = 0;
					for (double half : {0., .5})
					{
						double maximum = 0;
						for (const auto &v : s.vertices)
						{
							if (!budget.step())
								return;
							auto uv = v.lightmap[t.slot];
							D3 expected =
								D3(s.lightmapOrigin) +
								D3(s.lightmapBasis[0]) * (uv.u * 128 - s.chartX[t.slot] - half) +
								D3(s.lightmapBasis[1]) * (uv.v * 128 - s.chartY[t.slot] - half);
							double error = length(expected - D3(v.position));
							if (!std::isfinite(error))
							{
								maximum = std::numeric_limits<double>::infinity();
								break;
							}
							maximum = std::max(maximum, error);
						}
						if (maximum < best)
						{
							best = maximum;
							chosen = half;
						}
					}
					found = bases.emplace(id, std::make_pair(best, chosen)).first;
				}
				t.basisKnown = std::isfinite(found->second.first);
				t.basisError = t.basisKnown ? found->second.first : 0;
				t.basisOffset = found->second.second;
			}
			triangles.push_back(t);
			scratch += 2048;
		};
		for (const auto &s : geometry->Surfaces())
		{
			if (!budget.step())
				break;
			if (s.motion != SurfaceMotion::StaticWorld || s.excludedMaterial)
			{
				++tile.report.excluded;
				continue;
			}
			if (s.kind == BakedSurfaceKind::Unsupported)
			{
				++tile.report.unsupported;
				for (size_t slot = 0; slot < 4; ++slot)
					if (s.lightmap[slot] >= 0 && uint32_t(s.lightmap[slot]) == im.logicalIndex &&
						s.styles[slot] != 255)
					{
						if (s.uvBoundsKnown[slot])
							unsupportedBox(s.uvMin[slot].u, s.uvMin[slot].v, s.uvMax[slot].u,
										   s.uvMax[slot].v);
						else
							unsupportedBox(0, 0, 1, 1);
					}
				continue;
			}
			for (uint32_t slot = 0; slot < 4; ++slot)
			{
				if (s.lightmap[slot] < 0 || uint32_t(s.lightmap[slot]) != im.logicalIndex ||
					s.styles[slot] == 255)
					continue;
				if (s.kind == BakedSurfaceKind::Patch)
				{
					for (uint32_t py = 0; py + 2 < s.patchHeight; py += 2)
						for (uint32_t px = 0; px + 2 < s.patchWidth; px += 2)
						{
							if (!patchAffine(s, slot, px, py, o.uvTolerance))
							{
								++tile.report.unsupportedPatchUv;
								double u0 = 1e10, v0 = 1e10, u1 = -1e10, v1 = -1e10;
								for (uint32_t yy = 0; yy < 3; ++yy)
									for (uint32_t xx = 0; xx < 3; ++xx)
									{
										auto uv =
											s.vertices[size_t(py + yy) * s.patchWidth + px + xx]
												.lightmap[slot];
										u0 = std::min(u0, uv.u);
										v0 = std::min(v0, uv.v);
										u1 = std::max(u1, uv.u);
										v1 = std::max(v1, uv.v);
									}
								unsupportedBox(u0, v0, u1, v1);
								continue;
							}
							++tile.report.patchSegments;
							const size_t base = size_t(py) * s.patchWidth + px;
							const size_t idx[4] = {base, base + 2, base + 2 * s.patchWidth,
												   base + 2 * s.patchWidth + 2};
							for (auto corner :
								 {std::array<int, 3>{{0, 1, 2}}, std::array<int, 3>{{1, 3, 2}}})
							{
								MapTriangle t;
								t.surface = &s;
								t.slot = slot;
								t.patch = true;
								t.patchX = px;
								t.patchY = py;
								for (size_t j = 0; j < 3; ++j)
								{
									auto &v = s.vertices[idx[size_t(corner[j])]];
									t.uv[j] = v.lightmap[slot];
									t.p[j] = v.position;
									t.n[j] = v.shadingNormal;
								}
								append(t);
							}
						}
				}
				else
					for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
					{
						MapTriangle t;
						t.surface = &s;
						t.slot = slot;
						for (size_t j = 0; j < 3; ++j)
						{
							auto &v = s.vertices[s.indices[i + j]];
							t.uv[j] = v.lightmap[slot];
							t.p[j] = v.position;
							t.n[j] = v.shadingNormal;
						}
						append(t);
					}
			}
		}
		if (budget.exceeded)
			return Failure(Code::WorkLimit, "Luxel triangle/work budget exceeded");
		if (scratchExceeded)
			return Failure(Code::MemoryLimit, "Luxel triangle scratch budget exceeded");
		std::map<std::vector<double>, unsigned> edges;
		auto edgeKey = [&](const MapTriangle &t, size_t edge) {
			std::vector<double> a = {t.uv[edge].u, t.uv[edge].v, t.p[edge].x, t.p[edge].y,
									 t.p[edge].z},
								b = {t.uv[(edge + 1) % 3].u, t.uv[(edge + 1) % 3].v,
									 t.p[(edge + 1) % 3].x, t.p[(edge + 1) % 3].y,
									 t.p[(edge + 1) % 3].z};
			if (b < a)
				std::swap(a, b);
			a.insert(a.end(), b.begin(), b.end());
			a.push_back(t.surface->ordinal);
			a.push_back(t.slot); // chart borders remain borders across adjacent surfaces
			return a;
		};
		for (const auto &t : triangles)
			for (size_t edge = 0; edge < 3; ++edge)
				++edges[edgeKey(t, edge)];
		std::vector<std::vector<Piece>> pieces(static_cast<size_t>(count));
		size_t pieceCount = 0;
		for (size_t ti = 0; ti < triangles.size(); ++ti)
		{
			const auto &t = triangles[ti];
			double loU = std::min({t.uv[0].u, t.uv[1].u, t.uv[2].u}) * im.width,
				   hiU = std::max({t.uv[0].u, t.uv[1].u, t.uv[2].u}) * im.width;
			double loV = std::min({t.uv[0].v, t.uv[1].v, t.uv[2].v}) * im.height,
				   hiV = std::max({t.uv[0].v, t.uv[1].v, t.uv[2].v}) * im.height;
			if (hiU <= x || loU >= double(x) + width || hiV <= y || loV >= double(y) + height)
				continue;
			uint32_t left = uint32_t(std::max(double(x), std::floor(loU))),
					 right = uint32_t(std::min(double(x) + width, std::ceil(hiU)));
			uint32_t bottom = uint32_t(std::max(double(y), std::floor(loV))),
					 top = uint32_t(std::min(double(y) + height, std::ceil(hiV)));
			Polygon p = {t.uv[0], t.uv[1], t.uv[2]};
			if (t.determinant < 0)
				std::swap(p[1], p[2]);
			for (uint32_t yy = bottom; yy < top; ++yy)
				for (uint32_t xx = left; xx < right; ++xx)
				{
					if (!budget.step())
						break;
					Polygon box = {{double(xx) / im.width, double(yy) / im.height},
								   {double(xx + 1) / im.width, double(yy) / im.height},
								   {double(xx + 1) / im.width, double(yy + 1) / im.height},
								   {double(xx) / im.width, double(yy + 1) / im.height}};
					auto clipped = intersect(p, box);
					if (area(clipped) < 1e-16)
						continue;
					if (++pieceCount > o.maxPieces)
						return Failure(Code::MemoryLimit, "Luxel footprint piece budget exceeded");
					if (o.maxScratchBytes - scratch < 1024)
						return Failure(Code::MemoryLimit,
									   "Luxel footprint scratch budget exceeded");
					scratch += 1024;
					pieces[size_t(yy - y) * width + xx - x].push_back({std::move(clipped), ti});
				}
		}
		const double texelUvArea = 1. / (double(im.width) * im.height);
		for (size_t i = 0; i < tile.samples.size(); ++i)
		{
			auto &sample = tile.samples[i];
			sample.x = x + uint32_t(i % width);
			sample.y = y + uint32_t(i / width);
			sample.logicalLightmap = im.logicalIndex;
			sample.image = image;
			sample.centreUv = {(sample.x + .5) / im.width, (sample.y + .5) / im.height};
			sample.footprintMin = {double(sample.x) / im.width, double(sample.y) / im.height};
			sample.footprintMax = {double(sample.x + 1) / im.width,
								   double(sample.y + 1) / im.height};
			bool overlap = false, centre = false, mixed = false, normalSeam = false;
			const MapTriangle *selected = nullptr;
			for (size_t j = 0; j < pieces[i].size(); ++j)
			{
				const auto &piece = pieces[i][j];
				const auto &t = triangles[piece.triangle];
				if (t.duplicate)
					sample.issues |= LuxelDuplicate;
				if (t.multipleReferences)
					sample.issues |= LuxelMultipleCharts;
				if (!selected)
					selected = &t;
				for (size_t k = 0; k < j; ++k)
				{
					if (!budget.step())
						break;
					double shared = area(intersect(piece.polygon, pieces[i][k].polygon));
					if (shared > texelUvArea * 1e-7)
						overlap = true;
					const auto &other = triangles[pieces[i][k].triangle];
					if (other.surface->ordinal != t.surface->ordinal || other.slot != t.slot)
					{
						sample.issues |= LuxelMultipleCharts;
						auto a = evaluate(other, sample.centreUv), b = evaluate(t, sample.centreUv);
						if (length(a.p - b.p) > o.positionToleranceWorld ||
							std::abs(dot(a.normal, b.normal)) < 1 - 1e-6)
							mixed = true;
					}
				}
				sample.coveredFraction += area(piece.polygon) / texelUvArea;
				if (t.patch)
				{
					sample.issues |= LuxelAreaApproximate;
					for (size_t k = 1; k + 1 < piece.polygon.size(); ++k)
						sample.areaWorldSquared +=
							integrate(t, piece.polygon[0], piece.polygon[k], piece.polygon[k + 1],
									  o, 0, sample.areaErrorEstimate, budget);
				}
				else
					sample.areaWorldSquared += area(piece.polygon) * jacobian(t, piece.polygon[0]);
				for (size_t edge = 0; edge < 3; ++edge)
					if (edges[edgeKey(t, edge)] == 1)
					{
						auto a = t.uv[edge], b = t.uv[(edge + 1) % 3];
						double first = 0, last = 1;
						for (size_t axis = 0; axis < 2; ++axis)
						{
							double av = axis ? a.v : a.u, bv = axis ? b.v : b.u,
								   low = axis ? sample.footprintMin.v : sample.footprintMin.u,
								   high = axis ? sample.footprintMax.v : sample.footprintMax.u;
							if (std::abs(bv - av) < 1e-15)
							{
								if (av < low || av > high)
									last = -1;
							}
							else
							{
								double lo = (low - av) / (bv - av), hi = (high - av) / (bv - av);
								if (lo > hi)
									std::swap(lo, hi);
								first = std::max(first, lo);
								last = std::min(last, hi);
							}
						}
						if (last >= first && last >= 0 && first <= 1)
							sample.issues |= LuxelBorder;
					}
				if (contains(t, sample.centreUv, o.uvTolerance))
				{
					if (!centre)
						selected = &t;
					else
					{
						auto a = evaluate(*selected, sample.centreUv),
							 b = evaluate(t, sample.centreUv);
						if (length(a.normal) < .5 || length(b.normal) < .5)
							sample.issues |= LuxelDegenerate;
						if (length(a.p - b.p) > o.positionToleranceWorld)
							mixed = true;
						if (std::abs(dot(a.normal, b.normal)) < 1 - 1e-6)
							normalSeam = true;
					}
					centre = true;
				}
			}
			if (unsupported[i])
			{
				sample.coverage = LuxelCoverage::Unsupported;
				sample.issues |= LuxelUnsupported;
				sample.areaWorldSquared = 0;
				++tile.report.unsupportedTexels;
				continue;
			}
			if (!selected)
			{
				++tile.report.padding;
				continue;
			}
			sample.surface = selected->surface->ordinal;
			sample.styleSlot = selected->slot;
			sample.materialIndex = selected->surface->materialIndex;
			if (sample.coveredFraction < 1 - 1e-6)
			{
				sample.issues |= LuxelBorder;
			}
			if (sample.issues & LuxelBorder)
				++tile.report.borders;
			if (overlap || mixed || normalSeam)
			{
				sample.coverage = LuxelCoverage::Ambiguous;
				if (overlap)
					sample.issues |= LuxelOverlap;
				if (mixed)
					sample.issues |= LuxelMixedFootprint;
				if (normalSeam)
					sample.issues |= LuxelNormalSeam;
				sample.areaWorldSquared = 0;
				++tile.report.ambiguous;
				continue;
			}
			if (!centre)
			{
				sample.coverage = LuxelCoverage::FootprintOnly;
				++tile.report.footprintOnly;
				continue;
			}
			auto e = evaluate(*selected, sample.centreUv);
			if (!finite(e.p) || !finite(e.normal) || length(e.normal) < .5 ||
				!std::isfinite(sample.areaWorldSquared) || sample.areaWorldSquared <= 0)
			{
				sample.coverage = LuxelCoverage::Ambiguous;
				sample.issues |= LuxelDegenerate;
				sample.areaWorldSquared = 0;
				++tile.report.ambiguous;
				continue;
			}
			sample.shadingNormalKnown = length(e.shading) > .5;
			sample.shadingNormal = e.shading.value();
			D3 orientation = e.shading;
			if (!sample.shadingNormalKnown && selected->surface->kind == BakedSurfaceKind::Face)
				orientation = unit(D3(selected->surface->lightmapBasis[2]));
			sample.normalOrientationKnown = finite(orientation) && length(orientation) > .5 &&
											std::abs(dot(e.normal, orientation)) > 1e-4;
			if (sample.normalOrientationKnown && dot(e.normal, orientation) < 0)
			{
				e.normal = e.normal * (-1);
				sample.issues |= LuxelWindingReversed;
			}
			if (!sample.shadingNormalKnown)
				sample.issues |= LuxelShadingMissing;
			if (!sample.normalOrientationKnown)
				sample.issues |= LuxelOrientationUnknown;
			sample.positionWorld = e.p.value();
			sample.geometricNormal = e.normal.value();
			sample.tangentWorld = unit(e.du).value();
			sample.mappingErrorBoundWorld = o.positionToleranceWorld;
			if (selected->surface->kind == BakedSurfaceKind::Face)
			{
				const auto &s = *selected->surface;
				// Validated once per source chart at triangle assembly, never fitted per texel.
				const double best = selected->basisError, chosen = selected->basisOffset;
				sample.basisErrorWorld = best;
				tile.report.maximumBasisError = std::max(tile.report.maximumBasisError, best);
				if (selected->basisKnown && best <= o.positionToleranceWorld)
				{
					D3 position = D3(s.lightmapOrigin) +
								  D3(s.lightmapBasis[0]) * (sample.centreUv.u * 128 -
															s.chartX[selected->slot] - chosen) +
								  D3(s.lightmapBasis[1]) *
									  (sample.centreUv.v * 128 - s.chartY[selected->slot] - chosen);
					if (length(position - e.p) <= o.positionToleranceWorld)
					{
						sample.positionWorld = position.value();
						sample.basisValidated = true;
					}
				}
				if (!sample.basisValidated)
					sample.issues |= LuxelBasisMismatch;
			}
			if (!finite(sample.positionWorld) ||
				length(D3(sample.positionWorld) - e.p) > o.positionToleranceWorld)
			{
				sample.positionWorld = Vec3();
				sample.coverage = LuxelCoverage::Ambiguous;
				sample.issues |= LuxelPrecisionExceeded;
				sample.areaWorldSquared = 0;
				++tile.report.ambiguous;
				continue;
			}
			sample.coverage = LuxelCoverage::Mapped;
			sample.positionKnown = true;
			++tile.report.mapped;
		}
		if (context.cancellation && context.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Luxel mapping cancelled");
		if (budget.exceeded)
			return Failure(Code::WorkLimit, "Luxel work budget exceeded");
		tile.report.work = budget.work;
		tile.report.triangles = triangles.size();
		out = std::move(tile);
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Luxel mapping allocation failed");
	}
}
Error DecodeLuxel(const Decode::Decoder &decoder, const LuxelTile &tile, size_t sample,
				  const ReadContext &ctx, Decode::LightmapSample &out)
{
	out = Decode::LightmapSample();
	if (!tile.images || !tile.geometry)
		return Failure(Code::Unavailable, "Luxel tile has no source leases");
	if (tile.image >= tile.images->Images().size())
		return Failure(Code::InvalidInput, "Luxel image ordinal outside source");
	if (sample >= tile.samples.size())
		return Failure(Code::InvalidInput, "Luxel ordinal outside tile");
	const auto &s = tile.samples[sample];
	const auto &image = tile.images->Images()[tile.image];
	if (s.x >= image.width || s.y >= image.height || s.logicalLightmap != image.logicalIndex)
		return Failure(Code::InvalidInput, "Luxel source coordinates/identity mismatch");
	if (s.coverage != LuxelCoverage::Mapped)
		return Failure(Code::Unavailable, "Luxel lacks an unambiguous surface centre");
	return Decode::Lightmap(decoder, tile.images, tile.image,
							uint64_t(s.y) * tile.images->Images()[tile.image].width + s.x, ctx,
							out);
}
Error LuxelRayOrigin(const SurfaceLightingObservation &s, Vec3 &out, float requested)
{
	if (!s.positionKnown)
		return Failure(Code::Unavailable, "Luxel has no world position");
	if (!s.normalOrientationKnown)
		return Failure(Code::Unavailable, "Luxel normal orientation is unknown");
	if (!std::isfinite(requested) || requested < 0)
		return Failure(Code::InvalidInput, "Invalid ray offset");
	if (!finite(s.positionWorld) || !finite(s.geometricNormal) ||
		std::abs(length(s.geometricNormal) - 1) > 1e-3 || !std::isfinite(s.areaWorldSquared) ||
		s.areaWorldSquared <= 0)
		return Failure(Code::InvalidInput, "Invalid luxel geometry for ray origin");
	double offset = std::min(double(requested), .01 * std::sqrt(std::max(0., s.areaWorldSquared)));
	out = (D3(s.positionWorld) + D3(s.geometricNormal) * offset).value();
	return Error();
}
Error ReadSurfaceObservation(const Decode::Decoder &decoder, const LuxelTile &tile, size_t sample,
							 const ReadContext &context, DecodedSurfaceObservation &out)
{
	out = DecodedSurfaceObservation();
	Decode::LightmapSample colour;
	auto error = DecodeLuxel(decoder, tile, sample, context, colour);
	if (!error)
		return error;
	out.surface = tile.samples[sample];
	out.geometry = tile.geometry;
	out.colour = std::move(colour);
	error = ClassifyLuxel(tile, sample, out.classification);
	if (!error)
		return error;
	if (out.colour.value.encoded.state == Decode::State::Invalid)
	{
		out.classification.diffuseFitCandidate = false;
		out.classification.reasons |= InvalidColourData;
	}
	return ReadLuxelDeluxe(tile, sample, context, out.deluxe);
}
Error LuxelDebugObj(const LuxelTile &tile, size_t maxSamples, std::string &out)
{
	if (tile.samples.size() > maxSamples)
		return Failure(Code::MemoryLimit, "Debug export sample budget exceeded");
	try
	{
		std::ostringstream text;
		text.imbue(std::locale::classic());
		text << std::setprecision(9)
			 << "# Original baked luxels. World units. Normals: receiver outward.\n";
		size_t index = 1;
		for (const auto &s : tile.samples)
		{
			text << "# luxel " << s.x << ' ' << s.y << " surface " << s.surface << " coverage "
				 << int(s.coverage) << " issues " << s.issues << " area " << s.areaWorldSquared
				 << '\n';
			if (!s.positionKnown)
				continue;
			auto end = D3(s.positionWorld) + D3(s.geometricNormal) * 2;
			text << "v " << s.positionWorld.x << ' ' << s.positionWorld.y << ' '
				 << s.positionWorld.z << "\nv " << end.x << ' ' << end.y << ' ' << end.z << "\np "
				 << index << "\nl " << index << ' ' << index + 1 << '\n';
			index += 2;
		}
		out = text.str();
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Debug export allocation failed");
	}
}
} // namespace Core
} // namespace Rend2LightRecon
