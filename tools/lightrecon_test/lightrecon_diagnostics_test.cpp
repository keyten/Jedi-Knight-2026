// V03 data-only regression witnesses. No OpenGL or game assets.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/diagnostics.h"
#include "probe_fixtures.h"
#include "producer_test_scene.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
namespace D = Rend2LightRecon::Core::Diagnostics;
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
int checks = 0, failures = 0;
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
void OK(Error e)
{
	CHECK(bool(e));
	if (!e)
		throw std::runtime_error(e.message);
}
ReconstructedLightingAsset Asset()
{
	ReconstructedLightingAsset a;
	a.lineage = "V03 owned diagnostic fixture";
	a.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "V03/test";
	a.provenanceRecords.push_back(p);
	return a;
}
PhysicalEmitter Emitter(Id id, EmitterKind kind)
{
	PhysicalEmitter e;
	e.source.id = id;
	e.source.provenance = 1;
	e.kind = kind;
	e.position.known = kind != EmitterKind::Directional;
	e.position.world = Vec3(1, 2, 3);
	e.frame.known = true;
	e.geometry.known = true;
	e.geometry.halfWidthWorld = 2;
	e.geometry.halfHeightWorld = 1;
	e.geometry.halfLengthWorld = 3;
	e.geometry.radiusWorld = .25;
	e.angular.cosInner = .9f;
	e.angular.cosOuter = .8f;
	e.angular.kind = kind == EmitterKind::Point
						 ? AngularKind::Isotropic
						 : kind == EmitterKind::Spot
							   ? AngularKind::SmoothCone
							   : kind == EmitterKind::Directional ? AngularKind::Parallel
																  : AngularKind::Lambertian;
	return e;
}
void Interchange(const std::string &output)
{
	D::Scene scene;
	scene.map[0] = 4;
	scene.mapPath = "maps/owned-v03.bsp";
	scene.algorithm = "test/1";
	scene.scope = "NO physically recovered original source truth";
	D::Builder builder(scene);
	auto asset = Asset();
	Id id = 9007199254740993ull;
	for (auto kind : {EmitterKind::Point, EmitterKind::Spot, EmitterKind::Rectangle,
					  EmitterKind::Tube, EmitterKind::Directional})
	{
		auto e = Emitter(id++, kind);
		asset.emitters.push_back(e);
		builder.Emitter(e, D::State::Conditional, "specified-physical-relative",
						"alternative hypothesis");
	}
	auto unknown = Emitter(id++, EmitterKind::Point);
	unknown.position.known = false;
	builder.Emitter(unknown, D::State::Unavailable, "UnknownProfile", "unknown position");
	DynamicBoundary door;
	door.id = 331;
	door.model = 2;
	door.bounds = ProbeFixture::Box(Vec3(-1, -1, 0), Vec3(1, 1, 2));
	builder.Dynamic(door);
	RayResult hit;
	hit.state = VisibilityState::Blocked;
	hit.terminalHit.positionKnown = true;
	hit.terminalHit.surface = 7;
	hit.terminalHit.material = 19;
	hit.terminalHit.distanceWorld = 2;
	hit.terminalHit.positionWorld = Vec3(0, 0, 2);
	builder.Ray(44, asset.emitters[0].source.id, Vec3(), Vec3(0, 0, 4), hit,
				"controlled known wall reference");
	hit.state = VisibilityState::Unknown;
	hit.terminalHit = {};
	builder.Ray(45, asset.emitters[0].source.id, Vec3(), Vec3(1, 0, 4), hit,
				"unknown alpha is not visible");
	auto residual =
		D::Compare(50, Vec3(1, 1, 0), Vec3(7, 20, 300), Vec3(8, 19, 255), Vec3(9, 21, 255), 7, 4,
				   "encoded-rgb8/truncating-profile", PhotometricUnit::Relative, true);
	CHECK(residual.valueKnown && residual.value.x == -1 && residual.value.y == 0 &&
		  residual.value.z == 0);
	builder.Add(residual);
	auto missing = D::Compare(51, Vec3(), Vec3(1, 1, 1), Vec3(), Vec3(), 7, 0, "",
							  PhotometricUnit::Lux, false);
	CHECK(!missing.valueKnown && missing.state == D::State::Unavailable);
	builder.Add(missing);
	D::Selection selection;
	selection.layers = 1u << unsigned(D::Layer::Visibility);
	selection.source = asset.emitters[0].source.id;
	size_t selected = 0;
	for (auto &r : scene.records)
		selected += selection.Matches(r);
	CHECK(selected == 2);
	std::string encoded;
	OK(D::Encode(scene, encoded));
	D::Scene decoded;
	OK(D::Decode(encoded, scene.map, decoded));
	CHECK(decoded.records.size() == scene.records.size());
	std::string again;
	OK(D::Encode(decoded, again));
	CHECK(again == encoded);
	auto reordered = scene;
	std::reverse(reordered.records.begin(), reordered.records.end());
	OK(D::Encode(reordered, again));
	CHECK(again == encoded);
	auto invalid = scene;
	invalid.records[0].a.x = std::numeric_limits<float>::infinity();
	CHECK(!D::Encode(invalid, again));
	invalid = scene;
	invalid.records[0].layer = D::Layer::Count;
	CHECK(!D::Encode(invalid, again));
	auto corrupt = encoded;
	corrupt[100] ^= 1;
	CHECK(!D::Decode(corrupt, scene.map, decoded));
	CHECK(decoded.records.empty());
	CHECK(!D::Decode(encoded.substr(0, encoded.size() - 10), scene.map, decoded));
	auto reseal = [](std::string text) {
		text.resize(text.rfind("SHA256 "));
		return text + "SHA256 " + Disk::Hex(Disk::Hash(text.data(), text.size())) + "\n";
	};
	auto newer = encoded;
	newer.replace(0, 13, "JKLIGHTDEBUG2");
	CHECK(!D::Decode(reseal(newer), scene.map, decoded));
	auto negative = encoded;
	auto row = negative.find('\n', negative.find('\n') + 1) + 1;
	auto space = negative.find(' ', row);
	negative.replace(row, space - row, "-1");
	CHECK(!D::Decode(reseal(negative), scene.map, decoded));
	auto wrong = scene.map;
	wrong[0] ^= 1;
	CHECK(!D::Decode(encoded, wrong, decoded));
	D::Limits low;
	low.maxRecords = 1;
	CHECK(!D::Decode(encoded, scene.map, decoded, low));
	std::ostringstream json, csv, obj;
	OK(D::Json(scene, json));
	OK(D::Csv(scene, csv));
	OK(D::Obj(scene, obj));
	CHECK(json.str().find("\"9007199254740993\"") != std::string::npos);
	CHECK(json.str().find("hit-surface=7") != std::string::npos);
	D::Heatmap scale;
	scale.maximum = 32;
	scale.signedValues = true;
	std::string heat;
	OK(D::HeatmapPpm(scene.records, D::Layer::Residual, 4, scale, heat));
	CHECK(heat.find("unknown=magenta") != std::string::npos);
	scale.maximum = scale.minimum;
	CHECK(!D::HeatmapPpm(scene.records, D::Layer::Residual, 4, scale, heat));
	scale.maximum = 32;
	auto mixed = scene.records;
	auto other = residual;
	other.domain = "physically-incompatible-domain";
	mixed.push_back(other);
	CHECK(!D::HeatmapPpm(mixed, D::Layer::Residual, 4, scale, heat));
	D::Scene malformed;
	D::Builder shapes(malformed);
	auto panel = Emitter(77, EmitterKind::Rectangle);
	panel.geometry.halfWidthWorld = -1;
	shapes.Emitter(panel, D::State::Conditional, "physical", "invalid panel");
	CHECK(malformed.records.back().state == D::State::Invalid);
	auto unknownFrame = Emitter(78, EmitterKind::Spot);
	unknownFrame.frame.known = false;
	shapes.Emitter(unknownFrame, D::State::Conditional, "physical", "missing orientation");
	CHECK(malformed.records.back().state == D::State::Unavailable);
	bool photon = false, toward = false;
	for (auto &r : scene.records)
		if (r.layer == D::Layer::Sun)
		{
			if (r.reason.find("photon travel direction;") != std::string::npos)
				photon = r.b.z - r.a.z == 48;
			if (r.reason.find("receiver-to-sun") != std::string::npos)
				toward = r.b.z - r.a.z == -32;
		}
	CHECK(photon && toward);
	Manager owner;
	std::ostringstream report, human;
	OK(D::Report(owner.Acquire(), nullptr, report, &human));
	CHECK(report.str().find("\"asset_schema\":null") != std::string::npos);
	CHECK(owner.Publish(asset) == Status::Ok);
	auto old = owner.Acquire();
	report.str("");
	OK(D::Report(old, nullptr, report, &human, &scene));
	CHECK(report.str().find("\"existence\":null") != std::string::npos);
	CHECK(report.str().find("\"availability_is_physical_certification\":false") !=
		  std::string::npos);
	owner.Reset();
	CHECK(old.Asset().status == Status::Ok);
	CHECK(owner.Acquire().Asset().status != Status::Ok);
	D::Scene bounded;
	D::Limits limit;
	limit.maxRecords = 3;
	D::Builder budget(bounded, limit);
	for (unsigned j = 0; j < 10; ++j)
	{
		D::Record r;
		r.id = j + 1;
		budget.Add(r);
	}
	CHECK(bounded.records.size() == 3 && bounded.omitted == 7 && !bounded.complete);
	if (!output.empty())
	{
		std::ofstream data(output + ".jkdbg", std::ios::binary);
		data << encoded;
		std::ofstream js(output + ".json"), cs(output + ".csv"), ob(output + ".obj"),
			rp(output + "-report.json"), hm(output + ".ppm", std::ios::binary);
		OK(D::Json(scene, js));
		OK(D::Csv(scene, cs));
		OK(D::Obj(scene, ob));
		OK(D::Report(old, nullptr, rp, nullptr, &scene));
		scale.maximum = 32;
		OK(D::HeatmapPpm(scene.records, D::Layer::Residual, 4, scale, heat));
		hm << heat;
	}
	std::cout << "INTERCHANGE records=" << scene.records.size() << " bytes=" << encoded.size()
			  << " key=" << Disk::Hex(Disk::Hash(encoded.data(), encoded.size())) << '\n';
}
void ProbeAndAngular()
{
	ProbeFixture::Scene geometry;
	geometry.Solid(ProbeFixture::Box(Vec3(-.1f, -4, 0), Vec3(.1f, 4, 8)));
	auto input = geometry.Input();
	auto topology = geometry.Build(input);
	D::Scene scene;
	D::Builder builder(scene);
	builder.Probes(*topology);
	builder.ProbeQuery(101, *topology, Vec3(-2, 0, 2));
	builder.ProbeQuery(102, *topology, Vec3(0, 0, 2));
	CHECK(scene.unknownQueries >= 1);
	CHECK(!scene.records.empty());
	Fields::Artifact field;
	field.topology = topology;
	auto d = std::make_shared<ConditionalFieldDescriptor>();
	d->field = 800;
	d->quantity = ConditionalQuantity::NormalIrradiance;
	d->unit = PhotometricUnit::Relative;
	d->encoding = "SHL2";
	d->profile = "controlled relative";
	field.descriptor = d;
	Probes::Value v;
	v.probe = 999;
	v.evaluatedPositionWorld = Vec3(-2, 0, 2);
	v.angular.present = true;
	v.angular.degree = 0;
	v.angular.quantity = Angular::Quantity::DiffuseIrradiance;
	v.angular.sh = {Angular::V{{1, 2, 3}}};
	field.values.records.push_back(v);
	builder.Field(field);
	field.values.records[0].angular.sh.clear();
	field.values.records[0].angular.quantity = Angular::Quantity::IncomingRadiance;
	field.values.records[0].angular.sg = {
		Angular::Lobe(Angular::V{{0, 0, 1}}, 8, Angular::V{{1, 2, 3}})};
	builder.Field(field);
	size_t irradiance = 0, radiance = 0;
	for (auto &r : scene.records)
	{
		irradiance += r.quantity == "surface-normal-dependent-irradiance";
		radiance += r.quantity == "incoming-directional-radiance";
	}
	CHECK(irradiance == 6 && radiance == 6);
}
void Observer()
{
	using namespace JointTest;
	Rig rig;
	auto c = Point(10, V(-.6, 0, .8));
	auto bad = Point(11, V(0, 0, .15));
	rig.request.candidates = {c, bad};
	rig.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	struct WallDomain : RegionDomain
	{
		Error Locate(Vec3 p, SpatialLocation &l, double = 1e-5) const override
		{
			l = {};
			l.space = p.z < .25f ? SpaceClass::Solid : SpaceClass::Free;
			return {};
		}
		Error ClassifyBox(const Bounds &b, SpaceClass &s, uint64_t &w) const override
		{
			w = 1;
			s = b.maxWorld.z < .25f ? SpaceClass::Solid
									: b.minWorld.z >= .25f ? SpaceClass::Free : SpaceClass::Unknown;
			return {};
		}
	};
	rig.request.placement.domain = std::make_shared<WallDomain>();
	rig.request.placement.domainKey =
		JointFixture::Key("V03 known lower solid source-occupancy slab");
	J::Result before, after;
	auto q = rig.request;
	q.options.rounds = 1;
	q.options.maxModels = 8;
	OK(J::Solve(q, Read(), before));
	D::Scene scene;
	scene.map = rig.scene.map.sha256;
	D::PlacementCapture sink(scene, {"specified-relative-integral"}, 32);
	q.diagnostics = &sink;
	OK(J::Solve(q, Read(), after));
	CHECK(before.work.forwardCalls == after.work.forwardCalls &&
		  before.work.rays == after.work.rays);
	CHECK(before.state == after.state && before.alternatives.size() == after.alternatives.size());
	for (size_t j = 0; j < before.alternatives.size(); ++j)
		CHECK(before.alternatives[j].key == after.alternatives[j].key);
	CHECK(!scene.records.empty());
	size_t rejected = 0;
	for (auto &r : scene.records)
		rejected += r.state == D::State::Rejected;
	CHECK(rejected > 0);
	auto count = scene.records.size();
	OK(D::TrainingPairs(q, {c}, 1, 4, Read(), scene));
	CHECK(scene.records.size() > count);
	auto invalid = q;
	auto rows = std::make_shared<Stream<J::Observation>>();
	auto one = rig.train->data->front();
	one.role = J::Role::Heldout;
	rows->data->push_back(one);
	invalid.training = rows;
	invalid.count = 1;
	CHECK(D::TrainingPairs(invalid, {c}, 1, 1, Read(), scene).code == Code::InvalidInput);
	std::cout << "OBSERVER rays=" << after.work.rays << " forward=" << after.work.forwardCalls
			  << " receipts=" << scene.records.size() << " actual_rejected_records=" << rejected
			  << " solver_identical=true\n";
}
void FrozenLedger()
{
	using namespace ProducerTest;
	Rig rig;
	// This owned analytic fixture explicitly defines a linear Rec.709 basis.
	// Unknown stock primaries would correctly leave interval comparison unavailable.
	rig.request.profiles[0].model.linearRec709PrimariesAttested = true;
	auto candidate = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {candidate};
	rig.Observations(rig.Truth({Answer(candidate, V(2, 1, .5))}));
	Validation validation(rig);
	Final final(rig);
	P::Result produced;
	OK(P::Run(Request(rig), Read(), produced, &validation, &final));
	CHECK(bool(produced.artifact.asset));
	if (!produced.artifact.asset)
		throw std::runtime_error(produced.reason);
	D::Scene scene;
	OK(D::FrozenObservations(produced.artifact, Read(), scene, 8));
	size_t totals = 0, errors = 0;
	for (auto &r : scene.records)
	{
		totals += r.quantity == "frozen-model-total-in-observation-domain";
		errors += r.layer == D::Layer::Residual && r.valueKnown;
	}
	CHECK(totals == 8 && errors == 8);
	CHECK(scene.omitted > 0);
	OK(D::Contributions(produced.artifact, Read(), scene, 8));
	size_t roots = 0;
	for (auto &r : scene.records)
		roots += r.reason.find("source-root=") != std::string::npos;
	CHECK(roots > 0);
	auto corrupt = produced.artifact;
	corrupt.chunks[0].bytes[0] ^= 1;
	CHECK(!D::FrozenObservations(corrupt, Read(), scene, 8));
	std::cout << "FROZEN_LEDGER observations=" << totals << " interval_discrepancies=" << errors
			  << " roots=" << roots << " corrupted_payload_rejected=true\n";
}
int main(int argc, char **argv)
{
	try
	{
		Interchange(argc > 1 ? argv[1] : "");
		ProbeAndAngular();
		Observer();
		FrozenLedger();
	}
	catch (const std::exception &e)
	{
		++failures;
		std::cerr << e.what() << '\n';
	}
	std::cout << checks << " checks " << failures << " failures\n";
	return failures ? 1 : 0;
}
