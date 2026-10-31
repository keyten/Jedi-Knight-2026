// Conditional configuration sets and empirical event-rate calibration, S12.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "uncertainty.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Uncertainty
{
namespace
{
using namespace Bake::Detail;
void Need(bool v, Code c, const char *s)
{
	if (!v)
		throw Failure(c, s);
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (auto c : s)
		k.U(uint8_t(c));
}
bool Cancelled(const ReadContext &r) { return r.cancellation && r.cancellation->IsCancelled(); }
bool Finite(double v) { return std::isfinite(v); }
size_t Bin(const CalibrationPolicy &p, double x)
{
	size_t n = 0;
	while (n + 2 < p.binEdges.size() && x >= p.binEdges[n + 1])
		++n;
	return n;
}
double Peak(Vec3 v) { return std::max(double(v.x), std::max(double(v.y), double(v.z))); }
Estimate Range(double a, double b)
{
	Estimate e;
	e.bounded = true;
	e.lower = float(a);
	e.upper = float(b);
	return e;
}
class Rows final : public ChunkSource<Joint::Observation>
{
  public:
	std::shared_ptr<std::vector<Joint::Observation>> v = std::make_shared<std::vector<Joint::Observation>>();
	Error Read(uint64_t first, size_t count, const ReadContext &r,
			   Chunk<Joint::Observation> &out) const override
	{
		if (Cancelled(r))
			return Failure(Code::Cancelled, "S12 cancelled");
		if (first > v->size() || count > v->size() - first)
			return Failure(Code::ProviderError, "S12 short training read");
		out = VectorChunk<Joint::Observation>(v, size_t(first), count);
		return {};
	}
};
struct Explorer
{
	const Joint::Request &r;
	const Options &o;
	const ReadContext &read;
	Result &out;
	const Joint::Validation *validation;
	Joint::Request fit;
	std::shared_ptr<Rows> original;
	double trainBest = 0, valBest = 0;
	Disk::Digest admission{}, partition{};
	std::map<Id, double> removal;
	std::map<Id, Joint::Candidate> originalCandidates;
	bool truncated = false;
	uint64_t random;
	Explorer(const Joint::Request &rr, const Options &oo, const ReadContext &cc, Result &result,
			 const Joint::Validation *v)
		: r(rr), o(oo), read(cc), out(result), validation(v), fit(rr), random(oo.seed)
	{
		fit.options.geometry = o.refitGeometry;
	}
	uint64_t Random()
	{
		random ^= random << 13;
		random ^= random >> 7;
		random ^= random << 17;
		return random;
	}
	void Add(const Joint::Work &w)
	{
		out.work.evaluations += w.evaluations;
		out.work.rays += w.rays;
		out.work.traversal += w.traversal;
		out.work.arithmetic += w.arithmetic;
	}
	void Guard()
	{
		Need(!Cancelled(read), Code::Cancelled, "S12 cancelled; retained witnesses are incomplete");
		Need(out.work.evaluations < o.maxEvaluations && out.work.rays < o.maxRays &&
				 out.work.arithmetic + out.work.traversal < o.maxWork,
			 Code::WorkLimit, "S12 cumulative work limit");
		Need(out.work.accountedBytes <= std::min(o.maxBytes, read.maxBytes ? read.maxBytes : o.maxBytes),
			 Code::MemoryLimit, "S12 memory limit");
	}
	bool Slot()
	{
		Guard();
		if (out.attempts.size() >= o.maxAttempts || out.witnesses.size() >= o.maxWitnesses)
		{
			truncated = true;
			return false;
		}
		return true;
	}
	void Budget(Joint::Request &q)
	{
		q.options.maxEvaluations =
			std::min(q.options.maxEvaluations, o.maxEvaluations - out.work.evaluations);
		q.options.maxRays = std::min(q.options.maxRays, o.maxRays - out.work.rays);
		q.options.maxWork = std::min(q.options.maxWork, o.maxWork - out.work.arithmetic - out.work.traversal);
		q.options.maxBytes =
			std::min(q.options.maxBytes, std::min(o.maxBytes, read.maxBytes ? read.maxBytes : o.maxBytes) -
											 out.work.accountedBytes);
	}
	bool Compare(Joint::Model &m, Joint::Score &training)
	{
		if (!training.available || !training.requiredBlocksPass || training.used == 0)
			return false;
		if (m.training.Total() > trainBest + o.trainingRegret)
			return false;
		if (validation)
		{
			Joint::Score score;
			auto e = validation->Evaluate(m, read, score);
			Add(score.work);
			if (!e)
				throw e;
			Need(score.partitionKey == partition && score.admissionKey == admission, Code::InvalidState,
				 "S12 validation partition/admission changed");
			if (!score.available || !score.requiredBlocksPass ||
				score.objective > valBest + o.validationRegret)
				return false;
			m.validation = score;
		}
		return true;
	}
	bool Probe(Joint::Model seed, Method method, Id source, bool refit = true,
			   std::shared_ptr<Rows> resampled = {})
	{
		if (!Slot())
			return false;
		Attempt a;
		a.method = method;
		a.source = source;
		Joint::Model current, assessed;
		Joint::Work work;
		Joint::Score score;
		Joint::Request q = fit;
		// All model alternatives use the same original nuisance bounds; only the
		// deliberately profiled coordinate is fixed in seed.candidate.shape.
		if (resampled)
		{
			q.training = resampled;
			q.count = resampled->v->size();
		}
		Budget(q);
		Error e;
		if (refit)
		{
			if (out.work.refits >= o.maxRefits)
			{
				truncated = true;
				return false;
			}
			++out.work.refits;
			e = Joint::Refit(q, seed, read, current, work);
			Add(work);
		}
		else
		{
			current = std::move(seed);
		}
		if (e)
		{
			// Evaluate resampled fits on the ORIGINAL stream at frozen parameters.
			// Restore original bounds/policy, never optimize the original stream again.
			for (auto &s : current.sources)
			{
				auto c = originalCandidates.find(s.candidate.id);
				if (c != originalCandidates.end())
					s.candidate = c->second;
			}
			auto assessment = r;
			Budget(assessment);
			++out.work.assessments;
			e = Joint::AssessHypothesis(assessment, current, read, assessed, score, work);
			Add(work);
		}
		++out.work.attempts;
		if (!e)
		{
			a.code = e.code;
			a.reason = e.message;
			out.attempts.push_back(a);
			if (e.code == Code::Cancelled || e.code == Code::MemoryLimit || e.code == Code::WorkLimit)
				throw e;
			return true;
		}
		a.available = true;
		a.model = assessed.key;
		a.training = assessed.training.Total();
		bool keep = Compare(assessed, score);
		a.validation = assessed.validation.objective;
		if (method == Method::Removal && validation)
		{
			// A removal may be rejected for a block error but still provide measurable
			// validation evidence. Never replace Unavailable with a black prediction.
			if (!assessed.validation.available)
			{
				Joint::Score s;
				auto err = validation->Evaluate(assessed, read, s);
				Add(s.work);
				if (!err)
					throw err;
				Need(s.partitionKey == partition && s.admissionKey == admission, Code::InvalidState,
					 "Removal validation generation changed");
				assessed.validation = s;
			}
			if (assessed.validation.available && assessed.validation.used)
			{
				double delta = std::max(0., assessed.validation.objective - valBest);
				removal[source] = delta / (1 + delta);
				a.validation = assessed.validation.objective;
			}
		}
		a.retained = keep;
		a.reason = keep ? "Conditional regret-set witness, not a posterior sample"
						: "Outside frozen regret/coverage policy";
		if (keep && std::none_of(out.witnesses.begin(), out.witnesses.end(),
								 [&](const Witness &w) { return w.model.key == assessed.key; }))
		{
			Witness w;
			w.model = std::move(assessed);
			w.method = method;
			out.work.accountedBytes += sizeof(Witness) + w.model.sources.size() * sizeof(Joint::Source) +
									   w.model.materials.size() * sizeof(Indirect::Reflectance);
			out.witnesses.push_back(std::move(w));
		}
		out.attempts.push_back(std::move(a));
		Guard();
		return true;
	}
	void Load()
	{
		Need(r.training && r.count && r.count <= o.maxObservations, Code::InvalidInput,
			 "S12 needs a bounded nonempty training stream");
		Need(r.count <= (std::min(o.maxBytes, read.maxBytes ? read.maxBytes : o.maxBytes) /
						 sizeof(Joint::Observation)),
			 Code::MemoryLimit, "S12 training rows exceed memory budget");
		original = std::make_shared<Rows>();
		Id last = 0;
		for (uint64_t i = 0; i < r.count;)
		{
			Guard();
			size_t n = size_t(std::min(uint64_t(128), r.count - i));
			Chunk<Joint::Observation> c;
			auto e = r.training->Read(i, n, read, c);
			if (!e)
				throw e;
			Need(c.count == n && c.data, Code::ProviderError, "S12 incomplete training chunk");
			for (size_t j = 0; j < n; ++j)
			{
				const auto &v = c.data[j];
				Need(v.id > last && v.role == Joint::Role::Training && v.block != 0, Code::InvalidInput,
					 "S12 requires ordered training IDs and spatial blocks");
				last = v.id;
				original->v->push_back(v);
			}
			i += n;
		}
		out.work.accountedBytes = original->v->capacity() * sizeof(Joint::Observation);
		fit.training = original;
		fit.count = original->v->size();
	}
	void Run(const Joint::Result &input)
	{
		Load();
		Need(!input.alternatives.empty(), Code::Unavailable, "S12 has no supported input configuration");
		Need(input.alternatives.size() <= o.maxWitnesses && input.alternatives.size() <= o.maxAttempts,
			 Code::WorkLimit, "S12 structural alternatives exceed declared budget");
		for (const auto &c : r.candidates)
			originalCandidates.emplace(c.id, c);
		for (const auto &mode : input.alternatives)
			for (const auto &s : mode.sources)
				originalCandidates.emplace(s.candidate.id, s.candidate);
		auto bases = input.alternatives;
		std::sort(bases.begin(), bases.end(),
				  [](const Joint::Model &a, const Joint::Model &b) { return a.key < b.key; });
		trainBest = 1e300;
		valBest = 1e300;
		for (auto &m : bases)
		{
			Guard();
			Need(m.strictReevaluated, Code::InvalidInput, "S12 input was not strictly reevaluated");
			Joint::Model checked;
			Joint::Score score;
			Joint::Work w;
			auto q = r;
			Budget(q);
			auto err = Joint::AssessHypothesis(q, m, read, checked, score, w);
			Add(w);
			++out.work.assessments;
			if (!err)
				throw err;
			Need(m.key == checked.key, Code::InvalidInput,
				 "S12 input model seal or training identity changed");
			Need(score.available && score.used, Code::Unavailable,
				 "S12 input has missing supported training coverage");
			m = checked;
			if (score.requiredBlocksPass)
				trainBest = std::min(trainBest, m.training.Total());
			if (validation)
			{
				Joint::Score v;
				err = validation->Evaluate(m, read, v);
				Add(v.work);
				if (!err)
					throw err;
				Need(v.partitionKey == validation->Key() && v.available, Code::Unavailable,
					 "S12 missing validation score");
				if (partition == Disk::Digest{})
				{
					partition = v.partitionKey;
					admission = v.admissionKey;
				}
				Need(v.partitionKey == partition && v.admissionKey == admission, Code::InvalidState,
					 "S12 inconsistent validation coverage");
				m.validation = v;
				if (v.requiredBlocksPass)
					valBest = std::min(valBest, v.objective);
			}
			out.trainingKey = m.trainingKey;
			out.geometryKey = m.geometryKey;
		}
		Need(trainBest < 1e299 && (!validation || valBest < 1e299), Code::Unavailable,
			 "S12 no input configuration passes required spatial blocks");
		for (auto &m : bases)
			if (!Probe(m, Method::InputAlternative, 0, false))
				return;
		// Probe every retained structural mode, not just the numerically preferred
		// source list. Resource truncation is explicit and invalidates completeness.
		for (const auto &base : bases)
		{
			for (size_t i = 0; i < base.sources.size(); ++i)
			{
				const auto &source = base.sources[i];
				Id id = source.candidate.id;
				if (source.candidate.mayRemove)
				{
					auto m = base;
					m.sources.erase(m.sources.begin() + i);
					if (!Probe(m, Method::Removal, id))
						return;
				}
				if (source.candidate.root == TransportRootKind::LocalEmitter)
				{
					const V directions[] = {V(1, 0, 0),		  V(0, 1, 0),		V(0, 0, 1),
											Unit(V(1, 1, 0)), Unit(V(1, 0, 1)), Unit(V(0, 1, 1))};
					for (size_t level = 0; level < o.positionLevels; ++level)
						for (const auto &d : directions)
							for (int sign : {-1, 1})
							{
								auto m = base;
								auto &s = m.sources[i];
								s.value.geometry.position.world =
									(V(s.value.geometry.position.world) +
									 d * (sign * o.positionStepWorld * (level + 1)))
										.Float();
								s.candidate.shape.fitPosition = false;
								if (!Probe(m, Method::PositionProfile, id))
									return;
							}
				}
				for (unsigned channel = 0; channel < 3; ++channel)
					for (size_t j = 0; j < o.colourProfiles; ++j)
					{
						auto m = base;
						auto &s = m.sources[i];
						auto &c = s.candidate.shape;
						double t = o.colourProfiles == 1 ? .5 : double(j) / (o.colourProfiles - 1);
						V lower(c.lowerRgb), upper(c.upperRgb), value(s.value.rgbAmplitude);
						value[channel] = lower[channel] + t * (upper[channel] - lower[channel]);
						lower[channel] = upper[channel] = value[channel];
						c.lowerRgb = lower.Float();
						c.upperRgb = upper.Float();
						s.value.rgbAmplitude = value.Float();
						if (!Probe(m, Method::ColourProfile, id))
							return;
					}
				// Angular / dimension perturbations are model-conditional alternatives.
				// No invented physical angular tolerance is used in the reference model.
				if (o.positionLevels &&
					(source.value.geometry.frame.known || source.environment.orientation.known))
				{
					for (int sign : {-1, 1})
					{
						auto m = base;
						auto &s = m.sources[i];
						double a = sign * .05;
						Frame &f = s.candidate.root == TransportRootKind::Sun ? s.environment.orientation
																			  : s.value.geometry.frame;
						V right(f.right), up(f.up), forward(f.forward);
						f.right = (right * std::cos(a) + forward * std::sin(a)).Float();
						f.forward = (forward * std::cos(a) - right * std::sin(a)).Float();
						f.up = up.Float();
						s.candidate.shape.fitFrame = false;
						s.candidate.fitEnvironmentDirection = false;
						if (!Probe(m, Method::GeometryProfile, id))
							return;
					}
				}
				auto kind = source.value.geometry.kind;
				if (o.positionLevels && (kind == EmitterKind::Rectangle || kind == EmitterKind::Tube))
					for (int sign : {-1, 1})
					{
						auto m = base;
						auto &s = m.sources[i];
						auto &g = s.value.geometry.geometry;
						double a = std::exp(sign * .1);
						if (kind == EmitterKind::Rectangle)
						{
							g.halfWidthWorld *= float(a);
							g.halfHeightWorld *= float(a);
						}
						else
						{
							g.halfLengthWorld *= float(a);
							g.radiusWorld *= float(a);
						}
						s.candidate.shape.fitDimensions = false;
						if (!Probe(m, Method::GeometryProfile, id))
							return;
					}
			}
			if (o.materialProfiles)
				for (size_t i = 0; i < base.materials.size(); ++i)
				{
					const auto &material = base.materials[i];
					if (material.knowledge == Indirect::MaterialKnowledge::Known)
						continue;
					for (int side : {0, 1})
					{
						auto m = base;
						auto &v = m.materials[i];
						v.initial = side ? v.upper : v.lower;
						v.lower = v.upper = v.initial;
						if (!Probe(m, Method::MaterialProfile, 0))
							return;
					}
				}
			std::set<Id> blocks;
			for (const auto &v : *original->v)
				blocks.insert(v.block);
			std::vector<Id> ordered(blocks.begin(), blocks.end());
			for (size_t k = 0; k < std::min(o.deleteBlocks, ordered.size()); ++k)
			{
				auto rows = std::make_shared<Rows>();
				for (const auto &v : *original->v)
					if (v.block != ordered[k])
						rows->v->push_back(v);
				if (rows->v->empty())
					continue;
				++out.work.sampledBlocks;
				if (!Probe(base, Method::DeleteBlock, 0, true, rows))
					return;
			}
			for (size_t k = 0; k < o.subsampleReplicates && ordered.size() > 1; ++k)
			{
				// S11 normalizes each spatial group independently: multiplying all
				// areas in a group cancels. Use actual block subsampling without
				// replacement, not a falsely advertised multiplicity bootstrap.
				auto shuffled = ordered;
				for (size_t j = shuffled.size(); j > 1; --j)
					std::swap(shuffled[j - 1], shuffled[Random() % j]);
				std::set<Id> selected(shuffled.begin(),
									  shuffled.begin() + std::max(size_t(1), shuffled.size() / 2));
				auto rows = std::make_shared<Rows>();
				for (auto v : *original->v)
				{
					if (selected.count(v.block))
						rows->v->push_back(v);
				}
				out.work.sampledBlocks += selected.size();
				if (!Probe(base, Method::SubsampleBlocks, 0, true, rows))
					return;
			}
			for (size_t k = 0; k < o.restarts; ++k)
			{
				auto m = base;
				for (auto &s : m.sources)
				{
					V lo(s.candidate.shape.lowerRgb), hi(s.candidate.shape.upperRgb), v;
					for (unsigned c = 0; c < 3; ++c)
						v[c] = lo[c] + (hi[c] - lo[c]) * (double(Random() % 1000001) / 1000000);
					s.value.rgbAmplitude = v.Float();
				}
				if (!Probe(m, Method::Restart, 0))
					return;
			}
		}
	}
};
void Tracks(Result &out, const Joint::Request &r, const Options &o, const Explorer &e,
			const CalibrationModel *calibration)
{
	std::map<std::pair<bool, Id>, size_t> identities;
	for (size_t w = 0; w < out.witnesses.size(); ++w)
	{
		auto &witness = out.witnesses[w];
		std::set<size_t> used;
		for (const auto &s : witness.model.sources)
		{
			// Provenance identity first. Proximity is deliberately NOT enough to collapse
			// independently existing adjacent lamps. A family alias is used only when
			// both alternatives are mutually exclusive and never occur in one model.
			auto identity = std::make_pair(s.candidate.family != 0,
										   s.candidate.family ? s.candidate.family : s.candidate.id);
			auto found = identities.find(identity);
			size_t index;
			if (found == identities.end() || used.count(found->second))
			{
				Track t;
				t.id = s.candidate.id;
				if (s.candidate.family)
				{
					for (const auto &c : r.candidates)
						if (c.family == s.candidate.family)
							t.id = std::min(t.id, c.id);
					for (const auto &mode : out.witnesses)
						for (const auto &other : mode.model.sources)
							if (other.candidate.family == s.candidate.family)
								t.id = std::min(t.id, other.candidate.id);
				}
				while (std::any_of(out.tracks.begin(), out.tracks.end(),
								   [&](const Track &x) { return x.id == t.id; }))
					++t.id;
				index = out.tracks.size();
				out.tracks.push_back(t);
				if (found == identities.end())
					identities[identity] = index;
			}
			else
				index = found->second;
			used.insert(index);
			auto &t = out.tracks[index];
			witness.tracks.push_back(t.id);
			t.configurations.push_back(w);
			if (std::find(t.candidateIds.begin(), t.candidateIds.end(), s.candidate.id) ==
				t.candidateIds.end())
				t.candidateIds.push_back(s.candidate.id);
		}
	}
	for (auto &t : out.tracks)
	{
		std::vector<V> positions, directions;
		std::set<unsigned> types;
		std::set<size_t> profiles;
		std::set<TransportRootKind> roots;
		double lo = 1e300, hi = 0;
		V colourLo(1, 1, 1), colourHi;
		bool zeroColour = false;
		bool knownMaterial = true, conditionalTransport = false;
		t.minimumSceneSources = UINT32_MAX;
		t.absoluteScaleKnown = true;
		for (size_t w = 0; w < out.witnesses.size(); ++w)
		{
			const auto &witness = out.witnesses[w];
			const auto &m = witness.model;
			t.minimumSceneSources = std::min(t.minimumSceneSources, uint32_t(m.sources.size()));
			t.maximumSceneSources = std::max(t.maximumSceneSources, uint32_t(m.sources.size()));
			for (size_t i = 0; i < witness.tracks.size(); ++i)
				if (witness.tracks[i] == t.id)
				{
					const auto &s = m.sources[i];
					types.insert(unsigned(s.value.geometry.kind));
					roots.insert(s.candidate.root);
					profiles.insert(m.profile);
					t.absoluteScaleKnown &= m.absoluteScaleKnown;
					if (s.candidate.root == TransportRootKind::LocalEmitter)
						positions.push_back(V(s.value.geometry.position.world));
					const Frame &f = s.candidate.root == TransportRootKind::Sun ? s.environment.orientation
																				: s.value.geometry.frame;
					if (f.known)
						directions.push_back(
							V(s.value.geometry.kind == EmitterKind::Tube ? f.right : f.forward));
					lo = std::min(lo, Peak(s.value.rgbAmplitude));
					hi = std::max(hi, Peak(s.value.rgbAmplitude));
					double peak = Peak(s.value.rgbAmplitude);
					if (peak > 0)
					{
						V c = V(s.value.rgbAmplitude) / peak;
						for (unsigned k = 0; k < 3; ++k)
						{
							colourLo[k] = std::min(colourLo[k], c[k]);
							colourHi[k] = std::max(colourHi[k], c[k]);
						}
					}
					else
						zeroColour = true;
					for (const auto &mat : m.materials)
						knownMaterial &= mat.knowledge == Indirect::MaterialKnowledge::Known;
					conditionalTransport |= m.reflectedPowerFraction > 0;
					if (witness.method == Method::InputAlternative)
						++t.inputWitnesses;
					if (witness.method == Method::SubsampleBlocks || witness.method == Method::DeleteBlock)
						++t.resampledWitnesses;
				}
		}
		t.types = uint32_t(types.size());
		t.peakColourBounded = !zeroColour && lo <= hi;
		t.peakColourLower = colourLo.Float();
		t.peakColourUpper = colourHi.Float();
		if (lo <= hi)
			t.relativePeak = Range(lo, hi);
		if (positions.size() > 1)
		{
			V mean;
			for (auto p : positions)
				mean = mean + p;
			mean = mean / double(positions.size());
			double cov[6] = {};
			for (auto p : positions)
			{
				V d = p - mean;
				cov[0] += d.x * d.x;
				cov[1] += d.x * d.y;
				cov[2] += d.x * d.z;
				cov[3] += d.y * d.y;
				cov[4] += d.y * d.z;
				cov[5] += d.z * d.z;
			}
			for (auto &v : cov)
				v /= positions.size() - 1;
			auto &c = t.scatter;
			c.known = true;
			c.reference = mean.Float();
			c.xx = float(cov[0]);
			c.xy = float(cov[1]);
			c.xz = float(cov[2]);
			c.yy = float(cov[3]);
			c.yz = float(cov[4]);
			c.zz = float(cov[5]);
			c.samples = uint32_t(positions.size());
			c.method = "Descriptive configuration scatter; not a statistical error covariance";
		}
		if (!directions.empty())
		{
			double spread = 0;
			for (auto a : directions)
				for (auto b : directions)
					spread = std::max(spread, std::acos(std::max(-1., std::min(1., Dot(Unit(a), Unit(b))))));
			t.directionSpreadRadians = Range(0, spread);
		}
		bool absent = t.configurations.size() != out.witnesses.size();
		t.typeRanking = types.size() == 1 && roots.size() == 1 ? 1 : 0;
		for (Id id : t.candidateIds)
		{
			auto it = e.removal.find(id);
			if (it != e.removal.end())
			{
				t.removalAvailable = true;
				t.removalRanking = std::max(t.removalRanking, it->second);
			}
		}
		bool positive = false;
		for (const auto &w : out.witnesses)
			positive |= w.model.validation.positive;
		t.fieldRanking = t.removalRanking;
		auto add = [&](UncertaintyCause c, UncertaintyStatus s, const char *reason) {
			UncertaintyFactor f;
			f.cause = c;
			f.status = s;
			f.reason = reason;
			t.factors.push_back(f);
		};
		add(UncertaintyCause::Existence,
			!positive ? UncertaintyStatus::Unknown
					  : absent ? UncertaintyStatus::Ambiguous : UncertaintyStatus::Conditional,
			"Source removal and validation ranking; existence requires independent event calibration");
		add(UncertaintyCause::Type,
			types.size() > 1 || roots.size() > 1 ? UncertaintyStatus::Ambiguous
												 : UncertaintyStatus::Conditional,
			"Type/root alternatives in retained complete configurations; untested types remain possible");
		add(UncertaintyCause::Position,
			positions.size() > 1 ? UncertaintyStatus::Ambiguous : UncertaintyStatus::Unknown,
			"Bounded sampled position profiles; no proof outside search domain and no 95% XYZ interval");
		add(UncertaintyCause::Orientation,
			directions.empty() ? UncertaintyStatus::Unknown : UncertaintyStatus::Conditional,
			"Travel/axis directions retained per configuration; angular probes are conditional");
		add(UncertaintyCause::Dimensions, UncertaintyStatus::Conditional,
			"Shape alternatives and bounded dimension probes, not unique physical extent");
		add(UncertaintyCause::RelativeColour,
			lo < hi ? UncertaintyStatus::Ambiguous : UncertaintyStatus::Conditional,
			"Joint RGB/profile/material witnesses; marginal peak bounds are not a Cartesian feasible box");
		add(UncertaintyCause::AbsoluteScale,
			t.absoluteScaleKnown ? UncertaintyStatus::Supported : UncertaintyStatus::Unknown,
			"Physical calibration is independent of fitted relative RGB; unknown scale has no finite bound");
		add(UncertaintyCause::BakeProfile,
			profiles.size() > 1 ? UncertaintyStatus::Ambiguous : UncertaintyStatus::Conditional,
			"Conditional on supplied feature-scoped profile hypotheses; no Raven compiler identification");
		add(UncertaintyCause::Material,
			knownMaterial ? UncertaintyStatus::Conditional : UncertaintyStatus::Unknown,
			"Bake-time reflectance bounds retained; current PBR albedo is not substituted");
		add(UncertaintyCause::Visibility, UncertaintyStatus::Conditional,
			"Strict local geometry queries used; unavailable optics reject a probe rather than become "
			"transparent");
		add(UncertaintyCause::ShadowAssociation,
			r.shadow ? UncertaintyStatus::Conditional : UncertaintyStatus::Unknown,
			"Automatic shadow alternatives are not verified caster/source correspondences");
		add(UncertaintyCause::Attribution,
			conditionalTransport || roots.size() > 1 ? UncertaintyStatus::Ambiguous
													 : UncertaintyStatus::Conditional,
			"Root and bounce membership retained; discrepancy is not a new emitter");
		add(UncertaintyCause::Numerical,
			out.complete ? UncertaintyStatus::Conditional : UncertaintyStatus::Unknown,
			"Finite probe/refit schedule and strict values; sampled regret set is not an exhaustive feasible "
			"set");
		if (calibration && positive && !o.evaluationFamily.empty())
		{
			if (t.removalAvailable)
				CalibratedRate(*calibration, Event::Existence, t.removalRanking, o.population,
							   o.evaluationFamily, t.existence);
			CalibratedRate(*calibration, Event::CorrectType, t.typeRanking, o.population, o.evaluationFamily,
						   t.type);
			// Exact success is separately calibrated. Existence is never reused as exact-use probability.
			CalibratedRate(*calibration, Event::ExactUse, t.removalRanking, o.population, o.evaluationFamily,
						   t.exactUse);
			CalibratedRate(*calibration, Event::FieldUseful, t.fieldRanking, o.population, o.evaluationFamily,
						   t.fieldUse);
		}
		// Suitability is an event-calibrated admission hint, never global uniqueness
		// or renderer publication. Uncalibrated legacy brightness cannot pass it.
		bool poseModes = false;
		if (positions.size() > 1)
			for (auto p : positions)
				poseModes |= Length(p - positions.front()) > 1e-6;
		bool physicalRoot = roots.size() == 1 && (roots.count(TransportRootKind::LocalEmitter) ||
												  roots.count(TransportRootKind::Sun));
		bool disqualified = !physicalRoot || !t.absoluteScaleKnown || absent || types.size() != 1 ||
							!knownMaterial || poseModes ||
							(t.directionSpreadRadians.bounded && t.directionSpreadRadians.upper > 1e-5);
		t.exactLight = disqualified ? Suitability::Unsuitable
									: out.complete && positive && t.exactUse.probability.bounded &&
											  t.exactUse.probability.lower >= o.minimumExactRateLower
										  ? Suitability::Candidate
										  : Suitability::Unknown;
		t.approximateField = out.complete && positive && t.fieldUse.probability.bounded &&
									 t.fieldUse.probability.lower >= o.minimumFieldRateLower
								 ? Suitability::Candidate
								 : Suitability::Unknown;
	}
}
} // namespace
Error Calibrate(const CalibrationPolicy &policy, const std::vector<CalibrationTrial> &input,
				const ReadContext &read, CalibrationModel &out)
{
	out = {};
	try
	{
		Need(policy.independentFamiliesAttested && !policy.population.empty() && !policy.protocol.empty(),
			 Code::InvalidInput, "Calibration needs an attested independent population and frozen protocol");
		Need(policy.binEdges.size() >= 2 && policy.binEdges.size() <= 64 && policy.binEdges.front() == 0 &&
				 policy.binEdges.back() == 1 && policy.minimumFamilies > 1 && Finite(policy.alpha) &&
				 policy.alpha > 0 && policy.alpha < 1,
			 Code::InvalidInput, "Invalid calibration policy");
		for (size_t i = 1; i < policy.binEdges.size(); ++i)
			Need(Finite(policy.binEdges[i]) && policy.binEdges[i] > policy.binEdges[i - 1],
				 Code::InvalidInput, "Calibration bins must strictly increase");
		Need(input.size() <=
				 (read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024) / sizeof(CalibrationTrial),
			 Code::MemoryLimit, "Calibration input limit");
		uint64_t memory = uint64_t(input.size()) * sizeof(CalibrationTrial) + policy.population.size() +
						  policy.protocol.size();
		const auto limit = read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024;
		for (const auto &t : input)
		{
			Need(!Cancelled(read), Code::Cancelled, "Calibration cancelled");
			Need(memory <= limit, Code::MemoryLimit, "Calibration string budget");
			auto bytes = t.family.size() + t.scene.size() + t.trial.size();
			Need(bytes <= limit - memory, Code::MemoryLimit, "Calibration string budget");
			memory += bytes;
		}
		Need(memory <= limit / 4, Code::MemoryLimit, "Calibration temporary-map/sort budget");
		auto trials = input;
		std::sort(trials.begin(), trials.end(), [](const CalibrationTrial &a, const CalibrationTrial &b) {
			return std::tie(a.family, a.scene, a.trial, a.event) <
				   std::tie(b.family, b.scene, b.trial, b.event);
		});
		using Group = std::pair<unsigned, size_t>;
		using Counts = std::pair<uint64_t, uint64_t>;
		std::map<Group, std::map<std::string, Counts>> sums;
		std::map<Group, uint32_t> unknown;
		std::set<std::string> families;
		GeometryKey hash;
		hash.U(kVersion);
		Text(hash, policy.population);
		Text(hash, policy.protocol);
		hash.U(policy.minimumFamilies);
		hash.F(policy.alpha);
		for (double v : policy.binEdges)
			hash.F(v);
		std::tuple<std::string, std::string, std::string, Event> last;
		bool first = true;
		for (const auto &t : trials)
		{
			Need(!Cancelled(read), Code::Cancelled, "Calibration cancelled");
			Need(!t.family.empty() && !t.scene.empty() && !t.trial.empty() &&
					 unsigned(t.event) <= unsigned(Event::FieldUseful) && Finite(t.ranking) &&
					 t.ranking >= 0 && t.ranking <= 1,
				 Code::InvalidInput, "Invalid calibration trial");
			auto identity = std::make_tuple(t.family, t.scene, t.trial, t.event);
			Need(first || identity != last, Code::InvalidInput, "Duplicate calibration event");
			last = identity;
			first = false;
			families.insert(t.family);
			Group g(unsigned(t.event), Bin(policy, t.ranking));
			if (t.labelKnown)
			{
				auto &c = sums[g][t.family];
				++c.second;
				c.first += t.label ? 1 : 0;
			}
			else
				++unknown[g];
			Text(hash, t.family);
			Text(hash, t.scene);
			Text(hash, t.trial);
			hash.U(unsigned(t.event));
			hash.F(t.ranking);
			hash.U(t.labelKnown);
			hash.U(t.labelKnown && t.label);
		}
		out.policy = policy;
		out.families.assign(families.begin(), families.end());
		out.key = hash.Finish();
		for (unsigned event = 0; event <= unsigned(Event::FieldUseful); ++event)
			for (size_t b = 0; b + 1 < policy.binEdges.size(); ++b)
			{
				CalibrationBin v;
				v.event = Event(event);
				v.bin = uint32_t(b);
				Group g(event, b);
				v.unlabelled = unknown[g];
				for (const auto &family : sums[g])
				{
					v.rate += double(family.second.first) / family.second.second;
					v.rows += uint32_t(family.second.second);
					++v.families;
				}
				if (v.families)
				{
					v.rate /= v.families;
					double eps = std::sqrt(std::log(2 / policy.alpha) / (2 * v.families));
					v.lower = std::max(0., v.rate - eps);
					v.upper = std::min(1., v.rate + eps);
				}
				out.bins.push_back(v);
			}
		return {};
	}
	catch (const Error &e)
	{
		out = {};
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Calibration allocation");
	}
}
Error CalibratedRate(const CalibrationModel &model, Event event, double rank, const std::string &population,
					 const std::string &family, CalibratedConfidence &out)
{
	out = {};
	if (!Finite(rank) || rank < 0 || rank > 1 || model.policy.binEdges.size() < 2 ||
		population != model.policy.population || family.empty())
		return Failure(Code::Unavailable, "No calibrated population/bin");
	if (!model.policy.independentFamiliesAttested || model.key == Disk::Digest{})
		return Failure(Code::Unavailable, "Unattested calibration table");
	if (std::find(model.families.begin(), model.families.end(), family) != model.families.end())
		return Failure(Code::InvalidInput, "Evaluation family appears in calibration");
	for (const auto &b : model.bins)
		if (b.event == event && b.bin == Bin(model.policy, rank) &&
			b.families >= model.policy.minimumFamilies)
		{
			out.meaning = ConfidenceMeaning::IndependentEmpiricalBinRate;
			out.probability = Range(b.lower, b.upper);
			out.probability.known = true;
			out.probability.value = float(b.rate);
			out.event = EventName(event);
			out.population = population;
			out.protocol = model.policy.protocol;
			out.calibrationHash = Disk::Hex(model.key);
			out.independentFamilies = b.families;
			out.intervalNominal = float(1 - model.policy.alpha);
			return {};
		}
	return Failure(Code::Unavailable, "Insufficient independent families in frozen calibration bin");
}
Error Analyze(const Joint::Request &r, const Joint::Result &input, const Options &o, const ReadContext &read,
			  Result &out, const Joint::Validation *validation, const CalibrationModel *calibration)
{
	out = {};
	std::unique_ptr<Explorer> engine;
	try
	{
		Need(o.maxAttempts && o.maxWitnesses && o.maxRefits && o.maxEvaluations && o.maxRays && o.maxWork &&
				 o.maxBytes && Finite(o.trainingRegret) && o.trainingRegret >= 0 &&
				 Finite(o.validationRegret) && o.validationRegret >= 0 && Finite(o.positionStepWorld) &&
				 o.positionStepWorld > 0,
			 Code::InvalidInput, "Invalid S12 budget or regret policy");
		Need(!o.finiteUniverseAttested, Code::InvalidInput,
			 "Sampled continuous profiling cannot certify an exhaustive finite hypothesis universe");
		Need(Finite(o.minimumExactRateLower) && o.minimumExactRateLower >= 0 &&
				 o.minimumExactRateLower <= 1 && Finite(o.minimumFieldRateLower) &&
				 o.minimumFieldRateLower >= 0 && o.minimumFieldRateLower <= 1,
			 Code::InvalidInput, "S12 suitability thresholds must be frozen finite rates in [0,1]");
		if (calibration)
			Need(std::find(calibration->families.begin(), calibration->families.end(), o.evaluationFamily) ==
					 calibration->families.end(),
				 Code::InvalidInput, "S12 evaluation family overlaps calibration");
		engine.reset(new Explorer(r, o, read, out, validation));
		engine->Run(input);
		out.complete = !engine->truncated;
		out.state = out.complete ? State::ConditionalSet : State::Partial;
		Tracks(out, r, o, *engine, calibration);
		if (out.complete && out.witnesses.size() > 1)
			out.state = State::Ambiguous;
		if (out.witnesses.empty())
			out.state = State::Unknown;
		if (validation && out.complete &&
			std::none_of(out.witnesses.begin(), out.witnesses.end(),
						 [](const Witness &w) { return w.model.validation.positive; }))
			out.state = State::Unknown;
		out.limitations = {"Sampled conditional regret witnesses, not confidence/credible intervals or proof "
						   "of global uniqueness",
						   "No absolute photometric scale is inferred from legacy-relative RGB",
						   "Calibration is event/bin/population specific and requires independent families; "
						   "stock generalization is unverified"};
	}
	catch (const Error &e)
	{
		out.complete = false;
		out.state = e.code == Code::Cancelled
						? State::Cancelled
						: e.code == Code::WorkLimit || e.code == Code::MemoryLimit ? State::Budget
																				   : State::Unsupported;
		out.limitations.push_back(e.message);
		if (engine)
			Tracks(out, r, o, *engine, nullptr);
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out.complete = false;
		out.state = State::Budget;
		return Failure(Code::MemoryLimit, "S12 allocation");
	}
	GeometryKey hash;
	hash.U(kVersion);
	hash.Digest(out.trainingKey);
	hash.Digest(out.geometryKey);
	hash.Digest(validation ? validation->Key() : Disk::Digest{});
	hash.Digest(calibration ? calibration->key : Disk::Digest{});
	Text(hash, o.population);
	Text(hash, o.evaluationFamily);
	hash.U(o.seed);
	hash.F(o.trainingRegret);
	hash.F(o.validationRegret);
	hash.F(o.positionStepWorld);
	hash.U(o.positionLevels);
	hash.U(o.colourProfiles);
	hash.U(o.deleteBlocks);
	hash.U(o.subsampleReplicates);
	hash.U(o.restarts);
	hash.U(o.materialProfiles);
	hash.U(o.refitGeometry);
	hash.U(o.maxAttempts);
	hash.U(o.maxRefits);
	hash.U(o.maxWitnesses);
	hash.U(o.maxObservations);
	hash.U(o.maxBytes);
	hash.U(o.maxWork);
	hash.U(o.maxRays);
	hash.U(o.maxEvaluations);
	hash.F(o.minimumFieldRateLower);
	hash.F(o.minimumExactRateLower);
	hash.U(out.complete);
	for (const auto &w : out.witnesses)
		hash.Digest(w.model.key);
	for (const auto &a : out.attempts)
	{
		hash.U(unsigned(a.method));
		hash.U(a.source);
		hash.Digest(a.model);
		hash.U(a.retained);
		hash.U(unsigned(a.code));
	}
	out.key = hash.Finish();
	return {};
}
Error Attach(const Result &result, const Joint::Request &r, const std::vector<SourceBinding> &bindings,
			 Id provenance, Id firstRecord, ReconstructedLightingAsset &asset)
{
	try
	{
		Need(provenance && firstRecord && result.key != Disk::Digest{} && !result.witnesses.empty(),
			 Code::InvalidInput, "S12 attachment IDs and sealed nonempty result required");
		Need(std::any_of(asset.provenanceRecords.begin(), asset.provenanceRecords.end(),
						 [&](const Provenance &p) { return p.id == provenance; }),
			 Code::InvalidInput, "S12 attachment provenance missing");
		Need(result.geometryKey == r.geometry.geometryKey, Code::InvalidState,
			 "S12 attachment geometry changed");
		auto copy = asset;
		std::map<Id, Id> configIds;
		Id next = firstRecord;
		for (size_t i = 0; i < result.witnesses.size(); ++i)
		{
			const auto &w = result.witnesses[i];
			Need(w.model.profile < r.profiles.size() && w.tracks.size() == w.model.sources.size(),
				 Code::InvalidInput, "Invalid S12 witness");
			LightingConfiguration c;
			c.id = next++;
			c.provenance = provenance;
			c.key = Disk::Hex(w.model.key);
			c.profile = Disk::Hex(w.model.policyKey) + "/" + std::to_string(w.model.profile);
			c.calibrationHypothesis = r.profiles[w.model.profile].calibrationHypothesis;
			c.observationGain = float(r.profiles[w.model.profile].fixedGain);
			c.firstBounce = w.model.reflectedPowerFraction > 0;
			c.searchComplete = result.globalSearchComplete;
			for (size_t j = 0; j < w.model.sources.size(); ++j)
			{
				const auto &s = w.model.sources[j];
				JointParameterState v;
				v.track = w.tracks[j];
				v.root = s.candidate.root;
				v.kind = s.value.geometry.kind;
				v.position = s.value.geometry.position;
				v.frame = s.value.geometry.frame;
				if (v.root == TransportRootKind::Sun)
				{
					v.kind = EmitterKind::Directional;
					v.position = {};
					v.frame = s.environment.orientation;
				}
				v.dimensions = s.value.geometry.geometry;
				v.angular = s.value.geometry.angular;
				if (v.root == TransportRootKind::Sun)
					v.angular.kind = AngularKind::Parallel;
				v.amplitude = s.value.rgbAmplitude;
				v.amplitudeDomain = r.profiles[w.model.profile].model.law == Shape::Law::HistoricalBake
										? "compiler-relative-Q"
										: "specified-relative-integral";
				if (w.model.absoluteScaleKnown)
					v.amplitudeDomain = "canonical-physically-authored-RGB/source-kind-unit";
				v.legacySpotDistanceWorld = float(s.value.spotTargetDistanceWorld);
				v.legacySpotRadiusWorld = float(s.value.spotRadiusWorld);
				c.sources.push_back(v);
			}
			for (const auto &m : w.model.materials)
			{
				MaterialParameterState v;
				v.material = m.material;
				v.reflectance = m.initial;
				v.status = m.knowledge == Indirect::MaterialKnowledge::Known ? UncertaintyStatus::Supported
																			 : UncertaintyStatus::Conditional;
				c.materials.push_back(v);
			}
			copy.lightingConfigurations.push_back(c);
			configIds[i] = c.id;
		}
		std::set<Id> bound;
		for (const auto &binding : bindings)
		{
			Need(bound.insert(binding.source).second, Code::InvalidInput, "Duplicate S12 source binding");
			auto track = std::find_if(result.tracks.begin(), result.tracks.end(),
									  [&](const Track &t) { return t.id == binding.track; });
			Need(track != result.tracks.end(), Code::InvalidInput, "S12 track binding absent");
			Source *source = nullptr;
			for (auto &e : copy.emitters)
				if (e.source.id == binding.source)
					source = &e.source;
			for (auto &e : copy.environments)
				if (e.source.id == binding.source)
					source = &e.source;
			for (auto &e : copy.transport)
				if (e.source.id == binding.source)
					source = &e.source;
			Need(source != nullptr, Code::InvalidInput, "S12 must bind existing sources only");
			auto origin = std::find_if(copy.provenanceRecords.begin(), copy.provenanceRecords.end(),
									   [&](const Provenance &p) { return p.id == source->provenance; });
			Need(origin != copy.provenanceRecords.end() && origin->kind != ProvenanceKind::Authored &&
					 source->state != HypothesisState::Resolved,
				 Code::InvalidInput, "Authored/resolved sources are immutable under S12 attachment");
			SourceUncertainty u;
			u.id = next++;
			u.source = binding.source;
			u.track = binding.track;
			u.provenance = provenance;
			u.method =
				"S12 conditional joint profiles/block resampling and independent event-bin calibration v1";
			u.inputHash = Disk::Hex(result.key);
			u.factors = track->factors;
			u.existence = track->existence;
			u.type = track->type;
			u.exactUse = track->exactUse;
			u.fieldUse = track->fieldUse;
			u.positionScatter = track->scatter;
			u.relativePeak = track->relativePeak;
			u.directionSpreadRadians = track->directionSpreadRadians;
			u.peakColourBounded = track->peakColourBounded;
			u.peakColourLower = track->peakColourLower;
			u.peakColourUpper = track->peakColourUpper;
			u.absoluteScaleKnown = track->absoluteScaleKnown;
			u.complete = result.complete;
			u.minimumCount = track->minimumSceneSources;
			u.maximumCount = track->maximumSceneSources;
			u.exactLight = track->exactLight;
			u.approximateField = track->approximateField;
			for (auto index : track->configurations)
				u.configurations.push_back(configIds.at(index));
			copy.uncertainty.push_back(u);
			source->evidence.physicalEmitterConfidence = u.existence.probability;
			source->evidence.transportConfidence = u.fieldUse.probability;
			source->evidence.exactLight = u.exactLight;
			source->evidence.approximateIrradiance = u.approximateField;
		}
		std::string diagnostic;
		Need(Validate(copy, &diagnostic), Code::InvalidInput,
			 diagnostic.empty() ? "S12 attached asset failed canonical validation" : diagnostic.c_str());
		asset = std::move(copy);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "S12 attachment allocation");
	}
}
const char *StateName(State s)
{
	static const char *n[] = {"Unknown",   "ConditionalSet", "Ambiguous",  "Partial",
							  "Cancelled", "Budget",		 "Unsupported"};
	return unsigned(s) < 7 ? n[unsigned(s)] : "Invalid";
}
const char *EventName(Event s)
{
	static const char *n[] = {"SourceWithinDeclaredMatchingRegion", "CorrectSourceTypeGivenDetection",
							  "ExactLightUseWithinDeclaredTolerances", "HeldoutFieldUseful"};
	return unsigned(s) < 4 ? n[unsigned(s)] : "Invalid";
}
} // namespace Uncertainty
} // namespace Core
} // namespace Rend2LightRecon
