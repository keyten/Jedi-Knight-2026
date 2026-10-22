// Synthetic CPU API tests: no renderer, disk dataset loader or graphics library.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "../../shared/rd-rend2/tr_lightrecon_manager.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace {
std::atomic<bool> countAllocations(false);
std::atomic<size_t> allocations(0);
}
void* operator new(std::size_t size)
{
	if (countAllocations.load()) ++allocations;
	if (void* p = std::malloc(size ? size : 1)) return p;
	throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }

using namespace Rend2LightRecon;
static_assert(std::is_const<typename ConstRecord<PhysicalEmitter>::element_type>::value, "emitter leases must be const");
static_assert(std::is_const<typename std::remove_reference<decltype((std::declval<ConstRecord<PhysicalEmitter>>()->emission))>::type>::value,
	"nested geometry/intensity must be const");
static_assert(std::is_const<typename std::remove_reference<decltype(*std::declval<ConstArray<GeometryRegion>>().begin())>::type>::value,
	"array iterators must be const");
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
Estimate Known(float x) { Estimate e; e.known = true; e.value = x; return e; }
Source SourceAt(Id id)
{
	Source s; s.id = id; s.provenance = 1; s.state = HypothesisState::Resolved;
	s.evidence.transportConfidence = Known(0.9f); s.evidence.physicalEmitterConfidence = Known(0.25f); return s;
}
Dataset Data(Id id, DatasetPurpose purpose)
{
	Dataset d; d.id = id; d.purpose = purpose; d.locator = "missing://must-never-be-read";
	d.encoding = "test/prepared-only"; d.chunkElements = 4; return d;
}
ReconstructedLightingAsset Fixture()
{
	ReconstructedLightingAsset a; a.lineage = "synthetic-map/persistent-lineage"; a.provenance = 1;
	Provenance p; p.id = 1; p.kind = ProvenanceKind::Authored; p.producer = "synthetic-api-test/1";
	a.provenanceRecords.push_back(p);
	Calibration c; c.id = 1; c.kind = CalibrationKind::PhysicallyAuthored; c.provenance = 1; a.calibrations.push_back(c);
	for (unsigned kind = 1; kind <= 5; ++kind)
	{
		PhysicalEmitter e; e.source = SourceAt(1000 + kind); e.kind = static_cast<EmitterKind>(kind);
		e.position.known = kind != 5; e.position.sigmaWorld = Known(2.0f); e.frame.known = true;
		e.geometry.known = true; e.geometry.halfWidthWorld = 2.0f; e.geometry.halfHeightWorld = 1.0f;
		e.geometry.halfLengthWorld = 3.0f; e.geometry.radiusWorld = 0.5f; e.geometry.effectiveRadiusWorld = Known(1.0f);
		e.emission.intensity = Known(100.0f); e.emission.calibration = 1;
		e.emission.intensity.bounded = true; e.emission.intensity.lower = 90.0f; e.emission.intensity.upper = 110.0f;
		e.emission.unit = kind <= 2 ? PhotometricUnit::Candela : kind == 5 ? PhotometricUnit::Lux : PhotometricUnit::Nits;
		NormalizeChromaticity(Vec3(1.0f, 1.0f, 1.0f), e.emission.color);
		e.angular.kind = kind == 1 ? AngularKind::Isotropic : kind == 2 ? AngularKind::SmoothCone : kind == 5 ? AngularKind::Parallel : AngularKind::Lambertian;
		e.source.evidence.exactLight = Suitability::Candidate;
		if (kind == 2) e.source.state = HypothesisState::Candidate;
		if (kind == 4) { e.source.state = HypothesisState::Rejected; e.source.evidence.exactLight = Suitability::Unsuitable; }
		e.source.regions.push_back(kind == 1 ? 7 : 8);
		if (kind == 5) { e.frame.up = Vec3(0.0f, -1.0f, 0.0f); e.frame.forward = Vec3(0.0f, 0.0f, -1.0f); }
		a.emitters.push_back(e);
	}
	SunSource sun; sun.emitter = 1005; a.suns.push_back(sun);
	EnvironmentLighting sky; sky.source = SourceAt(2001); sky.source.state = HypothesisState::Candidate;
	sky.meanLuminance.calibration = 1; sky.meanLuminance.unit = PhotometricUnit::Nits; a.environments.push_back(sky);
	TransportField f; f.source = SourceAt(3001); f.unit = PhotometricUnit::Lux; f.calibration = 1; a.transport.push_back(f);
	f.source = SourceAt(3002); f.component = TransportComponent::UnattributedIncidentRadiance;
	f.quantity = FieldQuantity::Radiance; f.unit = PhotometricUnit::Nits; a.transport.push_back(f);
	for (Id id = 7; id <= 8; ++id)
	{
		GeometryRegion r; r.id = id; r.provenance = 1; r.bounds.known = true;
		r.bounds.maxWorld = Vec3(10.0f, 10.0f, 10.0f); a.regions.push_back(r); // overlapping AABBs are not a region classifier
	}
	a.datasets.push_back(Data(10, DatasetPurpose::Visibility));
	a.datasets.push_back(Data(11, DatasetPurpose::Contributions));
	a.datasets.push_back(Data(12, DatasetPurpose::SampleDomain));
	VisibilityReference v; v.dataset = 10; a.visibility.push_back(v);
	v.kind = VisibilityKind::SkyTransmittance; a.visibility.push_back(v);
	ContributionReference contribution; contribution.dataset = 11; contribution.sampleDomain = 12;
	contribution.sources.push_back(1001); contribution.component = ContributionComponent::Direct;
	contribution.unit = PhotometricUnit::Lux; contribution.calibration = 1; a.contributions.push_back(contribution);
	return a;
}
size_t Count(EmitterCursor cursor)
{
	size_t n = 0;
	for (auto e = cursor.Next(); e; e = cursor.Next()) ++n;
	CHECK(cursor.Next().status == Status::NotFound); return n;
}

