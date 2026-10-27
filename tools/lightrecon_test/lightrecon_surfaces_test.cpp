// CPU ground truth, independent of BSP parsing, atlas packing, renderer or camera.
#include "lightrecon/surfaces.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace
{
int checks = 0, failures = 0;
double maximumPositionError = 0, maximumAreaError = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                            \
		}                                                                                          \
	} while (0)
bool close(double a, double b, double epsilon = 1e-5)
{
	return std::abs(a - b) <= epsilon;
}
LightmapMapIdentity map()
{
	LightmapMapIdentity m;
	m.path = "maps/test.bsp";
	m.bytes = 256;
	m.sha256 = Disk::Hash("known geometry", 14);
	return m;
}
std::shared_ptr<const LightmapObservations> images(uint32_t w = 8, uint32_t h = 8)
{
	LightmapCapture c;
	c.Begin(BuildPolicy::ForceRebuild);
	uint8_t bsp[256] = {};
	CHECK(c.SetMap(map().path, bsp, 256));
	std::vector<uint8_t> pixels(size_t(w) * h * 4);
	for (size_t i = 0; i < pixels.size(); ++i)
		pixels[i] = static_cast<uint8_t>(i % 256);
	CHECK(c.Rgba8(3, LightmapRole::Lighting, "maps/test/lm_0003.tga", w, h, pixels.data(),
				  pixels.size()));
	CHECK(c.Freeze());
	return c.Acquire();
}
BakedSurface quad(uint32_t ordinal = 0)
{
	BakedSurface s;
	s.ordinal = ordinal;
	s.kind = BakedSurfaceKind::Face;
	s.motion = SurfaceMotion::StaticWorld;
	s.material = "textures/synthetic";
	s.lightmap[0] = 3;
	s.styles[0] = 0;
	s.materialIndex = 7;
	for (auto uv : {LightmapUv(0, 0), LightmapUv(1, 0), LightmapUv(0, 1), LightmapUv(1, 1)})
	{
		BakedVertex v;
		v.position = Vec3(float(uv.u * 16), float(uv.v * 8), 2);
		v.shadingNormal = Vec3(0, 0, 1);
		v.lightmap[0] = uv;
		s.vertices.push_back(v);
	}
	s.indices = {0, 1, 2, 1, 3, 2};
	s.lightmapOrigin = Vec3(0, 0, 2);
	s.lightmapBasis[0] = Vec3(16.f / 128, 0, 0);
	s.lightmapBasis[1] = Vec3(0, 8.f / 128, 0);
	s.lightmapBasis[2] = Vec3(0, 0, 1);
	return s;
}
std::shared_ptr<const SurfaceGeometry>
geometry(std::vector<BakedSurface> surfaces, const std::shared_ptr<const LightmapObservations> &im)
{
	SurfaceCapture c;
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(c.SetMap(im->Map()));
	for (auto &s : surfaces)
		CHECK(c.Add(std::move(s)));
	CHECK(c.Freeze());
	return c.Acquire();
}
LuxelTile run(std::vector<BakedSurface> surfaces, uint32_t w = 8, uint32_t h = 8)
{
	auto im = images(w, h);
	auto g = geometry(std::move(surfaces), im);
	ReadContext ctx;
	ctx.maxBytes = 32 * 1024 * 1024;
	LuxelTile t;
	CHECK(MapLuxels(g, im, 0, 0, 0, w, h, MappingOptions(), ctx, t));
	return t;
}
void planar()
{
	auto tile = run({quad()});
	CHECK(tile.samples.size() == 64);
	CHECK(tile.report.mapped == 64);
	CHECK(tile.report.ambiguous == 0);
	double area = 0;
	for (const auto &s : tile.samples)
	{
		CHECK(s.positionKnown);
		CHECK(close(s.positionWorld.x, (s.x + .5) * 2));
		CHECK(close(s.positionWorld.y, s.y + .5));
		CHECK(close(s.positionWorld.z, 2));
		CHECK(s.basisValidated);
		CHECK(close(s.coveredFraction, 1));
		CHECK(close(s.areaWorldSquared, 2));
		CHECK(close(s.geometricNormal.z, 1));
		CHECK(s.shadingNormalKnown);
		CHECK(s.surface == 0 && s.logicalLightmap == 3 && s.materialIndex == 7);
		area += s.areaWorldSquared;
		Vec3 ray;
		CHECK(LuxelRayOrigin(s, ray));
		CHECK(close(ray.z, 2 + .01 * std::sqrt(2.)));
		CHECK(close(s.positionWorld.z, 2));
	}
	CHECK(close(area, 128));
	auto halfBasis = quad();
	halfBasis.lightmapOrigin = Vec3(16.f / 256, 8.f / 256, 2);
	auto half = run({halfBasis});
	for (const auto &s : half.samples)
	{
		CHECK(s.basisValidated);
		CHECK(close(s.positionWorld.x, (s.x + .5) * 2));
		CHECK(close(s.positionWorld.y, s.y + .5));
	}
	Decode::Decoder decoder;
	Decode::LightmapSample colour;
	ReadContext context;
	context.maxBytes = 16;
	CHECK(DecodeLuxel(decoder, tile, 0, context, colour));
	CHECK(colour.value.raw.bytes[0] == 0);
	CHECK(colour.value.raw.bytes[1] == 1);
	CHECK(colour.value.source.state == Decode::State::Unavailable);
	DecodedSurfaceObservation complete;
	CHECK(ReadSurfaceObservation(decoder, tile, 0, context, complete));
	CHECK(complete.geometry == tile.geometry);
	CHECK(complete.colour.owner == tile.images);
	CHECK(complete.colour.value.raw.bytes[1] == 1);
	CHECK(complete.surface.positionKnown);
	CHECK(complete.geometry->Surfaces()[complete.surface.surface].material == "textures/synthetic");
	CHECK(!ReadSurfaceObservation(decoder, tile, tile.samples.size(), context, complete));
	CHECK(!complete.geometry);
	std::string obj;
	CHECK(LuxelDebugObj(tile, 64, obj));
	CHECK(obj.find("l 1 2") != std::string::npos);
	CHECK(!LuxelDebugObj(tile, 63, obj));
	// Different external dimensions sample the SAME original normalized chart.
	auto high = run({quad()}, 16, 4);
	CHECK(high.report.mapped == 64);
	area = 0;
	for (const auto &s : high.samples)
	{
		CHECK(close(s.positionWorld.x, s.x + .5));
		CHECK(close(s.positionWorld.y, (s.y + .5) * 2));
		area += s.areaWorldSquared;
	}
	CHECK(close(area, 128));
	// A hypothetical GPU atlas transform is absent from every canonical input.
	auto original = quad();
	auto rerun = run({original});
	for (size_t i = 0; i < 64; ++i)
	{
		double gpuU = (original.vertices[0].lightmap[0].u + 3) / 8;
		CHECK(gpuU != original.vertices[0].lightmap[0].u);
		CHECK(tile.samples[i].logicalLightmap == rerun.samples[i].logicalLightmap);
		CHECK(tile.samples[i].positionWorld.x == rerun.samples[i].positionWorld.x);
	}
}
void rotatedAndSeams()
{
	auto shade = quad();
	for (auto &v : shade.vertices)
		v.shadingNormal = Vec3(0, .6f, .8f);
	auto sh = run({shade});
	for (const auto &s : sh.samples)
	{
		CHECK(close(s.geometricNormal.z, 1));
		CHECK(close(s.shadingNormal.y, .6));
		CHECK(close(s.shadingNormal.z, .8));
	}
	auto q = quad();
	q.lightmapBasis[0] = Vec3();
	q.lightmapBasis[1] = Vec3();
	for (auto &v : q.vertices)
	{
		float old = v.position.x;
		v.position.x = v.position.z + 10;
		v.position.z = -old;
		v.shadingNormal = Vec3(1, 0, 0);
	}
	auto t = run({q});
	CHECK(t.report.mapped == 64);
	for (const auto &s : t.samples)
	{
		CHECK(close(s.positionWorld.x, 12));
		CHECK(close(s.positionWorld.z, -(s.x + .5) * 2));
		CHECK(close(s.geometricNormal.x, 1));
		CHECK(s.issues & LuxelBasisMismatch);
	}
	auto a = quad(0), b = quad(1);
	a.indices = {0, 1, 2};
	b.indices = {1, 3, 2};
	auto seams = run({a, b});
	CHECK(seams.report.mapped == 64);
	CHECK(seams.report.ambiguous == 0);
	CHECK(seams.report.borders > run({quad()}).report.borders);
	double area = 0;
	for (auto &s : seams.samples)
		area += s.areaWorldSquared;
	CHECK(close(area, 128));
	auto duplicate = quad();
	b.vertices[3].position.z += 4;
	auto crease = run({a, b});
	CHECK(crease.report.ambiguous > 0);
	for (const auto &s : crease.samples)
		if (s.coverage == LuxelCoverage::Ambiguous)
		{
			CHECK(!s.positionKnown);
			CHECK(s.issues & (LuxelNormalSeam | LuxelMixedFootprint));
			CHECK(!(s.issues & LuxelOverlap));
		}
	duplicate.indices.insert(duplicate.indices.end(), {2, 1, 0});
	auto d = run({duplicate});
	CHECK(d.report.duplicates == 1);
	for (auto &s : d.samples)
		CHECK(close(s.areaWorldSquared, 2));
	b = quad(1);
	for (auto &v : b.vertices)
		v.position.z += 8;
	auto overlap = run({quad(), b});
	CHECK(overlap.report.ambiguous == 64);
	for (const auto &s : overlap.samples)
	{
		CHECK(!s.positionKnown);
		CHECK(s.issues & LuxelOverlap);
		CHECK(s.areaWorldSquared == 0);
	}
	// Partial overlap below one full texel still must be detected.
	a = quad();
	a.vertices[1].lightmap[0].u = .1;
	a.vertices[3].lightmap[0].u = .1;
	b = a;
	b.ordinal = 1;
	for (auto &v : b.vertices)
		v.position.z += 1;
	auto small = run({a, b});
	CHECK(small.report.ambiguous > 0);
	CHECK(small.report.padding > 0);
}
void arbitrary()
{
	auto s = quad();
	s.kind = BakedSurfaceKind::Triangles;
	s.vertices.resize(3);
	s.indices = {0, 1, 2};
	s.vertices[0].lightmap[0] = {.12, .08};
	s.vertices[1].lightmap[0] = {.89, .21};
	s.vertices[2].lightmap[0] = {.31, .94};
	for (auto &v : s.vertices)
	{
		auto uv = v.lightmap[0];
		v.position = Vec3(float(10 + 7 * uv.u + 3 * uv.v), float(-5 + 2 * uv.u + 11 * uv.v),
						  float(3 + uv.u - 2 * uv.v));
		v.shadingNormal = Vec3(-15, 17, 71);
	}
	auto t = run({s}, 32, 32);
	CHECK(t.report.mapped > 100);
	CHECK(t.report.padding > 100);
	CHECK(t.report.footprintOnly > 0);
	CHECK(t.report.borders > 0);
	for (const auto &v : t.samples)
		if (v.positionKnown)
		{
			double u = v.centreUv.u, w = v.centreUv.v;
			double error = std::sqrt(std::pow(v.positionWorld.x - (10 + 7 * u + 3 * w), 2) +
									 std::pow(v.positionWorld.y - (-5 + 2 * u + 11 * w), 2) +
									 std::pow(v.positionWorld.z - (3 + u - 2 * w), 2));
			maximumPositionError = std::max(maximumPositionError, error);
			CHECK(error < 3e-6);
			CHECK(v.shadingNormalKnown);
		}
	double coveredArea = 0;
	for (auto &v : t.samples)
		coveredArea += v.areaWorldSquared;
	Vec3 a = s.vertices[0].position, b = s.vertices[1].position, c = s.vertices[2].position;
	double dx1 = double(b.x) - a.x, dy1 = double(b.y) - a.y, dz1 = double(b.z) - a.z,
		   dx2 = double(c.x) - a.x, dy2 = double(c.y) - a.y, dz2 = double(c.z) - a.z;
	double exact =
		.5 * std::sqrt(std::pow(dy1 * dz2 - dz1 * dy2, 2) + std::pow(dz1 * dx2 - dx1 * dz2, 2) +
					   std::pow(dx1 * dy2 - dy1 * dx2, 2));
	CHECK(close(coveredArea, exact, 1e-8));
}
BakedSurface patch()
{
	BakedSurface p;
	p.ordinal = 0;
	p.motion = SurfaceMotion::StaticWorld;
	p.kind = BakedSurfaceKind::Patch;
	p.patchWidth = p.patchHeight = 3;
	p.lightmap[0] = 3;
	p.styles[0] = 0;
	p.material = "synthetic/curved";
	for (unsigned y = 0; y < 3; ++y)
		for (unsigned x = 0; x < 3; ++x)
		{
			BakedVertex v;
			v.lightmap[0] = {double(x) / 2, double(y) / 2};
			v.position = Vec3(float(x) * 4, float(y) * 4, x == 1 ? 4.f : 0.f);
			v.shadingNormal = Vec3(0, 0, 1);
			p.vertices.push_back(v);
		}
	return p;
}
void curved()
{
	auto p = patch();
	auto t = run({p}, 16, 16);
	CHECK(t.report.mapped == 256);
	CHECK(t.report.patchSegments == 1);
	double area = 0;
	for (const auto &s : t.samples)
	{
		double u = s.centreUv.u, v = s.centreUv.v;
		double expectedZ = 8 * u * (1 - u);
		double err = std::sqrt(std::pow(s.positionWorld.x - 8 * u, 2) +
							   std::pow(s.positionWorld.y - 8 * v, 2) +
							   std::pow(s.positionWorld.z - expectedZ, 2));
		maximumPositionError = std::max(maximumPositionError, err);
		CHECK(err < 1e-6);
		CHECK(s.issues & LuxelAreaApproximate);
		CHECK(close(s.shadingNormal.z, 1));
		CHECK(close(s.geometricNormal.x / s.geometricNormal.z, -(1 - 2 * u), 2e-6));
		area += s.areaWorldSquared;
	}
	// z=8u(1-u), x=8u,y=8v. Area =64* integral_0^1 sqrt(1+(1-2u)^2) du.
	double exact = 32 * (std::sqrt(2.) + std::asinh(1.));
	maximumAreaError = std::abs(exact - area);
	CHECK(maximumAreaError < 1e-8);
	// World translation must not corrupt analytic derivatives or area.
	auto shifted = patch();
	for (auto &v : shifted.vertices)
	{
		v.position.x += 1024;
		v.position.y -= 2048;
		v.position.z += 4096;
	}
	auto translated = run({shifted}, 16, 16);
	CHECK(translated.report.mapped == 256);
	for (size_t i = 0; i < t.samples.size(); ++i)
	{
		const auto &a = t.samples[i];
		const auto &b = translated.samples[i];
		CHECK(close(a.geometricNormal.x, b.geometricNormal.x, 1e-7));
		CHECK(close(a.geometricNormal.z, b.geometricNormal.z, 1e-7));
		CHECK(close(a.areaWorldSquared, b.areaWorldSquared, 1e-10));
		CHECK(close(a.positionWorld.z, b.positionWorld.z - 4096, 1e-6));
	}
	// All controls at the first edge coincide. Its pixel centres have positions
	// but no regular surface normal; cancellation must never fabricate a unit vector.
	auto pole = patch();
	for (size_t y = 0; y < 3; ++y)
	{
		pole.vertices[y * 3].position = Vec3(-1916, 764, 1408);
		for (size_t x = 0; x < 3; ++x)
			pole.vertices[y * 3 + x].lightmap[0] =
				LightmapUv(.0625 + double(x) * .875 / 2, .0625 + double(y) * .875 / 2);
	}
	auto singular = run({pole});
	for (size_t y = 0; y < 8; ++y)
	{
		const auto &s = singular.samples[y * 8];
		CHECK(!s.positionKnown);
		CHECK(s.coverage == LuxelCoverage::Ambiguous);
		CHECK(s.issues & LuxelDegenerate);
		CHECK(s.areaWorldSquared == 0);
	}
	p.vertices[4].lightmap[0].u += .1;
	auto unsupported = run({p});
	CHECK(unsupported.report.unsupportedPatchUv == 1);
	CHECK(unsupported.report.mapped == 0);
	for (auto &s : unsupported.samples)
		CHECK(!s.positionKnown);
}
struct Cancel : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
void invalidAndLifetime()
{
	auto unused = quad();
	unused.vertices[0].lightmap[3].u = std::numeric_limits<double>::quiet_NaN();
	CHECK(run({unused}).report.mapped == 64);
	auto missingNormal = quad();
	missingNormal.vertices[0].shadingNormal.x = std::numeric_limits<float>::quiet_NaN();
	auto mn = run({missingNormal});
	CHECK(mn.report.mapped == 64);
	CHECK(!mn.samples[0].shadingNormalKnown);
	CHECK(mn.samples[0].issues & LuxelShadingMissing);
	missingNormal.vertices[0].shadingNormal.x = std::numeric_limits<float>::infinity();
	auto infNormal = run({missingNormal});
	CHECK(infNormal.report.mapped == 64);
	CHECK(!infNormal.samples[0].shadingNormalKnown);
	CHECK(std::isfinite(infNormal.samples[0].shadingNormal.x));
	CHECK(std::isfinite(infNormal.samples[0].geometricNormal.z));
	Vec3 origin;
	CHECK(LuxelRayOrigin(SurfaceLightingObservation(), origin).code == Code::Unavailable);
	auto unoriented = quad();
	unoriented.kind = BakedSurfaceKind::Triangles;
	for (auto &v : unoriented.vertices)
		v.shadingNormal = Vec3();
	auto u = run({unoriented});
	CHECK(u.report.mapped == 64);
	CHECK(!u.samples[0].normalOrientationKnown);
	CHECK(u.samples[0].issues & LuxelOrientationUnknown);
	CHECK(LuxelRayOrigin(u.samples[0], origin).code == Code::Unavailable);
	Decode::Decoder decoder;
	Decode::LightmapSample sourceColour;
	LuxelTile invalidTile;
	invalidTile.samples.push_back(SurfaceLightingObservation());
	CHECK(DecodeLuxel(decoder, invalidTile, 0, ReadContext(), sourceColour).code ==
		  Code::Unavailable);
	auto im = images();
	auto g = geometry({quad()}, im);
	ReadContext c;
	c.maxBytes = 1024 * 1024;
	MappingOptions options;
	LuxelTile out;
	CHECK(!MapLuxels({}, im, 0, 0, 0, 8, 8, options, c, out));
	CHECK(!MapLuxels(g, im, 1, 0, 0, 8, 8, options, c, out));
	CHECK(!MapLuxels(g, im, 0, 7, 0, 8, 8, options, c, out));
	c.maxBytes = 1;
	CHECK(MapLuxels(g, im, 0, 0, 0, 8, 8, options, c, out).code == Code::MemoryLimit);
	c.maxBytes = 1024 * 1024;
	Cancel cancel;
	c.cancellation = &cancel;
	CHECK(MapLuxels(g, im, 0, 0, 0, 8, 8, options, c, out).code == Code::Cancelled);
	c.cancellation = nullptr;
	options.maxWork = 1;
	CHECK(MapLuxels(g, im, 0, 0, 0, 8, 8, options, c, out).code == Code::WorkLimit);
	options = MappingOptions();
	options.maxScratchBytes = 1;
	CHECK(MapLuxels(g, im, 0, 0, 0, 8, 8, options, c, out).code == Code::MemoryLimit);
	options = MappingOptions();
	auto q = quad();
	q.motion = SurfaceMotion::BrushModel;
	auto excluded = run({q});
	CHECK(excluded.report.mapped == 0);
	CHECK(excluded.report.excluded == 1);
	q.motion = SurfaceMotion::Deformed;
	CHECK(run({q}).report.mapped == 0);
	q.motion = SurfaceMotion::Unknown;
	CHECK(run({q}).report.mapped == 0);
	q = quad();
	q.lightmap[0] = -3;
	CHECK(run({q}).report.mapped == 0);
	q = quad();
	q.indices = {0, 0, 1};
	CHECK(run({q}).report.degenerate == 1);
	q = quad();
	q.vertices[1].lightmap[0] = q.vertices[0].lightmap[0];
	q.vertices[2].lightmap[0] = q.vertices[0].lightmap[0];
	q.indices = {0, 1, 2};
	CHECK(run({q}).report.degenerate == 1);
	q = quad();
	q.indices[0] = 999;
	CHECK(run({q}).report.unsupported == 1);
	q = quad();
	q.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN();
	CHECK(run({q}).report.unsupported == 1);
	SurfaceCapture owner;
	for (int n = 0; n < 20; ++n)
	{
		owner.Begin(BuildPolicy::ForceRebuild);
		CHECK(owner.SetMap(im->Map()));
		CHECK(owner.Add(quad()));
		CHECK(owner.Material(0, false, false));
		CHECK(owner.Freeze());
		auto lease = owner.Acquire();
		owner.Reset();
		CHECK(!owner.Acquire());
		CHECK(MapLuxels(lease, im, 0, 0, 0, 8, 8, options, c, out));
		CHECK(out.report.mapped == 64);
	}
	for (int policy = 0; policy < 4; ++policy)
		for (bool hit : {false, true})
		{
			owner.Begin(static_cast<BuildPolicy>(policy), hit);
			CHECK(owner.Needed() == (policy == 3 || (policy == 2 && !hit)));
		}
	SurfaceLimits limits;
	limits.maxBytes = 1;
	SurfaceCapture small(limits);
	small.Begin(BuildPolicy::ForceRebuild);
	CHECK(small.SetMap(im->Map()));
	CHECK(small.Add(quad()).code == Code::MemoryLimit);
	CHECK(!small.Acquire());
	auto kept = run({quad()});
	auto copy = kept.samples;
	CHECK(MapLuxels(kept.geometry, kept.images, 0, 0, 0, 8, 8, options, c, out));
	for (size_t i = 0; i < copy.size(); ++i)
	{
		CHECK(copy[i].positionWorld.x == out.samples[i].positionWorld.x);
		CHECK(copy[i].areaWorldSquared == out.samples[i].areaWorldSquared);
	}
	InputDataset input;
	input.surfaceGeometry = g;
	InputReader reader(input);
	CHECK(reader.Geometry() == g);
}
} // namespace
int main()
{
	static_assert(std::is_const<decltype(SurfaceCapture().Acquire())::element_type>::value,
				  "immutable");
	planar();
	rotatedAndSeams();
	arbitrary();
	curved();
	invalidAndLifetime();
	// A nonpolygonal flare's irrelevant lightmap number must not invalidate a whole chart.
	BakedSurface flare; flare.ordinal=1; flare.kind=BakedSurfaceKind::Flare;
	flare.motion=SurfaceMotion::StaticWorld; flare.lightmap[0]=3; flare.styles[0]=0;
	auto withFlare=run({quad(),flare});
	CHECK(withFlare.report.mapped==64 && withFlare.report.unsupported==0);
	CHECK(withFlare.report.excluded==1);
	CHECK(ClassifySurface(flare,0,false).reasons&IntentionallyExcluded);
	flare.kind=BakedSurfaceKind::Unsupported;
	auto genuinelyUnknown=run({quad(),flare});
	CHECK(genuinelyUnknown.report.mapped==0 && genuinelyUnknown.report.unsupported>0);
	std::printf("luxel surfaces: %d checks, %d failures; maximum position error %.9g world units; "
				"area error %.9g world units squared\n",
				checks, failures, maximumPositionError, maximumAreaError);
	return failures ? 1 : 0;
}
