// X10 geometry, correlation, blind refinement and weight invariants. No GL.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/sampling.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace S = Sampling;
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
void Must(Error e)
{
	CHECK(e);
	if (!e)
		throw std::runtime_error(e.message);
}
Disk::Digest Key(const char *s)
{
	return Disk::Hash(s, std::strlen(s));
}
ReadContext Read()
{
	ReadContext r;
	r.maxBytes = 64ull << 20;
	return r;
}
std::vector<S::Geometry> Geometry()
{
	std::vector<S::Geometry> out;
	for (unsigned role = 0; role < 3; ++role)
		for (unsigned i = 0; i < 100; ++i)
		{
			S::Geometry g;
			g.id = role * 1000 + i + 1;
			g.surface = 1 + i % 2;
			g.material = 1;
			g.position =
				Vec3(float((int(i % 10) - 5) * 8 + (role == 1 ? 300 : role == 2 ? -300 : 0)),
					 float((int(i / 10) - 5) * 8), 64);
			g.normal = i % 2 ? Vec3(1, 0, 0) : Vec3(0, 0, 1);
			g.receiverArea = g.representedArea = 4;
			g.footprintRadius = 1;
			g.role = S::Role(role);
			g.block = role + 1;
			out.push_back(g);
		}
	return out;
}
std::vector<S::Signal> Signals(const S::Plan &p, bool materialOnly = false, bool saturated = false,
							   bool dark = false)
{
	std::vector<S::Signal> result;
	for (const auto &g : p.geometry)
		if (g.role == S::Role::Training)
		{
			S::Signal s;
			s.id = g.id;
			for (unsigned c = 0; c < 3; ++c)
			{
				s.rgb[c] = dark ? 0
								: saturated
									  ? 255
									  : materialOnly ? double(g.material) * 40
													 : 64 + g.position.x * .5 + g.position.y * .25;
				s.censored[c] = saturated;
			}
			result.push_back(s);
		}
	return result;
}
void Tests()
{
	auto geometry = Geometry();
	S::Policy policy;
	policy.maxJobs = 4;
	policy.allowConditionalEncodedSignal = true;
	S::Plan p;
	Must(S::Build(Key("owned-map"), Key("frozen-gap-partition"), geometry, policy, Read(), p));
	CHECK(p.jobs.size() > 0);
	CHECK(p.report.localCellWorld == 16);
	CHECK(p.report.input == 300);
	CHECK(!p.geometry.empty());
	auto geometryKey = p.geometryKey;
	S::Plan refined;
	auto signals = Signals(p);
	Must(S::Refine(p, signals, Read(), refined));
	size_t admitted = 0, independent = 0;
	for (const auto &j : refined.jobs)
	{
		admitted += j.training.discoveryAdmitted;
		independent += j.training.validationGeometryAvailable;
		CHECK(j.training.reasons & S::UnknownEncoding);
		CHECK(j.validation.positive == 0 && j.heldout.positive == 0);
	}
	CHECK(admitted > 0);
	CHECK(independent > 0);
	CHECK(refined.geometryKey == geometryKey);
	CHECK(refined.rounds == 1);
	for (size_t i = 0; i < p.jobs.size(); ++i)
	{
		CHECK(p.jobs[i].selected[size_t(S::Purpose::ModelValidation)].size() ==
			  refined.jobs[i].selected[size_t(S::Purpose::ModelValidation)].size());
		for (size_t n = 0; n < p.jobs[i].selected[size_t(S::Purpose::ModelValidation)].size(); ++n)
			CHECK(p.jobs[i].selected[size_t(S::Purpose::ModelValidation)][n].id ==
				  refined.jobs[i].selected[size_t(S::Purpose::ModelValidation)][n].id);
	}
	std::reverse(geometry.begin(), geometry.end());
	S::Plan repeat;
	Must(S::Build(Key("owned-map"), Key("frozen-gap-partition"), geometry, policy, Read(), repeat));
	std::reverse(signals.begin(), signals.end());
	S::Plan twice;
	Must(S::Refine(repeat, signals, Read(), twice));
	CHECK(twice.key == refined.key);
	// A fitter can request residual/influence/shadow refinement on training IDs.
	// These ranks never change the frozen evaluation geometry or objective mass.
	std::vector<S::Priority> priorities;
	for (unsigned i = 0; i < 3; ++i)
	{
		S::Priority request;
		request.id = signals[i].id;
		request.cause = S::PriorityCause(i);
		request.value = .8;
		priorities.push_back(request);
	}
	S::Plan adaptive, adaptiveRepeat;
	Must(S::Refine(p, signals, Read(), adaptive, priorities));
	std::reverse(priorities.begin(), priorities.end());
	Must(S::Refine(p, signals, Read(), adaptiveRepeat, priorities));
	CHECK(adaptive.key == adaptiveRepeat.key);
	CHECK(adaptive.trainingSignalKey != refined.trainingSignalKey);
	for (size_t i = 0; i < p.jobs.size(); ++i)
		for (auto purpose : {S::Purpose::ModelValidation, S::Purpose::FinalHeldout})
		{
			const auto &a = adaptive.jobs[i].selected[size_t(purpose)];
			const auto &b = p.jobs[i].selected[size_t(purpose)];
			CHECK(a.size() == b.size());
			for (size_t n = 0; n < a.size(); ++n)
				CHECK(a[n].id == b[n].id);
		}
	S::Plan invalidPriority;
	priorities[0].id = 1001; // known held-out geometry, not a training Signal
	CHECK(S::Refine(p, signals, Read(), invalidPriority, priorities).code == Code::InvalidInput);
	priorities[0].id = signals[0].id;
	priorities[0].value = 1.1;
	CHECK(S::Refine(p, signals, Read(), invalidPriority, priorities).code == Code::InvalidInput);
	priorities[0].value = .8;
	priorities[0].cause = S::PriorityCause(-1);
	CHECK(S::Refine(p, signals, Read(), invalidPriority, priorities).code == Code::InvalidInput);
	// Hidden colours cannot be supplied even if their geometry is public.
	auto tampered = p;
	for (auto &g : tampered.geometry)
		if (g.role == S::Role::Heldout)
		{
			g.role = S::Role::Training;
			break;
		}
	S::Plan invalidated;
	CHECK(S::Refine(tampered, signals, Read(), invalidated).code == Code::InvalidState);
	tampered = p;
	tampered.evaluationCoverage[0][0].id ^= 1;
	CHECK(S::Refine(tampered, signals, Read(), invalidated).code == Code::InvalidState);
	tampered = p;
	tampered.report.work = 0;
	CHECK(S::Refine(tampered, signals, Read(), invalidated).code == Code::InvalidState);
	auto attack = signals;
	S::Signal hidden;
	hidden.id = 1001;
	attack.push_back(hidden);
	S::Plan bad;
	CHECK(S::Refine(p, attack, Read(), bad).code == Code::InvalidInput);
	attack = signals;
	attack[0].encodingKnown = true;
	CHECK(S::Refine(p, attack, Read(), bad).code == Code::InvalidInput);
	Must(S::Refine(p, Signals(p, false, false, true), Read(), bad));
	for (const auto &j : bad.jobs)
	{
		CHECK(!j.training.discoveryAdmitted);
		CHECK(j.training.reasons & S::NoPositiveSignal);
	}
	Must(S::Refine(p, Signals(p, true), Read(), bad));
	for (const auto &j : bad.jobs)
		CHECK(!j.training.discoveryAdmitted);
	auto materialBoundary = Geometry();
	for (auto &g : materialBoundary)
		g.material = g.position.x >= 0 ? 1 : 2;
	S::Plan materialGeometry, materialSignal;
	Must(S::Build(Key("colour-boundary"), Key("frozen-gap-partition"), materialBoundary, policy,
				  Read(), materialGeometry));
	Must(S::Refine(materialGeometry, Signals(materialGeometry, true), Read(), materialSignal));
	for (const auto &j : materialSignal.jobs)
		CHECK(!j.training.discoveryAdmitted);
	Must(S::Refine(p, Signals(p, false, true), Read(), bad));
	for (const auto &j : bad.jobs)
	{
		CHECK(!j.training.discoveryAdmitted);
		CHECK(j.training.reasons & S::SaturatedOnly);
	}
	// Copying identical receivers creates neither new support nor objective mass.
	auto inflated = Geometry();
	for (size_t i = 0; i < 100; ++i)
	{
		auto g = inflated[i];
		g.id += 10000;
		inflated.push_back(g);
	}
	S::Plan dedup;
	Must(S::Build(Key("owned-map"), Key("frozen-gap-partition"), inflated, policy, Read(), dedup));
	CHECK(dedup.geometry.size() == p.geometry.size());
	CHECK(dedup.report.duplicates >= 100);
	auto conflict = Geometry();
	auto conflicting = conflict[0];
	conflicting.receiverArea *= 2;
	conflict.push_back(conflicting);
	S::Plan conflictingPlan;
	CHECK(S::Build(Key("owned-map"), Key("frozen-gap-partition"), conflict, policy, Read(),
				   conflictingPlan)
			  .code == Code::InvalidInput);
	auto a = p.global[size_t(S::Purpose::CoarseCoverage)],
		 b = dedup.global[size_t(S::Purpose::CoarseCoverage)];
	CHECK(a.size() == b.size());
	for (size_t i = 0; i < a.size(); ++i)
	{
		CHECK(a[i].id == b[i].id);
		CHECK(a[i].statisticalWeight == b[i].statisticalWeight);
		CHECK(a[i].receiverArea == 4);
	}
	// A single planar material can provide gradients, but cannot certify full XYZ.
	auto plane = Geometry();
	for (auto &g : plane)
	{
		g.surface = 1;
		g.normal = Vec3(0, 0, 1);
	}
	Must(S::Build(Key("plane"), Key("frozen-gap-partition"), plane, policy, Read(), bad));
	S::Plan planar;
	Must(S::Refine(bad, Signals(bad), Read(), planar));
	for (const auto &j : planar.jobs)
		CHECK(j.training.reasons & S::InsufficientNormals);
	// Missing independent blocks is not evidence that a physical lamp is absent.
	auto local = Geometry();
	local.erase(std::remove_if(local.begin(), local.end(),
							   [](const S::Geometry &g) { return g.role != S::Role::Training; }),
				local.end());
	Must(S::Build(Key("local"), Key("frozen-gap-partition"), local, policy, Read(), bad));
	for (const auto &j : bad.jobs)
		CHECK(j.reasons & S::MissingIndependentFolds);
	policy.allowConditionalEncodedSignal = false;
	Must(S::Build(Key("owned-map"), Key("frozen-gap-partition"), Geometry(), policy, Read(), bad));
	S::Plan strict;
	Must(S::Refine(bad, Signals(bad), Read(), strict));
	for (const auto &j : strict.jobs)
		CHECK(!j.training.discoveryAdmitted);
	policy.maxWork = 1;
	CHECK(S::Build(Key("m"), Key("p"), Geometry(), policy, Read(), bad).code == Code::WorkLimit);
	policy.maxWork = 16000000;
	policy.maxTotalRows = 1;
	CHECK(S::Build(Key("m"), Key("p"), Geometry(), policy, Read(), bad).code == Code::WorkLimit);
	class Cancel final : public Cancellation
	{
	  public:
		bool IsCancelled() const noexcept override
		{
			return true;
		}
	} cancel;
	auto r = Read();
	r.cancellation = &cancel;
	CHECK(S::Build(Key("m"), Key("p"), Geometry(), {}, r, bad).code == Code::Cancelled);
	// Repeated rounds are bounded and have a training-signal identity.
	CHECK(refined.trainingSignalKey != Disk::Digest{});
	S::Plan final;
	Must(S::Refine(refined, Signals(refined), Read(), final));
	CHECK(S::Refine(final, Signals(final), Read(), bad).code == Code::WorkLimit);
}
int main(int argc, char **argv)
{
	try
	{
		Tests();
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	std::printf("X10: %d checks, %d failures\n", checks, failures);
	if (argc > 1)
	{
		std::ofstream out(argv[1]);
		out << "{\"schema\":\"org.openjk.sampling-tests.v1\",\"checks\":" << checks
			<< ",\"failures\":" << failures << "}\n";
	}
	return failures ? 1 : 0;
}
