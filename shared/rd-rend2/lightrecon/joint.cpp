// Deterministic structural search with complete production forward reevaluation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Joint
{
using namespace Bake::Detail;
namespace
{
void Need(bool b, Code c, const std::string &s)
{
	if (!b)
		throw Failure(c, s);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
bool Zero(const Disk::Digest &d) { return d == Disk::Digest{}; }
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
template <class T> class Rows final : public ChunkSource<T>
{
  public:
	std::shared_ptr<std::vector<T>> v = std::make_shared<std::vector<T>>();
	Error Read(uint64_t a, size_t n, const ReadContext &r, Chunk<T> &out) const override
	{
		if (Cancelled(r))
			return Failure(Code::Cancelled, "Joint row read cancelled");
		if (a > v->size() || n > v->size() - a)
			return Failure(Code::ProviderError, "Joint row span");
		out = VectorChunk<T>(v, size_t(a), n);
		return {};
	}
};
double Component(Vec3 v, unsigned c) { return V(v)[c]; }
class TransferVisibility final : public VisibilityProvider
{
	std::shared_ptr<const Indirect::Transfer> transfer;
	Work &work;
	const Options &options;

  public:
	TransferVisibility(std::shared_ptr<const Indirect::Transfer> t, Work &w, const Options &o)
		: transfer(std::move(t)), work(w), options(o)
	{
	}
	Error Query(const RayQuery &q, const ReadContext &r, RayResult &result) const override
	{
		if (work.rays >= options.maxRays || work.traversal + work.arithmetic >= options.maxWork)
			return Failure(Code::WorkLimit, "Joint native ray budget");
		RayQuery bounded = q;
		bounded.maxWork = std::min(q.maxWork, options.maxWork - work.traversal - work.arithmetic);
		++work.rays;
		auto error = transfer->QueryPath(bounded, r, result);
		work.traversal += result.nodeTests + result.primitiveTests;
		return error;
	}
};
void Set(Vec3 &v, unsigned c, double x)
{
	V a(v);
	a[c] = x;
	v = a.Float();
}
double Residual(double p, const Amplitude::Interval &x)
{
	return p < x.lower ? p - x.lower : p > x.upper ? p - x.upper : 0;
}
double Huber(double e, double d)
{
	double a = std::abs(e);
	return a <= d ? .5 * e * e : d * (a - .5 * d);
}
bool Inside(const Bounds &b, Vec3 p)
{
	if (!b.known)
		return false;
	for (unsigned c = 0; c < 3; ++c)
		if (Component(p, c) < Component(b.minWorld, c) || Component(p, c) > Component(b.maxWorld, c))
			return false;
	return true;
}
V Rotate(V v, V axis, double a)
{
	return v * std::cos(a) + Cross(axis, v) * std::sin(a) + axis * (Dot(axis, v) * (1 - std::cos(a)));
}
Frame Rotation(Frame f, unsigned axis, double a)
{
	V u;
	u[axis] = 1;
	f.right = Rotate(V(f.right), u, a).Float();
	f.up = Rotate(V(f.up), u, a).Float();
	f.forward = Rotate(V(f.forward), u, a).Float();
	return f;
}
void HashSource(GeometryKey &k, const Source &s, bool amplitude)
{
	k.U(s.candidate.id);
	k.U(unsigned(s.candidate.root));
	k.U(unsigned(s.value.geometry.kind));
	k.V(s.value.geometry.position.world);
	k.V(s.value.geometry.frame.right);
	k.V(s.value.geometry.frame.up);
	k.V(s.value.geometry.frame.forward);
	k.F(s.value.geometry.geometry.halfWidthWorld);
	k.F(s.value.geometry.geometry.halfHeightWorld);
	k.F(s.value.geometry.geometry.halfLengthWorld);
	k.F(s.value.geometry.geometry.radiusWorld);
	k.U(unsigned(s.value.geometry.angular.kind));
	k.U(s.value.geometry.angular.twoSided);
	k.F(s.value.geometry.angular.cosInner);
	k.F(s.value.geometry.angular.cosOuter);
	k.F(s.value.spotTargetDistanceWorld);
	k.F(s.value.spotRadiusWorld);
	k.V(s.environment.orientation.forward);
	k.U(unsigned(s.environment.kind));
	if (amplitude)
		k.V(s.value.rgbAmplitude);
}
Disk::Digest ModelKey(const Model &m, bool values = true)
{
	GeometryKey k;
	k.U(kVersion);
	k.U(m.profile);
	if (values)
	{
		k.Digest(m.trainingKey);
		k.Digest(m.geometryKey);
		k.Digest(m.policyKey);
		k.Digest(m.transferKey);
	}
	for (const auto &s : m.sources)
	{
		if (values)
			HashSource(k, s, true);
		else
			k.U(s.candidate.id);
	}
	if (values)
		for (const auto &x : m.materials)
		{
			k.U(x.material);
			k.V(x.initial);
		}
	return k.Finish();
}
Source FromCandidate(const Candidate &c)
{
	Source s;
	s.candidate = c;
	s.value = c.shape.initial;
	s.environment = c.environment;
	s.value.geometry.source.id = c.id;
	s.value.geometry.source.state = HypothesisState::Candidate;
	s.environment.id = c.id;
	return s;
}
size_t Parameters(const Source &s)
{
	const auto &c = s.candidate;
	size_t n = c.fitAmplitude ? 3 : 0;
	if (c.root != TransportRootKind::LocalEmitter)
		return n + (c.fitEnvironmentDirection ? 2 : 0);
	if (c.shape.fitPosition)
		n += 3;
	if (c.shape.fitFrame && s.value.geometry.kind != EmitterKind::Point)
		n += s.value.geometry.kind == EmitterKind::Rectangle ? 3 : 2;
	if (c.shape.fitCone && s.value.geometry.kind == EmitterKind::Spot)
		n += 2;
	if (c.shape.fitDimensions &&
		(s.value.geometry.kind == EmitterKind::Rectangle || s.value.geometry.kind == EmitterKind::Tube))
		n += 2;
	return n;
}
struct Evaluation
{
	Terms terms;
	Score score;
	std::vector<Prediction> values;
	double reflected = 0;
	Estimate tail;
	bool linear = false;
	std::vector<RootPower> roots;
};
struct Engine
{
	const Request &r;
	const ReadContext &read;
	Work work;
	std::vector<Observation> training;
	Disk::Digest identity{};
	std::shared_ptr<const PointRefine::PlacementProvider> placement;
	std::map<Disk::Digest, std::shared_ptr<const Shape::Block>> cache;
	uint64_t cacheSize = 0;
	uint64_t baseBytes = 0;
	bool linearProof = false;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	std::map<Id, size_t> patchIndex;
	Bake::EvaluationContext effectiveGeometry;
	Engine(const Request &q, const ReadContext &ctx) : r(q), read(ctx)
	{
		Need(r.transfer && !Zero(r.mapKey) && !Zero(r.policyKey) && r.transfer->MapKey() == r.mapKey &&
				 r.transfer->GeometryKey() == r.geometry.geometryKey,
			 Code::MissingGeometry, "Joint geometry/map generation mismatch");
		Need(!r.profiles.empty() && r.profiles.size() <= 16 && r.options.beamWidth &&
				 r.options.beamWidth <= 16 && r.options.maxCandidates && r.options.maxCandidates <= 512 &&
				 r.options.maxSources && r.options.maxSources <= 64 && r.options.maxModels &&
				 r.options.maxModels <= 512 && r.options.restarts && r.options.restarts <= 32 &&
				 r.options.batchRows && r.options.batchRows <= 1024 && r.options.maxRetained &&
				 r.options.maxRetained <= 128,
			 Code::InvalidInput, "Joint bounded search options");
		Need(r.options.diagnosticSources <= 16 && r.options.diagnosticRows <= 512 &&
				 r.options.amplitudeSweeps <= 256 && r.options.amplitudeLineSteps <= 128 &&
				 r.options.geometrySweeps <= 64 && r.options.jointSweeps <= 64,
			 Code::InvalidInput, "Joint diagnostic/iteration limits");
		for (double x : {r.options.huber, r.options.parameterTolerance, r.options.positionStepWorld,
						 r.options.angularStepRadians, r.options.logDimensionStep})
			Need(std::isfinite(x) && x > 0, Code::InvalidInput, "Joint positive finite numerical tolerance");
		for (double x :
			 {r.options.objectiveTolerance, r.options.complexityPerSource, r.options.complexityPerParameter,
			  r.options.equivalentValidationSlack, r.options.worstBlockWeight,
			  r.options.maximumRequiredBlockRmse, r.options.minimumValidationImprovement, r.shadowWeight})
			Need(std::isfinite(x) && x >= 0, Code::InvalidInput, "Joint finite nonnegative objective policy");
		for (double x : r.options.channelWeight)
			Need(std::isfinite(x) && x >= 0, Code::InvalidInput, "Joint channel weight");
		for (const auto &p : r.profiles)
		{
			Need(p.model.units.worldUnitsPerMeter == p.contract.units.worldUnitsPerMeter, Code::InvalidInput,
				 "Forward and observation geometry scales must agree");
			if (p.model.law == Shape::Law::HistoricalBake)
				Need(p.model.profile.contentKey == p.contract.profile.contentKey, Code::InvalidInput,
					 "Forward and observation bake-profile hypotheses conflict");
		}
		Need(r.shadowWeight == 0 || (r.shadow && r.shadow->MapKey() == r.mapKey &&
									 r.shadow->GeometryKey() == r.geometry.geometryKey),
			 Code::MissingGeometry, "Joint shadow evidence generation mismatch");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes) : r.options.maxBytes;
		Need(r.transfer->Patches().size() * 1024ull + r.options.cacheBytes + 65536 <= cap, Code::MemoryLimit,
			 "Joint patch/cache budget");
		work.accountedBytes = r.transfer->Patches().size() * 1024ull + r.options.cacheBytes + 65536;
		baseBytes = work.accountedBytes;
		for (size_t i = 0; i < r.transfer->Patches().size(); ++i)
			patchIndex[r.transfer->Patches()[i].id] = i;
		Check(PointRefine::MakePlacement(r.placement, read, placement));
		effectiveGeometry = r.geometry;
		effectiveGeometry.visibility = std::make_shared<TransferVisibility>(r.transfer, work, r.options);
		Need(placement->MapKey() == r.mapKey && placement->GeometryKey() == r.geometry.geometryKey,
			 Code::MissingGeometry, "Joint placement generation");
	}
	void Limit(uint64_t n = 1)
	{
		Need(!Cancelled(read), Code::Cancelled, "Joint search cancelled");
		Need(work.evaluations <= r.options.maxEvaluations && work.rays <= r.options.maxRays &&
				 work.traversal + work.arithmetic <= r.options.maxWork,
			 Code::WorkLimit, "Joint cumulative forward/ray/work limit");
		work.arithmetic += n;
		if (r.options.timeLimitMilliseconds)
			Need(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
							  std::chrono::steady_clock::now() - start)
							  .count()) < r.options.timeLimitMilliseconds,
				 Code::WorkLimit, "Joint wall time safety limit");
	}
	std::vector<Observation> Load(std::shared_ptr<const ChunkSource<Observation>> src, uint64_t count,
								  Role role)
	{
		Need(src && count && count <= r.options.maxObservations, Code::InvalidInput,
			 "Joint explicit nonempty partition");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes) : r.options.maxBytes;
		Need(count * sizeof(Observation) * 3 + work.accountedBytes <= cap, Code::MemoryLimit,
			 "Joint observation budget");
		std::vector<Observation> rows;
		rows.reserve(size_t(count));
		Id last = 0;
		for (uint64_t a = 0; a < count; a += r.options.batchRows)
		{
			Limit();
			Chunk<Observation> c;
			Check(src->Read(a, size_t(std::min<uint64_t>(r.options.batchRows, count - a)), read, c));
			Need(c.owner && c.data && c.count == std::min<uint64_t>(r.options.batchRows, count - a),
				 Code::ProviderError, "Joint truncated partition");
			for (size_t j = 0; j < c.count; ++j)
			{
				auto x = c.data[j];
				Need(x.id > last && x.role == role && x.block &&
						 unsigned(x.channel) < unsigned(Channel::Count),
					 Code::InvalidInput, "Joint identity/order/partition role");
				last = x.id;
				Need(std::isfinite(x.measure) && x.measure > 0 && std::isfinite(x.sigma) && x.sigma > 0 &&
						 std::isfinite(x.angularSigmaRadians) && x.angularSigmaRadians > 0,
					 Code::InvalidInput, "Joint observation weights");
				if (x.channel == Channel::Surface)
					Need(patchIndex.count(x.patch), Code::InvalidInput,
						 "Joint observation references missing patch");
				for (auto v : x.rgb)
					Need(!v.present || (std::isfinite(v.lower) && v.lower >= 0 && !std::isnan(v.upper) &&
										v.upper >= v.lower),
						 Code::InvalidInput, "Joint invalid/censored interval");
				Need(!x.directionKnown || Normal(x.directionToSource), Code::InvalidInput,
					 "Joint direction convention/unit vector");
				rows.push_back(x);
			}
		}
		return rows;
	}
	void Pin()
	{
		training = Load(r.training, r.count, Role::Training);
		GeometryKey k;
		k.U(kVersion);
		k.Digest(Configuration());
		k.Digest(r.mapKey);
		k.Digest(r.trainingKey);
		k.Digest(r.policyKey);
		k.Digest(r.transfer->Key());
		k.Digest(placement->PolicyKey());
		k.U(r.options.seed);
		for (const auto &p : r.profiles)
		{
			k.Digest(p.model.profile.contentKey);
			k.U(unsigned(p.model.law));
			k.U(unsigned(p.contract.operation));
			k.F(p.fixedGain);
			Text(k, p.calibrationHypothesis);
		}
		for (const auto &x : training)
		{
			k.U(x.id);
			k.U(x.patch);
			k.U(x.region);
			k.U(x.block);
			k.U(unsigned(x.channel));
			k.V(x.receiver.world);
			k.V(x.receiver.geometricNormal);
			k.V(x.directionToSource);
			k.U(x.directionKnown);
			k.U(x.comparable);
			k.U(x.required);
			k.F(x.measure);
			k.F(x.sigma);
			k.F(x.angularSigmaRadians);
			for (auto v : x.rgb)
			{
				k.U(v.present);
				k.U(v.censored);
				k.F(v.lower);
				k.F(v.upper);
			}
		}
		std::vector<Candidate> cs = r.candidates;
		std::sort(cs.begin(), cs.end(), [](const Candidate &a, const Candidate &b) { return a.id < b.id; });
		for (const auto &c : cs)
		{
			HashSource(k, FromCandidate(c), true);
			k.U(c.family);
			k.U(c.region);
			k.F(c.clearanceWorld);
			k.F(c.maximumEnvironmentRotationRadians);
			k.U(c.initialActive);
			k.U(c.mayRemove);
			k.U(c.fitAmplitude);
			k.U(c.fitEnvironmentDirection);
			k.B(c.shape.centreBounds);
			k.V(c.shape.lowerRgb);
			k.V(c.shape.upperRgb);
			k.U(c.shape.fitPosition);
			k.U(c.shape.fitFrame);
			k.U(c.shape.fitDimensions);
			k.U(c.shape.fitCone);
			k.F(c.shape.maximumTranslationWorld);
			k.F(c.shape.maximumRotationRadians);
			k.F(c.shape.geometryPriorWeight);
			Text(k, c.provenance);
		}
		for (const auto &m : r.materials)
		{
			k.U(m.material);
			k.U(unsigned(m.knowledge));
			k.V(m.lower);
			k.V(m.upper);
			k.V(m.initial);
			k.F(m.priorWeight);
		}
		for (const auto &b : r.boundaryRoots)
		{
			k.Digest(b.generation);
			k.U(b.source);
		}
		for (double x : r.options.channelWeight)
			k.F(x);
		k.U(r.options.beamWidth);
		k.U(r.options.maxModels);
		k.U(r.options.maxSources);
		k.U(r.options.rounds);
		k.U(r.options.restarts);
		k.U(r.options.residualCandidates);
		k.U(r.options.residualGridDivisions);
		k.U(r.options.maxRetained);
		k.U(r.options.diagnosticSources);
		k.U(r.options.diagnosticRows);
		k.U(r.options.coarseRows);
		k.U(r.options.amplitudeSweeps);
		k.U(r.options.amplitudeLineSteps);
		k.U(r.options.geometrySweeps);
		k.U(r.options.jointSweeps);
		k.U(r.options.geometry);
		k.U(r.options.materials);
		k.U(r.options.residualSearch);
		k.U(r.options.mergeSplit);
		k.U(r.options.conditionalUnknownMaterials);
		k.F(r.options.huber);
		k.F(r.options.complexityPerSource);
		k.F(r.options.complexityPerParameter);
		k.F(r.options.equivalentValidationSlack);
		k.F(r.options.worstBlockWeight);
		k.F(r.options.maximumRequiredBlockRmse);
		k.F(r.options.minimumValidationImprovement);
		k.F(r.options.positionStepWorld);
		k.F(r.options.angularStepRadians);
		k.F(r.options.logDimensionStep);
		k.F(r.options.objectiveTolerance);
		k.F(r.options.parameterTolerance);
		if (r.shadowWeight)
		{
			k.Digest(r.shadow->Key());
			k.F(r.shadowWeight);
		}
		identity = k.Finish();
	}
	Disk::Digest Configuration() const
	{
		GeometryKey k;
		k.U(kVersion);
		k.Digest(r.policyKey);
		k.Digest(r.transfer->Key());
		for (const auto &p : r.profiles)
		{
			k.Digest(p.model.profile.contentKey);
			k.U(unsigned(p.model.law));
			k.U(unsigned(p.model.output));
			k.F(p.model.units.worldUnitsPerMeter);
			k.U(p.model.calibration.id);
			k.U(unsigned(p.model.calibration.kind));
			k.U(p.model.allowAssumed);
			k.U(p.model.allowFacetExpansion);
			k.U(p.model.rectangleDivisions);
			k.U(p.model.tubeSides);
			k.U(p.model.tubeSegments);
			k.F(p.model.historicalEmitterPushWorld);
			k.U(unsigned(p.contract.operation));
			k.U(unsigned(p.contract.incidentDomain));
			k.U(unsigned(p.contract.unit));
			k.U(p.contract.calibration.id);
			k.U(unsigned(p.contract.calibration.kind));
			Text(k, p.contract.calibration.method);
			k.U(p.contract.transferAttested);
			k.U(p.contract.primariesAttested);
			k.U(p.contract.physicalTransportAssumed);
			k.F(p.fixedGain);
			Text(k, p.calibrationHypothesis);
		}
		for (const auto &s : {r.coarseSampling, r.strictSampling})
		{
			k.U(s.areaDivisions);
			k.U(s.tubeSides);
			k.U(s.tubeSegments);
			k.U(s.receiverSubdivisions);
			k.U(s.compareRefined);
			k.F(s.receiverOffsetWorld);
			k.F(s.endpointToleranceWorld);
			k.F(s.minimumDistanceWorld);
		}
		for (const auto &m : r.materials)
		{
			k.U(m.material);
			k.U(unsigned(m.knowledge));
			k.V(m.lower);
			k.V(m.upper);
			k.V(m.initial);
			k.F(m.priorWeight);
		}
		k.B(r.sourceSearchBounds);
		return k.Finish();
	}
	bool Place(const Model &m)
	{
		if (m.sources.size() > r.options.maxSources)
			return false;
		std::set<Id> ids, families;
		for (const auto &s : m.sources)
		{
			const auto &c = s.candidate;
			if (!c.id || !ids.insert(c.id).second || (c.family && !families.insert(c.family).second))
				return false;
			if (!Positive(s.value.rgbAmplitude))
				return false;
			for (unsigned j = 0; j < 3; ++j)
				if (Component(s.value.rgbAmplitude, j) < Component(c.shape.lowerRgb, j) ||
					Component(s.value.rgbAmplitude, j) > Component(c.shape.upperRgb, j))
					return false;
			if (c.root != TransportRootKind::LocalEmitter)
			{
				if (c.root == TransportRootKind::Sun &&
					(!s.environment.orientation.known || !Normal(s.environment.orientation.forward)))
					return false;
				if (c.root == TransportRootKind::Sun && c.environment.orientation.known)
				{
					double angle = std::acos(
						Clamp(Dot(V(s.environment.orientation.forward), V(c.environment.orientation.forward)),
							  -1., 1.));
					if (angle > c.maximumEnvironmentRotationRadians + 1e-5)
						return false;
				}
				continue;
			}
			const auto &p = s.value.geometry;
			if (!Finite(p.position.world) || !Finite(c.shape.centreBounds.minWorld) ||
				!Finite(c.shape.centreBounds.maxWorld) || !Inside(c.shape.centreBounds, p.position.world) ||
				Length(V(p.position.world) - V(c.shape.initial.geometry.position.world)) >
					c.shape.maximumTranslationWorld + 1e-5)
				return false;
			if (p.kind != EmitterKind::Point)
			{
				if (!p.frame.known || !Normal(p.frame.forward) || !Normal(p.frame.right) ||
					Length(Cross(V(p.frame.right), V(p.frame.up)) - V(p.frame.forward)) > 1e-4)
					return false;
				double trace = Dot(V(p.frame.right), V(c.shape.initial.geometry.frame.right)) +
							   Dot(V(p.frame.up), V(c.shape.initial.geometry.frame.up)) +
							   Dot(V(p.frame.forward), V(c.shape.initial.geometry.frame.forward));
				double a = std::acos(Clamp((trace - 1) * .5, -1., 1.));
				if (a > c.shape.maximumRotationRadians + 1e-5)
					return false;
			}
			if (p.kind == EmitterKind::Rectangle || p.kind == EmitterKind::Tube)
			{
				double a = p.kind == EmitterKind::Rectangle ? 2 * p.geometry.halfWidthWorld
															: 2 * p.geometry.halfLengthWorld;
				double b = p.kind == EmitterKind::Rectangle ? 2 * p.geometry.halfHeightWorld
															: 2 * p.geometry.radiusWorld;
				if (a < c.shape.minimumDimensionWorld || a > c.shape.maximumDimensionWorld ||
					b < c.shape.minimumDimensionWorld || b > c.shape.maximumDimensionWorld)
					return false;
			}
			PointRefine::PlacementResult placed;
			Check(placement->Check(c.shape.initial.geometry.position.world, p.position.world,
								   c.clearanceWorld, read, placed));
			work.rays += placed.rays;
			work.traversal += placed.work;
			Limit();
			if (placed.state != PointRefine::Constraint::Free)
				return false;
			// Extended emitting support is checked explicitly; a free centre alone is insufficient.
			if (p.kind == EmitterKind::Rectangle || p.kind == EmitterKind::Tube)
			{
				double radius = 0;
				for (unsigned axis = 0; axis < 3; ++axis)
				{
					double extent =
						p.kind == EmitterKind::Rectangle
							? std::abs(Component(p.frame.right, axis)) * p.geometry.halfWidthWorld +
								  std::abs(Component(p.frame.up, axis)) * p.geometry.halfHeightWorld
							: std::abs(Component(p.frame.right, axis)) * p.geometry.halfLengthWorld +
								  p.geometry.radiusWorld *
									  std::sqrt(std::pow(Component(p.frame.up, axis), 2) +
												std::pow(Component(p.frame.forward, axis), 2));
					radius = std::max(radius, extent);
				}
				Check(placement->Check(p.position.world, p.position.world, radius + c.clearanceWorld, read,
									   placed));
				work.rays += placed.rays;
				work.traversal += placed.work;
				Limit();
				if (placed.state != PointRefine::Constraint::Free)
					return false;
				unsigned count = p.kind == EmitterKind::Rectangle ? 4 : 16;
				for (unsigned j = 0; j < count; ++j)
				{
					V target = p.position.world;
					if (p.kind == EmitterKind::Rectangle)
						target = target +
								 V(p.frame.right) *
									 (j & 1 ? p.geometry.halfWidthWorld : -p.geometry.halfWidthWorld) +
								 V(p.frame.up) *
									 (j & 2 ? p.geometry.halfHeightWorld : -p.geometry.halfHeightWorld);
					else
					{
						double a = 2 * Pi * (j % 8) / 8;
						target = target +
								 V(p.frame.right) *
									 (j < 8 ? -p.geometry.halfLengthWorld : p.geometry.halfLengthWorld) +
								 V(p.frame.up) * (p.geometry.radiusWorld * std::cos(a)) +
								 V(p.frame.forward) * (p.geometry.radiusWorld * std::sin(a));
					}
					Check(placement->Check(p.position.world, target.Float(), 0, read, placed));
					work.rays += placed.rays;
					work.traversal += placed.work;
					Limit();
					if (placed.state != PointRefine::Constraint::Free)
						return false;
				}
			}
		}
		return true;
	}
	Indirect::Root Primary(const Model &m, const Source &s, bool strict)
	{
		const auto &p = r.profiles.at(m.profile);
		Indirect::Root root;
		root.source = s.candidate.id;
		root.kind = s.candidate.root;
		root.domain = p.contract.incidentDomain;
		root.calibration = p.contract.calibration.id;
		root.sunCoverage =
			root.kind == TransportRootKind::Sky ? SunCoverage::ExcludesExplicitSun : SunCoverage::Unknown;
		root.provenance = s.candidate.provenance;
		GeometryKey generation;
		generation.Digest(r.transfer->Key());
		generation.Digest(p.model.profile.contentKey);
		HashSource(generation, s, true);
		generation.F(p.fixedGain);
		for (size_t first = 0; first < r.transfer->Patches().size(); first += r.options.batchRows)
		{
			Limit();
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
			for (size_t i = first; i < std::min(first + r.options.batchRows, r.transfer->Patches().size());
				 ++i)
				receivers->push_back(r.transfer->Patches()[i].receiver);
			if (root.kind == TransportRootKind::LocalEmitter)
			{
				auto model = p.model;
				model.output = model.output == Shape::Output::PhysicalLux ? Shape::Output::PhysicalLux
																		  : Shape::Output::RelativeIncident;
				GeometryKey key;
				key.Digest(r.transfer->Key());
				key.Digest(p.model.profile.contentKey);
				key.U(m.profile);
				key.U(strict);
				key.U(first);
				HashSource(key, s, false);
				auto digest = key.Finish();
				std::shared_ptr<const Shape::Block> block;
				auto found = cache.find(digest);
				if (found != cache.end())
				{
					block = found->second;
					++work.cacheHits;
				}
				else
				{
					Check(Shape::Prepare(model, {s.value}, Lease(receivers), effectiveGeometry,
										 strict ? r.strictSampling : r.coarseSampling, read, block));
					++work.preparationCalls;
					const auto &w = block->PreparationWork();
					work.pairs += w.pairs;
					Limit();
					if (block->Bytes() <= r.options.cacheBytes)
					{
						while (cacheSize + block->Bytes() > r.options.cacheBytes && !cache.empty())
						{
							cacheSize -= cache.begin()->second->Bytes();
							cache.erase(cache.begin());
						}
						cache[digest] = block;
						cacheSize += block->Bytes();
					}
				}
				std::vector<Shape::Prediction> values;
				linearProof &= block->Linear(s.candidate.shape.lowerRgb, s.candidate.shape.upperRgb, 0);
				Shape::Work w;
				Check(block->Evaluate({s.value.rgbAmplitude}, read, values, w));
				++work.forwardCalls;
				work.pairs += w.pairs;
				Limit();
				for (const auto &x : values)
				{
					root.incident.push_back((V(x.value) * p.fixedGain).Float());
					root.available.push_back(x.available);
				}
			}
			else
			{
				Need(p.model.law == Shape::Law::HistoricalBake, Code::Unavailable,
					 "Physical sun/sky integral needs an explicit supported environment operator");
				auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>(1, s.environment);
				(*sources)[0].compilerRgbAmplitude = s.value.rgbAmplitude;
				(*sources)[0].amplitudeKnown = true;
				Bake::Options o;
				o.output = Bake::Domain::CompilerLinearRelative255;
				o.allowAssumed = p.model.allowAssumed;
				o.maxRays = std::max<uint64_t>(1, r.options.maxRays - work.rays);
				o.maxWork = std::max<uint64_t>(1, r.options.maxWork - work.traversal);
				Bake::Result values;
				Bake::CpuForwardEvaluator evaluator;
				Check(evaluator.Evaluate(p.model.profile, Lease(sources), Lease(receivers), {},
										 effectiveGeometry, o, read, values));
				++work.forwardCalls;
				work.pairs += values.work.pairs;
				Limit();
				for (size_t i = 0; i < values.predictions.count; ++i)
				{
					auto x = values.predictions.data[i];
					root.incident.push_back((V(x.compilerRelative) * p.fixedGain).Float());
					root.available.push_back(x.available);
				}
			}
		}
		for (size_t i = 0; i < root.incident.size(); ++i)
		{
			generation.V(root.incident[i]);
			generation.U(root.available[i]);
		}
		root.generation = generation.Finish();
		return root;
	}
	std::vector<Indirect::PatchValue> Surface(const Model &m, bool strict, double &reflected, Estimate &tail,
											  std::vector<RootPower> &roots)
	{
		const auto &p = r.profiles.at(m.profile);
		Need(std::isfinite(p.fixedGain) && p.fixedGain > 0 && !p.calibrationHypothesis.empty(),
			 Code::InvalidInput, "Explicit fixed calibration gauge required");
		if (p.model.law == Shape::Law::HistoricalBake)
		{
			Bake::Recipe recipe;
			Check(Bake::DescribeRecipe(p.model.profile, recipe));
			Need(Peak(recipe.explicitSurfaceAmbient) == 0, Code::InvalidInput,
				 "Joint ambient must be one identified boundary root, not repeated per emitter");
		}
		std::vector<Indirect::PatchValue> total(r.transfer->Patches().size());
		for (size_t i = 0; i < total.size(); ++i)
		{
			total[i].patch = r.transfer->Patches()[i].id;
			total[i].comparable = p.contract.transferAttested && p.contract.primariesAttested;
			total[i].upperKnown = true;
		}
		double primaryPower = 0, reflectedPower = 0, rhoMax = 0;
		std::set<Id> rootIdentities;
		for (const auto &x : m.materials)
			rhoMax = std::max(rhoMax, Peak(x.initial));
		auto accumulate = [&](const Indirect::Root &root) {
			Need(rootIdentities.insert(root.source).second, Code::InvalidInput,
				 "Duplicate root identity would double count direct/reflected energy");
			Need(root.kind != TransportRootKind::Unknown, Code::Unavailable,
				 "Unknown-origin root cannot be relabelled ambient or physical bounce; use the separate "
				 "frozen unresolved stage");
			Need(root.source && !root.provenance.empty() && root.domain == p.contract.incidentDomain &&
					 root.calibration == p.contract.calibration.id,
				 Code::InvalidInput, "Root photometric domain/calibration/provenance mismatch");
			Need(root.kind != TransportRootKind::Sky || root.sunCoverage == SunCoverage::ExcludesExplicitSun,
				 Code::InvalidInput, "Sky root direct-sun membership must be explicit");
			for (auto rgb : root.incident)
				Need(Positive(rgb), Code::InvalidInput, "Invalid fixed root irradiance");
			RootPower power;
			power.source = root.source;
			power.kind = root.kind;
			if (!p.contract.physicalTransportAssumed)
			{
				Need(root.incident.size() == total.size() && root.available.size() == total.size(),
					 Code::InvalidInput, "Boundary root span mismatch");
				for (size_t i = 0; i < total.size(); ++i)
				{
					auto &t = total[i];
					Vec3 *component = root.kind == TransportRootKind::LocalEmitter
										  ? &t.direct
										  : root.kind == TransportRootKind::Sun
												? &t.sun
												: root.kind == TransportRootKind::Sky ? &t.sky : &t.ambient;
					*component = (V(*component) + V(root.incident[i])).Float();
					t.comparable &= root.available[i] != 0;
					if (!root.available[i])
						t.issues |= Indirect::MissingPrimary;
					double area = r.transfer->Patches()[i].areaWorldSquared;
					power.primaryAreaWeighted +=
						area * (root.incident[i].x + root.incident[i].y + root.incident[i].z);
					power.available &= root.available[i] != 0;
				}
				primaryPower += power.primaryAreaWeighted;
				roots.push_back(power);
				return;
			}
			Indirect::Request q;
			q.transfer = r.transfer;
			q.roots = {root};
			q.materials = m.materials;
			q.contract = p.contract;
			q.contract.operation = Indirect::ObservationOperator::IncidentLinear;
			q.contract.provenance += "; S11 fixed-value passive propagation";
			for (auto &x : q.materials)
			{
				Need(x.knowledge != Indirect::MaterialKnowledge::Unknown ||
						 r.options.conditionalUnknownMaterials,
					 Code::Unavailable,
					 "Unknown reflectance requires explicit conditional material hypothesis");
				x.knowledge = Indirect::MaterialKnowledge::Known;
				x.lower = x.upper = x.initial;
			}
			auto data = std::make_shared<Rows<Indirect::Observation>>();
			Indirect::Observation o;
			o.id = 1;
			o.patch = r.transfer->Patches()[0].id;
			o.spatialBlock = 1;
			o.areaWorldSquared = r.transfer->Patches()[0].areaWorldSquared;
			data->v->push_back(o);
			q.training = data;
			q.count = 1;
			q.trainingKey = identity;
			q.options.maxIterations = 1;
			q.options.maxWork = std::max<uint64_t>(1, r.options.maxWork - work.traversal);
			q.options.maxBytes = r.options.maxBytes;
			Indirect::Result value;
			Check(Indirect::Fit(q, read, value));
			work.links += value.work.linkOperations;
			work.arithmetic += value.work.linkOperations;
			Limit();
			Need(value.values.size() == total.size(), Code::AlgorithmError,
				 "Passive propagation result span");
			for (size_t i = 0; i < total.size(); ++i)
			{
				auto &a = total[i];
				const auto &b = value.values[i];
				a.direct = (V(a.direct) + V(b.direct)).Float();
				a.sun = (V(a.sun) + V(b.sun)).Float();
				a.sky = (V(a.sky) + V(b.sky)).Float();
				a.ambient = (V(a.ambient) + V(b.ambient)).Float();
				a.indirect = (V(a.indirect) + V(b.indirect)).Float();
				a.indirectLower = (V(a.indirectLower) + V(b.indirectLower)).Float();
				a.indirectUpper = (V(a.indirectUpper) + V(b.indirectUpper)).Float();
				a.comparable &= b.comparable;
				a.upperKnown &= b.upperKnown;
				a.issues |= b.issues;
				double area = r.transfer->Patches()[i].areaWorldSquared;
				primaryPower += area * (root.incident[i].x + root.incident[i].y + root.incident[i].z);
				reflectedPower += area * (b.indirect.x + b.indirect.y + b.indirect.z);
				power.primaryAreaWeighted +=
					area * (root.incident[i].x + root.incident[i].y + root.incident[i].z);
				power.firstBounceAreaWeighted += area * (b.indirect.x + b.indirect.y + b.indirect.z);
				power.available &= root.available[i] && b.comparable;
			}
			roots.push_back(power);
		};
		for (const auto &s : m.sources)
			accumulate(Primary(m, s, strict));
		for (const auto &root : r.boundaryRoots)
			accumulate(root);
		reflected = primaryPower > 0 ? reflectedPower / primaryPower : 0;
		if (p.contract.physicalTransportAssumed && r.transfer->Report().completePatchSet &&
			r.transfer->Report().allPairsRepresented && rhoMax < 1)
		{
			double bound = primaryPower * rhoMax * rhoMax / (1 - rhoMax);
			if (std::isfinite(bound) && bound <= std::numeric_limits<float>::max())
			{
				tail.known = true;
				tail.value = float(bound);
			}
		}
		for (size_t i = 0; i < total.size(); ++i)
		{
			auto &a = total[i];
			a.totalPreencoding = (V(a.direct) + V(a.sun) + V(a.sky) + V(a.ambient) + V(a.indirect)).Float();
			Need(Positive(a.totalPreencoding), Code::AlgorithmError,
				 "Nonfinite/negative global incident energy");
			a.totalPrediction = a.totalPreencoding;
			if (p.contract.operation == Indirect::ObservationOperator::NetRadiantEncoded)
			{
				uint8_t packed[3];
				Check(Bake::Encode(p.model.profile, a.totalPreencoding, a.totalPrediction, packed));
			}
			else if (p.contract.operation == Indirect::ObservationOperator::OutgoingLambertian)
			{
				Id id = r.transfer->Patches()[i].material;
				auto found = std::find_if(m.materials.begin(), m.materials.end(),
										  [&](const Indirect::Reflectance &x) { return x.material == id; });
				Need(found != m.materials.end(), Code::InvalidInput, "Missing receiver reflectance");
				V x;
				for (unsigned c = 0; c < 3; ++c)
					x[c] = Component(a.totalPreencoding, c) * Component(found->initial, c) / Pi;
				a.totalPrediction = x.Float();
			}
			else if (p.contract.operation != Indirect::ObservationOperator::IncidentLinear)
				a.comparable = false;
		}
		return total;
	}
	Bake::Prediction Auxiliary(const Model &m, const Observation &x)
	{
		Bake::Prediction value;
		const auto &p = r.profiles.at(m.profile);
		if (p.model.law != Shape::Law::HistoricalBake || p.contract.physicalTransportAssumed ||
			!r.boundaryRoots.empty())
			return value;
		// The current production grid reduction has no passive-GI extension. Never
		// relabel a surface bounce as grid ambient/directed or drop it silently.
		auto sources = std::make_shared<std::vector<Bake::SourceHypothesis>>();
		for (const auto &s : m.sources)
		{
			Bake::SourceHypothesis b = s.environment;
			if (s.candidate.root == TransportRootKind::LocalEmitter)
			{
				const auto &e = s.value;
				if (e.geometry.kind != EmitterKind::Point && e.geometry.kind != EmitterKind::Spot)
					return value;
				b.position = e.geometry.position;
				b.orientation = e.geometry.frame;
				b.angular = e.geometry.angular;
				b.geometry = e.geometry.geometry;
				b.kind =
					e.geometry.kind == EmitterKind::Point ? Bake::SourceKind::Point : Bake::SourceKind::Spot;
				b.spotTargetDistanceWorld = e.spotTargetDistanceWorld;
				b.spotRadiusWorld = e.spotRadiusWorld;
			}
			b.id = s.candidate.id;
			b.worldUnits = p.model.units;
			b.amplitudeKnown = true;
			b.compilerRgbAmplitude = s.value.rgbAmplitude;
			sources->push_back(b);
		}
		if (sources->empty())
		{
			value.available = x.channel == Channel::GridAmbient || x.channel == Channel::GridDirected;
			return value;
		}
		auto rows = std::make_shared<std::vector<Bake::Receiver>>(1, x.receiver);
		Bake::Options o;
		o.allowAssumed = p.model.allowAssumed;
		o.unpackedGridOnly = true;
		o.output = x.channel == Channel::DeluxeDirection ? Bake::Domain::DeluxeWorldDirection
														 : Bake::Domain::GridAmbientDirected;
		o.maxRays = std::max<uint64_t>(1, r.options.maxRays - work.rays);
		o.maxWork = std::max<uint64_t>(1, r.options.maxWork - work.traversal);
		Bake::Result result;
		Bake::CpuForwardEvaluator evaluator;
		auto error = evaluator.Evaluate(p.model.profile, Lease(sources), Lease(rows), {}, effectiveGeometry,
										o, read, result);
		if (!error && error.code == Code::Unavailable)
			return value;
		Check(error);
		++work.forwardCalls;
		work.pairs += result.work.pairs;
		Limit();
		if (result.predictions.count)
			value = result.predictions.data[0];
		value.gridAmbient = (V(value.gridAmbient) * p.fixedGain).Float();
		value.gridDirected = (V(value.gridDirected) * p.fixedGain).Float();
		return value;
	}
	Evaluation Evaluate(const Model &m, const std::vector<Observation> &observations, bool strict,
						bool priors = true)
	{
		Limit();
		Need(++work.evaluations <= r.options.maxEvaluations, Code::WorkLimit, "Joint evaluation budget");
		Evaluation out;
		linearProof =
			r.profiles.at(m.profile).model.law == Shape::Law::DiffuseIntegral &&
			(r.profiles.at(m.profile).contract.operation == Indirect::ObservationOperator::IncidentLinear ||
			 r.profiles.at(m.profile).contract.operation ==
				 Indirect::ObservationOperator::OutgoingLambertian) &&
			r.shadowWeight == 0;
		for (size_t c = 1; c < size_t(Channel::Count); ++c)
			if (r.options.channelWeight[c] > 0)
				linearProof = false;
		auto field = Surface(m, strict, out.reflected, out.tail, out.roots);
		out.linear = linearProof;
		using GroupKey = std::tuple<unsigned, Id, Id>;
		std::map<GroupKey, Group> groups;
		std::map<GroupKey, bool> required;
		GeometryKey admission;
		out.score.requested = observations.size();
		bool anyPositive = false;
		for (const auto &x : observations)
		{
			Limit();
			Prediction prediction;
			prediction.id = x.id;
			prediction.patch = x.patch;
			if (x.channel == Channel::Surface)
			{
				const auto &y = field.at(patchIndex.at(x.patch));
				prediction.available = y.comparable;
				prediction.rgb = y.totalPrediction;
				prediction.direct = y.direct;
				prediction.sun = y.sun;
				prediction.sky = y.sky;
				prediction.ambient = y.ambient;
				prediction.indirect = y.indirect;
				prediction.issues = y.issues;
			}
			else
			{
				auto y = Auxiliary(m, x);
				prediction.available = y.available;
				prediction.directionKnown = y.directionKnown;
				prediction.directionToSource = y.directionToSource;
				prediction.rgb = x.channel == Channel::GridAmbient ? y.gridAmbient : y.gridDirected;
				prediction.issues = y.visibilityIssues;
			}
			bool directional = x.channel == Channel::GridDirection || x.channel == Channel::DeluxeDirection;
			if (directional)
				prediction.available &= x.directionKnown && prediction.directionKnown;
			prediction.available &= x.comparable && r.options.channelWeight[size_t(x.channel)] > 0;
			prediction.available &= r.profiles.at(m.profile).contract.transferAttested &&
									r.profiles.at(m.profile).contract.primariesAttested;
			admission.U(x.id);
			admission.U(prediction.available);
			out.values.push_back(prediction);
			if (!x.comparable)
			{
				++out.score.incomparable;
				continue;
			}
			if (r.options.channelWeight[size_t(x.channel)] == 0)
			{
				++out.score.disabled;
				continue;
			}
			GroupKey key(unsigned(x.channel), x.region, x.block);
			auto &g = groups[key];
			g.channel = x.channel;
			g.region = x.region;
			g.block = x.block;
			++g.requested;
			required[key] |= x.required;
			if (!prediction.available)
			{
				++out.score.unavailable;
				continue;
			}
			++g.used;
			++out.score.used;
			double loss = 0, squared = 0, black = 0;
			unsigned n = 0;
			bool positive = false;
			if (directional)
			{
				double a =
					std::acos(Clamp(Dot(V(prediction.directionToSource), V(x.directionToSource)), -1., 1.));
				loss = Huber(a / x.angularSigmaRadians, r.options.huber);
				squared = a * a;
				black = Pi * Pi;
				positive = true;
				n = 1;
			}
			else
				for (unsigned c = 0; c < 3; ++c)
					if (x.rgb[c].present)
					{
						double e = Residual(Component(prediction.rgb, c), x.rgb[c]);
						Need(std::isfinite(e), Code::AlgorithmError, "Joint nonfinite discrepancy");
						Set(out.values.back().signedDiscrepancy, c, e);
						loss += Huber(e / x.sigma, r.options.huber);
						squared += e * e;
						double b = Residual(0, x.rgb[c]);
						black += b * b;
						positive |= x.rgb[c].lower > 0;
						++n;
					}
			if (n)
			{
				g.loss += x.measure * loss / n;
				g.squared += x.measure * squared / n;
				g.blackSquared += x.measure * black / n;
				g.measure += x.measure;
				if (positive)
					++g.positive;
				anyPositive |= positive;
			}
		}
		double channelLoss[size_t(Channel::Count)] = {}, worst = 0;
		uint64_t channelBlocks[size_t(Channel::Count)] = {};
		bool all = true;
		size_t surfaceGroups = 0;
		for (const auto &entry : groups)
		{
			auto g = entry.second;
			if (g.used != g.requested || g.measure <= 0)
			{
				all = false;
				out.score.groups.push_back(g);
				continue;
			}
			g.loss /= g.measure;
			g.squared /= g.measure;
			g.blackSquared /= g.measure;
			channelLoss[size_t(g.channel)] += g.loss;
			++channelBlocks[size_t(g.channel)];
			if (g.channel == Channel::Surface)
			{
				out.score.intervalMse += g.squared;
				out.score.blackMse += g.blackSquared;
				++surfaceGroups;
			}
			out.score.measure += g.measure;
			if (g.channel == Channel::Surface)
				out.score.worstBlockRmse = std::max(out.score.worstBlockRmse, std::sqrt(g.squared));
			worst = std::max(worst, g.loss);
			if (g.channel == Channel::Surface && required[entry.first] &&
				r.options.maximumRequiredBlockRmse > 0 &&
				std::sqrt(g.squared) > r.options.maximumRequiredBlockRmse)
				all = false;
			out.score.groups.push_back(g);
		}
		if (surfaceGroups)
		{
			out.score.intervalMse /= surfaceGroups;
			out.score.blackMse /= surfaceGroups;
		}
		double *terms[] = {&out.terms.surface, &out.terms.gridAmbient, &out.terms.gridDirected,
						   &out.terms.gridDirection, &out.terms.deluxe};
		for (size_t c = 0; c < size_t(Channel::Count); ++c)
			if (channelBlocks[c])
				*terms[c] = r.options.channelWeight[c] * channelLoss[c] / channelBlocks[c];
		out.terms.worstBlock = r.options.worstBlockWeight * worst;
		out.score.admissionKey = admission.Finish();
		out.score.available = !groups.empty() && out.score.unavailable == 0 && out.score.used > 0;
		out.score.positive = anyPositive;
		out.score.requiredBlocksPass = all && out.score.available;
		out.score.objective = out.terms.Total();
		if (priors)
		{
			for (const auto &s : m.sources)
			{
				const auto &c = s.candidate.shape;
				out.terms.complexity +=
					r.options.complexityPerSource + r.options.complexityPerParameter * Parameters(s);
				if (s.candidate.root == TransportRootKind::LocalEmitter && c.geometryPriorWeight > 0)
				{
					double prior = 0;
					V d = V(s.value.geometry.position.world) - V(c.initial.geometry.position.world);
					prior = Dot(d, d) / std::max(1e-12, c.positionSigmaWorld * c.positionSigmaWorld);
					if (s.value.geometry.kind != EmitterKind::Point)
					{
						double a = std::acos(
							Clamp(Dot(V(s.value.geometry.frame.forward), V(c.initial.geometry.frame.forward)),
								  -1., 1.));
						prior += a * a / std::max(1e-12, c.angleSigmaRadians * c.angleSigmaRadians);
					}
					const auto &g = s.value.geometry;
					const auto &original = c.initial.geometry;
					if (g.kind == EmitterKind::Rectangle || g.kind == EmitterKind::Tube)
					{
						double a = g.kind == EmitterKind::Rectangle ? g.geometry.halfWidthWorld
																	: g.geometry.halfLengthWorld;
						double b = g.kind == EmitterKind::Rectangle ? g.geometry.halfHeightWorld
																	: g.geometry.radiusWorld;
						double oa = g.kind == EmitterKind::Rectangle ? original.geometry.halfWidthWorld
																	 : original.geometry.halfLengthWorld;
						double ob = g.kind == EmitterKind::Rectangle ? original.geometry.halfHeightWorld
																	 : original.geometry.radiusWorld;
						prior += (std::pow(std::log(a / oa), 2) + std::pow(std::log(b / ob), 2)) /
								 std::max(1e-12, c.logSizeSigma * c.logSizeSigma);
					}
					out.terms.geometryPrior += c.geometryPriorWeight * prior;
				}
			}
			for (const auto &x : m.materials)
			{
				auto f =
					std::find_if(r.materials.begin(), r.materials.end(),
								 [&](const Indirect::Reflectance &a) { return a.material == x.material; });
				if (f != r.materials.end())
				{
					V d = V(x.initial) - V(f->initial);
					out.terms.materialPrior += f->priorWeight * Dot(d, d);
				}
			}
			if (r.shadowWeight)
			{
				std::vector<FitEvidence::Source> sources;
				for (const auto &s : m.sources)
					if (s.candidate.root == TransportRootKind::LocalEmitter)
					{
						FitEvidence::Source a;
						a.geometry = s.value.geometry;
						a.rgb = s.value.rgbAmplitude;
						a.legacySpotDistance = s.value.spotTargetDistanceWorld;
						a.legacySpotRadius = s.value.spotRadiusWorld;
						a.emittingSurface = s.value.emittingSurface;
						sources.push_back(a);
					}
				FitEvidence::Value v;
				Check(strict ? r.shadow->EvaluateReference(sources, read, v)
							 : r.shadow->Evaluate(sources, read, v));
				work.rays += v.rays;
				work.traversal += v.work;
				Limit();
				Need(v.available && std::isfinite(v.loss) && v.loss >= 0, Code::Unavailable,
					 "Joint shadow term unavailable");
				out.terms.shadow = r.shadowWeight * v.loss;
			}
		}
		Need(std::isfinite(out.terms.Total()) && out.terms.Total() >= 0, Code::AlgorithmError,
			 "Joint objective nonfinite");
		return out;
	}
	bool Better(const Evaluation &a, const Evaluation &b) const
	{
		return a.score.available && a.score.admissionKey == b.score.admissionKey &&
			   a.terms.Total() + r.options.objectiveTolerance < b.terms.Total();
	}
	bool LinearCoordinate(Model &m, Evaluation &best, size_t j, unsigned c, bool strict, double lo, double hi)
	{
		if (!best.linear)
			return false;
		Model probe = m;
		double current = Component(m.sources[j].value.rgbAmplitude, c);
		double q = current + (hi - current > current - lo ? 1 : -1) * std::max(1., (hi - lo) * .25);
		q = Clamp(q, lo, hi);
		if (q == current)
			return true;
		Set(probe.sources[j].value.rgbAmplitude, c, q);
		auto value = Evaluate(probe, training, strict);
		if (!value.linear || value.score.admissionKey != best.score.admissionKey)
			return false;
		++work.linearColumns;
		// One response column, bounded by the training span. All source/receiver
		// geometry has just passed Block::Linear over the complete admissible Q box.
		std::vector<double> column(training.size());
		for (size_t i = 0; i < column.size(); ++i)
			column[i] =
				(Component(value.values[i].rgb, c) - Component(best.values[i].rgb, c)) / (q - current);
		auto scalar = [&](double amplitude) {
			using K = std::tuple<unsigned, Id, Id>;
			std::map<K, std::pair<double, double>> sums;
			for (size_t i = 0; i < training.size(); ++i)
			{
				const auto &x = training[i];
				if (!best.values[i].available || !x.comparable ||
					r.options.channelWeight[size_t(x.channel)] == 0)
					continue;
				double loss = 0;
				unsigned channels = 0;
				for (unsigned channel = 0; channel < 3; ++channel)
					if (x.rgb[channel].present)
					{
						double v = Component(best.values[i].rgb, channel);
						if (channel == c)
							v += column[i] * (amplitude - current);
						loss += Huber(Residual(v, x.rgb[channel]) / x.sigma, r.options.huber);
						++channels;
					}
				if (channels)
				{
					auto &s = sums[K(unsigned(x.channel), x.region, x.block)];
					s.first += x.measure * loss / channels;
					s.second += x.measure;
				}
			}
			double total[size_t(Channel::Count)] = {}, maximum = 0;
			size_t count[size_t(Channel::Count)] = {};
			for (const auto &s : sums)
			{
				double v = s.second.first / s.second.second;
				auto channel = std::get<0>(s.first);
				total[channel] += v;
				++count[channel];
				maximum = std::max(maximum, v);
			}
			double objective = r.options.worstBlockWeight * maximum;
			for (size_t type = 0; type < size_t(Channel::Count); ++type)
				if (count[type])
					objective += r.options.channelWeight[type] * total[type] / count[type];
			Limit(training.size());
			return objective;
		};
		double chosen = current, minimum = scalar(current);
		for (double end : {lo, hi})
		{
			double v = scalar(end);
			if (v < minimum)
			{
				minimum = v;
				chosen = end;
			}
		}
		for (size_t step = 0; step < std::max<size_t>(32, r.options.amplitudeLineSteps) &&
							  hi - lo > r.options.parameterTolerance;
			 ++step)
		{
			double a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
			double va = scalar(a), vb = scalar(b);
			if (va < minimum)
			{
				minimum = va;
				chosen = a;
			}
			if (vb < minimum)
			{
				minimum = vb;
				chosen = b;
			}
			if (va <= vb)
				hi = b;
			else
				lo = a;
		}
		Model candidate = m;
		Set(candidate.sources[j].value.rgbAmplitude, c, chosen);
		auto exact = Evaluate(candidate, training, strict);
		if (Better(exact, best))
		{
			m = std::move(candidate);
			best = std::move(exact);
		}
		return true;
	}
	// Full nonlinear encoded/transport objective, not an amplitude-frozen matrix.
	// Golden/interval coordinate search plus endpoints crosses FAST active branches.
	void Amplitudes(Model &m, Evaluation &best, bool strict)
	{
		for (size_t sweep = 0; sweep < r.options.amplitudeSweeps; ++sweep)
		{
			double startValue = best.terms.Total();
			for (size_t j = 0; j < m.sources.size(); ++j)
				if (m.sources[j].candidate.fitAmplitude)
					for (unsigned c = 0; c < 3; ++c)
					{
						double lo = Component(m.sources[j].candidate.shape.lowerRgb, c),
							   hi = Component(m.sources[j].candidate.shape.upperRgb, c);
						if (hi <= lo)
							continue;
						if (LinearCoordinate(m, best, j, c, strict, lo, hi))
							continue;
						auto trial = [&](double q) {
							Model candidate = m;
							Set(candidate.sources[j].value.rgbAmplitude, c, q);
							auto e = Evaluate(candidate, training, strict);
							double objective = e.terms.Total();
							if (Better(e, best))
							{
								m = std::move(candidate);
								best = std::move(e);
							}
							return objective;
						};
						trial(lo);
						trial(hi);
						for (size_t step = 0;
							 step < r.options.amplitudeLineSteps && hi - lo > r.options.parameterTolerance;
							 ++step)
						{
							double a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
							double va = trial(a), vb = trial(b);
							if (va <= vb)
								hi = b;
							else
								lo = a;
						}
					}
			if (startValue - best.terms.Total() <= r.options.objectiveTolerance)
				break;
		}
	}
	void Materials(Model &m, Evaluation &best, bool strict)
	{
		if (!r.options.materials)
			return;
		for (size_t j = 0; j < m.materials.size(); ++j)
		{
			const auto &definition = r.materials.at(j);
			if (definition.knowledge == Indirect::MaterialKnowledge::Known)
				continue;
			for (unsigned c = 0; c < 3; ++c)
			{
				double lo = Component(definition.lower, c), hi = Component(definition.upper, c);
				auto trial = [&](double x) {
					Model n = m;
					Set(n.materials[j].initial, c, x);
					auto e = Evaluate(n, training, strict);
					double v = e.terms.Total();
					if (Better(e, best))
					{
						m = std::move(n);
						best = std::move(e);
					}
					return v;
				};
				trial(lo);
				trial(hi);
				for (size_t k = 0; k < r.options.amplitudeLineSteps; ++k)
				{
					double a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
					if (trial(a) <= trial(b))
						hi = b;
					else
						lo = a;
				}
			}
		}
	}
	void Geometry(Model &m, Evaluation &best, bool strict)
	{
		if (!r.options.geometry)
			return;
		for (size_t sweep = 0; sweep < r.options.geometrySweeps; ++sweep)
		{
			double fraction = std::pow(.5, double(sweep));
			for (size_t j = 0; j < m.sources.size(); ++j)
			{
				auto c = m.sources[j].candidate;
				auto trial = [&](Model n) {
					if (!Place(n))
						return;
					auto e = Evaluate(n, training, strict);
					if (!e.score.available || e.score.admissionKey != best.score.admissionKey)
						return;
					// Geometry moves get the same full amplitude refit and nuisance policy.
					Amplitudes(n, e, strict);
					if (Better(e, best))
					{
						m = std::move(n);
						best = std::move(e);
					}
				};
				if (c.root == TransportRootKind::LocalEmitter && c.shape.fitPosition)
					for (unsigned a = 0; a < 3; ++a)
						for (double sign : {-1., 1.})
						{
							Model n = m;
							Set(n.sources[j].value.geometry.position.world, a,
								Component(n.sources[j].value.geometry.position.world, a) +
									sign * r.options.positionStepWorld * fraction);
							trial(std::move(n));
						}
				if ((c.root == TransportRootKind::LocalEmitter && c.shape.fitFrame &&
					 m.sources[j].value.geometry.kind != EmitterKind::Point) ||
					(c.root == TransportRootKind::Sun && c.fitEnvironmentDirection))
					for (unsigned a = 0; a < 3; ++a)
						for (double sign : {-1., 1.})
						{
							Model n = m;
							double angle = sign * r.options.angularStepRadians * fraction;
							if (c.root == TransportRootKind::LocalEmitter)
								n.sources[j].value.geometry.frame =
									Rotation(n.sources[j].value.geometry.frame, a, angle);
							else
								n.sources[j].environment.orientation =
									Rotation(n.sources[j].environment.orientation, a, angle);
							trial(std::move(n));
						}
				if (c.root == TransportRootKind::LocalEmitter && c.shape.fitDimensions &&
					(m.sources[j].value.geometry.kind == EmitterKind::Rectangle ||
					 m.sources[j].value.geometry.kind == EmitterKind::Tube))
					for (unsigned a = 0; a < 2; ++a)
						for (double sign : {-1., 1.})
						{
							Model n = m;
							auto &p = n.sources[j].value.geometry;
							double scale = std::exp(sign * r.options.logDimensionStep * fraction);
							if (p.kind == EmitterKind::Rectangle)
							{
								if (a)
									p.geometry.halfHeightWorld *= scale;
								else
									p.geometry.halfWidthWorld *= scale;
							}
							else
							{
								if (a)
									p.geometry.radiusWorld *= scale;
								else
									p.geometry.halfLengthWorld *= scale;
							}
							trial(std::move(n));
						}
				if (c.root == TransportRootKind::LocalEmitter && c.shape.fitCone &&
					m.sources[j].value.geometry.kind == EmitterKind::Spot)
					for (double sign : {-1., 1.})
					{
						Model n = m;
						auto &e = n.sources[j].value;
						if (r.profiles[m.profile].model.law == Shape::Law::HistoricalBake)
							e.spotRadiusWorld *= std::exp(sign * r.options.logDimensionStep * fraction);
						else
						{
							double outer = std::acos(Clamp(e.geometry.angular.cosOuter, -1., 1.)) +
										   sign * r.options.angularStepRadians * fraction;
							if (outer < c.shape.minimumOuterRadians || outer > c.shape.maximumOuterRadians)
								continue;
							double inner = std::acos(Clamp(e.geometry.angular.cosInner, -1., 1.));
							inner = std::min(inner, outer * .95);
							e.geometry.angular.cosOuter = float(std::cos(outer));
							e.geometry.angular.cosInner = float(std::cos(inner));
						}
						trial(std::move(n));
					}
				if (c.root == TransportRootKind::LocalEmitter && c.shape.fitCone &&
					m.sources[j].value.geometry.kind == EmitterKind::Spot &&
					r.profiles[m.profile].model.law == Shape::Law::DiffuseIntegral)
					for (double sign : {-1., 1.})
					{
						Model n = m;
						auto &angular = n.sources[j].value.geometry.angular;
						double outer = std::acos(Clamp(angular.cosOuter, -1., 1.)),
							   inner = std::acos(Clamp(angular.cosInner, -1., 1.)) +
									   sign * r.options.angularStepRadians * fraction;
						if (inner < 0 || inner >= outer)
							continue;
						angular.cosInner = float(std::cos(inner));
						trial(std::move(n));
					}
			}
		}
	}
	void Diagnose(Model &m)
	{
		if (m.sources.empty() || m.sources.size() > r.options.diagnosticSources || !r.options.diagnosticRows)
			return;
		std::vector<Observation> subset;
		size_t count = std::min(r.options.diagnosticRows, training.size());
		for (size_t i = 0; i < count; ++i)
			subset.push_back(training[i * training.size() / count]);
		for (const auto &x : subset)
			if (x.channel != Channel::Surface)
				return;
		auto base = Evaluate(m, subset, true, false);
		if (!base.score.available)
			return;
		size_t parameters = m.sources.size() * 3;
		std::vector<std::vector<double>> columns;
		for (size_t source = 0; source < m.sources.size(); ++source)
			for (unsigned channel = 0; channel < 3; ++channel)
			{
				double q = Component(m.sources[source].value.rgbAmplitude, channel),
					   step = std::max(1e-3, std::abs(q) * 1e-3);
				double lo =
						   std::max(Component(m.sources[source].candidate.shape.lowerRgb, channel), q - step),
					   hi =
						   std::min(Component(m.sources[source].candidate.shape.upperRgb, channel), q + step);
				if (hi <= lo)
				{
					columns.push_back(std::vector<double>(subset.size() * 3));
					continue;
				}
				Model a = m, b = m;
				Set(a.sources[source].value.rgbAmplitude, channel, lo);
				Set(b.sources[source].value.rgbAmplitude, channel, hi);
				auto va = Evaluate(a, subset, true, false), vb = Evaluate(b, subset, true, false);
				if (!va.score.available || !vb.score.available ||
					va.score.admissionKey != base.score.admissionKey ||
					vb.score.admissionKey != base.score.admissionKey)
					return;
				std::vector<double> column;
				for (size_t i = 0; i < subset.size(); ++i)
					for (unsigned c = 0; c < 3; ++c)
						column.push_back((Component(vb.values[i].rgb, c) - Component(va.values[i].rgb, c)) /
										 (hi - lo) * std::sqrt(subset[i].measure) / subset[i].sigma);
				columns.push_back(std::move(column));
			}
		std::vector<std::vector<double>> gram(parameters, std::vector<double>(parameters));
		for (size_t i = 0; i < parameters; ++i)
			for (size_t j = 0; j <= i; ++j)
			{
				double sum = 0;
				for (size_t k = 0; k < columns[i].size(); ++k)
					sum += columns[i][k] * columns[j][k];
				gram[i][j] = gram[j][i] = sum;
				Limit(columns[i].size());
			}
		for (size_t iteration = 0; iteration < 80 * parameters * parameters; ++iteration)
		{
			size_t a = 0, b = 0;
			double largest = 0;
			for (size_t i = 0; i < parameters; ++i)
				for (size_t j = i + 1; j < parameters; ++j)
					if (std::abs(gram[i][j]) > largest)
					{
						largest = std::abs(gram[i][j]);
						a = i;
						b = j;
					}
			if (largest < 1e-14)
				break;
			double angle = .5 * std::atan2(2 * gram[a][b], gram[b][b] - gram[a][a]), c = std::cos(angle),
				   s = std::sin(angle);
			double aa = gram[a][a], bb = gram[b][b], ab = gram[a][b];
			for (size_t k = 0; k < parameters; ++k)
				if (k != a && k != b)
				{
					double x = gram[k][a], y = gram[k][b];
					gram[k][a] = gram[a][k] = c * x - s * y;
					gram[k][b] = gram[b][k] = s * x + c * y;
				}
			gram[a][a] = c * c * aa - 2 * s * c * ab + s * s * bb;
			gram[b][b] = s * s * aa + 2 * s * c * ab + c * c * bb;
			gram[a][b] = gram[b][a] = 0;
			Limit(parameters * parameters);
		}
		auto &spectrum = m.localAmplitudeSpectrum;
		spectrum = {};
		spectrum.available = true;
		spectrum.parameters = parameters;
		spectrum.scope = "Conditional local prediction RGB Jacobian at frozen geometry/material/profile; "
						 "source existence and XYZ/count unidentifiability remain separate";
		for (size_t i = 0; i < parameters; ++i)
			spectrum.singular.push_back(std::sqrt(std::max(0., gram[i][i])));
		std::sort(spectrum.singular.begin(), spectrum.singular.end(), std::greater<double>());
		double maximum = spectrum.singular.front();
		for (double singular : spectrum.singular)
			if (singular > maximum * spectrum.relativeThreshold && singular > 1e-12)
				++spectrum.rank;
		if (spectrum.rank == parameters)
		{
			spectrum.conditionKnown = true;
			spectrum.condition = maximum / spectrum.singular.back();
		}
	}
	Model Optimize(Model m)
	{
		// A structural child inherits geometry/amplitudes, not convergence/rank
		// claims from the parent model's different parameter space.
		m.stationary = false;
		m.localAmplitudeSpectrum = {};
		m.validation = {};
		m.assumptions.clear();
		struct RestoreTraining
		{
			std::vector<Observation> &target;
			std::vector<Observation> full;
			~RestoreTraining() { target = std::move(full); }
		} restore{training, training};
		if (r.options.coarseRows && r.options.coarseRows < training.size())
		{
			std::map<std::tuple<unsigned, Id, Id>, std::vector<Observation>> groups;
			for (const auto &x : training)
				groups[std::make_tuple(unsigned(x.channel), x.region, x.block)].push_back(x);
			Need(r.options.coarseRows >= groups.size(), Code::InvalidInput,
				 "Coarse observation budget must retain every evidence block");
			training.clear();
			for (size_t wave = 0; training.size() < r.options.coarseRows; ++wave)
				for (const auto &group : groups)
					if (wave < group.second.size() && training.size() < r.options.coarseRows)
						training.push_back(group.second[wave]);
			std::sort(training.begin(), training.end(),
					  [](const Observation &a, const Observation &b) { return a.id < b.id; });
		}
		Need(Place(m), Code::MissingGeometry, "Physically invalid source support/path/bounds");
		Evaluation best = Evaluate(m, training, false);
		Need(best.score.available, Code::Unavailable, "Joint comparable training coverage missing");
		for (size_t sweep = 0; sweep < r.options.jointSweeps; ++sweep)
		{
			double before = best.terms.Total();
			Amplitudes(m, best, false);
			Materials(m, best, false);
			Geometry(m, best, false);
			if (before - best.terms.Total() <= r.options.objectiveTolerance)
			{
				m.stationary = true;
				break;
			}
		}
		// A surrogate/coarse improvement never substitutes for final reference values.
		training = restore.full;
		best = Evaluate(m, training, true);
		Need(best.score.available, Code::Unavailable, "Strict final coverage unavailable");
		double strictStart = best.terms.Total();
		Amplitudes(m, best, true);
		Materials(m, best, true);
		Geometry(m, best, true);
		best = Evaluate(m, training, true);
		if (strictStart - best.terms.Total() > r.options.objectiveTolerance)
			m.stationary = false;
		m.training = best.terms;
		m.linearAmplitudeBlock = best.linear;
		m.strictReevaluated = true;
		m.trainingKey = identity;
		m.geometryKey = r.geometry.geometryKey;
		m.policyKey = Configuration();
		m.transferKey = r.transfer->Key();
		m.reflectedPowerFraction = best.reflected;
		m.roots = best.roots;
		m.omittedBouncePower = best.tail;
		m.key = ModelKey(m);
		Diagnose(m);
		m.absoluteScaleKnown =
			r.profiles[m.profile].contract.incidentDomain == Indirect::Root::Domain::PhysicalLux &&
			r.profiles[m.profile].contract.calibration.kind == CalibrationKind::PhysicallyAuthored;
		m.assumptions.push_back("Conditional model, not certified source existence; fixed calibration gauge; "
								"one passive bounce only");
		for (const auto &x : m.materials)
			if (x.knowledge != Indirect::MaterialKnowledge::Known)
				m.assumptions.push_back("Reflectance/source allocation is conditional and correlated");
		return m;
	}
};
Id DerivedId(const std::string &kind, Id a, Id b, unsigned index = 0)
{
	GeometryKey k;
	Text(k, kind);
	k.U(std::min(a, b));
	k.U(std::max(a, b));
	k.U(index);
	auto d = k.Finish();
	Id id = uint64_t(1) << 63;
	for (unsigned i = 0; i < 8; ++i)
		id |= Id(d[i]) << (8 * i);
	return id;
}
void Order(Model &m)
{
	std::sort(m.sources.begin(), m.sources.end(),
			  [](const Source &a, const Source &b) { return a.candidate.id < b.candidate.id; });
}
bool Contains(const Model &m, const Candidate &c)
{
	for (const auto &s : m.sources)
		if (s.candidate.id == c.id || (c.family && s.candidate.family == c.family))
			return true;
	return false;
}
std::vector<Model> Proposals(const Request &r, const Model &parent, const std::vector<Candidate> &catalog,
							 uint64_t &deferred)
{
	std::vector<Model> out;
	auto push = [&](Model m) {
		if (out.size() < r.options.maxModels)
			out.push_back(std::move(m));
		else
			++deferred;
	};
	std::map<std::tuple<Id, unsigned, unsigned>, std::vector<const Candidate *>> strata;
	for (const auto &c : catalog)
		strata[std::make_tuple(c.region, unsigned(c.root), unsigned(c.shape.initial.geometry.kind))]
			.push_back(&c);
	std::vector<const Candidate *> balanced;
	for (size_t wave = 0; balanced.size() < catalog.size(); ++wave)
		for (const auto &s : strata)
			if (wave < s.second.size())
				balanced.push_back(s.second[wave]);
	for (const auto pointer : balanced)
	{
		const auto &c = *pointer;
		if (!Contains(parent, c) && parent.sources.size() < r.options.maxSources)
		{
			Model m = parent;
			m.sources.push_back(FromCandidate(c));
			m.proposal = "activate:" + std::to_string(c.id);
			Order(m);
			push(std::move(m));
		}
		if (c.family)
			for (size_t i = 0; i < parent.sources.size(); ++i)
				if (parent.sources[i].candidate.family == c.family && parent.sources[i].candidate.id != c.id)
				{
					Model m = parent;
					m.sources[i] = FromCandidate(c);
					m.proposal = "type-alternative:" + std::to_string(c.id);
					Order(m);
					push(std::move(m));
				}
	}
	for (size_t i = 0; i < parent.sources.size(); ++i)
		if (parent.sources[i].candidate.mayRemove)
		{
			Model m = parent;
			m.proposal = "remove:" + std::to_string(m.sources[i].candidate.id);
			m.sources.erase(m.sources.begin() + i);
			push(std::move(m));
		}
	if (r.options.mergeSplit)
	{
		for (size_t i = 0; i < parent.sources.size(); ++i)
			for (size_t j = i + 1; j < parent.sources.size(); ++j)
			{
				const auto &a = parent.sources[i];
				const auto &b = parent.sources[j];
				if (a.candidate.root != TransportRootKind::LocalEmitter ||
					b.candidate.root != TransportRootKind::LocalEmitter ||
					a.value.geometry.kind != EmitterKind::Point ||
					b.value.geometry.kind != EmitterKind::Point || !a.candidate.mayRemove ||
					!b.candidate.mayRemove)
					continue;
				if (Length(V(a.value.geometry.position.world) - V(b.value.geometry.position.world)) >
					2 * r.options.positionStepWorld)
					continue;
				Model m = parent;
				Source merged = a;
				merged.candidate.id = DerivedId("merge", a.candidate.id, b.candidate.id);
				merged.candidate.family = 0;
				merged.candidate.parents = {a.candidate.id, b.candidate.id};
				merged.candidate.provider = "joint-merge";
				merged.candidate.provenance =
					"Alternative midpoint of two candidate hypotheses; not fixture deduplication";
				merged.value.geometry.position.world =
					((V(a.value.geometry.position.world) + V(b.value.geometry.position.world)) * .5).Float();
				merged.value.geometry.source.id = merged.candidate.id;
				merged.value.rgbAmplitude = (V(a.value.rgbAmplitude) + V(b.value.rgbAmplitude)).Float();
				for (unsigned c = 0; c < 3; ++c)
					Set(merged.value.rgbAmplitude, c,
						std::min(Component(merged.value.rgbAmplitude, c),
								 Component(merged.candidate.shape.upperRgb, c)));
				m.sources.erase(m.sources.begin() + j);
				m.sources[i] = merged;
				m.proposal = "merge:" + std::to_string(a.candidate.id) + ":" + std::to_string(b.candidate.id);
				Order(m);
				push(std::move(m));
			}
		for (size_t i = 0; i < parent.sources.size() && parent.sources.size() < r.options.maxSources; ++i)
		{
			const auto &s = parent.sources[i];
			if (s.candidate.root != TransportRootKind::LocalEmitter ||
				s.value.geometry.kind != EmitterKind::Point || !s.candidate.mayRemove)
				continue;
			for (unsigned axis = 0; axis < 3; ++axis)
			{
				Model m = parent;
				Source a = s, b = s;
				a.candidate.id = DerivedId("split-left", s.candidate.id, 0, axis);
				b.candidate.id = DerivedId("split-right", s.candidate.id, 0, axis);
				a.candidate.family = b.candidate.family = 0;
				a.candidate.parents = b.candidate.parents = {s.candidate.id};
				a.candidate.provider = b.candidate.provider = "joint-split";
				a.candidate.provenance = b.candidate.provenance =
					"Paired displaced alternative; source count remains unverified";
				a.value.geometry.source.id = a.candidate.id;
				b.value.geometry.source.id = b.candidate.id;
				Set(a.value.geometry.position.world, axis,
					Component(a.value.geometry.position.world, axis) - .5 * r.options.positionStepWorld);
				Set(b.value.geometry.position.world, axis,
					Component(b.value.geometry.position.world, axis) + .5 * r.options.positionStepWorld);
				a.value.rgbAmplitude = b.value.rgbAmplitude = (V(s.value.rgbAmplitude) * .5).Float();
				m.sources[i] = a;
				m.sources.push_back(b);
				m.proposal = "split:" + std::to_string(s.candidate.id) + ":" + std::to_string(axis);
				Order(m);
				push(std::move(m));
			}
		}
	}
	// Bound scratch proposals before fitting. A full queue is explicitly handled
	// as partial search by Solve, rather than an unbounded combinatorial vector.

	return out;
}
double Ranking(const Model &m)
{
	if (!m.validation.available)
		return std::numeric_limits<double>::max();
	return m.validation.objective + m.training.complexity + m.training.geometryPrior +
		   m.training.materialPrior;
}
bool RankLess(const Model &a, const Model &b)
{
	if (a.validation.requiredBlocksPass != b.validation.requiredBlocksPass)
		return a.validation.requiredBlocksPass;
	if (Ranking(a) != Ranking(b))
		return Ranking(a) < Ranking(b);
	return a.key < b.key;
}
void ResidualCandidates(Engine &e, const Model &model, std::vector<Candidate> &catalog)
{
	const auto &r = e.r;
	if (!r.options.residualSearch || !r.sourceSearchBounds.known || !r.options.residualCandidates ||
		catalog.size() >= r.options.maxCandidates)
		return;
	Need(r.options.residualGridDivisions >= 2 && r.options.residualGridDivisions <= 5, Code::InvalidInput,
		 "Residual grid bounded at 125 nodes");
	auto base = e.Evaluate(model, e.training, true);
	if (!base.score.available)
		return;
	double deficit = 0;
	for (size_t i = 0; i < e.training.size(); ++i)
		if (e.training[i].channel == Channel::Surface && base.values[i].available)
			for (unsigned c = 0; c < 3; ++c)
				if (e.training[i].rgb[c].present)
					deficit += std::max(0., e.training[i].rgb[c].lower - Component(base.values[i].rgb, c));
	if (deficit <= r.options.parameterTolerance)
		return;
	struct Trial
	{
		Candidate candidate;
		double score;
	};
	std::vector<Trial> proposed;
	unsigned n = r.options.residualGridDivisions;
	for (unsigned a = 0; a < n; ++a)
		for (unsigned b = 0; b < n; ++b)
			for (unsigned c = 0; c < n; ++c)
			{
				Candidate candidate;
				candidate.id = DerivedId("residual-grid", 0, 0, (a * n + b) * n + c);
				candidate.provider = "joint-training-residual-grid";
				candidate.provenance = "Predeclared geometry search lattice, ranked only against training "
									   "residual; not emitter evidence";
				candidate.shape.centreBounds = r.sourceSearchBounds;
				candidate.shape.initial.geometry.kind = EmitterKind::Point;
				candidate.shape.initial.geometry.source.id = candidate.id;
				candidate.shape.initial.geometry.position.known = true;
				candidate.shape.initial.geometry.angular.kind = AngularKind::Isotropic;
				V low = r.sourceSearchBounds.minWorld, high = r.sourceSearchBounds.maxWorld;
				candidate.shape.initial.geometry.position.world =
					V(low.x + (high.x - low.x) * a / (n - 1), low.y + (high.y - low.y) * b / (n - 1),
					  low.z + (high.z - low.z) * c / (n - 1))
						.Float();
				candidate.shape.maximumTranslationWorld = Length(high - low);
				candidate.shape.fitFrame = candidate.shape.fitDimensions = candidate.shape.fitCone = false;
				candidate.shape.maximumRotationRadians = 0;
				candidate.shape.upperRgb = Vec3(1200, 1200, 1200);
				candidate.shape.initial.rgbAmplitude = Vec3(16, 16, 16);
				// Bounds/seed for new hypotheses are explicit policy, not recovered intensity.
				if (!catalog.empty())
				{
					candidate.shape.upperRgb = catalog.front().shape.upperRgb;
					candidate.shape.initial.rgbAmplitude = (V(candidate.shape.upperRgb) * .1).Float();
				}
				if (Contains(model, candidate))
					continue;
				Model m = model;
				m.sources.push_back(FromCandidate(candidate));
				Order(m);
				if (!e.Place(m))
					continue;
				double score = std::numeric_limits<double>::max();
				for (double amplitude : {.02, .1, .4})
				{
					// Source sorting can put the derived ID anywhere; update by identity.
					for (auto &s : m.sources)
						if (s.candidate.id == candidate.id)
							s.value.rgbAmplitude = (V(candidate.shape.upperRgb) * amplitude).Float();
					auto v = e.Evaluate(m, e.training, false);
					if (v.score.available && v.score.admissionKey == base.score.admissionKey)
						score = std::min(score, v.terms.Total());
				}
				if (std::isfinite(score) && score < base.terms.Total())
					proposed.push_back({candidate, score});
			}
	std::sort(proposed.begin(), proposed.end(), [](const Trial &a, const Trial &b) {
		return a.score != b.score ? a.score < b.score : a.candidate.id < b.candidate.id;
	});
	for (size_t i = 0; i < std::min(r.options.residualCandidates, proposed.size()) &&
					   catalog.size() < r.options.maxCandidates;
		 ++i)
		if (std::none_of(catalog.begin(), catalog.end(),
						 [&](const Candidate &c) { return c.id == proposed[i].candidate.id; }))
			catalog.push_back(proposed[i].candidate);
	std::sort(catalog.begin(), catalog.end(),
			  [](const Candidate &a, const Candidate &b) { return a.id < b.id; });
}
State FailureState(Code c)
{
	return c == Code::Cancelled
			   ? State::Cancelled
			   : c == Code::WorkLimit || c == Code::MemoryLimit
					 ? State::Budget
					 : c == Code::AlgorithmError
						   ? State::NumericalFailure
						   : c == Code::MissingGeometry ? State::PhysicallyInvalid : State::Unavailable;
}
} // namespace

