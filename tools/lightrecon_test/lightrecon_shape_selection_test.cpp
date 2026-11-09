// Owned geometric witnesses and sealed same-material shape comparisons.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_test_scene.h"
#include "lightrecon/shape_selection.h"
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace JointTest;
namespace SSel = Rend2LightRecon::Core::ShapeSelection;
namespace SS = Rend2LightRecon::Core::SourceSupport;
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
struct Fixture
{
	Scene visual;
	SSel::Request request;
	Bake::EvaluationContext context;
	Fixture() : visual(false)
	{
		visual.surfaces.clear();
	}
	void Panel(double x, double y, double z = 0)
	{
		visual.Quad(V(-x, -y, z), V(x, -y, z), V(x, y, z), V(-x, y, z), MaterialGlow);
		visual.surfaces.back().materialFacts.cull = SurfaceCull::Back;
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
		visual.map.path = "maps/asr19-owned.bsp";
		visual.map.sha256 = key.Finish();
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Must(capture.SetMap(visual.map));
		for (auto s : visual.surfaces)
			Must(capture.Add(s));
		Must(capture.Freeze());
		request.fixture = 71;
		request.scene.map = visual.map;
		request.scene.domain = visual.domain;
		request.scene.domainKey = Key("ASR19 explicitly empty owned volume");
		request.scene.originalSurfaces = capture.Acquire();
		Must(StaticVisibility::Build(capture.Acquire(), {}, Read(), visual.visibility,
									 visual.domain));
		request.scene.geometry = visual.visibility;
		request.emittingSurfaces = {0};
		request.fullSurfaceEmissionAttested = true;
		request.emissionProvenance = "Owned uniformly luminous exact geometry";
		context.visibility = visual.visibility;
		context.geometryKey = visual.visibility->CertificationKey();
		context.materialKey = Key("ASR19 one owned receiver material");
	}
	SSel::Result Rank()
	{
		SSel::Result r;
		Must(SSel::Rank(request, Read(), r));
		return r;
	}
};
size_t Index(const SSel::Result &r, EmitterKind kind)
{
	for (size_t i = 0; i < r.ranked.size(); ++i)
		if (r.ranked[i].kind == kind)
			return i;
	return SIZE_MAX;
}
bool Has(const SSel::Rival &r, SSel::Reason x)
{
	return std::find(r.reasons.begin(), r.reasons.end(), x) != r.reasons.end();
}
S::Model Model(bool encoded = false)
{
	S::Model m;
	m.law = encoded ? S::Law::HistoricalBake : S::Law::DiffuseIntegral;
	m.output = encoded ? S::Output::EncodedRgb8 : S::Output::RelativeIncident;
	m.allowAssumed = m.allowFacetExpansion = true;
	m.units.worldUnitsPerMeter = 1;
	m.provenance = "ASR19 owned conditional model; no historical identification";
	Bake::Recipe recipe;
	recipe.fast = false;
	recipe.minimumDistanceWorld = .01;
	recipe.photonsPerParameter = 1;
	recipe.receiverPushWorld = 0;
	m.profile = Bake::NetRadiantProfile(recipe);
	m.historicalEmitterPushWorld = 0;
	return m;
}
S::Sampling Sampling()
{
	S::Sampling s;
	s.areaDivisions = 8;
	s.tubeSides = 32;
	s.tubeSegments = 8;
	s.compareRefined = false;
	return s;
}
std::shared_ptr<Stream<S::Observation>> Data(const Fixture &f, const S::Model &m, S::Emitter truth,
											 unsigned fold, bool far = false)
{
	auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
	for (unsigned i = 0; i < 5; ++i)
		for (unsigned j = 0; j < 3; ++j)
		{
			Bake::Receiver r;
			r.sample = 1 + fold * 100 + i * 3 + j;
			r.material = 1;
			r.world =
				Vec3(-8 + 4.f * i + .15f * fold, -1.1f + fold * .9f + .2f * j, far ? 200.f : 4.f);
			r.geometricNormal = r.shadingNormal = Vec3(0, 0, -1);
			r.mappedPlanar = r.compilerPlacementCertified = true;
			receivers->push_back(r);
		}
	std::shared_ptr<const S::Block> block;
	auto sampling = Sampling();
	sampling.areaDivisions = 16;
	Must(S::Prepare(m, {truth}, Lease(receivers), f.context, sampling, Read(), block));
	std::vector<S::Prediction> values;
	S::Work work;
	Must(block->Evaluate({truth.rgbAmplitude}, Read(), values, work));
	auto stream = std::make_shared<Stream<S::Observation>>();
	for (size_t i = 0; i < receivers->size(); ++i)
	{
		CHECK(values[i].available);
		S::Observation a;
		a.id = (*receivers)[i].sample;
		a.receiver = (*receivers)[i];
		a.spatialBlock = 1 + fold * 100 + i / 3;
		a.role = fold == 0 ? S::Role::Training : fold == 1 ? S::Role::Validation : S::Role::Heldout;
		a.comparable = true;
		for (unsigned c = 0; c < 3; ++c)
		{
			double v = V(values[i].value)[c];
			if (m.output == S::Output::EncodedRgb8)
				a.channel[c] = Amplitude::Rgb8(uint8_t(Clamp(v, 0, 254)));
			else
				a.channel[c].lower = a.channel[c].upper = v;
		}
		stream->data->push_back(a);
	}
	return stream;
}
SSel::ComparisonRequest ComparisonRequest(const Fixture &f, const SSel::Result &prior,
										  const S::Model &m,
										  std::shared_ptr<Stream<S::Observation>> training,
										  std::shared_ptr<Stream<S::Observation>> validation)
{
	SSel::ComparisonRequest r;
	r.prior = prior;
	r.material = 1;
	r.sameMaterialResponseAttested = true;
	r.targets = m.output == S::Output::EncodedRgb8 ? SSel::TargetDomain::HistoricalEncodedRgb8
												   : SSel::TargetDomain::RelativeLinearAttested;
	r.fitting.model = m;
	r.fitting.geometry = f.context;
	r.fitting.placement = f.request.scene;
	r.fitting.mapKey = prior.map;
	r.fitting.observationsKey = Key("ASR19 sealed training fold");
	r.fitting.training = training;
	r.fitting.count = training->data->size();
	r.validation = validation;
	r.validationCount = validation->data->size();
	r.fitting.searchSampling = r.fitting.strictSampling = Sampling();
	r.fitting.strictSampling.compareRefined = true;
	r.fitting.options.maxTrials = 16;
	r.fitting.options.maxSweeps = 2;
	r.fitting.options.huber = 100000;
	r.equivalentMseSlack = 1e-5;
	return r;
}
std::string Q(const std::string &s)
{
	std::ostringstream o;
	o << '"';
	for (char c : s)
	{
		if (c == '"' || c == '\\')
			o << '\\';
		if (c == '\n')
			o << "\\n";
		else
			o << c;
	}
	o << '"';
	return o.str();
}
void Vec(std::ostream &o, Vec3 p)
{
	o << '[' << p.x << ',' << p.y << ',' << p.z << ']';
}
std::vector<std::string> records;
void Record(const std::string &name, const SSel::Result &r, const SSel::Comparison *c = nullptr,
			double bakedPointControlMse = -1)
{
	std::ostringstream o;
	o << std::setprecision(17) << "{\"name\":" << Q(name) << ",\"key\":" << Q(Disk::Hex(r.key))
	  << ",\"primitive_known\":" << (r.primitiveKnown ? "true" : "false")
	  << ",\"emitting_geometry_exact\":" << (r.emittingGeometryExact ? "true" : "false")
	  << ",\"area\":" << r.emittingAreaWorldSquared << ",\"width\":" << r.widthWorld
	  << ",\"height\":" << r.heightWorld << ",\"aspect\":" << r.aspect
	  << ",\"coverage\":" << r.coverage << ",\"triangles\":[";
	for (size_t i = 0; i < r.original.original.count; ++i)
	{
		if (i)
			o << ',';
		o << '[';
		for (unsigned j = 0; j < 3; ++j)
		{
			if (j)
				o << ',';
			Vec(o, r.original.original.data[i].world[j]);
		}
		o << ']';
	}
	o << "],\"bounds_known\":" << (r.actualBounds.known ? "true" : "false") << ",\"bounds\":[";
	Vec(o, r.actualBounds.minWorld);
	o << ',';
	Vec(o, r.actualBounds.maxWorld);
	o << "],\"ranked\":[";
	for (size_t i = 0; i < r.ranked.size(); ++i)
	{
		if (i)
			o << ',';
		const auto &v = r.ranked[i];
		const auto &g = v.emitter.geometry;
		o << "{\"kind\":" << unsigned(v.kind) << ",\"type\":" << Q(SSel::KindName(v.kind))
		  << ",\"status\":" << Q(SSel::StatusName(v.status)) << ",\"preference\":" << v.preference
		  << ",\"position\":";
		Vec(o, g.position.world);
		o << ",\"forward\":";
		Vec(o, g.frame.forward);
		o << ",\"right\":";
		Vec(o, g.frame.right);
		o << ",\"up\":";
		Vec(o, g.frame.up);
		o << ",\"half_width\":" << g.geometry.halfWidthWorld
		  << ",\"half_height\":" << g.geometry.halfHeightWorld
		  << ",\"half_length\":" << g.geometry.halfLengthWorld
		  << ",\"radius\":" << g.geometry.radiusWorld;
		o << ",\"cos_outer\":" << g.angular.cosOuter << ",\"reasons\":[";
		for (size_t j = 0; j < v.reasons.size(); ++j)
			o << (j ? "," : "") << Q(SSel::ReasonName(v.reasons[j]));
		o << "],\"uncertainties\":[";
		for (size_t j = 0; j < v.uncertainties.size(); ++j)
			o << (j ? "," : "") << Q(v.uncertainties[j]);
		o << ']';
		if (c)
		{
			o << ",\"fit_available\":" << (c->fits[i].fitted ? "true" : "false")
			  << ",\"validation_available\":" << (c->validation[i].available ? "true" : "false")
			  << ",\"validation_mse\":" << c->validation[i].intervalMse
			  << ",\"heldout_available\":" << (c->heldout[i].available ? "true" : "false")
			  << ",\"heldout_mse\":" << c->heldout[i].intervalMse
			  << ",\"heldout_black_mse\":" << c->heldout[i].blackMse
			  << ",\"heldout_quadrature_difference\":" << c->heldout[i].quadratureDifference
			  << ",\"fit_limitations\":[";
			for (size_t j = 0; j < c->fits[i].limitations.size(); ++j)
				o << (j ? "," : "") << Q(c->fits[i].limitations[j]);
			o << ']';
		}
		o << '}';
	}
	o << "],\"limitation\":" << Q(r.limitation);
	if (c)
	{
		o << ",\"photometric_alternatives\":[";
		for (size_t i = 0; i < c->photometricAlternatives.size(); ++i)
			o << (i ? "," : "") << c->photometricAlternatives[i];
		o << "],\"target_domain\":"
		  << Q(c->targets == SSel::TargetDomain::HistoricalEncodedRgb8 ? "HistoricalEncodedRgb8"
																	   : "RelativeLinearAttested")
		  << ",\"comparison_limitations\":[";
		for (size_t i = 0; i < c->limitations.size(); ++i)
			o << (i ? "," : "") << Q(c->limitations[i]);
		o << ']';
	}
	if (bakedPointControlMse >= 0)
		o << ",\"authored_bake_point_control\":{\"position\":[0,0,1],\"heldout_encoded_interval_"
			 "mse\":"
		  << bakedPointControlMse << ",\"fixture_extent_collapsed\":true}";
	o << '}';
	records.push_back(o.str());
}
void Compare(const std::string &name, Fixture &f, SSel::Result prior, EmitterKind truthKind,
			 bool encoded = false, bool far = false)
{
	auto truth = prior.ranked[Index(prior, truthKind)].emitter;
	if (encoded)
	{
		// Original bake authoring is a different hypothesis from a modern lamp.
		// Preserve the historical exact-contact guard: the authored bake point is
		// independently declared one unit in front of the visible luminous panel.
		truth.geometry.position.world.z = 1;
		truth.emittingSurface = 0;
		truth.support.kind = SS::Kind::FreePoint;
		truth.support.mountKnown = false;
		truth.support.surfaces.clear();
		truth.support.provenance = "Owned explicitly authored bake point at z=1";
	}
	truth.rgbAmplitude = encoded ? Vec3(1000, 600, 300) : Vec3(20, 12, 6);
	auto model = Model(encoded);
	auto train = Data(f, model, truth, 0, far), validation = Data(f, model, truth, 1, far),
		 heldout = Data(f, model, truth, 2, far);
	auto r = ComparisonRequest(f, prior, model, train, validation);
	if (far)
		r.equivalentMseSlack = 1e-4;
	SSel::Comparison c;
	Must(SSel::FitAndValidate(r, Read(), c));
	CHECK(c.frozen && !c.heldoutAudited);
	auto alternatives = c.photometricAlternatives;
	auto key = c.prior.key;
	Must(SSel::AuditHeldout(r, heldout, heldout->data->size(), Read(), c));
	CHECK(c.heldoutAudited && c.prior.key == key && alternatives == c.photometricAlternatives);
	CHECK(!c.photometricAlternatives.empty());
	double controlMse = -1;
	if (encoded)
	{
		auto control = r.fitting;
		S::Candidate candidate;
		candidate.initial = truth;
		candidate.fitPosition = candidate.fitFrame = candidate.fitDimensions = candidate.fitCone =
			false;
		candidate.centreBounds.known = true;
		candidate.centreBounds.minWorld = candidate.centreBounds.maxWorld =
			truth.geometry.position.world;
		candidate.upperRgb = Vec3(100000, 100000, 100000);
		candidate.provenance = "Independent authored bake point control";
		control.candidates = {candidate};
		S::FitResult fit;
		Must(S::Fit(control, Read(), fit));
		S::Score score;
		Must(S::EvaluateFrozen(fit, heldout, heldout->data->size(), S::Role::Heldout, f.context,
							   control.strictSampling, 0, Read(), score));
		controlMse = score.intervalMse;
		CHECK(score.available && controlMse < 1e-5);
		CHECK(prior.ranked.front().kind == EmitterKind::Rectangle);
		CHECK(!c.heldout[Index(prior, EmitterKind::Point)].available);
	}
	if (far)
		CHECK(c.photometricAlternatives.size() > 1);
	if (!encoded && !far)
	{
		auto i = Index(prior, truthKind);
		CHECK(c.heldout[i].available && c.heldout[i].intervalMse < c.heldout[i].blackMse * .02);
	}
	if (name == "flat_panel" || name == "measured_tube")
		CHECK(c.heldout[Index(prior, EmitterKind::Point)].intervalMse >
			  100 * c.heldout[Index(prior, truthKind)].intervalMse);
	if (name == "focused_hood_spot")
		CHECK(c.heldout[Index(prior, EmitterKind::Point)].intervalMse >
			  c.heldout[Index(prior, EmitterKind::Spot)].intervalMse + 1e-5);
	Record(name, prior, &c, controlMse);
	// Reject material mismatch, leaked blocks and encoded-as-linear targets.
	auto bad = r;
	bad.sameMaterialResponseAttested = false;
	SSel::Comparison unused;
	CHECK(SSel::FitAndValidate(bad, Read(), unused).code == Code::InvalidInput);
	bad = r;
	auto saved = (*validation->data)[0].spatialBlock;
	(*validation->data)[0].spatialBlock = (*train->data)[0].spatialBlock;
	CHECK(SSel::FitAndValidate(bad, Read(), unused).code == Code::InvalidInput);
	(*validation->data)[0].spatialBlock = saved;
	bad = r;
	bad.material = 2;
	CHECK(SSel::FitAndValidate(bad, Read(), unused).code == Code::InvalidInput);
	if (encoded)
	{
		bad = r;
		bad.fitting.model = Model();
		CHECK(SSel::FitAndValidate(bad, Read(), unused).code == Code::Unavailable);
	}
	// Auditing another partition cannot run twice or leak an already used block.
	CHECK(SSel::AuditHeldout(r, heldout, heldout->data->size(), Read(), c).code ==
		  Code::InvalidState);
	c.heldoutAudited = false;
	auto heldBlock = (*heldout->data)[0].spatialBlock;
	(*heldout->data)[0].spatialBlock = (*train->data)[0].spatialBlock;
	CHECK(SSel::AuditHeldout(r, heldout, heldout->data->size(), Read(), c).code ==
		  Code::InvalidInput);
	(*heldout->data)[0].spatialBlock = heldBlock;
}
int main(int argc, char **argv)
{
	try
	{
		Fixture pin;
		pin.Panel(.05, .05);
		pin.Build();
		auto p = pin.Rank();
		CHECK(p.primitiveKnown && p.ranked.front().kind == EmitterKind::Point);
		Record("pin_bulb", p);
		Fixture panel;
		panel.Panel(6, 4);
		panel.Build();
		auto a = panel.Rank();
		CHECK(a.primitiveKnown && a.ranked.front().kind == EmitterKind::Rectangle);
		CHECK(a.emittingAreaWorldSquared == 96 &&
			  Has(a.ranked[Index(a, EmitterKind::Point)], SSel::Reason::PointCollapsesExtent));
		Compare("flat_panel", panel, a, EmitterKind::Rectangle);
		Compare("baked_point_despite_visible_panel", panel, a, EmitterKind::Point, true);
		Compare("distant_shape_ambiguity", panel, a, EmitterKind::Point, false, true);
		Fixture strip;
		strip.Panel(8, .25);
		strip.Build();
		auto s = strip.Rank();
		CHECK(s.ranked.front().kind == EmitterKind::Rectangle &&
			  Has(s.ranked.front(), SSel::Reason::FlatStrip));
		CHECK(s.ranked[Index(s, EmitterKind::Tube)].status == SSel::Status::Unknown);
		Record("flat_strip", s);
		Fixture spot;
		spot.Panel(.1, .1);
		spot.visual.Quad(V(-1, -1, 0), V(-1, 1, 0), V(-1, 1, 3), V(-1, -1, 3));
		spot.visual.Quad(V(1, 1, 0), V(1, -1, 0), V(1, -1, 3), V(1, 1, 3));
		spot.visual.Quad(V(-1, -1, 0), V(1, -1, 0), V(1, -1, 3), V(-1, -1, 3));
		spot.visual.Quad(V(1, 1, 0), V(-1, 1, 0), V(-1, 1, 3), V(1, 1, 3));
		spot.Build();
		spot.request.hoodSurfaces = {1, 2, 3, 4};
		auto focused = spot.Rank();
		CHECK(focused.ranked.front().kind == EmitterKind::Spot &&
			  Has(focused.ranked.front(), SSel::Reason::HoodDirectional));
		Compare("focused_hood_spot", spot, focused, EmitterKind::Spot);
		spot.visual.surfaces[1].materialFacts.signals = MaterialCutout;
		spot.Build();
		spot.request.hoodSurfaces = {1, 2, 3, 4};
		auto unknownHood = spot.Rank();
		CHECK(unknownHood.ranked[Index(unknownHood, EmitterKind::Spot)].status ==
			  SSel::Status::Unknown);
		Record("unknown_alpha_hood", unknownHood);
		auto cue = pin.request;
		cue.coneCueKnown = true;
		cue.coneHalfAngleRadians = .4;
		cue.cueProvenance = "Independent owned lighting cone cue; conditional";
		SSel::Result cone;
		Must(SSel::Rank(cue, Read(), cone));
		CHECK(cone.ranked.front().kind == EmitterKind::Spot &&
			  Has(cone.ranked.front(), SSel::Reason::ConeCue));
		Record("optional_cone_cue", cone);
		Fixture tube;
		tube.Panel(8, 1);
		tube.Build();
		auto t = tube.Rank();
		tube.request.tubeBodyKnown = true;
		tube.request.tubeBodyProvenance =
			"Owned measured lateral body, radius .25; tangent centreline z=.25";
		auto body = t.ranked[Index(t, EmitterKind::Rectangle)].emitter;
		body.geometry.kind = EmitterKind::Tube;
		body.geometry.position.world.z = .25f;
		body.geometry.geometry.halfLengthWorld = 6;
		body.geometry.geometry.radiusWorld = .25;
		body.support.kind = SS::Kind::Cylinder;
		body.support.original = {};
		body.support.triangleSurfaces.clear();
		tube.request.tubeBody = body;
		t = tube.Rank();
		CHECK(t.ranked.front().kind == EmitterKind::Tube &&
			  Has(t.ranked.front(), SSel::Reason::CylindricalBody));
		Compare("measured_tube", tube, t, EmitterKind::Tube);
		auto invalidTube = tube.request;
		invalidTube.tubeBody.geometry.geometry.radiusWorld = 0;
		SSel::Result invalid;
		CHECK(SSel::Rank(invalidTube, Read(), invalid).code == Code::InvalidInput);
		invalidTube = tube.request;
		invalidTube.tubeBody.geometry.position.world.z = 0;
		Must(SSel::Rank(invalidTube, Read(), invalid));
		CHECK(invalid.ranked[Index(invalid, EmitterKind::Tube)].status == SSel::Status::Rejected);
		Fixture blocked;
		blocked.Panel(6, 4);
		blocked.visual.Quad(V(0, -5, .5), V(0, 5, .5), V(0, 5, 3), V(0, -5, 3));
		blocked.Build();
		auto b = blocked.Rank();
		CHECK(b.ranked.front().kind == EmitterKind::Rectangle);
		Compare("partially_blocked_panel", blocked, b, EmitterKind::Rectangle);
		Fixture intersected;
		intersected.Panel(6, 4);
		intersected.visual.Quad(V(3, -5, -1), V(3, 5, -1), V(3, 5, 1), V(3, -5, 1));
		intersected.Build();
		auto crossing = intersected.Rank();
		CHECK(!crossing.primitiveKnown && crossing.ranked.front().kind == EmitterKind::Point);
		CHECK(crossing.ranked[Index(crossing, EmitterKind::Rectangle)].status ==
			  SSel::Status::Rejected);
		Record("panel_intersection_point_proxy_only", crossing);
		Fixture irregular;
		irregular.Panel(1, 1);
		irregular.visual.Quad(V(1, -1, 0), V(3, -1, 0), V(3, 1, 0), V(1, 1, 0));
		irregular.visual.surfaces.back().materialFacts.cull = SurfaceCull::Back;
		irregular.visual.Quad(V(-1, 1, 0), V(1, 1, 0), V(1, 3, 0), V(-1, 3, 0));
		irregular.visual.surfaces.back().materialFacts.cull = SurfaceCull::Back;
		irregular.Build();
		irregular.request.emittingSurfaces = {0, 1, 2};
		auto l = irregular.Rank();
		CHECK(!l.primitiveKnown && l.original.original.count == 6);
		Record("irregular_L_union", l);
		Fixture separate;
		separate.Panel(1, 1);
		separate.visual.Quad(V(3, -1, 0), V(5, -1, 0), V(5, 1, 0), V(3, 1, 0));
		separate.visual.surfaces.back().materialFacts.cull = SurfaceCull::Back;
		separate.Build();
		separate.request.emittingSurfaces = {0, 1};
		auto d = separate.Rank();
		CHECK(!d.primitiveKnown && !d.emittingGeometryExact && !d.limitation.empty());
		Record("disconnected", d);
		auto reordered = irregular.request;
		std::reverse(reordered.emittingSurfaces.begin(), reordered.emittingSurfaces.end());
		SSel::Result repeat;
		Must(SSel::Rank(reordered, Read(), repeat));
		CHECK(repeat.key == l.key);
		panel.request.fullSurfaceEmissionAttested = false;
		auto mask = panel.Rank();
		CHECK(!mask.primitiveKnown && mask.ranked.front().status == SSel::Status::Approximation &&
			  Has(mask.ranked.front(), SSel::Reason::MaskUnknown));
		Record("unknown_luminous_mask", mask);
		auto policy = panel.request;
		policy.scene.occupancyPolicy = PointRefine::OccupancyPolicy::ConditionalBake;
		policy.scene.occupancyProvenance = "Explicit alternate scene policy";
		Must(SSel::Rank(policy, Read(), repeat));
		CHECK(mask.key != repeat.key);
		auto stale = panel.request;
		stale.scene.map.sha256 = Key("Stale source map");
		CHECK(SSel::Rank(stale, Read(), repeat).code == Code::InvalidInput);
		struct Cancel : Cancellation
		{
			bool IsCancelled() const noexcept override
			{
				return true;
			}
		} cancel;
		auto read = Read();
		read.cancellation = &cancel;
		CHECK(SSel::Rank(panel.request, read, repeat).code == Code::Cancelled);
		auto limited = panel.request;
		limited.policy.maxWork = 1;
		CHECK(SSel::Rank(limited, Read(), repeat).code == Code::WorkLimit);
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	if (argc == 2)
	{
		std::ofstream o(argv[1]);
		o << "{\"schema\":\"org.openjk.modern-shape-selection.v1\",\"checks\":" << checks
		  << ",\"failures\":" << failures
		  << ",\"canonical_storage_changed\":false,\"historical_source_truth\":\"Unavailable\","
			 "\"fixtures\":[";
		for (size_t i = 0; i < records.size(); ++i)
			o << (i ? "," : "") << records[i];
		o << "]}\n";
	}
	std::printf("ASR19 checks=%d failures=%d fixtures=%zu\n", checks, failures, records.size());
	return failures ? 1 : 0;
}
