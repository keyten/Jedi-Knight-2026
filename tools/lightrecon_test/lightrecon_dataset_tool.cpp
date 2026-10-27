// Standalone dataset inspection/validation and self-authored fixture pipeline.
#include "offline_fixtures.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <cmath>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Rend2LightRecon::Core::Offline;
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(std::to_string(unsigned(e.code)) + ": " + e.message);
}
std::vector<uint8_t> ReadSmallFile(const char *path)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	auto size = in.tellg();
	if (!in || size < 0 || size > 32 * 1024 * 1024)
		throw std::runtime_error("Missing or oversized validation input (32 MiB limit)");
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	in.seekg(0);
	if (size && !in.read(reinterpret_cast<char *>(bytes.data()), size))
		throw std::runtime_error("Short validation input read");
	return bytes;
}
void AuditBsp(const Offline::Dataset &data, const char *path)
{
	auto b = ReadSmallFile(path);
	auto need = [](bool value, const char *message) {
		if (!value)
			throw std::runtime_error(message);
	};
	need(b.size() >= 152 && !std::memcmp(b.data(), "RBSP\1\0\0\0", 8), "Expected JA RBSP v1");
	need(data.Info().map.bytes == b.size() &&
			 data.Info().map.sha256 == Disk::Hash(b.data(), b.size()),
		 "Original source BSP fingerprint mismatch");
	auto u = [&](size_t pos) {
		need(pos <= b.size() && b.size() - pos >= 4, "Truncated BSP field");
		return uint32_t(b[pos]) | uint32_t(b[pos + 1]) << 8 | uint32_t(b[pos + 2]) << 16 |
			   uint32_t(b[pos + 3]) << 24;
	};
	auto f = [&](size_t pos) {
		auto bits = u(pos);
		float value;
		std::memcpy(&value, &bits, 4);
		return value;
	};
	struct Lump
	{
		size_t offset, bytes;
	} lumps[18];
	for (size_t i = 0; i < 18; ++i)
	{
		lumps[i] = {u(8 + i * 8), u(12 + i * 8)};
		need(lumps[i].offset <= b.size() && lumps[i].bytes <= b.size() - lumps[i].offset,
			 "Invalid BSP lump");
	}
	Sources s;
	RegionSnapshot graph;
	Check(data.Load(s, graph));
	need(s.images && s.grid && s.geometry, "Audit requires original images/grid/geometry");
	uint64_t imageBytes = 0, vertices = 0, deluxeDirections = 0, unusedNanUv = 0;
	for (const auto &image : s.images->Images())
	{
		if(image.missing) continue; // explicit absent external/deluxe alternative
		need(image.encoding == LightmapEncoding::BspRgb8 && image.bytes,
			 "Source audit needs embedded images; external inputs require their own file hashes");
		const auto &pixels = *image.bytes;
		need(image.sourceOffset <= b.size() && pixels.size() <= b.size() - image.sourceOffset &&
				 !std::memcmp(pixels.data(), b.data() + image.sourceOffset, pixels.size()),
			 "Original pixel bytes changed");
		imageBytes += pixels.size();
	}
	need(imageBytes == lumps[14].bytes, "Not every original colour/deluxe image was captured");
	auto equal = [&](const std::vector<uint8_t> &raw, unsigned lump) {
		need(raw.size() == lumps[lump].bytes &&
				 !std::memcmp(raw.data(), b.data() + lumps[lump].offset, raw.size()),
			 "Original grid bytes/indirection changed");
	};
	equal(s.grid->Records(), 15);
	equal(s.grid->Indices(), 17);
	need(s.geometry->Surfaces().size() == lumps[13].bytes / 148, "Surface ordinal/count mismatch");
	for (const auto &surface : s.geometry->Surfaces())
	{
		size_t ds = lumps[13].offset + surface.ordinal * 148;
		need(surface.materialIndex == u(ds), "BSP material ID changed");
		need(surface.materialIndex < lumps[1].bytes / 72, "Invalid source material ID");
		const char *name =
			reinterpret_cast<const char *>(b.data() + lumps[1].offset + surface.materialIndex * 72);
		size_t len = 0;
		while (len < 64 && name[len])
			++len;
		need(surface.material == std::string(name, len), "Source material identity changed");
		for (unsigned slot = 0; slot < 4; ++slot)
			need(uint32_t(surface.lightmap[slot]) == u(ds + 36 + slot * 4) &&
					 surface.styles[slot] == b[ds + 28 + slot] &&
					 surface.vertexStyles[slot] == b[ds + 32 + slot],
				 "Original style/lightmap identity changed");
		for (const auto &v : surface.vertices)
		{
			need(v.sourceVertex < lumps[10].bytes / 80, "Invalid source vertex ID");
			size_t pos = lumps[10].offset + v.sourceVertex * 80;
			need(v.position.x == f(pos) && v.position.y == f(pos + 4) && v.position.z == f(pos + 8),
				 "Original world coordinates changed");
			need(v.shadingNormal.x == f(pos + 52) && v.shadingNormal.y == f(pos + 56) &&
					 v.shadingNormal.z == f(pos + 60),
				 "Original shading normal changed");
			for (unsigned slot = 0; slot < 4; ++slot)
			{
				auto sameUv=[&](double captured,float original) {
					// Stock RBSPs contain NaNs in UNUSED style slots. NaN != NaN
					// is not evidence that an active source coordinate was altered.
					if((surface.styles[slot]==255 || surface.lightmap[slot]<0) &&
					   std::isnan(captured) && std::isnan(original)) {
						++unusedNanUv; return true;
					}
					return captured==original;
				};
				if(!sameUv(v.lightmap[slot].u,f(pos+20+slot*8)) || !sameUv(v.lightmap[slot].v,f(pos+24+slot*8)) ||
				   std::memcmp(v.colour[slot],b.data()+pos+64+slot*4,4)) {
					std::cerr << "source mismatch surface=" << surface.ordinal << " vertex=" << v.sourceVertex << " slot=" << slot
						<< " uv=" << v.lightmap[slot].u << ',' << v.lightmap[slot].v << " expected=" << f(pos+20+slot*8) << ',' << f(pos+24+slot*8)
						<< " colour=" << unsigned(v.colour[slot][0]) << ',' << unsigned(v.colour[slot][1]) << ',' << unsigned(v.colour[slot][2]) << ',' << unsigned(v.colour[slot][3])
						<< " expected=" << unsigned(b[pos+64+slot*4]) << ',' << unsigned(b[pos+65+slot*4]) << ',' << unsigned(b[pos+66+slot*4]) << ',' << unsigned(b[pos+67+slot*4]) << '\n';
					throw std::runtime_error("Original source UV/vertex colour changed");
				}
			}
			++vertices;
		}
	}
	for (size_t i = 0; i < data.Directory().size(); ++i)
		if (data.Directory()[i].kind == 15)
		{
			std::vector<Luxel> tile;
			Check(data.ReadLuxels(i, tile));
			for (const auto &sample : tile)
				if (sample.deluxe.state == DeluxeState::Available)
				{
					float x = sample.deluxe.raw[0] / 255.f - .5f,
						  y = sample.deluxe.raw[1] / 255.f - .5f,
						  z = sample.deluxe.raw[2] / 255.f - .5f;
					float length = std::sqrt(x * x + y * y + z * z);
					need(length > 0 && std::abs(sample.deluxe.toSource.x - x / length) < 1e-5 &&
							 std::abs(sample.deluxe.toSource.y - y / length) < 1e-5 &&
							 std::abs(sample.deluxe.toSource.z - z / length) < 1e-5,
						 "World-space deluxe direction decoding changed");
					++deluxeDirections;
				}
		}
	if (s.metadata.hints == HintPolicy::BlindBenchmark)
	{
		for (const auto &surface : s.geometry->Surfaces())
			need(surface.materialFacts.authoredLuminance == 0 &&
					 surface.materialFacts.legacySurfaceLight == 0,
				 "Authored intensity leaked into benchmark material");
		if (s.evidence)
			for (const auto &e : s.evidence->Evidence())
				need((e.origin == EvidenceOrigin::GlowSurface ||
					  e.origin == EvidenceOrigin::SkyShader) &&
						 e.raw.empty() && e.entities.empty() && !e.intensityKnown &&
						 e.unit == EvidenceUnit::Unknown,
					 "Source-answer leakage into blind benchmark");
	}
	std::cout << "{\"source_sha256\":\"" << Disk::Hex(data.Info().map.sha256)
			  << "\",\"exact_image_bytes\":" << imageBytes
			  << ",\"exact_grid_bytes\":" << s.grid->Records().size()
			  << ",\"exact_grid_index_bytes\":" << s.grid->Indices().size()
			  << ",\"exact_source_vertices\":" << vertices
			  << ",\"unused_nan_uv_components\":" << unusedNanUv
			  << ",\"verified_world_deluxe_directions\":" << deluxeDirections << "}\n";
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
		if (command == "audit-bsp" || command == "audit-source-bsp")
		{
			if (argc != 4)
				throw std::runtime_error("audit-bsp requires dataset and original BSP");
			uint64_t checked = 0;
			if(command=="audit-bsp") Check(data->Validate(true, checked));
			AuditBsp(*data, argv[3]);
			return 0;
		}
		if (command == "benchmark")
		{
			if (argc != 6)
				throw std::runtime_error("benchmark requires source, destination, compiler "
										 "version, PUBLIC bake profile");
			Sources s;
			RegionSnapshot graph;
			Check(data->Load(s, graph));
			auto recipe = ReadSmallFile(argv[5]);
			if (recipe.size() > 1024 * 1024)
				throw std::runtime_error("Public bake profile limit");
			s.metadata.recipe.assign(recipe.begin(), recipe.end());
			if (s.metadata.recipe.find("org.openjk.q3map2-public-bake-profile") ==
					std::string::npos ||
				s.metadata.recipe.find("linear; gamma=1, compensate=1, exposure=0") ==
					std::string::npos)
				throw std::runtime_error(
					"Only the controlled linear q3map2 fixture profile is supported");
			for (const char *secret :
				 {"\"entities\"", "\"lights\"", "\"source_secret\"", "ANSWER_ONLY_DO_NOT_EXPORT"})
				if (s.metadata.recipe.find(secret) != std::string::npos)
					throw std::runtime_error("Ground-truth data in public bake profile");
			s.metadata.recipeSha256 = Disk::Hex(Disk::Hash(recipe.data(), recipe.size()));
			s.metadata.compiler = "q3map2 controlled JA fixture";
			s.metadata.compilerVersion = argv[4];
			s.metadata.producer += " + offline benchmark admission";
			s.metadata.producerVersion += "; org.openjk.q3map-benchmark.v1";
			s.metadata.compilerKnown = s.metadata.synthetic = true;
			s.metadata.hints = HintPolicy::BlindBenchmark;
			s.metadata.decoding.identity = "q3map2-controlled-benchmark-v1";
			auto &source = s.metadata.decoding.source;
			source.documentedTransfer = Decode::Transfer::Linear;
			source.gamma = 1;
			source.assumedTransfer = Decode::Transfer::Unknown;
			source.transferProvenance =
				"Verified q3map2 log: linear lightmaps, gamma 1, compensate 1, exposure 0";
			source.assumeRec709 = true;
			source.assumptionRationale =
				"Controlled RGB fixtures; primaries assumption, not measured photometry";
			s.metadata.decoding.relativeDomain.clear();
			ReadContext ctx;
			ctx.maxBytes = Options().maxResidentBytes;
			std::shared_ptr<const StaticVisibility> rays;
			if (s.regionSnapshot)
				Check(ReplayGeometry(s, graph, ctx, rays, s.regions));
			Options options;
			options.mappedImages = 0;
			Summary summary;
			Check(ExportFile(argv[3], s, options, ctx, summary));
			std::shared_ptr<const Offline::Dataset> verify;
			Check(OpenFile(argv[3], options, verify));
			uint64_t checked = 0;
			Check(verify->Validate(true, checked));
			std::cout << "benchmark-written bytes=" << summary.fileBytes << " checked=" << checked
					  << " blind=1 absolute-scale=unknown\n";
			return 0;
		}
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
			if (argc != 9 && argc != 10)
				throw std::runtime_error("trace requires file sx sy sz ex ey ez [sky]");
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
			if (argc == 10)
			{
				if (std::string(argv[9]) != "sky")
					throw std::runtime_error("Unsupported trace purpose; expected sky");
				q.purpose = RayPurpose::Sky;
			}
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
