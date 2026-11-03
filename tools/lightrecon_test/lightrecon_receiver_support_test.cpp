// X15 controlled capability, fairness and adversarial missing-prediction tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_test_scene.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace JointTest;
namespace RS = Rend2LightRecon::Core::ReceiverSupport;
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
struct Cancel : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
RS::Operator HistoricalOperator(bool linear = false)
{
	RS::Operator o;
	Bake::Recipe recipe;
	recipe.fast = false;
	recipe.falloff = linear ? Bake::Falloff::Linear : Bake::Falloff::InverseSquare;
	o.model.profile = Bake::NetRadiantProfile(recipe);
	o.model.law = Shape::Law::HistoricalBake;
	o.model.allowAssumed = o.bake.allowAssumed = true;
	o.bake.output = Bake::Domain::EncodedRgb8BeforeQuantization;
	o.scope = "Owned known recipe, planar super1, independent geometry only";
	return o;
}
RS::Geometry Geometry(Id id)
{
	RS::Geometry g;
	g.receiver.sample = id;
	g.receiver.surface = 1;
	g.receiver.material = 1;
	g.receiver.world = Vec3(float(id) * .01f, 0, 0);
	g.receiver.geometricNormal = g.receiver.shadingNormal = Vec3(0, 0, 1);
	g.receiver.mappedPlanar = g.receiver.compilerPlacementCertified = true;
	g.block = id % 3 + 1;
	g.areaWorldSquared = 2;
	g.materialKnown = g.opticsKnown = true;
	return g;
}
std::shared_ptr<const RS::Manifest> Manifest(const std::vector<RS::Geometry> &geometry,
											 const std::vector<RS::Operator> &ops,
											 Bake::EvaluationContext ctx)
{
	RS::Request q;
	q.map = Key("owned map");
	q.geometry = ctx.geometryKey;
	q.partition = Key("owned isolated roles");
	q.policy = Key("frozen X15 scope");
	q.context = ctx;
	q.operators = ops;
	auto rows = std::make_shared<Stream<RS::Geometry>>();
	*rows->data = geometry;
	std::shared_ptr<const RS::Manifest> out;
	Must(RS::Build(q, rows, rows->data->size(), Read(), out));
	return out;
}
int main(int argc, char **argv)
{
	double positiveMse = -1, blackMse = -1;
	uint64_t runtimeRejected = 0, manifestChecks = 0;
	try
	{
		Rig rig;
		auto ctx = rig.request.geometry;
		auto historical = HistoricalOperator();
		auto different = Geometry(2);
		different.receiver.shadingNormal = Vec3(.6f, 0, .8f);
		auto original = different.receiver.shadingNormal;
		auto equal = Geometry(1);
		CHECK(RS::Check(historical, equal, ctx).reason == RS::Reason::Supported);
		CHECK(RS::Check(historical, different, ctx).reason == RS::Reason::DistinctNormals);
		CHECK(different.receiver.shadingNormal.x == original.x);
		auto patch = Geometry(3);
		patch.receiver.mappedPlanar = false;
		CHECK(RS::Check(historical, patch, ctx).reason == RS::Reason::NonPlanar);
		auto style = Geometry(4);
		style.receiver.style = 255;
		CHECK(RS::Check(historical, style, ctx).reason == RS::Reason::UnsupportedStyle);
		auto material = Geometry(5);
		material.materialKnown = false;
		CHECK(RS::Check(historical, material, ctx).reason == RS::Reason::Supported);
		auto outgoing = historical;
		outgoing.requiresMaterial = true;
		CHECK(RS::Check(outgoing, material, ctx).reason == RS::Reason::MaterialRequired);
		auto bounded = material;
		bounded.materialAvailable = true;
		CHECK(RS::Check(outgoing, bounded, ctx).reason == RS::Reason::Supported);
		auto unknown = historical;
		unknown.model.profile = {};
		CHECK(RS::Check(unknown, equal, ctx).reason == RS::Reason::UnknownProfile);
		auto transfer = historical;
		transfer.transferAttested = false;
		CHECK(RS::Check(transfer, equal, ctx).reason == RS::Reason::UnknownTransfer);
		auto grid = Geometry(6);
		grid.channel = RS::Channel::GridAmbient;
		CHECK(RS::Check(historical, grid, ctx).reason == RS::Reason::OrderedGrid);
		auto physical = historical;
		physical.model.law = Shape::Law::DiffuseIntegral;
		physical.model.output = Shape::Output::RelativeIncident;
		CHECK(RS::Check(physical, different, ctx).reason == RS::Reason::Supported);
		auto profiles = Manifest({equal, different, patch, style, material},
								 {historical, HistoricalOperator(true), physical}, ctx);
		CHECK(profiles->Report().requested == 5 && profiles->Report().admitted == 2);
		CHECK(profiles->Report().commonArea == 4 && profiles->Report().requestedArea == 10);
		CHECK(profiles->Report().profileAdmitted[2] == 3);
		CHECK(profiles->Find(2)->profiles[0].reason == RS::Reason::DistinctNormals);
		manifestChecks = profiles->Report().checks;
		std::ostringstream json;
		Must(RS::WriteJson(*profiles, json, Read()));
		CHECK(json.str().find("DistinctNormals") != std::string::npos);
		auto repeat = Manifest({equal, different, patch, style, material},
							   {historical, HistoricalOperator(true), physical}, ctx);
		CHECK(repeat->Key() == profiles->Key());
		auto changed = equal;
		changed.receiver.world.x += 1;
		const RS::Entry *entry = nullptr;
		CHECK(profiles->Match(changed, entry).code == Code::InvalidState);
		struct Origin : PointClassifier
		{
			PointDomain state = PointDomain::Solid;
			mutable Vec3 sampled;
			Error Classify(Vec3 p, double tolerance, PointDomain &out) const override
			{
				sampled = p;
				if (tolerance != 0)
					return Failure(Code::InvalidInput, "Wrong origin tolerance");
				out = state;
				return {};
			}
		};
		Origin domain;
		auto decision = RS::Check(historical, equal, ctx);
		uint64_t probes = 0;
		Must(RS::CheckOrigin(historical, equal, ctx, &domain, decision, probes));
		CHECK(decision.reason == RS::Reason::ReceiverOriginSolid && probes == 1 &&
			  domain.sampled.z == 1);
		domain.state = PointDomain::Boundary;
		decision = RS::Check(historical, equal, ctx);
		Must(RS::CheckOrigin(historical, equal, ctx, &domain, decision, probes));
		CHECK(decision.reason == RS::Reason::ReceiverOriginBoundary);
		domain.state = PointDomain::Unknown;
		decision = RS::Check(historical, equal, ctx);
		Must(RS::CheckOrigin(historical, equal, ctx, &domain, decision, probes));
		CHECK(decision.reason == RS::Reason::ReceiverOriginUnknown);
		domain.state = PointDomain::Free;
		decision = RS::Check(historical, equal, ctx);
		Must(RS::CheckOrigin(historical, equal, ctx, &domain, decision, probes));
		CHECK(decision.reason == RS::Reason::Supported);
		// Identical fixed denominator, distinct candidate visibility. Exclusions never follow the
		// candidate.
		std::vector<RS::RuntimePrediction> a(2);
		a[0].sample = 1;
		a[0].state = RS::RuntimeState::Available;
		a[0].value = Vec3(3, 2, 1);
		a[1].sample = 5;
		a[1].state = RS::RuntimeState::KnownBlocked;
		RS::Coverage ca, cb;
		Must(RS::AuditCoverage(*profiles, {1, 2, 3, 4, 5}, a, Read(), ca));
		CHECK(ca.complete && ca.used == 2 && ca.blocked == 1 && ca.excluded == 3);
		auto b = a;
		b[0].state = RS::RuntimeState::UnknownVisibility;
		Must(RS::AuditCoverage(*profiles, {1, 2, 3, 4, 5}, b, Read(), cb));
		CHECK(!cb.complete && cb.unknownVisibility == 1 && cb.denominator == ca.denominator);
		b.pop_back();
		Must(RS::AuditCoverage(*profiles, {1, 2, 3, 4, 5}, b, Read(), cb));
		CHECK(!cb.complete && cb.missing == 1);
		runtimeRejected += cb.missing + cb.unknownVisibility;
		b = a;
		b[0].state = RS::RuntimeState::WorkLimit;
		Must(RS::AuditCoverage(*profiles, {1, 5}, b, Read(), cb));
		CHECK(!cb.complete && cb.workLimited == 1);
		b = a;
		b[0].value.x = -1;
		Must(RS::AuditCoverage(*profiles, {1, 5}, b, Read(), cb));
		CHECK(!cb.complete && cb.nonFinite == 1);
		b = a;
		b[1].value.x = 1;
		Must(RS::AuditCoverage(*profiles, {1, 5}, b, Read(), cb));
		CHECK(!cb.complete && cb.nonFinite == 1);
		b = a;
		b.push_back(a[0]);
		CHECK(RS::AuditCoverage(*profiles, {1, 5}, b, Read(), cb).code == Code::InvalidInput);
		Cancel cancellation;
		auto read = Read();
		read.cancellation = &cancellation;
		CHECK(RS::AuditCoverage(*profiles, {1}, {}, read, cb).code == Code::Cancelled);
		// Actual production known-blocked / unknown-visibility forward predictions stay distinct.
		struct Visibility : VisibilityProvider
		{
			VisibilityState state = VisibilityState::Blocked;
			Error Query(const RayQuery &, const ReadContext &, RayResult &o) const override
			{
				o = {};
				o.state = state;
				o.reliability = RayReliability::Geometric;
				o.originDomain = PointDomain::Free;
				return {};
			}
		};
		auto visibility = std::make_shared<Visibility>();
		auto context = ctx;
		context.visibility = visibility;
		auto source = std::make_shared<std::vector<Bake::SourceHypothesis>>(1);
		auto &light = source->at(0);
		light.id = 9;
		light.kind = Bake::SourceKind::Point;
		light.position.known = true;
		light.position.world = Vec3(0, 0, 80);
		light.amplitudeKnown = true;
		light.compilerRgbAmplitude = Vec3(40, 20, 10);
		auto receivers = std::make_shared<std::vector<Bake::Receiver>>(1, equal.receiver);
		Bake::Options options;
		options.allowAssumed = true;
		Bake::Result prediction;
		Bake::CpuForwardEvaluator evaluator;
		Must(evaluator.Evaluate(historical.model.profile, Lease(source), Lease(receivers), {},
								context, options, Read(), prediction));
		CHECK(prediction.predictions.data[0].available &&
			  prediction.predictions.data[0].rgb.x == 0);
		visibility->state = VisibilityState::Unknown;
		Must(evaluator.Evaluate(historical.model.profile, Lease(source), Lease(receivers), {},
								context, options, Read(), prediction));
		CHECK(!prediction.predictions.data[0].available &&
			  prediction.predictions.data[0].status == Bake::Status::VisibilityUnknown);
		visibility->state = VisibilityState::Visible;
		std::shared_ptr<const Bake::PreparedFactors> factors;
		Must(evaluator.Prepare(historical.model.profile, Lease(source), Lease(receivers), context,
							   options, Read(), factors));
		CHECK(factors->PreparationWork().receiverCapabilityChecks == 1);
		auto cached = options;
		cached.factors = factors;
		Must(evaluator.Evaluate(historical.model.profile, Lease(source), Lease(receivers), {},
								context, cached, Read(), prediction));
		CHECK(prediction.work.receiverCapabilityChecks == 0 &&
			  prediction.predictions.data[0].available);
		(*receivers)[0].shadingNormal = Vec3(.6f, 0, .8f);
		CHECK(evaluator
				  .Evaluate(historical.model.profile, Lease(source), Lease(receivers), {}, context,
							cached, Read(), prediction)
				  .code == Code::InvalidState);
		(*receivers)[0] = equal.receiver;
		auto changedPolicy = cached;
		changedPolicy.required.push_back(Bake::Feature::BounceFill);
		CHECK(evaluator
				  .Evaluate(historical.model.profile, Lease(source), Lease(receivers), {}, context,
							changedPolicy, Read(), prediction)
				  .code == Code::InvalidState);
		changedPolicy = cached;
		changedPolicy.allowAssumed = false;
		CHECK(evaluator
				  .Evaluate(historical.model.profile, Lease(source), Lease(receivers), {}, context,
							changedPolicy, Read(), prediction)
				  .code == Code::InvalidState);

		struct GeometryVisibility : VisibilityProvider
		{
			Error Query(const RayQuery &q, const ReadContext &, RayResult &o) const override
			{
				o = {};
				o.state = q.endWorld.x > 0 ? VisibilityState::Unknown : VisibilityState::Visible;
				o.reliability = RayReliability::Geometric;
				o.originDomain = PointDomain::Free;
				return {};
			}
		};
		auto sourceDependent = context;
		sourceDependent.visibility = std::make_shared<GeometryVisibility>();
		light.position.world.x = -1;
		Must(evaluator.Evaluate(historical.model.profile, Lease(source), Lease(receivers), {},
								sourceDependent, options, Read(), prediction));
		CHECK(prediction.predictions.data[0].available && prediction.predictions.data[0].rgb.x > 0);
		light.position.world.x = 1;
		Must(evaluator.Evaluate(historical.model.profile, Lease(source), Lease(receivers), {},
								sourceDependent, options, Read(), prediction));
		CHECK(!prediction.predictions.data[0].available &&
			  prediction.predictions.data[0].status == Bake::Status::VisibilityUnknown);
		// Geometry-only streaming limits/corruption/cancellation.
		RS::Request request;
		request.map = Key("owned map");
		request.geometry = ctx.geometryKey;
		request.partition = Key("owned partition");
		request.context = ctx;
		request.operators = {historical};
		auto stream = std::make_shared<Stream<RS::Geometry>>();
		stream->data->push_back(equal);
		std::shared_ptr<const RS::Manifest> missing;
		auto tiny = Read();
		tiny.maxBytes = 1;
		CHECK(RS::Build(request, stream, 1, tiny, missing).code == Code::MemoryLimit);
		stream->corrupt = true;
		CHECK(RS::Build(request, stream, 1, Read(), missing).code == Code::ProviderError);
		stream->corrupt = false;
		CHECK(RS::Build(request, stream, 1, read, missing).code == Code::Cancelled);
		request.maxChecks = 0;
		CHECK(RS::Build(request, stream, 1, Read(), missing).code == Code::WorkLimit);
		// Production Joint fixes unsupported training geometry BEFORE any model is optimized.
		Historical(rig, Bake::Recipe{});
		auto candidate = CompilerPoint(71, V(-30, 0, 80), true);
		rig.request.candidates = {candidate};
		rig.Observations(rig.Truth({Answer(candidate, V(40, 25, 10))}), true);
		// Mapped observations have independent receiver geometry; preserve the original normals.
		auto normals = std::make_shared<std::vector<RS::Geometry>>();
		for (auto fold : {rig.train, rig.validation, rig.held})
			for (const auto &x : *fold->data)
				normals->push_back(J::ReceiverGeometry(rig.request, x));
		std::sort(normals->begin(), normals->end(),
				  [](const RS::Geometry &a, const RS::Geometry &b) {
					  return a.receiver.sample < b.receiver.sample;
				  });
		auto geometryStream = std::make_shared<Stream<RS::Geometry>>();
		*geometryStream->data = *normals;
		Must(J::FreezeReceiverSupport(rig.request, geometryStream, geometryStream->data->size(),
									  rig.request.trainingKey, Read(),
									  rig.request.receiverSupport));
		J::Result fitted;
		Must(J::Solve(rig.request, Read(), fitted));
		CHECK(fitted.work.forwardCalls > 0 && fitted.receiverSupport);
		CHECK(!fitted.alternatives.empty());
		if (!fitted.alternatives.empty())
		{
			J::Score score;
			Must(J::EvaluateFrozen(rig.request, fitted.alternatives.front(), rig.validation,
								   rig.validation->data->size(), J::Role::Validation,
								   Key("private validation"), Read(), score));
			positiveMse = score.intervalMse;
			blackMse = score.blackMse;
			CHECK(score.coverage.complete && score.positive &&
				  score.used == rig.validation->data->size());
			CHECK(positiveMse < blackMse);
			auto missingRows = std::make_shared<Stream<J::Observation>>();
			*missingRows->data = *rig.validation->data;
			missingRows->data->pop_back();
			// A controller must separately verify its requested row count. A truncated stream fails
			// Load.
			CHECK(J::EvaluateFrozen(rig.request, fitted.alternatives.front(), missingRows,
									rig.validation->data->size(), J::Role::Validation,
									Key("private validation"), Read(), score)
					  .code == Code::ProviderError);
			CHECK(J::EvaluateFrozen(rig.request, fitted.alternatives.front(), missingRows,
									missingRows->data->size(), J::Role::Validation,
									Key("private validation"), Read(), score)
					  .code == Code::InvalidState);
			auto dark = std::make_shared<Stream<J::Observation>>();
			*dark->data = *rig.validation->data;
			for (auto &x : *dark->data)
				for (auto &c : x.rgb)
					c = Amplitude::Rgb8(0);
			Must(J::EvaluateFrozen(rig.request, fitted.alternatives.front(), dark,
								   dark->data->size(), J::Role::Validation,
								   Key("private dark validation"), Read(), score));
			CHECK(!score.positive && score.blackMse == 0);
			auto wrong = rig.request;
			wrong.profiles[0].model.profile = Bake::NetRadiantProfile(Bake::Recipe{});
			wrong.profiles[0].model.profile.id += "changed";
			// Runtime profile rules, rather than the readable label alone, are in the binding.
			auto alternative = HistoricalOperator(true);
			wrong.profiles[0].model.profile = alternative.model.profile;
			wrong.profiles[0].contract.profile = alternative.model.profile;
			J::Result rejected;
			CHECK(!J::Solve(wrong, Read(), rejected));
		}
		// Explicit 55/64 coverage witness: no low-error source can erase the missing nine.
		std::vector<RS::Geometry> sixtyFour;
		std::vector<RS::RuntimePrediction> fiftyFive;
		std::vector<Id> ids;
		for (Id id = 1; id <= 64; ++id)
		{
			sixtyFour.push_back(Geometry(id));
			ids.push_back(id);
			if (id <= 55)
			{
				RS::RuntimePrediction p;
				p.sample = id;
				p.state = RS::RuntimeState::Available;
				fiftyFive.push_back(p);
			}
		}
		auto fixed = Manifest(sixtyFour, {historical}, ctx);
		Must(RS::AuditCoverage(*fixed, ids, fiftyFive, Read(), cb));
		CHECK(cb.admitted == 64 && cb.used == 55 && cb.missing == 9 && !cb.complete);
		// 69 independent blocks are a preflight error under a 64-row budget, before a model
		// attempt.
		Rig blocks;
		auto c = Point(9, V(-.5, 0, 1.2), true);
		blocks.request.candidates = {c};
		blocks.Observations(blocks.Truth({Answer(c, V(2, 1, .5))}));
		auto seed = blocks.train->data->front();
		blocks.train->data->clear();
		for (Id id = 1; id <= 69; ++id)
		{
			auto x = seed;
			x.id = 10000 + id;
			x.block = id;
			x.correlationGroup = 0;
			blocks.train->data->push_back(x);
		}
		blocks.request.count = 69;
		blocks.request.options.coarseRows = 64;
		J::Result budget;
		auto limit = J::Solve(blocks.request, Read(), budget);
		CHECK(limit.code == Code::InvalidInput && budget.work.attemptedModels == 0);
		CHECK(limit.message.find("required_groups=69") != std::string::npos);
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	if (argc == 2)
	{
		std::ofstream out(argv[1]);
		out << "{\"schema\":\"org.openjk.receiver-support-native.v1\",\"checks\":" << checks
			<< ",\"failures\":" << failures << ",\"manifest_checks\":" << manifestChecks
			<< ",\"unknown_or_missing_rejected\":" << runtimeRejected
			<< ",\"positive_mse\":" << positiveMse << ",\"black_mse\":" << blackMse << "}";
	}
	std::printf("X15 checks=%d failures=%d\n", checks, failures);
	return failures ? 1 : 0;
}
