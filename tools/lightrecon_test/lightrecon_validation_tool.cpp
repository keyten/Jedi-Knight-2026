// V02 distributional query costs on real prepared CPU backends, not mock values.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/contributions.h"
#include "lightrecon/field_builder.h"
#include "lightrecon/photometry.h"
#include "lightrecon/shadow_cache.h"
#include "probe_fixtures.h"
#include "producer_test_scene.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace F = Fields;
bool counting = false;
uint64_t allocations = 0;
void *operator new(std::size_t n)
{
	if (counting)
		++allocations;
	if (auto p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void *operator new[](std::size_t n)
{
	return ::operator new(n);
}
void operator delete(void *p) noexcept
{
	std::free(p);
}
void operator delete[](void *p) noexcept
{
	std::free(p);
}
volatile double sink = 0;
template <class Operation>
uint64_t Measure(std::ostream &o, const char *name, Operation op, bool comma = true)
{
	const unsigned samples = 4096, batch = 8;
	std::vector<double> times(samples);
	uint64_t good = 0, bad = 0;
	for (unsigned j = 0; j < 128; ++j)
		op();
	allocations = 0;
	counting = true;
	for (unsigned i = 0; i < samples; ++i)
	{
		auto start = std::chrono::steady_clock::now();
		for (unsigned j = 0; j < batch; ++j)
		{
			if (op())
				++good;
			else
				++bad;
		}
		times[i] =
			std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
				.count() /
			batch;
	}
	counting = false;
	auto count = allocations;
	std::sort(times.begin(), times.end());
	if (comma)
		o << ',';
	o << '"' << name << "\":{\"samples\":" << samples << ",\"calls\":" << samples * batch
	  << ",\"ok\":" << good << ",\"unavailable\":" << bad << ",\"allocations\":" << count
	  << ",\"p50_ns\":" << times[samples / 2] << ",\"p95_ns\":" << times[size_t(samples * .95)]
	  << ",\"p99_ns\":" << times[size_t(samples * .99)] << ",\"max_ns\":" << times.back() << '}';
	return count;
}
ReconstructedLightingAsset Asset()
{
	ReconstructedLightingAsset a;
	a.lineage = "owned/V02/performance-known-physical-inputs";
	a.provenance = 1;
	a.units.worldUnitsPerMeter = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "V02 performance fixture, no source recovery claim";
	a.provenanceRecords = {p};
	Calibration c;
	c.id = 1;
	c.provenance = 1;
	c.kind = CalibrationKind::PhysicallyAuthored;
	a.calibrations = {c};
	return a;
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 2)
			throw std::runtime_error("validation_tool output.json");
		ProbeFixture::Scene scene;
		scene.domain->world = scene.options.bounds =
			ProbeFixture::Box(Vec3(-2, -2, 0), Vec3(2, 2, 4));
		scene.options.coarseSpacingWorld = 1;
		scene.options.minimumSpacingWorld = 1;
		scene.Quad(Vec3(-2, -2, 4), Vec3(2, -2, 4), Vec3(2, 2, 4), Vec3(-2, 2, 4), MaterialSky);
		scene.Quad(Vec3(-2, -2, 0), Vec3(-2, 2, 0), Vec3(2, 2, 0), Vec3(2, -2, 0), MaterialSky);
		scene.Quad(Vec3(-2, -2, 0), Vec3(-2, -2, 4), Vec3(-2, 2, 4), Vec3(-2, 2, 0), MaterialSky);
		scene.Quad(Vec3(2, -2, 0), Vec3(2, 2, 0), Vec3(2, 2, 4), Vec3(2, -2, 4), MaterialSky);
		scene.Quad(Vec3(-2, -2, 0), Vec3(2, -2, 0), Vec3(2, -2, 4), Vec3(-2, -2, 4), MaterialSky);
		scene.Quad(Vec3(-2, 2, 0), Vec3(-2, 2, 4), Vec3(2, 2, 4), Vec3(2, 2, 0), MaterialSky);
		auto input = scene.Input();
		auto topology = scene.Build(input);
		auto asset = Asset();
		EnvironmentLighting sky;
		sky.source.id = 30;
		sky.source.provenance = 1;
		sky.source.state = HypothesisState::Resolved;
		sky.meanLuminance.calibration = 1;
		sky.meanLuminance.unit = PhotometricUnit::Nits;
		sky.radiance = 31;
		sky.sunCoverage = SunCoverage::ExcludesExplicitSun;
		asset.environments = {sky};
		Dataset d;
		d.id = 31;
		d.purpose = DatasetPurpose::Environment;
		d.layout = DatasetLayout::Sparse;
		d.encoding = "org.openjk.angular";
		d.locator = "owned://V02/sky";
		F::Environment env;
		env.source = 30;
		env.dataset = 31;
		env.incoming.present = true;
		env.incoming.degree = 0;
		double root = std::sqrt(4 * 3.141592653589793);
		env.incoming.sh = {Angular::V{{root, root, root}}};
		std::vector<uint8_t> encoded;
		if (Angular::Encode(env.incoming, Angular::Storage::Float32, encoded) != Angular::Code::Ok)
			throw std::runtime_error("Angular encoding");
		env.content = Disk::Hash(encoded.data(), encoded.size());
		d.contentHash = Disk::Hex(env.content);
		asset.datasets = {d};
		F::Request q;
		q.asset = std::make_shared<const ReconstructedLightingAsset>(asset);
		q.topology = topology;
		q.visibility = input.visibility;
		q.environments = {env};
		q.selection.field = 1000;
		q.selection.dataset = 1001;
		q.selection.geometryDataset = 1002;
		q.selection.topologyDataset = 1003;
		q.selection.provenance = 1;
		q.selection.calibration = 1;
		q.selection.sky = true;
		q.selection.sources = {30};
		q.options.degree = 2;
		q.options.angularOrder = 8;
		q.options.referenceOrder = 12;
		q.options.maximumRelativeAngularRMSE = .2;
		F::Artifact irradiance, radiance;
		ProbeFixture::Check(F::Build(q, ReadContext(), irradiance));
		q.selection.quantity = ConditionalQuantity::IncomingRadiance;
		q.options.maximumRelativeAngularRMSE = .6;
		ProbeFixture::Check(F::Build(q, ReadContext(), radiance));
		std::shared_ptr<const F::Prepared> ep, lp;
		ProbeFixture::Check(F::Prepare(irradiance, ReadContext(), ep));
		ProbeFixture::Check(F::Prepare(radiance, ReadContext(), lp));
		Manager eowner, lowner;
		if (eowner.Publish(irradiance.asset, Completeness::Partial, ep) != Status::Ok ||
			lowner.Publish(radiance.asset, Completeness::Partial, lp) != Status::Ok)
			throw std::runtime_error("Publication");
		auto es = eowner.Acquire(), ls = lowner.Acquire();
		IrradianceQuery eq;
		eq.positionWorld = Vec3(-1, -1, 1);
		eq.normalWorld = Vec3(0, 0, 1);
		RadianceQuery lq;
		lq.positionWorld = eq.positionWorld;
		lq.directionToSource = eq.normalWorld;
		ConditionalFieldQuery cq;
		cq.field = es.IdentifySource(1000).value;
		cq.optIn = true;
		cq.positionWorld = eq.positionWorld;
		cq.directionOrNormalWorld = eq.normalWorld;
		if (!ls.SampleIncomingRadiance(lq) || !es.SampleConditionalField(cq))
			throw std::runtime_error("Positive field query unsupported");
		ShadowCache::Request sq;
		asset.emitters.clear();
		PhysicalEmitter emitter;
		emitter.source.id = 10;
		emitter.source.provenance = 1;
		emitter.kind = EmitterKind::Point;
		emitter.position.known = true;
		emitter.position.world = Vec3(0, 0, 3);
		emitter.angular.kind = AngularKind::Isotropic;
		asset.emitters = {emitter};
		sq.asset = std::make_shared<const ReconstructedLightingAsset>(asset);
		sq.scene.map = input.visibility->GeometryIdentity();
		sq.scene.domain = scene.domain;
		sq.scene.domainKey = input.domainKey;
		sq.scene.geometry = input.visibility;
		sq.dataset = 900;
		sq.provenance = 1;
		sq.scope = "known V02 static geometry";
		ShadowCache::Item item;
		item.source = 10;
		item.receiver.id = 77;
		item.receiver.positionWorld = eq.positionWorld;
		sq.items = {item};
		ShadowCache::Artifact shadow;
		ProbeFixture::Check(ShadowCache::Build(sq, ReadContext(), shadow));
		std::shared_ptr<const ShadowCache::Prepared> sp;
		ProbeFixture::Check(ShadowCache::Prepare(shadow, ReadContext(), sp));
		Manager sowner;
		if (sowner.Publish(shadow.asset, Completeness::Partial, sp) != Status::Ok)
			throw std::runtime_error("Shadow publication");
		auto ss = sowner.Acquire();
		ShadowSupportQuery vq;
		vq.dataset = 900;
		vq.receiver = 77;
		vq.source = ss.IdentifySource(10).value;
		vq.receiverWorld = item.receiver.positionWorld;
		if (!ss.QueryShadowSupport(vq))
			throw std::runtime_error("Positive visibility query unsupported");
		ProducerTest::Rig rig;
		auto candidate = JointTest::Point(10, JointFixture::V(-.6, 0, .8));
		rig.request.candidates = {candidate};
		rig.request.profiles[0].model.linearRec709PrimariesAttested = true;
		rig.Observations(rig.Truth({JointTest::Answer(candidate, JointFixture::V(2, 1, .5))}));
		ProducerTest::Validation val(rig);
		ProducerTest::Final fin(rig);
		Producer::Result fitted;
		ProbeFixture::Check(
			Producer::Run(ProducerTest::Request(rig), JointFixture::Read(), fitted, &val, &fin));
		if (!fitted.artifact.asset)
			throw std::runtime_error("Owned contribution artifact unavailable");
		std::shared_ptr<const Attribution::Prepared> ap;
		ProbeFixture::Check(Attribution::Prepare(fitted.artifact, JointFixture::Read(), ap));
		Manager aowner;
		if (aowner.Publish(*fitted.artifact.asset, Completeness::Partial, ap) != Status::Ok)
			throw std::runtime_error("Contribution publication");
		auto as = aowner.Acquire();
		const auto &ref = fitted.artifact.asset->contributions.front();
		std::vector<Producer::ContributionRecord> cr;
		ProbeFixture::Check(
			Producer::ReadContributions(fitted.artifact, ref.dataset, JointFixture::Read(), cr));
		ContributionQuery aq;
		aq.source = as.IdentifySource(cr.front().source).value;
		aq.sampleDomain = ref.sampleDomain;
		aq.sample = cr.front().sample;
		aq.dataset = ref.dataset;
		aq.configuration = ref.configuration;
		aq.component = ref.component;
		aq.quantity = ref.quantity;
		aq.allowLegacyRelativeChannels = true;
		if (!as.SampleContribution(aq))
			throw std::runtime_error("Positive contribution query unsupported");
		std::ofstream out(argv[1]);
		out << "{\"schema\":\"org.openjk.validation-latency/"
			   "1\",\"known_source_inputs\":true,\"not_source_discovery\":true,\"probes\":"
			<< topology->Probes().size() << ",\"queries\":{";
		Measure(
			out, "snapshot_acquire",
			[&] {
				auto x = eowner.Acquire();
				sink += x.Generation();
				return true;
			},
			false);
		Measure(out, "indirect_api_on_sky_field", [&] {
			auto x = es.SampleIndirect(eq);
			if (x)
				sink += x.value.rgb.x;
			return bool(x);
		});
		Measure(out, "incoming_radiance", [&] {
			auto x = ls.SampleIncomingRadiance(lq);
			if (x)
				sink += x.value.rgb.x;
			return bool(x);
		});
		Measure(out, "conditional", [&] {
			auto x = es.SampleConditionalField(cq);
			if (x)
				sink += x.value.value.x;
			return bool(x);
		});
		Measure(out, "static_shadow", [&] {
			auto x = ss.QueryShadowSupport(vq);
			if (x)
				sink += x.value.staticGeometry.value;
			return bool(x);
		});
		auto contributionAllocations = Measure(out, "contribution", [&] {
			auto x = as.SampleContribution(aq);
			if (x)
				sink += x.value.value.x;
			return bool(x);
		});
		Measure(out, "probe_lookup", [&] {
			auto x = topology->Lookup(eq.positionWorld);
			sink += x.count;
			return x.status == Status::Ok;
		});
		Measure(out, "region_lookup", [&] { return bool(es.LookupRegion(eq.positionWorld)); });
		out << "},\"region_lookup_scope\":\"Prepared field has no RegionLookup/indirect-only "
			   "provider; unavailable outcomes "
			   "retained\",\"disk_io_in_measured_queries\":false,\"global_traces_in_measured_"
			   "queries\":false}\n";
		if (!out)
			throw std::runtime_error("Output write");
		if (contributionAllocations)
			throw std::runtime_error("SampleContribution allocated on the V02 successful query workload");
		return 0;
	}
	catch (const std::exception &e)
	{
		counting = false;
		std::cerr << e.what() << '\n';
		return 1;
	}
}
