// S11 trusted fixture/evaluation controller. No source answers enter Joint::Solve.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_fixture.h"
#include "lightrecon/shadow_evidence.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace JointFixture;
namespace J = Rend2LightRecon::Core::Joint;
namespace S = Rend2LightRecon::Core::Shape;
namespace I = Rend2LightRecon::Core::Indirect;
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
#include "joint_test_scene.h"
using namespace JointTest;
void GeometryJson(std::ostream &out, const J::Source &s)
{
	const auto &g = s.value.geometry;
	if (s.candidate.root == TransportRootKind::Sun)
	{
		out << ",\"direction\":[" << -s.environment.orientation.forward.x << ','
			<< -s.environment.orientation.forward.y << ',' << -s.environment.orientation.forward.z
			<< "],\"direction_convention\":\"receiver-to-source\"";
	}
	else if ((g.kind == EmitterKind::Spot || g.kind == EmitterKind::Rectangle ||
			  g.kind == EmitterKind::Tube) &&
			 g.frame.known)
	{
		Vec3 axis = g.kind == EmitterKind::Tube ? g.frame.right : g.frame.forward;
		out << ",\"direction\":[" << axis.x << ',' << axis.y << ',' << axis.z
			<< "],\"direction_convention\":\""
			<< (g.kind == EmitterKind::Tube ? "tube-axis" : "emission-propagation") << "\"";
		if (g.kind == EmitterKind::Spot && g.angular.kind == AngularKind::SmoothCone)
			out << ",\"cone_degrees\":" << std::acos(g.angular.cosOuter) * 180 / Pi;
		if (g.kind == EmitterKind::Rectangle)
			out << ",\"dimensions\":[" << 2 * g.geometry.halfWidthWorld << ','
				<< 2 * g.geometry.halfHeightWorld << "],\"tangent\":[" << g.frame.right.x << ','
				<< g.frame.right.y << ',' << g.frame.right.z << ']';
		if (g.kind == EmitterKind::Tube)
			out << ",\"dimensions\":[" << 2 * g.geometry.halfLengthWorld << ',' << g.geometry.radiusWorld
				<< ']';
	}
}
class Oracle final : public J::Validation
{
	const J::Request &geometry;
	std::shared_ptr<Stream<J::Observation>> data;

