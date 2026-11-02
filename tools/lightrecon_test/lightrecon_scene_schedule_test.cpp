// Controlled CPU orchestration tests. No source answers enter scheduling inputs.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/scene_schedule.h"
#include "producer_test_scene.h"
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace ProducerTest;
namespace SS = Rend2LightRecon::Core::SceneSchedule;
unsigned checks = 0, failures = 0;
std::string witness;
std::string amplitudeMetrics = "[]";
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
class Control final : public SS::Validation, public SS::FinalEvaluation
{
	const Rig &rig;

  public:
	mutable unsigned validations = 0, finals = 0;
	explicit Control(const Rig &r) : rig(r)
	{
	}
	Disk::Digest Key() const override
	{
		return JointFixture::Key("scene independent spatial evaluation");
	}
	Error Assess(const J::Request &q, const J::Model &m, const ReadContext &r,
				 J::Score &s) const override
	{
		++validations;
		return J::EvaluateFrozen(q, m, rig.validation, rig.validation->data->size(),
								 J::Role::Validation, Key(), r, s);
	}
	Error Assess(const J::Request &q, const J::Model &m, const ReadContext &r,
				 P::FinalReceipt &s) const override
	{
		++finals;
		s.evidence = "Frozen controlled held-out; answers not available to scheduler";
		return J::EvaluateFrozen(q, m, rig.held, rig.held->data->size(), J::Role::Heldout, Key(), r,
								 s.score);
	}
};
SS::Request Input(Rig &rig, size_t units)
{
	SS::Request s;
	auto request = ProducerTest::Request(rig);
	request.validationKey = request.heldoutKey =
		JointFixture::Key("scene independent spatial evaluation");
	request.fitting.options.geometry = request.fitting.options.materials =
		request.fitting.options.residualSearch = request.fitting.options.mergeSplit = false;
	request.fitting.options.maxCandidates = 32;
	request.fitting.options.maxSources = 8;
	s.branches = {request};
	s.observationPoolComplete = true;
	s.limits.wavesPerBranch = 2;
	s.limits.newCandidatesPerWave = 2;
	auto &h = s.discovery;
	h.map = rig.scene.map;
	h.geometry = rig.request.geometry;
	h.units = rig.request.profiles[0].model.units;
	h.domain = rig.scene.domain;
	h.domainKey = Key("controlled domain");
	h.analysisBounds = Box(V(-10, -10, -1), V(units * 512 + 512, 512, 512));
	h.options.coarseDivisions = 2;
	h.options.maxNodesPerArea = 8;
	h.options.refinementRounds = 0;
	h.options.spots = false;
	h.options.allowAssumedProfiles = h.options.allowAssumedEncoding = true;
	h.options.maxRays = 10000;
	h.options.maxWork = 100000;
	h.options.maxForwardCalls = 1000;
	auto samples = std::make_shared<std::vector<Hidden::Sample>>();
	for (size_t i = 0; i < units; ++i)
	{
		Hidden::Sample v;
		v.receiver.sample = 10000 + i;
		v.receiver.surface = 1;
		v.receiver.material = 1;
		v.receiver.world = V(i * 512 + 10, 10, 1).Float();
		v.receiver.geometricNormal = v.receiver.shadingNormal = Vec3(0, 0, 1);
		v.receiver.mappedPlanar = v.receiver.compilerPlacementCertified = true;
		v.areaWorldSquared = 1;
		v.eligible = true;
		samples->push_back(v);
	}
	h.training = VectorChunk<Hidden::Sample>(samples, 0, samples->size());
	return s;
}
Candidates::Provider Provider(Rig &rig, const std::string &name, Id candidate, Vec3 position,
							  const std::string &identity)
{
	auto ev = std::make_shared<std::vector<EmitterEvidence>>(1);
	auto ca = std::make_shared<std::vector<EmitterCandidate>>(1);
	auto an = std::make_shared<std::vector<Candidates::Annotation>>(1);
	auto &e = (*ev)[0];
	e.id = candidate;
	e.origin = EvidenceOrigin::BspEntity;
	e.source = name;
	e.positionKnown = e.staticEligible = true;
	e.position = position;
	e.shapes = CandidatePoint;
	auto &c = (*ca)[0];
	c.id = candidate;
	c.primary = 0;
	c.evidence = {0};
	c.shapes = CandidatePoint;
	c.staticEligible = true;
	auto &a = (*an)[0];
	a.candidate = candidate;
	a.sourceIdentity = Key(identity);
	a.tier = Candidates::Tier::Geometry;
	Candidates::Provider p;
	p.name = name;
	p.generation = Key(name);
	p.map = rig.scene.map.sha256;
	p.geometryKey = rig.request.geometry.geometryKey;
	p.evidence = VectorChunk<EmitterEvidence>(ev, 0, 1);
	p.candidates = VectorChunk<EmitterCandidate>(ca, 0, 1);
	p.annotations = VectorChunk<Candidates::Annotation>(an, 0, 1);
	return p;
}
void Test()
{
	Rig rig;
	auto a = Point(10, V(-.65, -.1, .8)), b = Point(20, V(.7, .15, 1.1));
	rig.request.candidates = {a, b};
	rig.Observations(rig.Truth({Answer(a, V(3.3, .1, .2)), Answer(b, V(.1, .3, 2.1))}));
	Control control(rig);
	auto input = Input(rig, 50);
	SS::Result whole, part, resumed;
	auto old = input.discovery;
	old.profiles = {rig.request.profiles[0].model.profile};
	Hidden::Result oldResult;
	CHECK(Hidden::Generate(old, Read(), oldResult).code == Code::WorkLimit);
	auto e = SS::Run(input, Read(), whole, &control, &control);
	CHECK(e);
	if (!e)
		std::printf("ERROR %s\n", e.message.c_str());
	CHECK(whole.units.size() == 50);
	CHECK(whole.work.localCalls == 50);
	CHECK(whole.work.jointCalls > 0);
	CHECK(whole.preferred != SIZE_MAX);
	CHECK(control.finals == 0);
	// Far receivers are behind the actual owned room wall, not magically clear.
	CHECK(std::count_if(whole.influence.begin(), whole.influence.end(),
						[](const SS::Influence &i) { return i.blocked > 0; }) >= 3);
	auto bridge = input;
	bridge.discovery.areas.clear();
	auto bridgeSamples = std::make_shared<std::vector<Hidden::Sample>>();
	for (unsigned i = 0; i < 3; ++i)
	{
		auto row = input.discovery.training.data[0];
		row.receiver.sample = 29000 + i;
		row.receiver.world = Vec3(float(-.45 + .45 * i), -.5f, 0);
		bridgeSamples->push_back(row);
		Hidden::SearchArea area;
		area.id = i + 1;
		area.positions = Box(V(-1.4, -1.3, .1), V(1.4, 1.3, 1.4));
		area.receivers =
			Box(V(row.receiver.world) - V(.1, .1, .1), V(row.receiver.world) + V(.1, .1, .1));
		area.reason = "Distinct receiver jobs with shared physical source influence";
		bridge.discovery.areas.push_back(area);
	}
	bridge.discovery.training =
		VectorChunk<Hidden::Sample>(bridgeSamples, 0, bridgeSamples->size());
	SS::Result sharedSource;
	CHECK(SS::Run(bridge, Read(), sharedSource, &control));
	CHECK(sharedSource.units.size() == 3);
	CHECK(std::count_if(sharedSource.influence.begin(), sharedSource.influence.end(),
						[](const SS::Influence &i) { return i.visible > 0; }) >= 3);
	if (whole.preferred != SIZE_MAX)
	{
		const auto &fit = whole.branches[whole.preferred].fit;
		CHECK(fit.preferred != SIZE_MAX);
		if (fit.preferred != SIZE_MAX)
		{
			const auto &m = fit.alternatives[fit.preferred];
			CHECK(m.sources.size() == 2);
			CHECK(m.validation.intervalMse < m.validation.blackMse * .01);
			std::ostringstream metric;
			metric << std::setprecision(12) << '[';
			for (size_t i = 0; i < m.sources.size(); ++i)
			{
				const auto &s = m.sources[i];
				V truth = s.candidate.id == 10 ? V(3.3, .1, .2) : V(.1, .3, 2.1);
				metric << (i ? "," : "") << "{\"candidate\":" << s.candidate.id
					   << ",\"fitted_relative_rgb\":[" << s.value.rgbAmplitude.x << ','
					   << s.value.rgbAmplitude.y << ',' << s.value.rgbAmplitude.z
					   << "],\"rgb_l2_error\":" << Length(V(s.value.rgbAmplitude) - truth) << '}';
			}
			metric << ']';
			amplitudeMetrics = metric.str();
		}
	}
	CHECK(SS::Run(input, Read(), part, &control, &control, {}, {}, 7));
	CHECK(part.work.localCalls == 7);
	CHECK(part.work.jointCalls == 0);
	CHECK(part.checkpoint);
	CHECK(SS::Run(input, Read(), resumed, &control, &control, part.checkpoint, part.cache));
	CHECK(resumed.graphKey == whole.graphKey);
	CHECK(resumed.work.localCalls == whole.work.localCalls);
	CHECK(resumed.branches[0].fit.alternatives.size() == whole.branches[0].fit.alternatives.size());
	if (resumed.preferred != SIZE_MAX && whole.preferred != SIZE_MAX)
		CHECK(resumed.branches[0].fit.alternatives[resumed.branches[0].fit.preferred].key ==
			  whole.branches[0].fit.alternatives[whole.branches[0].fit.preferred].key);
	// Reordering geometry areas cannot change stable jobs or source identities.
	std::vector<Hidden::SearchArea> areas;
	CHECK(Hidden::PlanAreas(input.discovery, 4096, Read(), areas));
	std::reverse(areas.begin(), areas.end());
	input.discovery.areas = areas;
	SS::Result reordered;
	CHECK(SS::Run(input, Read(), reordered, &control));
	CHECK(reordered.graphKey == whole.graphKey);
	for (size_t i = 0; i < whole.units.size(); ++i)
		CHECK(whole.units[i].id == reordered.units[i].id);
	auto changed = input;
	changed.branches[0].fitting.options.amplitudeSweeps++;
	SS::Result stale;
	CHECK(SS::Run(changed, Read(), stale, &control, nullptr, part.checkpoint).code ==
		  Code::InvalidState);
	// New numerical settings may reuse complete/no-evidence local receipts, never
	// transform incomplete geometry into clear rays or exact source evidence.
	CHECK(SS::Run(changed, Read(), stale, &control, nullptr, {}, whole.cache));
	CHECK(stale.graphKey != whole.graphKey);
	// The physical-integral branch has UnknownProfile for historical discovery;
	// unsupported admission is not a complete cached Hidden search.
	CHECK(stale.work.cacheHits == 0 && stale.work.localCalls == 50);
	for (size_t i = 0; i < whole.units.size(); ++i)
	{
		CHECK(stale.units[i].id == whole.units[i].id);
		CHECK(stale.units[i].generation == whole.units[i].generation);
	}
	SS::Result missing;
	CHECK(SS::Run(input, Read(), missing));
	CHECK(missing.work.jointCalls == 0);
	CHECK(!missing.finalEvaluated);
	class Stop : public Cancellation
	{
		bool IsCancelled() const noexcept override
		{
			return true;
		}
	} stop;
	auto read = Read();
	read.cancellation = &stop;
	CHECK(SS::Run(input, read, missing, &control).code == Code::Cancelled);
	changed = input;
	changed.limits.maxUnits = 4;
	CHECK(SS::Run(changed, Read(), missing, &control).code == Code::WorkLimit);
	changed = input;
	changed.limits.maxRays = 1;
	CHECK(SS::Run(changed, Read(), missing, &control).code == Code::WorkLimit);
	changed = input;
	changed.branches[0].fitting.geometry.geometryKey[0] ^= 1;
	CHECK(SS::Run(changed, Read(), missing, &control).code == Code::InvalidInput);
	class Countdown : public Cancellation
	{
		mutable unsigned calls = 0;
		bool IsCancelled() const noexcept override
		{
			return ++calls > 1350;
		}
	} countdown;
	read = Read();
	read.cancellation = &countdown;
	SS::Result interrupted, restarted;
	CHECK(SS::Run(input, read, interrupted, &control).code == Code::Cancelled);
	CHECK(interrupted.work.localCalls > 0 && interrupted.work.localCalls < 50);
	CHECK(SS::Run(input, Read(), restarted, &control, nullptr, interrupted.checkpoint,
				  interrupted.cache));
	CHECK(restarted.work.localCalls <= 51);
	CHECK(restarted.preferred != SIZE_MAX);
	// A separate gain hypothesis is observationally confounded with free RGB.
	auto ambiguous = input;
	auto alternative = ambiguous.branches[0];
	alternative.settingsIdentity += "; competing gain 2";
	alternative.fitting.profiles[0].fixedGain = 2;
	alternative.fitting.profiles[0].calibrationHypothesis =
		"Explicit competing relative gain=2; no measured photometry";
	ambiguous.branches.push_back(alternative);
	SS::Result alternatives;
	CHECK(SS::Run(ambiguous, Read(), alternatives, &control));
	CHECK(alternatives.branches.size() == 2);
	CHECK(alternatives.equivalentBranches.size() == 2);
	CHECK(alternatives.status == SS::Status::Unidentifiable);
	CHECK(control.finals == 0);
	std::reverse(ambiguous.branches.begin(), ambiguous.branches.end());
	SS::Result reversed;
	CHECK(SS::Run(ambiguous, Read(), reversed, &control));
	CHECK(reversed.graphKey == alternatives.graphKey);
	// Multiple providers explicitly attest one fixture, while a nearby distinct
	// lamp has a different authored identity. Proximity cannot unite the two.
	auto repeated = Input(rig, 3);
	repeated.branches[0].fitting.candidates.clear();
	repeated.branches[0].providers = {
		Provider(rig, "shard-a", 501, Vec3(0, 0, 1), "fixture-A"),
		Provider(rig, "shard-b", 502, Vec3(0, 0, 1), "fixture-A"),
		Provider(rig, "distinct-close-lamp", 503, Vec3(.02f, 0, 1), "fixture-B")};
	SS::Result merged, mergedReordered;
	CHECK(SS::Run(repeated, Read(), merged));
	CHECK(merged.branches[0].models == 2);
	CHECK(merged.branches[0].consolidation.merged == 1);
	std::reverse(repeated.branches[0].providers.begin(), repeated.branches[0].providers.end());
	CHECK(SS::Run(repeated, Read(), mergedReordered));
	CHECK(mergedReordered.graphKey == merged.graphKey);
	CHECK(mergedReordered.branches[0].catalogKey == merged.branches[0].catalogKey);
	// Evidence and candidate ordinals are different domains (stock crash witness).
	auto unequal = Provider(rig, "three-evidence-one-candidate", 601, Vec3(.3f, 0, 1), "fixture-C");
	auto ev = std::make_shared<std::vector<EmitterEvidence>>(3, unequal.evidence.data[0]);
	for (size_t i = 0; i < ev->size(); ++i)
		(*ev)[i].id = 900 + i;
	(*ev)[2].position = Vec3(.8f, .1f, 1.2f);
	auto ca = std::make_shared<std::vector<EmitterCandidate>>(1, unequal.candidates.data[0]);
	(*ca)[0].primary = 2;
	(*ca)[0].evidence = {0, 1, 2};
	unequal.evidence = VectorChunk<EmitterEvidence>(ev, 0, 3);
	unequal.candidates = VectorChunk<EmitterCandidate>(ca, 0, 1);
	Candidates::Request catalogInput;
	catalogInput.map = rig.scene.map.sha256;
	catalogInput.geometryKey = rig.request.geometry.geometryKey;
	catalogInput.providers = {unequal};
	Candidates::Catalog unequalCatalog;
	CHECK(Candidates::Consolidate(catalogInput, Read(), unequalCatalog));
	CHECK(unequalCatalog.models.size() == 1);
	J::Candidate converted;
	if (!unequalCatalog.models.empty())
	{
		CHECK(P::MakeCandidate(unequalCatalog, unequalCatalog.models[0], rig.request, converted));
		CHECK(converted.shape.initial.geometry.position.world.x == (*ev)[2].position.x);
		CHECK(converted.parents.size() >= 4);
	}
	// Known historical scope can cache a completed no-supported-evidence
	// admission, while keeping its lack of exhaustive physical search explicit.
	auto cacheable = Input(rig, 3);
	cacheable.branches[0].fitting.profiles[0].model.profile =
		Bake::NetRadiantProfile(Bake::Recipe());
	SS::Result cachedFirst, cachedAgain;
	CHECK(SS::Run(cacheable, Read(), cachedFirst));
	cacheable.branches[0].fitting.options.amplitudeSweeps++;
	CHECK(SS::Run(cacheable, Read(), cachedAgain, nullptr, nullptr, {}, cachedFirst.cache));
	CHECK(cachedAgain.work.cacheHits == 3 && cachedAgain.work.localCalls == 0);
	for (const auto &unit : cachedAgain.units)
		CHECK(unit.status == SS::Status::NoSupportedEvidence);
	// Positive reflected-only receivers do not require a local physical lamp.
	// The boundary root is supplied and labelled, not inferred from the answers.
	Rig reflection(false);
	reflection.request.profiles[0].contract.physicalTransportAssumed = true;
	reflection.request.materials = {KnownMaterial(1, V(.8, .1, .1)), KnownMaterial(2, V())};
	I::Root sky;
	sky.source = 950;
	sky.kind = TransportRootKind::Sky;
	sky.sunCoverage = SunCoverage::ExcludesExplicitSun;
	sky.generation = Key("known exterior boundary illumination");
	sky.provenance =
		"Controlled prescribed sky irradiance on ceiling reflector; no local point source";
	sky.incident.assign(reflection.request.transfer->Patches().size(), Vec3());
	sky.available.assign(sky.incident.size(), 1);
	sky.incident[0] = Vec3(4, 4, 4);
	reflection.request.boundaryRoots = {sky};
	I::Request gi;
	gi.transfer = reflection.request.transfer;
	gi.roots = {sky};
	gi.materials = reflection.request.materials;
	gi.contract = reflection.request.profiles[0].contract;
	gi.trainingKey = Key("trusted bounce answer generation, never candidate input");
	auto grow = std::make_shared<Stream<I::Observation>>();
	I::Observation dummy;
	dummy.id = 1;
	dummy.patch = 1;
	dummy.spatialBlock = 1;
	dummy.areaWorldSquared = 1;
	grow->data->push_back(dummy);
	gi.training = grow;
	gi.count = 1;
	I::Result answer;
	CHECK(I::Fit(gi, Read(), answer));
	std::vector<Vec3> bounce;
	for (const auto &v : answer.values)
		bounce.push_back(v.totalPreencoding);
	reflection.Observations(bounce);
	Control reflectionControl(reflection);
	auto noLamp = Input(reflection, 3);
	SS::Result reflected;
	CHECK(SS::Run(noLamp, Read(), reflected, &reflectionControl));
	CHECK(reflected.preferred != SIZE_MAX);
	if (reflected.preferred != SIZE_MAX)
	{
		const auto &fit = reflected.branches[reflected.preferred].fit;
		const auto &m = fit.alternatives[fit.preferred];
		CHECK(m.sources.empty());
		CHECK(m.validation.positive);
		CHECK(m.validation.intervalMse < m.validation.blackMse * .01);
	}
	// Independent publication performs held-out assessment once, after all waves.
	P::Result scoped;
	CHECK(P::PrepareModelRequests(input.branches[0], Read(), scoped));
	CHECK(scoped.scopedRequests.size() == 1);
	if (!scoped.scopedRequests.empty())
		input.branches[0] = *scoped.scopedRequests[0];
	input.limits.publish = true;
	SS::Result published;
	CHECK(SS::Run(input, Read(), published, &control, &control));
	CHECK(control.finals == 1);
	CHECK(published.finalEvaluated);
	CHECK(published.publication.artifact.asset);
	if (published.publication.artifact.asset)
	{
		CHECK(P::ValidateArtifact(published.publication.artifact, Read()));
		CHECK(published.publication.artifact.asset->assessment.resolvedSources == 0);
		CHECK(!published.publication.artifact.asset->assessment.providers.empty());
	}
	auto invalidBinding = input.branches[0];
	invalidBinding.fitting.geometry.geometryKey[0] ^= 1;
	CHECK(P::RebindProviders(input.branches[0], invalidBinding).code == Code::InvalidInput);
	SS::Result publishedResume;
	CHECK(SS::Run(input, Read(), publishedResume, &control, &control, published.checkpoint,
				  published.cache));
	CHECK(control.finals == 1);
	CHECK(publishedResume.publication.artifact.key == published.publication.artifact.key);
	witness = SS::ReportJson(whole);
}
int main(int argc, char **argv)
{
	try
	{
		Test();
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	if (argc > 1)
	{
		std::ofstream report(argv[1]);
		report << "{\"checks\":" << checks << ",\"failures\":" << failures
			   << ",\"protocol\":\"SuppliedFixedGeometrySceneSchedule\",\"source_metrics\":"
			   << amplitudeMetrics << ",\"scene\":" << (witness.empty() ? "null" : witness)
			   << "}\n";
	}
	std::printf("X11: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
