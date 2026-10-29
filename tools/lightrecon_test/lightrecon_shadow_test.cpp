// Owned scenes. Hidden answers stay in this test controller, never in Detect/Objective.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/emitter_shape.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/shadow_evidence.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace H = Rend2LightRecon::Core::Shadow;
namespace S = Rend2LightRecon::Core::Shape;
int checks = 0, failures = 0;
uint64_t associationTp = 0, associationFp = 0, associationFn = 0, associationTn = 0;
double finiteWidthReference = 8, finiteWidthPredicted = 0, finiteVisibility = 0;
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
ReadContext Read()
{
	ReadContext r;
	r.maxBytes = 64ull * 1024 * 1024;
	return r;
}
Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
Bounds Box(V a, V b)
{
	Bounds r;
	r.known = true;
	r.minWorld = a.Float();
	r.maxWorld = b.Float();
	return r;
}
class Domain final : public RegionDomain
{
  public:
	Error Locate(Vec3 p, SpatialLocation &o, double = 1e-5) const override
	{
		o = {};
		o.work = 1;
		o.space = p.z < -.01 ? SpaceClass::Outside : SpaceClass::Free;
		return {};
	}
	Error ClassifyBox(const Bounds &, SpaceClass &o, uint64_t &w) const override
	{
		o = SpaceClass::Free;
		w = 1;
		return {};
	}
};
struct Scene
{
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::shared_ptr<const StaticVisibility> visibility;
	LightmapMapIdentity map;
	std::vector<BakedSurface> surfaces;
	void Quad(V a, V b, V c, V d, uint32_t flags = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.material = "s09-owned";
		s.materialFacts.available = true;
		s.materialFacts.cull = SurfaceCull::None;
		s.materialFacts.signals = flags;
		for (auto p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p.Float();
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	Scene(bool alpha = false, bool missing = false, bool window = false, SurfaceCull cull = SurfaceCull::None)
	{
		Quad(V(-300, -300, 0), V(300, -300, 0), V(300, 300, 0), V(-300, 300, 0));
		if (window)
		{
			Quad(V(0, -100, 0), V(0, -10, 0), V(0, -10, 64), V(0, -100, 64));
			Quad(V(0, 10, 0), V(0, 100, 0), V(0, 100, 64), V(0, 10, 64));
		}
		else
			Quad(V(0, -100, 0), V(0, 100, 0), V(0, 100, 64), V(0, -100, 64),
				 alpha ? uint32_t(MaterialCutout) : 0u);
		// A geometrically plausible but wrong caster behind the true wall.
		Quad(V(-12, -100, 0), V(-12, 100, 0), V(-12, 100, 24), V(-12, -100, 24));
		if (missing)
		{
			BakedSurface s;
			s.ordinal = uint32_t(surfaces.size());
			s.kind = BakedSurfaceKind::Unsupported;
			s.motion = SurfaceMotion::StaticWorld;
			s.materialFacts.available = true;
			s.materialFacts.cull = SurfaceCull::None;
			surfaces.push_back(s);
		}
		GeometryKey key;
		for (auto &s : surfaces)
		{
			s.materialFacts.cull = cull;
			key.U(unsigned(cull));
			key.U(s.materialFacts.signals);
			key.U(unsigned(s.kind));
			for (auto &v : s.vertices)
				key.V(v.position);
		}
		map.path = "maps/s09-owned.bsp";
		map.bytes = 1;
		map.sha256 = key.Finish();
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Must(capture.SetMap(map));
		for (auto &s : surfaces)
			Must(capture.Add(s));
		Must(capture.Freeze());
		Must(StaticVisibility::Build(capture.Acquire(), {}, Read(), visibility, domain));
	}
};
Bake::Receiver Receiver(double x, double y, Id id, double half = .25)
{
	Bake::Receiver r;
	r.sample = id;
	r.surface = 1;
	r.world = V(x, y, 0).Float();
	r.geometricNormal = r.shadingNormal = Vec3(0, 0, 1);
	r.mappedPlanar = r.compilerPlacementCertified = true;
	auto f = std::make_shared<std::vector<Bake::FootprintTriangle>>(2);
	V a(x - half, y - half, 0), b(x + half, y - half, 0), c(x + half, y + half, 0), d(x - half, y + half, 0);
	(*f)[0].world[0] = a.Float();
	(*f)[0].world[1] = b.Float();
	(*f)[0].world[2] = c.Float();
	(*f)[1].world[0] = a.Float();
	(*f)[1].world[1] = c.Float();
	(*f)[1].world[2] = d.Float();
	for (auto &t : *f)
		for (auto &n : t.normal)
			n = Vec3(0, 0, 1);
	r.footprint = Lease(f);
	return r;
}
FitEvidence::Source CandidateSource(double x = -32, Id id = 50)
{
	FitEvidence::Source s;
	s.geometry.source.id = id;
	s.geometry.kind = EmitterKind::Point;
	s.geometry.position.known = true;
	s.geometry.position.world = V(x, 0, 128).Float();
	s.geometry.angular.kind = AngularKind::Isotropic;
	s.rgb = Vec3(100, 80, 60);
	return s;
}
// Independent exact planar shadow for point x<0,height128, caster top64.
double Visible(double receiverX, double sourceX, double half = .25)
{
	return Clamp((receiverX + half + sourceX) / (2 * half), 0, 1);
}
double Point(double x, double y, double sx);
std::shared_ptr<std::vector<H::Sample>> Samples(double boundary = 32, bool material = false, bool gi = false,
												double penumbra = 0)
{
	auto samples = std::make_shared<std::vector<H::Sample>>();
	for (unsigned y = 0; y < 5; ++y)
		for (unsigned x = 0; x < 49; ++x)
		{
			H::Sample s;
			s.id = 1 + x + y * 49;
			s.chart = 1;
			s.material = material && x >= 24 ? 2 : 1;
			s.x = x;
			s.y = y;
			s.receiver = Receiver(20.25 + x * .5, -4. + double(y) * 2, s.id);
			s.sourceLinear = s.primariesComparable = s.filterKnown = true;
			double vis = penumbra > 0
							 ? Clamp((s.receiver.world.x - boundary + penumbra * .5) / penumbra, 0, 1)
							 : Visible(s.receiver.world.x, -boundary);
			// Independent cosine/inverse-square point response; the analytical
			// visibility ramp is a detector fixture, not compiler super1 replay.
			double direct = 10000 * vis * Point(s.receiver.world.x, s.receiver.world.y, -boundary);
			s.rgb = V((gi ? .4 : .02) + direct, (gi ? .35 : .02) + .8 * direct, (gi ? .3 : .02) + .6 * direct)
						.Float();
			s.sigmaRgb = .5 / 255;
			samples->push_back(s);
		}
	return samples;
}
std::shared_ptr<const H::Evidence> Detect(const Scene &scene, std::shared_ptr<std::vector<H::Sample>> samples)
{
	std::shared_ptr<const H::Evidence> out;
	H::DetectionOptions options;
	options.minimumRelativeContrast = .015;
	Must(H::Detect(scene.map.sha256, Key("explicit synthetic linear RGB operator"), Lease(samples), options,
				   Read(), out));
	return out;
}
std::shared_ptr<const H::Objective> Objective(const Scene &scene, std::shared_ptr<const H::Evidence> e,
											  H::Scope scope = H::Scope::PhysicalFootprint,
											  bool oracle = false, double edgeWeight = 1)
{
	H::Context c;
	c.observations = e;
	c.visibility = scene.visibility;
	c.policy.scope = scope;
	c.policy.units.worldUnitsPerMeter = 1;
	c.policy.provenance = "Owned planar physical footprint hypothesis; no compiler parity claim";
	c.policy.searchRadiusWorld = 48;
	c.policy.edgeWeight = edgeWeight;
	c.policy.colourWeight = 0;
	c.policy.maxWork = 16000000;
	if (oracle)
		for (auto &edge : e->edges)
			if (edge.usable)
			{
				H::OracleBinding b;
				b.edge = edge.id;
				b.source = 50;
				b.caster = H::CasterId(2, Vec3(0, -100, 64), Vec3(0, 100, 64));
				c.oracle.push_back(b);
			}
	std::shared_ptr<const H::Objective> out;
	Must(H::Objective::Build(c, Read(), out));
	return out;
}
std::vector<std::string> rows;
std::vector<std::string> evidenceRows;
std::vector<std::string> derivativeRows;
void Record(const std::string &name, bool truth, const H::Evidence &e)
{
	bool admitted = std::any_of(e.edges.begin(), e.edges.end(), [](const H::Edge &v) { return v.usable; });
	std::ostringstream s;
	s << "{\"name\":\"" << name << "\",\"shadow_truth\":" << truth << ",\"candidate_admitted\":" << admitted
	  << ",\"verified_cast_shadow\":false}";
	evidenceRows.push_back(s.str());
}
void DetectorTests()
{
	Scene scene;
	auto samples = Samples();
	auto e = Detect(scene, samples);
	CHECK(e->edges.size() == 1);
	CHECK(e->edges[0].usable);
	CHECK(std::abs(e->edges[0].world.x - 32) < .3);
	CHECK(!e->edges[0].castShadowConfidence.known);
	Record("point_shadow", true, *e);
	// The same RGB with undisclosed albedo is observationally indistinguishable.
	// It must remain a possible edge, with unknown confidence, not a certified shadow.
	Record("undisclosed_albedo_same_RGB", false, *e);
	auto order = *samples;
	std::reverse(samples->begin(), samples->end());
	CHECK(Detect(scene, samples)->identity == e->identity);
	auto bad = Samples(32, true);
	auto m = Detect(scene, bad);
	Record("known_material_boundary", false, *m);
	CHECK(!m->gradients.empty());
	CHECK(std::none_of(m->edges.begin(), m->edges.end(), [](const H::Edge &e) { return e.usable; }));
	for (auto &s : *bad)
		s.material = 1, s.issues = H::ChartSeam;
	{
		auto x = Detect(scene, bad);
		CHECK(!x->edges[0].usable);
		Record("chart_seam", false, *x);
	}
	for (auto &s : *bad)
		s.issues = 0, s.sourceLinear = false;
	{
		auto x = Detect(scene, bad);
		CHECK(!x->edges[0].usable);
		Record("unknown_transfer", false, *x);
	}
	for (auto &s : *bad)
		s.sourceLinear = true, s.issues = H::SaturationEdge;
	{
		auto x = Detect(scene, bad);
		CHECK(!x->edges[0].usable);
		Record("saturation_boundary", false, *x);
	}
	for (auto &s : *bad)
		s.issues = 0, s.receiver.shadingNormal = s.x >= 24 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
	{
		auto x = Detect(scene, bad);
		CHECK(!x->edges[0].usable);
		Record("normal_crease", false, *x);
	}
	for (auto &s : *bad)
		s.receiver.shadingNormal = Vec3(0, 0, 1), s.rgb = Vec3(5, 5, 5);
	CHECK(Detect(scene, bad)->edges.empty());
	Record("uninformative_dark", false, *Detect(scene, bad));
	H::Context c;
	c.observations = e;
	c.visibility = scene.visibility;
	c.policy.provenance = "negative";
	auto held = std::make_shared<H::Evidence>(*e);
	held->samples[0].role = H::Sample::Role::Heldout;
	c.observations = held;
	std::shared_ptr<const H::Objective> out;
	CHECK(H::Objective::Build(c, Read(), out).code == Code::InvalidInput);
	held->samples[0].role = H::Sample::Role::Validation;
	CHECK(H::Objective::Build(c, Read(), out).code == Code::InvalidInput);
	c.observations = e;
	c.policy.scope = H::Scope::CompilerScoped;
	CHECK(H::Objective::Build(c, Read(), out).code == Code::Unavailable);
}
void AssociationTests(const std::string &prefix)
{
	Scene scene;
	auto e = Detect(scene, Samples());
	auto o = Objective(scene, e);
	H::Report r;
	Must(o->Inspect({CandidateSource()}, Read(), r));
	std::printf("edges=%llu matches=%llu unknown=%llu loss=%g attempts=%llu rays=%llu\n",
				(unsigned long long)r.eligibleEdges, (unsigned long long)r.value.matched,
				(unsigned long long)r.unknown, r.value.loss, (unsigned long long)r.associationAttempts,
				(unsigned long long)r.value.rays);
	for (auto &a : r.attempted)
		std::printf("state=%u surface=%llu low=%g high=%g distance=%g\n", unsigned(a.state),
					(unsigned long long)a.casterSurface, a.prediction.visibleLow, a.prediction.visibleHigh,
					a.prediction.signedEdgeDistanceWorld);
	CHECK(r.value.matched == 1);
	CHECK(r.value.loss < .01);
	CHECK(std::any_of(r.attempted.begin(), r.attempted.end(),
					  [](const H::Association &a) { return a.retained && a.casterSurface == 2; }));
	CHECK(!r.families.empty());
	if (!r.families.empty())
		CHECK(std::abs(r.families.front().planeNormal.y) < 1e-6);
	H::Report wrong;
	Must(o->Inspect({CandidateSource(-52)}, Read(), wrong));
	CHECK(wrong.value.loss > r.value.loss);
	auto oracle = Objective(scene, e, H::Scope::PhysicalFootprint, true);
	H::Report upper;
	Must(oracle->Inspect({CandidateSource()}, Read(), upper));
	CHECK(upper.oracle);
	CHECK(std::abs(upper.value.loss - r.value.loss) < 1e-9);
	H::Prediction p;
	Must(o->Probe(CandidateSource(), Receiver(50, 0, 500), Read(), p));
	CHECK(p.available && p.integratedVisibility > .999);
	Must(o->Probe(CandidateSource(), Receiver(10, 0, 501), Read(), p));
	CHECK(p.available && p.integratedVisibility < 1e-8);
	Must(o->Probe(CandidateSource(), Receiver(32, 0, 502), Read(), p));
	CHECK(p.available && std::abs(p.integratedVisibility - .5) < .01);
	auto approximate = Receiver(32, 0, 510);
	approximate.geometryErrorWorld = .1;
	Must(o->Probe(CandidateSource(), approximate, Read(), p));
	CHECK(!p.available && (p.issues & PatchApproximation));
	auto unsupported = CandidateSource();
	unsupported.geometry.kind = EmitterKind::Directional;
	CHECK(o->Probe(unsupported, Receiver(32, 0, 510), Read(), p).code == Code::Unavailable);
	unsupported = CandidateSource();
	unsupported.geometry.position.world.x = std::numeric_limits<float>::quiet_NaN();
	CHECK(o->Probe(unsupported, Receiver(32, 0, 510), Read(), p).code == Code::InvalidInput);
	for (double h : {.1, .02, .005})
	{
		H::Prediction a, b;
		Must(o->Probe(CandidateSource(-32 - h), Receiver(32, 0, 503), Read(), a));
		Must(o->Probe(CandidateSource(-32 + h), Receiver(32, 0, 504), Read(), b));
		double derivative = (b.integratedVisibility - a.integratedVisibility) / (2 * h);
		std::ostringstream row;
		row << std::setprecision(12) << "{\"h\":" << h << ",\"derivative\":" << derivative
			<< ",\"reference\":2,\"absolute_error\":" << std::abs(derivative - 2) << "}";
		derivativeRows.push_back(row.str());
		CHECK(a.available && b.available);
		CHECK(std::abs(derivative - 2) < .02);
	}
	auto centre = Objective(scene, e, H::Scope::CentreVisibility);
	H::Prediction a, b;
	Must(centre->Probe(CandidateSource(-32.1), Receiver(31.8, 0, 505), Read(), a));
	Must(centre->Probe(CandidateSource(-31.9), Receiver(31.8, 0, 506), Read(), b));
	CHECK(a.integratedVisibility == b.integratedVisibility);
	Scene alpha(true), missing(false, true), window(false, false, true);
	for (auto &s : {&alpha, &missing})
	{
		auto unknown = Objective(*s, Detect(*s, Samples()));
		Must(unknown->Probe(CandidateSource(), Receiver(10, 0, 501), Read(), p));
		CHECK(!p.available);
		H::Report x;
		Must(unknown->Inspect({CandidateSource()}, Read(), x));
		CHECK(!x.value.informative);
		CHECK(x.unknown > 0);
		if (s == &alpha)
		{
			std::string debug;
			Must(unknown->DebugObj(x, 1024 * 1024, debug));
			CHECK(debug.find("uncertain_caster") != std::string::npos);
		}
	}
	auto opening = Objective(window, Detect(window, Samples()));
	Must(opening->Probe(CandidateSource(), Receiver(10, 0, 507), Read(), p));
	CHECK(p.available && p.integratedVisibility > .999);
	Must(opening->Probe(CandidateSource(), Receiver(10, 40, 508), Read(), p));
	CHECK(p.available && p.integratedVisibility < 1e-8);
	for (auto cull : {SurfaceCull::Front, SurfaceCull::Back})
	{
		Scene oneSided(false, false, false, cull);
		auto side = Objective(oneSided, Detect(oneSided, Samples()));
		Must(side->Probe(CandidateSource(), Receiver(10, 0, 509), Read(), p));
		CHECK(p.available);
		CHECK(cull == SurfaceCull::Front ? p.integratedVisibility > .999 : p.integratedVisibility < 1e-8);
	}
	auto gi = Objective(scene, Detect(scene, Samples(32, false, true)));
	H::Report g;
	Must(gi->Inspect({CandidateSource()}, Read(), g));
	CHECK(g.value.informative && g.value.loss < .01);
	std::string text;
	Must(o->DebugTsv(r, 1024 * 1024, text));
	if (!prefix.empty())
	{
		std::ofstream f(prefix + "-associations.tsv");
		f << text;
	}
	Must(o->DebugObj(r, 1024 * 1024, text));
	if (!prefix.empty())
	{
		std::ofstream f(prefix + "-geometry.obj");
		f << text;
	}
	CHECK(o->DebugObj(r, 1, text).code == Code::MemoryLimit);
	class Cancel final : public Cancellation
	{
	  public:
		bool IsCancelled() const noexcept override { return true; }
	} cancellation;
	auto read = Read();
	read.cancellation = &cancellation;
	CHECK(o->Inspect({CandidateSource()}, read, r).code == Code::Cancelled);
}
class Stream final : public ChunkSource<S::Observation>
{
  public:
	std::shared_ptr<std::vector<S::Observation>> data = std::make_shared<std::vector<S::Observation>>();
	Error Read(uint64_t f, size_t n, const ReadContext &, Chunk<S::Observation> &out) const override
	{
		if (f > data->size() || n > data->size() - f)
			return Failure(Code::ProviderError, "fixture range");
		out = VectorChunk<S::Observation>(data, size_t(f), n);
		return {};
	}
};
double Point(double x, double y, double sx)
{
	V delta(sx - x, -y, 128);
	return 128 / std::pow(Length(delta), 3);
}
std::shared_ptr<Stream> Training()
{
	auto s = std::make_shared<Stream>();
	S::Observation o;
	o.id = 9000;
	o.spatialBlock = 1;
	o.comparable = true;
	o.receiver = Receiver(70, 0, o.id);
	for (int k = 0; k < 3; ++k)
		o.channel[k].lower = o.channel[k].upper = Point(70, 0, -32) * 10000 * (1 - .2 * k);
	s->data->push_back(o);
	return s;
}
void FitTests()
{
	Scene scene;
	auto evidence = Detect(scene, Samples());
	auto data = Training();
	S::Request request;
	request.mapKey = scene.map.sha256;
	request.observationsKey = Key("S09 disjoint RGB/edge training; heldout controlled separately");
	request.training = data;
	request.count = 1;
	request.model.law = S::Law::DiffuseIntegral;
	request.model.output = S::Output::RelativeIncident;
	request.model.allowAssumed = true;
	request.model.linearRec709PrimariesAttested = true;
	request.model.units.worldUnitsPerMeter = 1;
	request.model.provenance = "Physical point integral, synthetic relative units only";
	request.geometry.visibility = scene.visibility;
	request.geometry.geometryKey = scene.visibility->CertificationKey();
	request.geometry.materialKey = Key("owned");
	request.geometry.observationPolicyKey = Key("owned point sample reference");
	request.placement.map = scene.map;
	request.placement.domain = scene.domain;
	request.placement.domainKey = Key("owned free domain");
	request.placement.geometry = scene.visibility;
	S::Candidate candidate;
	candidate.initial.geometry = CandidateSource(-52).geometry;
	candidate.initial.rgbAmplitude = Vec3(5000, 5000, 5000);
	candidate.fitFrame = candidate.fitDimensions = candidate.fitCone = false;
	candidate.maximumTranslationWorld = 40;
	candidate.centreBounds = Box(V(-72, 0, 128), V(-12, 0, 128));
	candidate.lowerRgb = Vec3(0, 0, 0);
	candidate.upperRgb = Vec3(100000, 100000, 100000);
	candidate.provenance = "Given source height/y, one-dimensional localization test";
	request.candidates = {candidate};
	request.options.maxTrials = 400;
	request.options.maxSweeps = 60;
	request.options.maxValues = 200000;
	request.options.relativeImprovement = 1e-10;
	request.options.maxRays = 10000000;
	request.options.maxWork = 1000000000;
	request.options.minimumStepFraction = .01;
	request.searchSampling.compareRefined = false;
	request.strictSampling.compareRefined = true;
	for (double seed : {-52., -44., -20.})
	{
		request.candidates[0].initial.geometry.position.world.x = float(seed);
		double errors[5] = {};
		for (unsigned mode = 0; mode < 5; ++mode)
		{
			request.shadowWeight = mode ? 1 : 0;
			request.shadow =
				mode ? Objective(scene, evidence,
								 mode == 1 ? H::Scope::CentreVisibility : H::Scope::PhysicalFootprint,
								 mode == 3, mode <= 2 ? 0 : 1)
					 : nullptr;
			auto t = std::chrono::steady_clock::now();
			S::FitResult result;
			Must(S::Fit(request, Read(), result));
			CHECK(result.fitted && result.strictReevaluated);
			double sx = result.emitters[0].geometry.position.world.x;
			errors[mode] = std::abs(sx + 32);
			double held = 0, black = 0;
			for (double x : {36., 45., 65.})
				for (double y : {-14., 14.})
				{
					double truth = Point(x, y, -32) * 10000;
					for (int channel = 0; channel < 3; ++channel)
					{
						double actual = truth * (1 - .2 * channel);
						double predicted =
							(x > -sx ? 1 : 0) * Point(x, y, sx) * V(result.emitters[0].rgbAmplitude)[channel];
						held += std::pow(predicted - actual, 2) / 3;
						black += actual * actual / 3;
					}
				}
			CHECK(held < black);
			std::ostringstream row;
			row << std::setprecision(12) << "{\"seed_x\":" << seed << ",\"mode\":" << mode
				<< ",\"source_x\":" << sx << ",\"position_error_world\":" << errors[mode]
				<< ",\"heldout_mse\":" << held / 6 << ",\"black_mse\":" << black / 6
				<< ",\"rgb_amplitude\":" << result.emitters[0].rgbAmplitude.x
				<< ",\"trials\":" << result.trials << ",\"rays\":" << result.work.rays
				<< ",\"shadow_loss\":" << result.shadowObjective << "}";
			rows.push_back(row.str());
			std::printf("ablation %u x=%g error=%g held=%g rays=%llu time=%g\n", mode, sx, errors[mode],
						held / 6, (unsigned long long)result.work.rays,
						std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count());
		}
		CHECK(errors[4] < 1);
		CHECK(errors[4] + 2 < errors[0]);
		CHECK(std::abs(errors[4] - errors[3]) < .5);
	}
}
void ExtendedTests()
{
	Scene scene;
	auto e = Detect(scene, Samples(32, false, false, 8));
	auto o = Objective(scene, e);
	auto source = CandidateSource();
	source.geometry.kind = EmitterKind::Rectangle;
	source.geometry.frame.known = true;
	source.geometry.frame.forward = Vec3(0, 0, -1);
	source.geometry.frame.right = Vec3(1, 0, 0);
	source.geometry.frame.up = Vec3(0, -1, 0);
	source.geometry.angular.kind = AngularKind::Lambertian;
	source.geometry.geometry.known = true;
	source.geometry.geometry.halfWidthWorld = 4;
	source.geometry.geometry.halfHeightWorld = 2;
	H::Prediction p;
	Must(o->Probe(source, Receiver(32, 0, 600), Read(), p));
	CHECK(p.available);
	CHECK(std::abs(p.integratedVisibility - .5) < .1);
	finiteVisibility = p.integratedVisibility;
	CHECK(p.finiteSourceApproximation && !p.exactPointFootprint);
	H::Report r;
	Must(o->Inspect({source}, Read(), r));
	CHECK(r.value.informative);
	for (const auto &a : r.attempted)
		if (a.retained)
		{
			finiteWidthPredicted = a.prediction.predictedWidthWorld - 2 * e->edges[0].locationSigmaWorld;
		}
	CHECK(std::abs(finiteWidthPredicted - finiteWidthReference) < 1e-5);
	// A wide source and two separated points agree at the centre, but not at
	// independently sampled penumbra positions. No source-count claim follows
	// from the centre observation alone.
	for (double x : {32., 29.})
	{
		H::Prediction area, left, right;
		Must(o->Probe(source, Receiver(x, 0, 610), Read(), area));
		Must(o->Probe(CandidateSource(-28), Receiver(x, 0, 611), Read(), left));
		Must(o->Probe(CandidateSource(-36), Receiver(x, 0, 612), Read(), right));
		CHECK(area.available && left.available && right.available);
		double two = .5 * (left.integratedVisibility + right.integratedVisibility);
		CHECK(x == 32 ? std::abs(area.integratedVisibility - two) < .01
					  : std::abs(area.integratedVisibility - two) > .1);
	}
	auto tube = source;
	tube.geometry.kind = EmitterKind::Tube;
	tube.geometry.geometry.halfLengthWorld = 4;
	tube.geometry.geometry.radiusWorld = 1;
	Must(o->Probe(tube, Receiver(32, 0, 613), Read(), p));
	CHECK(p.available && p.finiteSourceApproximation && !p.exactPointFootprint);
	auto other = CandidateSource(-33, 51);
	Must(o->Inspect({CandidateSource(), other}, Read(), r));
	CHECK(r.value.matched > 0);
	CHECK(std::any_of(r.attempted.begin(), r.attempted.end(),
					  [](const H::Association &a) { return a.retained && a.source == 50; }));
	auto same = CandidateSource(-32, 51);
	Must(o->Inspect({CandidateSource(), same}, Read(), r));
	CHECK(std::count_if(r.attempted.begin(), r.attempted.end(),
						[](const H::Association &a) { return a.retained; }) >= 2);
	H::Context c;
	c.observations = e;
	c.visibility = scene.visibility;
	c.policy.provenance = "moving test";
	DynamicBoundary moving;
	moving.id = 1;
	moving.bounds = Box(V(-1, -30, 0), V(1, 30, 100));
	moving.sweptExtentKnown = true;
	c.moving = {moving};
	std::shared_ptr<const H::Objective> m;
	Must(H::Objective::Build(c, Read(), m));
	Must(m->Probe(CandidateSource(), Receiver(10, 0, 601), Read(), p));
	CHECK(!p.available);
	c.moving.clear();
	c.policy.maxRays = 1;
	Must(H::Objective::Build(c, Read(), m));
	CHECK(m->Inspect({CandidateSource()}, Read(), r).code == Code::WorkLimit);
	auto small = Read();
	small.maxBytes = 64;
	CHECK(o->Inspect({CandidateSource()}, small, r).code == Code::MemoryLimit);
}
void MultiSourceTests()
{
	Scene scene;
	auto samples = Samples();
	for (auto &s : *samples)
	{
		double a = Visible(s.receiver.world.x, -30), b = Visible(s.receiver.world.x, -36);
		s.rgb = Vec3(float(.02 + 10000 * a * Point(s.receiver.world.x, s.receiver.world.y, -30)), .02f,
					 float(.02 + 10000 * b * Point(s.receiver.world.x, s.receiver.world.y, -36)));
	}
	auto e = Detect(scene, samples);
	CHECK(e->edges.size() == 2);
	H::Context c;
	c.observations = e;
	c.visibility = scene.visibility;
	c.policy.provenance = "Two coloured roots, independently cast boundaries; no answer bindings";
	c.policy.units.worldUnitsPerMeter = 1;
	c.policy.colourWeight = 2;
	std::shared_ptr<const H::Objective> o;
	Must(H::Objective::Build(c, Read(), o));
	auto a = CandidateSource(-30, 50), b = CandidateSource(-36, 51);
	a.rgb = Vec3(100, 0, 0);
	b.rgb = Vec3(0, 0, 100);
	H::Report r;
	Must(o->Inspect({b, a}, Read(), r));
	for (auto &v : r.attempted)
		std::printf("multi edge=%llu source=%llu state=%u retained=%u dist=%g colour=%g loss=%g\n",
					(unsigned long long)v.edge, (unsigned long long)v.source, unsigned(v.state), v.retained,
					v.prediction.signedEdgeDistanceWorld, v.colourLoss, v.total);
	CHECK(r.value.matched == 2);
	for (const auto &edge : e->edges)
	{
		Id truth = edge.world.x < 33 ? 50 : 51;
		for (const auto &a : r.attempted)
			if (a.edge == edge.id)
			{
				bool expected = a.source == truth && a.casterSurface == 2;
				if (expected && a.retained)
					++associationTp;
				if (expected && !a.retained)
					++associationFn;
				if (!expected && a.retained)
					++associationFp;
				if (!expected && !a.retained)
					++associationTn;
			}
		CHECK(std::any_of(r.attempted.begin(), r.attempted.end(), [&](const H::Association &v) {
			return v.edge == edge.id && v.source == truth && v.casterSurface == 2 && v.retained;
		}));
		CHECK(std::none_of(r.attempted.begin(), r.attempted.end(), [&](const H::Association &v) {
			return v.edge == edge.id && v.source != truth && v.retained;
		}));
	}
	H::Report repeated;
	Must(o->Inspect({a, b}, Read(), repeated));
	CHECK(repeated.value.loss == r.value.loss && repeated.value.rays == r.value.rays);
	auto black = a;
	black.rgb = Vec3();
	Must(o->Inspect({black}, Read(), r));
	CHECK(!r.value.informative && r.value.matched == 0);
	Record("two_coloured_shadows", true, *e);
	H::Context wrong = c;
	wrong.visibility = Scene(true).visibility;
	std::shared_ptr<const H::Objective> invalid;
	CHECK(H::Objective::Build(wrong, Read(), invalid).code == Code::MissingGeometry);
}
int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		DetectorTests();
		AssociationTests(argc > 1 ? argv[1] : "");
		FitTests();
		ExtendedTests();
		MultiSourceTests();
		if (argc > 1)
		{
			std::ofstream f(std::string(argv[1]) + "-results.json");
			f << "{\"schema\":\"org.openjk.shadow-fit-s09.v1\",\"checks\":" << checks
			  << ",\"failures\":" << failures
			  << ",\"protocol\":\"fixed-height one-dimensional source localization\",\"ablations\":[";
			for (size_t i = 0; i < rows.size(); ++i)
				f << (i ? "," : "") << rows[i];
			f << "],\"edge_candidate_confusion\":[";
			for (size_t i = 0; i < evidenceRows.size(); ++i)
				f << (i ? "," : "") << evidenceRows[i];
			f << "],\"source_motion_derivatives\":[";
			for (size_t i = 0; i < derivativeRows.size(); ++i)
				f << (i ? "," : "") << derivativeRows[i];
			f << "],\"reported_association_confusion\":{\"tp\":" << associationTp
			  << ",\"fp\":" << associationFp << ",\"fn\":" << associationFn << ",\"tn\":" << associationTn
			  << ",\"scope\":\"reported source-caster pairs in the two-colour owned fixture, not all "
				 "possible pairs\"}"
			  << ",\"finite_source\":{\"reference_geometric_width\":" << finiteWidthReference
			  << ",\"predicted_geometric_width\":" << finiteWidthPredicted
			  << ",\"centre_integrated_visibility\":" << finiteVisibility
			  << ",\"quadrature_exact\":false}}\n";
		}
	}
	catch (const std::exception &e)
	{
		std::printf("EXCEPTION %s\n", e.what());
		++failures;
	}
	std::printf("S09: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
