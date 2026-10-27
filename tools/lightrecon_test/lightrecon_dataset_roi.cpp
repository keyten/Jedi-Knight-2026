// Bounded candidate-specific geometry information probes, not source fitting.
#include "lightrecon/offline.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace O = Rend2LightRecon::Core::Offline;
namespace
{
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
double Distance(Vec3 a, Vec3 b)
{
	double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
	return std::sqrt(x * x + y * y + z * z);
}
struct Roi
{
	Vec3 centre;
	double radius;
	uint64_t mapped = 0, fit = 0, saturated = 0, unknownMaterial = 0, normalBins[6] = {},
			 assigned = 0;
	std::set<uint32_t> surfaces;
	std::map<Disk::Digest, O::Luxel> sample;
};
} // namespace
int main(int argc, char **argv)
{
	try
	{
		if (argc < 6 || (argc - 2) % 4)
			throw std::runtime_error("usage: lightrecon_dataset_roi dataset [cx cy cz radius]...");
		std::vector<Roi> rois;
		for (int i = 2; i < argc; i += 4)
		{
			Roi r;
			r.centre = Vec3(std::stof(argv[i]), std::stof(argv[i + 1]), std::stof(argv[i + 2]));
			r.radius = std::stod(argv[i + 3]);
			if (!std::isfinite(r.radius) || r.radius <= 0 ||
				!std::isfinite(r.centre.x + r.centre.y + r.centre.z))
				throw std::runtime_error("Invalid ROI");
			rois.push_back(r);
		}
		O::Options o;
		std::shared_ptr<const O::Dataset> data;
		Check(O::OpenFile(argv[1], o, data));
		O::Sources s;
		O::RegionSnapshot graph;
		Check(data->Load(s, graph));
		ReadContext ctx;
		ctx.maxBytes = o.maxResidentBytes;
		std::shared_ptr<const StaticVisibility> rays;
		std::shared_ptr<const LightingRegionGraph> regions;
		Check(O::ReplayGeometry(s, graph, ctx, rays, regions));
		for (size_t i = 0; i < data->Directory().size(); ++i)
			if (data->Directory()[i].kind == 15)
			{
				std::vector<O::Luxel> tile;
				Check(data->ReadLuxels(i, tile));
				for (const auto &l : tile)
				{
					const auto &g = l.surface;
					if (!g.positionKnown || g.coverage != LuxelCoverage::Mapped)
						continue;
					for (auto &r : rois)
					{
						const auto p = g.positionWorld, c = r.centre;
						if (std::max({std::abs(double(p.x - c.x)), std::abs(double(p.y - c.y)),
									  std::abs(double(p.z - c.z))}) > r.radius)
							continue;
						++r.mapped;
						r.fit += l.classification.diffuseFitCandidate;
						r.saturated += l.colour.encoded.censoredChannels != 0;
						r.unknownMaterial += l.classification.materialUncertainty != 0;
						r.surfaces.insert(g.surface);
						if (g.normalOrientationKnown)
						{
							float n[3] = {g.geometricNormal.x, g.geometricNormal.y,
										  g.geometricNormal.z};
							int a = 0;
							for (int j = 1; j < 3; ++j)
								if (std::abs(n[j]) > std::abs(n[a]))
									a = j;
							++r.normalBins[a * 2 + (n[a] >= 0 ? 1 : 0)];
						}
						SurfaceRegionSides sides;
						if (g.normalOrientationKnown)
							Check(regions->AssignSurfaceSides(p, g.geometricNormal,
															  Id(g.surface) + 1, ctx, sides));
						r.assigned += sides.positive.region != 0 || sides.negative.region != 0;
						auto id = O::SampleIdentity(s.metadata.map, s.images->Images()[g.image], g,
													l.classification.style.id);
						r.sample.emplace(id, l);
						if (r.sample.size() > 64)
							r.sample.erase(--r.sample.end());
					}
				}
			}
		std::cout << std::setprecision(10) << "{\"map_sha256\":\""
				  << Disk::Hex(s.metadata.map.sha256) << "\",\"rois\":[";
		bool comma = false;
		for (const auto &r : rois)
		{
			if (comma)
				std::cout << ',';
			comma = true;
			std::vector<const EmitterEvidence *> candidates;
			if (s.evidence)
				for (const auto &c : s.evidence->Candidates())
				{
					const auto &e = s.evidence->Evidence()[c.primary];
					if (e.positionKnown && e.staticEligible &&
						Distance(e.position, r.centre) <= r.radius * 1.7321 + 256)
						candidates.push_back(&e);
				}
			uint64_t clear = 0, blocked = 0, unknown = 0, work = 0, observations = 0;
			uint64_t skyReached = 0, skyBlocked = 0, skyUnknown = 0;
			std::sort(candidates.begin(), candidates.end(),
					  [&](const EmitterEvidence *a, const EmitterEvidence *b) {
						  double x = Distance(a->position, r.centre),
								 y = Distance(b->position, r.centre);
						  return x != y ? x < y : a->id < b->id;
					  });
			size_t candidateCount = candidates.size();
			if (candidates.size() > 8)
				candidates.resize(8); // SAME source IDs for every receiver
			double minimum = 1e30, maximum = 0;
			std::set<unsigned> masks;
			for (const auto &sample : r.sample)
			{
				const auto &g = sample.second.surface;
				RayQuery sky;
				sky.startWorld = g.positionWorld;
				sky.endWorld =
					Vec3(g.positionWorld.x, g.positionWorld.y, g.positionWorld.z + 32768);
				sky.purpose = RayPurpose::Sky;
				sky.originSurface = Id(g.surface) + 1;
				sky.bias.normalKnown = g.normalOrientationKnown;
				sky.bias.geometricNormal = g.geometricNormal;
				RayResult skyResult;
				auto skyError = rays->Query(sky, ctx, skyResult);
				if (!skyError || skyResult.state == VisibilityState::Unknown)
					++skyUnknown;
				else if (skyResult.sky == SkyReach::Reached)
					++skyReached;
				else
					++skyBlocked;
				std::vector<std::pair<double, const EmitterEvidence *>> nearest;
				for (auto e : candidates)
					nearest.emplace_back(Distance(g.positionWorld, e->position), e);
				unsigned mask = 0;
				for (size_t j = 0; j < std::min<size_t>(nearest.size(), 8); ++j)
				{
					RayQuery q;
					q.startWorld = g.positionWorld;
					q.endWorld = nearest[j].second->position;
					q.originSurface = Id(g.surface) + 1;
					q.bias.normalKnown = g.normalOrientationKnown;
					q.bias.geometricNormal = g.geometricNormal;
					if (!nearest[j].second->surfaces.empty())
					{
						q.targetSurface = Id(nearest[j].second->surfaces[0]) + 1;
						q.targetToleranceWorld = .05;
					}
					RayResult hit;
					auto error = rays->Query(q, ctx, hit);
					if (!error)
					{
						++unknown;
						continue;
					}
					clear += hit.state == VisibilityState::Visible;
					blocked += hit.state == VisibilityState::Blocked;
					unknown += hit.state == VisibilityState::Unknown;
					mask |= unsigned(hit.state) << (j * 2);
					work += hit.nodeTests + hit.primitiveTests;
					minimum = std::min(minimum, nearest[j].first);
					maximum = std::max(maximum, nearest[j].first);
				}
				if (!nearest.empty())
				{
					masks.insert(mask);
					++observations;
				}
			}
			std::cout << "{\"centre\":[" << r.centre.x << ',' << r.centre.y << ',' << r.centre.z
					  << "],\"radius\":" << r.radius << ",\"mapped\":" << r.mapped
					  << ",\"conditional_fit\":" << r.fit << ",\"surfaces\":" << r.surfaces.size()
					  << ",\"censored\":" << r.saturated
					  << ",\"material_uncertain\":" << r.unknownMaterial
					  << ",\"region_assigned\":" << r.assigned << ",\"normal_bins\":[";
			for (unsigned i = 0; i < 6; ++i)
			{
				if (i)
					std::cout << ',';
				std::cout << r.normalBins[i];
			}
			std::cout << "],\"candidate_priors\":" << candidateCount
					  << ",\"sampled_candidates\":" << candidates.size()
					  << ",\"sampled_receivers\":" << observations << ",\"ray_visible\":" << clear
					  << ",\"ray_blocked\":" << blocked << ",\"ray_unknown\":" << unknown
					  << ",\"ray_work\":" << work << ",\"nearest_candidate_distance_range\":["
					  << (maximum ? minimum : 0) << ',' << maximum << ']'
					  << ",\"visibility_pattern_proxy\":" << masks.size()
					  << ",\"upward_sky_reached\":" << skyReached
					  << ",\"upward_sky_blocked\":" << skyBlocked
					  << ",\"upward_sky_unknown\":" << skyUnknown << '}';
		}
		std::cout << "]}\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
