// Owned, known-source geometry fixtures; no game/GL or source discovery.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/point_refine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace P = Rend2LightRecon::Core::PointRefine;
namespace A = Rend2LightRecon::Core::Amplitude;
const double worldUnitsPerMeter = Units().worldUnitsPerMeter;
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
Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
Bounds Box(V a, V b)
{
	Bounds x;
	x.known = true;
	x.minWorld = a.Float();
	x.maxWorld = b.Float();
	return x;
}
bool Inside(const Bounds &b, V p, double margin = 0)
{
	for (size_t k = 0; k < 3; ++k)
		if (p[k] < V(b.minWorld)[k] + margin || p[k] > V(b.maxWorld)[k] - margin)
			return false;
	return true;
}
bool Touch(const Bounds &a, const Bounds &b)
{
	for (size_t k = 0; k < 3; ++k)
		if (V(a.maxWorld)[k] < V(b.minWorld)[k] || V(a.minWorld)[k] > V(b.maxWorld)[k])
			return false;
	return true;
}
ReadContext Context()
{
	ReadContext r;
	r.maxBytes = 96ull * 1024 * 1024;
	return r;
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
class Domain final : public RegionDomain
{
  public:
	Bounds world = Box(V(-160, -160, 0), V(160, 160, 256));
	std::vector<Bounds> solid;
	bool unknown = false;
	Error Locate(Vec3 p, SpatialLocation &o, double tolerance = 1e-5) const override
	{
		o = {};
		o.work = 1;
		if (unknown)
		{
			o.space = SpaceClass::Unknown;
			return {};
		}
		if (!Inside(world, V(p)))
		{
			o.space = SpaceClass::Outside;
			return {};
		}
		for (const auto &b : solid)
		{
			++o.work;
			if (Inside(b, V(p), -tolerance))
			{
				o.space = Inside(b, V(p), tolerance) ? SpaceClass::Solid : SpaceClass::Unknown;
				return {};
			}
		}
		o.space = Inside(world, V(p), tolerance) ? SpaceClass::Free : SpaceClass::Unknown;
		return {};
	}
	Error ClassifyBox(const Bounds &b, SpaceClass &s, uint64_t &w) const override
	{
		w = 1;
		s = SpaceClass::Unknown;
		if (unknown)
			return {};
		if (!Touch(world, b))
		{
			s = SpaceClass::Outside;
			return {};
		}
		if (!Inside(world, V(b.minWorld), 1e-6) || !Inside(world, V(b.maxWorld), 1e-6))
			return {};
		for (const auto &wall : solid)
		{
			++w;
			if (Inside(wall, V(b.minWorld), 1e-6) && Inside(wall, V(b.maxWorld), 1e-6))
			{
				s = SpaceClass::Solid;
				return {};
			}
			if (Touch(wall, b))
				return {};
		}
		s = SpaceClass::Free;
		return {};
	}
};
class Rows final : public ChunkSource<A::Observation>
{
  public:
	mutable uint64_t calls = 0;
	std::shared_ptr<std::vector<A::Observation>> rows = std::make_shared<std::vector<A::Observation>>();
	Error Read(uint64_t first, size_t n, const ReadContext &c, Chunk<A::Observation> &out) const override
	{
		++calls;
		if (c.cancellation && c.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "fixture cancelled");
		if (first > rows->size() || n > rows->size() - first)
			return Failure(Code::ProviderError, "fixture range");
		out = VectorChunk<A::Observation>(rows, size_t(first), n);
		return {};
	}
};
struct Fixture
{
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::vector<BakedSurface> surfaces;
	std::shared_ptr<const StaticVisibility> geometry;
	std::shared_ptr<const P::PlacementProvider> placement;
	std::shared_ptr<Rows> rows = std::make_shared<Rows>();
	std::vector<Vec3> truth, q;
	P::Request request;
	LightmapMapIdentity map;
	bool recessed = false, occlusion = false;
	void Quad(V a, V b, V c, V d, uint32_t flags = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.material = "owned-test-opaque";
		s.materialFacts.available = true;
		s.materialFacts.cull = SurfaceCull::None;
		s.materialFacts.signals = flags;
		for (V p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p.Float();
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	void Solid(Bounds box)
	{
		domain->solid.push_back(box);
		V a(box.minWorld), b(box.maxWorld);
		Quad(V(a.x, a.y, a.z), V(a.x, b.y, a.z), V(a.x, b.y, b.z), V(a.x, a.y, b.z));
		Quad(V(b.x, a.y, a.z), V(b.x, b.y, a.z), V(b.x, b.y, b.z), V(b.x, a.y, b.z));
		Quad(V(a.x, a.y, a.z), V(b.x, a.y, a.z), V(b.x, a.y, b.z), V(a.x, a.y, b.z));
		Quad(V(a.x, b.y, a.z), V(b.x, b.y, a.z), V(b.x, b.y, b.z), V(a.x, b.y, b.z));
		Quad(V(a.x, a.y, a.z), V(b.x, a.y, a.z), V(b.x, b.y, a.z), V(a.x, b.y, a.z));
		Quad(V(a.x, a.y, b.z), V(b.x, a.y, b.z), V(b.x, b.y, b.z), V(a.x, b.y, b.z));
	}
	Fixture(bool recess = false, bool caster = false) : recessed(recess), occlusion(caster)
	{
		Quad(V(-160, -160, 0), V(160, -160, 0), V(160, 160, 0), V(-160, 160, 0));
		Quad(V(-160, -160, 0), V(-160, 160, 0), V(-160, 160, 256), V(-160, -160, 256));
		Quad(V(-160, 160, 0), V(160, 160, 0), V(160, 160, 256), V(-160, 160, 256));
		Quad(V(160, -160, 0), V(160, 160, 0), V(160, 160, 256), V(160, -160, 256));
		Quad(V(-160, -160, 0), V(160, -160, 0), V(160, -160, 256), V(-160, -160, 256));
		Quad(V(-160, -160, 256), V(160, -160, 256), V(160, 160, 256), V(-160, 160, 256));
		if (recess)
		{
			Solid(Box(V(80, -160, 0), V(140, -24, 256)));
			Solid(Box(V(80, 24, 0), V(140, 160, 256)));
			Solid(Box(V(80, -24, 0), V(140, 24, 96)));
			Solid(Box(V(80, -24, 176), V(140, 24, 256)));
			Solid(Box(V(140, -24, 96), V(160, 24, 176)));
		}
		if (caster)
			Solid(Box(V(-1, -160, 0), V(1, 48, 100)));
		GeometryKey key;
		for (auto &s : surfaces)
			for (auto &v : s.vertices)
				key.V(v.position);
		map.path = "maps/s07-owned.bsp";
		map.sha256 = key.Finish();
		map.bytes = 1;
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		CHECK(capture.SetMap(map));
		for (auto s : surfaces)
			CHECK(capture.Add(s));
		CHECK(capture.Freeze());
		CHECK(StaticVisibility::Build(capture.Acquire(), {}, Context(), geometry, domain));
		P::SceneInput scene;
		scene.map = map;
		scene.domain = domain;
		scene.domainKey = Key("owned-analytic-brush-domain-v1");
		scene.geometry = geometry;
		CHECK(P::MakePlacement(scene, Context(), placement));
		request.placement = placement;
		auto &a = request.lighting;
		Bake::Recipe recipe;
		recipe.fast = false;
		a.profile = Bake::NetRadiantProfile(recipe);
		a.mapKey = map.sha256;
		a.observationKey = Key("owned-target-stream");
		a.geometry.visibility = geometry;
		a.geometry.geometryKey = geometry->CertificationKey();
		a.geometry.materialKey = Key("known-static-opaque");
		a.geometry.observationPolicyKey = Key("super1-relative");
		a.observations = rows;
		a.forward.allowAssumed = true;
		a.options.maxBytes = 32ull * 1024 * 1024;
		a.options.loss = A::Loss::Squared;
		a.options.maxIterations = 400;
		a.options.maxEvaluations = 6000;
		a.options.restarts = 1;
		a.options.nonlinearPasses = 8;
		a.options.lineSearchSteps = 14;
		a.options.diagnostics = false;
		request.options.initialStepWorld = 16;
		request.options.minimumStepWorld = .1;
		request.options.maxFits = 1800;
		request.options.maxSweeps = 180;
		request.options.diagnosticRows = 128;
		request.options.maxDiagnosticFits = 18;
		request.options.relativeImprovement = 1e-11;
		request.options.maxAmplitudeEvaluations = 1000000;
		request.options.maxRays = 10000000;
		request.options.maxPairs = 1000000000;
		request.options.maxWork = 1000000000;
	}
	void Add(V real, V initial, V rgb)
	{
		A::Candidate c;
		c.source.id = q.size() + 1;
		c.source.kind = Bake::SourceKind::Point;
		c.source.position.known = true;
		c.source.position.world = initial.Float();
		c.upperQ = Vec3(1200, 1200, 1200);
		c.initialQ = Vec3(64, 64, 64);
		request.lighting.candidates.push_back(c);
		P::Point p;
		p.source = c.source.id;
		p.bounds = Box(V(-150, -150, 2), V(150, 150, 250));
		p.initialRadiusWorld = p.maximumRadiusWorld = 80;
		request.points.push_back(p);
		truth.push_back(real.Float());
		q.push_back(rgb.Float());
	}
	void Generate(bool onlyFloor = false, bool encoded = false, bool sparseGrid = false)
	{
		auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>();
		for (size_t j = 0; j < truth.size(); ++j)
		{
			auto s = request.lighting.candidates[j].source;
			s.position.world = truth[j];
			s.compilerRgbAmplitude = q[j];
			s.amplitudeKnown = true;
			sources->push_back(s);
		}
		auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
		for (size_t face = 0; face < (onlyFloor ? 1 : 3); ++face)
			for (size_t x = 0; x < 5; ++x)
				for (size_t y = 0; y < 4; ++y)
				{
					Bake::Receiver r;
					r.sample = receivers->size() + 1;
					r.surface = face + 1;
					r.mappedPlanar = r.compilerPlacementCertified = true;
					if (face == 0)
					{
						r.world = Vec3(-96 + float(x) * 48, -96 + float(y) * 64, 0);
						r.geometricNormal = r.shadingNormal = Vec3(0, 0, 1);
					}
					if (face == 1)
					{
						r.world = Vec3(-160, -96 + float(x) * 48, 24 + float(y) * 40);
						r.geometricNormal = r.shadingNormal = Vec3(1, 0, 0);
					}
					if (face == 2)
					{
						r.world = Vec3(-96 + float(x) * 48, 160, 24 + float(y) * 40);
						r.geometricNormal = r.shadingNormal = Vec3(0, -1, 0);
					}
					SpatialLocation loc;
					CHECK(domain->Locate((V(r.world) + V(r.geometricNormal)).Float(), loc));
					if (loc.space == SpaceClass::Free)
						receivers->push_back(r);
				}
		Bake::CpuForwardEvaluator forward;
		Bake::Options options = request.lighting.forward;
		options.output =
			encoded ? Bake::Domain::EncodedRgb8BeforeQuantization : Bake::Domain::CompilerLinearRelative255;
		Bake::Result prediction;
		auto e = forward.Evaluate(request.lighting.profile, Lease(sources), Lease(receivers), {},
								  request.lighting.geometry, options, Context(), prediction);
		if (!e)
			std::printf("fixture prediction: %s\n", e.message.c_str());
		CHECK(e);
		if (!e)
			throw std::runtime_error("Fixture forward unavailable");
		for (size_t i = 0; i < receivers->size(); ++i)
		{
			const auto &p = prediction.predictions.data[i];
			CHECK(p.available);
			if (!p.available)
				continue;
			A::Observation a;
			a.id = rows->rows->size() + 1;
			a.receiver = (*receivers)[i];
			a.receiver.sample = a.id;
			a.region = a.receiver.surface;
			a.type = encoded ? A::Type::SurfaceEncoded : A::Type::SurfaceRelative;
			a.encodingAttested = a.primariesComparable = a.comparisonScaleKnown = true;
			a.measure = 256;
			for (size_t c = 0; c < 3; ++c)
			{
				if (encoded)
					a.channels[c] = A::Rgb8(p.packedRgb[c]);
				else
					a.channels[c].lower = a.channels[c].upper = V(p.rgb)[c];
			}
			rows->rows->push_back(a);
		}
		if (sparseGrid)
		{
			auto probes = std::make_shared<std::vector<Bake::Receiver>>();
			for (V p : {V(-64, -40, 64), V(40, -20, 96), V(0, 64, 144), V(-32, 16, 208)})
			{
				Bake::Receiver receiver;
				receiver.sample = probes->size() + 1;
				receiver.surface = 0;
				receiver.world = p.Float();
				receiver.shadingNormal = receiver.geometricNormal = Vec3(0, 0, 1);
				receiver.mappedPlanar = receiver.compilerPlacementCertified = true;
				probes->push_back(receiver);
			}
			options.output = Bake::Domain::GridAmbientDirected;
			options.unpackedGridOnly = true;
			CHECK(forward.Evaluate(request.lighting.profile, Lease(sources), Lease(probes), {},
								   request.lighting.geometry, options, Context(), prediction));
			for (size_t i = 0; i < probes->size(); ++i)
				for (A::Type type : {A::Type::GridAmbientRelative, A::Type::GridDirectedRelative})
				{
					A::Observation a;
					a.id = rows->rows->size() + 1;
					a.receiver = (*probes)[i];
					a.receiver.sample = a.id;
					a.type = type;
					a.region = 100;
					a.encodingAttested = a.primariesComparable = a.comparisonScaleKnown = true;
					a.measure = 1;
					V value = type == A::Type::GridAmbientRelative
								  ? V(prediction.predictions.data[i].gridAmbient)
								  : V(prediction.predictions.data[i].gridDirected);
					for (size_t c = 0; c < 3; ++c)
						a.channels[c].lower = a.channels[c].upper = value[c];
					rows->rows->push_back(a);
				}
			request.lighting.options.typeWeight[size_t(A::Type::GridAmbientRelative)] = .25;
			request.lighting.options.typeWeight[size_t(A::Type::GridDirectedRelative)] = .25;
		}
		request.lighting.count = rows->rows->size();
	}
	P::Result Run()
	{
		P::Result result;
		auto e = P::Refine(request, Context(), result);
		if (!e)
		{
			std::printf("refine error %d: %s\n", int(e.code), e.message.c_str());
			for (auto &s : result.diagnostics)
				std::printf("  %s\n", s.c_str());
		}
		CHECK(e);
		CHECK(result.refined && result.strictReevaluated);
		if (!e)
			throw std::runtime_error("Refinement failure");
		CHECK(result.points.size() == truth.size());
		CHECK(result.amplitude.sources.size() == q.size());
		CHECK(result.objective <= result.initialObjective + 1e-6 * std::max(1., result.initialObjective));
		for (size_t i = 0; i < result.points.size(); ++i)
		{
			CHECK(result.points[i].source == i + 1);
			CHECK(Length(V(result.points[i].initialWorld) -
						 V(request.lighting.candidates[i].source.position.world)) == 0);
			P::PlacementResult place;
			CHECK(placement->Check(result.points[i].world, result.points[i].world,
								   request.points[i].sourceSupportRadiusWorld +
									   request.points[i].clearanceWorld,
								   Context(), place));
			CHECK(place.state == P::Constraint::Free);
			CHECK(Positive(V(result.amplitude.sources[i].compilerQ)));
			CHECK(!result.amplitude.sources[i].absoluteScaleKnown);
		}
		return result;
	}
};
struct Case
{
	std::string name, stop;
	double initial = 0, final = 0, objectiveInitial = 0, objective = 0, maxRgb = 0, seconds = 0;
	uint64_t fits = 0, evals = 0, rays = 0, work = 0, rejected = 0, visibility = 0, expansions = 0, rank = 0,
			 parameters = 0, accountedBytes = 0;
	double derivativeChange = 0;
	std::string sources;
	std::vector<double> singular, amplitudeSingular;
	size_t amplitudeRank = 0, amplitudeParameters = 0;
	bool conditionKnown = false, stepStable = false;
	double condition = 0;
	bool information = false, identifiable = false, converged = false;
};
std::vector<Case> cases;
std::vector<P::Iteration> exportTrace;
double PositionError(const Fixture &f, const P::Result &r, bool initial)
{
	double sum = 0;
	for (size_t i = 0; i < f.truth.size(); ++i)
	{
		V d = V(initial ? r.points[i].initialWorld : r.points[i].world) - V(f.truth[i]);
		sum += Dot(d, d);
	}
	return std::sqrt(sum / f.truth.size());
}
void Record(const std::string &name, const Fixture &f, const P::Result &r, double seconds)
{
	Case c;
	c.name = name;
	c.stop = P::StopName(r.stop);
	c.initial = PositionError(f, r, true);
	c.final = PositionError(f, r, false);
	c.objectiveInitial = r.initialObjective;
	c.objective = r.objective;
	c.maxRgb = r.amplitude.maximumIntervalError;
	c.seconds = seconds;
	c.fits = r.work.fits;
	c.evals = r.work.amplitudeEvaluations;
	c.rays = r.work.rays;
	c.work = r.work.traversal;
	c.visibility = r.work.visibilityChanges;
	c.expansions = r.work.expansions;
	c.rank = r.information.rank;
	c.parameters = r.information.parameters;
	c.information = r.information.available;
	c.converged = r.converged;
	c.identifiable = !r.points.empty() && r.points[0].locallyIdentifiable;
	c.accountedBytes = r.work.accountedBytes;
	c.derivativeChange = r.information.maximumDerivativeRelativeChange;
	c.singular = r.information.singular;
	c.amplitudeSingular = r.information.amplitudeSingular;
	c.amplitudeRank = r.information.amplitudeRank;
	c.amplitudeParameters = r.information.amplitudeParameters;
	c.conditionKnown = r.information.conditionKnown;
	c.condition = r.information.condition;
	c.stepStable = r.information.stepStable;
	std::ostringstream sources;
	sources << std::setprecision(12) << '[';
	auto vector = [&](Vec3 p) { sources << '[' << p.x << ',' << p.y << ',' << p.z << ']'; };
	for (size_t i = 0; i < r.points.size(); ++i)
	{
		if (i)
			sources << ',';
		sources << "{\"id\":" << r.points[i].source << ",\"initial_xyz\":";
		vector(r.points[i].initialWorld);
		sources << ",\"xyz\":";
		vector(r.points[i].world);
		sources << ",\"truth_xyz\":";
		vector(f.truth[i]);
		sources << ",\"q\":";
		vector(r.amplitude.sources[i].compilerQ);
		sources << ",\"truth_q\":";
		vector(f.q[i]);
		sources << ",\"axis_tested_ranges\":[";
		for (size_t k = 0; k < 3; ++k)
		{
			if (k)
				sources << ',';
			const auto &a = r.points[i].sampledRange[k];
			sources << "{\"sampled\":" << (a.sampled ? "true" : "false") << ",\"min\":" << a.minimum
					<< ",\"max\":" << a.maximum
					<< ",\"geometry_limit\":" << (a.limitedByGeometry ? "true" : "false")
					<< ",\"search_limit\":" << (a.limitedBySearch ? "true" : "false") << '}';
		}
		sources << "],\"boundary_encountered\":" << (r.points[i].boundaryConstrained ? "true" : "false")
				<< '}';
	}
	sources << ']';
	c.sources = sources.str();
	for (auto n : r.work.rejected)
		c.rejected += n;
	cases.push_back(c);
	std::printf(
		"%s: %.4f -> %.4f WU (%.5f m); objective %.6g -> %.6g; %llu fits, rank %llu/%llu, %s, %.2fs\n",
		name.c_str(), c.initial, c.final, c.final / worldUnitsPerMeter, c.objectiveInitial, c.objective,
		(unsigned long long)c.fits, (unsigned long long)c.rank, (unsigned long long)c.parameters,
		c.stop.c_str(), seconds);
	for (size_t i = 0; i < r.points.size(); ++i)
		std::printf("  source %llu XYZ %.7g %.7g %.7g Q %.7g %.7g %.7g; inner %s\n",
					(unsigned long long)r.points[i].source, r.points[i].world.x, r.points[i].world.y,
					r.points[i].world.z, r.amplitude.sources[i].compilerQ.x,
					r.amplitude.sources[i].compilerQ.y, r.amplitude.sources[i].compilerQ.z,
					A::StopName(r.amplitude.stop));
}
double Seconds(std::chrono::steady_clock::time_point t)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}
void OpenAndWall()
{
	{
		Fixture f;
		f.Add(V(12, -20, 144), V(44, -44, 112), V(300, 90, 30));
		f.Generate();
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("open_multinormal", f, r, Seconds(start));
		CHECK(PositionError(f, r, false) < 1);
		CHECK(r.information.available && r.information.rank == 3);
		exportTrace = r.trace;
		auto repeat = f.Run();
		CHECK(repeat.key == r.key);
		CHECK(repeat.work.fits == r.work.fits);
		CHECK(Length(V(repeat.points[0].world) - V(r.points[0].world)) == 0);
	}
	{
		Fixture f;
		f.Add(V(148, 0, 140), V(140, 8, 128), V(300, 180, 80));
		auto &p = f.request.points[0];
		p.bounds = Box(V(125, -24, 100), V(159, 24, 180));
		p.sourceSupportRadiusWorld = .2;
		p.clearanceWorld = .01;
		p.mount.known = true;
		p.mount.anchorWorld = Vec3(160, 0, 140);
		p.mount.normalWorld = Vec3(-1, 0, 0);
		p.mount.minimumOffsetWorld = 1;
		p.mount.maximumOffsetWorld = 40;
		f.Generate();
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("wall_adjacent", f, r, Seconds(start));
		CHECK(PositionError(f, r, false) < 2);
		CHECK(r.work.rejected[size_t(P::Constraint::Mount)] +
				  r.work.rejected[size_t(P::Constraint::Clearance)] +
				  r.work.rejected[size_t(P::Constraint::SearchBounds)] >
			  0);
	}
	{
		Fixture f(true);
		f.Add(V(120, 0, 136), V(110, 4, 128), V(300, 100, 40));
		auto &p = f.request.points[0];
		p.bounds = Box(V(98, -20, 104), V(138, 20, 168));
		p.sourceSupportRadiusWorld = .25;
		p.clearanceWorld = .01;
		p.mount.known = true;
		p.mount.anchorWorld = Vec3(140, 0, 136);
		p.mount.normalWorld = Vec3(-1, 0, 0);
		p.mount.minimumOffsetWorld = 2;
		p.mount.maximumOffsetWorld = 44;
		f.Generate();
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("recessed_fixture", f, r, Seconds(start));
		CHECK(PositionError(f, r, false) < 3);
	}
}
void OcclusionAndExpansion()
{
	{
		Fixture f(false, true);
		f.Add(V(-48, -24, 144), V(-80, -48, 120), V(300, 180, 50));
		f.request.points[0].bounds = Box(V(-120, -100, 104), V(-8, 40, 220));
		f.Generate();
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("partial_occlusion", f, r, Seconds(start));
		CHECK(PositionError(f, r, false) < PositionError(f, r, true) * .5);
		CHECK(r.work.visibilityChanges > 0);
	}
	{
		Fixture f;
		f.Add(V(72, -20, 144), V(0, -20, 144), V(300, 90, 30));
		auto &p = f.request.points[0];
		p.initialRadiusWorld = 16;
		p.maximumRadiusWorld = 128;
		f.request.options.expand = true;
		f.Generate();
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("wrong_start_bounded_expansion", f, r, Seconds(start));
		CHECK(r.work.expansions > 0);
		CHECK(PositionError(f, r, false) < 2);
	}
	{
		Fixture f;
		f.Add(V(12, -20, 144), V(-20, 12, 180), V(300, 90, 30));
		f.request.options.multiStarts = 2;
		f.Generate(true);
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("single_broad_plane_multiple_starts", f, r, Seconds(start));
		CHECK(PositionError(f, r, false) < 2);
	}
}
void AmbiguityAndPair()
{
	{
		Fixture f;
		f.Add(V(0, 20, 144), V(0, -20, 144), V(300, 100, 40));
		f.Generate(true);
		f.rows->rows->resize(2);
		for (size_t i = 0; i < 2; ++i)
		{
			(*f.rows->rows)[i].receiver.world = Vec3(i ? 32.f : -32.f, 0, 0);
			(*f.rows->rows)[i].receiver.sample = i + 1;
		}
		// Regenerate the exact two symmetric observations with the production operator.
		auto s =
			std::make_shared<std::vector<Bake::SourceHypothesis>>(1, f.request.lighting.candidates[0].source);
		(*s)[0].position.world = f.truth[0];
		(*s)[0].compilerRgbAmplitude = f.q[0];
		(*s)[0].amplitudeKnown = true;
		auto rec = std::make_shared<std::vector<Bake::Receiver>>();
		for (auto &a : *f.rows->rows)
			rec->push_back(a.receiver);
		Bake::Options o = f.request.lighting.forward;
		o.output = Bake::Domain::CompilerLinearRelative255;
		Bake::Result pred;
		Bake::CpuForwardEvaluator e;
		CHECK(e.Evaluate(f.request.lighting.profile, Lease(s), Lease(rec), {}, f.request.lighting.geometry, o,
						 Context(), pred));
		for (size_t i = 0; i < 2; ++i)
			for (size_t c = 0; c < 3; ++c)
				(*f.rows->rows)[i].channels[c].lower = (*f.rows->rows)[i].channels[c].upper =
					V(pred.predictions.data[i].rgb)[c];
		f.request.lighting.count = 2;
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("symmetric_two_receivers", f, r, Seconds(start));
		CHECK(!r.points[0].locallyIdentifiable);
		CHECK(r.information.rank < 3);
	}
	{
		Fixture f;
		f.Add(V(0, 0, 140), V(0, 0, 100), V(300, 100, 40));
		f.Generate(true);
		f.rows->rows->resize(1);
		f.request.lighting.count = 1;
		P::DirectionEvidence d;
		d.id = 1;
		d.source = 1;
		d.originWorld = Vec3(0, 0, 1);
		d.toSource = Vec3(0, 0, 1);
		d.halfAngleRadians = .01;
		d.weight = 1;
		d.isolatedSourceAttested = d.profileValidated = d.originKnownFree = true;
		d.provenance = "owned isolated-point cone, not an arbitrary mixed grid vector";
		f.request.directions = {d};
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("parallel_direction_depth_ambiguity", f, r, Seconds(start));
		CHECK(!r.points[0].locallyIdentifiable);
	}
	{
		Fixture f;
		f.Add(V(-18, -8, 140), V(-34, -20, 124), V(300, 30, 10));
		f.Add(V(26, 12, 148), V(40, 20, 164), V(10, 50, 280));
		for (size_t i = 0; i < 2; ++i)
		{
			auto &c = f.request.lighting.candidates[i];
			CHECK(NormalizeChromaticity(f.q[i], c.prior.colour));
			c.prior.colourWeight = .01;
			f.request.points[i].initialRadiusWorld = f.request.points[i].maximumRadiusWorld = 40;
		}
		f.request.options.initialStepWorld = 8;
		f.request.options.minimumStepWorld = .25;
		f.request.options.maxFits = 1800;
		f.request.options.maxDiagnosticFits = 12;
		f.Generate();
		auto start = std::chrono::steady_clock::now();
		auto r = f.Run();
		Record("two_nearby_coloured_sources", f, r, Seconds(start));
		CHECK(r.points.size() == 2 && r.points[0].source != r.points[1].source);
		CHECK(PositionError(f, r, false) < PositionError(f, r, true));
		CHECK(Length(V(r.points[0].world) - V(r.points[1].world)) > 10);
	}
}
void SparseGrid()
{
	Fixture f;
	f.Add(V(12, -20, 144), V(40, -44, 116), V(300, 90, 30));
	// Sparse volume samples create a narrow profiled position/amplitude valley;
	// resolve it rather than declaring stationarity at a quarter-world-unit poll.
	f.request.options.minimumStepWorld = .025;
	f.request.options.maxSweeps = 300;
	f.request.options.diagnostics = false;
	f.Generate(true, false, true);
	auto start = std::chrono::steady_clock::now();
	auto r = f.Run();
	Record("sparse_grid_plus_surface", f, r, Seconds(start));
	CHECK(PositionError(f, r, false) < 2);
	CHECK(!r.amplitude.linear);
}
void NegativeGeometry()
{
	Fixture f(false, true);
	P::PlacementResult result;
	CHECK(f.placement->Check(Vec3(-20, 0, 120), Vec3(20, 0, 120), .1, Context(), result));
	CHECK(result.state == P::Constraint::Free);
	CHECK(f.placement->Check(Vec3(-20, 0, 60), Vec3(20, 0, 60), .1, Context(), result));
	CHECK(result.state == P::Constraint::OpaquePath);
	CHECK(f.placement->Check(Vec3(0, 0, 60), Vec3(0, 0, 60), .1, Context(), result));
	CHECK(result.state != P::Constraint::Free);
	f.Add(V(-48, -24, 144), V(-80, -48, 120), V(300, 180, 50));
	f.Generate();
	P::Result out;
	auto bad = f.request;
	bad.lighting.profile = {};
	CHECK(P::Refine(bad, Context(), out).code == Code::Unavailable);
	CHECK(!out.refined && out.points.empty());
	bad = f.request;
	bad.lighting.options.batchRows = 4097;
	auto reads = f.rows->calls;
	CHECK(P::Refine(bad, Context(), out).code == Code::InvalidInput);
	CHECK(f.rows->calls == reads);
	bad = f.request;
	bad.options.maxFits = 1;
	CHECK(P::Refine(bad, Context(), out).code == Code::WorkLimit);
	CHECK(!out.refined && out.points.empty());
	bad = f.request;
	bad.options.maxBytes = 1;
	CHECK(P::Refine(bad, Context(), out).code == Code::MemoryLimit);
	class Cancel final : public Cancellation
	{
	  public:
		bool IsCancelled() const noexcept override { return true; }
	} cancel;
	auto read = Context();
	read.cancellation = &cancel;
	CHECK(P::Refine(f.request, read, out).code == Code::Cancelled);
	CHECK(!out.refined && out.points.empty());
	P::SceneInput moving;
	moving.map = f.map;
	moving.domain = f.domain;
	moving.domainKey = Key("moving");
	moving.geometry = f.geometry;
	DynamicBoundary b;
	b.id = 1;
	b.bounds = Box(V(-100, -100, 100), V(-60, -40, 160));
	b.sweptExtentKnown = true;
	moving.dynamic.push_back(b);
	std::shared_ptr<const P::PlacementProvider> p;
	CHECK(P::MakePlacement(moving, Context(), p));
	CHECK(p->Check(Vec3(-80, -60, 120), Vec3(-80, -60, 120), .1, Context(), result));
	CHECK(result.state == P::Constraint::Dynamic);
	auto invalid = f.request;
	invalid.placement = p;
	invalid.lighting.geometry.geometryKey = Key("stale");
	CHECK(P::Refine(invalid, Context(), out).code == Code::MissingGeometry);
	invalid = f.request;
	invalid.lighting.candidates[0].source.position.world = Vec3(0, 0, 60);
	CHECK(P::Refine(invalid, Context(), out).code == Code::Unavailable);
	CHECK(!out.refined && out.points.empty() && out.amplitude.sources.empty());
	// A near-surface finite support cube may not straddle a thin wall.
	CHECK(f.placement->Check(Vec3(-1.05f, 0, 60), Vec3(-1.05f, 0, 60), .1, Context(), result));
	CHECK(result.state != P::Constraint::Free);
	// Locally missing or optically unknown geometry never becomes open space.
	for (bool alpha : {false, true})
	{
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		CHECK(capture.SetMap(f.map));
		for (auto s : f.surfaces)
			CHECK(capture.Add(s));
		BakedSurface unknown;
		unknown.ordinal = uint32_t(f.surfaces.size());
		unknown.motion = SurfaceMotion::StaticWorld;
		if (alpha)
		{
			unknown = f.surfaces[0];
			unknown.ordinal = uint32_t(f.surfaces.size());
			unknown.materialFacts.signals = MaterialCutout;
			for (size_t i = 0; i < 4; ++i)
				unknown.vertices[i].position =
					Vec3(0, i == 1 || i == 2 ? 80.f : -80.f, i >= 2 ? 220.f : 110.f);
		}
		else
			unknown.kind = BakedSurfaceKind::Unsupported;
		CHECK(capture.Add(unknown));
		CHECK(capture.Freeze());
		std::shared_ptr<const StaticVisibility> geometry;
		CHECK(StaticVisibility::Build(capture.Acquire(), {}, Context(), geometry, f.domain));
		P::SceneInput s;
		s.map = f.map;
		s.domain = f.domain;
		s.domainKey = Key("uncertain-geometry-domain");
		s.geometry = geometry;
		std::shared_ptr<const P::PlacementProvider> provider;
		CHECK(P::MakePlacement(s, Context(), provider));
		CHECK(provider->Check(Vec3(-20, 0, 160), Vec3(20, 0, 160), .1, Context(), result));
		CHECK(result.state == (alpha ? P::Constraint::UnknownPath : P::Constraint::UnknownGeometry));
	}
	// Black/zero response is a valid negative observation, never position recovery.
	Fixture black;
	black.Add(V(12, -20, 144), V(44, -44, 112), V());
	black.Generate(true);
	auto noSignal = black.Run();
	CHECK(noSignal.stop == P::Stop::Uninformative && !noSignal.converged);
	CHECK(!noSignal.points[0].locallyIdentifiable);
	CHECK(Length(V(noSignal.points[0].world) - V(noSignal.points[0].initialWorld)) == 0);
	Fixture biased;
	biased.Add(V(12, -20, 144), V(-20, 12, 160), V(300, 90, 30));
	biased.Generate(true);
	biased.rows->rows->resize(1);
	biased.request.lighting.count = 1;
	auto &prior = biased.request.points[0].prior;
	prior.known = true;
	prior.meanWorld = Vec3(40, -20, 140);
	prior.sigmaWorld = Vec3(4, 4, 4);
	prior.weight = .2;
	prior.kind = P::PriorKind::Explicit;
	prior.provenance = "Owned deliberately biased geometry prior; not source truth";
	auto start = std::chrono::steady_clock::now();
	auto priorResult = biased.Run();
	Record("prior_driven_ambiguity", biased, priorResult, Seconds(start));
	CHECK(Length(V(priorResult.points[0].world) - V(prior.meanWorld)) < .5);
	CHECK(!priorResult.points[0].locallyIdentifiable && priorResult.information.rank < 3);
}
int main(int argc, char **argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	auto start = std::chrono::steady_clock::now();
	try
	{
		std::string group = argc > 2 ? argv[2] : "all";
		if (group == "all" || group == "open")
			OpenAndWall();
		if (group == "all" || group == "occlusion")
			OcclusionAndExpansion();
		if (group == "all" || group == "ambiguity")
			AmbiguityAndPair();
		if (group == "all" || group == "grid")
			SparseGrid();
		if (group == "all" || group == "negative")
			NegativeGeometry();
		CHECK(group == "all" || group == "open" || group == "occlusion" || group == "ambiguity" ||
			  group == "grid" || group == "negative");
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("test aborted: %s\n", e.what());
	}
	std::printf("S07 point refinement: %d checks, %d failures, %.3fs\n", checks, failures, Seconds(start));
	if (argc > 1)
	{
		std::ofstream f(argv[1]);
		f << std::setprecision(12)
		  << "{\"schema\":\"org.openjk.point-refinement-tests\",\"version\":1,\"checks\":" << checks
		  << ",\"failures\":" << failures << ",\"world_units_per_metre\":" << worldUnitsPerMeter
		  << ",\"cases\":[";
		for (size_t i = 0; i < cases.size(); ++i)
		{
			if (i)
				f << ',';
			auto &c = cases[i];
			auto array = [&](const std::vector<double> &v) {
				f << '[';
				for (size_t j = 0; j < v.size(); ++j)
				{
					if (j)
						f << ',';
					f << v[j];
				}
				f << ']';
			};
			f << "{\"name\":\"" << c.name << "\",\"before_world\":" << c.initial
			  << ",\"after_world\":" << c.final << ",\"after_metres\":" << c.final / worldUnitsPerMeter
			  << ",\"objective_initial\":" << c.objectiveInitial << ",\"objective_final\":" << c.objective
			  << ",\"max_rgb_error\":" << c.maxRgb << ",\"fits\":" << c.fits
			  << ",\"amplitude_evaluations\":" << c.evals << ",\"rays\":" << c.rays << ",\"work\":" << c.work
			  << ",\"rejected\":" << c.rejected << ",\"visibility_changes\":" << c.visibility
			  << ",\"expansions\":" << c.expansions << ",\"accounted_bytes\":" << c.accountedBytes
			  << ",\"derivative_step_relative_change\":" << c.derivativeChange << ",\"sources\":" << c.sources
			  << ",\"singular\":";
			array(c.singular);
			f << ",\"amplitude_singular\":";
			array(c.amplitudeSingular);
			f << ",\"amplitude_rank\":" << c.amplitudeRank
			  << ",\"amplitude_parameters\":" << c.amplitudeParameters
			  << ",\"step_stable\":" << (c.stepStable ? "true" : "false") << ",\"condition\":";
			if (c.conditionKnown)
				f << c.condition;
			else
				f << "null";
			f << ",\"information_available\":" << (c.information ? "true" : "false") << ",\"rank\":" << c.rank
			  << ",\"parameters\":" << c.parameters
			  << ",\"locally_identifiable\":" << (c.identifiable ? "true" : "false")
			  << ",\"converged\":" << (c.converged ? "true" : "false") << ",\"stop\":\"" << c.stop << "\"}";
		}
		f << "]}\n";
		std::ofstream perf(std::string(argv[1]) + ".resources.json");
		perf << "{\"seconds\":" << Seconds(start) << ",\"cases\":[";
		for (size_t i = 0; i < cases.size(); ++i)
		{
			if (i)
				perf << ',';
			perf << "{\"name\":\"" << cases[i].name << "\",\"seconds\":" << cases[i].seconds << '}';
		}
		perf << "]}\n";
		std::ofstream trace(std::string(argv[1]) + ".trace.csv");
		trace << "evaluation,sweep,source,x,y,z,step,objective,accepted,visibility_changed,expansion\n";
		for (const auto &t : exportTrace)
			trace << t.evaluation << ',' << t.sweep << ',' << t.source << ',' << t.position.x << ','
				  << t.position.y << ',' << t.position.z << ',' << t.stepWorld << ',' << t.total << ','
				  << t.accepted << ',' << t.visibilityChanged << ',' << t.expansion << '\n';
		if (!f || !perf || !trace)
			return 2;
	}
	return failures ? 1 : 0;
}
