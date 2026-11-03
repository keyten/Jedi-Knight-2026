// Owned analytic geometry and field references. No game data or hidden source answers.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/bake_math.h"
#include "lightrecon/probes.h"
#include <stdexcept>
namespace ProbeFixture
{
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace P = Probes;
inline void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
inline Bounds Box(Vec3 a, Vec3 b)
{
	Bounds x;
	x.known = true;
	x.minWorld = a;
	x.maxWorld = b;
	return x;
}
inline bool In(Bounds b, Vec3 p, double epsilon = 0)
{
	return p.x >= b.minWorld.x - epsilon && p.x <= b.maxWorld.x + epsilon &&
		   p.y >= b.minWorld.y - epsilon && p.y <= b.maxWorld.y + epsilon &&
		   p.z >= b.minWorld.z - epsilon && p.z <= b.maxWorld.z + epsilon;
}
inline bool Overlap(Bounds a, Bounds b)
{
	return a.minWorld.x <= b.maxWorld.x && a.maxWorld.x >= b.minWorld.x &&
		   a.minWorld.y <= b.maxWorld.y && a.maxWorld.y >= b.minWorld.y &&
		   a.minWorld.z <= b.maxWorld.z && a.maxWorld.z >= b.minWorld.z;
}
class Domain final : public RegionDomain
{
  public:
	Bounds world = Box(Vec3(-8, -4, 0), Vec3(8, 4, 8));
	std::vector<Bounds> solids;
	bool unknown = false, exterior = false;
	Error Locate(Vec3 p, SpatialLocation &l, double t = 1e-5) const override
	{
		l = {};
		l.work = 1;
		if (!In(world, p))
		{
			l.space = SpaceClass::Outside;
			return {};
		}
		if (unknown)
			return {};
		for (const auto &s : solids)
			if (In(s, p, t))
			{
				l.space = SpaceClass::Solid;
				return {};
			}
		l.space = exterior ? SpaceClass::Exterior : SpaceClass::Free;
		l.leaf = p.x < 0 ? 1 : 2;
		l.cluster = l.leaf;
		l.area = int32_t(p.z / 4);
		return {};
	}
	Error ClassifyBox(const Bounds &b, SpaceClass &s, uint64_t &w) const override
	{
		w = 1;
		s = SpaceClass::Unknown;
		if (unknown)
			return {};
		if (!In(world, b.minWorld) || !In(world, b.maxWorld))
			return {};
		for (const auto &solid : solids)
		{
			++w;
			if (In(solid, b.minWorld) && In(solid, b.maxWorld))
			{
				s = SpaceClass::Solid;
				return {};
			}
			if (Overlap(b, solid))
				return {};
		}
		s = exterior ? SpaceClass::Exterior : SpaceClass::Free;
		return {};
	}
};
struct Scene
{
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::vector<BakedSurface> surfaces;
	P::Options options;
	Scene()
	{
		options.bounds = domain->world;
		options.coarseSpacingWorld = 4;
		options.minimumSpacingWorld = 1;
		options.clearanceWorld = .1f;
		options.maxWork = 32000000;
	}
	void Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t signals = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.materialFacts.available = true;
		s.materialFacts.cull = SurfaceCull::None;
		s.materialFacts.signals = signals;
		for (auto p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p;
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	void Solid(Bounds b)
	{
		domain->solids.push_back(b);
		auto l = b.minWorld, h = b.maxWorld;
		Quad(Vec3(l.x, l.y, l.z), Vec3(l.x, h.y, l.z), Vec3(l.x, h.y, h.z), Vec3(l.x, l.y, h.z));
		Quad(Vec3(h.x, l.y, l.z), Vec3(h.x, h.y, l.z), Vec3(h.x, h.y, h.z), Vec3(h.x, l.y, h.z));
		Quad(Vec3(l.x, l.y, l.z), Vec3(h.x, l.y, l.z), Vec3(h.x, l.y, h.z), Vec3(l.x, l.y, h.z));
		Quad(Vec3(l.x, h.y, l.z), Vec3(h.x, h.y, l.z), Vec3(h.x, h.y, h.z), Vec3(l.x, h.y, h.z));
		Quad(Vec3(l.x, l.y, l.z), Vec3(h.x, l.y, l.z), Vec3(h.x, h.y, l.z), Vec3(l.x, h.y, l.z));
		Quad(Vec3(l.x, l.y, h.z), Vec3(h.x, l.y, h.z), Vec3(h.x, h.y, h.z), Vec3(l.x, h.y, h.z));
	}
	P::Input Input(bool regions = false) const
	{
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		LightmapMapIdentity map;
		map.path = "maps/probes-owned.bsp";
		map.bytes = 42;
		map.sha256[0] = 71;
		Check(capture.SetMap(map));
		for (auto s : surfaces)
			Check(capture.Add(s));
		Check(capture.Freeze());
		P::Input i;
		i.map = map.sha256;
		i.domain = domain;
		i.domainKey[0] = 19;
		VisibilityBuildOptions v;
		ReadContext read;
		read.maxBytes = 128ull << 20;
		Check(StaticVisibility::Build(capture.Acquire(), v, read, i.visibility, domain));
		i.geometry = i.visibility->CertificationKey();
		if (regions)
		{
			RegionBuildInput r;
			r.identity = map;
			r.domain = domain;
			r.geometry = i.visibility;
			RegionOptions o;
			o.bounds = options.bounds;
			o.coarseCellWorld = 4;
			o.minimumCellWorld = 1;
			o.inspectSky = false;
			Check(LightingRegionGraph::Build(r, o, read, i.regions));
		}
		return i;
	}
	std::shared_ptr<const P::Topology> Build(P::Input i) const
	{
		std::shared_ptr<const P::Topology> t;
		Check(P::Build(i, options, ReadContext(), t));
		return t;
	}
};
inline ReconstructedLightingAsset Asset(const P::Topology &t, bool radiance = false)
{
	ReconstructedLightingAsset a;
	a.lineage = "owned-probe-reference";
	a.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "probe-reference/1";
	a.provenanceRecords.push_back(p);
	Calibration c;
	c.id = 1;
	c.provenance = 1;
	c.kind = CalibrationKind::UncalibratedRelative;
	a.calibrations.push_back(c);
	TransportField f;
	f.source.id = 90;
	f.source.state = HypothesisState::Resolved;
	f.source.provenance = 1;
	f.calibration = 1;
	f.dataset = 91;
	f.unit = PhotometricUnit::Relative;
	f.quantity = radiance ? FieldQuantity::Radiance : FieldQuantity::Irradiance;
	f.component = radiance ? TransportComponent::UnattributedIncidentRadiance
						   : TransportComponent::IndirectIrradiance;
	f.membership.version = 2;
	f.membership.colour = ContributionColour::LinearRec709D65;
	f.membership.generation = Disk::Hex(t.Key());
	f.membership.geometry = Disk::Hex(t.GeometryKey());
	f.membership.profile = "owned-analytic/no-historical-bake";
	f.membership.model = "owned-analytic-field";
	f.membership.domain = "generated-probes";
	a.transport.push_back(f);
	Dataset d;
	d.id = 91;
	d.purpose = DatasetPurpose::Transport;
	d.encoding = "org.openjk.angular-reference.v1";
	d.layout = DatasetLayout::Sparse;
	d.locator = "prepared://owned-reference";
	d.elementCount = t.Probes().size();
	a.datasets.push_back(d);
	return a;
}
inline P::Values Values(const P::Topology &t, int field = 0, bool radiance = false)
{
	P::Values v;
	v.field = 90;
	v.topology = t.Key();
	for (auto &p : t.Probes())
	{
		P::Value x;
		x.probe = p.id;
		x.evaluatedPositionWorld = p.positionWorld;
		x.angular.present = true;
		x.angular.quantity =
			radiance ? Angular::Quantity::IncomingRadiance : Angular::Quantity::DiffuseIrradiance;
		x.angular.degree = 0;
		double r = 1, g = 2, b = 3;
		if (field == 1)
			r = g = b = p.positionWorld.x < 0 ? 0 : 10;
		if (field == 2)
		{
			r = 2 + .1 * p.positionWorld.x;
			g = 2 + .1 * p.positionWorld.y;
			b = 2 + .1 * p.positionWorld.z;
		}
		if (field == 3)
		{
			double dz = 7 - p.positionWorld.z, d = std::pow(p.positionWorld.x + 2, 2) +
												   std::pow(p.positionWorld.y, 2) + dz * dz;
			r = g = b = d > 0 ? 100 * std::max(0., dz) / std::pow(d, 1.5) : 0;
		}
		x.angular.sh.push_back(
			Angular::V{{r * std::sqrt(4 * 3.141592653589793), g * std::sqrt(4 * 3.141592653589793),
						b * std::sqrt(4 * 3.141592653589793)}});
		v.records.push_back(x);
	}
	return v;
}
inline std::shared_ptr<const P::Prepared> Prepared(const std::shared_ptr<const P::Topology> &t,
												   const P::Values &v,
												   const ReconstructedLightingAsset &a)
{
	std::shared_ptr<const P::Prepared> p;
	Check(P::Prepare(t, v, a, ReadContext(), p));
	return p;
}
} // namespace ProbeFixture