  public:
	Oracle(const Rig &r) : geometry(r.request), data(r.validation) {}
	Disk::Digest Key() const override { return JointFixture::Key("private validation fold v1"); }
	Error Evaluate(const J::Model &model, const ReadContext &read, J::Score &score) const override
	{
		return J::EvaluateFrozen(geometry, model, data, data->data->size(), J::Role::Validation, Key(), read,
								 score);
	}
};
std::vector<std::string> rows;
J::Result Run(Rig &rig, const std::string &name, const std::vector<J::Source> &truth, bool expectGood = true)
{
	rig.Observations(rig.Truth(truth),
					 rig.request.profiles[0].contract.operation == I::ObservationOperator::NetRadiantEncoded);
	Oracle oracle(rig);
	J::Result result;
	auto error = J::Solve(rig.request, Read(), result, &oracle);
	if (!error)
		std::printf("%s error %d: %s\n", name.c_str(), int(error.code), error.message.c_str());
	CHECK(error);
	CHECK(result.preferred != SIZE_MAX || !expectGood);
	if (result.preferred == SIZE_MAX)
	{
		for (const auto &attempt : result.attempts)
			std::printf("%s attempt %s: %s train=%g validation=%g\n", name.c_str(), attempt.proposal.c_str(),
						attempt.reason.c_str(), attempt.training, attempt.validation);
		for (const auto &m : result.checkpoint.completed)
			std::printf("  sources=%zu required=%d positive=%d mse=%g black=%g worst=%g\n", m.sources.size(),
						m.validation.requiredBlocksPass, m.validation.positive, m.validation.intervalMse,
						m.validation.blackMse, m.validation.worstBlockRmse);
		std::ostringstream row;
		row << "{\"name\":\"" << name << "\",\"state\":\"" << J::StateName(result.state)
			<< "\",\"prediction_available\":false,\"models\":" << result.work.attemptedModels << "}";
		rows.push_back(row.str());
		return result;
	}
	const auto &best = result.alternatives[result.preferred];
	J::Score held;
	std::vector<J::Prediction> predictions;
	Must(J::EvaluateFrozen(rig.request, best, rig.held, rig.held->data->size(), J::Role::Heldout,
						   Key("private held-out fold v1"), Read(), held, &predictions));
	CHECK(held.available);
	CHECK(!best.physicalExistenceCertified);
	CHECK(best.strictReevaluated);
	CHECK(!best.localAmplitudeSpectrum.available ||
		  best.localAmplitudeSpectrum.rank <= best.localAmplitudeSpectrum.parameters);
	if (expectGood)
	{
		CHECK(held.intervalMse < held.blackMse * .02);
		CHECK(held.intervalMse <
			  (rig.request.profiles[0].contract.operation == I::ObservationOperator::NetRadiantEncoded
				   ? 2.25
				   : .0005));
	}
	std::ostringstream row;
	row << std::setprecision(12) << "{\"name\":\"" << name << "\",\"state\":\"" << J::StateName(result.state)
		<< "\",\"heldout_mse\":" << held.intervalMse << ",\"black_mse\":" << held.blackMse
		<< ",\"train_objective\":" << best.training.Total()
		<< ",\"validation_objective\":" << best.validation.objective
		<< ",\"models\":" << result.work.attemptedModels << ",\"evaluations\":" << result.work.evaluations
		<< ",\"rays\":" << result.work.rays << ",\"pairs\":" << result.work.pairs
		<< ",\"cache_hits\":" << result.work.cacheHits << ",\"preferred_sources\":" << best.sources.size()
		<< ",\"alternatives\":" << result.alternatives.size()
		<< ",\"local_amplitude_rank\":" << best.localAmplitudeSpectrum.rank
		<< ",\"local_amplitude_parameters\":" << best.localAmplitudeSpectrum.parameters
		<< ",\"linear_amplitudes\":" << (best.linearAmplitudeBlock ? "true" : "false")
		<< ",\"physical_existence_certified\":false,\"sources\":[";
	for (size_t i = 0; i < best.sources.size(); ++i)
	{
		const auto &s = best.sources[i];
		if (i)
			row << ',';
		row << "{\"id\":" << s.candidate.id << ",\"kind\":" << unsigned(s.value.geometry.kind)
			<< ",\"root\":" << unsigned(s.candidate.root) << ",\"position\":["
			<< s.value.geometry.position.world.x << ',' << s.value.geometry.position.world.y << ','
			<< s.value.geometry.position.world.z << "],\"rgb\":[" << s.value.rgbAmplitude.x << ','
			<< s.value.rgbAmplitude.y << ',' << s.value.rgbAmplitude.z << "]";
		GeometryJson(row, s);
		row << '}';
	}
	row << "],\"truth\":[";
	for (size_t i = 0; i < truth.size(); ++i)
	{
		if (i)
			row << ',';
		const auto &s = truth[i];
		row << "{\"kind\":" << unsigned(s.value.geometry.kind) << ",\"root\":" << unsigned(s.candidate.root)
			<< ",\"position\":[" << s.value.geometry.position.world.x << ','
			<< s.value.geometry.position.world.y << ',' << s.value.geometry.position.world.z << "],\"rgb\":["
			<< s.value.rgbAmplitude.x << ',' << s.value.rgbAmplitude.y << ',' << s.value.rgbAmplitude.z
			<< "]";
		GeometryJson(row, s);
		row << '}';
	}
	row << "]}";
	rows.push_back(row.str());
	std::printf("%s: %s sources=%zu alternatives=%zu held=%g black=%g models=%llu eval=%llu\n", name.c_str(),
				J::StateName(result.state), best.sources.size(), result.alternatives.size(), held.intervalMse,
				held.blackMse, (unsigned long long)result.work.attemptedModels,
				(unsigned long long)result.work.evaluations);
	return result;
}
void Basic()
{
	Rig rig;
	auto a = Point(10, V(-.65, -.1, .8)), b = Point(20, V(.7, .15, 1.1)), wrong = Point(30, V(1.1, 1.1, .4));
	rig.request.candidates = {wrong, b, a};
	rig.request.options.coarseRows = 10;
	auto result = Run(rig, "two_colour", {Answer(a, V(3.3, .1, .2)), Answer(b, V(.1, .3, 2.1))});
	if (result.preferred != SIZE_MAX)
	{
		CHECK(result.alternatives[result.preferred].sources.size() == 2);
	}
}
void SameColour()
{
	Rig rig;
	auto a = Point(11, V(-.7, 0, .8)), b = Point(21, V(.7, 0, .8));
	rig.request.candidates = {a, b};
	auto result = Run(rig, "same_colour", {Answer(a, V(2.3, 2.3, 2.3)), Answer(b, V(1.7, 1.7, 1.7))});
	if (result.preferred != SIZE_MAX)
		CHECK(result.alternatives[result.preferred].sources.size() == 2);
}
void ScaleGauge()
{
	Rig rig;
	auto c = Point(10, V(0, 0, 1));
	rig.request.candidates = {c};
	auto alternative = rig.request.profiles[0];
	alternative.fixedGain = 2;
	alternative.calibrationHypothesis = "Explicit competing relative gain=2, no absolute reference";
	rig.request.profiles.push_back(alternative);
	auto result = Run(rig, "unknown_absolute_scale", {Answer(c, V(2.4, .8, 1.2))});
	bool first = false, second = false;
	for (const auto &model : result.alternatives)
	{
		first |= model.profile == 0;
		second |= model.profile == 1;
		CHECK(!model.absoluteScaleKnown);
	}
	CHECK(first && second);
	CHECK(result.state == J::State::Unidentifiable);
}
void Mixed()
{
	Rig rig;
	auto a = Point(10, V(-.8, -.1, .6)), b = Extended(20, EmitterKind::Spot, V(.5, 0, 1.1)),
		 c = Extended(30, EmitterKind::Rectangle, V(-.1, 0, 1.25));
	rig.request.candidates = {a, b, c};
	rig.request.options.rounds = 3;
	rig.request.options.beamWidth = 3;
	Run(rig, "point_spot_area",
		{Answer(a, V(1.7, .2, .1)), Answer(b, V(.1, 1.8, .1)), Answer(c, V(.1, .1, 4.1))});
}
void Adjacent()
{
	Rig rig;
	auto a = Extended(10, EmitterKind::Rectangle, V(-.5, 0, 1.15)),
		 b = Extended(20, EmitterKind::Rectangle, V(.5, 0, 1.15));
	rig.request.candidates = {a, b};
	auto result = Run(rig, "adjacent_panels", {Answer(a, V(4.2, .2, .2)), Answer(b, V(.2, .2, 3.3))});
	if (result.preferred != SIZE_MAX)
		CHECK(result.alternatives[result.preferred].sources.size() == 2);
}
void Tube()
{
	Rig rig;
	auto tube = Extended(10, EmitterKind::Tube, V(0, 0, 1.15)), point = Point(20, V(0, 0, 1.15));
	tube.family = point.family = 1;
	rig.request.candidates = {tube, point};
	Run(rig, "tube_type_alternative", {Answer(tube, V(4.2, 2.1, .5))});
}
void Bounce()
{
	Rig rig;
	rig.request.materials[0] = KnownMaterial(1, V(.7, .18, .08));
	rig.request.profiles[0].contract.physicalTransportAssumed = true;
	auto a = Point(10, V(-.7, -.1, .8)), b = Point(20, V(.7, 0, 1.1));
	rig.request.candidates = {a, b};
	auto result = Run(rig, "red_reflector", {Answer(a, V(3.2, 3.2, 3.2))});
	if (result.preferred != SIZE_MAX)
	{
		const auto &best = result.alternatives[result.preferred];
		CHECK(best.roots.size() == 1);
		CHECK(best.roots[0].firstBounceAreaWeighted > 0);
		CHECK(best.reflectedPowerFraction <= 1);
	}
}
void Weak()
{
	Rig rig;
	auto a = Point(10, V(-.75, 0, .8)), b = Point(20, V(.85, 0, .45));
	a.shape.upperRgb = Vec3(16, 16, 16);
	b.shape.upperRgb = Vec3(1, 1, 1);
	rig.request.candidates = {a, b};
	rig.request.options.equivalentValidationSlack = 1e-7;
	rig.request.options.maximumRequiredBlockRmse = .003;
	auto result = Run(rig, "bright_plus_weak", {Answer(a, V(12, 0, 0)), Answer(b, V(0, 0, .055))});
	if (result.preferred != SIZE_MAX)
		CHECK(result.alternatives[result.preferred].sources.size() == 2);
}
void Ambiguous()
{
	Rig rig;
	auto a = Point(10, V(0, 0, 1), true), b = Point(20, V(0, 0, 1), true);
	rig.request.candidates = {a, b};
	rig.request.options.equivalentValidationSlack = 1e-5;
	rig.request.options.mergeSplit = true;
	rig.request.options.rounds = 1;
	rig.request.options.maxModels = 24;
	auto result = Run(rig, "coincident_count_ambiguity", {Answer(a, V(2, 2, 2)), Answer(b, V(1, 1, 1))});
	bool one = false, two = false;
	for (const auto &m : result.alternatives)
	{
		one |= m.sources.size() == 1;
		two |= m.sources.size() == 2;
		CHECK(!m.sourceAllocationIdentifiable);
	}
	CHECK(one && two);
	CHECK(result.state == J::State::Unidentifiable);
}
void Dark()
{
	Rig rig;
	rig.request.candidates = {Point(10, V(0, 0, 1))};
	auto result = Run(rig, "zero_signal", {}, false);
	CHECK(result.state == J::State::InsufficientSignal);
	CHECK(result.preferred == SIZE_MAX);
}
void Ambient()
{
	Rig rig;
	I::Root ambient;
	ambient.source = 900;
	ambient.kind = TransportRootKind::LegacyAmbient;
	ambient.generation = Key("identified ambient boundary");
	ambient.provenance = "Authored analytical ambient control, not inferred GI";
	ambient.incident.assign(rig.request.transfer->Patches().size(), Vec3(.2, .3, .4));
	ambient.available.assign(ambient.incident.size(), 1);
	rig.request.boundaryRoots = {ambient};
	rig.request.candidates = {Point(10, V(0, 0, 1))};
	auto result = Run(rig, "ambient_no_fake_emitter", {});
	if (result.preferred != SIZE_MAX)
		CHECK(result.alternatives[result.preferred].sources.empty());
}
void Geometry()
{
	Rig rig;
	auto answer = Point(10, V(.1, 0, .9)), start = answer;
	start.shape.initial.geometry.position.world = Vec3(.4, 0, .9);
	start.shape.fitPosition = true;
	rig.request.candidates = {start};
	rig.request.options.geometry = true;
	rig.request.options.geometrySweeps = 2;
	rig.request.options.amplitudeSweeps = 3;
	rig.request.options.positionStepWorld = .2;
	auto result = Run(rig, "joint_position_refinement", {Answer(answer, V(2.8, .4, 1.2))});
	if (result.preferred != SIZE_MAX)
	{
		double after =
			Length(V(result.alternatives[result.preferred].sources[0].value.geometry.position.world) -
				   V(answer.shape.initial.geometry.position.world));
		CHECK(after < .2);
	}
}
void ShapeRefinement()
{
	for (auto kind : {EmitterKind::Spot, EmitterKind::Rectangle, EmitterKind::Tube})
	{
		Rig rig;
		auto truth = Extended(10, kind, V(0, 0, 1.15)), start = truth;
		if (kind == EmitterKind::Spot)
		{
			start.shape.initial.geometry.frame.forward = Vec3(.1f, 0, -std::sqrt(.99f));
			start.shape.initial.geometry.frame.right = Vec3(std::sqrt(.99f), 0, .1f);
			start.shape.initial.geometry.frame.up = Vec3(0, -1, 0);
			start.shape.fitFrame = true;
			start.shape.fitCone = false; // explicitly known fixture cone, separate ablation below
		}
		else
		{
			start.shape.fitDimensions = true;
			if (kind == EmitterKind::Rectangle)
				start.shape.initial.geometry.geometry.halfWidthWorld *= .85f;
			else
				start.shape.initial.geometry.geometry.halfLengthWorld *= .85f;
		}
		rig.request.candidates = {start};
		rig.request.options.geometry = true;
		rig.request.options.geometrySweeps = 2;
		rig.request.options.amplitudeSweeps = 3;
		rig.request.options.positionStepWorld = .1;
		rig.request.options.angularStepRadians = .05;
		rig.request.options.logDimensionStep = .08;
		auto name = kind == EmitterKind::Spot
						? "joint_spot_axis_known_cone"
						: kind == EmitterKind::Rectangle ? "joint_panel_dimensions" : "joint_tube_dimensions";
		auto result = Run(rig, name, {Answer(truth, V(3.2, 1.1, .5))});
		if (result.preferred != SIZE_MAX)
		{
			const auto &p = result.alternatives[result.preferred].sources[0].value.geometry;
			CHECK(p.geometry.halfWidthWorld > 0 && p.geometry.halfLengthWorld > 0);
			if (kind == EmitterKind::Spot)
				CHECK(p.angular.cosInner > p.angular.cosOuter);
		}
		if (kind == EmitterKind::Spot)
		{
			rig.request.candidates[0].shape.fitCone = true;
			auto free = Run(rig, "free_spot_axis_cone_validation", {Answer(truth, V(3.2, 1.1, .5))}, false);
			CHECK(free.preferred != SIZE_MAX || free.state == J::State::Unavailable ||
				  free.state == J::State::InsufficientSignal);
		}
	}
}
void Missing()
{
	Rig rig;
	auto trueSource = Point(99, V(0, 0, 1)), wrong = Point(10, V(1.1, .9, .4), true);
	rig.request.candidates = {wrong};
	rig.request.sourceSearchBounds = Box(V(-.7, -.5, .7), V(.7, .5, 1.3));
	rig.request.options.residualSearch = true;
	rig.request.options.residualCandidates = 3;
	rig.request.options.residualGridDivisions = 3;
	rig.request.options.rounds = 2;
	rig.request.options.maxModels = 32;
	rig.request.options.geometry = false;
	auto result = Run(rig, "missing_initial_candidate", {Answer(trueSource, V(2.7, .4, 1.1))});
	if (result.preferred != SIZE_MAX)
	{
		bool added = false;
		for (const auto &s : result.alternatives[result.preferred].sources)
			added |= s.candidate.provider == "joint-training-residual-grid";
		CHECK(added);
	}
}
void ProfileAlternatives()
{
	Rig rig;
	Bake::Recipe recipe;
	recipe.fast = false;
	Historical(rig, recipe);
	auto a = CompilerPoint(10, V(-35, 0, 60));
	rig.request.candidates = {a};
	auto wrong = rig.request.profiles[0];
	recipe.falloff = Bake::Falloff::Linear;
	wrong.model.profile = Bake::NetRadiantProfile(recipe);
	wrong.contract.profile = wrong.model.profile;
	rig.request.profiles.push_back(wrong);
	auto result = Run(rig, "known_profile_alternative", {Answer(a, V(35, 12, 21))});
	if (result.preferred != SIZE_MAX)
		CHECK(result.alternatives[result.preferred].profile == 0);
}
void Saturated()
{
	Rig rig;
	Bake::Recipe recipe;
	recipe.fast = false;
	Historical(rig, recipe);
	auto a = CompilerPoint(10, V(0, 0, 40));
	rig.request.candidates = {a};
	auto result = Run(rig, "censored_ldr", {Answer(a, V(400, 300, 200))});
	if (result.preferred != SIZE_MAX)
		CHECK(!result.alternatives[result.preferred].absoluteScaleKnown);
}
void Sun()
{
	Rig rig;
	Bake::Recipe recipe;
	recipe.fast = false;
	rig.scene.Quad(V(-4, -4, 3), V(4, -4, 3), V(4, 4, 3), V(-4, 4, 3), MaterialSky);
	Rebuild(rig.scene);
	Historical(rig, recipe);
	auto point = CompilerPoint(10, V(-40, -10, 45)), sun = CompilerPoint(20, V(0, 0, 0));
	sun.root = TransportRootKind::Sun;
	sun.environment.id = sun.id;
	sun.environment.kind = Bake::SourceKind::Directional;
	sun.environment.orientation = FrameDown();
	sun.shape.upperRgb = Vec3(300, 300, 300);
	rig.request.candidates = {point, sun};
	Run(rig, "local_and_sun", {Answer(point, V(35, 4, 8)), Answer(sun, V(30, 35, 40))});
}
void UnknownMaterial()
{
	Rig rig;
	rig.request.materials[0] = KnownMaterial(1, V(.7, .18, .08));
	rig.request.profiles[0].contract.physicalTransportAssumed = true;
	auto a = Point(10, V(-.7, -.1, .8));
	auto truth = rig.Truth({Answer(a, V(3, 3, 3))});
	rig.request.materials[0].knowledge = I::MaterialKnowledge::Unknown;
	rig.request.materials[0].lower = Vec3();
	rig.request.materials[0].upper = Vec3(.9, .9, .9);
	rig.request.materials[0].initial = Vec3(.2, .2, .2);
	rig.request.options.conditionalUnknownMaterials = true;
	rig.request.options.materials = true;
	rig.request.candidates = {a};
	rig.Observations(truth);
	Oracle oracle(rig);
	J::Result out;
	Must(J::Solve(rig.request, Read(), out, &oracle));
	CHECK(out.preferred != SIZE_MAX);
	if (out.preferred != SIZE_MAX)
	{
		const auto &m = out.alternatives[out.preferred];
		CHECK(!m.sourceAllocationIdentifiable);
		CHECK(!m.physicalExistenceCertified);
		CHECK(m.materials[0].initial.x <= .9f);
	}
}
void Auxiliary()
{
	Rig rig;
	Bake::Recipe recipe;
	recipe.fast = false;
	Historical(rig, recipe);
	auto c = CompilerPoint(10, V(10, 0, 65));
	rig.request.candidates = {c};
	auto answer = Answer(c, V(45, 24, 32));
	rig.Observations(rig.Truth({answer}), true);
	auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>(1);
	auto &s = (*sources)[0];
	s.id = c.id;
	s.kind = Bake::SourceKind::Point;
	s.position = answer.value.geometry.position;
	s.compilerRgbAmplitude = answer.value.rgbAmplitude;
	s.amplitudeKnown = true;
	uint64_t id = 10000;
	for (auto role : {J::Role::Training, J::Role::Validation, J::Role::Heldout})
		for (auto type : {J::Channel::GridAmbient, J::Channel::GridDirected, J::Channel::GridDirection,
						  J::Channel::DeluxeDirection})
		{
			J::Observation o;
			o.id = id++;
			o.role = role;
			o.channel = type;
			o.region = 1;
			o.block = 100 + unsigned(type);
			o.comparable = true;
			o.measure = 1;
			o.sigma = 5;
			o.angularSigmaRadians = .05;
			o.receiver =
				rig.request.transfer
					->Patches()[role == J::Role::Training ? 2 : role == J::Role::Validation ? 18 : 20]
					.receiver;
			if (type != J::Channel::DeluxeDirection)
				o.receiver.world.z = 20;
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>(1, o.receiver);
			Bake::Options opt;
			opt.output = type == J::Channel::DeluxeDirection ? Bake::Domain::DeluxeWorldDirection
															 : Bake::Domain::GridAmbientDirected;
			opt.allowAssumed = true;
			opt.unpackedGridOnly = true;
			Bake::CpuForwardEvaluator evaluator;
			Bake::Result predicted;
			Must(evaluator.Evaluate(rig.request.profiles[0].model.profile, Lease(sources), Lease(receivers),
									{}, rig.request.geometry, opt, Read(), predicted));
			CHECK(predicted.predictions.count == 1 && predicted.predictions.data[0].available);
			const auto &p = predicted.predictions.data[0];
			Vec3 rgb = type == J::Channel::GridAmbient ? p.gridAmbient : p.gridDirected;
			for (unsigned channel = 0; channel < 3; ++channel)
				o.rgb[channel].lower = o.rgb[channel].upper = V(rgb)[channel];
			o.directionKnown = p.directionKnown;
			o.directionToSource = p.directionToSource;
			auto stream = role == J::Role::Training ? rig.train
													: role == J::Role::Validation ? rig.validation : rig.held;
			stream->data->push_back(o);
		}
	for (auto stream : {rig.train, rig.validation, rig.held})
		std::sort(stream->data->begin(), stream->data->end(),
				  [](const J::Observation &a, const J::Observation &b) { return a.id < b.id; });
	rig.request.count = rig.train->data->size();
	for (size_t type = 1; type < size_t(J::Channel::Count); ++type)
		rig.request.options.channelWeight[type] = .2;
	Oracle oracle(rig);
	J::Result result;
	Must(J::Solve(rig.request, Read(), result, &oracle));
	CHECK(result.preferred != SIZE_MAX);
	if (result.preferred != SIZE_MAX)
	{
		const auto &m = result.alternatives[result.preferred];
		CHECK(!m.linearAmplitudeBlock);
		CHECK(m.validation.used == rig.validation->data->size());
		J::Score held;
		Must(J::EvaluateFrozen(rig.request, m, rig.held, rig.held->data->size(), J::Role::Heldout,
							   Key("aux heldout"), Read(), held));
		CHECK(held.available);
		CHECK(held.intervalMse < 1);
		std::ostringstream row;
		row << "{\"name\":\"distinct_grid_deluxe_operators\",\"heldout_mse\":" << held.intervalMse
			<< ",\"used\":" << held.used << ",\"requested\":" << held.requested
			<< ",\"grid_is_indirect\":false}";
		rows.push_back(row.str());
	}
	// Surface GI cannot silently substitute for the missing grid-bounce reduction.
	rig.request.profiles[0].contract.physicalTransportAssumed = true;
	J::Result unavailable;
	Must(J::Solve(rig.request, Read(), unavailable, &oracle));
	CHECK(unavailable.preferred == SIZE_MAX);
}
void Rejection()
{
	Rig rig;
	auto c = Point(10, V(0, 0, 1));
	rig.request.candidates = {c};
	rig.Observations(rig.Truth({Answer(c, V(2.1, .4, .8))}));
	Oracle oracle(rig);
	J::Result result;
	auto bad = rig.request;
	bad.profiles[0].contract.transferAttested = false;
	Must(J::Solve(bad, Read(), result));
	CHECK(result.preferred == SIZE_MAX);
	bad = rig.request;
	bad.candidates[0].shape.initial.rgbAmplitude.x = std::numeric_limits<float>::quiet_NaN();
	auto error = J::Solve(bad, Read(), result);
	CHECK(!error || result.preferred == SIZE_MAX);
	bad = rig.request;
	bad.options.maxCandidates = 1;
	bad.candidates.push_back(Point(20, V(0, 0, .8)));
	CHECK(J::Solve(bad, Read(), result).code == Code::InvalidInput);
	bad = rig.request;
	bad.candidates[0].shape.initial.geometry.position.world = Vec3(10, 10, 10);
	Must(J::Solve(bad, Read(), result, &oracle));
	CHECK(result.preferred == SIZE_MAX);
	rows.push_back("{\"name\":\"invalid_unknown_and_resource_controls\",\"accepted_physical_sources\":0}");
}
void Protocol()
{
	Rig rig;
	auto c = Point(10, V(0, 0, 1));
	rig.request.candidates = {c};
	rig.Observations(rig.Truth({Answer(c, V(2.1, .4, .8))}));
	Oracle oracle(rig);
	J::Result complete, part, resumed;
	Must(J::Solve(rig.request, Read(), complete, &oracle));
	CHECK(complete.preferred != SIZE_MAX);
	Must(J::Solve(rig.request, Read(), part, &oracle, nullptr, 1));
	Must(J::Solve(rig.request, Read(), resumed, &oracle, &part.checkpoint));
	CHECK(complete.alternatives[complete.preferred].key == resumed.alternatives[resumed.preferred].key);
	CHECK(complete.work.evaluations == resumed.work.evaluations);
	CHECK(complete.work.rays == resumed.work.rays);
	auto reversed = rig.request;
	std::reverse(reversed.candidates.begin(), reversed.candidates.end());
	auto bad = part.checkpoint;
	bad.inputKey[0] ^= 1;
	J::Result rejected;
	CHECK(J::Solve(rig.request, Read(), rejected, &oracle, &bad).code == Code::InvalidState);
	(*rig.train->data)[0].role = J::Role::Heldout;
	CHECK(J::Solve(rig.request, Read(), rejected, &oracle).code == Code::InvalidInput);
	(*rig.train->data)[0].role = J::Role::Training;
	rig.train->corrupt = true;
	CHECK(J::Solve(rig.request, Read(), rejected, &oracle).code == Code::ProviderError);
	rig.train->corrupt = false;
	class Stop final : public Cancellation
	{
		bool IsCancelled() const noexcept override { return true; }
	} stop;
	auto cancelled = Read();
	cancelled.cancellation = &stop;
	CHECK(J::Solve(rig.request, cancelled, rejected, &oracle).code == Code::Cancelled);
	CHECK(rejected.state == J::State::Cancelled);
	auto tiny = Read();
	tiny.maxBytes = 1024;
	CHECK(J::Solve(rig.request, tiny, rejected, &oracle).code == Code::MemoryLimit);
	auto bounded = rig.request;
	bounded.options.maxEvaluations = 1;
	CHECK(J::Solve(bounded, Read(), rejected, &oracle).code == Code::WorkLimit);
	CHECK(rejected.state == J::State::Budget);
	const auto key = complete.alternatives[complete.preferred].key;
	for (auto &x : *rig.held->data)
		for (auto &y : x.rgb)
			y.lower = y.upper = 10000;
	J::Result again;
	Must(J::Solve(rig.request, Read(), again, &oracle));
	CHECK(again.alternatives[again.preferred].key == key);
	J::Score score;
	auto changed = rig.request;
	changed.policyKey = Key("changed interpretation");
	CHECK(J::EvaluateFrozen(changed, complete.alternatives[complete.preferred], rig.validation,
							rig.validation->data->size(), J::Role::Validation, oracle.Key(), Read(), score)
			  .code == Code::InvalidState);
}
void GeometryUncertainty()
{
	Rig rig;
	auto c = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {c};
	auto target = rig.Truth({Answer(c, V(2, 1, 1))});
	rig.Observations(target);
	// Missing opaque geometry has no safe transparency interpretation.
	BakedSurface missing;
	missing.ordinal = uint32_t(rig.scene.surfaces.size());
	missing.kind = BakedSurfaceKind::Unsupported;
	missing.motion = SurfaceMotion::StaticWorld;
	rig.scene.surfaces.push_back(missing);
	Rebuild(rig.scene);
	rig.UpdateGeometry();
	Oracle oracle(rig);
	J::Result result;
	Must(J::Solve(rig.request, Read(), result, &oracle));
	CHECK(result.preferred == SIZE_MAX);
	rows.push_back("{\"name\":\"unbounded_missing_geometry\",\"unsupported_visibility_kept_unknown\":true,"
				   "\"accepted\":false}");
}
void WallOpening()
{
	for (bool opening : {false, true})
	{
		Rig rig;
		rig.scene.Quad(V(0, -.35, 0), V(0, .35, 0), V(0, .35, .6), V(0, -.35, .6));
		if (!opening)
			rig.scene.Quad(V(0, -1.4, .6), V(0, 1.4, .6), V(0, 1.4, 1.8), V(0, -1.4, 1.8));
		Rebuild(rig.scene);
		rig.UpdateGeometry();
		auto c = Point(10, V(-.65, 0, .8));
		rig.request.candidates = {c};
		Run(rig, opening ? "opening_and_occlusion" : "sealed_partition", {Answer(c, V(2.5, .7, 1.2))});
	}
}
void HighReflectance()
{
	Rig rig;
	rig.request.materials[0] = KnownMaterial(1, V(.95, .95, .95));
	rig.request.profiles[0].contract.physicalTransportAssumed = true;
	auto c = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {c};
	auto result = Run(rig, "high_reflectance_tail", {Answer(c, V(2, 1, 1))});
	if (result.preferred != SIZE_MAX)
	{
		const auto &m = result.alternatives[result.preferred];
		CHECK(m.omittedBouncePower.known);
		CHECK(m.omittedBouncePower.value > m.roots[0].firstBounceAreaWeighted);
	}
}
void ShadowJoint()
{
	namespace H = Rend2LightRecon::Core::Shadow;
	Rig rig(false);
	rig.scene.Quad(V(0, -1.4, 0), V(0, 1.4, 0), V(0, 1.4, .4), V(0, -1.4, .4));
	Rebuild(rig.scene);
	rig.UpdateGeometry();
	auto source = Point(10, V(-.65, 0, .8));
	auto start = source;
	start.shape.initial.geometry.position.world.x = -.85f;
	start.shape.fitPosition = true;
	start.shape.centreBounds = Box(V(-1.1, 0, .8), V(-.3, 0, .8));
	rig.request.candidates = {start};
	rig.request.options.geometry = true;
	rig.request.options.positionStepWorld = .1;
	rig.request.options.geometrySweeps = 2;
	auto truth = rig.Truth({Answer(source, V(3, 1, .5))});
	rig.Observations(truth);
	auto samples = std::make_shared<std::vector<H::Sample>>();
	for (const auto &o : *rig.train->data)
	{
		H::Sample s;
		s.id = o.id;
		s.chart = s.material = 1;
		s.page = 0;
		s.x = unsigned(std::lround((o.receiver.world.x + .9) / .45));
		s.y = unsigned(std::lround((o.receiver.world.y + 1) / .5));
		s.receiver = o.receiver;
		s.sourceLinear = s.primariesComparable = s.filterKnown = true;
		s.rgb = truth[size_t(o.patch - 1)];
		s.sigmaRgb = .001;
		samples->push_back(s);
	}
	std::shared_ptr<const H::Evidence> evidence;
	H::DetectionOptions detection;
	detection.minimumRelativeContrast = .01;
	Must(H::Detect(rig.request.mapKey, Key("Explicit analytical physical relative footprint"), Lease(samples),
				   detection, Read(), evidence));
	CHECK(!evidence->edges.empty());
	H::Context context;
	context.observations = evidence;
	context.visibility = rig.scene.visibility;
	context.policy.units.worldUnitsPerMeter = 1;
	context.policy.searchRadiusWorld = 3;
	context.policy.receiverPushWorld = .001;
	context.policy.sourceSamples = 1;
	context.policy.receiverSamples = 1;
	context.policy.provenance = "Automatic S09 associations on S11 training data; no oracle bindings";
	std::shared_ptr<const H::Objective> objective;
	Must(H::Objective::Build(context, Read(), objective));
	Oracle oracle(rig);
	J::Result baseline, automatic;
	Must(J::Solve(rig.request, Read(), baseline, &oracle));
	rig.request.shadow = objective;
	rig.request.shadowWeight = .1;
	Must(J::Solve(rig.request, Read(), automatic, &oracle));
	CHECK(automatic.preferred != SIZE_MAX);
	if (automatic.preferred != SIZE_MAX && baseline.preferred != SIZE_MAX)
	{
		const auto &a = automatic.alternatives[automatic.preferred];
		const auto &b = baseline.alternatives[baseline.preferred];
		double ae = Length(V(a.sources[0].value.geometry.position.world) -
						   V(source.shape.initial.geometry.position.world));
		double be = Length(V(b.sources[0].value.geometry.position.world) -
						   V(source.shape.initial.geometry.position.world));
		std::ostringstream row;
		row << "{\"name\":\"automatic_shadow_joint_ablation\",\"oracle\":false,\"edges\":"
			<< evidence->edges.size() << ",\"intensity_position_error\":" << be
			<< ",\"automatic_position_error\":" << ae << ",\"shadow_objective\":" << a.training.shadow
			<< ",\"rays\":" << automatic.work.rays << "}";
		rows.push_back(row.str());
	}
}
int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	auto start = std::chrono::steady_clock::now();
	try
	{
		const char *only = std::getenv("S11_CASE");
		auto run = [&](const char *name, void (*test)()) {
			if (!only || std::string(only) == name)
				test();
		};
		run("basic", Basic);
		run("same", SameColour);
		run("scale", ScaleGauge);
		run("mixed", Mixed);
		run("adjacent", Adjacent);
		run("tube", Tube);
		run("bounce", Bounce);
		run("weak", Weak);
		run("ambiguous", Ambiguous);
		run("dark", Dark);
		run("ambient", Ambient);
		run("geometry", Geometry);
		run("shape-refinement", ShapeRefinement);
		run("missing", Missing);
		run("profile", ProfileAlternatives);
		run("saturated", Saturated);
		run("sun", Sun);
		run("material", UnknownMaterial);
		run("auxiliary", Auxiliary);
		run("rejection", Rejection);
		run("protocol", Protocol);
		run("uncertainty", GeometryUncertainty);
		run("walls", WallOpening);
		run("tail", HighReflectance);
		run("shadow", ShadowJoint);
		if (argc > 1)
		{
			std::ofstream f(argv[1]);
			f << "{\"schema\":\"org.openjk.joint-tests.v1\",\"checks\":" << checks
			  << ",\"failures\":" << failures << ",\"cases\":[";
			for (size_t i = 0; i < rows.size(); ++i)
				f << (i ? "," : "") << rows[i];
			f << "]}\n";
		}
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	std::printf("S11: %d checks %d failures %.3fs\n", checks, failures,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	return failures ? 1 : 0;
}
