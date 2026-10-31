// Shared source-UV footprint geometry for trusted offline adapters.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/bake_math.h"
#include "lightrecon/surfaces.h"
#include <algorithm>
#include <stdexcept>
namespace SourceFootprint
{
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
inline void Require(bool value, const char *reason)
{
	if (!value)
		throw std::runtime_error(reason);
}
struct UVVertex
{
	double u = 0, v = 0;
	V p;
};
inline std::vector<UVVertex> Clip(std::vector<UVVertex> poly, unsigned axis, double edge,
								  bool lower)
{
	std::vector<UVVertex> out;
	if (poly.empty())
		return out;
	auto coord = [&](const UVVertex &v) { return axis ? v.v : v.u; };
	for (size_t i = 0; i < poly.size(); ++i)
	{
		auto a = poly[i], b = poly[(i + 1) % poly.size()];
		double x = coord(a) - edge, y = coord(b) - edge;
		bool inside = lower ? x >= 0 : x <= 0, next = lower ? y >= 0 : y <= 0;
		if (inside)
			out.push_back(a);
		if (inside != next)
		{
			double t = x / (x - y);
			UVVertex v;
			v.u = a.u + t * (b.u - a.u);
			v.v = a.v + t * (b.v - a.v);
			v.p = a.p + (b.p - a.p) * t;
			out.push_back(v);
		}
	}
	return out;
}
inline Chunk<Bake::FootprintTriangle> Footprint(const BakedSurface &surface, unsigned slot,
												unsigned x, unsigned y, Vec3 normal, double &area)
{
	Require(surface.kind == BakedSurfaceKind::Face && slot < 4,
			"Scoped planar source UV footprint required");
	auto triangles = std::make_shared<std::vector<Bake::FootprintTriangle>>();
	area = 0;
	for (size_t i = 0; i + 2 < surface.indices.size(); i += 3)
	{
		std::vector<UVVertex> p;
		for (unsigned k = 0; k < 3; ++k)
		{
			const auto &v = surface.vertices.at(surface.indices[i + k]);
			UVVertex q;
			q.u = v.lightmap[slot].u;
			q.v = v.lightmap[slot].v;
			q.p = V(v.position);
			p.push_back(q);
		}
		for (unsigned axis = 0; axis < 2; ++axis)
		{
			double v = axis ? y : x;
			p = Clip(p, axis, v / 128., true);
			p = Clip(p, axis, (v + 1) / 128., false);
		}
		for (size_t k = 1; k + 1 < p.size(); ++k)
		{
			Bake::FootprintTriangle t;
			t.world[0] = p[0].p.Float();
			t.world[1] = p[k].p.Float();
			t.world[2] = p[k + 1].p.Float();
			for (auto &n : t.normal)
				n = normal;
			// The public footprint stores float vertices. Its reported area must
			// describe those same vertices, including collapse at large coordinates.
			double a =
				.5 * Length(Cross(V(t.world[1]) - V(t.world[0]), V(t.world[2]) - V(t.world[0])));
			if (a > 1e-14)
			{
				area += a;
				triangles->push_back(t);
			}
		}
	}
	Require(area > 0 && triangles->size() <= 64, "Missing/oversized original clipped footprint");
	return VectorChunk<Bake::FootprintTriangle>(triangles, 0, triangles->size());
}

} // namespace SourceFootprint
