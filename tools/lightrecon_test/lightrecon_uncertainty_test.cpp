// Trusted S12 tests. Private source answers never enter uncertainty analysis.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_test_scene.h"
#include "lightrecon/uncertainty.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace JointTest;
namespace U = Rend2LightRecon::Core::Uncertainty;
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
class Oracle final : public J::Validation
{
	const J::Request &geometry;
	std::shared_ptr<Stream<J::Observation>> values;

  public:
	Oracle(const Rig &r) : geometry(r.request), values(r.validation) {}
	Disk::Digest Key() const override { return JointFixture::Key("S12 spatial validation only v1"); }
	Error Evaluate(const J::Model &m, const ReadContext &read, J::Score &score) const override
	{
		return J::EvaluateFrozen(geometry, m, values, values->data->size(), J::Role::Validation, Key(), read,
								 score);
	}
};
J::Model Initial(Rig &rig, const std::vector<J::Candidate> &candidates)
{
	J::Model m;
	m.geometryKey = rig.request.geometry.geometryKey;
	m.transferKey = rig.request.transfer->Key();
	m.materials = rig.request.materials;
	for (auto c : candidates)
		m.sources.push_back(Answer(c, V(c.shape.initial.rgbAmplitude)));
	J::Model fitted;
	J::Work work;
	Must(J::Refit(rig.request, m, Read(), fitted, work));
	return fitted;
}
U::Options Cheap()
{
	U::Options o;
	o.positionLevels = o.colourProfiles = o.deleteBlocks = o.subsampleReplicates = o.restarts = 0;
	o.materialProfiles = false;
	o.maxAttempts = 200;
	o.maxRefits = 200;
	o.maxWitnesses = 200;
	o.trainingRegret = 1e-4;
	o.validationRegret = 1e-4;
	return o;
}
std::vector<std::string> records;
void Record(const char *name, const U::Result &result)
{
	std::ostringstream s;
	s << std::setprecision(12) << "{\"name\":\"" << name << "\",\"state\":\"" << U::StateName(result.state)
	  << "\",\"complete\":" << (result.complete ? "true" : "false") << ",\"key\":\"" << Disk::Hex(result.key)
	  << "\",\"witnesses\":" << result.witnesses.size() << ",\"tracks\":" << result.tracks.size()
	  << ",\"attempts\":" << result.work.attempts << ",\"refits\":" << result.work.refits
	  << ",\"evaluations\":" << result.work.evaluations << ",\"rays\":" << result.work.rays
	  << ",\"bytes\":" << result.work.accountedBytes << ",\"parameters\":[";
	for (size_t i = 0; i < result.tracks.size(); ++i)
	{
		const auto &t = result.tracks[i];
		s << (i ? "," : "") << "{\"id\":" << t.id
		  << ",\"existence_known\":" << (t.existence.probability.known ? "true" : "false")
		  << ",\"exact\":" << unsigned(t.exactLight) << ",\"peak_lower\":" << t.relativePeak.lower
		  << ",\"peak_upper\":" << t.relativePeak.upper << ",\"position_scatter\":[" << t.scatter.xx << ','
		  << t.scatter.xy << ',' << t.scatter.xz << ',' << t.scatter.yy << ',' << t.scatter.yz << ','
		  << t.scatter.zz << "]}";
	}
	s << "]}";
	records.push_back(s.str());
}
void Unique()
{
	Rig rig;
	auto c = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {c};
	rig.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	auto m = Initial(rig, {c});
	J::Result input;
	input.alternatives = {m};
	Oracle oracle(rig);
	auto options = Cheap();
	options.positionLevels = 1;
	options.colourProfiles = 3;
	options.deleteBlocks = 2;
	options.subsampleReplicates = 2;
	options.restarts = 2;
	options.positionStepWorld = .2;
	U::Result result, again;
	Must(U::Analyze(rig.request, input, options, Read(), result, &oracle));
	Must(U::Analyze(rig.request, input, options, Read(), again, &oracle));
	CHECK(result.key == again.key);
	CHECK(result.complete);
	CHECK(!result.globalSearchComplete);
	CHECK(result.tracks.size() == 1);
	if (!result.tracks.empty())
	{
		auto &t = result.tracks[0];
		CHECK(t.factors.size() == 13);
		CHECK(!t.existence.probability.known);
		CHECK(t.exactLight == Suitability::Unsuitable);
		CHECK(!t.absoluteScaleKnown);
		CHECK(t.removalAvailable);
		CHECK(t.relativePeak.lower <= 2.001 && t.relativePeak.upper >= 1.999);
	}
	CHECK(result.work.sampledBlocks > 0);
	CHECK(result.work.refits > 10);
	Record("unique_multinormal", result);
	auto identityOnly = Cheap();
	U::Result keyA, keyB;
	Must(U::Analyze(rig.request, input, identityOnly, Read(), keyA, &oracle));
	identityOnly.minimumExactRateLower = .99;
	Must(U::Analyze(rig.request, input, identityOnly, Read(), keyB, &oracle));
	CHECK(keyA.key != keyB.key);
	identityOnly.minimumExactRateLower = 2;
	CHECK(U::Analyze(rig.request, input, identityOnly, Read(), keyB, &oracle).code == Code::InvalidInput);
	// Atomic canonical attachment preserves source parameters and unknown sigma.
	ReconstructedLightingAsset a;
	a.lineage = "s12-owned";
	a.provenance = 1;
	Provenance p;
	p.id = 1;
	p.producer = "s12-tests/1";
	a.provenanceRecords = {p};
	a.emitters = {c.shape.initial.geometry};
	a.emitters[0].source.provenance = 1;
	std::vector<U::SourceBinding> bindings(1);
	bindings[0].source = 10;
	bindings[0].track = result.tracks[0].id;
	Must(U::Attach(result, rig.request, bindings, 1, 1000, a));
	CHECK(Validate(a));
	CHECK(a.uncertainty.size() == 1);
	CHECK(!a.emitters[0].position.sigmaWorld.known);
	CHECK(a.emitters[0].position.world.x == c.shape.initial.geometry.position.world.x);
	CHECK(a.emitters[0].source.state == HypothesisState::Candidate);
	Disk::Fingerprint fingerprint;
	fingerprint.algorithm = "s12-test";
	fingerprint.algorithmVersion = 1;
	fingerprint.complete.fill(true);
	for (auto kind : {Disk::InputKind::Bsp, Disk::InputKind::Settings, Disk::InputKind::UnitCalibration})
	{
		Disk::InputContent v;
		v.kind = kind;
		v.name = "s12-owned/" + std::to_string(unsigned(kind));
		v.bytes = v.name.size();
		v.sha256 = Key(v.name);
		fingerprint.inputs.push_back(v);
	}
	std::vector<uint8_t> bytes;
	CHECK(Disk::Encode(a, fingerprint, {}, bytes));
	ReconstructedLightingAsset decoded;
	Disk::Index index;
	Disk::MemoryReader reader(bytes.data(), bytes.size());
	CHECK(Disk::Open(reader, fingerprint, decoded, index));
	CHECK(decoded.uncertainty.size() == 1);
	CHECK(decoded.lightingConfigurations.size() == a.lightingConfigurations.size());
	// Covariance and confidence claims must be validated rather than arbitrary floats.
	auto invalid = a;
	invalid.uncertainty[0].existence.probability.known = true;
	CHECK(!Validate(invalid));
	invalid = a;
	auto &scatter = invalid.uncertainty[0].positionScatter;
	scatter.known = true;
	scatter.samples = 2;
	scatter.method = "bad";
	scatter.xx = scatter.yy = scatter.zz = 1;
	scatter.xy = 3;
	CHECK(!Validate(invalid));
	invalid = a;
	invalid.provenanceRecords[0].kind = ProvenanceKind::Authored;
	CHECK(!U::Attach(result, rig.request, bindings, 1, 2000, invalid));
	CHECK(invalid.uncertainty.size() == 1);
	auto changed = rig.request;
	changed.geometry.geometryKey = Key("different geometry");
	CHECK(!U::Attach(result, changed, bindings, 1, 2000, a));
}
void Symmetry()
{
	Rig rig(false);
	auto c = Point(10, V(-.5, 0, .8));
	rig.request.candidates = {c};
	rig.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	auto filter = [](std::shared_ptr<Stream<J::Observation>> stream) {
		auto &v = *stream->data;
		v.erase(std::remove_if(v.begin(), v.end(),
							   [](const J::Observation &o) { return std::abs(o.receiver.world.x) > .001; }),
				v.end());
	};
	filter(rig.train);
	filter(rig.held);
	rig.validation->data->clear();
	for (auto o : *rig.held->data)
	{
		o.role = J::Role::Validation;
		rig.validation->data->push_back(o);
	}
	rig.request.count = rig.train->data->size();
	auto m = Initial(rig, {c});
	auto mirrored = m;
	mirrored.sources[0].value.geometry.position.world.x = .5f;
	J::Model checked;
	J::Score score;
	J::Work work;
	Must(J::AssessHypothesis(rig.request, mirrored, Read(), checked, score, work));
	J::Result input;
	input.alternatives = {m, checked};
	Oracle oracle(rig);
	U::Result result;
	Must(U::Analyze(rig.request, input, Cheap(), Read(), result, &oracle));
	CHECK(result.witnesses.size() >= 2);
	CHECK(result.tracks.size() == 1);
	CHECK(result.tracks[0].scatter.known);
	CHECK(result.tracks[0].scatter.xx > .2);
	CHECK(std::abs(result.tracks[0].scatter.reference.x) < .001);
	CHECK(result.tracks[0].exactLight == Suitability::Unsuitable);
	for (const auto &w : result.witnesses)
		CHECK(std::abs(w.model.sources[0].value.geometry.position.world.x) > .49);
	Record("two_disconnected_positions_single_plane", result);
	// Saturation: many different amplitudes satisfy one-sided bounds. It must not
	// become a narrow point-valued answer merely because the solver is stationary.
	for (auto stream : {rig.train, rig.validation})
		for (auto &row : *stream->data)
			for (auto &v : row.rgb)
			{
				v.lower = .1;
				v.upper = std::numeric_limits<double>::max();
				v.censored = true;
			}
	rig.request.options.maximumRequiredBlockRmse = 0;
	m = Initial(rig, {c});
	input.alternatives = {m};
	auto options = Cheap();
	options.colourProfiles = 3;
	U::Result saturated;
	Must(U::Analyze(rig.request, input, options, Read(), saturated, &oracle));
	CHECK(saturated.tracks.empty() ||
		  saturated.tracks[0].relativePeak.upper - saturated.tracks[0].relativePeak.lower > 1);
	Record("censored_weak_evidence", saturated);
}
void Correlation()
{
	Rig rig(false);
	auto c = Point(10, V(0, 0, .8));
	rig.request.candidates = {c};
	rig.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	// One compact receiver cannot separate tangential position and distance/amplitude.
	rig.train->data->resize(1);
	rig.request.count = 1;
	rig.validation->data->clear();
	auto row = rig.train->data->front();
	row.role = J::Role::Validation;
	row.id += 1000;
	rig.validation->data->push_back(row);
	auto m = Initial(rig, {c});
	J::Result input;
	input.alternatives = {m};
	Oracle oracle(rig);
	auto options = Cheap();
	options.positionLevels = 2;
	options.positionStepWorld = .15;
	options.trainingRegret = options.validationRegret = .002;
	U::Result result;
	Must(U::Analyze(rig.request, input, options, Read(), result, &oracle));
	CHECK(result.witnesses.size() > 2);
	CHECK(result.tracks[0].scatter.known);
	CHECK(result.tracks[0].scatter.xx + result.tracks[0].scatter.yy + result.tracks[0].scatter.zz > 0);
	Record("one_receiver_distance_rgb_ambiguity", result);
}
void ShadowAndEnvironment()
{
	Rig rig(false);
	auto c = Point(10, V(-.5, 0, .8));
	c.family = 90;
	auto behind = Point(11, V(.5, 0, .8));
	behind.family = 90;
	rig.scene.Quad(V(.25, -2, .01), V(.25, 2, .01), V(.25, 2, 1.5), V(.25, -2, 1.5));
	Rebuild(rig.scene);
	rig.UpdateGeometry();
	rig.request.candidates = {c, behind};
	rig.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	auto filter = [](std::shared_ptr<Stream<J::Observation>> s) {
		auto &v = *s->data;
		v.erase(std::remove_if(v.begin(), v.end(),
							   [](const J::Observation &o) { return std::abs(o.receiver.world.x) > .001; }),
				v.end());
	};
	filter(rig.train);
	filter(rig.held);
	rig.validation->data->clear();
	for (auto row : *rig.held->data)
	{
		row.role = J::Role::Validation;
		rig.validation->data->push_back(row);
	}
	rig.request.count = rig.train->data->size();
	auto m = Initial(rig, {c}), other = m;
	other.sources[0] = Answer(behind, V(2, 1, .5));
	J::Model assessed;
	J::Score score;
	J::Work work;
	Must(J::AssessHypothesis(rig.request, other, Read(), assessed, score, work));
	J::Result input;
	input.alternatives = {m, assessed};
	Oracle oracle(rig);
	U::Result result;
	Must(U::Analyze(rig.request, input, Cheap(), Read(), result, &oracle));
	CHECK(!result.witnesses.empty());
	for (const auto &w : result.witnesses)
		for (const auto &s : w.model.sources)
			CHECK(s.value.geometry.position.world.x < 0);
	Record("independent_opaque_shadow_breaks_plane_symmetry", result);
	Rig daylight(false);
	daylight.scene.Quad(V(-100, -100, 3), V(100, -100, 3), V(100, 100, 3), V(-100, 100, 3), MaterialSky);
	Rebuild(daylight.scene);
	Bake::Recipe recipe;
	recipe.fast = false;
	Historical(daylight, recipe);
	auto sun = CompilerPoint(10, V());
	sun.root = TransportRootKind::Sun;
	sun.family = 90;
	sun.environment.id = 10;
	sun.environment.kind = Bake::SourceKind::Directional;
	sun.environment.orientation = FrameDown();
	sun.shape.initial.geometry.kind = EmitterKind::Directional;
	sun.shape.initial.geometry.position.known = false;
	sun.shape.initial.geometry.frame = FrameDown();
	sun.shape.initial.geometry.angular.kind = AngularKind::Parallel;
	auto sky = sun;
	sky.id = 11;
	sky.root = TransportRootKind::Sky;
	sky.environment.id = 11;
	sky.environment.kind = Bake::SourceKind::Sky;
	daylight.request.candidates = {sun, sky};
	daylight.Observations(daylight.Truth({Answer(sun, V(40, 30, 20))}), true);
	// A single irradiance datum is deliberately insufficient to identify sun vs
	// sky. This unit test is not an independent positive-sun acceptance benchmark.
	daylight.train->data->resize(1);
	daylight.request.count = 1;
	daylight.validation->data->clear();
	auto datum = daylight.train->data->front();
	datum.role = J::Role::Validation;
	datum.id += 1000;
	daylight.validation->data->push_back(datum);
	input.alternatives = {Initial(daylight, {sun}), Initial(daylight, {sky})};
	Oracle skyOracle(daylight);
	auto options = Cheap();
	options.trainingRegret = options.validationRegret = 1;
	Must(U::Analyze(daylight.request, input, options, Read(), result, &skyOracle));
	CHECK(result.witnesses.size() >= 2);
	CHECK(result.tracks.size() == 1);
	CHECK(!result.tracks[0].existence.probability.known);
	CHECK(result.tracks[0].exactLight == Suitability::Unsuitable);
	Record("sun_sky_single_datum_ambiguity", result);
	Rig dark;
	auto lamp = Point(10, V(0, 0, .8));
	dark.request.candidates = {lamp};
	dark.Observations(std::vector<Vec3>(dark.scene.patches.size()));
	input.alternatives = {Initial(dark, {lamp})};
	Oracle darkOracle(dark);
	Must(U::Analyze(dark.request, input, Cheap(), Read(), result, &darkOracle));
	CHECK(result.state == U::State::Unknown);
	for (const auto &t : result.tracks)
	{
		CHECK(!t.existence.probability.known);
		CHECK(t.approximateField != Suitability::Candidate);
	}
	Record("zero_evidence_unknown_not_uniform_lights", result);
}
void CountTypeMaterial()
{
	Rig rig;
	auto a = Point(10, V(-.6, 0, .8)), b = Point(11, V(-.6, 0, .8));
	a.family = 11; // family IDs and independent source IDs are separate namespaces
	rig.request.candidates = {a, b};
	rig.Observations(rig.Truth({Answer(a, V(2, 1, .5))}));
	auto one = Initial(rig, {a}), two = Initial(rig, {a, b});
	J::Result input;
	input.alternatives = {one, two};
	Oracle oracle(rig);
	auto options = Cheap();
	options.trainingRegret = options.validationRegret = .01;
	U::Result result;
	Must(U::Analyze(rig.request, input, options, Read(), result, &oracle));
	CHECK(result.witnesses.size() >= 2);
	CHECK(result.tracks.size() == 2);
	CHECK(result.tracks[0].minimumSceneSources == 1 && result.tracks[0].maximumSceneSources == 2);
	Record("coincident_same_colour_count", result);
	// A physical point and a broad cone can be indistinguishable over a small floor.
	Rig plane(false);
	a = Point(10, V(-.4, 0, .8));
	a.family = 90;
	auto spot = Extended(11, EmitterKind::Spot, V(-.4, 0, .8));
	spot.family = 90;
	spot.shape.initial.geometry.angular.cosInner = .01f;
	spot.shape.initial.geometry.angular.cosOuter = -.99f;
	plane.request.candidates = {a, spot};
	plane.Observations(plane.Truth({Answer(a, V(2, 1, .5))}));
	input.alternatives = {Initial(plane, {a}), Initial(plane, {spot})};
	Oracle planeOracle(plane);
	options.trainingRegret = options.validationRegret = .1;
	Must(U::Analyze(plane.request, input, options, Read(), result, &planeOracle));
	CHECK(!result.tracks.empty());
	CHECK(result.tracks[0].types == 2);
	Record("point_broad_spot_alternatives", result);
	// Unknown material/profile calibration hypotheses remain coupled with RGB.
	rig.request.materials[0].knowledge = I::MaterialKnowledge::Bounded;
	rig.request.materials[0].upper = Vec3(.8, .2, .2);
	rig.request.options.materials = true;
	rig.request.profiles[0].contract.physicalTransportAssumed = true;
	auto profile = rig.request.profiles[0];
	profile.fixedGain = 2;
	profile.calibrationHypothesis = "Alternative gain=2, same unknown physical scale";
	rig.request.profiles.push_back(profile);
	rig.Observations(rig.Truth({Answer(a, V(2, 1, .5))}));
	one = Initial(rig, {a});
	auto second = one;
	second.profile = 1;
	J::Work w;
	Must(J::Refit(rig.request, second, Read(), second, w));
	input.alternatives = {one, second};
	options = Cheap();
	options.materialProfiles = true;
	Must(U::Analyze(rig.request, input, options, Read(), result, &oracle));
	CHECK(result.witnesses.size() >= 2);
	CHECK(!result.tracks[0].absoluteScaleKnown);
	Record("profile_material_gi_scale", result);
}
struct Stop final : Cancellation
{
	bool IsCancelled() const noexcept override { return true; }
};
void Negative()
{
	Rig rig;
	auto c = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {c};
	rig.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	J::Result input;
	input.alternatives = {Initial(rig, {c})};
	Oracle oracle(rig);
	U::Result result;
	auto options = Cheap();
	options.positionLevels = 1;
	options.maxAttempts = 1;
	Must(U::Analyze(rig.request, input, options, Read(), result, &oracle));
	CHECK(!result.complete);
	CHECK(result.state == U::State::Partial);
	Record("truncated_profile_schedule", result);
	Stop stop;
	auto read = Read();
	read.cancellation = &stop;
	CHECK(U::Analyze(rig.request, input, Cheap(), read, result, &oracle).code == Code::Cancelled);
	CHECK(result.state == U::State::Cancelled);
	options = Cheap();
	options.maxEvaluations = 1;
	CHECK(!U::Analyze(rig.request, input, options, Read(), result, &oracle));
	CHECK(!result.complete);
	auto invalid = input;
	invalid.alternatives[0].sources[0].value.rgbAmplitude.x += 1;
	CHECK(!U::Analyze(rig.request, invalid, Cheap(), Read(), result, &oracle));
	rig.train->data->front().role = J::Role::Heldout;
	CHECK(!U::Analyze(rig.request, input, Cheap(), Read(), result, &oracle));
	rig.train->data->front().role = J::Role::Training;
	rig.train->corrupt = true;
	CHECK(!U::Analyze(rig.request, input, Cheap(), Read(), result, &oracle));
	rig.train->corrupt = false;
	// Missing geometry must not manufacture a low confidence physical source.
	Rig missing;
	missing.scene.surfaces.push_back(BakedSurface());
	missing.scene.surfaces.back().kind = BakedSurfaceKind::Unsupported;
	missing.scene.surfaces.back().ordinal = unsigned(missing.scene.surfaces.size() - 1);
	Rebuild(missing.scene);
	missing.UpdateGeometry();
	missing.request.candidates = {c};
	missing.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	auto uncertain = input;
	uncertain.alternatives[0].geometryKey = missing.request.geometry.geometryKey;
	uncertain.alternatives[0].transferKey = missing.request.transfer->Key();
	CHECK(U::Analyze(missing.request, uncertain, Cheap(), Read(), result).code == Code::MissingGeometry);
	CHECK(result.witnesses.empty());
	Rig alpha;
	alpha.scene.Quad(V(0, -2, 0), V(0, 2, 0), V(0, 2, 1.6), V(0, -2, 1.6), MaterialCutout);
	Rebuild(alpha.scene);
	alpha.UpdateGeometry();
	alpha.request.candidates = {c};
	alpha.Observations(rig.Truth({Answer(c, V(2, 1, .5))}));
	uncertain = input;
	uncertain.alternatives[0].geometryKey = alpha.request.geometry.geometryKey;
	uncertain.alternatives[0].transferKey = alpha.request.transfer->Key();
	J::Model opticalModel;
	J::Score opticalScore;
	J::Work opticalWork;
	auto optical = J::AssessHypothesis(alpha.request, uncertain.alternatives[0], Read(), opticalModel,
									   opticalScore, opticalWork);
	if (optical)
	{
		CHECK(!opticalScore.available);
		uncertain.alternatives = {opticalModel};
		auto unavailable = U::Analyze(alpha.request, uncertain, Cheap(), Read(), result);
		CHECK(unavailable.code == Code::Unavailable);
		CHECK(result.witnesses.empty());
	}
	else
	{
		CHECK(optical.code == Code::MissingGeometry || optical.code == Code::Unavailable);
	}
	// Authored priors remain unchanged when the observations contradict them.
	auto wrong = input;
	wrong.alternatives[0].sources[0].value.geometry.position.world = Vec3(0, 0, -1);
	CHECK(!U::Analyze(rig.request, wrong, Cheap(), Read(), result, &oracle));
}
void TestCalibration()
{
	U::CalibrationPolicy policy;
	policy.population = "owned independent calibration unit tests";
	policy.protocol = "frozen matching region and bin rule v1";
	policy.independentFamiliesAttested = true;
	policy.minimumFamilies = 4;
	policy.binEdges = {0, .5, 1};
	std::vector<U::CalibrationTrial> rows;
	for (unsigned f = 0; f < 24; ++f)
		for (unsigned j = 0; j < 3; ++j)
		{
			U::CalibrationTrial t;
			t.family = "family-" + std::to_string(f);
			t.scene = "independent draw " + std::to_string(f);
			t.trial = std::to_string(j);
			t.ranking = j == 0 ? .1 : .9;
			t.labelKnown = true;
			t.label = j != 0;
			rows.push_back(t);
		}
	U::CalibrationModel model, repeated;
	Must(U::Calibrate(policy, rows, Read(), model));
	auto shuffled = rows;
	std::reverse(shuffled.begin(), shuffled.end());
	Must(U::Calibrate(policy, shuffled, Read(), repeated));
	CHECK(model.key == repeated.key);
	CalibratedConfidence low, high;
	Must(U::CalibratedRate(model, U::Event::Existence, .1, policy.population, "final-independent-1", low));
	Must(U::CalibratedRate(model, U::Event::Existence, .9, policy.population, "final-independent-1", high));
	CHECK(low.probability.known && low.probability.value == 0);
	CHECK(high.probability.value == 1);
	CHECK(low.independentFamilies == 24 && high.independentFamilies == 24);
	CHECK(high.probability.lower > .5);
	CHECK(!U::CalibratedRate(model, U::Event::Existence, .9, policy.population, "family-0", high));
	CHECK(!high.probability.known);
	CHECK(!U::CalibratedRate(model, U::Event::Existence, .9, "stock UnknownProfile", "unseen", high));
	for (auto &t : rows)
		t.family = "one-correlated-room";
	Must(U::Calibrate(policy, rows, Read(), model));
	CHECK(!U::CalibratedRate(model, U::Event::Existence, .9, policy.population, "unseen", high));
	rows.push_back(rows[0]);
	CHECK(!U::Calibrate(policy, rows, Read(), model));
}
int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		auto run = [](const char *name, void (*test)()) {
			std::printf("S12 case %s\n", name);
			test();
		};
		run("unique", Unique);
		run("symmetry", Symmetry);
		run("correlation", Correlation);
		run("shadow_environment", ShadowAndEnvironment);
		run("count_type_material", CountTypeMaterial);
		run("negative", Negative);
		run("calibration", TestCalibration);
		if (argc > 1)
		{
			std::ofstream file(argv[1]);
			file << "{\"schema\":\"org.openjk.uncertainty-tests.v1\",\"checks\":" << checks
				 << ",\"failures\":" << failures << ",\"cases\":[";
			for (size_t i = 0; i < records.size(); ++i)
				file << (i ? "," : "") << records[i];
			file << "]}\n";
		}
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	std::printf("S12 %d checks %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
