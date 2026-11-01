// Blind X10 protocol: all-role geometry, training-only RGB, no evaluator/answer handle.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/bake_math.h"
#include "lightrecon/sampling.h"
#include <set>
#include <sstream>
#include <tuple>
static int SamplingProtocol(const O::Sources &sources, std::istream &input)
{
	namespace S = Rend2LightRecon::Core::Sampling;
	using namespace Bake::Detail;
	std::string partition;
	size_t budget = 0, jobs = 0, count = 0;
	unsigned spots = 0;
	input >> partition >> budget >> jobs >> spots;
	if (partition.size() != 64 || budget < 8 || budget > 64 || jobs > 8 || !jobs || spots > 1)
		throw std::runtime_error("Sampling protocol policy");
	std::map<std::string, Id> materials;
	std::map<Id, const BakedSurface *> surfaces;
	for (const auto &s : sources.geometry->Surfaces())
	{
		surfaces[s.ordinal] = &s;
		materials.emplace(s.material, 0);
	}
	Id material = 0;
	for (auto &m : materials)
		m.second = ++material;
	std::vector<S::Geometry> geometry;
	input >> count;
	if (count > 32768)
		throw std::runtime_error("Sampling geometry limit");
	for (size_t i = 0; i < count; ++i)
	{
		S::Geometry g;
		Id surface = 0;
		unsigned role = 0, known = 0;
		input >> g.id >> surface >> g.block >> role >> g.position.x >> g.position.y >>
			g.position.z >> g.normal.x >> g.normal.y >> g.normal.z >> g.receiverArea >>
			g.footprintRadius >> known;
		if (!input || !surfaces.count(surface) || role > 3 || known > 1)
			throw std::runtime_error("Sampling geometry reference");
		g.surface = surface + 1;
		g.material = materials.at(surfaces.at(surface)->material);
		g.role = S::Role(role);
		g.encodingKnown = known;
		g.representedArea = g.receiverArea;
		g.supported = surfaces.at(surface)->kind == BakedSurfaceKind::Face;
		geometry.push_back(g);
	}
	std::vector<S::Signal> signals;
	input >> count;
	if (count > 32768)
		throw std::runtime_error("Sampling signal limit");
	for (size_t i = 0; i < count; ++i)
	{
		S::Signal s;
		unsigned known = 0;
		input >> s.id >> s.rgb[0] >> s.rgb[1] >> s.rgb[2] >> known;
		s.encodingKnown = known;
		for (unsigned c = 0; c < 3; ++c)
		{
			if (s.rgb[c] < 0 || s.rgb[c] > 255)
				throw std::runtime_error("Sampling RGB8 range");
			s.censored[c] = s.rgb[c] == 255;
		}
		signals.push_back(s);
	}
	std::string extra;
	if (!input || (input >> extra))
		throw std::runtime_error("Sampling truncated/trailing request");
	ReadContext read;
	read.maxBytes = 256ull << 20;
	S::Policy policy;
	policy.maxJobs = jobs;
	policy.discoveryRows = budget;
	policy.coarseRows = 16;
	policy.maxTotalRows = 2048;
	policy.allowConditionalEncodedSignal = true;
	S::Plan initial, plan;
	Check(S::Build(sources.metadata.map.sha256, Disk::Hash(partition.data(), partition.size()),
				   geometry, policy, read, initial));
	// Correlation pruning can discard redundant training IDs; never supply another role.
	std::set<Id> kept, trainingIds;
	for (auto &g : initial.geometry)
		if (g.role == S::Role::Training)
			kept.insert(g.id);
	for (auto &g : geometry)
		if (g.role == S::Role::Training)
			trainingIds.insert(g.id);
	std::vector<S::Signal> distinct;
	for (auto s : signals)
	{
		if (!trainingIds.count(s.id))
			throw std::runtime_error("Attempted hidden target read in sampler");
		if (kept.count(s.id))
			distinct.push_back(s);
	}
	Check(S::Refine(initial, distinct, read, plan));
	std::map<Id, S::Signal> colours;
	for (auto s : distinct)
		colours.emplace(s.id, s);
	VisibilityBuildOptions build;
	build.patchToleranceWorld = sources.metadata.visibilityPatchToleranceWorld;
	std::shared_ptr<const StaticVisibility> rays;
	Check(StaticVisibility::Build(sources.geometry, build, read, rays, sources.domain));
	std::map<Id, S::Geometry> records;
	for (auto g : plan.geometry)
		records.emplace(g.id, g);
	std::map<Id, S::Signal> allColours;
	for (auto s : signals)
		allColours.emplace(s.id, s);
	std::cout << std::setprecision(17)
			  << "{\"schema\":\"org.openjk.blind-sampling-proposals.v1\",\"plan_key\":"
			  << Quote(Disk::Hex(plan.key))
			  << ",\"geometry_key\":" << Quote(Disk::Hex(plan.geometryKey))
			  << ",\"heldout_colours_accessible\":false,\"budget\":" << budget << ",\"jobs\":[";
	for (size_t j = 0; j < plan.jobs.size(); ++j)
	{
		const auto &job = plan.jobs[j];
		auto run = [&](bool hierarchical) {
			H::Request r;
			r.map = sources.metadata.map;
			r.units = sources.metadata.units;
			r.analysisBounds = sources.domain->Source().worldBounds;
			r.domain = sources.domain;
			r.domainKey = plan.partitionKey;
			r.geometry.visibility = rays;
			r.geometry.geometryKey = rays->CertificationKey();
			r.geometry.materialKey = plan.geometryKey;
			r.geometry.observationPolicyKey = plan.key;
			r.options.maxSamples = 4096;
			r.options.maxAreas = 1;
			r.options.samplesPerArea = 64;
			r.options.maxNodesPerArea = 64;
			r.options.coarseDivisions = 4;
			r.options.refinementRounds = 0;
			r.options.spots = spots;
			r.options.allowAssumedEncoding = r.options.allowAssumedProfiles = true;
			r.options.maxBytes = 128ull << 20;
			r.options.maxWork = 16000000;
			r.options.maxRays = 100000;
			Bake::Recipe recipe;
			recipe.fast = false;
			r.profiles = {Bake::NetRadiantProfile(recipe)};
			H::SearchArea a;
			a.id = job.id;
			a.receivers = job.receivers;
			a.positions = job.sourceContext;
			for (unsigned c = 0; c < 3; ++c)
			{
				V lo(a.positions.minWorld), hi(a.positions.maxWorld);
				lo[c] = std::max(lo[c], V(r.analysisBounds.minWorld)[c]);
				hi[c] = std::min(hi[c], V(r.analysisBounds.maxWorld)[c]);
				a.positions.minWorld = lo.Float();
				a.positions.maxWorld = hi.Float();
			}
			a.reason = "X10 geometry-only fixture neighbourhood, no answer XYZ";
			r.areas = {a};
			auto inside = [&](Vec3 p) {
				return p.x >= a.receivers.minWorld.x && p.y >= a.receivers.minWorld.y &&
					   p.z >= a.receivers.minWorld.z && p.x < a.receivers.maxWorld.x &&
					   p.y < a.receivers.maxWorld.y && p.z < a.receivers.maxWorld.z;
			};
			std::vector<S::Selection> selected;
			if (hierarchical)
				selected = job.selected[size_t(S::Purpose::Discovery)];
			else
			{
				std::map<std::tuple<int, int, int, int>, Id> bins;
				for (auto g : geometry)
					if (g.role == S::Role::Training)
					{
						V n(g.normal);
						unsigned axis = 0;
						for (unsigned c = 1; c < 3; ++c)
							if (std::abs(n[c]) > std::abs(n[axis]))
								axis = c;
						auto key = std::make_tuple(int(std::floor(g.position.x / 512)),
												   int(std::floor(g.position.y / 512)),
												   int(std::floor(g.position.z / 512)),
												   int(axis * 2 + (n[axis] >= 0)));
						auto f = bins.find(key);
						if (f == bins.end() || g.id < f->second)
							bins[key] = g.id;
					}
				for (auto x : bins)
				{
					S::Selection v;
					v.id = x.second;
					selected.push_back(v);
				}
			}
			auto samples = std::make_shared<std::vector<H::Sample>>();
			for (auto v : selected)
			{
				auto lookup = std::find_if(geometry.begin(), geometry.end(),
										   [&](const S::Geometry &g) { return g.id == v.id; });
				if (lookup == geometry.end() || !inside(lookup->position) ||
					!allColours.count(v.id))
					continue;
				const auto &g = *lookup;
				H::Sample s;
				s.receiver.sample = g.id;
				s.receiver.surface = g.surface;
				s.receiver.material = g.material;
				s.receiver.world = g.position;
				s.receiver.geometricNormal = s.receiver.shadingNormal = g.normal;
				s.receiver.mappedPlanar = s.receiver.compilerPlacementCertified = true;
				s.areaWorldSquared = g.receiverArea;
				s.eligible = true;
				s.encodingAttested = g.encodingKnown;
				s.transportSuspected = true;
				for (unsigned c = 0; c < 3; ++c)
					s.rgb[c] = uint8_t(allColours.at(v.id).rgb[c]);
				if (hierarchical)
				{
					s.correlationGroup = v.correlation;
					s.statisticalWeight = v.statisticalWeight;
				}
				samples->push_back(s);
			}
			r.training = VectorChunk<H::Sample>(samples, 0, samples->size());
			H::Result result;
			auto error = H::Generate(r, read, result);
			std::ostringstream o;
			o << std::setprecision(17) << "{\"code\":" << unsigned(error.code)
			  << ",\"reason\":" << Quote(error.message) << ",\"receivers\":" << samples->size()
			  << ",\"rays\":" << result.report.rays << ",\"work\":" << result.report.traversalWork
			  << ",\"complete\":" << (result.report.complete ? "true" : "false")
			  << ",\"candidates\":[";
			for (size_t i = 0; i < result.hypotheses.size(); ++i)
			{
				const auto &h = result.hypotheses[i];
				o << (i ? "," : "") << "{\"id\":" << h.candidate << ",\"position\":";
				Vector(o, result.evidence[i].position);
				o << ",\"source_kind\":" << unsigned(h.sourceKind)
				  << ",\"kind\":" << unsigned(h.kind)
				  << ",\"underconstrained\":" << (h.positionUnderconstrained ? "true" : "false")
				  << '}';
			}
			o << "]}";
			return o.str();
		};
		std::cout << (j ? "," : "") << "{\"id\":" << job.id
				  << ",\"admitted\":" << (job.training.discoveryAdmitted ? "true" : "false")
				  << ",\"independent_geometry\":"
				  << (job.training.validationGeometryAvailable ? "true" : "false")
				  << ",\"reasons\":" << job.reasons << ",\"training\":" << job.training.samples
				  << ",\"same_material\":" << job.training.maxSameMaterial
				  << ",\"contrast\":" << job.training.contrast
				  << ",\"normal_bins\":" << job.training.normalBins
				  << ",\"validation\":" << job.validation.samples
				  << ",\"heldout\":" << job.heldout.samples << ",\"grid_radius_world\":"
				  << Length(V(job.sourceContext.maxWorld) - V(job.sourceContext.minWorld)) / 8
				  << ",\"validation_geometry_ids\":[";
		for (size_t i = 0; i < job.selected[size_t(S::Purpose::ModelValidation)].size(); ++i)
			std::cout << (i ? "," : "") << job.selected[size_t(S::Purpose::ModelValidation)][i].id;
		std::cout << "],\"heldout_geometry_ids\":[";
		for (size_t i = 0; i < job.selected[size_t(S::Purpose::FinalHeldout)].size(); ++i)
			std::cout << (i ? "," : "") << job.selected[size_t(S::Purpose::FinalHeldout)][i].id;
		std::cout << "],\"old\":" << run(false) << ",\"hierarchical\":" << run(true) << '}';
	}
	std::cout << "],\"purpose_counts\":{";
	for (size_t p = 0; p < size_t(S::Purpose::Count); ++p)
		std::cout << (p ? "," : "") << Quote(S::PurposeName(S::Purpose(p))) << ':'
				  << plan.global[p].size();
	std::cout << "},\"physical_recovery\":false}\n";
	return 0;
}
