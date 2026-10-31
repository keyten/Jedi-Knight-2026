// Trusted calibration/evaluation controller. No answers enter Refit/Analyze.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_test_scene.h"
#include "lightrecon/uncertainty.h"
#include <cstdio>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
using namespace JointTest;
namespace U = Rend2LightRecon::Core::Uncertainty;
class Oracle final : public J::Validation
{
	const Rig &rig;

  public:
	Oracle(const Rig &r) : rig(r) {}
	Disk::Digest Key() const override
	{
		return JointFixture::Key("S12 population validation spatial y+/x- v1");
	}
	Error Evaluate(const J::Model &m, const ReadContext &read, J::Score &score) const override
	{
		return J::EvaluateFrozen(rig.request, m, rig.validation, rig.validation->data->size(),
								 J::Role::Validation, Key(), read, score);
	}
};
struct Trial
{
	U::CalibrationTrial calibration;
	CalibratedConfidence confidence;
	bool fieldUseful = false;
	bool calibrationEligible = false;
	double heldMse = 0, blackMse = 0;
};
struct Family
{
	unsigned index = 0;
	std::string geometry;
	double scale = 1, intensity = 1;
	unsigned stratum = 0, truth = 0, missed = 0;
	uint64_t rays = 0, evaluations = 0, bytes = 0;
	std::vector<Trial> trials;
	double jointDistance = 0;
	bool jointCovered = false;
};
uint64_t randomState;
double Random()
{
	randomState ^= randomState << 13;
	randomState ^= randomState >> 7;
	randomState ^= randomState << 17;
	return double(randomState % 10000001) / 10000000;
}
std::vector<int> Match(const std::vector<J::Source> &truth, const std::vector<J::Source> &predicted,
					   double radius)
{
	// Small trusted-fixture exact assignment. Maximize match cardinality before
	// minimizing distance; neither source IDs nor source ordering defines truth.
	std::vector<int> current(predicted.size(), -1), best = current;
	std::set<size_t> used;
	int maximum = -1;
	double cost = 1e300;
	std::function<void(size_t, int, double)> visit = [&](size_t i, int n, double d) {
		if (i == predicted.size())
		{
			if (n > maximum || (n == maximum && d < cost))
			{
				maximum = n;
				cost = d;
				best = current;
			}
			return;
		}
		visit(i + 1, n, d);
		for (size_t j = 0; j < truth.size(); ++j)
			if (!used.count(j))
			{
				double error = Length(V(predicted[i].value.geometry.position.world) -
									  V(truth[j].value.geometry.position.world));
				if (error > radius)
					continue;
				used.insert(j);
				current[i] = int(j);
				visit(i + 1, n + 1, d + error);
				current[i] = -1;
				used.erase(j);
			}
	};
	visit(0, 0, 0);
	return best;
}
Family Run(unsigned index, const U::CalibrationModel *calibrated = nullptr)
{
	// Each scene is a separate deterministic draw of geometry, source layout,
	// amplitude and candidate perturbations. Population is intentionally narrow:
	// owned planar CPU rooms, NOT a calibration of stock JA or historical profiles.
	randomState = 0x123456789abcdefull ^ (uint64_t(index + 1) * 0x9e3779b97f4a7c15ull);
	for (int i = 0; i < 8; ++i)
		Random();
	Family f;
	f.index = index;
	f.stratum = index % 3;
	f.scale = .6 + Random() * 1.4;
	f.intensity = (index / 3) % 3 == 0 ? .04 : (index / 3) % 3 == 1 ? .4 : 3;
	Rig rig(f.stratum != 0);
	if (f.stratum == 2)
	{
		rig.scene.Quad(V(0, -1.4, 0), V(0, -.5, 0), V(0, -.5, 1.4), V(0, -1.4, 1.4));
		Rebuild(rig.scene);
	}
	Scale(rig.scene, f.scale);
	rig.UpdateGeometry();
	f.geometry = Disk::Hex(rig.scene.map.sha256);
	auto point = [&](Id id, V position) {
		auto c = Point(id, position * f.scale);
		c.shape.centreBounds = Box(V(-1.4, -1.3, .1) * f.scale, V(1.4, 1.3, 1.4) * f.scale);
		c.shape.maximumTranslationWorld = 2 * f.scale;
		c.shape.upperRgb = V(6, 6, 6).Float();
		return c;
	};
	V location(-.9 + Random() * 1.8, -.2 + Random() * .4, .45 + Random() * .7);
	auto actual = point(10, location);
	V q(f.intensity * (.7 + Random() * .3), f.intensity * (.2 + Random() * .8),
		f.intensity * (.2 + Random() * .8));
	std::vector<J::Source> answers;
	if (index % 11 != 0)
		answers.push_back(Answer(actual, q));
	if (index % 5 == 0 && index % 11 != 0)
	{
		auto extra = point(20, V(-location.x, .2, 1.2));
		answers.push_back(Answer(extra, q * .25));
	}
	f.truth = unsigned(answers.size());
	std::vector<J::Candidate> candidates;
	// Deliberately miss the physical source on a frozen subset of scenes. These
	// false candidates/missed truths remain in evaluation denominators.
	if (index % 6 != 0)
	{
		auto c = actual;
		if (index % 7 == 0)
			c.shape.initial.geometry.position.world.x += float(.28 * f.scale);
		candidates.push_back(c);
	}
	candidates.push_back(point(30, V(-.95, .3, .6)));
	candidates.push_back(point(40, V(.95, -.25, 1.15)));
	rig.request.candidates = candidates;
	rig.request.options.diagnosticSources = 0;
	rig.request.options.amplitudeSweeps = 5;
	rig.request.options.amplitudeLineSteps = 18;
	rig.request.options.maximumRequiredBlockRmse = 0;
	rig.request.options.complexityPerSource = 0;
	rig.request.options.complexityPerParameter = 0;
	rig.Observations(rig.Truth(answers));
	J::Model seed;
	seed.materials = rig.request.materials;
	seed.geometryKey = rig.request.geometry.geometryKey;
	seed.transferKey = rig.request.transfer->Key();
	for (const auto &c : candidates)
		seed.sources.push_back(Answer(c, V(.4, .4, .4)));
	J::Model fit;
	J::Work work;
	Must(J::Refit(rig.request, seed, Read(), fit, work));
	f.evaluations += work.evaluations;
	f.rays += work.rays;
	J::Result input;
	input.alternatives = {fit};
	Oracle oracle(rig);
	U::Options options;
	options.positionLevels = options.colourProfiles = options.deleteBlocks = options.subsampleReplicates =
		options.restarts = 0;
	options.materialProfiles = false;
	options.trainingRegret = options.validationRegret = 0;
	if (calibrated)
	{
		options.population = calibrated->policy.population;
		options.evaluationFamily = "independent-scene-" + std::to_string(index);
	}
	U::Result result;
	Must(U::Analyze(rig.request, input, options, Read(), result, &oracle, calibrated));
	f.evaluations += result.work.evaluations;
	f.rays += result.work.rays;
	f.bytes = result.work.accountedBytes;
	bool positiveValidation = std::any_of(result.witnesses.begin(), result.witnesses.end(),
										  [](const U::Witness &w) { return w.model.validation.positive; });
	J::Score held;
	Must(J::EvaluateFrozen(rig.request, fit, rig.held, rig.held->data->size(), J::Role::Heldout,
						   Key("S12 final heldout x+/y+"), Read(), held));
	f.evaluations += held.work.evaluations;
	f.rays += held.work.rays;
	std::set<size_t> matched;
	auto assignment = Match(answers, fit.sources, .16 * f.scale);
	for (size_t i = 0; i < fit.sources.size(); ++i)
	{
		const auto &s = fit.sources[i];
		int match = assignment[i];
		bool correct = match >= 0;
		if (correct)
			matched.insert(match);
		auto t = std::find_if(result.tracks.begin(), result.tracks.end(), [&](const U::Track &a) {
			return std::find(a.candidateIds.begin(), a.candidateIds.end(), s.candidate.id) !=
				   a.candidateIds.end();
		});
		// Empty feasible set is an honest abstention, not a detection success.
		double ranking = t == result.tracks.end() ? 0 : t->removalRanking;
		auto removed = fit;
		removed.sources.erase(removed.sources.begin() + i);
		J::Model removal;
		J::Score trainingScore, without;
		J::Work removalWork;
		Must(J::AssessHypothesis(rig.request, removed, Read(), removal, trainingScore, removalWork));
		f.evaluations += removalWork.evaluations;
		f.rays += removalWork.rays;
		Must(J::EvaluateFrozen(rig.request, removal, rig.held, rig.held->data->size(), J::Role::Heldout,
							   Key("S12 final heldout x+/y+"), Read(), without));
		f.evaluations += without.work.evaluations;
		f.rays += without.work.rays;
		bool useful = held.available && without.available && held.positive &&
					  held.intervalMse < held.blackMse * .2 &&
					  without.intervalMse > held.intervalMse + .01 * held.blackMse;
		for (unsigned event = 0; event < 4; ++event)
		{
			Trial row;
			auto &c = row.calibration;
			c.family = "independent-scene-" + std::to_string(index);
			c.scene = f.geometry;
			c.trial = std::to_string(s.candidate.id);
			c.event = U::Event(event);
			c.ranking = event == 1 ? 1 : ranking;
			c.labelKnown = event != 1 || correct;
			row.calibrationEligible = t != result.tracks.end() && positiveValidation && t->removalAvailable;
			if (calibrated && t != result.tracks.end())
				row.confidence =
					event == 0 ? t->existence : event == 1 ? t->type : event == 2 ? t->exactUse : t->fieldUse;
			c.label = event == 0 || event == 1 ? correct
											   : false; // no physical-scale or exact-use oracle is invented
			if (event == 3)
				c.label = useful;
			row.fieldUseful = held.available && held.positive && held.intervalMse < held.blackMse * .2;
			row.heldMse = held.intervalMse;
			row.blackMse = held.blackMse;
			f.trials.push_back(row);
		}
	}
	f.missed = f.truth - unsigned(matched.size());
	// Joint coverage means one complete retained configuration matches EVERY true
	// source injectively under the frozen physical matching radius. Axis-wise
	// min/max intervals and the mean XYZ do not count as a feasible configuration.
	for (const auto &witness : result.witnesses)
	{
		auto matches = Match(answers, witness.model.sources, .16 * f.scale);
		double maximum = 0;
		unsigned count = 0;
		for (size_t i = 0; i < matches.size(); ++i)
			if (matches[i] >= 0)
			{
				++count;
				maximum =
					std::max(maximum, Length(V(witness.model.sources[i].value.geometry.position.world) -
											 V(answers[size_t(matches[i])].value.geometry.position.world)));
			}
		bool covered = count == answers.size();
		if (covered && !answers.empty())
		{
			f.jointCovered = true;
			f.jointDistance = maximum;
			break;
		}
	}
	return f;
}
int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::fprintf(stderr, "usage: lightrecon_uncertainty_population output.json\n");
		return 2;
	}
	try
	{
		std::vector<Family> families;
		for (unsigned i = 0; i < 32; ++i)
		{
			families.push_back(Run(i));
			std::printf("S12 population %u/48\n", i + 1);
		}
		U::CalibrationPolicy policy;
		policy.population = "S12 owned procedural planar rooms with supplied point positions v1";
		policy.protocol = "frozen match radius=.16*scene scale; independent scene draws; first32 calibration "
						  "/ last16 evaluation; bins0,.02,.2,.6,1; positive-validation admission";
		policy.binEdges = {0, .02, .2, .6, 1};
		policy.minimumFamilies = 4;
		policy.independentFamiliesAttested = true;
		std::vector<U::CalibrationTrial> calibration;
		for (const auto &f : families)
			if (f.index < 32)
				for (const auto &t : f.trials)
					if (t.calibrationEligible)
						calibration.push_back(t.calibration);
		U::CalibrationModel model;
		Must(U::Calibrate(policy, calibration, Read(), model));
		// The calibrated table now enters the production analysis stage before any
		// final-family held-out scoring. No evaluation-family outcomes train it.
		for (unsigned i = 32; i < 48; ++i)
		{
			families.push_back(Run(i, &model));
			std::printf("S12 population %u/48\n", i + 1);
		}
		std::ofstream out(argv[1]);
		out << std::setprecision(12)
			<< "{\"schema\":\"org.openjk.uncertainty-population.v1\",\"calibration_key\":\""
			<< Disk::Hex(model.key) << "\",\"population\":\"" << policy.population << "\",\"families\":[";
		for (size_t j = 0; j < families.size(); ++j)
		{
			const auto &f = families[j];
			out << (j ? "," : "") << "{\"index\":" << f.index << ",\"split\":\""
				<< (f.index < 32 ? "calibration" : "evaluation") << "\",\"geometry\":\"" << f.geometry
				<< "\",\"stratum\":" << f.stratum << ",\"scale\":" << f.scale
				<< ",\"intensity\":" << f.intensity << ",\"true_sources\":" << f.truth
				<< ",\"missed\":" << f.missed << ",\"joint_covered\":" << (f.jointCovered ? "true" : "false")
				<< ",\"joint_distance\":" << f.jointDistance << ",\"rays\":" << f.rays
				<< ",\"evaluations\":" << f.evaluations << ",\"accounted_bytes\":" << f.bytes
				<< ",\"trials\":[";
			for (size_t k = 0; k < f.trials.size(); ++k)
			{
				const auto &t = f.trials[k];
				const auto &c = t.calibration;
				const auto &confidence = t.confidence;
				size_t bin = 0;
				while (bin + 2 < policy.binEdges.size() && c.ranking >= policy.binEdges[bin + 1])
					++bin;
				out << (k ? "," : "") << "{\"candidate\":\"" << c.trial
					<< "\",\"event\":" << unsigned(c.event) << ",\"rank\":" << c.ranking << ",\"bin\":" << bin
					<< ",\"label_known\":" << (c.labelKnown ? "true" : "false")
					<< ",\"calibration_eligible\":" << (t.calibrationEligible ? "true" : "false")
					<< ",\"label\":" << (c.label ? "true" : "false") << ",\"heldout_mse\":" << t.heldMse
					<< ",\"black_mse\":" << t.blackMse
					<< ",\"rate_known\":" << (confidence.probability.known ? "true" : "false")
					<< ",\"rate\":" << confidence.probability.value
					<< ",\"lower\":" << confidence.probability.lower
					<< ",\"upper\":" << confidence.probability.upper
					<< ",\"independent_families\":" << confidence.independentFamilies << "}";
			}
			out << "]}";
		}
		out << "],\"bins\":[";
		for (size_t i = 0; i < model.bins.size(); ++i)
		{
			const auto &b = model.bins[i];
			out << (i ? "," : "") << "{\"event\":" << unsigned(b.event) << ",\"bin\":" << b.bin
				<< ",\"families\":" << b.families << ",\"rows\":" << b.rows << ",\"unknown\":" << b.unlabelled
				<< ",\"rate\":" << b.rate << ",\"lower\":" << b.lower << ",\"upper\":" << b.upper << "}";
		}
		out << "]}\n";
		return out ? 0 : 1;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "S12 population failure: %s\n", e.what());
		return 1;
	}
}
