// Historical X01 baseline adapter ONLY. Build against revision 71b23f943, never production.
#include "lightrecon/offline.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace O = Rend2LightRecon::Core::Offline;
static_assert(kLightingRegionGraphVersion == 1 && kStaticVisibilityVersion == 1 &&
				  kSurfaceObservationVersion == 4,
			  "This witness requires the unmodified X01 base core");
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
double Axis(Vec3 v, unsigned k)
{
	return k == 0 ? v.x : k == 1 ? v.y : v.z;
}
void Set(Vec3 &v, unsigned k, double x)
{
	(k == 0 ? v.x : k == 1 ? v.y : v.z) = float(x);
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 11)
			throw std::runtime_error("usage: v1-replay dataset bsp cx cy cz radius coarse minimum "
									 "patch-tolerance raw|flares");
		std::shared_ptr<const O::Dataset> data;
		Check(O::OpenFile(argv[1], O::Options(), data));
		O::Sources source;
		O::RegionSnapshot archive;
		Check(data->Load(source, archive));
		std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
		auto size = in.tellg();
		if (!in || size < 152 || size > 128 * 1024 * 1024)
			throw std::runtime_error("BSP limit");
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		in.seekg(0);
		in.read(reinterpret_cast<char *>(bytes.data()), size);
		if (!in || source.metadata.map.sha256 != Disk::Hash(bytes.data(), bytes.size()))
			throw std::runtime_error("BSP hash mismatch");
		auto u = [&](size_t p) {
			if (p > bytes.size() - 4)
				throw std::runtime_error("BSP field bounds");
			return uint32_t(bytes[p]) | uint32_t(bytes[p + 1]) << 8 | uint32_t(bytes[p + 2]) << 16 |
				   uint32_t(bytes[p + 3]) << 24;
		};
		size_t first = u(8 + 13 * 8), count = u(12 + 13 * 8);
		if (first > bytes.size() || count > bytes.size() - first || count % 148 ||
			count / 148 != source.geometry->Surfaces().size())
			throw std::runtime_error("Surface lump mismatch");
		std::string policy = argv[10];
		if (policy != "raw" && policy != "flares")
			throw std::runtime_error("Unknown policy");
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Check(capture.SetMap(source.metadata.map));
		for (auto s : source.geometry->Surfaces())
		{
			if (policy == "flares" && u(first + s.ordinal * 148 + 8) == 4)
				s.materialFacts.signals |= MaterialNoDraw;
			Check(capture.Add(std::move(s)));
		}
		Check(capture.Freeze());
		source.geometry = capture.Acquire();
		ReadContext ctx;
		ctx.maxBytes = 512ull * 1024 * 1024;
		VisibilityBuildOptions vis;
		vis.patchToleranceWorld = std::stod(argv[9]);
		vis.maxBytes = 256ull * 1024 * 1024;
		std::shared_ptr<const StaticVisibility> rays;
		Check(StaticVisibility::Build(source.geometry, vis, ctx, rays, source.domain));
		RegionOptions options;
		auto world = source.domain->Source().worldBounds;
		double radius = std::stod(argv[6]);
		options.bounds.known = true;
		for (unsigned k = 0; k < 3; ++k)
		{
			double centre = std::string(argv[3 + k]) == "auto"
								? (Axis(world.minWorld, k) + Axis(world.maxWorld, k)) * .5
								: std::stod(argv[3 + k]);
			Set(options.bounds.minWorld, k, std::max(Axis(world.minWorld, k), centre - radius));
			Set(options.bounds.maxWorld, k, std::min(Axis(world.maxWorld, k), centre + radius));
		}
		options.coarseCellWorld = float(std::stod(argv[7]));
		options.minimumCellWorld = float(std::stod(argv[8]));
		options.constrictionWorld = options.coarseCellWorld * .5f;
		options.skySamplesPerRegion = 4;
		options.maxBytes = options.maxWork = 256ull * 1024 * 1024;
		RegionBuildInput input;
		input.identity = source.metadata.map;
		input.geometry = rays;
		input.domain = source.domain;
		input.dynamic = source.dynamic;
		std::shared_ptr<const LightingRegionGraph> graph;
		Check(LightingRegionGraph::Build(input, options, ctx, graph));
		uint64_t bspFree = 0, local = 0, global = 0;
		double volumes[6] = {};
		for (auto &cell : graph->Cells())
		{
			Bounds box;
			box.known = true;
			double volume = 1;
			for (unsigned k = 0; k < 3; ++k)
			{
				Set(box.minWorld, k,
					Axis(options.bounds.minWorld, k) +
						cell.origin[k] * double(options.minimumCellWorld));
				Set(box.maxWorld, k,
					Axis(options.bounds.minWorld, k) +
						(cell.origin[k] + cell.units) * double(options.minimumCellWorld));
				volume *=
					std::max(0., std::min(Axis(box.maxWorld, k), Axis(options.bounds.maxWorld, k)) -
									 Axis(box.minWorld, k));
			}
			volumes[unsigned(cell.space)] += volume;
			SpaceClass space;
			uint64_t work;
			Check(source.domain->ClassifyBox(box, space, work));
			if (space == SpaceClass::Free || space == SpaceClass::Exterior)
			{
				++bspFree;
				GeometryBoxResult result;
				Check(rays->OverlapBox(box, ctx, result));
				local += result.intersects;
				global += !result.intersects && !rays->Report().complete;
			}
		}
		auto &r = graph->Report();
		auto &v = rays->Report();
		std::cout << "{\"base\":\"71b23f943\",\"schema\":1,\"bsp_sha256\":\""
				  << Disk::Hex(source.metadata.map.sha256) << "\",\"capture_policy\":\"" << policy
				  << "\",\"cells\":" << graph->Cells().size() << ",\"free\":" << r.freeCells
				  << ",\"solid\":" << r.solidCells << ",\"unknown\":" << r.unknownCells
				  << ",\"regions\":" << r.regions << ",\"components\":" << r.staticComponents
				  << ",\"apertures\":" << r.apertures << ",\"bsp_free\":" << bspFree
				  << ",\"local_invalidated\":" << local << ",\"global_only_invalidated\":" << global
				  << ",\"unbounded_unsupported\":" << v.unboundedUnsupported
				  << ",\"complete\":" << (v.complete ? "true" : "false")
				  << ",\"free_volume\":" << volumes[0] + volumes[5]
				  << ",\"solid_volume\":" << volumes[1] << ",\"unknown_volume\":" << volumes[4]
				  << "}\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
