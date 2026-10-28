// Bounded text protocol for independent fixed-geometry fitting controllers.
// Inputs contain geometry/parameters only, NEVER target RGB or ground truth.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_forward.h"
#include "lightrecon/bake_trace.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Rend2LightRecon::Core::Bake;
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
void Vec(std::istream &in, Vec3 &v) { in >> v.x >> v.y >> v.z; }
void CompleteFrame(Frame &f)
{
	double x = f.forward.x, y = f.forward.y, z = f.forward.z;
	double length = std::sqrt(x * x + y * y + z * z);
	if (std::abs(length - 1) > 1e-4)
		throw std::runtime_error("Unit source axis required");
	Vec3 reference = std::abs(z) < .9 ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
	f.right =
		Vec3(reference.y * float(z) - reference.z * float(y), reference.z *float(x) - reference.x * float(z),
			 reference.x *float(y) - reference.y * float(x));
	double r = std::sqrt(f.right.x * f.right.x + f.right.y * f.right.y + f.right.z * f.right.z);
	f.right = Vec3(float(f.right.x / r), float(f.right.y / r), float(f.right.z / r));
	f.up = Vec3(f.forward.y * f.right.z - f.forward.z * f.right.y,
				f.forward.z * f.right.x - f.forward.x * f.right.z,
				f.forward.x * f.right.y - f.forward.y * f.right.x);
}
size_t Count(std::istream &in, size_t max)
{
	size_t n = 0;
	in >> n;
	if (!in || n > max)
		throw std::runtime_error("Protocol count limit");
	return n;
}
int main(int argc, char **argv)
{
	try
	{
		if (argc == 3 && std::string(argv[1]) == "--encode")
		{
			std::ifstream values(argv[2]);
			size_t count = Count(values, 4096);
			auto profile = NetRadiantProfile();
			std::cout << std::setprecision(10) << "{\"rows\":[";
			for (size_t i = 0; i < count; ++i)
			{
				Vec3 raw, encoded;
				uint8_t packed[3];
				Vec(values, raw);
				if (!values)
					throw std::runtime_error("Encoding input");
				auto e = Encode(profile, raw, encoded, packed);
				if (!e)
					throw std::runtime_error(e.message);
				if (i)
					std::cout << ',';
				std::cout << '[' << encoded.x << ',' << encoded.y << ',' << encoded.z << ']';
			}
			std::cout << "]}\n";
			return 0;
		}
		if (argc == 3 && std::string(argv[1]) == "--status")
		{
			auto sources = std::make_shared<std::vector<SourceHypothesis>>(1);
			auto &s = (*sources)[0];
			s.id = 1;
			s.kind = SourceKind::Point;
			s.position.known = true;
			s.position.world.z = 192;
			s.amplitudeKnown = true;
			s.compilerRgbAmplitude.x = s.compilerRgbAmplitude.y = s.compilerRgbAmplitude.z = 300;
			auto receivers = std::make_shared<std::vector<Receiver>>(1);
			auto &r = (*receivers)[0];
			r.sample = 1;
			r.mappedPlanar = r.compilerPlacementCertified = true;
			r.geometricNormal.z = r.shadingNormal.z = 1;
			std::string name = argv[2];
			auto profile = name == "unknown_photometry" ? ProfileHypothesis() : NetRadiantProfile();
			Options o;
			o.allowAssumed = true;
			if (name == "gi_operator_unavailable")
				o.required = {Feature::BounceFill};
			EvaluationContext c;
			Bake::Result result;
			CpuForwardEvaluator evaluator;
			auto e = evaluator.Evaluate(profile, Lease(sources), Lease(receivers), Decode::Profile(), c, o,
										ReadContext(), result);
			bool available = result.predictions.count && result.predictions.data[0].available;
			std::cout << "{\"available\":" << (available ? "true" : "false") << ",\"code\":" << int(e.code)
					  << ",\"status\":"
					  << (result.predictions.count ? int(result.predictions.data[0].status) : -1) << "}\n";
			return available ? 1 : 0;
		}
		if (argc != 2)
			throw std::runtime_error("usage: lightrecon_bake_forward_tool request.txt");
		std::ifstream in(argv[1]);
		std::string magic;
		in >> magic;
		if (magic != "JKBAKE2")
			throw std::runtime_error("Protocol version");
		Recipe recipe;
		int linear, normal, fast, srgb, domain, strict;
		in >> linear >> normal >> recipe.photonsPerParameter >> recipe.minimumDistanceWorld >> recipe.fade >>
			recipe.extraDistanceWorld >> recipe.angleScale >> recipe.tolerance >> fast >>
			recipe.receiverPushWorld >> recipe.areaParameterScale >> recipe.gamma >> recipe.compensate >>
			recipe.exposure >> srgb;
		Vec(in, recipe.explicitSurfaceAmbient);
		recipe.falloff = linear ? Falloff::Linear : Falloff::InverseSquare;
		recipe.normal = normal == 1 ? NormalResponse::None
									: normal == 2 ? NormalResponse::HalfLambert : NormalResponse::Cosine;
		recipe.fast = fast != 0;
		recipe.srgbOutput = srgb != 0;
		in >> domain >> strict;
		auto sources = std::make_shared<std::vector<SourceHypothesis>>(Count(in, 4096));
		for (auto &s : *sources)
		{
			int kind;
			in >> s.id >> kind;
			s.kind = static_cast<SourceKind>(kind);
			s.position.known = s.orientation.known = s.amplitudeKnown = true;
			Vec(in, s.position.world);
			Vec(in, s.orientation.forward);
			CompleteFrame(s.orientation);
			Vec(in, s.compilerRgbAmplitude);
			in >> s.spotTargetDistanceWorld >> s.spotRadiusWorld;
			auto p = std::make_shared<std::vector<Vec3>>(Count(in, 4096));
			for (auto &v : *p)
				Vec(in, v);
			s.polygon = Lease(p);
		}
		auto receivers = std::make_shared<std::vector<Receiver>>(Count(in, 1048576));
		for (auto &r : *receivers)
		{
			in >> r.sample >> r.surface;
			Vec(in, r.world);
			Vec(in, r.geometricNormal);
			r.shadingNormal = r.geometricNormal;
			r.mappedPlanar = r.compilerPlacementCertified = true;
		}
		BspSpaceInput bsp;
		std::string hex;
		in >> hex;
		Disk::Digest hash{};
		if (hex.size() != 64)
			throw std::runtime_error("Hash length");
		for (size_t k = 0; k < 32; ++k)
			hash[k] = uint8_t(std::stoul(hex.substr(2 * k, 2), nullptr, 16));
		bsp.planes.resize(Count(in, 131072));
		for (auto &p : bsp.planes)
		{
			Vec(in, p.normal);
			in >> p.distance;
		}
		bsp.nodes.resize(Count(in, 131072));
		for (auto &n : bsp.nodes)
			in >> n.plane >> n.children[0] >> n.children[1];
		bsp.leaves.resize(Count(in, 131072));
		for (auto &l : bsp.leaves)
			in >> l.cluster;
		auto triangles = std::make_shared<std::vector<TracePrimitive>>(Count(in, 1048576));
		for (auto &t : *triangles)
		{
			int sky;
			in >> t.triangle.id >> sky;
			t.optical = sky ? ObstacleKind::Sky : ObstacleKind::Opaque;
			for (auto &v : t.triangle.verticesWorld)
				Vec(in, v);
		}
		if (!in)
			throw std::runtime_error("Truncated input");
		std::string extra;
		if (in >> extra)
			throw std::runtime_error("Unexpected input");
		ReadContext read;
		read.maxBytes = 128ull * 1024 * 1024;
		EvaluationContext context;
		TraceLimits limits;
		limits.completeOpaqueSkyWorldGeometry =
			true; // controlled fixtures only; controller supplies all caster triangles
		auto start = std::chrono::steady_clock::now();
		auto e = CompilerTrace::Build(bsp, Lease(triangles), hash, limits, read, context.compilerTrace);
		if (!e)
			throw std::runtime_error(e.message);
		context.geometryKey = context.compilerTrace->Key();
		Options options;
		options.allowAssumed = true;
		options.trace = TraceMode::CompilerTree;
		options.requireScopedCompilerReplay = strict != 0;
		options.output = domain == 1 ? Domain::GridAmbientDirected
									 : domain == 2 ? Domain::DeluxeWorldDirection
												   : domain == 3 ? Domain::CompilerLinearRelative255
																 : Domain::EncodedRgb8BeforeQuantization;
		options.unpackedGridOnly = domain == 1;
		options.maxPairs = 16ull * 1024 * 1024;
		options.maxRays = 16ull * 1024 * 1024;
		Bake::Result result;
		CpuForwardEvaluator evaluator;
		auto profile = NetRadiantProfile(recipe);
		e = evaluator.Evaluate(profile, Lease(sources), Lease(receivers), Decode::Profile(), context, options,
							   read, result);
		if (!e)
			throw std::runtime_error(e.message);
		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::cout << std::setprecision(10) << "{\"schema\":\"org.openjk.bake-cpu.v2\",\"seconds\":" << seconds
				  << ",\"profile_id\":\"" << result.profileId << "\",\"profile_key\":\""
				  << Disk::Hex(result.profileKey) << "\",\"source_lock\":\""
				  << Disk::Hex(profile.sourceLockKey) << "\",\"catalogue_key\":\""
				  << Disk::Hex(profile.catalogueKey) << "\""
				  << ",\"trace_bytes\":" << context.compilerTrace->OwnedBytes()
				  << ",\"trace_nodes\":" << context.compilerTrace->NodeCount()
				  << ",\"trace_triangles\":" << context.compilerTrace->TriangleCount() << ",\"trace_key\":\""
				  << Disk::Hex(context.compilerTrace->Key()) << "\",\"pairs\":" << result.work.pairs
				  << ",\"rays\":" << result.work.rays << ",\"tests\":" << result.work.triangleTests
				  << ",\"node_tests\":" << result.work.nodeTests << ",\"rows\":[";
		for (size_t k = 0; k < result.predictions.count; ++k)
		{
			const auto &p = result.predictions.data[k];
			if (k)
				std::cout << ',';
			if (!p.available)
			{
				std::cout << "{\"id\":" << p.observation
						  << ",\"available\":false,\"status\":" << int(p.status)
						  << ",\"fidelity\":" << int(p.fidelity) << ",\"rgb\":null}";
				continue;
			}
			std::cout << "{\"id\":" << p.observation << ",\"available\":" << (p.available ? "true" : "false")
					  << ",\"status\":" << int(p.status) << ",\"fidelity\":" << int(p.fidelity)
					  << ",\"domain\":" << int(p.domain) << ",\"unit\":" << int(p.unit)
					  << ",\"issues\":" << p.issues
					  << ",\"packed_known\":" << (p.packedKnown ? "true" : "false")
					  << ",\"direction_known\":" << (p.directionKnown ? "true" : "false")
					  << ",\"deluxe_known\":" << (p.deluxeKnown ? "true" : "false") << ",\"rgb\":[" << p.rgb.x
					  << ',' << p.rgb.y << ',' << p.rgb.z << "],\"packed\":[" << int(p.packedRgb[0]) << ','
					  << int(p.packedRgb[1]) << ',' << int(p.packedRgb[2]) << "],\"ambient\":["
					  << p.gridAmbient.x << ',' << p.gridAmbient.y << ',' << p.gridAmbient.z
					  << "],\"directed\":[" << p.gridDirected.x << ',' << p.gridDirected.y << ','
					  << p.gridDirected.z << "],\"direction\":[" << p.directionToSource.x << ','
					  << p.directionToSource.y << ',' << p.directionToSource.z << "],\"deluxe\":["
					  << int(p.deluxeRgb[0]) << ',' << int(p.deluxeRgb[1]) << ',' << int(p.deluxeRgb[2])
					  << "]}";
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
