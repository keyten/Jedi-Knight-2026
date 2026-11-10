// Retain every shader-authored surface from an original observation snapshot.
// Uses the real producer and existing .jklr cache, without a numerical fit.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include "lightrecon/producer.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace P = Producer;
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
void Check(Disk::Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 3)
		{
			std::fprintf(stderr,
						 "usage: lightrecon_shader_lights_tool original.jkobs cache-directory\n");
			return 2;
		}
		ReadContext read;
		read.maxBytes = 256ull << 20;
		Offline::Options options;
		options.maxResidentBytes = read.maxBytes;
		options.maxSections = 131072;
		std::shared_ptr<const Offline::Dataset> data;
		Check(Offline::OpenFile(argv[1], options, data));
		Offline::Sources source;
		Offline::RegionSnapshot regions;
		Check(data->Load(source, regions));
		if (!source.geometry || source.metadata.hints == Offline::HintPolicy::BlindBenchmark)
			throw std::runtime_error("Original unstripped shader geometry required");
		P::Request request;
		request.map = source.metadata.map;
		request.fitting.mapKey = request.map.sha256;
		request.shaderGeometry = source.geometry;
		request.metadata.units = source.metadata.units;
		request.metadata.lineage = "shader-surfaces/" + Disk::Hex(request.map.sha256);
		request.metadata.revision = "shader-surface-extraction/1";
		request.decoder =
			"Authored shader metadata and original BSP surface geometry; no observation decoder";
		request.settingsIdentity =
			"Complete shader surface retention only; no numerical reconstruction or SI calibration";
		request.options.maxBytes = read.maxBytes;
		request.metadata.provenance = 1;
		Provenance provenance;
		provenance.id = 1;
		provenance.kind = ProvenanceKind::External;
		provenance.producer = "Original observation snapshot";
		provenance.locator = request.map.path;
		provenance.contentHash = "sha256:" + Disk::Hex(request.map.sha256);
		request.metadata.provenanceRecords = {provenance};
		// Only the geometry+selected MaterialFacts extraction is performed here.
		// Its source snapshot, BSP identity and explicit settings are complete.
		std::unique_ptr<Disk::FileReader> original;
		Check(Disk::FileReader::OpenFile(argv[1], original));
		Disk::Digest snapshot;
		Check(Disk::HashContent(*original, snapshot, 2ull << 30));
		request.fingerprint.complete.fill(true);
		request.fingerprint.units = request.metadata.units;
		Disk::InputContent bsp;
		bsp.kind = Disk::InputKind::Bsp;
		bsp.name = request.map.path;
		bsp.bytes = request.map.bytes;
		bsp.sha256 = request.map.sha256;
		request.fingerprint.inputs.push_back(bsp);
		Disk::InputContent materials;
		materials.kind = Disk::InputKind::Materials;
		materials.name = "original-surface-material-snapshot";
		materials.bytes = original->Size();
		materials.sha256 = snapshot;
		request.fingerprint.inputs.push_back(materials);
		for (auto kind : {Disk::InputKind::Settings, Disk::InputKind::UnitCalibration})
		{
			Disk::InputContent c;
			c.kind = kind;
			c.name = kind == Disk::InputKind::Settings ? request.settingsIdentity
													   : "uncalibrated legacy shader units";
			c.bytes = c.name.size();
			c.sha256 = Disk::Hash(c.name.data(), c.name.size());
			request.fingerprint.inputs.push_back(c);
		}
		P::Result result;
		Check(P::Run(request, read, result));
		Disk::Cache cache(argv[2]);
		Check(P::Store(result.artifact, cache, read));
		P::Artifact replay;
		Check(P::Load(cache, result.artifact.fingerprint, read, replay));
		std::map<unsigned, size_t> types;
		size_t rectangles = 0;
		for (auto &e : replay.asset->emitters)
		{
			ShaderLights::Surface s;
			Check(ShaderLights::Read(*replay.asset, replay.chunks, e, read, s));
			++types[unsigned(s.type)];
			rectangles += e.kind == EmitterKind::Rectangle;
		}
		std::printf("{\"sources\":%zu,\"rectangles\":%zu,\"faces\":%zu,\"patches\":%zu,"
					"\"triangles\":%zu,\"reloaded\":true,\"cache_key\":\"%s\"}\n",
					replay.asset->emitters.size(), rectangles,
					types[unsigned(BakedSurfaceKind::Face)],
					types[unsigned(BakedSurfaceKind::Patch)],
					types[unsigned(BakedSurfaceKind::Triangles)], Disk::Hex(replay.key).c_str());
		return 0;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