// Test-only, manually supplied CPU values. This is not a fitted model, SH
// evaluator, light falloff, visibility tracer or production fallback.
struct PreparedValues : CpuEvaluator
{
	mutable unsigned supportsCalls = 0, queryCalls = 0;
	mutable Vec3 lastDirection, lastNormal;
	Status support = Status::Ok, result = Status::Ok;
	LightingSample indirect, radiance;
	float transmittance = 0.75f, fraction = 0.4f;
	Id region = 7;
	PreparedValues()
	{
		indirect.source = 3001; indirect.rgb = Vec3(1.0f, 2.0f, 3.0f); indirect.unit = PhotometricUnit::Lux; indirect.calibration = 1;
		radiance = indirect; radiance.source = 3002; radiance.unit = PhotometricUnit::Nits;
	}
	Status Supports(Capability) const override { ++supportsCalls; return support; }
	Status Indirect(const ReconstructedLightingAsset&, const IrradianceQuery& q, LightingSample& out) const override
	{ ++queryCalls; lastNormal = q.normalWorld; out = indirect; return result; }
	Status IncomingRadiance(const ReconstructedLightingAsset&, const RadianceQuery& q, LightingSample& out) const override
	{ ++queryCalls; lastDirection = q.directionToSource; out = radiance; return result; }
	Status RegionAt(const ReconstructedLightingAsset&, const Vec3&, Id& out) const override
	{ ++queryCalls; out = region; return result; }
	Status Visibility(const ReconstructedLightingAsset&, const VisibilityQuery&, VisibilitySample& out) const override
	{ ++queryCalls; out.transmittance = transmittance; return result; }
	Status SkyVisibility(const ReconstructedLightingAsset&, const SkyVisibilityQuery& q, VisibilitySample& out) const override
	{ ++queryCalls; lastDirection = q.directionToSky; out.transmittance = transmittance; return result; }
	Status Contribution(const ReconstructedLightingAsset&, const ContributionQuery& q, ContributionSample& out) const override
	{ ++queryCalls; out.lighting = indirect; out.lighting.source = q.source.id; out.fraction = fraction; return result; }
};
IrradianceQuery Irradiance() { IrradianceQuery q; q.normalWorld = Vec3(0.0f, 0.0f, 1.0f); return q; }
RadianceQuery Radiance() { RadianceQuery q; q.directionToSource = Vec3(0.0f, 1.0f, 0.0f); return q; }
VisibilityQuery Visibility() { VisibilityQuery q; q.targetWorld = Vec3(1.0f, 0.0f, 0.0f); return q; }
SkyVisibilityQuery SkyVisibility() { SkyVisibilityQuery q; q.directionToSky = Vec3(0.0f, 0.0f, 1.0f); return q; }
ContributionQuery Contribution(const Snapshot& snapshot)
{
	ContributionQuery q; q.sampleDomain = 12; q.sample = 999999; q.source = snapshot.IdentifySource(1001).value;
	q.component = ContributionComponent::Direct; return q;
}
void StatesAndEmpty()
{
	Snapshot absent; CHECK(absent.State() == Availability::Missing); CHECK(absent.Asset().status == Status::Missing);
	CHECK(absent.Sun().status == Status::Missing); CHECK(absent.Weak().Lock().status == Status::Expired);
	Manager m;
	const Availability states[] = { Availability::Disabled, Availability::Missing, Availability::Pending, Availability::Corrupt, Availability::Unloaded };
	const Status statuses[] = { Status::Disabled, Status::Missing, Status::Pending, Status::Corrupt, Status::Unloaded };
	for (size_t i = 0; i < 5; ++i)
	{
		CHECK(m.SetUnavailable(states[i], "state reason") == Status::Ok);
		const Snapshot s = m.Acquire(); CHECK(s.State() == states[i]); CHECK(s.Message() == "state reason");
		CHECK(s.Asset().status == statuses[i]); CHECK(!s.Asset().value);
		CHECK(s.Emitters().status == statuses[i]); CHECK(s.SampleIndirect(Irradiance()).status == statuses[i]);
		CHECK(s.GetCapability(Capability::VisibilityData).status == statuses[i]);
	}
	CHECK(m.SetUnavailable(Availability::Ready) == Status::InvalidArgument);
	CHECK(m.SetUnavailable(Availability::Partial) == Status::InvalidArgument);
	CHECK(m.SetUnavailable(static_cast<Availability>(999)) == Status::InvalidArgument);
	CHECK(m.Publish(ReconstructedLightingAsset(), Completeness::Ready) == Status::Ok);
	const auto s = m.Acquire(); CHECK(s.State() == Availability::Ready); CHECK(s.Asset());
	CHECK(s.GetCapability(Capability::AssetMetadata).status == Status::Ok);
	CHECK(s.GetCapability(Capability::PhysicalEmitters).status == Status::Unavailable);
	CHECK(Count(s.Emitters().value) == 0); CHECK(s.Sun().status == Status::Unavailable); CHECK(s.Sky().status == Status::Unavailable);
	CHECK(s.Regions().value.empty()); CHECK(s.Regions().value.At(0).status == Status::NotFound);
	CHECK(EmitterCursor().Next().status == Status::Unavailable);
	CHECK(s.IdentifySource(0).status == Status::InvalidArgument); CHECK(s.IdentifySource(1001).status == Status::NotFound);
	CHECK(s.GetCapability(Capability::Count).status == Status::InvalidArgument);
}
void EnumerationAndMetadata()
{
	auto a = Fixture(); CHECK(Validate(a)); Manager m; CHECK(m.Publish(a) == Status::Ok);
	auto s = m.Acquire(); CHECK(s.State() == Availability::Partial); CHECK(Count(s.Emitters().value) == 4);
	EmitterFilter f; f.states = 7; CHECK(Count(s.Emitters(f).value) == 5);
	for (unsigned kind = 1; kind <= 5; ++kind)
	{ f.byKind = true; f.kind = static_cast<EmitterKind>(kind); CHECK(Count(s.Emitters(f).value) == 1); }
	f = EmitterFilter(); f.region = 7; CHECK(Count(s.Emitters(f).value) == 1);
	f.region = 99; CHECK(s.Emitters(f).status == Status::NotFound);
	f = EmitterFilter(); f.minimumPhysicalConfidence = 0.5f; CHECK(Count(s.Emitters(f).value) == 0);
	f.minimumPhysicalConfidence = 0.25f; CHECK(Count(s.Emitters(f).value) == 4);
	f.minimumTransportConfidence = 1.1f; CHECK(s.Emitters(f).status == Status::InvalidArgument);
	f = EmitterFilter(); f.states = 8; CHECK(s.Emitters(f).status == Status::InvalidArgument);
	f = EmitterFilter(); f.sunOnly = true; CHECK(Count(s.Emitters(f).value) == 1);
	f.exactCandidatesOnly = true; CHECK(Count(s.Emitters(f).value) == 1);
	const auto source = s.IdentifySource(1001); CHECK(source);
	const auto emitter = s.GetEmitter(source.value); CHECK(emitter && emitter.value->source.id == 1001);
	CHECK(s.GetEmitter(s.IdentifySource(3001).value).status == Status::NotFound);
	a.emitters[0].emission.intensity.value = 777.0f;
	CHECK(emitter.value->emission.intensity.value == 100.0f); // publication copied, no writable builder alias
	auto quality = s.Quality(source.value); CHECK(quality && quality.value.emitter);
	CHECK(quality.value.source->evidence.transportConfidence.value == 0.9f);
	CHECK(quality.value.emitter->position.sigmaWorld.value == 2.0f);
	CHECK(quality.value.emitter->emission.intensity.bounded && quality.value.emitter->emission.intensity.lower == 90.0f);
	CHECK(s.Quality(s.IdentifySource(3001).value).value.emitter == nullptr);
	CHECK(s.GetRegion(s.IdentifyRegion(7).value).value->id == 7);
	CHECK(s.GetCapability(Capability::PhysicalEmitters).records == 5);
	CHECK(s.GetCapability(Capability::IndirectField).status == Status::Ok);
	CHECK(s.GetCapability(Capability::VisibilityData).status == Status::Ok);
	CHECK(s.GetCapability(Capability::SourceContributions).status == Status::Ok);
	CHECK(s.TransportFields().value.size() == 2); CHECK(s.Environments().value.size() == 1);
	CHECK(s.VisibilityData().value.size() == 2); CHECK(s.Contributions().value.size() == 1); CHECK(s.Diagnostics().value.empty());
	for (unsigned i = 0; i < 100; ++i)
		CHECK(s.GetEmitter(source.value).value.get() == emitter.value.get());
}
void ResolvedAndConventions()
{
	auto a = Fixture(); Manager m; CHECK(m.Publish(a) == Status::Ok);
	auto s = m.Acquire(); auto sun = s.Sun(); CHECK(sun);
	CHECK(sun.value.directionToSun.z == 1.0f && sun.value.emitter->frame.forward.z == -1.0f);
	CHECK(s.Sky().status == Status::Unavailable); // candidate sky is not silently resolved
	a.emitters[4].source.state = HypothesisState::Candidate; CHECK(m.Publish(a) == Status::Ok);
	CHECK(m.Acquire().Sun().status == Status::Unavailable);
	a.emitters[4].source.state = HypothesisState::Resolved;
	a.emitters[4].source.evidence.exactLight = Suitability::Unknown; a.emitters[4].frame.known = false;
	CHECK(m.Publish(a) == Status::Ok); CHECK(m.Acquire().Sun().status == Status::Unavailable);
	a.emitters[4].frame.known = true;
	a.emitters.push_back(a.emitters[4]); a.emitters.back().source.id = 1015;
	SunSource second; second.emitter = 1015; a.suns.push_back(second);
	a.environments[0].source.state = HypothesisState::Resolved;
	CHECK(m.Publish(a) == Status::Ok); CHECK(m.Acquire().Sun().status == Status::Ambiguous); CHECK(m.Acquire().Sky());
	a.environments.push_back(a.environments[0]); a.environments.back().source.id = 2015;
	CHECK(m.Publish(a) == Status::Ok); CHECK(m.Acquire().Sky().status == Status::Ambiguous);
}
void LifetimeAndRebuild()
{
	Manager m; auto a = Fixture(); CHECK(m.Publish(a) == Status::Ok);
	SourceHandle oldId; RegionHandle oldRegion; WeakSnapshot weak; ConstRecord<PhysicalEmitter> pin;
	{
		auto old = m.Acquire(); oldId = old.IdentifySource(1001).value; oldRegion = old.IdentifyRegion(7).value;
		weak = old.Weak(); pin = old.GetEmitter(oldId).value;
		CHECK(m.Publish(a) == Status::Ok); CHECK(m.Acquire().Generation() != old.Generation());
		CHECK(m.Acquire().GetEmitter(oldId).status == Status::StaleHandle);
		CHECK(m.Acquire().GetRegion(oldRegion).status == Status::StaleHandle);
		CHECK(old.GetEmitter(oldId)); // pinned historical revision is still valid
		a.emitters[0].source.id = 1101; a.contributions[0].sources[0] = 1101;
		CHECK(m.Publish(a) == Status::Ok); CHECK(m.Acquire().IdentifySource(1001).status == Status::NotFound);
		CHECK(m.Acquire().IdentifySource(1101));
		CHECK(m.SetUnavailable(Availability::Unloaded) == Status::Ok);
		CHECK(m.Acquire().GetEmitter(oldId).status == Status::Unloaded); CHECK(old.GetEmitter(oldId));
	}
	CHECK(weak.Lock()); CHECK(pin->source.id == 1001); pin.reset(); CHECK(weak.Lock().status == Status::Expired);
	CHECK(m.Publish(Fixture()) == Status::Ok);
	auto s = m.Acquire(); auto view = s.Regions().value; auto viewWeak = s.Weak(); s = Snapshot();
	CHECK(m.SetUnavailable(Availability::Unloaded) == Status::Ok); CHECK(view.At(0)); CHECK(viewWeak.Lock());
	view = ConstArray<GeometryRegion>(); CHECK(viewWeak.Lock().status == Status::Expired);
	CHECK(m.Publish(Fixture()) == Status::Ok);
	auto before = m.Acquire(); auto invalid = Fixture(); invalid.emitters[0].geometry.effectiveRadiusWorld = Known(-1.0f);
	CHECK(m.Publish(invalid) == Status::Corrupt); CHECK(m.Acquire().State() == Availability::Corrupt);
	CHECK(!m.Acquire().Message().empty()); CHECK(!m.Acquire().Asset().value); CHECK(before.Asset());
	Manager other; CHECK(other.Publish(Fixture()) == Status::Ok);
	CHECK(other.Acquire().GetEmitter(before.IdentifySource(1001).value).status == Status::StaleHandle);
}
void UnavailableAndInvalidQueries()
{
	Manager m; CHECK(m.Publish(Fixture()) == Status::Ok); auto s = m.Acquire();
	CHECK(s.SampleIndirect(Irradiance()).status == Status::Unavailable);
	CHECK(s.SampleIncomingRadiance(Radiance()).status == Status::Unavailable);
	CHECK(s.LookupRegion(Vec3(1.0f, 1.0f, 1.0f)).status == Status::Unavailable); // no AABB-as-region guess
	CHECK(s.QueryVisibility(Visibility()).status == Status::Unavailable);
	CHECK(s.QuerySkyVisibility(SkyVisibility()).status == Status::Unavailable);
	CHECK(s.SampleContribution(Contribution(s)).status == Status::Unavailable);
	CHECK(s.GetCapability(Capability::IndirectSampling).status == Status::Unavailable);
	IrradianceQuery i; CHECK(s.SampleIndirect(i).status == Status::InvalidArgument);
	i = Irradiance(); i.normalWorld.z = 2.0f; CHECK(s.SampleIndirect(i).status == Status::InvalidArgument);
	i = Irradiance(); i.field = s.IdentifySource(1001).value; CHECK(s.SampleIndirect(i).status == Status::InvalidArgument);
	RadianceQuery r; CHECK(s.SampleIncomingRadiance(r).status == Status::InvalidArgument);
	CHECK(s.QueryVisibility(VisibilityQuery()).status == Status::InvalidArgument);
	CHECK(s.QuerySkyVisibility(SkyVisibilityQuery()).status == Status::InvalidArgument);
	CHECK(s.SampleContribution(ContributionQuery()).status == Status::InvalidArgument);
	const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
	for (float v : bad)
	{
		i = Irradiance(); i.positionWorld.x = v; CHECK(s.SampleIndirect(i).status == Status::InvalidArgument);
		r = Radiance(); r.directionToSource.y = v; CHECK(s.SampleIncomingRadiance(r).status == Status::InvalidArgument);
		VisibilityQuery q = Visibility(); q.receiverWorld.z = v; CHECK(s.QueryVisibility(q).status == Status::InvalidArgument);
		CHECK(s.LookupRegion(Vec3(v, 0.0f, 0.0f)).status == Status::InvalidArgument);
	}
}
void PreparedCpuBoundaryAndNoReadAllocations()
{
	Manager m; auto a = Fixture(); auto backend = std::make_shared<PreparedValues>();
	CHECK(m.Publish(a, Completeness::Ready, backend) == Status::Ok); CHECK(backend->supportsCalls == 6);
	auto s = m.Acquire(); CHECK(s.GetCapability(Capability::IndirectSampling).status == Status::Ok);
	CHECK(s.SampleIndirect(Irradiance()).value.rgb.y == 2.0f && backend->lastNormal.z == 1.0f);
	CHECK(s.SampleIncomingRadiance(Radiance()).value.unit == PhotometricUnit::Nits && backend->lastDirection.y == 1.0f);
	CHECK(s.LookupRegion(Vec3()).value.id == 7); CHECK(s.QueryVisibility(Visibility()).value.transmittance == 0.75f);
	CHECK(s.QuerySkyVisibility(SkyVisibility()).value.transmittance == 0.75f && backend->lastDirection.z == 1.0f);
	CHECK(s.SampleContribution(Contribution(s)).value.lighting.rgb.z == 3.0f);
	const auto handle = s.IdentifySource(1001).value;
	allocations = 0; countAllocations = true;
	for (unsigned n = 0; n < 100; ++n)
	{
		CHECK(m.Acquire().Generation() == s.Generation()); CHECK(s.Asset()); CHECK(s.GetEmitter(handle));
		CHECK(s.GetCapability(Capability::VisibilityData).status == Status::Ok);
		CHECK(s.Emitters().value.Next()); CHECK(s.Regions().value.At(0)); CHECK(s.Sun()); CHECK(s.Quality(handle));
		CHECK(s.SampleIndirect(Irradiance())); CHECK(s.SampleIncomingRadiance(Radiance()));
		CHECK(s.QueryVisibility(Visibility())); CHECK(s.SampleContribution(Contribution(s))); CHECK(s.LookupRegion(Vec3()));
		CHECK(s.QuerySkyVisibility(SkyVisibility()));
	}
	countAllocations = false; CHECK(allocations == 0); CHECK(backend->supportsCalls == 6);
	backend = std::make_shared<PreparedValues>(); backend->support = Status::Unsupported;
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	CHECK(m.Acquire().SampleIndirect(Irradiance()).status == Status::Unsupported); CHECK(backend->queryCalls == 0);
	backend = std::make_shared<PreparedValues>(); backend->indirect.rgb.x = std::numeric_limits<float>::quiet_NaN();
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	CHECK(m.Acquire().SampleIndirect(Irradiance()).status == Status::Corrupt);
	backend = std::make_shared<PreparedValues>(); backend->transmittance = 1.1f; backend->region = 999;
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	CHECK(m.Acquire().QueryVisibility(Visibility()).status == Status::Corrupt);
	CHECK(m.Acquire().LookupRegion(Vec3()).status == Status::Corrupt);
	backend = std::make_shared<PreparedValues>(); backend->result = Status::Unavailable;
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	CHECK(m.Acquire().SampleIndirect(Irradiance()).status == Status::Unavailable);
	CHECK(m.Acquire().SampleIndirect(Irradiance()).value.rgb.x == 0.0f); // failed provider output discarded
	backend = std::make_shared<PreparedValues>(); backend->support = Status::Ok;
	a.contributions[0].quantity = ContributionQuantity::AttributionFraction;
	a.contributions[0].unit = PhotometricUnit::Relative; a.contributions[0].calibration = 0;
	backend->fraction = -1.0f; CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	auto q = Contribution(m.Acquire()); q.quantity = ContributionQuantity::AttributionFraction;
	CHECK(m.Acquire().SampleContribution(q).status == Status::Corrupt);
}
void RemainingBoundaryCases()
{
	Manager m; auto a = Fixture(); auto backend = std::make_shared<PreparedValues>();
	CHECK(m.Publish(ReconstructedLightingAsset(), Completeness::Ready, backend) == Status::Ok);
	CHECK(backend->supportsCalls == 0 && backend->queryCalls == 0);
	CHECK(m.Acquire().SampleIndirect(Irradiance()).status == Status::Unavailable);
	a.emitters[0].source.evidence.physicalEmitterConfidence = Estimate();
	CHECK(m.Publish(a) == Status::Ok); auto s = m.Acquire();
	EmitterFilter f; f.region = 7; f.minimumPhysicalConfidence = 0.0f;
	CHECK(Count(s.Emitters(f).value) == 0); f.minimumPhysicalConfidence = -1.0f; CHECK(Count(s.Emitters(f).value) == 1);
	f.minimumPhysicalConfidence = -0.5f; CHECK(s.Emitters(f).status == Status::InvalidArgument);
	SourceHandle bad; bad.id = 1001; CHECK(s.GetEmitter(bad).status == Status::InvalidArgument);
	bad.generation = s.Generation(); bad.id = 999; CHECK(s.GetSource(bad).status == Status::NotFound);
	CHECK(m.Publish(a, static_cast<Completeness>(999)) == Status::InvalidArgument); CHECK(m.Acquire().Generation() == s.Generation());
	SkyVisibilityQuery sky = SkyVisibility(); sky.source = s.IdentifySource(1001).value;
	CHECK(s.QuerySkyVisibility(sky).status == Status::InvalidArgument);
	backend = std::make_shared<PreparedValues>(); backend->indirect.source = 999;
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	CHECK(m.Acquire().SampleIndirect(Irradiance()).status == Status::Corrupt);
	backend = std::make_shared<PreparedValues>(); backend->radiance.calibration = 999;
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	CHECK(m.Acquire().SampleIncomingRadiance(Radiance()).status == Status::Corrupt);
	backend = std::make_shared<PreparedValues>(); backend->support = Status::Pending;
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Corrupt); CHECK(m.Acquire().State() == Availability::Corrupt);
	backend = std::make_shared<PreparedValues>();
	for (auto& v : a.visibility) v.kind = VisibilityKind::RegionConnectivity;
	CHECK(m.Publish(a, Completeness::Partial, backend) == Status::Ok);
	CHECK(m.Acquire().GetCapability(Capability::VisibilityData).status == Status::Ok);
	CHECK(m.Acquire().QueryVisibility(Visibility()).status == Status::Unavailable);
	CHECK(m.Acquire().QuerySkyVisibility(SkyVisibility()).status == Status::Unavailable);
	WeakSnapshot weak; ConstRecord<PhysicalEmitter> pinned;
	{
		Manager temporary; CHECK(temporary.Publish(Fixture()) == Status::Ok);
		auto snapshot = temporary.Acquire(); weak = snapshot.Weak();
		pinned = snapshot.GetEmitter(snapshot.IdentifySource(1001).value).value;
	}
	CHECK(weak.Lock() && pinned->emission.intensity.value == 100.0f);
	pinned.reset(); CHECK(weak.Lock().status == Status::Expired);
}
} // namespace
int main()
{
	StatesAndEmpty(); EnumerationAndMetadata(); ResolvedAndConventions(); LifetimeAndRebuild();
	UnavailableAndInvalidQueries(); PreparedCpuBoundaryAndNoReadAllocations();
	RemainingBoundaryCases();
	std::printf("Reconstructed lighting API: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
