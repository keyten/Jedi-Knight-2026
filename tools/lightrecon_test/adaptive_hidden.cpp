// Bounded, source-answer-free volumes; scoring/admission reuse Hidden::Generate.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "adaptive_hidden.h"
#include "lightrecon/bake_math.h"
#include "lightrecon/geometry_key.h"
#include <map>
#include <set>
#include <tuple>

namespace AdaptiveHidden
{
using namespace Rend2LightRecon;
using namespace Bake::Detail;
namespace H = Hidden;
namespace
{
void Need(bool value, Code code, const char *message)
{
	if (!value) throw Failure(code, message);
}
bool Inside(const Bounds &b, V p)
{
	for (int k = 0; k < 3; ++k)
		if (p[k] < V(b.minWorld)[k] || p[k] > V(b.maxWorld)[k]) return false;
	return b.known;
}
bool Valid(const Bounds &b)
{
	if (!b.known || !Finite(b.minWorld) || !Finite(b.maxWorld)) return false;
	for (int k = 0; k < 3; ++k)
		if (V(b.maxWorld)[k] <= V(b.minWorld)[k]) return false;
	return true;
}
Bounds Box(V lo, V hi)
{
	Bounds b; b.known = true; b.minWorld = lo.Float(); b.maxWorld = hi.Float(); return b;
}
void Text(GeometryKey &key, const std::string &s)
{
	key.U(s.size()); for (unsigned char c : s) key.U(c);
}
Id Number(const Disk::Digest &d)
{
	Id id = 0; for (size_t i = 0; i < sizeof(id); ++i) id = (id << 8) | d[i]; return id ? id : 1;
}
struct Cell
{
	Bounds bounds;
	std::string reason;
	std::vector<Id> ids;
};
struct Feature
{
	V origin, normal;
	double strength = 0;
	Id surface = 0, peak = 0;
	unsigned channel = 0;
	std::vector<Id> ids;
};
struct Entry
{
	H::Hypothesis h;
	EmitterEvidence evidence;
	Cell cell;
};
bool Eligible(const H::Sample &s, const H::Request &r)
{
	return s.eligible && (s.encodingAttested || r.options.allowAssumedEncoding) &&
		   s.rgb[0] != 255 && s.rgb[1] != 255 && s.rgb[2] != 255;
}
std::vector<Feature> Features(const H::Request &r, const H::SearchArea &area,
							const Options &o, Audit &audit)
{
	std::map<std::pair<Id, Id>, std::vector<const H::Sample *>> groups;
	std::set<Id> correlations;
	std::vector<const H::Sample *> sorted;
	for (size_t i = 0; i < r.training.count; ++i) sorted.push_back(r.training.data + i);
	std::sort(sorted.begin(), sorted.end(), [](const H::Sample *a, const H::Sample *b) {
		return a->receiver.sample < b->receiver.sample;
	});
	for (auto s : sorted)
		if (Eligible(*s, r) && Inside(area.receivers, V(s->receiver.world)) &&
			(!s->correlationGroup || correlations.insert(s->correlationGroup).second))
			groups[{s->receiver.surface, s->receiver.material}].push_back(s);
	std::vector<Feature> result;
	for (const auto &group : groups)
	{
		const auto &rows = group.second;
		if (rows.size() < 6) { ++audit.unsupportedFeatures; continue; }
		V normal(rows.front()->receiver.geometricNormal), origin(rows.front()->receiver.world);
		bool planar = std::abs(Length(normal) - 1) < .001;
		for (auto s : rows)
		{
			++audit.featureWork;
			planar &= Dot(normal, V(s->receiver.geometricNormal)) >= .999 &&
				std::abs(Dot(V(s->receiver.world) - origin, normal)) <= .1;
		}
		if (!planar) { ++audit.unsupportedFeatures; continue; }
		// IDF-inspired support gate only: same material, planar normal, contrast,
		// multiple local samples, no censoring. No depth from code magnitude.
		for (unsigned channel = 0; channel < 3; ++channel)
		{
			auto peak = *std::max_element(rows.begin(), rows.end(), [channel](const H::Sample *a, const H::Sample *b) {
				return a->rgb[channel] != b->rgb[channel] ? a->rgb[channel] < b->rgb[channel] :
					a->receiver.sample > b->receiver.sample;
			});
			double low = 255;
			for (auto s : rows) low = std::min(low, double(s->rgb[channel]));
			double contrast = (peak->rgb[channel] - low) / (peak->rgb[channel] + 1.);
			if (peak->rgb[channel] < 8 || contrast < r.options.minimumContrast) continue;
			std::vector<const H::Sample *> local = rows;
			std::sort(local.begin(), local.end(), [&](const H::Sample *a, const H::Sample *b) {
				double da = Length(V(a->receiver.world) - V(peak->receiver.world));
				double db = Length(V(b->receiver.world) - V(peak->receiver.world));
				return da != db ? da < db : a->receiver.sample < b->receiver.sample;
			});
			Feature f; f.normal = normal; f.surface = peak->receiver.surface;
			f.peak = peak->receiver.sample; f.channel = channel; f.strength = contrast;
			double total = 0;
			for (size_t i = 0; i < std::min(size_t(6), local.size()); ++i)
			{
				++audit.featureWork;
				double w = std::max(0., local[i]->rgb[channel] - low);
				f.origin = f.origin + V(local[i]->receiver.world) * w; total += w;
				f.ids.push_back(local[i]->receiver.sample);
			}
			if (total <= 0) continue;
			f.origin = f.origin / total;
			result.push_back(f);
		}
	}
	std::sort(result.begin(), result.end(), [](const Feature &a, const Feature &b) {
		return a.strength != b.strength ? a.strength > b.strength :
			std::tie(a.surface, a.channel, a.peak) < std::tie(b.surface, b.channel, b.peak);
	});
	std::vector<Feature> unique;
	for (const auto &f : result)
	{
		bool duplicate = false;
		for (const auto &old : unique)
			duplicate |= Dot(old.normal, f.normal) > .999 && Length(old.origin - f.origin) < 24;
		if (!duplicate) unique.push_back(f);
		if (unique.size() == o.maxFeatures) break;
	}
	return unique;
}
bool RayVolume(const H::SearchArea &area, V origin, V direction, const Options &o, Cell &cell, double angularSlack = 0)
{
	if (Length(direction) < .99 || Length(direction) > 1.01) return false;
	double near = o.minimumCellWorld, far = o.maximumAreaExtentWorld * 2;
	for (int k = 0; k < 3; ++k)
	{
		if (std::abs(direction[k]) < 1e-8)
		{
			if (origin[k] < V(area.positions.minWorld)[k] || origin[k] > V(area.positions.maxWorld)[k]) return false;
			continue;
		}
		double a = (V(area.positions.minWorld)[k] - origin[k]) / direction[k];
		double b = (V(area.positions.maxWorld)[k] - origin[k]) / direction[k];
		near = std::max(near, std::min(a, b)); far = std::min(far, std::max(a, b));
	}
	if (far - near < o.minimumCellWorld * 2) return false;
	V a = origin + direction * near, b = origin + direction * far, lo, hi;
	V extent = V(area.positions.maxWorld) - V(area.positions.minWorld);
	for (int k = 0; k < 3; ++k)
	{
		// Broad normal depth, bounded tangential support. No photometric height.
		double band = std::max(o.minimumCellWorld * 2, std::min(64., extent[k] / 8));
		if (angularSlack > 0)
			band = std::max(band, angularSlack >= Pi/2 ? o.maximumAreaExtentWorld : far*std::tan(angularSlack));
		lo[k] = std::max(V(area.positions.minWorld)[k], std::min(a[k], b[k]) - band);
		hi[k] = std::min(V(area.positions.maxWorld)[k], std::max(a[k], b[k]) + band);
	}
	cell.bounds = Box(lo, hi); return Valid(cell.bounds);
}
double Rank(const Entry &e, unsigned channel)
{
	return e.h.channelScores[channel].known && e.h.feasibility == H::Feasibility::ProvenFree &&
		   e.h.visible >= 4 ? e.h.channelScores[channel].value : 1e10;
}
void Append(H::Result &out, Entry e)
{
	for (auto &old : out.hypotheses)
		if (old.candidate == e.h.candidate)
		{
			old.evidenceChannels |= e.h.evidenceChannels;
			for (unsigned c = 0; c < 3; ++c)
				if (e.h.channelScores[c].known) old.channelScores[c] = e.h.channelScores[c];
			return;
		}
	EmitterCandidate c;
	c.id = e.h.candidate; c.primary = out.evidence.size(); c.evidence = {c.primary};
	c.sourceIds = {c.id}; c.shapes = e.evidence.shapes; c.issues = e.evidence.issues;
	c.staticEligible = true;
	out.evidence.push_back(std::move(e.evidence)); out.candidates.push_back(c);
	out.hypotheses.push_back(std::move(e.h));
}
} // namespace

Error Generate(const H::Request &r, const Options &o, const ReadContext &read, H::Result &out, Audit &audit)
{
	out = H::Result(); audit = Audit();
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Adaptive search cancelled");
		Need(r.map.sha256 != Disk::Digest{} && r.geometry.geometryKey != Disk::Digest{} &&
			 r.geometry.visibility != nullptr && (!r.domain || r.domainKey != Disk::Digest{}),
			 Code::MissingGeometry, "Adaptive content-addressed geometry/classifier required");
		Need(!r.areas.empty() && r.areas.size() <= 32 && r.training.count <= 512 &&
			 r.training.owner && r.training.data && r.options.samplesPerArea <= 64 &&
			 r.options.samplesPerArea >= 6 && r.options.refinementRounds <= 4 &&
			 r.options.maxNodesPerArea >= 8 && r.options.maxNodesPerArea <= 1024 &&
			 r.options.retainedPerChannelProfile >= 1 && r.options.retainedPerChannelProfile <= 16 &&
			 r.options.refineSeeds >= 1 && r.options.refineSeeds <= 8 &&
			 r.profiles.size() > 0 && r.profiles.size() <= 4 && r.directions.size() <= 32 &&
			 o.maxFeatures > 0 && o.maxFeatures <= 16 && o.maxBatches > 0 && o.maxBatches <= 128 &&
			 std::isfinite(o.maximumAreaExtentWorld) && o.maximumAreaExtentWorld > 0 && o.maximumAreaExtentWorld <= 4096 &&
			 std::isfinite(o.minimumCellWorld) && o.minimumCellWorld >= .01 &&
			 o.minimumCellWorld <= o.maximumAreaExtentWorld && Valid(r.analysisBounds),
			 Code::InvalidInput, "Adaptive bounded input policy; explicit local unanchored areas required");
		Need(r.options.maxRays > 0 && r.options.maxRays <= 2000000 && r.options.maxForwardCalls > 0 &&
			 r.options.maxForwardCalls <= 100000 && r.options.maxWork > 0 && r.options.maxWork <= 1000000000,
			 Code::InvalidInput, "Adaptive hard ray/evaluation/traversal bounds");
		for (const auto &d : r.directions)
			Need(d.id && d.provenance.size() > 0 && d.provenance.size() <= 2048 && Finite(d.originWorld) &&
				 (!d.usable || (Finite(d.toSource) && std::abs(Length(V(d.toSource)) - 1) < .001 &&
				 std::isfinite(d.halfAngleRadians) && d.halfAngleRadians > 0 && d.halfAngleRadians <= Pi)),
				 Code::InvalidInput, "Adaptive incoming-direction evidence invalid");
		std::vector<H::SearchArea> areas;
		auto planned = H::PlanAreas(r, 32, read, areas); if (!planned) throw planned;
		std::set<Id> explained(o.explainedAreas.begin(), o.explainedAreas.end());
		Need(explained.size() == o.explainedAreas.size() && explained.size() <= areas.size(), Code::InvalidInput, "Adaptive fixture exclusion identities");
		for (Id id : explained)
			Need(std::any_of(areas.begin(), areas.end(), [id](const H::SearchArea &a) { return a.id == id; }),
				 Code::InvalidInput, "Explained area not in request");
		GeometryKey key; key.Digest(H::RequestIdentity(r)); Text(key, "ASR16 adaptive-local-v1");
		key.U(o.maxFeatures); key.U(o.maxBatches); key.F(o.maximumAreaExtentWorld); key.F(o.minimumCellWorld);
		for (Id id : explained) key.U(id);
		out.key = key.Finish();
		const uint64_t scratch = uint64_t(r.options.maxNodesPerArea) * r.profiles.size() *
			(r.options.spots ? 3 : 1) * (sizeof(Entry) + 8192 + r.options.samplesPerArea * sizeof(H::SupportingObservation)) +
			r.training.count * sizeof(H::Sample) * 8 + o.maxBatches * 8192;
		Need(scratch < r.options.maxBytes && (!read.maxBytes || scratch < read.maxBytes), Code::MemoryLimit, "Adaptive accounted scratch bound");
		out.report.accountedPeakBytes = scratch;
		for (const auto &area : areas)
		{
			Need(!Cancelled(read), Code::Cancelled, "Adaptive search cancelled");
			for (int k = 0; k < 3; ++k)
				Need(V(area.positions.maxWorld)[k] - V(area.positions.minWorld)[k] <= o.maximumAreaExtentWorld,
					 Code::InvalidInput, "Adaptive area exceeds bounded local extent");
			if (explained.count(area.id)) { ++audit.explainedSkipped; continue; }
			std::vector<Cell> queue;
			// Eight fallback positions, not a global free-cell enumeration.
			Cell root; root.bounds = area.positions; root.reason = "Local geometry coverage fallback; fixture absence conditional";
			queue.push_back(root);
			for (const auto &d : r.directions)
				if (d.usable && Inside(area.receivers, V(d.originWorld)))
				{
					Cell cell; cell.reason = "Source-direction ray: " + d.provenance + "; mixture half-angle=" + std::to_string(d.halfAngleRadians);
					cell.ids = {d.id};
					if (RayVolume(area, V(d.originWorld), V(d.toSource), o, cell, d.halfAngleRadians)) queue.push_back(cell);
				}
			for (const auto &f : Features(r, area, o, audit))
			{
				Cell cell; cell.reason = "Coherent encoded lightmap contrast; same-material planar IDF-inspired support gate; no depth from RGB8";
				cell.ids = f.ids;
				if (RayVolume(area, f.origin, f.normal, o, cell)) queue.push_back(cell);
			}
			// Reserve half of the pose tickets for local refinement. Every batch
			// uses the exact historical scorer on a two-by-two-by-two local volume.
			const size_t initial = std::max(size_t(1), r.options.maxNodesPerArea / 16);
			if (queue.size() > initial) queue.resize(initial);
			std::vector<Entry> pool;
			std::set<std::tuple<float, float, float, float, float, float>> seen;
			H::AreaReport areaReport; areaReport.id = area.id;
			for (uint32_t round = 0; round <= r.options.refinementRounds; ++round)
			{
				for (const auto &cell : queue)
				{
					Need(!Cancelled(read), Code::Cancelled, "Adaptive search cancelled");
					const auto &lo = cell.bounds.minWorld; const auto &hi = cell.bounds.maxWorld;
					if (!seen.emplace(lo.x, lo.y, lo.z, hi.x, hi.y, hi.z).second) continue;
					uint64_t rays = 8ull * r.options.samplesPerArea * r.profiles.size() * (r.options.spots ? 3 : 1);
					uint64_t calls = 8ull * r.profiles.size() * (r.options.spots ? 3 : 1) * 3;
					if (areaReport.nodes + 8 > r.options.maxNodesPerArea || audit.batches.size() == o.maxBatches ||
						out.report.rays + rays > r.options.maxRays || out.report.forwardCalls + calls > r.options.maxForwardCalls ||
						out.report.traversalWork >= r.options.maxWork)
					{ audit.budgetLimited = true; break; }
					H::Request q = r; q.areas = {area}; q.areas[0].positions = cell.bounds;
					q.areas[0].reason = cell.reason;
					q.directions.clear(); // Direction input seeds volumes, not quadratic cross-products inside each batch.
					q.options.coarseDivisions = 2; q.options.maxNodesPerArea = 8; q.options.refinementRounds = 0;
					q.options.directOnlyAttested = false; // Proposals always retain source-versus-transport ambiguity.
					q.options.maxRays = r.options.maxRays - out.report.rays;
					q.options.maxForwardCalls = r.options.maxForwardCalls - out.report.forwardCalls;
					q.options.maxWork = r.options.maxWork - out.report.traversalWork;
					q.options.maxBytes = r.options.maxBytes - scratch;
					ReadContext localRead = read;
					localRead.maxBytes = read.maxBytes ? std::min(read.maxBytes - scratch, q.options.maxBytes) : q.options.maxBytes;
					H::Result local;
					auto status = H::Generate(q, localRead, local);
					out.report.accountedPeakBytes = std::max(out.report.accountedPeakBytes, scratch + local.report.accountedPeakBytes);
					out.report.rays += local.report.rays; out.report.forwardCalls += local.report.forwardCalls;
					out.report.sourceReceiverPairs += local.report.sourceReceiverPairs; out.report.traversalWork += local.report.traversalWork;
					if (!status && status.code != Code::Unavailable) throw status;
					if (!status) out.report.diagnostics.push_back("Area " + std::to_string(area.id) + ": " + status.message);
					Batch batch; batch.area = area.id; batch.positions = cell.bounds; batch.reason = cell.reason; batch.observations = cell.ids;
					batch.retained = local.hypotheses.size();
					for (size_t i = 0; i < r.training.count; ++i)
						if (Eligible(r.training.data[i], r) && Inside(area.receivers, V(r.training.data[i].receiver.world)) &&
							std::max(r.training.data[i].rgb[0], std::max(r.training.data[i].rgb[1], r.training.data[i].rgb[2])) <= 8)
							++batch.darkReceivers;
					for (const auto &a : local.report.areas)
					{
						batch.nodes += a.nodes; areaReport.nodes += a.nodes; areaReport.rejectedSolid += a.rejectedSolid;
						areaReport.rejectedOutside += a.rejectedOutside; areaReport.unknownRegion += a.unknownRegion;
						areaReport.unknownVisibility += a.unknownVisibility; areaReport.pointTrials += a.pointTrials; areaReport.spotTrials += a.spotTrials;
						areaReport.selected = a.selected; areaReport.requested = a.requested;
					}
					for (size_t i = 0; i < local.hypotheses.size(); ++i)
					{
						Entry e; e.h = local.hypotheses[i]; e.evidence = local.evidence[local.candidates[i].primary];
						e.cell.bounds = e.h.searchedCell; e.cell.reason = "Adaptive coarse-to-fine child of " + cell.reason;
						e.cell.ids = cell.ids;
						GeometryKey id; id.Digest(out.key); id.U(area.id); id.V(e.evidence.position);
						id.U(uint64_t(e.h.sourceKind)); id.V(e.evidence.outward); id.F(e.h.spotRadiusWorld); Text(id, e.h.profile);
						e.h.candidate = Number(id.Finish()); e.evidence.id = e.h.candidate;
						e.evidence.source = "asr16-adaptive:" + Disk::Hex(r.map.sha256);
						e.evidence.mode = "adaptive-local-hidden-hypothesis";
						e.evidence.raw += "; ASR16 seed=" + cell.reason;
						e.h.reasons.push_back("ASR16: " + cell.reason);
						e.h.reasons.push_back("Known dark and occluded receivers remain in the paired native encoded-shape comparison; unknown rows excluded, never black");
						for (const auto &relation : e.h.observations)
							if (relation.relation == H::Relation::Blocked) ++batch.blockedBrightRelations;
						pool.push_back(std::move(e));
					}
					audit.batches.push_back(std::move(batch));
				}
				if (round == r.options.refinementRounds || audit.budgetLimited) break;
				queue.clear();
				std::set<Id> seeds;
				// Kind/channel/profile tickets prevent a broad white point from
				// consuming all refinement around a narrow directional structure.
				for (unsigned c = 0; c < 3 && queue.size() < r.options.refineSeeds; ++c)
					for (unsigned kind = 0; kind < (r.options.spots ? 2u : 1u) && queue.size() < r.options.refineSeeds; ++kind)
						for (const auto &profile : r.profiles)
						{
							const Entry *best = nullptr;
							for (const auto &e : pool)
								if (e.h.sourceKind == (kind ? Bake::SourceKind::Spot : Bake::SourceKind::Point) && e.h.profile == profile.id &&
									!seeds.count(e.h.candidate) && Rank(e, c) < 1e9 && (!best || Rank(e, c) < Rank(*best, c) ||
									(Rank(e, c) == Rank(*best, c) && e.h.candidate < best->h.candidate))) best = &e;
							if (!best) continue;
							V extent = V(best->cell.bounds.maxWorld) - V(best->cell.bounds.minWorld);
							if (std::max({extent.x, extent.y, extent.z}) <= o.minimumCellWorld) continue;
							seeds.insert(best->h.candidate); queue.push_back(best->cell);
							if (queue.size() == r.options.refineSeeds) break;
							}
			}
			for (unsigned channel = 0; channel < 3; ++channel)
				for (unsigned kind = 0; kind < (r.options.spots ? 2u : 1u); ++kind)
					for (const auto &profile : r.profiles)
					{
						std::vector<const Entry *> order;
						for (const auto &e : pool)
							if (e.h.profile == profile.id && e.h.sourceKind == (kind ? Bake::SourceKind::Spot : Bake::SourceKind::Point)) order.push_back(&e);
						std::sort(order.begin(), order.end(), [channel](const Entry *a, const Entry *b) {
							return Rank(*a, channel) != Rank(*b, channel) ? Rank(*a, channel) < Rank(*b, channel) : a->h.candidate < b->h.candidate;
						});
						std::vector<V> positions;
						for (const auto *e : order)
						{
							if (positions.size() == r.options.retainedPerChannelProfile) break;
							if (Rank(*e, channel) >= 1e9 || Rank(*e, channel) > Rank(*order.front(), channel) + r.options.alternativeScoreDelta) continue;
							bool duplicate = false;
							for (auto p : positions) duplicate |= Length(p - V(e->evidence.position)) < o.minimumCellWorld;
							if (duplicate) continue;
							Append(out, *e); positions.push_back(V(e->evidence.position));
						}
					}
			areaReport.retained = std::count_if(out.hypotheses.begin(), out.hypotheses.end(),
				[&](const H::Hypothesis &h) { return h.area == area.id; }); areaReport.searchLimited = true;
			areaReport.admission = "Adaptive explicit local volume; source-versus-transport unresolved";
			out.report.areas.push_back(areaReport);
		}
		out.report.complete = false;
		out.report.outcome = out.candidates.empty() ? ProviderOutcome::NoSupportedEvidence : ProviderOutcome::Partial;
		out.report.diagnostics.push_back("ASR16 bounded adaptive local volumes; not exhaustive map coverage or historical source certification");
		return {};
	}
	catch (const Error &error)
	{
		auto report = out.report; out = H::Result(); out.report = report; out.report.complete = false;
		out.report.diagnostics.push_back(error.message); return error;
	}
	catch (const std::bad_alloc &)
	{
		out = H::Result(); return Failure(Code::MemoryLimit, "Adaptive allocation failed");
	}
}
} // namespace AdaptiveHidden
