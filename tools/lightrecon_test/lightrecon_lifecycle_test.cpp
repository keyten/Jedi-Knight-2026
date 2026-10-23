// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "lightrecon/lifecycle.h"
#include <cstdio>
#include <functional>
#include <stdexcept>

using namespace Rend2LightRecon;
namespace
{
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
Disk::Fingerprint Key(const std::string &map = "maps/test.bsp")
{
	Disk::Fingerprint f;
	f.algorithm = "org.openjk.lifecycle-test/manual";
	f.algorithmVersion = 1;
	f.complete.fill(true);
	for (auto kind :
		 {Disk::InputKind::Bsp, Disk::InputKind::Settings, Disk::InputKind::UnitCalibration})
	{
		Disk::InputContent c;
		c.kind = kind;
		c.name = kind == Disk::InputKind::Bsp ? map : std::to_string(uint32_t(kind));
		c.bytes = c.name.size();
		c.sha256 = Disk::Hash(c.name.data(), c.name.size());
		f.inputs.push_back(c);
	}
	return f;
}
ReconstructedLightingAsset Asset()
{
	ReconstructedLightingAsset a;
	a.lineage = "synthetic/complete-result";
	a.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "lifecycle-test/1";
	a.provenanceRecords.push_back(p);
	PhysicalEmitter e;
	e.source.id = 100;
	e.source.provenance = 1;
	a.emitters.push_back(e);
	return a;
}
WorldFacts Ready()
{
	WorldFacts facts;
	facts.geometryReady = facts.areaLightsReady = true;
	return facts; // no lightmaps/grid required
}
struct Blob : Core::ChunkSource<Core::SurfaceObservation>
{
	std::vector<uint8_t> owned = std::vector<uint8_t>(1024 * 1024, 17);
	Core::Error Read(uint64_t, size_t, const Core::ReadContext &,
					 Core::Chunk<Core::SurfaceObservation> &) const override
	{
		return Core::Failure(Core::Code::Unavailable,
							 "test algorithm must not request observations");
	}
};
enum class Mode
{
	Complete,
	Continue,
	InvalidOutput,
	NoAlgorithm,
	PrepareFailure,
	BadSchema,
	MissingGeometry,
	Throw
};
struct Algorithm : Core::Algorithm
{
	Mode mode;
	std::function<void()> inspect;
	explicit Algorithm(Mode m, std::function<void()> f) : mode(m), inspect(f) {}
	Core::Requirements Needs() const override
	{
		Core::Requirements r;
		r.geometry = mode == Mode::MissingGeometry;
		return r;
	}
	Core::WorkResult Step(Core::InputReader &, Core::AssetBuilder &builder,
						  const Core::AlgorithmContext &) override
	{
		if (inspect)
			inspect();
		if (mode == Mode::Throw)
			throw std::runtime_error("algorithm failure");
		builder.Draft() = Asset();
		if (mode == Mode::InvalidOutput)
			builder.Draft().emitters[0].source.provenance = 999;
		Core::WorkResult result;
		result.next = mode == Mode::Continue ? Core::Next::Continue : Core::Next::Complete;
		return result;
	}
};
struct Source : WorldSource
{
	Disk::Fingerprint fingerprint = Key();
	Mode mode = Mode::Complete;
	int descriptions = 0, prepares = 0;
	bool throwDescription = false, wrongWorld = false;
	std::weak_ptr<const Blob> temporary;
	std::function<void()> onDescribe, onPrepare, onStep;
	Disk::Error Describe(const std::string &world, Disk::Fingerprint &out) override
	{
		++descriptions;
		if (onDescribe)
			onDescribe();
		if (throwDescription)
			throw std::runtime_error("describe failed");
		out = fingerprint;
		if (!wrongWorld)
			for (auto &input : out.inputs)
				if (input.kind == Disk::InputKind::Bsp)
					input.name = world;
		return Disk::Error();
	}
	Core::Error Prepare(const std::string &, BuildPlan &plan) override
	{
		++prepares;
		if (onPrepare)
			onPrepare();
		if (mode == Mode::PrepareFailure)
			return Core::Failure(Core::Code::Unavailable, "no input adapter");
		auto a = Asset();
		plan.input.manifest.lineage = a.lineage;
		plan.input.manifest.revision = "immutable-synthetic/1";
		plan.input.manifest.provenance = a.provenance;
		plan.input.manifest.provenanceRecords = a.provenanceRecords;
		auto owned = std::make_shared<Blob>();
		temporary = owned;
		plan.input.surfaces = owned;
		plan.input.manifest.surfaces = 1;
		if (mode == Mode::BadSchema)
			plan.input.manifest.version = 999;
		if (mode != Mode::NoAlgorithm)
			plan.algorithm.reset(new Algorithm(mode, onStep));
		return Core::Error();
	}
};
struct Cache : WorldCache
{
	int loads = 0, writes = 0;
	Disk::Code loadCode = Disk::Code::Ok, writeCode = Disk::Code::Ok;
	ReconstructedLightingAsset asset = Asset();
	bool throwLoad = false, throwWrite = false;
	Disk::Error Load(const Disk::Fingerprint &, ReconstructedLightingAsset &out,
					 const Disk::Limits &) override
	{
		++loads;
		if (throwLoad)
			throw std::runtime_error("cache read failed");
		Disk::Error e;
		e.code = loadCode;
		e.message = "synthetic cache status";
		if (e)
			out = asset;
		return e;
	}
	Disk::Error Store(const Disk::Fingerprint &, const ReconstructedLightingAsset &a,
					  const Disk::Limits &) override
	{
		++writes;
		if (throwWrite)
			throw std::runtime_error("cache write failed");
		asset = a;
		Disk::Error e;
		e.code = writeCode;
		return e;
	}
};
void Policies()
{
	for (int policy = 0; policy < 4; ++policy)
		for (bool hit : {false, true})
		{
			WorldLighting owner;
			CHECK(owner.Report().state == WorldState::Unloaded &&
				  owner.Acquire().State() == Availability::Unloaded);
			Source source;
			Cache cache;
			cache.loadCode = hit ? Disk::Code::Ok : Disk::Code::Missing;
			auto begin = owner.Begin("maps/test.bsp", static_cast<BuildPolicy>(policy));
			CHECK(begin);
			CHECK(!owner.Acquire().Asset());
			Status status = owner.Complete(begin.value, Ready(), source, &cache);
			CHECK(status == (policy == 0 ? Status::Disabled
										 : policy == 1 && !hit ? Status::Missing : Status::Ok));
			CHECK(source.descriptions == (policy == 0 ? 0 : 1));
			CHECK(cache.loads == ((policy == 1 || policy == 2) ? 1 : 0));
			CHECK(source.prepares == ((policy == 3 || (policy == 2 && !hit)) ? 1 : 0));
			CHECK(cache.writes == source.prepares);
			CHECK(source.temporary.expired());
			CHECK(owner.Report().transientBytesKnown && owner.Report().transientBytes == 0);
			if (status == Status::Ok)
			{
				CHECK(owner.Report().state == WorldState::Available);
				CHECK(owner.Acquire().Asset());
				CHECK(owner.Report().assetBytes > 0);
			}
			if (policy == 0)
			{
				CHECK(owner.Report().state == WorldState::Disabled);
				CHECK(owner.Acquire().State() == Availability::Disabled);
			}
			if (policy == 1 && !hit)
				CHECK(owner.Report().state == WorldState::Unavailable);
			CHECK(owner.Complete(begin.value, Ready(), source, &cache) ==
				  (policy == 0 ? Status::Disabled : Status::InvalidArgument));
		}
}
void FailuresAndCleanup()
{
	{
		WorldLighting owner;
		Source source;
		auto begin = owner.Begin("test", BuildPolicy::CacheOnly);
		CHECK(owner.Complete(begin.value, Ready(), source) == Status::Unsupported);
		CHECK(owner.Report().state == WorldState::Unavailable && !owner.Report().cacheCalled);
	}
	for (auto code : {Disk::Code::Corrupt, Disk::Code::Mismatch, Disk::Code::Unsupported,
					  Disk::Code::LimitExceeded})
	{
		WorldLighting owner;
		Source source;
		Cache cache;
		cache.loadCode = code;
		auto begin = owner.Begin("test", BuildPolicy::CacheOnly);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) != Status::Ok);
		CHECK(owner.Report().state == WorldState::InvalidCache && !owner.Acquire().Asset());
		CHECK(source.prepares == 0);
		begin = owner.Begin("test", BuildPolicy::CacheOrBuild);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Ok);
		CHECK(owner.Report().cacheStatus == code && cache.writes == 1 &&
			  source.temporary.expired());
	}
	for (auto mode : {Mode::Continue, Mode::InvalidOutput, Mode::NoAlgorithm, Mode::PrepareFailure,
					  Mode::BadSchema, Mode::MissingGeometry, Mode::Throw})
	{
		WorldLighting owner;
		Source source;
		Cache cache;
		source.mode = mode;
		WorldLimits limits;
		limits.maxBuildSteps = 2;
		auto begin = owner.Begin("test", BuildPolicy::ForceRebuild);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache, limits) != Status::Ok);
		CHECK(!owner.Acquire().Asset());
		CHECK(cache.loads == 0 && cache.writes == 0);
		CHECK(source.temporary.expired());
		CHECK(owner.Report().transientBytesKnown && owner.Report().transientBytes == 0);
		if (mode == Mode::Continue)
			CHECK(owner.Report().steps == 2 && owner.Report().state == WorldState::BuildRequired);
	}
	{
		WorldLighting owner;
		Source source;
		Cache cache;
		source.fingerprint.complete[2] = false;
		auto begin = owner.Begin("test", BuildPolicy::CacheOnly);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Unavailable);
		CHECK(owner.Report().cacheStatus == Disk::Code::NotCacheable && cache.loads == 0 &&
			  source.prepares == 0);
		begin = owner.Begin("test", BuildPolicy::CacheOrBuild);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Ok);
		CHECK(!owner.Report().keyComplete && cache.loads == 0 && cache.writes == 0 &&
			  source.prepares == 1);
	}
	{
		WorldLighting owner;
		Source source;
		Cache cache;
		source.wrongWorld = true;
		auto begin = owner.Begin("maps/another-world.bsp", BuildPolicy::CacheOnly);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Unavailable);
		CHECK(owner.Report().cacheStatus == Disk::Code::NotCacheable && cache.loads == 0 &&
			  source.prepares == 0);
	}
	{
		WorldLighting owner;
		Source source;
		Cache cache;
		auto begin = owner.Begin("test", BuildPolicy::CacheOnly);
		CHECK(owner.Complete(begin.value, WorldFacts(), source, &cache) == Status::Unavailable);
		CHECK(source.descriptions == 0 && cache.loads == 0);
		begin = owner.Begin("test", BuildPolicy::CacheOnly);
		source.throwDescription = true;
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Corrupt);
		CHECK(!owner.Acquire().Asset());
		begin = owner.Begin("test", BuildPolicy::CacheOnly);
		source.throwDescription = false;
		cache.throwLoad = true;
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Unavailable &&
			  owner.Report().cacheStatus == Disk::Code::IoError);
		begin = owner.Begin("test", BuildPolicy::ForceRebuild);
		cache.throwWrite = true;
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Ok &&
			  owner.Report().writeStatus == Disk::Code::IoError);
		CHECK(owner.Acquire().Asset() && source.temporary.expired());
	}
	{
		WorldLighting owner;
		Source source;
		Cache cache;
		cache.asset.emitters[0].source.provenance = 999;
		auto begin = owner.Begin("test", BuildPolicy::CacheOnly);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Corrupt);
		CHECK(owner.Report().state == WorldState::InvalidCache && !owner.Acquire().Asset());
		cache.asset = ReconstructedLightingAsset();
		begin = owner.Begin("empty", BuildPolicy::CacheOnly);
		CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Ok);
		CHECK(owner.Acquire().Emitters().value.Next().status == Status::NotFound);
	}
}
void EpochsAndLeases()
{
	WorldLighting owner;
	Source source;
	Cache cache;
	auto first = owner.Begin("maps/a.bsp", BuildPolicy::CacheOnly);
	CHECK(owner.Complete(first.value, Ready(), source, &cache) == Status::Ok);
	auto old = owner.Acquire();
	auto weak = old.Weak();
	auto handle = old.IdentifySource(100);
	CHECK(handle);
	cache.asset.lineage = "synthetic/map-b";
	auto second = owner.Begin("maps/b.bsp", BuildPolicy::CacheOnly);
	CHECK(owner.Abort(first.value) == Status::StaleHandle);
	CHECK(owner.Complete(first.value, Ready(), source, &cache) == Status::StaleHandle);
	CHECK(owner.Report().world == "maps/b.bsp" && !owner.Acquire().Asset());
	CHECK(owner.Complete(second.value, Ready(), source, &cache) == Status::Ok);
	CHECK(owner.Acquire().GetSource(handle.value).status == Status::StaleHandle);
	CHECK(old.GetSource(handle.value));
	CHECK(old.Asset().value->lineage == "synthetic/complete-result");
	owner.Unload();
	CHECK(owner.Acquire().State() == Availability::Unloaded && old.GetSource(handle.value));
	old = Snapshot();
	CHECK(!weak.Lock());
	for (unsigned i = 0; i < 32; ++i)
	{
		WorldLoadGuard load(owner, "maps/cycle" + std::to_string(i) + ".bsp",
							BuildPolicy::ForceRebuild);
		CHECK(load.Finish(Ready(), source, &cache) == Status::Ok);
		CHECK(source.temporary.expired());
		UseSettings uses;
		uses.master = true;
		uses.requested.fill(true);
		auto descriptions = source.descriptions;
		auto builds = source.prepares;
		for (size_t c = 0; c < kConsumerCount; ++c)
			EvaluateConsumer(owner.Acquire(), uses, static_cast<Consumer>(c), FeatureSupport());
		CHECK(source.descriptions == descriptions && source.prepares == builds);
		owner.Unload();
		CHECK(!owner.Acquire().Asset());
	}
	try
	{
		WorldLoadGuard failed(owner, "maps/failed.bsp", BuildPolicy::CacheOnly);
		throw std::runtime_error("ERR_DROP equivalent");
	}
	catch (const std::exception &)
	{
	}
	CHECK(owner.Report().state == WorldState::Unavailable && !owner.Acquire().Asset());
	owner.Unload();
	CHECK(owner.Report().world.empty() && owner.Report().assetBytes == 0);
	CHECK(!owner.Begin("", BuildPolicy::CacheOnly));
	CHECK(!owner.Acquire().Asset());
}
void Reentry()
{
	WorldLighting owner;
	Source source;
	Cache cache;
	auto begin = owner.Begin("test", BuildPolicy::ForceRebuild);
	source.onDescribe = [&] {
		CHECK(owner.Report().state == WorldState::Loading);
		CHECK(owner.Begin("nested", BuildPolicy::Disabled).status == Status::Pending);
		CHECK(owner.Unload() == Status::Pending);
	};
	source.onPrepare = [&] {
		CHECK(owner.Report().state == WorldState::Preparing);
		CHECK(!owner.Acquire().Asset());
	};
	source.onStep = [&] {
		CHECK(owner.Report().state == WorldState::Building);
		CHECK(owner.Acquire().Asset().status == Status::Pending);
		CHECK(owner.Abort(begin.value) == Status::Pending);
	};
	CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Ok);
	CHECK(owner.Report().world == "test");
}
void Native(const std::string &root)
{
	Source source;
	Disk::Digest digest;
	Disk::Fingerprint fingerprint;
	CHECK(source.Describe("native", fingerprint));
	CHECK(Disk::MakeKey(fingerprint, digest));
	std::string filename = root + "/" + Disk::Hex(digest) + ".jklr";
	std::remove(filename.c_str());
	NativeWorldCache cache(root);
	WorldLighting owner;
	auto begin = owner.Begin("native", BuildPolicy::CacheOnly);
	CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Missing);
	begin = owner.Begin("native", BuildPolicy::CacheOrBuild);
	CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Ok);
	CHECK(owner.Report().writeStatus == Disk::Code::Ok && source.temporary.expired());
	begin = owner.Begin("native", BuildPolicy::CacheOnly);
	CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Ok);
	CHECK(!owner.Report().buildCalled);
	std::FILE *file = std::fopen(filename.c_str(), "wb");
	CHECK(file != nullptr);
	if (file)
	{
		std::fputs("corrupt", file);
		std::fclose(file);
	}
	begin = owner.Begin("native", BuildPolicy::CacheOnly);
	CHECK(owner.Complete(begin.value, Ready(), source, &cache) == Status::Corrupt);
	CHECK(owner.Report().state == WorldState::InvalidCache);
	CHECK(!owner.Acquire().Asset());
	owner.Unload();
	std::remove(filename.c_str());
}
} // namespace
int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	Policies();
	FailuresAndCleanup();
	EpochsAndLeases();
	Reentry();
	Native(argv[1]);
	std::printf("lightrecon lifecycle: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
