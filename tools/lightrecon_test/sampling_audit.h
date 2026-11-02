// Trusted archive controller for X10. Numerical Sampling receives no archive or hidden RGB.
// Included after the existing map-audit scan/helpers; not a renderer component.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/sampling.h"
namespace SP = Rend2LightRecon::Core::Sampling;
struct SamplingSelection
{
	SP::Plan plan;
	std::array<std::vector<Selected>, 3> folds;
	uint64_t reads = 0, poolCandidates = 0, poolDropped = 0, footprintWork = 0;
	std::map<Id, SP::Geometry> geometry;
	std::shared_ptr<std::vector<Hidden::Sample>> localTraining;
};
struct SamplingReference
{
	SurfaceLightingObservation surface;
	Id id = 0;
	size_t entry = 0, offset = 0;
	double represented = 0;
	bool encodingKnown = false;
};
static Disk::Digest SamplingPartitionKey()
{
	return Key("X10 immutable original stock split: seed20261031,256WU,origin17/31/47,3*radius+1; "
			   "roles never reassigned");
}
static void SamplingQualityJson(std::ostream &o, const SP::Quality &q, bool correlationKnown = true)
{
	o << "{\"samples\":" << q.samples
	  << ",\"effective_groups\":" << (correlationKnown ? std::to_string(q.effectiveGroups) : "null")
	  << ",\"effective_group_meaning\":\"geometry correlation proxy, not independent Fisher "
		 "information\""
	  << ",\"surfaces\":" << q.surfaces << ",\"materials\":" << q.materials
	  << ",\"normal_bins\":" << q.normalBins << ",\"blocks\":" << q.blocks
	  << ",\"selected_receiver_area\":" << q.receiverArea
	  << ",\"represented_area_not_transport\":" << q.representedArea
	  << ",\"signal_evaluated\":" << (q.signalEvaluated ? "true" : "false")
	  << ",\"positive\":" << (q.signalEvaluated ? std::to_string(q.positive) : "null")
	  << ",\"censored\":" << (q.signalEvaluated ? std::to_string(q.censored) : "null")
	  << ",\"largest_material_group\":" << q.maxSameMaterial
	  << ",\"informative_materials\":" << q.informativeMaterials << ",\"contrast\":" << q.contrast
	  << ",\"baseline_world\":" << q.baselineWorld << ",\"normal_diversity\":" << q.normalDiversity
	  << ",\"reasons\":" << q.reasons
	  << ",\"admitted\":" << (q.discoveryAdmitted ? "true" : "false")
	  << ",\"independent_evaluation_geometry\":"
	  << (q.validationGeometryAvailable ? "true" : "false") << '}';
}
static SP::Quality SamplingMeasured(const std::vector<Selected> &rows,
									const Bounds *bounds = nullptr)
{
	SP::Quality q;
	q.signalEvaluated = true;
	std::set<Id> surfaces, materials, blocks;
	std::set<unsigned> normals;
	std::map<Id, std::vector<const Selected *>> byMaterial;
	V lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300), first;
	bool have = false;
	for (const auto &v : rows)
	{
		if (bounds && !InSelection(*bounds, v.receiver.world))
			continue;
		++q.samples;
		surfaces.insert(v.receiver.surface);
		materials.insert(v.receiver.material);
		blocks.insert(v.block);
		byMaterial[v.receiver.material].push_back(&v);
		V n(v.receiver.geometricNormal);
		unsigned code = unsigned(std::lround((n.x + 1) * 4)) * 81 +
						unsigned(std::lround((n.y + 1) * 4)) * 9 +
						unsigned(std::lround((n.z + 1) * 4));
		normals.insert(code);
		if (!have)
		{
			first = n;
			have = true;
		}
		q.normalDiversity = std::max(q.normalDiversity, 1 - Dot(first, n));
		bool positive = false, clipped = false;
		for (unsigned c = 0; c < 3; ++c)
		{
			unsigned b = v.luxel.colour.raw.bytes[c];
			positive |= b > 0 && b < 255;
			clipped |= b == 255;
			lo[c] = std::min(lo[c], V(v.receiver.world)[c]);
			hi[c] = std::max(hi[c], V(v.receiver.world)[c]);
		}
		q.positive += positive;
		q.censored += clipped;
		q.receiverArea += v.area;
	}
	for (const auto &m : byMaterial)
	{
		q.maxSameMaterial = std::max<uint64_t>(q.maxSameMaterial, m.second.size());
		bool useful = false;
		for (unsigned c = 0; c < 3; ++c)
		{
			double low = 1e300, high = 0;
			size_t positive = 0;
			for (auto v : m.second)
			{
				unsigned b = v->luxel.colour.raw.bytes[c];
				if (b == 255)
					continue;
				low = std::min(low, double(b));
				high = std::max(high, double(b));
				positive += b > 0;
			}
			if (positive >= 4)
			{
				double contrast = (high - low) / (high + 1);
				q.contrast = std::max(q.contrast, contrast);
				useful |= contrast >= .03;
			}
		}
		q.informativeMaterials += useful;
	}
	q.surfaces = surfaces.size();
	q.materials = materials.size();
	q.blocks = blocks.size();
	q.normalBins = normals.size();
	q.effectiveGroups = q.samples;
	q.baselineWorld = q.samples ? Length(hi - lo) : 0;
	q.directionalSupport = q.normalDiversity > .05;
	q.discoveryAdmitted = q.samples >= 6 && q.informativeMaterials && q.positive >= 4;
	return q;
}
static SamplingSelection HierarchicalSelect(const O::Dataset &data, const O::Sources &s,
											const std::array<std::vector<Selected>, 3> &old,
											const std::string &folder, const ReadContext &read,
											bool retainLocalTraining = false)
{
	auto start = std::chrono::steady_clock::now();
	SamplingSelection result;
	constexpr size_t poolLimit = 32768;
	using Chart = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>;
	std::map<Id, SamplingReference> pool;
	std::map<Chart, Id> keys;
	std::map<uint32_t, size_t> primary;
	for (size_t i = 0; i < s.images->Images().size(); ++i)
	{
		const auto &im = s.images->Images()[i];
		if (im.role == LightmapRole::Lighting && !im.missing)
			primary.emplace(im.logicalIndex, i);
	}
	for (size_t entry = 0; entry < data.Directory().size(); ++entry)
		if (data.Directory()[entry].kind == 15)
		{
			Need(!read.cancellation || !read.cancellation->IsCancelled(), "Sampling cancelled");
			std::vector<O::Luxel> tile;
			Check(data.ReadLuxels(entry, tile));
			result.reads += tile.size();
			Need(result.reads <= 8ull * 1024 * 1024, "Sampling original observation read budget");
			for (size_t offset = 0; offset < tile.size(); ++offset)
			{
				const auto &l = tile[offset];
				const auto &g = l.surface;
				if (!primary.count(g.logicalLightmap) || primary[g.logicalLightmap] != g.image ||
					g.surface >= s.geometry->Surfaces().size())
					continue;
				const auto &surface = s.geometry->Surfaces()[g.surface];
				if (surface.motion != SurfaceMotion::StaticWorld ||
					surface.kind != BakedSurfaceKind::Face || g.coverage != LuxelCoverage::Mapped ||
					!l.classification.diffuseFitCandidate || l.classification.style.id != 0 ||
					!g.positionKnown || !g.normalOrientationKnown || !g.shadingNormalKnown ||
					g.areaWorldSquared <= 0 || l.colour.raw.encoding != Decode::Encoding::Rgb8 ||
					(g.issues & (LuxelOverlap | LuxelDuplicate | LuxelMultipleCharts |
								 LuxelMixedFootprint | LuxelPrecisionExceeded)) ||
					Length(V(g.shadingNormal) - V(g.geometricNormal)) >= 1e-4)
					continue;
				Chart chart(g.surface, g.logicalLightmap, g.x / 2, g.y / 2);
				Core::GeometryKey k;
				k.Digest(s.metadata.map.sha256);
				k.U(g.surface);
				k.U(g.logicalLightmap);
				k.U(g.x / 2);
				k.U(g.y / 2);
				Id priority = Number(k.Finish());
				auto found = pool.find(priority);
				if (found == pool.end() && pool.size() >= poolLimit &&
					priority > pool.rbegin()->first)
				{
					++result.poolDropped;
					continue;
				}
				Id id = Number(O::SampleIdentity(s.metadata.map, s.images->Images()[g.image], g,
												 l.classification.style.id));
				if (found != pool.end())
				{
					found->second.represented += g.areaWorldSquared;
					if (found->second.id < id)
						continue;
				}
				SamplingReference r;
				r.surface = g;
				r.id = id;
				r.entry = entry;
				r.offset = offset;
				r.encodingKnown = l.colour.resolvedTransfer != Decode::Transfer::Unknown &&
								  l.colour.resolvedPrimaries != Decode::Primaries::Unknown;
				r.represented =
					found == pool.end() ? g.areaWorldSquared : found->second.represented;
				pool[priority] = r;
				if (pool.size() > poolLimit)
				{
					pool.erase(std::prev(pool.end()));
					++result.poolDropped;
				}
			}
		}
	result.poolCandidates = pool.size();
	std::vector<SP::Geometry> geometry;
	std::map<Id, SamplingReference> reference;
	uint64_t gap = 0, invalid = 0;
	for (const auto &item : pool)
	{
		const auto &r = item.second;
		const auto &g = r.surface;
		const auto &surface = s.geometry->Surfaces()[g.surface];
		result.footprintWork += surface.indices.size();
		if (result.footprintWork > 128000000)
		{
			++result.poolDropped;
			continue;
		}
		double area = 0;
		Chunk<Bake::FootprintTriangle> footprint;
		try
		{
			footprint =
				SourceFootprint::Footprint(surface, g.styleSlot, g.x, g.y, g.geometricNormal, area);
		}
		catch (const std::exception &)
		{
			++invalid;
			continue;
		}
		if (std::abs(area - g.areaWorldSquared) > 1e-4 * std::max(1., g.areaWorldSquared))
		{
			++invalid;
			continue;
		}
		double radius = 0;
		for (size_t i = 0; i < footprint.count; ++i)
			for (auto v : footprint.data[i].world)
				radius = std::max(radius, Length(V(v) - V(g.positionWorld)));
		radius += g.mappingErrorBoundWorld + 1e-4;
		Id block = 0;
		unsigned role = Split(g.positionWorld, radius, block);
		if (role == 3)
		{
			++gap;
			continue;
		}
		SP::Geometry p;
		p.id = r.id;
		p.surface = g.surface + 1;
		p.material = g.materialIndex + 1;
		p.block = block;
		p.position = g.positionWorld;
		p.normal = g.geometricNormal;
		p.receiverArea = area;
		p.footprintRadius = radius;
		p.representedArea = r.represented;
		p.role = SP::Role(role);
		p.encodingKnown = r.encodingKnown;
		geometry.push_back(p);
		reference.emplace(p.id, r);
	}
	SP::Policy policy;
	policy.allowConditionalEncodedSignal = true;
	policy.maxJobs = 8;
	policy.maxTotalRows = 2048;
	policy.maxPool = poolLimit;
	policy.sourcePoolComplete = !result.poolDropped;
	Check(SP::Build(s.metadata.map.sha256, SamplingPartitionKey(), geometry, policy, read,
					result.plan));
	// Only whitelisted TRAINING identifiers cross this interface. The trusted
	// archive reader releases each tile before the numerical signal stage.
	std::map<size_t, std::vector<Id>> trainEntries;
	for (auto &g : result.plan.geometry)
		if (g.role == SP::Role::Training)
			trainEntries[reference.at(g.id).entry].push_back(g.id);
	std::vector<SP::Signal> signals;
	if(retainLocalTraining)result.localTraining=std::make_shared<std::vector<Hidden::Sample>>();
	for (const auto &group : trainEntries)
	{
		std::vector<O::Luxel> tile;
		Check(data.ReadLuxels(group.first, tile));
		result.reads += tile.size();
		Need(result.reads <= 16ull * 1024 * 1024, "Training-only sampling read budget");
		for (auto id : group.second)
		{
			const auto &r = reference.at(id);
			const auto &l = tile.at(r.offset);
			SP::Signal v;
			v.id = id;
			v.encodingKnown = r.encodingKnown;
			for (unsigned c = 0; c < 3; ++c)
			{
				v.rgb[c] = l.colour.raw.bytes[c];
				v.censored[c] = l.colour.raw.bytes[c] == 255;
			}
			signals.push_back(v);
			if(retainLocalTraining){
				const auto&g=r.surface;const auto&support=*std::lower_bound(result.plan.geometry.begin(),result.plan.geometry.end(),id,[](const SP::Geometry&a,Id b){return a.id<b;});
				Hidden::Sample sample;sample.receiver.sample=id;sample.receiver.surface=g.surface+1;sample.receiver.material=g.materialIndex+1;
				sample.receiver.world=g.positionWorld;sample.receiver.geometricNormal=g.geometricNormal;sample.receiver.shadingNormal=g.shadingNormal;
				sample.receiver.mappedPlanar=sample.receiver.compilerPlacementCertified=true;sample.receiver.mappingErrorWorld=g.mappingErrorBoundWorld;
				sample.receiver.footprint=SourceFootprint::Footprint(s.geometry->Surfaces()[g.surface],g.styleSlot,g.x,g.y,g.geometricNormal,sample.areaWorldSquared);
				sample.correlationGroup=support.correlation;sample.statisticalWeight=1;sample.eligible=true;sample.encodingAttested=r.encodingKnown;
				// Unknown direct/GI mixture already yields AmbiguousSourceOrTransport.
				// No independent transport-only evidence exists for every stock texel.
				sample.transportSuspected=false;
				sample.materialUncertainty=l.classification.materialUncertainty;for(unsigned c=0;c<3;++c)sample.rgb[c]=l.colour.raw.bytes[c];
				result.localTraining->push_back(std::move(sample));
			}
		}
	}
	SP::Plan refined;
	Check(SP::Refine(result.plan, signals, read, refined));
	result.plan = std::move(refined);
	for (auto g : result.plan.geometry)
		result.geometry.emplace(g.id, g);
	std::array<std::map<Id, SP::Selection>, 3> chosen;
	for (auto purpose : {SP::Purpose::CoarseCoverage, SP::Purpose::Discovery})
		for (auto v : result.plan.global[size_t(purpose)])
			chosen[0].emplace(v.id, v);
	for (auto v : result.plan.global[size_t(SP::Purpose::ModelValidation)])
		chosen[1].emplace(v.id, v);
	for (auto v : result.plan.global[size_t(SP::Purpose::FinalHeldout)])
		chosen[2].emplace(v.id, v);
	std::map<size_t, std::vector<std::pair<unsigned, SP::Selection>>> entries;
	for (unsigned role = 0; role < 3; ++role)
	{
		std::vector<SP::Selection> normalized;
		for (auto v : chosen[role])
			normalized.push_back(v.second);
		SP::Normalize(result.plan.geometry, normalized);
		for (auto v : normalized)
			entries[reference.at(v.id).entry].push_back({role, v});
	}
	for (auto &group : entries)
	{
		std::vector<O::Luxel> tile;
		Check(data.ReadLuxels(group.first, tile));
		result.reads += tile.size();
		Need(result.reads <= 24ull * 1024 * 1024, "Frozen evaluator sampling read budget");
		for (auto selected : group.second)
		{
			unsigned role = selected.first;
			const auto &v = selected.second;
			const auto &r = reference.at(v.id);
			const auto &g = r.surface;
			const auto &support = result.geometry.at(v.id);
			Selected x;
			x.id = v.id;
			x.block = support.block;
			x.role = role;
			x.luxel = tile.at(r.offset);
			x.receiver.sample = v.id;
			x.receiver.surface = g.surface + 1;
			x.receiver.material = g.materialIndex + 1;
			x.receiver.style = 0;
			x.receiver.world = g.positionWorld;
			x.receiver.geometricNormal = g.geometricNormal;
			x.receiver.shadingNormal = g.shadingNormal;
			x.receiver.mappedPlanar = x.receiver.compilerPlacementCertified = true;
			x.receiver.mappingErrorWorld = g.mappingErrorBoundWorld;
			x.receiver.footprint =
				SourceFootprint::Footprint(s.geometry->Surfaces()[g.surface], g.styleSlot, g.x, g.y,
										   g.geometricNormal, x.area);
			x.radius = support.footprintRadius;
			x.weight = v.statisticalWeight;
			result.folds[role].push_back(std::move(x));
		}
	}
	for (auto &fold : result.folds)
		std::sort(fold.begin(), fold.end(),
				  [](const Selected &a, const Selected &b) { return a.id < b.id; });
	std::ofstream t(folder + "/hierarchical-geometry.tsv");
	t << std::setprecision(17)
	  << "id\trole\tblock\tcorrelation\tsurface\tmaterial\tactual_area\trepresented_area_not_"
		 "physical\tstatistical_weight\tx\ty\tz\tradius\n";
	for (const auto &fold : result.folds)
		for (const auto &x : fold)
		{
			const auto &g = result.geometry.at(x.id);
			t << x.id << '\t' << x.role << '\t' << x.block << '\t' << g.correlation << '\t'
			  << g.surface << '\t' << g.material << '\t' << x.area << '\t' << g.representedArea
			  << '\t' << x.weight << '\t' << g.position.x << '\t' << g.position.y << '\t'
			  << g.position.z << '\t' << g.footprintRadius << '\n';
		}
	std::ofstream o(folder + "/sampling.json");
	o << std::setprecision(17)
	  << "{\"schema\":\"org.openjk.hierarchical-sampling-x10.v1\",\"version\":" << SP::kVersion
	  << ",\"map_sha256\":" << Q(Disk::Hex(s.metadata.map.sha256))
	  << ",\"plan_key\":" << Q(Disk::Hex(result.plan.key))
	  << ",\"geometry_key\":" << Q(Disk::Hex(result.plan.geometryKey))
	  << ",\"partition_key\":" << Q(Disk::Hex(result.plan.partitionKey))
	  << ",\"folds_changed\":false,\"gaps_shrunk\":false,\"heldout_rgb_used_for_selection\":false,"
		 "\"historical_profile\":\"UnknownProfile\",\"contrast_domain\":\"encoded-channel proxy "
		 "under explicit assumption; not irradiance\",\"pool_candidates\":"
	  << result.poolCandidates << ",\"pool_dropped\":" << result.poolDropped
	  << ",\"pool_gaps\":" << gap << ",\"invalid_footprints\":" << invalid
	  << ",\"read_records\":" << result.reads << ",\"footprint_work\":" << result.footprintWork
	  << ",\"accounted_bytes\":" << result.plan.report.bytes
	  << ",\"work\":" << result.plan.report.work
	  << ",\"eligible_neighbourhoods\":" << result.plan.report.eligibleNeighbourhoods
	  << ",\"deferred_neighbourhoods\":" << result.plan.report.deferredNeighbourhoods
	  << ",\"complete\":" << (result.plan.report.complete ? "true" : "false")
	  << ",\"median_footprint_diameter\":" << result.plan.report.medianFootprintDiameter
	  << ",\"correlation_world\":" << result.plan.report.correlationWorld
	  << ",\"local_cell_world\":" << result.plan.report.localCellWorld
	  << ",\"coarse_cell_world\":" << result.plan.report.coarseCellWorld << ",\"old_whole_map\":[";
	for (unsigned role = 0; role < 3; ++role)
	{
		if (role)
			o << ',';
		SamplingQualityJson(o, SamplingMeasured(old[role]), false);
	}
	o << "],\"new_whole_map\":[";
	for (unsigned role = 0; role < 3; ++role)
	{
		if (role)
			o << ',';
		SamplingQualityJson(o, SamplingMeasured(result.folds[role]));
	}
	o << "],\"purposes\":{";
	for (size_t p = 0; p < size_t(SP::Purpose::Count); ++p)
		o << (p ? "," : "") << Q(SP::PurposeName(SP::Purpose(p))) << ':'
		  << result.plan.global[p].size();
	o << "},\"jobs\":[";
	for (size_t i = 0; i < result.plan.jobs.size(); ++i)
	{
		const auto &job = result.plan.jobs[i];
		o << (i ? "," : "") << "{\"id\":" << job.id << ",\"training\":";
		SamplingQualityJson(o, job.training);
		o << ",\"validation_geometry\":";
		SamplingQualityJson(o, job.validation);
		o << ",\"heldout_geometry\":";
		SamplingQualityJson(o, job.heldout);
		auto frozenSignal = [&](unsigned role, SP::Purpose purpose) {
			std::set<Id> ids;
			for (auto v : job.selected[size_t(purpose)])
				ids.insert(v.id);
			std::vector<Selected> rows;
			for (const auto &v : result.folds[role])
				if (ids.count(v.id))
					rows.push_back(v);
			return SamplingMeasured(rows);
		};
		o << ",\"validation_frozen_signal\":";
		SamplingQualityJson(o, frozenSignal(1, SP::Purpose::ModelValidation));
		o << ",\"heldout_frozen_signal\":";
		SamplingQualityJson(o, frozenSignal(2, SP::Purpose::FinalHeldout));
		o << ",\"old_training_same_neighbourhood\":";
		SamplingQualityJson(o, SamplingMeasured(old[0], &job.receivers), false);
		o << ",\"reasons\":" << job.reasons << '}';
	}
	o << "],\"physical_source_recovery\":false,\"selection_seconds\":"
	  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '}';
	return result;
}
static std::string SamplingProposals(const SamplingSelection &sampling, const O::Sources &s,
									 const std::shared_ptr<const StaticVisibility> &rays,
									 const std::array<std::vector<Selected>, 3> &old,
									 const ReadContext &read)
{
	std::ostringstream o;
	o << "{\"scope\":\"bounded conditional source proposals, not physical existence\",\"jobs\":[";
	size_t attempts = 0;
	for (const auto &job : sampling.plan.jobs)
	{
		if (!job.training.discoveryAdmitted)
			continue;
		if (attempts == 4)
			break;
		auto run = [&](bool hierarchical) {
			Hidden::Request r;
			r.map = s.metadata.map;
			r.units = s.metadata.units;
			r.analysisBounds = s.domain->Source().worldBounds;
			r.domain = s.domain;
			r.domainKey = sampling.plan.partitionKey;
			r.geometry.visibility = rays;
			r.geometry.geometryKey = rays->CertificationKey();
			r.geometry.materialKey = sampling.plan.geometryKey;
			r.geometry.observationPolicyKey = sampling.plan.key;
			Bake::Recipe recipe;
			recipe.fast = false;
			r.profiles = {Bake::NetRadiantProfile(recipe)};
			r.options.allowAssumedProfiles = r.options.allowAssumedEncoding = true;
			r.options.maxSamples = 4096;
			r.options.maxAreas = 1;
			r.options.samplesPerArea = 64;
			r.options.maxNodesPerArea = 64;
			r.options.coarseDivisions = 4;
			r.options.refinementRounds = 0;
			r.options.spots = false;
			r.options.maxWork = 16000000;
			r.options.maxRays = 100000;
			r.options.maxBytes = 128ull << 20;
			Hidden::SearchArea area;
			area.id = job.id;
			area.receivers = job.receivers;
			area.positions = job.sourceContext;
			for (unsigned c = 0; c < 3; ++c)
			{
				V lo(area.positions.minWorld), hi(area.positions.maxWorld);
				lo[c] = std::max(lo[c], V(r.analysisBounds.minWorld)[c]);
				hi[c] = std::min(hi[c], V(r.analysisBounds.maxWorld)[c]);
				area.positions.minWorld = lo.Float();
				area.positions.maxWorld = hi.Float();
			}
			area.reason = "X10 frozen geometry neighbourhood, no source answer";
			r.areas = {area};
			auto samples = std::make_shared<std::vector<Hidden::Sample>>();
			std::set<Id> discovery;
			for (auto v : job.selected[size_t(SP::Purpose::Discovery)])
				discovery.insert(v.id);
			for (const auto &v : hierarchical ? sampling.folds[0] : old[0])
				if ((!hierarchical || discovery.count(v.id)) &&
					InSelection(job.receivers, v.receiver.world))
				{
					Hidden::Sample h;
					h.receiver = v.receiver;
					h.areaWorldSquared = v.area;
					h.eligible = true;
					h.transportSuspected = true;
					for (unsigned c = 0; c < 3; ++c)
						h.rgb[c] = v.luxel.colour.raw.bytes[c];
					if (hierarchical)
					{
						h.correlationGroup = sampling.geometry.at(v.id).correlation;
						h.statisticalWeight = v.weight;
					}
					samples->push_back(h);
				}
			r.training = Lease(samples);
			Hidden::Result result;
			auto status = Hidden::Generate(r, read, result);
			std::ostringstream text;
			text << "{\"code\":" << unsigned(status.code) << ",\"reason\":" << Q(status.message)
				 << ",\"input\":" << samples->size()
				 << ",\"candidates\":" << result.candidates.size()
				 << ",\"rays\":" << result.report.rays
				 << ",\"work\":" << result.report.traversalWork
				 << ",\"complete\":" << (result.report.complete ? "true" : "false")
				 << ",\"areas\":[";
			for (size_t i = 0; i < result.report.areas.size(); ++i)
			{
				const auto &a = result.report.areas[i];
				text << (i ? "," : "") << "{\"selected\":" << a.selected
					 << ",\"admission\":" << Q(a.admission)
					 << ",\"unsupported\":" << a.unsupportedReceivers << '}';
			}
			text << "]}";
			return text.str();
		};
		o << (attempts ? "," : "") << "{\"id\":" << job.id << ",\"old\":" << run(false)
		  << ",\"hierarchical\":" << run(true) << '}';
		++attempts;
	}
	o << "],\"attempted_jobs\":" << attempts
	  << ",\"source_xyz_accuracy\":null,\"stock_source_recall\":null}";
	return o.str();
}
