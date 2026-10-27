// Trusted dataset controller. Outputs are NOT an optimizer input; see blind_views.py.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Rend2LightRecon::Core::Offline;
namespace
{
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
std::string Quote(const std::string &s)
{
	std::string r = "\"";
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
			r += '\\';
		if (c >= 32)
			r += char(c);
		else
		{
			const char *h = "0123456789abcdef";
			r += "\\u00";
			r += h[c >> 4];
			r += h[c & 15];
		}
	}
	return r + '"';
}
void Vector(std::ostream &o, Vec3 p)
{
	o << '[' << p.x << ',' << p.y << ',' << p.z << ']';
}
void Facts(const Sources &s, const std::string &prefix)
{
	std::ofstream out(prefix + ".facts.json", std::ios::binary);
	if (!out)
		throw std::runtime_error("Cannot write source facts");
	uint64_t kinds[5] = {}, motions[4] = {}, styles[256] = {}, signals[13] = {}, missingCharts = 0,
			 vertexColours = 0;
	std::set<uint32_t> hinted;
	if (s.evidence)
		for (const auto &e : s.evidence->Evidence())
			for (auto n : e.surfaces)
				hinted.insert(n);
	if (s.geometry)
		for (const auto &v : s.geometry->Surfaces())
		{
			++kinds[unsigned(v.kind)];
			++motions[unsigned(v.motion)];
			for (unsigned slot = 0; slot < 4; ++slot)
				if (v.styles[slot] != 255)
				{
					++styles[v.styles[slot]];
					missingCharts += v.lightmap[slot] < 0;
				}
			for (unsigned bit = 0; bit < 13; ++bit)
				signals[bit] += (v.materialFacts.signals >> bit) & 1;
			for (const auto &p : v.vertices)
				vertexColours += p.colourPresent;
		}
	out << "{\"surface_denominator\":" << (s.geometry ? s.geometry->Surfaces().size() : 0)
		<< ",\"kind_counts\":[";
	for (unsigned i = 0; i < 5; ++i)
	{
		if (i)
			out << ',';
		out << kinds[i];
	}
	out << "],\"motion_counts\":[";
	for (unsigned i = 0; i < 4; ++i)
	{
		if (i)
			out << ',';
		out << motions[i];
	}
	out << "],\"material_signal_counts\":[";
	for (unsigned i = 0; i < 13; ++i)
	{
		if (i)
			out << ',';
		out << signals[i];
	}
	out << "],\"source_style_counts\":[";
	for (unsigned i = 0; i < 256; ++i)
	{
		if (i)
			out << ',';
		out << styles[i];
	}
	out << "],\"style_channels_without_lightmap\":" << missingCharts
		<< ",\"vertices_with_original_colour\":" << vertexColours
		<< ",\"surfaces_referenced_by_source_hints\":" << hinted.size()
		<< ",\"fixture_geometry_priors\":[";
	bool comma = false;
	if (s.evidence)
		for (const auto &e : s.evidence->Evidence())
			if (e.origin == EvidenceOrigin::GlowSurface && e.staticEligible && e.positionKnown)
			{
				if (comma)
					out << ',';
				comma = true;
				out << "{\"id\":\"" << e.id << "\",\"position\":";
				Vector(out, e.position);
				out << ",\"shapes\":" << e.shapes << '}';
			}
	out << "]}\n";
	if (!out)
		throw std::runtime_error("Source facts write failed");
}
struct Progress : ExportProgress, Cancellation
{
	uint64_t tiles = 0, cancelAt = 0;
	uint32_t previous = UINT32_MAX;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now(),
										  page = started;
	void Tile(uint32_t image, uint32_t, uint32_t, uint64_t samples) override
	{
		++tiles;
		if (previous != image)
		{
			auto now = std::chrono::steady_clock::now();
			if (previous != UINT32_MAX)
				std::cerr << "page=" << previous
						  << " seconds=" << std::chrono::duration<double>(now - page).count()
						  << '\n';
			previous = image;
			page = now;
		}
		if (!(tiles % 256))
			std::cerr << "tiles=" << tiles << " samples=" << samples << '\n';
	}
	bool IsCancelled() const noexcept override
	{
		return cancelAt && tiles >= cancelAt;
	}
};
double Radius(const BakedSurface &s, const SurfaceLightingObservation &sample,
			  const LightmapImage &image, uint64_t &work)
{
	// A conservative bound, not sqrt(area): skinny footprints can be very long.
	// Patch convex hull bounds the entire receiver. Affine triangles use all four
	// pixel corners for every triangle whose UV bounding box intersects the pixel.
	double radius = 0;
	auto distance = [&](Vec3 p) {
		double x = p.x - sample.positionWorld.x, y = p.y - sample.positionWorld.y,
			   z = p.z - sample.positionWorld.z;
		radius = std::max(radius, std::sqrt(x * x + y * y + z * z));
	};
	if (s.kind == BakedSurfaceKind::Patch)
	{
		for (const auto &v : s.vertices)
		{
			if (++work > 512ull * 1024 * 1024)
				throw std::runtime_error("Footprint analysis work budget");
			distance(v.position);
		}
		return radius;
	}
	const double u0 = double(sample.x) / image.width, u1 = double(sample.x + 1) / image.width,
				 v0 = double(sample.y) / image.height, v1 = double(sample.y + 1) / image.height;
	for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
	{
		if (++work > 512ull * 1024 * 1024)
			throw std::runtime_error("Footprint analysis work budget");
		const auto &a = s.vertices[s.indices[i]], &b = s.vertices[s.indices[i + 1]],
				   &c = s.vertices[s.indices[i + 2]];
		auto av = a.lightmap[sample.styleSlot], bv = b.lightmap[sample.styleSlot],
			 cv = c.lightmap[sample.styleSlot];
		if (std::max({av.u, bv.u, cv.u}) < u0 || std::min({av.u, bv.u, cv.u}) > u1 ||
			std::max({av.v, bv.v, cv.v}) < v0 || std::min({av.v, bv.v, cv.v}) > v1)
			continue;
		double eu = bv.u - av.u, ev = bv.v - av.v, fu = cv.u - av.u, fv = cv.v - av.v,
			   det = eu * fv - ev * fu;
		if (std::abs(det) < 1e-14)
			continue;
		for (double u : {u0, u1})
			for (double v : {v0, v1})
			{
				double q = ((u - av.u) * fv - (v - av.v) * fu) / det,
					   r = (eu * (v - av.v) - ev * (u - av.u)) / det;
				distance(Vec3(float(a.position.x + q * (b.position.x - a.position.x) +
									r * (c.position.x - a.position.x)),
							  float(a.position.y + q * (b.position.y - a.position.y) +
									r * (c.position.y - a.position.y)),
							  float(a.position.z + q * (b.position.z - a.position.z) +
									r * (c.position.z - a.position.z))));
			}
	}
	return radius + sample.mappingErrorBoundWorld + 1e-4;
}
void Scan(const Offline::Dataset &data, Sources &s, const RegionSnapshot &graph,
		  const std::string &prefix, const ReadContext &ctx)
{
	std::remove((prefix + ".source.json").c_str());
	std::ofstream rows(prefix + ".rows.jsonl", std::ios::binary),
		metadata(prefix + ".source.json.tmp", std::ios::binary);
	if (!rows || !metadata)
		throw std::runtime_error("Cannot create scan outputs");
	rows << std::setprecision(10);
	std::map<uint32_t, const BakedSurface *> surfaces;
	if (s.geometry)
		for (const auto &v : s.geometry->Surfaces())
			surfaces[v.ordinal] = &v;
	auto phase = std::chrono::steady_clock::now();
	std::shared_ptr<const StaticVisibility> rays;
	std::shared_ptr<const LightingRegionGraph> regions;
	if (s.regionSnapshot)
		Check(ReplayGeometry(s, graph, ctx, rays, regions));
	if (!rays && s.geometry)
	{
		VisibilityBuildOptions o;
		o.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
		o.maxBytes = ctx.maxBytes;
		Check(StaticVisibility::Build(s.geometry, o, ctx, rays, s.domain));
	}
	std::cerr << "geometry_prepare_seconds="
			  << std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count()
			  << '\n';
	phase = std::chrono::steady_clock::now();
	uint64_t written = 0, footprintWork = 0;
	size_t maskedImage = SIZE_MAX;
	std::vector<uint8_t> chartMask;
	SurfaceMappingIndex pageIndex;
	Check(SurfaceMappingIndex::Build(s.geometry, ctx, pageIndex));
	for (size_t entry = 0; entry < data.Directory().size(); ++entry)
		if (data.Directory()[entry].kind == 15)
		{
			std::vector<Luxel> tile;
			Check(data.ReadLuxels(entry, tile));
			for (const auto &l : tile)
			{
				const auto &g = l.surface;
				const auto &im = s.images->Images()[g.image];
				if (maskedImage != g.image)
				{
					if (uint64_t(im.width) * im.height > ctx.maxBytes / 8)
						throw std::runtime_error("Chart-mask memory budget");
					chartMask.assign(size_t(im.width) * im.height, 0);
					maskedImage = g.image;
					const auto *page = pageIndex.Page(im.logicalIndex);
					uint64_t work = 0;
					if (page)
						for (auto ordinal : *page)
						{
							const auto &v = s.geometry->Surfaces()[ordinal];
							if (v.kind == BakedSurfaceKind::Flare)
								continue;
							uint8_t flag =
								uint8_t((v.excludedMaterial ? 2 : 1) |
										(v.motion != SurfaceMotion::StaticWorld ? 4 : 0) |
										(v.kind == BakedSurfaceKind::Unsupported ? 8 : 0));
							for (unsigned slot = 0; slot < 4; ++slot)
								if (v.lightmap[slot] >= 0 &&
									uint32_t(v.lightmap[slot]) == im.logicalIndex &&
									v.styles[slot] != 255)
								{
									double u0 = v.uvBoundsKnown[slot] ? v.uvMin[slot].u : 0,
										   u1 = v.uvBoundsKnown[slot] ? v.uvMax[slot].u : 1;
									double v0 = v.uvBoundsKnown[slot] ? v.uvMin[slot].v : 0,
										   v1 = v.uvBoundsKnown[slot] ? v.uvMax[slot].v : 1;
									uint32_t x0 = uint32_t(
												 std::max(0., std::min(double(im.width),
																	   std::floor(u0 * im.width)))),
											 x1 = uint32_t(
												 std::max(0., std::min(double(im.width),
																	   std::ceil(u1 * im.width)))),
											 y0 = uint32_t(std::max(
												 0., std::min(double(im.height),
															  std::floor(v0 * im.height)))),
											 y1 = uint32_t(
												 std::max(0., std::min(double(im.height),
																	   std::ceil(v1 * im.height))));
									for (uint32_t yy = y0; yy < y1; ++yy)
										for (uint32_t xx = x0; xx < x1; ++xx)
										{
											if (++work > 128ull * 1024 * 1024)
												throw std::runtime_error("Chart-mask work budget");
											chartMask[size_t(yy) * im.width + xx] |= flag;
										}
								}
						}
				}
				auto found = surfaces.find(g.surface);
				const BakedSurface *surface =
					g.positionKnown && found != surfaces.end() ? found->second : nullptr;
				rows << "{\"id\":"
					 << Quote(Disk::Hex(
							SampleIdentity(s.metadata.map, im, g, l.classification.style.id)))
					 << ",\"image\":" << g.image << ",\"page\":" << g.logicalLightmap
					 << ",\"x\":" << g.x << ",\"y\":" << g.y << ",\"surface\":" << g.surface
					 << ",\"slot\":" << g.styleSlot
					 << ",\"style\":" << unsigned(l.classification.style.id)
					 << ",\"coverage\":" << unsigned(g.coverage) << ",\"chart_reference_flags\":"
					 << unsigned(chartMask[size_t(g.y) * im.width + g.x])
					 << ",\"issues\":" << g.issues
					 << ",\"classification\":" << l.classification.reasons
					 << ",\"fit_candidate\":" << l.classification.diffuseFitCandidate
					 << ",\"material_signals\":" << l.classification.materialSignals
					 << ",\"material_uncertainty\":" << l.classification.materialUncertainty
					 << ",\"kind\":" << (surface ? int(surface->kind) : -1)
					 << ",\"material\":" << (surface ? Quote(surface->material) : "null")
					 << ",\"position\":";
				if (g.positionKnown)
					Vector(rows, g.positionWorld);
				else
					rows << "null";
				rows << ",\"normal\":";
				if (g.normalOrientationKnown)
					Vector(rows, g.geometricNormal);
				else
					rows << "null";
				rows << ",\"shading_normal\":";
				if (g.shadingNormalKnown)
					Vector(rows, g.shadingNormal);
				else
					rows << "null";
				rows << ",\"area\":" << g.areaWorldSquared
					 << ",\"area_error\":" << g.areaErrorEstimate << ",\"footprint_radius\":"
					 << (surface ? Radius(*surface, g, im, footprintWork) : 0)
					 << ",\"encoding\":" << unsigned(l.colour.raw.encoding) << ",\"raw\":[";
				for (unsigned c = 0; c < 3; ++c)
				{
					if (c)
						rows << ',';
					if (l.colour.raw.encoding == Decode::Encoding::LinearRgb32F)
					{
						float v = c == 0 ? l.colour.raw.hdr.x
										 : c == 1 ? l.colour.raw.hdr.y : l.colour.raw.hdr.z;
						if (std::isfinite(v))
							rows << v;
						else
							rows << "null";
					}
					else
						rows << unsigned(l.colour.raw.bytes[c]);
				}
				rows << "],\"channel_flags\":[" << l.colour.channels[0] << ','
					 << l.colour.channels[1] << ',' << l.colour.channels[2]
					 << "],\"transfer\":" << unsigned(l.colour.resolvedTransfer)
					 << ",\"primaries\":" << unsigned(l.colour.resolvedPrimaries) << ",\"source\":";
				if (l.colour.source.state == Decode::State::Available)
					Vector(rows, l.colour.source.rgb);
				else
					rows << "null";
				rows << ",\"scene\":";
				if (l.colour.scene.state == Decode::State::Available)
					Vector(rows, l.colour.scene.rgb);
				else
					rows << "null";
				rows << ",\"deluxe_state\":" << unsigned(l.deluxe.state) << ",\"deluxe\":";
				if (l.deluxe.state == DeluxeState::Available)
					Vector(rows, l.deluxe.toSource);
				else
					rows << "null";
				SurfaceRegionSides sides;
				if (regions && surface && g.normalOrientationKnown)
					Check(regions->AssignSurfaceSides(g.positionWorld, g.geometricNormal,
													  Id(g.surface) + 1, ctx, sides));
				rows << ",\"positive_region\":" << sides.positive.region
					 << ",\"negative_region\":" << sides.negative.region
					 << ",\"region_state\":" << unsigned(sides.positive.state)
					 << ",\"region_reasons\":" << sides.positive.issues;
				// Local absence of geometry is not a segment-visibility certificate.
				GeometryBoxResult local;
				if (surface && rays)
				{
					Vec3 origin;
					if (LuxelRayOrigin(g, origin))
					{
						Bounds b;
						b.known = true;
						b.minWorld = Vec3(origin.x - .01f, origin.y - .01f, origin.z - .01f);
						b.maxWorld = Vec3(origin.x + .01f, origin.y + .01f, origin.z + .01f);
						Check(rays->OverlapBox(b, ctx, local));
					}
				}
				rows << ",\"local_geometry_complete\":" << local.coverageComplete
					 << ",\"local_geometry_intersects\":" << local.intersects
					 << ",\"local_geometry_issues\":" << local.issues << "}\n";
				++written;
			}
			if (!rows)
				throw std::runtime_error("Sample stream write failed");
		}
	std::cerr << "tile_scan_seconds="
			  << std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count()
			  << '\n';
	phase = std::chrono::steady_clock::now();
	std::ofstream gridRows(prefix + ".grid.jsonl", std::ios::binary);
	if (!gridRows)
		throw std::runtime_error("Cannot write grid stream");
	gridRows << std::setprecision(10);
	Decode::Decoder decoder(s.metadata.decoding);
	uint64_t gridPresent = 0, gridDirections = 0, gridSuspect = 0, gridAssigned = 0, gridStyled = 0;
	if (s.grid)
		for (uint64_t i = 0; i < s.grid->Report().cells; ++i)
		{
			GridCell cell;
			Check(s.grid->Cell(i, cell));
			gridPresent += cell.presence == GridPresence::Present;
			gridDirections += cell.direction.quality != GridDirectionQuality::Unavailable;
			gridSuspect += cell.suspectedInvalid;
			gridStyled +=
				cell.styleStatus != GridStyle::None && cell.styleStatus != GridStyle::Unavailable;
			if (regions)
			{
				RegionAssignment a;
				Check(regions->AssignGrid(cell, ctx, a));
				gridAssigned += a.state == RegionAssignmentState::Assigned;
			}
			if (cell.presence != GridPresence::Present || cell.suspectedInvalid ||
				cell.styleStatus == GridStyle::None || cell.styleStatus == GridStyle::Unavailable)
				continue;
			const auto &spacing = s.grid->Layout().spacingWorld;
			double radius =
				.5 * std::sqrt(double(spacing.x) * spacing.x + double(spacing.y) * spacing.y +
							   double(spacing.z) * spacing.z);
			gridRows << "{\"ordinal\":" << i << ",\"record\":" << cell.sourceRecord
					 << ",\"position\":";
			Vector(gridRows, cell.centreWorld);
			gridRows << ",\"footprint_radius\":" << radius << ",\"styles\":[";
			for (unsigned slot = 0; slot < 4; ++slot)
			{
				if (slot)
					gridRows << ',';
				gridRows << unsigned(cell.styles[slot]);
			}
			gridRows << "],\"ambient\":[";
			for (unsigned slot = 0; slot < 4; ++slot)
			{
				if (slot)
					gridRows << ',';
				gridRows << '[' << unsigned(cell.ambient[slot][0]) << ','
						 << unsigned(cell.ambient[slot][1]) << ','
						 << unsigned(cell.ambient[slot][2]) << ']';
			}
			gridRows << "],\"directed\":[";
			for (unsigned slot = 0; slot < 4; ++slot)
			{
				if (slot)
					gridRows << ',';
				gridRows << '[' << unsigned(cell.directed[slot][0]) << ','
						 << unsigned(cell.directed[slot][1]) << ','
						 << unsigned(cell.directed[slot][2]) << ']';
			}
			gridRows << "],\"direction_bytes\":[" << unsigned(cell.latLong[0]) << ','
					 << unsigned(cell.latLong[1]) << "],\"to_source\":";
			Vector(gridRows, cell.direction.toSource);
			gridRows << ",\"angular_bound\":" << cell.direction.errorBoundRadians << ",\"hdr\":";
			if (cell.hdrPresent)
			{
				gridRows << '[';
				for (unsigned c = 0; c < 6; ++c)
				{
					if (c)
						gridRows << ',';
					if (std::isfinite(cell.hdr[c]))
						gridRows << cell.hdr[c];
					else
						gridRows << "null";
				}
				gridRows << ']';
			}
			else
				gridRows << "null";
			gridRows << ",\"ambient_source\":";
			Decode::Observation value;
			auto decoded = DecodeGridLdr(decoder, cell, 0, Decode::Component::GridAmbient, value);
			if (decoded && value.source.state == Decode::State::Available)
				Vector(gridRows, value.source.rgb);
			else
				gridRows << "null";
			gridRows << ",\"directed_source\":";
			decoded = DecodeGridLdr(decoder, cell, 0, Decode::Component::GridDirected, value);
			if (decoded && value.source.state == Decode::State::Available)
				Vector(gridRows, value.source.rgb);
			else
				gridRows << "null";
			gridRows << "}\n";
		}
	if (!gridRows)
		throw std::runtime_error("Grid stream write failed");
	std::cerr << "grid_scan_seconds="
			  << std::chrono::duration<double>(std::chrono::steady_clock::now() - phase).count()
			  << '\n';
	metadata << "{\"schema\":\"org.openjk.observation-quality.v1\",\"map\":"
			 << Quote(s.metadata.map.path)
			 << ",\"map_sha256\":" << Quote(Disk::Hex(s.metadata.map.sha256))
			 << ",\"compiler_known\":" << s.metadata.compilerKnown
			 << ",\"synthetic\":" << s.metadata.synthetic
			 << ",\"compiler_version\":" << Quote(s.metadata.compilerVersion)
			 << ",\"recipe_sha256\":" << Quote(s.metadata.recipeSha256)
			 << ",\"derived_complete\":" << data.Counts().derivedComplete
			 << ",\"raw_pixels\":" << data.Counts().imagePixels << ",\"rows\":" << written
			 << ",\"grid_cells\":" << data.Counts().gridCells << ",\"grid_present\":" << gridPresent
			 << ",\"grid_directions_quantized_uncalibrated\":" << gridDirections
			 << ",\"grid_suspect\":" << gridSuspect << ",\"grid_assigned\":" << gridAssigned
			 << ",\"grid_with_style\":" << gridStyled
			 << ",\"candidates\":" << data.Counts().candidates
			 << ",\"regions\":" << data.Counts().regions
			 << ",\"visibility_complete\":" << (rays && rays->Report().complete)
			 << ",\"visibility_spatial_complete\":"
			 << (rays && rays->Report().spatialCoverageComplete)
			 << ",\"world_units_per_metre\":" << s.metadata.units.worldUnitsPerMeter
			 << ",\"pages\":[";
	bool comma = false;
	if (s.images)
		for (size_t i = 0; i < s.images->Images().size(); ++i)
		{
			const auto &im = s.images->Images()[i];
			if (im.role != LightmapRole::Lighting)
				continue;
			if (comma)
				metadata << ',';
			comma = true;
			metadata << "{\"image\":" << i << ",\"page\":" << im.logicalIndex
					 << ",\"width\":" << im.width << ",\"height\":" << im.height
					 << ",\"missing\":" << im.missing
					 << ",\"pixel_sha256\":" << Quote(Disk::Hex(im.pixelHash)) << '}';
		}
	metadata << "]}\n";
	if (!metadata)
		throw std::runtime_error("Metadata write failed");
	// Public geometry package: no source images, grid colours/directions, vertex
	// colours/HDR or retained source-light entities/intensities. Same CPU ray geometry.
	SurfaceCapture capture;
	capture.Begin(BuildPolicy::ForceRebuild);
	Check(capture.SetMap(s.metadata.map));
	if (s.geometry)
		for (auto v : s.geometry->Surfaces())
		{
			for (auto &vertex : v.vertices)
			{
				vertex.colourPresent = false;
				std::memset(vertex.colour, 0, sizeof(vertex.colour));
			}
			v.materialFacts.authoredLuminance = v.materialFacts.legacySurfaceLight = 0;
			v.materialFacts.legacySourceColour = {};
			Check(capture.Add(std::move(v)));
		}
	Check(capture.Freeze());
	s.geometry = capture.Acquire();
	s.images.reset();
	s.grid.reset();
	s.regions.reset();
	s.regionSnapshot.reset();
	s.metadata.hints = HintPolicy::BlindBenchmark;
	s.metadata.recipe.clear();
	// The complete controlled bake recipe stays with the evaluator. Do not claim
	// full recipe provenance for this deliberately redacted geometry package.
	s.metadata.compilerKnown = false;
	s.metadata.recipeSha256.clear();
	Options o;
	o.mappedImages = 0;
	Summary summary;
	Check(ExportFile(prefix + ".public.jkobs", s, o, ctx, summary));
	rows.close();
	gridRows.close();
	metadata.close();
	if (!rows || !gridRows || !metadata ||
		std::rename((prefix + ".source.json.tmp").c_str(), (prefix + ".source.json").c_str()))
		throw std::runtime_error("Scan publication failed");
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		if (argc < 4)
			throw std::runtime_error("usage: lightrecon_dataset_quality derive source output "
									 "[firstPage count tileSide cancelTiles] | scan source prefix");
		Options o;
		o.mappedImages = 0;
		std::shared_ptr<const Offline::Dataset> data;
		Check(OpenFile(argv[2], o, data));
		Sources s;
		RegionSnapshot graph;
		auto start = std::chrono::steady_clock::now();
		Check(data->Load(s, graph));
		std::cerr << "source_load_seconds="
				  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
				  << '\n';
		Progress progress;
		ReadContext ctx;
		ctx.maxBytes = o.maxResidentBytes;
		ctx.cancellation = &progress;
		std::string command = argv[1];
		if (command == "derive")
		{
			if (argc > 4)
				o.firstMappedImage = uint32_t(std::stoul(argv[4]));
			if (argc > 5)
				o.mappedImages = uint32_t(std::stoul(argv[5]));
			if (argc > 6)
				o.tileSide = uint32_t(std::stoul(argv[6]));
			if (argc > 7)
				progress.cancelAt = std::stoull(argv[7]);
			std::shared_ptr<const StaticVisibility> rays;
			if (s.regionSnapshot)
				Check(ReplayGeometry(s, graph, ctx, rays, s.regions));
			o.progress = &progress;
			Summary summary;
			Check(ExportFile(argv[3], s, o, ctx, summary));
			std::cout << "samples=" << summary.mappedSamples
					  << " complete=" << summary.derivedComplete << " bytes=" << summary.fileBytes
					  << '\n';
		}
		else if (command == "scan")
			Scan(*data, s, graph, argv[3], ctx);
		else if (command == "facts")
			Facts(s, argv[3]);
		else if (command == "public-check")
		{
			if (s.images || s.grid || !s.geometry || !s.geometry->VertexHdr().empty() ||
				s.metadata.hints != HintPolicy::BlindBenchmark || !s.metadata.recipe.empty())
				throw std::runtime_error("Private lighting data in public geometry package");
			for (const auto &v : s.geometry->Surfaces())
			{
				if (v.materialFacts.authoredLuminance || v.materialFacts.legacySurfaceLight)
					throw std::runtime_error("Source intensity in public material");
				for (const auto &p : v.vertices)
				{
					if (p.colourPresent)
						throw std::runtime_error("Vertex RGB in public geometry");
					for (const auto &slot : p.colour)
						for (auto c : slot)
							if (c)
								throw std::runtime_error("Hidden vertex RGB");
				}
			}
			if (s.evidence)
				for (const auto &e : s.evidence->Evidence())
					if ((e.origin != EvidenceOrigin::GlowSurface &&
						 e.origin != EvidenceOrigin::SkyShader) ||
						!e.entities.empty() || !e.raw.empty() || e.intensityKnown ||
						e.intensity != 0)
						throw std::runtime_error("Source-answer evidence in public geometry");
			std::cout << "public geometry verified: no images/grid/vertex RGB/HDR/source answers\n";
		}
		else
			throw std::runtime_error("Unknown command");
		std::cerr << "elapsed_seconds="
				  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
				  << '\n';
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
