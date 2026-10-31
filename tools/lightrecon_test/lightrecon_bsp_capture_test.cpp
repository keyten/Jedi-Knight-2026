// Original RBSP moving-model bounds: planar is valid, inverted is corrupt.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
static void Need(bool value)
{
	if (!value)
		throw std::runtime_error("BSP capture assertion");
}
static void U(std::vector<uint8_t> &b, size_t p, uint32_t v)
{
	for (unsigned k = 0; k < 4; ++k)
		b[p + k] = uint8_t(v >> (k * 8));
}
static void F(std::vector<uint8_t> &b, size_t p, float f)
{
	uint32_t v;
	std::memcpy(&v, &f, 4);
	U(b, p, v);
}
int main()
{
	try
	{
		// One splitting plane, two ordinary non-solid leaves and two model records.
		std::vector<uint8_t> b(152 + 16 + 36 + 96 + 80);
		std::memcpy(b.data(), "RBSP", 4);
		U(b, 4, 1);
		size_t plane = 152, node = 168, leaf = 204, model = 300;
		for (auto l : {std::make_pair(2u, std::make_pair(plane, 16u)),
					   std::make_pair(3u, std::make_pair(node, 36u)),
					   std::make_pair(4u, std::make_pair(leaf, 96u)),
					   std::make_pair(7u, std::make_pair(model, 80u))})
		{
			U(b, 8 + l.first * 8, uint32_t(l.second.first));
			U(b, 12 + l.first * 8, l.second.second);
		}
		F(b, plane + 8, 1);
		U(b, node + 4, uint32_t(-1));
		U(b, node + 8, uint32_t(-2));
		U(b, leaf, 0);
		U(b, leaf + 48, 0);
		for (unsigned a = 0; a < 3; ++a)
		{
			F(b, model + a * 4, -10);
			F(b, model + 12 + a * 4, 10);
		}
		F(b, model + 40, -2);
		F(b, model + 44, -2);
		F(b, model + 48, -4);
		F(b, model + 52, 2);
		F(b, model + 56, 2);
		F(b, model + 60, -4);
		LightmapMapIdentity map;
		map.path = "maps/owned-planar-model.bsp";
		map.bytes = b.size();
		map.sha256 = Disk::Hash(b.data(), b.size());
		ReadContext read;
		read.maxBytes = 1 << 20;
		std::shared_ptr<const BspRegionDomain> domain;
		std::vector<DynamicBoundary> moving;
		Need(bool(Offline::CaptureBspSpace(b.data(), b.size(), map, read, domain, moving)));
		Need(moving.size() == 1 && moving[0].id == 1 && moving[0].model == 1);
		Need(moving[0].bounds.minWorld.z < -4 && moving[0].bounds.maxWorld.z > -4);
		Need(moving[0].bounds.minWorld.x == -2 && moving[0].bounds.maxWorld.x == 2);
		// Geometry interpretation is content-keyed. An inverted extent is never
		// fixed.
		F(b, model + 60, -5);
		map.sha256 = Disk::Hash(b.data(), b.size());
		Need(!Offline::CaptureBspSpace(b.data(), b.size(), map, read, domain, moving));
		Need(!domain && moving.empty());
		std::cout << "planar moving model retained conservatively; inverted model "
					 "rejected\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
