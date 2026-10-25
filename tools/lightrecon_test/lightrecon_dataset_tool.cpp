// Standalone dataset inspection/validation and self-authored fixture pipeline.
#include "offline_fixtures.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <iomanip>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Rend2LightRecon::Core::Offline;
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(std::to_string(unsigned(e.code)) + ": " + e.message);
}
int main(int argc, char **argv)
{
	try
	{
		if (argc < 2)
			throw std::runtime_error("usage: lightrecon_dataset_tool inspect|validate|trace <file> "
									 "; fixture <scene> <observations> <answer> ; list ; version");
		std::string command = argv[1];
		if (command == "version")
		{
#ifdef _MSC_VER
			std::cout << "org.openjk.offline-dataset-tool.v1 MSVC " << _MSC_FULL_VER << "\n";
#else
			std::cout << "org.openjk.offline-dataset-tool.v1 GNU/Clang " << __VERSION__ << "\n";
#endif
			return 0;
		}
		if (command == "list")
		{
			for (auto &n : OfflineFixtures::Names())
				std::cout << n << '\n';
			return 0;
		}
		if (command == "fixture")
		{
			if (argc != 5)
				throw std::runtime_error(
					"fixture requires scene, observation filename and SEPARATE answer filename");
			std::string observation = argv[3], answer = argv[4];
			auto parent = [](const std::string &p) { return p.substr(0, p.find_last_of("/\\")); };
			if (parent(observation) == parent(answer))
				throw std::runtime_error("Ground truth must be outside the solver-input directory");
			auto f = OfflineFixtures::Make(argv[2]);
			Options options;
			options.mappedImages = 0;
			ReadContext context;
			context.maxBytes = options.maxResidentBytes;
			Summary summary;
			Check(ExportFile(observation, f.sources, options, context, summary));
			std::ofstream out(answer, std::ios::binary);
			if (!out || !out.write(f.answer.data(), std::streamsize(f.answer.size())))
				throw std::runtime_error(
					"Cannot write separate answer file (create its parent directory)");
			std::cout << "fixture=" << argv[2] << " bytes=" << summary.fileBytes
					  << " samples=" << summary.mappedSamples << " q3map2-executed=0\n";
			return 0;
		}
		if (argc < 3)
			throw std::runtime_error("Dataset filename required");
		std::shared_ptr<const Offline::Dataset> data;
		Check(OpenFile(argv[2], Options(), data));
		if (command == "inspect")
		{
			std::cout << InspectText(*data);
			Sources source;
			RegionSnapshot graph;
			Check(data->Load(source, graph));
			uint64_t sourceAccounting = data->Counts().rawImageBytes;
			if (source.geometry)
				sourceAccounting += source.geometry->Report().bytes;
			if (source.grid)
				sourceAccounting += source.grid->Report().bytes;
			if (source.domain)
				sourceAccounting += source.domain->OwnedBytes();
			if (source.evidence)
				sourceAccounting += source.evidence->Report().bytes;
			sourceAccounting += graph.report.ownedBytes;
			std::cout << "source-component-memory-estimate=" << sourceAccounting
					  << " (excludes allocator/reader/scratch and imported graph copy; not RSS)\n";
			if (source.domain)
			{
				const auto &b = source.domain->Source().worldBounds;
				std::cout << "bounds=" << b.minWorld.x << ',' << b.minWorld.y << ',' << b.minWorld.z
						  << " .. " << b.maxWorld.x << ',' << b.maxWorld.y << ',' << b.maxWorld.z
						  << " collision-bytes=" << source.domain->OwnedBytes() << '\n';
			}
			uint64_t invalid = 0, unknownMaterial = 0, styles[256] = {}, emissive = 0;
			if (source.geometry)
				for (const auto &s : source.geometry->Surfaces())
				{
					invalid += s.kind == BakedSurfaceKind::Unsupported;
					unknownMaterial += !s.materialFacts.available;
					emissive += (s.materialFacts.signals & MaterialGlow) != 0;
					for (uint8_t style : s.styles)
						if (style != 255)
							++styles[style];
				}
			std::cout << "unsupported-geometry=" << invalid
					  << " unknown-material=" << unknownMaterial
					  << " emissive-surfaces=" << emissive << '\n';
			for (unsigned i = 0; i < 256; ++i)
				if (styles[i])
					std::cout << "style[" << i << "]=" << styles[i] << '\n';
			uint64_t partitions[3] = {}, coverage[5] = {}, reasons[32] = {}, maximumTile = 0;
			for (size_t i = 0; i < data->Directory().size(); ++i)
				if (data->Directory()[i].kind == 15)
				{
					std::vector<Luxel> tile;
					Check(data->ReadLuxels(i, tile));
					maximumTile = std::max(maximumTile, uint64_t(tile.size() * sizeof(Luxel)));
					for (const auto &sample : tile)
					{
						++partitions[unsigned(sample.partition)];
						++coverage[unsigned(sample.surface.coverage)];
						for (unsigned bit = 0; bit < 32; ++bit)
							reasons[bit] += (sample.surface.issues >> bit) & 1;
					}
				}
			std::cout << "partitions train=" << partitions[0] << " validation=" << partitions[1]
					  << " held-out=" << partitions[2]
					  << " maximum-tile-object-bytes=" << maximumTile << '\n';
			for (unsigned i = 0; i < 5; ++i)
				std::cout << "coverage[" << i << "]=" << coverage[i] << '\n';
			for (unsigned i = 0; i < 32; ++i)
				if (reasons[i])
					std::cout << "luxel-issue[" << i << "]=" << reasons[i] << '\n';
			std::cout << "region-free=" << graph.report.freeCells
					  << " solid=" << graph.report.solidCells
					  << " outside=" << graph.report.outsideCells
					  << " sky=" << graph.report.skyCells
					  << " unknown=" << graph.report.unknownCells
					  << " snapshot-bytes=" << graph.report.ownedBytes
					  << " build-peak-estimate=" << graph.report.peakBuildBytes << '\n';
			return 0;
		}
		if (command == "validate")
		{
			uint64_t checked = 0;
			Check(data->Validate(true, checked));
			std::cout << "validated=" << checked
					  << " source/decoded/UV/world/style/partition records; no GL, engine or "
						 "reconstruction\n";
			return 0;
		}
		if (command == "samples")
		{
			if (argc != 4)
				throw std::runtime_error("samples requires file train|validation|heldout");
			std::string name = argv[3];
			if (name != "train" && name != "validation" && name != "heldout")
				throw std::runtime_error("Invalid partition name");
			Partition selected =
				name == "train" ? Partition::Train
								: name == "validation" ? Partition::Validation : Partition::HeldOut;
			uint64_t checked = 0;
			Check(data->Validate(true, checked));
			std::cout << std::setprecision(9);
			for (size_t i = 0; i < data->Directory().size(); ++i)
				if (data->Directory()[i].kind == 15)
				{
					std::vector<Luxel> tile;
					Check(data->ReadLuxels(i, tile));
					for (const auto &s : tile)
						if (s.partition == selected && s.surface.positionKnown)
						{
							const auto &g = s.surface;
							std::cout
								<< "{\"image\":" << g.image << ",\"x\":" << g.x << ",\"y\":" << g.y
								<< ",\"surface\":" << g.surface
								<< ",\"material\":" << g.materialIndex
								<< ",\"style\":" << unsigned(s.classification.style.id)
								<< ",\"world\":[" << g.positionWorld.x << ',' << g.positionWorld.y
								<< ',' << g.positionWorld.z << "],\"normal\":["
								<< g.geometricNormal.x << ',' << g.geometricNormal.y << ','
								<< g.geometricNormal.z << "],\"area_world2\":" << g.areaWorldSquared
								<< ",\"raw_rgb8\":[" << unsigned(s.colour.raw.bytes[0]) << ','
								<< unsigned(s.colour.raw.bytes[1]) << ','
								<< unsigned(s.colour.raw.bytes[2])
								<< "],\"encoding\":" << unsigned(s.colour.raw.encoding)
								<< ",\"source_state\":" << unsigned(s.colour.source.state)
								<< ",\"source_relative_rgb\":[" << s.colour.source.rgb.x << ','
								<< s.colour.source.rgb.y << ',' << s.colour.source.rgb.z
								<< "],\"channel_flags\":[" << s.colour.channels[0] << ','
								<< s.colour.channels[1] << ',' << s.colour.channels[2]
								<< "],\"classification_reasons\":" << s.classification.reasons
								<< "}\n";
						}
				}
			return 0;
		}
		if (command == "trace")
		{
			if (argc != 9)
				throw std::runtime_error("trace requires file sx sy sz ex ey ez");
			Sources source;
			RegionSnapshot graph;
			Check(data->Load(source, graph));
			if (!source.geometry)
				throw std::runtime_error("Geometry unavailable");
			ReadContext ctx;
			ctx.maxBytes = 256ull * 1024 * 1024;
			VisibilityBuildOptions options;
			options.patchToleranceWorld = data->Info().visibilityPatchToleranceWorld;
			options.maxBytes = ctx.maxBytes;
			std::shared_ptr<const StaticVisibility> rays;
			Check(StaticVisibility::Build(source.geometry, options, ctx, rays, source.domain));
			RayQuery q;
			q.startWorld = Vec3(float(atof(argv[3])), float(atof(argv[4])), float(atof(argv[5])));
			q.endWorld = Vec3(float(atof(argv[6])), float(atof(argv[7])), float(atof(argv[8])));
			RayResult result;
			Check(rays->Query(q, ctx, result));
			std::cout << "visibility=" << uint32_t(result.state) << " sky=" << uint32_t(result.sky)
					  << " surface=" << result.firstHit.surface << " issues=" << result.issues
					  << " bvh-bytes=" << rays->Report().ownedBytes << '\n';
			return 0;
		}
		throw std::runtime_error("Unknown dataset command");
	}
	catch (const std::exception &e)
	{
		std::cerr << "dataset error: " << e.what() << '\n';
		return 1;
	}
}
