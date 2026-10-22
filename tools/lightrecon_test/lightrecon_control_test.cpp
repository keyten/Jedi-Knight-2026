// CPU-only policy tests. Synthetic providers never read maps or allocate GPU data.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "../../shared/rd-rend2/lightrecon/control.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace Rend2LightRecon;
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
Estimate Known(float x) { Estimate v; v.known = true; v.value = x; return v; }
Source SourceAt(Id id)
{
	Source s; s.id = id; s.provenance = 1; s.state = HypothesisState::Resolved; return s;
}
PhysicalEmitter Emitter(Id id, EmitterKind kind)
{
	PhysicalEmitter e; e.source = SourceAt(id); e.kind = kind;
	e.source.evidence.exactLight = Suitability::Candidate;
	e.position.known = kind != EmitterKind::Directional; e.frame.known = true;
	e.geometry.known = true; e.geometry.halfWidthWorld = 2; e.geometry.halfHeightWorld = 1;
	e.geometry.halfLengthWorld = 3; e.geometry.radiusWorld = 0.5f;
	e.geometry.effectiveRadiusWorld = Known(1);
	e.emission.intensity = Known(100); e.emission.calibration = 1;
	NormalizeChromaticity(Vec3(1, 1, 1), e.emission.color);
	e.emission.unit = kind == EmitterKind::Directional ? PhotometricUnit::Lux :
		kind == EmitterKind::Rectangle || kind == EmitterKind::Tube ? PhotometricUnit::Nits : PhotometricUnit::Candela;
	e.angular.kind = kind == EmitterKind::Directional ? AngularKind::Parallel :
		kind == EmitterKind::Rectangle || kind == EmitterKind::Tube ? AngularKind::Lambertian : AngularKind::Isotropic;
	return e;
}
ReconstructedLightingAsset Fixture()
{
	ReconstructedLightingAsset a; a.lineage = "controls/synthetic"; a.provenance = 1;
	Provenance p; p.id = 1; p.kind = ProvenanceKind::Authored; p.producer = "controls-test/1"; a.provenanceRecords.push_back(p);
	Calibration c; c.id = 1; c.kind = CalibrationKind::PhysicallyAuthored; c.provenance = 1; a.calibrations.push_back(c);
	a.emitters.push_back(Emitter(10, EmitterKind::Point));
	a.emitters.push_back(Emitter(11, EmitterKind::Rectangle));
	a.emitters.push_back(Emitter(12, EmitterKind::Directional));
	SunSource sun; sun.emitter = 12; a.suns.push_back(sun);
	EnvironmentLighting sky; sky.source = SourceAt(20); sky.meanLuminance.calibration = 1;
	sky.meanLuminance.unit = PhotometricUnit::Nits; a.environments.push_back(sky);
	TransportField field; field.source = SourceAt(30); field.unit = PhotometricUnit::Lux; field.calibration = 1;
	a.transport.push_back(field);
	Dataset d; d.id = 1; d.purpose = DatasetPurpose::Visibility; d.encoding = "test/prepared";
	d.layout = DatasetLayout::Sparse; d.locator = "never-opened://visibility"; a.datasets.push_back(d);
	VisibilityReference v; v.dataset = 1; a.visibility.push_back(v);
	return a;
}
struct Prepared : CpuEvaluator
{
	mutable unsigned calls = 0;
	Status support = Status::Ok;
	Status Supports(Capability) const override { ++calls; return support; }
};
struct Backend : BuildBackend
{
	unsigned caches = 0, builds = 0;
	Status cache = Status::Ok, build = Status::Ok;
	bool throwCache = false, throwBuild = false, noProduct = false;
	BuildProduct product;
	std::shared_ptr<const ReconstructedLightingAsset> cacheAssetOverride;
	Backend() { product.asset = std::make_shared<const ReconstructedLightingAsset>(Fixture()); }
	Status LoadCache(BuildProduct& out) override
	{
		++caches; if (throwCache) throw std::runtime_error("cache failure");
		if (!noProduct) { out = product; if (cacheAssetOverride) out.asset = cacheAssetOverride; }
		return cache;
	}
	Status Build(BuildProduct& out) override
	{ ++builds; if (throwBuild) throw std::runtime_error("build failure"); if (!noProduct) out = product; return build; }
};
void Policies()
{
	const Status outcomes[] = { Status::Ok, Status::Missing, Status::Unavailable, Status::Unsupported, Status::Corrupt };
	for (int policy = 0; policy != 4; ++policy)
		for (Status cache : outcomes) for (Status build : outcomes)
		{
			Manager owner; Backend b; b.cache = cache; b.build = build;
			CHECK(owner.Publish(Fixture()) == Status::Ok);
			const Snapshot old = owner.Acquire();
			const BuildReport r = ApplyBuildPolicy(static_cast<BuildPolicy>(policy), owner, &b);
			const bool cacheCalled = policy == 1 || policy == 2;
			const bool buildCalled = policy == 3 || (policy == 2 && cache != Status::Ok);
			const Status expected = policy == 0 ? Status::Disabled : buildCalled ? build : cache;
			CHECK(b.caches == (cacheCalled ? 1u : 0u)); CHECK(b.builds == (buildCalled ? 1u : 0u));
			CHECK(r.cacheCalled == cacheCalled); CHECK(r.buildCalled == buildCalled); CHECK(r.status == expected);
			CHECK(r.cacheStatus == (cacheCalled ? cache : Status::Unavailable));
			CHECK(r.buildStatus == (buildCalled ? build : Status::Unavailable));
			CHECK(static_cast<bool>(owner.Acquire().Asset()) == (expected == Status::Ok));
			CHECK(owner.Acquire().State() == (expected == Status::Ok ? Availability::Partial :
				expected == Status::Disabled ? Availability::Disabled : expected == Status::Corrupt ? Availability::Corrupt : Availability::Missing));
			CHECK(old.Asset().value->emitters.size() == 3); CHECK(old.Generation() != owner.Acquire().Generation());
		}
	Manager owner;
	for (int policy = 0; policy != 4; ++policy)
	{
		const auto r = ApplyBuildPolicy(static_cast<BuildPolicy>(policy), owner);
		CHECK(!r.cacheCalled && !r.buildCalled); CHECK(r.status == (policy == 0 ? Status::Disabled : Status::Unavailable));
	}
	Backend b;
	CHECK(ApplyBuildPolicy(static_cast<BuildPolicy>(-1), owner, &b).status == Status::InvalidArgument);
	CHECK(ApplyBuildPolicy(static_cast<BuildPolicy>(4), owner, &b).status == Status::InvalidArgument);
	CHECK(b.caches == 0 && b.builds == 0);
	b.noProduct = true;
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOnly, owner, &b).status == Status::Corrupt);
	b.noProduct = false; b.cache = Status::Pending;
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOnly, owner, &b).status == Status::Corrupt);
	b.cache = Status::Ok; b.throwCache = true;
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOnly, owner, &b).status == Status::Corrupt);
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOrBuild, owner, &b).status == Status::Ok);
	b.throwBuild = true;
	CHECK(ApplyBuildPolicy(BuildPolicy::ForceRebuild, owner, &b).status == Status::Corrupt);
	b.throwCache = b.throwBuild = false;
	auto invalid = Fixture(); invalid.version = 999;
	b.product.asset = std::make_shared<const ReconstructedLightingAsset>(invalid);
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOnly, owner, &b).status == Status::Corrupt);
	CHECK(owner.Acquire().State() == Availability::Corrupt);
	b.cacheAssetOverride = b.product.asset;
	b.product.asset = std::make_shared<const ReconstructedLightingAsset>(Fixture());
	const auto rebuilt = ApplyBuildPolicy(BuildPolicy::CacheOrBuild, owner, &b);
	CHECK(rebuilt.cacheStatus == Status::Corrupt && rebuilt.buildStatus == Status::Ok);
	CHECK(rebuilt.cacheCalled && rebuilt.buildCalled && rebuilt.status == Status::Ok);
	CHECK(owner.Acquire().Asset().value->emitters.size() == 3);
	b.cacheAssetOverride.reset();
	b.product.asset = std::make_shared<const ReconstructedLightingAsset>();
	CHECK(ApplyBuildPolicy(BuildPolicy::ForceRebuild, owner, &b).status == Status::Ok);
	CHECK(owner.Acquire().Asset().value->emitters.empty());
	b.product.completeness = Completeness::Ready;
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOnly, owner, &b).status == Status::Ok);
	CHECK(owner.Acquire().State() == Availability::Ready);
	b.product.completeness = static_cast<Completeness>(99);
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOnly, owner, &b).status == Status::Corrupt);
}
void Consumers()
{
	Manager owner; auto evaluator = std::make_shared<Prepared>();
	CHECK(owner.Publish(Fixture(), Completeness::Partial, evaluator) == Status::Ok);
	const Snapshot snapshot = owner.Acquire(); const auto asset = snapshot.Asset().value;
	const unsigned calls = evaluator->calls;
	UseSettings settings; FeatureSupport yes; yes.available = true; FeatureSupport no;
	for (size_t i = 0; i < kConsumerCount; ++i)
	{
		const Consumer consumer = static_cast<Consumer>(i);
		CHECK(EvaluateConsumer(snapshot, settings, consumer, yes).reason == GateReason::NotRequested);
	}
	settings.requested.fill(true);
	for (size_t i = 0; i < kConsumerCount; ++i)
	{
		const auto state = EvaluateConsumer(snapshot, settings, static_cast<Consumer>(i), yes);
		CHECK(state.reason == GateReason::MasterDisabled); CHECK(!state.effective);
		CHECK(state.assetLoaded && state.capabilityPresent && state.featureAvailable);
	}
	settings.master = true;
	for (size_t off = 0; off < kConsumerCount; ++off)
	{
		settings.requested[off] = false;
		for (size_t i = 0; i < kConsumerCount; ++i)
		{
			const auto state = EvaluateConsumer(snapshot, settings, static_cast<Consumer>(i), yes);
			CHECK(state.effective == (off != i));
			CHECK(state.reason == (off == i ? GateReason::NotRequested : GateReason::Enabled));
		}
		settings.requested[off] = true;
	}
	for (unsigned repeat = 0; repeat < 50; ++repeat)
		for (size_t i = 0; i < kConsumerCount; ++i)
		{
			CHECK(EvaluateConsumer(snapshot, settings, static_cast<Consumer>(i), yes).effective);
			const auto state = EvaluateConsumer(snapshot, settings, static_cast<Consumer>(i), no);
			CHECK(state.reason == GateReason::FeatureUnavailable); CHECK(!state.effective);
		}
	CHECK(evaluator->calls == calls); // not even Supports is called from a control query
	CHECK(owner.Acquire().Asset().value.get() == asset.get()); CHECK(owner.Acquire().Generation() == snapshot.Generation());
	CHECK(asset->emitters[0].emission.intensity.value == 100); CHECK(asset->emitters[0].source.evidence.exactLight == Suitability::Candidate);
	CHECK(EvaluateConsumer(snapshot, settings, Consumer::Count, yes).reason == GateReason::InvalidConsumer);
	CHECK(EvaluateConsumer(snapshot, settings, static_cast<Consumer>(-1), yes).reason == GateReason::InvalidConsumer);
	const Availability unavailable[] = { Availability::Disabled, Availability::Missing, Availability::Pending, Availability::Corrupt, Availability::Unloaded };
	for (Availability state : unavailable)
	{
		CHECK(owner.SetUnavailable(state) == Status::Ok);
		for (size_t i = 0; i < kConsumerCount; ++i)
		{
			const auto result = EvaluateConsumer(owner.Acquire(), settings, static_cast<Consumer>(i), yes);
			CHECK(result.reason == GateReason::AssetUnavailable); CHECK(!result.assetLoaded && !result.effective);
		}
	}
	CHECK(EvaluateConsumer(snapshot, settings, Consumer::Area, yes).effective); // old retained lease still safe
	CHECK(owner.Publish(ReconstructedLightingAsset(), Completeness::Ready) == Status::Ok);
	for (size_t i = 0; i < kConsumerCount; ++i)
	{
		const auto state = EvaluateConsumer(owner.Acquire(), settings, static_cast<Consumer>(i), yes);
		CHECK(state.assetLoaded && !state.capabilityPresent && !state.effective); CHECK(state.reason == GateReason::CapabilityUnavailable);
	}
	CHECK(owner.Publish(Fixture()) == Status::Ok);
	CHECK(EvaluateConsumer(owner.Acquire(), settings, Consumer::Indirect, yes).capabilityStatus == Status::Unavailable);
	CHECK(EvaluateConsumer(owner.Acquire(), settings, Consumer::Shadows, yes).capabilityStatus == Status::Unavailable);
	evaluator->support = Status::Unsupported;
	CHECK(owner.Publish(Fixture(), Completeness::Partial, evaluator) == Status::Ok);
	CHECK(EvaluateConsumer(owner.Acquire(), settings, Consumer::Specular, yes).capabilityStatus == Status::Unsupported);
	// Metadata/candidates/unknown emission alone must never enable exact sources.
	for (unsigned mode = 0; mode < 5; ++mode)
	{
		auto a = Fixture(); a.emitters.resize(1); a.suns.clear();
		if (mode == 0) a.emitters[0].source.state = HypothesisState::Candidate;
		if (mode == 1) a.emitters[0].source.evidence.exactLight = Suitability::Unknown;
		if (mode >= 2) a.emitters[0].source.evidence.exactLight = Suitability::Unknown;
		if (mode == 2) a.emitters[0].emission.intensity.known = false;
		if (mode == 3) a.emitters[0].emission.color.known = false;
		if (mode == 4) a.emitters[0].position.known = false;
		CHECK(owner.Publish(a) == Status::Ok);
		CHECK(!EvaluateConsumer(owner.Acquire(), settings, Consumer::WorldDiffuse, yes).effective);
		CHECK(!EvaluateConsumer(owner.Acquire(), settings, Consumer::Area, yes).effective);
		CHECK(owner.Acquire().GetCapability(Capability::PhysicalEmitters).status == Status::Ok);
	}
	auto a = Fixture(); a.emitters.erase(a.emitters.begin() + 1); // point/directional is not an LTC area source
	CHECK(owner.Publish(a) == Status::Ok);
	CHECK(!EvaluateConsumer(owner.Acquire(), settings, Consumer::Area, yes).effective);
	CHECK(EvaluateConsumer(owner.Acquire(), settings, Consumer::WorldDiffuse, yes).effective);
	a = Fixture(); a.emitters.push_back(Emitter(13, EmitterKind::Directional));
	SunSource sun; sun.emitter = 13; a.suns.push_back(sun);
	CHECK(owner.Publish(a) == Status::Ok);
	CHECK(EvaluateConsumer(owner.Acquire(), settings, Consumer::Sun, yes).capabilityStatus == Status::Ambiguous);
}

