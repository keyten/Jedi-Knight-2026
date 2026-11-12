// End-to-end field production and Snapshot queries. No game or licensed assets.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/contributions.h"
#include "lightrecon/field_builder.h"
#include "lightrecon/photometry.h"
#include "probe_fixtures.h"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace ProbeFixture;
namespace F = Rend2LightRecon::Core::Fields;
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
Estimate Known(float x)
{
	Estimate e;
	e.known = true;
	e.value = x;
	return e;
}
ReconstructedLightingAsset Asset()
{
	ReconstructedLightingAsset a;
	a.lineage = "owned-fields";
	a.provenance = 1;
	a.units.worldUnitsPerMeter = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "field-builder-test/1";
	a.provenanceRecords.push_back(p);
	Calibration c;
	c.id = 1;
	c.provenance = 1;
	c.kind = CalibrationKind::PhysicallyAuthored;
	a.calibrations.push_back(c);
	c.id = 2;
	c.kind = CalibrationKind::UncalibratedRelative;
	c.method = "known synthetic relative scale";
	a.calibrations.push_back(c);
	return a;
}
PhysicalEmitter Emitter(Id id, EmitterKind kind, Vec3 p, Vec3 rgb)
{
	PhysicalEmitter e;
	e.source.id = id;
	e.source.provenance = 1;
	e.source.state = HypothesisState::Resolved;
	e.source.evidence.exactLight = Suitability::Unknown;
	e.kind = kind;
	e.position.known = kind != EmitterKind::Directional;
	e.position.world = p;
	e.frame.known = true;
	e.geometry.known = true;
	e.geometry.halfWidthWorld = .8;
	e.geometry.halfHeightWorld = .6;
	e.geometry.halfLengthWorld = 1.2;
	e.geometry.radiusWorld = .3;
	e.angular.kind = kind == EmitterKind::Point
						 ? AngularKind::Isotropic
						 : kind == EmitterKind::Spot
							   ? AngularKind::SmoothCone
							   : kind == EmitterKind::Directional ? AngularKind::Parallel
																  : AngularKind::Lambertian;
	e.angular.cosInner = .95f;
	e.angular.cosOuter = .8f;
	auto photo = Photometric::RGBToPhotometry(
		rgb,
		kind == EmitterKind::Point || kind == EmitterKind::Spot
			? PhotometricUnit::Candela
			: kind == EmitterKind::Directional ? PhotometricUnit::Lux : PhotometricUnit::Nits,
		1);
	e.emission = photo.value;
	return e;
}
struct Rig
{
	Scene scene;
	Probes::Input input;
	std::shared_ptr<const Probes::Topology> topology;
	ReconstructedLightingAsset asset = Asset();
	Rig()
	{
		scene.domain->world = scene.options.bounds = Box(Vec3(-2, -2, 0), Vec3(2, 2, 4));
		scene.options.coarseSpacingWorld = 2;
		scene.options.minimumSpacingWorld = 1;
		input = scene.Input();
		topology = scene.Build(input);
	}
	void Rebuild(bool regions = false)
	{
		input = scene.Input(regions);
		topology = scene.Build(input);
		if (regions)
			for (const auto &region : input.regions->Regions())
			{
				GeometryRegion record;
				record.id = region.id;
				record.provenance = asset.provenance;
				record.key = "owned-region/" + std::to_string(region.id);
				record.bounds = region.bounds;
				asset.regions.push_back(record);
			}
	}
	F::Request Request()
	{
		F::Request q;
		q.asset = std::make_shared<const ReconstructedLightingAsset>(asset);
		q.topology = topology;
		q.visibility = input.visibility;
		q.selection.field = 1000;
		q.selection.dataset = 1001;
		q.selection.geometryDataset = 1002;
		q.selection.topologyDataset = 1003;
		q.selection.provenance = 1;
		q.selection.calibration = 1;
		q.selection.provenanceText = "owned known sources";
		q.options.angularOrder = 8;
		q.options.referenceOrder = 12;
		q.options.degree = 4;
		q.options.sourceSampling.areaDivisions = 4;
		q.options.sourceSampling.tubeSides = 12;
		q.options.sourceSampling.tubeSegments = 4;
		return q;
	}
};
F::Artifact Build(const F::Request &q)
{
	F::Artifact a;
	auto e = F::Build(q, ReadContext(), a);
	if (!e)
		std::cerr << "BUILD " << unsigned(e.code) << ' ' << e.message << '\n';
	Check(e);
	return a;
}
ConditionalFieldSample Sample(const F::Artifact &a, Vec3 position, Vec3 n = Vec3(0, 0, 1))
{
	std::shared_ptr<const F::Prepared> p;
	Check(F::Prepare(a, ReadContext(), p));
	ConditionalFieldQuery q;
	q.optIn = true;
	q.field.id = a.descriptor->field;
	q.configuration = a.descriptor->configuration;
	q.quantity = a.descriptor->quantity;
	q.positionWorld = position;
	q.directionOrNormalWorld = n;
	ConditionalFieldSample value;
	auto s = p->ConditionalField(a.asset, q, value);
	if (s != Status::Ok)
		std::cerr << "QUERY " << unsigned(s) << '\n';
	CHECK(s == Status::Ok);
	if (s != Status::Ok)
		throw std::runtime_error("Missing test field support");
	return value;
}
struct Cancel : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
class PassiveRows : public ChunkSource<Indirect::Observation>
{
  public:
	std::shared_ptr<std::vector<Indirect::Observation>> rows =
		std::make_shared<std::vector<Indirect::Observation>>();
	Error Read(uint64_t first, size_t count, const ReadContext &,
			   Chunk<Indirect::Observation> &out) const override
	{
		if (first > rows->size() || count > rows->size() - first)
			return Failure(Code::InvalidInput, "Truncated passive fixture");
		out = VectorChunk<Indirect::Observation>(rows, size_t(first), count);
		return {};
	}
};
F::Passive Passive(const Rig &r)
{
	ReadContext read;
	read.maxBytes = 128ull << 20;
	Indirect::BuildInput input;
	input.mapKey = r.topology->MapKey();
	input.geometry = r.input.visibility;
	input.completePatchSet = true;
	input.patchInterpretationKey[0] = 37;
	Indirect::Patch patch;
	patch.id = 1;
	patch.material = 1;
	patch.areaWorldSquared = 4;
	patch.receiver.sample = 1;
	patch.receiver.surface = 1;
	patch.receiver.material = 1;
	patch.receiver.world = Vec3(0, 0, 4);
	patch.receiver.geometricNormal = patch.receiver.shadingNormal = Vec3(0, 0, -1);
	patch.receiver.mappedPlanar = patch.receiver.compilerPlacementCertified = true;
	auto tris = std::make_shared<std::vector<Bake::FootprintTriangle>>(2);
	(*tris)[0].world[0] = Vec3(-1, -1, 4);
	(*tris)[0].world[1] = Vec3(1, 1, 4);
	(*tris)[0].world[2] = Vec3(1, -1, 4);
	(*tris)[1].world[0] = Vec3(-1, -1, 4);
	(*tris)[1].world[1] = Vec3(-1, 1, 4);
	(*tris)[1].world[2] = Vec3(1, 1, 4);
	for (auto &t : *tris)
		for (auto &n : t.normal)
			n = Vec3(0, 0, -1);
	patch.receiver.footprint = VectorChunk<Bake::FootprintTriangle>(tris, 0, 2);
	input.patches = {patch};
	F::Passive p;
	p.sourceScene = F::SourceIdentity(r.asset);
	Check(Indirect::Transfer::Build(input, Indirect::BuildOptions(), read, p.transfer));
	Indirect::Request request;
	request.transfer = p.transfer;
	Indirect::Root root;
	root.source = 10;
	root.kind = TransportRootKind::LocalEmitter;
	root.domain = Indirect::Root::Domain::PhysicalLux;
	root.calibration = 1;
	root.incident = {Vec3(100, 100, 100)};
	root.available = {1};
	root.generation[0] = 13;
	root.provenance = "authored fixed patch-average root irradiance";
	request.roots = {root};
	Indirect::Reflectance material;
	material.material = 1;
	material.knowledge = Indirect::MaterialKnowledge::Known;
	material.lower = material.upper = material.initial = Vec3(.8f, .05f, .05f);
	material.provenance = "authored red diffuse reflector";
	request.materials = {material};
	request.contract.operation = Indirect::ObservationOperator::IncidentLinear;
	request.contract.incidentDomain = Indirect::Root::Domain::PhysicalLux;
	request.contract.unit = PhotometricUnit::Lux;
	request.contract.calibration = r.asset.calibrations[0];
	request.contract.units = r.asset.units;
	request.contract.transferAttested = request.contract.primariesAttested =
		request.contract.physicalTransportAssumed = true;
	request.contract.provenance = "one-bounce authored Lambertian patch model";
	auto stream = std::make_shared<PassiveRows>();
	Indirect::Observation o;
	o.id = 1;
	o.patch = 1;
	o.spatialBlock = 1;
	o.areaWorldSquared = 4;
	for (auto &channel : o.channels)
		channel.lower = channel.upper = 100;
	stream->rows->push_back(o);
	request.training = stream;
	request.count = 1;
	request.trainingKey[0] = 15;
	auto result = std::make_shared<Indirect::Result>();
	Check(Indirect::Fit(request, read, *result));
	p.solution = result;
	return p;
}
double DenseRectangle(const Rig &r, const PhysicalEmitter &e, Vec3 p, unsigned n)
{
	ReadContext read;
	read.maxBytes = 128ull << 20;
	double sum = 0;
	auto emission = Photometric::Emission(r.asset, e);
	for (unsigned i = 0; i < n; ++i)
		for (unsigned j = 0; j < n; ++j)
		{
			Bake::Detail::V source = Bake::Detail::V(e.position.world) +
									 Bake::Detail::V(e.frame.right) *
										 (2 * e.geometry.halfWidthWorld * ((i + .5) / n - .5)) +
									 Bake::Detail::V(e.frame.up) *
										 (2 * e.geometry.halfHeightWorld * ((j + .5) / n - .5));
			auto delta = source - Bake::Detail::V(p);
			double d2 = Bake::Detail::Dot(delta, delta);
			auto w = Bake::Detail::Unit(delta);
			RayQuery ray;
			ray.startWorld = p;
			ray.endWorld = source.Float();
			ray.policy.facing = FacingPolicy::TwoSided;
			ray.bias.absoluteWorld = ray.bias.relative = 0;
			RayResult hit;
			Check(r.input.visibility->Query(ray, read, hit));
			if (hit.state == VisibilityState::Visible)
				sum += emission.value.rgb.x * std::max(0., w.z) *
					   std::max(0., -Bake::Detail::Dot(Bake::Detail::V(e.frame.forward), w)) / d2 *
					   4 * e.geometry.halfWidthWorld * e.geometry.halfHeightWorld / (n * n);
		}
	return sum;
}
int main(int argc, char **argv)
{
	try
	{
		Rig rig;
		rig.asset.emitters.push_back(
			Emitter(10, EmitterKind::Point, Vec3(0, 0, 6), Vec3(100, 40, 10)));
		auto q = rig.Request();
		q.selection.directLocal = true;
		q.selection.allowDirectProjection = true;
		q.selection.sources = {10};
		auto l2q = q;
		l2q.options.degree = 2;
		auto l2point = Build(l2q);
		CHECK(l2point.work.available == 0);
		auto point = Build(q);
		CHECK(point.work.available == point.work.probes && point.work.available > 0);
		auto value = Sample(point, Vec3(-1, -1, 1));
		double d2 = 27, truth = 100 * 5 / std::pow(d2, 1.5);
		CHECK(std::abs(value.value.x - truth) / truth < .12);
		CHECK(value.descriptor->membership.directLocal == FieldMembership::Included &&
			  value.descriptor->membership.reflected == FieldMembership::Excluded);
		CHECK(point.asset.emitters[0].emission.intensity.value ==
			  rig.asset.emitters[0].emission.intensity.value);
		std::shared_ptr<const F::Prepared> p;
		CHECK(F::Prepare(point, ReadContext(), p));
		Manager manager;
		CHECK(manager.Publish(point.asset, Completeness::Partial, p) == Status::Ok);
		ConditionalFieldQuery cq;
		cq.optIn = true;
		cq.field = manager.Acquire().IdentifySource(1000).value;
		cq.positionWorld = Vec3(-1, -1, 1);
		auto publicValue = manager.Acquire().SampleConditionalField(cq);
		CHECK(publicValue && publicValue.value.generation == manager.Acquire().Generation());
		cq.optIn = false;
		CHECK(manager.Acquire().SampleConditionalField(cq).status == Status::InvalidArgument);
		cq.optIn = true;
		auto old = manager.Acquire();
		manager.Reset();
		CHECK(old.SampleConditionalField(cq));
		CHECK(manager.Acquire().SampleConditionalField(cq).status != Status::Ok);
		F::Artifact decoded;
		CHECK(F::Decode(point.asset, point.chunks, 1000, rig.topology->GeometryKey(), ReadContext(),
						decoded));
		auto after = Sample(decoded, Vec3(-1, -1, 1));
		CHECK(std::abs(after.value.x - value.value.x) < 1e-5);
		auto corrupt = point.chunks;
		corrupt.back().bytes[25] ^= 1;
		CHECK(!F::Decode(point.asset, corrupt, 1000, rig.topology->GeometryKey(), ReadContext(),
						 decoded));
		auto missing = point.chunks;
		missing.pop_back();
		CHECK(!F::Decode(point.asset, missing, 1000, rig.topology->GeometryKey(), ReadContext(),
						 decoded));
		auto mismatch = point.asset;
		for (auto &x : mismatch.datasets)
			if (x.id == 1001)
				x.encodingVersion = 77;
		CHECK(!F::Decode(mismatch, point.chunks, 1000, rig.topology->GeometryKey(), ReadContext(),
						 decoded));
		auto noDirect = q;
		noDirect.selection.allowDirectProjection = false;
		F::Artifact failed;
		CHECK(F::Build(noDirect, ReadContext(), failed).code == Code::Unavailable);
		auto incoming = q;
		incoming.selection.quantity = ConditionalQuantity::IncomingRadiance;
		CHECK(F::Build(incoming, ReadContext(), failed).code == Code::Unavailable);
		rig.asset.emitters.push_back(
			Emitter(11, EmitterKind::Point, Vec3(2, 1, 6), Vec3(2, 10, 100)));
		auto two = rig.Request();
		two.selection = q.selection;
		two.selection.sources = {10, 11};
		auto colours = Build(two);
		auto colour = Sample(colours, Vec3(-1, -1, 1));
		CHECK(colour.value.z > value.value.z && colour.value.y > value.value.y);
		CHECK(colour.descriptor->membership.roots.size() == 2);
		double shapeErrors[3] = {};
		unsigned shapeAt = 0;
		for (auto kind : {EmitterKind::Spot, EmitterKind::Rectangle, EmitterKind::Tube})
		{
			Rig r;
			auto e = Emitter(20, kind, Vec3(0, 0, 6), Vec3(100, 70, 30));
			e.frame.right = Vec3(1, 0, 0);
			e.frame.up = Vec3(0, -1, 0);
			e.frame.forward = Vec3(0, 0, -1);
			r.asset.emitters = {e};
			auto request = r.Request();
			request.selection.directLocal = true;
			request.selection.allowDirectProjection = true;
			request.selection.sources = {20};
			auto a = Build(request);
			CHECK(a.work.available > 0);
			auto y = Sample(a, Vec3(-1, -1, 1));
			Photometric::Receiver receiver;
			receiver.positionWorld = Vec3(-1, -1, 1);
			receiver.normalWorld = Vec3(0, 0, 1);
			auto reference =
				kind == EmitterKind::Spot
					? Photometric::PointReceiverIrradiance(
						  r.asset, e, receiver, Photometric::PointModel::IdealInverseSquare)
					: Photometric::AreaReceiverIrradiance(r.asset, e, receiver, 64);
			CHECK(reference);
			double relative = std::abs(y.value.x - reference.value.rgb.x) / reference.value.rgb.x;
			shapeErrors[shapeAt++] = relative;
			CHECK(relative < .12);
		}
		// Physical sky must reach retained sky geometry, never a no-hit-to-infinity
		// fallback.
		Rig sky;
		auto b = sky.scene.domain->world;
		sky.scene.Quad(Vec3(-2, -2, 4), Vec3(2, -2, 4), Vec3(2, 2, 4), Vec3(-2, 2, 4), MaterialSky);
		sky.scene.Quad(Vec3(-2, -2, 0), Vec3(-2, 2, 0), Vec3(2, 2, 0), Vec3(2, -2, 0), MaterialSky);
		sky.scene.Quad(Vec3(-2, -2, 0), Vec3(-2, -2, 4), Vec3(-2, 2, 4), Vec3(-2, 2, 0),
					   MaterialSky);
		sky.scene.Quad(Vec3(2, -2, 0), Vec3(2, 2, 0), Vec3(2, 2, 4), Vec3(2, -2, 4), MaterialSky);
		sky.scene.Quad(Vec3(-2, -2, 0), Vec3(2, -2, 0), Vec3(2, -2, 4), Vec3(-2, -2, 4),
					   MaterialSky);
		sky.scene.Quad(Vec3(-2, 2, 0), Vec3(-2, 2, 4), Vec3(2, 2, 4), Vec3(2, 2, 0), MaterialSky);
		sky.Rebuild();
		EnvironmentLighting env;
		env.source.id = 30;
		env.source.provenance = 1;
		env.source.state = HypothesisState::Resolved;
		env.meanLuminance.calibration = 1;
		env.meanLuminance.unit = PhotometricUnit::Nits;
		env.radiance = 31;
		env.sunCoverage = SunCoverage::ExcludesExplicitSun;
		sky.asset.environments.push_back(env);
		Dataset ed;
		ed.id = 31;
		ed.purpose = DatasetPurpose::Environment;
		ed.layout = DatasetLayout::Sparse;
		ed.encoding = "org.openjk.angular";
		ed.locator = "owned://sky";
		sky.asset.datasets.push_back(ed);
		F::Environment angular;
		angular.source = 30;
		angular.dataset = 31;
		angular.content[0] = 8;
		angular.incoming.present = true;
		angular.incoming.degree = 0;
		angular.incoming.sh.push_back(
			Angular::V{{std::sqrt(4 * 3.141592653589793), std::sqrt(4 * 3.141592653589793),
						std::sqrt(4 * 3.141592653589793)}});
		std::vector<uint8_t> skyBytes;
		CHECK(Angular::Encode(angular.incoming, Angular::Storage::Float32, skyBytes) ==
			  Angular::Code::Ok);
		angular.content = Disk::Hash(skyBytes.data(), skyBytes.size());
		sky.asset.datasets.back().contentHash = Disk::Hex(angular.content);
		auto sq = sky.Request();
		sq.selection.sky = true;
		sq.selection.sources = {30};
		sq.environments = {angular};
		sq.options.maximumRelativeAngularRMSE = .2;
		auto skyE = Build(sq);
		CHECK(skyE.work.available > 0);
		auto sk = Sample(skyE, Vec3(-1, -1, 1));
		CHECK(std::abs(sk.value.x - 3.141592653589793) < .025);
		auto sr = sq;
		sr.selection.quantity = ConditionalQuantity::IncomingRadiance;
		sr.options.maximumRelativeAngularRMSE = .6;
		auto skyL = Build(sr);
		CHECK(skyL.work.available > 0);
		CHECK(F::Prepare(skyL, ReadContext(), p));
		CHECK(manager.Publish(skyL.asset, Completeness::Partial, p) == Status::Ok);
		RadianceQuery rq;
		rq.positionWorld = Vec3(-1, -1, 1);
		rq.directionToSource = Vec3(0, 0, 1);
		auto light = manager.Acquire().SampleIncomingRadiance(rq);
		CHECK(light && light.value.unit == PhotometricUnit::Nits);
		auto missingSky = sq;
		missingSky.visibility = rig.input.visibility;
		missingSky.topology = rig.topology;
		auto unlit = Build(missingSky);
		CHECK(unlit.work.available == 0);
		CHECK(F::Prepare(unlit, ReadContext(), p));
		CHECK(p->Supports(Capability::IncomingRadianceSampling) == Status::Unavailable);
		auto sun = Emitter(40, EmitterKind::Directional, Vec3(), Vec3(20, 20, 20));
		sun.frame.right = Vec3(1, 0, 0);
		sun.frame.up = Vec3(0, -1, 0);
		sun.frame.forward = Vec3(0, 0, -1);
		sky.asset.emitters.push_back(sun);
		SunSource ss;
		ss.emitter = 40;
		sky.asset.suns.push_back(ss);
		auto sunq = sky.Request();
		sunq.selection.sky = sunq.selection.sun = true;
		sunq.selection.allowDirectProjection = true;
		sunq.selection.sources = {30, 40};
		sunq.environments = {angular};
		sunq.options.maximumRelativeAngularRMSE = .2;
		auto sunSky = Build(sunq);
		CHECK(sunSky.work.available > 0);
		CHECK(sunSky.descriptor->membership.directSun == FieldMembership::Included &&
			  sunSky.descriptor->membership.directSky == FieldMembership::Included);
		// Explicitly approximate encoded appearance. No physical capability or root
		// separation.
		Rig appearance;
		appearance.scene.Solid(Box(Vec3(-.05f, -2, 0), Vec3(.05f, 2, 4)));
		appearance.Rebuild();
		auto aq = appearance.Request();
		aq.selection.route = F::Route::ObservationAppearance;
		aq.selection.domain = FieldDomain::LegacyAppearance;
		aq.selection.calibration = 2;
		aq.selection.quantity = ConditionalQuantity::LegacyAppearance;
		aq.selection.colour = ContributionColour::EncodedSourceRGB;
		aq.selection.primariesKnown = aq.selection.linearTransferKnown = false;
		aq.selection.appearanceDomain = "historical-encoded-rgb8/no-physical-claim";
		aq.appearance = {{1, Vec3(-1, -1, 1), Vec3(0, 0, 0), 1},
						 {2, Vec3(1, -1, 1), Vec3(200, 80, 20), 1}};
		aq.appearanceValidation = {{101, Vec3(-1.05f, -1, 1), Vec3(0, 0, 0), 1},
								   {102, Vec3(1.05f, -1, 1), Vec3(200, 80, 20), 1}};
		auto app = Build(aq);
		CHECK(app.work.available > 0);
		auto dark = Sample(app, Vec3(-1, -1, 1));
		CHECK(dark.value.x == 0);
		auto bright = Sample(app, Vec3(1, -1, 1));
		CHECK(bright.value.x == 200);
		CHECK(F::Prepare(app, ReadContext(), p));
		CHECK(p->Supports(Capability::IndirectSampling) == Status::Unavailable &&
			  p->Supports(Capability::IncomingRadianceSampling) == Status::Unavailable);
		CHECK(manager.Publish(app.asset, Completeness::Partial, p) == Status::Ok);
		IrradianceQuery iq;
		iq.positionWorld = Vec3(-1, -1, 1);
		iq.normalWorld = Vec3(0, 0, 1);
		CHECK(manager.Acquire().SampleIndirect(iq).status == Status::Unavailable);
		CHECK(app.asset.transport.back().quantity == FieldQuantity::ApproximateAppearance);
		CHECK(app.descriptor->membership.addition == CompositionRule::Forbidden);
		CHECK(!app.descriptor->membership.rootsComplete &&
			  !app.descriptor->membership.reflectionOrderKnown);
		CHECK(app.work.appearanceValidationRequested == 2 &&
			  app.work.appearanceValidationAvailable == 2);
		// X18: sparse independent support has an explicit denominator. Missing is
		// never filled with black; the default still requires complete validation.
		auto sparseAppearance = aq;
		sparseAppearance.appearanceValidation.push_back(
			{103, Vec3(100, 100, 100), Vec3(10, 10, 10), 1});
		F::Artifact sparseResult;
		CHECK(F::Build(sparseAppearance, ReadContext(), sparseResult).code == Code::Unavailable);
		CHECK(sparseResult.work.appearanceValidationRequested == 3 &&
			  sparseResult.work.appearanceValidationAvailable == 2 && sparseResult.work.rays > 0);
		CHECK(!sparseResult.descriptor && sparseResult.values.records.empty() &&
			  sparseResult.asset.transport.empty());
		sparseAppearance.options.appearanceMinimumValidationFraction = .5;
		CHECK(F::Build(sparseAppearance, ReadContext(), sparseResult));
		CHECK(sparseResult.work.appearanceValidationRequested == 3 &&
			  sparseResult.work.appearanceValidationAvailable == 2);
		auto reorderedAppearance = aq;
		std::reverse(reorderedAppearance.appearance.begin(), reorderedAppearance.appearance.end());
		std::reverse(reorderedAppearance.appearanceValidation.begin(),
					 reorderedAppearance.appearanceValidation.end());
		auto reorderedApp = Build(reorderedAppearance);
		CHECK(reorderedApp.chunks.size() == app.chunks.size());
		for (size_t j = 0; j < app.chunks.size(); ++j)
			CHECK(reorderedApp.chunks[j].bytes == app.chunks[j].bytes);
		F::Artifact loadedAppearance;
		CHECK(F::Decode(app.asset, app.chunks, 1000, appearance.topology->GeometryKey(),
						ReadContext(), loadedAppearance));
		auto loadedBright = Sample(loadedAppearance, Vec3(1, -1, 1));
		CHECK(loadedBright.value.x == bright.value.x &&
			  loadedBright.descriptor->quantity == ConditionalQuantity::LegacyAppearance &&
			  loadedBright.descriptor->unit == PhotometricUnit::Relative);
		auto allAbsent = sparseAppearance;
		allAbsent.appearanceValidation.erase(allAbsent.appearanceValidation.begin(),
											 allAbsent.appearanceValidation.begin() + 2);
		CHECK(F::Build(allAbsent, ReadContext(), sparseResult).code == Code::Unavailable);
		auto unsupportedRoom = aq;
		unsupportedRoom.appearance.erase(unsupportedRoom.appearance.begin() + 1);
		unsupportedRoom.appearanceValidation.erase(unsupportedRoom.appearanceValidation.begin() + 1);
		auto leftOnly = Build(unsupportedRoom);
		CHECK(F::Prepare(leftOnly, ReadContext(), p));
		ConditionalFieldQuery noSupport;
		noSupport.optIn = true;
		noSupport.field.id = 1000;
		noSupport.quantity = ConditionalQuantity::LegacyAppearance;
		noSupport.positionWorld = Vec3(1, -1, 1);
		noSupport.directionOrNormalWorld = Vec3(0, 0, 1);
		ConditionalFieldSample noValue;
		CHECK(p->ConditionalField(leftOnly.asset, noSupport, noValue) == Status::Unavailable);
		CHECK(Sample(app, Vec3(1, -1, 1), Vec3(0, 0, -1)).value.x == bright.value.x);
		Attribution::Component appearanceComponent;
		appearanceComponent.field = true;
		appearanceComponent.dataset = 1001;
		CHECK(Attribution::CanAdd(app.asset, appearanceComponent, appearanceComponent).status ==
			  Status::Unsupported);
		Rig gi;
		gi.scene.Quad(Vec3(-1, -1, 4), Vec3(-1, 1, 4), Vec3(1, 1, 4), Vec3(1, -1, 4));
		gi.Rebuild(true);
		gi.asset.emitters.push_back(
			Emitter(10, EmitterKind::Point, Vec3(0, 0, 2), Vec3(400, 400, 400)));
		auto gq = gi.Request();
		gq.selection.firstBounce = true;
		gq.selection.bounceRoots = {10};
		gq.passive = Passive(gi);
		auto reflected = Build(gq);
		auto stalePassive = gq;
		stalePassive.passive.sourceScene[0] ^= 1;
		CHECK(F::Build(stalePassive, ReadContext(), failed).code == Code::Unavailable);
		CHECK(reflected.work.available > 0);
		auto red = Sample(reflected, Vec3(-1, -1, 1));
		CHECK(red.value.x > red.value.y * 15.9f && red.value.x < red.value.y * 16.1f);
		CHECK(reflected.descriptor->membership.reflectionOrders == std::vector<uint32_t>{1});
		CHECK(reflected.descriptor->membership.directLocal == FieldMembership::Excluded);
		CHECK(F::Prepare(reflected, ReadContext(), p));
		CHECK(manager.Publish(reflected.asset, Completeness::Partial, p) == Status::Ok);
		iq.positionWorld = Vec3(-1, -1, 1);
		auto physicalGI = manager.Acquire().SampleIndirect(iq);
		CHECK(physicalGI && physicalGI.value.unit == PhotometricUnit::Lux);
		CHECK(manager.Acquire().SampleIncomingRadiance(rq).status == Status::Unavailable);
		CHECK(manager.Acquire().SampleReflectedIrradianceSH(iq.positionWorld).status == Status::Unavailable);
		auto gpuRequest = gq;
		gpuRequest.options.degree = 2;
		gpuRequest.options.maximumRelativeAngularRMSE = .25;
		auto gpuField = Build(gpuRequest);
		std::shared_ptr<const F::Prepared> gpuEvaluator;
		Check(F::Prepare(gpuField, ReadContext(), gpuEvaluator));
		Manager gpuManager;
		CHECK(gpuManager.Publish(gpuField.asset, Completeness::Partial, gpuEvaluator) == Status::Ok);
		const auto gpu = gpuManager.Acquire().SampleReflectedIrradianceSH(iq.positionWorld);
		CHECK(gpu && gpu.value.degree == 2 && gpu.value.region && gpu.value.cell);
		const auto scalar = gpuManager.Acquire().SampleIndirect(iq);
		double basis[9]{}, rgb[3]{};
		CHECK(Angular::BasisInto({{iq.normalWorld.x, iq.normalWorld.y, iq.normalWorld.z}}, 2, basis, 9) ==
			Angular::Code::Ok);
		for (int k = 0; k < 9; ++k)
		{
			rgb[0] += basis[k] * gpu.value.coefficients[k].x;
			rgb[1] += basis[k] * gpu.value.coefficients[k].y;
			rgb[2] += basis[k] * gpu.value.coefficients[k].z;
		}
		CHECK(scalar && std::abs(rgb[0] - scalar.value.rgb.x) < 1e-5 &&
			std::abs(rgb[1] - scalar.value.rgb.y) < 1e-5 && std::abs(rgb[2] - scalar.value.rgb.z) < 1e-5);
		auto inadmissible = gpuField.asset.transport.back();
		CHECK(F::SupportsReflectedIrradiance(gpuField.asset, inadmissible));
		inadmissible.membership.directLocal = FieldMembership::Included;
		CHECK(!F::SupportsReflectedIrradiance(gpuField.asset, inadmissible));
		inadmissible = gpuField.asset.transport.back();
		inadmissible.membership.directSun = FieldMembership::Unknown;
		CHECK(!F::SupportsReflectedIrradiance(gpuField.asset, inadmissible));
		inadmissible = gpuField.asset.transport.back();
		inadmissible.membership.configuration = 100;
		CHECK(!F::SupportsReflectedIrradiance(gpuField.asset, inadmissible));
		inadmissible = gpuField.asset.transport.back();
		inadmissible.membership.profile = "unknown";
		CHECK(!F::SupportsReflectedIrradiance(gpuField.asset, inadmissible));
		inadmissible = gpuField.asset.transport.back();
		inadmissible.source.hypothesisGroup = 50;
		CHECK(!F::SupportsReflectedIrradiance(gpuField.asset, inadmissible));
		inadmissible = gpuField.asset.transport.back();
		inadmissible.unit = PhotometricUnit::Relative;
		CHECK(!F::SupportsReflectedIrradiance(gpuField.asset, inadmissible));
		CHECK(gpuManager.Acquire().SampleReflectedIrradianceSH(Vec3(100, 100, 100)).status == Status::Unavailable);
		// Independent positions and dense area quadrature for the same rho*E0/pi
		// patch.
		auto reflector = Emitter(99, EmitterKind::Rectangle, Vec3(0, 0, 4),
								 Vec3(float(80 / 3.141592653589793), float(5 / 3.141592653589793),
									  float(5 / 3.141592653589793)));
		reflector.geometry.halfWidthWorld = reflector.geometry.halfHeightWorld = 1;
		reflector.frame.right = Vec3(1, 0, 0);
		reflector.frame.up = Vec3(0, -1, 0);
		reflector.frame.forward = Vec3(0, 0, -1);
		double bounceSE = 0, bounceReference = 0;
		unsigned bounceUsed = 0, bounceMissing = 0;
		for (unsigned j = 0; j < 30; ++j)
		{
			iq.positionWorld = Vec3(-1.5f + float(j % 6) * .6f, -1.4f + float(j / 6) * .7f, 1.25f);
			auto predicted = manager.Acquire().SampleIndirect(iq);
			if (!predicted)
			{
				++bounceMissing;
				continue;
			}
			Photometric::Receiver receiver;
			receiver.positionWorld = iq.positionWorld;
			receiver.normalWorld = iq.normalWorld;
			auto reference = Photometric::AreaReceiverIrradiance(gi.asset, reflector, receiver, 64);
			CHECK(reference);
			bounceSE += std::pow(double(predicted.value.rgb.x) - reference.value.rgb.x, 2);
			bounceReference += std::pow(double(reference.value.rgb.x), 2);
			++bounceUsed;
		}
		CHECK(bounceUsed > 0 && bounceReference > 0);
		double bounceRelativeRMSE = std::sqrt(bounceSE / bounceReference);
		// Quality control: uniformly finer geometry-only topology, same sources,
		// passive solution, reference points and angular/integration tolerances.
		gi.scene.options.coarseSpacingWorld = gi.scene.options.minimumSpacingWorld = .5;
		gi.Rebuild();
		auto fineGIRequest = gi.Request();
		fineGIRequest.selection = gq.selection;
		fineGIRequest.passive = gq.passive;
		auto fineGI = Build(fineGIRequest);
		CHECK(F::Prepare(fineGI, ReadContext(), p));
		CHECK(manager.Publish(fineGI.asset, Completeness::Partial, p) == Status::Ok);
		double fineBounceSE = 0, fineBounceReference = 0;
		unsigned fineBounceUsed = 0, fineBounceMissing = 0;
		for (unsigned j = 0; j < 30; ++j)
		{
			iq.positionWorld = Vec3(-1.5f + float(j % 6) * .6f, -1.4f + float(j / 6) * .7f, 1.25f);
			auto predicted = manager.Acquire().SampleIndirect(iq);
			if (!predicted)
			{
				++fineBounceMissing;
				continue;
			}
			Photometric::Receiver receiver;
			receiver.positionWorld = iq.positionWorld;
			receiver.normalWorld = iq.normalWorld;
			auto reference = Photometric::AreaReceiverIrradiance(gi.asset, reflector, receiver, 64);
			CHECK(reference);
			fineBounceSE += std::pow(double(predicted.value.rgb.x) - reference.value.rgb.x, 2);
			fineBounceReference += std::pow(double(reference.value.rgb.x), 2);
			++fineBounceUsed;
		}
		CHECK(fineBounceUsed > 0 && fineBounceReference > 0);
		double fineBounceRMSE = std::sqrt(fineBounceSE / fineBounceReference);
		CHECK(fineBounceUsed >= bounceUsed && fineBounceRMSE < bounceRelativeRMSE);
		auto directGI = gi.Request();
		directGI.asset = std::make_shared<const ReconstructedLightingAsset>(reflected.asset);
		directGI.selection.directLocal = true;
		directGI.selection.allowDirectProjection = true;
		directGI.selection.sources = {10};
		directGI.selection.field = 2000;
		directGI.selection.dataset = 2001;
		directGI.selection.geometryDataset = 2002;
		directGI.selection.topologyDataset = 2003;
		auto hybrid = Build(directGI);
		Attribution::Component indirectComponent;
		indirectComponent.field = true;
		indirectComponent.dataset = 1001;
		Attribution::Component directComponent;
		directComponent.field = true;
		directComponent.dataset = 2001;
		CHECK(Attribution::CanAdd(hybrid.asset, indirectComponent, directComponent));
		CHECK(!Attribution::CanAdd(hybrid.asset, directComponent, directComponent));
		auto unknownMaterial = gq;
		auto unknownResult = std::make_shared<Indirect::Result>(*gq.passive.solution);
		unknownResult->materials[0].knowledge = Indirect::MaterialKnowledge::Unknown;
		unknownMaterial.passive.solution = unknownResult;
		CHECK(F::Build(unknownMaterial, ReadContext(), failed).code == Code::Unavailable);
		auto relative = rig.Request();
		relative.selection = q.selection;
		relative.selection.domain = FieldDomain::SourceLinearRelative;
		relative.selection.calibration = 2;
		auto ra = std::make_shared<ReconstructedLightingAsset>(*relative.asset);
		ra->emitters.resize(1);
		ra->emitters[0].source.evidence.exactLight = Suitability::Unknown;
		ra->emitters[0].emission.unit = PhotometricUnit::Relative;
		ra->emitters[0].emission.calibration = 2;
		relative.asset = ra;
		auto rel = Build(relative);
		CHECK(F::Prepare(rel, ReadContext(), p));
		CHECK(p->Supports(Capability::IndirectSampling) == Status::Unavailable);
		auto relSample = Sample(rel, Vec3(-1, -1, 1));
		CHECK(relSample.descriptor->unit == PhotometricUnit::Relative &&
			  relSample.descriptor->domain == FieldDomain::SourceLinearRelative);
		auto rejected = rig.Request();
		rejected.selection = q.selection;
		auto rejectAsset = std::make_shared<ReconstructedLightingAsset>(*rejected.asset);
		rejectAsset->emitters[0].source.state = HypothesisState::Rejected;
		rejectAsset->emitters[0].source.evidence.exactLight = Suitability::Unsuitable;
		rejected.asset = rejectAsset;
		CHECK(F::Build(rejected, ReadContext(), failed).code == Code::Unavailable);
		auto uncertain = q;
		auto uncertainAsset = std::make_shared<ReconstructedLightingAsset>(*q.asset);
		uncertainAsset->emitters[0].position.sigmaWorld = Known(.2f);
		uncertain.asset = uncertainAsset;
		auto uncertainField = Build(uncertain);
		CHECK(uncertainField.descriptor->uncertainty & AttributionParametersUnknown);
		CHECK(uncertainField.asset.transport.back().source.state == HypothesisState::Candidate);
		CHECK(uncertainField.descriptor->membership.addition == CompositionRule::Forbidden);
		CHECK(F::Prepare(uncertainField, ReadContext(), p));
		CHECK(p->Supports(Capability::IndirectSampling) == Status::Unavailable);
		CHECK(Sample(uncertainField, Vec3(-1, -1, 1)).outcome == FieldOutcome::Partial);
		Cancel cancel;
		ReadContext cancelled;
		cancelled.cancellation = &cancel;
		CHECK(F::Build(q, cancelled, failed).code == Code::Cancelled);
		auto limited = q;
		limited.options.maxRays = 1;
		CHECK(F::Build(limited, ReadContext(), failed).code == Code::WorkLimit);
		limited = q;
		limited.options.maxBytes = 128;
		CHECK(!F::Build(limited, ReadContext(), failed));
		auto largeMetadata = q;
		auto largeAsset = std::make_shared<ReconstructedLightingAsset>(*q.asset);
		largeAsset->provenanceRecords[0].method = std::string(70000, 'x');
		largeMetadata.asset = largeAsset;
		largeMetadata.options.maxBytes = 65536;
		CHECK(F::Build(largeMetadata, ReadContext(), failed).code == Code::MemoryLimit);
		auto nonfinite = q;
		nonfinite.options.absoluteErrorFloor = std::numeric_limits<double>::quiet_NaN();
		CHECK(F::Build(nonfinite, ReadContext(), failed).code == Code::InvalidInput);
		nonfinite = q;
		nonfinite.options.appearanceRadiusWorld = std::numeric_limits<double>::infinity();
		CHECK(F::Build(nonfinite, ReadContext(), failed).code == Code::InvalidInput);
		Rig sealed;
		sealed.scene.Solid(Box(Vec3(-.05f, -2, 0), Vec3(.05f, 2, 4)));
		sealed.Rebuild();
		sealed.asset.emitters.push_back(
			Emitter(70, EmitterKind::Point, Vec3(1, 0, 3), Vec3(100, 100, 100)));
		auto wallq = sealed.Request();
		wallq.selection.directLocal = wallq.selection.allowDirectProjection = true;
		wallq.selection.sources = {70};
		auto wallField = Build(wallq);
		unsigned wallTests = 0;
		for (const auto &probe : sealed.topology->Probes())
			if (probe.positionWorld.x < 0 &&
				std::any_of(wallField.values.records.begin(), wallField.values.records.end(),
							[&](const Probes::Value &v) { return v.probe == probe.id; }))
			{
				auto x = Sample(wallField, probe.positionWorld);
				CHECK(x.value.x == 0);
				++wallTests;
			}
		CHECK(wallTests > 0);
		Rig doorway;
		doorway.scene.Solid(Box(Vec3(-.05f, -2, 0), Vec3(.05f, -.4f, 4)));
		doorway.scene.Solid(Box(Vec3(-.05f, .4f, 0), Vec3(.05f, 2, 4)));
		doorway.Rebuild();
		doorway.asset.emitters = sealed.asset.emitters;
		auto doorq = doorway.Request();
		doorq.selection = wallq.selection;
		auto doorField = Build(doorq);
		unsigned positiveOpening = 0;
		for (const auto &probe : doorway.topology->Probes())
			if (probe.positionWorld.x < 0 && probe.positionWorld.z < 2.9f &&
				std::any_of(doorField.values.records.begin(), doorField.values.records.end(),
							[&](const Probes::Value &v) { return v.probe == probe.id; }))
			{
				auto x = Sample(doorField, probe.positionWorld);
				positiveOpening += x.value.x > 0;
			}
		CHECK(positiveOpening > 0);
		// Appearance support may pass a real opening without inventing a lamp or
		// claiming that the copied scalar proxy is directional incident radiance.
		Rig wideDoor;
		wideDoor.scene.Solid(Box(Vec3(-.05f, -2, 0), Vec3(.05f, -1.2f, 4)));
		wideDoor.scene.Solid(Box(Vec3(-.05f, 1.2f, 0), Vec3(.05f, 2, 4)));
		wideDoor.Rebuild();
		auto appearanceDoor = wideDoor.Request();
		appearanceDoor.selection = aq.selection;
		appearanceDoor.appearance = {{1, Vec3(-1, 0, 1), Vec3(120, 40, 10), 1}};
		appearanceDoor.appearanceValidation = {{2, Vec3(1, 0, 1), Vec3(120, 40, 10), 1}};
		auto appearanceOpening = Build(appearanceDoor);
		CHECK(Sample(appearanceOpening, Vec3(1, 0, 1)).value.x == 120);
		Rig partial;
		partial.scene.Solid(Box(Vec3(-.05f, -2, 2), Vec3(.05f, 2, 4)));
		partial.Rebuild();
		auto panel = Emitter(80, EmitterKind::Rectangle, Vec3(1, 0, 6), Vec3(100, 100, 100));
		panel.frame.right = Vec3(1, 0, 0);
		panel.frame.up = Vec3(0, -1, 0);
		panel.frame.forward = Vec3(0, 0, -1);
		partial.asset.emitters = {panel};
		auto pq = partial.Request();
		pq.selection.directLocal = pq.selection.allowDirectProjection = true;
		pq.selection.sources = {80};
		pq.options.sourceSampling.areaDivisions = 16;
		auto penumbra = Build(pq);
		bool partialCompared = false;
		double partialError = 0, partialFraction = 0;
		for (const auto &probe : partial.topology->Probes())
			if (!partialCompared && probe.positionWorld.x < -.1f && probe.positionWorld.z < 2 &&
				std::abs(probe.positionWorld.y) < 1.6 &&
				std::any_of(penumbra.values.records.begin(), penumbra.values.records.end(),
							[&](const Probes::Value &v) { return v.probe == probe.id; }))
			{
				double reference = DenseRectangle(partial, panel, probe.positionWorld, 64);
				Photometric::Receiver receiver;
				receiver.positionWorld = probe.positionWorld;
				receiver.normalWorld = Vec3(0, 0, 1);
				auto unblocked =
					Photometric::AreaReceiverIrradiance(partial.asset, panel, receiver, 64);
				if (reference > 0 && reference < .95 * unblocked.value.rgb.x)
				{
					auto y = Sample(penumbra, probe.positionWorld);
					partialError = std::abs(y.value.x - reference) / reference;
					partialFraction = reference / unblocked.value.rgb.x;
					CHECK(partialError < .12);
					partialCompared = true;
				}
			}
		CHECK(partialCompared);
		auto conditional = relative;
		auto conditionalAsset = std::make_shared<ReconstructedLightingAsset>(*relative.asset);
		conditionalAsset->emitters[0].source.state = HypothesisState::Candidate;
		conditionalAsset->emitters[0].source.hypothesisGroup = 55;
		HypothesisGroup group;
		group.id = 55;
		group.description = "exclusive controlled source configurations";
		conditionalAsset->hypothesisGroups.push_back(group);
		LightingConfiguration cfg;
		cfg.id = 50;
		cfg.provenance = 1;
		cfg.key = "owned/conditional";
		cfg.profile = "physical-emission/1";
		cfg.calibrationHypothesis = "source-linear relative, no absolute scale";
		JointParameterState parameters;
		auto &ce = conditionalAsset->emitters[0];
		parameters.track = ce.source.id;
		parameters.kind = ce.kind;
		parameters.position = ce.position;
		parameters.frame = ce.frame;
		parameters.dimensions = ce.geometry;
		parameters.angular = ce.angular;
		parameters.amplitude =
			Photometric::Emission(*relative.asset, relative.asset->emitters[0]).value.rgb;
		parameters.amplitudeDomain = "source-linear-physical-model/1";
		cfg.sources.push_back(parameters);
		conditionalAsset->lightingConfigurations.push_back(cfg);
		conditional.asset = conditionalAsset;
		conditional.selection.configuration = 50;
		conditional.selection.alternativeGroup = 55;
		CHECK(F::Build(conditional, ReadContext(), failed).code == Code::Unavailable);
		conditional.selection.allowConditionalSources = true;
		auto conditionalField = Build(conditional);
		CHECK(F::Prepare(conditionalField, ReadContext(), p));
		ConditionalFieldQuery selection;
		selection.optIn = true;
		selection.field.id = 1000;
		selection.positionWorld = Vec3(-1, -1, 1);
		ConditionalFieldSample selected;
		CHECK(p->ConditionalField(conditionalField.asset, selection, selected) ==
			  Status::Ambiguous);
		selection.configuration = 50;
		CHECK(p->ConditionalField(conditionalField.asset, selection, selected) == Status::Ok);
		CHECK(selected.descriptor->alternativeGroup == 55 &&
			  selected.descriptor->configuration == 50);
		selection.configuration = 51;
		CHECK(p->ConditionalField(conditionalField.asset, selection, selected) == Status::NotFound);
		// Held-out positions/normals: prediction coverage and error are separate.
		double spatialSE = 0, spatialReference = 0, blackSE = 0;
		unsigned heldoutUsed = 0, heldoutMissing = 0;
		CHECK(F::Prepare(point, ReadContext(), p));
		auto normals = Angular::Directions(30);
		for (unsigned i = 0; i < 60; ++i)
		{
			ConditionalFieldQuery h;
			h.optIn = true;
			h.field.id = 1000;
			h.positionWorld =
				Vec3(-.8f + 1.6f * (i % 10) / 9.f, -.8f + 1.6f * ((i / 10) % 6) / 5.f, 1.3f);
			for (auto n : normals)
			{
				h.directionOrNormalWorld = Vec3(float(n[0]), float(n[1]), float(n[2]));
				ConditionalFieldSample predicted;
				Photometric::Receiver receiver;
				receiver.positionWorld = h.positionWorld;
				receiver.normalWorld = h.directionOrNormalWorld;
				auto truth = Photometric::PointReceiverIrradiance(
					*q.asset, q.asset->emitters[0], receiver,
					Photometric::PointModel::IdealInverseSquare);
				CHECK(truth);
				blackSE += truth.value.rgb.x * truth.value.rgb.x;
				auto status = p->ConditionalField(point.asset, h, predicted);
				if (status != Status::Ok)
				{
					++heldoutMissing;
					continue;
				}
				spatialSE += std::pow(predicted.value.x - truth.value.rgb.x, 2);
				spatialReference += truth.value.rgb.x * truth.value.rgb.x;
				++heldoutUsed;
			}
		}
		double spatialRelativeRMSE = std::sqrt(spatialSE / spatialReference);
		CHECK(heldoutUsed > 0 && spatialSE < blackSE);
		// Canonical container persistence, not just an in-memory Angular
		// round-trip.
		Disk::Fingerprint fp;
		fp.algorithm = "org.openjk.field-builder-test";
		fp.algorithmVersion = 1;
		fp.units = point.asset.units;
		fp.complete.fill(true);
		for (unsigned k = 0; k < unsigned(Disk::InputKind::Count); ++k)
		{
			Disk::InputContent input;
			input.kind = Disk::InputKind(k);
			input.name = "owned/" + std::to_string(k);
			input.bytes = input.name.size();
			input.sha256 = Disk::Hash(input.name.data(), input.name.size());
			fp.inputs.push_back(input);
		}
		std::vector<uint8_t> archive;
		CHECK(Disk::Encode(point.asset, fp, point.chunks, archive));
		if (argc > 2)
		{
			std::ofstream file(argv[2], std::ios::binary);
			file.write(reinterpret_cast<const char *>(archive.data()), archive.size());
			CHECK(bool(file));
		}
		Disk::MemoryReader reader(archive.data(), archive.size());
		ReconstructedLightingAsset restored;
		Disk::Index index;
		CHECK(Disk::Open(reader, fp, restored, index));
		std::vector<Disk::Chunk> restoredChunks;
		for (auto &section : index.sections)
			if (section.type == unsigned(Disk::Section::Chunk))
			{
				Disk::Chunk c;
				c.dataset = section.dataset;
				c.firstElement = section.firstElement;
				c.count = section.count;
				CHECK(Disk::ReadChunk(reader, index, c.dataset, c.firstElement, c.bytes));
				restoredChunks.push_back(c);
			}
		CHECK(F::Decode(restored, restoredChunks, 1000, rig.topology->GeometryKey(), ReadContext(),
						decoded));
		auto stale = point;
		stale.asset.emitters[0].position.world.x += .1f;
		CHECK(F::Prepare(stale, ReadContext(), p).code == Code::InvalidState);
		auto absentValues = point;
		absentValues.values.records.pop_back();
		CHECK(!F::Prepare(absentValues, ReadContext(), p));
		auto wrongMembership = point;
		auto wrongDescriptor = std::make_shared<ConditionalFieldDescriptor>(*point.descriptor);
		wrongDescriptor->membership.roots.clear();
		wrongMembership.descriptor = wrongDescriptor;
		CHECK(!F::Prepare(wrongMembership, ReadContext(), p));
		auto unknownCalibration = q;
		unknownCalibration.selection.calibration = 2;
		CHECK(F::Build(unknownCalibration, ReadContext(), failed).code == Code::Unavailable);
		auto noSource = rig.Request();
		CHECK(F::Build(noSource, ReadContext(), failed).code == Code::Unavailable);
		auto badProfile = q;
		badProfile.selection.profile = "UnknownProfile";
		CHECK(F::Build(badProfile, ReadContext(), failed).code == Code::Unavailable);
		auto missingValidation = aq;
		missingValidation.appearanceValidation.clear();
		CHECK(!F::Build(missingValidation, ReadContext(), failed));
		auto equivalent = q;
		auto equivalentAsset = std::make_shared<ReconstructedLightingAsset>(*q.asset);
		Calibration eq;
		eq.id = 3;
		eq.provenance = 1;
		eq.kind = CalibrationKind::LegacySceneEquivalent;
		eq.method = "explicit synthetic scene-equivalent gauge; unknown absolute scale";
		equivalentAsset->calibrations.push_back(eq);
		equivalentAsset->emitters[0].emission.calibration = 3;
		equivalent.asset = equivalentAsset;
		equivalent.selection.domain = FieldDomain::SceneEquivalent;
		equivalent.selection.calibration = 3;
		auto eqField = Build(equivalent);
		auto eqValue = Sample(eqField, Vec3(-1, -1, 1));
		CHECK(eqValue.descriptor->domain == FieldDomain::SceneEquivalent &&
			  eqValue.descriptor->unit == PhotometricUnit::Lux);
		CHECK(F::Prepare(eqField, ReadContext(), p));
		CHECK(p->Supports(Capability::IndirectSampling) == Status::Unavailable);
		// Identical complete dataset witnesses; time/RSS are not compared.
		auto repeat = Build(q);
		CHECK(repeat.chunks.size() == point.chunks.size());
		for (size_t j = 0; j < point.chunks.size(); ++j)
			CHECK(repeat.chunks[j].bytes == point.chunks[j].bytes);
		CHECK(F::Prepare(point, ReadContext(), p));
		auto begin = std::chrono::steady_clock::now();
		cq.field.id = 1000;
		cq.field.generation = 0;
		cq.positionWorld = Vec3(-.95f, -.95f, 1.05f);
		cq.configuration = 0;
		cq.quantity = ConditionalQuantity::NormalIrradiance;
		ConditionalFieldSample output;
		for (unsigned j = 0; j < 10000; ++j)
			CHECK(p->ConditionalField(point.asset, cq, output) == Status::Ok);
		double queryNs =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() * 1e5;
		if (argc > 1)
		{
			double maximumAngularRMSE = 0, maximumEnergyBias = 0;
			for (const auto &d : point.diagnostics)
				if (d.referenceKnown)
				{
					maximumAngularRMSE = std::max(maximumAngularRMSE, d.relativeRMSE);
					maximumEnergyBias = std::max(maximumEnergyBias, std::abs(d.energyBias));
				}
			std::ofstream o(argv[1]);
			o << "{\"schema\":\"org.openjk.field-builder-test.v1\",\"checks\":" << checks
			  << ",\"failures\":" << failures
			  << ",\"point_relative_error\":" << std::abs(value.value.x - truth) / truth
			  << ",\"spot_relative_error\":" << shapeErrors[0]
			  << ",\"rectangle_relative_error\":" << shapeErrors[1]
			  << ",\"tube_relative_error\":" << shapeErrors[2]
			  << ",\"sky_irradiance_available\":" << skyE.work.available
			  << ",\"sky_radiance_available\":" << skyL.work.available
			  << ",\"red_bounce_ratio\":" << red.value.x / red.value.y
			  << ",\"bounce_heldout_used\":" << bounceUsed
			  << ",\"bounce_heldout_unavailable\":" << bounceMissing
			  << ",\"bounce_heldout_relative_rmse\":" << bounceRelativeRMSE
			  << ",\"fine_bounce_heldout_used\":" << fineBounceUsed
			  << ",\"fine_bounce_heldout_unavailable\":" << fineBounceMissing
			  << ",\"fine_bounce_heldout_relative_rmse\":" << fineBounceRMSE
			  << ",\"fine_bounce_probes\":" << fineGI.work.available
			  << ",\"fine_bounce_requested_probes\":" << fineGI.work.probes
			  << ",\"fine_bounce_rays\":" << fineGI.work.rays
			  << ",\"sky_irradiance_absolute_error\":" << std::abs(sk.value.x - 3.141592653589793)
			  << ",\"sky_radiance_absolute_error\":" << std::abs(light.value.rgb.x - 1)
			  << ",\"point_probes\":" << point.work.available
			  << ",\"point_maximum_angular_rmse\":" << maximumAngularRMSE
			  << ",\"point_maximum_energy_bias\":" << maximumEnergyBias
			  << ",\"point_build_seconds\":" << point.work.seconds
			  << ",\"point_rays\":" << point.work.rays << ",\"point_bytes\":" << point.work.bytes
			  << ",\"point_integration_work\":" << point.work.angularEvaluations
			  << ",\"point_traversal_work\":" << point.work.traversal
			  << ",\"point_geometry_volume\":" << point.descriptor->geometryVolumeWorldCubed
			  << ",\"point_lighting_volume\":" << point.descriptor->lightingVolumeWorldCubed
			  << ",\"sky_irradiance_rays\":" << skyE.work.rays
			  << ",\"sky_radiance_rays\":" << skyL.work.rays
			  << ",\"passive_rays\":" << reflected.work.rays << ",\"point_archive_sha256\":\""
			  << Disk::Hex(Disk::Hash(archive.data(), archive.size())) << "\""
			  << ",\"point_topology_sha256\":\"" << point.descriptor->topology << "\""
			  << ",\"point_scene_sha256\":\"" << point.descriptor->membership.generation << "\""
			  << ",\"l2_rejected_probes\":" << l2point.work.probes - l2point.work.available
			  << ",\"wall_blocked_checks\":" << wallTests
			  << ",\"positive_opening_checks\":" << positiveOpening
			  << ",\"partial_area_relative_error\":" << partialError
			  << ",\"partial_area_fraction\":" << partialFraction
			  << ",\"heldout_used\":" << heldoutUsed
			  << ",\"heldout_unavailable\":" << heldoutMissing
			  << ",\"heldout_relative_rmse\":" << spatialRelativeRMSE
			  << ",\"archive_bytes\":" << archive.size() << ",\"query_nanoseconds\":" << queryNs
			  << ",\"appearance_wall_leak\":0}";
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
