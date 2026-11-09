// Bounded, colour-free stock representation audit. Exact geometry stays private.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
static void ShapeSelectionAudit(const O::Sources &s,
								const std::shared_ptr<const SurfaceGeometry> &geometry,
								const std::shared_ptr<const StaticVisibility> &rays,
								const ReadContext &read, const std::string &folder)
{
	namespace Sel = ShapeSelection;
	Need(bool(s.evidence), "Original fixture evidence required");
	Sel::Request request;
	request.scene.map = s.metadata.map;
	request.scene.domain = s.domain;
	request.scene.domainKey = Key("ASR19 original retained BSP domain");
	request.scene.originalSurfaces = geometry;
	request.scene.geometry = rays;
	request.scene.dynamic = s.dynamic;
	request.scene.occupancyPolicy = PointRefine::OccupancyPolicy::ConditionalBake;
	request.scene.occupancyProvenance =
		"ASR19 static scene conditional on unknown bake model states";
	// Archived mask blobs do not contain an exact clipped luminous triangle
	// lease. Never rebrand their entire BSP face as a measured luminous panel.
	request.fullSurfaceEmissionAttested = false;
	std::vector<const EmitterCandidate *> candidates;
	for (const auto &c : s.evidence->Candidates())
		candidates.push_back(&c);
	std::sort(candidates.begin(), candidates.end(),
			  [](const EmitterCandidate *a, const EmitterCandidate *b) { return a->id < b->id; });
	std::set<std::vector<uint32_t>> seen;
	uint64_t attempts = 0, work = 0, unknown = 0;
	std::map<unsigned, uint64_t> top;
	std::map<std::string, uint64_t> reasons;
	std::ofstream rows(folder + "/shape-selection.jsonl");
	rows << std::setprecision(17);
	for (const auto *c : candidates)
	{
		if (attempts >= 64 || work >= 16000000)
			break;
		const auto &e = s.evidence->Evidence().at(c->primary);
		if (!c->staticEligible || e.surfaces.empty() ||
			(e.origin != EvidenceOrigin::GlowSurface && e.origin != EvidenceOrigin::SurfaceLight))
			continue;
		auto ids = e.surfaces;
		std::sort(ids.begin(), ids.end());
		if (!seen.insert(ids).second)
			continue;
		request.fixture = c->id;
		request.emittingSurfaces = ids;
		Sel::Result result;
		Check(Sel::Rank(request, read, result));
		++attempts;
		work += result.work;
		unknown += !result.primitiveKnown;
		Need(work <= 16000000, "ASR19 stock cumulative geometry budget");
		rows << "{\"fixture\":" << Q(std::to_string(c->id))
			 << ",\"key\":" << Q(Disk::Hex(result.key)) << ",\"material\":" << Q(e.material)
			 << ",\"primitive_known\":" << result.primitiveKnown
			 << ",\"emitting_geometry_exact\":" << result.emittingGeometryExact
			 << ",\"area_upper_bound\":" << result.emittingAreaWorldSquared
			 << ",\"coverage\":" << result.coverage << ",\"surfaces\":[";
		for (size_t i = 0; i < ids.size(); ++i)
			rows << (i ? "," : "") << ids[i];
		rows << "],\"bounds_known\":" << result.actualBounds.known << ",\"bounds\":[";
		Vec(rows, result.actualBounds.minWorld);
		rows << ',';
		Vec(rows, result.actualBounds.maxWorld);
		rows << "],\"actual_triangles\":[";
		for (size_t i = 0; i < result.original.original.count; ++i)
		{
			rows << (i ? "," : "") << '[';
			for (unsigned j = 0; j < 3; ++j)
			{
				if (j)
					rows << ',';
				Vec(rows, result.original.original.data[i].world[j]);
			}
			rows << ']';
		}
		rows << "],\"ranked\":[";
		for (size_t i = 0; i < result.ranked.size(); ++i)
		{
			const auto &v = result.ranked[i];
			const auto &g = v.emitter.geometry;
			if (i)
				rows << ',';
			rows << "{\"kind\":" << unsigned(v.kind) << ",\"type\":" << Q(Sel::KindName(v.kind))
				 << ",\"status\":" << Q(Sel::StatusName(v.status))
				 << ",\"preference\":" << v.preference << ",\"position\":";
			Vec(rows, g.position.world);
			rows << ",\"forward\":";
			Vec(rows, g.frame.forward);
			rows << ",\"right\":";
			Vec(rows, g.frame.right);
			rows << ",\"up\":";
			Vec(rows, g.frame.up);
			rows << ",\"half_width\":" << g.geometry.halfWidthWorld
				 << ",\"half_height\":" << g.geometry.halfHeightWorld
				 << ",\"half_length\":" << g.geometry.halfLengthWorld
				 << ",\"radius\":" << g.geometry.radiusWorld << ",\"reasons\":[";
			for (size_t j = 0; j < v.reasons.size(); ++j)
			{
				if (j)
					rows << ',';
				auto name = Sel::ReasonName(v.reasons[j]);
				rows << Q(name);
				++reasons[name];
			}
			rows << "],\"uncertainties\":[";
			for (size_t j = 0; j < v.uncertainties.size(); ++j)
				rows << (j ? "," : "") << Q(v.uncertainties[j]);
			rows << "],\"placement\":"
				 << Q(PointRefine::ConstraintName(v.placement.placement.state)) << '}';
		}
		rows << "],\"limitation\":" << Q(result.limitation) << "}\n";
		if (!result.ranked.empty() && (result.ranked.front().status == Sel::Status::Supported ||
									   result.ranked.front().status == Sel::Status::Approximation))
			++top[unsigned(result.ranked.front().kind)];
	}
	Need(bool(rows), "Shape selection private witness write failed");
	std::ofstream summary(folder + "/shape-selection-summary.json");
	summary << "{\"schema\":\"org.openjk.stock-shape-priors.v1\",\"map\":" << Q(s.metadata.map.path)
			<< ",\"attempts\":" << attempts << ",\"primitive_unknown\":" << unknown
			<< ",\"work\":" << work
			<< ",\"reads_observed_rgb\":false,\"stock_fits_attempted\":false,\"profile\":"
			   "\"UnknownProfile\",\"historical_source_truth\":\"Unavailable\",\"bound_only_top_"
			   "preferences\":{";
	bool first = true;
	for (auto p : top)
	{
		summary << (first ? "" : ",") << Q(std::to_string(p.first)) << ':' << p.second;
		first = false;
	}
	summary << "},\"reason_counts\":{";
	first = true;
	for (auto p : reasons)
	{
		summary << (first ? "" : ",") << Q(p.first) << ':' << p.second;
		first = false;
	}
	summary << "},\"selection\":\"First 64 stable named-surface candidate IDs; identical surface "
			   "aliases collapsed for audit only; 16M work cap\"}";
	Need(bool(summary), "Shape selection summary write failed");
}
