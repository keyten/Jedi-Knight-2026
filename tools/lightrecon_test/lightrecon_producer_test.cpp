// S13 trusted controlled pipeline tests. No hidden answers enter Producer::Run.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/producer.h"
#include "producer_test_scene.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace JointTest;
namespace P = Rend2LightRecon::Core::Producer;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                             \
	do                                                                                                       \
	{                                                                                                        \
		++checks;                                                                                            \
		if (!(x))                                                                                            \
		{                                                                                                    \
			++failures;                                                                                      \
			std::printf("FAIL %d %s\n", __LINE__, #x);                                                       \
		}                                                                                                    \
	} while (0)
using namespace ProducerTest;
std::vector<std::string> rows;
P::Result Run(Rig &rig, const char *name)
{
	Validation validation(rig);
	Final final(rig);
	P::Result result;
	auto request = Request(rig);
	auto error = P::Run(request, Read(), result, &validation, &final);
	if (!error)
		std::printf("%s producer error: %s\n", name, error.message.c_str());
	CHECK(error);
	CHECK(result.state == P::State::Completed);
	if (result.state != P::State::Completed)
	{
		std::printf("%s: %s %s\n", name, P::StateName(result.state), result.reason.c_str());
		return result;
	}
	if (!result.artifact.asset)
		return result;
	CHECK(P::ValidateArtifact(result.artifact, Read()));
	CHECK(final.calls == 1);
	CHECK(result.final.score.intervalMse < result.final.score.blackMse * .02);
	CHECK(result.artifact.asset->assessment.resolvedSources == 0);
	CHECK(result.artifact.asset->assessment.strictEvaluated);
	CHECK(result.artifact.asset->assessment.readiness.size() == 7);
	Manager manager;
	auto generation = manager.Acquire().Generation();
	CHECK(P::Publish(result.artifact, manager, generation, Read()) == Status::Ok);
	auto pinned = manager.Acquire();
	CHECK(pinned.State() == Availability::Partial);
	UseSettings use;
	use.master = true;
	use.requested.fill(true);
	for (unsigned c = 0; c < unsigned(Consumer::Count); ++c)
		CHECK(!EvaluateConsumer(pinned, use, Consumer(c), {}).effective);
	CHECK(pinned.GetCapability(Capability::ContributionSampling).status == Status::Unavailable);
	auto corrupt = result.artifact;
	corrupt.chunks[0].bytes.back() ^= 1;
	CHECK(!P::ValidateArtifact(corrupt, Read()));
	auto before = manager.Acquire().Generation();
	CHECK(P::Publish(corrupt, manager, before, Read()) == Status::Corrupt);
	CHECK(manager.Acquire().Generation() == before);
	manager.Reset();
	CHECK(P::Publish(result.artifact, manager, generation, Read()) == Status::StaleHandle);
	CHECK(pinned.Asset());
	std::vector<Disk::Chunk> chunks = result.artifact.chunks;
	std::vector<uint8_t> bytes;
	CHECK(Disk::Encode(*result.artifact.asset, result.artifact.fingerprint, chunks, bytes));
	rows.push_back(std::string("{\"name\":\"") + name + "\",\"report\":" + P::ReportJson(result) +
				   ",\"serialized_bytes\":" + std::to_string(bytes.size()) +
				   ",\"checksummed_payloads\":" + std::to_string(chunks.size()) + "}");
	return result;
}
void Basic()
{
	Rig rig;
	auto a = Point(10, V(-.6, 0, .8)), b = Point(20, V(.6, .1, 1));
	rig.request.candidates = {a, b};
	rig.request.profiles[0].model.linearRec709PrimariesAttested = true;
	rig.Observations(rig.Truth({Answer(a, V(2.5, .3, .5)), Answer(b, V(.2, 2, 1))}));
	auto result = Run(rig, "two_colour_producer");
	if (!result.artifact.asset)
		return;
	CHECK(result.artifact.asset->emitters.size() == 2);
	for (const auto &emitter : result.artifact.asset->emitters)
	{
		CHECK(emitter.emission.unit == PhotometricUnit::Relative);
		CHECK(emitter.emission.color.known);
		CHECK(!emitter.source.evidence.physicalEmitterConfidence.known);
	}
	auto artifact = result.artifact;
	artifact.chunks.pop_back();
	CHECK(!P::ValidateArtifact(artifact, Read()));
	auto modified = std::make_shared<ReconstructedLightingAsset>(*result.artifact.asset);
	modified->assessment.resolvedSources = 7;
	artifact = result.artifact;
	artifact.asset = modified;
	CHECK(!P::ValidateArtifact(artifact, Read()));
	Validation validation(rig);
	Final final(rig);
	auto request = Request(rig);
	P::Result repeated;
	Must(P::Run(request, Read(), repeated, &validation, &final));
	std::vector<uint8_t> first, second;
	CHECK(Disk::Encode(*result.artifact.asset, result.artifact.fingerprint, result.artifact.chunks, first));
	CHECK(Disk::Encode(*repeated.artifact.asset, repeated.artifact.fingerprint, repeated.artifact.chunks,
					   second));
	CHECK(first == second);
}
void Transport()
{
	Rig rig;
	auto a = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {a};
	rig.request.materials[0] = KnownMaterial(1, V(.7, .1, .05));
	rig.request.profiles[0].contract.physicalTransportAssumed = true;
	rig.Observations(rig.Truth({Answer(a, V(2, 2, 2))}));
	auto out = Run(rig, "source_linked_coloured_bounce");
	if (out.artifact.asset)
	{
		CHECK(!out.artifact.asset->transport.empty());
		for (const auto &f : out.artifact.asset->transport)
		{
			CHECK(f.membership.reflectionOrderKnown);
			CHECK(f.membership.directLocal == FieldMembership::Excluded);
			CHECK(f.source.evidence.approximateIrradiance != Suitability::Candidate);
		}
	}
	rig.request.materials[0].knowledge = I::MaterialKnowledge::Bounded;
	rig.request.materials[0].lower = Vec3(.2f,.05f,.01f);
	rig.request.materials[0].upper = Vec3(.9f,.4f,.2f);
	auto conditional = Run(rig, "conditional_reflectance_publication");
	if (conditional.fitted.preferred != SIZE_MAX)
	{
		I::Result frozen;
		J::Work work;
		CHECK(J::DecomposeFrozen(rig.request, conditional.fitted.alternatives[conditional.fitted.preferred], Read(), frozen, work));
		CHECK(frozen.materials[0].knowledge == I::MaterialKnowledge::Bounded);
		for (const auto &value : frozen.values) CHECK(!value.upperKnown);
	}
	Rig ambient;
	I::Root fill;
	fill.source = 90;
	fill.kind = TransportRootKind::LegacyAmbient;
	fill.domain = I::Root::Domain::SourceLinearRelative;
	fill.generation = Key("Known artist ambient control");
	fill.provenance = "Controlled known ambient, not GI";
	fill.incident.assign(ambient.scene.patches.size(), Vec3(.3, .2, .1));
	fill.available.assign(ambient.scene.patches.size(), 1);
	ambient.request.boundaryRoots = {fill};
	ambient.Observations(ambient.Truth({}));
	auto outAmbient = Run(ambient, "ambient_without_fake_emitters");
	if (outAmbient.artifact.asset)
		CHECK(outAmbient.artifact.asset->emitters.empty());
}
void GeometryAndShapes()
{
	Rig rig;
	auto truth = Point(10, V(.1, 0, .9)), start = truth;
	start.shape.initial.geometry.position.world = Vec3(.4, 0, .9);
	start.shape.fitPosition = true;
	rig.request.candidates = {start};
	rig.request.options.geometry = true;
	rig.request.options.geometrySweeps = 2;
	rig.request.options.amplitudeSweeps = 3;
	rig.request.options.positionStepWorld = .2;
	rig.Observations(rig.Truth({Answer(truth, V(2, 1, .5))}));
	auto positioned = Run(rig, "point_geometry_refinement");
	if (positioned.artifact.asset)
	{
		double best = 10;
		for (const auto &e : positioned.artifact.asset->emitters)
			best =
				std::min(best, Length(V(e.position.world) - V(truth.shape.initial.geometry.position.world)));
		CHECK(best < .2);
		rows.push_back(
			"{\"name\":\"independent_position_metric\",\"initial_world_error\":0.3,\"final_world_error\":" +
			std::to_string(best) + "}");
	}
	for (auto kind : {EmitterKind::Spot, EmitterKind::Rectangle, EmitterKind::Tube})
	{
		Rig shape;
		auto extended = Extended(10, kind, V(0, 0, 1.15)), point = Point(20, V(0, 0, 1.15));
		extended.family = point.family = 1;
		shape.request.candidates = {extended, point};
		shape.Observations(shape.Truth({Answer(extended, V(4.2, 2.1, .5))}));
		auto result = Run(shape, ("shape_" + std::to_string(unsigned(kind))).c_str());
		if (result.artifact.asset)
		{
			bool found = false;
			for (const auto &e : result.artifact.asset->emitters)
				found |= e.kind == kind;
			CHECK(found);
		}
	}
}
void SunSky()
{
	Rig rig;
	Bake::Recipe recipe;
	recipe.fast = false;
	rig.scene.Quad(V(-4, -4, 3), V(4, -4, 3), V(4, 4, 3), V(-4, 4, 3), MaterialSky);
	Rebuild(rig.scene);
	Historical(rig, recipe);
	auto sun = CompilerPoint(20, V());
	sun.root = TransportRootKind::Sun;
	sun.environment.id = sun.id;
	sun.environment.kind = Bake::SourceKind::Directional;
	sun.environment.orientation = FrameDown();
	sun.shape.upperRgb = Vec3(300, 300, 300);
	rig.request.candidates = {sun};
	rig.Observations(rig.Truth({Answer(sun, V(30, 35, 40))}));
	auto result = Run(rig, "directional_sun_publication");
	if (result.artifact.asset)
	{
		CHECK(result.artifact.asset->suns.size() == 1);
		CHECK(result.artifact.asset->emitters[0].kind == EmitterKind::Directional);
	}
	Rig sky;
	sky.scene.Quad(V(-64, -64, 3), V(64, -64, 3), V(64, 64, 3), V(-64, 64, 3), MaterialSky);
	Rebuild(sky.scene);
	Historical(sky, recipe);
	auto environment = CompilerPoint(30, V());
	environment.root = TransportRootKind::Sky;
	environment.environment.id = environment.id;
	environment.environment.kind = Bake::SourceKind::Sky;
	environment.shape.upperRgb = Vec3(300, 300, 300);
	sky.request.candidates = {environment};
	sky.Observations(sky.Truth({Answer(environment, V(30, 35, 40))}));
	auto skyResult = Run(sky, "diffuse_sky_publication");
	if (skyResult.artifact.asset)
	{
		CHECK(skyResult.artifact.asset->environments.size() == 1);
		CHECK(skyResult.artifact.asset->suns.empty());
		CHECK(skyResult.artifact.asset->environments[0].sunCoverage == SunCoverage::ExcludesExplicitSun);
	}
}
void Negative()
{
	Rig rig;
	auto a = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {a};
	rig.Observations(rig.Truth({Answer(a, V(2, 1, .5))}));
	Validation validation(rig);
	Final final(rig);
	auto r = Request(rig);
	P::Result out;
	auto mismatched = r;
	mismatched.map.sha256[0] ^= 1;
	CHECK(!P::Run(mismatched, Read(), out, &validation, &final));
	CHECK(!out.artifact.asset);
	mismatched = r;
	mismatched.heldoutKey[0] ^= 1;
	CHECK(!P::Run(mismatched, Read(), out, &validation, &final));
	CHECK(!out.artifact.asset);
	auto unknown = r;
	unknown.fitting.profiles[0].model.law = S::Law::HistoricalBake;
	unknown.fitting.profiles[0].model.profile = {};
	unknown.coverage.sourceTransferKnown = unknown.coverage.primariesKnown = false;
	Must(P::Run(unknown, Read(), out));
	CHECK(out.state == P::State::DiagnosticOnly);
	CHECK(out.artifact.asset->emitters.empty());
	CHECK(!out.artifact.asset->assessment.strictEvaluated);
	rows.push_back(std::string("{\"name\":\"unknown_profile_diagnostic\",\"report\":") + P::ReportJson(out) +
				   "}");
	CHECK(!P::Run(r, Read(), out, nullptr, &final));
	CHECK(!out.artifact.asset);
	final.fail = true;
	CHECK(!P::Run(r, Read(), out, &validation, &final));
	CHECK(!out.artifact.asset);
	final.fail = false;
	auto budget = r;
	budget.fitting.options.maxEvaluations = 1;
	CHECK(!P::Run(budget, Read(), out, &validation, &final));
	CHECK(out.state == P::State::Budget);
	CHECK(!out.artifact.asset);
	budget = r;
	budget.options.uncertainty.maxAttempts = 1;
	CHECK(!P::Run(budget, Read(), out, &validation, &final));
	CHECK(!out.artifact.asset);
	struct Cancel final : Cancellation
	{
		bool IsCancelled() const noexcept override { return true; }
	} cancel;
	auto read = Read();
	read.cancellation = &cancel;
	CHECK(P::Run(r, read, out, &validation, &final).code == Code::Cancelled);
	CHECK(!out.artifact.asset);
	auto invalid = r;
	invalid.fitting.candidates[0].shape.initial.rgbAmplitude.x = -1;
	auto error = P::Run(invalid, Read(), out, &validation, &final);
	CHECK(!error || out.state == P::State::DiagnosticOnly);
	if (out.artifact.asset)
		CHECK(out.artifact.asset->assessment.resolvedSources == 0);
}
void Cache(const std::string &root)
{
	Rig rig;
	auto a = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {a};
	rig.Observations(rig.Truth({Answer(a, V(2, 1, .5))}));
	auto r = Request(rig);
	Validation validation(rig);
	Final final(rig);
	auto cache = std::make_shared<Disk::Cache>(root);
	P::Backend backend(r, Read(), &validation, &final, cache);
	Manager manager;
	auto disabled = ApplyBuildPolicy(BuildPolicy::Disabled, manager, &backend);
	CHECK(!disabled.buildCalled && !disabled.cacheCalled);
	auto missingRequest = r;
	missingRequest.fitting.options.seed = 0x713ac;
	P::Backend missingBackend(missingRequest, Read(), &validation, &final,
							  std::make_shared<Disk::Cache>(root + "/only-missing"));
	auto missing = ApplyBuildPolicy(BuildPolicy::CacheOnly, manager, &missingBackend);
	CHECK(!missing.buildCalled && missing.status == Status::Missing);
	auto built = ApplyBuildPolicy(BuildPolicy::ForceRebuild, manager, &backend);
	CHECK(built.status == Status::Ok);
	CHECK(backend.cacheWrite == Disk::Code::Ok);
	auto previous = manager.Acquire();
	unsigned calls = validation.calls;
	auto loaded = ApplyBuildPolicy(BuildPolicy::CacheOnly, manager, &backend);
	CHECK(loaded.status == Status::Ok && !loaded.buildCalled);
	CHECK(validation.calls == calls);
	CHECK(previous.Asset());
	for (unsigned i = 0; i < 40; ++i)
	{
		CHECK(P::Store(backend.last.artifact, *cache, Read()));
		P::Artifact reread;
		CHECK(P::Load(*cache, backend.last.artifact.fingerprint, Read(), reread));
	}
	const auto file = root + "/" + Disk::Hex(backend.last.artifact.key) + ".jklr";
	uint64_t payloadOffset = 0;
	{
		ReconstructedLightingAsset meta;
		Disk::Index index;
		std::unique_ptr<Disk::FileReader> reader;
		CHECK(cache->Load(backend.last.artifact.fingerprint, meta, index, reader));
		for (const auto &entry : index.sections)
			if (entry.type == unsigned(Disk::Section::Chunk) && entry.bytes)
			{
				payloadOffset = entry.offset;
				break;
			}
		CHECK(payloadOffset != 0);
	}
	{
		std::fstream corrupt(file, std::ios::binary | std::ios::in | std::ios::out);
		corrupt.seekg(std::streamoff(payloadOffset));
		char byte = 0;
		corrupt.read(&byte, 1);
		byte ^= 1;
		corrupt.seekp(std::streamoff(payloadOffset));
		corrupt.write(&byte, 1);
	}
	P::Artifact corrupted;
	CHECK(!P::Load(*cache, backend.last.artifact.fingerprint, Read(), corrupted));
	CHECK(!corrupted.asset);
	CHECK(P::Store(backend.last.artifact, *cache, Read()));
	auto changed = r;
	changed.fitting.options.seed += 1;
	P::Backend different(changed, Read(), &validation, &final, cache);
	auto stale = ApplyBuildPolicy(BuildPolicy::CacheOnly, manager, &different);
	CHECK(stale.status != Status::Ok && !stale.buildCalled);
	changed = r;
	changed.fitting.candidates[0].shape.initial.geometry.position.world.x += .01f;
	P::Backend moved(changed, Read(), &validation, &final, cache);
	auto movedCache = ApplyBuildPolicy(BuildPolicy::CacheOnly, manager, &moved);
	CHECK(movedCache.status == Status::Missing && !movedCache.buildCalled);
	changed = r;
	changed.validationKey[0] ^= 1;
	P::Backend repartitioned(changed, Read(), &validation, &final, cache);
	auto wrongFold = ApplyBuildPolicy(BuildPolicy::CacheOnly, manager, &repartitioned);
	CHECK(wrongFold.status == Status::Missing && !wrongFold.buildCalled);
	{
		std::ofstream block(root + "/blocked-root");
		block << "This is a file, not a directory";
	}
	auto badCache = std::make_shared<Disk::Cache>(root + "/blocked-root/child");
	P::Backend failedWrite(r, Read(), &validation, &final, badCache);
	auto stillPublished = ApplyBuildPolicy(BuildPolicy::ForceRebuild, manager, &failedWrite);
	CHECK(stillPublished.status == Status::Ok);
	CHECK(failedWrite.last.artifact.asset);
	CHECK(failedWrite.cacheWrite != Disk::Code::Ok);
	rows.push_back("{\"name\":\"offline_cache_lifecycle\",\"warm_hit_without_fit\":true,\"production_mapload_"
				   "cache\":false}");
}
int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		Basic();
		Transport();
		GeometryAndShapes();
		SunSky();
		Negative();
		if (argc > 1)
			Cache(argv[1]);
		if (argc > 2)
		{
			std::ofstream out(argv[2]);
			out << "{\"schema\":\"org.openjk.producer-tests.v1\",\"checks\":" << checks
				<< ",\"failures\":" << failures << ",\"cases\":[";
			for (size_t i = 0; i < rows.size(); ++i)
				out << (i ? "," : "") << rows[i];
			out << "]}\n";
		}
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("S13 exception %s\n", e.what());
	}
	std::printf("S13 %d checks %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
