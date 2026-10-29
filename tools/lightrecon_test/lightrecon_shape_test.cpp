// Owned CPU geometry/photometry fixtures. Truth is held by this controller only.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/emitter_shape.h"
#include "lightrecon/geometry_key.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace S = Rend2LightRecon::Core::Shape;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                             \
	do                                                                                                       \
	{                                                                                                        \
		++checks;                                                                                            \
		if (!(x))                                                                                            \
		{                                                                                                    \
			++failures;                                                                                      \
			std::printf("FAIL %d %s\n", __LINE__, #x);                                                       \
		}                                                                                                    \
	} while (0)
void Must(Error e)
{
	CHECK(e);
	if (!e)
		throw std::runtime_error(e.message);
}
Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
ReadContext Read()
{
	ReadContext r;
	r.maxBytes = 64ull * 1024 * 1024;
	return r;
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
Bounds Box(V a, V b)
{
	Bounds v;
	v.known = true;
	v.minWorld = a.Float();
	v.maxWorld = b.Float();
	return v;
}
Frame FrameFor(V forward, V right = V(1, 0, 0))
{
	Frame f;
	f.known = true;
	f.forward = Unit(forward).Float();
	f.right = Unit(right - Unit(forward) * Dot(Unit(forward), right)).Float();
	f.up = Cross(V(f.forward), V(f.right)).Float();
	return f;
}
class Domain final : public RegionDomain
{
  public:
	bool wall = false;
	Error Locate(Vec3 p, SpatialLocation &o, double tolerance = 1e-5) const override
	{
		o = {};
		o.work = 1;
		V v(p);
		o.space = std::max(std::abs(v.x), std::abs(v.y)) >= 400 || v.z < -.01 || v.z > 500
					  ? SpaceClass::Outside
					  : SpaceClass::Free;
		if (wall && std::abs(v.x) <= 1 + tolerance && v.y < 30 && v.z < 100)
			o.space = SpaceClass::Solid;
		return {};
	}
	Error ClassifyBox(const Bounds &b, SpaceClass &o, uint64_t &work) const override
	{
		work = 1;
		V a(b.minWorld), v(b.maxWorld);
		o = SpaceClass::Free;
		if (a.x <= -400 || a.y <= -400 || a.z <= 0 || v.x >= 400 || v.y >= 400 || v.z >= 500)
			o = SpaceClass::Unknown;
		if (wall && a.x <= 1 && v.x >= -1 && a.y < 30 && a.z < 100)
			o = SpaceClass::Unknown;
		return {};
	}
};
class Stream final : public ChunkSource<S::Observation>
{
  public:
	std::shared_ptr<std::vector<S::Observation>> data = std::make_shared<std::vector<S::Observation>>();
	mutable uint64_t calls = 0;
	bool corrupt = false;
	Error Read(uint64_t f, size_t n, const ReadContext &c, Chunk<S::Observation> &o) const override
	{
		++calls;
		if (c.cancellation && c.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Fixture cancelled");
		if (corrupt)
			return Failure(Code::ProviderError, "Corrupt synthetic stream");
		if (f > data->size() || n > data->size() - f)
			return Failure(Code::ProviderError, "Fixture range");
		o = VectorChunk<S::Observation>(data, size_t(f), n);
		return {};
	}
};
struct Scene
{
	bool levels = false;
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::shared_ptr<const StaticVisibility> visibility;
	LightmapMapIdentity map;
	Bake::EvaluationContext context;
	PointRefine::SceneInput placement;
	std::vector<BakedSurface> surfaces;
	void Quad(V a, V b, V c, V d, uint32_t signal = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.material = "owned-s08";
		s.materialFacts.available = true;
		s.materialFacts.cull = SurfaceCull::None;
		s.materialFacts.signals = signal;
		for (auto p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p.Float();
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	Scene(bool wall = false, bool unknown = false, bool platforms = false) : levels(platforms)
	{
		domain->wall = wall;
		Quad(V(-400, -400, 0), V(400, -400, 0), V(400, 400, 0), V(-400, 400, 0));
		if (wall)
			Quad(V(0, -400, 0), V(0, 30, 0), V(0, 30, 100), V(0, -400, 100),
				 unknown ? uint32_t(MaterialCutout) : 0u);
		if (levels)
		{
			Quad(V(-40, -40, 40), V(0, -40, 40), V(0, 40, 40), V(-40, 40, 40));
			Quad(V(0, -40, 70), V(40, -40, 70), V(40, 40, 70), V(0, 40, 70));
		}
		GeometryKey k;
		for (auto &s : surfaces)
		{
			k.U(s.materialFacts.signals);
			for (auto &v : s.vertices)
				k.V(v.position);
		}
		map.path = "maps/s08-owned.bsp";
		map.sha256 = k.Finish();
		map.bytes = 1;
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Must(capture.SetMap(map));
		for (auto &s : surfaces)
			Must(capture.Add(s));
		Must(capture.Freeze());
		Must(StaticVisibility::Build(capture.Acquire(), {}, Read(), visibility, domain));
		context.visibility = visibility;
		context.geometryKey = visibility->CertificationKey();
		context.materialKey = Key("owned-known-material");
		context.observationPolicyKey = Key("s08-test-policy");
		placement.map = map;
		placement.domain = domain;
		placement.domainKey = Key("s08-owned-analytic-domain-v1");
		placement.geometry = visibility;
	}
};
S::Model Model(bool historical = false)
{
	S::Model m;
	m.law = historical ? S::Law::HistoricalBake : S::Law::DiffuseIntegral;
	m.output = S::Output::RelativeIncident;
	m.provenance = "Owned explicit test hypothesis; no Raven identification";
	m.allowAssumed = true;
	m.allowFacetExpansion = true;
	m.linearRec709PrimariesAttested = true;
	m.units.worldUnitsPerMeter = 1;
	Bake::Recipe p;
	p.fast = false;
	p.receiverPushWorld = 1; // actual declared compiler sample, clear of source receiver plane
	m.profile = Bake::NetRadiantProfile(p);
	return m;
}
S::Emitter Emitter(EmitterKind kind, Id id = 1)
{
	S::Emitter e;
	e.geometry.source.id = id;
	e.geometry.kind = kind;
	e.geometry.position.known = true;
	e.geometry.position.world = Vec3(0, 0, 120);
	e.geometry.frame = FrameFor(V(0, 0, -1));
	e.geometry.angular.kind =
		kind == EmitterKind::Point
			? AngularKind::Isotropic
			: kind == EmitterKind::Spot ? AngularKind::SmoothCone : AngularKind::Lambertian;
	e.geometry.angular.cosOuter = float(std::cos(.5));
	e.geometry.angular.cosInner = float(std::cos(.3));
	e.geometry.geometry.known = true;
	e.geometry.geometry.halfWidthWorld = 32;
	e.geometry.geometry.halfHeightWorld = 16;
	e.geometry.geometry.halfLengthWorld = 48;
	e.geometry.geometry.radiusWorld = 5;
	e.rgbAmplitude =
		kind == EmitterKind::Point || kind == EmitterKind::Spot ? Vec3(40000, 16000, 8000) : Vec3(20, 8, 4);
	e.spotTargetDistanceWorld = 128;
	e.spotRadiusWorld = std::tan(.5) * 128 - 16;
	return e;
}
S::Sampling Sampling()
{
	S::Sampling s;
	s.areaDivisions = 4;
	s.tubeSides = 12;
	s.tubeSegments = 4;
	s.receiverOffsetWorld = .001;
	s.compareRefined = false;
	s.maxRays = 3000000;
	return s;
}
std::shared_ptr<std::vector<Bake::Receiver>> ReceiverSet(unsigned fold, bool above = false)
{
	auto v = std::make_shared<std::vector<Bake::Receiver>>();
	for (unsigned i = 0; i < 7; ++i)
		for (unsigned j = 0; j < 5; ++j)
		{
			Bake::Receiver r;
			r.sample = 1 + fold * 1000 + i * 10 + j;
			r.surface = 1;
			r.world = Vec3(-133 + 40.f * i + float(fold) * 9, -108 + 48.f * j + float(fold) * 9,
						   above ? 240.f : 0.f);
			r.shadingNormal = r.geometricNormal = above ? Vec3(0, 0, -1) : Vec3(0, 0, 1);
			r.mappedPlanar = r.compilerPlacementCertified = true;
			v->push_back(r);
		}
	return v;
}
std::shared_ptr<Stream> Data(const Scene &scene, const S::Model &model, const std::vector<S::Emitter> &truth,
							 unsigned fold, bool above = false, bool encoded = false)
{
	auto receivers = ReceiverSet(fold, above);
	if (scene.levels)
		for (unsigned platform = 0; platform < 2; ++platform)
			for (unsigned a = 0; a < 3; ++a)
				for (unsigned b = 0; b < 4; ++b)
				{
					Bake::Receiver r;
					r.sample = fold * 1000 + 100 + platform * 30 + a * 5 + b;
					r.surface = 2 + platform;
					r.world = Vec3((platform ? 6.f : -34.f) + a * 10 + fold * 2, -30.f + b * 18 + fold * 2,
								   platform ? 70.f : 40.f);
					r.shadingNormal = r.geometricNormal = Vec3(0, 0, 1);
					r.mappedPlanar = r.compilerPlacementCertified = true;
					receivers->push_back(r);
				}
	auto sampling = Sampling();
	sampling.areaDivisions = 12;
	sampling.tubeSides = 48;
	sampling.tubeSegments = 12;
	if (truth[0].geometry.kind == EmitterKind::Tube && truth[0].geometry.position.world.z < 40)
	{
		sampling.tubeSides = 64;
		sampling.tubeSegments = 32;
	}
	std::shared_ptr<const S::Block> block;
	Must(S::Prepare(model, truth, Lease(receivers), scene.context, sampling, Read(), block));
	std::vector<Vec3> rgb;
	for (auto &e : truth)
		rgb.push_back(e.rgbAmplitude);
	std::vector<S::Prediction> predictions;
	S::Work work;
	Must(block->Evaluate(rgb, Read(), predictions, work));
	auto stream = std::make_shared<Stream>();
	for (size_t i = 0; i < receivers->size(); ++i)
	{
		CHECK(predictions[i].available);
		S::Observation a;
		a.id = (*receivers)[i].sample;
		a.spatialBlock = 1 + i / 5;
		a.receiver = (*receivers)[i];
		a.comparable = true;
		a.area = 64;
		a.role = fold == 0 ? S::Role::Training : fold == 1 ? S::Role::Validation : S::Role::Heldout;
		for (int c = 0; c < 3; ++c)
		{
			double x = V(predictions[i].value)[c];
			if (encoded)
				a.channel[c] = Amplitude::Rgb8(uint8_t(Clamp(x, 0, 255)));
			else
				a.channel[c].lower = a.channel[c].upper = x;
		}
		stream->data->push_back(a);
	}
	return stream;
}
S::Candidate Candidate(S::Emitter emitter)
{
	S::Candidate c;
	c.initial = emitter;
	c.centreBounds = Box(V(-160, -160, 40), V(160, 160, 200));
	c.maximumTranslationWorld = 24;
	c.maximumRotationRadians = .4;
	c.minimumDimensionWorld = 2;
	c.maximumDimensionWorld = 100;
	c.provenance = "Declared fixture candidate prior";
	c.evidence = S::ShapeEvidence::Inferred;
	c.upperRgb = Vec3(100000, 100000, 100000);
	return c;
}
S::Request Request(const Scene &scene, const S::Model &model, std::shared_ptr<Stream> data,
				   const std::vector<S::Candidate> &candidates)
{
	S::Request r;
	r.model = model;
	r.training = data;
	r.count = data->data->size();
	r.candidates = candidates;
	r.geometry = scene.context;
	r.placement = scene.placement;
	r.mapKey = scene.map.sha256;
	r.observationsKey = Key("s08-owned-training-partition-v1");
	r.searchSampling = Sampling();
	r.strictSampling = Sampling();
	r.strictSampling.compareRefined = true;
	r.options.maxSweeps = 100;
	r.options.maxTrials = 3000;
	r.options.maxValues = 2000000;
	r.options.maxAmplitudeIterations = 100;
	r.options.huber = 100000;
	r.options.relativeImprovement = 1e-12;
	r.options.minimumStepFraction = .02;
	r.options.maxRays = 100000000;
	r.options.maxWork = 1000000000;
	return r;
}
struct Row
{
	std::string name, stop;
	std::string sources;
	double before = 0, after = 0, train = 0, validation = 0, heldout = 0, black = 0, seconds = 0, angular = 0,
		   size = 0;
	uint64_t trials = 0, rays = 0, values = 0;
	bool converged = false;
};
std::vector<Row> records;
double Seconds(std::chrono::steady_clock::time_point t)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}
S::FitResult Run(const std::string &name, const Scene &scene, const S::Model &model,
				 const std::vector<S::Emitter> &truth, std::vector<S::Candidate> candidates, bool top = false)
{
	auto train = Data(scene, model, truth, 0), validation = Data(scene, model, truth, 1),
		 heldout = Data(scene, model, truth, 2);
	if (top)
	{
		auto a = Data(scene, model, truth, 0, true), b = Data(scene, model, truth, 1, true),
			 c = Data(scene, model, truth, 2, true);
		for (auto s : {a, b, c})
			for (auto &x : *s->data)
				x.id = x.receiver.sample += 500;
		train->data->insert(train->data->end(), a->data->begin(), a->data->end());
		validation->data->insert(validation->data->end(), b->data->begin(), b->data->end());
		heldout->data->insert(heldout->data->end(), c->data->begin(), c->data->end());
	}
	auto request = Request(scene, model, train, candidates);
	if (name == "near_field_tube")
	{
		request.searchSampling.tubeSides = 32;
		request.searchSampling.tubeSegments = 16;
		request.strictSampling = request.searchSampling;
		request.strictSampling.compareRefined = true;
	}
	auto start = std::chrono::steady_clock::now();
	S::FitResult out;
	auto e = S::Fit(request, Read(), out);
	if (!e)
		std::printf("%s fit error: %s\n", name.c_str(), e.message.c_str());
	Must(e);
	CHECK(out.fitted && out.strictReevaluated && out.emitters.size() == truth.size());
	S::Score v, h;
	Must(S::EvaluateFrozen(out, validation, validation->data->size(), S::Role::Validation, scene.context,
						   request.strictSampling, 1e-7, Read(), v));
	Must(S::EvaluateFrozen(out, heldout, heldout->data->size(), S::Role::Heldout, scene.context,
						   request.strictSampling, 1e-7, Read(), h));
	CHECK(v.available && h.available);
	Row r;
	r.name = name;
	r.stop = S::StopName(out.stop);
	r.converged = out.converged;
	r.train = out.objective;
	r.validation = v.intervalMse;
	r.heldout = h.intervalMse;
	r.black = h.blackMse;
	r.trials = out.trials;
	r.values = out.work.values;
	r.rays = out.work.rays;
	r.seconds = Seconds(start);
	std::ostringstream details;
	details << std::setprecision(12) << '[';
	auto vector = [&](Vec3 v) { details << '[' << v.x << ',' << v.y << ',' << v.z << ']'; };
	double sumBefore = 0, sumAfter = 0;
	for (size_t i = 0; i < truth.size(); ++i)
	{
		auto p = truth[i].geometry.position.world;
		sumBefore += std::pow(Length(V(p) - V(candidates[i].initial.geometry.position.world)), 2);
		sumAfter += std::pow(Length(V(p) - V(out.emitters[i].geometry.position.world)), 2);
		r.angular = std::max(r.angular, std::acos(Clamp(Dot(V(truth[i].geometry.frame.forward),
															V(out.emitters[i].geometry.frame.forward)),
														-1, 1)));
		CHECK(out.emitters[i].geometry.source.id == truth[i].geometry.source.id &&
			  Positive(out.emitters[i].rgbAmplitude));
		if (i)
			details << ',';
		auto &fitted = out.emitters[i];
		auto &original = candidates[i].initial;
		details << "{\"id\":" << fitted.geometry.source.id << ",\"kind\":" << unsigned(fitted.geometry.kind)
				<< ",\"xyz\":";
		vector(fitted.geometry.position.world);
		details << ",\"initial_xyz\":";
		vector(original.geometry.position.world);
		details << ",\"truth_xyz\":";
		vector(truth[i].geometry.position.world);
		details << ",\"right\":";
		vector(fitted.geometry.frame.right);
		details << ",\"forward\":";
		vector(fitted.geometry.frame.forward);
		details << ",\"rgb\":";
		vector(fitted.rgbAmplitude);
		details << ",\"truth_rgb\":";
		vector(truth[i].rgbAmplitude);
		auto dims = [&](const S::Emitter &e) {
			auto &d = e.geometry.geometry;
			details << '[' << d.halfWidthWorld << ',' << d.halfHeightWorld << ',' << d.halfLengthWorld << ','
					<< d.radiusWorld << ']';
		};
		details << ",\"dimensions\":";
		dims(fitted);
		details << ",\"truth_dimensions\":";
		dims(truth[i]);
		details << ",\"cos_inner\":" << fitted.geometry.angular.cosInner
				<< ",\"cos_outer\":" << fitted.geometry.angular.cosOuter
				<< ",\"legacy_spot_radius\":" << fitted.spotRadiusWorld
				<< ",\"angular_profile\":" << unsigned(fitted.geometry.angular.kind)
				<< ",\"canonical_emission_unit\":" << unsigned(fitted.geometry.emission.unit)
				<< ",\"canonical_intensity_known\":"
				<< (fitted.geometry.emission.intensity.known ? "true" : "false") << '}';
	}
	details << ']';
	r.sources = details.str();
	r.before = std::sqrt(sumBefore / truth.size());
	r.after = std::sqrt(sumAfter / truth.size());
	records.push_back(r);
	std::printf("%s: XYZ %.5g -> %.5g; angle %.5g; heldout %.5g black %.5g; %llu trials %llu rays %.2fs %s\n",
				name.c_str(), r.before, r.after, r.angular, r.heldout, r.black, (unsigned long long)r.trials,
				(unsigned long long)r.rays, r.seconds, r.stop.c_str());
	return out;
}
void ReferenceTests()
{
	Scene scene;
	auto m = Model();
	auto point = Emitter(EmitterKind::Point);
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
	auto r = (*ReceiverSet(0))[17];
	r.world = Vec3();
	receivers->push_back(r);
	std::shared_ptr<const S::Block> block;
	auto sample = Sampling();
	Must(S::Prepare(m, {point}, Lease(receivers), scene.context, sample, Read(), block));
	std::vector<S::Prediction> result;
	S::Work work;
	Must(block->Evaluate({point.rgbAmplitude}, Read(), result, work));
	CHECK(std::abs(result[0].value.x - 40000. / (120 * 120)) < 1e-6);
	auto spot = Emitter(EmitterKind::Spot);
	Must(S::Prepare(m, {spot}, Lease(receivers), scene.context, sample, Read(), block));
	Must(block->Evaluate({spot.rgbAmplitude}, Read(), result, work));
	CHECK(std::abs(result[0].value.x - 40000. / 14400) < 1e-6);
	auto panel = Emitter(EmitterKind::Rectangle);
	sample.areaDivisions = 16;
	sample.compareRefined = true;
	Must(S::Prepare(m, {panel}, Lease(receivers), scene.context, sample, Read(), block));
	Must(block->Evaluate({panel.rgbAmplitude}, Read(), result, work));
	// Independent exact solid-angle edge integral for parallel aligned rectangle.
	double a = 32, b = 16, d = 120;
	double exact = 2 *
				   ((a / std::sqrt(a * a + d * d)) * std::atan(b / std::sqrt(a * a + d * d)) +
					(b / std::sqrt(b * b + d * d)) * std::atan(a / std::sqrt(b * b + d * d))) *
				   20;
	CHECK(std::abs(result[0].value.x - exact) < .0005);
	CHECK(result[0].quadratureDifference < .002);
	(*receivers)[0].world = Vec3(0, 0, 240);
	(*receivers)[0].shadingNormal = (*receivers)[0].geometricNormal = Vec3(0, 0, -1);
	Must(S::Prepare(m, {panel}, Lease(receivers), scene.context, sample, Read(), block));
	Must(block->Evaluate({panel.rgbAmplitude}, Read(), result, work));
	CHECK(result[0].value.x == 0);
	panel.geometry.angular.twoSided = true;
	Must(S::Prepare(m, {panel}, Lease(receivers), scene.context, sample, Read(), block));
	Must(block->Evaluate({panel.rgbAmplitude}, Read(), result, work));
	CHECK(result[0].value.x > 0);
	auto tube = Emitter(EmitterKind::Tube);
	(*receivers)[0].world = Vec3(0, 0, -10000);
	(*receivers)[0].shadingNormal = (*receivers)[0].geometricNormal = Vec3(0, 0, 1);
	// Domain is bounded; test far field with a separately certified empty visibility provider below.
	(*receivers)[0].world = Vec3(0, 0, 1);
	tube.geometry.position.world = Vec3(0, 0, 400);
	tube.geometry.geometry.halfLengthWorld = 4;
	tube.geometry.geometry.radiusWorld = .5;
	sample.tubeSides = 64;
	sample.tubeSegments = 8;
	Must(S::Prepare(m, {tube}, Lease(receivers), scene.context, sample, Read(), block));
	Must(block->Evaluate({tube.rgbAmplitude}, Read(), result, work));
	double far = 20 * 4 * .5 * 4 / (399. * 399.);
	CHECK(std::abs(result[0].value.x / far - 1) < .01);
	auto historical = Model(true);
	auto hp = Emitter(EmitterKind::Point);
	hp.rgbAmplitude = Vec3(300, 100, 40);
	(*receivers)[0].world = Vec3();
	(*receivers)[0].shadingNormal = (*receivers)[0].geometricNormal = Vec3(0, 0, 1);
	Must(S::Prepare(historical, {hp}, Lease(receivers), scene.context, Sampling(), Read(), block));
	Must(block->Evaluate({hp.rgbAmplitude}, Read(), result, work));
	CHECK(result[0].available);
	CHECK(std::abs(result[0].value.x - 300 * 7500. / (119 * 119)) < 1e-4);
	historical.profile = {};
	CHECK(S::Prepare(historical, {hp}, Lease(receivers), scene.context, Sampling(), Read(), block).code ==
		  Code::Unavailable);
}
void FittingTests()
{
	Scene scene;
	auto model = Model();
	{
		auto truth = Emitter(EmitterKind::Point);
		auto c = Candidate(truth);
		c.initial.geometry.position.world = Vec3(8, -8, 128);
		c.initial.rgbAmplitude = Vec3(20000, 10000, 4000);
		c.fitFrame = c.fitCone = c.fitDimensions = false;
		auto out = Run("true_point", scene, model, {truth}, {c});
		CHECK(records.back().after < records.back().before * .5);
		CHECK(records.back().heldout < records.back().black * .01);
	}
	{
		auto truth = Emitter(EmitterKind::Spot);
		Scene multiHeight(false, false, true);
		auto c = Candidate(truth);
		c.initial.geometry.frame = FrameFor(Unit(V(.1, 0, -1)));
		c.initial.geometry.angular.cosOuter = float(std::cos(.6));
		c.initial.geometry.angular.cosInner = float(std::cos(.35));
		c.initial.geometry.position.world = Vec3(4, 0, 124);
		c.initial.rgbAmplitude = Vec3(30000, 12000, 6000);
		auto out = Run("narrow_spot", multiHeight, model, {truth}, {c});
		CHECK(records.back().angular < .06);
		CHECK(records.back().after < records.back().before * .5);
		CHECK(records.back().heldout < records.back().black * .01);
	}
	{
		auto truth = Emitter(EmitterKind::Rectangle);
		auto c = Candidate(truth);
		c.fitPosition = false;
		c.fitFrame = false;
		c.initial.geometry.geometry.halfWidthWorld = 26;
		c.initial.geometry.geometry.halfHeightWorld = 21;
		c.initial.rgbAmplitude = Vec3(16, 6, 3);
		auto out = Run("rectangle", scene, model, {truth}, {c});
		CHECK(records.back().heldout < records.back().black * .002);
		CHECK(std::abs(out.emitters[0].geometry.geometry.halfWidthWorld / 32 - 1) < .25);
		CHECK(std::abs(out.emitters[0].geometry.geometry.halfHeightWorld / 16 - 1) < .35);
	}
	{
		auto truth = Emitter(EmitterKind::Tube);
		auto c = Candidate(truth);
		c.fitPosition = c.fitFrame = false;
		c.initial.geometry.geometry.halfLengthWorld = 38;
		c.initial.geometry.geometry.radiusWorld = 7;
		c.initial.rgbAmplitude = Vec3(16, 6, 3);
		auto out = Run("long_tube", scene, model, {truth}, {c});
		CHECK(records.back().heldout < records.back().black * .005);
		CHECK(std::abs(out.emitters[0].geometry.geometry.halfLengthWorld / 48 - 1) < .3);
	}
	{
		auto truth = Emitter(EmitterKind::Rectangle);
		truth.geometry.angular.twoSided = true;
		auto c = Candidate(truth);
		c.fitPosition = c.fitFrame = c.fitDimensions = false;
		c.initial.rgbAmplitude = Vec3(10, 4, 2);
		auto out = Run("two_sided_sheet", scene, model, {truth}, {c}, true);
		CHECK(records.back().heldout < records.back().black * .001);
	}
	{
		auto a = Emitter(EmitterKind::Rectangle, 1), b = Emitter(EmitterKind::Rectangle, 2);
		a.geometry.position.world = Vec3(-50, 0, 120);
		b.geometry.position.world = Vec3(50, 0, 120);
		a.rgbAmplitude = Vec3(30, 3, 1);
		b.rgbAmplitude = Vec3(1, 3, 30);
		auto ca = Candidate(a), cb = Candidate(b);
		for (auto c : {&ca, &cb})
		{
			c->fitPosition = c->fitFrame = c->fitDimensions = false;
			c->initial.rgbAmplitude = Vec3(8, 8, 8);
		}
		auto out = Run("two_adjacent_panels", scene, model, {a, b}, {ca, cb});
		CHECK(out.emitters.size() == 2);
		CHECK(records.back().heldout < records.back().black * .001);
	}
}
void SelectionTests()
{
	Scene scene;
	auto model = Model();
	auto truth = Emitter(EmitterKind::Point);
	auto train = Data(scene, model, {truth}, 0), validation = Data(scene, model, {truth}, 1);
	auto point = Candidate(truth);
	point.fitPosition = point.fitFrame = point.fitDimensions = point.fitCone = false;
	point.initial.rgbAmplitude = Vec3(20000, 10000, 3000);
	auto spot = point;
	spot.initial.geometry.kind = EmitterKind::Spot;
	spot.initial.geometry.angular.kind = AngularKind::SmoothCone;
	spot.initial.geometry.angular.cosOuter = float(std::cos(1.5));
	spot.initial.geometry.angular.cosInner = float(std::cos(1.4));
	spot.fitCone = true;
	auto req = Request(scene, model, train, {point});
	S::FitResult a, b;
	Must(S::Fit(req, Read(), a));
	req.candidates = {spot};
	Must(S::Fit(req, Read(), b));
	S::Score va, vb;
	Must(S::EvaluateFrozen(a, validation, validation->data->size(), S::Role::Validation, scene.context,
						   req.strictSampling, .001, Read(), va));
	Must(S::EvaluateFrozen(b, validation, validation->data->size(), S::Role::Validation, scene.context,
						   req.strictSampling, .001, Read(), vb));
	S::Selection result;
	Must(S::Select({a, b}, {va, vb}, 1e-4, result));
	CHECK(result.retained.size() == 1 && result.retained[0] == 0);
	Must(S::Select({a, b}, {va, vb}, .01, result));
	CHECK(result.state == S::Selection::State::Alternatives);
	vb.identity = Key("different-observations");
	CHECK(S::Select({a, b}, {va, vb}, 1e-4, result).code == Code::InvalidInput);
	// Same source geometry offered as a historical spot is not a verified source type.
	auto unknown = model;
	unknown.law = S::Law::HistoricalBake;
	unknown.profile = {};
	req.model = unknown;
	S::FitResult failed;
	CHECK(S::Fit(req, Read(), failed).code == Code::Unavailable);
	CHECK(!failed.fitted && failed.emitters.empty());
	auto bad = req;
	bad.model = model;
	bad.training = validation;
	CHECK(S::Fit(bad, Read(), failed).code == Code::Unavailable);
	req = Request(scene, model, train, {point});
	req.options.maxTrials = 1;
	auto moving = point;
	moving.fitPosition = true;
	req.candidates = {moving};
	CHECK(S::Fit(req, Read(), failed).code == Code::WorkLimit);
	CHECK(failed.emitters.empty());
	class Cancel final : public Cancellation
	{
		bool IsCancelled() const noexcept override { return true; }
	} cancel;
	auto read = Read();
	read.cancellation = &cancel;
	CHECK(S::Fit(req, read, failed).code == Code::Cancelled);
	req.options.maxBytes = 1;
	CHECK(S::Fit(req, Read(), failed).code == Code::MemoryLimit);
}
void Occlusion()
{
	Scene scene(true);
	auto model = Model();
	auto truth = Emitter(EmitterKind::Rectangle);
	truth.geometry.position.world = Vec3(-32, -32, 140);
	auto c = Candidate(truth);
	c.fitPosition = c.fitFrame = c.fitDimensions = false;
	c.initial.rgbAmplitude = Vec3(8, 4, 2);
	auto out = Run("partial_occlusion", scene, model, {truth}, {c});
	CHECK(records.back().heldout < records.back().black * .03);
	Scene uncertain(true, true);
	auto rec = ReceiverSet(0);
	std::shared_ptr<const S::Block> block;
	Must(S::Prepare(model, {truth}, Lease(rec), uncertain.context, Sampling(), Read(), block));
	std::vector<S::Prediction> values;
	S::Work w;
	Must(block->Evaluate({truth.rgbAmplitude}, Read(), values, w));
	size_t unknown = 0;
	for (auto &p : values)
		unknown += !p.available;
	CHECK(unknown > 0);
}
struct Comparison
{
	std::string name;
	std::vector<EmitterKind> kinds;
	std::vector<S::Score> validation, heldout;
	std::vector<size_t> retained;
	size_t preferred = SIZE_MAX;
};
std::vector<Comparison> comparisons;
void CompareTypes(const std::string &name, const Scene &scene, const S::Model &model, const S::Emitter &truth,
				  double penalty, double slack)
{
	auto train = Data(scene, model, {truth}, 0), validation = Data(scene, model, {truth}, 1),
		 heldout = Data(scene, model, {truth}, 2);
	if (name == "missing_shape_evidence")
		for (auto stream : {train, validation, heldout})
		{
			auto &v = *stream->data;
			v.erase(std::remove_if(v.begin(), v.end(),
								   [](const S::Observation &a) {
									   return std::abs(a.receiver.world.x) > 20 ||
											  std::abs(a.receiver.world.y) > 20;
								   }),
					v.end());
			CHECK(v.size() == 1);
		}
	std::vector<S::FitResult> fits;
	Comparison comparison;
	comparison.name = name;
	for (auto kind : {EmitterKind::Point, EmitterKind::Spot, EmitterKind::Rectangle, EmitterKind::Tube})
	{
		auto initial = Emitter(kind);
		initial.geometry.position.world = Vec3(0, 0, 120);
		auto c = Candidate(initial);
		c.fitPosition = c.fitFrame = false;
		c.initial.rgbAmplitude = kind == EmitterKind::Point || kind == EmitterKind::Spot
									 ? Vec3(20000, 10000, 4000)
									 : Vec3(16, 8, 4);
		if (kind == EmitterKind::Spot)
		{
			c.initial.geometry.angular.cosOuter = float(std::cos(1.4));
			c.initial.geometry.angular.cosInner = float(std::cos(1.2));
		}
		auto request = Request(scene, model, train, {c});
		request.options.maxSweeps = 60;
		S::FitResult fit;
		Must(S::Fit(request, Read(), fit));
		fits.push_back(fit);
		comparison.kinds.push_back(kind);
		S::Score score;
		Must(S::EvaluateFrozen(fit, validation, validation->data->size(), S::Role::Validation, scene.context,
							   request.strictSampling, penalty, Read(), score));
		comparison.validation.push_back(score);
	}
	S::Selection selection;
	Must(S::Select(fits, comparison.validation, slack, selection));
	comparison.retained = selection.retained;
	comparison.preferred = selection.preferred;
	// All fits/selection are frozen BEFORE this controller opens held-out RGB.
	for (auto &fit : fits)
	{
		S::Score score;
		auto strict = Sampling();
		strict.compareRefined = true;
		Must(S::EvaluateFrozen(fit, heldout, heldout->data->size(), S::Role::Heldout, scene.context, strict,
							   penalty, Read(), score));
		comparison.heldout.push_back(score);
	}
	std::printf("type comparison %s: ", name.c_str());
	for (size_t i = 0; i < fits.size(); ++i)
		std::printf("%u=%.7g ", unsigned(comparison.kinds[i]), comparison.heldout[i].intervalMse);
	std::printf(" retained");
	for (auto i : comparison.retained)
		std::printf(" %u", unsigned(comparison.kinds[i]));
	std::printf("\n");
	CHECK(selection.state != S::Selection::State::InsufficientSignal && !comparison.retained.empty());
	if (name == "point_vs_overcomplex")
		CHECK(!comparison.retained.empty() &&
			  comparison.retained[0] == 0); // quadrature-uncertain alternatives are not falsely ruled out
	if (name == "missing_shape_evidence")
		CHECK(comparison.retained.size() > 1);
	comparisons.push_back(comparison);
}
void AdditionalTests()
{
	Scene scene;
	auto model = Model();
	{
		auto truth = Emitter(EmitterKind::Tube);
		truth.geometry.position.world.z = 18;
		auto c = Candidate(truth);
		c.centreBounds = Box(V(-160, -160, 8), V(160, 160, 80));
		c.fitPosition = c.fitFrame = false;
		c.initial.geometry.geometry.halfLengthWorld = 42;
		c.initial.geometry.geometry.radiusWorld = 6;
		c.initial.rgbAmplitude = Vec3(16, 6, 3);
		auto out = Run("near_field_tube", scene, model, {truth}, {c});
		CHECK(std::abs(out.emitters[0].geometry.geometry.radiusWorld / 5 - 1) < .3);
		CHECK(std::abs(out.emitters[0].geometry.geometry.halfLengthWorld / 48 - 1) < .2);
	}
	{
		auto truth = Emitter(EmitterKind::Spot);
		truth.geometry.angular.cosOuter = float(std::cos(1.45));
		truth.geometry.angular.cosInner = float(std::cos(1.3));
		auto c = Candidate(truth);
		c.fitPosition = c.fitFrame = false;
		c.initial.geometry.angular.cosOuter = float(std::cos(1.5));
		c.initial.geometry.angular.cosInner = float(std::cos(1.4));
		auto out = Run("broad_spot", scene, model, {truth}, {c});
		CHECK(!out.preciseAngularClassification);
	}
	{
		auto truth = Emitter(EmitterKind::Rectangle);
		truth.geometry.frame = FrameFor(Unit(V(.08, -.06, -1)));
		auto c = Candidate(truth);
		c.initial.geometry.frame = FrameFor(V(0, 0, -1));
		c.initial.geometry.position.world = Vec3(3, -3, 120);
		c.fitDimensions = false;
		c.initial.rgbAmplitude = Vec3(16, 7, 3);
		c.evidence = S::ShapeEvidence::VisibleFixture;
		c.geometryPriorWeight = 1e-7;
		auto out = Run("panel_pose", scene, model, {truth}, {c});
		CHECK(records.back().angular < .08);
		CHECK(records.back().after < records.back().before);
	}
	{
		auto truth = Emitter(EmitterKind::Tube);
		truth.geometry.frame = FrameFor(V(0, 0, -1), Unit(V(1, .15, 0)));
		auto c = Candidate(truth);
		c.initial.geometry.frame = FrameFor(V(0, 0, -1));
		c.fitPosition = c.fitDimensions = false;
		c.initial.rgbAmplitude = Vec3(15, 6, 3);
		auto out = Run("tube_axis", scene, model, {truth}, {c});
		double angle = std::acos(Clamp(
			std::abs(Dot(V(out.emitters[0].geometry.frame.right), V(truth.geometry.frame.right))), -1, 1));
		CHECK(angle < .08);
	}
	{
		auto historical = Model(true);
		auto truth = Emitter(EmitterKind::Spot);
		truth.rgbAmplitude = Vec3(300, 120, 40);
		auto c = Candidate(truth);
		c.upperRgb = Vec3(1200, 1200, 1200);
		c.initial.spotRadiusWorld *= 1.15;
		c.initial.rgbAmplitude = Vec3(150, 60, 20);
		c.fitPosition = c.fitFrame = false;
		auto out = Run("historical_spot", scene, historical, {truth}, {c});
		CHECK(out.linearAmplitudes);
		CHECK(records.back().heldout < records.back().black * .002);
		CHECK(out.emitters[0].geometry.emission.unit == PhotometricUnit::Relative && !out.absoluteScaleKnown);
	}
	{
		auto m = Model();
		m.output = S::Output::PhysicalLux;
		m.calibration.id = 7;
		m.calibration.kind = CalibrationKind::PhysicallyAuthored;
		m.calibration.method = "Owned calibrated SI reference";
		auto truth = Emitter(EmitterKind::Rectangle);
		auto c = Candidate(truth);
		c.fitPosition = c.fitFrame = c.fitDimensions = false;
		auto out = Run("authored_SI_panel", scene, m, {truth}, {c});
		CHECK(out.absoluteScaleKnown && out.emitters[0].geometry.emission.unit == PhotometricUnit::Nits &&
			  out.emitters[0].geometry.emission.calibration == 7);
	}
	CompareTypes("point_vs_overcomplex", scene, model, Emitter(EmitterKind::Point), 1e-4, 1e-5);
	auto panel = Emitter(EmitterKind::Rectangle);
	panel.geometry.geometry.halfWidthWorld = 60;
	panel.geometry.geometry.halfHeightWorld = 25;
	CompareTypes("panel", scene, model, panel, 1e-7, 1e-5);
	auto tube = Emitter(EmitterKind::Tube);
	tube.geometry.geometry.halfLengthWorld = 70;
	CompareTypes("tube", scene, model, tube, 1e-7, 1e-5);
	auto spot = Emitter(EmitterKind::Spot);
	CompareTypes("spot", scene, model, spot, 1e-7, 1e-5);
	auto small = Emitter(EmitterKind::Rectangle);
	small.geometry.geometry.halfWidthWorld = small.geometry.geometry.halfHeightWorld = 2;
	small.rgbAmplitude = Vec3(2000, 800, 400);
	CompareTypes("missing_shape_evidence", scene, model, small, 0, 1e-4);
}
void BoundariesAndNonlinear()
{
	Scene scene;
	auto physical = Model();
	auto point = Emitter(EmitterKind::Point);
	auto rec = ReceiverSet(0);
	std::shared_ptr<const S::Block> block;
	auto sampling = Sampling();
	auto invalid = point;
	invalid.geometry.kind = EmitterKind::Tube;
	invalid.geometry.geometry.radiusWorld = 0;
	CHECK(S::Prepare(physical, {invalid}, Lease(rec), scene.context, sampling, Read(), block).code ==
		  Code::InvalidInput);
	auto historical = Model(true);
	auto tube = Emitter(EmitterKind::Tube);
	historical.allowFacetExpansion = false;
	CHECK(S::Prepare(historical, {tube}, Lease(rec), scene.context, sampling, Read(), block).code ==
		  Code::Unavailable);
	historical.allowFacetExpansion = true;
	historical.tubeSides = 16;
	historical.tubeSegments = 4;
	Must(S::Prepare(historical, {tube}, Lease(rec), scene.context, sampling, Read(), block));
	std::vector<S::Prediction> p;
	S::Work work;
	Must(block->Evaluate({tube.rgbAmplitude}, Read(), p, work));
	CHECK(p[0].available);
	auto exact = Emitter(EmitterKind::Spot);
	exact.geometry.angular.cosInner = exact.geometry.angular.cosOuter;
	CHECK(S::Prepare(physical, {exact}, Lease(rec), scene.context, sampling, Read(), block).code ==
		  Code::InvalidInput);
	auto bad = physical;
	bad.output = S::Output::PhysicalLux;
	CHECK(S::Prepare(bad, {point}, Lease(rec), scene.context, sampling, Read(), block).code ==
		  Code::Unavailable);
	sampling.maxBytes = 1;
	CHECK(S::Prepare(physical, {point}, Lease(rec), scene.context, sampling, Read(), block).code ==
		  Code::MemoryLimit);
	sampling = Sampling();
	sampling.maxRays = 1;
	CHECK(S::Prepare(physical, {point}, Lease(rec), scene.context, sampling, Read(), block).code ==
		  Code::WorkLimit);
	sampling = Sampling();
	auto moved = point;
	moved.geometry.position.world.x += 1;
	std::shared_ptr<const S::Block> original;
	Must(S::Prepare(physical, {point}, Lease(rec), scene.context, sampling, Read(), original));
	Must(S::Prepare(physical, {moved}, Lease(rec), scene.context, sampling, Read(), block));
	CHECK(block->Key() != original->Key());
	// Historical FAST/coupled packing is never extrapolated from a unit-Q matrix.
	Bake::Recipe recipe;
	recipe.fast = true;
	historical = Model(true);
	historical.profile = Bake::NetRadiantProfile(recipe);
	historical.output = S::Output::EncodedRgb8;
	auto hpoint = Emitter(EmitterKind::Point);
	hpoint.rgbAmplitude = Vec3(900, 400, 1200);
	Must(S::Prepare(historical, {hpoint}, Lease(rec), scene.context, sampling, Read(), block));
	CHECK(!block->AllLinear({Vec3()}, {Vec3(2000, 2000, 2000)}));
	Must(block->Evaluate({hpoint.rgbAmplitude}, Read(), p, work));
	bool clipped = false;
	for (auto &v : p)
	{
		CHECK(v.available);
		clipped |= std::max({v.value.x, v.value.y, v.value.z}) >= 254.99;
	}
	CHECK(clipped);
	auto train = Data(scene, historical, {hpoint}, 0, false, true);
	auto validation = Data(scene, historical, {hpoint}, 1, false, true);
	train->data->resize(5);
	validation->data->resize(5);
	auto c = Candidate(hpoint);
	c.fitPosition = c.fitFrame = c.fitCone = c.fitDimensions = false;
	c.upperRgb = Vec3(2000, 2000, 2000);
	c.initial.rgbAmplitude = Vec3(200, 200, 200);
	auto request = Request(scene, historical, train, {c});
	request.options.maxAmplitudeIterations = 20;
	S::FitResult fit;
	Must(S::Fit(request, Read(), fit));
	CHECK(!fit.linearAmplitudes && fit.strictReevaluated);
	S::Score score;
	Must(S::EvaluateFrozen(fit, validation, validation->data->size(), S::Role::Validation, scene.context,
						   request.strictSampling, 0, Read(), score));
	CHECK(score.available && score.intervalMse < score.blackMse * .01);
	auto copied = fit;
	auto stale = scene.context;
	stale.geometryKey = Key("changed-geometry");
	CHECK(S::EvaluateFrozen(copied, validation, validation->data->size(), S::Role::Validation, stale,
							request.strictSampling, 0, Read(), score)
			  .code == Code::InvalidInput);
	train->corrupt = true;
	CHECK(S::Fit(request, Read(), fit).code == Code::ProviderError);
	CHECK(!fit.fitted && fit.emitters.empty());
	// Unsupported visible-geometry priors cannot be silently weakened to zero.
	train->corrupt = false;
	request.candidates[0].evidence = S::ShapeEvidence::Authored;
	request.candidates[0].geometryPriorWeight = 0;
	CHECK(S::Fit(request, Read(), fit).code == Code::InvalidInput);
	// Unknown source primaries permit same-domain RGB fitting, not canonical Y.
	train->corrupt = false;
	auto relative = Model();
	relative.linearRec709PrimariesAttested = false;
	auto real = Emitter(EmitterKind::Point);
	auto rows = Data(scene, Model(), {real}, 0);
	auto candidate = Candidate(real);
	candidate.fitPosition = candidate.fitFrame = candidate.fitDimensions = candidate.fitCone = false;
	auto relativeRequest = Request(scene, relative, rows, {candidate});
	Must(S::Fit(relativeRequest, Read(), fit));
	CHECK(!fit.emitters[0].geometry.emission.intensity.known &&
		  !fit.emitters[0].geometry.emission.color.known && !fit.absoluteScaleKnown);
	// Receiver triangles are integrated as supplied, not replaced by a GPU atlas square.
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>(1, (*rec)[0]);
	auto &receiver = (*receivers)[0];
	receiver.world = Vec3();
	auto footprint = std::make_shared<std::vector<Bake::FootprintTriangle>>(2);
	(*footprint)[0].world[0] = Vec3(-1, -1, 0);
	(*footprint)[0].world[1] = Vec3(1, -1, 0);
	(*footprint)[0].world[2] = Vec3(1, 1, 0);
	(*footprint)[1].world[0] = Vec3(-1, -1, 0);
	(*footprint)[1].world[1] = Vec3(1, 1, 0);
	(*footprint)[1].world[2] = Vec3(-1, 1, 0);
	for (auto &t : *footprint)
		for (auto &n : t.normal)
			n = Vec3(0, 0, 1);
	receiver.footprint = Lease(footprint);
	sampling = Sampling();
	sampling.receiverSubdivisions = 2;
	sampling.compareRefined = true;
	Must(S::Prepare(Model(), {real}, Lease(receivers), scene.context, sampling, Read(), block));
	Must(block->Evaluate({real.rgbAmplitude}, Read(), p, work));
	CHECK(p[0].value.x <= 40000. / 14400 && p[0].value.x >= 40000. * 120 / std::pow(14402., 1.5));
}
void HistoricalWrapperParity()
{
	Scene scene;
	auto model = Model(true);
	auto receivers = ReceiverSet(0);
	for (auto kind : {EmitterKind::Point, EmitterKind::Spot, EmitterKind::Rectangle})
	{
		auto emitter = Emitter(kind);
		emitter.rgbAmplitude = Vec3(300, 100, 40);
		std::shared_ptr<const S::Block> block;
		Must(S::Prepare(model, {emitter}, Lease(receivers), scene.context, Sampling(), Read(), block));
		std::vector<S::Prediction> wrapped;
		S::Work w;
		Must(block->Evaluate({emitter.rgbAmplitude}, Read(), wrapped, w));
		auto native = std::make_shared<std::vector<Bake::SourceHypothesis>>(1);
		auto &s = (*native)[0];
		s.id = 1;
		s.position = emitter.geometry.position;
		s.orientation = emitter.geometry.frame;
		s.amplitudeKnown = true;
		s.compilerRgbAmplitude = emitter.rgbAmplitude;
		s.kind = kind == EmitterKind::Point
					 ? Bake::SourceKind::Point
					 : kind == EmitterKind::Spot ? Bake::SourceKind::Spot : Bake::SourceKind::Polygon;
		s.spotTargetDistanceWorld = emitter.spotTargetDistanceWorld;
		s.spotRadiusWorld = emitter.spotRadiusWorld;
		if (kind == EmitterKind::Rectangle)
		{
			auto v = std::make_shared<std::vector<Vec3>>();
			V c(s.position.world), right(s.orientation.right), up(s.orientation.up);
			for (V p : {c - right * 32 - up * 16, c + right * 32 - up * 16, c + right * 32 + up * 16,
						c - right * 32 + up * 16})
				v->push_back(p.Float());
			s.polygon = Lease(v);
			s.position.world = (c + V(s.orientation.forward)).Float();
		}
		Bake::CpuForwardEvaluator evaluator;
		Bake::Options o;
		o.allowAssumed = true;
		o.output = Bake::Domain::CompilerLinearRelative255;
		Bake::Result direct;
		Must(evaluator.Evaluate(model.profile, Lease(native), Lease(receivers), Decode::Profile(),
								scene.context, o, Read(), direct));
		for (size_t i = 0; i < wrapped.size(); ++i)
		{
			CHECK(wrapped[i].available == direct.predictions.data[i].available);
			CHECK(Length(V(wrapped[i].value) - V(direct.predictions.data[i].compilerRelative)) < 1e-7);
		}
	}
}
int main(int argc, char **argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	auto start = std::chrono::steady_clock::now();
	try
	{
		ReferenceTests();
		FittingTests();
		SelectionTests();
		Occlusion();
		AdditionalTests();
		BoundariesAndNonlinear();
		HistoricalWrapperParity();
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("ABORT %s\n", e.what());
	}
	std::printf("S08: %d checks %d failures %.3fs\n", checks, failures, Seconds(start));
	if (argc > 1)
	{
		std::ofstream out(argv[1]);
		out << std::setprecision(12)
			<< "{\"schema\":\"org.openjk.s08-tests\",\"version\":1,\"checks\":" << checks
			<< ",\"failures\":" << failures << ",\"cases\":[";
		for (size_t i = 0; i < records.size(); ++i)
		{
			if (i)
				out << ',';
			auto &r = records[i];
			out << "{\"name\":\"" << r.name << "\",\"before_world\":" << r.before
				<< ",\"after_world\":" << r.after << ",\"angular_error_radians\":" << r.angular
				<< ",\"train_objective\":" << r.train << ",\"validation_mse\":" << r.validation
				<< ",\"heldout_mse\":" << r.heldout << ",\"black_mse\":" << r.black
				<< ",\"trials\":" << r.trials << ",\"values\":" << r.values << ",\"rays\":" << r.rays
				<< ",\"sources\":" << r.sources << ",\"converged\":" << (r.converged ? "true" : "false")
				<< ",\"stop\":\"" << r.stop << "\"}";
		}
		out << "]}\n";
		std::ofstream resources(std::string(argv[1]) + ".resources.json");
		resources << "{\"seconds\":" << Seconds(start) << ",\"cases\":[";
		for (size_t i = 0; i < records.size(); ++i)
		{
			if (i)
				resources << ',';
			resources << "{\"name\":\"" << records[i].name << "\",\"seconds\":" << records[i].seconds << '}';
		}
		resources << "]}\n";
		std::ofstream models(std::string(argv[1]) + ".models.json");
		models << std::setprecision(12)
			   << "{\"schema\":\"org.openjk.s08-model-comparison\",\"version\":1,\"cases\":[";
		for (size_t i = 0; i < comparisons.size(); ++i)
		{
			if (i)
				models << ',';
			auto &c = comparisons[i];
			models << "{\"name\":\"" << c.name << "\",\"models\":[";
			for (size_t j = 0; j < c.kinds.size(); ++j)
			{
				if (j)
					models << ',';
				auto &v = c.validation[j];
				auto &h = c.heldout[j];
				models << "{\"kind\":" << unsigned(c.kinds[j]) << ",\"validation_mse\":" << v.intervalMse
					   << ",\"heldout_mse\":" << h.intervalMse << ",\"black_mse\":" << h.blackMse
					   << ",\"penalty\":" << v.penalty << ",\"parameters\":" << v.parameters
					   << ",\"quadrature_difference\":" << v.quadratureDifference << '}';
			}
			models << "],\"preferred_kind\":" << unsigned(c.kinds[c.preferred]) << ",\"retained_kinds\":[";
			for (size_t j = 0; j < c.retained.size(); ++j)
			{
				if (j)
					models << ',';
				models << unsigned(c.kinds[c.retained[j]]);
			}
			models << "]}";
		}
		models << "]}\n";
		if (!out || !resources || !models)
			return 2;
	}
	return failures ? 1 : 0;
}
