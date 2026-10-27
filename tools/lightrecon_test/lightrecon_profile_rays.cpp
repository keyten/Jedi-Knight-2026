// Offline X03 visibility witness. No source intensities, fitting or renderer state.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace O = Rend2LightRecon::Core::Offline;
static void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 4)
			throw std::runtime_error("usage: lightrecon_profile_rays public-geometry.jkobs "
									 "queries.tsv|--hints output.tsv");
		std::shared_ptr<const O::Dataset> data;
		Check(O::OpenFile(argv[1], O::Options(), data));
		O::Sources s;
		O::RegionSnapshot graph;
		Check(data->Load(s, graph));
		if (s.images || s.grid || !s.geometry)
			throw std::runtime_error("Sanitized CPU geometry required");
		if (std::string(argv[2]) == "--hints")
		{
			std::ofstream output(argv[3]);
			if (s.evidence)
				for (const auto &e : s.evidence->Evidence())
					if (e.origin == EvidenceOrigin::GlowSurface && e.staticEligible &&
						e.positionKnown)
						output << e.id << ' ' << e.position.x << ' ' << e.position.y << ' '
							   << e.position.z << ' ' << (e.orientationKnown ? e.outward.x : 0)
							   << ' ' << (e.orientationKnown ? e.outward.y : 0) << ' '
							   << (e.orientationKnown ? e.outward.z : 0) << '\n';
			if (!output)
				throw std::runtime_error("Hint output failure");
			return 0;
		}
		ReadContext ctx;
		ctx.maxBytes = 256ull * 1024 * 1024;
		VisibilityBuildOptions options;
		options.patchToleranceWorld = data->Info().visibilityPatchToleranceWorld;
		std::shared_ptr<const StaticVisibility> rays;
		Check(StaticVisibility::Build(s.geometry, options, ctx, rays, s.domain));
		std::ifstream input(argv[2]);
		std::ofstream output(argv[3]);
		std::string word, identity;
		if (!(input >> word >> identity) || word != "map" ||
			identity != Disk::Hex(s.metadata.map.sha256))
			throw std::runtime_error("Query map fingerprint mismatch");
		std::string line;
		std::getline(input, line);
		uint64_t count = 0, unknown = 0;
		while (std::getline(input, line))
		{
			if (line.empty())
				continue;
			if (line.size() > 4096 || ++count > 16384)
				throw std::runtime_error("Batch limit exceeded");
			std::istringstream row(line);
			std::string id, key;
			uint32_t surface;
			uint64_t source;
			RayQuery query;
			if (!(row >> id >> key >> query.startWorld.x >> query.startWorld.y >>
				  query.startWorld.z >> query.bias.geometricNormal.x >>
				  query.bias.geometricNormal.y >> query.bias.geometricNormal.z >> surface >>
				  source >> query.endWorld.x >> query.endWorld.y >> query.endWorld.z))
				throw std::runtime_error("Malformed query row");
			query.originSurface = Id(surface) + 1;
			query.bias.normalKnown = true;
			if (s.evidence)
				for (const auto &e : s.evidence->Evidence())
					if (e.id == source && !e.surfaces.empty())
					{
						query.targetSurface = Id(e.surfaces[0]) + 1;
						query.targetToleranceWorld = .05;
						break;
					}
			RayResult result;
			Check(rays->Query(query, ctx, result));
			const char *state =
				result.state == VisibilityState::Visible
					? "visible"
					: result.state == VisibilityState::Blocked ? "blocked" : "unknown";
			unknown += result.state == VisibilityState::Unknown;
			output << id << '\t' << key << '\t' << state << '\t' << result.issues << '\t'
				   << result.firstHit.surface << '\n';
		}
		if (!output)
			throw std::runtime_error("Ray witness write failed");
		std::cout << "queries=" << count << " unknown=" << unknown
				  << " global-complete=" << rays->Report().complete << '\n';
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
