// S13 trusted controlled pipeline tests. No hidden answers enter Producer::Run.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/producer.h"
#include "producer_test_scene.h"
#include "lightrecon/persistent.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace JointTest;
namespace P = Rend2LightRecon::Core::Producer;
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
	CHECK(pinned.GetCapability(Capability::ContributionSampling).status ==
		  (result.artifact.asset->contributions.empty() ? Status::Unavailable : Status::Ok));
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
	CHECK(Disk::Encode(*result.artifact.asset, result.artifact.fingerprint, result.artifact.chunks,
					   first));
	CHECK(Disk::Encode(*repeated.artifact.asset, repeated.artifact.fingerprint,
					   repeated.artifact.chunks, second));
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
	rig.request.materials[0].lower = Vec3(.2f, .05f, .01f);
	rig.request.materials[0].upper = Vec3(.9f, .4f, .2f);
	auto conditional = Run(rig, "conditional_reflectance_publication");
	if (conditional.fitted.preferred != SIZE_MAX)
	{
		I::Result frozen;
		J::Work work;
		CHECK(J::DecomposeFrozen(rig.request,
								 conditional.fitted.alternatives[conditional.fitted.preferred],
								 Read(), frozen, work));
		CHECK(frozen.materials[0].knowledge == I::MaterialKnowledge::Bounded);
		for (const auto &value : frozen.values)
			CHECK(!value.upperKnown);
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
			best = std::min(
				best, Length(V(e.position.world) - V(truth.shape.initial.geometry.position.world)));
		CHECK(best < .2);
		rows.push_back("{\"name\":\"independent_position_metric\",\"initial_world_error\":0.3,"
					   "\"final_world_error\":" +
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
std::shared_ptr<Hidden::Request> HiddenRequest(const Rig &, bool);
std::shared_ptr<Solar::Request> SolarRequest(const Rig &, bool);
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
	Run(rig,
		"directional_sun_publication"); // retain the original full publication/cache assertions
	auto request = Request(rig);
	request.hidden = HiddenRequest(rig, false);
	request.solar = SolarRequest(rig, false);
	Validation validation(rig);
	Final final(rig);
	P::Result result;
	CHECK(P::Run(request, Read(), result, &validation, &final));
	CHECK(result.state == P::State::Completed);
	CHECK(result.fitted.work.attemptedModels > 0);
	rows.push_back("{\"name\":\"solar_candidates_no_hidden_proposals\",\"report\":" +
				   P::ReportJson(result) + "}");
	if (result.artifact.asset)
	{
		CHECK(result.artifact.asset->suns.size() == 1);
		CHECK(result.artifact.asset->emitters[0].kind == EmitterKind::Directional);
	}
	auto local = CompilerPoint(25, V(-30, 0, 50));
	rig.request.candidates = {sun, local};
	rig.Observations(rig.Truth({Answer(sun, V(30, 35, 40)), Answer(local, V(60, 20, 15))}));
	request = Request(rig);
	request.hidden = HiddenRequest(rig, false);
	request.solar = SolarRequest(rig, false);
	CHECK(P::Run(request, Read(), result, &validation, &final));
	CHECK(result.state == P::State::Completed);
	if (result.artifact.asset)
	{
		CHECK(result.artifact.asset->suns.size() == 1);
		CHECK(result.artifact.asset->emitters.size() == 2);
	}
	rows.push_back("{\"name\":\"local_lamp_supported_directional_prior\",\"report\":" +
				   P::ReportJson(result) + "}");

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
	Run(sky, "diffuse_sky_publication");
	auto skyRequest = Request(sky);
	skyRequest.hidden = HiddenRequest(sky, false);
	Validation skyValidation(sky);
	Final skyFinal(sky);
	P::Result skyResult;
	CHECK(P::Run(skyRequest, Read(), skyResult, &skyValidation, &skyFinal));
	CHECK(skyResult.state == P::State::Completed);
	CHECK(skyResult.fitted.work.attemptedModels > 0);
	rows.push_back(
		"{\"name\":\"positive_sky_no_local_lamp\",\"report\":" + P::ReportJson(skyResult) + "}");
	if (skyResult.artifact.asset)
	{
		CHECK(skyResult.artifact.asset->environments.size() == 1);
		CHECK(skyResult.artifact.asset->suns.empty());
		CHECK(skyResult.artifact.asset->environments[0].sunCoverage ==
			  SunCoverage::ExcludesExplicitSun);
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
	{
		auto failingProvider = r;
		auto hidden = std::make_shared<Hidden::Request>();
		hidden->map = r.map;
		hidden->geometry = r.fitting.geometry;
		// Deliberately missing analysis bounds. The failure must retain the
		// provider report rather than displaying a false complete/zero-work search.
		failingProvider.hidden = hidden;
		CHECK(!P::Run(failingProvider, Read(), out, &validation, &final));
		CHECK(!out.artifact.asset && !out.work.hidden.complete);
		CHECK(!out.work.hidden.diagnostics.empty());
	}
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
	rows.push_back(std::string("{\"name\":\"unknown_profile_diagnostic\",\"report\":") +
				   P::ReportJson(out) + "}");
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
		bool IsCancelled() const noexcept override
		{
			return true;
		}
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

const ProviderAssessment *Provider(const P::Result &r, const std::string &name)
{
	for (const auto &p : r.work.providers)
		if (p.name == name)
			return &p;
	return nullptr;
}
std::shared_ptr<Hidden::Request> HiddenRequest(const Rig &rig, bool positive = false)
{
	auto h = std::make_shared<Hidden::Request>();
	h->map = rig.scene.map;
	h->units = rig.request.profiles[0].model.units;
	h->geometry = rig.request.geometry;
	h->domain = rig.scene.domain;
	h->domainKey = Key("owned provider classifier");
	h->analysisBounds = Box(V(-90, -85, 1), V(90, 85, 100));
	h->profiles = {rig.request.profiles[0].model.law == S::Law::HistoricalBake
					   ? rig.request.profiles[0].model.profile
					   : Bake::NetRadiantProfile()};
	h->options.allowAssumedProfiles = h->options.allowAssumedEncoding = true;
	h->options.spots = false;
	h->options.coarseDivisions = 2;
	h->options.refinementRounds = 0;
	h->options.maxNodesPerArea = 8;
	if (positive)
	{
		auto samples = std::make_shared<std::vector<Hidden::Sample>>();
		for (const auto &o : *rig.train->data)
		{
			Hidden::Sample a;
			a.receiver = o.receiver;
			a.areaWorldSquared = o.measure;
			a.eligible = a.encodingAttested = true;
			a.receiver.domain = Bake::Domain::EncodedRgb8BeforeQuantization;
			for (unsigned k = 0; k < 3; ++k)
				a.rgb[k] = uint8_t(std::min(254., o.rgb[k].lower));
			samples->push_back(a);
		}
		h->training = Lease(samples);
	}
	return h;
}
std::shared_ptr<Solar::Request> SolarRequest(const Rig &rig, bool positive = false)
{
	auto q = std::make_shared<Solar::Request>();
	q->mapKey = rig.scene.map.sha256;
	q->lineage = "owned provider absence test";
	q->geometry = rig.request.geometry;
	q->units = rig.request.profiles[0].model.units;
	q->profiles = {Bake::NetRadiantProfile()};
	q->options.allowAssumedProfiles = q->options.allowAssumedObservationEncoding = true;
	q->options.sphereDirections = 16;
	q->options.refineSeeds = q->options.refineLevels = 0;
	q->options.minSamples = 6;
	q->options.minBlocks = 2;
	q->options.blockWorld = 4;
	q->options.minimumNormalEigenRatio = .001;
	if (positive)
	{
		auto samples = std::make_shared<std::vector<Solar::Sample>>();
		for (const auto &o : *rig.train->data)
		{
			Solar::Sample a;
			a.receiver = o.receiver;
			a.receiver.domain = Bake::Domain::EncodedRgb8BeforeQuantization;
			a.areaWorldSquared = o.measure;
			a.eligible = a.encodingVerifiedForProfile = true;
			for (auto &v : a.rgb)
				v = 80;
			samples->push_back(a);
		}
		q->training = Lease(samples);
	}
	return q;
}
void ProviderIndependence()
{
	Rig rig;
	auto lamp = Point(10, V(-.6, 0, .8), true);
	lamp.mayRemove = false;
	rig.request.candidates = {lamp};
	rig.Observations(rig.Truth({Answer(lamp, V(2, 1, .5))}));
	Validation val(rig);
	Final final(rig);
	auto r = Request(rig);
	r.solar = SolarRequest(rig);
	P::Result out;
	auto e = P::Run(r, Read(), out, &val, &final);
	CHECK(e);
	CHECK(out.state == P::State::Completed);
	CHECK(out.fitted.work.attemptedModels > 0);
	CHECK(Provider(out, "solar.fit") &&
		  Provider(out, "solar.fit")->outcome == ProviderOutcome::NoSupportedEvidence);
	CHECK(out.artifact.asset && out.artifact.asset->suns.empty() &&
		  out.artifact.asset->environments.empty());
	if (out.artifact.asset)
	{
		CHECK(out.artifact.asset->assessment.sunEvidence == AssessmentStatus::Unknown);
		CHECK(out.artifact.asset->assessment.providers.size() == out.work.providers.size());
		CHECK(out.artifact.asset->assessment.version == 2);
		CHECK(!out.artifact.asset->assessment.admissions.empty());
		auto malformed = *out.artifact.asset;
		malformed.assessment.providers.front().exactLightingAdmissible = true;
		CHECK(!Validate(malformed));
		malformed = *out.artifact.asset;
		malformed.assessment.sunEvidence = AssessmentStatus::Validated;
		CHECK(!Validate(malformed));
	}
	rows.push_back("{\"name\":\"local_lamp_unknown_sun\",\"report\":" + P::ReportJson(out) + "}");
	auto previous = out.artifact.asset;
	auto required = r;
	required.providerPolicy.solarRequired = true;
	CHECK(!P::Run(required, Read(), out, &val, &final));
	CHECK(!out.artifact.asset);
	r.hidden = HiddenRequest(rig);
	e = P::Run(r, Read(), out, &val, &final);
	CHECK(e);
	CHECK(out.fitted.work.attemptedModels > 0);
	CHECK(Provider(out, "hidden") &&
		  Provider(out, "hidden")->outcome == ProviderOutcome::NoSupportedEvidence);
	CHECK(out.artifact.asset && out.artifact.asset->emitters.size() == 1);
	// Explicit local search budget must not cancel an independent supplied lamp.
	auto budget = std::make_shared<Hidden::Request>(*r.hidden);
	Hidden::SearchArea area;
	area.id = 1;
	area.reason = "owned local shard";
	area.positions = area.receivers = budget->analysisBounds;
	budget->areas = {area, area};
	budget->areas[1].id = 2;
	budget->options.maxAreas = 1;
	r.hidden = budget;
	e = P::Run(r, Read(), out, &val, &final);
	CHECK(e);
	CHECK(out.fitted.work.attemptedModels > 0);
	CHECK(Provider(out, "hidden") &&
		  Provider(out, "hidden")->outcome == ProviderOutcome::BudgetExceeded &&
		  Provider(out, "hidden")->budgetScope == ProviderBudgetScope::Local &&
		  !Provider(out, "hidden")->searchComplete);
	rows.push_back("{\"name\":\"optional_local_provider_budget\",\"report\":" + P::ReportJson(out) +
				   "}");
	auto strict = r;
	strict.providerPolicy.hiddenRequired = true;
	CHECK(!P::Run(strict, Read(), out, &val, &final));
	CHECK(out.state == P::State::Budget);
	strict = r;
	strict.providerPolicy.allowOptionalLocalBudget = false;
	CHECK(!P::Run(strict, Read(), out, &val, &final));
	// Global read budget is never softened into an optional local budget.
	auto tiny = Read();
	tiny.maxBytes = 128;
	CHECK(!P::Run(r, tiny, out, &val, &final));
	CHECK(!out.artifact.asset);
	r.hidden = HiddenRequest(rig);
	auto corrupt = std::make_shared<Hidden::Request>(*r.hidden);
	corrupt->profiles[0].schemaVersion = 999;
	r.hidden = corrupt;
	CHECK(!P::Run(r, Read(), out, &val, &final));
	CHECK(!out.artifact.asset && out.fitted.state == J::State::NotStarted);
	CHECK(out.work.hidden.outcome == ProviderOutcome::Corrupt);
	r.hidden = HiddenRequest(rig);
	auto stale = std::make_shared<Hidden::Request>(*r.hidden);
	stale->geometry.geometryKey[0] ^= 1;
	r.hidden = stale;
	CHECK(!P::Run(r, Read(), out, &val, &final));
	CHECK(!out.artifact.asset);
	struct LaterCancel final : Cancellation
	{
		mutable unsigned calls = 0;
		bool IsCancelled() const noexcept override
		{
			return ++calls >= 4;
		}
	} cancel;
	r.hidden = HiddenRequest(rig);
	auto read = Read();
	read.cancellation = &cancel;
	CHECK(P::Run(r, read, out, &val, &final).code == Code::Cancelled);
	CHECK(!out.artifact.asset);
	// A provider returning malformed validation data is a fatal operational error.
	auto corruptSolar = std::make_shared<Solar::Request>(*SolarRequest(rig));
	corruptSolar->profiles[0].formulaVersion = 999;
	r.solar = corruptSolar;
	CHECK(!P::Run(r, Read(), out, &val, &final));
	CHECK(!out.artifact.asset);
	CHECK(previous &&
		  previous->assessment.providers.size() > 0); // immutable lease survives later failures
}
void ProviderScopes()
{
	Rig rig;
	Bake::Recipe recipe;
	recipe.fast = false;
	rig.scene.Quad(V(-64, -64, 3), V(64, -64, 3), V(64, 64, 3), V(-64, 64, 3), MaterialSky);
	Rebuild(rig.scene);
	Historical(rig, recipe);
	auto lamp = CompilerPoint(10, V(-30, 0, 50), true);
	lamp.mayRemove = false;
	rig.request.candidates = {lamp};
	rig.Observations(rig.Truth({Answer(lamp, V(120, 50, 30))}));
	auto r = Request(rig);
	r.hidden = HiddenRequest(rig, true);
	r.solar = SolarRequest(rig);
	Validation val(rig);
	Final final(rig);
	P::Result out;
	auto e = P::Run(r, Read(), out, &val, &final);
	CHECK(e);
	CHECK(out.fitted.work.attemptedModels > 0);
	rows.push_back("{\"name\":\"hidden_search_without_sun\",\"report\":" + P::ReportJson(out) +
				   "}");
	// Real positive Solar::Fit with independently unavailable validation: it must
	// leave local candidates usable, never promote unvalidated directional roots.
	struct NoValidation final : P::SolarValidation
	{
		mutable unsigned calls = 0;
		Error Assess(const Solar::FitResult &, const ReadContext &,
					 Solar::Result &out) const override
		{
			++calls;
			out.outcome = ProviderOutcome::NoSupportedEvidence;
			out.state = Solar::State::UnknownSun;
			return Failure(Code::Unavailable, "owned independent validation lacks solar evidence");
		}
	} unavailable;
	r.hidden = HiddenRequest(rig, true);
	r.solar = SolarRequest(rig, true);
	r.solarValidation = &unavailable;
	e = P::Run(r, Read(), out, &val, &final);
	CHECK(e);
	CHECK(out.fitted.work.attemptedModels > 0);
	CHECK(unavailable.calls > 0);
	CHECK(Provider(out, "solar.validation") &&
		  Provider(out, "solar.validation")->outcome == ProviderOutcome::NoSupportedEvidence);
	CHECK(out.artifact.asset && out.artifact.asset->suns.empty());
	CHECK(Provider(out, "hidden") && Provider(out, "hidden")->evidenceAvailable);
	rows.push_back("{\"name\":\"hidden_with_solar_validation_unavailable\",\"report\":" +
				   P::ReportJson(out) + "}");
	struct CorruptValidation final : P::SolarValidation
	{
		mutable unsigned calls = 0;
		Error Assess(const Solar::FitResult &, const ReadContext &, Solar::Result &) const override
		{
			++calls;
			return Failure(Code::ProviderError, "owned corrupt validation provider");
		}
	} corruptValidation;
	r.solarValidation = &corruptValidation;
	r.providerPolicy.solarRequired = true;
	e = P::Run(r, Read(), out, &val, &final);
	CHECK(e.code == Code::ProviderError);
	CHECK(corruptValidation.calls > 0);
	CHECK(!out.artifact.asset);
	r.solarValidation = &unavailable;
	r.providerPolicy.solarRequired = false;
	// Explicitly supplied environment alternatives use the same provider API as
	// Solar::FromSolar, with independent scenario tags and immutable leases.
	auto make = [&](bool sky, const char *scenario) {
		Candidates::Provider p;
		p.name = sky ? "environment-sky" : "directional-sun";
		p.map = r.map.sha256;
		p.geometryKey = r.fitting.geometry.geometryKey;
		p.generation = Key(scenario);
		auto evidence = std::make_shared<std::vector<EmitterEvidence>>(1);
		auto &e = (*evidence)[0];
		e.id = sky ? 200 : 300;
		e.origin = EvidenceOrigin::SunShader;
		e.source = "owned independent environment hypothesis";
		e.orientationKnown = true;
		e.toSource = Vec3(0, 0, 1);
		e.shapes = sky ? CandidateSky : CandidateSun;
		e.staticEligible = true;
		auto candidates = std::make_shared<std::vector<EmitterCandidate>>(1);
		auto &c = (*candidates)[0];
		c.id = e.id;
		c.shapes = e.shapes;
		c.evidence = {0};
		c.staticEligible = true;
		auto annotations = std::make_shared<std::vector<Candidates::Annotation>>(1);
		auto &a = (*annotations)[0];
		a.candidate = c.id;
		a.scenario = Key(scenario);
		a.profile = r.fitting.profiles[0].model.profile.id;
		a.tier = Candidates::Tier::Geometry;
		a.nature = Candidates::Nature::Environment;
		p.evidence = Lease(evidence);
		p.candidates = Lease(candidates);
		p.annotations = Lease(annotations);
		return p;
	};
	r.solar.reset();
	r.solarValidation = nullptr;
	r.providers = {make(false, "exclusive-sun"), make(true, "exclusive-sky")};
	// A positive hidden-only profile legitimately adds a third, no-promoted-sun
	// alternative. Check that cheaply without fitting every candidate combination.
	P::Result withHidden;
	CHECK(P::PrepareModelRequests(r, Read(), withHidden));
	CHECK(withHidden.scopedRequests.size() == 3);
	for (const auto &scope : withHidden.scopedRequests)
	{
		size_t roots = 0;
		for (const auto &c : scope->fitting.candidates)
			roots += c.root == TransportRootKind::Sun || c.root == TransportRootKind::Sky;
		CHECK(roots <= 1);
	}
	// This separate fixture specifically tests solar candidates with NO hidden
	// proposals, so its two directional/environment scenarios are the only scopes.
	r.hidden = HiddenRequest(rig, false);
	P::Result plan;
	CHECK(P::PrepareModelRequests(r, Read(), plan));
	CHECK(plan.scopedRequests.size() == 2);
	CHECK(P::Run(r, Read(), out, &val, &final));
	CHECK(out.state == P::State::DiagnosticOnly && out.fitted.state == J::State::NotStarted);
	CHECK(out.scopedRequests.size() == 2);
	for (const auto &scope : plan.scopedRequests)
	{
		size_t sun = 0, sky = 0;
		for (const auto &c : scope->fitting.candidates)
		{
			sun += c.root == TransportRootKind::Sun;
			sky += c.root == TransportRootKind::Sky;
		}
		CHECK(sun + sky == 1);
		CHECK(scope->fitting.candidates.size() >= 2);
		P::Result scopedResult;
		CHECK(P::Run(*scope, Read(), scopedResult, &val, &final));
		CHECK(scopedResult.fitted.work.attemptedModels > 0);
		CHECK(scopedResult.scenarios.size() == 1 && scopedResult.scenarios.front().selected);
		rows.push_back("{\"name\":\"independent_solar_scope\",\"report\":" +
					   P::ReportJson(scopedResult) + "}");
		auto changed = *scope;
		changed.fitting.geometry.geometryKey[0] ^= 1;
		CHECK(!P::Run(changed, Read(), out, &val, &final));
	}
	auto reverse = r;
	std::reverse(reverse.providers.begin(), reverse.providers.end());
	P::Result again;
	CHECK(P::PrepareModelRequests(reverse, Read(), again));
	CHECK(again.scenarios.size() == plan.scenarios.size());
	if (again.scenarios.size() == plan.scenarios.size())
		for (size_t i = 0; i < again.scenarios.size(); ++i)
			CHECK(again.scenarios[i].key == plan.scenarios[i].key);
	rows.push_back("{\"name\":\"exclusive_solar_scopes\",\"report\":" + P::ReportJson(plan) + "}");
	// An automatically narrowed profile list cannot reuse callbacks bound to a
	// different index layout. Even one scope must be explicitly rebound.
	auto multi = r;
	auto second = multi.fitting.profiles.front();
	Bake::Recipe alt;
	alt.fast = false;
	alt.falloff = Bake::Falloff::Linear;
	second.model.profile = Bake::NetRadiantProfile(alt);
	second.contract.profile = second.model.profile;
	multi.fitting.profiles.push_back(second);
	multi.providers = {r.providers.front()};
	CHECK(P::Run(multi, Read(), out, &val, &final));
	CHECK(out.state == P::State::DiagnosticOnly && out.fitted.state == J::State::NotStarted);
	CHECK(out.scopedRequests.size() == 1);
	auto bounded = r;
	bounded.providerPolicy.maxScopedRequests = 1;
	CHECK(!P::PrepareModelRequests(bounded, Read(), out));
	CHECK(out.state == P::State::Budget && !out.artifact.asset);
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
	auto cacheStart = std::chrono::steady_clock::now();
	auto built = ApplyBuildPolicy(BuildPolicy::ForceRebuild, manager, &backend);
	double coldSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-cacheStart).count();
	CHECK(built.status == Status::Ok);
	CHECK(backend.cacheWrite == Disk::Code::Ok);
	auto previous = manager.Acquire();
	unsigned calls = validation.calls;
	cacheStart = std::chrono::steady_clock::now();
	auto loaded = ApplyBuildPolicy(BuildPolicy::CacheOnly, manager, &backend);
	double warmSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-cacheStart).count();
	CHECK(loaded.status == Status::Ok && !loaded.buildCalled);
	CHECK(validation.calls == calls);
	CHECK(previous.Asset());
	Disk::Persistent::Selection selection;
	selection.contributions = true;
	Disk::Persistent::Work cacheWork;
	BuildProduct preparedContributions;
	CHECK(Disk::Persistent::Load(*cache, backend.last.artifact.fingerprint, selection,
		preparedContributions, cacheWork));
	CHECK(preparedContributions.evaluator && preparedContributions.evaluator->Supports(Capability::ContributionSampling)==Status::Ok);
	std::printf("V01_PRODUCER_CACHE cold_seconds=%.9g warm_seconds=%.9g payload_bytes=%llu file_bytes=%llu warm_validation_calls=%u warm_solver_steps=%llu\n",
		coldSeconds,warmSeconds,static_cast<unsigned long long>(cacheWork.payloadBytes),
		static_cast<unsigned long long>(cacheWork.fileBytes),validation.calls-calls,
		static_cast<unsigned long long>(backend.last.work.solver.forwardCalls));
	CHECK(backend.last.work.providers.size() ==
		  backend.last.artifact.asset->assessment.providers.size());
	auto policyChanged = r;
	policyChanged.providerPolicy.allowOptionalLocalBudget = false;
	P::Backend policyBackend(policyChanged, Read(), &validation, &final, cache);
	CHECK(ApplyBuildPolicy(BuildPolicy::CacheOnly, manager, &policyBackend).status ==
		  Status::Missing);
	for (unsigned i = 0; i < 40; ++i)
	{
		auto stored=P::Store(backend.last.artifact,*cache,Read());
		if(!stored)std::printf("cache repeated-store iteration=%u code=%u reason=%s\n",i,unsigned(stored.code),stored.message.c_str());
		CHECK(stored);
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
	rows.push_back(
		"{\"name\":\"offline_cache_lifecycle\",\"warm_hit_without_fit\":true,\"production_mapload_"
		"cache\":false}");
}
void HiddenAngularAndMounting()
{
	Rig rig;
	auto request=Request(rig);
	SurfaceCapture capture;capture.Begin(BuildPolicy::ForceRebuild);
	CHECK(capture.SetMap(rig.scene.map));
	for(const auto&s:rig.scene.surfaces)CHECK(capture.Add(s));
	CHECK(capture.Freeze());
	request.fitting.placement.originalSurfaces=capture.Acquire();
	auto hidden=std::make_shared<Hidden::Result>();hidden->key=Key("X16 angular contract");
	EmitterEvidence e;e.id=991;e.origin=EvidenceOrigin::SurfaceObservation;
	e.source="training receivers, NOT luminous fixture";e.positionKnown=e.orientationKnown=e.staticEligible=true;
	e.position=Vec3(0,0,1);e.outward=Vec3(0,0,-1);e.right=Vec3(1,0,0);e.up=Vec3(0,-1,0);
	e.coneKnown=true;e.coneDegrees=53.130102f;e.surfaces={0};e.shapes=CandidateSpot;
	e.issues=ShapeHypothesis|UncalibratedEmission;
	hidden->evidence={e};
	EmitterCandidate c;c.id=e.id;c.primary=0;c.evidence={0};c.shapes=CandidateSpot;c.staticEligible=true;
	hidden->candidates={c};
	Hidden::Hypothesis h;h.candidate=c.id;h.sourceKind=Bake::SourceKind::Spot;
	h.spotTargetDistanceWorld=128;h.spotRadiusWorld=64;h.profile="conditional";
	hidden->hypotheses={h};
	Candidates::Provider provider;
	CHECK(Candidates::FromHidden("X16",request.map.sha256,request.fitting.geometry.geometryKey,{},hidden,provider));
	Candidates::Request q;q.map=request.map.sha256;q.geometryKey=request.fitting.geometry.geometryKey;q.providers={provider};
	Candidates::Catalog catalog;CHECK(Candidates::Consolidate(q,Read(),catalog));
	CHECK(catalog.models.size()==1);
	if(!catalog.models.empty()){
		J::Candidate v;CHECK(P::MakeCandidate(catalog,catalog.models[0],request.fitting,v));
		CHECK(v.shape.initial.spotTargetDistanceWorld==128);
		CHECK(v.shape.initial.spotRadiusWorld==64);
		CHECK(v.shape.initial.support.kind==SourceSupport::Kind::LegacyVacuum);
		CHECK(v.shape.evidence==Shape::ShapeEvidence::Inferred);
	}
	auto annotations=std::make_shared<std::vector<Candidates::Annotation>>(provider.annotations.data,provider.annotations.data+provider.annotations.count);
	(*annotations)[0].historicalSpotTargetWorld=0;
	provider.annotations=Lease(annotations);q.providers={provider};
	CHECK(Candidates::Consolidate(q,Read(),catalog).code==Code::InvalidInput);
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
		ProviderIndependence();
		ProviderScopes();
		HiddenAngularAndMounting();
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
