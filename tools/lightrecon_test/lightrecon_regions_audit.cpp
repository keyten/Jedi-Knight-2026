// Matched-input replay of renderer-captured observations. Never loads a game/GL context.
#include "lightrecon/geometry_key.h"
#include "lightrecon/offline.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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
static double Axis(Vec3 v, unsigned k)
{
	return k == 0 ? v.x : k == 1 ? v.y : v.z;
}
static void Set(Vec3 &v, unsigned k, double x)
{
	(k == 0 ? v.x : k == 1 ? v.y : v.z) = float(x);
}
static bool Inside(const Bounds &b, Vec3 v)
{
	for (unsigned k = 0; k < 3; ++k)
		if (Axis(v, k) < Axis(b.minWorld, k) || Axis(v, k) > Axis(b.maxWorld, k))
			return false;
	return true;
}
#include "regions_audit_geometry.h"
int main(int argc, char **argv)
{
	try
	{
		if (argc != 13)
			throw std::runtime_error(
				"usage: regions_audit dataset original.bsp prefix cx cy cz radius coarse minimum "
				"sky-samples patch-tolerance raw|flares|inspector");
		auto start = std::chrono::steady_clock::now();
		O::Options limits;
		std::shared_ptr<const O::Dataset> dataset;
		Check(O::OpenFile(argv[1], limits, dataset));
		O::Sources s;
		O::RegionSnapshot snapshot;
		Check(dataset->Load(s, snapshot));
		ReadContext ctx;
		ctx.maxBytes = 512ull * 1024 * 1024;
		std::ifstream bsp(argv[2], std::ios::binary | std::ios::ate);
		auto size = bsp.tellg();
		if (!bsp || size < 152 || size > 128 * 1024 * 1024)
			throw std::runtime_error("Invalid BSP size");
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		bsp.seekg(0);
		bsp.read(reinterpret_cast<char *>(bytes.data()), size);
		if (!bsp || s.metadata.map.sha256 != Disk::Hash(bytes.data(), bytes.size()) ||
			s.metadata.map.bytes != bytes.size())
			throw std::runtime_error("BSP fingerprint mismatch");
		auto u = [&](size_t p) {
			if (p > bytes.size() - 4)
				throw std::runtime_error("Truncated BSP");
			return uint32_t(bytes[p]) | uint32_t(bytes[p + 1]) << 8 | uint32_t(bytes[p + 2]) << 16 |
				   uint32_t(bytes[p + 3]) << 24;
		};
		size_t surfaceOffset = u(8 + 13 * 8), surfaceBytes = u(12 + 13 * 8);
		if (surfaceOffset > bytes.size() || surfaceBytes > bytes.size() - surfaceOffset ||
			surfaceBytes / 148 != s.geometry->Surfaces().size())
			throw std::runtime_error("Surface count mismatch");
		size_t shaderOffset = u(8 + 1 * 8), shaderBytes = u(12 + 1 * 8),
			   vertexOffset = u(8 + 10 * 8), vertexBytes = u(12 + 10 * 8),
			   indexOffset = u(8 + 11 * 8), indexBytes = u(12 + 11 * 8);
		for (auto span :
			 {std::make_pair(shaderOffset, shaderBytes), std::make_pair(vertexOffset, vertexBytes),
			  std::make_pair(indexOffset, indexBytes)})
			if (span.first > bytes.size() || span.second > bytes.size() - span.first)
				throw std::runtime_error("Source geometry lump bounds");
		auto f = [&](size_t offset) {
			uint32_t bits = u(offset);
			float value;
			std::memcpy(&value, &bits, 4);
			return value;
		};
		std::string mode = argv[12];
		if (mode != "raw" && mode != "flares" && mode != "inspector")
			throw std::runtime_error("Unknown capture policy");
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Check(capture.SetMap(s.metadata.map));
		uint64_t flares = 0, unsupported = 0, empty = 0, unknownMaterials = 0;
		std::map<uint32_t, uint64_t> sourceTypes;
		std::ostringstream signature;
		signature << std::setprecision(17);
		for (auto surface : s.geometry->Surfaces())
		{
			uint32_t kind = u(surfaceOffset + surface.ordinal * 148 + 8);
			++sourceTypes[kind];
			if (surface.kind == BakedSurfaceKind::Flare && kind != 4)
				throw std::runtime_error("Non-flare BSP surface labelled as Flare");
			if (surface.kind == BakedSurfaceKind::Face || surface.kind == BakedSurfaceKind::Patch ||
				surface.kind == BakedSurfaceKind::Triangles)
			{
				size_t record = surfaceOffset + surface.ordinal * 148, fv = u(record + 12),
					   nv = u(record + 16), fi = u(record + 20), ni = u(record + 24);
				if (kind != uint32_t(surface.kind) + 1)
					throw std::runtime_error("Retained/BSP surface type mismatch");
				if (surface.kind == BakedSurfaceKind::Patch &&
					(surface.patchWidth != u(record + 140) || surface.patchHeight != u(record + 144)))
					throw std::runtime_error("Original BSP patch dimensions changed");
				if (fv > vertexBytes / 80 || nv > vertexBytes / 80 - fv || fi > indexBytes / 4 ||
					ni > indexBytes / 4 - fi || nv != surface.vertices.size() ||
					ni != surface.indices.size())
					throw std::runtime_error("Retained/source vertex or index count mismatch");
				for (size_t j = 0; j < nv; ++j)
					for (unsigned k = 0; k < 3; ++k)
						if (Axis(surface.vertices[j].position, k) !=
							f(vertexOffset + (fv + j) * 80 + k * 4))
							throw std::runtime_error("Original BSP position changed");
				for (size_t j = 0; j < ni; ++j)
					if (surface.indices[j] != u(indexOffset + (fi + j) * 4))
						throw std::runtime_error("Original BSP triangle order changed");
			}
			if (kind == 4)
			{
				++flares;
				if (mode != "raw")
					surface.kind = BakedSurfaceKind::Flare;
			}
			if (mode == "inspector")
			{
				if (surface.materialIndex >= shaderBytes / 72)
					throw std::runtime_error("BSP material index bounds");
				auto sf = u(shaderOffset + surface.materialIndex * 72 + 64),
					 cf = u(shaderOffset + surface.materialIndex * 72 + 68);
				uint32_t flags = (sf & 0x2000 ? uint32_t(MaterialSky) : 0u) |
								 (sf & 0x200000 ? uint32_t(MaterialNoDraw) : 0u) |
								 (cf & 0x80000000 ? uint32_t(MaterialTranslucent) : 0u);
				if (!(flags & MaterialSky))
					flags |= MaterialMissing;
				surface.materialFacts = MaterialFacts();
				surface.materialFacts.signals = flags;
				surface.materialFacts.surfaceFlags = sf;
				surface.materialFacts.contentFlags = cf;
				surface.materialFacts.cull = SurfaceCull::None;
				surface.materialFacts.available = (flags & MaterialSky) != 0;
			}
			unsupported += surface.kind == BakedSurfaceKind::Unsupported;
			empty += surface.kind == BakedSurfaceKind::Unsupported && surface.vertices.empty();
			unknownMaterials += !surface.materialFacts.available;
			signature << surface.ordinal << ' ' << unsigned(surface.kind) << ' '
					  << unsigned(surface.motion) << ' ' << surface.materialIndex << ' '
					  << surface.materialFacts.signals << ' ' << surface.materialFacts.available
					  << ' ' << unsigned(surface.materialFacts.cull) << ' '
					  << (surface.materialFacts.signals & MaterialCutout) << '\n';
			for (auto &v : surface.vertices)
				signature << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
			for (auto i : surface.indices)
				signature << i << ' ';
			Check(capture.Add(std::move(surface)));
		}
		Check(capture.Freeze());
		s.geometry = capture.Acquire();
		std::shared_ptr<const BspRegionDomain> originalDomain;
		std::vector<DynamicBoundary> originalDynamic;
		Check(O::CaptureBspSpace(bytes.data(), bytes.size(), s.metadata.map, ctx, originalDomain,
								 originalDynamic));
		auto domainKey = RegionAudit::DomainKey(*originalDomain, originalDynamic);
		if (domainKey != RegionAudit::DomainKey(*s.domain, s.dynamic))
			throw std::runtime_error("Archived/original BSP domain or dynamic bounds mismatch");
		// Both paths use precisely the same domain and dynamic records, not a second inferred
		// scene.
		s.domain = originalDomain;
		s.dynamic = originalDynamic;
		auto &world = s.domain->Source().worldBounds;
		Vec3 centre;
		for (unsigned k = 0; k < 3; ++k)
			Set(centre, k,
				std::string(argv[4 + k]) == "auto"
					? (Axis(world.minWorld, k) + Axis(world.maxWorld, k)) / 2
					: std::stod(argv[4 + k]));
		double radius = std::stod(argv[7]);
		auto archivedOptions = snapshot.options;
		auto &o = snapshot.options;
		o.bounds.known = true;
		for (unsigned k = 0; k < 3; ++k)
		{
			Set(o.bounds.minWorld, k, std::max(Axis(world.minWorld, k), Axis(centre, k) - radius));
			Set(o.bounds.maxWorld, k, std::min(Axis(world.maxWorld, k), Axis(centre, k) + radius));
		}
		o.coarseCellWorld = float(std::stod(argv[8]));
		o.minimumCellWorld = float(std::stod(argv[9]));
		o.constrictionWorld = o.coarseCellWorld * .5f;
		o.skySamplesPerRegion = unsigned(std::stoul(argv[10]));
		o.maxBytes = 256ull * 1024 * 1024;
		o.maxWork = 256ull * 1024 * 1024;
		s.metadata.visibilityPatchToleranceWorld = float(std::stod(argv[11]));
		if (radius == 0)
			o = archivedOptions; // exact archived export ROI/options, not an assumed default
		std::shared_ptr<const StaticVisibility> rays;
		std::shared_ptr<const LightingRegionGraph> graph;
		VisibilityBuildOptions visibility;
		visibility.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
		visibility.maxBytes = 256ull * 1024 * 1024;
		auto replayStart = std::chrono::steady_clock::now();
		Check(O::ReplayGeometry(s, snapshot, ctx, rays, graph, &visibility));
		auto replayEnd = std::chrono::steady_clock::now();
		RegionBuildInput input;
		input.identity = s.metadata.map;
		input.domain = s.domain;
		input.geometry = rays;
		input.dynamic = s.dynamic;
		std::shared_ptr<const LightingRegionGraph> direct;
		Check(LightingRegionGraph::Build(input, o, ctx, direct));
		auto directEnd = std::chrono::steady_clock::now();
		if (direct->Cells().size() != graph->Cells().size())
			throw std::runtime_error("Direct/replay cell mismatch");
		for (size_t i = 0; i < graph->Cells().size(); ++i)
		{
			auto &a = graph->Cells()[i];
			auto &b = direct->Cells()[i];
			if (a.space != b.space || a.region != b.region || a.issues != b.issues)
				throw std::runtime_error("Direct/replay classification mismatch");
		}
		std::string prefix = argv[3], obj;
		Check(graph->DebugObj(350000, 64 * 1024 * 1024, obj));
		std::ofstream(prefix + ".obj") << obj;
		std::ofstream cells(prefix + "-cells.tsv");
		cells << "cell\tregion\tclass\tissues\tx0\ty0\tz0\tx1\ty1\tz1\tcluster\tarea\n"
			  << std::setprecision(9);
		uint64_t domainCounts[6] = {}, local = 0, globalOnly = 0, disagreement = 0;
		double volumes[6] = {};
		for (auto &c : graph->Cells())
		{
			Bounds box;
			box.known = true;
			cells << c.id << '\t' << c.region << '\t' << unsigned(c.space) << '\t' << c.issues;
			for (unsigned high = 0; high < 2; ++high)
				for (unsigned k = 0; k < 3; ++k)
				{
					double value =
						Axis(o.bounds.minWorld, k) +
						(c.origin[k] + (high ? c.units : 0)) * double(o.minimumCellWorld);
					Set(high ? box.maxWorld : box.minWorld, k, value);
					cells << '\t' << value;
				}
			cells << '\t' << c.bsp.cluster << '\t' << c.bsp.area << '\n';
			double volume = 1;
			for (unsigned k = 0; k < 3; ++k)
				volume *= std::max(0., std::min(Axis(box.maxWorld, k), Axis(o.bounds.maxWorld, k)) -
										   Axis(box.minWorld, k));
			volumes[unsigned(c.space)] += volume;
			SpaceClass bspSpace;
			uint64_t work = 0;
			Check(s.domain->ClassifyBox(box, bspSpace, work));
			++domainCounts[unsigned(bspSpace)];
			if (bspSpace == SpaceClass::Free || bspSpace == SpaceClass::Exterior)
			{
				GeometryBoxResult hit;
				Check(rays->OverlapBox(box, ctx, hit));
				if (hit.intersects)
					++local;
				else if (!hit.coverageComplete)
					++globalOnly;
				if (c.bsp.space != SpaceClass::Free && c.bsp.space != SpaceClass::Exterior)
					++disagreement;
			}
		}
		uint64_t gridIn = 0, gridAssigned = 0;
		if (s.grid)
			for (uint64_t i = 0; i < s.grid->Report().cells; ++i)
			{
				GridCell c;
				Check(s.grid->Cell(i, c));
				if (!Inside(o.bounds, c.centreWorld))
					continue;
				++gridIn;
				RegionAssignment a;
				Check(graph->AssignGrid(c, ctx, a));
				gridAssigned += a.state == RegionAssignmentState::Assigned;
			}
		auto triangles = RegionAudit::Geometry(s, o.bounds, prefix);
		auto reference = RegionAudit::Rays(s, *graph, *rays, ctx, triangles, prefix);
		auto &rayStates = reference.states;
		uint64_t luxelsIn = 0, luxelsChecked = 0, luxelsAssigned = 0;
		for (size_t i = 0; i < dataset->Directory().size(); ++i)
			if (dataset->Directory()[i].kind == 15)
			{
				std::vector<O::Luxel> tile;
				Check(dataset->ReadLuxels(i, tile));
				for (auto &sample : tile)
					if (sample.surface.positionKnown &&
						sample.surface.coverage == LuxelCoverage::Mapped &&
						Inside(o.bounds, sample.surface.positionWorld))
					{
						++luxelsIn;
						if (luxelsChecked >= 2048)
							continue;
						++luxelsChecked;
						SurfaceRegionSides sides;
						Check(graph->AssignSurfaceSides(
							sample.surface.positionWorld, sample.surface.geometricNormal,
							Id(sample.surface.surface) + 1, ctx, sides));
						luxelsAssigned += sides.positive.state == RegionAssignmentState::Assigned ||
										  sides.negative.state == RegionAssignmentState::Assigned;
					}
			}
		auto sig = signature.str();
		auto &r = graph->Report();
		auto &v = rays->Report();
		std::ostringstream out;
		out << std::setprecision(12) << "{\n\"map_sha256\":\"" << Disk::Hex(s.metadata.map.sha256)
			<< "\",\n\"geometry_signature\":\"" << Disk::Hex(Disk::Hash(sig.data(), sig.size()))
			<< "\",\n\"capture_mode\":\"" << mode << "\",\n\"centre\":[" << centre.x << ','
			<< centre.y << ',' << centre.z << "],\n\"radius\":" << radius
			<< ",\"coarse\":" << o.coarseCellWorld << ",\"minimum\":" << o.minimumCellWorld
			<< ",\"patch_tolerance\":" << s.metadata.visibilityPatchToleranceWorld
			<< ",\"sky_samples\":" << o.skySamplesPerRegion
			<< ",\n\"surfaces\":" << s.geometry->Surfaces().size()
			<< ",\"source_flares\":" << flares << ",\"unsupported\":" << unsupported
			<< ",\"empty_unsupported\":" << empty << ",\"unknown_materials\":" << unknownMaterials
			<< ",\"dynamic_bounds\":" << s.dynamic.size()
			<< ",\n\"visibility_complete\":" << (v.complete ? "true" : "false")
			<< ",\"triangles\":" << v.triangles << ",\"unsupported_bounds\":" << v.unsupportedBounds
			<< ",\"unbounded_unsupported\":" << v.unboundedUnsupported
			<< ",\n\"domain_free\":" << domainCounts[0] << ",\"domain_solid\":" << domainCounts[1]
			<< ",\"domain_unknown\":" << domainCounts[4]
			<< ",\"local_intersection_invalidations\":" << local
			<< ",\"global_only_invalidations\":" << globalOnly
			<< ",\"free_certificate_point_disagreements\":" << disagreement
			<< ",\n\"cells\":" << graph->Cells().size() << ",\"free\":" << r.freeCells
			<< ",\"solid\":" << r.solidCells << ",\"unknown\":" << r.unknownCells
			<< ",\"regions\":" << r.regions << ",\"components\":" << r.staticComponents
			<< ",\"apertures\":" << r.apertures << ",\n\"free_volume\":" << volumes[0] + volumes[5]
			<< ",\"solid_volume\":" << volumes[1] << ",\"unknown_volume\":" << volumes[4]
			<< ",\n\"grid_in_roi\":" << gridIn << ",\"grid_assigned\":" << gridAssigned
			<< ",\"ray_visible\":" << rayStates[0] << ",\"ray_blocked\":" << rayStates[1]
			<< ",\"ray_unknown\":" << rayStates[2] << ",\n\"visibility_bytes\":" << v.ownedBytes
			<< ",\"region_bytes\":" << r.ownedBytes << ",\"region_peak\":" << r.peakBuildBytes
			<< ",\"domain_bytes\":" << s.domain->OwnedBytes()
			<< ",\"seconds_two_builds_and_diagnostics\":"
			<< std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
			<< ",\"direct_replay_identical\":true\n}\n";
		auto json = out.str();
		json.resize(json.size() - 2);
		std::ostringstream diagnostics;
		diagnostics << ",\n\"region_schema\":" << kLightingRegionGraphVersion
					<< ",\"visibility_schema\":" << kStaticVisibilityVersion
					<< ",\"surface_schema\":" << kSurfaceObservationVersion
					<< ",\"imported_region_schema\":" << snapshot.schemaVersion
					<< ",\"certification_key\":\"" << Disk::Hex(graph->CertificationKey())
					<< "\",\n\"spatial_coverage_complete\":"
					<< (v.spatialCoverageComplete ? "true" : "false")
					<< ",\"excluded_flares\":" << v.excludedFlares
					<< ",\"representative_class_differences\":" << r.representativeDisagrees
					<< ",\"degenerate_triangles\":" << v.degenerate
					<< ",\"excluded_moving\":" << v.excludedMoving
					<< ",\"excluded_nodraw\":" << v.excludedNoDraw
					<< ",\"missing_unsupported_bounds\":" << v.missingUnsupportedBounds
					<< ",\"missing_known_surface_bounds\":" << v.missingKnownSurfaceBounds
					<< ",\"bsp_unclustered\":" << r.bspUnclustered
					<< ",\"bsp_possible_brush\":" << r.bspPossibleBrush
					<< ",\"bsp_outside_world\":" << r.bspOutsideWorld
					<< ",\"minimum_limited\":" << r.minimumLimited
					<< ",\"moving_uncertain\":" << r.movingUncertain
					<< ",\"optical_uncertain\":" << r.opticalUncertain
					<< ",\"unsupported_uncertain\":" << r.unsupportedUncertain
					<< ",\"locally_clear_unassigned\":" << r.locallyClearUnassigned
					<< ",\"bsp_refined_disjoint\":" << r.bspRefinedDisjoint
					<< ",\"source_domain_and_dynamic_match\":true,\"retained_geometry_matches_"
					   "original_bsp\":true,\"domain_signature\":\""
					<< Disk::Hex(domainKey) << "\",\"visibility_max_bytes\":" << visibility.maxBytes
					<< ",\"visibility_max_work\":" << visibility.maxWork
					<< ",\"context_max_bytes\":" << ctx.maxBytes
					<< ",\n\"reference_opaque_face_hits\":" << reference.referenceOpaqueHits
					<< ",\"reference_false_clear\":" << reference.falseClear
					<< ",\"reference_exact_contacts\":" << reference.exactContacts
					<< ",\"reference_max_error_world\":" << std::setprecision(12)
					<< reference.maxContactError << ",\"luxels_in_roi\":" << luxelsIn
					<< ",\"luxels_assignment_checked\":" << luxelsChecked
					<< ",\"luxels_assigned\":" << luxelsAssigned
					<< ",\"region_max_bytes\":" << o.maxBytes
					<< ",\"region_max_work\":" << o.maxWork
					<< ",\"region_max_ray_work\":" << o.maxRayWork << ",\"archive_bounds_min\":["
					<< archivedOptions.bounds.minWorld.x << ',' << archivedOptions.bounds.minWorld.y
					<< ',' << archivedOptions.bounds.minWorld.z << "],\"archive_bounds_max\":["
					<< archivedOptions.bounds.maxWorld.x << ',' << archivedOptions.bounds.maxWorld.y
					<< ',' << archivedOptions.bounds.maxWorld.z << "]\n}\n";
		json += diagnostics.str();
		json.resize(json.size() - 2);
		std::ostringstream stages;
		stages << ",\n\"replay_bvh_and_region_seconds\":"
			   << std::chrono::duration<double>(replayEnd - replayStart).count()
			   << ",\"direct_region_seconds\":"
			   << std::chrono::duration<double>(directEnd - replayEnd).count()
			   << ",\"domain_work\":" << r.domainWork << ",\"geometry_work\":" << r.geometryWork
			   << ",\"connection_rays\":" << r.rays << ",\"source_types\":{";
		bool first = true;
		for (auto type : sourceTypes)
		{
			if (!first)
				stages << ',';
			first = false;
			stages << '"' << type.first << "\":" << type.second;
		}
		stages << "}\n}\n";
		json += stages.str();
		std::ofstream(prefix + ".json") << json;
		std::cout << json;
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "region audit failed: " << e.what() << '\n';
		return 1;
	}
}