Error Solve(const Request &r, const ReadContext &read, Result &out, const Validation *validation,
			const Checkpoint *resume, size_t roundsThisCall)
{
	out = {};
	std::unique_ptr<Engine> engine;
	Checkpoint checkpoint;
	try
	{
		engine.reset(new Engine(r, read));
		auto &e = *engine;
		e.Pin();
		if (validation)
		{
			GeometryKey k;
			k.Digest(e.identity);
			k.Digest(validation->Key());
			e.identity = k.Finish();
		}
		Need(!Zero(r.trainingKey) && r.candidates.size() <= r.options.maxCandidates, Code::InvalidInput,
			 "Joint pinned training key / candidate budget");
		std::set<Id> ids;
		for (const auto &c : r.candidates)
		{
			Need(c.id && ids.insert(c.id).second && !c.provenance.empty() && c.provenance.size() <= 4096 &&
					 c.provider.size() <= 4096,
				 Code::InvalidInput, "Joint candidate identity/provenance");
			Need(c.root == TransportRootKind::LocalEmitter || c.root == TransportRootKind::Sun ||
					 c.root == TransportRootKind::Sky,
				 Code::InvalidInput,
				 "Finite/sun/sky candidates only; ambient remains an explicit boundary root");
			Need(Positive(c.shape.lowerRgb) && Positive(c.shape.upperRgb) &&
					 Positive(c.shape.initial.rgbAmplitude),
				 Code::InvalidInput, "Finite nonnegative candidate amplitude bounds");
			for (unsigned channel = 0; channel < 3; ++channel)
				Need(Component(c.shape.lowerRgb, channel) <=
							 Component(c.shape.initial.rgbAmplitude, channel) &&
						 Component(c.shape.initial.rgbAmplitude, channel) <=
							 Component(c.shape.upperRgb, channel),
					 Code::InvalidInput, "Candidate amplitude outside bounds");
			Need(std::isfinite(c.clearanceWorld) && c.clearanceWorld > 0 &&
					 std::isfinite(c.shape.geometryPriorWeight) && c.shape.geometryPriorWeight >= 0 &&
					 std::isfinite(c.shape.maximumTranslationWorld) && c.shape.maximumTranslationWorld >= 0 &&
					 std::isfinite(c.shape.maximumRotationRadians) && c.shape.maximumRotationRadians >= 0,
				 Code::InvalidInput, "Source clearance/fixture prior/pose bounds");
			if (c.root == TransportRootKind::Sun)
				Need(c.environment.kind == Bake::SourceKind::Directional, Code::InvalidInput,
					 "Sun candidate requires directional historical operator");
			if (c.root == TransportRootKind::Sky)
				Need(c.environment.kind == Bake::SourceKind::Sky, Code::InvalidInput,
					 "Sky candidate requires explicit environment operator");
		}
		Need(!r.materials.empty() && r.materials.size() <= 256, Code::InvalidInput,
			 "Joint explicit bounded material definitions");
		std::set<Id> materialIdentities;
		for (const auto &x : r.materials)
		{
			Need(materialIdentities.insert(x.material).second && std::isfinite(x.priorWeight) &&
					 x.priorWeight >= 0,
				 Code::InvalidInput, "Duplicate material / invalid material prior");
			Need(x.material && Positive(x.lower) && Positive(x.upper) && Positive(x.initial) &&
					 Peak(x.upper) <= 1,
				 Code::InvalidInput, "Passive material bounds");
			for (unsigned c = 0; c < 3; ++c)
				Need(Component(x.lower, c) <= Component(x.initial, c) &&
						 Component(x.initial, c) <= Component(x.upper, c),
					 Code::InvalidInput, "Reflectance outside bounds");
			if (x.knowledge == Indirect::MaterialKnowledge::Known)
				Need(Length(V(x.lower) - V(x.upper)) == 0 && Length(V(x.initial) - V(x.lower)) == 0,
					 Code::InvalidInput, "Known material must have equal fixed bounds");
		}
		for (const auto &patch : r.transfer->Patches())
			Need(materialIdentities.count(patch.material), Code::InvalidInput,
				 "Missing patch material definition");
		if (resume)
		{
			Need(resume->version == kVersion && resume->inputKey == e.identity, Code::InvalidState,
				 "Stale joint checkpoint");
			Need(resume->completed.size() <= r.options.maxModels &&
					 resume->catalog.size() <= r.options.maxCandidates &&
					 resume->beam.size() <= r.options.beamWidth && resume->round <= r.options.rounds,
				 Code::InvalidState, "Checkpoint search bounds");
			checkpoint = *resume;
			e.work = checkpoint.work;
			e.cache = checkpoint.geometryCache;
			e.cacheSize = 0;
			for (const auto &entry : e.cache)
			{
				Need(entry.second != nullptr, Code::InvalidState, "Invalid checkpoint geometry lease");
				e.cacheSize += entry.second->Bytes();
			}
			Need(e.cacheSize == checkpoint.cacheBytes && e.cacheSize <= r.options.cacheBytes,
				 Code::InvalidState, "Checkpoint geometry cache budget");
		}
		else
		{
			checkpoint.inputKey = e.identity;
			checkpoint.catalog = r.candidates;
			std::sort(checkpoint.catalog.begin(), checkpoint.catalog.end(),
					  [](const Candidate &a, const Candidate &b) { return a.id < b.id; });
		}
		std::set<Disk::Digest> seen;
		for (const auto &a : checkpoint.attempts)
			if (a.state != State::Budget && a.state != State::Cancelled)
				seen.insert(a.key);
		Disk::Digest validationPartition{};
		if (resume && !checkpoint.completed.empty())
			validationPartition = checkpoint.completed.front().validation.admissionKey;
		if (validation)
			Need(!Zero(validation->Key()), Code::InvalidInput,
				 "Validation oracle must identify its private partition");
		auto run = [&](Model candidate) {
			Order(candidate);
			auto structure = ModelKey(candidate, false);
			if (seen.count(structure))
				return;
			Need(e.work.attemptedModels < r.options.maxModels, Code::WorkLimit,
				 "Bounded structural model search exhausted");
			uint64_t cap = read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes) : r.options.maxBytes;
			uint64_t metadata = (checkpoint.completed.size() + r.options.maxModels + 8) *
								(r.options.maxSources * (sizeof(Source) + 2048) + 16384) * 3;
			Need(e.baseBytes + metadata < cap, Code::MemoryLimit,
				 "Joint model/beam/proposal metadata budget");
			e.work.accountedBytes = std::max(e.work.accountedBytes, e.baseBytes + metadata);
			Attempt attempt;
			attempt.key = structure;
			attempt.proposal = candidate.proposal;
			attempt.profile = candidate.profile;
			attempt.sources = candidate.sources.size();
			++e.work.attemptedModels;
			Model best;
			bool have = false;
			try
			{
				for (size_t restart = 0; restart < r.options.restarts; ++restart)
				{
					Model start = candidate;
					if (restart)
					{
						uint64_t random = r.options.seed + restart * 0x9e3779b97f4a7c15ull;
						for (auto &s : start.sources)
							if (s.candidate.fitAmplitude)
								for (unsigned c = 0; c < 3; ++c)
								{
									random ^= random >> 12;
									random ^= random << 25;
									random ^= random >> 27;
									double t =
										double((random * 2685821657736338717ull) >> 11) / 9007199254740992.;
									Set(s.value.rgbAmplitude, c,
										Component(s.candidate.shape.lowerRgb, c) +
											t * (Component(s.candidate.shape.upperRgb, c) -
												 Component(s.candidate.shape.lowerRgb, c)));
								}
					}
					auto fitted = e.Optimize(std::move(start));
					if (!have || fitted.training.Total() < best.training.Total())
					{
						best = std::move(fitted);
						have = true;
					}
				}
				if (validation)
				{
					Check(validation->Evaluate(best, read, best.validation));
					const auto &vw = best.validation.work;
					e.work.evaluations += vw.evaluations;
					e.work.forwardCalls += vw.forwardCalls;
					e.work.preparationCalls += vw.preparationCalls;
					e.work.cacheHits += vw.cacheHits;
					e.work.rays += vw.rays;
					e.work.traversal += vw.traversal;
					e.work.pairs += vw.pairs;
					e.work.links += vw.links;
					e.work.arithmetic += vw.arithmetic;
					e.work.linearColumns += vw.linearColumns;
					e.Limit();
					Need(best.validation.partitionKey == validation->Key(), Code::InvalidState,
						 "Validation partition identity changed");
					if (Zero(validationPartition))
						validationPartition = best.validation.admissionKey;
					Need(best.validation.available && !Zero(best.validation.admissionKey) &&
							 best.validation.admissionKey == validationPartition,
						 Code::Unavailable, "Model cannot win by dropping validation coverage");
					Need(std::isfinite(best.validation.objective) && best.validation.objective >= 0,
						 Code::AlgorithmError, "Invalid validation score");
				}
				attempt.state = best.stationary ? State::Converged : State::Partial;
				attempt.training = best.training.Total();
				attempt.validation = best.validation.objective;
				checkpoint.completed.push_back(std::move(best));
			}
			catch (const Error &error)
			{
				if (error.code == Code::Cancelled || error.code == Code::WorkLimit ||
					error.code == Code::MemoryLimit)
				{
					attempt.state = FailureState(error.code);
					attempt.reason = error.message;
					checkpoint.attempts.push_back(attempt);
					throw;
				}
				attempt.state = FailureState(error.code);
				attempt.reason = error.message;
				++e.work.rejectedModels;
			}
			seen.insert(structure);
			checkpoint.attempts.push_back(std::move(attempt));
		};
		if (checkpoint.completed.empty() && checkpoint.round == 0)
		{
			for (size_t profile = 0; profile < r.profiles.size(); ++profile)
			{
				size_t before = checkpoint.completed.size();
				Model m;
				m.profile = profile;
				m.materials = r.materials;
				m.proposal = "no-source/boundary-only";
				run(m);
				for (const auto &c : checkpoint.catalog)
					if (c.initialActive)
						m.sources.push_back(FromCandidate(c));
				m.proposal = "initial-active";
				run(m);
				// Directional operators have no defined axis for a black model.
				// An unavailable empty baseline must not prevent supported positive
				// candidates from ever entering the beam.
				if (checkpoint.completed.size() == before)
				{
					m.sources.clear();
					auto seeds = Proposals(r, m, checkpoint.catalog, e.work.proposalsDeferred);
					for (auto &seed : seeds)
					{
						run(std::move(seed));
						if (checkpoint.completed.size() > before)
							break;
					}
				}
			}
		}
		auto beam = [&]() {
			auto models = checkpoint.completed;
			if (validation)
				std::sort(models.begin(), models.end(), RankLess);
			else
				std::sort(models.begin(), models.end(), [](const Model &a, const Model &b) {
					return a.training.Total() != b.training.Total() ? a.training.Total() < b.training.Total()
																	: a.key < b.key;
				});
			if (models.size() > r.options.beamWidth)
				models.resize(r.options.beamWidth);
			return models;
		};
		checkpoint.beam = beam();
		size_t until = roundsThisCall > r.options.rounds
						   ? r.options.rounds
						   : std::min(r.options.rounds, checkpoint.round + roundsThisCall);
		for (; checkpoint.round < until; ++checkpoint.round)
		{
			auto parents = checkpoint.beam;
			if (parents.empty())
				break;
			ResidualCandidates(e, parents.front(), checkpoint.catalog);
			for (const auto &parent : parents)
			{
				auto proposals = Proposals(r, parent, checkpoint.catalog, e.work.proposalsDeferred);
				for (auto &m : proposals)
					run(std::move(m));
			}
			checkpoint.beam = beam();
		}
		checkpoint.work = e.work;
		checkpoint.geometryCache = e.cache;
		checkpoint.cacheBytes = e.cacheSize;
		out.checkpoint = checkpoint;
		out.work = e.work;
		out.attempts = checkpoint.attempts;
		if (checkpoint.completed.empty())
		{
			out.state = State::Unavailable;
			out.reason = "No supported, physically feasible complete-coverage model";
			return {};
		}
		if (!validation)
		{
			out.alternatives = checkpoint.beam;
			out.state = State::Unvalidated;
			out.reason = "No independent validation controller; no physical acceptance";
			return {};
		}
		auto all = checkpoint.completed;
		std::sort(all.begin(), all.end(), RankLess);
		double best = all.front().validation.objective;
		bool positive = all.front().validation.positive &&
						all.front().validation.blackMse - all.front().validation.intervalMse >
							r.options.minimumValidationImprovement;
		for (const auto &m : all)
			if (m.validation.objective <= best + r.options.equivalentValidationSlack &&
				m.validation.requiredBlocksPass)
			{
				if (std::none_of(out.alternatives.begin(), out.alternatives.end(),
								 [&](const Model &x) { return x.key == m.key; }))
					out.alternatives.push_back(m);
				if (out.alternatives.size() >= r.options.maxRetained)
					break;
			}
		if (!positive)
		{
			out.state = State::InsufficientSignal;
			out.reason = "No positive independent light evidence";
		}
		else if (out.alternatives.empty())
		{
			out.state = State::Unavailable;
			out.reason = "No model passes required independent spatial blocks";
		}
		else
		{
			out.preferred = 0;
			out.state = out.alternatives.size() > 1
							? State::Unidentifiable
							: checkpoint.round < r.options.rounds || e.work.proposalsDeferred ||
									  !out.alternatives.front().stationary
								  ? State::Partial
								  : State::Converged;
			out.reason = "Preferred conditional strict-forward model; source existence is not certified by "
						 "residual or sparsity";
		}
		return {};
	}
	catch (const Error &error)
	{
		out.state = FailureState(error.code);
		out.reason = error.message;
		if (engine)
		{
			checkpoint.work = engine->work;
			checkpoint.geometryCache = engine->cache;
			checkpoint.cacheBytes = engine->cacheSize;
			out.work = engine->work;
		}
		out.checkpoint = checkpoint;
		out.attempts = checkpoint.attempts;
		return error;
	}
	catch (const std::bad_alloc &)
	{
		out.state = State::Budget;
		out.reason = "Joint allocation limit";
		return Failure(Code::MemoryLimit, out.reason);
	}
}
// These entry points accept caller-owned parameter proposals, never observed
// validation/held-out RGB. They seal values against the original training policy.
Error Refit(const Request &r, const Model &seed, const ReadContext &read, Model &out, Work &work)
{
	work = {};
	std::unique_ptr<Engine> engine;
	try
	{
		Model input = seed;
		out = {}; // input/output aliasing is supported
		Need(r.transfer && input.profile < r.profiles.size() && input.geometryKey == r.geometry.geometryKey &&
				 input.transferKey == r.transfer->Key(),
			 Code::InvalidInput, "Nuisance refit seed generation/profile mismatch");
		input.physicalExistenceCertified = false;
		input.sourceAllocationIdentifiable = false;
		for (const auto &m : input.materials)
		{
			auto original =
				std::find_if(r.materials.begin(), r.materials.end(),
							 [&](const Indirect::Reflectance &a) { return a.material == m.material; });
			Need(original != r.materials.end(), Code::InvalidInput, "Refit material is not in request");
			for (unsigned c = 0; c < 3; ++c)
				Need(Component(m.lower, c) >= Component(original->lower, c) &&
						 Component(m.upper, c) <= Component(original->upper, c) &&
						 Component(m.lower, c) <= Component(m.initial, c) &&
						 Component(m.initial, c) <= Component(m.upper, c),
					 Code::InvalidInput, "Refit material outside declared nuisance bounds");
		}
		engine.reset(new Engine(r, read));
		engine->Pin();
		out = engine->Optimize(input);
		work = engine->work;
		return {};
	}
	catch (const Error &e)
	{
		out = {};
		if (engine)
			work = engine->work;
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Nuisance refit allocation");
	}
}
Error AssessHypothesis(const Request &r, const Model &seed, const ReadContext &read, Model &out, Score &score,
					   Work &work, std::vector<Prediction> *predictions)
{
	score = {};
	work = {};
	if (predictions)
		predictions->clear();
	std::unique_ptr<Engine> engine;
	try
	{
		Model input = seed;
		out = {};
		Need(r.transfer && input.profile < r.profiles.size() && input.geometryKey == r.geometry.geometryKey &&
				 input.transferKey == r.transfer->Key(),
			 Code::InvalidInput, "Hypothesis generation/profile mismatch");
		engine.reset(new Engine(r, read));
		auto &e = *engine;
		e.Pin();
		Need(e.Place(input), Code::MissingGeometry, "Hypothesis source geometry not certified");
		for (const auto &m : input.materials)
		{
			auto original =
				std::find_if(r.materials.begin(), r.materials.end(),
							 [&](const Indirect::Reflectance &a) { return a.material == m.material; });
			Need(original != r.materials.end() && Positive(m.initial) && Peak(m.initial) <= 1,
				 Code::InvalidInput, "Hypothesis material outside passive model");
			for (unsigned c = 0; c < 3; ++c)
				Need(Component(m.initial, c) >= Component(original->lower, c) &&
						 Component(m.initial, c) <= Component(original->upper, c),
					 Code::InvalidInput, "Hypothesis material bounds");
		}
		auto value = e.Evaluate(input, e.training, true);
		out = std::move(input);
		out.training = value.terms;
		out.trainingKey = e.identity;
		out.geometryKey = r.geometry.geometryKey;
		out.transferKey = r.transfer->Key();
		out.policyKey = e.Configuration();
		out.strictReevaluated = true;
		out.stationary = false;
		out.validation = {};
		out.key = ModelKey(out);
		out.roots = value.roots;
		out.reflectedPowerFraction = value.reflected;
		out.omittedBouncePower = value.tail;
		out.physicalExistenceCertified = false;
		out.sourceAllocationIdentifiable = false;
		out.localAmplitudeSpectrum = {};
		out.absoluteScaleKnown =
			r.profiles[out.profile].contract.incidentDomain == Indirect::Root::Domain::PhysicalLux &&
			r.profiles[out.profile].contract.calibration.kind == CalibrationKind::PhysicallyAuthored;
		score = value.score;
		work = e.work;
		if (predictions)
			*predictions = std::move(value.values);
		return {};
	}
	catch (const Error &error)
	{
		out = {};
		if (engine)
			work = engine->work;
		return error;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Hypothesis assessment allocation");
	}
}
Error EvaluateFrozen(const Request &r, const Model &model,
					 std::shared_ptr<const ChunkSource<Observation>> source, uint64_t count, Role role,
					 const Disk::Digest &partition, const ReadContext &read, Score &score,
					 std::vector<Prediction> *values, Work *work)
{
	score = {};
	if (values)
		values->clear();
	if (work)
		*work = {};
	try
	{
		Need(role != Role::Training && !Zero(partition) && model.strictReevaluated, Code::InvalidInput,
			 "Freeze before independent validation/held-out evaluation");
		Need(model.profile < r.profiles.size() && model.key == ModelKey(model), Code::InvalidInput,
			 "Invalid or modified frozen model parameters/profile");
		Engine e(r, read);
		e.identity = model.trainingKey;
		Need(!Zero(e.identity) && model.geometryKey == r.geometry.geometryKey &&
				 model.policyKey == e.Configuration() && model.transferKey == r.transfer->Key(),
			 Code::InvalidState, "Frozen model generation mismatch");
		auto rows = e.Load(source, count, role);
		Need(e.Place(model), Code::MissingGeometry, "Frozen source geometry invalid");
		auto result = e.Evaluate(model, rows, true, false);
		score = std::move(result.score);
		score.partitionKey = partition;
		score.work = e.work;
		if (values)
			*values = std::move(result.values);
		if (work)
			*work = e.work;
		return {};
	}
	catch (const Error &error)
	{
		return error;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Frozen joint evaluation allocation");
	}
}
const char *StateName(State s)
{
	static const char *names[] = {"NotStarted",	 "Converged",		   "Partial",		   "Unidentifiable",
								  "Unvalidated", "InsufficientSignal", "Unavailable",	   "Cancelled",
								  "Budget",		 "NumericalFailure",   "PhysicallyInvalid"};
	return unsigned(s) < sizeof(names) / sizeof(*names) ? names[unsigned(s)] : "Invalid";
}
} // namespace Joint
} // namespace Core
} // namespace Rend2LightRecon
