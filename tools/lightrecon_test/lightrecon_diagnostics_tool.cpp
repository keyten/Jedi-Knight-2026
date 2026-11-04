// Explicit offline diagnostic import/export. No cache, GL or solver.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/diagnostics.h"
#include <fstream>
#include <iostream>
using namespace Rend2LightRecon;
namespace D = Core::Diagnostics;
int main(int argc, char **argv)
{
	try
	{
		if (argc != 4)
			throw std::runtime_error(
				"diagnostics_tool input.jkdbg expected-map-sha256 output-stem");
		Disk::Digest map{};
		std::string hex = argv[2];
		if (hex.size() != 64)
			throw std::runtime_error("Expected 64 hex digits");
		for (size_t i = 0; i < 32; ++i)
		{
			unsigned v = 0;
			for (size_t j = 0; j < 2; ++j)
			{
				char c = hex[2 * i + j];
				unsigned n =
					c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
				if (n > 15)
					throw std::runtime_error("Invalid map hash");
				v = v * 16 + n;
			}
			map[i] = uint8_t(v);
		}
		std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
		if (!file || file.tellg() < 0 || file.tellg() > (16 << 20))
			throw std::runtime_error("Missing/oversize diagnostic file");
		std::string bytes(size_t(file.tellg()), '\0');
		file.seekg(0);
		if (!file.read(&bytes[0], bytes.size()))
			throw std::runtime_error("Short diagnostic read");
		D::Scene scene;
		auto check = [](Core::Error e) {
			if (!e)
				throw std::runtime_error(e.message);
		};
		check(D::Decode(bytes, map, scene));
		std::string stem = argv[3];
		std::ofstream json(stem + ".json"), csv(stem + ".csv"), obj(stem + ".obj");
		check(D::Json(scene, json));
		check(D::Csv(scene, csv));
		check(D::Obj(scene, obj));
		D::Heatmap range;
		range.maximum = 32;
		range.signedValues = true;
		std::string image;
		if (D::HeatmapPpm(scene.records, D::Layer::Residual, 32, range, image))
		{
			std::ofstream ppm(stem + "-interval-residual.ppm", std::ios::binary);
			ppm.write(image.data(), image.size());
			if (!ppm)
				throw std::runtime_error("Heatmap write");
		}
		std::cout << "records=" << scene.records.size() << " omitted=" << scene.omitted
				  << " rays=" << scene.rays << " map=" << Disk::Hex(scene.map) << "\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << "\n";
		return 1;
	}
}
