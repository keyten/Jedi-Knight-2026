// Original BSP collision metadata capture only; no engine/collision trace dependency.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "offline.h"
#include <cstring>
#include <cmath>
#include <stdexcept>
namespace Rend2LightRecon
{
namespace Core
{
namespace Offline
{
Error CaptureBspSpace(const void *original, size_t bytes, const LightmapMapIdentity &map,
					  const ReadContext &ctx, std::shared_ptr<const BspRegionDomain> &out,
					  std::vector<DynamicBoundary> &dynamic)
{
	out.reset();
	dynamic.clear();
	try
	{
		auto need = [](bool b, const char *m) {
			if (!b)
				throw Failure(Code::InvalidInput, m, "offline BSP");
		};
		need(original && bytes >= 152 && bytes <= ctx.maxBytes, "Invalid original BSP size");
		const auto *p = static_cast<const uint8_t *>(original);
		auto u = [&](size_t offset) {
			need(offset <= bytes && bytes - offset >= 4, "Truncated BSP field");
			return uint32_t(p[offset]) | uint32_t(p[offset + 1]) << 8 |
				   uint32_t(p[offset + 2]) << 16 | uint32_t(p[offset + 3]) << 24;
		};
		auto f = [&](size_t offset) {
			uint32_t b = u(offset);
			float v;
			std::memcpy(&v, &b, 4);
			need(std::isfinite(v), "Non-finite BSP geometry");
			return v;
		};
		auto v = [&](size_t offset) { return Vec3(f(offset), f(offset + 4), f(offset + 8)); };
		auto i = [&](size_t offset) {
			uint32_t b = u(offset);
			int32_t n;
			std::memcpy(&n, &b, 4);
			return n;
		};
		struct Lump
		{
			size_t offset, count;
		};
		auto rows = [&](unsigned n, size_t stride) {
			Lump l{u(8 + n * 8), 0};
			uint32_t length = u(12 + n * 8);
			need(l.offset <= bytes && length <= bytes - l.offset && !(length % stride),
				 "Invalid BSP lump extent");
			l.count = length / stride;
			need(l.count <= 4000000, "BSP record limit");
			return l;
		};
		need(!std::memcmp(p, "RBSP", 4) && u(4) == 1, "Unsupported BSP schema");
		need(map.bytes == bytes && map.sha256 == Disk::Hash(p, bytes),
			 "Original BSP fingerprint mismatch");
		auto shaders = rows(1, 72), planes = rows(2, 16), nodes = rows(3, 36), leaves = rows(4, 48),
			 leafBrushes = rows(6, 4), models = rows(7, 40), brushes = rows(8, 12),
			 sides = rows(9, 12);
		need(models.count > 0, "BSP world model missing");
		uint64_t estimate = planes.count * sizeof(SpatialPlane) +
							nodes.count * sizeof(SpatialNode) + leaves.count * sizeof(SpatialLeaf) +
							leafBrushes.count * 4 + brushes.count * sizeof(SpatialBrush) +
							sides.count * 4 + (models.count - 1) * sizeof(DynamicBoundary);
		if (estimate > ctx.maxBytes)
			throw Failure(Code::MemoryLimit, "BSP spatial snapshot limit");
		BspSpaceInput space;
		space.identity = map;
		space.worldBounds.known = true;
		space.worldBounds.minWorld = v(models.offset);
		space.worldBounds.maxWorld = v(models.offset + 12);
		for (size_t n = 0; n < planes.count; ++n)
		{
			SpatialPlane q;
			q.normal = v(planes.offset + n * 16);
			q.distance = f(planes.offset + n * 16 + 12);
			space.planes.push_back(q);
		}
		for (size_t n = 0; n < nodes.count; ++n)
		{
			SpatialNode q;
			size_t o = nodes.offset + n * 36;
			q.plane = u(o);
			q.children[0] = i(o + 4);
			q.children[1] = i(o + 8);
			space.nodes.push_back(q);
		}
		for (size_t n = 0; n < leaves.count; ++n)
		{
			SpatialLeaf q;
			size_t o = leaves.offset + n * 48;
			q.cluster = i(o);
			q.area = i(o + 4);
			q.firstBrush = u(o + 40);
			q.brushCount = u(o + 44);
			space.leaves.push_back(q);
		}
		for (size_t n = 0; n < leafBrushes.count; ++n)
			space.leafBrushes.push_back(u(leafBrushes.offset + n * 4));
		for (size_t n = 0; n < sides.count; ++n)
			space.sides.push_back(u(sides.offset + n * 12));
		uint32_t first = u(models.offset + 32), count = u(models.offset + 36);
		need(first <= brushes.count && count <= brushes.count - first, "Invalid world brush range");
		for (size_t n = 0; n < brushes.count; ++n)
		{
			size_t o = brushes.offset + n * 12;
			uint32_t shader = u(o + 8);
			need(shader < shaders.count, "Invalid brush material reference");
			SpatialBrush q;
			q.firstSide = u(o);
			q.sideCount = u(o + 4);
			q.world = n >= first && n < first + count;
			q.solid = (u(shaders.offset + shader * 72 + 68) & 1) != 0;
			q.sky = (u(shaders.offset + shader * 72 + 64) & 0x2000) != 0;
			space.brushes.push_back(q);
		}
		std::vector<DynamicBoundary> staged;
		for (size_t n = 1; n < models.count; ++n)
		{
			DynamicBoundary q;
			q.id = n;
			q.model = uint32_t(n);
			q.bounds.known = true;
			q.bounds.minWorld = v(models.offset + n * 40);
			q.bounds.maxWorld = v(models.offset + n * 40 + 12);
			staged.push_back(q);
		}
		auto error = BspRegionDomain::Create(std::move(space), ctx, out);
		if (!error)
			return error;
		dynamic = std::move(staged);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "BSP snapshot allocation failed");
	}
	catch (const std::exception &e)
	{
		return Failure(Code::InvalidInput, e.what());
	}
}
} // namespace Offline
} // namespace Core
} // namespace Rend2LightRecon
