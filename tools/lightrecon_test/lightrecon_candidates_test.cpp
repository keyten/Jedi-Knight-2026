// Own-content candidate stress fixtures; no source fitting, GPU or game assets.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/candidate_pool.h"
#include "lightrecon/classification.h"
#include "lightrecon/geometry_key.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace C = Rend2LightRecon::Core::Candidates;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                             \
	do                                                                                                       \
	{                                                                                                        \
		++checks;                                                                                            \
		if (!(x))                                                                                            \
		{                                                                                                    \
			++failures;                                                                                      \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                                      \
		}                                                                                                    \
	} while (0)
Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
struct Inputs
{
	std::vector<EmitterEvidence> evidence;
	std::vector<EmitterCandidate> candidates;
	std::vector<C::Annotation> annotations;
	void Add(Id id, Vec3 p, uint32_t shape = CandidatePoint, bool authored = false)
	{
		EmitterEvidence e;
		e.id = id;
		e.origin = authored ? EvidenceOrigin::AuthoredArea : EvidenceOrigin::GlowSurface;
		e.staticEligible = true;
		e.positionKnown = true;
		e.position = p;
		e.shapes = shape;
		e.colourKnown = true;
		e.colour = Vec3(1, 1, 1);
		e.orientationKnown = true;
		e.right = Vec3(1, 0, 0);
		e.up = Vec3(0, 1, 0);
		e.outward = Vec3(0, 0, -1);
		e.toSource = Vec3(0, 0, 1);
		e.halfWidth = 2;
		e.halfHeight = 1;
		e.radius = .2f;
		e.coneKnown = true;
		e.coneDegrees = 30;
		e.source = "own-content fixture";
		e.material = "identical material";
		e.surfaces.push_back(uint32_t(id));
		EmitterCandidate c;
		c.id = id + 1000000;
		c.primary = evidence.size();
		c.evidence.push_back(c.primary);
		c.staticEligible = true;
		c.shapes = shape;
		c.sourceIds.push_back(id);
		C::Annotation a;
		a.candidate = c.id;
		a.protectedAuthored = authored;
		a.tier = authored ? C::Tier::Authored : C::Tier::Geometry;
		a.geometryWork = 1;
		evidence.push_back(e);
		candidates.push_back(c);
		annotations.push_back(a);
	}
	C::Provider Provider(const std::string &name = "fixture", const std::string &generation = "gen") const
	{
		C::Provider p;
		p.name = name;
		p.generation = Key(generation);
		p.map = Key("map");
		p.geometryKey = Key("geometry");
		auto e = std::make_shared<const std::vector<EmitterEvidence>>(evidence);
		auto c = std::make_shared<const std::vector<EmitterCandidate>>(candidates);
		auto a = std::make_shared<const std::vector<C::Annotation>>(annotations);
		p.evidence = VectorChunk<EmitterEvidence>(e, 0, e->size());
		p.candidates = VectorChunk<EmitterCandidate>(c, 0, c->size());
		p.annotations = VectorChunk<C::Annotation>(a, 0, a->size());
		return p;
	}
	void Reverse()
	{
		size_t n = evidence.size();
		std::reverse(evidence.begin(), evidence.end());
		for (auto &c : candidates)
		{
			c.primary = n - 1 - c.primary;
			for (auto &i : c.evidence)
				i = n - 1 - i;
			std::reverse(c.evidence.begin(), c.evidence.end());
			std::reverse(c.sourceIds.begin(), c.sourceIds.end());
		}
		std::reverse(candidates.begin(), candidates.end());
		std::reverse(annotations.begin(), annotations.end());
		for (auto &a : annotations)
		{
			std::reverse(a.support.begin(), a.support.end());
			std::reverse(a.regions.begin(), a.regions.end());
		}
	}
};
C::Request Request(std::vector<C::Provider> p)
{
	C::Request r;
	r.map = Key("map");
	r.geometryKey = Key("geometry");
	r.providers = std::move(p);
	return r;
}
C::Catalog Catalog(C::Request r)
{
	C::Catalog c;
	auto e = C::Consolidate(r, {}, c);
	if (!e)
		std::printf("consolidate: %s\n", e.message.c_str());
	CHECK(e);
	return c;
}
C::Selection Wave(const C::Catalog &c, const C::Budget &b, C::Cursor &cursor)
{
	C::Selection s;
	auto e = C::Select(c, b, {}, cursor, s);
	if (!e)
		std::printf("select: %s\n", e.message.c_str());
	CHECK(e);
	CHECK(s.tickets.size() <= b.maxTickets);
	CHECK(s.geometryWork <= b.maxGeometryWork);
	std::set<Id> unique;
	for (auto &t : s.tickets)
		CHECK(unique.insert(t.bundle).second);
	return s;
}
std::string Semantic(const C::Catalog &c)
{
	GeometryKey k;
	k.Digest(c.key);
	for (auto &b : c.bundles)
	{
		k.U(b.id);
		k.U(b.provenance);
		for (Id x : b.models)
			k.U(x);
		for (auto &r : b.inputs)
			k.U(r.scoped);
	}
	for (auto &m : c.models)
	{
		k.U(m.id);
		for (auto &p : m.parts)
			k.U(p.scoped);
	}
	for (auto &l : c.links)
	{
		k.U(l.a);
		k.U(l.b);
		k.U(uint32_t(l.kind));
	}
	return Disk::Hex(k.Finish());
}
const C::Model &Model(const C::Catalog &c, Id id)
{
	return *std::find_if(c.models.begin(), c.models.end(), [&](const C::Model &m) { return m.id == id; });
}
void StrictUnions()
{
	Inputs a;
	a.Add(1, {0, 0, 10});
	a.Add(2, {4, 0, 10});
	a.Add(3, {8, 0, 10});
	auto base = Catalog(Request({a.Provider()}));
	CHECK(base.bundles.size() == 3);
	CHECK(base.report.merged == 0);
	CHECK(base.links.size() == 3); // proximity bridge cannot merge lamps
	auto duplicate = Catalog(Request({a.Provider("first"), a.Provider("second")}));
	CHECK(duplicate.bundles.size() == 3);
	CHECK(duplicate.models.size() == 3);
	CHECK(duplicate.mapping.size() == 6);
	CHECK(duplicate.report.merged == 3);
	for (auto &b : duplicate.bundles)
	{
		CHECK(b.evidence.size() == 2);
		CHECK(b.inputs.size() == 2);
		CHECK(b.multiplicityUnknown);
		CHECK(b.provenance != 0);
	}
	for (auto &m : duplicate.models)
		CHECK(m.parts.size() == 1);
	Inputs unavailable = a;
	for (auto &z : unavailable.annotations)
		z.supported = false;
	auto redundant = Catalog(Request({unavailable.Provider("unavailable-copy"), a.Provider("usable-copy")}));
	for (const auto &m : redundant.models)
		CHECK(m.geometryKnown);
	C::Cursor rc;
	C::Budget rb;
	CHECK(Wave(redundant, rb, rc).tickets.size() == 3);
	// A missing UNIQUE fragment is different from a redundant unavailable copy.
	unavailable.annotations[0].supported = true;
	for (auto &z : unavailable.annotations)
	{
		z.sourceIdentity = Key("single exact union");
		z.fragmentSet = Key("three required parts");
	}
	auto partial = Catalog(Request({unavailable.Provider("incomplete-union")}));
	CHECK(partial.models.size() == 1);
	CHECK(!partial.models[0].geometryKnown);
	rc = {};
	CHECK(Wave(partial, rb, rc).tickets.empty());
	Inputs b = a;
	b.evidence[0].position.z = 11; // shared explicit identity, competing geometry
	a.annotations[0].sourceIdentity = Key("lamp-one");
	b.annotations[0].sourceIdentity = Key("lamp-one");
	b.candidates[0].shapes = CandidateSpot;
	b.evidence[0].shapes = CandidateSpot;
	auto alternatives = Catalog(Request({a.Provider("surface"), b.Provider("hidden")}));
	CHECK(alternatives.bundles.size() == 3);
	CHECK(alternatives.models.size() == 4);
	CHECK(alternatives.report.alternatives == 1);
	C::Cursor cursor;
	C::Budget budget;
	auto s = Wave(alternatives, budget, cursor);
	CHECK(s.tickets.size() == 3);
	s = Wave(alternatives, budget, cursor);
	CHECK(s.tickets.size() == 1);
	CHECK(s.exhausted);
	Inputs near;
	near.Add(21, {0, 0, 10}, CandidateRectangle, true);
	near.Add(22, {0, 0, 10}, CandidateRectangle, true);
	near.evidence[0].intensityKnown = near.evidence[1].intensityKnown = true;
	near.evidence[0].intensity = 0;
	near.evidence[1].intensity = 1000000;
	auto separate = Catalog(Request({near.Provider()}));
	CHECK(separate.bundles.size() == 2);
	CHECK(separate.models.size() == 2);
	CHECK(separate.report.merged == 0);
	C::Budget one;
	one.perHome = 1;
	C::Cursor pc;
	auto first = Wave(separate, one, pc);
	CHECK(first.tickets.size() == 1);
	CHECK(first.deferred.size() == 1);
	CHECK(first.deferred[0].reason == C::Deferred::ProtectedOverflow);
	auto second = Wave(separate, one, pc);
	CHECK(second.tickets.size() == 1);
	CHECK(second.exhausted);
	CHECK(C::Evidence(separate, separate.models[0].primary).intensity !=
		  C::Evidence(separate, separate.models[1].primary).intensity);
	// Attested contradiction retains original values and alternatives, no averaging.
	near.annotations[0].sourceIdentity = near.annotations[1].sourceIdentity = Key("explicit-one-source");
	auto conflict = Catalog(Request({near.Provider()}));
	CHECK(conflict.bundles.size() == 1);
	CHECK(conflict.models.size() == 2);
	CHECK(conflict.models[0].contradictoryPrior && conflict.models[1].contradictoryPrior);
}
void OrderAndLifetime()
{
	Inputs a;
	for (Id i = 1; i <= 30; ++i)
	{
		a.Add(i, {float(i), 0, 10}, i % 2 ? CandidatePoint : CandidateRectangle);
		C::Support s;
		s.observation = i + 900;
		s.surface = i;
		s.visibility = VisibilityState::Visible;
		a.annotations.back().support.push_back(s);
		a.annotations.back().regions = {3, 1};
	}
	auto p = a.Provider("one");
	auto original = Catalog(Request({p, a.Provider("two")}));
	auto sig = Semantic(original);
	a.Reverse();
	auto reordered = Catalog(Request({a.Provider("two"), a.Provider("one")}));
	CHECK(sig == Semantic(reordered));
	C::Budget budget;
	C::Cursor ca, cb;
	auto sa = Wave(original, budget, ca), sb = Wave(reordered, budget, cb);
	CHECK(sa.tickets.size() == sb.tickets.size());
	for (size_t i = 0; i < sa.tickets.size(); ++i)
		CHECK(sa.tickets[i].model == sb.tickets[i].model);
	std::weak_ptr<const void> weak = p.evidence.owner;
	p = {};
	CHECK(!weak.expired());
	CHECK(!C::Evidence(original, original.models[0].primary).material.empty());
	auto changed = a;
	changed.evidence[0].position.x += 1;
	CHECK(Catalog(Request({changed.Provider("one"), changed.Provider("two")})).key != original.key);
	auto rr = Request({a.Provider()});
	rr.geometryKey = Key("new geometry");
	rr.providers[0].geometryKey = rr.geometryKey;
	CHECK(Catalog(rr).bundles[0].id != reordered.bundles[0].id);
}
void ContextAndUnsupported()
{
	Inputs a;
	for (Id i = 1; i <= 4; ++i)
	{
		a.Add(i, {float(i * 3), 0, 10});
		a.annotations.back().profile = i % 2 ? "inverse-square" : "linear";
		a.annotations.back().sourceIdentity = Key("one hypothesis");
		a.annotations.back().scenario = Key(i <= 2 ? "scenario-a" : "scenario-b");
	}
	auto c = Catalog(Request({a.Provider()}));
	CHECK(c.models.size() == 4);
	C::Cursor cursor;
	C::Budget b;
	auto s = Wave(c, b, cursor);
	CHECK(s.tickets.empty());
	CHECK(s.deferred.size() == 4);
	for (auto &d : s.deferred)
		CHECK(d.reason == C::Deferred::Context);
	b.profile = "inverse-square";
	b.scenario = Key("scenario-a");
	cursor = {};
	s = Wave(c, b, cursor);
	CHECK(s.tickets.size() == 1);
	CHECK(s.deferred.size() == 3);
	CHECK(s.exhausted); // remaining scenarios are not coexisting lights
	b.profile = "linear";
	C::Selection failed;
	CHECK(C::Select(c, b, {}, cursor, failed).code == Code::InvalidState);
	Inputs unknown;
	unknown.Add(1, {0, 0, 0});
	unknown.evidence[0].positionKnown = false;
	unknown.Add(2, {0, 0, 10});
	unknown.annotations[1].nature = C::Nature::TransportEvidence;
	auto u = Catalog(Request({unknown.Provider()}));
	cursor = {};
	b = {};
	s = Wave(u, b, cursor);
	CHECK(s.tickets.size() == 1);
	CHECK(Model(u, s.tickets[0].model).sourceClass == C::Class::Transport);
	CHECK(s.deferred[0].reason == C::Deferred::UnsupportedGeometry);
	CHECK(u.report.inputs == 2); // unsupported never becomes black or a fabricated location
}
class Cancel final : public Cancellation
{
  public:
	mutable size_t calls = 0;
	size_t at = 1;
	bool IsCancelled() const noexcept override { return ++calls >= at; }
};
void FailureSafety()
{
	Inputs a;
	a.Add(1, {0, 0, 10});
	auto good = Request({a.Provider()});
	C::Catalog out;
	auto invalid = good;
	invalid.providers[0].map = Key("wrong map");
	CHECK(C::Consolidate(invalid, {}, out).code == Code::InvalidInput);
	CHECK(out.models.empty());
	a.candidates[0].evidence = {55};
	CHECK(C::Consolidate(Request({a.Provider()}), {}, out).code == Code::InvalidInput);
	a.candidates[0].evidence = {0};
	a.annotations[0].fragmentSet = Key("fragment");
	CHECK(C::Consolidate(Request({a.Provider()}), {}, out).code == Code::InvalidInput);
	a.annotations[0].fragmentSet = {};
	a.evidence[0].position.x = std::numeric_limits<float>::quiet_NaN();
	CHECK(C::Consolidate(Request({a.Provider()}), {}, out).code == Code::InvalidInput);
	invalid = good;
	invalid.limits.maxBytes = 8;
	CHECK(C::Consolidate(invalid, {}, out).code == Code::MemoryLimit);
	CHECK(out.models.empty());
	invalid = good;
	invalid.limits.maxCandidates = 0;
	CHECK(C::Consolidate(invalid, {}, out).code == Code::MemoryLimit);
	Cancel stop;
	ReadContext ctx;
	ctx.cancellation = &stop;
	CHECK(C::Consolidate(good, ctx, out).code == Code::Cancelled);
	auto c = Catalog(good);
	C::Cursor cursor;
	C::Budget b;
	auto s = Wave(c, b, cursor);
	auto saved = cursor;
	stop.calls = 0;
	C::Selection failed;
	CHECK(C::Select(c, b, ctx, cursor, failed).code == Code::Cancelled);
	CHECK(cursor.nextPart == saved.nextPart && cursor.nextHome == saved.nextHome);
	CHECK(failed.tickets.empty());
	cursor = {};
	b.maxBytes = 1;
	CHECK(C::Select(c, b, {}, cursor, failed).code == Code::MemoryLimit);
	CHECK(cursor.catalog == Disk::Digest{});
	b = {};
	b.maxGeometryWork = 1;
	a = Inputs();
	a.Add(1, {0, 0, 10});
	a.annotations[0].geometryWork = 2;
	auto over = Catalog(Request({a.Provider()}));
	s = Wave(over, b, cursor);
	CHECK(s.tickets.empty());
	CHECK(s.deferred[0].reason == C::Deferred::Oversize);
	b.maxGeometryWork = 2;
	s = Wave(over, b, cursor);
	CHECK(s.tickets.size() == 1);
}
struct Free final : PointClassifier
{
	Error Classify(Vec3, double, PointDomain &p) const override
	{
		p = PointDomain::Free;
		return {};
	}
};
std::shared_ptr<const StaticVisibility> Wall(bool alpha = false)
{
	SurfaceCapture capture;
	capture.Begin(BuildPolicy::ForceRebuild);
	LightmapMapIdentity map;
	map.sha256 = Key("map");
	map.path = "maps/own.bsp";
	map.bytes = 1;
	CHECK(capture.SetMap(map));
	BakedSurface s;
	s.kind = BakedSurfaceKind::Face;
	s.motion = SurfaceMotion::StaticWorld;
	s.materialFacts.available = true;
	s.materialFacts.cull = SurfaceCull::None;
	s.materialFacts.signals = alpha ? uint32_t(MaterialCutout) : 0u;
	for (auto p : {Vec3(0, -100, 0), Vec3(0, 100, 0), Vec3(0, 100, 100), Vec3(0, -100, 100)})
	{
		BakedVertex v;
		v.position = p;
		s.vertices.push_back(v);
	}
	s.indices = {0, 1, 2, 0, 2, 3};
	CHECK(capture.Add(s));
	CHECK(capture.Freeze());
	std::shared_ptr<const StaticVisibility> scene;
	ReadContext context;
	context.maxBytes = 128ull * 1024 * 1024;
	CHECK(StaticVisibility::Build(capture.Acquire(), {}, context, scene, std::make_shared<Free>()));
	return scene;
}
void GeometryLinks()
{
	Inputs a;
	a.Add(1, {-2, 0, 10});
	a.Add(2, {2, 0, 10});
	a.annotations[0].regions = {17, 18};
	a.annotations[1].regions = {18};
	C::Support s;
	s.observation = 33;
	s.surface = 2;
	s.visibility = VisibilityState::Visible;
	a.annotations[0].support.push_back(s);
	a.annotations[1].support.push_back(s);
	auto r = Request({a.Provider()});
	r.visibility = Wall();
	auto c = Catalog(r);
	CHECK(c.bundles.size() == 2);
	CHECK(c.links.size() == 1);
	CHECK(c.links[0].kind == C::LinkKind::Blocked);
	CHECK(c.links[0].visibility == VisibilityState::Blocked);
	CHECK(c.links[0].sharedSupport);
	CHECK(c.links[0].sameRegion); // room label cannot overrule the wall
	CHECK(c.models[0].regions.size() + c.models[1].regions.size() == 3);
	r.visibility = Wall(true);
	auto uncertain = Catalog(r);
	CHECK(uncertain.links[0].visibility == VisibilityState::Unknown);
	CHECK(uncertain.bundles.size() == 2);
	r.visibility.reset();
	r.limits.maxPairTests = 0;
	auto bounded = Catalog(r);
	CHECK(!bounded.report.linksComplete);
	CHECK(bounded.models.size() == 2);
	CHECK(bounded.links.empty());
	r.limits.maxPairTests = 100;
	r.limits.maxRelationWork = 0;
	auto relations = Catalog(r);
	CHECK(!relations.links[0].relationsComplete);
	CHECK(!relations.report.linksComplete);
}
class SplitDomain final : public RegionDomain
{
  public:
	Error Locate(Vec3 p, SpatialLocation &o, double t = 1e-5) const override
	{
		o.work = 1;
		o.space = std::abs(p.x) <= t ? SpaceClass::Unknown : SpaceClass::Free;
		return {};
	}
	Error ClassifyBox(const Bounds &b, SpaceClass &s, uint64_t &w) const override
	{
		w = 1;
		s = b.minWorld.x <= 0 && b.maxWorld.x >= 0 ? SpaceClass::Unknown : SpaceClass::Free;
		return {};
	}
};
void RegionsAndAdaptiveQuota()
{
	auto scene = Wall();
	RegionBuildInput input;
	input.identity = scene->GeometryIdentity();
	input.geometry = scene;
	input.domain = std::make_shared<SplitDomain>();
	RegionOptions options;
	options.bounds.known = true;
	options.bounds.minWorld = Vec3(-16, -16, 1);
	options.bounds.maxWorld = Vec3(16, 16, 25);
	options.coarseCellWorld = 8;
	options.minimumCellWorld = 2;
	options.inspectSky = false;
	ReadContext ctx;
	ctx.maxBytes = 128ull * 1024 * 1024;
	std::shared_ptr<const LightingRegionGraph> graph;
	CHECK(LightingRegionGraph::Build(input, options, ctx, graph));
	CHECK(graph && graph->Regions().size() == 2);
	if (!graph)
		return;
	RegionAssignment left, right;
	CHECK(graph->Lookup(Vec3(-8, 0, 10), ctx, left));
	CHECK(graph->Lookup(Vec3(8, 0, 10), ctx, right));
	CHECK(left.region && right.region && left.region != right.region);
	Inputs a;
	a.Add(1, {-8, 0, 10});
	a.annotations.back().regions = {left.region, right.region};
	a.Add(2, {8, 0, 10});
	a.annotations.back().regions = {right.region};
	auto p = a.Provider();
	p.geometryKey = scene->CertificationKey();
	p.regionKey = graph->CertificationKey();
	auto r = Request({p});
	r.geometryKey = p.geometryKey;
	r.regionKey = p.regionKey;
	r.regions = graph;
	r.visibility = scene;
	auto c = Catalog(r);
	for (const auto &b : c.bundles)
		CHECK(b.home.kind == C::Home::Kind::Region);
	CHECK(c.links[0].visibility == VisibilityState::Blocked);
	CHECK(c.models[0].regions.size() + c.models[1].regions.size() == 3);
	r.geometryKey = Key("stale geometry");
	r.providers[0].geometryKey = r.geometryKey;
	C::Catalog failed;
	CHECK(C::Consolidate(r, ctx, failed).code == Code::InvalidInput);
	CHECK(failed.models.empty());
	// Equal brightness, distinct certified-visible receiver surfaces: an adaptive
	// extra quota admits additional coverage without increasing probabilities.
	Inputs coverage;
	for (Id i = 1; i <= 8; ++i)
	{
		coverage.Add(i, {float(i), 0, 10});
		C::Support s;
		s.observation = i;
		s.surface = i;
		s.visibility = VisibilityState::Visible;
		coverage.annotations.back().support.push_back(s);
	}
	auto many = Catalog(Request({coverage.Provider()}));
	C::Budget b;
	b.perClass[size_t(C::Class::Point)] = 1;
	b.perHome = 8;
	b.coverageBonusPerHome = 2;
	C::Cursor cursor;
	auto chosen = Wave(many, b, cursor);
	CHECK(chosen.tickets.size() == 3);
	C::Budget noBonus = b;
	noBonus.coverageBonusPerHome = 0;
	cursor = {};
	CHECK(Wave(many, noBonus, cursor).tickets.size() == 1);
	Cancel midway;
	midway.at = 2;
	ctx.cancellation = &midway;
	cursor = {};
	C::Selection out;
	CHECK(C::Select(many, b, ctx, cursor, out).code == Code::Cancelled);
	CHECK(cursor.catalog == Disk::Digest{});
	CHECK(out.tickets.empty());
}
Solar::Hypothesis SolarFixture(float intensity)
{
	Solar::Hypothesis x;
	x.fitted.profile.id = "verified-feature-scope";
	x.fitted.toSource = Vec3(0, 0, 1);
	x.fitted.sunRgb = Vec3(intensity, intensity, intensity);
	x.fitted.skyRgb = Vec3(10, 10, 10);
	x.fitted.model = Solar::Model::SunAndSky;
	auto &a = x.asset;
	a.lineage = "own-content/solar-budget-test";
	a.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::LegacyLightmap;
	p.locator = "own-content solar fixture";
	p.producer = "candidate-pool-test/1";
	a.provenanceRecords.push_back(p);
	Calibration cal;
	cal.id = 2;
	cal.provenance = 1;
	cal.kind = CalibrationKind::UncalibratedRelative;
	a.calibrations.push_back(cal);
	PhysicalEmitter e;
	e.source.id = 10;
	e.source.provenance = 1;
	e.kind = EmitterKind::Directional;
	e.frame.known = true;
	e.frame.forward = Vec3(0, 0, -1);
	e.frame.right = Vec3(1, 0, 0);
	e.frame.up = Vec3(0, -1, 0);
	e.angular.kind = AngularKind::Parallel;
	e.emission.unit = PhotometricUnit::Relative;
	e.emission.calibration = 2;
	e.emission.intensity.known = true;
	e.emission.intensity.value = intensity;
	e.emission.color.known = true;
	e.emission.color.rgb = Vec3(1, 1, 1);
	a.emitters.push_back(e);
	SunSource sun;
	sun.emitter = 10;
	a.suns.push_back(sun);
	EnvironmentLighting env;
	env.source.id = 20;
	env.source.provenance = 1;
	env.meanLuminance = e.emission;
	env.meanLuminance.intensity.value = 10;
	env.sunCoverage = SunCoverage::ExcludesExplicitSun;
	a.environments.push_back(env);
	return x;
}
void Adapters()
{
	// Existing source extractor -> common provider, including exact authored raw/unit data.
	EmitterEvidenceInput input;
	input.map.path = "maps/own.bsp";
	input.map.sha256 = Key("map");
	input.map.bytes = 1;
	input.entityText = "{\n\"classname\" \"light\"\n\"origin\" \"1 2 3\"\n\"light\" \"300\"\n}\n";
	std::shared_ptr<const EmitterEvidenceSet> extracted;
	ReadContext context;
	context.maxBytes = 128ull * 1024 * 1024;
	CHECK(ExtractEmitterEvidence(input, {}, context, extracted));
	C::Provider p;
	CHECK(C::FromEvidence("authored", Key("geometry"), {}, extracted, p));
	auto c = Catalog(Request({p}));
	CHECK(!c.models.empty());
	extracted.reset();
	for (auto &m : c.models)
	{
		CHECK(m.protectedAuthored);
		CHECK(C::Evidence(c, m.primary).unit == EvidenceUnit::LegacyBakeTool);
	}
	auto hidden = std::make_shared<Hidden::Result>();
	Inputs a;
	a.Add(4, {1, 2, 10});
	hidden->key = Key("hidden");
	hidden->evidence = a.evidence;
	hidden->candidates = a.candidates;
	Hidden::Hypothesis h;
	h.candidate = a.candidates[0].id;
	h.profile = "assumed profile";
	h.region = 7;
	h.kind = Hidden::Kind::AmbiguousSourceOrTransport;
	Hidden::SupportingObservation support;
	support.observation = 5;
	support.surface = 6;
	support.relation = Hidden::Relation::Unknown;
	h.observations.push_back(support);
	hidden->hypotheses.push_back(h);
	CHECK(C::FromHidden("hidden", Key("map"), Key("geometry"), {}, hidden, p));
	c = Catalog(Request({p}));
	hidden.reset();
	CHECK(c.models.size() == 1);
	CHECK(c.models[0].nature == C::Nature::AmbiguousSourceOrTransport);
	CHECK(c.models[0].profile == "assumed profile");
	CHECK(c.models[0].support[0].visibility == VisibilityState::Unknown);
	CHECK(c.providers[0].extraOwner != nullptr);
	// Valid empty Solar alternatives: leases retain full canonical calibration/
	// environment payload. Numerical Solar coverage stays in its own test suite.
	auto solar = std::make_shared<Solar::Result>();
	Solar::Hypothesis x;
	x.fitted.profile.id = "known-scope";
	x.fitted.toSource = Vec3(0, 0, 1);
	solar->hypotheses.push_back(x);
	std::vector<C::Provider> ps;
	CHECK(C::FromSolar("solar", Key("map"), Key("geometry"), solar, ps));
	CHECK(ps.size() == 1);
	CHECK(ps[0].canonical != nullptr);
	CHECK(ps[0].extraOwner != nullptr);
	solar.reset();
	CHECK(ps[0].canonical->emitters.empty());
	auto positive = std::make_shared<Solar::Result>();
	positive->hypotheses.push_back(SolarFixture(100));
	positive->hypotheses.push_back(SolarFixture(200));
	std::string invalidAsset;
	if (!Validate(positive->hypotheses[0].asset, &invalidAsset))
		std::printf("solar asset: %s\n", invalidAsset.c_str());
	CHECK(Validate(positive->hypotheses[0].asset));
	CHECK(C::FromSolar("solar", Key("map"), Key("geometry"), positive, ps));
	auto joint = Catalog(Request(ps));
	CHECK(joint.models.size() == 4);
	CHECK(joint.bundles.size() == 2);
	if (joint.models.empty())
		return;
	C::Cursor cursor;
	C::Budget budget;
	auto unresolved = Wave(joint, budget, cursor);
	CHECK(unresolved.tickets.empty());
	budget.scenario = ps[0].generation;
	budget.profile = "verified-feature-scope";
	cursor = {};
	auto one = Wave(joint, budget, cursor);
	CHECK(one.tickets.size() == 2);
	for (const auto &t : one.tickets)
		CHECK(Model(joint, t.model).scenario == ps[0].generation);
	positive.reset();
	for (const auto &v : joint.providers)
	{
		CHECK(v.canonical->environments[0].sunCoverage == SunCoverage::ExcludesExplicitSun);
		CHECK(v.canonical->emitters[0].emission.unit == PhotometricUnit::Relative);
		CHECK(!v.canonical->emitters[0].source.evidence.physicalEmitterConfidence.known);
	}
}
struct Metrics
{
	uint64_t inputs = 0, bundles = 0, models = 0, firstPoint = 0, firstSpot = 0, firstArea = 0, allPoint = 0,
			 allSpot = 0, allArea = 0, waves = 0, deferred = 0, bytes = 0, links = 0, parts = 0,
			 partWaves = 0;
	std::string key;
} metrics;
std::vector<C::Statistics> firstRows;
double largeBuildSeconds = 0, largeSelectSeconds = 0, unionBuildSeconds = 0, unionSelectSeconds = 0;
double Seconds(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
void LargeDiversity()
{
	Inputs a;
	Id id = 1;
	const unsigned rooms = 24, areaPerRoom = 250;
	for (unsigned room = 0; room < rooms; ++room)
	{
		for (unsigned f = 0; f < areaPerRoom; ++f)
			a.Add(id++, {float(room * 1024 + f % 16), float(f / 16), 20}, CandidateRectangle);
		a.Add(id++, {float(room * 1024 + 20), 20, 30}, CandidatePoint, true);
		a.evidence.back().intensityKnown = true;
		a.evidence.back().intensity = .00001f;
		a.Add(id++, {float(room * 1024 + 30), 20, 30}, CandidateSpot);
		a.annotations.back().tier = C::Tier::SurfaceProposal;
	}
	auto r = Request({a.Provider("6000-fragments-and-lamps")});
	r.limits.maxPairTests = 1000;
	r.limits.maxLinks = 1000;
	r.limits.maxNeighbours = 4;
	auto begin = std::chrono::steady_clock::now();
	auto c = Catalog(r);
	largeBuildSeconds = Seconds(begin);
	metrics.inputs = c.report.inputs;
	metrics.bundles = c.report.bundles;
	metrics.models = c.report.models;
	metrics.bytes = c.report.accountedBytes;
	metrics.links = c.report.pairTests;
	metrics.key = Disk::Hex(c.key);
	CHECK(c.bundles.size() == rooms * (areaPerRoom + 2));
	CHECK(c.report.merged == 0);
	CHECK(!c.report.linksComplete);
	C::Budget budget;
	budget.maxTickets = 192;
	C::Cursor cursor;
	std::set<Id> all;
	for (unsigned wave = 0; wave < 100; ++wave)
	{
		begin = std::chrono::steady_clock::now();
		auto s = Wave(c, budget, cursor);
		largeSelectSeconds += Seconds(begin);
		if (wave == 0)
		{
			metrics.deferred = s.deferred.size();
			for (auto &t : s.tickets)
			{
				auto cls = Model(c, t.model).sourceClass;
				if (cls == C::Class::Point)
					++metrics.firstPoint;
				if (cls == C::Class::Spot)
					++metrics.firstSpot;
				if (cls == C::Class::Area)
					++metrics.firstArea;
			}
			CHECK(metrics.firstPoint == rooms);
			CHECK(metrics.firstSpot == rooms);
			CHECK(metrics.firstArea == 4 * rooms);
			auto rows = C::Summarize(c, s);
			firstRows = rows;
			uint64_t count = 0;
			for (auto &row : rows)
				count += row.models;
			CHECK(count == c.models.size());
		}
		for (auto &t : s.tickets)
		{
			CHECK(t.completesModel);
			CHECK(all.insert(t.model).second);
		}
		++metrics.waves;
		if (s.exhausted)
			break;
	}
	for (Id mid : all)
	{
		auto cls = Model(c, mid).sourceClass;
		if (cls == C::Class::Point)
			++metrics.allPoint;
		if (cls == C::Class::Spot)
			++metrics.allSpot;
		if (cls == C::Class::Area)
			++metrics.allArea;
	}
	CHECK(all.size() == c.models.size());
	CHECK(metrics.allPoint == rooms);
	CHECK(metrics.allSpot == rooms);
	CHECK(metrics.allArea == rooms * areaPerRoom);
	CHECK(metrics.waves < 100);
	// Bounded first wave with many homes; later homes must receive work.
	C::Budget small;
	small.maxTickets = 1;
	small.perHome = 1;
	small.perClass[size_t(C::Class::Area)] = 0;
	C::Cursor fairness;
	std::set<int64_t> homes;
	for (unsigned wave = 0; wave < rooms; ++wave)
	{
		auto s = Wave(c, small, fairness);
		CHECK(s.tickets.size() == 1);
		Id bundle = s.tickets[0].bundle;
		auto b = std::find_if(c.bundles.begin(), c.bundles.end(),
							  [&](const C::Bundle &x) { return x.id == bundle; });
		homes.insert(b->home.cell[0]);
	}
	CHECK(homes.size() == rooms);
	// More unexplained-error work can be requested locally without changing source IDs.
	C::Budget expand;
	expand.maxTickets = 1000;
	expand.maxGeometryWork = 1000;
	C::Demand d;
	d.home = c.bundles[0].home;
	d.classes = 1u << uint32_t(C::Class::Area);
	d.extraPerClass = 3;
	d.reason = "controller requests refinement in unexplained training-side region";
	expand.more.push_back(d);
	C::Cursor fresh;
	auto s = Wave(c, expand, fresh);
	size_t localAreas = 0;
	for (auto &t : s.tickets)
		if (Model(c, t.model).sourceClass == C::Class::Area)
		{
			auto b = std::find_if(c.bundles.begin(), c.bundles.end(),
								  [&](const C::Bundle &x) { return x.id == t.bundle; });
			if (b->home.cell[0] == d.home.cell[0])
				++localAreas;
		}
	CHECK(localAreas == 7);
}
void FragmentUnion()
{
	Inputs a;
	const size_t n = 2048;
	for (size_t i = 0; i < n; ++i)
	{
		a.Add(i + 1, {float(i % 128), float(i / 128), 20}, CandidateRectangle);
		a.annotations.back().sourceIdentity = Key("upstream-proven-connected-panel");
		a.annotations.back().fragmentSet = Key("exact-mesh-union");
	}
	auto r = Request({a.Provider("exact connected fragment union")});
	r.limits.maxPairTests = 0;
	auto begin = std::chrono::steady_clock::now();
	auto c = Catalog(r);
	unionBuildSeconds = Seconds(begin);
	CHECK(c.bundles.size() == 1);
	CHECK(c.models.size() == 1);
	CHECK(c.models[0].parts.size() == n);
	CHECK(c.models[0].fragmentUnion);
	CHECK(c.models[0].evidence.size() == n);
	C::Cursor cursor;
	C::Budget b;
	b.maxPartsPerTicket = 32;
	std::set<size_t> visited;
	for (unsigned wave = 0; wave < 100; ++wave)
	{
		begin = std::chrono::steady_clock::now();
		auto s = Wave(c, b, cursor);
		unionSelectSeconds += Seconds(begin);
		CHECK(s.tickets.size() == 1);
		for (auto &t : s.tickets)
		{
			for (size_t i = t.firstPart; i < t.firstPart + t.partCount; ++i)
				CHECK(visited.insert(i).second);
			CHECK(t.completesModel == s.exhausted);
		}
		++metrics.partWaves;
		if (s.exhausted)
			break;
	}
	CHECK(visited.size() == n);
	metrics.parts = visited.size();
	CHECK(metrics.partWaves == 64);
}
int main(int argc, char **argv)
{
	auto start = std::chrono::steady_clock::now();
	StrictUnions();
	OrderAndLifetime();
	ContextAndUnsupported();
	FailureSafety();
	GeometryLinks();
	RegionsAndAdaptiveQuota();
	Adapters();
	LargeDiversity();
	FragmentUnion();
	std::printf("candidate consolidation: %d checks, %d failures, %.3f s\n", checks, failures,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	if (argc > 1)
	{
		std::ofstream f(argv[1]);
		f << "{\n  \"schema\": \"org.openjk.candidate-pool-validation\", \"version\": 1,\n  \"checks\": "
		  << checks << ", \"failures\": " << failures << ",\n  \"catalog_key\": \"" << metrics.key
		  << "\",\n  \"inputs\": " << metrics.inputs << ", \"bundles\": " << metrics.bundles
		  << ", \"models\": " << metrics.models << ",\n  \"first_wave\": {\"point\": " << metrics.firstPoint
		  << ", \"spot\": " << metrics.firstSpot << ", \"area\": " << metrics.firstArea
		  << ", \"deferred\": " << metrics.deferred << "},\n  \"all_waves\": {\"point\": " << metrics.allPoint
		  << ", \"spot\": " << metrics.allSpot << ", \"area\": " << metrics.allArea
		  << ", \"waves\": " << metrics.waves << "},\n  \"conservative_accounted_bytes\": " << metrics.bytes
		  << ", \"pair_tests\": " << metrics.links << ",\n  \"exact_union_parts\": " << metrics.parts
		  << ", \"union_waves\": " << metrics.partWaves << "\n}\n";
		if (!f)
			return 2;
		std::ofstream csv(std::string(argv[1]) + ".csv");
		csv << "home_kind,region,cell_x,cell_y,cell_z,class,provider,reason,models,selected,protected_models,"
			   "parts\n";
		for (const auto &r : firstRows)
			csv << int(r.home.kind) << ',' << r.home.region << ',' << r.home.cell[0] << ',' << r.home.cell[1]
				<< ',' << r.home.cell[2] << ',' << C::ClassName(r.sourceClass) << ',' << r.provider << ','
				<< r.reason << ',' << r.models << ',' << r.selected << ',' << r.protectedModels << ','
				<< r.parts << '\n';
		std::ofstream performance(std::string(argv[1]) + ".resources.json");
		performance << std::setprecision(9) << "{\"large_consolidate_seconds\": " << largeBuildSeconds
					<< ", \"large_63_waves_seconds\": " << largeSelectSeconds
					<< ", \"union_consolidate_seconds\": " << unionBuildSeconds
					<< ", \"union_64_waves_seconds\": " << unionSelectSeconds << "}\n";
		if (!csv || !performance)
			return 2;
	}
	return failures ? 1 : 0;
}
