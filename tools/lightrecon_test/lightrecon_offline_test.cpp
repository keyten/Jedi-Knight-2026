// Dataset integrity, ownership, geometry, adversarial-input and fixture tests.
#include "offline_fixtures.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>
#include <fstream>

using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace O = Rend2LightRecon::Core::Offline;
namespace
{
int checks = 0, failures = 0;
double maximumPositionError = 0;
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
struct Memory final : O::Writer, Disk::Reader
{
	std::vector<uint8_t> bytes;
	Error Write(uint64_t offset, const void *p, size_t n) override
	{
		if (offset > 64 * 1024 * 1024 || n > 64 * 1024 * 1024 - offset)
			return Failure(Code::MemoryLimit, "Test stream limit");
		if (offset + n > bytes.size())
			bytes.resize(size_t(offset + n));
		if (n)
			std::memcpy(bytes.data() + offset, p, n);
		return {};
	}
	uint64_t Size() const override
	{
		return bytes.size();
	}
	Disk::Error Read(uint64_t offset, void *p, size_t n) const override
	{
		Disk::Error e;
		if (offset > bytes.size() || n > bytes.size() - offset)
		{
			e.code = Disk::Code::Corrupt;
			return e;
		}
		if (n)
			std::memcpy(p, bytes.data() + offset, n);
		return e;
	}
};
ReadContext Ctx()
{
	ReadContext c;
	c.maxBytes = 256ull * 1024 * 1024;
	return c;
}
O::Options Opt()
{
	O::Options o;
	o.mappedImages = 0;
	return o;
}
std::shared_ptr<Memory> Encode(const O::Sources &s)
{
	auto m = std::make_shared<Memory>();
	O::Summary report;
	auto e = O::Export(s, Opt(), Ctx(), *m, report);
	if (!e)
		std::printf("export error %s\n", e.message.c_str());
	CHECK(e);
	CHECK(report.fileBytes == m->bytes.size());
	return m;
}
std::shared_ptr<const O::Dataset> Open(std::shared_ptr<Memory> m)
{
	std::shared_ptr<const O::Dataset> d;
	auto e = O::Dataset::Open(m, Opt(), d);
	if (!e)
		std::printf("open error %s\n", e.message.c_str());
	CHECK(e);
	return d;
}
uint64_t U(const std::vector<uint8_t> &v, size_t p, unsigned bytes = 8)
{
	uint64_t n = 0;
	for (unsigned i = 0; i < bytes; ++i)
		n |= uint64_t(v[p + i]) << (i * 8);
	return n;
}
void Put(std::vector<uint8_t> &v, size_t p, uint64_t n, unsigned bytes = 8)
{
	for (unsigned i = 0; i < bytes; ++i)
		v[p + i] = uint8_t(n >> (i * 8));
}
void Digest(std::vector<uint8_t> &v, size_t p, const void *data, size_t n)
{
	auto h = Disk::Hash(data, n);
	std::copy(h.begin(), h.end(), v.begin() + p);
}
void Rehash(Memory &m, bool payload = true)
{
	auto &v = m.bytes;
	size_t dir = size_t(U(v, 40));
	unsigned count = unsigned(U(v, 28, 4));
	for (unsigned i = 0; payload && i < count; ++i)
	{
		size_t entry = dir + i * 80;
		uint64_t offset = U(v, entry + 32), bytes = U(v, entry + 40);
		Digest(v, entry + 48, v.data() + offset, size_t(bytes));
	}
	Digest(v, 96, v.data() + dir, size_t(U(v, 48)));
	Digest(v, 128, v.data(), 128);
}
void Reject(std::shared_ptr<Memory> m)
{
	std::shared_ptr<const O::Dataset> d;
	auto e = O::Dataset::Open(m, Opt(), d);
	CHECK(!e);
	CHECK(!d);
}
void Corruption(const O::Sources &s)
{
	auto original = Encode(s);
	for (size_t length :
		 {size_t(0), size_t(8), size_t(159), size_t(200), original->bytes.size() - 1})
	{
		auto m = std::make_shared<Memory>(*original);
		m->bytes.resize(length);
		Reject(m);
	}
	auto m = std::make_shared<Memory>(*original);
	m->bytes[8] = 2;
	Rehash(*m, false);
	Reject(m);
	m = std::make_shared<Memory>(*original);
	m->bytes[200] ^= 1;
	Reject(m);
	m = std::make_shared<Memory>(*original);
	m->bytes[64] ^= 1;
	Rehash(*m, false);
	Reject(m);
	m = std::make_shared<Memory>(*original);
	size_t dir = size_t(U(m->bytes, 40));
	Put(m->bytes, dir + 32, 0);
	Rehash(*m, false);
	Reject(m);
	m = std::make_shared<Memory>(*original);
	dir = size_t(U(m->bytes, 40));
	Put(m->bytes, dir + 40, ~uint64_t(0));
	Rehash(*m, false);
	Reject(m);
	auto valid = Open(original);
	auto meta = std::find_if(valid->Directory().begin(), valid->Directory().end(),
							 [](const O::Entry &e) { return e.kind == 1; });
	CHECK(meta != valid->Directory().end());
	m = std::make_shared<Memory>(*original);
	Put(m->bytes, size_t(meta->offset + meta->bytes - 12), 999, 4);
	Rehash(*m);
	Reject(m);
	// Malicious array count: structural open succeeds, bounded typed import rejects before resize.
	auto image = std::find_if(valid->Directory().begin(), valid->Directory().end(),
							  [](const O::Entry &e) { return e.kind == 2; });
	m = std::make_shared<Memory>(*original);
	Put(m->bytes, size_t(image->offset), ~uint64_t(0));
	Rehash(*m);
	auto attack = Open(m);
	O::Sources loaded;
	O::RegionSnapshot regions;
	CHECK(attack->Load(loaded, regions).code == Code::MemoryLimit);
	CHECK(!loaded.geometry);
	// Rehashed tampering in world position is detected by numerical correspondence validation.
	auto tile = std::find_if(valid->Directory().begin(), valid->Directory().end(),
							 [](const O::Entry &e) { return e.kind == 15; });
	m = std::make_shared<Memory>(*original);
	float position[3] = {-180, -120, 0};
	auto begin = m->bytes.begin() + size_t(tile->offset), end = begin + size_t(tile->bytes);
	auto p = std::search(begin, end, reinterpret_cast<uint8_t *>(position),
						 reinterpret_cast<uint8_t *>(position) + 12);
	CHECK(p != end);
	if (p != end)
	{
		float changed = -172;
		std::memcpy(&*p, &changed, 4);
		Rehash(*m);
		attack = Open(m);
		uint64_t checked = 0;
		CHECK(!attack->Validate(true, checked));
	}
	O::Options limits = Opt();
	limits.maxFileBytes = 100;
	std::shared_ptr<const O::Dataset> none;
	CHECK(!O::Dataset::Open(original, limits, none));
	CHECK(!none);
	// Append a zero-length unknown extension; update both size declarations and all hashes.
	// Optional extensions survive validation; future required semantics must be rejected.
	m = std::make_shared<Memory>(*original);
	dir = size_t(U(m->bytes, 40));
	const size_t oldSize = m->bytes.size();
	const uint64_t entries = U(m->bytes, 28, 4);
	bool sizeFound = false;
	for (size_t pos = size_t(meta->offset); pos + 8 <= meta->offset + meta->bytes; ++pos)
		if (U(m->bytes, pos) == oldSize)
		{
			Put(m->bytes, pos, oldSize + 80);
			sizeFound = true;
			break;
		}
	CHECK(sizeFound);
	m->bytes.resize(oldSize + 80, 0);
	std::memmove(m->bytes.data() + dir + 80, m->bytes.data() + dir, size_t(entries * 80));
	std::fill(m->bytes.begin() + dir, m->bytes.begin() + dir + 80, uint8_t(0));
	Put(m->bytes, dir, 999, 4);
	Put(m->bytes, dir + 32, 160); // sorted empty optional section precedes existing payloads
	Put(m->bytes, 28, entries + 1, 4);
	Put(m->bytes, 32, oldSize + 80);
	Put(m->bytes, 48, (entries + 1) * 80);
	Rehash(*m);
	auto extension = Open(m);
	uint64_t checked = 0;
	CHECK(extension->Validate(true, checked));
	Put(m->bytes, dir + 4, 1, 4);
	Rehash(*m);
	Reject(m);
}
void RoundTrip(const std::string &name)
{
	auto fixture = OfflineFixtures::Make(name);
	CHECK(fixture.answer.find("ANSWER_ONLY_DO_NOT_EXPORT") != std::string::npos);
	auto bytes = Encode(fixture.sources);
	auto repeated = Encode(OfflineFixtures::Make(name).sources);
	CHECK(bytes->bytes == repeated->bytes);
	std::string raw(bytes->bytes.begin(), bytes->bytes.end());
	CHECK(raw.find("ANSWER_ONLY_DO_NOT_EXPORT") == std::string::npos);
	CHECK(raw.find("\"classname\" \"light\"") == std::string::npos);
	auto d = Open(bytes);
	uint64_t checked = 0;
	CHECK(d->Validate(true, checked));
	CHECK(checked >= 286);
	CHECK(d->Counts().gridCells == 30);
	CHECK(d->Counts().invalidGrid == 1);
	CHECK(d->Counts().missingGridCells == 2);
	CHECK(d->Info().hints == O::HintPolicy::BlindBenchmark);
	CHECK(d->Info().compilerKnown);
	O::Sources loaded;
	O::RegionSnapshot graph;
	CHECK(d->Load(loaded, graph));
	CHECK(loaded.geometry && loaded.images && loaded.grid && loaded.domain);
	CHECK(loaded.geometry->Map().sha256 == fixture.sources.geometry->Map().sha256);
	CHECK(loaded.images->Images()[0].logicalIndex == 0);
	CHECK(*loaded.images->Images()[0].bytes == *fixture.sources.images->Images()[0].bytes);
	CHECK(loaded.grid->Indices() == fixture.sources.grid->Indices());
	CHECK(graph.cells.size() == fixture.sources.regions->Cells().size());
	for (const auto &s : loaded.geometry->Surfaces())
	{
		CHECK(s.materialFacts.authoredLuminance == 0);
		CHECK(s.materialFacts.legacySurfaceLight == 0);
	}
	if (loaded.evidence)
		for (const auto &e : loaded.evidence->Evidence())
		{
			CHECK(!e.intensityKnown && e.unit == EvidenceUnit::Unknown && e.entities.empty() &&
				  e.raw.empty());
			CHECK(e.origin != EvidenceOrigin::BspEntity &&
				  e.origin != EvidenceOrigin::AuthoredArea &&
				  e.origin != EvidenceOrigin::SunShader);
		}
	std::shared_ptr<const StaticVisibility> replayRays;
	CHECK(O::ReplayGeometry(loaded, graph, Ctx(), replayRays, loaded.regions));
	CHECK(loaded.regions->Cells().size() == graph.cells.size());
	auto round = Encode(loaded);
	CHECK(round->bytes == bytes->bytes);
	unsigned partitions[3] = {};
	bool saturated = false, nearly = false;
	for (size_t i = 0; i < d->Directory().size(); ++i)
		if (d->Directory()[i].kind == 15)
		{
			std::vector<O::Luxel> tile;
			CHECK(d->ReadLuxels(i, tile));
			for (const auto &l : tile)
			{
				++partitions[unsigned(l.partition)];
				for (unsigned c = 0; c < 3; ++c)
				{
					saturated |= (l.colour.channels[c] & Decode::Saturated) != 0;
					nearly |= (l.colour.channels[c] & Decode::NearSaturated) != 0;
				}
				if (l.surface.positionKnown)
				{
					CHECK(l.surface.positionWorld.z == 0);
					double dx = l.surface.positionWorld.x - (-192 + (l.surface.x + .5) * 24);
					double dy = l.surface.positionWorld.y - (-128 + (l.surface.y + .5) * 16);
					maximumPositionError =
						std::max(maximumPositionError, std::sqrt(dx * dx + dy * dy));
					CHECK(std::abs(dx) < 1e-5 && std::abs(dy) < 1e-5);
					CHECK(std::abs(l.surface.areaWorldSquared - 384) < 1e-5);
					CHECK(l.surface.areaWorldSquared > 0);
					CHECK(l.surface.materialIndex == 0);
					CHECK(l.partition == O::Split(d->Info(), l.surface.positionWorld));
				}
			}
		}
	CHECK(partitions[0] && partitions[1] && partitions[2]);
	CHECK(saturated && nearly);
	// Geometry and probes in the same spatial block always share their partition.
	CHECK(O::Split(d->Info(), Vec3(4, 5, 6)) == O::Split(d->Info(), Vec3(10, 50, 20)));
	if (name == "wall_leak" || name == "door_occlusion")
	{
		VisibilityBuildOptions opt;
		opt.patchToleranceWorld = d->Info().visibilityPatchToleranceWorld;
		std::shared_ptr<const StaticVisibility> rays;
		CHECK(StaticVisibility::Build(loaded.geometry, opt, Ctx(), rays, loaded.domain));
		RayQuery q;
		q.startWorld = Vec3(-80, 0, 80);
		q.endWorld = Vec3(80, 0, 80);
		RayResult r;
		CHECK(rays->Query(q, Ctx(), r));
		CHECK(r.state ==
			  (name == "wall_leak" ? VisibilityState::Blocked : VisibilityState::Visible));
		q.startWorld.y = q.endWorld.y = 90;
		CHECK(rays->Query(q, Ctx(), r));
		CHECK(r.state == VisibilityState::Blocked);
		if (name == "wall_leak")
		{
			const auto &rgb = *loaded.images->Images()[0].bytes;
			CHECK(rgb[(8 * 16 + 3) * 3] > rgb[(8 * 16 + 12) * 3] + 40);
			GridCell cell;
			CHECK(loaded.grid->Cell(7, cell));
			CHECK(cell.styles[0] == 0);
			CHECK(cell.directed[0][0] == 70);
			CHECK(cell.geometry == GridGeometry::Unknown);
			SpatialLocation location;
			CHECK(loaded.domain->Locate(cell.centreWorld, location));
			CHECK(location.space == SpaceClass::Solid);
		}
	}
	if (name == "bounce_colour")
	{
		CHECK(loaded.grid->Report().nonFiniteHdr == 1);
		CHECK(loaded.images->Images().size() == 3);
	}
	if (name == "point_room")
		Corruption(fixture.sources);
}
void MetadataErrors()
{
	auto f = OfflineFixtures::Make("point_room");
	Memory writer;
	O::Summary summary;
	auto bad = f.sources;
	bad.metadata.compilerVersion.clear();
	CHECK(!O::Export(bad, Opt(), Ctx(), writer, summary));
	bad = f.sources;
	bad.metadata.recipe += "changed";
	CHECK(!O::Export(bad, Opt(), Ctx(), writer, summary));
	bad = f.sources;
	bad.metadata.hints = O::HintPolicy::ObservedPriors;
	CHECK(!O::Export(bad, Opt(), Ctx(), writer, summary));
	struct Cancel : Cancellation
	{
		bool IsCancelled() const noexcept override
		{
			return true;
		}
	} cancel;
	auto ctx = Ctx();
	ctx.cancellation = &cancel;
	CHECK(O::Export(f.sources, Opt(), ctx, writer, summary).code == Code::Cancelled);
}
void Empty()
{
	O::Sources s;
	s.metadata.map.path = "synthetic://empty";
	s.metadata.map.bytes = 1;
	s.metadata.map.sha256 = Disk::Hash("x", 1);
	s.metadata.producer = "CPU test";
	s.metadata.producerVersion = "1";
	auto bytes = Encode(s);
	auto d = Open(bytes);
	CHECK(!d->Counts().images && !d->Counts().surfaces && !d->Counts().gridCells);
	uint64_t checked = 123;
	CHECK(d->Validate(true, checked));
	CHECK(checked == 0);
	O::Sources imported;
	O::RegionSnapshot graph;
	CHECK(d->Load(imported, graph));
	CHECK(!imported.images && !imported.geometry && !imported.grid);
	CHECK(Encode(imported)->bytes == bytes->bytes);
}
void NativeLifetime()
{
	const char *path = "lightrecon-offline-native-test.jkobs";
	auto first = OfflineFixtures::Make("point_room");
	O::Summary summary;
	CHECK(O::ExportFile(path, first.sources, Opt(), Ctx(), summary));
	std::shared_ptr<const O::Dataset> old;
	CHECK(O::OpenFile(path, Opt(), old));
	const auto hash = old->Info().map.sha256;
	struct Cancel : Cancellation
	{
		bool IsCancelled() const noexcept override
		{
			return true;
		}
	} cancel;
	auto context = Ctx();
	context.cancellation = &cancel;
	auto second = OfflineFixtures::Make("spot");
	CHECK(O::ExportFile(path, second.sources, Opt(), context, summary).code == Code::Cancelled);
	std::shared_ptr<const O::Dataset> current;
	CHECK(O::OpenFile(path, Opt(), current));
	CHECK(current->Info().map.sha256 == hash);
	const auto replacement = O::ExportFile(path, second.sources, Opt(), Ctx(), summary);
	if (!replacement)
		std::printf("Native replacement error: %s\n", replacement.message.c_str());
	CHECK(replacement);
	CHECK(O::OpenFile(path, Opt(), current));
	CHECK(current->Info().map.sha256 != hash);
	uint64_t checked = 0;
	CHECK(old->Validate(true, checked)); // open file lease survives atomic replacement
	O::Sources retained;
	O::RegionSnapshot graph;
	CHECK(old->Load(retained, graph));
	old.reset();
	CHECK(retained.images->Map().sha256 == hash);
	CHECK(current->Validate(true, checked));
	current.reset();
	CHECK(std::remove(path) == 0);
	CHECK(!O::OpenFile(path, Opt(), current));
	CHECK(!current);
	{
		std::ofstream corrupt(path, std::ios::binary);
		corrupt << "truncated";
	}
	CHECK(!O::OpenFile(path, Opt(), current));
	CHECK(!current);
	CHECK(std::remove(path) == 0);
}
} // namespace
int main()
{
	for (const auto &name : OfflineFixtures::Names())
		RoundTrip(name);
	MetadataErrors();
	Empty();
	NativeLifetime();
	std::printf("offline dataset: %d checks, %d failures; 11 quantized scenes, q3map2 not run\n",
				checks, failures);
	std::printf("Maximum fixture luxel position error: %.9g world units\n", maximumPositionError);
	return failures ? 1 : 0;
}
