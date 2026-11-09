// Original-lightgrid observation diagnostic. No reconstruction/solver entry point.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_grid_evidence.h"
#include "lightrecon/offline.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <cmath>
using namespace OriginalGridEvidence;
static void Check(Error e) { if (!e) throw std::runtime_error(e.message); }
static std::string Quote(const std::string &s)
{
	std::string r = "\"";
	for (unsigned char c : s) { if (c == '"' || c == '\\') r += '\\'; if (c >= 32) r += char(c); }
	return r+'"';
}
static void Vector(std::ostream &out, Vec3 v) { out << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
static void Number(std::ostream &out, double v) { if (std::isfinite(v)) out << v; else out << "null"; }
static void Value(std::ostream &out, const Interval &v)
{
	if (!v.available) { out << "null"; return; }
	out << "{\"value\":" << v.value << ",\"code_bounds\":";
	if (v.bounded) out << '[' << v.lower << ',' << v.upper << ']'; else out << "null";
	out << '}';
}
static void ColourJson(std::ostream &out, const Colour &c)
{
	out << "{\"rgb\":[";
	for (unsigned j = 0; j < 3; ++j) { if (j) out << ','; Value(out,c.rgb[j]); }
	out << "],\"mean_rgb\":"; Value(out,c.intensity); out << ",\"decoder_flags\":" << c.decoderFlags << '}';
}
static void GradientJson(std::ostream &out, const Gradient &g)
{
	out << '[';
	for (unsigned a = 0; a < 3; ++a)
	{
		if (a) out << ',';
		out << "{\"rgb\":[";
		for (unsigned c = 0; c < 3; ++c) { if (c) out << ','; Value(out,g.rgb[a][c]); }
		out << "],\"mean_rgb\":"; Value(out,g.intensity[a]); out << '}';
	}
	out << ']';
}
static void CellJson(std::ostream &out, const Observation &r, uint64_t references)
{
	out << "{\"ordinal\":" << r.raw.ordinal << ",\"xyz\":[" << r.raw.coordinates[0] << ',' << r.raw.coordinates[1]
		<< ',' << r.raw.coordinates[2] << "],\"world\":"; Vector(out,r.raw.centreWorld);
	out << ",\"presence\":" << unsigned(r.raw.presence) << ",\"record\":";
	if (r.raw.indexPresent) out << r.raw.sourceRecord; else out << "null";
	out << ",\"record_references\":" << references << ",\"issues\":" << r.raw.issues
		<< ",\"space\":" << Quote(SpaceName(r.location.space)) << ",\"boundary\":" << r.location.boundary
		<< ",\"dynamic_possible\":" << r.dynamicPossible << ",\"lat_long\":[" << unsigned(r.raw.latLong[0])
		<< ',' << unsigned(r.raw.latLong[1]) << "],\"incoming_consumer256\":";
	if (r.raw.presence == GridPresence::Present) Vector(out,r.direction.consumer256); else out << "null";
	out << ",\"incoming_writer255_hypothesis\":";
	if (r.raw.presence == GridPresence::Present) Vector(out,r.direction.writer255); else out << "null";
	out << ",\"writer255_code_possible\":" << r.direction.writer255CodePossible;
	out << ",\"styles\":[";
	for (unsigned s = 0; s < 4; ++s)
	{
		if (s) out << ',';
		const auto &v = r.styles[s];
		out << "{\"slot\":" << s << ",\"id\":" << unsigned(v.id) << ",\"active\":" << v.active
			<< ",\"weak_directed\":" << v.weakDirected << ",\"raw_ambient\":[";
		for (unsigned c = 0; c < 3; ++c) out << (c ? "," : "") << unsigned(r.raw.ambient[s][c]);
		out << "],\"raw_directed\":[";
		for (unsigned c = 0; c < 3; ++c) out << (c ? "," : "") << unsigned(r.raw.directed[s][c]);
		out << "],\"ambient\":"; ColourJson(out,v.ambient);
		out << ",\"directed\":"; ColourJson(out,v.directed);
		out << ",\"ambient_gradient_xyz\":"; GradientJson(out,v.ambientGradient);
		out << ",\"directed_gradient_xyz\":"; GradientJson(out,v.directedGradient); out << '}';
	}
	out << "],\"edges\":[";
	for (unsigned e = 0; e < 6; ++e) out << (e ? "," : "") << Quote(EdgeName(r.edges[e]));
	out << "],\"hdr_unstyled_alternative\":";
	if (!r.raw.hdrPresent) out << "null";
	else { out << '['; for (unsigned i = 0; i < 6; ++i) { if (i) out << ','; Number(out,r.raw.hdr[i]); } out << ']'; }
	out << ",\"hdr_ambient_source_relative\":"; ColourJson(out,r.hdrAmbient);
	out << ",\"hdr_directed_source_relative\":"; ColourJson(out,r.hdrDirected);
	out << "}\n";
}
int main(int argc, char **argv)
{
	try
	{
		if (argc < 3) throw std::runtime_error("grid_evidence_tool original.jkobs report.json [--cells cells.jsonl] [--q3map2-merge4] [--source-relative]");
		Options options;
		std::string cellPath;
		for (int a = 3; a < argc; ++a)
		{
			std::string flag = argv[a];
			if (flag == "--q3map2-merge4") options.compression = Compression::Q3map2Merge4;
			else if (flag == "--source-relative") options.colourSpace = ColourSpace::ProfileSourceRelative;
			else if (flag == "--cells" && a+1 < argc) cellPath = argv[++a];
			else throw std::runtime_error("Unknown/incomplete option: "+flag);
		}
		std::shared_ptr<const Offline::Dataset> dataset;
		Check(Offline::OpenFile(argv[1],Offline::Options(),dataset));
		Offline::Sources source; Offline::RegionSnapshot snapshot; Check(dataset->Load(source,snapshot));
		if (!source.grid) throw std::runtime_error("No ORIGINAL BSP lightgrid; generated probes are not a fallback");
		Input in; in.grid = source.grid; in.domain = source.domain; in.dynamic = source.dynamic; in.profile = source.metadata.decoding;
		ReadContext read; read.maxBytes = 256ull << 20;
		std::shared_ptr<const StaticVisibility> visibility;
		if (source.geometry)
		{
			if (source.geometry->Map().sha256 != source.grid->Map().sha256) throw std::runtime_error("Geometry/grid identity mismatch");
			VisibilityBuildOptions vo; vo.maxBytes = read.maxBytes; vo.patchToleranceWorld = source.metadata.visibilityPatchToleranceWorld;
			Check(StaticVisibility::Build(source.geometry,vo,read,visibility,source.domain)); in.visibility = visibility;
		}
		const auto &grid = *source.grid; const auto &report = grid.Report();
		std::vector<uint64_t> references(size_t(report.records),0);
		GridCell cell;
		for (uint64_t i = 0; i < report.cells; ++i) { Check(grid.Cell(i,cell)); if (cell.presence == GridPresence::Present) ++references[cell.sourceRecord]; }
		std::map<std::string,uint64_t> unique;
		uint64_t referenced = 0, sharedRecords = 0, duplicateRecords = 0;
		for (size_t i = 0; i < references.size(); ++i)
		{
			if (references[i]) ++referenced;
			if (references[i] > 1) ++sharedRecords;
			std::string raw(reinterpret_cast<const char *>(grid.Records().data()+i*kGridRecordBytes),kGridRecordBytes);
			if (!unique.emplace(raw,i).second) ++duplicateRecords;
		}
		std::ofstream cells;
		if (!cellPath.empty()) { cells.open(cellPath,std::ios::binary); if (!cells) throw std::runtime_error("Cannot open cell output"); cells << std::setprecision(17) << std::boolalpha; }
		std::map<std::string,uint64_t> spaces, edges;
		uint64_t active = 0, weak = 0, gradients = 0, bounded = 0, dynamic = 0, boundaries = 0, unavailable = 0, writerImpossible = 0;
		uint64_t styles[256] = {}, presence[4] = {};
		for (uint64_t i = 0; i < report.cells; ++i)
		{
			Observation r; Check(Extract(in,options,i,read,r));
			++presence[unsigned(r.raw.presence)];
			writerImpossible += r.raw.presence == GridPresence::Present && !r.direction.writer255CodePossible;
			++spaces[SpaceName(r.location.space)]; dynamic += r.dynamicPossible; boundaries += r.location.boundary;
			for (auto e : r.edges) ++edges[EdgeName(e)];
			for (const auto &s : r.styles) if (s.active)
			{
				++active; ++styles[s.id]; weak += s.weakDirected;
				unavailable += !s.directed.intensity.available;
				for (auto g : s.directedGradient.intensity) { gradients += g.available; bounded += g.available && g.bounded; }
			}
			if (cells.is_open()) CellJson(cells,r,r.raw.presence == GridPresence::Present ? references[r.raw.sourceRecord] : 0);
		}
		if (cells.is_open()) { cells.flush(); if (!cells) throw std::runtime_error("Cell output write failed"); }
		std::ofstream out(argv[2],std::ios::binary); out << std::setprecision(17) << std::boolalpha;
		if (!out) throw std::runtime_error("Cannot open report");
		out << "{\"schema\":\"org.openjk.original-grid-evidence.v1\",\"map\":" << Quote(grid.Map().path)
			<< ",\"bsp_sha256\":" << Quote(Disk::Hex(grid.Map().sha256))
			<< ",\"records_sha256\":" << Quote(Disk::Hex(Disk::Hash(grid.Records().data(),grid.Records().size())))
			<< ",\"indices_sha256\":" << Quote(Disk::Hex(Disk::Hash(grid.Indices().data(),grid.Indices().size())))
			<< ",\"compiler_known\":" << source.metadata.compilerKnown << ",\"profile\":" << Quote(in.profile.identity)
			<< ",\"synthetic\":" << source.metadata.synthetic << ",\"compiler\":" << Quote(source.metadata.compiler)
			<< ",\"compiler_version\":" << Quote(source.metadata.compilerVersion)
			<< ",\"colour_space\":" << Quote(options.colourSpace == ColourSpace::EncodedRgb8 ? "encoded_rgb8_relative" : "explicit_profile_source_relative")
			<< ",\"absolute_calibration_known\":false,\"aggregate_is_individual_emitter\":false,\"generated_probes\":0"
			<< ",\"direction_reliability_known\":false,\"source_transfer\":" << unsigned(in.profile.source.documentedTransfer)
			<< ",\"source_assumed_transfer\":" << unsigned(in.profile.source.assumedTransfer)
			<< ",\"geometry_key\":" << (visibility ? Quote(Disk::Hex(visibility->CertificationKey())) : "null")
			<< ",\"compression\":" << Quote(options.compression == Compression::Unknown ? "unknown" : "q3map2_merge4_hypothesis")
			<< ",\"compression_bound_known\":" << (options.compression != Compression::Unknown)
			<< ",\"interpolation_error_known\":false,\"compiler_sample_nudge_known\":false,\"direction_shared_across_styles\":true"
			<< ",\"consumer_encoding_bound_radians\":" << InterpretDirection(0,0,options.compression).consumerEncodingBoundRadians
			<< ",\"writer255_encoding_bound_radians\":" << InterpretDirection(0,0,options.compression).writerEncodingBoundRadians
			<< ",\"merge_angular_bound_radians\":";
		if (options.compression == Compression::Unknown) out << "null"; else out << InterpretDirection(0,0,options.compression).mergeBoundRadians;
		out << ",\"origin\":"; Vector(out,grid.Layout().originWorld); out << ",\"spacing\":"; Vector(out,grid.Layout().spacingWorld);
		out << ",\"bounds\":[" << grid.Layout().bounds[0] << ',' << grid.Layout().bounds[1] << ',' << grid.Layout().bounds[2]
			<< "],\"cells\":" << report.cells << ",\"records\":" << report.records << ",\"referenced_records\":" << referenced
			<< ",\"shared_records\":" << sharedRecords << ",\"reused_cell_references\":" << report.cells-report.missingCells-referenced
			<< ",\"byte_identical_extra_records\":" << duplicateRecords << ",\"missing_cells\":" << report.missingCells
			<< ",\"invalid_indices\":" << report.invalidIndices << ",\"style_gaps\":" << report.styleGaps << ",\"no_style\":" << report.noStyle
			<< ",\"hdr_cells\":" << report.hdrCells << ",\"hdr_nonfinite\":" << report.nonFiniteHdr << ",\"hdr_negative\":" << report.negativeHdr
			<< ",\"hdr_state\":" << unsigned(grid.HdrState())
			<< ",\"hdr_sha256\":" << Quote(Disk::Hex(Disk::Hash(grid.Hdr().data(),grid.Hdr().size()*sizeof(float))))
			<< ",\"issues\":" << report.issues << ",\"boundary_cells\":" << boundaries << ",\"dynamic_possible_cells\":" << dynamic
			<< ",\"writer255_impossible_code_cells\":" << writerImpossible
			<< ",\"active_style_observations\":" << active << ",\"weak_directed_style_observations\":" << weak
			<< ",\"unavailable_colour_observations\":" << unavailable << ",\"directed_gradient_axes\":" << gradients
			<< ",\"code_bounded_directed_gradient_axes\":" << bounded << ",\"space_counts\":{";
		bool comma = false;
		for (const char *s : {"free","solid","outside","sky","exterior","unknown"}) { out << (comma ? "," : "") << Quote(s) << ':' << spaces[s]; comma = true; }
		out << "},\"presence_counts\":{\"absent\":" << presence[0] << ",\"present\":" << presence[1]
			<< ",\"missing_index\":" << presence[2] << ",\"invalid_index\":" << presence[3] << "},\"edge_counts\":{"; comma = false;
		for (const char *s : {"unsupported","clear","blocked","unknown","dynamic"}) { out << (comma ? "," : "") << Quote(s) << ':' << edges[s]; comma = true; }
		out << "},\"style_counts\":{"; comma = false;
		for (unsigned s = 0; s < 255; ++s) if (styles[s]) { out << (comma ? "," : "") << Quote(std::to_string(s)) << ':' << styles[s]; comma = true; }
		out << "}}\n"; out.flush(); if (!out) throw std::runtime_error("Report write failed");
		std::cout << report.cells << " captured grid cells; " << gradients << " directed gradient axes\n";
		return 0;
	}
	catch (const std::exception &e) { std::cerr << "original grid diagnostic: " << e.what() << '\n'; return 1; }
}
