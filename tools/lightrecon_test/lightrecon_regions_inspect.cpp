// Offline RBSP v1 inspection adapter. Core binary geometry/graph code has no file I/O.
#include "lightrecon/regions.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace
{
constexpr uint32_t skyFlag = 0x2000u, noDrawFlag = 0x200000u, solidFlag = 1u,
				   translucentFlag = 0x80000000u;
ReadContext Context()
{
	ReadContext c;
	c.maxBytes = 512ull * 1024 * 1024;
	return c;
}
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(std::to_string(unsigned(e.code)) + ": " + e.message);
}
struct Reader
{
	std::vector<uint8_t> bytes;
	struct Lump
	{
		size_t offset = 0, count = 0;
	};
	uint32_t U(size_t p) const
	{
		if (p > bytes.size() || bytes.size() - p < 4)
			throw std::runtime_error("Truncated RBSP field");
		return uint32_t(bytes[p]) | uint32_t(bytes[p + 1]) << 8 | uint32_t(bytes[p + 2]) << 16 |
			   uint32_t(bytes[p + 3]) << 24;
	}
	int32_t I(size_t p) const
	{
		uint32_t u = U(p);
		int32_t i;
		std::memcpy(&i, &u, 4);
		return i;
	}
	float F(size_t p) const
	{
		uint32_t u = U(p);
		float f;
		std::memcpy(&f, &u, 4);
		if (!std::isfinite(f))
			throw std::runtime_error("Non-finite RBSP float");
		return f;
	}
	Vec3 V(size_t p) const
	{
		return Vec3(F(p), F(p + 4), F(p + 8));
	}
	Lump Rows(unsigned index, size_t stride, size_t limit = 4000000) const
	{
		Lump l;
		l.offset = U(8 + index * 8);
		size_t length = U(12 + index * 8);
		if (l.offset > bytes.size() || length > bytes.size() - l.offset || length % stride ||
			length / stride > limit)
			throw std::runtime_error("Invalid/oversized RBSP lump " + std::to_string(index));
		l.count = length / stride;
		return l;
	}
};
double Axis(Vec3 p, unsigned k)
{
	return k == 0 ? p.x : k == 1 ? p.y : p.z;
}
void Set(Vec3 &p, unsigned k, double v)
{
	if (k == 0)
		p.x = float(v);
	else if (k == 1)
		p.y = float(v);
	else
		p.z = float(v);
}
bool Inside(const Bounds &b, Vec3 p)
{
	return p.x >= b.minWorld.x && p.x <= b.maxWorld.x && p.y >= b.minWorld.y &&
		   p.y <= b.maxWorld.y && p.z >= b.minWorld.z && p.z <= b.maxWorld.z;
}
void Write(const std::string &name, const std::string &text)
{
	std::ofstream out(name, std::ios::binary);
	if (!out || !out.write(text.data(), std::streamsize(text.size())))
		throw std::runtime_error("Cannot write " + name);
}
} // namespace
int main(int argc, char **argv)
{
	if (argc != 10)
	{
		std::fprintf(stderr, "usage: lightrecon_regions_inspect map.bsp output-prefix cx cy cz "
							 "radius coarse minimum sky-samples\n");
		return 2;
	}
	try
	{
		auto start = std::chrono::steady_clock::now();
		Reader r;
		std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
		if (!file)
			throw std::runtime_error("Missing BSP");
		auto length = file.tellg();
		if (length < 152 || length > 128 * 1024 * 1024)
			throw std::runtime_error("BSP size limit");
		r.bytes.resize(size_t(length));
		file.seekg(0);
		if (!file.read(reinterpret_cast<char *>(r.bytes.data()), std::streamsize(length)))
			throw std::runtime_error("Short BSP read");
		if (std::memcmp(r.bytes.data(), "RBSP", 4) || r.U(4) != 1)
			throw std::runtime_error("Only original JA RBSP version 1 supported");
		const auto shaders = r.Rows(1, 72), planes = r.Rows(2, 16), nodes = r.Rows(3, 36),
				   leaves = r.Rows(4, 48), leafBrushes = r.Rows(6, 4), models = r.Rows(7, 40),
				   brushes = r.Rows(8, 12), sides = r.Rows(9, 12),
				   verts = r.Rows(10, 80, 1024 * 1024), indices = r.Rows(11, 4),
				   surfaces = r.Rows(13, 148, 131072);
		if (!models.count)
			throw std::runtime_error("Missing world model");
		LightmapMapIdentity identity;
		identity.path = "maps/";
		std::string path = argv[1];
		identity.path += path.substr(path.find_last_of("/\\") + 1);
		identity.bytes = r.bytes.size();
		identity.sha256 = Disk::Hash(r.bytes.data(), r.bytes.size());
		BspSpaceInput space;
		space.identity = identity;
		space.worldBounds.known = true;
		space.worldBounds.minWorld = r.V(models.offset);
		space.worldBounds.maxWorld = r.V(models.offset + 12);
		for (size_t i = 0; i < planes.count; ++i)
		{
			SpatialPlane p;
			p.normal = r.V(planes.offset + i * 16);
			p.distance = r.F(planes.offset + i * 16 + 12);
			space.planes.push_back(p);
		}
		for (size_t i = 0; i < nodes.count; ++i)
		{
			SpatialNode n;
			n.plane = r.U(nodes.offset + i * 36);
			n.children[0] = r.I(nodes.offset + i * 36 + 4);
			n.children[1] = r.I(nodes.offset + i * 36 + 8);
			space.nodes.push_back(n);
		}
		for (size_t i = 0; i < leaves.count; ++i)
		{
			size_t p = leaves.offset + i * 48;
			SpatialLeaf l;
			l.cluster = r.I(p);
			l.area = r.I(p + 4);
			l.firstBrush = r.U(p + 40);
			l.brushCount = r.U(p + 44);
			space.leaves.push_back(l);
		}
		for (size_t i = 0; i < leafBrushes.count; ++i)
			space.leafBrushes.push_back(r.U(leafBrushes.offset + i * 4));
		for (size_t i = 0; i < sides.count; ++i)
			space.sides.push_back(r.U(sides.offset + i * 12));
		const uint32_t firstBrush = r.U(models.offset + 32), worldBrushes = r.U(models.offset + 36);
		if (firstBrush > brushes.count || worldBrushes > brushes.count - firstBrush)
			throw std::runtime_error("World brush span");
		for (size_t i = 0; i < brushes.count; ++i)
		{
			size_t p = brushes.offset + i * 12;
			uint32_t shader = r.U(p + 8);
			if (shader >= shaders.count)
				throw std::runtime_error("Brush shader reference");
			SpatialBrush b;
			b.firstSide = r.U(p);
			b.sideCount = r.U(p + 4);
			b.solid = (r.U(shaders.offset + shader * 72 + 68) & solidFlag) != 0;
			b.sky = (r.U(shaders.offset + shader * 72 + 64) & skyFlag) != 0;
			b.world = i >= firstBrush && i < firstBrush + worldBrushes;
			space.brushes.push_back(b);
		}
		std::shared_ptr<const BspRegionDomain> domain;
		Check(BspRegionDomain::Create(std::move(space), Context(), domain));
		SurfaceLimits limits;
		limits.maxBytes = 128ull * 1024 * 1024;
		SurfaceCapture capture(limits);
		capture.Begin(BuildPolicy::ForceRebuild);
		Check(capture.SetMap(identity));
		std::vector<uint32_t> owner(surfaces.count, UINT32_MAX);
		std::vector<DynamicBoundary> dynamic;
		for (uint32_t model = 0; model < models.count; ++model)
		{
			size_t p = models.offset + model * 40;
			uint32_t first = r.U(p + 24), count = r.U(p + 28);
			if (first > surfaces.count || count > surfaces.count - first)
				throw std::runtime_error("Model surface span");
			for (uint32_t i = 0; i < count; ++i)
			{
				if (owner[first + i] != UINT32_MAX)
					throw std::runtime_error("Overlapping model ownership");
				owner[first + i] = model;
			}
			if (model)
			{
				DynamicBoundary d;
				d.id = model;
				d.model = model;
				d.bounds.known = true;
				d.bounds.minWorld = r.V(p);
				d.bounds.maxWorld = r.V(p + 12);
				if (d.bounds.minWorld.x < d.bounds.maxWorld.x &&
					d.bounds.minWorld.y < d.bounds.maxWorld.y &&
					d.bounds.minWorld.z < d.bounds.maxWorld.z)
					dynamic.push_back(d);
			}
		}
		uint64_t ignoredFlares = 0, unknownMaterials = 0;
		for (uint32_t i = 0; i < surfaces.count; ++i)
		{
			size_t p = surfaces.offset + i * 148;
			uint32_t shader = r.U(p), kind = r.U(p + 8), first = r.U(p + 12), count = r.U(p + 16),
					 firstIndex = r.U(p + 20), countIndices = r.U(p + 24);
			if (shader >= shaders.count || first > verts.count || count > verts.count - first ||
				firstIndex > indices.count || countIndices > indices.count - firstIndex)
				throw std::runtime_error("Surface references");
			BakedSurface s;
			s.ordinal = i;
			s.model = owner[i];
			s.motion = s.model == 0 ? SurfaceMotion::StaticWorld
									: s.model == UINT32_MAX ? SurfaceMotion::Unknown
															: SurfaceMotion::BrushModel;
			s.kind = kind == 1 ? BakedSurfaceKind::Face
							   : kind == 2 ? BakedSurfaceKind::Patch
										   : kind == 3 ? BakedSurfaceKind::Triangles
													   : BakedSurfaceKind::Unsupported;
			s.materialIndex = shader;
			uint32_t flags = r.U(shaders.offset + shader * 72 + 64),
					 contents = r.U(shaders.offset + shader * 72 + 68);
			s.materialFacts.surfaceFlags = flags;
			s.materialFacts.contentFlags = contents;
			s.materialFacts.cull = SurfaceCull::None;
			if (flags & skyFlag)
			{
				s.materialFacts.available = true;
				s.materialFacts.signals |= MaterialSky;
			}
			else
			{
				++unknownMaterials;
				s.materialFacts.signals |= MaterialMissing;
			}
			if (flags & noDrawFlag)
				s.materialFacts.signals |= MaterialNoDraw;
			if (contents & translucentFlag)
				s.materialFacts.signals |= MaterialTranslucent;
			if (kind == 4)
			{
				++ignoredFlares;
				s.materialFacts.signals |= MaterialNoDraw;
			} // explicit nonpolygonal flare, not guessed opaque geometry
			if (s.kind != BakedSurfaceKind::Unsupported)
			{
				for (uint32_t j = 0; j < count; ++j)
				{
					BakedVertex v;
					v.position = r.V(verts.offset + (first + j) * 80);
					v.shadingNormal = r.V(verts.offset + (first + j) * 80 + 52);
					s.vertices.push_back(v);
				}
				for (uint32_t j = 0; j < countIndices; ++j)
					s.indices.push_back(r.U(indices.offset + (firstIndex + j) * 4));
			}
			s.patchWidth = r.U(p + 140);
			s.patchHeight = r.U(p + 144);
			Check(capture.Add(std::move(s)));
		}
		Check(capture.Freeze());
		auto geometry = capture.Acquire();
		VisibilityBuildOptions visibility;
		visibility.patchToleranceWorld = std::max(.01, std::stod(argv[8]) * .25);
		visibility.maxBytes = 256ull * 1024 * 1024;
		std::shared_ptr<const StaticVisibility> rays;
		Check(StaticVisibility::Build(geometry, visibility, Context(), rays, domain));
		RegionOptions options;
		Vec3 centre(float(std::stod(argv[3])), float(std::stod(argv[4])),
					float(std::stod(argv[5])));
		double radius = std::stod(argv[6]);
		if (!std::isfinite(radius) || radius <= 0)
			throw std::runtime_error("Invalid radius");
		options.bounds.known = true;
		for (unsigned k = 0; k < 3; ++k)
		{
			Set(options.bounds.minWorld, k,
				std::max(Axis(domain->Source().worldBounds.minWorld, k), Axis(centre, k) - radius));
			Set(options.bounds.maxWorld, k,
				std::min(Axis(domain->Source().worldBounds.maxWorld, k), Axis(centre, k) + radius));
		}
		options.coarseCellWorld = float(std::stod(argv[7]));
		options.minimumCellWorld = float(std::stod(argv[8]));
		options.constrictionWorld = options.coarseCellWorld * .5f;
		options.maxBytes = 256ull * 1024 * 1024;
		options.maxWork = 256ull * 1024 * 1024;
		// Last argument is a deterministic sky sample count, useful for bounded offline inspection.
		options.skySamplesPerRegion = unsigned(std::stoul(argv[9]));
		RegionBuildInput input;
		input.identity = identity;
		input.domain = domain;
		input.geometry = rays;
		input.dynamic = dynamic;
		std::shared_ptr<const LightingRegionGraph> graph;
		Check(LightingRegionGraph::Build(std::move(input), options, Context(), graph));
		std::string obj;
		Check(graph->DebugObj(100000, 32 * 1024 * 1024, obj));
		Write(std::string(argv[2]) + ".obj", obj);
		std::ofstream cells(std::string(argv[2]) + "-cells.tsv");
		cells << "cell\tregion\tclass\tissues\tx0\ty0\tz0\tx1\ty1\tz1\tcluster\tarea\n"
			  << std::setprecision(9);
		for (const auto &c : graph->Cells())
		{
			cells << c.id << '\t' << c.region << '\t' << unsigned(c.space) << '\t' << c.issues;
			for (unsigned high = 0; high < 2; ++high)
				for (unsigned k = 0; k < 3; ++k)
					cells << '\t'
						  << Axis(options.bounds.minWorld, k) +
								 double(c.origin[k] + (high ? c.units : 0)) *
									 options.minimumCellWorld;
			cells << '\t' << c.bsp.cluster << '\t' << c.bsp.area << '\n';
		}
		if (!cells)
			throw std::runtime_error("Cell export failed");
		std::ofstream edges(std::string(argv[2]) + "-edges.tsv");
		edges << "a\tb\ttransport\tclear\tblocked\tunknown\tdynamic\tarea\tx\ty\tz\n"
			  << std::setprecision(9);
		for (const auto &e : graph->Connections())
			edges << e.a << '\t' << e.b << '\t' << unsigned(e.transport) << '\t' << e.clearSamples
				  << '\t' << e.blockedSamples << '\t' << e.unknownSamples << '\t'
				  << e.dynamicPossible << '\t' << e.sampledAreaWorldSquared << '\t'
				  << e.sampledOpeningCentreWorld.x << '\t' << e.sampledOpeningCentreWorld.y << '\t'
				  << e.sampledOpeningCentreWorld.z << '\n';
		if (!edges)
			throw std::runtime_error("Edge export failed");
		// Original indexed lightgrid coordinates, never inferred from RGB/styles.
		auto ent = r.Rows(0, 1), grid = r.Rows(15, 30), array = r.Rows(17, 2);
		Vec3 spacing(64, 64, 128);
		std::string entities(reinterpret_cast<const char *>(r.bytes.data() + ent.offset),
							 ent.count);
		const auto worldEnd = entities.find('}');
		if (worldEnd != std::string::npos)
			entities.resize(worldEnd + 1);
		std::smatch match;
		std::regex sizePattern("\\\"gridsize\\\"\\s*\\\"([^\\\"]+)\\\"");
		if (std::regex_search(entities, match, sizePattern))
		{
			std::istringstream value(match[1].str());
			if (!(value >> spacing.x >> spacing.y >> spacing.z))
				throw std::runtime_error("Invalid authored grid size");
		}
		GridLayout layout;
		layout.spacingWorld = spacing;
		for (unsigned k = 0; k < 3; ++k)
		{
			if (!std::isfinite(Axis(spacing, k)) || Axis(spacing, k) <= 0)
				throw std::runtime_error("Nonpositive grid size");
			Set(layout.originWorld, k,
				Axis(spacing, k) *
					std::ceil(Axis(domain->Source().worldBounds.minWorld, k) / Axis(spacing, k)));
			double last =
				Axis(spacing, k) *
				std::floor(Axis(domain->Source().worldBounds.maxWorld, k) / Axis(spacing, k));
			double bound = (last - Axis(layout.originWorld, k)) / Axis(spacing, k) + 1;
			if (!std::isfinite(bound) || bound < 1 || bound > 8 * 1024 * 1024)
				throw std::runtime_error("Invalid grid bounds");
			layout.bounds[k] = uint32_t(bound);
		}
		uint64_t sampled = 0, assigned = 0, solid = 0, unknown = 0, missing = 0;
		uint64_t total = 1;
		for (auto bound : layout.bounds)
		{
			if (total > 8 * 1024 * 1024 / bound)
				throw std::runtime_error("Grid product limit");
			total *= bound;
		}
		if (total > 8 * 1024 * 1024)
			throw std::runtime_error("Grid cell limit");
		for (uint64_t i = 0; i < total; ++i)
		{
			uint64_t q = i;
			GridCell c;
			c.ordinal = i;
			for (unsigned k = 0; k < 3; ++k)
			{
				uint32_t coordinate = uint32_t(q % layout.bounds[k]);
				q /= layout.bounds[k];
				Set(c.centreWorld, k, Axis(layout.originWorld, k) + coordinate * Axis(spacing, k));
			}
			if (!Inside(options.bounds, c.centreWorld))
				continue;
			++sampled;
			if (i < array.count)
			{
				size_t p = array.offset + size_t(i) * 2;
				uint16_t index = uint16_t(r.bytes[p]) | uint16_t(r.bytes[p + 1]) << 8;
				c.presence =
					index < grid.count ? GridPresence::Present : GridPresence::InvalidIndex;
			}
			RegionAssignment a;
			Check(graph->AssignGrid(c, Context(), a));
			if (a.state == RegionAssignmentState::Assigned)
				++assigned;
			else if (a.state == RegionAssignmentState::Solid)
				++solid;
			else if (a.state == RegionAssignmentState::MissingObservation)
				++missing;
			else
				++unknown;
		}
		const auto &report = graph->Report();
		double seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::printf("map=%s sha256=%s domain-bytes=%llu triangles=%llu visibility-bytes=%llu "
					"ignored-flares=%llu unknown-materials=%llu\n",
					identity.path.c_str(), Disk::Hex(identity.sha256).c_str(),
					static_cast<unsigned long long>(domain->OwnedBytes()),
					static_cast<unsigned long long>(rays->Report().triangles),
					static_cast<unsigned long long>(rays->Report().ownedBytes),
					static_cast<unsigned long long>(ignoredFlares),
					static_cast<unsigned long long>(unknownMaterials));
		std::printf("regions=%llu components=%llu connections=%llu apertures=%llu free=%llu "
					"solid=%llu sky=%llu unknown=%llu dynamic=%llu sky-regions=%llu bytes=%llu "
					"peak=%llu domain-work=%llu geometry-work=%llu rays=%llu seconds=%.6f\n",
					static_cast<unsigned long long>(report.regions),
					static_cast<unsigned long long>(report.staticComponents),
					static_cast<unsigned long long>(report.connections),
					static_cast<unsigned long long>(report.apertures),
					static_cast<unsigned long long>(report.freeCells),
					static_cast<unsigned long long>(report.solidCells),
					static_cast<unsigned long long>(report.skyCells),
					static_cast<unsigned long long>(report.unknownCells),
					static_cast<unsigned long long>(report.dynamicCells),
					static_cast<unsigned long long>(report.skyRegions),
					static_cast<unsigned long long>(report.ownedBytes),
					static_cast<unsigned long long>(report.peakBuildBytes),
					static_cast<unsigned long long>(report.domainWork),
					static_cast<unsigned long long>(report.geometryWork),
					static_cast<unsigned long long>(report.rays), seconds);
		std::printf(
			"grid-in-domain=%llu grid-assigned=%llu grid-solid=%llu grid-unknown=%llu "
			"grid-missing=%llu bsp-links=%zu "
			"boundary-refs=%zu\n",
			static_cast<unsigned long long>(sampled), static_cast<unsigned long long>(assigned),
			static_cast<unsigned long long>(solid), static_cast<unsigned long long>(unknown),
			static_cast<unsigned long long>(missing), graph->BspLinks().size(),
			graph->Boundaries().size());
		return 0;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "regions inspection failed: %s\n", e.what());
		return 1;
	}
}
