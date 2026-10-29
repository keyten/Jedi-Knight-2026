// S06 owned CPU fixtures. Numerical truth stays in this test controller.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/amplitude_fit.h"
#include "lightrecon/bake_math.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <set>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace A = Rend2LightRecon::Core::Amplitude;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                             \
	do                                                                                                       \
	{                                                                                                        \
		++checks;                                                                                            \
		if (!(x))                                                                                            \
		{                                                                                                    \
			++failures;                                                                                      \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                                      \
		}                                                                                                    \
	} while (0)
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
class Rows final : public ChunkSource<A::Observation>
{
  public:
	std::vector<A::Observation> data;
	Error Read(uint64_t first, size_t n, const ReadContext &c, Chunk<A::Observation> &out) const override
	{
		if (c.cancellation && c.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "test cancelled");
		if (first > data.size() || n > data.size() - first)
			return Failure(Code::ProviderError, "row range");
		auto p = std::make_shared<std::vector<A::Observation>>(data.begin() + size_t(first),
															   data.begin() + size_t(first) + n);
		out = Lease(p);
		return {};
	}
};
class Room final : public VisibilityProvider
{
  public:
	mutable uint64_t rays = 0;
	bool unknown = false;
	Error Query(const RayQuery &q, const ReadContext &, RayResult &r) const override
	{
		++rays;
		r = {};
		r.nodeTests = 1;
		r.primitiveTests = 1;
		r.originDomain = PointDomain::Free;
		r.state = unknown ? VisibilityState::Unknown
						  : (q.startWorld.x < 120 && q.endWorld.x > 120) ? VisibilityState::Blocked
																		 : VisibilityState::Visible;
		r.reliability = unknown ? RayReliability::Unknown : RayReliability::Geometric;
		return {};
	}
};
struct Rig
{
	A::Request request;
	std::shared_ptr<Rows> rows = std::make_shared<Rows>();
	std::shared_ptr<Room> room = std::make_shared<Room>();
	std::vector<Vec3> truth;
	explicit Rig(Bake::Recipe recipe = Bake::Recipe())
	{
		recipe.fast = false;
		request.profile = Bake::NetRadiantProfile(recipe);
		request.mapKey = Key("s06-generated-map");
		request.observationKey = Key("s06-generated-observations");
		request.geometry.geometryKey = Key("s06-room-with-wall-x120");
		request.geometry.materialKey = Key("known-generated-material");
		request.geometry.observationPolicyKey = Key("explicit-compiler-domain");
		request.geometry.visibility = room;
		request.forward.allowAssumed = true;
		request.observations = rows;
		request.options.loss = A::Loss::Squared;
		request.options.maxIterations = 1000;
		request.options.maxEvaluations = 12000;
		request.options.nonlinearPasses = 16;
		request.options.restarts = 1;
		request.options.batchRows = 128;
		request.cache = std::make_shared<A::Cache>();
	}
	void Add(Vec3 xyz, Vec3 q, Vec3 upper = Vec3(1200, 1200, 1200),
			 Bake::SourceKind kind = Bake::SourceKind::Point)
	{
		A::Candidate c;
		c.source.id = request.candidates.size() + 1;
		c.source.kind = kind;
		c.source.position.known = true;
		c.source.position.world = xyz;
		c.source.amplitudeDomain = Bake::AmplitudeDomain::CompilerRelative;
		c.upperQ = upper;
		c.initialQ = V(std::min(64.f, upper.x), std::min(64.f, upper.y), std::min(64.f, upper.z)).Float();
		if (kind == Bake::SourceKind::Spot)
		{
			c.source.orientation.known = true;
			c.source.orientation.forward = Vec3(0, 0, -1);
			c.source.orientation.up = Vec3(0, -1, 0);
			c.source.spotTargetDistanceWorld = 160;
			c.source.spotRadiusWorld = 140;
		}
		request.candidates.push_back(c);
		truth.push_back(q);
	}
	void Generate(A::Type type = A::Type::SurfaceRelative, size_t extent = 4)
	{
		rows->data.clear();
		auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>();
		for (size_t j = 0; j < truth.size(); ++j)
		{
			auto s = request.candidates[j].source;
			s.compilerRgbAmplitude = truth[j];
			s.amplitudeKnown = true;
			sources->push_back(s);
		}
		auto receiver = std::make_shared<std::vector<Bake::Receiver>>();
		for (size_t face = 0; face < 3; ++face)
			for (size_t i = 0; i < extent; ++i)
				for (size_t j = 0; j < extent; ++j)
				{
					Bake::Receiver r;
					r.sample = receiver->size() + 1;
					r.surface = face + 1;
					r.mappedPlanar = r.compilerPlacementCertified = true;
					if (face == 0)
					{
						r.world = Vec3(-96 + float(i) * 48, -96 + float(j) * 48, 0);
						r.shadingNormal = r.geometricNormal = Vec3(0, 0, 1);
					}
					if (face == 1)
					{
						r.world = Vec3(-128, -96 + float(i) * 48, 32 + float(j) * 32);
						r.shadingNormal = r.geometricNormal = Vec3(1, 0, 0);
					}
					if (face == 2)
					{
						r.world = Vec3(-96 + float(i) * 48, 128, 32 + float(j) * 32);
						r.shadingNormal = r.geometricNormal = Vec3(0, -1, 0);
					}
					receiver->push_back(r);
				}
		Bake::Options options = request.forward;
		options.output = type == A::Type::SurfaceEncoded
							 ? Bake::Domain::EncodedRgb8BeforeQuantization
							 : (type == A::Type::SurfaceRelative ? Bake::Domain::CompilerLinearRelative255
																 : Bake::Domain::GridAmbientDirected);
		options.unpackedGridOnly =
			type == A::Type::GridAmbientRelative || type == A::Type::GridDirectedRelative;
		ReadContext context;
		context.maxBytes = 64ull * 1024 * 1024;
		Bake::CpuForwardEvaluator evaluator;
		Bake::Result prediction;
		auto status = evaluator.Evaluate(request.profile, Lease(sources), Lease(receiver), {},
										 request.geometry, options, context, prediction);
		if (!status)
			std::printf("fixture forward: %s [%s]\n", status.message.c_str(), status.path.c_str());
		CHECK(status);
		if (!status)
			throw std::runtime_error("invalid generated fixture");
		for (size_t i = 0; i < receiver->size(); ++i)
		{
			CHECK(prediction.predictions.data[i].available);
			A::Observation a;
			a.id = i + 1;
			a.receiver = (*receiver)[i];
			a.region = a.receiver.surface;
			a.type = type;
			a.measure = 256;
			a.encodingAttested = a.primariesComparable = a.comparisonScaleKnown = true;
			const auto &p = prediction.predictions.data[i];
			V value = type == A::Type::GridAmbientRelative
						  ? V(p.gridAmbient)
						  : type == A::Type::GridDirectedRelative ? V(p.gridDirected) : V(p.rgb);
			for (size_t k = 0; k < 3; ++k)
			{
				a.channels[k].lower = a.channels[k].upper = value[k];
				if (type == A::Type::SurfaceEncoded)
					a.channels[k] = A::Rgb8(p.packedRgb[k]);
			}
			rows->data.push_back(a);
		}
		request.count = rows->data.size();
	}
	A::Result Fit()
	{
		A::Result result;
		ReadContext context;
		context.maxBytes = 64ull * 1024 * 1024;
		auto e = A::Fit(request, context, result);
		if (!e)
			std::printf("fit: %s\n", e.message.c_str());
		CHECK(e);
		CHECK(result.fitted);
		CHECK(result.strictReevaluated);
		CHECK(result.sources.size() == truth.size());
		for (const auto &s : result.sources)
		{
			CHECK(Positive(V(s.compilerQ)));
			CHECK(!s.absoluteScaleKnown);
		}
		for (size_t i = 1; i < result.history.size(); ++i)
			CHECK(result.history[i] <= result.history[i - 1] + 1e-8 * std::max(1., result.history[i - 1]));
		return result;
	}
};
struct Witness
{
	std::string name;
	bool linear = false, converged = false, ambiguous = false;
	double error = 0, objective = 0, kkt = 0, strict = 0;
	uint64_t evaluations = 0, rays = 0, rank = 0, parameters = 0;
};
std::vector<Witness> witness;
double RelativeError(const Rig &rig, const A::Result &r)
{
	double sum = 0, den = 0;
	for (size_t j = 0; j < rig.truth.size(); ++j)
	{
		V delta = V(r.sources[j].compilerQ) - V(rig.truth[j]);
		sum += Dot(delta, delta);
		den += Dot(V(rig.truth[j]), V(rig.truth[j]));
	}
	return std::sqrt(sum / std::max(1., den));
}
void Record(const std::string &name, const Rig &rig, const A::Result &r)
{
	std::printf("%s: relative Q %.6g, max interval %.6g, stop %s, rank %zu/%zu\n", name.c_str(),
				RelativeError(rig, r), r.maximumIntervalError, A::StopName(r.stop), r.spectrum.rank,
				r.spectrum.parameters);
	Witness w;
	w.name = name;
	w.linear = r.linear;
	w.converged = r.converged;
	w.error = RelativeError(rig, r);
	w.objective = r.objective.Total();
	w.kkt = r.projectedKkt;
	w.strict = r.strictDifference;
	w.evaluations = r.work.objectiveEvaluations;
	w.rays = r.work.rays;
	w.rank = r.spectrum.rank;
	w.parameters = r.spectrum.parameters;
	for (const auto &s : r.sources)
		w.ambiguous |= s.allocationAmbiguous;
	witness.push_back(w);
}
void LinearCases()
{
	{
		Rig r;
		r.Add({0, 0, 160}, {300, 80, 20});
		r.Generate();
		auto fit = r.Fit();
		CHECK(fit.linear);
		CHECK(RelativeError(r, fit) < 1e-5);
		CHECK(fit.projectedKkt < 1e-5);
		CHECK(fit.spectrum.rank == 3);
		Record("point_linear", r, fit);
		auto before = r.room->rays;
		auto repeat = r.Fit();
		CHECK(repeat.key == fit.key);
		CHECK(repeat.sources[0].compilerQ.x == fit.sources[0].compilerQ.x);
		CHECK(repeat.work.prepareCalls == 0);
		CHECK(r.room->rays - before < fit.work.rays);
		r.request.candidates[0].source.position.world.x += 4;
		auto moved = r.Fit();
		CHECK(moved.key != fit.key);
		CHECK(moved.work.prepareCalls > 0);
	}
	{
		Rig r;
		r.Add({-55, -20, 160}, {300, 30, 10});
		r.Add({65, 15, 120}, {10, 50, 250});
		r.Generate();
		auto fit = r.Fit();
		CHECK(fit.linear);
		CHECK(RelativeError(r, fit) < .003);
		CHECK(fit.spectrum.rank == 6);
		Record("two_colour_linear", r, fit);
		std::reverse(r.request.candidates.begin(), r.request.candidates.end());
		auto repeat = r.Fit();
		CHECK(repeat.key == fit.key);
		for (size_t j = 0; j < fit.sources.size(); ++j)
			CHECK(repeat.sources[j].compilerQ.x == fit.sources[j].compilerQ.x);
	}
	{
		Rig r;
		r.Add({-65, -50, 160}, {300, 300, 300});
		r.Add({65, 30, 100}, {120, 120, 120});
		r.Add({40, -60, 200}, {0, 0, 0});
		r.Generate();
		auto fit = r.Fit();
		CHECK(RelativeError(r, fit) < .005);
		CHECK(Peak(V(fit.sources[2].compilerQ)) < 1);
		Record("same_colour_and_zero", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 320}, {20000, 10000, 5000}, {24000, 24000, 24000});
		r.Add({50, 0, 48}, {4, 2, 1});
		r.Generate();
		auto fit = r.Fit();
		CHECK(RelativeError(r, fit) < .001);
		CHECK(std::abs(fit.sources[1].compilerQ.x - 4) < .3);
		Record("bright_and_weak", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {200, 80, 40});
		r.Add({0, 0, 160}, {100, 40, 20});
		r.Generate();
		auto fit = r.Fit();
		CHECK(fit.spectrum.rank < fit.spectrum.parameters);
		CHECK(fit.sources[0].allocationAmbiguous && fit.sources[1].allocationAmbiguous);
		CHECK(fit.exchanges.size() == 3);
		for (const auto &e : fit.exchanges)
		{
			CHECK(std::abs(e.secondPerFirst - 1) < 1e-8);
			CHECK(e.lowerStep < e.upperStep);
		}
		CHECK(std::abs(fit.sources[0].compilerQ.x + fit.sources[1].compilerQ.x - 300) < .01);
		Record("coincident_rank_deficient", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {300, 80, 40});
		r.Add({.001f, 0, 160}, {100, 40, 20});
		r.request.options.ridge = 1e-10;
		r.Generate();
		auto fit = r.Fit();
		CHECK(fit.spectrum.rank < fit.spectrum.parameters || fit.spectrum.condition > 1e4);
		Record("near_proportional", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {300, 80, 20});
		r.Add({400, 0, 160}, {500, 500, 500});
		r.Generate();
		auto fit = r.Fit();
		CHECK(fit.sources[1].zeroResponse);
		CHECK(Peak(V(fit.sources[1].compilerQ)) == 0);
		CHECK(fit.sources[1].allocationAmbiguous);
		CHECK(std::abs(fit.sources[0].compilerQ.x - 300) < .01);
		Record("occluded_source", r, fit);
	}
}
void RobustAndPriors()
{
	Rig exact;
	exact.Add({0, 0, 160}, {300, 80, 20});
	exact.Generate();
	auto contaminated = exact.rows->data;
	for (size_t i = 0; i < contaminated.size(); i += 7)
		for (auto &c : contaminated[i].channels)
		{
			c.lower += 100;
			c.upper += 100;
		}
	Rig l2;
	l2.Add({0, 0, 160}, {300, 80, 20});
	l2.Generate();
	l2.rows->data = contaminated;
	l2.request.options.maxIterations = 300;
	auto ordinary = l2.Fit();
	Rig robust;
	robust.Add({0, 0, 160}, {300, 80, 20});
	robust.Generate();
	robust.rows->data = contaminated;
	robust.request.options.loss = A::Loss::Huber;
	robust.request.options.huber = 1;
	auto resistant = robust.Fit();
	CHECK(RelativeError(robust, resistant) < RelativeError(l2, ordinary) * .2);
	Record("corrupted_huber", robust, resistant);
	Rig area;
	area.Add({0, 0, 160}, {300, 80, 20});
	area.Generate();
	area.rows->data[0].measure = 1e-8;
	for (auto &c : area.rows->data[0].channels)
		c.lower = c.upper = c.lower + 1000;
	auto weighted = area.Fit();
	CHECK(RelativeError(area, weighted) < 1e-4);
	Record("world_area_weight", area, weighted);
	Rig colour;
	colour.Add({0, 0, 160}, {300, 80, 20});
	colour.Generate();
	auto &prior = colour.request.candidates[0].prior;
	CHECK(NormalizeChromaticity(Vec3(1, 80.f / 300, 20.f / 300), prior.colour));
	prior.colourWeight = .01;
	prior.intervalWeight = 1e-8;
	prior.softLower = Vec3(10, 10, 10);
	prior.softUpper = Vec3(30, 30, 30);
	auto fitted = colour.Fit();
	CHECK(fitted.sources[0].compilerQ.x > 290);
	CHECK(RelativeError(colour, fitted) < .002);
	Record("soft_colour_and_uncertain_intensity_prior", colour, fitted);
}
void NonlinearCases()
{
	{
		Rig r;
		r.Add({0, 0, 160}, {300, 80, 20}, {400, 400, 400});
		r.Generate(A::Type::SurfaceEncoded);
		auto fit = r.Fit();
		CHECK(fit.linear);
		CHECK(fit.maximumIntervalError < 1e-4);
		for (size_t c = 0; c < 3; ++c)
		{
			const auto &s = fit.sources[0].conditional[c];
			double q = c == 0 ? 300 : c == 1 ? 80 : 20;
			CHECK(s.available);
			CHECK(s.lower <= q && s.upper >= q);
		}
		Record("linear_encoded_quantization_slices", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {300, 80, 20}, {400, 100, 100});
		r.request.candidates[0].lowerQ = Vec3(200, 0, 0);
		r.request.candidates[0].initialQ = Vec3(250, 64, 64);
		Bake::Recipe p;
		p.fast = true;
		r.request.profile = Bake::NetRadiantProfile(p);
		r.Generate(A::Type::SurfaceEncoded);
		auto fit = r.Fit();
		CHECK(fit.linear);
		CHECK(fit.maximumIntervalError < 1e-4);
		Record("proven_fast_positive_active_box", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {300, 80, 20});
		Bake::Recipe p;
		p.fast = true;
		r.request.profile = Bake::NetRadiantProfile(p);
		r.Generate(A::Type::SurfaceEncoded);
		auto fit = r.Fit();
		CHECK(!fit.linear);
		CHECK(RelativeError(r, fit) < .03);
		CHECK(fit.maximumIntervalError < 1.01);
		Record("fast_encoded", r, fit);
		r.request.options.method = A::Method::LinearOnly;
		A::Result bad;
		CHECK(A::Fit(r.request, {}, bad).code == Code::Unavailable);
		CHECK(!bad.fitted && bad.sources.empty());
	}
	{
		Bake::Recipe p;
		p.falloff = Bake::Falloff::Linear;
		p.normal = Bake::NormalResponse::None;
		Rig r(p);
		p.fast = true;
		r.request.profile = Bake::NetRadiantProfile(p);
		r.Add({0, 0, 160}, {350, 70, 35});
		r.Generate(A::Type::SurfaceEncoded);
		auto fit = r.Fit();
		CHECK(!fit.linear);
		CHECK(fit.maximumIntervalError < 1.01);
		CHECK(RelativeError(r, fit) < .03);
		Record("linear_falloff_nonlinear_fit", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {300, 100, 20}, {1200, 1200, 1200}, Bake::SourceKind::Spot);
		Bake::Recipe p;
		p.fast = true;
		r.request.profile = Bake::NetRadiantProfile(p);
		r.Generate(A::Type::SurfaceEncoded);
		auto fit = r.Fit();
		CHECK(!fit.linear);
		CHECK(fit.maximumIntervalError < 1.01);
		Record("spot_fast", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {4000, 2000, 1000}, {10000, 10000, 10000});
		r.Generate(A::Type::SurfaceEncoded);
		auto fit = r.Fit();
		CHECK(!fit.linear);
		CHECK(fit.coverage.censored > 0);
		CHECK(fit.maximumIntervalError < 1.01);
		CHECK(fit.sources[0].allocationAmbiguous);
		CHECK(fit.sources[0].compilerQ.y / fit.sources[0].compilerQ.x > .48 &&
			  fit.sources[0].compilerQ.y / fit.sources[0].compilerQ.x < .52);
		Record("shared_peak_censored", r, fit);
	}
	{
		Rig r;
		r.Add({0, 0, 160}, {1000, 500, 250}, {2000, 2000, 2000});
		r.Generate();
		for (auto &row : r.rows->data)
			for (auto &c : row.channels)
				if (c.lower > 100)
				{
					c.lower = 100;
					c.upper = std::numeric_limits<double>::infinity();
					c.censored = true;
				}
		auto fit = r.Fit();
		CHECK(fit.linear);
		CHECK(fit.maximumIntervalError < 1e-3);
		CHECK(fit.coverage.censored > 0);
		Record("linear_censored_lower_bounds", r, fit);
	}
}
class GeneratedRows final : public ChunkSource<A::Observation>
{
  public:
	size_t count = 25000;
	mutable size_t largest = 0;
	bool truncated = false;
	Error Read(uint64_t first, size_t n, const ReadContext &, Chunk<A::Observation> &out) const override
	{
		largest = std::max(largest, n);
		auto rows = std::make_shared<std::vector<A::Observation>>();
		rows->reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			uint64_t id = first + i;
			A::Observation a;
			a.id = id + 1;
			a.receiver.sample = a.id;
			a.receiver.surface = 1;
			a.receiver.mappedPlanar = a.receiver.compilerPlacementCertified = true;
			a.receiver.world = Vec3(float(int(id % 101) - 50) * 2, float(int((id / 101) % 101) - 50) * 2, 0);
			a.receiver.shadingNormal = a.receiver.geometricNormal = Vec3(0, 0, 1);
			a.type = A::Type::SurfaceRelative;
			a.region = 1;
			a.measure = id % 2 ? 4 : 1;
			a.encodingAttested = a.primariesComparable = a.comparisonScaleKnown = true;
			V d = V(0, 0, 160) - (V(a.receiver.world) + V(0, 0, 1));
			double distance = Length(d), coefficient = 7500 * (d.z / distance) / (distance * distance);
			for (size_t c = 0; c < 3; ++c)
				a.channels[c].lower = a.channels[c].upper = coefficient * (c == 0 ? 300 : c == 1 ? 80 : 20);
			rows->push_back(a);
		}
		out = Lease(rows);
		if (truncated && out.count)
			--out.count;
		return {};
	}
};
uint64_t largeRows = 0, largeAccounted = 0, largeRays = 0, largestRead = 0;
void StreamingAndControls()
{
	Rig r;
	r.Add({0, 0, 160}, {300, 80, 20});
	auto generated = std::make_shared<GeneratedRows>();
	r.request.observations = generated;
	r.request.count = generated->count;
	r.request.options.batchRows = 64;
	r.request.options.maxCacheBytes = 0;
	r.request.options.maxBytes = 8ull * 1024 * 1024;
	r.request.cache->Clear();
	A::Result out;
	ReadContext read;
	read.maxBytes = 8ull * 1024 * 1024;
	auto status = A::Fit(r.request, read, out);
	if (!status)
		std::printf("stream: %s\n", status.message.c_str());
	CHECK(status);
	CHECK(out.fitted && out.linear);
	CHECK(out.coverage.used == generated->count);
	CHECK(std::abs(out.sources[0].compilerQ.x - 300) < .01);
	CHECK(generated->largest <= 64);
	CHECK(r.request.cache->bytes == 0);
	CHECK(out.work.accountedBytes <= read.maxBytes);
	largeRows = out.coverage.used;
	largeAccounted = out.work.accountedBytes;
	largeRays = out.work.rays;
	largestRead = generated->largest;
	generated->truncated = true;
	CHECK(A::Fit(r.request, read, out).code == Code::ProviderError);
	CHECK(!out.fitted && out.sources.empty());
	generated->truncated = false;
	class CancelAfter final : public Cancellation
	{
	  public:
		mutable size_t calls = 0;
		bool IsCancelled() const noexcept override { return ++calls > 40; }
	} cancel;
	read.cancellation = &cancel;
	CHECK(A::Fit(r.request, read, out).code == Code::Cancelled);
	CHECK(!out.fitted && out.sources.empty());
	CHECK(out.coverage.used < generated->count);
	// Black, missing channels and a deliberately unregularized nuisance baseline.
	Rig black;
	black.Add({0, 0, 160}, {0, 0, 0});
	black.Generate();
	auto zero = black.Fit();
	CHECK(Peak(V(zero.sources[0].compilerQ)) == 0);
	CHECK(zero.coverage.positive == 0);
	Record("black_observations", black, zero);
	Rig partial;
	partial.Add({0, 0, 160}, {300, 80, 20});
	partial.Generate();
	for (auto &a : partial.rows->data)
		a.channels[2].present = false;
	auto missing = partial.Fit();
	CHECK(missing.spectrum.rank < missing.spectrum.parameters);
	CHECK(missing.sources[0].allocationAmbiguous);
	Record("missing_blue_channel", partial, missing);
	partial.request.options.baselineUpper = Vec3(30, 30, 30);
	partial.request.options.baselinePenalty = 0;
	CHECK(A::Fit(partial.request, {}, out).code == Code::InvalidInput);
	Rig overflow;
	overflow.Add({0, 0, 160}, {300, 80, 20});
	overflow.Generate();
	for (auto &row : overflow.rows->data)
		row.measure = std::numeric_limits<double>::max();
	CHECK(A::Fit(overflow.request, {}, out).code == Code::InvalidInput);
	CHECK(!out.fitted && out.sources.empty());
}
void GridAndFailure()
{
	Rig r;
	r.Add({-55, -20, 160}, {300, 30, 10});
	r.Add({65, 15, 120}, {10, 50, 250});
	r.Generate();
	auto surface = r.rows->data;
	r.Generate(A::Type::GridAmbientRelative, 3);
	auto grid = r.rows->data;
	r.Generate(A::Type::GridDirectedRelative, 3);
	auto directed = r.rows->data;
	auto combine = [&](bool scaleKnown) {
		r.rows->data = surface;
		for (auto a : grid)
		{
			a.id = r.rows->data.size() + 1;
			a.receiver.sample = a.id;
			a.comparisonScaleKnown = scaleKnown;
			a.predictionScale = 2;
			for (auto &c : a.channels)
			{
				c.lower *= 2;
				c.upper *= 2;
			}
			r.rows->data.push_back(a);
		}
		for (auto a : directed)
		{
			a.id = r.rows->data.size() + 1;
			a.receiver.sample = a.id;
			a.comparisonScaleKnown = scaleKnown;
			r.rows->data.push_back(a);
		}
		r.request.count = r.rows->data.size();
		r.request.options.typeWeight[size_t(A::Type::GridAmbientRelative)] = .2;
		r.request.options.typeWeight[size_t(A::Type::GridDirectedRelative)] = .2;
	};
	combine(false);
	auto omitted = r.Fit();
	CHECK(omitted.coverage.calibration == grid.size() + directed.size());
	CHECK(omitted.linear);
	CHECK(RelativeError(r, omitted) < .003);
	combine(true);
	auto joint = r.Fit();
	CHECK(!joint.linear);
	CHECK(joint.coverage.used == surface.size() + grid.size() + directed.size());
	CHECK(RelativeError(r, joint) < .02);
	Record("separate_unpacked_grid_operators", r, joint);
	Rig bad;
	bad.Add({0, 0, 160}, {300, 80, 20});
	bad.Generate();
	A::Result out;
	bad.request.profile = {};
	CHECK(A::Fit(bad.request, {}, out).code == Code::Unavailable);
	CHECK(!out.fitted && out.sources.empty());
	bad.request.profile = Bake::NetRadiantProfile();
	bad.room->unknown = true;
	CHECK(A::Fit(bad.request, {}, out).code == Code::Unavailable);
	CHECK(out.coverage.visibility > 0);
	CHECK(out.sources.empty());
	bad.room->unknown = false;
	// The provider's visibility meaning changed: immutable cache generations must
	// change too. Reusing the old key would correctly retain Unknown factors.
	bad.request.geometry.geometryKey = Key("restored-known-room-revision");
	bad.request.options.maxBytes = 32;
	CHECK(A::Fit(bad.request, {}, out).code == Code::MemoryLimit);
	bad.request.options.maxBytes = 64ull * 1024 * 1024;
	bad.request.options.maxEvaluations = 1;
	CHECK(A::Fit(bad.request, {}, out).code == Code::WorkLimit);
	CHECK(!out.fitted && out.sources.empty());
	bad.request.options.maxEvaluations = 10000;
	bad.rows->data[0].channels[0].lower = std::numeric_limits<double>::quiet_NaN();
	CHECK(A::Fit(bad.request, {}, out).code == Code::InvalidInput);
	bad.rows->data[0].channels[0].lower = 0;
	class Cancel final : public Cancellation
	{
		bool IsCancelled() const noexcept override { return true; }
	} cancel;
	ReadContext context;
	context.cancellation = &cancel;
	CHECK(A::Fit(bad.request, context, out).code == Code::Cancelled);
	CHECK(!out.fitted && out.sources.empty());
}
int main(int argc, char **argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	auto start = std::chrono::steady_clock::now();
	LinearCases();
	RobustAndPriors();
	NonlinearCases();
	GridAndFailure();
	StreamingAndControls();
	std::printf("S06 amplitude fit: %d checks, %d failures, %.3f s\n", checks, failures,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	if (argc > 1)
	{
		std::ofstream f(argv[1]);
		f << std::setprecision(12)
		  << "{\"schema\":\"org.openjk.amplitude-fit-tests\",\"version\":1,\"checks\":" << checks
		  << ",\"failures\":" << failures << ",\"cases\":[";
		for (size_t i = 0; i < witness.size(); ++i)
		{
			if (i)
				f << ',';
			const auto &w = witness[i];
			f << "{\"name\":\"" << w.name << "\",\"linear\":" << (w.linear ? "true" : "false")
			  << ",\"converged\":" << (w.converged ? "true" : "false")
			  << ",\"allocation_ambiguous\":" << (w.ambiguous ? "true" : "false")
			  << ",\"relative_q_error\":" << w.error << ",\"objective\":" << w.objective
			  << ",\"kkt\":" << w.kkt << ",\"strict_difference\":" << w.strict
			  << ",\"evaluations\":" << w.evaluations << ",\"rays\":" << w.rays << ",\"rank\":" << w.rank
			  << ",\"parameters\":" << w.parameters << '}';
		}
		f << "],\"streaming\":{\"rows\":" << largeRows << ",\"largest_read\":" << largestRead
		  << ",\"accounted_bytes\":" << largeAccounted << ",\"rays\":" << largeRays << "}}\n";
		if (!f)
			return 2;
	}
	return failures ? 1 : 0;
}
