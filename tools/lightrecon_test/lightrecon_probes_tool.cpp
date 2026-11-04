// Geometry-only stock audit. No observed RGB, emitters, solver or field values.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/offline.h"
#include "lightrecon/probes.h"
#include "lightrecon/diagnostics.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace P = Probes;
static Disk::Digest DomainKey(const BspRegionDomain &domain)
{
	const auto &s = domain.Source();
	Core::GeometryKey k;
	k.U(P::kVersion);
	k.Digest(s.identity.sha256);
	k.B(s.worldBounds);
	for (auto &p : s.planes)
	{
		k.V(p.normal);
		k.F(p.distance);
	}
	for (auto &n : s.nodes)
	{
		k.U(n.plane);
		k.U(n.children[0]);
		k.U(n.children[1]);
	}
	for (auto &l : s.leaves)
	{
		k.U(l.cluster);
		k.U(l.area);
		k.U(l.firstBrush);
		k.U(l.brushCount);
	}
	for (auto &b : s.brushes)
	{
		k.U(b.firstSide);
		k.U(b.sideCount);
		k.U(b.solid);
		k.U(b.world);
		k.U(b.sky);
	}
	for (auto x : s.sides)
		k.U(x);
	for (auto x : s.leafBrushes)
		k.U(x);
	return k.Finish();
}
static void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
static double Radical(unsigned j, unsigned base)
{
	double r = 0, f = 1;
	while (j)
	{
		f /= base;
		r += (j % base) * f;
		j /= base;
	}
	return r;
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 3)
			throw std::runtime_error("probes_tool captured.jkobs report.json");
		auto start = std::chrono::steady_clock::now();
		std::shared_ptr<const Offline::Dataset> dataset;
		Offline::Options limits;
		Check(Offline::OpenFile(argv[1], limits, dataset));
		Offline::Sources source;
		Offline::RegionSnapshot snapshot;
		Check(dataset->Load(source, snapshot));
		if (!source.domain || !source.geometry)
			throw std::runtime_error("Missing original geometry");
		ReadContext read;
		read.maxBytes = 256ull << 20;
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Check(capture.SetMap(source.metadata.map));
		std::vector<Vec3> anchors;
		for (auto s : source.geometry->Surfaces())
		{
			for (auto &v : s.vertices)
				v.colourPresent = false;
			if (s.motion == SurfaceMotion::StaticWorld && s.kind != BakedSurfaceKind::Flare &&
				!s.vertices.empty())
			{
				auto p = s.vertices[s.vertices.size() / 2].position;
				if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z))
					anchors.push_back(p);
			}
			Check(capture.Add(std::move(s)));
		}
		Check(capture.Freeze());
		VisibilityBuildOptions visibility;
		visibility.maxBytes = 256ull << 20;
		visibility.patchToleranceWorld = source.metadata.visibilityPatchToleranceWorld;
		std::shared_ptr<const StaticVisibility> geometry;
		Check(
			StaticVisibility::Build(capture.Acquire(), visibility, read, geometry, source.domain));
		if (anchors.size() < 4)
			throw std::runtime_error("Insufficient geometry anchors");
		double load =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::ofstream out(argv[2]);
		out << std::setprecision(17) << "{\"schema\":\"org.openjk.probe-stock.v1\",\"map\":\""
			<< source.metadata.map.path << "\",\"bsp_sha256\":\""
			<< Disk::Hex(source.metadata.map.sha256) << "\",\"geometry_key\":\""
			<< Disk::Hex(geometry->CertificationKey())
			<< "\",\"photometric_truth\":null,\"lighting_values\":0,\"rgb_used\":false,\"geometry_"
			   "load_seconds\":"
			<< load << ",\"rois\":[";
		for (unsigned roi = 0; roi < 3; ++roi)
		{
			auto point = anchors[(roi + 1) * anchors.size() / 4];
			P::Options o;
			o.bounds.known = true;
			o.bounds.minWorld = Vec3(point.x - 256, point.y - 256, point.z - 256);
			o.bounds.maxWorld = Vec3(point.x + 256, point.y + 256, point.z + 256);
			o.coarseSpacingWorld = 128;
			o.minimumSpacingWorld = 32;
			o.clearanceWorld = 1;
			o.maxWork = 32000000;
			o.maxNodes = 10000;
			o.maxProbes = 10000;
			o.maxLinks = 80000;
			P::Input input;
			input.map = source.metadata.map.sha256;
			input.geometry = geometry->CertificationKey();
			input.domainKey = DomainKey(*source.domain);
			input.domain = source.domain;
			input.visibility = geometry;
			input.dynamic = source.dynamic;
			auto begin = std::chrono::steady_clock::now();
			RegionBuildInput regionInput;
			regionInput.identity = source.metadata.map;
			regionInput.domain = source.domain;
			regionInput.geometry = geometry;
			regionInput.dynamic = source.dynamic;
			RegionOptions regionOptions;
			regionOptions.bounds = o.bounds;
			regionOptions.coarseCellWorld = 128;
			regionOptions.minimumCellWorld = 32;
			regionOptions.inspectSky = false;
			regionOptions.maxNodes = 10000;
			regionOptions.maxCells = 10000;
			regionOptions.maxLinks = 80000;
			auto regionError =
				LightingRegionGraph::Build(regionInput, regionOptions, read, input.regions);
			std::shared_ptr<const P::Topology> topology;
			auto error = P::Build(input, o, read, topology);
			if(error){
				Diagnostics::Scene debug;debug.map=input.map;debug.mapPath=source.metadata.map.path;
				debug.algorithm="P02 geometry-only generated probe support";debug.scope="No field values or recovered emitter evidence";debug.units=source.metadata.units;
				Diagnostics::Builder builder(debug);builder.Probes(*topology);
				for(const auto&d:source.dynamic)builder.Dynamic(d);
				std::string encoded;Check(Diagnostics::Encode(debug,encoded));
				std::string stem=std::string(argv[2])+"-roi-"+std::to_string(roi);
				std::ofstream data(stem+".jkdbg",std::ios::binary);data.write(encoded.data(),encoded.size());
				std::ofstream json(stem+".debug.json"),obj(stem+".obj");Check(Diagnostics::Json(debug,json));Check(Diagnostics::Obj(debug,obj));
			}
			double build =
				std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
			out << (roi ? "," : "") << "{\"ordinal\":" << roi
				<< ",\"anchor_surface_quantile\":" << double(roi + 1) / 4
				<< ",\"build_code\":" << unsigned(error.code)
				<< ",\"region_build_code\":" << unsigned(regionError.code)
				<< ",\"regions\":" << (input.regions ? input.regions->Regions().size() : 0)
				<< ",\"build_seconds\":" << build;
			if (!error)
			{
				out << ",\"completed\":false}";
				continue;
			}
			const auto &r = topology->Diagnostics();
			uint64_t support = 0, strict = 0, conditional = 0, free = 0, unknown = 0, blocked = 0,
					 verified = 0, unknownRay = 0;
			P::QueryPolicy policy;
			policy.allowConditionalStatic = true;
			auto queryStart = std::chrono::steady_clock::now();
			for (unsigned j = 1; j <= 1024; ++j)
			{
				Vec3 p(o.bounds.minWorld.x + float(512 * Radical(j, 2)),
					   o.bounds.minWorld.y + float(512 * Radical(j, 3)),
					   o.bounds.minWorld.z + float(512 * Radical(j, 5)));
				SpatialLocation loc;
				Check(source.domain->Locate(p, loc));
				free += loc.space == SpaceClass::Free || loc.space == SpaceClass::Exterior;
				unknown += loc.space == SpaceClass::Unknown;
				auto s = topology->Lookup(p, policy);
				strict += topology->Lookup(p).status == Status::Ok;
				if (s.status != Status::Ok)
					continue;
				++support;
				conditional += s.conditional;
				if (j <= 128)
					for (unsigned k = 0; k < s.count; ++k)
					{
						auto it =
							std::find_if(topology->Probes().begin(), topology->Probes().end(),
										 [&](const P::Probe &q) { return q.id == s.probes[k]; });
						if (it == topology->Probes().end())
							throw std::runtime_error("Bad lookup ID");
						if (Bake::Detail::Length(Bake::Detail::V(p) -
												 Bake::Detail::V(it->positionWorld)) < 1e-5)
							continue;
						RayQuery ray;
						ray.startWorld = p;
						ray.endWorld = it->positionWorld;
						ray.purpose = RayPurpose::IrradianceProbe;
						ray.policy.facing = FacingPolicy::TwoSided;
						RayResult result;
						Check(geometry->Query(ray, read, result));
						blocked += result.state == VisibilityState::Blocked;
						unknownRay += result.state == VisibilityState::Unknown;
						verified += result.state == VisibilityState::Visible;
					}
			}
			double query =
				std::chrono::duration<double>(std::chrono::steady_clock::now() - queryStart)
					.count();
			std::vector<Disk::Chunk> chunks;
			Check(P::Encode(*topology, 1, 2, read, chunks));
			std::shared_ptr<const P::Topology> decoded;
			Check(P::Decode(chunks, input.geometry, read, decoded));
			if (decoded->Key() != topology->Key())
				throw std::runtime_error("Probe roundtrip differs");
			uint64_t bytes = 0;
			for (const auto &c : chunks)
				bytes += c.bytes.size();
			out << ",\"completed\":true,\"topology_key\":\"" << Disk::Hex(topology->Key())
				<< "\",\"probes\":" << r.valid << ",\"relocated\":" << r.relocated
				<< ",\"nodes\":" << r.nodes << ",\"links\":" << r.links
				<< ",\"rejected_links\":" << r.rejectedLinks
				<< ",\"static_supported_volume\":" << r.supportedStaticVolume
				<< ",\"analysis_volume\":" << r.analysisVolume
				<< ",\"unknown_volume\":" << r.unknownVolume
				<< ",\"solid_volume\":" << r.solidVolume
				<< ",\"conditional_probes\":" << r.conditional
				<< ",\"unassigned_regions\":" << r.unassignedRegion
				<< ",\"maximum_relocation\":" << r.maximumDisplacement
				<< ",\"topology_bytes\":" << r.bytes
				<< ",\"peak_build_bytes_estimate\":" << r.peakBuildBytes
				<< ",\"encoded_bytes\":" << bytes << ",\"chunks\":" << chunks.size()
				<< ",\"work\":" << r.work << ",\"relocation_rays\":" << r.rays
				<< ",\"query_points\":1024,\"bsp_free_points\":" << free
				<< ",\"bsp_unknown_points\":" << unknown << ",\"static_support_points\":" << support
				<< ",\"strict_support_points\":" << strict
				<< ",\"conditional_support_points\":" << conditional
				<< ",\"reference_visible_paths\":" << verified
				<< ",\"reference_blocked_paths\":" << blocked
				<< ",\"reference_unknown_paths\":" << unknownRay
				<< ",\"query_diagnostic_seconds\":" << query << ",\"roundtrip\":true}";
		}
		out << "]}";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
