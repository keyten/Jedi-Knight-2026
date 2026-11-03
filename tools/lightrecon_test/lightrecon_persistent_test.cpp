// V01 dependency, selective preparation, lifecycle and native atomic I/O tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/dependencies.h"
#include "lightrecon/field_builder.h"
#include "lightrecon/lifecycle.h"
#include "lightrecon/persistent.h"
#include "lightrecon/photometry.h"
#include "lightrecon/shadow_cache.h"
#include "probe_fixtures.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#endif
bool countAllocations = false;
uint64_t allocationCalls = 0, requestedAllocationBytes = 0;
void *operator new(std::size_t n)
{
	if (countAllocations)
	{
		++allocationCalls;
		requestedAllocationBytes += n;
	}
	if (void *p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
	std::free(p);
}
void *operator new[](std::size_t n)
{
	return ::operator new(n);
}
void operator delete[](void *p) noexcept
{
	::operator delete(p);
}
using namespace ProbeFixture;
namespace D = Rend2LightRecon::Disk;
namespace Dep = D::Dependencies;
namespace PC = D::Persistent;
namespace F = Rend2LightRecon::Core::Fields;
namespace S = Rend2LightRecon::Core::ShadowCache;
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
void OK(D::Error e)
{
	CHECK(bool(e));
	if (!e)
		throw std::runtime_error(e.message);
}
D::Digest Key(const D::Fingerprint &f)
{
	D::Digest d;
	OK(D::MakeKey(f, d));
	return d;
}
struct Bytes : D::Reader
{
	std::vector<uint8_t> v;
	uint64_t Size() const override
	{
		return v.size();
	}
	D::Error Read(uint64_t p, void *b, size_t n) const override
	{
		return D::MemoryReader(v.data(), v.size()).Read(p, b, n);
	}
};
struct Sources : Dep::Sources
{
	std::map<std::string, std::string> files, materials;
	D::Error Open(const std::string &p, std::unique_ptr<D::Reader> &r) const override
	{
		auto i = files.find(p);
		D::Error e;
		if (i == files.end())
		{
			e.code = D::Code::Missing;
			return e;
		}
		std::unique_ptr<Bytes> b(new Bytes);
		b->v.assign(i->second.begin(), i->second.end());
		r = std::move(b);
		return e;
	}
	D::Error Material(const std::string &p, std::string &s) const override
	{
		auto i = materials.find(p);
		D::Error e;
		if (i == materials.end())
		{
			e.code = D::Code::Missing;
			return e;
		}
		s = i->second;
		return e;
	}
};
void U(std::vector<uint8_t> &b, size_t p, uint32_t n)
{
	for (unsigned j = 0; j < 4; ++j)
		b[p + j] = uint8_t(n >> (j * 8));
}
Bytes BSP()
{
	Bytes b;
	b.v.resize(152 + 72 + 148);
	std::copy_n("RBSP", 4, b.v.begin());
	U(b.v, 4, 1);
	U(b.v, 16, 152);
	U(b.v, 20, 72);
	U(b.v, 112, 224);
	U(b.v, 116, 148);
	std::copy_n("textures/test", 13, b.v.begin() + 152);
	return b;
}
Dep::Recipe Recipe()
{
	Dep::Recipe r;
	r.materialContext = "SP/1";
	r.decoder = "decoder/1";
	r.geometry = "geometry/1";
	r.profiles = "conditional/1";
	r.solver = "solver/1";
	r.settings = "seed=5;budget=1000";
	r.calibration = "relative/1";
	return r;
}
D::Fingerprint Manifest()
{
	auto b = BSP();
	Sources s;
	s.materials["textures/test"] = "{ { map textures/colour } }";
	D::Fingerprint f;
	Dep::Work w;
	OK(Dep::Collect("maps/mp/test.bsp", b, s, Recipe(), f, w));
	return f;
}
void Dependencies()
{
	auto b = BSP();
	Sources s;
	s.materials["textures/test"] = "{ { map textures/colour } }";
	auto r = Recipe();
	D::Fingerprint f;
	Dep::Work w;
	OK(Dep::Collect("maps/mp/test.bsp", b, s, r, f, w));
	auto key = Key(f);
	CHECK(w.materials == 1 && w.missing > 0);
	CHECK(std::find_if(f.inputs.begin(), f.inputs.end(), [](const D::InputContent &c) {
			  return c.name == "absent:maps/test/lm_0000.hdr";
		  }) != f.inputs.end());
	std::reverse(f.inputs.begin(), f.inputs.end());
	CHECK(Key(f) == key);
	auto compare = [&](bool changed) {
		D::Fingerprint next;
		OK(Dep::Collect("maps/mp/test.bsp", b, s, r, next, w));
		CHECK((Key(next) != key) == changed);
	};
	compare(false);
	b.v.back() = 1;
	compare(true);
	b.v.back() = 0;
	s.materials["textures/test"] += " // changed";
	compare(true);
	s.materials["textures/test"] = "{ { map textures/colour } }";
	s.files["textures/colour.png"] = "mask pixels";
	compare(true);
	s.files.clear();
	s.files["maps/test/lightgrid.raw"] = "hdr-grid";
	compare(true);
	s.files.clear();
	s.files["maps/test/lm_0000.hdr"] = "hdr-lightmap";
	compare(true);
	s.files.clear();
	s.files["maps/test/dm_0000.tga"] = "deluxe";
	compare(true);
	s.files.clear();
	s.files["maps/test.arealights.json"] = "source metadata";
	compare(true);
	s.files.clear();
	s.files["unrelated/display.cfg"] = "exposure 8";
	compare(false);
	s.files.clear();
	{
		auto material=s.materials["textures/test"];
		s.materials["textures/test"]="{ { map textures/colour normalMap textures/spec-only } }";
		D::Fingerprint before,after;OK(Dep::Collect("maps/mp/test.bsp",b,s,r,before,w));
		s.files["textures/spec-only.png"]="unrelated renderer normal map";
		OK(Dep::Collect("maps/mp/test.bsp",b,s,r,after,w));CHECK(Key(before)==Key(after));
		s.files.clear();s.materials["textures/test"]=material;
	}
	r.materialContext = "MP/1";
	compare(true);
	r = Recipe();
	r.calibration = "calibrated/2";
	compare(true);
	r = Recipe();
	r.units.worldUnitsPerMeter *= 2;
	compare(true);
	r = Recipe();
	r.solver.clear();
	D::Fingerprint untouched = f;
	CHECK(Dep::Collect("maps/mp/test.bsp", b, s, r, f, w).code == D::Code::NotCacheable);
	CHECK(Key(f) == Key(untouched));
	r = Recipe();
	Dep::Budget budget;
	budget.maxFiles = 1;
	CHECK(Dep::Collect("maps/mp/test.bsp", b, s, r, f, w, budget).code == D::Code::LimitExceeded);
	b.v[0] = 'X';
	CHECK(Dep::Collect("maps/mp/test.bsp", b, s, r, f, w).code == D::Code::Unsupported);
	b=BSP();U(b.v,224+36,2);D::Fingerprint deluxe;
	OK(Dep::Collect("maps/mp/test.bsp",b,s,r,deluxe,w));
	CHECK(std::find_if(deluxe.inputs.begin(),deluxe.inputs.end(),[](const D::InputContent&c){return c.name=="absent:maps/test/lm_0003.png";})!=deluxe.inputs.end());
	s.files["maps/test/lm_0003.png"]="last external deluxe page";
	D::Fingerprint changedDeluxe;OK(Dep::Collect("maps/mp/test.bsp",b,s,r,changedDeluxe,w));
	CHECK(Key(changedDeluxe)!=Key(deluxe));s.files.clear();
	U(b.v,116,149);CHECK(Dep::Collect("maps/mp/test.bsp",b,s,r,f,w).code==D::Code::Corrupt);
	Dep::LayerRecipe l;
	l.layer = Dep::Layer::Visibility;
	l.algorithm = "static";
	l.version = 1;
	l.settings = "samples=4";
	for (auto n : {"geometry", "optics", "source-geometry", "scenario"})
	{
		D::InputContent c;
		c.name = n;
		c.sha256 = D::Hash(n, std::strlen(n));
		l.dependencies.push_back(c);
	}
	D::Fingerprint derived;
	OK(Dep::Derive(untouched, l, derived));
	auto original = Key(derived);
	auto parent = untouched;
	parent.inputs.push_back(D::InputContent());
	parent.inputs.back().kind = D::InputKind::Settings;
	parent.inputs.back().name = "source-amplitude";
	parent.inputs.back().sha256[0] = 8;
	OK(Dep::Derive(parent, l, derived));
	CHECK(Key(derived) == original);
	auto materialChanged=parent;
	for(auto &c:materialChanged.inputs)if(c.kind==D::InputKind::Materials){c.sha256[0]^=1;break;}
	OK(Dep::Derive(materialChanged,l,derived));CHECK(Key(derived)!=original);
	l.dependencies[2].sha256[0] ^= 1;
	OK(Dep::Derive(parent, l, derived));
	CHECK(Key(derived) != original);
	l.dependencies.pop_back();
	CHECK(Dep::Derive(parent, l, derived).code == D::Code::NotCacheable);
	l.layer = Dep::Layer::Field;
	l.dependencies.clear();
	for (auto n : {"canonical-result", "probes"})
	{
		D::InputContent c;
		c.name = n;
		c.sha256 = D::Hash(n, std::strlen(n));
		l.dependencies.push_back(c);
	}
	OK(Dep::Derive(untouched, l, derived));
	auto fieldKey = Key(derived);
	l.settings = "SHL4";
	OK(Dep::Derive(untouched, l, derived));
	CHECK(Key(derived) != fieldKey && Key(untouched) == key);
}
ReconstructedLightingAsset Asset()
{
	ReconstructedLightingAsset a;
	a.provenance = 1;
	a.lineage = "V01/conditional-owned";
	a.units.worldUnitsPerMeter = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "persistent-test/1";
	a.provenanceRecords.push_back(p);
	Calibration c;
	c.id = 1;
	c.provenance = 1;
	c.kind = CalibrationKind::PhysicallyAuthored;
	a.calibrations.push_back(c);
	return a;
}
D::Fingerprint Fingerprint()
{
	auto f = Manifest();
	f.units = Asset().units;
	for (auto &i : f.inputs)
		if (i.kind == D::InputKind::Bsp)
		{
			i.name = "maps/probes-owned.bsp";
			i.bytes = 42;
			i.sha256 = {};
			i.sha256[0] = 71;
		}
	return f;
}
struct Stop : D::WriteObserver
{
	D::WriteStage stage;
	bool crash = false, throws = false;
	bool Continue(D::WriteStage s, const std::string &) override
	{
		if (stage != s)
			return true;
		if (crash)
			std::_Exit(23);
		if (throws)
			throw 7;
		return false;
	}
};
void Atomic(const std::string &root)
{
	D::Cache cache(root);
	auto f = Fingerprint();
	auto a = Asset();
	OK(cache.Store(f, a));
	ReconstructedLightingAsset old;
	D::Index index;
	std::unique_ptr<D::FileReader> lease;
	OK(cache.Load(f, old, index, lease));
	a.lineage = "V01/new";
	for (auto stage : {D::WriteStage::Created, D::WriteStage::Written, D::WriteStage::Flushed})
	{
		Stop stop;
		stop.stage = stage;
		CHECK(cache.Store(f, a, {}, D::Limits(), &stop).code == D::Code::IoError);
		ReconstructedLightingAsset still;
		D::Index ix;
		std::unique_ptr<D::FileReader> rd;
		OK(cache.Load(f, still, ix, rd));
		CHECK(still.lineage == old.lineage);
	}
	Stop thrown;
	thrown.stage = D::WriteStage::Flushed;
	thrown.throws = true;
	CHECK(cache.Store(f, a, {}, D::Limits(), &thrown).code == D::Code::IoError);
	for (unsigned j = 0; j < 200; ++j)
	{
		a.lineage = "V01/replace-" + std::to_string(j);
		auto e = cache.Store(f, a);
		if (!e)
		{
			std::cerr << "ATOMIC " << j << ' ' << e.message << '\n';
			CHECK(false);
			break;
		}
	}
	ReconstructedLightingAsset retained;
	D::Index retainedIndex;
	OK(D::Open(*lease, f, retained, retainedIndex));
	CHECK(retained.lineage == old.lineage);
	std::atomic<unsigned> errors{0};
	std::vector<std::thread> threads;
	for (unsigned t = 0; t < 4; ++t)
		threads.emplace_back([&, t] {
			for (unsigned j = 0; j < 30; ++j)
			{
				auto next = a;
				next.lineage = "writer/" + std::to_string(t) + "/" + std::to_string(j);
				auto write = cache.Store(f, next);
				if (!write)
				{
					++errors;
					std::cerr << "CONCURRENT WRITE " << write.message << '\n';
				}
				ReconstructedLightingAsset got;
				D::Index ix;
				std::unique_ptr<D::FileReader> r;
				auto load = cache.Load(f, got, ix, r);
				if (!load)
				{
					++errors;
					std::cerr << "CONCURRENT READ " << load.message << '\n';
				}
			}
		});
	for (auto &t : threads)
		t.join();
	CHECK(errors == 0);
#ifdef _WIN32
	std::string path = root + "/" + D::Hex(Key(f)) + ".jklr";
	HANDLE lock = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
							  FILE_ATTRIBUTE_NORMAL, nullptr);
	CHECK(lock != INVALID_HANDLE_VALUE);
	auto denied = cache.Store(f, a);
	CHECK(denied.code == D::Code::IoError);
	CHECK(denied.message.find("rename") != std::string::npos);
	CloseHandle(lock);
	OK(cache.Load(f, retained, index, lease));
#endif
}
struct WorldFixture : WorldSource
{
	D::Fingerprint f;
	unsigned descriptions = 0, prepares = 0;
	D::Error Describe(const std::string &, D::Fingerprint &out) override
	{
		++descriptions;
		out = f;
		return {};
	}
	Error Prepare(const std::string &, BuildPlan &) override
	{
		++prepares;
		return Failure(Code::Unavailable, "No solver in cache test");
	}
};
void PreparedCache(const std::string &root)
{
	Scene scene;
	scene.domain->world = scene.options.bounds = Box(Vec3(-2, -2, 0), Vec3(2, 2, 4));
	scene.options.coarseSpacingWorld = .5f;
	scene.options.minimumSpacingWorld = .5f;
	auto geometry = scene.Input();
	auto topology = scene.Build(geometry);
	auto asset = Asset();
	PhysicalEmitter e;
	e.source.id = 10;
	e.source.provenance = 1;
	e.source.state = HypothesisState::Resolved;
	e.position.known = true;
	e.position.world = Vec3(0, 0, 6);
	e.kind = EmitterKind::Point;
	e.angular.kind = AngularKind::Isotropic;
	e.emission = Photometric::RGBToPhotometry(Vec3(100, 40, 10), PhotometricUnit::Candela, 1).value;
	asset.emitters.push_back(e);
	F::Request request;
	request.asset = std::make_shared<const ReconstructedLightingAsset>(asset);
	request.topology = topology;
	request.visibility = geometry.visibility;
	request.selection.field = 1000;
	request.selection.dataset = 1001;
	request.selection.geometryDataset = 1002;
	request.selection.topologyDataset = 1003;
	request.selection.provenance = 1;
	request.selection.calibration = 1;
	request.selection.directLocal = request.selection.allowDirectProjection = true;
	request.selection.sources = {10};
	request.options.degree = 4;
	request.options.angularOrder = 8;
	request.options.referenceOrder = 12;
	auto start = std::chrono::steady_clock::now();
	allocationCalls = requestedAllocationBytes = 0;
	countAllocations = true;
	F::Artifact field;
	Check(F::Build(request, ReadContext(), field));
	S::Request shadow;
	shadow.asset = std::make_shared<const ReconstructedLightingAsset>(field.asset);
	shadow.scene.map = geometry.visibility->GeometryIdentity();
	shadow.scene.domain = scene.domain;
	shadow.scene.domainKey = geometry.domainKey;
	shadow.scene.geometry = geometry.visibility;
	shadow.dataset = 900;
	shadow.provenance = 1;
	shadow.scope = "V01 supported geometry";
	S::Item item;
	item.source = 10;
	item.receiver.id = 77;
	item.receiver.positionWorld = Vec3(-1, -1, 1);
	shadow.items.push_back(item);
	S::Artifact vis;
	Check(S::Build(shadow, ReadContext(), vis));
	double cold = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	countAllocations = false;
	auto coldAllocations = allocationCalls;
	auto chunks = field.chunks;
	chunks.insert(chunks.end(), vis.chunks.begin(), vis.chunks.end());
	auto f = Fingerprint();
	D::Cache cache(root);
	PC::Selection all;
	PC::Item select;
	select.id = 1000;
	select.geometry = geometry.geometry;
	all.fields.push_back(select);
	select.id = 900;
	all.visibility.push_back(select);
	OK(PC::Store(cache, f, vis.asset, chunks, all));
	std::vector<uint8_t> first, second;
	OK(D::Encode(vis.asset, f, chunks, first));
	std::reverse(chunks.begin(), chunks.end());
	OK(D::Encode(vis.asset, f, chunks, second));
	CHECK(first == second);
	BuildProduct product;
	PC::Work w;
	allocationCalls = requestedAllocationBytes = 0;
	countAllocations = true;
	OK(PC::Load(cache, f, all, product, w));
	CHECK(w.chunks>8 && field.work.available>64);
	countAllocations = false;
	auto warmAllocations = allocationCalls, warmAllocationBytes = requestedAllocationBytes;
	CHECK(product.evaluator);
	CHECK(product.evaluator->Supports(Capability::ConditionalFieldSampling) == Status::Ok);
	CHECK(product.evaluator->Supports(Capability::StaticShadowSupport) == Status::Ok);
	Manager owner;
	CHECK(owner.Publish(*product.asset, product.completeness, product.evaluator) == Status::Ok);
	auto lease = owner.Acquire();
	ConditionalFieldQuery query;
	query.optIn = true;
	query.positionWorld = Vec3(-1, -1, 1);
	query.directionOrNormalWorld = Vec3(0, 0, 1);
	// Prepared evaluation can be called directly with stable IDs; snapshot handle
	// binding is covered by the existing P01/P05 query tests.
	query.field.id = 1000;
	ConditionalFieldSample value;
	CHECK(product.evaluator->ConditionalField(*product.asset, query, value) == Status::Ok);
	CHECK(value.value.x > 0);
	auto originalValue = value.value;
	allocationCalls = 0;
	countAllocations = true;
	for (unsigned j = 0; j < 1000; ++j)
		if (product.evaluator->ConditionalField(*product.asset, query, value) != Status::Ok)
			++failures;
	countAllocations = false;
	CHECK(allocationCalls == 0);
	std::shared_ptr<const Probes::Topology> reloadedTopology;
	PC::Work pw;
	OK(PC::LoadProbes(cache, f, 1002, 1003, geometry.geometry, reloadedTopology, pw));
	CHECK(reloadedTopology->Key() == topology->Key() && pw.payloadBytes < w.payloadBytes);
	PC::Selection onlyField = all;
	onlyField.visibility.clear();
	BuildProduct fp;
	PC::Work fw;
	OK(PC::Load(cache, f, onlyField, fp, fw));
	CHECK(fw.payloadBytes < w.payloadBytes);
	CHECK(fp.evaluator->Supports(Capability::StaticShadowSupport) != Status::Ok);
	PC::Selection onlyShadow = all;
	onlyShadow.fields.clear();
	BuildProduct sp;
	PC::Work sw;
	OK(PC::Load(cache, f, onlyShadow, sp, sw));
	CHECK(sw.payloadBytes < w.payloadBytes);
	CHECK(sp.evaluator->Supports(Capability::ConditionalFieldSampling) != Status::Ok);
	PC::Selection metadata;
	BuildProduct mp;
	PC::Work mw;
	OK(PC::Load(cache, f, metadata, mp, mw));
	CHECK(mw.chunks == 0 && !mp.evaluator);
	PC::Selection low = all;
	low.maxPreparedBytes = 100;
	CHECK(PC::Load(cache, f, low, mp, mw).code == D::Code::LimitExceeded);
	WorldFixture source;
	source.f = f;
	NativeWorldCache native(root, all);
	WorldLighting world;
	auto ticket = world.Begin("maps/probes-owned.bsp", BuildPolicy::CacheOnly).value;
	CHECK(world.Preflight(ticket, source, &native) == Status::Ok);
	CHECK(world.Report().pendingCacheHit && world.Report().steps == 0);
	CHECK(world.Acquire().Asset().status != Status::Ok);
	WorldFacts facts;
	facts.geometryReady = facts.areaLightsReady = true;
	CHECK(world.Complete(ticket, facts, source, &native) == Status::Ok);
	CHECK(source.descriptions == 1 && source.prepares == 0 && !world.Report().buildCalled);
	auto old = world.Acquire();
	CHECK(world.Unload() == Status::Unloaded);
	CHECK(old.Asset().status == Status::Ok);
	ticket = world.Begin("maps/probes-owned.bsp", BuildPolicy::Disabled).value;
	CHECK(world.Preflight(ticket, source, &native) == Status::Disabled);
	CHECK(source.descriptions == 1);
	ticket = world.Begin("maps/probes-owned.bsp", BuildPolicy::ForceRebuild).value;
	world.Preflight(ticket, source, &native);
	CHECK(!world.Report().cacheCalled);
	CHECK(world.Complete(ticket, facts, source, &native) == Status::Unavailable);
	CHECK(source.prepares == 1);
	auto wrong = f;
	wrong.inputs[0].sha256[0] ^= 1;
	source.f = wrong;
	ticket = world.Begin("maps/probes-owned.bsp", BuildPolicy::CacheOnly).value;
	CHECK(world.Preflight(ticket, source, &native) != Status::Ok);
	CHECK(world.Complete(ticket, facts, source, &native) != Status::Ok);
	CHECK(source.prepares == 1);
	// Corrupt a selected field payload: field request fails, independent shadow
	// request still validates and prepares. Unknown bytes never become black.
	auto corrupt = chunks;
	for (auto &c : corrupt)
		if (c.dataset == 1001 && c.firstElement)
		{
			c.bytes[24] ^= 4;
			break;
		}
	OK(cache.Store(f, vis.asset, corrupt));
	CHECK(PC::Load(cache, f, onlyField, mp, mw).code == D::Code::Corrupt);
	CHECK(product.evaluator->ConditionalField(*product.asset, query, value) == Status::Ok);
	CHECK(value.value.x == originalValue.x);
	OK(PC::Load(cache, f, onlyShadow, sp, sw));
	OK(cache.Store(f, vis.asset, chunks));
	auto unknown = vis.asset;
	for (auto &d : unknown.datasets)
		if (d.id == 1001)
			d.encodingVersion = 999;
	OK(cache.Store(f, unknown, chunks));
	CHECK(PC::Load(cache, f, onlyField, mp, mw).code == D::Code::Unsupported);
	OK(cache.Store(f, vis.asset, chunks));
	auto stale = onlyShadow;
	stale.visibility[0].geometry[0] ^= 1;
	CHECK(PC::Load(cache, f, stale, mp, mw).code == D::Code::Corrupt);
	auto movedAsset=vis.asset;movedAsset.emitters[0].position.world.x+=.1f;
	OK(cache.Store(f,movedAsset,chunks));
	CHECK(PC::Load(cache,f,onlyField,mp,mw).code==D::Code::Corrupt);
	CHECK(PC::Load(cache,f,onlyShadow,mp,mw).code==D::Code::Corrupt);
	OK(cache.Store(f,vis.asset,chunks));
	uint64_t peakWorkingSet = 0;
#ifdef _WIN32
	using GetMemory = BOOL(WINAPI *)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
	auto memory = reinterpret_cast<GetMemory>(
		GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
	PROCESS_MEMORY_COUNTERS counters{};
	counters.cb = sizeof(counters);
	if (memory && memory(GetCurrentProcess(), &counters, sizeof(counters)))
		peakWorkingSet = counters.PeakWorkingSetSize;
#endif
	std::cout << "WITNESS asset_sha256=" << D::Hex(D::Hash(first.data(), first.size()))
			  << " key=" << D::Hex(Key(f)) << '\n';
	std::cout << "METRICS cold_build_seconds=" << cold << " field_rays=" << field.work.rays
			  << " shadow_rays=" << vis.work.rays << " file_bytes=" << w.fileBytes
			  << " all_payload=" << w.payloadBytes << " field_payload=" << fw.payloadBytes
			  << " visibility_payload=" << sw.payloadBytes << " warm_open_seconds=" << w.openSeconds
			  << " warm_prepare_seconds=" << w.prepareSeconds << " warm_read_bytes=" << w.readBytes
			  << " warm_reads=" << w.reads << " prepared_reservation=" << w.preparedBudgetBytes
			  << " cold_allocations=" << coldAllocations << " warm_allocations=" << warmAllocations
			  << " warm_allocated_bytes=" << warmAllocationBytes
			  << " peak_process_working_set=" << peakWorkingSet
			  << " warm_solver_steps=0 warm_rays=0 query_allocations=0\n";
}
void SelectiveBenchmark(const std::string &root)
{
	Scene scene;
	scene.domain->world=scene.options.bounds=Box(Vec3(-2,-2,0),Vec3(2,2,4));
	auto geometry=scene.Input();auto fingerprint=Fingerprint();D::Cache cache(root);
	for(unsigned mode=0;mode<4;++mode) {
		PC::Selection selection;PC::Item item;item.geometry=geometry.geometry;
		if(mode==1||mode==3){item.id=1000;selection.fields.push_back(item);}
		if(mode==2||mode==3){item.id=900;selection.visibility.push_back(item);}
		double open=0,prepare=0,release=0;PC::Work work;BuildProduct product;
		for(unsigned repeat=0;repeat<10;++repeat){
			OK(PC::Load(cache,fingerprint,selection,product,work));open+=work.openSeconds;prepare+=work.prepareSeconds;
			auto start=std::chrono::steady_clock::now();product={};release+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
		}
		std::cout<<"SELECTIVE mode="<<mode<<" open_seconds="<<open/10<<" prepare_seconds="<<prepare/10<<" release_seconds="<<release/10<<" read_bytes="<<work.readBytes<<" payload_bytes="<<work.payloadBytes<<" chunks="<<work.chunks<<'\n';
		if(mode==1){OK(PC::Load(cache,fingerprint,selection,product,work));ConditionalFieldQuery q;q.field.id=1000;q.optIn=true;q.positionWorld=Vec3(-1,-1,1);q.directionOrNormalWorld=Vec3(0,0,1);ConditionalFieldSample value;auto start=std::chrono::steady_clock::now();for(unsigned j=0;j<100000;++j)if(product.evaluator->ConditionalField(*product.asset,q,value)!=Status::Ok)throw std::runtime_error("Benchmark query unsupported");
			std::cout<<"QUERY seconds_per_sample="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/100000<<" samples=100000\n";}
	}
}
int main(int argc, char **argv)
{
	try
	{
		std::string root = argc > 1 ? argv[1] : "";
		if (root.empty())
			throw std::runtime_error("Explicit absolute cache test root required");
		if(argc>2&&std::string(argv[2])=="benchmark"){SelectiveBenchmark(root);return failures?1:0;}
		if (argc > 3 && std::string(argv[2]) == "seed-diagnostic")
		{
			// Runtime I/O witness ONLY. No sources, photons or recovery claims.
			std::ifstream in(argv[3]);
			if (!in)
				throw std::runtime_error("Missing production manifest log");
			D::Fingerprint f;
			std::string line, expected;
			while (std::getline(in, line))
			{
				auto p = line.find("dependency-manifest algorithm=");
				if (p != std::string::npos)
				{
					f = {};
					f.complete.fill(true);
					std::istringstream row(line.substr(p));
					std::string token;
					row >> token;
					while (row >> token)
					{
						auto split = token.find('=');
						auto k = token.substr(0, split), v = token.substr(split + 1);
						if (k == "algorithm")
							f.algorithm = v;
						else if (k == "version")
							f.algorithmVersion = uint32_t(std::stoul(v));
						else if (k == "world-units")
							f.units.worldUnitsPerMeter = std::stof(v);
						else if (k == "physical-to-scene")
							f.units.physicalToScene = std::stof(v);
						else if (k == "key")
							expected = v;
					}
				}
				p = line.find("dependency kind=");
				if (p == std::string::npos)
					continue;
				std::istringstream row(line.substr(p));
				std::string token;
				row >> token;
				D::InputContent c;
				while (row >> token)
				{
					auto split = token.find('=');
					auto k = token.substr(0, split), v = token.substr(split + 1);
					if (k == "kind")
						c.kind = D::InputKind(std::stoul(v));
					else if (k == "bytes")
						c.bytes = std::stoull(v);
					else if (k == "name")
						c.name = v;
					else if (k == "sha256")
					{
						if (v.size() != 64)
							throw std::runtime_error("Bad manifest hash");
						for (size_t j = 0; j < 32; ++j)
							c.sha256[j] = uint8_t(std::stoul(v.substr(j * 2, 2), nullptr, 16));
					}
				}
				f.inputs.push_back(c);
			}
			CHECK(D::Hex(Key(f)) == expected);
			if (failures)
				return 1;
			auto a = Asset();
			a.units = f.units;
			a.calibrations.clear();
			a.lineage = "V01/cache-io-witness-NOT-a-reconstruction";
			Diagnostic d;
			d.code = "CACHE_IO_TEST_ONLY";
			d.message = "Empty diagnostic asset tests map-load cache ownership; no lighting "
						"evidence or original-source recovery";
			a.diagnostics.push_back(d);
			OK(D::Cache(root).Store(f, a));
			std::cout << "diagnostic-only key=" << expected << " sources=0\n";
			return failures ? 1 : 0;
		}
		if (argc > 2 && std::string(argv[2]) == "crash")
		{
			D::Cache c(root);
			auto a = Asset();
			OK(c.Store(Fingerprint(), a));
			a.lineage = "must-not-publish";
			Stop s;
			s.stage = D::WriteStage::Flushed;
			s.crash = true;
			c.Store(Fingerprint(), a, {}, D::Limits(), &s);
			return 1;
		}
		if (argc > 2 && std::string(argv[2]) == "verify-crash")
		{
			ReconstructedLightingAsset a;
			D::Index i;
			std::unique_ptr<D::FileReader> r;
			OK(D::Cache(root).Load(Fingerprint(), a, i, r));
			CHECK(a.lineage == Asset().lineage);
		}
		else
		{
			Dependencies();
			Atomic(root + "/atomic");
			PreparedCache(root + "/prepared");
		}
	}
	catch (const std::exception &e)
	{
		++failures;
		std::cerr << "EXCEPTION " << e.what() << '\n';
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
