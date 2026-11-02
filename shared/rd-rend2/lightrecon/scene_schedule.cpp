// Deterministic cooperative offline scheduling over existing numerical operators.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_schedule.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace SceneSchedule
{
using namespace Bake::Detail;
namespace
{
void Need(bool b, Code c, const char *s)
{
	if (!b)
		throw Failure(c, s);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
Id Number(Disk::Digest d)
{
	Id n = 0;
	for (unsigned i = 0; i < 8; ++i)
		n |= Id(d[i]) << (8 * i);
	return n ? n : 1;
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (auto c : s)
		k.U(uint8_t(c));
}
bool Inside(const Bounds &b, Vec3 p)
{
	for (unsigned c = 0; c < 3; ++c)
		if (V(p)[c] < V(b.minWorld)[c] || V(p)[c] > V(b.maxWorld)[c])
			return false;
	return b.known;
}
bool Touch(const Bounds &a, const Bounds &b)
{
	for (unsigned c = 0; c < 3; ++c)
		if (V(a.maxWorld)[c] < V(b.minWorld)[c] || V(b.maxWorld)[c] < V(a.minWorld)[c])
			return false;
	return true;
}
std::string Quoted(const std::string &s)
{
	std::ostringstream o;
	o << '"';
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
			o << '\\';
		if (c >= 32)
			o << c;
		else
			o << ' ';
	}
	o << '"';
	return o.str();
}
struct Receipt
{
	Disk::Digest key{};
	std::shared_ptr<const Hidden::Result> value;
	Status status = Status::Pending;
};
struct BranchState
{
	Candidates::Catalog catalog;
	Candidates::Cursor cursor[2];
	bool contextDone[2] = {false, false};
	std::set<Id> tried;
	Joint::Result incumbent;
	Joint::Request lastRequest;
	bool prepared = false, done = false;
	size_t waves = 0;
};
struct Data
{
	Disk::Digest key{};
	std::vector<Unit> units;
	std::vector<Receipt> receipts;
	std::vector<BranchState> branches;
	std::vector<BranchResult> reports;
	Work work;
	size_t next = 0, branch = 0;
	bool finished = false, localComplete = true, candidateComplete = true;
	uint64_t elapsedMilliseconds = 0; // OS safety state; excluded from numerical identity/report
	Producer::Result publication;
	size_t preferred = SIZE_MAX;
	std::vector<size_t> equivalentBranches;
	std::vector<Influence> influence;
};
uint64_t Bytes(const Hidden::Result &r)
{
	uint64_t n = sizeof(r) + r.evidence.size() * sizeof(EmitterEvidence) +
				 r.candidates.size() * sizeof(EmitterCandidate) +
				 r.hypotheses.size() * sizeof(Hidden::Hypothesis);
	for (const auto &e : r.evidence)
		n += e.source.size() + e.raw.size() + e.mode.size() + e.name.size() + e.material.size() +
			 e.surfaces.size() * 4 + e.entities.size() * 4;
	for (const auto &c : r.candidates)
		n += (c.evidence.size() + c.sourceIds.size()) * 8;
	for (const auto &h : r.hypotheses)
	{
		n += h.observations.size() * sizeof(Hidden::SupportingObservation) + h.profile.size() +
			 h.profileScope.size();
		for (const auto &s : h.reasons)
			n += s.size();
	}
	return n * 2; // conservative container/scratch allowance, actual RSS measured by controller
}
void Add(Joint::Work &a, const Joint::Work &b)
{
	a.evaluations += b.evaluations;
	a.forwardCalls += b.forwardCalls;
	a.preparationCalls += b.preparationCalls;
	a.cacheHits += b.cacheHits;
	a.rays += b.rays;
	a.traversal += b.traversal;
	a.pairs += b.pairs;
	a.links += b.links;
	a.attemptedModels += b.attemptedModels;
	a.rejectedModels += b.rejectedModels;
	a.arithmetic += b.arithmetic;
	a.linearColumns += b.linearColumns;
	a.proposalsDeferred += b.proposalsDeferred;
	a.accountedBytes = std::max(a.accountedBytes, b.accountedBytes);
}
struct Engine
{
	const Request &r;
	const ReadContext &read;
	Data &d;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	Engine(const Request &a, const ReadContext &b, Data &c) : r(a), read(b), d(c)
	{
	}
	void Limit(uint64_t work = 0)
	{
		Need(!Cancelled(read), Code::Cancelled, "Scene scheduling cancelled");
		d.work.work += work;
		Need(d.work.work <= r.limits.maxWork && d.work.rays <= r.limits.maxRays &&
				 d.work.forwardCalls <= r.limits.maxForwardCalls,
			 Code::WorkLimit, "Scene mandatory accumulated work/ray/forward budget");
		Need(d.work.accountedBytes <= r.limits.maxBytes &&
				 (!read.maxBytes || d.work.accountedBytes <= read.maxBytes),
			 Code::MemoryLimit, "Scene retained receipts/catalog memory");
		Need(!r.limits.wallMilliseconds ||
				 d.elapsedMilliseconds +
						 uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
									  std::chrono::steady_clock::now() - start)
									  .count()) <=
					 r.limits.wallMilliseconds,
			 Code::WorkLimit, "Scene OS wall-time safety limit; not deterministic convergence");
	}
	void Charge(const Joint::Work &w)
	{
		Add(d.work.fitting, w);
		d.work.rays += w.rays;
		d.work.work += w.traversal + w.arithmetic + w.pairs;
		d.work.forwardCalls += w.forwardCalls;
		Limit();
	}
};
Hidden::Request Local(const Request &r, const Unit &u)
{
	auto q = r.discovery;
	q.areas = {u.area};
	q.profiles = {r.branches[u.branch].fitting.profiles[0].model.profile};
	q.options.maxAreas = 1;
	q.options.maxProfiles = 1;
	q.options.maxSamples = r.limits.samplesPerUnit;
	q.options.samplesPerArea = r.limits.samplesPerUnit;
	q.options.maxRays = u.rayBudget;
	q.options.maxWork = u.workBudget;
	q.options.maxBytes = u.byteBudget;
	q.options.maxForwardCalls = std::min(q.options.maxForwardCalls, r.limits.maxForwardCalls);
	auto samples = std::make_shared<std::vector<Hidden::Sample>>();
	std::set<Id> selected(u.training.begin(), u.training.end());
	for (size_t i = 0; i < r.discovery.training.count; ++i)
		if (selected.count(r.discovery.training.data[i].receiver.sample))
			samples->push_back(r.discovery.training.data[i]);
	std::sort(samples->begin(), samples->end(),
			  [](const Hidden::Sample &a, const Hidden::Sample &b) {
				  return a.receiver.sample < b.receiver.sample;
			  });
	q.training = VectorChunk<Hidden::Sample>(samples, 0, samples->size());
	return q;
}
Disk::Digest Identity(const Request &r, std::vector<Unit> &units, const ReadContext &read,
					  bool makeUnits)
{
	Need(!r.branches.empty() && r.branches.size() <= 8 && r.discovery.training.count <= 65536,
		 Code::InvalidInput, "Scene branch/training limits");
	const auto &l = r.limits;
	Need(l.maxUnits && l.maxUnits <= 4096 && l.samplesPerUnit >= 6 && l.samplesPerUnit <= 256 &&
			 l.candidatesPerUnit && l.candidatesPerUnit <= 128 &&
			 l.maxCatalogCandidates >= l.newCandidatesPerWave && l.maxCatalogCandidates <= 4096 &&
			 l.newCandidatesPerWave && l.newCandidatesPerWave <= 32 && l.wavesPerBranch &&
			 l.wavesPerBranch <= 64 && l.influenceSamples && l.influenceSamples <= 16 &&
			 l.maxBytes && l.maxRays && l.maxWork && l.maxForwardCalls,
		 Code::InvalidInput, "Scene finite budgets required");
	Need(!r.discovery.training.count || (r.discovery.training.owner && r.discovery.training.data),
		 Code::InvalidInput, "Scene training lease");
	Need(!read.maxBytes || r.discovery.training.count * sizeof(Hidden::Sample) * 4 <= read.maxBytes,
		 Code::MemoryLimit, "Scene planning input memory");
	std::set<Id> trainingIds;
	for (size_t i = 0; i < r.discovery.training.count; ++i)
	{
		Need(!Cancelled(read), Code::Cancelled, "Scene input validation cancelled");
		const auto &s = r.discovery.training.data[i];
		Need(s.receiver.sample && trainingIds.insert(s.receiver.sample).second &&
				 s.receiver.surface && Finite(s.receiver.world) &&
				 Normal(s.receiver.geometricNormal) && Normal(s.receiver.shadingNormal) &&
				 std::isfinite(s.areaWorldSquared) && s.areaWorldSquared > 0 &&
				 std::isfinite(s.statisticalWeight) && s.statisticalWeight >= 0 &&
				 ((s.correlationGroup && s.statisticalWeight > 0) ||
				  (!s.correlationGroup && s.statisticalWeight == 0)),
			 Code::InvalidInput, "Scene malformed/duplicate training observation");
	}
	std::vector<Hidden::SearchArea> areas;
	Check(Hidden::PlanAreas(r.discovery, l.maxUnits, read, areas));
	Need(areas.size() <= l.maxUnits / r.branches.size(), Code::WorkLimit,
		 "Scene profile-expanded unit limit");
	Need(uint64_t(areas.size()) * r.branches.size() * r.discovery.training.count <= l.maxWork,
		 Code::WorkLimit, "Scene geometry partition planning budget");
	GeometryKey key;
	key.U(kVersion);
	key.U(Hidden::kVersion);
	key.U(Candidates::kVersion);
	key.U(Joint::kVersion);
	for (auto n : {l.maxUnits, l.samplesPerUnit, l.candidatesPerUnit, l.maxCatalogCandidates,
				   l.newCandidatesPerWave, l.wavesPerBranch, l.maxInfluenceLinks,
				   l.influenceSamples, l.maxNeighbourLinks})
		key.U(n);
	key.U(l.maxGILinks);
	key.U(l.maxBytes);
	key.U(l.maxRays);
	key.U(l.maxWork);
	key.U(l.maxForwardCalls);
	key.U(l.wallMilliseconds);
	key.U(l.publish);
	key.U(r.observationPoolComplete);
	auto canonical = r.discovery;
	canonical.areas = areas;
	std::sort(canonical.directions.begin(), canonical.directions.end(),
			  [](const Hidden::DirectionConstraint &a, const Hidden::DirectionConstraint &b) {
				  return a.id < b.id;
			  });
	key.Digest(Hidden::RequestIdentity(canonical));
	std::set<Disk::Digest> branchKeys;
	for (size_t b = 0; b < r.branches.size(); ++b)
	{
		const auto &p = r.branches[b];
		const auto &q = p.fitting;
		Need(q.profiles.size() == 1 && q.mapKey == r.discovery.map.sha256 &&
				 p.map.sha256==q.mapKey && q.transfer && q.transfer->MapKey()==q.mapKey &&
				 q.geometry.geometryKey == r.discovery.geometry.geometryKey &&
				 q.profiles[0].model.units.worldUnitsPerMeter==r.discovery.units.worldUnitsPerMeter,
			 Code::InvalidInput, "Scene branches require one matching profile/geometry scope");
		Need(!p.hidden && !p.solar && q.count && q.training && q.transfer, Code::InvalidInput,
			 "Scene branches must already have independently scoped optional providers");
		Need(q.transfer->Links().size() <= l.maxGILinks, Code::WorkLimit,
			 "Scene passive GI link budget");
		Need(q.count <= q.options.maxObservations &&
				 q.candidates.size() <= q.options.maxCandidates &&
				 q.options.maxSources >= l.newCandidatesPerWave,
			 Code::InvalidInput, "Scene joint input/numerical bounds");
		auto fingerprint = Producer::DependencyFingerprint(p);
		Need(!fingerprint.inputs.empty(), Code::InvalidInput, "Scene branch dependency identity");
		GeometryKey bk;
		for (const auto &i : fingerprint.inputs)
		{
			bk.Digest(i.sha256);
			bk.U(i.bytes);
		}
		Text(bk, p.settingsIdentity);
		for (const auto &v : p.providers)
		{
			bk.Digest(v.generation);
			Text(bk, v.name);
			bk.U(v.candidates.count);
		}
		const auto branchKey = bk.Finish();
		Need(branchKeys.insert(branchKey).second, Code::InvalidInput, "Duplicate scene branch");
		key.Digest(branchKey);
		GeometryKey localScope;
		localScope.Digest(q.profiles[0].model.profile.contentKey);
		localScope.F(q.profiles[0].fixedGain);
		Text(localScope, q.profiles[0].calibrationHypothesis);
		localScope.U(q.profiles[0].contract.physicalTransportAssumed);
		localScope.U(unsigned(q.profiles[0].contract.operation));
		for (const auto &root : q.boundaryRoots)
		{
			localScope.U(root.source);
			localScope.Digest(root.generation);
		}
		for (const auto &source : q.candidates)
			if (source.root != TransportRootKind::LocalEmitter)
			{
				localScope.U(source.id);
				localScope.U(unsigned(source.root));
			}
		const auto scopeKey = localScope.Finish();
		if (!makeUnits)
			continue;
		for (const auto &a : areas)
		{
			Need(!Cancelled(read), Code::Cancelled, "Scene geometry planning cancelled");
			Unit u;
			u.branch = b;
			u.map = q.mapKey;
			u.geometry = q.geometry.geometryKey;
			u.profile = q.profiles[0].model.profile.contentKey;
			u.area = a;
			u.candidateBudget = l.candidatesPerUnit;
			u.rayBudget = r.discovery.options.maxRays;
			u.workBudget = r.discovery.options.maxWork;
			u.byteBudget = std::min(r.discovery.options.maxBytes, l.maxBytes / 2);
			// Surface-balanced geometry-only retention. No luminance decides the shard.
			std::map<Id, std::vector<Id>> surfaces;
			for (size_t i = 0; i < r.discovery.training.count; ++i)
			{
				const auto &s = r.discovery.training.data[i];
				Need(s.receiver.sample && Finite(s.receiver.world), Code::InvalidInput,
					 "Scene training geometry");
				if (s.eligible && Inside(a.receivers, s.receiver.world))
					surfaces[s.receiver.surface].push_back(s.receiver.sample);
			}
			for (auto &v : surfaces)
				std::sort(v.second.begin(), v.second.end());
			for (size_t n = 0; u.training.size() < l.samplesPerUnit; ++n)
			{
				bool more = false;
				for (const auto &v : surfaces)
					if (n < v.second.size())
					{
						u.training.push_back(v.second[n]);
						more = true;
						if (u.training.size() == l.samplesPerUnit)
							break;
					}
				if (!more)
					break;
			}
			std::sort(u.training.begin(), u.training.end());
			Need(std::adjacent_find(u.training.begin(), u.training.end()) == u.training.end(),
				 Code::InvalidInput, "Duplicate scene observation identity");
			// Local identity excludes later joint settings and validation/held-out RGB.
			u.generation = Hidden::RequestIdentity(Local(r, u));
			GeometryKey unitKey;
			unitKey.Digest(u.generation);
			unitKey.Digest(scopeKey);
			u.id = Number(unitKey.Finish());
			units.push_back(std::move(u));
		}
	}
	std::sort(units.begin(), units.end(), [](const Unit &a, const Unit &b) {
		return std::tie(a.branch, a.id) < std::tie(b.branch, b.id);
	});
	return key.Finish();
}
// Compact only selected rows; immutable huge source evidence is not copied into
// every optimization wave. IDs/generation/provenance of the original survive.
Candidates::Provider Compact(const Candidates::Provider &p, const std::vector<size_t> &indices)
{
	auto ev = std::make_shared<std::vector<EmitterEvidence>>();
	auto ca = std::make_shared<std::vector<EmitterCandidate>>();
	auto an = std::make_shared<std::vector<Candidates::Annotation>>();
	std::map<size_t, size_t> remap;
	for (size_t i : indices)
	{
		Need(i < p.candidates.count && i < p.annotations.count, Code::InvalidInput,
			 "Scene provider span");
		auto c = p.candidates.data[i];
		for (auto old : c.evidence)
		{
			Need(old < p.evidence.count, Code::InvalidInput, "Scene evidence reference");
			if (!remap.count(old))
			{
				remap[old] = ev->size();
				ev->push_back(p.evidence.data[old]);
			}
		}
		Need(c.primary < p.evidence.count, Code::InvalidInput, "Scene primary evidence");
		if (!remap.count(c.primary))
		{
			remap[c.primary] = ev->size();
			ev->push_back(p.evidence.data[c.primary]);
		}
		c.primary = remap.at(c.primary);
		for (auto &x : c.evidence)
			x = remap.at(x);
		ca->push_back(std::move(c));
		an->push_back(p.annotations.data[i]);
	}
	auto result = p;
	result.evidence = VectorChunk<EmitterEvidence>(ev, 0, ev->size());
	result.candidates = VectorChunk<EmitterCandidate>(ca, 0, ca->size());
	result.annotations = VectorChunk<Candidates::Annotation>(an, 0, an->size());
	return result;
}
void Catalog(Engine &e, size_t branch)
{
	auto &state = e.d.branches[branch];
	auto &report = e.d.reports[branch];
	const auto &base = e.r.branches[branch];
	Candidates::Request request;
	request.map = base.fitting.mapKey;
	request.geometryKey = base.fitting.geometry.geometryKey;
	request.visibility = base.fitting.geometry.visibility;
	request.limits = base.options.candidates;
	request.regions = base.fitting.placement.regions;
	if (request.regions)
		request.regionKey = request.regions->CertificationKey();
	request.limits.maxCandidates = e.r.limits.maxCatalogCandidates;
	request.limits.maxBytes = std::min(request.limits.maxBytes, e.r.limits.maxBytes / 2);
	Need(e.d.work.accountedBytes + 131072 < e.r.limits.maxBytes, Code::MemoryLimit,
		 "Scene candidate preparation remaining memory");
	request.limits.maxBytes =
		std::min(request.limits.maxBytes, (e.r.limits.maxBytes - e.d.work.accountedBytes) / 2);
	// Keep provider identities independent of completion order/catalog contents.
	auto aggregate = std::make_shared<Hidden::Result>();
	GeometryKey revision;
	revision.U(kVersion);
	revision.Digest(request.map);
	revision.Digest(request.geometryKey);
	Text(revision, "local-hidden-records");
	aggregate->key = revision.Finish();
	std::vector<std::pair<size_t, size_t>> rows;
	for (size_t n = 0; n < e.d.units.size(); ++n)
		if (e.d.units[n].branch == branch && e.d.receipts[n].value)
		{
			const auto &v = *e.d.receipts[n].value;
			report.offered += v.candidates.size();
			std::map<Bake::SourceKind, std::vector<size_t>> classes;
			for (size_t i = 0; i < v.hypotheses.size(); ++i)
				classes[v.hypotheses[i].sourceKind].push_back(i);
			size_t selected = 0;
			for (size_t pass = 0; selected < e.r.limits.candidatesPerUnit; ++pass)
			{
				bool more = false;
				for (const auto &c : classes)
					if (pass < c.second.size())
					{
						rows.push_back({n, c.second[pass]});
						++selected;
						more = true;
						if (selected == e.r.limits.candidatesPerUnit)
							break;
					}
				if (!more)
					break;
			}
			report.deferred += v.candidates.size() - selected;
			e.d.work.candidateInputsDeferred += v.candidates.size() - selected;
		}
	// One offer per unit per pass, preventing first-unit or area-fragment monopoly.
	std::sort(rows.begin(), rows.end(),
			  [&](const std::pair<size_t, size_t> &a, const std::pair<size_t, size_t> &b) {
				  return std::tie(a.second, e.d.units[a.first].id) <
						 std::tie(b.second, e.d.units[b.first].id);
			  });
	size_t room = e.r.limits.maxCatalogCandidates;
	const size_t authoredAllowance = base.providers.empty() ? 0 : room / 2;
	std::vector<Candidates::Provider> supplied = base.providers;
	std::sort(supplied.begin(), supplied.end(),
			  [](const Candidates::Provider &a, const Candidates::Provider &b) {
				  return std::tie(a.name, a.generation) < std::tie(b.name, b.generation);
			  });
	for (const auto &p : supplied)
	{
		Need(p.candidates.count <= 1000000 && p.annotations.count == p.candidates.count &&
				 p.candidates.owner && p.annotations.owner,
			 Code::InvalidInput, "Scene supplied provider limits/leases");
		Need(p.evidence.owner && (!p.evidence.count || p.evidence.data) &&
				 p.candidates.count * 256ull <= e.r.limits.maxBytes - e.d.work.accountedBytes,
			 Code::MemoryLimit, "Scene authored spatial indexing bound");
		std::map<std::tuple<bool, Id, int64_t, int64_t, int64_t, uint32_t>, std::vector<size_t>>
			groups;
		for (size_t i = 0; i < p.candidates.count; ++i)
		{
			e.Limit(1);
			const auto &c = p.candidates.data[i];
			Need(c.primary < p.evidence.count, Code::InvalidInput, "Scene authored primary");
			const auto &v = p.evidence.data[c.primary];
			const auto &a = p.annotations.data[i];
			Need(!v.positionKnown || Finite(v.position), Code::InvalidInput,
				 "Scene authored position");
			const double cell = request.limits.localCellWorld;
			groups[{!a.protectedAuthored, a.regions.empty() ? 0 : a.regions.front(),
					int64_t(std::floor(v.position.x / cell)),
					int64_t(std::floor(v.position.y / cell)),
					int64_t(std::floor(v.position.z / cell)), c.shapes}]
				.push_back(i);
		}
		std::vector<size_t> selected;
		for (auto &g : groups)
			std::sort(g.second.begin(), g.second.end(), [&](size_t a, size_t b) {
				return p.candidates.data[a].id < p.candidates.data[b].id;
			});
		const size_t cap = std::min(
			room, std::max(size_t(1), authoredAllowance / std::max(size_t(1), supplied.size())));
		for (size_t pass = 0; selected.size() < cap; ++pass)
		{
			bool more = false;
			for (const auto &g : groups)
				if (pass < g.second.size())
				{
					selected.push_back(g.second[pass]);
					more = true;
					if (selected.size() == cap)
						break;
				}
			if (!more)
				break;
		}
		report.deferred += p.candidates.count - selected.size();
		e.d.work.candidateInputsDeferred += p.candidates.count - selected.size();
		if (!selected.empty())
		{
			request.providers.push_back(Compact(p, selected));
			room -= selected.size();
		}
	}
	for (const auto &row : rows)
	{
		if (!room)
			break;
		const auto &v = *e.d.receipts[row.first].value;
		auto c = v.candidates.at(row.second);
		auto h = v.hypotheses.at(row.second);
		Need(c.primary < v.evidence.size(), Code::InvalidInput, "Generated source primary");
		std::map<size_t, size_t> remap;
		for (auto old : c.evidence)
		{
			Need(old < v.evidence.size(), Code::InvalidInput, "Generated evidence span");
			if (!remap.count(old))
			{
				remap[old] = aggregate->evidence.size();
				aggregate->evidence.push_back(v.evidence[old]);
			}
		}
		if (!remap.count(c.primary))
		{
			remap[c.primary] = aggregate->evidence.size();
			aggregate->evidence.push_back(v.evidence[c.primary]);
		}
		c.primary = remap[c.primary];
		for (auto &old : c.evidence)
			old = remap[old];
		aggregate->candidates.push_back(std::move(c));
		aggregate->hypotheses.push_back(std::move(h));
		--room;
	}
	report.deferred += rows.size() - aggregate->candidates.size();
	e.d.work.candidateInputsDeferred += rows.size() - aggregate->candidates.size();
	if (!aggregate->candidates.empty())
	{
		Candidates::Provider p;
		Check(Candidates::FromHidden("scene-local-hidden", request.map, request.geometryKey,
									 request.regionKey, aggregate, p));
		request.providers.push_back(p);
	}
	if (!request.providers.empty())
		Check(Candidates::Consolidate(request, e.read, state.catalog));
	report.consolidation = state.catalog.report;
	report.catalogKey = state.catalog.key;
	report.models = state.catalog.models.size();
	// S05 can schedule fragment unions and transport evidence which the current
	// Joint geometry adapter cannot numerically evaluate. Do not spend every
	// progressive wave finishing impossible models while usable lamps starve.
	// This is a capability-filtered view of the canonical catalog, not a merge,
	// source rejection, or loss of its input/provenance diagnostics.
	GeometryKey eligibleKey;
	eligibleKey.Digest(state.catalog.key);
	Text(eligibleKey, "Joint numerical capability view/1");
	std::vector<Candidates::Model> eligible;
	for (const auto &model : state.catalog.models)
	{
		Joint::Candidate candidate;
		if (Producer::MakeCandidate(state.catalog, model, base.fitting, candidate))
		{
			eligibleKey.U(model.id);
			eligible.push_back(model);
		}
		else
		{
			++e.d.work.candidatesUnsupported;
			report.diagnostics.push_back("Model " + std::to_string(model.id) +
										 ": unsupported Joint shape/transport/fragment union; "
										 "evidence retained in canonical input receipts");
		}
	}
	state.catalog.models = std::move(eligible);
	if (!state.catalog.models.empty())
		state.catalog.key = eligibleKey.Finish();
	if (state.catalog.models.size() != report.models)
		e.d.candidateComplete = false;
	e.d.work.rays += state.catalog.report.rays;
	e.d.work.work += state.catalog.report.pairTests + state.catalog.report.rayWork +
					 state.catalog.report.relationWork;
	e.d.work.accountedBytes += state.catalog.report.accountedBytes;
	state.prepared = true;
	if (report.deferred)
		e.d.candidateComplete = false;
	e.Limit();
}
struct Validator final : Joint::Validation
{
	const SceneSchedule::Validation &v;
	const Joint::Request &q;
	Validator(const SceneSchedule::Validation &a, const Joint::Request &b) : v(a), q(b)
	{
	}
	Disk::Digest Key() const override
	{
		return v.Key();
	}
	Error Evaluate(const Joint::Model &m, const ReadContext &r, Joint::Score &s) const override
	{
		return v.Assess(q, m, r, s);
	}
};
double Rank(const Joint::Result &r)
{
	if (r.preferred == SIZE_MAX || r.preferred >= r.alternatives.size())
		return 1e300;
	const auto &m = r.alternatives[r.preferred];
	if (!m.validation.available || !m.validation.requiredBlocksPass)
		return 1e300;
	return m.validation.objective + m.training.complexity + m.training.geometryPrior +
		   m.training.materialPrior;
}
bool Wave(Engine &e, size_t branch, const Validation *validation)
{
	auto &s = e.d.branches[branch];
	auto &report = e.d.reports[branch];
	const auto &base = e.r.branches[branch];
	auto q = base.fitting;
	if (!s.prepared)
		Catalog(e, branch);
	if (!validation)
	{
		report.status = Status::InsufficientValidation;
		s.done = true;
		return false;
	}
	Need(base.validationKey == validation->Key(), Code::InvalidInput,
		 "Scene validation identity mismatch");
	std::map<Id, Joint::Candidate> incoming;
	for (const auto &c : q.candidates)
		incoming.emplace(c.id, c);
	if (s.incumbent.preferred != SIZE_MAX)
	{
		for (const auto &source : s.incumbent.alternatives[s.incumbent.preferred].sources)
		{
			auto c = source.candidate;
			c.initialActive = true;
			c.shape.initial = source.value;
			c.environment = source.environment;
			incoming[c.id] = c;
		}
	}
	Need(incoming.size() + e.r.limits.newCandidatesPerWave <= q.options.maxCandidates,
		 Code::WorkLimit, "Scene incumbent plus proposal wave capacity");
	bool exhausted = s.catalog.models.empty();
	auto cursorBefore = s.cursor[0], profileCursorBefore = s.cursor[1];
	if (!s.catalog.models.empty())
	{
		auto budget = base.options.selection;
		if (s.incumbent.preferred != SIZE_MAX)
		{
			auto groups = s.incumbent.alternatives[s.incumbent.preferred].validation.groups;
			std::sort(groups.begin(), groups.end(),
					  [](const Joint::Group &a, const Joint::Group &b) {
						  return a.squared != b.squared ? a.squared > b.squared : a.block < b.block;
					  });
			for (const auto &g : groups)
			{
				if (budget.more.size() >= 4)
					break;
				if (!g.positive || g.blackSquared <= 0 || g.squared < .5 * g.blackSquared)
					continue;
				Candidates::Demand demand;
				if (g.region)
				{
					demand.home.kind = Candidates::Home::Kind::Region;
					demand.home.region = g.region;
				}
				else
				{
					Bounds bounds;
					if (!validation->BlockBounds(g.block, bounds) || !bounds.known)
						continue;
					demand.home.kind = Candidates::Home::Kind::SpatialCell;
					V centre = (V(bounds.minWorld) + V(bounds.maxWorld)) * .5;
					for (unsigned c = 0; c < 3; ++c)
						demand.home.cell[c] =
							int64_t(std::floor(centre[c] / base.options.candidates.localCellWorld));
				}
				demand.extraPerClass = 1;
				demand.reason = "Frozen validation protocol: positive block residual >= half black "
								"baseline; request bounded remaining hypotheses";
				budget.more.push_back(demand);
				report.diagnostics.push_back(demand.reason);
			}
		}
		const unsigned which = unsigned(s.waves % 2);
		bool has[2] = {false, false};
		for (const auto &m : s.catalog.models)
		{
			if (m.profile.empty())
				has[0] = true;
			if (m.profile == q.profiles[0].model.profile.id)
				has[1] = true;
		}
		for (unsigned c = 0; c < 2; ++c)
			if (!has[c])
				s.contextDone[c] = true;
		const unsigned context = has[which] && !s.contextDone[which] ? which : 1 - which;
		budget.profile = context ? q.profiles[0].model.profile.id : "";
		budget.maxTickets = e.r.limits.newCandidatesPerWave;
		budget.maxGeometryWork = std::max<uint64_t>(budget.maxGeometryWork, budget.maxTickets);
		Candidates::Selection selected;
		Check(Candidates::Select(s.catalog, budget, e.read, s.cursor[context], selected));
		// One context ending does not imply the other profile-neutral stream ended.
		s.contextDone[context] = selected.exhausted;
		exhausted = s.contextDone[0] && s.contextDone[1];
		if (selected.tickets.empty() && !selected.exhausted)
		{
			s.done = true;
			e.d.candidateComplete = false;
			report.status = Status::BudgetLimited;
			report.diagnostics.push_back("S05 local quota/protected/fragment work deferred");
			return false;
		}
		for (const auto &t : selected.tickets)
			if (t.completesModel)
			{
				s.tried.insert(t.model);
				auto model =
					std::find_if(s.catalog.models.begin(), s.catalog.models.end(),
								 [&](const Candidates::Model &m) { return m.id == t.model; });
				Need(model != s.catalog.models.end(), Code::AlgorithmError,
					 "Scheduled catalog model missing");
				Joint::Candidate c;
				if (Producer::MakeCandidate(s.catalog, *model, q, c))
					incoming.emplace(c.id, c);
				else
					++e.d.work.candidatesUnsupported;
			}
	}
	q.candidates.clear();
	for (const auto &c : incoming)
		q.candidates.push_back(c.second);
	++s.waves;
	++e.d.work.waves;
	report.waves = s.waves;
	if (q.candidates.empty() && q.boundaryRoots.empty())
	{
		s.done = exhausted || s.waves >= e.r.limits.wavesPerBranch;
		if (!exhausted && s.done)
			e.d.candidateComplete = false;
		report.status = Status::NoSupportedEvidence;
		return !s.done;
	}
	// Every wave uses the SAME map-wide observations; no home-shard clipping.
	e.Limit();
	Need(e.r.limits.maxRays > e.d.work.rays && e.r.limits.maxWork > e.d.work.work, Code::WorkLimit,
		 "Scene remaining joint budget exhausted");
	q.options.maxRays = std::min(q.options.maxRays, e.r.limits.maxRays - e.d.work.rays);
	q.options.maxWork = std::min(q.options.maxWork, e.r.limits.maxWork - e.d.work.work);
	Need(e.d.work.accountedBytes + 131072 < e.r.limits.maxBytes, Code::MemoryLimit,
		 "Scene joint scratch remaining memory");
	q.options.maxBytes =
		std::min(q.options.maxBytes, (e.r.limits.maxBytes - e.d.work.accountedBytes) / 2);
	q.options.cacheBytes = std::min(q.options.cacheBytes, q.options.maxBytes / 8);
	Validator evaluator(*validation, q);
	Joint::Result fitted;
	++e.d.work.jointCalls;
	auto error = Joint::Solve(q, e.read, fitted, &evaluator);
	e.Charge(fitted.work);
	for (const auto &attempt : fitted.attempts)
	{
		if (report.attempts.size() < 256)
			report.attempts.push_back(attempt);
		else
			++report.omittedAttempts;
	}
	if (!error)
	{
		s.cursor[0] = cursorBefore;
		s.cursor[1] = profileCursorBefore;
		report.fit = fitted;
		report.diagnostics.push_back(error.message);
		report.status = error.code == Code::WorkLimit || error.code == Code::MemoryLimit
							? Status::BudgetLimited
							: Status::Unsupported;
		if (error.code == Code::Cancelled || error.code == Code::InvalidInput ||
			error.code == Code::ProviderError || error.code == Code::AlgorithmError)
			throw error;
	}
	else if (fitted.preferred != SIZE_MAX && Rank(fitted) < Rank(s.incumbent))
	{
		s.incumbent = std::move(fitted);
		s.lastRequest = q;
		report.status = Status::Produced;
	}
	else if (s.incumbent.preferred == SIZE_MAX)
	{
		report.fit = fitted;
		report.status = fitted.state == Joint::State::Unidentifiable ? Status::Unidentifiable
																	 : Status::NoSupportedEvidence;
	}
	if (exhausted || s.waves >= e.r.limits.wavesPerBranch)
	{
		s.done = true;
		if (!exhausted)
		{
			e.d.candidateComplete = false;
			report.deferred += s.catalog.models.size() - s.tried.size();
			report.diagnostics.push_back(
				"Progressive S05 cursor deferred by wave budget; no absence certified");
		}
	}
	if (s.incumbent.preferred != SIZE_MAX)
		report.fit = s.incumbent;
	return !s.done;
}
void InfluenceGraph(Engine &e, const Joint::Request &q, const Joint::Model &m)
{
	for (const auto &s : m.sources)
		for (const auto &unit : e.d.units)
		{
			if (unit.map != q.mapKey)
				continue;
			if (e.d.influence.size() >= e.r.limits.maxInfluenceLinks)
			{
				e.d.work.influenceComplete = false;
				return;
			}
			Influence link;
			link.candidate = s.candidate.id;
			link.unit = unit.id;
			if (s.candidate.root != TransportRootKind::LocalEmitter)
			{
				link.unknown = 1;
				e.d.influence.push_back(link);
				continue;
			}
			size_t count = 0;
			std::set<Id> ids(unit.training.begin(), unit.training.end());
			for (size_t i = 0;
				 i < e.r.discovery.training.count && count < e.r.limits.influenceSamples; ++i)
			{
				const auto &sample = e.r.discovery.training.data[i];
				if (!ids.count(sample.receiver.sample))
					continue;
				e.Limit();
				RayQuery ray;
				ray.startWorld = sample.receiver.world;
				ray.endWorld = s.value.geometry.position.world;
				ray.originSurface = sample.receiver.surface;
				ray.bias.normalKnown = true;
				ray.bias.geometricNormal = sample.receiver.geometricNormal;
				ray.policy.requireKnownFreeOrigin = false;
				ray.maxWork = std::min<uint64_t>(65536, e.r.limits.maxWork - e.d.work.work);
				RayResult hit;
				auto error = q.geometry.visibility->Query(ray, e.read, hit);
				++e.d.work.rays;
				e.Limit(hit.nodeTests + hit.primitiveTests);
				Check(error);
				if (hit.state == VisibilityState::Visible &&
					hit.reliability == RayReliability::Geometric)
					++link.visible;
				else if (hit.state == VisibilityState::Blocked &&
						 hit.reliability == RayReliability::Geometric)
					++link.blocked;
				else
					++link.unknown;
				++count;
			}
			e.d.influence.push_back(link);
		}
	e.d.work.influenceLinks = e.d.influence.size();
}
struct Final final : Producer::FinalEvaluation
{
	const SceneSchedule::FinalEvaluation &v;
	const Joint::Request &q;
	Final(const SceneSchedule::FinalEvaluation &a, const Joint::Request &b) : v(a), q(b)
	{
	}
	Disk::Digest Key() const override
	{
		return v.Key();
	}
	Error Assess(const Joint::Model &m, const ReadContext &r,
				 Producer::FinalReceipt &s) const override
	{
		return v.Assess(q, m, r, s);
	}
};
} // namespace
class Checkpoint
{
  public:
	Data data;
};
class LocalCache
{
  public:
	std::map<Disk::Digest, Receipt> records;
	uint64_t bytes = 0;
};
Error Run(const Request &original, const ReadContext &read, Result &out,
		  const Validation *validation, const FinalEvaluation *final,
		  std::shared_ptr<const Checkpoint> resume, std::shared_ptr<const LocalCache> cache,
		  size_t unitsThisCall)
{
	out = {};
	const auto callStarted = std::chrono::steady_clock::now();
	auto state = std::make_shared<Checkpoint>();
	auto saved = std::make_shared<LocalCache>();
	try
	{
		Need(!original.branches.empty() && original.branches.size() <= 8, Code::InvalidInput,
			 "Scene branch count before settings hashing");
		for (const auto &branch : original.branches)
		{
			Need(branch.contextHypotheses.size() <= 64 && branch.fitting.profiles.size() == 1,
				 Code::InvalidInput, "Scene scoped branch/context limits");
			for (const auto &text : branch.contextHypotheses)
				Need(text.size() <= 4096, Code::InvalidInput, "Scene contextual text limit");
		}
		Request r = original;
		for (auto &b : r.branches)
			std::sort(b.providers.begin(), b.providers.end(),
					  [](const Candidates::Provider &a, const Candidates::Provider &b) {
						  return std::tie(a.name, a.generation) < std::tie(b.name, b.generation);
					  });
		std::sort(r.branches.begin(), r.branches.end(),
				  [](const Producer::Request &a, const Producer::Request &b) {
					  auto x = Producer::DependencyFingerprint(a),
						   y = Producer::DependencyFingerprint(b);
					  return std::tie(a.settingsIdentity, x.inputs.back().sha256) <
							 std::tie(b.settingsIdentity, y.inputs.back().sha256);
				  });
		Need(!Cancelled(read), Code::Cancelled, "Scene scheduling cancelled before planning");
		std::vector<Unit> planned;
		auto key = Identity(r, planned, read, !resume);
		if (resume)
		{
			Need(resume->data.key == key, Code::InvalidState,
				 "Scene continuation input/settings changed");
			state->data = resume->data;
		}
		else
		{
			state->data.key = key;
			state->data.units = std::move(planned);
			state->data.receipts.resize(state->data.units.size());
			state->data.branches.resize(r.branches.size());
			state->data.reports.resize(r.branches.size());
			state->data.work.accountedBytes = state->data.units.size() * 2048 + 1048576;
			state->data.work.work = state->data.units.size() * r.discovery.training.count;
			for (size_t b = 0; b < r.branches.size(); ++b)
			{
				auto &report = state->data.reports[b];
				report.identity =
					Producer::DependencyFingerprint(r.branches[b]).inputs.back().sha256;
				report.profile = r.branches[b].fitting.profiles[0].model.profile.id;
				report.scenario = r.branches[b].settingsIdentity;
			}
		}
		if (cache)
		{
			Need(cache->bytes <= r.limits.maxBytes / 2, Code::MemoryLimit,
				 "Scene borrowed local cache bound");
			*saved = *cache;
			if (!resume)
				state->data.work.accountedBytes += cache->bytes;
		}
		auto &d = state->data;
		Engine e{r, read, d};
		e.Limit();
		if (!resume)
		{
			for (size_t i = 0; i < d.units.size(); ++i)
				for (size_t j = i + 1; j < d.units.size(); ++j)
				{
					e.Limit(1);
					if (d.units[i].branch != d.units[j].branch ||
						!Touch(d.units[i].area.positions, d.units[j].area.receivers))
						continue;
					if (d.work.neighbourLinks >= r.limits.maxNeighbourLinks)
					{
						d.work.neighboursComplete = false;
						continue;
					}
					d.units[i].neighbours.push_back(d.units[j].id);
					d.units[j].neighbours.push_back(d.units[i].id);
					d.work.neighbourLinks += 2;
				}
		}
		size_t issued = 0;
		while (d.next < d.units.size() && issued < unitsThisCall)
		{
			e.Limit();
			auto &u = d.units[d.next];
			auto found = saved->records.find(u.generation);
			Receipt receipt;
			if (found != saved->records.end())
			{
				receipt = found->second;
				u.cached = true;
				++d.work.cacheHits;
			}
			else
			{
				Need(u.rayBudget <= r.limits.maxRays - d.work.rays &&
						 u.workBudget <= r.limits.maxWork - d.work.work &&
						 std::min(r.discovery.options.maxForwardCalls, r.limits.maxForwardCalls) <=
							 r.limits.maxForwardCalls - d.work.forwardCalls,
					 Code::WorkLimit, "Scene cannot reserve next bounded local invocation");
				auto request = Local(r, u);
				auto value = std::make_shared<Hidden::Result>();
				++d.work.localCalls;
				auto error = Hidden::Generate(request, read, *value);
				u.reason = error.message;
				u.rays = value->report.rays;
				u.work = value->report.traversalWork;
				u.forwardCalls = value->report.forwardCalls;
				d.work.rays += u.rays;
				d.work.work += u.work;
				d.work.forwardCalls += u.forwardCalls;
				d.work.candidatesGenerated += value->candidates.size();
				u.searchComplete = error && value->report.complete;
				if (!error && error.code != Code::Unavailable && error.code != Code::WorkLimit &&
					error.code != Code::MemoryLimit)
					throw error;
				u.status =
					error ? (value->candidates.empty()
								 ? Status::NoSupportedEvidence
								 : value->report.complete ? Status::Produced : Status::Partial)
						  : error.code == Code::Unavailable
								? (value->report.outcome == ProviderOutcome::NoSupportedEvidence
									   ? Status::NoSupportedEvidence
									   : Status::Unsupported)
								: Status::BudgetLimited;
				receipt.key = u.generation;
				receipt.value = value;
				receipt.status = u.status;
				const uint64_t bytes = Bytes(*value);
				d.work.accountedBytes += bytes;
				if (u.searchComplete ||
					(u.status == Status::NoSupportedEvidence &&
					 value->report.outcome == ProviderOutcome::NoSupportedEvidence))
				{
					saved->records.emplace(u.generation, receipt);
					saved->bytes += bytes;
				}
			}
			Need(receipt.key == u.generation && receipt.value, Code::InvalidState,
				 "Incomplete/wrong local cache receipt");
			if (u.cached)
			{
				u.status = receipt.status;
				u.searchComplete = receipt.value->report.complete;
				u.reason = "Reused immutable local receipt; status/coverage preserved";
			}
			u.candidates = receipt.value->candidates.size();
			if (!u.searchComplete)
				d.localComplete = false;
			d.receipts[d.next] = std::move(receipt);
			++d.next;
			++issued;
			e.Limit();
		}
		if (d.next == d.units.size() && !d.finished)
		{
			while (d.branch < d.branches.size())
			{
				e.Limit();
				if (d.branches[d.branch].done)
				{
					++d.branch;
					continue;
				}
				auto &active = d.branches[d.branch];
				auto before0 = active.cursor[0], before1 = active.cursor[1];
				const bool done0 = active.contextDone[0], done1 = active.contextDone[1];
				auto tried = active.tried;
				auto waves = active.waves;
				try
				{
					Wave(e, d.branch, validation);
				}
				catch (...)
				{
					active.cursor[0] = before0;
					active.cursor[1] = before1;
					active.contextDone[0] = done0;
					active.contextDone[1] = done1;
					active.tried = std::move(tried);
					active.waves = waves;
					throw;
				}
				if (d.branches[d.branch].done)
					++d.branch;
			}
			for (size_t b = 0; b < d.branches.size(); ++b)
				if (Rank(d.branches[b].incumbent) <
					(d.preferred == SIZE_MAX ? 1e300 : Rank(d.branches[d.preferred].incumbent)))
					d.preferred = b;
			if (d.preferred != SIZE_MAX)
				for (size_t b = 0; b < d.branches.size(); ++b)
					if (Rank(d.branches[b].incumbent) <=
						Rank(d.branches[d.preferred].incumbent) +
							r.branches[d.preferred].fitting.options.equivalentValidationSlack)
						d.equivalentBranches.push_back(b);
			if (d.preferred != SIZE_MAX)
			{
				auto &chosen = d.branches[d.preferred];
				const auto &model = chosen.incumbent.alternatives[chosen.incumbent.preferred];
				InfluenceGraph(e, chosen.lastRequest, model);
				if (r.limits.publish)
				{
					Need(final && final->Key() == r.branches[d.preferred].heldoutKey,
						 Code::Unavailable,
						 "Final held-out evaluator required only after model selection");
					auto publication = r.branches[d.preferred];
					publication.prepared.reset();
					publication.providers.clear();
					publication.hidden.reset();
					publication.solar.reset();
					publication.solarValidation = nullptr;
					publication.fitting = chosen.lastRequest;
					publication.fitting.candidates.clear();
					for (const auto &source : model.sources)
					{
						auto c = source.candidate;
						c.initialActive = true;
						c.shape.initial = source.value;
						c.environment = source.environment;
						publication.fitting.candidates.push_back(c);
					}
					publication.coverage.limitations.push_back(
						"SceneSchedule/1: computational shards are not opaque rooms; map-wide "
						"observations used in every joint wave");
					publication.coverage.limitations.push_back(
						"Scene graph " + Disk::Hex(d.key) +
						"; jobs=" + std::to_string(d.units.size()) + "; locally complete=" +
						std::to_string(d.localComplete) + "; candidate coverage complete=" +
						std::to_string(d.candidateComplete && r.observationPoolComplete));
					for (const auto &u : d.units)
					{
						std::ostringstream line;
						line << "search-unit=" << u.id << " status=" << StatusName(u.status)
							 << " receiver-bounds=" << u.area.receivers.minWorld.x << ','
							 << u.area.receivers.minWorld.y << ',' << u.area.receivers.minWorld.z
							 << ':' << u.area.receivers.maxWorld.x << ','
							 << u.area.receivers.maxWorld.y << ',' << u.area.receivers.maxWorld.z;
						publication.coverage.limitations.push_back(line.str());
					}
					publication.settingsIdentity += "; scene=" + Disk::Hex(d.key);
					for (size_t b : d.equivalentBranches)
						publication.contextHypotheses.push_back(
							"SceneSchedule competing scope: " + d.reports[b].profile + "; " +
							d.reports[b].scenario);
					if (d.equivalentBranches.size() > 1)
						publication.coverage.limitations.push_back(
							"Multiple comparable validation-supported profile/scenario hypotheses "
							"remain; selected branch is conditional, not identified compiler");
					publication.options.uncertainty.maxRays = std::min(
						publication.options.uncertainty.maxRays, r.limits.maxRays - d.work.rays);
					publication.options.uncertainty.maxWork = std::min(
						publication.options.uncertainty.maxWork, r.limits.maxWork - d.work.work);
					publication.fitting.options.maxRays = std::min(
						publication.fitting.options.maxRays, r.limits.maxRays - d.work.rays);
					publication.fitting.options.maxWork = std::min(
						publication.fitting.options.maxWork, r.limits.maxWork - d.work.work);
					Validator control(*validation, publication.fitting);
					Final held(*final, publication.fitting);
					Check(Producer::RebindProviders(r.branches[d.preferred], publication));
					auto error = Producer::Run(publication, read, d.publication, &control, &held);
					e.Charge(d.publication.work.solver);
					e.Charge(d.publication.work.strict);
					e.Charge(d.publication.work.decomposition);
					d.work.rays += d.publication.work.uncertainty.rays;
					d.work.work += d.publication.work.uncertainty.traversal +
								   d.publication.work.uncertainty.arithmetic;
					e.Limit();
					Check(error);
				}
			}
			d.finished = true;
		}
		out.status = !d.finished
						 ? Status::Partial
						 : d.preferred == SIZE_MAX
							   ? Status::NoSupportedEvidence
							   : d.localComplete && d.candidateComplete && r.observationPoolComplete
									 ? Status::CompleteScopedFit
									 : Status::Partial;
		if (d.finished && d.equivalentBranches.size() > 1)
			out.status = Status::Unidentifiable;
		if (d.finished && d.preferred != SIZE_MAX &&
			d.branches[d.preferred].incumbent.state == Joint::State::Unidentifiable)
			out.status = Status::Unidentifiable;
		if (d.finished && !validation)
			out.status = Status::InsufficientValidation;
		out.reason = d.preferred == SIZE_MAX ? "No independently supported combined model; "
											   "unsearched/unavailable light is not zero"
											 : "Conditional scene-wide model; source "
											   "existence/stock compiler identity not certified";
	}
	catch (const Error &error)
	{
		state->data.elapsedMilliseconds +=
			uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::steady_clock::now() - callStarted)
						 .count());
		out.status = error.code == Code::Cancelled
						 ? Status::Cancelled
						 : error.code == Code::WorkLimit || error.code == Code::MemoryLimit
							   ? Status::BudgetLimited
							   : Status::Failed;
		out.reason = error.message;
		out.graphKey = state->data.key;
		out.units = state->data.units;
		out.branches = state->data.reports;
		out.work = state->data.work;
		out.checkpoint = state;
		out.cache = saved;
		return error;
	}
	catch (const std::bad_alloc &)
	{
		out.status = Status::BudgetLimited;
		out.reason = "Scene allocation failure";
		return Failure(Code::MemoryLimit, out.reason);
	}
	const auto &d = state->data;
	state->data.elapsedMilliseconds +=
		uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
					 std::chrono::steady_clock::now() - callStarted)
					 .count());
	out.graphKey = d.key;
	out.units = d.units;
	out.branches = d.reports;
	out.influence = d.influence;
	out.work = d.work;
	out.preferred = d.preferred;
	out.localCoverageComplete =
		d.localComplete && d.next == d.units.size() && original.observationPoolComplete;
	out.candidateCoverageComplete = d.candidateComplete && original.observationPoolComplete;
	out.equivalentBranches = d.equivalentBranches;
	out.publication = d.publication;
	out.finalEvaluated = d.publication.final.score.available;
	out.checkpoint = state;
	out.cache = saved;
	return {};
}
const char *StatusName(Status s)
{
	static const char *n[] = {"Pending",
							  "Produced",
							  "NoSupportedEvidence",
							  "Partial",
							  "BudgetLimited",
							  "Unsupported",
							  "InsufficientValidation",
							  "Unidentifiable",
							  "CompleteScopedFit",
							  "Cancelled",
							  "Failed"};
	return unsigned(s) < 11 ? n[unsigned(s)] : "Invalid";
}
std::string ReportJson(const Result &r)
{
	std::ostringstream o;
	o << std::setprecision(12)
	  << "{\"schema\":\"org.openjk.scene-schedule.v1\",\"status\":" << Quoted(StatusName(r.status))
	  << ",\"reason\":" << Quoted(r.reason) << ",\"graph_key\":" << Quoted(Disk::Hex(r.graphKey))
	  << ",\"units\":[";
	for (size_t i = 0; i < r.units.size(); ++i)
	{
		const auto &u = r.units[i];
		o << (i ? "," : "") << "{\"id\":" << u.id
		  << ",\"generation\":" << Quoted(Disk::Hex(u.generation)) << ",\"branch\":" << u.branch
		  << ",\"status\":" << Quoted(StatusName(u.status)) << ",\"training\":" << u.training.size()
		  << ",\"candidates\":" << u.candidates
		  << ",\"complete\":" << (u.searchComplete ? "true" : "false")
		  << ",\"cached\":" << (u.cached ? "true" : "false") << ",\"rays\":" << u.rays
		  << ",\"work\":" << u.work << ",\"reason\":" << Quoted(u.reason) << '}';
	}
	o << "],\"branches\":[";
	for (size_t i = 0; i < r.branches.size(); ++i)
	{
		const auto &b = r.branches[i];
		o << (i ? "," : "") << "{\"profile\":" << Quoted(b.profile)
		  << ",\"status\":" << Quoted(StatusName(b.status)) << ",\"catalog_models\":" << b.models
		  << ",\"deferred\":" << b.deferred << ",\"waves\":" << b.waves
		  << ",\"joint_state\":" << Quoted(Joint::StateName(b.fit.state))
		  << ",\"preferred\":" << (b.fit.preferred == SIZE_MAX ? -1 : int(b.fit.preferred));
		if (b.fit.preferred != SIZE_MAX)
		{
			const auto &m = b.fit.alternatives[b.fit.preferred];
			o << ",\"sources\":" << m.sources.size()
			  << ",\"validation_mse\":" << m.validation.intervalMse
			  << ",\"black_mse\":" << m.validation.blackMse;
		}
		o << ",\"diagnostics\":[";
		for (size_t j = 0; j < b.diagnostics.size(); ++j)
			o << (j ? "," : "") << Quoted(b.diagnostics[j]);
		o << "],\"attempts\":[";
		for (size_t j = 0; j < b.attempts.size(); ++j)
		{
			const auto &a = b.attempts[j];
			o << (j ? "," : "") << "{\"proposal\":" << Quoted(a.proposal)
			  << ",\"status\":" << Quoted(Joint::StateName(a.state))
			  << ",\"reason\":" << Quoted(a.reason) << ",\"sources\":" << a.sources << '}';
		}
		o << "],\"omitted_attempts\":" << b.omittedAttempts << '}';
	}
	o << "],\"equivalent_branches\":[";
	for (size_t i = 0; i < r.equivalentBranches.size(); ++i)
		o << (i ? "," : "") << r.equivalentBranches[i];
	o << "],\"influence\":[";
	for (size_t i = 0; i < r.influence.size(); ++i)
	{
		const auto &v = r.influence[i];
		o << (i ? "," : "") << "{\"source\":" << v.candidate << ",\"unit\":" << v.unit
		  << ",\"visible\":" << v.visible << ",\"blocked\":" << v.blocked
		  << ",\"unknown\":" << v.unknown << '}';
	}
	o << "],\"local_calls\":" << r.work.localCalls << ",\"cache_hits\":" << r.work.cacheHits
	  << ",\"joint_calls\":" << r.work.jointCalls << ",\"rays\":" << r.work.rays
	  << ",\"work\":" << r.work.work << ",\"forward_calls\":" << r.work.forwardCalls
	  << ",\"candidate_inputs_deferred\":" << r.work.candidateInputsDeferred
	  << ",\"accounted_bytes\":" << r.work.accountedBytes
	  << ",\"influence_links\":" << r.influence.size()
	  << ",\"influence_complete\":" << (r.work.influenceComplete ? "true" : "false")
	  << ",\"candidates_unsupported\":" << r.work.candidatesUnsupported
	  << ",\"fitting_evaluations\":" << r.work.fitting.evaluations
	  << ",\"fitting_models\":" << r.work.fitting.attemptedModels
	  << ",\"local_coverage_complete\":" << (r.localCoverageComplete ? "true" : "false")
	  << ",\"candidate_coverage_complete\":" << (r.candidateCoverageComplete ? "true" : "false")
	  << ",\"final_evaluated\":" << (r.finalEvaluated ? "true" : "false")
	  << ",\"stock_source_truth\":null}";
	return o.str();
}
} // namespace SceneSchedule
} // namespace Core
} // namespace Rend2LightRecon
