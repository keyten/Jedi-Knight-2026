// P06 owned reference receipts and real S13 producer/query integration.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/contributions.h"
#include "lightrecon/photometry.h"
#include "producer_test_scene.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace JointTest;
namespace P = Rend2LightRecon::Core::Producer;
namespace A = Rend2LightRecon::Core::Attribution;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d %s\n", __LINE__, #x);                                             \
		}                                                                                          \
	} while (0)
std::vector<std::string> cases;
Estimate Bound(float lo, float hi)
{
	Estimate e;
	e.bounded = true;
	e.lower = lo;
	e.upper = hi;
	return e;
}
struct Fixture
{
	ReconstructedLightingAsset asset;
	std::vector<Disk::Chunk> chunks;
	std::vector<P::SampleRecord> samples;
	P::ObservationRecord observation;
	std::vector<std::vector<P::ContributionRecord>> records;
	Fixture()
	{
		asset.lineage = "owned/P06/reference";
		asset.provenance = 1;
		asset.units.worldUnitsPerMeter = 1;
		Provenance p;
		p.id = 1;
		p.kind = ProvenanceKind::Authored;
		p.producer = "P06 owned analytic source answers; not solver calibration";
		asset.provenanceRecords = {p};
		Calibration c;
		c.id = 2;
		c.provenance = 1;
		c.kind = CalibrationKind::PhysicallyAuthored;
		c.method = "Authored candela/metre reference";
		asset.calibrations = {c};
		LightingConfiguration configuration;
		configuration.id = 100;
		configuration.provenance = 1;
		configuration.key = "owned/reference/1";
		configuration.profile = "specified-physical-integral";
		configuration.calibrationHypothesis = "authored SI";
		asset.lightingConfigurations = {configuration};
		auto &a = asset.assessment;
		a.present = true;
		a.producer = "P06 owned receipts";
		a.algorithm = "analytical reference/1";
		a.map = "maps/owned/P06";
		a.bspSha256 = Disk::Hex(Key("owned geometry"));
		a.settingsHash = "owned/1";
		a.decoder = "physical linear Rec709";
		a.inputsLoaded = true;
		a.structuralValidated = a.payloadsValidated = true;
		a.selectedConfiguration = 100;
		a.sampleDomain = 1000;
		a.diagnostics = 1001;
		for (unsigned i = 0; i < 7; ++i)
		{
			ReadinessAssessment r;
			r.kind = ReadinessKind(i);
			r.scope = "Owned analytical reference only";
			a.readiness.push_back(r);
		}
		P::SampleRecord s;
		s.id = 701;
		s.patch = 701;
		s.surface = 21;
		s.material = 31;
		s.normal = s.shadingNormal = Vec3(0, 0, 1);
		s.area = 1;
		s.kind = P::SampleKind::SurfaceLuxel;
		samples = {s};
		observation.sample = 701;
		observation.patch = 701;
		observation.sampleDomain = 1000;
		observation.available = observation.discrepancyKnown = true;
		observation.domain = P::ObservationDomain::LinearRec709Irradiance;
		observation.presentMask = 7;
		observation.unit = PhotometricUnit::Lux;
		observation.calibration = 2;
	}
	Id Add(Vec3 rgb, TransportRootKind root = TransportRootKind::LocalEmitter, unsigned order = 0)
	{
		Id id = 10 + records.size();
		if (root == TransportRootKind::Sky)
		{
			EnvironmentLighting e;
			e.source.id = id;
			e.source.provenance = 1;
			e.source.state = HypothesisState::Resolved;
			e.meanLuminance.unit = PhotometricUnit::Nits;
			e.meanLuminance.calibration = 2;
			e.sunCoverage = SunCoverage::ExcludesExplicitSun;
			asset.environments.push_back(e);
		}
		else
		{
			PhysicalEmitter e;
			e.source.id = id;
			e.source.provenance = 1;
			e.source.state = HypothesisState::Resolved;
			e.position.known = true;
			e.position.world = Vec3(0, 0, 4);
			e.emission.unit =
				root == TransportRootKind::Sun ? PhotometricUnit::Lux : PhotometricUnit::Candela;
			e.emission.calibration = 2;
			auto photo = Rend2LightRecon::Photometric::RGBToPhotometry(
				(V(rgb) * (root == TransportRootKind::Sun ? 1 : 16)).Float(), e.emission.unit, 2);
			if (!photo)
				throw std::runtime_error("Owned reference photometry failed");
			e.emission = photo.value;
			e.kind = root == TransportRootKind::Sun ? EmitterKind::Directional : EmitterKind::Point;
			e.angular.kind =
				root == TransportRootKind::Sun ? AngularKind::Parallel : AngularKind::Isotropic;
			if (root == TransportRootKind::Sun)
			{
				e.position.known = false;
				e.frame = FrameDown();
				SunSource sun;
				sun.emitter = id;
				asset.suns.push_back(sun);
			}
			asset.emitters.push_back(e);
		}
		ContributionReference r;
		r.dataset = 2000 + records.size();
		r.sampleDomain = 1000;
		r.sources = {id};
		r.configuration = 100;
		r.domain = "physical-irradiance-lux";
		r.quantity = ContributionQuantity::IrradianceRGB;
		r.unit = PhotometricUnit::Lux;
		r.calibration = 2;
		r.component = order ? ContributionComponent::Indirect : ContributionComponent::Direct;
		auto &m = r.membership;
		m.version = 2;
		m.model = "owned-reference";
		m.rootsComplete = true;
		TransportRoot rr;
		rr.source = id;
		rr.kind = root;
		m.roots = {rr};
		m.reflectionOrderKnown = true;
		m.reflectionOrders = {order};
		m.minimumReflectionOrder = m.maximumReflectionOrder = order;
		m.directLocal = m.directSun = m.directSky = m.legacyAmbient = m.reflected = m.unresolved =
			FieldMembership::Excluded;
		if (order)
			m.reflected = FieldMembership::Included;
		else if (root == TransportRootKind::Sun)
			m.directSun = FieldMembership::Included;
		else if (root == TransportRootKind::Sky)
			m.directSky = FieldMembership::Included;
		else
			m.directLocal = FieldMembership::Included;
		m.colour = ContributionColour::LinearRec709D65;
		m.configuration = 100;
		m.generation = "owned-generation";
		m.geometry = "owned-geometry";
		m.profile = "owned-physical-profile";
		m.domain = r.domain;
		m.addition = CompositionRule::Permitted;
		asset.contributions.push_back(r);
		P::ContributionRecord v;
		v.sample = 701;
		v.source = id;
		v.root = root;
		v.reflectionOrder = order;
		v.available = true;
		v.value = v.lower = v.upper = rgb;
		records.push_back({v});
		if (order)
			observation.indirect = (V(observation.indirect) + V(rgb)).Float();
		else if (root == TransportRootKind::Sun)
			observation.sun = rgb;
		else if (root == TransportRootKind::Sky)
			observation.sky = rgb;
		else
			observation.direct = (V(observation.direct) + V(rgb)).Float();
		observation.prediction = (V(observation.direct) + V(observation.sun) + V(observation.sky) +
								  V(observation.indirect))
									 .Float();
		observation.observedLower = observation.observedUpper = observation.prediction;
		for (unsigned use = 0; use <= unsigned(SourceUse::Specular); ++use)
		{
			SourceAdmission admission;
			admission.source = id;
			admission.use = SourceUse(use);
			admission.reason = "Reference-data test only; no renderer consumer enabled";
			asset.assessment.admissions.push_back(admission);
		}
		asset.assessment.resolvedSources = asset.emitters.size() + asset.environments.size();
		return id;
	}
	void Bounce(size_t original, Vec3 rgb)
	{
		auto ref = asset.contributions.at(original);
		ref.dataset = 2000 + records.size();
		ref.component = ContributionComponent::Indirect;
		auto &m = ref.membership;
		m.directLocal = m.directSun = m.directSky = m.legacyAmbient = FieldMembership::Excluded;
		m.reflected = FieldMembership::Included;
		m.reflectionOrders = {1};
		m.minimumReflectionOrder = m.maximumReflectionOrder = 1;
		auto value = records.at(original)[0];
		value.reflectionOrder = 1;
		value.value = value.lower = value.upper = rgb;
		asset.contributions.push_back(ref);
		records.push_back({value});
		observation.indirect = (V(observation.indirect) + V(rgb)).Float();
		observation.prediction = (V(observation.direct) + V(observation.sun) + V(observation.sky) +
								  V(observation.indirect))
									 .Float();
		observation.observedLower = observation.observedUpper = observation.prediction;
	}
	void Payload(Dataset d, std::vector<Disk::Chunk> c)
	{
		asset.datasets.push_back(d);
		chunks.insert(chunks.end(), c.begin(), c.end());
	}
	P::Artifact Build()
	{
		asset.datasets.clear();
		chunks.clear();
		Dataset d;
		std::vector<Disk::Chunk> c;
		Must(P::EncodeSamples(1000, samples, 1, Read(), d, c));
		Payload(d, c);
		for (size_t i = 0; i < records.size(); ++i)
		{
			Must(P::EncodeContributions(asset.contributions[i].dataset, records[i], 1, Read(), d,
										c));
			Payload(d, c);
		}
		Must(P::EncodeObservations(1001, {observation}, 1, Read(), d, c));
		Payload(d, c);
		std::string error;
		if (!Validate(asset, &error))
			std::printf("fixture validation: %s\n", error.c_str());
		P::Artifact a;
		a.asset = std::make_shared<const ReconstructedLightingAsset>(asset);
		a.chunks = chunks;
		return a;
	}
	ContributionQuery Query(size_t index = 0) const
	{
		ContributionQuery q;
		q.sampleDomain = 1000;
		q.sample = 701;
		q.source.id = asset.contributions[index].sources[0];
		q.dataset = asset.contributions[index].dataset;
		q.configuration = 100;
		q.component = asset.contributions[index].component;
		return q;
	}
	void Proof()
	{
		auto &r = asset.contributions[0];
		r.membership.replacement = CompositionRule::Permitted;
		r.replacement.supported = ReplacementAllProofs;
		r.replacement.provenance = 1;
		r.replacement.scope = "One independently authored linear receiver";
		r.replacement.runtimeOperator = "physical-point-irradiance/1";
		r.replacement.maximumAbsoluteError = Bound(0, .001f);
		r.replacement.leakageRisk = Bound(0, 0);
		SourceUncertainty u;
		u.id = 4000;
		u.source = r.sources[0];
		u.track = 10;
		u.provenance = 1;
		u.method = "Independent authored answers, not a confidence posterior";
		u.inputHash = "own-reference";
		u.complete = u.absoluteScaleKnown = true;
		u.minimumCount = u.maximumCount = 1;
		for (unsigned i = 0; i < 13; ++i)
		{
			UncertaintyFactor f;
			f.cause = UncertaintyCause(i);
			f.status = UncertaintyStatus::Supported;
			f.reason = "Controlled authored reference truth";
			u.factors.push_back(f);
		}
		asset.uncertainty = {u};
	}
	A::ReplacementRequest Replacement() const
	{
		A::ReplacementRequest q;
		q.contribution = Query();
		q.targetDomain = "physical-irradiance-lux";
		q.unit = PhotometricUnit::Lux;
		q.calibration = 2;
		q.colour = ContributionColour::LinearRec709D65;
		q.runtimeOperator = "physical-point-irradiance/1";
		q.maximumAbsoluteError = .002;
		return q;
	}
};
std::shared_ptr<const A::Prepared> Prepare(const P::Artifact &a)
{
	std::shared_ptr<const A::Prepared> p;
	auto e = A::Prepare(a, Read(), p);
	if (!e)
		std::printf("prepare: %s\n", e.message.c_str());
	CHECK(e);
	return p;
}
void QueriesAndPhysics()
{
	Fixture f;
	Id source = f.Add(Vec3(6.25, 3.125, 1.5625));
	f.Proof();
	auto artifact = f.Build();
	auto p = Prepare(artifact);
	if (!p)
		return;
	// Independent analytical point reference, not a read-back of stored data.
	auto emitter = f.asset.emitters[0];
	emitter.emission.intensity.known = true;
	emitter.emission.intensity.value = 100;
	Rend2LightRecon::Photometric::Receiver receiver;
	receiver.normalWorld = Vec3(0, 0, 1);
	emitter.emission.color.known = true;
	emitter.emission.color.rgb = Vec3(1, 1, 1);
	auto reference = Rend2LightRecon::Photometric::PointReceiverIrradiance(
		f.asset, emitter, receiver, Rend2LightRecon::Photometric::PointModel::IdealInverseSquare);
	CHECK(reference);
	CHECK(std::abs(reference.value.rgb.x - 6.25) < 1e-6);
	Manager manager;
	CHECK(P::Publish(artifact, manager, manager.Acquire().Generation(), Read()) == Status::Ok);
	auto snapshot = manager.Acquire();
	auto q = f.Query();
	q.source = snapshot.IdentifySource(source).value;
	auto result = snapshot.SampleContribution(q);
	CHECK(result);
	CHECK(result.value.value.x == 6.25 && result.value.lightingSampleAvailable);
	A::Ledger ledger;
	CHECK(p->Audit(1000, 701, ledger) == Status::Ok);
	CHECK(ledger.attributionComplete && ledger.ratioAvailable && ledger.modelObservedRatio.x == 1);
	CHECK(!ledger.physicalConservationTested);
	CHECK(A::MayReplace(f.asset, *p, f.Replacement()));
	q.sample = 0xabcdef;
	CHECK(snapshot.SampleContribution(q).status == Status::NotFound);
	manager.Reset();
	CHECK(snapshot.SampleContribution(f.Query()).status ==
		  Status::InvalidArgument); // source generation is mandatory
	CHECK(snapshot.SampleContribution(q).status == Status::NotFound); // old snapshot remains pinned
	auto changed = f.asset;
	changed.contributions[0].replacement.scope = "changed";
	CHECK(manager.Publish(changed, Completeness::Partial, p) == Status::Corrupt);
	std::vector<uint8_t> bytes;
	Disk::Fingerprint key;
	key.algorithm = "P06-test";
	key.algorithmVersion = 1;
	key.units = f.asset.units;
	key.complete.fill(true);
	for (auto kind :
		 {Disk::InputKind::Bsp, Disk::InputKind::Settings, Disk::InputKind::UnitCalibration})
	{
		Disk::InputContent c;
		c.kind = kind;
		c.name = "owned/" + std::to_string(unsigned(kind));
		c.bytes = 1;
		c.sha256 = Key(c.name);
		key.inputs.push_back(c);
	}
	CHECK(Disk::Encode(f.asset, key, artifact.chunks, bytes));
	Disk::MemoryReader reader(bytes.data(), bytes.size());
	ReconstructedLightingAsset restored;
	Disk::Index index;
	CHECK(Disk::Open(reader, key, restored, index));
	CHECK(restored.version == 8 &&
		  restored.contributions[0].replacement.supported == ReplacementAllProofs);
	P::Artifact round;
	round.asset = std::make_shared<const ReconstructedLightingAsset>(restored);
	for (const auto &section : index.sections)
		if (section.type == unsigned(Disk::Section::Chunk))
		{
			Disk::Chunk c;
			c.dataset = section.dataset;
			c.firstElement = section.firstElement;
			c.count = section.count;
			CHECK(Disk::ReadChunk(reader, index, c.dataset, c.firstElement, c.bytes));
			round.chunks.push_back(c);
		}
	auto loaded = Prepare(round);
	CHECK(loaded && loaded->AcceptsAsset(f.asset) == Status::Ok);
	cases.push_back("{\"name\":\"point_plane_query_roundtrip\",\"value_lux\":6.25,\"safe_authored_"
					"receiver\":true}");
}
void SparseAndUnknown()
{
	Fixture f;
	f.Add(Vec3());
	auto missing = f.samples[0];
	missing.id = 702;
	f.samples.push_back(missing);
	f.asset.contributions[0].completeSampleCoverage = false;
	auto p = Prepare(f.Build());
	if (!p)
		return;
	ContributionSample value;
	CHECK(p->Contribution(f.asset, f.Query(), value) == Status::Ok && value.value.x == 0);
	auto q = f.Query();
	q.sample = 702;
	CHECK(p->Contribution(f.asset, q, value) == Status::Unavailable);
	f.records[0][0].available = false;
	p = Prepare(f.Build());
	CHECK(p && p->Contribution(f.asset, f.Query(), value) == Status::Unavailable);
	f.records[0][0].available = true;
	f.records[0][0].issues = OpticalUnknown;
	p = Prepare(f.Build());
	CHECK(p && p->Contribution(f.asset, f.Query(), value) == Status::Ok);
	CHECK(value.issues == OpticalUnknown);
	CHECK(!A::MayReplace(f.asset, *p, f.Replacement()));
	auto corrupt = f.Build();
	corrupt.chunks[0].bytes.back() ^= 1;
	std::shared_ptr<const A::Prepared> out;
	CHECK(!A::Prepare(corrupt, Read(), out) && !out);
	corrupt = f.Build();
	auto wrongStyle = std::make_shared<ReconstructedLightingAsset>(*corrupt.asset);
	wrongStyle->contributions[0].membership.style = 1;
	corrupt.asset = wrongStyle;
	CHECK(!A::Prepare(corrupt, Read(), out) && !out);
	ReadContext small = Read();
	small.maxBytes = 64;
	CHECK(!A::Prepare(f.Build(), small, out));
	class Cancel final : public Cancellation
	{
	  public:
		bool IsCancelled() const noexcept override
		{
			return true;
		}
	} cancel;
	auto read = Read();
	read.cancellation = &cancel;
	CHECK(A::Prepare(f.Build(), read, out).code == Code::Cancelled);
	cases.push_back("{\"name\":\"sparse_zero_unknown_corruption\",\"missing_is_zero\":false}");
	Fixture grid;
	grid.Add(Vec3(2, 1, 1));
	grid.samples[0].kind = P::SampleKind::GridAmbient;
	grid.samples[0].area = 0;
	grid.records[0].clear();
	grid.asset.contributions[0].completeSampleCoverage = false;
	grid.observation.channel = J::Channel::GridAmbient;
	grid.observation.domain = P::ObservationDomain::EncodedSourceRGB;
	grid.observation.unit = PhotometricUnit::Relative;
	grid.observation.calibration = 0;
	grid.observation.direct = {};
	p = Prepare(grid.Build());
	if (p)
	{
		A::Ledger ledger;
		CHECK(p->Audit(1000, 701, ledger) == Status::Ok);
		CHECK(ledger.predictionAvailable && !ledger.componentTotalsAvailable &&
			  !ledger.attributionComplete);
		CHECK(p->Contribution(grid.asset, grid.Query(), value) == Status::Unavailable);
	}
	cases.push_back("{\"name\":\"grid_total_without_source_reduction\",\"total_available\":true,"
					"\"source_attribution\":\"Unavailable\"}");
}
void Composition()
{
	Fixture f;
	f.Add(Vec3(3, 0, 0));
	f.Add(Vec3(0, 0, 4));
	f.Build();
	A::Component a, b;
	a.dataset = 2000;
	b.dataset = 2001;
	CHECK(A::CanAdd(f.asset, a, b));
	CHECK(!A::CanAdd(f.asset, a, a));
	f.asset.contributions[1].membership.roots = f.asset.contributions[0].membership.roots;
	f.asset.contributions[1].sources = f.asset.contributions[0].sources;
	f.asset.contributions[1].component = ContributionComponent::Indirect;
	f.asset.contributions[1].membership.directLocal = FieldMembership::Excluded;
	f.asset.contributions[1].membership.reflected = FieldMembership::Included;
	f.asset.contributions[1].membership.reflectionOrders = {1};
	f.asset.contributions[1].membership.minimumReflectionOrder =
		f.asset.contributions[1].membership.maximumReflectionOrder = 1;
	CHECK(A::CanAdd(f.asset, a, b)); // one direct root plus its first bounce
	f.asset.contributions[1].membership.reflectionOrders = {0, 1};
	f.asset.contributions[1].membership.minimumReflectionOrder = 0;
	f.asset.contributions[1].membership.directLocal = FieldMembership::Included;
	f.asset.contributions[1].component = ContributionComponent::Unresolved;
	CHECK(!A::CanAdd(f.asset, a, b)); // total field overlaps direct path
	f.asset.contributions[1].membership.rootsComplete = false;
	CHECK(!A::CanAdd(f.asset, a, b));
	f.asset.contributions[1].membership.rootsComplete = true;
	f.asset.contributions[1].membership.colour = ContributionColour::EncodedSourceRGB;
	CHECK(!A::CanAdd(f.asset, a, b));
	f.asset.contributions[1].membership.colour = ContributionColour::LinearRec709D65;
	auto config = f.asset.lightingConfigurations[0];
	config.id = 101;
	config.key = "alternative";
	f.asset.lightingConfigurations.push_back(config);
	f.asset.contributions[1].configuration = f.asset.contributions[1].membership.configuration =
		101;
	CHECK(A::CanAdd(f.asset, a, b).status == Status::Ambiguous);
	f.asset.contributions[1].membership.reflectionOrders = {1, 0};
	CHECK(!Validate(f.asset));
	cases.push_back(
		"{\"name\":\"root_order_alternative_composition\",\"double_count_rejected\":true}");
	Fixture field;
	field.Add(Vec3(2, 1, 1));
	field.Build();
	Dataset descriptor;
	descriptor.id = 3000;
	descriptor.purpose = DatasetPurpose::Transport;
	descriptor.layout = DatasetLayout::Chunked;
	descriptor.chunkElements = 1;
	descriptor.encoding = "owned-membership-only";
	descriptor.contentHash = "sha256:owned";
	descriptor.locator = "owned/field";
	field.asset.datasets.push_back(descriptor);
	TransportField transport;
	transport.source.id = 50;
	transport.source.provenance = 1;
	transport.dataset = 3000;
	transport.unit = PhotometricUnit::Lux;
	transport.calibration = 2;
	transport.membership = field.asset.contributions[0].membership;
	transport.membership.directLocal = FieldMembership::Excluded;
	transport.membership.reflected = FieldMembership::Included;
	transport.membership.reflectionOrders = {1};
	transport.membership.minimumReflectionOrder = transport.membership.maximumReflectionOrder = 1;
	field.asset.transport.push_back(transport);
	for (unsigned use = 0; use <= unsigned(SourceUse::Specular); ++use)
	{
		SourceAdmission admission;
		admission.source = 50;
		admission.use = SourceUse(use);
		admission.reason = "Field membership control";
		field.asset.assessment.admissions.push_back(admission);
	}
	A::Component direct, indirect;
	direct.dataset = 2000;
	indirect.dataset = 3000;
	indirect.field = true;
	CHECK(A::CanAdd(field.asset, direct, indirect));
	auto &m = field.asset.transport[0].membership;
	field.asset.transport[0].component = TransportComponent::ApertureTransport;
	m.directLocal = FieldMembership::Included;
	m.reflectionOrders = {0, 1};
	m.minimumReflectionOrder = 0;
	CHECK(!A::CanAdd(field.asset, direct, indirect));
	m.rootsComplete = false;
	m.roots[0].kind = TransportRootKind::Unknown;
	m.roots[0].source = 0;
	CHECK(!A::CanAdd(field.asset, direct, indirect));
	field.asset.transport[0].membership.version = 9;
	CHECK(!Validate(field.asset));
	cases.push_back(
		"{\"name\":\"field_direct_bounce_total_unknown\",\"unknown_roots_additive\":false}");
}
void UnsafeReplacement()
{
	for (unsigned mode = 0; mode < 14; ++mode)
	{
		Fixture f;
		f.Add(Vec3(6, 2, 1));
		f.Proof();
		if (mode == 0)
			f.asset.contributions[0].membership.uncertainty = AttributionParametersUnknown;
		if (mode == 1)
		{
			f.asset.emitters[0].source.state = HypothesisState::Candidate;
			f.asset.assessment.resolvedSources = 0;
			f.asset.assessment.conditionalSources = 1;
		}
		if (mode == 2)
			f.asset.uncertainty[0].factors[0].status = UncertaintyStatus::Ambiguous;
		if (mode == 3)
		{
			f.observation.domain = P::ObservationDomain::EncodedSourceRGB;
			f.observation.unit = PhotometricUnit::Relative;
			f.observation.calibration = 0;
			f.observation.unboundedUpperMask = 1;
			f.observation.observedLower.x = 255;
			f.observation.signedIntervalDiscrepancy.x = 249;
		}
		if (mode == 4)
			f.asset.contributions[0].membership.uncertainty = AttributionMaterialUnknown;
		if (mode == 5)
			f.records[0][0].issues = OpticalUnknown;
		if (mode == 6)
			f.asset.contributions[0].replacement.maximumAbsoluteError = {};
		if (mode == 7)
		{
			f.asset.contributions[0].replacement.leakageRisk = Bound(0, .2f);
		}
		if (mode == 8)
		{
			auto &r = f.asset.contributions[0];
			r.quantity = ContributionQuantity::LegacyRelativeChannels;
			r.unit = PhotometricUnit::Relative;
			r.membership.colour = ContributionColour::CompilerRelativeChannels;
			r.domain = r.membership.domain = "compiler-relative255/pre-encoding";
			r.membership.uncertainty |= AttributionProfileUnknown;
			f.asset.calibrations[0].kind = CalibrationKind::UncalibratedRelative;
			f.asset.emitters[0].emission.unit = PhotometricUnit::Relative;
			r.replacement.supported = ReplacementAllProofs;
			f.observation.unit = PhotometricUnit::Relative;
			f.observation.domain = P::ObservationDomain::EncodedSourceRGB;
			f.asset.uncertainty[0].absoluteScaleKnown = false;
		}
		if (mode == 9)
			f.asset.emitters[0].position.known = false;
		if (mode == 10)
		{
			f.samples[0].style = 1;
			f.asset.contributions[0].membership.style = 1;
		}
		if (mode == 11)
		{
			f.observation.observedLower = f.observation.observedUpper = Vec3(4, 2, 1);
			f.observation.signedIntervalDiscrepancy = Vec3(-2, 0, 0);
		}
		if (mode == 12)
			f.observation.issues = OpticalUnknown;
		if (mode == 13)
			f.observation.discrepancyKnown = false;
		auto p = Prepare(f.Build());
		if (p)
		{
			auto q = f.Replacement();
			q.contribution.quantity = f.asset.contributions[0].quantity;
			q.contribution.allowLegacyRelativeChannels = true;
			CHECK(!A::MayReplace(f.asset, *p, q));
		}
	}
	Fixture wrong;
	wrong.Add(Vec3(6, 2, 1));
	wrong.observation.observedLower = wrong.observation.observedUpper = Vec3(4, 2, 1);
	wrong.observation.signedIntervalDiscrepancy = Vec3(-2, 0, 0);
	auto p = Prepare(wrong.Build());
	if (p)
	{
		A::Ledger ledger;
		CHECK(p->Audit(1000, 701, ledger) == Status::Ok);
		CHECK(ledger.modelMinusObservedInterval.x == 2 && ledger.unresolved.x == 0);
		CHECK(ledger.modelObservedRatio.x == 1.5f);
	}
	cases.push_back("{\"name\":\"unsafe_replacement_adversaries\",\"negative_cases\":14,\"signed_"
					"discrepancy_becomes_GI\":false}");
}
void EncodedCoupling()
{
	Fixture f;
	f.Add(Vec3(510, 0, 0));
	f.Add(Vec3(0, 255, 0));
	f.asset.calibrations[0].kind = CalibrationKind::UncalibratedRelative;
	for (auto &e : f.asset.emitters)
		e.emission.unit = PhotometricUnit::Relative;
	for (auto &r : f.asset.contributions)
	{
		r.quantity = ContributionQuantity::LegacyRelativeChannels;
		r.unit = PhotometricUnit::Relative;
		r.domain = r.membership.domain = "compiler-relative255/pre-encoding";
		r.membership.colour = ContributionColour::CompilerRelativeChannels;
		r.membership.uncertainty = AttributionProfileUnknown | AttributionCensored;
		r.membership.replacement = CompositionRule::Forbidden;
	}
	Vec3 total;
	uint8_t packed[3];
	Must(Bake::Encode(Bake::NetRadiantProfile(), Vec3(510, 255, 0), total, packed));
	CHECK(total.x == 255 && total.y == 127.5f);
	Vec3 first, second;
	Must(Bake::Encode(Bake::NetRadiantProfile(), Vec3(510, 0, 0), first, packed));
	Must(Bake::Encode(Bake::NetRadiantProfile(), Vec3(0, 255, 0), second, packed));
	CHECK(first.y + second.y != total.y);
	f.observation.domain = P::ObservationDomain::EncodedSourceRGB;
	f.observation.unit = PhotometricUnit::Relative;
	f.observation.calibration = 2;
	f.observation.prediction = total;
	f.observation.observedLower = Vec3(255, 127, 0);
	f.observation.observedUpper = Vec3(0, 128, 1);
	f.observation.unboundedUpperMask = 1;
	auto artifact = f.Build();
	auto p = Prepare(artifact);
	if (!p)
		return;
	Manager manager;
	CHECK(P::Publish(artifact, manager, manager.Acquire().Generation(), Read()) == Status::Ok);
	auto snapshot = manager.Acquire();
	auto q = f.Query();
	q.quantity = ContributionQuantity::LegacyRelativeChannels;
	q.source = snapshot.IdentifySource(q.source.id).value;
	CHECK(snapshot.SampleContribution(q).status == Status::Unsupported);
	q.allowLegacyRelativeChannels = true;
	auto result = snapshot.SampleContribution(q);
	CHECK(result && result.value.value.x == 510 && !result.value.lightingSampleAvailable);
	A::Ledger ledger;
	CHECK(p->Audit(1000, 701, ledger) == Status::Ok && ledger.preencodingTotal.x == 510 &&
		  ledger.modelObservation.x == 255);
	CHECK(!ledger.ratioAvailable && !ledger.luminanceAvailable);
	auto replacement = f.Replacement();
	replacement.contribution = q;
	CHECK(!A::MayReplace(f.asset, *p, replacement));
	cases.push_back(
		"{\"name\":\"shared_peak_encoded_censoring\",\"preencoding_r\":510,\"encoded_r\":255,"
		"\"encoded_g\":127.5,\"sum_encoded_sources_g\":255,\"replacement\":false}");
}
void GeometryReceipts()
{
	for (bool opening : {false, true})
		for (bool spot : {false, true})
		{
			Scene scene(false, true, opening);
			auto receiver = scene.patches[1].receiver;
			receiver.sample = 701;
			S::Emitter source;
			source.geometry.position.known = true;
			source.geometry.position.world = Vec3(-1, 0, 1.8f);
			source.geometry.source.id = 10;
			source.geometry.kind = spot ? EmitterKind::Spot : EmitterKind::Point;
			source.rgbAmplitude = Vec3(100, 50, 25);
			source.geometry.angular.kind = spot ? AngularKind::SmoothCone : AngularKind::Isotropic;
			if (spot)
			{
				auto direction = Unit(V(receiver.world) - V(source.geometry.position.world));
				auto &frame = source.geometry.frame;
				frame.known = true;
				frame.forward = direction.Float();
				frame.right = Vec3(0, 1, 0);
				frame.up = Cross(direction, V(frame.right)).Float();
				source.geometry.angular.cosInner = .98f;
				source.geometry.angular.cosOuter = .6f;
			}
			S::Model model;
			model.law = S::Law::DiffuseIntegral;
			model.output = S::Output::RelativeIncident;
			model.units.worldUnitsPerMeter = 1;
			model.linearRec709PrimariesAttested = true;
			model.provenance =
				"Owned finite receiver physical integral; no historical compiler equivalence";
			Bake::EvaluationContext context;
			context.visibility = scene.visibility;
			context.geometryKey = scene.visibility->CertificationKey();
			context.materialKey = Key("opaque control");
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>(1, receiver);
			std::shared_ptr<const S::Block> block;
			S::Sampling policy;
			policy.compareRefined = false;
			Must(S::Prepare(model, {source}, Lease(receivers), context, policy, Read(), block));
			std::vector<S::Prediction> prediction;
			S::Work work;
			Must(block->Evaluate({source.rgbAmplitude}, Read(), prediction, work));
			CHECK(prediction.size() == 1 && prediction[0].available);
			if (opening)
				CHECK(prediction[0].value.x > 0);
			else
				CHECK(prediction[0].value.x == 0);
			Fixture f;
			f.Add(prediction[0].value);
			f.samples[0].world = receiver.world;
			f.samples[0].surface = receiver.surface;
			f.asset.emitters[0].kind = source.geometry.kind;
			f.asset.emitters[0].position = source.geometry.position;
			f.asset.emitters[0].frame = source.geometry.frame;
			f.asset.emitters[0].angular = source.geometry.angular;
			auto p = Prepare(f.Build());
			if (p)
			{
				ContributionSample value;
				CHECK(p->Contribution(f.asset, f.Query(), value) == Status::Ok);
				CHECK(value.value.x == prediction[0].value.x);
			}
			std::ostringstream row;
			row << "{\"name\":\"" << (spot ? "spot" : "point")
				<< (opening ? "_opening" : "_opaque_wall")
				<< "\",\"positive\":" << (opening ? "true" : "false")
				<< ",\"predicted_r\":" << prediction[0].value.x
				<< ",\"rays\":" << block->PreparationWork().rays << "}";
			cases.push_back(row.str());
		}
	Fixture environment;
	double partial = 0, unoccluded = 0;
	for (bool wall : {false, true})
	{
		Scene scene(false, wall, true);
		for (auto &vertex : scene.surfaces[1].vertices)
			vertex.position.y *= 5;
		Rebuild(scene);
		auto receiver = Patch(701, 2, 2, V(1, .8, 0), V(0, 0, 1), .4).receiver;
		S::Emitter source;
		source.geometry.source.id = 10;
		source.geometry.kind = EmitterKind::Spot;
		source.geometry.position.known = true;
		source.geometry.position.world = Vec3(-1, 0, 1.8f);
		source.geometry.angular.kind = AngularKind::SmoothCone;
		source.geometry.angular.cosInner = .98f;
		source.geometry.angular.cosOuter = .6f;
		source.rgbAmplitude = Vec3(100, 50, 25);
		V direction = Unit(V(receiver.world) - V(source.geometry.position.world));
		source.geometry.frame.known = true;
		source.geometry.frame.forward = direction.Float();
		source.geometry.frame.right = Vec3(0, 1, 0);
		// Orthogonalize the tangent for this oblique axis.
		source.geometry.frame.right = Unit(Cross(V(0, 0, 1), direction)).Float();
		source.geometry.frame.up = Cross(direction, V(source.geometry.frame.right)).Float();
		S::Model model;
		model.law = S::Law::DiffuseIntegral;
		model.output = S::Output::RelativeIncident;
		model.units.worldUnitsPerMeter = 1;
		model.provenance = "Owned aperture footprint reference";
		Bake::EvaluationContext context;
		context.visibility = scene.visibility;
		context.geometryKey = scene.visibility->CertificationKey();
		auto receivers = std::make_shared<std::vector<Bake::Receiver>>(1, receiver);
		std::shared_ptr<const S::Block> block;
		S::Sampling policy;
		policy.compareRefined = false;
		Must(S::Prepare(model, {source}, Lease(receivers), context, policy, Read(), block));
		std::vector<S::Prediction> prediction;
		S::Work work;
		Must(block->Evaluate({source.rgbAmplitude}, Read(), prediction, work));
		CHECK(prediction[0].available);
		if (wall)
			partial = prediction[0].value.x;
		else
			unoccluded = prediction[0].value.x;
	}
	CHECK(partial > 0 && partial < unoccluded);
	std::ostringstream transition;
	transition << "{\"name\":\"spot_partial_aperture_footprint\",\"partial_r\":" << partial
			   << ",\"unoccluded_r\":" << unoccluded << ",\"compiler_boundary_claim\":false}";
	cases.push_back(transition.str());
	{
		Scene unknown(false, true, false, true);
		S::Emitter source;
		source.geometry.source.id = 10;
		source.geometry.kind = EmitterKind::Point;
		source.geometry.position.known = true;
		source.geometry.position.world = Vec3(-1, 0, 1.8f);
		source.geometry.angular.kind = AngularKind::Isotropic;
		source.rgbAmplitude = Vec3(100, 50, 25);
		S::Model model;
		model.law = S::Law::DiffuseIntegral;
		model.output = S::Output::RelativeIncident;
		model.units.worldUnitsPerMeter = 1;
		model.provenance = "Owned unknown-alpha path";
		Bake::EvaluationContext context;
		context.visibility = unknown.visibility;
		context.geometryKey = unknown.visibility->CertificationKey();
		auto receivers =
			std::make_shared<std::vector<Bake::Receiver>>(1, unknown.patches[1].receiver);
		std::shared_ptr<const S::Block> block;
		S::Sampling policy;
		policy.compareRefined = false;
		Must(S::Prepare(model, {source}, Lease(receivers), context, policy, Read(), block));
		std::vector<S::Prediction> prediction;
		S::Work work;
		Must(block->Evaluate({source.rgbAmplitude}, Read(), prediction, work));
		CHECK(!prediction[0].available);
		cases.push_back("{\"name\":\"unknown_alpha_path\",\"known_zero_claimed\":false}");
	}
	environment.Add(Vec3(3, 4, 5), TransportRootKind::Sun);
	environment.Add(Vec3(1, 2, 4), TransportRootKind::Sky);
	environment.Bounce(0, Vec3(.7, .1, .05));
	auto p = Prepare(environment.Build());
	if (p)
	{
		A::Ledger ledger;
		CHECK(p->Audit(1000, 701, ledger) == Status::Ok);
		CHECK(ledger.sun.x == 3 && ledger.sky.x == 1 && ledger.indirect.x == .7f);
		A::Component a, b;
		a.dataset = 2000;
		b.dataset = 2001;
		CHECK(A::CanAdd(environment.asset, a, b));
	}
	cases.push_back("{\"name\":\"sun_sky_coloured_bounce_receipt\",\"components_separate\":true}");
	Fixture degenerate;
	degenerate.Add(Vec3(1, 1, 1));
	degenerate.Add(Vec3(1, 1, 1));
	for (auto &r : degenerate.asset.contributions)
		r.membership.uncertainty |= AttributionAlternative | AttributionParametersUnknown;
	p = Prepare(degenerate.Build());
	if (p)
	{
		A::Ledger ledger;
		CHECK(p->Audit(1000, 701, ledger) == Status::Ok && ledger.preencodingTotal.x == 2);
		CHECK(!A::MayReplace(degenerate.asset, *p, degenerate.Replacement()));
	}
	cases.push_back(
		"{\"name\":\"same_colour_allocation_ambiguity\",\"unique_allocation_claimed\":false}");
}
void RealProducer()
{
	Rig rig;
	auto a = Point(10, V(-.6, 0, .8)), b = Point(20, V(.6, .1, 1));
	rig.request.candidates = {a, b};
	rig.request.profiles[0].model.linearRec709PrimariesAttested = true;
	rig.Observations(rig.Truth({Answer(a, V(2.5, .3, .5)), Answer(b, V(.2, 2, 1))}));
	ProducerTest::Validation validation(rig);
	ProducerTest::Final final(rig);
	P::Result result;
	Must(P::Run(ProducerTest::Request(rig), Read(), result, &validation, &final));
	CHECK(result.state == P::State::Completed);
	if (!result.artifact.asset)
		return;
	std::shared_ptr<const A::Prepared> p;
	A::PreparedWork preparedWork;
	CHECK(A::Prepare(result.artifact, Read(), p, &preparedWork));
	if (!p)
		return;
	CHECK(result.artifact.asset->contributions.size() == 4);
	const auto &selected = result.fitted.alternatives.at(result.fitted.preferred);
	I::Result independentRoots;
	J::Work rootWork;
	Must(J::DecomposeFrozen(rig.request, selected, Read(), independentRoots, rootWork));
	std::map<Id, Id> originalIds;
	for (const auto &witness : result.uncertainty.witnesses)
		if (witness.model.key == selected.key)
			for (size_t i = 0; i < witness.tracks.size(); ++i)
				for (const auto &u : result.artifact.asset->uncertainty)
					if (u.track == witness.tracks[i])
						originalIds[u.source] = witness.model.sources[i].candidate.id;
	double maximumAttributionError = 0;
	size_t observed = 0;
	for (const auto &ref : result.artifact.asset->contributions)
	{
		std::vector<P::ContributionRecord> values;
		CHECK(P::ReadContributions(result.artifact, ref.dataset, Read(), values));
		for (const auto &v : values)
		{
			ContributionQuery q;
			q.sampleDomain = ref.sampleDomain;
			q.sample = v.sample;
			q.source.id = v.source;
			q.component = ref.component;
			q.quantity = ref.quantity;
			q.dataset = ref.dataset;
			ContributionSample response;
			auto status = p->Contribution(*result.artifact.asset, q, response);
			CHECK(status == (v.available ? Status::Ok : Status::Unavailable));
			if (v.available)
				CHECK(Length(V(response.value) - V(v.value)) == 0);
			auto patch = std::find_if(rig.scene.patches.begin(), rig.scene.patches.end(),
									  [&](const I::Patch &x) { return x.id == v.sample; });
			if (patch != rig.scene.patches.end() && originalIds.count(v.source))
			{
				auto root = std::find_if(
					independentRoots.fixedRoots.begin(), independentRoots.fixedRoots.end(),
					[&](const I::Root &r) { return r.source == originalIds.at(v.source); });
				CHECK(root != independentRoots.fixedRoots.end());
				if (root != independentRoots.fixedRoots.end())
					maximumAttributionError = std::max(
						maximumAttributionError,
						Length(V(v.value) -
							   V(root->incident.at(size_t(patch - rig.scene.patches.begin())))));
			}
		}
		if (ref.sampleDomain != result.artifact.asset->assessment.sampleDomain)
		{
			++observed;
			A::Ledger ledger;
			CHECK(p->Audit(ref.sampleDomain, values[0].sample, ledger) == Status::Ok &&
				  ledger.attributionComplete);
			CHECK(!A::MayReplace(*result.artifact.asset, *p, A::ReplacementRequest{}));
		}
	}
	CHECK(observed == 2);
	CHECK(final.calls == 1);
	CHECK(maximumAttributionError < 1e-5);
	std::ostringstream entry;
	entry << "{\"name\":\"production_two_colour_observation_attribution\",\"observation_root_"
			 "datasets\":2,\"exact_replacement\":false,\"maximum_root_reference_error\":"
		  << maximumAttributionError << ",\"prepared_samples\":" << preparedWork.samples
		  << ",\"prepared_records\":" << preparedWork.records
		  << ",\"prepared_accounted_bytes\":" << preparedWork.bytes
		  << ",\"decomposition_rays\":" << result.work.decomposition.rays
		  << ",\"decomposition_forward_calls\":" << result.work.decomposition.forwardCalls << "}";
	cases.push_back(entry.str());
}
int main(int argc, char **argv)
{
	try
	{
		QueriesAndPhysics();
		SparseAndUnknown();
		Composition();
		UnsafeReplacement();
		EncodedCoupling();
		GeometryReceipts();
		RealProducer();
	}
	catch (const Error &e)
	{
		++failures;
		std::printf("ERROR %s\n", e.message.c_str());
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	if (argc > 1)
	{
		std::ofstream out(argv[1]);
		out << "{\"schema\":\"org.openjk.contribution-tests.v1\",\"checks\":" << checks
			<< ",\"failures\":" << failures << ",\"cases\":[";
		for (size_t i = 0; i < cases.size(); ++i)
			out << (i ? "," : "") << cases[i];
		out << "]}\n";
	}
	std::printf("P06: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
