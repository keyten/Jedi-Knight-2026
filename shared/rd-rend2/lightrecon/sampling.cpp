// Deterministic multiscale sampling; no dataset/target reader or renderer state.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "sampling.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Sampling
{
using namespace Bake::Detail;
namespace
{
void Need(bool b, Code c, const char *s)
{
	if (!b)
		throw Failure(c, s);
}
using Cell = std::tuple<int64_t, int64_t, int64_t>;
Id Number(Disk::Digest d)
{
	Id x = 0;
	for (unsigned i = 0; i < 8; ++i)
		x |= Id(d[i]) << (8 * i);
	return x ? x : 1;
}
Cell Bin(Vec3 p, double scale)
{
	Need(Length(V(p)) / scale < 1e12, Code::InvalidInput, "Sampling coordinate range");
	return Cell(int64_t(std::floor(p.x / scale)), int64_t(std::floor(p.y / scale)),
				int64_t(std::floor(p.z / scale)));
}
unsigned NormalBin(Vec3 n)
{
	return unsigned(std::lround((n.x + 1) * 4)) * 81 + unsigned(std::lround((n.y + 1) * 4)) * 9 +
		   unsigned(std::lround((n.z + 1) * 4));
}
Bounds Box(V a, V b)
{
	Bounds r;
	r.known = true;
	r.minWorld = a.Float();
	r.maxWorld = b.Float();
	return r;
}
bool Inside(const Bounds &b, Vec3 p)
{
	for (unsigned c = 0; c < 3; ++c)
		if (V(p)[c] < V(b.minWorld)[c] || V(p)[c] > V(b.maxWorld)[c])
			return false;
	return true;
}
Bounds Expand(Bounds b, double d)
{
	return Box(V(b.minWorld) - V(d, d, d), V(b.maxWorld) + V(d, d, d));
}
void HashGeometry(Core::GeometryKey &k, const Geometry &g)
{
	k.U(g.id);
	k.U(g.surface);
	k.U(g.material);
	k.U(g.block);
	k.U(g.correlation);
	k.V(g.position);
	k.V(g.normal);
	k.F(g.receiverArea);
	k.F(g.footprintRadius);
	k.F(g.representedArea);
	k.U(unsigned(g.role));
	k.U(g.supported);
	k.U(g.encodingKnown);
}
Selection Selected(const Geometry &g)
{
	Selection s;
	s.id = g.id;
	s.correlation = g.correlation;
	s.receiverArea = g.receiverArea;
	s.representedArea = g.representedArea;
	return s;
}
struct Engine
{
	Plan &out;
	const ReadContext &read;
	void Step(uint64_t n = 1)
	{
		Need(!Cancelled(read), Code::Cancelled, "Sampling cancelled");
		out.report.work += n;
		Need(out.report.work <= out.policy.maxWork, Code::WorkLimit,
			 "Sampling deterministic work budget");
	}
	std::vector<Selection> Choose(const std::vector<size_t> &candidates, size_t limit,
								  const std::map<Id, double> *priority = nullptr)
	{
		using Group = std::tuple<Id, Id, Id, unsigned>;
		std::map<Group, std::vector<size_t>> groups;
		std::map<Id, size_t> materials;
		for (auto i : candidates)
		{
			Step();
			const auto &g = out.geometry[i];
			groups[Group(g.material, g.surface, g.block, NormalBin(g.normal))].push_back(i);
			++materials[g.material];
		}
		std::vector<Selection> chosen;
		std::set<Id> used;
		auto pick = [&](const std::vector<size_t> &list) -> bool {
			size_t best = SIZE_MAX;
			double score = -1;
			for (auto i : list)
			{
				Step();
				const auto &g = out.geometry[i];
				if (used.count(g.id))
					continue;
				double value = priority && priority->count(g.id) ? priority->at(g.id) : 0;
				double distance = 1;
				for (const auto &s : chosen)
				{
					auto f = std::lower_bound(out.geometry.begin(), out.geometry.end(), s.id,
											  [](const Geometry &a, Id b) { return a.id < b; });
					Step();
					distance = std::min(distance, Length(V(g.position) - V(f->position)) /
													  out.report.localCellWorld);
				}
				value += distance;
				if (best == SIZE_MAX || value > score ||
					(value == score && g.id < out.geometry[best].id))
				{
					best = i;
					score = value;
				}
			}
			if (best == SIZE_MAX)
				return false;
			used.insert(out.geometry[best].id);
			chosen.push_back(Selected(out.geometry[best]));
			return true;
		};
		// Reserve real within-material support before round-robin diversity. No RGB.
		std::vector<std::pair<size_t, Id>> order;
		for (auto m : materials)
			order.emplace_back(m.second, m.first);
		std::sort(order.begin(), order.end(),
				  [](const std::pair<size_t, Id> &a, const std::pair<size_t, Id> &b) {
					  return a.first != b.first ? a.first > b.first : a.second < b.second;
				  });
		for (size_t m = 0; m < std::min(size_t(2), order.size()); ++m)
		{
			std::vector<size_t> list;
			for (auto i : candidates)
				if (out.geometry[i].material == order[m].second)
					list.push_back(i);
			for (size_t n = 0; n < 8 && chosen.size() < limit; ++n)
				if (!pick(list))
					break;
		}
		for (size_t round = 0; chosen.size() < limit; ++round)
		{
			bool more = false;
			for (const auto &g : groups)
			{
				if (chosen.size() == limit)
					break;
				more |= pick(g.second);
			}
			if (!more)
				break;
		}
		Normalize(out.geometry, chosen);
		return chosen;
	}
};
Quality Describe(const Plan &p, const std::vector<Selection> &set,
				 const std::map<Id, Signal> *signals = nullptr)
{
	Quality q;
	q.signalEvaluated = signals != nullptr;
	if (set.empty())
		q.reasons |= MissingGeometry;
	q.samples = set.size();
	std::set<Id> surfaces, materials, blocks, correlations;
	std::set<unsigned> normals;
	std::map<Id, size_t> counts;
	std::map<Id, std::array<double, 3>> lo, hi;
	std::map<Id, std::array<size_t, 3>> positive;
	V min(1e300, 1e300, 1e300), max(-1e300, -1e300, -1e300), normal;
	bool first = true;
	for (const auto &s : set)
	{
		auto it = std::lower_bound(p.geometry.begin(), p.geometry.end(), s.id,
								   [](const Geometry &a, Id b) { return a.id < b; });
		const auto &g = *it;
		surfaces.insert(g.surface);
		materials.insert(g.material);
		blocks.insert(g.block);
		correlations.insert(g.correlation);
		normals.insert(NormalBin(g.normal));
		q.receiverArea += g.receiverArea;
		q.representedArea += g.representedArea;
		q.maxSameMaterial = std::max<uint64_t>(q.maxSameMaterial, ++counts[g.material]);
		for (unsigned c = 0; c < 3; ++c)
		{
			min[c] = std::min(min[c], V(g.position)[c]);
			max[c] = std::max(max[c], V(g.position)[c]);
		}
		if (first)
		{
			normal = g.normal;
			first = false;
		}
		q.normalDiversity = std::max(q.normalDiversity, 1 - Dot(normal, V(g.normal)));
		if (!g.encodingKnown)
			q.reasons |= UnknownEncoding;
		if (signals && signals->count(g.id))
		{
			const auto &v = signals->at(g.id);
			bool pos = false, clip = false;
			for (unsigned c = 0; c < 3; ++c)
			{
				clip |= v.censored[c];
				pos |= v.rgb[c] > 0 && !v.censored[c];
				if (v.censored[c])
					continue;
				if (!lo.count(g.material))
				{
					lo[g.material] = {{1e300, 1e300, 1e300}};
					hi[g.material] = {{0, 0, 0}};
				}
				lo[g.material][c] = std::min(lo[g.material][c], v.rgb[c]);
				hi[g.material][c] = std::max(hi[g.material][c], v.rgb[c]);
				positive[g.material][c] += v.rgb[c] > 0;
			}
			q.positive += pos;
			q.censored += clip;
		}
	}
	q.effectiveGroups = correlations.size();
	q.surfaces = surfaces.size();
	q.materials = materials.size();
	q.blocks = blocks.size();
	q.normalBins = normals.size();
	q.baselineWorld = set.empty() ? 0 : Length(max - min);
	q.directionalSupport = q.normalBins >= 2 && q.normalDiversity > .05;
	if (set.size() < 6 || q.maxSameMaterial < 4)
		q.reasons |= TooFewSameMaterial;
	if (!q.directionalSupport)
		q.reasons |= InsufficientNormals;
	if (q.effectiveGroups < 6)
		q.reasons |= CorrelatedSupport;
	if (signals)
	{
		for (const auto &m : lo)
		{
			bool useful = false;
			for (unsigned c = 0; c < 3; ++c)
			{
				double contrast = (hi[m.first][c] - m.second[c]) / (hi[m.first][c] + 1);
				if (positive[m.first][c] >= 4)
				{
					q.contrast = std::max(q.contrast, contrast);
					useful |= contrast >= p.policy.minimumContrast;
				}
			}
			q.informativeMaterials += useful;
		}
		if (!q.informativeMaterials || !q.positive)
			q.reasons |= NoPositiveSignal;
		if (q.censored == set.size() && !set.empty())
			q.reasons |= SaturatedOnly;
	}
	q.discoveryAdmitted =
		signals && q.effectiveGroups >= 6 && q.maxSameMaterial >= 4 && q.informativeMaterials > 0 &&
		q.positive >= 4 &&
		(!(q.reasons & UnknownEncoding) || p.policy.allowConditionalEncodedSignal);
	return q;
}
void Seal(Plan &out)
{
	Core::GeometryKey k;
	k.U(kVersion);
	k.Digest(out.geometryKey);
	k.Digest(out.trainingSignalKey);
	k.U(out.rounds);
	k.U(out.report.work);
	k.U(out.report.bytes);
	const auto &p = out.policy;
	for (auto x : {p.maxPool, p.maxJobs, p.coarseRows, p.discoveryRows, p.positionRows,
				   p.shadowRows, p.validationRows, p.heldoutRows, p.maxTotalRows, p.maxRounds,
				   p.maxSurfacesPerJob, p.maxBlocksPerJob})
		k.U(x);
	k.U(p.maxWork);
	k.U(p.maxBytes);
	k.F(p.minimumContrast);
	k.F(p.partitionBlockWorld);
	k.U(p.allowConditionalEncodedSignal);
	k.U(p.sourcePoolComplete);
	auto selections = [&](const std::vector<Selection> &set) {
		k.U(set.size());
		for (const auto &s : set)
		{
			k.U(s.id);
			k.U(s.correlation);
			k.F(s.receiverArea);
			k.F(s.representedArea);
			k.F(s.statisticalWeight);
		}
	};
	k.U(out.jobs.size());
	for (const auto &job : out.jobs)
	{
		k.U(job.id);
		k.B(job.receivers);
		k.B(job.sourceContext);
		for (const auto &set : job.selected)
			selections(set);
		k.U(job.training.reasons);
		k.U(job.training.discoveryAdmitted);
		k.U(job.validation.effectiveGroups);
		k.U(job.heldout.effectiveGroups);
		k.U(job.reasons);
	}
	for (const auto &set : out.global)
		selections(set);
	for (const auto &set : out.evaluationCoverage)
		selections(set);
	k.U(out.report.complete);
	out.key = k.Finish();
}
void Global(Plan &p)
{
	for (size_t purpose = 0; purpose < size_t(Purpose::Count); ++purpose)
	{
		if (purpose == size_t(Purpose::Census) || purpose == size_t(Purpose::CoarseCoverage))
			continue;
		std::map<Id, Selection> combined;
		for (const auto &job : p.jobs)
			for (auto s : job.selected[purpose])
				combined.emplace(s.id, s);
		if (purpose == size_t(Purpose::ModelValidation) || purpose == size_t(Purpose::FinalHeldout))
			for (auto s : p.evaluationCoverage[purpose == size_t(Purpose::ModelValidation) ? 0 : 1])
				combined.emplace(s.id, s);
		auto &set = p.global[purpose];
		set.clear();
		for (auto s : combined)
			set.push_back(s.second);
		Normalize(p.geometry, set);
	}
	std::set<Id> total;
	for (const auto &set : p.global)
		for (auto s : set)
			total.insert(s.id);
	Need(total.size() <= p.policy.maxTotalRows, Code::WorkLimit,
		 "Combined sampling row budget; use explicit spatial shards");
}
} // namespace
void Normalize(const std::vector<Geometry> &geometry, std::vector<Selection> &selected)
{
	std::map<std::pair<Id, Id>, double> sums;
	std::set<Id> unique;
	for (const auto &s : selected)
	{
		Need(unique.insert(s.correlation).second, Code::InvalidInput,
			 "Repeated correlated sampling vote");
		auto g = std::lower_bound(geometry.begin(), geometry.end(), s.id,
								  [](const Geometry &a, Id b) { return a.id < b; });
		Need(g != geometry.end() && g->id == s.id, Code::InvalidInput,
			 "Sampling reference missing");
		Need(s.correlation == g->correlation && s.receiverArea == g->receiverArea &&
				 s.representedArea == g->representedArea && std::isfinite(s.receiverArea) &&
				 s.receiverArea > 0,
			 Code::InvalidInput, "Sampling weights must retain actual geometry measures");
		auto &sum = sums[{g->block, g->surface}];
		sum += s.receiverArea;
		Need(std::isfinite(sum), Code::InvalidInput, "Sampling area accumulation overflow");
	}
	for (auto &s : selected)
	{
		auto g = std::lower_bound(geometry.begin(), geometry.end(), s.id,
								  [](const Geometry &a, Id b) { return a.id < b; });
		s.statisticalWeight = s.receiverArea / sums.at({g->block, g->surface});
	}
	std::sort(selected.begin(), selected.end(),
			  [](const Selection &a, const Selection &b) { return a.id < b.id; });
}
Error Build(Disk::Digest map, Disk::Digest partition, std::vector<Geometry> geometry,
			const Policy &p, const ReadContext &read, Plan &out)
{
	out = {};
	try
	{
		Need(map != Disk::Digest{} && partition != Disk::Digest{}, Code::InvalidInput,
			 "Sampling map/partition identity required");
		Need(!geometry.empty() && geometry.size() <= p.maxPool && p.maxPool <= 65536 && p.maxJobs &&
				 p.maxJobs <= 32 && p.maxRounds <= 4 && p.maxTotalRows <= 8192 && p.maxTotalRows &&
				 p.maxWork && p.maxWork <= 1000000000,
			 Code::InvalidInput, "Sampling pool/job/round limits");
		for (auto n : {p.coarseRows, p.discoveryRows, p.positionRows, p.shadowRows,
					   p.validationRows, p.heldoutRows})
			Need(n && n <= 256, Code::InvalidInput, "Sampling purpose budget");
		Need(p.maxSurfacesPerJob && p.maxSurfacesPerJob <= 256 && p.maxBlocksPerJob &&
				 p.maxBlocksPerJob <= 64 && std::isfinite(p.partitionBlockWorld) &&
				 p.partitionBlockWorld > 0 && std::isfinite(p.minimumContrast) &&
				 p.minimumContrast > 0,
			 Code::InvalidInput, "Sampling geometric/admission policy");
		out.mapKey = map;
		out.partitionKey = partition;
		out.policy = p;
		auto &r = out.report;
		r.complete = p.sourcePoolComplete;
		r.input = geometry.size();
		r.bytes = geometry.size() * sizeof(Geometry) * 4 + 1048576;
		Need(r.bytes <= p.maxBytes && (!read.maxBytes || r.bytes <= read.maxBytes),
			 Code::MemoryLimit, "Sampling geometry memory budget");
		Engine e{out, read};
		std::sort(geometry.begin(), geometry.end(),
				  [](const Geometry &a, const Geometry &b) { return a.id < b.id; });
		std::vector<double> diameters;
		Id previous = 0;
		Geometry previousGeometry;
		for (const auto &g : geometry)
		{
			e.Step();
			Need(g.id && g.surface && g.material && g.block && Finite(g.position) &&
					 Normal(g.normal) && std::isfinite(g.receiverArea) && g.receiverArea > 0 &&
					 std::isfinite(g.footprintRadius) && g.footprintRadius > 0 &&
					 g.role >= Role::Training && g.role <= Role::Gap &&
					 std::isfinite(g.representedArea) && g.representedArea >= 0,
				 Code::InvalidInput, "Invalid sampling geometry");
			if (g.id == previous)
			{
				++r.duplicates;
				Core::GeometryKey currentKey, previousKey;
				HashGeometry(currentKey, g);
				HashGeometry(previousKey, previousGeometry);
				Need(currentKey.Finish() == previousKey.Finish(), Code::InvalidInput,
					 "Conflicting duplicate sample identity");
				continue;
			}
			previous = g.id;
			previousGeometry = g;
			if (g.role == Role::Gap)
			{
				++r.gap;
				continue;
			}
			if (!g.supported)
			{
				++r.unsupported;
				continue;
			}
			out.geometry.push_back(g);
			diameters.push_back(2 * g.footprintRadius);
		}
		Need(!out.geometry.empty(), Code::Unavailable, "No supported receiver geometry after gaps");
		std::sort(diameters.begin(), diameters.end());
		r.medianFootprintDiameter = diameters[diameters.size() / 2];
		r.correlationWorld = 1.5 * r.medianFootprintDiameter;
		r.localCellWorld = 8 * r.medianFootprintDiameter;
		// A correlation proxy, never a claim of independent Fisher information.
		using Correlation = std::tuple<Id, unsigned, int64_t, int64_t, int64_t>;
		std::map<Correlation, size_t> groups;
		std::vector<Geometry> distinct;
		for (auto g : out.geometry)
		{
			e.Step();
			auto b = Bin(g.position, r.correlationWorld);
			Correlation c(g.material, NormalBin(g.normal), std::get<0>(b), std::get<1>(b),
						  std::get<2>(b));
			auto f = groups.find(c);
			if (f != groups.end())
			{
				auto &old = distinct[f->second];
				Need(old.role == g.role, Code::InvalidInput,
					 "Correlated footprints bridge immutable folds");
				old.representedArea += g.representedArea;
				++r.duplicates;
				continue;
			}
			Core::GeometryKey k;
			k.Digest(map);
			k.U(g.material);
			k.U(NormalBin(g.normal));
			k.U(uint64_t(std::get<0>(b)));
			k.U(uint64_t(std::get<1>(b)));
			k.U(uint64_t(std::get<2>(b)));
			g.correlation = Number(k.Finish());
			groups[c] = distinct.size();
			distinct.push_back(g);
		}
		out.geometry = std::move(distinct);
		std::sort(out.geometry.begin(), out.geometry.end(),
				  [](const Geometry &a, const Geometry &b) { return a.id < b.id; });
		V lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
		for (const auto &g : out.geometry)
			for (unsigned a = 0; a < 3; ++a)
			{
				lo[a] = std::min(lo[a], V(g.position)[a]);
				hi[a] = std::max(hi[a], V(g.position)[a]);
			}
		r.coarseCellWorld =
			std::max(r.localCellWorld,
					 Length(hi - lo) / std::cbrt(double(std::max(size_t(1), p.coarseRows / 4))));
		Core::GeometryKey gkey;
		gkey.U(kVersion);
		gkey.Digest(map);
		gkey.Digest(partition);
		gkey.F(r.localCellWorld);
		gkey.F(r.correlationWorld);
		for (const auto &g : out.geometry)
			HashGeometry(gkey, g);
		out.geometryKey = gkey.Finish();
		std::map<Cell, std::vector<size_t>> cells;
		std::vector<size_t> training;
		for (size_t i = 0; i < out.geometry.size(); ++i)
			if (out.geometry[i].role == Role::Training)
			{
				cells[Bin(out.geometry[i].position, r.localCellWorld)].push_back(i);
				training.push_back(i);
			}
		out.global[size_t(Purpose::CoarseCoverage)] = e.Choose(training, p.coarseRows);
		for (unsigned role = 1; role < 3; ++role)
		{
			std::vector<size_t> candidates;
			for (size_t i = 0; i < out.geometry.size(); ++i)
				if (unsigned(out.geometry[i].role) == role)
					candidates.push_back(i);
			out.evaluationCoverage[role - 1] = e.Choose(candidates, p.coarseRows);
		}
		struct Choice
		{
			Cell cell;
			size_t support = 0, normals = 0;
			Id key = 0;
		};
		std::vector<Choice> choices;
		for (const auto &cell : cells)
		{
			std::map<Id, size_t> materials;
			std::set<unsigned> normals;
			for (auto i : cell.second)
			{
				++materials[out.geometry[i].material];
				normals.insert(NormalBin(out.geometry[i].normal));
			}
			Choice c;
			c.cell = cell.first;
			for (auto m : materials)
				c.support = std::max(c.support, m.second);
			c.normals = normals.size();
			Core::GeometryKey k;
			k.Digest(map);
			k.U(uint64_t(std::get<0>(c.cell)));
			k.U(uint64_t(std::get<1>(c.cell)));
			k.U(uint64_t(std::get<2>(c.cell)));
			c.key = Number(k.Finish());
			choices.push_back(c);
		}
		std::sort(choices.begin(), choices.end(), [](const Choice &a, const Choice &b) {
			auto x = std::make_pair(std::min(a.support, size_t(48)),
									std::min(a.normals, size_t(6))),
				 y = std::make_pair(std::min(b.support, size_t(48)),
									std::min(b.normals, size_t(6)));
			return x != y ? x > y : a.key < b.key;
		});
		r.eligibleNeighbourhoods = choices.size();
		r.deferredNeighbourhoods = choices.size() > p.maxJobs ? choices.size() - p.maxJobs : 0;
		if (r.deferredNeighbourhoods)
			r.complete = false;
		std::set<Cell> covered;
		for (unsigned pass = 0; pass < 2 && out.jobs.size() < p.maxJobs; ++pass)
			for (const auto &choice : choices)
			{
				if (out.jobs.size() == p.maxJobs)
					break;
				if (std::any_of(out.jobs.begin(), out.jobs.end(),
								[&](const Job &j) { return j.id == choice.key; }))
					continue;
				V min(double(std::get<0>(choice.cell)) * r.localCellWorld,
					  double(std::get<1>(choice.cell)) * r.localCellWorld,
					  double(std::get<2>(choice.cell)) * r.localCellWorld);
				V max = min + V(1, 1, 1) * r.localCellWorld;
				auto coarse = Bin(((min + max) * .5).Float(), r.coarseCellWorld);
				if (!pass && covered.count(coarse))
					continue;
				covered.insert(coarse);
				Job job;
				job.id = choice.key;
				job.receivers = Expand(Box(min, max), r.localCellWorld * .5);
				job.sourceContext = Expand(job.receivers, r.localCellWorld * .5);
				std::array<std::vector<size_t>, 3> lists;
				std::array<std::set<Id>, 3> surfaces, blocks;
				Bounds eval =
					Expand(job.receivers, std::max(r.localCellWorld, p.partitionBlockWorld * 3));
				for (size_t i = 0; i < out.geometry.size(); ++i)
				{
					e.Step();
					const auto &g = out.geometry[i];
					unsigned role = unsigned(g.role);
					if (role > 2 || !Inside(role ? eval : job.receivers, g.position))
						continue;
					if ((!surfaces[role].count(g.surface) &&
						 surfaces[role].size() >= p.maxSurfacesPerJob) ||
						(!blocks[role].count(g.block) && blocks[role].size() >= p.maxBlocksPerJob))
					{
						r.complete = false;
						continue;
					}
					surfaces[role].insert(g.surface);
					blocks[role].insert(g.block);
					lists[role].push_back(i);
				}
				job.selected[size_t(Purpose::Discovery)] = e.Choose(lists[0], p.discoveryRows);
				job.selected[size_t(Purpose::PositionRefinement)] =
					e.Choose(lists[0], p.positionRows);
				job.selected[size_t(Purpose::ModelValidation)] =
					e.Choose(lists[1], p.validationRows);
				job.selected[size_t(Purpose::FinalHeldout)] = e.Choose(lists[2], p.heldoutRows);
				job.training = Describe(out, job.selected[size_t(Purpose::Discovery)]);
				job.validation = Describe(out, job.selected[size_t(Purpose::ModelValidation)]);
				job.heldout = Describe(out, job.selected[size_t(Purpose::FinalHeldout)]);
				job.training.validationGeometryAvailable =
					job.validation.effectiveGroups >= 4 && job.heldout.effectiveGroups >= 4;
				if (!job.training.validationGeometryAvailable)
					job.reasons |= MissingIndependentFolds;
				out.jobs.push_back(std::move(job));
			}
		Global(out);
		Seal(out);
		return {};
	}
	catch (const Error &e)
	{
		out.report.complete = false;
		if (e.code == Code::WorkLimit)
			for (auto &j : out.jobs)
				j.reasons |= WorkLimited;
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Sampling allocation");
	}
}
Error Refine(const Plan &input, const std::vector<Signal> &signals, const ReadContext &read,
			 Plan &out, const std::vector<Priority> &priorities)
{
	out = {};
	try
	{
		Need(input.geometry.size() <= 65536 && input.jobs.size() <= 32 && signals.size() <= 65536 &&
				 priorities.size() <= 65536,
			 Code::MemoryLimit, "Sampling resume input bounds");
		Core::GeometryKey geometryKey;
		geometryKey.U(kVersion);
		geometryKey.Digest(input.mapKey);
		geometryKey.Digest(input.partitionKey);
		geometryKey.F(input.report.localCellWorld);
		geometryKey.F(input.report.correlationWorld);
		for (const auto &g : input.geometry)
			HashGeometry(geometryKey, g);
		Need(geometryKey.Finish() == input.geometryKey, Code::InvalidState,
			 "Sampling immutable geometry/roles changed");
		auto sealed = input;
		Seal(sealed);
		Need(sealed.key == input.key, Code::InvalidState, "Sampling plan/policy changed");
		Need(input.rounds < input.policy.maxRounds, Code::WorkLimit,
			 "Sampling refinement round budget");
		uint64_t extraBytes =
			signals.size() * sizeof(Signal) * 4 + priorities.size() * sizeof(Priority) * 4;
		Need(signals.size() <= input.policy.maxPool &&
				 input.report.bytes + extraBytes <= input.policy.maxBytes &&
				 (!read.maxBytes || input.report.bytes + extraBytes <= read.maxBytes),
			 Code::MemoryLimit, "Training signal budget");
		out = input;
		out.report.bytes += extraBytes;
		Engine e{out, read};
		std::map<Id, Signal> values;
		for (const auto &s : signals)
		{
			e.Step();
			auto g = std::lower_bound(out.geometry.begin(), out.geometry.end(), s.id,
									  [](const Geometry &a, Id b) { return a.id < b; });
			Need(g != out.geometry.end() && g->id == s.id && g->role == Role::Training,
				 Code::InvalidInput, "Signal is not in training geometry; hidden RGB forbidden");
			Need(s.encodingKnown == g->encodingKnown, Code::InvalidInput,
				 "Signal cannot upgrade unknown encoding");
			Need(values.emplace(s.id, s).second, Code::InvalidInput, "Repeated training signal");
			for (auto v : s.rgb)
				Need(std::isfinite(v) && v >= 0, Code::InvalidInput, "Invalid training signal");
		}
		std::map<std::pair<Id, PriorityCause>, double> requested;
		std::map<Id, double> requestedRank, requestedEdge;
		for (const auto &p : priorities)
		{
			e.Step();
			Need(values.count(p.id) && p.cause >= PriorityCause::TrainingResidual &&
					 p.cause <= PriorityCause::SupportedShadow && std::isfinite(p.value) &&
					 p.value >= 0 && p.value <= 1,
				 Code::InvalidInput, "Adaptive priority must reference supported training signal");
			Need(requested.emplace(std::make_pair(p.id, p.cause), p.value).second,
				 Code::InvalidInput, "Repeated adaptive priority");
			requestedRank[p.id] = std::max(requestedRank[p.id], p.value);
			if (p.cause == PriorityCause::SupportedShadow)
				requestedEdge[p.id] = std::max(requestedEdge[p.id], p.value);
		}
		Core::GeometryKey signalKey;
		signalKey.Digest(input.partitionKey);
		for (const auto &v : values)
		{
			signalKey.U(v.first);
			for (unsigned c = 0; c < 3; ++c)
			{
				signalKey.F(v.second.rgb[c]);
				signalKey.U(v.second.censored[c]);
			}
		}
		for (const auto &p : requested)
		{
			signalKey.U(p.first.first);
			signalKey.U(unsigned(p.first.second));
			signalKey.F(p.second);
		}
		out.trainingSignalKey = signalKey.Finish();
		for (auto &job : out.jobs)
		{
			std::vector<size_t> candidates;
			std::map<Id, std::array<double, 3>> low, high;
			std::set<Id> surfaces, blocks;
			bool limited = false;
			for (size_t i = 0; i < out.geometry.size(); ++i)
			{
				e.Step();
				const auto &g = out.geometry[i];
				if (g.role != Role::Training || !Inside(job.receivers, g.position) ||
					!values.count(g.id))
					continue;
				if ((!surfaces.count(g.surface) &&
					 surfaces.size() >= out.policy.maxSurfacesPerJob) ||
					(!blocks.count(g.block) && blocks.size() >= out.policy.maxBlocksPerJob))
				{
					limited = true;
					out.report.complete = false;
					continue;
				}
				surfaces.insert(g.surface);
				blocks.insert(g.block);
				candidates.push_back(i);
				if (!low.count(g.material))
				{
					low[g.material] = {{1e300, 1e300, 1e300}};
					high[g.material] = {{0, 0, 0}};
				}
				for (unsigned c = 0; c < 3; ++c)
					if (!values[g.id].censored[c])
					{
						low[g.material][c] = std::min(low[g.material][c], values[g.id].rgb[c]);
						high[g.material][c] = std::max(high[g.material][c], values[g.id].rgb[c]);
					}
			}
			limited |= candidates.size() > 512;
			if (candidates.size() > 512)
			{
				candidates.resize(512);
				out.report.complete = false;
			}
			std::map<Id, double> priority, edge;
			for (auto i : candidates)
			{
				const auto &g = out.geometry[i];
				const auto &s = values.at(g.id);
				double extreme = 0;
				for (unsigned c = 0; c < 3; ++c)
					if (!s.censored[c] && high[g.material][c] > low[g.material][c])
					{
						double x = (s.rgb[c] - low[g.material][c]) /
								   (high[g.material][c] - low[g.material][c]);
						extreme = std::max(extreme, std::abs(x - .5));
					}
				priority[g.id] = extreme + requestedRank[g.id];
			}
			// Bounded nearest-neighbour training edge proxy. Same surface/material,
			// not an assertion of cast-shadow correspondence or linear irradiance.
			for (auto i : candidates)
			{
				const auto &g = out.geometry[i];
				double nearest = 1e300;
				size_t neighbour = SIZE_MAX;
				for (auto j : candidates)
				{
					e.Step();
					const auto &h = out.geometry[j];
					if (i == j || g.surface != h.surface || g.material != h.material ||
						g.block != h.block)
						continue;
					double d = Length(V(g.position) - V(h.position));
					if (d > 0 && d < nearest && d <= 4 * out.report.medianFootprintDiameter)
					{
						nearest = d;
						neighbour = j;
					}
				}
				if (neighbour != SIZE_MAX)
				{
					double contrast = 0;
					for (unsigned c = 0; c < 3; ++c)
						if (!values[g.id].censored[c] &&
							!values[out.geometry[neighbour].id].censored[c])
							contrast = std::max(
								contrast,
								std::abs(values[g.id].rgb[c] -
										 values[out.geometry[neighbour].id].rgb[c]) /
									(1 + std::max(values[g.id].rgb[c],
												  values[out.geometry[neighbour].id].rgb[c])));
					edge[g.id] = contrast;
				}
				edge[g.id] += requestedEdge[g.id];
			}
			job.selected[size_t(Purpose::Discovery)] =
				e.Choose(candidates, out.policy.discoveryRows, &priority);
			job.selected[size_t(Purpose::PositionRefinement)] =
				e.Choose(candidates, out.policy.positionRows, &priority);
			job.selected[size_t(Purpose::ShadowRefinement)] =
				e.Choose(candidates, out.policy.shadowRows, &edge);
			job.training = Describe(out, job.selected[size_t(Purpose::Discovery)], &values);
			job.training.validationGeometryAvailable =
				job.validation.effectiveGroups >= 4 && job.heldout.effectiveGroups >= 4;
			job.reasons = job.training.reasons |
						  (job.training.validationGeometryAvailable ? 0 : MissingIndependentFolds) |
						  ((limited || !out.policy.sourcePoolComplete) ? WorkLimited : 0);
		}
		++out.rounds;
		Global(out);
		Seal(out);
		return {};
	}
	catch (const Error &e)
	{
		out.report.complete = false;
		if (e.code == Code::WorkLimit)
			for (auto &j : out.jobs)
				j.reasons |= WorkLimited;
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Sampling refinement allocation");
	}
}
const char *PurposeName(Purpose p)
{
	static const char *names[] = {"census",
								  "coarse_coverage",
								  "candidate_discovery",
								  "position_refinement",
								  "shadow_refinement",
								  "model_validation",
								  "final_heldout"};
	return size_t(p) < size_t(Purpose::Count) ? names[size_t(p)] : "invalid";
}
} // namespace Sampling
} // namespace Core
} // namespace Rend2LightRecon
