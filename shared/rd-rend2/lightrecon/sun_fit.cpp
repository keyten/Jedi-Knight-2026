// CPU-only S02 estimator. Profile-scoped hypotheses, not runtime sun control.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "sun_fit.h"
#include "bake_math.h"
#include "bake_trace.h"
#include "geometry_key.h"
#include <map>
#include <set>
#include <tuple>

namespace Rend2LightRecon
{
namespace Core
{
namespace Solar
{
namespace
{
using namespace Bake::Detail;
using Cell = std::tuple<int64_t, int64_t, int64_t>;
void Require(bool b, Code c, const char *s)
{
	if (!b)
		throw Failure(c, s);
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
Cell Block(Vec3 p, const Options &o)
{
	return Cell(int64_t(std::floor((p.x - o.partitionOriginWorld.x) / o.blockWorld)),
				int64_t(std::floor((p.y - o.partitionOriginWorld.y) / o.blockWorld)),
				int64_t(std::floor((p.z - o.partitionOriginWorld.z) / o.blockWorld)));
}
double Y(V a) { return .2126 * a.x + .7152 * a.y + .0722 * a.z; }
double Angle(V a, V b) { return std::acos(Clamp(Dot(a, b), -1, 1)); }
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
Frame Orientation(V to)
{
	Frame f;
	f.known = true;
	V z = to * -1, x = Unit(Cross(std::abs(z.z) < .9 ? V(0, 0, 1) : V(0, 1, 0), z));
	f.forward = z.Float();
	f.right = x.Float();
	f.up = Cross(z, x).Float();
	return f;
}
double Bin(double p, uint8_t b)
{
	return p < b ? p - b : p > std::min(255, int(b) + 1) ? p - std::min(255, int(b) + 1) : 0;
}
double Parameter(const Bake::ProfileHypothesis &p, Bake::Feature f, const char *name)
{
	const auto *r = p.Rule(f);
	if (r)
		for (const auto &v : r->parameters)
			if (v.name == name && v.value.known)
				return v.value.value;
	throw Failure(Code::Unavailable, "Missing profile parameter");
}
// This wrapper enforces explicit sky reach even for an incomplete/custom adapter.
// A clear segment without a sky hit cannot turn into directional illumination.
class SkyGuard final : public VisibilityProvider
{
  public:
	Bake::EvaluationContext original;
	Error Query(const RayQuery &q, const ReadContext &read, RayResult &out) const override
	{
		Error e;
		if (original.visibility)
			e = original.visibility->Query(q, read, out);
		else if (original.compilerTrace)
		{
			if (original.compilerTrace->ClassifyOrigin(q.startWorld) == PointDomain::Solid)
			{
				out = RayResult();
				out.issues = OriginInsideSolid;
				return {};
			}
			e = original.compilerTrace->Query(q.startWorld, q.endWorld, true, read, q.maxWork, out);
		}
		else
			return Failure(Code::MissingGeometry,
						   "Sun/sky requires static visibility and actual sky surfaces");
		if (e && out.state != VisibilityState::Unknown &&
			(out.reliability != RayReliability::Geometric ||
			 (out.issues & (OpticalUnknown | UnsupportedGeometry | IncompleteGeometry | PolicyAssumption)) ||
			 (out.state == VisibilityState::Visible && out.sky != SkyReach::Reached)))
		{
			out.state = VisibilityState::Unknown;
			out.issues |= SkyBoundaryMissing;
		}
		return e;
	}
};
struct Engine
{
	Options options;
	const ReadContext &read;
	Work work;
	Bake::EvaluationContext geometry;
	Engine(Options o, const ReadContext &r, const Bake::EvaluationContext &g)
		: options(o), read(r), geometry(g)
	{
		auto guard = std::make_shared<SkyGuard>();
		guard->original = g;
		geometry.visibility = guard;
	}
	void Check()
	{
		Require(!Cancelled(read), Code::Cancelled, "Sun/sky cancelled");
		Require(work.rays <= options.maxRays && work.pairs <= options.maxPairs &&
					work.traversal <= options.maxTraversalWork &&
					work.encodingCalls <= options.maxEncodingCalls,
				Code::WorkLimit, "Sun/sky work budget");
	}
	Bake::Result Forward(const Bake::ProfileHypothesis &p,
						 const std::shared_ptr<std::vector<Bake::Receiver>> &receivers, Bake::SourceKind kind,
						 V direction)
	{
		Check();
		auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>(1);
		auto &s = (*sources)[0];
		s.id = 1;
		s.kind = kind;
		s.orientation = Orientation(direction);
		s.amplitudeKnown = true;
		s.compilerRgbAmplitude = Vec3(1, 1, 1);
		Bake::Options o;
		o.allowAssumed = options.allowAssumedProfiles;
		o.trace = Bake::TraceMode::StaticGeometry;
		o.output = Bake::Domain::CompilerLinearRelative255;
		o.maxPairs = options.maxPairs - work.pairs;
		o.maxRays = options.maxRays - work.rays;
		o.maxWork = options.maxTraversalWork - work.traversal;
		o.required = {Bake::Feature::SunSky, Bake::Feature::OutputEncoding,
					  Bake::Feature::SamplingVisibility};
		Bake::Result result;
		Bake::CpuForwardEvaluator evaluator;
		auto e = evaluator.Evaluate(p, Lease(sources), Lease(receivers), Decode::Profile(), geometry, o, read,
									result);
		++work.forwardCalls;
		work.rays += result.work.rays;
		work.pairs += result.work.pairs;
		work.traversal += result.work.nodeTests + result.work.triangleTests;
		Check();
		if (!e)
		{
			work.complete = false;
			throw e;
		}
		return result;
	}
	Vec3 Encode(const Bake::ProfileHypothesis &p, V a)
	{
		++work.encodingCalls;
		Check();
		Vec3 rgb;
		uint8_t packed[3];
		auto e = Bake::Encode(p, a.Float(), rgb, packed);
		if (!e)
			throw e;
		return rgb;
	}
};
struct Rows
{
	std::vector<Sample> values;
	std::vector<double> sky, weights;
	std::vector<V> base;
	std::vector<ReceiverEvidence> evidence;
	std::shared_ptr<std::vector<Bake::Receiver>> receivers = std::make_shared<std::vector<Bake::Receiver>>();
	Coverage coverage;
};
double NormalRatio(const std::vector<Sample> &v)
{
	double a[3][3] = {};
	for (const auto &s : v)
	{
		V n(s.receiver.geometricNormal);
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				a[i][j] += n[i] * n[j];
	}
	for (int iter = 0; iter < 40; ++iter)
	{
		int p = 0, q = 1;
		for (int i = 0; i < 3; ++i)
			for (int j = i + 1; j < 3; ++j)
				if (std::abs(a[i][j]) > std::abs(a[p][q]))
				{
					p = i;
					q = j;
				}
		if (std::abs(a[p][q]) < 1e-12)
			break;
		double t = .5 * std::atan2(2 * a[p][q], a[q][q] - a[p][p]), c = std::cos(t), s = std::sin(t),
			   x = a[p][p], y = a[q][q], z = a[p][q];
		for (int k = 0; k < 3; ++k)
			if (k != p && k != q)
			{
				double u = a[k][p], v = a[k][q];
				a[k][p] = a[p][k] = c * u - s * v;
				a[k][q] = a[q][k] = s * u + c * v;
			}
		a[p][p] = c * c * x - 2 * s * c * z + s * s * y;
		a[q][q] = s * s * x + 2 * s * c * z + c * c * y;
		a[p][q] = a[q][p] = 0;
	}
	double largest = std::max(a[0][0], std::max(a[1][1], a[2][2]));
	return largest > 0 ? std::max(0.0, std::min(a[0][0], std::min(a[1][1], a[2][2]))) / largest : 0;
}
void ValidateOptions(const Options &o)
{
	Require(o.sphereDirections >= 16 && o.sphereDirections <= 512 && o.refineSeeds <= 16 &&
				o.refineLevels <= 8 && o.maxAlternatives > 0 && o.maxAlternatives <= 64,
			Code::InvalidInput, "Angular search limits");
	Require(o.maxSamples >= 12 && o.maxSamples <= 4096 && o.maxProfiles > 0 && o.maxProfiles <= 8 &&
				o.minSamples >= 4 && o.minValidationSamples >= 4 && o.minBlocks >= 2 &&
				o.minValidationBlocks >= 2,
			Code::InvalidInput, "Sun/sky sample limits");
	for (double x : {o.blockWorld, o.amplitudeUpper, o.huberCodes, o.acceptableRmseCodes, o.maximumErrorCodes,
					 o.sunImprovementCodes, o.skyVsFillImprovementCodes, o.alternativeDeltaCodes,
					 o.minBlackRmseCodes, o.positiveCodes, o.minimumNormalEigenRatio, o.minimumCoverage})
		Require(std::isfinite(x) && x > 0, Code::InvalidInput, "Finite positive sun/sky options required");
	Require(o.minimumCoverage <= 1 && o.minimumNormalEigenRatio <= 1 && std::isfinite(o.partitionGapWorld) &&
				o.partitionGapWorld >= 0,
			Code::InvalidInput, "Invalid coverage/gap");
	Require(Finite(V(o.partitionOriginWorld)), Code::InvalidInput, "Invalid partition origin");
	Require(o.blockWorld >= 1e-3 && o.blockWorld <= 1e9 && o.amplitudeUpper <= 1e8, Code::InvalidInput,
			"Unsupported numerical sun-fit scale");
}
Rows Prepare(Engine &engine, const Bake::ProfileHypothesis &p, const Chunk<Sample> &input)
{
	const auto &o = engine.options;
	Rows rows;
	rows.coverage.requested = input.count;
	Require(input.count <= o.maxSamples && (!input.count || (input.data && input.owner)), Code::InvalidInput,
			"Invalid/oversized sun/sky observation lease");
	Require(input.count * sizeof(Sample) * 8 <= o.maxBytes, Code::MemoryLimit,
			"Sun/sky receiver memory budget");
	// The linear amplitude block is verified only for this unfiltered output branch.
	Require(Parameter(p, Bake::Feature::OutputEncoding, "gamma") == 1 &&
				Parameter(p, Bake::Feature::OutputEncoding, "compensate") == 1 &&
				Parameter(p, Bake::Feature::OutputEncoding, "exposure") == 0 &&
				Parameter(p, Bake::Feature::OutputEncoding, "srgb") == 0,
			Code::Unavailable,
			"S02 amplitude fit supports gamma1/compensate1/exposure0 only; no inferred output transfer");
	std::vector<Sample> sorted;
	if (input.count)
		sorted.assign(input.data, input.data + input.count);
	std::sort(sorted.begin(), sorted.end(),
			  [](const Sample &a, const Sample &b) { return a.receiver.sample < b.receiver.sample; });
	Id last = 0;
	Bake::Options capability;
	capability.allowAssumed = o.allowAssumedProfiles;
	capability.output = Bake::Domain::CompilerLinearRelative255;
	for (const auto &s : sorted)
	{
		engine.Check();
		Require(s.receiver.sample > last, Code::InvalidInput, "Zero/duplicate sun observation ID");
		last = s.receiver.sample;
		Require(Finite(V(s.receiver.world)) && Normal(V(s.receiver.geometricNormal)) &&
					Positive(V(s.otherPreencoding)) && std::isfinite(s.areaWorldSquared) &&
					s.areaWorldSquared > 0 && s.areaWorldSquared <= 1e24 &&
					std::isfinite(s.footprintRadiusWorld) && s.footprintRadiusWorld >= 0 &&
					s.footprintRadiusWorld <= 1e12 &&
					Length(V(s.receiver.world) - V(o.partitionOriginWorld)) / o.blockWorld < 1e12,
				Code::InvalidInput, "Invalid sun receiver/area/baseline");
		ReceiverEvidence e;
		e.sample = s.receiver.sample;
		if (!s.eligible)
		{
			e.exclusion = Exclusion::IneligibleReceiver;
			++rows.coverage.excludedIneligible;
		}
		else if (!s.encodingVerifiedForProfile && !o.allowAssumedObservationEncoding)
		{
			e.exclusion = Exclusion::UnknownEncoding;
			++rows.coverage.excludedEncoding;
		}
		else if (!Bake::Capability(p, Bake::SourceKind::Directional, s.receiver, capability).implemented ||
				 s.receiver.domain != Bake::Domain::EncodedRgb8BeforeQuantization)
		{
			e.exclusion = Exclusion::UnsupportedGeometry;
			++rows.coverage.excludedGeometry;
		}
		rows.evidence.push_back(e);
		if (e.exclusion == Exclusion::None)
		{
			rows.values.push_back(s);
			rows.receivers->push_back(s.receiver);
		}
	}
	if (rows.values.empty())
		return rows;
	auto sky = engine.Forward(p, rows.receivers, Bake::SourceKind::Sky, V(0, 0, 1));
	Rows kept;
	kept.coverage = rows.coverage;
	kept.evidence = rows.evidence;
	std::map<Id, size_t> indices;
	for (size_t i = 0; i < kept.evidence.size(); ++i)
		indices[kept.evidence[i].sample] = i;
	std::map<Cell, double> areas;
	std::set<Cell> positiveBlocks;
	for (size_t i = 0; i < rows.values.size(); ++i)
	{
		const auto &prediction = sky.predictions.data[i];
		const auto &s = rows.values[i];
		auto &e = kept.evidence[indices[s.receiver.sample]];
		if (!prediction.available)
		{
			e.exclusion = Exclusion::UnknownSkyVisibility;
			++kept.coverage.excludedSky;
			continue;
		}
		e.skyResponse = prediction.sky.x;
		kept.values.push_back(s);
		kept.receivers->push_back(s.receiver);
		kept.sky.push_back(prediction.sky.x);
		V base = V(s.otherPreencoding) + V(prediction.ambient);
		kept.base.push_back(base);
		areas[Block(s.receiver.world, o)] += s.areaWorldSquared;
		bool saturated = false;
		for (auto b : s.rgb)
			saturated |= b == 255;
		kept.coverage.saturated += saturated;
		Vec3 baseline = engine.Encode(p, base);
		double maximum = 0;
		for (int c = 0; c < 3; ++c)
			maximum = std::max(maximum, double(s.rgb[c]) - V(baseline)[c]);
		kept.coverage.positive += maximum >= o.positiveCodes;
		if (maximum >= o.positiveCodes)
			positiveBlocks.insert(Block(s.receiver.world, o));
	}
	kept.coverage.blocks = areas.size();
	kept.coverage.used = kept.values.size();
	kept.coverage.normalEigenRatio = NormalRatio(kept.values);
	kept.coverage.positiveBlocks = positiveBlocks.size();
	double black = 0;
	for (const auto &s : kept.values)
	{
		double w = s.areaWorldSquared / areas[Block(s.receiver.world, o)] / areas.size();
		kept.weights.push_back(w);
		for (int c = 0; c < 3; ++c)
			black += w * s.rgb[c] * s.rgb[c] / 3.;
	}
	kept.coverage.blackRmse = std::sqrt(black);
	return kept;
}
bool Enough(const Rows &r, const Options &o, bool validation)
{
	return r.coverage.used >= (validation ? o.minValidationSamples : o.minSamples) &&
		   r.coverage.blocks >= (validation ? o.minValidationBlocks : o.minBlocks) &&
		   r.coverage.used >= r.coverage.requested * o.minimumCoverage && r.coverage.positive >= 4 &&
		   r.coverage.positiveBlocks >= 2 && r.coverage.blackRmse >= o.minBlackRmseCodes;
}
Score Evaluate(Engine &e, const Bake::ProfileHypothesis &p, const Rows &r, const std::vector<double> &sun,
			   V q, V k, Model model)
{
	Score score;
	for (size_t i = 0; i < r.values.size(); ++i)
	{
		V value = r.base[i] + q * sun[i] + k * (model == Model::UniformFillControl ? 1 : r.sky[i]);
		V encoded(e.Encode(p, value));
		for (int c = 0; c < 3; ++c)
		{
			double d = Bin(encoded[c], r.values[i].rgb[c]), a = std::abs(d), w = r.weights[i] / 3;
			score.intervalRmse += w * d * d;
			score.maximumError = std::max(score.maximumError, a);
			score.huber += w * (a <= e.options.huberCodes ? d * d
														  : 2 * e.options.huberCodes * a -
																e.options.huberCodes * e.options.huberCodes);
		}
	}
	score.intervalRmse = std::sqrt(score.intervalRmse);
	return score;
}
std::pair<double, double> Solve2(double a, double b, double d, double u, double v, double upper)
{
	std::vector<std::pair<double, double>> candidates = {{0, 0}, {upper, upper}};
	auto add = [&](double x, double y) { candidates.emplace_back(Clamp(x, 0, upper), Clamp(y, 0, upper)); };
	if (a > 1e-15)
	{
		add(u / a, 0);
		add((u - b * upper) / a, upper);
	}
	if (d > 1e-15)
	{
		add(0, v / d);
		add(upper, (v - b * upper) / d);
	}
	double det = a * d - b * b;
	if (det > std::max(1e-20, a * d * 1e-12))
		add((u * d - v * b) / det, (v * a - u * b) / det);
	return *std::min_element(candidates.begin(), candidates.end(),
							 [&](std::pair<double, double> x, std::pair<double, double> y) {
								 auto f = [&](std::pair<double, double> t) {
									 return a * t.first * t.first + 2 * b * t.first * t.second +
											d * t.second * t.second - 2 * u * t.first - 2 * v * t.second;
								 };
								 return f(x) < f(y);
							 });
}
Proposal Amplitude(Engine &engine, const Bake::ProfileHypothesis &p, const Rows &r,
				   const std::vector<double> &sun, Model model, V direction, double resolution, bool authored)
{
	Proposal candidate;
	candidate.profile = p;
	candidate.model = model;
	candidate.toSource = direction.Float();
	candidate.searchResolutionRadians = resolution;
	candidate.authoredSeed = authored;
	candidate.coverage = r.coverage;
	V q, k;
	double smallestDet = 1;
	for (int channel = 0; channel < 3; ++channel)
	{
		double x = 0, y = 0;
		for (int iteration = 0; iteration < 6; ++iteration)
		{
			engine.Check();
			double a = 0, b = 0, d = 0, u = 0, v = 0;
			for (size_t i = 0; i < r.values.size(); ++i)
			{
				const auto &s = r.values[i];
				if (s.rgb[0] == 255 || s.rgb[1] == 255 || s.rgb[2] == 255)
					continue; // coupled clipping is not an exact linear target
				double A = sun[i], B = model == Model::UniformFillControl ? 1 : r.sky[i],
					   target = s.rgb[channel] + .5 - r.base[i][channel];
				double residual = A * x + B * y - target,
					   w = r.weights[i] *
						   std::min(1., engine.options.huberCodes / std::max(1e-12, std::abs(residual)));
				a += w * A * A;
				b += w * A * B;
				d += w * B * B;
				u += w * A * target;
				v += w * B * target;
			}
			auto solution = Solve2(a, b, d, u, v, engine.options.amplitudeUpper);
			x = solution.first;
			y = solution.second;
			if (a * d > 1e-20)
				smallestDet = std::min(smallestDet, (a * d - b * b) / (a * d));
		}
		q[channel] = x;
		k[channel] = y;
	}
	candidate.sunRgb = q.Float();
	candidate.skyRgb = k.Float();
	candidate.amplitudeDegenerate = model == Model::SunAndSky && smallestDet < .001;
	candidate.training = Evaluate(engine, p, r, sun, q, k, model);
	return candidate;
}
bool SunBasis(Engine &e, const Bake::ProfileHypothesis &p, const Rows &r, V direction,
			  std::vector<double> &basis, std::vector<ReceiverEvidence> *evidence = nullptr)
{
	auto result = e.Forward(p, r.receivers, Bake::SourceKind::Directional, direction);
	basis.clear();
	if (evidence)
		*evidence = r.evidence;
	for (size_t i = 0; i < r.values.size(); ++i)
	{
		const auto &v = result.predictions.data[i];
		if (!v.available)
			return false;
		basis.push_back(v.sun.x);
		if (evidence)
			for (auto &record : *evidence)
				if (record.sample == r.values[i].receiver.sample)
				{
					record.sunResponse = v.sun.x;
					record.sunFacing = Dot(V(r.values[i].receiver.geometricNormal), direction) > 0;
					break;
				}
	}
	return true;
}
bool Better(const Proposal &a, const Proposal &b)
{
	if (a.training.huber != b.training.huber)
		return a.training.huber < b.training.huber;
	if (a.authoredSeed != b.authoredSeed)
		return a.authoredSeed;
	if (a.searchResolutionRadians != b.searchResolutionRadians)
		return a.searchResolutionRadians < b.searchResolutionRadians;
	return std::tie(a.toSource.x, a.toSource.y, a.toSource.z) <
		   std::tie(b.toSource.x, b.toSource.y, b.toSource.z);
}
Disk::Digest Frozen(const FitResult &f)
{
	GeometryKey k;
	k.U(kVersion);
	k.Digest(f.mapKey);
	k.Digest(f.geometryKey);
	Text(k, f.lineage);
	const auto &o = f.options;
	k.V(o.partitionOriginWorld);
	for (uint64_t n : std::initializer_list<uint64_t>{
			 o.sphereDirections, o.refineSeeds, o.refineLevels, o.maxAlternatives, uint64_t(o.maxSamples),
			 uint64_t(o.maxProfiles), uint64_t(o.minSamples), uint64_t(o.minBlocks),
			 uint64_t(o.minValidationSamples), uint64_t(o.minValidationBlocks), o.maxRays, o.maxPairs,
			 o.maxTraversalWork, o.maxEncodingCalls, o.maxBytes, uint64_t(o.allowAssumedProfiles),
			 uint64_t(o.allowAssumedObservationEncoding)})
		k.U(n);
	for (double n :
		 {o.blockWorld, o.partitionGapWorld, o.amplitudeUpper, o.huberCodes, o.acceptableRmseCodes,
		  o.maximumErrorCodes, o.sunImprovementCodes, o.skyVsFillImprovementCodes, o.alternativeDeltaCodes,
		  o.minBlackRmseCodes, o.positiveCodes, o.minimumNormalEigenRatio, o.minimumCoverage})
		k.F(n);
	k.F(f.units.worldUnitsPerMeter);
	k.F(f.units.physicalToScene);
	for (const auto &h : f.hints)
	{
		k.U(h.id);
		k.U(uint64_t(h.kind));
		k.V(h.toSource);
		k.U(h.style);
		k.U(h.staticEligible);
		for (char c : h.provenance)
			k.U(uint8_t(c));
	}
	for (const auto &p : f.proposals)
	{
		k.U(uint64_t(p.model));
		k.Digest(p.profile.contentKey);
		k.V(p.toSource);
		k.V(p.sunRgb);
		k.V(p.skyRgb);
		k.F(p.searchResolutionRadians);
		Text(k, p.profile.id);
		Text(k, p.profile.family);
		Text(k, p.profile.compilerSourceRevision);
		Text(k, p.profile.compilerBinarySha256);
		k.U(p.profile.schemaVersion);
		k.U(p.profile.formulaVersion);
		k.U(uint64_t(p.profile.compiler));
		k.U(uint64_t(p.profile.applicability));
		k.Digest(p.profile.sourceLockKey);
		k.Digest(p.profile.catalogueKey);
		for (const auto &rule : p.profile.rules)
		{
			k.U(uint64_t(rule.knowledge));
			k.U(rule.formulaVersion);
			Text(k, rule.selector);
			Text(k, rule.provenance);
			Text(k, rule.verificationScope);
			for (const auto &v : rule.parameters)
			{
				Text(k, v.name);
				Text(k, v.unit);
				k.U(v.value.known);
				k.F(v.value.value);
			}
		}
	}
	for (const auto &s : f.trainingGeometry)
	{
		k.U(s.sample);
		k.V(s.world);
		k.F(s.footprintRadius);
	}
	return k.Finish();
}
Id Identity(const FitResult &f, const Proposal &p, uint64_t tag)
{
	GeometryKey k;
	k.U(kVersion);
	k.Digest(f.mapKey);
	k.Digest(f.geometryKey);
	k.Digest(f.frozenKey);
	k.Digest(p.profile.contentKey);
	k.V(p.toSource);
	k.V(p.sunRgb);
	k.V(p.skyRgb);
	k.U(tag);
	auto hash = k.Finish();
	Id id = 0;
	for (int i = 0; i < 8; ++i)
		id |= Id(hash[i]) << (8 * i);
	return id < 100 ? id + 100 : id;
}
Photometry Photo(V q, Estimate slice = Estimate())
{
	Photometry p;
	p.unit = PhotometricUnit::Relative;
	p.calibration = 2;
	p.intensity.known = true;
	p.intensity.value = float(Y(q));
	if (slice.bounded)
	{
		p.intensity.bounded = true;
		p.intensity.lower = slice.lower;
		p.intensity.upper = slice.upper;
	}
	if (Y(q) > 0)
		Require(NormalizeChromaticity(q.Float(), p.color), Code::AlgorithmError,
				"Invalid inferred chromaticity");
	return p;
}
void Canonical(const FitResult &f, Hypothesis &h)
{
	const auto &p = h.fitted;
	auto &a = h.asset;
	a.lineage = f.lineage + "/solar-v1/" + Disk::Hex(f.mapKey);
	a.units = f.units;
	a.provenance = 3;
	Provenance raw;
	raw.id = 1;
	raw.kind = ProvenanceKind::LegacyLightmap;
	raw.locator = f.lineage;
	raw.contentHash = "sha256:" + Disk::Hex(f.mapKey);
	raw.producer = "immutable CPU observations";
	Provenance recon;
	recon.id = 3;
	recon.parents = {1};
	recon.kind = ProvenanceKind::Reconstruction;
	recon.producer = "S02 solar-v1";
	recon.locator = p.profile.id;
	recon.contentHash = "sha256:" + Disk::Hex(f.frozenKey);
	recon.method = "Independent validation; explicit conditional bake profile; single-sun+sky17 hypothesis, "
				   "no bounce/fill recovery";
	recon.observationIssues = ObservationQuantized | (p.coverage.saturated ? ObservationClipped : 0) |
							  (f.options.allowAssumedObservationEncoding ? ObservationUnreliable : 0);
	a.provenanceRecords = {raw, recon};
	Calibration calibration;
	calibration.id = 2;
	calibration.provenance = 3;
	calibration.kind = CalibrationKind::UncalibratedRelative;
	calibration.method = "Compiler-effective RGB; unknown absolute lux/radiance and stock bake applicability";
	a.calibrations.push_back(calibration);
	for (size_t i = 0; i < f.hints.size(); ++i)
	{
		const auto &hint = f.hints[i];
		Provenance prior;
		prior.id = 100 + i;
		prior.parents = {1};
		prior.kind = hint.kind == HintKind::AuthoredSun
						 ? ProvenanceKind::Authored
						 : hint.kind == HintKind::GridDominant ? ProvenanceKind::LegacyLightgrid
															   : ProvenanceKind::LegacyLightmap;
		prior.locator = hint.provenance;
		prior.producer = "Sun/sky input hint " + std::to_string(hint.id);
		prior.method = "Direction seed only; lightstyle eligibility checked; no authored intensity or "
					   "reliability assumed";
		a.provenanceRecords.push_back(prior);
		a.provenanceRecords[1].parents.push_back(prior.id);
	}
	if (p.model == Model::SunAndSky && Y(V(p.sunRgb)) > 0)
	{
		PhysicalEmitter sun;
		sun.source.id = Identity(f, p, 1);
		sun.source.provenance = 3;
		sun.source.state = HypothesisState::Candidate;
		sun.source.evidence.exactLight = Suitability::Unsuitable;
		sun.source.evidence.approximateIrradiance = Suitability::Candidate;
		sun.source.evidence.observationIssues = recon.observationIssues;
		sun.kind = EmitterKind::Directional;
		sun.frame = Orientation(V(p.toSource));
		sun.angular.kind = AngularKind::Parallel;
		sun.emission = Photo(V(p.sunRgb), h.sunIntensitySlice); // extent/confidence intentionally unknown
		a.emitters.push_back(sun);
		SunSource ref;
		ref.emitter = sun.source.id;
		a.suns.push_back(ref);
	}
	if (h.skySupported && Y(V(p.skyRgb)) > 0)
	{
		EnvironmentLighting environment;
		environment.source.id = Identity(f, p, 2);
		environment.source.provenance = 3;
		environment.source.state = HypothesisState::Candidate;
		environment.source.evidence.approximateIrradiance = Suitability::Candidate;
		environment.source.evidence.exactLight = Suitability::Unsuitable;
		environment.meanLuminance = Photo(V(p.skyRgb), h.skyIntensitySlice);
		environment.sunCoverage = SunCoverage::ExcludesExplicitSun;
		Dataset data;
		data.id = Identity(f, p, 3);
		data.purpose = DatasetPurpose::Environment;
		data.layout = DatasetLayout::Sparse;
		data.locator = "urn:openjk:solar-v1:" + std::to_string(data.id);
		data.encoding = "org.openjk.bake-effective-sky17-relative";
		data.encodingVersion = 1;
		data.elementCount = 17;
		GeometryKey key; // populated below using the declared profile quadrature
		double angle = 0;
		for (double elevation : {Pi / 12, Pi / 4})
		{
			for (int i = 0; i < 8; ++i)
			{
				EnvironmentSample s;
				s.toSource = V(std::cos(angle) * std::cos(elevation), std::sin(angle) * std::cos(elevation),
							   std::sin(elevation))
								 .Float();
				s.relativeRgb = p.skyRgb;
				h.environment.push_back(s);
				angle += Pi / 4;
			}
			angle += Pi / 8;
		}
		EnvironmentSample top;
		top.toSource = Vec3(0, 0, 1);
		top.relativeRgb = p.skyRgb;
		h.environment.push_back(top);
		for (const auto &s : h.environment)
		{
			key.V(s.toSource);
			key.V(s.relativeRgb);
			key.F(s.responseWeight);
		}
		data.contentHash = "sha256:" + Disk::Hex(key.Finish());
		environment.radiance = data.id;
		a.datasets.push_back(data);
		a.environments.push_back(environment);
	}
	else if (Y(V(p.skyRgb)) > 0)
	{
		// A small fitted angular nuisance is not evidence of physical skylight.
		// Retain explicit missing/unattributed metadata, not a fabricated GI field.
		TransportField unresolved;
		unresolved.source.id = Identity(f, p, 4);
		unresolved.source.provenance = 3;
		unresolved.source.state = HypothesisState::Candidate;
		unresolved.source.evidence.exactLight = Suitability::Unsuitable;
		unresolved.component = TransportComponent::UnattributedIncidentRadiance;
		unresolved.quantity = FieldQuantity::Radiance;
		unresolved.unit = PhotometricUnit::Relative;
		unresolved.calibration = 2;
		a.transport.push_back(unresolved);
		Diagnostic nuisance;
		nuisance.code = "unresolved-sky-shaped-nuisance";
		nuisance.source = unresolved.source.id;
		nuisance.message = "Fitted sky-shaped offset is not independently identified as skylight/bounce; "
						   "numeric RGB remains in fit diagnostics, canonical field data is unavailable";
		nuisance.metric.known = true;
		nuisance.metric.value = float(Y(V(p.skyRgb)));
		nuisance.metricUnit = "compiler-relative Y amplitude; not physical GI";
		a.diagnostics.push_back(nuisance);
	}
	Diagnostic d;
	d.code = "solar-conditional";
	d.message = h.acceptance + "; all amplitudes relative; direct sun excluded from sky; bounce/legacy "
							   "fill/unresolved discrepancy not assigned to either component";
	a.diagnostics.push_back(d);
	d.code = "sun-angular-extent-unknown";
	d.message =
		"Sharp compiler sun fitted; apparent source size and soft-shadow bake filter are not identified";
	a.diagnostics.push_back(d);
	d.code = "sampled-direction-ambiguity";
	d.metric.known = true;
	d.metric.value = float(h.sampledAngularAmbiguityRadians);
	d.metricUnit = "radians; finite search set, not confidence interval";
	a.diagnostics.push_back(d);
	std::string error;
	Require(Validate(a, &error), Code::AlgorithmError, error.c_str());
}
} // anonymous namespace

std::vector<DirectionHint> AuthoredHints(const EmitterEvidenceSet &set)
{
	std::vector<DirectionHint> result;
	for (const auto &e : set.Evidence())
		if (e.origin == EvidenceOrigin::SunShader && e.orientationKnown)
		{
			DirectionHint h;
			h.id = e.id;
			h.kind = HintKind::AuthoredSun;
			h.toSource = e.toSource;
			h.provenance = e.source + " " + e.raw;
			h.staticEligible = e.staticEligible;
			h.style = e.style;
			result.push_back(h);
		}
	return result;
}
Error FromObservation(const Bake::Receiver &r, const Decode::Observation &observation, double area,
					  double radius, bool eligible, bool verified, Sample &out)
{
	out = Sample();
	const auto &raw = observation.raw;
	if (raw.encoding != Decode::Encoding::Rgb8 || raw.component != Decode::Component::Lightmap ||
		raw.missing || raw.directionData)
		return Failure(Code::Unavailable,
					   "S02 requires original colour lightmap RGB8; HDR/deluxe are distinct operators");
	if (!Finite(V(r.world)) || !Normal(V(r.geometricNormal)) || !std::isfinite(area) || area <= 0 ||
		!std::isfinite(radius) || radius < 0)
		return Failure(Code::InvalidInput, "Invalid canonical sun observation geometry");
	Sample s;
	s.receiver = r;
	s.areaWorldSquared = area;
	s.footprintRadiusWorld = radius;
	s.eligible = eligible;
	// Unknown transfer/primaries cannot become 'verified' through this adapter.
	s.encodingVerifiedForProfile =
		verified && observation.resolvedTransfer == Decode::Transfer::Linear &&
		observation.resolvedPrimaries == Decode::Primaries::Rec709D65 &&
		!(observation.flags & (Decode::TransferAssumed | Decode::PrimariesAssumed));
	for (int i = 0; i < 3; ++i)
		s.rgb[i] = raw.bytes[i];
	out = s;
	return {};
}
DirectionHint GridHint(const GridCell &cell, Id id)
{
	DirectionHint h;
	h.id = id;
	h.kind = HintKind::GridDominant;
	h.toSource = cell.direction.toSource;
	h.staticEligible = cell.presence == GridPresence::Present && cell.styleStatus == GridStyle::Static;
	h.staticEligible = h.staticEligible && cell.direction.quality != GridDirectionQuality::Unavailable;
	h.provenance = "Original lightgrid dominant mixture; seed only, no validated sun reduction";
	h.quantizationRadians.known = true;
	h.quantizationRadians.value = cell.direction.errorBoundRadians;
	return h;
}
DirectionHint DeluxeHint(const DeluxeDirection &d, Id id)
{
	DirectionHint h;
	h.id = id;
	h.kind = HintKind::DeluxeDominant;
	h.toSource = d.toSource;
	h.staticEligible = d.state == DeluxeState::Available;
	h.provenance = "Deluxe incoming mixture; seed only, no verified sun operator";
	h.quantizationRadians.known = true;
	h.quantizationRadians.value = d.angularBoundRadians;
	return h;
}
Error Fit(const Request &request, const ReadContext &read, FitResult &out)
{
	out = FitResult();
	Engine engine(request.options, read, request.geometry);
	try
	{
		ValidateOptions(request.options);
		engine.Check();
		Require(!request.lineage.empty(), Code::InvalidInput, "Sun/sky lineage required");
		Require(request.mapKey != Disk::Digest{} && request.geometry.geometryKey != Disk::Digest{},
				Code::InvalidInput, "Content-addressed map and geometry identities required");
		float metres = 0;
		Require(WorldToMeters(1, request.units, metres), Code::InvalidInput, "Invalid sun/sky world units");
		Require(request.profiles.size() <= request.options.maxProfiles, Code::InvalidInput,
				"Sun/sky profile budget");
		Require(request.hints.size() <= 256, Code::InvalidInput, "Sun/sky hint budget");
		std::set<std::string> profileIds;
		for(const auto &p:request.profiles)Require(!p.id.empty()&&profileIds.insert(p.id).second,Code::InvalidInput,"Profile hypothesis IDs must be nonempty and unique");
		Require(request.geometry.visibility || request.geometry.compilerTrace, Code::MissingGeometry,
				"No sky geometry provider");
		out.mapKey = request.mapKey;
		out.geometryKey = request.geometry.geometryKey;
		out.lineage = request.lineage;
		out.units = request.units;
		out.options = request.options;
		out.hints = request.hints;
		Require(request.training.count <= request.options.maxSamples &&
					(!request.training.count || (request.training.data && request.training.owner)),
				Code::InvalidInput, "Invalid training lease");
		std::set<Id> ids;
		for (size_t i = 0; i < request.training.count; ++i)
		{
			const auto &s = request.training.data[i];
			Require(ids.insert(s.receiver.sample).second, Code::InvalidInput, "Duplicate training ID");
			TrainingGeometry t;
			t.sample = s.receiver.sample;
			t.world = s.receiver.world;
			t.footprintRadius = s.footprintRadiusWorld;
			out.trainingGeometry.push_back(t);
		}
		for (const auto &profile : request.profiles)
		{
			uint64_t profileBytes = sizeof(profile);
			profileBytes += profile.id.size() + profile.family.size() +
							profile.compilerSourceRevision.size() + profile.compilerBinarySha256.size() +
							profile.nuisanceBounds.size() * sizeof(Bake::Parameter);
			for (const auto &r : profile.rules)
			{
				profileBytes += r.selector.size() + r.provenance.size() + r.verificationScope.size() +
								r.parameters.size() * sizeof(Bake::Parameter);
				for (const auto &v : r.parameters)
					profileBytes += v.name.size() + v.unit.size();
				for (const auto &s : r.unsupported)
					profileBytes += sizeof(std::string) + s.size();
			}
			uint64_t candidateCapacity = request.options.sphereDirections + 6 + request.hints.size() +
										 request.options.refineSeeds * request.options.refineLevels * 9;
			Require(profileBytes <= 4096 * 16 &&
						profileBytes <=
							request.options.maxBytes /
								std::max(uint64_t(1), candidateCapacity * 3 * request.profiles.size()),
					Code::MemoryLimit, "Sun search profile/candidate memory budget");
			Rows rows;
			try
			{
				rows = Prepare(engine, profile, request.training);
			}
			catch (const Error &e)
			{
				if (e.code != Code::Unavailable)
					throw;
				out.diagnostics.push_back(profile.id + ": " + e.message);
				continue;
			}
			out.profileCoverage.push_back(rows.coverage);
			if (!Enough(rows, request.options, false))
			{
				out.diagnostics.push_back(profile.id +
										  ": insufficient positive/spatial/comparable training coverage");
				continue;
			}
			std::vector<double> zeros(rows.values.size(), 0);
			out.proposals.push_back(Amplitude(engine, profile, rows, zeros, Model::SkyOnly, V(), 0, false));
			out.proposals.push_back(
				Amplitude(engine, profile, rows, zeros, Model::UniformFillControl, V(), 0, false));
			std::vector<Proposal> candidates;
			std::vector<std::pair<V, bool>> directions;
			for (const auto &h : request.hints)
			{
				Require(h.id && !h.provenance.empty() && (!h.staticEligible || Normal(V(h.toSource))),
						Code::InvalidInput, "Invalid directional hint/provenance");
				if (h.staticEligible && h.style == 0)
					directions.emplace_back(V(h.toSource), h.kind == HintKind::AuthoredSun);
				if (h.kind == HintKind::GridDominant || h.kind == HintKind::DeluxeDominant)
					out.diagnostics.push_back(
						"Directional mixture hint is seed-only; not a physical-sun confidence measurement");
			}
			for (V v : {V(0, 0, 1), V(0, 0, -1), V(1, 0, 0), V(-1, 0, 0), V(0, 1, 0), V(0, -1, 0)})
				directions.emplace_back(v, false);
			for (uint32_t i = 0; i < request.options.sphereDirections; ++i)
			{
				double z = 1 - 2 * (i + .5) / request.options.sphereDirections, a = i * 2.399963229728653;
				directions.emplace_back(
					V(std::sqrt(1 - z * z) * std::cos(a), std::sqrt(1 - z * z) * std::sin(a), z), false);
			}
			auto trial = [&](V direction, double resolution, bool authored, Proposal &p) {
				++engine.work.directions;
				std::vector<double> basis;
				if (!SunBasis(engine, profile, rows, direction, basis))
					return false;
				p = Amplitude(engine, profile, rows, basis, Model::SunAndSky, direction, resolution,
							  authored);
				return true;
			};
			for (const auto &d : directions)
			{
				Proposal p;
				if (trial(d.first, std::sqrt(4 * Pi / request.options.sphereDirections), d.second, p))
					candidates.push_back(p);
			}
			std::sort(candidates.begin(), candidates.end(), Better);
			auto seeds = candidates;
			for (size_t seed = 0; seed < std::min(size_t(request.options.refineSeeds), seeds.size()); ++seed)
			{
				auto current = seeds[seed];
				for (uint32_t level = 0; level < request.options.refineLevels; ++level)
				{
					double step = 10 * Pi / 180 / std::pow(2., level);
					V center(current.toSource),
						right = Unit(Cross(std::abs(center.z) < .9 ? V(0, 0, 1) : V(0, 1, 0), center)),
						up = Cross(center, right);
					auto best = current;
					for (int j = 0; j < 8; ++j)
					{
						double phi = j * Pi / 4;
						V d = Unit(center * std::cos(step) +
								   (right * std::cos(phi) + up * std::sin(phi)) * std::sin(step));
						Proposal p;
						if (trial(d, step, false, p))
						{
							candidates.push_back(p);
							if (Better(p, best))
								best = p;
						}
					}
					current = best;
					current.searchResolutionRadians = step;
					candidates.push_back(current);
				}
			}
			std::sort(candidates.begin(), candidates.end(), Better);
			std::vector<Proposal> selected;
			for (const auto &p : candidates)
			{
				if (p.training.intervalRmse >
					candidates.front().training.intervalRmse + request.options.alternativeDeltaCodes)
					continue;
				bool duplicate = false;
				for (const auto &s : selected)
					duplicate |= Angle(V(s.toSource), V(p.toSource)) < .25 * Pi / 180;
				if (!duplicate && selected.size() < request.options.maxAlternatives)
					selected.push_back(p);
			}
			if (selected.size() == request.options.maxAlternatives)
				out.diagnostics.push_back("Direction alternatives reached output cap; sampled spread is NOT "
										  "a complete angular confidence set");
			for (const auto &h : request.hints)
				if (h.kind == HintKind::AuthoredSun && h.staticEligible && h.style == 0)
					for (const auto &p : candidates)
						if (p.authoredSeed && Angle(V(p.toSource), V(h.toSource)) < 1e-5 &&
							p.training.intervalRmse > candidates.front().training.intervalRmse +
														  request.options.sunImprovementCodes)
							out.diagnostics.push_back("Authored sun prior contradicted by training "
													  "geometry/RGB; not force-enabled");
			out.proposals.insert(out.proposals.end(), selected.begin(), selected.end());
		}
		Require(!out.proposals.empty(), Code::Unavailable,
				"Unavailable: no positive supported training data/profile; no sun inferred");
		out.frozenKey = Frozen(out);
		out.state = State::UnknownSun;
		out.reason = "Frozen training proposals; independent validation required";
		out.work = engine.work;
		return {};
	}
	catch (const Error &e)
	{
		auto diagnostics = out.diagnostics;
		auto coverage = out.profileCoverage;
		out = FitResult();
		out.work = engine.work;
		out.reason = e.message;
		out.diagnostics = std::move(diagnostics);
		out.profileCoverage = std::move(coverage);
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = FitResult();
		out.reason = "Sun search allocation failure";
		out.work = engine.work;
		return Failure(Code::MemoryLimit, out.reason);
	}
}

Error ValidateAndPublish(const FitResult &fit, Disk::Digest mapKey, const Chunk<Sample> &validation,
						 const Bake::EvaluationContext &geometry, const ReadContext &read, Result &out)
{
	out = Result();
	Engine engine(fit.options, read, geometry);
	try
	{
		ValidateOptions(fit.options);
		engine.Check();
		Require(mapKey == fit.mapKey && geometry.geometryKey == fit.geometryKey &&
					fit.frozenKey == Frozen(fit),
				Code::InvalidInput, "Map/geometry/frozen sun parameters changed");
		Require(!fit.proposals.empty(), Code::Unavailable, "No frozen sun proposals");
		Require(validation.count <= fit.options.maxSamples && validation.data && validation.owner,
				Code::InvalidInput, "Invalid validation lease");
		for (size_t i = 0; i < validation.count; ++i)
			for (const auto &t : fit.trainingGeometry)
			{
				const auto &v = validation.data[i];
				Require(Finite(V(v.receiver.world)) &&
							Length(V(v.receiver.world)) / fit.options.blockWorld < 1e12,
						Code::InvalidInput, "Invalid validation coordinates");
				Require(
					v.receiver.sample != t.sample &&
						Block(v.receiver.world, fit.options) != Block(t.world, fit.options) &&
						Length(V(v.receiver.world) - V(t.world)) >
							3 * (v.footprintRadiusWorld + t.footprintRadius) + fit.options.partitionGapWorld,
					Code::InvalidInput, "Training/validation identity, block or filter-footprint leakage");
			}
		out.diagnostics = fit.diagnostics;
		std::map<std::string, Rows> cache;
		std::map<std::string, std::pair<double, double>> controls;
		for (const auto &p : fit.proposals)
			if (!cache.count(p.profile.id))
				cache.emplace(p.profile.id, Prepare(engine, p.profile, validation));
		for (const auto &p : fit.proposals)
			if (p.model != Model::SunAndSky)
			{
				const auto &r = cache.at(p.profile.id);
				if (!Enough(r, fit.options, true))
					continue;
				std::vector<double> zeros(r.values.size(), 0);
				auto score = Evaluate(engine, p.profile, r, zeros, V(), V(p.skyRgb), p.model);
				if (p.model == Model::SkyOnly)
					controls[p.profile.id].first = score.intervalRmse;
				else
					controls[p.profile.id].second = score.intervalRmse;
			}
		std::vector<Hypothesis> sun, sky;
		for (const auto &p : fit.proposals)
		{
			if (p.model == Model::UniformFillControl)
				continue;
			const auto &r = cache.at(p.profile.id);
			Decision decision;
			decision.profile = p.profile.id;
			decision.model = p.model;
			decision.toSource = p.toSource;
			if (!Enough(r, fit.options, true))
			{
				decision.reason = "Missing positive independent validation coverage";
				out.decisions.push_back(decision);
				continue;
			}
			std::vector<double> basis(r.values.size(), 0);
			Hypothesis h;
			h.fitted = p;
			h.coverage = r.coverage;
			h.receivers = r.evidence;
			if (p.model == Model::SunAndSky &&
				!SunBasis(engine, p.profile, r, V(p.toSource), basis, &h.receivers))
			{
				decision.reason = "Unknown sun visibility on fixed validation support";
				out.decisions.push_back(decision);
				continue;
			}
			h.validation = Evaluate(engine, p.profile, r, basis, V(p.sunRgb), V(p.skyRgb), p.model);
			auto withoutSky = Evaluate(engine, p.profile, r, basis, V(p.sunRgb), V(), p.model);
			h.skySupported =
				withoutSky.intervalRmse - h.validation.intervalRmse >= fit.options.skyVsFillImprovementCodes;
			auto c = controls.at(p.profile.id);
			h.validationSkyOnlyRmse = c.first;
			h.validationFillRmse = c.second;
			bool accurate = h.validation.intervalRmse <= fit.options.acceptableRmseCodes &&
							h.validation.maximumError <= fit.options.maximumErrorCodes;
			if (p.model == Model::SunAndSky)
			{
				bool identifiable = r.coverage.normalEigenRatio >= fit.options.minimumNormalEigenRatio &&
									p.coverage.normalEigenRatio >= fit.options.minimumNormalEigenRatio;
				if (accurate && identifiable && Y(V(p.sunRgb)) > 0 &&
					c.first - h.validation.intervalRmse >= fit.options.sunImprovementCodes &&
					c.second - h.validation.intervalRmse >= fit.options.sunImprovementCodes)
				{
					h.acceptance = "Positive independent directional improvement over sky-only and "
								   "uniform-fill controls; source count/profile/extent remain conditional";
					decision.admitted = true;
					decision.reason = h.acceptance;
					sun.push_back(h);
				}
				else
					decision.reason = "Rejected sun: insufficient directional improvement, normal diversity, "
									  "visibility or independent RGB agreement";
			}
			else if (accurate && Y(V(p.skyRgb)) > 0 &&
					 c.second - h.validation.intervalRmse >= fit.options.skyVsFillImprovementCodes)
			{
				h.acceptance = "Positive sky-only prediction beats uniform fill; no required directional sun";
				decision.admitted = true;
				decision.reason = h.acceptance;
				sky.push_back(h);
			}
			else
				decision.reason = "Sky not distinguished from fill or outside independent error tolerance";
			decision.validation = h.validation;
			decision.evaluated = true;
			out.decisions.push_back(decision);
		}
		if (!sun.empty())
		{
			std::sort(sun.begin(), sun.end(), [](const Hypothesis &a, const Hypothesis &b) {
				return a.validation.huber < b.validation.huber;
			});
			double best = sun.front().validation.intervalRmse;
			std::set<std::string> profiles;
			std::set<size_t> chosen;
			for (size_t i = 0; i < sun.size(); ++i)
				if (sun[i].validation.intervalRmse <= best + fit.options.alternativeDeltaCodes &&
					!profiles.count(sun[i].fitted.profile.id) &&
					out.hypotheses.size() < fit.options.maxAlternatives)
				{
					profiles.insert(sun[i].fitted.profile.id);
					chosen.insert(i);
					out.hypotheses.push_back(sun[i]);
				}
			for (size_t i = 0; i < sun.size(); ++i)
				if (!chosen.count(i) &&
					sun[i].validation.intervalRmse <= best + fit.options.alternativeDeltaCodes &&
					out.hypotheses.size() < fit.options.maxAlternatives)
					out.hypotheses.push_back(sun[i]);
			for (auto &h : sky)
				if (h.validation.intervalRmse <= best + fit.options.alternativeDeltaCodes)
					out.hypotheses.push_back(std::move(h));
			out.state = State::SunHypotheses;
			if (!sky.empty())
				out.state = State::AmbiguousSun;
			out.reason = "Directional hypotheses supported under explicit bake assumptions; not calibrated "
						 "physical lux";
		}
		else if (!sky.empty())
		{
			out.hypotheses = std::move(sky);
			out.state = State::SkyOnly;
			out.reason = "Diffuse sky supported; direct sun not required";
		}
		else
		{
			out.state = State::UnknownSun;
			for (const auto &control : controls)
				if (control.second.second <= fit.options.acceptableRmseCodes)
					out.state = State::UnresolvedEnvironment;
			out.reason = "UnknownSun: insufficient directional evidence or unresolved ambient/bounce/model "
						 "discrepancy";
			out.work = engine.work;
			return Failure(Code::Unavailable, out.reason);
		}
		for (auto &h : out.hypotheses)
		{
			double spread = h.fitted.searchResolutionRadians;
			for (const auto &other : out.hypotheses)
				if (h.fitted.model == Model::SunAndSky && other.fitted.model == Model::SunAndSky)
					spread = std::max(spread, Angle(V(h.fitted.toSource), V(other.fitted.toSource)));
			h.sampledAngularAmbiguityRadians = spread;
			if (h.fitted.model == Model::SunAndSky && spread > 5 * Pi / 180)
				out.state = State::AmbiguousSun;
			const auto &r = cache.at(h.fitted.profile.id);
			std::vector<double> basis(r.values.size(), 0);
			if (h.fitted.model == Model::SunAndSky)
				Require(SunBasis(engine, h.fitted.profile, r, V(h.fitted.toSource), basis), Code::Unavailable,
						"Visibility changed during sun uncertainty evaluation");
			auto slice = [&](bool isSun) -> Estimate {
				V q(h.fitted.sunRgb), k(h.fitted.skyRgb), target = isSun ? q : k;
				double intensity = Y(target);
				Estimate e;
				if (intensity <= 0 || h.fitted.amplitudeDegenerate)
					return e;
				e.known = e.bounded = true;
				e.value = float(intensity);
				for (int side = 0; side < 2; ++side)
				{
					double good = 1,
						   bad = side ? fit.options.amplitudeUpper / std::max(1e-10, Peak(target)) : 0;
					for (int iter = 0; iter < 12; ++iter)
					{
						double s = (good + bad) / 2;
						auto score = Evaluate(engine, h.fitted.profile, r, basis, isSun ? q * s : q,
											  isSun ? k : k * s, h.fitted.model);
						if (score.intervalRmse <= fit.options.acceptableRmseCodes &&
							score.maximumError <= fit.options.maximumErrorCodes)
							good = s;
						else
							bad = s;
					}
					if (side)
						e.upper = float(good * intensity);
					else
						e.lower = float(good * intensity);
				}
				return e;
			};
			h.sunIntensitySlice = slice(true);
			h.skyIntensitySlice = slice(false);
			Canonical(fit, h);
		}
		out.work = engine.work;
		return {};
	}
	catch (const Error &e)
	{
		out = Result();
		out.reason = e.message;
		out.work = engine.work;
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = Result();
		out.reason = "Sun validation allocation failure";
		out.work = engine.work;
		return Failure(Code::MemoryLimit, out.reason);
	}
}
const char *StateName(State s)
{
	switch (s)
	{
	case State::Unavailable:
		return "Unavailable";
	case State::UnknownSun:
		return "UnknownSun";
	case State::UnresolvedEnvironment:
		return "UnresolvedEnvironment";
	case State::SkyOnly:
		return "SkyOnly";
	case State::SunHypotheses:
		return "SunHypotheses";
	case State::AmbiguousSun:
		return "AmbiguousSun";
	}
	return "Invalid";
}
Error Assess(const Proposal &p, const Options &o, const Chunk<Sample> &samples,
			 const Bake::EvaluationContext &geometry, const ReadContext &read, Assessment &out)
{
	out = Assessment();
	Engine engine(o, read, geometry);
	try
	{
		ValidateOptions(o);
		Rows rows = Prepare(engine, p.profile, samples);
		Require(!rows.values.empty(), Code::Unavailable, "No comparable frozen prediction receivers");
		std::vector<double> basis(rows.values.size(), 0);
		out.receivers = rows.evidence;
		if (p.model == Model::SunAndSky)
			Require(SunBasis(engine, p.profile, rows, V(p.toSource), basis, &out.receivers),
					Code::Unavailable, "Frozen sun prediction has unknown visibility");
		out.coverage = rows.coverage;
		out.score = Evaluate(engine, p.profile, rows, basis, V(p.sunRgb), V(p.skyRgb), p.model);
		out.work = engine.work;
		return {};
	}
	catch (const Error &e)
	{
		out = Assessment();
		out.work = engine.work;
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = Assessment();
		out.work = engine.work;
		return Failure(Code::MemoryLimit, "Sun assessment allocation failure");
	}
}
} // namespace Solar
} // namespace Core
} // namespace Rend2LightRecon
