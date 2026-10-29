// Fixed geometry/profile only. Linear proof precedes every matrix-free linear fit.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "amplitude_fit.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <functional>
#include <numeric>
#include <set>

namespace Rend2LightRecon
{
namespace Core
{
namespace Amplitude
{
using Bake::Detail::Clamp;
using Bake::Detail::Dot;
using Bake::Detail::Peak;
using Bake::Detail::Positive;
using Bake::Detail::V;
namespace
{
void Need(bool ok, Code code, const std::string &s)
{
	if (!ok)
		throw Failure(code, s);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
bool Grid(Type t) { return t == Type::GridAmbientRelative || t == Type::GridDirectedRelative; }
double Residual(double p, const Interval &i)
{
	return p < i.lower ? p - i.lower : p > i.upper ? p - i.upper : 0;
}
struct Rho
{
	double value = 0, gradient = 0;
};
Rho Robust(double e, const Options &o)
{
	Rho r;
	double a = std::abs(e), d = o.huber;
	if (o.loss == Loss::Squared)
	{
		r.value = .5 * e * e;
		r.gradient = e;
	}
	else if (o.loss == Loss::Huber)
	{
		r.value = a <= d ? .5 * e * e : d * (a - .5 * d);
		r.gradient = Clamp(e, -d, d);
	}
	else
	{
		double z = e / d;
		r.value = .5 * d * d * std::log1p(z * z);
		r.gradient = e / (1 + z * z);
	}
	return r;
}
bool Sum(uint64_t &a, uint64_t b, uint64_t cap)
{
	if (b > cap || a > cap - b)
		return false;
	a += b;
	return true;
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
double Channel(Vec3 v, size_t c) { return c == 0 ? v.x : c == 1 ? v.y : v.z; }
bool Zero(const Disk::Digest &d) { return d == Disk::Digest{}; }
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
struct Engine
{
	const Request &r;
	const Options &o;
	ReadContext read;
	Result result;
	Bake::Recipe recipe;
	Bake::CpuForwardEvaluator forward;
	std::shared_ptr<std::vector<Bake::SourceHypothesis>> sources;
	std::vector<Candidate> candidates;
	std::vector<uint8_t> admitted;
	std::vector<double> lower, upper, initial, diagonal;
	std::vector<bool> zeroResponse;
	std::shared_ptr<Cache> cache;
	size_t parameters = 0;
	bool baseline = false, linear = true;
	GeometryKey identity;
	Engine(const Request &request, const ReadContext &c) : r(request), o(r.options), read(c)
	{
		Check(Bake::DescribeRecipe(r.profile, recipe));
		Need(r.observations && !Zero(r.mapKey) && !Zero(r.observationKey) && r.count && r.count <= o.maxRows,
			 Code::InvalidInput, "Missing immutable training stream/identity or row limit");
		Need(r.candidates.size() <= o.maxSources && o.maxSources <= 128 && o.batchRows &&
				 o.batchRows <= 4096 && o.maxIterations && o.maxEvaluations && o.lineSearchSteps <= 64 &&
				 o.restarts && o.restarts <= 8,
			 Code::InvalidInput, "Invalid fitting count/work policy");
		Need(r.forward.mode == Bake::Mode::CompilerFaithful && !r.geometry.orderedGridReduction,
			 Code::Unavailable,
			 "Only strict implemented compiler operations; ordered grid compression is unsupported");
		Need(o.method <= Method::NonlinearOnly && o.loss <= Loss::Cauchy && std::isfinite(o.huber) &&
				 o.huber > 0 && std::isfinite(o.ridge) && o.ridge >= 0 && std::isfinite(o.baselinePenalty) &&
				 o.baselinePenalty >= 0,
			 Code::InvalidInput, "Invalid loss/prior policy");
		for (double v : {o.projectedKktTolerance, o.objectiveTolerance, o.parameterTolerance,
						 o.relativeImprovement, o.strictTolerance})
			Need(std::isfinite(v) && v >= 0, Code::InvalidInput, "Invalid stopping tolerance");
		for (double w : o.typeWeight)
			Need(std::isfinite(w) && w >= 0, Code::InvalidInput, "Invalid observation-type weight");
		Need(Positive(V(o.baselineUpper)), Code::InvalidInput, "Invalid baseline bounds");
		baseline = Peak(V(o.baselineUpper)) > 0;
		Need(!baseline || o.baselinePenalty > 0, Code::InvalidInput,
			 "An enabled nuisance baseline requires positive regularization");
		candidates = r.candidates;
		std::sort(candidates.begin(), candidates.end(),
				  [](const Candidate &a, const Candidate &b) { return a.source.id < b.source.id; });
		sources = std::make_shared<std::vector<Bake::SourceHypothesis>>();
		for (size_t j = 0; j < candidates.size(); ++j)
		{
			const auto &c = candidates[j];
			Need(c.source.id && (!j || c.source.id != candidates[j - 1].source.id), Code::InvalidInput,
				 "Duplicate/absent source ID");
			Need(c.source.amplitudeDomain == Bake::AmplitudeDomain::CompilerRelative, Code::Unavailable,
				 "Fit compiler Q explicitly; canonical photometry requires an independent documented "
				 "calibration mapping");
			Need(c.source.kind == Bake::SourceKind::Point || c.source.kind == Bake::SourceKind::Spot ||
					 c.source.kind == Bake::SourceKind::Polygon ||
					 c.source.kind == Bake::SourceKind::Directional,
				 Code::Unavailable, "No supported fixed-amplitude kernel for this candidate type");
			Need(Positive(V(c.lowerQ)) && Positive(V(c.upperQ)) && Positive(V(c.initialQ)) &&
					 Positive(V(c.prior.softLower)) && Positive(V(c.prior.softUpper)),
				 Code::InvalidInput, "Nonfinite/negative Q/prior");
			Need(std::isfinite(c.prior.intervalWeight) && c.prior.intervalWeight >= 0 &&
					 std::isfinite(c.prior.colourWeight) && c.prior.colourWeight >= 0,
				 Code::InvalidInput, "Invalid source prior weight");
			Need(!c.prior.colour.known ||
					 (Positive(V(c.prior.colour.rgb)) &&
					  std::abs(Dot(V(c.prior.colour.rgb), V(.2126, .7152, .0722)) - 1) < 1e-4),
				 Code::InvalidInput, "Invalid chromaticity prior");
			for (size_t k = 0; k < 3; ++k)
			{
				double a = Channel(c.lowerQ, k), b = Channel(c.upperQ, k), x = Channel(c.initialQ, k);
				Need(a <= b && b <= 1e9 && x >= a && x <= b &&
						 Channel(c.prior.softLower, k) <= Channel(c.prior.softUpper, k),
					 Code::InvalidInput, "Invalid amplitude/prior interval");
				lower.push_back(a);
				upper.push_back(b);
				initial.push_back(x);
			}
			auto s = c.source;
			s.amplitudeKnown = true;
			s.compilerRgbAmplitude = c.initialQ;
			sources->push_back(s);
		}
		if (baseline)
			for (size_t k = 0; k < 3; ++k)
			{
				lower.push_back(0);
				upper.push_back(Channel(o.baselineUpper, k));
				initial.push_back(0);
			}
		parameters = initial.size();
		Need(parameters > 0, Code::InvalidInput,
			 "No amplitude or explicitly regularized baseline parameters");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, o.maxBytes) : o.maxBytes;
		uint64_t scratch = uint64_t(o.batchRows) *
						   (sizeof(Observation) + sizeof(Bake::Prediction) + sizeof(Bake::Receiver) +
							sizeof(Bake::Factor) * (sources->size() + 1)) *
						   4;
		Need(Sum(scratch, r.count, cap) && Sum(scratch, o.maxCacheBytes, cap) &&
				 Sum(scratch, parameters * parameters * 64 + 2 * 1024 * 1024, cap),
			 Code::MemoryLimit, "Fitting stream/cache/numerical scratch memory preflight");
		read.maxBytes = cap;
		result.work.accountedBytes = scratch;
		admitted.assign(size_t(r.count), 0);
		zeroResponse.assign(candidates.size(), true);
		diagonal.assign(parameters, 0);
		cache = r.cache ? r.cache : std::make_shared<Cache>();
		if (cache->bytes > o.maxCacheBytes)
			cache->Clear();
		identity.U(kVersion);
		identity.Digest(r.mapKey);
		identity.Digest(r.observationKey);
		identity.Digest(r.geometry.geometryKey);
		identity.Digest(r.geometry.materialKey);
		identity.Digest(r.geometry.observationPolicyKey);
		identity.Digest(r.profile.contentKey);
		Text(identity, r.profile.id);
		for (const auto &c : candidates)
		{
			identity.U(c.source.id);
			identity.V(c.source.position.world);
			identity.V(c.source.orientation.forward);
			identity.V(c.lowerQ);
			identity.V(c.upperQ);
			identity.V(c.initialQ);
			identity.V(c.prior.softLower);
			identity.V(c.prior.softUpper);
			identity.F(c.prior.intervalWeight);
			identity.U(c.prior.colour.known);
			identity.V(c.prior.colour.rgb);
			identity.F(c.prior.colourWeight);
		}
		identity.U(uint32_t(o.method));
		identity.U(uint32_t(o.loss));
		identity.F(o.huber);
		identity.F(o.ridge);
		identity.V(o.baselineUpper);
		identity.F(o.baselinePenalty);
		identity.F(o.projectedKktTolerance);
		identity.F(o.objectiveTolerance);
		identity.F(o.parameterTolerance);
		identity.F(o.relativeImprovement);
		identity.F(o.strictTolerance);
		identity.U(o.maxIterations);
		identity.U(o.maxEvaluations);
		identity.U(o.nonlinearPasses);
		identity.U(o.lineSearchSteps);
		identity.U(o.restarts);
		identity.U(o.diagnostics);
		identity.U(o.diagnosticParameters);
		for (double v : o.typeWeight)
			identity.F(v);
		result.profileKey = r.profile.contentKey;
	}
	void Cancel()
	{
		Need(!(read.cancellation && read.cancellation->IsCancelled()), Code::Cancelled,
			 "Intensity fitting cancelled");
	}
	void Arithmetic(uint64_t n)
	{
		Need(Sum(result.work.arithmetic, n, o.maxArithmetic), Code::WorkLimit,
			 "Fitter arithmetic budget exceeded");
	}
	void Work(const Bake::Work &w)
	{
		Need(Sum(result.work.pairs, w.pairs, o.maxPairs) && Sum(result.work.rays, w.rays, o.maxRays) &&
				 Sum(result.work.traversal, w.nodeTests + w.triangleTests, o.maxTraversal),
			 Code::WorkLimit, "Fitter cumulative source/ray/traversal budget exceeded");
		result.work.cacheHits += w.cacheHits;
	}
	void Set(const std::vector<double> &x)
	{
		for (size_t j = 0; j < sources->size(); ++j)
			(*sources)[j].compilerRgbAmplitude = V(x[j * 3], x[j * 3 + 1], x[j * 3 + 2]).Float();
	}
	Bake::Options ForwardOptions(Type t, bool strict)
	{
		auto b = r.forward;
		b.mode = Bake::Mode::CompilerFaithful;
		b.output = Grid(t) ? Bake::Domain::GridAmbientDirected : Bake::Domain::CompilerLinearRelative255;
		b.unpackedGridOnly = Grid(t);
		b.factors.reset();
		b.contributions = nullptr;
		b.maxPairs = std::min<uint64_t>(b.maxPairs, o.maxPairs - result.work.pairs);
		b.maxRays = std::min<uint64_t>(b.maxRays, o.maxRays - result.work.rays);
		b.maxWork = std::min<uint64_t>(b.maxWork, o.maxTraversal - result.work.traversal);
		(void)strict;
		return b;
	}
	Chunk<Observation> Read(uint64_t first, size_t count)
	{
		Cancel();
		Chunk<Observation> b;
		Check(r.observations->Read(first, count, read, b));
		Need(b.count == count && b.owner && b.data, Code::ProviderError,
			 "Truncated or unowned observation block");
		result.work.rowReads += count;
		return b;
	}
	std::shared_ptr<const Bake::PreparedFactors> Factors(const Chunk<Bake::Receiver> &receivers,
														 Bake::Options &fo)
	{
		auto key = Bake::FactorDependencyKey(r.profile, Lease(sources), receivers, r.geometry, fo);
		auto found = cache->blocks.find(key);
		if (found != cache->blocks.end())
		{
			result.work.cacheHits += found->second->Records().size();
			return found->second;
		}
		std::shared_ptr<const Bake::PreparedFactors> p;
		++result.work.prepareCalls;
		Check(forward.Prepare(r.profile, Lease(sources), receivers, r.geometry, fo, read, p));
		Work(p->PreparationWork());
		uint64_t bytes = p->Records().size() * sizeof(Bake::Factor) + 256;
		if (bytes <= o.maxCacheBytes)
		{
			if (cache->bytes > o.maxCacheBytes - bytes)
				cache->Clear();
			cache->blocks.emplace(key, p);
			cache->bytes += bytes;
		}
		return p;
	}
	void Hash(const Observation &a)
	{
		identity.U(a.id);
		identity.U(a.region);
		identity.U(uint32_t(a.type));
		identity.U(a.receiver.sample);
		identity.U(a.receiver.surface);
		identity.V(a.receiver.world);
		identity.V(a.receiver.geometricNormal);
		identity.V(a.receiver.shadingNormal);
		identity.F(a.measure);
		identity.F(a.reliability);
		identity.F(a.sigma);
		identity.U(a.eligible);
		identity.U(a.encodingAttested);
		identity.U(a.primariesComparable);
		identity.U(a.comparisonScaleKnown);
		identity.F(a.predictionScale);
		identity.V(a.knownPreencoding);
		for (const auto &i : a.channels)
		{
			identity.U(i.present);
			identity.F(i.lower);
			if (std::isfinite(i.upper))
				identity.F(i.upper);
			else
				identity.U(UINT64_MAX);
			identity.U(i.censored);
		}
	}
	bool Eligible(const Observation &a)
	{
		Need(a.id && a.receiver.sample && size_t(a.type) < size_t(Type::Count) && std::isfinite(a.measure) &&
				 a.measure >= 0 && std::isfinite(a.reliability) && a.reliability >= 0 && a.reliability <= 1 &&
				 std::isfinite(a.sigma) && a.sigma > 0 && std::isfinite(a.predictionScale) &&
				 a.predictionScale > 0 && Positive(V(a.knownPreencoding)),
			 Code::InvalidInput, "Invalid observation geometry/weight/calibration");
		bool present = false;
		for (const auto &i : a.channels)
		{
			if (!i.present)
				continue;
			present = true;
			Need(std::isfinite(i.lower) && i.lower >= 0 && i.upper >= i.lower &&
					 (std::isfinite(i.upper) ||
					  (i.censored && i.upper == std::numeric_limits<double>::infinity())),
				 Code::InvalidInput, "Invalid quantization/censor interval");
		}
		if (!a.eligible || !present || a.measure == 0 || a.reliability == 0)
		{
			++result.coverage.missing;
			return false;
		}
		if (!a.encodingAttested || !a.primariesComparable)
		{
			++result.coverage.encoding;
			return false;
		}
		if (!a.comparisonScaleKnown)
		{
			++result.coverage.calibration;
			return false;
		}
		Need(a.type != Type::SurfaceEncoded || a.predictionScale == 1, Code::InvalidInput,
			 "Encoded RGB scale must use the profile encoding, not a post-encoding photometric gain");
		if (!o.typeWeight[size_t(a.type)])
		{
			++result.coverage.disabled;
			return false;
		}
		Need(!Grid(a.type) || Peak(V(a.knownPreencoding)) == 0, Code::Unavailable,
			 "Grid terms require their own accumulation/reduction operator; no surface RGB baseline");
		return true;
	}
	void Discover()
	{
		Cancel();
		result.coverage.requested = r.count;
		Id last = 0;
		for (uint64_t first = 0; first < r.count; first += o.batchRows)
		{
			size_t n = size_t(std::min<uint64_t>(o.batchRows, r.count - first));
			auto input = Read(first, n);
			for (size_t i = 0; i < n; ++i)
			{
				const auto &a = input.data[i];
				Need(a.id > last, Code::InvalidInput,
					 "Training observation IDs must be strictly ordered/unique");
				last = a.id;
				Hash(a);
				if (Eligible(a))
					admitted[size_t(first) + i] = 1;
			}
			for (size_t t = 0; t < size_t(Type::Count); ++t)
			{
				auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
				std::vector<size_t> indices;
				auto fo = ForwardOptions(Type(t), false);
				for (size_t i = 0; i < n; ++i)
				{
					const auto &a = input.data[i];
					if (!admitted[size_t(first) + i] || size_t(a.type) != t)
						continue;
					bool supported = true;
					for (const auto &s : *sources)
						if (!Bake::Capability(r.profile, s.kind, a.receiver, fo).implemented)
							supported = false;
					if (sources->empty() &&
						!Bake::Capability(r.profile, Bake::SourceKind::Point, a.receiver, fo).implemented)
						supported = false;
					if (!supported)
					{
						admitted[size_t(first) + i] = 0;
						++result.coverage.geometry;
						continue;
					}
					auto rec = a.receiver;
					rec.domain = fo.output;
					receivers->push_back(rec);
					indices.push_back(i);
				}
				if (receivers->empty())
					continue;
				auto factors = Factors(Lease(receivers), fo);
				identity.Digest(factors->Key());
				for (size_t k = 0; k < indices.size(); ++k)
				{
					size_t i = indices[k];
					const auto &a = input.data[i];
					bool known = true;
					for (size_t j = 0; j < sources->size(); ++j)
						known &= factors->Records()[k * sources->size() + j].visibility !=
								 VisibilityState::Unknown;
					if (!known)
					{
						admitted[size_t(first) + i] = 0;
						++result.coverage.visibility;
						continue;
					}
					++result.coverage.used;
					result.coverage.measure[t] += a.measure * a.reliability;
					Need(std::isfinite(result.coverage.measure[t]), Code::InvalidInput,
						 "Observation measure sum overflow");
					for (const auto &v : a.channels)
						if (v.present)
						{
							++result.coverage.channels;
							result.coverage.censored += v.censored;
							result.coverage.positive += v.lower > 0;
						}
					V maximum = V(recipe.explicitSurfaceAmbient) + V(a.knownPreencoding) + V(o.baselineUpper);
					for (size_t j = 0; j < sources->size(); ++j)
					{
						const auto &factor = factors->Records()[k * sources->size() + j];
						Bake::LinearResponse response;
						if (Grid(a.type))
						{
							linear = false;
							zeroResponse[j] =
								zeroResponse[j] && factor.visibility == VisibilityState::Blocked;
							continue;
						}
						Check(Bake::ProveLinearResponse(recipe, (*sources)[j], factor, candidates[j].lowerQ,
														candidates[j].upperQ, response));
						linear &= response.proven;
						zeroResponse[j] = zeroResponse[j] && response.proven && response.inactive;
						if (!response.proven && result.diagnostics.size() < 12)
							result.diagnostics.push_back(response.reason);
						maximum = maximum + V(candidates[j].upperQ) * response.coefficient;
					}
					if (a.type == Type::SurfaceEncoded && (recipe.gamma != 1 || recipe.exposure != 0 ||
														   recipe.srgbOutput || Peak(maximum) > 255))
					{
						linear = false;
						if (result.diagnostics.size() < 12)
							result.diagnostics.push_back(
								"Nonlinear/coupled output encoding is reachable over the declared Q box");
					}
				}
			}
		}
		Need(result.coverage.used > 0 && result.coverage.channels > 0, Code::Unavailable,
			 "No comparable supported observations; missing data are not black predictions");
		result.linear = linear && o.method != Method::NonlinearOnly && o.loss != Loss::Cauchy;
		Need(o.method != Method::LinearOnly || result.linear, Code::Unavailable,
			 "LinearOnly refused: active-branch/output admissibility was not proven");
		for (size_t j = 0; j < zeroResponse.size(); ++j)
			if (zeroResponse[j])
				for (size_t k = 0; k < 3; ++k)
					initial[j * 3 + k] = lower[j * 3 + k];
		result.key = identity.Finish();
	}
	double Weight(const Observation &a) const
	{
		double weight = (a.measure * a.reliability / result.coverage.measure[size_t(a.type)]) *
						(o.typeWeight[size_t(a.type)] / 3);
		Need(std::isfinite(weight), Code::InvalidInput, "Nonfinite observation weight");
		return weight;
	}
	typedef std::function<void(const Observation &, V, const std::vector<double> &)> Visitor;
	void Scan(const std::vector<double> &x, bool strict, bool useLinear, const Visitor &visitor)
	{
		Cancel();
		Set(x);
		++result.work.passes;
		for (uint64_t first = 0; first < r.count; first += o.batchRows)
		{
			size_t n = size_t(std::min<uint64_t>(o.batchRows, r.count - first));
			auto input = Read(first, n);
			for (size_t t = 0; t < size_t(Type::Count); ++t)
			{
				auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
				std::vector<size_t> indices;
				auto fo = ForwardOptions(Type(t), strict);
				for (size_t i = 0; i < n; ++i)
					if (admitted[size_t(first) + i] && size_t(input.data[i].type) == t)
					{
						auto receiver = input.data[i].receiver;
						receiver.domain = fo.output;
						receivers->push_back(receiver);
						indices.push_back(i);
					}
				if (indices.empty())
					continue;
				std::shared_ptr<const Bake::PreparedFactors> factors;
				factors = Factors(Lease(receivers), fo);
				Bake::Result prediction, cached;
				if (!useLinear)
				{
					fo.factors = strict ? nullptr : factors;
					++result.work.forwardCalls;
					Check(forward.Evaluate(r.profile, Lease(sources), Lease(receivers), Decode::Profile(),
										   r.geometry, fo, read, prediction));
					Work(prediction.work);
					Need(prediction.predictions.count == indices.size(), Code::ProviderError,
						 "Missing forward prediction coverage");
					if (strict && !result.linear)
					{
						fo = ForwardOptions(Type(t), false);
						fo.factors = factors;
						++result.work.forwardCalls;
						Check(forward.Evaluate(r.profile, Lease(sources), Lease(receivers), Decode::Profile(),
											   r.geometry, fo, read, cached));
						Work(cached.work);
					}
				}
				std::vector<double> coefficients(sources->size());
				for (size_t k = 0; k < indices.size(); ++k)
				{
					const auto &a = input.data[indices[k]];
					V value;
					V reference;
					if (useLinear || (strict && result.linear))
					{
						Need(Sum(result.work.pairs, sources->size(), o.maxPairs), Code::WorkLimit,
							 "Linear response-pair budget exceeded");
						value = V(recipe.explicitSurfaceAmbient);
						for (size_t j = 0; j < sources->size(); ++j)
						{
							Bake::LinearResponse response;
							Check(Bake::ProveLinearResponse(
								recipe, (*sources)[j], factors->Records()[k * sources->size() + j],
								candidates[j].lowerQ, candidates[j].upperQ, response));
							Need(response.proven, Code::InvalidState, "Linear branch support changed");
							coefficients[j] = response.coefficient;
							value = value + V(x[j * 3], x[j * 3 + 1], x[j * 3 + 2]) * response.coefficient;
						}
					}
					if (!useLinear)
					{
						reference = value;
						const auto &p = prediction.predictions.data[k];
						Need(p.available && !p.surrogate, Code::Unavailable,
							 "Unsupported/Unknown forward prediction is not a black observation");
						value = Grid(a.type)
									? V(a.type == Type::GridAmbientRelative ? p.gridAmbient : p.gridDirected)
									: V(p.compilerRelative);
					}
					auto finish = [&](V value, bool linearValue) {
						if (!Grid(a.type))
						{
							value = value + V(a.knownPreencoding);
							if (baseline)
								value = value + V(x[parameters - 3], x[parameters - 2], x[parameters - 1]);
							if (a.type == Type::SurfaceEncoded)
							{
								if (linearValue)
									value = value / recipe.compensate;
								else
								{
									Vec3 encoded;
									uint8_t packed[3];
									Check(Bake::Encode(recipe, value.Float(), encoded, packed));
									value = V(encoded);
								}
							}
						}
						return value * a.predictionScale;
					};
					value = finish(value, useLinear);
					if (strict)
					{
						if (!result.linear)
						{
							const auto &cp = cached.predictions.data[k];
							Need(cp.available, Code::Unavailable, "Cached reference prediction unavailable");
							reference = Grid(a.type)
											? V(a.type == Type::GridAmbientRelative ? cp.gridAmbient
																					: cp.gridDirected)
											: V(cp.compilerRelative);
						}
						reference = finish(reference, result.linear);
						for (size_t c = 0; c < 3; ++c)
							result.strictDifference =
								std::max(result.strictDifference, std::abs(value[c] - reference[c]));
					}
					Need(Positive(value), Code::AlgorithmError, "Nonfinite/negative fitting prediction");
					Arithmetic(sources->size() * 3 + 3);
					visitor(a, value, coefficients);
				}
			}
		}
	}
	double Coefficient(const Observation &a, const std::vector<double> &c, size_t parameter,
					   size_t channel) const
	{
		if (parameter % 3 != channel)
			return 0;
		double v = parameter / 3 < sources->size() ? c[parameter / 3] : 1;
		return v * a.predictionScale / (a.type == Type::SurfaceEncoded ? recipe.compensate : 1);
	}
	Terms Priors(const std::vector<double> &x, std::vector<double> *gradient = nullptr,
				 std::vector<double> *lipschitz = nullptr)
	{
		Terms terms;
		for (size_t k = 0; k < parameters; ++k)
		{
			double lambda = k < sources->size() * 3 ? o.ridge : o.baselinePenalty;
			double term = .5 * lambda * x[k] * x[k];
			if (k < sources->size() * 3)
				terms.ridge += term;
			else
				terms.baselinePrior += term;
			if (gradient)
				(*gradient)[k] += lambda * x[k];
			if (lipschitz)
				(*lipschitz)[k] += lambda;
		}
		for (size_t j = 0; j < candidates.size(); ++j)
		{
			const auto &p = candidates[j].prior;
			V q(x[j * 3], x[j * 3 + 1], x[j * 3 + 2]), direction(p.colour.rgb);
			double length = std::sqrt(Dot(direction, direction));
			V orthogonal = length > 0 ? q - direction * (Dot(q, direction) / (length * length)) : V();
			for (size_t k = 0; k < 3; ++k)
			{
				Interval bound;
				bound.lower = Channel(p.softLower, k);
				bound.upper = Channel(p.softUpper, k);
				double e = Residual(x[j * 3 + k], bound);
				terms.sourcePrior += .5 * p.intervalWeight * e * e;
				if (gradient)
					(*gradient)[j * 3 + k] += p.intervalWeight * e;
				if (lipschitz)
					(*lipschitz)[j * 3 + k] += p.intervalWeight;
				if (p.colour.known)
				{
					terms.colourPrior += .5 * p.colourWeight * orthogonal[k] * orthogonal[k];
					if (gradient)
						(*gradient)[j * 3 + k] += p.colourWeight * orthogonal[k];
					if (lipschitz)
						(*lipschitz)[j * 3 + k] +=
							p.colourWeight * (1 - direction[k] * direction[k] / (length * length));
				}
			}
		}
		return terms;
	}
	Terms Evaluate(const std::vector<double> &x, bool strict = false, bool details = false,
				   std::vector<V> *values = nullptr)
	{
		Need(result.work.objectiveEvaluations < o.maxEvaluations, Code::WorkLimit,
			 "Intensity objective evaluation budget");
		++result.work.objectiveEvaluations;
		Terms terms = Priors(x);
		double maximum = 0;
		std::map<std::pair<Id, Type>, GroupTerms> groups;
		Scan(x, strict, result.linear && !strict,
			 [&](const Observation &a, V p, const std::vector<double> &) {
				 if (values)
					 values->push_back(p);
				 double w = Weight(a), rowLoss = 0, squared = 0;
				 uint64_t count = 0;
				 for (size_t k = 0; k < 3; ++k)
					 if (a.channels[k].present)
					 {
						 double e = Residual(p[k], a.channels[k]);
						 maximum = std::max(maximum, std::abs(e));
						 auto rho = Robust(e / a.sigma, o);
						 rowLoss += w * rho.value;
						 squared += w * e * e;
						 ++count;
					 }
				 terms.data += rowLoss;
				 if (details)
				 {
					 Need(groups.size() < 4096 || groups.count({a.region, a.type}), Code::MemoryLimit,
						  "Per-region diagnostic group limit");
					 auto &g = groups[{a.region, a.type}];
					 g.region = a.region;
					 g.type = a.type;
					 g.objective += rowLoss;
					 g.weightedSquared += squared;
					 g.measure += a.measure;
					 g.channels += count;
				 }
			 });
		Need(std::isfinite(terms.Total()), Code::AlgorithmError, "Nonfinite objective");
		if (details)
		{
			result.maximumIntervalError = maximum;
			result.groups.clear();
			for (const auto &g : groups)
				result.groups.push_back(g.second);
		}
		return terms;
	}
	std::vector<double> Gradient(const std::vector<double> &x, bool initialize,
								 std::vector<double> *active = nullptr)
	{
		std::vector<double> g(parameters, 0), l(parameters, 0);
		Priors(x, &g, initialize ? &l : nullptr);
		if (active)
		{
			active->assign(parameters, 0);
			Priors(x, nullptr, active);
		}
		Scan(x, false, true, [&](const Observation &a, V p, const std::vector<double> &coefficient) {
			double w = Weight(a);
			for (size_t k = 0; k < 3; ++k)
				if (a.channels[k].present)
				{
					double e = Residual(p[k], a.channels[k]) / a.sigma;
					auto rho = Robust(e, o);
					for (size_t j = k; j < parameters; j += 3)
					{
						double c = Coefficient(a, coefficient, j, k);
						g[j] += w * rho.gradient * c / a.sigma;
						if (initialize)
							l[j] += w * c * c / (a.sigma * a.sigma);
						if (active && e != 0)
							(*active)[j] +=
								w * c * c / (a.sigma * a.sigma) *
								(o.loss == Loss::Huber ? std::min(1., o.huber / std::abs(e)) : 1.);
						Arithmetic(1);
					}
				}
		});
		if (initialize)
			diagonal = l;
		return g;
	}
	double Kkt(const std::vector<double> &x, const std::vector<double> &g)
	{
		double worst = 0;
		for (size_t k = 0; k < parameters; ++k)
		{
			double v = g[k];
			if ((x[k] <= lower[k] && v >= 0) || (x[k] >= upper[k] && v <= 0))
				v = 0;
			worst = std::max(worst, std::abs(v) / std::sqrt(std::max(diagonal[k], 1e-30)));
		}
		return worst;
	}
	std::vector<double> Linear()
	{
		result.projectedKktAvailable = true;
		auto x = initial;
		auto g = Gradient(x, true);
		double f = Evaluate(x).Total();
		result.initialObjective = f;
		result.history.push_back(f);
		for (size_t sweep = 0; sweep < o.maxIterations; ++sweep)
		{
			Cancel();
			result.projectedKkt = Kkt(x, g);
			if (result.projectedKkt <= o.projectedKktTolerance)
			{
				result.stop = Stop::ProjectedKkt;
				result.converged = true;
				break;
			}
			double before = f, motion = 0;
			for (size_t k = 0; k < parameters; ++k)
			{
				if (diagonal[k] <= 0 || lower[k] == upper[k])
					continue;
				std::vector<double> active;
				g = Gradient(x, false, &active);
				double curvature = std::max(active[k], diagonal[k] * 1e-12), start = x[k];
				bool accepted = false;
				for (unsigned line = 0; line < 14; ++line)
				{
					auto trial = x;
					trial[k] = Clamp(start - g[k] / curvature, lower[k], upper[k]);
					if (trial[k] == start)
					{
						accepted = true;
						break;
					}
					double value = Evaluate(trial).Total();
					if (value <= f + 1e-14 * std::max(1., f))
					{
						motion = std::max(motion, std::abs(trial[k] - start));
						x = trial;
						f = value;
						accepted = true;
						break;
					}
					curvature *= 2;
				}
				if (!accepted)
				{
					auto trial = x;
					trial[k] = Clamp(start - g[k] / diagonal[k], lower[k], upper[k]);
					double value = Evaluate(trial).Total();
					Need(value <= f + 1e-10 * std::max(1., f), Code::AlgorithmError,
						 "Convex coordinate safeguard failed");
					motion = std::max(motion, std::abs(trial[k] - start));
					x = trial;
					f = value;
				}
			}
			f = Evaluate(x).Total();
			Need(f <= before + 1e-8 * std::max(1.0, before), Code::AlgorithmError,
				 "Linear convex coordinate objective increased");
			result.history.push_back(f);
			g = Gradient(x, false);
			if (f <= o.objectiveTolerance)
			{
				result.stop = Stop::IntervalFeasible;
				result.converged = true;
				break;
			}
			if (std::abs(before - f) <= o.relativeImprovement * std::max(1.0, before) &&
				motion <= o.parameterTolerance)
			{
				result.stop = Stop::Stagnated;
				break;
			}
			if (sweep + 1 == o.maxIterations)
				result.stop = Stop::IterationLimit;
		}
		result.projectedKkt = Kkt(x, Gradient(x, false));
		if (result.projectedKkt <= o.projectedKktTolerance)
		{
			result.stop = Stop::ProjectedKkt;
			result.converged = true;
		}
		return x;
	}
	std::vector<double> Solve(std::vector<double> a, std::vector<double> b)
	{
		size_t n = b.size();
		for (size_t i = 0; i < n; ++i)
		{
			size_t pivot = i;
			for (size_t j = i + 1; j < n; ++j)
				if (std::abs(a[j * n + i]) > std::abs(a[pivot * n + i]))
					pivot = j;
			if (std::abs(a[pivot * n + i]) < 1e-30)
				return std::vector<double>(n, 0);
			for (size_t k = i; k < n; ++k)
				std::swap(a[i * n + k], a[pivot * n + k]);
			std::swap(b[i], b[pivot]);
			double d = a[i * n + i];
			for (size_t k = i; k < n; ++k)
				a[i * n + k] /= d;
			b[i] /= d;
			for (size_t j = 0; j < n; ++j)
				if (j != i)
				{
					double s = a[j * n + i];
					for (size_t k = i; k < n; ++k)
						a[j * n + k] -= s * a[i * n + k];
					b[j] -= s * b[i];
				}
		}
		return b;
	}
	void NonlinearWarm(std::vector<double> &x, double &f)
	{
		if (parameters > 24 || result.coverage.used > 512)
			return;
		for (unsigned round = 0; round < 6 && f > o.objectiveTolerance; ++round)
		{
			std::vector<Observation> rows;
			std::vector<V> centre;
			Scan(x, false, false, [&](const Observation &a, V p, const std::vector<double> &) {
				rows.push_back(a);
				centre.push_back(p);
			});
			size_t m = rows.size();
			std::vector<double> jac(parameters * m * 3, 0);
			for (size_t k = 0; k < parameters; ++k)
			{
				auto a = x, b = x;
				double h = std::max(1e-3, std::min(1.0, (upper[k] - lower[k]) * .001));
				a[k] = std::min(upper[k], x[k] + h);
				b[k] = std::max(lower[k], x[k] - h);
				if (a[k] == b[k])
					continue;
				std::vector<V> pa, pb;
				Evaluate(a, false, false, &pa);
				Evaluate(b, false, false, &pb);
				Need(pa.size() == m && pb.size() == m, Code::InvalidState,
					 "Changing derivative observation coverage");
				for (size_t i = 0; i < m; ++i)
					for (size_t c = 0; c < 3; ++c)
						jac[(k * m + i) * 3 + c] = (pa[i][c] - pb[i][c]) / (a[k] - b[k]);
			}
			std::vector<double> gram(parameters * parameters, 0), g(parameters, 0), priorL(parameters, 0);
			Priors(x, &g, &priorL);
			for (size_t i = 0; i < m; ++i)
				for (size_t c = 0; c < 3; ++c)
					if (rows[i].channels[c].present)
					{
						double e = Residual(centre[i][c], rows[i].channels[c]) / rows[i].sigma;
						auto rho = Robust(e, o);
						double weight = Weight(rows[i]);
						double irls = e != 0 ? rho.gradient / e : 1;
						for (size_t j = 0; j < parameters; ++j)
						{
							double a = jac[(j * m + i) * 3 + c] / rows[i].sigma;
							g[j] += weight * a * rho.gradient;
							for (size_t k = 0; k < parameters; ++k)
								gram[j * parameters + k] +=
									weight * irls * a * jac[(k * m + i) * 3 + c] / rows[i].sigma;
						}
						Arithmetic(parameters * parameters);
					}
			double diagonalMax = 0;
			for (size_t k = 0; k < parameters; ++k)
				diagonalMax = std::max(diagonalMax, gram[k * parameters + k]);
			for (size_t k = 0; k < parameters; ++k)
			{
				gram[k * parameters + k] += priorL[k] + std::max(1e-12, diagonalMax * 1e-7);
				g[k] = -g[k];
			}
			auto step = Solve(gram, g);
			bool accepted = false;
			for (double alpha : {1., .5, .25, .125, .0625})
			{
				auto trial = x;
				for (size_t k = 0; k < parameters; ++k)
					trial[k] = Clamp(x[k] + alpha * Clamp(step[k], -500, 500), lower[k], upper[k]);
				double value = Evaluate(trial).Total();
				if (value < f)
				{
					x = trial;
					f = value;
					accepted = true;
					result.history.push_back(std::min(result.history.back(), f));
					break;
				}
			}
			if (!accepted)
				break;
		}
	}
	std::vector<double> Nonlinear()
	{
		std::vector<double> best = initial;
		double bestValue = Evaluate(best).Total();
		result.initialObjective = bestValue;
		result.history.push_back(bestValue);
		Stop bestStop = Stop::IterationLimit;
		for (size_t restart = 0; restart < o.restarts; ++restart)
		{
			auto x = initial;
			if (restart)
				for (size_t k = 0; k < parameters; ++k)
					x[k] = zeroResponse.size() > k / 3 && zeroResponse[k / 3]
							   ? lower[k]
							   : lower[k] + (upper[k] - lower[k]) * (restart == 1 ? .32 : .68);
			double f = Evaluate(x).Total();
			NonlinearWarm(x, f);
			Stop stop = Stop::IterationLimit;
			for (size_t pass = 0; pass < o.nonlinearPasses; ++pass)
			{
				Cancel();
				double before = f;
				auto old = x;
				if (f <= o.objectiveTolerance)
				{
					stop = Stop::IntervalFeasible;
					break;
				}
				for (size_t k = 0; k < parameters; ++k)
				{
					if (lower[k] == upper[k] || (k / 3 < zeroResponse.size() && zeroResponse[k / 3]))
						continue;
					std::vector<double> knots = {lower[k], upper[k], x[k]};
					for (double fraction : {.025, .08, .16, .32, .64})
						knots.push_back(lower[k] + fraction * (upper[k] - lower[k]));
					std::sort(knots.begin(), knots.end());
					knots.erase(std::unique(knots.begin(), knots.end()), knots.end());
					size_t at = 0;
					double value = f, chosen = x[k];
					for (size_t i = 0; i < knots.size(); ++i)
					{
						auto trial = x;
						trial[k] = knots[i];
						double v = Evaluate(trial).Total();
						if (v < value)
						{
							chosen = knots[i];
							value = v;
							at = i;
						}
						else if (knots[i] == chosen)
							at = i;
					}
					double lo = knots[at ? at - 1 : 0], hi = knots[std::min(at + 1, knots.size() - 1)];
					const double ratio = .6180339887498948482;
					double a = hi - ratio * (hi - lo), b = lo + ratio * (hi - lo);
					auto sample = [&](double z) {
						auto trial = x;
						trial[k] = z;
						return Evaluate(trial).Total();
					};
					double fa = sample(a), fb = sample(b);
					for (size_t step = 0; step < o.lineSearchSteps && hi - lo > o.parameterTolerance; ++step)
					{
						if (fa <= fb)
						{
							hi = b;
							b = a;
							fb = fa;
							a = hi - ratio * (hi - lo);
							fa = sample(a);
						}
						else
						{
							lo = a;
							a = b;
							fa = fb;
							b = lo + ratio * (hi - lo);
							fb = sample(b);
						}
					}
					for (auto point : {std::make_pair(a, fa), std::make_pair(b, fb)})
						if (point.second < value)
						{
							chosen = point.first;
							value = point.second;
						}
					x[k] = chosen;
					f = value;
				}
				result.history.push_back(std::min(bestValue, f));
				double motion = 0;
				for (size_t k = 0; k < parameters; ++k)
					motion = std::max(motion, std::abs(x[k] - old[k]));
				if (f <= o.objectiveTolerance)
				{
					stop = Stop::IntervalFeasible;
					break;
				}
				if (before - f <= o.relativeImprovement * std::max(1.0, before) &&
					motion <= o.parameterTolerance)
				{
					stop = Stop::ParameterStep;
					break;
				}
				if (pass + 1 == o.nonlinearPasses)
					stop = Stop::IterationLimit;
			}
			if (f < bestValue || (f == bestValue && restart == 0))
			{
				best = x;
				bestValue = f;
				bestStop = stop;
			}
			if (bestValue <= o.objectiveTolerance)
				break;
		}
		result.stop = bestStop;
		result.converged = bestStop == Stop::IntervalFeasible || bestStop == Stop::ParameterStep;
		return best;
	}
	Spectrum SpectrumOf(std::vector<double> a, const std::string &scope)
	{
		Spectrum s;
		s.available = true;
		s.parameters = parameters;
		s.scope = scope;
		size_t n = parameters;
		for (size_t sweep = 0; sweep < 80 * n * n; ++sweep)
		{
			size_t p = 0, q = 0;
			double largest = 0;
			for (size_t i = 0; i < n; ++i)
				for (size_t j = i + 1; j < n; ++j)
					if (std::abs(a[i * n + j]) > largest)
					{
						largest = std::abs(a[i * n + j]);
						p = i;
						q = j;
					}
			double diag = 0;
			for (size_t i = 0; i < n; ++i)
				diag = std::max(diag, std::abs(a[i * n + i]));
			if (largest <= std::max(1e-30, diag * 1e-14))
				break;
			double angle = .5 * std::atan2(2 * a[p * n + q], a[q * n + q] - a[p * n + p]),
				   c = std::cos(angle), v = std::sin(angle), app = a[p * n + p], aqq = a[q * n + q],
				   apq = a[p * n + q];
			for (size_t k = 0; k < n; ++k)
				if (k != p && k != q)
				{
					double x = a[k * n + p], y = a[k * n + q];
					a[k * n + p] = a[p * n + k] = c * x - v * y;
					a[k * n + q] = a[q * n + k] = v * x + c * y;
				}
			a[p * n + p] = c * c * app - 2 * c * v * apq + v * v * aqq;
			a[q * n + q] = v * v * app + 2 * c * v * apq + c * c * aqq;
			a[p * n + q] = a[q * n + p] = 0;
		}
		for (size_t i = 0; i < n; ++i)
			s.singular.push_back(std::sqrt(std::max(0.0, a[i * n + i])));
		std::sort(s.singular.rbegin(), s.singular.rend());
		double cutoff = s.singular.empty() ? 0 : s.singular[0] * s.relativeThreshold;
		for (double v : s.singular)
			s.rank += v > std::max(1e-15, cutoff);
		s.conditionKnown = s.rank == n && n > 0;
		if (s.conditionKnown)
			s.condition = s.singular.front() / s.singular.back();
		return s;
	}
	void Diagnostics(const std::vector<double> &x)
	{
		if (!o.diagnostics)
			return;
		if (parameters > o.diagnosticParameters || result.coverage.used > 512 && !result.linear)
		{
			result.diagnostics.push_back("Spectrum/conditional slices unavailable at declared diagnostic "
										 "parameter/row budget; not an assertion of full rank");
			return;
		}
		uint64_t gramWork = result.coverage.used * parameters * parameters * 3;
		if (gramWork > o.maxArithmetic - result.work.arithmetic)
		{
			result.diagnostics.push_back("Spectrum arithmetic budget unavailable");
			return;
		}
		std::vector<double> gram(parameters * parameters, 0);
		std::vector<Slice> slices(parameters);
		for (size_t k = 0; k < parameters; ++k)
		{
			slices[k].available = result.linear;
			slices[k].lower = lower[k];
			slices[k].upper = upper[k];
			slices[k].upperIsSearchLimit = true;
		}
		if (result.linear)
		{
			Scan(x, false, true, [&](const Observation &a, V p, const std::vector<double> &c) {
				double w = Weight(a);
				for (size_t k = 0; k < 3; ++k)
					if (a.channels[k].present)
					{
						for (size_t i = k; i < parameters; i += 3)
						{
							double ci = Coefficient(a, c, i, k);
							for (size_t j = k; j < parameters; j += 3)
								gram[i * parameters + j] +=
									w * ci * Coefficient(a, c, j, k) / (a.sigma * a.sigma);
							if (ci > 0)
							{
								double lo = x[i] + (a.channels[k].lower - p[k]) / ci,
									   hi = x[i] + (a.channels[k].upper - p[k]) / ci;
								slices[i].lower = std::max(slices[i].lower, lo);
								if (hi < slices[i].upper)
								{
									slices[i].upper = hi;
									slices[i].upperIsSearchLimit = false;
								}
							}
						}
						Arithmetic(parameters * parameters);
					}
			});
			result.spectrum = SpectrumOf(gram, "Unregularized linear response design, including "
											   "interval-interior rows; not Fisher information");
			for (size_t i = 0; i < candidates.size(); ++i)
				for (size_t j = i + 1; j < candidates.size(); ++j)
					for (size_t channel = 0; channel < 3; ++channel)
					{
						size_t a = 3 * i + channel, b = 3 * j + channel;
						double aa = gram[a * parameters + a], bb = gram[b * parameters + b],
							   ab = gram[a * parameters + b];
						if (aa <= 0 || bb <= 0 || ab <= 0)
							continue;
						double ratio = ab / bb,
							   residual = std::max(0.0, aa - 2 * ratio * ab + ratio * ratio * bb);
						if (residual > aa * 1e-12)
							continue;
						AllocationExchange exchange;
						exchange.first = candidates[i].source.id;
						exchange.second = candidates[j].source.id;
						exchange.channel = uint32_t(channel);
						exchange.secondPerFirst = ratio;
						exchange.lowerStep = std::max(lower[a] - x[a], (x[b] - upper[b]) / ratio);
						exchange.upperStep = std::min(upper[a] - x[a], (x[b] - lower[b]) / ratio);
						if (exchange.upperStep > exchange.lowerStep)
							result.exchanges.push_back(exchange);
					}
		}
		else
		{
			std::vector<Observation> rows;
			Scan(x, false, false,
				 [&](const Observation &a, V, const std::vector<double> &) { rows.push_back(a); });
			size_t n = rows.size();
			std::vector<double> jac(parameters * n * 3, 0);
			for (size_t k = 0; k < parameters; ++k)
			{
				auto a = x, b = x;
				a[k] = std::min(upper[k], x[k] + 1);
				b[k] = std::max(lower[k], x[k] - 1);
				if (a[k] == b[k])
					continue;
				std::vector<V> pa, pb;
				Evaluate(a, false, false, &pa);
				Evaluate(b, false, false, &pb);
				for (size_t i = 0; i < n; ++i)
					for (size_t c = 0; c < 3; ++c)
						jac[(k * n + i) * 3 + c] = (pa[i][c] - pb[i][c]) / (a[k] - b[k]);
			}
			for (size_t i = 0; i < parameters; ++i)
				for (size_t j = 0; j < parameters; ++j)
				{
					double value = 0;
					for (size_t k = 0; k < n; ++k)
						for (size_t c = 0; c < 3; ++c)
							if (rows[k].channels[c].present)
								value += Weight(rows[k]) * jac[(i * n + k) * 3 + c] *
										 jac[(j * n + k) * 3 + c] / (rows[k].sigma * rows[k].sigma);
					gram[i * parameters + j] = value;
					Arithmetic(n * 3);
				}
			result.spectrum = SpectrumOf(gram, "Local strict-operator amplitude finite differences (step 1 "
											   "Q), not global rank or a shadow derivative");
		}
		for (size_t j = 0; j < result.sources.size(); ++j)
			for (size_t k = 0; k < 3; ++k)
			{
				auto &s = slices[j * 3 + k];
				s.available &=
					s.lower <= s.upper && x[j * 3 + k] >= s.lower - 1e-5 && x[j * 3 + k] <= s.upper + 1e-5;
				result.sources[j].conditional[k] = s;
			}
		for (size_t j = 0; j < result.sources.size(); ++j)
		{
			auto removed = x;
			for (size_t k = 0; k < 3; ++k)
				removed[j * 3 + k] = 0;
			bool admissible = true;
			for (size_t k = 0; k < 3; ++k)
				admissible &= lower[j * 3 + k] == 0;
			if (admissible)
			{
				result.sources[j].removalObjectiveIncrease =
					Evaluate(removed).Total() - result.objective.Total();
				result.sources[j].removalAvailable = true;
			}
		}
	}
	Result Run()
	{
		Discover();
		auto x = result.linear ? Linear() : Nonlinear();
		result.objective = Evaluate(x, true, true);
		Need(result.strictDifference <= o.strictTolerance, Code::AlgorithmError,
			 "Linear/cache result disagrees with strict production replay");
		result.strictReevaluated = true;
		for (size_t j = 0; j < candidates.size(); ++j)
		{
			SourceResult s;
			s.source = candidates[j].source.id;
			s.compilerQ = V(x[j * 3], x[j * 3 + 1], x[j * 3 + 2]).Float();
			s.peakQ = Peak(V(s.compilerQ));
			if (s.peakQ > 0)
				s.peakNormalizedColour = (V(s.compilerQ) / s.peakQ).Float();
			s.zeroResponse = zeroResponse[j];
			s.prior = candidates[j].prior;
			result.sources.push_back(s);
		}
		if (baseline)
			result.baseline = V(x[parameters - 3], x[parameters - 2], x[parameters - 1]).Float();
		Diagnostics(x);
		for (auto &s : result.sources)
			s.allocationAmbiguous = s.zeroResponse || !result.spectrum.available ||
									result.spectrum.rank < result.spectrum.parameters ||
									(result.spectrum.conditionKnown && result.spectrum.condition > 1e3) ||
									result.coverage.censored > 0;
		for (size_t j = 0; j < result.sources.size(); ++j)
			if (candidates[j].source.position.sigmaWorld.known &&
				candidates[j].source.position.sigmaWorld.value > 0)
				result.sources[j].allocationAmbiguous = true;
		result.diagnostics.push_back(
			"Fixed-geometry compiler-relative Q diagnostic only; absolute photometric scale unknown; no "
			"source existence probability or renderer publication");
		if (!result.linear)
			result.diagnostics.push_back(
				"Nonlinear local/discrete branch search; no global convergence or unique intensity "
				"guarantee. Conditional nonlinear slices unsupported.");
		Cancel();
		result.fitted = true;
		return result;
	}
};
} // namespace
Interval Rgb8(uint8_t b)
{
	Interval i;
	i.lower = b;
	i.upper = b == 255 ? std::numeric_limits<double>::infinity() : double(b) + 1;
	i.censored = b == 255;
	return i;
}
Error Fit(const Request &r, const ReadContext &read, Result &out)
{
	out = {};
	std::unique_ptr<Engine> engine;
	try
	{
		engine.reset(new Engine(r, read));
		out = engine->Run();
		return {};
	}
	catch (const Error &e)
	{
		if (engine)
		{
			out.coverage = engine->result.coverage;
			out.work = engine->result.work;
			out.diagnostics = engine->result.diagnostics;
		}
		out.stop = e.code == Code::Cancelled
					   ? Stop::Cancelled
					   : e.code == Code::WorkLimit
							 ? Stop::WorkLimit
							 : e.code == Code::AlgorithmError ? Stop::NumericalFailure : Stop::Unsupported;
		out.diagnostics.push_back(e.message);
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		out.stop = Stop::NumericalFailure;
		return Failure(Code::MemoryLimit, "Amplitude fitting allocation failed");
	}
}
const char *StopName(Stop s)
{
	const char *n[] = {"NotStarted",  "IntervalFeasible", "ProjectedKkt", "ParameterStep",
					   "Stagnated",	  "IterationLimit",	  "WorkLimit",	  "Cancelled",
					   "Unsupported", "NumericalFailure"};
	return size_t(s) < 10 ? n[size_t(s)] : "Invalid";
}
} // namespace Amplitude
} // namespace Core
} // namespace Rend2LightRecon
