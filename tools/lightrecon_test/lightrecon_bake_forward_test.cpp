// Numerical tests stay active in Release (no assert/NDEBUG dependency).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_forward.h"
#include "lightrecon/bake_trace.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace B = Rend2LightRecon::Core::Bake;
int failures = 0, checks = 0;
void Check(bool p, const char *name)
{
	++checks;
	if (!p)
	{
		++failures;
		std::printf("FAILED: %s\n", name);
	}
}
Vec3 V(float x, float y, float z)
{
	Vec3 v;
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
class TestVisibility : public VisibilityProvider
{
  public:
	VisibilityState state = VisibilityState::Visible;
	mutable unsigned calls = 0;
	Error Query(const RayQuery &, const ReadContext &, RayResult &o) const override
	{
		++calls;
		o.state = state;
		return Error();
	}
};
class Cancel : public Cancellation
{
  public:
	bool IsCancelled() const noexcept override { return true; }
};
struct Rig
{
	std::shared_ptr<std::vector<B::SourceHypothesis>> sources =
		std::make_shared<std::vector<B::SourceHypothesis>>(1);
	std::shared_ptr<std::vector<B::Receiver>> receivers = std::make_shared<std::vector<B::Receiver>>(1);
	B::Recipe recipe;
	B::Options options;
	B::EvaluationContext context;
	ReadContext read;
	B::Result result;
	B::CpuForwardEvaluator evaluator;
	Rig()
	{
		auto &s = (*sources)[0];
		s.id = 1;
		s.kind = B::SourceKind::Point;
		s.position.known = true;
		s.position.world = V(0, 0, 192);
		s.amplitudeKnown = true;
		s.compilerRgbAmplitude = V(300, 300, 300);
		auto &r = (*receivers)[0];
		r.sample = 2;
		r.world = V(0, 0, 0);
		r.geometricNormal = r.shadingNormal = V(0, 0, 1);
		r.mappedPlanar = r.compilerPlacementCertified = true;
		options.allowAssumed = true;
		options.externallyCertifiedVisibility = true;
		options.output = B::Domain::CompilerLinearRelative255;
		context.geometryKey[0] = 1;
	}
	Error Run()
	{
		return evaluator.Evaluate(B::NetRadiantProfile(recipe), Lease(sources), Lease(receivers),
								  Decode::Profile(), context, options, read, result);
	}
	const B::Prediction &P() { return result.predictions.data[0]; }
};
int main(int argc, char **argv)
{
	{
		SurfaceLightingObservation observation;
		observation.surface = 5;
		observation.materialIndex = 7;
		observation.logicalLightmap = 12;
		observation.positionKnown = observation.shadingNormalKnown = observation.normalOrientationKnown =
			true;
		observation.coverage = LuxelCoverage::Mapped;
		observation.positionWorld = V(.25, .25, 0);
		observation.geometricNormal = observation.shadingNormal = V(0, 0, 1);
		observation.mappingErrorBoundWorld = 1e-6;
		BakedSurface surface;
		surface.ordinal = 5;
		surface.materialIndex = 7;
		surface.motion = SurfaceMotion::StaticWorld;
		surface.kind = BakedSurfaceKind::Face;
		surface.lightmap[0] = 12;
		surface.styles[0] = 0;
		surface.materialFacts.cull = SurfaceCull::Front;
		surface.vertices.resize(3);
		surface.vertices[0].position = V(0, 0, 0);
		surface.vertices[1].position = V(1, 0, 0);
		surface.vertices[2].position = V(0, 1, 0);
		B::Receiver receiver;
		Check(bool(B::ReceiverFromLuxel(42, observation, surface, true, receiver)) && receiver.sample == 42 &&
				  receiver.surface == 6 && receiver.material == 8 && receiver.world.x == .25f,
			  "original mapped luxel geometry adapter without colour access");
		observation.coverage = LuxelCoverage::Ambiguous;
		Check(B::ReceiverFromLuxel(42, observation, surface, true, receiver).code == Code::Unavailable,
			  "overlapping chart not fabricated");
		observation.coverage = LuxelCoverage::Mapped;
		surface.motion = SurfaceMotion::BrushModel;
		Check(B::ReceiverFromLuxel(42, observation, surface, true, receiver).code == Code::Unavailable,
			  "moving receiver excluded");
		surface.motion = SurfaceMotion::StaticWorld;
		surface.kind = BakedSurfaceKind::Patch;
		Check(B::ReceiverFromLuxel(42, observation, surface, true, receiver).code == Code::Unavailable,
			  "curved mapping is explicitly unsupported by compiler subset");
		surface.kind = BakedSurfaceKind::Face;
		Check(bool(B::ReceiverFromLuxel(42, observation, surface, false, receiver)) &&
				  !B::Capability(B::NetRadiantProfile(), B::SourceKind::Point, receiver, B::Options())
					   .implemented,
			  "geometry mapping does not certify compiler sample placement");
	}
	{
		Rig t;
		auto a = (*t.sources)[0];
		a.compilerRgbAmplitude = V(300, 0, 0);
		(*t.sources)[0] = a;
		a.id = 9;
		a.compilerRgbAmplitude = V(0, 150, 0);
		t.sources->push_back(a);
		Check(bool(t.Run()) && t.P().rgb.x > 0 && std::abs(t.P().rgb.x - 2 * t.P().rgb.y) < 1e-4,
			  "independent RGB sources accumulate before encoding");
		auto first = t.P().rgb;
		std::reverse(t.sources->begin(), t.sources->end());
		Check(bool(t.Run()) && t.P().rgb.x == first.x && t.P().rgb.y == first.y,
			  "stable ID accumulation independent of source order");
		t.sources->clear();
		Check(bool(t.Run()) && t.P().available && t.P().rgb.x == 0,
			  "explicit empty lighting is a valid prediction, not reconstruction success");
		t.receivers->clear();
		Check(bool(t.Run()) && t.result.predictions.count == 0, "empty observation batch");
	}
	{
		Rig t;
		auto &s = (*t.sources)[0];
		s.position.world = V(0, 0, 1501);
		s.compilerRgbAmplitude = V(299, 299, 299);
		std::shared_ptr<const B::PreparedFactors> f;
		Check(bool(t.evaluator.Prepare(B::NetRadiantProfile(), Lease(t.sources), Lease(t.receivers),
									   t.context, t.options, t.read, f)),
			  "cache at FAST envelope");
		t.options.factors = f;
		Check(bool(t.Run()) && t.P().rgb.x == 0, "FAST weak source outside envelope");
		s.compilerRgbAmplitude = V(600, 600, 600);
		Check(bool(t.Run()) && t.P().rgb.x > 1, "amplitude change recomputes FAST envelope");
		t.context.geometryKey[0] = 2;
		Check(t.Run().code == Code::InvalidState, "geometry generation invalidates cache");
		t.context.geometryKey[0] = 1;
		(*t.receivers)[0].world.x = 1;
		Check(t.Run().code == Code::InvalidState, "receiver motion invalidates cache");
	}
	Rig r;
	Check(bool(r.Run()) && r.P().available && std::abs(r.P().rgb.x - 2250000.0 / (191 * 191)) < 1e-4,
		  "positive point / world distance");
	auto &s = (*r.sources)[0];
	auto &receiver = (*r.receivers)[0];
	receiver.shadingNormal = receiver.geometricNormal = V(0, 0, -1);
	Check(bool(r.Run()) && r.P().rgb.x == 0, "back-facing black");
	receiver.twoSided = true;
	Check(bool(r.Run()) && r.P().rgb.x > 0, "two sided response");
	receiver.twoSided = false;
	receiver.shadingNormal = receiver.geometricNormal = V(0, 0, 1);
	s.position.world = V(0, 0, 2);
	Check(bool(r.Run()) && std::abs(r.P().rgb.x - 2250000.0 / 256) < 1e-3, "minimum16 clamp");
	s.position.world = V(0, 0, 192);
	r.recipe.falloff = B::Falloff::Linear;
	r.recipe.normal = B::NormalResponse::None;
	Check(bool(r.Run()) && std::abs(r.P().rgb.x - 90.25) < 1e-5, "linear noangle");
	r.recipe = B::Recipe();
	auto visibility = std::make_shared<TestVisibility>();
	r.context.visibility = visibility;
	r.options.externallyCertifiedVisibility = false;
	visibility->state = VisibilityState::Unknown;
	Check(bool(r.Run()) && !r.P().available && r.P().status == B::Status::VisibilityUnknown,
		  "unknown is unavailable, not black");
	visibility->state = VisibilityState::Blocked;
	Check(bool(r.Run()) && r.P().available && r.P().rgb.x == 0, "known blocked");
	visibility->state = VisibilityState::Visible;
	Check(bool(r.Run()) && r.P().rgb.x > 0, "static visibility API used");
	std::shared_ptr<const B::PreparedFactors> factors;
	Check(bool(r.evaluator.Prepare(B::NetRadiantProfile(), Lease(r.sources), Lease(r.receivers), r.context,
								   r.options, r.read, factors)),
		  "prepare optional block");
	unsigned calls = visibility->calls;
	r.options.factors = factors;
	s.compilerRgbAmplitude = V(600, 0, 0);
	Check(bool(r.Run()) && r.P().rgb.x > 100 && r.P().rgb.y == 0 && visibility->calls == calls,
		  "amplitude change reuses visibility");
	s.position.world.x = 1;
	Check(r.Run().code == Code::InvalidState && !r.result.predictions.count,
		  "source motion invalidates factors");
	s.position.world.x = 0;
	r.options.factors.reset();
	r.options.maxPairs = 0;
	Check(r.Run().code == Code::WorkLimit, "pair budget");
	r.options.maxPairs = 1048576;
	r.read.maxBytes = 1;
	Check(r.Run().code == Code::MemoryLimit, "output memory limit");
	r.read.maxBytes = 0;
	Cancel cancel;
	r.read.cancellation = &cancel;
	Check(r.Run().code == Code::Cancelled, "cancel before work");
	r.read.cancellation = nullptr;
	s.compilerRgbAmplitude.x = std::numeric_limits<float>::infinity();
	Check(r.Run().code == Code::InvalidInput, "nonfinite source");
	s.compilerRgbAmplitude = V(300, 300, 300);
	B::Result unknown;
	Check(bool(r.evaluator.Evaluate(B::ProfileHypothesis(), Lease(r.sources), Lease(r.receivers),
									Decode::Profile(), r.context, r.options, r.read, unknown)) &&
			  !unknown.predictions.data[0].available,
		  "UnknownProfile has no numerical law");
	B::Prediction preflight;
	auto profile = B::NetRadiantProfile();
	auto fake = profile;
	fake.rules[size_t(B::Feature::Attenuation)].selector = "invented";
	Check(bool(B::Require(fake, r.options, preflight)) &&
			  !B::Capability(fake, B::SourceKind::Point, receiver, r.options).implemented,
		  "metadata is not numerical support");
	r.options.allowAssumed = false;
	Check(bool(r.Run()) && !r.P().available, "dataset applicability explicit");
	r.options.allowAssumed = true;
	r.options.required = {B::Feature::BounceFill};
	Check(bool(r.Run()) && !r.P().available, "bounce not silently direct only");
	r.options.required.clear();
	receiver.style = 2;
	Check(bool(r.Run()) && !r.P().available, "unsupported styles");
	receiver.style = 0;
	receiver.mappedPlanar = false;
	Check(bool(r.Run()) && !r.P().available, "unsupported receiver geometry");
	receiver.mappedPlanar = true;
	s.kind = B::SourceKind::Tube;
	Check(bool(r.Run()) && !r.P().available, "tube native unsupported");
	s.kind = B::SourceKind::Point;
	s.geometry.known = true;
	s.geometry.radiusWorld = -1;
	Check(r.Run().code == Code::InvalidInput, "negative geometry");
	s.geometry.radiusWorld = 0;
	r.options.output = B::Domain::EncodedRgb8BeforeQuantization;
	Vec3 encoded;
	uint8_t packed[3];
	Check(bool(B::Encode(profile, V(510, 255, 0), encoded, packed)) && packed[0] == 255 && packed[1] == 127 &&
			  encoded.y == 127.5f,
		  "shared peak / truncation");
	Check(bool(B::Encode(profile, V(255, 0, 0), encoded, packed)) && packed[0] == 255, "censored endpoint");
	Check(!B::Encode(profile, V(-1, 0, 0), encoded, packed), "negative encoding rejected");
	r.options.output = B::Domain::GridAmbientDirected;
	Check(bool(r.Run()) && !r.P().available, "final grid merge unsupported");
	r.options.unpackedGridOnly = true;
	Check(bool(r.Run()) && r.P().available && r.P().directionKnown && r.P().gridAmbient.x < 1e-6 &&
			  r.P().gridDirected.x > 0,
		  "unpacked grid separate reduction");
	r.options.output = B::Domain::DeluxeWorldDirection;
	Check(bool(r.Run()) && r.P().available && r.P().deluxeKnown && r.P().directionToSource.z > .999,
		  "deluxe separate operator");
	r.options.output = B::Domain::CompilerLinearRelative255;
	s.kind = B::SourceKind::Spot;
	s.orientation.known = true;
	s.orientation.forward = V(0, 0, -1);
	s.orientation.up = V(0, -1, 0);
	s.spotTargetDistanceWorld = 128;
	s.spotRadiusWorld = 64;
	Check(bool(r.Run()) && r.P().available && r.P().rgb.x > 0, "positive spot");
	s.spotTargetDistanceWorld = 0;
	Check(r.Run().code == Code::InvalidInput, "invalid spot axis geometry");
	s.kind = B::SourceKind::Point;
	// SI irradiance reference over [-1,1]^2, source (0,0,4) metres: I*Omega/A.
	r.options = B::Options();
	r.options.mode = B::Mode::PhysicalIntegral;
	r.options.quadratureDepth = 7;
	r.options.integralTolerance = 1e-6;
	r.options.maxWork = 1000000;
	auto footprint = std::make_shared<std::vector<B::FootprintTriangle>>(2);
	(*footprint)[0].world[0] = V(-1, -1, 0);
	(*footprint)[0].world[1] = V(1, -1, 0);
	(*footprint)[0].world[2] = V(1, 1, 0);
	(*footprint)[1].world[0] = V(-1, -1, 0);
	(*footprint)[1].world[1] = V(1, 1, 0);
	(*footprint)[1].world[2] = V(-1, 1, 0);
	for (auto &t : *footprint)
		for (auto &n : t.normal)
			n = V(0, 0, 1);
	receiver.footprint = Lease(footprint);
	s.position.world = V(0, 0, 4);
	s.worldUnits.worldUnitsPerMeter = 1;
	s.amplitudeDomain = B::AmplitudeDomain::CanonicalPhotometry;
	s.angular.kind = AngularKind::Isotropic;
	s.calibration.id = 3;
	s.calibration.kind = CalibrationKind::PhysicallyAuthored;
	s.canonicalEmission.calibration = 3;
	s.canonicalEmission.unit = PhotometricUnit::Candela;
	s.canonicalEmission.intensity.known = true;
	s.canonicalEmission.intensity.value = 100;
	s.canonicalEmission.color.known = true;
	s.canonicalEmission.color.rgb = V(1, 1, 1);
	auto scene = std::make_shared<B::IntegralScene>();
	scene->complete = true;
	r.context.integralScene = scene;
	double expected = 100 * std::atan(1 / (4 * std::sqrt(18.0)));
	Check(bool(r.Run()) && r.P().available && r.P().unit == PhotometricUnit::Lux &&
			  std::abs(r.P().rgb.x - expected) < 1e-4,
		  "SI footprint versus independent solid angle");
	double physicalFull = r.P().rgb.x;
	s.worldUnits.physicalToScene = 100;
	Check(bool(r.Run()) && r.P().rgb.x == physicalFull, "SI reference ignores presentation scale");
	r.options.mode = B::Mode::CompilerFaithful;
	r.options.allowAssumed = true;
	r.options.output = B::Domain::CompilerLinearRelative255;
	Check(r.Run().code == Code::Unavailable, "no implicit candela to compiler amplitude conversion");
	s.compilerParametersPerCanonicalUnit.known = true;
	s.compilerParametersPerCanonicalUnit.value = 3;
	s.calibrationReference = "authored SI fixture reference";
	s.calibrationMethod = "explicit example gain, not measured JA scale";
	Check(bool(r.Run()) && r.P().available && r.P().unit == PhotometricUnit::Relative,
		  "explicit physical to legacy mapping remains relative");
	s.canonicalEmission.unit = PhotometricUnit::Lux;
	Check(r.Run().code == Code::InvalidInput, "wrong photometric unit rejected");
	s.canonicalEmission.unit = PhotometricUnit::Candela;
	r.options.mode = B::Mode::PhysicalIntegral;
	double full = physicalFull;
	Check(bool(r.Run()) && r.P().available, "return to independent SI mode");
	full = r.P().rgb.x;
	auto casters = std::make_shared<std::vector<B::TracePrimitive>>(2);
	(*casters)[0].triangle.verticesWorld[0] = V(0, -4, 2);
	(*casters)[0].triangle.verticesWorld[1] = V(0, 4, 2);
	(*casters)[0].triangle.verticesWorld[2] = V(4, 4, 2);
	(*casters)[1].triangle.verticesWorld[0] = V(0, -4, 2);
	(*casters)[1].triangle.verticesWorld[1] = V(4, 4, 2);
	(*casters)[1].triangle.verticesWorld[2] = V(4, -4, 2);
	for (auto &t : *casters)
		t.optical = ObstacleKind::Opaque;
	scene->triangles = Lease(casters);
	Check(bool(r.Run()) && r.P().available && std::abs(r.P().rgb.x - full * .5) < 1e-4 &&
			  std::abs(r.P().integratedVisibility.value - .5) < 1e-5,
		  "boundary-clipped half footprint");
	double halfValue = r.P().rgb.x;
	uint64_t halfQuadrature = r.P().work.quadrature;
	// Moving the source shifts the clipped footprint. A fixed four-ray mask
	// remains unchanged across these steps and misses the visibility derivative.
	double step = .002;
	s.position.world.x = float(-step);
	Check(bool(r.Run()) && r.P().available, "negative motion integral");
	double vl = r.P().integratedVisibility.value, il = r.P().rgb.x;
	s.position.world.x = float(step);
	Check(bool(r.Run()) && r.P().available, "positive motion integral");
	double vr = r.P().integratedVisibility.value, ir = r.P().rgb.x;
	auto exact = [&](double sx) {
		double boundary = -sx;
		auto corner = [&](double x, double y) {
			double d = x - sx;
			return std::atan2(d * y, 4 * std::sqrt(16 + d * d + y * y));
		};
		return 25 * (corner(boundary, 1) - corner(-1, 1) - corner(boundary, -1) + corner(-1, -1));
	};
	Check(std::abs((vr - vl) / (2 * step) + .5) < 1e-4, "moving-boundary integrated visibility derivative");
	Check(std::abs((ir - il) / (2 * step) - (exact(step) - exact(-step)) / (2 * step)) < .002,
		  "integrated irradiance derivative versus independent solid angle");
	int fixedLeft = 0, fixedRight = 0;
	for (double x : {-.5, .5})
		for (int y = 0; y < 2; ++y)
		{
			fixedLeft += x < step;
			fixedRight += x < -step;
		}
	Check(fixedLeft == fixedRight && vl != vr, "fixed 2x2 zero-gradient failure is explicit");
	s.position.world.x = 0;
	(*casters)[0].optical = ObstacleKind::AlphaTested;
	Check(bool(r.Run()) && !r.P().available && r.P().status == B::Status::VisibilityUnknown,
		  "uncertain footprint blocker");
	scene->complete = false;
	Check(bool(r.Run()) && !r.P().available, "missing integral geometry");
	scene->complete = true;
	scene->triangles = Chunk<B::TracePrimitive>();
	s.kind = B::SourceKind::Polygon;
	s.canonicalEmission.unit = PhotometricUnit::Nits;
	s.angular.kind = AngularKind::Lambertian;
	s.canonicalEmission.intensity.value = 1;
	s.geometry.known = true;
	s.geometry.halfWidthWorld = s.geometry.halfHeightWorld = 1;
	s.orientation.known = true;
	s.orientation.right = V(1, 0, 0);
	s.orientation.up = V(0, -1, 0);
	s.orientation.forward = V(0, 0, -1);
	r.options.integralTolerance = 1e-4; // explicitly coarse finite-source quadrature, not compiler replay
	auto rectangleStatus = r.Run();
	if (!rectangleStatus)
		std::printf("rectangle diagnostic: %s\n", rectangleStatus.message.c_str());
	else
		std::printf("rectangle SI lux: %.9g\n", r.P().rgb.x);
	double rectangleLux = rectangleStatus ? r.P().rgb.x : 0;
	Check(bool(rectangleStatus) && r.P().available && r.P().rgb.x > .15 && r.P().rgb.x < .25 &&
			  !r.P().numericalError.known,
		  "finite SI rectangle, source quadrature error not falsely bounded");
	s.geometry.halfWidthWorld = 0;
	Check(r.Run().code == Code::InvalidInput, "degenerate physical rectangle rejected");
	// Restricted compiler replay must not infer complete geometry or sky.
	BspSpaceInput bsp;
	SpatialPlane plane;
	plane.normal = V(1, 0, 0);
	bsp.planes.push_back(plane);
	SpatialNode root;
	root.children[0] = -1;
	root.children[1] = -2;
	bsp.nodes.push_back(root);
	SpatialLeaf free;
	free.cluster = 0;
	bsp.leaves = {free, free};
	auto traceTriangles = std::make_shared<std::vector<B::TracePrimitive>>(1);
	auto &tt = (*traceTriangles)[0];
	tt.optical = ObstacleKind::Opaque;
	tt.triangle.verticesWorld[0] = V(0, -2, -2);
	tt.triangle.verticesWorld[1] = V(0, 2, -2);
	tt.triangle.verticesWorld[2] = V(0, 0, 2);
	B::TraceLimits limits;
	std::shared_ptr<const B::CompilerTrace> trace;
	Check(B::CompilerTrace::Build(bsp, Lease(traceTriangles), Disk::Digest{}, limits, ReadContext(), trace)
				  .code == Code::Unavailable,
		  "trace coverage assertion mandatory");
	limits.completeOpaqueSkyWorldGeometry = true;
	Check(bool(B::CompilerTrace::Build(bsp, Lease(traceTriangles), Disk::Digest{}, limits, ReadContext(),
									   trace)),
		  "bounded compiler trace build");
	RayResult hit;
	Check(bool(trace->Query(V(-1, 0, 0), V(1, 0, 0), false, ReadContext(), 10000, hit)) &&
			  hit.state == VisibilityState::Blocked,
		  "opaque compiler trace wall");
	Check(bool(trace->Query(V(1, 0, 0), V(1, 0, 4), true, ReadContext(), 10000, hit)) &&
			  hit.state == VisibilityState::Unknown,
		  "empty space does not invent sky");
	Disk::Digest oldKey = trace->Key();
	bsp.planes[0].distance = .125f;
	Check(bool(B::CompilerTrace::Build(bsp, Lease(traceTriangles), Disk::Digest{}, limits, ReadContext(),
									   trace)) &&
			  oldKey != trace->Key(),
		  "BSP planes invalidate trace identity");
	tt.optical = ObstacleKind::AlphaTested;
	Check(B::CompilerTrace::Build(bsp, Lease(traceTriangles), Disk::Digest{}, limits, ReadContext(), trace)
				  .code == Code::Unavailable,
		  "compiler trace unknown alpha cannot become opaque/transparent");
	if (argc == 2)
	{
		std::ofstream report(argv[1]);
		report << std::setprecision(12)
			   << "{\"schema\":\"org.openjk.bake-integral-check.v2\",\"checks\":" << checks
			   << ",\"failures\":" << failures
			   << ",\"point_centre_lux\":6.25,\"point_integrated_lux\":" << physicalFull
			   << ",\"point_analytic_lux\":" << expected << ",\"half_integrated_lux\":" << halfValue
			   << ",\"half_quadrature_responses\":" << halfQuadrature
			   << ",\"visibility_derivative_fd\":" << (vr - vl) / (2 * step)
			   << ",\"visibility_derivative_analytic\":-0.5,\"irradiance_derivative_fd\":"
			   << (ir - il) / (2 * step)
			   << ",\"irradiance_derivative_analytic_fd\":" << (exact(step) - exact(-step)) / (2 * step)
			   << ",\"fixed_2x2_visibility_derivative\":0,\"finite_rectangle_lux\":" << rectangleLux
			   << ",\"finite_rectangle_total_error_bound\":null}\n";
		if (!report)
			return 2;
	}
	std::printf("bake forward: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
