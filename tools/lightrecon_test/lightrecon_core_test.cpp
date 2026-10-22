// No OpenGL/engine startup; all algorithms/providers below are test fixtures.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/session.h"
#include <atomic>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static_assert(std::is_const<typename std::remove_pointer<decltype(Chunk<GridObservation>().data)>::type>::value, "input observations must be const");
static_assert(std::is_const<typename decltype(Session(InputDataset()).Output())::element_type>::value, "finished output must be const");

struct Cancel : Cancellation
{
	std::atomic<bool> requested{false};
	bool IsCancelled() const noexcept override { return requested.load(); }
};
template<typename T> struct MemorySource : ChunkSource<T>
{
	std::shared_ptr<const std::vector<T>> records;
	mutable unsigned calls = 0;
	mutable size_t largest = 0;
	Code failure = Code::Ok;
	bool shortRead = false, noLease = false, throws = false;
	explicit MemorySource(std::vector<T> rows) : records(std::make_shared<const std::vector<T>>(std::move(rows))) {}
	Error Read(uint64_t first, size_t count, const ReadContext& context, Chunk<T>& out) const override
	{
		++calls; if (count > largest) largest = count;
		if (throws) throw std::runtime_error("synthetic provider exception");
		if (context.cancellation && context.cancellation->IsCancelled()) return Failure(Code::Cancelled, "provider cancelled");
		if (failure != Code::Ok) return Failure(failure, "synthetic stream failure");
		if (first > records->size() || count > records->size()-first) return Failure(Code::ProviderError, "fixture range unavailable");
		out = VectorChunk(records, static_cast<size_t>(first), shortRead ? count-1 : count);
		if (noLease) out.owner.reset();
		return Error();
	}
};
struct Visibility : VisibilityProvider
{
	Code code = Code::Ok;
	float value = 0.5f;
	std::shared_ptr<Cancel> cancelAfter;
	mutable unsigned calls = 0;
	mutable Vec3 skyDirection;
	Error Segment(const Vec3&, const Vec3&, const ReadContext&, float& out) const override
	{
		++calls; out = value;
		if (cancelAfter) cancelAfter->requested = true;
		return code == Code::Ok ? Error() : Failure(code, "trace provider failure");
	}
	Error Sky(const Vec3&, const Vec3& dir, const ReadContext&, float& out) const override
	{ skyDirection = dir; return Segment(Vec3(), dir, ReadContext(), out); }
};
InputDataset Base()
{
	InputDataset d; auto& m = d.manifest; m.lineage = "core-test/scene"; m.revision = "sha256:test-input-1"; m.provenance = 1;
	Provenance p; p.id = 1; p.kind = ProvenanceKind::Authored; p.producer = "core-fixture/1"; m.provenanceRecords.push_back(p);
	Calibration c; c.id = 1; c.kind = CalibrationKind::PhysicallyAuthored; c.provenance = 1; m.calibrations.push_back(c);
	c.id = 2; c.kind = CalibrationKind::UncalibratedRelative; m.calibrations.push_back(c);
	return d;
}
SourceHint Hint(Id id = 10)
{
	SourceHint h; h.id = id; h.provenance = 1; h.emitter.source.id = 100; h.emitter.source.provenance = 1;
	h.emitter.position.known = true; h.emitter.position.world = Vec3(1.0f, 2.0f, 3.0f);
	h.emitter.emission.unit = PhotometricUnit::Candela; h.emitter.emission.calibration = 1;
	h.emitter.emission.intensity.known = true; h.emitter.emission.intensity.value = 100.0f;
	NormalizeChromaticity(Vec3(1.0f, 0.5f, 0.25f), h.emitter.emission.color); return h;
}
InputDataset WithHint()
{
	auto d = Base(); d.manifest.hints = 1; d.hints = std::make_shared<MemorySource<SourceHint>>(std::vector<SourceHint>{Hint()}); return d;
}
struct Log : Logger
{
	unsigned calls = 0; Code last = Code::Ok;
	void Write(const Error& e) noexcept override { ++calls; last = e.code; }
};
struct ProgressLog : ProgressSink
{
	unsigned calls = 0;
	bool failCompletion = false;
	std::vector<Progress> records;
	Error Report(const Progress& p) override
	{
		++calls; records.push_back(p);
		return failCompletion && p.state == SessionState::Completed ? Failure(Code::ProviderError, "progress sink failed") : Error();
	}
};
enum class Work { CopyHint, Trace, Loop, CorruptOutput, Throw };
struct TestAlgorithm : Algorithm
{
	Work work; Requirements needs;
	std::shared_ptr<std::vector<uint64_t>> seeds;
	explicit TestAlgorithm(Work w = Work::CopyHint) : work(w) {}
	Requirements Needs() const override { return needs; }
	WorkResult Step(InputReader& input, AssetBuilder& builder, const AlgorithmContext& context) override
	{
		if (seeds) seeds->push_back(context.options.randomSeed + context.step);
		if (work == Work::Throw) throw std::runtime_error("synthetic algorithm exception");
		WorkResult r;
		if (work == Work::Trace)
		{
			float v = 77.0f; r.error = input.TraceSegment(Vec3(), Vec3(1.0f, 0.0f, 0.0f), v);
			r.next = Next::Complete; return r;
		}
		if (work == Work::Loop) { r.next = Next::Continue; return r; }
		if (work == Work::CorruptOutput) { builder.Draft().version = 999; r.next = Next::Complete; return r; }
		Chunk<SourceHint> hints; r.error = input.ReadHints(0, 1, hints);
		if (r.error) builder.Draft().emitters.push_back(hints.data[0].emitter);
		r.next = Next::Complete; return r;
	}
};
void EmptyAndMetadata()
{
	CHECK(ValidateInput(InputDataset()));
	Session empty{InputDataset()}; CHECK(empty.State() == SessionState::Created); CHECK(!empty.Output());
	CHECK(empty.Begin().code == Code::Unavailable); CHECK(empty.State() == SessionState::Unavailable);
	CHECK(empty.Step().code == Code::InvalidState); CHECK(!empty.Output());
	auto d = WithHint(); auto provider = std::static_pointer_cast<const MemorySource<SourceHint>>(d.hints);
	Session noAlgorithm(d); CHECK(provider->calls == 0); CHECK(noAlgorithm.Begin().code == Code::Unavailable); CHECK(provider->calls == 0);
	d.manifest.version = 2; Session version(d); CHECK(version.Begin().code == Code::UnsupportedSchema);
	d.manifest.version = 1; d.manifest.schema = "unknown"; CHECK(ValidateInput(d).code == Code::UnsupportedSchema);
	d = WithHint(); d.manifest.revision.clear(); CHECK(ValidateInput(d).code == Code::InvalidInput);
	d = Base(); d.manifest.triangles = 1; CHECK(ValidateInput(d).code == Code::MissingGeometry);
	d = WithHint(); d.hints.reset(); CHECK(ValidateInput(d).code == Code::Unavailable);
	d = WithHint(); d.manifest.units.worldUnitsPerMeter = 0.0f; CHECK(ValidateInput(d).code == Code::InvalidInput);
	ReadLimits zero; zero.maxRecords = 0; CHECK(InputReader(Base(), zero).ValidateMetadata().code == Code::InvalidInput);
	auto algorithm = std::unique_ptr<TestAlgorithm>(new TestAlgorithm); algorithm->needs.geometry = true;
	Session geometry(WithHint(), Options(), Services(), std::move(algorithm)); CHECK(geometry.Begin().code == Code::MissingGeometry); CHECK(!geometry.Output());
}
void StreamingAndOwnership()
{
	auto d = Base(); std::vector<SurfaceObservation> records(3);
	for (size_t i = 0; i < records.size(); ++i)
	{
		records[i].id = 10+i; records[i].surface = 1; records[i].provenance = 1;
		records[i].normalKnown = true; records[i].normalWorld = Vec3(0.0f, 0.0f, 1.0f);
		records[i].lighting.issues = ObservationClipped | ObservationQuantized;
	}
	auto source = std::make_shared<MemorySource<SurfaceObservation>>(records); d.surfaces = source; d.manifest.surfaces = 3;
	ReadLimits limits; limits.maxRecords = 2; limits.maxBytes = 2*sizeof(SurfaceObservation);
	InputReader reader(d, limits); CHECK(source->calls == 0);
	Chunk<SurfaceObservation> block;
	CHECK(reader.ReadSurfaces(0, 3, block).code == Code::MemoryLimit); CHECK(source->calls == 0 && !block.owner);
	CHECK(reader.ReadSurfaces(0, 2, block)); CHECK(block.count == 2 && block.data == source->records->data());
	CHECK(reader.ReadSurfaces(2, 1, block)); CHECK(block.data[0].id == 12);
	CHECK(reader.ReadSurfaces(0, 2, block)); CHECK(block.data[0].id == 10); // repeatable ordinal reads
	CHECK(reader.ReadSurfaces(3, 0, block)); CHECK(block.count == 0);
	CHECK(reader.ReadSurfaces(std::numeric_limits<uint64_t>::max(), 1, block).code == Code::InvalidInput);
	CHECK(source->largest <= 2);
	d.manifest.surfaces = std::numeric_limits<uint64_t>::max();
	Session huge(d); CHECK(huge.Begin().code == Code::Unavailable); CHECK(source->calls == 3); // no eager read of a huge declared stream
	Chunk<SourceHint> held; std::weak_ptr<const std::vector<SourceHint>> weak;
	{
		auto owned = WithHint(); auto p = std::static_pointer_cast<const MemorySource<SourceHint>>(owned.hints); weak = p->records;
		InputReader local(std::move(owned)); CHECK(local.ReadHints(0, 1, held));
	}
	CHECK(!weak.expired() && held.data[0].emitter.source.id == 100); held = Chunk<SourceHint>(); CHECK(weak.expired());
	records[1].id = records[0].id;
	d = Base(); d.manifest.surfaces = 3; d.surfaces = std::make_shared<MemorySource<SurfaceObservation>>(records);
	InputReader order(d); CHECK(order.ReadSurfaces(0, 1, block));
	auto error = order.ReadSurfaces(1, 1, block); CHECK(error.code == Code::InvalidInput && error.ordinal == 1 && error.record == 10); CHECK(!block.owner);
}
void RecordsAndProviderFailures()
{
	auto d = Base(); GridObservation g; g.id = 20; g.provenance = 1; g.directionKnown = true; g.directionToSource = Vec3(0.0f, 1.0f, 0.0f);
	g.ambient.known = true; g.ambient.calibration = 2; g.ambient.issues = ObservationMissing;
	d.grid = std::make_shared<MemorySource<GridObservation>>(std::vector<GridObservation>{g}); d.manifest.grid = 1;
	d.manifest.gridMeaning = GridMeaning::LegacyLdrMaximum;
	Chunk<GridObservation> grid; CHECK(InputReader(d).ReadGrid(0, 1, grid)); CHECK(grid.data[0].ambient.issues == ObservationMissing);
	g.directionToSource = Vec3(); d.grid = std::make_shared<MemorySource<GridObservation>>(std::vector<GridObservation>{g});
	CHECK(InputReader(d).ReadGrid(0, 1, grid).code == Code::InvalidInput);
	g.directionKnown = false; g.ambient.rgb.x = std::numeric_limits<float>::quiet_NaN();
	d.grid = std::make_shared<MemorySource<GridObservation>>(std::vector<GridObservation>{g}); CHECK(InputReader(d).ReadGrid(0, 1, grid).code == Code::InvalidInput);
	d = Base(); Triangle triangle; triangle.id = 30; triangle.provenance = 1; triangle.verticesWorld[1] = Vec3(1.0f, 0.0f, 0.0f); triangle.verticesWorld[2] = Vec3(0.0f, 1.0f, 0.0f);
	d.manifest.triangles = 1; d.triangles = std::make_shared<MemorySource<Triangle>>(std::vector<Triangle>{triangle}); Chunk<Triangle> triangles;
	CHECK(InputReader(d).ReadTriangles(0, 1, triangles)); triangle.verticesWorld[2] = triangle.verticesWorld[1];
	d.triangles = std::make_shared<MemorySource<Triangle>>(std::vector<Triangle>{triangle}); CHECK(InputReader(d).ReadTriangles(0, 1, triangles).code == Code::InvalidInput);
	d = Base(); Material material; material.id = 7; material.provenance = 1; material.support = MaterialSupport::Unsupported;
	d.manifest.materials = 1; d.materials = std::make_shared<MemorySource<Material>>(std::vector<Material>{material}); Chunk<Material> materials;
	CHECK(InputReader(d).ReadMaterials(0, 1, materials)); CHECK(materials.data[0].support == MaterialSupport::Unsupported);
	material.metallic = -1.0f; d.materials = std::make_shared<MemorySource<Material>>(std::vector<Material>{material}); CHECK(InputReader(d).ReadMaterials(0, 1, materials).code == Code::InvalidInput);
	auto h = Hint(); h.emitter.geometry.radiusWorld = -1.0f;
	d = WithHint(); auto provider = std::make_shared<MemorySource<SourceHint>>(std::vector<SourceHint>{h}); d.hints = provider; Chunk<SourceHint> hints;
	CHECK(InputReader(d).ReadHints(0, 1, hints).code == Code::InvalidInput);
	provider->failure = Code::ProviderError; CHECK(InputReader(d).ReadHints(0, 1, hints).code == Code::ProviderError);
	provider->failure = Code::Ok; provider->shortRead = true; CHECK(InputReader(d).ReadHints(0, 1, hints).code == Code::ProviderError);
	provider->shortRead = false; provider->noLease = true; CHECK(InputReader(d).ReadHints(0, 1, hints).code == Code::ProviderError);
	provider->noLease = false; provider->throws = true; CHECK(InputReader(d).ReadHints(0, 1, hints).code == Code::ProviderError);
	h = SourceHint(); h.id = 10; h.provenance = 1; h.kind = HintKind::TransportAperture; h.geometry = 50;
	d.hints = std::make_shared<MemorySource<SourceHint>>(std::vector<SourceHint>{h}); CHECK(InputReader(d).ReadHints(0, 1, hints));
	CHECK(hints.data[0].emitter.source.id == 0); // doorway hint is not a physical light
}
void VisibilityAndCancellation()
{
	auto d = Base(); float value = 77.0f;
	CHECK(InputReader(d).TraceSegment(Vec3(), Vec3(1.0f, 0.0f, 0.0f), value).code == Code::Unavailable && value == 77.0f);
	auto visibility = std::make_shared<Visibility>(); d.visibility = visibility;
	InputReader reader(d); CHECK(reader.TraceSegment(Vec3(), Vec3(1.0f, 0.0f, 0.0f), value) && value == 0.5f);
	CHECK(reader.TraceSky(Vec3(), Vec3(0.0f, 0.0f, 1.0f), value) && visibility->skyDirection.z == 1.0f);
	CHECK(reader.TraceSky(Vec3(), Vec3(), value).code == Code::InvalidInput);
	value = 77.0f; visibility->code = Code::ProviderError;
	CHECK(reader.TraceSegment(Vec3(), Vec3(1.0f, 0.0f, 0.0f), value).code == Code::ProviderError && value == 77.0f);
	visibility->code = Code::Ok; visibility->value = std::numeric_limits<float>::infinity();
	CHECK(reader.TraceSegment(Vec3(), Vec3(1.0f, 0.0f, 0.0f), value).code == Code::ProviderError && value == 77.0f);
	auto cancel = std::make_shared<Cancel>(); Services services; services.cancellation = cancel;
	cancel->requested = true;
	Session before(WithHint(), Options(), services, std::unique_ptr<Algorithm>(new TestAlgorithm));
	CHECK(before.Begin().code == Code::Cancelled && !before.Output());
	cancel->requested = false; visibility->value = 0.5f; visibility->cancelAfter = cancel;
	Session during(d, Options(), services, std::unique_ptr<Algorithm>(new TestAlgorithm(Work::Trace)));
	CHECK(during.Begin()); CHECK(during.Step().code == Code::Cancelled); CHECK(!during.Output());
	services.cancellation.reset(); visibility->cancelAfter.reset(); visibility->code = Code::ProviderError;
	auto log = std::make_shared<Log>(); services.logger = log;
	Session fail(d, Options(), services, std::unique_ptr<Algorithm>(new TestAlgorithm(Work::Trace)));
	CHECK(fail.Begin()); CHECK(fail.Step().code == Code::ProviderError); CHECK(log->calls == 1 && log->last == Code::ProviderError);
}
void BuilderAndSessions()
{
	auto input = WithHint(); auto progress = std::make_shared<ProgressLog>(); Services services; services.progress = progress;
	auto seeds = std::make_shared<std::vector<uint64_t>>(); Options options; options.randomSeed = 42;
	for (int i = 0; i < 50; ++i)
	{
		auto algorithm = std::unique_ptr<TestAlgorithm>(new TestAlgorithm); algorithm->seeds = seeds;
		Session session(input, options, services, std::move(algorithm)); CHECK(!session.Output()); CHECK(session.Begin());
		CHECK(session.Step()); CHECK(session.State() == SessionState::Completed && session.Steps() == 1);
		CHECK(session.Output() && Validate(*session.Output())); CHECK(session.Output()->emitters[0].source.id == 100);
		CHECK(session.Begin().code == Code::InvalidState); CHECK(session.Step().code == Code::InvalidState);
	}
	for (uint64_t seed : *seeds) CHECK(seed == 42);
	CHECK(progress->records.front().state == SessionState::Running && progress->records.back().state == SessionState::Completed);
	AssetBuilder builder(input.manifest); builder.Draft().emitters.push_back(Hint().emitter);
	std::shared_ptr<const ReconstructedLightingAsset> output; CHECK(builder.Finish(output));
	builder.Draft().emitters[0].emission.intensity.value = 999.0f; CHECK(output->emitters[0].emission.intensity.value == 100.0f);
	builder.Draft().version = 999; CHECK(builder.Finish(output).code == Code::InvalidInput && !output);
	Session corrupt(input, options, services, std::unique_ptr<Algorithm>(new TestAlgorithm(Work::CorruptOutput)));
	CHECK(corrupt.Begin()); CHECK(corrupt.Step().code == Code::InvalidInput && !corrupt.Output());
	Session throws(input, options, Services(), std::unique_ptr<Algorithm>(new TestAlgorithm(Work::Throw)));
	CHECK(throws.Begin()); CHECK(throws.Step().code == Code::AlgorithmError && !throws.Output());
	options.maxSteps = 2; Session limited(input, options, Services(), std::unique_ptr<Algorithm>(new TestAlgorithm(Work::Loop)));
	CHECK(limited.Begin()); CHECK(limited.Step()); CHECK(limited.Step()); CHECK(limited.Step().code == Code::WorkLimit && limited.Steps() == 2);
	progress->failCompletion = true; Session callback(input, Options(), services, std::unique_ptr<Algorithm>(new TestAlgorithm));
	CHECK(callback.Begin()); CHECK(callback.Step().code == Code::ProviderError && !callback.Output());
}
} // namespace
int main()
{
	EmptyAndMetadata(); StreamingAndOwnership(); RecordsAndProviderFailures(); VisibilityAndCancellation(); BuilderAndSessions();
	std::printf("Reconstruction core: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
