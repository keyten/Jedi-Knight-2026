// Deterministic owned ASR20 witnesses; historical/compiled bakes are tested separately.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "aesthetic_fit.h"
#include "joint_test_scene.h"
#include "lightrecon/shape_selection.h"
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace JointTest;
namespace A = Rend2LightRecon::Aesthetic;
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
#include "aesthetic_test_scene.h"
void ScoreJson(std::ostream &o, const A::Score &s)
{
	o << "{\"available\":" << s.available << ",\"robust\":" << s.robust << ",\"mse\":" << s.mse
	  << '}';
}
void Run(std::ostream &out, const char *name, AestheticRig &rig, double ratio = .2)
{
	auto experiment = rig.Fit();
	std::vector<A::Score> scores;
	A::Score base;
	Must(A::AuditHeldout(experiment, rig.folds[2], Read(), scores, base));
	CHECK(experiment.Selected() != SIZE_MAX);
	CHECK(base.available);
	if (experiment.Selected() == SIZE_MAX)
		return;
	CHECK(scores[experiment.Selected()].available);
	CHECK(scores[experiment.Selected()].robust < base.robust * ratio);
	const auto &selected = experiment.Fits()[experiment.Selected()];
	for (const auto &s : selected.sources)
	{
		CHECK(Positive(s.relativePower));
		CHECK(s.emitter.geometry.source.state == HypothesisState::Candidate);
	}
	out << "{\"name\":\"" << name << "\",\"selected\":" << experiment.Selected()
		<< ",\"work\":" << experiment.Work() << ",\"baseline\":";
	ScoreJson(out, base);
	out << ",\"configurations\":[";
	for (size_t i = 0; i < scores.size(); ++i)
	{
		if (i)
			out << ',';
		out << "{\"name\":\"" << experiment.Fits()[i].name << "\",\"heldout\":";
		ScoreJson(out, scores[i]);
		out << '}';
	}
	out << "],\"sources\":[";
	for (size_t i = 0; i < selected.sources.size(); ++i)
	{
		const auto &s = selected.sources[i];
		if (i)
			out << ',';
		out << "{\"fixture\":" << s.fixture << ",\"variant\":" << s.variant << ",\"relative_rgb\":["
			<< s.relativePower.x << ',' << s.relativePower.y << ',' << s.relativePower.z
			<< "],\"correlation\":" << s.maximumColumnCorrelation
			<< ",\"direction_constrained\":" << s.directionConstrained << '}';
	}
	out << "]}";
}
int main(int argc, char **argv)
{
	try
	{
		std::ostringstream output;
		output << std::setprecision(12)
			   << "{\"schema\":\"org.openjk.aesthetic-owned.v1\",\"cases\":[";
		AestheticRig single;
		single.Fixture(1, V(-1, 0, 1.2));
		single.request.configurations = {{"single", {0}}};
		single.Data({Vec3(3, 0, 1)});
		Run(output, "single-zero-green", single, .02);
		auto clean = single.Fit();
		AestheticRig equivalent = single;
		equivalent.request.configurations.push_back(equivalent.request.configurations[0]);
		auto alternatives = equivalent.Fit();
		CHECK(alternatives.Selected() == 0);
		CHECK(alternatives.ValidationAlternatives().size() == 2);
		CHECK(clean.Fits()[0].sources[0].relativePower.y < .002);
		CHECK(std::abs(clean.Fits()[0].sources[0].relativePower.x - 3) < .002);
		CHECK(std::abs(clean.Fits()[0].background.x - .03) < .002);
		AestheticRig multiple;
		multiple.Fixture(1, V(-1.4, -.8, 1.2));
		multiple.Fixture(2, V(1.4, .8, 1.1));
		multiple.Fixture(3, V(0, -1.7, 1.3));
		multiple.request.configurations = {
			{"single", {0}}, {"dual", {0, 1}}, {"multiple", {0, 1, 2}}};
		multiple.Data({Vec3(3, .2, .1), Vec3(.1, .4, 4), Vec3(.1, 2, .2)}, Vec3(.03, .02, .01),
					  true);
		output << ',';
		Run(output, "three-colour-with-outlier", multiple, .15);
		CHECK(multiple.Fit().Selected() == 2);
		AestheticRig panel;
		panel.MountedPanel();
		panel.request.configurations = {{"mounted-panel", {0}}};
		panel.Data({Vec3(2, 1, .2)});
		output << ',';
		Run(output, "mounted-area", panel, .05);
		AestheticRig gauge = single;
		gauge.request.targets.gain = 10;
		gauge.request.fixtures[0].upperPower = Vec3(1.2, 1.2, 1.2);
		gauge.request.options.backgroundUpper = Vec3(.008, .008, .008);
		auto scaled = gauge.Fit();
		CHECK(std::abs(scaled.Fits()[0].sources[0].relativePower.x * 10 -
					   clean.Fits()[0].sources[0].relativePower.x) < .01);
		AestheticRig encoded;
		encoded.Fixture(1, V(-1, 0, 1.2));
		encoded.request.configurations = {{"single", {0}}};
		encoded.request.targets.domain = A::Domain::ForwardEncodedRgb8;
		encoded.request.targets.gain = 80;
		Bake::Recipe recipe;
		recipe.gamma = 2;
		recipe.srgbOutput = true;
		encoded.request.targets.encoding = Bake::NetRadiantProfile(recipe);
		encoded.request.options.huber = 4;
		encoded.Data({Vec3(3, 1, .2)});
		output << ',';
		Run(output, "forward-encoded", encoded, .1);
		AestheticRig spot;
		spot.Fixture(4, V(0, 0, 1.2), true);
		spot.request.configurations = {{"spot", {0}}};
		auto &f = spot.request.fixtures[0];
		f.maximumAxisRadians = .4;
		f.minimumOuterRadians = .5;
		f.maximumOuterRadians = 1.2;
		f.directionProvenance = "Owned hood bounds fixed before receiver targets";
		auto truth = f.variants[0];
		double a = .3;
		truth.geometry.frame.right = Vec3(float(std::cos(a)), 0, float(std::sin(a)));
		truth.geometry.frame.up = Vec3(0, -1, 0);
		truth.geometry.frame.forward = Vec3(float(std::sin(a)), 0, float(-std::cos(a)));
		truth.geometry.angular.cosInner = std::cos(.5);
		truth.geometry.angular.cosOuter = std::cos(.85);
		f.variants.push_back(truth);
		spot.Data({Vec3(3, 2, 1)});
		output << ',';
		Run(output, "bounded-spot-axis-cone", spot, .05);
		auto directional = spot.Fit();
		CHECK(directional.Fits()[0].sources[0].variant == 1);
		CHECK(directional.Fits()[0].sources[0].directionConstrained);
		// Area/normal/block balancing is invariant to duplicating represented area.
		AestheticRig split = single;
		auto row = split.folds[0][0];
		split.folds[0][0].area = .5;
		row.area = .5;
		row.id = 999;
		row.receiver.sample = 999;
		split.folds[0].push_back(row);
		auto balanced = split.Fit();
		CHECK(std::abs(balanced.Fits()[0].sources[0].relativePower.x -
					   clean.Fits()[0].sources[0].relativePower.x) < 1e-4);
		AestheticRig weight = single;
		weight.request.fixtures.clear();
		weight.request.configurations = {{"background-only", {}}};
		weight.request.options.backgroundUpper = Vec3();
		weight.request.options.huber = 100;
		for (unsigned role = 0; role < 3; ++role)
		{
			weight.folds[role].resize(4);
			for (unsigned i = 0; i < 4; ++i)
			{
				auto &a = weight.folds[role][i];
				a.spatialBlock = 1 + role * 100 + (i < 2 ? 0 : i - 1);
				a.area = i == 1 ? 3 : 1;
				a.receiver.geometricNormal = a.receiver.shadingNormal =
					i == 3 ? Vec3(1, 0, 0) : Vec3(0, 0, 1);
				for (auto &c : a.channel)
					c.lower = c.upper = i == 1 ? 9 : i == 3 ? 10 : 1;
			}
		}
		CHECK(std::abs(weight.Fit().Baseline().training.mse - 65.5) < 1e-8);
		// Leakage, unknown transfer/material, signed power bounds, stale supports and budgets fail
		// closed.
		A::Experiment rejected;
		auto bad = single.request;
		bad.targets.domain = A::Domain::Unknown;
		CHECK(A::FitAndValidate(bad, single.folds[0], single.folds[1], Read(), rejected).code ==
			  Code::Unavailable);
		bad = single.request;
		bad.targets.comparableMaterialResponseAttested = false;
		CHECK(!A::FitAndValidate(bad, single.folds[0], single.folds[1], Read(), rejected));
		bad.targets.sameMaterialResponseAssumed = true;
		CHECK(!A::FitAndValidate(bad, single.folds[0], single.folds[1], Read(), rejected));
		bad = encoded.request;
		bad.targets.encoding = {};
		CHECK(!A::FitAndValidate(bad, encoded.folds[0], encoded.folds[1], Read(), rejected));
		bad = single.request;
		bad.fixtures[0].upperPower.x = -1;
		CHECK(!A::FitAndValidate(bad, single.folds[0], single.folds[1], Read(), rejected));
		auto leaked = single.folds[1];
		leaked[0].spatialBlock = single.folds[0][0].spatialBlock;
		CHECK(A::FitAndValidate(single.request, single.folds[0], leaked, Read(), rejected).code ==
			  Code::InvalidInput);
		std::vector<A::Score> held;
		A::Score baseline;
		auto hidden = single.folds[2];
		hidden[0].id = single.folds[0][0].id;
		hidden[0].receiver.sample = hidden[0].id;
		CHECK(!A::AuditHeldout(clean, hidden, Read(), held, baseline));
		CHECK(held.empty());
		bad = spot.request;
		bad.fixtures[0].directionProvenance.clear();
		CHECK(!A::FitAndValidate(bad, spot.folds[0], spot.folds[1], Read(), rejected));
		bad = single.request;
		bad.options.maxWork = 1;
		CHECK(A::FitAndValidate(bad, single.folds[0], single.folds[1], Read(), rejected).code ==
			  Code::WorkLimit);
		bad = single.request;
		bad.options.maxBytes = 1024;
		CHECK(A::FitAndValidate(bad, single.folds[0], single.folds[1], Read(), rejected).code ==
			  Code::MemoryLimit);
		bad = single.request;
		bad.fixtures[0].variants[0].support.map = {};
		CHECK(!A::FitAndValidate(bad, single.folds[0], single.folds[1], Read(), rejected));
		struct CancelledRead : Cancellation
		{
			bool IsCancelled() const noexcept override
			{
				return true;
			}
		} cancellation;
		auto read = Read();
		read.cancellation = &cancellation;
		CHECK(A::FitAndValidate(single.request, single.folds[0], single.folds[1], read, rejected)
				  .code == Code::Cancelled);
		// Constant-only targets retain the fitted nuisance baseline, not a fictitious lamp.
		AestheticRig dark = single;
		for (auto &fold : dark.folds)
			for (auto &row : fold)
				for (auto &v : row.channel)
					v.lower = v.upper = .02;
		CHECK(dark.Fit().Selected() == SIZE_MAX);
		AestheticRig unknown = single;
		unknown.scene = Scene(true, false, false, false, true);
		unknown.request.scene.geometry = unknown.scene.visibility;
		unknown.request.scene.map = unknown.scene.map;
		unknown.request.geometry.visibility = unknown.scene.visibility;
		unknown.request.geometry.geometryKey = unknown.scene.visibility->CertificationKey();
		unknown.request.fixtures[0].variants[0].support.map = unknown.scene.map.sha256;
		unknown.request.fixtures[0].variants[0].support.geometry =
			unknown.request.geometry.geometryKey;
		auto unavailable = unknown.Fit();
		CHECK(!unavailable.Fits()[0].available);
		CHECK(unavailable.Selected() == SIZE_MAX);
		AestheticRig bound = single;
		bound.request.fixtures[0].upperPower = Vec3(1, 1, 1);
		CHECK(bound.Fit().Fits()[0].sources[0].atPowerBound);
		AestheticRig correlated = single;
		correlated.request.fixtures.push_back(correlated.request.fixtures[0]);
		correlated.request.fixtures[1].variants[0].geometry.source.id = 2;
		correlated.request.fixtures[1].variants[0].support.fixture = 2;
		correlated.request.configurations = {{"collinear", {0, 1}}};
		CHECK(correlated.Fit().Fits()[0].sources[0].maximumColumnCorrelation > .9999);
		auto before = clean.Fits()[0].sources[0].relativePower;
		for (auto &a : single.folds[2])
			for (auto &v : a.channel)
				v.lower = v.upper = 1000;
		Must(A::AuditHeldout(clean, single.folds[2], Read(), held, baseline));
		CHECK(clean.Selected() == 0);
		CHECK(clean.Fits()[0].sources[0].relativePower.x == before.x);
		output << "],\"checks\":" << checks << ",\"failures\":" << failures
			   << ",\"historical_source_truth\":\"Unavailable\",\"absolute_scale_known\":false}";
		if (argc == 2)
		{
			std::ofstream file(argv[1]);
			file << output.str() << '\n';
			if (!file)
				throw std::runtime_error("Owned report write failed");
		}
		std::printf("ASR20: %d checks, %d failures\n", checks, failures);
		return failures ? 1 : 0;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "ASR20 exception: %s\n", e.what());
		return 2;
	}
}
