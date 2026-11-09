// SPDX-License-Identifier: GPL-2.0-or-later
#include "iterative_discovery.h"
#include "lightrecon/bake_math.h"
#include "lightrecon/geometry_key.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
namespace Rend2LightRecon { namespace Discovery {
using namespace Core::Bake::Detail;
namespace {
void Need(bool ok, Code code, const char *why) { if (!ok) throw Failure(code, why); }
void Check(Error e) { if (!e) throw e; }
void Cancel(const ReadContext &r) {
	Need(!(r.cancellation && r.cancellation->IsCancelled()), Code::Cancelled, "Discovery cancelled");
}
class Rows final : public ChunkSource<Joint::Observation> {
public:
	std::shared_ptr<std::vector<Joint::Observation>> values =
		std::make_shared<std::vector<Joint::Observation>>();
	Error Read(uint64_t first, size_t count, const ReadContext &r,
		Chunk<Joint::Observation> &out) const override {
		if (r.cancellation && r.cancellation->IsCancelled()) return Failure(Code::Cancelled, "Discovery read cancelled");
		if (first > values->size() || count > values->size() - first) return Failure(Code::ProviderError, "Discovery row range");
		out = VectorChunk<Joint::Observation>(values, size_t(first), count); return {};
	}
};
void Inspect(const std::vector<Joint::Observation> &rows, Joint::Role role, size_t maximum) {
	Need(!rows.empty() && rows.size() <= maximum, Code::WorkLimit, "Discovery row bounds");
	std::set<Id> ids;
	for (const auto &o : rows) {
		Need(o.id && o.region && o.block && o.role == role && ids.insert(o.id).second &&
			o.channel == Joint::Channel::Surface && Finite(o.receiver.world) &&
			std::isfinite(o.sigma) && o.sigma > 0, Code::InvalidInput, "Identified surface partition required");
	}
}
void Disjoint(const std::vector<Joint::Observation> &a, const std::vector<Joint::Observation> &b) {
	std::set<Id> ids, blocks, correlations;
	for (const auto &o : a) { ids.insert(o.id); blocks.insert(o.block); if (o.correlationGroup) correlations.insert(o.correlationGroup); }
	for (const auto &o : b) Need(!ids.count(o.id) && !blocks.count(o.block) &&
		(!o.correlationGroup || !correlations.count(o.correlationGroup)), Code::InvalidInput, "Discovery partition leakage");
}
std::shared_ptr<Rows> Snapshot(const Joint::Request &q, const ReadContext &read) {
	Need(q.training && q.count && q.count <= q.options.maxObservations, Code::WorkLimit, "Discovery training bounds");
	auto rows = std::make_shared<Rows>();
	for (uint64_t first = 0; first < q.count;) {
		Cancel(read);
		size_t n = size_t(std::min<uint64_t>(128, q.count - first));
		Chunk<Joint::Observation> chunk;
		Check(q.training->Read(first, n, read, chunk));
		Need(chunk.count == n && chunk.data, Code::ProviderError, "Short discovery training read");
		rows->values->insert(rows->values->end(), chunk.data, chunk.data + n); first += n;
	}
	return rows;
}
void Add(Joint::Work &a, const Joint::Work &b) {
	a.evaluations += b.evaluations; a.forwardCalls += b.forwardCalls;
	a.preparationCalls += b.preparationCalls; a.rays += b.rays;
	a.traversal += b.traversal; a.pairs += b.pairs; a.links += b.links;
	a.arithmetic += b.arithmetic; a.placementChecks += b.placementChecks;
	a.accountedBytes = std::max(a.accountedBytes, b.accountedBytes);
}
bool Contains(const Joint::Model &m, Id id) {
	return std::any_of(m.sources.begin(), m.sources.end(), [&](const Joint::Source &s) { return s.candidate.id == id; });
}
bool FamilyOccupied(const Joint::Model &m, const Joint::Candidate &c) {
	return c.family && std::any_of(m.sources.begin(), m.sources.end(), [&](const Joint::Source &s) { return s.candidate.family == c.family; });
}
bool NewPowerSupported(const Joint::Model &m, const Joint::Candidate &c, const Options &o) {
	for (const auto &s : m.sources) if (s.candidate.id == c.id) {
		bool active = false;
		for (unsigned k = 0; k < 3; ++k) {
			double upper = V(c.shape.upperRgb)[k], power = V(s.value.rgbAmplitude)[k];
			if (upper > 0) {
				if (power >= upper * (1 - 1e-5)) return false;
				active |= power > upper * o.minimumPowerFraction;
			}
		}
		return active;
	}
	return false;
}
void ExpandVolumes(Request &r) {
	Need(r.volumes.size() <= 8, Code::WorkLimit, "Discovery local volume count");
	for (const auto &v : r.volumes) {
		Need(v.divisions >= 2 && v.divisions <= 4 && v.positions.known &&
			Finite(v.positions.minWorld) && Finite(v.positions.maxWorld) &&
			v.positions.minWorld.x <= v.positions.maxWorld.x && v.positions.minWorld.y <= v.positions.maxWorld.y &&
			v.positions.minWorld.z <= v.positions.maxWorld.z, Code::InvalidInput, "Bounded discovery lattice required");
		Need(r.candidates.size() + v.divisions * v.divisions * v.divisions <= r.options.maxCandidates,
			Code::WorkLimit, "Discovery lattice/candidate budget before allocation");
		const auto &e = v.prototype.shape.initial;
		Need(v.prototype.id && v.prototype.region && !v.prototype.provenance.empty(),
			Code::InvalidInput, "Identified geometry search volume required");
		Need((e.geometry.kind == EmitterKind::Point && e.support.kind == SourceSupport::Kind::FreePoint) ||
			(e.geometry.kind == EmitterKind::Spot && e.support.kind == SourceSupport::Kind::FreeSpot),
			Code::InvalidInput, "Search volumes require explicit free point/spot support; never translate a mounted surface");
		for (unsigned x = 0; x < v.divisions; ++x) for (unsigned y = 0; y < v.divisions; ++y) for (unsigned z = 0; z < v.divisions; ++z) {
			auto c = v.prototype;
			GeometryKey key; key.U(22); key.Digest(r.fit.mapKey); key.U(c.id); key.B(v.positions);
			key.U(v.divisions); key.U(x); key.U(y); key.U(z); auto digest = key.Finish();
			Id id = 0; for (unsigned k = 0; k < 8; ++k) id |= Id(digest[k]) << (8 * k);
			Need(id != 0, Code::InvalidInput, "Discovery lattice identity collision");
			c.family = c.family ? c.family : c.id; c.id = id;
			V lo(v.positions.minWorld), hi(v.positions.maxWorld);
			c.shape.initial.geometry.source.id = id;
			c.shape.initial.geometry.position.world = V(lo.x + (hi.x - lo.x) * x / (v.divisions - 1),
				lo.y + (hi.y - lo.y) * y / (v.divisions - 1), lo.z + (hi.z - lo.z) * z / (v.divisions - 1)).Float();
			c.shape.centreBounds = v.positions;
			c.shape.initial.support.fixture = id;
			c.provenance += "; bounded geometry-only lattice pose, not historical source evidence";
			r.candidates.push_back(std::move(c));
		}
	}
}
bool Supported(const Joint::Score &s) { return s.available && s.coverage.complete && s.used == s.requested && !s.supportExcluded; }
double ValidationObjective(const Joint::Score &s, const Joint::Model &m, const Options &o) {
	return s.objective + o.complexityPerSource * m.sources.size();
}
bool BlocksPass(const Joint::Score &base, const Joint::Score &trial, double slack) {
	if (base.groups.size() != trial.groups.size()) return false;
	for (size_t i = 0; i < base.groups.size(); ++i) {
		const auto &a = base.groups[i]; const auto &b = trial.groups[i];
		if (a.channel != b.channel || a.region != b.region || a.block != b.block ||
			b.used != a.used || b.loss > a.loss + slack) return false;
	}
	return true;
}
std::vector<Region> Classify(const Request &r, const std::vector<Joint::Observation> &rows,
	const std::vector<Joint::Prediction> &predictions) {
	using Cell = std::tuple<Id, int64_t, int64_t, int64_t>;
	std::map<Cell, std::vector<size_t>> cells;
	std::map<Id, Evidence> evidence;
	for (const auto &e : r.evidence) evidence[e.region] = e;
	for (size_t i = 0; i < rows.size(); ++i) {
		V p(rows[i].receiver.world);
		int64_t c[3];
		for (unsigned k = 0; k < 3; ++k) {
			double v = std::floor(p[k] / r.options.cellWorld);
			Need(std::isfinite(v) && std::abs(v) < 1e12, Code::InvalidInput, "Discovery cell coordinate bounds");
			c[k] = int64_t(v);
		}
		cells[Cell(rows[i].region, c[0], c[1], c[2])].push_back(i);
	}
	std::vector<Region> result;
	for (const auto &entry : cells) {
		Region a; a.region = std::get<0>(entry.first);
		a.cell[0] = std::get<1>(entry.first); a.cell[1] = std::get<2>(entry.first); a.cell[2] = std::get<3>(entry.first);
		auto found = evidence.find(a.region);
		Evidence e; if (found != evidence.end()) e = found->second;
		a.provenance = e.provenance;
		bool unsupported = !r.fit.placement.geometry ||
			!r.fit.placement.geometry->Report().spatialCoverageComplete;
		bool censored = false, indirect = !r.directOnlyAttested && !e.indirectSeparated;
		if (!r.directOnlyAttested) {
			indirect |= !r.fit.transfer->Report().completePatchSet || !r.fit.transfer->Report().allPairsRepresented;
			for (const auto &m : r.fit.materials) {
				indirect |= m.knowledge != Indirect::MaterialKnowledge::Known;
				indirect |= Peak(m.initial) > 0 && !r.fit.profiles[0].contract.physicalTransportAssumed;
			}
		}
		bool profile = r.comparisonSupported && r.profileSeparated;
		std::set<Id> blocks;
		Bounds positive; positive.known = false;
		for (size_t i : entry.second) {
			const auto &o = rows[i]; a.observations.push_back(o.id);
			if (predictions.empty()) continue; // no numerical residual under unknown profile/domain
			const auto &p = predictions.at(i);
			Need(p.id == o.id, Code::AlgorithmError, "Discovery prediction identity mismatch");
			bool geometry = !unsupported && p.available && !p.visibilityIssues &&
				p.support == ReceiverSupport::RuntimeState::Available;
			unsupported |= !geometry;
			indirect |= !r.directOnlyAttested && (!p.transportAvailable || p.directOnlyConditional ||
				(p.issues & (Indirect::UnknownOptics | Indirect::MovingPath | Indirect::ApproximateGeometry |
				Indirect::UnknownMaterial | Indirect::OmittedLinks | Indirect::MissingPrimary | Indirect::MissingTransport)));
			bool comparable = profile && geometry && o.comparable;
			for (const auto &c : o.rgb) comparable &= c.present && !c.censored && std::isfinite(c.lower) && std::isfinite(c.upper);
			for (const auto &c : o.rgb) censored |= c.censored;
			if (!comparable) continue;
			double deficit = 0;
			for (unsigned c = 0; c < 3; ++c) deficit = std::max(deficit, std::max(0., o.rgb[c].lower - V(p.rgb)[c]) / o.sigma);
			if (deficit > r.options.positiveThreshold) {
				++a.positiveRows; blocks.insert(o.block); a.positiveDiscrepancy += deficit;
				if (!positive.known) { positive.known = true; positive.minWorld = positive.maxWorld = o.receiver.world; }
				else for (unsigned c = 0; c < 3; ++c) {
					Vec3 &lo = positive.minWorld, &hi = positive.maxWorld;
					float *l = c == 0 ? &lo.x : c == 1 ? &lo.y : &lo.z;
					float *h = c == 0 ? &hi.x : c == 1 ? &hi.y : &hi.z;
					*l = std::min(*l, float(V(o.receiver.world)[c])); *h = std::max(*h, float(V(o.receiver.world)[c]));
				}
			}
		}
		a.blocks = blocks.size();
		a.receiverBaselineWorld = positive.known ? Length(V(positive.maxWorld) - V(positive.minWorld)) : 0;
		if (unsupported) { a.classification = Classification::UnsupportedGeometry; a.reason = "Missing/conditional geometry or prediction; never empty space"; }
		else if (!profile) a.reason = "Comparison domain or competing bake profile unresolved; no numerical residual";
		else if (censored) a.reason = "Censored/saturated structure cannot initialize a lamp";
		else if (!e.sunSeparated) { a.classification = Classification::SunAmbiguous; a.reason = "Solar alternatives not independently separated"; }
		else if (indirect) { a.classification = Classification::IndirectPossible; a.reason = "Passive transport/material uncertainty remains; unknown energy is not a source"; }
		else if (e.localDirectCue && a.positiveRows >= r.options.minimumRows && a.blocks >= r.options.minimumBlocks &&
			positive.known && a.receiverBaselineWorld >= r.options.minimumBaselineWorld) {
			a.classification = Classification::CandidateLocalDirect; a.reason = "Coherent positive comparison structure plus independent local direct cue; conditional proposal only";
		} else a.reason = "No coherent supported positive structure with independent local direct evidence";
		result.push_back(std::move(a));
	}
	return result;
}
void Validate(const Request &r, const ReadContext &read) {
	Cancel(read); const auto &o = r.options; const auto &q = r.fit;
	Need(!r.provenance.empty() && r.provenance.size() <= 4096, Code::InvalidInput, "Discovery provenance required");
	Need(o.iterations && o.iterations <= 8 && o.maxCandidates && o.maxCandidates <= 128 &&
		r.candidates.size() <= o.maxCandidates && r.volumes.size() <= 8 && o.fitsPerIteration && o.fitsPerIteration <= 32 &&
		o.proposalsPerIteration && o.proposalsPerIteration <= o.fitsPerIteration &&
		o.maxSources && o.maxSources <= 16 && r.initial.sources.size() <= o.maxSources &&
		o.minimumRows >= 3 && o.minimumBlocks >= 2, Code::WorkLimit, "Discovery frozen search bounds");
	for (double v : {o.cellWorld, o.minimumBaselineWorld, o.positiveThreshold, o.minimumTrainingGain,
		o.minimumValidationGain, o.complexityPerSource, o.minimumPowerFraction}) Need(std::isfinite(v) && v > 0, Code::InvalidInput, "Positive discovery thresholds required");
	Need(o.minimumPowerFraction < 1, Code::InvalidInput, "Invalid minimum contribution fraction");
	Need(std::isfinite(o.maximumValidationBlockRegression) && o.maximumValidationBlockRegression >= 0,
		Code::InvalidInput, "Invalid validation block gate");
	Need(q.profiles.size() == 1 && r.initial.profile == 0 && q.profiles[0].model.law == Shape::Law::DiffuseIntegral &&
		q.profiles[0].contract.transferAttested && q.profiles[0].contract.primariesAttested &&
		(q.profiles[0].contract.operation == Indirect::ObservationOperator::IncidentLinear ||
		 q.profiles[0].contract.operation == Indirect::ObservationOperator::NetRadiantEncoded),
		Code::Unavailable, "One explicit modern relative/forward-encoded comparison hypothesis required");
	Need(!q.options.geometry && !q.options.materials && !q.options.residualSearch && !q.options.mergeSplit &&
		q.options.timeLimitMilliseconds == 0 && q.options.maxObservations <= 4096 &&
		q.options.maxEvaluations <= 200000 && q.options.maxWork <= 2000000000 && q.options.maxRays <= 12000000 &&
		q.options.maxSources >= o.maxSources && !q.diagnostics,
		Code::InvalidInput, "Discovery requires frozen geometry/material policy and deterministic work limits");
	Need(q.transfer && q.options.maxBytes <= 256ull * 1024 * 1024, Code::MemoryLimit, "Bounded discovery evaluator required");
	uint64_t bytes = q.options.maxBytes + uint64_t(q.options.maxObservations) * sizeof(Joint::Observation) * 4 +
		uint64_t(o.fitsPerIteration + 4) * (sizeof(Joint::Model) + o.maxSources * (sizeof(Joint::Source) + 8192));
	Need(!read.maxBytes || bytes <= read.maxBytes, Code::MemoryLimit, "Discovery snapshot/model memory preflight");
	std::set<Id> ids, regions;
	for (const auto &s : r.initial.sources) Need(s.candidate.id && ids.insert(s.candidate.id).second,
		Code::InvalidInput, "Unique initial source identity required");
	for (const auto &e : r.evidence) Need(e.region && regions.insert(e.region).second && !e.provenance.empty() &&
		e.provenance.size() <= 4096, Code::InvalidInput, "Unique independently sourced region evidence required");
	for (const auto &c : r.candidates) {
		Need(c.id && c.region && ids.insert(c.id).second && !c.provenance.empty() && c.provenance.size() <= 4096 &&
			c.root == TransportRootKind::LocalEmitter && c.shape.initial.geometry.source.id == c.id &&
			c.shape.initial.geometry.source.state == HypothesisState::Candidate &&
			c.shape.initial.support.kind != SourceSupport::Kind::LegacyVacuum,
			Code::InvalidInput, "Identified conditional local geometry hypotheses with explicit support required");
	}
}
} // namespace
struct Experiment::Data {
	Request request;
	std::shared_ptr<Rows> training, validation;
	Disk::Digest validationKey{};
	Joint::Model initial, selected;
	Report report;
};
const Report &Experiment::Receipt() const { static const Report empty; return data ? data->report : empty; }
const Joint::Model &Experiment::Initial() const { static const Joint::Model empty; return data ? data->initial : empty; }
const Joint::Model &Experiment::Selected() const { static const Joint::Model empty; return data ? data->selected : empty; }
Error Discover(const Request &input, const std::vector<Joint::Observation> &validation,
	const Disk::Digest &validationKey, const ReadContext &read, Experiment &out) {
	out = {};
	try {
		Validate(input, read);
		Need(validationKey != Disk::Digest{} && validationKey != input.fit.trainingKey, Code::InvalidInput, "Independent validation identity required");
		auto d = std::make_shared<Experiment::Data>(); d->request = input;
		auto &r = d->request; auto &q = r.fit;
		ExpandVolumes(r); Validate(r, read);
		d->training = Snapshot(q, read); q.training = d->training;
		d->validation = std::make_shared<Rows>(); *d->validation->values = validation; d->validationKey = validationKey;
		Inspect(*d->training->values, Joint::Role::Training, q.options.maxObservations);
		Inspect(validation, Joint::Role::Validation, q.options.maxObservations); Disjoint(*d->training->values, validation);
		std::sort(r.candidates.begin(), r.candidates.end(), [](const Joint::Candidate &a, const Joint::Candidate &b) { return a.id < b.id; });
		d->report.provenance = r.provenance; d->report.policy = r.options; d->report.evidence = r.evidence;
		d->report.candidateCount = r.candidates.size();
		d->report.comparisonSupported = r.comparisonSupported; d->report.profileSeparated = r.profileSeparated;
		d->report.directOnlyAttested = r.directOnlyAttested;
		d->selected = r.initial;
		if (!r.comparisonSupported || !r.profileSeparated) {
			Iteration it; it.limits = r.options; it.evaluationLimits = q.options;
			it.provenance = r.provenance;
			it.regions = Classify(r, *d->training->values, {}); it.stop = "UnsupportedComparison";
			d->report.iterations.push_back(it); d->report.stop = it.stop; out.data = d; return {};
		}
		std::set<Id> rejected;
		Joint::Score incumbentValidation;
		for (size_t round = 0; round < r.options.iterations; ++round) {
			Cancel(read); Iteration it; it.index = round; it.limits = r.options; it.evaluationLimits = q.options;
			it.provenance = r.provenance;
			Joint::Work work; Joint::Score training; Joint::Model assessed; std::vector<Joint::Prediction> predictions;
			if (round == 0) {
				// Assess geometry support before optimizing: an unsupported baseline
				// still gets a diagnostic classification, never a replacement lamp.
				++it.calls; Check(Joint::AssessHypothesis(q, d->selected, read, assessed, training, work, &predictions)); Add(it.work, work);
				if (!Supported(training)) {
					it.regions = Classify(r, *d->training->values, predictions); it.stop = "UnsupportedCoverage";
					d->report.stop = it.stop; d->report.iterations.push_back(it); out.data = d; return {};
				}
				++it.calls; auto status = Joint::Refit(q, d->selected, read, d->selected, work); Add(it.work, work); Check(status);
				d->initial = d->selected;
				++it.calls; Check(Joint::EvaluateFrozen(q, d->selected, d->validation, validation.size(), Joint::Role::Validation,
					validationKey, read, incumbentValidation, nullptr, &work)); Add(it.work, work);
			}
			it.incumbent = d->selected.key;
			++it.calls; Check(Joint::AssessHypothesis(q, d->selected, read, assessed, training, work, &predictions)); Add(it.work, work);
			it.regions = Classify(r, *d->training->values, predictions);
			std::set<Id> eligible;
			for (const auto &a : it.regions) if (a.classification == Classification::CandidateLocalDirect) eligible.insert(a.region);
			struct Trial { Joint::Model model; size_t attempt; double gain; };
			std::vector<Trial> trials;
			if (!Supported(training) || !Supported(incumbentValidation)) it.stop = "UnsupportedCoverage";
			else if (d->selected.sources.size() >= r.options.maxSources) it.stop = "SourceLimit";
			else if (eligible.empty()) it.stop = "NoAdmissibleResidual";
			else for (const auto &c : r.candidates) {
				if (!eligible.count(c.region) || Contains(d->selected, c.id) || FamilyOccupied(d->selected, c) || rejected.count(c.id)) continue;
				if (it.attempts.size() == r.options.fitsPerIteration) { it.candidateLimited = true; break; }
				Attempt a; a.candidate = c.id; a.provenance = c.provenance;
				Joint::Model seed = d->selected; Joint::Source source; source.candidate = c; source.value = c.shape.initial; source.environment = c.environment;
				seed.sources.push_back(source);
				std::sort(seed.sources.begin(), seed.sources.end(), [](const Joint::Source &x, const Joint::Source &y) { return x.candidate.id < y.candidate.id; });
				Joint::Model model; ++it.calls;
				auto status = Joint::Refit(q, seed, read, model, work); Add(it.work, work);
				if (!status && (status.code == Code::MissingGeometry || status.code == Code::Unavailable)) a.reason = status.message;
				else {
					Check(status); a.fitted = true;
					a.trainingGain = d->selected.training.Total() - model.training.Total() - r.options.complexityPerSource;
					if (model.occupancyConditional) a.reason = "Conditional source occupancy cannot introduce a new local lamp";
					else if (!NewPowerSupported(model, c, r.options)) a.reason = "New source negligible or at declared power bound; retain unexplained energy";
					else if (a.trainingGain > r.options.minimumTrainingGain) {
						trials.push_back({model, it.attempts.size(), a.trainingGain});
						a.reason = "Training-frozen proposal deferred by ranked validation batch limit";
					}
					else a.reason = "Training improvement does not pay source complexity cost";
				}
				it.attempts.push_back(a);
				if (!a.fitted || a.trainingGain <= r.options.minimumTrainingGain) rejected.insert(c.id);
			}
			// Freeze the bounded training-ranked batch before reading validation values.
			std::stable_sort(trials.begin(), trials.end(), [](const Trial &a, const Trial &b) { return a.gain > b.gain; });
			size_t best = SIZE_MAX; double bestGain = r.options.minimumValidationGain;
			Joint::Score bestValidation;
			for (size_t i = 0; i < std::min(trials.size(), r.options.proposalsPerIteration); ++i) {
				auto &a = it.attempts[trials[i].attempt]; Joint::Score score;
				++it.calls; Check(Joint::EvaluateFrozen(q, trials[i].model, d->validation, validation.size(), Joint::Role::Validation,
					validationKey, read, score, nullptr, &work)); Add(it.work, work); a.validated = true;
				a.validationGain = ValidationObjective(incumbentValidation, d->selected, r.options) - ValidationObjective(score, trials[i].model, r.options);
				bool pass = Supported(score) && score.admissionKey == incumbentValidation.admissionKey &&
					score.requiredBlocksPass && BlocksPass(incumbentValidation, score, r.options.maximumValidationBlockRegression);
				if (pass && a.validationGain > bestGain) { best = i; bestGain = a.validationGain; bestValidation = score; }
				a.reason = !Supported(score) ? "Validation coverage unavailable" :
					score.admissionKey != incumbentValidation.admissionKey ? "Validation denominator changed" :
					!score.requiredBlocksPass ? "Validation required block threshold failed" :
					!BlocksPass(incumbentValidation, score, r.options.maximumValidationBlockRegression) ? "Validation independent block regression" :
					"Frozen validation gain must pay complexity cost and exceed fixed margin";
				if (!pass || a.validationGain <= r.options.minimumValidationGain) rejected.insert(a.candidate);
			}
			if (best != SIZE_MAX) {
				auto &a = it.attempts[trials[best].attempt]; a.accepted = true;
				a.reason = "Accepted conditional candidate after joint refit, independent validation and complexity gates";
				d->selected = trials[best].model; incumbentValidation = bestValidation;
			} else if (it.stop.empty()) it.stop = it.candidateLimited ? "CandidateFitLimit" :
				it.attempts.empty() ? "CandidateCatalogExhausted" : "NoValidatedImprovement";
			it.selected = d->selected.key;
			if (round + 1 == r.options.iterations && it.stop.empty()) it.stop = "IterationLimit";
			bool stopped = !it.stop.empty();
			if (!stopped) it.stop = "ContinueValidatedImprovement";
			d->report.iterations.push_back(it);
			if (stopped) { d->report.stop = it.stop; break; }
		}
		d->report.frozen = true; out.data = d; return {};
	} catch (const Error &e) { return e; }
	catch (const std::bad_alloc &) { return Failure(Code::MemoryLimit, "Discovery allocation"); }
}
Error AuditHeldout(const Experiment &experiment, const std::vector<Joint::Observation> &held,
	const Disk::Digest &partition, const ReadContext &read, Joint::Score &initial, Joint::Score &selected) {
	initial = {}; selected = {};
	try {
		Need(experiment.data && experiment.data->report.frozen, Code::InvalidState, "Frozen discovery required");
		const auto &d = *experiment.data;
		Need(partition != Disk::Digest{} && partition != d.validationKey && partition != d.request.fit.trainingKey,
			Code::InvalidInput, "Independent heldout identity required");
		Inspect(held, Joint::Role::Heldout, d.request.fit.options.maxObservations);
		Disjoint(held, *d.training->values); Disjoint(held, *d.validation->values);
		auto rows = std::make_shared<Rows>(); *rows->values = held;
		Joint::Score a, b;
		Check(Joint::EvaluateFrozen(d.request.fit, d.initial, rows, held.size(), Joint::Role::Heldout, partition, read, a));
		Check(Joint::EvaluateFrozen(d.request.fit, d.selected, rows, held.size(), Joint::Role::Heldout, partition, read, b));
		initial = a; selected = b; return {};
	} catch (const Error &e) { return e; }
	catch (const std::bad_alloc &) { return Failure(Code::MemoryLimit, "Discovery audit allocation"); }
}
const char *Name(Classification c) {
	static const char *names[] = {"Unexplained", "IndirectPossible", "SunAmbiguous", "UnsupportedGeometry", "CandidateLocalDirect"};
	return unsigned(c) < 5 ? names[unsigned(c)] : "Invalid";
}
} }
