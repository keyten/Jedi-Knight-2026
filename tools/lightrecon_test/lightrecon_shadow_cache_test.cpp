// P05 controlled geometric evidence; no game data, source recovery or renderer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/photometry.h"
#include "lightrecon/shadow_cache.h"
#include "probe_fixtures.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
using namespace ProbeFixture;
using namespace Rend2LightRecon::Core::Bake::Detail;
namespace S = Rend2LightRecon::Core::ShadowCache;
namespace Placement = Rend2LightRecon::Core::PointRefine;
int checks = 0, failures = 0;
bool countAllocations = false;
uint64_t allocations = 0;
void *operator new(std::size_t n)
{
	if (countAllocations)
		++allocations;
	if (void *p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
	std::free(p);
}
void *operator new[](std::size_t n)
{
	return ::operator new(n);
}
void operator delete[](void *p) noexcept
{
	::operator delete(p);
}
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n';                                \
		}                                                                                          \
	} while (0)
PhysicalEmitter Emitter(Id id, EmitterKind kind, Vec3 position)
{
	PhysicalEmitter e;
	e.source.id = id;
	e.source.provenance = 1;
	e.source.state = HypothesisState::Resolved;
	e.position.known = kind != EmitterKind::Directional;
	e.position.world = position;
	e.kind = kind;
	e.frame.known = true;
	e.frame.right = Vec3(1, 0, 0);
	e.frame.up = Vec3(0, -1, 0);
	e.frame.forward = Vec3(0, 0, -1);
	e.geometry.known = true;
	e.geometry.halfWidthWorld = 1;
	e.geometry.halfHeightWorld = .6f;
	e.geometry.halfLengthWorld = 2;
	e.geometry.radiusWorld = .2f;
	e.angular.kind = kind == EmitterKind::Point
						 ? AngularKind::Isotropic
						 : kind == EmitterKind::Spot
							   ? AngularKind::SmoothCone
							   : kind == EmitterKind::Directional ? AngularKind::Parallel
																  : AngularKind::Lambertian;
	e.angular.cosInner = .9f;
	e.angular.cosOuter = .75f;
	e.emission = Photometric::RGBToPhotometry(Vec3(10, 10, 10),
											  kind == EmitterKind::Directional
												  ? PhotometricUnit::Lux
												  : kind == EmitterKind::Rectangle ||
															kind == EmitterKind::Tube
														? PhotometricUnit::Nits
														: PhotometricUnit::Candela,
											  1)
					 .value;
	return e;
}
ReconstructedLightingAsset Asset()
{
	ReconstructedLightingAsset a;
	a.lineage = "owned-shadow-cache";
	a.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "shadow-cache-test/1";
	a.provenanceRecords.push_back(p);
	Calibration c;
	c.id = 1;
	c.provenance = 1;
	c.kind = CalibrationKind::PhysicallyAuthored;
	a.calibrations.push_back(c);
	return a;
}
struct Rig
{
	Scene scene;
	Probes::Input geometry;
	ReconstructedLightingAsset asset = Asset();
	Rig()
	{
		Refresh();
	}
	void Refresh()
	{
		geometry = scene.Input();
	}
	S::Request Request()
	{
		S::Request r;
		r.asset = std::make_shared<const ReconstructedLightingAsset>(asset);
		r.scene.map = geometry.visibility->GeometryIdentity();
		r.scene.domain = scene.domain;
		r.scene.domainKey = geometry.domainKey;
		r.scene.geometry = geometry.visibility;
		r.dataset = 900;
		r.provenance = 1;
		r.scope = "owned supported static visual geometry, no runtime occluders";
		r.options.optical.facing = FacingPolicy::TwoSided;
		return r;
	}
};
S::Item SourceItem(Id source, Id receiver, Vec3 p)
{
	S::Item i;
	i.source = source;
	i.receiver.id = receiver;
	i.receiver.positionWorld = p;
	return i;
}
S::Item Segment(Id receiver, Vec3 a, Vec3 b)
{
	S::Item i;
	i.kind = ShadowQueryKind::Segment;
	i.receiver.id = receiver;
	i.receiver.positionWorld = a;
	i.targetOrDirection = b;
	return i;
}
S::Artifact Build(const S::Request &r)
{
	S::Artifact out;
	auto e = S::Build(r, ReadContext(), out);
	if (!e)
		std::cerr << "BUILD " << unsigned(e.code) << ' ' << e.message << '\n';
	Check(e);
	return out;
}
ShadowSupportSample Sample(const S::Artifact &a, const S::Item &i)
{
	std::shared_ptr<const S::Prepared> p;
	Check(S::Prepare(a, ReadContext(), p));
	ShadowSupportQuery q;
	q.dataset = a.descriptor.dataset;
	q.receiver = i.receiver.id;
	q.source.id = i.source;
	q.kind = i.kind;
	q.receiverWorld = i.receiver.positionWorld;
	q.targetOrDirection = i.targetOrDirection;
	q.scenario = a.descriptor.scenario;
	ShadowSupportSample v;
	CHECK(p->ShadowSupport(a.asset, q, v) == Status::Ok);
	return v;
}
struct Cancel : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
struct CancelDuring : Cancellation
{
	mutable unsigned calls = 0;
	bool IsCancelled() const noexcept override
	{
		return ++calls > 50;
	}
};
double DenseArea(const PhysicalEmitter &e, Vec3 p, unsigned n, bool tube = false)
{
	double visible = 0, total = 0;
	const double pi = 3.141592653589793;
	for (unsigned x = 0; x < n; ++x)
		for (unsigned y = 0; y < n; ++y)
		{
			Vec3 node;
			V normal;
			if (!tube)
			{
				node = (V(e.position.world) +
						V(e.frame.right) * ((2 * (x + .5) / n - 1) * e.geometry.halfWidthWorld) +
						V(e.frame.up) * ((2 * (y + .5) / n - 1) * e.geometry.halfHeightWorld))
						   .Float();
				normal = V(e.frame.forward);
			}
			else
			{
				double theta = 2 * pi * (y + .5) / n;
				normal = V(e.frame.up) * std::cos(theta) + V(e.frame.forward) * std::sin(theta);
				node = (V(e.position.world) +
						V(e.frame.right) * ((2 * (x + .5) / n - 1) * e.geometry.halfLengthWorld) +
						normal * e.geometry.radiusWorld)
						   .Float();
			}
			V delta = V(node) - V(p);
			double distance = Length(delta), cosine = -Dot(normal, delta * (1 / distance));
			if (cosine <= 0)
				continue;
			double weight = cosine / (distance * distance);
			// Independent axis-aligned slab clipping of the actual finite-thickness box.
			V low(Vec3(-.01f, -4, 3)), high(Vec3(.01f, 4, 8));
			double enter = 0, leave = 1;
			bool blocked = true;
			for (unsigned axis = 0; axis < 3; ++axis)
			{
				double a = V(p)[axis], d = delta[axis];
				if (std::abs(d) < 1e-15)
				{
					if (a < low[axis] || a > high[axis])
						blocked = false;
				}
				else
				{
					double t0 = (low[axis] - a) / d, t1 = (high[axis] - a) / d;
					if (t0 > t1)
						std::swap(t0, t1);
					enter = std::max(enter, t0);
					leave = std::min(leave, t1);
				}
			}
			blocked = blocked && enter <= leave;
			total += weight;
			if (!blocked)
				visible += weight;
		}
	return visible / total;
}
int main(int argc, char **argv)
{
	try
	{
		Rig open;
		open.asset.emitters = {Emitter(10, EmitterKind::Point, Vec3(2, 0, 3)),
							   Emitter(11, EmitterKind::Point, Vec3(-2, 0, 3))};
		auto request = open.Request();
		request.items = {SourceItem(10, 1, Vec3(-2, 0, 1)), SourceItem(11, 2, Vec3(2, 0, 1)),
						 Segment(3, Vec3(-2, 0, 1), Vec3(2, 0, 1))};
		auto cache = Build(request);
		for (auto &r : cache.records)
		{
			CHECK(r.result.staticGeometry.estimateKnown && r.result.staticGeometry.value == 1);
			CHECK(!r.result.runtimeUpdateRequired);
		}
		Manager manager;
		std::shared_ptr<const S::Prepared> prepared;
		CHECK(S::Prepare(cache, ReadContext(), prepared));
		S::Coverage coverage;
		CHECK(prepared->SourceCoverage(10, coverage) == Status::Ok && coverage.records == 1 &&
			  coverage.known == 1);
		CHECK(prepared->RegionCoverage(0, coverage) == Status::Ok && coverage.records == 3);
		CHECK(prepared->SourceCoverage(999, coverage) == Status::NotFound);
		CHECK(manager.Publish(cache.asset, Completeness::Partial, prepared) == Status::Ok);
		auto snapshot = manager.Acquire();
		ShadowSupportQuery q;
		q.dataset = 900;
		q.receiver = 1;
		q.source = snapshot.IdentifySource(10).value;
		q.receiverWorld = Vec3(-2, 0, 1);
		auto value = snapshot.QueryShadowSupport(q);
		CHECK(value && value.value.generation == snapshot.Generation());
		auto old = snapshot;
		manager.Reset();
		CHECK(old.QueryShadowSupport(q));
		CHECK(manager.Publish(cache.asset, Completeness::Partial, prepared) == Status::Ok);
		CHECK(manager.Acquire().QueryShadowSupport(q).status == Status::StaleHandle);
		manager.Reset();
		CHECK(manager.Acquire().QueryShadowSupport(q).status != Status::Ok);
		auto wrong = q;
		wrong.source.id = 999;
		CHECK(old.QueryShadowSupport(wrong).status == Status::NotFound);
		wrong = q;
		wrong.receiver = 999;
		CHECK(old.QueryShadowSupport(wrong).status == Status::Unavailable);
		wrong = q;
		wrong.receiverWorld.x += .01f;
		CHECK(old.QueryShadowSupport(wrong).status == Status::Unavailable);
		VisibilityQuery segment;
		segment.receiverWorld = Vec3(-2, 0, 1);
		segment.targetWorld = Vec3(2, 0, 1);
		CHECK(old.QueryVisibility(segment) &&
			  old.QueryVisibility(segment).value.transmittance == 1);
		segment.targetWorld.y = .1f;
		CHECK(old.QueryVisibility(segment).status == Status::Unavailable);
		// Clear geometry is independent of BSP cluster/region labels.
		SpatialLocation left, right;
		CHECK(open.scene.domain->Locate(Vec3(-2, 0, 1), left));
		CHECK(open.scene.domain->Locate(Vec3(2, 0, 1), right));
		CHECK(left.cluster != right.cluster);
		auto reordered = request;
		std::reverse(reordered.items.begin(), reordered.items.end());
		auto second = Build(reordered);
		CHECK(cache.chunks.size() == second.chunks.size());
		for (size_t j = 0; j < cache.chunks.size(); ++j)
			CHECK(cache.chunks[j].bytes == second.chunks[j].bytes);
		// Exact slab reference for a thin world wall; same cluster is not a clear certificate.
		Rig wall;
		wall.scene.Solid(Box(Vec3(-.01f, -4, 0), Vec3(.01f, 4, 8)));
		wall.Refresh();
		wall.asset.emitters = open.asset.emitters;
		auto wr = wall.Request();
		for (unsigned j = 0; j < 64; ++j)
		{
			auto i = SourceItem(
				j % 2 ? 10 : 11, j + 1,
				Vec3(j % 2 ? -2.f : 2.f, -2.f + float(j % 16) * .25f, 1.f + float(j / 16) * .5f));
			i.receiver.region = 77;
			wr.items.push_back(i);
		}
		auto blocked = Build(wr);
		// Probe placement/support and direct occlusion are independent predicates.
		auto topology = wall.scene.Build(wall.geometry);
		std::vector<S::Item> probeItems;
		CHECK(S::ProbeItems(*topology, {10}, 20000, probeItems));
		CHECK(!probeItems.empty());
		auto probeRequest = wall.Request();
		probeRequest.items = probeItems;
		auto probeCache = Build(probeRequest);
		unsigned probeBlocked = 0, probeVisible = 0;
		for (const auto &row : probeCache.records)
		{
			probeBlocked +=
				row.result.staticGeometry.estimateKnown && row.result.staticGeometry.value == 0;
			probeVisible +=
				row.result.staticGeometry.estimateKnown && row.result.staticGeometry.value == 1;
		}
		CHECK(probeBlocked > 0 && probeVisible > 0);
		unsigned falseClear = 0;
		for (auto &r : blocked.records)
		{
			falseClear +=
				!r.result.staticGeometry.estimateKnown || r.result.staticGeometry.value != 0;
			CHECK(r.result.occluderDistanceKnown);
			CHECK(r.result.staticGeometry.value == 0 && r.result.staticGeometry.estimateKnown);
		}
		CHECK(falseClear == 0);
		Rig same;
		same.scene.Solid(Box(Vec3(1.99f, -4, 0), Vec3(2.01f, 4, 8)));
		same.Refresh();
		auto sameRequest = same.Request();
		sameRequest.items = {Segment(1, Vec3(1, 0, 1), Vec3(3, 0, 1))};
		CHECK(Sample(Build(sameRequest), sameRequest.items[0]).staticGeometry.value == 0);
		Rig door;
		door.scene.Solid(Box(Vec3(-.01f, -4, 0), Vec3(.01f, -.6f, 8)));
		door.scene.Solid(Box(Vec3(-.01f, .6f, 0), Vec3(.01f, 4, 8)));
		door.Refresh();
		door.asset.emitters = {Emitter(10, EmitterKind::Point, Vec3(2, 0, 3))};
		auto dr = door.Request();
		dr.items = {SourceItem(10, 1, Vec3(-2, 0, 1)), SourceItem(10, 2, Vec3(-2, 3, 1))};
		auto doors = Build(dr);
		CHECK(Sample(doors, dr.items[0]).staticGeometry.value == 1);
		CHECK(Sample(doors, dr.items[1]).staticGeometry.value == 0);
		Rig spot;
		spot.asset.emitters = {Emitter(20, EmitterKind::Spot, Vec3(0, 0, 6))};
		auto sr = spot.Request();
		sr.items = {SourceItem(20, 1, Vec3(0, 0, 1)), SourceItem(20, 2, Vec3(7, 0, 6))};
		auto spots = Build(sr);
		CHECK(Sample(spots, sr.items[0]).responseApplicable);
		auto outside = Sample(spots, sr.items[1]);
		CHECK(outside.staticGeometry.value == 1 && !outside.responseApplicable &&
			  (outside.issues & S::OutsideEmission));
		// Independent analytic slab ray intersection + dense source surface quadrature.
		Rig finite;
		finite.scene.Solid(Box(Vec3(-.01f, -4, 3), Vec3(.01f, 4, 8)));
		finite.Refresh();
		auto area = Emitter(30, EmitterKind::Rectangle, Vec3(2, 0, 5));
		area.geometry.halfWidthWorld = 1;
		area.geometry.halfHeightWorld = 1;
		finite.asset.emitters = {area};
		auto ar = finite.Request();
		ar.options.sampling.areaDivisions = 16;
		S::SourceSupport freeArea;
		freeArea.source = 30;
		freeArea.geometry.kind = Rend2LightRecon::Core::SourceSupport::Kind::Rectangle;
		freeArea.geometry.map = finite.geometry.map;
		freeArea.geometry.geometry = finite.geometry.geometry;
		freeArea.geometry.provenance = "separated physical panel, no world-solid penetration";
		ar.support = {freeArea};
		ar.items = {SourceItem(30, 1, Vec3(-2, 0, 1))};
		auto areaCache = Build(ar);
		auto av = Sample(areaCache, ar.items[0]);
		double areaReference = DenseArea(area, Vec3(-2, 0, 1), 512);
		double areaError = std::abs(av.staticResponse.value - areaReference);
		CHECK(av.staticResponse.estimateKnown && av.staticResponse.value > 0 &&
			  av.staticResponse.value < 1);
		CHECK(areaError < .035);
		CHECK(av.issues & S::FiniteSampling);
		auto tube = Emitter(31, EmitterKind::Tube, Vec3(3.5f, 0, 5));
		tube.geometry.halfLengthWorld = 3;
		finite.asset.emitters = {tube};
		auto tr = finite.Request();
		tr.options.sampling.tubeSides = 32;
		tr.options.sampling.tubeSegments = 16;
		S::SourceSupport freeTube = freeArea;
		freeTube.source = 31;
		freeTube.geometry.kind = Rend2LightRecon::Core::SourceSupport::Kind::Cylinder;
		freeTube.geometry.provenance = "separated finite tube, no world-solid penetration";
		tr.support = {freeTube};
		tr.items = {SourceItem(31, 1, Vec3(-2, 0, 1))};
		auto tubes = Build(tr);
		auto tv = Sample(tubes, tr.items[0]);
		double tubeReference = DenseArea(tube, Vec3(-2, 0, 1), 512, true);
		double tubeError = std::abs(tv.staticResponse.value - tubeReference);
		double areaReferenceDifference =
			std::abs(areaReference - DenseArea(area, Vec3(-2, 0, 1), 1024));
		double tubeReferenceDifference =
			std::abs(tubeReference - DenseArea(tube, Vec3(-2, 0, 1), 1024, true));
		CHECK(areaReferenceDifference < .003 && tubeReferenceDifference < .003);
		CHECK(tv.staticResponse.estimateKnown && tubeError < .04);
		auto forcedDisagreement = tr;
		forcedDisagreement.options.maximumSamplingDifference = 0;
		auto flagged = Sample(Build(forcedDisagreement), tr.items[0]);
		CHECK(flagged.samplingDifference > 0 && (flagged.issues & S::SamplingDisagreement));
		auto maximumRule = tr;
		maximumRule.options.sampling.tubeSides = 64;
		maximumRule.options.sampling.tubeSegments = 32;
		auto resolution = Sample(Build(maximumRule), maximumRule.items[0]);
		CHECK(resolution.referenceSamples == 0 && (resolution.issues & S::ResolutionLimited));
		Rig sky;
		sky.scene.Quad(Vec3(-8, -4, 8), Vec3(8, -4, 8), Vec3(8, 4, 8), Vec3(-8, 4, 8), MaterialSky);
		sky.Refresh();
		auto sun = Emitter(40, EmitterKind::Directional, Vec3());
		sky.asset.emitters = {sun};
		SunSource sunRecord;
		sunRecord.emitter = 40;
		sky.asset.suns = {sunRecord};
		auto sk = sky.Request();
		auto skyItem = SourceItem(0, 1, Vec3(0, 0, 1));
		skyItem.kind = ShadowQueryKind::SkyDirection;
		skyItem.targetOrDirection = Vec3(0, 0, 1);
		sk.items = {skyItem, SourceItem(40, 2, Vec3(0, 0, 1))};
		auto skies = Build(sk);
		CHECK(Sample(skies, sk.items[0]).staticGeometry.value == 1);
		CHECK(Sample(skies, sk.items[1]).staticGeometry.value == 1);
		CHECK(S::Prepare(skies, ReadContext(), prepared));
		CHECK(manager.Publish(skies.asset, Completeness::Partial, prepared) == Status::Ok);
		SkyVisibilityQuery skyQuery;
		skyQuery.positionWorld = Vec3(0, 0, 1);
		skyQuery.directionToSky = Vec3(0, 0, 1);
		CHECK(manager.Acquire().QuerySkyVisibility(skyQuery));
		skyQuery.source = manager.Acquire().IdentifySource(40).value;
		CHECK(manager.Acquire().QuerySkyVisibility(skyQuery));
		auto missingSky = open.Request();
		missingSky.items = {skyItem};
		auto noSky = Sample(Build(missingSky), skyItem);
		CHECK(!noSky.staticGeometry.estimateKnown && noSky.staticGeometry.upper == 1);
		// Unknown alpha/translucent optics never silently clear or opaque.
		uint64_t opticalUnknown = 0;
		for (auto material : {MaterialCutout, MaterialTranslucent})
		{
			Rig alpha;
			alpha.scene.Quad(Vec3(0, -4, 0), Vec3(0, 4, 0), Vec3(0, 4, 8), Vec3(0, -4, 8),
							 material);
			alpha.Refresh();
			auto r = alpha.Request();
			r.items = {Segment(1, Vec3(-2, 0, 1), Vec3(2, 0, 1))};
			auto unknown = Sample(Build(r), r.items[0]);
			CHECK(!unknown.staticGeometry.estimateKnown && unknown.staticGeometry.lower == 0 &&
				  unknown.staticGeometry.upper == 1);
			opticalUnknown += !unknown.staticGeometry.estimateKnown;
		}
		Rig uncertainGeometry;
		BakedSurface unsupported;
		unsupported.kind = BakedSurfaceKind::Unsupported;
		unsupported.motion = SurfaceMotion::StaticWorld;
		uncertainGeometry.scene.surfaces.push_back(unsupported);
		uncertainGeometry.Refresh();
		auto ur = uncertainGeometry.Request();
		ur.items = {Segment(1, Vec3(-2, 0, 1), Vec3(2, 0, 1))};
		auto missingGeometry = Sample(Build(ur), ur.items[0]);
		CHECK(!missingGeometry.staticGeometry.estimateKnown &&
			  missingGeometry.staticGeometry.upper == 1);
		Rig patch;
		BakedSurface curved;
		curved.kind = BakedSurfaceKind::Patch;
		curved.motion = SurfaceMotion::StaticWorld;
		curved.patchWidth = curved.patchHeight = 3;
		curved.materialFacts.available = true;
		curved.materialFacts.cull = SurfaceCull::None;
		for (unsigned y = 0; y < 3; ++y)
			for (unsigned z = 0; z < 3; ++z)
			{
				BakedVertex v;
				v.position = Vec3(y == 1 && z == 1 ? .3f : 0, -2.f + y * 2, 1.f + z * 2);
				curved.vertices.push_back(v);
			}
		patch.scene.surfaces.push_back(curved);
		patch.Refresh();
		auto pr = patch.Request();
		pr.items = {Segment(1, Vec3(-2, 0, 3), Vec3(2, 0, 3))};
		auto patchCache = Build(pr);
		CHECK(!Sample(patchCache, pr.items[0]).staticGeometry.estimateKnown);
		// A distant unbounded mover does not erase static evidence. Bake and runtime remain
		// distinct.
		auto dynamic = request;
		DynamicBoundary mover;
		mover.id = 100;
		mover.model = 1;
		mover.bounds = Box(Vec3(20, 20, 20), Vec3(21, 21, 21));
		dynamic.scene.dynamic = {mover};
		auto runtime = Build(dynamic);
		auto unknownMotion = Sample(runtime, dynamic.items[0]);
		CHECK(unknownMotion.staticGeometry.estimateKnown &&
			  unknownMotion.staticGeometry.value == 1);
		CHECK(!unknownMotion.scenarioGeometry.estimateKnown && unknownMotion.runtimeUpdateRequired);
		Rig movingVisual;
		movingVisual.scene.Quad(Vec3(0, -4, 0), Vec3(0, 4, 0), Vec3(0, 4, 8), Vec3(0, -4, 8));
		movingVisual.scene.surfaces[0].motion = SurfaceMotion::BrushModel;
		movingVisual.scene.surfaces[0].model = 1;
		movingVisual.Refresh();
		auto moving = movingVisual.Request();
		moving.items = {Segment(1, Vec3(-2, 0, 1), Vec3(2, 0, 1))};
		auto localMover = mover;
		localMover.bounds = Box(Vec3(-.1f, -4, 0), Vec3(.1f, 4, 8));
		moving.scene.dynamic = {localMover};
		auto movingCache = Build(moving);
		CHECK(movingVisual.geometry.visibility->Report().excludedMoving == 1);
		auto movingValue = Sample(movingCache, moving.items[0]);
		CHECK(movingValue.staticGeometry.estimateKnown && movingValue.staticGeometry.value == 1 &&
			  !movingValue.scenarioGeometry.estimateKnown);
		dynamic.scene.occupancyPolicy = Placement::OccupancyPolicy::ConditionalBake;
		dynamic.scene.occupancyProvenance =
			"explicit scoped bake hypothesis; distant original model bounds are not its trajectory";
		dynamic.scenario = 200;
		auto conditional = Build(dynamic);
		auto cv = Sample(conditional, dynamic.items[0]);
		CHECK(cv.staticGeometry.estimateKnown && cv.scenarioGeometry.estimateKnown &&
			  cv.scenarioGeometry.value == 1 && cv.conditional && cv.runtimeUpdateRequired);
		auto bounded = dynamic;
		bounded.scene.occupancyPolicy = Placement::OccupancyPolicy::RuntimeConservative;
		bounded.scene.dynamic[0].sweptExtentKnown = true;
		CHECK(!Sample(Build(bounded), bounded.items[0]).runtimeUpdateRequired);
		// Same explicit model, independent open/closed bake scenarios, actual model BVH.
		Placement::OccupancyEvidence evidence;
		evidence.id = 100;
		evidence.model = 1;
		evidence.mapKey = open.geometry.map;
		evidence.geometryGeneration = open.geometry.geometry;
		evidence.provenance = "controlled moving door";
		Placement::BakeState opened;
		opened.id = 1;
		opened.generation[0] = 1;
		opened.participation = Placement::BakeParticipation::Excluded;
		opened.verified = true;
		opened.provenance = "known open bake state";
		Placement::BakeState closed = opened;
		closed.id = 2;
		closed.participation = Placement::BakeParticipation::Included;
		closed.generation[0] = 2;
		closed.provenance = "known closed bake state";
		Rig doorModel;
		doorModel.scene.Solid(Box(Vec3(-.01f, -4, 0), Vec3(.01f, 4, 8)));
		doorModel.Refresh();
		closed.geometry = doorModel.geometry.visibility;
		evidence.states = {opened, closed};
		evidence.selectedState = 1;
		auto states = request;
		states.scenario = 300;
		states.scene.occupancy = {evidence};
		auto openState = Build(states);
		CHECK(Sample(openState, states.items[0]).scenarioGeometry.value == 1);
		states.scene.occupancy[0].selectedState = 2;
		states.scenario = 301;
		auto closedState = Build(states);
		auto closedValue = Sample(closedState, states.items[0]);
		CHECK(closedValue.staticGeometry.value == 1 && closedValue.scenarioGeometry.estimateKnown &&
			  closedValue.scenarioGeometry.value == 0);
		// Source mounting side and independent opaque blocker.
		Rig mounted;
		mounted.scene.Quad(Vec3(-4, -4, 4), Vec3(4, -4, 4), Vec3(4, 4, 4), Vec3(-4, 4, 4));
		mounted.Refresh();
		auto panel = Emitter(50, EmitterKind::Rectangle, Vec3(0, 0, 4));
		mounted.asset.emitters = {panel};
		auto mr = mounted.Request();
		S::SourceSupport support;
		support.source = 50;
		support.geometry.kind = Rend2LightRecon::Core::SourceSupport::Kind::Rectangle;
		support.geometry.map = mounted.geometry.map;
		support.geometry.geometry = mounted.geometry.geometry;
		support.geometry.surfaces = {1};
		support.geometry.mountKnown = true;
		support.geometry.anchorWorld = Vec3(0, 0, 4);
		support.geometry.outward = Vec3(0, 0, -1);
		support.geometry.fixture = 77;
		support.geometry.provenance = "known outward face at original ceiling";
		mr.support = {support};
		mr.items = {SourceItem(50, 1, Vec3(0, 0, 1)), SourceItem(50, 2, Vec3(0, 0, 6))};
		auto mount = Build(mr);
		CHECK(Sample(mount, mr.items[0]).staticGeometry.value == 1);
		CHECK(!Sample(mount, mr.items[1]).responseApplicable);
		mounted.scene.Solid(Box(Vec3(-4, -4, 2), Vec3(4, 4, 2.1f)));
		mounted.Refresh();
		auto obstructed = mounted.Request();
		obstructed.items = {mr.items[0]};
		support.geometry.geometry = mounted.geometry.geometry;
		obstructed.support = {support};
		auto behind = Sample(Build(obstructed), obstructed.items[0]);
		CHECK(behind.staticGeometry.estimateKnown && behind.staticGeometry.value == 0);
		// Wall-side receiver offset retains original coordinates; no blanket surface exclusion.
		auto boundary = wall.Request();
		auto boundaryItem = SourceItem(10, 900, Vec3(-.01f, 0, 1));
		boundaryItem.receiver.surface = 1;
		boundaryItem.receiver.normalKnown = true;
		boundaryItem.receiver.geometricNormal = Vec3(-1, 0, 0);
		boundary.items = {boundaryItem};
		auto boundaryCache = Build(boundary);
		CHECK(boundaryCache.records[0].item.receiver.positionWorld.x ==
			  boundaryItem.receiver.positionWorld.x);
		CHECK(!boundaryCache.records[0].result.staticGeometry.estimateKnown ||
			  boundaryCache.records[0].result.staticGeometry.value == 0);
		// Sparse codec and immutable generation controls.
		S::Artifact decoded;
		CHECK(S::Decode(areaCache.asset, areaCache.chunks, 900, finite.geometry.map,
						finite.geometry.geometry, ReadContext(), decoded));
		CHECK(std::abs(Sample(decoded, ar.items[0]).staticResponse.value -
					   av.staticResponse.value) < 1e-12);
		auto compact = ar;
		compact.options.storage = S::Storage::UNorm16;
		auto compressed = Build(compact);
		auto compactValue = Sample(compressed, ar.items[0]);
		CHECK(std::abs(compactValue.staticResponse.value - av.staticResponse.value) <= 1. / 65535);
		CHECK(compactValue.staticResponse.lower <= av.staticResponse.value &&
			  compactValue.staticResponse.upper >= av.staticResponse.value);
		CHECK(S::Decode(compressed.asset, compressed.chunks, 900, finite.geometry.map,
						finite.geometry.geometry, ReadContext(), decoded));
		auto compactRepeat = compressed;
		CHECK(S::Encode(compactRepeat, ReadContext()));
		CHECK(compactRepeat.chunks.size() == compressed.chunks.size());
		for (size_t j = 0; j < compressed.chunks.size(); ++j)
			CHECK(compactRepeat.chunks[j].bytes == compressed.chunks[j].bytes);
		Disk::Fingerprint fp;
		fp.algorithm = "org.openjk.shadow-cache-test";
		fp.algorithmVersion = 1;
		fp.units = areaCache.asset.units;
		fp.complete.fill(true);
		for (unsigned j = 0; j < unsigned(Disk::InputKind::Count); ++j)
		{
			Disk::InputContent content;
			content.kind = Disk::InputKind(j);
			content.name = "owned/shadow/" + std::to_string(j);
			content.bytes = content.name.size();
			content.sha256 = Disk::Hash(content.name.data(), content.name.size());
			fp.inputs.push_back(content);
		}
		std::vector<uint8_t> archive;
		CHECK(Disk::Encode(areaCache.asset, fp, areaCache.chunks, archive));
		Disk::MemoryReader reader(archive.data(), archive.size());
		ReconstructedLightingAsset restored;
		Disk::Index index;
		CHECK(Disk::Open(reader, fp, restored, index));
		std::vector<Disk::Chunk> loaded;
		for (const auto &section : index.sections)
			if (section.type == unsigned(Disk::Section::Chunk))
			{
				Disk::Chunk chunk;
				chunk.dataset = section.dataset;
				chunk.firstElement = section.firstElement;
				chunk.count = section.count;
				CHECK(
					Disk::ReadChunk(reader, index, chunk.dataset, chunk.firstElement, chunk.bytes));
				loaded.push_back(std::move(chunk));
			}
		CHECK(S::Decode(restored, loaded, 900, finite.geometry.map, finite.geometry.geometry,
						ReadContext(), decoded));
		if (argc > 2)
		{
			std::ofstream file(argv[2], std::ios::binary);
			file.write(reinterpret_cast<const char *>(archive.data()), archive.size());
			CHECK(bool(file));
		}
		auto corrupt = areaCache.chunks;
		corrupt.back().bytes[24] ^= 1;
		CHECK(!S::Decode(areaCache.asset, corrupt, 900, finite.geometry.map,
						 finite.geometry.geometry, ReadContext(), decoded));
		auto missing = areaCache.chunks;
		missing.pop_back();
		CHECK(!S::Decode(areaCache.asset, missing, 900, finite.geometry.map,
						 finite.geometry.geometry, ReadContext(), decoded));
		auto wrongMap = finite.geometry.map;
		wrongMap[0] ^= 1;
		CHECK(!S::Decode(areaCache.asset, areaCache.chunks, 900, wrongMap, finite.geometry.geometry,
						 ReadContext(), decoded));
		auto stale = cache;
		stale.asset.emitters[0].position.world.x += .1f;
		CHECK(S::Prepare(stale, ReadContext(), prepared).code == Code::InvalidState);
		auto warm = request;
		warm.previous = std::make_shared<const S::Artifact>(cache);
		auto reused = Build(warm);
		CHECK(reused.work.reused == 3 && reused.work.rays == 0);
		auto changedAsset = std::make_shared<ReconstructedLightingAsset>(*request.asset);
		changedAsset->emitters[0].emission.intensity.value *= 2;
		warm.asset = changedAsset;
		auto brightness = Build(warm);
		CHECK(brightness.work.reused == 3 && brightness.work.rays == 0);
		changedAsset->emitters[0].position.world.x += .1f;
		auto moved = Build(warm);
		CHECK(moved.work.reused == 2 && moved.work.built == 1 && moved.work.rays == 1);
		auto limited = request;
		limited.options.maxRays = 1;
		S::Artifact failed;
		CHECK(S::Build(limited, ReadContext(), failed).code == Code::WorkLimit &&
			  failed.records.empty());
		limited = request;
		limited.options.maxBytes = 64;
		CHECK(!S::Build(limited, ReadContext(), failed));
		Cancel cancel;
		ReadContext cancelled;
		cancelled.cancellation = &cancel;
		CHECK(S::Build(request, cancelled, failed).code == Code::Cancelled);
		CancelDuring during;
		ReadContext interrupt;
		interrupt.cancellation = &during;
		CHECK(S::Build(wr, interrupt, failed).code == Code::Cancelled && failed.records.empty());
		auto unsupportedSource = sk;
		auto unsupportedAsset = std::make_shared<ReconstructedLightingAsset>(*sk.asset);
		unsupportedAsset->emitters[0].geometry.angularRadiusRadians.known = true;
		unsupportedAsset->emitters[0].geometry.angularRadiusRadians.value = .01f;
		unsupportedSource.asset = unsupportedAsset;
		unsupportedSource.items = {sk.items[1]};
		auto unsupportedCache = Build(unsupportedSource);
		CHECK(!unsupportedCache.records[0].result.staticGeometry.estimateKnown &&
			  (unsupportedCache.records[0].result.issues & S::UnsupportedSource));
		auto duplicate = request;
		duplicate.items.push_back(duplicate.items[0]);
		CHECK(S::Build(duplicate, ReadContext(), failed).code == Code::InvalidInput);
		auto dangling = request;
		dangling.provenance = 999;
		CHECK(S::Build(dangling, ReadContext(), failed).code == Code::InvalidInput);
		auto corruptSupport = request;
		S::SourceSupport malformed;
		malformed.source = 10;
		malformed.geometry.version = 999;
		corruptSupport.support = {malformed};
		CHECK(S::Build(corruptSupport, ReadContext(), failed).code == Code::InvalidInput);
		auto unknownSource = request;
		auto unknownAsset = std::make_shared<ReconstructedLightingAsset>(*request.asset);
		unknownAsset->emitters[0].source.state = HypothesisState::Candidate;
		unknownAsset->emitters[0].position.known = false;
		unknownSource.asset = unknownAsset;
		auto unknownSourceCache = Build(unknownSource);
		CHECK(unknownSourceCache.work.unsupported == 1 &&
			  unknownSourceCache.work.staticKnown == 2 && unknownSourceCache.records.size() == 3);
		auto ambiguous = request;
		auto duplicateCoordinates = ambiguous.items.back();
		duplicateCoordinates.receiver.id = 1000;
		ambiguous.items.push_back(duplicateCoordinates);
		auto ambiguousCache = Build(ambiguous);
		CHECK(S::Prepare(ambiguousCache, ReadContext(), prepared));
		VisibilitySample ambiguousValue;
		segment.receiverWorld = Vec3(-2, 0, 1);
		segment.targetWorld = Vec3(2, 0, 1);
		CHECK(prepared->Visibility(ambiguousCache.asset, segment, ambiguousValue) ==
			  Status::Ambiguous);
		auto badReceiver = request;
		badReceiver.items[1].receiver.id = badReceiver.items[0].receiver.id;
		CHECK(S::Build(badReceiver, ReadContext(), failed).code == Code::InvalidInput);
		uint64_t scaleRows[3] = {}, scaleRays[3] = {}, scaleBytes[3] = {};
		double scaleSeconds[3] = {};
		unsigned scaleIndex = 0;
		for (unsigned count : {1u, 8u, 32u})
		{
			auto scale = open.Request();
			auto sources = std::make_shared<ReconstructedLightingAsset>(Asset());
			for (unsigned source = 0; source < count; ++source)
			{
				sources->emitters.push_back(Emitter(100 + source, EmitterKind::Point,
													Vec3(-5 + float(source % 16) * .6f, 0, 6)));
				for (unsigned receiver = 0; receiver < 32; ++receiver)
					scale.items.push_back(SourceItem(
						100 + source, receiver + 1,
						Vec3(-6 + float(receiver % 16) * .75f, -1 + float(receiver / 16) * 2, 1)));
			}
			scale.asset = sources;
			auto scaled = Build(scale);
			CHECK(scaled.records.size() == count * 32 && scaled.work.staticUnknown == 0);
			scaleRows[scaleIndex] = scaled.records.size();
			scaleRays[scaleIndex] = scaled.work.rays;
			scaleBytes[scaleIndex] = scaled.work.bytes;
			scaleSeconds[scaleIndex++] = scaled.work.seconds;
		}
		// Repeated prepared queries allocate nothing and trace no rays.
		CHECK(S::Prepare(cache, ReadContext(), prepared));
		ShadowSupportQuery query;
		q = ShadowSupportQuery();
		query.dataset = 900;
		query.source.id = 10;
		query.receiver = 1;
		query.receiverWorld = Vec3(-2, 0, 1);
		ShadowSupportSample output;
		allocations = 0;
		countAllocations = true;
		auto begin = std::chrono::steady_clock::now();
		unsigned successes = 0;
		for (unsigned j = 0; j < 100000; ++j)
			successes += prepared->ShadowSupport(cache.asset, query, output) == Status::Ok;
		double queryNs =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() * 1e4;
		countAllocations = false;
		CHECK(allocations == 0 && successes == 100000);
		RayQuery ray;
		ray.startWorld = Vec3(-2, 0, 1);
		ray.endWorld = Vec3(2, 0, 3);
		ray.policy.facing = FacingPolicy::TwoSided;
		RayResult hit;
		ReadContext rayRead;
		rayRead.maxBytes = 128ull << 20;
		begin = std::chrono::steady_clock::now();
		for (unsigned j = 0; j < 100000; ++j)
			Check(open.geometry.visibility->Query(ray, rayRead, hit));
		double directNs =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() * 1e4;
		uint64_t diskBytes = 0, compactBytes = 0;
		for (auto &c : areaCache.chunks)
			diskBytes += c.bytes.size();
		for (auto &c : compressed.chunks)
			compactBytes += c.bytes.size();
		CHECK(compactBytes < diskBytes);
		if (argc > 1)
		{
			std::ofstream f(argv[1]);
			f << "{\"schema\":\"org.openjk.shadow-cache-test.v1\",\"checks\":" << checks
			  << ",\"failures\":" << failures
			  << ",\"opaque_tests\":64,\"false_clear\":" << falseClear
			  << ",\"door_positive\":1,\"door_blocked\":1,\"optical_unknown\":" << opticalUnknown
			  << ",\"area_reference\":" << areaReference
			  << ",\"area_fraction\":" << av.staticResponse.value << ",\"area_error\":" << areaError
			  << ",\"area_sampling_difference\":" << av.samplingDifference
			  << ",\"tube_sampling_difference\":" << tv.samplingDifference
			  << ",\"tube_reference\":" << tubeReference
			  << ",\"area_reference_difference\":" << areaReferenceDifference
			  << ",\"tube_reference_difference\":" << tubeReferenceDifference
			  << ",\"tube_fraction\":" << tv.staticResponse.value << ",\"tube_error\":" << tubeError
			  << ",\"area_disk_bytes\":" << diskBytes << ",\"compact_disk_bytes\":" << compactBytes
			  << ",\"cache_query_ns\":" << queryNs << ",\"raw_trace_ns\":" << directNs
			  << ",\"query_allocations\":" << allocations
			  << ",\"reused_rows\":" << reused.work.reused
			  << ",\"moved_rebuilt_rows\":" << moved.work.built
			  << ",\"moved_reused_rows\":" << moved.work.reused
			  << ",\"area_rays\":" << areaCache.work.rays << ",\"tube_rays\":" << tubes.work.rays
			  << ",\"area_bytes\":" << areaCache.work.bytes
			  << ",\"area_build_seconds\":" << areaCache.work.seconds
			  << ",\"probe_rows\":" << probeCache.records.size()
			  << ",\"probe_visible\":" << probeVisible << ",\"probe_blocked\":" << probeBlocked
			  << ",\"archive_sha256\":\"" << Disk::Hex(Disk::Hash(archive.data(), archive.size()))
			  << "\",\"cache_key\":\"" << Disk::Hex(areaCache.descriptor.key) << "\",\"scaling\":[";
			for (unsigned j = 0; j < 3; ++j)
			{
				if (j)
					f << ',';
				f << "{\"rows\":" << scaleRows[j] << ",\"rays\":" << scaleRays[j]
				  << ",\"bytes\":" << scaleBytes[j] << ",\"seconds\":" << scaleSeconds[j] << '}';
			}
			f << "]}";
		}
		std::cout << checks << " checks, " << failures << " failures\n";
		return failures ? 1 : 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 2;
	}
}