// Compile the actual shared registration body against both engine signatures.
// SP discards descriptions; MP preserves them. The same body is included by the
// two real R_Register implementations, compiled again in the renderer builds.
constexpr int CVAR_ARCHIVE = 1, CVAR_LATCH = 32;
constexpr bool qtrue = true;
struct cvar_t { std::string name, value, description; int flags = 0, maximum = -1; };
struct Registration
{
	std::array<cvar_t, 12> records; size_t count = 0;
	cvar_t* Get(const char* name, const char* value, int flags, const char* description = "")
	{
		if (count == records.size()) throw std::runtime_error("too many cvars");
		cvar_t& c = records[count++]; c.name = name; c.value = value; c.flags = flags; c.description = description; return &c;
	}
	void Cvar_CheckRange(cvar_t* c, int minimum, int maximum, bool integral)
	{ CHECK(minimum == 0); CHECK(integral); c->maximum = maximum; }
};
#define LIGHTRECON_CONTROL(name, maximum, description) cvar_t *name;
#define LIGHTRECON_CONSUMER(name, kind, description) cvar_t *name;
#include "../../shared/rd-rend2/tr_lightrecon_cvars.inc"
#undef LIGHTRECON_CONSUMER
#undef LIGHTRECON_CONTROL
namespace SP {
struct Imports : Registration { cvar_t* Cvar_Get(const char* n, const char* v, int f) { return Get(n, v, f); } } ri;
void Register()
{
#define LIGHTRECON_GET(n, v, f, d) ri.Cvar_Get(n, v, f)
#include "../../shared/rd-rend2/tr_lightrecon_register.inc"
#undef LIGHTRECON_GET
}
}
namespace MP {
struct Imports : Registration { cvar_t* Cvar_Get(const char* n, const char* v, int f, const char* d) { return Get(n, v, f, d); } } ri;
void Register()
{
#define LIGHTRECON_GET ri.Cvar_Get
#include "../../shared/rd-rend2/tr_lightrecon_register.inc"
#undef LIGHTRECON_GET
}
}
void Registrations()
{
	SP::Register(); MP::Register(); CHECK(SP::ri.count == 12 && MP::ri.count == 12);
	for (size_t i = 0; i < 12; ++i)
	{
		const auto& s = SP::ri.records[i]; const auto& m = MP::ri.records[i];
		CHECK(s.name == m.name); CHECK(s.value == "0" && m.value == "0");
		CHECK(s.flags == CVAR_ARCHIVE && m.flags == CVAR_ARCHIVE); CHECK(!(s.flags & CVAR_LATCH));
		CHECK(s.maximum == (i == 0 ? 3 : 1) && m.maximum == s.maximum);
		CHECK(s.description.empty()); CHECK(!m.description.empty());
		if (i >= 2) CHECK(s.name == ConsumerCvar(static_cast<Consumer>(i - 2)));
		for (size_t j = 0; j < i; ++j) CHECK(s.name != SP::ri.records[j].name);
	}
	CHECK(MP::ri.records[0].description.find("next map load/reload") != std::string::npos);
}
} // namespace
int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	static_assert(std::is_const<typename ConstRecord<ReconstructedLightingAsset>::element_type>::value, "consumer asset is read only");
	std::string error;
	if (!Validate(Fixture(), &error)) { std::printf("Invalid test fixture: %s\n", error.c_str()); return 1; }
	Policies(); Consumers(); Registrations();
	std::printf("lightrecon controls: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
