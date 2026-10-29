// Sanitized TrainingView adapter for S04. No rendered images or private answers.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/hidden_candidates.h"
#include "lightrecon/offline.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace H = Rend2LightRecon::Core::Hidden;
namespace O = Rend2LightRecon::Core::Offline;
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
void Vector(std::ostream &s, Vec3 v) { s << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
std::string Quote(const std::string &s)
{
	std::string out = "\"";
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
			out += '\\';
		if (c >= 32)
			out += char(c);
	}
	return out + '"';
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 3)
			throw std::runtime_error(
				"usage: lightrecon_hidden_tool sanitized-geometry.jkobs training.request");
		auto start = std::chrono::steady_clock::now();
		O::Options limits;
		limits.maxResidentBytes = 512ull * 1024 * 1024;
		std::shared_ptr<const O::Dataset> data;
		Check(O::OpenFile(argv[1], limits, data));
		O::Sources s;
		O::RegionSnapshot graph;
		Check(data->Load(s, graph));
		if (s.images || s.grid || !s.geometry || s.metadata.hints != O::HintPolicy::BlindBenchmark)
			throw std::runtime_error(
				"Sanitized blind geometry required; full observation/answer archives are forbidden");
		for (const auto &surface : s.geometry->Surfaces())
			for (const auto &v : surface.vertices)
				if (v.colourPresent)
					throw std::runtime_error("Geometry must not carry hidden vertex RGB");
		std::ifstream f(argv[2]);
		f.seekg(0, std::ios::end);
		if (!f || f.tellg() > 2 * 1024 * 1024)
			throw std::runtime_error("Training request size limit");
		f.seekg(0);
		std::string magic, map;
		f >> magic >> map;
		if (magic != "JKHIDDEN1" || map != Disk::Hex(s.metadata.map.sha256))
			throw std::runtime_error("Training schema/map fingerprint mismatch");
		H::Request r;
		r.map = s.metadata.map;
		r.units = s.metadata.units;
		r.analysisBounds.known = true;
		f >> r.analysisBounds.minWorld.x >> r.analysisBounds.minWorld.y >> r.analysisBounds.minWorld.z >>
			r.analysisBounds.maxWorld.x >> r.analysisBounds.maxWorld.y >> r.analysisBounds.maxWorld.z;
		unsigned spots = 0, direct = 0, encoding = 0;
		f >> r.options.maxNodesPerArea >> r.options.coarseDivisions >> r.options.refinementRounds >> spots >>
			direct >> encoding;
		if (spots > 1 || direct > 1 || encoding > 1)
			throw std::runtime_error("Invalid explicit assumptions");
		r.options.spots = spots;
		r.options.directOnlyAttested = direct;
		r.options.allowAssumedEncoding = encoding;
		r.options.allowAssumedProfiles = true;
		r.options.maxSamples = 512;
		r.options.samplesPerArea = 64;
		r.options.retainedPerChannelProfile = 4;
		size_t count = 0;
		f >> count;
		if (count > 32)
			throw std::runtime_error("Search-area limit");
		for (size_t i = 0; i < count; ++i)
		{
			H::SearchArea a;
			f >> a.id;
			a.positions.known = a.receivers.known = true;
			f >> a.positions.minWorld.x >> a.positions.minWorld.y >> a.positions.minWorld.z >>
				a.positions.maxWorld.x >> a.positions.maxWorld.y >> a.positions.maxWorld.z >>
				a.receivers.minWorld.x >> a.receivers.minWorld.y >> a.receivers.minWorld.z >>
				a.receivers.maxWorld.x >> a.receivers.maxWorld.y >> a.receivers.maxWorld.z;
			a.reason = "Predeclared blind training ROI, no answer coordinates";
			r.areas.push_back(a);
		}
		std::map<uint32_t, const BakedSurface *> surfaces;
		std::map<std::string, Id> materials;
		for (const auto &v : s.geometry->Surfaces())
		{
			surfaces[v.ordinal] = &v;
			materials.emplace(v.material, 0);
		}
		Id material = 0;
		for (auto &v : materials)
			v.second = ++material;
		auto samples = std::make_shared<std::vector<H::Sample>>();
		f >> count;
		if (count > 512)
			throw std::runtime_error("Training sample count limit");
		for (size_t i = 0; i < count; ++i)
		{
			H::Sample sample;
			unsigned surface = 0, attested = 0;
			int rgb[3];
			f >> sample.receiver.sample >> surface >> sample.receiver.world.x >> sample.receiver.world.y >>
				sample.receiver.world.z >> sample.receiver.geometricNormal.x >>
				sample.receiver.geometricNormal.y >> sample.receiver.geometricNormal.z >> rgb[0] >> rgb[1] >>
				rgb[2] >> sample.areaWorldSquared >> attested;
			if (!f || attested > 1 || !surfaces.count(surface))
				throw std::runtime_error("Malformed training receiver/reference");
			const auto &geometry = *surfaces.at(surface);
			sample.receiver.surface = Id(surface) + 1;
			sample.receiver.material = materials.at(geometry.material);
			sample.receiver.shadingNormal = sample.receiver.geometricNormal;
			sample.receiver.mappedPlanar =
				geometry.kind == BakedSurfaceKind::Face || geometry.kind == BakedSurfaceKind::Triangles;
			sample.receiver.compilerPlacementCertified =
				true; // caller's mapped training recipe, explicitly conditional traces
			sample.materialUncertainty = MaterialUncertaintyFlags(geometry.materialFacts);
			sample.transportSuspected = (geometry.materialFacts.signals & MaterialReflection) != 0;
			sample.eligible = ClassifySurface(geometry, 0, false).diffuseFitCandidate;
			sample.encodingAttested = attested;
			for (int c = 0; c < 3; ++c)
			{
				if (rgb[c] < 0 || rgb[c] > 255)
					throw std::runtime_error("RGB8 range");
				sample.rgb[c] = uint8_t(rgb[c]);
			}
			samples->push_back(sample);
		}
		size_t profiles = 0;
		f >> profiles;
		if (profiles < 1 || profiles > 4)
			throw std::runtime_error("Explicit profile list required");
		for (size_t i = 0; i < profiles; ++i)
		{
			unsigned selector;
			f >> selector;
			if (selector > 1)
				throw std::runtime_error("Unknown profile is not a falloff law");
			Bake::Recipe recipe;
			recipe.fast = false;
			recipe.falloff = selector ? Bake::Falloff::Linear : Bake::Falloff::InverseSquare;
			auto p = Bake::NetRadiantProfile(recipe);
			// Probe recipes disable FAST and use generic static traces. Even on
			// known fixtures these are conditional candidate models.
			p.applicability = Bake::Applicability::AssumedForDataset;
			r.profiles.push_back(p);
		}
		std::string extra;
		if (!f || (f >> extra))
			throw std::runtime_error("Truncated/trailing request data");
		ReadContext read;
		read.maxBytes = 256ull * 1024 * 1024;
		VisibilityBuildOptions options;
		options.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
		std::shared_ptr<const StaticVisibility> rays;
		Check(StaticVisibility::Build(s.geometry, options, read, rays, s.domain));
		r.geometry.visibility = rays;
		r.geometry.geometryKey = rays->CertificationKey();
		r.domain = s.domain;
		if (s.domain)
		{
			std::string identity =
				"BspRegionDomain-v" + std::to_string(kLightingRegionGraphVersion) + ":" + map;
			r.domainKey = Disk::Hash(identity.data(), identity.size());
		}
		r.training = VectorChunk<H::Sample>(samples, 0, samples->size());
		auto prepared = std::chrono::steady_clock::now();
		H::Result result;
		Error code = H::Generate(r, read, result);
		auto end = std::chrono::steady_clock::now();
		std::cout << std::setprecision(12)
				  << "{\"schema\":\"org.openjk.hidden-candidate-diagnostic.v1\",\"code\":" << int(code.code)
				  << ",\"reason\":" << Quote(code.message) << ",\"key\":\"" << Disk::Hex(result.key)
				  << "\",\"map_sha256\":\"" << map
				  << "\",\"historical_profile\":\"UnknownProfile\",\"runtime_sources\":0,\"reconstructed\":"
					 "false,\"search_complete\":"
				  << (result.report.complete ? "true" : "false")
				  << ",\"geometry_global_complete\":" << (rays->Report().complete ? "true" : "false")
				  << ",\"geometry_bytes\":" << rays->Report().ownedBytes
				  << ",\"accounted_bytes\":" << result.report.accountedPeakBytes
				  << ",\"rays\":" << result.report.rays << ",\"forward_calls\":" << result.report.forwardCalls
				  << ",\"pairs\":" << result.report.sourceReceiverPairs
				  << ",\"work\":" << result.report.traversalWork << ",\"areas\":[";
		for (size_t i = 0; i < result.report.areas.size(); ++i)
		{
			if (i)
				std::cout << ',';
			const auto &a = result.report.areas[i];
			std::cout << "{\"id\":" << a.id << ",\"requested\":" << a.requested
					  << ",\"selected\":" << a.selected << ",\"saturated\":" << a.saturated
					  << ",\"excluded\":" << a.excluded
					  << ",\"unsupported_receivers\":" << a.unsupportedReceivers << ",\"nodes\":" << a.nodes
					  << ",\"solid_rejected\":" << a.rejectedSolid
					  << ",\"outside_rejected\":" << a.rejectedOutside
					  << ",\"unknown_region\":" << a.unknownRegion
					  << ",\"unknown_relations\":" << a.unknownVisibility << ",\"retained\":" << a.retained
					  << ",\"limited\":" << ((a.searchLimited || a.sampleLimited) ? "true" : "false") << '}';
		}
		std::cout << "],\"candidates\":[";
		for (size_t i = 0; i < result.hypotheses.size(); ++i)
		{
			if (i)
				std::cout << ',';
			const auto &h = result.hypotheses[i];
			const auto &v = result.evidence[i];
			std::cout << "{\"id\":\"" << h.candidate << "\",\"position\":";
			Vector(std::cout, v.position);
			std::cout << ",\"outward\":";
			Vector(std::cout, v.outward);
			std::cout << ",\"area\":" << h.area << ",\"region\":" << h.region << ",\"kind\":" << int(h.kind)
					  << ",\"source_kind\":" << int(h.sourceKind) << ",\"feasibility\":" << int(h.feasibility)
					  << ",\"context\":" << int(h.context) << ",\"method\":" << int(h.method)
					  << ",\"profile\":" << Quote(h.profile) << ",\"channel\":" << h.channel
					  << ",\"channel_mask\":" << h.evidenceChannels << ",\"bsp_leaf\":" << h.bsp.leaf
					  << ",\"bsp_cluster\":" << h.bsp.cluster << ",\"bsp_area\":" << h.bsp.area
					  << ",\"shape_score\":";
			if (h.scoreAvailable)
				std::cout << h.score;
			else
				std::cout << "null";
			std::cout << ",\"surfaces\":" << h.distinctSurfaces
					  << ",\"normal_diversity\":" << h.normalDiversity << ",\"visible\":" << h.visible
					  << ",\"blocked\":" << h.blocked << ",\"unknown\":" << h.unknown
					  << ",\"underconstrained\":" << (h.positionUnderconstrained ? "true" : "false")
					  << ",\"intensity\":null,\"existence_confidence\":null,\"observations\":[";
			for (size_t j = 0; j < h.observations.size(); ++j)
			{
				if (j)
					std::cout << ',';
				const auto &o = h.observations[j];
				std::cout << "[" << o.observation << ',' << o.surface << ',' << int(o.relation) << ','
						  << o.issues << ']';
			}
			std::cout << "]}";
		}
		std::cout << "],\"diagnostics\":[";
		for (size_t i = 0; i < result.report.diagnostics.size(); ++i)
		{
			if (i)
				std::cout << ',';
			std::cout << Quote(result.report.diagnostics[i]);
		}
		std::cout << "],\"resources\":{\"load_build_seconds\":"
				  << std::chrono::duration<double>(prepared - start).count()
				  << ",\"search_seconds\":" << std::chrono::duration<double>(end - prepared).count()
				  << "}}\n";
		return 0; // inspect explicit code: Unavailable is not a successful reconstruction
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 2;
	}
}
