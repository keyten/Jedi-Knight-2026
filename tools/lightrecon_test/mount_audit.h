// X14 geometry-only admission audit. No observation target values enter this path.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
static void MountAudit(const O::Sources &s, const std::shared_ptr<const SurfaceGeometry> &geometry,
					   const std::shared_ptr<const StaticVisibility> &rays, const ReadContext &read,
					   const std::string &output)
{
	auto start = std::chrono::steady_clock::now();
	Candidates::Provider provider;
	Need(bool(s.evidence), "Original source evidence missing");
	Check(Candidates::FromEvidence("original-emissive/authored", rays->CertificationKey(), {},
								   s.evidence, provider));
	Candidates::Request consolidate;
	consolidate.map = s.metadata.map.sha256;
	consolidate.geometryKey = rays->CertificationKey();
	consolidate.providers = {provider};
	consolidate.visibility = rays;
	consolidate.limits.maxCandidates = 250000;
	consolidate.limits.maxEvidence = 250000;
	consolidate.limits.maxBytes = 256ull << 20;
	Candidates::Catalog catalog;
	Check(Candidates::Consolidate(consolidate, read, catalog));
	J::Request request;
	request.mapKey = s.metadata.map.sha256;
	request.geometry.geometryKey = rays->CertificationKey();
	request.geometry.visibility = rays;
	request.sourceSearchBounds = s.domain->Source().worldBounds;
	request.placement.map = s.metadata.map;
	request.placement.domain = s.domain;
	request.placement.domainKey = Key("X14 captured original brush domain");
	request.placement.geometry = rays;
	request.placement.dynamic = s.dynamic;
	request.placement.occupancyPolicy = PointRefine::OccupancyPolicy::ConditionalBake;
	request.placement.occupancyProvenance =
		"X14 explicit static bake scene conditional on unknown model states; independent receiver "
		"visibility unchanged";
	std::shared_ptr<const PointRefine::PlacementProvider> placement;
	Check(PointRefine::MakePlacement(request.placement, read, placement));
	auto withSupport = request;
	withSupport.placement.originalSurfaces = geometry;
	std::shared_ptr<const PointRefine::PlacementProvider> supportPlacement;
	Check(PointRefine::MakePlacement(withSupport.placement, read, supportPlacement));
	std::vector<const Candidates::Model *> ordered;
	for (const auto &m : catalog.models)
		ordered.push_back(&m);
	std::sort(ordered.begin(), ordered.end(),
			  [](const Candidates::Model *a, const Candidates::Model *b) { return a->id < b->id; });
	uint64_t attempts = 0, accepted = 0, newlyAdmitted = 0, work = 0, conditional = 0,
			 oldClasses[5] = {}, newClasses[5] = {};
	std::map<std::string, uint64_t> reasons;
	std::map<unsigned, uint64_t> kinds;
	std::ofstream detail(output + "/mounting-witnesses.tsv");
	detail << "source\tfixture\tkind\tsupport\tx\ty\tz\twidth\theight\tlength\tradius\tnamed_"
			  "surfaces\told\tnew\treason\twork\n";
	for (const auto *model : ordered)
	{
		if (attempts >= 256 || work >= 16000000)
			break;
		J::Candidate before, after;
		if (!P::MakeCandidate(catalog, *model, request, before) ||
			!P::MakeCandidate(catalog, *model, withSupport, after))
			continue;
		if (before.root != TransportRootKind::LocalEmitter ||
			after.shape.initial.support.kind == SourceSupport::Kind::LegacyVacuum)
			continue;
		++attempts;
		auto &e = before.shape.initial.geometry;
		PointRefine::PlacementResult prior;
		Check(placement->Check(e.position.world, e.position.world, before.clearanceWorld, read,
							   prior));
		work += prior.work;
		if (PointRefine::Admissible(prior) &&
			(e.kind == EmitterKind::Rectangle || e.kind == EmitterKind::Tube))
		{
			double radius = 0;
			for (unsigned k = 0; k < 3; ++k)
			{
				double extent = e.kind == EmitterKind::Rectangle
									? std::abs(V(e.frame.right)[k]) * e.geometry.halfWidthWorld +
										  std::abs(V(e.frame.up)[k]) * e.geometry.halfHeightWorld
									: std::abs(V(e.frame.right)[k]) * e.geometry.halfLengthWorld +
										  e.geometry.radiusWorld *
											  std::sqrt(std::pow(V(e.frame.up)[k], 2) +
														std::pow(V(e.frame.forward)[k], 2));
				radius = std::max(radius, extent);
			}
			Check(placement->Check(e.position.world, e.position.world,
								   radius + before.clearanceWorld, read, prior));
			work += prior.work;
		}
		SourceSupport::Result next;
		auto error = SourceSupport::Check(withSupport.placement, *supportPlacement, e,
										  after.shape.initial.geometry, after.shape.initial.support,
										  read, next);
		if (!error)
		{
			next.placement.state = PointRefine::Constraint::UnknownGeometry;
			next.reason = "Operational: " + error.message;
		}
		work += next.work;
		accepted += PointRefine::Admissible(next.placement);
		conditional += next.placement.state == PointRefine::Constraint::Conditional;
		newlyAdmitted += !PointRefine::Admissible(prior) && PointRefine::Admissible(next.placement);
		++oldClasses[unsigned(PointRefine::Classify(prior))];
		++newClasses[unsigned(PointRefine::Classify(next.placement))];
		++reasons[next.reason];
		++kinds[unsigned(e.kind)];
		detail << model->id << '\t' << after.shape.initial.support.fixture << '\t'
			   << unsigned(e.kind) << '\t' << unsigned(after.shape.initial.support.kind) << '\t'
			   << e.position.world.x << '\t' << e.position.world.y << '\t' << e.position.world.z
			   << '\t' << 2 * e.geometry.halfWidthWorld << '\t' << 2 * e.geometry.halfHeightWorld
			   << '\t' << 2 * e.geometry.halfLengthWorld << '\t' << e.geometry.radiusWorld << '\t';
		for (auto id : after.shape.initial.support.surfaces)
			detail << id << ',';
		detail << '\t' << PointRefine::ConstraintName(prior.state) << '\t'
			   << PointRefine::ConstraintName(next.placement.state) << '\t' << next.reason << '\t'
			   << next.work << '\n';
	}
	std::ofstream json(output + "/mounting.json");
	json << "{\"schema\":\"org.openjk.emitter-support-stock.v1\",\"map\":" << Q(s.metadata.map.path)
		 << ",\"bsp_sha256\":" << Q(Disk::Hex(s.metadata.map.sha256))
		 << ",\"catalog_models\":" << catalog.models.size() << ",\"attempts\":" << attempts
		 << ",\"accepted\":" << accepted << ",\"conditional\":" << conditional
		 << ",\"newly_admitted\":" << newlyAdmitted << ",\"old_classes\":[";
	for (unsigned i = 0; i < 5; ++i)
		json << (i ? "," : "") << oldClasses[i];
	json << "],\"new_classes\":[";
	for (unsigned i = 0; i < 5; ++i)
		json << (i ? "," : "") << newClasses[i];
	json << "],\"class_order\":[\"Proven\",\"Collision\",\"Conditional\",\"Unknown\",\"Invalid\"],"
			"\"reasons\":{";
	bool first = true;
	for (const auto &r : reasons)
	{
		json << (first ? "" : ",") << Q(r.first) << ':' << r.second;
		first = false;
	}
	json << "},\"types\":{";
	first = true;
	for (auto k : kinds)
	{
		json << (first ? "" : ",") << Q(std::to_string(k.first)) << ':' << k.second;
		first = false;
	}
	json << "},\"work\":" << work << ",\"catalog_work\":" << catalog.report.pairTests
		 << ",\"seconds\":"
		 << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
		 << ",\"reads_observed_rgb\":false,\"joint_fit_attempted\":false,\"selection\":\"stable "
			"consolidated ID; first 256 named-surface candidates, 16M geometry work "
			"cap\",\"profile\":\"UnknownProfile\",\"claims_original_source_recovery\":false}";
}
