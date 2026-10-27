// Generated scenes only. Independent reference uses plane/barycentric intersection.
#include "lightrecon/visibility.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace
{
int checks = 0, failures = 0;
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
ReadContext Context()
{
	ReadContext c;
	c.maxBytes = 256ull * 1024 * 1024;
	return c;
}
bool Near(double a, double b, double e = 1e-6)
{
	return std::abs(a - b) <= e;
}
struct Scene
{
	std::vector<BakedSurface> surfaces;
	BakedSurface &Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t signals = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.motion = SurfaceMotion::StaticWorld;
		s.kind = BakedSurfaceKind::Face;
		s.material = "test";
		s.materialIndex = s.ordinal + 17;
		s.materialFacts.available = true;
		s.materialFacts.signals = signals;
		s.materialFacts.cull = SurfaceCull::None;
		for (auto p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p;
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
		return surfaces.back();
	}
	void WallX(float x, float lo = -2, float hi = 2, float bottom = 0, float top = 2,
			   uint32_t flags = 0)
	{
		Quad(Vec3(x, lo, bottom), Vec3(x, hi, bottom), Vec3(x, hi, top), Vec3(x, lo, top), flags);
	}
	void PlaneZ(float z, uint32_t flags = 0)
	{
		Quad(Vec3(-2, -2, z), Vec3(2, -2, z), Vec3(2, 2, z), Vec3(-2, 2, z), flags);
	}
	std::shared_ptr<const SurfaceGeometry> Freeze() const
	{
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		LightmapMapIdentity map;
		map.path = "maps/generated.bsp";
		map.bytes = 1;
		map.sha256[0] = 17;
		CHECK(capture.SetMap(map));
		for (auto s : surfaces)
			CHECK(capture.Add(std::move(s)));
		CHECK(capture.Freeze());
		return capture.Acquire();
	}
	std::shared_ptr<const StaticVisibility>
	Build(VisibilityBuildOptions o = VisibilityBuildOptions(),
		  std::shared_ptr<const PointClassifier> domain = {}) const
	{
		std::shared_ptr<const StaticVisibility> result;
		CHECK(StaticVisibility::Build(Freeze(), o, Context(), result, domain));
		CHECK(result);
		return result;
	}
};
RayQuery Ray(Vec3 start, Vec3 end, RayPurpose purpose = RayPurpose::Segment)
{
	RayQuery q;
	q.startWorld = start;
	q.endWorld = end;
	q.purpose = purpose;
	q.originKnownFree = true;
	return q;
}
RayResult Trace(const std::shared_ptr<const StaticVisibility> &scene, RayQuery q)
{
	RayResult r;
	CHECK(scene->Query(q, Context(), r));
	return r;
}
class BoxDomain final : public PointClassifier
{
  public:
	Error Classify(Vec3 p, double tolerance, PointDomain &out) const override
	{
		// This fixture has a real closed [-.25,.25] x [-2,2] x [0,2] solid.
		const double v[3] = {p.x, p.y, p.z}, lo[3] = {-.25, -2, 0}, hi[3] = {.25, 2, 2};
		bool boundary = false;
		for (unsigned k = 0; k < 3; ++k)
		{
			if (v[k] < lo[k] - tolerance || v[k] > hi[k] + tolerance)
			{
				out = PointDomain::Free;
				return Error();
			}
			boundary = boundary || v[k] <= lo[k] + tolerance || v[k] >= hi[k] - tolerance;
		}
		out = boundary ? PointDomain::Boundary : PointDomain::Solid;
		return Error();
	}
};
class FailedDomain final : public PointClassifier
{
  public:
	explicit FailedDomain(bool throws) : throws_(throws) {}
	Error Classify(Vec3, double, PointDomain &) const override
	{
		if (throws_)
			throw std::runtime_error("fixture failure");
		return Failure(Code::ProviderError, "fixture failure");
	}

  private:
	bool throws_;
};
class Stop final : public Cancellation
{
  public:
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
void RoomsAndPolicies()
{
	Scene s;
	s.WallX(-2);
	s.WallX(2); // outside walls of two rooms
	s.WallX(0, -2, -.5f);
	s.WallX(0, .5f, 2);
	s.WallX(0, -.5f, .5f, 1, 2); // doorway
	auto open = s.Build();
	auto q = Ray(Vec3(-1, 0, .5f), Vec3(1, 0, .5f));
	auto r = Trace(open, q);
	CHECK(r.state == VisibilityState::Visible);
	CHECK(!r.firstHit.present);
	CHECK(r.reliability == RayReliability::Geometric);
	r = Trace(open, Ray(Vec3(-1, 1, 1), Vec3(1, 1, 1)));
	CHECK(r.state == VisibilityState::Blocked);
	CHECK(r.firstHit.surface == 4);
	CHECK(r.firstHit.material == 20);
	CHECK(r.firstHit.positionKnown && Near(r.firstHit.positionWorld.x, 0));
	CHECK(Near(r.firstHit.distanceWorld, 1));
	CHECK(r.terminalHit.triangle != 0);
	CHECK(r.firstHit.obstacle == ObstacleKind::Opaque);
	// Same hypothetical PVS cluster has no effect: no PVS input can bypass the wall.
	s.WallX(0, -.5f, .5f, 0, 1);
	auto closed = s.Build();
	CHECK(Trace(closed, q).state == VisibilityState::Blocked);
	s.surfaces.back().motion = SurfaceMotion::BrushModel;
	auto moving = s.Build();
	CHECK(Trace(moving, q).state == VisibilityState::Visible);
	CHECK(moving->Report().excludedMoving == 1);
	s.surfaces.back().motion = SurfaceMotion::Deformed;
	CHECK(s.Build()->Report().excludedMoving == 1);
	s.surfaces.back().motion = SurfaceMotion::StaticWorld;
	s.surfaces.back().materialFacts.signals = MaterialNoDraw;
	CHECK(Trace(s.Build(), q).state == VisibilityState::Visible);

	Scene grate;
	grate.WallX(0, -2, 2, 0, 2, MaterialCutout);
	auto alpha = grate.Build();
	r = Trace(alpha, q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & OpticalUnknown);
	CHECK(r.firstHit.obstacle == ObstacleKind::AlphaTested);
	CHECK(r.firstHit.positionKnown);
	q.policy.alphaTested = OpticalPolicy::AssumeOpaque;
	r = Trace(alpha, q);
	CHECK(r.state == VisibilityState::Blocked);
	CHECK(r.reliability == RayReliability::Assumed);
	q.policy.alphaTested = OpticalPolicy::Ignore;
	r = Trace(alpha, q);
	CHECK(r.state == VisibilityState::Visible);
	CHECK(r.reliability == RayReliability::Assumed);
	q.policy = RayPolicy();
	grate.WallX(.5f);
	r = Trace(grate.Build(), q);
	CHECK(r.state == VisibilityState::Blocked);
	CHECK(r.firstHit.obstacle == ObstacleKind::AlphaTested);
	CHECK(r.terminalHit.obstacle == ObstacleKind::Opaque);
	CHECK(Near(r.terminalHit.positionWorld.x, .5));
	Scene glass;
	glass.WallX(0, -2, 2, 0, 2, MaterialTranslucent);
	r = Trace(glass.Build(), q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.firstHit.obstacle == ObstacleKind::Translucent);
	glass.surfaces[0].materialFacts.signals |= MaterialCutout;
	q.policy.alphaTested = OpticalPolicy::Ignore;
	CHECK(Trace(glass.Build(), q).state == VisibilityState::Unknown);
	q.policy.translucent = OpticalPolicy::Ignore;
	CHECK(Trace(glass.Build(), q).reliability == RayReliability::Assumed);
	q.policy = RayPolicy();
	Scene missing;
	missing.WallX(0);
	missing.surfaces[0].materialFacts.available = false;
	r = Trace(missing.Build(), q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.firstHit.obstacle == ObstacleKind::Unsupported);
	Scene side;
	side.PlaneZ(0);
	side.surfaces[0].materialFacts.cull = SurfaceCull::Back;
	auto sided = side.Build();
	auto down = Ray(Vec3(0, 0, 1), Vec3(0, 0, -1));
	CHECK(Trace(sided, down).state == VisibilityState::Blocked);
	auto up = Ray(down.endWorld, down.startWorld);
	CHECK(Trace(sided, up).state == VisibilityState::Visible);
	up.policy.facing = FacingPolicy::TwoSided;
	CHECK(Trace(sided, up).state == VisibilityState::Blocked);
	side.surfaces[0].materialFacts.cull = SurfaceCull::Front;
	sided = side.Build();
	CHECK(Trace(sided, down).state == VisibilityState::Visible);
	CHECK(Trace(sided, Ray(down.endWorld, down.startWorld)).state == VisibilityState::Blocked);
	side.surfaces[0].materialFacts.cull = SurfaceCull::Unknown;
	r = Trace(side.Build(), down);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & SidednessUnknown);
}
void SkyAndBias()
{
	Scene s;
	// Roof with skylight opening; actual sky boundary above it.
	s.Quad(Vec3(-2, -2, 1), Vec3(-.5f, -2, 1), Vec3(-.5f, 2, 1), Vec3(-2, 2, 1));
	s.Quad(Vec3(.5f, -2, 1), Vec3(2, -2, 1), Vec3(2, 2, 1), Vec3(.5f, 2, 1));
	s.PlaneZ(2, MaterialSky);
	auto scene = s.Build();
	auto q = Ray(Vec3(0, 0, 0), Vec3(0, 0, 3), RayPurpose::Sky);
	auto r = Trace(scene, q);
	CHECK(r.state == VisibilityState::Visible && r.sky == SkyReach::Reached);
	CHECK(r.terminalHit.obstacle == ObstacleKind::Sky);
	CHECK(Near(r.firstHit.distanceWorld, 2));
	CHECK(Near(r.firstHit.parameter, 2. / 3));
	q.purpose = RayPurpose::Sun;
	CHECK(Trace(scene, q).sky == SkyReach::Reached);
	q.purpose = RayPurpose::Segment;
	CHECK(Trace(scene, q).state == VisibilityState::Unknown);
	q = Ray(Vec3(1, 0, 0), Vec3(1, 0, 3), RayPurpose::Sky);
	r = Trace(scene, q);
	CHECK(r.state == VisibilityState::Blocked && r.sky == SkyReach::NotReached);
	Scene empty;
	auto emptyScene = empty.Build();
	r = Trace(emptyScene, Ray(Vec3(0, 0, 0), Vec3(0, 0, 3), RayPurpose::Sky));
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & SkyBoundaryMissing);
	CHECK(Trace(emptyScene, Ray(Vec3(0, 0, 0), Vec3(0, 0, 3))).state == VisibilityState::Visible);
	s.surfaces[2].materialFacts.signals = MaterialSky;
	s.PlaneZ(2);
	r = Trace(s.Build(), Ray(Vec3(0, 0, 0), Vec3(0, 0, 3), RayPurpose::Sky));
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & CoincidentSurfaces);

	Scene plane;
	plane.PlaneZ(0);
	auto flat = plane.Build();
	q = Ray(Vec3(0, 0, 0), Vec3(0, 0, 1));
	r = Trace(flat, q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & OriginOnBoundary);
	q.originSurface = 1;
	q.bias.normalKnown = true;
	q.bias.geometricNormal = Vec3(0, 0, 1);
	r = Trace(flat, q);
	CHECK(r.state == VisibilityState::Visible);
	CHECK(r.appliedBiasWorld > 0);
	CHECK(q.startWorld.z == 0);
	CHECK(r.effectiveOriginWorld.z > 0);
	q.endWorld.z = -1;
	CHECK(Trace(flat, q).state == VisibilityState::Visible);
	q.endWorld.z = 1;
	plane.PlaneZ(.000001f);
	r = Trace(plane.Build(), q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & BiasObstructed);
	CHECK(r.firstHit.surface == 2);
	q.bias.footprintWorld = 1e-6;
	r = Trace(flat, q);
	CHECK(r.issues & PrecisionLimit);
	q = Ray(Vec3(0, 0, 1e-7f), Vec3(0, 0, -1e-7f));
	r = Trace(flat, q);
	CHECK(r.state == VisibilityState::Blocked);
	CHECK(Near(r.firstHit.distanceWorld, 1e-7, 1e-13));
	q = Ray(Vec3(-1, 0, 0), Vec3(1, 0, 0));
	r = Trace(flat, q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & CoplanarRay);
	q = Ray(Vec3(0, 0, 1), Vec3(0, 0, 0));
	q.targetSurface = 1;
	q.targetToleranceWorld = 1e-5;
	CHECK(Trace(flat, q).state == VisibilityState::Visible);
	q.targetSurface = 0;
	CHECK(Trace(flat, q).state == VisibilityState::Blocked);
	Scene far;
	far.Quad(Vec3(32766, -2, 32768), Vec3(32770, -2, 32768), Vec3(32770, 2, 32768),
			 Vec3(32766, 2, 32768));
	q = Ray(Vec3(32768, 0, 32768), Vec3(32768, 0, 32769));
	q.originSurface = 1;
	q.bias.normalKnown = true;
	q.bias.geometricNormal = Vec3(0, 0, 1);
	r = Trace(far.Build(), q);
	CHECK(r.state == VisibilityState::Visible);
	CHECK(r.appliedBiasWorld > .015 && r.appliedBiasWorld < .02);
	CHECK(r.effectiveOriginWorld.z > q.startWorld.z);
}
void DomainsAndFailures()
{
	Scene s;
	s.WallX(-.25f);
	s.WallX(.25f);
	s.PlaneZ(0);
	s.PlaneZ(2);
	s.Quad(Vec3(-.25f, -2, 0), Vec3(.25f, -2, 0), Vec3(.25f, -2, 2), Vec3(-.25f, -2, 2));
	s.Quad(Vec3(-.25f, 2, 0), Vec3(.25f, 2, 0), Vec3(.25f, 2, 2), Vec3(-.25f, 2, 2));
	auto scene = s.Build(VisibilityBuildOptions(), std::make_shared<BoxDomain>());
	auto q = Ray(Vec3(0, 0, 1), Vec3(1, 0, 1));
	auto r = Trace(scene, q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & OriginInsideSolid);
	CHECK(!r.firstHit.present);
	q = Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1));
	q.originKnownFree = false;
	CHECK(Trace(scene, q).state == VisibilityState::Blocked);
	q = Ray(Vec3(-1, 0, 1), Vec3(-.5f, 0, 1));
	q.originKnownFree = false;
	CHECK(Trace(scene, q).state == VisibilityState::Visible);
	auto unclassified = s.Build();
	r = Trace(unclassified, q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & OriginUnclassified);
	q.policy.requireKnownFreeOrigin = false;
	r = Trace(unclassified, q);
	CHECK(r.state == VisibilityState::Visible && r.reliability == RayReliability::Assumed);
	float transmission = 17;
	ReadContext ctx = Context();
	CHECK(unclassified->Segment(q.startWorld, q.endWorld, ctx, transmission).code ==
		  Code::Unavailable);
	CHECK(transmission == 17);
	CHECK(scene->Segment(q.startWorld, q.endWorld, ctx, transmission));
	CHECK(transmission == 1);
	q = Ray(Vec3(-.25f, 0, 1), Vec3(1, 0, 1));
	q.originSurface = 1;
	q.bias.normalKnown = true;
	q.bias.geometricNormal = Vec3(-1, 0, 0);
	r = Trace(scene, q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & OriginInsideSolid);
	for (bool throws : {false, true})
	{
		auto failed = s.Build(VisibilityBuildOptions(), std::make_shared<FailedDomain>(throws));
		CHECK(failed->Query(q, ctx, r).code == Code::ProviderError);
		CHECK(r.state == VisibilityState::Unknown);
	}
	q.startWorld.x = std::numeric_limits<float>::infinity();
	CHECK(scene->Query(q, ctx, r).code == Code::InvalidInput);
	q = Ray(Vec3(), Vec3());
	CHECK(scene->Query(q, ctx, r).code == Code::InvalidInput);
	q = Ray(Vec3(0, 0, 1), Vec3(1, 0, 1));
	q.bias.absoluteWorld = -1;
	CHECK(scene->Query(q, ctx, r).code == Code::InvalidInput);
	q.bias = RayBias();
	q.originSurface = UINT32_MAX;
	CHECK(scene->Query(q, ctx, r).code == Code::InvalidInput);
	q.originSurface = 1;
	q.bias.normalKnown = true;
	CHECK(scene->Query(q, ctx, r).code == Code::InvalidInput);
	q = Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1));
	q.maxWork = 1;
	CHECK(scene->Query(q, ctx, r).code == Code::WorkLimit);
	q.maxWork = 100;
	ctx.maxBytes = 4095;
	CHECK(scene->Query(q, ctx, r).code == Code::MemoryLimit);
	ctx = Context();
	Stop stop;
	ctx.cancellation = &stop;
	CHECK(scene->Query(q, ctx, r).code == Code::Cancelled);
	std::shared_ptr<const StaticVisibility> out = scene;
	VisibilityBuildOptions options;
	CHECK(StaticVisibility::Build({}, options, Context(), out).code == Code::MissingGeometry);
	CHECK(!out);
	options.maxBytes = 1;
	CHECK(StaticVisibility::Build(s.Freeze(), options, Context(), out).code == Code::MemoryLimit);
	CHECK(!out);
	options = VisibilityBuildOptions();
	options.maxPrimitives = 1;
	CHECK(StaticVisibility::Build(s.Freeze(), options, Context(), out).code == Code::MemoryLimit);
	options = VisibilityBuildOptions();
	options.maxWork = 1;
	CHECK(StaticVisibility::Build(s.Freeze(), options, Context(), out).code == Code::WorkLimit);
	options = VisibilityBuildOptions();
	options.maxPatchSubdivisions = 3;
	CHECK(StaticVisibility::Build(s.Freeze(), options, Context(), out).code == Code::InvalidInput);
	options = VisibilityBuildOptions();
	CHECK(StaticVisibility::Build(s.Freeze(), options, ctx, out).code == Code::Cancelled);
	options.completeStaticGeometry = false;
	auto incomplete = s.Build(options);
	r = Trace(incomplete, Ray(Vec3(-1, 0, 1), Vec3(-.5f, 0, 1)));
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & IncompleteGeometry);
	RayQuery queries[3] = {Ray(Vec3(-1, 0, 1), Vec3(-.5f, 0, 1)), Ray(Vec3(), Vec3()),
						   Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1))};
	RayResult results[3];
	size_t completed = 77;
	CHECK(scene->Batch(queries, 3, Context(), results, completed).code == Code::InvalidInput);
	CHECK(completed == 1);
	CHECK(scene->Batch(nullptr, 0, ReadContext(), nullptr, completed));
	CHECK(completed == 0);
	CHECK(scene->Batch(nullptr, 1, Context(), results, completed).code == Code::InvalidInput);
	InputDataset input;
	input.visibility = scene;
	CHECK(input.visibility->Batch(queries, 1, Context(), results, completed));
	CHECK(completed == 1 && results[0].state == VisibilityState::Visible);
	VisibilityProvider provider;
	CHECK(provider.Query(q, Context(), r).code == Code::Unavailable);
	static_assert(
		std::is_same<decltype(scene->Surfaces()), const std::vector<VisibilitySurface> &>::value,
		"immutable views");
}
void MeshAndPatch()
{
	Scene diagonal;
	diagonal.Quad(Vec3(-1, -1, 0), Vec3(1, 1, 0), Vec3(1, 1, 2), Vec3(-1, -1, 2));
	diagonal.surfaces[0].kind = BakedSurfaceKind::Triangles;
	auto mesh = diagonal.Build();
	auto r = Trace(mesh, Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1)));
	CHECK(r.state == VisibilityState::Blocked);
	CHECK(Near(r.firstHit.distanceWorld, 1));
	// Shared diagonal and vertices must not leak; both traversal orders agree.
	for (int i = -10; i <= 10; ++i)
	{
		float v = float(i) / 10;
		r = Trace(mesh, Ray(Vec3(v - 1, v, 1), Vec3(v + 1, v, 1)));
		CHECK(r.state == VisibilityState::Blocked);
	}
	Scene patch;
	BakedSurface p;
	p.ordinal = 0;
	p.kind = BakedSurfaceKind::Patch;
	p.motion = SurfaceMotion::StaticWorld;
	p.patchWidth = p.patchHeight = 3;
	p.materialFacts.available = true;
	p.materialFacts.cull = SurfaceCull::None;
	for (unsigned y = 0; y < 3; ++y)
		for (unsigned x = 0; x < 3; ++x)
		{
			BakedVertex v;
			v.position = Vec3(float(x), float(y), x == 1 && y == 1 ? 4.0f : 0.0f);
			p.vertices.push_back(v);
		}
	patch.surfaces.push_back(p);
	auto curved = patch.Build();
	CHECK(curved->Report().patchTriangles > 2);
	CHECK(curved->Report().maximumPatchErrorWorld < .011);
	auto q = Ray(Vec3(1, 1, 3), Vec3(1, 1, -1));
	r = Trace(curved, q);
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & PatchApproximation);
	q.policy.acceptPatchApproximation = true;
	r = Trace(curved, q);
	CHECK(r.state == VisibilityState::Blocked);
	CHECK(r.reliability == RayReliability::Approximate);
	CHECK(r.terminalHit.positionKnown);
	CHECK(Near(r.terminalHit.positionWorld.z, 1, .011));
	for (int y = 1; y < 10; ++y)
		for (int x = 1; x < 10; ++x)
		{
			float u = float(x) / 5, v = float(y) / 5;
			auto hit = Trace(curved, Ray(Vec3(u, v, 3), Vec3(u, v, -1)));
			CHECK(
				hit.state !=
				VisibilityState::Visible); // expanded chord bounds never certify a clear curved ray
		}
	VisibilityBuildOptions tight;
	tight.maxPatchSubdivisions = 1;
	std::shared_ptr<const StaticVisibility> failed;
	CHECK(StaticVisibility::Build(patch.Freeze(), tight, Context(), failed).code ==
		  Code::WorkLimit);
	Scene unsupported;
	unsupported.WallX(0);
	unsupported.surfaces[0].kind = BakedSurfaceKind::Unsupported;
	auto unknown = unsupported.Build();
	r = Trace(unknown, Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1)));
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(r.issues & UnsupportedGeometry);
	CHECK(!r.firstHit.positionKnown);
	unsupported.surfaces[0].vertices.clear();
	unknown = unsupported.Build();
	CHECK(!unknown->Report().complete);
	CHECK(unknown->Report().unboundedUnsupported == 1);
	CHECK(Trace(unknown, Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1))).state == VisibilityState::Unknown);
	Scene degenerate;
	degenerate.WallX(0);
	degenerate.surfaces[0].vertices[1].position = degenerate.surfaces[0].vertices[0].position;
	CHECK(degenerate.Build()->Report().degenerate == 1);
	Scene noTriangles;
	noTriangles.WallX(0);
	noTriangles.surfaces[0].indices.clear();
	r = Trace(noTriangles.Build(), Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1)));
	CHECK(r.state == VisibilityState::Unknown);
	CHECK(!r.firstHit.positionKnown);
}
struct V
{
	double x, y, z;
	V(Vec3 p) : x(p.x), y(p.y), z(p.z) {}
	V(double a, double b, double c) : x(a), y(b), z(c) {}
	V operator-(V b) const
	{
		return {x - b.x, y - b.y, z - b.z};
	}
	V operator+(V b) const
	{
		return {x + b.x, y + b.y, z + b.z};
	}
	V operator*(double b) const
	{
		return {x * b, y * b, z * b};
	}
};
double dot(V a, V b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
V cross(V a, V b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
// Independent plane intersection followed by Gram-matrix barycentric coordinates.
double Reference(const Scene &scene, const RayQuery &q)
{
	V origin(q.startWorld), delta = V(q.endWorld) - origin;
	double nearest = 2;
	for (const auto &s : scene.surfaces)
		for (size_t i = 0; i < s.indices.size(); i += 3)
		{
			V a(s.vertices[s.indices[i]].position),
				e0 = V(s.vertices[s.indices[i + 1]].position) - a,
				e1 = V(s.vertices[s.indices[i + 2]].position) - a;
			V normal = cross(e0, e1);
			double den = dot(normal, delta);
			if (den == 0)
				continue;
			double t = dot(normal, a - origin) / den;
			if (t < 0 || t > 1 || t > nearest)
				continue;
			V p = origin + delta * t - a;
			double aa = dot(e0, e0), ab = dot(e0, e1), bb = dot(e1, e1), ap = dot(e0, p),
				   bp = dot(e1, p), det = aa * bb - ab * ab;
			double u = (bb * ap - ab * bp) / det, v = (aa * bp - ab * ap) / det;
			if (u >= 0 && v >= 0 && u + v <= 1)
				nearest = t;
		}
	return nearest;
}
uint32_t randomState = 1729;
float Random(float maximum)
{
	randomState = randomState * 1664525u + 1013904223u;
	return float(double(randomState) / 4294967296.0 * maximum);
}
Vec3 RandomPoint(float span)
{
	const float x = Random(span) - span / 2;
	const float y = Random(span) - span / 2;
	const float z = Random(span) - span / 2;
	return Vec3(x, y, z);
}
void ReferenceAndBenchmark()
{
	// Irregular, noncoplanar triangle soup: compare independently computed hits.
	Scene soup;
	BakedSurface mesh;
	mesh.ordinal = 0;
	mesh.kind = BakedSurfaceKind::Triangles;
	mesh.motion = SurfaceMotion::StaticWorld;
	mesh.materialFacts.available = true;
	mesh.materialFacts.cull = SurfaceCull::None;
	for (unsigned i = 0; i < 1000; ++i)
		for (unsigned vertex = 0; vertex < 3; ++vertex)
		{
			BakedVertex v;
			v.position = RandomPoint(20);
			mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
			mesh.vertices.push_back(v);
		}
	soup.surfaces.push_back(mesh);
	auto irregular = soup.Build();
	double soupError = 0;
	for (unsigned i = 0; i < 1024; ++i)
	{
		const Vec3 start = RandomPoint(30);
		const Vec3 end = RandomPoint(30);
		auto q = Ray(start, end);
		double reference = Reference(soup, q);
		auto result = Trace(irregular, q);
		CHECK((reference <= 1) == (result.state == VisibilityState::Blocked));
		if (reference <= 1)
		{
			double error = std::abs(reference - result.firstHit.parameter);
			soupError = std::max(soupError, error);
			CHECK(error < 1e-12);
		}
	}
	std::printf(
		"visibility irregular reference: triangles=1000 rays=1024 max-parameter-error=%.17g\n",
		soupError);
	Scene large;
	for (unsigned y = 0; y < 128; ++y)
		for (unsigned x = 0; x < 128; ++x)
		{
			float a = float(x), b = float(y);
			large.Quad(Vec3(a, b, 0), Vec3(a + 1, b, 0), Vec3(a + 1, b + 1, 0), Vec3(a, b + 1, 0));
		}
	auto begin = std::chrono::steady_clock::now();
	auto geometry = large.Freeze();
	std::shared_ptr<const StaticVisibility> scene;
	VisibilityBuildOptions options;
	CHECK(StaticVisibility::Build(geometry, options, Context(), scene));
	auto built = std::chrono::steady_clock::now();
	std::vector<RayQuery> rays;
	std::vector<RayResult> results;
	for (unsigned i = 0; i < 100000; ++i)
	{
		float x = Random(132) - 2, y = Random(132) - 2;
		const float startZ = 1 + Random(3);
		const float endZ = -1 - Random(3);
		rays.push_back(Ray(Vec3(x, y, startZ), Vec3(x + .2f, y + .1f, endZ)));
	}
	results.resize(rays.size());
	size_t completed = 0;
	CHECK(scene->Batch(rays.data(), rays.size(), Context(), results.data(), completed));
	CHECK(completed == rays.size());
	auto traced = std::chrono::steady_clock::now();
	double maxError = 0;
	uint64_t tests = 0;
	for (unsigned i = 0; i < 512; ++i)
	{
		double ref = Reference(large, rays[i]);
		CHECK((ref <= 1) == (results[i].state == VisibilityState::Blocked));
		if (ref <= 1)
		{
			double error = std::abs(ref - results[i].firstHit.parameter);
			maxError = std::max(maxError, error);
			CHECK(error < 1e-12);
		}
	}
	for (const auto &r : results)
		tests += r.primitiveTests;
	CHECK(tests < rays.size() * 100);
	CHECK(scene->Report().depth < 64);
	// Immutable scene outlives input geometry; repeated and concurrent queries reuse it.
	geometry.reset();
	std::atomic<unsigned> mismatches(0);
	std::vector<std::thread> workers;
	for (unsigned thread = 0; thread < 4; ++thread)
		workers.emplace_back([&, thread] {
			for (unsigned i = thread; i < 4096; i += 4)
			{
				RayResult r;
				auto error = scene->Query(rays[i], Context(), r);
				if (!error || r.state != results[i].state ||
					r.firstHit.triangle != results[i].firstHit.triangle ||
					r.firstHit.distanceWorld != results[i].firstHit.distanceWorld)
					++mismatches;
			}
		});
	for (auto &worker : workers)
		worker.join();
	CHECK(mismatches == 0);
	for (unsigned i = 0; i < 16; ++i)
	{
		auto small = Scene().Build();
		CHECK(small->Report().triangles == 0);
	}
	double buildMs = std::chrono::duration<double, std::milli>(built - begin).count(),
		   seconds = std::chrono::duration<double>(traced - built).count();
	std::printf("visibility benchmark: triangles=%llu nodes=%llu bytes=%llu peak-build-bytes=%llu "
				"depth=%u build-ms=%.3f rays=%zu seconds=%.6f rays-per-second=%.0f "
				"primitive-tests-per-ray=%.3f max-reference-parameter-error=%.17g\n",
				static_cast<unsigned long long>(scene->Report().triangles),
				static_cast<unsigned long long>(scene->Report().nodes),
				static_cast<unsigned long long>(scene->Report().ownedBytes),
				static_cast<unsigned long long>(scene->Report().peakBuildBytes),
				scene->Report().depth, buildMs, rays.size(), seconds, double(rays.size()) / seconds,
				double(tests) / double(rays.size()), maxError);
}
} // namespace
void LocalCompleteness()
{
	Scene recess;
	recess.WallX(0, -2, -.5f);
	recess.WallX(0, .5f, 2);
	recess.WallX(-1);
	auto fixture = recess.Build();
	CHECK(Trace(fixture, Ray(Vec3(-.5f, 0, 1), Vec3(2, 0, 1))).state == VisibilityState::Visible);
	CHECK(Trace(fixture, Ray(Vec3(-.5f, 0, 1), Vec3(2, 3, 1))).state == VisibilityState::Blocked);
	Scene scene;
	scene.WallX(0);
	BakedSurface flare;
	flare.ordinal = 1;
	flare.kind = BakedSurfaceKind::Flare;
	flare.lightmapOrigin = Vec3(10000, 10000, 10000);
	scene.surfaces.push_back(flare);
	auto good = scene.Build();
	CHECK(good->Report().excludedFlares == 1 && good->Report().spatialCoverageComplete);
	CHECK(Trace(good, Ray(Vec3(-1, 0, 1), Vec3(-1, 1, 1))).state == VisibilityState::Visible);
	CHECK(Trace(good, Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1))).state == VisibilityState::Blocked);
	scene.surfaces.back().kind = BakedSurfaceKind::Unsupported;
	auto unknown = scene.Build();
	CHECK(!unknown->Report().complete && !unknown->Report().spatialCoverageComplete);
	CHECK(unknown->Report().missingUnsupportedBounds == 1);
	auto r = Trace(unknown, Ray(Vec3(-1, 0, 1), Vec3(-1, 1, 1)));
	CHECK(r.state == VisibilityState::Unknown && (r.issues & IncompleteGeometry));
	CHECK(Trace(unknown, Ray(Vec3(-1, 0, 1), Vec3(1, 0, 1))).state == VisibilityState::Blocked);
	// An unsupported bounded object affects only boxes/rays intersecting its bound.
	for (auto p : {Vec3(5, 5, 5), Vec3(6, 6, 6)})
	{
		BakedVertex v;
		v.position = p;
		scene.surfaces.back().vertices.push_back(v);
	}
	auto bounded = scene.Build();
	CHECK(!bounded->Report().complete && bounded->Report().spatialCoverageComplete);
	CHECK(Trace(bounded, Ray(Vec3(-1, 0, 1), Vec3(-1, 1, 1))).state == VisibilityState::Visible);
	CHECK(Trace(bounded, Ray(Vec3(4, 5.5f, 5.5f), Vec3(7, 5.5f, 5.5f))).state ==
		  VisibilityState::Unknown);
	CHECK(bounded->CertificationKey() != unknown->CertificationKey());
	CHECK(bounded->CertificationKey() == scene.Build()->CertificationKey());
	// SAT must reject empty portions of a diagonal triangle AABB, never its interior.
	Scene diagonal;
	auto &s = diagonal.Quad(Vec3(0, 0, 0), Vec3(10, 0, 0), Vec3(0, 10, 0), Vec3(0, 10, 0));
	s.indices = {0, 1, 2};
	auto triangles = diagonal.Build();
	GeometryBoxResult box;
	Bounds b;
	b.known = true;
	b.minWorld = Vec3(8, 8, -.1f);
	b.maxWorld = Vec3(9, 9, .1f);
	CHECK(triangles->OverlapBox(b, Context(), box));
	CHECK(!box.intersects && box.coverageComplete);
	b.minWorld = Vec3(1, 1, -.001f);
	b.maxWorld = Vec3(2, 2, .001f);
	CHECK(triangles->OverlapBox(b, Context(), box));
	CHECK(box.intersects);
}
int main()
{
	RoomsAndPolicies();
	SkyAndBias();
	DomainsAndFailures();
	MeshAndPatch();
	ReferenceAndBenchmark();
	LocalCompleteness();
	std::printf("visibility: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
