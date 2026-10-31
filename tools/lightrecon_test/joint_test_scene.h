// Shared owned scene and trusted answer generator for S11/S12 tests only.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "joint_fixture.h"
#include <algorithm>
namespace JointTest
{
using namespace JointFixture;
namespace J = Rend2LightRecon::Core::Joint;
namespace S = Rend2LightRecon::Core::Shape;
namespace I = Rend2LightRecon::Core::Indirect;
template <class T> class Stream final : public ChunkSource<T>
{
  public:
	std::shared_ptr<std::vector<T>> data = std::make_shared<std::vector<T>>();
	bool corrupt = false;
	Error Read(uint64_t first, size_t count, const ReadContext &r, Chunk<T> &out) const override
	{
		if (r.cancellation && r.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Fixture cancellation");
		if (corrupt)
			return Failure(Code::ProviderError, "Fixture corrupted stream");
		if (first > data->size() || count > data->size() - first)
			return Failure(Code::ProviderError, "Fixture short read");
		out = VectorChunk<T>(data, size_t(first), count);
		return {};
	}
};
Frame FrameDown()
{
	Frame f;
	f.known = true;
	f.right = Vec3(1, 0, 0);
	f.up = Vec3(0, -1, 0);
	f.forward = Vec3(0, 0, -1);
	return f;
}
void Rebuild(Scene &scene)
{
	GeometryKey key;
	for (const auto &s : scene.surfaces)
	{
		key.U(unsigned(s.kind));
		key.U(s.materialFacts.signals);
		key.U(unsigned(s.materialFacts.cull));
		for (const auto &v : s.vertices)
			key.V(v.position);
	}
	scene.map.sha256 = key.Finish();
	SurfaceCapture capture;
	capture.Begin(BuildPolicy::ForceRebuild);
	Must(capture.SetMap(scene.map));
	for (const auto &s : scene.surfaces)
		Must(capture.Add(s));
	Must(capture.Freeze());
	Must(StaticVisibility::Build(capture.Acquire(), {}, Read(), scene.visibility, scene.domain));
}
void Receivers(Scene &scene)
{
	for (int side : {-1, 1})
	{
		double x = side * 1.5;
		size_t surface = scene.surfaces.size() + 1;
		scene.Quad(V(x, -1.4, 0), V(x, 1.4, 0), V(x, 1.4, 1.5), V(x, -1.4, 1.5));
		for (double y : {-1., -.5, .5, 1.})
			for (double z : {.35, .9})
				scene.patches.push_back(
					Patch(scene.patches.size() + 1, surface, 2, V(x, y, z), V(-side, 0, 0), .12));
	}
	Rebuild(scene);
}
void Scale(Scene &scene, double factor)
{
	for (auto &s : scene.surfaces)
		for (auto &v : s.vertices)
			v.position = (V(v.position) * factor).Float();
	for (auto &p : scene.patches)
	{
		p.receiver.world = (V(p.receiver.world) * factor).Float();
		p.areaWorldSquared *= factor * factor;
		auto footprint = std::make_shared<std::vector<Bake::FootprintTriangle>>(
			p.receiver.footprint.data, p.receiver.footprint.data + p.receiver.footprint.count);
		for (auto &t : *footprint)
			for (auto &v : t.world)
				v = (V(v) * factor).Float();
		p.receiver.footprint = Lease(footprint);
	}
	Rebuild(scene);
}
J::Candidate Point(Id id, V position, bool active = false)
{
	J::Candidate c;
	c.id = id;
	c.initialActive = active;
	c.provider = "controlled-fixture-prior";
	c.provenance = "Candidate position/shape prior supplied; source amplitudes withheld";
	auto &s = c.shape;
	s.initial.geometry.source.id = id;
	s.initial.geometry.position.known = true;
	s.initial.geometry.position.world = position.Float();
	s.initial.geometry.kind = EmitterKind::Point;
	s.initial.geometry.angular.kind = AngularKind::Isotropic;
	s.initial.rgbAmplitude = Vec3(.4, .4, .4);
	s.upperRgb = Vec3(6, 6, 6);
	s.centreBounds = Box(V(-1.4, -1.3, .1), V(1.4, 1.3, 1.4));
	s.maximumTranslationWorld = 2;
	s.fitPosition = s.fitFrame = s.fitDimensions = s.fitCone = false;
	s.minimumDimensionWorld = .02;
	s.maximumDimensionWorld = 2;
	return c;
}
J::Candidate Extended(Id id, EmitterKind kind, V position, bool active = false)
{
	auto c = Point(id, position, active);
	auto &e = c.shape.initial;
	e.geometry.kind = kind;
	e.geometry.frame = FrameDown();
	e.geometry.geometry.known = true;
	e.geometry.geometry.halfWidthWorld = .28;
	e.geometry.geometry.halfHeightWorld = .12;
	e.geometry.geometry.halfLengthWorld = .4;
	e.geometry.geometry.radiusWorld = .04;
	e.geometry.angular.kind = kind == EmitterKind::Spot ? AngularKind::SmoothCone : AngularKind::Lambertian;
	e.geometry.angular.cosInner = .95;
	e.geometry.angular.cosOuter = .55;
	c.shape.maximumRotationRadians = .5;
	return c;
}
I::Reflectance KnownMaterial(Id id, V rho)
{
	I::Reflectance m;
	m.material = id;
	m.knowledge = I::MaterialKnowledge::Known;
	m.lower = m.upper = m.initial = rho.Float();
	m.provenance = "Owned bake-time diffuse reflectance";
	return m;
}
struct Rig
{
	Scene scene;
	J::Request request;
	std::shared_ptr<Stream<J::Observation>> train = std::make_shared<Stream<J::Observation>>();
	std::shared_ptr<Stream<J::Observation>> validation = std::make_shared<Stream<J::Observation>>();
	std::shared_ptr<Stream<J::Observation>> held = std::make_shared<Stream<J::Observation>>();
	Rig(bool room = true) : scene(true)
	{
		if (room)
			Receivers(scene);
		request.mapKey = scene.map.sha256;
		request.transfer = scene.Transfer(10);
		request.trainingKey = Key("S11 fixed spatial training partition, fixture v1");
		request.policyKey = Key("S11 predeclared relative physical fixture policy v1");
		request.geometry.visibility = scene.visibility;
		request.geometry.geometryKey = request.transfer->GeometryKey();
		request.geometry.materialKey = Key("owned S11 material");
		request.geometry.observationPolicyKey = Key("owned original footprint");
		request.placement.map = scene.map;
		request.placement.domain = scene.domain;
		request.placement.domainKey = Key("owned free domain + visual geometry");
		request.placement.geometry = scene.visibility;
		request.materials = {KnownMaterial(1, V()), KnownMaterial(2, V())};
		J::Profile p;
		p.model.law = S::Law::DiffuseIntegral;
		p.model.output = S::Output::RelativeIncident;
		p.model.allowAssumed = true;
		p.model.units.worldUnitsPerMeter = 1;
		p.model.provenance = "Specified physical relative integral, not historical Raven identity";
		p.contract.operation = I::ObservationOperator::IncidentLinear;
		p.contract.transferAttested = p.contract.primariesAttested = true;
		p.contract.units.worldUnitsPerMeter = 1;
		p.contract.provenance = "Owned relative observation contract";
		p.calibrationHypothesis = "Unknown absolute scale; gauge gain=1";
		request.profiles = {p};
		request.coarseSampling.compareRefined = request.strictSampling.compareRefined = false;
		request.coarseSampling.areaDivisions = 2;
		request.strictSampling.areaDivisions = 4;
		request.coarseSampling.tubeSides = 8;
		request.strictSampling.tubeSides = 16;
		request.coarseSampling.tubeSegments = 2;
		request.strictSampling.tubeSegments = 4;
		auto &o = request.options;
		o.geometry = false;
		o.materials = false;
		o.residualSearch = false;
		o.mergeSplit = false;
		o.rounds = 2;
		o.beamWidth = 2;
		o.maxModels = 32;
		o.amplitudeSweeps = 5;
		o.amplitudeLineSteps = 22;
		o.jointSweeps = 1;
		o.geometrySweeps = 1;
		o.complexityPerSource = 1e-5;
		o.complexityPerParameter = 1e-7;
		o.equivalentValidationSlack = 1e-5;
		o.maximumRequiredBlockRmse = .015;
		o.maxEvaluations = 200000;
		o.maxRays = 20000000;
		o.maxWork = 2000000000;
	}
	void UpdateGeometry()
	{
		request.mapKey = scene.map.sha256;
		request.transfer = scene.Transfer(1000);
		request.geometry.visibility = scene.visibility;
		request.geometry.geometryKey = request.transfer->GeometryKey();
		request.placement.map = scene.map;
		request.placement.geometry = scene.visibility;
	}
	std::vector<Vec3> Truth(const std::vector<J::Source> &privateSources)
	{
		std::vector<Vec3> total(request.transfer->Patches().size());
		for (const auto &s : privateSources)
		{
			I::Root root;
			root.source = s.candidate.id;
			root.kind = s.candidate.root;
			root.domain = request.profiles[0].contract.incidentDomain;
			root.provenance = "Trusted answer generation; never passed to Solve";
			root.generation = Key("private answer root");
			root.sunCoverage = SunCoverage::ExcludesExplicitSun;
			if (root.kind == TransportRootKind::LocalEmitter)
			{
				auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
				for (const auto &p : request.transfer->Patches())
					receivers->push_back(p.receiver);
				auto model = request.profiles[0].model;
				model.output = S::Output::RelativeIncident;
				std::shared_ptr<const S::Block> block;
				Must(S::Prepare(model, {s.value}, Lease(receivers), request.geometry, request.strictSampling,
								Read(), block));
				std::vector<S::Prediction> value;
				S::Work work;
				Must(block->Evaluate({s.value.rgbAmplitude}, Read(), value, work));
				for (const auto &p : value)
				{
					root.incident.push_back(p.value);
					root.available.push_back(p.available);
				}
			}
			else
			{
				auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>(1, s.environment);
				(*sources)[0].compilerRgbAmplitude = s.value.rgbAmplitude;
				(*sources)[0].amplitudeKnown = true;
				Bake::Options o;
				o.output = Bake::Domain::CompilerLinearRelative255;
				o.allowAssumed = true;
				Bake::Work w;
				Must(I::PredictRoot(*request.transfer, s.candidate.root, request.profiles[0].model.profile,
									Lease(sources), request.geometry, o, Read(), root, w));
			}
			I::Request q;
			q.transfer = request.transfer;
			q.roots = {root};
			q.materials = request.materials;
			q.contract = request.profiles[0].contract;
			q.contract.operation = I::ObservationOperator::IncidentLinear;
			q.trainingKey = Key("private answer dummy value operation");
			auto rows = std::make_shared<Stream<I::Observation>>();
			I::Observation obs;
			obs.id = 1;
			obs.patch = request.transfer->Patches()[0].id;
			obs.spatialBlock = 1;
			obs.areaWorldSquared = request.transfer->Patches()[0].areaWorldSquared;
			rows->data->push_back(obs);
			q.training = rows;
			q.count = 1;
			I::Result bounce;
			Must(I::Fit(q, Read(), bounce));
			for (size_t i = 0; i < total.size(); ++i)
				total[i] = (V(bounce.values[i].totalPreencoding) + V(total[i])).Float();
		}
		for (const auto &root : request.boundaryRoots)
			for (size_t i = 0; i < total.size(); ++i)
				total[i] = (V(total[i]) + V(root.incident[i])).Float();
		return total;
	}
	void Observations(const std::vector<Vec3> &incident, bool encoded = false)
	{
		train->data->clear();
		validation->data->clear();
		held->data->clear();
		for (size_t i = 1; i < request.transfer->Patches().size(); ++i)
		{
			const auto &p = request.transfer->Patches()[i];
			if (std::abs(p.receiver.world.y) < .1)
				continue;
			J::Observation o;
			o.id = p.id;
			o.patch = p.id;
			o.receiver = p.receiver;
			o.region = p.receiver.world.x < 0 ? 1 : 2;
			o.block = p.receiver.surface * 10 + o.region;
			o.measure = p.areaWorldSquared;
			o.comparable = true;
			Vec3 value = incident[i];
			uint8_t packed[3];
			if (encoded)
			{
				Must(Bake::Encode(request.profiles[0].model.profile, value, value, packed));
				for (unsigned c = 0; c < 3; ++c)
					o.rgb[c] = Amplitude::Rgb8(packed[c]);
			}
			else
				for (unsigned c = 0; c < 3; ++c)
					o.rgb[c].lower = o.rgb[c].upper = V(value)[c];
			if (p.receiver.world.y < 0)
			{
				o.role = J::Role::Training;
				train->data->push_back(o);
			}
			else if (p.receiver.world.x < 0)
			{
				o.role = J::Role::Validation;
				validation->data->push_back(o);
			}
			else
			{
				o.role = J::Role::Heldout;
				held->data->push_back(o);
			}
		}
		request.training = train;
		request.count = train->data->size();
	}
};
J::Source Answer(J::Candidate c, V q)
{
	J::Source s;
	s.candidate = c;
	s.value = c.shape.initial;
	s.value.rgbAmplitude = q.Float();
	s.environment = c.environment;
	return s;
}

void Historical(Rig &rig, Bake::Recipe recipe)
{
	Scale(rig.scene, 64);
	rig.UpdateGeometry();
	auto &p = rig.request.profiles[0];
	p.model.law = S::Law::HistoricalBake;
	p.model.profile = Bake::NetRadiantProfile(recipe);
	p.model.allowAssumed = true;
	p.contract.profile = p.model.profile;
	p.contract.incidentDomain = I::Root::Domain::CompilerRelative255;
	p.contract.operation = I::ObservationOperator::NetRadiantEncoded;
	rig.request.options.maximumRequiredBlockRmse = 2;
	rig.request.options.equivalentValidationSlack = .15;
	rig.request.options.amplitudeLineSteps = 24;
}
J::Candidate CompilerPoint(Id id, V position, bool active = false)
{
	auto c = Point(id, position, active);
	c.shape.centreBounds = Box(V(-90, -85, 1), V(90, 85, 100));
	c.shape.maximumTranslationWorld = 150;
	c.shape.initial.rgbAmplitude = Vec3(12, 12, 12);
	c.shape.upperRgb = Vec3(600, 600, 600);
	return c;
}

} // namespace JointTest
