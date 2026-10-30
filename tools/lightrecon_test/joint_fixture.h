// Reuses the owned S10 planar room/doorway fixture geometry for S11.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/bake_math.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/joint.h"
#include <stdexcept>
namespace JointFixture
{
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace I = Rend2LightRecon::Core::Indirect;
inline void Must(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
inline ReadContext Read()
{
	ReadContext r;
	r.maxBytes = 128ull * 1024 * 1024;
	return r;
}
inline Disk::Digest Key(const std::string &s) { return Disk::Hash(s.data(), s.size()); }
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
inline Bounds Box(V a, V b)
{
	Bounds r;
	r.known = true;
	r.minWorld = a.Float();
	r.maxWorld = b.Float();
	return r;
}
class Domain final : public RegionDomain
{
  public:
	Error Locate(Vec3, SpatialLocation &o, double = 1e-5) const override
	{
		o = {};
		o.work = 1;
		o.space = SpaceClass::Free;
		return {};
	}
	Error ClassifyBox(const Bounds &, SpaceClass &o, uint64_t &w) const override
	{
		o = SpaceClass::Free;
		w = 1;
		return {};
	}
};
I::Patch Patch(Id id, Id surface, Id material, V p, V normal, double side)
{
	I::Patch v;
	v.id = id;
	v.material = material;
	v.receiver.sample = id;
	v.receiver.surface = surface;
	v.receiver.material = material;
	v.receiver.world = p.Float();
	v.receiver.geometricNormal = v.receiver.shadingNormal = normal.Float();
	v.receiver.mappedPlanar = v.receiver.compilerPlacementCertified = true;
	V u = Unit(Cross(normal, std::abs(normal.z) > .9 ? V(0, 1, 0) : V(0, 0, 1))), w = Cross(normal, u);
	auto f = std::make_shared<std::vector<Bake::FootprintTriangle>>(2);
	V a = p - u * (side * .5) - w * (side * .5), b = p + u * (side * .5) - w * (side * .5),
	  c = p + u * (side * .5) + w * (side * .5), d = p - u * (side * .5) + w * (side * .5);
	(*f)[0].world[0] = a.Float();
	(*f)[0].world[1] = b.Float();
	(*f)[0].world[2] = c.Float();
	(*f)[1].world[0] = a.Float();
	(*f)[1].world[1] = c.Float();
	(*f)[1].world[2] = d.Float();
	for (auto &t : *f)
		for (auto &n : t.normal)
			n = normal.Float();
	v.receiver.footprint = Lease(f);
	v.areaWorldSquared = side * side;
	return v;
}
struct Scene
{
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::shared_ptr<const StaticVisibility> visibility;
	LightmapMapIdentity map;
	std::vector<BakedSurface> surfaces;
	std::vector<I::Patch> patches;
	void Quad(V a, V b, V c, V d, uint32_t flags = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.material = "owned-s10";
		s.materialFacts.available = true;
		s.materialFacts.cull = SurfaceCull::None;
		s.materialFacts.signals = flags;
		for (auto p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p.Float();
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	Scene(bool network = true, bool wall = false, bool opening = false, bool alpha = false,
		  bool missing = false, SurfaceCull wallCull = SurfaceCull::None)
	{
		if (network)
		{
			Quad(V(-4, -3, 0), V(4, -3, 0), V(4, 3, 0), V(-4, 3, 0));
			Quad(V(-.25, -.25, 1.6), V(.25, -.25, 1.6), V(.25, .25, 1.6), V(-.25, .25, 1.6));
			patches.push_back(Patch(1, 2, 1, V(0, 0, 1.6), V(0, 0, -1), .5));
			for (int y = -2; y <= 2; ++y)
				for (int x = -2; x <= 2; ++x)
					patches.push_back(
						Patch(patches.size() + 1, 1, 2, V(x * .45, y * .5, 0), V(0, 0, 1), .15));
		}
		else
		{
			Quad(V(-1.3, -.3, 2), V(-.7, -.3, 2), V(-.7, .3, 2), V(-1.3, .3, 2));
			Quad(V(.7, -.3, 0), V(1.3, -.3, 0), V(1.3, .3, 0), V(.7, .3, 0));
			patches = {Patch(1, 1, 1, V(-1, 0, 2), V(0, 0, -1), .2),
					   Patch(2, 2, 2, V(1, 0, 0), V(0, 0, 1), .2)};
		}
		if (wall)
		{
			size_t firstWall = surfaces.size();
			if (opening)
			{
				Quad(V(0, -2, -.1), V(0, -.4, -.1), V(0, -.4, 3), V(0, -2, 3));
				Quad(V(0, .4, -.1), V(0, 2, -.1), V(0, 2, 3), V(0, .4, 3));
			}
			else
				Quad(V(0, -2, -.1), V(0, 2, -.1), V(0, 2, 3), V(0, -2, 3),
					 alpha ? uint32_t(MaterialCutout) : 0u);
			for (size_t i = firstWall; i < surfaces.size(); ++i)
				surfaces[i].materialFacts.cull = wallCull;
		}
		if (missing)
		{
			BakedSurface s;
			s.ordinal = uint32_t(surfaces.size());
			s.kind = BakedSurfaceKind::Unsupported;
			s.motion = SurfaceMotion::StaticWorld;
			surfaces.push_back(s);
		}
		GeometryKey k;
		for (const auto &s : surfaces)
		{
			k.U(unsigned(s.kind));
			k.U(s.materialFacts.signals);
			k.U(unsigned(s.materialFacts.cull));
			for (const auto &v : s.vertices)
				k.V(v.position);
		}
		map.path = "maps/s10-owned.bsp";
		map.bytes = 1;
		map.sha256 = k.Finish();
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Must(capture.SetMap(map));
		for (const auto &s : surfaces)
			Must(capture.Add(s));
		Must(capture.Freeze());
		Must(StaticVisibility::Build(capture.Acquire(), {}, Read(), visibility, domain));
	}
	std::shared_ptr<const I::Transfer> Transfer(double range = 10, bool complete = true, bool moving = false)
	{
		I::BuildInput in;
		in.mapKey = map.sha256;
		in.patchInterpretationKey = Key("Owned exact planar receiving pieces v1");
		in.patches = patches;
		in.geometry = visibility;
		in.completePatchSet = complete;
		if (moving)
		{
			DynamicBoundary b;
			b.id = 1;
			b.sweptExtentKnown = true;
			b.bounds = Box(V(-.1, -2, 0), V(.1, 2, 3));
			in.moving.push_back(b);
		}
		I::BuildOptions o;
		o.maximumLinkDistanceWorld = range;
		o.subdivisions = 0;
		o.maxRays = 4000000;
		std::shared_ptr<const I::Transfer> out;
		Must(I::Transfer::Build(in, o, Read(), out));
		return out;
	}
};

} // namespace JointFixture
