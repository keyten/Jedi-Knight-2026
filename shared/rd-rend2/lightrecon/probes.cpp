// Conservative, generated probe topology. No lighting inference or historical relocation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "probe_internal.h"
#include <functional>
#include <numeric>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace Probes
{
namespace
{
void Need(bool ok, Code c, const char *s)
{
	if (!ok)
		throw Failure(c, s);
}
uint64_t Bytes(const Topology::Data &d)
{
	return sizeof(d) + d.probes.size() * sizeof(Probe) + d.cells.size() * sizeof(Cell) +
		   d.links.size() * sizeof(Link) + d.index.size() * sizeof(Node);
}
bool Usable(const Probe &p)
{
	return p.classification == Classification::Valid ||
		   p.classification == Classification::Relocated;
}
uint32_t Dynamic(const Input &i, Bounds b)
{
	for (const auto &d : i.dynamic)
		if (!d.sweptExtentKnown || !d.bounds.known || Overlap(b, d.bounds))
			return DynamicConditional;
	return 0;
}
struct Builder
{
	const Input &i;
	const Options &o;
	const ReadContext &read;
	Topology::Data &d;
	uint64_t cap;
	void Tick(uint64_t n = 1)
	{
		Need(!Cancelled(read), Code::Cancelled, "Probe preparation cancelled");
		Need(n <= o.maxWork - d.report.work, Code::WorkLimit, "Probe geometry work budget");
		d.report.work += n;
	}
	bool Certify(Bounds b, uint32_t &issues, SpaceClass *classification = nullptr)
	{
		Tick();
		SpaceClass s = SpaceClass::Unknown;
		uint64_t work = 0;
		auto e = i.domain->ClassifyBox(b, s, work);
		if (!e)
			throw e;
		Tick(work);
		if (classification)
			*classification = s;
		if (s != SpaceClass::Free && s != SpaceClass::Exterior)
		{
			issues |= GeometryUnknown;
			return false;
		}
		GeometryBoxResult g;
		e = i.visibility->OverlapBox(b, read, g, o.rayWork);
		if (!e)
			throw e;
		Tick(g.work);
		if (!g.coverageComplete || g.intersects)
		{
			issues |= g.issues & Core::OpticalUnknown ? Probes::OpticalUnknown : GeometryUnknown;
			return false;
		}
		issues |= Dynamic(i, b);
		return true;
	}
	void Limit()
	{
		Need(Bytes(d) + d.cells.size() * sizeof(Node) * 2 <= cap, Code::MemoryLimit,
			 "Probe topology memory budget");
		d.report.peakBuildBytes =
			std::max(d.report.peakBuildBytes, Bytes(d) + d.cells.size() * sizeof(Node) * 2);
	}
	void Leaf(Bounds b, Id parent, unsigned level, bool canSplit)
	{
		Tick();
		if (++d.report.nodes > o.maxNodes)
			throw Failure(Code::WorkLimit, "Probe adaptive node limit");
		uint32_t issues = 0;
		SpaceClass space;
		bool free = Certify(b, issues, &space);
		double target = o.coarseSpacingWorld;
		for (const auto &r : i.refinement)
			if (Overlap(r.bounds, b))
				target = std::min(target, double(r.spacingWorld));
		bool split = Width(b) > target ||
					 (!free && space != SpaceClass::Solid && space != SpaceClass::Outside);
		const Id id = Identity(i.map, b, level);
		if (split && canSplit && Width(b) * .5 >= o.minimumSpacingWorld && level < o.maxDepth)
		{
			Vec3 mid = Centre(b);
			for (unsigned j = 0; j < 8; ++j)
			{
				Bounds c = b;
				for (unsigned k = 0; k < 3; ++k)
					Set((j & (1u << k)) ? c.minWorld : c.maxWorld, k, Axis(mid, k));
				Leaf(c, id, level + 1, true);
			}
			return;
		}
		Cell cell;
		cell.id = id;
		cell.parent = parent;
		cell.bounds = b;
		cell.level = level;
		cell.space = space;
		cell.issues = issues;
		Probe p;
		p.id = Identity(i.map, b, level, 1);
		p.source = p.id;
		p.cell = id;
		p.level = level;
		p.originalWorld = p.positionWorld = Centre(b);
		p.classification =
			space == SpaceClass::Solid ? Classification::Solid : Classification::Unknown;
		if (!free && space != SpaceClass::Solid && space != SpaceClass::Outside)
		{
			SpatialLocation loc;
			auto e = i.domain->Locate(p.originalWorld, loc);
			if (!e)
				throw e;
			Tick(loc.work);
			// Never "escape" a solid/unknown seed through a wall. Only free generated
			// seeds may move along an independently geometric segment.
			if (loc.space == SpaceClass::Free || loc.space == SpaceClass::Exterior)
			{
				for (unsigned j = 0; j < 7 && !free; ++j)
				{
					Vec3 q = p.originalWorld;
					if (j)
					{
						unsigned k = (j - 1) / 2;
						Set(q, k,
							Axis(q, k) + (j % 2 ? 1 : -1) * Width(b) * o.maximumRelocationFraction);
					}
					Vec3 delta(o.clearanceWorld, o.clearanceWorld, o.clearanceWorld);
					Bounds local = Box((V(q) - V(delta)).Float(), (V(q) + V(delta)).Float());
					if (!Inside(b, local.minWorld) || !Inside(b, local.maxWorld))
						continue;
					uint32_t localIssues = 0;
					if (!Certify(local, localIssues))
						continue;
					if (j)
					{
						Need(d.report.rays < o.maxRays, Code::WorkLimit,
							 "Probe relocation ray budget");
						++d.report.rays;
						RayQuery ray;
						ray.startWorld = p.originalWorld;
						ray.endWorld = q;
						ray.purpose = RayPurpose::IrradianceProbe;
						ray.maxWork = o.rayWork;
						ray.policy.facing = FacingPolicy::TwoSided;
						RayResult hit;
						auto error = i.visibility->Query(ray, read, hit);
						if (!error)
							throw error;
						Tick(hit.nodeTests + hit.primitiveTests);
						if (hit.state != VisibilityState::Visible ||
							hit.reliability != RayReliability::Geometric)
							continue;
					}
					p.positionWorld = q;
					p.support = local;
					cell.support = local;
					free = true;
					issues = localIssues | ResolutionLimited;
				}
			}
		}
		if (free)
		{
			if (!p.support.known)
				p.support = cell.support = b;
			p.displacementWorld = Length(V(p.positionWorld) - V(p.originalWorld));
			p.clearanceWorld = std::numeric_limits<double>::infinity();
			for (unsigned k = 0; k < 3; ++k)
				p.clearanceWorld =
					std::min(p.clearanceWorld,
							 std::min(Axis(p.positionWorld, k) - Axis(p.support.minWorld, k),
									  Axis(p.support.maxWorld, k) - Axis(p.positionWorld, k)));
			p.classification =
				p.displacementWorld ? Classification::Relocated : Classification::Valid;
			p.issues = issues;
			cell.issues = issues;
			SpatialLocation loc;
			auto e = i.domain->Locate(p.positionWorld, loc);
			if (!e)
				throw e;
			Tick(loc.work);
			p.leaf = loc.leaf;
			p.area = loc.area;
			if (i.regions)
			{
				RegionAssignment r;
				e = i.regions->Lookup(p.positionWorld, read, r);
				if (!e)
					throw e;
				if (r.state == RegionAssignmentState::Assigned)
					p.region = r.region;
			}
			if (!p.region)
			{
				p.issues |= RegionUnknown;
				++d.report.unassignedRegion;
			}
			Need(d.probes.size() < o.maxProbes, Code::WorkLimit, "Probe count budget");
			cell.probe = uint32_t(d.probes.size());
			d.probes.push_back(p);
			++d.report.valid;
			d.report.relocated += p.displacementWorld > 0;
			d.report.conditional += (issues & DynamicConditional) != 0;
			d.report.maximumDisplacement =
				std::max(d.report.maximumDisplacement, p.displacementWorld);
			d.report.supportedStaticVolume += Volume(cell.support);
			d.report.unknownVolume += Volume(b) - Volume(cell.support);
		}
		else if (space == SpaceClass::Solid)
		{
			++d.report.solid;
			d.report.solidVolume += Volume(b);
		}
		else
		{
			++d.report.unknown;
			d.report.unknownVolume += Volume(b);
			cell.issues |= ResolutionLimited;
		}
		d.cells.push_back(cell);
		Limit();
	}
	void TopologyLinks()
	{
		Tick(uint64_t(d.cells.size()) * 32);
		Index(d);
		for (auto &cell : d.cells)
		{
			if (cell.probe == UINT32_MAX)
				continue;
			struct Candidate
			{
				double distance;
				uint32_t index;
			};
			Candidate closest[kNeighbours];
			unsigned count = 0;
			uint32_t stack[64];
			unsigned top = 0;
			stack[top++] = 0;
			Vec3 centre = d.probes[cell.probe].positionWorld;
			double range = Width(cell.bounds) * 2.1;
			Bounds search =
				Box((V(centre) - V(Vec3(float(range), float(range), float(range)))).Float(),
					(V(centre) + V(Vec3(float(range), float(range), float(range)))).Float());
			while (top)
			{
				Tick();
				auto &node = d.index[stack[--top]];
				if (!Overlap(node.b, search))
					continue;
				if (!node.leaf)
				{
					Need(top + 2 <= 64, Code::WorkLimit, "Probe index stack");
					stack[top++] = node.a;
					stack[top++] = node.bchild;
					continue;
				}
				auto n = d.cells[node.index].probe;
				if (n == UINT32_MAX)
					continue;
				Candidate c{Length(V(d.probes[n].positionWorld) - V(centre)), n};
				unsigned at = 0;
				while (at < count && (closest[at].distance < c.distance ||
									  (closest[at].distance == c.distance &&
									   d.probes[closest[at].index].id < d.probes[n].id)))
					++at;
				if (at >= o.neighbours)
					continue;
				if (count < o.neighbours)
					++count;
				for (unsigned j = count - 1; j > at; --j)
					closest[j] = closest[j - 1];
				closest[at] = c;
			}
			cell.firstLink = uint32_t(d.links.size());
			for (unsigned j = 0; j < count; ++j)
			{
				auto n = closest[j].index;
				Bounds certificate = Union(cell.support, d.probes[n].support);
				uint32_t issues = 0;
				if (n == cell.probe || Certify(certificate, issues))
				{
					Need(d.links.size() < o.maxLinks, Code::WorkLimit,
						 "Probe interpolation link budget");
					Link l;
					l.probe = n;
					l.certificate = certificate;
					l.issues = issues | cell.issues | d.probes[n].issues;
					d.links.push_back(l);
					++cell.linkCount;
				}
				else
					++d.report.rejectedLinks;
			}
			Limit();
		}
		d.report.links = d.links.size();
		d.report.bytes = Bytes(d);
	}
};
} // namespace
Error MomentWeight(const Moment &m, Vec3 dir, double distance, double &weight)
{
	weight = 0;
	if (!m.present)
		return Failure(Code::Unavailable, "No directional depth moments");
	if (!Normal(dir) || !Normal(m.directionToQuery) || !std::isfinite(distance) || distance < 0 ||
		!std::isfinite(m.mean) || m.mean < 0 || !std::isfinite(m.second) ||
		!std::isfinite(m.maximumDistance) || m.maximumDistance <= 0 || m.mean > m.maximumDistance ||
		!m.samples || !std::isfinite(m.minimumCosine) || m.minimumCosine < -1 ||
		m.minimumCosine > 1)
		return Failure(Code::InvalidInput, "Malformed probe distance moments");
	double tolerance = 64 * std::numeric_limits<double>::epsilon() * std::max(1., m.mean * m.mean);
	if (m.second < m.mean * m.mean - tolerance ||
		m.second > m.maximumDistance * m.maximumDistance + tolerance)
		return Failure(Code::InvalidInput, "Invalid second distance moment");
	if (Dot(V(dir), V(m.directionToQuery)) < m.minimumCosine || distance > m.maximumDistance)
		return Failure(Code::Unavailable, "Outside directional moment support");
	double variance = std::max(0., m.second - m.mean * m.mean), delta = distance - m.mean;
	weight = delta <= 0 ? 1 : variance / (variance + delta * delta);
	return {};
}
void Index(Topology::Data &d)
{
	d.index.clear();
	std::vector<uint32_t> order(d.cells.size());
	std::iota(order.begin(), order.end(), 0);
	std::function<uint32_t(size_t, size_t)> build = [&](size_t a, size_t b) {
		uint32_t n = uint32_t(d.index.size());
		d.index.push_back(Node{});
		Bounds bounds = d.cells[order[a]].bounds;
		for (size_t j = a + 1; j < b; ++j)
			bounds = Union(bounds, d.cells[order[j]].bounds);
		d.index[n].b = bounds;
		if (b - a == 1)
		{
			d.index[n].leaf = true;
			d.index[n].index = order[a];
			return n;
		}
		unsigned axis = 0;
		for (unsigned k = 1; k < 3; ++k)
			if (Axis(bounds.maxWorld, k) - Axis(bounds.minWorld, k) >
				Axis(bounds.maxWorld, axis) - Axis(bounds.minWorld, axis))
				axis = k;
		std::sort(order.begin() + a, order.begin() + b, [&](uint32_t x, uint32_t y) {
			double p = Axis(Centre(d.cells[x].bounds), axis),
				   q = Axis(Centre(d.cells[y].bounds), axis);
			return p == q ? d.cells[x].id < d.cells[y].id : p < q;
		});
		size_t mid = (a + b) / 2;
		auto l = build(a, mid), r = build(mid, b);
		d.index[n].a = l;
		d.index[n].bchild = r;
		return n;
	};
	if (!order.empty())
		build(0, order.size());
}
Disk::Digest Content(const Topology::Data &d)
{
	Core::GeometryKey k;
	k.U(kVersion);
	k.Digest(d.map);
	k.Digest(d.geometry);
	k.Digest(d.domain);
	k.Digest(d.policy);
	for (const auto &p : d.probes)
	{
		k.U(p.id);
		k.U(p.source);
		k.U(p.cell);
		k.U(p.region);
		k.V(p.originalWorld);
		k.V(p.positionWorld);
		k.B(p.support);
		k.U(unsigned(p.classification));
		k.U(p.issues);
		k.U(p.level);
		k.U(uint32_t(p.leaf));
		k.U(uint32_t(p.area));
		k.F(p.displacementWorld);
		k.F(p.clearanceWorld);
		k.U(p.moment.present);
		k.V(p.moment.directionToQuery);
		k.F(p.moment.minimumCosine);
		k.F(p.moment.mean);
		k.F(p.moment.second);
		k.F(p.moment.maximumDistance);
		k.U(p.moment.samples);
	}
	for (const auto &c : d.cells)
	{
		k.U(c.id);
		k.U(c.parent);
		k.B(c.bounds);
		k.B(c.support);
		k.U(c.level);
		k.U(c.issues);
		k.U(unsigned(c.space));
		k.U(c.probe);
		k.U(c.firstLink);
		k.U(c.linkCount);
	}
	for (const auto &l : d.links)
	{
		k.U(l.probe);
		k.B(l.certificate);
		k.U(l.issues);
	}
	return k.Finish();
}
Error ValidateData(const Topology::Data &d, const ReadContext &read)
{
	if (Cancelled(read))
		return Failure(Code::Cancelled, "Probe dataset validation cancelled");
	if (d.map == Disk::Digest{} || d.geometry == Disk::Digest{} || !ValidBox(d.options.bounds) ||
		d.cells.empty() || d.probes.size() > 200000 || d.cells.size() > 1000000 ||
		d.links.size() > 3200000 ||
		Bytes(d) + (d.index.empty() ? d.cells.size() * (2 * sizeof(Node) + sizeof(uint32_t)) : 0) >
			(read.maxBytes ? read.maxBytes : 128ull << 20))
		return Failure(Code::InvalidInput, "Invalid/budgeted probe dataset");
	std::set<Id> ids;
	for (const auto &p : d.probes)
	{
		if (!p.id || !ids.insert(p.id).second || !Usable(p) || !Finite(p.positionWorld) ||
			!Finite(p.originalWorld) || !ValidBox(p.support) ||
			!Inside(p.support, p.positionWorld) || !std::isfinite(p.displacementWorld) ||
			p.displacementWorld < 0)
			return Failure(Code::InvalidInput, "Invalid generated probe");
		if (p.moment.present)
		{
			double weight = 0;
			auto e = MomentWeight(p.moment, p.moment.directionToQuery, 0, weight);
			if (!e)
				return e;
		}
	}
	ids.clear();
	for (const auto &c : d.cells)
	{
		if (!c.id || !ids.insert(c.id).second || !ValidBox(c.bounds) || c.linkCount > kNeighbours ||
			c.firstLink > d.links.size() || c.linkCount > d.links.size() - c.firstLink ||
			unsigned(c.space) > unsigned(SpaceClass::Exterior))
			return Failure(Code::InvalidInput, "Invalid probe topology");
		if (c.probe != UINT32_MAX &&
			(c.probe >= d.probes.size() || d.probes[c.probe].cell != c.id || !ValidBox(c.support) ||
			 !Inside(c.bounds, c.support.minWorld) || !Inside(c.bounds, c.support.maxWorld)))
			return Failure(Code::InvalidInput, "Invalid probe support cell");
		if (c.probe == UINT32_MAX && c.linkCount)
			return Failure(Code::InvalidInput, "Invalid cell has interpolation support");
		std::set<uint32_t> neighbours;
		for (unsigned j = 0; j < c.linkCount; ++j)
		{
			const auto &l = d.links[c.firstLink + j];
			if (l.probe >= d.probes.size() || !neighbours.insert(l.probe).second ||
				!Inside(l.certificate, c.support.minWorld) ||
				!Inside(l.certificate, c.support.maxWorld) ||
				!Inside(l.certificate, d.probes[l.probe].positionWorld))
				return Failure(Code::InvalidInput, "Corrupt interpolation support certificate");
		}
	}
	for (const auto &l : d.links)
		if (l.probe >= d.probes.size() || !ValidBox(l.certificate))
			return Failure(Code::InvalidInput, "Invalid prepared probe link");
	if (Content(d) != d.key)
		return Failure(Code::InvalidState, "Probe content/generation digest mismatch");
	return {};
}
Error Build(const Input &i, const Options &o, const ReadContext &read,
			std::shared_ptr<const Topology> &out)
{
	out.reset();
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Probe preparation cancelled");
		Need(i.map != Disk::Digest{} && i.geometry != Disk::Digest{} &&
				 i.domainKey != Disk::Digest{} && i.domain && i.visibility && ValidBox(o.bounds) &&
				 o.minimumSpacingWorld > 0 && o.coarseSpacingWorld >= o.minimumSpacingWorld &&
				 o.clearanceWorld > 0 && o.maximumRelocationFraction >= 0 &&
				 o.maximumRelocationFraction <= .45 && o.neighbours &&
				 o.neighbours <= kNeighbours && o.maxDepth <= 24 && o.maxNodes &&
				 o.maxNodes <= 1000000 && o.maxProbes <= 200000 && o.maxLinks <= 3200000,
			 Code::InvalidInput, "Probe build identity/options");
		Need(i.geometry == i.visibility->CertificationKey(), Code::InvalidState,
			 "Stale probe visual geometry");
		Need(i.map == i.visibility->GeometryIdentity().sha256, Code::InvalidState,
			 "Wrong-map probe geometry");
		if (i.domain->SourceIdentity())
			Need(i.domain->SourceIdentity()->sha256 == i.map, Code::InvalidState,
				 "Wrong-map probe domain");
		if (i.regions)
			Need(i.regions->VisibilityKey() == i.geometry, Code::InvalidState,
				 "Stale probe region geometry");
		Need(i.refinement.size() <= 4096 && i.dynamic.size() <= 4096, Code::MemoryLimit,
			 "Probe refinement/dynamic records bound");
		std::set<Id> hints;
		for (const auto &r : i.refinement)
			Need(r.evidence && ValidBox(r.bounds) && r.spacingWorld >= o.minimumSpacingWorld &&
					 std::isfinite(r.spacingWorld) && hints.insert(r.evidence).second,
				 Code::InvalidInput, "Probe refinement policy");
		auto d = std::make_shared<Topology::Data>();
		d->map = i.map;
		d->geometry = i.geometry;
		d->domain = i.domainKey;
		d->options = o;
		d->report.analysisVolume = Volume(o.bounds);
		Core::GeometryKey policy;
		policy.U(kVersion);
		policy.Digest(i.regions ? i.regions->CertificationKey() : Disk::Digest{});
		policy.B(o.bounds);
		policy.F(o.coarseSpacingWorld);
		policy.F(o.minimumSpacingWorld);
		policy.F(o.clearanceWorld);
		policy.F(o.maximumRelocationFraction);
		policy.U(o.neighbours);
		policy.U(o.maxDepth);
		policy.U(o.maxNodes);
		policy.U(o.maxProbes);
		policy.U(o.maxLinks);
		policy.U(o.maxWork);
		policy.U(o.maxBytes);
		policy.U(o.maxRays);
		policy.U(o.rayWork);
		std::vector<Refinement> refinement = i.refinement;
		std::sort(refinement.begin(), refinement.end(),
				  [](const Refinement &a, const Refinement &b) { return a.evidence < b.evidence; });
		for (auto &r : refinement)
		{
			policy.U(r.evidence);
			policy.B(r.bounds);
			policy.F(r.spacingWorld);
			policy.U(unsigned(r.reason));
		}
		auto dynamic = i.dynamic;
		std::sort(dynamic.begin(), dynamic.end(),
				  [](const DynamicBoundary &a, const DynamicBoundary &b) { return a.id < b.id; });
		hints.clear();
		for (const auto &b : dynamic)
		{
			Need(b.id && hints.insert(b.id).second, Code::InvalidInput,
				 "Duplicate dynamic geometry identity");
			policy.U(b.id);
			policy.B(b.bounds);
			policy.U(b.sweptExtentKnown);
			d->report.unboundedMotion |= !b.sweptExtentKnown || !b.bounds.known;
		}
		d->policy = policy.Finish();
		ReadContext effective = read;
		effective.maxBytes = read.maxBytes ? std::min(read.maxBytes, o.maxBytes) : o.maxBytes;
		Builder b{i, o, effective, *d, effective.maxBytes};
		b.Leaf(o.bounds, 0, 0, true);
		b.TopologyLinks();
		d->key = Content(*d);
		auto e = ValidateData(*d, effective);
		if (!e)
			return e;
		out = std::make_shared<Topology>(d);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Probe build allocation");
	}
}
Topology::Topology(std::shared_ptr<const Data> d) : data_(std::move(d))
{
}
const std::vector<Probe> &Topology::Probes() const
{
	return data_->probes;
}
const std::vector<Cell> &Topology::Cells() const
{
	return data_->cells;
}
const std::vector<Link> &Topology::Links() const
{
	return data_->links;
}
const Report &Topology::Diagnostics() const
{
	return data_->report;
}
const Disk::Digest &Topology::Key() const
{
	return data_->key;
}
const Disk::Digest &Topology::GeometryKey() const
{
	return data_->geometry;
}
const Disk::Digest &Topology::MapKey() const
{
	return data_->map;
}
Support Topology::Lookup(Vec3 p, const QueryPolicy &policy) const
{
	Support s;
	if (!Finite(p) || !std::isfinite(policy.minimumWeightFraction) ||
		policy.minimumWeightFraction < 0 || policy.minimumWeightFraction > 1)
	{
		s.status = Status::InvalidArgument;
		return s;
	}
	if (data_->index.empty())
		return s;
	uint32_t stack[64];
	unsigned top = 0;
	stack[top++] = 0;
	const Cell *cell = nullptr;
	while (top)
	{
		auto &n = data_->index[stack[--top]];
		if (!Inside(n.b, p))
			continue;
		if (!n.leaf)
		{
			if (top + 2 > 64)
			{
				s.status = Status::Corrupt;
				return s;
			}
			stack[top++] = n.a;
			stack[top++] = n.bchild;
		}
		else
		{
			const auto &c = data_->cells[n.index];
			if (c.probe != UINT32_MAX && Inside(c.support, p) && (!cell || c.id < cell->id))
				cell = &c;
		}
	}
	if (!cell)
	{
		s.issues = GeometryUnknown;
		return s;
	}
	s.cell = cell->id;
	s.region = data_->probes[cell->probe].region;
	s.issues = cell->issues;
	Id exact = 0;
	for (unsigned j = 0; j < cell->linkCount; ++j)
	{
		const auto &l = data_->links[cell->firstLink + j];
		const auto &probe = data_->probes[l.probe];
		double distance = Length(V(p) - V(probe.positionWorld));
		double scale = Width(cell->bounds), w = 1 / (1 + distance * distance / (scale * scale));
		s.rawWeight += w;
		if (!Inside(l.certificate, p))
			continue;
		if (l.issues & DynamicConditional)
		{
			s.conditional = true;
			if (!policy.allowConditionalStatic)
				continue;
		}
		if (policy.useMoments && distance > 0)
		{
			double moment = 0;
			auto e = MomentWeight(probe.moment,
								  ((V(p) - V(probe.positionWorld)) * (1 / distance)).Float(),
								  distance, moment);
			if (!e)
				continue;
			w *= moment;
			s.issues |= MomentApproximation;
		}
		if (w <= 0)
			continue;
		if (distance <= scale * 1e-7)
			exact = probe.id;
		s.probes[s.count] = probe.id;
		s.weights[s.count] = w;
		++s.count;
		s.supportedWeight += w;
		s.issues |= l.issues;
	}
	if (!s.count || s.supportedWeight <= 0 ||
		s.supportedWeight < s.rawWeight * policy.minimumWeightFraction)
	{
		s.count = 0;
		s.status = Status::Unavailable;
		return s;
	}
	// Exact probe samples reproduce their own value. No ghost weight floor.
	if (exact)
	{
		s.probes[0] = exact;
		s.weights[0] = 1;
		s.count = 1;
		s.status = Status::Ok;
		return s;
	}
	for (unsigned j = 0; j < s.count; ++j)
		s.weights[j] /= s.supportedWeight;
	s.status = Status::Ok;
	return s;
}
} // namespace Probes
} // namespace Core
} // namespace Rend2LightRecon
