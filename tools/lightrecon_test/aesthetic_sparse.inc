// ASR21 tool-only implementation, included after Experiment::Data.
// SPDX-License-Identifier: GPL-2.0-or-later
namespace
{
Disk::Digest FixtureKey(const Fixture &f)
{
	GeometryKey k;
	k.V(f.upperPower);
	k.F(f.maximumAxisRadians);
	k.F(f.minimumOuterRadians);
	k.F(f.maximumOuterRadians);
	k.U(f.variants.size());
	for (const auto &e : f.variants)
	{
		const auto &g = e.geometry;
		k.U(g.source.id);
		k.U(unsigned(g.source.state));
		k.U(unsigned(g.kind));
		k.U(g.position.known);
		k.V(g.position.world);
		k.U(g.frame.known);
		k.V(g.frame.right);
		k.V(g.frame.up);
		k.V(g.frame.forward);
		k.U(g.geometry.known);
		k.F(g.geometry.halfWidthWorld);
		k.F(g.geometry.halfHeightWorld);
		k.F(g.geometry.halfLengthWorld);
		k.F(g.geometry.radiusWorld);
		for (const auto &v : {g.geometry.effectiveRadiusWorld, g.geometry.angularRadiusRadians})
		{
			k.U(v.known);
			k.F(v.value);
			k.U(v.bounded);
			k.F(v.lower);
			k.F(v.upper);
		}
		k.U(g.geometry.extension);
		k.U(unsigned(g.angular.kind));
		k.F(g.angular.cosInner);
		k.F(g.angular.cosOuter);
		k.U(g.angular.twoSided);
		k.U(g.angular.dataset);
		k.U(e.emittingSurface);
		k.F(e.spotTargetDistanceWorld);
		k.F(e.spotRadiusWorld);
		k.Digest(SourceSupport::Key(e.support));
	}
	return k.Finish();
}
void Decision(SparseReport &report, Id id, size_t config, const char *action,
			  const std::string &reason)
{
	SparseDecision d;
	d.fixture = id;
	d.configuration = config;
	d.action = action;
	d.rationale = reason;
	report.decisions.push_back(std::move(d));
}
void NormalizeSparse(const Request &input, const SparsePolicy &p, Request &r,
					 SparseReport &report, std::vector<FixtureEvidence> &evidence)
{
	Need(p.maxCandidates > 0 && p.maxCandidates <= 128 &&
		 input.fixtures.size() <= p.maxCandidates && p.maxSources > 0 && p.maxSources <= 16 &&
		 p.maxModels > 0 && p.maxModels <= 2048, Code::WorkLimit, "Sparse search bounds");
	Need(input.configurations.empty() && p.evidence.size() == input.fixtures.size() &&
		 !p.provenance.empty() && p.provenance.size() <= 4096, Code::InvalidInput,
		 "Sparse policy/evidence required; no preset configurations");
	for (double v : {p.trainingPenaltyPerFixture, p.minimumTrainingGain, p.pruneSlack,
					 p.minimumContribution})
		Need(std::isfinite(v) && v >= 0, Code::InvalidInput, "Invalid sparse threshold");
	report.enabled = true;
	report.policy = p;
	report.inputCandidates = input.fixtures.size();
	report.assumptions = {
		"Conditional aesthetic candidates; exact historical source truth unavailable",
		"Fixed supplied source geometry and independent placement/visibility admission",
		"Training-only greedy additions, deletion refits and terminal replacement alternatives; no global optimum claim",
		"Positive bounded relative RGB; shared bounded nuisance background; residual GI may remain unexplained",
		"Validation chooses complexity-penalized frozen models; visible evidence breaks equivalent ties only",
		"Heldout is a third disjoint audit and cannot select or refit"};
	r = input;
	r.fixtures.clear();
	std::map<Id, size_t> byId;
	std::vector<Disk::Digest> keys;
	// Sort by persistent identity, never input position, floating score hash or pointer.
	std::vector<size_t> order;
	for (size_t i = 0; i < input.fixtures.size(); ++i)
	{
		Need(!input.fixtures[i].variants.empty() && input.fixtures[i].variants.size() <= 32 &&
			 input.fixtures[i].variants[0].geometry.source.id && !p.evidence[i].provenance.empty() &&
			 p.evidence[i].provenance.size() <= 4096,
			 Code::InvalidInput, "Bounded identified sparse fixture and independent evidence provenance required");
		order.push_back(i);
	}
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		return input.fixtures[a].variants[0].geometry.source.id <
			input.fixtures[b].variants[0].geometry.source.id;
	});
	for (size_t i : order)
	{
		const auto &f = input.fixtures[i];
		Id id = f.variants[0].geometry.source.id;
		auto key = FixtureKey(f);
		auto found = byId.find(id);
		if (found != byId.end())
		{
			size_t j = found->second;
			Need(key == keys[j] && f.provenance == r.fixtures[j].provenance &&
				 f.directionProvenance == r.fixtures[j].directionProvenance &&
				 p.evidence[i].independentlyVisible == evidence[j].independentlyVisible &&
				 p.evidence[i].provenance == evidence[j].provenance,
				 Code::InvalidInput, "Conflicting duplicate source identity; cannot silently merge hypotheses");
			++report.duplicateInputs;
			Decision(report, id, SIZE_MAX, "duplicate-input-removed",
				"Identical source ID, geometry, support, bounds and evidence; one amplitude block retained");
			continue;
		}
		byId[id] = r.fixtures.size();
		keys.push_back(key);
		r.fixtures.push_back(f);
		evidence.push_back(p.evidence[i]);
	}
	report.uniqueCandidates = r.fixtures.size();
}
bool Active(const Fit &fit, const SparsePolicy &p)
{
	if (!fit.available)
		return false;
	for (const auto &s : fit.sources)
		if (s.columnRms * std::max({double(s.relativePower.x), double(s.relativePower.y),
								 double(s.relativePower.z)}) <= p.minimumContribution)
			return false;
	return true;
}
void SparseTraining(Experiment::Data &d, const Columns &columns, Meter &meter)
{
	auto &r = d.request;
	auto &report = d.sparse;
	const auto &p = report.policy;
	for (size_t f = 0; f < columns.size(); ++f)
		if (!columns[f][0].available)
			Decision(report, r.fixtures[f].variants[0].geometry.source.id, SIZE_MAX,
				"unavailable", columns[f][0].reason);
	std::map<std::vector<size_t>, size_t> catalog;
	auto fit = [&](std::vector<size_t> fs) -> size_t {
		meter.Tick();
		if (fs.empty())
			return SIZE_MAX;
		std::sort(fs.begin(), fs.end());
		auto found = catalog.find(fs);
		if (found != catalog.end())
			return found->second;
		Need(d.fits.size() < p.maxModels, Code::WorkLimit,
			 "Sparse model budget exhausted; no partial result selected");
		Configuration c;
		c.name = "sparse";
		c.fixtures = fs;
		for (size_t f : fs)
			c.name += "-" + std::to_string(r.fixtures[f].variants[0].geometry.source.id);
		size_t index = d.fits.size();
		State state; // each unique subset refits every amplitude and the same nuisance
		d.fits.push_back(MakeFit(r, d.training, columns, c, state, meter));
		d.states.push_back(std::move(state));
		r.configurations.push_back(std::move(c));
		catalog[fs] = index;
		return index;
	};
	auto objective = [&](size_t i) {
		return i == SIZE_MAX ? d.baseline.training.robust :
			!d.fits[i].available ? std::numeric_limits<double>::infinity() :
			d.fits[i].training.robust + p.trainingPenaltyPerFixture * d.fits[i].sources.size();
	};
	auto fixtures = [&](size_t i) {
		return i == SIZE_MAX ? std::vector<size_t>() : r.configurations[i].fixtures;
	};
	size_t current = SIZE_MAX;
	std::set<std::vector<size_t>> expanded;
	while (fixtures(current).size() < p.maxSources && expanded.insert(fixtures(current)).second)
	{
		auto fs = fixtures(current);
		size_t best = current, added = SIZE_MAX;
		for (size_t f = 0; f < r.fixtures.size(); ++f)
			if (!std::binary_search(fs.begin(), fs.end(), f))
			{
				auto proposed = fs;
				proposed.push_back(f);
				size_t i = fit(proposed);
				if (objective(i) + p.minimumTrainingGain + p.pruneSlack + r.options.tolerance < objective(best))
				{
					best = i;
					added = f;
				}
			}
		if (best == current)
			break;
		current = best;
		report.trainingPath.push_back(current);
		Decision(report, r.fixtures[added].variants[0].geometry.source.id, current, "add-refit",
			"Positive marginal training improvement exceeds complexity cost and predeclared gain/slack");
		for (;;)
		{
			fs = fixtures(current);
			best = current;
			size_t removed = SIZE_MAX;
			for (size_t f : fs)
			{
				auto proposed = fs;
				proposed.erase(std::find(proposed.begin(), proposed.end(), f));
				size_t i = fit(proposed);
				if (objective(i) <= objective(current) + p.pruneSlack &&
					(best == current || objective(i) < objective(best)))
				{
					best = i;
					removed = f;
				}
			}
			if (best == current)
				break;
			current = best;
			report.trainingPath.push_back(current);
			Decision(report, r.fixtures[removed].variants[0].geometry.source.id, current, "prune-refit",
				"Deletion and full bounded refit preserve the penalized training objective within prune slack");
		}
	}
	// Preserve near-equivalent IDs even when the greedy path chose their rival first.
	// These are training fits only, never validation-conditioned proposal generation.
	auto fs = fixtures(current);
	for (size_t old : fs)
		for (size_t f = 0; f < r.fixtures.size(); ++f)
			if (!std::binary_search(fs.begin(), fs.end(), f))
			{
				auto proposed = fs;
				*std::find(proposed.begin(), proposed.end(), old) = f;
				fit(proposed);
			}
	report.models = d.fits.size();
}
size_t HiddenCount(const Experiment::Data &d, size_t i)
{
	size_t n = 0;
	for (size_t f : d.request.configurations[i].fixtures)
		n += !d.evidence[f].independentlyVisible;
	return n;
}
void SparseConclusion(Experiment::Data &d)
{
	auto &report = d.sparse;
	report.rationale = d.selected == SIZE_MAX ?
		"Nuisance-only baseline retained: no active sparse configuration passes the penalized validation improvement/equivalence gate" :
		"Among validation-equivalent improvements, prefer the fewest sources, then independently visible fixtures, then stable source IDs";
	for (size_t f = 0; f < d.request.fixtures.size(); ++f)
	{
		Id id = d.request.fixtures[f].variants[0].geometry.source.id;
		auto contains = [&](size_t i) {
			return i != SIZE_MAX && std::binary_search(d.request.configurations[i].fixtures.begin(),
				d.request.configurations[i].fixtures.end(), f);
		};
		bool alternative = false;
		for (size_t i : d.alternatives)
			alternative |= contains(i);
		if (contains(d.selected))
			Decision(report, id, d.selected, "selected", report.rationale);
		else if (alternative)
			Decision(report, id, SIZE_MAX, "alternative-retained",
				"Distinct source identity retained in a validation-equivalent configuration; never summed with the selected model");
		else
			Decision(report, id, SIZE_MAX, "excluded",
				"No selected/equivalent supported configuration needs this source; lower training loss alone is insufficient");
	}
}
} // namespace
