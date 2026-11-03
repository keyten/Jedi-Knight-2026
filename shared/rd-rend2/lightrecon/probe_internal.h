// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_math.h"
#include "geometry_key.h"
#include "probes.h"
#include <algorithm>
namespace Rend2LightRecon
{
namespace Core
{
namespace Probes
{
using namespace Bake::Detail;
inline double Axis(Vec3 v, unsigned k)
{
	return k == 0 ? v.x : k == 1 ? v.y : v.z;
}
inline void Set(Vec3 &v, unsigned k, double x)
{
	(k == 0 ? v.x : k == 1 ? v.y : v.z) = float(x);
}
inline Bounds Box(Vec3 a, Vec3 b)
{
	Bounds r;
	r.known = true;
	r.minWorld = a;
	r.maxWorld = b;
	return r;
}
inline bool Inside(Bounds b, Vec3 p)
{
	for (unsigned k = 0; k < 3; ++k)
		if (Axis(p, k) < Axis(b.minWorld, k) || Axis(p, k) > Axis(b.maxWorld, k))
			return false;
	return b.known;
}
inline bool Overlap(Bounds a, Bounds b)
{
	for (unsigned k = 0; k < 3; ++k)
		if (Axis(a.maxWorld, k) < Axis(b.minWorld, k) || Axis(b.maxWorld, k) < Axis(a.minWorld, k))
			return false;
	return a.known && b.known;
}
inline Vec3 Centre(Bounds b)
{
	return ((V(b.minWorld) + V(b.maxWorld)) * .5).Float();
}
inline double Width(Bounds b)
{
	return std::max({Axis(b.maxWorld, 0) - Axis(b.minWorld, 0),
					 Axis(b.maxWorld, 1) - Axis(b.minWorld, 1),
					 Axis(b.maxWorld, 2) - Axis(b.minWorld, 2)});
}
inline double Volume(Bounds b)
{
	if (!b.known)
		return 0;
	double v = 1;
	for (unsigned k = 0; k < 3; ++k)
		v *= Axis(b.maxWorld, k) - Axis(b.minWorld, k);
	return v;
}
inline Bounds Union(Bounds a, Bounds b)
{
	for (unsigned k = 0; k < 3; ++k)
	{
		Set(a.minWorld, k, std::min(Axis(a.minWorld, k), Axis(b.minWorld, k)));
		Set(a.maxWorld, k, std::max(Axis(a.maxWorld, k), Axis(b.maxWorld, k)));
	}
	return a;
}
inline bool ValidBox(Bounds b)
{
	return b.known && Finite(b.minWorld) && Finite(b.maxWorld) && Volume(b) > 0 &&
		   Axis(b.minWorld, 0) < Axis(b.maxWorld, 0) && Axis(b.minWorld, 1) < Axis(b.maxWorld, 1) &&
		   Axis(b.minWorld, 2) < Axis(b.maxWorld, 2);
}
inline Id Identity(const Disk::Digest &m, Bounds b, unsigned level, Id salt = 0)
{
	Core::GeometryKey k;
	k.U(kVersion);
	k.Digest(m);
	k.B(b);
	k.U(level);
	k.U(salt);
	auto d = k.Finish();
	Id id = 0;
	for (unsigned i = 0; i < 8; ++i)
		id |= Id(d[i]) << (8 * i);
	return id ? id : 1;
}
struct Node
{
	Bounds b;
	uint32_t a = 0, bchild = 0, index = 0;
	bool leaf = false;
};
struct Topology::Data
{
	Disk::Digest map{}, geometry{}, domain{}, policy{}, key{};
	Options options;
	Report report;
	std::vector<Probe> probes;
	std::vector<Cell> cells;
	std::vector<Link> links;
	std::vector<Node> index;
};
void Index(Topology::Data &);
Disk::Digest Content(const Topology::Data &);
Error ValidateData(const Topology::Data &, const ReadContext &);
} // namespace Probes
} // namespace Core
} // namespace Rend2LightRecon
