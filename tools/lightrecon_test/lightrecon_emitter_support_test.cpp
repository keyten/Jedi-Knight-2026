// X14 owned brush/surface geometry. No commercial geometry or renderer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_test_scene.h"
#include "lightrecon/producer.h"
#include <cstdio>
#include <fstream>
using namespace JointTest;
namespace SS = Rend2LightRecon::Core::SourceSupport;
namespace P = Rend2LightRecon::Core::PointRefine;
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
struct World
{
	Scene visual;
	BspSpaceInput input;
	P::SceneInput scene;
	std::shared_ptr<const P::PlacementProvider> placement;
	std::shared_ptr<const SurfaceGeometry> geometry;
	World()
	{
		visual.surfaces.clear();
		input.worldBounds = Box(V(-20, -20, -8), V(20, 20, 16));
		SpatialLeaf leaf;
		leaf.cluster = 0;
		input.leaves = {leaf};
	}
	void Brush(V lo, V hi)
	{
		SpatialBrush b;
		b.world = b.solid = true;
		b.firstSide = uint32_t(input.sides.size());
		b.sideCount = 6;
		for (unsigned k = 0; k < 3; ++k)
			for (int sign : {-1, 1})
			{
				SpatialPlane p;
				V n;
				n[k] = sign;
				p.normal = n.Float();
				p.distance = sign < 0 ? -lo[k] : hi[k];
				input.sides.push_back(uint32_t(input.planes.size()));
				input.planes.push_back(p);
			}
		input.leafBrushes.push_back(uint32_t(input.brushes.size()));
		input.brushes.push_back(b);
		input.leaves[0].brushCount = uint32_t(input.brushes.size());
	}
	void Floor(double z = 0)
	{
		visual.Quad(V(-8, -8, z), V(8, -8, z), V(8, 8, z), V(-8, 8, z));
		Brush(V(-16, -16, -4), V(16, 16, z));
	}
	void Build()
	{
		GeometryKey key;
		for (const auto &s : visual.surfaces)
		{
			key.U(s.materialFacts.signals);
			for (const auto &v : s.vertices)
				key.V(v.position);
		}
		for (const auto &p : input.planes)
		{
			key.V(p.normal);
			key.F(p.distance);
		}
		visual.map.path = "maps/x14-owned.bsp";
		visual.map.sha256 = key.Finish();
		input.identity = visual.map;
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Must(capture.SetMap(visual.map));
		for (auto s : visual.surfaces)
			Must(capture.Add(s));
		Must(capture.Freeze());
		geometry = capture.Acquire();
		std::shared_ptr<const BspRegionDomain> domain;
		Must(BspRegionDomain::Create(input, Read(), domain));
		scene.map = visual.map;
		scene.domain = domain;
		scene.domainKey = Key("X14 owned brush classifier");
		Must(StaticVisibility::Build(geometry, {}, Read(), visual.visibility, domain));
		scene.geometry = visual.visibility;
		scene.originalSurfaces = geometry;
		Must(P::MakePlacement(scene, Read(), placement));
	}
	SS::Contract Contract(SS::Kind kind = SS::Kind::Rectangle)
	{
		SS::Contract c;
		c.kind = kind;
		c.fixture = 71;
		c.map = scene.map.sha256;
		c.geometry = scene.geometry->CertificationKey();
		c.surfaces = {1};
		c.anchorWorld = visual.surfaces[0].vertices[0].position;
		c.outward = Vec3(0, 0, 1);
		c.mountKnown = true;
		c.provenance =
			"Owned original fixture and declared outward emission side; no bake amplitude assumed";
		return c;
	}
	SS::Result At(const Shape::Emitter &e)
	{
		SS::Result out;
		Must(SS::Check(scene, *placement, e.geometry, e.geometry, e.support, Read(), out));
		return out;
	}
};
Shape::Emitter Emitter(EmitterKind kind, V pos = V())
{
	auto e = Extended(71, kind, pos).shape.initial;
	e.geometry.frame.known = true;
	e.geometry.frame.right = Vec3(1, 0, 0);
	e.geometry.frame.up = Vec3(0, 1, 0);
	e.geometry.frame.forward = Vec3(0, 0, 1);
	e.rgbAmplitude = Vec3(2, 3, 4);
	e.geometry.geometry.halfWidthWorld = 1;
	e.geometry.geometry.halfHeightWorld = .5;
	e.geometry.geometry.halfLengthWorld = 1;
	e.geometry.geometry.radiusWorld = .25;
	if (kind == EmitterKind::Point)
		e.geometry.angular.kind = AngularKind::Isotropic;
	return e;
}
struct Cancel : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
void FixedPointPlaneControls()
{
	const auto modern = SS::Admission::ModernFixedPoint;
	auto addPanel = [](World &world, V offset = V()) {
		world.visual.Quad(offset + V(-6, -6, 1), offset + V(6, -6, -1),
						  offset + V(6, 6, -1), offset + V(-6, 6, 1));
	};
	auto fixed = [](World &world, V position, uint32_t ordinal = 0) {
		auto e = Emitter(EmitterKind::Point, position);
		Must(SS::FromMountedPointSurface(world.geometry, *world.scene.geometry, ordinal,
										e.geometry.source.id, e.geometry.position.world, 8, Read(), e.support));
		return e;
	};
	auto check = [modern](World &world, const Shape::Emitter &e, SS::Result &result) {
		return SS::Check(world.scene, *world.placement, e.geometry, e.geometry, e.support,
						 Read(), result, modern);
	};
	SS::Result result;
	World oblique;
	addPanel(oblique);
	oblique.Build();
	auto e = fixed(oblique, V(0, 0, 4));
	CHECK(e.support.mountedPointPlane != nullptr);
	Must(check(oblique, e, result));
	CHECK(P::Admissible(result.placement) && result.sourceSideCertified && result.contact);
	CHECK(result.contactErrorWorld == 0 && result.rays == 0);
	CHECK(result.key == SS::Key(e.support));

	SS::Contract existing;
	Must(SS::FromSurfaces(oblique.geometry, *oblique.scene.geometry, {0}, 71, Read(), existing));
	existing.kind = SS::Kind::MountedPoint;
	existing.maximumStandOffWorld = 8;
	Must(SS::Check(oblique.scene, *oblique.placement, e.geometry, e.geometry, existing, Read(), result));
	CHECK(result.placement.state == P::Constraint::UnknownGeometry && !result.sourceSideCertified);
	CHECK(SS::Key(e.support) != SS::Key(existing));
	CHECK(SS::Check(oblique.scene, *oblique.placement, e.geometry, e.geometry, e.support,
					Read(), result).code == Code::Unavailable);

	// Closed triangle coverage uses the exact orthogonal projection. This
	// point projects to the shared diagonal, and the outside point misses.
	auto diagonal = fixed(oblique, V(.5, 0, 3));
	Must(check(oblique, diagonal, result));
	CHECK(result.sourceSideCertified);
	CHECK(SS::Key(e.support) != SS::Key(diagonal.support));
	for (V position : {V(0, 0, -4), V(0, 0, 0), V(0, 0, 9), V(0, 7, 4)})
	{
		Must(check(oblique, fixed(oblique, position), result));
		CHECK(result.placement.state == P::Constraint::Mount && !result.sourceSideCertified);
	}

	for (unsigned axis = 0; axis < 3; ++axis)
	{
		for (float sign : {-1.f, 1.f})
		{
			auto changed = e;
			float *components[] = {&changed.support.anchorWorld.x, &changed.support.anchorWorld.y,
								   &changed.support.anchorWorld.z};
			*components[axis] = std::nextafter(*components[axis], sign * std::numeric_limits<float>::infinity());
			CHECK(SS::Key(changed.support) != SS::Key(e.support));
			CHECK(check(oblique, changed, result).code == Code::InvalidInput);
		}
	}
	auto changed = e;
	changed.support.surfaces = {2};
	CHECK(check(oblique, changed, result).code == Code::InvalidInput);
	changed.support.surfaces.clear();
	CHECK(check(oblique, changed, result).code == Code::InvalidInput);
	changed = e;
	changed.support.geometry = Key("old point geometry");
	CHECK(check(oblique, changed, result).code == Code::InvalidState);
	changed = e;
	changed.geometry.position.world.x = .25f;
	CHECK(check(oblique, changed, result).code == Code::InvalidInput);
	changed = e;
	changed.support.frontClearanceWorld = 1;
	CHECK(check(oblique, changed, result).code == Code::InvalidInput);
	changed = e;
	changed.geometry.angular.twoSided = true;
	CHECK(check(oblique, changed, result).code == Code::InvalidInput);
	changed = e;
	changed.support.outward = (V(e.support.outward) * -1).Float();
	CHECK(check(oblique, changed, result).code == Code::InvalidInput);

	World changedSideEvidence;
	addPanel(changedSideEvidence);
	changedSideEvidence.visual.surfaces[0].vertices[0].shadingNormal.y = .125f;
	changedSideEvidence.Build();
	CHECK(changedSideEvidence.scene.geometry->CertificationKey() == oblique.scene.geometry->CertificationKey());
	auto changedEvidence = fixed(changedSideEvidence, V(0, 0, 4));
	CHECK(SS::Key(changedEvidence.support) != SS::Key(e.support));
	CHECK(check(changedSideEvidence, e, result).code == Code::InvalidState);

	World warped;
	addPanel(warped);
	warped.visual.surfaces[0].vertices[3].position.z = std::nextafter(1.f, 2.f);
	warped.Build();
	CHECK(SS::FromMountedPointSurface(warped.geometry, *warped.scene.geometry, 0, 71,
									 Vec3(0, 0, 4), 8, Read(), existing).code == Code::Unavailable);
	World warpedBelowRoundoff;
	warpedBelowRoundoff.visual.Quad(V(0, -6, 0), V(12, -6, -2), V(12, 6, -2), V(0, 6, 0));
	warpedBelowRoundoff.visual.surfaces[0].vertices[3].position.z = std::ldexp(1.f, -50);
	warpedBelowRoundoff.Build();
	auto tinyWarp = fixed(warpedBelowRoundoff, V(6, 0, 4));
	CHECK(tinyWarp.support.mountedPointPlane != nullptr);
	Must(check(warpedBelowRoundoff, tinyWarp, result));
	CHECK(result.placement.state == P::Constraint::UnknownGeometry && !result.sourceSideCertified);
	World overlapping;
	addPanel(overlapping);
	overlapping.visual.surfaces[0].indices = {0, 1, 2, 0, 1, 2};
	overlapping.Build();
	Must(check(overlapping, fixed(overlapping, V(0, 0, 4)), result));
	CHECK(result.placement.state == P::Constraint::UnknownGeometry && !result.sourceSideCertified);
	World inSolid;
	addPanel(inSolid);
	inSolid.Brush(V(-1, -1, 3), V(1, 1, 5));
	inSolid.Build();
	Must(check(inSolid, fixed(inSolid, V(0, 0, 4)), result));
	CHECK(result.placement.state == P::Constraint::Solid && !result.sourceSideCertified);

	for (uint32_t flags : {0u, uint32_t(MaterialCutout)})
	{
		World blocker;
		addPanel(blocker);
		blocker.visual.Quad(V(-1, -1, 4), V(1, -1, 4), V(1, 1, 4), V(-1, 1, 4), flags);
		blocker.Build();
		Must(check(blocker, fixed(blocker, V(0, 0, 4)), result));
		CHECK(result.placement.state == (flags ? P::Constraint::UnknownGeometry : P::Constraint::Clearance));
		CHECK(!result.sourceSideCertified);
	}
	World neighboring;
	addPanel(neighboring);
	neighboring.visual.Quad(V(-1, -1, 3), V(1, -1, 3), V(1, 1, 3), V(-1, 1, 3), MaterialCutout);
	neighboring.Build();
	Must(check(neighboring, fixed(neighboring, V(0, 0, 4)), result));
	CHECK(result.sourceSideCertified && (result.visualIssues & OpticalUnknown));
	World unknownMount;
	addPanel(unknownMount);
	unknownMount.visual.surfaces[0].materialFacts.signals = MaterialCutout;
	unknownMount.Build();
	Must(check(unknownMount, fixed(unknownMount, V(0, 0, 4)), result));
	CHECK(result.placement.state == P::Constraint::UnknownGeometry && !result.sourceSideCertified);
	World unknown;
	addPanel(unknown);
	unknown.visual.Quad(V(-1, -1, 3), V(1, -1, 3), V(1, 1, 3), V(-1, 1, 3));
	unknown.visual.surfaces[1].kind = BakedSurfaceKind::Unsupported;
	unknown.Build();
	Must(check(unknown, fixed(unknown, V(0, 0, 4)), result));
	CHECK(result.placement.state == P::Constraint::UnknownGeometry && !result.sourceSideCertified);

	World translated;
	V offset(4096, 4096, 4096);
	addPanel(translated, offset);
	translated.input.worldBounds = Box(offset + V(-20, -20, -8), offset + V(20, 20, 16));
	translated.Build();
	Must(check(translated, fixed(translated, offset + V(0, 0, 4)), result));
	CHECK(result.sourceSideCertified);
	World oversize;
	oversize.visual.Quad(V(-6, -300, 1), V(6, -300, -1), V(6, 300, -1), V(-6, 300, 1));
	oversize.Build();
	Must(check(oversize, fixed(oversize, V(0, 0, 4)), result));
	CHECK(result.placement.state == P::Constraint::UnknownGeometry && !result.sourceSideCertified);
	Must(check(oblique, fixed(oblique, V(std::ldexp(1., -25), 0, 4)), result));
	CHECK(result.placement.state == P::Constraint::UnknownGeometry && !result.sourceSideCertified);

	for (int slope : {0, 1})
	{
		World compatible;
		compatible.visual.Quad(V(-6, -6, 6 * slope), V(6, -6, -6 * slope),
							   V(6, 6, -6 * slope), V(-6, 6, 6 * slope));
		compatible.Build();
		auto point = fixed(compatible, V(0, 0, 4));
		CHECK(!point.support.mountedPointPlane);
		Must(check(compatible, point, result));
		CHECK(result.sourceSideCertified);
	}

	Cancel cancel;
	auto cancelled = Read();
	cancelled.cancellation = &cancel;
	CHECK(SS::Check(oblique.scene, *oblique.placement, e.geometry, e.geometry, e.support,
					cancelled, result, modern).code == Code::Cancelled);
	auto limited = oblique.scene;
	limited.maxWorkPerCheck = 1;
	CHECK(SS::Check(limited, *oblique.placement, e.geometry, e.geometry, e.support,
					Read(), result, modern).code == Code::WorkLimit);
	std::vector<Shape::EmissionNode> nodes;
	CHECK(Shape::EmissionNodes(e.geometry, e.support, {}, Read(), nodes).code == Code::Unavailable);
	Shape::Model historical;
	historical.law = Shape::Law::HistoricalBake;
	historical.units.worldUnitsPerMeter = 1;
	historical.provenance = "Owned historical-mode refusal for modern named point support";
	Bake::EvaluationContext context;
	context.visibility = oblique.scene.geometry;
	context.geometryKey = oblique.scene.geometry->CertificationKey();
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>(1);
	std::shared_ptr<const Shape::Block> block;
	CHECK(Shape::Prepare(historical, {e}, Lease(receivers), context, {}, Read(), block).code == Code::Unavailable);
	CHECK(!block);
}
int main(int argc, char **argv)
{
	uint64_t rays = 0, work = 0;
	double area = 0, positive = 0, blocked = -1, meshDifference = -1, fitError = -1, heldMse = -1;
	try
	{
		FixedPointPlaneControls();
		World w;
		w.Floor();
		w.Build();
		auto e = Emitter(EmitterKind::Rectangle);
		e.support = w.Contract();
		P::PlacementResult old;
		Must(w.placement->Check(e.geometry.position.world, e.geometry.position.world, 1.001, Read(),
								old));
		CHECK(!P::Admissible(old));
		auto flush = w.At(e);
		CHECK(P::Admissible(flush.placement) && flush.contact && flush.sourceSideCertified);
		CHECK(flush.emittingAreaWorldSquared == 2);
		area = flush.emittingAreaWorldSquared;
		work += flush.work;
		World boundary;
		boundary.Floor();
		SpatialPlane partition;
		partition.normal = Vec3(0, 0, -1);
		partition.distance = 0;
		SpatialNode node;
		node.plane = uint32_t(boundary.input.planes.size());
		node.children[0] = -2;
		node.children[1] = -1;
		boundary.input.planes.push_back(partition);
		boundary.input.nodes.push_back(node);
		auto solidLeaf = boundary.input.leaves[0];
		solidLeaf.cluster = -1;
		boundary.input.leaves.push_back(solidLeaf);
		boundary.Build();
		auto boundaryPanel = e;
		boundaryPanel.support = boundary.Contract();
		CHECK(P::Admissible(boundary.At(boundaryPanel).placement));
		auto insidePoint = Emitter(EmitterKind::Point, V(0, 0, -1));
		insidePoint.support = boundary.Contract(SS::Kind::FreePoint);
		insidePoint.support.mountKnown = false;
		insidePoint.support.surfaces.clear();
		CHECK(boundary.At(insidePoint).placement.state == P::Constraint::Solid);
		insidePoint.geometry.position.world.z = 0;
		CHECK(!P::Admissible(boundary.At(insidePoint).placement));
		auto wrong = e;
		wrong.geometry.position.world.z = -.01f;
		CHECK(!P::Admissible(w.At(wrong).placement));
		wrong = e;
		wrong.geometry.frame.up = Vec3(0, -1, 0);
		wrong.geometry.frame.forward = Vec3(0, 0, -1);
		CHECK(w.At(wrong).placement.state == P::Constraint::Mount);
		wrong = e;
		wrong.geometry.angular.twoSided = true;
		CHECK(!P::Admissible(w.At(wrong).placement));
		wrong.support.twoSidedAttested = true;
		wrong.support.kind = SS::Kind::TwoSidedRectangle;
		CHECK(!P::Admissible(w.At(wrong).placement));
		World hanging;
		hanging.visual.surfaces = w.visual.surfaces;
		hanging.Build();
		wrong.support = hanging.Contract(SS::Kind::TwoSidedRectangle);
		wrong.support.twoSidedAttested = true;
		CHECK(P::Admissible(hanging.At(wrong).placement));
		auto point = Emitter(EmitterKind::Point, V(0, 0, .0001));
		point.support = w.Contract(SS::Kind::FreePoint);
		point.support.mountKnown = false;
		point.support.surfaces.clear();
		CHECK(P::Admissible(w.At(point).placement));
		point.geometry.position.world.z = -1;
		CHECK(w.At(point).placement.state == P::Constraint::Solid);
		point.geometry.position.world.z = 0;
		point.support = w.Contract(SS::Kind::MountedPoint);
		CHECK(P::Admissible(w.At(point).placement));
		auto spot = Emitter(EmitterKind::Spot);
		spot.support = w.Contract(SS::Kind::MountedSpot);
		CHECK(P::Admissible(w.At(spot).placement));
		spot.geometry.frame.up = Vec3(0, -1, 0);
		spot.geometry.frame.forward = Vec3(0, 0, -1);
		CHECK(!P::Admissible(w.At(spot).placement));
		auto tube = Emitter(EmitterKind::Tube, V(0, 0, .25));
		tube.support = w.Contract(SS::Kind::Cylinder);
		CHECK(P::Admissible(w.At(tube).placement));
		tube.geometry.position.world.z = .249f;
		CHECK(!P::Admissible(w.At(tube).placement));
		auto assembly = e;
		assembly.support.kind = SS::Kind::UnsupportedAssembly;
		CHECK(!P::Admissible(w.At(assembly).placement));
		auto stale = e;
		stale.support.geometry = Key("changed geometry");
		SS::Result result;
		auto error = SS::Check(w.scene, *w.placement, stale.geometry, stale.geometry, stale.support,
							   Read(), result);
		CHECK(error.code == Code::InvalidState);
		auto r = Read();
		Cancel cancel;
		r.cancellation = &cancel;
		error = SS::Check(w.scene, *w.placement, e.geometry, e.geometry, e.support, r, result);
		CHECK(error.code == Code::Cancelled);
		auto small = w.scene;
		small.maxWorkPerCheck = 1;
		error = SS::Check(small, *w.placement, e.geometry, e.geometry, e.support, Read(), result);
		CHECK(error.code == Code::WorkLimit);
		auto moved = e.geometry;
		moved.position.world.x = .1f;
		Must(SS::Check(w.scene, *w.placement, e.geometry, moved, e.support, Read(), result));
		CHECK(result.placement.state == P::Constraint::UnknownPath);
		auto malformed = e.support;
		malformed.surfaces = {1, 1};
		CHECK(SS::Validate(malformed).code == Code::InvalidInput);
		// Actual recessed cavity, not a world-solid exception or a free-space offset.
		World recess;
		recess.visual.Quad(V(-2, -2, -1), V(2, -2, -1), V(2, 2, -1), V(-2, 2, -1));
		recess.Brush(V(-16, -16, -4), V(16, 16, -1));
		recess.Brush(V(-16, -16, -1), V(-2, 16, 0));
		recess.Brush(V(2, -16, -1), V(16, 16, 0));
		recess.Build();
		auto re = e;
		re.geometry.position.world.z = -1;
		re.support = recess.Contract(SS::Kind::RecessedRectangle);
		re.support.frontClearanceWorld = 1;
		CHECK(recess.At(re).placement.state == P::Constraint::Conditional);
		auto fake = e;
		fake.geometry.position.world.z = -1;
		fake.support = w.Contract(SS::Kind::RecessedRectangle);
		CHECK(!P::Admissible(w.At(fake).placement));
		// Independent visual wall crossing the emitter; named mounting surface never hides it.
		World cross;
		cross.Floor();
		cross.visual.Quad(V(0, -2, -1), V(0, 2, -1), V(0, 2, 2), V(0, -2, 2));
		cross.Build();
		auto ce = e;
		ce.support = cross.Contract();
		CHECK(!P::Admissible(cross.At(ce).placement));
		// ASR18: exact opaque fixture edges ending at the luminous plane from
		// behind are mounting contact, for ideal and finite source supports alike.
		World skirt;
		skirt.Floor();
		skirt.visual.Quad(V(1,-2,-1),V(1,2,-1),V(1,2,0),V(1,-2,0));
		skirt.Build();
		for (auto kind : {EmitterKind::Point, EmitterKind::Spot, EmitterKind::Rectangle, EmitterKind::Tube})
		{
			auto mounted = Emitter(kind, kind == EmitterKind::Tube ? V(0,0,.25) :
				kind == EmitterKind::Rectangle ? V() : V(1,0,0));
			mounted.support = skirt.Contract(kind == EmitterKind::Point ? SS::Kind::MountedPoint :
				kind == EmitterKind::Spot ? SS::Kind::MountedSpot :
				kind == EmitterKind::Tube ? SS::Kind::Cylinder : SS::Kind::Rectangle);
			CHECK(P::Admissible(skirt.At(mounted).placement));
			mounted.support.frontClearanceWorld = 1;
			CHECK(P::Admissible(skirt.At(mounted).placement));
		}
		World skirtCross;
		skirtCross.Floor();
		skirtCross.visual.Quad(V(1,-2,-1),V(1,2,-1),V(1,2,.00001),V(1,-2,.00001));
		skirtCross.Build();
		auto skirtPanel = e; skirtPanel.support = skirtCross.Contract();
		CHECK(skirtCross.At(skirtPanel).placement.state == P::Constraint::Clearance);
		World skirtAlpha;
		skirtAlpha.visual.surfaces = skirt.visual.surfaces;
		skirtAlpha.Brush(V(-16,-16,-4),V(16,16,0));
		skirtAlpha.visual.surfaces[1].materialFacts.signals = MaterialCutout;
		skirtAlpha.Build(); skirtPanel.support = skirtAlpha.Contract();
		CHECK(skirtAlpha.At(skirtPanel).placement.state == P::Constraint::UnknownGeometry);
		World overlay;
		overlay.Floor();
		overlay.visual.Quad(V(-1,-1,0),V(1,-1,0),V(1,1,0),V(-1,1,0));
		overlay.Build(); skirtPanel.support = overlay.Contract();
		CHECK(overlay.At(skirtPanel).placement.state == P::Constraint::Clearance);
		// A tube may be tangent with its declared radius, but a positive body gap
		// requires explicit stand-off evidence. Candidate creation never pushes it.
		auto floatingTube = Emitter(EmitterKind::Tube,V(0,0,.5));
		floatingTube.support = w.Contract(SS::Kind::Cylinder);
		CHECK(w.At(floatingTube).placement.state == P::Constraint::Mount);
		floatingTube.support.maximumStandOffWorld = .25;
		CHECK(P::Admissible(w.At(floatingTube).placement));
		// Front AABB overlap alone is not an intersection with a triangle.
		World frontMiss;
		frontMiss.Floor();
		frontMiss.visual.Quad(V(.9,1.5,.5),V(1.5,.4,.5),V(1.6,.5,.5),V(1,1.6,.5));
		frontMiss.Build(); auto frontPanel=e; frontPanel.support=frontMiss.Contract();
		frontPanel.support.frontClearanceWorld=1;
		CHECK(P::Admissible(frontMiss.At(frontPanel).placement));
		World frontHit;
		frontHit.Floor();
		frontHit.visual.Quad(V(-2,-2,.75),V(2,-2,.75),V(2,2,.75),V(-2,2,.75));
		frontHit.Build();
		for (auto kind : {EmitterKind::Point, EmitterKind::Spot, EmitterKind::Rectangle, EmitterKind::Tube})
		{
			auto mounted=Emitter(kind,kind==EmitterKind::Tube?V(0,0,.25):V());
			mounted.support=frontHit.Contract(kind==EmitterKind::Point?SS::Kind::MountedPoint:
				kind==EmitterKind::Spot?SS::Kind::MountedSpot:kind==EmitterKind::Tube?SS::Kind::Cylinder:SS::Kind::Rectangle);
			CHECK(P::Admissible(frontHit.At(mounted).placement));
			mounted.support.frontClearanceWorld=1;
			CHECK(frontHit.At(mounted).placement.state==P::Constraint::Clearance);
		}
		frontHit.visual.surfaces[1].materialFacts.signals=MaterialTranslucent;
		frontHit.Build(); frontPanel.support=frontHit.Contract();frontPanel.support.frontClearanceWorld=1;
		CHECK(frontHit.At(frontPanel).placement.state==P::Constraint::UnknownGeometry);
		World frontSolid;
		frontSolid.Floor();frontSolid.Brush(V(-2,-2,.75),V(2,2,2));frontSolid.Build();
		frontPanel.support=frontSolid.Contract();frontPanel.support.frontClearanceWorld=1;
		CHECK(!P::Admissible(frontSolid.At(frontPanel).placement));
		// Separate occupancy is checked over the front too, even when static
		// geometry is clear. The known swept model does not overlap the sheet.
		auto occupiedFront=w.scene;
		DynamicBoundary frontModel;frontModel.id=818;frontModel.model=1;
		frontModel.bounds=Box(V(-.1,-.1,.8),V(.1,.1,.9));frontModel.sweptExtentKnown=true;
		occupiedFront.dynamic={frontModel};
		std::shared_ptr<const P::PlacementProvider> frontPlacement;
		Must(P::MakePlacement(occupiedFront,Read(),frontPlacement));
		auto occupancyPanel=e;
		Must(SS::Check(occupiedFront,*frontPlacement,occupancyPanel.geometry,occupancyPanel.geometry,
			occupancyPanel.support,Read(),result));
		CHECK(P::Admissible(result.placement));
		occupancyPanel.support.frontClearanceWorld=1;
		Must(SS::Check(occupiedFront,*frontPlacement,occupancyPanel.geometry,occupancyPanel.geometry,
			occupancyPanel.support,Read(),result));
		CHECK(result.placement.state==P::Constraint::Dynamic);
		// A two-sided sheet has no non-emitting rear halfspace. Its explicit
		// clearance must include both sides, even without a world brush behind it.
		World bothSides;
		bothSides.visual.surfaces=skirt.visual.surfaces;bothSides.Build();
		auto twoSided=e;twoSided.support=bothSides.Contract(SS::Kind::TwoSidedRectangle);
		twoSided.support.twoSidedAttested=twoSided.geometry.angular.twoSided=true;
		CHECK(bothSides.At(twoSided).placement.state==P::Constraint::Clearance);
		World backHit;
		backHit.visual.surfaces=hanging.visual.surfaces;
		backHit.visual.Quad(V(-2,-2,-.75),V(2,-2,-.75),V(2,2,-.75),V(-2,2,-.75));
		backHit.Build();twoSided.support=backHit.Contract(SS::Kind::TwoSidedRectangle);
		twoSided.support.twoSidedAttested=true;
		CHECK(P::Admissible(backHit.At(twoSided).placement));
		twoSided.support.frontClearanceWorld=1;
		CHECK(backHit.At(twoSided).placement.state==P::Constraint::Clearance);
		World alpha;
		alpha.Floor();
		alpha.visual.surfaces[0].materialFacts.signals = MaterialCutout;
		alpha.Build();
		auto ae = e;
		ae.support = alpha.Contract();
		CHECK(alpha.At(ae).placement.state == P::Constraint::UnknownGeometry);
		// ASR17: a panel on its own mount can also touch an unnamed brush edge.
		// This is unresolved boundary support, not numerical proof of penetration.
		World edge;
		edge.Floor();
		edge.Brush(V(1, -16, -1), V(4, 16, 4));
		edge.Build();
		auto edgePanel = e; edgePanel.support = edge.Contract();
		CHECK(edge.At(edgePanel).placement.state == P::Constraint::UnknownGeometry);
		World gap;
		gap.Floor(); gap.Brush(V(1.001, -16, -1), V(4, 16, 4)); gap.Build();
		edgePanel.support = gap.Contract();
		CHECK(P::Admissible(gap.At(edgePanel).placement));
		World overlap;
		overlap.Floor(); overlap.Brush(V(.5, -16, -1), V(4, 16, 4)); overlap.Build();
		edgePanel.support = overlap.Contract();
		CHECK(overlap.At(edgePanel).placement.state == P::Constraint::Solid);
		// Opposite emitted side on exactly the same zero-thickness polygon needs
		// separate side evidence: its declared normal cannot match the floor brush.
		auto reversed = e;
		reversed.geometry.frame.up = Vec3(0,-1,0);
		reversed.geometry.frame.forward = reversed.support.outward = Vec3(0,0,-1);
		CHECK(w.At(reversed).placement.state == P::Constraint::UnknownGeometry);
		World sandwich;
		sandwich.Floor(); sandwich.Brush(V(-4,-4,0),V(4,4,4)); sandwich.Build();
		auto shared=e;shared.support=sandwich.Contract();
		CHECK(sandwich.At(shared).placement.state == P::Constraint::UnknownGeometry);
		SpatialLocation outwardPoint;
		Must(sandwich.scene.domain->Locate(Vec3(0,0,.125),outwardPoint,0));
		CHECK(outwardPoint.space == SpaceClass::Solid);
		// RBSP-style raw winding opposes captured normals. The same actual
		// polygon now gets its supported side from captured orientation evidence.
		World winding;
		winding.Floor();
		for(auto &v:winding.visual.surfaces[0].vertices)v.shadingNormal=Vec3(0,0,1);
		for(size_t i=0;i<winding.visual.surfaces[0].indices.size();i+=3)
			std::swap(winding.visual.surfaces[0].indices[i+1],winding.visual.surfaces[0].indices[i+2]);
		winding.Build();auto wound=e;
		Must(SS::FromSurfaces(winding.geometry,*winding.scene.geometry,{0},71,Read(),wound.support));
		CHECK(wound.support.outward.z == 1);
		CHECK(P::Admissible(winding.At(wound).placement));
		// A reversed source interpretation never removes an independent solid.
		auto blockedWound = shared; blockedWound.support.outward = Vec3(0,0,1);
		CHECK(sandwich.At(blockedWound).placement.state == P::Constraint::UnknownGeometry);
		World noSide;
		noSide.Floor();for(auto &v:noSide.visual.surfaces[0].vertices)v.shadingNormal=Vec3();
		noSide.Build();SS::Contract unknownSide;
		CHECK(SS::FromSurfaces(noSide.geometry,*noSide.scene.geometry,{0},71,Read(),unknownSide).code == Code::Unavailable);
		World contradictory;
		contradictory.Floor();contradictory.visual.surfaces[0].vertices[0].shadingNormal=Vec3(0,0,-1);
		contradictory.Build();
		CHECK(SS::FromSurfaces(contradictory.geometry,*contradictory.scene.geometry,{0},71,Read(),unknownSide).code == Code::Unavailable);
		// Emulate pre-fix archived priors, using production consolidation and
		// conversion against their exact original geometry lease.
		auto evidence=std::make_shared<std::vector<EmitterEvidence>>(1);
		auto &priorEvidence=(*evidence)[0];
		priorEvidence.id=991;priorEvidence.source="owned archived winding";
		priorEvidence.origin=EvidenceOrigin::GlowSurface;
		priorEvidence.positionKnown=priorEvidence.orientationKnown=priorEvidence.staticEligible=true;
		priorEvidence.shapes=CandidatePoint|CandidateRectangle|CandidateTube;
		priorEvidence.surfaces={0};priorEvidence.right=Vec3(1,0,0);
		priorEvidence.up=Vec3(0,-1,0);priorEvidence.outward=Vec3(0,0,-1);
		priorEvidence.halfWidth=1;priorEvidence.halfHeight=.5f;priorEvidence.radius=.25f;
		auto candidates=std::make_shared<std::vector<EmitterCandidate>>(1);
		(*candidates)[0].id=991;(*candidates)[0].shapes=priorEvidence.shapes;
		(*candidates)[0].staticEligible=true;(*candidates)[0].evidence={0};
		auto annotations=std::make_shared<std::vector<Candidates::Annotation>>(1);
		(*annotations)[0].candidate=991;
		Candidates::Provider archived;
		archived.name="owned archived";archived.map=winding.scene.map.sha256;
		archived.geometryKey=winding.scene.geometry->CertificationKey();archived.generation=Key("archived side/1");
		archived.evidence=Lease(evidence);archived.candidates=Lease(candidates);archived.annotations=Lease(annotations);
		Candidates::Request pool;pool.map=archived.map;pool.geometryKey=archived.geometryKey;pool.providers={archived};
		Candidates::Catalog catalog;Must(Candidates::Consolidate(pool,Read(),catalog));
		CHECK(catalog.models.size()==3);
		J::Request conversion;conversion.mapKey=archived.map;conversion.geometry.geometryKey=archived.geometryKey;
		conversion.geometry.visibility=winding.scene.geometry;conversion.placement=winding.scene;
		conversion.sourceSearchBounds=winding.input.worldBounds;
		for(const auto &model:catalog.models)
		{
			J::Candidate converted;CHECK(Producer::MakeCandidate(catalog,model,conversion,converted));
			auto &initial=converted.shape.initial;
			CHECK(initial.geometry.position.world.x==0 && initial.geometry.position.world.y==0 && initial.geometry.position.world.z==0);
			CHECK(initial.support.outward.z==1 && initial.support.fixture==model.bundle);
			if(initial.geometry.kind!=EmitterKind::Point)CHECK(initial.geometry.frame.forward.z==1 && initial.geometry.frame.right.x==1);
			SS::Result admission;Must(SS::Check(winding.scene,*winding.placement,initial.geometry,initial.geometry,initial.support,Read(),admission));
			if(initial.geometry.kind==EmitterKind::Tube)
				CHECK(admission.placement.state==P::Constraint::Solid && initial.geometry.geometry.radiusWorld==.25);
			else CHECK(P::Admissible(admission.placement));
		}
		// Visual scope controls: a local approximation is not missing everywhere.
		World curved;
		curved.Floor();
		BakedSurface patch;
		patch.ordinal = 1; patch.kind = BakedSurfaceKind::Patch;
		patch.motion = SurfaceMotion::StaticWorld; patch.patchWidth = patch.patchHeight = 3;
		patch.materialFacts.available = true; patch.materialFacts.cull = SurfaceCull::None;
		for (unsigned y=0;y<3;++y) for (unsigned x=0;x<3;++x)
		{
			BakedVertex v; v.position=Vec3(float(x)-1,float(y)-1,x==1&&y==1?.01f:0);
			patch.vertices.push_back(v);
		}
		curved.visual.surfaces.push_back(patch); curved.Build();
		auto curvedPanel=e;curvedPanel.support=curved.Contract();
		CHECK(curved.At(curvedPanel).reason == "Incomplete local visual support");
		LocalGeometry nearPatch, farPatch;
		Must(curved.scene.geometry->ExtractBox(Box(V(-1,-.5,0),V(1,.5,0)),Read(),4096,1048576,nearPatch));
		Must(curved.scene.geometry->ExtractBox(Box(V(5,5,1),V(6,6,2)),Read(),4096,1048576,farPatch));
		CHECK(!nearPatch.coverageComplete && (nearPatch.issues & PatchApproximation));
		CHECK(farPatch.coverageComplete);
		World translucent;
		translucent.Floor(); translucent.visual.surfaces[0].materialFacts.signals=MaterialTranslucent;
		translucent.Build(); auto transPanel=e;transPanel.support=translucent.Contract();
		CHECK(translucent.At(transPanel).reason == "Mount optics/curved geometry are not exact opaque support");
		// A ceiling point very close to its support is geometrically valid without a vacuum ball.
		World ceiling;
		ceiling.visual.Quad(V(-8, -8, 6), V(-8, 8, 6), V(8, 8, 6), V(8, -8, 6));
		ceiling.Brush(V(-16, -16, 6), V(16, 16, 10));
		ceiling.Build();
		auto cp = Emitter(EmitterKind::Point, V(0, 0, 5.999));
		cp.support = ceiling.Contract(SS::Kind::MountedPoint);
		cp.support.outward = Vec3(0, 0, -1);
		cp.support.maximumStandOffWorld = .01;
		CHECK(P::Admissible(ceiling.At(cp).placement));
		// No invented luminous area: a connected L union retains the missing corner.
		World unionWorld;
		unionWorld.visual.Quad(V(-2, -2, 0), V(0, -2, 0), V(0, 0, 0), V(-2, 0, 0));
		unionWorld.visual.Quad(V(0, -2, 0), V(2, -2, 0), V(2, 0, 0), V(0, 0, 0));
		unionWorld.visual.Quad(V(-2, 0, 0), V(0, 0, 0), V(0, 2, 0), V(-2, 2, 0));
		unionWorld.Brush(V(-16, -16, -4), V(16, 16, 0));
		unionWorld.Build();
		SS::Contract original;
		Must(SS::FromSurfaces(unionWorld.geometry, *unionWorld.scene.geometry, {0, 1, 2}, 71,
							  Read(), original));
		auto mesh = e;
		mesh.support = original;
		auto mr = unionWorld.At(mesh);
		CHECK(P::Admissible(mr.placement));
		CHECK(mr.emittingAreaWorldSquared == 12);
		auto filled = mesh;
		filled.support.kind = SS::Kind::Rectangle;
		filled.geometry.geometry.halfWidthWorld = filled.geometry.geometry.halfHeightWorld = 2;
		CHECK(!P::Admissible(unionWorld.At(filled).placement));
		World separate;
		separate.visual.Quad(V(-3, -1, 0), V(-2, -1, 0), V(-2, 1, 0), V(-3, 1, 0));
		separate.visual.Quad(V(2, -1, 0), V(3, -1, 0), V(3, 1, 0), V(2, 1, 0));
		separate.Brush(V(-16, -16, -4), V(16, 16, 0));
		separate.Build();
		CHECK(SS::FromSurfaces(separate.geometry, *separate.scene.geometry, {0, 1}, 71, Read(),
							   original)
				  .code == Code::Unavailable);
		SS::Contract one, two;
		Must(SS::FromSurfaces(separate.geometry, *separate.scene.geometry, {0}, 71, Read(), one));
		Must(SS::FromSurfaces(separate.geometry, *separate.scene.geometry, {1}, 72, Read(), two));
		CHECK(SS::Key(one) != SS::Key(two));
		// Independent source-side ray queries: the contact certificate never clears a wall.
		Shape::Model model;
		model.law = Shape::Law::DiffuseIntegral;
		model.output = Shape::Output::RelativeIncident;
		model.units.worldUnitsPerMeter = 1;
		model.allowAssumed = true;
		model.provenance = "Owned finite Lambertian integral; radiance amplitude, not compiler Q";
		auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
		Bake::Receiver receiver;
		receiver.sample = 99;
		receiver.world = Vec3(0, 0, 4);
		receiver.geometricNormal = receiver.shadingNormal = Vec3(0, 0, -1);
		receiver.mappedPlanar = true;
		receivers->push_back(receiver);
		auto evaluate = [&](World &world, Shape::Emitter source) {
			Bake::EvaluationContext ctx;
			ctx.visibility = world.scene.geometry;
			ctx.geometryKey = world.scene.geometry->CertificationKey();
			Shape::Sampling sampling;
			sampling.areaDivisions = 8;
			sampling.compareRefined = true;
			std::shared_ptr<const Shape::Block> b;
			Must(Shape::Prepare(model, {source}, Lease(receivers), ctx, sampling, Read(), b));
			std::vector<Shape::Prediction> out;
			Shape::Work cost;
			Must(b->Evaluate({source.rgbAmplitude}, Read(), out, cost));
			rays += b->PreparationWork().rays;
			work += b->PreparationWork().traversal;
			CHECK(out.size() == 1 && out[0].available);
			return out[0].value.x;
		};
		SS::Contract floorOriginal;
		Must(SS::FromSurfaces(w.geometry, *w.scene.geometry, {0}, 71, Read(), floorOriginal));
		// Restrict the exact mesh to the panel only for a numerical quadrature comparison.
		World panel;
		panel.visual.Quad(V(-1, -.5, 0), V(1, -.5, 0), V(1, .5, 0), V(-1, .5, 0));
		panel.Brush(V(-16, -16, -4), V(16, 16, 0));
		panel.Build();
		auto pe = e;
		pe.support = panel.Contract();
		pe.emittingSurface = 1;
		positive = evaluate(panel, pe);
		CHECK(positive > 0);
		auto me = pe;
		Must(SS::FromSurfaces(panel.geometry, *panel.scene.geometry, {0}, 71, Read(), me.support));
		double exact = evaluate(panel, me);
		World reversedPanel;
		reversedPanel.visual.surfaces=panel.visual.surfaces;
		for(size_t i=0;i<reversedPanel.visual.surfaces[0].indices.size();i+=3)
			std::swap(reversedPanel.visual.surfaces[0].indices[i+1],reversedPanel.visual.surfaces[0].indices[i+2]);
		reversedPanel.Brush(V(-16,-16,-4),V(16,16,0));reversedPanel.Build();
		auto reversedMesh=me;
		Must(SS::FromSurfaces(reversedPanel.geometry,*reversedPanel.scene.geometry,{0},71,Read(),reversedMesh.support));
		CHECK(P::Admissible(reversedPanel.At(reversedMesh).placement));
		CHECK(std::abs(evaluate(reversedPanel,reversedMesh)-exact)<1e-8);
		meshDifference = std::abs(exact - positive) / positive;
		CHECK(meshDifference < .002);
		World blockedWorld;
		blockedWorld.visual.surfaces = panel.visual.surfaces;
		blockedWorld.Brush(V(-16, -16, -4), V(16, 16, 0));
		blockedWorld.visual.Quad(V(-6, -6, 2), V(6, -6, 2), V(6, 6, 2), V(-6, 6, 2));
		blockedWorld.Brush(V(-6, -6, 1.9), V(6, 6, 2.1));
		blockedWorld.Build();
		auto be = pe;
		be.support = blockedWorld.Contract();
		CHECK(P::Admissible(blockedWorld.At(be).placement));
		blocked = evaluate(blockedWorld, be);
		CHECK(blocked == 0);
		// Fixed flush geometry, training-only amplitude fit, independently located held-out
		// receivers.
		Shape::Request request;
		request.mapKey = panel.scene.map.sha256;
		request.observationsKey = Key("X14 training fold");
		request.model = model;
		request.placement = panel.scene;
		request.geometry.visibility = panel.scene.geometry;
		request.geometry.geometryKey = panel.scene.geometry->CertificationKey();
		auto rows = std::make_shared<Stream<Shape::Observation>>(),
			 held = std::make_shared<Stream<Shape::Observation>>();
		for (unsigned fold = 0; fold < 2; ++fold)
		{
			auto target = std::make_shared<std::vector<Bake::Receiver>>();
			for (int j = 0; j < 8; ++j)
			{
				auto r = receiver;
				r.sample = 100 + 100 * fold + j;
				r.world = Vec3(float((j % 4 - 1.5) * .4), float(fold ? .9 : -.9), 4);
				target->push_back(r);
			}
			Shape::Sampling reference;
			reference.areaDivisions = 16;
			reference.compareRefined = true;
			std::shared_ptr<const Shape::Block> truth;
			Must(Shape::Prepare(model, {pe}, Lease(target), request.geometry, reference, Read(),
								truth));
			std::vector<Shape::Prediction> predictions;
			Shape::Work cost;
			Must(truth->Evaluate({pe.rgbAmplitude}, Read(), predictions, cost));
			for (size_t i = 0; i < target->size(); ++i)
			{
				Shape::Observation o;
				o.id = target->at(i).sample;
				o.receiver = target->at(i);
				o.role = fold ? Shape::Role::Heldout : Shape::Role::Training;
				o.spatialBlock = fold + 1;
				o.area = 1;
				o.comparable = true;
				for (unsigned c = 0; c < 3; ++c)
					o.channel[c].lower = o.channel[c].upper = V(predictions[i].value)[c];
				(fold ? held : rows)->data->push_back(o);
			}
		}
		Shape::Candidate candidate;
		candidate.initial = pe;
		candidate.initial.rgbAmplitude = Vec3(.1, .1, .1);
		candidate.upperRgb = Vec3(10, 10, 10);
		candidate.centreBounds = Box(V(-.1, -.1, -.1), V(.1, .1, .1));
		candidate.fitPosition = candidate.fitFrame = candidate.fitDimensions = candidate.fitCone =
			false;
		candidate.provenance = "Fixed owned flush support; hidden RGB amplitude not supplied";
		request.candidates = {candidate};
		request.training = rows;
		request.count = rows->data->size();
		request.searchSampling.areaDivisions = 8;
		request.strictSampling.areaDivisions = 16;
		Shape::FitResult fitted;
		Must(Shape::Fit(request, Read(), fitted));
		CHECK(fitted.fitted && fitted.strictReevaluated);
		fitError = Length(V(fitted.emitters[0].rgbAmplitude) - V(pe.rgbAmplitude)) /
				   Length(V(pe.rgbAmplitude));
		CHECK(fitError < .003);
		Shape::Score score;
		Must(Shape::EvaluateFrozen(fitted, held, held->data->size(), Shape::Role::Heldout,
								   request.geometry, request.strictSampling, 0, Read(), score));
		heldMse = score.intervalMse;
		CHECK(score.available && score.positive && score.comparable == held->data->size() &&
			  score.unknown == 0);
		CHECK(score.intervalMse < 1e-7 && score.blackMse > 1e-3);
		// Explicit ideal point on a mount emits only into its declared outward side.
		auto mountedPoint = Emitter(EmitterKind::Point);
		mountedPoint.support = panel.Contract(SS::Kind::MountedPoint);
		CHECK(evaluate(panel, mountedPoint) > 0);
		Shape::Model historical = model;
		historical.law = Shape::Law::HistoricalBake;
		historical.output = Shape::Output::RelativeIncident;
		historical.profile = Bake::NetRadiantProfile(Bake::Recipe{});
		historical.allowAssumed = true;
		Bake::EvaluationContext contactContext;
		contactContext.visibility = panel.scene.geometry;
		contactContext.geometryKey = panel.scene.geometry->CertificationKey();
		std::shared_ptr<const Shape::Block> unverified;
		CHECK(Shape::Prepare(historical, {mountedPoint}, Lease(receivers), contactContext, {},
							 Read(), unverified)
				  .code == Code::Unavailable);
		historical.allowFacetExpansion=true;
		contactContext.visibility=reversedPanel.scene.geometry;
		contactContext.geometryKey=reversedPanel.scene.geometry->CertificationKey();
		auto reverseHistorical=Shape::Prepare(historical,{reversedMesh},Lease(receivers),contactContext,{},Read(),unverified);
		CHECK(reverseHistorical.code==Code::Unavailable &&
			reverseHistorical.message.find("reversed captured source winding")!=std::string::npos);
		SS::Contract canonicalOrderA, canonicalOrderB;
		Must(SS::FromSurfaces(unionWorld.geometry, *unionWorld.scene.geometry, {0, 1, 2}, 71,
							  Read(), canonicalOrderA));
		Must(SS::FromSurfaces(unionWorld.geometry, *unionWorld.scene.geometry, {2, 0, 1}, 71,
							  Read(), canonicalOrderB));
		CHECK(SS::Key(canonicalOrderA) == SS::Key(canonicalOrderB));
		// A geometry lease must not become an alternate held-out vertex-RGB route.
		SurfaceCapture coloured;
		coloured.Begin(BuildPolicy::ForceRebuild);
		Must(coloured.SetMap(panel.scene.map));
		auto colourSurface = panel.visual.surfaces[0];
		colourSurface.vertices[0].colour[0][0] = 123;
		Must(coloured.Add(colourSurface));
		Must(coloured.Freeze());
		auto colourLease = coloured.Acquire();
		auto forbidden = panel.scene;
		forbidden.originalSurfaces = colourLease;
		Must(
			StaticVisibility::Build(colourLease, {}, Read(), forbidden.geometry, forbidden.domain));
		std::shared_ptr<const P::PlacementProvider> badPlacement;
		CHECK(P::MakePlacement(forbidden, Read(), badPlacement).code == Code::InvalidInput);
		// Existing Joint and strict production evaluation accept an explicit free-point contract.
		Rig rig;
		rig.request.candidates = {Point(101, V(-.5, 0, 1.2), true)};
		auto &source = rig.request.candidates[0].shape.initial;
		source.support.kind = SS::Kind::FreePoint;
		source.support.map = rig.request.mapKey;
		source.support.geometry = rig.request.geometry.geometryKey;
		source.support.provenance = "Owned explicit ideal point support; no mount assumed";
		auto truth = rig.request.candidates[0];
		truth.shape.initial.rgbAmplitude = Vec3(2, 1, .5);
		rig.Observations(rig.Truth({Answer(truth, V(2, 1, .5))}));
		J::Result fit;
		Must(J::Solve(rig.request, Read(), fit));
		CHECK(!fit.alternatives.empty());
		CHECK(fit.work.forwardCalls > 0);
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	if (argc == 2)
	{
		std::ofstream f(argv[1]);
		f << "{\"schema\":\"org.openjk.emitter-support-native.v1\",\"checks\":" << checks
		  << ",\"failures\":" << failures << ",\"panel_area\":" << area
		  << ",\"positive_response\":" << positive << ",\"blocked_response\":" << blocked
		  << ",\"triangle_rectangle_relative_difference\":" << meshDifference
		  << ",\"fit_rgb_relative_error\":" << fitError << ",\"heldout_mse\":" << heldMse
		  << ",\"rays\":" << rays << ",\"work\":" << work << "}";
	}
	std::printf(
		"X14 checks=%d failures=%d positive=%.9g blocked=%.9g mesh_difference=%.9g rays=%llu\n",
		checks, failures, positive, blocked, meshDifference, (unsigned long long)rays);
	return failures ? 1 : 0;
}
