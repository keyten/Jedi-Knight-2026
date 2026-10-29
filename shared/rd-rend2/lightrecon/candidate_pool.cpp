// Conservative evidence unions and bounded offline work queues; no light fitting.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "candidate_pool.h"
#include "geometry_key.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace Rend2LightRecon
{
namespace Core
{
namespace Candidates
{
namespace
{
bool Zero(const Disk::Digest &d) { return d == Disk::Digest{}; }
bool Stop(const ReadContext &c) { return c.cancellation && c.cancellation->IsCancelled(); }
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
Id ID(const Disk::Digest &d)
{
	Id x = 0;
	for (unsigned i = 0; i < 8; ++i)
		x |= Id(d[i]) << (i * 8);
	return x ? x : 1;
}
bool Finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
double Dot(Vec3 a, Vec3 b) { return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z; }
double Distance(Vec3 a, Vec3 b)
{
	Vec3 d(a.x - b.x, a.y - b.y, a.z - b.z);
	return std::sqrt(Dot(d, d));
}
bool Aligned(Vec3 a, Vec3 b, double cosine)
{
	double n = Dot(a, a) * Dot(b, b);
	return n > 0 && Dot(a, b) >= cosine * std::sqrt(n);
}
bool HomeLess(const Home &a, const Home &b)
{
	return std::tie(a.kind, a.region, a.cell[0], a.cell[1], a.cell[2]) <
		   std::tie(b.kind, b.region, b.cell[0], b.cell[1], b.cell[2]);
}
bool SameHome(const Home &a, const Home &b) { return !HomeLess(a, b) && !HomeLess(b, a); }
template <class T> void Unique(std::vector<T> &v)
{
	std::sort(v.begin(), v.end());
	v.erase(std::unique(v.begin(), v.end()), v.end());
}
bool RefLess(const Ref &a, const Ref &b) { return a.scoped < b.scoped; }
void UniqueRefs(std::vector<Ref> &r)
{
	std::sort(r.begin(), r.end(), RefLess);
	r.erase(std::unique(r.begin(), r.end(), [](const Ref &a, const Ref &b) { return a.scoped == b.scoped; }),
			r.end());
}
void HashSupport(GeometryKey &k, const Support &s)
{
	k.U(s.observation);
	k.U(s.surface);
	k.U(s.region);
	k.U(uint32_t(s.visibility));
}
bool SupportLess(const Support &a, const Support &b)
{
	return std::tie(a.observation, a.surface, a.region, a.visibility) <
		   std::tie(b.observation, b.surface, b.region, b.visibility);
}
void UniqueSupport(std::vector<Support> &s)
{
	std::sort(s.begin(), s.end(), SupportLess);
	s.erase(std::unique(
				s.begin(), s.end(),
				[](const Support &a, const Support &b) { return !SupportLess(a, b) && !SupportLess(b, a); }),
			s.end());
}
Disk::Digest ShapeKey(const EmitterEvidence &e)
{
	GeometryKey k;
	k.U(kVersion);
	k.U(e.positionKnown);
	k.V(e.position);
	k.U(e.orientationKnown);
	k.V(e.right);
	k.V(e.up);
	k.V(e.outward);
	k.V(e.toSource);
	k.F(e.halfWidth);
	k.F(e.halfHeight);
	k.F(e.radius);
	k.F(e.areaWorld);
	k.F(e.sourceRadiusHint);
	k.U(e.coneKnown);
	k.F(e.coneDegrees);
	k.U(e.twoSided);
	k.U(e.style);
	// Different photometry is an alternative, never averaged or silently overwritten.
	k.U(e.intensityKnown);
	k.F(e.intensity);
	k.U(uint32_t(e.unit));
	k.U(e.colourKnown);
	k.V(e.colour);
	Text(k, e.material);
	k.U(e.issues);
	k.U(e.staticEligible);
	return k.Finish();
}
Disk::Digest ProviderKey(const Provider &p)
{
	GeometryKey k;
	Text(k, p.name);
	k.Digest(p.generation);
	k.Digest(p.map);
	return k.Finish();
}
Disk::Digest RecordKey(const EmitterEvidence &e)
{
	GeometryKey k;
	k.U(e.id);
	k.Digest(ShapeKey(e));
	k.U(uint32_t(e.origin));
	k.U(e.ordinal);
	k.U(e.shapes);
	k.U(e.materialSignals);
	for (const auto *s : {&e.source, &e.material, &e.emissionTexture, &e.name, &e.mode, &e.raw})
		Text(k, *s);
	auto s = e.surfaces, t = e.entities;
	Unique(s);
	Unique(t);
	k.U(s.size());
	for (auto x : s)
		k.U(x);
	k.U(t.size());
	for (auto x : t)
		k.U(x);
	k.U(e.chromaticityKnown);
	k.V(e.chromaticity);
	k.F(e.range);
	k.U(e.maskSampled);
	k.F(e.geometricCoverage);
	return k.Finish();
}
Ref MakeRef(const Provider &p, size_t provider, size_t index, Id original, bool evidence)
{
	Ref r;
	r.provider = provider;
	r.index = index;
	r.original = original;
	GeometryKey k;
	k.Digest(ProviderKey(p));
	k.U(evidence ? 1 : 2);
	k.U(original);
	r.scoped = ID(k.Finish());
	return r;
}
template <class T, class O> Chunk<T> Borrow(std::shared_ptr<const O> p, const std::vector<T> &v)
{
	Chunk<T> c;
	c.owner = p;
	c.data = v.empty() ? nullptr : v.data();
	c.count = v.size();
	return c;
}
bool Authored(const EmitterEvidence &e)
{
	return e.origin == EvidenceOrigin::AuthoredArea || e.origin == EvidenceOrigin::BspEntity ||
		   e.origin == EvidenceOrigin::SunShader || (e.issues & AuthoredPrior);
}
Annotation Basic(const EmitterCandidate &c, const EmitterEvidence &e)
{
	Annotation a;
	a.candidate = c.id;
	a.protectedAuthored = Authored(e);
	a.supported = c.staticEligible && e.staticEligible;
	a.tier = a.protectedAuthored ? Tier::Authored : e.positionKnown ? Tier::Geometry : Tier::Unlocalized;
	if (!a.supported)
		a.tier = Tier::Unsupported;
	return a;
}
std::vector<Class> Classes(uint32_t shapes, Nature nature)
{
	if (nature == Nature::TransportEvidence)
		return {Class::Transport};
	std::vector<Class> v;
	const uint32_t bits[] = {CandidatePoint, CandidateSpot, CandidateRectangle | CandidateSurface,
							 CandidateTube,	 CandidateSun,	CandidateSky};
	const Class cls[] = {Class::Point, Class::Spot, Class::Area, Class::Tube, Class::Sun, Class::Sky};
	for (size_t i = 0; i < 6; ++i)
		if (shapes & bits[i])
			v.push_back(cls[i]);
	if (v.empty())
		v.push_back(Class::Unknown);
	return v;
}
bool GeometryKnown(Class c, const EmitterEvidence &e)
{
	if (c == Class::Sky)
		return true;
	if (c == Class::Sun)
		return e.orientationKnown && Dot(e.toSource, e.toSource) > .99;
	if (!e.positionKnown)
		return false;
	if (c == Class::Point || c == Class::Transport)
		return true;
	if (c == Class::Spot)
		return e.orientationKnown && Dot(e.outward, e.outward) > .99 && e.coneKnown;
	if (c == Class::Tube)
		return e.orientationKnown && e.halfWidth > 0 && e.radius > 0;
	if (c == Class::Area)
		return e.orientationKnown && e.halfWidth > 0 && e.halfHeight > 0;
	return false;
}
// Saturating accounting is also used for safety preflights, never unchecked n*sizeof(T).
bool Add(uint64_t &n, uint64_t a, uint64_t limit)
{
	if (a > limit || n > limit - a)
		return false;
	n += a;
	return true;
}
bool Product(uint64_t &n, uint64_t count, uint64_t each, uint64_t limit)
{
	return (!each || count <= limit / each) && Add(n, count * each, limit);
}
Disk::Digest AdapterGeneration(const Chunk<EmitterEvidence> &e, const Chunk<EmitterCandidate> &c)
{
	std::vector<Disk::Digest> keys;
	keys.reserve(e.count + c.count);
	for (size_t i = 0; i < e.count; ++i)
		keys.push_back(RecordKey(e.data[i]));
	for (size_t i = 0; i < c.count; ++i)
	{
		GeometryKey k;
		k.U(c.data[i].id);
		k.U(c.data[i].shapes);
		keys.push_back(k.Finish());
	}
	std::sort(keys.begin(), keys.end());
	GeometryKey k;
	for (auto &d : keys)
		k.Digest(d);
	return k.Finish();
}
struct Item
{
	Ref ref;
	std::vector<Ref> evidence;
	Disk::Digest strong{}, exact{};
};
struct DSU
{
	std::vector<size_t> p;
	explicit DSU(size_t n) : p(n)
	{
		for (size_t i = 0; i < n; ++i)
			p[i] = i;
	}
	size_t Root(size_t i)
	{
		while (p[i] != i)
		{
			p[i] = p[p[i]];
			i = p[i];
		}
		return i;
	}
	void Join(size_t a, size_t b)
	{
		a = Root(a);
		b = Root(b);
		if (a > b)
			std::swap(a, b);
		p[b] = a;
	}
};
Home Spatial(Vec3 p, double size)
{
	Home h;
	h.kind = Home::Kind::SpatialCell;
	double v[] = {p.x, p.y, p.z};
	for (unsigned i = 0; i < 3; ++i)
		h.cell[i] = int64_t(std::floor(v[i] / size));
	return h;
}
bool ValidEvidence(const EmitterEvidence &e)
{
	if (!e.id || !Finite(e.position) || !Finite(e.right) || !Finite(e.up) || !Finite(e.outward) ||
		!Finite(e.toSource) || !Finite(e.colour) || !Finite(e.chromaticity))
		return false;
	const float scalars[] = {e.halfWidth,	e.halfHeight, e.radius, e.areaWorld,		e.sourceRadiusHint,
							 e.coneDegrees, e.intensity,  e.range,	e.geometricCoverage};
	for (float f : scalars)
		if (!std::isfinite(f) || f < 0)
			return false;
	return !(e.colourKnown && (e.colour.x < 0 || e.colour.y < 0 || e.colour.z < 0));
}
} // namespace

const EmitterEvidence &Evidence(const Catalog &c, const Ref &r)
{
	return c.providers.at(r.provider).evidence.data[r.index];
}
const Annotation &InputAnnotation(const Catalog &c, const Ref &r)
{
	return c.providers.at(r.provider).annotations.data[r.index];
}

Error FromEvidence(const std::string &name, Disk::Digest geometry, Disk::Digest regions,
				   std::shared_ptr<const EmitterEvidenceSet> src, Provider &out)
{
	out = {};
	if (!src)
		return Failure(Code::InvalidInput, "missing evidence lease");
	Provider p;
	p.name = name;
	p.map = src->Map().sha256;
	p.geometryKey = geometry;
	p.regionKey = regions;
	p.evidence = Borrow(src, src->Evidence());
	p.candidates = Borrow(src, src->Candidates());
	auto a = std::make_shared<std::vector<Annotation>>();
	for (const auto &c : src->Candidates())
	{
		if (c.primary >= src->Evidence().size())
			return Failure(Code::InvalidInput, "invalid evidence primary");
		a->push_back(Basic(c, src->Evidence()[c.primary]));
	}
	p.annotations = VectorChunk<Annotation>(a, 0, a->size());
	p.generation = AdapterGeneration(p.evidence, p.candidates);
	out = std::move(p);
	return {};
}
Error FromHidden(const std::string &name, Disk::Digest map, Disk::Digest geometry, Disk::Digest regions,
				 std::shared_ptr<const Hidden::Result> src, Provider &out)
{
	out = {};
	if (!src || src->candidates.size() != src->hypotheses.size())
		return Failure(Code::InvalidInput, "missing/inconsistent Hidden result");
	Provider p;
	p.name = name;
	p.map = map;
	p.geometryKey = geometry;
	p.regionKey = regions;
	p.generation = src->key;
	p.extraOwner = src;
	p.evidence = Borrow(src, src->evidence);
	p.candidates = Borrow(src, src->candidates);
	auto a = std::make_shared<std::vector<Annotation>>();
	for (size_t i = 0; i < src->candidates.size(); ++i)
	{
		const auto &c = src->candidates[i];
		const auto &h = src->hypotheses[i];
		if (c.primary >= src->evidence.size() || h.candidate != c.id)
			return Failure(Code::InvalidInput, "Hidden identity mismatch");
		Annotation v = Basic(c, src->evidence[c.primary]);
		v.profile = h.profile;
		v.nature = h.kind == Hidden::Kind::TransportEvidence
					   ? Nature::TransportEvidence
					   : h.kind == Hidden::Kind::AmbiguousSourceOrTransport
							 ? Nature::AmbiguousSourceOrTransport
							 : Nature::PhysicalCandidate;
		v.tier = h.visible ? Tier::GeometryAndVisibility : Tier::Unlocalized;
		if (h.region)
			v.regions.push_back(h.region);
		for (const auto &s : h.observations)
		{
			Support z;
			z.observation = s.observation;
			z.surface = s.surface;
			z.visibility = s.relation == Hidden::Relation::Visible
							   ? VisibilityState::Visible
							   : s.relation == Hidden::Relation::Blocked ? VisibilityState::Blocked
																		 : VisibilityState::Unknown;
			v.support.push_back(z);
		}
		a->push_back(std::move(v));
	}
	p.annotations = VectorChunk<Annotation>(a, 0, a->size());
	out = std::move(p);
	return {};
}
Error FromSolar(const std::string &name, Disk::Digest map, Disk::Digest geometry,
				std::shared_ptr<const Solar::Result> src, std::vector<Provider> &out)
{
	out.clear();
	if (!src)
		return Failure(Code::InvalidInput, "missing Solar lease");
	std::vector<Provider> all;
	for (const auto &h : src->hypotheses)
	{
		if (!Validate(h.asset))
			return Failure(Code::InvalidInput, "invalid canonical Solar asset");
		Provider p;
		p.name = name;
		p.map = map;
		p.geometryKey = geometry;
		p.extraOwner = src;
		GeometryKey key;
		key.Digest(map);
		Text(key, h.fitted.profile.id);
		key.Digest(h.fitted.profile.contentKey);
		key.V(h.fitted.toSource);
		key.V(h.fitted.sunRgb);
		key.V(h.fitted.skyRgb);
		key.U(uint32_t(h.fitted.model));
		p.generation = key.Finish();
		p.canonical = std::shared_ptr<const ReconstructedLightingAsset>(src, &h.asset);
		auto ev = std::make_shared<std::vector<EmitterEvidence>>();
		auto ca = std::make_shared<std::vector<EmitterCandidate>>();
		auto an = std::make_shared<std::vector<Annotation>>();
		auto append = [&](EmitterEvidence e, Id canonical) {
			EmitterCandidate c;
			c.id = e.id;
			c.shapes = e.shapes;
			c.staticEligible = true;
			c.primary = ev->size();
			c.evidence.push_back(c.primary);
			c.sourceIds.push_back(c.id);
			auto a = Basic(c, e);
			a.canonicalSource = canonical;
			a.profile = h.fitted.profile.id;
			a.scenario = p.generation;
			a.nature = Nature::Environment;
			a.tier = Tier::GeometryAndVisibility;
			for (const auto &r : h.receivers)
				if (r.exclusion == Solar::Exclusion::None)
				{
					Support s;
					s.observation = r.sample;
					a.support.push_back(s);
				}
			ev->push_back(e);
			ca->push_back(c);
			an->push_back(a);
		};
		for (const auto &e : h.asset.emitters)
		{
			if (e.kind != EmitterKind::Directional)
				return Failure(Code::InvalidInput, "Solar contains non-directional source");
			EmitterEvidence v;
			v.id = e.source.id;
			v.origin = EvidenceOrigin::SurfaceObservation;
			v.shapes = CandidateSun;
			v.positionKnown = e.position.known;
			v.position = e.position.world;
			v.orientationKnown = e.frame.known;
			v.outward = e.frame.forward;
			v.toSource = Vec3(-e.frame.forward.x, -e.frame.forward.y, -e.frame.forward.z);
			v.staticEligible = true;
			v.source = "Solar canonical emitter (photometry retained in asset lease)";
			append(v, e.source.id);
			an->back().regions = e.source.regions;
		}
		for (const auto &e : h.asset.environments)
		{
			EmitterEvidence v;
			v.id = e.source.id;
			v.origin = EvidenceOrigin::SkyShader;
			v.shapes = CandidateSky;
			v.staticEligible = true;
			v.source = "Solar canonical environment (sun coverage retained in asset lease)";
			append(v, e.source.id);
			an->back().regions = e.source.regions;
		}
		p.evidence = VectorChunk<EmitterEvidence>(ev, 0, ev->size());
		p.candidates = VectorChunk<EmitterCandidate>(ca, 0, ca->size());
		p.annotations = VectorChunk<Annotation>(an, 0, an->size());
		all.push_back(std::move(p));
	}
	out = std::move(all);
	return {};
}

Error Consolidate(const Request &r, const ReadContext &ctx, Catalog &out)
{
	try
	{
		out = {};
		const auto &l = r.limits;
		uint64_t cap = ctx.maxBytes ? std::min(ctx.maxBytes, l.maxBytes) : l.maxBytes, bytes = 0, text = 0,
				 n = 0, evidence = 0, support = 0;
		ReadContext queryContext = ctx;
		queryContext.maxBytes = cap;
		if (Stop(ctx))
			return Failure(Code::Cancelled, "candidate consolidation cancelled");
		if (Zero(r.map) || Zero(r.geometryKey) || r.providers.size() > l.maxProviders ||
			!std::isfinite(l.localCellWorld) || l.localCellWorld <= 0 ||
			!std::isfinite(l.possibleMatchWorld) || l.possibleMatchWorld <= 0 ||
			l.possibleMatchWorld > l.localCellWorld || !std::isfinite(l.colourCosine) || l.colourCosine < 0 ||
			l.colourCosine > 1 || !std::isfinite(l.directionCosine) || l.directionCosine < 0 ||
			l.directionCosine > 1)
			return Failure(Code::InvalidInput, "invalid candidate scope/options");
		if (r.regions &&
			(r.regions->Identity().sha256 != r.map || r.regions->CertificationKey() != r.regionKey ||
			 r.regions->VisibilityKey() != r.geometryKey))
			return Failure(Code::InvalidInput, "stale region certification");
		Catalog c;
		c.map = r.map;
		c.geometryKey = r.geometryKey;
		c.regionKey = r.regionKey;
		for (const auto &p : r.providers)
			if (p.name.size() > 4096 || !Add(text, p.name.size(), l.maxTextBytes))
				return Failure(Code::MemoryLimit, "provider name/text limit");
		c.providers = r.providers;
		std::sort(c.providers.begin(), c.providers.end(),
				  [](const Provider &a, const Provider &b) { return ProviderKey(a) < ProviderKey(b); });
		for (size_t p = 0; p < c.providers.size(); ++p)
		{
			const auto &v = c.providers[p];
			if (v.name.empty() || Zero(v.generation) || v.map != r.map || v.geometryKey != r.geometryKey ||
				(!Zero(v.regionKey) && v.regionKey != r.regionKey) ||
				v.annotations.count != v.candidates.count ||
				(p && ProviderKey(c.providers[p - 1]) == ProviderKey(v)))
				return Failure(Code::InvalidInput, "invalid/duplicate provider generation");
			if ((v.evidence.count && (!v.evidence.owner || !v.evidence.data)) ||
				(v.candidates.count && (!v.candidates.owner || !v.candidates.data)) ||
				(v.annotations.count && (!v.annotations.owner || !v.annotations.data)))
				return Failure(Code::InvalidInput, "missing immutable candidate lease");
			if (!Add(n, v.candidates.count, l.maxCandidates) ||
				!Add(evidence, v.evidence.count, l.maxEvidence))
				return Failure(Code::MemoryLimit, "candidate/evidence count limit");
			std::set<Id> ids;
			for (size_t j = 0; j < v.evidence.count; ++j)
			{
				const auto &e = v.evidence.data[j];
				if (!ValidEvidence(e) || !ids.insert(e.id).second)
					return Failure(Code::InvalidInput, "invalid/duplicate evidence ID or geometry");
				for (const auto *s : {&e.source, &e.material, &e.emissionTexture, &e.name, &e.mode, &e.raw})
					if (!Add(text, s->size(), l.maxTextBytes))
						return Failure(Code::MemoryLimit, "candidate text limit");
				if (!Product(bytes, e.surfaces.size() + e.entities.size(), sizeof(uint32_t), cap))
					return Failure(Code::MemoryLimit, "candidate anchors limit");
			}
			ids.clear();
			for (size_t j = 0; j < v.candidates.count; ++j)
			{
				const auto &a = v.annotations.data[j];
				const auto &s = v.candidates.data[j];
				if (!s.id || !ids.insert(s.id).second || s.primary >= v.evidence.count ||
					a.candidate != s.id || !a.geometryWork || a.tier > Tier::Unsupported ||
					a.nature > Nature::Environment || s.evidence.empty() ||
					std::find(s.evidence.begin(), s.evidence.end(), s.primary) == s.evidence.end())
					return Failure(Code::InvalidInput, "invalid candidate/annotation reference");
				if (!Zero(a.fragmentSet) && Zero(a.sourceIdentity))
					return Failure(Code::InvalidInput, "fragment union lacks explicit same-source identity");
				for (size_t x : s.evidence)
					if (x >= v.evidence.count)
						return Failure(Code::InvalidInput, "invalid evidence reference");
				for (const auto &z : a.support)
					if (!z.observation || z.visibility > VisibilityState::Unknown)
						return Failure(Code::InvalidInput, "invalid support reference/state");
				if (!Add(support, a.support.size(), l.maxSupport) ||
					!Add(text, a.profile.size(), l.maxTextBytes) ||
					!Product(bytes, s.evidence.size() + s.sourceIds.size() + a.regions.size(), 384, cap))
					return Failure(Code::MemoryLimit, "candidate reference/support limit");
				const auto &e = v.evidence.data[s.primary];
				for (double value : {double(e.position.x), double(e.position.y), double(e.position.z)})
					if (std::abs(value / l.possibleMatchWorld) >
						double(std::numeric_limits<int64_t>::max() / 4))
						return Failure(Code::InvalidInput, "candidate spatial coordinate overflow");
			}
		}
		// Conservative transient+output upper accounting, including small-map links only.
		uint64_t linkBound = n && l.maxNeighbours > l.maxLinks / n
								 ? l.maxLinks
								 : std::min<uint64_t>(l.maxLinks, n * l.maxNeighbours);
		if (!Add(bytes, 4096, cap) || !Product(bytes, n, 8192, cap) || !Product(bytes, evidence, 512, cap) ||
			!Product(bytes, support, 256, cap) || !Product(bytes, linkBound, 128, cap) ||
			!Add(bytes, text * 2, cap))
			return Failure(Code::MemoryLimit, "candidate consolidation memory preflight");
		c.report.accountedBytes = bytes;
		c.report.borrowedBytes =
			text + evidence * sizeof(EmitterEvidence) + n * (sizeof(EmitterCandidate) + sizeof(Annotation));
		c.report.inputs = n;
		c.report.evidence = evidence;
		std::vector<Item> items;
		items.reserve(size_t(n));
		std::set<Id> scoped;
		for (size_t p = 0; p < c.providers.size(); ++p)
		{
			const auto &v = c.providers[p];
			for (size_t i = 0; i < v.candidates.count; ++i)
			{
				if (Stop(ctx))
					return Failure(Code::Cancelled, "candidate consolidation cancelled");
				Item t;
				t.ref = MakeRef(v, p, i, v.candidates.data[i].id, false);
				if (!scoped.insert(t.ref.scoped).second)
					return Failure(Code::InvalidInput, "candidate scoped-ID collision");
				const auto &a = v.annotations.data[i];
				const auto &cand = v.candidates.data[i];
				const auto &e = v.evidence.data[cand.primary];
				for (size_t x : cand.evidence)
					t.evidence.push_back(MakeRef(v, p, x, v.evidence.data[x].id, true));
				UniqueRefs(t.evidence);
				t.strong = a.sourceIdentity;
				// An identical primary source identity AND exact geometry/metadata are required.
				// Nearness, same colour/material or a shared room cannot trigger this union.
				GeometryKey k;
				k.Digest(ShapeKey(e));
				k.U(e.id);
				k.U(uint32_t(e.origin));
				Text(k, e.source);
				auto surfaces = e.surfaces;
				auto entities = e.entities;
				Unique(surfaces);
				Unique(entities);
				for (auto x : surfaces)
					k.U(x);
				k.U(0);
				for (auto x : entities)
					k.U(x);
				if (Authored(e))
				{
					auto aliases = cand.sourceIds;
					Unique(aliases);
					for (Id x : aliases)
						k.U(x);
				}
				t.exact = k.Finish();
				items.push_back(std::move(t));
			}
		}
		std::sort(items.begin(), items.end(),
				  [](const Item &a, const Item &b) { return RefLess(a.ref, b.ref); });
		DSU d(items.size());
		std::map<Disk::Digest, size_t> strong, exact;
		for (size_t i = 0; i < items.size(); ++i)
		{
			if (!Zero(items[i].strong))
			{
				auto z = strong.emplace(items[i].strong, i);
				if (!z.second)
					d.Join(i, z.first->second);
			}
			// Explicitly contradictory identities prohibit even an exact automatic union.
			GeometryKey k;
			k.Digest(items[i].exact);
			k.Digest(items[i].strong);
			auto z = exact.emplace(k.Finish(), i);
			if (!z.second)
				d.Join(i, z.first->second);
		}
		std::map<size_t, std::vector<size_t>> groups;
		for (size_t i = 0; i < items.size(); ++i)
			groups[d.Root(i)].push_back(i);
		GeometryKey root;
		root.U(kVersion);
		root.Digest(r.map);
		root.Digest(r.geometryKey);
		root.Digest(r.regionKey);
		root.F(l.localCellWorld);
		root.F(l.possibleMatchWorld);
		root.F(l.colourCosine);
		root.F(l.directionCosine);
		root.U(l.maxPairTests);
		root.U(l.maxLinks);
		root.U(l.maxNeighbours);
		root.U(l.maxRayWork);
		root.U(l.maxRelationWork);
		for (const auto &v : c.providers)
		{
			root.Digest(ProviderKey(v));
			std::vector<Disk::Digest> records;
			for (size_t j = 0; j < v.evidence.count; ++j)
				records.push_back(RecordKey(v.evidence.data[j]));
			std::sort(records.begin(), records.end());
			for (const auto &key : records)
				root.Digest(key);
		}
		// Include annotations/geometry as well as declared provider revisions: stale IDs cannot reuse a
		// catalog key.
		for (const auto &t : items)
		{
			root.U(t.ref.scoped);
			root.Digest(t.exact);
			const auto &a = InputAnnotation(c, t.ref);
			const auto &ca = c.providers[t.ref.provider].candidates.data[t.ref.index];
			root.U(ca.shapes);
			root.U(ca.issues);
			root.U(ca.staticEligible);
			auto aliases = ca.sourceIds;
			Unique(aliases);
			for (Id x : aliases)
				root.U(x);
			root.Digest(a.sourceIdentity);
			root.Digest(a.fragmentSet);
			root.Digest(a.scenario);
			Text(root, a.profile);
			root.U(uint32_t(a.nature));
			root.U(uint32_t(a.tier));
			root.U(a.geometryWork);
			root.U(a.protectedAuthored);
			root.U(a.supported);
			root.U(a.canonicalSource);
			auto s = a.support;
			UniqueSupport(s);
			for (const auto &x : s)
				HashSupport(root, x);
			auto rs = a.regions;
			Unique(rs);
			for (Id x : rs)
				root.U(x);
			for (const auto &e : t.evidence)
				root.U(e.scoped);
		}
		c.key = root.Finish();
		Provenance map;
		GeometryKey mk;
		mk.Digest(r.map);
		mk.U(1);
		map.id = ID(mk.Finish());
		map.kind = ProvenanceKind::Geometry;
		map.contentHash = "sha256:" + Disk::Hex(r.map);
		map.producer = "candidate-pool/1";
		map.method = "immutable original map";
		c.provenance.push_back(map);
		std::map<Id, Ref> evidenceRefs;
		for (const auto &t : items)
			for (const auto &e : t.evidence)
				evidenceRefs.emplace(e.scoped, e);
		for (const auto &pair : evidenceRefs)
		{
			const auto &e = Evidence(c, pair.second);
			Provenance p;
			p.id = pair.first;
			p.kind = Authored(e) ? ProvenanceKind::Authored : ProvenanceKind::External;
			p.locator = c.providers[pair.second.provider].name + ":" + std::to_string(e.id);
			p.producer = "candidate-pool/1";
			p.method = "immutable evidence lease; original units and raw provenance retained";
			p.parents.push_back(map.id);
			c.provenance.push_back(std::move(p));
		}
		for (const auto &g : groups)
		{
			Bundle b;
			GeometryKey bk;
			bk.U(kVersion);
			bk.Digest(r.map);
			bk.Digest(r.geometryKey);
			bk.Digest(r.regionKey);
			bk.F(l.localCellWorld);
			for (size_t i : g.second)
			{
				b.inputs.push_back(items[i].ref);
				bk.U(items[i].ref.scoped);
				b.evidence.insert(b.evidence.end(), items[i].evidence.begin(), items[i].evidence.end());
				b.sourceIdentityAttested |= !Zero(items[i].strong);
			}
			b.id = ID(bk.Finish());
			b.multiplicityUnknown = !b.sourceIdentityAttested;
			UniqueRefs(b.evidence);
			std::map<Disk::Digest, Model> models;
			for (size_t i : g.second)
			{
				const auto &t = items[i];
				const auto &a = InputAnnotation(c, t.ref);
				const auto &v = c.providers[t.ref.provider];
				const auto &cand = v.candidates.data[t.ref.index];
				Ref primary =
					MakeRef(v, t.ref.provider, cand.primary, v.evidence.data[cand.primary].id, true);
				const auto &e = Evidence(c, primary);
				for (Class cls : Classes(cand.shapes, a.nature))
				{
					GeometryKey key;
					key.U(uint32_t(cls));
					key.U(uint32_t(a.nature));
					key.Digest(a.scenario);
					Text(key, a.profile);
					if (!Zero(a.fragmentSet))
						key.Digest(a.fragmentSet);
					else
						key.Digest(ShapeKey(e));
					auto modelKey = key.Finish();
					auto &m = models[modelKey];
					if (!m.id)
					{
						m.bundle = b.id;
						GeometryKey id;
						id.U(b.id);
						id.Digest(modelKey);
						m.id = ID(id.Finish());
						m.primary = primary;
						m.sourceClass = cls;
						m.nature = a.nature;
						m.tier = a.tier;
						m.profile = a.profile;
						m.scenario = a.scenario;
						m.geometryKnown = true;
						m.fragmentUnion = !Zero(a.fragmentSet);
					}
					m.protectedAuthored |= a.protectedAuthored;
					m.tier = std::min(m.tier, a.tier);
					if (a.protectedAuthored && !Authored(Evidence(c, m.primary)))
						m.primary = primary;
					m.parts.push_back(t.ref);
					m.evidence.insert(m.evidence.end(), t.evidence.begin(), t.evidence.end());
					m.support.insert(m.support.end(), a.support.begin(), a.support.end());
					m.regions.insert(m.regions.end(), a.regions.begin(), a.regions.end());
				}
			}
			for (auto &pair : models)
			{
				auto &m = pair.second;
				UniqueRefs(m.evidence);
				UniqueSupport(m.support);
				Unique(m.regions);
				UniqueRefs(m.parts);
				// Exact duplicate parts from multiple providers count as one geometry component.
				std::map<Disk::Digest, size_t> partKeys;
				std::vector<Ref> parts;
				std::vector<bool> available;
				for (auto &part : m.parts)
				{
					const auto &v = c.providers[part.provider];
					const auto &candidate = v.candidates.data[part.index];
					const auto &e = v.evidence.data[candidate.primary];
					bool usable = InputAnnotation(c, part).supported && candidate.staticEligible &&
								  e.staticEligible && GeometryKnown(m.sourceClass, e);
					GeometryKey k;
					k.U(e.id);
					k.Digest(ShapeKey(e));
					auto inserted = partKeys.emplace(k.Finish(), parts.size());
					if (inserted.second)
					{
						parts.push_back(part);
						available.push_back(usable);
					}
					else if (usable && !available[inserted.first->second])
					{
						parts[inserted.first->second] = part;
						available[inserted.first->second] = true;
					}
				}
				m.geometryKnown = std::all_of(available.begin(), available.end(), [](bool v) { return v; });
				m.parts = std::move(parts);
				m.contradictoryPrior = m.protectedAuthored && models.size() > 1;
				b.models.push_back(m.id);
				b.regions.insert(b.regions.end(), m.regions.begin(), m.regions.end());
				c.models.push_back(std::move(m));
			}
			Unique(b.models);
			Unique(b.regions);
			const auto &first = *std::find_if(c.models.end() - models.size(), c.models.end(),
											  [&](const Model &m) { return m.id == b.models.front(); });
			const auto &e = Evidence(c, first.primary);
			if (r.regions && e.positionKnown)
			{
				RegionAssignment a;
				auto err = r.regions->Lookup(e.position, queryContext, a);
				if (!err)
					return err;
				if (a.state == RegionAssignmentState::Assigned)
				{
					b.home.kind = Home::Kind::Region;
					b.home.region = a.region;
					b.regions.push_back(a.region);
					Unique(b.regions);
				}
			}
			if (b.home.kind == Home::Kind::Unlocalized)
			{
				if (first.sourceClass == Class::Sun || first.sourceClass == Class::Sky)
					b.home.kind = Home::Kind::GlobalEnvironment;
				else if (e.positionKnown)
					b.home = Spatial(e.position, l.localCellWorld);
			}
			GeometryKey pk;
			pk.U(b.id);
			pk.U(3);
			b.provenance = ID(pk.Finish());
			Provenance p;
			p.id = b.provenance;
			p.kind = ProvenanceKind::Reconstruction;
			p.producer = "candidate-pool/1";
			p.method = b.sourceIdentityAttested
						   ? "explicit same-source evidence union; alternatives preserved"
						   : "exact-identity consolidation; multiplicity unresolved";
			for (const auto &z : b.evidence)
				p.parents.push_back(z.scoped);
			c.provenance.push_back(p);
			for (const auto &in : b.inputs)
			{
				Mapping z;
				z.input = in;
				z.bundle = b.id;
				const auto &a = InputAnnotation(c, in);
				z.action = !Zero(a.fragmentSet)
							   ? InputAction::FragmentPart
							   : b.models.size() > 1 ? InputAction::AlternativeModel
													 : b.inputs.size() > 1 ? InputAction::MergedEvidence
																		   : InputAction::Standalone;
				c.mapping.push_back(z);
			}
			c.bundles.push_back(std::move(b));
		}
		std::sort(c.bundles.begin(), c.bundles.end(),
				  [](const Bundle &a, const Bundle &b) { return a.id < b.id; });
		std::sort(c.models.begin(), c.models.end(),
				  [](const Model &a, const Model &b) { return a.id < b.id; });
		std::sort(c.mapping.begin(), c.mapping.end(),
				  [](const Mapping &a, const Mapping &b) { return RefLess(a.input, b.input); });
		// Bounded local merge proposals. No transitive proximity clustering.
		typedef std::tuple<int64_t, int64_t, int64_t> Cell;
		std::map<Cell, std::vector<size_t>> bins;
		std::vector<Vec3> pos(c.bundles.size());
		std::vector<const EmitterEvidence *> pe(c.bundles.size(), nullptr);
		std::vector<std::vector<Id>> supportIds(c.bundles.size());
		for (size_t i = 0; i < c.bundles.size(); ++i)
		{
			const auto &b = c.bundles[i];
			for (const Ref &input : b.inputs)
				for (const auto &s : InputAnnotation(c, input).support)
					supportIds[i].push_back(s.observation);
			Unique(supportIds[i]);
			const auto &in = b.inputs.front();
			const auto &v = c.providers[in.provider];
			const auto &e = v.evidence.data[v.candidates.data[in.index].primary];
			if (e.positionKnown)
			{
				pe[i] = &e;
				pos[i] = e.position;
				auto h = Spatial(e.position, l.possibleMatchWorld);
				bins[Cell(h.cell[0], h.cell[1], h.cell[2])].push_back(i);
			}
		}
		for (size_t i = 0; i < c.bundles.size(); ++i)
		{
			if (Stop(ctx))
				return Failure(Code::Cancelled, "candidate linking cancelled");
			if (!pe[i])
				continue;
			auto home = Spatial(pos[i], l.possibleMatchWorld);
			size_t neighbours = 0;
			for (int x = -1; x <= 1; ++x)
				for (int y = -1; y <= 1; ++y)
					for (int z = -1; z <= 1; ++z)
					{
						auto bin = bins.find(Cell(home.cell[0] + x, home.cell[1] + y, home.cell[2] + z));
						if (bin == bins.end())
							continue;
						auto begin = std::upper_bound(bin->second.begin(), bin->second.end(), i);
						for (auto it = begin; it != bin->second.end(); ++it)
						{
							if (neighbours >= l.maxNeighbours || c.report.pairTests >= l.maxPairTests ||
								c.links.size() >= l.maxLinks)
							{
								c.report.linksComplete = false;
								break;
							}
							++neighbours;
							++c.report.pairTests;
							size_t j = *it;
							double dist = Distance(pos[i], pos[j]);
							if (dist > l.possibleMatchWorld)
								continue;
							Link link;
							link.a = c.bundles[i].id;
							link.b = c.bundles[j].id;
							link.distanceWorld = dist;
							const auto &a = *pe[i], &b = *pe[j];
							link.directionCompatible = a.orientationKnown && b.orientationKnown &&
													   Aligned(a.outward, b.outward, l.directionCosine);
							const auto &ar = c.bundles[i].regions, &br = c.bundles[j].regions;
							auto intersect = [&](const std::vector<Id> &a, const std::vector<Id> &b) {
								size_t x = 0, y = 0;
								while (x < a.size() && y < b.size())
								{
									if (c.report.relationWork >= l.maxRelationWork)
									{
										link.relationsComplete = false;
										c.report.linksComplete = false;
										return false;
									}
									++c.report.relationWork;
									if (a[x] == b[y])
										return true;
									if (a[x] < b[y])
										++x;
									else
										++y;
								}
								return false;
							};
							link.sameRegion = intersect(ar, br);
							link.sharedSupport = intersect(supportIds[i], supportIds[j]);
							if (a.colourKnown && b.colourKnown && !(a.issues & ColourTransferUnknown) &&
								!(b.issues & ColourTransferUnknown) && a.unit == b.unit &&
								!Aligned(a.colour, b.colour, l.colourCosine))
								link.kind = LinkKind::ConflictingColour;
							else if (a.shapes != b.shapes)
								link.kind = LinkKind::ShapeAlternative;
							if (r.visibility && dist > 1e-6 && c.report.rayWork < l.maxRayWork)
							{
								RayQuery q;
								q.startWorld = pos[i];
								q.endWorld = pos[j];
								q.maxWork = std::min<uint64_t>(65536, l.maxRayWork - c.report.rayWork);
								RayResult result;
								auto err = r.visibility->Query(q, queryContext, result);
								++c.report.rays;
								c.report.rayWork += result.nodeTests + result.primitiveTests;
								if (!err && err.code != Code::Unavailable && err.code != Code::WorkLimit)
									return err;
								if (err &&
									(result.reliability == RayReliability::Geometric ||
									 result.reliability == RayReliability::Approximate) &&
									!(result.issues & PolicyAssumption))
									link.visibility = result.state;
								if (link.visibility == VisibilityState::Blocked)
									link.kind = LinkKind::Blocked;
								else if (link.visibility == VisibilityState::Unknown)
									link.kind = LinkKind::VisibilityUnknown;
							}
							c.links.push_back(link);
						}
					}
		}
		c.report.bundles = c.bundles.size();
		c.report.models = c.models.size();
		c.report.merged = c.report.inputs - c.report.bundles;
		for (const auto &b : c.bundles)
			c.report.alternatives += b.models.size() > 1 ? b.models.size() - 1 : 0;
		for (const auto &m : c.models)
			if (m.fragmentUnion)
				c.report.fragmentParts += m.parts.size();
		if (!c.report.linksComplete)
			c.report.diagnostics.push_back("Local proposal work/links truncated; ALL source records "
										   "retained. No optical claim inferred.");
		c.report.diagnostics.push_back("Candidate work catalog only: no fitted intensities, calibrated "
									   "confidence or recovered-light capability.");
		// IDs are persistent keys, so collisions must fail rather than alias records.
		std::set<Id> ids;
		for (const auto &p : c.provenance)
			if (!ids.insert(p.id).second)
				return Failure(Code::InvalidInput, "provenance ID collision");
		ids.clear();
		for (const auto &b : c.bundles)
			if (!ids.insert(b.id).second)
				return Failure(Code::InvalidInput, "bundle ID collision");
		ids.clear();
		for (const auto &m : c.models)
			if (!ids.insert(m.id).second)
				return Failure(Code::InvalidInput, "model ID collision");
		out = std::move(c);
		return {};
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "candidate consolidation allocation failed");
	}
}

namespace
{
Disk::Digest PolicyKey(const Budget &b)
{
	GeometryKey k;
	k.U(kVersion);
	Text(k, b.profile);
	k.Digest(b.scenario);
	k.U(b.classes);
	return k.Finish();
}
uint64_t VisibleCoverage(const Model &m)
{
	std::set<Id> s;
	for (const auto &z : m.support)
		if (z.visibility == VisibilityState::Visible)
			s.insert(z.surface ? z.surface : z.observation);
	return s.size();
}
} // namespace
Error Select(const Catalog &c, const Budget &b, const ReadContext &ctx, Cursor &cursor, Selection &out)
{
	try
	{
		out = {};
		if (Stop(ctx))
			return Failure(Code::Cancelled, "candidate selection cancelled");
		if (Zero(c.key) || !b.perHome || !b.maxTickets || !b.maxPartsPerTicket || !b.maxGeometryWork)
			return Failure(Code::InvalidInput, "invalid catalog/wave budget");
		uint64_t bytes = 0, cap = ctx.maxBytes ? std::min(ctx.maxBytes, b.maxBytes) : b.maxBytes;
		if (b.more.size() > 4096)
			return Failure(Code::WorkLimit, "too many local expansion requests");
		if (!Add(bytes, b.profile.size(), cap))
			return Failure(Code::MemoryLimit, "wave profile text limit");
		for (const auto &d : b.more)
			if (d.reason.size() > 4096 || !Add(bytes, d.reason.size() * 2, cap))
				return Failure(Code::MemoryLimit, "wave expansion text limit");
		uint64_t supportCount = 0;
		for (const auto &m : c.models)
			if (!Add(supportCount, m.support.size(), cap / 64))
				return Failure(Code::MemoryLimit, "wave support scratch limit");
		if (!Product(bytes, c.models.size(), 256, cap) || !Product(bytes, supportCount, 64, cap) ||
			!Product(bytes, b.maxTickets, sizeof(Ticket) + 64, cap) ||
			!Product(bytes, b.more.size(), sizeof(Demand) + 128, cap))
			return Failure(Code::MemoryLimit, "wave memory preflight");
		Cursor next = cursor;
		auto policy = PolicyKey(b);
		if (!Zero(next.catalog) &&
			(next.catalog != c.key || next.policy != policy || next.nextPart.size() != c.models.size()))
			return Failure(Code::InvalidState,
						   "cursor belongs to a different catalog/context; reset explicitly");
		if (Zero(next.catalog))
		{
			next.catalog = c.key;
			next.policy = policy;
			next.nextPart.assign(c.models.size(), 0);
		}
		std::map<Id, const Bundle *> bundles;
		for (const auto &x : c.bundles)
			bundles[x.id] = &x;
		for (const auto &m : c.models)
		{
			if (!m.id || size_t(m.sourceClass) >= size_t(Class::Count) || !bundles.count(m.bundle) ||
				m.parts.empty())
				return Failure(Code::InvalidInput, "invalid consolidated model");
			for (const auto &p : m.parts)
				if (p.provider >= c.providers.size() || p.index >= c.providers[p.provider].annotations.count)
					return Failure(Code::InvalidInput, "invalid consolidated model part");
		}
		std::set<std::string> profiles;
		std::set<Disk::Digest> scenarios;
		for (const auto &m : c.models)
		{
			if (!m.profile.empty())
				profiles.insert(m.profile);
			if (!Zero(m.scenario))
				scenarios.insert(m.scenario);
		}
		if ((!b.profile.empty() && !profiles.count(b.profile)) ||
			(!Zero(b.scenario) && !scenarios.count(b.scenario)))
			return Failure(Code::InvalidInput, "selected profile/scenario absent");
		std::vector<Deferred> reason(c.models.size(), Deferred::WaveBudget);
		std::vector<bool> pending(c.models.size(), false), eligible(c.models.size(), false);
		std::vector<uint64_t> coverage(c.models.size(), 0);
		std::map<Home, std::vector<size_t>, decltype(&HomeLess)> homes(&HomeLess);
		for (size_t i = 0; i < c.models.size(); ++i)
		{
			const auto &m = c.models[i];
			if (next.nextPart[i] > m.parts.size())
				return Failure(Code::InvalidState, "invalid wave cursor progress");
			if (next.nextPart[i] == m.parts.size())
				continue;
			pending[i] = true;
			if ((!m.profile.empty() && ((b.profile.empty() && profiles.size() > 1) ||
										(!b.profile.empty() && m.profile != b.profile))) ||
				(!Zero(m.scenario) && ((Zero(b.scenario) && scenarios.size() > 1) ||
									   (!Zero(b.scenario) && m.scenario != b.scenario))))
				reason[i] = Deferred::Context;
			else if (!m.geometryKnown)
				reason[i] = Deferred::UnsupportedGeometry;
			else if (!(b.classes & (1u << uint32_t(m.sourceClass))))
				reason[i] = Deferred::FeatureDisabled;
			else
			{
				eligible[i] = true;
				coverage[i] = VisibleCoverage(m);
				homes[bundles.at(m.bundle)->home].push_back(i);
			}
		}
		std::vector<Home> order;
		for (auto &h : homes)
		{
			order.push_back(h.first);
			auto &q = h.second;
			std::sort(q.begin(), q.end(), [&](size_t a, size_t z) {
				const auto &x = c.models[a], &y = c.models[z];
				if (x.protectedAuthored != y.protectedAuthored)
					return x.protectedAuthored > y.protectedAuthored;
				if (x.tier != y.tier)
					return x.tier < y.tier;
				auto vx = coverage[a], vy = coverage[z];
				if (vx != vy)
					return vx > vy;
				return x.id < y.id;
			});
		}
		Selection result;
		std::set<Id> picked;
		size_t start = order.empty() ? 0 : next.nextHome % order.size();
		if (b.more.size() > 4096)
			return Failure(Code::WorkLimit, "too many local expansion requests");
		for (const auto &d : b.more)
			if (d.reason.empty() || d.reason.size() > 4096 || d.extraPerClass > 1000000)
				return Failure(Code::InvalidInput, "invalid expansion demand");
		// Visit each home at most once per wave. The starting home rotates even when a
		// global safety budget is exhausted, preventing an area-heavy prefix monopoly.
		size_t visited = 0;
		for (; visited < order.size(); ++visited)
		{
			if (Stop(ctx))
				return Failure(Code::Cancelled, "candidate selection cancelled");
			size_t index = (start + visited) % order.size();
			const Home &home = order[index];
			const auto &q = homes.at(home);
			std::vector<const Demand *> applicable;
			for (const auto &d : b.more)
			{
				bool match = SameHome(d.home, home);
				if (!match && d.home.kind == Home::Kind::Region)
					for (size_t i : q)
					{
						const auto &regions = bundles.at(c.models[i].bundle)->regions;
						if (std::binary_search(regions.begin(), regions.end(), d.home.region))
						{
							match = true;
							break;
						}
					}
				if (match)
					applicable.push_back(&d);
			}
			size_t quota[size_t(Class::Count)];
			for (size_t k = 0; k < size_t(Class::Count); ++k)
			{
				quota[k] = b.perClass[k];
				for (const auto *d : applicable)
					if (d->classes & (1u << k))
					{
						if (d->extraPerClass > std::numeric_limits<size_t>::max() - quota[k])
							return Failure(Code::InvalidInput, "expansion quota overflow");
						quota[k] += d->extraPerClass;
					}
			}
			size_t counts[size_t(Class::Count)] = {}, homeCount = 0;
			std::set<Id> covered;
			auto admit = [&](size_t i) {
				const auto &m = c.models[i];
				uint64_t work = 0;
				size_t first = size_t(next.nextPart[i]), parts = 0;
				while (first + parts < m.parts.size() && parts < b.maxPartsPerTicket)
				{
					uint64_t w = InputAnnotation(c, m.parts[first + parts]).geometryWork;
					if (w > b.maxGeometryWork || work > b.maxGeometryWork - w ||
						result.geometryWork > b.maxGeometryWork - work - w)
						break;
					work += w;
					++parts;
				}
				if (!parts)
				{
					reason[i] = InputAnnotation(c, m.parts[first]).geometryWork > b.maxGeometryWork
									? Deferred::Oversize
									: Deferred::WaveBudget;
					return false;
				}
				if (result.tickets.size() >= b.maxTickets)
					return false;
				Ticket t;
				t.model = m.id;
				t.bundle = m.bundle;
				t.firstPart = first;
				t.partCount = parts;
				t.geometryWork = work;
				t.completesModel = first + parts == m.parts.size();
				t.protectedAuthored = m.protectedAuthored;
				result.tickets.push_back(t);
				result.geometryWork += work;
				next.nextPart[i] += parts;
				pending[i] = next.nextPart[i] < m.parts.size();
				if (t.completesModel)
					++result.completedModels;
				picked.insert(m.bundle);
				++counts[size_t(m.sourceClass)];
				++homeCount;
				for (const auto &s : m.support)
					if (s.visibility == VisibilityState::Visible)
						covered.insert(s.surface ? s.surface : s.observation);
				return true;
			};
			bool progress = true;
			while (progress && homeCount < b.perHome)
			{
				progress = false;
				for (size_t cls = 0; cls < size_t(Class::Count) && homeCount < b.perHome; ++cls)
				{
					if (counts[cls] >= quota[cls])
						continue;
					for (size_t i : q)
					{
						const auto &m = c.models[i];
						if (size_t(m.sourceClass) != cls || !pending[i] || picked.count(m.bundle))
							continue;
						if (admit(i))
						{
							progress = true;
							break;
						}
					}
				}
				if (result.tickets.size() >= b.maxTickets || result.geometryWork >= b.maxGeometryWork)
					break;
			}
			// Adaptive extra quota: only independently known-visible support not already
			// covered in this home. No brightness/confidence manipulation or hidden RGB.
			for (size_t bonus = 0; bonus < b.coverageBonusPerHome && homeCount < b.perHome &&
								   result.tickets.size() < b.maxTickets;
				 ++bonus)
			{
				size_t best = c.models.size(), gain = 0;
				for (size_t i : q)
				{
					const auto &m = c.models[i];
					if (!pending[i] || picked.count(m.bundle) || !quota[size_t(m.sourceClass)])
						continue;
					std::set<Id> extra;
					for (const auto &s : m.support)
						if (s.visibility == VisibilityState::Visible)
						{
							Id id = s.surface ? s.surface : s.observation;
							if (!covered.count(id))
								extra.insert(id);
						}
					if (extra.size() > gain)
					{
						gain = extra.size();
						best = i;
					}
				}
				if (best == c.models.size() || !admit(best))
					break;
			}
			for (size_t i : q)
				if (pending[i] && reason[i] == Deferred::WaveBudget)
				{
					const auto &m = c.models[i];
					if (picked.count(m.bundle))
						reason[i] = next.nextPart[i] > 0 ? Deferred::PartialGeometry : Deferred::Alternative;
					else if (counts[size_t(m.sourceClass)] >= quota[size_t(m.sourceClass)] ||
							 homeCount >= b.perHome)
						reason[i] = m.protectedAuthored ? Deferred::ProtectedOverflow : Deferred::LocalQuota;
				}
			if (result.tickets.size() >= b.maxTickets || result.geometryWork >= b.maxGeometryWork)
			{
				++visited;
				break;
			}
		}
		if (!order.empty())
			next.nextHome = (start + std::max<size_t>(1, visited)) % order.size();
		bool any = false;
		for (size_t i = 0; i < c.models.size(); ++i)
			if (pending[i])
			{
				Deferral d;
				d.model = c.models[i].id;
				d.bundle = c.models[i].bundle;
				d.reason = reason[i];
				result.deferred.push_back(d);
				any |= eligible[i];
			}
		result.exhausted = !any;
		result.diagnostics.push_back("Tickets are alternative optimisation work, not simultaneous lights. "
									 "Deferred records remain immutable and requestable.");
		if (!b.more.empty())
			for (const auto &d : b.more)
				result.diagnostics.push_back("Expansion: " + d.reason);
		if (Stop(ctx))
			return Failure(Code::Cancelled, "candidate selection cancelled before publication");
		cursor = std::move(next);
		out = std::move(result);
		return {};
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "candidate selection allocation failed");
	}
}

const char *ClassName(Class c)
{
	const char *n[] = {"point", "spot", "area", "tube", "sun", "sky", "transport", "unknown"};
	return size_t(c) < size_t(Class::Count) ? n[size_t(c)] : "invalid";
}
const char *DeferredName(Deferred d)
{
	const char *n[] = {"conditional-context", "unsupported-geometry", "class-disabled",
					   "local-quota",		  "wave-budget",		  "protected-overflow",
					   "oversize-part",		  "alternative-model",	  "partial-geometry"};
	return size_t(d) < 9 ? n[size_t(d)] : "invalid";
}
std::vector<Statistics> Summarize(const Catalog &c, const Selection &s)
{
	std::map<Id, Home> homes;
	for (const auto &b : c.bundles)
		homes[b.id] = b.home;
	std::set<Id> selected;
	for (const auto &t : s.tickets)
		selected.insert(t.model);
	std::map<Id, std::string> reasons;
	for (const auto &d : s.deferred)
		reasons[d.model] = DeferredName(d.reason);
	std::vector<Statistics> rows;
	for (const auto &m : c.models)
	{
		std::set<std::string> providers;
		for (const auto &ref : m.evidence)
			providers.insert(c.providers[ref.provider].name);
		for (const auto &p : providers)
		{
			Statistics row;
			row.home = homes.at(m.bundle);
			row.sourceClass = m.sourceClass;
			row.provider = p;
			row.reason = selected.count(m.id)
							 ? "selected-work"
							 : reasons.count(m.id) ? reasons.at(m.id) : "previously-completed";
			row.models = 1;
			row.selected = selected.count(m.id);
			row.protectedModels = m.protectedAuthored;
			row.parts = m.parts.size();
			rows.push_back(row);
		}
	}
	auto less = [](const Statistics &a, const Statistics &b) {
		if (!SameHome(a.home, b.home))
			return HomeLess(a.home, b.home);
		return std::tie(a.sourceClass, a.provider, a.reason) < std::tie(b.sourceClass, b.provider, b.reason);
	};
	std::sort(rows.begin(), rows.end(), less);
	std::vector<Statistics> out;
	for (const auto &row : rows)
	{
		if (!out.empty() && !less(out.back(), row) && !less(row, out.back()))
		{
			auto &r = out.back();
			r.models += row.models;
			r.selected += row.selected;
			r.protectedModels += row.protectedModels;
			r.parts += row.parts;
		}
		else
			out.push_back(row);
	}
	return out;
}
} // namespace Candidates
} // namespace Core
} // namespace Rend2LightRecon
